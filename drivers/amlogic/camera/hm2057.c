/*
 *hm2057 - This code emulates a real video device with v4l2 api
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
#include <mach/mod_gate.h>
#include<linux/amlogic/saradc.h>

#define hm2057_CAMERA_MODULE_NAME "hm2057"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)           /* 0.5 seconds */

#define hm2057_CAMERA_MAJOR_VERSION 0
#define hm2057_CAMERA_MINOR_VERSION 7
#define hm2057_CAMERA_RELEASE 0
#define hm2057_CAMERA_VERSION \
KERNEL_VERSION(hm2057_CAMERA_MAJOR_VERSION, hm2057_CAMERA_MINOR_VERSION, hm2057_CAMERA_RELEASE)

MODULE_DESCRIPTION("hm2057 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define HM2057_DRIVER_VERSION "HM2057-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");
static unsigned int camera_state=-1;
static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");
#define HM2057_VEDIO_encode_mode 0
enum {
	KAL_TRUE = 1,
};

static int HM2057_h_active=800;
static int HM2057_v_active=600;
static struct v4l2_fract hm2057_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

static struct vdin_v4l2_ops_s *vops;

/* supported controls */
static struct v4l2_queryctrl hm2057_qctrl[] = {
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

#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/
struct v4l2_querymenu hm2057_qmenu_wbmode[] = {
		{
			.id 		= V4L2_CID_DO_WHITE_BALANCE,
			.index		= CAM_WB_AUTO,
			.name		= "auto",
			.reserved	= 0,
		},{
			.id 		= V4L2_CID_DO_WHITE_BALANCE,
			.index		= CAM_WB_CLOUD,
			.name		= "cloudy-daylight",
			.reserved	= 0,
		},{
			.id 		= V4L2_CID_DO_WHITE_BALANCE,
			.index		= CAM_WB_INCANDESCENCE,
			.name		= "incandescent",
			.reserved	= 0,
		},{
			.id 		= V4L2_CID_DO_WHITE_BALANCE,
			.index		= CAM_WB_DAYLIGHT,
			.name		= "daylight",
			.reserved	= 0,
		},{
			.id 		= V4L2_CID_DO_WHITE_BALANCE,
			.index		= CAM_WB_FLUORESCENT,
			.name		= "fluorescent", 
			.reserved	= 0,
		},{
			.id 		= V4L2_CID_DO_WHITE_BALANCE,
			.index		= CAM_WB_FLUORESCENT,
			.name		= "warm-fluorescent", 
			.reserved	= 0,
		},
	};

struct v4l2_querymenu hm2057_qmenu_anti_banding_mode[] = {
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
    struct v4l2_querymenu* hm2057_qmenu;
}hm2057_qmenu_set_t;

hm2057_qmenu_set_t hm2057_qmenu_set[] = {
    {
        .id         	= V4L2_CID_DO_WHITE_BALANCE,
        .num            = ARRAY_SIZE(hm2057_qmenu_wbmode),
        .hm2057_qmenu   = hm2057_qmenu_wbmode,
    },{
        .id         	= V4L2_CID_POWER_LINE_FREQUENCY,
        .num            = ARRAY_SIZE(hm2057_qmenu_anti_banding_mode),
        .hm2057_qmenu   = hm2057_qmenu_anti_banding_mode,
    },
};

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(hm2057_qmenu_set); i++)
	if (a->id && a->id == hm2057_qmenu_set[i].id) {
	    for(j = 0; j < hm2057_qmenu_set[i].num; j++)
		if (a->index == hm2057_qmenu_set[i].hm2057_qmenu[j].index) {
			memcpy(a, &( hm2057_qmenu_set[i].hm2057_qmenu[j]),
				sizeof(*a));
			return (0);
		}
	}

	return -EINVAL;
}

struct hm2057_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct hm2057_fmt formats[] = {
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

static struct hm2057_fmt *get_format(struct v4l2_format *f)
{
	struct hm2057_fmt *fmt;
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
struct hm2057_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct hm2057_fmt        *fmt;
};

struct hm2057_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(hm2057_devicelist);

struct hm2057_device {
	struct list_head			hm2057_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct hm2057_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t  cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(hm2057_qctrl)];
};

static inline struct hm2057_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct hm2057_device, sd);
}

struct hm2057_fh {
	struct hm2057_device            *dev;

	/* video capture */
	struct hm2057_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};

/*static inline struct hm2057_fh *to_fh(struct hm2057_device *dev)
{
	return container_of(dev, struct hm2057_fh, dev);
}*/

static struct v4l2_frmsize_discrete hm2057_prev_resolution[]= //should include 352x288 and 640x480, those two size are used for recording
{
	{320,240},
	{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete hm2057_pic_resolution[]=
{
	{800, 600},
	{1600, 1200},
	{2048, 1536},
	{2592, 1944},
};

static int get_total_pic_size(resolution_size_t max_size)
{
	int ret = 2;
	if (max_size >= SIZE_2592X1944)
		ret = 4;
	else if (max_size >= SIZE_2048X1536)
		ret = 3;    
		
	//printk("total_pic_size is %d\n", ret);
	
	return ret;
}

/* ------------------------------------------------------------------
	reg spec of hm2057
   ------------------------------------------------------------------*/

struct aml_camera_i2c_fig_s hm2057_script[] = {
		{0x0022,0x00},  // Reset     
		{0x0025,0x00},  // CKCFG 80 from system clock, 00
		{0x0026,0x87},  // PLL1CFG should be 07 when syst
		{0x0004,0x10},  //                               
		{0x0006,0x00},  // Flip/Mirror                   
		{0x000D,0x11},  //; 20120220 to fix morie        
		{0x000E,0x11},  // Binning ON                    
		{0x000F,0x10},  // IMGCFG                        
		{0x0011,0x02},  //                               
		{0x0012,0x1C},  // 2012.02.08                    
		{0x0013,0x01},  //                               
		{0x0015,0x02},  //                               
		{0x0016,0x80},  //                               
		{0x0018,0x00},  //                               
		{0x001D,0x40},  //                               
		{0x0020,0x00},  //                               
		{0x0027,0x38},  // YUV output                    
		{0x0040,0x20},  // 20120224 for BLC stable       
		{0x0053,0x0A},  //                               
		{0x0044,0x06},  // enable BLC_phase2             
		{0x0046,0xD8},  // enable BLC_phase1, disable BLC
		{0x004A,0x0A},  // disable BLC_phase2 hot pixel f
		{0x004B,0x72},  //                               
		{0x0075,0x01},  // in OMUX data swap for debug us
		{0x002A,0x1F},  // Output=48MHz                  
		{0x0070,0x5F},  //                               
		{0x0071,0xFF},  //                               
		{0x0072,0x55},  //                               
		{0x0073,0x50},  //      ;                        
		{0x0080,0xC8},  // 2012.02.08                    
		{0x0082,0xA2},  //                               
		{0x0083,0xF0},  //                               
		{0x0085,0x12},  // Enable Thin-Oxide Case (Kwango
		{0x0086,0x02},  // K.Kim 2011.12.09              
		{0x0087,0x80},  // K.Kim 2011.12.09              
		{0x0088,0x6C},  // andrew  vcmi=0.3v  04/10  CTIA
		{0x0089,0x2E},  //                               
		{0x008A,0x7D},  // 20120224 for BLC stable       
		{0x008D,0x20},  //                               
		{0x0090,0x00},  // 1.5x(Change Gain Table )      
		{0x0091,0x10},  // 3x  (3x CTIA)                 
		{0x0092,0x11},  // 6x  (3x CTIA + 2x PGA)        
		{0x0093,0x12},  // 12x (3x CTIA + 4x PGA)        
		{0x0094,0x16},  // 24x (3x CTIA + 8x PGA)        
		{0x0095,0x08},  // 1.5x  20120217 for color shift
		{0x0096,0x00},  // 3x    20120217 for color shift
		{0x0097,0x10},  // 6x    20120217 for color shift
		{0x0098,0x11},  // 12x   20120217 for color shift
		{0x0099,0x12},  // 24x   20120217 for color shift
		{0x009A,0x06},  // 24x                           
		{0x009B,0x34},  //                               
		{0x00A0,0x00},  //                               
		{0x00A1,0x04},  // 2012.02.06(for Ver.C)         
		{0x011F,0xF7},  // simple bpc P31 & P33[4] P40 P4 20130128 F7
		{0x0120,0x36},  // 36:50Hz, 37:60Hz, BV_Win_Weigh
		{0x0121,0x83},  // NSatScale_En=0, NSatScale=0   
		{0x0122,0x7B},  //                               
		{0x0123,0xC2},  //                               
		{0x0124,0xDE},  //                               
		{0x0125,0xFF},  //  20130128                             
		{0x0126,0x70},  //                               
		{0x0128,0x1F},  //                               
		{0x0132,0x10},  //                               
		{0x0131,0xAD},  // simle bpc enable[4]    20130128        
		{0x0140,0x14},  //                               
		{0x0141,0x0A},  //                               
		{0x0142,0x14},  //                               
		{0x0143,0x0A},  //                               
		{0x0144,0x04},  // Sort bpc hot pixel ratio      
		{0x0145,0x00},  //                               
		{0x0146,0x20},  //                               
		{0x0147,0x0A},  //                               
		{0x0148,0x10},  //                               
		{0x0149,0x0C},  //                               
		{0x014A,0x80},  //                               
		{0x014B,0x80},  //                               
		{0x014C,0x2E},  //                               
		{0x014D,0x2E},  //                               
		{0x014E,0x05},  //                               
		{0x014F,0x05},  //                               
		{0x0150,0x0D},  //                               
		{0x0155,0x00},  //                               
		{0x0156,0x10},  //                               
		{0x0157,0x0A},  //                               
		{0x0158,0x0A},  //                               
		{0x0159,0x0A},  //                               
		{0x015A,0x05},  //                               
		{0x015B,0x05},  //                               
		{0x015C,0x05},  //                               
		{0x015D,0x05},  //                               
		{0x015E,0x08},  //                               
		{0x015F,0xFF},  //                               
		{0x0160,0x50},  // OTP BPC 2line & 4line enable  
		{0x0161,0x20},  //                               
		{0x0162,0x14},  //                               
		{0x0163,0x0A},  //                               
		{0x0164,0x10},  // OTP 4line Strength            
		{0x0165,0x0A},  //                               
		{0x0166,0x0A},  //                               
		{0x018C,0x24},  //                               
		{0x018D,0x04},  //; Cluster correction enable sin
		{0x018E,0x00},  //; Enable Thermal sensor control
		{0x018F,0x11},  // Cluster Pulse enable T1[0] T2[
		{0x0190,0x80},  // A11 BPC Strength[7:3], cluster
		{0x0191,0x47},  // A11[0],A7[1],Sort[3],A13 AVG[6
		{0x0192,0x48},  // A13 Strength[4:0],hot pixel de
		{0x0193,0x64},  //                               
		{0x0194,0x32},  //                               
		{0x0195,0xc8},  //                               
		{0x0196,0x96},  //                               
		{0x0197,0x64},  //                               
		{0x0198,0x32},  //                               
		{0x0199,0x14},  // A13 hot pixel th              
		{0x019A,0x20},  // A13 edge detect th            
		{0x019B,0x14},  //                               
		{0x01B0,0x55},  // G1G2 Balance                  
		{0x01B1,0x0C},  //                               
		{0x01B2,0x0A},  //                               
		{0x01B3,0x10},  //                               
		{0x01B4,0x0E},  //                               
		{0x01BA,0x10},  // BD                            
		{0x01BB,0x04},  //                               
		{0x01D8,0x40},  //                               
		{0x01DE,0x60},  //                               
		{0x01E4,0x10},  //                               
		{0x01E5,0x10},  //                               
		{0x01F2,0x0C},  //                               
		{0x01F3,0x14},  //                               
		{0x01F8,0x04},  //                               
		{0x01F9,0x0C},  //                               
		{0x01FE,0x02},  //                               
		{0x01FF,0x04},  //                               
		{0x0220,0x00},  // LSC                           
		{0x0221,0xB0},  //                               
		{0x0222,0x00},  //                               
		{0x0223,0x80},  //                               
		{0x0224,0x8E},  //                               
		{0x0225,0x00},  //                               
		{0x0226,0x88},  //                               
		{0x022A,0x88},  //                               
		{0x022B,0x00},  //                               
		{0x022C,0x8C},  //                               
		{0x022D,0x13},  //                               
		{0x022E,0x0B},  //                               
		{0x022F,0x13},  //                               
		{0x0230,0x0B},  //                               
		{0x0233,0x13},  //                               
		{0x0234,0x0B},  //                               
		{0x0235,0x28},  //                               
		{0x0236,0x03},  //                               
		{0x0237,0x28},  //                               
		{0x0238,0x03},  //                               
		{0x023B,0x28},  //                               
		{0x023C,0x03},  //                               
		{0x023D,0x5C},  //                               
		{0x023E,0x02},  //                               
		{0x023F,0x5C},  //                               
		{0x0240,0x02},  //                               
		{0x0243,0x5C},  //                               
		{0x0244,0x02},  //                               
		{0x0251,0x0E},  //                               
		{0x0252,0x00},  //                               
		{0x0280,0x0A},  //  	; Gamma                    
		{0x0282,0x14},  //                               
		{0x0284,0x2A},  //                               
		{0x0286,0x50},  //                               
		{0x0288,0x60},  //                               
		{0x028A,0x6D},  //                               
		{0x028C,0x79},  //                               
		{0x028E,0x82},  //                               
		{0x0290,0x8A},  //                               
		{0x0292,0x91},  //                               
		{0x0294,0x9C},  //                               
		{0x0296,0xA7},  //                               
		{0x0298,0xBA},  //                               
		{0x029A,0xCD},  //                               
		{0x029C,0xE0},  //                               
		{0x029E,0x2D},  //                               
		{0x02A0,0x06},  // Gamma by Alpha                
		{0x02E0,0x04},  // CCM by Alpha  
		#if 0
		{0x02C0,0x8F},  // CCM                           
		{0x02C1,0x01},  //                               
		{0x02C2,0x8F},  //                               
		{0x02C3,0x07},  //                               
		{0x02C4,0xE3},  //                               
		{0x02C5,0x07},  //                               
		{0x02C6,0xC1},  //                               
		{0x02C7,0x07},  //                               
		{0x02C8,0x70},  //                               
		{0x02C9,0x01},  //                               
		{0x02CA,0xD0},  //                               
		{0x02CB,0x07},  //                               
		{0x02CC,0xF7},  //                               
		{0x02CD,0x07},  //                               
		{0x02CE,0x5A},  //                               
		{0x02CF,0x07},  //                               
		{0x02D0,0xB0},  //                               
		{0x02D1,0x01},  //                               
		{0x0302,0x00},  //                               
		{0x0303,0x00},  //                               
		{0x0304,0x00},  //                               
		{0x02F0,0x80},  //                               
		{0x02F1,0x07},  //                               
		{0x02F2,0x8E},  //                               
		{0x02F3,0x00},  //                               
		{0x02F4,0xF2},  //                               
		{0x02F5,0x07},  //                               
		{0x02F6,0xCC},  //                               
		{0x02F7,0x07},  //                               
		{0x02F8,0x16},  //                               
		{0x02F9,0x00},  //                               
		{0x02FA,0x1E},  //                               
		{0x02FB,0x00},  //                               
		{0x02FC,0x9D},  //                               
		{0x02FD,0x07},  //                               
		{0x02FE,0xA6},  //                               
		{0x02FF,0x07},  //                               
		{0x0300,0xBD},  //                               
		{0x0301,0x00},  //                               
		{0x0305,0x00},  //                               
		{0x0306,0x00},  //                               
		{0x0307,0x00},  //  
		#else
		{0x02C0,0xb1},//b1
        {0x02C1,0x01},
        {0x02C2,0x7D},
        {0x02C3,0x07},
        {0x02C4,0xD2},
        {0x02C5,0x07},
        {0x02C6,0xC4},
        {0x02C7,0x07},
        {0x02C8,0x79},
        {0x02C9,0x01},
        {0x02CA,0xC4},
        {0x02CB,0x07},
        {0x02CC,0xF7},
        {0x02CD,0x07},
        {0x02CE,0x3B},
        {0x02CF,0x07},
        {0x02D0,0xCF},
        {0x02D1,0x01},
        {0x0302,0x00},
        {0x0303,0x00},
        {0x0304,0x00},
        
        {0x02F0,0x5e},//5e
        {0x02F1,0x07},
        {0x02F2,0xA0},
        {0x02F3,0x00},
        {0x02F4,0x02},
        {0x02F5,0x00},
        {0x02F6,0xC4},
        {0x02F7,0x07},
        {0x02F8,0x11},
        {0x02F9,0x00},
        {0x02FA,0x2A},
        {0x02FB,0x00},
        {0x02FC,0xA1},
        {0x02FD,0x07},
        {0x02FE,0xB8},
        {0x02FF,0x07},
        {0x0300,0xA7},
        {0x0301,0x00},
        {0x0305,0x00},
        {0x0306,0x00},
        {0x0307,0x00},
		#endif
		{0x032D,0x00},  //                               
		{0x032E,0x01},  //                               
		{0x032F,0x00},  //                               
		{0x0330,0x01},  //                               
		{0x0331,0x00},  //                               
		{0x0332,0x01},  //                               
		{0x0333,0x82},  // AWB channel offset            
		{0x0334,0x00},  //                               
		{0x0335,0x84},  //                               
		{0x0336,0x00},  //; LED AWB gain                 
		{0x0337,0x01},  //                               
		{0x0338,0x00},  //                               
		{0x0339,0x01},  //                               
		{0x033A,0x00},  //                               
		{0x033B,0x01},  //                               
		{0x033E,0x04},  //                               
		{0x033F,0x86},  //                               
		{0x0340,0x30},  // AWB                           
		{0x0341,0x44},  //                               
		{0x0342,0x4A},  //                               
		{0x0343,0x42},  // CT1                           
		{0x0344,0x74},  //	;                            
		{0x0345,0x4F},  // CT2                           
		{0x0346,0x67},  //	;                            
		{0x0347,0x5C},  // CT3                           
		{0x0348,0x59},  //                               
		{0x0349,0x67},  // CT4                           
		{0x034A,0x4D},  //                               
		{0x034B,0x6E},  // CT5                           
		{0x034C,0x44},  //                               
		{0x0350,0x80},  //                               
		{0x0351,0x80},  //                               
		{0x0352,0x18},  //                               
		{0x0353,0x18},  //                               
		{0x0354,0x6E},  //                               
		{0x0355,0x4A},  //                               
		{0x0356,0x73},  //                               
		{0x0357,0xC0},  //                               
		{0x0358,0x06},  //                               
		{0x035A,0x06},  //                               
		{0x035B,0xA0},  //                               
		{0x035C,0x73},  //                               
		{0x035D,0x50},  //                               
		{0x035E,0xC0},  //                               
		{0x035F,0xA0},  //                               
		{0x0360,0x02},  //                               
		{0x0361,0x18},  //                               
		{0x0362,0x80},  //                               
		{0x0363,0x6C},  //                               
		{0x0364,0x00},  //                               
		{0x0365,0xF0},  //                               
		{0x0366,0x20},  //                               
		{0x0367,0x0C},  //                               
		{0x0369,0x00},  //                               
		{0x036A,0x10},  //                               
		{0x036B,0x10},  //                               
		{0x036E,0x20},  //                               
		{0x036F,0x00},  //                               
		{0x0370,0x10},  //                               
		{0x0371,0x18},  //                               
		{0x0372,0x0C},  //                               
		{0x0373,0x38},  //                               
		{0x0374,0x3A},  //                               
		{0x0375,0x13},  //                               
		{0x0376,0x22},  //                               
		{0x0380,0xFF},  //                               
		{0x0381,0x4A},  //                               
		{0x0382,0x56},  //     20130923--36-->56                          
		{0x038A,0x42},  //     20130923--40-->42                          
		{0x038B,0x08},  //                               
		{0x038C,0xC1},  //                               
		{0x038E,0x4c},  //20130923-- 40->4c                           
		{0x038F,0x04},  //                               
		{0x0390,0x8c},  //                               
		{0x0391,0x05},  //                               
		{0x0393,0x80},  //                               
		{0x0395,0x21},  // AEAWB skip count              
		{0x0398,0x02},  // AE Frame Control              
		{0x0399,0x84},  //                               
		{0x039A,0x03},  //                               
		{0x039B,0x25},  //                               
		{0x039C,0x03},  //                               
		{0x039D,0xC6},  //                               
		{0x039E,0x05},  //                               
		{0x039F,0x08},  //                               
		{0x03A0,0x06},  //                               
		{0x03A1,0x4A},  //                               
		{0x03A2,0x07},  //                               
		{0x03A3,0x8C},  //                               
		{0x03A4,0x0A},  //                               
		{0x03A5,0x10},  //                               
		{0x03A6,0x0C},  //                               
		{0x03A7,0x0E},  //                               
		{0x03A8,0x10},  //                               
		{0x03A9,0x18},  //                               
		{0x03AA,0x20},  //                               
		{0x03AB,0x28},  //                               
		{0x03AC,0x1E},  //                               
		{0x03AD,0x1A},  //                               
		{0x03AE,0x13},  //                               
		{0x03AF,0x0C},  //                               
		{0x03B0,0x0B},  //                               
		{0x03B1,0x09},  //                               
		{0x03B3,0x10},  // AE window array               
		{0x03B4,0x00},  //                               
		{0x03B5,0x10},  //                               
		{0x03B6,0x00},  //                               
		{0x03B7,0xEA},  //                               
		{0x03B8,0x00},  //                               
		{0x03B9,0x3A},  //                               
		{0x03BA,0x01},  //                               
		{0x03BB,0x9F},  // enable 5x5 window             
		{0x03BC,0xCF},  //                               
		{0x03BD,0xE7},  //                               
		{0x03BE,0xF3},  //                               
		{0x03BF,0x01},  //                               
		{0x03D0,0xF8},  // AE NSMode YTh                 
		{0x03E0,0x04},  // weight                        
		{0x03E1,0x01},  //                               
		{0x03E2,0x04},  //                               
		{0x03E4,0x10},  //                               
		{0x03E5,0x12},  //                               
		{0x03E6,0x00},  //                               
		{0x03E8,0x21},  //                               
		{0x03E9,0x23},  //                               
		{0x03EA,0x01},  //                               
		{0x03EC,0x21},  //                               
		{0x03ED,0x23},  //                               
		{0x03EE,0x01},  //                               
		{0x03F0,0x20},  //                               
		{0x03F1,0x22},  //                               
		{0x03F2,0x00},  //                               
		{0x0420,0x84},  // Digital Gain offset           
		{0x0421,0x00},  //                               
		{0x0422,0x00},  //                               
		{0x0423,0x83},  //                               
		{0x0430,0x08},  // ABLC                          
		{0x0431,0x28},  //                               
		{0x0432,0x10},  //                               
		{0x0433,0x08},  //                               
		{0x0435,0x0C},  //                               
		{0x0450,0xFF},  //                               
		{0x0451,0xE8},  //                               
		{0x0452,0xC4},  //                               
		{0x0453,0x88},  //              02c0                 
		{0x0454,0x00},  //                               
		{0x0458,0x70},  //                               
		{0x0459,0x03},  //                               
		{0x045A,0x00},  //                               
		{0x045B,0x30},  //                               
		{0x045C,0x00},  //                               
		{0x045D,0x70},  //                               
		{0x0466,0x14},  //                               
		{0x047A,0x00},  // ELOFFNRB                      
		{0x047B,0x00},  // ELOFFNRY                      
		{0x0480,0x67},  //  64                             
		{0x0481,0x06},  //                               
		{0x0482,0x0C},  //                               
		{0x04B0,0x58},  // Contrast                      
		{0x04B6,0x30},  //                               
		{0x04B9,0x10},  //                               
		{0x04B3,0x10},  //                               
		{0x04B1,0x8E},  //                               
		{0x04B4,0x20},  //                               
		{0x0540,0x00},  //                               
		{0x0541,0x60},  // 60Hz Flicker  9D                
		{0x0542,0x00},  //                               
		{0x0543,0x73},  // 50Hz Flicker bc                 
		{0x0580,0x01},  // Blur str sigma                
		{0x0581,0x0F},  // Blur str sigma ALPHA          
		{0x0582,0x04},  // Blur str sigma OD             
		{0x0594,0x00},  // UV Gray TH                    
		{0x0595,0x04},  // UV Gray TH Alpha              
		{0x05A9,0x03},  //                               
		{0x05AA,0x40},  //                               
		{0x05AB,0x80},  //                               
		{0x05AC,0x0A},  //                               
		{0x05AD,0x10},  //                               
		{0x05AE,0x0C},  //                               
		{0x05AF,0x0C},  //                               
		{0x05B0,0x03},  //                               
		{0x05B1,0x03},  //                               
		{0x05B2,0x1C},  //                               
		{0x05B3,0x02},  //                               
		{0x05B4,0x00},  //                               
		{0x05B5,0x0C},  // BlurW                         
		{0x05B8,0x80},  //                               
		{0x05B9,0x32},  //                               
		{0x05BA,0x00},  //                               
		{0x05BB,0x80},  //                               
		{0x05BC,0x03},  //                               
		{0x05BD,0x00},  //                               
		{0x05BF,0x05},  //                               
		{0x05C0,0x10},  // BlurW LowLight                
		{0x05C3,0x00},  //                               
		{0x05C4,0x0C},  // BlurW Outdoor                 
		{0x05C5,0x20},  //                               
		{0x05C7,0x01},  //                               
		{0x05C8,0x14},  //                               
		{0x05C9,0x54},  //                               
		{0x05CA,0x14},  //                               
		{0x05CB,0xE0},  //                               
		{0x05CC,0x20},  //                               
		{0x05CD,0x00},  //                               
		{0x05CE,0x08},  //                               
		{0x05CF,0x60},  //                               
		{0x05D0,0x10},  //                               
		{0x05D1,0x05},  //                               
		{0x05D2,0x03},  //                               
		{0x05D4,0x00},  //                               
		{0x05D5,0x05},  //                               
		{0x05D6,0x05},  //                               
		{0x05D7,0x05},  //                               
		{0x05D8,0x08},  //                               
		{0x05DC,0x0C},  //                               
		{0x05D9,0x00},  //                               
		{0x05DB,0x00},  //                               
		{0x05DD,0x0F},  //                               
		{0x05DE,0x00},  //                               
		{0x05DF,0x0A},  //                               
		{0x05E0,0xA0},  // Scaler                        
		{0x05E1,0x00},  //                               
		{0x05E2,0xA0},  //    
		#if 1 
		{0x011F,0x80},
		{0x0125,0xDF},
		{0x0126,0x70},
		{0x0131,0xAD},


		
		{0x05E3,0x00},  //                               
		{0x05E4,0x05},  // Windowing                     
		{0x05E5,0x00},  //                               
		{0x05E6,0x24},  //                               
		{0x05E7,0x03},  //                               
		{0x05E8,0x07},  //                               
		{0x05E9,0x00},  //                               
		{0x05EA,0x5E},  //                               
		{0x05EB,0x02},  //  
		#endif
		#if 0                          
		{0x05E3,0x00},  //                               
		{0x05E4,0x04},  // Windowing                     
		{0x05E5,0x00},  //                               
		{0x05E6,0x8b},  //  83                             
		{0x05E7,0x02},  //                               
		{0x05E8,0x06},  //                               
		{0x05E9,0x00},  //                               
		{0x05EA,0xEb},  //e5                               
		{0x05EB,0x01},  //  
		#endif
		                                     
		{0x0660,0x04},  //                               
		{0x0661,0x16},  //                               
		{0x0662,0x04},  //                               
		{0x0663,0x28},  //                               
		{0x0664,0x04},  //                               
		{0x0665,0x18},  //                               
		{0x0666,0x04},  //                               
		{0x0667,0x21},  //                               
		{0x0668,0x04},  //                               
		{0x0669,0x0C},  //                               
		{0x066A,0x04},  //                               
		{0x066B,0x25},  //                               
		{0x066C,0x00},  //                               
		{0x066D,0x12},  //                               
		{0x066E,0x00},  //                               
		{0x066F,0x80},  //                               
		{0x0670,0x00},  //                               
		{0x0671,0x0A},  //                               
		{0x0672,0x04},  //                               
		{0x0673,0x1D},  //                               
		{0x0674,0x04},  //                               
		{0x0675,0x1D},  //                               
		{0x0676,0x00},  //                               
		{0x0677,0x7E},  //                               
		{0x0678,0x01},  //                               
		{0x0679,0x47},  //                               
		{0x067A,0x00},  //                               
		{0x067B,0x73},  //                               
		{0x067C,0x04},  //                               
		{0x067D,0x14},  //                               
		{0x067E,0x04},  //                               
		{0x067F,0x28},  //                               
		{0x0680,0x00},  //                               
		{0x0681,0x22},  //                               
		{0x0682,0x00},  //                               
		{0x0683,0xA5},  //                               
		{0x0684,0x00},  //                               
		{0x0685,0x1E},  //                               
		{0x0686,0x04},  //                               
		{0x0687,0x1D},  //                               
		{0x0688,0x04},  //                               
		{0x0689,0x19},  //                               
		{0x068A,0x04},  //                               
		{0x068B,0x21},  //                               
		{0x068C,0x04},  //                               
		{0x068D,0x0A},  //                               
		{0x068E,0x04},  //                               
		{0x068F,0x25},  //                               
		{0x0690,0x04},  //                               
		{0x0691,0x15},  //                               
		{0x0698,0x20},  //                               
		{0x0699,0x20},  //                               
		{0x069A,0x01},  //                               
		{0x069C,0x22},  //                               
		{0x069D,0x10},  //                               
		{0x069E,0x10},  //                               
		{0x069F,0x08},  //                               
		{0x0000,0x01},  //                               
		{0x0100,0x01},  //                               
		{0x0101,0x01},  //                               
		{0x0005,0x01},  // Turn on rolling shutter       
	
		{0xffff,0xff},
};

//load hm2057 parameters 
static void hm2057_init_regs(struct hm2057_device *dev)
{
		struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
			int i=0;
	
		while (1) {
			if (hm2057_script[i].val==0xff&&hm2057_script[i].addr==0xffff)
			{
				printk("hm2057_write_regs success in initial hm2057.\n");
				break;
			}
			if((i2c_put_byte(client,hm2057_script[i].addr, hm2057_script[i].val)) < 0)
			{
				printk("fail in initial hm2057. \n");
				return;
			}
			i++;
		}
}

static struct v4l2_frmivalenum hm2057_frmivalenum[] = {
	{
		.index 		= 0,
		.pixel_format	= V4L2_PIX_FMT_NV21,
		.width		= 352,
		.height		= 288,
		.type		= V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete	={
				.numerator	= 1,
				.denominator	= 15,
			}
		}
	},{
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
	},{
		.index 		= 1,
		.pixel_format	= V4L2_PIX_FMT_NV21,
		.width		= 2048,
		.height		= 1536,
		.type		= V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete	={
				.numerator	= 1,
				.denominator	= 5,
			}
		}
	},{
		.index 		= 1,
		.pixel_format	= V4L2_PIX_FMT_NV21,
		.width		= 2560,
		.height		= 2048,
		.type		= V4L2_FRMIVAL_TYPE_DISCRETE,
		{
			.discrete	={
				.numerator	= 1,
				.denominator	= 5,
			}
		}
	},
};
/*************************************************************************
* FUNCTION
*    hm2057_set_param_wb
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
void hm2057_set_param_wb(struct hm2057_device *dev,enum  camera_wb_flip_e para)//white balance
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    switch (para)
	{

		case CAM_WB_AUTO://auto
			i2c_put_byte(client,0x0380, 0xFF);   // select Auto WB
			i2c_put_byte(client,0x0101, 0xFF);  
			break;

		case CAM_WB_CLOUD: //cloud
			i2c_put_byte(client,0x0380, 0xFD);  	//Disable AWB
			i2c_put_byte(client,0x032D, 0x0A);
			i2c_put_byte(client,0x032E, 0x02); 	//Red
			i2c_put_byte(client,0x032F, 0xF3);
			i2c_put_byte(client,0x0330, 0x00);		//Green
			i2c_put_byte(client,0x0331, 0xBD);
			i2c_put_byte(client,0x0332, 0x00);		//Blue
			i2c_put_byte(client,0x0101, 0xFF);
			break;

		case CAM_WB_DAYLIGHT: //
        	i2c_put_byte(client,0x0380, 0xFD);  	//Disable AWB
        	i2c_put_byte(client,0x032D, 0x9E);
        	i2c_put_byte(client,0x032E, 0x01); 	//Red
        	i2c_put_byte(client,0x032F, 0x88);
        	i2c_put_byte(client,0x0330, 0x01);		//Green
        	i2c_put_byte(client,0x0331, 0x20);
        	i2c_put_byte(client,0x0332, 0x01);		//Blue
        	i2c_put_byte(client,0x0101, 0xFF);
			break;

		case CAM_WB_INCANDESCENCE:
    		i2c_put_byte(client,0x0380, 0xFD);  	//Disable AWB
    		i2c_put_byte(client,0x032D, 0x00);
    		i2c_put_byte(client,0x032E, 0x01); 	//Red
    		i2c_put_byte(client,0x032F, 0x14);
    		i2c_put_byte(client,0x0330, 0x01);		//Green
    		i2c_put_byte(client,0x0331, 0xD6);
    		i2c_put_byte(client,0x0332, 0x01);		//Blue
    		i2c_put_byte(client,0x0101, 0xFF); 
			break;

		case CAM_WB_TUNGSTEN:
    		i2c_put_byte(client,0x0380, 0xFD);  	//Disable AWB
    		i2c_put_byte(client,0x032D, 0xC4);
    		i2c_put_byte(client,0x032E, 0x00); 	//Red
    		i2c_put_byte(client,0x032F, 0xAE);
    		i2c_put_byte(client,0x0330, 0x00);		//Green
    		i2c_put_byte(client,0x0331, 0x05);
    		i2c_put_byte(client,0x0332, 0x01);		//Blue
    		i2c_put_byte(client,0x0101, 0xFF);
			break;

      	case CAM_WB_FLUORESCENT:
    		i2c_put_byte(client,0x0380, 0xFD);  	//Disable AWB
    		i2c_put_byte(client,0x032D, 0xEF);
    		i2c_put_byte(client,0x032E, 0x00); 	//Red
    		i2c_put_byte(client,0x032F, 0xAE);
    		i2c_put_byte(client,0x0330, 0x00);		//Green
    		i2c_put_byte(client,0x0331, 0x72);
    		i2c_put_byte(client,0x0332, 0x01);		//Blue
    		i2c_put_byte(client,0x0101, 0xFF);
			break;

		case CAM_WB_MANUAL:
		default:
			// TODO
			break;
	}

} /* hm2057_set_param_wb */
/*************************************************************************
* FUNCTION
*    hm2057_set_param_exposure
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
void hm2057_set_param_exposure(struct hm2057_device *dev,enum camera_exposure_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    switch (para)
	{
		case EXPOSURE_N4_STEP:
    		i2c_put_byte(client,0x04C0,0xC0);
    		i2c_put_byte(client,0x038E,0x28);	// 20-28-30
    		i2c_put_byte(client,0x0381,0x30);
    		i2c_put_byte(client,0x0382,0x20);
    		i2c_put_byte(client,0x0100,0xFF);
			break;

		case EXPOSURE_N3_STEP:
    		i2c_put_byte(client,0x04C0,0xB0);
    		i2c_put_byte(client,0x038E,0x30);	// 28-30-38
    		i2c_put_byte(client,0x0381,0x38);
    		i2c_put_byte(client,0x0382,0x28);
    		i2c_put_byte(client,0x0100,0xFF);
			break;

		case EXPOSURE_N2_STEP:
    		i2c_put_byte(client,0x04C0,0xA0);
    		i2c_put_byte(client,0x038E,0x38);  // 30-38-40
    		i2c_put_byte(client,0x0381,0x40);
    		i2c_put_byte(client,0x0382,0x30);
    		i2c_put_byte(client,0x0100,0xFF);
			break;

		case EXPOSURE_N1_STEP:
    		i2c_put_byte(client,0x04C0,0x90);
    		i2c_put_byte(client,0x038E,0x40);	// 38-40-48
    		i2c_put_byte(client,0x0381,0x48);
    		i2c_put_byte(client,0x0382,0x38);
    		i2c_put_byte(client,0x0100,0xFF); 
			break;

		case EXPOSURE_0_STEP:
    		i2c_put_byte(client,0x04C0,0x00);
    		i2c_put_byte(client,0x038E,0x4c);	// 40-48-50
    		i2c_put_byte(client,0x0381,0x56);
    		i2c_put_byte(client,0x0382,0x4c);
    		i2c_put_byte(client,0x0100,0xFF);
			break;

		case EXPOSURE_P1_STEP:
    		i2c_put_byte(client,0x04C0,0x10);	
    		i2c_put_byte(client,0x038E,0x50);	// 48-50-58
    		i2c_put_byte(client,0x0381,0x58);
    		i2c_put_byte(client,0x0382,0x48);
    		i2c_put_byte(client,0x0100,0xFF); 
			break;

		case EXPOSURE_P2_STEP:
    		i2c_put_byte(client,0x04C0,0x20);	
    		i2c_put_byte(client,0x038E,0x58);	// 50-58-60
    		i2c_put_byte(client,0x0381,0x60);
    		i2c_put_byte(client,0x0382,0x50);
    		i2c_put_byte(client,0x0100,0xFF); 
			break;

		case EXPOSURE_P3_STEP:
    		i2c_put_byte(client,0x04C0,0x30);	
    		i2c_put_byte(client,0x038E,0x60);	// 58-60-68
    		i2c_put_byte(client,0x0381,0x68);
    		i2c_put_byte(client,0x0382,0x58);
    		i2c_put_byte(client,0x0100,0xFF);
			break;

		case EXPOSURE_P4_STEP:
    		i2c_put_byte(client,0x04C0,0x40);
    		i2c_put_byte(client,0x038E,0x68);	// 60-68-70
    		i2c_put_byte(client,0x0381,0x70);
    		i2c_put_byte(client,0x0382,0x60);
    		i2c_put_byte(client,0x0100,0xFF);
			break;

		default:
    		i2c_put_byte(client,0x04C0,0x00);
    		i2c_put_byte(client,0x038E,0x48);	// 40-48-50
    		i2c_put_byte(client,0x0381,0x50);
    		i2c_put_byte(client,0x0382,0x40);
    		i2c_put_byte(client,0x0100,0xFF);
		break;
	}
	
	mdelay(20);

} /* hm2057_set_param_exposure */
/*************************************************************************
* FUNCTION
*    hm2057_set_param_effect
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
void hm2057_set_param_effect(struct hm2057_device *dev,enum camera_effect_flip_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL:
			i2c_put_byte(client,0x0005, 0x00);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);
			i2c_put_byte(client,0x0488, 0x10); //[0]:Image scense, [1]:Image Xor
			i2c_put_byte(client,0x0486, 0x00); //Hue, sin                       
			i2c_put_byte(client,0x0487, 0xFF); //Hue, cos
			i2c_put_byte(client,0x0120, 0x37);
			i2c_put_byte(client,0x0005, 0x01);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);
			break;

		case CAM_EFFECT_ENC_GRAYSCALE:
			i2c_put_byte(client,0x0005, 0x00);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);
			i2c_put_byte(client,0x0486, 0x80); //Hue, sin						 
			i2c_put_byte(client,0x0487, 0x80); //Hue, cos
			i2c_put_byte(client,0x0488, 0x11); //[0]:Image scense, [1]:Image Xor
			i2c_put_byte(client,0x0120, 0x27);
			i2c_put_byte(client,0x0005, 0x01);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);
			break;

		case CAM_EFFECT_ENC_SEPIA:
			i2c_put_byte(client,0x0005, 0x00);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);
			i2c_put_byte(client,0x0486, 0x40); //Hue, sin						 
			i2c_put_byte(client,0x0487, 0xA0); //Hue, cos
			i2c_put_byte(client,0x0488, 0x11); //[0]:Image scense, [1]:Image Xor
			i2c_put_byte(client,0x0120, 0x27);
			i2c_put_byte(client,0x0005, 0x01);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);
			break;

		case CAM_EFFECT_ENC_SEPIAGREEN:
			i2c_put_byte(client,0x0005, 0x00);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);
			i2c_put_byte(client,0x0486, 0x60); //Hue, sin						 
			i2c_put_byte(client,0x0487, 0x60); //Hue, cos
			i2c_put_byte(client,0x0488, 0x11); //[0]:Image scense, [1]:Image Xor
			i2c_put_byte(client,0x0120, 0x27);
			i2c_put_byte(client,0x0005, 0x01);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);
			break;

		case CAM_EFFECT_ENC_SEPIABLUE:
			i2c_put_byte(client,0x0005, 0x00);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);
			i2c_put_byte(client,0x0486, 0xB0); //Hue, sin						 
			i2c_put_byte(client,0x0487, 0x80); //Hue, cos
			i2c_put_byte(client,0x0488, 0x11); //[0]:Image scense, [1]:Image Xor
			i2c_put_byte(client,0x0120, 0x27);
			i2c_put_byte(client,0x0005, 0x01);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);
			break;

		case CAM_EFFECT_ENC_COLORINV:
			i2c_put_byte(client,0x0005, 0x00);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);
			i2c_put_byte(client,0x0488, 0x12); //[0]:Image scense, [1]:Image Xor
			i2c_put_byte(client,0x0486, 0x00); //Hue, sin						 
			i2c_put_byte(client,0x0487, 0xFF); //Hue, cos
			i2c_put_byte(client,0x0120, 0x37);
			i2c_put_byte(client,0x0005, 0x01);
			i2c_put_byte(client,0x0000, 0x01);
			i2c_put_byte(client,0x0100, 0xFF);
			i2c_put_byte(client,0x0101, 0xFF);

			break;

		default:
			break;
	}
	

} /* hm2057_set_param_effect */

/*************************************************************************
* FUNCTION
*    hm2057_NightMode
*
* DESCRIPTION
*    This function night mode of hm2057.
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
void hm2057_set_night_mode(struct hm2057_device *dev,enum  camera_night_mode_flip_e enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	if (enable) 		/* Night Mode */
	{
		/* HM2057 night mode enable. */
		if (HM2057_VEDIO_encode_mode == KAL_TRUE)	/* Video */
		{
			i2c_put_byte(client,0x038F,0x0A);	//Max 5 FPS.
			i2c_put_byte(client,0x0390,0x00);
		}
		else 										/* Camera */
		{
			i2c_put_byte(client,0x038F,0x0A);	//Max 5 FPS.
			i2c_put_byte(client,0x0390,0x00);
		}			
    
		i2c_put_byte(client,0x02E0,0x00);	// 00 for Night Mode, By Brandon/20110208
		i2c_put_byte(client,0x0481,0x06);	// 06 for Night Mode, By Brandon/20110208
		i2c_put_byte(client,0x04B1,0x88);	// 88 for Night Mode, By Brandon/20110208
		i2c_put_byte(client,0x04B4,0x20);	// 20 for Night Mode, By Brandon/20110208
		i2c_put_byte(client,0x0000,0xFF);
		i2c_put_byte(client,0x0100,0xFF);  
	}

}    /* hm2057_NightMode */
void hm2057_set_param_banding(struct hm2057_device *dev,enum  camera_banding_flip_e banding)
{
			 struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
			switch(banding)
				{
				case CAM_BANDING_50HZ:
					i2c_put_byte(client,0x0120,0x36);
					i2c_put_byte(client,0x0000, 0xFF);	//AE CMU   
					i2c_put_byte(client,0x0100, 0xFF);	//AE CMU	
					i2c_put_byte(client,0x0101, 0xFF);	//AE CMU			
					break;
				case CAM_BANDING_60HZ:
					i2c_put_byte(client,0x0120,0x37);
					i2c_put_byte(client,0x0000, 0xFF);	//AE CMU   
					i2c_put_byte(client,0x0100, 0xFF);	//AE CMU	
					i2c_put_byte(client,0x0101, 0xFF);	//AE CMU 
					break;
				default:
					break;
		
				}
}
#if 0
static int set_flip(struct hm2057_device *dev)
{
   	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	temp = i2c_get_byte(client, 0x0006);
	//printk("src temp is 0x%x\n", temp);
	temp &= 0xfc;
	temp |= dev->cam_info.m_flip << 0;
	temp |= dev->cam_info.v_flip << 1;
	//printk("dst temp is 0x%x\n", temp);
	if((i2c_put_byte(client, 0x0006, temp)) < 0) {
            printk("fail in setting sensor orientation \n");
            return -1;
        }
        return 0;
}
#endif


void hm2057_set_resolution(struct hm2057_device *dev,int height,int width)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
	//printk( KERN_INFO" set camera  HM2057_set_resolution=width =0x%d \n ",width);
	//printk( KERN_INFO" set camera  HM2057_set_resolution=height =0x%d \n ",height);

	if((width*height<640*480)){
		HM2057_h_active=320;
		HM2057_v_active=238;
	}
	 else if(width*height<1200*1600 ){
	  	
	  printk("*******************************************width800xheight600:%dx%d\n", width, height);


		//800*600  
		i2c_put_byte(client,0x0005,0x00);
		//mdelay(200);
		i2c_put_byte(client,0x000D,0x11);//
		i2c_put_byte(client,0x000E,0x11);//		
		i2c_put_byte(client,0x011F,0x88);//
		i2c_put_byte(client,0x0125,0xDF);//
		i2c_put_byte(client,0x0126,0x70);//
		i2c_put_byte(client,0x0131,0xAD);//
		i2c_put_byte(client,0x0366,0x08);// MinCTFCount, 08=20/4
		i2c_put_byte(client,0x0433,0x10);// ABLC LPoint, 10=40/4
		i2c_put_byte(client,0x0435,0x14);// ABLC UPoint, 14=50/4

	
		i2c_put_byte(client,0x05E4,0x05);// Windowing
		i2c_put_byte(client,0x05E5,0x00);//
		i2c_put_byte(client,0x05E6,0x24);//24
		i2c_put_byte(client,0x05E7,0x03);//
		i2c_put_byte(client,0x05E8,0x07);//
		i2c_put_byte(client,0x05E9,0x00);//
		i2c_put_byte(client,0x05EA,0x5E);//
		i2c_put_byte(client,0x05EB,0x02);//

		i2c_put_byte(client,0x0000,0x01);//
		i2c_put_byte(client,0x0100,0x01);//
		i2c_put_byte(client,0x0101,0x01);//

		
		i2c_put_byte(client,0x0000,0x01);//
		i2c_put_byte(client,0x0100,0x01);//
		i2c_put_byte(client,0x0101,0x01);//
		i2c_put_byte(client,0x0005,0x01);
	     mdelay(200);
		hm2057_frmintervals_active.denominator 	= 15;
		hm2057_frmintervals_active.numerator	= 1;
        HM2057_h_active=800;
        HM2057_v_active=598;
		
	} else if(width*height>=1600*1200){
		//1600x1200
		//i2c_put_byte(client,0x0005,0x00);
		//mdelay(200);
        i2c_put_byte(client,0x000D,0x00);
     	i2c_put_byte(client,0x000E,0x00);
     	i2c_put_byte(client,0x011F,0x88);
     	i2c_put_byte(client,0x0125,0xDF);
     	i2c_put_byte(client,0x0126,0x70);
     	i2c_put_byte(client,0x0131,0xAC);
     	i2c_put_byte(client,0x0366,0x20);// MinCTFCount, 08=20/4
     	i2c_put_byte(client,0x0433,0x40);// ABLC LPoint, 10=40/4
     	i2c_put_byte(client,0x0435,0x50);// ABLC UPoint, 14=50/4
     	i2c_put_byte(client,0x05E4,0x05);// Windowing a
     	i2c_put_byte(client,0x05E5,0x00);
     	i2c_put_byte(client,0x05E6,0x4C);//49
     	i2c_put_byte(client,0x05E7,0x06);
     	i2c_put_byte(client,0x05E8,0x08);//a
     	i2c_put_byte(client,0x05E9,0x00);
     	i2c_put_byte(client,0x05EA,0xBD);//b9
     	i2c_put_byte(client,0x05EB,0x04);
     	i2c_put_byte(client,0x0000,0x01);
     	i2c_put_byte(client,0x0100,0x01);
     	i2c_put_byte(client,0x0101,0x01);
		i2c_put_byte(client,0x0005,0x01);
     	mdelay(200);
		HM2057_h_active=1600;
		HM2057_v_active=1198;
		hm2057_frmintervals_active.denominator 	= 5;
		hm2057_frmintervals_active.numerator	= 1;
	}
	printk(KERN_INFO " set camera  HM2057_set_resolution=w=%d,h=%d. \n ",width,height);
	//set_flip(dev);	

}    /* HM2057_set_resolution */

unsigned char v4l_2_hm2057(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int hm2057_setting(struct hm2057_device *dev,int PROP_ID,int value )
{
	//printk("----------- %s \n",__func__);

	int ret=0;
	//unsigned char cur_val;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_hm2057(value));
		ret=i2c_put_byte(client,0x0201,v4l_2_hm2057(value));
		break;
	case V4L2_CID_CONTRAST:
		ret=i2c_put_byte(client,0x0200, value);
		break;
	case V4L2_CID_SATURATION:
		ret=i2c_put_byte(client,0x0202, value);
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
        if(hm2057_qctrl[0].default_value!=value){
			hm2057_qctrl[0].default_value=value;
			hm2057_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
        	}
		break;
	case V4L2_CID_EXPOSURE:
        if(hm2057_qctrl[1].default_value!=value){
			hm2057_qctrl[1].default_value=value;
			hm2057_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
        	}
		break;
	case V4L2_CID_COLORFX:
        if(hm2057_qctrl[2].default_value!=value){
			hm2057_qctrl[2].default_value=value;
			hm2057_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
        	}
		break;
	case V4L2_CID_WHITENESS:
		 if(hm2057_qctrl[3].default_value!=value){
			hm2057_qctrl[3].default_value=value;
			hm2057_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
        	}
		break;
	case V4L2_CID_BLUE_BALANCE:
		 if(hm2057_qctrl[4].default_value!=value){
			hm2057_qctrl[4].default_value=value;
			hm2057_set_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
        	}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(hm2057_qctrl[5].default_value!=value){
			hm2057_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(hm2057_qctrl[7].default_value!=value){
			hm2057_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
        	}
		break;
	case V4L2_CID_ROTATE:
	    printk(" set camera111  rotate =%d. \n ",value);
		if(hm2057_qctrl[8].default_value!=value){
			hm2057_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;

	

}

static void power_down_hm2057(struct hm2057_device *dev)
{
   	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	i2c_put_byte(client,0x0005,0x00);
    i2c_put_byte(client,0x0004, 0x17); //pwd
}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)
#if 0
static int getCameraState(){
    int i=get_adc_sample(0);
	if(i>374&&i<391){
        return 1;
	}else{
        return 0;
	}
}
#endif
static void hm2057_fillbuff(struct hm2057_fh *fh, struct hm2057_buffer *buf)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = hm2057_qctrl[5].default_value&3;// not set
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = hm2057_qctrl[7].default_value;
	para.angle = hm2057_qctrl[8].default_value;
	para.vaddr = (unsigned)vbuf;
	//camera flip function code
	/*if(camera_state<0){
       camera_state=getCameraState();
	   if(camera_state>0){
          i2c_put_byte(v4l2_get_subdevdata(&dev->sd), 0x0006, 0x02);
	   }else{
          i2c_put_byte(v4l2 _get_subdevdata(&dev->sd), 0x0006, 0x00);
	   }
	}else{
       if(camera_state!=getCameraState()){
          camera_state=getCameraState();
		  if(camera_state>0){
             i2c_put_byte(v4l2_get_subdevdata(&dev->sd), 0x0006, 0x02);
		  }else{
             i2c_put_byte(v4l2_get_subdevdata(&dev->sd), 0x0006, 0x00);
		  }
	   }
	}*/
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void hm2057_thread_tick(struct hm2057_fh *fh)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_buffer *buf;
	struct hm2057_device *dev = fh->dev;
	struct hm2057_dmaqueue *dma_q = &dev->vidq;

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
			 struct hm2057_buffer, vb.queue);
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
	hm2057_fillbuff(fh, buf);
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

static void hm2057_sleep(struct hm2057_fh *fh)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_device *dev = fh->dev;
	struct hm2057_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	hm2057_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int hm2057_thread(void *data)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_fh  *fh = data;
	struct hm2057_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		hm2057_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int hm2057_start_thread(struct hm2057_fh *fh)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_device *dev = fh->dev;
	struct hm2057_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(hm2057_thread, fh, "hm2057");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void hm2057_stop_thread(struct hm2057_dmaqueue  *dma_q)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_device *dev = container_of(dma_q, struct hm2057_device, vidq);

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
	//printk("----------- %s \n",__func__);

	struct hm2057_fh  *fh = vq->priv_data;
	struct hm2057_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct hm2057_buffer *buf)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_fh  *fh = vq->priv_data;
	struct hm2057_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);
    videobuf_waiton(vq, &buf->vb, 0, 0);
	if (in_interrupt())
		BUG();

	videobuf_vmalloc_free(&buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}
static int vidioc_enum_frameintervals(struct file *file, void *priv,
        struct v4l2_frmivalenum *fival)
{
    unsigned int k;

    if(fival->index > ARRAY_SIZE(hm2057_frmivalenum))
        return -EINVAL;

    for(k =0; k< ARRAY_SIZE(hm2057_frmivalenum); k++)
    {
        if( (fival->index==hm2057_frmivalenum[k].index)&&
                (fival->pixel_format ==hm2057_frmivalenum[k].pixel_format )&&
                (fival->width==hm2057_frmivalenum[k].width)&&
                (fival->height==hm2057_frmivalenum[k].height)){
            memcpy( fival, &hm2057_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
            return 0;
        }
    }

    return -EINVAL;

}
#define norm_maxw() 3000
#define norm_maxh() 3000
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_fh     *fh  = vq->priv_data;
	struct hm2057_device    *dev = fh->dev;
	struct hm2057_buffer *buf = container_of(vb, struct hm2057_buffer, vb);
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
	//printk("----------- %s \n",__func__);

	struct hm2057_buffer    *buf  = container_of(vb, struct hm2057_buffer, vb);
	struct hm2057_fh        *fh   = vq->priv_data;
	struct hm2057_device       *dev  = fh->dev;
	struct hm2057_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_buffer   *buf  = container_of(vb, struct hm2057_buffer, vb);
	struct hm2057_fh       *fh   = vq->priv_data;
	struct hm2057_device      *dev  = (struct hm2057_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops hm2057_video_qops = {
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
	//printk("----------- %s \n",__func__);

	struct hm2057_fh  *fh  = priv;
	struct hm2057_device *dev = fh->dev;

	strcpy(cap->driver, "hm2057");
	strcpy(cap->card, "hm2057");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = hm2057_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_fmt *fmt;

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
	//printk("----------- %s \n",__func__);

	struct hm2057_fh *fh = priv;

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
    struct hm2057_fh *fh = priv;
    struct hm2057_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = hm2057_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}
static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_fh  *fh  = priv;
	struct hm2057_device *dev = fh->dev;
	struct hm2057_fmt *fmt;
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
	//printk("----------- %s \n",__func__);

	struct hm2057_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct hm2057_device *dev = fh->dev;

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
		hm2057_set_resolution(dev,fh->height,fh->width);
	} else {
		hm2057_set_resolution(dev,fh->height,fh->width);
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
	//printk("----------- %s \n",__func__);

	struct hm2057_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct hm2057_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct hm2057_fh *fh = priv;
	struct hm2057_device *dev = fh->dev;
	vdin_parm_t para;
	int ret = 0 ;
	printk(KERN_INFO " vidioc_streamon+++ \n ");
	
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = hm2057_frmintervals_active.denominator;
	para.h_active = HM2057_h_active;
	para.v_active = HM2057_v_active;
	para.hsync_phase = 1;
	para.vsync_phase  = 1;
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
	para.skip_count = 2; //skip_num
	para.bt_path = dev->cam_info.bt_path;
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
		vops->start_tvin_service(0,&para);
		fh->stream_on        = 1;
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct hm2057_fh  *fh = priv;

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

static int vidioc_enum_framesizes(struct file *file, void *priv, struct v4l2_frmsizeenum *fsize)
{
	//printk("----------- %s \n",__func__);
	struct hm2057_fh *fh = priv;
	struct hm2057_device *dev = fh->dev;
	int ret = 0,i=0;
	struct hm2057_fmt *fmt = NULL;
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
		) {
		if (fsize->index >= ARRAY_SIZE(hm2057_prev_resolution))
			return -EINVAL;
		frmsize = &hm2057_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	} else if (fmt->fourcc == V4L2_PIX_FMT_RGB24) {
		//printk("dev->cam_info.max_cap_size is %d\n", dev->cam_info.max_cap_size);
		if (fsize->index >= get_total_pic_size(dev->cam_info.max_cap_size))
			return -EINVAL;
		frmsize = &hm2057_pic_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
		//printk("width %d; height %d\n", frmsize->width,  frmsize->height);
	}
	return ret;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id i)
{
	//printk("----------- %s \n",__func__);

	return 0;
}

/* only one input in this sample driver */
static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	//if (inp->index >= NUM_INPUTS)
		//return -EINVAL;
	//printk("----------- %s \n",__func__);

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_525_60;
	sprintf(inp->name, "Camera %u", inp->index);

	return (0);
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_fh *fh = priv;
	struct hm2057_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	//printk("----------- %s \n",__func__);

	struct hm2057_fh *fh = priv;
	struct hm2057_device *dev = fh->dev;

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
	//printk("----------- %s \n",__func__);

	for (i = 0; i < ARRAY_SIZE(hm2057_qctrl); i++)
		if (qc->id && qc->id == hm2057_qctrl[i].id) {
			memcpy(qc, &(hm2057_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	//printk("----------- %s \n",__func__);
	struct hm2057_fh *fh = priv;
	struct hm2057_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(hm2057_qctrl); i++)
		if (ctrl->id == hm2057_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	//printk("----------- %s \n",__func__);
	struct hm2057_fh *fh = priv;
	struct hm2057_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(hm2057_qctrl); i++)
		if (ctrl->id == hm2057_qctrl[i].id) {
			if (ctrl->value < hm2057_qctrl[i].minimum ||
			    ctrl->value > hm2057_qctrl[i].maximum ||
			    hm2057_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int hm2057_open(struct file *file)
{
	struct hm2057_device *dev = video_drvdata(file);
	struct hm2057_fh *fh = NULL;
	int retval = 0;

#if CONFIG_CMA
    retval = vm_init_buf(16*SZ_1M);
    if(retval <0)
    {
        pr_err("%s : Allocation from CMA failed\n", __func__);
        return -1;
    }
#endif

#ifdef CONFIG_ARCH_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif	
	aml_cam_init(&dev->cam_info);
	hm2057_init_regs(dev);
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

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &hm2057_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct hm2057_buffer), fh,NULL);

	hm2057_start_thread(fh);
    //msleep(50);  // added james
	return 0;
}

static ssize_t
hm2057_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct hm2057_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
hm2057_poll(struct file *file, struct poll_table_struct *wait)
{
	struct hm2057_fh        *fh = file->private_data;
	struct hm2057_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int hm2057_close(struct file *file)
{
	struct hm2057_fh         *fh = file->private_data;
	struct hm2057_device *dev       = fh->dev;
	struct hm2057_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);

	hm2057_stop_thread(vidq);
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
	HM2057_h_active=800;
	HM2057_v_active=600;
	hm2057_qctrl[0].default_value=0;
	hm2057_qctrl[1].default_value=4;
	hm2057_qctrl[2].default_value=0;
	hm2057_qctrl[3].default_value=0;
	hm2057_qctrl[4].default_value=0;

	hm2057_qctrl[5].default_value=0;
	hm2057_qctrl[7].default_value=100;
	hm2057_qctrl[8].default_value=0;
	camera_state=-1;
	hm2057_frmintervals_active.numerator = 1;
	hm2057_frmintervals_active.denominator = 15;
	power_down_hm2057(dev);
#endif
	aml_cam_uninit(&dev->cam_info);
#ifdef CONFIG_ARCH_MESON6
	switch_mod_gate_by_name("ge2d", 0);
#endif	
	wake_unlock(&(dev->wake_lock));
#ifdef CONFIG_CMA
    vm_deinit_buf();
#endif
	return 0;
}

static int hm2057_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct hm2057_fh  *fh = file->private_data;
	struct hm2057_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations hm2057_fops = {
	.owner		= THIS_MODULE,
	.open           = hm2057_open,
	.release        = hm2057_close,
	.read           = hm2057_read,
	.poll		= hm2057_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = hm2057_mmap,
};

static const struct v4l2_ioctl_ops hm2057_ioctl_ops = {
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
	.vidioc_g_parm = vidioc_g_parm,
	.vidioc_enum_frameintervals = vidioc_enum_frameintervals,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
};

static struct video_device hm2057_template = {
	.name		= "hm2057_v4l",
	.fops           = &hm2057_fops,
	.ioctl_ops 	= &hm2057_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int hm2057_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_HM2057, 0);
}

static const struct v4l2_subdev_core_ops hm2057_core_ops = {
	.g_chip_ident = hm2057_g_chip_ident,
};

static const struct v4l2_subdev_ops hm2057_ops = {
	.core = &hm2057_core_ops,
};

static int hm2057_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	struct hm2057_device *t;
	struct v4l2_subdev *sd;
	aml_cam_info_t* plat_dat;
	vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &hm2057_ops);
	mutex_init(&t->mutex);

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &hm2057_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);

	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "hm2057");
	/* Register it */
	plat_dat= (aml_cam_info_t*)client->dev.platform_data;
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if (plat_dat->front_back >=0)  
			video_nr = plat_dat->front_back;
	} else {
		printk("camera hm2057: have no platform data\n");
		kfree(t);
		return -1;
	}
		
	t->cam_info.version = HM2057_DRIVER_VERSION;
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

static int hm2057_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hm2057_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id hm2057_id[] = {
	{ "hm2057", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hm2057_id);

static struct i2c_driver hm2057_i2c_driver = {
	.driver = {
		.name = "hm2057",
	},
	.probe = hm2057_probe,
	.remove = hm2057_remove,
	.id_table = hm2057_id,
};

module_i2c_driver(hm2057_i2c_driver);


