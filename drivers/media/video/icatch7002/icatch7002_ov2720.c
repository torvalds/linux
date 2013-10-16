
#include "icatch7002_common.h"
/*
*      Driver Version Note
*v0.0.1: this driver is compatible with generic_sensor
*v0.0.3:
*        add sensor_focus_af_const_pause_usr_cb;
*/
static int version = KERNEL_VERSION(0,0,3);
module_param(version, int, S_IRUGO);



static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

/* Sensor Driver Configuration Begin */
#define SENSOR_NAME RK29_CAM_ISP_ICATCH7002_OV2720
#define SENSOR_V4L2_IDENT V4L2_IDENT_ICATCH7002_OV2720
#define SENSOR_ID 0x2720
#define SENSOR_BUS_PARAM  (SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING|\
						  SOCAM_HSYNC_ACTIVE_HIGH| SOCAM_VSYNC_ACTIVE_HIGH|\
						  SOCAM_DATA_ACTIVE_HIGH|SOCAM_DATAWIDTH_8	|SOCAM_MCLK_24MHZ)
#define SENSOR_PREVIEW_W					 1280
#define SENSOR_PREVIEW_H					 960
#define SENSOR_PREVIEW_FPS					 30000	   // 15fps 
#define SENSOR_FULLRES_L_FPS				 15000	   // 7.5fps
#define SENSOR_FULLRES_H_FPS				 15000	   // 7.5fps
#define SENSOR_720P_FPS 					 30000
#define SENSOR_1080P_FPS					 0

									
static unsigned int SensorConfiguration = 0;
static unsigned int SensorChipID[] = {SENSOR_ID};
/* Sensor Driver Configuration End */


#define SENSOR_NAME_STRING(a) STR(CONS(SENSOR_NAME, a))
//#define SENSOR_NAME_VARFUN(a) CONS(SENSOR_NAME, a)

//#define SensorRegVal(a,b) CONS4(SensorReg,SENSOR_REGISTER_LEN,Val,SENSOR_VALUE_LEN)(a,b)
//#define sensor_write(client,reg,v) CONS4(sensor_write_reg,SENSOR_REGISTER_LEN,val,SENSOR_VALUE_LEN)(client,(reg),(v))
//#define sensor_read(client,reg,v) CONS4(sensor_read_reg,SENSOR_REGISTER_LEN,val,SENSOR_VALUE_LEN)(client,(reg),(v))
//#define sensor_write_array generic_sensor_write_array



/*
*  The follow setting need been filled.
*  
*  Must Filled:
*  sensor_init_data :				Sensor initial setting;
*  sensor_fullres_lowfps_data : 	Sensor full resolution setting with best auality, recommand for video;
*  sensor_preview_data :			Sensor preview resolution setting, recommand it is vga or svga;
*  sensor_softreset_data :			Sensor software reset register;
*  sensor_check_id_data :			Sensir chip id register;
*
*  Optional filled:
*  sensor_fullres_highfps_data: 	Sensor full resolution setting with high framerate, recommand for video;
*  sensor_720p: 					Sensor 720p setting, it is for video;
*  sensor_1080p:					Sensor 1080p setting, it is for video;
*
*  :::::WARNING:::::
*  The SensorEnd which is the setting end flag must be filled int the last of each setting;
*/

/* Sensor initial setting */
static struct rk_sensor_reg sensor_init_data[] ={
    SensorStreamChk,
	SensorEnd
};
/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] ={
	SensorEnd

};

/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] ={
	SensorEnd
};
/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] =
{
    SensorStreamChk,
	SensorEnd
};
/* 1280x720 */
static struct rk_sensor_reg sensor_720p[]={
    SensorStreamChk,
	SensorEnd
};

/* 1920x1080 */
static struct rk_sensor_reg sensor_1080p[]={
	SensorEnd
};


static struct rk_sensor_reg sensor_softreset_data[]={
	SensorEnd
};

static struct rk_sensor_reg sensor_check_id_data[]={
	SensorEnd
};
/*
*  The following setting must been filled, if the function is turn on by CONFIG_SENSOR_xxxx
*/
static struct rk_sensor_reg sensor_WhiteB_Auto[]=
{
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	//Sunny
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	//Office
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//Home
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};

static	struct rk_sensor_reg sensor_Brightness0[]=
{
	// Brightness -2
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness1[]=
{
	// Brightness -1

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness2[]=
{
	//	Brightness 0

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness3[]=
{
	// Brightness +1

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness4[]=
{
	//	Brightness +2

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness5[]=
{
	//	Brightness +3

	SensorEnd
};
static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
	sensor_Brightness4, sensor_Brightness5,NULL,
};

static	struct rk_sensor_reg sensor_Effect_Normal[] =
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_WandB[] =
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Negative[] =
{
	//Negative
	SensorEnd
};
static	struct rk_sensor_reg sensor_Effect_Bluish[] =
{
	// Bluish
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Green[] =
{
	//	Greenish
	SensorEnd
};
static struct rk_sensor_reg *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
	sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};

static	struct rk_sensor_reg sensor_Exposure0[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure1[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure2[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure3[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure4[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure5[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure6[]=
{
	SensorEnd
};

static struct rk_sensor_reg *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
	sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,NULL,
};

static	struct rk_sensor_reg sensor_Saturation0[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Saturation1[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Saturation2[]=
{
	SensorEnd
};
static struct rk_sensor_reg *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, NULL,};

static	struct rk_sensor_reg sensor_Contrast0[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast1[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast2[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast3[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast4[]=
{

	SensorEnd
};


static	struct rk_sensor_reg sensor_Contrast5[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast6[]=
{

	SensorEnd
};
static struct rk_sensor_reg *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
	sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};
static	struct rk_sensor_reg sensor_SceneAuto[] =
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_SceneNight[] =
{
	SensorEnd
};
static struct rk_sensor_reg *sensor_SceneSeqe[] = {sensor_SceneAuto, sensor_SceneNight,NULL,};

static struct rk_sensor_reg sensor_Zoom0[] =
{
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom1[] =
{
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom2[] =
{
	SensorEnd
};


static struct rk_sensor_reg sensor_Zoom3[] =
{
	SensorEnd
};
static struct rk_sensor_reg *sensor_ZoomSeqe[] = {sensor_Zoom0, sensor_Zoom1, sensor_Zoom2, sensor_Zoom3, NULL,};

/*
* User could be add v4l2_querymenu in sensor_controls by new_usr_v4l2menu
*/
static struct v4l2_querymenu sensor_menus[] =
{
	//white balance
	new_usr_v4l2menu(V4L2_CID_DO_WHITE_BALANCE,0,"auto",0),
	new_usr_v4l2menu(V4L2_CID_DO_WHITE_BALANCE,1,"incandescent",0),
	new_usr_v4l2menu(V4L2_CID_DO_WHITE_BALANCE,2,"fluorescent",0),
	new_usr_v4l2menu(V4L2_CID_DO_WHITE_BALANCE,3,"daylight",0),
	new_usr_v4l2menu(V4L2_CID_DO_WHITE_BALANCE,4,"cloudy-daylight",0),

	//speical effect
	new_usr_v4l2menu(V4L2_CID_EFFECT,0,"normal",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,1,"aqua",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,2,"negative",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,3,"sepia",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,4,"mono",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,5,"none",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,6,"aura",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,7,"vintage",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,8,"vintage2",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,9,"lomo",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,10,"red",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,11,"blue",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,12,"green",0),

    //scence
	new_usr_v4l2menu(V4L2_CID_SCENE,0,"normal",0),
	new_usr_v4l2menu(V4L2_CID_SCENE,1,"auto",0),
	new_usr_v4l2menu(V4L2_CID_SCENE,2,"landscape",0),
	new_usr_v4l2menu(V4L2_CID_SCENE,3,"night",0),
	new_usr_v4l2menu(V4L2_CID_SCENE,4,"night_portrait",0),
	new_usr_v4l2menu(V4L2_CID_SCENE,5,"snow",0),
	new_usr_v4l2menu(V4L2_CID_SCENE,6,"sports",0),
	new_usr_v4l2menu(V4L2_CID_SCENE,7,"candlelight",0),

	//antibanding
	new_usr_v4l2menu(V4L2_CID_ANTIBANDING,0,"50hz",0),
	new_usr_v4l2menu(V4L2_CID_ANTIBANDING,1,"60hz",0),

	//ISO
	new_usr_v4l2menu(V4L2_CID_ISO,0,"auto",0),
	new_usr_v4l2menu(V4L2_CID_ISO,1,"50",0),
	new_usr_v4l2menu(V4L2_CID_ISO,2,"100",0),
	new_usr_v4l2menu(V4L2_CID_ISO,3,"200",0),
	new_usr_v4l2menu(V4L2_CID_ISO,4,"400",0),
	new_usr_v4l2menu(V4L2_CID_ISO,5,"800",0),
	new_usr_v4l2menu(V4L2_CID_ISO,6,"1600",0),
};
/*
* User could be add v4l2_queryctrl in sensor_controls by new_user_v4l2ctrl
*/
static struct sensor_v4l2ctrl_usr_s sensor_controls[] =
{
	new_user_v4l2ctrl(V4L2_CID_DO_WHITE_BALANCE,V4L2_CTRL_TYPE_MENU,"White Balance Control", 0, 4, 1, 0,sensor_set_get_control_cb, NULL),
//	new_user_v4l2ctrl(V4L2_CID_BRIGHTNESS,V4L2_CTRL_TYPE_INTEGER,"Brightness Control", -3, 2, 1, 0,sensor_set_get_control_cb, NULL),
	new_user_v4l2ctrl(V4L2_CID_EXPOSURE,V4L2_CTRL_TYPE_INTEGER,"Exposure Control", -3, 3, 1, 0,sensor_set_get_control_cb, NULL),
	new_user_v4l2ctrl(V4L2_CID_EFFECT,V4L2_CTRL_TYPE_MENU,"Effect Control", 0, 12, 1, 5,sensor_set_get_control_cb, NULL),
//	new_user_v4l2ctrl(V4L2_CID_CONTRAST,V4L2_CTRL_TYPE_INTEGER,"Contrast Control", -4, 4, 1, 0,sensor_set_get_control_cb, NULL),
	new_user_v4l2ctrl(V4L2_CID_SCENE,V4L2_CTRL_TYPE_MENU,"Scene Control", 0, 7, 1, 1,sensor_set_get_control_cb, NULL),
	new_user_v4l2ctrl(V4L2_CID_ANTIBANDING,V4L2_CTRL_TYPE_MENU,"Antibanding Control", 0, 1, 1, 0,sensor_set_get_control_cb, NULL),
	new_user_v4l2ctrl(V4L2_CID_WHITEBALANCE_LOCK,V4L2_CTRL_TYPE_BOOLEAN,"WhiteBalanceLock Control", 0, 1, 1, 0,sensor_set_get_control_cb, NULL),
	new_user_v4l2ctrl(V4L2_CID_EXPOSURE_LOCK,V4L2_CTRL_TYPE_BOOLEAN,"ExposureLock Control", 0, 1, 1, 0,sensor_set_get_control_cb, NULL),
	new_user_v4l2ctrl(V4L2_CID_METERING_AREAS,V4L2_CTRL_TYPE_INTEGER,"MeteringAreas Control", 0, 1, 1, 1,sensor_set_get_control_cb, NULL),
	new_user_v4l2ctrl(V4L2_CID_WDR,V4L2_CTRL_TYPE_BOOLEAN,"WDR Control", 0, 1, 1, 0,sensor_set_get_control_cb, NULL),
	new_user_v4l2ctrl(V4L2_CID_EDGE,V4L2_CTRL_TYPE_BOOLEAN,"EDGE Control", 0, 1, 1, 1,sensor_set_get_control_cb, NULL),
	new_user_v4l2ctrl(V4L2_CID_JPEG_EXIF,V4L2_CTRL_TYPE_BOOLEAN,"Exif Control", 0, 1, 1, 0,sensor_set_get_control_cb, NULL),
	new_user_v4l2ctrl(V4L2_CID_ISO,V4L2_CTRL_TYPE_MENU,"Exif Control", 0, 6, 1, 0,sensor_set_get_control_cb, NULL),
};

//MUST define the current used format as the first item   
static struct rk_sensor_datafmt sensor_colour_fmts[] = {
	{V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG} 
};


/*
**********************************************************
* Following is local code:
* 
* Please codeing your program here 
**********************************************************
*/
/*
**********************************************************
* Following is callback
* If necessary, you could coding these callback
**********************************************************
*/
/*
* the function is called in open sensor  
*/
static int sensor_activate_cb(struct i2c_client *client)
{
    return  icatch_sensor_init(client);
}
/*
* the function is called in close sensor
*/
static int sensor_deactivate_cb(struct i2c_client *client)
{
	
	return 0;
}
/*
* the function is called before sensor register setting in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_th(struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{
    return icatch_s_fmt(client, mf,capture);
}
/*
* the function is called after sensor register setting finished in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_bh (struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{
	return 0;
}
static int sensor_try_fmt_cb_th(struct i2c_client *client,struct v4l2_mbus_framefmt *mf)
{
	return 0;
}

static int sensor_softrest_usr_cb(struct i2c_client *client,struct rk_sensor_reg *series)
{
	
	return 0;
}
static int sensor_check_id_usr_cb(struct i2c_client *client,struct rk_sensor_reg *series)
{
	struct generic_sensor *sensor = to_generic_sensor(client);
	return sensor->info_priv.chip_id[0];
}
static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
	//struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
		
	if (pm_msg.event == PM_EVENT_SUSPEND) {
		SENSOR_DG("Suspend");
		
	} else {
		SENSOR_TR("pm_msg.event(0x%x) != PM_EVENT_SUSPEND\n",pm_msg.event);
		return -EINVAL;
	}
	return 0;
}

static int sensor_resume(struct soc_camera_device *icd)
{

	SENSOR_DG("Resume");

	return 0;

}
/*
* the function is v4l2 control V4L2_CID_HFLIP callback	
*/
static int sensor_v4l2ctrl_mirror_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
													 struct v4l2_ext_control *ext_ctrl)
{
	return 0;
}

/*
* the function is v4l2 control V4L2_CID_VFLIP callback	
*/
static int sensor_v4l2ctrl_flip_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
													 struct v4l2_ext_control *ext_ctrl)
{
	return 0;
}
/*
* the function is v4l2 control V4L2_CID_HFLIP callback 
*/

static int sensor_flip_cb(struct i2c_client *client, int flip)
{
    int err = 0;    

    return err;    
}
static int sensor_mirror_cb(struct i2c_client *client, int flip)
{
    int err = 0;    

    return err;    
}

/*
* the functions are focus callbacks
*/
static int sensor_focus_init_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_single_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_near_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_far_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_specialpos_usr_cb(struct i2c_client *client,int pos){
	return 0;
}

static int sensor_focus_af_const_usr_cb(struct i2c_client *client){

	return 0;
}
static int sensor_focus_af_const_pause_usr_cb(struct i2c_client *client)
{
    return 0;
}
static int sensor_focus_af_close_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_zoneupdate_usr_cb(struct i2c_client *client, int *zone_tm_pos)
{
	return 0;
}

/*
face defect call back
*/
static int	sensor_face_detect_usr_cb(struct i2c_client *client,int on){
	return 0;
}

/*
*	The function can been run in sensor_init_parametres which run in sensor_probe, so user can do some
* initialization in the function. 
*/
static void sensor_init_parameters_user(struct specific_sensor* spsensor,struct soc_camera_device *icd)
{
	spsensor->common_sensor.sensor_cb.sensor_s_stream_cb = icatch_s_stream;
    spsensor->common_sensor.sensor_cb.sensor_enum_framesizes = icatch_enum_framesizes;

	spsensor->isp_priv_info.outputSize =OUTPUT_1080P|OUTPUT_QUADVGA;
	spsensor->isp_priv_info.supportedSizeNum = 2;
	spsensor->isp_priv_info.supportedSize[0] = OUTPUT_QUADVGA;
    spsensor->isp_priv_info.supportedSize[1] = OUTPUT_1080P;
	return;
}

/*
* :::::WARNING:::::
* It is not allowed to modify the following code
*/

sensor_init_parameters_default_code();

sensor_v4l2_struct_initialization();

sensor_probe_default_code();

sensor_remove_default_code();

sensor_driver_default_module_code();




