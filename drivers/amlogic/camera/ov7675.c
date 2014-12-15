/*
 *ov7675 - This code emulates a real video device with v4l2 api
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
#include <mach/pinmux.h>
#include "common/plat_ctrl.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif

#define OV7675_CAMERA_MODULE_NAME "ov7675"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define OV7675_CAMERA_MAJOR_VERSION 0
#define OV7675_CAMERA_MINOR_VERSION 7
#define OV7675_CAMERA_RELEASE 0
#define OV7675_CAMERA_VERSION \
	KERNEL_VERSION(OV7675_CAMERA_MAJOR_VERSION, OV7675_CAMERA_MINOR_VERSION, OV7675_CAMERA_RELEASE)

MODULE_DESCRIPTION("ov7675 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define OV7675_DRIVER_VERSION "OV7675-COMMON-01-140717"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
static unsigned int ov7675_have_opened = 0;
static struct i2c_client *this_client;
static struct vdin_v4l2_ops_s *vops;
static struct v4l2_fract ov7675_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");


/* supported controls */
static struct v4l2_queryctrl ov7675_qctrl[] = {
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
static struct v4l2_frmivalenum ov7675_frmivalenum[]={
		 {
			 .index 		= 0,
			 .pixel_format	= V4L2_PIX_FMT_NV21,
			 .width 	= 640,
			 .height		= 480,
			 .type		= V4L2_FRMIVAL_TYPE_DISCRETE,
			 {
				 .discrete	={
				 .numerator = 1,
				 .denominator	= 15,
				 }
			 }
			 },{
				 .index 		= 1,
				 .pixel_format	= V4L2_PIX_FMT_NV21,
				 .width 	= 1600,
				 .height		= 1200,
				 .type		= V4L2_FRMIVAL_TYPE_DISCRETE,
				 {
					 .discrete	={
						 .numerator = 1,
						 .denominator	= 5,
					 }
				 }
			 },
};
struct ov7675_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct ov7675_fmt formats[] = {
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

static struct ov7675_fmt *get_format(struct v4l2_format *f)
{
	struct ov7675_fmt *fmt;
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
struct ov7675_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct ov7675_fmt        *fmt;
};

struct ov7675_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(ov7675_devicelist);

struct ov7675_device {
	struct list_head			ov7675_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct ov7675_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t  cam_info;
	
	/* wake lock */
	struct wake_lock	wake_lock;

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(ov7675_qctrl)];
};

static inline struct ov7675_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov7675_device, sd);
}

struct ov7675_fh {
	struct ov7675_device            *dev;

	/* video capture */
	struct ov7675_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	int  stream_on;
	unsigned int		f_flags;
};

/*static inline struct ov7675_fh *to_fh(struct ov7675_device *dev)
{
	return container_of(dev, struct ov7675_fh, dev);
}*/

static struct v4l2_frmsize_discrete ov7675_prev_resolution[]=
{
	{320,240},
	{352,288},
	{640,480},
};

static struct v4l2_frmsize_discrete ov7675_pic_resolution[]=
{
	{640,480},
};

/* ------------------------------------------------------------------
	reg spec of OV7675
   ------------------------------------------------------------------*/

#if 1

struct aml_camera_i2c_fig1_s OV7675_script[] = {
       //Input clock 24Mhz, 25fps
//SCCB_salve_Address = 0x42;
{0x12, 0x80},
{0x11, 0x00},
{0x3a, 0x04},
{0x12, 0x00},
{0x17, 0x13},
{0x18, 0x01},
{0x32, 0xb6},
{0x19, 0x03},
{0x1a, 0x7b},
{0x03, 0x08},
{0x0c, 0x00},
{0x3e, 0x00},
{0x70, 0x3a},
{0x71, 0x35},
{0x72, 0x11},
{0x73, 0xf0},
{0xa2, 0x02},
{0x09, 0x00},
{0x7a, 0x18},
{0x7b, 0x04},
{0x7c, 0x09},
{0x7d, 0x18},
{0x7e, 0x38},
{0x7f, 0x47},
{0x80, 0x56},
{0x81, 0x66},
{0x82, 0x74},
{0x83, 0x7f},
{0x84, 0x89},
{0x85, 0x9a},
{0x86, 0xA9},
{0x87, 0xC4},
{0x88, 0xDb},
{0x89, 0xEe},
{0x13, 0xe0},
{0x01, 0x50},
{0x02, 0x68},
{0x00, 0x00},

{0x10, 0x00},
{0x0d, 0x40},
{0x14, 0x18},
{0xa5, 0x05},   // 07
{0xab, 0x06},  //08
{0x24, 0x40},
{0x25, 0x38},
{0x26, 0x81},
{0x9f, 0x78},
{0xa0, 0x68},
{0xa1, 0x03},


{0xa6, 0xd8},
{0xa7, 0xd8},
{0xa8, 0xf0},
{0xa9, 0x90},
{0xaa, 0x14},
{0x13, 0xe5},
{0x0e, 0x61},
{0x0f, 0x4b},
{0x16, 0x02},
{0x1e, 0x27},
{0x21, 0x02},
{0x22, 0x91},
{0x29, 0x07},
{0x33, 0x0b},
{0x35, 0x0b},
{0x37, 0x1d},
{0x38, 0x71},
{0x39, 0x2a},
{0x3c, 0x78},
{0x4d, 0x40},
{0x4e, 0x20},
{0x69, 0x00},
{0x6b, 0x0a},
{0x74, 0x10},
{0x8d, 0x4f},
{0x8e, 0x00},
{0x8f, 0x00},
{0x90, 0x00},
{0x91, 0x00},
{0x92, 0x1a}, //1a
{0x2b, 0x00},
{0x96, 0x00},
{0x9a, 0x80},
{0xb0, 0x84},
{0xb1, 0x0c},
{0xb2, 0x0e},

{0xb3, 0x82},
{0xb8, 0x12},
{0x43, 0x14},
{0x44, 0xf0},
{0x45, 0x41},
{0x46, 0x66},
{0x47, 0x2a},
{0x48, 0x3e},
{0x59, 0x8d},
{0x5a, 0x8e},
{0x5b, 0x53},
{0x5c, 0x83},
{0x5d, 0x4f},
{0x5e, 0x0e},
{0x6c, 0x0a},
{0x6d, 0x55},
{0x6e, 0x11},
{0x6f, 0x9e},
{0x62, 0x90},
{0x63, 0x30},
{0x64, 0x11},
{0x65, 0x00},
{0x66, 0x05},
{0x94, 0x11},
{0x95, 0x18},
{0x6a, 0x40},
{0x01, 0x40},
{0x02, 0x40},
{0x13, 0xe7},
{0x4f, 0x80},
{0x50, 0x80},
{0x51, 0x00},
{0x52, 0x22},
{0x53, 0x5e},
{0x54, 0x80},
{0x58, 0x9e},
{0x41, 0x08},
{0x3f, 0x00},
{0x75, 0x22},
{0x76, 0xe1},
{0x4c, 0x00},
{0x77, 0x03},
{0x3d, 0xc3},
{0x55, 0x10},

{0x4b, 0x09},
{0xc9, 0x60},
{0x41, 0x38},
{0x56, 0x40},
{0x34, 0x11},
{0x3b, 0x0a},
{0xa4, 0x88},
{0x96, 0x00},
{0x97, 0x30},
{0x98, 0x20},
{0x99, 0x30},
{0x9a, 0x84},
{0x9b, 0x29},
{0x9c, 0x03},
{0x9d, 0x4c}, //98
{0x9e, 0x3f},
{0xa5, 0x06}, //98
{0xab, 0x07},
{0x78, 0x04},
{0x79, 0x01},
{0xc8, 0xf0},
{0x79, 0x0f},
{0xc8, 0x00},
{0x79, 0x10},
{0xc8, 0x7e},
{0x79, 0x0a},
{0xc8, 0x80},
{0x79, 0x0b},
{0xc8, 0x01},
{0x79, 0x0c},
{0xc8, 0x0f},
{0x79, 0x0d},
{0xc8, 0x20},
{0x79, 0x09},
{0xc8, 0x80},
{0x79, 0x02},
{0xc8, 0xc0},
{0x79, 0x03},
{0xc8, 0x40},
{0x79, 0x05},
{0xc8, 0x30},
{0x79, 0x26},
{0x2d, 0x00},
{0x2e, 0x00},

	{0xff,0xff},
};

static int set_flip(struct ov7675_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	unsigned char buf[2];
	temp = i2c_get_byte_add8(client, 0x1e);
	temp &= 0xcf;
	temp |= dev->cam_info.m_flip << 5;
	temp |= dev->cam_info.v_flip << 4;
	buf[0] = 0x1e;
	buf[1] = temp;
	if((i2c_put_byte_add8(client,buf, 2)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
	}
	return 0;
}

//load GT2005 parameters
static void OV7675_init_regs(struct ov7675_device *dev)
{
    int i=0;//,j;
    unsigned char buf[2];
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    while(1)
    {
        buf[0] = OV7675_script[i].addr;//(unsigned char)((OV7675_script[i].addr >> 8) & 0xff);
        //buf[1] = (unsigned char)(OV7675_script[i].addr & 0xff);
	    buf[1] = OV7675_script[i].val;
		i++;
	 if (OV7675_script[i].val==0xff&&OV7675_script[i].addr==0xff)
	 	{
 	    	printk("OV7675_write_regs success in initial OV7675.\n");
	 	break;
	 	}
        if((i2c_put_byte_add8(client,buf, 2)) < 0)
        	{
    	    	printk("fail in initial OV7675.i=%d \n",i);
		return;
        	}
		if(i==1)
			msleep(40);
    }
	//unsigned char  temp_reg;
	//temp_reg=ov7675_read_byte(0x22);
	//buf[0]=0x13;
	//buf[1]=0;
	//temp_reg=i2c_get_byte_add8(client,buf);
	//printk(" camera set_OV7675_param_wb=%d. \n ",temp_reg);
	/*msleep(40);
	unsigned char  temp_reg;
	i=0;
	while(1)
		{
			buf[0] = OV7675_script[i].addr;//(unsigned char)((OV7675_script[i].addr >> 8) & 0xff);
			//buf[1] = (unsigned char)(OV7675_script[i].addr & 0xff);
			buf[1] = OV7675_script[i].val;
			i++;
		 if (OV7675_script[i].val==0xff&&OV7675_script[i].addr==0xff)
			{
				printk("OV7675_read_regs success in initial OV7675.\n");
			break;
			}
		 temp_reg=i2c_get_byte_add8(client,buf);
			if(temp_reg < 0)
				{
					printk("fail in read OV7675.i=%d \n",i);
			return;
				}
			else{
				printk("read OV7675.add=%x,val=%x \n",buf[0],temp_reg);
				}
			//if(i==1)
				//msleep(40);
		}*/
    set_flip(dev);
    return;

}

/*void OV7675_init_regs(struct ov7675_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    int i=0;

    while(1)
    {
        if (OV7675_script[i].val==0xff&&OV7675_script[i].addr==0xff)
        {
        	//printk("GT2005_write_regs success in initial GT2005.\n");
        	break;
        }
        if((i2c_put_byte(client,OV7675_script[i].addr, OV7675_script[i].val)) < 0)
        {
        	printk("fail in initial OV7675. \n");
		break;
		}
		i++;
    }

    return;
}
*/
#endif

/*************************************************************************
* FUNCTION
*	set_OV7675_param_wb
*
* DESCRIPTION
*	OV7675 wb setting.
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
void set_OV7675_param_wb(struct ov7675_device *dev,enum  camera_wb_flip_e para)
{
//	kal_uint16 rgain=0x80, ggain=0x80, bgain=0x80;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	unsigned char buf[4];

	unsigned char  temp_reg;
	//temp_reg=ov7675_read_byte(0x22);
	buf[0]=0x13;
	buf[1]=0;
	temp_reg=i2c_get_byte_add8(client, buf[0]);
	temp_reg=0xe7;
	printk(" camera set_OV7675_param_wb=%d. \n ",temp_reg);
	switch (para)
	{
		case CAM_WB_AUTO:
			buf[0]=0x13;
			buf[1]=temp_reg|0x2;
			i2c_put_byte_add8(client,buf,2);   // Enable AWB
			break;

		case CAM_WB_CLOUD:
			buf[0]=0x6a;
			buf[1]=0x40;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x13;
			buf[1]=temp_reg&(~0x2);
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x01;
			buf[1]=0x52;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x02;
			buf[1]=0x6c;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_WB_DAYLIGHT:   // tai yang guang
		    buf[0]=0x6a;
			buf[1]=0x40;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x13;
			buf[1]=temp_reg&(~0x2);
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x01;
			buf[1]=0x52;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x02;
			buf[1]=0x66;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_WB_INCANDESCENCE:   // bai re guang
		    buf[0]=0x6a;
			buf[1]=0x40;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x13;
			buf[1]=temp_reg&(~0x2);
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x01;
			buf[1]=0x8c;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x02;
			buf[1]=0x59;
			i2c_put_byte_add8(client,buf,2);

			break;

		case CAM_WB_FLUORESCENT:   //ri guang deng
		    buf[0]=0x6a;
			buf[1]=0x40;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x13;
			buf[1]=temp_reg&(~0x2);
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x01;
			buf[1]=0x7e;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x02;
			buf[1]=0x49;
			i2c_put_byte_add8(client,buf,2);
			break;

		case CAM_WB_TUNGSTEN:   // wu si deng
		    buf[0]=0x6a;
			buf[1]=0x40;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x13;
			buf[1]=temp_reg&(~0x2);
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x01;
			buf[1]=0x90;
			i2c_put_byte_add8(client,buf,2);
			buf[0]=0x02;
			buf[1]=0x3D;
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
*	OV7675_night_mode
*
* DESCRIPTION
*	This function night mode of OV7675.
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
void OV7675_night_mode(struct ov7675_device *dev,enum  camera_night_mode_flip_e enable)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];

	unsigned char  temp_reg;
	//temp_reg=ov7675_read_byte(0x22);
	//buf[0]=0x20;
	temp_reg=i2c_get_byte_add8(client,0x3b);
	//int temp;

    if(enable)
    {
		temp_reg = temp_reg | 0xc0;
		buf[0]=0x3b;
		buf[1]=temp_reg;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x14;
		buf[1]=0x38;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x2d;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x2e;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
     }
    else
     {
		temp_reg = temp_reg | 0xa0;
		buf[0]=0x3b;
		buf[1]=temp_reg;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x14;
		buf[1]=0x28;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x2d;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
		buf[0]=0x2e;
		buf[1]=0x00;
		i2c_put_byte_add8(client,buf,2);
	}

}
/*************************************************************************
* FUNCTION
*	OV7675_night_mode
*
* DESCRIPTION
*	This function night mode of OV7675.
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

void OV7675_set_param_banding(struct ov7675_device *dev,enum  camera_banding_flip_e banding)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//unsigned char buf[4];
	//int temp;
	switch(banding)
		{
		case CAM_BANDING_60HZ:
			/*
			temp = i2c_get_byte(client, 0x3b);
			temp = temp & 0xf7; // banding 60, bit[3] = 0
			i2c_put_byte(client, 0x3b, temp);
			i2c_put_byte_add8(client,0x9d,0x3f);
			i2c_put_byte_add8(client,0xa5,0x06);
			*/
			break;
		case CAM_BANDING_50HZ:
			/*
			temp = i2c_get_byte(client, 0x3b);
			temp = temp | 0x08; // banding 50, bit[3] = 1
			i2c_put_byte(client, 0x3b, temp);
			i2c_put_byte_add8(client,0x9e,0x4c);
			i2c_put_byte_add8(client,0xab,0x05);
			*/
			break;
		default:
			break;

		}

}


/*************************************************************************
* FUNCTION
*	set_OV7675_param_exposure
*
* DESCRIPTION
*	OV7675 exposure setting.
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
void set_OV7675_param_exposure(struct ov7675_device *dev,enum camera_exposure_e para)//曝光调节
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	unsigned char buf[4];

	switch (para)
	{
		case EXPOSURE_N4_STEP:
			buf[0]=0x55;
		    buf[1]=0xb0;
			break;

		case EXPOSURE_N3_STEP:
			buf[0]=0x55;
		    buf[1]=0xa0;
			break;

		case EXPOSURE_N2_STEP:
			buf[0]=0x55;
		    buf[1]=0x90;
			break;

		case EXPOSURE_N1_STEP:
			buf[0]=0x55;
		    buf[1]=0x00;
			break;

		case EXPOSURE_0_STEP:
			buf[0]=0x55;
		    buf[1]=0x10;
			break;

		case EXPOSURE_P1_STEP:
			buf[0]=0x55;
		    buf[1]=0x20;
			break;

		case EXPOSURE_P2_STEP:
			buf[0]=0x55;
		    buf[1]=0x30;
			break;

		case EXPOSURE_P3_STEP:
			buf[0]=0x55;
		    buf[1]=0x40;
			break;

		case EXPOSURE_P4_STEP:
			buf[0]=0x55;
		    buf[1]=0x50;
			break;

		default:
			buf[0]=0x55;
		    buf[1]=0x10;
			break;
	}
	i2c_put_byte_add8(client,buf,2);

}

/*************************************************************************
* FUNCTION
*	set_OV7675_param_effect
*
* DESCRIPTION
*	OV7675 effect setting.
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
void set_OV7675_param_effect(struct ov7675_device *dev,enum camera_effect_flip_e para)//特效设置
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	switch (para)
	{
		case CAM_EFFECT_ENC_NORMAL:
			buf[0]=0x3a;
		    buf[1]=0x04;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
		    buf[1]=0xc0;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);
			break;
		case CAM_EFFECT_ENC_GRAYSCALE:
			buf[0]=0x3a;
		    buf[1]=0x04;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
		    buf[1]=0xc0;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);

			break;
		case CAM_EFFECT_ENC_SEPIA:
			buf[0]=0x3a;
		    buf[1]=0x14;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
		    buf[1]=0x40;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
		    buf[1]=0xa0;
		    i2c_put_byte_add8(client,buf,2);
			break;
		case CAM_EFFECT_ENC_COLORINV:
			buf[0]=0x3a;
		    buf[1]=0x24;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);
			break;
		case CAM_EFFECT_ENC_SEPIAGREEN:
			buf[0]=0x3a;
		    buf[1]=0x04;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
		    buf[1]=0xc0;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);
			break;
		case CAM_EFFECT_ENC_SEPIABLUE:
			buf[0]=0x3a;
		    buf[1]=0x04;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x67;
		    buf[1]=0xc0;
		    i2c_put_byte_add8(client,buf,2);
			buf[0]=0x68;
		    buf[1]=0x80;
		    i2c_put_byte_add8(client,buf,2);

			break;
		default:
			break;
	}

}

unsigned char v4l_2_ov7675(int val)
{
	int ret=val/0x20;
	if(ret<4) return ret*0x20+0x80;
	else if(ret<8) return ret*0x20+0x20;
	else return 0;
}

static int ov7675_setting(struct ov7675_device *dev,int PROP_ID,int value )
{
	int ret=0;
	//unsigned char cur_val;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_ov7675(value));
		ret=i2c_put_byte(client,0x0201,v4l_2_ov7675(value));
		break;
	case V4L2_CID_CONTRAST:
		ret=i2c_put_byte(client,0x0200, value);
		break;
	case V4L2_CID_SATURATION:
		ret=i2c_put_byte(client,0x0202, value);
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		if(ov7675_qctrl[0].default_value!=value){
			ov7675_qctrl[0].default_value=value;
			set_OV7675_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
        	}
		break;
	case V4L2_CID_EXPOSURE:
		if(ov7675_qctrl[1].default_value!=value){
			ov7675_qctrl[1].default_value=value;
			set_OV7675_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
        	}
		break;
	case V4L2_CID_COLORFX:
		if(ov7675_qctrl[2].default_value!=value){
			ov7675_qctrl[2].default_value=value;
			set_OV7675_param_effect(dev,value);
			printk(KERN_INFO " set camera  effect=%d. \n ",value);
        	}
		break;
	case V4L2_CID_WHITENESS:
		 if(ov7675_qctrl[3].default_value!=value){
			ov7675_qctrl[3].default_value=value;
			OV7675_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
        	}
		break;
	case V4L2_CID_HFLIP:
		value = value & 0x3;
		if(ov7675_qctrl[4].default_value!=value){
			ov7675_qctrl[4].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
        	}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */         
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(ov7675_qctrl[6].default_value!=value){
			ov7675_qctrl[6].default_value=value;
			//printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
        	}
		break;
	case V4L2_CID_ROTATE:
		 if(ov7675_qctrl[7].default_value!=value){
			ov7675_qctrl[7].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
        	}
		break;
	default:
		ret=-1;
		break;
	}
	return ret;

}

static void power_down_ov7675(struct ov7675_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char buf[4];
	buf[0]=0x12;
	buf[1]=0x80;
	i2c_put_byte_add8(client,buf,2);
	msleep(5);
	buf[0]=0xb8;
	buf[1]=0x12;
	i2c_put_byte_add8(client,buf,2);
	msleep(1);
	return;
}

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void ov7675_fillbuff(struct ov7675_fh *fh, struct ov7675_buffer *buf)
{
	struct ov7675_device *dev = fh->dev;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	para.mirror = ov7675_qctrl[4].default_value&3;
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = 0x18221223;
	para.zoom = ov7675_qctrl[6].default_value;
	para.angle = ov7675_qctrl[7].default_value;
	para.vaddr = (unsigned)vbuf;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void ov7675_thread_tick(struct ov7675_fh *fh)
{
	struct ov7675_buffer *buf;
	struct ov7675_device *dev = fh->dev;
	struct ov7675_dmaqueue *dma_q = &dev->vidq;

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
			 struct ov7675_buffer, vb.queue);
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
	ov7675_fillbuff(fh, buf);
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

static void ov7675_sleep(struct ov7675_fh *fh)
{
	struct ov7675_device *dev = fh->dev;
	struct ov7675_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	ov7675_thread_tick(fh);

	schedule_timeout_interruptible(2);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int ov7675_thread(void *data)
{
	struct ov7675_fh  *fh = data;
	struct ov7675_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		ov7675_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int ov7675_start_thread(struct ov7675_fh *fh)
{
	struct ov7675_device *dev = fh->dev;
	struct ov7675_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(ov7675_thread, fh, "ov7675");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void ov7675_stop_thread(struct ov7675_dmaqueue  *dma_q)
{
	struct ov7675_device *dev = container_of(dma_q, struct ov7675_device, vidq);

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
	struct ov7675_fh  *fh = vq->priv_data;
	struct ov7675_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct ov7675_buffer *buf)
{
	struct ov7675_fh  *fh = vq->priv_data;
	struct ov7675_device *dev  = fh->dev;

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
	struct ov7675_fh     *fh  = vq->priv_data;
	struct ov7675_device    *dev = fh->dev;
	struct ov7675_buffer *buf = container_of(vb, struct ov7675_buffer, vb);
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
	struct ov7675_buffer    *buf  = container_of(vb, struct ov7675_buffer, vb);
	struct ov7675_fh        *fh   = vq->priv_data;
	struct ov7675_device       *dev  = fh->dev;
	struct ov7675_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct ov7675_buffer   *buf  = container_of(vb, struct ov7675_buffer, vb);
	struct ov7675_fh       *fh   = vq->priv_data;
	struct ov7675_device      *dev  = (struct ov7675_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops ov7675_video_qops = {
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
	struct ov7675_fh  *fh  = priv;
	struct ov7675_device *dev = fh->dev;

	strcpy(cap->driver, "ov7675");
	strcpy(cap->card, "ov7675");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = OV7675_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct ov7675_fmt *fmt;

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

    if(fival->index > ARRAY_SIZE(ov7675_frmivalenum))
        return -EINVAL;

    for(k =0; k< ARRAY_SIZE(ov7675_frmivalenum); k++)
    {
        if( (fival->index==ov7675_frmivalenum[k].index)&&
                (fival->pixel_format ==ov7675_frmivalenum[k].pixel_format )&&
                (fival->width==ov7675_frmivalenum[k].width)&&
                (fival->height==ov7675_frmivalenum[k].height)){
            memcpy( fival, &ov7675_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
            return 0;
        }
    }

    return -EINVAL;

}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct ov7675_fh *fh = priv;

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
	struct ov7675_fh  *fh  = priv;
	struct ov7675_device *dev = fh->dev;
	struct ov7675_fmt *fmt;
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
	struct ov7675_fh *fh = priv;
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

	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return ret;
}

static int vidioc_g_parm(struct file *file, void *priv,
        struct v4l2_streamparm *parms)
{
    struct ov7675_fh *fh = priv;
    struct ov7675_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = ov7675_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct ov7675_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov7675_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov7675_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ov7675_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct ov7675_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct ov7675_fh  *fh = priv;
	vdin_parm_t para;
	int ret = 0 ;
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof(para));
	para.port  = TVIN_PORT_CAMERA;

	para.fmt = TVIN_SIG_FMT_MAX;//TVIN_SIG_FMT_MAX+1;TVIN_SIG_FMT_CAMERA_1280X720P_30Hz
	para.frame_rate = ov7675_frmintervals_active.denominator;
	para.h_active = 640;
	para.v_active = 480;
	para.hsync_phase = 0;
	para.vsync_phase = 1;
	para.hs_bp = 0;
	para.vs_bp = 2;
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
	struct ov7675_fh  *fh = priv;

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
	struct ov7675_fmt *fmt = NULL;
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
		if (fsize->index >= ARRAY_SIZE(ov7675_prev_resolution))
			return -EINVAL;
		frmsize = &ov7675_prev_resolution[fsize->index];
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = frmsize->width;
		fsize->discrete.height = frmsize->height;
	}
	else if(fmt->fourcc == V4L2_PIX_FMT_RGB24){
		if (fsize->index >= ARRAY_SIZE(ov7675_pic_resolution))
			return -EINVAL;
		frmsize = &ov7675_pic_resolution[fsize->index];
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
	struct ov7675_fh *fh = priv;
	struct ov7675_device *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct ov7675_fh *fh = priv;
	struct ov7675_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(ov7675_qctrl); i++)
		if (qc->id && qc->id == ov7675_qctrl[i].id) {
			memcpy(qc, &(ov7675_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct ov7675_fh *fh = priv;
	struct ov7675_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(ov7675_qctrl); i++)
		if (ctrl->id == ov7675_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct ov7675_fh *fh = priv;
	struct ov7675_device *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(ov7675_qctrl); i++)
		if (ctrl->id == ov7675_qctrl[i].id) {
			if (ctrl->value < ov7675_qctrl[i].minimum ||
			    ctrl->value > ov7675_qctrl[i].maximum ||
			    ov7675_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int ov7675_open(struct file *file)
{
	struct ov7675_device *dev = video_drvdata(file);
	struct ov7675_fh *fh = NULL;
	int retval = 0;

#if CONFIG_CMA
    retval = vm_init_buf(16*SZ_1M);
    if(retval <0)
        return -1;
#endif
	ov7675_have_opened=1;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("ge2d", 1);
#endif
       aml_cam_init(&dev->cam_info);
	OV7675_init_regs(dev);
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

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &ov7675_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct ov7675_buffer), fh,NULL);

	ov7675_start_thread(fh);

	return 0;
}

static ssize_t
ov7675_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct ov7675_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
ov7675_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ov7675_fh        *fh = file->private_data;
	struct ov7675_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int ov7675_close(struct file *file)
{
	struct ov7675_fh         *fh = file->private_data;
	struct ov7675_device *dev       = fh->dev;
	struct ov7675_dmaqueue *vidq = &dev->vidq;
	struct video_device  *vdev = video_devdata(file);
	ov7675_have_opened=0;

	ov7675_stop_thread(vidq);
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
	ov7675_qctrl[0].default_value=0;
	ov7675_qctrl[1].default_value=4;
	ov7675_qctrl[2].default_value=0;
	ov7675_qctrl[3].default_value=0;
	
	ov7675_qctrl[4].default_value=0;
	ov7675_qctrl[6].default_value=100;
	ov7675_qctrl[7].default_value=0;
       ov7675_frmintervals_active.numerator = 1;
       ov7675_frmintervals_active.denominator = 15;
	power_down_ov7675(dev);
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

static int ov7675_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ov7675_fh  *fh = file->private_data;
	struct ov7675_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations ov7675_fops = {
	.owner		= THIS_MODULE,
	.open           = ov7675_open,
	.release        = ov7675_close,
	.read           = ov7675_read,
	.poll		= ov7675_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = ov7675_mmap,
};

static const struct v4l2_ioctl_ops ov7675_ioctl_ops = {
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

static struct video_device ov7675_template = {
	.name		= "ov7675_v4l",
	.fops           = &ov7675_fops,
	.ioctl_ops 	= &ov7675_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int ov7675_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_OV7675, 0);
}

static const struct v4l2_subdev_core_ops ov7675_core_ops = {
	.g_chip_ident = ov7675_g_chip_ident,
};

static const struct v4l2_subdev_ops ov7675_ops = {
	.core = &ov7675_core_ops,
};

static int ov7675_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{

	int err;
	struct ov7675_device *t;
	struct v4l2_subdev *sd;
	aml_cam_info_t* plat_dat;
	vops = get_vdin_v4l2_ops();
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &ov7675_ops);
	mutex_init(&t->mutex);
	this_client=client;

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	memcpy(t->vdev, &ov7675_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);

	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "ov7675");
	/* Register it */
	plat_dat= (aml_cam_info_t*)client->dev.platform_data;
	if (plat_dat) {
	    memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
	    if (plat_dat->front_back >=0)  
	   	video_nr = plat_dat->front_back;
	} else {
	    printk("camera ov7675: have no platform data\n");
	    kfree(t);
        return -1; 
	}
	
	t->cam_info.version = OV7675_DRIVER_VERSION;
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

static int ov7675_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov7675_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id ov7675_id[] = {
	{ "ov7675", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov7675_id);

static struct i2c_driver ov7675_i2c_driver = {
	.driver = {
		.name = "ov7675",
	},
	.probe = ov7675_probe,
	.remove = ov7675_remove,
	.id_table = ov7675_id,
};

module_i2c_driver(ov7675_i2c_driver);

