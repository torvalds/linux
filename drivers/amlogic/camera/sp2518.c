/*
 *sp2518 - This code emulates a real video device with v4l2 api
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
#include <mach/gpio.h>
#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include "common/plat_ctrl.h"
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif
#include "common/vm.h"

#define SP2518_CAMERA_MODULE_NAME "sp2518"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define SP2518_CAMERA_MAJOR_VERSION 0
#define SP2518_CAMERA_MINOR_VERSION 7
#define SP2518_CAMERA_RELEASE 0
#define SP2518_CAMERA_VERSION \
	KERNEL_VERSION(SP2518_CAMERA_MAJOR_VERSION, SP2518_CAMERA_MINOR_VERSION, SP2518_CAMERA_RELEASE)
	
#define SP2518_DRIVER_VERSION "SP2518-COMMON-01-140722"

//unsigned short DGain_shutter,AGain_shutter,DGain_shutterH,DGain_shutterL,AGain_shutterH,AGain_shutterL,shutterH,shutterL,shutter;
//unsigned short UXGA_Cap = 0;
static struct i2c_client * g_i2c_client;
static u32 cur_reg=0;
static u8 cur_val;

 typedef enum

{

	DCAMERA_FLICKER_50HZ = 0,

	DCAMERA_FLICKER_60HZ,

	FLICKER_MAX

}DCAMERA_FLICKER;
static unsigned short Antiflicker = DCAMERA_FLICKER_50HZ;

#define SP2518_NORMAL_Y0ffset 0x08
#define SP2518_LOWLIGHT_Y0ffset  0x20

MODULE_DESCRIPTION("sp2518 On Board");
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

extern int disable_sp2518;

static int sp2518_h_active=1600;//800;
static int sp2518_v_active=1198;//600;

static int sp2518_have_open=0;
static struct v4l2_fract sp2518_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

static int sp2518_night_or_normal = 0;	//add by sp_yjp,20120905
static struct vdin_v4l2_ops_s *vops;
/* supported controls */
static struct v4l2_queryctrl sp2518_qctrl[] = {
	{
		.id            = V4L2_CID_DO_WHITE_BALANCE,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "white balance",
		.minimum       = CAM_WB_AUTO,
		.maximum       = CAM_WB_FLUORESCENT ,
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
struct v4l2_querymenu sp2518_qmenu_wbmode[] = {
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
        .index      = CAM_WB_WARM_FLUORESCENT,
        .name       = "warm-fluorescent",
        .reserved   = 0,
    },
};

struct v4l2_querymenu sp2518_qmenu_anti_banding_mode[] = {
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
static struct v4l2_frmivalenum sp2518_frmivalenum[]={
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


typedef struct {
    __u32   id;
    int     num;
    struct v4l2_querymenu* sp2518_qmenu;
}sp2518_qmenu_set_t;

sp2518_qmenu_set_t sp2518_qmenu_set[] = {
    {
        .id         	= V4L2_CID_DO_WHITE_BALANCE,
        .num            = ARRAY_SIZE(sp2518_qmenu_wbmode),
        .sp2518_qmenu   = sp2518_qmenu_wbmode,
    },{
        .id         	= V4L2_CID_POWER_LINE_FREQUENCY,
        .num            = ARRAY_SIZE(sp2518_qmenu_anti_banding_mode),
        .sp2518_qmenu   = sp2518_qmenu_anti_banding_mode,
    },
};

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(sp2518_qmenu_set); i++)
	if (a->id && a->id == sp2518_qmenu_set[i].id) {
	    for(j = 0; j < sp2518_qmenu_set[i].num; j++)
		if (a->index == sp2518_qmenu_set[i].sp2518_qmenu[j].index) {
			memcpy(a, &( sp2518_qmenu_set[i].sp2518_qmenu[j]),
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

struct sp2518_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct sp2518_fmt formats[] = {
	{
		.name     = "RGB565 (BE)",
		.fourcc   = V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
		.depth    = 16,
	},{
		.name     = "RGB888 (24)",
		.fourcc   = V4L2_PIX_FMT_RGB24, /* 24  RGB-8-8-8 */
		.depth    = 24,
	},{
		.name     = "BGR888 (24)",
		.fourcc   = V4L2_PIX_FMT_BGR24, /* 24  BGR-8-8-8 */
		.depth    = 24,
	},{
		.name     = "12  Y/CbCr 4:2:0",
		.fourcc   = V4L2_PIX_FMT_NV12,
		.depth    = 12,
	},{
		.name     = "12  Y/CbCr 4:2:0",
		.fourcc   = V4L2_PIX_FMT_NV21,
		.depth    = 12,
	},{
		.name     = "YUV420P",
		.fourcc   = V4L2_PIX_FMT_YUV420,
		.depth    = 12,
	},{
		.name     = "YVU420P",
		.fourcc   = V4L2_PIX_FMT_YVU420,
		.depth    = 12,
	}
};

static struct sp2518_fmt *get_format(struct v4l2_format *f)
{
	struct sp2518_fmt *fmt;
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
struct sp2518_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct sp2518_fmt        *fmt;
	
	unsigned int canvas_id;
};

struct sp2518_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(sp2518_devicelist);

struct sp2518_device {
	struct list_head			sp2518_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct sp2518_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t  cam_info;

	/* wake lock */
	struct wake_lock	wake_lock;
	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(sp2518_qctrl)];
};

static inline struct sp2518_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sp2518_device, sd);
}

struct sp2518_fh {
	struct sp2518_device            *dev;

	/* video capture */
	struct sp2518_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	struct videobuf_res_privdata res;
	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};

/*static inline struct sp2518_fh *to_fh(struct sp2518_device *dev)
{
	return container_of(dev, struct sp2518_fh, dev);
}*/

static struct v4l2_frmsize_discrete sp2518_prev_resolution[]= //should include 352x288 and 640x480, those two size are used for recording
{
	{320,240},
	{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete sp2518_pic_resolution[]=
{
	{1600,1200},
	{800,600}
};

#ifndef SP2518_MIRROR
#define SP2518_MIRROR   0
#endif
#ifndef SP2518_FLIP
#define SP2518_FLIP   0
#endif

/* ------------------------------------------------------------------
	reg spec of SP2518
   ------------------------------------------------------------------*/

struct aml_camera_i2c_fig_s SP2518_script[] = {
	{0xfd,0x00},
	{0x1b,0x02},//maximum drv ability
	{0x0e,0x01},
	{0x0f,0x2f},
	{0x10,0x2e},
	{0x11,0x00},
	{0x12,0x4f},
	{0x14,0x20},
	{0x16,0x02},
	{0x17,0x10},
	{0x1a,0x1f},
	{0x1e,0x81},
	{0x21,0x00},
	{0x22,0x1b},
	{0x25,0x10},
	{0x26,0x25},
	{0x27,0x6d},
	{0x2c,0x31},//Ronlus remove balck dot0x45},
	{0x2d,0x75},
	{0x2e,0x38},//sxga 0x18
	{0x31,0x00},//mirror upside down
	{0x44,0x03},
	{0x6f,0x00},
	{0xa0,0x04},
	{0x5f,0x01},
	{0x32,0x00},
	{0xfd,0x01},
	{0x2c,0x00},
	{0x2d,0x00},
	{0xfd,0x00},
	{0xfb,0x83},
	{0xf4,0x09},
	//Pregain
	{0xfd,0x01},
	{0xc6,0x90},
	{0xc7,0x90},
	{0xc8,0x90},
	{0xc9,0x90},
	//blacklevel
	{0xfd,0x00},
	{0x65,0x08},
	{0x66,0x08},
	{0x67,0x08},
	{0x68,0x08},
	//rpc
	{0xfd,0x00},
	{0xe0,0x6c},
	{0xe1,0x54},
	{0xe2,0x48},
	{0xe3,0x40},
	{0xe4,0x40},
	{0xe5,0x3e},
	{0xe6,0x3e},
	{0xe8,0x3a},
	{0xe9,0x3a},
	{0xea,0x3a},
	{0xeb,0x38},
	{0xf5,0x38},
	{0xf6,0x38},
	{0xfd,0x01},
	{0x94,0xc0},//f8
	{0x95,0x38},
	{0x9c,0x6c},
	{0x9d,0x38},	
	#if defined(EM668_V1)
	///SI50_SP2518 UXGA 24MEclk 2PLL 1DIV 50Hz fix 9fps
	///ae setting
	{0xfd , 0x00},
	{0x03 , 0x02},
	{0x04 , 0xb8},
	{0x05 , 0x00},
	{0x06 , 0x00},
	{0x07 , 0x00},
	{0x08 , 0x00},
	{0x09 , 0x00},
	{0x0a , 0x6c},
	{0x2f , 0x00},
	{0x30 , 0x04},
	{0xf0 , 0x74},
	{0xf1 , 0x00},
	{0xfd , 0x01},
	{0x90 , 0x0b},
	{0x92 , 0x01},
	{0x98 , 0x74},
	{0x99 , 0x00},
	{0x9a , 0x01},
	{0x9b , 0x00},
	///Status
	{0xfd , 0x01},
	{0xce , 0xfc},
	{0xcf , 0x04},
	{0xd0 , 0xfc},
	{0xd1 , 0x04},
	{0xd7 , 0x70},
	{0xd8 , 0x00},
	{0xd9 , 0x74},
	{0xda , 0x00},
	{0xfd , 0x00},
	#else
	///SI50_SP2518 UXGA 24MEclk 3PLL 1DIV 50Hz 10-13fps
	///ae setting
	/*
	{0xfd,0x00},
	{0x03,0x03},
	{0x04,0xf6},
	{0x05,0x00},
	{0x06,0x00},
	{0x07,0x00},
	{0x08,0x00},
	{0x09,0x00},
	{0x0a,0x8b},
	{0x2f,0x00},
	{0x30,0x08},
	{0xf0,0xa9},
	{0xf1,0x00},
	{0xfd,0x01},
	{0x90,0x0a},
	{0x92,0x01},
	{0x98,0xa9},
	{0x99,0x00},
	{0x9a,0x01},
	{0x9b,0x00},
	///Status 
	{0xfd,0x01},
	{0xce,0x9a},
	{0xcf,0x06},
	{0xd0,0x9a},
	{0xd1,0x06},
	{0xd7,0xa5},
	{0xd8,0x00},
	{0xd9,0xa9},
	{0xda,0x00},
	{0xfd,0x00},
	*/
//ae setting 13-12fps	
{0xfd,0x00},
{0x03,0x03},
{0x04,0xf6},
{0x05,0x00},
{0x06,0x00},
{0x07,0x00},
{0x08,0x00},
{0x09,0x00},
{0x0a,0x8b},
{0x2f,0x00},
{0x30,0x08},
{0xf0,0xa9},
{0xf1,0x00},
{0xfd,0x01},
{0x90,0x08},
{0x92,0x01},
{0x98,0xa9},
{0x99,0x00},
{0x9a,0x01},
{0x9b,0x00},
//Status    
{0xfd,0x01},
{0xce,0x48},
{0xcf,0x05},
{0xd0,0x48},
{0xd1,0x05},
{0xd7,0xa5},
{0xd8,0x00},
{0xd9,0xa9},
{0xda,0x00},
{0xfd,0x00},

	#endif
	
	{0xfd,0x01},
	{0xca,0x30},//mean dummy2low
	{0xcb,0x50},//mean low2dummy
	{0xcc,0xc0},//f8;rpc low
	{0xcd,0xc0},//rpc dummy
	{0xd5,0x80},//mean normal2dummy
	{0xd6,0x90},//mean dummy2normal
	{0xfd,0x00},  
	//lens shading for 舜泰979C-171A\181A
	{0xfd,0x00},
	{0xa1,0x20},
	{0xa2,0x20},
	{0xa3,0x20},
	{0xa4,0xff},
	{0xa5,0x80},
	{0xa6,0x80},
	{0xfd,0x01},
	{0x64,0x22},//28
	{0x65,0x1e},//25
	{0x66,0x1e},//2a
	{0x67,0x1a},//25
	{0x68,0x1c},//25
	{0x69,0x1c},//29
	{0x6a,0x1a},//28
	{0x6b,0x16},//20
	{0x6c,0x1a},//22
	{0x6d,0x1a},//22
	{0x6e,0x1a},//22
	{0x6f,0x16},//1c
	{0xb8,0x04},//0a
	{0xb9,0x13},//0a
	{0xba,0x00},//23
	{0xbb,0x03},//14
	{0xbc,0x03},//08
	{0xbd,0x11},//08
	{0xbe,0x00},//12
	{0xbf,0x02},//00
	{0xc0,0x04},//05
	{0xc1,0x0e},//05
	{0xc2,0x00},//18
	{0xc3,0x05},//08   
	//raw filter
	{0xfd,0x01},
	{0xde,0x0f},
	{0xfd,0x00},
	{0x57,0x08},//raw_dif_thr
	{0x58,0x08},//a
	{0x56,0x08},//a
	{0x59,0x10},
	//R\B通道间平滑
	{0x5a,0xa0},//raw_rb_fac_outdoor
	{0xc4,0xa0},//60raw_rb_fac_indoor
	{0x43,0xa0},//40raw_rb_fac_dummy  
	{0xad,0x40},//raw_rb_fac_low  
	//Gr、Gb 通道内部平滑
	{0x4f,0xa0},//raw_gf_fac_outdoor
	{0xc3,0xa0},//60raw_gf_fac_indoor
	{0x3f,0xa0},//40raw_gf_fac_dummy
	{0x42,0x40},//raw_gf_fac_low
	{0xc2,0x15},
	//Gr、Gb通道间平滑
	{0xb6,0x80},//raw_gflt_fac_outdoor
	{0xb7,0x80},//60raw_gflt_fac_normal
	{0xb8,0x40},//40raw_gflt_fac_dummy
	{0xb9,0x20},//raw_gflt_fac_low
	//Gr、Gb通道阈值
	{0xfd,0x01},
	{0x50,0x0c},//raw_grgb_thr
	{0x51,0x0c},
	{0x52,0x10},
	{0x53,0x10},
	{0xfd,0x00},	
	// awb1
	{0xfd,0x01},
	{0x11,0x10},
	{0x12,0x1f},
	{0x16,0x1c},
	{0x18,0x00},
	{0x19,0x00},
	{0x1b,0x96},
	{0x1a,0x9a},//95
	{0x1e,0x2f},
	{0x1f,0x29},
	{0x20,0xff},
	{0x22,0xff},  
	{0x28,0xce},
	{0x29,0x8a},
	{0xfd,0x00},
	{0xe7,0x03},
	{0xe7,0x00},
	{0xfd,0x01},
	{0x2a,0xf0},
	{0x2b,0x10},
	{0x2e,0x04},
	{0x2f,0x18},
	{0x21,0x60},
	{0x23,0x60},
	{0x8b,0xab},
	{0x8f,0x12},
	//awb2
	{0xfd,0x01},
	{0x1a,0x80},
	{0x1b,0x80},
	{0x43,0x80},
	//d65
	{0x35,0xd6},//d6;b0
	{0x36,0xf0},//f0;d1;e9
	{0x37,0x7a},//8a;70
	{0x38,0x9a},//dc;9a;af
	//indoor
	{0x39,0xab},
	{0x3a,0xca},
	{0x3b,0xa3},
	{0x3c,0xc1},
	//f
	{0x31,0x82},//7d
	{0x32,0xa5},//a0;74
	{0x33,0xd6},//d2
	{0x34,0xec},//e8
	{0x3d,0xa5},//a7;88
	{0x3e,0xc2},//be;bb
	{0x3f,0xa7},//b3;ad
	{0x40,0xc5},//c5;d0
	//Color Correction				  
	{0xfd,0x01},
	{0x1c,0xc0},
	{0x1d,0x95},
	{0xa0,0xa6},//b8 
	{0xa1,0xda},//;d5
	{0xa2,0x00},//;f2
	{0xa3,0x06},//;e8
	{0xa4,0xb2},//;95
	{0xa5,0xc7},//;03
	{0xa6,0x00},//;f2
	{0xa7,0xce},//;c4
	{0xa8,0xb2},//;ca
	{0xa9,0x0c},//;3c
	{0xaa,0x30},//;03
	{0xab,0x0c},//;0f
	{0xac,0xc0},//b8 
	{0xad,0xc0},//d5
	{0xae,0x00},//f2
	{0xaf,0xf2},//e8
	{0xb0,0xa6},//95
	{0xb1,0xe8},//03
	{0xb2,0x00},//f2
	{0xb3,0xe7},//c4
	{0xb4,0x99},//ca
	{0xb5,0x0c},//3c
	{0xb6,0x33},//03
	{0xb7,0x0c},//0f
	//Saturation
	{0xfd,0x00},
	{0xbf,0x01},
	{0xbe,0xbb},
	{0xc0,0xb0},
	{0xc1,0xf0},
	{0xd3,0x77},
	{0xd4,0x77},
	{0xd6,0x77},
	{0xd7,0x77},
	{0xd8,0x77},
	{0xd9,0x77},
	{0xda,0x77},
	{0xdb,0x77},
	//uv_dif
	{0xfd,0x00},
	{0xf3,0x03},
	{0xb0,0x00},
	{0xb1,0x23},
	//gamma1
	{0xfd,0x00},//
	{0x8b,0x0 },//0 ;0	
	{0x8c,0xA },//14;A 
	{0x8d,0x13},//24;13
	{0x8e,0x25},//3a;25
	{0x8f,0x43},//59;43
	{0x90,0x5D},//6f;5D
	{0x91,0x74},//84;74
	{0x92,0x88},//95;88
	{0x93,0x9A},//a3;9A
	{0x94,0xA9},//b1;A9
	{0x95,0xB5},//be;B5
	{0x96,0xC0},//c7;C0
	{0x97,0xCA},//d1;CA
	{0x98,0xD4},//d9;D4
	{0x99,0xDD},//e1;DD
	{0x9a,0xE6},//e9;E6
	{0x9b,0xEF},//f1;EF
	{0xfd,0x01},//01;01
	{0x8d,0xF7},//f9;F7
	{0x8e,0xFF},//ff;FF
	//gamma2   
	{0xfd,0x00},//
	{0x78,0x0 },//0   
	{0x79,0xA },//14
	{0x7a,0x13},//24
	{0x7b,0x25},//3a
	{0x7c,0x43},//59
	{0x7d,0x5D},//6f
	{0x7e,0x74},//84
	{0x7f,0x88},//95
	{0x80,0x9A},//a3
	{0x81,0xA9},//b1
	{0x82,0xB5},//be
	{0x83,0xC0},//c7
	{0x84,0xCA},//d1
	{0x85,0xD4},//d9
	{0x86,0xDD},//e1
	{0x87,0xE6},//e9
	{0x88,0xEF},//f1
	{0x89,0xF7},//f9
	{0x8a,0xFF},//ff
	//gamma_ae  
	{0xfd,0x01},
	{0x96,0x46},
	{0x97,0x14},
	{0x9f,0x06},
	//HEQ
	{0xfd,0x00},//
	{0xdd,0x80},//
	{0xde,0x88},//a0 0x95//
	{0xdf,0x80},//
	//Ytarget 
	{0xfd,0x00},// 
	{0xec,0x70},//6a
	{0xed,0x86},//7c
	{0xee,0x70},//65
	{0xef,0x86},//78
	{0xf7,0x80},//78
	{0xf8,0x74},//6e
	{0xf9,0x80},//74
	{0xfa,0x74},//6a 
	//sharpen
	{0xfd,0x01},
	{0xdf,0x0f},
	{0xe5,0x10},
	{0xe7,0x10},
	{0xe8,0x20},
	{0xec,0x20},
	{0xe9,0x20},
	{0xed,0x20},
	{0xea,0x10},
	{0xef,0x10},
	{0xeb,0x10},
	{0xf0,0x10},
	//;gw
	{0xfd,0x01},//
	{0x70,0x76},//
	{0x7b,0x40},//
	{0x81,0x30},//
	//;Y_offset
	{0xfd,0x00},
	{0xb2,0x1f},
	{0xb3,0x0f},
	{0xb4,0x30},
	{0xb5,0x50},
	//;CNR
	{0xfd,0x00},
	{0x5b,0x20},
	{0x61,0x80},
	{0x77,0x80},
	{0xca,0x80},
	//;YNR  
	{0xab,0x00},
	{0xac,0x02},
	{0xae,0x08},
	{0xaf,0x20},
	{0xfd,0x00},
	{0x31, 0x10 | (SP2518_FLIP<<6) | (SP2518_MIRROR<<5)},
	{0x32,0x0d},
	{0x33,0xcf},//ef
	{0x34,0x3f},
	{0x35,0x41},//0xc0
	{0x1b,0x1a},//02
#if 0	
	//set selution 640*480
	{0xfd , 0x00},
	{0x4b , 0x00},
	{0x4c , 0x00},
	{0x47 , 0x00},
	{0x48 , 0x00},
	{0xfd , 0x01},
	{0x06 , 0x00},
	{0x07 , 0x40},
	{0x08 , 0x00},
	{0x09 , 0x40},
	{0x0a , 0x02},
	{0x0b , 0x58},
	{0x0c , 0x03},
	{0x0d , 0x20},
	{0x0e , 0x01},
	{0xfd , 0x00},
#endif	
	{0xe7,0x03},
	{0xe7,0x00},
	{0xff,0xff},
};

//load SP2518 parameters
void SP2518_init_regs(struct sp2518_device *dev)
{
   struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    int i=0;

    while(1)
    {
        if (SP2518_script[i].val==0xff&&SP2518_script[i].addr==0xff)
        {
        	printk("SP2518_write_regs success in initial SP2518.\n");
        	break;
        }
        if((i2c_put_byte_add8_new(client,SP2518_script[i].addr, SP2518_script[i].val)) < 0)
        {
        	printk("fail in initial SP2518. \n");
		return;
		}
		i++;
    }
	/*
    aml_plat_cam_data_t* plat_dat= (aml_plat_cam_data_t*)client->dev.platform_data;
    if (plat_dat&&plat_dat->custom_init_script) {
		i=0;
		aml_camera_i2c_fig_t*  custom_script = (aml_camera_i2c_fig_t*)plat_dat->custom_init_script;
		while(1)
		{
			if (custom_script[i].val==0xff&&custom_script[i].addr==0xff)
			{
				printk("SP2518_write_custom_regs success in initial SP2518.\n");
				break;
			}
			if((i2c_put_byte_add8_new(client,custom_script[i].addr, custom_script[i].val)) < 0)
			{
				printk("fail in initial SP2518 custom_regs. \n");
				return;
			}
			i++;
		}
    }
    */
    return;
}
/*************************************************************************
* FUNCTION
*    SP2518_set_param_wb
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
void SP2518_set_param_wb(struct sp2518_device *dev,enum  camera_wb_flip_e para)//white balance
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	switch (para)
	{

		case CAM_WB_AUTO://auto
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0x28,0xce);
			i2c_put_byte_add8_new(client,0x29,0x8a);
			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x32,0x0d);
			break;

		case CAM_WB_CLOUD: //cloud
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0x28,0xe2);
			i2c_put_byte_add8_new(client,0x29,0x82);
			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x32,0x05);
			break;

		case CAM_WB_DAYLIGHT: //
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0x28,0xc1);
			i2c_put_byte_add8_new(client,0x29,0x88);
			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x32,0x05);
			break;

		case CAM_WB_INCANDESCENCE:
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0x28,0x7b);
			i2c_put_byte_add8_new(client,0x29,0xd3);
			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x32,0x05);
			break;

		case CAM_WB_TUNGSTEN:
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0x28,0xae);
			i2c_put_byte_add8_new(client,0x29,0xcc);
			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x32,0x05);
			break;

		case CAM_WB_FLUORESCENT:
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0x28,0xb4);
			i2c_put_byte_add8_new(client,0x29,0xc4);
			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x32,0x05);
			break;

		case CAM_WB_MANUAL:
		    	// TODO
			break;
		default:
			break;
	}


} /* SP2518_set_param_wb */
/*************************************************************************
* FUNCTION
*    SP2518_set_param_exposure
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
void SP2518_set_param_exposure(struct sp2518_device *dev,enum camera_exposure_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);


    switch (para)
	{

		case EXPOSURE_N4_STEP:
			i2c_put_byte_add8_new(client,0xfd , 0x00);
			i2c_put_byte_add8_new(client,0xdc , 0xc0);//-40
			break;



		case EXPOSURE_N3_STEP:
			i2c_put_byte_add8_new(client,0xfd , 0x00);    
			i2c_put_byte_add8_new(client,0xdc , 0xd0);//-30
			break;


		case EXPOSURE_N2_STEP:
			i2c_put_byte_add8_new(client,0xfd , 0x00);    
			i2c_put_byte_add8_new(client,0xdc , 0xe0);//-20
			break;


		case EXPOSURE_N1_STEP:
			i2c_put_byte_add8_new(client,0xfd , 0x00);    
			i2c_put_byte_add8_new(client,0xdc , 0xf0);//-10
			break;
			
		case EXPOSURE_0_STEP:
			i2c_put_byte_add8_new(client,0xfd , 0x00);    
			i2c_put_byte_add8_new(client,0xdc , 0x00);//00
			break;
			
		case EXPOSURE_P1_STEP:
			i2c_put_byte_add8_new(client,0xfd , 0x00);    
			i2c_put_byte_add8_new(client,0xdc , 0x10);//10
			break;
			
		case EXPOSURE_P2_STEP:
			i2c_put_byte_add8_new(client,0xfd , 0x00);    
			i2c_put_byte_add8_new(client,0xdc , 0x20);//20
			break;

		case EXPOSURE_P3_STEP:
			i2c_put_byte_add8_new(client,0xfd , 0x00);    
			i2c_put_byte_add8_new(client,0xdc , 0x30);//30
			break;
					
		case EXPOSURE_P4_STEP:	
			i2c_put_byte_add8_new(client,0xfd , 0x00);    
			i2c_put_byte_add8_new(client,0xdc , 0x40);//40
			break;

		default:
			i2c_put_byte_add8_new(client,0xfd , 0x00);    
			i2c_put_byte_add8_new(client,0xdc , 0x00);//00
			break;
				//break;



	}


} /* SP2518_set_param_exposure */
/*************************************************************************
* FUNCTION
*    SP2518_set_param_effect
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
void SP2518_set_param_effect(struct sp2518_device *dev,enum camera_effect_flip_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);


    switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL:
			i2c_put_byte_add8_new(client,0xfd, 0x00);
			i2c_put_byte_add8_new(client,0x62, 0x00);
			i2c_put_byte_add8_new(client,0x63, 0x80);
			i2c_put_byte_add8_new(client,0x64, 0x80);
			break;

		case CAM_EFFECT_ENC_GRAYSCALE:
			i2c_put_byte_add8_new(client,0xfd, 0x00);
			i2c_put_byte_add8_new(client,0x62, 0x20);
			i2c_put_byte_add8_new(client,0x63, 0x80);
			i2c_put_byte_add8_new(client,0x64, 0x80);
			break;

		case CAM_EFFECT_ENC_SEPIA:
			i2c_put_byte_add8_new(client,0xfd, 0x00);
			i2c_put_byte_add8_new(client,0x62, 0x10);
			i2c_put_byte_add8_new(client,0x63, 0xb0);
			i2c_put_byte_add8_new(client,0x64, 0x40);
			break;

		case CAM_EFFECT_ENC_SEPIAGREEN:
			i2c_put_byte_add8_new(client,0xfd, 0x00);
			i2c_put_byte_add8_new(client,0x62, 0x10);
			i2c_put_byte_add8_new(client,0x63, 0x50);
			i2c_put_byte_add8_new(client,0x64, 0x50);
			break;

		case CAM_EFFECT_ENC_SEPIABLUE:
			i2c_put_byte_add8_new(client,0xfd, 0x00);
			i2c_put_byte_add8_new(client,0x62, 0x10);
			i2c_put_byte_add8_new(client,0x63, 0x80);
			i2c_put_byte_add8_new(client,0x64, 0xb0);
			break;

		case CAM_EFFECT_ENC_COLORINV:
			i2c_put_byte_add8_new(client,0xfd, 0x00);
			i2c_put_byte_add8_new(client,0x62, 0x04);
			i2c_put_byte_add8_new(client,0x63, 0x80);
			i2c_put_byte_add8_new(client,0x64, 0x80);

			break;

		default:
			break;
	}



} /* SP2518_set_param_effect */

/*************************************************************************
* FUNCTION
*    SP2518_NightMode
*
* DESCRIPTION
*    This function night mode of SP2518.
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
void SP2518_set_night_mode(struct sp2518_device *dev,enum  camera_night_mode_flip_e enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);	
	
	if (enable)
	{
		sp2518_night_or_normal = 1;	//=1,night mode; =0,normal mode	//add by sp_yjp,20120905
		i2c_put_byte_add8_new(client,0xfd,0x0 );
		i2c_put_byte_add8_new(client,0xb2,SP2518_LOWLIGHT_Y0ffset);
		i2c_put_byte_add8_new(client,0xb3,0x1f);
		if(Antiflicker== DCAMERA_FLICKER_50HZ)
		{
			i2c_put_byte_add8_new(client,0xfd,0x00);	//disable AE,add by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x32,0x05);
			printk("night mode 50hz\r\n");
			#ifdef CLK24M_48M

			//capture preview night 48M 50hz fix 6FPS maxgain 
			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x03,0x00);	//0x01	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x04,0x09);	//0xd4	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x09,0x02);
			i2c_put_byte_add8_new(client,0x0a,0x64); 
			i2c_put_byte_add8_new(client,0xf0,0x4e);
			i2c_put_byte_add8_new(client,0xf1,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x01); 
			i2c_put_byte_add8_new(client,0x90,0x10);
			i2c_put_byte_add8_new(client,0x92,0x01);
			i2c_put_byte_add8_new(client,0x98,0x4e);
			i2c_put_byte_add8_new(client,0x99,0x00);
			i2c_put_byte_add8_new(client,0x9a,0x01);
			i2c_put_byte_add8_new(client,0x9b,0x00);				  

			// status				

			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0xce,0xe0);
			i2c_put_byte_add8_new(client,0xcf,0x04);
			i2c_put_byte_add8_new(client,0xd0,0xe0);
			i2c_put_byte_add8_new(client,0xd1,0x04);
			i2c_put_byte_add8_new(client,0xd7,0x4a);//exp_nr_outd_8lsb
			i2c_put_byte_add8_new(client,0xd8,0x00);
			i2c_put_byte_add8_new(client,0xd9,0x4e);//exp_outd_nr_8lsb
			i2c_put_byte_add8_new(client,0xda,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x00);

			
			#elif  defined(CLK24M_72M)

			//capture preview night 72M 50hz fix 6FPS maxgain									   

			i2c_put_byte_add8_new(client,0xfd , 0x00);
			i2c_put_byte_add8_new(client,0x03 , 0x00);	//0x01	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x04 , 0x09);	//0xd4	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x05 , 0x00);
			i2c_put_byte_add8_new(client,0x06 , 0x00);
			i2c_put_byte_add8_new(client,0x07 , 0x00);
			i2c_put_byte_add8_new(client,0x08 , 0x00);
			i2c_put_byte_add8_new(client,0x09 , 0x05);
			i2c_put_byte_add8_new(client,0x0a , 0x66);
			i2c_put_byte_add8_new(client,0xf0 , 0x4e);
			i2c_put_byte_add8_new(client,0xf1 , 0x00);
			i2c_put_byte_add8_new(client,0xfd , 0x01);
			i2c_put_byte_add8_new(client,0x90 , 0x10);
			i2c_put_byte_add8_new(client,0x92 , 0x01);
			i2c_put_byte_add8_new(client,0x98 , 0x4e);
			i2c_put_byte_add8_new(client,0x99 , 0x00);
			i2c_put_byte_add8_new(client,0x9a , 0x01);
			i2c_put_byte_add8_new(client,0x9b , 0x00);

			//Status							  

			i2c_put_byte_add8_new(client,0xfd , 0x01);
			i2c_put_byte_add8_new(client,0xce , 0xe0);
			i2c_put_byte_add8_new(client,0xcf , 0x04);
			i2c_put_byte_add8_new(client,0xd0 , 0xe0);
			i2c_put_byte_add8_new(client,0xd1 , 0x04);
			i2c_put_byte_add8_new(client,0xd7 , 0x4a);
			i2c_put_byte_add8_new(client,0xd8 , 0x00);
			i2c_put_byte_add8_new(client,0xd9 , 0x4e);
			i2c_put_byte_add8_new(client,0xda , 0x00);
			i2c_put_byte_add8_new(client,0xfd , 0x00);
			#endif
			i2c_put_byte_add8_new(client,0xe7,0x03);	//add by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0xe7,0x00);			

		}
		else
		{
			i2c_put_byte_add8_new(client,0xfd,0x00);	//disable AE,add by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x32,0x05);
			printk("night mode 60hz\r\n");

			#ifdef CLK24M_48M

			//capture preview night 48M 60hz fix 6FPS maxgain

			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x03,0x00);	//0x01	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x04,0x06);	//0x86	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x09,0x02);
			i2c_put_byte_add8_new(client,0x0a,0x64);
			i2c_put_byte_add8_new(client,0xf0,0x41);
			i2c_put_byte_add8_new(client,0xf1,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0x90,0x14); 
			i2c_put_byte_add8_new(client,0x92,0x01);
			i2c_put_byte_add8_new(client,0x98,0x41);
			i2c_put_byte_add8_new(client,0x99,0x00);
			i2c_put_byte_add8_new(client,0x9a,0x01);
			i2c_put_byte_add8_new(client,0x9b,0x00);

			// status				
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0xce,0x14);
			i2c_put_byte_add8_new(client,0xcf,0x05);
			i2c_put_byte_add8_new(client,0xd0,0x14);
			i2c_put_byte_add8_new(client,0xd1,0x05);
			i2c_put_byte_add8_new(client,0xd7,0x3d);//exp_nr_outd_8lsb
			i2c_put_byte_add8_new(client,0xd8,0x00);
			i2c_put_byte_add8_new(client,0xd9,0x41);//exp_outd_nr_8lsb
			i2c_put_byte_add8_new(client,0xda,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x00);

			#elif  defined(CLK24M_72M)

			//capture preview night 72M 60hz fix 6FPS maxgain	
			i2c_put_byte_add8_new(client,0xfd , 0x00);
			i2c_put_byte_add8_new(client,0x03 , 0x01);	//0x01	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x04 , 0x06);	//0x86	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x05 , 0x00);
			i2c_put_byte_add8_new(client,0x06 , 0x00);
			i2c_put_byte_add8_new(client,0x07 , 0x00);
			i2c_put_byte_add8_new(client,0x08 , 0x00);
			i2c_put_byte_add8_new(client,0x09 , 0x05);
			i2c_put_byte_add8_new(client,0x0a , 0x66);
			i2c_put_byte_add8_new(client,0xf0 , 0x41);
			i2c_put_byte_add8_new(client,0xf1 , 0x00);
			i2c_put_byte_add8_new(client,0xfd , 0x01);
			i2c_put_byte_add8_new(client,0x90 , 0x14);
			i2c_put_byte_add8_new(client,0x92 , 0x01);
			i2c_put_byte_add8_new(client,0x98 , 0x41);
			i2c_put_byte_add8_new(client,0x99 , 0x00);
			i2c_put_byte_add8_new(client,0x9a , 0x01);
			i2c_put_byte_add8_new(client,0x9b , 0x00);

			//Status	
			i2c_put_byte_add8_new(client,0xfd , 0x01);
			i2c_put_byte_add8_new(client,0xce , 0x14);
			i2c_put_byte_add8_new(client,0xcf , 0x05);
			i2c_put_byte_add8_new(client,0xd0 , 0x14);
			i2c_put_byte_add8_new(client,0xd1 , 0x05);
			i2c_put_byte_add8_new(client,0xd7 , 0x3d);
			i2c_put_byte_add8_new(client,0xd8 , 0x00);
			i2c_put_byte_add8_new(client,0xd9 , 0x41);
			i2c_put_byte_add8_new(client,0xda , 0x00);
			i2c_put_byte_add8_new(client,0xfd , 0x00);
			#endif	

			i2c_put_byte_add8_new(client,0xe7,0x03);	//add by sp_yjp,20120905
 			i2c_put_byte_add8_new(client,0xe7,0x00);

		}
	}
	else
	{
        //i2c_put_byte_add8_new(client,); //Camera Enable night mode  1/5 Frame rate //zyy test
		sp2518_night_or_normal = 0;	//=1,night mode; =0,normal mode	//add by sp_yjp,20120905
		
		i2c_put_byte_add8_new(client,0xfd,0x00);
		i2c_put_byte_add8_new(client,0xb2,SP2518_NORMAL_Y0ffset);
		i2c_put_byte_add8_new(client,0xb3,0x1f);

		if(Antiflicker== DCAMERA_FLICKER_50HZ)
		{
			i2c_put_byte_add8_new(client,0xfd,0x00);	//disable AE,add by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x32,0x05);
			printk("normal mode 50hz\r\n");	
			#ifdef CLK24M_48M
			//capture preview daylight 48M 50hz fix 9FPS maxgain  
			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x03,0x00);	//0x02	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x04,0x09);	//0xbe	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x09,0x00);
			i2c_put_byte_add8_new(client,0x0a,0x64); 
			i2c_put_byte_add8_new(client,0xf0,0x75);
			i2c_put_byte_add8_new(client,0xf1,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x01);	
			i2c_put_byte_add8_new(client,0x90,0x0b);	
			i2c_put_byte_add8_new(client,0x92,0x01);
			i2c_put_byte_add8_new(client,0x98,0x75);
			i2c_put_byte_add8_new(client,0x99,0x00);
			i2c_put_byte_add8_new(client,0x9a,0x01);
			i2c_put_byte_add8_new(client,0x9b,0x00);				                  
			
			// status               
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0xce,0x07);
			i2c_put_byte_add8_new(client,0xcf,0x05);
			i2c_put_byte_add8_new(client,0xd0,0x07);
			i2c_put_byte_add8_new(client,0xd1,0x05);
			i2c_put_byte_add8_new(client,0xd7,0x71);//exp_nr_outd_8lsb
			i2c_put_byte_add8_new(client,0xd8,0x00);
			i2c_put_byte_add8_new(client,0xd9,0x75);//exp_outd_nr_8lsb
			i2c_put_byte_add8_new(client,0xda,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x00);

			#elif  defined(CLK24M_72M)	
			//capture preview daylight 72M 50hz 10-13FPS maxgain
			///SI50_SP2518 UXGA 24MEclk 3PLL 1DIV 50Hz 10-13fps
			///ae setting
			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x03,0x00);	//0x03	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x04,0x09);	//0xf6	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x09,0x00);
			i2c_put_byte_add8_new(client,0x0a,0x8b);
			i2c_put_byte_add8_new(client,0xf0,0xa9);
			i2c_put_byte_add8_new(client,0xf1,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0x90,0x0a);
			i2c_put_byte_add8_new(client,0x92,0x01);
			i2c_put_byte_add8_new(client,0x98,0xa9);
			i2c_put_byte_add8_new(client,0x99,0x00);
			i2c_put_byte_add8_new(client,0x9a,0x01);
			i2c_put_byte_add8_new(client,0x9b,0x00);
			///Status 
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0xce,0x9a);
			i2c_put_byte_add8_new(client,0xcf,0x06);
			i2c_put_byte_add8_new(client,0xd0,0x9a);
			i2c_put_byte_add8_new(client,0xd1,0x06);
			i2c_put_byte_add8_new(client,0xd7,0xa5);
			i2c_put_byte_add8_new(client,0xd8,0x00);
			i2c_put_byte_add8_new(client,0xd9,0xa9);
			i2c_put_byte_add8_new(client,0xda,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x00);      
			#endif	      

			i2c_put_byte_add8_new(client,0xe7,0x03);	//add by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0xe7,0x00);
		}
		else
		{
			i2c_put_byte_add8_new(client,0xfd,0x00);	//disable AE,add by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x32,0x05);
			printk("normal mode 60hz\r\n");	
			#ifdef CLK24M_48M
			//capture preview daylight 48M 60Hz fix 9FPS maxgain   
			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x03,0x00);	//0x02	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x04,0x09); 	//0x4c	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x09,0x00);
			i2c_put_byte_add8_new(client,0x0a,0x5e); 
			i2c_put_byte_add8_new(client,0xf0,0x62);
			i2c_put_byte_add8_new(client,0xf1,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x01);	
			i2c_put_byte_add8_new(client,0x90,0x0d);	
			i2c_put_byte_add8_new(client,0x92,0x01);
			i2c_put_byte_add8_new(client,0x98,0x62);
			i2c_put_byte_add8_new(client,0x99,0x00);
			i2c_put_byte_add8_new(client,0x9a,0x01);
			i2c_put_byte_add8_new(client,0x9b,0x00);
			
			// status               
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0xce,0xfa);
			i2c_put_byte_add8_new(client,0xcf,0x04);
			i2c_put_byte_add8_new(client,0xd0,0xfa);
			i2c_put_byte_add8_new(client,0xd1,0x04);
			i2c_put_byte_add8_new(client,0xd7,0x5e);//exp_nr_outd_8lsb
			i2c_put_byte_add8_new(client,0xd8,0x00);
			i2c_put_byte_add8_new(client,0xd9,0x62);//exp_outd_nr_8lsb
			i2c_put_byte_add8_new(client,0xda,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x00);
			#elif  defined(CLK24M_72M)  
			//capture preview daylight 72M 60Hz 10-13FPS maxgain
			///SI50_SP2518 UXGA 24MEclk 3PLL 1DIV 60Hz 10-13fps
			///ae setting
			i2c_put_byte_add8_new(client,0xfd,0x00);
			i2c_put_byte_add8_new(client,0x03,0x00);	//0x03	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x04,0x09);  	//0x4e	modify by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0x09,0x00);
			i2c_put_byte_add8_new(client,0x0a,0x8a);  
			i2c_put_byte_add8_new(client,0xf0,0x8d);
			i2c_put_byte_add8_new(client,0xf1,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0x90,0x0c);
			i2c_put_byte_add8_new(client,0x92,0x01);
			i2c_put_byte_add8_new(client,0x98,0x8d);
			i2c_put_byte_add8_new(client,0x99,0x00);
			i2c_put_byte_add8_new(client,0x9a,0x01);
			i2c_put_byte_add8_new(client,0x9b,0x00);
			///Status
			i2c_put_byte_add8_new(client,0xfd,0x01);
			i2c_put_byte_add8_new(client,0xce,0x9c);
			i2c_put_byte_add8_new(client,0xcf,0x06);
			i2c_put_byte_add8_new(client,0xd0,0x9c);
			i2c_put_byte_add8_new(client,0xd1,0x06);
			i2c_put_byte_add8_new(client,0xd7,0x89);
			i2c_put_byte_add8_new(client,0xd8,0x00);
			i2c_put_byte_add8_new(client,0xd9,0x8d);
			i2c_put_byte_add8_new(client,0xda,0x00);
			i2c_put_byte_add8_new(client,0xfd,0x00);
			#endif	

			i2c_put_byte_add8_new(client,0xe7,0x03);	//add by sp_yjp,20120905
			i2c_put_byte_add8_new(client,0xe7,0x00);
		}
	//i2c_put_byte_add8_new(client,); //Disable night mode  1/2 Frame rate
	}
	i2c_put_byte_add8_new(client,0xfd,0x00);	//enable AE,add by sp_yjp,20120905
	i2c_put_byte_add8_new(client,0x32,0x0d);
}    /* SP2518_NightMode */
void SP2518_set_param_banding(struct sp2518_device *dev,enum  camera_banding_flip_e banding)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//unsigned char buf[4];
	
	switch(banding) {
	case CAM_BANDING_50HZ: 		
		Antiflicker = DCAMERA_FLICKER_50HZ;
		printk( " set_SP2518_anti_flicker  50hz \r\n" );
		break;
	case CAM_BANDING_60HZ:
		Antiflicker = DCAMERA_FLICKER_60HZ;
		printk( " set_SP2518_anti_flicker  60hz \r\n" );
		break;
	default:
		break;
	}

}

static int set_flip(struct sp2518_device *dev)
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

void SP2518_set_resolution(struct sp2518_device *dev,int height,int width)
{

	//int ret;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	printk("---------- %s : height %d  width  %d \n " , __FUNCTION__,height,width);
	if((width<1600)&&(height<1198)){
		//800*600
		printk("----------  :   aaaa \n " );
#if 0
		i2c_put_byte_add8_new(client,0xfd , 0x01);
		i2c_put_byte_add8_new(client,0x06 , 0x00);
		i2c_put_byte_add8_new(client,0x07 , 0x40);
		i2c_put_byte_add8_new(client,0x08 , 0x00);
		i2c_put_byte_add8_new(client,0x09 , 0x40);
		i2c_put_byte_add8_new(client,0x0a , 0x02);
		i2c_put_byte_add8_new(client,0x0b , 0x58);
		i2c_put_byte_add8_new(client,0x0c , 0x03);
		i2c_put_byte_add8_new(client,0x0d , 0x20);
		i2c_put_byte_add8_new(client,0x0e , 0x00);
		//i2c_put_byte_add8_new(client,0x0f , 0x01);
		//i2c_put_byte_add8_new(client,0xfd , 0x00);
		//i2c_put_byte_add8_new(client,0x2f , 0x08);
//#else
		i2c_put_byte_add8_new(client,0xfd , 0x00);
		i2c_put_byte_add8_new(client,0x4b , 0x00);
		i2c_put_byte_add8_new(client,0x4c , 0x00);
		i2c_put_byte_add8_new(client,0x47 , 0x00);
		i2c_put_byte_add8_new(client,0x48 , 0x00);
		i2c_put_byte_add8_new(client,0xfd , 0x01);	
		i2c_put_byte_add8_new(client,0x06 , 0x00);
		i2c_put_byte_add8_new(client,0x07 , 0x40);
		i2c_put_byte_add8_new(client,0x08 , 0x00);
		i2c_put_byte_add8_new(client,0x09 , 0x40);
		i2c_put_byte_add8_new(client,0x0a , 0x02);
		i2c_put_byte_add8_new(client,0x0b , 0x58);
		i2c_put_byte_add8_new(client,0x0c , 0x03);
		i2c_put_byte_add8_new(client,0x0d , 0x20);
		i2c_put_byte_add8_new(client,0x0e , 0x01);
		i2c_put_byte_add8_new(client,0xfd , 0x00);
#endif		
               mdelay(100);
               sp2518_frmintervals_active.denominator 	= 15;
               sp2518_frmintervals_active.numerator	= 1;
		//sp2518_h_active=800;
		//sp2518_v_active=600;
		
		}
		else	if(width>=1600&&height>=1198 ){
		//1600x1200
		printk("----------  :  bbbb \n " );

		i2c_put_byte_add8_new(client,0xfd , 0x01);
		i2c_put_byte_add8_new(client,0x06 , 0x00);
		i2c_put_byte_add8_new(client,0x07 , 0x40);
		i2c_put_byte_add8_new(client,0x08 , 0x00);
		i2c_put_byte_add8_new(client,0x09 , 0x40);
		i2c_put_byte_add8_new(client,0x0a , 0x02);
		i2c_put_byte_add8_new(client,0x0b , 0x58);
		i2c_put_byte_add8_new(client,0x0c , 0x03);
		i2c_put_byte_add8_new(client,0x0d , 0x20);
		i2c_put_byte_add8_new(client,0x0e , 0x00);//resize_en
		i2c_put_byte_add8_new(client,0x0f , 0x00);
		i2c_put_byte_add8_new(client,0xfd , 0x00);
		i2c_put_byte_add8_new(client,0x2f , 0x00);

/*****************************88
		UXGA_Cap = 1;
		ret = i2c_put_byte_add8_new(client, 0x0300, 0xc1);

		shutterH = i2c_put_byte_add8_new(client, 0x0012);
		shutterL = i2c_put_byte_add8_new(client, 0x0013);
		AGain_shutterH = i2c_put_byte_add8_new(client, 0x0014);
		AGain_shutterL = i2c_put_byte_add8_new(client, 0x0015);
		DGain_shutterH = i2c_put_byte_add8_new(client, 0x0016);
		DGain_shutterL = i2c_put_byte_add8_new(client, 0x0017);
		//AGain_shutter = ((AGain_shutterH<<8)|(AGain_shutterL&0xff));
		DGain_shutter = (DGain_shutterH<<8|(DGain_shutterL&0xff));
		DGain_shutter = DGain_shutter>>2;
		shutter =( (shutterH<<8)|(shutterL&0xff));
		//shutter = shutter/2;
		ret = i2c_put_byte_add8_new(client, 0x0300, 0x41);
		ret = i2c_put_byte_add8_new(client, 0x0304, shutter>>8);
		ret = i2c_put_byte_add8_new(client, 0x0305, shutter&0xff);

		ret = i2c_put_byte_add8_new(client, 0x0307, AGain_shutterL);
		ret = i2c_put_byte_add8_new(client, 0x0306, AGain_shutterH);
		ret = i2c_put_byte_add8_new(client, 0x0308, DGain_shutter);
		*******************/
		mdelay(100);
               sp2518_frmintervals_active.denominator 	= 5;
               sp2518_frmintervals_active.numerator	= 1;
		sp2518_h_active=1600;
		sp2518_v_active=1198;//1200;
		

		}
		set_flip(dev);
		printk( " 2011-----11-----30--------- \n ");

}    /* SP2518_set_resolution */

unsigned char v4l_2_sp2518(int val)
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

static int sp2518_setting(struct sp2518_device *dev,int PROP_ID,int value )
{
#if 1 //zyy test
	int ret=0;
	//unsigned char cur_val;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_sp2518(value));
		ret=i2c_put_byte_add8_new(client,0xdc,v4l_2_sp2518(value));
		break;
	case V4L2_CID_CONTRAST:
		ret=i2c_put_byte_add8_new(client,0xde, value);
		break;
	case V4L2_CID_SATURATION:
		ret=i2c_put_byte_add8_new(client,0xd9, value);
		break;
#if 0
	case V4L2_CID_EXPOSURE:
		ret=i2c_put_byte_add8_new(client,0xdc, value);
		break;
#endif
#if 0
	case V4L2_CID_HFLIP:    /* set flip on H. */
		ret=i2c_put_byte_add8_new(client,0x0101);
		if(ret>0) {
			cur_val=(char)ret;
			if(value!=0)
				cur_val=cur_val|0x1;
			else
				cur_val=cur_val&0xFE;
			ret=i2c_put_byte_add8_new(client,0x0101,cur_val);
			if(ret<0) dprintk(dev, 1, "V4L2_CID_HFLIP setting error\n");
		}  else {
			dprintk(dev, 1, "vertical read error\n");
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		ret=i2c_put_byte_add8_new(client,0x0101);
		if(ret>0) {
			cur_val=(char)ret;
			if(value!=0)
				cur_val=cur_val|0x02;
			else
				cur_val=cur_val&0xFD;
			ret=i2c_put_byte_add8_new(client,0x0101,cur_val);
		} else {
			dprintk(dev, 1, "vertical read error\n");
		}
		break;
#endif
	case V4L2_CID_DO_WHITE_BALANCE:
        if(sp2518_qctrl[0].default_value!=value){
			sp2518_qctrl[0].default_value=value;
			SP2518_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
        	}
		break;
	case V4L2_CID_EXPOSURE:
        if(sp2518_qctrl[1].default_value!=value){
			sp2518_qctrl[1].default_value=value;
			SP2518_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
        	}
		break;
	case V4L2_CID_COLORFX:
        if(sp2518_qctrl[2].default_value!=value){
			sp2518_qctrl[2].default_value=value;
			//SP2518_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
        	}
		break;
	case V4L2_CID_WHITENESS:
		 if(sp2518_qctrl[3].default_value!=value){
			sp2518_qctrl[3].default_value=value;
			printk("@@@SP_000:SP2518_set_param_banding,value=%d\n",value);
			SP2518_set_param_banding(dev,value);

			printk("@@@SP_111:sp2518_night_or_normal = %d",sp2518_night_or_normal);
			SP2518_set_night_mode(dev,sp2518_night_or_normal);	//add by sp_yjp,20120905
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
        	}
		break;
	case V4L2_CID_BLUE_BALANCE:
		 if(sp2518_qctrl[4].default_value!=value){
			sp2518_qctrl[4].default_value=value;
			printk("@@@SP_222:SP2518_set_night_mode,night mode=%d\n",value);
			printk("@@@SP_333:sp2518_night_or_normal = %d",sp2518_night_or_normal);
			SP2518_set_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
        	}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(sp2518_qctrl[5].default_value!=value){
			sp2518_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */         
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(sp2518_qctrl[7].default_value!=value){
			sp2518_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(sp2518_qctrl[8].default_value!=value){
			sp2518_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
	default:
		ret=-1;
		break;
	}
	return ret;
#endif
}

static void power_down_sp2518(struct sp2518_device *dev)
{

}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void sp2518_fillbuff(struct sp2518_fh *fh, struct sp2518_buffer *buf)
{
	struct sp2518_device *dev = fh->dev;
	void *vbuf = (void *)videobuf_to_res(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
	/*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	if(buf->canvas_id == 0)
           buf->canvas_id = convert_canvas_index(fh->fmt->fourcc, CAMERA_USER_CANVAS_INDEX+buf->vb.i*3);
	para.mirror = sp2518_qctrl[5].default_value&3;// not set
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = MAGIC_RE_MEM;
	para.zoom = sp2518_qctrl[7].default_value;
	para.angle = sp2518_qctrl[8].default_value;
	para.vaddr = (unsigned)vbuf;
	para.ext_canvas = buf->canvas_id;
        para.width = buf->vb.width;
        para.height = buf->vb.height;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void sp2518_thread_tick(struct sp2518_fh *fh)
{
	struct sp2518_buffer *buf;
	struct sp2518_device *dev = fh->dev;
	struct sp2518_dmaqueue *dma_q = &dev->vidq;

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
			 struct sp2518_buffer, vb.queue);
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
	sp2518_fillbuff(fh, buf);
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

static void sp2518_sleep(struct sp2518_fh *fh)
{
	struct sp2518_device *dev = fh->dev;
	struct sp2518_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	sp2518_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int sp2518_thread(void *data)
{
	struct sp2518_fh  *fh = data;
	struct sp2518_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		sp2518_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int sp2518_start_thread(struct sp2518_fh *fh)
{
	struct sp2518_device *dev = fh->dev;
	struct sp2518_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(sp2518_thread, fh, "sp2518");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void sp2518_stop_thread(struct sp2518_dmaqueue  *dma_q)
{
	struct sp2518_device *dev = container_of(dma_q, struct sp2518_device, vidq);

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
	struct sp2518_fh *fh = container_of(res, struct sp2518_fh, res);
	struct sp2518_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct sp2518_buffer *buf)
{
	struct videobuf_res_privdata *res = vq->priv_data;
	struct sp2518_fh *fh = container_of(res, struct sp2518_fh, res);
	struct sp2518_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);
       videobuf_waiton(vq, &buf->vb, 0, 0); 
	if (in_interrupt())
		BUG();

	videobuf_res_free(vq, &buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 1920
#define norm_maxh() 1600
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct videobuf_res_privdata *res = vq->priv_data;
	struct sp2518_fh *fh = container_of(res, struct sp2518_fh, res);
	struct sp2518_device    *dev = fh->dev;
	struct sp2518_buffer *buf = container_of(vb, struct sp2518_buffer, vb);
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
	struct sp2518_buffer    *buf  = container_of(vb, struct sp2518_buffer, vb);
	struct videobuf_res_privdata *res = vq->priv_data;
	struct sp2518_fh *fh = container_of(res, struct sp2518_fh, res);
	struct sp2518_device       *dev  = fh->dev;
	struct sp2518_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct sp2518_buffer   *buf  = container_of(vb, struct sp2518_buffer, vb);
	struct videobuf_res_privdata *res = vq->priv_data;
	struct sp2518_fh *fh = container_of(res, struct sp2518_fh, res);
	struct sp2518_device      *dev  = (struct sp2518_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops sp2518_video_qops = {
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
	struct sp2518_fh  *fh  = priv;
	struct sp2518_device *dev = fh->dev;

	strcpy(cap->driver, "sp2518");
	strcpy(cap->card, "sp2518.canvas");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = SP2518_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct sp2518_fmt *fmt;

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

    if(fival->index > ARRAY_SIZE(sp2518_frmivalenum))
        return -EINVAL;

    for(k =0; k< ARRAY_SIZE(sp2518_frmivalenum); k++)
    {
        if( (fival->index==sp2518_frmivalenum[k].index)&&
                (fival->pixel_format ==sp2518_frmivalenum[k].pixel_format )&&
                (fival->width==sp2518_frmivalenum[k].width)&&
                (fival->height==sp2518_frmivalenum[k].height)){
            memcpy( fival, &sp2518_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
            return 0;
        }
    }

    return -EINVAL;

}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct sp2518_fh *fh = priv;

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
	struct sp2518_fh  *fh  = priv;
	struct sp2518_device *dev = fh->dev;
	struct sp2518_fmt *fmt;
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
	struct sp2518_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct sp2518_device *dev = fh->dev;
	int ret;

	f->fmt.pix.width = (f->fmt.pix.width + (CANVAS_WIDTH_ALIGN-1) ) & (~(CANVAS_WIDTH_ALIGN-1));
	if ((f->fmt.pix.pixelformat==V4L2_PIX_FMT_YVU420) ||
			(f->fmt.pix.pixelformat==V4L2_PIX_FMT_YUV420)){
		f->fmt.pix.width = (f->fmt.pix.width + (CANVAS_WIDTH_ALIGN*2-1) ) & (~(CANVAS_WIDTH_ALIGN*2-1));
	}

	ret = vidioc_try_fmt_vid_cap(file, fh, f);
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
	set_flip(dev);
	if(f->fmt.pix.pixelformat==V4L2_PIX_FMT_RGB24){
		vidio_set_fmt_ticks=1;
		SP2518_set_resolution(dev,fh->height,fh->width);
		}
	else if(vidio_set_fmt_ticks==1){
		SP2518_set_resolution(dev,fh->height,fh->width);
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
    struct sp2518_fh *fh = priv;
    struct sp2518_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;
    //int ret;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = sp2518_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}


static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct sp2518_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sp2518_fh  *fh = priv;

        int ret = videobuf_querybuf(&fh->vb_vidq, p);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	if(ret == 0){
		p->reserved  = convert_canvas_index(fh->fmt->fourcc, CAMERA_USER_CANVAS_INDEX + p->index*3);
	}else{
		p->reserved = 0;
	}
#endif
	return ret;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sp2518_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sp2518_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct sp2518_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct sp2518_fh  *fh = priv;
	vdin_parm_t para;
        int ret = 0 ;
	printk(KERN_INFO " vidioc_streamon+++ \n ");	
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para ));
	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = sp2518_frmintervals_active.denominator;
	para.h_active = sp2518_h_active;
	para.v_active = sp2518_v_active;
	para.hsync_phase = 0;
	para.vsync_phase = 1;
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
        para.dfmt = TVIN_NV21;
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
	struct sp2518_fh  *fh = priv;

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
	struct sp2518_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(sp2518_prev_resolution))
			return -EINVAL;
		frmsize = &sp2518_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(sp2518_pic_resolution))
			return -EINVAL;
		frmsize = &sp2518_pic_resolution[fsize->index];
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
	struct sp2518_fh *fh = priv;
	struct sp2518_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct sp2518_fh *fh = priv;
	struct sp2518_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(sp2518_qctrl); i++)
		if (qc->id && qc->id == sp2518_qctrl[i].id) {
			memcpy(qc, &(sp2518_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct sp2518_fh *fh = priv;
	struct sp2518_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(sp2518_qctrl); i++)
		if (ctrl->id == sp2518_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct sp2518_fh *fh = priv;
	struct sp2518_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(sp2518_qctrl); i++)
		if (ctrl->id == sp2518_qctrl[i].id) {
			if (ctrl->value < sp2518_qctrl[i].minimum ||
			    ctrl->value > sp2518_qctrl[i].maximum ||
			    sp2518_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int sp2518_open(struct file *file)
{
	struct sp2518_device *dev = video_drvdata(file);
	struct sp2518_fh *fh = NULL;
	int retval = 0;
	resource_size_t mem_start = 0;
	unsigned int mem_size = 0;
#if CONFIG_CMA
    retval = vm_init_buf(16*SZ_1M);
    if(retval <0)
        return -1;
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif		

	aml_cam_init(&dev->cam_info);
	SP2518_init_regs(dev);
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
	fh->width    = 1600;//640//zyy
	fh->height   = 1198;//1200;//480
	fh->stream_on = 0 ;
	fh->f_flags  = file->f_flags;
	/* Resets frame counters */
	dev->jiffies = jiffies;

//    TVIN_SIG_FMT_CAMERA_640X480P_30Hz,
//    TVIN_SIG_FMT_CAMERA_800X600P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1024X768P_30Hz, // 190
//    TVIN_SIG_FMT_CAMERA_1920X1080P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1280X720P_30Hz,

	get_vm_buf_info(&mem_start, &mem_size, NULL);
	fh->res.start = mem_start;
	fh->res.end = mem_start+mem_size-1;
	fh->res.magic = MAGIC_RE_MEM;
	fh->res.priv = NULL;
	videobuf_queue_res_init(&fh->vb_vidq, &sp2518_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
	sizeof(struct sp2518_buffer), (void*)&fh->res, NULL);

	sp2518_start_thread(fh);
	sp2518_have_open = 1;
	return 0;
}

static ssize_t
sp2518_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct sp2518_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
sp2518_poll(struct file *file, struct poll_table_struct *wait)
{
	struct sp2518_fh        *fh = file->private_data;
	struct sp2518_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int sp2518_close(struct file *file)
{
	struct sp2518_fh         *fh = file->private_data;
	struct sp2518_device *dev       = fh->dev;
	struct sp2518_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);
	sp2518_have_open = 0;
	sp2518_stop_thread(vidq);
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
	sp2518_h_active=1600;//800//zyy
	sp2518_v_active=1198;//1200;//600
	sp2518_qctrl[0].default_value=0;
	sp2518_qctrl[1].default_value=4;
	sp2518_qctrl[2].default_value=0;
	sp2518_qctrl[3].default_value=0;
	sp2518_qctrl[4].default_value=0;

	sp2518_qctrl[5].default_value=0;
	sp2518_qctrl[7].default_value=100;
	sp2518_qctrl[8].default_value=0;
	sp2518_frmintervals_active.numerator = 1;
	sp2518_frmintervals_active.denominator = 15;
	power_down_sp2518(dev);
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

static int sp2518_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sp2518_fh  *fh = file->private_data;
	struct sp2518_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations sp2518_fops = {
	.owner		= THIS_MODULE,
	.open           = sp2518_open,
	.release        = sp2518_close,
	.read           = sp2518_read,
	.poll		= sp2518_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = sp2518_mmap,
};

static const struct v4l2_ioctl_ops sp2518_ioctl_ops = {
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

static struct video_device sp2518_template = {
	.name		= "sp2518_v4l",
	.fops           = &sp2518_fops,
	.ioctl_ops 	= &sp2518_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int sp2518_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_SP2518, 0);
}

static const struct v4l2_subdev_core_ops sp2518_core_ops = {
	.g_chip_ident = sp2518_g_chip_ident,
};

static const struct v4l2_subdev_ops sp2518_ops = {
	.core = &sp2518_core_ops,
};

static ssize_t sp2518_show(struct device *dev, struct device_attribute *attr, char *_buf)
{
	return sprintf(_buf, "0x%02x=0x%02x\n", cur_reg, cur_val);
}

static u32 strtol(const char *nptr, int base)
{
	u32 ret;
	if(!nptr || (base!=16 && base!=10 && base!=8))
	{
		printk("%s(): NULL pointer input\n", __FUNCTION__);
		return -1;
	}
	for(ret=0; *nptr; nptr++)
	{
		if((base==16 && *nptr>='A' && *nptr<='F') || 
			(base==16 && *nptr>='a' && *nptr<='f') || 
			(base>=10 && *nptr>='0' && *nptr<='9') ||
			(base>=8 && *nptr>='0' && *nptr<='7') )
		{
			ret *= base;
			if(base==16 && *nptr>='A' && *nptr<='F')
				ret += *nptr-'A'+10;
			else if(base==16 && *nptr>='a' && *nptr<='f')
				ret += *nptr-'a'+10;
			else if(base>=10 && *nptr>='0' && *nptr<='9')
				ret += *nptr-'0';
			else if(base>=8 && *nptr>='0' && *nptr<='7')
				ret += *nptr-'0';
		}
		else
			return ret;
	}
	return ret;
}

static ssize_t sp2518_store(struct device *dev,
					struct device_attribute *attr,
					const char *_buf, size_t _count)
{
	const char * p=_buf;
	u16 reg;
	u8 val;

	if(!strncmp(_buf, "get", strlen("get")))
	{
		p+=strlen("get");
		cur_reg=(u32)strtol(p, 16);
		val=i2c_get_byte_add8(g_i2c_client, cur_reg);
		printk("%s(): get 0x%04x=0x%02x\n", __FUNCTION__, cur_reg, val);
        cur_val=val;
	}
	else if(!strncmp(_buf, "put", strlen("put")))
	{
		p+=strlen("put");
		reg=strtol(p, 16);
		p=strchr(_buf, '=');
		if(p)
		{
			++ p;
			val=strtol(p, 16);
			i2c_put_byte_add8_new(g_i2c_client, reg, val);
			printk("%s(): set 0x%04x=0x%02x\n", __FUNCTION__, reg, val);
		}
		else
			printk("%s(): Bad string format input!\n", __FUNCTION__);
	}
	else
		printk("%s(): Bad string format input!\n", __FUNCTION__);
	
	return _count;
} 

static ssize_t name_show(struct device *dev, struct device_attribute *attr, char *_buf)
{
    strcpy(_buf, "SP2518");
    return 4;
}

static struct device *sp2518_dev = NULL;
static struct class *  sp2518_class = NULL;
static DEVICE_ATTR(sp2518, 0666, sp2518_show, sp2518_store);
static DEVICE_ATTR(name, 0666, name_show, NULL);

#define  EMDOOR_DEBUG_SP2518	1  
#ifdef EMDOOR_DEBUG_SP2518
unsigned int sp2518_reg_addr;
static struct i2c_client *sp2518_client;

static ssize_t sp2518_show_mine(struct kobject *kobj, struct kobj_attribute *attr,			
	       char *buf)
{	
	unsigned  char dat;
	dat = i2c_get_byte_add8(sp2518_client, sp2518_reg_addr);
	return sprintf(buf, "REG[0x%x]=0x%x\n", sp2518_reg_addr, dat);
}

static ssize_t sp2518_store_mine(struct kobject *kobj, struct kobj_attribute *attr,			 
	      const char *buf, size_t count)
{	
	int tmp;
	unsigned short reg;
	unsigned char val;
	tmp = simple_strtoul(buf, NULL, 16);
	//sscanf(buf, "%du", &tmp);
	if(tmp < 0xff){
		sp2518_reg_addr = tmp;
	}
	else {
		reg = (tmp >> 8) & 0xFFFF; //reg
		sp2518_reg_addr = reg;
		val = tmp & 0xFF;        //val
		i2c_put_byte_add8_new(sp2518_client, reg, val);
	}
	
	return count;
}


static struct kobj_attribute sp2518_attribute =	__ATTR(sp2518, 0666, sp2518_show_mine, sp2518_store_mine);


static struct attribute *sp2518_attrs[] = {	
	&sp2518_attribute.attr,	
	NULL,	
};


static const struct attribute_group sp2518_group =
{
	.attrs = sp2518_attrs,
};
#endif	

static int sp2518_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	struct sp2518_device *t;
	struct v4l2_subdev *sd;
	aml_cam_info_t* plat_dat;
	vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	//modify correct i2c addr--- 0x30
	client->addr = 0x30;
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &sp2518_ops);
	plat_dat= (aml_cam_info_t*)client->dev.platform_data;

	/* test if devices exist. */

	/* Now create a video4linux device */
	mutex_init(&t->mutex);

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &sp2518_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);

	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "spa2518");
	/* Register it */
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if (plat_dat->front_back >=0)  video_nr = plat_dat->front_back;
	} else {
	    printk("camera sp2518: have no platform data\n");
           kfree(t);
	    return -1; 	
	}
	
	t->cam_info.version = SP2518_DRIVER_VERSION;
	if (aml_cam_info_reg(&t->cam_info) < 0)
		printk("reg caminfo error\n");
	
	err = video_register_device(t->vdev, VFL_TYPE_GRABBER, video_nr);
	if (err < 0) {
		video_device_release(t->vdev);
		kfree(t);
		return err;
	}


    sp2518_class = class_create(THIS_MODULE, "sp2518");
    if (IS_ERR(sp2518_class)) 
    {
        printk("Create class sp2518.\n");
        return -ENOMEM;
    }
    sp2518_dev = device_create(sp2518_class, NULL, MKDEV(0, 1), NULL, "dev");
    device_create_file(sp2518_dev, &dev_attr_sp2518);
    device_create_file(sp2518_dev, &dev_attr_name);

    g_i2c_client=client;
	
#ifdef EMDOOR_DEBUG_SP2518
	sp2518_client = client;
	err = sysfs_create_group(&client->dev.kobj, &sp2518_group);
#endif	

    return 0;
}

static int sp2518_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sp2518_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}


static const struct i2c_device_id sp2518_id[] = {
	{ "sp2518", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sp2518_id);

static struct i2c_driver sp2518_i2c_driver = {
	.driver = {
		.name = "sp2518",
	},
	.probe = sp2518_probe,
	.remove = sp2518_remove,
	.id_table = sp2518_id,
};

module_i2c_driver(sp2518_i2c_driver);

