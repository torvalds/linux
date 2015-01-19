/*
 *NT99250 - This code emulates a real video device with v4l2 api
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
#include <linux/amlogic/camera/aml_cam_info.h>
#include <linux/amlogic/vmapi.h>

#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <mach/gpio.h>
#include "common/plat_ctrl.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif

#define NT99250_CAMERA_MODULE_NAME "nt99250"		// NTK 2012-05-08

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define NT99250_CAMERA_MAJOR_VERSION 0
#define NT99250_CAMERA_MINOR_VERSION 7
#define NT99250_CAMERA_RELEASE 0
#define NT99250_CAMERA_VERSION \
	KERNEL_VERSION(NT99250_CAMERA_MAJOR_VERSION, NT99250_CAMERA_MINOR_VERSION, NT99250_CAMERA_RELEASE)

MODULE_DESCRIPTION("nt99250 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define NT99250_DRIVER_VERSION "NT99250-COMMON-01-140717"

#define NT99250_TLINE_LENGTH_2500	//

#define AE_TARGET_MEAN	0x35	//0x32	//0x38
//AE_STATISTICS_MODE
#define AE_STATISTICS_MODE_CENTER
 
static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

int camera_wb_state = 0;

static struct vdin_v4l2_ops_s *vops;

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static int vidio_set_fmt_ticks=0;

//extern int disable_nt99250;

static int nt99250_h_active=800;
static int nt99250_v_active=600;

static struct v4l2_fract nt99250_frmintervals_active = {
	.numerator = 1,
	.denominator = 15,
};

static struct i2c_client *this_client;

/* supported controls */
static struct v4l2_queryctrl nt99250_qctrl[] = {
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

static struct v4l2_frmivalenum nt99250_frmivalenum[]={
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

struct nt99250_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct nt99250_fmt formats[] = {
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

static struct nt99250_fmt *get_format(struct v4l2_format *f)
{
	struct nt99250_fmt *fmt;
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
struct nt99250_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct nt99250_fmt        *fmt;
};

struct nt99250_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(nt99250_devicelist);

struct nt99250_device {
	struct list_head			nt99250_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct nt99250_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(nt99250_qctrl)];
};

static inline struct nt99250_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct nt99250_device, sd);
}

struct nt99250_fh {
	struct nt99250_device            *dev;

	/* video capture */
	struct nt99250_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int f_flags;
};

/*static inline struct nt99250_fh *to_fh(struct nt99250_device *dev)
{
	return container_of(dev, struct nt99250_fh, dev);
}*/

static struct v4l2_frmsize_discrete nt99250_prev_resolution[]=
{
	{320,240},
	{352,288},
	{640,480}, 
};

static struct v4l2_frmsize_discrete nt99250_pic_resolution[]=
{	
	{1600,1200},
	{800,600},
};

/* ------------------------------------------------------------------
	reg spec of NT99250---2012-0420
   ------------------------------------------------------------------*/

struct aml_camera_i2c_fig_s NT99250_script[] = {
    // 800x600 Output YUV  
	//nt99250 init
#if 0
    {0x32F0, 0x03},  //swap CbCr, Chroma & Y
    {0x301e, 0x54},
    {0x301f, 0x48},
    //{0x3104, 0x00}, //Disable LDO
		/*
    //gama winson
    {0x3270, 0x00},
    {0x3271, 0x08},
    {0x3272, 0x10},
    {0x3273, 0x20},
    {0x3274, 0x32},
    {0x3275, 0x44},
    {0x3276, 0x62},
    {0x3277, 0x7a},
    {0x3278, 0x8d},
    {0x3279, 0x9d},
    {0x327A, 0xba},
    {0x327B, 0xd1},
    {0x327C, 0xe6},
    {0x327D, 0xf4},
    {0x327E, 0xFa},
		*/
		//[Gamma_HDR2]                                                   
    {0x3270, 0x00}, 
    {0x3271, 0x0A}, 
    {0x3272, 0x16}, 
    {0x3273, 0x30}, 
    {0x3274, 0x3F}, 
    {0x3275, 0x50}, 
    {0x3276, 0x6E}, 
    {0x3277, 0x88}, 
    {0x3278, 0xA0}, 
    {0x3279, 0xB3}, 
    {0x327A, 0xD2}, 
    {0x327B, 0xE8}, 
    {0x327C, 0xF5}, 
    {0x327D, 0xFF}, 
    {0x327E, 0xFF}, 
    /*
		//[Gamma_HDR]
    {0x3270, 0x00},
    {0x3271, 0x04},
    {0x3272, 0x0E},
    {0x3273, 0x28},
    {0x3274, 0x3F},
    {0x3275, 0x50},
    {0x3276, 0x6E},
    {0x3277, 0x88},
    {0x3278, 0xA0},
    {0x3279, 0xB3},
    {0x327A, 0xD2},
    {0x327B, 0xE8},
    {0x327C, 0xF5},
    {0x327D, 0xFF},
    {0x327E, 0xFF},
		*/

    {0x3290, 0x01},
    {0x3291, 0x40},
    {0x3296, 0x01},
    {0x3297, 0x70},
		/*
    //  cc
    {0x3302, 0x00},
    {0x3303, 0x94},
    {0x3304, 0x00},
    {0x3305, 0x7c},
    {0x3306, 0x07},
    {0x3307, 0xf0},
    {0x3308, 0x07},
    {0x3309, 0xc0},
    {0x330A, 0x06},
    {0x330B, 0xd6},
    {0x330C, 0x01},
    {0x330D, 0x6b},
    {0x330E, 0x01},
    {0x330F, 0x7f},
    {0x3310, 0x06},
    {0x3311, 0x96},
    {0x3312, 0x07},
    {0x3313, 0xeb},
		*/
		//[CC_MS] ;750
    {0x3302, 0x00},
    {0x3303, 0x25},
    {0x3304, 0x00},
    {0x3305, 0xB7},
    {0x3306, 0x00},
    {0x3307, 0x23},
    {0x3308, 0x07},
    {0x3309, 0xFC},
    {0x330A, 0x06},
    {0x330B, 0xB5},
    {0x330C, 0x01},
    {0x330D, 0x4F},
    {0x330E, 0x01},
    {0x330F, 0x2A},
    {0x3310, 0x06},
    {0x3311, 0xE7},
    {0x3312, 0x07},
    {0x3313, 0xEF},
		/*
		//[CC_MS+2] ;750
    {0x3302, 0x00},
    {0x3303, 0x1F},
    {0x3304, 0x00},
    {0x3305, 0xBF},
    {0x3306, 0x00},
    {0x3307, 0x21},
    {0x3308, 0x00},
    {0x3309, 0x01},
    {0x330A, 0x06},
    {0x330B, 0x92},
    {0x330C, 0x01},
    {0x330D, 0x6D},
    {0x330E, 0x01},
    {0x330F, 0x53},
    {0x3310, 0x06},
    {0x3311, 0xBB},
    {0x3312, 0x07},
    {0x3313, 0xF2},
		*/
    {0x3102, 0x0b},
    {0x3103, 0x46},
    {0x3105, 0x33},
    {0x3107, 0x32},
    {0x310A, 0x03},
    {0x310B, 0x18},
    {0x310f, 0x08},
    {0x3110, 0x03},
    {0x3113, 0x0F},
    {0x3119, 0x17},
    {0x3114, 0x03},
    {0x3117, 0x03},
    {0x3118, 0x01},
    {0x3380, 0x03},
    {0x3044, 0x02},
    {0x3045, 0xd0},
    {0x3046, 0x02},
    {0x3047, 0xd0},
    {0x3048, 0x02},
    {0x3049, 0xd0},
    {0x304a, 0x02},
    {0x304b, 0xd0},
    {0x303e, 0x02},
    {0x303f, 0x2b},
    {0x3052, 0x80},
    {0x3053, 0x00},
    {0x3059, 0x10},
    {0x305a, 0x28},
    {0x305b, 0x20},
    {0x305c, 0x04},
    {0x305d, 0x28},
    {0x305e, 0x04},
    {0x305f, 0x52},
    {0x3058, 0x01},
    {0x3080, 0x80},
    {0x3081, 0x80},
    {0x3082, 0x80},
    {0x3083, 0x40},
    {0x3084, 0x80},
    {0x3085, 0x40},
    {0x32b0, 0x02},		//AE_CENTER
    {0x32b1, 0x98},
    //{0x32b0, 0x00},	//AE_AVERAGE
    //{0x32b1, 0xd0},
    {0x32B2, 0x00},
    {0x32B3, 0xc8},
    {0x32B4, 0x00},
    {0x32B5, 0x97}, 
    {0x32B6, 0x02}, 
    {0x32B7, 0x58}, 
    {0x32B8, 0x01}, 
    {0x32B9, 0xc3}, 
    {0x32BB, 0x1b},
    {0x32bd, 0x05},
    {0x32be, 0x05},
    {0x32cd, 0x01},
    {0x32d3, 0x13},
    {0x32d7, 0x82},
    {0x32f6, 0x0c},

    {0x3012, 0x03}, 
    {0x3013, 0x00},   
    {0x301d, 0x08},
    //edge table
    {0x3326, 0x04}, //0x03
    {0x3327, 0x0a}, 
    {0x3328, 0x0a}, 
    {0x3329, 0x06}, 
    {0x332A, 0x06}, 
    {0x332B, 0x1c}, 
    {0x332C, 0x1c}, 
    {0x332D, 0x00}, 
    {0x332E, 0x1d}, 
    {0x332F, 0x1f},

    {0x329b, 0x01}, 
    {0x32A1, 0x01}, 
    {0x32A2, 0x20}, 
    {0x32A3, 0x01}, 
    {0x32A4, 0xa0}, 
    {0x32A5, 0x01}, 
    {0x32A6, 0x40}, 
    {0x32A7, 0x01}, 
    {0x32A8, 0xe0}, 
    {0x3321, 0x0A}, 
    {0x3322, 0x0A},  
    {0x306D, 0x01}, 
    {0x32d8, 0x3f},
    {0x32d9, 0x18},

    {0x3320, 0x20}, 
    {0x3338, 0x02},
    {0x3339, 0x18},
    {0x333A, 0x28}, 
    {0x3324, 0x07},
    {0x3331, 0x06},
    {0x3332, 0x40},
    {0x3300, 0x30}, 
    {0x3301, 0x80}, 

    {0x32C6, 0x16},
    {0x32C7, 0x62},
    {0x32C0, 0x22},
    {0x32CD, 0x02},
    {0x3320, 0x20},
    {0x32bf, 0x49},
    {0x3200, 0x3e},
    {0x3201, 0x3f},
    {0x3077, 0x0f},
    {0x32c5, 0x18},
    {0x32c1, 0x24},
    {0x32c2, 0x80},
    {0x32bc, AE_TARGET_MEAN},  //0x38
    {0x3262, 0x02},
    {0x3263, 0x02},
    {0x3264, 0x02},

    {0x32a9, 0x00}, 	//0x21  //0x10
    {0x32aa, 0x00},
	//{0x3025, 0x02},	//Test pattern : Color Bar

    {0x32f1, 0x00},
    {0x32f3, 0x80},
    {0x32f2, 0x80},
    {0x32f4, 0x80},
    {0x32f5, 0x80},
    //Chroma
    {0x32f6, 0x0d},
    {0x32f9, 0x51},
    {0x32fA, 0x33},		

    {0x3069, 0x01}, //Pix   //01 :for M1002     00 :for other
    {0x306d, 0x01}, //pclk   //00 :for M1002     01 :for other
 
    //==============================
    //Output  size  : 800*600
    //==============================
	{0x32d8, 0x3f},
	{0x32d9, 0x1a},
	{0x3320, 0x20}, 
	//{0x3338, 0x00},	//02
	{0x3331, 0x04},
	{0x3332, 0x80},
	{0x3300, 0x70},	//0x70 
	{0x3301, 0x80}, 
	{0x3077, 0x0f},
	{0x3324, 0x07},
	{0x3339, 0x10},
	{0x333a, 0x28},
	{0x3200, 0x3e},
	//{0x32c5,0x18},

#if 0	//def NT99250_TLINE_LENGTH_2500
	//SVGA pclk=58M
	//[1440x1080_10_Fps]
    {0x32e0, 0x03}, 
    {0x32e1, 0x20}, 
    {0x32e2, 0x02}, 
    {0x32e3, 0x5a}, 
    {0x32e4, 0x00}, 
    {0x32e5, 0xcd}, 
    {0x32e6, 0x00}, 
    {0x32e7, 0xcc}, 
    {0x301e, 0x00}, 
    {0x301f, 0x21}, //0x27
    {0x3022, 0x24}, 
    {0x3023, 0x24}, 
    {0x3002, 0x00}, 
    {0x3003, 0x54}, 
    {0x3004, 0x00}, 
    {0x3005, 0x40}, 
    {0x3006, 0x05}, 
    {0x3007, 0xf3}, 
    {0x3008, 0x04}, 
    {0x3009, 0x77}, 
    {0x300a, 0x09}, 
    {0x300b, 0xc4},
	//{0x300c, 0x05},	//8.33fps 
	//{0x300d, 0xa0},
	//{0x300d, 0x35},	//9fps
	{0x300c, 0x04}, 
	{0x300d, 0xb0},	//10fps  
    {0x300e, 0x05}, 
    {0x300f, 0xa0}, 
    {0x3010, 0x04}, 
    {0x3011, 0x38}, 
    {0x32bb, 0x0b},
	//{0x32c1, 0x25}, //8.33fps
	//{0x32c2, 0xa0}, 
	//{0x32c2, 0x35},  //9fps  
    {0x32c1, 0x24},  //10fps
    {0x32c2, 0xb0}, 
    {0x32c8, 0x78}, 
    {0x32c9, 0x64}, 
    {0x32c4, 0x00}, 
    {0x3201, 0x7f}, 
#else
	//SVGA pclk=56M
	//[1440x1080_10_Fps]
	{0x32e0, 0x03}, 
	{0x32e1, 0x20}, 
	{0x32e2, 0x02}, 
	{0x32e3, 0x5a}, 
	{0x32e4, 0x00}, 
	{0x32e5, 0xcd}, 
	{0x32e6, 0x00}, 
	{0x32e7, 0xcc}, 
	{0x301e, 0x00}, 
	{0x301f, 0x27}, 
	{0x3022, 0x24}, 
	{0x3023, 0x24}, 
	{0x3002, 0x00}, 
	{0x3003, 0x54}, 
	{0x3004, 0x00}, 
	{0x3005, 0x40}, 
	{0x3006, 0x05}, 
	{0x3007, 0xf3}, 
	{0x3008, 0x04}, 
	{0x3009, 0x77}, 
	{0x300a, 0x09}, 
	{0x300b, 0xc4},
	//0x300c, 0x05},	//8.33fps 
	//0x300d, 0x40},
	{0x300c, 0x04}, 
	//{0x300d, 0xd0},	//9fps
	{0x300d, 0x60},	//10fps 
	{0x300e, 0x05}, 
	{0x300f, 0xa0}, 
	{0x3010, 0x04}, 
	{0x3011, 0x38}, 
	{0x32bb, 0x0b},
	//{0x32c1, 0x25}, //8.33fps
	//{0x32c2, 0x40}, 
	//{0x32c1, 0x24},  //9fps 
	//{0x32c2, 0xd0}, 
	{0x32c1, 0x24},  //10fps 
	{0x32c2, 0x60},  
	{0x32c8, 0x70}, 
	{0x32c9, 0x5d}, 
	{0x32c4, 0x00}, 
	{0x3201, 0x7f},
#endif
  	//AE_STATISTICS_MODE
  	#ifdef AE_STATISTICS_MODE_CENTER
    {0x32b0, 0x02},		//Center_Weighting
    {0x32b1, 0xd0},
    #else
    {0x32b0, 0x00},		//Average
    {0x32b1, 0xd0},
    #endif
    {0x32B2, 0x00},
    {0x32B3, 0xc8},
    {0x32B4, 0x00},
    {0x32B5, 0x97}, 
    {0x32B6, 0x02}, 
    {0x32B7, 0x58}, 
    {0x32B8, 0x01}, 
    {0x32B9, 0xc3}, 
 
  	//{0x3025, 0x02},	//test pattern : Color bars
    {0x3021, 0x06},
    {0x3060, 0x01},
#else
	//Init 	// 2012-05-08
	{0x32F0, 0x03},
	{0x3024, 0x00},	//hsync, vsync
	{0x301e, 0x54},
	{0x301f, 0x48},
	//gama winson
	/*
	{0x3270, 0x00},
	{0x3271, 0x08},
	{0x3272, 0x10},
	{0x3273, 0x20},
	{0x3274, 0x32},
	{0x3275, 0x44},
	{0x3276, 0x62},
	{0x3277, 0x7a},
	{0x3278, 0x8d},
	{0x3279, 0x9d},
	{0x327A, 0xba},
	{0x327B, 0xd1},
	{0x327C, 0xe6},
	{0x327D, 0xf4},
	{0x327E, 0xFa},*/
	// Gmma10
	{0x3270,0x0B}, 
	{0x3271,0x10},
	{0x3272,0x16},
	{0x3273,0x29},
	{0x3274,0x3C},
	{0x3275,0x4F},
	{0x3276,0x6F},
	{0x3277,0x8A},
	{0x3278,0x9F},
	{0x3279,0xB4},
	{0x327A,0xD3},
	{0x327B,0xE5},
	{0x327C,0xF1},
	{0x327D,0xFA},
	{0x327E,0xFF},

	{0x3290, 0x01},
	{0x3291, 0x40},
	{0x3296, 0x01},
	{0x3297, 0x70},
	/*
	//  cc
	{0x3302, 0x00},
	{0x3303, 0x94},
	{0x3304, 0x00},
	{0x3305, 0x7c},
	{0x3306, 0x07},
	{0x3307, 0xf0},
	{0x3308, 0x07},
	{0x3309, 0xc0},
	{0x330A, 0x06},
	{0x330B, 0xd6},
	{0x330C, 0x01},
	{0x330D, 0x6b},
	{0x330E, 0x01},
	{0x330F, 0x7f},
	{0x3310, 0x06},
	{0x3311, 0x96},
	{0x3312, 0x07},
	{0x3313, 0xeb},*/

	// CC_MS
	{0x3302, 0x00},
	{0x3303, 0x25},
	{0x3304, 0x00},
	{0x3305, 0xB7},
	{0x3306, 0x00},
	{0x3307, 0x23},
	{0x3308, 0x07},
	{0x3309, 0xFC},
	{0x330A, 0x06},
	{0x330B, 0xB5},
	{0x330C, 0x01},
	{0x330D, 0x4F},
	{0x330E, 0x01},
	{0x330F, 0x2A},
	{0x3310, 0x06},
	{0x3311, 0xE7},
	{0x3312, 0x07},
	{0x3313, 0xEF},
	{0x0000, 0x00},

	{0x3102, 0x0b},
	{0x3103, 0x46},
	{0x3105, 0x33},
	{0x3107, 0x32},
	{0x310A, 0x03},
	{0x310B, 0x18},
	{0x310f, 0x08},
	{0x3110, 0x03},
	{0x3113, 0x0F},
	{0x3119, 0x17},
	{0x3114, 0x03},
	{0x3117, 0x03},
	{0x3118, 0x01},
	{0x3380, 0x03},
	{0x3044, 0x02},
	{0x3045, 0xd0},
	{0x3046, 0x02},
	{0x3047, 0xd0},
	{0x3048, 0x02},
	{0x3049, 0xd0},
	{0x304a, 0x02},
	{0x304b, 0xd0},
	{0x303e, 0x02},
	{0x303f, 0x2b},
	{0x3052, 0x80},
	{0x3053, 0x00},
	{0x3059, 0x10},
	{0x305a, 0x28},
	{0x305b, 0x20},
	{0x305c, 0x04},
	{0x305d, 0x28},
	{0x305e, 0x04},
	{0x305f, 0x52},
	{0x3058, 0x01},
	{0x3080, 0x80},
	{0x3081, 0x80},
	{0x3082, 0x80},
	{0x3083, 0x40},
	{0x3084, 0x80},
	{0x3085, 0x40},
	{0x32b0, 0x02},
	{0x32b1, 0x9F},
	{0x32BB, 0x1b},
	{0x32bd, 0x05},
	{0x32be, 0x05},
	{0x32cd, 0x01},
	{0x32d3, 0x13},
	{0x32d7, 0x82},
	{0x32f6, 0x0c},
	{0x32B2, 0x00},
	{0x32B3, 0xc8},
	{0x32B4, 0x00},
	{0x32B5, 0x97}, 
	{0x32B6, 0x02}, 
	{0x32B7, 0x58}, 
	{0x32B8, 0x01}, 
	{0x32B9, 0xc3}, 
	//edge table
	{0x3326, 0x04}, 
	{0x3327, 0x0a}, 
	{0x3328, 0x0a}, 
	{0x3329, 0x06}, 
	{0x332A, 0x06}, 
	{0x332B, 0x1c}, 
	{0x332C, 0x1c}, 
	{0x332D, 0x00}, 
	{0x332E, 0x1d}, 
	{0x332F, 0x1f},
	/* 
	//edge table winson
	{0x3326, 0x03}, 
	{0x3327, 0x04}, 
	{0x3328, 0x04}, 
	{0x3329, 0x04}, 
	{0x332A, 0x04}, 
	{0x332B, 0x1c}, 
	{0x332C, 0x1c}, 
	{0x332D, 0x04}, 
	{0x332E, 0x1e}, 
	{0x332F, 0x1e},
	*/
	{0x329b, 0x01}, 
	{0x32A1, 0x01}, 
	{0x32A2, 0x20}, 
	{0x32A3, 0x01}, 
	{0x32A4, 0xa0}, 
	{0x32A5, 0x01}, 
	{0x32A6, 0x40}, 
	{0x32A7, 0x01}, 
	{0x32A8, 0xe0}, 
	{0x3321, 0x0A}, 
	{0x3322, 0x0A},  
	{0x306D, 0x01}, 
	{0x32d8, 0x3f},
	{0x32d9, 0x18},

	{0x3320, 0x20}, 
	{0x3338, 0x02},
	{0x3339, 0x18},
	{0x333A, 0x28}, 
	{0x3324, 0x07},
	{0x3331, 0x06},
	{0x3332, 0x40},
	{0x3300, 0x30}, 
	{0x3301, 0x80}, 

	{0x32C6, 0x16},
	{0x32C7, 0x62},
	{0x32C0, 0x22},
	{0x32CD, 0x02},
	{0x3320, 0x20},
	{0x32bf, 0x49},
	{0x3200, 0x3e},
	{0x3201, 0x3f},
	{0x3077, 0x0f},
	{0x32c5, 0x14},
	{0x32c1, 0x24},
	{0x32c2, 0x80},
	{0x32bc, AE_TARGET_MEAN},
	{0x3262, 0x02},
	{0x3263, 0x02},
	{0x3264, 0x02},

	//{0x32a9, 0x21},
	//{0x32a9, 0x01},
	//{0x32aa, 0x01},
	//{0x3025, 0x02},

	{0x32f1, 0x05},
	{0x32f3, 0xa0},
	{0x32f2, 0x78},
	{0x32f4, 0x80},
	{0x32f5, 0x80},
	//Chroma
	{0x32f6, 0x0d},
	{0x32f9, 0x51},
	{0x32fA, 0x33},

	{0x3012, 0x04}, 
	{0x3013, 0x60},   
	{0x301d, 0x00},

	{0x3104, 0x01},
	{0x3060, 0x01},
#endif

	//preview  
	{0x32d8, 0x3f},
	{0x32d9, 0x1a},
	{0x3320, 0x20}, 
	//{0x3338, 0x00},//02
	{0x3331, 0x04},
	{0x3332, 0x80},
	{0x3300, 0x70},		//0x70 
	{0x3301, 0x80}, 
	{0x3077, 0x0f},
	{0x3324, 0x07},
	{0x3339, 0x10},
	{0x333a, 0x28},
	{0x3200, 0x3e},
	
#if 0	//def NT99250_TLINE_LENGTH_2500
	//SVGA pclk=58M
	//[1440x1080_10_Fps]
	{0x32e0, 0x03}, 
	{0x32e1, 0x20}, 
	{0x32e2, 0x02}, 
	{0x32e3, 0x5a}, 
	{0x32e4, 0x00}, 
	{0x32e5, 0xcd}, 
	{0x32e6, 0x00}, 
	{0x32e7, 0xcc}, 
	{0x301e, 0x00}, 
	{0x301f, 0x21}, //0x27
	{0x3022, 0x24}, 
	{0x3023, 0x24}, 
	{0x3002, 0x00}, 
	{0x3003, 0x54}, 
	{0x3004, 0x00}, 
	{0x3005, 0x40}, 
	{0x3006, 0x05}, 
	{0x3007, 0xf3}, 
	{0x3008, 0x04}, 
	{0x3009, 0x77}, 
	{0x300a, 0x09}, 
	{0x300b, 0xc4},
	//{0x300c, 0x05},	//8.33fps 
	//{0x300d, 0xa0},
	//{0x300d, 0x35},	//9fps
	{0x300c, 0x04}, 
	{0x300d, 0xb0},	//10fps  
	{0x300e, 0x05}, 
	{0x300f, 0xa0}, 
	{0x3010, 0x04}, 
	{0x3011, 0x38}, 
	{0x32bb, 0x0b},
	//{0x32c1, 0x25}, //8.33fps
	//{0x32c2, 0xa0}, 
	//{0x32c2, 0x35},  //9fps  
	{0x32c1, 0x24},  //10fps
	{0x32c2, 0xb0}, 
	{0x32c8, 0x78}, 
	{0x32c9, 0x64}, 
	{0x32c4, 0x00}, 
	{0x3201, 0x7f}, 
#else
	//SVGA pclk=56M
	//[1440x1080_10_Fps]
	{0x32e0, 0x03}, 
	{0x32e1, 0x20}, 
	{0x32e2, 0x02}, 
	{0x32e3, 0x5a}, 
	{0x32e4, 0x00}, 
	{0x32e5, 0xcd}, 
	{0x32e6, 0x00}, 
	{0x32e7, 0xcc}, 
	{0x301e, 0x00}, 
	{0x301f, 0x27}, 
	{0x3022, 0x24}, 
	{0x3023, 0x24}, 
	{0x3002, 0x00}, 
	{0x3003, 0x54}, 
	{0x3004, 0x00}, 
	{0x3005, 0x40}, 
	{0x3006, 0x05}, 
	{0x3007, 0xf3}, 
	{0x3008, 0x04}, 
	{0x3009, 0x77}, 
	{0x300a, 0x09}, 
	{0x300b, 0xc4},
	//{0x300c, 0x05},	//8.33fps 
	//{0x300d, 0x40},
	{0x300c, 0x04}, 
	//{0x300d, 0xd0},	//9fps
	{0x300d, 0x60},	//10fps 
	{0x300e, 0x05}, 
	{0x300f, 0xa0}, 
	{0x3010, 0x04}, 
	{0x3011, 0x38}, 
	{0x32bb, 0x0b},
	//{0x32c1, 0x25}, //8.33fps
	//{0x32c2, 0x40}, 
	//{0x32c1, 0x24},  //9fps 
	//{0x32c2, 0xd0}, 
	{0x32c1, 0x24},  //10fps 
	{0x32c2, 0x60},  
	{0x32c8, 0x70}, 
	{0x32c9, 0x5d}, 
	{0x32c4, 0x00}, 
	{0x3201, 0x7f},
#endif

  	//AE_STATISTICS_MODE
#ifdef AE_STATISTICS_MODE_CENTER
	{0x32b0, 0x02},		//Center_Weighting
	{0x32b1, 0xd0},
#else
	{0x32b0, 0x00},		//Average
	{0x32b1, 0xd0},
#endif
	{0x32B2, 0x00},
	{0x32B3, 0xc8},
	{0x32B4, 0x00},
	{0x32B5, 0x97}, 
	{0x32B6, 0x02}, 
	{0x32B7, 0x58}, 
	{0x32B8, 0x01}, 
	{0x32B9, 0xc3}, 

	//{0x3025,0x02},		//test pattern : color bars 
	{0x3021, 0x06}, 
	{0x3060, 0x01}, 
	
	{0xffff, 0xff},
};

static void NT99250_capture_set(struct nt99250_device *dev)
{
	unsigned short nt99250_pll,AEC_AWB;   
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//dprintk("------nt99250_capture_set %s %d\n", __FUNCTION__, __LINE__);

	nt99250_pll = i2c_get_byte(client,0x301e);
	AEC_AWB = i2c_get_byte(client,0x3201);
	i2c_put_byte(client,0x3201, (AEC_AWB&0x0f));  // for capture
	/*
	if((nt99250_pll & 0x04)==0x00)
	{
		i2c_put_byte(client,0x3077, 0x00);  // for capture
		i2c_put_byte(client,0x301e, 0x04);  // for capture
	}*/
	if(i2c_get_byte(this_client, 0x301d)>0x0a)
	{	
		i2c_put_byte(client, 0x301e, 0x04);
		i2c_put_byte(client, 0x3077, 0x00);
	}
}

int NT99250_preview(struct nt99250_device *dev)
{
	// set NT99250 to preview mode
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp_reg;
	int i=0;
	int regPreview[] =
	{
	//NT99250 reference setting 2012-04-24
	//MCLK = 24MHz
  //==============================
  //Output  size  : 800*600
  //==============================
  
	0x32d8, 0x3f,
	0x32d9, 0x1a,
	0x3320, 0x20, 
	//0x3338, 0x00,//02
	0x3331, 0x04,
	0x3332, 0x80,
	0x3300, 0x70,		//0x70 
	0x3301, 0x80, 
	0x3077, 0x0f,
	0x3324, 0x07,
	0x3339, 0x10,
	0x333a, 0x28,
	0x3200, 0x3e,
	
#if 0	//def NT99250_TLINE_LENGTH_2500
	//SVGA pclk=58M
	//[1440x1080_10_Fps]
	0x32e0, 0x03, 
	0x32e1, 0x20, 
	0x32e2, 0x02, 
	0x32e3, 0x5a, 
	0x32e4, 0x00, 
	0x32e5, 0xcd, 
	0x32e6, 0x00, 
	0x32e7, 0xcc, 
	0x301e, 0x00, 
	0x301f, 0x21, //0x27
	0x3022, 0x24, 
	0x3023, 0x24, 
	0x3002, 0x00, 
	0x3003, 0x54, 
	0x3004, 0x00, 
	0x3005, 0x40, 
	0x3006, 0x05, 
	0x3007, 0xf3, 
	0x3008, 0x04, 
	0x3009, 0x77, 
	0x300a, 0x09, 
	0x300b, 0xc4,
	//0x300c, 0x05,	//8.33fps 
	//0x300d, 0xa0,
	//0x300d, 0x35,	//9fps
	0x300c, 0x04, 
	0x300d, 0xb0,	//10fps  
	0x300e, 0x05, 
	0x300f, 0xa0, 
	0x3010, 0x04, 
	0x3011, 0x38, 
	0x32bb, 0x0b,
	//0x32c1, 0x25, //8.33fps
	//0x32c2, 0xa0, 
	//0x32c2, 0x35,  //9fps  
	0x32c1, 0x24,  //10fps
	0x32c2, 0xb0, 
	0x32c8, 0x78, 
	0x32c9, 0x64, 
	0x32c4, 0x00, 
	0x3201, 0x7f, 
#else
	//SVGA pclk=56M
	//[1440x1080_10_Fps]
	0x32e0, 0x03, 
	0x32e1, 0x20, 
	0x32e2, 0x02, 
	0x32e3, 0x5a, 
	0x32e4, 0x00, 
	0x32e5, 0xcd, 
	0x32e6, 0x00, 
	0x32e7, 0xcc, 
	0x301e, 0x00, 
	0x301f, 0x27, 
	0x3022, 0x24, 
	0x3023, 0x24, 
	0x3002, 0x00, 
	0x3003, 0x54, 
	0x3004, 0x00, 
	0x3005, 0x40, 
	0x3006, 0x05, 
	0x3007, 0xf3, 
	0x3008, 0x04, 
	0x3009, 0x77, 
	0x300a, 0x09, 
	0x300b, 0xc4,
	//0x300c, 0x05,	//8.33fps 
	//0x300d, 0x40,
	0x300c, 0x04, 
	//0x300d, 0xd0,	//9fps
	0x300d, 0x60,	//10fps 
	0x300e, 0x05, 
	0x300f, 0xa0, 
	0x3010, 0x04, 
	0x3011, 0x38, 
	0x32bb, 0x0b,
	//0x32c1, 0x25, //8.33fps
	//0x32c2, 0x40, 
	//0x32c1, 0x24,  //9fps 
	//0x32c2, 0xd0, 
	0x32c1, 0x24,  //10fps 
	0x32c2, 0x60,  
	0x32c8, 0x70, 
	0x32c9, 0x5d, 
	0x32c4, 0x00, 
	0x3201, 0x7f,
#endif

  	//AE_STATISTICS_MODE
#ifdef AE_STATISTICS_MODE_CENTER
	0x32b0, 0x02,		//Center_Weighting
	0x32b1, 0xd0,
#else
	0x32b0, 0x00,		//Average
	0x32b1, 0xd0,
#endif
	0x32B2, 0x00,
	0x32B3, 0xc8,
	0x32B4, 0x00,
	0x32B5, 0x97, 
	0x32B6, 0x02, 
	0x32B7, 0x58, 
	0x32B8, 0x01, 
	0x32B9, 0xc3, 

	//0x3025,0x02,		//test pattern : color bars 
	0x3021, 0x06, 
	0x3060, 0x01, 
	};
	// Write preview table
	for (i=0; i<sizeof(regPreview)/sizeof(int); i+=2)
	{
	i2c_put_byte(client,regPreview[i], regPreview[i+1]);
	}
	
	temp_reg=i2c_get_byte(client,0x3201);
	//printk("NT99250_preview(CCCCC): NT99250_get_AE_AWB_3201=%x\n",temp_reg);
    //printk("wb mode: camera_wb_state=%d .\n",camera_wb_state);
	if(camera_wb_state)
	{	
		i2c_put_byte(client,0x3201, temp_reg&~0x10);  // select manual WB
	}
	else
	{
		i2c_put_byte(client,0x3201, temp_reg|0x10);   // select Auto WB
	}
	temp_reg=i2c_get_byte(client,0x3201);
	//printk("_GJL_  NT99250_get_AE_AWB_3201=%x\n",temp_reg);

	return 0;
}
int NT99250_capture(struct nt99250_device *dev)
{
	// set NT99250 to preview mode
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int i=0;

	int regCapture[] =
	{
	//NT99250 reference setting 2012-04-19
	//==============================
	//Output  size  : 1600*1200
	//==============================

	0x3300, 0x3f,  //0x7f
	0x3301, 0x60,
	0x3320, 0x20,
	0x3331, 0x04,
	0x3332, 0x80,    	
	0x3200, 0x3e,
	0x32d8, 0x3f,
	0x32d9, 0x1a,
	//0x32c5, 0x18,	

	//0x3200, 0x3e,
	0x3201, 0x0f,
	//Resoltion Setting : 1600*1200
#if 0
	//[1600x1200_8.33_Fps]
	//PCLK_58M
	0x301e, 0x00, 
	0x301f, 0x21, 
	0x3022, 0x24, 
	0x3023, 0x24, 
	0x3002, 0x00, 
	0x3003, 0x04, 
	0x3004, 0x00, 
	0x3005, 0x04, 
	0x3006, 0x06, 
	0x3007, 0x43, 
	0x3008, 0x04, 
	0x3009, 0xb3, 
	0x300a, 0x09, 
	0x300b, 0xc4, 
	0x300c, 0x05, 
	0x300d, 0xa0, 
	0x300e, 0x06, 
	0x300f, 0x40, 
	0x3010, 0x04, 
	0x3011, 0xb2, 
	0x32bb, 0x0b,  
	0x32c1, 0x29, 
	0x32c2, 0x60, 
	0x32c8, 0x78, 
	0x32c9, 0x64, 
	0x32c4, 0x00, 
	0x3201, 0x0f,
#else
	//PCLK_56M
	0x301e, 0x00,  //0x04	
	0x301f, 0x27,
	0x3022, 0x24,
	0x3023, 0x24,
	0x3002, 0x00,
	0x3003, 0x04,
	0x3004, 0x00,
	0x3005, 0x04,
	0x3006, 0x06,
	0x3007, 0x43,
	0x3008, 0x04,
	0x3009, 0xb3,
	0x300a, 0x09,
	0x300b, 0x82,
	0x300c, 0x04,
	0x300d, 0xb9,
	0x300e, 0x06,
	0x300f, 0x40,
	0x3010, 0x04,
	0x3011, 0xb2,  //0xb0
	0x32c4, 0x00,
	0x3201, 0x0f,
#endif
	0x3021, 0x06,
	0x3060, 0x01,
	// extreme exposure off, AGC off, AEC off
	};
		
	// Write preview table
	for (i=0; i<sizeof(regCapture)/sizeof(int); i+=2)
	{
		i2c_put_byte(client, regCapture[i], regCapture[i+1]);
	}
	NT99250_capture_set(dev);
	
	msleep(300);	
	return 0;
}

//load NT99250 parameters
void NT99250_init_regs(struct nt99250_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    int i=0;

    while(1)
    {
        if (NT99250_script[i].val==0xff&&NT99250_script[i].addr==0xffff)
        {
        	printk("NT99250_write_regs success in initial NT99250.\n");
        	break;
        }
        if((i2c_put_byte(client,NT99250_script[i].addr, NT99250_script[i].val)) < 0)
        {
        	printk("fail in initial NT99250. i=%d\n",i);
		return;
		}
		i++;
    }

    return;
}
/*************************************************************************
* FUNCTION
*    NT99250_set_param_wb
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
void NT99250_set_param_wb(struct nt99250_device *dev,enum  camera_wb_flip_e para)//white balance
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
			i2c_put_byte(client,0x3291, 0x48);
			i2c_put_byte(client,0x3296, 0x01);
			i2c_put_byte(client,0x3297, 0x58);
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
			i2c_put_byte(client,0x3291, 0x24);
			i2c_put_byte(client,0x3296, 0x01);
			i2c_put_byte(client,0x3297, 0x78);
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


} /* NT99250_set_param_wb */

/*************************************************************************
* FUNCTION
*    NT99250_set_param_exposure
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
void NT99250_set_param_exposure(struct nt99250_device *dev,enum camera_exposure_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	switch (para)
	{
		case EXPOSURE_N4_STEP:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f2, 0x58);
			break;
		case EXPOSURE_N3_STEP:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f2, 0x60);
			break;
		case EXPOSURE_N2_STEP:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f2, 0x68);
			break;
		case EXPOSURE_N1_STEP:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f2, 0x70);
			break;
		case EXPOSURE_0_STEP:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f2, 0x80);
			break;
		case EXPOSURE_P1_STEP:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f2, 0x90);
			break;
		case EXPOSURE_P2_STEP:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f2, 0xa0);
			break;
		case EXPOSURE_P3_STEP:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f2, 0xa8);
			break;
		case EXPOSURE_P4_STEP:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f2, 0xb0);
			break;
		default:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f2, 0xb8);
			break;	

	}


} /* NT99250_set_param_exposure */
/*************************************************************************
* FUNCTION
*    NT99250_set_param_effect
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
void NT99250_set_param_effect(struct nt99250_device *dev,enum camera_effect_flip_e para)
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
			//i2c_put_byte(client,0x32f6, 0x20);
			break;

		case CAM_EFFECT_ENC_SEPIAGREEN:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f4, 0x60);
			i2c_put_byte(client,0x32f5, 0x20);
			//i2c_put_byte(client,0x32f6, 0x0c);
			break;

		case CAM_EFFECT_ENC_SEPIABLUE:
			i2c_put_byte(client,0x32f1, 0x05);
			i2c_put_byte(client,0x32f4, 0x04);
			i2c_put_byte(client,0x32f5, 0x80);
			//i2c_put_byte(client,0x32f6, 0x0c);
			break;

		case CAM_EFFECT_ENC_COLORINV:
			i2c_put_byte(client,0x32f1, 0x03);
			//i2c_put_byte(client,0x32f6, 0x10);
			break;

		default:
			break;
	}



} /* NT99250_set_param_effect */

/*************************************************************************
* FUNCTION
*    NT99250_NightMode
*
* DESCRIPTION
*    This function night mode of NT99250.
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
void NT99250_set_night_mode(struct nt99250_device *dev,enum  camera_night_mode_flip_e enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp;
	if (enable)
	{
		temp = i2c_get_byte(client, 0x32bb);
		temp = temp | 0x10; // night mode on, bit[4] = 1
		i2c_put_byte(client, 0x32bb, temp);
	}
	else
	{
		temp = i2c_get_byte(client, 0x32bb);
		temp = temp & 0xef; // night mode off, bit[4] = 0		
		i2c_put_byte(client, 0x32bb, temp);
	}

}    /* NT99250_NightMode */
void NT99250_set_param_banding(struct nt99250_device *dev,enum  camera_banding_flip_e banding)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);	
	int temp;
	switch(banding){
	case CAM_BANDING_50HZ:			
		temp = i2c_get_byte(client, 0x32bb);
		temp = temp | 0x8; // banding 50, bit[3] = 1
		i2c_put_byte(client, 0x32bb, temp);				
		printk(KERN_INFO "banding 50 in\n");			
		break;
	case CAM_BANDING_60HZ:			
		temp = i2c_get_byte(client, 0x32bb);
		temp = temp & 0xf7; // banding 60, bit[3] = 0
		i2c_put_byte(client, 0x32bb, temp);									
		printk(KERN_INFO " banding 60 in\n ");				
		break;
	default:
		break;
	}
}

static int set_flip(struct nt99250_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	temp = i2c_get_byte(client, 0x3022);
	temp &= 0xfc;
	temp |= dev->cam_info.m_flip << 1;
	temp |= dev->cam_info.v_flip << 0;
	if((i2c_put_byte(client, 0x3821, temp)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
        }
        return 0;
}

void NT99250_set_resolution(struct nt99250_device *dev,int height,int width)
{

	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    #if 1	 
	if(height&&width&&(height<=1200)&&(width<=1600))
	{		
		if((height<=600)&&(width<=800))
		{
			#if 1
			NT99250_preview(dev);		
			nt99250_h_active=800;
			nt99250_v_active=600;	
			nt99250_frmintervals_active.denominator = 15;
			nt99250_frmintervals_active.numerator = 1;	
			#endif			
		}
		else
		{
			#if 1
			NT99250_capture(dev);
			#endif
			msleep(10);// 10
			nt99250_h_active=1600;
			nt99250_v_active=1200;
			nt99250_frmintervals_active.denominator = 5;
			nt99250_frmintervals_active.numerator = 1;
		}
	}
    #else
		i2c_put_byte(client,0x32e0, 0x03);
		i2c_put_byte(client,0x32e1, 0x20);
		i2c_put_byte(client,0x32e2, 0x02);
		i2c_put_byte(client,0x32e3, 0x58);// 5a		
    #endif
	set_flip(dev);
}    /* NT99250_set_resolution */

unsigned char v4l_2_nt99250(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int nt99250_setting(struct nt99250_device *dev,int PROP_ID,int value )
{
	int ret=0;
	//unsigned char cur_val;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		//dprintk(dev, 1, "setting brightned:%d\n",v4l_2_NT99250(value));
		//ret=i2c_put_byte(client,0x0201,v4l_2_nt99250(value));
		break;
	case V4L2_CID_CONTRAST:
		//ret=i2c_put_byte(client,0x0200, value);
		break;
	case V4L2_CID_SATURATION:
		//ret=i2c_put_byte(client,0x0202, value);
		break;
#if 0
	case V4L2_CID_EXPOSURE:
		ret=i2c_put_byte(client,0x0201, value);
		break;

	case V4L2_CID_HFLIP:    /* set flip on H. */
		ret=i2c_get_byte(client,0x3022);
		if(ret>0) {
			cur_val=(char)ret;
			if(value!=0)
				cur_val=cur_val|0x1;
			else
				cur_val=cur_val&0xFE;
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
				cur_val=cur_val|0x02;
			else
				cur_val=cur_val&0xFD;
			ret=i2c_put_byte(client,0x3022,cur_val);
		} else {
			dprintk(dev, 1, "vertical read error\n");
		}
		break;
#endif
	case V4L2_CID_DO_WHITE_BALANCE:
        if(nt99250_qctrl[0].default_value!=value){
			nt99250_qctrl[0].default_value=value;
			NT99250_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
        	}
		break;
	case V4L2_CID_EXPOSURE:
        if(nt99250_qctrl[1].default_value!=value){
			nt99250_qctrl[1].default_value=value;
			NT99250_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
        	}
		break;
	case V4L2_CID_COLORFX:
        if(nt99250_qctrl[2].default_value!=value){
			nt99250_qctrl[2].default_value=value;
			NT99250_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
        	}
		break;
	case V4L2_CID_WHITENESS:
		 if(nt99250_qctrl[3].default_value!=value){
			nt99250_qctrl[3].default_value=value;
			NT99250_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		break;
	case V4L2_CID_BLUE_BALANCE:
		if(nt99250_qctrl[4].default_value!=value){
			nt99250_qctrl[4].default_value=value;
			NT99250_set_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(nt99250_qctrl[5].default_value!=value){
			nt99250_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(nt99250_qctrl[7].default_value!=value){
			nt99250_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(nt99250_qctrl[8].default_value!=value){
			nt99250_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;

}

static void power_down_nt99250(struct nt99250_device *dev)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//i2c_put_byte(client,0x3012, 0x80);
	//msleep(5);
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

static void nt99250_fillbuff(struct nt99250_fh *fh, struct nt99250_buffer *buf)
{
	struct nt99250_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = nt99250_qctrl[5].default_value&3;// not set
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = nt99250_qctrl[7].default_value;
	para.angle = nt99250_qctrl[8].default_value;
	para.vaddr = (unsigned)vbuf;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void nt99250_thread_tick(struct nt99250_fh *fh)
{
	struct nt99250_buffer *buf;
	struct nt99250_device *dev = fh->dev;
	struct nt99250_dmaqueue *dma_q = &dev->vidq;

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
			 struct nt99250_buffer, vb.queue);
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
	nt99250_fillbuff(fh, buf);
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

static void nt99250_sleep(struct nt99250_fh *fh)
{
	struct nt99250_device *dev = fh->dev;
	struct nt99250_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	nt99250_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int nt99250_thread(void *data)
{
	struct nt99250_fh  *fh = data;
	struct nt99250_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		nt99250_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int nt99250_start_thread(struct nt99250_fh *fh)
{
	struct nt99250_device *dev = fh->dev;
	struct nt99250_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(nt99250_thread, fh, "nt99250");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void nt99250_stop_thread(struct nt99250_dmaqueue  *dma_q)
{
	struct nt99250_device *dev = container_of(dma_q, struct nt99250_device, vidq);

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
	struct nt99250_fh  *fh = vq->priv_data;
	struct nt99250_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct nt99250_buffer *buf)
{
	struct nt99250_fh  *fh = vq->priv_data;
	struct nt99250_device *dev  = fh->dev;

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
	struct nt99250_fh     *fh  = vq->priv_data;
	struct nt99250_device    *dev = fh->dev;
	struct nt99250_buffer *buf = container_of(vb, struct nt99250_buffer, vb);
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
	struct nt99250_buffer    *buf  = container_of(vb, struct nt99250_buffer, vb);
	struct nt99250_fh        *fh   = vq->priv_data;
	struct nt99250_device       *dev  = fh->dev;
	struct nt99250_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct nt99250_buffer   *buf  = container_of(vb, struct nt99250_buffer, vb);
	struct nt99250_fh       *fh   = vq->priv_data;
	struct nt99250_device      *dev  = (struct nt99250_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops nt99250_video_qops = {
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
	struct nt99250_fh  *fh  = priv;
	struct nt99250_device *dev = fh->dev;

	strcpy(cap->driver, "nt99250");
	strcpy(cap->card, "nt99250");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = NT99250_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct nt99250_fmt *fmt;

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
	
	if(fival->index > ARRAY_SIZE(nt99250_frmivalenum))
		return -EINVAL;
	
	for(k =0; k< ARRAY_SIZE(nt99250_frmivalenum); k++)
	{
		if( (fival->index==nt99250_frmivalenum[k].index)&&
				(fival->pixel_format ==nt99250_frmivalenum[k].pixel_format )&&
				(fival->width==nt99250_frmivalenum[k].width)&&
				(fival->height==nt99250_frmivalenum[k].height)){
			memcpy( fival, &nt99250_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
			return 0;
		}
	}
	
	return -EINVAL;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct nt99250_fh *fh = priv;

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
	struct nt99250_fh *fh = priv;
	struct nt99250_device *dev = fh->dev;
	struct v4l2_captureparm *cp = &parms->parm.capture;

	dprintk(dev,3,"vidioc_g_parm\n");
	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	
	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	
	cp->timeperframe = nt99250_frmintervals_active;
	printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
	cp->timeperframe.numerator );
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct nt99250_fh  *fh  = priv;
	struct nt99250_device *dev = fh->dev;
	struct nt99250_fmt *fmt;
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
	struct nt99250_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct nt99250_device *dev = fh->dev;

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
	
	if (f->fmt.pix.pixelformat==V4L2_PIX_FMT_RGB24) {
		vidio_set_fmt_ticks=1;
		NT99250_set_resolution(dev,fh->height,fh->width);
	} else if (vidio_set_fmt_ticks==1) {
		NT99250_set_resolution(dev,fh->height,fh->width);
	}

	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct nt99250_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct nt99250_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct nt99250_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct nt99250_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct nt99250_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct nt99250_fh  *fh = priv;
	struct nt99250_device *dev = fh->dev;
	vdin_parm_t para;
	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para ));
	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = nt99250_frmintervals_active.denominator
				/nt99250_frmintervals_active.numerator;//175
	para.h_active = nt99250_h_active;
	para.v_active = nt99250_v_active;
	para.hsync_phase = 0;
	para.vsync_phase  = 0;	
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
	para.skip_count =  2;//skip num
	para.bt_path = dev->cam_info.bt_path;
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
		vops->start_tvin_service(0,&para);
		fh->stream_on = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct nt99250_fh  *fh = priv;

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
	struct nt99250_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(nt99250_prev_resolution))
			return -EINVAL;
		frmsize = &nt99250_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(nt99250_pic_resolution))
			return -EINVAL;
		frmsize = &nt99250_pic_resolution[fsize->index];
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
	struct nt99250_fh *fh = priv;
	struct nt99250_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct nt99250_fh *fh = priv;
	struct nt99250_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(nt99250_qctrl); i++)
		if (qc->id && qc->id == nt99250_qctrl[i].id) {
			memcpy(qc, &(nt99250_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct nt99250_fh *fh = priv;
	struct nt99250_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(nt99250_qctrl); i++)
		if (ctrl->id == nt99250_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct nt99250_fh *fh = priv;
	struct nt99250_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(nt99250_qctrl); i++)
		if (ctrl->id == nt99250_qctrl[i].id) {
			if (ctrl->value < nt99250_qctrl[i].minimum ||
			    ctrl->value > nt99250_qctrl[i].maximum ||
			    nt99250_setting(dev,ctrl->id,ctrl->value)<0) {
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
/*void NT99250_get_AE_AWB_3201(struct nt99250_device *dev)		//_GJL_
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp_reg=i2c_get_byte(client,0x3201);
	//dprintk("_GJL_  NT99250_get_AE_AWB_3201=%x\n",temp_reg);
}*/

static int nt99250_open(struct file *file)
{
	struct nt99250_device *dev = video_drvdata(file);
	struct nt99250_fh *fh = NULL;
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
	
	NT99250_init_regs(dev);
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
	fh->f_flags = file->f_flags;
	/* Resets frame counters */
	dev->jiffies = jiffies;

//    TVIN_SIG_FMT_CAMERA_640X480P_30Hz,
//    TVIN_SIG_FMT_CAMERA_800X600P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1024X768P_30Hz, // 190
//    TVIN_SIG_FMT_CAMERA_1920X1080P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1280X720P_30Hz,

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &nt99250_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct nt99250_buffer), fh,NULL);

	nt99250_start_thread(fh);

	return 0;
}

static ssize_t
nt99250_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct nt99250_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
nt99250_poll(struct file *file, struct poll_table_struct *wait)
{
	struct nt99250_fh        *fh = file->private_data;
	struct nt99250_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int nt99250_close(struct file *file)
{
	struct nt99250_fh         *fh = file->private_data;
	struct nt99250_device *dev       = fh->dev;
	struct nt99250_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);

	nt99250_stop_thread(vidq);
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
	nt99250_h_active=800;
	nt99250_v_active=600;	//598;
	nt99250_qctrl[0].default_value=CAM_WB_AUTO;
	nt99250_qctrl[1].default_value=4;
	nt99250_qctrl[2].default_value=0;
	nt99250_qctrl[3].default_value=CAM_BANDING_50HZ;
	nt99250_qctrl[4].default_value=0;

	nt99250_qctrl[5].default_value=0;
	nt99250_qctrl[7].default_value=100;
	
	nt99250_frmintervals_active.numerator = 1;
	nt99250_frmintervals_active.denominator = 15;

	power_down_nt99250(dev);
#endif
	msleep(10);
	aml_cam_uninit(&dev->cam_info);

	msleep(10);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 0);
#endif		
	wake_unlock(&(dev->wake_lock));
#ifdef CONFIG_CMA
    vm_deinit_buf();
#endif
	return 0;
}

static int nt99250_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct nt99250_fh  *fh = file->private_data;
	struct nt99250_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations nt99250_fops = {
	.owner		= THIS_MODULE,
	.open           = nt99250_open,
	.release        = nt99250_close,
	.read           = nt99250_read,
	.poll		= nt99250_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = nt99250_mmap,
};

static const struct v4l2_ioctl_ops nt99250_ioctl_ops = {
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

static struct video_device nt99250_template = {
	.name		= "nt99250_v4l",
	.fops           = &nt99250_fops,
	.ioctl_ops 	= &nt99250_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int nt99250_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_NT99250, 0);
}

static const struct v4l2_subdev_core_ops nt99250_core_ops = {
	.g_chip_ident = nt99250_g_chip_ident,
};

static const struct v4l2_subdev_ops nt99250_ops = {
	.core = &nt99250_core_ops,
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
    .name = "camera-nt99250",
    .class_attrs = camera_ctrl_class_attrs,
};
//****************************

static int nt99250_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	struct nt99250_device *t;
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
	v4l2_i2c_subdev_init(sd, client, &nt99250_ops);
	plat_dat= (aml_cam_info_t*)client->dev.platform_data;

	/* test if devices exist. */
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_PROBE
	unsigned char buf[4];
	buf[0]=0;
	plat_dat->device_init();
	err=i2c_get_byte(client,0); 
	plat_dat->device_uninit(); 
	if(err<0) return  -ENODEV;
#endif

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
	memcpy(t->vdev, &nt99250_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);
	
	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "nt99250");
	/* Register it */
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if (plat_dat->front_back >=0)
			video_nr = plat_dat->front_back;
	} else {
		printk("camera nt99250: have no platform data\n");
		kfree(t);
		kfree(client);
		return -1;
	}
	err = video_register_device(t->vdev, VFL_TYPE_GRABBER, video_nr);
	if (err < 0) {
		video_device_release(t->vdev);
		kfree(t);
		return err;
	}

	t->cam_info.version = NT99250_DRIVER_VERSION;
	if (aml_cam_info_reg(&t->cam_info) < 0)
		printk("reg caminfo error\n");

	ret = class_register(&camera_ctrl_class);
	if(ret){
		printk("class register camera_ctrl_class fail!\n");
	}
	return 0;
}

static int nt99250_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct nt99250_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id nt99250_id[] = {
	{ "nt99250", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nt99250_id);

static struct i2c_driver nt99250_i2c_driver = {
	.driver = {
		.name = "nt99250",
	},
	.probe = nt99250_probe,
	.remove = nt99250_remove,
	.id_table = nt99250_id,
};

module_i2c_driver(nt99250_i2c_driver);

