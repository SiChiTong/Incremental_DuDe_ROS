//ROS
#include "ros/ros.h"
#include "nav_msgs/GetMap.h"
#include "std_msgs/String.h"

//openCV
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

//DuDe
#include "inc_decomp.hpp"





class ROS_handler
{
	ros::NodeHandle n;
	
	image_transport::ImageTransport it_;
	image_transport::Subscriber image_sub_;
	image_transport::Publisher image_pub_;	
	cv_bridge::CvImagePtr cv_ptr;
		
	std::string mapname_;
	ros::Subscriber map_sub_;	
	ros::Subscriber chat_sub_;	
	ros::Timer timer;
			
	float Decomp_threshold_;
	Incremental_Decomposer inc_decomp;
	Stable_graph Stable;

	cv::Mat image2save_clean, image2save_black, image2save_Inc;
	
	cv::Mat previous_image;
	std::vector <cv::Vec3b> colormap;

	std::vector <double> clean_time_vector, decomp_time_vector, paint_time_vector, complete_time_vector;

	
	public:
		ROS_handler(const std::string& mapname, float threshold) : mapname_(mapname),  it_(n), Decomp_threshold_(threshold)
		{
			ROS_INFO("Waiting for the map");
			map_sub_ = n.subscribe("map", 2, &ROS_handler::mapCallback, this); //mapname_ to include different name
			chat_sub_ = n.subscribe("chatter", 1, &ROS_handler::chatCallback, this); 
			timer = n.createTimer(ros::Duration(0.5), &ROS_handler::metronomeCallback, this);

			image_pub_ = it_.advertise("/tagged_image", 1);			
			cv_ptr.reset (new cv_bridge::CvImage);
//			cv_ptr->encoding = "mono8";
			cv_ptr->encoding = "bgr8";
						
			
			cv::Vec3b black(208, 208, 208);
			colormap.push_back(black);
			for(int i=0;i<= 500; i++){
				cv::Vec3b color(rand() % 255,rand() % 255,rand() % 255);
				colormap.push_back(color);
			}
						
						
						
		}


/////////////////////////////	
// ROS CALLBACKS			
////////////////////////////////		

		void mapCallback(const nav_msgs::OccupancyGridConstPtr& map)
		{
			double begin_process, end_process, begin_whole, occupancy_time, decompose_time, drawPublish_time, whole_time;
			begin_whole = begin_process = getTime();
			
			ROS_INFO("Received a %d X %d map @ %.3f m/pix", map->info.width, map->info.height, map->info.resolution);

		///////////////////////Occupancy to clean image	
			cv::Mat grad, img(map->info.height, map->info.width, CV_8U);
			img.data = (unsigned char *)(&(map->data[0]) );
			cv::Mat received_image = img.clone();
			
			float pixel_Tau = Decomp_threshold_ / map->info.resolution;				
			cv_ptr->header = map->header;
			cv::Point2f origin = cv::Point2f(map->info.origin.position.x, map->info.origin.position.y);

			
			cv::Rect first_rect = find_image_bounding_Rect(received_image); 
			float rect_area = (first_rect.height)*(first_rect.width);
			float img_area = (received_image.rows) * (received_image.cols);
			cout <<"Area Ratio " <<  ( rect_area/img_area  )*100 <<"% "<< endl;
			
			cv::Mat cropped_img;
			received_image(first_rect).copyTo(cropped_img); /////////// Cut the relevant image

			cv::Mat image_cleaned = cv::Mat::zeros(received_image.size(), CV_8UC1);
			cv::Mat black_image   = cv::Mat::zeros(received_image.size(), CV_8UC1);

//*
			cv::Mat black_image2, image_cleaned2 = clean_image2(cropped_img, black_image2);
			

			
			image_cleaned2.copyTo(image_cleaned (first_rect));
			black_image2.copyTo(black_image (first_rect));
//*/			
//			image_cleaned = clean_image2(received_image, black_image);
//			image2save_clean = image_cleaned.clone();						
			cv::flip(image_cleaned, image2save_clean,0);
						
			end_process = getTime();	occupancy_time = end_process - begin_process;


//			Incremental_Decomposer inc_decomp_batch; //Uncoment to batch

		///////////////////////// Decompose Image
			begin_process = getTime();
			
		    try{
				Stable = inc_decomp.decompose_image(image_cleaned, pixel_Tau, origin, map->info.resolution);
//				Stable = inc_decomp_batch.decompose_image(image_cleaned, pixel_Tau, origin, map->info.resolution); //Uncoment to batch
			}
			catch (...)  {			}

			
			end_process = getTime();	decompose_time = end_process - begin_process;





			
		////////////Draw Image & publish
		
			begin_process = getTime();
/*
	//		cv::Mat croppedRef(Colored_Frontier, resize_rect);			
			cv::flip(black_image, black_image,0);  cv::Mat big = Stable.draw_stable_contour() & ~black_image;

			cout << "Rect "<< first_rect << endl;

			big(first_rect).copyTo(grad);

//*/	
			cv::flip(black_image, black_image,0);
			image2save_black = black_image.clone();
			grad = Stable.draw_stable_contour() & ~black_image;	

			inc_decomp.frontiers_in_map(grad, received_image);


//			grad = image_cleaned;	

//			std::vector <cv::Vec3b> new_colormap;
			if(colormap.size()>=0){
//				colormap = convert_image_to_color( grad, &image2save_Inc);
				image2save_Inc = paint_image_colormap(grad, colormap);
			}
			else{
//				new_colormap = convert_image_to_color( grad, &image2save_Inc);
				std::map<int,int> original_map = compare_images(previous_image, grad );
				paint_with_previous_color( grad, &colormap,  original_map, &image2save_Inc);
				

			}
//									std::cerr<<"Stable.Region_centroid.size()  outside"<<Stable.Region_centroid.size()<<std::endl;
			
			previous_image = grad.clone();


//			image2save_Inc = grad.clone();

//			cv_ptr->encoding = sensor_msgs::image_encodings::TYPE_32FC1;			grad.convertTo(grad, CV_32F);
//			cv_ptr->encoding = sensor_msgs::image_encodings::TYPE_8UC1;			grad.convertTo(grad, CV_8UC1);
//			grad.copyTo(cv_ptr->image);////most important
			image2save_Inc.copyTo(cv_ptr->image);////most important

			end_process = getTime();	drawPublish_time = end_process - begin_process;
			whole_time = end_process - begin_whole;

			/////// Time Measures
			{
				printf("Time: total %.0f, Classified: occ %.1f, Decomp %.1f, Draw %.1f \n", whole_time, occupancy_time, decompose_time, drawPublish_time);
	
	
				clean_time_vector.push_back(occupancy_time);
				decomp_time_vector.push_back(decompose_time);
				paint_time_vector.push_back(drawPublish_time);
				complete_time_vector.push_back(whole_time);
				
				cout << "Time Vector size "<< clean_time_vector.size() << endl;
			}
			

//*
			double cum_time = 0, cum_quad_time = 0;
			for(int i=0; i < clean_time_vector.size(); i++){
//				cout << time_vector[i] << endl;
				printf("%.0f %.0f %.0f %.0f \n", paint_time_vector[i], clean_time_vector[i],  decomp_time_vector[i] , complete_time_vector[i]);
				cum_time += complete_time_vector[i];
				cum_quad_time += complete_time_vector[i]*complete_time_vector[i];
			}
			float avg_time      =      cum_time/clean_time_vector.size() ;
			float avg_quad_time = cum_quad_time/clean_time_vector.size() ;
			float std_time = sqrt( avg_quad_time - avg_time*avg_time  );

			std::cout << "Number of Regions " << Stable.Region_contour.size() << std::endl;
			
//			std::cout << clean_time_vector.size();
//			printf(" frames processed. Avg time: %.0f + %.0f ms \n", avg_time, std_time);

//			cv::flip(black_image, black_image,0);  cv::Mat big = Stable.draw_stable_contour() & ~black_image;			
//			save_images_color(big);
			//*/
			
		/////////////////////////	
		}
			

/////////////////		
		void metronomeCallback(const ros::TimerEvent&)
		{
//		  ROS_INFO("tic tac");
		  publish_Image();
		}

////////////////
		void chatCallback(const std_msgs::String& chat_msg){
			std::cout << "chat in" << std::endl;
			
			std::string saving_path = "src/Incremental_DuDe_ROS/maps/Topological_Segmentation/";
			cv::Mat proxy, zero =cv::Mat::zeros(image2save_clean.size(),CV_8U);
			///////////
				cv::Mat Batch_segmentated = simple_segment(image2save_clean);
			//////////
			std::map<int,int> Batch_Inc_map = compare_images(Batch_segmentated, image2save_Inc);
			
			Batch_segmentated.copyTo( proxy , ~image2save_black);						
			Batch_segmentated = proxy.clone(); 

			double min, max_batch, max_inc;
			cv::minMaxLoc(Batch_segmentated, &min, &max_batch);
			cv::minMaxLoc(image2save_Inc, &min, &max_inc);
			
			cv::Mat destroyable_batch = Batch_segmentated.clone();
			std::vector<std::vector<cv::Point> > test_contour;
			cv::findContours(destroyable_batch, test_contour, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE );

			cv::Rect first_rect = cv::boundingRect(test_contour[0]);
			for(int i=1; i < test_contour.size(); i++){
				first_rect |= cv::boundingRect(test_contour[i]);
			}
			
			
			cv::Mat cropped_Batch, cropped_Inc;
/*			
			cv::Mat image_roi = Batch_segmentated(first_rect);
			image_roi.copyTo(cropped_Batch);
			
			
			float rect_area = (first_rect.height)*(first_rect.width);
			float img_area = (Batch_segmentated.rows) * (Batch_segmentated.cols);
			cout <<"Area Ratio " <<  ( rect_area/img_area  )*100 <<"% "<< endl;
			*/
			Batch_segmentated(first_rect).copyTo(cropped_Batch); /////////// Cut the relevant image
			image2save_Inc(first_rect).copyTo(cropped_Inc); /////////// Cut the relevant image
			
			
			
			if ( true ){
				std::vector <cv::Vec3b> colormap = save_image_original_color(saving_path + chat_msg.data + "_Batch.png", cropped_Batch);
				save_decomposed_image_color(saving_path + chat_msg.data + "_Inc.png", cropped_Inc, colormap, Batch_Inc_map);
			}
			else{
				std::vector <cv::Vec3b> colormap = save_image_original_color(saving_path + chat_msg.data + "_Inc.png", image2save_Inc );
				save_decomposed_image_color(saving_path + chat_msg.data + "_Batch.png", Batch_segmentated, colormap, Batch_Inc_map);
			}

			
		}


////////////////////////
// PUBLISHING METHODS		
////////////////////////////		
		void publish_Image(){
			image_pub_.publish(cv_ptr->toImageMsg());
		}



/////////////////////////
//// UTILITY
/////////////////////////

		cv::Mat clean_image(cv::Mat Occ_Image, cv::Mat &black_image){
			//Occupancy Image to Free Space	
			
			cv::Mat valid_image = Occ_Image < 101;
			std::vector<std::vector<cv::Point> > test_contour;
			cv::findContours(valid_image, test_contour, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE );

			cv::Rect first_rect = cv::boundingRect(test_contour[0]);
			for(int i=1; i < test_contour.size(); i++){
				first_rect |= cv::boundingRect(test_contour[i]);
			}
			cv::Mat reduced_Image;
			valid_image(first_rect).copyTo(reduced_Image);
			
			
			cv::Mat open_space = reduced_Image<10;
			black_image = reduced_Image>90 & reduced_Image<=100;		
			cv::Mat Median_Image, out_image, temp_image ;
			int filter_size=2;

			cv::boxFilter(black_image, temp_image, -1, cv::Size(filter_size, filter_size), cv::Point(-1,-1), false, cv::BORDER_DEFAULT ); // filter open_space
			black_image = temp_image > filter_size*filter_size/2;  // threshold in filtered
			cv::dilate(black_image, black_image, cv::Mat(), cv::Point(-1,-1), 4, cv::BORDER_CONSTANT, cv::morphologyDefaultBorderValue() );			// inflate obstacle

			filter_size=10;
			cv::boxFilter(open_space, temp_image, -1, cv::Size(filter_size, filter_size), cv::Point(-1,-1), false, cv::BORDER_DEFAULT ); // filter open_space
			Median_Image = temp_image > filter_size*filter_size/2;  // threshold in filtered
			Median_Image = Median_Image | open_space ;
			//cv::medianBlur(Median_Image, Median_Image, 3);
			cv::dilate(Median_Image, Median_Image,cv::Mat());

			out_image = Median_Image & ~black_image;// Open space without obstacles




			cv::Size image_size = Occ_Image.size();
			cv::Mat image_out(image_size, CV_8UC1);
			cv::Mat black_image_out(image_size, CV_8UC1) ; 

			out_image.copyTo(image_out(first_rect));
			
			black_image.copyTo(black_image_out(first_rect));
			black_image =black_image_out;

			return image_out;
		}


		cv::Mat clean_image2(cv::Mat Occ_Image, cv::Mat &black_image){
			//Occupancy Image to Free Space	
			cv::Mat open_space = Occ_Image<10;
			black_image = Occ_Image>90 & Occ_Image<=100;		
			cv::Mat Median_Image, out_image, temp_image ;
			int filter_size=2;

			cv::boxFilter(black_image, temp_image, -1, cv::Size(filter_size, filter_size), cv::Point(-1,-1), false, cv::BORDER_DEFAULT ); // filter open_space
			black_image = temp_image > filter_size*filter_size/2;  // threshold in filtered
			cv::dilate(black_image, black_image, cv::Mat(), cv::Point(-1,-1), 4, cv::BORDER_CONSTANT, cv::morphologyDefaultBorderValue() );			// inflate obstacle

			filter_size=10;
			cv::boxFilter(open_space, temp_image, -1, cv::Size(filter_size, filter_size), cv::Point(-1,-1), false, cv::BORDER_DEFAULT ); // filter open_space
			Median_Image = temp_image > filter_size*filter_size/2;  // threshold in filtered
			Median_Image = Median_Image | open_space ;
			//cv::medianBlur(Median_Image, Median_Image, 3);
			cv::dilate(Median_Image, Median_Image,cv::Mat());

			out_image = Median_Image & ~black_image;// Open space without obstacles

			return out_image;
		}

		cv::Rect find_image_bounding_Rect(cv::Mat Occ_Image){
			cv::Mat valid_image = Occ_Image < 101;
			std::vector<std::vector<cv::Point> > test_contour;
			cv::findContours(valid_image, test_contour, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE );

			cv::Rect first_rect = cv::boundingRect(test_contour[0]);
			for(int i=1; i < test_contour.size(); i++){
				first_rect |= cv::boundingRect(test_contour[i]);
			}
			return first_rect;
		}

	/////////////////
		void save_images_color(cv::Mat DuDe_segmentation){
			std::string full_path_decomposed       = "src/Incremental_DuDe_ROS/maps/Topological_Segmentation/map_decomposed.png";
			double min, max;
			
			std::vector <cv::Vec3b> color_vector;
			cv::Vec3b black(0, 0, 0);
			color_vector.push_back(black);
			
			cv::minMaxLoc(DuDe_segmentation, &min,&max);

			for(int i=0;i<= max; i++){
				cv::Vec3b color(rand() % 255,rand() % 255,rand() % 255);
				color_vector.push_back(color);
			}
			cv::Mat DuDe_segmentation_float = cv::Mat::zeros(DuDe_segmentation.size(), CV_8UC3);
			for(int i=0; i < DuDe_segmentation.rows; i++){
				for(int j=0;j<DuDe_segmentation.cols; j++){
					int color_index = DuDe_segmentation.at<uchar>(i,j);
					DuDe_segmentation_float.at<cv::Vec3b>(i,j) = color_vector[color_index];
				}
			}
			cv::imwrite( full_path_decomposed , DuDe_segmentation_float );

		}





/////////////////////////////////////
//// Evaluation
//////////////////////////////////




	/////////////////
		void save_decomposed_image_color(std::string path, cv::Mat image_in, std::vector <cv::Vec3b> colormap, std::map<int,int> original_map){
			double min, max;
			std::vector <cv::Vec3b> color_vector;
			cv::Vec3b black(208, 208, 208);
			color_vector.push_back(black);
			
			std::map<int,int>::iterator map_iter;
			
			cv::minMaxLoc(image_in, &min,&max);
			color_vector.resize(max);

			for(int i=1;i<= max; i++){
				map_iter = original_map.find(i);
				if (map_iter != original_map.end()){
					int index_in_original = map_iter->second;
					color_vector[i]=colormap[index_in_original];
				}
				else{		
					cv::Vec3b color(rand() % 255,rand() % 255,rand() % 255);
					color_vector[i] = color;
				}
			}
			/////
			cv::Mat image_float = cv::Mat::zeros(image_in.size(), CV_8UC3);
			for(int i=0; i < image_in.rows; i++){
				for(int j=0;j< image_in.cols; j++){
					int color_index = image_in.at<uchar>(i,j);
					image_float.at<cv::Vec3b>(i,j) = color_vector[color_index];
				}
			}
			cv::imwrite( path , image_float );



		}

	/////////////////
		std::vector <cv::Vec3b> save_image_original_color(std::string path, cv::Mat image_in){

			double min, max;			
			std::vector <cv::Vec3b> color_vector;
			cv::Vec3b black(208, 208, 208);
			color_vector.push_back(black);
			
			cv::minMaxLoc(image_in, &min,&max);

			for(int i=0;i<= max; i++){
				cv::Vec3b color(rand() % 255,rand() % 255,rand() % 255);
				color_vector.push_back(color);
			}
			cv::Mat image_float = cv::Mat::zeros(image_in.size(), CV_8UC3);
			for(int i=0; i < image_in.rows; i++){
				for(int j=0;j< image_in.cols; j++){
					int color_index = image_in.at<uchar>(i,j);
					image_float.at<cv::Vec3b>(i,j) = color_vector[color_index];
				}
			}
			cv::imwrite( path , image_float );

			return color_vector;
		}


	////////////////////
		cv::Mat simple_segment(cv::Mat image_in){
			Incremental_Decomposer inc_decomp;
			Stable_graph Stable;
			cv::Point2f origin(0,0);
			float resolution = 0.05;

			
			cv::Mat pre_decompose = image_in.clone();
			cv::Mat pre_decompose_BW = pre_decompose > 250;
//			cv::Mat pre_decompose_BW = clean_image(pre_decompose > 250);


			Stable = inc_decomp.decompose_image(pre_decompose_BW, Decomp_threshold_/resolution, origin , resolution);
		
				

//			cv::Mat Segmentation = Stable.draw_stable_contour();
			
			cv::Mat Drawing = cv::Mat::zeros(image_in.size(), CV_8UC1);	
			for(int i = 0; i < Stable.Region_contour.size();i++){
				drawContours(Drawing, Stable.Region_contour, i, i+1, -1, 8);
			}
			std::cout << "Decomposition size: " << Stable.Region_contour.size() << std::endl;

			return Drawing;
		}


	/////////////////////
		std::map<int,int> compare_images(cv::Mat previous_segmentation, cv::Mat new_segmentation){
			
			std::map<int,int> segmented2GT_tags;
			
			cv::Mat prev_segmentation   = cv::Mat::zeros(previous_segmentation.size(),CV_8UC1);
			cv::Mat current_segmentation = cv::Mat::zeros(previous_segmentation.size(),CV_8UC1);
			
			previous_segmentation  .convertTo(prev_segmentation, CV_8UC1);
			new_segmentation.convertTo(current_segmentation, CV_8UC1);			
			
			std::map <std::vector<int>, std::vector <cv::Point> > links2points;
			std::map <int, std::vector <cv::Point> > prev_points, new_points;
			
			for(int x=0; x < prev_segmentation.size().width; x++){
				for(int y=0; y < prev_segmentation.size().height; y++){
					cv::Point current_pixel(x,y);
					std::vector<int> match;
										
					int tag_prev   = prev_segmentation.at<uchar>(current_pixel);
					int tag_new  = current_segmentation.at<uchar>(current_pixel);
					
//					if( tag_new>0 && tag_prev>0){
					if( tag_new>0){
						match.push_back( tag_prev);
						match.push_back( tag_new) ;
						links2points[match].push_back(current_pixel);
						
						prev_points[tag_prev].push_back(current_pixel);
						new_points[tag_new].push_back(current_pixel);
						
					}
				}
			}



			std::map <int, std::pair<int, float>  > map_current_to_max_and_relation;
			int this_max = -1;
			int current_new_evaluated = -1;
			float this_max_relation = -1;
			std::map <std::vector<int>, float > match_to_relation;
			

			for( std::map <std::vector<int>, std::vector <cv::Point> >::iterator it = links2points.begin(); it!= links2points.end(); it++ ){
				std::vector <cv::Point> points_in_match = it->second;
				
				int previous = it->first[0];
				int current  = it->first[1];

				float A = prev_points  [ previous ].size();
				float B = new_points[ current ].size();
				float AandB = points_in_match.size();

				float relation = AandB/B;  //( A + B - AandB );
				
				match_to_relation[it->first] = relation;


				std::cerr << "    ["<<previous<<","<<current<<"] has "<< relation << endl;
//				std::cout << "  AandB "<<AandB<<", A "<< A <<", B "<< B << endl;

				std::pair<int, float> values(previous, relation);				

				std::map <int, std::pair<int, float>  >::iterator map_iter = map_current_to_max_and_relation.find(current);
				if (map_iter != map_current_to_max_and_relation.end()){
					std::cerr << "     old relation "<<  map_current_to_max_and_relation[current].second << ", new relation "<< relation << endl;				
					if(map_current_to_max_and_relation[current].second < relation){
						map_current_to_max_and_relation[current]= values;
						std::cerr << "     max modified to "<<  map_current_to_max_and_relation[current].first << endl;				
					}
				}
				else{
					map_current_to_max_and_relation[current]= values;
				}
				//



				/*
				if(current_new_evaluated != current ){//update maximum
					if( current_new_evaluated != -1)
						max_match_current[current_new_evaluated] = previous;

//					std::cout << "    Maximum is ["<< current_new_evaluated <<","<< this_max <<"] with relation "<< this_max_relation << endl;
					current_new_evaluated = current;
					this_max = previous;
					this_max_relation = relation;
				}
				else if(relation > this_max_relation){
					this_max = previous;
					this_max_relation = relation;
				}				
			}
			max_match_current[current_new_evaluated] = this_max;
			*/
			
			}
			std::map<int,int>	max_match_current;		
			for(std::map <int, std::pair<int, float>  >::iterator map_iter = map_current_to_max_and_relation.begin(); map_iter != map_current_to_max_and_relation.end(); map_iter++){
				std::cerr << "     "<<map_iter->first <<" in new correspond to the old "<< map_iter->second.first <<" with relation " << map_iter->second.second<<  endl;				
				max_match_current[map_iter->second.first] = map_iter->first;
			}
			
			
			
			for(std::map<int,int>::iterator map_iter =   max_match_current.begin(); map_iter !=   max_match_current.end(); map_iter ++ ){
				std::cerr << "     "<<map_iter->first <<" correspond to "<< map_iter->second << endl;				
			}
				std::cerr << endl << endl;	
				

			return max_match_current;
		}


	/////////////////
		std::vector <cv::Vec3b> convert_image_to_color( cv::Mat image_in, cv::Mat *image_out){

			double min, max;			
			std::vector <cv::Vec3b> color_vector;
			cv::Vec3b black(208, 208, 208);
			color_vector.push_back(black);
			
			cv::minMaxLoc(image_in, &min,&max);

			for(int i=0;i<= max; i++){
				cv::Vec3b color(rand() % 255,rand() % 255,rand() % 255);
				color_vector.push_back(color);
			}
			cv::Mat image_float = cv::Mat::zeros(image_in.size(), CV_8UC3);
			for(int i=0; i < image_in.rows; i++){
				for(int j=0;j< image_in.cols; j++){
					int color_index = image_in.at<uchar>(i,j);
					image_float.at<cv::Vec3b>(i,j) = color_vector[color_index];
				}
			}

			*image_out= image_float.clone();
			return color_vector;
		}



	////////////////
	void paint_with_previous_color( cv::Mat image_in, std::vector <cv::Vec3b> *colormap, std::map<int,int> original_map, cv::Mat *image_out){
		double min, max;
		std::vector <cv::Vec3b> color_vector;
		cv::Vec3b black(208, 208, 208);
		color_vector.push_back(black);
		
		std::map<int,int>::iterator map_iter;
		
		cv::minMaxLoc(image_in, &min,&max);
		color_vector.resize(max);

		for(int i=1;i<= max; i++){
			map_iter = original_map.find(i);
			if (map_iter != original_map.end()){
				int index_in_original = map_iter->second;
				color_vector[i]=(*colormap)[index_in_original];
			}
			else{		
				cv::Vec3b color(rand() % 255,rand() % 255,rand() % 255);
				color_vector[i] = color;
			}
		}
		/////
		cv::Mat image_float = cv::Mat::zeros(image_in.size(), CV_8UC3);
		for(int i=0; i < image_in.rows; i++){
			for(int j=0;j< image_in.cols; j++){
				int color_index = image_in.at<uchar>(i,j);
				image_float.at<cv::Vec3b>(i,j) = color_vector[color_index];
			}
		}


		*image_out = image_float.clone();
		colormap->clear();
		*colormap = color_vector;


	}


	cv::Mat paint_image_colormap(cv::Mat image_in, std::vector <cv::Vec3b> color_vector){



		cv::Mat image_float = cv::Mat::zeros(image_in.size(), CV_8UC3);



		for(int i=0; i < image_in.rows; i++){
			for(int j=0;j< image_in.cols; j++){
				int color_index = image_in.at<uchar>(i,j);
				image_float.at<cv::Vec3b>(i,j) = color_vector[color_index];
			}
		}
		

		cv::flip(image_float,image_float,0);
		
//		std::cerr<<"Stable.Region_centroid.size()"<<Stable.Region_centroid.size()<<std::endl;

		std::cerr << "Current edges " << Stable.diagonal_connections.size() << std::endl;
		
		//*
		for(int i=0;i < Stable.diagonal_centroid.size();i++){
			int region_from =*(Stable.diagonal_connections[i].begin() );
			int region_to =*(Stable.diagonal_connections[i].rbegin() );
			
			cv::Point Start_link = Stable.Region_centroid[region_from];
			cv::Point End_link = Stable.Region_centroid[region_to];

			std::cerr << "    from "<<region_from << ", to " << region_to << std::endl;
			
			cv::line( image_float, Start_link, Stable.diagonal_centroid[i], cv::Scalar( 0, 255, 0 ), 3, 8);
			cv::line( image_float, Stable.diagonal_centroid[i], End_link, cv::Scalar( 0, 255, 0 ), 3, 8);
			
		}
		//*/




		for(int i = 0; i < Stable.Region_centroid.size();i++){
			cv::Point flip_centroid;
			flip_centroid.x = Stable.Region_centroid[i].x;
			flip_centroid.y = Stable.image_size.height - Stable.Region_centroid[i].y;
			
//			circle( image_float,flip_centroid,5,cv::Scalar(0, 255, 0),-1,8);
			circle( image_float,Stable.Region_centroid[i],6,cv::Scalar(0, 255, 0),-1,8);
			circle( image_float,Stable.Region_centroid[i],3,cv::Scalar(0, 0, 255),-1,8);
		 }
		
		
		
		
		
		cv::flip(image_float,image_float,0);
		
		return image_float;
	}


};










int main(int argc, char **argv)
{
	
	ros::init(argc, argv, "incremental_decomposer");
	
	std::string mapname = "map";
	
	float decomp_th=3;
	if (argc ==2){ decomp_th = atof(argv[1]); }	
	
	ROS_handler mg(mapname, decomp_th);
	ros::spin();
	
	return 0;
}
