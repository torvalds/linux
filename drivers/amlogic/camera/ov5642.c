/*
 *OV5642 - This code emulates a real video device with v4l2 api
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
#include <linux/mutex.h>

#include <linux/i2c.h>
#include <media/v4l2-chip-ident.h>
#include <linux/amlogic/camera/aml_cam_info.h>
#include <linux/amlogic/vmapi.h>

#include <mach/am_regs.h>
//#include <mach/am_eth_pinmux.h>
#include <mach/pinmux.h>
#include <mach/gpio.h>

#include "common/plat_ctrl.h"
#include "ov5642_firmware.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif
#define OV5642_CAMERA_MODULE_NAME "ov5642"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define OV5642_CAMERA_MAJOR_VERSION 0
#define OV5642_CAMERA_MINOR_VERSION 7
#define OV5642_CAMERA_RELEASE 0
#define OV5642_CAMERA_VERSION \
	KERNEL_VERSION(OV5642_CAMERA_MAJOR_VERSION, OV5642_CAMERA_MINOR_VERSION, OV5642_CAMERA_RELEASE)

MODULE_DESCRIPTION("ov5642 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define OV5642_DRIVER_VERSION "OV5642-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");


//extern int disable_ov5642;
static int ov5642_have_opened = 0;

static void do_download(struct work_struct *work);
static DECLARE_DELAYED_WORK(dl_work, do_download);

static struct vdin_v4l2_ops_s *vops;

static bool bDoingAutoFocusMode=false;

static struct v4l2_fract ov5642_frmintervals_active = {
       .numerator = 1,
       .denominator = 15,
};
static struct v4l2_frmivalenum ov5642_frmivalenum[]={
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

/* supported controls */
static struct v4l2_queryctrl ov5642_qctrl[] = {
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
	}, {
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

struct v4l2_querymenu ov5642_qmenu_autofocus[] = {
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

struct v4l2_querymenu ov5642_qmenu_flashmode[] = {
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

typedef struct {
	__u32   id;
	int     num;
	struct v4l2_querymenu* ov5642_qmenu;
}ov5642_qmenu_set_t;

ov5642_qmenu_set_t ov5642_qmenu_set[] = {
	{
	 	.id             = V4L2_CID_FOCUS_AUTO,
	 	.num            = ARRAY_SIZE(ov5642_qmenu_autofocus),
	 	.ov5642_qmenu   = ov5642_qmenu_autofocus,
	}, {
		.id             = V4L2_CID_BACKLIGHT_COMPENSATION,
		.num            = ARRAY_SIZE(ov5642_qmenu_flashmode),
		.ov5642_qmenu   = ov5642_qmenu_flashmode,
	}
};

#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct ov5642_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct ov5642_fmt formats[] = {
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
	}, {
		.name     = "YVU420P",
		.fourcc   = V4L2_PIX_FMT_YVU420,
		.depth    = 12,
	}
};

static struct ov5642_fmt *get_format(struct v4l2_format *f)
{
	struct ov5642_fmt *fmt;
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
struct ov5642_buffer {
    /* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct ov5642_fmt        *fmt;
};

struct ov5642_dmaqueue {
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

static LIST_HEAD(ov5642_devicelist);

struct ov5642_device {
	struct list_head	    	ov5642_devicelist;
	struct v4l2_subdev	    	sd;
	struct v4l2_device	    	v4l2_dev;

	spinlock_t                 slock;
	struct mutex	        	mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct ov5642_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int	           input;

	/* platform device data from board initting. */
	aml_cam_info_t cam_info;
    
	/* Control 'registers' */
	int                qctl_regs[ARRAY_SIZE(ov5642_qctrl)];
	
	/* current resolution param for preview and capture */
	resolution_param_t* cur_resolution_param;
	
	/* wake lock */
	struct wake_lock	wake_lock;
	
	/* for down load firmware */
	struct work_struct dl_work;
	
	int firmware_ready;
};

static DEFINE_MUTEX(firmware_mutex);

static inline struct ov5642_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov5642_device, sd);
}

struct ov5642_fh {
	struct ov5642_device            *dev;

	/* video capture */
	struct ov5642_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int	           input;     /* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};

/*static inline struct ov5642_fh *to_fh(struct ov5642_device *dev)
{
	return container_of(dev, struct ov5642_fh, dev);
}*/



/* ------------------------------------------------------------------
	reg spec of OV5642
   ------------------------------------------------------------------*/
static struct aml_camera_i2c_fig_s OV5642_script[] = {
	{0x3103, 0x93},
	{0x3008, 0x82},
	{0x3017, 0x7f},
	{0x3018, 0xfc},
	{0x3810, 0xc2},
	{0x3615, 0xf0},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x5c},
	{0x3003, 0x00},
	{0x3004, 0xff},
	{0x3005, 0xff},
	{0x3006, 0x43},
	{0x3007, 0x37},
	{0x3011, 0x08},
	{0x3010, 0x00}, // 30fps
	{0x460c, 0x22},
	{0x3815, 0x04},
	{0x370c, 0xa0},
	{0x3602, 0xfc},
	{0x3612, 0xff},
	{0x3634, 0xc0},
	{0x3613, 0x00},
	{0x3605, 0x7c},
	{0x3621, 0x87},
	{0x3622, 0x60},
	{0x3604, 0x40},
	{0x3603, 0xa7},
	{0x3603, 0x27},
	{0x4000, 0x21},
	{0x401d, 0x22},
	{0x3600, 0x54},
	{0x3605, 0x04},
	{0x3606, 0x3f},
	{0x3c00, 0x04},
	{0x3c01, 0x80},
	{0x5000, 0x4f},
	{0x5020, 0x04},
	{0x5181, 0x79},
	{0x5182, 0x00},
	{0x5185, 0x22},
	{0x5197, 0x01},
	{0x5001, 0xff},
	{0x5500, 0x0a},
	{0x5504, 0x00},
	{0x5505, 0x7f},
	{0x5080, 0x08},
	{0x300e, 0x18},
	{0x4610, 0x00},
	{0x471d, 0x05},
	{0x4708, 0x06},
	{0x3808, 0x02},
	{0x3809, 0x80},
	{0x380a, 0x01},
	{0x380b, 0xe0},
	{0x380e, 0x07},
	{0x380f, 0xd0},
	{0x501f, 0x00},
	{0x5000, 0x4f},
	{0x4300, 0x31},
	{0x3503, 0x07},
	{0x3501, 0x73},
	{0x3502, 0x80},
	{0x350b, 0x00},
	{0x3503, 0x07},
	{0x3824, 0x11},
	{0x3501, 0x1e},
	{0x3502, 0x80},
	{0x350b, 0x7f},
	{0x380c, 0x0c},
	{0x380d, 0x80},
	{0x380e, 0x03},
	{0x380f, 0xe8},
	{0x3a0d, 0x04},
	{0x3a0e, 0x03},
	{0x3818, 0xc1},
	{0x3705, 0xdb},
	{0x370a, 0x81},
	{0x3801, 0x80},
	{0x3621, 0x87},
	{0x3801, 0x50},
	{0x3803, 0x08},
	{0x3827, 0x08},
	{0x3810, 0x40},
	{0x3804, 0x05},
	{0x3805, 0x00},
	{0x5682, 0x05},
	{0x5683, 0x00},
	{0x3806, 0x03},
	{0x3807, 0xc0},
	{0x5686, 0x03},
	{0x5687, 0xbc},
	{0x3a00, 0x78},
	{0x3a1a, 0x05},
	{0x3a13, 0x30},
	{0x3a18, 0x00},
	{0x3a19, 0x7c},
	{0x3a08, 0x12},
	{0x3a09, 0xc0},
	{0x3a0a, 0x0f},
	{0x3a0b, 0xa0},
	{0x350c, 0x07},
	{0x350d, 0xd0},
	{0x3500, 0x00},
	{0x3501, 0x00},
	{0x3502, 0x00},
	{0x350a, 0x00},
	{0x350b, 0x00},
	{0x3503, 0x00},
	{0x528a, 0x02},
	{0x528b, 0x04},
	{0x528c, 0x08},
	{0x528d, 0x08},
	{0x528e, 0x08},
	{0x528f, 0x10},
	{0x5290, 0x10},
	{0x5292, 0x00},
	{0x5293, 0x02},
	{0x5294, 0x00},
	{0x5295, 0x02},
	{0x5296, 0x00},
	{0x5297, 0x02},
	{0x5298, 0x00},
	{0x5299, 0x02},
	{0x529a, 0x00},
	{0x529b, 0x02},
	{0x529c, 0x00},
	{0x529d, 0x02},
	{0x529e, 0x00},
	{0x529f, 0x02},
	{0x3a0f, 0x3c},
	{0x3a10, 0x30},
	{0x3a1b, 0x3c},
	{0x3a1e, 0x30},
	{0x3a11, 0x70},
	{0x3a1f, 0x10},
	//{0x3030, 0x0b}, //2b
	{0x3030, 0x2b},
	{0x3a02, 0x00},
	{0x3a03, 0x7d},
	{0x3a04, 0x00},
	{0x3a14, 0x00},
	{0x3a15, 0x7d},
	{0x3a16, 0x00},
	{0x3a00, 0x78},
	{0x5193, 0x70},
	{0x589b, 0x04},
	{0x589a, 0xc5},
	{0x401e, 0x20},
	{0x4001, 0x42},
	{0x401c, 0x04},
	{0x528a, 0x01},
	{0x528b, 0x04},
	{0x528c, 0x08},
	{0x528d, 0x10},
	{0x528e, 0x20},
	{0x528f, 0x28},
	{0x5290, 0x30},
	{0x5292, 0x00},
	{0x5293, 0x01},
	{0x5294, 0x00},
	{0x5295, 0x04},
	{0x5296, 0x00},
	{0x5297, 0x08},
	{0x5298, 0x00},
	{0x5299, 0x10},
	{0x529a, 0x00},
	{0x529b, 0x20},
	{0x529c, 0x00},
	{0x529d, 0x28},
	{0x529e, 0x00},
	{0x529f, 0x30},
	{0x5282, 0x00},
	{0x5300, 0x00},
	{0x5301, 0x20},
	{0x5302, 0x00},
	{0x5303, 0x7c},
	{0x530c, 0x00},
	{0x530d, 0x0c},
	{0x530e, 0x20},
	{0x530f, 0x80},
	{0x5310, 0x20},
	{0x5311, 0x80},
	{0x5308, 0x20},
	{0x5309, 0x40},
	{0x5304, 0x00},
	{0x5305, 0x30},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5314, 0x08},
	{0x5315, 0x20},
	{0x5319, 0x30},
	{0x5316, 0x10},
	{0x5317, 0x00},
	{0x5318, 0x02},
	{0x5380, 0x01},
	{0x5381, 0x00},
	{0x5382, 0x00},
	{0x5383, 0x4e},
	{0x5384, 0x00},
	{0x5385, 0x0f},
	{0x5386, 0x00},
	{0x5387, 0x00},
	{0x5388, 0x01},
	{0x5389, 0x15},
	{0x538a, 0x00},
	{0x538b, 0x31},
	{0x538c, 0x00},
	{0x538d, 0x00},
	{0x538e, 0x00},
	{0x538f, 0x0f},
	{0x5390, 0x00},
	{0x5391, 0xab},
	{0x5392, 0x00},
	{0x5393, 0xa2},
	{0x5394, 0x08},
	{0x5480, 0x14},
	{0x5481, 0x21},
	{0x5482, 0x36},
	{0x5483, 0x57},
	{0x5484, 0x65},
	{0x5485, 0x71},
	{0x5486, 0x7d},
	{0x5487, 0x87},
	{0x5488, 0x91},
	{0x5489, 0x9a},
	{0x548a, 0xaa},
	{0x548b, 0xb8},
	{0x548c, 0xcd},
	{0x548d, 0xdd},
	{0x548e, 0xea},
	{0x548f, 0x1d},
	{0x5490, 0x05},
	{0x5491, 0x00},
	{0x5492, 0x04},
	{0x5493, 0x20},
	{0x5494, 0x03},
	{0x5495, 0x60},
	{0x5496, 0x02},
	{0x5497, 0xb8},
	{0x5498, 0x02},
	{0x5499, 0x86},
	{0x549a, 0x02},
	{0x549b, 0x5b},
	{0x549c, 0x02},
	{0x549d, 0x3b},
	{0x549e, 0x02},
	{0x549f, 0x1c},
	{0x54a0, 0x02},
	{0x54a1, 0x04},
	{0x54a2, 0x01},
	{0x54a3, 0xed},
	{0x54a4, 0x01},
	{0x54a5, 0xc5},
	{0x54a6, 0x01},
	{0x54a7, 0xa5},
	{0x54a8, 0x01},
	{0x54a9, 0x6c},
	{0x54aa, 0x01},
	{0x54ab, 0x41},
	{0x54ac, 0x01},
	{0x54ad, 0x20},
	{0x54ae, 0x00},
	{0x54af, 0x16},
	{0x54b0, 0x01},
	{0x54b1, 0x20},
	{0x54b2, 0x00},
	{0x54b3, 0x10},
	{0x54b4, 0x00},
	{0x54b5, 0xf0},
	{0x54b6, 0x00},
	{0x54b7, 0xdf},
	{0x5402, 0x3f},
	{0x5403, 0x00},
	{0x3406, 0x00},
	{0x5180, 0xff},
	{0x5181, 0x52},
	{0x5182, 0x11},
	{0x5183, 0x14},
	{0x5184, 0x25},
	{0x5185, 0x24},
	{0x5186, 0x06},
	{0x5187, 0x08},
	{0x5188, 0x08},
	{0x5189, 0x7c},
	{0x518a, 0x60},
	{0x518b, 0xb2},
	{0x518c, 0xb2},
	{0x518d, 0x44},
	{0x518e, 0x3d},
	{0x518f, 0x58},
	{0x5190, 0x46},
	{0x5191, 0xf8},
	{0x5192, 0x04},
	{0x5193, 0x70},
	{0x5194, 0xf0},
	{0x5195, 0xf0},
	{0x5196, 0x03},
	{0x5197, 0x01},
	{0x5198, 0x04},
	{0x5199, 0x12},
	{0x519a, 0x04},
	{0x519b, 0x00},
	{0x519c, 0x06},
	{0x519d, 0x82},
	{0x519e, 0x00},
	{0x5025, 0x80},
	{0x3a0f, 0x38},
	{0x3a10, 0x30},
	{0x3a1b, 0x3a},
	{0x3a1e, 0x2e},
	{0x3a11, 0x60},
	{0x3a1f, 0x10},
	{0x5688, 0xa6},
	{0x5689, 0x6a},
	{0x568a, 0xea},
	{0x568b, 0xae},
	{0x568c, 0xa6},
	{0x568d, 0x6a},
	{0x568e, 0x62},
	{0x568f, 0x26},
	{0x5583, 0x40},
	{0x5584, 0x40},
	{0x5580, 0x02},
	{0x5000, 0xcf},
	{0x5800, 0x27},
	{0x5801, 0x19},
	{0x5802, 0x12},
	{0x5803, 0x0f},
	{0x5804, 0x10},
	{0x5805, 0x15},
	{0x5806, 0x1e},
	{0x5807, 0x2f},
	{0x5808, 0x15},
	{0x5809, 0x0d},
	{0x580a, 0x0a},
	{0x580b, 0x09},
	{0x580c, 0x0a},
	{0x580d, 0x0c},
	{0x580e, 0x12},
	{0x580f, 0x19},
	{0x5810, 0x0b},
	{0x5811, 0x07},
	{0x5812, 0x04},
	{0x5813, 0x03},
	{0x5814, 0x03},
	{0x5815, 0x06},
	{0x5816, 0x0a},
	{0x5817, 0x0f},
	{0x5818, 0x0a},
	{0x5819, 0x05},
	{0x581a, 0x01},
	{0x581b, 0x00},
	{0x581c, 0x00},
	{0x581d, 0x03},
	{0x581e, 0x08},
	{0x581f, 0x0c},
	{0x5820, 0x0a},
	{0x5821, 0x05},
	{0x5822, 0x01},
	{0x5823, 0x00},
	{0x5824, 0x00},
	{0x5825, 0x03},
	{0x5826, 0x08},
	{0x5827, 0x0c},
	{0x5828, 0x0e},
	{0x5829, 0x08},
	{0x582a, 0x06},
	{0x582b, 0x04},
	{0x582c, 0x05},
	{0x582d, 0x07},
	{0x582e, 0x0b},
	{0x582f, 0x12},
	{0x5830, 0x18},
	{0x5831, 0x10},
	{0x5832, 0x0c},
	{0x5833, 0x0a},
	{0x5834, 0x0b},
	{0x5835, 0x0e},
	{0x5836, 0x15},
	{0x5837, 0x19},
	{0x5838, 0x32},
	{0x5839, 0x1f},
	{0x583a, 0x18},
	{0x583b, 0x16},
	{0x583c, 0x17},
	{0x583d, 0x1e},
	{0x583e, 0x26},
	{0x583f, 0x53},
	{0x5840, 0x10},
	{0x5841, 0x0f},
	{0x5842, 0x0d},
	{0x5843, 0x0c},
	{0x5844, 0x0e},
	{0x5845, 0x09},
	{0x5846, 0x11},
	{0x5847, 0x10},
	{0x5848, 0x10},
	{0x5849, 0x10},
	{0x584a, 0x10},
	{0x584b, 0x0e},
	{0x584c, 0x10},
	{0x584d, 0x10},
	{0x584e, 0x11},
	{0x584f, 0x10},
	{0x5850, 0x0f},
	{0x5851, 0x0c},
	{0x5852, 0x0f},
	{0x5853, 0x10},
	{0x5854, 0x10},
	{0x5855, 0x0f},
	{0x5856, 0x0e},
	{0x5857, 0x0b},
	{0x5858, 0x10},
	{0x5859, 0x0d},
	{0x585a, 0x0d},
	{0x585b, 0x0c},
	{0x585c, 0x0c},
	{0x585d, 0x0c},
	{0x585e, 0x0b},
	{0x585f, 0x0c},
	{0x5860, 0x0c},
	{0x5861, 0x0c},
	{0x5862, 0x0d},
	{0x5863, 0x08},
	{0x5864, 0x11},
	{0x5865, 0x18},
	{0x5866, 0x18},
	{0x5867, 0x19},
	{0x5868, 0x17},
	{0x5869, 0x19},
	{0x586a, 0x16},
	{0x586b, 0x13},
	{0x586c, 0x13},
	{0x586d, 0x12},
	{0x586e, 0x13},
	{0x586f, 0x16},
	{0x5870, 0x14},
	{0x5871, 0x12},
	{0x5872, 0x10},
	{0x5873, 0x11},
	{0x5874, 0x11},
	{0x5875, 0x16},
	{0x5876, 0x14},
	{0x5877, 0x11},
	{0x5878, 0x10},
	{0x5879, 0x0f},
	{0x587a, 0x10},
	{0x587b, 0x14},
	{0x587c, 0x13},
	{0x587d, 0x12},
	{0x587e, 0x11},
	{0x587f, 0x11},
	{0x5880, 0x12},
	{0x5881, 0x15},
	{0x5882, 0x14},
	{0x5883, 0x15},
	{0x5884, 0x15},
	{0x5885, 0x15},
	{0x5886, 0x13},
	{0x5887, 0x17},
	{0x3710, 0x10},
	{0x3632, 0x51},
	{0x3702, 0x10},
	{0x3703, 0xb2},
	{0x3704, 0x18},
	{0x370b, 0x40},
	{0x370d, 0x03},
	{0x3631, 0x01},
	{0x3632, 0x52},
	{0x3606, 0x24},
	{0x3620, 0x96},
	{0x5785, 0x07},
	{0x3a13, 0x30},
	{0x3600, 0x52},
	{0x3604, 0x48},
	{0x3606, 0x1b},
	{0x370d, 0x0b},
	{0x370f, 0xc0},
	{0x3709, 0x01},
	{0x3823, 0x00},
	{0x5007, 0x00},
	{0x5009, 0x00},
	{0x5011, 0x00},
	{0x5013, 0x00},
	{0x519e, 0x00},
	{0x5086, 0x00},
	{0x5087, 0x00},
	{0x5088, 0x00},
	{0x5089, 0x00},
	{0x302b, 0x00},
	{0xffff, 0xff}
};

#define PREVIEW_22_FRAME 

struct aml_camera_i2c_fig_s OV5642_preview_script[] = {
	{0x3503, 0x00},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x5c},
	{0x3003, 0x00},
	{0x3004, 0xff},
	{0x3005, 0xff},
	{0x3006, 0x43},
	{0x3007, 0x37},
	
	{0x3818, 0xc1},
	{0x3621, 0x87},
	
	{0x350c, 0x03},
	{0x350d, 0xe8},
	{0x3602, 0xfc},
	{0x3612, 0xff},
	{0x3613, 0x00},
	{0x3622, 0x60},
	{0x3623, 0x01},
	{0x3604, 0x48},
	{0x3705, 0xdb},
	{0x370a, 0x81},
	
	{0x3801, 0x50},
	{0x3803, 0x08},
	
	{0x3804, 0x05},
	{0x3805, 0x00},
	{0x3806, 0x03},
	{0x3807, 0xc0},
	{0x3808, 0x02},
	{0x3809, 0x80},
	{0x380a, 0x01},
	{0x380b, 0xe0},
	{0x380c, 0x0c},
	{0x380d, 0x80},
	{0x380e, 0x03},
	{0x380f, 0xe8},
	{0x3810, 0x40},
	{0x3815, 0x04},
	
	{0x3824, 0x11},
	
	{0x3825, 0xb4},
	{0x3827, 0x08},
	{0x3a00, 0x78},
	{0x3a0d, 0x04},
	{0x3a0e, 0x03},
	{0x5682, 0x05},
	{0x5683, 0x00},
	{0x5686, 0x03},
	{0x5687, 0xbc},
	{0x5001, 0xff},
	{0x589b, 0x04},
	{0x589a, 0xc5},
	{0x4407, 0x0c},
	{0x3008, 0x02},
	{0x460b, 0x37},
	{0x460c, 0x22},
	{0x471d, 0x05},
	{0x4713, 0x02},
	{0x471c, 0xd0},
	{0x3815, 0x04},
	{0x501f, 0x00},
	{0x3002, 0x5c},
	{0x3819, 0x80},
	{0x5002, 0xe0},
	{0x530a, 0x01},
	{0x530d, 0x0c},
	{0x530c, 0x00},
	{0x5312, 0x40},
	{0x5282, 0x00},
#ifdef PREVIEW_22_FRAME
	{0x3010, 0x00},
	{0x3011, 0x06},
	{0x3a08, 0x0e}, //50hz
	{0x3a09, 0x10}, //50hz
	{0x3a0e, 0x04}, //50hz
	
	{0x3a0a, 0x0b}, //60hz
	{0x3a0b, 0xc0}, //60hz
	{0x3a0d, 0x05}, //60hz
#else
	{0x3010, 0x10},//00
	{0x3011, 0x08},//00
	
	{0x3a08, 0x09}, //50hz
	{0x3a09, 0x60}, //50hz
	{0x3a0e, 0x06}, //50hz
	
	{0x3a0a, 0x07}, //60hz
	{0x3a0b, 0xd0}, //60hz
	{0x3a0d, 0x08}, //60hz
#endif
	{0xffff, 0xff}
};

struct aml_camera_i2c_fig_s OV5642_preview_QVGA_script[] = {
	{0x3503, 0x00},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x5c},
	{0x3003, 0x00},
	{0x3004, 0xff},
	{0x3005, 0xff},
	{0x3006, 0x43},
	{0x3007, 0x37},
	{0x3010, 0x10},//00
	
	{0x3818, 0xc1},
	{0x3621, 0x87},
	
	{0x350c, 0x03},
	{0x350d, 0xe8},
	{0x3602, 0xfc},
	{0x3612, 0xff},
	{0x3613, 0x00},
	{0x3622, 0x60},
	{0x3623, 0x01},
	{0x3604, 0x48},
	{0x3705, 0xdb},
	{0x370a, 0x81},
	
	{0x3801, 0x50},
	{0x3803, 0x08},
	
	{0x3804, 0x05},
	{0x3805, 0x00},
	{0x3806, 0x03},
	{0x3807, 0xc0},
	{0x3808, 0x01},
	{0x3809, 0x40},
	{0x380a, 0x00},
	{0x380b, 0xf0},
	{0x380c, 0x0c},
	{0x380d, 0x80},
	{0x380e, 0x03},
	{0x380f, 0xe8},
	{0x3810, 0x40},
	{0x3815, 0x04},
	
	{0x3824, 0x11},
	
	{0x3825, 0xb4},
	{0x3827, 0x08},
	{0x3a00, 0x78},
	{0x3a0d, 0x04},
	{0x3a0e, 0x03},
	{0x5682, 0x05},
	{0x5683, 0x00},
	{0x5686, 0x03},
	{0x5687, 0xbc},
	{0x5001, 0xff},
	{0x589b, 0x04},
	{0x589a, 0xc5},
	{0x4407, 0x0c},
	{0x3008, 0x02},
	{0x460b, 0x37},
	{0x460c, 0x22},
	{0x471d, 0x05},
	{0x4713, 0x02},
	{0x471c, 0xd0},
	{0x3815, 0x04},
	{0x501f, 0x00},
	{0x3002, 0x5c},
	{0x3819, 0x80},
	{0x5002, 0xe0},
	{0x530a, 0x01},
	{0x530d, 0x0c},
	{0x530c, 0x00},
	{0x5312, 0x40},
	{0x5282, 0x00},
	{0xffff, 0xff}
};

struct aml_camera_i2c_fig_s OV5642_capture_5M_script[] = {
	{0x3503, 0x07},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3003, 0x00},
	{0x3004, 0xff},
	{0x3005, 0xff},
	{0x3006, 0xff},
	{0x3007, 0x3f},
	{0x3010, 0x30},
	{0x3011, 0x08},
	
	{0x3818, 0xc0},
	{0x3621, 0x09},
	
	{0x350c, 0x07},
	{0x350d, 0xd0},
	{0x3602, 0xe4},
	{0x3612, 0xac},
	{0x3613, 0x44},
	{0x3622, 0x60},
	{0x3623, 0x22},
	{0x3604, 0x48},
	{0x3705, 0xda},
	{0x370a, 0x80},
	
	{0x3801, 0x95},
	{0x3803, 0x0e},
	
	{0x3804, 0x0a},
	{0x3805, 0x20},
	{0x3806, 0x07},
	{0x3807, 0x98},
	{0x3808, 0x0a},
	{0x3809, 0x20},
	{0x380a, 0x07},
	{0x380b, 0x98},
	{0x380c, 0x0c},
	{0x380d, 0x80},
	{0x380e, 0x07},
	{0x380f, 0xd0},
	{0x3810, 0xc2},
	{0x3815, 0x44},
	
	{0x3824, 0x11},
	
	{0x3825, 0xac},
	{0x3827, 0x0c},
	{0x3a00, 0x78},
	{0x3a0d, 0x10},
	{0x3a0e, 0x0d},
	{0x5682, 0x0a},
	{0x5683, 0x20},
	{0x5686, 0x07},
	{0x5687, 0x98},
	{0x5001, 0xff},
	{0x589b, 0x00},
	{0x589a, 0xc0},
	{0x4407, 0x04},
	{0x3008, 0x02},
	{0x460b, 0x37},
	{0x460c, 0x22},
	{0x471d, 0x05},
	{0x4713, 0x03},
	{0x471c, 0xd0},
	{0x3815, 0x01},
	
	{0x501f, 0x00},
	{0x3002, 0x1c},
	{0x3819, 0x80},
	{0x5002, 0xe0},
	{0x530a, 0x01},
	{0x530d, 0x10},
	{0x530c, 0x04},
	{0x5312, 0x20},
	{0x5282, 0x01},
	{0xffff, 0xff}
};

struct aml_camera_i2c_fig_s OV5642_capture_3M_script[] = {
	{0x3503, 0x07},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3003, 0x00},
	{0x3004, 0xff},
	{0x3005, 0xff},
	{0x3006, 0xff},
	{0x3007, 0x3f},
	{0x3010, 0x30},
	{0x3011, 0x08},
	
	{0x3818, 0xc0},
	{0x3621, 0x09},
	
	{0x350c, 0x07},
	{0x350d, 0xd0},
	{0x3602, 0xe4},
	{0x3612, 0xac},
	{0x3613, 0x44},
	{0x3622, 0x60},
	{0x3623, 0x22},
	{0x3604, 0x48},
	{0x3705, 0xda},
	{0x370a, 0x80},
	
	{0x3801, 0x95},
	{0x3803, 0x0e},
	
	{0x3804, 0x0a},
	{0x3805, 0x20},
	{0x3806, 0x07},
	{0x3807, 0x98},
	{0x3808, 0x08},
	{0x3809, 0x00},
	{0x380a, 0x06},
	{0x380b, 0x00},
	{0x380c, 0x0c},
	{0x380d, 0x80},
	{0x380e, 0x07},
	{0x380f, 0xd0},
	{0x3810, 0xc2},
	{0x3815, 0x44},
	
	{0x3824, 0x11},
	
	{0x3825, 0xac},
	{0x3827, 0x0c},
	{0x3a00, 0x78},
	{0x3a0d, 0x10},
	{0x3a0e, 0x0d},
	{0x5682, 0x0a},
	{0x5683, 0x20},
	{0x5686, 0x07},
	{0x5687, 0x98},
	{0x5001, 0xff},
	{0x589b, 0x00},
	{0x589a, 0xc0},
	{0x4407, 0x04},
	{0x3008, 0x02},
	{0x460b, 0x37},
	{0x460c, 0x22},
	{0x471d, 0x05},
	{0x4713, 0x03},
	{0x471c, 0xd0},
	{0x3815, 0x01},
	
	{0x501f, 0x00},
	{0x3002, 0x1c},
	{0x3819, 0x80},
	{0x5002, 0xe0},
	{0x530a, 0x01},
	{0x530d, 0x10},
	{0x530c, 0x04},
	{0x5312, 0x20},
	{0x5282, 0x01},
	{0xffff, 0xff}
};

struct aml_camera_i2c_fig_s OV5642_capture_2M_script[] = {
	{0x3503, 0x07},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3003, 0x00},
	{0x3004, 0xff},
	{0x3005, 0xff},
	{0x3006, 0xff},
	{0x3007, 0x3f},
	{0x3010, 0x30},
	{0x3011, 0x08},
	
	{0x3818, 0xc0},
	{0x3621, 0x09},
	
	{0x350c, 0x07},
	{0x350d, 0xd0},
	{0x3602, 0xe4},
	{0x3612, 0xac},
	{0x3613, 0x44},
	{0x3622, 0x60},
	{0x3623, 0x22},
	{0x3604, 0x48},
	{0x3705, 0xda},
	{0x370a, 0x80},
	
	{0x3801, 0x95},
	{0x3803, 0x0e},
	
	{0x3804, 0x0a},
	{0x3805, 0x20},
	{0x3806, 0x07},
	{0x3807, 0x98},
	{0x3808, 0x06},
	{0x3809, 0x40},
	{0x380a, 0x04},
	{0x380b, 0xb0},
	{0x380c, 0x0c},
	{0x380d, 0x80},
	{0x380e, 0x07},
	{0x380f, 0xd0},
	{0x3810, 0xc2},
	{0x3815, 0x44},
	
	{0x3824, 0x11},
	
	{0x3825, 0xac},
	{0x3827, 0x0c},
	{0x3a00, 0x78},
	{0x3a0d, 0x10},
	{0x3a0e, 0x0d},
	{0x5682, 0x0a},
	{0x5683, 0x20},
	{0x5686, 0x07},
	{0x5687, 0x98},
	{0x5001, 0xff},
	{0x589b, 0x00},
	{0x589a, 0xc0},
	{0x4407, 0x04},
	{0x3008, 0x02},
	{0x460b, 0x37},
	{0x460c, 0x22},
	{0x471d, 0x05},
	{0x4713, 0x03},
	{0x471c, 0xd0},
	{0x3815, 0x01},
	
	{0x501f, 0x00},
	{0x3002, 0x1c},
	{0x3819, 0x80},
	{0x5002, 0xe0},
	{0x530a, 0x01},
	{0x530d, 0x10},
	{0x530c, 0x04},
	{0x5312, 0x20},
	{0x5282, 0x01},
	{0xffff, 0xff}
};

static resolution_param_t  prev_resolution_array[] = {
	{
		.frmsize			= {640, 480},
		.active_frmsize			= {640, 478},
		.active_fps			= 236,
		.size_type			= SIZE_640X480,
		.reg_script			= OV5642_preview_script,
	}, {
		.frmsize			= {320, 240},
		.active_frmsize			= {320, 240},
		.active_fps			= 236,
		.size_type			= SIZE_320X240,
		.reg_script			= OV5642_preview_QVGA_script,
	},{
		.frmsize			= {352, 288},
		.active_frmsize			= {320, 240},
		.active_fps			= 236,
		.size_type			= SIZE_352X288,
		.reg_script			= OV5642_preview_QVGA_script,
	},{
		.frmsize			= {176, 144},
		.active_frmsize			= {320, 240},
		.active_fps			= 236,
		.size_type			= SIZE_176X144,
		.reg_script			= OV5642_preview_QVGA_script,
	},
};

static resolution_param_t  capture_resolution_array[] = {
	{
		.frmsize			= {2592, 1944},
		.active_frmsize			= {2592, 1942},
		.active_fps			= 150,
		.size_type			= SIZE_2592X1944,
		.reg_script			= OV5642_capture_5M_script,
	}, {
		.frmsize			= {1600, 1200},
		.active_frmsize			= {1600, 1198},
		.active_fps			= 150,
		.size_type			= SIZE_1600X1200,
		.reg_script			= OV5642_capture_2M_script,
	},{
		.frmsize			= {2048, 1536},
		.active_frmsize			= {2032, 1534},
		.active_fps			= 150,
		.size_type			= SIZE_2048X1536,
		.reg_script			= OV5642_capture_3M_script,
	},
};

// download firmware by single i2c write
int OV5642_download_firmware(struct ov5642_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int ret = 0;
#if 0
    int totalCnt = 0; 
    int index=0; 
    unsigned short addr = 0x8000; 

    i2c_put_byte(client, 0x3000, 0x20);
    totalCnt = ARRAY_SIZE(ad5820_firmware); 

    while (index < totalCnt) {        
        i2c_put_byte(client, addr, ad5820_firmware[index]);
        index++;
        addr++
    }
    printk("addr = 0x%x\n", addr);
    printk("index = 0x%x\n", index);
    printk("addr = %d\n", sentCnt);
    i2c_put_byte(client, 0x3022, 0x00);
    i2c_put_byte(client, 0x3023, 0x00);
    i2c_put_byte(client, 0x3024, 0x00);
    i2c_put_byte(client, 0x3025, 0x00);
    i2c_put_byte(client, 0x3026, 0x00);
    i2c_put_byte(client, 0x3027, 0x00);
    i2c_put_byte(client, 0x3028, 0x00);
    i2c_put_byte(client, 0x3029, 0x7f);
    i2c_put_byte(client, 0x3000, 0x00);  
#else
	int i=0;
	i2c_put_byte(client, 0x3000, 0x20);
	msleep(20);
	while(1)
	{
		if (OV5642_AF_firmware[i].val==0xff&&OV5642_AF_firmware[i].addr==0xffff) 
		{
			printk("download firmware success in initial OV5642.\n");
			break;
		}
		if((i2c_put_byte(client,OV5642_AF_firmware[i].addr, OV5642_AF_firmware[i].val)) < 0)
		{
			printk("fail in download firmware OV5642. i=%d\n",i);
			ret = -1;
			break;
		}
		i++;
	}
#endif
	return ret;
}

static camera_focus_mode_t start_focus_mode = CAM_FOCUS_MODE_RELEASE;
static int OV5642_AutoFocus(struct ov5642_device *dev, int focus_mode);

static void do_download(struct work_struct *work)
{
	struct ov5642_device *dev = container_of(work, struct ov5642_device, dl_work);
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//int mcu_on = 0, afc_on = 0;
	int ret;
	int i = 10;
	mutex_lock(&firmware_mutex);
	if(OV5642_download_firmware(dev) >= 0) {
		while(i-- && ov5642_have_opened) {
			ret = i2c_get_byte(client, FW_STATUS);
			if (ret == 0x70) {
				pr_info("down load firmware suscess!!!!\n");
				break;
			} 
			msleep(5);
			pr_info("FW_STATUS:0x%x\n", ret);
		}
	}
	dev->firmware_ready = 1;
	mutex_unlock(&firmware_mutex);
	if (start_focus_mode) {
		OV5642_AutoFocus(dev,(int)start_focus_mode);
		start_focus_mode = CAM_FOCUS_MODE_RELEASE;
	}
}

void OV5642_init_regs(struct ov5642_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int i=0;
	
	while(1) {
	 	if (OV5642_script[i].val == 0xff && OV5642_script[i].addr==0xffff) {
	 		printk("success in initial OV5642.\n");
	 		break;
	 	}
	 	if ((i2c_put_byte(client,OV5642_script[i].addr, OV5642_script[i].val)) < 0) {
	 		printk("fail in initial OV5642. \n");
			return;
		}
		i++;
	}
	return;
}

static unsigned long ov5642_preview_exposure;
static unsigned long ov5642_gain;
static unsigned long ov5642_preview_maxlines;

static unsigned char preview_reg3500;
static unsigned char preview_reg3501;
static unsigned char preview_reg3502;
static unsigned char preview_reg350b;


static int rewrite_preview_gain_exposure(struct ov5642_device *dev)
{
	//int rc = 0;
	//unsigned char reg_l, reg_m, reg_h;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
	i2c_put_byte(client, 0x3500, preview_reg3500);
	i2c_put_byte(client, 0x3501, preview_reg3501);
	i2c_put_byte(client, 0x3502, preview_reg3502);
	
	i2c_put_byte(client, 0x350b, preview_reg350b);
	return 0;
}
	

static int get_preview_exposure_gain(struct ov5642_device *dev)
{
	int rc = 0;
	unsigned int ret_l,ret_m,ret_h;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	ret_h = ret_m = ret_l = 0;
	
	i2c_put_byte(client, 0x3503, 0x07);//stop aec/agc
	//get preview exp & gain
	ret_h = ret_m = ret_l = 0;
	ov5642_preview_exposure = 0;
	preview_reg3500 = ret_h = i2c_get_byte(client, 0x3500);
	preview_reg3501 = ret_m = i2c_get_byte(client,0x3501);
	preview_reg3502 = ret_l = i2c_get_byte(client,0x3502);
	ov5642_preview_exposure = (ret_h << 12) + (ret_m << 4) + (ret_l >> 4);
	//printk("preview_exposure=%d\n", ov5642_preview_exposure);
	
	ret_h = ret_m = ret_l = 0;
	ov5642_preview_maxlines = 0;
	ret_h = i2c_get_byte(client, 0x380e);
	ret_l = i2c_get_byte(client, 0x380f);
	ov5642_preview_maxlines = (ret_h << 8) + ret_l;
	//printk("Preview_Maxlines=%d\n", ov5642_preview_maxlines);
	
	//Read back AGC Gain for preview
	ov5642_gain = 0;
	preview_reg350b = ov5642_gain = i2c_get_byte(client, 0x350b);
	//printk("Gain,0x350b=0x%x\n", ov5642_gain);

	return rc;
}

#ifdef PREVIEW_22_FRAME
#define CAPTURE_FRAMERATE 750
#define PREVIEW_FRAMERATE 2250
#else
#define CAPTURE_FRAMERATE 750
#define PREVIEW_FRAMERATE 1500
#endif
static int cal_exposure(struct ov5642_device *dev)
{
	int rc = 0;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//calculate capture exp & gain
	unsigned char ExposureLow,ExposureMid,ExposureHigh;
	unsigned int ret_l,ret_m,ret_h,Lines_10ms;
	unsigned short ulCapture_Exposure,iCapture_Gain;
	unsigned int ulCapture_Exposure_Gain,Capture_MaxLines;
	ret_h = ret_m = ret_l = 0;
	ret_h = i2c_get_byte(client, 0x380e);
	ret_l = i2c_get_byte(client, 0x380f);
	Capture_MaxLines = (ret_h << 8) + ret_l;
	printk("Capture_MaxLines=%d\n", Capture_MaxLines);
	if (ov5642_qctrl[7].default_value == CAM_BANDING_60HZ) { //60Hz
		Lines_10ms = CAPTURE_FRAMERATE * Capture_MaxLines/12000;
	} else {
		Lines_10ms = CAPTURE_FRAMERATE * Capture_MaxLines/10000;
	}
	if (ov5642_preview_maxlines == 0) {
		ov5642_preview_maxlines = 1;
	}
	
	ulCapture_Exposure = (ov5642_preview_exposure*(CAPTURE_FRAMERATE)*(Capture_MaxLines))/
	(((ov5642_preview_maxlines)*(PREVIEW_FRAMERATE)));
	
	iCapture_Gain = (ov5642_gain & 0x0f) + 16;

	if (ov5642_gain & 0x10) {
		iCapture_Gain = iCapture_Gain << 1;
	}
	
	if (ov5642_gain & 0x20)
	{
		iCapture_Gain = iCapture_Gain << 1;
	}
	
	if (ov5642_gain & 0x40)
	{
		iCapture_Gain = iCapture_Gain << 1;
	}
	
	if (ov5642_gain & 0x80)
	{
		iCapture_Gain = iCapture_Gain << 1;
	}

	ulCapture_Exposure_Gain = ulCapture_Exposure * iCapture_Gain;
	if (ulCapture_Exposure_Gain < Capture_MaxLines*16) {
		ulCapture_Exposure = ulCapture_Exposure_Gain/16;
		if (ulCapture_Exposure > Lines_10ms) {
			ulCapture_Exposure /= Lines_10ms;
			ulCapture_Exposure *= Lines_10ms;
		}
	} else {
		ulCapture_Exposure = Capture_MaxLines;
	}
	if (ulCapture_Exposure == 0) {
		ulCapture_Exposure = 1;
	}
	iCapture_Gain = (ulCapture_Exposure_Gain*2/ulCapture_Exposure + 1)/2;
	
	ExposureLow = ((unsigned char)ulCapture_Exposure) << 4;
	ExposureMid = (unsigned char)(ulCapture_Exposure >> 4) & 0xff;
	ExposureHigh = (unsigned char)(ulCapture_Exposure >> 12);
	
	ov5642_gain = 0;

	if (iCapture_Gain > 31) {
		ov5642_gain |= 0x10;
		iCapture_Gain = iCapture_Gain >> 1;
	}

	if (iCapture_Gain > 31) {
		ov5642_gain |= 0x20;
		iCapture_Gain = iCapture_Gain >> 1;
	}

	if (iCapture_Gain > 31) {
		ov5642_gain |= 0x40;
		iCapture_Gain = iCapture_Gain >> 1;
	}

	if (iCapture_Gain > 31) {
		ov5642_gain |= 0x80;
		iCapture_Gain = iCapture_Gain >> 1;
	}

	if (iCapture_Gain > 16) {
		ov5642_gain |= ((iCapture_Gain -16) & 0x0f);
	}

	if (ov5642_gain == 0x10) {
		ov5642_gain = 0x11;
	}
	
	i2c_put_byte(client, 0x350b, ov5642_gain);
	i2c_put_byte(client, 0x3502, ExposureLow);
	i2c_put_byte(client, 0x3501, ExposureMid);
	i2c_put_byte(client, 0x3500, ExposureHigh);
	
	//printk("ov5642_gain=%d\n", ov5642_gain);
	printk("ExposureLow=%d\n", ExposureLow);
	printk("ExposureMid=%d\n", ExposureMid);
	printk("ExposureHigh=%d\n", ExposureHigh);
	msleep(50);
	return rc;
}

/*************************************************************************
* FUNCTION
*    OV5642_set_param_wb
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
void OV5642_set_param_wb(struct ov5642_device *dev,enum  camera_wb_flip_e para)//白平衡
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	switch (para) {      
    	case CAM_WB_AUTO://自动
        	i2c_put_byte(client, 0x3212, 0x03);
            i2c_put_byte(client, 0x3406, 0x00);
            i2c_put_byte(client, 0x3400, 0x04);
            i2c_put_byte(client, 0x3401, 0x00);
            i2c_put_byte(client, 0x3402, 0x04);
            i2c_put_byte(client, 0x3403, 0x00);
            i2c_put_byte(client, 0x3404, 0x04);
            i2c_put_byte(client, 0x3405, 0x00);
            i2c_put_byte(client, 0x3212, 0x13);
            i2c_put_byte(client, 0x3212, 0xa3);

        	break;

    	case CAM_WB_CLOUD: //阴天
        	i2c_put_byte(client, 0x3212, 0x03);
        	i2c_put_byte(client,0x3406 , 0x01);
        	i2c_put_byte(client,0x3400 , 0x06);
        	i2c_put_byte(client,0x3401 , 0x48);
        	i2c_put_byte(client,0x3402 , 0x04);
        	i2c_put_byte(client,0x3403 , 0x00);
        	i2c_put_byte(client,0x3404 , 0x04);
        	i2c_put_byte(client,0x3405 , 0xd3);
            i2c_put_byte(client, 0x3212, 0x13);
            i2c_put_byte(client, 0x3212, 0xa3);            

        	break;

    	case CAM_WB_DAYLIGHT: //
        	i2c_put_byte(client, 0x3212, 0x03);
        	i2c_put_byte(client,0x3406 , 0x01);
        	i2c_put_byte(client,0x3400 , 0x06);
        	i2c_put_byte(client,0x3401 , 0x1c);
        	i2c_put_byte(client,0x3402 , 0x04);
        	i2c_put_byte(client,0x3403 , 0x00);
        	i2c_put_byte(client,0x3404 , 0x04);
        	i2c_put_byte(client,0x3405 , 0xf3);
            i2c_put_byte(client, 0x3212, 0x13);
            i2c_put_byte(client, 0x3212, 0xa3);          

        	break;

    	case CAM_WB_INCANDESCENCE: 
            i2c_put_byte(client, 0x3212, 0x03);
            i2c_put_byte(client, 0x3406, 0x01);
            i2c_put_byte(client, 0x3400, 0x05);
            i2c_put_byte(client, 0x3401, 0x48);
            i2c_put_byte(client, 0x3402, 0x04);
            i2c_put_byte(client, 0x3403, 0x00);
            i2c_put_byte(client, 0x3404, 0x07);
            i2c_put_byte(client, 0x3405, 0xcf);
            i2c_put_byte(client, 0x3212, 0x13);
            i2c_put_byte(client, 0x3212, 0xa3);

        	break;
            
    	case CAM_WB_TUNGSTEN: 
            i2c_put_byte(client,0x3212, 0x03);
            i2c_put_byte(client,0x3406, 0x01);
            i2c_put_byte(client,0x3400, 0x04);
            i2c_put_byte(client,0x3401, 0x10);
            i2c_put_byte(client,0x3402, 0x04);
            i2c_put_byte(client,0x3403, 0x00);
            i2c_put_byte(client,0x3404, 0x08);
            i2c_put_byte(client,0x3405, 0x40);
            i2c_put_byte(client,0x3212, 0x13);
            i2c_put_byte(client,0x3212, 0xa3);

        	break;

      	case CAM_WB_FLUORESCENT:
              	i2c_put_byte(client,0x3406 , 0x01);
        	i2c_put_byte(client,0x3400 , 0x05);
        	i2c_put_byte(client,0x3401 , 0x48);
        	i2c_put_byte(client,0x3402 , 0x04);
        	i2c_put_byte(client,0x3403 , 0x00);
        	i2c_put_byte(client,0x3404 , 0x07);
        	i2c_put_byte(client,0x3405 , 0xcf);
        	break;

    	case CAM_WB_MANUAL:
                // TODO
        	break;
        default:
        	break;
	}
    

} /* OV5642_set_param_wb */
/*************************************************************************
* FUNCTION
*    OV5642_set_param_exposure
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
void OV5642_set_param_exposure(struct ov5642_device *dev,enum camera_exposure_e para)//曝光调节
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);


    switch (para) {
    	case EXPOSURE_N4_STEP:  //负4档  
            i2c_put_byte(client,0x3a0f , 0x18);
        	i2c_put_byte(client,0x3a10 , 0x10);
        	i2c_put_byte(client,0x3a1b , 0x18);
        	i2c_put_byte(client,0x3a1e , 0x10);
        	i2c_put_byte(client,0x3a11 , 0x30);
        	i2c_put_byte(client,0x3a1f , 0x10);
        	break;
            
    	case EXPOSURE_N3_STEP:
            i2c_put_byte(client,0x3a0f , 0x20);
        	i2c_put_byte(client,0x3a10 , 0x18);
        	i2c_put_byte(client,0x3a11 , 0x41);
        	i2c_put_byte(client,0x3a1b , 0x20);
        	i2c_put_byte(client,0x3a1e , 0x18);
        	i2c_put_byte(client,0x3a1f , 0x10);
        	break;
            
    	case EXPOSURE_N2_STEP:
            i2c_put_byte(client,0x3a0f , 0x28);
        	i2c_put_byte(client,0x3a10 , 0x20);
        	i2c_put_byte(client,0x3a11 , 0x51);
        	i2c_put_byte(client,0x3a1b , 0x28);
        	i2c_put_byte(client,0x3a1e , 0x20);
        	i2c_put_byte(client,0x3a1f , 0x10);
        	break;
            
    	case EXPOSURE_N1_STEP:
            i2c_put_byte(client,0x3a0f , 0x30);
        	i2c_put_byte(client,0x3a10 , 0x28);
        	i2c_put_byte(client,0x3a11 , 0x61);
        	i2c_put_byte(client,0x3a1b , 0x30);
        	i2c_put_byte(client,0x3a1e , 0x28);
        	i2c_put_byte(client,0x3a1f , 0x10);
        	break;
            
    	case EXPOSURE_0_STEP://默认零档
            i2c_put_byte(client,0x3a0f , 0x38);
        	i2c_put_byte(client,0x3a10 , 0x30);
        	i2c_put_byte(client,0x3a11 , 0x61);
        	i2c_put_byte(client,0x3a1b , 0x38);
        	i2c_put_byte(client,0x3a1e , 0x30);
        	i2c_put_byte(client,0x3a1f , 0x10);
        	break;
            
    	case EXPOSURE_P1_STEP://正一档
            i2c_put_byte(client,0x3a0f , 0x40);
        	i2c_put_byte(client,0x3a10 , 0x38);
        	i2c_put_byte(client,0x3a11 , 0x71);
        	i2c_put_byte(client,0x3a1b , 0x40);
        	i2c_put_byte(client,0x3a1e , 0x38);
        	i2c_put_byte(client,0x3a1f , 0x10);
        	break;
            
    	case EXPOSURE_P2_STEP:
            i2c_put_byte(client,0x3a0f , 0x48);
        	i2c_put_byte(client,0x3a10 , 0x40);
        	i2c_put_byte(client,0x3a11 , 0x80);
        	i2c_put_byte(client,0x3a1b , 0x48);
        	i2c_put_byte(client,0x3a1e , 0x40);
        	i2c_put_byte(client,0x3a1f , 0x20);
        	break;
            
    	case EXPOSURE_P3_STEP:
            i2c_put_byte(client,0x3a0f , 0x50);
        	i2c_put_byte(client,0x3a10 , 0x48);
        	i2c_put_byte(client,0x3a11 , 0x90);
        	i2c_put_byte(client,0x3a1b , 0x50);
        	i2c_put_byte(client,0x3a1e , 0x48);
        	i2c_put_byte(client,0x3a1f , 0x20);
        	break;
            
    	case EXPOSURE_P4_STEP:    
            i2c_put_byte(client,0x3a0f , 0x58);
        	i2c_put_byte(client,0x3a10 , 0x50);
        	i2c_put_byte(client,0x3a11 , 0x91);
        	i2c_put_byte(client,0x3a1b , 0x58);
        	i2c_put_byte(client,0x3a1e , 0x50);
        	i2c_put_byte(client,0x3a1f , 0x20);
        	break;
            
    	default:
            i2c_put_byte(client,0x3a0f , 0x38);
        	i2c_put_byte(client,0x3a10 , 0x30);
        	i2c_put_byte(client,0x3a11 , 0x61);
        	i2c_put_byte(client,0x3a1b , 0x38);
        	i2c_put_byte(client,0x3a1e , 0x30);
        	i2c_put_byte(client,0x3a1f , 0x10);
        	break;
    }
} /* OV5642_set_param_exposure */
/*************************************************************************
* FUNCTION
*    OV5642_set_param_effect
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
void OV5642_set_param_effect(struct ov5642_device *dev,enum camera_effect_flip_e para)//特效设置
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
  
    switch (para) {
    	case CAM_EFFECT_ENC_NORMAL://正常
        	i2c_put_byte(client,0x5001,0x03);//disable effect
        	break;        

    	case CAM_EFFECT_ENC_GRAYSCALE://灰阶
        	i2c_put_byte(client,0x5001,0x83);
        	i2c_put_byte(client,0x5580,0x20);
        	break;

    	case CAM_EFFECT_ENC_SEPIA://复古
                 /*i2c_put_byte(client,0x0115,0x0a);
        	i2c_put_byte(client,0x026e,0x60);
        	i2c_put_byte(client,0x026f,0xa0);*/
        	break;        
                
    	case CAM_EFFECT_ENC_SEPIAGREEN://复古绿
            /*i2c_put_byte(client,0x0115,0x0a);
        	i2c_put_byte(client,0x026e,0x20);
        	i2c_put_byte(client,0x026f,0x00);*/
        	break;                    

    	case CAM_EFFECT_ENC_SEPIABLUE://复古蓝
            /*i2c_put_byte(client,0x0115,0x0a);
        	i2c_put_byte(client,0x026e,0xfb);
        	i2c_put_byte(client,0x026f,0x00);*/
        	break;                                

    	case CAM_EFFECT_ENC_COLORINV://底片
        	i2c_put_byte(client,0x5001,0x83);
        	i2c_put_byte(client,0x5580,0x40);
        	break;        

    	default:
        	break;
    }


} /* OV5642_set_param_effect */

/*************************************************************************
* FUNCTION
*	OV5642_night_mode
*
* DESCRIPTION
*    This function night mode of OV5642.
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
static void OV5642_set_param_banding(struct ov5642_device *dev,enum  camera_banding_flip_e banding)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//unsigned char buf[4];
	switch(banding){
	case CAM_BANDING_60HZ:
		printk("set banding 60Hz\n");
		i2c_put_byte(client, 0x3c00, 0x00);
		break;
	case CAM_BANDING_50HZ:
		printk("set banding 50Hz\n");
		i2c_put_byte(client, 0x3c00, 0x04);
		break;
	default:
	    break;
	}
}

static int OV5642_AutoFocus(struct ov5642_device *dev, int focus_mode)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int ret = 0;
	//int i = 0;
    
	switch (focus_mode) {
	case CAM_FOCUS_MODE_AUTO:
		i2c_put_byte(client, CMD_ACK , 0x1);
		i2c_put_byte(client, CMD_MAIN , 0x3); //start to auto focus
		bDoingAutoFocusMode = true;
		#if 0
		//wait for the auto focus to be done
		while(i2c_get_byte(client, CMD_ACK) && i < 40){
			//printk("waiting for focus ready\n");
			i++;
			msleep(10);
		}
		
		if (i2c_get_byte(client, CMD_PARA4) == 0)
			ret = -1;
		else {
			msleep(10);
			i2c_put_byte(client, CMD_MAIN , 0x6); //pause the auto focus
			i2c_put_byte(client, CMD_ACK , 0x1);
		}
		#endif
		printk("auto mode start\n");
		break;
		
	case CAM_FOCUS_MODE_CONTI_VID:
	case CAM_FOCUS_MODE_CONTI_PIC:
    		i2c_put_byte(client, CMD_ACK , 0x1);
		i2c_put_byte(client, CMD_MAIN , 0x4); //start to auto focus
            
		/*while(i2c_get_byte(client, CMD_ACK) == 0x1) {
			msleep(10);
		}*/
		printk("start continous focus\n");
		break;
            
	case CAM_FOCUS_MODE_RELEASE:
	case CAM_FOCUS_MODE_FIXED:
	default:
		i2c_put_byte(client, CMD_ACK , 0x1);
		i2c_put_byte(client, CMD_MAIN , 0x8);
		printk("release focus to infinit\n");
		break;
	}
	return ret;

}    /* OV5642_AutoFocus */

static int OV5642_FlashCtrl(struct ov5642_device *dev, int flash_mode)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int ret = 0;
	//int i = 0;
    
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

}    /* OV5642_FlashCtrl */

static resolution_size_t get_size_type(int width, int height)
{
	resolution_size_t rv = SIZE_NULL;
	if (width * height >= 2500 * 1900)
		rv = SIZE_2592X1944;
	else if (width * height >= 2000 * 1500)
		rv = SIZE_2048X1536;
	else if (width * height >= 1600 * 1200)
		rv = SIZE_1600X1200;
	else if (width * height >= 600 * 400)
		rv = SIZE_640X480;
	else if (width * height >= 352 * 288)
		rv = SIZE_352X288;
	else if (width * height >= 320 * 240)
		rv = SIZE_320X240;
	else if (width * height >= 176 * 144)
		rv = SIZE_176X144;
	return rv;
}

static int set_flip(struct ov5642_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	temp = i2c_get_byte(client, 0x3818);
	printk("0x3818 src is %x\n", temp);
	temp &= ~(dev->cam_info.m_flip << 6 );
	temp |= (dev->cam_info.v_flip << 5);
	printk("0x3818 dst is %x\n", temp);
	i2c_put_byte(client, 0x3818, temp);
	temp = i2c_get_byte(client, 0x3621);
	printk("0x3621 src is %x\n", temp);
	temp |= dev->cam_info.m_flip << 5;
	i2c_put_byte(client, 0x3621, temp);
	printk("0x3621 dst is %x\n", temp);
	
	return 0;
}
		
static resolution_param_t* 
get_resolution_param(struct ov5642_device *dev,int is_capture, 
			int width, int height)
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
	}
	
	for (i = 0; i < arry_size; i++) {
		if (tmp_resolution_param[i].size_type == res_type)
			return &tmp_resolution_param[i];
	}
	return NULL;
}

static int 
set_resolution_param(struct ov5642_device *dev, resolution_param_t* res_param)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//int rc = -1;
	int i=0;
	if (!res_param->reg_script) {
		printk("error, resolution reg script is NULL\n");
		return -1;
	}
	while(1) {
		if (res_param->reg_script[i].val==0xff
				&&res_param->reg_script[i].addr==0xffff) {
			printk("setting resolutin param complete\n");
			break;
		}
		if((i2c_put_byte(client,res_param->reg_script[i].addr,
				 res_param->reg_script[i].val)) < 0) {
			printk("fail in setting resolution param. i=%d\n",i);
			break;
		}
		i++;
	}
	dev->cur_resolution_param = res_param;
	ov5642_frmintervals_active.denominator = 15;//res_param->active_fps/10;
	ov5642_frmintervals_active.numerator  = 1;
	set_flip(dev);
	return 0;
}

unsigned char v4l_2_ov5642(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int set_focus_zone(struct ov5642_device *dev, int value)
{
	int xc, yc;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int retry_count = 10;
	int ret = -1;
	
	xc = ((value >> 16) & 0xffff) * 80 / 2000;
	yc = (value & 0xffff) * 60 / 2000;
	printk("xc = %d, yc = %d\n", xc, yc);
	i2c_put_byte(client, CMD_PARA0, xc);
	i2c_put_byte(client, CMD_PARA1, yc);
	i2c_put_byte(client, CMD_ACK, 0x01);
	i2c_put_byte(client, CMD_MAIN, 0x81);
	
	do {
		msleep(5);
		pr_info("waiting for focus zone to be set\n");
	} while (i2c_get_byte(client, CMD_ACK) && retry_count--);
	
	if (retry_count)
		ret = 0;
	return ret;
}

static int ov5642_setting(struct ov5642_device *dev,int PROP_ID,int value ) 
{
	int ret=0;
	//unsigned char cur_val;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		mutex_lock(&firmware_mutex);
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_ov5642(value));
		ret=i2c_put_byte(client,0x0201,v4l_2_ov5642(value));
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
		if(ov5642_qctrl[2].default_value!=value){
			ov5642_qctrl[2].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;    
	case V4L2_CID_DO_WHITE_BALANCE:
		mutex_lock(&firmware_mutex);
		if(ov5642_qctrl[4].default_value!=value){
			ov5642_qctrl[4].default_value=value;
			OV5642_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
		}
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_EXPOSURE:
		mutex_lock(&firmware_mutex);
		if(ov5642_qctrl[5].default_value!=value){
			ov5642_qctrl[5].default_value=value;
			OV5642_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
		}
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_COLORFX:
		mutex_lock(&firmware_mutex);
		if(ov5642_qctrl[6].default_value!=value){
			ov5642_qctrl[6].default_value=value;
			OV5642_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
		}
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_WHITENESS:
		mutex_lock(&firmware_mutex);
		if(ov5642_qctrl[7].default_value!=value){
			ov5642_qctrl[7].default_value=value;
			OV5642_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_FOCUS_AUTO:
		mutex_lock(&firmware_mutex);
		if (dev->firmware_ready) 
			ret = OV5642_AutoFocus(dev,value);
		else if (value == CAM_FOCUS_MODE_CONTI_VID ||
        			value == CAM_FOCUS_MODE_CONTI_PIC)
			start_focus_mode = value;
		else
			ret = -1;
		mutex_unlock(&firmware_mutex);
		break;
	case V4L2_CID_BACKLIGHT_COMPENSATION:
		if (dev->cam_info.flash_support) 
			ret = OV5642_FlashCtrl(dev,value);
		else
			ret = -1;
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(ov5642_qctrl[10].default_value!=value){
			ov5642_qctrl[10].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(ov5642_qctrl[11].default_value!=value){
			ov5642_qctrl[11].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		printk("V4L2_CID_FOCUS_ABSOLUTE\n");
		if(ov5642_qctrl[13].default_value!=value){
			ov5642_qctrl[13].default_value=value;
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

static void power_down_ov5642(struct ov5642_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	i2c_put_byte(client,CMD_MAIN, 0x8); //release focus
	i2c_put_byte(client,0x300e, 0x18);
	i2c_put_byte(client,0x3008, 0x42);//in soft power down mode
}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void ov5642_fillbuff(struct ov5642_fh *fh, struct ov5642_buffer *buf)
{
	struct ov5642_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);    
	if (!vbuf)
    	return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = ov5642_qctrl[2].default_value&3;
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = ov5642_qctrl[10].default_value;
	para.angle = ov5642_qctrl[11].default_value;
	para.vaddr = (unsigned)vbuf;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void ov5642_thread_tick(struct ov5642_fh *fh)
{
	struct ov5642_buffer *buf;
	struct ov5642_device *dev = fh->dev;
	struct ov5642_dmaqueue *dma_q = &dev->vidq;

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
             struct ov5642_buffer, vb.queue);
    dprintk(dev, 1, "%s\n", __func__);
	dprintk(dev, 1, "list entry get buf is %x\n", (unsigned)buf);

    /* Nobody is waiting on this buffer, return */
    /*
	if (!waitqueue_active(&buf->vb.done))
    	goto unlock;
*/

       if( ! (fh->f_flags & O_NONBLOCK) ){
        /* Nobody is waiting on this buffer, return */
               if (!waitqueue_active(&buf->vb.done))
                       goto unlock;
       }
       buf->vb.state = VIDEOBUF_ACTIVE;

	list_del(&buf->vb.queue);

	do_gettimeofday(&buf->vb.ts);

    /* Fill buffer */
	spin_unlock_irqrestore(&dev->slock, flags);
	ov5642_fillbuff(fh, buf);
	dprintk(dev, 1, "filled buffer %p\n", buf);

	wake_up(&buf->vb.done);
	dprintk(dev, 2, "[%p/%d] wakeup\n", buf, buf->vb. i);
	return;
unlock:
	spin_unlock_irqrestore(&dev->slock, flags);
	return;
}

static void ov5642_sleep(struct ov5642_fh *fh)
{
	struct ov5642_device *dev = fh->dev;
	struct ov5642_dmaqueue *dma_q = &dev->vidq;

	int timeout;
	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
        (unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
    	goto stop_task;

    /* Calculate time to wake up */
	timeout = msecs_to_jiffies(2);

	ov5642_thread_tick(fh);

	schedule_timeout_interruptible(timeout);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int ov5642_thread(void *data)
{
	struct ov5642_fh  *fh = data;
	struct ov5642_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
    	ov5642_sleep(fh);

    	if (kthread_should_stop())
        	break;
    }
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int ov5642_start_thread(struct ov5642_fh *fh)
{
	struct ov5642_device *dev = fh->dev;
	struct ov5642_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(ov5642_thread, fh, "ov5642");

	if (IS_ERR(dma_q->kthread)) {
    	v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
    	return PTR_ERR(dma_q->kthread);
    }
    /* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void ov5642_stop_thread(struct ov5642_dmaqueue  *dma_q)
{
	struct ov5642_device *dev = 
			container_of(dma_q, struct ov5642_device, vidq);

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
	struct ov5642_fh  *fh = vq->priv_data;
	struct ov5642_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct ov5642_buffer *buf)
{
	struct ov5642_fh  *fh = vq->priv_data;
	struct ov5642_device *dev  = fh->dev;

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
	struct ov5642_fh     *fh  = vq->priv_data;
	struct ov5642_device    *dev = fh->dev;
	struct ov5642_buffer *buf = container_of(vb, struct ov5642_buffer, vb);
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
	struct ov5642_buffer    *buf  = container_of(vb, struct ov5642_buffer, vb);
	struct ov5642_fh        *fh   = vq->priv_data;
	struct ov5642_device       *dev  = fh->dev;
	struct ov5642_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
               struct videobuf_buffer *vb)
{
	struct ov5642_buffer   *buf  = container_of(vb, struct ov5642_buffer, vb);
	struct ov5642_fh       *fh   = vq->priv_data;
	struct ov5642_device      *dev  = (struct ov5642_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops ov5642_video_qops = {
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
	struct ov5642_fh  *fh  = priv;
	struct ov5642_device *dev = fh->dev;

	strcpy(cap->driver, "ov5642");
	strcpy(cap->card, "ov5642");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = OV5642_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
            	V4L2_CAP_STREAMING     |
            	V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
                	struct v4l2_fmtdesc *f)
{
	struct ov5642_fmt *fmt;

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
	struct ov5642_fh *fh = priv;

	printk("vidioc_g_fmt_vid_cap...fh->width =%d,fh->height=%d\n",
		fh->width,fh->height);
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
	struct ov5642_fh  *fh  = priv;
	struct ov5642_device *dev = fh->dev;
	struct ov5642_fmt *fmt;
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
	struct ov5642_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct ov5642_device *dev = fh->dev;
	resolution_param_t* res_param = NULL;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//unsigned char gain = 0, exposurelow = 0, exposuremid = 0, exposurehigh = 0;

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
    	get_preview_exposure_gain(dev);
    	set_resolution_param(dev, res_param);
    	//set_exposure_param_500m(dev, gain, exposurelow, exposuremid, exposurehigh);
    	cal_exposure(dev);
    }
    else {
        res_param = get_resolution_param(dev, 0, fh->width,fh->height);
        if (!res_param) {
    		printk("error, resolution param not get\n");
    		goto out;
   	}
   	rewrite_preview_gain_exposure(dev);
   	set_resolution_param(dev, res_param);
   	rewrite_preview_gain_exposure(dev);
   	
   	/* relaunch the focus zone to the center */
   	mutex_lock(&firmware_mutex);
   	if (dev->firmware_ready) {
   		i2c_put_byte(client, CMD_PARA0, 40);
   		i2c_put_byte(client, CMD_PARA1, 30);
   		i2c_put_byte(client, CMD_ACK, 0x01);
   		i2c_put_byte(client, CMD_MAIN, 0x81);
   	}
   	mutex_unlock(&firmware_mutex);
   	msleep(100);
   	printk("preview delay\n");
    }
    
#endif
    ret = 0;
out:
    mutex_unlock(&q->vb_lock);

    return ret;
}
/*
 * Implement G/S_PARM.  There is a "high quality" mode we could try
 * to do someday; for now, we just do the frame rate tweak.
 * V4L2_CAP_TIMEPERFRAME need to be supported furthermore.
 */
static int vidioc_g_parm(struct file *file, void *priv,
                	struct v4l2_streamparm *parms)
{
	struct ov5642_fh *fh = priv;	
	struct ov5642_device *dev = fh->dev;
	struct v4l2_captureparm *cp = &parms->parm.capture;
	//int ret;
	//int i;

	dprintk(dev,3,"vidioc_g_parm\n");
	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	
	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	
	cp->timeperframe = ov5642_frmintervals_active;
	printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
						cp->timeperframe.numerator );
	return 0;
}
static int vidioc_enum_frameintervals(struct file *file, void *priv,
				   struct v4l2_frmivalenum *fival)
{
	//struct ov5642_fmt *fmt;
	unsigned int k;

	if(fival->index > ARRAY_SIZE(ov5642_frmivalenum))
		return -EINVAL;

	for(k =0; k< ARRAY_SIZE(ov5642_frmivalenum); k++)
	{
		if( (fival->index==ov5642_frmivalenum[k].index)&&
			(fival->pixel_format ==ov5642_frmivalenum[k].pixel_format )&&
		(fival->width==ov5642_frmivalenum[k].width)&&
		(fival->height==ov5642_frmivalenum[k].height)){
			memcpy( fival, &ov5642_frmivalenum[k], sizeof(struct v4l2_frmivalenum));		
			return 0;
		}
}

	return -EINVAL;
	
}

static int vidioc_reqbufs(struct file *file, void *priv,
              struct v4l2_requestbuffers *p)
{
	struct ov5642_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov5642_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov5642_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov5642_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
            	file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct ov5642_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct ov5642_fh  *fh = priv;
	vdin_parm_t para;
	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    	return -EINVAL;
	if (i != fh->type)
    	return -EINVAL;

        memset( &para, 0, sizeof( para ));
        para.port  = TVIN_PORT_CAMERA;
        para.fmt = TVIN_SIG_FMT_MAX;//TVIN_SIG_FMT_MAX+1;;TVIN_SIG_FMT_CAMERA_1280X720P_30Hz
        if (fh->dev->cur_resolution_param) {
                para.frame_rate = fh->dev->cur_resolution_param->active_fps;//175;
                para.h_active = fh->dev->cur_resolution_param->active_frmsize.width;
                para.v_active = fh->dev->cur_resolution_param->active_frmsize.height;
                para.hs_bp = 0;
                para.vs_bp = 2;
                para.cfmt = TVIN_YUV422;
                para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
        } else {
                para.frame_rate = 15;
                para.h_active = 640;
                para.v_active = 478;
                para.hs_bp = 0;
                para.vs_bp = 2;
                para.cfmt = TVIN_YUV422;
                para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
        }

        printk("ov5642: h_active = %d; v_active = %d, frame_rate=%d\n",
                        para.h_active, para.v_active, para.frame_rate);
        para.hsync_phase = 1;
        para.vsync_phase  = 1;
        para.skip_count =  2;
        ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
		vops->start_tvin_service(0,&para);
		fh->stream_on = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct ov5642_fh  *fh = priv;

	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    	return -EINVAL;
	if (i != fh->type)
    	return -EINVAL;
	ret = videobuf_streamoff(&fh->vb_vidq);
	if(ret == 0 ){
		vops->stop_tvin_service(0);
		fh->stream_on = 0;
	}
	return ret;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,struct v4l2_frmsizeenum *fsize)
{
	int ret = 0,i=0;
	struct ov5642_fmt *fmt = NULL;
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
		printk("ov5642_prev_resolution[fsize->index]   before fsize->index== %d\n",fsize->index);//potti
		if (fsize->index >= ARRAY_SIZE(prev_resolution_array))
			return -EINVAL;
		frmsize = &prev_resolution_array[fsize->index].frmsize;
		printk("ov5642_prev_resolution[fsize->index]   after fsize->index== %d\n",fsize->index);
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	} else if (fmt->fourcc == V4L2_PIX_FMT_RGB24) {
		printk("ov5642_pic_resolution[fsize->index]   before fsize->index== %d\n",fsize->index);
		if (fsize->index >= ARRAY_SIZE(capture_resolution_array))
			return -EINVAL;
		frmsize = &capture_resolution_array[fsize->index].frmsize;
		printk("ov5642_pic_resolution[fsize->index]   after fsize->index== %d\n",fsize->index);    
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
	struct ov5642_fh *fh = priv;
	struct ov5642_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct ov5642_fh *fh = priv;
	struct ov5642_device *dev = fh->dev;

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
	struct ov5642_fh *fh = priv;
	struct ov5642_device *dev = fh->dev;
	
	if (!dev->cam_info.flash_support 
			&& qc->id == V4L2_CID_BACKLIGHT_COMPENSATION)
			return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ov5642_qctrl); i++)
    	if (qc->id && qc->id == ov5642_qctrl[i].id) {
            memcpy(qc, &(ov5642_qctrl[i]),
            	sizeof(*qc));
            if (ov5642_qctrl[i].type == V4L2_CTRL_TYPE_MENU)
                return ov5642_qctrl[i].maximum+1;
            else
        	return (0);
        }

	return -EINVAL;
}

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(ov5642_qmenu_set); i++)
    	if (a->id && a->id == ov5642_qmenu_set[i].id) {
    	    for(j = 0; j < ov5642_qmenu_set[i].num; j++)
    	        if (a->index == ov5642_qmenu_set[i].ov5642_qmenu[j].index) {
        	        memcpy(a, &( ov5642_qmenu_set[i].ov5642_qmenu[j]),
            	        sizeof(*a));
        	        return (0);
        	    }
        }

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
             struct v4l2_control *ctrl)
{
	struct ov5642_fh *fh = priv;
	struct ov5642_device *dev = fh->dev;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int i;
	int i2cret = -1;

	for (i = 0; i < ARRAY_SIZE(ov5642_qctrl); i++)
		if (ctrl->id == ov5642_qctrl[i].id) {
			if( (V4L2_CID_FOCUS_AUTO == ctrl->id)
					&& bDoingAutoFocusMode){
				if(i2c_get_byte(client, CMD_ACK)){
					return -EBUSY;
		    		}else{
					bDoingAutoFocusMode = false;
					if(i2c_get_byte(client, CMD_PARA4) == 0){
						printk("auto mode failed!\n");
						return -EAGAIN;
					}else {
						/* pause the auto focus */
						i2c_put_byte(client, CMD_ACK , 0x1);
						i2c_put_byte(client, CMD_MAIN , 0x6); 
						printk("pause auto focus\n");
					}
				}
			}else if( V4L2_CID_AUTO_FOCUS_STATUS == ctrl->id){

				i2cret = i2c_get_byte(client, FW_STATUS);
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

static int 
vidioc_s_ctrl(struct file *file, void *priv, struct v4l2_control *ctrl)
{
	struct ov5642_fh *fh = priv;
	struct ov5642_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(ov5642_qctrl); i++)
    	if (ctrl->id == ov5642_qctrl[i].id) {
		if (ctrl->value < ov5642_qctrl[i].minimum ||
				ctrl->value > ov5642_qctrl[i].maximum ||
				ov5642_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int ov5642_open(struct file *file)
{
	struct ov5642_device *dev = video_drvdata(file);
	struct ov5642_fh *fh = NULL;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int retval = 0;
	//int reg_val;
	//int i = 0;
#if CONFIG_CMA
    retval = vm_init_buf(24*SZ_1M);
    if(retval <0)
        return -1;
#endif
	ov5642_have_opened=1;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif	
	aml_cam_init(&dev->cam_info);
	OV5642_init_regs(dev);
	
	msleep(10);
	
	schedule_work(&(dev->dl_work));
	//do_download(&(dev->dl_work));

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
            
	videobuf_queue_vmalloc_init(&fh->vb_vidq, &ov5642_video_qops,
        	NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
        	sizeof(struct ov5642_buffer), fh, NULL);
	bDoingAutoFocusMode=false;
	ov5642_start_thread(fh);

	return 0;
}

static ssize_t
ov5642_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct ov5642_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
ov5642_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ov5642_fh        *fh = file->private_data;
	struct ov5642_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int ov5642_close(struct file *file)
{
	struct ov5642_fh         *fh = file->private_data;
	struct ov5642_device *dev       = fh->dev;
	struct ov5642_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);
	mutex_lock(&firmware_mutex);
	dev->firmware_ready = 0;
	mutex_unlock(&firmware_mutex);
	ov5642_have_opened=0;
	ov5642_stop_thread(vidq);
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
	ov5642_qctrl[4].default_value=0;
	ov5642_qctrl[5].default_value=4;
	ov5642_qctrl[6].default_value=0;
	
	ov5642_qctrl[2].default_value=0;
	ov5642_qctrl[10].default_value=100;
	ov5642_qctrl[11].default_value=0;
	ov5642_frmintervals_active.denominator=15;
	ov5642_frmintervals_active.numerator=1;
	power_down_ov5642(dev);
#endif
	msleep(2);

	aml_cam_uninit(&dev->cam_info);
	aml_cam_flash(&dev->cam_info, 0);
	msleep(2); 
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 0);
#endif	
	wake_unlock(&(dev->wake_lock));
#ifdef CONFIG_CMA
    vm_deinit_buf();
#endif
	return 0;
}

static int ov5642_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ov5642_fh  *fh = file->private_data;
	struct ov5642_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
        (unsigned long)vma->vm_start,
        (unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
    	ret);

	return ret;
}

static const struct v4l2_file_operations ov5642_fops = {
	.owner	    = THIS_MODULE,
	.open       = ov5642_open,
	.release    = ov5642_close,
	.read       = ov5642_read,
	.poll	    = ov5642_poll,
	.ioctl      = video_ioctl2, /* V4L2 ioctl handler */
	.mmap       = ov5642_mmap,
};

static const struct v4l2_ioctl_ops ov5642_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_g_parm 		  = vidioc_g_parm,
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
	.vidioc_enum_frameintervals = vidioc_enum_frameintervals,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
};

static struct video_device ov5642_template = {
	.name	        = "ov5642_v4l",
	.fops           = &ov5642_fops,
	.ioctl_ops      = &ov5642_ioctl_ops,
	.release	    = video_device_release,
	
	.tvnorms        = V4L2_STD_525_60,
	.current_norm   = V4L2_STD_NTSC_M,
};

static int ov5642_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_OV5642, 0);
}

static const struct v4l2_subdev_core_ops ov5642_core_ops = {
	.g_chip_ident = ov5642_g_chip_ident,
};

static const struct v4l2_subdev_ops ov5642_ops = {
	.core = &ov5642_core_ops,
};

static int ov5642_probe(struct i2c_client *client,
        	const struct i2c_device_id *id)
{
	int err;
	struct ov5642_device *t;
	struct v4l2_subdev *sd;
	aml_cam_info_t* plat_dat = NULL;
	v4l_info(client, "chip found @ 0x%x (%s)\n",
        	client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &ov5642_ops);
	mutex_init(&t->mutex);

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &ov5642_template, sizeof(*t->vdev));
	
	video_set_drvdata(t->vdev, t);
	
	INIT_WORK(&(t->dl_work), do_download);
	
	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "ov5642");
	/* Register it */
	plat_dat = (aml_cam_info_t*)client->dev.platform_data;
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if (plat_dat->front_back >=0)  
			video_nr = plat_dat->front_back;
	} else {
		printk("camera ov5642: have no platform data\n");
		kfree(t);
		kfree(client);
		return -1;
	}
	
	t->cam_info.version = OV5642_DRIVER_VERSION;
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

static int ov5642_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5642_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id ov5642_id[] = {
	{"ov5642", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov5642_id);

static struct i2c_driver ov5642_i2c_driver = {
	.driver = {
		.name = "ov5642",
	},
	.probe = ov5642_probe,
	.remove = ov5642_remove,
	.id_table = ov5642_id,
};

module_i2c_driver(ov5642_i2c_driver);

