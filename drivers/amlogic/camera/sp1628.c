/*
 *sp1628 - This code emulates a real video device with v4l2 api
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

#define SP1628_CAMERA_MODULE_NAME "sp1628"
#define P1_Ae_Target_0xeb         0x78
#define P1_Ae_Target_0xec         0x78
typedef enum{
	DCAMERA_FLICKER_50HZ = 0,
	DCAMERA_FLICKER_60HZ,
	FLICKER_MAX
}DCAMERA_FLICKER;
static unsigned short Antiflicker = DCAMERA_FLICKER_50HZ;

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define SP1628_CAMERA_MAJOR_VERSION 0
#define SP1628_CAMERA_MINOR_VERSION 7
#define SP1628_CAMERA_RELEASE 0
#define SP1628_CAMERA_VERSION \
	KERNEL_VERSION(SP1628_CAMERA_MAJOR_VERSION, SP1628_CAMERA_MINOR_VERSION, SP1628_CAMERA_RELEASE)

MODULE_DESCRIPTION("sp1628 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define SP1628_DRIVER_VERSION "SP1628-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");
static int sp1628_night_or_normal = 0;	//add by sp_yjp,20120905
static int sp1628_h_active=640;
static int sp1628_v_active=480;
static struct v4l2_fract sp1628_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

static struct vdin_v4l2_ops_s *vops;

/* supported controls */
static struct v4l2_queryctrl sp1628_qctrl[] = {
	{
		.id            = V4L2_CID_DO_WHITE_BALANCE,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "white balance",
		.minimum       = CAM_WB_AUTO,
		.maximum       = CAM_WB_WARM_FLUORESCENT,
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

static struct v4l2_frmivalenum sp1628_frmivalenum[]={
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
        },
};

struct v4l2_querymenu sp1628_qmenu_wbmode[] = {
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

struct v4l2_querymenu sp1628_qmenu_anti_banding_mode[] = {
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
    struct v4l2_querymenu* sp1628_qmenu;
}sp1628_qmenu_set_t;

sp1628_qmenu_set_t sp1628_qmenu_set[] = {
    {
        .id         	= V4L2_CID_DO_WHITE_BALANCE,
        .num            = ARRAY_SIZE(sp1628_qmenu_wbmode),
        .sp1628_qmenu   = sp1628_qmenu_wbmode,
    },{
        .id         	= V4L2_CID_POWER_LINE_FREQUENCY,
        .num            = ARRAY_SIZE(sp1628_qmenu_anti_banding_mode),
        .sp1628_qmenu   = sp1628_qmenu_anti_banding_mode,
    },
};

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(sp1628_qmenu_set); i++)
	if (a->id && a->id == sp1628_qmenu_set[i].id) {
	    for(j = 0; j < sp1628_qmenu_set[i].num; j++)
		if (a->index == sp1628_qmenu_set[i].sp1628_qmenu[j].index) {
			memcpy(a, &( sp1628_qmenu_set[i].sp1628_qmenu[j]),
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

struct sp1628_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct sp1628_fmt formats[] = {
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

static struct sp1628_fmt *get_format(struct v4l2_format *f)
{
	struct sp1628_fmt *fmt;
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
struct sp1628_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct sp1628_fmt        *fmt;
};

struct sp1628_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(sp1628_devicelist);

struct sp1628_device {
	struct list_head			sp1628_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct sp1628_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t  cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(sp1628_qctrl)];
};

static inline struct sp1628_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sp1628_device, sd);
}

struct sp1628_fh {
	struct sp1628_device            *dev;

	/* video capture */
	struct sp1628_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};


static struct v4l2_frmsize_discrete sp1628_prev_resolution[]= //should include 352x288 and 640x480, those two size are used for recording
{
	{320,240},
	{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete sp1628_pic_resolution[]=
{
	{1280,960},
};


/* ------------------------------------------------------------------
	reg spec of SP1628
   ------------------------------------------------------------------*/

#if 1

struct aml_camera_i2c_fig1_s SP1628_script[] ={
  {0xfd,0x00},//
  {0x1b,0x60},//
  {0x2f,0x10},//20//;24M*2=48M//20 3pll
  {0x1c,0x10},//10
  {0x30,0x00},//;00		
  {0x0c,0x66},//;analog
  {0x0d,0x12},//
  {0x13,0x1d},//
  {0x6b,0x20},//
  {0x6d,0x20},//
  {0x6f,0x21},//
  {0x73,0x22},//
  {0x7a,0x20},//
  {0x15,0x30},// 
  {0x71,0x32},//
  {0x76,0x34},//  
  {0x29,0x08},//
  {0x18,0x01},//
  {0x19,0x10},//
  {0x1a,0xc3},//;c1
  {0x1b,0x6f},//
  {0x1d,0x11},//;01
  {0x1e,0x00},//;1e
  {0x1f,0xa0},//80
  {0x20,0x7f},//
  {0x22,0x3c},//;1b
  {0x25,0xff},//
  {0x2b,0x88},//
  {0x2c,0x85},//
  {0x2d,0x00},//
  {0x2e,0x80},//
  {0x27,0x38},//
  {0x28,0x03},// 
  {0x70,0x40},//
  {0x72,0x40},//    
  {0x74,0x38},//    
  {0x75,0x38},//
  {0x77,0x38},//  
  {0x7f,0x40},//       
  //{0x31,0x30},//70//		;mirror/flip 960
  {0xfd,0x01},//
  {0x5d,0x11},//		;position
  {0x5f,0x00},//		;延长
  {0xfb,0x25},//		;blacklevl
  {0x48,0x00},//		;dp
  {0x49,0x99},// 
  {0xf2,0x0a},//		;同SP1628 0xf4     
  {0xfd,0x02},//;AE
  {0x52,0x34},//
  {0x53,0x02},//		;测试是否ae抖
  {0x54,0x0c},//
  {0x55,0x08},// 
  {0x86,0x0c},//		;其中满足条件帧数
  {0x87,0x10},//		;检测总帧数
  {0x8b,0x10},// 

  

#if 0
//capture preview daylight 24M 3pll 50hz 17.6-9FPS vga 
{0xfd,0x00},
{0x03,0x04},
{0x04,0x38},
{0x05,0x00},
{0x06,0x01},
{0x09,0x01},
{0x0a,0xde},
{0xfd,0x01},
{0xf0,0x00},
{0xf7,0xb4},
{0xf8,0x96},
{0x02,0x0b},
{0x03,0x01},
{0x06,0xb4},
{0x07,0x00},
{0x08,0x01},
{0x09,0x00},
{0xfd,0x02},
{0x40,0x0d},
{0x41,0x96},
{0x42,0x00},
{0x88,0xd8},
{0x89,0x69},
{0x8a,0x32},
{0xfd,0x02},
{0xbe,0xbc},
{0xbf,0x07},
{0xd0,0xbc},
{0xd1,0x07},
{0xfd,0x01},
{0x5b,0x07},
{0x5c,0xbc},
{0xfd,0x00},
#elif 0
  {0xfd,0x00},
  {0x03,0x04},
  {0x04,0x44},
  {0x05,0x00},
  {0x06,0x01},
  {0x09,0x01},
  {0x0a,0xd3},
  {0xfd,0x01},
  {0xf0,0x00},
  {0xf7,0xb6},
  {0xf8,0x98},
  {0x02,0x0b},
  {0x03,0x01},
  {0x06,0xb6},
  {0x07,0x00},
  {0x08,0x01},
  {0x09,0x00},
  {0xfd,0x02},
  {0x40,0x0d},
  {0x41,0x98},
  {0x42,0x00},
  {0x88,0xd0},
  {0x89,0x5e},
  {0x8a,0x32},
  {0xfd,0x02},
  {0xbe,0xd2},
  {0xbf,0x07},
  {0xd0,0xd2},
  {0xd1,0x07},
  {0xfd,0x01},
  {0x5b,0x07},
  {0x5c,0xd2},
  {0xfd,0x00},
 #else
 //capture preview daylight 24M 2pll 50hz 17-9FPS vga 
  {0xfd,0x00},
  {0x03,0x04},
  {0x04,0x08},
  {0x05,0x00},
  {0x06,0x01},
  {0x09,0x00},
  {0x0a,0xaf},
  {0xfd,0x01},
  {0xf0,0x00},
  {0xf7,0xac},
  {0xf8,0x90},
  {0x02,0x0b},
  {0x03,0x01},
  {0x06,0xac},
  {0x07,0x00},
  {0x08,0x01},
  {0x09,0x00},
  {0xfd,0x02},
  {0x40,0x0d},
  {0x41,0x90},
  {0x42,0x00},
  {0x88,0xfa},
  {0x89,0x8e},
  {0x8a,0x32},
  {0xfd,0x02},
  {0xbe,0x64},
  {0xbf,0x07},
  {0xd0,0x64},
  {0xd1,0x07},
  {0xfd,0x01},
  {0x5b,0x07},
  {0x5c,0x64},
  {0xfd,0x00},
 #endif

  {0xfd,0x01},//;fix status
  {0x5a,0x38},//		;DP_gain
  {0xfd,0x02},//
  {0xba,0x30},//		;mean_dummy_low
  {0xbb,0x50},//		;mean_low_dummy
  {0xbc,0xc0},//		;rpc_heq_low
  {0xbd,0xa0},//		;rpc_heq_dummy
  {0xb8,0x80},//		;mean_nr_dummy
  {0xb9,0x90},//		;mean_dummy_nr
  
  {0xfd,0x01},//;rpc
  {0xe0,0x54},//;6c 
  {0xe1,0x40},//;54 
  {0xe2,0x38},//;48 
  {0xe3,0x34},//;40
  {0xe4,0x34},//;40
  {0xe5,0x30},//;3e
  {0xe6,0x30},//;3e
  {0xe7,0x2e},//;3a
  {0xe8,0x2e},//;3a
  {0xe9,0x2e},//;3a
  {0xea,0x2c},//;38
  {0xf3,0x2c},//;38
  {0xf4,0x2c},//;38
  {0xfd,0x01},//;ae min gain 
  {0x04,0xc0},//		;rpc_max_indr
  {0x05,0x2c},//;38		;1e;rpc_min_indr 
  {0x0a,0xc0},//		;rpc_max_outdr
  {0x0b,0x2c},//;38		;rpc_min_outdr 
  {0xfd,0x01},//;ae target
  {0xeb,0x78},//		 
  {0xec,0x78},//		
  {0xed,0x05},//
  {0xee,0x0a},//
  {0xfd,0x01},//		;lsc
  {0x26,0x30},//
  {0x27,0xdc},//
  {0x28,0x05},//
  {0x29,0x08},//
  {0x2a,0x00},//
  {0x2b,0x03},//
  {0x2c,0x00},//
  {0x2d,0x2f},//
  {0xfd,0x01},//		;RGain
  {0xa1,0x46},//;48		;left
  {0xa2,0x50},//;58		;right
  {0xa3,0x50},//;58		;up
  {0xa4,0x47},//;50		;down
  {0xad,0x08},//;08		;lu
  {0xae,0x0a},//;10		;ru
  {0xaf,0x0a},//;10		;ld
  {0xb0,0x06},//;10		;rd
  {0x18,0x00},//;40		;left  
  {0x19,0x00},//;50		;right 
  {0x1a,0x00},//;32		;up    
  {0x1b,0x00},//;30		;down  
  {0xbf,0x00},//;a5		;lu    
  {0xc0,0x00},//;a0		;ru    
  {0xc1,0x00},//;08		;ld    
  {0xfa,0x00},//;00		;rd   
  {0xa5,0x30},//;38	;GGain
  {0xa6,0x36},//;48
  {0xa7,0x30},//;48
  {0xa8,0x38},//;40
  {0xb1,0x00},//;00
  {0xb2,0x00},//;00
  {0xb3,0x00},//;00
  {0xb4,0x00},//;00
  {0x1c,0x00},//;28
  {0x1d,0x00},//;40
  {0x1e,0x00},//;2c
  {0xb9,0x00},//;25 
  {0x21,0x00},//;b0
  {0x22,0x00},//;a0
  {0x23,0x00},//;50
  {0x24,0x00},//;0d  
  {0xa9,0x31},//;38		;BGain
  {0xaa,0x36},//;48
  {0xab,0x30},//;46
  {0xac,0x38},//;46
  {0xb5,0x00},//;08
  {0xb6,0x00},//;08
  {0xb7,0x04},//;08
  {0xb8,0x02},//;08
  {0xba,0x00},//;12
  {0xbc,0x00},//;30
  {0xbd,0x00},//;31
  {0xbe,0x00},//;1e
  {0x25,0x00},//;a0
  {0x45,0x00},//;a0
  {0x46,0x00},//;12
  {0x47,0x00},//;09    
  {0xfd,0x01},//		;awb
  {0x32,0x15},//
  {0xfd,0x02},//
  {0x26,0xc9},//
  {0x27,0x8b},//
  {0x1b,0x80},//
  {0x1a,0x80},//
  {0x18,0x27},//
  {0x19,0x26},//
  {0x2a,0x01},//
  {0x2b,0x10},//
  {0x28,0xf8},//		;0xa0
  {0x29,0x08},// ;d65 88
  {0x66,0x38},//;35		;0x48
  {0x67,0x54},//;60		;0x69
  {0x68,0xba},//;b0		;c8;0xb5;0xaa
  {0x69,0xce},//;e0		;f4;0xda;0xed
  {0x6a,0xa5},//;indoor 89
  {0x7c,0x17},//;43
  {0x7d,0x40},//
  {0x7e,0xe6},//
  {0x7f,0x13},//
  {0x80,0xa6},//;cwf   8a
  {0x70,0x15},//;2f		;0x3b
  {0x71,0x38},//;4a		;0x55
  {0x72,0x08},//		;0x28
  {0x73,0x20},//;24		;0x45
  {0x74,0xaa},//;tl84  8b
  {0x6b,0x08},//;18
  {0x6c,0x24},//;34		;0x25;0x2f
  {0x6d,0x12},//;17		;0x35
  {0x6e,0x32},//		;0x52
  {0x6f,0xaa},//;f    8c
  {0x61,0xf0},//;e0;10		;04;0xf4;0xed
  {0x62,0x1a},//;38		;22;0x14;0f
  {0x63,0x1c},//		;30;0x5d
  {0x64,0x3a},//		;55;0x75;0x8f
  {0x65,0x6a},//		;0x6a
  {0x75,0x80},//
  {0x76,0x09},//
  {0x77,0x02},//
  {0x24,0x25},//
  {0x0e,0x16},//
  {0x3b,0x09},//
  {0xfd,0x02},//		; sharp
  {0xde,0x0f},//
  {0xd2,0x0c},//		;控制黑白边；0-边粗，f-变细
  {0xd3,0x0c},//0a
  {0xd4,0x0a},//08
  {0xd5,0x08},//
  {0xd7,0x0a},//	10	;轮廓判断
  {0xd8,0x14},//1d
  {0xd9,0x28},//32
  {0xda,0x34},//
  {0xdb,0x08},//
  {0xe8,0x58},//	38	;轮廓强度
  {0xe9,0x54},//38
  {0xea,0x3c},//
  {0xeb,0x26},//
  {0xec,0x74},//
  {0xed,0x5c},//
  {0xee,0x3e},//
  {0xef,0x26},//
  {0xf3,0x00},//		;平坦区域锐化力度
  {0xf4,0x00},//
  {0xf5,0x00},//
  {0xf6,0x00},//
  {0xfd,0x02},//		;skin sharpen
  {0xdc,0x04},//		;肤色降锐化
  {0x05,0x6f},//		;排除肤色降锐化对分辨率卡引起的干扰
  {0x09,0x10},//		;肤色排除白点区域
  {0xfd,0x01},//		;dns
  {0x64,0x22},//		;沿方向边缘平滑力度  ;0-最强，8-最弱
  {0x65,0x22},//		
  {0x86,0x20},//		;沿方向边缘平滑阈值，越小越弱
  {0x87,0x20},//		
  {0x88,0x20},//		
  {0x89,0x20},//		
  {0x6d,0x0f},//		;强平滑（平坦）区域平滑阈值
  {0x6e,0x0f},//		
  {0x6f,0x10},//		
  {0x70,0x10},//		
  {0x71,0x0d},//		;弱轮廓（非平坦）区域平滑阈值	
  {0x72,0x23},//		
  {0x73,0x2a},//		
  {0x74,0x2f},//		
  {0x75,0x46},//		;[7:4]平坦区域强度，[3:0]非平坦区域强度；0-最强，8-最弱；
  {0x76,0x36},//		
  {0x77,0x25},//		
  {0x78,0x12},//		
  {0x81,0x1d},//		;2x;根据增益判定区域阈值
  {0x82,0x2b},//		;4x
  {0x83,0xff},//		;8x
  {0x84,0xff},//		;16x
  {0x85,0x0a},//		; 12/8+reg0x81 第二阈值，在平坦和非平坦区域做连接
  {0xfd,0x01},//		;gamma  
  {0x8b,0x00},//;00;00;00;     
  {0x8c,0x10},//;02;0b;0b;     
  {0x8d,0x20},//;0a;19;17;     
  {0x8e,0x31},//;13;2a;27;     
  {0x8f,0x3f},//;1d;37;35;     
  {0x90,0x53},//;30;4b;51;     
  {0x91,0x64},//;40;5e;64;     
  {0x92,0x74},//;4e;6c;74;     
  {0x93,0x80},//;5a;78;80;     
  {0x94,0x92},//;71;92;92;     
  {0x95,0xa2},//;85;a6;a2;     
  {0x96,0xaf},//;96;b5;af;     
  {0x97,0xbb},//;a6;bf;bb;     
  {0x98,0xc6},//;b3;ca;c6;     
  {0x99,0xd0},//;c0;d2;d0;     
  {0x9a,0xd9},//;cb;d9;d9;     
  {0x9b,0xe0},//;d5;e1;e0;     
  {0x9c,0xe8},//;df;e8;e8;     
  {0x9d,0xee},//;e9;ee;ee;     
  {0x9e,0xf4},//;f2;f4;f4;     
  {0x9f,0xfa},//;fa;fa;fa;     
  {0xa0,0xff},//;ff;ff;ff;     
  {0xfd,0x02},//		;CCM
  {0x15,0xac},//		;b>th a4
  {0x16,0x90},//		;r<th 87
  {0xa0,0x80},//;99;a6;a6;8c;80; 非F
  {0xa1,0x00},//;0c;da;da;da;fa;00;
  {0xa2,0x00},//;da;00;00;00;fa;00;
  {0xa3,0x00},//;00;e7;e7;da;da;e7;
  {0xa4,0x80},//;99;c0;c0;c0;c0;a6;
  {0xa5,0x00},//;e7;da;da;e7;e7;f4;
  {0xa6,0x00},//;00;00;00;00;00;00;
  {0xa7,0xe7},//;e7;b4;b4;a7;cd;da;
  {0xa8,0x99},//;99;cc;d9;b3;a6;
  {0xa9,0x00},//;30;0c;0c;0c;3c;00;
  {0xaa,0x00},//;30;33;33;33;33;33;
  {0xab,0x0c},//;0c;0c;0c;0c;0c;0c;
  {0xac,0x99},//;80;a2;b3;8c;F
  {0xad,0x26},//;00;04;0c;0c;
  {0xae,0xc0},//;00;da;c0;e7;
  {0xaf,0xed},//;e7;cd;cd;b4;
  {0xb0,0xcc},//;c0;d9;e6;e6;
  {0xb1,0xcd},//;da;da;cd;e7;
  {0xb2,0xed},//;e7;f6;e7;e7;
  {0xb3,0xda},//;b4;98;9a;9a;
  {0xb4,0xb9},//;e6;f3;00;00;
  {0xb5,0x30},//;00;30;30;30;
  {0xb6,0x33},//;33;33;33;33;
  {0xb7,0x0f},//;0f;0f;1f;1f; 
  {0xfd,0x01},//		;sat u 
  {0xd3,0x98},//	过标准105%
  {0xd4,0x98},//	
  {0xd5,0x80},//		
  {0xd6,0x70},//		
  {0xd7,0x98},// ;sat v 
  {0xd8,0x98},//	
  {0xd9,0x80},//		
  {0xda,0x70},//		
  {0xfd,0x01},//		;auto_sat
  {0xd2,0x00},//		;autosa_en
  {0xfd,0x01},//		;uv_th	
  {0xc2,0xee},//   ;白色物体表面有彩色噪声降低此值  
  {0xc3,0xee},//
  {0xc4,0xdd},//
  {0xc5,0xbb},//
  {0xfd,0x01},//		;low_lum_offset
  {0xcd,0x10},//
  {0xce,0x1f},//
  {0xfd,0x02},//		;gw
  {0x35,0x6f},//
  {0x37,0x13},//
  {0xfd,0x01},//		;heq
  {0xdb,0x00},//  
  {0x10,0x00},// 
  {0x14,0x15},//  
  {0x11,0x00},//
  {0x15,0x10},//
  {0x16,0x10},// 
  {0xfd,0x02},//		;cnr 找张国华解释  
  {0x8e,0x10},// 
  {0x90,0x20},//
  {0x91,0x20},//
  {0x92,0x60},//
  {0x93,0x80},//
  {0xfd,0x02},//		;auto 
  {0x85,0x00},//	;12 enable 50Hz/60Hz function
  {0xfd,0x01},// 
  {0x00,0x00},// 	;fix mode   
  {0x32,0x15},//;		;ae en
  {0x33,0xef},//		;lsc\bpc en
  {0x34,0xc7},//		;ynr\cnr\gamma\color en
  {0x35,0x41},//	40	;YUYV
  {0xfd,0x00},		  
 #if 1
 	//set VGA
	{0xfd,0x00},
	{0x19,0x17},
	{0x30,0x00}, //pclk/2
	{0x31,0x54},//74
#endif //end vga set	

#if 0
   //mipi
	{0x94,0x80}, 
	{0x95,0x02},
	{0x96,0xe0},//  0x2d0=720P
	{0x97,0x01},//	  0x3c0=960
#endif

	{0xfd,0x01},
	{0xfb,0x25},


	
	{0xe7,0x03},
	{0xe7,0x00},
	{0xff,0xff},
} ;


void SP1628_init_regs(struct sp1628_device *dev)
{
	int i=0;//,j;
	unsigned char buf[2];
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
	while (1) {
		buf[0] = SP1628_script[i].addr;
		buf[1] = SP1628_script[i].val;
		if(SP1628_script[i].val==0xff&&SP1628_script[i].addr==0xff){
			printk("SP1628_write_regs success in initial SP1628.\n");
			break;
		}
		if((i2c_put_byte_add8(client,buf, 2)) < 0){
			printk("fail in initial SP1628. \n");
			return;
		}
		i++;
	}
	return;
}

#endif

#if 0
static int set_flip(struct sp1628_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	unsigned char buf[2];
	temp = i2c_get_byte_add8(client, 0x31);
	temp &= 0x9f;
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
#endif

static void sp1628_set_resolution(struct sp1628_device *dev,int height,int width)
{
	//int i=0;
	//unsigned char buf[2];
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	if (width*height >= 640*480) {
		printk("set resolution 1280X960\n");
		
		i2c_put_byte_add8_new(client,0xfd,0x00);
		i2c_put_byte_add8_new(client,0x19,0x10);//1280*960		
		i2c_put_byte_add8_new(client,0x30,0x00);
		i2c_put_byte_add8_new(client,0x31,0x50);
		
		/*i2c_put_byte_add8_new(client,0xfd,0x01);
		i2c_put_byte_add8_new(client,0x4a,0x00);
		i2c_put_byte_add8_new(client,0x4b,0x03);
		i2c_put_byte_add8_new(client,0x4c,0xc0);
		i2c_put_byte_add8_new(client,0x4d,0x00);
		i2c_put_byte_add8_new(client,0x4e,0x05);
		i2c_put_byte_add8_new(client,0x4f,0x00);
		i2c_put_byte_add8_new(client,0xfd,0x00);
		i2c_put_byte_add8_new(client,0x19,0x10);
		i2c_put_byte_add8_new(client,0x30,0x00);
		i2c_put_byte_add8_new(client,0x31,0x10);
		i2c_put_byte_add8_new(client,0xfd,0x00);
		mdelay(100);*/
		/*while (1) {
			buf[0] = resolution_script[i].addr;
			buf[1] = resolution_script[i].val;
			if(resolution_script[i].val==0xff&&resolution_script[i].addr==0xff){
				break;
			}
			if((i2c_put_byte_add8(client,buf, 2)) < 0){
				printk("fail in set resolution. \n");
				return;
			}
			i++;
		}
		mdelay(100);*/
		sp1628_h_active = 1280;
		sp1628_v_active = 958;
		sp1628_frmintervals_active.denominator 	= 15;
		sp1628_frmintervals_active.numerator	= 1;
		/*
		i2c_put_byte_add8_new(client,0xfd,0x00);
		i2c_put_byte_add8_new(client,0x19,0x17);	//640*480	
		i2c_put_byte_add8_new(client,0x30,0x00);
		i2c_put_byte_add8_new(client,0x31,0x54);*/
		
		/*i2c_put_byte_add8_new(client,0xfd,0x00);
		i2c_put_byte_add8_new(client,0x19,0x10);//1280*960		
		i2c_put_byte_add8_new(client,0x30,0x00);
		i2c_put_byte_add8_new(client,0x31,0x50);*/
		
	} else {
		printk("set resolution 320X240\n");
		sp1628_h_active = 320;
		sp1628_v_active = 240;
		sp1628_frmintervals_active.denominator 	= 15;
		sp1628_frmintervals_active.numerator	= 1;
	}
    //set_flip(dev);
}
/*************************************************************************
* FUNCTION
*	set_SP1628_param_wb
*
* DESCRIPTION
*	SP1628 wb setting.
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
void set_SP1628_param_wb(struct sp1628_device *dev,enum  camera_wb_flip_e para)
	{
		struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
		switch (para)
		{
	
			case CAM_WB_AUTO://auto
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x32,0x05);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x03);
				
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x26,0xc9);
				i2c_put_byte_add8_new(client,0x27,0x8b);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x00);
				
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x32,0x15);
				break;
	
			case CAM_WB_CLOUD: //cloud
	
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x32,0x05);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x03);
				
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x26,0xdb);
				i2c_put_byte_add8_new(client,0x27,0x63);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x00);
				break;
	
			case CAM_WB_DAYLIGHT: //
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x32,0x05);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x03);
				
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x26,0xca);
				i2c_put_byte_add8_new(client,0x27,0x73);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x00);
				break;
	
			case CAM_WB_INCANDESCENCE:
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x32,0x05);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x03);
				
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x26,0x8c);
				i2c_put_byte_add8_new(client,0x27,0xb3);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x00);
				break;
	
			case CAM_WB_TUNGSTEN:
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x32,0x05);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x03);
				
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x26,0x90);
				i2c_put_byte_add8_new(client,0x27,0xa5);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x00);
				break;
	
			case CAM_WB_FLUORESCENT:
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x32,0x05);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x03);
				
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x26,0x95);
				i2c_put_byte_add8_new(client,0x27,0x9c);
				
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0xe7,0x00);
				break;
	
			case CAM_WB_MANUAL:
			default:
					// TODO
				break;
		}
	
	
	}


/*************************************************************************
* FUNCTION
*	SP1628_night_mode
*
* DESCRIPTION
*	This function night mode of SP1628.
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
void SP1628_night_mode(struct sp1628_device *dev,enum  camera_night_mode_flip_e enable)
	{
		struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);	
		
		if (enable)
		{
			sp1628_night_or_normal = 1; //=1,night mode; =0,normal mode //add by sp_yjp,20120905
	
			if(Antiflicker== DCAMERA_FLICKER_50HZ)
			{
				//i2c_put_byte_add8_new(client,0xfd,0x00);	//disable AE,add by sp_yjp,20120905
				//i2c_put_byte_add8_new(client,0x32,0x08);
				printk("night mode 50hz\r\n");
				#if 0					   
					//capture preview night  24M 3pll 50hz 17.6-6FPS vga
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0x03,0x04);
				i2c_put_byte_add8_new(client,0x04,0x44);
				i2c_put_byte_add8_new(client,0x05,0x00);
				i2c_put_byte_add8_new(client,0x06,0x01);
				i2c_put_byte_add8_new(client,0x09,0x01);
				i2c_put_byte_add8_new(client,0x0a,0xd3);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0xf0,0x00);
				i2c_put_byte_add8_new(client,0xf7,0xb6);
				i2c_put_byte_add8_new(client,0xf8,0x98);
				i2c_put_byte_add8_new(client,0x02,0x0b);
				i2c_put_byte_add8_new(client,0x03,0x01);
				i2c_put_byte_add8_new(client,0x06,0xb6);
				i2c_put_byte_add8_new(client,0x07,0x00);
				i2c_put_byte_add8_new(client,0x08,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x40,0x0d);
				i2c_put_byte_add8_new(client,0x41,0x98);
				i2c_put_byte_add8_new(client,0x42,0x00);
				i2c_put_byte_add8_new(client,0x88,0xd0);
				i2c_put_byte_add8_new(client,0x89,0x5e);
				i2c_put_byte_add8_new(client,0x8a,0x32);
				i2c_put_byte_add8_new(client,0xfd,0x02);//Status
				i2c_put_byte_add8_new(client,0xbe,0xd2);
				i2c_put_byte_add8_new(client,0xbf,0x07);
				i2c_put_byte_add8_new(client,0xd0,0xd2);
				i2c_put_byte_add8_new(client,0xd1,0x07);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x5b,0x07);
				i2c_put_byte_add8_new(client,0x5c,0xd2);
				i2c_put_byte_add8_new(client,0xfd,0x00);
				#else
				//capture preview night  24M 2pll 50hz 17-6FPS vga
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0x03,0x04);
				i2c_put_byte_add8_new(client,0x04,0x08);
				i2c_put_byte_add8_new(client,0x05,0x00);
				i2c_put_byte_add8_new(client,0x06,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0x0a,0xaf);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0xf0,0x00);
				i2c_put_byte_add8_new(client,0xf7,0xac);
				i2c_put_byte_add8_new(client,0xf8,0x90);
				i2c_put_byte_add8_new(client,0x02,0x10);
				i2c_put_byte_add8_new(client,0x03,0x01);
				i2c_put_byte_add8_new(client,0x06,0xac);
				i2c_put_byte_add8_new(client,0x07,0x00);
				i2c_put_byte_add8_new(client,0x08,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x40,0x14);
				i2c_put_byte_add8_new(client,0x41,0x90);
				i2c_put_byte_add8_new(client,0x42,0x00);
				i2c_put_byte_add8_new(client,0x88,0xfa);
				i2c_put_byte_add8_new(client,0x89,0x8e);
				i2c_put_byte_add8_new(client,0x8a,0x32);
				i2c_put_byte_add8_new(client,0xfd,0x02);//Status
				i2c_put_byte_add8_new(client,0xbe,0xc0);
				i2c_put_byte_add8_new(client,0xbf,0x0a);
				i2c_put_byte_add8_new(client,0xd0,0xc0);
				i2c_put_byte_add8_new(client,0xd1,0x0a);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x5b,0x0a);
				i2c_put_byte_add8_new(client,0x5c,0xc0);
				i2c_put_byte_add8_new(client,0xfd,0x00);
				
				#endif
				
				i2c_put_byte_add8_new(client,0xe7,0x03);	//add by sp_yjp,20120905
				i2c_put_byte_add8_new(client,0xe7,0x00);			
	
			}
			else
			{
				//i2c_put_byte_add8_new(client,,0xfd,0x00);	//disable AE,add by sp_yjp,20120905
				//i2c_put_byte_add8_new(client,,0x32,0x08);
				printk("night mode 60hz\r\n");
	            #if 0
				 //capture preview night  24M 3pll 60hz 17.6-6FPS vga
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0x03,0x03);
				i2c_put_byte_add8_new(client,0x04,0x84);
				i2c_put_byte_add8_new(client,0x05,0x00);
				i2c_put_byte_add8_new(client,0x06,0x01);
				i2c_put_byte_add8_new(client,0x09,0x01);
				i2c_put_byte_add8_new(client,0x0a,0xde);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0xf0,0x00);
				i2c_put_byte_add8_new(client,0xf7,0x96);
				i2c_put_byte_add8_new(client,0xf8,0x96);
				i2c_put_byte_add8_new(client,0x02,0x14);
				i2c_put_byte_add8_new(client,0x03,0x01);
				i2c_put_byte_add8_new(client,0x06,0x96);
				i2c_put_byte_add8_new(client,0x07,0x00);
				i2c_put_byte_add8_new(client,0x08,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x40,0x14);
				i2c_put_byte_add8_new(client,0x41,0x96);
				i2c_put_byte_add8_new(client,0x42,0x00);
				i2c_put_byte_add8_new(client,0x88,0x69);
				i2c_put_byte_add8_new(client,0x89,0x69);
				i2c_put_byte_add8_new(client,0x8a,0x33);  
				i2c_put_byte_add8_new(client,0xfd,0x02);//Status
				i2c_put_byte_add8_new(client,0xbe,0xb8);
				i2c_put_byte_add8_new(client,0xbf,0x0b);
				i2c_put_byte_add8_new(client,0xd0,0xb8);
				i2c_put_byte_add8_new(client,0xd1,0x0b);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x5b,0x0b);
				i2c_put_byte_add8_new(client,0x5c,0xb8);
				i2c_put_byte_add8_new(client,0xfd,0x00);
	            #else
			    //capture preview night  24M 2pll 60hz 17-6FPS vga
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0x03,0x03);
				i2c_put_byte_add8_new(client,0x04,0x60);
				i2c_put_byte_add8_new(client,0x05,0x00);
				i2c_put_byte_add8_new(client,0x06,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0x0a,0xac);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0xf0,0x00);
				i2c_put_byte_add8_new(client,0xf7,0x90);
				i2c_put_byte_add8_new(client,0xf8,0x90);
				i2c_put_byte_add8_new(client,0x02,0x14);
				i2c_put_byte_add8_new(client,0x03,0x01);
				i2c_put_byte_add8_new(client,0x06,0x90);
				i2c_put_byte_add8_new(client,0x07,0x00);
				i2c_put_byte_add8_new(client,0x08,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x40,0x14);
				i2c_put_byte_add8_new(client,0x41,0x90);
				i2c_put_byte_add8_new(client,0x42,0x00);
				i2c_put_byte_add8_new(client,0x88,0x8e);
				i2c_put_byte_add8_new(client,0x89,0x8e);
				i2c_put_byte_add8_new(client,0x8a,0x33);  
				i2c_put_byte_add8_new(client,0xfd,0x02);//Status
				i2c_put_byte_add8_new(client,0xbe,0x40);
				i2c_put_byte_add8_new(client,0xbf,0x0b);
				i2c_put_byte_add8_new(client,0xd0,0x40);
				i2c_put_byte_add8_new(client,0xd1,0x0b);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x5b,0x0b);
				i2c_put_byte_add8_new(client,0x5c,0x40);
				i2c_put_byte_add8_new(client,0xfd,0x00);
				#endif
				i2c_put_byte_add8_new(client,0xe7,0x03);	//add by sp_yjp,20120905
				i2c_put_byte_add8_new(client,0xe7,0x00);
	
			}
		}
		else
		{
			//i2c_put_byte_add8_new(client,); //Camera Enable night mode  1/5 Frame rate //zyy test
			sp1628_night_or_normal = 0; //=1,night mode; =0,normal mode //add by sp_yjp,20120905
	
	
			if(Antiflicker== DCAMERA_FLICKER_50HZ)
			{
				//i2c_put_byte_add8_new(client,0xfd,0x00);	//disable AE,add by sp_yjp,20120905
				//i2c_put_byte_add8_new(client,0x32,0x08);
				printk("normal mode 50hz\r\n"); 
	            #if 0
			   //capture preview daylight 24M 3pll 50hz 17.6-9FPS vga
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0x03,0x04);
				i2c_put_byte_add8_new(client,0x04,0x38);
				i2c_put_byte_add8_new(client,0x05,0x00);
				i2c_put_byte_add8_new(client,0x06,0x01);
				i2c_put_byte_add8_new(client,0x09,0x01);
				i2c_put_byte_add8_new(client,0x0a,0xde);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0xf0,0x00);
				i2c_put_byte_add8_new(client,0xf7,0xb4);
				i2c_put_byte_add8_new(client,0xf8,0x96);
				i2c_put_byte_add8_new(client,0x02,0x0b);
				i2c_put_byte_add8_new(client,0x03,0x01);
				i2c_put_byte_add8_new(client,0x06,0xb4);
				i2c_put_byte_add8_new(client,0x07,0x00);
				i2c_put_byte_add8_new(client,0x08,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x40,0x0d);
				i2c_put_byte_add8_new(client,0x41,0x96);
				i2c_put_byte_add8_new(client,0x42,0x00);
				i2c_put_byte_add8_new(client,0x88,0xd8);
				i2c_put_byte_add8_new(client,0x89,0x69);
				i2c_put_byte_add8_new(client,0x8a,0x32);
				i2c_put_byte_add8_new(client,0xfd,0x02);//Status
				i2c_put_byte_add8_new(client,0xbe,0xbc);
				i2c_put_byte_add8_new(client,0xbf,0x07);
				i2c_put_byte_add8_new(client,0xd0,0xbc);
				i2c_put_byte_add8_new(client,0xd1,0x07);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x5b,0x07);
				i2c_put_byte_add8_new(client,0x5c,0xbc);
				i2c_put_byte_add8_new(client,0xfd,0x00); 		
				#else
				//capture preview daylight 24M 2pll 50hz 17-9FPS vga
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0x03,0x04);
				i2c_put_byte_add8_new(client,0x04,0x08);
				i2c_put_byte_add8_new(client,0x05,0x00);
				i2c_put_byte_add8_new(client,0x06,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0x0a,0xaf);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0xf0,0x00);
				i2c_put_byte_add8_new(client,0xf7,0xac);
				i2c_put_byte_add8_new(client,0xf8,0x90);
				i2c_put_byte_add8_new(client,0x02,0x0b);
				i2c_put_byte_add8_new(client,0x03,0x01);
				i2c_put_byte_add8_new(client,0x06,0xac);
				i2c_put_byte_add8_new(client,0x07,0x00);
				i2c_put_byte_add8_new(client,0x08,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x40,0x0d);
				i2c_put_byte_add8_new(client,0x41,0x90);
				i2c_put_byte_add8_new(client,0x42,0x00);
				i2c_put_byte_add8_new(client,0x88,0xfa);
				i2c_put_byte_add8_new(client,0x89,0x8e);
				i2c_put_byte_add8_new(client,0x8a,0x32);
				i2c_put_byte_add8_new(client,0xfd,0x02);//Status
				i2c_put_byte_add8_new(client,0xbe,0x64);
				i2c_put_byte_add8_new(client,0xbf,0x07);
				i2c_put_byte_add8_new(client,0xd0,0x64);
				i2c_put_byte_add8_new(client,0xd1,0x07);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x5b,0x07);
				i2c_put_byte_add8_new(client,0x5c,0x64);
				i2c_put_byte_add8_new(client,0xfd,0x00); 	
				#endif
	
				i2c_put_byte_add8_new(client,0xe7,0x03);	//add by sp_yjp,20120905
				i2c_put_byte_add8_new(client,0xe7,0x00);
			}
			else
			{
				//i2c_put_byte_add8_new(client,0xfd,0x00);	//disable AE,add by sp_yjp,20120905
				//i2c_put_byte_add8_new(client,0x32,0x08);
				printk("normal mode 60hz\r\n"); 
			    #if 0
				//capture preview daylight 24M 3pll 60hz 17.6-9FPS vga
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0x03,0x03);
				i2c_put_byte_add8_new(client,0x04,0x84);
				i2c_put_byte_add8_new(client,0x05,0x00);
				i2c_put_byte_add8_new(client,0x06,0x01);
				i2c_put_byte_add8_new(client,0x09,0x01);
				i2c_put_byte_add8_new(client,0x0a,0xde);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0xf0,0x00);
				i2c_put_byte_add8_new(client,0xf7,0x96);
				i2c_put_byte_add8_new(client,0xf8,0x96);
				i2c_put_byte_add8_new(client,0x02,0x0d);
				i2c_put_byte_add8_new(client,0x03,0x01);
				i2c_put_byte_add8_new(client,0x06,0x96);
				i2c_put_byte_add8_new(client,0x07,0x00);
				i2c_put_byte_add8_new(client,0x08,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x40,0x0d);
				i2c_put_byte_add8_new(client,0x41,0x96);
				i2c_put_byte_add8_new(client,0x42,0x00);
				i2c_put_byte_add8_new(client,0x88,0x69);
				i2c_put_byte_add8_new(client,0x89,0x69);
				i2c_put_byte_add8_new(client,0x8a,0x33);
				i2c_put_byte_add8_new(client,0xfd,0x02);//Status
				i2c_put_byte_add8_new(client,0xbe,0x9e);
				i2c_put_byte_add8_new(client,0xbf,0x07);
				i2c_put_byte_add8_new(client,0xd0,0x9e);
				i2c_put_byte_add8_new(client,0xd1,0x07);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x5b,0x07);
				i2c_put_byte_add8_new(client,0x5c,0x9e);
				i2c_put_byte_add8_new(client,0xfd,0x00);
				#else
				//capture preview daylight 24M 3pll 60hz 17.6-9FPS vga
				i2c_put_byte_add8_new(client,0xfd,0x00);
				i2c_put_byte_add8_new(client,0x03,0x03);
				i2c_put_byte_add8_new(client,0x04,0x60);
				i2c_put_byte_add8_new(client,0x05,0x00);
				i2c_put_byte_add8_new(client,0x06,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0x0a,0xac);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0xf0,0x00);
				i2c_put_byte_add8_new(client,0xf7,0x90);
				i2c_put_byte_add8_new(client,0xf8,0x90);
				i2c_put_byte_add8_new(client,0x02,0x0d);
				i2c_put_byte_add8_new(client,0x03,0x01);
				i2c_put_byte_add8_new(client,0x06,0x90);
				i2c_put_byte_add8_new(client,0x07,0x00);
				i2c_put_byte_add8_new(client,0x08,0x01);
				i2c_put_byte_add8_new(client,0x09,0x00);
				i2c_put_byte_add8_new(client,0xfd,0x02);
				i2c_put_byte_add8_new(client,0x40,0x0d);
				i2c_put_byte_add8_new(client,0x41,0x90);
				i2c_put_byte_add8_new(client,0x42,0x00);
				i2c_put_byte_add8_new(client,0x88,0x8e);
				i2c_put_byte_add8_new(client,0x89,0x8e);
				i2c_put_byte_add8_new(client,0x8a,0x33);
				i2c_put_byte_add8_new(client,0xfd,0x02);//Status
				i2c_put_byte_add8_new(client,0xbe,0x50);
				i2c_put_byte_add8_new(client,0xbf,0x07);
				i2c_put_byte_add8_new(client,0xd0,0x50);
				i2c_put_byte_add8_new(client,0xd1,0x07);
				i2c_put_byte_add8_new(client,0xfd,0x01);
				i2c_put_byte_add8_new(client,0x5b,0x07);
				i2c_put_byte_add8_new(client,0x5c,0x50);
				i2c_put_byte_add8_new(client,0xfd,0x00); 
				#endif
	
				i2c_put_byte_add8_new(client,0xe7,0x03);	//add by sp_yjp,20120905
				i2c_put_byte_add8_new(client,0xe7,0x00);
			}
		//i2c_put_byte_add8_new(client,); //Disable night mode	1/2 Frame rate
		}
		//i2c_put_byte_add8_new(client,0xfd,0x00);	//enable AE,add by sp_yjp,20120905
		//i2c_put_byte_add8_new(client,0x32,0x0d);
	}

/*************************************************************************
* FUNCTION
*	SP1628_night_mode
*
* DESCRIPTION
*	This function night mode of SP1628.
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

void SP1628_set_param_banding(struct sp1628_device *dev,enum  camera_banding_flip_e banding)
	{
		//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
		//unsigned char buf[4];
		
		  switch(banding)
		
		 {
			 case CAM_BANDING_50HZ: 		
		
			Antiflicker = DCAMERA_FLICKER_50HZ;
		
			printk( " set_SP1628_anti_flicker  50hz \r\n" );
		
			break;
		
			 case CAM_BANDING_60HZ:
		
			Antiflicker = DCAMERA_FLICKER_60HZ;
		
			printk( " set_SP1628_anti_flicker  60hz \r\n" );
		
			break;
		
			default:
		
				 break;
		
		 }
		
	}



/*************************************************************************
* FUNCTION
*	set_SP1628_param_exposure
*
* DESCRIPTION
*	SP1628 exposure setting.
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
void set_SP1628_param_exposure(struct sp1628_device *dev,enum camera_exposure_e para)//曝光调节
	{
		struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
	
		switch (para)
		{
	
			case EXPOSURE_N4_STEP:
				i2c_put_byte_add8_new(client,0xfd , 0x01);
				i2c_put_byte_add8_new(client,0xeb , P1_Ae_Target_0xeb-0x40);
				i2c_put_byte_add8_new(client,0xec , P1_Ae_Target_0xec-0x40);
				break;
	
	
	
			case EXPOSURE_N3_STEP:
				i2c_put_byte_add8_new(client,0xfd , 0x01);
				i2c_put_byte_add8_new(client,0xeb , P1_Ae_Target_0xeb-0x30);
				i2c_put_byte_add8_new(client,0xec , P1_Ae_Target_0xec-0x30);
				break;
	
	
			case EXPOSURE_N2_STEP:
				i2c_put_byte_add8_new(client,0xfd , 0x01);
				i2c_put_byte_add8_new(client,0xeb , P1_Ae_Target_0xeb-0x20);
				i2c_put_byte_add8_new(client,0xec , P1_Ae_Target_0xec-0x20);
				break;
	
	
			case EXPOSURE_N1_STEP:
				i2c_put_byte_add8_new(client,0xfd , 0x01);
				i2c_put_byte_add8_new(client,0xeb , P1_Ae_Target_0xeb-0x10);
				i2c_put_byte_add8_new(client,0xec , P1_Ae_Target_0xec-0x10);
				break;
				
			case EXPOSURE_0_STEP:
				i2c_put_byte_add8_new(client,0xfd , 0x01);
				i2c_put_byte_add8_new(client,0xeb , P1_Ae_Target_0xeb);//-40
				i2c_put_byte_add8_new(client,0xec , P1_Ae_Target_0xec);//-40
				break;
				
			case EXPOSURE_P1_STEP:
				i2c_put_byte_add8_new(client,0xfd , 0x01);
				i2c_put_byte_add8_new(client,0xeb , P1_Ae_Target_0xeb+0x10);
				i2c_put_byte_add8_new(client,0xec , P1_Ae_Target_0xec+0x10);
				break;
				
			case EXPOSURE_P2_STEP:
				i2c_put_byte_add8_new(client,0xfd , 0x01);
				i2c_put_byte_add8_new(client,0xeb , P1_Ae_Target_0xeb+0x20);
				i2c_put_byte_add8_new(client,0xec , P1_Ae_Target_0xec+0x20);
				break;
	
			case EXPOSURE_P3_STEP:
				i2c_put_byte_add8_new(client,0xfd , 0x01);
				i2c_put_byte_add8_new(client,0xeb , P1_Ae_Target_0xeb+0x30);
				i2c_put_byte_add8_new(client,0xec , P1_Ae_Target_0xec+0x30);
				break;
						
			case EXPOSURE_P4_STEP:	
				i2c_put_byte_add8_new(client,0xfd , 0x01);
				i2c_put_byte_add8_new(client,0xeb , P1_Ae_Target_0xeb+0x40);
				i2c_put_byte_add8_new(client,0xec , P1_Ae_Target_0xec+0x40);
				break;
	
			default:
				i2c_put_byte_add8_new(client,0xfd , 0x01);
				i2c_put_byte_add8_new(client,0xeb , P1_Ae_Target_0xeb);//
				i2c_put_byte_add8_new(client,0xec , P1_Ae_Target_0xec);//
				break;
					//break;
	
	
	
		}
	
	
	}


/*************************************************************************
* FUNCTION
*	set_SP1628_param_effect
*
* DESCRIPTION
*	SP1628 effect setting.
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
void set_SP1628_param_effect(struct sp1628_device *dev,enum camera_effect_flip_e para)//特效设置
	{
		struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
	
		switch (para)
		{
			case CAM_EFFECT_ENC_NORMAL:
				i2c_put_byte_add8_new(client,0xfd, 0x01);
				i2c_put_byte_add8_new(client,0x66, 0x00);
				i2c_put_byte_add8_new(client,0x67, 0x80);
				i2c_put_byte_add8_new(client,0x68, 0x80);
				i2c_put_byte_add8_new(client,0xdb, 0x00);
				i2c_put_byte_add8_new(client,0x34, 0xc7);
				i2c_put_byte_add8_new(client,0xfd, 0x02);
				i2c_put_byte_add8_new(client,0x14, 0x00);
				break;
	
			case CAM_EFFECT_ENC_GRAYSCALE:
				i2c_put_byte_add8_new(client,0xfd, 0x01);
				i2c_put_byte_add8_new(client,0x66, 0x20);
				i2c_put_byte_add8_new(client,0x67, 0x80);
				i2c_put_byte_add8_new(client,0x68, 0x80);
				i2c_put_byte_add8_new(client,0xdb, 0x00);
				i2c_put_byte_add8_new(client,0x34, 0xc7);
				i2c_put_byte_add8_new(client,0xfd, 0x02);
				i2c_put_byte_add8_new(client,0x14, 0x00);
				break;
	
			case CAM_EFFECT_ENC_SEPIA:
				i2c_put_byte_add8_new(client,0xfd, 0x01);
				i2c_put_byte_add8_new(client,0x66, 0x10);
				i2c_put_byte_add8_new(client,0x67, 0x98);
				i2c_put_byte_add8_new(client,0x68, 0x58);
				i2c_put_byte_add8_new(client,0xdb, 0x00);
				i2c_put_byte_add8_new(client,0x34, 0xc7);
				i2c_put_byte_add8_new(client,0xfd, 0x02);
				i2c_put_byte_add8_new(client,0x14, 0x00);
				break;
	
			case CAM_EFFECT_ENC_SEPIAGREEN:
				i2c_put_byte_add8_new(client,0xfd, 0x01);
				i2c_put_byte_add8_new(client,0x66, 0x10);
				i2c_put_byte_add8_new(client,0x67, 0x50);
				i2c_put_byte_add8_new(client,0x68, 0x50);
				i2c_put_byte_add8_new(client,0xdb, 0x00);
				i2c_put_byte_add8_new(client,0x34, 0xc7);
				i2c_put_byte_add8_new(client,0xfd, 0x02);
				i2c_put_byte_add8_new(client,0x14, 0x00);
				break;
	
			case CAM_EFFECT_ENC_SEPIABLUE:
				i2c_put_byte_add8_new(client,0xfd, 0x01);
				i2c_put_byte_add8_new(client,0x66, 0x10);
				i2c_put_byte_add8_new(client,0x67, 0x80);
				i2c_put_byte_add8_new(client,0x68, 0xb0);
				i2c_put_byte_add8_new(client,0xdb, 0x00);
				i2c_put_byte_add8_new(client,0x34, 0xc7);
				i2c_put_byte_add8_new(client,0xfd, 0x02);
				i2c_put_byte_add8_new(client,0x14, 0x00);
				break;
	
			case CAM_EFFECT_ENC_COLORINV:
	
				break;
	
			default:
				break;
		}
	
	
	
	}


unsigned char v4l_2_sp1628(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int sp1628_setting(struct sp1628_device *dev,int PROP_ID,int value )
{
	int ret=0;
	switch(PROP_ID)  {
#if 0
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_sp1628(value));
		ret=i2c_put_byte(client,0x0201,v4l_2_sp1628(value));
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
		if(sp1628_qctrl[0].default_value!=value){
			sp1628_qctrl[0].default_value=value;
			set_SP1628_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
		}
		break;
	case V4L2_CID_EXPOSURE:
		if(sp1628_qctrl[1].default_value!=value){
			sp1628_qctrl[1].default_value=value;
			set_SP1628_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
		}
		break;
	case V4L2_CID_COLORFX:
		if(sp1628_qctrl[2].default_value!=value){
			sp1628_qctrl[2].default_value=value;
			set_SP1628_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
		}
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		if(sp1628_qctrl[3].default_value!=value){
			sp1628_qctrl[3].default_value=value;
			SP1628_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		break;
	case V4L2_CID_BLUE_BALANCE:
		if(sp1628_qctrl[4].default_value!=value){
			sp1628_qctrl[4].default_value=value;
			SP1628_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(sp1628_qctrl[5].default_value!=value){
			sp1628_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */         
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(sp1628_qctrl[7].default_value!=value){
			sp1628_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(sp1628_qctrl[8].default_value!=value){
			sp1628_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;

}

/*static void power_down_sp1628(struct sp1628_device *dev)
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
}*/

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void sp1628_fillbuff(struct sp1628_fh *fh, struct sp1628_buffer *buf)
{
	struct sp1628_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = sp1628_qctrl[5].default_value&3;// not set
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = sp1628_qctrl[7].default_value;
	para.vaddr = (unsigned)vbuf;
	para.angle = sp1628_qctrl[8].default_value;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void sp1628_thread_tick(struct sp1628_fh *fh)
{
	struct sp1628_buffer *buf;
	struct sp1628_device *dev = fh->dev;
	struct sp1628_dmaqueue *dma_q = &dev->vidq;

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
			 struct sp1628_buffer, vb.queue);
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
	sp1628_fillbuff(fh, buf);
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

static void sp1628_sleep(struct sp1628_fh *fh)
{
	struct sp1628_device *dev = fh->dev;
	struct sp1628_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	sp1628_thread_tick(fh);

	schedule_timeout_interruptible(2);//if fps > 25 , 2->1

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int sp1628_thread(void *data)
{
	struct sp1628_fh  *fh = data;
	struct sp1628_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		sp1628_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int sp1628_start_thread(struct sp1628_fh *fh)
{
	struct sp1628_device *dev = fh->dev;
	struct sp1628_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(sp1628_thread, fh, "sp1628");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void sp1628_stop_thread(struct sp1628_dmaqueue  *dma_q)
{
	struct sp1628_device *dev = container_of(dma_q, struct sp1628_device, vidq);

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
	struct sp1628_fh  *fh = vq->priv_data;
	struct sp1628_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct sp1628_buffer *buf)
{
	struct sp1628_fh  *fh = vq->priv_data;
	struct sp1628_device *dev  = fh->dev;

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
	struct sp1628_fh     *fh  = vq->priv_data;
	struct sp1628_device    *dev = fh->dev;
	struct sp1628_buffer *buf = container_of(vb, struct sp1628_buffer, vb);
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
	struct sp1628_buffer    *buf  = container_of(vb, struct sp1628_buffer, vb);
	struct sp1628_fh        *fh   = vq->priv_data;
	struct sp1628_device       *dev  = fh->dev;
	struct sp1628_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct sp1628_buffer   *buf  = container_of(vb, struct sp1628_buffer, vb);
	struct sp1628_fh       *fh   = vq->priv_data;
	struct sp1628_device      *dev  = (struct sp1628_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops sp1628_video_qops = {
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
	struct sp1628_fh  *fh  = priv;
	struct sp1628_device *dev = fh->dev;

	strcpy(cap->driver, "sp1628");
	strcpy(cap->card, "sp1628");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = SP1628_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct sp1628_fmt *fmt;

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

        if(fival->index > ARRAY_SIZE(sp1628_frmivalenum))
                return -EINVAL;

        for(k =0; k< ARRAY_SIZE(sp1628_frmivalenum); k++)
        {
                if( (fival->index==sp1628_frmivalenum[k].index)&&
                                (fival->pixel_format ==sp1628_frmivalenum[k].pixel_format )&&
                                (fival->width==sp1628_frmivalenum[k].width)&&
                                (fival->height==sp1628_frmivalenum[k].height)){
                        memcpy( fival, &sp1628_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
                        return 0;
                }
        }

        return -EINVAL;

}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct sp1628_fh *fh = priv;

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
	struct sp1628_fh  *fh  = priv;
	struct sp1628_device *dev = fh->dev;
	struct sp1628_fmt *fmt;
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
	struct sp1628_fh *fh = priv;
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
		sp1628_set_resolution(fh->dev,fh->height,fh->width);
	} else {
		sp1628_set_resolution(fh->dev,fh->height,fh->width);
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
        struct sp1628_fh *fh = priv;
        struct sp1628_device *dev = fh->dev;
        struct v4l2_captureparm *cp = &parms->parm.capture;

        dprintk(dev,3,"vidioc_g_parm\n");
        if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
                return -EINVAL;

        memset(cp, 0, sizeof(struct v4l2_captureparm));
        cp->capability = V4L2_CAP_TIMEPERFRAME;

        cp->timeperframe = sp1628_frmintervals_active;
        printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
                        cp->timeperframe.numerator );
        return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct sp1628_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sp1628_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sp1628_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sp1628_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct sp1628_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct sp1628_fh  *fh = priv;
	vdin_parm_t para;
	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para ));
	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = sp1628_frmintervals_active.denominator;
	para.h_active = sp1628_h_active;
	para.v_active = sp1628_v_active;
	para.hsync_phase = 0;
	para.vsync_phase = 1;
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
	para.skip_count =  2; //skip_num
	printk("0308,h=%d, v=%d, frame_rate=%d\n",
		sp1628_h_active, sp1628_v_active, sp1628_frmintervals_active.denominator);
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
	    vops->start_tvin_service(0,&para);
	    fh->stream_on        = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct sp1628_fh  *fh = priv;

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
	struct sp1628_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(sp1628_prev_resolution))
			return -EINVAL;
		frmsize = &sp1628_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(sp1628_pic_resolution))
			return -EINVAL;
		frmsize = &sp1628_pic_resolution[fsize->index];
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
	struct sp1628_fh *fh = priv;
	struct sp1628_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct sp1628_fh *fh = priv;
	struct sp1628_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(sp1628_qctrl); i++)
		if (qc->id && qc->id == sp1628_qctrl[i].id) {
			memcpy(qc, &(sp1628_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct sp1628_fh *fh = priv;
	struct sp1628_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(sp1628_qctrl); i++)
		if (ctrl->id == sp1628_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct sp1628_fh *fh = priv;
	struct sp1628_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(sp1628_qctrl); i++)
		if (ctrl->id == sp1628_qctrl[i].id) {
			if (ctrl->value < sp1628_qctrl[i].minimum ||
			    ctrl->value > sp1628_qctrl[i].maximum ||
			    sp1628_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int sp1628_open(struct file *file)
{
	struct sp1628_device *dev = video_drvdata(file);
	struct sp1628_fh *fh = NULL;
	int retval = 0;
#if CONFIG_CMA
    retval = vm_init_buf(24*SZ_1M);
    if(retval <0)
        return -1;
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif	
	aml_cam_init(&dev->cam_info);	
	
	SP1628_init_regs(dev);
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

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &sp1628_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct sp1628_buffer), fh,NULL);

	sp1628_start_thread(fh);

	return 0;
}

static ssize_t
sp1628_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct sp1628_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
sp1628_poll(struct file *file, struct poll_table_struct *wait)
{
	struct sp1628_fh        *fh = file->private_data;
	struct sp1628_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int sp1628_close(struct file *file)
{
	struct sp1628_fh         *fh = file->private_data;
	struct sp1628_device *dev       = fh->dev;
	struct sp1628_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);

	sp1628_stop_thread(vidq);
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
	sp1628_qctrl[0].default_value=0;
	sp1628_qctrl[1].default_value=4;
	sp1628_qctrl[2].default_value=0;
	sp1628_qctrl[3].default_value= CAM_BANDING_50HZ;
	sp1628_qctrl[4].default_value=0;

	sp1628_qctrl[5].default_value=0;
	sp1628_qctrl[7].default_value=100;
	sp1628_qctrl[8].default_value=0;

	sp1628_frmintervals_active.numerator = 1;
	sp1628_frmintervals_active.denominator = 15;
	//power_down_sp1628(dev);
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

static int sp1628_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sp1628_fh  *fh = file->private_data;
	struct sp1628_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations sp1628_fops = {
	.owner		= THIS_MODULE,
	.open           = sp1628_open,
	.release        = sp1628_close,
	.read           = sp1628_read,
	.poll		= sp1628_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = sp1628_mmap,
};

static const struct v4l2_ioctl_ops sp1628_ioctl_ops = {
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

static struct video_device sp1628_template = {
	.name		= "sp1628_v4l",
	.fops           = &sp1628_fops,
	.ioctl_ops 	= &sp1628_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int sp1628_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_SP1628, 0);
}

static const struct v4l2_subdev_core_ops sp1628_core_ops = {
	.g_chip_ident = sp1628_g_chip_ident,
};

static const struct v4l2_subdev_ops sp1628_ops = {
	.core = &sp1628_core_ops,
};

static int sp1628_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	aml_cam_info_t* plat_dat;
	int err;
	struct sp1628_device *t;
	struct v4l2_subdev *sd;
    vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &sp1628_ops);

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
	memcpy(t->vdev, &sp1628_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);

	wake_lock_init(&(t->wake_lock), WAKE_LOCK_SUSPEND, "sp1628");
	
	/* Register it */
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if (plat_dat->front_back >=0)  
			video_nr = plat_dat->front_back;
	} else {
		printk("camera sp1628: have no platform data\n");
		kfree(t);
		kfree(client);
		return -1;
	}
	
	t->cam_info.version = SP1628_DRIVER_VERSION;
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

static int sp1628_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sp1628_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id sp1628_id[] = {
	{ "sp1628", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sp1628_id);

static struct i2c_driver sp1628_i2c_driver = {
	.driver = {
		.name = "sp1628",
	},
	.probe = sp1628_probe,
	.remove = sp1628_remove,
	.id_table = sp1628_id,
};

module_i2c_driver(sp1628_i2c_driver);

