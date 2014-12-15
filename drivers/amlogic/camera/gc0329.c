/*
 *gc0329 - This code emulates a real video device with v4l2 api
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
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <linux/wakelock.h>

#include <linux/i2c.h>
#include <media/v4l2-chip-ident.h>
#include <linux/amlogic/camera/aml_cam_info.h>
#include <linux/amlogic/vmapi.h>

#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <mach/gpio.h>
#include "common/plat_ctrl.h"
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif

#define GC0329_CAMERA_MODULE_NAME "gc0329"

static struct vdin_v4l2_ops_s *vops;

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define GC0329_CAMERA_MAJOR_VERSION 0
#define GC0329_CAMERA_MINOR_VERSION 7
#define GC0329_CAMERA_RELEASE 0
#define GC0329_CAMERA_VERSION \
	KERNEL_VERSION(GC0329_CAMERA_MAJOR_VERSION, GC0329_CAMERA_MINOR_VERSION, GC0329_CAMERA_RELEASE)

MODULE_DESCRIPTION("gc0329 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define GC0329_DRIVER_VERSION "GC0329-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");


static int gc0329_h_active=320;
static int gc0329_v_active=240;
static struct v4l2_fract gc0329_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

typedef enum
{
	GC0329_RGB_Gamma_m1 = 1,
	GC0329_RGB_Gamma_m2,
	GC0329_RGB_Gamma_m3,
	GC0329_RGB_Gamma_m4,
	GC0329_RGB_Gamma_m5,
	GC0329_RGB_Gamma_m6,
	GC0329_RGB_Gamma_night
}GC0329_GAMMA_TAG;

//static void GC0329AwbEnable(struct gc0329_device *dev, int Enable);
//void GC0329GammaSelect(struct gc0329_device *dev, GC0329_GAMMA_TAG GammaLvl);
	// void GC0329write_more_registers(struct gc0329_device *dev);
static struct
{
	bool BypassAe;
	bool BypassAwb;
	bool CapState; /* TRUE: in capture state, else in preview state */
	bool PvMode; /* TRUE: preview size output, else full size output */
	bool VideoMode; /* TRUE: video mode, else preview mode */
	bool NightMode;/*TRUE:work in night mode, else normal mode*/
	unsigned char BandingFreq; /* GC0329_50HZ or GC0329_60HZ for 50Hz/60Hz */
	unsigned InternalClock; /* internal clock which using process pixel(for exposure) */
	unsigned Pclk; /* output clock which output to baseband */
	unsigned Gain; /* base on 0x40 */
	unsigned Shutter; /* unit is (linelength / internal clock) s */
	unsigned FrameLength; /* total line numbers in one frame(include dummy line) */
	unsigned LineLength; /* total pixel numbers in one line(include dummy pixel) */
	//IMAGE_SENSOR_INDEX_ENUM SensorIdx;
	//sensor_data_struct *NvramData;
} GC0329Sensor;



/* supported controls */
static struct v4l2_queryctrl gc0329_qctrl[] = {
	{
		.id            = V4L2_CID_DO_WHITE_BALANCE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
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
		.id            = V4L2_CID_BLUE_BALANCE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "scene mode",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 0x1,
		.default_value = 0,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id            = V4L2_CID_HFLIP,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "flip on horizontal",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 0x1,
		.default_value = 0,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id            = V4L2_CID_VFLIP,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "flip on vertical",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 0x1,
		.default_value = 0,
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
 	}
};
static struct v4l2_frmivalenum gc0329_frmivalenum[]={
	{
		.index 		= 0,
		.pixel_format	= V4L2_PIX_FMT_NV21,
		.width		= 640,
		.height		= 480,
		.type		= V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete	={
				.numerator	= 1,
				.denominator	= 15,
			}
		}
	},{
		.index 		= 1,
		.pixel_format	= V4L2_PIX_FMT_NV21,
		.width		= 1600,
		.height		= 1200,
		.type		= V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete	={
				.numerator	= 1,
				.denominator	= 5,
			}
		}
	},
};

#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct gc0329_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct gc0329_fmt formats[] = {
	{
		.name     = "RGB565 (BE)",
		.fourcc   = V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
		.depth    = 16,
	},
	{
		.name     = "RGB888 (24)",
		.fourcc   = V4L2_PIX_FMT_RGB24, /* 24  RGB-8-8-8 */
		.depth    = 24,
	},
	{
		.name     = "BGR888 (24)",
		.fourcc   = V4L2_PIX_FMT_BGR24, /* 24  BGR-8-8-8 */
		.depth    = 24,
	},
	{
		.name     = "12  Y/CbCr 4:2:0",
		.fourcc   = V4L2_PIX_FMT_NV12,
		.depth    = 12,
	},
	{
		.name     = "12  Y/CbCr 4:2:0",
		.fourcc   = V4L2_PIX_FMT_NV21,
		.depth    = 12,
	},
	{
		.name     = "YUV420P",
		.fourcc   = V4L2_PIX_FMT_YUV420,
		.depth    = 12,
	},
	{
		.name     = "YVU420P",
		.fourcc   = V4L2_PIX_FMT_YVU420,
		.depth    = 12,
	}
#if 0
	{
		.name     = "4:2:2, packed, YUYV",
		.fourcc   = V4L2_PIX_FMT_VYUY,
		.depth    = 16,
	},
	{
		.name     = "RGB565 (LE)",
		.fourcc   = V4L2_PIX_FMT_RGB565, /* gggbbbbb rrrrrggg */
		.depth    = 16,
	},
	{
		.name     = "RGB555 (LE)",
		.fourcc   = V4L2_PIX_FMT_RGB555, /* gggbbbbb arrrrrgg */
		.depth    = 16,
	},
	{
		.name     = "RGB555 (BE)",
		.fourcc   = V4L2_PIX_FMT_RGB555X, /* arrrrrgg gggbbbbb */
		.depth    = 16,
	},
#endif
};

static struct gc0329_fmt *get_format(struct v4l2_format *f)
{
	struct gc0329_fmt *fmt;
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
struct gc0329_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct gc0329_fmt        *fmt;
};

struct gc0329_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(gc0329_devicelist);

struct gc0329_device {
	struct list_head			gc0329_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct gc0329_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(gc0329_qctrl)];
};

static inline struct gc0329_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc0329_device, sd);
}

struct gc0329_fh {
	struct gc0329_device            *dev;

	/* video capture */
	struct gc0329_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};

/*static inline struct gc0329_fh *to_fh(struct gc0329_device *dev)
{
	return container_of(dev, struct gc0329_fh, dev);
}*/

static struct v4l2_frmsize_discrete gc0329_prev_resolution[]= //should include 320x240 and 640x480, those two size are used for recording
{
	{320,240},
	{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete gc0329_pic_resolution[]=
{
	{640,480},
};

/* ------------------------------------------------------------------
	reg spec of GC0329
   ------------------------------------------------------------------*/
struct aml_camera_i2c_fig1_s GC0329_script[] = {
	{0xfe,0x80},
	{0xfe, 0x80},
	{0xfc, 0x16},
	{0xfc, 0x16},
	{0xfc, 0x16},
	{0xfc, 0x16},
	{0xfe,0x00},
	{0xf0, 0x07},
	{0xf1, 0x01},

	{0x73, 0x90},
	{0x74, 0x80},
	{0x75, 0x80},
	{0x76, 0x94},

	{0x42, 0x00},
	{0x77, 0x57},
	{0x78, 0x4d},
	{0x79, 0x45},
	//{0x42, 0xfc},

	////////////////////analog////////////////////
	{0x0a, 0x02},
	{0x0c, 0x02},
	{0x17, 0x16},
	{0x19, 0x05},
	{0x1b, 0x24},
	{0x1c, 0x04},
	{0x1e, 0x08},
	{0x1f, 0xc0},
	{0x20, 0x00},
	{0x21, 0x48},
	{0x22, 0xba},
	{0x23, 0x22},
	{0x24, 0x16},

	////////////////////blk////////////////////
	{0x26, 0xf7}, 
	{0x29, 0x80}, 
	{0x32, 0x04},
	{0x33, 0x20},
	{0x34, 0x20},
	{0x35, 0x20},
	{0x36, 0x20},

	////////////////////ISP BLOCK ENABL////////////////////
	{0x40, 0xff},
	{0x41, 0x44},
	{0x42, 0x7e},
	{0x44, 0xa2},
	{0x46, 0x02},
	{0x4b, 0xca},
	{0x4d, 0x01},
	{0x4f, 0x01},     
	{0x03, 0x01}, 
	{0x04, 0xe0},
	{0x70, 0x48},

	//{0xb0, 0x00},
	//{0xbc, 0x00},
	//{0xbd, 0x00},
	//{0xbe, 0x00},
	////////////////////DNDD////////////////////
	{0x80, 0xe7}, 
	{0x82, 0x55}, 
	{0x83, 0x02}, // 03--dndd
	{0x87, 0x4a},

	////////////////////INTPEE////////////////////
	{0x95, 0x45},

	////////////////////ASDE////////////////////
	//{0xfe, 0x01},
	//{0x18, 0x22},
	//{0xfe , 0x00},
	//{0x9c, 0x0a},
	//{0xa0, 0xaf},
	//{0xa2, 0xff},
	//{0xa4, 0x50},
	//{0xa5, 0x21},
	//{0xa7, 0x35},

	////////////////////RGB gamma////////////////////
	//RGB gamma m4'
	{0xbf, 0x06},
	{0xc0, 0x14},
	{0xc1, 0x27},
	{0xc2, 0x3b},
	{0xc3, 0x4f},
	{0xc4, 0x62},
	{0xc5, 0x72},
	{0xc6, 0x8d},
	{0xc7, 0xa4},
	{0xc8, 0xb8},
	{0xc9, 0xc9},
	{0xcA, 0xd6},
	{0xcB, 0xe0},
	{0xcC, 0xe8},
	{0xcD, 0xf4},
	{0xcE, 0xFc},
	{0xcF, 0xFF},

	//////////////////CC///////////////////
	{0xfe, 0x00},

	{0xb3, 0x44},
	{0xb4, 0xfd},
	{0xb5, 0x02},
	{0xb6, 0xfa},
	{0xb7, 0x48},
	{0xb8, 0xf0},

	// crop 						   
	{0x50, 0x01},

	////////////////////YCP////////////////////
	{0xfe, 0x00},
	{0xd1, 0x38},
	{0xd2, 0x38},
	{0xdd, 0x54},

	////////////////////AEC////////////////////
	{0xfe, 0x01},
	{0x10, 0x40},
	{0x11, 0x21},
	{0x12, 0x07},
	{0x13, 0x50},
	{0x17, 0x88},
	{0x21, 0xb0},
	{0x22, 0x48},
	{0x3c, 0x95},
	{0x3d, 0x50},
	{0x3e, 0x48}, 

	////////////////////AWB////////////////////
	{0xfe, 0x01},
	{0x06, 0x16},
	{0x07, 0x06},
	{0x08, 0x98},
	{0x09, 0xee},
	{0x50, 0xfc},
	{0x51, 0x20},
	{0x52, 0x0b},
	{0x53, 0x20},
	{0x54, 0x40},
	{0x55, 0x10},
	{0x56, 0x20},
	//{0x57, 0x40},
	{0x58, 0xa0},
	{0x59, 0x28},
	{0x5a, 0x02},
	{0x5b, 0x63},
	{0x5c, 0x34},
	{0x5d, 0x73},
	{0x5e, 0x11},
	{0x5f, 0x40},
	{0x60, 0x40},
	{0x61, 0xc8},
	{0x62, 0xa0},
	{0x63, 0x40},
	{0x64, 0x50},
	{0x65, 0x98},
	{0x66, 0xfa},
	{0x67, 0x70},
	{0x68, 0x58},
	{0x69, 0x85},
	{0x6a, 0x40},
	{0x6b, 0x39},
	{0x6c, 0x18},
	{0x6d, 0x28},
	{0x6e, 0x41},
	{0x70, 0x02},
	{0x71, 0x00},
	{0x72, 0x10},
	{0x73, 0x40},

	//{0x74, 0x32},
	//{0x75, 0x40},
	//{0x76, 0x30},
	//{0x77, 0x48},
	//{0x7a, 0x50},
	//{0x7b, 0x20}, 

	{0x80, 0x60},
	{0x81, 0x50},
	{0x82, 0x42},
	{0x83, 0x40},
	{0x84, 0x40},
	{0x85, 0x40},

	{0x74, 0x40},
	{0x75, 0x58},
	{0x76, 0x24},
	{0x77, 0x40},
	{0x78, 0x20},
	{0x79, 0x60},
	{0x7a, 0x58},
	{0x7b, 0x20},
	{0x7c, 0x30},
	{0x7d, 0x35},
	{0x7e, 0x10},
	{0x7f, 0x08},

	////////////////////ABS////////////////////
	{0x9c, 0x02}, 
	{0x9d, 0x20},
	//{0x9f, 0x40},	

	////////////////////CC-AWB////////////////////
	{0xd0, 0x00},
	{0xd2, 0x2c},
	{0xd3, 0x80}, 

	////////////////////LSC///////////////////
	{0xfe,0x01},
	{0xa0, 0x00},
	{0xa1, 0x3c},
	{0xa2, 0x50},
	{0xa3, 0x00},
	{0xa8, 0x0f},
	{0xa9, 0x08},
	{0xaa, 0x00},
	{0xab, 0x04},
	{0xac, 0x00},
	{0xad, 0x07},
	{0xae, 0x0e},
	{0xaf, 0x00},
	{0xb0, 0x00},
	{0xb1, 0x09},
	{0xb2, 0x00},
	{0xb3, 0x00},
	{0xb4, 0x31},
	{0xb5, 0x19},
	{0xb6, 0x24},
	{0xba, 0x3a},
	{0xbb, 0x24},
	{0xbc, 0x2a},
	{0xc0, 0x17},
	{0xc1, 0x13},
	{0xc2, 0x17},
	{0xc6, 0x21},
	{0xc7, 0x1c},
	{0xc8, 0x1c},
	{0xb7, 0x00},
	{0xb8, 0x00},
	{0xb9, 0x00},
	{0xbd, 0x00},
	{0xbe, 0x00},
	{0xbf, 0x00},
	{0xc3, 0x00},
	{0xc4, 0x00},
	{0xc5, 0x00},
	{0xc9, 0x00},
	{0xca, 0x00},
	{0xcb, 0x00},
	{0xa4, 0x00},
	{0xa5, 0x00},
	{0xa6, 0x00},
	{0xa7, 0x00},

	////////////////////asde ///////////////////
	//{0xa0, 0xaf},
	//{0xa2, 0xff},

	////////////20120427/////////////               
	{0xfe, 0x01}, 		
	{0x18, 0x22}, 		
	{0x21, 0xc0}, 		
	{0x06, 0x12},		
	{0x08, 0x9c},		
	{0x51, 0x28},		
	{0x52, 0x10},		
	{0x53, 0x20},		
	{0x54, 0x40},		
	{0x55, 0x16},		
	{0x56, 0x30},		
	{0x58, 0x60},		
	{0x59, 0x08},		
	{0x5c, 0x35},		
	{0x5d, 0x72},		
	{0x67, 0x80},		
	{0x68, 0x60},		
	{0x69, 0x90},		
	{0x6c, 0x30},		
	{0x6d, 0x60},		
	{0x70, 0x10},	        

	{0xfe, 0x00},		
	{0x9c, 0x0a},		
	{0xa0, 0xaf},		
	{0xa2, 0xff},		
	{0xa4, 0x60},		
	{0xa5, 0x31},		
	{0xa7, 0x35},		
	{0x42, 0xfe},		
	{0xd1, 0x30},// 34		
	{0xd2, 0x30},// 34
	{0xfe, 0x00},	
     ///////////// /////////////  /////////////       
	{0x44, 0xa3},// a2

	{0x05, 0x02}, 	
	{0x06, 0x2c}, 
	{0x07, 0x00},
	{0x08, 0xb8},

	{0xfe, 0x01},
	{0x33, 0x20},
	{0x29, 0x00},   //anti-flicker step [11:8]
	{0x2a, 0x60},   //anti-flicker step [7:0]

	{0x2b, 0x02},   //exp level 0  14.28fps
	{0x2c, 0xa0}, 
	{0x2d, 0x03},   //exp level 1  12.50fps
	{0x2e, 0x00}, 
	{0x2f, 0x03},   //exp level 2  10.00fps
	{0x30, 0xc0}, 
	{0x31, 0x05},   //exp level 3  7.14fps
	{0x32, 0x40}, 
	{0xfe, 0x00},


	/*
	{0x05, 0x03}, 	
	{0x06, 0x26}, 
	{0x07, 0x00},
	{0x08, 0x48},

	{0xfe , 0x01},
	{0x29, 0x00},   //anti-flicker step [11:8]
	{0x2a, 0x50},   //anti-flicker step [7:0]

	{0x2b, 0x02},   //exp level 0  14.28fps
	{0x2c, 0x30}, 
	{0x2d, 0x02},   //exp level 1  12.50fps
	{0x2e, 0x80}, 
	{0x2f, 0x03},   //exp level 2  10.00fps
	{0x30, 0x20}, 
	{0x31, 0x06},   //exp level 3  7.14fps
	{0x32, 0x40}, 
	{0xfe, 0x00},
	*/
	{0xfa, 0x00},
	{0xfa, 0x00},
	{0xfa, 0x00},

	{0xd3, 0x3d},


	{0xff,0xff},
};


void GC0329_init_regs(struct gc0329_device *dev)
{
	int i=0;//,j;
	unsigned char buf[2];
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
	while(1)
	{
		buf[0] = GC0329_script[i].addr;
		buf[1] = GC0329_script[i].val;
		if(GC0329_script[i].val==0xff&&GC0329_script[i].addr==0xff){
			printk("GC0329_write_regs success in initial GC0329.\n");
			break;
		}
		if((i2c_put_byte_add8(client,buf, 2)) < 0){
			printk("fail in initial GC0329. \n");
			return;
		}
		i++;
	}
	
	return;
}

static struct aml_camera_i2c_fig1_s resolution_320x240_script[] = {

	{0x59, 0x22},
	{0x5a, 0x0e},
	{0x5b, 0x00},
	{0x5c, 0x00},
	{0x5d, 0x00},
	{0x5e, 0x00},
	{0x5f, 0x00},
	{0x60, 0x00},
	{0x61, 0x00},
	{0x62, 0x00},
                
	{0xff, 0xff}

};

static struct aml_camera_i2c_fig1_s resolution_640x480_script[] = {

	{0x59, 0x11},
	{0x5a, 0x0e},
	{0x5b, 0x02},
	{0x5c, 0x04},
	{0x5d, 0x00},
	{0x5e, 0x00},
	{0x5f, 0x02},
	{0x60, 0x04},
	{0x61, 0x00},
	{0x62, 0x00},
                
	{0xff, 0xff}

};

static int set_flip(struct gc0329_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp = 0;
	unsigned char buf[2];
	buf[0]=0xfe;
	buf[1]=0x00;
	i2c_put_byte_add8(client, buf, 2);
	
	temp = i2c_get_byte_add8(client, 0x17);
	temp &= 0xfc;
	temp |= dev->cam_info.m_flip << 0;
	temp |= dev->cam_info.v_flip << 1;
	buf[0] = 0x17;
	buf[1] = temp;
	if((i2c_put_byte_add8(client, buf, 2)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
	}
	return 0;
}

static void gc0329_set_resolution(struct gc0329_device *dev,int height,int width)
{
	int i=0;
	unsigned char buf[2];
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	struct aml_camera_i2c_fig1_s* resolution_script;
	if (height >= 480) {
		printk("set resolution 640X480\n");
		resolution_script = resolution_640x480_script;
		gc0329_frmintervals_active.denominator 	= 15;
		gc0329_frmintervals_active.numerator	= 1;
		gc0329_h_active = 640;
		gc0329_v_active = 478;
		//GC0329_init_regs(dev);
		//return;
	} else {
		printk("set resolution 320X240\n");
		gc0329_frmintervals_active.denominator 	= 15;
		gc0329_frmintervals_active.numerator	= 1;
		gc0329_h_active = 320;
		gc0329_v_active = 238;
		resolution_script = resolution_320x240_script;
	}
	
	while(1) {
		buf[0] = resolution_script[i].addr;
		buf[1] = resolution_script[i].val;
		if(resolution_script[i].val==0xff&&resolution_script[i].addr==0xff) {
			break;
		}
		if((i2c_put_byte_add8(client,buf, 2)) < 0) {
			printk("fail in setting resolution \n");
			return;
		}
		i++;
	}
	set_flip(dev);
}



/*************************************************************************
* FUNCTION
*   GC0329AwbEnable
*
* DESCRIPTION
*   disable/enable awb
*
* PARAMETERS
*   Enable
*
* RETURNS
*   None
*
* LOCAL AFFECTED
*
*************************************************************************/

static void GC0329AwbEnable(struct gc0329_device *dev, int Enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	if (GC0329Sensor.BypassAwb)
	{
		Enable = 0;
	}
	// TODO: enable or disable AWB here
	{
		unsigned char temp = i2c_get_byte_add8(client,0x42);
		if (Enable)
		{
			i2c_put_byte_add8_new(client,0x42, (temp | 0x02));
		}
		else
		{
			i2c_put_byte_add8_new(client,0x42, (temp & (~0x02)));
		}
	}
}




/*************************************************************************
* FUNCTION
*	GC0329GammaSelect
*
* DESCRIPTION
*	This function is served for FAE to select the appropriate GAMMA curve.
*
* PARAMETERS
*	None
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
void GC0329GammaSelect(struct gc0329_device *dev, GC0329_GAMMA_TAG GammaLvl)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	switch(GammaLvl) {
	case GC0329_RGB_Gamma_m1:						//smallest gamma curve
		i2c_put_byte_add8_new(client, (unsigned char)0xfe, (unsigned char)0x00);
		i2c_put_byte_add8_new(client, (unsigned char)0xbf, (unsigned char)0x06);
		i2c_put_byte_add8_new(client, (unsigned char)0xc0, (unsigned char)0x12);
		i2c_put_byte_add8_new(client, (unsigned char)0xc1, (unsigned char)0x22);
		i2c_put_byte_add8_new(client, (unsigned char)0xc2, (unsigned char)0x35);
		i2c_put_byte_add8_new(client, (unsigned char)0xc3, (unsigned char)0x4b);
		i2c_put_byte_add8_new(client, (unsigned char)0xc4, (unsigned char)0x5f);
		i2c_put_byte_add8_new(client, (unsigned char)0xc5, (unsigned char)0x72);
		i2c_put_byte_add8_new(client, (unsigned char)0xc6, (unsigned char)0x8d);
		i2c_put_byte_add8_new(client, (unsigned char)0xc7, (unsigned char)0xa4);
		i2c_put_byte_add8_new(client, (unsigned char)0xc8, (unsigned char)0xb8);
		i2c_put_byte_add8_new(client, (unsigned char)0xc9, (unsigned char)0xc8);
		i2c_put_byte_add8_new(client, (unsigned char)0xca, (unsigned char)0xd4);
		i2c_put_byte_add8_new(client, (unsigned char)0xcb, (unsigned char)0xde);
		i2c_put_byte_add8_new(client, (unsigned char)0xcc, (unsigned char)0xe6);
		i2c_put_byte_add8_new(client, (unsigned char)0xcd, (unsigned char)0xf1);
		i2c_put_byte_add8_new(client, (unsigned char)0xce, (unsigned char)0xf8);
		i2c_put_byte_add8_new(client, (unsigned char)0xcf, (unsigned char)0xfd);
		break;
	case GC0329_RGB_Gamma_m2:
		i2c_put_byte_add8_new(client,(unsigned char)0xBF, (unsigned char)0x08);
		i2c_put_byte_add8_new(client,(unsigned char)0xc0, (unsigned char)0x0F);
		i2c_put_byte_add8_new(client,(unsigned char)0xc1, (unsigned char)0x21);
		i2c_put_byte_add8_new(client,(unsigned char)0xc2, (unsigned char)0x32);
		i2c_put_byte_add8_new(client,(unsigned char)0xc3, (unsigned char)0x43);
		i2c_put_byte_add8_new(client,(unsigned char)0xc4, (unsigned char)0x50);
		i2c_put_byte_add8_new(client,(unsigned char)0xc5, (unsigned char)0x5E);
		i2c_put_byte_add8_new(client,(unsigned char)0xc6, (unsigned char)0x78);
		i2c_put_byte_add8_new(client,(unsigned char)0xc7, (unsigned char)0x90);
		i2c_put_byte_add8_new(client,(unsigned char)0xc8, (unsigned char)0xA6);
		i2c_put_byte_add8_new(client,(unsigned char)0xc9, (unsigned char)0xB9);
		i2c_put_byte_add8_new(client,(unsigned char)0xcA, (unsigned char)0xC9);
		i2c_put_byte_add8_new(client,(unsigned char)0xcB, (unsigned char)0xD6);
		i2c_put_byte_add8_new(client,(unsigned char)0xcC, (unsigned char)0xE0);
		i2c_put_byte_add8_new(client,(unsigned char)0xcD, (unsigned char)0xEE);
		i2c_put_byte_add8_new(client,(unsigned char)0xcE, (unsigned char)0xF8);
		i2c_put_byte_add8_new(client,(unsigned char)0xcF, (unsigned char)0xFF);
		break;               
		
	case GC0329_RGB_Gamma_m3:			
		i2c_put_byte_add8_new(client, (unsigned char)0xBF, (unsigned char)0x0B);
		i2c_put_byte_add8_new(client, (unsigned char)0xc0, (unsigned char)0x16);
		i2c_put_byte_add8_new(client, (unsigned char)0xc1, (unsigned char)0x29);
		i2c_put_byte_add8_new(client, (unsigned char)0xc2, (unsigned char)0x3C);
		i2c_put_byte_add8_new(client, (unsigned char)0xc3, (unsigned char)0x4F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc4, (unsigned char)0x5F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc5, (unsigned char)0x6F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc6, (unsigned char)0x8A);
		i2c_put_byte_add8_new(client, (unsigned char)0xc7, (unsigned char)0x9F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc8, (unsigned char)0xB4);
		i2c_put_byte_add8_new(client, (unsigned char)0xc9, (unsigned char)0xC6);
		i2c_put_byte_add8_new(client, (unsigned char)0xcA, (unsigned char)0xD3);
		i2c_put_byte_add8_new(client, (unsigned char)0xcB, (unsigned char)0xDD);
		i2c_put_byte_add8_new(client, (unsigned char)0xcC, (unsigned char)0xE5);
		i2c_put_byte_add8_new(client, (unsigned char)0xcD, (unsigned char)0xF1);
		i2c_put_byte_add8_new(client, (unsigned char)0xcE, (unsigned char)0xFA);
		i2c_put_byte_add8_new(client, (unsigned char)0xcF, (unsigned char)0xFF);
		break;                    
		
	case GC0329_RGB_Gamma_m4:
		i2c_put_byte_add8_new(client, (unsigned char)0xBF, (unsigned char)0x0E);
		i2c_put_byte_add8_new(client, (unsigned char)0xc0, (unsigned char)0x1C);
		i2c_put_byte_add8_new(client, (unsigned char)0xc1, (unsigned char)0x34);
		i2c_put_byte_add8_new(client, (unsigned char)0xc2, (unsigned char)0x48);
		i2c_put_byte_add8_new(client, (unsigned char)0xc3, (unsigned char)0x5A);
		i2c_put_byte_add8_new(client, (unsigned char)0xc4, (unsigned char)0x6B);
		i2c_put_byte_add8_new(client, (unsigned char)0xc5, (unsigned char)0x7B);
		i2c_put_byte_add8_new(client, (unsigned char)0xc6, (unsigned char)0x95);
		i2c_put_byte_add8_new(client, (unsigned char)0xc7, (unsigned char)0xAB);
		i2c_put_byte_add8_new(client, (unsigned char)0xc8, (unsigned char)0xBF);
		i2c_put_byte_add8_new(client, (unsigned char)0xc9, (unsigned char)0xCE);
		i2c_put_byte_add8_new(client, (unsigned char)0xcA, (unsigned char)0xD9);
		i2c_put_byte_add8_new(client, (unsigned char)0xcB, (unsigned char)0xE4);
		i2c_put_byte_add8_new(client, (unsigned char)0xcC, (unsigned char)0xEC);
		i2c_put_byte_add8_new(client, (unsigned char)0xcD, (unsigned char)0xF7);
		i2c_put_byte_add8_new(client, (unsigned char)0xcE, (unsigned char)0xFD);
		i2c_put_byte_add8_new(client, (unsigned char)0xcF, (unsigned char)0xFF);
		break;               
		
	case GC0329_RGB_Gamma_m5:
		i2c_put_byte_add8_new(client, (unsigned char)0xBF, (unsigned char)0x10);
		i2c_put_byte_add8_new(client, (unsigned char)0xc0, (unsigned char)0x20);
		i2c_put_byte_add8_new(client, (unsigned char)0xc1, (unsigned char)0x38);
		i2c_put_byte_add8_new(client, (unsigned char)0xc2, (unsigned char)0x4E);
		i2c_put_byte_add8_new(client, (unsigned char)0xc3, (unsigned char)0x63);
		i2c_put_byte_add8_new(client, (unsigned char)0xc4, (unsigned char)0x76);
		i2c_put_byte_add8_new(client, (unsigned char)0xc5, (unsigned char)0x87);
		i2c_put_byte_add8_new(client, (unsigned char)0xc6, (unsigned char)0xA2);
		i2c_put_byte_add8_new(client, (unsigned char)0xc7, (unsigned char)0xB8);
		i2c_put_byte_add8_new(client, (unsigned char)0xc8, (unsigned char)0xCA);
		i2c_put_byte_add8_new(client, (unsigned char)0xc9, (unsigned char)0xD8);
		i2c_put_byte_add8_new(client, (unsigned char)0xcA, (unsigned char)0xE3);
		i2c_put_byte_add8_new(client, (unsigned char)0xcB, (unsigned char)0xEB);
		i2c_put_byte_add8_new(client, (unsigned char)0xcC, (unsigned char)0xF0);
		i2c_put_byte_add8_new(client, (unsigned char)0xcD, (unsigned char)0xF8);
		i2c_put_byte_add8_new(client, (unsigned char)0xcE, (unsigned char)0xFD);
		i2c_put_byte_add8_new(client, (unsigned char)0xcF, (unsigned char)0xFF);
		break;                   
		
	case GC0329_RGB_Gamma_m6:										// largest gamma curve
		i2c_put_byte_add8_new(client, (unsigned char)0xBF, (unsigned char)0x14);
		i2c_put_byte_add8_new(client, (unsigned char)0xc0, (unsigned char)0x28);
		i2c_put_byte_add8_new(client, (unsigned char)0xc1, (unsigned char)0x44);
		i2c_put_byte_add8_new(client, (unsigned char)0xc2, (unsigned char)0x5D);
		i2c_put_byte_add8_new(client, (unsigned char)0xc3, (unsigned char)0x72);
		i2c_put_byte_add8_new(client, (unsigned char)0xc4, (unsigned char)0x86);
		i2c_put_byte_add8_new(client, (unsigned char)0xc5, (unsigned char)0x95);
		i2c_put_byte_add8_new(client, (unsigned char)0xc6, (unsigned char)0xB1);
		i2c_put_byte_add8_new(client, (unsigned char)0xc7, (unsigned char)0xC6);
		i2c_put_byte_add8_new(client, (unsigned char)0xc8, (unsigned char)0xD5);
		i2c_put_byte_add8_new(client, (unsigned char)0xc9, (unsigned char)0xE1);
		i2c_put_byte_add8_new(client, (unsigned char)0xcA, (unsigned char)0xEA);
		i2c_put_byte_add8_new(client, (unsigned char)0xcB, (unsigned char)0xF1);
		i2c_put_byte_add8_new(client, (unsigned char)0xcC, (unsigned char)0xF5);
		i2c_put_byte_add8_new(client, (unsigned char)0xcD, (unsigned char)0xFB);
		i2c_put_byte_add8_new(client, (unsigned char)0xcE, (unsigned char)0xFE);
		i2c_put_byte_add8_new(client, (unsigned char)0xcF, (unsigned char)0xFF);
		break;                 
	case GC0329_RGB_Gamma_night:									//Gamma for night mode
		i2c_put_byte_add8_new(client, (unsigned char)0xBF, (unsigned char)0x0B);
		i2c_put_byte_add8_new(client, (unsigned char)0xc0, (unsigned char)0x16);
		i2c_put_byte_add8_new(client, (unsigned char)0xc1, (unsigned char)0x29);
		i2c_put_byte_add8_new(client, (unsigned char)0xc2, (unsigned char)0x3C);
		i2c_put_byte_add8_new(client, (unsigned char)0xc3, (unsigned char)0x4F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc4, (unsigned char)0x5F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc5, (unsigned char)0x6F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc6, (unsigned char)0x8A);
		i2c_put_byte_add8_new(client, (unsigned char)0xc7, (unsigned char)0x9F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc8, (unsigned char)0xB4);
		i2c_put_byte_add8_new(client, (unsigned char)0xc9, (unsigned char)0xC6);
		i2c_put_byte_add8_new(client, (unsigned char)0xcA, (unsigned char)0xD3);
		i2c_put_byte_add8_new(client, (unsigned char)0xcB, (unsigned char)0xDD);
		i2c_put_byte_add8_new(client, (unsigned char)0xcC, (unsigned char)0xE5);
		i2c_put_byte_add8_new(client, (unsigned char)0xcD, (unsigned char)0xF1);
		i2c_put_byte_add8_new(client, (unsigned char)0xcE, (unsigned char)0xFA);
		i2c_put_byte_add8_new(client, (unsigned char)0xcF, (unsigned char)0xFF);
		break;                   
	default:/*
		//GC0329_RGB_Gamma_m1
		i2c_put_byte_add8_new(client, (unsigned char)0xfe, (unsigned char)0x00);
		i2c_put_byte_add8_new(client, (unsigned char)0xbf, (unsigned char)0x06);
		i2c_put_byte_add8_new(client, (unsigned char)0xc0, (unsigned char)0x12);
		i2c_put_byte_add8_new(client, (unsigned char)0xc1, (unsigned char)0x22);
		i2c_put_byte_add8_new(client, (unsigned char)0xc2, (unsigned char)0x35);
		i2c_put_byte_add8_new(client, (unsigned char)0xc3, (unsigned char)0x4b);
		i2c_put_byte_add8_new(client, (unsigned char)0xc4, (unsigned char)0x5f);
		i2c_put_byte_add8_new(client, (unsigned char)0xc5, (unsigned char)0x72);
		i2c_put_byte_add8_new(client, (unsigned char)0xc6, (unsigned char)0x8d);
		i2c_put_byte_add8_new(client, (unsigned char)0xc7, (unsigned char)0xa4);
		i2c_put_byte_add8_new(client, (unsigned char)0xc8, (unsigned char)0xb8);
		i2c_put_byte_add8_new(client, (unsigned char)0xc9, (unsigned char)0xc8);
		i2c_put_byte_add8_new(client, (unsigned char)0xca, (unsigned char)0xd4);
		i2c_put_byte_add8_new(client, (unsigned char)0xcb, (unsigned char)0xde);
		i2c_put_byte_add8_new(client, (unsigned char)0xcc, (unsigned char)0xe6);
		i2c_put_byte_add8_new(client, (unsigned char)0xcd, (unsigned char)0xf1);
		i2c_put_byte_add8_new(client, (unsigned char)0xce, (unsigned char)0xf8);
		i2c_put_byte_add8_new(client, (unsigned char)0xcf, (unsigned char)0xfd);
		*/
		// GC0329_RGB_Gamma_m3:			
		/*
		i2c_put_byte_add8_new(client, (unsigned char)0xBF, (unsigned char)0x0B);
		i2c_put_byte_add8_new(client, (unsigned char)0xc0, (unsigned char)0x16);
		i2c_put_byte_add8_new(client, (unsigned char)0xc1, (unsigned char)0x29);
		i2c_put_byte_add8_new(client, (unsigned char)0xc2, (unsigned char)0x3C);
		i2c_put_byte_add8_new(client, (unsigned char)0xc3, (unsigned char)0x4F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc4, (unsigned char)0x5F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc5, (unsigned char)0x6F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc6, (unsigned char)0x8A);
		i2c_put_byte_add8_new(client, (unsigned char)0xc7, (unsigned char)0x9F);
		i2c_put_byte_add8_new(client, (unsigned char)0xc8, (unsigned char)0xB4);
		i2c_put_byte_add8_new(client, (unsigned char)0xc9, (unsigned char)0xC6);
		i2c_put_byte_add8_new(client, (unsigned char)0xcA, (unsigned char)0xD3);
		i2c_put_byte_add8_new(client, (unsigned char)0xcB, (unsigned char)0xDD);
		i2c_put_byte_add8_new(client, (unsigned char)0xcC, (unsigned char)0xE5);
		i2c_put_byte_add8_new(client, (unsigned char)0xcD, (unsigned char)0xF1);
		i2c_put_byte_add8_new(client, (unsigned char)0xcE, (unsigned char)0xFA);
		i2c_put_byte_add8_new(client, (unsigned char)0xcF, (unsigned char)0xFF);
		*/
		
		i2c_put_byte_add8_new(client, (unsigned char)0xbf, (unsigned char)0x06);
		i2c_put_byte_add8_new(client, (unsigned char)0xc0, (unsigned char)0x14);
		i2c_put_byte_add8_new(client, (unsigned char)0xc1, (unsigned char)0x27);
		i2c_put_byte_add8_new(client, (unsigned char)0xc2, (unsigned char)0x3b);
		i2c_put_byte_add8_new(client, (unsigned char)0xc3, (unsigned char)0x4f);
		i2c_put_byte_add8_new(client, (unsigned char)0xc4, (unsigned char)0x62);
		i2c_put_byte_add8_new(client, (unsigned char)0xc5, (unsigned char)0x72);
		i2c_put_byte_add8_new(client, (unsigned char)0xc6, (unsigned char)0x8d);
		i2c_put_byte_add8_new(client, (unsigned char)0xc7, (unsigned char)0xa4);
		i2c_put_byte_add8_new(client, (unsigned char)0xc8, (unsigned char)0xb8);
		i2c_put_byte_add8_new(client, (unsigned char)0xc9, (unsigned char)0xc9);
		i2c_put_byte_add8_new(client, (unsigned char)0xcA, (unsigned char)0xd6);
		i2c_put_byte_add8_new(client, (unsigned char)0xcB, (unsigned char)0xe0);
		i2c_put_byte_add8_new(client, (unsigned char)0xcC, (unsigned char)0xe8);
		i2c_put_byte_add8_new(client, (unsigned char)0xcD, (unsigned char)0xf4);
		i2c_put_byte_add8_new(client, (unsigned char)0xcE, (unsigned char)0xFc);
		i2c_put_byte_add8_new(client, (unsigned char)0xcF, (unsigned char)0xFF);
			
		break;
	}
}

void GC0329write_more_registers(struct gc0329_device *dev)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);	
	GC0329GammaSelect(dev,0);// GC0329_RGB_Gamma_m3);	
}

/*************************************************************************
* FUNCTION
*	set_GC0329_param_wb
*
* DESCRIPTION
*	GC0329 wb setting.
*
* PARAMETERS
*	none
*
* RETURNS
*	None
*
* GLOBALS AFFECTED  白平衡参数
*
*************************************************************************/
void set_GC0329_param_wb(struct gc0329_device *dev,enum  camera_wb_flip_e para)
{
//	uint16 rgain=0x80, ggain=0x80, bgain=0x80;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	printk(" camera set_GC0329_param_wb=%d. \n ",para);
	switch (para) {
	case CAM_WB_AUTO:
		i2c_put_byte_add8_new(client, (unsigned char)0x77, (unsigned char)0x57);
		i2c_put_byte_add8_new(client, (unsigned char)0x78, (unsigned char)0x4d);
		i2c_put_byte_add8_new(client, (unsigned char)0x79, (unsigned char)0x45);
		GC0329AwbEnable(dev, 1);
		break;

	case CAM_WB_CLOUD:
		GC0329AwbEnable(dev, 0);
		i2c_put_byte_add8_new(client, (unsigned char)0x77, (unsigned char)0x8c);
		i2c_put_byte_add8_new(client, (unsigned char)0x78, (unsigned char)0x50);
		i2c_put_byte_add8_new(client, (unsigned char)0x79, (unsigned char)0x40);
		break;

	case CAM_WB_DAYLIGHT:   // tai yang guang
		GC0329AwbEnable(dev, 0);
		i2c_put_byte_add8_new(client, (unsigned char)0x77, (unsigned char)0x74); 
		i2c_put_byte_add8_new(client, (unsigned char)0x78, (unsigned char)0x52);
		i2c_put_byte_add8_new(client, (unsigned char)0x79, (unsigned char)0x40); 
		break;

	case CAM_WB_INCANDESCENCE:   // bai re guang
		GC0329AwbEnable(dev, 0);
		i2c_put_byte_add8_new(client, (unsigned char)0x77, (unsigned char)0x48);
		i2c_put_byte_add8_new(client, (unsigned char)0x78, (unsigned char)0x40);
		i2c_put_byte_add8_new(client, (unsigned char)0x79, (unsigned char)0x5c);
		break;

	case CAM_WB_FLUORESCENT:   //ri guang deng
		GC0329AwbEnable(dev, 0);
		i2c_put_byte_add8_new(client, (unsigned char)0x77, (unsigned char)0x40);
		i2c_put_byte_add8_new(client, (unsigned char)0x78, (unsigned char)0x42);
		i2c_put_byte_add8_new(client, (unsigned char)0x79, (unsigned char)0x50);
		break;

	case CAM_WB_TUNGSTEN:   // wu si deng
		GC0329AwbEnable(dev, 0);
		i2c_put_byte_add8_new(client, (unsigned char)0x77, (unsigned char)0x40);
		i2c_put_byte_add8_new(client, (unsigned char)0x78, (unsigned char)0x54);
		i2c_put_byte_add8_new(client, (unsigned char)0x79, (unsigned char)0x70);
		break;

	case CAM_WB_MANUAL:
		// TODO
		break;
	default:
		break;
	}
//	sleep_task(20);
}

/*************************************************************************
* FUNCTION
*	GC0329_night_mode
*
* DESCRIPTION
*	This function night mode of GC0329.
*
* PARAMETERS
*	none
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
void GC0329_night_mode(struct gc0329_device *dev,enum  camera_night_mode_flip_e enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	GC0329Sensor.NightMode = (bool)enable;

	if (enable) {
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		if(GC0329Sensor.VideoMode)
			i2c_put_byte_add8_new(client,0x33, 0x00);
		else
			i2c_put_byte_add8_new(client,0x33, 0x30);
		i2c_put_byte_add8_new(client,0xfe, 0x00);
		GC0329GammaSelect(dev,0);// GC0329_RGB_Gamma_night);
	
	} else {
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		if(GC0329Sensor.VideoMode)
			i2c_put_byte_add8_new(client,0x33, 0x00);
		else
			i2c_put_byte_add8_new(client,0x33, 0x20);
		i2c_put_byte_add8_new(client,0xfe, 0x00);
		GC0329GammaSelect(dev, GC0329_RGB_Gamma_m3); 	
	
	}
	
}
/*************************************************************************
* FUNCTION
*	GC0329_night_mode
*
* DESCRIPTION
*	This function night mode of GC0329.
*
* PARAMETERS
*	none
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/

void GC0329_set_param_banding(struct gc0329_device *dev,enum  camera_banding_flip_e banding)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(banding){
        case CAM_BANDING_60HZ:
		i2c_put_byte_add8_new(client,0x05, 0x02); 	
		i2c_put_byte_add8_new(client,0x06, 0x4c); 
		i2c_put_byte_add8_new(client,0x07, 0x00);
		i2c_put_byte_add8_new(client,0x08, 0x88);
		
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		i2c_put_byte_add8_new(client,0x29, 0x00);   //anti-flicker step [11:8]
		i2c_put_byte_add8_new(client,0x2a, 0x4e);   //anti-flicker step [7:0]
		
		i2c_put_byte_add8_new(client,0x2b, 0x02);   //exp level 0  15.00fps
		i2c_put_byte_add8_new(client,0x2c, 0x70); 
		i2c_put_byte_add8_new(client,0x2d, 0x03);   //exp level 0  12.00fps
		i2c_put_byte_add8_new(client,0x2e, 0x0c); 
		i2c_put_byte_add8_new(client,0x2f, 0x03);   //exp level 0  10.00fps
		i2c_put_byte_add8_new(client,0x30, 0xa8); 
		i2c_put_byte_add8_new(client,0x31, 0x05);   //exp level 0  7.05fps
		i2c_put_byte_add8_new(client,0x32, 0x2e); 
		i2c_put_byte_add8_new(client,0xfe, 0x00);

		break;
        case CAM_BANDING_50HZ:



		i2c_put_byte_add8_new(client,0x05, 0x02); 	
		i2c_put_byte_add8_new(client,0x06, 0x2c); 
		i2c_put_byte_add8_new(client,0x07, 0x00);
		i2c_put_byte_add8_new(client,0x08, 0xb8);
		
		i2c_put_byte_add8_new(client, 0xfe, 0x01);
		i2c_put_byte_add8_new(client,0x29, 0x00);   //anti-flicker step [11:8]
		i2c_put_byte_add8_new(client,0x2a, 0x60);   //anti-flicker step [7:0]
		
		i2c_put_byte_add8_new(client,0x2b, 0x02);   //exp level 0  14.28fps
		i2c_put_byte_add8_new(client,0x2c, 0xa0); 
		i2c_put_byte_add8_new(client,0x2d, 0x03);   //exp level 1  12.50fps
		i2c_put_byte_add8_new(client,0x2e, 0x00); 
		i2c_put_byte_add8_new(client,0x2f, 0x03);   //exp level 2  10.00fps
		i2c_put_byte_add8_new(client,0x30, 0xc0); 
		i2c_put_byte_add8_new(client,0x31, 0x05);   //exp level 3  7.14fps
		i2c_put_byte_add8_new(client,0x32, 0x40); 
		i2c_put_byte_add8_new(client,0xfe, 0x00);
/*
			i2c_put_byte_add8_new(client,0x05, 0x03); 	
			i2c_put_byte_add8_new(client,0x06, 0x26); 
			i2c_put_byte_add8_new(client,0x07, 0x00);
			i2c_put_byte_add8_new(client,0x08, 0x48);

			i2c_put_byte_add8_new(client, 0xfe, 0x01);
			i2c_put_byte_add8_new(client,0x29, 0x00);   //anti-flicker step [11:8]
			i2c_put_byte_add8_new(client,0x2a, 0x50);   //anti-flicker step [7:0]
			
			i2c_put_byte_add8_new(client,0x2b, 0x02);   //exp level 0  14.28fps
			i2c_put_byte_add8_new(client,0x2c, 0x30); 
			i2c_put_byte_add8_new(client,0x2d, 0x02);   //exp level 1  12.50fps
			i2c_put_byte_add8_new(client,0x2e, 0x80); 
			i2c_put_byte_add8_new(client,0x2f, 0x03);   //exp level 2  10.00fps
			i2c_put_byte_add8_new(client,0x30, 0x20); 
			i2c_put_byte_add8_new(client,0x31, 0x06);   //exp level 3  7.14fps
			i2c_put_byte_add8_new(client,0x32, 0x40); 
			i2c_put_byte_add8_new(client,0xfe, 0x00);
			*/
		break;
        default:
		break;
	}
}


/*************************************************************************
* FUNCTION
*	set_GC0329_param_exposure
*
* DESCRIPTION
*	GC0329 exposure setting.
*
* PARAMETERS
*	none
*
* RETURNS
*	None
*
* GLOBALS AFFECTED  亮度等级 调节参数
*
*************************************************************************/
void set_GC0329_param_exposure(struct gc0329_device *dev,enum camera_exposure_e para)//曝光调节
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);


	unsigned char value_luma, value_Y;
	/*switch night or normal mode*/
	value_luma = (GC0329Sensor.NightMode?0x2b:0x08);
	value_Y = (GC0329Sensor.NightMode?0x68:0x58);
	switch (para) {
	case EXPOSURE_N4_STEP:
			i2c_put_byte_add8_new(client,0xd5, value_luma - 0x48);
			i2c_put_byte_add8_new(client,0xfe, 0x01);
			i2c_put_byte_add8_new(client,0x13, value_Y - 0x28);
			i2c_put_byte_add8_new(client,0xfe, 0x00);
		break;
	case EXPOSURE_N3_STEP:
		i2c_put_byte_add8_new(client,0xd5, value_luma - 0x30);
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		i2c_put_byte_add8_new(client,0x13, value_Y - 0x18);
		i2c_put_byte_add8_new(client,0xfe, 0x00);
		break;
	case EXPOSURE_N2_STEP:
		i2c_put_byte_add8_new(client,0xd5, value_luma - 0x20);
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		i2c_put_byte_add8_new(client,0x13, value_Y - 0x10);
		i2c_put_byte_add8_new(client,0xfe, 0x00);
		break;
	case EXPOSURE_N1_STEP:
		i2c_put_byte_add8_new(client,0xd5, value_luma - 0x10);
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		i2c_put_byte_add8_new(client,0x13, value_Y - 0x08);
		i2c_put_byte_add8_new(client,0xfe, 0x00);
		break;
	case EXPOSURE_0_STEP:
		i2c_put_byte_add8_new(client,0xd5, value_luma);
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		i2c_put_byte_add8_new(client,0x13, value_Y);
		i2c_put_byte_add8_new(client,0xfe, 0x00);
		break;
	case EXPOSURE_P1_STEP:
		i2c_put_byte_add8_new(client,0xd5, value_luma + 0x10);
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		i2c_put_byte_add8_new(client,0x13, value_Y + 0x10);
		i2c_put_byte_add8_new(client,0xfe, 0x00);
		break;
	case EXPOSURE_P2_STEP:
		i2c_put_byte_add8_new(client,0xd5, value_luma + 0x20);
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		i2c_put_byte_add8_new(client,0x13, value_Y + 0x20);
		i2c_put_byte_add8_new(client,0xfe, 0x00);
		break;
	case EXPOSURE_P3_STEP:
		i2c_put_byte_add8_new(client,0xd5, value_luma + 0x30);
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		i2c_put_byte_add8_new(client,0x13, value_Y + 0x30);
		i2c_put_byte_add8_new(client,0xfe, 0x00);
		break;
	case EXPOSURE_P4_STEP:
		i2c_put_byte_add8_new(client,0xd5, value_luma + 0x40);
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		i2c_put_byte_add8_new(client,0x13, value_Y + 0x40);
		i2c_put_byte_add8_new(client,0xfe, 0x00);
		break;
	default:
		i2c_put_byte_add8_new(client,0xd5, value_luma);
		i2c_put_byte_add8_new(client,0xfe, 0x01);
		i2c_put_byte_add8_new(client,0x13, value_Y);
		i2c_put_byte_add8_new(client,0xfe, 0x00);
		break;
	}
	//msleep(300);
}

/*************************************************************************
* FUNCTION
*	set_GC0329_param_effect
*
* DESCRIPTION
*	GC0329 effect setting.
*
* PARAMETERS
*	none
*
* RETURNS
*	None
*
* GLOBALS AFFECTED  特效参数
*
*************************************************************************/
void set_GC0329_param_effect(struct gc0329_device *dev,enum camera_effect_flip_e para)//特效设置
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch (para) {

	case CAM_EFFECT_ENC_NORMAL:
		i2c_put_byte_add8_new(client,0x43 , 0x00);

		break;
	case CAM_EFFECT_ENC_GRAYSCALE:
		i2c_put_byte_add8_new(client,0x43 , 0x02);
		i2c_put_byte_add8_new(client,0xda , 0x00);
		i2c_put_byte_add8_new(client,0xdb , 0x00);
		break;
	case CAM_EFFECT_ENC_SEPIA:
		i2c_put_byte_add8_new(client,0x43 , 0x02);
		i2c_put_byte_add8_new(client,0xda , 0xd0);
		i2c_put_byte_add8_new(client,0xdb , 0x28);

		break;
	case CAM_EFFECT_ENC_COLORINV:
		i2c_put_byte_add8_new(client,0x43 , 0x01);

		break;
	case CAM_EFFECT_ENC_SEPIAGREEN:
		i2c_put_byte_add8_new(client,0x43 , 0x02);
		i2c_put_byte_add8_new(client,0xda , 0xc0);
		i2c_put_byte_add8_new(client,0xdb , 0xc0);

		break;
	case CAM_EFFECT_ENC_SEPIABLUE:
		i2c_put_byte_add8_new(client,0x43 , 0x02);
		i2c_put_byte_add8_new(client,0xda , 0x50);
		i2c_put_byte_add8_new(client,0xdb , 0xe0);
		break;
	default:
		break;
	}

}

unsigned char v4l_2_gc0329(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int gc0329_setting(struct gc0329_device *dev,int PROP_ID,int value )
{
	int ret=0;
	//unsigned char cur_val;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_DO_WHITE_BALANCE:
		if(gc0329_qctrl[0].default_value!=value){
			gc0329_qctrl[0].default_value=value;
			set_GC0329_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
		}
		break;
	case V4L2_CID_EXPOSURE:
		if(gc0329_qctrl[1].default_value!=value){
			gc0329_qctrl[1].default_value=value;
			set_GC0329_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
		}
		break;
	case V4L2_CID_COLORFX:
		if(gc0329_qctrl[2].default_value!=value){
			gc0329_qctrl[2].default_value=value;
			set_GC0329_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
		}
		break;
	case V4L2_CID_WHITENESS:
		if(gc0329_qctrl[3].default_value!=value){
			gc0329_qctrl[3].default_value=value;
			GC0329_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		break;
	case V4L2_CID_BLUE_BALANCE:
		if(gc0329_qctrl[4].default_value!=value){
			gc0329_qctrl[4].default_value=value;
			GC0329_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(gc0329_qctrl[5].default_value!=value){
			gc0329_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(gc0329_qctrl[7].default_value!=value){
			gc0329_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(gc0329_qctrl[8].default_value!=value){
			gc0329_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;

}

/*static void power_down_gc0329(struct gc0329_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	return;
	msleep(5);
	return;
}*/

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void gc0329_fillbuff(struct gc0329_fh *fh, struct gc0329_buffer *buf)
{
	struct gc0329_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = gc0329_qctrl[5].default_value&3;
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = gc0329_qctrl[7].default_value;
	para.angle = gc0329_qctrl[8].default_value;
	para.vaddr = (unsigned)vbuf;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void gc0329_thread_tick(struct gc0329_fh *fh)
{
	struct gc0329_buffer *buf;
	struct gc0329_device *dev = fh->dev;
	struct gc0329_dmaqueue *dma_q = &dev->vidq;

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
			 struct gc0329_buffer, vb.queue);
	dprintk(dev, 1, "%s\n", __func__);
	dprintk(dev, 1, "list entry get buf is %x\n",(unsigned)buf);

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
	gc0329_fillbuff(fh, buf);
	dprintk(dev, 1, "filled buffer %p\n", buf);

	wake_up(&buf->vb.done);
	dprintk(dev, 2, "[%p/%d] wakeup\n", buf, buf->vb. i);
	return;
unlock:
	spin_unlock_irqrestore(&dev->slock, flags);
	return;
}

#define frames_to_ms(frames)					\
	((frames * WAKE_NUMERATOR * 1000) / WAKE_DENOMINATOR)

static void gc0329_sleep(struct gc0329_fh *fh)
{
	struct gc0329_device *dev = fh->dev;
	struct gc0329_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	gc0329_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int gc0329_thread(void *data)
{
	struct gc0329_fh  *fh = data;
	struct gc0329_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		gc0329_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int gc0329_start_thread(struct gc0329_fh *fh)
{
	struct gc0329_device *dev = fh->dev;
	struct gc0329_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(gc0329_thread, fh, "gc0329");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void gc0329_stop_thread(struct gc0329_dmaqueue  *dma_q)
{
	struct gc0329_device *dev = container_of(dma_q, struct gc0329_device, vidq);

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
	struct gc0329_fh  *fh = vq->priv_data;
	struct gc0329_device *dev  = fh->dev;
	//int bytes = fh->fmt->depth >> 3 ;
	*size = fh->width*fh->height*fh->fmt->depth >> 3;
	if (0 == *count)
		*count = 32;

	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	dprintk(dev, 1, "%s, count=%d, size=%d\n", __func__,
		*count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct gc0329_buffer *buf)
{
	struct gc0329_fh  *fh = vq->priv_data;
	struct gc0329_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);
	videobuf_waiton(vq, &buf->vb, 0, 0);
	if (in_interrupt())
		BUG();

	videobuf_vmalloc_free(&buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 1024
#define norm_maxh() 1024
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct gc0329_fh     *fh  = vq->priv_data;
	struct gc0329_device    *dev = fh->dev;
	struct gc0329_buffer *buf = container_of(vb, struct gc0329_buffer, vb);
	int rc;
	//int bytes = fh->fmt->depth >> 3 ;
	dprintk(dev, 1, "%s, field=%d\n", __func__, field);

	BUG_ON(NULL == fh->fmt);

	if (fh->width  < 48 || fh->width  > norm_maxw() ||
	    fh->height < 32 || fh->height > norm_maxh())
		return -EINVAL;

	buf->vb.size = fh->width*fh->height*fh->fmt->depth >> 3;
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
	struct gc0329_buffer    *buf  = container_of(vb, struct gc0329_buffer, vb);
	struct gc0329_fh        *fh   = vq->priv_data;
	struct gc0329_device       *dev  = fh->dev;
	struct gc0329_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct gc0329_buffer   *buf  = container_of(vb, struct gc0329_buffer, vb);
	struct gc0329_fh       *fh   = vq->priv_data;
	struct gc0329_device      *dev  = (struct gc0329_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops gc0329_video_qops = {
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
	struct gc0329_fh  *fh  = priv;
	struct gc0329_device *dev = fh->dev;

	strcpy(cap->driver, "gc0329");
	strcpy(cap->card, "gc0329");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = GC0329_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct gc0329_fmt *fmt;

	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	fmt = &formats[f->index];

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	return 0;
}
static int vidioc_enum_frameintervals(struct file *file, void *priv,
        struct v4l2_frmivalenum *fival)
{
	unsigned int k;
	
	if(fival->index > ARRAY_SIZE(gc0329_frmivalenum))
		return -EINVAL;
	for(k =0; k< ARRAY_SIZE(gc0329_frmivalenum); k++)
	{
		if( (fival->index==gc0329_frmivalenum[k].index)&&
				(fival->pixel_format ==gc0329_frmivalenum[k].pixel_format )&&
				(fival->width==gc0329_frmivalenum[k].width)&&
				(fival->height==gc0329_frmivalenum[k].height)){
			memcpy( fival, &gc0329_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
			return 0;
		}
	}
	
	return -EINVAL;
	
}
static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct gc0329_fh *fh = priv;

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
static int vidioc_g_parm(struct file *file, void *priv,
        struct v4l2_streamparm *parms)
{
	struct gc0329_fh *fh = priv;
	struct gc0329_device *dev = fh->dev;
	struct v4l2_captureparm *cp = &parms->parm.capture;
	
	dprintk(dev,3,"vidioc_g_parm\n");
	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	
	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe = gc0329_frmintervals_active;
	printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
		cp->timeperframe.numerator );
	return 0;
}
static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct gc0329_fh  *fh  = priv;
	struct gc0329_device *dev = fh->dev;
	struct gc0329_fmt *fmt;
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

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct gc0329_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;

	int ret = vidioc_try_fmt_vid_cap(file, fh, f);
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
#if 1
	if(f->fmt.pix.pixelformat==V4L2_PIX_FMT_RGB24){
		gc0329_set_resolution(fh->dev,fh->height,fh->width);
	} else {
		gc0329_set_resolution(fh->dev,fh->height,fh->width);
	}
#endif
	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct gc0329_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gc0329_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gc0329_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gc0329_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct gc0329_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct gc0329_fh  *fh = priv;
	struct gc0329_device *dev = fh->dev;
	vdin_parm_t para;
	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para ));
	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = gc0329_frmintervals_active.denominator;
	para.h_active = gc0329_h_active;
	para.v_active = gc0329_v_active;
	para.hsync_phase = 0;
	para.vsync_phase = 1;
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
	para.skip_count =  2; //skip_num
	para.bt_path = dev->cam_info.bt_path;
	printk("gc0329,h=%d, v=%d, frame_rate=%d\n", 
		gc0329_h_active, gc0329_v_active, gc0329_frmintervals_active.denominator);
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
		vops->start_tvin_service(0,&para);
		fh->stream_on        = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct gc0329_fh  *fh = priv;

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
	struct gc0329_fmt *fmt = NULL;
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
		||(fmt->fourcc == V4L2_PIX_FMT_YVU420)
		){
		if (fsize->index >= ARRAY_SIZE(gc0329_prev_resolution))
			return -EINVAL;
		frmsize = &gc0329_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(gc0329_pic_resolution))
			return -EINVAL;
		frmsize = &gc0329_pic_resolution[fsize->index];
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
	struct gc0329_fh *fh = priv;
	struct gc0329_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct gc0329_fh *fh = priv;
	struct gc0329_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(gc0329_qctrl); i++)
		if (qc->id && qc->id == gc0329_qctrl[i].id) {
			memcpy(qc, &(gc0329_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct gc0329_fh *fh = priv;
	struct gc0329_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(gc0329_qctrl); i++)
		if (ctrl->id == gc0329_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct gc0329_fh *fh = priv;
	struct gc0329_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(gc0329_qctrl); i++)
		if (ctrl->id == gc0329_qctrl[i].id) {
			if (ctrl->value < gc0329_qctrl[i].minimum ||
			    ctrl->value > gc0329_qctrl[i].maximum ||
			    gc0329_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int gc0329_open(struct file *file)
{
	struct gc0329_device *dev = video_drvdata(file);
	struct gc0329_fh *fh = NULL;
	int retval = 0;
#if CONFIG_CMA
    retval = vm_init_buf(16*SZ_1M);
    if(retval <0)
        return -1;
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif		
	aml_cam_init(&dev->cam_info);
	GC0329_init_regs(dev);
	GC0329write_more_registers(dev);
	msleep(100);//40
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

//    TVIN_SIG_FMT_CAMERA_640X480P_30Hz,
//    TVIN_SIG_FMT_CAMERA_800X600P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1024X768P_30Hz, // 190
//    TVIN_SIG_FMT_CAMERA_1920X1080P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1280X720P_30Hz,

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &gc0329_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct gc0329_buffer), fh,NULL);

	gc0329_start_thread(fh);

	return 0;
}

static ssize_t
gc0329_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct gc0329_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
gc0329_poll(struct file *file, struct poll_table_struct *wait)
{
	struct gc0329_fh        *fh = file->private_data;
	struct gc0329_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int gc0329_close(struct file *file)
{
	struct gc0329_fh         *fh = file->private_data;
	struct gc0329_device *dev       = fh->dev;
	struct gc0329_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);

	gc0329_stop_thread(vidq);
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
	gc0329_qctrl[0].default_value=0;
	gc0329_qctrl[1].default_value=4;
	gc0329_qctrl[2].default_value=0;
	gc0329_qctrl[3].default_value=0;
	gc0329_qctrl[4].default_value=0;
	
	gc0329_qctrl[5].default_value=0;
	gc0329_qctrl[7].default_value=100;
	gc0329_qctrl[8].default_value=0;
	gc0329_frmintervals_active.numerator = 1;
	gc0329_frmintervals_active.denominator = 15;
	//power_down_gc0329(dev);
#endif
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

static int gc0329_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gc0329_fh  *fh = file->private_data;
	struct gc0329_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations gc0329_fops = {
	.owner		= THIS_MODULE,
	.open           = gc0329_open,
	.release        = gc0329_close,
	.read           = gc0329_read,
	.poll		= gc0329_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = gc0329_mmap,
};

static const struct v4l2_ioctl_ops gc0329_ioctl_ops = {
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

static struct video_device gc0329_template = {
	.name		= "gc0329_v4l",
	.fops           = &gc0329_fops,
	.ioctl_ops 	= &gc0329_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int gc0329_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_GC0329, 0);
}

static const struct v4l2_subdev_core_ops gc0329_core_ops = {
	.g_chip_ident = gc0329_g_chip_ident,
};

static const struct v4l2_subdev_ops gc0329_ops = {
	.core = &gc0329_core_ops,
};

static int gc0329_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	aml_cam_info_t* plat_dat;
	int err;
	struct gc0329_device *t;
	struct v4l2_subdev *sd;
	
	vops = get_vdin_v4l2_ops();
	
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &gc0329_ops);
	
	plat_dat= (aml_cam_info_t*)client->dev.platform_data;

	/* Now create a video4linux device */
	mutex_init(&t->mutex);

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &gc0329_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);
	
	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "gc0329");

	/* Register it */
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if (plat_dat->front_back >=0)
			video_nr = plat_dat->front_back;
	} else {
		printk("camera gc0329: have no platform data\n");
		kfree(t);
		return -1;
	}
	
	t->cam_info.version = GC0329_DRIVER_VERSION;
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

static int gc0329_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0329_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id gc0329_id[] = {
	{ "gc0329", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gc0329_id);

static struct i2c_driver gc0329_i2c_driver = {
	.driver = {
		.name = "gc0329",
	},
	.probe = gc0329_probe,
	.remove = gc0329_remove,
	.id_table = gc0329_id,
};

module_i2c_driver(gc0329_i2c_driver);

