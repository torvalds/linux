/*
 *HI704 - This code emulates a real video device with v4l2 api
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

#define HI704_CAMERA_MODULE_NAME "HI704"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define HI704_CAMERA_MAJOR_VERSION 0
#define HI704_CAMERA_MINOR_VERSION 7
#define HI704_CAMERA_RELEASE 0
#define HI704_CAMERA_VERSION \
	KERNEL_VERSION(HI704_CAMERA_MAJOR_VERSION, HI704_CAMERA_MINOR_VERSION, HI704_CAMERA_RELEASE)

MODULE_DESCRIPTION("HI704 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define HI704_DRIVER_VERSION "HI704-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static struct v4l2_fract hi704_frmintervals_active = {
	.numerator = 1,
	.denominator = 15,
};

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static struct vdin_v4l2_ops_s *vops;

/* supported controls */
static struct v4l2_queryctrl HI704_qctrl[] = {

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

static struct v4l2_frmivalenum hi704_frmivalenum[]={
	{
		.index = 0,
		.pixel_format = V4L2_PIX_FMT_NV21,
		.width = 640,
		.height = 480,
		.type = V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete ={
				.numerator = 1,
				.denominator = 15,
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
	},
};

#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct HI704_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct HI704_fmt formats[] = {
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

static struct HI704_fmt *get_format(struct v4l2_format *f)
{
	struct HI704_fmt *fmt;
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
struct HI704_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct HI704_fmt        *fmt;
};

struct HI704_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(HI704_devicelist);

struct HI704_device {
	struct list_head			HI704_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct HI704_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(HI704_qctrl)];
};

static inline struct HI704_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct HI704_device, sd);
}

struct HI704_fh {
	struct HI704_device            *dev;

	/* video capture */
	struct HI704_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int f_flags;
};

/*static inline struct HI704_fh *to_fh(struct HI704_device *dev)
{
	return container_of(dev, struct HI704_fh, dev);
}*/

static struct v4l2_frmsize_discrete HI704_prev_resolution[]= //should include 320x240 and 640x480, those two size are used for recording
{
	{320,240},
	{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete HI704_pic_resolution[]=
{
	{640,480},
};

/* ------------------------------------------------------------------
	reg spec of HI704
   ------------------------------------------------------------------*/

#if 1

	struct aml_camera_i2c_fig1_s HI704_script[] = {
	{0x01, 0xf1},//sleep on
	{0x01, 0xf3},//soft reset
	{0x01, 0xf1},//sleep on

	{0x03, 0x20},//page 20
	{0x10, 0x1c},//AE off
	{0x03, 0x22},//page 22
	{0x10, 0x6a},//AWB off

	//page 00
	{0x03, 0x00},
	{0x10, 0x00},
	{0x11, 0x91},
	{0x12, 0x24},	//20fixxucm:0x00->0x20 ，修改vsync极性
	{0x20, 0x00},
	{0x21, 0x04},//04
	{0x22, 0x00},
	{0x23, 0x02},//07

	{0x24, 0x01},
	{0x25, 0xe6},//e0
	{0x26, 0x02},
	{0x27, 0x86},//80

	{0x40, 0x01},//hblank
	{0x41, 0x58},
	{0x42, 0x00},//vblank
	{0x43, 0x14},
		
	            
	//BLC
	{0x80, 0x2e},
	{0x81, 0x7e},
	{0x82, 0x90},
	{0x83, 0x30},
	{0x84, 0x2c},
	{0x85, 0x4b},
	{0x89, 0x48},

	{0x90, 0x0b},
	{0x91, 0x0b},    
	{0x92, 0x48},
	{0x93, 0x48},
	{0x98, 0x38},
	{0x99, 0x40},
	{0xa0, 0x00},
	{0xa8, 0x40},

	//PAGE 2
	//Analog Circuit
	{0x03, 0x02},      
	{0x13, 0x40},
	{0x14, 0x04},
	{0x1a, 0x00},
	{0x1b, 0x08},

	{0x20, 0x33},
	{0x21, 0xaa},
	{0x22, 0xa7},
	{0x23, 0x32},       //For Sun Pot b1->32 

	{0x3b, 0x48},

	{0x50, 0x21},
	{0x52, 0xa2},
	{0x53, 0x0a},
	{0x54, 0x30},
	{0x55, 0x10},
	{0x56, 0x0c},
	{0x59, 0x0F},

	{0x60, 0xca}, // 54-ca 
	{0x61, 0xdb},
	{0x62, 0xca},
	{0x63, 0xda},
	{0x64, 0xca},
	{0x65, 0xda},
	{0x72, 0xcb},
	{0x73, 0xd8},
	{0x74, 0xcb},
	{0x75, 0xd8},
	{0x80, 0x02},
	{0x81, 0xbd},
	{0x82, 0x24},
	{0x83, 0x3e},
	{0x84, 0x24},
	{0x85, 0x3e},
	{0x92, 0x72},
	{0x93, 0x8c},
	{0x94, 0x72},
	{0x95, 0x8c},
	{0xa0, 0x03},
	{0xa1, 0xbb},
	{0xa4, 0xbb},
	{0xa5, 0x03},
	{0xa8, 0x44},
	{0xa9, 0x6a},
	{0xaa, 0x92},
	{0xab, 0xb7},
	{0xb8, 0xc9},
	{0xb9, 0xd0},
	{0xbc, 0x20},
	{0xbd, 0x28},
	{0xc0, 0xde},
	{0xc1, 0xec},
	{0xc2, 0xde},
	{0xc3, 0xec},
	{0xc4, 0xe0},
	{0xc5, 0xea},
	{0xc6, 0xe0},
	{0xc7, 0xea},
	{0xc8, 0xe1},
	{0xc9, 0xe8},
	{0xca, 0xe1},
	{0xcb, 0xe8},
	{0xcc, 0xe2},
	{0xcd, 0xe7},
	{0xce, 0xe2},
	{0xcf, 0xe7},
	{0xd0, 0xc8},
	{0xd1, 0xef},

	//PAGE 10
	//Image Format, Image Effect
	{0x03, 0x10},
	{0x10, 0x00},//02
	{0x11, 0x43},
	{0x12, 0x30},

	{0x40, 0x80},
	{0x41, 0x00},
	{0x48, 0x88},

	{0x50, 0x42},
	   
	{0x60, 0x7f},
	{0x61, 0x00},
	{0x62, 0x8e}, // sat blue
	{0x63, 0x8e}, //sat red
	{0x64, 0x48},
	{0x66, 0x90},
	{0x67, 0x36}, //42

	//PAGE 11
	//Z-LPF
	{0x03, 0x11},
	{0x10, 0x25},   
	{0x11, 0x1f},   

	{0x20, 0x00},   
	{0x21, 0x38},   
	{0x23, 0x0a},

	{0x60, 0x10},   
	{0x61, 0x82},
	{0x62, 0x00},   
	{0x63, 0x83},   
	{0x64, 0x83},      
	{0x67, 0xF0},   
	{0x68, 0x30},   
	{0x69, 0x10},   

	//PAGE 12
	//2D
	{0x03, 0x12},

	{0x40, 0xe9},
	{0x41, 0x09},

	{0x50, 0x18},
	{0x51, 0x24},

	{0x70, 0x1f},
	{0x71, 0x00},
	{0x72, 0x00},
	{0x73, 0x00},
	{0x74, 0x10},
	{0x75, 0x10},
	{0x76, 0x20},
	{0x77, 0x80},
	{0x78, 0x88},
	{0x79, 0x18},

	{0xb0, 0x7d},

	//PAGE 13
	//Edge Enhancement
	{0x03, 0x13},
	{0x10, 0x01},   
	{0x11, 0x89},   
	{0x12, 0x14},   
	{0x13, 0x19},   
	{0x14, 0x08},

	{0x20, 0x05},
	{0x21, 0x03},
	{0x23, 0x30},
	{0x24, 0x33},
	{0x25, 0x08},
	{0x26, 0x18},
	{0x27, 0x00},
	{0x28, 0x08},
	{0x29, 0x50},
	{0x2a, 0xe0},
	{0x2b, 0x10},
	{0x2c, 0x28},
	{0x2d, 0x40},
	{0x2e, 0x00},
	{0x2f, 0x00},
	{0x30, 0x11},
	{0x80, 0x03},
	{0x81, 0x07},
	{0x90, 0x03},
	{0x91, 0x03},
	{0x92, 0x00},
	{0x93, 0x20},
	{0x94, 0x42},
	{0x95, 0x60},

	//PAGE 11
	{0x30, 0x11},

	{0x80, 0x03},
	{0x81, 0x07},

	{0x90, 0x04},
	{0x91, 0x02},
	{0x92, 0x00},
	{0x93, 0x20},
	{0x94, 0x42},
	{0x95, 0x60},

	//PAGE 14
	//Lens Shading Correction
	{0x03, 0x14},
	{0x10, 0x01},

	{0x20, 0x80},   //For Y decay
	{0x21, 0x80},   //For Y decay
	{0x22, 0x78},
	{0x23, 0x4d},
	{0x24, 0x46},

	//PAGE 15 
	//Color Correction
	{0x03, 0x15}, 
	{0x10, 0x03},         
	{0x14, 0x3c},
	{0x16, 0x2c},
	{0x17, 0x2f},
	  
	{0x30, 0xcb},
	{0x31, 0x61},
	{0x32, 0x16},
	{0x33, 0x23},
	{0x34, 0xce},
	{0x35, 0x2b},
	{0x36, 0x01},
	{0x37, 0x34},
	{0x38, 0x75},
	   
	{0x40, 0x87},
	{0x41, 0x18},
	{0x42, 0x91},
	{0x43, 0x94},
	{0x44, 0x9f},
	{0x45, 0x33},
	{0x46, 0x00},
	{0x47, 0x94},
	{0x48, 0x14},

	//PAGE 16
	//Gamma Correction
	{0x03,  0x16},        
	{0x30,  0x00},
	{0x31,  0x0a},
	{0x32,  0x1b},
	{0x33,  0x2e},
	{0x34,  0x5c},
	{0x35,  0x79},
	{0x36,  0x95},
	{0x37,  0xa4},
	{0x38,  0xb1},
	{0x39,  0xbd},
	{0x3a,  0xc8},
	{0x3b,  0xd9},
	{0x3c,  0xe8},
	{0x3d,  0xf5},
	{0x3e,  0xff},

	//PAGE 17 
	//Auto Flicker Cancellation 
	{0x03, 0x17},

	{0xc4, 0x3c},
	{0xc5, 0x32},

	//PAGE 20 
	//AE 
	{0x03, 0x20},

	{0x10, 0x0c},
	{0x11, 0x04},
	   
	{0x20, 0x01},
	{0x28, 0x27},
	{0x29, 0xa1},   
	{0x2a, 0xf0},
	{0x2b, 0x34},
	{0x2c, 0x2b},
	   
	{0x30, 0xf8},
	{0x39, 0x22},
	{0x3a, 0xde},
	{0x3b, 0x22},
	{0x3c, 0xde},

	{0x60, 0x95},
	{0x68, 0x3c},
	{0x69, 0x64},
	{0x6A, 0x28},
	{0x6B, 0xc8},

	{0x70, 0x42},   //For Y decay   
	{0x76, 0x22},
	{0x77, 0x02},   
	{0x78, 0x12},
	{0x79, 0x27},
	{0x7a, 0x23},  
	{0x7c, 0x1d},
	{0x7d, 0x22},

	{0x83, 0x00},
	{0x84, 0xaf},
	{0x85, 0xc8}, 

	{0x86, 0x00},
	{0x87, 0xfa},

	{0x88, 0x03}, // 7fps
	{0x89, 0x34},
	{0x8a, 0x50},    

	{0x8b, 0x3a},
	{0x8c, 0x98},  

	{0x8d, 0x30},
	{0x8e, 0xd4},

	{0x91, 0x02},
	{0x92, 0xdc},
	{0x93, 0x6c},   
	{0x94, 0x01},
	{0x95, 0xb7},
	{0x96, 0x74},   
	{0x98, 0x8C},
	{0x99, 0x23},  

	{0x9c, 0x06},   //For Y decay: Exposure Time
	{0x9d, 0xd6},   //For Y decay: Exposure Time
	{0x9e, 0x00},
	{0x9f, 0xfa},

	{0xb1, 0x14},
	{0xb2, 0x48}, //50
	{0xb4, 0x14},
	{0xb5, 0x38},
	{0xb6, 0x26},
	{0xb7, 0x20},
	{0xb8, 0x1d},
	{0xb9, 0x1b},
	{0xba, 0x1a},
	{0xbb, 0x19},
	{0xbc, 0x19},
	{0xbd, 0x18},

	{0xc0, 0x16},   //0x1a->0x16
	{0xc3, 0x48},
	{0xc4, 0x48}, 

	//PAGE 22 
	//AWB
	{0x03, 0x22},
	{0x10, 0xe2},
	{0x11, 0x26},
	{0x20, 0x34},   
	{0x21, 0x40},
	   
	{0x30, 0x80},
	{0x31, 0x80},
	{0x38, 0x12},
	{0x39, 0x33},

	{0x40, 0xf0},
	{0x41, 0x33}, //55
	{0x42, 0x33}, //
	{0x43, 0xf3}, //
	{0x44, 0x55}, //88
	{0x45, 0x44}, //66
	{0x46, 0x02},
	   
	{0x80, 0x3a},
	{0x81, 0x20},
	{0x82, 0x40},
	{0x83, 0x52},
	{0x84, 0x1b},
	{0x85, 0x50},
	{0x86, 0x25},
	{0x87, 0x4d},
	{0x88, 0x38},
	{0x89, 0x3e},
	{0x8a, 0x29},
	{0x8b, 0x02},
	{0x8d, 0x22},
	{0x8e, 0x71},  
	{0x8f, 0x63},

	{0x90, 0x60},
	{0x91, 0x5c},
	{0x92, 0x56},
	{0x93, 0x52},
	{0x94, 0x4c},
	{0x95, 0x36},
	{0x96, 0x31},
	{0x97, 0x2e},
	{0x98, 0x2a},
	{0x99, 0x29},
	{0x9a, 0x26},
	{0x9b, 0x09},

	//PAGE 22
	{0x03, 0x22},
	{0x10, 0xfb},

	//PAGE 20
	{0x03, 0x20},
	{0x10, 0x9c},

	{0x03, 0x00},
	{0x01, 0xf0},
	{0x03, 0x00},
	{0xff, 0xff},
};

//load GT2005 parameters
void HI704_init_regs(struct HI704_device *dev)
{
	int i=0;//,j;
	unsigned char buf[2],tmp[2];
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	buf[0] = i2c_get_byte_add8(client, 0x04);
	printk("#####[%s(%d)]:buf[0] = 0x%x\n", __FUNCTION__, __LINE__, buf[0]);
	
	while(1) {
		buf[0] = HI704_script[i].addr;//(unsigned char)((HI704_script[i].addr >> 8) & 0xff);
		//buf[1] = (unsigned char)(HI704_script[i].addr & 0xff);
		buf[1] = HI704_script[i].val;
		
		if (HI704_script[i].val==0xff&&HI704_script[i].addr==0xff) {
			printk("HI704_write_regs success in initial HI704.\n");
			break;
	 	}
		if((i2c_put_byte_add8(client,buf, 2)) < 0) {
			printk("fail in initial HI704. \n");
			return;
		} else {
			tmp[0] = i2c_get_byte_add8(client, buf[0]);
			if(buf[1]!=tmp[0])
				printk("failed: write buf[0x%x] = 0x%x, result = 0x%x\n",buf[0],buf[1],tmp[0]);
		}
		i++;
	}

	return;
}


#endif

/*************************************************************************
* FUNCTION
*	set_HI704_param_wb
*
* DESCRIPTION
*	HI704 wb setting.
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
void set_HI704_param_wb(struct HI704_device *dev,enum  camera_wb_flip_e para)
{
//	kal_uint16 rgain=0x80, ggain=0x80, bgain=0x80;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	unsigned char buf[4];
	int i=0;

	//unsigned char  temp_reg;
	//temp_reg=HI704_read_byte(0x22);
	//buf[0]=0x22;
	//temp_reg=i2c_get_byte_add8(client,buf);

	printk(" camera set_HI704_param_wb=%d. \n ",para);
	
	#if 1
	switch (para) {
	case CAM_WB_AUTO:
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03 , 0x22},
					{0x10 , 0xea},
					{0x80 , 0x30},
					{0x81 , 0x28},
					{0x82 , 0x30},
					{0x83 , 0x55},
					{0x84 , 0x16},
					{0x85 , 0x53},
					{0x86 , 0x25},
					{0xff , 0xff},
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}
		
		break;

	case CAM_WB_CLOUD:
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03, 0x22},  									   
					{0x10, 0x6a},
					{0x80, 0x50},
					{0x81, 0x20}, 
					{0x82, 0x24}, 
					{0x83, 0x6d},
					{0x84, 0x65}, 
					{0x85, 0x24}, 
					{0x86, 0x1c},   
					{0xff, 0xff}    
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}
		
		break;

	case CAM_WB_DAYLIGHT:   
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03, 0x22},  									   
					{0x10, 0x6a},
					{0x80, 0x40},
					{0x81, 0x20}, 
					{0x82, 0x28}, 
					{0x83, 0x45},
					{0x84, 0x35}, 
					{0x85, 0x2d}, 
					{0x86, 0x1c},   
					{0xff, 0xff}    
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}

		
		break;

	case CAM_WB_INCANDESCENCE:   // bai re guang
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03, 0x22},									   
					{0x10, 0x6a},
					{0x80, 0x35},
					{0x81, 0x20}, 
					{0x82, 0x32}, 
					{0x83, 0x3c},
					{0x84, 0x2c}, 
					{0x85, 0x45}, 
					{0x86, 0x35}, 
					{0xff, 0xff}    
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}
		break;

	case CAM_WB_FLUORESCENT:  
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03, 0x22}, 									   
					{0x10, 0x6a},
					{0x80, 0x23},
					{0x81, 0x20}, 
					{0x82, 0x3d}, 
					{0x83, 0x2e},
					{0x84, 0x24}, 
					{0x85, 0x43}, 
					{0x86, 0x3d},   
					{0xff, 0xff}    
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}
		break;

	case CAM_WB_TUNGSTEN:   
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03, 0x22},  		//U30							   
					{0x10, 0x6a},
					{0x80, 0x58},
					{0x81, 0x10}, 
					{0x82, 0x70}, 
					{0x83, 0x58},
					{0x84, 0x10}, 
					{0x85, 0x70}, 
					{0x86, 0x10},   
					{0xff, 0xff}  
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}
		break;

	case CAM_WB_MANUAL:
		// TODO
		break;
	default:
		break;
	}
	#endif
//	kal_sleep_task(20);
}

/*************************************************************************
* FUNCTION
*	HI704_night_mode
*
* DESCRIPTION
*	This function night mode of HI704.
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
void HI704_night_mode(struct HI704_device *dev,enum  camera_night_mode_flip_e enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	int i=0;

	//unsigned char  temp_reg;
	//temp_reg=HI704_read_byte(0x22);
	//buf[0]=0x20;
	//temp_reg=i2c_get_byte_add8(client,buf);
	//temp_reg=0xff;
	#if 1
	if (enable) {
		
		struct aml_camera_i2c_fig_s regs[]=
			{
			       {0x03, 0x20}, //Page 20
				{0x83, 0x04}, //EXP Normal 5.00 fps 
				{0x84, 0x93}, 
				{0x85, 0xe0}, 
				{0x86, 0x00}, //EXPMin 6000.00 fps
				{0x87, 0xfa}, 
				{0x88, 0x04}, //EXP Max 5.00 fps 
				{0x89, 0x93}, 
				{0x8a, 0xe0}, 
				{0x8B, 0x3a}, //EXP100 
				{0x8C, 0x98}, 
				{0x8D, 0x30}, //EXP120 
				{0x8E, 0xd4}, 
				{0x9c, 0x08}, //EXP Limit 666.67 fps 
				{0x9d, 0xca}, 
				{0x9e, 0x00}, //EXP Unit 
				{0x9f, 0xfa}, 
		
				{0xff , 0xff},
			};
		i=0;
		while (regs[i].addr!= 0xff && regs[i].val!= 0xff) {
			buf[0]=regs[i].addr;
			buf[1]=regs[i].val;
			i2c_put_byte_add8(client,buf, 2);
			i++;
		}


	} else {
		
		struct aml_camera_i2c_fig_s regs[]=
			{
				{0x03, 0x20}, //Page 20
				{0x83, 0x00}, //EXP Normal 33.33 fps 
				{0x84, 0xaf}, 
				{0x85, 0xc8}, 
				{0x86, 0x00}, //EXPMin 6000.00 fps
				{0x87, 0xfa}, 
				{0x88, 0x03}, //EXP Max 7.14 fps 
				{0x89, 0x34}, 
				{0x8a, 0x50}, 
				{0x8B, 0x3a}, //EXP100 
				{0x8C, 0x98}, 
				{0x8D, 0x30}, //EXP120 
				{0x8E, 0xd4}, 
				{0x9c, 0x08}, //EXP Limit 666.67 fps 
				{0x9d, 0xca}, 
				{0x9e, 0x00}, //EXP Unit 
				{0x9f, 0xfa}, 
				{0xff , 0xff},
			};
		i=0;
		while (regs[i].addr!= 0xff && regs[i].val!= 0xff) {
			buf[0]=regs[i].addr;
			buf[1]=regs[i].val;
			i2c_put_byte_add8(client,buf, 2);
			i++;
		}
	}
#endif
}
/*************************************************************************
* FUNCTION
*	HI704_night_mode
*
* DESCRIPTION
*	This function night mode of HI704.
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
struct aml_camera_i2c_fig_s regs60hz[]=
{
	{0x03, 0x20}, //Page 20
	{0x10, 0x8c}, 
	{0x83, 0x00}, //EXP Normal 30.00 fps 
	{0x84, 0xc3}, 
	{0x85, 0x50}, 
	{0x86, 0x00}, //EXPMin 6000.00 fps
	{0x87, 0xfa}, 
	{0x88, 0x03}, //EXP Max 7.06 fps 
	{0x89, 0x3e}, 
	{0x8a, 0x14}, 
	{0x8B, 0x3a}, //EXP100 
	{0x8C, 0x98}, 
	{0x8D, 0x30}, //EXP120 
	{0x8E, 0xd4}, 
	{0x9c, 0x08}, //EXP Limit 666.67 fps 
	{0x9d, 0xca}, 
	{0x9e, 0x00}, //EXP Unit 
	{0x9f, 0xfa}, 

	{0xff , 0xff},
};

struct aml_camera_i2c_fig_s regs50hz[]=
{
	{0x03, 0x20}, //Page 20
	{0x10, 0x9c},
	{0x83, 0x00}, //EXP Normal 33.33 fps 
	{0x84, 0xaf}, 
	{0x85, 0xc8}, 
	{0x86, 0x00}, //EXPMin 6000.00 fps
	{0x87, 0xfa}, 
	{0x88, 0x03}, //EXP Max 7.14 fps 
	{0x89, 0x34}, 
	{0x8a, 0x50}, 
	{0x8B, 0x3a}, //EXP100 
	{0x8C, 0x98}, 
	{0x8D, 0x30}, //EXP120 
	{0x8E, 0xd4}, 
	{0x9c, 0x08}, //EXP Limit 666.67 fps 
	{0x9d, 0xca}, 
	{0x9e, 0x00}, //EXP Unit 
	{0x9f, 0xfa}, 

	{0xff , 0xff},
};

void HI704_set_param_banding(struct HI704_device *dev,enum  camera_banding_flip_e banding)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int i;
	unsigned char buf[2];
	#if 1
	switch (banding) {
	case CAM_BANDING_60HZ:
		i=0;
		while (regs60hz[i].addr!= 0xff && regs60hz[i].val!= 0xff)
		{
			buf[0]=regs60hz[i].addr;
			buf[1]=regs60hz[i].val;
			i2c_put_byte_add8(client,buf, 2);
			i++;
		}
		break;
	case CAM_BANDING_50HZ:
		i=0;
		while (regs50hz[i].addr!= 0xff && regs50hz[i].val!= 0xff)
		{
			buf[0]=regs50hz[i].addr;
			buf[1]=regs50hz[i].val;
			i2c_put_byte_add8(client,buf, 2);
			i++;
		}
		break;
	default:
		break;

	}
	#endif
}


/*************************************************************************
* FUNCTION
*	set_HI704_param_exposure
*
* DESCRIPTION
*	HI704 exposure setting.
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
void set_HI704_param_exposure(struct HI704_device *dev,enum camera_exposure_e para)//曝光调节
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	unsigned char buf1[2];
	unsigned char buf2[2];
#if 1
	switch (para) {
	case EXPOSURE_N4_STEP:
		buf1[0]=0x03;
		buf1[1]=0x10;
		buf2[0]=0x40;
		buf2[1]=0xc0;
		break;
	case EXPOSURE_N3_STEP:
		buf1[0]=0x03;
		buf1[1]=0x10;
		buf2[0]=0x40;
		buf2[1]=0xb0;
		break;
	case EXPOSURE_N2_STEP:
		buf1[0]=0x03;
		buf1[1]=0x10;
		buf2[0]=0x40;
		buf2[1]=0xa0;
		break;
	case EXPOSURE_N1_STEP:
		buf1[0]=0x03;
		buf1[1]=0x10;
		buf2[0]=0x40;
		buf2[1]=0x98;
		break;
	case EXPOSURE_0_STEP:
		buf1[0]=0x03;
		buf1[1]=0x10;
		buf2[0]=0x40;
		buf2[1]=0x80;
		break;
	case EXPOSURE_P1_STEP:
		buf1[0]=0x03;
		buf1[1]=0x10;
		buf2[0]=0x40;
		buf2[1]=0x10;
		break;
	case EXPOSURE_P2_STEP:
		buf1[0]=0x03;
		buf1[1]=0x10;
		buf2[0]=0x40;
		buf2[1]=0x20;
		break;
	case EXPOSURE_P3_STEP:
		buf1[0]=0x03;
		buf1[1]=0x10;
		buf2[0]=0x40;
		buf2[1]=0x30;
		break;
	case EXPOSURE_P4_STEP:
		buf1[0]=0x03;
		buf1[1]=0x10;
		buf2[0]=0x40;
		buf2[1]=0x40;
		break;
	default:
		buf1[0]=0x03;
		buf1[1]=0x10;
		buf2[0]=0x40;
		buf2[1]=0x50;
		break;
	}
	//msleep(300);
	i2c_put_byte_add8(client,buf1,2);
	i2c_put_byte_add8(client,buf2,2);
#endif
}

/*************************************************************************
* FUNCTION
*	set_HI704_param_effect
*
* DESCRIPTION
*	HI704 effect setting.
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
void set_HI704_param_effect(struct HI704_device *dev,enum camera_effect_flip_e para)//特效设置
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	int i;
	#if 1
	switch (para) {
	case CAM_EFFECT_ENC_NORMAL:
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03,0x10},
					{0x11,0x43},
					{0x12,0X30},                           
					{0x13,0x00},                            
					{0x44,0x80},                                        
					{0x45,0x80},                                    
					{0x47,0x7f}, 
					{0xff,0xff},
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}

		break;
	case CAM_EFFECT_ENC_GRAYSCALE:
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03 , 0x10},
					{0x11 , 0x03},
					{0x12 , 0x03},
					{0x13 , 0x02},
					{0x40 , 0x00},
					{0x44 , 0x80},
					{0x45 , 0x80},
					{0xff , 0xff},
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}
	    
	  

		break;
	case CAM_EFFECT_ENC_SEPIA:
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03 , 0x10},
					{0x11 , 0x03},
					{0x12 , 0x33},
					{0x13 , 0x02},
					{0x44 , 0x70},
					{0x45 , 0x98},
					{0xff , 0xff},
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}

		break;
	case CAM_EFFECT_ENC_COLORINV:
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03 , 0x10},
					{0x11 , 0x03},
					{0x12 , 0x08},
					{0x13 , 0x02},
					{0x14 , 0x00},
					{0xff , 0xff},
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}
		
		break;
	case CAM_EFFECT_ENC_SEPIAGREEN:
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03 , 0x10},
					{0x11 , 0x03},
					{0x12 , 0x03},
					{0x40 , 0x00},
					{0x13 , 0x02},
					{0x44 , 0x30},
					{0x45 , 0x50},
					{0xff , 0xff},
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}
		break;
	case CAM_EFFECT_ENC_SEPIABLUE:
		{
			struct aml_camera_i2c_fig_s regs[]=
				{
					{0x03 , 0x10},
					{0x11 , 0x03},
					{0x12 , 0x03},
					{0x40 , 0x00},
					{0x13 , 0x02},
					{0x44 , 0xb0},
					{0x45 , 0x40},
					{0xff , 0xff},
				};
				i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
		}

		break;
	default:
		break;
	}
#endif
}

unsigned char v4l_2_HI704(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int HI704_setting(struct HI704_device *dev,int PROP_ID,int value )
{
	int ret=0;
	//unsigned char cur_val;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_HI704(value));
		ret=i2c_put_byte(client,0x0201,v4l_2_HI704(value));
		break;
	case V4L2_CID_CONTRAST:
		//ret=i2c_put_byte(client,0x0200, value);
		break;
	case V4L2_CID_SATURATION:
		//ret=i2c_put_byte(client,0x0202, value);
		break;
#if 0
	case V4L2_CID_EXPOSURE:
		//ret=i2c_put_byte(client,0x0201, value);
		break;

	case V4L2_CID_HFLIP:    /* set flip on H. */
		//ret=i2c_get_byte(client,0x0101);
		if(ret>0) {
			cur_val=(char)ret;
			//if(value!=0)
			//	cur_val=cur_val|0x1;
			//else
			//	cur_val=cur_val&0xFE;
			//ret=i2c_put_byte(client,0x0101,cur_val);
			if(ret<0) dprintk(dev, 1, "V4L2_CID_HFLIP setting error\n");
		}  else {
			dprintk(dev, 1, "vertical read error\n");
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		//ret=i2c_get_byte(client,0x0101);
		if(ret>0) {
			cur_val=(char)ret;
			// if(value!=0)
			//	cur_val=cur_val|0x10;
			// else
			//	cur_val=cur_val&0xFD;
			//ret=i2c_put_byte(client,0x0101,cur_val);
		} else {
			dprintk(dev, 1, "vertical read error\n");
		}
		break;
#endif
	case V4L2_CID_DO_WHITE_BALANCE:
		if(HI704_qctrl[0].default_value!=value){
			HI704_qctrl[0].default_value=value;
			set_HI704_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
        	}
		break;
	case V4L2_CID_EXPOSURE:
		if(HI704_qctrl[1].default_value!=value){
			HI704_qctrl[1].default_value=value;
		      set_HI704_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
        	}
		break;
	case V4L2_CID_COLORFX:
		if(HI704_qctrl[2].default_value!=value){
			HI704_qctrl[2].default_value=value;
			set_HI704_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
        	}
		break;
	case V4L2_CID_WHITENESS:
		 if(HI704_qctrl[3].default_value!=value){
			HI704_qctrl[3].default_value=value;
			HI704_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
        	}
	case V4L2_CID_BLUE_BALANCE:
		if(HI704_qctrl[4].default_value!=value){
			HI704_qctrl[4].default_value=value;
			HI704_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
        	}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(HI704_qctrl[5].default_value!=value){
			HI704_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */         
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(HI704_qctrl[7].default_value!=value){
			HI704_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(HI704_qctrl[8].default_value!=value){
			HI704_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;
}

/*static void power_down_HI704(struct HI704_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	buf[0]=0x01;
	buf[1]=0xf1;
	i2c_put_byte_add8(client,buf,2);

	msleep(5);
	return;
}*/

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	15  //10 20120418
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void HI704_fillbuff(struct HI704_fh *fh, struct HI704_buffer *buf)
{
	struct HI704_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror =  HI704_qctrl[5].default_value&3;
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = HI704_qctrl[7].default_value;
	para.angle = HI704_qctrl[8].default_value;
	para.vaddr = (unsigned)vbuf;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void HI704_thread_tick(struct HI704_fh *fh)
{
	struct HI704_buffer *buf;
	struct HI704_device *dev = fh->dev;
	struct HI704_dmaqueue *dma_q = &dev->vidq;

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
			 struct HI704_buffer, vb.queue);
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
	HI704_fillbuff(fh, buf);
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

static void HI704_sleep(struct HI704_fh *fh)
{
	struct HI704_device *dev = fh->dev;
	struct HI704_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	HI704_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int HI704_thread(void *data)
{
	struct HI704_fh  *fh = data;
	struct HI704_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		HI704_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int HI704_start_thread(struct HI704_fh *fh)
{
	struct HI704_device *dev = fh->dev;
	struct HI704_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(HI704_thread, fh, "HI704");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void HI704_stop_thread(struct HI704_dmaqueue  *dma_q)
{
	struct HI704_device *dev = container_of(dma_q, struct HI704_device, vidq);

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
	struct HI704_fh  *fh = vq->priv_data;
	struct HI704_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct HI704_buffer *buf)
{
	struct HI704_fh  *fh = vq->priv_data;
	struct HI704_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

	videobuf_waiton(vq, &buf->vb, 0, 0);
	if (in_interrupt())
		BUG();

	videobuf_vmalloc_free(&buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 1920  //1024
#define norm_maxh() 1200  //768
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct HI704_fh     *fh  = vq->priv_data;
	struct HI704_device    *dev = fh->dev;
	struct HI704_buffer *buf = container_of(vb, struct HI704_buffer, vb);
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
	struct HI704_buffer    *buf  = container_of(vb, struct HI704_buffer, vb);
	struct HI704_fh        *fh   = vq->priv_data;
	struct HI704_device       *dev  = fh->dev;
	struct HI704_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct HI704_buffer   *buf  = container_of(vb, struct HI704_buffer, vb);
	struct HI704_fh       *fh   = vq->priv_data;
	struct HI704_device      *dev  = (struct HI704_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops HI704_video_qops = {
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
	struct HI704_fh  *fh  = priv;
	struct HI704_device *dev = fh->dev;

	strcpy(cap->driver, "HI704");
	strcpy(cap->card, "HI704");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = HI704_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct HI704_fmt *fmt;

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
	
	if(fival->index > ARRAY_SIZE(hi704_frmivalenum))
		return -EINVAL;
	
	for(k =0; k< ARRAY_SIZE(hi704_frmivalenum); k++)
	{
		if( (fival->index==hi704_frmivalenum[k].index)&&
				(fival->pixel_format ==hi704_frmivalenum[k].pixel_format )&&
				(fival->width==hi704_frmivalenum[k].width)&&
				(fival->height==hi704_frmivalenum[k].height)){
			memcpy( fival, &hi704_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
			return 0;
		}
	}
	
	return -EINVAL;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct HI704_fh *fh = priv;

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
	struct HI704_fh *fh = priv;
	struct HI704_device *dev = fh->dev;
	struct v4l2_captureparm *cp = &parms->parm.capture;
	
	dprintk(dev,3,"vidioc_g_parm\n");
	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	
	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	
	cp->timeperframe = hi704_frmintervals_active;
	printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
	cp->timeperframe.numerator );
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct HI704_fh  *fh  = priv;
	struct HI704_device *dev = fh->dev;
	struct HI704_fmt *fmt;
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

/*static int set_flip(struct HI704_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	unsigned char buf[2];
	buf[0] = 0x03;
	buf[1] = 0x00;
	if((i2c_put_byte_add8(client, buf, 2)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
	}
	temp = i2c_get_byte_add8(client, 0x11);
	temp &= 0xfc;
	temp |= dev->cam_info.m_flip << 0;
	temp |= dev->cam_info.v_flip << 1;
	buf[0] = 0x11;
	buf[1] = temp;
	if((i2c_put_byte_add8(client,buf, 2)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
	}
	return 0;
}*/

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct HI704_fh *fh = priv;
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

	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct HI704_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct HI704_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct HI704_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct HI704_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct HI704_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct HI704_fh  *fh = priv;
	struct HI704_device *dev = fh->dev;
	vdin_parm_t para;
	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para ));
	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;//TVIN_SIG_FMT_MAX+1;TVIN_SIG_FMT_CAMERA_1280X720P_30Hz
	para.frame_rate = hi704_frmintervals_active.denominator
				/hi704_frmintervals_active.numerator;//175
	para.h_active = 640;//640
	para.v_active = 480;//480
	para.hsync_phase = 1;//0
	para.vsync_phase  = 1;//1	
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
	para.skip_count =  2;//skip num
	para.bt_path = dev->cam_info.bt_path;
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
		vops->start_tvin_service(0,&para);
		fh->stream_on        = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct HI704_fh  *fh = priv;

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
	struct HI704_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(HI704_prev_resolution))
			return -EINVAL;
		frmsize = &HI704_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(HI704_pic_resolution))
			return -EINVAL;
		frmsize = &HI704_pic_resolution[fsize->index];
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
	struct HI704_fh *fh = priv;
	struct HI704_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct HI704_fh *fh = priv;
	struct HI704_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(HI704_qctrl); i++)
		if (qc->id && qc->id == HI704_qctrl[i].id) {
			memcpy(qc, &(HI704_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct HI704_fh *fh = priv;
	struct HI704_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(HI704_qctrl); i++)
		if (ctrl->id == HI704_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct HI704_fh *fh = priv;
	struct HI704_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(HI704_qctrl); i++)
		if (ctrl->id == HI704_qctrl[i].id) {
			if (ctrl->value < HI704_qctrl[i].minimum ||
			    ctrl->value > HI704_qctrl[i].maximum ||
			    HI704_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int HI704_open(struct file *file)
{
	struct HI704_device *dev = video_drvdata(file);
	struct HI704_fh *fh = NULL;
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
	
	HI704_init_regs(dev);
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
	fh->width    = 640;//640
	fh->height   = 480;//480
	fh->stream_on = 0 ;
	fh->f_flags = file->f_flags;
	/* Resets frame counters */
	dev->jiffies = jiffies;

//    TVIN_SIG_FMT_CAMERA_640X480P_30Hz,
//    TVIN_SIG_FMT_CAMERA_800X600P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1024X768P_30Hz, // 190
//    TVIN_SIG_FMT_CAMERA_1920X1080P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1280X720P_30Hz,

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &HI704_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct HI704_buffer), fh, NULL);

	HI704_start_thread(fh);

	return 0;
}

static ssize_t
HI704_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct HI704_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
HI704_poll(struct file *file, struct poll_table_struct *wait)
{
	struct HI704_fh        *fh = file->private_data;
	struct HI704_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int HI704_close(struct file *file)
{
	struct HI704_fh         *fh = file->private_data;
	struct HI704_device *dev       = fh->dev;
	struct HI704_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);

	HI704_stop_thread(vidq);
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
	HI704_qctrl[0].default_value=0;
	HI704_qctrl[1].default_value=4;
	HI704_qctrl[2].default_value=0;
	HI704_qctrl[3].default_value=0;
	HI704_qctrl[4].default_value=0;
	
	HI704_qctrl[5].default_value=0;
	HI704_qctrl[7].default_value=100;
	HI704_qctrl[8].default_value=0;

	//power_down_HI704(dev);
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

static int HI704_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct HI704_fh  *fh = file->private_data;
	struct HI704_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations HI704_fops = {
	.owner		= THIS_MODULE,
	.open           = HI704_open,
	.release        = HI704_close,
	.read           = HI704_read,
	.poll		= HI704_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = HI704_mmap,
};

static const struct v4l2_ioctl_ops HI704_ioctl_ops = {
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

static struct video_device HI704_template = {
	.name		= "HI704_v4l",
	.fops           = &HI704_fops,
	.ioctl_ops 	= &HI704_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int HI704_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_HI704, 0);
}

static const struct v4l2_subdev_core_ops HI704_core_ops = {
	.g_chip_ident = HI704_g_chip_ident,
};

static const struct v4l2_subdev_ops HI704_ops = {
	.core = &HI704_core_ops,
};

static int HI704_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	aml_cam_info_t* plat_dat;
	int err;
	struct HI704_device *t;
	struct v4l2_subdev *sd;
	vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &HI704_ops);
	
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
	memcpy(t->vdev, &HI704_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);

	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "hi704");
	/* Register it */
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if (plat_dat->front_back >=0)
			video_nr = plat_dat->front_back;
	} else {
		printk("camera hi253: have no platform data\n");
		kfree(t);
		kfree(client);
		return -1;
	}
	
	t->cam_info.version = HI704_DRIVER_VERSION;
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

static int HI704_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct HI704_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id HI704_id[] = {
	{ "HI704", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, HI704_id);

static struct i2c_driver HI704_i2c_driver = {
	.driver = {
		.name = "HI704",
	},
	.probe = HI704_probe,
	.remove = HI704_remove,
	.id_table = HI704_id,
};

module_i2c_driver(HI704_i2c_driver);

