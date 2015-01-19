/*
 *OV3660 - This code emulates a real video device with v4l2 api
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
#include <mach/gpio.h>
#include <mach/am_regs.h>
//#include <mach/am_eth_pinmux.h>
#include <mach/pinmux.h>
#include "common/plat_ctrl.h"
#include <mach/mod_gate.h>

static struct vdin_v4l2_ops_s *vops;

#define OV3660_CAMERA_MODULE_NAME "ov3660"
#define TEST_I2C   1

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define OV3660_CAMERA_MAJOR_VERSION 0
#define OV3660_CAMERA_MINOR_VERSION 7
#define OV3660_CAMERA_RELEASE 0
#define OV3660_CAMERA_VERSION \
	KERNEL_VERSION(OV3660_CAMERA_MAJOR_VERSION, OV3660_CAMERA_MINOR_VERSION, OV3660_CAMERA_RELEASE)

MODULE_DESCRIPTION("ov3660 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define OV3660_DRIVER_VERSION "OV3660-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static int ov3660_h_active=800;
static int ov3660_v_active=596;

static struct v4l2_fract ov3660_frmintervals_active = {
	.numerator = 1,
	.denominator = 20,
};

#define EMDOOR_DEBUG_OV3660        1
static struct i2c_client *ov3660_client;
/* supported controls */
static struct v4l2_queryctrl ov3660_qctrl[] = {
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Brightness",
		.minimum       = -3,
		.maximum       = 3,
		.step          = 1,
		.default_value = 0,
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
	},{
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

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct ov3660_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct ov3660_fmt formats[] = {
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
		.name     = "12  Y/CbCr 4:2:0SP",
		.fourcc   = V4L2_PIX_FMT_NV12,
		.depth    = 12,	
	},
	{
		.name     = "12  Y/CbCr 4:2:0SP",
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

static struct ov3660_fmt *get_format(struct v4l2_format *f)
{
	struct ov3660_fmt *fmt;
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
struct ov3660_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct ov3660_fmt        *fmt;
};

struct ov3660_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(ov3660_devicelist);

struct ov3660_device {
	struct list_head			ov3660_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct ov3660_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t  cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;
	
	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(ov3660_qctrl)];
};

static inline struct ov3660_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov3660_device, sd);
}

struct ov3660_fh {
	struct ov3660_device            *dev;

	/* video capture */
	struct ov3660_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};
/*static inline struct ov3660_fh *to_fh(struct ov3660_device *dev)
{
	return container_of(dev, struct ov3660_fh, dev);
}*/
static struct v4l2_frmsize_discrete ov3660_prev_resolution[]= //should include 352x288 and 640x480, those two size are used for recording
{
	{320,240},
	{352,288},
	{640, 480},
};

static struct v4l2_frmsize_discrete ov3660_pic_resolution[]=
{
	{2048, 1536},
	{1600, 1200},
//	{800,600},
};

/* ------------------------------------------------------------------
	reg spec of OV3660
   ------------------------------------------------------------------*/
struct aml_camera_i2c_fig_s OV3660_script[] = {
	//{0x3008, 0x82}, //software reset
//delay 3ms here
	//msleep(3);
	{0x3103, 0x13},
	{0x3008, 0x42}, //software power down
	{0x3017, 0xff}, //enable output
	{0x3018, 0xff}, //enable output
	{0x302c, 0x83}, // Driver 3x, Fsin input enable,Strobe input enable
	{0x3032, 0x00},
	{0x3901, 0x13},
	{0x3704, 0x80},
	{0x3717, 0x00},
	{0x371b, 0x60},
	{0x370b, 0x10},
	{0x3624, 0x03},
	{0x3622, 0x80},
	{0x3614, 0x80},
	{0x3630, 0x52},
	{0x3632, 0x07},
	{0x3633, 0xd2},
	{0x3619, 0x75},
	{0x371c, 0x00},
	{0x370b, 0x12},
	{0x3704, 0x80},
	{0x3600, 0x08},
	{0x3620, 0x43},
	{0x3702, 0x20},
	{0x3739, 0x48},
	{0x3730, 0x20},
	{0x370c, 0x0c},
	{0x3a18, 0x00}, //gain ceiling 8bit high
	{0x3a19, 0xf8}, //gain ceiling 8bit low
	{0x3000, 0x10}, //reset for blocks
	{0x3004, 0xef}, //clock enable for blocks
//temperature sensor control registers
	{0x6700, 0x05},
	{0x6701, 0x19},
	{0x6702, 0xfd},
	{0x6703, 0xd1},
	{0x6704, 0xff},
	{0x6705, 0xff},
	{0x3002, 0x1c},
	{0x3006, 0xc3},
	{0x3826, 0x23},
	{0x3618, 0x00},
	{0x3623, 0x00},
	{0x4300, 0x31}, //YUV422,YUYV
	{0x440e, 0x09},
	{0x460b, 0x37},
	{0x460c, 0x20},
	{0x4713, 0x02},
	{0x471c, 0xd0},
	{0x5086, 0x00},
	{0x5000, 0x07},
	{0x5001, 0x03},
	{0x5002, 0x00},
	{0x501f, 0x00}, //ISP YUV422
	{0x302c, 0x03},
	{0x3a00, 0x38},
	{0x440e, 0x08}, //for MBIST
	//{0x3008, 0x02}, //WAKE UP FROM SOFTWARE POWER DOWN MODE
//Reference IQ
//EV
    {0x3a0f, 0x38},  
    {0x3a10, 0x30},  
    {0x3a1b, 0x38},  
    {0x3a1e, 0x30},  
    {0x3a11, 0x70},  
	{0x3a1f, 0x14},
// awb init 	
	{0x3406, 0x01},     
    {0x3400, 0x06},              
    {0x3401, 0x80},              
    {0x3402, 0x04},              
    {0x3403, 0x00},              
    {0x3404, 0x06},              
    {0x3405, 0x40},  
	
//Awb
    {0x5180, 0xff},  // awb
	{0x5181, 0xf2},
	{0x5182, 0x00},
	{0x5183, 0x14},
	{0x5184, 0x25},
	{0x5185, 0x24},
    {0x5186, 0x16},  
    {0x5187, 0x16},   
    {0x5188, 0x16},   
    {0x5189, 0x68},  
    {0x518a, 0x60},  
    {0x518b, 0xe0},  
    {0x518c, 0xb2},  
    {0x518d, 0x42},  
    {0x518e, 0x15},  
    {0x518f, 0x86},  
    {0x5190, 0x56},  
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
	{0x519e, 0x38},
//Color Matrix
    {0x5381, 0x1c},  // ccm  
    {0x5382, 0x5a},  
    {0x5383, 0x12},   
    {0x5384, 0x07},    
    {0x5385, 0x73},  
    {0x5386, 0x7a},  
    {0x5387, 0x7a},  
    {0x5388, 0x5e},  
    {0x5389, 0x1c},  
	{0x538a, 0x01},
    {0x538b, 0x98},  
//CIP
	{0x5300, 0x0c},
	{0x5301, 0x20},
	{0x5302, 0x10},
	{0x5303, 0x00},
	{0x5304, 0x0c},
	{0x5305, 0x20},
	{0x5306, 0x0c},
	{0x5307, 0x20},
	{0x5309, 0x0c},
	{0x530a, 0x20},
	{0x530b, 0x00},
	{0x530c, 0x10},
//Gamma
    {0x5481, 0x06},  //gamma 
    {0x5482, 0x0e},  
    {0x5483, 0x14},  
    {0x5484, 0x3e},  
    {0x5485, 0x4c},  
    {0x5486, 0x5a},  
    {0x5487, 0x68},  
    {0x5488, 0x74},  
    {0x5489, 0x80},  
    {0x548a, 0x8d},  
    {0x548b, 0x9e},  
    {0x548c, 0xac},  
    {0x548d, 0xc3},  
    {0x548e, 0xd5},  
    {0x548f, 0xe8},  
    {0x5490, 0x20},  
//UV adjust
	{0x5001, 0xa7}, //SDE on, scale on, UV average on, color matrix on, AWB on
	{0x5580, 0x06},
	{0x5583, 0x60},
	{0x5584, 0x40},
	{0x5589, 0x10},
	{0x558a, 0x00},
	{0x558b, 0x70},
//Lens Correction
	{0x5000, 0xa7}, //Lens on, Gamma on, BPC on, WPC on, interpolation on
    {0x5800, 0x0C}, 
    {0x5801, 0x09},  
    {0x5802, 0x0C},  
    {0x5803, 0x0C},  
    {0x5804, 0x0D},  
    {0x5805, 0x17},  
    {0x5806, 0x06},  
    {0x5807, 0x05},  
    {0x5808, 0x04},  
	{0x5809, 0x06},
    {0x580a, 0x09},  
    {0x580b, 0x0E},  
    {0x580c, 0x05},  
    {0x580d, 0x01},  
    {0x580e, 0x00},  
    {0x580f, 0x01},  
    {0x5810, 0x05},  
    {0x5811, 0x0D},  
    {0x5812, 0x05},  
    {0x5813, 0x01},  
	{0x5814, 0x00},
    {0x5815, 0x01},  
    {0x5816, 0x05},  
    {0x5817, 0x0D},  
    {0x5818, 0x08},  
    {0x5819, 0x06},  
	{0x581a, 0x05},
    {0x581b, 0x07},  
    {0x581c, 0x0B},  
    {0x581d, 0x0D},  
    {0x581e, 0x12},  
    {0x581f, 0x0D},  
    {0x5820, 0x0E},  
    {0x5821, 0x10},  
    {0x5822, 0x10},  
    {0x5823, 0x1E},  
    {0x5824, 0x53},  
    {0x5825, 0x15},  
    {0x5826, 0x05},  
    {0x5827, 0x14},  
    {0x5828, 0x54},  
    {0x5829, 0x25},  
    {0x582a, 0x33},  
    {0x582b, 0x33},  
    {0x582c, 0x34},  
    {0x582d, 0x16},  
    {0x582e, 0x24},  
    {0x582f, 0x41},  
    {0x5830, 0x50},  
    {0x5831, 0x42},  
    {0x5832, 0x15},  
    {0x5833, 0x25},  
    {0x5834, 0x34},  
    {0x5835, 0x33},  
    {0x5836, 0x24},  
    {0x5837, 0x26},  
    {0x5838, 0x54},  
    {0x5839, 0x25},  
    {0x583a, 0x15},  
    {0x583b, 0x25},  
    {0x583c, 0x53},  
    {0x583d, 0xCF},  
//dns
	{0x5308, 0x25},
	{0x5304, 0x08},
	{0x5305, 0x30},
	{0x5306, 0x08},
	{0x5307, 0x50},
//Sharpness Auto
	{0x5300, 0x08},
	{0x5301, 0x18},
	{0x5302, 0x30}, //0x18
	{0x5303, 0x20}, //0x00
	{0x5306, 0x18},  
    {0x5307, 0x28}, 
	{0x5309, 0x10},
	{0x530a, 0x18},
	{0x530b, 0x04},
	{0x530c, 0x18},
	
	//{0x3820, 0x41},//0x40
	//{0x3821, 0x07},//0x04
	//{0x4514, 0xbb},
//BANDING
//Under 27M Pclk
	{0x3a00, 0x38}, //BIT[5]:1,banding function enable;bit[2] night mode disable
/******if use 50HZ banding remove, refer to below setting********/
	{0x3c00, 0x04}, //BIT[2]:1,ENABLE 50HZ
	{0x3a14, 0x06}, //NIGHT MODE CEILING, 50HZ
	{0x3a15, 0x6d}, //NIGHT MODE CEILING, 50HZ
	{0x3a08, 0x00}, //50HZ BAND WIDTH
	{0x3a09, 0xeb}, //50HZ BAND WIDTH
	{0x3a0e, 0x06}, //50HZ MAX BAND
	
	{0x380e, 0x06}, 
	{0x380f, 0x6d}, //inset 81 dummy lines for banding filter//1c
	/******if use 60HZ banding remove, refer to below setting********/
	
	//{0x3c00, 0x00}, //BIT[2]:0,ENABLE 50HZ
	{0x3a02, 0x06}, //NIGHT MODE CEILING, 60HZ
	{0x3a03, 0x6d}, //NIGHT MODE CEILING, 60HZ
	{0x3a0a, 0x00}, //60HZ BAND WIDTH
	{0x3a0b, 0xc4}, //60HZ BAND WIDTH
	{0x3a0d, 0x08}, //60HZ MAX BAND

	#if 0
	{0x3a0f, 0x38},  
    {0x3a10, 0x30},  
    {0x3a1b, 0x38},  
    {0x3a1e, 0x30},  
    {0x3a11, 0x70},  
	{0x3a1f, 0x14},
	#endif
	{0x3008, 0x02}, //WAKE UP FROM SOFTWARE POWER DOWN MODE
	{0xffff, 0xff},

};

//load OV3660 parameters
void OV3660_init_regs(struct ov3660_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
       int i=0;
#ifdef TEST_I2C
	struct aml_camera_i2c_fig_s ov3660_id[2];
	unsigned int id;
	ov3660_id[0].addr = 0x300A;
	ov3660_id[1].addr = 0x300B;
	ov3660_id[0].val = i2c_get_byte(client, ov3660_id[0].addr);
	ov3660_id[1].val = i2c_get_byte(client, ov3660_id[1].addr);
	id = (ov3660_id[0].val << 8)|ov3660_id[1].val;
	printk("<=================Ov3660 ID:0x%x\n", id);
#endif
	i2c_put_byte(client, 0x3008, 0x82); //soft reset
	msleep(3);
    while(1)
    {
        if (OV3660_script[i].val==0xff&&OV3660_script[i].addr==0xffff) 
        {
        	printk("OV3660_write_regs success in initial OV3660.\n");
        	break;
        }
        if((i2c_put_byte(client,OV3660_script[i].addr, OV3660_script[i].val)) < 0)
        {
        	printk("fail in initial OV3660. i=%d\n",i);
           //break;
		}
		i++;
    }
    return;
}
/*************************************************************************
* FUNCTION
*    OV3660_set_param_brightness
*
* DESCRIPTION
*    brightness setting.
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
void OV3660_set_param_brightness(struct ov3660_device *dev, int value)//亮度
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int temp;

    switch (value) {	
		case 3:
			temp = i2c_get_byte(client,0x5588);
			i2c_put_byte(client, 0x5588, temp&0xf7);
			i2c_put_byte(client, 0x5587, 0x60);
			break;
		case 2: 
			temp = i2c_get_byte(client,0x5588);
			i2c_put_byte(client, 0x5588, temp&0xf7);
			i2c_put_byte(client, 0x5587, 0x40);
			break;
		case 1: 
			temp = i2c_get_byte(client,0x5588);
			i2c_put_byte(client, 0x5588, temp&0xf7);
			i2c_put_byte(client, 0x5587, 0x20);
			break;
		case 0: 
			temp = i2c_get_byte(client,0x5588);
			i2c_put_byte(client, 0x5588, temp&0xf7);
			i2c_put_byte(client, 0x5587, 0x00);
			break;			
		case -1: 
			temp = i2c_get_byte(client,0x5588);
			i2c_put_byte(client, 0x5588, temp | 0x08);
			i2c_put_byte(client, 0x5587, 0x20);
			break;
      	case -2: 
      		temp = i2c_get_byte(client,0x5588);
			i2c_put_byte(client, 0x5588, temp | 0x08);
			i2c_put_byte(client, 0x5587, 0x40);
			break;
		case -3:
		    temp = i2c_get_byte(client,0x5588);
			i2c_put_byte(client, 0x5588, temp | 0x08);
			i2c_put_byte(client, 0x5587, 0x60);
			break;
		default:
			temp = i2c_get_byte(client,0x5588);
			i2c_put_byte(client, 0x5588, temp&0xf7);
			i2c_put_byte(client, 0x5587, 0x00);
			break;
	}

} /* OV3660_set_param_brightness */
/*************************************************************************
* FUNCTION
*    OV3660_set_param_contrast
*
* DESCRIPTION
*    contrast setting.
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
void OV3660_set_param_contrast(struct ov3660_device *dev, int value)//亮度
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    switch (value) {	
		case 3:
			i2c_put_byte(client, 0x5586, 0x38);
			break;
		case 2: 
			i2c_put_byte(client, 0x5586, 0x30);
			break;
		case 1: 
			i2c_put_byte(client, 0x5586, 0x28);
			break;
		case 0: 
			i2c_put_byte(client, 0x5586, 0x20);
			break;			
		case -1: 
			i2c_put_byte(client, 0x5586, 0x18);
			break;
      	case -2: 
      		i2c_put_byte(client, 0x5586, 0x10);
			break;
		case -3:
		    i2c_put_byte(client, 0x5586, 0x08);
			break;
		default:
			i2c_put_byte(client, 0x5586, 0x20);
			break;
	}

} /* OV3660_set_param_contrast */
/*************************************************************************
* FUNCTION
*    OV3660_set_param_saturation
*
* DESCRIPTION
*    saturation setting.
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
void OV3660_set_param_saturation(struct ov3660_device *dev, int value)//饱和度
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    switch (value) {	
		case 2: 
			i2c_put_byte(client, 0x5583, 0x70);
			i2c_put_byte(client, 0x5584, 0x70);
			break;
		case 1: 
			i2c_put_byte(client, 0x5583, 0x50);
			i2c_put_byte(client, 0x5584, 0x50);
			break;
		case 0: 
			i2c_put_byte(client, 0x5583, 0x60);
			i2c_put_byte(client, 0x5584, 0x40);
			break;			
		case -1: 
			i2c_put_byte(client, 0x5583, 0x30);
			i2c_put_byte(client, 0x5584, 0x30);
			break;
      	case -2: 
      		i2c_put_byte(client, 0x5583, 0x10);
			i2c_put_byte(client, 0x5584, 0x10);
			break;
		default:
			i2c_put_byte(client, 0x5583, 0x60);
			i2c_put_byte(client, 0x5584, 0x40);
			break;
	}

} /* OV3660_set_param_saturation */
/*************************************************************************
* FUNCTION
*    OV3660_set_param_wb
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
void OV3660_set_param_wb(struct ov3660_device *dev,enum  camera_wb_flip_e para)//白平衡
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    switch (para)
	{
		case CAM_WB_AUTO://自动
			i2c_put_byte(client, 0x3212, 0x03);
			i2c_put_byte(client, 0x3406, 0x00);
			i2c_put_byte(client, 0x3212, 0x13);
			i2c_put_byte(client, 0x3212, 0xa3);
			break;

		case CAM_WB_CLOUD: //阴天
			i2c_put_byte(client, 0x3212, 0x03);
			i2c_put_byte(client, 0x3406, 0x01);
			i2c_put_byte(client, 0x3400, 0x07);
			i2c_put_byte(client, 0x3401, 0xff);
			i2c_put_byte(client, 0x3402, 0x04);
			i2c_put_byte(client, 0x3403, 0x00);
			i2c_put_byte(client, 0x3404, 0x04);
			i2c_put_byte(client, 0x3405, 0x00);
			i2c_put_byte(client, 0x3212, 0x13);
			i2c_put_byte(client, 0x3212, 0xa3);
			break;

		case CAM_WB_DAYLIGHT: //
			i2c_put_byte(client, 0x3212, 0x03);
			i2c_put_byte(client, 0x3406, 0x01);
			i2c_put_byte(client, 0x3400, 0x07);
			i2c_put_byte(client, 0x3401, 0x29);
			i2c_put_byte(client, 0x3402, 0x04);
			i2c_put_byte(client, 0x3403, 0x00);
			i2c_put_byte(client, 0x3404, 0x04);
			i2c_put_byte(client, 0x3405, 0x5d);
			i2c_put_byte(client, 0x3212, 0x13);
			i2c_put_byte(client, 0x3212, 0xa3);
			break;

		case CAM_WB_INCANDESCENCE: 
			i2c_put_byte(client, 0x3212, 0x03);
			i2c_put_byte(client, 0x3406, 0x01);
			i2c_put_byte(client, 0x3400, 0x04);
			i2c_put_byte(client, 0x3401, 0xc5);
			i2c_put_byte(client, 0x3402, 0x04);
			i2c_put_byte(client, 0x3403, 0x00);
			i2c_put_byte(client, 0x3404, 0x08);
			i2c_put_byte(client, 0x3405, 0x4d);
			i2c_put_byte(client, 0x3212, 0x13);
			i2c_put_byte(client, 0x3212, 0xa3);
			break;
			
		case CAM_WB_TUNGSTEN: 
			break;

      	case CAM_WB_FLUORESCENT:
      		i2c_put_byte(client, 0x3212, 0x03);
      		i2c_put_byte(client, 0x3406, 0x01);
			i2c_put_byte(client, 0x3400, 0x06);
			i2c_put_byte(client, 0x3401, 0x57);
			i2c_put_byte(client, 0x3402, 0x04);
			i2c_put_byte(client, 0x3403, 0x00);
			i2c_put_byte(client, 0x3404, 0x07);
			i2c_put_byte(client, 0x3405, 0x6f);
			i2c_put_byte(client, 0x3212, 0x13);
			i2c_put_byte(client, 0x3212, 0xa3);
			break;
		case CAM_WB_MANUAL:
		    	// TODO
			break;
	default :
			break;
	}
	

} /* OV3660_set_param_wb */
/*************************************************************************
* FUNCTION
*    OV3660_set_param_exposure
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
void OV3660_set_param_exposure(struct ov3660_device *dev,enum camera_exposure_e para)//曝光调节
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    switch (para)
	{
		case EXPOSURE_N4_STEP:  //负4档  
            i2c_put_byte(client,0x3a0f , 0x18);
			i2c_put_byte(client,0x3a10 , 0x10);
			i2c_put_byte(client,0x3a1b , 0x18);
			i2c_put_byte(client,0x3a1e , 0x10);
			i2c_put_byte(client,0x3a11 , 0x30);
			i2c_put_byte(client,0x3a1f , 0x08);
			break;
			
		case EXPOSURE_N3_STEP:
            i2c_put_byte(client,0x3a0f , 0x20);
			i2c_put_byte(client,0x3a10 , 0x18);
			i2c_put_byte(client,0x3a1b , 0x20);
			i2c_put_byte(client,0x3a1e , 0x18);
			i2c_put_byte(client,0x3a11 , 0x40);
			i2c_put_byte(client,0x3a1f , 0x0c);
			break;
			
		case EXPOSURE_N2_STEP:
            i2c_put_byte(client,0x3a0f , 0x28);
			i2c_put_byte(client,0x3a10 , 0x20);
			i2c_put_byte(client,0x3a1b , 0x28);
			i2c_put_byte(client,0x3a1e , 0x20);
			i2c_put_byte(client,0x3a11 , 0x50);
			i2c_put_byte(client,0x3a1f , 0x10);
			break;
			
		case EXPOSURE_N1_STEP:
            i2c_put_byte(client,0x3a0f , 0x30);
			i2c_put_byte(client,0x3a10 , 0x28);
			i2c_put_byte(client,0x3a1b , 0x30);
			i2c_put_byte(client,0x3a1e , 0x28);
			i2c_put_byte(client,0x3a11 , 0x60);
			i2c_put_byte(client,0x3a1f , 0x14);
			break;
			
		case EXPOSURE_0_STEP://默认零档
            i2c_put_byte(client,0x3a0f , 0x38);
			i2c_put_byte(client,0x3a10 , 0x30);
			i2c_put_byte(client,0x3a1b , 0x38);
			i2c_put_byte(client,0x3a1e , 0x30);
			i2c_put_byte(client,0x3a11 , 0x70);
			i2c_put_byte(client,0x3a1f , 0x14);
			break;
			
		case EXPOSURE_P1_STEP://正一档
            i2c_put_byte(client,0x3a0f , 0x40);
			i2c_put_byte(client,0x3a10 , 0x38);
			i2c_put_byte(client,0x3a1b , 0x40);
			i2c_put_byte(client,0x3a1e , 0x38);
			i2c_put_byte(client,0x3a11 , 0x80);
			i2c_put_byte(client,0x3a1f , 0x1c);
			break;
			
		case EXPOSURE_P2_STEP:
            i2c_put_byte(client,0x3a0f , 0x48);
			i2c_put_byte(client,0x3a10 , 0x40);
			i2c_put_byte(client,0x3a1b , 0x48);
			i2c_put_byte(client,0x3a1e , 0x40);
			i2c_put_byte(client,0x3a11 , 0x90);
			i2c_put_byte(client,0x3a1f , 0x20);
			break;
			
		case EXPOSURE_P3_STEP:
            i2c_put_byte(client,0x3a0f , 0x50);
			i2c_put_byte(client,0x3a10 , 0x48);
			i2c_put_byte(client,0x3a1b , 0x50);
			i2c_put_byte(client,0x3a1e , 0x48);
			i2c_put_byte(client,0x3a11 , 0xa0);
			i2c_put_byte(client,0x3a1f , 0x24);
			break;
			
		case EXPOSURE_P4_STEP:	
            i2c_put_byte(client,0x3a0f , 0x58);
			i2c_put_byte(client,0x3a10 , 0x50);
			i2c_put_byte(client,0x3a1b , 0x58);
			i2c_put_byte(client,0x3a1e , 0x58);
			i2c_put_byte(client,0x3a11 , 0xb0);
			i2c_put_byte(client,0x3a1f , 0x28);
			break;
			
		default:
            i2c_put_byte(client,0x3a0f , 0x38);
			i2c_put_byte(client,0x3a10 , 0x30);
			i2c_put_byte(client,0x3a1b , 0x38);
			i2c_put_byte(client,0x3a1e , 0x30);
			i2c_put_byte(client,0x3a11 , 0x70);
			i2c_put_byte(client,0x3a1f , 0x14);
			break;
	}

} /* OV3660_set_param_exposure */
/*************************************************************************
* FUNCTION
*    OV3660_set_param_effect
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
void OV3660_set_param_effect(struct ov3660_device *dev,enum camera_effect_flip_e para)//特效设置
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
  
    switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL://正常
			i2c_put_byte(client,0x5001,0x00);//disable effect
			i2c_put_byte(client,0x5580,0x00);//disable effect
			break;		

		case CAM_EFFECT_ENC_GRAYSCALE://灰阶
			i2c_put_byte(client,0x5001,0x80);
			i2c_put_byte(client,0x5580,0x18);
			i2c_put_byte(client,0x5583,0x80);
			i2c_put_byte(client,0x5584,0x80);
			break;

		case CAM_EFFECT_ENC_SEPIA://复古
		    i2c_put_byte(client,0x5001,0x80);
			i2c_put_byte(client,0x5580,0x18);
			i2c_put_byte(client,0x5583,0x40);
			i2c_put_byte(client,0x5584,0xa0);
			break;		
				
		case CAM_EFFECT_ENC_SEPIAGREEN://复古绿
			temp = i2c_get_byte(client, 0x5580);
			i2c_put_byte(client, 0x5580, ((temp & 0xbf )| 0x18));
			i2c_put_byte(client,0x5583,0x60);
			i2c_put_byte(client,0x5584,0x60);
			break;					

		case CAM_EFFECT_ENC_SEPIABLUE://复古蓝
			temp = i2c_get_byte(client, 0x5580);
			i2c_put_byte(client,0x5580, ((temp & 0xbf) | 0x18));
			i2c_put_byte(client,0x5583,0xa0);
			i2c_put_byte(client,0x5584,0x40);
			break;								

		case CAM_EFFECT_ENC_COLORINV://底片
			i2c_put_byte(client,0x5001,0x80);
			i2c_put_byte(client,0x5580,0x58);
			break;		

		default:
			break;
	}

} /* OV3660_set_param_effect */

/*************************************************************************
* FUNCTION
*	OV3660_night_mode
*
* DESCRIPTION
*	This function night mode of OV3660.
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
void OV3660_set_param_banding(struct ov3660_device *dev,enum  camera_banding_flip_e banding)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    switch(banding){
        case CAM_BANDING_60HZ:
        	printk("set banding 60Hz\n");
        	i2c_put_byte(client, 0x3c00, 0x00);
            break;
        case CAM_BANDING_50HZ:
        	printk("set banding 50Hz\n");
        	i2c_put_byte(client, 0x3c00, 0x04);
            break;

        default :
	    	break;
    }
}

unsigned char v4l_2_ov3660(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int ov3660_setting(struct ov3660_device *dev,int PROP_ID,int value ) 
{
	int ret=0;
	//unsigned char cur_val;
	//unsigned char reg_3820, reg_3821, reg_4515;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		if(ov3660_qctrl[0].default_value!=value){
			ov3660_qctrl[0].default_value=value;
			OV3660_set_param_brightness(dev,value);
			printk(KERN_INFO " set camera  brightness=%d. \n ",value);
		}
	case V4L2_CID_CONTRAST:
		if(ov3660_qctrl[1].default_value!=value){
			ov3660_qctrl[1].default_value=value;
			OV3660_set_param_contrast(dev,value);
			printk(KERN_INFO " set camera  contrast=%d. \n ",value);
		}
		break;	
	case V4L2_CID_HFLIP:    /* set flip on H. */
		value = value & 0x3;
		if(ov3660_qctrl[2].default_value!=value){
			ov3660_qctrl[2].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;	
	case V4L2_CID_DO_WHITE_BALANCE:
		if(ov3660_qctrl[4].default_value!=value){
			ov3660_qctrl[4].default_value=value;
			OV3660_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
		}
		break;
	case V4L2_CID_EXPOSURE:
		if(ov3660_qctrl[5].default_value!=value){
			ov3660_qctrl[5].default_value=value;
			OV3660_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
		}
		break;
	case V4L2_CID_COLORFX:
		if(ov3660_qctrl[6].default_value!=value){
			ov3660_qctrl[6].default_value=value;
			OV3660_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
		}
		break;
	case V4L2_CID_WHITENESS:
		if(ov3660_qctrl[7].default_value!=value){
			ov3660_qctrl[7].default_value=value;
			OV3660_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(ov3660_qctrl[8].default_value!=value){
			ov3660_qctrl[8].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
        	}
		break;
	case V4L2_CID_ROTATE:
		if(ov3660_qctrl[9].default_value!=value){
			ov3660_qctrl[9].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;
	
}

static void power_down_ov3660(struct ov3660_device *dev)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	/*i2c_put_byte(client,0x0104, 0x00);
	i2c_put_byte(client,0x0100, 0x00);*/
}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void ov3660_fillbuff(struct ov3660_fh *fh, struct ov3660_buffer *buf)
{
	struct ov3660_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para ={0};
	dprintk(dev,1,"%s\n", __func__);	
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = ov3660_qctrl[2].default_value&3;
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = ov3660_qctrl[8].default_value;
	para.angle = ov3660_qctrl[9].default_value;
	para.vaddr = (unsigned)vbuf;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void ov3660_thread_tick(struct ov3660_fh *fh)
{
	struct ov3660_buffer *buf;
	struct ov3660_device *dev = fh->dev;
	struct ov3660_dmaqueue *dma_q = &dev->vidq;

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
			 struct ov3660_buffer, vb.queue);
        dprintk(dev, 1, "%s\n", __func__);
        dprintk(dev, 1, "list entry get buf is %p\n",buf);
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
	ov3660_fillbuff(fh, buf);
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

static void ov3660_sleep(struct ov3660_fh *fh)
{
	struct ov3660_device *dev = fh->dev;
	struct ov3660_dmaqueue *dma_q = &dev->vidq;

	//int timeout;
	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	ov3660_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int ov3660_thread(void *data)
{
	struct ov3660_fh  *fh = data;
	struct ov3660_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		ov3660_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int ov3660_start_thread(struct ov3660_fh *fh)
{
	struct ov3660_device *dev = fh->dev;
	struct ov3660_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(ov3660_thread, fh, "ov3660");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void ov3660_stop_thread(struct ov3660_dmaqueue  *dma_q)
{
	struct ov3660_device *dev = container_of(dma_q, struct ov3660_device, vidq);

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
	struct ov3660_fh  *fh = vq->priv_data;
	struct ov3660_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct ov3660_buffer *buf)
{
	struct ov3660_fh  *fh = vq->priv_data;
	struct ov3660_device *dev  = fh->dev;

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
	struct ov3660_fh     *fh  = vq->priv_data;
	struct ov3660_device    *dev = fh->dev;
	struct ov3660_buffer *buf = container_of(vb, struct ov3660_buffer, vb);
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
	struct ov3660_buffer    *buf  = container_of(vb, struct ov3660_buffer, vb);
	struct ov3660_fh        *fh   = vq->priv_data;
	struct ov3660_device       *dev  = fh->dev;
	struct ov3660_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct ov3660_buffer   *buf  = container_of(vb, struct ov3660_buffer, vb);
	struct ov3660_fh       *fh   = vq->priv_data;
	struct ov3660_device      *dev  = (struct ov3660_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops ov3660_video_qops = {
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
	struct ov3660_fh  *fh  = priv;
	struct ov3660_device *dev = fh->dev;

	strcpy(cap->driver, "ov3660");
	strcpy(cap->card, "ov3660");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = OV3660_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct ov3660_fmt *fmt;

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
	struct ov3660_fh *fh = priv;

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
	struct ov3660_fh  *fh  = priv;
	struct ov3660_device *dev = fh->dev;
	struct ov3660_fmt *fmt;
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

static struct aml_camera_i2c_fig_s pic2048x1536[]={
	{0x3002, 0x1c},
	{0x3006, 0xc3},
	{0x3800, 0x00},  //HS
	{0x3801, 0x00},  //HS
	{0x3802, 0x00},  //VS
	{0x3803, 0x00},  //VS
	{0x3804, 0x08},  //HW=1696
	{0x3805, 0x1f},  //HW
	{0x3806, 0x06},  //VH=1261
	{0x3807, 0x0b},  //VH
	{0x3808, 0x08},  //DVPHO=640
	{0x3809, 0x00},  //DVPHO
	{0x380a, 0x06},  //DVPVO=480
	{0x380b, 0x00},  //DVPVO
	{0x380c, 0x08},  //HTS=1808
	{0x380d, 0xfc},  //HTS
	{0x380e, 0x06},  //VTS=498
	{0x380f, 0x1c},  //VTS
	{0x3810, 0x00},  //HOFFSET
	{0x3811, 0x10},  //HOFFSET
	{0x3812, 0x00},  //VOFFSET
	{0x3813, 0x06},  //VOFFSET
	{0x3814, 0x11},  //X INC
	{0x3815, 0x11},  //Y INC
	//{0x3820, 0x40},  //FLIP
	//{0x3821, 0x06},  //MIRROR
	//{0x3824, 0x01},  //PCLK RATIO
	{0x3826, 0x23},

	{0x303a, 0x00},
	{0x303b, 0x1b},//0x1b
	{0x303c, 0x12},//0x11
	{0x303d, 0x32},//30
	{0x460c, 0x22},
	{0x3824, 0x02},

	{0x4005, 0x12},

	{0x3618, 0x78},
	{0x3708, 0x21},
	{0x3709, 0x12},
	{0x4300, 0x31},  //YUV422,YUYV
	{0x440e, 0x09},
	{0x4520, 0xb0},
	{0x460b, 0x37},
	{0x460c, 0x20},
	{0x4713, 0x02},
	{0x471c, 0xd0},
	//{0x4514, 0xbb},
	{0x3503, 0x03},  // turn on AGC/AEC
	
#if 0
	//for jpeg
	{0x440e, 0x08},
	{0x3002, 0x00},
	{0x3006, 0xff},
	{0x4300, 0x30},
	{0x5000, 0xa7},
	{0x5001, 0xa7},
	{0x501f, 0x00},
	{0x3821, 0x20},
	{0x4713, 0x02},
	{0x460c, 0x22},
	{0x3824, 0x04},
	{0x460b, 0x35},
	{0x471c, 0x50},
#endif
	{0xffff, 0xff},
};
#if 0
static struct aml_camera_i2c_fig_s pic1600x1200[]={
	{0x5001,0x23},
	{0x3800,0x00},
	{0x3801,0x00},
	{0x3802,0x00},
	{0x3803,0x00},
	{0x3804,0x08},
	{0x3805,0x1f},
	{0x3806,0x06},
	{0x3807,0x0b},
	{0x3808,0x06},
	{0x3809,0x40},
	{0x380a,0x04},
	{0x380b,0xb0},
	{0x380c,0x08},
	{0x380d,0xfc},
	{0x380e,0x06},
	{0x380f,0x1c},
	{0x3810,0x00},
	{0x3811,0x10},
	{0x3812,0x00},
	{0x3813,0x06},
	
	{0x3814,0x11},	// X inc
	{0x3815,0x11},	// Y inc
	//{0x3820,0x40},	// Vertical binning off
	//{0x3821,0x00},	// Horizontal binning off
	{0x3708,0x63},
	{0x3709,0x12},
	{0x370c,0x0c},
	//{0x4520,0xb0},
	//{0x3821,0x06},	// Mirror on,binning off
	//{0x4514,0xbb},
	{0x303a,0x00},
	{0x303b,0x1b},
	{0x303c,0x13},
	{0x303d,0x32},
	{0x3824,0x01},//0x02 can not work,black image
	{0x460c,0x22},	
	
//{0x3a10, 0x2d},
	
	{0xffff, 0xff},
};
#endif

static struct aml_camera_i2c_fig_s pic800x600[]={
	{0x5001,0x23},
	{0x3503,0x00},
	{0x303d,0x30},
	{0x3800,0x00},
	{0x3801,0x00},
	{0x3802,0x00},
	{0x3803,0x00},
	{0x3804,0x08},
	{0x3805,0x1f},
	{0x3806,0x06},
	{0x3807,0x0b},
	{0x3808,0x03},
	{0x3809,0x20},
	{0x380a,0x02},
	{0x380b,0x58},
	{0x380c,0x08},
	{0x380d,0xfc},
	{0x380e,0x06},
	{0x380f,0x6d},//1c
	{0x3810,0x00},
	{0x3811,0x10},
	{0x3812,0x00},
	{0x3813,0x06},
	
	//{0x3820, 0x41},  //FLIP//40
	//{0x3821, 0x07},  //MIRROR//04
    {0x3814, 0x11},
	{0x3815, 0x11},
	//{0x3a10, 0x2d},
		//pll
	{0x303b, 0x1b},
	{0x303c, 0x11},
	{0x303d, 0x30},
	{0x460c, 0x22},
	{0x3824, 0x02},
	
	{0xffff, 0xff},
};

#if 0
static struct aml_camera_i2c_fig_s pic640x480[]={
	{0x3008, 0x42},
	{0x303c, 0x11},//12
	{0x5001, 0xa3},         
	{0x3503, 0x00}, 
	{0x3a00, 0x3c}, 

	{0x5302, 0x30}, 
	{0x5303, 0x10}, 
	{0x5306, 0x18}, 
	{0x5307, 0x28}, 

	{0x3800, 0x00}, 
	{0x3801, 0x00}, 
	{0x3802, 0x00}, 
	{0x3803, 0x00},
	{0x3804, 0x08}, 
	{0x3805, 0x1f}, 
	{0x3806, 0x06}, 
	{0x3807, 0x09},
	{0x3808, 0x02},
	{0x3809, 0x80}, 
	{0x380a, 0x01},
	{0x380b, 0xe0},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x02},
	{0x3814, 0x31},
	{0x3815, 0x31},

	{0x3826, 0x23},
	{0x303a, 0x00},
	{0x303b, 0x1b},
	{0x303c, 0x11},
	{0x303d, 0x30},
	{0x3824, 0x02},
	{0x460c, 0x22},

	{0x380c, 0x08},
	{0x380d, 0xfc},
	{0x380e, 0x03},
	{0x380f, 0x10},

	{0x3a08, 0x00},
	{0x3a09, 0xeb},
	{0x3a0e, 0x03},
	{0x3a0a, 0x00},
	{0x3a0b, 0xc4},
	{0x3a0d, 0x04},

	//{0x3820, 0x01},
	//{0x3821, 0x07},
	//{0x4514, 0xbb},
	{0x3618, 0x00},
	{0x3708, 0x66},
	{0x3709, 0x12},
	{0x4520, 0x0b},

	{0x3008, 0x02},
	
	//{0x3a10, 0x2d},
	
	{0xffff, 0xff},
};
#endif

#if 0
static struct aml_camera_i2c_fig_s pic320x240[]={
	{0x5001,0x23},
	{0x3503,0x00},
	{0x303d,0x30},
	{0x3800,0x00},
	{0x3801,0x00},
	{0x3802,0x00},
	{0x3803,0x00},
	{0x3804,0x08},
	{0x3805,0x1f},
	{0x3806,0x06},
	{0x3807,0x0b},
	{0x3808,0x01},
	{0x3809,0x40},
	{0x380a,0x00},
	{0x380b,0xf0},
	{0x380c,0x08},
	{0x380d,0xfc},
	{0x380e,0x06},
	{0x380f,0x6d},
	{0x3810,0x00},
	{0x3811,0x10},
	{0x3812,0x00},
	{0x3813,0x06},
	
	//{0x3820, 0x41},  //FLIP//40
	//{0x3821, 0x07},  //MIRROR//04
    {0x3814, 0x11},
	{0x3815, 0x11},
	//{0x3a10, 0x2d},
		//pll
	{0x303b, 0x1b},
	{0x303c, 0x11},
	{0x303d, 0x30},
	{0x460c, 0x22},
	{0x3824, 0x02},
	
	{0xffff, 0xff},
};
#endif

static void pic_set_size(struct ov3660_device *dev, struct aml_camera_i2c_fig_s* size)
{
	int i = 0;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	struct aml_camera_i2c_fig_s* pic = size;
	i = 0;
	 while(1){
		if (pic[i].val==0xff&&pic[i].addr==0xffff) {
				//printk("OV3660 pic \n");
				break;
			}if((i2c_put_byte(client,pic[i].addr, pic[i].val)) < 0){
				printk("fail in initial OV3660. i=%d\n",i);
		//		break;
			}
			i++;
		}
}
static int set_flip(struct ov3660_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	temp = i2c_get_byte(client, 0x3821);
	temp &= 0xfc;
	temp |= dev->cam_info.m_flip << 0;
	if((i2c_put_byte(client, 0x3821, temp)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
        }
        temp = i2c_get_byte(client, 0x3820);
        temp &= 0xfc;
	temp |= dev->cam_info.v_flip << 0;
	if((i2c_put_byte(client, 0x3820, temp)) < 0) {
	    printk("fail in setting sensor orientation\n");
	    return -1;
        }
        return 0;
}

void OV3660_set_resolution(struct ov3660_device *dev,int height,int width)
{
	printk("<========%s w:%d, h:%d\n", __func__, width, height);
	if((width<1600)&&(height<1200)){
		//800*600
		pic_set_size(dev, pic800x600);
		//pic_set_size(dev, pic320x240);
		mdelay(20);
		ov3660_h_active=800;
		ov3660_v_active=598;
		ov3660_frmintervals_active.denominator 	= 20;
		ov3660_frmintervals_active.numerator	= 1;
		//ov3660_h_active=320;
		//ov3660_v_active=238;
	} else if ((width>=1600&&height>=1200) && (width<2000&&height<1500)){
		//2048x1536
		//pic_set_size(dev, pic1600x1200);
		pic_set_size(dev, pic2048x1536);
        mdelay(20);
		ov3660_h_active=2032;//1600;
		ov3660_v_active=1534;//1198;
		ov3660_frmintervals_active.denominator 	= 5;
		ov3660_frmintervals_active.numerator	= 1;
	} else if (width>=2000&&height>=1500) {
		//2048x1536
		//pic_set_size(dev, pic1600x1200);
		pic_set_size(dev, pic2048x1536);
		mdelay(20);
		ov3660_h_active=2048;
		ov3660_v_active=1534;
		ov3660_frmintervals_active.denominator 	= 5;
		ov3660_frmintervals_active.numerator	= 1;
	}	
	set_flip(dev);
}    /* OV2659_set_resolution */

static unsigned long ov3660_preview_exposure;
static unsigned long ov3660_preview_extra_lines;
static unsigned long ov3660_gain;
static unsigned long ov3660_preview_maxlines;

static int Get_preview_exposure_gain(struct ov3660_device *dev)
{
	int rc = 0;
	unsigned int ret_l,ret_m,ret_h;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	ret_h = ret_m = ret_l = 0;
	ret_h = i2c_get_byte(client, 0x350c);
	ret_l = i2c_get_byte(client,0x350d);
	ov3660_preview_extra_lines = ((ret_h << 8) + ret_l);
	i2c_put_byte(client,0x3503, 0x03);//stop aec/agc
	//get preview exp & gain
	ret_h = ret_m = ret_l = 0;
	ov3660_preview_exposure = 0;
	ret_h = i2c_get_byte(client, 0x3500);
	ret_m = i2c_get_byte(client,0x3501);
	ret_l = i2c_get_byte(client,0x3502);
	ov3660_preview_exposure = (ret_h << 12) + (ret_m << 4) + (ret_l >> 4);
	ret_h = ret_m = ret_l = 0;
	ov3660_preview_exposure = ov3660_preview_exposure + (ov3660_preview_extra_lines)/16;
	//printk("preview_exposure=%d\n", ov3660_preview_exposure);
	ret_h = ret_m = ret_l = 0;
	ov3660_preview_maxlines = 0;
	ret_h = i2c_get_byte(client, 0x380e);
	ret_l = i2c_get_byte(client, 0x380f);
	ov3660_preview_maxlines = (ret_h << 8) + ret_l;
	//printk("Preview_Maxlines=%d\n", ov3660_preview_maxlines);
	//Read back AGC Gain for preview
	ov3660_gain = 0;
	ov3660_gain = i2c_get_byte(client, 0x350b);
	//printk("Gain,0x350b=0x%x\n", ov3660_gain);

	return rc;
}

#define CAPTURE_FRAMERATE 375
#define PREVIEW_FRAMERATE 1500
static int cal_exposure(struct ov3660_device *dev)
{
	int rc = 0;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//calculate capture exp & gain
	unsigned char ExposureLow,ExposureMid,ExposureHigh,Capture_MaxLines_High,Capture_MaxLines_Low;
	unsigned int ret_l,ret_m,ret_h,Lines_10ms;
	unsigned short ulCapture_Exposure,iCapture_Gain;
	unsigned int ulCapture_Exposure_Gain,Capture_MaxLines;
	ret_h = ret_m = ret_l = 0;
	ret_h = i2c_get_byte(client, 0x380e);
	ret_l = i2c_get_byte(client, 0x380f);
	Capture_MaxLines = (ret_h << 8) + ret_l;
	Capture_MaxLines = Capture_MaxLines + (ov3660_preview_extra_lines)/16;
	printk("Capture_MaxLines=%d\n", Capture_MaxLines);
	if(ov3660_qctrl[7].default_value == CAM_BANDING_60HZ) //60Hz
	{
		Lines_10ms = CAPTURE_FRAMERATE * Capture_MaxLines/12000;
	}
	else
	{
		Lines_10ms = CAPTURE_FRAMERATE * Capture_MaxLines/10000;
	}
	if(ov3660_preview_maxlines == 0)
	{
		ov3660_preview_maxlines = 1;
	}
	ulCapture_Exposure = (ov3660_preview_exposure*(CAPTURE_FRAMERATE)*(Capture_MaxLines))/
	(((ov3660_preview_maxlines)*(PREVIEW_FRAMERATE)));
	iCapture_Gain = ov3660_gain;
	ulCapture_Exposure_Gain = ulCapture_Exposure * iCapture_Gain;
	if(ulCapture_Exposure_Gain < Capture_MaxLines*16)
	{
		ulCapture_Exposure = ulCapture_Exposure_Gain/16;
		if (ulCapture_Exposure > Lines_10ms)
		{
			ulCapture_Exposure /= Lines_10ms;
			ulCapture_Exposure *= Lines_10ms;
		}
	}
	else
	{
		ulCapture_Exposure = Capture_MaxLines;
	}
	if(ulCapture_Exposure == 0)
	{
		ulCapture_Exposure = 1;
	}
	iCapture_Gain = (ulCapture_Exposure_Gain*2/ulCapture_Exposure + 1)/2;
	ExposureLow = ((unsigned char)ulCapture_Exposure)<<4;
	ExposureMid = (unsigned char)(ulCapture_Exposure >> 4) & 0xff;
	ExposureHigh = (unsigned char)(ulCapture_Exposure >> 12);
	Capture_MaxLines_Low = (unsigned char)(Capture_MaxLines & 0xff);
	Capture_MaxLines_High = (unsigned char)(Capture_MaxLines >> 8);
	i2c_put_byte(client, 0x380e, Capture_MaxLines_High);
	i2c_put_byte(client, 0x380f, Capture_MaxLines_Low);
	i2c_put_byte(client, 0x350b, iCapture_Gain);
	i2c_put_byte(client, 0x3502, ExposureLow);
	i2c_put_byte(client, 0x3501, ExposureMid);
	i2c_put_byte(client, 0x3500, ExposureHigh);
	printk("iCapture_Gain=%d\n", iCapture_Gain);
	printk("ExposureLow=%d\n", ExposureLow);
	printk("ExposureMid=%d\n", ExposureMid);
	printk("ExposureHigh=%d\n", ExposureHigh);
	msleep(250);
	return rc;
}

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct ov3660_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct ov3660_device *dev = fh->dev;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char Reg0x3503;

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
		Reg0x3503 = i2c_get_byte(client, 0x3503);
		Reg0x3503 = Reg0x3503 | 0x03;
		i2c_put_byte(client, 0x3503, Reg0x3503);
		
		Get_preview_exposure_gain(dev);
		OV3660_set_resolution(dev,fh->height,fh->width);
		cal_exposure(dev);	
	}
	else {
		Reg0x3503 = i2c_get_byte(client, 0x3503);
		Reg0x3503 = Reg0x3503& (~0x03);
		i2c_put_byte(client, 0x3503, Reg0x3503);
		
		OV3660_set_resolution(dev,fh->height,fh->width);
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
	struct ov3660_fh *fh = priv;
	struct ov3660_device *dev = fh->dev;
	struct v4l2_captureparm *cp = &parms->parm.capture;

	dprintk(dev,3,"vidioc_g_parm\n");
	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;

	cp->timeperframe = ov3660_frmintervals_active;
	dprintk(dev, 3,"g_parm,deno=%d, numerator=%d\n",
		cp->timeperframe.denominator, cp->timeperframe.numerator );

	return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct ov3660_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov3660_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov3660_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov3660_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct ov3660_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct ov3660_fh  *fh = priv;
	struct ov3660_device *dev = fh->dev;
	vdin_parm_t para;
	int ret = 0 ;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para ));
	para.port  = TVIN_PORT_CAMERA;
        para.fmt = TVIN_SIG_FMT_MAX+1;//TVIN_SIG_FMT_CAMERA_1280X720P_30Hz;

	para.frame_rate = ov3660_frmintervals_active.denominator * 10
					/ov3660_frmintervals_active.numerator;//175

	dprintk(dev, 3,"streamon framerate=%d\n",para.frame_rate);
	para.h_active = ov3660_h_active;
	para.v_active = ov3660_v_active;
	para.hsync_phase = 0;//0x1
	para.vsync_phase  = 1;//0x1
	para.hs_bp = 0;
	para.vs_bp = 0;
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
	struct ov3660_fh  *fh = priv;

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
	struct ov3660_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(ov3660_prev_resolution))
			return -EINVAL;
		frmsize = &ov3660_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(ov3660_pic_resolution))
			return -EINVAL;
		frmsize = &ov3660_pic_resolution[fsize->index];
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
	struct ov3660_fh *fh = priv;
	struct ov3660_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct ov3660_fh *fh = priv;
	struct ov3660_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(ov3660_qctrl); i++)
		if (qc->id && qc->id == ov3660_qctrl[i].id) {
			memcpy(qc, &(ov3660_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct ov3660_fh *fh = priv;
	struct ov3660_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(ov3660_qctrl); i++)
		if (ctrl->id == ov3660_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct ov3660_fh *fh = priv;
	struct ov3660_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(ov3660_qctrl); i++)
		if (ctrl->id == ov3660_qctrl[i].id) {
			if (ctrl->value < ov3660_qctrl[i].minimum ||
			    ctrl->value > ov3660_qctrl[i].maximum ||
			    ov3660_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int ov3660_open(struct file *file)
{
	struct ov3660_device *dev = video_drvdata(file);
	struct ov3660_fh *fh = NULL;
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
	OV3660_init_regs(dev);
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

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &ov3660_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct ov3660_buffer), fh,NULL);

	ov3660_start_thread(fh);

	return 0;
}

static ssize_t
ov3660_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct ov3660_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
ov3660_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ov3660_fh        *fh = file->private_data;
	struct ov3660_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int ov3660_close(struct file *file)
{
	struct ov3660_fh         *fh = file->private_data;
	struct ov3660_device *dev       = fh->dev;
	struct ov3660_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);

	ov3660_stop_thread(vidq);
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
	ov3660_h_active=800;
	ov3660_v_active=600;
	ov3660_frmintervals_active.numerator = 1;
	ov3660_frmintervals_active.denominator = 20;
	
	ov3660_qctrl[2].default_value=0;
	ov3660_qctrl[5].default_value=4;
	ov3660_qctrl[8].default_value=100;
	ov3660_qctrl[9].default_value=0;
	power_down_ov3660(dev);
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

static int ov3660_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ov3660_fh  *fh = file->private_data;
	struct ov3660_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations ov3660_fops = {
	.owner		= THIS_MODULE,
	.open           = ov3660_open,
	.release        = ov3660_close,
	.read           = ov3660_read,
	.poll		= ov3660_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = ov3660_mmap,
};

static const struct v4l2_ioctl_ops ov3660_ioctl_ops = {
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
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
};

static struct video_device ov3660_template = {
	.name		= "ov3660_v4l",
	.fops           = &ov3660_fops,
	.ioctl_ops 	= &ov3660_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int ov3660_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_OV3660, 0);
}

static const struct v4l2_subdev_core_ops ov3660_core_ops = {
	.g_chip_ident = ov3660_g_chip_ident,
};

static const struct v4l2_subdev_ops ov3660_ops = {
	.core = &ov3660_core_ops,
};

#ifdef EMDOOR_DEBUG_OV3660
//add by emdoor jf.s for debug ov3660
unsigned int ov3660_reg_addr;

static ssize_t ov3660_show(struct kobject *kobj, struct kobj_attribute *attr,			
	       char *buf)
{	
	unsigned  char dat;
	dat = i2c_get_byte(ov3660_client, ov3660_reg_addr);
	return sprintf(buf, "REG[0x%x]=0x%x\n", ov3660_reg_addr, dat);
}

static ssize_t ov3660_store(struct kobject *kobj, struct kobj_attribute *attr,			 
	      const char *buf, size_t count)
{	
	int tmp;
	unsigned short reg;
	unsigned char val;
	tmp = simple_strtoul(buf, NULL, 16);
	//sscanf(buf, "%du", &tmp);
	if(tmp < 0xffff)
		ov3660_reg_addr = tmp;
	else {
		reg = (tmp >> 8) & 0xFFFF; //reg
		ov3660_reg_addr = reg;
		val = tmp & 0xFF;        //val
		i2c_put_byte(ov3660_client, reg, val);
	}
	
	return count;
}


static struct kobj_attribute ov3660_attribute =	__ATTR(ov3660, 0666, ov3660_show, ov3660_store);


static struct attribute *ov3660_attrs[] = {	
	&ov3660_attribute.attr,	
	NULL,	
};


static const struct attribute_group ov3660_group =
{
	.attrs = ov3660_attrs,
};
#endif
static int ov3660_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	aml_cam_info_t* plat_dat;
	struct ov3660_device *t;
	struct v4l2_subdev *sd;
	vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &ov3660_ops);
	mutex_init(&t->mutex);

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &ov3660_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);

	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "ov3660");
	/* Register it */
	plat_dat= (aml_cam_info_t*)client->dev.platform_data;
	if (plat_dat) {
	    memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
	    if (plat_dat->front_back >=0)  video_nr = plat_dat->front_back;
	} else {
	   printk("camera ov3660: have no platform data\n");
	   kfree(t);
	   return -1; 	
	}
	
	t->cam_info.version = OV3660_DRIVER_VERSION;
	if (aml_cam_info_reg(&t->cam_info) < 0)
		printk("reg caminfo error\n");
	
	err = video_register_device(t->vdev, VFL_TYPE_GRABBER, video_nr);
	if (err < 0) {
		video_device_release(t->vdev);
		kfree(t);
		return err;
	}
#ifdef EMDOOR_DEBUG_OV3660
	//add by emdoor jf.s for debug ov3660
	ov3660_client = client;
	err = sysfs_create_group(&client->dev.kobj, &ov3660_group);
#endif
	return 0;
}

static int ov3660_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov3660_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id ov3660_id[] = {
	{ "ov3660", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov3660_id);

static struct i2c_driver ov3660_i2c_driver = {
	.driver = {
		.name = "ov3660",
	},
	.probe = ov3660_probe,
	.remove = ov3660_remove,
	.id_table = ov3660_id,
};

module_i2c_driver(ov3660_i2c_driver);
