#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/circ_buf.h>
#include <linux/miscdevice.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <plat/rk_camera.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/firmware.h>

static int debug = 3;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

#define SENSOR_TR(format, ...) printk(KERN_ERR format, ## __VA_ARGS__)
#define SENSOR_DG(format, ...) dprintk(1, format, ## __VA_ARGS__)

#define _CONS(a,b) a##b
#define CONS(a,b) _CONS(a,b)

#define __STR(x) #x
#define _STR(x) __STR(x)
#define STR(x) _STR(x)

#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)    ((x>y) ? x: y)

/* Sensor Driver Configuration */
#define SENSOR_NAME RK29_CAM_ISP_MTK9335
#define SENSOR_V4L2_IDENT V4L2_IDENT_MTK9335ISP
#define SENSOR_ID 0x35
#define SENSOR_MIN_WIDTH    640//176
#define SENSOR_MIN_HEIGHT   480//144
#define SENSOR_MAX_WIDTH    2592
#define SENSOR_MAX_HEIGHT   1944
#define SENSOR_INIT_WIDTH	640			/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  480
#define SENSOR_INIT_WINSEQADR sensor_vga
#define SENSOR_INIT_PIXFMT V4L2_MBUS_FMT_YUYV8_2X8

#define CONFIG_SENSOR_WhiteBalance	1
#define CONFIG_SENSOR_Brightness	0
#define CONFIG_SENSOR_Contrast      0
#define CONFIG_SENSOR_Saturation    0
#define CONFIG_SENSOR_Effect        1
#define CONFIG_SENSOR_Scene         1
#define CONFIG_SENSOR_DigitalZoom   0
#define CONFIG_SENSOR_Focus         1
#define CONFIG_SENSOR_Exposure      1
#define CONFIG_SENSOR_Flash         0
#define CONFIG_SENSOR_Mirror        0
#define CONFIG_SENSOR_Flip          0
#define CONFIG_SENSOR_FOCUS_ZONE   1
#define CONFIG_SENSOR_FACE_DETECT   1

#if CONFIG_SENSOR_Focus
#define SENSOR_AF_MODE_CLOSE 0 
#define SENSOR_AF_SINGLE 1
#define SENSOR_AF_MACRO 2
#define SENSOR_AF_CONTINUOUS 5
#define SENSOR_AF_CONTINUOUS_OFF 6
static int sensor_set_auto_focus(struct i2c_client *client, int value);
#endif

#define CONFIG_SENSOR_I2C_SPEED     400000       /* Hz */
/* Sensor write register continues by preempt_disable/preempt_enable for current process not be scheduled */
#define CONFIG_SENSOR_I2C_NOSCHED   0
#define CONFIG_SENSOR_I2C_RDWRCHK   0

#define SENSOR_BUS_PARAM  (SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING|\
                          SOCAM_HSYNC_ACTIVE_HIGH| SOCAM_VSYNC_ACTIVE_HIGH|\
                          SOCAM_DATA_ACTIVE_HIGH|SOCAM_DATAWIDTH_8  |SOCAM_MCLK_24MHZ)

#define SENSOR_NAME_STRING(a) STR(CONS(SENSOR_NAME, a))
#define SENSOR_NAME_VARFUN(a) CONS(SENSOR_NAME, a)
// Using define data 

#define   I2C_IDLE   1 
#define   I2C_STATUS_REG2  0x0D 
#define   DEV_ID_MV9335_I2CSLAVE_FLASH_PRO (0x40 >> 1) 
#define   get_lsb(x)        (char)((u16)(x)& 0xFF)
#define   get_msb(x)        (char)(((u16)(x)>>8)& 0xFF)
#define   FLASH_ERASE_BY_SECTOR 0
#define   FLASH_ERASE_ALL 1
#define   WRITE_READ_CHECK 0
#define   FLASH_WRITE_CHECK 0

#define    PRODUCT_ID_VALUE_REG0 0x93
#define    PRODUCT_VERSION_VALUE_REG1 0x35
#define    I2C_CHECKE_VALUE_REG5 0x50
#define    CHECKE_VALUE_REG6 0x80

#define FWVERSION (0x09)
#if (FWVERSION == 0x03)
const char fw_name[] = {"mv9335_DS1001B_RK3066_OV5653_v1_0x03.bin"};
#define    FIRMWARE_MAJOR_VERSION_VALUE_REG2 0x01
#define    FIRMWARE_MINOR_VERSION_VALUE_REG3 0x03
#define    CHECKE_VALUE_REG0X90 0x3c
#define    CHECKE_VALUE_REG0X91 0x88
#elif(FWVERSION == 0x04)
const char fw_name[] = {"mv9335_DS1001B_RK3066_OV5653_v1_0x04.bin"};
#define    FIRMWARE_MAJOR_VERSION_VALUE_REG2 0x01
#define    FIRMWARE_MINOR_VERSION_VALUE_REG3 0x04
#define    CHECKE_VALUE_REG0X90 0xa4
#define    CHECKE_VALUE_REG0X91 0x5e
#elif(FWVERSION == 0x05) //no focus
const char fw_name[] = {"mv9335_DS1001B_RK3066_OV5653_v1_0x05.bin"};
#define    FIRMWARE_MAJOR_VERSION_VALUE_REG2 0x01
#define    FIRMWARE_MINOR_VERSION_VALUE_REG3 0x05
#define    CHECKE_VALUE_REG0X90 0x51
#define    CHECKE_VALUE_REG0X91 0x54
#elif(FWVERSION == 0x06) 
const char fw_name[] = {"mv9335_DS1001B_RK3066_OV5653_v1_0x06.bin"};
#define    FIRMWARE_MAJOR_VERSION_VALUE_REG2 0x01
#define    FIRMWARE_MINOR_VERSION_VALUE_REG3 0x06
#define    CHECKE_VALUE_REG0X90 0x90
#define    CHECKE_VALUE_REG0X91 0xe7
#elif(FWVERSION == 0x07) 
const char fw_name[] = {"mv9335_DS1001B_RK3066_OV5653_v1_0x07.bin"};
#define    FIRMWARE_MAJOR_VERSION_VALUE_REG2 0x01
#define    FIRMWARE_MINOR_VERSION_VALUE_REG3 0x07
#define    CHECKE_VALUE_REG0X90 0x22
#define    CHECKE_VALUE_REG0X91 0x8a
#elif(FWVERSION == 0x08) 
const char fw_name[] = {"mv9335_DS1001B_RK3066_OV5653_v1_0x08.bin"};
#define    FIRMWARE_MAJOR_VERSION_VALUE_REG2 0x01
#define    FIRMWARE_MINOR_VERSION_VALUE_REG3 0x08
#define    CHECKE_VALUE_REG0X90 0x7a
#define    CHECKE_VALUE_REG0X91 0x32
#elif(FWVERSION == 0x09) 
const char fw_name[] = {"mv9335_DS1001B_RK3066_OV5653_v1_0x09.bin"};
#define    FIRMWARE_MAJOR_VERSION_VALUE_REG2 0x01
#define    FIRMWARE_MINOR_VERSION_VALUE_REG3 0x09
#define    CHECKE_VALUE_REG0X90 0x48
#define    CHECKE_VALUE_REG0X91 0x3e

#else
const char fw_name[] = {"mv9335_DS1001B_RK3066_OV5653_v1_0x09.bin"};
#define    FIRMWARE_MAJOR_VERSION_VALUE_REG2 0x01
#define    FIRMWARE_MINOR_VERSION_VALUE_REG3 0x09
#define    CHECKE_VALUE_REG0X90 0x48
#define    CHECKE_VALUE_REG0X91 0x3e
#endif
struct reginfo
{
    u8 reg;
    u8 val;
};
	
/* only one fixed colorspace per pixelcode */
struct sensor_datafmt {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};	

enum ISP_OUTPUT_RES{
    OUTPUT_QCIF =0x0001, // 176*144
    OUTPUT_HQVGA=0x0002,// 240*160
    OUTPUT_QVGA =0x0004, // 320*240
    OUTPUT_CIF  =0x0008,  // 352*288
    OUTPUT_VGA  =0x0010,  // 640*480
    OUTPUT_SVGA =0x0020, // 800*600
    OUTPUT_720P =0x0040, // 1280*720
    OUTPUT_XGA  =0x0080,  // 1024*768
    OUTPUT_SXGA =0x0100, // 1280*1024
    OUTPUT_UXGA =0x0200, // 1600*1200
    OUTPUT_1080P=0x0400, //1920*1080
    OUTPUT_QXGA =0x0800,  // 2048*1536
    OUTPUT_QSXGA=0x1000, // 2592*1944
};
static u8 sendI2cCmd(struct i2c_client *client,u8 cmd, u8 dat); 
static int isp_i2c_read(struct i2c_client *client, u8 reg, u8 *val,u16 ext_addr);
static int flash_read_firmware_data(struct i2c_client *client,u16 addr,u8* rd_data,int readcount);
static int flash_read_data(struct i2c_client *client,u8* rd_data,int readcount);
static int isp_init_check(struct i2c_client *client);
static int isp_init_cmds(struct i2c_client *client);
static struct soc_camera_ops sensor_ops;

#if CONFIG_SENSOR_DigitalZoom
static int sensor_set_digitalzoom(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int *value);
#endif 


struct focus_zone_s{
u8 lx;
u8 ty;
u8 rx;
u8 dy;
};

//flash and focus must be considered.

//soft isp or external isp used
//if soft isp is defined , the value in this sturct is used in cif driver . cif driver use this value to do isp func.
//value of this sturct MUST be defined(initialized) correctly. 
struct isp_data{

	//focus
	//flash
	  int focus;
    int auto_focus;
    int flash;
	//awb
	//ae
	//scence
	//effect
	//brightness
	//digitalzoom
    int whiteBalance;
    int brightness;
    int contrast;
    int saturation;
    int effect;
    int scene;
    int digitalzoom;
    int exposure;
    int face;
	//mirror or flip
    unsigned char mirror;                                        /* HFLIP */
    unsigned char flip;                                          /* VFLIP */
	//preview or capture
	int outputSize; // supported resolution
	int curRes;
    struct focus_zone_s focus_zone;
    struct sensor_datafmt fmt;
	//mutex for access the isp data
	struct mutex access_data_lock;
};


struct isp_func_ops{
	int (*isp_power_on)(void);
	int (*isp_power_off)(void);
	//realized by isp or external dev
	int (*isp_set_focus_mode)(int value,struct isp_data* data);
	int (*isp_set_flash_mode)( int value,struct isp_data* data);

	//by soft isp or external isp
	int (*isp_set_wb_mode)( int value,struct isp_data* data);
	int (*isp_set_effect_mode)( int value,struct isp_data* data);
	int (*isp_set_scence_mode)( int value,struct isp_data* data);
	int (*isp_set_expose_mode)( int value,struct isp_data* data);
	int (*isp_set_digitalzoom_mode)( int value,struct isp_data* data);
	int (*isp_set_flip)( int value,struct isp_data* data);
	int (*isp_set_mirror)( int value,struct isp_data* data);
};

struct isp_dev{
    struct v4l2_subdev subdev;
    struct i2c_client *client;
	struct workqueue_struct *sensor_wq;
	struct mutex wq_lock;
    int model;	/* V4L2_IDENT_OV* codes from v4l2-chip-ident.h */
    struct isp_func_ops* isp_ops;
    struct isp_data isp_priv_info;	
	};

static struct isp_dev* to_sensor(const struct i2c_client *client)
{
    return container_of(i2c_get_clientdata(client), struct isp_dev, subdev);
}

/* Find a data format by a pixel code in an array */
static const struct sensor_datafmt *sensor_find_datafmt(
	enum v4l2_mbus_pixelcode code, const struct sensor_datafmt *fmt,
	int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (fmt[i].code == code)
			return fmt + i;

	return NULL;
}

static const struct sensor_datafmt sensor_colour_fmts[] = {
    {V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG}	
};

static int isp_reg_value_check(struct i2c_client *client,u8 checked_reg,u8 checked_value){
    int cnt = 100;
    u8 check_val = 0;
       // msleep(10);
     while((cnt-- > 0) && (isp_i2c_read(client,checked_reg,&check_val,0) || (check_val != checked_value))){
      //  SENSOR_TR("error: %s:%d   check_val = 0x%x, exp = 0x%x\n", __func__,__LINE__, check_val,checked_value);
        mdelay(30);
    }
     if(cnt <= 0){
        SENSOR_TR("error: %s:%d  reg value checked erro,check_val = 0x%x, exp = 0x%x!\n", __func__,__LINE__,check_val,checked_value);
        return -1;
     }
     return 0;

}

#if CONFIG_SENSOR_WhiteBalance
static int sensor_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    u8 set_val = 0;

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        switch(value){
            case 0: //enable auto
                set_val = 0x1;
                break;
            case 1: //incandescent
                set_val = 0x45;
                break;
            case 2: //fluorescent
                set_val = 0x25;
                break;
            case 3: //daylight
                set_val = 0x15;
                break;
            case 4: //cloudy-daylight
                set_val = 0x35;
                break;
            default:
                break;
        }
        //awb
        sendI2cCmd(client, 0x0E, 0x00);
        sendI2cCmd(client, 0x1c, set_val);
        isp_reg_value_check(client,0x0E,0xBB);
        return 0;
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -1;
}
#endif
#if CONFIG_SENSOR_Effect
static int sensor_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    u8 set_val = 0;
    printk("set effect,value = %d ......\n",value);
    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        switch(value){
            case 0: //none
                set_val = 0x00;
                break;
            case 1: //mono
                set_val = 0x01;
                break;
            case 2: //negative
                set_val = 0x02;
                break;
            case 3: //sepia
                set_val = 0x03;
                break;
            case 4: //aqua
                set_val = 0x04;
                break;
            default:
                break;
        }
        //image effect
        sendI2cCmd(client, 0x0E, 0x00);
        sendI2cCmd(client, 0x26, set_val);
        isp_reg_value_check(client,0x0E,0xBB);
        return 0;
    }
	SENSOR_TR("\n%s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -1;
}
#endif

static const struct v4l2_querymenu sensor_menus[] =
{
	#if CONFIG_SENSOR_Effect
    { .id = V4L2_CID_EFFECT,  .index = 0,  .name = "normal",  .reserved = 0, }, {  .id = V4L2_CID_EFFECT,  .index = 1, .name = "mono",  .reserved = 0,},
    { .id = V4L2_CID_EFFECT,  .index = 2,  .name = "negative", .reserved = 0,}, {  .id = V4L2_CID_EFFECT, .index = 3,  .name = "sepia", .reserved = 0,},
    { .id = V4L2_CID_EFFECT,  .index = 4,  .name = "aqua", .reserved = 0,},
     #endif
    
	#if CONFIG_SENSOR_WhiteBalance
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 0,  .name = "auto",  .reserved = 0, }, {  .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 1, .name = "incandescent",  .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 2,  .name = "fluorescent", .reserved = 0,}, {  .id = V4L2_CID_DO_WHITE_BALANCE, .index = 3,  .name = "daylight", .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 4,  .name = "cloudy-daylight", .reserved = 0,},
    #endif


	#if CONFIG_SENSOR_Scene
    { .id = V4L2_CID_SCENE,  .index = 0, .name = "normal", .reserved = 0,} ,{ .id = V4L2_CID_SCENE,  .index = 1,  .name = "auto", .reserved = 0,},
    { .id = V4L2_CID_SCENE,  .index = 2, .name = "landscape", .reserved = 0,} ,{ .id = V4L2_CID_SCENE,  .index = 3,  .name = "night", .reserved = 0,},
    { .id = V4L2_CID_SCENE,  .index = 4, .name = "night_portrait", .reserved = 0,} ,{ .id = V4L2_CID_SCENE,  .index = 5,  .name = "snow", .reserved = 0,},
    { .id = V4L2_CID_SCENE,  .index = 6, .name = "sports", .reserved = 0,} ,{ .id = V4L2_CID_SCENE,  .index = 7,  .name = "candlelight", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Flash
    { .id = V4L2_CID_FLASH,  .index = 0,  .name = "off",  .reserved = 0, }, {  .id = V4L2_CID_FLASH,  .index = 1, .name = "auto",  .reserved = 0,},
    { .id = V4L2_CID_FLASH,  .index = 2,  .name = "on", .reserved = 0,}, {  .id = V4L2_CID_FLASH, .index = 3,  .name = "torch", .reserved = 0,},
    #endif
};

static  struct v4l2_queryctrl sensor_controls[] =
{
	#if CONFIG_SENSOR_WhiteBalance
    {
        .id		= V4L2_CID_DO_WHITE_BALANCE,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "White Balance Control",
        .minimum	= 0,
        .maximum	= 4,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Brightness
	{
        .id		= V4L2_CID_BRIGHTNESS,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Brightness Control",
        .minimum	= -3,
        .maximum	= 2,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Effect
	{
        .id		= V4L2_CID_EFFECT,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Effect Control",
        .minimum	= 0,
        .maximum	= 4,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Exposure
	{
        .id		= V4L2_CID_EXPOSURE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Exposure Control",
        .minimum	= 0,
        .maximum	= 5,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Saturation
	{
        .id		= V4L2_CID_SATURATION,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Saturation Control",
        .minimum	= 0,
        .maximum	= 2,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Contrast
	{
        .id		= V4L2_CID_CONTRAST,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Contrast Control",
        .minimum	= -3,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Mirror
	{
        .id		= V4L2_CID_HFLIP,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Mirror Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 1,
    },
    #endif

	#if CONFIG_SENSOR_Flip
	{
        .id		= V4L2_CID_VFLIP,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Flip Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 1,
    },
    #endif

	#if CONFIG_SENSOR_Scene
    {
        .id		= V4L2_CID_SCENE,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Scene Control",
        .minimum	= 0,
        .maximum	= 7,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_DigitalZoom
    {
        .id		= V4L2_CID_ZOOM_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "DigitalZoom Control",
        .minimum	= 100,
        .maximum	= 275, // app pass 275-25 maximum
        .step		= 25,
        .default_value = 100,
    }, 
    #endif

	#if CONFIG_SENSOR_Focus
	/*{
        .id		= V4L2_CID_FOCUS_RELATIVE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Focus Control",
        .minimum	= -1,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    }, */
    {
        .id		= V4L2_CID_FOCUS_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 2,
        .step		= 1,
        .default_value = 0,
    },
	{
        .id		= V4L2_CID_FOCUS_AUTO,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    },{
        .id		= V4L2_CID_FOCUS_CONTINUOUS,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Flash
	{
        .id		= V4L2_CID_FLASH,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Flash Control Focus",
        .minimum	= 0,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    },
	#endif
    #if CONFIG_SENSOR_FOCUS_ZONE
	{
        .id		= V4L2_CID_FOCUSZONE,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Focus Zone support",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 1,
    },
	#endif
	#if	CONFIG_SENSOR_FACE_DETECT
	{
	.id 	= V4L2_CID_FACEDETECT,
	.type		= V4L2_CTRL_TYPE_BOOLEAN,
	.name		= "face dectect support",
	.minimum	= 0,
	.maximum	= 1,
	.step		= 1,
	.default_value = 1,
	},
	#endif

};


static int sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct isp_dev *sensor = to_sensor(client);
    const struct v4l2_queryctrl *qctrl;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
        return -EINVAL;
    }

    switch (ctrl->id)
    {
        case V4L2_CID_BRIGHTNESS:
            {
                ctrl->value = sensor->isp_priv_info.brightness;
                break;
            }
        case V4L2_CID_SATURATION:
            {
                ctrl->value = sensor->isp_priv_info.saturation;
                break;
            }
        case V4L2_CID_CONTRAST:
            {
                ctrl->value = sensor->isp_priv_info.contrast;
                break;
            }
        case V4L2_CID_DO_WHITE_BALANCE:
            {
                ctrl->value = sensor->isp_priv_info.whiteBalance;
                break;
            }
        case V4L2_CID_EXPOSURE:
            {
                ctrl->value = sensor->isp_priv_info.exposure;
                break;
            }
        case V4L2_CID_HFLIP:
            {
                ctrl->value = sensor->isp_priv_info.mirror;
                break;
            }
        case V4L2_CID_VFLIP:
            {
                ctrl->value = sensor->isp_priv_info.flip;
                break;
            }
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                ctrl->value = sensor->isp_priv_info.digitalzoom;
                break;
            }
        default :
                break;
    }
    return 0;
}
static int sensor_set_face_detect(struct i2c_client *client, int value);
#if CONFIG_SENSOR_Exposure
static int sensor_set_exposure(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    u8 set_val = 0x0 ,ac_val = 0x01;
    printk("set iso ,value = %d......\n", value);

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        switch(value){
            case 0: //iso auto
                set_val = 0x0;
                break;
            case 1: //iso 100
                set_val = 0x1;
                break;
            case 2: //iso 200
                set_val = 0x2;
                break;
            case 3: //iso 400
                set_val = 0x3;
                break;
            case 4: //iso 800
                set_val = 0x4;
                break;
            case 5: //iso 1600
                set_val = 0x5;
                break;
            default:
                break;
        }
        
        //iso
        sendI2cCmd(client, 0x0E, 0x00);
        sendI2cCmd(client, 0x17, set_val);
        isp_reg_value_check(client,0x0E,0xBB);

        //AC freq :50Hz
        sendI2cCmd(client, 0x0E, 0x00);
        sendI2cCmd(client, 0x16, 0x01);
        isp_reg_value_check(client,0x0E,0xBB);
        return 0;
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif


static int sensor_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct isp_dev *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;
    const struct v4l2_queryctrl *qctrl;


    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
        return -EINVAL;
    }

    switch (ctrl->id)
    {
#if CONFIG_SENSOR_Brightness
        case V4L2_CID_BRIGHTNESS:
            {
                if (ctrl->value != sensor->isp_priv_info.brightness)
                {
                    if (sensor_set_brightness(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->isp_priv_info.brightness = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Exposure
        case V4L2_CID_EXPOSURE:
            {
                if (ctrl->value != sensor->isp_priv_info.exposure)
                {
                    if (sensor_set_exposure(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->isp_priv_info.exposure = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Saturation
        case V4L2_CID_SATURATION:
            {
                if (ctrl->value != sensor->isp_priv_info.saturation)
                {
                    if (sensor_set_saturation(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->isp_priv_info.saturation = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Contrast
        case V4L2_CID_CONTRAST:
            {
                if (ctrl->value != sensor->isp_priv_info.contrast)
                {
                    if (sensor_set_contrast(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->isp_priv_info.contrast = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_WhiteBalance
        case V4L2_CID_DO_WHITE_BALANCE:
            {
                if (ctrl->value != sensor->isp_priv_info.whiteBalance)
                {
                    if (sensor_set_whiteBalance(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->isp_priv_info.whiteBalance = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Mirror
        case V4L2_CID_HFLIP:
            {
                if (ctrl->value != sensor->isp_priv_info.mirror)
                {
                    if (sensor_set_mirror(icd, qctrl,ctrl->value) != 0)
                        return -EINVAL;
                    sensor->isp_priv_info.mirror = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Flip
        case V4L2_CID_VFLIP:
            {
                if (ctrl->value != sensor->isp_priv_info.flip)
                {
                    if (sensor_set_flip(icd, qctrl,ctrl->value) != 0)
                        return -EINVAL;
                    sensor->isp_priv_info.flip = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_DigitalZoom
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                int val_offset = 0;
                printk("V4L2_CID_ZOOM_ABSOLUTE ...... ctrl->value = %d\n",ctrl->value);
                if ((ctrl->value < qctrl->minimum) || (ctrl->value > qctrl->maximum)){
                    return -EINVAL;
                    }

                if (ctrl->value != sensor->isp_priv_info.digitalzoom)
                {
                    val_offset = ctrl->value -sensor->isp_priv_info.digitalzoom;

                    if (sensor_set_digitalzoom(icd, qctrl,&val_offset) != 0)
                        return -EINVAL;
                    sensor->isp_priv_info.digitalzoom += val_offset;

                    SENSOR_DG("%s digitalzoom is %x\n",SENSOR_NAME_STRING(),  sensor->isp_priv_info.digitalzoom);
                }

                break;
            }
        case V4L2_CID_ZOOM_RELATIVE:
            {
                if (ctrl->value)
                {
                    if (sensor_set_digitalzoom(icd, qctrl,&ctrl->value) != 0)
                        return -EINVAL;
                    sensor->isp_priv_info.digitalzoom += ctrl->value;

                    SENSOR_DG("%s digitalzoom is %x\n", SENSOR_NAME_STRING(), sensor->isp_priv_info.digitalzoom);
                }
                break;
            }
#endif
        default:
            break;
    }

    return 0;
}
static int sensor_g_ext_control(struct soc_camera_device *icd , struct v4l2_ext_control *ext_ctrl)
{
    const struct v4l2_queryctrl *qctrl;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct isp_dev *sensor = to_sensor(client);

    qctrl = soc_camera_find_qctrl(&sensor_ops, ext_ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
        return -EINVAL;
    }

    switch (ext_ctrl->id)
    {
        case V4L2_CID_SCENE:
            {
                ext_ctrl->value = sensor->isp_priv_info.scene;
                break;
            }
        case V4L2_CID_EFFECT:
            {
                ext_ctrl->value = sensor->isp_priv_info.effect;
                break;
            }
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                ext_ctrl->value = sensor->isp_priv_info.digitalzoom;
                break;
            }
        case V4L2_CID_ZOOM_RELATIVE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FOCUS_ABSOLUTE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FOCUS_RELATIVE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FLASH:
            {
                ext_ctrl->value = sensor->isp_priv_info.flash;
                break;
            }
	 case V4L2_CID_FACEDETECT:
		{
	 	ext_ctrl->value =sensor->isp_priv_info.face ;
		break;
	 	}
        default :
            break;
    }
    return 0;
}
#if CONFIG_SENSOR_Focus
static int sensor_set_auto_focus(struct i2c_client *client, int value)
{
    struct isp_dev *sensor = to_sensor(client);
    u8 zone_x = 0x0,zone_y = 0x0; // 0->0x0f
    int ret = 0;
//	return 0;
    //set the zone
 //   printk("lx = %x,rx = %x,ty = %x,dy = %x\n",sensor->isp_priv_info.focus_zone.lx,sensor->isp_priv_info.focus_zone.rx,sensor->isp_priv_info.focus_zone.ty,sensor->isp_priv_info.focus_zone.dy);
    zone_x = (sensor->isp_priv_info.focus_zone.lx << 4) | (sensor->isp_priv_info.focus_zone.rx & 0x0f);
    zone_y = (sensor->isp_priv_info.focus_zone.ty << 4) | (sensor->isp_priv_info.focus_zone.dy & 0x0f);
    //auto focus
    sendI2cCmd(client, 0x0E, 0x00);
    if((zone_x != 0) && (zone_y !=0)){
        sendI2cCmd(client, 0x21, zone_x);
        sendI2cCmd(client, 0x22, zone_y);
        }else{
        sendI2cCmd(client, 0x21, 0x6a);
        sendI2cCmd(client, 0x22, 0x6a);
        }
    printk("%s:auto focus, val = %d,zone_x = %x, zone_y = %x\n",__func__,value,zone_x,zone_y);
    sendI2cCmd(client, 0x23, value);
    ret = isp_reg_value_check(client,0x0E,0xBB);
    if(ret == 0xCC){
        printk("%s:%d,auto focus failed!\n",__func__,__LINE__);
        }
  //  printk("%s:auto focus done\n",__func__);
    return 0;
}

enum sensor_work_state
{
	sensor_work_ready = 0,
	sensor_working,
};
enum sensor_wq_result
{
    WqRet_success = 0,
    WqRet_fail = -1,
    WqRet_inval = -2
};
enum sensor_wq_cmd
{
    WqCmd_af_init,
    WqCmd_af_single,
    WqCmd_af_special_pos,
    WqCmd_af_far_pos,
    WqCmd_af_near_pos,
    WqCmd_af_continues,
    WqCmd_af_return_idle,
};
struct sensor_work
{
	struct i2c_client *client;
	struct delayed_work dwork;
	enum sensor_wq_cmd cmd;
    wait_queue_head_t done;
    enum sensor_wq_result result;
    bool wait;
    int var;    
};

static void sensor_af_workqueue(struct work_struct *work)
{
	struct sensor_work *sensor_work = container_of(work, struct sensor_work, dwork.work);
	struct i2c_client *client = sensor_work->client;
    struct isp_dev *sensor = to_sensor(client);
    
    SENSOR_DG("%s %s Enter, cmd:0x%x \n",SENSOR_NAME_STRING(), __FUNCTION__,sensor_work->cmd);
    
    mutex_lock(&sensor->wq_lock);
    
//    printk("%s:auto focus, val = %d\n",__func__,sensor_work->var);
    //auto focus
    if(sensor_set_auto_focus(client,sensor_work->var) == 0){
        sensor_work->result = WqRet_success;
        }else{
        printk("%s:auto focus failed\n",__func__);
        }
//    printk("%s:auto focus done\n",__func__);
    
//set_end:
    if (sensor_work->wait == false) {
        kfree((void*)sensor_work);
    } else {
        wake_up(&sensor_work->done); 
    }
    mutex_unlock(&sensor->wq_lock); 
    return;
}

static int sensor_af_workqueue_set(struct soc_camera_device *icd, enum sensor_wq_cmd cmd, int var, bool wait)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct isp_dev *sensor = to_sensor(client); 
    struct sensor_work *wk;
    int ret=0;

    if (sensor->sensor_wq == NULL) { 
        ret = -EINVAL;
        goto sensor_af_workqueue_set_end;
    }

    wk = kzalloc(sizeof(struct sensor_work), GFP_KERNEL);
    if (wk) {
	    wk->client = client;
	    INIT_WORK(&(wk->dwork.work), sensor_af_workqueue);
        wk->cmd = cmd;
        wk->result = WqRet_inval;
        wk->wait = wait;
        wk->var = var;
        init_waitqueue_head(&wk->done);
        
	    queue_delayed_work(sensor->sensor_wq,&(wk->dwork),0);
        
        /* ddl@rock-chips.com: 
        * video_lock is been locked in v4l2_ioctl function, but auto focus may slow,
        * As a result any other ioctl calls will proceed very, very slowly since each call
        * will have to wait for the AF to finish. Camera preview is pause,because VIDIOC_QBUF 
        * and VIDIOC_DQBUF is sched. so unlock video_lock here.
        */
        if (wait == true) {
            mutex_unlock(&icd->video_lock);
            if (wait_event_timeout(wk->done, (wk->result != WqRet_inval), msecs_to_jiffies(5000)) == 0) {  //hhb
                SENSOR_TR("%s %s cmd(%d) is timeout!\n",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
            }
            ret = wk->result;
            kfree((void*)wk);
            mutex_lock(&icd->video_lock);  
        }
        
    } else {
        SENSOR_TR("%s %s cmd(%d) ingore,because struct sensor_work malloc failed!",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
        ret = -1;
    }
sensor_af_workqueue_set_end:
    return ret;
}
#endif


#if CONFIG_SENSOR_Scene
static int sensor_set_scene(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
//when scene mod is working , face deteciton and awb and iso are not recomemnded.
    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
            sendI2cCmd(client, 0x0E, 0x00);
        //    sendI2cCmd(client, 0x13, value);
            isp_reg_value_check(client,0x0E,0xBB);
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
static int sensor_set_face_detect(struct i2c_client *client, int value)
{
	u8 orien, window;
	window = (value & 0x100)>>8;
    //face detect
    switch (value & 0x1f){
	case 0:
		orien = 0x02; //off
		break;
  	case 1:
		orien = 0x80; //default on
		break;
	case 2:
		orien = 0x90; //90 degree
		break;
	case 3:
		orien = 0xb0; //180 degree
		break;
	case 4:
		orien = 0xa0; //270 degree
		break;
	default:
		orien = 0x02; //off

	}
	printk("orien = %x,window = %d \n",orien,window);
        sendI2cCmd(client, 0x0E, 0x00);
        sendI2cCmd(client, 0x2a, orien);
        isp_reg_value_check(client,0x0E,0xBB);

        sendI2cCmd(client, 0x0E, 0x00);
        sendI2cCmd(client, 0x29, window);
        isp_reg_value_check(client,0x0E,0xBB);
    
    SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
    return 0;
}
#if CONFIG_SENSOR_Flip
//off 0x00;mirror 0x01,flip 0x10;
static int sensor_set_flip(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int *value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	sendI2cCmd(client, 0x0E, 0x00);
	sendI2cCmd(client, 0x28, 0x10);
	isp_reg_value_check(client,0x0E,0xBB);
	return 0;

}
#endif
#if CONFIG_SENSOR_Mirror
static int sensor_set_mirror(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int *value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	sendI2cCmd(client, 0x0E, 0x00);
	sendI2cCmd(client, 0x28, 0x01);
	isp_reg_value_check(client,0x0E,0xBB);
	return 0;
}
#endif

#if CONFIG_SENSOR_DigitalZoom
static int sensor_set_digitalzoom(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int *value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct isp_dev *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl_info;
    int digitalzoom_cur, digitalzoom_total;
    u8 zoom_val = 0;

	qctrl_info = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_ZOOM_ABSOLUTE);
	if (!qctrl_info){
		return -EINVAL;
	    }

    digitalzoom_cur = sensor->isp_priv_info.digitalzoom;
    digitalzoom_total = qctrl_info->maximum;

    if ((*value > 0) && (digitalzoom_cur >= digitalzoom_total))
    {
        SENSOR_TR("%s digitalzoom is maximum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if  ((*value < 0) && (digitalzoom_cur <= qctrl_info->minimum))
    {
        SENSOR_TR("%s digitalzoom is minimum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if ((*value > 0) && ((digitalzoom_cur + *value) > digitalzoom_total))
    {
        *value = digitalzoom_total - digitalzoom_cur;
    }

    if ((*value < 0) && ((digitalzoom_cur + *value) < 0))
    {
        *value = 0 - digitalzoom_cur;
    }

    digitalzoom_cur += *value;
    printk("digitalzoom_cur = %d =====\n",digitalzoom_cur);
    switch(digitalzoom_cur){
    case 100 :
        zoom_val = 0;
        break;
    case 125 :
        zoom_val = 1;
        break;
    case 150 :
        zoom_val = 2;
        break;
    case 175 :
        zoom_val = 3;
        break;
    case 200 :
        zoom_val = 4;
        break;
    case 225 :
        zoom_val = 5;
        break;
    case 250 :
        zoom_val = 6;
        break;
    case 275 :
        zoom_val = 7;
        break;
    case 300 :
        zoom_val = 8;
        break;
    }
    
    sendI2cCmd(client, 0x0E, 0x00);
    sendI2cCmd(client, 0x2b, zoom_val);
    isp_reg_value_check(client,0x0E,0xBB);
    mdelay(5);
    return 0;
}
#endif
static int sensor_s_ext_control(struct soc_camera_device *icd, struct v4l2_ext_control *ext_ctrl)
{
    const struct v4l2_queryctrl *qctrl;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct isp_dev *sensor = to_sensor(client);
    int val_offset;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ext_ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
        return -EINVAL;
    }

	val_offset = 0;
    switch (ext_ctrl->id)
    {
#if CONFIG_SENSOR_Scene
        case V4L2_CID_SCENE:
            {
                if (ext_ctrl->value != sensor->isp_priv_info.scene)
                {
                    if (sensor_set_scene(icd, qctrl,ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->isp_priv_info.scene = ext_ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Effect
        case V4L2_CID_EFFECT:
            {   
                if (ext_ctrl->value != sensor->isp_priv_info.effect)
                {                    
                    if (sensor_set_effect(icd, qctrl,ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->isp_priv_info.effect= ext_ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_DigitalZoom
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                printk("V4L2_CID_ZOOM_ABSOLUTE ...... ext_ctrl->value = %d\n",ext_ctrl->value);
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum)){
                    return -EINVAL;
                    }

                if (ext_ctrl->value != sensor->isp_priv_info.digitalzoom)
                {
                    val_offset = ext_ctrl->value -sensor->isp_priv_info.digitalzoom;

                    if (sensor_set_digitalzoom(icd, qctrl,&val_offset) != 0)
                        return -EINVAL;
                    sensor->isp_priv_info.digitalzoom += val_offset;

                    SENSOR_DG("%s digitalzoom is %x\n",SENSOR_NAME_STRING(),  sensor->isp_priv_info.digitalzoom);
                }

                break;
            }
        case V4L2_CID_ZOOM_RELATIVE:
            {
                if (ext_ctrl->value)
                {
                    if (sensor_set_digitalzoom(icd, qctrl,&ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->isp_priv_info.digitalzoom += ext_ctrl->value;

                    SENSOR_DG("%s digitalzoom is %x\n", SENSOR_NAME_STRING(), sensor->isp_priv_info.digitalzoom);
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Focus
        case V4L2_CID_FOCUS_ABSOLUTE:
            {
		//DO MACRO
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
                    return -EINVAL;
		
		if (SENSOR_AF_CONTINUOUS == sensor->isp_priv_info.auto_focus) {
			sensor_af_workqueue_set(icd,0,SENSOR_AF_CONTINUOUS_OFF,true);
			}
		if (ext_ctrl->value == 1) {
			sensor_af_workqueue_set(icd,0,SENSOR_AF_MACRO,true);
			sensor->isp_priv_info.auto_focus = SENSOR_AF_MACRO;
		} else if(ext_ctrl->value == 0){
			 sensor_af_workqueue_set(icd,0,SENSOR_AF_MODE_CLOSE,true);
			sensor->isp_priv_info.auto_focus = SENSOR_AF_MODE_CLOSE;
		}
                break;
            }
        case V4L2_CID_FOCUS_RELATIVE:
            {
          //      if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
        //            return -EINVAL;

         //       sensor_set_focus_relative(icd, qctrl,ext_ctrl->value);
                break;
            }
		
	case V4L2_CID_FOCUS_AUTO:
			{
				
				if (SENSOR_AF_CONTINUOUS == sensor->isp_priv_info.auto_focus) {
					sensor_af_workqueue_set(icd,0,SENSOR_AF_CONTINUOUS_OFF,true);
				}
				if (ext_ctrl->value == 1) {
	                            sensor_af_workqueue_set(icd,0,SENSOR_AF_SINGLE,true);
					sensor->isp_priv_info.auto_focus = SENSOR_AF_SINGLE;
				} else if(ext_ctrl->value == 0){
		                     sensor_af_workqueue_set(icd,0,SENSOR_AF_MODE_CLOSE,true);
					sensor->isp_priv_info.auto_focus = SENSOR_AF_MODE_CLOSE;
				}
				break;
			}
		case V4L2_CID_FOCUS_CONTINUOUS:
			{
				if ((ext_ctrl->value == 1) && (SENSOR_AF_CONTINUOUS != sensor->isp_priv_info.auto_focus)) {
					sensor_af_workqueue_set(icd,0,SENSOR_AF_MODE_CLOSE,true);
					 sensor_af_workqueue_set(icd,0,SENSOR_AF_CONTINUOUS,true);
					sensor->isp_priv_info.auto_focus = SENSOR_AF_CONTINUOUS;
				}else if(ext_ctrl->value == 0){
						sensor_af_workqueue_set(icd,0,SENSOR_AF_CONTINUOUS_OFF,true);
						sensor->isp_priv_info.auto_focus = SENSOR_AF_CONTINUOUS_OFF;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_Flash
        case V4L2_CID_FLASH:
            {
                if (sensor_set_flash(icd, qctrl,ext_ctrl->value) != 0)
                    return -EINVAL;
                sensor->info_priv.flash = ext_ctrl->value;

                SENSOR_DG("%s flash is %x\n",SENSOR_NAME_STRING(), sensor->isp_priv_info.flash);
                break;
            }
#endif
#if CONFIG_SENSOR_FACE_DETECT
	case V4L2_CID_FACEDETECT:
		{
			if(sensor->isp_priv_info.face != ext_ctrl->value){
				if (sensor_set_face_detect(client, ext_ctrl->value) != 0)
					return -EINVAL;
				sensor->isp_priv_info.face = ext_ctrl->value;
				SENSOR_DG("%s flash is %x\n",SENSOR_NAME_STRING(), sensor->isp_priv_info.face);
				}
			break;
		}
#endif
        default:
            break;
    }

    return 0;
}

static int sensor_g_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    int i, error_cnt=0, error_idx=-1;

    for (i=0; i<ext_ctrl->count; i++) {
        if (sensor_g_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
            error_cnt++;
            error_idx = i;
        }
    }

    if (error_cnt > 1)
        error_idx = ext_ctrl->count;

    if (error_idx != -1) {
        ext_ctrl->error_idx = error_idx;
        return -EINVAL;
    } else {
        return 0;
    }
}
static int sensor_s_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct isp_dev *sensor = to_sensor(client);

    int i, error_cnt=0, error_idx=-1;
    for (i=0; i<ext_ctrl->count; i++) {
        if(ext_ctrl->controls[i].id == V4L2_CID_FOCUS_AUTO){
            int lx,ty,rx,dy;
		/*
            lx = (s16)((int)(ext_ctrl->reserved[0])>>16);
            ty = (s16)(ext_ctrl->reserved[0]);
            rx = (s16)((int)(ext_ctrl->reserved[1])>>16);
            dy = (s16)(ext_ctrl->reserved[1]);
            */
            //
            lx = ext_ctrl->controls[i].rect[0];
            ty = ext_ctrl->controls[i].rect[1];
            rx = ext_ctrl->controls[i].rect[2];
            dy = ext_ctrl->controls[i].rect[3];
            printk("lx = %d,ty = %d,rx = %d,dy = %d\n",lx,ty,rx,dy);
            sensor->isp_priv_info.focus_zone.lx = (lx+1000)*16/2000;
            sensor->isp_priv_info.focus_zone.ty = (ty+1000)*16/2000;
            sensor->isp_priv_info.focus_zone.rx = (rx+1000)*16/2000;
            sensor->isp_priv_info.focus_zone.dy = (dy+1000)*16/2000;
        //    printk("lx = %x,ty = %x,rx = %x,dy = %x\n",sensor->isp_priv_info.focus_zone.lx,sensor->isp_priv_info.focus_zone.ty,sensor->isp_priv_info.focus_zone.rx,sensor->isp_priv_info.focus_zone.dy);
            if((lx == 0)&&(ty == 0)&&(rx==0)&&(dy==0)){
                sensor->isp_priv_info.focus_zone.lx = 0;
                sensor->isp_priv_info.focus_zone.ty = 0;
                sensor->isp_priv_info.focus_zone.rx = 0;
                sensor->isp_priv_info.focus_zone.dy = 0;
                }
            if(sensor->isp_priv_info.focus_zone.lx > 0xf){
                sensor->isp_priv_info.focus_zone.lx = 0xf;
            } 
            if(sensor->isp_priv_info.focus_zone.ty > 0xf){
                sensor->isp_priv_info.focus_zone.ty = 0xf;
            }
            if(sensor->isp_priv_info.focus_zone.rx > 0xf){
                sensor->isp_priv_info.focus_zone.rx = 0xf;
            } 
            if(sensor->isp_priv_info.focus_zone.dy > 0xf){
                sensor->isp_priv_info.focus_zone.dy = 0xf;
            }                
         }
        if(ext_ctrl->controls[i].id == V4L2_CID_ZOOM_ABSOLUTE){
            printk("%s: digtal zoom \n",__func__);
        }

        if (sensor_s_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
            error_cnt++;
            error_idx = i;
        }
    }

    if (error_cnt > 1)
        error_idx = ext_ctrl->count;

    if (error_idx != -1) {
        ext_ctrl->error_idx = error_idx;
        return -EINVAL;
    } else {
        return 0;
    }
}

//640*480 default
static int sensor_set_isp_output_res(struct i2c_client *client,enum ISP_OUTPUT_RES outputSize){
    u8 check_val = 0;
    struct isp_dev *sensor = to_sensor(client);
    switch(outputSize)
        {
        case OUTPUT_QCIF:
            {
             SENSOR_TR(" SET qcif!\n");
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x10, 0x05);
            isp_reg_value_check(client,0x0E,0xBB);
            
            //isp output 176*144
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x11, 0x00);
            isp_reg_value_check(client,0x0E,0xBB);

            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x07, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x08, 0x00);
            isp_reg_value_check(client,0x0E,0xBB);
            }
            break;
        case OUTPUT_HQVGA:
            {
             SENSOR_TR(" SET hqvga !\n");
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x10, 0x05);
            isp_reg_value_check(client,0x0E,0xBB);
            
            //isp output 240*160
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x11, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);

            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x07, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x08, 0x00);
            isp_reg_value_check(client,0x0E,0xBB);
		break;
            }
        case OUTPUT_QVGA:
            {
             SENSOR_TR(" SET qvga RES!\n");
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x10, 0x05);
            isp_reg_value_check(client,0x0E,0xBB);
            
            //isp output 320*240
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x11, 0x02);
            isp_reg_value_check(client,0x0E,0xBB);

            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x07, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x08, 0x00);
            isp_reg_value_check(client,0x0E,0xBB);
            }
            break;
        case OUTPUT_CIF:
            {
             SENSOR_TR(" SET cif RES!\n");
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x10, 0x05);
            isp_reg_value_check(client,0x0E,0xBB);
            
            //isp output 352*288
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x11, 0x04);
            isp_reg_value_check(client,0x0E,0xBB);

            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x07, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x08, 0x00);
            isp_reg_value_check(client,0x0E,0xBB);
            }

            break;
        case OUTPUT_VGA:
            {
             SENSOR_TR(" SET VGA RES!\n");
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x10, 0x05);
            isp_reg_value_check(client,0x0E,0xBB);
            
            //isp output 640x480
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x11, 0x03);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x07, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x08, 0x00);
            isp_reg_value_check(client,0x0E,0xBB);
            }
            break;
        case OUTPUT_SVGA:
            {
             SENSOR_TR(" SET SVGA RES!\n");
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x10, 0x05);
            isp_reg_value_check(client,0x0E,0xBB);
            //isp output 800*600
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x11, 0x06);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x07, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x08, 0x00);
            isp_reg_value_check(client,0x0E,0xBB);
            }
            break;
        case OUTPUT_720P:
            {
             SENSOR_TR(" SET 720P RES!\n");
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x10, 0x0c);
            isp_reg_value_check(client,0x0E,0xBB);
            
            //isp output 1280*720
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x11, 0x0c);
            isp_reg_value_check(client,0x0E,0xBB);
           
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x07, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x08, 0x00);
            isp_reg_value_check(client,0x0E,0xBB);

            }
            break;
        case OUTPUT_XGA:
            {
             SENSOR_TR(" SET XGA RES!\n");
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x08, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x10, 0x0E);
            isp_reg_value_check(client,0x0E,0xBB);
            
            //isp output 1024*768
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x11, 0x07);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x07, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            }
            break;
        case OUTPUT_SXGA:
            break;
        case OUTPUT_UXGA:
            {
             SENSOR_TR(" SET UXGA RES!\n");
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x08, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);

            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x10, 0x0E);
            isp_reg_value_check(client,0x0E,0xBB);
            
            //isp output 1600*1200
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x11, 0x09);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x07, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            }
            break;
        case OUTPUT_1080P:
            break;
        case OUTPUT_QXGA:
            {
             SENSOR_TR(" SET QXGA RES!\n");
            //sensor output 2592*1944
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x08, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);

            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x10, 0x0E);
            isp_reg_value_check(client,0x0E,0xBB);
            
            //isp output 2592*1944
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x11, 0x0D);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x07, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            }
            break;
        case OUTPUT_QSXGA:
            {
             SENSOR_TR(" SET QSXGA RES!\n");
            //sensor output 2592*1944
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x08, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);

            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x10, 0x0E);
            isp_reg_value_check(client,0x0E,0xBB);
            
            //isp output 2592*1944
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x11, 0x0E);
            isp_reg_value_check(client,0x0E,0xBB);
            
            sendI2cCmd(client, 0x0E, 0x00);
            sendI2cCmd(client, 0x07, 0x01);
            isp_reg_value_check(client,0x0E,0xBB);
            }
            break;
        default:
            SENSOR_TR("%s %s  isp not support this resolution!\n",SENSOR_NAME_STRING(),__FUNCTION__);
            
        }

    //AC freq :50Hz
    sendI2cCmd(client, 0x0E, 0x00);
    sendI2cCmd(client, 0x16, 0x01);
    isp_reg_value_check(client,0x0E,0xBB);
    
    return 0;

}
static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct soc_camera_link *icl = to_soc_camera_link(icd);
    struct isp_dev *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl;
    //define the outputsize , qcif - cif is down-scaling from vga , or jaggies is very serious
    sensor->isp_priv_info.outputSize = /*OUTPUT_QCIF|OUTPUT_HQVGA|OUTPUT_QVGA |OUTPUT_CIF |*/OUTPUT_VGA | OUTPUT_SVGA|OUTPUT_720P|OUTPUT_XGA|OUTPUT_QXGA|OUTPUT_UXGA|OUTPUT_QSXGA;
    sensor->isp_priv_info.curRes = -1;
    #if 1
        //reset , reset pin low ,then high
	if (icl->reset)
		icl->reset(icd->pdev);
    #endif
    isp_init_cmds(client);
    #if 1
    isp_reg_value_check(client,0x01,PRODUCT_VERSION_VALUE_REG1);
    printk("init default size :VGA\n");
    sensor->isp_priv_info.curRes = OUTPUT_VGA;
    sensor_set_isp_output_res(client,OUTPUT_VGA);
    #endif
        /* sensor sensor information for initialization  */
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
	if (qctrl)
    	sensor->isp_priv_info.whiteBalance = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_BRIGHTNESS);
	if (qctrl)
    	sensor->isp_priv_info.brightness = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
	if (qctrl)
    	sensor->isp_priv_info.effect = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EXPOSURE);
	if (qctrl){
        sensor->isp_priv_info.exposure = qctrl->default_value;
        //set expose auto 
        sensor_set_exposure(icd,qctrl,qctrl->default_value);
	    }

	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_SATURATION);
	if (qctrl)
        sensor->isp_priv_info.saturation = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_CONTRAST);
	if (qctrl)
        sensor->isp_priv_info.contrast = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_HFLIP);
	if (qctrl)
        sensor->isp_priv_info.mirror = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_VFLIP);
	if (qctrl)
        sensor->isp_priv_info.flip = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_SCENE);
	if (qctrl){
        sensor->isp_priv_info.scene = qctrl->default_value;
        sensor_set_scene(icd,qctrl,qctrl->default_value);
	    }
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_ZOOM_ABSOLUTE);
	if (qctrl)
        sensor->isp_priv_info.digitalzoom = qctrl->default_value;
    /* ddl@rock-chips.com : if sensor support auto focus and flash, programer must run focus and flash code  */
	//qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_AUTO);
	//if (qctrl)
		sensor->isp_priv_info.auto_focus = SENSOR_AF_CONTINUOUS_OFF;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FACEDETECT);
	if (qctrl)
		sensor->isp_priv_info.face = qctrl->default_value;
    return 0;
}
static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
    return 0;
}
static int sensor_ioctrl(struct soc_camera_device *icd,enum rk29sensor_power_cmd cmd, int on)
{
#if 0
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	int ret = 0;

    SENSOR_DG("%s %s  cmd(%d) on(%d)\n",SENSOR_NAME_STRING(),__FUNCTION__,cmd,on);
	switch (cmd)
	{
		case Sensor_PowerDown:
		{
			if (icl->powerdown) {
				ret = icl->powerdown(icd->pdev, on);
				if (ret == RK29_CAM_IO_SUCCESS) {
					if (on == 0) {
						mdelay(2);
						if (icl->reset)
							icl->reset(icd->pdev);
					}
				} else if (ret == RK29_CAM_EIO_REQUESTFAIL) {
					ret = -ENODEV;
					goto sensor_power_end;
				}
			}
			break;
		}
		case Sensor_Flash:
		{
			struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    		struct sensor *sensor = to_sensor(client);

			if (sensor->sensor_io_request && sensor->sensor_io_request->sensor_ioctrl) {
				sensor->sensor_io_request->sensor_ioctrl(icd->pdev,Cam_Flash, on);
                if(on){
                    //flash off after 2 secs
            		hrtimer_cancel(&(flash_off_timer.timer));
            		hrtimer_start(&(flash_off_timer.timer),ktime_set(0, 800*1000*1000),HRTIMER_MODE_REL);
                    }
			}
            break;
		}
		default:
		{
			SENSOR_TR("%s %s cmd(0x%x) is unknown!",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
			break;
		}
	}
sensor_power_end:
	return ret;
#endif
return 0;
}
#if 0
static enum hrtimer_restart flash_off_func(struct hrtimer *timer){
	struct flash_timer *fps_timer = container_of(timer, struct flash_timer, timer);
    sensor_ioctrl(fps_timer->icd,Sensor_Flash,0);
	SENSOR_DG("%s %s !!!!!!",SENSOR_NAME_STRING(),__FUNCTION__);
    return 0;
    
}
#endif
static int sensor_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct isp_dev *sensor = to_sensor(client);

    mf->width	= icd->user_width;
	mf->height	= icd->user_height;
	mf->code	= sensor->isp_priv_info.fmt.code;
	mf->colorspace	= sensor->isp_priv_info.fmt.colorspace;
	mf->field	= V4L2_FIELD_NONE;
    return 0;
}
static int sensor_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    const struct sensor_datafmt *fmt;
    struct isp_dev *sensor = to_sensor(client);
	//const struct v4l2_queryctrl *qctrl;
	//struct soc_camera_device *icd = client->dev.platform_data;
    int ret=0;
    int set_w,set_h;
    int supported_size = sensor->isp_priv_info.outputSize;
    int res_set = 0;
	fmt = sensor_find_datafmt(mf->code, sensor_colour_fmts,
				   ARRAY_SIZE(sensor_colour_fmts));
	if (!fmt) {
        ret = -EINVAL;
        goto sensor_s_fmt_end;
    }
    set_w = mf->width;
    set_h = mf->height;
	if (((set_w <= 176) && (set_h <= 144)) && (supported_size & OUTPUT_QCIF))
	{
        set_w = 176;
        set_h = 144;
        res_set = OUTPUT_QCIF;
	}
	else if (((set_w <= 320) && (set_h <= 240)) && (supported_size & OUTPUT_QVGA))
    {
        set_w = 320;
        set_h = 240;
        res_set = OUTPUT_QVGA;

    }
    else if (((set_w <= 352) && (set_h<= 288)) && (supported_size & OUTPUT_CIF))
    {
        set_w = 352;
        set_h = 288;
        res_set = OUTPUT_CIF;
        
    }
    else if (((set_w <= 640) && (set_h <= 480)) && (supported_size & OUTPUT_VGA))
    {
        set_w = 640;
        set_h = 480;
        res_set = OUTPUT_VGA;
        
    }
    else if (((set_w <= 800) && (set_h <= 600)) && (supported_size & OUTPUT_SVGA))
    {
        set_w = 800;
        set_h = 600;
        res_set = OUTPUT_SVGA;
        
    }
	else if (((set_w <= 1024) && (set_h <= 768)) && (supported_size & OUTPUT_XGA))
    {
        set_w = 1024;
        set_h = 768;
        res_set = OUTPUT_XGA;
        
    }
	else if (((set_w <= 1280) && (set_h <= 720)) && (supported_size & OUTPUT_720P))
    {
        set_w = 1280;
        set_h = 720;
        res_set = OUTPUT_720P;
    }
    else if (((set_w <= 1280) && (set_h <= 1024)) && (supported_size & OUTPUT_XGA))
    {
        set_w = 1280;
        set_h = 1024;
        res_set = OUTPUT_XGA;
    }
    else if (((set_w <= 1600) && (set_h <= 1200)) && (supported_size & OUTPUT_UXGA))
    {
        set_w = 1600;
        set_h = 1200;
        res_set = OUTPUT_UXGA;
    }
    else if (((set_w <= 1920) && (set_h <= 1080)) && (supported_size & OUTPUT_1080P))
    {
        set_w = 1920;
        set_h = 1080;
        res_set = OUTPUT_1080P;
    }
	else if (((set_w <= 2048) && (set_h <= 1536)) && (supported_size & OUTPUT_QXGA))
    {
        set_w = 2048;
        set_h = 1536;
        res_set = OUTPUT_QXGA;
    }
	else if (((set_w <= 2592) && (set_h <= 1944)) && (supported_size & OUTPUT_QSXGA))
    {
        set_w = 2592;
        set_h = 1944;
        res_set = OUTPUT_QSXGA;
    }
    else
    {
        set_w = SENSOR_INIT_WIDTH;
        set_h = SENSOR_INIT_HEIGHT;
        res_set = OUTPUT_VGA;
    }
    if(res_set != sensor->isp_priv_info.curRes)
        sensor_set_isp_output_res(client,res_set);
    sensor->isp_priv_info.curRes = res_set;
	mf->width = set_w;
    mf->height = set_h;

sensor_s_fmt_end:
    return ret;
}
static int sensor_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct isp_dev *sensor = to_sensor(client);
    const struct sensor_datafmt *fmt;
    int ret = 0,set_w,set_h;
    int supported_size = sensor->isp_priv_info.outputSize;
   
	fmt = sensor_find_datafmt(mf->code, sensor_colour_fmts,
				   ARRAY_SIZE(sensor_colour_fmts));
	if (fmt == NULL) {
		fmt = &sensor->isp_priv_info.fmt;
        mf->code = fmt->code;
	} 
    if (mf->height > SENSOR_MAX_HEIGHT)
        mf->height = SENSOR_MAX_HEIGHT;
    else if (mf->height < SENSOR_MIN_HEIGHT)
        mf->height = SENSOR_MIN_HEIGHT;

    if (mf->width > SENSOR_MAX_WIDTH)
        mf->width = SENSOR_MAX_WIDTH;
    else if (mf->width < SENSOR_MIN_WIDTH)
        mf->width = SENSOR_MIN_WIDTH;

    set_w = mf->width;
    set_h = mf->height;

	if (((set_w <= 176) && (set_h <= 144)) && (supported_size & OUTPUT_QCIF))
	{
        set_w = 176;
        set_h = 144;
	}
	else if (((set_w <= 320) && (set_h <= 240)) && (supported_size & OUTPUT_QVGA))
    {
        set_w = 320;
        set_h = 240;
    }
    else if (((set_w <= 352) && (set_h<= 288)) && (supported_size & OUTPUT_CIF))
    {
        set_w = 352;
        set_h = 288;
    }
    else if (((set_w <= 640) && (set_h <= 480)) && (supported_size & OUTPUT_VGA))
    {
        set_w = 640;
        set_h = 480;
    }
    else if (((set_w <= 800) && (set_h <= 600)) && (supported_size & OUTPUT_SVGA))
    {
        set_w = 800;
        set_h = 600;
    }
	else if (((set_w <= 1024) && (set_h <= 768)) && (supported_size & OUTPUT_XGA))
    {
        set_w = 1024;
        set_h = 768;
    }
	else if (((set_w <= 1280) && (set_h <= 720)) && (supported_size & OUTPUT_720P))
    {
        set_w = 1280;
        set_h = 720;
    }
    else if (((set_w <= 1280) && (set_h <= 1024)) && (supported_size & OUTPUT_XGA))
    {
        set_w = 1280;
        set_h = 1024;
    }
    else if (((set_w <= 1600) && (set_h <= 1200)) && (supported_size & OUTPUT_UXGA))
    {
        set_w = 1600;
        set_h = 1200;
    }
    else if (((set_w <= 1920) && (set_h <= 1080)) && (supported_size & OUTPUT_1080P))
    {
        set_w = 1920;
        set_h = 1080;
    }
	else if (((set_w <= 2048) && (set_h <= 1536)) && (supported_size & OUTPUT_QXGA))
    {
        set_w = 2048;
        set_h = 1536;
    }
	else if (((set_w <= 2592) && (set_h <= 1944)) && (supported_size & OUTPUT_QSXGA))
    {
        set_w = 2592;
        set_h = 1944;
    }
    else
    {
        set_w = SENSOR_INIT_WIDTH;
        set_h = SENSOR_INIT_HEIGHT;
    }


    mf->width = set_w;
    mf->height = set_h;
    mf->colorspace = fmt->colorspace;
    
    return ret;

}

static int sensor_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			    enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(sensor_colour_fmts))
		return -EINVAL;

	*code = sensor_colour_fmts[index].code;
	return 0;

}
static int sensor_s_stream(struct v4l2_subdev *sd, int enable){
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_device *icd = client->dev.platform_data;
    if(enable == 0){
     //   sensor_set_face_detect(client,0);
      //  sensor_af_workqueue_set(icd,0,0,true);
        }else{
        //sensor_set_face_detect(client,1);
     }
    return 0;
}
static int sensor_g_face_area(struct v4l2_subdev *sd, void* face_data){
    struct i2c_client *client = v4l2_get_subdevdata(sd);
     u8 lx_rx = 0,tx_dy = 0;
     isp_i2c_read(client,0x2c,&lx_rx,0);
     isp_i2c_read(client,0x2d,&tx_dy,0);
    // printk("lx_rx = %x, ty_dy = %x\n",lx_rx,tx_dy);
      *(u16 *)face_data = ((u16)lx_rx << 8 ) | tx_dy;

}
static struct v4l2_subdev_core_ops isp_subdev_core_ops = {
	.init		= sensor_init,
	.g_ctrl		= sensor_g_control,
	.s_ctrl		= sensor_s_control,
	.g_ext_ctrls          = sensor_g_ext_controls,
	.s_ext_ctrls          = sensor_s_ext_controls,
//	.g_chip_ident	= sensor_g_chip_ident,
	.ioctl = sensor_ioctl,
};
static struct v4l2_subdev_video_ops isp_subdev_video_ops = {
	.s_mbus_fmt	= sensor_s_fmt,
	.g_mbus_fmt	= sensor_g_fmt,
	.try_mbus_fmt	= sensor_try_fmt,
	.enum_mbus_fmt	= sensor_enum_fmt,
	.s_stream   = sensor_s_stream,
	//.g_face_area = sensor_g_face_area,
};

static struct v4l2_subdev_ops sensor_subdev_ops = {
	.core	= &isp_subdev_core_ops,
	.video = &isp_subdev_video_ops,
};
static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
    return 0;
}
static int sensor_resume(struct soc_camera_device *icd)
{
    return 0;

}
static int sensor_set_bus_param(struct soc_camera_device *icd,
                                unsigned long flags)
{

    return 0;
}

static unsigned long sensor_query_bus_param(struct soc_camera_device *icd)
{
    struct soc_camera_link *icl = to_soc_camera_link(icd);
    unsigned long flags = SENSOR_BUS_PARAM;

    return soc_camera_apply_sensor_flags(icl, flags);
}
static struct soc_camera_ops sensor_ops =
{
    .suspend                     = sensor_suspend,
    .resume                       = sensor_resume,
    .set_bus_param		= sensor_set_bus_param,
    .query_bus_param	= sensor_query_bus_param,
    .controls		= sensor_controls,
    .menus                         = sensor_menus,
    .num_controls		= ARRAY_SIZE(sensor_controls),
    .num_menus		= ARRAY_SIZE(sensor_menus),
};
	

/* sensor register read */
static int isp_i2c_read(struct i2c_client *client, u8 reg, u8 *val,u16 ext_addr)
{
    int err = 0,cnt;
    u8 buf[1];
    struct i2c_msg msg[2];

    buf[0] = reg;

    msg[0].addr = (ext_addr == 0)? client->addr:ext_addr;
    msg[0].flags = client->flags;
    msg[0].buf = buf;
    msg[0].len = sizeof(buf);
    msg[0].scl_rate = CONFIG_SENSOR_I2C_SPEED;       /* ddl@rock-chips.com : 100kHz */
    msg[0].read_type = 0;   /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

    cnt = 50;
    err = -EAGAIN;
    while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, &msg[0], 1);

        if (err >= 0) {
	   break;
        } else {
                //SENSOR_TR("\n %s read reg(0x%x val:0x%x) failed, try to read again! \n",SENSOR_NAME_STRING(),reg, *val);
            udelay(10);
        }
    }
    if(cnt <=0 ){
	SENSOR_TR("write i2c addr erro!\n");
	return err;
	}

    msg[1].addr = (ext_addr == 0)? client->addr:ext_addr;
    msg[1].flags = client->flags|I2C_M_RD;
    msg[1].buf = buf;
    msg[1].len = 1;
    msg[1].scl_rate = CONFIG_SENSOR_I2C_SPEED;                       /* ddl@rock-chips.com : 100kHz */
    msg[1].read_type = 2;                             /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

    cnt = 50;
    err = -EAGAIN;
    while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, &msg[1], 1);

        if (err >= 0) {
            *val = buf[0];
            return 0;
        } else {
        	SENSOR_TR("\n %s read reg(0x%x val:0x%x) failed, try to read again! \n",SENSOR_NAME_STRING(),reg, *val);
            udelay(10);
        }
    }

    return err;
}

u8  i2cWaitIdle(struct i2c_client *client,u8 timeOut) 
{ 
  u8  ret; 
  u8 dat = 0; 
  /* read I2C_IDLE_IDX register until the value I2C_IDLE */ 
  while(timeOut) 
  {   
    ret = isp_i2c_read(client, I2C_STATUS_REG2, &dat,0); 
    if ((ret >= 0) && ((dat&0x01) == I2C_IDLE)) 
    	break; 
    udelay(100); 
    timeOut--; 
  } 
  if(!timeOut){ 
   //SENSOR_TR("wait i2c idle erro!!\n");  
   return -1;
  } 
 return 0; 
} 

static int isp_write_regs(struct i2c_client *client,  u8 *reg_info, u16 num,u16 ext_addr,int seq)
{
	int err=0,cnt;
	struct i2c_msg msg;
    u16 windex = 0;
    
    switch(seq){
    case 0 :
            {
                msg.len = num;	
            	msg.addr = (ext_addr == 0)? client->addr:ext_addr;	
            	msg.flags = client->flags;	
            	msg.buf = reg_info;	
            	msg.scl_rate = CONFIG_SENSOR_I2C_SPEED;         /* ddl@rock-chips.com : 100kHz */	
            	msg.read_type = 0;               /* fpga i2c:0==I2C_NORMAL : direct use number not enum for don't want include spi_fpga.h */	

            	
            	cnt= 10;	
            	err = -EAGAIN;
            	
            	while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */		
            		err = i2c_transfer(client->adapter, &msg, 1);		
            		if (err >= 0) {		            
            			return 0;		
            		} else {		            
            			SENSOR_TR("\n %s write reg failed, try to write again!\n",	SENSOR_NAME_STRING());		            
            			mdelay(5);	
            		}	
            	}
                break;
            }
    case 1 :
            {
                #define TRPERTIME (2)
                int remain = 0;
            	msg.len = TRPERTIME;	
            	msg.addr = (ext_addr == 0)? client->addr:ext_addr;	
            	msg.flags = client->flags;	
            	msg.scl_rate = CONFIG_SENSOR_I2C_SPEED;         /* ddl@rock-chips.com : 100kHz */	
            	msg.read_type = 0;               /* fpga i2c:0==I2C_NORMAL : direct use number not enum for don't want include spi_fpga.h */	
            	
            	err = -EAGAIN;
                remain = num % TRPERTIME;
                while(windex < num){
                	cnt= 10;	
                    msg.buf = reg_info+windex;
                	err = -EAGAIN;
                	while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */		
                		err = i2c_transfer(client->adapter, &msg, 1);		
                		if (err >= 0) {		            
                			break;		
                		} else {		            
                			SENSOR_TR("\n %s write reg failed, try to write again ,index = %d!\n",	SENSOR_NAME_STRING(),windex);		            
                			udelay(10);	
                		}	
                	}
                    if(cnt <= 0){
            			SENSOR_TR("\n %s write reg failed ,index = %d!\n",	SENSOR_NAME_STRING(),windex);
                        break;
                    }
                    windex+= TRPERTIME;
                    if((windex >= num) && (remain != 0)){
                        windex = num - remain;
                        remain = 0;
                        }
                }
                if(err >= 0){
                    err = 0;
                    if(num % TRPERTIME){
                    	SENSOR_TR("\n %s isp_write_regs sucess ,index = %d, val = %x!!\n",SENSOR_NAME_STRING(),(windex  - (TRPERTIME - num % TRPERTIME) -1),*((u8*)(reg_info+(windex  - (TRPERTIME - num % TRPERTIME) -1))));
                        }
                    else{
                    	SENSOR_TR("\n %s isp_write_regs sucess ,index = %d, val = %x!!\n",SENSOR_NAME_STRING(),windex - 1,*((u8*)(reg_info+windex - 1)));
                        }
                    }
                
                break;
            }
    default:
    	SENSOR_TR("\n %s isp_write_regs erro\n",SENSOR_NAME_STRING());
    }
	
	return err;

}

static u8 sendI2cCmd(struct i2c_client *client,u8 cmd, u8 dat) 
{ 
    u8 buf[2];
    buf[0] = cmd & 0xFF;
    buf[1] = dat;
  if (i2cWaitIdle(client,100) < 0){ 
    	SENSOR_TR("%s:%d,send i2c command erro!",__func__,__LINE__);
    	return -1;
	}
  if (isp_write_regs(client,buf,2,0,0)<0) 
      { 
        SENSOR_TR("%s:%d,send i2c command erro!",__func__,__LINE__);
        return -1; 
      } 
 //check the write
 #if WRITE_READ_CHECK
  if(cmd != 0xE){// no need to check if 0x0e
      buf[1] = 0xff;
      isp_i2c_read(client,cmd,&buf[1],0);
      if(buf[1]!=dat){
        SENSOR_TR("%s:%d,send i2c command check erro,reg %x:(%x:%x)\n!",__func__,__LINE__,cmd,buf[1],dat);
      }
    }
 #endif
  return 0; 
} 

static int flash_program_mode(struct i2c_client *client,bool enable){
    u8 ss_array[2] = {0x5b,enable};
    u8 mem_choose[2] = {0x80,0x2}; // 0x2 ,flash ;0x0 sdram
    if(enable == true){
        //select flash mem
        if(isp_write_regs(client,(u8*)mem_choose,sizeof(mem_choose),0,0)){
            SENSOR_TR("%s:%d,enter_program_mode %d erro!",__func__,__LINE__,enable);
            return -1;
        }
        //enter pro mode
        if(isp_write_regs(client,(u8*)ss_array,sizeof(ss_array),0,0)){
            SENSOR_TR("%s:%d,enter_program_mode %d erro!",__func__,__LINE__,enable);
            return -1;
        }
        mdelay(10);
    }else{
        //exit promode
        mem_choose[1] = 0x0; 
        if(isp_write_regs(client,(u8*)ss_array,sizeof(ss_array),0,0)){
            SENSOR_TR("%s:%d,enter_program_mode %d erro!",__func__,__LINE__,enable);
            return -1;
        }
        //select sdram mem
        mdelay(10);
        if(isp_write_regs(client,(u8*)mem_choose,sizeof(mem_choose),0,0)){
            SENSOR_TR("%s:%d,enter_program_mode %d erro!",__func__,__LINE__,enable);
            return -1;
        }
    }
    return 0;
}
static int flash_mem_erase(struct i2c_client *client,u8 sector){
    //default is chip erase.
    u8 ss_array[3] = {0x00,sector,0x02};

    if(sector == 0xFF){
        if(isp_write_regs(client,(u8*)ss_array,sizeof(ss_array),DEV_ID_MV9335_I2CSLAVE_FLASH_PRO,0)){
            SENSOR_TR("%s:%d,chip_erase  erro!",__func__,__LINE__);
            return -1;
        }
        //delay 120ms
        mdelay(120);
    }else{
        ss_array[2] = 0x05;
        if(isp_write_regs(client,(u8*)ss_array,sizeof(ss_array),DEV_ID_MV9335_I2CSLAVE_FLASH_PRO,0)){
            SENSOR_TR("%s:%d,sector_erase  erro\n!",__func__,__LINE__);
            return -1;
        }
        mdelay(120);
    }
    
    return 0;
}
static int flash_write_read_addr(struct i2c_client *client,u16 addr,u8 commd){
    //commd :0x50 write, 0x00 read
    u8 lsb = get_lsb(addr);
    u8 msb = get_msb(addr);
    u8 ss_array[3] = {lsb,msb,commd};
    if(isp_write_regs(client,(u8*)ss_array,sizeof(ss_array),DEV_ID_MV9335_I2CSLAVE_FLASH_PRO,0)){
        SENSOR_TR("%s:%d,flash_write_addr  erro!",__func__,__LINE__);
        return -1;
    }
    return 0;
}
static int flash_write_data(struct i2c_client *client){
    int ret = 0;
	const struct firmware *fw = NULL;
	ret = request_firmware(&fw, fw_name, &client->dev);
    if(ret){
        SENSOR_TR("%s:%d,request_firmware  erro+++++++++++++!\n",__func__,__LINE__);
        ret = -1;
        goto free_fw_buffer;
    }
        SENSOR_TR("%s:%d,request_firmware sucess,val = %x!\n",__func__,__LINE__,*((u8*)fw->data+fw->size-1));
    if(isp_write_regs(client,(u8*)fw->data,fw->size,DEV_ID_MV9335_I2CSLAVE_FLASH_PRO,1)){
        SENSOR_TR("%s:%d,flash_write_data  erro,size = %d!\n",__func__,__LINE__,fw->size);
        ret = -1;
        goto free_fw_buffer;
    }
    #if FLASH_WRITE_CHECK // read all datas back to check
        {
        flash_write_read_addr(client,0x00,0x00);
        u8 rd_data = 0;
        u32 i = 0;
        for(;i<fw->size;i++){
            flash_read_data(client,&rd_data,1);
            if(rd_data!= *((u8*)(fw->data+i)))
                {
                printk("0x%x, ori = 0x%x, rb = 0x%x\n",i,*((u8*)(fw->data+i)),rd_data);
                break;
                }
            rd_data = 0;
            udelay(200);
            }
        }
    #endif

free_fw_buffer:
    if(fw)
    	release_firmware(fw);
    return ret;
}
static int flash_read_data(struct i2c_client *client,u8* rd_data,int readcount){
    int err = 0,cnt,i = 0;
    //u8 buf[2];
    u8 buf;
    struct i2c_msg msg;

    msg.addr = DEV_ID_MV9335_I2CSLAVE_FLASH_PRO;
    msg.flags = client->flags|I2C_M_RD;
    msg.buf = &buf;
    msg.len = 1;
    msg.scl_rate = CONFIG_SENSOR_I2C_SPEED;       /* ddl@rock-chips.com : 100kHz */
    msg.read_type = 2;   /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */
    for(i = 0;i < readcount;i++){
            cnt = 3;
            err = -EAGAIN;
            while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
                err = i2c_transfer(client->adapter, &msg, 1);

                if (err >= 0) {
                    rd_data[i] = buf;
            	   break;
                } else {
                    //SENSOR_TR("\n %s read reg(0x%x val:0x%x) failed, try to read again! \n",SENSOR_NAME_STRING(),reg, *val);
                    udelay(10);
                }
            }
            if(cnt <=0 ){
        	SENSOR_TR("flash read  erro,i = %d ,readcount = %d!\n",i,readcount);
        	return err;
        	}
        }
    return 0;
}
static int isp_load_fw_prepare(struct i2c_client *client){
	//pll start cmd
	sendI2cCmd(client, 0x80, 0x0);
//	msleep(5);
	//flash memory devider
	sendI2cCmd(client, 0xD0, 0x30);	
//	msleep(5);
	//pll 1 Conf.command(24MHZ->192MHZ)
	sendI2cCmd(client, 0x40, 0x01); 
	sendI2cCmd(client, 0x41, 0x40); 
	sendI2cCmd(client, 0x42, 0x05); 
	sendI2cCmd(client, 0x49, 0x03); 
	sendI2cCmd(client, 0x80, 0x02); 
//	sendI2cCmd(client, 0x4A, 0x01); 
    return 0;
}
static int isp_init_cmds(struct i2c_client *client){
	//pll start cmd
	sendI2cCmd(client, 0x80, 0x0);
//	msleep(5);
	//flash memory devider
	sendI2cCmd(client, 0xD0, 0x30);	
//	msleep(5);
	//pll 1 Conf.command(24MHZ->192MHZ)
	sendI2cCmd(client, 0x40, 0x01); 
	sendI2cCmd(client, 0x41, 0x40); 
	sendI2cCmd(client, 0x42, 0x05); 
	sendI2cCmd(client, 0x49, 0x03);
    isp_reg_value_check(client,0x36,0x03);
	sendI2cCmd(client, 0x80, 0x03); 
//	sendI2cCmd(client, 0x4A, 0x01); 
  //  msleep(100);
    return 0;
}
static int isp_init_check(struct i2c_client *client){
    if(!isp_reg_value_check(client,0x00,PRODUCT_ID_VALUE_REG0)){
		SENSOR_TR("%s:%d,check erro!\n", __func__,__LINE__);
		return -1;
    	}
    if(!isp_reg_value_check(client,0x01,PRODUCT_VERSION_VALUE_REG1)){
		SENSOR_TR("%s:%d,check erro!\n", __func__,__LINE__);
		return -1;
	}

    isp_reg_value_check(client,0x02,FIRMWARE_MAJOR_VERSION_VALUE_REG2);
    isp_reg_value_check(client,0x03,FIRMWARE_MINOR_VERSION_VALUE_REG3);
    isp_reg_value_check(client,0x05,I2C_CHECKE_VALUE_REG5);
  //  isp_reg_value_check(client,0x06,CHECKE_VALUE_REG6);
    //now do not check the two reg.
	sendI2cCmd(client, 0x1F, 0x01); 
    isp_reg_value_check(client,0x90,CHECKE_VALUE_REG0X90);
    isp_reg_value_check(client,0x91,CHECKE_VALUE_REG0X91);
    SENSOR_TR("%s:%d,check succsess!\n", __func__,__LINE__);
	//while(1);
    return 0;
}
static int flash_down_load_firmware(struct i2c_client *client,u16 addr){
    struct soc_camera_device *icd = client->dev.platform_data;
    struct soc_camera_link *icl = to_soc_camera_link(icd);
       isp_load_fw_prepare(client);
    // 1.enter pro mode
    if(flash_program_mode(client,true)){
        printk("%s:%d erro \n",__func__,__LINE__);
        goto erro_quit;
        }
    // 2. flash erase , chip erase or sector erase
    #if FLASH_ERASE_BY_SECTOR
    //for test
    {
    int sect,sectCnt,i;

    sect = 0;
    sectCnt = 15;
    for(i = 0;i < sectCnt;i++)
        {
        sect = (i << 4) & 0xF0;
        if(flash_mem_erase(client,sect/*+i*/))
            {
            printk("%s:%d erro \n",__func__,__LINE__);
            goto erro_quit_promode;  
            }
        }
    }
    #elif FLASH_ERASE_ALL
    if(flash_mem_erase(client,0xFF))
        {
        printk("%s:%d erro \n",__func__,__LINE__);
        goto erro_quit_promode;
        }
    #endif

    // 3. write addr
    if(flash_write_read_addr(client,addr,0x50)){
        printk("%s:%d erro \n",__func__,__LINE__);
        goto erro_quit_promode;
       }

    // 4. write data ,sequence write
    if(flash_write_data(client)){
        printk("%s:%d erro \n",__func__,__LINE__);
        goto erro_quit_promode;
        }
    // 5. quit pro mode
    if(flash_program_mode(client,false)){
        printk("%s:%d erro \n",__func__,__LINE__);
        goto erro_quit;
        }
    //reset , reset pin low ,then high
	if (icl->reset)
		icl->reset(icd->pdev);
        mdelay(10);
    isp_init_cmds(client);
    isp_init_check(client);
    return 0;
    erro_quit_promode:
        flash_program_mode(client,false);
    erro_quit:
        printk("%s:%d erro \n",__func__,__LINE__);
        return -1;
}
static int flash_read_firmware_data(struct i2c_client *client,u16 addr,u8* rd_data,int readcount){
    // 1.enter pro mode
    if(flash_program_mode(client,true)){
        printk("%s:%d erro \n",__func__,__LINE__);
        goto erro_quit;
        }
    // 2. write addr
    if(flash_write_read_addr(client,addr,0x00)){
        printk("%s:%d erro \n",__func__,__LINE__);
        goto erro_quit_promode;
        }
    // 3. read data
    if(flash_read_data(client,rd_data,readcount)){
        printk("%s:%d erro \n",__func__,__LINE__);
        goto erro_quit_promode;
        }
    // 4. quit pro mode
    if(flash_program_mode(client,false)){
        printk("%s:%d erro \n",__func__,__LINE__);
        goto erro_quit;
        }
    return 0;
    erro_quit_promode:
        flash_program_mode(client,false);
    erro_quit:
        printk("%s:%d erro \n",__func__,__LINE__);
        return -1;
}
//power control
//power_up sequence : reset low,no mclk; power up ; supply mclk ; reset high
//power_off sequence: reset low ,mclk off , power off
//power down sequence :as the power up ,then cut off mclk;
//wake up from power down: supply mclk ;reset asserted twice(low - high -low -high)
//soc camera has done the power control
/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int sensor_video_probe(struct soc_camera_device *icd,
			       struct i2c_client *client)
{

    /* We must have a parent by now. And it cannot be a wrong one.
     * So this entire test is completely redundant. */
    if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;
	//get the pid of sensor
	//pll start cmd

	//ISP initialization check
#if 1
    flash_down_load_firmware(client,0x00);
    isp_init_cmds(client);
    if(!isp_init_check(client)){
		SENSOR_TR("%s:%d,check erro!\n", __func__,__LINE__);
		return -1;
 	}

#endif
    return 0;

//sensor_video_probe_err:
 //   return ret;
}
static int sensor_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	    struct isp_dev *sensor;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
    struct soc_camera_link *icl;
    int ret;

    SENSOR_DG("\n%s..%s..%d..\n",__FUNCTION__,__FILE__,__LINE__);
    if (!icd) {
        dev_err(&client->dev, "%s: missing soc-camera data!\n",SENSOR_NAME_STRING());
        return -EINVAL;
    }

    icl = to_soc_camera_link(icd);
    if (!icl) {
        dev_err(&client->dev, "%s driver needs platform data\n", SENSOR_NAME_STRING());
        return -EINVAL;
    }

    if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
        dev_warn(&adapter->dev,
        	 "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
        return -EIO;
    }

    sensor = kzalloc(sizeof(struct isp_dev), GFP_KERNEL);
    if (!sensor)
        return -ENOMEM;

    v4l2_i2c_subdev_init(&sensor->subdev, client, &sensor_subdev_ops);

    /* Second stage probe - when a capture adapter is there */
    icd->ops		= &sensor_ops;
    sensor->isp_priv_info.fmt = sensor_colour_fmts[0];


    ret = sensor_video_probe(icd, client);
    if (ret < 0) {
        icd->ops = NULL;
        i2c_set_clientdata(client, NULL);
        kfree(sensor);
		sensor = NULL;
    } else {
		#if CONFIG_SENSOR_Focus
		sensor->sensor_wq = create_workqueue(SENSOR_NAME_STRING(_af_workqueue));
		if (sensor->sensor_wq == NULL)
			SENSOR_TR("%s create fail!", SENSOR_NAME_STRING(_af_workqueue));
    		mutex_init(&sensor->wq_lock);
		#endif
		mutex_init(&sensor->isp_priv_info.access_data_lock);

    }

    SENSOR_DG("\n%s..%s..%d  ret = %x \n",__FUNCTION__,__FILE__,__LINE__,ret);
    return ret;
}
static int sensor_remove(struct i2c_client *client)
{
    struct isp_dev *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;

    icd->ops = NULL;
    i2c_set_clientdata(client, NULL);
    client->driver = NULL;
    kfree(sensor);
	sensor = NULL;

    return 0;
}
static const struct i2c_device_id sensor_id[] = {
	{SENSOR_NAME_STRING(), 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_i2c_driver = {
	.driver = {
		.name = SENSOR_NAME_STRING(),
	},
	.probe		= sensor_probe,
	.remove		= sensor_remove,
	.id_table	= sensor_id,
};

static int __init sensor_mod_init(void)
{
    SENSOR_DG("\n%s..%s.. \n",__FUNCTION__,SENSOR_NAME_STRING());
    return i2c_add_driver(&sensor_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
    i2c_del_driver(&sensor_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION(SENSOR_NAME_STRING(Camera sensor driver));
MODULE_AUTHOR("ddl <kernel@rock-chips>");
MODULE_LICENSE("GPL");
