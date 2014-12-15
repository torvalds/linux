/*
 *bf3720 - This code emulates a real video device with v4l2 api
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
#include "common/vm.h"

#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <mach/gpio.h>
#include "common/plat_ctrl.h"
#include <mach/mod_gate.h>

#define bf3720_CAMERA_MODULE_NAME "bf3720"
#define MAGIC_RE_MEM 0x123039dc
#define BF3720_RES0_CANVAS_INDEX CAMERA_USER_CANVAS_INDEX

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)           /* 0.5 seconds */

//#define  Auto_LSC_debug   // for debut lsc


#define bf3720_CAMERA_MAJOR_VERSION 0
#define bf3720_CAMERA_MINOR_VERSION 7
#define bf3720_CAMERA_RELEASE 0
#define bf3720_CAMERA_VERSION \
KERNEL_VERSION(bf3720_CAMERA_MAJOR_VERSION, bf3720_CAMERA_MINOR_VERSION, bf3720_CAMERA_RELEASE)

#define BF3720_DRIVER_VERSION "BF3720-COMMON-01-140717"

MODULE_DESCRIPTION("bf3720 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static int BF3720_h_active=800;
static int BF3720_v_active=598;
static struct v4l2_fract bf3720_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

static struct vdin_v4l2_ops_s *vops;

/* supported controls */
static struct v4l2_queryctrl bf3720_qctrl[] = {
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
struct v4l2_querymenu bf3720_qmenu_wbmode[] = {
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
	
struct v4l2_querymenu bf3720_qmenu_anti_banding_mode[] = {
		{
			.id 		= V4L2_CID_POWER_LINE_FREQUENCY,
			.index		= CAM_BANDING_50HZ, 
			.name		= "50hz",
			.reserved	= 0,
		},{
			.id 		= V4L2_CID_POWER_LINE_FREQUENCY,
			.index		= CAM_BANDING_60HZ, 
			.name		= "60hz",
			.reserved	= 0,
		},
	};
	
	typedef struct {
		__u32	id;
		int 	num;
		struct v4l2_querymenu* bf3720_qmenu;
	}bf3720_qmenu_set_t;
	
	bf3720_qmenu_set_t bf3720_qmenu_set[] = {
		{
			.id 			= V4L2_CID_DO_WHITE_BALANCE,
			.num			= ARRAY_SIZE(bf3720_qmenu_wbmode),
			.bf3720_qmenu	= bf3720_qmenu_wbmode,
		},{
			.id 			= V4L2_CID_POWER_LINE_FREQUENCY,
			.num			= ARRAY_SIZE(bf3720_qmenu_anti_banding_mode),
			.bf3720_qmenu	= bf3720_qmenu_anti_banding_mode,
		},
	};
	
	static int vidioc_querymenu(struct file *file, void *priv,
					struct v4l2_querymenu *a)
	{
		int i, j;
	
		for (i = 0; i < ARRAY_SIZE(bf3720_qmenu_set); i++)
		if (a->id && a->id == bf3720_qmenu_set[i].id) {
			for(j = 0; j < bf3720_qmenu_set[i].num; j++)
			if (a->index == bf3720_qmenu_set[i].bf3720_qmenu[j].index) {
				memcpy(a, &( bf3720_qmenu_set[i].bf3720_qmenu[j]),
					sizeof(*a));
				return (0);
			}
		}
	
		return -EINVAL;
	}

struct bf3720_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct bf3720_fmt formats[] = {
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

static struct bf3720_fmt *get_format(struct v4l2_format *f)
{
	struct bf3720_fmt *fmt;
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
struct bf3720_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct bf3720_fmt        *fmt;

	unsigned int canvas_id;
};

struct bf3720_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(bf3720_devicelist);

struct bf3720_device {
	struct list_head			bf3720_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct bf3720_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t  cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(bf3720_qctrl)];
};

static inline struct bf3720_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct bf3720_device, sd);
}

struct bf3720_fh {
	struct bf3720_device            *dev;

	/* video capture */
	struct bf3720_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	struct videobuf_res_privdata res;
	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};

static inline struct bf3720_fh *to_fh(struct bf3720_device *dev)
{
	return container_of(&dev, struct bf3720_fh, dev);
}

static struct v4l2_frmsize_discrete bf3720_prev_resolution[]= //should include 352x288 and 640x480, those two size are used for recording
{
	{320,240},
	{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete bf3720_pic_resolution[]=
{
	{640,480},
	{1600, 1200},
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
	reg spec of bf3720
   ------------------------------------------------------------------*/

struct aml_camera_i2c_fig_s bf3720_script[] = {
	
	{0x1b , 0x2f},  // 0x2f; 0x5b:3倍频;   0x6e:2倍频; 0X6F: 1倍频
	{0x11 , 0x00},
	{0x15 , 0x00},
	{0x3a , 0x01},  // YUV 输出顺序；03:VYUY; 02:UYVY; 01:YVYU; 00:YUYV                    
	{0x09 , 0x10},

	{0x13 , 0x80},
	{0x8c , 0x00},
	{0x8d , 0x04},
	{0x87 , 0x2a},
	{0x13 , 0x87},

	{0xbb , 0x20},	
	{0x2f , 0x10},
	{0x27 , 0xc1},
	{0x16 , 0x80},	
	{0xe1 , 0x98},
	{0xe4 , 0x0f},	
	{0xe3 , 0x04},
	{0xec , 0xa8},
	{0x20 , 0x40},
	{0xeb , 0xd5},
 	{0xd2 , 0x19},

	//lens shading
	{0x33 , 0x80},
	{0x34 , 0x28},
	{0x35 , 0x34},
	{0x65 , 0x32},
	{0x66 , 0x30},	
	{0xf1 , 0x00},	
	
	//AE	
	{0x24 , 0x50},//0x70 zhouj20140212
	{0x97 , 0x70},
	{0x25 , 0x88},
	{0x82 , 0x2a},
	{0x83 , 0x3a},
	{0x84 , 0x38},
	{0x85 , 0x41},
	{0x86 , 0x90},
	{0x8a , 0xBC},//0xa6
	{0x8b , 0x9c},
	{0x39 , 0x0f},
	{0x98 , 0x12},
	{0x3f , 0x30},//0x48 zhouj20140212
	{0x90 , 0x30},
	{0x91 , 0x20},
	{0x2d , 0x40},
	{0x2e , 0x38},
     /*
	//gamma 清晰亮丽	
	{0x40 , 0x58},
	{0x41 , 0x54},
	{0x42 , 0x4e},
	{0x43 , 0x44}, 
	{0x44 , 0x3e}, 
	{0x45 , 0x39},
	{0x46 , 0x35},
	{0x47 , 0x31},
	{0x48 , 0x2e}, 
	{0x49 , 0x2b}, 
	{0x4b , 0x29},
	{0x4c , 0x27},
	{0x4e , 0x23},
	{0x4f , 0x20}, 
	{0x50 , 0x1f},

	//gamma default
	{0x40 , 0x88},
	{0x41 , 0x70},
	{0x42 , 0x5f},
	{0x43 , 0x50}, 
	{0x44 , 0x46}, 
	{0x45 , 0x40},
	{0x46 , 0x3a},
	{0x47 , 0x36},
	{0x48 , 0x32}, 
	{0x49 , 0x2f}, 
	{0x4b , 0x2c},
	{0x4c , 0x29},
	{0x4e , 0x25},
	{0x4f , 0x22}, 
	{0x50 , 0x1f},
	 */
     /*
	//gamma 过曝过度好，高亮
	{0x40 , 0x98},
	{0x41 , 0x88},
	{0x42 , 0x6e},
	{0x43 , 0x59}, 
	{0x44 , 0x4d}, 
	{0x45 , 0x45},
	{0x46 , 0x3e},
	{0x47 , 0x39},
	{0x48 , 0x35}, 
	{0x49 , 0x31}, 
	{0x4b , 0x2e},
	{0x4c , 0x2b},
	{0x4e , 0x26},
	{0x4f , 0x23}, 
	{0x50 , 0x20},
      */
	//gamma low noise
	{0x40 , 0x68}, 
	{0x41 , 0x60}, 
	{0x42 , 0x54}, 
	{0x43 , 0x46}, 
	{0x44 , 0x3e}, 
	{0x45 , 0x39}, 
	{0x46 , 0x35}, 
	{0x47 , 0x31}, 
	{0x48 , 0x2e}, 
	{0x49 , 0x2b}, 
	{0x4b , 0x29}, 
	{0x4c , 0x27}, 
	{0x4e , 0x24}, 
	{0x4f , 0x22}, 
	{0x50 , 0x20}, 
	{0x3f , 0x30},
		
	//非A光CCM  CCM default
	{0xd4 , 0x0a},
	{0x51 , 0x26},
	{0x52 , 0x21},
	{0x53 , 0x05},
	{0x54 , 0x1e},
	{0x57 , 0x8d},
	{0x58 , 0x6e},
	{0x59 , 0x5a},
	{0x5a , 0x6e},
	{0x5b , 0x14},
	{0x5d , 0x91},
	{0xd3 , 0x00},
    /*
	//非A光 CCM 艳丽
	{0xd4 , 0x0a},
	{0x51 , 0x0a},
	{0x52 , 0x18},
	{0x53 , 0x0e},
	{0x54 , 0x17},
	{0x57 , 0x95},
	{0x58 , 0x7d},
	{0x59 , 0x62},
	{0x5a , 0x83},
	{0x5b , 0x21},
	{0x5d , 0x95},
	{0xd3 , 0x00},

	//非A光 CCM 色彩淡
	{0xd4 , 0x0a},
	{0x51 , 0x19},
	{0x52 , 0x24},
	{0x53 , 0x0b},
	{0x54 , 0x12},
	{0x57 , 0x77},
	{0x58 , 0x65},
	{0x59 , 0x45},		
	{0x5a , 0x5e},		
	{0x5b , 0x19},		
	{0x5d , 0x95},		
	{0xd3 , 0x00},
      
	//非A光 CCM 绿色较好
	{0xd4 , 0x0a},                   
	{0x51 , 0x05},                    
	{0x52 , 0x16},                    
	{0x53 , 0x11},                    
	{0x54 , 0x15},                    
	{0x57 , 0x8f},                    
	{0x58 , 0x7a},                    
	{0x59 , 0x48},                    
	{0x5a , 0x88},                    
	{0x5b , 0x3f},                    
	{0x5d , 0x95},                    
	{0xd3 , 0x00},
	*/

	//a光CCM
	{0xd4 , 0x4a},
	{0x51 , 0x0f},
	{0x52 , 0x35},
	{0x53 , 0x26},
	{0x54 , 0x17},
	{0x57 , 0x86},
	{0x58 , 0x9d},
	{0x59 , 0x5b},
	{0x5a , 0x8d},
	{0x5b , 0x32},
	{0x5d , 0x9d},
	{0xd3 , 0x00},

	//非A光饱和度	
	{0x3B , 0x00},
	{0xb1 , 0xc0},
	{0xb2 , 0x98},//0xa0

	//A光饱和度	
	{0x3B , 0x01},
	{0xb1 , 0xc0},
	{0xb2 , 0xa0},

	{0xb0 , 0xd4},
	{0xb3 , 0x86},


	{0x70 , 0x87},//高四位边缘增强
	{0x72 , 0x72},
	{0x7a , 0x33},//去噪寄存器
	{0x78 , 0x33},	
	{0x5c , 0x00},
	//gain


	{0xa2 , 0x0c},//AWB	
	{0xa3 , 0x27},
	{0xa4 , 0x09},
	{0xa5 , 0x30},
	{0xa7 , 0x97},
	{0xa8 , 0x19},
	{0xa9 , 0x52},
	{0xaa , 0x52},	
	{0xab , 0x18},
	{0xac , 0x30},
	{0xad , 0xf0},
	{0xae , 0x33},
	{0xd0 , 0x00},
	{0xd1 , 0x91},
	{0x23 , 0x44},
	{0xee , 0x30},	
	 
	{0x91 , 0x9c},
	{0x78 , 0x73},
	{0x7c , 0x48},
	{0xd9 , 0xb6},

     /*
        //vga
	{0xc0 , 0xd3},
	{0xc1 , 0x04},
	{0xc2 , 0xc4},
	{0xc3 , 0x0a},
	{0xc4 , 0x0a},
	{0x17 , 0x00},
	{0x18 , 0xa0},
	{0x19 , 0x00},
	{0x1a , 0x78},
	{0x12 , 0x10},
       */	
      //1600*1200
	{0xc0 , 0x00},
	{0xc1 , 0x00},
	{0xc2 , 0x00},
	{0xc3 , 0x00},
	{0xc4 , 0x00},

	//{0x12, 0x00},
	{0x17, 0x00},
	{0x18, 0xc8},		///c8
	{0x19, 0x00},
	{0x1a, 0x96},
	
    {0x12, 0x10}, 
        
	{0x1e , 0x30},//mirror and flip//0x00//0x20 //0x10
	{0x89 , 0x55},

	{0xff,0xff}, 
};

//load bf3720 parameters 
static void bf3720_init_regs(struct bf3720_device *dev)
{
	int i=0;//,j;
	unsigned char buf[2];
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
	while(1)
	{
		buf[0] = bf3720_script[i].addr;//(unsigned char)((bf3720_script[i].addr >> 8) & 0xff);
		//buf[1] = (unsigned char)(bf3720_script[i].addr & 0xff);
		buf[1] = bf3720_script[i].val;
		if (bf3720_script[i].val==0xff&&bf3720_script[i].addr==0xff) 
	 	{
	    		printk("bf3720_write_regs success in initial bf3720.\n");
	 		break;
	 	}
		if((i2c_put_byte_add8(client,buf, 2)) < 0)
	    	{
			printk("fail in initial bf3720. \n");
			return;
	    	}
		i++;	
	}
	msleep(20);
	return;
}
static struct v4l2_frmivalenum bf3720_frmivalenum[] = {
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
*    bf3720_set_param_wb
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
void bf3720_set_param_wb(struct bf3720_device *dev,enum  camera_wb_flip_e para)//white balance
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    unsigned char buf[2];
    switch (para)
	{
		case CAM_WB_AUTO://auto
			buf[0]=0x13;
			buf[1]=0x87;
			i2c_put_byte_add8(client,buf,2);	
		    	printk("CAM_WB_AUTO       \n");
			
			break;

		case CAM_WB_CLOUD: //cloud

			buf[0]=0x13;
			buf[1]=0x85;
			i2c_put_byte_add8(client,buf,2);	
			
			printk("CAM_WB_CLOUD       \n");
			buf[0]=0x01;
			buf[1]=0x06;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x02;
			buf[1]=0x1d;
			i2c_put_byte_add8(client,buf,2);		
			
			break;

		case CAM_WB_DAYLIGHT: //
			buf[0]=0x13;
			buf[1]=0x85;
			i2c_put_byte_add8(client,buf,2);	
			printk("CAM_WB_DAYLIGHT       \n");
			buf[0]=0x01;
			buf[1]=0x0d;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x02;
			buf[1]=0x16;
			i2c_put_byte_add8(client,buf,2);		

			break;

		case CAM_WB_INCANDESCENCE:
			buf[0]=0x13;
			buf[1]=0x85;
			i2c_put_byte_add8(client,buf,2);	
			
			printk("CAM_WB_INCANDESCENCE       \n");
			buf[0]=0x01;
			buf[1]=0x13;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x02;
			buf[1]=0x11;
			i2c_put_byte_add8(client,buf,2);		


			break;

		case CAM_WB_TUNGSTEN://wusideng
			buf[0]=0x13;
			buf[1]=0x85;
			i2c_put_byte_add8(client,buf,2);	
                     printk("CAM_WB_TUNGSTEN       \n");
			buf[0]=0x01;
			buf[1]=0x13;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x02;
			buf[1]=0x11;
			i2c_put_byte_add8(client,buf,2);		

	
			break;

      	case CAM_WB_FLUORESCENT:

			buf[0]=0x13;
			buf[1]=0x85;
			i2c_put_byte_add8(client,buf,2);	
			printk("CAM_WB_FLUORESCENT       \n");
 			buf[0]=0x01;
			buf[1]=0x0a;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x02;
			buf[1]=0x1b;
			i2c_put_byte_add8(client,buf,2);		

			break;
		case CAM_WB_MANUAL:
		    	                      // TODO
			break;
			
		default:
			break;
	}

} /* bf3720_set_param_wb */
/*************************************************************************
* FUNCTION
*    bf3720_set_param_exposure
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
void bf3720_set_param_exposure(struct bf3720_device *dev,enum camera_exposure_e para)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    unsigned char buf[2];
    switch (para)
	{
		case EXPOSURE_N4_STEP:	
			buf[0]=0x55;
			buf[1]=0xc8;
			i2c_put_byte_add8(client,buf,2);
			break;
			
		case EXPOSURE_N3_STEP:	
			buf[0]=0x55;
			buf[1]=0xb8;
			i2c_put_byte_add8(client,buf,2);
			break;

		case EXPOSURE_N2_STEP:	
			buf[0]=0x55;
			buf[1]=0xa8;
			i2c_put_byte_add8(client,buf,2);
			break;

		case EXPOSURE_N1_STEP:	
			printk("EXPOSURE_N1_STEP       \n");
			buf[0]=0x55;
			buf[1]=0x98;
			i2c_put_byte_add8(client,buf,2);
			break;

		case EXPOSURE_0_STEP:
            printk("EXPOSURE_0_STEP       \n");
			buf[0]=0x55;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			break;

		case EXPOSURE_P1_STEP:
            printk("EXPOSURE_P1_STEP       \n");	
			buf[0]=0x55;
			buf[1]=0x18;
			i2c_put_byte_add8(client,buf,2);
			break;

		case EXPOSURE_P2_STEP:			
			buf[0]=0x55;
			buf[1]=0x28;
			i2c_put_byte_add8(client,buf,2);
			break;

		case EXPOSURE_P3_STEP:
			buf[0]=0x55;
			buf[1]=0x38;
			i2c_put_byte_add8(client,buf,2);
			break;

		case EXPOSURE_P4_STEP:			
			buf[0]=0x55;
			buf[1]=0x48;
			i2c_put_byte_add8(client,buf,2);
			break;

		default:			
			buf[0]=0x55;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			break;
	}
	
	mdelay(20);

} /* bf3720_set_param_exposure */
/*************************************************************************
* FUNCTION
*    bf3720_set_param_effect
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
void bf3720_set_param_effect(struct bf3720_device *dev,enum camera_effect_flip_e para)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//unsigned char buf[4];
/*

    switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL:
            buf[0]=0x98;
			buf[1]=0x8a;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x70;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x69;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
			buf[1]=0x80;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
			buf[1]=0x80;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x56;
			buf[1]=0x28;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb0;
			buf[1]=0x8d;
			i2c_put_byte_add8(client,buf,2);
            buf[0]=0xb1;
			buf[1]=0x15; 
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x56;
			buf[1]=0xe8;
			i2c_put_byte_add8(client,buf,2);
		    buf[0]=0xb4;
			buf[1]=0x40;
			i2c_put_byte_add8(client,buf,2);	
																													
			break;

		case CAM_EFFECT_ENC_GRAYSCALE:
			buf[0]=0x98;
			buf[1]=0x8a;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x70;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x69;
			buf[1]=0x20;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
			buf[1]=0x80;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
			buf[1]=0x80;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x56;
			buf[1]=0x28;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb0;
			buf[1]=0x8d;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb1;
			buf[1]=0x95;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x56;
			buf[1]=0xe8;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb4;
			buf[1]=0x40;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_EFFECT_ENC_SEPIA:
			buf[0]=0x98;
			buf[1]=0x8a;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x70;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x69;
			buf[1]=0x20;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
			buf[1]=0x58;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
			buf[1]=0xa0;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x56;
			buf[1]=0x28;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb0;
			buf[1]=0x8d;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb1;
			buf[1]=0x95;
			i2c_put_byte_add8(client,buf,2);
		    buf[0]=0x56;
			buf[1]=0xe8;
            i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb4;
			buf[1]=0x40;
			i2c_put_byte_add8(client,0xb4,0x40);
            i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_EFFECT_ENC_SEPIAGREEN:
			buf[0]=0x98;
			buf[1]=0x8a;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x70;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
            buf[0]=0x69;
			buf[1]=0x20;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
			buf[1]=0x68;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
			buf[1]=0x60;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x56;
			buf[1]=0x28;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb0;
			buf[1]=0x8d;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb1;
			buf[1]=0x95;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x56;
			buf[1]=0xe8;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb4;
			buf[1]=0x40;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_EFFECT_ENC_SEPIABLUE:
			buf[0]=0x98;
			buf[1]=0x8a;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x70;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x69;
			buf[1]=0x20;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
			buf[1]=0xc0;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
			buf[1]=0x58;
			i2c_put_byte_add8(client,buf,2);
            buf[0]=0x56;
			buf[1]=0x28;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb0;
			buf[1]=0x8d;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb1;
			buf[1]=0x95;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x56;
			buf[1]=0xe8;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb4;
			buf[1]=0x40;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_EFFECT_ENC_COLORINV:
			buf[0]=0x98;
			buf[1]=0x8a;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x70;
			buf[1]=0x00;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x69;
			buf[1]=0x01;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
			buf[1]=0x80;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
			buf[1]=0x80;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x56;
			buf[1]=0x28;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb0;
			buf[1]=0x8d;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb1;
			buf[1]=0x95;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x56;
			buf[1]=0xe8;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0xb4;
			buf[1]=0x40;
			i2c_put_byte_add8(client,buf,2);
			break;

		default:
			break;
	}

*/

} /* bf3720_set_param_effect */

/*************************************************************************
* FUNCTION
*    bf3720_NightMode
*
* DESCRIPTION
*    This function night mode of bf3720.
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
void bf3720_set_night_mode(struct bf3720_device *dev,enum  camera_night_mode_flip_e enable)
{
/*
	if (enable) {
		buf[0]=0x86;
		buf[1]=0xff;
		i2c_put_byte_add8(client,buf ,2); //Camera Enable night mode 
	    buf[0]=0x89;
		buf[1]=0xa5;
	    i2c_put_byte_add8(client,buf, 2); //Camera Enable night mode  
	}
	else {
		buf[0]=0x86;
		buf[1]=0xc0;
		i2c_put_byte_add8(client,buf , 2); // //97
		buf[0]=0x89;
		buf[1]=0x5d;
	    i2c_put_byte_add8(client,0x89 , 0x5d);  
	}
*/
}    /* bf3720_NightMode */
void bf3720_set_param_banding(struct bf3720_device *dev,enum  camera_banding_flip_e banding)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[2];
	switch(banding)
	{

		case CAM_BANDING_50HZ:
			buf[0]=0x80;
			buf[1]=0x82;   // ad clock-48m,2pclk=96m mclk
			i2c_put_byte_add8(client,buf,2);
			
			buf[0]=0x8a;
			buf[1]=0xbc;//24 M  //0xa6//0xbc 3XLCK//0X7D 2XLCK//
			i2c_put_byte_add8(client,buf,2);
			break;
		case CAM_BANDING_60HZ:

			buf[0]=0x80;
			buf[1]=0x80;
			i2c_put_byte_add8(client,buf,2);

			buf[0]=0x8b;
			buf[1]=0x9c;//24 M //0x8a//0x9c 3XLCK//0X68 2XLCK//
			i2c_put_byte_add8(client,buf,2);		
			break;
		default:
		    	break;

	}
}

static int set_flip(struct bf3720_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp = 0;
	unsigned char buf[2];
	temp = i2c_get_byte_add8(client, 0x1e);
	temp |= dev->cam_info.m_flip << 5;
	temp |= dev->cam_info.v_flip << 4;
	buf[0] = 0x1e;
	buf[1] = temp;
	if((i2c_put_byte_add8(client, buf, 2)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
	}
	return 0;
}

//static unsigned int shutter_l = 0;
//static unsigned int shutter_h = 0;

void bf3720_set_resolution(struct bf3720_device *dev,int height,int width)
{
	unsigned char buf[2];
//	unsigned  int value;
//	unsigned   int pid=0,shutter;
//	static unsigned int shutter_l = 0;
//	static unsigned int shutter_h = 0;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	
	//printk( KERN_INFO" set camera  BF3720_set_resolution=width =0x%d \n ",width);
	//printk( KERN_INFO" set camera  BF3720_set_resolution=height =0x%d \n ",height);
	if (width*height<640*480){		

		 printk("####################set 320X240 #################################\n");
		 bf3720_frmintervals_active.denominator	= 15;
   		 bf3720_frmintervals_active.numerator	= 1;
   		 BF3720_h_active=320;
   		 BF3720_v_active=238;
   		
	}
	else if((width*height<1600*1200))
	{
		//800*600 
            printk("####################set 800X600 #################################\n");

			//buf[0]=0x1b;
    		//buf[1]=0x2f;
    		//i2c_put_byte_add8(client,buf,2);
			
    		buf[0]=0xc0;
    		buf[1]=0x00;
    		i2c_put_byte_add8(client,buf,2);
    		
    		buf[0]=0xc1;
    		buf[1]=0x00;   
    		i2c_put_byte_add8(client,buf,2);
    		
    		buf[0]=0xc2;
    		buf[1]=0x00;
    		i2c_put_byte_add8(client,buf,2);
    		
    		
    		buf[0]=0xc3;
    		buf[1]=0x00;
    		i2c_put_byte_add8(client,buf,2);
    		
    		buf[0]=0xc4;
    		buf[1]=0x00;
    		i2c_put_byte_add8(client,buf,2);
    		
    
    		
    		buf[0]=0x17;
    		buf[1]=0x00;  
    		i2c_put_byte_add8(client,buf,2);
    		
    		buf[0]=0x18;
    		buf[1]=0xc8;
    		i2c_put_byte_add8(client,buf,2);
    		
    		buf[0]=0x19;
    		buf[1]=0x00;
    		i2c_put_byte_add8(client,buf,2);
    		
    		buf[0]=0x1a;
    		buf[1]=0x96;  
    		i2c_put_byte_add8(client,buf,2);
    		
    		buf[0]=0x12;
    		buf[1]=0x10;  
    		i2c_put_byte_add8(client,buf,2);
    
    	   // buf[0]=0x89;
    		//buf[1]=0x3d;  
    		//i2c_put_byte_add8(client,buf,2);
			
    	bf3720_frmintervals_active.numerator = 1;
    	bf3720_frmintervals_active.denominator = 15;
    	BF3720_h_active=800;
    	BF3720_v_active=598;
		mdelay(50);
	} else if(width*height>=1200*1600 ) {
	    printk("####################set 1600X1200 #################################\n");
		buf[0]=0xc0;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0xc1;
		buf[1]=0x00;   
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0xc2;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		
		buf[0]=0xc3;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0xc4;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x12;
		buf[1]=0x00;  
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x17;
		buf[1]=0x00;  
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x18;
		buf[1]=0xc8;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x19;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		
		buf[0]=0x1a;
		buf[1]=0x96;  
		i2c_put_byte_add8(client,buf,2);
		
		//buf[0]=0x1b; // 0x2f; 0x5b:3倍频;   0x6e:2倍频; 0X6F: 1倍频
		//buf[1]=0x2f;  
		//i2c_put_byte_add8(client,buf,2);
		//mdelay(20);
			
		bf3720_frmintervals_active.denominator 	= 5;
		bf3720_frmintervals_active.numerator	= 1;
		BF3720_h_active=1600;
		BF3720_v_active=1198;

		mdelay(130);
	}
	printk(KERN_INFO " set camera  BF3720_set_resolution=w=%d,h=%d. \n ",width,height);
	set_flip(dev);	

}    /* BF3720_set_resolution */

unsigned char v4l_2_bf3720(int val)
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

static int bf3720_setting(struct bf3720_device *dev,int PROP_ID,int value )
{
	//printk("----------- %s \n",__func__);

	int ret=0;
	//unsigned char cur_val;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_bf3720(value));
	//	ret=i2c_put_byte(client,0x0201,v4l_2_bf3720(value));
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
#endif
	case V4L2_CID_DO_WHITE_BALANCE:
        if(bf3720_qctrl[0].default_value!=value){
			bf3720_qctrl[0].default_value=value;
			bf3720_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
        	}
		break;
	case V4L2_CID_EXPOSURE:
        if(bf3720_qctrl[1].default_value!=value){
			bf3720_qctrl[1].default_value=value;
			bf3720_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
        	}
		break;
	case V4L2_CID_COLORFX:
        if(bf3720_qctrl[2].default_value!=value){
			bf3720_qctrl[2].default_value=value;
			bf3720_set_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
        	}
		break;
	case V4L2_CID_WHITENESS:
		 if(bf3720_qctrl[3].default_value!=value){
			bf3720_qctrl[3].default_value=value;
			bf3720_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
        	}
		break;
	case V4L2_CID_BLUE_BALANCE:
		 if(bf3720_qctrl[4].default_value!=value){
			bf3720_qctrl[4].default_value=value;
			bf3720_set_night_mode(dev,value);
			printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
        	}
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */          
		value = value & 0x3;
		if(bf3720_qctrl[5].default_value!=value){
			bf3720_qctrl[5].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(bf3720_qctrl[7].default_value!=value){
			bf3720_qctrl[7].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
        	}
		break;
	case V4L2_CID_ROTATE:
	    printk(" set camera111  rotate =%d. \n ",value);
		if(bf3720_qctrl[8].default_value!=value){
			bf3720_qctrl[8].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;

	

}

static void power_down_bf3720(struct bf3720_device *dev)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//unsigned char buf[4];
			//buf[0]=0x45;
			//buf[1]=0x00;
			//i2c_put_byte_add8(client,buf,2);
}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void bf3720_fillbuff(struct bf3720_fh *fh, struct bf3720_buffer *buf)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_device *dev = fh->dev;
	//void *vbuf = videobuf_to_vmalloc(&buf->vb);
        void *vbuf = (void *)videobuf_to_res(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
        if(buf->canvas_id == 0)
           buf->canvas_id = convert_canvas_index(fh->fmt->fourcc, BF3720_RES0_CANVAS_INDEX+buf->vb.i*3);
	para.mirror = bf3720_qctrl[5].default_value&3;// not set
	para.v4l2_format = fh->fmt->fourcc;
	//para.v4l2_memory = 0x18221223;
        para.v4l2_memory = MAGIC_RE_MEM;
	para.zoom = bf3720_qctrl[7].default_value;
	para.angle = bf3720_qctrl[8].default_value;
	para.vaddr = (unsigned)vbuf;
        para.ext_canvas = buf->canvas_id;
        para.width = buf->vb.width;
        para.height = buf->vb.height;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void bf3720_thread_tick(struct bf3720_fh *fh)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_buffer *buf;
	struct bf3720_device *dev = fh->dev;
	struct bf3720_dmaqueue *dma_q = &dev->vidq;

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
			 struct bf3720_buffer, vb.queue);
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
	bf3720_fillbuff(fh, buf);
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

static void bf3720_sleep(struct bf3720_fh *fh)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_device *dev = fh->dev;
	struct bf3720_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	bf3720_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int bf3720_thread(void *data)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_fh  *fh = data;
	struct bf3720_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		bf3720_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int bf3720_start_thread(struct bf3720_fh *fh)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_device *dev = fh->dev;
	struct bf3720_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(bf3720_thread, fh, "bf3720");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void bf3720_stop_thread(struct bf3720_dmaqueue  *dma_q)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_device *dev = container_of(dma_q, struct bf3720_device, vidq);

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

        struct videobuf_res_privdata *res = vq->priv_data;
        struct bf3720_fh *fh  = container_of(res, struct bf3720_fh, res);
	struct bf3720_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct bf3720_buffer *buf)
{
	//printk("----------- %s \n",__func__);

        struct videobuf_res_privdata *res = vq->priv_data;
        struct bf3720_fh *fh  = container_of(res, struct bf3720_fh, res);
	struct bf3720_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);
    videobuf_waiton(vq, &buf->vb, 0, 0);
	if (in_interrupt())
		BUG();

       videobuf_res_free(vq, &buf->vb);

	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}
static int vidioc_enum_frameintervals(struct file *file, void *priv,
        struct v4l2_frmivalenum *fival)
{
    unsigned int k;

    if(fival->index > ARRAY_SIZE(bf3720_frmivalenum))
        return -EINVAL;

    for(k =0; k< ARRAY_SIZE(bf3720_frmivalenum); k++)
    {
        if( (fival->index==bf3720_frmivalenum[k].index)&&
                (fival->pixel_format ==bf3720_frmivalenum[k].pixel_format )&&
                (fival->width==bf3720_frmivalenum[k].width)&&
                (fival->height==bf3720_frmivalenum[k].height)){
            memcpy( fival, &bf3720_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
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

        struct videobuf_res_privdata *res = vq->priv_data;
        struct bf3720_fh *fh  = container_of(res, struct bf3720_fh, res);
	struct bf3720_device    *dev = fh->dev;
	struct bf3720_buffer *buf = container_of(vb, struct bf3720_buffer, vb);
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

	struct bf3720_buffer    *buf  = container_of(vb, struct bf3720_buffer, vb);
        struct videobuf_res_privdata *res = vq->priv_data;
        struct bf3720_fh *fh  = container_of(res, struct bf3720_fh, res);

	struct bf3720_device       *dev  = fh->dev;
	struct bf3720_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_buffer   *buf  = container_of(vb, struct bf3720_buffer, vb);
	struct bf3720_fh       *fh   = vq->priv_data;
	struct bf3720_device      *dev  = (struct bf3720_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops bf3720_video_qops = {
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

	struct bf3720_fh  *fh  = priv;
	struct bf3720_device *dev = fh->dev;

	strcpy(cap->driver, "bf3720");
	strcpy(cap->card, "bf3720.canvas");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = bf3720_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_fmt *fmt;

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

	struct bf3720_fh *fh = priv;

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
    struct bf3720_fh *fh = priv;
    struct bf3720_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = bf3720_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}
static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_fh  *fh  = priv;
	struct bf3720_device *dev = fh->dev;
	struct bf3720_fmt *fmt;
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

	struct bf3720_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct bf3720_device *dev = fh->dev;
	int ret = vidioc_try_fmt_vid_cap(file, fh, f);

        f->fmt.pix.width = (f->fmt.pix.width + (CANVAS_WIDTH_ALIGN-1) ) & (~(CANVAS_WIDTH_ALIGN-1));
	if ((f->fmt.pix.pixelformat==V4L2_PIX_FMT_YVU420) ||
            (f->fmt.pix.pixelformat==V4L2_PIX_FMT_YUV420)){
                f->fmt.pix.width = (f->fmt.pix.width + (CANVAS_WIDTH_ALIGN*2-1) ) & (~(CANVAS_WIDTH_ALIGN*2-1));
        }
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
		bf3720_set_resolution(dev,fh->height,fh->width);
	} else {
		bf3720_set_resolution(dev,fh->height,fh->width);
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

	struct bf3720_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_fh  *fh = priv;

        int ret = videobuf_querybuf(&fh->vb_vidq, p);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
        if(ret == 0){
        	p->reserved  = convert_canvas_index(fh->fmt->fourcc, BF3720_RES0_CANVAS_INDEX + p->index*3);
        }else{
        	p->reserved = 0;
        }
#endif
        return ret;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct bf3720_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct bf3720_fh *fh = priv;
	struct bf3720_device *dev = fh->dev;
	vdin_parm_t para;
	int ret = 0 ;
	
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	para.port  = TVIN_PORT_CAMERA;
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = bf3720_frmintervals_active.denominator;
	para.h_active = BF3720_h_active;
	para.v_active = BF3720_v_active;
	para.hsync_phase = 1;
	para.vsync_phase  = 1;
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = TVIN_YUV422;
        para.dfmt = TVIN_NV21;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
	para.skip_count = 4; //skip_num
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
	struct bf3720_fh  *fh = priv;

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

static int vidioc_enum_framesizes(struct file *file, void *priv, struct v4l2_frmsizeenum *fsize)
{
	//printk("----------- %s \n",__func__);
	struct bf3720_fh *fh = priv;
	struct bf3720_device *dev = fh->dev;
	int ret = 0,i=0;
	struct bf3720_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(bf3720_prev_resolution))
			return -EINVAL;
		frmsize = &bf3720_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	} else if (fmt->fourcc == V4L2_PIX_FMT_RGB24) {
		//printk("dev->cam_info.max_cap_size is %d\n", dev->cam_info.max_cap_size);
		if (fsize->index >= get_total_pic_size(dev->cam_info.max_cap_size))
			return -EINVAL;
		frmsize = &bf3720_pic_resolution[fsize->index];
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

	struct bf3720_fh *fh = priv;
	struct bf3720_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	//printk("----------- %s \n",__func__);

	struct bf3720_fh *fh = priv;
	struct bf3720_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(bf3720_qctrl); i++)
		if (qc->id && qc->id == bf3720_qctrl[i].id) {
			memcpy(qc, &(bf3720_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	//printk("----------- %s \n",__func__);
	struct bf3720_fh *fh = priv;
	struct bf3720_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(bf3720_qctrl); i++)
		if (ctrl->id == bf3720_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	//printk("----------- %s \n",__func__);
	struct bf3720_fh *fh = priv;
	struct bf3720_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(bf3720_qctrl); i++)
		if (ctrl->id == bf3720_qctrl[i].id) {
			if (ctrl->value < bf3720_qctrl[i].minimum ||
			    ctrl->value > bf3720_qctrl[i].maximum ||
			    bf3720_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int bf3720_open(struct file *file)
{
	struct bf3720_device *dev = video_drvdata(file);
	struct bf3720_fh *fh = NULL;
	int retval = 0;
        resource_size_t mem_start = 0;
        unsigned int mem_size = 0;

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif	
	aml_cam_init(&dev->cam_info);
	bf3720_init_regs(dev);
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
	get_vm_buf_info(&mem_start, &mem_size, NULL);
	fh->res.start = mem_start;
	fh->res.end = mem_start+mem_size-1;
	fh->res.magic = MAGIC_RE_MEM;
	fh->res.priv = NULL;
	videobuf_queue_res_init(&fh->vb_vidq, &bf3720_video_qops,
					NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
					sizeof(struct bf3720_buffer), (void*)&fh->res, NULL);

	bf3720_start_thread(fh);
    //msleep(50);  // added james
	return 0;
}

static ssize_t
bf3720_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct bf3720_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
bf3720_poll(struct file *file, struct poll_table_struct *wait)
{
	struct bf3720_fh        *fh = file->private_data;
	struct bf3720_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int bf3720_close(struct file *file)
{
	struct bf3720_fh         *fh = file->private_data;
	struct bf3720_device *dev       = fh->dev;
	struct bf3720_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);

	bf3720_stop_thread(vidq);
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
	BF3720_h_active=800;
	BF3720_v_active=598;
	bf3720_qctrl[0].default_value=0;
	bf3720_qctrl[1].default_value=4;
	bf3720_qctrl[2].default_value=0;
	bf3720_qctrl[3].default_value=0;
	bf3720_qctrl[4].default_value=0;

	bf3720_qctrl[5].default_value=0;
	bf3720_qctrl[7].default_value=100;
	bf3720_qctrl[8].default_value=0;
	bf3720_frmintervals_active.numerator = 1;
	bf3720_frmintervals_active.denominator = 15;
	power_down_bf3720(dev);
#endif
	aml_cam_uninit(&dev->cam_info);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 0);
#endif	
	wake_unlock(&(dev->wake_lock));

	return 0;
}

static int bf3720_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct bf3720_fh  *fh = file->private_data;
	struct bf3720_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations bf3720_fops = {
	.owner		= THIS_MODULE,
	.open           = bf3720_open,
	.release        = bf3720_close,
	.read           = bf3720_read,
	.poll		= bf3720_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = bf3720_mmap,
};

static const struct v4l2_ioctl_ops bf3720_ioctl_ops = {
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
	.vidioc_querymenu     = vidioc_querymenu,
	.vidioc_queryctrl     = vidioc_queryctrl,
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

static struct video_device bf3720_template = {
	.name		= "bf3720_v4l",
	.fops           = &bf3720_fops,
	.ioctl_ops 	= &bf3720_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int bf3720_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_GT2005, 0);
}

static const struct v4l2_subdev_core_ops bf3720_core_ops = {
	.g_chip_ident = bf3720_g_chip_ident,
};

static const struct v4l2_subdev_ops bf3720_ops = {
	.core = &bf3720_core_ops,
};

static int bf3720_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	struct bf3720_device *t;
	struct v4l2_subdev *sd;
	aml_cam_info_t* plat_dat;
	vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &bf3720_ops);
	mutex_init(&t->mutex);

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &bf3720_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);

	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "bf3720");
	/* Register it */
	plat_dat= (aml_cam_info_t*)client->dev.platform_data;
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if (plat_dat->front_back >=0) { 
			   video_nr = plat_dat->front_back;
		}
	} else {
		printk("camera bf3720: have no platform data\n");
		kfree(t);
		return -1;
	}
	
	t->cam_info.version = BF3720_DRIVER_VERSION;
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

static int bf3720_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct bf3720_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id bf3720_id[] = {
	{ "bf3720", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bf3720_id);

static struct i2c_driver bf3720_i2c_driver = {
	.driver = {
		.name = "bf3720",
	},
	.probe = bf3720_probe,
	.remove = bf3720_remove,
	.id_table = bf3720_id,
};

module_i2c_driver(bf3720_i2c_driver);



