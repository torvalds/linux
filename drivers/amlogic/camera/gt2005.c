/*
 *gt2005 - This code emulates a real video device with v4l2 api
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

#define GT2005_CAMERA_MODULE_NAME "gt2005"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define GT2005_CAMERA_MAJOR_VERSION 0
#define GT2005_CAMERA_MINOR_VERSION 7
#define GT2005_CAMERA_RELEASE 0
#define GT2005_CAMERA_VERSION \
	KERNEL_VERSION(GT2005_CAMERA_MAJOR_VERSION, GT2005_CAMERA_MINOR_VERSION, GT2005_CAMERA_RELEASE)

unsigned int  DGain_shutter,AGain_shutter,DGain_shutterH,DGain_shutterL,AGain_shutterH,AGain_shutterL,shutterH,shutterL,shutter;
unsigned short UXGA_Cap = 0;

MODULE_DESCRIPTION("gt2005 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define GT2005_DRIVER_VERSION "GT2005-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

extern int disable_gt2005;

static int gt2005_h_active=800;
static int gt2005_v_active=600;
static struct v4l2_fract gt2005_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

static int gt2005_have_open=0;
static struct vdin_v4l2_ops_s *vops;
/* supported controls */
static struct v4l2_queryctrl gt2005_qctrl[] = {
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

static struct v4l2_frmivalenum gt2005_frmivalenum[]={
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

struct v4l2_querymenu gt2005_qmenu_wbmode[] = {
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

struct v4l2_querymenu gt2005_qmenu_anti_banding_mode[] = {
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
    struct v4l2_querymenu* gt2005_qmenu;
}gt2005_qmenu_set_t;

gt2005_qmenu_set_t gt2005_qmenu_set[] = {
    {
        .id         	= V4L2_CID_DO_WHITE_BALANCE,
        .num            = ARRAY_SIZE(gt2005_qmenu_wbmode),
        .gt2005_qmenu   = gt2005_qmenu_wbmode,
    },{
        .id         	= V4L2_CID_POWER_LINE_FREQUENCY,
        .num            = ARRAY_SIZE(gt2005_qmenu_anti_banding_mode),
        .gt2005_qmenu   = gt2005_qmenu_anti_banding_mode,
    },
};

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(gt2005_qmenu_set); i++)
	if (a->id && a->id == gt2005_qmenu_set[i].id) {
	    for(j = 0; j < gt2005_qmenu_set[i].num; j++)
		if (a->index == gt2005_qmenu_set[i].gt2005_qmenu[j].index) {
			memcpy(a, &( gt2005_qmenu_set[i].gt2005_qmenu[j]),
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

struct gt2005_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct gt2005_fmt formats[] = {
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

static struct gt2005_fmt *get_format(struct v4l2_format *f)
{
	struct gt2005_fmt *fmt;
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
struct gt2005_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct gt2005_fmt        *fmt;
};

struct gt2005_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(gt2005_devicelist);

struct gt2005_device {
	struct list_head			gt2005_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct gt2005_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(gt2005_qctrl)];
};

static inline struct gt2005_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gt2005_device, sd);
}

struct gt2005_fh {
	struct gt2005_device            *dev;

	/* video capture */
	struct gt2005_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};

/*static inline struct gt2005_fh *to_fh(struct gt2005_device *dev)
{
	return container_of(dev, struct gt2005_fh, dev);
}*/

static struct v4l2_frmsize_discrete gt2005_prev_resolution[]= //should include 352x288 and 640x480, those two size are used for recording
{
	{320,240},
	{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete gt2005_pic_resolution[]=
{
	{1600,1200},
	{800,600}
};

/* ------------------------------------------------------------------
	reg spec of GT2005
   ------------------------------------------------------------------*/

struct aml_camera_i2c_fig_s GT2005_script[] = {
	{0x0101 , 0x00},
	{0x0102 , 0x01},
	{0x0103 , 0x00},

	//Hcount&Vcount
	{0x0105 , 0x00},
	{0x0106 , 0xF0},
	{0x0107 , 0x00},
	{0x0108 , 0x1C},

	//Binning&Resoultion
	{0x0109 , 0x00},
	{0x010A , 0x04},
	{0x010B , 0x0F},
	{0x010C , 0x00},
	{0x010D , 0x08},
	{0x010E , 0x00},
	{0x010F , 0x0A},
	{0x0110 , 0x03},
	{0x0111 , 0x20},
	{0x0112 , 0x02},
	{0x0113 , 0x5a},// 5a

	//YUV Mode
	{0x0114 , 0x06},

	//Picture Effect
	{0x0115 , 0x00},//00

	//PLL&Frame Rate
/*	{0x0116 , 0x02},
	{0x0117 , 0x00},
	{0x0118 , 0x34},
	{0x0119 , 0x01},
	{0x011A , 0x04},
	{0x011B , 0x00},
	*/

		//PLL&Frame Rate
	//{0x0116 , 0x01},
	{0x0116 , 0x02},
	{0x0117 , 0x00},
	//{0x0118 , 0x34},
	{0x0118 , 0x40},//  0x60
	{0x0119 , 0x01},
	{0x011A , 0x04},
	{0x011B , 0x00},

	//DCLK Polarity
	{0x011C , 0x00},//00

	//Do not change
	{0x011D , 0x02},
	{0x011E , 0x00},

	{0x011F , 0x00},
	{0x0120 , 0x1C},
	{0x0121 , 0x00},
	{0x0122 , 0x04},
	{0x0123 , 0x00},
	{0x0124 , 0x00},
	{0x0125 , 0x00},
	{0x0126 , 0x00},
	{0x0127 , 0x00},
	{0x0128 , 0x00},

	//Contrast
	{0x0200 , 0x08},//0x30

	//Brightness
	{0x0201 , 0x00}, //   

	//Saturation
	{0x0202 , 0x40},  //

 	//Do not change
	{0x0203 , 0x00},
	{0x0204 , 0x78},//0x03
	{0x0205 , 0x1F},
	{0x0206 , 0x0B},
	{0x0207 , 0x20},
	{0x0208 , 0x00},
	{0x0209 , 0x2A},
	{0x020A , 0x01},

	//Sharpness
	{0x020B , 0x68},
	{0x020C , 0x84},

	//Do not change
	{0x020D , 0xC8},
	{0x020E , 0xBC},
	{0x020F , 0x08},
	{0x0210 , 0xf0},// f6  20111130
	{0x0211 , 0x00},
	{0x0212 , 0x20},
	{0x0213 , 0x81},
	{0x0214 , 0x15},
	{0x0215 , 0x00},
	{0x0216 , 0x00},
	{0x0217 , 0x00},
	{0x0218 , 0x46},
	{0x0219 , 0x30},
	{0x021A , 0x03},
	{0x021B , 0x28},
	{0x021C , 0x02},
	{0x021D , 0x60},
	{0x021E , 0x00},
	{0x021F , 0x00},

	{0x0220 , 0x10},//08
	{0x0221 , 0x10},//08
	{0x0222 , 0x10},//04
	{0x0223 , 0x10},//00
	{0x0224 , 0x1F},
	{0x0225 , 0x1E},
	{0x0226 , 0x18},
	{0x0227 , 0x1D},
	{0x0228 , 0x1F},
	{0x0229 , 0x1F},
	{0x022A , 0x01},
	{0x022B , 0x04},
	{0x022C , 0x05},
	{0x022D , 0x05},
	{0x022E , 0x04},
	{0x022F , 0x03},
	{0x0230 , 0x02},
	{0x0231 , 0x1F},
	{0x0232 , 0x1A},
	{0x0233 , 0x19},
	{0x0234 , 0x19},
	{0x0235 , 0x1B},
	{0x0236 , 0x1F},
	{0x0237 , 0x04},
	{0x0238 , 0xEE},
	{0x0239 , 0xFF},
	{0x023A , 0x00},
	{0x023B , 0x00},
	{0x023C , 0x00},
	{0x023D , 0x00},
	{0x023E , 0x00},
	{0x023F , 0x00},
	{0x0240 , 0x00},
	{0x0241 , 0x00},
	{0x0242 , 0x00},
	{0x0243 , 0x21},
	{0x0244 , 0x42},
	{0x0245 , 0x53},
	{0x0246 , 0x54},
	{0x0247 , 0x54},
	{0x0248 , 0x54},
	{0x0249 , 0x33},
	{0x024A , 0x11},
	{0x024B , 0x00},
	{0x024C , 0x00},
	{0x024D , 0xFF},
	{0x024E , 0xEE},
	{0x024F , 0xDD},
	{0x0250 , 0x00},
	{0x0251 , 0x00},
	{0x0252 , 0x00},
	{0x0253 , 0x00},
	{0x0254 , 0x00},
	{0x0255 , 0x00},
	{0x0256 , 0x00},
	{0x0257 , 0x00},
	{0x0258 , 0x00},
	{0x0259 , 0x00},
	{0x025A , 0x00},
	{0x025B , 0x00},
	{0x025C , 0x00},
	{0x025D , 0x00},
	{0x025E , 0x00},
	{0x025F , 0x00},
	{0x0260 , 0x00},
	{0x0261 , 0x00},
	{0x0262 , 0x00},
	{0x0263 , 0x00},
	{0x0264 , 0x00},
	{0x0265 , 0x00},
	{0x0266 , 0x00},
	{0x0267 , 0x00},
	{0x0268 , 0x93},//8f r2cr
	{0x0269 , 0x94},//a3 g2cr
	{0x026A , 0xB0},//b4 b2cb
	{0x026B , 0x90},//90  b2cb

	{0x026C , 0x00},
	{0x026D , 0xD0},
	{0x026E , 0x60},
	{0x026F , 0xA0},
	{0x0270 , 0x40},
	{0x0300 , 0x81},
	{0x0301 , 0x80},  // 80
	{0x0302 , 0x22},
	{0x0303 , 0x06},
	{0x0304 , 0x03},
	{0x0305 , 0x83},
	{0x0306 , 0x00},
	{0x0307 , 0x22},
	{0x0308 , 0x00},
	{0x0309 , 0x55},
	{0x030A , 0x55},
	{0x030B , 0x55},
	{0x030C , 0x54},
	{0x030D , 0x1F},
	{0x030E , 0x13},
	{0x030F , 0x10},
	{0x0310 , 0x04},
	{0x0311 , 0xFF},
	//{0x0312 , 0x98},// 08
	{0x0312 , 0x98},
	//Banding Setting{50Hz}

	{0x0313 , 0x35},
	{0x0314 , 0x35},  // 24M MCLK

	{0x0315 , 0x16},
	{0x0316 , 0x26},

	{0x0317 , 0x02},
	{0x0318 , 0x08},
	{0x0319 , 0x0C},

	//AWB
	//A LIGHT CORRECTION
	/*{0x031A , 0x81},
	{0x031B , 0x00},
	{0x031C , 0x1D},
	{0x031D , 0x00},
	{0x031E , 0xFD},
	{0x031F , 0x00},
	{0x0320 , 0xE1},
	{0x0321 , 0x1A},
	{0x0322 , 0xDE},
	{0x0323 , 0x11},
	{0x0324 , 0x1A},
	{0x0325 , 0xEE},
	{0x0326 , 0x50},
	{0x0327 , 0x18},
	{0x0328 , 0x25},
	{0x0329 , 0x37},
	{0x032A , 0x24},
	{0x032B , 0x32},
	{0x032C , 0xA9},
	{0x032D , 0x32},
	{0x032E , 0xFF},
	{0x032F , 0x7F},
	{0x0330 , 0xBA},
	{0x0331 , 0x7F},
	{0x0332 , 0x7F},
	{0x0333 , 0x14},
	{0x0334 , 0x81},
	{0x0335 , 0x14},
	{0x0336 , 0xFF},
	{0x0337 , 0x20},
	{0x0338 , 0x46},
	{0x0339 , 0x04},
	{0x033A , 0x04},
	{0x033B , 0x00},
	{0x033C , 0x00},
	{0x033D , 0x00},

	//Do not change
	{0x033E , 0x03},
	{0x033F , 0x28},
	{0x0340 , 0x02},
	{0x0341 , 0x60},
	{0x0342 , 0xAC},
	{0x0343 , 0x97},
	{0x0344 , 0x7F},
	{0x0400 , 0xE8},
	{0x0401 , 0x40},
	{0x0402 , 0x00},
	{0x0403 , 0x00},
	{0x0404 , 0xF8},
	{0x0405 , 0x03},
	{0x0406 , 0x03},
	{0x0407 , 0x85},
	{0x0408 , 0x44},
	{0x0409 , 0x1F},
	{0x040A , 0x40},
	{0x040B , 0x31},// 33,
*/
	//Lens Shading Correction{Default Sunny}
/*
	{0x040C , 0xA0},
	{0x040D , 0x00},
	{0x040E , 0x00},
	{0x040F , 0x00},
	{0x0410 , 0x02},
	{0x0411 , 0x02},
	{0x0412 , 0x00},
	{0x0413 , 0x00},
	{0x0414 , 0x00},
	{0x0415 , 0x00},
	{0x0416 , 0x00},
	{0x0417 , 0x04},
	{0x0418 , 0x02},
	{0x0419 , 0x02},
	{0x041a , 0x00},
	{0x041b , 0x00},
	{0x041c , 0x04},
	{0x041d , 0x04},
	{0x041e , 0x00},
	{0x041f , 0x00},
	{0x0420 , 0x2A},
	{0x0421 , 0x2A},
	{0x0422 , 0x27},
	{0x0423 , 0x22},
	{0x0424 , 0x2A}, 
	{0x0425 , 0x2A},
	{0x0426 , 0x27},
	{0x0427 , 0x22},
	{0x0428 , 0x07},
	{0x0429 , 0x07},
	{0x042a , 0x08},
	{0x042b , 0x06},
	{0x042c , 0x11},
	{0x042d , 0x11},
	{0x042e , 0x0F},
	{0x042f , 0x0D},
	{0x0430 , 0x00},
	{0x0431 , 0x00},
	{0x0432 , 0x00},
	{0x0433 , 0x00},
	{0x0434 , 0x00},
	{0x0435 , 0x00},
	{0x0436 , 0x00},
	{0x0437 , 0x00},
	{0x0438 , 0x00},
	{0x0439 , 0x00},
	{0x043a , 0x00},
	{0x043b , 0x00},
	{0x043c , 0x00},
	{0x043d , 0x00},
	{0x043e , 0x00},
	{0x043f , 0x00},

	//PWB Gain
	{0x0440 , 0x00},
	{0x0441 , 0x5F},
	{0x0442 , 0x00},
	{0x0443 , 0x00},
	{0x0444 , 0x40}, //0x17 kim 
*/

//above old para
//next huaxin-lens related
	{0x031A,0x81},
	{0x031B,0x00},
	{0x031C,0x3D},
	{0x031D,0x00},
	{0x031E,0xF9},
	{0x031F,0x00},
	{0x0320,0x24},
	{0x0321,0x14},
	{0x0322,0x1A},
	{0x0323,0x24},
	{0x0324,0x08},
	{0x0325,0xF0},
	{0x0326,0x30},
	{0x0327,0x17},
	{0x0328,0x11},
	{0x0329,0x22},
	{0x032A,0x2F},
	{0x032B,0x21},
	{0x032C,0xDA},
	{0x032D,0x10},
	{0x032E,0xEA},
	{0x032F,0x18},
	{0x0330,0x29},
	{0x0331,0x25},
	{0x0332,0x12},
	{0x0333,0x0F},
	{0x0334,0xE0},
	{0x0335,0x13},
	{0x0336,0xFF},
	{0x0337,0x20},
	{0x0338 , 0x46},
	{0x0339,0x04},
	{0x033A,0x04},
	{0x033B,0xFF},
	{0x033C,0x01},
	{0x033D,0x00},
	//Do not change
	{0x033E,0x03},
	{0x033F,0x28},
	{0x0340,0x02},
	{0x0341,0x60},
	{0x0342 , 0xAC},
	{0x0343 , 0x97},
	{0x0344 , 0x7F},
	{0x0400,0xE8},
	{0x0401,0x40},
	{0x0402,0x00},
	{0x0403,0x00},
	{0x0404,0xF8},
	{0x0405,0x03},
	{0x0406,0x03},
	{0x0407,0x85},
	{0x0408,0x44},
	{0x0409,0x1F},
	{0x040A,0x40},
	{0x040B , 0x33},// 33,*/

	//Lens Shading Correction

	{0x040C,0xA0},
	{0x040D,0x00},
	{0x040E,0x00},
	{0x040F,0x00},
	{0x0410 , 0x06},//d
	{0x0411 , 0x06},//d
	{0x0412 , 0x06},
	{0x0413 , 0x0a},//04
	{0x0414 , 0x00},
	{0x0415 , 0x00},
	{0x0416 , 0x03},//7
	{0x0417 , 0x09},
	{0x0418 , 0x16},
	{0x0419 , 0x14},
	{0x041A , 0x11},
	{0x041B , 0x14},
	{0x041C , 0x07},
	{0x041D, 0x07},
	{0x041E, 0x06},
	{0x041F,0x02},
	{0x0420 , 0x42},
	{0x0421 , 0x42},
	{0x0422 , 0x47},
	{0x0423 , 0x39},
	{0x0424 , 0x3E},
	{0x0425 , 0x4D},
	{0x0426 , 0x46},
	{0x0427 , 0x3A},
	{0x0428 , 0x21},
	{0x0429 , 0x21},
	{0x042A , 0x26},
	{0x042B , 0x1C},
	{0x042C , 0x25},
	{0x042D , 0x25},
	{0x042E , 0x28},
	{0x042F , 0x20},
	{0x0430 , 0x3E},
	{0x0431 , 0x3E},
	{0x0432 , 0x33},
	{0x0433 , 0x2E},
	{0x0434 , 0x54},
	{0x0435 , 0x53},
	{0x0436 , 0x3C},
	{0x0437 , 0x51},
	{0x0438 , 0x2B},
	{0x0439 , 0x2B},
	{0x043A , 0x38},
	{0x043B , 0x22},
	{0x043C , 0x3B},
	{0x043D , 0x3B},
	{0x043E , 0x31},
	{0x043F , 0x37},

	//PWB Gain
	{0x0440,0x00},
	{0x0441 , 0x4b},  //4b
	{0x0442,0x00},
	{0x0443,0x00},
	{0x0444 , 0x31},

	{0x0445 , 0x00},
	{0x0446 , 0x00},
	{0x0447 , 0x00},
	{0x0448 , 0x00},
	{0x0449 , 0x00},
	{0x044A , 0x00},
	{0x044D , 0xE0},
	{0x044E , 0x05},
	{0x044F , 0x07},
	{0x0450 , 0x00},
	{0x0451 , 0x00},
	{0x0452 , 0x00},
	{0x0453 , 0x00},
	{0x0454 , 0x00},
	{0x0455 , 0x00},
	{0x0456 , 0x00},
	{0x0457 , 0x00},
	{0x0458 , 0x00},
	{0x0459 , 0x00},
	{0x045A , 0x00},
	{0x045B , 0x00},
	{0x045C , 0x00},
	{0x045D , 0x00},
	{0x045E , 0x00},
	{0x045F , 0x00},

	//GAMMA Correction
	/*{0x0460 , 0x80},
	{0x0461 , 0x10},//10
	{0x0462 , 0x10},
	{0x0463 , 0x10},
	{0x0464 , 0x08},
	{0x0465 , 0x08},
	{0x0466 , 0x11},
	{0x0467 , 0x09},
	{0x0468 , 0x23},
	{0x0469 , 0x2A},
	{0x046A , 0x2A},
	{0x046B , 0x47},
	{0x046C , 0x52},
	{0x046D , 0x42},
	{0x046E , 0x36},
	{0x046F , 0x46},
	{0x0470 , 0x3A},
	{0x0471 , 0x32},
	{0x0472 , 0x32},
	{0x0473 , 0x38},
	{0x0474 , 0x3D},
	{0x0475 , 0x2F},
	{0x0476 , 0x29},
	{0x0477 , 0x48},*/

	{0x0460 , 0x80},
	{0x0461 , 0x10},//10
	{0x0462 , 0x10},
	{0x0463 , 0x10},
	{0x0464 , 0x08},
	{0x0465 , 0x08},
	{0x0466 , 0x11},
	{0x0467 , 0x09},
	{0x0468 , 0x23},
	{0x0469 , 0x2A},
	{0x046A , 0x2A},
	{0x046B , 0x47},
	{0x046C , 0x52},
	{0x046D , 0x42},
	{0x046E , 0x36},
	{0x046F , 0x46},
	{0x0470 , 0x3A},
	{0x0471 , 0x32},
	{0x0472 , 0x32},
	{0x0473 , 0x38},
	{0x0474 , 0x3D},
	{0x0475 , 0x2F},
	{0x0476 , 0x29},
	{0x0477 , 0x48},

	//Do not change
	{0x0600 , 0x00},
	{0x0601 , 0x24},
	{0x0602 , 0x45},
	{0x0603 , 0x0E},
	{0x0604 , 0x14},
	{0x0605 , 0x2F},
	{0x0606 , 0x01},
	{0x0607 , 0x0E},
	{0x0608 , 0x0E},
	{0x0609 , 0x37},
	{0x060A , 0x18},
	{0x060B , 0xA0},
	{0x060C , 0x20},
	{0x060D , 0x07},
	{0x060E , 0x47},
	{0x060F , 0x90},
	{0x0610 , 0x06},
	{0x0611 , 0x0C},
	{0x0612 , 0x28},
	{0x0613 , 0x13},
	{0x0614 , 0x0B},
	{0x0615 , 0x10},
	{0x0616 , 0x14},
	{0x0617 , 0x19},
	{0x0618 , 0x52},
	{0x0619 , 0xA0},
	{0x061A , 0x11},
	{0x061B , 0x33},
	{0x061C , 0x56},
	{0x061D , 0x20},
	{0x061E , 0x28},
	{0x061F , 0x2B},
	{0x0620 , 0x22},
	{0x0621 , 0x11},
	{0x0622 , 0x75},
	{0x0623 , 0x49},
	{0x0624 , 0x6E},
	{0x0625 , 0x80},
	{0x0626 , 0x02},
	{0x0627 , 0x0C},
	{0x0628 , 0x51},
	{0x0629 , 0x25},
	{0x062A , 0x01},
	{0x062B , 0x3D},
	{0x062C , 0x04},
	{0x062D , 0x01},
	{0x062E , 0x0C},
	{0x062F , 0x2C},
	{0x0630 , 0x0D},
	{0x0631 , 0x14},
	{0x0632 , 0x12},
	{0x0633 , 0x34},
	{0x0634 , 0x00},
	{0x0635 , 0x00},
	{0x0636 , 0x00},
	{0x0637 , 0xB1},
	{0x0638 , 0x22},
	{0x0639 , 0x32},
	{0x063A , 0x0E},
	{0x063B , 0x18},
	{0x063C , 0x88},
	{0x0640 , 0xB2},
	{0x0641 , 0xC0},
	{0x0642 , 0x01},
	{0x0643 , 0x26},
	{0x0644 , 0x13},
	{0x0645 , 0x88},
	{0x0646 , 0x64},
	{0x0647 , 0x00},
	{0x0681 , 0x1B},
	{0x0682 , 0xA0},
	{0x0683 , 0x28},
	{0x0684 , 0x00},
	{0x0685 , 0xB0},
	{0x0686 , 0x6F},
	{0x0687 , 0x33},
	{0x0688 , 0x1F},
	{0x0689 , 0x44},
	{0x068A , 0xA8},
	{0x068B , 0x44},
	{0x068C , 0x08},
	{0x068D , 0x08},
	{0x068E , 0x00},
	{0x068F , 0x00},
	{0x0690 , 0x01},
	{0x0691 , 0x00},
	{0x0692 , 0x01},
	{0x0693 , 0x00},
	{0x0694 , 0x00},
	{0x0695 , 0x00},
	{0x0696 , 0x00},
	{0x0697 , 0x00},
	{0x0698 , 0x2A},
	{0x0699 , 0x80},
	{0x069A , 0x1F},
	{0x069B , 0x00},
	{0x069C , 0x02},
	{0x069D , 0xF5},
	{0x069E , 0x03},
	{0x069F , 0x6D},
	{0x06A0 , 0x0C},
	{0x06A1 , 0xB8},
	{0x06A2 , 0x0D},
	{0x06A3 , 0x74},
	{0x06A4 , 0x00},
	{0x06A5 , 0x2F},
	{0x06A6 , 0x00},
	{0x06A7 , 0x2F},
	{0x0F00 , 0x00},
	{0x0F01 , 0x00},

	//Output Enable
	{0x0100 , 0x01},
	{0x0102 , 0x02},
	{0x0104 , 0x03},
	{0xffff,0xff},
};

//load GT2005 parameters
void GT2005_init_regs(struct gt2005_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
        int i=0;

	while (1) {
		if (GT2005_script[i].val==0xff&&GT2005_script[i].addr==0xffff)
		{
			printk("GT2005_write_regs success in initial GT2005.\n");
			break;
		}
		if((i2c_put_byte(client,GT2005_script[i].addr, GT2005_script[i].val)) < 0)
		{
	 		printk("fail in initial GT2005. \n");
			return;
		}
		i++;
	}
	
	return;
}
/*************************************************************************
* FUNCTION
*    GT2005_set_param_wb
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
void GT2005_set_param_wb(struct gt2005_device *dev,enum  camera_wb_flip_e para)//white balance
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	switch (para)
	{

		case CAM_WB_AUTO://auto
			/*i2c_put_byte(client,0x031a , 0x81);
			i2c_put_byte(client,0x0320 , 0x24);
			i2c_put_byte(client,0x0321 , 0x14);
			i2c_put_byte(client,0x0322 , 0x1a);
			i2c_put_byte(client,0x0323 , 0x24);
			i2c_put_byte(client,0x0441 , 0x4B);
			i2c_put_byte(client,0x0442 , 0x00);
			i2c_put_byte(client,0x0443 , 0x00);
			i2c_put_byte(client,0x0444 , 0x31);*/
// huaxin-lens update
			i2c_put_byte(client,0x031a , 0x81);
			i2c_put_byte(client,0x0320 , 0x24);
			i2c_put_byte(client,0x0321 , 0x14);
			i2c_put_byte(client,0x0322 , 0x1a);
			i2c_put_byte(client,0x0323 , 0x24);
			i2c_put_byte(client,0x0441 , 0x4f);// 0x4b
			i2c_put_byte(client,0x0442 , 0x00);
			i2c_put_byte(client,0x0443 , 0x00);
			i2c_put_byte(client,0x0444 , 0x31);
			break;

		case CAM_WB_CLOUD: //cloud
			i2c_put_byte(client,0x0320 , 0x02);
			i2c_put_byte(client,0x0321 , 0x02);
			i2c_put_byte(client,0x0322 , 0x02);
			i2c_put_byte(client,0x0323 , 0x02);
			i2c_put_byte(client,0x0441 , 0x80);
			i2c_put_byte(client,0x0442 , 0x00);
			i2c_put_byte(client,0x0443 , 0x00);
			i2c_put_byte(client,0x0444 , 0x0D);
			break;

		case CAM_WB_DAYLIGHT: //
			i2c_put_byte(client,0x0320 , 0x02);
			i2c_put_byte(client,0x0321 , 0x02);
			i2c_put_byte(client,0x0322 , 0x02);
			i2c_put_byte(client,0x0323 , 0x02);
			i2c_put_byte(client,0x0441 , 0x60);
			i2c_put_byte(client,0x0442 , 0x00);
			i2c_put_byte(client,0x0443 , 0x00);
			i2c_put_byte(client,0x0444 , 0x14);
			break;

		case CAM_WB_INCANDESCENCE:
			i2c_put_byte(client,0x0320 , 0x02);
			i2c_put_byte(client,0x0321 , 0x02);
			i2c_put_byte(client,0x0322 , 0x02);
			i2c_put_byte(client,0x0323 , 0x02);
			i2c_put_byte(client,0x0441 , 0x50);
			i2c_put_byte(client,0x0442 , 0x00);
			i2c_put_byte(client,0x0443 , 0x00);
			i2c_put_byte(client,0x0444 , 0x30);
			break;

		case CAM_WB_WARM_FLUORESCENT:
			i2c_put_byte(client,0x0320 , 0x02);
			i2c_put_byte(client,0x0321 , 0x02);
			i2c_put_byte(client,0x0322 , 0x02);
			i2c_put_byte(client,0x0323 , 0x02);
			i2c_put_byte(client,0x0441 , 0x0B);
			i2c_put_byte(client,0x0442 , 0x00);
			i2c_put_byte(client,0x0443 , 0x00);
			i2c_put_byte(client,0x0444 , 0x5E);
			break;

		case CAM_WB_FLUORESCENT:
			i2c_put_byte(client,0x0320 , 0x02);
			i2c_put_byte(client,0x0321 , 0x02);
			i2c_put_byte(client,0x0322 , 0x02);
			i2c_put_byte(client,0x0323 , 0x02);
			i2c_put_byte(client,0x0441 , 0x43);
			i2c_put_byte(client,0x0442 , 0x00);
			i2c_put_byte(client,0x0443 , 0x00);
			i2c_put_byte(client,0x0444 , 0x4B);
			break;

		case CAM_WB_MANUAL:
		    	// TODO
			break;
		default:
			break;
	}


} /* GT2005_set_param_wb */
/*************************************************************************
* FUNCTION
*    GT2005_set_param_exposure
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
void GT2005_set_param_exposure(struct gt2005_device *dev,enum camera_exposure_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    switch (para)
	{
		case EXPOSURE_N4_STEP:
			i2c_put_byte(client,0x0300 , 0x81);
			i2c_put_byte(client,0x0301 , 0x40);//40
			i2c_put_byte(client,0x0201 , 0x90);
			break;
		case EXPOSURE_N3_STEP:
			i2c_put_byte(client,0x0300 , 0x81);
			i2c_put_byte(client,0x0301 , 0x50);
			i2c_put_byte(client,0x0201 , 0xa0);
			break;
		case EXPOSURE_N2_STEP:
			i2c_put_byte(client,0x0300 , 0x81);
			i2c_put_byte(client,0x0301 , 0x60);
			i2c_put_byte(client,0x0201 , 0xb0);
			break;
		case EXPOSURE_N1_STEP:
			i2c_put_byte(client,0x0300 , 0x81);
			i2c_put_byte(client,0x0301 , 0x70);
			i2c_put_byte(client,0x0201 , 0xd0);
			break;		
		case EXPOSURE_0_STEP:
			i2c_put_byte(client,0x0300 , 0x81);
			i2c_put_byte(client,0x0301 , 0x80);
			i2c_put_byte(client,0x0201 , 0x00);
			break;		
		case EXPOSURE_P1_STEP:
			i2c_put_byte(client,0x0300 , 0x81);
			i2c_put_byte(client,0x0301 , 0x90);
			i2c_put_byte(client,0x0201 , 0x10);
			break;	
		case EXPOSURE_P2_STEP:
			i2c_put_byte(client,0x0300 , 0x81);
			i2c_put_byte(client,0x0301 , 0xa0);
			i2c_put_byte(client,0x0201 , 0x20);
			break;
		case EXPOSURE_P3_STEP:
			i2c_put_byte(client,0x0300 , 0x81);
			i2c_put_byte(client,0x0301 , 0xb0);
			i2c_put_byte(client,0x0201 , 0x30);
			break;						
		case EXPOSURE_P4_STEP:	
			i2c_put_byte(client,0x0300 , 0x81);
			i2c_put_byte(client,0x0301 , 0xc0);
			i2c_put_byte(client,0x0201 , 0x40);
			break;
		default:
			i2c_put_byte(client,0x0300 , 0x81);
			i2c_put_byte(client,0x0301 , 0x80);
			i2c_put_byte(client,0x0201 , 0x00);
			break;
	}
} /* gt2005_set_param_exposure */
/*************************************************************************
* FUNCTION
*    GT2005_set_param_effect
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
void GT2005_set_param_effect(struct gt2005_device *dev,enum camera_effect_flip_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);


    switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL:
			i2c_put_byte(client,0x0115,0x00);
			break;

		case CAM_EFFECT_ENC_GRAYSCALE:
			i2c_put_byte(client,0x0115,0x06);
			break;

		case CAM_EFFECT_ENC_SEPIA:
		     	i2c_put_byte(client,0x0115,0x0a);
			i2c_put_byte(client,0x026e,0x60);
			i2c_put_byte(client,0x026f,0xa0);
			break;

		case CAM_EFFECT_ENC_SEPIAGREEN:
			i2c_put_byte(client,0x0115,0x0a);
			i2c_put_byte(client,0x026e,0x20);
			i2c_put_byte(client,0x026f,0x00);
			break;

		case CAM_EFFECT_ENC_SEPIABLUE:
			i2c_put_byte(client,0x0115,0x0a);
			i2c_put_byte(client,0x026e,0xfb);
			i2c_put_byte(client,0x026f,0x00);
			break;

		case CAM_EFFECT_ENC_COLORINV:
			i2c_put_byte(client,0x0115,0x09);
			break;

		default:
			break;
	}



} /* GT2005_set_param_effect */

/*************************************************************************
* FUNCTION
*    GT2005_NightMode
*
* DESCRIPTION
*    This function night mode of GT2005.
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
void GT2005_set_night_mode(struct gt2005_device *dev,enum  camera_night_mode_flip_e enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	if (enable) {
		i2c_put_byte(client,0x0312 , 0xc8); //Camera Enable night mode  1/5 Frame rate
	}
	else {
		i2c_put_byte(client,0x0312 , 0x98); //Disable night mode  1/2 Frame rate
	}

}    /* GT2005_NightMode */
void GT2005_set_param_banding(struct gt2005_device *dev,enum  camera_banding_flip_e banding)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//unsigned char buf[4];

	switch(banding){
		case CAM_BANDING_50HZ:
			i2c_put_byte(client,0x0315,0x16);
			break;
		case CAM_BANDING_60HZ:
			i2c_put_byte(client,0x0315,0x56);
			break;
		default:
			break;
	}

}

static int set_flip(struct gt2005_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	temp = i2c_get_byte(client, 0x0101);
	//printk("src temp is 0x%x\n", temp);
	temp &= 0xfc;
	temp |= dev->cam_info.m_flip << 0;
	temp |= dev->cam_info.v_flip << 1;
	//printk("dst temp is 0x%x\n", temp);
	if((i2c_put_byte(client, 0x0101, temp)) < 0) {
            printk("fail in setting sensor orientation \n");
            return -1;
        }
        return 0;
}

void GT2005_set_resolution(struct gt2005_device *dev,int height,int width)
{

	int ret=0;
	//unsigned char cur_val;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	if (width*height<640*480){
		//320*240
		printk("####################set 320X240 #################################\n");
		i2c_put_byte(client,0x0109, 0x00);
		i2c_put_byte(client,0x010a, 0x04);
		i2c_put_byte(client,0x010b, 0x03);
		
		i2c_put_byte(client,0x0110, 0x01);
		i2c_put_byte(client,0x0111, 0x40);
		i2c_put_byte(client,0x0112, 0x00);
		i2c_put_byte(client,0x0113, 0xf0);

		mdelay(100);
		gt2005_frmintervals_active.denominator 	= 15;
		gt2005_frmintervals_active.numerator	= 1;
		gt2005_h_active=320;
		gt2005_v_active=238;
	} else if (width*height<1600*1200){
		//800*600

		i2c_put_byte(client,0x0109 ,  0x00);
		i2c_put_byte(client,0x010a ,  0x04);
		i2c_put_byte(client,0x010b ,  0x0F);
		i2c_put_byte(client,0x0110, 0x03);
		i2c_put_byte(client,0x0111, 0x20);
		i2c_put_byte(client,0x0112, 0x02);
		i2c_put_byte(client,0x0113,   0x5A);//258


		i2c_put_byte(client,0x0300 , 0x81); //alc on
		mdelay(100);
		gt2005_frmintervals_active.denominator 	= 15;
		gt2005_frmintervals_active.numerator	= 1;
		gt2005_h_active=800;
		gt2005_v_active=600;
	} else if(width*height>=1600*1200){
		//1600x1200
#if  1
		i2c_put_byte(client,0x020B , 0x28);
		i2c_put_byte(client,0x020C , 0x44);

		i2c_put_byte(client,0x040A , 0x00);
		i2c_put_byte(client,0x040B , 0xf6);

		i2c_put_byte(client, 0x0300, 0xc1);

		ret= i2c_get_byte(client, 0x12);
		shutterH=(char)ret;

		ret =i2c_get_byte(client, 0x13);
		shutterL=(char)ret;
		
		ret =i2c_get_byte(client, 0x14);
		AGain_shutterH=(char)ret;

		ret = i2c_get_byte(client, 0x15);
		AGain_shutterL=(char)ret;

		ret = i2c_get_byte(client, 0x16);
		DGain_shutterH=(char)ret;

		ret =i2c_get_byte(client, 0x17);
		DGain_shutterL=(char)ret;
		
		//AGain_shutter = ((AGain_shutterH<<8)|(AGain_shutterL&0xff));
#endif
		i2c_put_byte(client,0x0109 ,  0x01);
		i2c_put_byte(client,0x010a ,  0x00);
		i2c_put_byte(client,0x010b ,  0x00);

		i2c_put_byte(client,0x0110 , 0x06);
		i2c_put_byte(client,0x0111 ,  0x40);
		i2c_put_byte(client,0x0112 , 0x04);
		i2c_put_byte(client,0x0113 , 0xb2);//b0
#if  1 
		//AGain_shutter = ((AGain_shutterH<<8)|(AGain_shutterL&0xff));
		DGain_shutter = (DGain_shutterH<<8|(DGain_shutterL&0xff));
		DGain_shutter = DGain_shutter>>2;
		shutter =( (shutterH<<8)|(shutterL&0xff));
		//shutter = shutter/2;
	   	i2c_put_byte(client, 0x0300, 0x41);
		i2c_put_byte(client, 0x0304, shutter>>8);
		i2c_put_byte(client, 0x0305, shutter&0xff);

		i2c_put_byte(client, 0x0307, AGain_shutterL);
		i2c_put_byte(client, 0x0306, AGain_shutterH);
		i2c_put_byte(client, 0x0308, DGain_shutter);
#endif
		msleep(500);
		ret=i2c_get_byte(client,0x0300);
		ret=i2c_get_byte(client,0x0003);
		gt2005_h_active=1600;
		gt2005_v_active=1200;
		gt2005_frmintervals_active.denominator 	= 5;
		gt2005_frmintervals_active.numerator	= 1;
	}
	set_flip(dev);
	printk(KERN_INFO " set camera  GT2005_set_resolution=w=%d,h=%d. \n ",width,height);
}    /* GT2005_set_resolution */

unsigned char v4l_2_gt2005(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int gt2005_setting(struct gt2005_device *dev,int PROP_ID,int value )
{
	int ret=0;
	//unsigned char cur_val;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_gt2005(value));
		ret=i2c_put_byte(client,0x0201,v4l_2_gt2005(value));
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
		if(gt2005_qctrl[0].default_value!=value){
			gt2005_qctrl[0].default_value=value;
			GT2005_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
		}
		break;
	case V4L2_CID_EXPOSURE:
		if(gt2005_qctrl[1].default_value!=value){
			gt2005_qctrl[1].default_value=value;
			GT2005_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
		}
		break;
	case V4L2_CID_COLORFX:
		if(gt2005_qctrl[2].default_value!=value){
			gt2005_qctrl[2].default_value=value;
			GT2005_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
		}
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		if(gt2005_qctrl[3].default_value!=value){
			gt2005_qctrl[3].default_value=value;
			GT2005_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		break;
	case V4L2_CID_BLUE_BALANCE:
		if(gt2005_qctrl[4].default_value!=value){
			gt2005_qctrl[4].default_value=value;
			GT2005_set_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(gt2005_qctrl[5].default_value!=value){
			gt2005_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(gt2005_qctrl[7].default_value!=value){
			gt2005_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(gt2005_qctrl[8].default_value!=value){
			gt2005_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;

}

static void power_down_gt2005(struct gt2005_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	i2c_put_byte(client,0x0104, 0x00);
	i2c_put_byte(client,0x0100, 0x00);
}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void gt2005_fillbuff(struct gt2005_fh *fh, struct gt2005_buffer *buf)
{
	struct gt2005_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
	/*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = gt2005_qctrl[5].default_value&3;// not set
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = gt2005_qctrl[7].default_value;
	para.angle = gt2005_qctrl[8].default_value;
	para.vaddr = (unsigned)vbuf;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void gt2005_thread_tick(struct gt2005_fh *fh)
{
	struct gt2005_buffer *buf;
	struct gt2005_device *dev = fh->dev;
	struct gt2005_dmaqueue *dma_q = &dev->vidq;

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
			 struct gt2005_buffer, vb.queue);
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
	gt2005_fillbuff(fh, buf);
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

static void gt2005_sleep(struct gt2005_fh *fh)
{
	struct gt2005_device *dev = fh->dev;
	struct gt2005_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	gt2005_thread_tick(fh);

	schedule_timeout_interruptible(2);//if fps > 25 , 2->1

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int gt2005_thread(void *data)
{
	struct gt2005_fh  *fh = data;
	struct gt2005_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		gt2005_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int gt2005_start_thread(struct gt2005_fh *fh)
{
	struct gt2005_device *dev = fh->dev;
	struct gt2005_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(gt2005_thread, fh, "gt2005");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void gt2005_stop_thread(struct gt2005_dmaqueue  *dma_q)
{
	struct gt2005_device *dev = container_of(dma_q, struct gt2005_device, vidq);

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
	struct gt2005_fh  *fh = vq->priv_data;
	struct gt2005_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct gt2005_buffer *buf)
{
	struct gt2005_fh  *fh = vq->priv_data;
	struct gt2005_device *dev  = fh->dev;

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
	struct gt2005_fh     *fh  = vq->priv_data;
	struct gt2005_device    *dev = fh->dev;
	struct gt2005_buffer *buf = container_of(vb, struct gt2005_buffer, vb);
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
	struct gt2005_buffer    *buf  = container_of(vb, struct gt2005_buffer, vb);
	struct gt2005_fh        *fh   = vq->priv_data;
	struct gt2005_device       *dev  = fh->dev;
	struct gt2005_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct gt2005_buffer   *buf  = container_of(vb, struct gt2005_buffer, vb);
	struct gt2005_fh       *fh   = vq->priv_data;
	struct gt2005_device      *dev  = (struct gt2005_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops gt2005_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_enum_frameintervals(struct file *file, void *priv,
        struct v4l2_frmivalenum *fival)
{
    unsigned int k;

    if(fival->index > ARRAY_SIZE(gt2005_frmivalenum))
        return -EINVAL;

    for(k =0; k< ARRAY_SIZE(gt2005_frmivalenum); k++)
    {
        if( (fival->index==gt2005_frmivalenum[k].index)&&
                (fival->pixel_format ==gt2005_frmivalenum[k].pixel_format )&&
                (fival->width==gt2005_frmivalenum[k].width)&&
                (fival->height==gt2005_frmivalenum[k].height)){
            memcpy( fival, &gt2005_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
            return 0;
        }
    }

    return -EINVAL;

}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct gt2005_fh  *fh  = priv;
	struct gt2005_device *dev = fh->dev;

	strcpy(cap->driver, "gt2005");
	strcpy(cap->card, "gt2005");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = GT2005_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct gt2005_fmt *fmt;

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
	struct gt2005_fh *fh = priv;

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
	struct gt2005_fh  *fh  = priv;
	struct gt2005_device *dev = fh->dev;
	struct gt2005_fmt *fmt;
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
	struct gt2005_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct gt2005_device *dev = fh->dev;

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
		GT2005_set_resolution(dev,fh->height,fh->width);
	} else{
		GT2005_set_resolution(dev,fh->height,fh->width);
	}

	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return ret;
}

static int vidioc_g_parm(struct file *file, void *priv,
        struct v4l2_streamparm *parms)
{
    struct gt2005_fh *fh = priv;
    struct gt2005_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = gt2005_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct gt2005_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gt2005_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gt2005_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gt2005_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct gt2005_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct gt2005_fh  *fh = priv;
	vdin_parm_t para;
	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para ));
	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = gt2005_frmintervals_active.denominator;
	para.h_active = gt2005_h_active;
	para.v_active = gt2005_v_active;
	para.hsync_phase = 1;
	para.vsync_phase = 1;
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
	para.skip_count =  2; //skip_num
	printk("gt2005,h=%d, v=%d, frame_rate=%d\n", 
		gt2005_h_active, gt2005_v_active, gt2005_frmintervals_active.denominator);
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
	    vops->start_tvin_service(0,&para);
	    fh->stream_on        = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct gt2005_fh  *fh = priv;

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
	struct gt2005_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(gt2005_prev_resolution))
			return -EINVAL;
		frmsize = &gt2005_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(gt2005_pic_resolution))
			return -EINVAL;
		frmsize = &gt2005_pic_resolution[fsize->index];
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
	struct gt2005_fh *fh = priv;
	struct gt2005_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct gt2005_fh *fh = priv;
	struct gt2005_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(gt2005_qctrl); i++)
		if (qc->id && qc->id == gt2005_qctrl[i].id) {
			memcpy(qc, &(gt2005_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct gt2005_fh *fh = priv;
	struct gt2005_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(gt2005_qctrl); i++)
		if (ctrl->id == gt2005_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct gt2005_fh *fh = priv;
	struct gt2005_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(gt2005_qctrl); i++)
		if (ctrl->id == gt2005_qctrl[i].id) {
			if (ctrl->value < gt2005_qctrl[i].minimum ||
			    ctrl->value > gt2005_qctrl[i].maximum ||
			    gt2005_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int gt2005_open(struct file *file)
{
	struct gt2005_device *dev = video_drvdata(file);
	struct gt2005_fh *fh = NULL;
	int retval = 0;
#ifdef CONFIG_CMA
    retval = vm_init_buf(16*SZ_1M);
    if(retval <0)
        return -1;
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif		
	aml_cam_init(&dev->cam_info);
	
	GT2005_init_regs(dev);
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

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &gt2005_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct gt2005_buffer), fh,NULL);

	gt2005_start_thread(fh);
	gt2005_have_open = 1;
	return 0;
}

static ssize_t
gt2005_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct gt2005_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
gt2005_poll(struct file *file, struct poll_table_struct *wait)
{
	struct gt2005_fh        *fh = file->private_data;
	struct gt2005_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int gt2005_close(struct file *file)
{
	struct gt2005_fh         *fh = file->private_data;
	struct gt2005_device *dev       = fh->dev;
	struct gt2005_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);
	gt2005_have_open = 0;
	gt2005_stop_thread(vidq);
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
	gt2005_h_active=800;
	gt2005_v_active=600;
	gt2005_qctrl[0].default_value= CAM_WB_AUTO;
	gt2005_qctrl[1].default_value=4;
	gt2005_qctrl[2].default_value=0;
	gt2005_qctrl[3].default_value=CAM_BANDING_50HZ;
	gt2005_qctrl[4].default_value=0;

	gt2005_qctrl[5].default_value=0;
	gt2005_qctrl[7].default_value=100;
	gt2005_qctrl[8].default_value=0;
	gt2005_frmintervals_active.numerator = 1;
	gt2005_frmintervals_active.denominator = 15;
	power_down_gt2005(dev);
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

static int gt2005_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gt2005_fh  *fh = file->private_data;
	struct gt2005_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations gt2005_fops = {
	.owner		= THIS_MODULE,
	.open           = gt2005_open,
	.release        = gt2005_close,
	.read           = gt2005_read,
	.poll		= gt2005_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = gt2005_mmap,
};

static const struct v4l2_ioctl_ops gt2005_ioctl_ops = {
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

static struct video_device gt2005_template = {
	.name		= "gt2005_v4l",
	.fops           = &gt2005_fops,
	.ioctl_ops 	= &gt2005_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int gt2005_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_GT2005, 0);
}

static const struct v4l2_subdev_core_ops gt2005_core_ops = {
	.g_chip_ident = gt2005_g_chip_ident,
};

static const struct v4l2_subdev_ops gt2005_ops = {
	.core = &gt2005_core_ops,
};

static int gt2005_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	struct gt2005_device *t;
	struct v4l2_subdev *sd;
	aml_cam_info_t* plat_dat;
	vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &gt2005_ops);

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
	memcpy(t->vdev, &gt2005_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);
	
	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "gt2005");
	/* Register it */
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if(plat_dat->front_back>=0)  
			video_nr=plat_dat->front_back;
	}else {
		printk("camera gt2005: have no platform data\n");
		kfree(t);
		kfree(client);
		return -1;
	}
	
	t->cam_info.version = GT2005_DRIVER_VERSION;
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

static int gt2005_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gt2005_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id gt2005_id[] = {
	{ "gt2005", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gt2005_id);

static struct i2c_driver gt2005_i2c_driver = {
	.driver = {
		.name = "gt2005",
	},
	.probe = gt2005_probe,
	.remove = gt2005_remove,
	.id_table = gt2005_id,
};

module_i2c_driver(gt2005_i2c_driver);

