#include <ros/ros.h>
#include <sensor_msgs/CompressedImage.h>
#include <std_msgs/String.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/PointCloud2.h>
#include <move_base_msgs/MoveBaseAction.h>

#include <tf/tf.h>

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/calib3d.hpp>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_types.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl/conversions.h>
#include <pcl_ros/transforms.h>

#include <fstream>
#include <map>

int room_number;

bool is_there_new_qrcode = false;
std::string artwork_label = "";
std::vector<cv::KeyPoint> new_keypoints;
std::map<std::string, std::string> qrcode;

std::ofstream qrcode_database;

int i = 0; // Guarda o indice do qrcode atual
bool desinfetado = 0;
bool to_point = 0; //true se o robô chegou ao QRcode

void barcodeCallback(const std_msgs::String& msg)
{
    is_there_new_qrcode = true;
    std::string str;
    std::string label_s;

	char* pch;
	char buff[200];
	memcpy(buff, msg.data.c_str(), msg.data.size());

	pch = strtok(buff, "\n");
    str = std::string(pch);

    unsigned first = str.find(" ");
	unsigned last = str.find("\n");

    label_s = str.substr(first,last-first);

    room_number = atof(label_s.c_str());

}

bool calculatePoseWrtMap(const std::string& frame_id, const double x, const double y, const double z, double &nx, double &ny, double &nz){

        static tf::TransformListener tf_listener; //!static

        tf::StampedTransform tf_transform;

        try{

            // the first line shouldn't be necessary in general

            // but during the few milliseconds in the beginning we don't have the transform yet ready

            tf_listener.waitForTransform("map", frame_id, ros::Time(0), ros::Duration(2.0));

            tf_listener.lookupTransform("map", frame_id, ros::Time(0), tf_transform);

        }

        catch(tf::TransformException& ex){

            ROS_ERROR_THROTTLE(0.2, "%s", ex.what());

            return false;

        }

        tf::Vector3 v(x, y, z);
        v = tf_transform(v);
        nx = v.x();
        ny = v.y();
        nz = v.z();

        return true;

}



void pointcloudCallback(const sensor_msgs::PointCloud2& msg){
    if (new_keypoints.empty()){
        
        return;
    }
    
    pcl::PCLPointCloud2 pointcloud_t2;
    pcl_conversions::toPCL(msg, pointcloud_t2);
    
    pcl::PointCloud<pcl::PointXYZ>::Ptr pointcloud(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::fromPCLPointCloud2(pointcloud_t2, *pointcloud);
    //std::cout << " ---------------------- \n";
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_z = 0.0;
    int count = 0;
    for (int i=0; i<new_keypoints.size(); i++){
        int ind = new_keypoints[i].pt.x + new_keypoints[i].pt.y*msg.width;
        if (!std::isnan(pointcloud->points[ind].x)){
            sum_x += pointcloud->points[ind].x;
            sum_y += pointcloud->points[ind].y;
            sum_z += pointcloud->points[ind].z;
            count++;
        }
    }
    if (count == 0){
        return;
    }
    sum_x /= count;
    sum_y /= count;
    sum_z /= count;
    
    double nx = 0.0;
    double ny = 0.0;
    double nz = 0.0;
    sum_z = sum_z - 0.30;
    
    calculatePoseWrtMap(msg.header.frame_id, sum_x, sum_y, sum_z, nx, ny, nz);
    
    char out_string[512];
    char label[50];
    // format string: label, x, y, z
    std::string aux;
    sprintf(label,"%d", room_number);
    sprintf(out_string, "%d, %2.2f, %2.2f, %2.2f\n", room_number, nx, ny, nz);

    if (sqrt(sum_x*sum_x + sum_y*sum_y + sum_z*sum_z) < 2.0){
        auto rv = qrcode.emplace(label, out_string);
        if (rv.second){
            std::cout << out_string << std::endl;
            qrcode_database << out_string;
            qrcode_database.flush();
        }        
    }

    new_keypoints.clear();
}

void cameraCallback(const sensor_msgs::CompressedImageConstPtr& msg){

	try{
		cv::Mat color_img = imdecode(cv::Mat(msg->data), cv::IMREAD_COLOR);

        if (!is_there_new_qrcode){
            cv::Mat disp_img;
            cv::resize(color_img, disp_img, cv::Size(0, 0), 0.25, 0.25, cv::INTER_LINEAR);
            cv::imshow("Current Image", disp_img);
            cv::waitKey(1);                        
            return;
        }
        is_there_new_qrcode = false;
        
        cv::Mat gray_img;
        cv::cvtColor(color_img, gray_img, cv::COLOR_RGB2GRAY);        
        cv::SimpleBlobDetector::Params params;
        params.filterByColor = true;
        params.minThreshold = 0;
        params.maxThreshold = 200;
        cv::Ptr<cv::SimpleBlobDetector> detector = cv::SimpleBlobDetector::create(params);
        
        new_keypoints.clear();
        detector->detect(gray_img, new_keypoints);
        cv::Mat  marked_img;
        if (new_keypoints.size()>0){
            cv::drawKeypoints(color_img, new_keypoints, marked_img, cv::Scalar(0,0,255)); 
            // , cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS
        }
        cv::resize(marked_img, marked_img, cv::Size(0, 0), 0.25, 0.25, cv::INTER_LINEAR);
        imshow("Current Image", marked_img);
        cv::waitKey(1);
	}
	
	catch(cv::Exception& e){
		ROS_ERROR("Error converting image, %s", e.what());
	}
}


int main(int argc, char **argv){
	ros::init(argc,argv,"explorer_node");
	ros::NodeHandle nh;

    ros::Subscriber subs_barcode = nh.subscribe("/barcode",1,barcodeCallback);
	ros::Subscriber subs_camera = nh.subscribe("/camera/rgb/image_raw/compressed", 1, cameraCallback);
    ros::Subscriber subs_pc = nh.subscribe("/camera/depth/points", 1, pointcloudCallback);
    
    qrcode_database.open("/home/duarte/RC-project/qrcode_database.csv", std::ios::out);
	if (!qrcode_database.is_open()){
		ROS_ERROR("Could not open QRCode database!");
		return -1;
	}

	ros::Rate rate(10);
	while(ros::ok){
		ros::spinOnce();
		rate.sleep();
	}

    qrcode_database.close();
	
}
