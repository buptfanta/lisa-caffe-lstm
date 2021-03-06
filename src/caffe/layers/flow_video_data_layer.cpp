#include <opencv2/core/core.hpp>

#include <fstream>  // NOLINT(readability/streams)
#include <iostream>  // NOLINT(readability/streams)
#include <string>
#include <utility>
#include <vector>

#include "caffe/data_layers.hpp"
#include "caffe/layer.hpp"
#include "caffe/util/benchmark.hpp"
#include "caffe/util/io.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/util/rng.hpp"

namespace caffe {

template <typename Dtype>
FlowVideoDataLayer<Dtype>::~FlowVideoDataLayer<Dtype>() {
  this->JoinPrefetchThread();
}

template <typename Dtype>
float FlowVideoDataLayer<Dtype>:: random(float start, float end)
{
        return start+(end-start)*caffe_rng_rand()/(UINT_MAX + 1.0);
}


template <typename Dtype>
int FlowVideoDataLayer<Dtype>::myrandomdata (int i) { return caffe_rng_rand()%i;}

template <typename Dtype>
void FlowVideoDataLayer<Dtype>::DataLayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  const int new_height = this->layer_param_.image_data_param().new_height();
  const int new_width  = this->layer_param_.image_data_param().new_width();
  const bool is_color  = this->layer_param_.image_data_param().is_color();
  string root_folder = this->layer_param_.image_data_param().root_folder();
  const int pairsize = this->layer_param_.image_data_param().pair_size();
  const int frame_num = this->layer_param_.image_data_param().frame_num();
  const int pairsize_sub = this->layer_param_.image_data_param().pair_size_sub();
  const int read_mode = this->layer_param_.image_data_param().read_mode();
  const bool multiview  = this->layer_param_.image_data_param().multiview();


  CHECK((new_height == 0 && new_width == 0) ||
      (new_height > 0 && new_width > 0)) << "Current implementation requires "
      "new_height and new_width to be set at the same time.";
  // Read the file with filenames and labels
  const string& source = this->layer_param_.image_data_param().source();
  LOG(INFO) << "Opening file " << source;
  std::ifstream infile(source.c_str());
  string filename;
  int label, h_off, w_off, do_mirror;
  while (infile >> filename >> label) {
    lines_.push_back(std::make_pair(filename, label));

    if (multiview)
    {
    	infile >> h_off >> w_off >> do_mirror;
    	vector<int> tparams;
    	tparams.push_back(h_off);
    	tparams.push_back(w_off);
    	tparams.push_back(do_mirror);
    	multiview_params_.push_back(tparams);
    }

  }

  if (this->layer_param_.image_data_param().shuffle()) {
    // randomly shuffle data
    LOG(INFO) << "Shuffling data";
    const unsigned int prefetch_rng_seed = caffe_rng_rand();
    prefetch_rng_.reset(new Caffe::RNG(prefetch_rng_seed));
    ShuffleImages();
  }
  LOG(INFO) << "A total of " << lines_.size() << " images.";

  lines_id_ = 0;
  // Check if we would need to randomly skip a few data points
  if (this->layer_param_.image_data_param().rand_skip()) {
    unsigned int skip = caffe_rng_rand() %
        this->layer_param_.image_data_param().rand_skip();
    LOG(INFO) << "Skipping first " << skip << " data points.";
    CHECK_GT(lines_.size(), skip) << "Not enough points to skip";
    lines_id_ = skip;
  }
  // Read an image, and use it to initialize the top blob.
  cv::Mat cv_img = ReadImageToCVMat(root_folder + lines_[lines_id_ + 1].first,
                                    new_height, new_width, is_color);
  const int channels = cv_img.channels()  * frame_num * pairsize_sub;
  const int height = cv_img.rows;
  const int width = cv_img.cols;
  // image
  const int crop_size = this->layer_param_.transform_param().crop_size();
  const int batch_size = this->layer_param_.image_data_param().batch_size();
  if (crop_size > 0) {
    top[0]->Reshape(batch_size, channels, crop_size, crop_size);
    this->prefetch_data_.Reshape(batch_size, channels, crop_size, crop_size);
    this->transformed_data_.Reshape(1, channels , crop_size, crop_size);
  } else {
    top[0]->Reshape(batch_size, channels, height, width);
    this->prefetch_data_.Reshape(batch_size, channels, height, width);
    this->transformed_data_.Reshape(1, channels , height, width);
  }
  LOG(INFO) << "output data size: " << top[0]->num() << ","
      << top[0]->channels() << "," << top[0]->height() << ","
      << top[0]->width();
  // label
  //vector<int> label_shape(1, batch_size / sidesize);
  top[1]->Reshape(batch_size, 2, 1, 1);
  this->prefetch_label_.Reshape(batch_size, 2, 1, 1);
}

template <typename Dtype>
void FlowVideoDataLayer<Dtype>::ShuffleImages() {
  // caffe::rng_t* prefetch_rng = static_cast<caffe::rng_t*>(prefetch_rng_->generator());
  // shuffle(lines_.begin(), lines_.end(), prefetch_rng);
  const int num_images = lines_.size();
  DLOG(INFO) << "My Shuffle.";
  vector<std::pair<std::string, int> > tlines_;
  vector<int> tnum;

  int i = 0;
  while (i < num_images)
  {
	  tnum.push_back(i);
	  i += lines_[i].second + 1;
	  // debug
	  //if (tnum.size() > 10) break;
  }
  // debug
  //for(int i = 0; i < tnum.size(); i ++) printf("%d ", tnum[i]);

  std::random_shuffle(tnum.begin(), tnum.end(), FlowVideoDataLayer<Dtype>::myrandomdata);
  // debug
  //for(int i = 0; i < tnum.size(); i ++) printf("%d ", tnum[i]);

  tlines_.clear();
  for(int i = 0; i < tnum.size(); i ++)
  {
	  int videoframes = lines_[tnum[i]].second;
	  for(int j = 0; j < videoframes + 1; j ++)
		  tlines_.push_back(lines_[tnum[i] + j]);
  }
  lines_ = tlines_;

  // debug
  //for(int i = 0; i < lines_.size(); i += 10) printf("%s\n", lines_[i].first.c_str() );
}


template <typename Dtype>
string FlowVideoDataLayer<Dtype>::AddFrame(string fname, int id)
{
	int len = fname.size(), i, startid, endid;
	for(i = len - 1; i >= 0; i --)
	{
		if (fname[i] == '/')
		{
			startid = i + 1;
			break;
		}
	}
	int num = 0;
	for(i = startid; i < len; i ++)
	{
		if(fname[i] >= '0' && fname[i] <= '9')
		{
			endid = i;
			num = num * 10 + fname[i] - '0';
		}
		else
		{
			break;
		}
	}
	num = num + id;
	char fname2[1010];
	string prefix_s = fname.substr(0, startid);
	string suffix_s = fname.substr(endid + 1, len - endid - 1);
	sprintf(fname2, "%s%04d%s", prefix_s.c_str(), num, suffix_s.c_str());
	string now_fname = fname2;
	return now_fname;

}



template <typename Dtype>
void FlowVideoDataLayer<Dtype>::process_input2(const Mat &cv_img, float bound, bool domirror, Datum* datum,  int read_mode)
{
  // read_mode = 1: restore optical flow
  // read_mode = 2: scale to 0 - 255 : min(128.0 * (u.x / maxrad) + 128.0, 255.0)
  // read_mode = 3: scale to 0 - 255 : (u.x - min) / (max - min) * 255
  int channels = cv_img.channels();
  datum->set_channels(channels );
  datum->set_height(cv_img.rows);
  datum->set_width(cv_img.cols);
  datum->clear_data();
  datum->clear_float_data();
  datum->clear_label();

  float lowbound = - bound;
  float upbound = bound;

  vector<float> tdata;

	for (int h = 0; h < cv_img.rows; ++h)
	{
	  for (int w = 0; w < cv_img.cols; ++w)
	  {
		float tnum = static_cast<uint8_t>(cv_img.at<uchar>(h, w));
		tnum = tnum / 255 * (upbound - lowbound) + lowbound;
		if (domirror) tnum = - tnum;
		tdata.push_back(tnum);
	  }
	}

  if(read_mode == 4)
  {
    for(int i = 0; i < tdata.size(); i ++)
      datum->add_float_data(tdata[i]);
  }
  else if (read_mode == 5)
  {
    float minnum = lowbound, maxnum = upbound;
    for(int i = 0; i < tdata.size(); i ++)
    {
      float tnum = tdata[i];
      if (tnum > maxnum) tnum = 255;
      else if(tnum < minnum) tnum = 0;
      else
      {
    	  tnum = min( (tnum - minnum) / (maxnum - minnum) * 255.0, 255.0);
      }
      datum->add_float_data(tnum);
    }
  }



}



// This function is used to create a thread that prefetches the data.
template <typename Dtype>
void FlowVideoDataLayer<Dtype>::InternalThreadEntry() {
  CPUTimer batch_timer;
  batch_timer.Start();
  double read_time = 0;
  double trans_time = 0;
  CPUTimer timer;
  CHECK(this->prefetch_data_.count());
  CHECK(this->transformed_data_.count());
  ImageDataParameter image_data_param = this->layer_param_.image_data_param();
  const int batch_size = image_data_param.batch_size();
  const int new_height = image_data_param.new_height();
  const int new_width = image_data_param.new_width();
  const int crop_size = this->layer_param_.transform_param().crop_size();
  const bool is_color = image_data_param.is_color();
  const int pairsize = image_data_param.pair_size();
  const int classnum = image_data_param.classnum();
  const bool rand_frame = image_data_param.rand_frame();
  const int range_scale = image_data_param.range_scale();
  const int color_aug = image_data_param.color_aug();

  const int frame_num = image_data_param.frame_num();
  const int pairsize_sub = image_data_param.pair_size_sub();
  const int read_mode = image_data_param.read_mode();
  const int bound = image_data_param.bound();
  const bool multiview  = this->layer_param_.image_data_param().multiview();

  string root_folder = image_data_param.root_folder();

  // Reshape on single input batches for inputs of varying dimension.
  if (batch_size == 1 && crop_size == 0 && new_height == 0 && new_width == 0) {
    cv::Mat cv_img = ReadImageToCVMat(root_folder + lines_[lines_id_].first,
        0, 0, is_color);
    this->prefetch_data_.Reshape(1, cv_img.channels(),
        cv_img.rows, cv_img.cols);
    this->transformed_data_.Reshape(1, cv_img.channels(),
        cv_img.rows, cv_img.cols);
  }

  Dtype* prefetch_data = this->prefetch_data_.mutable_cpu_data();
  Dtype* prefetch_label = this->prefetch_label_.mutable_cpu_data();

  // datum scales
  const int lines_size = lines_.size();
  CHECK_EQ(batch_size % pairsize, 0);
  int video_size = batch_size / pairsize;
  for (int item_id = 0; item_id < batch_size; item_id += pairsize) {
    // get a blob
    timer.Start();
    CHECK_GT(lines_size, lines_id_);
    int video_id = item_id / pairsize;

    int img_channel = this->prefetch_data_.channels() / (pairsize_sub * frame_num);
    int offset = 0;

    read_time += timer.MicroSeconds();
    timer.Start();

    int video_frames = lines_[lines_id_].second / pairsize_sub;
    lines_id_ ++;
    float gap_num = (Dtype)(video_frames) / (pairsize - 1 );

    CHECK( gap_num > 0);
    int perturb_range = std::max( (int)(gap_num / 2) , 1);
    // max range < range_scale
    perturb_range = std::min(perturb_range, range_scale);

    // color augmentation
    vector<float> col_range;
    for(int i = 0; i < img_channel; i ++) col_range.push_back(1.0);

    int h_off = - 1, w_off = -1, do_mirror = -1;
  	if (multiview)
  	{
  		h_off = multiview_params_[lines_id_][0];
  		w_off = multiview_params_[lines_id_][1];
  		do_mirror = multiview_params_[lines_id_][2];
  	}

    for(int i = 0; i < pairsize; i ++)
    {
    	int id = i * gap_num;
    	int rand_range = caffe_rng_rand() % perturb_range - perturb_range / 2;
    	if (rand_frame == false)
    	{
    		rand_range = 0;
    	}
    	id += rand_range;
    	id = std::max(id, 0);
    	id = std::min(id, video_frames - 1);
    	int now_itemid = video_size * i + video_id;

    	for (int frame_id = 0; frame_id < frame_num; frame_id ++)
		{
			for(int pair_id = 0; pair_id < pairsize_sub; pair_id ++)
			{
				int nowid = lines_id_ + id * pairsize_sub + pair_id;
				string fname =  lines_[nowid].first;
				fname = AddFrame(fname, frame_id);
				cv::Mat cv_img = ReadImageToCVMat(root_folder + fname,
				  					new_height, new_width, is_color);
				CHECK(cv_img.data) << "Could not load " << fname;
		        // debug
		        //LOG(INFO) << "Frame number: " << video_frames << "side_num: " << side_num;
		        //if (item_id < 3 * sidesize || item_id > batch_size - 3 * sidesize)
		        //LOG(INFO) << "fretch" << lines_[nowid].first << h_off << w_off << do_mirror << " " << lines_[nowid].second << " " << item_id;

			    offset = this->prefetch_data_.offset(now_itemid, img_channel * (frame_id * pairsize_sub + pair_id));
			    this->transformed_data_.set_cpu_data(prefetch_data + offset);
			    if(color_aug)
				{
					for(int i = 0; i < img_channel; i ++)
						col_range[i] = random(0.8,1.2);
				}
			    if (read_mode == 0)
			    {
			    	this->data_transformer_->Transform(cv_img, &(this->transformed_data_), h_off, w_off, do_mirror, col_range);
			    }
			    else if (read_mode >=4 && read_mode <=5)
				{
					Datum datum;
					if (do_mirror < 0)
					{
					  do_mirror = this->data_transformer_->GetMirror( );
					}
					bool now_do_mirror = do_mirror;
					if (pair_id == 1) now_do_mirror = 0; // dy do not do mirror
					process_input2(cv_img, bound, now_do_mirror, &datum,  read_mode);
					this->data_transformer_->Transform(datum, &(this->transformed_data_), h_off, w_off, do_mirror, col_range );
				}
			}
		}

    	prefetch_label[ (now_itemid) * 2] = lines_[lines_id_].second;
		if (pairsize == 0)
			prefetch_label[(now_itemid) * 2 + 1] = 0;
		else
			prefetch_label[(now_itemid) * 2 + 1] = 1;


    }


    lines_id_ += video_frames * pairsize_sub;

    trans_time += timer.MicroSeconds();

    // go to the next iter
    if (lines_id_ >= lines_size) {
      // We have reached the end. Restart from the first.
      DLOG(INFO) << "Restarting data prefetching from start.";
      lines_id_ = 0;
      if (this->layer_param_.image_data_param().shuffle()) {
        ShuffleImages();
      }
    }
  }
  batch_timer.Stop();
  DLOG(INFO) << "Prefetch batch: " << batch_timer.MilliSeconds() << " ms.";
  DLOG(INFO) << "     Read time: " << read_time / 1000 << " ms.";
  DLOG(INFO) << "Transform time: " << trans_time / 1000 << " ms.";
}



INSTANTIATE_CLASS(FlowVideoDataLayer);
REGISTER_LAYER_CLASS(FlowVideoData);

}  // namespace caffe
