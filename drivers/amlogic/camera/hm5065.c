/*
 *HM5065 - This code emulates a real video device with v4l2 api
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
#include <linux/sizes.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>
#include <media/videobuf-res.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <linux/wakelock.h>

#include <linux/i2c.h>
#include <media/v4l2-chip-ident.h>
#include <linux/amlogic/camera/aml_cam_info.h>
#include <linux/amlogic/vmapi.h>

#include <mach/am_regs.h>
//#include <mach/am_eth_pinmux.h>
#include <mach/pinmux.h>
#include <mach/gpio.h>

#include "common/plat_ctrl.h"
#include "common/vm.h"
#include "hm5065_firmware.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif
#define HM5065_CAMERA_MODULE_NAME "hm5065"
#define MAGIC_RE_MEM 0x123039dc
#define HM5065_RES0_CANVAS_INDEX CAMERA_USER_CANVAS_INDEX

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */
#define AF_STATUS     	0x07AE
#define FACE_LC			0x0714
#define FACE_START_XH 	0x0715
#define FACE_START_XL 	0x0716
#define FACE_SIZE_XH  	0x0717
#define FACE_SIZE_XL 	0x0718
#define FACE_START_YH 	0x0719
#define FACE_START_YL 	0x071A
#define FACE_SIZE_YH 	0x071B
#define FACE_SIZE_YL 	0x071C

#define HM5065_CAMERA_MAJOR_VERSION 0
#define HM5065_CAMERA_MINOR_VERSION 7
#define HM5065_CAMERA_RELEASE 0
#define HM5065_CAMERA_VERSION \
	KERNEL_VERSION(HM5065_CAMERA_MAJOR_VERSION, HM5065_CAMERA_MINOR_VERSION, HM5065_CAMERA_RELEASE)

MODULE_DESCRIPTION("hm5065 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define HM5065_DRIVER_VERSION "HM5065-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 32;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");


//extern int disable_hm5065;
static int hm5065_have_opened = 0;
static unsigned char focus_position_hi, focus_position_lo;
static struct vdin_v4l2_ops_s *vops;

static bool bDoingAutoFocusMode=false;

static struct v4l2_fract hm5065_frmintervals_active = {
	.numerator = 1,
	.denominator = 15,
};

static struct v4l2_frmivalenum hm5065_frmivalenum[]={
	{
		.index = 0,
		.pixel_format = V4L2_PIX_FMT_NV21,
		.width = 640,
		.height = 480,
		.type = V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete ={
				.numerator = 1,
				.denominator = 30,
			}
		}
	},{
		.index = 0,
		.pixel_format = V4L2_PIX_FMT_NV21,
		.width = 1024,
		.height = 768,
		.type = V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete ={
				.numerator = 1,
				.denominator = 30,
			}
		}
	},{
		.index = 0,
		.pixel_format = V4L2_PIX_FMT_NV21,
		.width = 1280,
		.height = 720,
		.type = V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete ={
				.numerator = 1,
				.denominator = 30,
			}
		}
	},{
		.index = 0,
		.pixel_format = V4L2_PIX_FMT_NV21,
		.width = 1920,
		.height = 1080,
		.type = V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete ={
				.numerator = 1,
				.denominator = 30,
			}
		}
	},{
		.index = 1,
		.pixel_format = V4L2_PIX_FMT_NV21,
		.width = 1600,
		.height = 1200,
		.type = V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete ={
				.numerator = 1,
				.denominator = 5,
			}
		}
	},{
		.index = 1,
		.pixel_format = V4L2_PIX_FMT_NV21,
		.width = 2048,
		.height = 1536,
		.type = V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete ={
				.numerator = 1,
				.denominator = 5,
			}
		}
	},{
		.index = 1,
		.pixel_format = V4L2_PIX_FMT_NV21,
		.width = 2592,
		.height = 1944,
		.type = V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete ={
				.numerator = 1,
				.denominator = 5,
			}
		}
	},
};

/* supported controls */
static struct v4l2_queryctrl hm5065_qctrl[] = {
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 127,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	}, {
		.id            = V4L2_CID_CONTRAST,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Contrast",
		.minimum       = 0x10,
		.maximum       = 0x60,
		.step          = 0xa,
		.default_value = 0x30,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id            = V4L2_CID_HFLIP,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "flip on horizontal",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 0x1,
		.default_value = 0,
		.flags         = V4L2_CTRL_FLAG_DISABLED,
	} ,{
		.id            = V4L2_CID_VFLIP,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "flip on vertical",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 0x1,
		.default_value = 0,
		.flags         = V4L2_CTRL_FLAG_DISABLED,
	},{
		.id            = V4L2_CID_DO_WHITE_BALANCE,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "white balance",
		.minimum       = 0,
		.maximum       = 6,
		.step          = 0x1,
		.default_value = 0,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id            = V4L2_CID_EXPOSURE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "exposure",
		.minimum       = 0,
		.maximum       = 8,
		.step          = 0x1,
		.default_value = 4,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id            = V4L2_CID_COLORFX,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "effect",
		.minimum       = 0,
		.maximum       = 6,
		.step          = 0x1,
		.default_value = 0,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id            = V4L2_CID_WHITENESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "banding",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 0x1,
		.default_value = 0,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id            = V4L2_CID_FOCUS_AUTO,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "auto focus",
		.minimum       = CAM_FOCUS_MODE_RELEASE,
		.maximum       = CAM_FOCUS_MODE_CONTI_PIC,
		.step          = 0x1,
		.default_value = CAM_FOCUS_MODE_CONTI_PIC,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id            = V4L2_CID_BACKLIGHT_COMPENSATION,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "flash",
		.minimum       = FLASHLIGHT_ON,
		.maximum       = FLASHLIGHT_TORCH,
		.step          = 0x1,
		.default_value = FLASHLIGHT_OFF,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id            = V4L2_CID_ZOOM_ABSOLUTE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Zoom, Absolute",
		.minimum       = 100,
		.maximum       = 300,
		.step          = 20,
		.default_value = 100,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id		= V4L2_CID_ROTATE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Rotate",
		.minimum	= 0,
		.maximum	= 270,
		.step		= 90,
		.default_value	= 0,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id            = V4L2_CID_AUTO_FOCUS_STATUS,
		.type          = 8,//V4L2_CTRL_TYPE_BITMASK,
		.name          = "focus status",
		.minimum       = 0,
		.maximum       = ~3,
		.step          = 0x1,
		.default_value = V4L2_AUTO_FOCUS_STATUS_IDLE,
		.flags         = V4L2_CTRL_FLAG_READ_ONLY,
	},{
		.id		= V4L2_CID_FOCUS_ABSOLUTE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "focus center",
		.minimum	= 0,
		.maximum	= ((2000) << 16) | 2000,
		.step		= 1,
		.default_value	= (1000 << 16) | 1000,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
 	}
};

struct v4l2_querymenu hm5065_qmenu_autofocus[] = {
	{
		.id         = V4L2_CID_FOCUS_AUTO,
		.index      = CAM_FOCUS_MODE_INFINITY,
		.name       = "infinity",
		.reserved   = 0,
	},{
		.id         = V4L2_CID_FOCUS_AUTO,
		.index      = CAM_FOCUS_MODE_AUTO,
		.name       = "auto",
		.reserved   = 0,
	},{
		.id         = V4L2_CID_FOCUS_AUTO,
		.index      = CAM_FOCUS_MODE_CONTI_VID,
		.name       = "continuous-video",
		.reserved   = 0,
	},{
		.id         = V4L2_CID_FOCUS_AUTO,
		.index      = CAM_FOCUS_MODE_CONTI_PIC,
		.name       = "continuous-picture",
		.reserved   = 0,
	}
};

struct v4l2_querymenu hm5065_qmenu_flashmode[] = {
	{
		.id         = V4L2_CID_BACKLIGHT_COMPENSATION,
		.index      = FLASHLIGHT_ON,
		.name       = "on",
		.reserved   = 0,
	},{
		.id         = V4L2_CID_BACKLIGHT_COMPENSATION,
		.index      = FLASHLIGHT_OFF,
		.name       = "off",
		.reserved   = 0,
	},{
		.id         = V4L2_CID_BACKLIGHT_COMPENSATION,
		.index      = FLASHLIGHT_TORCH,
		.name       = "torch",
		.reserved   = 0,
	}
};

struct v4l2_querymenu hm5065_qmenu_wbmode[] = {
    {
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_AUTO,
        .name       = "auto",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_CLOUD,
        .name       = "cloudy-daylight",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_INCANDESCENCE,
        .name       = "incandescent",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_DAYLIGHT,
        .name       = "daylight",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_FLUORESCENT,
        .name       = "fluorescent", 
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_FLUORESCENT,
        .name       = "warm-fluorescent", 
        .reserved   = 0,
    },
};

typedef struct {
	__u32   id;
	int     num;
	struct v4l2_querymenu* hm5065_qmenu;
}hm5065_qmenu_set_t;

hm5065_qmenu_set_t hm5065_qmenu_set[] = {
	{
		.id             = V4L2_CID_FOCUS_AUTO,
		.num            = ARRAY_SIZE(hm5065_qmenu_autofocus),
		.hm5065_qmenu   = hm5065_qmenu_autofocus,
	}, {
		.id             = V4L2_CID_BACKLIGHT_COMPENSATION,
		.num            = ARRAY_SIZE(hm5065_qmenu_flashmode),
		.hm5065_qmenu   = hm5065_qmenu_flashmode,
	},{
        .id         	= V4L2_CID_DO_WHITE_BALANCE,
        .num            = ARRAY_SIZE(hm5065_qmenu_wbmode),
        .hm5065_qmenu   = hm5065_qmenu_wbmode,
    }
};

#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct hm5065_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct hm5065_fmt formats[] = {
	{
		.name     = "RGB565 (BE)",
		.fourcc   = V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
		.depth    = 16,
	}, {
		.name     = "RGB888 (24)",
		.fourcc   = V4L2_PIX_FMT_RGB24, /* 24  RGB-8-8-8 */
		.depth    = 24,
	}, {
		.name     = "BGR888 (24)",
		.fourcc   = V4L2_PIX_FMT_BGR24, /* 24  BGR-8-8-8 */
		.depth    = 24,
	}, {
		.name     = "12  Y/CbCr 4:2:0SP",
		.fourcc   = V4L2_PIX_FMT_NV12,
		.depth    = 12,    
	}, {
		.name     = "12  Y/CbCr 4:2:0SP",
		.fourcc   = V4L2_PIX_FMT_NV21,
		.depth    = 12,    
	}, {
		.name     = "YUV420P",
		.fourcc   = V4L2_PIX_FMT_YUV420,
		.depth    = 12,
	},{
		.name     = "YVU420P",
		.fourcc   = V4L2_PIX_FMT_YVU420,
		.depth    = 12,
	}
};

static struct hm5065_fmt *get_format(struct v4l2_format *f)
{
	struct hm5065_fmt *fmt;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(formats); k++) {
		fmt = &formats[k];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}

	if (k == ARRAY_SIZE(formats))
    	return NULL;

	return &formats[k];
}

struct sg_to_addr {
	int pos;
	struct scatterlist *sg;
};

/* buffer for one video frame */
struct hm5065_buffer {
    /* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct hm5065_fmt        *fmt;

	unsigned int canvas_id;
};

struct hm5065_dmaqueue {
	struct list_head       active;

    /* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
    /* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

typedef struct resolution_param {
	struct v4l2_frmsize_discrete frmsize;
	struct v4l2_frmsize_discrete active_frmsize;
	int active_fps;
	resolution_size_t size_type;
	struct aml_camera_i2c_fig_s* reg_script;
} resolution_param_t;

static LIST_HEAD(hm5065_devicelist);

struct hm5065_device {
	struct list_head	    	hm5065_devicelist;
	struct v4l2_subdev	    	sd;
	struct v4l2_device	    	v4l2_dev;

	spinlock_t                 slock;
	struct mutex	        	mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct hm5065_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int	           input;

	/* platform device data from board initting. */
	aml_cam_info_t  cam_info;
    
	/* Control 'registers' */
	int                qctl_regs[ARRAY_SIZE(hm5065_qctrl)];
	
	/* current resolution param for preview and capture */
	resolution_param_t* cur_resolution_param;
	
	/* wake lock */
	struct wake_lock	wake_lock;
	
	/* for down load firmware */
	struct work_struct dl_work;
	
	int firmware_ready;
};

static DEFINE_MUTEX(firmware_mutex);

static inline struct hm5065_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct hm5065_device, sd);
}

struct hm5065_fh {
	struct hm5065_device            *dev;

	/* video capture */
	struct hm5065_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	struct videobuf_res_privdata res;

	enum v4l2_buf_type         type;
	int	           input;     /* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};

/*static inline struct hm5065_fh *to_fh(struct hm5065_device *dev)
{
	return container_of(dev, struct hm5065_fh, dev);
}*/

/* ------------------------------------------------------------------
	reg spec of HM5065
   ------------------------------------------------------------------*/
static struct aml_camera_i2c_fig_s HM5065_script[] = {
		{0x0083,0x00},   //	; HFlip disable
		{0x0084,0x00},   //	; VFlip	disable
		{0x0040,0x01},//	binning mode and subsampling mode for frame rate   
		{0x0041,0x04},//	04 : VGA mode : 0A : self define , 00 : 5M ,03:SVGA
		{0xffff,0xff},
};

#define FPS_15_HZ_960P
static struct aml_camera_i2c_fig_s HM5065_preview_960P_script[] ={
    
	  {0x0010,0x02},
	  {0x00b2,0x50},//713M P：89M
	  {0x00b3,0xca},
	  {0x00b4,0x01},
	  {0x00b5,0x01},
	  {0x0030,0x12},
#if 1
	//固定30帧
	  {0x00E8,0x00},
	  {0x00C8,0x00},
	  {0x00C9,0x1E},
	  {0x00CA,0x01},
#else
	//最低10帧-30帧
	  {0x00E8,0x01},
	  {0x00ED,0x0a},
	  {0x00EE,0x1E},
#endif
		{0x0040,0x01},
		{0x0041,0x0A},
		{0x0042,0x05},//1280
		{0x0043,0x00},
		{0x0044,0x03}, //960
		{0x0045,0xC0},
        {0x0010,0x01},
		{0xffff,0xff},
} ;

static struct aml_camera_i2c_fig_s HM5065_preview_QVGA_script[] ={
    {0xffff, 0xff}
} ;

static struct aml_camera_i2c_fig_s HM5065_capture_5M_script[] ={
		{0x0030,0x11},
		{0x0040,0x00},//Full size                    
		{0x0041,0x0A},//00:full size                 
		{0x0042,0x0A},//X:2048                       
		{0x0043,0x20},                               
		{0x0044,0x07},//Y:1536                       
		{0x0045,0x98},   	
		{0xffff,0xff}
} ;

static struct aml_camera_i2c_fig_s HM5065_capture_3M_script[] = {
 		{0x0030,0x11},
		{0x0040,0x00},
		{0x0041,0x0a},
		{0x0042,0x08},
		{0x0043,0x00},
		{0x0044,0x06},
		{0x0045,0x00},
		{0x0010,0x01},
	  	{0xffff, 0xff}
};

static struct aml_camera_i2c_fig_s HM5065_capture_2M_script[] ={
    	{0x0030,0x11},
		{0x0040,0x00},
		{0x0041,0x01},
	  	{0xffff,0xff}
} ;

static resolution_param_t  prev_resolution_array[] = {
    {
		.frmsize			= {1280, 960},
		.active_frmsize			= {1280, 958},
		.active_fps			= 30,
		.size_type			= SIZE_1280X960,
		.reg_script			= HM5065_preview_960P_script,
	},{
		.frmsize			= {1280, 720},
		.active_frmsize			= {1280, 718},
		.active_fps			= 30,
		.size_type			= SIZE_1280X720,
		.reg_script			= HM5065_preview_960P_script,
	},{
		.frmsize			= {1024, 768},
		.active_frmsize			= {1280, 958},
		.active_fps			= 30,
		.size_type			= SIZE_1024X768,
		.reg_script			= HM5065_preview_960P_script,
	},{
		.frmsize			= {640, 480},
		.active_frmsize			= {640, 478},
		.active_fps			= 30,
		.size_type			= SIZE_640X480,
		.reg_script			= HM5065_script,
	},{
		.frmsize			= {320, 240},
		.active_frmsize			= {320, 240},
		.active_fps			= 30,
		.size_type			= SIZE_320X240,
		.reg_script			= HM5065_preview_QVGA_script,
	},{
		.frmsize			= {352, 288},
		.active_frmsize			= {320, 240},
		.active_fps			= 30,
		.size_type			= SIZE_320X240,
		.reg_script			= HM5065_preview_QVGA_script,
	},
};

static resolution_param_t  capture_resolution_array[] = {
	{
		.frmsize			= {2592, 1944},
		.active_frmsize			= {2592, 1942},
		.active_fps			= 5,
		.size_type			= SIZE_2592X1944,
		.reg_script			= HM5065_capture_5M_script,
	}, 
	{
		.frmsize			= {1600, 1200},
		.active_frmsize			= {1600, 1198},
		.active_fps			= 5,
		.size_type			= SIZE_1600X1200,
		.reg_script			= HM5065_capture_2M_script,
	},{
		.frmsize			= {2048, 1536},
		.active_frmsize			= {2032, 1534},
		.active_fps			= 5,
		.size_type			= SIZE_2048X1536,
		.reg_script			= HM5065_capture_3M_script,
	},
};
static camera_focus_mode_t start_focus_mode = CAM_FOCUS_MODE_RELEASE;
static int HM5065_AutoFocus(struct hm5065_device *dev, int focus_mode);
void HM5065_init_regs(struct hm5065_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int i=0;

	while (1) {
		if (HM5065_script_step[i].val==0xff&&HM5065_script_step[i].addr==0xffff) {
		//if (HM5065_preview_960P_script[i].val==0xff&&HM5065_preview_960P_script[i].addr==0xffff) {
			printk("success in initial HM5065.\n");
			break;
		}
		if ((i2c_put_byte(client,HM5065_script_step[i].addr, HM5065_script_step[i].val)) < 0) {
			printk("fail in initial HM5065. \n");
			return;
		}
		i++;
	}
	msleep(300);
	return;
}

//static unsigned long hm5065_preview_exposure;
//static unsigned long hm5065_preview_extra_lines;
//static unsigned long hm5065_gain;
//static unsigned long hm5065_preview_maxlines;
/*************************************************************************
* FUNCTION
*    HM5065_set_param_wb
*
* DESCRIPTION
*    wb setting.
*
* PARAMETERS
*    none
*
* RETURNS
*    None
*
* GLOBALS AFFECTED
*
*************************************************************************/
void HM5065_set_param_wb(struct hm5065_device *dev,enum  camera_wb_flip_e para)//白平衡
	{
		struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
		switch (para) { 	 
			case CAM_WB_AUTO://自动
			   // i2c_put_byte(client, 0x0085, 0x02);	
				i2c_put_byte(client, 0x01a0, 0x01); 		   
				break;
	
			case CAM_WB_CLOUD: //阴天
				i2c_put_byte(client, 0x01a0, 0x03);
				i2c_put_byte(client, 0x01a1, 0x62);
				i2c_put_byte(client, 0x01a2, 0x08);
				i2c_put_byte(client, 0x01a3, 0x00); 		
				break;
	
			case CAM_WB_DAYLIGHT: //
				//i2c_put_byte(client, 0x0085, 0x03); 
				i2c_put_byte(client, 0x01a0, 0x03);
				i2c_put_byte(client, 0x01a1, 0x7F);
				i2c_put_byte(client, 0x01a2, 0x3F);
				i2c_put_byte(client, 0x01a3, 0x01);
				break;
	
			case CAM_WB_INCANDESCENCE: 
				i2c_put_byte(client, 0x01a0, 0x03);
				i2c_put_byte(client, 0x01a1, 0x39);
				i2c_put_byte(client, 0x01a2, 0x00);
				i2c_put_byte(client, 0x01a3, 0x59);
				break;
				
			case CAM_WB_TUNGSTEN: 
				i2c_put_byte(client, 0x01a0, 0x03);
				i2c_put_byte(client, 0x01a1, 0x05);
				i2c_put_byte(client, 0x01a2, 0x00);
				i2c_put_byte(client, 0x01a3, 0x7f);
				break;
	
			case CAM_WB_FLUORESCENT:
				i2c_put_byte(client, 0x01a0, 0x03);
				i2c_put_byte(client, 0x01a1, 0x1F);
				i2c_put_byte(client, 0x01a2, 0x00);
				i2c_put_byte(client, 0x01a3, 0x4D);
				break;
	
			case CAM_WB_MANUAL:
			default:
					// TODO
				break;
		}
		
	
	}

 /* HM5065_set_param_wb */
/*************************************************************************
* FUNCTION
*    HM5065_set_param_exposure
*
* DESCRIPTION
*    exposure setting.
*
* PARAMETERS
*    none
*
* RETURNS
*    None
*
* GLOBALS AFFECTED
*
*************************************************************************/
void HM5065_set_param_exposure(struct hm5065_device *dev,enum camera_exposure_e para)//曝光调节
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    switch (para) {
    	case EXPOSURE_N4_STEP:  //负4档  
            i2c_put_byte(client, 0x0130, 0xfc);       	
        	break;
            
    	case EXPOSURE_N3_STEP:
            i2c_put_byte(client, 0x0130, 0xfd); 
        	break;
            
    	case EXPOSURE_N2_STEP:
            i2c_put_byte(client, 0x0130, 0xfe); 
        	break;
            
    	case EXPOSURE_N1_STEP:
            i2c_put_byte(client, 0x0130, 0xff); 
        	break;
            
    	case EXPOSURE_0_STEP://默认零档
            i2c_put_byte(client, 0x0130, 0x00); 
        	break;
            
    	case EXPOSURE_P1_STEP://正一档
            i2c_put_byte(client, 0x0130, 0x01); 
        	break;
            
    	case EXPOSURE_P2_STEP:
            i2c_put_byte(client, 0x0130, 0x02); 
        	break;
            
    	case EXPOSURE_P3_STEP:
            i2c_put_byte(client, 0x0130, 0x03); 
        	break;
            
    	case EXPOSURE_P4_STEP:    
            i2c_put_byte(client, 0x0130, 0x04); 
        	break;
            
    	default:
            i2c_put_byte(client, 0x0130, 0x00); 
        	break;
    }
} /* HM5065_set_param_exposure */
/*************************************************************************
* FUNCTION
*    HM5065_set_param_effect
*
* DESCRIPTION
*    effect setting.
*
* PARAMETERS
*    none
*
* RETURNS
*    None
*
* GLOBALS AFFECTED
*
*************************************************************************/
void HM5065_set_param_effect(struct hm5065_device *dev,enum camera_effect_flip_e para)//特效设置
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
  
    switch (para) {
    	case CAM_EFFECT_ENC_NORMAL://正常
      i2c_put_byte(client, 0x0380, 0x00);
			i2c_put_byte(client, 0x0381, 0x00);
			i2c_put_byte(client, 0x0382, 0x00);
			i2c_put_byte(client, 0x0384, 0x00);
			i2c_put_byte(client, 0x01a0, 0x01);
			i2c_put_byte(client, 0x01a1, 0x80);
			i2c_put_byte(client, 0x01a2, 0x80);
			i2c_put_byte(client, 0x01a3, 0x80);
			i2c_put_byte(client, 0x01a5, 0x3e);
			i2c_put_byte(client, 0x01a6, 0x00);
			i2c_put_byte(client, 0x01a7, 0x3e);
			i2c_put_byte(client, 0x01a8, 0x00);
        	break;        

    	case CAM_EFFECT_ENC_GRAYSCALE://灰阶
      i2c_put_byte(client, 0x0380, 0x00);
			i2c_put_byte(client, 0x0381, 0x00);
			i2c_put_byte(client, 0x0382, 0x00);
			i2c_put_byte(client, 0x0384, 0x05);
			i2c_put_byte(client, 0x01a0, 0x01);
			i2c_put_byte(client, 0x01a1, 0x80);
			i2c_put_byte(client, 0x01a2, 0x80);
			i2c_put_byte(client, 0x01a3, 0x80);
			i2c_put_byte(client, 0x01a5, 0x3e);
			i2c_put_byte(client, 0x01a6, 0x00);
			i2c_put_byte(client, 0x01a7, 0x3e);
			i2c_put_byte(client, 0x01a8, 0x00);
        	break;

    	case CAM_EFFECT_ENC_SEPIA://复古
      i2c_put_byte(client, 0x0380, 0x00);
			i2c_put_byte(client, 0x0381, 0x00);
			i2c_put_byte(client, 0x0382, 0x00);
			i2c_put_byte(client, 0x0384, 0x06);
			i2c_put_byte(client, 0x01a0, 0x01);
			i2c_put_byte(client, 0x01a1, 0x80);
			i2c_put_byte(client, 0x01a2, 0x80);
			i2c_put_byte(client, 0x01a3, 0x80);
			i2c_put_byte(client, 0x01a5, 0x3e);
			i2c_put_byte(client, 0x01a6, 0x00);
			i2c_put_byte(client, 0x01a7, 0x3e);
			i2c_put_byte(client, 0x01a8, 0x00);
        	break;        
                
    	case CAM_EFFECT_ENC_SEPIAGREEN://复古绿
      i2c_put_byte(client, 0x0380, 0x00);
			i2c_put_byte(client, 0x0381, 0x00);
			i2c_put_byte(client, 0x0382, 0x00);
			i2c_put_byte(client, 0x0384, 0x07);
			i2c_put_byte(client, 0x01a0, 0x01);
			i2c_put_byte(client, 0x01a1, 0x80);
			i2c_put_byte(client, 0x01a2, 0x80);
			i2c_put_byte(client, 0x01a3, 0x80);
			i2c_put_byte(client, 0x01a5, 0x3e);
			i2c_put_byte(client, 0x01a6, 0x00);
			i2c_put_byte(client, 0x01a7, 0x3e);
			i2c_put_byte(client, 0x01a8, 0x00);
        	break;                    

    	case CAM_EFFECT_ENC_SEPIABLUE://复古蓝
      i2c_put_byte(client, 0x0380, 0x00);
			i2c_put_byte(client, 0x0381, 0x00);
			i2c_put_byte(client, 0x0382, 0x00);
			i2c_put_byte(client, 0x0384, 0x08);
			i2c_put_byte(client, 0x01a0, 0x01);
			i2c_put_byte(client, 0x01a1, 0x80);
			i2c_put_byte(client, 0x01a2, 0x80);
			i2c_put_byte(client, 0x01a3, 0x80);
			i2c_put_byte(client, 0x01a5, 0x3e);
			i2c_put_byte(client, 0x01a6, 0x00);
			i2c_put_byte(client, 0x01a7, 0x3e);
			i2c_put_byte(client, 0x01a8, 0x00);
        	break;                                

    	case CAM_EFFECT_ENC_COLORINV://底片
      i2c_put_byte(client, 0x0380, 0x01);
			i2c_put_byte(client, 0x0381, 0x00);
			i2c_put_byte(client, 0x0382, 0x00);
			i2c_put_byte(client, 0x0384, 0x00);
			i2c_put_byte(client, 0x01a0, 0x01);
			i2c_put_byte(client, 0x01a1, 0x80);
			i2c_put_byte(client, 0x01a2, 0x80);
			i2c_put_byte(client, 0x01a3, 0x80);
			i2c_put_byte(client, 0x01a5, 0x3e);
			i2c_put_byte(client, 0x01a6, 0x00);
			i2c_put_byte(client, 0x01a7, 0x3e);
			i2c_put_byte(client, 0x01a8, 0x00);
        	break;        

    	default:
        	break;
    }
} /* HM5065_set_param_effect */

/*************************************************************************
* FUNCTION
*	HM5065_night_mode
*
* DESCRIPTION
*    This function night mode of HM5065.
*
* PARAMETERS
*    none
*
* RETURNS
*    None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static void HM5065_set_param_banding(struct hm5065_device *dev,enum  camera_banding_flip_e banding)
{
		struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
		//unsigned char buf[4];
		switch(banding){
			case CAM_BANDING_60HZ:
							printk("set banding 60Hz\n");
							i2c_put_byte(client, 0x0190, 0x00);
							i2c_put_byte(client, 0x019c, 0x4b);
							i2c_put_byte(client, 0x019d, 0xc0);
				break;
			case CAM_BANDING_50HZ:
							printk("set banding 50Hz\n");
							i2c_put_byte(client, 0x0190, 0x00);
							i2c_put_byte(client, 0x019c, 0x4b);
							i2c_put_byte(client, 0x019d, 0x20);
				break;
			default:
				break;
		}
}

static int HM5065_AutoFocus(struct hm5065_device *dev, int focus_mode)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int ret = 0;
    //int i = 0;
    
	switch (focus_mode) {
					case CAM_FOCUS_MODE_AUTO:
					i2c_put_byte(client, 0x070a , 0x03);			
					//msleep(100);
					i2c_put_byte(client, 0x070b , 0x01);			
					msleep(200);
					i2c_put_byte(client, 0x070b , 0x02);
					bDoingAutoFocusMode = true;
					printk("single auto focus mode start\n");
					break;
					case CAM_FOCUS_MODE_CONTI_VID:
					case CAM_FOCUS_MODE_CONTI_PIC:
					i2c_put_byte(client, 0x070a , 0x01); //start to continous focus            
					printk("start continous focus\n");
					break;
					case CAM_FOCUS_MODE_RELEASE:
					case CAM_FOCUS_MODE_FIXED:
					default:
					//i2c_put_byte(client, 0x070a , 0x00);
					//i2c_put_byte(client, 0x070c , 0x00);
					//i2c_put_byte(client, 0x070c , 0x03);			
					printk("release focus to infinit\n");
					break;
    }
    return ret;

}    /* HM5065_AutoFocus */

static int HM5065_FlashCtrl(struct hm5065_device *dev, int flash_mode)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int ret = 0;
    
	switch (flash_mode) {
	case FLASHLIGHT_ON:
	case FLASHLIGHT_AUTO:
		if (dev->cam_info.torch_support)
			aml_cam_torch(&dev->cam_info, 1);
		aml_cam_flash(&dev->cam_info, 1);
		break;
	case FLASHLIGHT_TORCH:
		if (dev->cam_info.torch_support) {
			aml_cam_torch(&dev->cam_info, 1);
			aml_cam_flash(&dev->cam_info, 0);
		} else 
			aml_cam_torch(&dev->cam_info, 1);
		break;
	case FLASHLIGHT_OFF:
		aml_cam_flash(&dev->cam_info, 0);
		if (dev->cam_info.torch_support)
			aml_cam_torch(&dev->cam_info, 0);
		break;
	default:
		printk("this flash mode not support yet\n");
		break;
	}
	return ret;
}    /* HM5065_FlashCtrl */

static resolution_size_t get_size_type(int width, int height)
{
	resolution_size_t rv = SIZE_NULL;
	if (width * height >= 2500 * 1900)
		rv = SIZE_2592X1944;
	else if (width * height >= 2000 * 1500)
		rv = SIZE_2048X1536;
	else if (width * height >= 1920 * 1080)
		rv = SIZE_1920X1080;
	else if (width * height >= 1600 * 1200)
		rv = SIZE_1600X1200;
	else if (width * height >= 1280 * 960)
		rv = SIZE_1280X960;
	else if (width * height >= 1280 * 720)
		rv = SIZE_1280X720;
	else if (width * height >= 1024 * 768)
		rv = SIZE_1024X768;
	else if (width * height >= 800 * 600)
		rv = SIZE_800X600;
	else if (width * height >= 600 * 400)
		rv = SIZE_640X480;
	else if (width * height >= 300 * 200)
		rv = SIZE_320X240;
	return rv;
}

static int set_flip(struct hm5065_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	temp = i2c_get_byte(client, 0x0083);
	temp &= 0xfd;
	temp |= dev->cam_info.m_flip << 0;
	if((i2c_put_byte(client, 0x0083,temp)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
        }
	temp = i2c_get_byte(client, 0x0084);
	temp &= 0xfd;
	temp |= dev->cam_info.v_flip << 0;
	if((i2c_put_byte(client, 0x0084, temp)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
        }
        
        return 0;
}

static resolution_param_t* get_resolution_param(struct hm5065_device *dev, int is_capture, int width, int height)
{
	int i = 0;
	int arry_size = 0;
	resolution_param_t* tmp_resolution_param = NULL;
	resolution_size_t res_type = SIZE_NULL;
	res_type = get_size_type(width, height);
	if (res_type == SIZE_NULL)
		return NULL;
	if (is_capture) {
		tmp_resolution_param = capture_resolution_array;
		arry_size = sizeof(capture_resolution_array);
	} else {
		tmp_resolution_param = prev_resolution_array;
		arry_size = sizeof(prev_resolution_array);
		hm5065_frmintervals_active.denominator = 23;
		hm5065_frmintervals_active.numerator = 1;
	}
	
	for (i = 0; i < arry_size; i++) {
		if (tmp_resolution_param[i].size_type == res_type) {
			hm5065_frmintervals_active.denominator = tmp_resolution_param[i].active_fps;
			hm5065_frmintervals_active.numerator = 1;
			return &tmp_resolution_param[i];
		}
	}
	return NULL;
}

static int set_resolution_param(struct hm5065_device *dev, resolution_param_t* res_param)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int i=0;
	//int rc = -1;
	if (!res_param->reg_script) {
		printk("error, resolution reg script is NULL\n");
		return -1;
	}
	while(1) {
		if (res_param->reg_script[i].val==0xff&&res_param->reg_script[i].addr==0xffff) {
			printk("setting resolutin param complete\n");
			break;
		}
		if((i2c_put_byte(client,res_param->reg_script[i].addr, res_param->reg_script[i].val)) < 0) {
			printk("fail in setting resolution param. i=%d\n",i);
			break;
		}
		i++;
	}
	dev->cur_resolution_param = res_param;
	set_flip(dev);
	return 0;
}

static int set_focus_zone(struct hm5065_device *dev, int value)
	{	  
			int xc, yc;
			struct i2c_client *client =v4l2_get_subdevdata(&dev->sd);
			int retry_count = 9;
			int reg_value = 0;
			int ret = -1;
			xc = ((value >> 16) & 0xffff) * 80 / 2000;
			yc = (value & 0xffff) * 60 / 2000;
			printk("xc1 = %d, yc1 = %d\n", xc, yc); 	
			if(-1==ret)
		   {
			i2c_put_byte(client,0x0808,0x01);
			i2c_put_byte(client,0x0809,0x00); 
			i2c_put_byte(client,0x080a,0x00);
			i2c_put_byte(client,0x080b,0x00);  
			i2c_put_byte(client,0x080c,0x00);
			i2c_put_byte(client,0x080d,0x00);
			i2c_put_byte(client,0x080e,0x00); 
		#if 1
		  i2c_put_byte(client,FACE_LC,0x01);//enable	
			i2c_put_byte(client,FACE_START_XH, xc>>8);
			i2c_put_byte(client,FACE_START_XL, xc&0xFF);		
			i2c_put_byte(client,FACE_START_YH, yc>>8);
			i2c_put_byte(client,FACE_START_YL, yc&0xFF);
			i2c_put_byte(client,FACE_SIZE_XH, 0x00);
			i2c_put_byte(client,FACE_SIZE_XL, 80);
			i2c_put_byte(client,FACE_SIZE_YH, 0x00);
			i2c_put_byte(client,FACE_SIZE_YL, 60);
			printk("SENSOR: _hm5065_Foucs_stareX: %d, %d\n",(xc>>8),(xc&0xFF)); 
			printk("SENSOR: _hm5065_Foucs_stareY: %d, %d\n",(yc>>8),(yc&0xFF)); 
		#endif
			i2c_put_byte(client, 0x070a , 0x03);			
			//msleep(100);
			i2c_put_byte(client, 0x070b , 0x01);			
			msleep(200);
			i2c_put_byte(client, 0x070b , 0x02);
			do
			{
				if(0x00==retry_count)
				{
					printk("SENSOR: _hm5065_AutoFocusZone error!\n"); 
					ret=-1;
					i2c_put_byte(client,0x0700, 0x01);
				   i2c_put_byte(client,0x0701, 0xFD);
					break ;
				}
				msleep(1);		  
				reg_value=i2c_get_byte(client,AF_STATUS);
				retry_count--;
			}while(0x01!=reg_value);
			ret=0;
			focus_position_hi = i2c_get_byte(client,0x06F0);
			focus_position_lo = i2c_get_byte(client,0x06F1);
			i2c_put_byte(client,0x0700, focus_position_hi&0xFF);// target position H
			i2c_put_byte(client,0x0701, focus_position_lo&0xFF);// target position L
			printk("SENSOR: _hm5065_AF status %d\n",i2c_get_byte(client,AF_STATUS)); 
		}
		return ret; 		
		}


unsigned char v4l_2_hm5065(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int convert_canvas_index(unsigned int v4l2_format, unsigned int start_canvas)
{
	int canvas = start_canvas;

	switch(v4l2_format){
	case V4L2_PIX_FMT_RGB565X:
	case V4L2_PIX_FMT_VYUY:
		canvas = start_canvas;
		break;
	case V4L2_PIX_FMT_YUV444:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_RGB24:
		canvas = start_canvas;
		break; 
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21: 
		canvas = start_canvas | ((start_canvas+1)<<8);
		break;
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV420:
		if(V4L2_PIX_FMT_YUV420 == v4l2_format){
			canvas = start_canvas|((start_canvas+1)<<8)|((start_canvas+2)<<16);
		}else{
			canvas = start_canvas|((start_canvas+2)<<8)|((start_canvas+1)<<16);
		}
		break;
	default:
		break;
	}
	return canvas;
}

static int hm5065_setting(struct hm5065_device *dev,int PROP_ID,int value ) 
{
	int ret=0;
	//unsigned char cur_val;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		mutex_lock(&firmware_mutex);
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_hm5065(value));
		ret=i2c_put_byte(client,0x0201,v4l_2_hm5065(value));
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_CONTRAST:
		mutex_lock(&firmware_mutex);
		ret=i2c_put_byte(client,0x0200, value);
		mutex_unlock(&firmware_mutex);
		break;    
	case V4L2_CID_SATURATION:
		mutex_lock(&firmware_mutex);
		ret=i2c_put_byte(client,0x0202, value);
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */
		value = value & 0x3;
		if(hm5065_qctrl[2].default_value!=value){
			hm5065_qctrl[2].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;    
	case V4L2_CID_DO_WHITE_BALANCE:
		mutex_lock(&firmware_mutex);
		if(hm5065_qctrl[4].default_value!=value){
			hm5065_qctrl[4].default_value=value;
			HM5065_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
		}
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_EXPOSURE:
		mutex_lock(&firmware_mutex);
		if(hm5065_qctrl[5].default_value!=value){
			hm5065_qctrl[5].default_value=value;
			HM5065_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
		}
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_COLORFX:
		mutex_lock(&firmware_mutex);
		if(hm5065_qctrl[6].default_value!=value){
			hm5065_qctrl[6].default_value=value;
			HM5065_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
		}
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_WHITENESS:
		mutex_lock(&firmware_mutex);
		if(hm5065_qctrl[7].default_value!=value){
			hm5065_qctrl[7].default_value=value;
			HM5065_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_FOCUS_AUTO:
		mutex_lock(&firmware_mutex);
		if (hm5065_have_opened) {
			if (dev->firmware_ready) 
				ret = HM5065_AutoFocus(dev,value);
			else if (value == CAM_FOCUS_MODE_CONTI_VID ||
        				value == CAM_FOCUS_MODE_CONTI_PIC)
				start_focus_mode = value;
			else
				ret = -1;
		}
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_BACKLIGHT_COMPENSATION:
		if (dev->cam_info.flash_support) 
			ret = HM5065_FlashCtrl(dev,value);
		else
			ret = -1;
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(hm5065_qctrl[10].default_value!=value){
			hm5065_qctrl[10].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(hm5065_qctrl[11].default_value!=value){
			hm5065_qctrl[11].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		if(hm5065_qctrl[12].default_value!=value){
			hm5065_qctrl[12].default_value=value;
			printk(" set camera  focus zone =%d. \n ",value);
			set_focus_zone(dev, value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;
    
}

static void power_down_hm5065(struct hm5065_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	i2c_put_byte(client,0x070a, 0x00); //release focus
	i2c_put_byte(client,0x0010, 0x02);//in soft power down mode
}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void hm5065_fillbuff(struct hm5065_fh *fh, struct hm5065_buffer *buf)
{
	struct hm5065_device *dev = fh->dev;
	//void *vbuf = videobuf_to_vmalloc(&buf->vb);
	void *vbuf = (void *)videobuf_to_res(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);    
	if (!vbuf)
    	return;
	/*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	if(buf->canvas_id == 0)
           buf->canvas_id = convert_canvas_index(fh->fmt->fourcc, HM5065_RES0_CANVAS_INDEX+buf->vb.i*3);
	para.mirror = hm5065_qctrl[2].default_value&3;
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = MAGIC_RE_MEM;//0x18221223;
	para.zoom = hm5065_qctrl[10].default_value;
	para.angle = hm5065_qctrl[11].default_value;
	para.vaddr = (unsigned)vbuf;
	para.ext_canvas = buf->canvas_id;
	para.width = buf->vb.width;
	para.height = buf->vb.height;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void hm5065_thread_tick(struct hm5065_fh *fh)
{
	struct hm5065_buffer *buf;
	struct hm5065_device *dev = fh->dev;
	struct hm5065_dmaqueue *dma_q = &dev->vidq;

	unsigned long flags = 0;

	dprintk(dev, 1, "Thread tick\n");
	if(!fh->stream_on){
		dprintk(dev, 1, "sensor doesn't stream on\n");
		return ;
	}

	spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&dma_q->active)) {
		dprintk(dev, 1, "No active queue to serve\n");
		goto unlock;
	}

	buf = list_entry(dma_q->active.next,
 			struct hm5065_buffer, vb.queue);
	dprintk(dev, 1, "%s\n", __func__);
	dprintk(dev, 1, "list entry get buf is %x\n", (unsigned)buf);


	if(!(fh->f_flags & O_NONBLOCK)){
		/* Nobody is waiting on this buffer, return */
		if (!waitqueue_active(&buf->vb.done))
			goto unlock;
	}
	buf->vb.state = VIDEOBUF_ACTIVE;

	list_del(&buf->vb.queue);

	do_gettimeofday(&buf->vb.ts);

	/* Fill buffer */
	spin_unlock_irqrestore(&dev->slock, flags);
	hm5065_fillbuff(fh, buf);
	dprintk(dev, 1, "filled buffer %p\n", buf);

	wake_up(&buf->vb.done);
	dprintk(dev, 2, "[%p/%d] wakeup\n", buf, buf->vb. i);
	return;
unlock:
	spin_unlock_irqrestore(&dev->slock, flags);
	return;
}

static void hm5065_sleep(struct hm5065_fh *fh)
{
	struct hm5065_device *dev = fh->dev;
	struct hm5065_dmaqueue *dma_q = &dev->vidq;

	//int timeout;
	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
        (unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
    	goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(1);

	hm5065_thread_tick(fh);

	schedule_timeout_interruptible(1);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int hm5065_thread(void *data)
{
	struct hm5065_fh  *fh = data;
	struct hm5065_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		hm5065_sleep(fh);
		
		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int hm5065_start_thread(struct hm5065_fh *fh)
{
	struct hm5065_device *dev = fh->dev;
	struct hm5065_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(hm5065_thread, fh, "hm5065");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void hm5065_stop_thread(struct hm5065_dmaqueue  *dma_q)
{
	struct hm5065_device *dev = container_of(dma_q, struct hm5065_device, vidq);

	dprintk(dev, 1, "%s\n", __func__);
	/* shutdown control thread */
	if (dma_q->kthread) {
		kthread_stop(dma_q->kthread);
		dma_q->kthread = NULL;
	}
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct videobuf_res_privdata *res = vq->priv_data;
	struct hm5065_fh *fh  = container_of(res, struct hm5065_fh, res);
	struct hm5065_device *dev  = fh->dev;
	//int bytes = fh->fmt->depth >> 3 ;
	int height = fh->height;
	if(height==1080)
           height = 1088;
	*size = (fh->width*height*fh->fmt->depth)>>3;    
	if (0 == *count)
        *count = 32;

	while (*size * *count > vid_limit * 1024 * 1024)
        (*count)--;

	dprintk(dev, 1, "%s, count=%d, size=%d\n", __func__,
        *count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct hm5065_buffer *buf)
{
	struct videobuf_res_privdata *res = vq->priv_data;
	struct hm5065_fh *fh  = container_of(res, struct hm5065_fh, res);
	struct hm5065_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);
	videobuf_waiton(vq, &buf->vb, 0, 0);

	if (in_interrupt())
    		BUG();

	videobuf_res_free(vq, &buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 3000
#define norm_maxh() 3000
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
                    	enum v4l2_field field)
{
	struct videobuf_res_privdata *res = vq->priv_data;
	struct hm5065_fh *fh  = container_of(res, struct hm5065_fh, res);
	struct hm5065_device    *dev = fh->dev;
	struct hm5065_buffer *buf = container_of(vb, struct hm5065_buffer, vb);
	int rc;
	//int bytes = fh->fmt->depth >> 3 ;
	dprintk(dev, 1, "%s, field=%d\n", __func__, field);

	BUG_ON(NULL == fh->fmt);

	if (fh->width  < 48 || fh->width  > norm_maxw() ||
			fh->height < 32 || fh->height > norm_maxh())
		return -EINVAL;

	buf->vb.size = (fh->width*fh->height*fh->fmt->depth)>>3;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
    	return -EINVAL;

	/* These properties only change when queue is idle, see s_fmt */
	buf->fmt       = fh->fmt;
	buf->vb.width  = fh->width;
	buf->vb.height = fh->height;
	buf->vb.field  = field;

	//precalculate_bars(fh);

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
    	rc = videobuf_iolock(vq, &buf->vb, NULL);
		if (rc < 0)
			goto fail;
	}

	buf->vb.state = VIDEOBUF_PREPARED;

	return 0;

fail:
	free_buffer(vq, buf);
	return rc;
}

static void
buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct hm5065_buffer    *buf  = container_of(vb, struct hm5065_buffer, vb);
	struct videobuf_res_privdata *res = vq->priv_data;
	struct hm5065_fh *fh  = container_of(res, struct hm5065_fh, res);
	struct hm5065_device       *dev  = fh->dev;
	struct hm5065_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
               struct videobuf_buffer *vb)
{
	struct hm5065_buffer   *buf  = container_of(vb, struct hm5065_buffer, vb);
	struct videobuf_res_privdata *res = vq->priv_data;
	struct hm5065_fh *fh  = container_of(res, struct hm5065_fh, res);
	struct hm5065_device *dev = (struct hm5065_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops hm5065_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void  *priv,
                	struct v4l2_capability *cap)
{
	struct hm5065_fh  *fh  = priv;
	struct hm5065_device *dev = fh->dev;

	strcpy(cap->driver, "hm5065");
	strcpy(cap->card, "hm5065.canvas");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = HM5065_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
        			V4L2_CAP_STREAMING     |
        			V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
                	struct v4l2_fmtdesc *f)
{
	struct hm5065_fmt *fmt;

	if (f->index >= ARRAY_SIZE(formats))
    	return -EINVAL;

	fmt = &formats[f->index];

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
                	struct v4l2_format *f)
{
	struct hm5065_fh *fh = priv;

	printk("vidioc_g_fmt_vid_cap...fh->width =%d,fh->height=%d\n",fh->width,fh->height);
	f->fmt.pix.width        = fh->width;
	f->fmt.pix.height       = fh->height;
	f->fmt.pix.field        = fh->vb_vidq.field;
	f->fmt.pix.pixelformat  = fh->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fh->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
    	f->fmt.pix.height * f->fmt.pix.bytesperline;

	return (0);
}

static int vidioc_enum_frameintervals(struct file *file, void *priv,
					struct v4l2_frmivalenum *fival)
{
	unsigned int k;
	
	if(fival->index > ARRAY_SIZE(hm5065_frmivalenum))
	return -EINVAL;
	
	for(k =0; k< ARRAY_SIZE(hm5065_frmivalenum); k++) {
		if( (fival->index==hm5065_frmivalenum[k].index)&&
				(fival->pixel_format ==hm5065_frmivalenum[k].pixel_format )&&
				(fival->width==hm5065_frmivalenum[k].width)&&
				(fival->height==hm5065_frmivalenum[k].height)){
			memcpy( fival, &hm5065_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
			return 0;
		}
	}
	
	return -EINVAL;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
        	struct v4l2_format *f)
{
	struct hm5065_fh  *fh  = priv;
	struct hm5065_device *dev = fh->dev;
	struct hm5065_fmt *fmt;
	enum v4l2_field field;
	unsigned int maxw, maxh;

	fmt = get_format(f);
	if (!fmt) {
		dprintk(dev, 1, "Fourcc format (0x%08x) invalid.\n",
				f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	field = f->fmt.pix.field;
	
	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_INTERLACED;
	} else if (V4L2_FIELD_INTERLACED != field) {
		dprintk(dev, 1, "Field type invalid.\n");
		return -EINVAL;
	}

	maxw  = norm_maxw();
	maxh  = norm_maxh();

	f->fmt.pix.field = field;
	v4l_bound_align_image(&f->fmt.pix.width, 48, maxw, 2,
                  &f->fmt.pix.height, 32, maxh, 0, 0);
	f->fmt.pix.bytesperline =
        	(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
    		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static resolution_param_t* prev_res = NULL;

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
                	struct v4l2_format *f)
{
	struct hm5065_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct hm5065_device *dev = fh->dev;
	resolution_param_t* res_param = NULL;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//unsigned char gain = 0, exposurelow = 0, exposuremid = 0, exposurehigh = 0;
	int cap_fps, pre_fps;

	int ret = vidioc_try_fmt_vid_cap(file, fh, f);
        f->fmt.pix.width = (f->fmt.pix.width + (CANVAS_WIDTH_ALIGN-1) ) & (~(CANVAS_WIDTH_ALIGN-1));
	if ((f->fmt.pix.pixelformat==V4L2_PIX_FMT_YVU420) ||
            (f->fmt.pix.pixelformat==V4L2_PIX_FMT_YUV420)){
                f->fmt.pix.width = (f->fmt.pix.width + (CANVAS_WIDTH_ALIGN*2-1) ) & (~(CANVAS_WIDTH_ALIGN*2-1));
        }
	if (ret < 0)
    	return ret;

	mutex_lock(&q->vb_lock);

	if (videobuf_queue_is_busy(&fh->vb_vidq)) {
		dprintk(fh->dev, 1, "%s queue busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	fh->fmt           = get_format(f);
	fh->width         = f->fmt.pix.width;
	fh->height        = f->fmt.pix.height;
	fh->vb_vidq.field = f->fmt.pix.field;
	fh->type          = f->type;
	printk("system aquire ...fh->height=%d, fh->width= %d\n",fh->height,fh->width);//potti
#if 1
	if(f->fmt.pix.pixelformat==V4L2_PIX_FMT_RGB24){
		res_param = get_resolution_param(dev, 1, fh->width,fh->height);
		if (!res_param) {
			printk("error, resolution param not get\n");
			goto out;
		}
		/*get_exposure_param(dev, &gain, &exposurelow, &exposuremid, &exposurehigh);
		printk("gain=0x%x, exposurelow=0x%x, exposuremid=0x%x, exposurehigh=0x%x\n",
				 gain, exposurelow, exposuremid, exposurehigh);
		*/
		set_resolution_param(dev, res_param);
		//set_exposure_param_500m(dev, gain, exposurelow, exposuremid, exposurehigh);
		if (prev_res && (prev_res->size_type == SIZE_1280X960 
				|| prev_res->size_type == SIZE_1024X768)) {
			pre_fps = 1500;
		} else if (prev_res && prev_res->size_type == SIZE_1280X720 ) {
			pre_fps = 3000;
		} else {
			pre_fps = 1500;
		} 
		if (res_param && res_param->size_type == SIZE_2592X1944 ) {
			//cap_fps = 750;
			cap_fps = 500;
		} else {
			cap_fps = 750;
		} 
		printk("pre_fps=%d,cap_fps=%d\n", pre_fps, cap_fps);
	} else {
		res_param = get_resolution_param(dev, 0, fh->width,fh->height);
		if (!res_param) {
			printk("error, resolution param not get\n");
			goto out;
		}
		set_resolution_param(dev, res_param);
		prev_res = res_param;
	}
    
#endif
	ret = 0;
out:
	mutex_unlock(&q->vb_lock);
	
	return ret;
}

static int vidioc_g_parm(struct file *file, void *priv,
 				struct v4l2_streamparm *parms)
{
	struct hm5065_fh *fh = priv;
	struct hm5065_device *dev = fh->dev;
	struct v4l2_captureparm *cp = &parms->parm.capture;
	
	dprintk(dev,3,"vidioc_g_parm\n");
	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	
	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	
	cp->timeperframe = hm5065_frmintervals_active;
	printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
	cp->timeperframe.numerator );
	return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
              struct v4l2_requestbuffers *p)
{
	struct hm5065_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct hm5065_fh  *fh = priv;
	int ret = videobuf_querybuf(&fh->vb_vidq, p);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	if(ret == 0){
	    p->reserved  = convert_canvas_index(fh->fmt->fourcc, HM5065_RES0_CANVAS_INDEX+p->index*3);
	}else{
	    p->reserved = 0;
	}
#endif
	return ret;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct hm5065_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct hm5065_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
            	file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct hm5065_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
        struct hm5065_fh  *fh = priv;
        struct hm5065_device *dev = fh->dev;
        vdin_parm_t para;
        int ret = 0 ;
        if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
                return -EINVAL;
        if (i != fh->type)
                return -EINVAL;

        memset( &para, 0, sizeof( para ));
        para.port  = TVIN_PORT_CAMERA;
        para.fmt = TVIN_SIG_FMT_MAX;//TVIN_SIG_FMT_MAX+1;;TVIN_SIG_FMT_CAMERA_1280x720P_30Hz
        if (fh->dev->cur_resolution_param) {
                para.frame_rate = hm5065_frmintervals_active.denominator;
                para.h_active = fh->dev->cur_resolution_param->active_frmsize.width;
                para.v_active = fh->dev->cur_resolution_param->active_frmsize.height;
                para.hs_bp = 0;
                para.vs_bp = 2;
                para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
        } else {
                para.frame_rate = hm5065_frmintervals_active.denominator;;
                para.h_active = fh->dev->cur_resolution_param->active_frmsize.width;
                para.v_active = fh->dev->cur_resolution_param->active_frmsize.height;
                para.hs_bp = 0;
                para.vs_bp = 2;
                para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
        }

        printk("hm5065: h_active = %d; v_active = %d, frame_rate=%d\n",
                        para.h_active, para.v_active, para.frame_rate);
        para.cfmt = TVIN_YUV422;
        para.dfmt = TVIN_NV21;
        para.hsync_phase = 1;
        para.vsync_phase  = 1;    
        para.skip_count =  2;
        para.bt_path = dev->cam_info.bt_path;
        ret =  videobuf_streamon(&fh->vb_vidq);
        if(ret == 0){
                vops->start_tvin_service(0,&para);
                fh->stream_on = 1;
        }
		HM5065_set_param_wb(dev,hm5065_qctrl[4].default_value);
        return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct hm5065_fh  *fh = priv;

        int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    	return -EINVAL;
	if (i != fh->type)
    	return -EINVAL;
	ret = videobuf_streamoff(&fh->vb_vidq);
	if(ret == 0 ){
		vops->stop_tvin_service(0);
		fh->stream_on        = 0;
	}
	return ret;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,struct v4l2_frmsizeenum *fsize)
{
        int ret = 0,i=0;
        struct hm5065_fmt *fmt = NULL;
        struct v4l2_frmsize_discrete *frmsize = NULL;
        for (i = 0; i < ARRAY_SIZE(formats); i++) {
                if (formats[i].fourcc == fsize->pixel_format){
                        fmt = &formats[i];
                        break;
                }
        }
        if (fmt == NULL)
                return -EINVAL;
        if ((fmt->fourcc == V4L2_PIX_FMT_NV21)
                        ||(fmt->fourcc == V4L2_PIX_FMT_NV12)
                        ||(fmt->fourcc == V4L2_PIX_FMT_YUV420)
                        ||(fmt->fourcc == V4L2_PIX_FMT_YVU420)){
                printk("hm5065_prev_resolution[fsize->index]"
                                "   before fsize->index== %d\n",fsize->index);//potti
                if (fsize->index >= ARRAY_SIZE(prev_resolution_array))
                        return -EINVAL;
                frmsize = &prev_resolution_array[fsize->index].frmsize;
                printk("hm5065_prev_resolution[fsize->index]"
                                "   after fsize->index== %d\n",fsize->index);
                fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
                fsize->discrete.width = frmsize->width;
                fsize->discrete.height = frmsize->height;
        } else if (fmt->fourcc == V4L2_PIX_FMT_RGB24){
                printk("hm5065_pic_resolution[fsize->index]"
                                "   before fsize->index== %d\n",fsize->index);
                if (fsize->index >= ARRAY_SIZE(capture_resolution_array))
                        return -EINVAL;
                frmsize = &capture_resolution_array[fsize->index].frmsize;
                printk("hm5065_pic_resolution[fsize->index]"
                                "   after fsize->index== %d\n",fsize->index);
                fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
                fsize->discrete.width = frmsize->width;
                fsize->discrete.height = frmsize->height;
        }
        return ret;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id i)
{
	return 0;
}

/* only one input in this sample driver */
static int vidioc_enum_input(struct file *file, void *priv,
            	struct v4l2_input *inp)
{
    //if (inp->index >= NUM_INPUTS)
        //return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_525_60;
	sprintf(inp->name, "Camera %u", inp->index);

	return (0);
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct hm5065_fh *fh = priv;
	struct hm5065_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct hm5065_fh *fh = priv;
	struct hm5065_device *dev = fh->dev;

    //if (i >= NUM_INPUTS)
        //return -EINVAL;

	dev->input = i;
    //precalculate_bars(fh);

	return (0);
}

    /* --- controls ---------------------------------------------- */
static int vidioc_queryctrl(struct file *file, void *priv,
                struct v4l2_queryctrl *qc)
{
	int i;
	struct hm5065_fh *fh = priv;
	struct hm5065_device *dev = fh->dev;
	
	if (!dev->cam_info.flash_support 
			&& qc->id == V4L2_CID_BACKLIGHT_COMPENSATION)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(hm5065_qctrl); i++)
		if (qc->id && qc->id == hm5065_qctrl[i].id) {
			memcpy(qc, &(hm5065_qctrl[i]),
				sizeof(*qc));
			if (hm5065_qctrl[i].type == V4L2_CTRL_TYPE_MENU)
				return hm5065_qctrl[i].maximum+1;
			else
				return (0);
		}

	return -EINVAL;
}

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(hm5065_qmenu_set); i++)
 		if (a->id && a->id == hm5065_qmenu_set[i].id) {
 			for(j = 0; j < hm5065_qmenu_set[i].num; j++)
 				if (a->index == hm5065_qmenu_set[i].hm5065_qmenu[j].index) {
 					memcpy(a, &( hm5065_qmenu_set[i].hm5065_qmenu[j]),
 		    			sizeof(*a));
 					return (0);
 				}
 		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
             struct v4l2_control *ctrl)
{
	struct hm5065_fh *fh = priv;
	struct hm5065_device *dev = fh->dev;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int i;
	int i2cret = -1;

	for (i = 0; i < ARRAY_SIZE(hm5065_qctrl); i++)
		if (ctrl->id == hm5065_qctrl[i].id) {
			if( (V4L2_CID_FOCUS_AUTO == ctrl->id)
					&& bDoingAutoFocusMode){
				if(i2c_get_byte(client, 0x3023)){
					return -EBUSY;
				}else{
					bDoingAutoFocusMode = false;
					if(i2c_get_byte(client, 0x3028) == 0){
						printk("auto mode failed!\n");
						return -EAGAIN;
					}else {
						i2c_put_byte(client, 0x3022 , 0x6); //pause the auto focus
						i2c_put_byte(client, 0x3023 , 0x1);
						printk("pause auto focus\n");
					}
				}
			}else if( V4L2_CID_AUTO_FOCUS_STATUS == ctrl->id){
				i2cret = i2c_get_byte(client, 0x3029);
				if( 0x00 == i2cret){
					ctrl->value = V4L2_AUTO_FOCUS_STATUS_BUSY;
				}else if( 0x10 == i2cret){
					ctrl->value = V4L2_AUTO_FOCUS_STATUS_REACHED;
				}else if( 0x20 == i2cret){
					ctrl->value = V4L2_AUTO_FOCUS_STATUS_IDLE;
				}else{
					printk("should resart focus\n");
					ctrl->value = V4L2_AUTO_FOCUS_STATUS_FAILED;
				}
		        	
				return 0;
			}
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
            	struct v4l2_control *ctrl)
{
	struct hm5065_fh *fh = priv;
	struct hm5065_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(hm5065_qctrl); i++)
    	if (ctrl->id == hm5065_qctrl[i].id) {
        	if (ctrl->value < hm5065_qctrl[i].minimum ||
			ctrl->value > hm5065_qctrl[i].maximum ||
			hm5065_setting(dev,ctrl->id,ctrl->value)<0) {
			return -ERANGE;
		}
        	dev->qctl_regs[i] = ctrl->value;
        	return 0;
        }
	return -EINVAL;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static int hm5065_open(struct file *file)
{
	struct hm5065_device *dev = video_drvdata(file);
	struct hm5065_fh *fh = NULL;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	resource_size_t mem_start = 0;
	unsigned int mem_size = 0;
	int retval = 0;
	//int reg_val;
	//int i = 0;
#if CONFIG_CMA
    retval = vm_init_buf(32*SZ_1M);
    if(retval <0) {
    	printk("error: no cma memory\n");
        return -1;
    }
#endif
	mutex_lock(&firmware_mutex);
	hm5065_have_opened=1;
	mutex_unlock(&firmware_mutex);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif	
	aml_cam_init(&dev->cam_info);
	
	HM5065_init_regs(dev);
	
	msleep(10);
	
	/*if(HM5065_download_firmware(dev) >= 0) {
		while(i2c_get_byte(client, 0x3029) != 0x70 && i < 10) { //wait for the mcu ready 
        	msleep(5);
        	i++;
    	}
    	dev->firmware_ready = 1;
	}*/
	
	schedule_work(&(dev->dl_work));

	mutex_lock(&dev->mutex);
	dev->users++;
	if (dev->users > 1) {
		dev->users--;
		mutex_unlock(&dev->mutex);
		return -EBUSY;
	}

	dprintk(dev, 1, "open %s type=%s users=%d\n",
    	video_device_node_name(dev->vdev),
    	v4l2_type_names[V4L2_BUF_TYPE_VIDEO_CAPTURE], dev->users);

        /* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	init_waitqueue_head(&dev->vidq.wq);
	spin_lock_init(&dev->slock);
	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh) {
		dev->users--;
		retval = -ENOMEM;
	}
	mutex_unlock(&dev->mutex);

	if (retval)
    	return retval;

	wake_lock(&(dev->wake_lock));
	file->private_data = fh;
	fh->dev      = dev;

	fh->type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fh->fmt      = &formats[0];
	fh->width    = 640;
	fh->height   = 480;
	fh->stream_on = 0 ;
	fh->f_flags  = file->f_flags;
	/* Resets frame counters */
	dev->jiffies = jiffies;

	get_vm_buf_info(&mem_start, &mem_size, NULL);
	fh->res.start = mem_start;
	fh->res.end = mem_start+mem_size-1;
	fh->res.magic = MAGIC_RE_MEM;
	fh->res.priv = NULL;		
	videobuf_queue_res_init(&fh->vb_vidq, &hm5065_video_qops,
					NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
					sizeof(struct hm5065_buffer), (void*)&fh->res, NULL);

	bDoingAutoFocusMode=false;
	hm5065_start_thread(fh);
	return 0;
}

static ssize_t
hm5065_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct hm5065_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
                	file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
hm5065_poll(struct file *file, struct poll_table_struct *wait)
{
	struct hm5065_fh        *fh = file->private_data;
	struct hm5065_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
    	return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int hm5065_close(struct file *file)
{
	struct hm5065_fh         *fh = file->private_data;
	struct hm5065_device *dev       = fh->dev;
	struct hm5065_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);
	mutex_lock(&firmware_mutex);
	hm5065_have_opened=0;
	dev->firmware_ready = 0;
	mutex_unlock(&firmware_mutex);
	hm5065_stop_thread(vidq);
	videobuf_stop(&fh->vb_vidq);
	if(fh->stream_on){
		vops->stop_tvin_service(0);     
	}
	videobuf_mmap_free(&fh->vb_vidq);

	kfree(fh);

	mutex_lock(&dev->mutex);
	dev->users--;
	mutex_unlock(&dev->mutex);

	dprintk(dev, 1, "close called (dev=%s, users=%d)\n",
    	video_device_node_name(vdev), dev->users);
#if 1	    
	hm5065_qctrl[4].default_value=0;
	hm5065_qctrl[5].default_value=4;
	hm5065_qctrl[6].default_value=0;
	
	hm5065_qctrl[2].default_value=0;
	hm5065_qctrl[10].default_value=100;
	hm5065_qctrl[11].default_value=0;
	power_down_hm5065(dev);
#endif
	hm5065_frmintervals_active.numerator = 1;
	hm5065_frmintervals_active.denominator = 25;
	
	aml_cam_uninit(&dev->cam_info);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 0);
#endif	
	wake_unlock(&(dev->wake_lock));
#ifdef CONFIG_CMA
    vm_deinit_buf();
#endif
	return 0;
}

static int hm5065_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct hm5065_fh  *fh = file->private_data;
	struct hm5065_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations hm5065_fops = {
	.owner	    = THIS_MODULE,
	.open       = hm5065_open,
	.release    = hm5065_close,
	.read       = hm5065_read,
	.poll	    = hm5065_poll,
	.ioctl      = video_ioctl2, /* V4L2 ioctl handler */
	.mmap       = hm5065_mmap,
};

static const struct v4l2_ioctl_ops hm5065_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_s_std         = vidioc_s_std,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_queryctrl     = vidioc_queryctrl,
	.vidioc_querymenu     = vidioc_querymenu,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,
	.vidioc_g_parm = vidioc_g_parm,
	.vidioc_enum_frameintervals = vidioc_enum_frameintervals,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
};

static struct video_device hm5065_template = {
	.name	        = "hm5065_v4l",
	.fops           = &hm5065_fops,
	.ioctl_ops      = &hm5065_ioctl_ops,
	.release	    = video_device_release,
	
	.tvnorms        = V4L2_STD_525_60,
	.current_norm   = V4L2_STD_NTSC_M,
};

static int hm5065_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_HM5065, 0);
}

static const struct v4l2_subdev_core_ops hm5065_core_ops = {
	.g_chip_ident = hm5065_g_chip_ident,
};

static const struct v4l2_subdev_ops hm5065_ops = {
	.core = &hm5065_core_ops,
};

static int hm5065_probe(struct i2c_client *client,
        	const struct i2c_device_id *id)
{
	aml_cam_info_t* plat_dat;
	int err;
	struct hm5065_device *t;
	struct v4l2_subdev *sd;
	vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
        	client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
    		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &hm5065_ops);
	mutex_init(&t->mutex);

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &hm5065_template, sizeof(*t->vdev));
	
	video_set_drvdata(t->vdev, t);
	
	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "hm5065");
	/* Register it */
	plat_dat= (aml_cam_info_t*)client->dev.platform_data;
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if (plat_dat->front_back >=0)  
			video_nr = plat_dat->front_back;
	} else {
		printk("camera hm5065: have no platform data\n");
		kfree(t);
		kfree(client);
		return -1;
	}
	
	t->cam_info.version = HM5065_DRIVER_VERSION;
	if (aml_cam_info_reg(&t->cam_info) < 0)
		printk("reg caminfo error\n");
	
	err = video_register_device(t->vdev, VFL_TYPE_GRABBER, video_nr);
	if (err < 0) {
		video_device_release(t->vdev);
		kfree(t);
		return err;
	}
	return 0;
}

static int hm5065_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hm5065_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id hm5065_id[] = {
	{ "hm5065", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hm5065_id);

static struct i2c_driver hm5065_i2c_driver = {
	.driver = {
		.name = "hm5065",
	},
	.probe = hm5065_probe,
	.remove = hm5065_remove,
	.id_table = hm5065_id,
};

module_i2c_driver(hm5065_i2c_driver);

