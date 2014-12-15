/*
 *hi253 - This code emulates a real video device with v4l2 api
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
 //20110916_Amlogic_8726m1+HI253_Brian_V1.0
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

#define HI253_CAMERA_MODULE_NAME "hi253"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define HI253_CAMERA_MAJOR_VERSION 0
#define HI253_CAMERA_MINOR_VERSION 7
#define HI253_CAMERA_RELEASE 0
#define HI253_CAMERA_VERSION \
	KERNEL_VERSION(HI253_CAMERA_MAJOR_VERSION, HI253_CAMERA_MINOR_VERSION, HI253_CAMERA_RELEASE)

MODULE_DESCRIPTION("hi253 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define HI253_DRIVER_VERSION "HI253-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static struct vdin_v4l2_ops_s *vops;

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static int vidio_set_fmt_ticks=0;


static int hi253_h_active=800;
static int hi253_v_active=600;

//static int hi253_have_open=0;

/* supported controls */
static struct v4l2_queryctrl hi253_qctrl[] = {
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

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct hi253_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct hi253_fmt formats[] = {
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

static struct hi253_fmt *get_format(struct v4l2_format *f)
{
	struct hi253_fmt *fmt;
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
struct hi253_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct hi253_fmt        *fmt;
};

struct hi253_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(hi253_devicelist);

struct hi253_device {
	struct list_head			hi253_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct hi253_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t  cam_info;
	
	
	/* wake lock */
	struct wake_lock	wake_lock;
	
	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(hi253_qctrl)];
};

static inline struct hi253_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct hi253_device, sd);
}

struct hi253_fh {
	struct hi253_device            *dev;

	/* video capture */
	struct hi253_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
};

/*static inline struct hi253_fh *to_fh(struct hi253_device *dev)
{
	return container_of(dev, struct hi253_fh, dev);
}*/

static struct v4l2_frmsize_discrete hi253_prev_resolution[]= //should include 352x288 and 640x480, those two size are used for recording
{
	{320,240},
	{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete hi253_pic_resolution[]=
{
	{1600,1200},
	{800,600}
};

/* ------------------------------------------------------------------
	reg spec of HI253
   ------------------------------------------------------------------*/

struct aml_camera_i2c_fig_s HI253_script[] ={

{0x01, 0xf9}, //sleep on        
{0x08, 0x0f}, //Hi-Z on         
{0x01, 0xf8}, //sleep off       
{0x03, 0x00}, // Dummy 750us STA
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00}, // Dummy 750us END
{0x0e, 0x03}, //PLL On          
{0x0e, 0x73}, //PLLx2           
{0x03, 0x00}, // Dummy 750us STA
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00}, // Dummy 750us END
{0x0e, 0x00}, //PLL off         
{0x01, 0xf1}, //sleep on        
{0x08, 0x00}, //Hi-Z off        
{0x01, 0xf3},                   
{0x01, 0xf1},                   
// PAGE 20                        
{0x03, 0x20}, //page 20         
{0x10, 0x1c}, //ae off          
// PAGE 22                        
{0x03, 0x22}, //page 22         
{0x10, 0x69}, //awb off         
/////// PAGE 0 START ///////      
{0x03, 0x00},                   
{0x10, 0x11}, // Sub1/2_Preview2
{0x11, 0x90},  //92                 
{0x12, 0x24},  //00                 
{0x0b, 0xaa}, // ESD Check Regis
{0x0c, 0xaa}, // ESD Check Regis
{0x0d, 0xaa}, // ESD Check Regis
{0x20, 0x00}, // Windowing start
{0x21, 0x00},  //04                 
{0x22, 0x00}, // Windowing start
{0x23, 0x07},  //07                 
{0x24, 0x04},                   
{0x25, 0xb4},//b0                   
{0x26, 0x06},                   
{0x27, 0x44}, // WINROW END   //40  
{0x40, 0x01}, //Hblank 408      
{0x41, 0x68},                   
{0x42, 0x00}, //Vblank 20       
{0x43, 0x34}, //14                  
{0x45, 0x04},                   
{0x46, 0x18},                   
{0x47, 0xd8},                   
//B00LC                           
{0x80, 0x2e},                   
{0x81, 0x7e},                   
{0x82, 0x90},                   
{0x83, 0x00},                   
{0x84, 0x0c},                   
{0x85, 0x00},                   
{0x90, 0x0e}, //BLC_TIME_TH_ON  
{0x91, 0x0e}, //BLC_TIME_TH_OFF 
{0x92, 0x78}, //BLC_AG_TH_ON    
{0x93, 0x70}, //BLC_AG_TH_OFF   
{0x94, 0x75},                   
{0x95, 0x70},                   
{0x96, 0xdc},                   
{0x97, 0xfe},                   
{0x98, 0x38},                   
//OutDoor  BLC                    
{0x99, 0x43},                   
{0x9a, 0x43},                   
{0x9b, 0x43},                   
{0x9c, 0x43},                   
//D00ark BLC                      
{0xa0, 0x00},                   
{0xa2, 0x00},                   
{0xa4, 0x00},                   
{0xa6, 0x00},                   
//N00ormal BLC                    
{0xa8, 0x03},                   
{0xaa, 0x03},                   
{0xac, 0x03},                   
{0xae, 0x03},                   
///00//// PAGE 2 START ///////    
{0x03, 0x02},                   
{0x12, 0x03},                   
{0x13, 0x03},                   
{0x16, 0x00},                   
{0x17, 0x8C},                   
{0x18, 0x4c}, //Double_AG off   
{0x19, 0x00},                   
{0x1a, 0x39}, //ADC400->560     
{0x1c, 0x09},                   
{0x1d, 0x40},                   
{0x1e, 0x30},                   
{0x1f, 0x10},                   
{0x20, 0x77},                   
{0x21, 0xde},                   
{0x22, 0xa7},                   
{0x23, 0x30}, //CLAMP           
{0x27, 0x3c},                   
{0x2b, 0x80},                   
{0x2e, 0x00},//11               
{0x2f, 0x00},//a1               
{0x30, 0x05}, //For Hi-253 never
{0x50, 0x20},  
{0x51, 0x03},                  
{0x52, 0x01},                   
{0x53, 0xc1}, //20101203_Ãß°¡   
{0x55, 0x1c},                   
{0x56, 0x11},                   
{0x5d, 0xa2},                   
{0x5e, 0x5a},                   
{0x60, 0x87},                   
{0x61, 0x99},                   
{0x62, 0x88},                   
{0x63, 0x97},                   
{0x64, 0x88},                   
{0x65, 0x97},                   
{0x67, 0x0c},                   
{0x68, 0x0c},                   
{0x69, 0x0c},                   
{0x72, 0x89},                   
{0x73, 0x96},                   
{0x74, 0x89},                   
{0x75, 0x96},                   
{0x76, 0x89},                   
{0x77, 0x96},                   
{0x7c, 0x85},                   
{0x7d, 0xaf},                   
{0x80, 0x01},                   
{0x81, 0x7f},                   
{0x82, 0x13},                   
{0x83, 0x24},                   
{0x84, 0x7d},                   
{0x85, 0x81},                   
{0x86, 0x7d},                   
{0x87, 0x81},                   
{0x92, 0x48},                   
{0x93, 0x54},                   
{0x94, 0x7d},                   
{0x95, 0x81},                   
{0x96, 0x7d},                   
{0x97, 0x81},                   
{0xa0, 0x02},                   
{0xa1, 0x7b},                   
{0xa2, 0x02},                   
{0xa3, 0x7b},                   
{0xa4, 0x7b},                   
{0xa5, 0x02},                   
{0xa6, 0x7b},                   
{0xa7, 0x02},                   
{0xa8, 0x85},                   
{0xa9, 0x8c},                   
{0xaa, 0x85},                   
{0xab, 0x8c},                   
{0xac, 0x10},                   
{0xad, 0x16},                   
{0xae, 0x10},                   
{0xaf, 0x16},                   
{0xb0, 0x99},                   
{0xb1, 0xa3},                   
{0xb2, 0xa4},                   
{0xb3, 0xae},                   
{0xb4, 0x9b},                   
{0xb5, 0xa2},                   
{0xb6, 0xa6},                   
{0xb7, 0xac},                   
{0xb8, 0x9b},                   
{0xb9, 0x9f},                   
{0xba, 0xa6},                   
{0xbb, 0xaa},                   
{0xbc, 0x9b},                   
{0xbd, 0x9f},                   
{0xbe, 0xa6},                   
{0xbf, 0xaa},                   
{0xc4, 0x2c},                   
{0xc5, 0x43},                   
{0xc6, 0x63},                   
{0xc7, 0x79},                   
{0xc8, 0x2d},                   
{0xc9, 0x42},                   
{0xca, 0x2d},                   
{0xcb, 0x42},                   
{0xcc, 0x64},                   
{0xcd, 0x78},                   
{0xce, 0x64},                   
{0xcf, 0x78},                   
{0xd0, 0x0a},                   
{0xd1, 0x09},                   
{0xd4, 0x0e}, //DCDC_TIME_TH_ON 
{0xd5, 0x0e}, //DCDC_TIME_TH_OFF
{0xd6, 0x78}, //DCDC_AG_TH_ON   
{0xd7, 0x70}, //DCDC_AG_TH_OFF  
{0xe0, 0xc4},                   
{0xe1, 0xc4},                   
{0xe2, 0xc4},                   
{0xe3, 0xc4},                   
{0xe4, 0x00},                   
{0xe8, 0x80},                   
{0xe9, 0x40},                   
{0xea, 0x7f},                   
{0xf0, 0x01},                   
{0xf1, 0x01},                   
{0xf2, 0x01},                   
{0xf3, 0x01},                   
{0xf4, 0x01},                   
///00//// PAGE 3 ///////          
{0x03, 0x03},                   
{0x10, 0x10},                   
///00//// PAGE 10 START ///////   
{0x03, 0x10},                   
{0x10, 0x02}, // CrYCbY // For D//02
{0x12, 0x30},                   
{0x13, 0x0a},//add jacky open co
{0x20, 0x00},                   
{0x30, 0x00},                   
{0x31, 0x00},                   
{0x32, 0x00},                   
{0x33, 0x00},                   
{0x34, 0x30},                   
{0x35, 0x00},                   
{0x36, 0x00},                   
{0x38, 0x00},                   
{0x3e, 0x58},                   
{0x3f, 0x00},                   
{0x40, 0x80}, // YOFS           
{0x41, 0x10}, // DYOFS  //00        
{0x48, 0x88},                   
{0x60, 0x67},//6b AG ratio  jack
{0x61, 0x75}, //7e //8e //88 //7
{0x62, 0x80}, //7e //8e //88 //8
{0x63, 0x50}, //Double_AG 50->30
{0x64, 0x41},                   
{0x66, 0x42},                   
{0x67, 0x20},                   
{0x6a, 0x80}, //8a              
{0x6b, 0x84}, //74              
{0x6c, 0x80}, //7e //7a         
{0x6d, 0x80}, //8e              
///00//// PAGE 11 START ///////   
{0x03, 0x11},                   
{0x10, 0x7f},                   
{0x11, 0x40},                   
{0x12, 0x0a}, // Blue Max-Filter
{0x13, 0xbb},                   
{0x26, 0x31}, // Double_AG 31->2
{0x27, 0x34}, // Double_AG 34->2
{0x28, 0x0f},                   
{0x29, 0x10},                   
{0x2b, 0x30},                   
{0x2c, 0x32},                   
//O00ut2 D-LPF th                 
{0x30, 0x70},                   
{0x31, 0x10},                   
{0x32, 0x58},                   
{0x33, 0x09},                   
{0x34, 0x06},                   
{0x35, 0x03},                   
//O00ut1 D-LPF th                 
{0x36, 0x70},                   
{0x37, 0x18},                   
{0x38, 0x58},                   
{0x39, 0x09},                   
{0x3a, 0x06},                   
{0x3b, 0x03},                   
//I00ndoor D-LPF th               
{0x3c, 0x80},                   
{0x3d, 0x18},                   
{0x3e, 0xa0}, //80              
{0x3f, 0x0c},                   
{0x40, 0x09},                   
{0x41, 0x06},                   
{0x42, 0x80},                   
{0x43, 0x18},                   
{0x44, 0xa0}, //80              
{0x45, 0x12},                   
{0x46, 0x10},                   
{0x47, 0x10},                   
{0x48, 0x90},                   
{0x49, 0x40},                   
{0x4a, 0x80},                   
{0x4b, 0x13},                   
{0x4c, 0x10},                   
{0x4d, 0x11},                   
{0x4e, 0x80},                   
{0x4f, 0x30},                   
{0x50, 0x80},                   
{0x51, 0x13},                   
{0x52, 0x10},                   
{0x53, 0x13},                   
{0x54, 0x11},                   
{0x55, 0x17},                   
{0x56, 0x20},                   
{0x57, 0x01},                   
{0x58, 0x00},                   
{0x59, 0x00},                   
{0x5a, 0x1f}, //18              
{0x5b, 0x00},                   
{0x5c, 0x00},                   
{0x60, 0x3f},                   
{0x62, 0x60},                   
{0x70, 0x06},                   
///00//// PAGE 12 START ///////   
{0x03, 0x12},                   
{0x20, 0x0f},                   
{0x21, 0x0f},                   
{0x25, 0x00}, //0x30            
{0x28, 0x00},                   
{0x29, 0x00},                   
{0x2a, 0x00},                   
{0x30, 0x50},                   
{0x31, 0x18},                   
{0x32, 0x32},                   
{0x33, 0x40},                   
{0x34, 0x50},                   
{0x35, 0x70},                   
{0x36, 0xa0},                   
//O00ut2 th                       
{0x40, 0xa0},                   
{0x41, 0x40},                   
{0x42, 0xa0},                   
{0x43, 0x90},                   
{0x44, 0x90},                   
{0x45, 0x80},                   
//O00ut1 th                       
{0x46, 0xb0},                   
{0x47, 0x55},                   
{0x48, 0xa0},                   
{0x49, 0x90},                   
{0x4a, 0x90},                   
{0x4b, 0x80},                   
//I00ndoor th                     
{0x4c, 0xb0},                   
{0x4d, 0x40},                   
{0x4e, 0x90},                   
{0x4f, 0x90},                   
{0x50, 0xa0},                   
{0x51, 0x80},                   
//D00ark1 th                      
{0x52, 0x00}, //b0              
{0x53, 0x50}, //60              
{0x54, 0xd4}, //c0              
{0x55, 0xc0}, //c0              
{0x56, 0x70}, //b0              
{0x57, 0xec}, //70              
//D00ark2 th                      
{0x58, 0x60}, //90              
{0x59, 0x40}, //                
{0x5a, 0xd0},                   
{0x5b, 0xd0},                   
{0x5c, 0xc0},                   
{0x5d, 0x9b}, //70              
//D00ark3 th                      
{0x5e, 0x70},//88               
{0x5f, 0x40},                   
{0x60, 0xe0},                   
{0x61, 0xe0},                   
{0x62, 0xe0},                   
{0x63, 0xb4},//80               
{0x70, 0x15},                   
{0x71, 0x01}, //Don't Touch regi
{0x72, 0x18},                   
{0x73, 0x01}, //Don't Touch regi
{0x74, 0x25},                   
{0x75, 0x15},                   
{0x80, 0x20},                   
{0x81, 0x40},                   
{0x82, 0x65},                   
{0x85, 0x1a},                   
{0x88, 0x00},                   
{0x89, 0x00},                   
{0x90, 0x5d}, //For capture     
//D00ont Touch register           
{0xD0, 0x0c},                   
{0xD1, 0x80},                   
{0xD2, 0x67},                   
{0xD3, 0x00},                   
{0xD4, 0x00},                   
{0xD5, 0x02},                   
{0xD6, 0xff},                   
{0xD7, 0x18},                   
//E00nd                           
{0x3b, 0x06},                   
{0x3c, 0x06},                   
{0xc5, 0x00},//55->48           
{0xc6, 0x00},//48->40           
///00//// PAGE 13 START ///////   
{0x03, 0x13},                   
//E00dge                          
{0x10, 0xcb},                   
{0x11, 0x7b},                   
{0x12, 0x07},                   
{0x14, 0x00},                   
{0x20, 0x15},                   
{0x21, 0x13},                   
{0x22, 0x33},                   
{0x23, 0x05},                   
{0x24, 0x09},                   
{0x26, 0x18},                   
{0x27, 0x30},                   
{0x29, 0x12},                   
{0x2a, 0x50},                   
//L00ow clip th                   
{0x2b, 0x01}, //Out2 02         
{0x2c, 0x01}, //Out1 02         
{0x25, 0x06},                   
{0x2d, 0x0c},                   
{0x2e, 0x12},                   
{0x2f, 0x12},                   
//O00ut2 Edge                     
{0x50, 0x18},                   
{0x51, 0x1c},                   
{0x52, 0x1b},                   
{0x53, 0x15},                   
{0x54, 0x18},                   
{0x55, 0x15},                   
//O00ut1 Edge                     
{0x56, 0x18},                   
{0x57, 0x1c},                   
{0x58, 0x1b},                   
{0x59, 0x15},                   
{0x5a, 0x18},                   
{0x5b, 0x15},                   
//I00ndoor Edge                   
{0x5c, 0x0b},                   
{0x5d, 0x0c},                   
{0x5e, 0x0a},                   
{0x5f, 0x08},                   
{0x60, 0x09},                   
{0x61, 0x08},                   
//D00ark1 Edge                    
{0x62, 0x08},                   
{0x63, 0x08},                   
{0x64, 0x08},                   
{0x65, 0x06},                   
{0x66, 0x06},                   
{0x67, 0x06},                   
//D00ark2 Edge                    
{0x68, 0x07},                   
{0x69, 0x07},                   
{0x6a, 0x07},                   
{0x6b, 0x05},                   
{0x6c, 0x05},                   
{0x6d, 0x05},                   
//D00ark3 Edge                    
{0x6e, 0x07},                   
{0x6f, 0x07},                   
{0x70, 0x07},                   
{0x71, 0x05},                   
{0x72, 0x05},                   
{0x73, 0x05},                   
{0x80, 0xfd},                   
{0x81, 0x1f},                   
{0x82, 0x05},                   
{0x83, 0x31},                   
{0x90, 0x05},                   
{0x91, 0x05},                   
{0x92, 0x33},                   
{0x93, 0x30},                   
{0x94, 0x03},                   
{0x95, 0x14},                   
{0x97, 0x20},                   
{0x99, 0x20},                   
{0xa0, 0x01},                   
{0xa1, 0x02},                   
{0xa2, 0x01},                   
{0xa3, 0x02},                   
{0xa4, 0x05},                   
{0xa5, 0x05},                   
{0xa6, 0x07},                   
{0xa7, 0x08},                   
{0xa8, 0x07},                   
{0xa9, 0x08},                   
{0xaa, 0x07},                   
{0xab, 0x08},                   
{0xb0, 0x22},                   
{0xb1, 0x2a},                   
{0xb2, 0x28},                   
{0xb3, 0x22},                   
{0xb4, 0x2a},                   
{0xb5, 0x28},                   
{0xb6, 0x22},                   
{0xb7, 0x2a},                   
{0xb8, 0x28},                   
{0xb9, 0x22},                   
{0xba, 0x2a},                   
{0xbb, 0x28},                   
{0xbc, 0x25},                   
{0xbd, 0x2a},                   
{0xbe, 0x27},                   
{0xbf, 0x25},                   
{0xc0, 0x2a},                   
{0xc1, 0x27},                   
{0xc2, 0x1e},                   
{0xc3, 0x24},                   
{0xc4, 0x20},                   
{0xc5, 0x1e},                   
{0xc6, 0x24},                   
{0xc7, 0x20},                   
{0xc8, 0x18},                   
{0xc9, 0x20},                   
{0xca, 0x1e},                   
{0xcb, 0x18},                   
{0xcc, 0x20},                   
{0xcd, 0x1e},                   
{0xce, 0x18},                   
{0xcf, 0x20},                   
{0xd0, 0x1e},                   
{0xd1, 0x18},                   
{0xd2, 0x20},                   
{0xd3, 0x1e},                   
///00//// PAGE 14 START ///////   
{0x03, 0x14},                   
{0x10, 0x11},                   
{0x14, 0x80}, // GX             
{0x15, 0x80}, // GY             
{0x16, 0x80}, // RX             
{0x17, 0x80}, // RY             
{0x18, 0x80}, // BX             
{0x19, 0x80}, // BY             
{0x20, 0x80}, //X 60 //a0       
{0x21, 0x80}, //Y               
{0x22, 0x80},                   
{0x23, 0x80},                   
{0x24, 0x80},                   
{0x30, 0xc8},                   
{0x31, 0x2b},                   
{0x32, 0x00},                   
{0x33, 0x00},                   
{0x34, 0x90},                   
{0x40, 0x5C}, //0x48//65        
{0x50, 0x4d}, //0x34            
{0x60, 0x46}, //0x29            
{0x70, 0x4d}, //0x34            
///00//// PAGE 15 START ///////   
{0x03, 0x15},                   
{0x10, 0x0f},                   
//R00step H 16                    
//R00step L 14                    
                                  
                                  
{0x14, 0x42},	//CMCOFSGH        
{0x15, 0x32},	//CMCOFSGM        
{0x16, 0x24},	//CMCOFSGL        
{0x17, 0x2f},	//CMC SIGN        
//C00MC                           
{0x30, 0x80},                   
{0x31, 0x4d},                   
{0x32, 0x0d},                   
{0x33, 0x0c},                   
{0x34, 0x4d},                   
{0x35, 0x01},                   
{0x36, 0x00},                   
{0x37, 0x43},                   
{0x38, 0x83},                   
//C00MC OFS                       
{0x40, 0x92},                   
{0x41, 0x1b},                   
{0x42, 0x89},                   
{0x43, 0x81},                   
{0x44, 0x00},                   
{0x45, 0x01},                   
{0x46, 0x89},                   
{0x47, 0x9e},                   
{0x48, 0x28},                   
//C00MC POFS                      
{0x50, 0x02},                   
{0x51, 0x82},                   
{0x52, 0x00},                   
{0x53, 0x07},                   
{0x54, 0x11},                   
{0x55, 0x98},                   
{0x56, 0x00},                   
{0x57, 0x0b},                   
{0x58, 0x8b},                   
                                  
                                  
{0x80, 0x03},                   
{0x85, 0x40},                   
{0x87, 0x02},                   
{0x88, 0x00},                   
{0x89, 0x00},                   
{0x8a, 0x00},                   
///00//// PAGE 16 START ///////   
{0x03, 0x16},                   
{0x03, 0x16},                   
{0x10, 0x31},//GMA_CTL          
{0x18, 0x5e},//AG_ON            
{0x19, 0x5d},//AG_OFF           
{0x1A, 0x0e},//TIME_ON          
{0x1B, 0x01},//TIME_OFF         
{0x1C, 0xdc},//OUT_ON           
{0x1D, 0xfe},//OUT_OFF          
//G00MA                           
{0x30, 0x00},                   
{0x31, 0x08},//06                   
{0x32, 0x18}, //12                  
{0x33, 0x33}, //29                  
{0x34, 0x4d},                   
{0x35, 0x6c},                   
{0x36, 0x81},                   
{0x37, 0x94},                   
{0x38, 0xa4},                   
{0x39, 0xb3},                   
{0x3a, 0xc0},                   
{0x3b, 0xcb},                   
{0x3c, 0xd5},                   
{0x3d, 0xde},                   
{0x3e, 0xe6},                   
{0x3f, 0xee},                   
{0x40, 0xf5},                   
{0x41, 0xfc},                   
{0x42, 0xff},                   
//R00GMA                          
{0x50, 0x00},                   
{0x51, 0x09},                   
{0x52, 0x1f},                   
{0x53, 0x37},                   
{0x54, 0x5b},                   
{0x55, 0x76},                   
{0x56, 0x8d},                   
{0x57, 0xa1},                   
{0x58, 0xb2},                   
{0x59, 0xbe},                   
{0x5a, 0xc9},                   
{0x5b, 0xd2},                   
{0x5c, 0xdb},                   
{0x5d, 0xe3},                   
{0x5e, 0xeb},                   
{0x5f, 0xf0},                   
{0x60, 0xf5},                   
{0x61, 0xf7},                   
{0x62, 0xf8},                   
//B00GMA                          
{0x70, 0x05}, //00                  
{0x71, 0x0a},//08                   
{0x72, 0x18},  //12                 
{0x73, 0x33},  //2a                 
{0x74, 0x53},                   
{0x75, 0x6c},                   
{0x76, 0x81},                   
{0x77, 0x94},                   
{0x78, 0xa4},                   
{0x79, 0xb3},                   
{0x7a, 0xc0},                   
{0x7b, 0xcb},                   
{0x7c, 0xd5},                   
{0x7d, 0xde},                   
{0x7e, 0xe6},                   
{0x7f, 0xee},                   
{0x80, 0xf4},                   
{0x81, 0xfa},                   
{0x82, 0xff},                   
///00//// PAGE 17 START ///////   
{0x03, 0x17},                   
{0x10, 0xf7},                   
///00//// PAGE 20 START ///////   
{0x03, 0x20},                   
{0x11, 0x1c},                   
{0x18, 0x30},                   
{0x1a, 0x08},                   
{0x20, 0x01}, //05_lowtemp Y Mea
{0x21, 0x30},                   
{0x22, 0x10},                   
{0x23, 0x00},                   
{0x24, 0x00}, //Uniform Scene Of
{0x28, 0xe7},                   
{0x29, 0x0d}, //20100305 ad->0d 
{0x2a, 0xff},                   
{0x2b, 0x34}, //f4->Adaptive on 
{0x2c, 0xc2},                   
{0x2d, 0xcf},  //fe->AE Speed op
{0x2e, 0x33},                   
{0x30, 0x78}, //f8              
{0x32, 0x03},                   
{0x33, 0x2e},                   
{0x34, 0x30},                   
{0x35, 0xd4},                   
{0x36, 0xfe},                   
{0x37, 0x32},                   
{0x38, 0x04},                   
{0x39, 0x22}, //AE_escapeC10    
{0x3a, 0xde}, //AE_escapeC11    
{0x3b, 0x22}, //AE_escapeC1     
{0x3c, 0xde}, //AE_escapeC2     
{0x50, 0x45},                   
{0x51, 0x88},                   
{0x56, 0x03},                   
{0x57, 0xf7},                   
{0x58, 0x14},                   
{0x59, 0x88},                   
{0x5a, 0x04},                   
{0x60, 0xff},                   
{0x61, 0xff},                   
{0x62, 0xea},                   
{0x63, 0xab},                   
{0x64, 0xea},                   
{0x65, 0xab},                   
{0x66, 0xea},//eb               
{0x67, 0x2b},//eb               
{0x68, 0xe8},//eb               
{0x69, 0x2b},//eb               
{0x6a, 0xea},                   
{0x6b, 0xab},                   
{0x6c, 0xea},                   
{0x6d, 0xab},                   
{0x6e, 0xff},                   
{0x6f, 0xff},                   
{0x70, 0x76}, //6e              
{0x71, 0x89}, //00 //-4         
{0x76, 0x43},                   
{0x77, 0xe2}, //04 //f2         
{0x78, 0x23}, //Yth1            
{0x79, 0x46}, //Yth2 //46       
{0x7a, 0x23}, //23              
{0x7b, 0x22}, //22              
{0x7d, 0x23},                   
{0x83, 0x01}, //EXP Normal 33.33
{0x84, 0x6e},                   
{0x85, 0x36},                   
{0x86, 0x01}, //EXPMin 5859.38 f
{0x87, 0xf4},                   
{0x88, 0x05}, //EXP Max 10.00 fp
{0x89, 0xb8},                   
{0x8a, 0xd8},                   
{0x8B, 0x7a}, //EXP100          
{0x8C, 0x12},                   
{0x8D, 0x65}, //EXP120          
{0x8E, 0x90},                   
{0x9c, 0x17}, //EXP Limit 488.28
{0x9d, 0x70},                   
{0x9e, 0x01}, //EXP Unit        
{0x9f, 0xf4},                   
{0xb0, 0x18},                   
{0xb1, 0x14}, //ADC 400->560    
{0xb2, 0x80},//a0               
{0xb3, 0x18},                   
{0xb4, 0x1a},                   
{0xb5, 0x44},                   
{0xb6, 0x2f},                   
{0xb7, 0x28},                   
{0xb8, 0x25},                   
{0xb9, 0x22},                   
{0xba, 0x21},                   
{0xbb, 0x20},                   
{0xbc, 0x1f},                   
{0xbd, 0x1f},                   
{0xc0, 0x14},                   
{0xc1, 0x1f},                   
{0xc2, 0x1f},                   
{0xc3, 0x18}, //2b              
{0xc4, 0x10}, //08              
{0xc8, 0x80},                   
{0xc9, 0x40},                   
///00//// PAGE 22 START ///////   
{0x03, 0x22},                   
{0x10, 0xfd},                   
{0x11, 0x2e},                   
{0x19, 0x01},                   
{0x24, 0x01},                   
{0x30, 0x80},                   
{0x31, 0x80},                   
{0x38, 0x11},                   
{0x39, 0x34},                   
{0x40, 0xf7},                   
{0x41, 0x55},                   
{0x42, 0x33},                   
{0x46, 0x00},                   
{0x80, 0x30},//40               
{0x81, 0x20},                   
{0x82, 0x3e},                   
{0x83, 0x53},                   
{0x84, 0x16},//1E               
{0x85, 0x5A},//52               
{0x86, 0x25},                   
{0x87, 0x49},                   
{0x88, 0x35},                   
{0x89, 0x47},                   
{0x8a, 0x28},                   
{0x8b, 0x41},                   
{0x8c, 0x39},                   
{0x8d, 0x3f},                   
{0x8e, 0x28},                   
{0x8f, 0x53},                   
{0x90, 0x52},                   
{0x91, 0x50},                   
{0x92, 0x4c},                   
{0x93, 0x43},                   
{0x94, 0x37},                   
{0x95, 0x2a},                   
{0x96, 0x24},                   
{0x97, 0x20},                   
{0x98, 0x1e},                   
{0x99, 0x1f},                   
{0x9a, 0x20},                   
{0x9b, 0x88},                   
{0x9c, 0x88},                   
{0x9d, 0x48},                   
{0x9e, 0x38},                   
{0x9f, 0x30},                   
{0xa0, 0x60},                   
{0xa1, 0x34},                   
{0xa2, 0x6f},                   
{0xa3, 0xff},                   
{0xa4, 0x14},                   
{0xa5, 0x2c},                   
{0xa6, 0xcf},                   
{0xad, 0x40},                   
{0xae, 0x4a},                   
{0xaf, 0x28},                   
{0xb0, 0x26},                   
{0xb1, 0x00},                   
{0xb8, 0xa0},                   
{0xb9, 0x00},                   
// 00PAGE 20                      
{0x03, 0x20}, //page 20         
{0x10, 0x9c}, //ae off          
// 00PAGE 22                      
{0x03, 0x22}, //page 22         
{0x10, 0xe9}, //awb off         
// 00PAGE 0                       
{0x03, 0x00},
/*
{0x10, 0x00},
{0x03, 0x18},
{0x12, 0x20},
{0x10, 0x07},
{0x11, 0x00},
{0x20, 0x05},
{0x21, 0x20},
{0x22, 0x03},
{0x23, 0xd8},
{0x24, 0x00},
{0x25, 0x90},
{0x26, 0x00},
{0x27, 0x6c},
{0x28, 0x04},
{0x29, 0x90},
{0x2a, 0x03},
{0x2b, 0x6c},
{0x2c, 0x09},
{0x2d, 0xc1},
{0x2e, 0x09},
{0x2f, 0xc1},
{0x30, 0x56},
*/
{0x03, 0x00},                   
{0x0e, 0x03}, //PLL On          
{0x0e, 0x73}, //PLLx2           
{0x03, 0x00}, // Dummy 750us    
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00},                   
{0x03, 0x00}, // Page 0         
{0x01, 0x50}, // Sleep Off      

{0xff, 0xff},
};

//load HI253 parameters
void HI253_init_regs(struct hi253_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
   	int i=0;
	unsigned char buf[2],tmp[2];
	
    	buf[0] = i2c_get_byte_add8(client, 0x04);
	
	printk("#####[%s(%d)]:buf[0] = 0x%x\n", __FUNCTION__, __LINE__, buf[0]);
	
	while(1) {
		if (HI253_script[i].val==0xff&&HI253_script[i].addr==0xff) {
			printk("HI253_write_regs success in initial HI253.\n");
			break;
		}
		buf[0]=HI253_script[i].addr;
		buf[1]=HI253_script[i].val;
		if((i2c_put_byte_add8(client,buf, 2)) < 0)
		{
			printk("fail in initial HI253. \n");
			return;
		} else {
			tmp[0] = i2c_get_byte_add8(client, buf[0]);
			if(buf[1]!=tmp[0])
			printk("failed: write buf[0x%x] = 0x%x, result = 0x%x\n",buf[0],buf[1],tmp[0]);
		}
		i++;
	}

	buf[0]=0x03;
	buf[1]=0x00;
	i2c_put_byte_add8(client,buf, 2);
	i=i2c_get_byte_add8(client, 0x04);
	printk("HI253 0x04=0x%x\n", i);
	return;
}
/*************************************************************************
* FUNCTION
*    HI253_set_param_wb
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
void HI253_set_param_wb(struct hi253_device *dev,enum  camera_wb_flip_e para)//white balance
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	#if 1	

	switch (para)
	{
	
		
		case CAM_WB_AUTO://auto
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						/*{0x03 , 0x22},
						{0x11 , 0x2e},
						{0x80 , 0x30},
						{0x81 , 0x28},
						{0x82 , 0x30},
						{0x83 , 0x55},
						{0x84 , 0x16},
						{0x85 , 0x53},
						{0x86 , 0x53},
						{0xff , 0xff},*/
						{0x03 , 0x22},
						{0x11 , 0x2e},
						{0x80 , 0x30},
						{0x81 , 0x20},
						{0x82 , 0x3e},
						{0x83 , 0x53},
						{0x84 , 0x16},
						{0x85 , 0x5A},
						{0x86 , 0x25},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;

		case CAM_WB_CLOUD: //cloud
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x22},
						{0x11 , 0x28},
						{0x80 , 0x71},
						//{0x81 , 0x28},
						{0x82 , 0x2b},
						{0x83 , 0x72},
						{0x84 , 0x70},
						{0x85 , 0x2b},
						{0x86 , 0x28},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;

		case CAM_WB_DAYLIGHT: //
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x22},
						{0x11 , 0x28},
						{0x80 , 0x59},
						//{0x81 , 0x28},
						{0x82 , 0x29},
						{0x83 , 0x60},
						{0x84 , 0x50},
						{0x85 , 0x2f},
						{0x86 , 0x23},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;

		case CAM_WB_INCANDESCENCE: 
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x22},
						{0x11 , 0x28},
						{0x80 , 0x29},
						//{0x81 , 0x28},
						{0x82 , 0x54},
						{0x83 , 0x2e},
						{0x84 , 0x23},
						{0x85 , 0x58},
						{0x86 , 0x4f},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;
			
		case CAM_WB_TUNGSTEN: 
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x22},
						//{0x11 , 0x28},
						{0x80 , 0x24},
						{0x81 , 0x20},
						{0x82 , 0x58},
						{0x83 , 0x27},
						{0x84 , 0x22},
						{0x85 , 0x58},
						{0x86 , 0x52},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;

      	case CAM_WB_FLUORESCENT:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x22},
						{0x11 , 0x28},
						{0x80 , 0x41},
						//{0x81 , 0x20},
						{0x82 , 0x42},
						{0x83 , 0x44},
						{0x84 , 0x34},
						{0x85 , 0x46},
						{0x86 , 0x3a},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;

		case CAM_WB_MANUAL:
		    	// TODO
			break;
		default:
			break;
	}
	
#endif
} /* HI253_set_param_wb */
/*************************************************************************
* FUNCTION
*    HI253_set_param_exposure
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
void HI253_set_param_exposure(struct hi253_device *dev,enum camera_exposure_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
   #if 1

    switch (para)
	{
		

		case EXPOSURE_N4_STEP:	
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x40 , 0xc0},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;
			
		case EXPOSURE_N3_STEP:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x40 , 0xa0},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;
			
		case EXPOSURE_N2_STEP:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x40 , 0x90},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;
			
		case EXPOSURE_N1_STEP:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x40 , 0x88},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;
			
		case EXPOSURE_0_STEP:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x40 , 0x80},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;
			
		case EXPOSURE_P1_STEP:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x40 , 0x08},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;
			
		case EXPOSURE_P2_STEP:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x40 , 0x10},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;
			
		case EXPOSURE_P3_STEP:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x40 , 0x20},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;
			
		case EXPOSURE_P4_STEP:	
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x40 , 0x30},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;
			
		default:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x40 , 0x40},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;


		
	}
#endif

} /* HI253_set_param_exposure */
/*************************************************************************
* FUNCTION
*    HI253_set_param_effect
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
void HI253_set_param_effect(struct hi253_device *dev,enum camera_effect_flip_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x11 , 0x03},
						{0x12 , 0x30},
						{0x13 , 0x02},
						{0x44 , 0x80},
						{0x45 , 0x80},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;		

		case CAM_EFFECT_ENC_GRAYSCALE:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x11 , 0x03},
						{0x12 , 0x03},
						{0x13 , 0x02},
						{0x40 , 0x00},
						{0x44 , 0x80},
						{0x45 , 0x80},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;

		case CAM_EFFECT_ENC_SEPIA:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x11 , 0x03},
						{0x12 , 0x33},
						{0x13 , 0x02},
						{0x44 , 0x70},
						{0x45 , 0x98},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;		
		case CAM_EFFECT_ENC_SEPIAGREEN:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x11 , 0x03},
						{0x12 , 0x03},
						{0x40 , 0x00},
						{0x13 , 0x02},
						{0x44 , 0x30},
						{0x45 , 0x50},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;		
		case CAM_EFFECT_ENC_SEPIABLUE:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x11 , 0x03},
						{0x12 , 0x03},
						{0x40 , 0x00},
						{0x13 , 0x02},
						{0x44 , 0xb0},
						{0x45 , 0x40},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;		
		case CAM_EFFECT_ENC_COLORINV:
			{
				struct aml_camera_i2c_fig_s regs[]=
					{
						{0x03 , 0x10},
						{0x11 , 0x03},
						{0x12 , 0x08},
						{0x13 , 0x02},
						{0x14 , 0x00},
						{0xff , 0xff},
					};
				int i=0;
				unsigned char buf[2];
				while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
				{
					buf[0]=regs[i].addr;
					buf[1]=regs[i].val;
					i2c_put_byte_add8(client,buf, 2);
					i++;
				}
			}
			break;		

		default:
			break;
	}
	if(para!=CAM_EFFECT_ENC_COLORINV){
		HI253_set_param_exposure(dev,hi253_qctrl[1].default_value);

	}

} /* HI253_set_param_effect */

/*************************************************************************
* FUNCTION
*    HI253_NightMode
*
* DESCRIPTION
*    This function night mode of HI253.
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
void HI253_set_night_mode(struct hi253_device *dev,enum  camera_night_mode_flip_e enable)
{

}    /* HI253_NightMode */

static struct aml_camera_i2c_fig_s HI253_50HZ_scrip[] = {
	{0x03 , 0x20},
	{0x10 , 0x9c},
	{0xff , 0xff},
};

static struct aml_camera_i2c_fig_s HI253_60HZ_scrip[] = {
	{0x03 , 0x20},
	{0x10 , 0x8c},
	{0xff , 0xff},
};

void HI253_set_param_banding(struct hi253_device *dev,enum  camera_banding_flip_e banding)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	int i;
	struct aml_camera_i2c_fig_s* regs;
	switch(banding){
	case CAM_BANDING_50HZ:
		{
		regs = HI253_50HZ_scrip;		
		i=0;
		while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
		{
			buf[0]=regs[i].addr;
			buf[1]=regs[i].val;
			i2c_put_byte_add8(client,buf, 2);
			i++;
		}
		}
		break;		
	case CAM_BANDING_60HZ:
		{
		regs = HI253_60HZ_scrip;
		i=0;
		while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
		{
			buf[0]=regs[i].addr;
			buf[1]=regs[i].val;
			i2c_put_byte_add8(client,buf, 2);
			i++;
		}
		}
		break;	
	default:
		break;
			
	}

}

static int set_flip(struct hi253_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	unsigned char buf[2];
	buf[0] = 0x03;
	buf[1] = 0x00;
	if((i2c_put_byte_add8(client, buf, 2)) < 0) {
            printk("fail in setting sensor orientation\n");
            return -1;
        }
	temp = i2c_get_byte_add8(client, 0x11);
	temp &= 0xfc;
	temp |= dev->cam_info.m_flip << 0;
	temp |= dev->cam_info.v_flip << 1;
	buf[0] = 0x11;
	buf[1] = temp;
	if((i2c_put_byte_add8(client,buf, 2)) < 0) {
            printk("fail in setting sensor orientation\n");
            return -1;
        }
        return 0;
}

static struct aml_camera_i2c_fig_s res_800x600_scripts[] = {
	{0x03 , 0x00},
	{0x10 , 0x11},					
	{0xff , 0xff},
};

static struct aml_camera_i2c_fig_s res_1600x1200_scripts[] = {
	{0x03 , 0x00},
	{0x10 , 0x00},				
	{0xff , 0xff},
};

void HI253_set_resolution(struct hi253_device *dev,int height,int width)
{	
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	struct aml_camera_i2c_fig_s* regs;
	int i;
	unsigned char buf[2];	 
	if(height&&width&&(height<=1200)&&(width<=1600))
	{		
		if((height<=600)&&(width<=800))
		{
			#if 1
			printk(KERN_INFO " set camera  HI253_set_resolution1111=w=%d,h=%d. \n ",width,height);
			regs = res_800x600_scripts;
					
			i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
			#endif
			mdelay(100);
			hi253_h_active=800;
			hi253_v_active=600;
		} else {
			//1600x1200
			regs = res_1600x1200_scripts;
			i=0;
			while (regs[i].addr!= 0xff && regs[i].val!= 0xff)
			{
				buf[0]=regs[i].addr;
				buf[1]=regs[i].val;
				i2c_put_byte_add8(client,buf, 2);
				i++;
			}
			mdelay(100);
			hi253_h_active=1600;
			hi253_v_active=1200;
			printk(KERN_INFO " set camera  HI253_set_resolution0000=w=%d,h=%d. \n ",width,height);
			/*1024x768
			i2c_put_byte(client,0x0102 ,  0x01);
			i2c_put_byte(client,0x010a ,  0x00);
			i2c_put_byte(client,0x010b ,  0x03);
			i2c_put_byte(client,0x0105 ,  0x00);
			i2c_put_byte(client,0x0106 ,  0xf0);
			i2c_put_byte(client,0x0107 ,  0x00);
			i2c_put_byte(client,0x0108 ,  0x0e);
			i2c_put_byte(client,0x0109 ,  0x01);
			i2c_put_byte(client,0x010c ,  0x01);
			i2c_put_byte(client,0x010d ,  0x28);
			i2c_put_byte(client,0x010e ,  0x00);
			i2c_put_byte(client,0x010f ,  0xe0);
			i2c_put_byte(client,0x0110 , 0x04);
			i2c_put_byte(client,0x0111 ,  0x00);
			i2c_put_byte(client,0x0112 , 0x03);
			i2c_put_byte(client,0x0113 ,  0x00);
			*/
			/*1280x720
			i2c_put_byte(client,0x0102 ,  0x01);
			i2c_put_byte(client,0x010a ,  0x00);
			i2c_put_byte(client,0x010b ,  0x03);
			i2c_put_byte(client,0x0105 ,  0x00);
			i2c_put_byte(client,0x0106 ,  0xf0);
			i2c_put_byte(client,0x0107 ,  0x00);
			i2c_put_byte(client,0x0108 ,  0x0e);
			i2c_put_byte(client,0x0109 ,  0x01);
			i2c_put_byte(client,0x010c ,  0x00);
			i2c_put_byte(client,0x010d ,  0xa8);
			i2c_put_byte(client,0x010e ,  0x00);
			i2c_put_byte(client,0x010f ,  0xf8);
			i2c_put_byte(client,0x0110 , 0x05);
			i2c_put_byte(client,0x0111 ,  0x00);
			i2c_put_byte(client,0x0112 , 0x02);
			i2c_put_byte(client,0x0113 ,  0xd0);
			*/

		}
	}
	printk(KERN_INFO " set camera  HI253_set_resolution=w=%d,h=%d. \n ",width,height);
	set_flip(dev);
}    /* HI253_set_resolution */

unsigned char v4l_2_hi253(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int hi253_setting(struct hi253_device *dev,int PROP_ID,int value ) 
{
	int ret=0;
	
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_hi253(value));
		//ret=i2c_put_byte(client,0x0201,v4l_2_hi253(value));
		break;
	case V4L2_CID_CONTRAST:
		//ret=i2c_put_byte(client,0x0200, value);
		break;	
	case V4L2_CID_SATURATION:
		//ret=i2c_put_byte(client,0x0202, value);
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		if(hi253_qctrl[0].default_value!=value){
			hi253_qctrl[0].default_value=value;
			HI253_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
        	}
		break;
	case V4L2_CID_EXPOSURE:
		if(hi253_qctrl[1].default_value!=value){
			hi253_qctrl[1].default_value=value;
			HI253_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
        	}
		break;
	case V4L2_CID_COLORFX:
		if(hi253_qctrl[2].default_value!=value){
			hi253_qctrl[2].default_value=value;
			HI253_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
        	}
		break;
	case V4L2_CID_WHITENESS:
		 if(hi253_qctrl[3].default_value!=value){
			hi253_qctrl[3].default_value=value;
			HI253_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
        	}
		break;
	case V4L2_CID_BLUE_BALANCE:
		if(hi253_qctrl[4].default_value!=value){
			hi253_qctrl[4].default_value=value;
			HI253_set_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(hi253_qctrl[5].default_value!=value){
			hi253_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(hi253_qctrl[7].default_value!=value){
			hi253_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(hi253_qctrl[8].default_value!=value){
			hi253_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;
	
}

static void power_down_hi253(struct hi253_device *dev)
{

}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void hi253_fillbuff(struct hi253_fh *fh, struct hi253_buffer *buf)
{
	struct hi253_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);	
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = hi253_qctrl[5].default_value;
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom =  hi253_qctrl[7].default_value;
	para.angle = hi253_qctrl[8].default_value;
	para.vaddr = (unsigned)vbuf;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void hi253_thread_tick(struct hi253_fh *fh)
{
	struct hi253_buffer *buf;
	struct hi253_device *dev = fh->dev;
	struct hi253_dmaqueue *dma_q = &dev->vidq;

	unsigned long flags = 0;

	dprintk(dev, 1, "Thread tick\n");

	spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&dma_q->active)) {
		dprintk(dev, 1, "No active queue to serve\n");
		goto unlock;
	}

	buf = list_entry(dma_q->active.next,
			 struct hi253_buffer, vb.queue);
    dprintk(dev, 1, "%s\n", __func__);
	dprintk(dev, 1, "list entry get buf is %x\n",(unsigned)buf);

	/* Nobody is waiting on this buffer, return */
	if (!waitqueue_active(&buf->vb.done))
		goto unlock;

	list_del(&buf->vb.queue);

	do_gettimeofday(&buf->vb.ts);

	/* Fill buffer */
	spin_unlock_irqrestore(&dev->slock, flags);
	hi253_fillbuff(fh, buf);
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

static void hi253_sleep(struct hi253_fh *fh)
{
	struct hi253_device *dev = fh->dev;
	struct hi253_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */

	hi253_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int hi253_thread(void *data)
{
	struct hi253_fh  *fh = data;
	struct hi253_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		hi253_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int hi253_start_thread(struct hi253_fh *fh)
{
	struct hi253_device *dev = fh->dev;
	struct hi253_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(hi253_thread, fh, "hi253");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void hi253_stop_thread(struct hi253_dmaqueue  *dma_q)
{
	struct hi253_device *dev = container_of(dma_q, struct hi253_device, vidq);

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
	struct hi253_fh  *fh = vq->priv_data;
	struct hi253_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct hi253_buffer *buf)
{
	struct hi253_fh  *fh = vq->priv_data;
	struct hi253_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

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
	struct hi253_fh     *fh  = vq->priv_data;
	struct hi253_device    *dev = fh->dev;
	struct hi253_buffer *buf = container_of(vb, struct hi253_buffer, vb);
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
	struct hi253_buffer    *buf  = container_of(vb, struct hi253_buffer, vb);
	struct hi253_fh        *fh   = vq->priv_data;
	struct hi253_device       *dev  = fh->dev;
	struct hi253_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct hi253_buffer   *buf  = container_of(vb, struct hi253_buffer, vb);
	struct hi253_fh       *fh   = vq->priv_data;
	struct hi253_device      *dev  = (struct hi253_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops hi253_video_qops = {
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
	struct hi253_fh  *fh  = priv;
	struct hi253_device *dev = fh->dev;

	strcpy(cap->driver, "hi253");
	strcpy(cap->card, "hi253");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = HI253_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct hi253_fmt *fmt;

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
	struct hi253_fh *fh = priv;

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
	struct hi253_fh  *fh  = priv;
	struct hi253_device *dev = fh->dev;
	struct hi253_fmt *fmt;
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
	struct hi253_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct hi253_device *dev = fh->dev;

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
		HI253_set_resolution(dev,fh->height,fh->width);
		}
	else if(vidio_set_fmt_ticks==1){
		HI253_set_resolution(dev,fh->height,fh->width);
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
	struct hi253_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct hi253_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct hi253_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct hi253_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct hi253_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct hi253_fh  *fh = priv;
	vdin_parm_t para;
	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( vdin_parm_t));
	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = 15;//236;
	para.h_active = hi253_h_active;//800
	para.v_active = hi253_v_active;//600
	para.hsync_phase = 1;
	para.vsync_phase  = 1;
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
	para.skip_count =  2;//skip num
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
            vops->start_tvin_service(0,&para);
	    fh->stream_on        = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct hi253_fh  *fh = priv;

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
	struct hi253_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(hi253_prev_resolution))
			return -EINVAL;
		frmsize = &hi253_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(hi253_pic_resolution))
			return -EINVAL;
		frmsize = &hi253_pic_resolution[fsize->index];
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
	struct hi253_fh *fh = priv;
	struct hi253_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct hi253_fh *fh = priv;
	struct hi253_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(hi253_qctrl); i++)
		if (qc->id && qc->id == hi253_qctrl[i].id) {
			memcpy(qc, &(hi253_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct hi253_fh *fh = priv;
	struct hi253_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(hi253_qctrl); i++)
		if (ctrl->id == hi253_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct hi253_fh *fh = priv;
	struct hi253_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(hi253_qctrl); i++)
		if (ctrl->id == hi253_qctrl[i].id) {
			if (ctrl->value < hi253_qctrl[i].minimum ||
			    ctrl->value > hi253_qctrl[i].maximum ||
			    hi253_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int hi253_open(struct file *file)
{
	struct hi253_device *dev = video_drvdata(file);
	struct hi253_fh *fh = NULL;
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
	
	HI253_init_regs(dev);
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
	/* Resets frame counters */
	dev->jiffies = jiffies;
			
//    TVIN_SIG_FMT_CAMERA_640X480P_30Hz,
//    TVIN_SIG_FMT_CAMERA_800X600P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1024X768P_30Hz, // 190
//    TVIN_SIG_FMT_CAMERA_1920X1080P_30Hz,
//    TVIN_SIG_FMT_CAMERA_1280X720P_30Hz,

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &hi253_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct hi253_buffer), fh, NULL);

	hi253_start_thread(fh);

	return 0;
}

static ssize_t
hi253_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct hi253_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
hi253_poll(struct file *file, struct poll_table_struct *wait)
{
	struct hi253_fh        *fh = file->private_data;
	struct hi253_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int hi253_close(struct file *file)
{
	struct hi253_fh         *fh = file->private_data;
	struct hi253_device *dev       = fh->dev;
	struct hi253_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);

	hi253_stop_thread(vidq);
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
	hi253_h_active=800;
	hi253_v_active=600;
	hi253_qctrl[0].default_value=0;
	hi253_qctrl[1].default_value=4;
	hi253_qctrl[2].default_value=0;
	hi253_qctrl[3].default_value=0;
	hi253_qctrl[4].default_value=0;
	
	hi253_qctrl[5].default_value=0;
	hi253_qctrl[7].default_value=100;
	hi253_qctrl[8].default_value=0;

	power_down_hi253(dev);
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

static int hi253_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct hi253_fh  *fh = file->private_data;
	struct hi253_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations hi253_fops = {
	.owner		= THIS_MODULE,
	.open           = hi253_open,
	.release        = hi253_close,
	.read           = hi253_read,
	.poll		= hi253_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = hi253_mmap,
};

static const struct v4l2_ioctl_ops hi253_ioctl_ops = {
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
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
};

static struct video_device hi253_template = {
	.name		= "hi253_v4l",
	.fops           = &hi253_fops,
	.ioctl_ops 	= &hi253_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int hi253_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_HI253, 0);
}

static const struct v4l2_subdev_core_ops hi253_core_ops = {
	.g_chip_ident = hi253_g_chip_ident,
};

static const struct v4l2_subdev_ops hi253_ops = {
	.core = &hi253_core_ops,
};

static int hi253_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	aml_cam_info_t* plat_dat;
	int err;
	struct hi253_device *t;
	struct v4l2_subdev *sd;
	vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &hi253_ops);
	mutex_init(&t->mutex);

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &hi253_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);

	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "hi253");
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
	
	t->cam_info.version = HI253_DRIVER_VERSION;
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

static int hi253_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hi253_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id hi253_id[] = {
	{ "hi253", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hi253_id);

static struct i2c_driver hi253_i2c_driver = {
	.driver = {
		.name = "hi253",
	},
	.probe = hi253_probe,
	.remove = hi253_remove,
	.id_table = hi253_id,
};

module_i2c_driver(hi253_i2c_driver);

