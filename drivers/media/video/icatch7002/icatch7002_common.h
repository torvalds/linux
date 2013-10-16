#ifndef ICATCH7002_COMMON_H
#include "../generic_sensor.h"
#include "app_i2c_lib_icatch.h"
#include <linux/firmware.h>

/* CAMERA_REAR_SENSOR_SETTING:V17.00.18 */
/* CAMERA_FRONT_SENSOR_SETTING:V17.00.18 */
#if defined(CONFIG_TRACE_LOG_PRINTK)
 #define DEBUG_TRACE(format, ...) printk(KERN_WARNING format, ## __VA_ARGS__)
#else
 #define DEBUG_TRACE(format, ...)
#endif
#define LOG_TRACE(format, ...) printk(KERN_WARNING format, ## __VA_ARGS__)


#define	CALIBRATION_MODE_FUN		1

#define SENSOR_REGISTER_LEN 				 2		   // sensor register address bytes
#define SENSOR_VALUE_LEN					 1		   // sensor register value bytes

extern struct i2c_client *g_icatch_i2c_client;
#define icatch_sensor_write(reg,v) CONS4(sensor_write_reg,SENSOR_REGISTER_LEN,val,SENSOR_VALUE_LEN)((g_icatch_i2c_client),(reg),(v))
#define icatch_sensor_write_array(regarry) generic_sensor_write_array(g_icatch_i2c_client,regarry)


static inline u8 icatch_sensor_read(u16 reg)
{
    u8 val;
    sensor_read_reg2val1(g_icatch_i2c_client,reg,&val);
    return val;
}


enum sensor_work_state {
	sensor_work_ready = 0,
	sensor_working,
};

#define CONFIG_SENSOR_WhiteBalance	1
#define CONFIG_SENSOR_Brightness	0
#define CONFIG_SENSOR_Contrast		0
#define CONFIG_SENSOR_Saturation	0
#define CONFIG_SENSOR_Effect		1
#define CONFIG_SENSOR_Scene 		1
#define CONFIG_SENSOR_DigitalZoom	0
#define CONFIG_SENSOR_Focus 		1
#define CONFIG_SENSOR_Exposure		1
#define CONFIG_SENSOR_Flash 		0
#define CONFIG_SENSOR_Mirror		0
#define CONFIG_SENSOR_Flip		0
#define CONFIG_SENSOR_FOCUS_ZONE	0
#define CONFIG_SENSOR_FACE_DETECT	0
#define CONFIG_SENSOR_ISO		1
#define CONFIG_SENSOR_AntiBanding	1
#define CONFIG_SENSOR_WhiteBalanceLock	1
#define CONFIG_SENSOR_ExposureLock	1
#define CONFIG_SENSOR_MeteringAreas	1
#define CONFIG_SENSOR_Wdr			1
#define CONFIG_SENSOR_EDGE			1
#define CONFIG_SENSOR_JPEG_EXIF		1
#define CONFIG_SENSOR_DUMPREGS		0

#if CONFIG_SENSOR_Focus
extern int icatch_sensor_set_auto_focus(struct i2c_client *client, int value,int *tmp_zone);
#endif

#if CALIBRATION_MODE_FUN
void icatch_create_proc_entry(void);
void icatch_remove_proc_entry(void);
#endif
extern void BB_WrSPIFlash(u32 size);
extern int icatch_request_firmware(const struct firmware ** fw);
extern void icatch_release_firmware(const struct firmware * fw);
extern void icatch_sensor_power_ctr(struct soc_camera_device *icd ,int on,int power_mode);
extern int icatch_load_fw(struct soc_camera_device *icd,u8 sensorid);
int icatch_get_rearid_by_lowlevelmode(struct soc_camera_device *icd,UINT16 *rear_id);
int icatch_get_frontid_by_lowlevelmode(struct soc_camera_device *icd,UINT16 *front_id);
extern int icatch_sensor_init(struct i2c_client *client);
extern int icatch_s_fmt(struct i2c_client *client, struct v4l2_mbus_framefmt *mf,bool is_capture);
extern int icatch_s_stream(struct v4l2_subdev *sd, int enable);
extern int sensor_set_get_control_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
                                                     struct v4l2_ext_control *ext_ctrl,bool is_set);
extern int icatch_enum_framesizes(struct v4l2_subdev *sd, struct v4l2_frmsizeenum *fsize);

enum ISP_OUTPUT_RES{
	OUTPUT_QCIF 		=0x0001, // 176*144
	OUTPUT_HQVGA		=0x0002,// 240*160
	OUTPUT_QVGA 		=0x0004, // 320*240
	OUTPUT_CIF			=0x0008,  // 352*288
	OUTPUT_VGA			=0x0010,  // 640*480
	OUTPUT_SVGA 		=0x0020, // 800*600
	OUTPUT_720P 		=0x0040, // 1280*720
	OUTPUT_XGA			=0x0080,  // 1024*768
	OUTPUT_QUADVGA		=0x0100,  // 1280*960
	OUTPUT_SXGA 		=0x0200, // 1280*1024
	OUTPUT_UXGA 		=0x0400, // 1600*1200
	OUTPUT_1080P		=0x0800, //1920*1080
	OUTPUT_QXGA 		=0x1000,  // 2048*1536
	OUTPUT_QSXGA		=0x2000, // 2592*1944
	OUTPUT_QUXGA		=0x4000, //3264*2448
};



enum sensor_preview_cap_mode{
	PREVIEW_MODE,
	CAPTURE_MODE,
	CAPTURE_ZSL_MODE,
	CAPTURE_NONE_ZSL_MODE,
	IDLE_MODE,
};


typedef struct rk_sensor_focus_zone rk_sensor_tae_zone;
typedef struct{
	uint32_t num;
	uint32_t denom;
}rat_t;


typedef struct{
	/*IFD0*/
	char *maker;//manufacturer of digicam, just to adjust to make inPhybusAddr to align to 64
	int makerchars;//length of maker, contain the end '\0', so equal strlen(maker)+1
	char *modelstr;//model number of digicam
	int modelchars;//length of modelstr, contain the end '\0'
	int Orientation;//usually 1
	//XResolution, YResolution; if need be not 72, TODO...
	char DateTime[20];//must be 20 chars->  yyyy:MM:dd0x20hh:mm:ss'\0'
	/*Exif SubIFD*/
	rat_t ExposureTime;//such as 1/400=0.0025s
	rat_t ApertureFNumber;//actual f-number
	int ISOSpeedRatings;//CCD sensitivity equivalent to Ag-Hr film speedrate
	rat_t CompressedBitsPerPixel;
	rat_t ShutterSpeedValue;
	rat_t ApertureValue;
	rat_t ExposureBiasValue;
	rat_t MaxApertureValue;
	int MeteringMode;
	int Flash;
	rat_t FocalLength;
	rat_t FocalPlaneXResolution;
	rat_t FocalPlaneYResolution;
	int SensingMethod;//2 means 1 chip color area sensor
	int FileSource;//3 means the image source is digital still camera
	int CustomRendered;//0
	int ExposureMode;//0
	int WhiteBalance;//0
	rat_t DigitalZoomRatio;// inputw/inputw
	//int FocalLengthIn35mmFilm;
	int SceneCaptureType;//0
	
}RkExifInfo;

//flash and focus must be considered.

//soft isp or external isp used
//if soft isp is defined , the value in this sturct is used in cif driver . cif driver use this value to do isp func.
//value of this sturct MUST be defined(initialized) correctly. 
struct isp_data{
	int focus;
	int auto_focus;
	int flash;
	int whiteBalance;
	int brightness;
	int contrast;
	int saturation;
	int effect;
	int scene;
	int digitalzoom;
	int exposure;
	int iso;
	int face;
	int antibanding;
	int WhiteBalanceLock;
	int ExposureLock;
	int MeteringAreas;
	int Wdr;
	int hdr;
	//mirror or flip
	unsigned char mirror;										 /* HFLIP */
	unsigned char flip; 										 /* VFLIP */
	//preview or capture
	int outputSize; // supported resolution
	int curRes;
	int curPreviewCapMode;
	int supportedSize[10];
	int supportedSizeNum;
	int had_setprvsize;
    RkExifInfo exifInfo;
#if CALIBRATION_MODE_FUN
	int rk_query_PreviewCapMode;
	int sensor_id;
#endif
	struct rk_sensor_focus_zone focus_zone;
	rk_sensor_tae_zone tae_zone;

};




struct specific_sensor{
	struct generic_sensor common_sensor;
	//define user data below
	struct isp_data isp_priv_info;

};

#endif


