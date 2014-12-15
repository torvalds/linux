/*
 *NT99340 - This code emulates a real video device with v4l2 api
 *Amlogic 2012-04-24
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
#include <mach/gpio.h>
#include <linux/amlogic/camera/aml_cam_info.h>
#include <linux/amlogic/vmapi.h>
#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include "common/plat_ctrl.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif

#define NT99340_CAMERA_MODULE_NAME "nt99340"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define NT99340_CAMERA_MAJOR_VERSION 0
#define NT99340_CAMERA_MINOR_VERSION 7
#define NT99340_CAMERA_RELEASE 0
#define NT99340_CAMERA_VERSION \
	KERNEL_VERSION(NT99340_CAMERA_MAJOR_VERSION, NT99340_CAMERA_MINOR_VERSION, NT99340_CAMERA_RELEASE)
	
#define NT99340_DRIVER_VERSION "NT99340-COMMON-01-140717"

MODULE_DESCRIPTION("nt99340 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");
static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */
static int camera_wb_state = 0;

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static int vidio_set_fmt_ticks=0;

//extern int disable_nt99340;

static int nt99340_h_active=800;
static int nt99340_v_active=598;

static struct i2c_client *this_client;
static struct v4l2_fract nt99340_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};
static struct vdin_v4l2_ops_s *vops;
/* supported controls */
static struct v4l2_queryctrl nt99340_qctrl[] = {
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

#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)
static struct v4l2_frmivalenum nt99340_frmivalenum[]={
	 {
		 .index 		= 0,
		 .pixel_format	= V4L2_PIX_FMT_NV21,
		 .width 	= 640,
		 .height		= 480,
		 .type		= V4L2_FRMIVAL_TYPE_DISCRETE,
		 {
			 .discrete	={
				 .numerator = 1,
				 .denominator	= 15,
			 }
		 }
	 },{
		 .index 		= 1,
		 .pixel_format	= V4L2_PIX_FMT_NV21,
		 .width 	= 1600,
		 .height		= 1200,
		 .type		= V4L2_FRMIVAL_TYPE_DISCRETE,
		 {
			 .discrete	={
				 .numerator = 1,
				 .denominator	= 5,
			 }
		 }
	 },{
		 .index 		= 1,
		 .pixel_format	= V4L2_PIX_FMT_NV21,
		 .width 	= 2592,
		 .height		= 1944,
		 .type		= V4L2_FRMIVAL_TYPE_DISCRETE,
		 {
			 .discrete	={
				 .numerator = 1,
				 .denominator	= 5,
			 }
		 }
	 },
};

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct nt99340_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct nt99340_fmt formats[] = {
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
	},{
		.name     = "YVU420P",
		.fourcc   = V4L2_PIX_FMT_YVU420,
		.depth    = 12,
	}
};

static struct nt99340_fmt *get_format(struct v4l2_format *f)
{
	struct nt99340_fmt *fmt;
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
struct nt99340_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct nt99340_fmt        *fmt;
};

struct nt99340_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(nt99340_devicelist);

struct nt99340_device {
	struct list_head			nt99340_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct nt99340_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t  cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(nt99340_qctrl)];
};

static inline struct nt99340_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct nt99340_device, sd);
}

struct nt99340_fh {
	struct nt99340_device            *dev;

	/* video capture */
	struct nt99340_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int   f_flags;
};

/*static inline struct nt99340_fh *to_fh(struct nt99340_device *dev)
{
	return container_of(dev, struct nt99340_fh, dev);
}*/

static struct v4l2_frmsize_discrete nt99340_prev_resolution[]=
{
	{320,240},
	{352,288},
	{640,480}, //{800,600},
};

static struct v4l2_frmsize_discrete nt99340_pic_resolution[]=
{		
	{800,600},
	//{2048,1536},
	{2592, 1944},
};

/*------------------------------------------------------------------
	reg spec of NT99340---2012-0420 [2012-10-19 Ver_01]
------------------------------------------------------------------*/

struct aml_camera_i2c_fig_s NT99340_script[] = {
	// initial
	{0x32F0, 0x03}, // Output Format
	{0x306a, 0x02}, // pclk
	{0x32FF, 0x00},
	{0x303E, 0x0C}, // Sen_DPC_En, LSC_Clamp
	{0x303F, 0x02},
	{0x3040, 0xFF},
	{0x3041, 0x00},
	{0x3042, 0x00}, // ABLC_Base
	{0x3051, 0xFC}, // ABLC_GainW
	{0x3052, 0x10}, // DBLC by ABLC
	{0x3118, 0xA7}, // Pixel Setting
	{0x3119, 0xA7},
	{0x311A, 0xA7},
	{0x3100, 0x03}, // Analog Setting
	{0x3101, 0x80},
	{0x3102, 0x09},
	{0x3103, 0x09},
	{0x3104, 0x01},
	{0x3105, 0x01}, // ADC_VC
	{0x3106, 0x03},
	{0x3107, 0x20},
	{0x3108, 0x00},
	{0x3109, 0x02}, // Disable LDO
	{0x310A, 0x04},
	{0x3127, 0x01}, // Lb_Smp2se_interval
	{0x3030, 0x0A}, // Init VCM
	{0x306C, 0x0C},
	{0x306D, 0x0C},
	{0x3583, 0x18},
	{0x3587, 0x02},
	{0x3584, 0x00},
	{0x3585, 0x00},
	{0x3580, 0xB2},
	{0x3600, 0x66}, // Init AFC
	{0x3601, 0x99},
	{0x3604, 0x15},
	{0x3605, 0x14},
	{0x3614, 0x20},
	{0x361A, 0x10},
	{0x3615, 0x24},
	{0x3610, 0x02},
	{0x3290, 0x01}, // Init AWB Gain
	{0x3291, 0x80},
	{0x3296, 0x01},
	{0x3297, 0x60},
	{0x3250, 0x80}, // CA Zone
	{0x3251, 0x01},
	{0x3252, 0x27},
	{0x3253, 0x9D},
	{0x3254, 0x00},
	{0x3255, 0xE3},
	{0x3256, 0x81}, 
	{0x3257, 0x70},
	{0x329B, 0x00}, // AWB Gain Limit
	{0x32A1, 0x00},
	{0x32A2, 0xEC},
	{0x32A3, 0x01},
	{0x32A4, 0x7A},
	{0x32A5, 0x01},
	{0x32A6, 0x14},
	{0x32A7, 0x01},
	{0x32A8, 0xB3},
	{0x3270, 0x00}, // Gamma9712
	{0x3271, 0x0C},
	{0x3272, 0x18},
	{0x3273, 0x32},
	{0x3274, 0x44},
	{0x3275, 0x54},
	{0x3276, 0x70},
	{0x3277, 0x88},
	{0x3278, 0x9D},
	{0x3279, 0xB0},
	{0x327A, 0xCF},
	{0x327B, 0xE2},
	{0x327C, 0xEF},
	{0x327D, 0xF7},
	{0x327E, 0xFF},
	{0x3302, 0x00}, // Color Correction
	{0x3303, 0x4D},
	{0x3304, 0x00},
	{0x3305, 0x96},
	{0x3306, 0x00},
	{0x3307, 0x1D},
	{0x3308, 0x07}, 
	{0x3309, 0xC7}, 
	{0x330A, 0x07}, 
	{0x330B, 0x0A}, 
	{0x330C, 0x01}, 
	{0x330D, 0x30}, 
	{0x330E, 0x01}, 
	{0x330F, 0x07}, 
	{0x3310, 0x06}, 
	{0x3311, 0xFF}, 
	{0x3312, 0x07}, 
	{0x3313, 0xFA}, 
	{0x3210, 0x1E}, // Lens Correction
	{0x3211, 0x20},
	{0x3212, 0x20},
	{0x3213, 0x1E},
	{0x3214, 0x18},
	{0x3215, 0x1A},
	{0x3216, 0x1A},
	{0x3217, 0x18},
	{0x3218, 0x18},
	{0x3219, 0x1A},
	{0x321A, 0x1A},
	{0x321B, 0x18},
	{0x321C, 0x16},
	{0x321D, 0x1A},
	{0x321E, 0x17},
	{0x321F, 0x15},
	{0x3234, 0x0F},
	{0x3235, 0x0F},
	{0x3236, 0x0F},
	{0x3237, 0x0F},
	{0x3238, 0x40},
	{0x3239, 0x40},
	{0x323A, 0x40},
	{0x3241, 0x40}, // EV Boundary
	{0x3243, 0x41},
	{0x32F6, 0x0F}, // Special Effect
	{0x32F9, 0x84}, // Chroma Map
	{0x32FA, 0x48},
	{0x3338, 0x18}, // Dark Chroma Map
	{0x3339, 0xC6},
	{0x333A, 0x6C},
	{0x3335, 0x60}, // VA Threshold for Color Shading
	{0x3336, 0x20},
	{0x3337, 0x40},
	{0x333B, 0xC6}, // Special CMP
	{0x333C, 0x6C},
	{0x3325, 0xAF},
	{0x3326, 0x02}, // EExt_Div
	{0x3327, 0x00}, // Default EExt
	{0x3331, 0x08},
	{0x3332, 0xFF},
	{0x333F, 0x07}, // AThr Disable
	{0x334A, 0x34}, // Green Filter
	{0x334B, 0x14},
	{0x334C, 0x10},
	{0x3363, 0x33}, // IQ_Auto_Control
	{0x3360, 0x08}, // IQ Param Sel
	{0x3361, 0x10}, 
	{0x3362, 0x18},
	{0x3364, 0x88}, // CC_Ctrl
	{0x3365, 0x80},
	{0x3366, 0x68},
	{0x3367, 0x40},
	{0x3368, 0x50}, // Edge Enhancement
	{0x3369, 0x40},
	{0x336A, 0x30},
	{0x336B, 0x20},  
	{0x336C, 0x00},
	{0x336D, 0x1A}, // DPC Ratio
	{0x336E, 0x16}, 
	{0x336F, 0x14}, 
	{0x3370, 0x0C},
	{0x3371, 0x3F}, // NR Weight
	{0x3372, 0x3F}, 
	{0x3373, 0x3F}, 
	{0x3374, 0x3F},
	{0x3375, 0x0E}, // NR Thr
	{0x3376, 0x10}, 
	{0x3377, 0x14}, 
	{0x3378, 0x30}, 
	{0x3012, 0x02},                                        
	{0x3013, 0x58}, // 1/20 Sec (5x Flicker Base), 1/50: 0x00F0, 1/10: 0x04B0
	{0x301D, 0x01},
	
	//SVGA
	{0x32BF, 0x60},
	{0x32C0, 0x78},
	{0x32C1, 0x78},
	{0x32C2, 0x78},
	{0x32C3, 0x00},
	{0x32C4, 0x20},
	{0x32C5, 0x20},
	{0x32C6, 0x20},
	{0x32C7, 0x00},
	{0x32C8, 0x78},
	{0x32C9, 0x78},
	{0x32CA, 0x98},
	{0x32CB, 0x98},
	{0x32CC, 0x98},
	{0x32CD, 0x98},
	{0x32DB, 0x6E},
	{0x32D0, 0x01},
	{0x32E0, 0x03},
	{0x32E1, 0x20},
	{0x32E2, 0x02},
	{0x32E3, 0x58},
	{0x32E4, 0x00},
	{0x32E5, 0x48},
	{0x32E6, 0x00},
	{0x32E7, 0x48},
	{0x3200, 0x3F},
	{0x3201, 0x0F},
	{0x302A, 0x00},
	{0x302B, 0x01},
	{0x302C, 0x07},
	{0x302D, 0x00},
	{0x302E, 0x00},
	{0x302F, 0x02},
	{0x3022, 0x24},
	{0x3023, 0x6E},
	{0x3128, 0x01},
	{0x3002, 0x00},
	{0x3003, 0x04},
	{0x3004, 0x00},
	{0x3005, 0x04},
	{0x3006, 0x08},
	{0x3007, 0x03},
	{0x3008, 0x06},
	{0x3009, 0x03},
	{0x300A, 0x07},
	{0x300B, 0xD0},
	{0x300C, 0x03},
	{0x300D, 0x20},
	{0x300E, 0x04},
	{0x300F, 0x00},
	{0x3010, 0x03},
	{0x3011, 0x00},
	{0x3052, 0x10},
	{0x32BB, 0x87},
	{0x32B8, 0x3B},
	{0x32B9, 0x2D},
	{0x32BC, 0x34},
	{0x32BD, 0x38},
	{0x32BE, 0x30},
	{0x3201, 0xFF},
	{0x3109, 0x02},
	{0x3530, 0xC0},
	{0x320A, 0x42},
	{0x3021, 0x06},
	{0x3060, 0x01},
	
	{0xffff,0xff}	
};

int NT99340_preview(struct nt99340_device *dev)
{
	// set NT99340 to preview mode
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp_reg;
	int i=0;

	int regPreview[] =
	{
		//SVGA
		0x32BF, 0x60,
		0x32C0, 0x78,
		0x32C1, 0x78,
		0x32C2, 0x78,
		0x32C3, 0x00,
		0x32C4, 0x20,
		0x32C5, 0x20,
		0x32C6, 0x20,
		0x32C7, 0x00,
		0x32C8, 0x78,
		0x32C9, 0x78,
		0x32CA, 0x98,
		0x32CB, 0x98,
		0x32CC, 0x98,
		0x32CD, 0x98,
		0x32DB, 0x6E,
		0x32D0, 0x01,
		0x32E0, 0x03,
		0x32E1, 0x20,
		0x32E2, 0x02,
		0x32E3, 0x58,
		0x32E4, 0x00,
		0x32E5, 0x48,
		0x32E6, 0x00,
		0x32E7, 0x48,
		0x3200, 0x3F,
		0x3201, 0x0F,
		0x302A, 0x00,
		0x302B, 0x01,
		0x302C, 0x07,
		0x302D, 0x00,
		0x302E, 0x00,
		0x302F, 0x02,
		0x3022, 0x24,
		0x3023, 0x6E,
		0x3128, 0x01,
		0x3002, 0x00,
		0x3003, 0x04,
		0x3004, 0x00,
		0x3005, 0x04,
		0x3006, 0x08,
		0x3007, 0x03,
		0x3008, 0x06,
		0x3009, 0x03,
		0x300A, 0x07,
		0x300B, 0xD0,
		0x300C, 0x03,
		0x300D, 0x20,
		0x300E, 0x04,
		0x300F, 0x00,
		0x3010, 0x03,
		0x3011, 0x00,
		0x3052, 0x10,
		0x32BB, 0x87,
		0x32B8, 0x3B,
		0x32B9, 0x2D,
		0x32BC, 0x34,
		0x32BD, 0x38,
		0x32BE, 0x30,
		0x3201, 0xFF,
		0x3109, 0x02,
		0x3530, 0xC0,
		0x320A, 0x42,
		0x3021, 0x06,
		0x3060, 0x01,
	};
//	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	//For test
	temp_reg=i2c_get_byte(client,0x3201);
	//printk("NT99340_preview(BBBBB): NT99340_get_AE_AWB_3201=%x\n",temp_reg);

	// Write preview table
	for (i=0; i<sizeof(regPreview)/sizeof(int); i+=2)
	{
		i2c_put_byte(client,regPreview[i], regPreview[i+1]);
	}
		
	temp_reg=i2c_get_byte(client,0x3201);
	if(camera_wb_state)
	{	
		i2c_put_byte(client,0x3201, temp_reg&~0x10);  // select manual WB
	}
	else
	{
		i2c_put_byte(client,0x3201, temp_reg|0x10);   // select Auto WB
	}
	temp_reg=i2c_get_byte(client,0x3201);
	//printk("_GJL_  NT99340_get_AE_AWB_3201=%x\n",temp_reg);
	
	return 0;
}
int NT99340_capture(struct nt99340_device *dev)
{
	// set NT99340 to preview mode
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int i=0;

	int regCapture[] =
	{
		//2048*1536
		0x32BF, 0x40,
		0x32C0, 0x84,
		0x32C1, 0x84,
		0x32C2, 0x84,
		0x32C3, 0x00,
		0x32C4, 0x20,
		0x32C5, 0x20,
		0x32C6, 0x20,
		0x32C7, 0x00,
		0x32C8, 0x4F,
		0x32C9, 0x84,
		0x32CA, 0xA4,
		0x32CB, 0xA4,
		0x32CC, 0xA4,
		0x32CD, 0xA4,
		0x32DB, 0x63,
		0x32D0, 0x01,
	    0x32E0, 0x08,
	    0x32E1, 0x00,
	    0x32E2, 0x06,
	    0x32E3, 0x00,
	    0x32E4, 0x00,
	    0x32E5, 0x00,
	    0x32E6, 0x00,
	    0x32E7, 0x00,
		0x3200, 0x3F,
		
		0x302A, 0x00,
		0x302B, 0x01,
		0x302C, 0x07,
		0x302D, 0x00,
		0x302E, 0x00,
		0x302F, 0x02,
		0x3022, 0x24,
		0x3023, 0x24,
		0x3128, 0x01,
		0x3002, 0x00,
		0x3003, 0x04,
		0x3004, 0x00,
		0x3005, 0x04,
		0x3006, 0x08,
		0x3007, 0x03,
		0x3008, 0x06,
		0x3009, 0x03,
		0x300A, 0x0B,
		0x300B, 0xD0,
		0x300C, 0x06,
		0x300D, 0x33,
		0x300E, 0x08,
		0x300F, 0x00,
		0x3010, 0x06,
		0x3011, 0x00,
		0x3052, 0x10,
		0x32BB, 0x87,
		0x32B8, 0x3B,
		0x32B9, 0x2D,
		0x32BC, 0x34,     
		0x32BD, 0x38,
		0x32BE, 0x30,
		0x3201, 0xBF,
		0x3109, 0x02,
		0x3530, 0xC0,
		0x320A, 0x42,
		0x3021, 0x06,
		0x3060, 0x01,		
	};
		
	// Write preview table
	for (i=0; i<sizeof(regCapture)/sizeof(int); i+=2)
	{
	i2c_put_byte(client, regCapture[i], regCapture[i+1]);
	}
		
	msleep(300);	
	return 0;
}

//load NT99340 parameters
static void NT99340_init_regs(struct nt99340_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    int i=0;

    while(1)
    {
        if (NT99340_script[i].val==0xff&&NT99340_script[i].addr==0xffff)
        {
        	printk("NT99340_write_regs success in initial NT99340.\n");
        	break;
        }
        if((i2c_put_byte(client,NT99340_script[i].addr, NT99340_script[i].val)) < 0)
        {
        	printk("fail in initial NT99340. ");
		return;
		}
		i++;
    }
    return;
}
/*************************************************************************
* FUNCTION
*    NT99340_set_param_wb
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
void NT99340_set_param_wb(struct nt99340_device *dev,enum  camera_wb_flip_e para)//white balance
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	int temp_reg=i2c_get_byte(client,0x3201);

	camera_wb_state = para;
    switch (para)
	{

		case CAM_WB_AUTO://auto{
			i2c_put_byte(client,0x3201, temp_reg|0x10);   // select Auto WB
			break;

		case CAM_WB_CLOUD: /* Cloudy Colour Temperature : 6500K - 8000K  */		
			i2c_put_byte(client,0x3201, temp_reg&~0x10);  // select manual WB
			i2c_put_byte(client,0x3290, 0x01);
			i2c_put_byte(client,0x3291, 0x51);
			i2c_put_byte(client,0x3296, 0x01);
			i2c_put_byte(client,0x3297, 0x00);
			break;

		case CAM_WB_DAYLIGHT: /* ClearDay Colour Temperature : 5000K - 6500K	*/	
			i2c_put_byte(client,0x3201, temp_reg&~0x10);  // select manual WB
			i2c_put_byte(client,0x3290, 0x01);
			i2c_put_byte(client,0x3291, 0x38);
			i2c_put_byte(client,0x3296, 0x01);
			i2c_put_byte(client,0x3297, 0x68);
			break;

		case CAM_WB_INCANDESCENCE:
			i2c_put_byte(client,0x3201, temp_reg&~0x10);  // select manual WB
			i2c_put_byte(client,0x3290, 0x01);
			i2c_put_byte(client,0x3291, 0x30);
			i2c_put_byte(client,0x3296, 0x01);
			i2c_put_byte(client,0x3297, 0xcb);
			break;

		case CAM_WB_TUNGSTEN: /* Office Colour Temperature : 3500K - 5000K  */	
			i2c_put_byte(client,0x3201, temp_reg&~0x10);  // select manual WB
			i2c_put_byte(client,0x3290, 0x01);
			i2c_put_byte(client,0x3291, 0x00);
			i2c_put_byte(client,0x3296, 0x02);
			i2c_put_byte(client,0x3297, 0x30);
			break;

      	case CAM_WB_FLUORESCENT:			
			i2c_put_byte(client,0x3201, temp_reg&~0x10);  // select manual WB
			i2c_put_byte(client,0x3290, 0x01);
			i2c_put_byte(client,0x3291, 0x70);
			i2c_put_byte(client,0x3296, 0x01);
			i2c_put_byte(client,0x3297, 0xff);
			break;

		case CAM_WB_MANUAL:
		    // TODO
			break;
		default:
			break;
	}
} /* NT99340_set_param_wb */

/*************************************************************************
* FUNCTION
*    NT99340_set_param_exposure
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
void NT99340_set_param_exposure(struct nt99340_device *dev,enum camera_exposure_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	switch (para)
	{
		case EXPOSURE_N4_STEP:
			i2c_put_byte(client,0x32f2, 0x40);
			break;
		case EXPOSURE_N3_STEP:
			i2c_put_byte(client,0x32f2, 0x50);
			break;
		case EXPOSURE_N2_STEP:
			i2c_put_byte(client,0x32f2, 0x60);
			break;
		case EXPOSURE_N1_STEP:
			i2c_put_byte(client,0x32f2, 0x70);
			break;
		case EXPOSURE_0_STEP:
			i2c_put_byte(client,0x32f2, 0x80);
			break;
		case EXPOSURE_P1_STEP:
			i2c_put_byte(client,0x32f2, 0x90);
			break;
		case EXPOSURE_P2_STEP:
			i2c_put_byte(client,0x32f2, 0xa0);
			break;
		case EXPOSURE_P3_STEP:
			i2c_put_byte(client,0x32f2, 0xb0);
			break;
		case EXPOSURE_P4_STEP:
			i2c_put_byte(client,0x32f2, 0xc0);
			break;
		default:
			i2c_put_byte(client,0x32f2, 0x80);
			break;	

	}


} /* NT99340_set_param_exposure */
/*************************************************************************
* FUNCTION
*    NT99340_set_param_effect
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
void NT99340_set_param_effect(struct nt99340_device *dev,enum camera_effect_flip_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);


    switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL:
			i2c_put_byte(client,0x32f1, 0x00);
			break;

		case CAM_EFFECT_ENC_GRAYSCALE:
			i2c_put_byte(client,0x32f1, 0x01);
			break;

		case CAM_EFFECT_ENC_SEPIA:
			i2c_put_byte(client,0x32f1, 0x02);
			break;

		case CAM_EFFECT_ENC_SEPIAGREEN:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f4, 0x60);
			i2c_put_byte(client,0x32f5, 0x20);
			break;

		case CAM_EFFECT_ENC_SEPIABLUE:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f4, 0xF0);
			i2c_put_byte(client,0x32f5, 0x80);
			break;

		case CAM_EFFECT_ENC_COLORINV:
			i2c_put_byte(client,0x32f1, 0x03);
			break;

		default:
			break;
	}
} /* NT99340_set_param_effect */

/*************************************************************************
* FUNCTION
*    NT99340_NightMode
*
* DESCRIPTION
*    This function night mode of NT99340.
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
void NT99340_set_night_mode(struct nt99340_device *dev,enum  camera_night_mode_flip_e enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//int temp;
	if (enable) {
		i2c_put_byte(client, 0x32C4, 0x28);
		i2c_put_byte(client, 0x302A, 0x04);
	} else {
		i2c_put_byte(client, 0x32C4, 0x20);
		i2c_put_byte(client, 0x302A, 0x00);
	}

}    /* NT99340_NightMode */
void NT99340_set_param_banding(struct nt99340_device *dev,enum  camera_banding_flip_e banding)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);	
	//int temp;
	switch(banding) {
	case CAM_BANDING_50HZ:			
		i2c_put_byte(client, 0x32BF, 0x60);
		i2c_put_byte(client, 0x32C0, 0x78);
		i2c_put_byte(client, 0x32C1, 0x78);
		i2c_put_byte(client, 0x32C2, 0x78);
		i2c_put_byte(client, 0x32C3, 0x00);
		i2c_put_byte(client, 0x32C4, 0x20);
		i2c_put_byte(client, 0x32C5, 0x20);
		i2c_put_byte(client, 0x32C6, 0x20);
		i2c_put_byte(client, 0x32C7, 0x00);
		i2c_put_byte(client, 0x32C8, 0x78);
		i2c_put_byte(client, 0x32C9, 0x78);
		i2c_put_byte(client, 0x32CA, 0x98);
		i2c_put_byte(client, 0x32CB, 0x98);
		i2c_put_byte(client, 0x32CC, 0x98);
		i2c_put_byte(client, 0x32CD, 0x98);
		i2c_put_byte(client, 0x32DB, 0x6E);
		i2c_put_byte(client, 0x32D0, 0x01);
		printk(KERN_INFO "banding 50 in\n");			
		break;
	case CAM_BANDING_60HZ:			
		i2c_put_byte(client, 0x32BF, 0x60);
		i2c_put_byte(client, 0x32C0, 0x7C);
		i2c_put_byte(client, 0x32C1, 0x7C);
		i2c_put_byte(client, 0x32C2, 0x7C);
		i2c_put_byte(client, 0x32C3, 0x00);
		i2c_put_byte(client, 0x32C4, 0x20);
		i2c_put_byte(client, 0x32C5, 0x20);
		i2c_put_byte(client, 0x32C6, 0x20);
		i2c_put_byte(client, 0x32C7, 0x00);
		i2c_put_byte(client, 0x32C8, 0x64);
		i2c_put_byte(client, 0x32C9, 0x7C);
		i2c_put_byte(client, 0x32CA, 0x9C);
		i2c_put_byte(client, 0x32CB, 0x9C);
		i2c_put_byte(client, 0x32CC, 0x9C);
		i2c_put_byte(client, 0x32CD, 0x9C);
		i2c_put_byte(client, 0x32DB, 0x68);
		i2c_put_byte(client, 0x32D0, 0x01);
		printk(KERN_INFO " banding 60 in\n ");				
		break;
	default:
			break;
	}
}

static int set_flip(struct nt99340_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	i2c_put_byte(client, 0x3021, 0x60);
	temp = i2c_get_byte(client, 0x3022);
	temp &= 0xfc;
	temp |= dev->cam_info.m_flip << 1;
	temp |= dev->cam_info.v_flip << 0;
        if((i2c_put_byte(client, 0x3022, temp)) < 0) {
	     printk("fail in setting sensor orientation\n");
	     return -1;
        }
        return 0;
}

void NT99340_set_resolution(struct nt99340_device *dev,int height,int width)
{

	//int ret;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

#if 1	 
	if(height&&width&&(height<=1536)&&(width<=2048))
	{		
	  if((height<=600)&&(width<=800))
	  {
	    #if 1
			NT99340_preview(dev);
                        nt99340_frmintervals_active.denominator = 15;
                        nt99340_frmintervals_active.numerator	= 1;
			nt99340_h_active=800;
			nt99340_v_active=598;		
			#endif			
		}
		else
		{
			#if 1
		       NT99340_capture(dev);
			#endif
			msleep(10);// 10
                        nt99340_frmintervals_active.denominator = 5;
                        nt99340_frmintervals_active.numerator	= 1;
			nt99340_h_active=2048;
			nt99340_v_active=1534;
		}
	}
#else
	i2c_put_byte(client,0x32e0, 0x03);
	i2c_put_byte(client,0x32e1, 0x20);
	i2c_put_byte(client,0x32e2 ,0x02);
	i2c_put_byte(client,0x32e3, 0x58);//	
#endif
       set_flip(dev);
}    /* NT99340_set_resolution */

unsigned char v4l_2_nt99340(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int nt99340_setting(struct nt99340_device *dev,int PROP_ID,int value )
{
	int ret=0;
	//unsigned char cur_val;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		//dprintk(dev, 1, "setting brightned:%d\n",v4l_2_NT99340(value));
		//ret=i2c_put_byte(client,0x0201,v4l_2_nt99340(value));
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
		ret=i2c_get_byte(client,0x3022);
		if(ret>0) {
			cur_val=(char)ret;
			if(value!=0)
				cur_val=cur_val|0x2;
			else
				cur_val=cur_val&0xFD;
			ret=i2c_put_byte(client,0x3022,cur_val);
			if(ret<0) dprintk(dev, 1, "V4L2_CID_HFLIP setting error\n");
		}  else {
			dprintk(dev, 1, "vertical read error\n");
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		ret=i2c_get_byte(client,0x3022);
		if(ret>0) {
			cur_val=(char)ret;
			if(value!=0)
				cur_val=cur_val|0x01;
			else
				cur_val=cur_val&0xFE;
			ret=i2c_put_byte(client,0x3022,cur_val);
		} else {
			//dprintk(dev, 1, "vertical read error\n");
		}
		break;
#endif
	case V4L2_CID_DO_WHITE_BALANCE:
    if(nt99340_qctrl[0].default_value!=value){
			nt99340_qctrl[0].default_value=value;
			NT99340_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
    }
		break;
	case V4L2_CID_EXPOSURE:
        if(nt99340_qctrl[1].default_value!=value){
			nt99340_qctrl[1].default_value=value;
			NT99340_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
        	}
		break;
	case V4L2_CID_COLORFX:
        if(nt99340_qctrl[2].default_value!=value){
			nt99340_qctrl[2].default_value=value;
			NT99340_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
        	}
		break;
	case V4L2_CID_WHITENESS:
		 if(nt99340_qctrl[3].default_value!=value){
			nt99340_qctrl[3].default_value=value;
			NT99340_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		break;
	case V4L2_CID_BLUE_BALANCE:
		if(nt99340_qctrl[4].default_value!=value){
			nt99340_qctrl[4].default_value=value;
			NT99340_set_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(nt99340_qctrl[5].default_value!=value){
			nt99340_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(nt99340_qctrl[7].default_value!=value){
			nt99340_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(nt99340_qctrl[8].default_value!=value){
			nt99340_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;

}

static void power_down_nt99340(struct nt99340_device *dev)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//i2c_put_byte(client,0x3012, 0x80);
	msleep(5);
	//i2c_put_byte(client,0x30ab, 0x00);
	//i2c_put_byte(client,0x30ad, 0x0a);
	//i2c_put_byte(client,0x30ae, 0x27);
	//i2c_put_byte(client,0x363b, 0x01);
}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void nt99340_fillbuff(struct nt99340_fh *fh, struct nt99340_buffer *buf)
{
	struct nt99340_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = nt99340_qctrl[5].default_value&3;// not set
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = nt99340_qctrl[7].default_value;
	para.angle = nt99340_qctrl[8].default_value;
	para.vaddr = (unsigned)vbuf;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void nt99340_thread_tick(struct nt99340_fh *fh)
{
	struct nt99340_buffer *buf;
	struct nt99340_device *dev = fh->dev;
	struct nt99340_dmaqueue *dma_q = &dev->vidq;

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
			 struct nt99340_buffer, vb.queue);
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
	nt99340_fillbuff(fh, buf);
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

static void nt99340_sleep(struct nt99340_fh *fh)
{
	struct nt99340_device *dev = fh->dev;
	struct nt99340_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	nt99340_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int nt99340_thread(void *data)
{
	struct nt99340_fh  *fh = data;
	struct nt99340_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		nt99340_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int nt99340_start_thread(struct nt99340_fh *fh)
{
	struct nt99340_device *dev = fh->dev;
	struct nt99340_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(nt99340_thread, fh, "nt99340");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void nt99340_stop_thread(struct nt99340_dmaqueue  *dma_q)
{
	struct nt99340_device *dev = container_of(dma_q, struct nt99340_device, vidq);

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
	struct nt99340_fh  *fh = vq->priv_data;
	struct nt99340_device *dev  = fh->dev;
    //int bytes = fh->fmt->depth >> 3 ;
	*size = (fh->width*fh->height*fh->fmt->depth)>>3;
	if (0 == *count)
		*count = 32;

	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	dprintk(dev, 1, "%s, count=%d, size=%d\n", __func__,
		*count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct nt99340_buffer *buf)
{
	struct nt99340_fh  *fh = vq->priv_data;
	struct nt99340_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);
        videobuf_waiton(vq, &buf->vb, 0, 0);
	if (in_interrupt())
		BUG();

	videobuf_vmalloc_free(&buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 3000
#define norm_maxh() 3000
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct nt99340_fh     *fh  = vq->priv_data;
	struct nt99340_device    *dev = fh->dev;
	struct nt99340_buffer *buf = container_of(vb, struct nt99340_buffer, vb);
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
	struct nt99340_buffer    *buf  = container_of(vb, struct nt99340_buffer, vb);
	struct nt99340_fh        *fh   = vq->priv_data;
	struct nt99340_device       *dev  = fh->dev;
	struct nt99340_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct nt99340_buffer   *buf  = container_of(vb, struct nt99340_buffer, vb);
	struct nt99340_fh       *fh   = vq->priv_data;
	struct nt99340_device      *dev  = (struct nt99340_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops nt99340_video_qops = {
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
	struct nt99340_fh  *fh  = priv;
	struct nt99340_device *dev = fh->dev;

	strcpy(cap->driver, "nt99340");
	strcpy(cap->card, "nt99340");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = NT99340_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct nt99340_fmt *fmt;

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

    if(fival->index > ARRAY_SIZE(nt99340_frmivalenum))
        return -EINVAL;
    for(k =0; k< ARRAY_SIZE(nt99340_frmivalenum); k++)
    {
        if( (fival->index==nt99340_frmivalenum[k].index)&&
                (fival->pixel_format ==nt99340_frmivalenum[k].pixel_format )&&
                (fival->width==nt99340_frmivalenum[k].width)&&
                (fival->height==nt99340_frmivalenum[k].height)){
            memcpy( fival, &nt99340_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
            return 0;
        }
    }

    return -EINVAL;

}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct nt99340_fh *fh = priv;

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
	struct nt99340_fh  *fh  = priv;
	struct nt99340_device *dev = fh->dev;
	struct nt99340_fmt *fmt;
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
	struct nt99340_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct nt99340_device *dev = fh->dev;

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

	if(f->fmt.pix.pixelformat==V4L2_PIX_FMT_RGB24){
		vidio_set_fmt_ticks=1;
		NT99340_set_resolution(dev,fh->height,fh->width);
	} else if (vidio_set_fmt_ticks==1) {
		NT99340_set_resolution(dev,fh->height,fh->width);
	}

	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return ret;
}
static int vidioc_g_parm(struct file *file, void *priv,
        struct v4l2_streamparm *parms)
{
    struct nt99340_fh *fh = priv;
    struct nt99340_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = nt99340_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct nt99340_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct nt99340_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct nt99340_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct nt99340_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct nt99340_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct nt99340_fh  *fh = priv;
	vdin_parm_t para;
	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para ));
	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate =nt99340_frmintervals_active.denominator;//175;
	para.h_active = nt99340_h_active;
	para.v_active = nt99340_v_active;
	para.hsync_phase = 0;
	para.vsync_phase  = 0;	
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
	para.skip_count =  2;//skip num
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
            vops->start_tvin_service(0,&para);
            fh->stream_on = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct nt99340_fh  *fh = priv;

    int ret = 0 ;
	printk(KERN_INFO " vidioc_streamoff+++ \n ");
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
	struct nt99340_fmt *fmt = NULL;
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
		||(fmt->fourcc == V4L2_PIX_FMT_YVU420)
		||(fmt->fourcc == V4L2_PIX_FMT_YUV420))
	{
		if (fsize->index >= ARRAY_SIZE(nt99340_prev_resolution))
			return -EINVAL;
		frmsize = &nt99340_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(nt99340_pic_resolution))
			return -EINVAL;
		frmsize = &nt99340_pic_resolution[fsize->index];
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
	struct nt99340_fh *fh = priv;
	struct nt99340_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct nt99340_fh *fh = priv;
	struct nt99340_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(nt99340_qctrl); i++)
		if (qc->id && qc->id == nt99340_qctrl[i].id) {
			memcpy(qc, &(nt99340_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct nt99340_fh *fh = priv;
	struct nt99340_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(nt99340_qctrl); i++)
		if (ctrl->id == nt99340_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct nt99340_fh *fh = priv;
	struct nt99340_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(nt99340_qctrl); i++)
		if (ctrl->id == nt99340_qctrl[i].id) {
			if (ctrl->value < nt99340_qctrl[i].minimum ||
			    ctrl->value > nt99340_qctrl[i].maximum ||
			    nt99340_setting(dev,ctrl->id,ctrl->value)<0) {
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
static int nt99340_open(struct file *file)
{
	struct nt99340_device *dev = video_drvdata(file);
	struct nt99340_fh *fh = NULL;
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
	NT99340_init_regs(dev);
	msleep(40);
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

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &nt99340_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct nt99340_buffer), fh, NULL);

	nt99340_start_thread(fh);

	return 0;
}

static ssize_t
nt99340_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct nt99340_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
nt99340_poll(struct file *file, struct poll_table_struct *wait)
{
	struct nt99340_fh        *fh = file->private_data;
	struct nt99340_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int nt99340_close(struct file *file)
{
	struct nt99340_fh         *fh = file->private_data;
	struct nt99340_device *dev       = fh->dev;
	struct nt99340_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);

	nt99340_stop_thread(vidq);
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

	nt99340_h_active=800;
	nt99340_v_active=598;	//598;
	nt99340_qctrl[0].default_value=CAM_WB_AUTO;
	nt99340_qctrl[1].default_value=4;
	nt99340_qctrl[2].default_value=0;
	nt99340_qctrl[3].default_value=CAM_BANDING_50HZ;
	nt99340_qctrl[4].default_value=0;

	nt99340_qctrl[5].default_value=0;
	nt99340_qctrl[7].default_value=100;
        nt99340_frmintervals_active.numerator = 1;
        nt99340_frmintervals_active.denominator = 15;
	power_down_nt99340(dev);
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

static int nt99340_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct nt99340_fh  *fh = file->private_data;
	struct nt99340_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations nt99340_fops = {
	.owner		= THIS_MODULE,
	.open           = nt99340_open,
	.release        = nt99340_close,
	.read           = nt99340_read,
	.poll		= nt99340_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = nt99340_mmap,
};

static const struct v4l2_ioctl_ops nt99340_ioctl_ops = {
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

static struct video_device nt99340_template = {
	.name		= "nt99340_v4l",
	.fops           = &nt99340_fops,
	.ioctl_ops 	= &nt99340_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int nt99340_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_NT99340, 0);
}

static const struct v4l2_subdev_core_ops nt99340_core_ops = {
	.g_chip_ident = nt99340_g_chip_ident,
};

static const struct v4l2_subdev_ops nt99340_ops = {
	.core = &nt99340_core_ops,
};

//****************************
static ssize_t camera_ctrl(struct class *class, 
			struct class_attribute *attr,	const char *buf, size_t count)
{
    unsigned int reg, val, ret;	
	int n=1,i;
	if(buf[0] == 'w'){
		ret = sscanf(buf, "w %x %x", &reg, &val);		
		printk("write camera reg 0x%x value %x\n", reg, val);
		i2c_put_byte(this_client, reg, val);		
	}
	else{
		ret =  sscanf(buf, "r %x %d", &reg,&n);
		printk("read %d camera register from reg: %x \n",n,reg);
		for(i=0;i<n;i++)
		{			
			val = i2c_get_byte(this_client, reg+i);
			printk("reg 0x%x : 0x%x\n", reg+i, val);
		}
	}
	
	if (ret != 1 || ret !=2)
		return -EINVAL;
	
	return count;
	//return 0;
}

static struct class_attribute camera_ctrl_class_attrs[] = {
    __ATTR(reg,  S_IRUGO | S_IWUSR, NULL,    camera_ctrl), 
    __ATTR_NULL
};

static struct class camera_ctrl_class = {
    .name = "camera-nt99340",
    .class_attrs = camera_ctrl_class_attrs,
};
//****************************

static int nt99340_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	struct nt99340_device *t;
	struct v4l2_subdev *sd;
	int ret;
        aml_cam_info_t* plat_dat;
        vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &nt99340_ops);
        plat_dat= (aml_cam_info_t*)client->dev.platform_data;
	/* Now create a video4linux device */
	mutex_init(&t->mutex);
	this_client=client;

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &nt99340_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);
	
	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "nt99340");
	/* Register it */
	if (plat_dat) {
            memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
            if (plat_dat->front_back >=0) video_nr = plat_dat->front_back;
        } else {
            printk("camera nt99340: have no platform data\n");
            kfree(t);
            return -1; 
        }
	err = video_register_device(t->vdev, VFL_TYPE_GRABBER, video_nr);
	if (err < 0) {
		video_device_release(t->vdev);
		kfree(t);
		return err;
	}

	t->cam_info.version = NT99340_DRIVER_VERSION;
	if (aml_cam_info_reg(&t->cam_info) < 0)
		printk("reg caminfo error\n");
	
	ret = class_register(&camera_ctrl_class);
	if(ret){
		printk(" class register camera_ctrl_class fail!\n");
	}
	return 0;
}

static int nt99340_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct nt99340_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id nt99340_id[] = {
	{ "nt99340", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nt99340_id);

static struct i2c_driver nt99340_i2c_driver = {
	.driver = {
		.name = "nt99340",
	},
	.probe = nt99340_probe,
	.remove = nt99340_remove,
	.id_table = nt99340_id,
};

module_i2c_driver(nt99340_i2c_driver);

