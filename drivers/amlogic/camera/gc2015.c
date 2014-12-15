/*
 *gc2015 - This code emulates a real video device with v4l2 api
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

#define gc2015_CAMERA_MODULE_NAME "gc2015"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)           /* 0.5 seconds */

#define gc2015_CAMERA_MAJOR_VERSION 0
#define gc2015_CAMERA_MINOR_VERSION 7
#define gc2015_CAMERA_RELEASE 0
#define gc2015_CAMERA_VERSION \
KERNEL_VERSION(gc2015_CAMERA_MAJOR_VERSION, gc2015_CAMERA_MINOR_VERSION, gc2015_CAMERA_RELEASE)

MODULE_DESCRIPTION("gc2015 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define GC2015_DRIVER_VERSION "GC2015-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static struct vdin_v4l2_ops_s *vops;

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static int vidio_set_fmt_ticks=0;

extern int disable_gt2005;

static int gc2015_h_active=800;
static int gc2015_v_active=600;

static struct v4l2_fract gc2015_frmintervals_active = {
	.numerator = 1,
	.denominator = 15,
};

/* supported controls */
static struct v4l2_queryctrl gc2015_qctrl[] = {
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

static struct v4l2_frmivalenum gc2015_frmivalenum[]={
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

struct gc2015_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct gc2015_fmt formats[] = {
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

static struct gc2015_fmt *get_format(struct v4l2_format *f)
{
	struct gc2015_fmt *fmt;
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
struct gc2015_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct gc2015_fmt        *fmt;
};

struct gc2015_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(gc2015_devicelist);

struct gc2015_device {
	struct list_head			gc2015_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct gc2015_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(gc2015_qctrl)];
};

static inline struct gc2015_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc2015_device, sd);
}

struct gc2015_fh {
	struct gc2015_device            *dev;

	/* video capture */
	struct gc2015_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int f_flags;
};

/*static inline struct gc2015_fh *to_fh(struct gc2015_device *dev)
{
	return container_of(dev, struct gc2015_fh, dev);
}*/

static struct v4l2_frmsize_discrete gc2015_prev_resolution[]= //should include 352x288 and 640x480, those two size are used for recording
{
	{320,240},
	{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete gc2015_pic_resolution[]=
{
	{1600,1200},
	{800,600}
};

/* ------------------------------------------------------------------
	reg spec of gc2015
   ------------------------------------------------------------------*/

struct aml_camera_i2c_fig_s gc2015_script[] = {
	{0xfe,0x80}, //soft reset
	{0xfe,0x80}, //soft reset
	{0xfe,0x80}, //soft reset

	{0xfe,0x00}, //page0
	{0x45,0x00}, //output_disable

	//////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////preview capture switch /////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////
		//preview
	{0x02,0x01}, //preview mode
	{0x2a,0xca}, //[7]col_binning,0x[6]even skip
	{0x48,0x40}, //manual_gain


	{0x09, 0x00}, //
	{0x0a, 0x01}, //row start
	
	{0x0b, 0x00}, //
	{0x0c, 0x0c}, //col start  james  0x10
	
	{0x0d, 0x04}, //0x04  20120629
	{0x0e, 0xd0}, //Preview Window height

	////////////////////////////////////////////////////////////////////////
	////////////////////////// preview LSC /////////////////////////////////
	////////////////////////////////////////////////////////////////////////
	#if 0
	{0xfe,0x01}, //page1
	{0xb0,0x03}, //[4]Y_LSC_en [3]lsc_compensate [2]signed_b4 [1:0]pixel array select
	{0xb1,0x23}, //P_LSC_red_b2
	{0xb2,0x20}, //P_LSC_green_b2
	{0xb3,0x20}, //P_LSC_blue_b2
	{0xb4,0x24}, //P_LSC_red_b4
	{0xb5,0x20}, //P_LSC_green_b4
	{0xb6,0x22}, //P_LSC_blue_b4
	{0xb7,0x00}, //P_LSC_compensate_b2
	{0xb8,0x80}, //P_LSC_row_center,0x344,0x (1200/2-344)/2=128,0x,0x
	{0xb9,0x80}, //P_LSC_col_center,0x544,0x (1600/2-544)/2=128


	////////////////////////////////////////////////////////////////////////
	////////////////////////// capture LSC /////////////////////////////////
	////////////////////////////////////////////////////////////////////////
	{0xba,0x03}, //[4]Y_LSC_en [3]lsc_compensate [2]signed_b4 [1:0]pixel array select
	{0xbb,0x23}, //C_LSC_red_b2
	{0xbc,0x20}, //C_LSC_green_b2
	{0xbd,0x20}, //C_LSC_blue_b2
	{0xbe,0x24}, //C_LSC_red_b4
	{0xbf,0x20}, //C_LSC_green_b4
	{0xc0,0x22}, //C_LSC_blue_b4
	{0xc1,0x00}, //C_Lsc_compensate_b2
	{0xc2,0x80}, //C_LSC_row_center,0x344,0x (1200/2-344)/2=128
	{0xc3,0x80}, //C_LSC_col_center,0x544,0x (1600/2-544)/2=128
	{0xfe,0x00}, //page0

#endif
	{0xfe,0x01},  //page1
	{0xb0,0x03},   //[4]Y_LSC_en [3]lsc_compensate [2]signed_b4 [1:0]pixel array select
	{0xb1,0x3e},   //P_LSC_red_b2
	{0xb2,0x3a},   //P_LSC_green_b2
	{0xb3,0x3a},   //P_LSC_blue_b2
	{0xb4,0x20},   //P_LSC_red_b4
	{0xb5,0x20},   //P_LSC_green_b4
	{0xb6,0x20},   //P_LSC_blue_b4
	{0xb7,0x00},   //P_LSC_compensate_b2
	{0xb8,0x80},   //P_LSC_row_center  344   (600/2-100)/2=100
	{0xb9,0x60},   //P_LSC_col_center  544   (800/2-200)/2=100
		
	{0xba,0x03},   //[4]Y_LSC_en [3]lsc_compensate [2]signed_b4 [1:0]pixel array select
	{0xbb,0x3e},   //C_LSC_red_b2 // 20
	{0xbc,0x3a},   //C_LSC_green_b2   // 20
	{0xbd,0x3a},   //C_LSC_blue_b2  // 20
	{0xbe,0x20},   //C_LSC_red_b4
	{0xbf,0x20},   //C_LSC_green_b4
	{0xc0,0x20},   //C_LSC_blue_b4
	{0xc1,0x00},   //C_Lsc_compensate_b2
	{0xc2,0x80},   //C_LSC_row_center   344    (0x1200/2-344)/2=128
	{0xc3,0x60},   //C_LSC_col_center   544    (0x1600/2-544)/2=128
	{0xfe,0x60}, //page0
	////////////////////////////////////////////////////////////////////////
	////////////////////////// analog configure ///////////////////////////
	////////////////////////////////////////////////////////////////////////
	{0xfe,0x00}, //page0
	{0x29,0x01}, //cisctl mode 1
	{0x2b,0x06}, //cisctl mode 3	
	{0x32,0x1c}, //analog mode 1
	{0x33,0x0f}, //analog mode 2
	{0x34,0x30}, //[6:4]da_rsg

	{0x35,0x88}, //Vref_A25
	{0x37,0x13}, //0x16  Drive Current

	/////////////////////////////////////////////////////////////////////
	/////////////////////////// ISP Related /////////////////////////////
	/////////////////////////////////////////////////////////////////////
	{0x40,0xff}, 
	{0x41,0x20}, //[5]skin_detectionenable[2]auto_gray,0x[1]y_gamma
	{0x42,0xf6}, //[7]auto_sa[6]auto_ee[5]auto_dndd[4]auto_lsc[3]na[2]abs,0x[1]awb
	{0x4b,0xe8}, //[1]AWB_gain_mode,0x1:atpregain0:atpostgain
	{0x4d,0x03}, //[1]inbf_en
	{0x4f,0x01}, //AEC enable

	////////////////////////////////////////////////////////////////////
	///////////////////////////  BLK  //////////////////////////////////
	////////////////////////////////////////////////////////////////////
	{0x63,0x77}, //BLK mode 1
	{0x66,0x00}, //BLK global offset
	{0x6d,0x00},
	{0x6e,0x1a}, //BLK offset submode},offset R
	{0x6f,0x20},
	{0x70,0x1a},
	{0x71,0x20},
	{0x73,0x00},
	{0x77,0x80}, //0x80  r gain
	{0x78,0x80},// 0x80  g gain
	{0x79,0x90},//0x90  b gain

	////////////////////////////////////////////////////////////////////
	/////////////////////////// DNDD ///////////////////////////////////
	////////////////////////////////////////////////////////////////////
	{0x80,0x07}, //[7]dn_inc_or_dec [4]zero_weight_mode[3]share [2]c_weight_adap [1]dn_lsc_mode [0]dn_b
	{0x82,0x0c}, //DN lilat b base
	{0x83,0x03},

	{0x7d,0x80},//0x80    r _ratio
	{0x7e,0x80},//0x80   g_ratio
	{0x7f,0x80},//0x80   b_ratio

	////////////////////////////////////////////////////////////////////
	/////////////////////////// EEINTP ////////////////////////////////
	////////////////////////////////////////////////////////////////////
	{0x8a,0x7c},
	{0x8c,0x02},
	{0x8e,0x02},
	{0x8f,0x45},


	/////////////////////////////////////////////////////////////////////
	/////////////////////////// CC_t ////////////////////////////////////
	/////////////////////////////////////////////////////////////////////
	#if 0
	{0xb0,0x48},
	{0xb1,0xfe},
	{0xb2,0x00},
	{0xb3,0xf0},
	{0xb4,0x50},
	{0xb5,0xf8},
	{0xb6,0x00},  // r offset
	{0xb7,0x00},  // g offset
	{0xb8,0x00}, // b offset
#endif
	{0xb0,0x40},  
	{0xb1,0xfe},   // 00
	{0xb2,0x00},   // 04
	{0xb3,0xf8},  
	{0xb4,0x48},  
	{0xb5,0xf8},  
	{0xb6,0x00},  
	{0xb7,0x04},  
	{0xb8,0x00},


	/////////////////////////////////////////////////////////////////////
	/////////////////////////// GAMMA ///////////////////////////////////
	/////////////////////////////////////////////////////////////////////
		//RGB_GAMMA
	/***********************
	{0xbf,0x08}, 
	{0xc0,0x1e},
	{0xc1,0x33},
	{0xc2,0x47},
	{0xc3,0x59},
	{0xc4,0x68},
	{0xc5,0x74},
	{0xc6,0x86},
	{0xc7,0x97},
	{0xc8,0xA5},
	{0xc9,0xB1},
	{0xca,0xBd},
	{0xcb,0xC8},
	{0xcc,0xD3},
	{0xcd,0xE4},
	{0xce,0xF4},
	{0xcf,0xff},
	
	{0xbF,0x0B}, //case Gamma_dm_3:
	{0xc0,0x16}, 
	{0xc1,0x29}, 
	{0xc2,0x3C}, 
	{0xc3,0x4F}, 
	{0xc4,0x5F}, 
	{0xc5,0x6F}, 
	{0xc6,0x8A}, 
	{0xc7,0x9F}, 
	{0xc8,0xB4}, 
	{0xc9,0xC6}, 
	{0xcA,0xD3}, 
	{0xcB,0xDD},  
	{0xcC,0xE5},  
	{0xcD,0xF1}, 
	{0xcE,0xFA}, 
	{0xcF,0xFF},
	********************/

	{0xbF,0x0B}, //case Gamma_dm_3:
	{0xc0,0x16}, 
	{0xc1,0x29}, 
	{0xc2,0x3C}, 
	{0xc3,0x4F}, 
	{0xc4,0x5F}, 
	{0xc5,0x6F}, 
	{0xc6,0x8A}, 
	{0xc7,0x9F}, 
	{0xc8,0xB4}, 
	{0xc9,0xC6}, 
	{0xcA,0xD3}, 
	{0xcB,0xDD},  
	{0xcC,0xE5},  
	{0xcD,0xF1}, 
	{0xcE,0xFA}, 
	{0xcF,0xFF},
	//////gamma//////


	/////////////////////////////////////////////////////////////////////
	/////////////////////////// YCP_t ///////////////////////////////////
	/////////////////////////////////////////////////////////////////////
	{0xd1,0x38}, //saturation
	{0xd2,0x38}, //saturation
	{0xdd,0x38}, //edge_dec
	{0xd3,0x40},//  0x40 contrast  
	{0xde,0x21}, //auto_gray

	////////////////////////////////////////////////////////////////////
	/////////////////////////// ASDE ///////////////////////////////////
	////////////////////////////////////////////////////////////////////
	{0x98,0x3a}, 
	{0x99,0x60}, 
	{0x9b,0x00}, 
	{0x9f,0x12}, 
	{0xa1,0x80}, 
	{0xa2,0x21}, 

	{0xfe,0x01}, //page1
	{0xc5,0x10}, 
	{0xc6,0xff}, 
	{0xc7,0xff}, 
	{0xc8,0xff}, 
#if 0  // 2015 A
	////////////////////////////////////////////////////////////////////
	/////////////////////////// AEC ////////////////////////////////////
	////////////////////////////////////////////////////////////////////
	{0x10,0x09}, //AEC mode 1
	{0x11,0xb2}, //[7]fix target
	{0x12,0x20}, 
	{0x13,0x78}, 
	{0x17,0x00}, 
	{0x1c,0x96}, 
	{0x1d,0x04}, // sunlight step 
	{0x1e,0x11}, 
	{0x21,0xc0}, //max_post_gain
	{0x22,0x60}, //max_pre_gain
	{0x2d,0x06}, //P_N_AEC_exp_level_1[12:8]
	{0x2e,0x00}, //P_N_AEC_exp_level_1[7:0]
	{0x1e,0x32}, 
	{0x33,0x00}, //[6:5]max_exp_level [4:0]min_exp_level
	{0x34,0x04}, // min exp

	////////////////////////////////////////////////////////////////////
	/////////////////////////// Measure Window /////////////////////////
	////////////////////////////////////////////////////////////////////
	{0x06,0x07},
	{0x07,0x03},
	{0x08,0x64},
	{0x09,0x4a},

	////////////////////////////////////////////////////////////////////
	/////////////////////////// AWB ////////////////////////////////////
	////////////////////////////////////////////////////////////////////
	{0x50,0xf5},
	{0x51,0x18},
	{0x53,0x10},
	{0x54,0x20},
	{0x55,0x60},
	{0x57,0x33}, //number limit }, 33 half }, must <0x65 base on measure wnd now
	{0x5d,0x52}, //44
	{0x5c,0x25}, //show mode},close dark_mode
	{0x5e,0x19}, //close color temp
	{0x5f,0x50}, //50
	{0x60,0x57}, //50
	{0x61,0xdf},
	{0x62,0x80}, //7b
	{0x63,0x08}, //20
	{0x64,0x5B},
	{0x65,0x90},
	{0x66,0xd0},
	{0x67,0x80}, //5a
	{0x68,0x68}, //68
	{0x69,0x90}, //80

	////////////////////////////////////////////////////////////////////
	/////////////////////////// ABS ////////////////////////////////////
	////////////////////////////////////////////////////////////////////
	{0x80,0x82},
	{0x81,0x00},
	{0x83,0x10}, //ABS Y stretch limit
	{0xfe,0x00},
////////////////////////////////////////////////////////////////////
/////////////////////////// OUT ////////////////////////////////////
////////////////////////////////////////////////////////////////////
	{0xfe,0x00},
#endif

	{0xfe,0x01},   //2015
	////////////////////////////////////////////////////////////////////
	/////////////////////////// AEC  ////////////////////////////////
	////////////////////////////////////////////////////////////////////
	{0x10 , 0x45}, //AEC mode 1
	{0x11 , 0x32}, //[7]fix target
	{0x13 , 0x68},   //0x60
	{0x17 , 0x00},
	{0x1c , 0x96},
	{0x1e , 0x11},
	{0x21 , 0xc0}, //max_post_gain
	{0x22 , 0x40}, //max_pre_gain
	{0x2d , 0x06}, //P_N_AEC_exp_level_1[12:8]
	{0x2e , 0x00}, //P_N_AEC_exp_level_1[7:0]
	{0x1e , 0x32},
	{0x33 , 0x00}, //[6:5]max_exp_level [4:0]min_exp_level

	////////////////////////////////////////////////////////////////////
	///////////////////////////  AWB  ////////////////////////////////
	////////////////////////////////////////////////////////////////////
	{0x57 , 0x40}, //number limit
	{0x5d , 0x44}, //
	{0x5c , 0x35}, //show mode,close dark_mode
	{0x5e , 0x29}, //close color temp
	{0x5f , 0x50},
	{0x60 , 0x50}, 
	{0x65 , 0xc0},

	////////////////////////////////////////////////////////////////////
	///////////////////////////  ABS  ////////////////////////////////
	////////////////////////////////////////////////////////////////////
	{0x80 , 0x82},
	{0x81 , 0x00},
	{0x83 , 0x00}, //ABS Y stretch limit

	{0xfe,0x00},

	//crop 
	{0x50,0x01},
	{0x51,0x00},
	{0x52,0x00},
	{0x53,0x00},
	{0x54,0x00},
	{0x55,0x02},
	{0x56,0x5c},
	{0x57,0x03},
	{0x58,0x20},

	{0x44,0xa3}, //YUV sequence
	{0x45,0x0f}, //output enable
	{0x46,0x03}, //sync mode

	/////write more registers
	{0xfe,0x00},
	{0x32,0x34},
	{0x34,0x00},

	/////pv_setting},even_skip(default_preview) start/////
	{0x02,0x01}, 
	{0x2a,0xca}, 

	{0x55,0x02},
	{0x56,0x5c},  //604
	{0x57,0x03},
	{0x58,0x20},//800
		
	{0x48,0x40}, //global_gain

	{0x63,0x77}, //BLK mode

	{0x6e,0x1a},
	{0x70,0x1a},
	/////pv_setting},even_skip(default_preview) end/////	


	//////////banding////////////
#ifdef GC2015_48MHz_MCLK

	{0x05 ,0x01},//HB
	{0x06 ,0xc1},
	{0x07 ,0x00},//VB
	{0x08 ,0x40},

	{0xfe ,0x01},
	{0x29 ,0x01},//Anti Step
	{0x2a ,0x00},

	{0x2b ,0x05},//Level_0  20fps
	{0x2c ,0x00},
	{0x2d ,0x08},//Level_1  12.5fps
	{0x2e ,0x00},
	{0x2f ,0x0c},//Level_2  8.33fps
	{0x30 ,0x00},
	{0x31 ,0x10},//Level_3  6.25fps
	{0x32 ,0x00},
	{0xfe ,0x00},
#else			
	{0x05 ,0x01},//HB
	{0x06 ,0xc1},
	{0x07 ,0x00},//VB
	{0x08 ,0x40},

	{0xfe ,0x01},
	{0x29 ,0x00},//Anti Step 128
	{0x2a ,0x80},

	{0x2b ,0x05},//Level_0  10.00fps
	{0x2c ,0x00},
	{0x2d ,0x06},//Level_1   8.33fps
	{0x2e ,0x00},
	{0x2f ,0x08},//Level_2   6.25fps
	{0x30 ,0x00},
	{0x31 ,0x09},//Level_3   5.55fps
	{0x32 ,0x00},
	{0xfe ,0x00},
#endif
			
	{0x43 ,0x00},  //effect normal
	{0x42 ,0xf6},  // wb normal
	{0xff,0xff}, 

};

//load gc2015 parameters
void gc2015_init_regs(struct gc2015_device *dev)
{
	int i=0;//,j;
	unsigned char buf[2];
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
	while(1) {
		buf[0] = gc2015_script[i].addr;//(unsigned char)((gc2015_script[i].addr >> 8) & 0xff);
		//buf[1] = (unsigned char)(gc2015_script[i].addr & 0xff);
		buf[1] = gc2015_script[i].val;
		i++;
		if (gc2015_script[i].val==0xff&&gc2015_script[i].addr==0xff) {
			printk("gc2015_write_regs success in initial gc2015.\n");
			break;
	 	}
		if((i2c_put_byte_add8(client,buf, 2)) < 0) {
			printk("fail in initial gc2015. \n");
			return;
	    	}
	}
	return;
}
/*************************************************************************
* FUNCTION
*    gc2015_set_param_wb
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
void gc2015_set_param_wb(struct gc2015_device *dev,enum  camera_wb_flip_e para)//white balance
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

        unsigned char buf[4];


	switch (para) {
	case CAM_WB_AUTO://auto
	    	printk("CAM_WB_AUTO       \n");
		buf[0]=0x42;
		buf[1]=0x76;
		i2c_put_byte_add8(client,buf,2);		
		break;

	case CAM_WB_CLOUD: //cloud
		printk("CAM_WB_CLOUD       \n");
		buf[0]=0x42;
		buf[1]=0x74;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7a;
		buf[1]=0x8c;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7b;
		buf[1]=0x50;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7c;
		buf[1]=0x40;
		i2c_put_byte_add8(client,buf,2);			
		break;

	case CAM_WB_DAYLIGHT: //
		printk("CAM_WB_DAYLIGHT       \n");
		buf[0]=0x42;
		buf[1]=0x74;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7a;
		buf[1]=0x74;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7b;
		buf[1]=0x52;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7c;
		buf[1]=0x40;
		i2c_put_byte_add8(client,buf,2);
		break;

	case CAM_WB_INCANDESCENCE:
		printk("CAM_WB_INCANDESCENCE       \n");
		buf[0]=0x42;
		buf[1]=0x74;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7a;
		buf[1]=0x48;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7b;
		buf[1]=0x40;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7c;
		buf[1]=0x5c;
		i2c_put_byte_add8(client,buf,2);
		break;

	case CAM_WB_TUNGSTEN:
		printk("CAM_WB_TUNGSTEN       \n");
		buf[0]=0x42;
		buf[1]=0x74;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7a;
		buf[1]=0x40;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7b;
		buf[1]=0x54;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7c;
		buf[1]=0x70;
		i2c_put_byte_add8(client,buf,2);
		break;

	case CAM_WB_FLUORESCENT:
		printk("CAM_WB_FLUORESCENT       \n");
		buf[0]=0x42;
		buf[1]=0x74;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7a;
		buf[1]=0x40;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7b;
		buf[1]=0x42;
		i2c_put_byte_add8(client,buf,2);

		buf[0]=0x7c;
		buf[1]=0x50;
		i2c_put_byte_add8(client,buf,2);
		break;

	case CAM_WB_MANUAL:
	    	                      // TODO
		break;
			
		default:
			break;
	}

} /* gc2015_set_param_wb */
/*************************************************************************
* FUNCTION
*    gc2015_set_param_exposure
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
void gc2015_set_param_exposure(struct gc2015_device *dev,enum camera_exposure_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
        unsigned char buf[4];
	switch (para) {	
	case EXPOSURE_N4_STEP:	
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0x13;
		buf[1]=0x40;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xd5;
		buf[1]=0xc0;
		i2c_put_byte_add8(client,buf,2);
		break;
	case EXPOSURE_N3_STEP:	
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0x13;
		buf[1]=0x48;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xd5;
		buf[1]=0xd0;
		i2c_put_byte_add8(client,buf,2);
		break;
	case EXPOSURE_N2_STEP:	
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0x13;
		buf[1]=0x50;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xd5;
		buf[1]=0xe0;
		i2c_put_byte_add8(client,buf,2);
		break;
	case EXPOSURE_N1_STEP:	
		printk("EXPOSURE_N1_STEP       \n");
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0x13;
		buf[1]=0x58;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xd5;
		buf[1]=0xf0;
		i2c_put_byte_add8(client,buf,2);
		break;
	
	case EXPOSURE_0_STEP:
	            printk("EXPOSURE_0_STEP       \n");
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0x13;
		buf[1]=0x68;//0x60
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xd5;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);			
		break;
	
	case EXPOSURE_P1_STEP:
	            printk("EXPOSURE_P1_STEP       \n");	
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0x13;
		buf[1]=0x68;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xd5;
		buf[1]=0x10;
		i2c_put_byte_add8(client,buf,2);
		break;
	
	case EXPOSURE_P2_STEP:			
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0x13;
		buf[1]=0x70;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xd5;
		buf[1]=0x20;
		i2c_put_byte_add8(client,buf,2);
		break;
	
	case EXPOSURE_P3_STEP:
		
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0x13;
		buf[1]=0x78;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xd5;
		buf[1]=0x30;
		i2c_put_byte_add8(client,buf,2);
		break;
	
	case EXPOSURE_P4_STEP:			
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0x13;
		buf[1]=0x80;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xd5;
		buf[1]=0x40;
		i2c_put_byte_add8(client,buf,2);
		break;
	
	default:			
		buf[0]=0xfe;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0x13;
		buf[1]=0x60;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	
		buf[0]=0xd5;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		break;
	}
	mdelay(150);

} /* gc2015_set_param_exposure */
/*************************************************************************
* FUNCTION
*    gc2015_set_param_effect
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
void gc2015_set_param_effect(struct gc2015_device *dev,enum camera_effect_flip_e para)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//unsigned char buf[4];
/*

    switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL:

			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x43;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
				
			break;

		case CAM_EFFECT_ENC_GRAYSCALE:

			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x43;
			buf[1]=0x02;
			i2c_put_byte_add8(client,buf,2);

			
			buf[0]=0xda;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			
			buf[0]=0xdb;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			break;
			
		case CAM_EFFECT_ENC_SEPIA:

			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x43;
			buf[1]=0x02;
			i2c_put_byte_add8(client,buf,2);

			
			buf[0]=0xda;
			buf[1]=0xd0;
			i2c_put_byte_add8(client,buf,2);

			
			buf[0]=0xdb;
			buf[1]=0x28;
			i2c_put_byte_add8(client,buf,2);
		
			break;

		case CAM_EFFECT_ENC_SEPIAGREEN:

			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x43;
			buf[1]=0x02;
			i2c_put_byte_add8(client,buf,2);

			
			buf[0]=0xda;
			buf[1]=0xc0;
			i2c_put_byte_add8(client,buf,2);

			
			buf[0]=0xdb;
			buf[1]=0xc0;
			i2c_put_byte_add8(client,buf,2);
			
			break;

		case CAM_EFFECT_ENC_SEPIABLUE:

			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x43;
			buf[1]=0x02;
			i2c_put_byte_add8(client,buf,2);

			
			buf[0]=0xda;
			buf[1]=0x50;
			i2c_put_byte_add8(client,buf,2);

			
			buf[0]=0xdb;
			buf[1]=0xe0;
			i2c_put_byte_add8(client,buf,2);
		
			break;

		case CAM_EFFECT_ENC_COLORINV:
			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x43;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);

			
			break;		

		default:
			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x43;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			break;
	}

*/

} /* gc2015_set_param_effect */

/*************************************************************************
* FUNCTION
*    gc2015_NightMode
*
* DESCRIPTION
*    This function night mode of gc2015.
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
void gc2015_set_night_mode(struct gc2015_device *dev,enum  camera_night_mode_flip_e enable)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//unsigned char buf[4];
/*
	if (enable)
	{
			buf[0]=0xfe;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x33;
			buf[1]=0x60;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
	}
	else
	{
			buf[0]=0xfe;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x33;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
	}
*/
}    /* gc2015_NightMode */
void gc2015_set_param_banding(struct gc2015_device *dev,enum  camera_banding_flip_e banding)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	#if 1
	switch(banding)
		{

		case CAM_BANDING_50HZ:

			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x05;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x06;
			buf[1]=0xc1;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x07;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			
			buf[0]=0x08;
			buf[1]=0x40;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0xfe;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x29;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2a;
			buf[1]=0x80;  //step
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2b;
			buf[1]=0x05;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2c;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2d;
			buf[1]=0x06;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2e;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2f;
			buf[1]=0x08;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x30;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x31;
			buf[1]=0x09;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x32;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			break;
		case CAM_BANDING_60HZ:

			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x05;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x06;
			buf[1]=0xd9;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x07;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			
			buf[0]=0x08;
			buf[1]=0x20;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0xfe;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x29;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2a;
			buf[1]=0x68;  //step
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2b;
			buf[1]=0x04;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2c;
			buf[1]=0xe0;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2d;
			buf[1]=0x05;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2e;
			buf[1]=0xb0;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x2f;
			buf[1]=0x06;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x30;
			buf[1]=0xe8;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x31;
			buf[1]=0x08;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x32;
			buf[1]=0x20;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0xfe;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
	             			
			break;
		    default:
		    	break;

		}
	#endif	
}      

static int set_flip(struct gc2015_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	unsigned char buf[2];
	buf[0] = 0xfe;
	buf[1] = 0x00;
	if((i2c_put_byte_add8(client, buf, 2)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
	}
	temp = i2c_get_byte_add8(client, 0x29);
	temp &= 0xfc;
	temp |= dev->cam_info.m_flip << 0;
	temp |= dev->cam_info.v_flip << 1;
	buf[0] = 0x29;
	buf[1] = temp;
	if((i2c_put_byte_add8(client,buf, 2)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
	}
	return 0;
}

void gc2015_set_resolution(struct gc2015_device *dev,int height,int width)
{
#if 1
	unsigned char buf[4];
	unsigned  int value;
	unsigned   int pid=0,shutter;
	unsigned int  hb_total=298;
	unsigned int  temp_reg;
	static unsigned int shutter_l = 0;
	static unsigned int shutter_h = 0;
	
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
	if ((width<1600)&&(height<1200)) {
		//800*600 
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		/* rewrite shutter : 0x03, 0x04*/
		#if 1
		buf[0]=0x03;
		buf[1]=(unsigned char)shutter_l;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x04;
		buf[1]=(unsigned char)shutter_h;
		i2c_put_byte_add8(client,buf,2);
		#endif
		
		buf[0]=0x02;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x2a;
		buf[1]=0xca;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x55;
		buf[1]=0x02;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x56;
		buf[1]=0x5c;  //604
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x57;
		buf[1]=0x03;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x58;
		buf[1]=0x20;   // 800
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x48;
		buf[1]=0x58;
		i2c_put_byte_add8(client,buf,2);
		
		mdelay(250);
		
		buf[0]=0x4f;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		
		gc2015_h_active=800;
		gc2015_v_active=600;
		gc2015_frmintervals_active.denominator = 15;
		gc2015_frmintervals_active.numerator = 1;

		mdelay(50);
	} else if(width>=1600&&height>=1200) {
		#if  1
		buf[0]=0x4f;
		buf[1]=0x00;   //aec off
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x03;  //0x00
		//value=i2c_get_byte(client, 0x03);
		value=i2c_get_byte_add8(client, 0x03);
		shutter_l = value;
		printk( KERN_INFO" set camera  GC2015_set_resolution=0x03=0x%x \n ",value);
			
		pid |= (value << 8);
		buf[0]=0x04; //0x00
		//value=i2c_get_byte(client, 0x04);
		value=i2c_get_byte_add8(client, 0x04);
		shutter_h = value;
		printk( KERN_INFO" set camera  GC2015_set_resolution=0x04=0x%x \n ",value);			
		pid |= (value & 0xff);
		
		shutter=pid;
		printk( KERN_INFO" set camera  GC2015_set_resolution=shutter=0x%x \n ",shutter);
           	#endif		
		buf[0]=0xfe;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x02;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x2a;
		buf[1]=0x0a;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x59;
		buf[1]=0x11;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x5a;
		buf[1]=0x06;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x5b;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x5c;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x5d;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x5e;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x5f;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x60;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);	
		
		buf[0]=0x61;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x62;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);	
		
		
		buf[0]=0x50;
		buf[1]=0x01;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x51;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x52;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x53;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x54;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x55;
		buf[1]=0x04;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x56;
		buf[1]=0xb2; //1202
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x57;
		buf[1]=0x06;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x58;
		buf[1]=0x40; // 1600
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x48;
		buf[1]=0x60;  //78global  gain
		i2c_put_byte_add8(client,buf,2);

		mdelay(50);
		#if  1
		
		buf[0]=0x12;
		buf[1]= ((hb_total>>8)&0xff);
		i2c_put_byte_add8(client,buf,2);
		
		
		buf[0]=0x13;
		buf[1]= (hb_total&0xff);
		i2c_put_byte_add8(client,buf,2);
		
		
		temp_reg = shutter * 10  /  16 ;
		if(temp_reg < 1) temp_reg = 1;
		
		printk( KERN_INFO" set camera  GC2015_set_resolution=temp_ret=0x%x \n ",temp_reg);
		buf[0]=0x03;
		buf[1]= ((temp_reg>>8)&0xff);
		i2c_put_byte_add8(client,buf,2);
		
		
		buf[0]=0x04;
		buf[1]= (temp_reg&0xff);
		i2c_put_byte_add8(client,buf,2);
		#if 0
		buf[0]=0x6e;
		buf[1]= 0x1b;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x6f;
		buf[1]= 0x20;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x70;
		buf[1]= 0x1b;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x71;
		buf[1]= 0x20;
		i2c_put_byte_add8(client,buf,2);
		#endif
		#endif		
		gc2015_h_active=1600;
		gc2015_v_active=1200;
		gc2015_frmintervals_active.denominator = 5;
		gc2015_frmintervals_active.numerator = 1;
		mdelay(150);
	}
	printk(KERN_INFO " set camera  GC2015_set_resolution=w=%d,h=%d. \n ",width,height);	
#endif
	set_flip(dev);
}    /* gc2015_set_resolution */

unsigned char v4l_2_gc2015(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int gc2015_setting(struct gc2015_device *dev,int PROP_ID,int value )
{
	int ret=0;
	//unsigned char cur_val;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_gc2015(value));
	//	ret=i2c_put_byte(client,0x0201,v4l_2_gc2015(value));
		break;
	case V4L2_CID_CONTRAST:
	//	ret=i2c_put_byte(client,0x0200, value);
		break;
	case V4L2_CID_SATURATION:
	//	ret=i2c_put_byte(client,0x0202, value);
		break;
#if 0
	case V4L2_CID_EXPOSURE:
		ret=i2c_put_byte(client,0x0201, value);
		break;

	case V4L2_CID_HFLIP:    /* set flip on H. */
		
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		
		break;
#endif
	case V4L2_CID_DO_WHITE_BALANCE:
		if(gc2015_qctrl[0].default_value!=value){
			gc2015_qctrl[0].default_value=value;
			gc2015_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
        	}
		break;
	case V4L2_CID_EXPOSURE:
		if(gc2015_qctrl[1].default_value!=value){
			gc2015_qctrl[1].default_value=value;
			gc2015_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
        	}
		break;
	case V4L2_CID_COLORFX:
		if(gc2015_qctrl[2].default_value!=value){
			gc2015_qctrl[2].default_value=value;
			gc2015_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
        	}
		break;
	case V4L2_CID_WHITENESS:
		 if(gc2015_qctrl[3].default_value!=value){
			gc2015_qctrl[3].default_value=value;
			gc2015_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
        	}
		break;
	case V4L2_CID_BLUE_BALANCE:
		 if(gc2015_qctrl[4].default_value!=value){
			gc2015_qctrl[4].default_value=value;
			gc2015_set_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(gc2015_qctrl[5].default_value!=value){
			gc2015_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(gc2015_qctrl[7].default_value!=value){
			gc2015_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
        	}
		break;
	case V4L2_CID_ROTATE:
		if(gc2015_qctrl[8].default_value!=value){
			gc2015_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;

	

}

static void power_down_gc2015(struct gc2015_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	buf[0]=0x45;
	buf[1]=0x00;
	i2c_put_byte_add8(client,buf,2);
}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void gc2015_fillbuff(struct gc2015_fh *fh, struct gc2015_buffer *buf)
{
	struct gc2015_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = gc2015_qctrl[5].default_value&3;
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = gc2015_qctrl[7].default_value;
	para.vaddr = (unsigned)vbuf;
	para.angle = gc2015_qctrl[8].default_value;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void gc2015_thread_tick(struct gc2015_fh *fh)
{
	struct gc2015_buffer *buf;
	struct gc2015_device *dev = fh->dev;
	struct gc2015_dmaqueue *dma_q = &dev->vidq;

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
			 struct gc2015_buffer, vb.queue);
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
	gc2015_fillbuff(fh, buf);
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

static void gc2015_sleep(struct gc2015_fh *fh)
{
	struct gc2015_device *dev = fh->dev;
	struct gc2015_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	gc2015_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int gc2015_thread(void *data)
{
	struct gc2015_fh  *fh = data;
	struct gc2015_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		gc2015_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int gc2015_start_thread(struct gc2015_fh *fh)
{
	struct gc2015_device *dev = fh->dev;
	struct gc2015_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(gc2015_thread, fh, "gc2015");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void gc2015_stop_thread(struct gc2015_dmaqueue  *dma_q)
{
	struct gc2015_device *dev = container_of(dma_q, struct gc2015_device, vidq);

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
	struct gc2015_fh  *fh = vq->priv_data;
	struct gc2015_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct gc2015_buffer *buf)
{
	struct gc2015_fh  *fh = vq->priv_data;
	struct gc2015_device *dev  = fh->dev;

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
	struct gc2015_fh     *fh  = vq->priv_data;
	struct gc2015_device    *dev = fh->dev;
	struct gc2015_buffer *buf = container_of(vb, struct gc2015_buffer, vb);
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
	struct gc2015_buffer    *buf  = container_of(vb, struct gc2015_buffer, vb);
	struct gc2015_fh        *fh   = vq->priv_data;
	struct gc2015_device       *dev  = fh->dev;
	struct gc2015_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct gc2015_buffer   *buf  = container_of(vb, struct gc2015_buffer, vb);
	struct gc2015_fh       *fh   = vq->priv_data;
	struct gc2015_device      *dev  = (struct gc2015_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops gc2015_video_qops = {
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
	struct gc2015_fh  *fh  = priv;
	struct gc2015_device *dev = fh->dev;

	strcpy(cap->driver, "gc2015");
	strcpy(cap->card, "gc2015");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = gc2015_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct gc2015_fmt *fmt;

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
	
	if(fival->index > ARRAY_SIZE(gc2015_frmivalenum))
	return -EINVAL;
	
	for(k =0; k< ARRAY_SIZE(gc2015_frmivalenum); k++)
	{
		if( (fival->index==gc2015_frmivalenum[k].index)&&
				(fival->pixel_format ==gc2015_frmivalenum[k].pixel_format )&&
				(fival->width==gc2015_frmivalenum[k].width)&&
				(fival->height==gc2015_frmivalenum[k].height)){
			memcpy( fival, &gc2015_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
			return 0;
		}
	}
	
	return -EINVAL;

}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct gc2015_fh *fh = priv;

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
	struct gc2015_fh *fh = priv;
	struct gc2015_device *dev = fh->dev;
	struct v4l2_captureparm *cp = &parms->parm.capture;
	
	dprintk(dev,3,"vidioc_g_parm\n");
	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	
	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	
	cp->timeperframe = gc2015_frmintervals_active;
	printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
	cp->timeperframe.numerator );
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct gc2015_fh  *fh  = priv;
	struct gc2015_device *dev = fh->dev;
	struct gc2015_fmt *fmt;
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
	struct gc2015_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct gc2015_device *dev = fh->dev;

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
		gc2015_set_resolution(dev,fh->height,fh->width);
	} else if(vidio_set_fmt_ticks==1){
		gc2015_set_resolution(dev,fh->height,fh->width);
	}
	#endif
	
	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct gc2015_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gc2015_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gc2015_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct gc2015_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct gc2015_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct gc2015_fh  *fh = priv;
	struct gc2015_device *dev = fh->dev;
	vdin_parm_t para;
	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para));
	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX+1;//TVIN_SIG_FMT_MAX+1;;TVIN_SIG_FMT_CAMERA_1280X720P_30Hz
	para.frame_rate = gc2015_frmintervals_active.denominator
				/gc2015_frmintervals_active.numerator;//175
	para.h_active = gc2015_h_active;
	para.v_active = gc2015_v_active;
	para.hsync_phase = 1;
	para.vsync_phase  = 1;
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
	para.skip_count =  2; //skip_num
	para.bt_path = dev->cam_info.bt_path;
	printk("gc2015,h=%d, v=%d, frame_rate=%d\n", 
		gc2015_h_active, gc2015_v_active, para.frame_rate);
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
		vops->start_tvin_service(0,&para);
		fh->stream_on        = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct gc2015_fh  *fh = priv;

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
	struct gc2015_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(gc2015_prev_resolution))
			return -EINVAL;
		frmsize = &gc2015_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(gc2015_pic_resolution))
			return -EINVAL;
		frmsize = &gc2015_pic_resolution[fsize->index];
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
	struct gc2015_fh *fh = priv;
	struct gc2015_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct gc2015_fh *fh = priv;
	struct gc2015_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(gc2015_qctrl); i++)
		if (qc->id && qc->id == gc2015_qctrl[i].id) {
			memcpy(qc, &(gc2015_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct gc2015_fh *fh = priv;
	struct gc2015_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(gc2015_qctrl); i++)
		if (ctrl->id == gc2015_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct gc2015_fh *fh = priv;
	struct gc2015_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(gc2015_qctrl); i++)
		if (ctrl->id == gc2015_qctrl[i].id) {
			if (ctrl->value < gc2015_qctrl[i].minimum ||
			    ctrl->value > gc2015_qctrl[i].maximum ||
			    gc2015_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int gc2015_open(struct file *file)
{
	struct gc2015_device *dev = video_drvdata(file);
	struct gc2015_fh *fh = NULL;
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
	gc2015_init_regs(dev);
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
	fh->width    = 800;
	fh->height   = 600;
	fh->stream_on = 0 ;
	fh->f_flags = file->f_flags;
	/* Resets frame counters */
	dev->jiffies = jiffies;

//    TVIN_SIG_FMT_CAMERA_640X480P_30Hz,
//    TVIN_SIG_FMT_CAMERA_800X600P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1024X768P_30Hz, // 190
//    TVIN_SIG_FMT_CAMERA_1920X1080P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1280X720P_30Hz,

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &gc2015_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct gc2015_buffer), fh,NULL);

	gc2015_start_thread(fh);

	return 0;
}

static ssize_t
gc2015_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct gc2015_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
gc2015_poll(struct file *file, struct poll_table_struct *wait)
{
	struct gc2015_fh        *fh = file->private_data;
	struct gc2015_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int gc2015_close(struct file *file)
{
	struct gc2015_fh         *fh = file->private_data;
	struct gc2015_device *dev       = fh->dev;
	struct gc2015_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);

	gc2015_stop_thread(vidq);
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
	gc2015_h_active=800;
	gc2015_v_active=600;
	gc2015_qctrl[0].default_value=0;
	gc2015_qctrl[1].default_value=4;
	gc2015_qctrl[2].default_value=0;
	gc2015_qctrl[3].default_value=0;
	gc2015_qctrl[4].default_value=0;

	gc2015_qctrl[5].default_value=0;
	gc2015_qctrl[7].default_value=100;
	gc2015_qctrl[8].default_value=0;
	
	gc2015_frmintervals_active.numerator = 1;
	gc2015_frmintervals_active.denominator = 15;

	power_down_gc2015(dev);
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

static int gc2015_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gc2015_fh  *fh = file->private_data;
	struct gc2015_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations gc2015_fops = {
	.owner		= THIS_MODULE,
	.open           = gc2015_open,
	.release        = gc2015_close,
	.read           = gc2015_read,
	.poll		= gc2015_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = gc2015_mmap,
};

static const struct v4l2_ioctl_ops gc2015_ioctl_ops = {
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

static struct video_device gc2015_template = {
	.name		= "gc2015_v4l",
	.fops           = &gc2015_fops,
	.ioctl_ops 	= &gc2015_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int gc2015_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_GT2005, 0);
}

static const struct v4l2_subdev_core_ops gc2015_core_ops = {
	.g_chip_ident = gc2015_g_chip_ident,
};

static const struct v4l2_subdev_ops gc2015_ops = {
	.core = &gc2015_core_ops,
};

static int gc2015_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	struct gc2015_device *t;
	struct v4l2_subdev *sd;
	aml_cam_info_t* plat_dat;
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &gc2015_ops);
	mutex_init(&t->mutex);

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &gc2015_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);
	
	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "gc2015");
	/* Register it */
	plat_dat= (aml_cam_info_t*)client->dev.platform_data;
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
	
	t->cam_info.version = GC2015_DRIVER_VERSION;
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

static int gc2015_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2015_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id gc2015_id[] = {
	{ "gc2015", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gc2015_id);

static struct i2c_driver gc2015_i2c_driver = {
	.driver = {
		.name = "gc2015",
	},
	.probe = gc2015_probe,
	.remove = gc2015_remove,
	.id_table = gc2015_id,
};

module_i2c_driver(gc2015_i2c_driver);

