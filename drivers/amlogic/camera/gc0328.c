/*
 *gc0328 - This code emulates a real video device with v4l2 api
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

#define GC0328_CAMERA_MODULE_NAME "gc0328"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define GC0328_CAMERA_MAJOR_VERSION 0
#define GC0328_CAMERA_MINOR_VERSION 7
#define GC0328_CAMERA_RELEASE 0
#define GC0328_CAMERA_VERSION \
	KERNEL_VERSION(GC0328_CAMERA_MAJOR_VERSION, GC0328_CAMERA_MINOR_VERSION, GC0328_CAMERA_RELEASE)
#define PREVIEW_15_FPS
MODULE_DESCRIPTION("gc0328 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define GC0328_DRIVER_VERSION "GC0328-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static int gc0328_have_open=0;

static int gc0328_h_active=320;
static int gc0328_v_active=240;
static struct v4l2_fract gc0328_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};


static struct vdin_v4l2_ops_s *vops;

/* supported controls */
static struct v4l2_queryctrl gc0328_qctrl[] = {
	{
		.id            = V4L2_CID_DO_WHITE_BALANCE,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "white balance",
		.minimum       = 0,
		.maximum       = 6,
		.step          = 0x1,
		.default_value = CAM_WB_AUTO,
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
		.id            = V4L2_CID_POWER_LINE_FREQUENCY,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "banding",
		.minimum       = CAM_BANDING_50HZ,
		.maximum       = CAM_BANDING_60HZ,
		.step          = 0x1,
		.default_value = CAM_BANDING_50HZ,
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
static struct v4l2_frmivalenum gc0328_frmivalenum[]={
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
struct v4l2_querymenu gc0328_qmenu_wbmode[] = {
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

struct v4l2_querymenu gc0328_qmenu_anti_banding_mode[] = {
	{
		.id         = V4L2_CID_POWER_LINE_FREQUENCY,
		.index      = CAM_BANDING_50HZ, 
		.name       = "50hz",
		.reserved   = 0,
	},{
		.id         = V4L2_CID_POWER_LINE_FREQUENCY,
		.index      = CAM_BANDING_60HZ, 
		.name       = "60hz",
		.reserved   = 0,
	},
};

typedef struct {
	__u32   id;
	int     num;
	struct v4l2_querymenu* gc0328_qmenu;
}gc0328_qmenu_set_t;

gc0328_qmenu_set_t gc0328_qmenu_set[] = {
	{
		.id         	= V4L2_CID_DO_WHITE_BALANCE,
		.num            = ARRAY_SIZE(gc0328_qmenu_wbmode),
		.gc0328_qmenu   = gc0328_qmenu_wbmode,
	},{
		.id         	= V4L2_CID_POWER_LINE_FREQUENCY,
		.num            = ARRAY_SIZE(gc0328_qmenu_anti_banding_mode),
		.gc0328_qmenu   = gc0328_qmenu_anti_banding_mode,
	},
};

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(gc0328_qmenu_set); i++)
	if (a->id && a->id == gc0328_qmenu_set[i].id) {
	    for(j = 0; j < gc0328_qmenu_set[i].num; j++)
		if (a->index == gc0328_qmenu_set[i].gc0328_qmenu[j].index) {
			memcpy(a, &( gc0328_qmenu_set[i].gc0328_qmenu[j]),
				sizeof(*a));
			return (0);
		}
	}

	return -EINVAL;
}

#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct gc0328_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct gc0328_fmt formats[] = {
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
};

static struct gc0328_fmt *get_format(struct v4l2_format *f)
{
	struct gc0328_fmt *fmt;
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
struct gc0328_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct gc0328_fmt        *fmt;
};

struct gc0328_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(gc0328_devicelist);

struct gc0328_device {
	struct list_head			gc0328_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct gc0328_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(gc0328_qctrl)];
};

static inline struct gc0328_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc0328_device, sd);
}

struct gc0328_fh {
	struct gc0328_device            *dev;

	/* video capture */
	struct gc0328_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};

/*static inline struct gc0328_fh *to_fh(struct gc0328_device *dev)
{
	return container_of(dev, struct gc0328_fh, dev);
}*/

static struct v4l2_frmsize_discrete gc0328_prev_resolution[]= //should include 320x240 and 640x480, those two size are used for recording
{
	{320,240},
	{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete gc0328_pic_resolution[]=
{
	{640,480},
};

/* ------------------------------------------------------------------
	reg spec of GC0328
   ------------------------------------------------------------------*/

#if 1
struct aml_camera_i2c_fig1_s GC0328_script[] = {
        {0xfe , 0x80},	 
        {0xfe , 0x80},
        {0xfc , 0x16},	  
	{0xfc , 0x16},	  
	{0xfc , 0x16},	  
	{0xfc , 0x16},	   
	{0xfe , 0x00},	
	{0x4f , 0x00},  
	{0x42 , 0x00},  
	{0x03 , 0x00},  
	{0x04 , 0xc0},  
	{0x77 , 0x62},  
	{0x78 , 0x40},  
	{0x79 , 0x4d},    
	{0xfe , 0x01},	 
	{0x4f , 0x00},	 
	{0x4c , 0x01},	  
	{0xfe , 0x00},	 
	//////////////////////////////
	//AWB///////////////////////
	////////////////////////////////
	{0xfe , 0x01},	
	{0x51 , 0x80},	
	{0x52 , 0x12},	
	{0x53 , 0x80},	
	{0x54 , 0x60},	
	{0x55 , 0x01},	
	{0x56 , 0x06},	
	{0x5b , 0x02},	
	{0x61 , 0xdc},	
        {0x62 , 0xdc},	
	{0x7c , 0x71},	
	{0x7d , 0x00},	
	{0x76 , 0x00},	
	{0x79 , 0x20},	
	{0x7b , 0x00},	
	{0x70 , 0xFF}, 
	{0x71 , 0x00},	
	{0x72 , 0x10},	
	{0x73 , 0x40},	
	{0x74 , 0x40},	
	////AWB//
	{0x50 , 0x00},	 
	{0xfe , 0x01},	 
	{0x4f , 0x00},	  
	{0x4c , 0x01},	
	{0x4f , 0x00},	
	{0x4f , 0x00},  
	{0x4f , 0x00},  
	{0x4d , 0x36},  
	{0x4e , 0x02},  
	{0x4d , 0x46},  
	{0x4e , 0x02},  
	{0x4e , 0x02},  
	{0x4d , 0x53},  
	{0x4e , 0x08},  
	{0x4e , 0x04},  
	{0x4e , 0x04},  
	{0x4d , 0x63},  
	{0x4e , 0x08},  
	{0x4e , 0x08},  
	{0x4d , 0x82},  
	{0x4e , 0x20},  
	{0x4e , 0x20},  
	{0x4d , 0x92},  
	{0x4e , 0x40},  
	{0x4d , 0xa2},  
	{0x4e , 0x40},  
	{0x4f , 0x01},    
	{0x50 , 0x88},	 
	{0xfe , 0x00},    
	////////////////////////////////////////////////
	//////////// 	BLK 	 //////////////////////
	////////////////////////////////////////////////
	{0x27 , 0x00},	
	{0x2a , 0x40},  
	{0x2b , 0x40},  
	{0x2c , 0x40},  
	{0x2d , 0x40},  
        //////////////////////////////////////////////
	////////// page	0	 ////////////////////////
	//////////////////////////////////////////////
	{0xfe , 0x00},	
	{0x05 , 0x00},	
	{0x06 , 0xde},	
	{0x07 , 0x00},	
	{0x08 , 0xa7},	  
	{0x0d , 0x01},	 
	{0x0e , 0xe8},	 
	{0x0f , 0x02},	 
	{0x10 , 0x88},	 
	{0x09 , 0x00},	 
	{0x0a , 0x00},	 
	{0x0b , 0x00},	 
	{0x0c , 0x00},	 
	{0x16 , 0x00},	 
	{0x17 , 0x15},	//[1]upside down,[0]mirror
	{0x18 , 0x0e},	 
	{0x19 , 0x06},	   
	{0x1b , 0x48},	 
	{0x1f , 0xC8},	 
	{0x20 , 0x01},	 
	{0x21 , 0x78},	 
	{0x22 , 0xb0},	 
	{0x23 , 0x06},	 
	{0x24 , 0x11},	 
	{0x26 , 0x00},	   
	{0x50 , 0x01},	  //crop mode				   
	//global gain for range 
	{0x70 , 0x85},	  
	////////////////////////////////////////////////
	//////////// 	block enable	  /////////////
	////////////////////////////////////////////////
	{0x40 , 0x7f},	
	{0x41 , 0x24},	
	{0x42 , 0xff},
	{0x45 , 0x00},	
	{0x44 , 0x03},	
	{0x46 , 0x03},	
	{0x4b , 0x01},	
	{0x50 , 0x01},   
	//DN & EEINTP
	{0x7e , 0x0a},	 
	{0x7f , 0x03},	 
	{0x81 , 0x15},	 
	{0x82 , 0x85},	 
	{0x83 , 0x02},	 
	{0x84 , 0xe5},	 
	{0x90 , 0xac},	 
	{0x92 , 0x02},	 
	{0x94 , 0x02},	 
	{0x95 , 0x54},	   
	///////YCP
	{0xd1 , 0x32},
	{0xd2 , 0x32},
	{0xdd , 0x58},
	{0xde , 0x36},
	{0xe4 , 0x88},
	{0xe5 , 0x40},	 
	{0xd7 , 0x0e},	 						 
	///////////////////////////// 
	//////////////// GAMMA ////// 
	///////////////////////////// 
	//rgb gamma	
	#if 1
	{0xfe , 0x00},
	{0xbf , 0x0e},
	{0xc0 , 0x1c},
	{0xc1 , 0x34},
	{0xc2 , 0x48},
	{0xc3 , 0x5a},
	{0xc4 , 0x6b},
	{0xc5 , 0x7b},
	{0xc6 , 0x95},
	{0xc7 , 0xab},
	{0xc8 , 0xbf},
	{0xc9 , 0xce},
	{0xca , 0xd9},
	{0xcb , 0xe4},
	{0xcc , 0xec},
	{0xcd , 0xf7},
	{0xce , 0xfd},
	{0xcf , 0xff},
	#else
	{0xfe , 0x00},
	{0xbf , 0x08},
	{0xc0 , 0x10},
	{0xc1 , 0x22},
	{0xc2 , 0x32},
	{0xc3 , 0x43},
	{0xc4 , 0x50},
	{0xc5 , 0x5e},
	{0xc6 , 0x78},
	{0xc7 , 0x90},
	{0xc8 , 0xa6},
	{0xc9 , 0xb9},
	{0xca , 0xc9},
	{0xcb , 0xd6},
	{0xcc , 0xe0},
	{0xcd , 0xee},
	{0xce , 0xf8},
	{0xcf , 0xff},
	#endif	
	///Y gamma			
	{0xfe , 0x00},	 
	{0x63 , 0x00},	 
	{0x64 , 0x05},	 
	{0x65 , 0x0b},	 
	{0x66 , 0x19},	 
	{0x67 , 0x2e},	 
	{0x68 , 0x40},	 
	{0x69 , 0x54},	 
	{0x6a , 0x66},	 
	{0x6b , 0x86},	 
	{0x6c , 0xa7},	 
	{0x6d , 0xc6},	 
	{0x6e , 0xe4},	 
	{0x6f , 0xFF},					  
	//////ASDE			  
	{0xfe , 0x01},	 
	{0x18 , 0x02},	 
	{0xfe , 0x00},	 
	{0x98 , 0x00},	 
	{0x9b , 0x20},	 
	{0x9c , 0x80},	 
	{0xa4 , 0x10},	 
	{0xa8 , 0xB0},	 
	{0xaa , 0x40},	 
	{0xa2 , 0x23},	 
	{0xad , 0x01},	   
	//////////////////////////////////////////////
	////////// AEC	 ////////////////////////
	//////////////////////////////////////////////
	{0xfe , 0x01},	
	{0x9c , 0x02},	
	{0x08 , 0xa0},	
	{0x09 , 0xe8},	  
	{0x10 , 0x00},  
	{0x11 , 0x11},	
	{0x12 , 0x10},	
	{0x13 , 0x88},	
	{0x15 , 0xfc},	
	{0x18 , 0x03},
	{0x21 , 0xc0},	
	{0x22 , 0x60},	
	{0x23 , 0x30},	
	{0x25 , 0x00},	
	{0x24 , 0x14},	
 #ifdef PREVIEW_15_FPS
	{0xfe , 0x00},	
	{0x05 , 0x00},	
	{0x06 , 0xde},	
	{0x07 , 0x00},	  
	{0x08 , 0xa7},  
	{0xfe , 0x01},	
	{0x29 , 0x00},	
	{0x2a , 0x83},	
	{0x2b , 0x02},	
	{0x2c , 0x8f},
	{0x2d , 0x02},	
	{0x2e , 0x8f},	
	{0x2f , 0x02},	
	{0x30 , 0x8f},	
	{0x31 , 0x05},	
	{0x32 , 0xdc},	
	{0xfe , 0x00},	
 #endif

	//////////////////////////////////////
	////////////LSC//////////////////////
	//////////////////////////////////////
	//gc0328 Alight lsc reg setting list
	////Record date: 2013-04-01 15:59:05
	{0xfe , 0x01},
	{0xc0 , 0x0d},
	{0xc1 , 0x05},
        {0xc2 , 0x00},
	{0xc6 , 0x07},
	{0xc7 , 0x03},
	{0xc8 , 0x01},
	{0xba , 0x19},
	{0xbb , 0x10},
	{0xbc , 0x0a},
	{0xb4 , 0x19},
	{0xb5 , 0x0d},
	{0xb6 , 0x09},
	{0xc3 , 0x00},
	{0xc4 , 0x00},
	{0xc5 , 0x0e},
	{0xc9 , 0x00},
	{0xca , 0x00},
	{0xcb , 0x00},
	{0xbd , 0x07},
	{0xbe , 0x00},
	{0xbf , 0x0e},
	{0xb7 , 0x09},
	{0xb8 , 0x00},
	{0xb9 , 0x0d},
	{0xa8 , 0x01},
	{0xa9 , 0x00},
	{0xaa , 0x03},
	{0xab , 0x02},
	{0xac , 0x05},
	{0xad , 0x0c},
	{0xae , 0x03},
	{0xaf , 0x00},
	{0xb0 , 0x04},
	{0xb1 , 0x04},
	{0xb2 , 0x03},
	{0xb3 , 0x08},
	{0xa4 , 0x00},
	{0xa5 , 0x00},
	{0xa6 , 0x00},
	{0xa7 , 0x00},
	{0xa1 , 0x3c},
	{0xa2 , 0x50},
	{0xfe , 0x00},				 
	///cct		
	{0xB1 , 0x02},	
	{0xB2 , 0x02},	
	{0xB3 , 0x07},	
	{0xB4 , 0xf0},	
	{0xB5 , 0x05},	
	{0xB6 , 0xf0},	  
	// {WRITE_SENSOR_DELAY , 0xff},   //delay_25ms
	{0xfe , 0x00},	
	{0x27 , 0xf7},  
	{0x28 , 0x7F},	
	{0x29 , 0x20},	
	{0x33 , 0x20},	
	{0x34 , 0x20},	
	{0x35 , 0x20},	
	{0x36 , 0x20},	
	{0x32 , 0x08},	  
	{0x47 , 0x00},	
	{0x48 , 0x00},	 
	{0xfe , 0x01},  
	{0x79 , 0x00},  
	{0x7d , 0x00},	
	{0x50 , 0x88},	
	{0x5b , 0x04}, 
	{0x76 , 0x8f},	
	{0x80 , 0x70},
	{0x81 , 0x70},
	{0x82 , 0xb0},
	{0x70 , 0xff}, 
	{0x71 , 0x00}, 
	{0x72 , 0x10}, 
	{0x73 , 0x40}, 
	{0x74 , 0x40},   
	{0xfe , 0x00},  
	{0x70 , 0x4a},   //45
	{0x4f , 0x01},  
	{0xf1 , 0x07},    
	{0xf2 , 0x01},  
	{0xff , 0xff},
};

//load GT2005 parameters
void GC0328_init_regs(struct gc0328_device *dev)
{
    int i=0;//,j;
    unsigned char buf[2];
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    while(1)
    {
        buf[0] = GC0328_script[i].addr;//(unsigned char)((GC0328_script[i].addr >> 8) & 0xff);
        //buf[1] = (unsigned char)(GC0328_script[i].addr & 0xff);
        buf[1] = GC0328_script[i].val;
        if(GC0328_script[i].val==0xff&&GC0328_script[i].addr==0xff){
            printk("GC0328_write_regs success in initial GC0328.\n");
            break;
        }
        if((i2c_put_byte_add8(client,buf, 2)) < 0){
            printk("fail in initial GC0328. \n");
            return;
        }
        i++;
    }
    
    return;

}

#endif


static struct aml_camera_i2c_fig1_s resolution_320x240_script[] = {

    {0xff, 0xff}
};

static struct aml_camera_i2c_fig1_s resolution_640x480_script[] = {

    {0xff, 0xff}
};

static int set_flip(struct gc0328_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	unsigned char buf[2];
	/* select page 0 */
	buf[0]=0xfe;
	buf[1]=0x00;
	i2c_put_byte_add8(client,buf,2);
	
	temp = i2c_get_byte_add8(client, 0x17);
	temp &= 0xfc;
	temp |= dev->cam_info.m_flip << 0;
	temp |= dev->cam_info.v_flip << 1;
	buf[0] = 0x17;
	buf[1] = temp;
	if((i2c_put_byte_add8(client,buf, 2)) < 0) {
            printk("fail in setting sensor orientation\n");
            return -1;
        }
        return 0;
}

static void gc0328_set_resolution(struct gc0328_device *dev,int height,int width)
{
	int i=0;
        unsigned char buf[2];
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	struct aml_camera_i2c_fig1_s* resolution_script;
	if (width*height >= 640*480) {
		printk("set resolution 640X480\n");
		resolution_script = resolution_640x480_script;
		gc0328_h_active = 640;
		gc0328_v_active = 478;
		gc0328_frmintervals_active.denominator 	= 15;
		gc0328_frmintervals_active.numerator	= 1;
		//GC0328_init_regs(dev);
		//return;
	} else {
		printk("set resolution 320X240\n");
		gc0328_h_active = 320;
		gc0328_v_active = 238;
		gc0328_frmintervals_active.denominator 	= 15;
		gc0328_frmintervals_active.numerator	= 1;
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
*	set_GC0328_param_wb
*
* DESCRIPTION
*	GC0328 wb setting.
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
void set_GC0328_param_wb(struct gc0328_device *dev,enum  camera_wb_flip_e para)
{
//	kal_uint16 rgain=0x80, ggain=0x80, bgain=0x80;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	unsigned char buf[4];

	unsigned char  temp_reg;
	//temp_reg=gc0328_read_byte(0x22);
	buf[0]=0x42;
	temp_reg=i2c_get_byte_add8(client,0x42);

	printk(" camera set_GC0328_param_wb=%d. \n ",para);
	switch (para)
	{
		case CAM_WB_AUTO:
		    #if 0
		    buf[0]=0x77;
		    buf[1]=0x60;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x78;
		    buf[1]=0x40;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x79;
		    buf[1]=0x60;
		    i2c_put_byte_add8(client,buf,2);
		    #endif
		    buf[0]=0x42;
		    buf[1]=temp_reg|0x02;
		    i2c_put_byte_add8(client,buf,2);
		    break;

		case CAM_WB_CLOUD:
                    buf[0]=0x42;
        	    buf[1]=temp_reg&~0x02;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x77;
		    buf[1]=0x58;//0x8c;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x78;
		    buf[1]=0x40;//0x50;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x79;
		    buf[1]=0x58;//0x40;
		    i2c_put_byte_add8(client,buf,2);
		    break;

		case CAM_WB_DAYLIGHT:   // tai yang guang
                    buf[0]=0x42;
		    buf[1]=temp_reg&~0x02;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x77;
		    buf[1]=0x70;//0x74;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x78;
		    buf[1]=0x40;//0x52;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x79;
		    buf[1]=0x50;//0x40;
		    i2c_put_byte_add8(client,buf,2);
		    break;

		case CAM_WB_INCANDESCENCE:   // bai re guang
                    buf[0]=0x42;
		    buf[1]=temp_reg&~0x02;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x77;
		    buf[1]=0x50;//0x48;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x78;
		    buf[1]=0x40;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x79;
		    buf[1]=0xa8;//0x5c;
		    i2c_put_byte_add8(client,buf,2);
		    break;

		case CAM_WB_FLUORESCENT:   //ri guang deng
                    buf[0]=0x42;
		    buf[1]=temp_reg&~0x02;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x77;
		    buf[1]=0x72;//0x40;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x78;
		    buf[1]=0x40;//0x42;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x79;
		    buf[1]=0x5b;//0x50;
		    i2c_put_byte_add8(client,buf,2);
		    break;

	       /*case CAM_WB_WARM_FLUORESCENT://CAM_WB_TUNGSTEN:   // wu si deng
		    buf[0]=0x42;
		    buf[1]=temp_reg&~0x02;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x77;
		    buf[1]=0x40;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x78;
		    buf[1]=0x54;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x79;
		    buf[1]=0x70;
		    i2c_put_byte_add8(client,buf,2);
		    break;

		case CAM_WB_MANUAL:
			// TODO
			break;

		case CAM_WB_SHADE:
			// TODO
			break;

		case CAM_WB_TWILIGHT:
			// TODO
			break;*/
		default:
			break;
	}
//	kal_sleep_task(20);
}

/*************************************************************************
* FUNCTION
*	GC0328_night_mode
*
* DESCRIPTION
*	This function night mode of GC0328.
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
void GC0328_night_mode(struct gc0328_device *dev,enum  camera_night_mode_flip_e enable)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];

	unsigned char  temp_reg;
	//temp_reg=gc0328_read_byte(0x22);
	buf[0]=0x40;
	temp_reg=i2c_get_byte_add8(client,buf[0]);
	temp_reg=0xff;

    if(enable)
    {
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x33;
		buf[1]=0x30;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);

     }
    else
     {
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x33;
		buf[1]=0x20;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);


	}

}
/*************************************************************************
* FUNCTION
*	GC0328_night_mode
*
* DESCRIPTION
*	This function night mode of GC0328.
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

void GC0328_set_param_banding(struct gc0328_device *dev,enum  camera_banding_flip_e banding)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    unsigned char buf[4];
    switch(banding){
        case CAM_BANDING_60HZ:
			/*
			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x05;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x06;
			buf[1]=0xde;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x07;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x08;
			buf[1]=0xa7;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfe;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x29;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2a;
			buf[1]=0x83;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2b;
			buf[1]=0x02;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2c;
			buf[1]=0x8f;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2d;
			#ifndef PREVIEW_15_FPS
			buf[1]=0x03;
			#else
			buf[1]=0x02;
			#endif
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2e;
			#ifndef PREVIEW_15_FPS
			buf[1]=0x95;
			#else
			buf[1]=0x8f;
			#endif
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2f;
			#ifndef PREVIEW_15_FPS
			buf[1]=0x05;
			#else
			buf[1]=0x02;
			#endif
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x30;
			#ifndef PREVIEW_15_FPS
			buf[1]=0x1e;
			#else
			buf[1]=0x8f;
			#endif
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x31;
			buf[1]=0x06;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x32;
			buf[1]=0x24;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x33;
			buf[1]=0x30;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			*/
            break;
        case CAM_BANDING_50HZ:
			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x05;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x06;
			buf[1]=0xde;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x07;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x08;
			buf[1]=0xa7;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfe;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x29;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2a;
			buf[1]=0x83;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2b;
			buf[1]=0x02;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2c;
			buf[1]=0x8f;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2d;
			#ifndef PREVIEW_15_FPS
			buf[1]=0x03;
			#else
			buf[1]=0x02;
			#endif 
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2e;
            #ifndef PREVIEW_15_FPS
			buf[1]=0x95;
            #else
			buf[1]=0x8f;
			#endif
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x2f;
			#ifndef PREVIEW_15_FPS
			buf[1]=0x05;
			#else
			buf[1]=0x02;
			#endif
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x30;
			#ifndef PREVIEW_15_FPS
			buf[1]=0x1e;
			#else
			buf[1]=0x8f;
			#endif
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x31;
			buf[1]=0x06;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x32;
			buf[1]=0x24;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x33;
			buf[1]=0x30;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
    
            break;
	default:
		break;
    }
}


/*************************************************************************
* FUNCTION
*	set_GC0328_param_exposure
*
* DESCRIPTION
*	GC0328 exposure setting.
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
void set_GC0328_param_exposure(struct gc0328_device *dev,enum camera_exposure_e para)//曝光调节
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	unsigned char buf1[2];
	unsigned char buf2[2];
	unsigned char buf3[2];
	unsigned char buf4[2];
	unsigned char buf5[2];

	switch (para)
	{
		case EXPOSURE_N4_STEP:
                        buf1[0]=0xfe;
			buf1[1]=0x00;
			buf2[0]=0xd5;// offset 
			buf2[1]=0xc0;//00
			buf3[0]=0xfe;
			buf3[1]=0x01;
			buf4[0]=0x13;//Y_target  exp_target
			buf4[1]=0x58;//6a
			buf5[0]=0xfe;
			buf5[1]=0x00;
			break;
		case EXPOSURE_N3_STEP:
                        buf1[0]=0xfe;
			buf1[1]=0x00;
			buf2[0]=0xd5;
			buf2[1]=0xd0;//00
			buf3[0]=0xfe;
			buf3[1]=0x01;
			buf4[0]=0x13;
			buf4[1]=0x60;//6a
			buf5[0]=0xfe;
			buf5[1]=0x00;
			break;
		case EXPOSURE_N2_STEP:
                        buf1[0]=0xfe;
			buf1[1]=0x00;
			buf2[0]=0xd5;
			buf2[1]=0xe0;//00
			buf3[0]=0xfe;
			buf3[1]=0x01;
			buf4[0]=0x13;
			buf4[1]=0x68;//6a
			buf5[0]=0xfe;
			buf5[1]=0x00;
			break;
		case EXPOSURE_N1_STEP:
                        buf1[0]=0xfe;
			buf1[1]=0x00;
			buf2[0]=0xd5;
			buf2[1]=0xf0;//00
			buf3[0]=0xfe;
			buf3[1]=0x01;
			buf4[0]=0x13;
			buf4[1]=0x70;//6a
			buf5[0]=0xfe;
			buf5[1]=0x00;
			break;
		case EXPOSURE_0_STEP:
                        buf1[0]=0xfe;
			buf1[1]=0x00;
			buf2[0]=0xd5;
			buf2[1]=0x08;//00
			buf3[0]=0xfe;
			buf3[1]=0x01;
			buf4[0]=0x13;
			buf4[1]=0x88;//6a
			buf5[0]=0xfe;
			buf5[1]=0x00;
			break;
		case EXPOSURE_P1_STEP:
                        buf1[0]=0xfe;
			buf1[1]=0x00;
			buf2[0]=0xd5;
			buf2[1]=0x10;//00
			buf3[0]=0xfe;
			buf3[1]=0x01;
			buf4[0]=0x13;
			buf4[1]=0x90;//6a
			buf5[0]=0xfe;
			buf5[1]=0x00;
			break;
		case EXPOSURE_P2_STEP:
                        buf1[0]=0xfe;
			buf1[1]=0x00;
			buf2[0]=0xd5;
			buf2[1]=0x20;//00
			buf3[0]=0xfe;
			buf3[1]=0x01;
			buf4[0]=0x13;
			buf4[1]=0x98;//6a
			buf5[0]=0xfe;
			buf5[1]=0x00;
			break;
		case EXPOSURE_P3_STEP:
                        buf1[0]=0xfe;
			buf1[1]=0x00;
			buf2[0]=0xd5;
			buf2[1]=0x30;//00
			buf3[0]=0xfe;
			buf3[1]=0x01;
			buf4[0]=0x13;
			buf4[1]=0xa0;//6a
			buf5[0]=0xfe;
			buf5[1]=0x00;
			break;
		case EXPOSURE_P4_STEP:
                        buf1[0]=0xfe;
			buf1[1]=0x00;
			buf2[0]=0xd5;
			buf2[1]=0x40;//00
			buf3[0]=0xfe;
			buf3[1]=0x01;
			buf4[0]=0x13;
			buf4[1]=0xa8;//6a
			buf5[0]=0xfe;
			buf5[1]=0x00;
			break;
		default:
                        buf1[0]=0xfe;
			buf1[1]=0x00;
			buf2[0]=0xd5;
			buf2[1]=0x00;//00
			buf3[0]=0xfe;
			buf3[1]=0x01;
			buf4[0]=0x13;
			buf4[1]=0xb0;//6a
			buf5[0]=0xfe;
			buf5[1]=0x00;
			break;
	}
	//msleep(300);
    i2c_put_byte_add8(client,buf1,2);
    i2c_put_byte_add8(client,buf2,2);
    i2c_put_byte_add8(client,buf3,2);
    i2c_put_byte_add8(client,buf4,2);
    i2c_put_byte_add8(client,buf5,2);
}

/*************************************************************************
* FUNCTION
*	set_GC0328_param_effect
*
* DESCRIPTION
*	GC0328 effect setting.
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
void set_GC0328_param_effect(struct gc0328_device *dev,enum camera_effect_flip_e para)//特效设置
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL:
		    buf[0]=0x43;
		    buf[1]=0x00;
		    i2c_put_byte_add8(client,buf,2);
                    break;

		case CAM_EFFECT_ENC_GRAYSCALE:
		    buf[0]=0x43;
		    buf[1]=0x01;
		    i2c_put_byte_add8(client,buf,2);
		    break;

		case CAM_EFFECT_ENC_SEPIA:
		    buf[0]=0x43;
		    buf[1]=0x02;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0xda;
		    buf[1]=0xc0;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0xdb;
		    buf[1]=0xc0;
		    i2c_put_byte_add8(client,buf,2);
                    break;

		case CAM_EFFECT_ENC_COLORINV:
		    buf[0]=0x43;
		    buf[1]=0x02;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0xda;
		    buf[1]=0xd0;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0xdb;
		    buf[1]=0x28;
		    i2c_put_byte_add8(client,buf,2);
		    break;

		case CAM_EFFECT_ENC_SEPIAGREEN:
		    buf[0]=0x43;
		    buf[1]=0x02;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0xda;
		    buf[1]=0x50;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0xdb;
		    buf[1]=0xe0;
		    i2c_put_byte_add8(client,buf,2);
		    break;

		case CAM_EFFECT_ENC_SEPIABLUE:
		    buf[0]=0x43;
		    buf[1]=0x02;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0xda;
		    buf[1]=0x00;
		    i2c_put_byte_add8(client,buf,2);
		    buf[0]=0xdb;
		    buf[1]=0x00;
		    i2c_put_byte_add8(client,buf,2);
		    break;

		default:
		    break;
	}

}

unsigned char v4l_2_gc0328(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int gc0328_setting(struct gc0328_device *dev,int PROP_ID,int value )
{
	int ret=0;
	switch(PROP_ID)  {
#if 0
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_gc0328(value));
		ret=i2c_put_byte(client,0x0201,v4l_2_gc0328(value));
		break;
	case V4L2_CID_CONTRAST:
		ret=i2c_put_byte(client,0x0200, value);
		break;
	case V4L2_CID_SATURATION:
		ret=i2c_put_byte(client,0x0202, value);
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */
		ret=i2c_get_byte(client,0x0101);
		if(ret>0) {
			cur_val=(char)ret;
			if(value!=0)
				cur_val=cur_val|0x1;
			else
				cur_val=cur_val&0xFE;
			ret=i2c_put_byte(client,0x0101,cur_val);
			if(ret<0) dprintk(dev, 1, "V4L2_CID_HFLIP setting error\n");
		}  else {
			dprintk(dev, 1, "vertical read error\n");
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		ret=i2c_get_byte(client,0x0101);
		if(ret>0) {
			cur_val=(char)ret;
			if(value!=0)
				cur_val=cur_val|0x10;
			else
				cur_val=cur_val&0xFD;
			ret=i2c_put_byte(client,0x0101,cur_val);
		} else {
			dprintk(dev, 1, "vertical read error\n");
		}
		break;
#endif
	case V4L2_CID_DO_WHITE_BALANCE:
		if(gc0328_qctrl[0].default_value!=value){
			gc0328_qctrl[0].default_value=value;
			set_GC0328_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
		}
		break;
	case V4L2_CID_EXPOSURE:
		if(gc0328_qctrl[1].default_value!=value){
			gc0328_qctrl[1].default_value=value;
			set_GC0328_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
		}
		break;
	case V4L2_CID_COLORFX:
		if(gc0328_qctrl[2].default_value!=value){
			gc0328_qctrl[2].default_value=value;
			set_GC0328_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
		}
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		if(gc0328_qctrl[3].default_value!=value){
			gc0328_qctrl[3].default_value=value;
			GC0328_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		break;
	case V4L2_CID_BLUE_BALANCE:
		if(gc0328_qctrl[4].default_value!=value){
			gc0328_qctrl[4].default_value=value;
			GC0328_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(gc0328_qctrl[5].default_value!=value){
			gc0328_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */         
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(gc0328_qctrl[7].default_value!=value){
			gc0328_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(gc0328_qctrl[8].default_value!=value){
			gc0328_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;

}

#if 0
static void power_down_gc0328(struct gc0328_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	/*
	buf[0]=0x1a;
	buf[1]=0x17;
	i2c_put_byte_add8(client,buf,2);
	buf[0]=0x25;
	buf[1]=0x00;
	i2c_put_byte_add8(client,buf,2);
	*/
	msleep(5);
	return;
}
#endif

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void gc0328_fillbuff(struct gc0328_fh *fh, struct gc0328_buffer *buf)
{
	struct gc0328_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = gc0328_qctrl[5].default_value&3;// not set
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = gc0328_qctrl[7].default_value;
	para.vaddr = (unsigned)vbuf;
	para.angle = gc0328_qctrl[8].default_value;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void gc0328_thread_tick(struct gc0328_fh *fh)
{
	struct gc0328_buffer *buf;
	struct gc0328_device *dev = fh->dev;
	struct gc0328_dmaqueue *dma_q = &dev->vidq;

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
			 struct gc0328_buffer, vb.queue);
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
	gc0328_fillbuff(fh, buf);
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

static void gc0328_sleep(struct gc0328_fh *fh)
{
	struct gc0328_device *dev = fh->dev;
	struct gc0328_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	gc0328_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int gc0328_thread(void *data)
{
	struct gc0328_fh  *fh = data;
	struct gc0328_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		gc0328_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int gc0328_start_thread(struct gc0328_fh *fh)
{
	struct gc0328_device *dev = fh->dev;
	struct gc0328_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(gc0328_thread, fh, "gc0328");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void gc0328_stop_thread(struct gc0328_dmaqueue  *dma_q)
{
	struct gc0328_device *dev = container_of(dma_q, struct gc0328_device, vidq);

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
	struct gc0328_fh  *fh = vq->priv_data;
	struct gc0328_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct gc0328_buffer *buf)
{
	struct gc0328_fh  *fh = vq->priv_data;
	struct gc0328_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);
    videobuf_waiton(vq, &buf->vb, 0, 0);
	if (in_interrupt())
		BUG();

	videobuf_vmalloc_free(&buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 1024
#define norm_maxh() 768
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct gc0328_fh     *fh  = vq->priv_data;
	struct gc0328_device    *dev = fh->dev;
	struct gc0328_buffer *buf = container_of(vb, struct gc0328_buffer, vb);
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
	struct gc0328_buffer    *buf  = container_of(vb, struct gc0328_buffer, vb);
	struct gc0328_fh        *fh   = vq->priv_data;
	struct gc0328_device       *dev  = fh->dev;
	struct gc0328_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct gc0328_buffer   *buf  = container_of(vb, struct gc0328_buffer, vb);
	struct gc0328_fh       *fh   = vq->priv_data;
	struct gc0328_device      *dev  = (struct gc0328_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops gc0328_video_qops = {
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
	struct gc0328_fh  *fh  = priv;
	struct gc0328_device *dev = fh->dev;

	strcpy(cap->driver, "gc0328");
	strcpy(cap->card, "gc0328");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = GC0328_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct gc0328_fmt *fmt;

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

    if(fival->index > ARRAY_SIZE(gc0328_frmivalenum))
        return -EINVAL;

    for(k =0; k< ARRAY_SIZE(gc0328_frmivalenum); k++)
    {
        if( (fival->index==gc0328_frmivalenum[k].index)&&
                (fival->pixel_format ==gc0328_frmivalenum[k].pixel_format )&&
                (fival->width==gc0328_frmivalenum[k].width)&&
                (fival->height==gc0328_frmivalenum[k].height)){
            memcpy( fival, &gc0328_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
            return 0;
        }
    }

    return -EINVAL;

}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct gc0328_fh *fh = priv;

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

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct gc0328_fh  *fh  = priv;
	struct gc0328_device *dev = fh->dev;
	struct gc0328_fmt *fmt;
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
	struct gc0328_fh *fh = priv;
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
		gc0328_set_resolution(fh->dev,fh->height,fh->width);
	} else {
		gc0328_set_resolution(fh->dev,fh->height,fh->width);
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
    struct gc0328_fh *fh = priv;
    struct gc0328_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = gc0328_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct gc0328_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gc0328_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gc0328_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gc0328_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct gc0328_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct gc0328_fh  *fh = priv;
	vdin_parm_t para;
	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para ));
	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = gc0328_frmintervals_active.denominator;
	para.h_active = gc0328_h_active;
	para.v_active = gc0328_v_active;
	para.hsync_phase = 0;
	para.vsync_phase  = 1;	
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
	para.skip_count =  2; //skip_num
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
        vops->start_tvin_service(0,&para);
	    fh->stream_on        = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct gc0328_fh  *fh = priv;

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
	struct gc0328_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(gc0328_prev_resolution))
			return -EINVAL;
		frmsize = &gc0328_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(gc0328_pic_resolution))
			return -EINVAL;
		frmsize = &gc0328_pic_resolution[fsize->index];
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
	struct gc0328_fh *fh = priv;
	struct gc0328_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct gc0328_fh *fh = priv;
	struct gc0328_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(gc0328_qctrl); i++)
		if (qc->id && qc->id == gc0328_qctrl[i].id) {
			memcpy(qc, &(gc0328_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct gc0328_fh *fh = priv;
	struct gc0328_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(gc0328_qctrl); i++)
		if (ctrl->id == gc0328_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct gc0328_fh *fh = priv;
	struct gc0328_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(gc0328_qctrl); i++)
		if (ctrl->id == gc0328_qctrl[i].id) {
			if (ctrl->value < gc0328_qctrl[i].minimum ||
			    ctrl->value > gc0328_qctrl[i].maximum ||
			    gc0328_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int gc0328_open(struct file *file)
{
	struct gc0328_device *dev = video_drvdata(file);
	struct gc0328_fh *fh = NULL;
	int retval = 0;
#ifdef CONFIG_CMA
    retval = vm_init_buf(16*SZ_1M);
    if(retval <0)
        return -1;
#endif
	gc0328_have_open=1;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif		
	aml_cam_init(&dev->cam_info);	
	
	GC0328_init_regs(dev);
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

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &gc0328_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct gc0328_buffer), fh,NULL);

	gc0328_start_thread(fh);

	return 0;
}

static ssize_t
gc0328_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct gc0328_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
gc0328_poll(struct file *file, struct poll_table_struct *wait)
{
	struct gc0328_fh        *fh = file->private_data;
	struct gc0328_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int gc0328_close(struct file *file)
{
	struct gc0328_fh         *fh = file->private_data;
	struct gc0328_device *dev       = fh->dev;
	struct gc0328_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);
	gc0328_have_open=0;

	gc0328_stop_thread(vidq);
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
	gc0328_qctrl[0].default_value=0;
	gc0328_qctrl[1].default_value=4;
	gc0328_qctrl[2].default_value=0;
	gc0328_qctrl[3].default_value= CAM_BANDING_50HZ;
	gc0328_qctrl[4].default_value=0;

	gc0328_qctrl[5].default_value=0;
	gc0328_qctrl[7].default_value=100;
	gc0328_qctrl[8].default_value=0;
       gc0328_frmintervals_active.numerator = 1;
	gc0328_frmintervals_active.denominator = 15;
	//power_down_gc0328(dev);
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

static int gc0328_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gc0328_fh  *fh = file->private_data;
	struct gc0328_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations gc0328_fops = {
	.owner		= THIS_MODULE,
	.open           = gc0328_open,
	.release        = gc0328_close,
	.read           = gc0328_read,
	.poll		= gc0328_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = gc0328_mmap,
};

static const struct v4l2_ioctl_ops gc0328_ioctl_ops = {
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

static struct video_device gc0328_template = {
	.name		= "gc0328_v4l",
	.fops           = &gc0328_fops,
	.ioctl_ops 	= &gc0328_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int gc0328_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_GC0328, 0);
}

static const struct v4l2_subdev_core_ops gc0328_core_ops = {
	.g_chip_ident = gc0328_g_chip_ident,
};

static const struct v4l2_subdev_ops gc0328_ops = {
	.core = &gc0328_core_ops,
};

static int gc0328_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	aml_cam_info_t* plat_dat;
	int err;
	struct gc0328_device *t;
	struct v4l2_subdev *sd;
    vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &gc0328_ops);
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
	memcpy(t->vdev, &gc0328_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);

	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "gc0328");
	
	/* Register it */
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		video_nr = plat_dat->front_back;
	} else {
		printk("camera gc0328: have no platform data\n");
		kfree(t);
		kfree(client);
		return -1;
	}
	
	t->cam_info.version = GC0328_DRIVER_VERSION;
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

static int gc0328_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0328_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id gc0328_id[] = {
	{ "gc0328_i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gc0328_id);

static struct i2c_driver gc0328_i2c_driver = {
	.driver = {
		.name = "gc0328",
	},
	.probe = gc0328_probe,
	.remove = gc0328_remove,
	.id_table = gc0328_id,
};

module_i2c_driver(gc0328_i2c_driver);

