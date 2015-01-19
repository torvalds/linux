/*
 *ov2655 - This code emulates a real video device with v4l2 api
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
#include <mach/gpio.h>
#include <linux/amlogic/camera/aml_cam_info.h>
#include <linux/amlogic/vmapi.h>
#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include "common/plat_ctrl.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif

#define OV2655_CAMERA_MODULE_NAME "ov2655"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define OV2655_CAMERA_MAJOR_VERSION 0
#define OV2655_CAMERA_MINOR_VERSION 7
#define OV2655_CAMERA_RELEASE 0
#define OV2655_CAMERA_VERSION \
	KERNEL_VERSION(OV2655_CAMERA_MAJOR_VERSION, OV2655_CAMERA_MINOR_VERSION, OV2655_CAMERA_RELEASE)

#define OV2655_DRIVER_VERSION "OV2655-COMMON-01-140717"

MODULE_DESCRIPTION("ov2655 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static int vidio_set_fmt_ticks=0;

//extern int disable_ov2655;

static int ov2655_h_active=800;
static int ov2655_v_active=598;
static int ov2655_have_opened = 0;
static struct i2c_client *this_client;
static struct v4l2_fract ov2655_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};



static struct vdin_v4l2_ops_s *vops;

/* supported controls */
static struct v4l2_queryctrl ov2655_qctrl[] = {
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
static struct v4l2_frmivalenum ov2655_frmivalenum[]={
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
	 },
};

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct ov2655_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct ov2655_fmt formats[] = {
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

static struct ov2655_fmt *get_format(struct v4l2_format *f)
{
	struct ov2655_fmt *fmt;
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
struct ov2655_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct ov2655_fmt        *fmt;
};

struct ov2655_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(ov2655_devicelist);

struct ov2655_device {
	struct list_head			ov2655_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct ov2655_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t  cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(ov2655_qctrl)];
};

static inline struct ov2655_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov2655_device, sd);
}

struct ov2655_fh {
	struct ov2655_device            *dev;

	/* video capture */
	struct ov2655_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	unsigned int   f_flags;
	int  stream_on;
};

/*static inline struct ov2655_fh *to_fh(struct ov2655_device *dev)
{
	return container_of(dev, struct ov2655_fh, dev);
}*/

static struct v4l2_frmsize_discrete ov2655_prev_resolution[2]=
{
	{320,240},
	{800,600},
};

static struct v4l2_frmsize_discrete ov2655_pic_resolution[2]=
{
	{800,600},
	{1600,1200},
};

/* ------------------------------------------------------------------
	reg spec of OV2655
   ------------------------------------------------------------------*/

struct aml_camera_i2c_fig_s OV2655_script[] = {
   // 800x600 skip YUV

//Soft Reset
{ 0x3012, 0x80 },


//Add some dealy or wait a few miliseconds after register reset

//IO & Clock & Analog Setup
{ 0x308c, 0x80 },
{ 0x308d, 0x0e },
{ 0x360b, 0x00 },
{ 0x30b0, 0xff },
{ 0x30b1, 0xff },
{ 0x30b2, 0x04 },

{ 0x300e, 0x33 },
{ 0x300f, 0xa6 },
{ 0x3010, 0x81 },
{ 0x3082, 0x01 },
{ 0x30f4, 0x01 },
{ 0x3090, 0x43 },
{ 0x3091, 0xc0 },
{ 0x30ac, 0x42 },

{ 0x30d1, 0x08 },
{ 0x30a8, 0x54 },
{ 0x3015, 0x21 },
{ 0x3093, 0x00 },
{ 0x307e, 0xe5 },
{ 0x3079, 0x00 },
{ 0x30aa, 0x52 },
{ 0x3017, 0x40 },
{ 0x30f3, 0x83 },
{ 0x306a, 0x0c },
{ 0x306d, 0x00 },
{ 0x336a, 0x3c },
{ 0x3076, 0x6a },
{ 0x30d9, 0x95 },
{ 0x3016, 0x82 },
{ 0x3601, 0x30 },
{ 0x304e, 0x88 },
{ 0x30f1, 0x82 },
{ 0x306f, 0x14 },
{ 0x302a, 0x02 },
{ 0x302b, 0x8a },   //6a

{ 0x3012, 0x10  },
{ 0x3011, 0x00  },

//AEC/AGC
{ 0x3013, 0xf7 },
{ 0x3018, 0x78 },
{ 0x3019, 0x68 },
{ 0x301a, 0xa4 },
{ 0x301c, 0x13 },
{ 0x301d, 0x17 },
{ 0x3070, 0x5d },
{ 0x3072, 0x4d },

//D5060
{ 0x30af, 0x00 },
{ 0x3048, 0x1f },
{ 0x3049, 0x4e },
{ 0x304a, 0x20 },
{ 0x304f, 0x20 },
{ 0x304b, 0x02 },
{ 0x304c, 0x00 },
{ 0x304d, 0x02 },
{ 0x304f, 0x20 },
{ 0x30a3, 0x10 },
{ 0x3013, 0xf7 },
{ 0x3014, 0x44 },
{ 0x3071, 0x00 },
{ 0x3070, 0x5d },
{ 0x3073, 0x00 },
{ 0x3072, 0x4d },
{ 0x301c, 0x05 },
{ 0x301d, 0x06 },
{ 0x304d, 0x42 },
{ 0x304a, 0x40 },
{ 0x304f, 0x40 },
{ 0x3095, 0x07 },
{ 0x3096, 0x16 },
{ 0x3097, 0x1d },

//Window Setup
{ 0x3020, 0x01 },
{ 0x3021, 0x18 },
{ 0x3022, 0x00 },
{ 0x3023, 0x06 },
{ 0x3024, 0x06 },
{ 0x3025, 0x58 },
{ 0x3026, 0x02 },
{ 0x3027, 0x5e },
{ 0x3088, 0x03 },
{ 0x3089, 0x20 },
{ 0x308a, 0x02 },
{ 0x308b, 0x58 },
{ 0x3316, 0x64 },
{ 0x3317, 0x25 },
{ 0x3318, 0x80 },
{ 0x3319, 0x08 },
{ 0x331a, 0x64 },
{ 0x331b, 0x4b },
{ 0x331c, 0x00 },
{ 0x331d, 0x38 },
{ 0x3100, 0x00 },

//AWB
{ 0x3320, 0xfa },
{ 0x3321, 0x11 },
{ 0x3322, 0x92 },
{ 0x3323, 0x01 },
{ 0x3324, 0x97 },
{ 0x3325, 0x02 },
{ 0x3326, 0xff },
{ 0x3327, 0x10 },
{ 0x3328, 0x10 },
{ 0x3329, 0x1f },
{ 0x332a, 0x58 },
{ 0x332b, 0x50 },
{ 0x332c, 0xbe },
{ 0x332d, 0xce },
{ 0x332e, 0x2e },
{ 0x332f, 0x36 },
{ 0x3330, 0x4d },
{ 0x3331, 0x44 },
{ 0x3332, 0xf0 },
{ 0x3333, 0x0a },
{ 0x3334, 0xf0 },
{ 0x3335, 0xf0 },
{ 0x3336, 0xf0 },
{ 0x3337, 0x40 },
{ 0x3338, 0x40 },
{ 0x3339, 0x40 },
{ 0x333a, 0x00 },
{ 0x333b, 0x00 },

//Color Matrix
{ 0x3380, 0x20 },
{ 0x3381, 0x5b },
{ 0x3382, 0x05 },
{ 0x3383, 0x22 },
{ 0x3384, 0x9d },
{ 0x3385, 0xc0 },
{ 0x3386, 0xb6 },
{ 0x3387, 0xb5 },
{ 0x3388, 0x02 },
{ 0x3389, 0x98 },
{ 0x338a, 0x00 },

//Gamma
/*
{ 0x3340, 0x09 },
{ 0x3341, 0x19 },
{ 0x3342, 0x2f },
{ 0x3343, 0x45 },
{ 0x3344, 0x5a },
{ 0x3345, 0x69 },
{ 0x3346, 0x75 },
{ 0x3347, 0x7e },
{ 0x3348, 0x88 },
{ 0x3349, 0x96 },
{ 0x334a, 0xa3 },
{ 0x334b, 0xaf },
{ 0x334c, 0xc4 },
{ 0x334d, 0xd7 },
{ 0x334e, 0xe8 },
{ 0x334f, 0x20 },
 */
 { 0x3340, 0x06 },
{ 0x3341, 0x14 },
{ 0x3342, 0x2b },
{ 0x3343, 0x42 },
{ 0x3344, 0x55 },
{ 0x3345, 0x65 },
{ 0x3346, 0x70 },
{ 0x3347, 0x7c },
{ 0x3348, 0x86 },
{ 0x3349, 0x96 },
{ 0x334a, 0xa3 },
{ 0x334b, 0xaf },
{ 0x334c, 0xc4 },
{ 0x334d, 0xd7 },
{ 0x334e, 0xe8 },
{ 0x334f, 0x20 },
//Lens correct,ion
{ 0x3350, 0x32 },
{ 0x3351, 0x25 },
{ 0x3352, 0x80 },
{ 0x3353, 0x14 },  //1e
{ 0x3354, 0x00 },
{ 0x3355, 0x84 },
{ 0x3356, 0x32 },
{ 0x3357, 0x25 },
{ 0x3358, 0x80 },
{ 0x3359, 0x12 },  //1b
{ 0x335a, 0x00 },
{ 0x335b, 0x84 },
{ 0x335c, 0x32 },
{ 0x335d, 0x25 },
{ 0x335e, 0x80 },
{ 0x335f, 0x12 },  //1b
{ 0x3360, 0x00 },
{ 0x3361, 0x84 },
{ 0x3363, 0x70 },
{ 0x3364, 0x7f },
{ 0x3365, 0x00 },
{ 0x3366, 0x00 },

//UVadjust
{ 0x3301, 0xff },
{ 0x338B, 0x1b },
{ 0x338c, 0x1f },
{ 0x338d, 0x40 },

//Sharpness/De-noise
{ 0x3370, 0xd0 },
{ 0x3371, 0x00 },
{ 0x3372, 0x00 },
{ 0x3373, 0x80 },
{ 0x3374, 0x10 },
{ 0x3375, 0x10 },
{ 0x3376, 0x05 },
{ 0x3377, 0x00 },
{ 0x3378, 0x04 },
{ 0x3379, 0x80 },

//BLC
{ 0x3069, 0x86 },
{ 0x307c, 0x13 },
{ 0x3087, 0x02 },

   #if 1
//black sun
//Avdd 2.55~3.0V
{ 0x3090, 0x0b },
{ 0x30aa, 0x52 },
{ 0x30a3, 0x80 },
{ 0x30a1, 0x41 },
#else
//Avdd 2.45~2.7V
 { 0x3090, 0x03},
 { 0x30aa, 0x22},
 { 0x30a3, 0x80},
 { 0x30a1, 0x41},
#endif

//Other functions
{ 0x3300, 0xfc },
{ 0x3302, 0x11 },
{ 0x3400, 0x01 },
{ 0x3600, 0x83 },

{ 0x3606, 0x20 },
{ 0x3601, 0x30 },
{ 0x300e, 0x33 },
{ 0x30f3, 0x83 },
{ 0x304e, 0x88 },
{ 0x363b, 0x01 },
{ 0x363c, 0xf2 },

{ 0x3086, 0x0f },
{ 0x3086, 0x00 },
	//{ 0x3308, 0x01 },

{ 0x3018,0x64},
{ 0x3019,0x54},
{ 0x301a,0x93},

//2650 series lens correction

{ 0x3350,0x31},
{ 0x3351,0x25},
{ 0x3352,0x4c},
{ 0x3353,0x18},
{ 0x3354,0x00},
{ 0x3355,0x84},
{ 0x3356,0x32},
{ 0x3357,0x25},
{ 0x3358,0x88},
{ 0x3359,0x18},
{ 0x335a,0x00},
{ 0x335b,0x84},
{ 0x335c,0x31},
{ 0x335d,0x25},
{ 0x335e,0x40},
{ 0x335f,0x16},
{ 0x3360,0x00},
{ 0x3361,0x84},
{ 0x3300,0xfc},

{ 0x3300,0xfc},

		 //gamma
/*
 {0x334f,0x1d},
 {0x3340,0x06},
 {0x3341,0x0d},
 {0x3342,0x1f},
 {0x3343,0x35},
 {0x3344,0x48},
 {0x3345,0x56},
 {0x3346,0x63},
 {0x3347,0x6f},
 {0x3348,0x7a},
 {0x3349,0x8d},
 {0x334a,0x9e},
 {0x334b,0xae},
 {0x334c,0xc9},
 {0x334d,0xdc},
 {0x334e,0xea},
*/
 {0x334f,0x20},
 {0x3340,0x08},
 {0x3341,0x16},
 {0x3342,0x2f},
 {0x3343,0x45},
 {0x3344,0x55},
 {0x3345,0x65},
 {0x3346,0x70},
 {0x3347,0x7c},
 {0x3348,0x86},
 {0x3349,0x96},
 {0x334a,0xa3},
 {0x334b,0xaf},
 {0x334c,0xc4},
 {0x334d,0xd7},
 {0x334e,0xe8},

 //awb
{ 0x3320,0xfa},
{ 0x3321,0x11},
{ 0x3322,0x92},
{ 0x3323,0x01},
{ 0x3324,0x97},
{ 0x3325,0x02},
{ 0x3326,0xff},
{ 0x3327,0x10},
{ 0x3328,0x15},
{ 0x3329,0x1f},
{ 0x332a,0x61},//5e
{ 0x332b,0x50},
{ 0x332c,0xe9},
{ 0x332d,0xce},
{ 0x332e,0x2e},
{ 0x332f,0x34},
{ 0x3330,0x53}, //4f
{ 0x3331,0x45},
{ 0x3332,0xf0},
{ 0x3333,0x0a},
{ 0x3334,0xf0},
{ 0x3335,0xf0},
{ 0x3336,0xf0},
{ 0x3337,0x40},
{ 0x3338,0x40},
{ 0x3339,0x40},
{ 0x333a,0x00},
{ 0x333b,0x00},

         //cmx
{ 0x3380,0x28},
{ 0x3381,0x48},
{ 0x3382,0x10},
{ 0x3383,0x34},
{ 0x3384,0x9c},
{ 0x3385,0xd0},
{ 0x3386,0xad},
{ 0x3387,0xa4},
{ 0x3388,0x09},
{ 0x3389,0x98},
{ 0x338a,0x01},


         //cip
{ 0x3306,0x00},
{ 0x3370,0xff},
{ 0x3373,0x80},
{ 0x3374,0x10},
{ 0x3375,0x10},
{ 0x3376,0x06},
{ 0x3377,0x00},
{ 0x3378,0x04},
{ 0x3379,0x80},

{0x3391, 0x06},
{0x3390, 0x41},
{0x339a, 0x10},
{0xffff, 0xff},

};
int m_iCombo_NightMode = 0; // night mode switch from UI
int XVCLK = 2200; // real clock/10000
int preview_sysclk, preview_HTS;

int OV2655_get_shutter(struct ov2655_device *dev)
{
	// read shutter, in number of line period
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int shutter, extra_line;

	shutter = i2c_get_byte(client, 0x03002);
	shutter = (shutter<<8) + i2c_get_byte(client, 0x3003);
	extra_line = i2c_get_byte(client, 0x0302d);
	extra_line = (extra_line<<8) + i2c_get_byte(client, 0x302e);

	return shutter + extra_line;
}
int OV2655_set_shutter(struct ov2655_device *dev,int shutter)
{
	// write shutter, in number of line period
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp;

	shutter = shutter & 0xffff;
	temp = shutter & 0xff;
	i2c_put_byte(client, 0x3003, temp);
	temp = shutter >> 8;
	i2c_put_byte(client, 0x3002, temp);

	return 0;
}
int OV2655_get_gain16(struct ov2655_device *dev)
{
	// read gain, 16 = 1x
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int gain16, temp;

	temp = i2c_get_byte(client, 0x3000);
	gain16 = ((temp>>4) + 1) * (16 + (temp & 0x0f));

	return gain16;
}
int OV2655_set_gain16(struct ov2655_device *dev,int gain16)
{
	// write gain, 16 = 1x
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int reg;

	gain16 = gain16 & 0x1ff; // max gain is 32x
	reg = 0;
	if (gain16 > 32) {
		gain16 = gain16 /2;
		reg = 0x10;
		}
	if (gain16 > 32) {
		gain16 = gain16 /2;
		reg = reg | 0x20;
		}
	if (gain16 > 32) {
		gain16 = gain16 /2;
		reg = reg | 0x40;
		}
	if (gain16 > 32) {
		gain16 = gain16 /2;
		reg = reg | 0x80;
		}
	reg = reg | (gain16 -16);
	i2c_put_byte(client, 0x3000, reg);

	return 0;
}
int OV2655_get_sysclk(struct ov2655_device *dev)
{
	// calculate sysclk
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp1, temp2;
	int Indiv2x, Bit8Div, FreqDiv2x, PllDiv, SensorDiv, ScaleDiv, DvpDiv, ClkDiv, VCO, sysclk;
	int Indiv2x_map[] = {2, 3, 4, 6, 4, 6, 8, 12};
	int Bit8Div_map[] = {1, 1, 4, 5};
	int FreqDiv2x_map[] = {2, 3, 4, 6};
	int DvpDiv_map[] = {1, 2, 8, 16};

	temp1 = i2c_get_byte(client, 0x300e);
	// bit[5:0] PllDiv
	PllDiv = 64 - (temp1 & 0x3f);
	temp1 = i2c_get_byte(client, 0x300f);
	// bit[2:0] Indiv
	temp2 = temp1 & 0x07;
	Indiv2x = Indiv2x_map[temp2];
	// bit[5:4] Bit8Div
	temp2 = (temp1 >> 4) & 0x03;
	Bit8Div = Bit8Div_map[temp2];
	// bit[7:6] FreqDiv
	temp2 = temp1 >> 6;
	FreqDiv2x = FreqDiv2x_map[temp2];
	temp1 = i2c_get_byte(client, 0x3010);
	//bit[3:0] ScaleDiv
	temp2 = temp1 & 0x0f;
	if(temp2==0) {
		ScaleDiv = 1;
		}
	else {
		ScaleDiv = temp2 * 2;
		}

	// bit[4] SensorDiv
	if(temp1 & 0x10) {
		SensorDiv = 2;
		}
	else {
		SensorDiv = 1;
		}
	// bit[5] LaneDiv
	// bit[7:6] DvpDiv
	temp2 = temp1 >> 6;
	DvpDiv = DvpDiv_map[temp2];
	temp1 = i2c_get_byte(client, 0x3011);
	// bit[5:0] ClkDiv
	temp2 = temp1 & 0x3f;
	ClkDiv = temp2 + 1;
	VCO = XVCLK * Bit8Div * FreqDiv2x * PllDiv / Indiv2x ;
	sysclk = VCO / Bit8Div / SensorDiv / ClkDiv / 4;

	return sysclk;
}
int OV2655_get_HTS(struct ov2655_device *dev)
{
	// read HTS from register settings
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int HTS, extra_HTS;

	HTS = i2c_get_byte(client, 0x3028);
	HTS = (HTS<<8) + i2c_get_byte(client, 0x3029);
	extra_HTS = i2c_get_byte(client, 0x302c);

	return HTS + extra_HTS;
}
int OV2655_get_VTS(struct ov2655_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int VTS, extra_VTS;

	VTS = i2c_get_byte(client, 0x302a);
	VTS = (VTS<<8) + i2c_get_byte(client, 0x302b);
	extra_VTS = i2c_get_byte(client, 0x302d);
	extra_VTS = (extra_VTS<<8) + i2c_get_byte(client, 0x302e);

	return VTS + extra_VTS;
}
int OV2655_set_VTS(struct ov2655_device *dev,int VTS)
{
	// write VTS to registers
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp;

	temp = VTS & 0xff;
	i2c_put_byte(client, 0x302b, temp);
	temp = VTS>>8;
	i2c_put_byte(client, 0x302a, temp);
	return 0;
}
int OV2655_get_binning(struct ov2655_device *dev)
{
	// write VTS to registers
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp, binning;

	temp = i2c_get_byte(client, 0x300b);
	if(temp==0x52) {
	// OV2650
	binning = 2;
	}

	else {
	// OV2655
	binning = 1;
	}
	return binning;
}
int OV2655_get_light_frequency(struct ov2655_device *dev)
{
	// get banding filter value
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp, light_frequency;

	temp = i2c_get_byte(client, 0x3014);
	if (temp & 0x40) {
	// auto
	temp = i2c_get_byte(client, 0x508e);
	if (temp & 0x01) {
	light_frequency = 50;
	}
	else {
	light_frequency = 60;
	}
	}
	else {
	// manual
	if (temp & 0x80) {
	// 50Hz
	light_frequency = 50;
	}
	else {
	// 60Hz
	light_frequency = 60;
	}
	}
	return light_frequency;
}
void OV2655_set_bandingfilter(struct ov2655_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int preview_VTS;
	int band_step60, max_band60, band_step50, max_band50;
	int temp;

	// read preview PCLK
	preview_sysclk = OV2655_get_sysclk(dev);
	// read preview HTS
	preview_HTS = OV2655_get_HTS(dev);
	// read preview VTS
	preview_VTS = OV2655_get_VTS(dev);
	// calculate banding filter
	// 60Hz
	band_step60 = preview_sysclk * 100/preview_HTS * 100/120;
	i2c_put_byte(client, 0x3073, (band_step60 >> 8));
	i2c_put_byte(client, 0x3072, (band_step60 & 0xff));
	max_band60 = (int)((preview_VTS-4)/band_step60);
	i2c_put_byte(client, 0x301d, max_band60-1);

	// 50Hz
	temp = i2c_get_byte(client, 0x3014);
	temp = temp | 0x80; // banding 50, bit[7] = 1
	i2c_put_byte(client, 0x3014, temp);

	band_step50 = preview_sysclk * 100/preview_HTS;
	i2c_put_byte(client, 0x3071, (band_step50 >> 8));
	i2c_put_byte(client, 0x3070, (band_step50 & 0xff));
	max_band50 = (int)((preview_VTS-4)/band_step50);
	i2c_put_byte(client, 0x301c, max_band50-1);
}
void ChangeNightMode(struct ov2655_device *dev,int NightMode)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp;

	switch (NightMode)
	{
	case 0://Off
	temp = i2c_get_byte(client, 0x3014);
	temp = temp & 0xf7; // night mode off, bit[3] = 0  | 0x08; //
	i2c_put_byte(client, 0x3014, temp);
	i2c_put_byte(client, 0x3015, 0x01);
	// clear dummy lines
	i2c_put_byte(client, 0x302d, 0);
	i2c_put_byte(client, 0x302e, 0);
	break;
	case 1:// On
	temp = i2c_get_byte(client, 0x3014);
	temp = temp | 0x08; // night mode on, bit[3] = 1
	i2c_put_byte(client, 0x3014, temp);
	i2c_put_byte(client, 0x3015, 0x32);
	i2c_put_byte(client, 0x302d, 0);
	i2c_put_byte(client, 0x302e, 0);
	break;
	default:
	break;
	}
}
int OV2655_preview(struct ov2655_device *dev)
{
	// set OV2655 to preview mode
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp;
	int i=0;
	int regPreview[] =
	{
	//2655 Rev1A reference setting 06232008
	//24MHz //15FPS
	//800x600 skip //YUV output
	 0x306f, 0x14, // BLC target
	 0x3012, 0x10, // SVGA skip
	 0x3011, 0x00, // clock divider
	 0x300e, 0x38, // clock divider
	 0x3070, 0x58, // B50
	 0x3071, 0x00, // B50 High
	 0x3072, 0x49, // B60
	 0x3073, 0x00, // B60 igh
	 0x301c, 0x06, // max 50
	 0x301d, 0x07, // max 60
	 0x302d, 0x00, // B60 igh
	 0x302e, 0x00, // max 50
	 0x302c, 0x56,
	//Window Setup
	 0x3020, 0x01, // HS
	 0x3021, 0x1a, // HS
	 0x3022, 0x00, // VS
	 0x3023, 0x06, // VS
	 0x3024, 0x06, // HW
	 0x3025, 0x58, // HW
	 0x3026, 0x02, // VH
	 0x3027, 0x5e, // VH
	 0x302a, 0x02, // VTS
	 0x302b, 0x6a, // VTS    //6a
	 0x3088, 0x03, // ISP X out
	 0x3089, 0x20, // ISP X out
	 0x308a, 0x02, // ISP Y out
	 0x308b, 0x58, // ISP Y out
	 0x3316, 0x64, // scale H input Size
	 0x3317, 0x25, // scale V input Size
	 0x3318, 0x80, // scale V/H input Size
	 0x3319, 0x08, // offset scale
	 0x331a, 0x64, // scale H output Size
	 0x331b, 0x4b, // scale V output Size
	 0x331c, 0x00, // scale V/H output Size
	 0x331d, 0x38, // offset scale
	 0x3100, 0x00, // Output YUV
	 0x338d, 0x40, // UV adjust th2
	 0x30a1, 0x41, // black sun
	 0x3302, 0x11, // scale on, UV average on
	 0x338B, 0x1b, // UV adjust offset
	};
	// Write preview table
	for (i=0; i<sizeof(regPreview)/sizeof(int); i+=2)
	{
	i2c_put_byte(client,regPreview[i], regPreview[i+1]);
	}
	// set banding filter
	OV2655_set_bandingfilter(dev);
	// turn on AEC, AGC
	temp = i2c_get_byte(client, 0x3013);
	temp = temp | 0x05;
	i2c_put_byte(client, 0x3013, temp);
	// update night mode setting
	ChangeNightMode(dev,m_iCombo_NightMode);

	return 0;
}
int OV2655_capture(struct ov2655_device *dev)
{
	// set OV2655 to preview mode
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int i=0;

	int regCapture[] =
	{
	//2655 Rev1A reference setting 06232008
	//24MHz //5FPS
	 0x306f, 0x54, // BLC target
	 0x3012, 0x00, // UXGA
	 0x3011, 0x01, // clock divider
	//Window Setup
	 0x3020, 0x01, // HS
	 0x3021, 0x1a, // HS
	 0x3022, 0x00, // VS
	 0x3023, 0x0c, // VS
	 0x3024, 0x06, // HW
	 0x3025, 0x58, // HW
	 0x3026, 0x04, // VH
	 0x3027, 0xbc, // VH
	 0x302a, 0x04, // VTS
	 0x302b, 0xd4, // VTS
	 0x3088, 0x06, // ISP X out
	 0x3089, 0x40, // ISP X out
	 0x308a, 0x04, // ISP Y out
	 0x308b, 0xb2, // ISP Y out
	 0x3316, 0x64, // scale H input Size
	 0x3317, 0x4b, // scale V input Size
	 0x3318, 0x00, // scale V/H input Size
	 0x3319, 0x4c, // offset scale
	 0x331a, 0x64, // scale H output Size
	 0x331b, 0x4b, // scale V output Size
	 0x331c, 0x00, // scale V/H output Size
	 0x331d, 0x6c, // offset scale
	 0x3100, 0x00, // Output YUV
	 0x338d, 0x40, // UV adjsut TH2
	 0x30a1, 0x41, // black sun
	 0x3302, 0x01, // scale off, UV average on
	 0x338B, 0x1b, // UV adjust offset
	 0x3013, 0xf2, // AEC fast, big step, banding on, < band on
	// extreme exposure off, AGC off, AEC off
	};
	int preview_shutter, preview_gain16, preview_binning;
	int capture_shutter, capture_gain16, capture_sysclk, capture_HTS, capture_VTS;
	int light_frequency, capture_bandingfilter, capture_max_band;
	long capture_gain16_shutter;
	int temp;
	// read preview shutter
	preview_shutter = OV2655_get_shutter(dev);
	// read preview gain
	preview_gain16 = OV2655_get_gain16(dev);
	preview_binning = OV2655_get_binning(dev);
	// turn off night mode for capture
	ChangeNightMode(dev,0);
	// Write preview table
	for (i=0; i<sizeof(regCapture)/sizeof(int); i+=2)
	{
	i2c_put_byte(client, regCapture[i], regCapture[i+1]);
	}
	// turn off AEC, AGC
	temp = i2c_get_byte(client, 0x3013);
	temp = temp & 0xfa;
	i2c_put_byte(client, 0x3013, temp);
	// read capture sysclk
	capture_sysclk = OV2655_get_sysclk(dev);
	// read capture HTS
	capture_HTS = OV2655_get_HTS(dev);
	// read capture VTS
	capture_VTS = OV2655_get_VTS(dev);
	// calculate capture banding filter
	light_frequency = OV2655_get_light_frequency(dev);
	if (light_frequency == 60) {
	// 60Hz
	capture_bandingfilter = capture_sysclk * 100 / capture_HTS * 100 / 120;
	}
	else {
	// 50Hz
	capture_bandingfilter = capture_sysclk * 100 / capture_HTS;
	}
	capture_max_band = (int)((capture_VTS-4)/capture_bandingfilter);
	// calculate capture shutter
	capture_shutter = preview_shutter;
	// gain to shutter
	capture_gain16 = preview_gain16 * capture_sysclk/preview_sysclk * preview_HTS/capture_HTS * preview_binning;
	if (capture_gain16 < 16) {
	capture_gain16 = 16;
	}
	capture_gain16_shutter = capture_gain16 * capture_shutter;
	if(capture_gain16_shutter < (capture_bandingfilter * 16)) {
	// shutter < 1/100
	capture_shutter = capture_gain16_shutter/16;
	capture_gain16 = capture_gain16_shutter/capture_shutter;
	}
	else {
	if(capture_gain16_shutter > (capture_bandingfilter*capture_max_band*16)) {
	// exposure reach max
	capture_shutter = capture_bandingfilter*capture_max_band;
	capture_gain16 = capture_gain16_shutter / capture_shutter;
	}
	else {
	// 1/100 < capture_shutter < max, capture_shutter = n/100
	capture_shutter = ((int) (capture_gain16_shutter/16/capture_bandingfilter)) * capture_bandingfilter;
	capture_gain16 = capture_gain16_shutter / capture_shutter;
	}
	}
	// write capture gain
	OV2655_set_gain16(dev,capture_gain16);
	// write capture shutter
	if (capture_shutter > (capture_VTS - 4)) {
	capture_VTS = capture_shutter + 4;
	OV2655_set_VTS(dev,capture_VTS);
	}
	OV2655_set_shutter(dev,capture_shutter);
	// skip 2 vysnc
	// start capture at 3rd vsync
	return 0;
}

//load OV2655 parameters
void OV2655_init_regs(struct ov2655_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    int i=0;

    while(1)
    {
        if (OV2655_script[i].val==0xff&&OV2655_script[i].addr==0xffff)
        {
        	printk("OV2655_write_regs success in initial OV2655.\n");
        	break;
        }
        if((i2c_put_byte(client,OV2655_script[i].addr, OV2655_script[i].val)) < 0)
        {
        	printk("fail in initial OV2655. i=%d\n",i);
		return;
		}
		i++;
    }
	//OV2655_preview(dev);
    return;
}


/*************************************************************************
* FUNCTION
*    OV2655_set_param_wb
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
void OV2655_set_param_wb(struct ov2655_device *dev,enum  camera_wb_flip_e para)//white balance
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp_reg=i2c_get_byte(client,0x3306);

	switch (para) {
	case CAM_WB_AUTO://auto
		i2c_put_byte(client,0x3306, temp_reg&~0x2);   // select Auto WB
		break;

	case CAM_WB_CLOUD: //cloud

		i2c_put_byte(client,0x3306, temp_reg|0x2);  // select manual WB
		i2c_put_byte(client,0x3337, 0x68); //manual R G B
		i2c_put_byte(client,0x3338, 0x40);
		i2c_put_byte(client,0x3339, 0x4e);
		break;

	case CAM_WB_DAYLIGHT: //

		i2c_put_byte(client,0x3306, temp_reg|0x2);  // Disable AWB
		i2c_put_byte(client,0x3337, 0x5e);
		i2c_put_byte(client,0x3338, 0x40);
		i2c_put_byte(client,0x3339, 0x46);
		break;

	case CAM_WB_INCANDESCENCE:

		i2c_put_byte(client,0x3306, temp_reg|0x2);  // Disable AWB
		i2c_put_byte(client,0x3337, 0x5e);
		i2c_put_byte(client,0x3338, 0x40);
		i2c_put_byte(client,0x3339, 0x58);
		break;

	case CAM_WB_TUNGSTEN:

		i2c_put_byte(client,0x13, temp_reg|0x2);	// Disable AWB
		i2c_put_byte(client,0x3337, 0x54);
		i2c_put_byte(client,0x3338, 0x40);
		i2c_put_byte(client,0x3339, 0x70);
		break;

	case CAM_WB_FLUORESCENT:

		i2c_put_byte(client,0x3306, temp_reg|0x2); // Disable AWB
		i2c_put_byte(client,0x3337, 0x65);
		i2c_put_byte(client,0x3338, 0x40);
		i2c_put_byte(client,0x3339, 0x41);

		break;

	case CAM_WB_MANUAL:
	    	// TODO
		break;
	default:
		break;
	}


} /* OV2655_set_param_wb */
/*************************************************************************
* FUNCTION
*    OV2655_set_param_exposure
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
void OV2655_set_param_exposure(struct ov2655_device *dev,enum camera_exposure_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	switch (para)
		{
		/*case EXPOSURE_N4_STEP:
			i2c_put_byte(client,0x3376, 0x02);
			break;
		case EXPOSURE_N3_STEP:
			i2c_put_byte(client,0x3376, 0x03);

			break;
		case EXPOSURE_N2_STEP:
			i2c_put_byte(client,0x3376, 0x04);

			break;
		case EXPOSURE_N1_STEP:
			i2c_put_byte(client,0x3376, 0x05);

			break;
		case EXPOSURE_0_STEP:
			i2c_put_byte(client,0x3376, 0x06);

			break;
		case EXPOSURE_P1_STEP:
			i2c_put_byte(client,0x3376, 0x07);

			break;
		case EXPOSURE_P2_STEP:
			i2c_put_byte(client,0x3376, 0x08);

			break;
		case EXPOSURE_P3_STEP:
			i2c_put_byte(client,0x3376, 0x09);

			break;
		case EXPOSURE_P4_STEP:
			i2c_put_byte(client,0x3376, 0x0a);

			break;
		default:
			i2c_put_byte(client,0x3376, 0x06);

			break;
		*/
		#if 1
		case EXPOSURE_N4_STEP:
			i2c_put_byte(client,0x3391, 0x06);
			i2c_put_byte(client,0x3390, 0x49);
			i2c_put_byte(client,0x339a, 0x30);
			break;
		case EXPOSURE_N3_STEP:
			i2c_put_byte(client,0x3391, 0x06);
			i2c_put_byte(client,0x3390, 0x49);
			i2c_put_byte(client,0x339a, 0x20);
			break;
		case EXPOSURE_N2_STEP:
			i2c_put_byte(client,0x3391, 0x06);
			i2c_put_byte(client,0x3390, 0x49);
			i2c_put_byte(client,0x339a, 0x10);
			break;
		case EXPOSURE_N1_STEP:
			i2c_put_byte(client,0x3391, 0x06);
			i2c_put_byte(client,0x3390, 0x49);
			i2c_put_byte(client,0x339a, 0x00);
			break;
		case EXPOSURE_0_STEP:
			i2c_put_byte(client,0x3391, 0x06);
			i2c_put_byte(client,0x3390, 0x41);
			i2c_put_byte(client,0x339a, 0x10);
			break;
		case EXPOSURE_P1_STEP:
			i2c_put_byte(client,0x3391, 0x06);
			i2c_put_byte(client,0x3390, 0x41);
			i2c_put_byte(client,0x339a, 0x20);
			break;
		case EXPOSURE_P2_STEP:
			i2c_put_byte(client,0x3391, 0x06);
			i2c_put_byte(client,0x3390, 0x41);
			i2c_put_byte(client,0x339a, 0x30);
			break;
		case EXPOSURE_P3_STEP:
			i2c_put_byte(client,0x3391, 0x06);
			i2c_put_byte(client,0x3390, 0x41);
			i2c_put_byte(client,0x339a, 0x40);
			break;
		case EXPOSURE_P4_STEP:
			i2c_put_byte(client,0x3391, 0x06);
			i2c_put_byte(client,0x3390, 0x41);
			i2c_put_byte(client,0x339a, 0x50);
			break;
		default:
			i2c_put_byte(client,0x3391, 0x06);
			i2c_put_byte(client,0x3390, 0x41);
			i2c_put_byte(client,0x339a, 0x14);
			break;
		#else
		case EXPOSURE_N4_STEP:
			i2c_put_byte(client,0x3018, 0x40);
			i2c_put_byte(client,0x3019, 0x30);
			i2c_put_byte(client,0x301a, 0x71);
			break;
		case EXPOSURE_N3_STEP:
			i2c_put_byte(client,0x3018, 0x40);
			i2c_put_byte(client,0x3019, 0x30);
			i2c_put_byte(client,0x301a, 0x71);
			break;
		case EXPOSURE_N2_STEP:
			i2c_put_byte(client,0x3018, 0x5a);
			i2c_put_byte(client,0x3019, 0x4a);
			i2c_put_byte(client,0x301a, 0xc2);
			break;
		case EXPOSURE_N1_STEP:
			i2c_put_byte(client,0x3018, 0x6a);
			i2c_put_byte(client,0x3019, 0x5a);
			i2c_put_byte(client,0x301a, 0xd4);
			break;
		case EXPOSURE_0_STEP:
			i2c_put_byte(client,0x3018, 0x78);
			i2c_put_byte(client,0x3019, 0x68);
			i2c_put_byte(client,0x301a, 0xd4);
			break;
		case EXPOSURE_P1_STEP:
			i2c_put_byte(client,0x3018, 0x88);
			i2c_put_byte(client,0x3019, 0x78);
			i2c_put_byte(client,0x301a, 0xd5);
			break;
		case EXPOSURE_P2_STEP:
			i2c_put_byte(client,0x3018, 0xa8);
			i2c_put_byte(client,0x3019, 0x98);
			i2c_put_byte(client,0x301a, 0xe6);
			break;
		case EXPOSURE_P3_STEP:
			i2c_put_byte(client,0x3018, 0xc8);
			i2c_put_byte(client,0x3019, 0xb8);
			i2c_put_byte(client,0x301a, 0xf7);

			break;
		case EXPOSURE_P4_STEP:
			i2c_put_byte(client,0x3018, 0xc8);
			i2c_put_byte(client,0x3019, 0xb8);
			i2c_put_byte(client,0x301a, 0xf7);
			break;
		default:
			i2c_put_byte(client,0x3018, 0x78);
			i2c_put_byte(client,0x3019, 0x68);
			i2c_put_byte(client,0x301a, 0xd4);
			break;
		#endif

	}


} /* OV2655_set_param_exposure */
/*************************************************************************
* FUNCTION
*    OV2655_set_param_effect
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
void OV2655_set_param_effect(struct ov2655_device *dev,enum camera_effect_flip_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);


    switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL:
			i2c_put_byte(client,0x3391, 0x06);
			i2c_put_byte(client,0x3396, 0x80);
			i2c_put_byte(client,0x3397, 0x80);
			break;

		case CAM_EFFECT_ENC_GRAYSCALE:
			//i2c_put_byte(client,0x0115,0x06);
			break;

		case CAM_EFFECT_ENC_SEPIA:
			i2c_put_byte(client,0x3391, 0x1e);
			i2c_put_byte(client,0x3396, 0x40);
			i2c_put_byte(client,0x3397, 0xa6);
			break;

		case CAM_EFFECT_ENC_SEPIAGREEN:
			i2c_put_byte(client,0x3391, 0x1e);
			i2c_put_byte(client,0x3396, 0x60);
			i2c_put_byte(client,0x3397, 0x60);
			break;

		case CAM_EFFECT_ENC_SEPIABLUE:
			i2c_put_byte(client,0x3391, 0x1e);
			i2c_put_byte(client,0x3396, 0xa0);
			i2c_put_byte(client,0x3397, 0x40);
			break;

		case CAM_EFFECT_ENC_COLORINV:
			i2c_put_byte(client,0x3391, 0x46); //bit[6] negative
			i2c_put_byte(client,0x3396, 0x80);
			i2c_put_byte(client,0x3397, 0x80);
			break;

		default:
			break;
	}
	if(para!=CAM_EFFECT_ENC_COLORINV){
		OV2655_set_param_exposure(dev,ov2655_qctrl[1].default_value);

	}


} /* OV2655_set_param_effect */

/*************************************************************************
* FUNCTION
*    OV2655_NightMode
*
* DESCRIPTION
*    This function night mode of OV2655.
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
void OV2655_set_night_mode(struct ov2655_device *dev,enum  camera_night_mode_flip_e enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp;
	if (enable)
	{
		temp = i2c_get_byte(client, 0x3014);
		temp = temp | 0x08; // night mode on, bit[3] = 1
		i2c_put_byte(client, 0x3014, temp);

	}
	else
	{
		temp = i2c_get_byte(client, 0x3014);
		temp = temp | 0x08; // night mode on, bit[3] = 1
		//temp = temp & 0xf7; // night mode on, bit[3] = 1
		i2c_put_byte(client, 0x3014, temp);

	}

}    /* OV2655_NightMode */
void OV2655_set_param_banding(struct ov2655_device *dev,enum  camera_banding_flip_e banding)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp;
	switch(banding) {
	case CAM_BANDING_50HZ:
		temp = i2c_get_byte(client, 0x3014);
		temp = temp | 0x80; // banding 50, bit[3] = 1
		i2c_put_byte(client, 0x3014, temp);

		i2c_put_byte(client, 0x3070, 0x58);
		i2c_put_byte(client, 0x3071, 0x00);
		i2c_put_byte(client, 0x301c, 0x06);

		printk(KERN_INFO "banding 50 in\n");

		break;
	case CAM_BANDING_60HZ:
		temp = i2c_get_byte(client, 0x3014);
		temp = temp & 0x7f; // night mode on, bit[3] = 1
		i2c_put_byte(client, 0x3014, temp);
		//i2c_put_byte(client,0x0315,0x56);

		i2c_put_byte(client, 0x3072, 0x49);
		i2c_put_byte(client, 0x3073, 0x00);
		i2c_put_byte(client, 0x301d, 0x07);

		printk(KERN_INFO " banding 60 in\n ");
		break;
	default:
		break;

	}

}
static int set_flip(struct ov2655_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp = 0;
	temp = i2c_get_byte(client, 0x307c);
	temp &=0xfc;
	temp |= dev->cam_info.m_flip << 0;
	temp |= dev->cam_info.v_flip << 1;
	if((i2c_put_byte(client, 0x307c, temp)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
	}
	return 0;
}

void OV2655_set_resolution(struct ov2655_device *dev,int height,int width)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    #if 1
	if(height&&width&&(height<=1200)&&(width<=1600))
	{
	    if((height<=600)&&(width<=800))
	    {
	    #if 1
		OV2655_preview(dev);
		ov2655_frmintervals_active.denominator 	= 15;
		ov2655_frmintervals_active.numerator	= 1;
		ov2655_h_active=800;
		ov2655_v_active=598;
		#endif

		}
		else
		{
		#if 1
		OV2655_capture(dev);
		#endif
		msleep(10);// 10
		ov2655_frmintervals_active.denominator 	= 5;
		ov2655_frmintervals_active.numerator	= 1;
		ov2655_h_active=1600;
		ov2655_v_active=1200;
		}
	}
    #else
		i2c_put_byte(client,0x0110, 0x03);
		i2c_put_byte(client,0x0111, 0x20);
			i2c_put_byte(client,0x0112 , 0x02);
		i2c_put_byte(client,0x0113, 0x5a);// 5a
    #endif
       set_flip(dev);
}    /* OV2655_set_resolution */

unsigned char v4l_2_ov2655(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int ov2655_setting(struct ov2655_device *dev,int PROP_ID,int value )
{
	int ret=0;
	//unsigned char cur_val;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_ov2655(value));
		ret=i2c_put_byte(client,0x0201,v4l_2_ov2655(value));
		break;
	case V4L2_CID_CONTRAST:
		ret=i2c_put_byte(client,0x0200, value);
		break;
	case V4L2_CID_SATURATION:
		ret=i2c_put_byte(client,0x0202, value);
		break;
#if 0
	case V4L2_CID_EXPOSURE:
		ret=i2c_put_byte(client,0x0201, value);
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
				cur_val=cur_val|0x02;
			else
				cur_val=cur_val&0xFD;
			ret=i2c_put_byte(client,0x0101,cur_val);
		} else {
			dprintk(dev, 1, "vertical read error\n");
		}
		break;
#endif
	case V4L2_CID_DO_WHITE_BALANCE:
        if(ov2655_qctrl[0].default_value!=value){
			ov2655_qctrl[0].default_value=value;
			OV2655_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
        	}
		break;
	case V4L2_CID_EXPOSURE:
        if(ov2655_qctrl[1].default_value!=value){
			ov2655_qctrl[1].default_value=value;
			OV2655_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
        	}
		break;
	case V4L2_CID_COLORFX:
        if(ov2655_qctrl[2].default_value!=value){
			ov2655_qctrl[2].default_value=value;
			OV2655_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
        	}
		break;
	case V4L2_CID_WHITENESS:
		 if(ov2655_qctrl[3].default_value!=value){
			ov2655_qctrl[3].default_value=value;
			OV2655_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
        	}
		break;
	case V4L2_CID_HFLIP:
		value = value & 0x3;
		if(ov2655_qctrl[4].default_value!=value){
			ov2655_qctrl[4].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
        	}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */         
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(ov2655_qctrl[6].default_value!=value){
			ov2655_qctrl[6].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
        	}
		break;
	case V4L2_CID_ROTATE:
		 if(ov2655_qctrl[7].default_value!=value){
			ov2655_qctrl[7].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
        	}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;

}

static void power_down_ov2655(struct ov2655_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	i2c_put_byte(client,0x3012, 0x80);
	msleep(5);
	i2c_put_byte(client,0x30ab, 0x00);
	i2c_put_byte(client,0x30ad, 0x0a);
	i2c_put_byte(client,0x30ae, 0x27);
	i2c_put_byte(client,0x363b, 0x01);
}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void ov2655_fillbuff(struct ov2655_fh *fh, struct ov2655_buffer *buf)
{
	struct ov2655_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
	/*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = ov2655_qctrl[4].default_value&3;
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = ov2655_qctrl[6].default_value;
	para.angle = ov2655_qctrl[7].default_value;
	para.vaddr = (unsigned)vbuf;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void ov2655_thread_tick(struct ov2655_fh *fh)
{
	struct ov2655_buffer *buf;
	struct ov2655_device *dev = fh->dev;
	struct ov2655_dmaqueue *dma_q = &dev->vidq;

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
			 struct ov2655_buffer, vb.queue);
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
	ov2655_fillbuff(fh, buf);
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

static void ov2655_sleep(struct ov2655_fh *fh)
{
	struct ov2655_device *dev = fh->dev;
	struct ov2655_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	ov2655_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int ov2655_thread(void *data)
{
	struct ov2655_fh  *fh = data;
	struct ov2655_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		ov2655_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int ov2655_start_thread(struct ov2655_fh *fh)
{
	struct ov2655_device *dev = fh->dev;
	struct ov2655_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(ov2655_thread, fh, "ov2655");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void ov2655_stop_thread(struct ov2655_dmaqueue  *dma_q)
{
	struct ov2655_device *dev = container_of(dma_q, struct ov2655_device, vidq);

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
	struct ov2655_fh  *fh = vq->priv_data;
	struct ov2655_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct ov2655_buffer *buf)
{
	struct ov2655_fh  *fh = vq->priv_data;
	struct ov2655_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);
       videobuf_waiton(vq, &buf->vb, 0, 0);
	if (in_interrupt())
		BUG();

	videobuf_vmalloc_free(&buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 1920
#define norm_maxh() 1600
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct ov2655_fh     *fh  = vq->priv_data;
	struct ov2655_device    *dev = fh->dev;
	struct ov2655_buffer *buf = container_of(vb, struct ov2655_buffer, vb);
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
	struct ov2655_buffer    *buf  = container_of(vb, struct ov2655_buffer, vb);
	struct ov2655_fh        *fh   = vq->priv_data;
	struct ov2655_device       *dev  = fh->dev;
	struct ov2655_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct ov2655_buffer   *buf  = container_of(vb, struct ov2655_buffer, vb);
	struct ov2655_fh       *fh   = vq->priv_data;
	struct ov2655_device      *dev  = (struct ov2655_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops ov2655_video_qops = {
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
	struct ov2655_fh  *fh  = priv;
	struct ov2655_device *dev = fh->dev;

	strcpy(cap->driver, "ov2655");
	strcpy(cap->card, "ov2655");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = OV2655_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct ov2655_fmt *fmt;

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

    if(fival->index > ARRAY_SIZE(ov2655_frmivalenum))
        return -EINVAL;

    for(k =0; k< ARRAY_SIZE(ov2655_frmivalenum); k++)
    {
        if( (fival->index==ov2655_frmivalenum[k].index)&&
                (fival->pixel_format ==ov2655_frmivalenum[k].pixel_format )&&
                (fival->width==ov2655_frmivalenum[k].width)&&
                (fival->height==ov2655_frmivalenum[k].height)){
            memcpy( fival, &ov2655_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
            return 0;
        }
    }

    return -EINVAL;

}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct ov2655_fh *fh = priv;

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
	struct ov2655_fh  *fh  = priv;
	struct ov2655_device *dev = fh->dev;
	struct ov2655_fmt *fmt;
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
	struct ov2655_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct ov2655_device *dev = fh->dev;

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
		vidio_set_fmt_ticks=1;
		OV2655_set_resolution(dev,fh->height,fh->width);
		}
	else if(vidio_set_fmt_ticks==1){
		OV2655_set_resolution(dev,fh->height,fh->width);
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
    struct ov2655_fh *fh = priv;
    struct ov2655_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = ov2655_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct ov2655_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov2655_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov2655_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov2655_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct ov2655_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct ov2655_fh  *fh = priv;
	vdin_parm_t para;
       int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para ));
       para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = ov2655_frmintervals_active.denominator;//175;
	para.h_active = ov2655_h_active;
	para.v_active = ov2655_v_active;
	para.hsync_phase = 0;
	para.vsync_phase  = 0;	
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
	struct ov2655_fh  *fh = priv;

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
	struct ov2655_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(ov2655_prev_resolution))
			return -EINVAL;
		frmsize = &ov2655_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(ov2655_pic_resolution))
			return -EINVAL;
		frmsize = &ov2655_pic_resolution[fsize->index];
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
	struct ov2655_fh *fh = priv;
	struct ov2655_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct ov2655_fh *fh = priv;
	struct ov2655_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(ov2655_qctrl); i++)
		if (qc->id && qc->id == ov2655_qctrl[i].id) {
			memcpy(qc, &(ov2655_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct ov2655_fh *fh = priv;
	struct ov2655_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(ov2655_qctrl); i++)
		if (ctrl->id == ov2655_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct ov2655_fh *fh = priv;
	struct ov2655_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(ov2655_qctrl); i++)
		if (ctrl->id == ov2655_qctrl[i].id) {
			if (ctrl->value < ov2655_qctrl[i].minimum ||
			    ctrl->value > ov2655_qctrl[i].maximum ||
			    ov2655_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int ov2655_open(struct file *file)
{
	struct ov2655_device *dev = video_drvdata(file);
	struct ov2655_fh *fh = NULL;
	int retval = 0;
#if CONFIG_CMA
    retval = vm_init_buf(16*SZ_1M);
    if(retval <0)
        return -1;
#endif
	ov2655_have_opened=1;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif
        aml_cam_init(&dev->cam_info);
	OV2655_init_regs(dev);
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

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &ov2655_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct ov2655_buffer), fh,NULL);

	ov2655_start_thread(fh);

	return 0;
}

static ssize_t
ov2655_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct ov2655_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
ov2655_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ov2655_fh        *fh = file->private_data;
	struct ov2655_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int ov2655_close(struct file *file)
{
	struct ov2655_fh         *fh = file->private_data;
	struct ov2655_device *dev       = fh->dev;
	struct ov2655_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);
	ov2655_have_opened=0;

	ov2655_stop_thread(vidq);
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
	ov2655_h_active=800;
	ov2655_v_active=598;
	ov2655_qctrl[0].default_value=0;
	ov2655_qctrl[1].default_value=4;
	ov2655_qctrl[2].default_value=0;
	ov2655_qctrl[3].default_value=0;
	
	ov2655_qctrl[4].default_value=0;
	ov2655_qctrl[6].default_value=100;
	ov2655_qctrl[7].default_value=0;
       ov2655_frmintervals_active.numerator = 1;
	ov2655_frmintervals_active.denominator = 15;

	power_down_ov2655(dev);
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

static int ov2655_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ov2655_fh  *fh = file->private_data;
	struct ov2655_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations ov2655_fops = {
	.owner		= THIS_MODULE,
	.open           = ov2655_open,
	.release        = ov2655_close,
	.read           = ov2655_read,
	.poll		= ov2655_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = ov2655_mmap,
};

static const struct v4l2_ioctl_ops ov2655_ioctl_ops = {
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

static struct video_device ov2655_template = {
	.name		= "ov2655_v4l",
	.fops           = &ov2655_fops,
	.ioctl_ops 	= &ov2655_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int ov2655_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_OV2655, 0);
}

static const struct v4l2_subdev_core_ops ov2655_core_ops = {
	.g_chip_ident = ov2655_g_chip_ident,
};

static const struct v4l2_subdev_ops ov2655_ops = {
	.core = &ov2655_core_ops,
};

static int ov2655_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	struct ov2655_device *t;
	struct v4l2_subdev *sd;
	aml_cam_info_t* plat_dat;
	vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &ov2655_ops);
	mutex_init(&t->mutex);
	this_client=client;

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &ov2655_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);
	
	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "ov2655");
	/* Register it */
	plat_dat= (aml_cam_info_t*)client->dev.platform_data;
	if (plat_dat) {
	    memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
	    if (plat_dat->front_back >=0) video_nr = plat_dat->front_back;
	} else {
	    printk("camera ov2655: have no platform data\n");
	    kfree(t);
	    return -1; 	
	}
	
	t->cam_info.version = OV2655_DRIVER_VERSION;
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

static int ov2655_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2655_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id ov2655_id[] = {
	{ "ov2655", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov2655_id);

static struct i2c_driver ov2655_i2c_driver = {
	.driver = {
		.name = "ov2655",
	},
	.probe = ov2655_probe,
	.remove = ov2655_remove,
	.id_table = ov2655_id,
};

module_i2c_driver(ov2655_i2c_driver);

