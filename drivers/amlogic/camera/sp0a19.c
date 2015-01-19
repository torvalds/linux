/*
 *sp0a19 - This code emulates a real video device with v4l2 api
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
//#include <mach/gpio_data.h>
#include "common/plat_ctrl.h"
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif

#define SP0A19_CAMERA_MODULE_NAME "sp0a19"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define SP0A19_CAMERA_MAJOR_VERSION 0
#define SP0A19_CAMERA_MINOR_VERSION 7
#define SP0A19_CAMERA_RELEASE 0
#define SP0A19_CAMERA_VERSION \
	KERNEL_VERSION(SP0A19_CAMERA_MAJOR_VERSION, SP0A19_CAMERA_MINOR_VERSION, SP0A19_CAMERA_RELEASE)

MODULE_DESCRIPTION("sp0a19 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define SP0A19_DRIVER_VERSION "SP0A19-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static int sp0a19_have_open=0;

static int sp0a19_h_active=320;
static int sp0a19_v_active=240;
static struct v4l2_fract sp0a19_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

static struct vdin_v4l2_ops_s *vops;

/* supported controls */
static struct v4l2_queryctrl sp0a19_qctrl[] = {
{
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

static struct v4l2_frmivalenum sp0a19_frmivalenum[]={
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

struct v4l2_querymenu sp0a19_qmenu_wbmode[] = {
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

struct v4l2_querymenu sp0a19_qmenu_anti_banding_mode[] = {
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
    struct v4l2_querymenu* sp0a19_qmenu;
}sp0a19_qmenu_set_t;

sp0a19_qmenu_set_t sp0a19_qmenu_set[] = {
    {
        .id         	= V4L2_CID_DO_WHITE_BALANCE,
        .num            = ARRAY_SIZE(sp0a19_qmenu_wbmode),
        .sp0a19_qmenu   = sp0a19_qmenu_wbmode,
    },{
        .id         	= V4L2_CID_POWER_LINE_FREQUENCY,
        .num            = ARRAY_SIZE(sp0a19_qmenu_anti_banding_mode),
        .sp0a19_qmenu   = sp0a19_qmenu_anti_banding_mode,
    },
};

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(sp0a19_qmenu_set); i++)
	if (a->id && a->id == sp0a19_qmenu_set[i].id) {
	    for(j = 0; j < sp0a19_qmenu_set[i].num; j++)
		if (a->index == sp0a19_qmenu_set[i].sp0a19_qmenu[j].index) {
			memcpy(a, &( sp0a19_qmenu_set[i].sp0a19_qmenu[j]),
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

struct sp0a19_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct sp0a19_fmt formats[] = {
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

static struct sp0a19_fmt *get_format(struct v4l2_format *f){
	struct sp0a19_fmt *fmt;
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
struct sp0a19_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct sp0a19_fmt        *fmt;
};

struct sp0a19_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(sp0a19_devicelist);

struct sp0a19_device {
	struct list_head			sp0a19_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct sp0a19_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t  cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(sp0a19_qctrl)];
};

static inline struct sp0a19_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sp0a19_device, sd);
}

struct sp0a19_fh {
	struct sp0a19_device            *dev;

	/* video capture */
	struct sp0a19_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};

static inline struct sp0a19_fh *to_fh(struct sp0a19_device *dev)
{
	return container_of(&dev, struct sp0a19_fh, dev);
}

static struct v4l2_frmsize_discrete sp0a19_prev_resolution[]= //should include 320x240 and 640x480, those two size are used for recording
{
	{320,240},
	//{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete sp0a19_pic_resolution[]=
{
	{640,480},
};

/* ------------------------------------------------------------------
	reg spec of SP0A19
   ------------------------------------------------------------------*/

struct aml_camera_i2c_fig1_s SP0A19_script[] = {  
//SP0A19 ini
	  {0xfd,0x00},
	  {0x1C,0x28},
	  {0x32,0x00},
	  {0x0f,0x2f},
	  {0x10,0x2e},
	  {0x11,0x00},
	  {0x12,0x18},
	  {0x13,0x2f},
	  {0x14,0x00},
	  {0x15,0x3f},
	  {0x16,0x00},
	  {0x17,0x18},
	  {0x25,0x40},
	  {0x1a,0x0b},
	  {0x1b,0xc },
	  {0x1e,0xb },
	  {0x20,0x3f}, // add
	  {0x21,0x13}, // 0x0c 24
	  {0x22,0x19},
	  {0x26,0x1a},
	  {0x27,0xab},
	  {0x28,0xfd},
	  {0x30,0x00},
	  {0x31,0x00},//0x00 0x20改摄像头移动方向
	{0xfb,0x33},
	{0x1f,0x08},  


	//Blacklevel
	{0xfd,0x00},
	{0x65,0x00},//06
	{0x66,0x00},//06
	{0x67,0x00},//06
	{0x68,0x00},//06
	  {0x45,0x00},//add wxc
	  {0x46,0x0f},//add wxc
        #if 1 //PZT 2013-7-11
	//ae setting
	{0xfd,0x00},
	{0x03,0x01},
	{0x04,0x32},
	{0x06,0x00},
	{0x09,0x01},
	{0x0a,0x46},
	{0xf0,0x66},
	{0xf1,0x00},
	{0xfd,0x01},
	{0x90,0x10},
	{0x92,0x01},
	{0x98,0x66},
	{0x99,0x00},
	{0x9a,0x01},
	{0x9b,0x00},
	{0xfd,0x01},
	{0xce,0x60},
	{0xcf,0x06},
	{0xd0,0x60},
	{0xd1,0x06}, 
       #endif
	#if 0 //PZT 2013-7-11
	  {0xfd,0x00},
	  {0x03,0x01},
	  {0x04,0x32},
	  {0x06,0x00},
	  {0x09,0x01},
	  {0x0a,0x46},
	  {0xf0,0x66},
	  {0xf1,0x00},
	  {0xfd,0x01},
	  {0x90,0x0c},
	  {0x92,0x01},
	  {0x98,0x66},
	  {0x99,0x00},
	  {0x9a,0x01},
	  {0x9b,0x00},
	  
	//Status
	{0xfd,0x01},
	{0xce,0xc8},
	{0xcf,0x04},
	{0xd0,0xc8},
	{0xd1,0x04}, 
       #endif

	{0xfd,0x01},
	{0xc4,0x56},
	{0xc5,0x8f},
	{0xca,0x30},
	{0xcb,0x45},
	{0xcc,0xb0},
	{0xcd,0x48},
	{0xfd,0x00},

	  //lsc  for st 
	  {0xfd,0x01},
	  {0x35,0x15},
	  {0x36,0x15}, //20
	  {0x37,0x15},
	  {0x38,0x15},
	  {0x39,0x15},
	  {0x3a,0x15}, //15
	  {0x3b,0x13},
	  {0x3c,0x15},
	  {0x3d,0x15},
	  {0x3e,0x15}, //12
	  {0x3f,0x15},
	  {0x40,0x18},
	  {0x41,0x00},
	  {0x42,0x04},
	  {0x43,0x04},
	  {0x44,0x00},
	  {0x45,0x00},
	  {0x46,0x00},
	  {0x47,0x00},
	  {0x48,0x00},
	  {0x49,0xfd},
	  {0x4a,0x00},
	  {0x4b,0x00},
	  {0x4c,0xfd},
	  {0xfd,0x00},

	//awb 1
	  {0xfd,0x01},
	  {0x28,0xc5},
	  {0x29,0x9b},
	//{0x10,0x08},
	//{0x11,0x14},	
	//{0x12,0x14},
	  {0x2e,0x02},	
	  {0x2f,0x16},
	  {0x17,0x17},
	  {0x18,0x19},	//0x29	 0813
	  {0x19,0x45},	

	//{0x1a,0x9e},//a1;a5   
	//{0x1b,0xae},//b0;9a
	//{0x33,0xef},
	  {0x2a,0xef},
	  {0x2b,0x15},

	  //awb2
	  {0xfd,0x01},
	  {0x73,0x80},
	  {0x1a,0x80},
	  {0x1b,0x80}, 
	//d65
	  {0x65,0xd5}, //d6
	  {0x66,0xfa}, //f0
	  {0x67,0x72}, //7a
	  {0x68,0x8a}, //9a
	//indoor
	  {0x69,0xc6}, //ab
	  {0x6a,0xee}, //ca
	  {0x6b,0x94}, //a3
	  {0x6c,0xab}, //c1
	//f 
	  {0x61,0x7a}, //82
	  {0x62,0x98}, //a5
	  {0x63,0xc5}, //d6
	  {0x64,0xe6}, //ec
	  //cwf
	  {0x6d,0xb9}, //a5
	  {0x6e,0xde}, //c2
	  {0x6f,0xb2}, //a7
	  {0x70,0xd5}, //c5
	 
	//skin detect
	 {0xfd,0x01},
	 {0x08,0x15},
	 {0x09,0x04},
	 {0x0a,0x20},
	 {0x0b,0x12},
	 {0x0c,0x27},
	 {0x0d,0x06},
	 {0x0f,0x63},//0x5f	0813

	   //BPC_grad
	  {0xfd,0x00},
	  {0x79,0xf0},
	  {0x7a,0x80}, //f0 
	  {0x7b,0x80}, //f0 
	  {0x7c,0x20},//f0	
#if 0
	//smooth
	  {0xfd,0x00},

	  //
	  {0x57,0x06}, //raw_dif_thr_outdoor
	  {0x58,0x0d}, //raw_dif_thr_normal
	  {0x56,0x10}, //raw_dif_thr_dummy
	  {0x59,0x10}, //raw_dif_thr_lowlight
		//GrGb
	  {0x89,0x06}, //raw_grgb_thr_outdoor 
	  {0x8a,0x0d}, //raw_grgb_thr_normal	
	  {0x9c,0x10}, //raw_grgb_thr_dummy	
	  {0x9d,0x10}, //raw_grgb_thr_lowlight
		//Gr\Gb
	  {0x81,0xe0}, //raw_gflt_fac_outdoor
	  {0x82,0xe0}, //raw_gflt_fac_normal
	  {0x83,0x80}, //raw_gflt_fac_dummy
	  {0x84,0x40}, //raw_gflt_fac_lowlight
		//GrGb
	  {0x85,0xe0}, //raw_gf_fac_outdoor  
	  {0x86,0xc0}, //raw_gf_fac_normal  
	  {0x87,0x80}, //raw_gf_fac_dummy   
	  {0x88,0x40}, //raw_gf_fac_lowlight
		//
	  {0x5a,0xff},  //raw_rb_fac_outdoor
	  {0x5b,0xe0},  //raw_rb_fac_normal
	  {0x5c,0x80},  //raw_rb_fac_dummy
	  {0x5d,0x00},  //raw_rb_fac_lowlight
	  
	//sharpen 
	  {0xfd,0x01}, 
	  {0xe2,0x30}, //sharpen_y_base
	  {0xe4,0xa0}, //sharpen_y_max

	{0xe5,0x04}, //rangek_neg_outdoor  //0x08
	{0xd3,0x04}, //rangek_pos_outdoor	//0x08
	{0xd7,0x04}, //range_base_outdoor	//0x08

	{0xe6,0x06}, //rangek_neg_normal
	{0xd4,0x06}, //rangek_pos_normal 
	{0xd8,0x06}, //range_base_normal  

	{0xe7,0x08}, //rangek_neg_dummy   // 0x10
	{0xd5,0x08}, //rangek_pos_dummy   // 0x10
	{0xd9,0x08}, //range_base_dummy    // 0x10

	{0xd2,0x10}, //rangek_neg_lowlight
	{0xd6,0x10}, //rangek_pos_lowlight
	{0xda,0x10}, //range_base_lowlight

	{0xe8,0x20},  //sharp_fac_pos_outdoor  // 0x35
	{0xec,0x35},  //sharp_fac_neg_outdoor
	{0xe9,0x20},  //sharp_fac_pos_nr	  // 0x35
	{0xed,0x25},//sharp_fac_neg_nr
	{0xea,0x20},//sharp_fac_pos_dummy
	{0xef,0x25},//sharp_fac_neg_dummy
	{0xeb,0x15},//sharp_fac_pos_low
	{0xf0,0x20},//sharp_fac_neg_low 
#else //PZT 2013-7-11
	{0xfd,0x00},

	{0x57,0x06}, //raw_dif_thr_outdoor
	{0x58,0x10}, //raw_dif_thr_normal
	{0x56,0x18}, //raw_dif_thr_dummy
	{0x59,0x10}, //raw_dif_thr_lowlight
	{0x89,0x06}, //raw_grgb_thr_outdoor 
	{0x8a,0x10}, //raw_grgb_thr_normal	
	{0x9c,0x18}, //raw_grgb_thr_dummy	
	{0x9d,0x10}, //raw_grgb_thr_lowlight
	{0x81,0xe0}, //raw_gflt_fac_outdoor
	{0x82,0xd8}, //raw_gflt_fac_normal
	{0x83,0x78}, //raw_gflt_fac_dummy
	{0x84,0x40}, //raw_gflt_fac_lowlight
	{0x85,0xe0}, //raw_gf_fac_outdoor  
	{0x86,0xb8}, //raw_gf_fac_normal  
	{0x87,0x78}, //raw_gf_fac_dummy   
	{0x88,0x40}, //raw_gf_fac_lowlight
	{0x5a,0xff},  //raw_rb_fac_outdoor
	{0x5b,0xd8},  //raw_rb_fac_normal
	{0x5c,0x78},  //raw_rb_fac_dummy
	{0x5d,0x00},  //raw_rb_fac_lowlight
	{0xfd,0x01}, 
	{0xe2,0x30}, //sharpen_y_base
	{0xe4,0xa0}, //sharpen_y_max
	{0xe5,0x04}, //rangek_neg_outdoor  //0x08
	{0xd3,0x04}, //rangek_pos_outdoor	//0x08
	{0xd7,0x04}, //range_base_outdoor	//0x08
	{0xe6,0x06}, //rangek_neg_normal
	{0xd4,0x06}, //rangek_pos_normal 
	{0xd8,0x06}, //range_base_normal  
	{0xe7,0x08}, //rangek_neg_dummy   // 0x10
	{0xd5,0x08}, //rangek_pos_dummy   // 0x10
	{0xd9,0x08}, //range_base_dummy    // 0x10
	{0xd2,0x10}, //rangek_neg_lowlight
	{0xd6,0x10}, //rangek_pos_lowlight
	{0xda,0x10}, //range_base_lowlight
	{0xe8,0x20},  //sharp_fac_pos_outdoor  // 0x35
	{0xec,0x30},  //sharp_fac_neg_outdoor
	{0xe9,0x20},  //sharp_fac_pos_nr	  // 0x35
	{0xed,0x28},//sharp_fac_neg_nr
	{0xea,0x20},//sharp_fac_pos_dummy
	{0xef,0x28},//sharp_fac_neg_dummy
	{0xeb,0x10},//sharp_fac_pos_low
	{0xf0,0x20},//sharp_fac_neg_low 
#endif
	//CCM
	  {0xfd,0x01},
	  {0xa0,0x80},
	  {0xa1,0x00},
	  {0xa2,0x00},
	  {0xa3,0xf3},  // 0xf6
	  {0xa4,0x8e},  // 0x99
	  {0xa5,0x00},  // 0xf2
	  {0xa6,0x00},  // 0x0d
	  {0xa7,0xe6},   // 0xda
	  {0xa8,0x9a},//0xa0 0813  // 0x98
	  {0xa9,0x00},
	  {0xaa,0x03},  // 0x33
	  {0xab,0x0c},
	  {0xfd,0x00},

			//gamma  
	  {0xfd,0x00},
	  {0x8b,0x0 },  // 00;0 ;0 
	  {0x8c,0xC },  // 0f;C ;11
	  {0x8d,0x19},  // 1e;19;19
	  {0x8e,0x2C},  // 3d;2C;28
	  {0x8f,0x49},  // 6c;49;46
	  {0x90,0x61},  // 92;61;61
	  {0x91,0x77},  // aa;77;78
	  {0x92,0x8A},  // b9;8A;8A
	  {0x93,0x9B},  // c4;9B;9B
	  {0x94,0xA9},  // cf;A9;A9
	  {0x95,0xB5},  // d4;B5;B5
	  {0x96,0xC0},  // da;C0;C0
	  {0x97,0xCA},  // e0;CA;CA
	  {0x98,0xD4},  // e4;D4;D4
	  {0x99,0xDD},  // e8;DD;DD
	  {0x9a,0xE6},  // ec;E6;E6
	  {0x9b,0xEF},  // f1;EF;EF
	  {0xfd,0x01},  // 01;01;01
	  {0x8d,0xF7},  // f7;F7;F7
	  {0x8e,0xFF},  // ff;FF;FF		 
	  {0xfd,0x00},  //

	   //rpc
	  {0xfd,0x00}, 
	  {0xe0,0x4c}, //  4c;44;4c;3e;3c;3a;38;rpc_1base_max
	  {0xe1,0x3c}, //  3c;36;3c;30;2e;2c;2a;rpc_2base_max
	  {0xe2,0x34}, //  34;2e;34;2a;28;26;26;rpc_3base_max
	  {0xe3,0x2e}, //  2e;2a;2e;26;24;22;rpc_4base_max
	  {0xe4,0x2e}, //  2e;2a;2e;26;24;22;rpc_5base_max
	  {0xe5,0x2c}, //  2c;28;2c;24;22;20;rpc_6base_max
	  {0xe6,0x2c}, //  2c;28;2c;24;22;20;rpc_7base_max
	  {0xe8,0x2a}, //  2a;26;2a;22;20;20;1e;rpc_8base_max
	  {0xe9,0x2a}, //  2a;26;2a;22;20;20;1e;rpc_9base_max 
	  {0xea,0x2a}, //  2a;26;2a;22;20;20;1e;rpc_10base_max
	  {0xeb,0x28}, //  28;24;28;20;1f;1e;1d;rpc_11base_max
	  {0xf5,0x28}, //  28;24;28;20;1f;1e;1d;rpc_12base_max
	  {0xf6,0x28}, //  28;24;28;20;1f;1e;1d;rpc_13base_max	

	//ae min gain  
	{0xfd,0x01},
	{0x94,0x80},//rpc_max_indr //0xb0
	{0x95,0x28},//1e//rpc_min_indr 
	{0x9c,0xa0},//rpc_max_outdr
	{0x9d,0x28},//rpc_min_outdr    

	//ae target
	  {0xfd,0x00}, 
	  {0xed,0x8c}, //80 
	  {0xf7,0x88}, //7c 
	  {0xf8,0x80}, //70 
	  {0xec,0x7c}, //6c  
	  
	  {0xef,0x74}, //99
	  {0xf9,0x70}, //90
	  {0xfa,0x68}, //80
	  {0xee,0x64}, //78

		
	//gray detect
	  {0xfd,0x01},
	  {0x30,0x40},
	  //add 0813 
	  {0x31,0x70},
	  {0x32,0x40},
	  {0x33,0xef},
	  {0x34,0x05},
	  {0x4d,0x2f},
	  {0x4e,0x20},
	  {0x4f,0x16},

	//lowlight lum
	  {0xfd,0x00}, //
	  {0xb2,0x20}, //lum_limit  // 0x10
	  {0xb3,0x1f}, //lum_set
	  {0xb4,0x30}, //black_vt  // 0x20
	  {0xb5,0x45}, //white_vt

	//saturation
	  {0xfd,0x00}, 
	  {0xbe,0xff}, 
	  {0xbf,0x01}, 
	  {0xc0,0xff}, 
	  {0xc1,0xd8}, 
	  {0xd3,0x88}, //0x78
	  {0xd4,0x78}, //0x78
	  {0xd6,0x70}, //0x78 	   (0xd7,0x60}, //0x78
	  {0xd7,0x60},

	//HEQ
	  {0xfd,0x00}, 
	  {0xdc,0x00}, 
	{0xdd,0x78}, //0x80 0813  // 0x80
	  {0xde,0xa8}, //80  0x88  0813
	  {0xdf,0x80}, 
	   
	//func enable
	  {0xfd,0x00},  
	  {0x32,0x15},  //0x0d
	  {0x34,0x76},  //16
	  {0x35,0x41},  
	  {0x33,0xef},  
	  {0x5f,0x51}, 
	  {0xff,0xff},
};

//load GT2005 parameters
void SP0A19_init_regs(struct sp0a19_device *dev)
{
    int i=0;//,j;
    unsigned char buf[2];
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
	while (1) {
		buf[0] = SP0A19_script[i].addr;
		buf[1] = SP0A19_script[i].val;
		if(SP0A19_script[i].val==0xff&&SP0A19_script[i].addr==0xff){
			printk("SP0A19_write_regs success in initial SP0A19.\n");
			break;
		}
		if((i2c_put_byte_add8(client,buf, 2)) < 0){
			printk("fail in initial SP0A19. \n");
			return;
		}
		i++;
	}
	return;

}


static struct aml_camera_i2c_fig1_s resolution_320x240_script[] = {
	{0xfd, 0x00},
	{0x47, 0x00},
	{0x48, 0x78},
	{0x49, 0x00},
	{0x4a, 0xf0},
	{0x4b, 0x00},
	{0x4c, 0xa0},
	{0x4d, 0x01},
	{0x4e, 0x40},                
	{0xff, 0xff}
};

static struct aml_camera_i2c_fig1_s resolution_640x480_script[] = {
	{0xfd, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x01},
	{0x4a, 0xe0},
	{0x4b, 0x00},
	{0x4c, 0x00},
	{0x4d, 0x02},
	{0x4e, 0x80},                
	{0xff, 0xff}
};


static int set_flip(struct sp0a19_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	unsigned char buf[2];
	temp = i2c_get_byte_add8(client, 0x31);
	temp &= 0xf3;
	temp |= dev->cam_info.m_flip << 5;
	temp |= dev->cam_info.v_flip << 6;
	buf[0] = 0x31;
	buf[1] = temp;
	if((i2c_put_byte_add8(client,buf, 2)) < 0) {
            printk("fail in setting sensor orientation\n");
            return -1;
        }
        return 0;
}


static void sp0a19_set_resolution(struct sp0a19_device *dev,int height,int width)
{
	int i=0;
    unsigned char buf[2];
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	struct aml_camera_i2c_fig1_s* resolution_script;
	if (width*height >= 640*480) {
		printk("set resolution 640X480\n");
		resolution_script = resolution_640x480_script;
		sp0a19_h_active = 640;
		sp0a19_v_active = 478; //480 
		sp0a19_frmintervals_active.denominator 	= 15;
		sp0a19_frmintervals_active.numerator	= 1;
		//SP0A19_init_regs(dev);
		//return;
	} else {
		printk("set resolution 320X240\n");
		sp0a19_h_active = 320;
		sp0a19_v_active = 238;
	    sp0a19_frmintervals_active.denominator 	= 15;
		sp0a19_frmintervals_active.numerator	= 1;
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
*	set_SP0A19_param_wb
*
* DESCRIPTION
*	SP0A19 wb setting.
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
void set_SP0A19_param_wb(struct sp0a19_device *dev,enum  camera_wb_flip_e para)
{
//	kal_uint16 rgain=0x80, ggain=0x80, bgain=0x80;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	unsigned char buf[4];

	unsigned char  temp_reg;
	//temp_reg=sp0a19_read_byte(0x22);
	//buf[0]=0x22; //SP0A19 enable auto wb
	buf[0]=0x32;
	temp_reg=i2c_get_byte_add8(client,buf[0]);

	printk(" camera set_SP0A19_param_wb=%d. \n ",para);
	switch (para)
	{
		case CAM_WB_AUTO:
			buf[0]=0xfd;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x28;
			buf[1]=0xc4;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x29;
			buf[1]=0x9e;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x32;
			buf[1]=0x15;  //temp_reg|0x10;    // SP0A19 AWB enable bit[1]   ie. 0x02;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_WB_CLOUD:
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x32;
			buf[1]=0x05;//temp_reg&~0x10;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x28;
			buf[1]=0xe0;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x29;
			buf[1]=0x80;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_WB_DAYLIGHT:   // tai yang guang
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x32;
			buf[1]=0x05;//temp_reg&~0x10;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x28;
			buf[1]=0xb0;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x29;
			buf[1]=0x70;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_WB_INCANDESCENCE:   // bai re guang
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x32;
			buf[1]=0x05;//temp_reg&~0x10;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x28;
			buf[1]=0x80;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x29;
			buf[1]=0xe0;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_WB_FLUORESCENT:   //ri guang deng
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x32;
			buf[1]=0x05;//temp_reg&~0x10;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x28;
			buf[1]=0x98;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x29;
			buf[1]=0xc0;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_WB_TUNGSTEN:   // wu si deng
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x32;
			buf[1]=0x05;//temp_reg&~0x10;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x28;
			buf[1]=0xaf;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x29;
			buf[1]=0x99;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_WB_MANUAL:
			// TODO
			break;
		default:
			break;
	}
//	kal_sleep_task(20);
}

/*************************************************************************
* FUNCTION
*	SP0A19_night_mode
*
* DESCRIPTION
*	This function night mode of SP0A19.
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
void SP0A19_night_mode(struct sp0a19_device *dev,enum  camera_night_mode_flip_e enable)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];

	unsigned char  temp_reg;
	//temp_reg=sp0a19_read_byte(0x22);
	buf[0]=0x32;
	temp_reg=i2c_get_byte_add8(client,buf[0]);
	temp_reg=0xff;

    if(enable)
    {

#if 0
		//caprure preview night 24M 50hz 20-6FPS maxgain:0x78		
		buf[0]=0xfd;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x05;
		buf[1]=0x0 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x06;
		buf[1]=0x0 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x09;
		buf[1]=0x1 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x0a;
		buf[1]=0x76;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf0;
		buf[1]=0x62;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf1;
		buf[1]=0x0 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf2;
		buf[1]=0x5f;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf5;
		buf[1]=0x78;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfd;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x00;
		buf[1]=0xc0;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x0f;
		buf[1]=0x60;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x16;
		buf[1]=0x60;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x17;
		buf[1]=0xa8;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x18;
		buf[1]=0xb0;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x1b;
		buf[1]=0x60;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x1c;
		buf[1]=0xb0;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xb4;
		buf[1]=0x20;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xb5;
		buf[1]=0x3a;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xb6;
		buf[1]=0x5e;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xb9;
		buf[1]=0x40;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xba;
		buf[1]=0x4f;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xbb;
		buf[1]=0x47;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xbc;
		buf[1]=0x45;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xbd;
		buf[1]=0x43;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xbe;
		buf[1]=0x42;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xbf;
		buf[1]=0x42;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc0;
		buf[1]=0x42;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc1;
		buf[1]=0x41;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc2;
		buf[1]=0x41;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc3;
		buf[1]=0x41;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc4;
		buf[1]=0x41;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc5;
		buf[1]=0x41;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc6;
		buf[1]=0x41;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xca;
		buf[1]=0x78;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xcb;
		buf[1]=0x10;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x14;
		buf[1]=0x20;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x15;
		buf[1]=0x1f;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfd;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	#endif
     }
    else
     {
     #if 0
              //caprure preview daylight 24M 50hz 20-8FPS maxgain:0x70
		buf[0]=0xfd;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x05;
		buf[1]=0x0 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x06;
		buf[1]=0x0 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x09;
		buf[1]=0x1 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x0a;
		buf[1]=0x76;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf0;
		buf[1]=0x62;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf1;
		buf[1]=0x0 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf2;
		buf[1]=0x5f;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf5;
		buf[1]=0x78;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfd;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x00;
		buf[1]=0xb2;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x0f;
		buf[1]=0x60;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x16;
		buf[1]=0x60;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x17;
		buf[1]=0xa2;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x18;
		buf[1]=0xaa;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x1b;
		buf[1]=0x60;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x1c;
		buf[1]=0xaa;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xb4;
		buf[1]=0x20;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xb5;
		buf[1]=0x3a;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xb6;
		buf[1]=0x5e;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xb9;
		buf[1]=0x40;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xba;
		buf[1]=0x4f;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xbb;
		buf[1]=0x47;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xbc;
		buf[1]=0x45;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xbd;
		buf[1]=0x43;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xbe;
		buf[1]=0x42;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xbf;
		buf[1]=0x42;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc0;
		buf[1]=0x42;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc1;
		buf[1]=0x41;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc2;
		buf[1]=0x41;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc3;
		buf[1]=0x41;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc4;
		buf[1]=0x41;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc5;
		buf[1]=0x70;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xc6;
		buf[1]=0x41;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xca;
		buf[1]=0x70;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xcb;
		buf[1]=0x0c;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x14;
		buf[1]=0x20;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x15;
		buf[1]=0x0f;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfd;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2); 
		#endif
	}

}
/*************************************************************************
* FUNCTION
*	SP0A19_night_mode
*
* DESCRIPTION
*	This function night mode of SP0A19.
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

void SP0A19_set_param_banding(struct sp0a19_device *dev,enum camera_banding_flip_e banding)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    unsigned char buf[4];
    switch(banding){
        case CAM_BANDING_60HZ:
                     #if 1
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x03;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x04;
			buf[1]=0xff;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x06;
			buf[1]=0x00 ;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x09;
			buf[1]=0x1 ;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x0a;
			buf[1]=0x46;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xf0;
			buf[1]=0x55;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xf1;
			buf[1]=0x0 ;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x90;
			buf[1]=0x14;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x92;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x98;
			buf[1]=0x55;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x99;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x9a;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x9b;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xce;
			buf[1]=0xa4;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xcf;
			buf[1]=0x06;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xd0;
			buf[1]=0xa4;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xd1;
			buf[1]=0x06;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x00;	
			i2c_put_byte_add8(client,buf,2);	
			
			 #else
	       //caprure preview daylight 24M 60hz 20-8FPS maxgain:0x70
		buf[0]=0xfd;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x03;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x04;
		buf[1]=0xff;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x06;
		buf[1]=0x31 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x09;
		buf[1]=0x1 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x0a;
		buf[1]=0x46;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf0;
		buf[1]=0x55;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf1;
		buf[1]=0x0 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfd;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x90;
		buf[1]=0x0f;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x92;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x98;
		buf[1]=0x55;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x99;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x9a;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x9b;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfd;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xce;
		buf[1]=0xfb;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xcf;
		buf[1]=0x04;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xd0;
		buf[1]=0xfb;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xd1;
		buf[1]=0x04;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfd;
		buf[1]=0x00;	
		i2c_put_byte_add8(client,buf,2);	
                     #endif
            break;
   case CAM_BANDING_50HZ:
              #if 1
			buf[0]=0xfd;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x03;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x04;
			buf[1]=0x32;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x06;
			buf[1]=0x00 ;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x09;
			buf[1]=0x1 ;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x0a;
			buf[1]=0x46;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xf0;
			buf[1]=0x66;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xf1;
			buf[1]=0x0 ;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x90;
			buf[1]=0x10;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x92;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x98;
			buf[1]=0x66;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x99;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x9a;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x9b;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xce;
			buf[1]=0x60;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xcf;
			buf[1]=0x06;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xd0;
			buf[1]=0x60;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xd1;
			buf[1]=0x06;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xfd;
			buf[1]=0x00;	
			i2c_put_byte_add8(client,buf,2);	
			
			#else
    //caprure preview daylight 24M 50hz 20-8FPS maxgain:0x70
		buf[0]=0xfd;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x03;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x04;
		buf[1]=0x32;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x06;
		buf[1]=0x31 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x09;
		buf[1]=0x1 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x0a;
		buf[1]=0x46;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf0;
		buf[1]=0x66;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xf1;
		buf[1]=0x0 ;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfd;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x90;
		buf[1]=0x0c;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x92;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x98;
		buf[1]=0x66;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x99;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x9a;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x9b;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfd;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xce;
		buf[1]=0xc8;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xcf;
		buf[1]=0x04;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xd0;
		buf[1]=0xc8;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xd1;
		buf[1]=0x04;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0xfd;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
                     #endif
            break;
		default:
			break;
    }
}


/*************************************************************************
* FUNCTION
*	set_SP0A19_param_exposure
*
* DESCRIPTION
*	SP0A19 exposure setting.
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
void set_SP0A19_param_exposure(struct sp0a19_device *dev,enum camera_exposure_e para)//曝光调节
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	unsigned char buf1[2];
	unsigned char buf2[2];

	switch (para)
	{
			case EXPOSURE_N4_STEP:
			buf1[0]=0xfd;
			buf1[1]=0x00;
			buf2[0]=0xdc;
			buf2[1]=0xc0;
			break;
		case EXPOSURE_N3_STEP:
			buf1[0]=0xfd;
			buf1[1]=0x00;
			buf2[0]=0xdc;
			buf2[1]=0xd0;
			break;
		case EXPOSURE_N2_STEP:
			buf1[0]=0xfd;
			buf1[1]=0x00;
			buf2[0]=0xdc;
			buf2[1]=0xe0;
			break;
		case EXPOSURE_N1_STEP:
			buf1[0]=0xfd;
			buf1[1]=0x00;
			buf2[0]=0xdc;
			buf2[1]=0xf0;
			break;
		case EXPOSURE_0_STEP:
			buf1[0]=0xfd;
			buf1[1]=0x00;
			buf2[0]=0xdc;
			buf2[1]=0x00;//6a
			break;
		case EXPOSURE_P1_STEP:
			buf1[0]=0xfd;
			buf1[1]=0x00;
			buf2[0]=0xdc;
			buf2[1]=0x10;
			break;
		case EXPOSURE_P2_STEP:
			buf1[0]=0xfd;
			buf1[1]=0x00;
			buf2[0]=0xdc;
			buf2[1]=0x20;
			break;
		case EXPOSURE_P3_STEP:
			buf1[0]=0xfd;
			buf1[1]=0x00;
			buf2[0]=0xdc;
			buf2[1]=0x30;
			break;
		case EXPOSURE_P4_STEP:
			buf1[0]=0xfd;
			buf1[1]=0x00;
			buf2[0]=0xdc;
			buf2[1]=0x40;
			break;
		default:
			buf1[0]=0xfd;
			buf1[1]=0x00;
			buf2[0]=0xdc;
			buf2[1]=0x00;
			break; 
	} 
	//msleep(300);  
	i2c_put_byte_add8(client,buf1,2);
	i2c_put_byte_add8(client,buf2,2);

}

/*************************************************************************
* FUNCTION
*	set_SP0A19_param_effect
*
* DESCRIPTION
*	SP0A19 effect setting.
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
void set_SP0A19_param_effect(struct sp0a19_device *dev,enum camera_effect_flip_e para)//特效设置
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL:
			buf[0]=0xfd;
		    buf[1]=0x00;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x62;
		    buf[1]=0x00;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x63;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x64;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);

			break;
		case CAM_EFFECT_ENC_GRAYSCALE:
			buf[0]=0xfd;
		    buf[1]=0x00;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x62;
		    buf[1]=0x20;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x63;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x64;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);

			break;
		case CAM_EFFECT_ENC_SEPIA:
			buf[0]=0xfd;
		    buf[1]=0x00;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x62;
		    buf[1]=0x10;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x63;
		    buf[1]=0xc0;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x64;
		    buf[1]=0x20;
		    i2c_put_byte_add8(client,buf,2);

			break;
		case CAM_EFFECT_ENC_COLORINV:
			buf[0]=0xfd;
		    buf[1]=0x00;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x62;
		    buf[1]=0x04;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x63;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x64;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);
			break;
		case CAM_EFFECT_ENC_SEPIAGREEN:
			buf[0]=0xfd;
		    buf[1]=0x00;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x62;
		    buf[1]=0x10;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x63;
		    buf[1]=0x20;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x64;
		    buf[1]=0x20;
		    i2c_put_byte_add8(client,buf,2);
			break;
			
		case CAM_EFFECT_ENC_SEPIABLUE:
			buf[0]=0xfd;
		    buf[1]=0x00;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x62;
		    buf[1]=0x10;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x63;
		    buf[1]=0x20;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x64;
		    buf[1]=0xf0;
		    i2c_put_byte_add8(client,buf,2);

			break;
		default:
			break;  
	}

}

unsigned char v4l_2_sp0a19(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int sp0a19_setting(struct sp0a19_device *dev,int PROP_ID,int value )
{
	int ret=0;
	//unsigned char cur_val;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_DO_WHITE_BALANCE:
		if(sp0a19_qctrl[0].default_value!=value){
			sp0a19_qctrl[0].default_value=value;
			set_SP0A19_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
		}
		break;
	case V4L2_CID_EXPOSURE:
		if(sp0a19_qctrl[1].default_value!=value){
			sp0a19_qctrl[1].default_value=value;
			set_SP0A19_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
		}
		break;
	case V4L2_CID_COLORFX:
		if(sp0a19_qctrl[2].default_value!=value){
			sp0a19_qctrl[2].default_value=value;
			set_SP0A19_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
		}
		break;
	case V4L2_CID_WHITENESS:
		if(sp0a19_qctrl[3].default_value!=value){
			sp0a19_qctrl[3].default_value=value;
			SP0A19_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		break;
	case V4L2_CID_BLUE_BALANCE:
		if(sp0a19_qctrl[4].default_value!=value){
			sp0a19_qctrl[4].default_value=value;
			SP0A19_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(sp0a19_qctrl[5].default_value!=value){
			sp0a19_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */         
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(sp0a19_qctrl[7].default_value!=value){
			sp0a19_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(sp0a19_qctrl[8].default_value!=value){
			sp0a19_qctrl[8].default_value=value;
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
static void power_down_sp0a19(struct sp0a19_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	buf[0]=0x1a;
	buf[1]=0x17;
	i2c_put_byte_add8(client,buf,2);
	buf[0]=0x25;
	buf[1]=0x00;
	i2c_put_byte_add8(client,buf,2);
	
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

static void sp0a19_fillbuff(struct sp0a19_fh *fh, struct sp0a19_buffer *buf)
{
	struct sp0a19_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	//para.mirror = sp0a19_qctrl[5].default_value&3;;// not set
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = sp0a19_qctrl[7].default_value;
	para.vaddr = (unsigned)vbuf;
	para.angle =sp0a19_qctrl[8].default_value;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void sp0a19_thread_tick(struct sp0a19_fh *fh)
{
	struct sp0a19_buffer *buf;
	struct sp0a19_device *dev = fh->dev;
	struct sp0a19_dmaqueue *dma_q = &dev->vidq;

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
			 struct sp0a19_buffer, vb.queue);
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
	sp0a19_fillbuff(fh, buf);
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

static void sp0a19_sleep(struct sp0a19_fh *fh)
{
	struct sp0a19_device *dev = fh->dev;
	struct sp0a19_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	sp0a19_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int sp0a19_thread(void *data)
{
	struct sp0a19_fh  *fh = data;
	struct sp0a19_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		sp0a19_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int sp0a19_start_thread(struct sp0a19_fh *fh)
{
	struct sp0a19_device *dev = fh->dev;
	struct sp0a19_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(sp0a19_thread, fh, "sp0a19");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void sp0a19_stop_thread(struct sp0a19_dmaqueue  *dma_q)
{
	struct sp0a19_device *dev = container_of(dma_q, struct sp0a19_device, vidq);

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
	struct sp0a19_fh  *fh = vq->priv_data;
	struct sp0a19_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct sp0a19_buffer *buf)
{
	struct sp0a19_fh  *fh = vq->priv_data;
	struct sp0a19_device *dev  = fh->dev;

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
	struct sp0a19_fh     *fh  = vq->priv_data;
	struct sp0a19_device    *dev = fh->dev;
	struct sp0a19_buffer *buf = container_of(vb, struct sp0a19_buffer, vb);
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
	struct sp0a19_buffer    *buf  = container_of(vb, struct sp0a19_buffer, vb);
	struct sp0a19_fh        *fh   = vq->priv_data;
	struct sp0a19_device       *dev  = fh->dev;
	struct sp0a19_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct sp0a19_buffer   *buf  = container_of(vb, struct sp0a19_buffer, vb);
	struct sp0a19_fh       *fh   = vq->priv_data;
	struct sp0a19_device      *dev  = (struct sp0a19_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops sp0a19_video_qops = {
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
	struct sp0a19_fh  *fh  = priv;
	struct sp0a19_device *dev = fh->dev;

	strcpy(cap->driver, "sp0a19");
	strcpy(cap->card, "sp0a19");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = SP0A19_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct sp0a19_fmt *fmt;

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

    if(fival->index > ARRAY_SIZE(sp0a19_frmivalenum))
        return -EINVAL;

    for(k =0; k< ARRAY_SIZE(sp0a19_frmivalenum); k++)
    {
        if( (fival->index==sp0a19_frmivalenum[k].index)&&
                (fival->pixel_format ==sp0a19_frmivalenum[k].pixel_format )&&
                (fival->width=sp0a19_frmivalenum[k].width)&&
                (fival->height==sp0a19_frmivalenum[k].height)){
            memcpy( fival, &sp0a19_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
            return 0;
        }
    }

    return -EINVAL;

}
static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct sp0a19_fh *fh = priv;

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
	struct sp0a19_fh  *fh  = priv;
	struct sp0a19_device *dev = fh->dev;
	struct sp0a19_fmt *fmt;
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
	struct sp0a19_fh *fh = priv;
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
		sp0a19_set_resolution(fh->dev,fh->height,fh->width);
	} else {
		sp0a19_set_resolution(fh->dev,fh->height,fh->width);
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
    struct sp0a19_fh *fh = priv;
    struct sp0a19_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = sp0a19_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}
static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct sp0a19_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sp0a19_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sp0a19_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sp0a19_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct sp0a19_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct sp0a19_fh  *fh = priv;
    vdin_parm_t para;
    int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;
    memset( &para, 0, sizeof( para ));
	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = sp0a19_frmintervals_active.denominator;
	para.h_active = sp0a19_h_active;
	para.v_active = sp0a19_v_active;
	para.hsync_phase = 0;
	para.vsync_phase = 1;
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
	para.skip_count =  2; //skip_num
	printk("0a19,h=%d, v=%d, frame_rate=%d\n", sp0a19_h_active, sp0a19_v_active, sp0a19_frmintervals_active.denominator);
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
	    vops->start_tvin_service(0,&para);
	    fh->stream_on        = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct sp0a19_fh  *fh = priv;

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
	struct sp0a19_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(sp0a19_prev_resolution))
			return -EINVAL;
		frmsize = &sp0a19_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(sp0a19_pic_resolution))
			return -EINVAL;
		frmsize = &sp0a19_pic_resolution[fsize->index];
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
	struct sp0a19_fh *fh = priv;
	struct sp0a19_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct sp0a19_fh *fh = priv;
	struct sp0a19_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(sp0a19_qctrl); i++)
		if (qc->id && qc->id == sp0a19_qctrl[i].id) {
			memcpy(qc, &(sp0a19_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct sp0a19_fh *fh = priv;
	struct sp0a19_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(sp0a19_qctrl); i++)
		if (ctrl->id == sp0a19_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct sp0a19_fh *fh = priv;
	struct sp0a19_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(sp0a19_qctrl); i++)
		if (ctrl->id == sp0a19_qctrl[i].id) {
			if (ctrl->value < sp0a19_qctrl[i].minimum ||
			    ctrl->value > sp0a19_qctrl[i].maximum ||
			    sp0a19_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int sp0a19_open(struct file *file)
{
	struct sp0a19_device *dev = video_drvdata(file);
	struct sp0a19_fh *fh = NULL;
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
	
	SP0A19_init_regs(dev);
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

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &sp0a19_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct sp0a19_buffer), fh,NULL);

	sp0a19_start_thread(fh);

	return 0;
}

static ssize_t
sp0a19_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct sp0a19_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
sp0a19_poll(struct file *file, struct poll_table_struct *wait)
{
	struct sp0a19_fh        *fh = file->private_data;
	struct sp0a19_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int sp0a19_close(struct file *file)
{
	struct sp0a19_fh         *fh = file->private_data;
	struct sp0a19_device *dev       = fh->dev;
	struct sp0a19_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);
	sp0a19_have_open=0;

	sp0a19_stop_thread(vidq);
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
	sp0a19_qctrl[0].default_value=0;
	sp0a19_qctrl[1].default_value=4;
	sp0a19_qctrl[2].default_value=0;
	sp0a19_qctrl[3].default_value=0;
	sp0a19_qctrl[4].default_value=0;

	sp0a19_frmintervals_active.numerator = 1;
	sp0a19_frmintervals_active.denominator = 15;
	//power_down_sp0a19(dev);
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

static int sp0a19_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sp0a19_fh  *fh = file->private_data;
	struct sp0a19_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations sp0a19_fops = {
	.owner		= THIS_MODULE,
	.open           = sp0a19_open,
	.release        = sp0a19_close,
	.read           = sp0a19_read,
	.poll		= sp0a19_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = sp0a19_mmap,
};

static const struct v4l2_ioctl_ops sp0a19_ioctl_ops = {
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

static struct video_device sp0a19_template = {
	.name		= "sp0a19_v4l",
	.fops           = &sp0a19_fops,
	.ioctl_ops 	= &sp0a19_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int sp0a19_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_SP0A19, 0); 
	//return v4l2_chip_ident_i2c_client(client, chip,  V4L2_IDENT_GT2005, 0); 
}

static const struct v4l2_subdev_core_ops sp0a19_core_ops = {
	.g_chip_ident = sp0a19_g_chip_ident,
};

static const struct v4l2_subdev_ops sp0a19_ops = {
	.core = &sp0a19_core_ops,
};

static int sp0a19_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	aml_cam_info_t* plat_dat;
	int err;
	struct sp0a19_device *t;
	struct v4l2_subdev *sd;
    vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &sp0a19_ops);

	plat_dat = (aml_cam_info_t*)client->dev.platform_data;
	
	/* Now create a video4linux device */
	mutex_init(&t->mutex);

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &sp0a19_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);

	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "sp0a19");
	
	/* Register it */
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if (plat_dat->front_back >=0)  
			video_nr = plat_dat->front_back;
	} else {
		printk("camera sp0a19: have no platform data\n");
		kfree(t);
		kfree(client);
		return -1;
	}
	
	t->cam_info.version = SP0A19_DRIVER_VERSION;
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

static int sp0a19_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sp0a19_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id sp0a19_id[] = {
	{ "sp0a19", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sp0a19_id);

static struct i2c_driver sp0a19_i2c_driver = {
	.driver = {
		.name = "sp0a19",
	},
	.probe = sp0a19_probe,
	.remove = sp0a19_remove,
	.id_table = sp0a19_id,
};

module_i2c_driver(sp0a19_i2c_driver);

