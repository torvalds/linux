/*
 *ov5647 - This code emulates a real video device with v4l2 api
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

#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>
#include "../ionvideo/videobuf2-ion.h"///to be replace
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <linux/wakelock.h>
#include <linux/vmalloc.h>

#include <linux/i2c.h>
#include <media/v4l2-chip-ident.h>

#include <linux/amlogic/mipi/am_mipi_csi2.h>
#include <linux/amlogic/camera/aml_cam_info.h>
#include <linux/amlogic/vmapi.h>
#include "common/vm.h"

#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <mach/gpio.h>
//#include <media/amlogic/656in.h>
#include "common/plat_ctrl.h"
#include <linux/amlogic/tvin/tvin_v4l2.h>
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
//static struct early_suspend ov5647_early_suspend;
#endif

#include "common/config_parser.h"

#define OV5647_CAMERA_MODULE_NAME "ov5647"
#define MAGIC_RE_MEM 0x123039dc
#define OV5647_RES0_CANVAS_INDEX CAMERA_USER_CANVAS_INDEX

static int capture_proc = 0;

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define OV5647_CAMERA_MAJOR_VERSION 0
#define OV5647_CAMERA_MINOR_VERSION 7
#define OV5647_CAMERA_RELEASE 0
#define OV5647_CAMERA_VERSION \
	KERNEL_VERSION(OV5647_CAMERA_MAJOR_VERSION, OV5647_CAMERA_MINOR_VERSION, OV5647_CAMERA_RELEASE)

#define OV5647_DRIVER_VERSION "OV5647-COMMON-01-140717"

MODULE_DESCRIPTION("ov5647 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 32;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static unsigned int vcm_mod = 0;
module_param(vcm_mod,uint,0664);
MODULE_PARM_DESC(vcm_mod,"\n vcm_mod,1:DLC;0:LSC.\n");

static int ov5647_h_active=800;
static int ov5647_v_active=600;
static struct v4l2_fract ov5647_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

static int ov5647_have_open=0;
static camera_mode_t ov5647_work_mode = CAMERA_PREVIEW;
static struct class *cam_class;
static unsigned int g_ae_manual_exp;
static unsigned int g_ae_manual_ag;
static unsigned int g_ae_manual_vts;
//static unsigned int exp_mode;
//static unsigned int change_cnt;
static unsigned int current_fmt;
static unsigned int current_fr = 0;//50 hz
static unsigned int aet_index;
static unsigned int last_af_step = 0;
static int i_index = -1;
static int t_index = -1;
static int dest_hactive = 640;
static int dest_vactive = 480;
static bool bDoingAutoFocusMode = false;
static unsigned char last_exp_h = 0, last_exp_m = 0, last_exp_l = 0, last_ag_h = 0, last_ag_l = 0, last_vts_h = 0, last_vts_l = 0;
static configure_t *cf;
/* supported controls */
static struct v4l2_queryctrl ov5647_qctrl[] = {
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 127,
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
	} ,{
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
		.id            = V4L2_CID_FOCUS_AUTO,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "auto focus",
		.minimum       = CAM_FOCUS_MODE_RELEASE,
		.maximum       = CAM_FOCUS_MODE_CONTI_PIC,
		.step          = 0x1,
		.default_value = CAM_FOCUS_MODE_CONTI_PIC,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	},{
		.id            = V4L2_CID_BACKLIGHT_COMPENSATION,
		.type          = V4L2_CTRL_TYPE_MENU,
		.name          = "flash",
		.minimum       = FLASHLIGHT_ON,
		.maximum       = FLASHLIGHT_TORCH,
		.step          = 0x1,
		.default_value = FLASHLIGHT_OFF,
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
    },{
        .id            = V4L2_CID_AUTO_FOCUS_STATUS,
            .type          = 8,//V4L2_CTRL_TYPE_BITMASK,
            .name          = "focus status",
            .minimum       = 0,
            .maximum       = ~3,
            .step          = 0x1,
            .default_value = V4L2_AUTO_FOCUS_STATUS_IDLE,
            .flags         = V4L2_CTRL_FLAG_READ_ONLY,
    },{
        .id		= V4L2_CID_FOCUS_ABSOLUTE,
            .type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "focus center",
		.minimum	= 0,
		.maximum	= ((2000) << 16) | 2000,
		.step		= 1,
		.default_value	= (1000 << 16) | 1000,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
 	}
};

static struct v4l2_frmivalenum ov5647_frmivalenum[]={
        {
                .index 		= 0,
                .pixel_format	= V4L2_PIX_FMT_NV21,
                .width		= 640,
                .height		= 480,
                .type		= V4L2_FRMIVAL_TYPE_DISCRETE,
                {
                        .discrete	={
                                .numerator	= 1,
                                .denominator	= 30,
                        }
                }
        },{
                .index 		= 0,
                .pixel_format	= V4L2_PIX_FMT_NV21,
                .width		= 1280,
                .height		= 720,
                .type		= V4L2_FRMIVAL_TYPE_DISCRETE,
                {
                        .discrete	={
                                .numerator	= 1,
                                .denominator	= 30,
                        }
                }
        },{
                .index 		= 0,
                .pixel_format	= V4L2_PIX_FMT_NV21,
                .width		= 1280,
                .height		= 960,
                .type		= V4L2_FRMIVAL_TYPE_DISCRETE,
                {
                        .discrete	={
                                .numerator	= 1,
                                .denominator	= 30,
                        }
                }
        },{
                .index 		= 0,
                .pixel_format	= V4L2_PIX_FMT_NV21,
                .width		= 2048,
                .height		= 1536,
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
                .width		= 1920,
                .height		= 1080,
                .type		= V4L2_FRMIVAL_TYPE_DISCRETE,
                {
                        .discrete	={
                                .numerator	= 1,
                                .denominator	= 30,
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
                                .denominator	= 15,
                        }
                }
        },
};

struct v4l2_querymenu ov5647_qmenu_wbmode[] = {
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
        .index      = CAM_WB_DAYLIGHT,
        .name       = "daylight",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_INCANDESCENCE,
        .name       = "incandescent",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_TUNGSTEN,
        .name       = "tungsten",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_FLUORESCENT,
        .name       = "fluorescent", 
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_MANUAL,
        .name       = "manual", 
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_SHADE,
        .name       = "shade", 
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_TWILIGHT,
        .name       = "twilight", 
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_WARM_FLUORESCENT,
        .name       = "warm-fluorescent",
        .reserved   = 0,
    },
};

struct v4l2_querymenu ov5647_qmenu_autofocus[] = {
    {
        .id         = V4L2_CID_FOCUS_AUTO,
        .index      = CAM_FOCUS_MODE_INFINITY,
        .name       = "infinity",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_FOCUS_AUTO,
        .index      = CAM_FOCUS_MODE_AUTO,
        .name       = "auto",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_FOCUS_AUTO,
        .index      = CAM_FOCUS_MODE_CONTI_VID,
        .name       = "continuous-video",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_FOCUS_AUTO,
        .index      = CAM_FOCUS_MODE_CONTI_PIC,
        .name       = "continuous-picture",
        .reserved   = 0,
    }
};

struct v4l2_querymenu ov5647_qmenu_anti_banding_mode[] = {
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

struct v4l2_querymenu ov5647_qmenu_flashmode[] = {
    {
        .id         = V4L2_CID_BACKLIGHT_COMPENSATION,
        .index      = FLASHLIGHT_ON,
        .name       = "on",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_BACKLIGHT_COMPENSATION,
        .index      = FLASHLIGHT_OFF,
        .name       = "off",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_BACKLIGHT_COMPENSATION,
        .index      = FLASHLIGHT_TORCH,
        .name       = "torch",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_BACKLIGHT_COMPENSATION,
        .index      = FLASHLIGHT_AUTO,
        .name       = "auto",
        .reserved   = 0,
    }
};

typedef struct {
    __u32   id;
    int     num;
    struct v4l2_querymenu* ov5647_qmenu;
}ov5647_qmenu_set_t;

ov5647_qmenu_set_t ov5647_qmenu_set[] = {
    {
        .id         	= V4L2_CID_DO_WHITE_BALANCE,
        .num            = ARRAY_SIZE(ov5647_qmenu_wbmode),
        .ov5647_qmenu   = ov5647_qmenu_wbmode,
    },{
        .id         	= V4L2_CID_POWER_LINE_FREQUENCY,
        .num            = ARRAY_SIZE(ov5647_qmenu_anti_banding_mode),
        .ov5647_qmenu   = ov5647_qmenu_anti_banding_mode,
    }, {
        .id             = V4L2_CID_FOCUS_AUTO,
        .num            = ARRAY_SIZE(ov5647_qmenu_autofocus),
        .ov5647_qmenu   = ov5647_qmenu_autofocus,
    }, {
        .id             = V4L2_CID_BACKLIGHT_COMPENSATION,
        .num            = ARRAY_SIZE(ov5647_qmenu_flashmode),
        .ov5647_qmenu   = ov5647_qmenu_flashmode,
    }
};

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(ov5647_qmenu_set); i++)
	if (a->id && a->id == ov5647_qmenu_set[i].id) {
	    for(j = 0; j < ov5647_qmenu_set[i].num; j++)
		if (a->index == ov5647_qmenu_set[i].ov5647_qmenu[j].index) {
			memcpy(a, &( ov5647_qmenu_set[i].ov5647_qmenu[j]),
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

struct ov5647_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct ov5647_fmt formats[] = {
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

static const struct ov5647_fmt *__get_format(u32 pixelfmt)
{
	const struct ov5647_fmt *fmt;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(formats); k++) {
		fmt = &formats[k];
		if (fmt->fourcc == pixelfmt)
			break;
	}

	if (k == ARRAY_SIZE(formats))
		return NULL;

	return &formats[k];
}
static const struct ov5647_fmt *get_format(struct v4l2_format *f)
{
	return __get_format(f->fmt.pix.pixelformat);
};

/* buffer for one video frame */
struct ov5647_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_buffer 		vb;
	struct list_head		list;
	const struct ov5647_fmt        	*fmt;
	
	unsigned int canvas_id;
};

struct ov5647_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

typedef struct resolution_param {
	struct v4l2_frmsize_discrete frmsize;
	struct v4l2_frmsize_discrete active_frmsize;
	int active_fps;
	resolution_size_t size_type;
	struct aml_camera_i2c_fig_s *reg_script[2]; //0:dvp, 1:mipi
} resolution_param_t;

static LIST_HEAD(ov5647_devicelist);

struct ov5647_fh;

struct ov5647_device {
        struct list_head	ov5647_devicelist;
        struct v4l2_subdev	sd;
        struct v4l2_device	v4l2_dev;
        struct video_device     vdev;

        spinlock_t              slock;
        struct mutex		mutex;

        int                     users;


        struct ov5647_dmaqueue  vidq;

        /* Several counters */
        unsigned long           jiffies;

        /* Input Number */
        int	                input;

        /* platform device data from board initting. */
        aml_cam_info_t          cam_info;

        cam_parameter_t         *cam_para;

        para_index_t            pindex;

        struct vdin_v4l2_ops_s *vops;
        unsigned int            is_vdin_start;

        fe_arg_t                fe_arg;

        vdin_arg_t              vdin_arg;
        /* wake lock */
        struct wake_lock	wake_lock;
        /* ae status */
        bool                    ae_on;

        camera_priv_data_t      camera_priv_data;

        configure_t             *configure;
        /* Control 'registers' */
        int 		        qctl_regs[ARRAY_SIZE(ov5647_qctrl)];

        /* video capture */
        const struct ov5647_fmt *fmt;
        unsigned int            width, height;
        struct vb2_queue        vb_vidq;

        //struct videobuf_res_privdata res;
};

static inline struct ov5647_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov5647_device, sd);
}

/* ------------------------------------------------------------------
	reg spec of OV5647
   ------------------------------------------------------------------*/
static struct aml_camera_i2c_fig_s OV5647_script[] = {
	{0x0100,0x00}, // software standby        
	{0x0103,0x01}, // software reset          
	{0xffff, 0xff}
};

struct aml_camera_i2c_fig_s OV5647_VGA_script_mipi[] = {
          {0x4800, 0x24},
          {0x0100, 0x00},
          {0x0103, 0x01},
          {0x3035, 0x11},
          {0x3036, 0x46},
          {0x303c, 0x11},
          {0x3821, 0x07},
          {0x3820, 0x41},
          {0x370c, 0x0f},
          {0x3612, 0x59},
          {0x3618, 0x00},
          {0x5000, 0x06},

          {0x5000, 0x06},
          {0x5001, 0x00},
          {0x5002, 0x41},
          {0x5003, 0x08},
          {0x5a00, 0x08},
          {0x3000, 0xff},
          {0x3001, 0xff},
          {0x3002, 0xff},
          {0x301d, 0xf0},
          {0x3a18, 0x00},
          {0x3a19, 0xf8},
          {0x3c01, 0x80},
          {0x3b07, 0x0c},
          {0x3500, 0x00},
          {0x3501, 0x1c},
          {0x3502, 0x60},		  	
          {0x380c, 0x07},
          {0x380d, 0x3c},
          {0x380e, 0x03},
          {0x380f, 0xf0},
          {0x3814, 0x71},
          {0x3815, 0x71},
          {0x3708, 0x64},
          {0x3709, 0x52},
          {0x3808, 0x02},
          {0x3809, 0x80},
          {0x380a, 0x01},
          {0x380b, 0xe0},
          {0x3800, 0x00},
          {0x3801, 0x10},
          {0x3802, 0x00},
          {0x3803, 0x00},
          {0x3804, 0x0a},
          {0x3805, 0x2f},
          {0x3806, 0x07},
          {0x3807, 0x9f},
          {0x3630, 0x2e},
          {0x3632, 0xe2},
          {0x3633, 0x23},
          {0x3634, 0x44},
          {0x3620, 0x64},
          {0x3621, 0xe0},
          {0x3600, 0x37},
          {0x3704, 0xa0},
          {0x3703, 0x5a},
          {0x3715, 0x78},
          {0x3717, 0x01},
          {0x3731, 0x02},
          {0x370b, 0x60},
          {0x3705, 0x1a},
          {0x3f05, 0x02},
          {0x3f06, 0x10},
          {0x3f01, 0x0a},
          {0x3503, 0x03},
          {0x3a08, 0x01},
          {0x3a09, 0x2e},
          {0x3a0a, 0x00},
          {0x3a0b, 0xfb},
          {0x3a0d, 0x02},
          {0x3a0e, 0x01},
          {0x3a0f, 0x58},
          {0x3a10, 0x50},
          {0x3a1b, 0x58},
          {0x3a1e, 0x50},
          {0x3a11, 0x60},
          {0x3a1f, 0x28},
          {0x4001, 0x02},
          {0x4004, 0x02},
          {0x4000, 0x09},
          {0x4050, 0x6e},
          {0x4051, 0x8f},
          {0x0100, 0x01},
          {0x3000, 0x00},
          {0x3001, 0x00},
          {0x3002, 0x00},
          {0x3017, 0xe0},
          {0x301c, 0xfc},
          {0x3636, 0x06},
          {0x3016, 0x08},
          {0x3827, 0xec},
          {0x4800, 0x24},
          {0x3018, 0x44},
          {0x3035, 0x21},
          {0x3106, 0xf5},
          {0x3034, 0x1a},
          {0x301c, 0xf8},
          {0xffff, 0xff},
};
struct aml_camera_i2c_fig_s OV5647_preview_VGA_script[] = {
	{0x0100,0x00},
	{0x0103,0x01},
	{0x3035,0x11},
	{0x3036,0x46},
	{0x303c,0x11},
	{0x3821,0x07},
	{0x3820,0x41},
	{0x370c,0x0f},
	{0x3612,0x59},
	{0x3618,0x00},
	{0x5000,0x06},
	{0x5800,0x1b},
	{0x5801,0x0d},
	{0x5802,0x09},
	{0x5803,0x0a},
	{0x5804,0x0b},
	{0x5805,0x1c},
	{0x5806,0x07},
	{0x5807,0x05},
	{0x5808,0x03},
	{0x5809,0x03},
	{0x580a,0x05},
	{0x580b,0x07},
	{0x580c,0x05},
	{0x580d,0x02},
	{0x580e,0x01},
	{0x580f,0x01},
	{0x5810,0x02},
	{0x5811,0x05},
	{0x5812,0x06},
	{0x5813,0x02},
	{0x5814,0x01},
	{0x5815,0x01},
	{0x5816,0x02},
	{0x5817,0x06},
	{0x5818,0x07},
	{0x5819,0x05},
	{0x581a,0x03},
	{0x581b,0x03},
	{0x581c,0x05},
	{0x581d,0x06},
	{0x581e,0x1f},
	{0x581f,0x0d},
	{0x5820,0x0a},
	{0x5821,0x0a},
	{0x5822,0x0c},
	{0x5823,0x1e},
	{0x5824,0x0e},
	{0x5825,0x08},
	{0x5826,0x0a},
	{0x5827,0x18},
	{0x5828,0x2a},
	{0x5829,0x0a},
	{0x582a,0x26},
	{0x582b,0x24},
	{0x582c,0x26},
	{0x582d,0x28},
	{0x582e,0x0a},
	{0x582f,0x22},
	{0x5830,0x40},
	{0x5831,0x42},
	{0x5832,0x2a},
	{0x5833,0x0a},
	{0x5834,0x26},
	{0x5835,0x26},
	{0x5836,0x26},
	{0x5837,0x2a},
	{0x5838,0x2c},
	{0x5839,0x08},
	{0x583a,0x09},
	{0x583b,0x28},
	{0x583c,0x2c},
	{0x583d,0xce},
	{0x5000,0x06},
	{0x5001,0x00},
	{0x5002,0x41},			
	{0x5180,0x08},
	{0x5003,0x08},
	{0x5a00,0x08},
	{0x3000,0xff},
	{0x3001,0xff},
	{0x3002,0xff},
	{0x301d,0xf0},
	{0x3a18,0x00},
	{0x3a19,0xf8},
	{0x3c01,0x80},
	{0x3b07,0x0c},
	{0x3500, 0x00},
	{0x3501, 0x1c},
	{0x3502, 0x60},
	{0x380c,0x07},
	{0x380d,0x3c},
	{0x380e,0x01},
	{0x380f,0xf8},
	{0x3814,0x71},
	{0x3815,0x71},
	{0x3708,0x64},
	{0x3709,0x52},
	{0x3808,0x02},
	{0x3809,0x80},
	{0x380a,0x01},
	{0x380b,0xe0},
	{0x3800,0x00},
	{0x3801,0x10},
	{0x3802,0x00},
	{0x3803,0x00},
	{0x3804,0x0a},
	{0x3805,0x2f},
	{0x3806,0x07},
	{0x3807,0x9f},
	{0x3630,0x2e},
	{0x3632,0xe2},
	{0x3633,0x23},
	{0x3634,0x44},
	{0x3620,0x64},
	{0x3621,0xe0},
	{0x3600,0x37},
	{0x3704,0xa0},
	{0x3703,0x5a},
	{0x3715,0x78},
	{0x3717,0x01},
	{0x3731,0x02},
	{0x370b,0x60},
	{0x3705,0x1a},
	{0x3f05,0x02},
	{0x3f06,0x10},
	{0x3f01,0x0a},
	{0x3503,0x03},
	{0x3a08,0x01},
	{0x3a09,0x2e},
	{0x3a0a,0x00},
	{0x3a0b,0xfb},
	{0x3a0d,0x02},
	{0x3a0e,0x01},
	{0x3a0f,0x58},
	{0x3a10,0x50},
	{0x3a1b,0x58},
	{0x3a1e,0x50},
	{0x3a11,0x60},
	{0x3a1f,0x28},
	{0x4001,0x02},
	{0x4004,0x02},
	{0x4000,0x09},
	{0x4050,0x6e},
	{0x4051,0x8f},
	{0x0100,0x01},
	             
	{0x3035,0x21},
	{0x303c,0x12},
	{0x3a08,0x00},
	{0x3a09,0x97},
	{0x3a0a,0x00},
	{0x3a0b,0x7e},
	{0x3a0d,0x04},
	{0x3a0e,0x03},
	{0xffff,0xff},  
};

struct aml_camera_i2c_fig_s OV5647_720P_script_mipi[] = {
          {0x4800, 0x24},
          {0x0100, 0x00},
          {0x0103, 0x01},
          {0x3035, 0x11},
          {0x3036, 0x64},
          {0x303c, 0x11},
          {0x3821, 0x07},
          {0x3820, 0x41},
          {0x370c, 0x0f},
          {0x3612, 0x59},
          {0x3618, 0x00},
          {0x5000, 0x06},

          {0x5000, 0x06},
          {0x5001, 0x00},
          {0x5002, 0x41},

          {0x5003, 0x08},
          {0x5a00, 0x08},
          {0x3000, 0xff},
          {0x3001, 0xff},
          {0x3002, 0xff},
          {0x301d, 0xf0},
          {0x3a18, 0x00},
          {0x3a19, 0xf8},
          {0x3c01, 0x80},
          {0x3b07, 0x0c},
		  {0x3500, 0x00},
		  {0x3501, 0x29},
		  {0x3502, 0xe0},
          {0x380c, 0x07},
          {0x380d, 0x00},
          {0x380e, 0x05},
          {0x380f, 0xd0},
          {0x3814, 0x31},
          {0x3815, 0x31},
          {0x3708, 0x64},
          {0x3709, 0x52},
          {0x3808, 0x05},
          {0x3809, 0x00},
          {0x380a, 0x02},
          {0x380b, 0xd0},
          {0x3800, 0x00},
          {0x3801, 0x18},
          {0x3802, 0x00},
          {0x3803, 0xf8},
          {0x3804, 0x0a},
          {0x3805, 0x27},
          {0x3806, 0x06},
          {0x3807, 0xa7},
          {0x3630, 0x2e},
          {0x3632, 0xe2},
          {0x3633, 0x23},
          {0x3634, 0x44},
          {0x3620, 0x64},
          {0x3621, 0xe0},
          {0x3600, 0x37},
          {0x3704, 0xa0},
          {0x3703, 0x5a},
          {0x3715, 0x78},
          {0x3717, 0x01},
          {0x3731, 0x02},
          {0x370b, 0x60},
          {0x3705, 0x1a},
          {0x3f05, 0x02},
          {0x3f06, 0x10},
          {0x3f01, 0x0a},
          {0x3503, 0x03},
          {0x3a08, 0x01},
          {0x3a09, 0xbe},
          {0x3a0a, 0x01},
          {0x3a0b, 0x74},
          {0x3a0d, 0x02},
          {0x3a0e, 0x01},
          {0x3a0f, 0x58},
          {0x3a10, 0x50},
          {0x3a1b, 0x58},
          {0x3a1e, 0x50},
          {0x3a11, 0x60},
          {0x3a1f, 0x28},
          {0x4001, 0x02},
          {0x4004, 0x02},
          {0x4000, 0x09},
          {0x4050, 0x6e},
          {0x4051, 0x8f},
          {0x0100, 0x01},
          {0x3000, 0x00},
          {0x3001, 0x00},
          {0x3002, 0x00},
          {0x3017, 0xe0},
          {0x301c, 0xfc},
          {0x3636, 0x06},
          {0x3016, 0x08},
          {0x3827, 0xec},
          {0x4800, 0x24},
          {0x3018, 0x44},
          {0x3035, 0x21},
          {0x3106, 0xf5},
          {0x3034, 0x1a},
          {0x301c, 0xf8},
	  {0xffff, 0xff},
};
struct aml_camera_i2c_fig_s OV5647_preview_720P_script[] = {
	{0x0100,0x00},
	{0x0103,0x01},
	{0x3035,0x11},
	{0x3036,0x64},
	{0x303c,0x11},
	{0x3821,0x07},
	{0x3820,0x41},
	{0x370c,0x0f},
	{0x3612,0x59},
	{0x3618,0x00},
	{0x5000,0x06},
	{0x5800,0x1b},
	{0x5801,0x0d},
	{0x5802,0x09},
	{0x5803,0x0a},
	{0x5804,0x0b},
	{0x5805,0x1c},
	{0x5806,0x07},
	{0x5807,0x05},
	{0x5808,0x03},
	{0x5809,0x03},
	{0x580a,0x05},
	{0x580b,0x07},
	{0x580c,0x05},
	{0x580d,0x02},
	{0x580e,0x01},
	{0x580f,0x01},
	{0x5810,0x02},
	{0x5811,0x05},
	{0x5812,0x06},
	{0x5813,0x02},
	{0x5814,0x01},
	{0x5815,0x01},
	{0x5816,0x02},
	{0x5817,0x06},
	{0x5818,0x07},
	{0x5819,0x05},
	{0x581a,0x03},
	{0x581b,0x03},
	{0x581c,0x05},
	{0x581d,0x06},
	{0x581e,0x1f},
	{0x581f,0x0d},
	{0x5820,0x0a},
	{0x5821,0x0a},
	{0x5822,0x0c},
	{0x5823,0x1e},
	{0x5824,0x0e},
	{0x5825,0x08},
	{0x5826,0x0a},
	{0x5827,0x18},
	{0x5828,0x2a},
	{0x5829,0x0a},
	{0x582a,0x26},
	{0x582b,0x24},
	{0x582c,0x26},
	{0x582d,0x28},
	{0x582e,0x0a},
	{0x582f,0x22},
	{0x5830,0x40},
	{0x5831,0x42},
	{0x5832,0x2a},
	{0x5833,0x0a},
	{0x5834,0x26},
	{0x5835,0x26},
	{0x5836,0x26},
	{0x5837,0x2a},
	{0x5838,0x2c},
	{0x5839,0x08},
	{0x583a,0x09},
	{0x583b,0x28},
	{0x583c,0x2c},
	{0x583d,0xce},
	{0x5000,0x06},
	{0x5001,0x00},
	{0x5002,0x41},
	{0x5180,0x08},
	{0x5003,0x08},
	{0x5a00,0x08},
	{0x3000,0xff},
	{0x3001,0xff},
	{0x3002,0xff},
	{0x301d,0xf0},
	{0x3a18,0x00},
	{0x3a19,0xf8},
	{0x3c01,0x80},
	{0x3b07,0x0c},
	{0x3500, 0x00},
	{0x3501, 0x29},
	{0x3502, 0xe0},
	{0x380c,0x07},
	{0x380d,0x00},
	{0x380e,0x02},
	{0x380f,0xe8},
	{0x3814,0x31},
	{0x3815,0x31},
	{0x3708,0x64},
	{0x3709,0x52},
	{0x3808,0x05},
	{0x3809,0x00},
	{0x380a,0x02},
	{0x380b,0xd0},
	{0x3800,0x00},
	{0x3801,0x18},
	{0x3802,0x00},
	{0x3803,0xf8},
	{0x3804,0x0a},
	{0x3805,0x27},
	{0x3806,0x06},
	{0x3807,0xa7},
	{0x3630,0x2e},
	{0x3632,0xe2},
	{0x3633,0x23},
	{0x3634,0x44},
	{0x3620,0x64},
	{0x3621,0xe0},
	{0x3600,0x37},
	{0x3704,0xa0},
	{0x3703,0x5a},
	{0x3715,0x78},
	{0x3717,0x01},
	{0x3731,0x02},
	{0x370b,0x60},
	{0x3705,0x1a},
	{0x3f05,0x02},
	{0x3f06,0x10},
	{0x3f01,0x0a},
	{0x3503,0x03},
	{0x3a08,0x01},
	{0x3a09,0xbe},
	{0x3a0a,0x01},
	{0x3a0b,0x74},
	{0x3a0d,0x02},
	{0x3a0e,0x01},
	{0x3a0f,0x58},
	{0x3a10,0x50},
	{0x3a1b,0x58},
	{0x3a1e,0x50},
	{0x3a11,0x60},
	{0x3a1f,0x28},
	{0x4001,0x02},
	{0x4004,0x02},
	{0x4000,0x09},
	{0x4050,0x6e},
	{0x4051,0x8f},
	{0x0100,0x01},
	             
	{0x3035,0x21},
	{0x303c,0x12},
	{0x3a08,0x00},
	{0x3a09,0xdf},
	{0x3a0a,0x00},
	{0x3a0b,0xba},
	{0x3a0d,0x04},
	{0x3a0e,0x03},
	{0xffff, 0xff},
};

struct aml_camera_i2c_fig_s OV5647_960P_script_mipi[] = {
    { 0x4800, 0x24},
    { 0x0100, 0x00},
    { 0x0103, 0x01},
    { 0x3035, 0x11},
    { 0x3036, 0x46},
    { 0x303c, 0x11},
    { 0x3821, 0x07},
    { 0x3820, 0x41},
    { 0x370c, 0x03},
    { 0x3612, 0x59},
    { 0x3618, 0x00},
    { 0x5000, 0x06},
    { 0x5003, 0x08},
    { 0x5a00, 0x08},
    { 0x3000, 0xff},
    { 0x3001, 0xff},
    { 0x3002, 0xff},
    { 0x301d, 0xf0},
    { 0x3a18, 0x00},
    { 0x3a19, 0xf8},
    { 0x3c01, 0x80},
    { 0x3b07, 0x0c},
    { 0x380c, 0x07},
    { 0x380d, 0x68},
    { 0x380e, 0x03},
    { 0x380f, 0xd8},
    { 0x3814, 0x31},
    { 0x3815, 0x31},
    { 0x3708, 0x64},
    { 0x3709, 0x52},
    { 0x3808, 0x05},
    { 0x3809, 0x00},
    { 0x380a, 0x03},
    { 0x380b, 0xc0},
    { 0x3800, 0x00},
    { 0x3801, 0x18},
    { 0x3802, 0x00},
    { 0x3803, 0x0e},
    { 0x3804, 0x0a},
    { 0x3805, 0x27},
    { 0x3806, 0x07},
    { 0x3807, 0x95},
    { 0x3630, 0x2e},
    { 0x3632, 0xe2},
    { 0x3633, 0x23},
    { 0x3634, 0x44},
    { 0x3620, 0x64},
    { 0x3621, 0xe0},
    { 0x3600, 0x37},
    { 0x3704, 0xa0},
    { 0x3703, 0x5a},
    { 0x3715, 0x78},
    { 0x3717, 0x01},
    { 0x3731, 0x02},
    { 0x370b, 0x60},
    { 0x3705, 0x1a},
    { 0x3f05, 0x02},
    { 0x3f06, 0x10},
    { 0x3f01, 0x0a},
    { 0x3a08, 0x01},
    { 0x3a09, 0x27},
    { 0x3a0a, 0x00},
    { 0x3a0b, 0xf6},
    { 0x3a0d, 0x04},
    { 0x3a0e, 0x03},
    { 0x3a0f, 0x58},
    { 0x3a10, 0x50},
    { 0x3a1b, 0x58},
    { 0x3a1e, 0x50},
    { 0x3a11, 0x60},
    { 0x3a1f, 0x28},
    { 0x4001, 0x02},
    { 0x4004, 0x02},
    { 0x4000, 0x09},
    { 0x0100, 0x01},
    { 0x3000, 0x00},
    { 0x3001, 0x00},
    { 0x3002, 0x00},
    { 0x3017, 0xe0},
    { 0x301c, 0xfc},
    { 0x3636, 0x06},
    { 0x3016, 0x08},
    { 0x3827, 0xec},
    { 0x4800, 0x24},
    { 0x3018, 0x44},
    { 0x3035, 0x21},
    { 0x3106, 0xf5},
    { 0x3034, 0x1a},
    { 0x301c, 0xf8},
	  { 0xffff, 0xff},
};
struct aml_camera_i2c_fig_s OV5647_preview_960P_script[] = {
	{0x0100,0x00},  
	{0x0103,0x01},  
	{0x3035,0x11},  
	{0x3036,0x46},  
	{0x303c,0x11},  
	{0x3821,0x07},  
	{0x3820,0x41},  
	{0x370c,0x0f},  
	{0x3612,0x59},  
	{0x3618,0x00},  
	{0x5000,0x06},  
	{0x5800,0x1b},  
	{0x5801,0x0d},  
	{0x5802,0x09},  
	{0x5803,0x0a},  
	{0x5804,0x0b},  
	{0x5805,0x1c},  
	{0x5806,0x07},  
	{0x5807,0x05},  
	{0x5808,0x03},  
	{0x5809,0x03},  
	{0x580a,0x05},  
	{0x580b,0x07},  
	{0x580c,0x05},  
	{0x580d,0x02},  
	{0x580e,0x01},  
	{0x580f,0x01},  
	{0x5810,0x02},  
	{0x5811,0x05},  
	{0x5812,0x06},  
	{0x5813,0x02},  
	{0x5814,0x01},  
	{0x5815,0x01},  
	{0x5816,0x02},  
	{0x5817,0x06},  
	{0x5818,0x07},  
	{0x5819,0x05},  
	{0x581a,0x03},  
	{0x581b,0x03},  
	{0x581c,0x05},  
	{0x581d,0x06},  
	{0x581e,0x1f},  
	{0x581f,0x0d},  
	{0x5820,0x0a},  
	{0x5821,0x0a},  
	{0x5822,0x0c},  
	{0x5823,0x1e},  
	{0x5824,0x0e},  
	{0x5825,0x08},  
	{0x5826,0x0a},  
	{0x5827,0x18},  
	{0x5828,0x2a},  
	{0x5829,0x0a},  
	{0x582a,0x26},  
	{0x582b,0x24},  
	{0x582c,0x26},  
	{0x582d,0x28},  
	{0x582e,0x0a},  
	{0x582f,0x22},  
	{0x5830,0x40},  
	{0x5831,0x42},  
	{0x5832,0x2a},  
	{0x5833,0x0a},  
	{0x5834,0x26},  
	{0x5835,0x26},  
	{0x5836,0x26},  
	{0x5837,0x2a},  
	{0x5838,0x2c},  
	{0x5839,0x08},  
	{0x583a,0x09},  
	{0x583b,0x28},  
	{0x583c,0x2c},  
	{0x583d,0xce},  
	{0x5000,0x06},  
	{0x5001,0x00},  
	{0x5002,0x41},	
	{0x5180,0x08},
	{0x5003,0x08},  
	{0x5a00,0x08},  
	{0x3000,0xff},  
	{0x3001,0xff},  
	{0x3002,0xff},  
	{0x301d,0xf0},  
	{0x3a18,0x00},  
	{0x3a19,0xf8},  
	{0x3c01,0x80},  
	{0x3b07,0x0c}, 
	{0x3500,0x00},
	{0x3501,0x37},
	{0x3502,0x60},
	{0x380c,0x07},  
	{0x380d,0x68},  
	{0x380e,0x03},  
	{0x380f,0xd8},  
	{0x3814,0x31},  
	{0x3815,0x31},  
	{0x3708,0x64},  
	{0x3709,0x52},  
	{0x3808,0x05},  
	{0x3809,0x00},  
	{0x380a,0x03},  
	{0x380b,0xc0},  
	{0x3800,0x00},  
	{0x3801,0x18},  
	{0x3802,0x00},  
	{0x3803,0x0e},  
	{0x3804,0x0a},  
	{0x3805,0x27},  
	{0x3806,0x07},  
	{0x3807,0x95},  
	{0x3630,0x2e},  
	{0x3632,0xe2},  
	{0x3633,0x23},  
	{0x3634,0x44},  
	{0x3620,0x64},  
	{0x3621,0xe0},  
	{0x3600,0x37},  
	{0x3704,0xa0},  
	{0x3703,0x5a},  
	{0x3715,0x78},  
	{0x3717,0x01},  
	{0x3731,0x02},  
	{0x370b,0x60},  
	{0x3705,0x1a},  
	{0x3f05,0x02},  
	{0x3f06,0x10},  
	{0x3f01,0x0a},  
	{0x3503,0x03},  
	{0x3a08,0x01},  
	{0x3a09,0x27},  
	{0x3a0a,0x00},  
	{0x3a0b,0xf6},  
	{0x3a0d,0x04},  
	{0x3a0e,0x03},  
	{0x3a0f,0x58},  
	{0x3a10,0x50},  
	{0x3a1b,0x58},  
	{0x3a1e,0x50},  
	{0x3a11,0x60},  
	{0x3a1f,0x28},  
	{0x4001,0x02},  
	{0x4004,0x02},  
	{0x4000,0x09},  
	{0x4050,0x6e},  
	{0x4051,0x8f},  
	{0x0100,0x01},
	{0xffff, 0xff},
};

struct aml_camera_i2c_fig_s OV5647_1080P_script_mipi[] = {
          {0x4800, 0x24},
          {0x0100, 0x00},
          {0x0103, 0x01},
          {0x3035, 0x11},
          {0x3036, 0x64},
          {0x303c, 0x11},
          {0x3821, 0x06},
          {0x3820, 0x00},
          {0x370c, 0x0f},
          {0x3612, 0x5b},
          {0x3618, 0x04},
          {0x5000, 0x06},

          {0x5000, 0x06},
          {0x5001, 0x00},
          {0x5002, 0x41},

          {0x5003, 0x08},
          {0x5a00, 0x08},
          {0x3000, 0xff},
          {0x3001, 0xff},
          {0x3002, 0xff},
          {0x301d, 0xf0},
          {0x3a18, 0x00},
          {0x3a19, 0xf8},
          {0x3c01, 0x80},
          {0x3b07, 0x0c},
          {0x380c, 0x09},
          {0x380d, 0x70},
          {0x380e, 0x04},
          {0x380f, 0x50},
          {0x3814, 0x11},
          {0x3815, 0x11},
          {0x3708, 0x64},
          {0x3709, 0x12},
          {0x3808, 0x07},
          {0x3809, 0x80},
          {0x380a, 0x04},
          {0x380b, 0x38},
          {0x3800, 0x01},
          {0x3801, 0x5c},
          {0x3802, 0x01},
          {0x3803, 0xb2},
          {0x3804, 0x08},
          {0x3805, 0xe3},
          {0x3806, 0x05},
          {0x3807, 0xf1},
          {0x3630, 0x2e},
          {0x3632, 0xe2},
          {0x3633, 0x23},
          {0x3634, 0x44},
          {0x3620, 0x64},
          {0x3621, 0xe0},
          {0x3600, 0x37},
          {0x3704, 0xa0},
          {0x3703, 0x5a},
          {0x3715, 0x78},
          {0x3717, 0x01},
          {0x3731, 0x02},
          {0x370b, 0x60},
          {0x3705, 0x1a},
          {0x3f05, 0x02},
          {0x3f06, 0x10},
          {0x3f01, 0x0a},
          {0x3503, 0x03},
          {0x3a08, 0x01},
          {0x3a09, 0x4b},
          {0x3a0a, 0x01},
          {0x3a0b, 0x13},
          {0x3a0d, 0x04},
          {0x3a0e, 0x03},
          {0x3a0f, 0x58},
          {0x3a10, 0x50},
          {0x3a1b, 0x58},
          {0x3a1e, 0x50},
          {0x3a11, 0x60},
          {0x3a1f, 0x28},
          {0x4001, 0x02},
          {0x4004, 0x04},
          {0x4000, 0x09},
          {0x4050, 0x6e},
          {0x4051, 0x8f},
          {0x0100, 0x01},
          {0x3000, 0x00},
          {0x3001, 0x00},
          {0x3002, 0x00},
          {0x3017, 0xe0},
          {0x301c, 0xfc},
          {0x3636, 0x06},
          {0x3016, 0x08},
          {0x3827, 0xec},
          {0x4800, 0x24},
          {0x3018, 0x44},
          {0x3035, 0x21},
          {0x3106, 0xf5},
          {0x3034, 0x1a},
          {0x301c, 0xf8},

	  {0xffff, 0xff},
};
struct aml_camera_i2c_fig_s OV5647_preview_1080P_script[] = {
	{0x0100,0x00},
	{0x0103,0x01},
	{0x3035,0x11},
	{0x3036,0x64},
	{0x303c,0x11},
	{0x3821,0x06},
	{0x3820,0x00},
	{0x370c,0x0f},
	{0x3612,0x5b},
	{0x3618,0x04},
	{0x5000,0x06},
	{0x5800,0x1b},
	{0x5801,0x0d},
	{0x5802,0x09},
	{0x5803,0x0a},
	{0x5804,0x0b},
	{0x5805,0x1c},
	{0x5806,0x07},
	{0x5807,0x05},
	{0x5808,0x03},
	{0x5809,0x03},
	{0x580a,0x05},
	{0x580b,0x07},
	{0x580c,0x05},
	{0x580d,0x02},
	{0x580e,0x01},
	{0x580f,0x01},
	{0x5810,0x02},
	{0x5811,0x05},
	{0x5812,0x06},
	{0x5813,0x02},
	{0x5814,0x01},
	{0x5815,0x01},
	{0x5816,0x02},
	{0x5817,0x06},
	{0x5818,0x07},
	{0x5819,0x05},
	{0x581a,0x03},
	{0x581b,0x03},
	{0x581c,0x05},
	{0x581d,0x06},
	{0x581e,0x1f},
	{0x581f,0x0d},
	{0x5820,0x0a},
	{0x5821,0x0a},
	{0x5822,0x0c},
	{0x5823,0x1e},
	{0x5824,0x0e},
	{0x5825,0x08},
	{0x5826,0x0a},
	{0x5827,0x18},
	{0x5828,0x2a},
	{0x5829,0x0a},
	{0x582a,0x26},
	{0x582b,0x24},
	{0x582c,0x26},
	{0x582d,0x28},
	{0x582e,0x0a},
	{0x582f,0x22},
	{0x5830,0x40},
	{0x5831,0x42},
	{0x5832,0x2a},
	{0x5833,0x0a},
	{0x5834,0x26},
	{0x5835,0x26},
	{0x5836,0x26},
	{0x5837,0x2a},
	{0x5838,0x2c},
	{0x5839,0x08},
	{0x583a,0x09},
	{0x583b,0x28},
	{0x583c,0x2c},
	{0x583d,0xce},
	{0x5000,0x06},
	{0x5001,0x00},
	{0x5002,0x41},
	{0x5180,0x08},
	{0x5003,0x08},
	{0x5a00,0x08},
	{0x3000,0xff},
	{0x3001,0xff},
	{0x3002,0xff},
	{0x301d,0xf0},
	{0x3a18,0x00},
	{0x3a19,0xf8},
	{0x3c01,0x80},
	{0x3b07,0x0c},
	{0x3500,0x00},
	{0x3501,0x3e},
	{0x3502,0x10},
	{0x380c,0x09},
	{0x380d,0x70},
	{0x380e,0x04},
	{0x380f,0x50},
	{0x3814,0x11},
	{0x3815,0x11},
	{0x3708,0x64},
	{0x3709,0x12},
	{0x3808,0x07},
	{0x3809,0x80},
	{0x380a,0x04},
	{0x380b,0x38},
	{0x3800,0x01},
	{0x3801,0x5c},
	{0x3802,0x01},
	{0x3803,0xb2},
	{0x3804,0x08},
	{0x3805,0xe3},
	{0x3806,0x05},
	{0x3807,0xf1},
	{0x3630,0x2e},
	{0x3632,0xe2},
	{0x3633,0x23},
	{0x3634,0x44},
	{0x3620,0x64},
	{0x3621,0xe0},
	{0x3600,0x37},
	{0x3704,0xa0},
	{0x3703,0x5a},
	{0x3715,0x78},
	{0x3717,0x01},
	{0x3731,0x02},
	{0x370b,0x60},
	{0x3705,0x1a},
	{0x3f05,0x02},
	{0x3f06,0x10},
	{0x3f01,0x0a},
	{0x3503,0x03},
	{0x3a08,0x01},
	{0x3a09,0x4b},
	{0x3a0a,0x01},
	{0x3a0b,0x13},
	{0x3a0d,0x04},
	{0x3a0e,0x03},
	{0x3a0f,0x58},
	{0x3a10,0x50},
	{0x3a1b,0x58},
	{0x3a1e,0x50},
	{0x3a11,0x60},
	{0x3a1f,0x28},
	{0x4001,0x02},
	{0x4004,0x04},
	{0x4000,0x09},
	{0x4050,0x6e},
	{0x4051,0x8f},
	{0x0100,0x01},
   
	{0x3035,0x21},
	{0x303c,0x12},
	{0x3a08,0x00},
	{0x3a09,0xa6},
	{0x3a0a,0x00},
	{0x3a0b,0x8a},
	{0x3a0d,0x08},
	{0x3a0e,0x06},
	{0xffff, 0xff},
};

struct aml_camera_i2c_fig_s OV5647_5M_script_mipi[] = {
          {0x0100, 0x00},
          {0x0103, 0x01},
          {0x3035, 0x11},
          {0x3036, 0x64},
          {0x303c, 0x11},
          {0x3821, 0x06},
          {0x3820, 0x00},
          {0x370c, 0x0f},
          {0x3612, 0x5b},
          {0x3618, 0x04},
          {0x5000, 0x06},

          {0x5000, 0x06},
          {0x5001, 0x00},
          {0x5002, 0x41},
          {0x5003, 0x08},
          {0x5a00, 0x08},
          {0x3000, 0xff},
          {0x3001, 0xff},
          {0x3002, 0xff},
          {0x301d, 0xf0},
          {0x3a18, 0x00},
          {0x3a19, 0xf8},
          {0x3c01, 0x80},
          {0x3b07, 0x0c},
          {0x380c, 0x0a},
          {0x380d, 0x8c},
          {0x380e, 0x07},
          {0x380f, 0xb6},
          {0x3814, 0x11},
          {0x3815, 0x11},
          {0x3708, 0x64},
          {0x3709, 0x12},
          {0x3808, 0x0a},
          {0x3809, 0x20},
          {0x380a, 0x07},
          {0x380b, 0x98},
          {0x3800, 0x00},
          {0x3801, 0x0c},
          {0x3802, 0x00},
          {0x3803, 0x04},
          {0x3804, 0x0a},
          {0x3805, 0x33},
          {0x3806, 0x07},
          {0x3807, 0xa3},
          {0x3630, 0x2e},
          {0x3632, 0xe2},
          {0x3633, 0x23},
          {0x3634, 0x44},
          {0x3620, 0x64},
          {0x3621, 0xe0},
          {0x3600, 0x37},
          {0x3704, 0xa0},
          {0x3703, 0x5a},
          {0x3715, 0x78},
          {0x3717, 0x01},
          {0x3731, 0x02},
          {0x370b, 0x60},
          {0x3705, 0x1a},
          {0x3f05, 0x02},
          {0x3f06, 0x10},
          {0x3f01, 0x0a},
          {0x3503, 0x03},
          {0x3a08, 0x01},
          {0x3a09, 0x28},
          {0x3a0a, 0x00},
          {0x3a0b, 0xf6},
          {0x3a0d, 0x08},
          {0x3a0e, 0x06},
          {0x3a0f, 0x58},
          {0x3a10, 0x50},
          {0x3a1b, 0x58},
          {0x3a1e, 0x50},
          {0x3a11, 0x60},
          {0x3a1f, 0x28},
          {0x4001, 0x02},
          {0x4004, 0x04},
          {0x4000, 0x09},
          {0x4050, 0x6e},
          {0x4051, 0x8f},
          {0x0100, 0x01},
          {0x3000, 0x00},
          {0x3001, 0x00},
          {0x3002, 0x00},
          {0x3017, 0xe0},
          {0x301c, 0xfc},
          {0x3636, 0x06},
          {0x3016, 0x08},
          {0x3827, 0xec},
          {0x4800, 0x24},
          {0x3018, 0x44},
          {0x3035, 0x21},
          {0x3106, 0xf5},
          {0x3034, 0x1a},
          {0x301c, 0xf8},
	  {0xffff, 0xff},
};
struct aml_camera_i2c_fig_s OV5647_capture_5M_script[] = {
#if 0
  {0x0100,0x00},
	{0x0103,0x01},
	{0x3035,0x11},
	{0x3036,0x64},
	{0x303c,0x11},
	{0x3821,0x06},
	{0x3820,0x00},
	{0x370c,0x0f},
	{0x3612,0x5b},
	{0x3618,0x04},
	{0x5000,0x06},
	{0x5800,0x1b},
	{0x5801,0x0d},
	{0x5802,0x09},
	{0x5803,0x0a},
	{0x5804,0x0b},
	{0x5805,0x1c},
	{0x5806,0x07},
	{0x5807,0x05},
	{0x5808,0x03},
	{0x5809,0x03},
	{0x580a,0x05},
	{0x580b,0x07},
	{0x580c,0x05},
	{0x580d,0x02},
	{0x580e,0x01},
	{0x580f,0x01},
	{0x5810,0x02},
	{0x5811,0x05},
	{0x5812,0x06},
	{0x5813,0x02},
	{0x5814,0x01},
	{0x5815,0x01},
	{0x5816,0x02},
	{0x5817,0x06},
	{0x5818,0x07},
	{0x5819,0x05},
	{0x581a,0x03},
	{0x581b,0x03},
	{0x581c,0x05},
	{0x581d,0x06},
	{0x581e,0x1f},
	{0x581f,0x0d},
	{0x5820,0x0a},
	{0x5821,0x0a},
	{0x5822,0x0c},
	{0x5823,0x1e},
	{0x5824,0x0e},
	{0x5825,0x08},
	{0x5826,0x0a},
	{0x5827,0x18},
	{0x5828,0x2a},
	{0x5829,0x0a},
	{0x582a,0x26},
	{0x582b,0x24},
	{0x582c,0x26},
	{0x582d,0x28},
	{0x582e,0x0a},
	{0x582f,0x22},
	{0x5830,0x40},
	{0x5831,0x42},
	{0x5832,0x2a},
	{0x5833,0x0a},
	{0x5834,0x26},
	{0x5835,0x26},
	{0x5836,0x26},
	{0x5837,0x2a},
	{0x5838,0x2c},
	{0x5839,0x08},
	{0x583a,0x09},
	{0x583b,0x28},
	{0x583c,0x2c},
	{0x583d,0xce},
	{0x5000,0x06},
	{0x5001,0x00},
	{0x5002,0x41},
	{0x5180,0x08},
	{0x5003,0x08},
	{0x5a00,0x08},
	{0x3000,0xff},
	{0x3001,0xff},
	{0x3002,0xff},
	{0x301d,0xf0},
	{0x3a18,0x00},
	{0x3a19,0xf8},
	{0x3c01,0x80},
	{0x3b07,0x0c},
	{0x380c,0x0a},
	{0x380d,0x8c},
	{0x380e,0x07},
	{0x380f,0xb6},
	{0x3814,0x11},
	{0x3815,0x11},
	{0x3708,0x64},
	{0x3709,0x12},
	{0x3808,0x0a},
	{0x3809,0x20},
	{0x380a,0x07},
	{0x380b,0x98},
	{0x3800,0x00},
	{0x3801,0x0c},
	{0x3802,0x00},
	{0x3803,0x04},
	{0x3804,0x0a},
	{0x3805,0x33},
	{0x3806,0x07},
	{0x3807,0xa3},
	{0x3630,0x2e},
	{0x3632,0xe2},
	{0x3633,0x23},
	{0x3634,0x44},
	{0x3620,0x64},
	{0x3621,0xe0},
	{0x3600,0x37},
	{0x3704,0xa0},
	{0x3703,0x5a},
	{0x3715,0x78},
	{0x3717,0x01},
	{0x3731,0x02},
	{0x370b,0x60},
	{0x3705,0x1a},
	{0x3f05,0x02},
	{0x3f06,0x10},
	{0x3f01,0x0a},
	{0x3503,0x03},
	{0x3a08,0x01},
	{0x3a09,0x28},
	{0x3a0a,0x00},
	{0x3a0b,0xf6},
	{0x3a0d,0x08},
	{0x3a0e,0x06},
	{0x3a0f,0x58},
	{0x3a10,0x50},
	{0x3a1b,0x58},
	{0x3a1e,0x50},
	{0x3a11,0x60},
	{0x3a1f,0x28},
	{0x4001,0x02},
	{0x4004,0x04},
	{0x4000,0x09},
	{0x4050,0x6e},
	{0x4051,0x8f},
	{0x0100,0x01},
	             
	{0x3035,0x21},
	{0x303c,0x12},
	{0x3a08,0x00},
	{0x3a09,0x94},
	{0x3a0a,0x00},
	{0x3a0b,0x7b},
	{0x3a0d,0x10},
	{0x3a0e,0x0d},
#endif
#if 1
{0x0100, 0x01},
{0x3000, 0x0f},
{0x3001, 0xff},
{0x3002, 0xe4},
//{0x300a, 0x0,

//{0x300b, 0x0,

{0x0100, 0x00},
{0x0103, 0x01},
{0x3013, 0x08},
{0x5000, 0x03},
{0x5001, 0x00},
{0x5a00, 0x08},
{0x3a18, 0x01},
{0x3a19, 0xe0},
{0x3c01, 0x80},
{0x3b07, 0x0c},
{0x3630, 0x2e},
{0x3632, 0xe2},
{0x3633, 0x23},
{0x3634, 0x44},
{0x3620, 0x64},
{0x3621, 0xe0},
{0x3600, 0x37},
{0x3704, 0xa0},
{0x3703, 0x5a},
{0x3715, 0x78},
{0x3717, 0x01},
{0x3731, 0x02},
{0x370b, 0x60},
{0x3705, 0x1a},
{0x3f05, 0x02},
{0x3f06, 0x10},
{0x3f01, 0x0a},
{0x3a08, 0x00},
{0x3a0a, 0x00},
{0x3a0f, 0x58},
{0x3a10, 0x50},
{0x3a1b, 0x58},
{0x3a1e, 0x50},
{0x3a11, 0x60},
{0x3a1f, 0x28},
{0x0100, 0x01},
{0x4000, 0x89},
{0x4001, 0x02},
{0x4002, 0xc5},
{0x4004, 0x06},
{0x4005, 0x1a},
{0x3503, 0x03},
{0x3501, 0x10},
{0x3502, 0x80},
{0x350a, 0x00},
{0x350b, 0x7f},
{0x350c, 0x00},
{0x350d, 0x00},
{0x3011, 0x22},

{0x3000, 0x00},
{0x3001, 0x00},
{0x3002, 0x00},
{0x3035, 0x11},
{0x3036, 0x64},
{0x303c, 0x11},
{0x3820, 0x00},
{0x3821, 0x06},
{0x3612, 0x4b},
{0x3618, 0x04},
{0x3708, 0x24},
{0x3709, 0x12},
{0x370c, 0x00},
{0x3500, 0x00},
{0x3501, 0x6e},
{0x3502, 0xb0},
{0x380c, 0x0a},
{0x380d, 0x96},
{0x380e, 0x07},
{0x380f, 0xb0},
{0x3814, 0x11},
{0x3815, 0x11},
{0x3808, 0x0a},
{0x3809, 0x20},
{0x380a, 0x07},
{0x380b, 0x98},
{0x3800, 0x00},
{0x3801, 0x0c},
{0x3802, 0x00},
{0x3803, 0x04},
{0x3804, 0x0a},
{0x3805, 0x33},
{0x3806, 0x07},
{0x3807, 0xa3},
{0x3000, 0x0f},
{0x3001, 0xff},
{0x3002, 0xe4},
/*{0x3208, 0x00},
{0x3503, 0x13},
{0x3502, 0xa0},
{0x3501, 0x6e},
{0x3500, 0x00},
{0x3208, 0x10},
{0x3208, 0xa0},
{0x3208, 0x00},
{0x3503, 0x13},
{0x350b, 0xa0},
{0x350a, 0x00},
{0x3508, 0x10},
{0x3208, 0xa0},*/
#endif
	{0xffff, 0xff},
};

static resolution_param_t  debug_prev_resolution_array[] = {
	{
		.frmsize			= {176, 144},
		.active_frmsize		= {1280, 960},
		.active_fps			= 30,
		.size_type			= SIZE_176X144,
		.reg_script[0]			= OV5647_preview_960P_script,
		.reg_script[1]			= OV5647_VGA_script_mipi,
	},{
		.frmsize			= {352, 288},
		.active_frmsize		= {1280, 960},
		.active_fps			= 30,
		.size_type			= SIZE_352X288,
		.reg_script[0]			= OV5647_preview_960P_script,
		.reg_script[1]			= OV5647_VGA_script_mipi,
	}, {
		.frmsize			= {320, 240},
		.active_frmsize		= {1280, 960},
		.active_fps			= 30,
		.size_type			= SIZE_320X240,
		.reg_script[0]			= OV5647_preview_960P_script,
		.reg_script[1]			= OV5647_VGA_script_mipi,
	},{
		.frmsize			= {640, 480},
		.active_frmsize		= {1280, 960},
		.active_fps			= 30,
		.size_type			= SIZE_640X480,
		.reg_script[0]			= OV5647_preview_960P_script,
		.reg_script[1]			= OV5647_VGA_script_mipi,
	}, {
		.frmsize			= {1280, 720},
		.active_frmsize		= {1280, 720},
		.active_fps			= 30,
		.size_type			= SIZE_1280X720,
		.reg_script[0]			= OV5647_preview_720P_script,
		.reg_script[1]			= OV5647_720P_script_mipi,
	}, {
		.frmsize			= {1280, 960},
		.active_frmsize		= {2592, 1944},
		.active_fps			= 15,
		.size_type			= SIZE_1280X960,
		.reg_script[0]			= OV5647_capture_5M_script,//OV5647_preview_960P_script,
		.reg_script[1]			=  OV5647_720P_script_mipi,//OV5647_960P_script_mipi,
	}, {
		.frmsize			= {1920, 1080},
		.active_frmsize		= {1280, 720},
		.active_fps			= 30,
		.size_type			= SIZE_1920X1080,
		.reg_script[0]			= OV5647_preview_720P_script,//OV5647_preview_1080P_script,
		.reg_script[1]			= OV5647_1080P_script_mipi,
	},/*{
		.frmsize			= {2048, 1536},
		.active_frmsize		= {2592, 1944},
		.active_fps			= 7.5,
		.size_type			= SIZE_2048X1536,
		.reg_script[0]			= OV5647_capture_5M_script,
		.reg_script[1]			= OV5647_5M_script_mipi,
	}*/
};


static resolution_param_t  prev_resolution_array[] = {
	{
		.frmsize			= {176, 144},
		.active_frmsize		= {1280, 960},
		.active_fps			= 30,
		.size_type			= SIZE_176X144,
		.reg_script[0]			= OV5647_preview_960P_script,
		.reg_script[1]			= OV5647_VGA_script_mipi,
	},{
		.frmsize			= {352, 288},
		.active_frmsize		= {1280, 960},
		.active_fps			= 30,
		.size_type			= SIZE_352X288,
		.reg_script[0]			= OV5647_preview_960P_script,
		.reg_script[1]			= OV5647_VGA_script_mipi,
	}, {
		.frmsize			= {320, 240},
		.active_frmsize		= {1280, 960},
		.active_fps			= 30,
		.size_type			= SIZE_320X240,
		.reg_script[0]			= OV5647_preview_960P_script,
		.reg_script[1]			= OV5647_VGA_script_mipi,
	}, {
		.frmsize			= {640, 480},
		.active_frmsize		= {1280, 960},
		.active_fps			= 30,
		.size_type			= SIZE_640X480,
		.reg_script[0]			= OV5647_preview_960P_script,
		.reg_script[1]			= OV5647_VGA_script_mipi,
	}, {
		.frmsize			= {1280, 720},
		.active_frmsize		= {1280, 720},
		.active_fps			= 30,
		.size_type			= SIZE_1280X720,
		.reg_script[0]			= OV5647_preview_720P_script,
		.reg_script[1]			= OV5647_720P_script_mipi,
	}, {
		.frmsize			= {1280, 960},
		.active_frmsize		= {2592, 1944},
		.active_fps			= 15,
		.size_type			= SIZE_1280X960,
		.reg_script[0]			= OV5647_capture_5M_script,//OV5647_preview_960P_script,
		.reg_script[1]			=  OV5647_720P_script_mipi,//OV5647_960P_script_mipi,
	}, {
		.frmsize			= {1920, 1080},
		.active_frmsize		= {1280, 960},
		.active_fps			= 30,
		.size_type			= SIZE_1920X1080,
		.reg_script[0]			= OV5647_preview_720P_script,//OV5647_preview_1080P_script,
		.reg_script[1]			= OV5647_1080P_script_mipi,
	},/*{
		.frmsize			= {2048, 1536},
		.active_frmsize		= {2592, 1944},
		.active_fps			= 7.5,
		.size_type			= SIZE_2048X1536,
		.reg_script[0]			= OV5647_capture_5M_script,
		.reg_script[1]			= OV5647_5M_script_mipi,
	}*/
};
	

static resolution_param_t  capture_resolution_array[] = {
	{
		.frmsize			= {2592, 1936},
		.active_frmsize		= {2592, 1944},
		.active_fps			= 15,
		.size_type			= SIZE_2592X1944,
		.reg_script[0]			= OV5647_capture_5M_script,
		.reg_script[1]			= OV5647_5M_script_mipi,
	},{
		.frmsize			= {2048, 1536},
		.active_frmsize		= {2592, 1944},
		.active_fps			= 15,
		.size_type			= SIZE_2048X1536,
		.reg_script[0]			= OV5647_capture_5M_script,
		.reg_script[1]			= OV5647_5M_script_mipi,
	},{
		.frmsize			= {1600, 1200},
		.active_frmsize		= {2592, 1944},
		.active_fps			= 15,
		.size_type			= SIZE_1600X1200,
		.reg_script[0]			= OV5647_capture_5M_script,
		.reg_script[1]			= OV5647_5M_script_mipi,
	},
};


static void parse_param(const char *buf,char **parm){
	char *buf_orig, *ps, *token;
	unsigned int n=0;

	buf_orig = kstrdup(buf, GFP_KERNEL);
	ps = buf_orig;
	while(1) {
	        token = strsep(&ps, " \n");
	        if (token == NULL)
	                break;
	        if (*token == '\0')
	                continue;
	        parm[n++] = token;
	        printk("%s\n",parm[n-1]);
	}
	//kfree(buf_orig);
}

void OV5647_manual_set_aet(int exp, int ag, int vts){
	unsigned char exp_h = 0, exp_m = 0, exp_l = 0, ag_h = 0, ag_l = 0, vts_h = 0, vts_l = 0;
	struct i2c_adapter *adapter;
	if(exp != -1){
		exp_h = (unsigned char)((exp >> 12) & 0x0000000f);
		exp_m = (unsigned char)((exp >>  4) & 0x000000ff);
		exp_l = (unsigned char)((exp <<  4) & 0x000000f0);
	}
	if(ag != -1){
		ag_h = (unsigned char)((ag >> 8) & 0x00000003);
		ag_l = (unsigned char)(ag & 0x000000ff);
	}
	if(vts != -1){
		vts_h = (unsigned char)((vts >> 8) & 0x000000ff);
		vts_l = (unsigned char)(vts & 0x000000ff);
	}
	
	adapter = i2c_get_adapter(4);
	my_i2c_put_byte(adapter,0x36,0x3208, 0x00 );
	if(exp != -1)
	{
		if(exp_h != last_exp_h){
			my_i2c_put_byte(adapter,0x36,0x3500, exp_h);
			last_exp_h = exp_h;
		}
		if(exp_m != last_exp_m){
			my_i2c_put_byte(adapter,0x36,0x3501, exp_m);
			last_exp_m = exp_m;
		}
		if(exp_m != last_exp_m){
			my_i2c_put_byte(adapter,0x36,0x3502, exp_l);
			last_exp_l = exp_l;
		}
		if(vts_h != last_vts_h){
			my_i2c_put_byte(adapter,0x36,0x380e, vts_h);
			last_vts_h = vts_h;
		}
		if(vts_l != last_vts_l){
			my_i2c_put_byte(adapter,0x36,0x380f, vts_l);
			last_vts_l = vts_l;
		}
	}
	if(ag != -1)
	{
		if(ag_h != last_ag_h){
			my_i2c_put_byte(adapter,0x36,0x350a, ag_h );
			last_ag_h = ag_h;
		}
		if(ag_l != last_ag_l){
			my_i2c_put_byte(adapter,0x36,0x350b, ag_l );
			last_ag_l = ag_l;
		}
	}
	my_i2c_put_byte(adapter,0x36,0x3208, 0x10 );
	my_i2c_put_byte(adapter,0x36,0x3208, 0xa0 );
}

static ssize_t aet_manual_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	char *param[3] = {NULL};
	unsigned int exp = 0, ag = 0, vts = 0;
	parse_param(buf,&param[0]);
	
	if(param[0] == NULL || param[1] == NULL || param[2] == NULL){
		printk("wrong param\n");
		return len;	
	}	
	sscanf(param[0],"%x",&exp);
	sscanf(param[1],"%x",&ag);
	sscanf(param[2],"%x",&vts);
	
	OV5647_manual_set_aet(exp,ag,vts);
	return len;
}

static ssize_t aet_manual_show(struct class *cls,struct class_attribute *attr, char* buf)
{
	size_t len = 0;
	return len;
}

static CLASS_ATTR(aet_debug, 0664, aet_manual_show, aet_manual_store);

/* ov5647 uses exp+ag mode */
static bool OV5647_set_aet_new_step(void *priv, unsigned int new_step, bool exp_mode, bool ag_mode){
  	unsigned int exp = 0, ag = 0, vts = 0;
	camera_priv_data_t *camera_priv_data = (camera_priv_data_t *)priv; 
	sensor_aet_t *sensor_aet_table = camera_priv_data->sensor_aet_table;
	sensor_aet_info_t *sensor_aet_info = camera_priv_data->sensor_aet_info;
	
	if(camera_priv_data == NULL || sensor_aet_table == NULL || sensor_aet_info == NULL)
		return false;	
	if (((!exp_mode) && (!ag_mode)) || (new_step > sensor_aet_info[aet_index].tbl_max_step))
		return(false);
	else
	{
		camera_priv_data->sensor_aet_step = new_step;
		exp = sensor_aet_table[camera_priv_data->sensor_aet_step].exp;
		ag = sensor_aet_table[camera_priv_data->sensor_aet_step].ag;
		vts = sensor_aet_table[camera_priv_data->sensor_aet_step].vts;
		if(exp_mode == 1 && ag_mode == 1){
			OV5647_manual_set_aet(exp,ag,vts);
		}else if(exp_mode == 1 && ag_mode == 0){
			OV5647_manual_set_aet(exp,-1,vts);
		}else if(exp_mode == 0 && ag_mode == 1){
			OV5647_manual_set_aet(-1,ag,-1);
		}
		return true;
	}
}


static bool OV5647_check_mains_freq(void *priv){// when the fr change,we need to change the aet table
#if 0
    int detection; 
    struct i2c_adapter *adapter;
#endif
    priv = priv;

    return true;
}

bool OV5647_set_af_new_step(void *priv, unsigned int af_step){
    struct i2c_adapter *adapter;
    char buf[3];
    if(af_step == last_af_step)
        return true;
    if(vcm_mod == 0){
	    unsigned int diff,vcm_data=0,codes;
	    diff = (af_step > last_af_step) ? af_step - last_af_step : last_af_step - af_step;
	    last_af_step = af_step;
	    if(diff < 256){
	        codes = 1;
	    }else if(diff < 512){
	        codes = 2;
	    }else
	        codes = 3;
	    vcm_data |= (codes << 2); // bit[3:2]
	    vcm_data |= (last_af_step << 4);  // bit[4:13]
	    buf[0]  = (vcm_data >> 8) & 0x000000ff;
	    buf[1]  = (vcm_data >> 0) & 0x000000ff;
    }
    else{
	    last_af_step = af_step;
	    buf[0] = (af_step>>4)&0xff;
	    buf[1] = (af_step<<4)&0xff;
    }
    adapter = i2c_get_adapter(4);
    my_i2c_put_byte_add8(adapter,0x0c,buf,2);
    return true;

}

void OV5647_set_new_format(void *priv,int width,int height,int fr){
    int index = 0;
    camera_priv_data_t *camera_priv_data = (camera_priv_data_t *)priv;
    configure_t *configure = camera_priv_data->configure;
    current_fr = fr;
    if(camera_priv_data == NULL)
    	return;
    printk("sum:%d,mode:%d,fr:%d\n",configure->aet.sum,ov5647_work_mode,fr);
    while(index < configure->aet.sum){
        if(width == configure->aet.aet[index].info->fmt_hactive && height == configure->aet.aet[index].info->fmt_vactive \
                && fr == configure->aet.aet[index].info->fmt_main_fr && ov5647_work_mode == configure->aet.aet[index].info->fmt_capture){
            break;	
        }
        index++;	
    }
    if(index >= configure->aet.sum){
        printk("use default value\n");
        index = 0;	
    }
    printk("current aet index :%d\n",index);
    camera_priv_data->sensor_aet_info = configure->aet.aet[index].info;
    camera_priv_data->sensor_aet_table = configure->aet.aet[index].aet_table;
    camera_priv_data->sensor_aet_step = camera_priv_data->sensor_aet_info->tbl_rated_step;
    return;
}



void OV5647_ae_manual_set(char **param){
	if(param[0] == NULL || param[1] == NULL || param[2] == NULL){
		printk("wrong param\n");
		return ;	
	}	
	sscanf(param[0],"%x",&g_ae_manual_exp);
	sscanf(param[1],"%x",&g_ae_manual_ag);
	sscanf(param[2],"%x",&g_ae_manual_vts);
	
	g_ae_manual_exp = (g_ae_manual_exp > 0x0000ffff) ? 0x0000ffff : g_ae_manual_exp;
	g_ae_manual_ag = (g_ae_manual_ag > 0x000003ff) ? 0x000003ff : g_ae_manual_ag;
	g_ae_manual_vts = (g_ae_manual_vts > 0x0000ffff) ? 0x0000ffff : g_ae_manual_vts;
	
	OV5647_manual_set_aet(g_ae_manual_exp,g_ae_manual_ag,g_ae_manual_vts);
}           

static ssize_t ae_manual_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	char *param[3] = {NULL};
	parse_param(buf,&param[0]);
	OV5647_ae_manual_set(&param[0]);
	return len;
}

static ssize_t ae_manual_show(struct class *cls,struct class_attribute *attr, char* buf)
{
	size_t len = 0;
	printk("exp:%x,ag%x,vts:%x\n",g_ae_manual_exp,g_ae_manual_ag,g_ae_manual_vts);
	return len;
}

static CLASS_ATTR(ae_debug, 0664, ae_manual_show, ae_manual_store);

static ssize_t i2c_debug_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	char cmd;
	int addr,value;
    int endaddr;
	struct i2c_adapter *adapter;
	char *param[3] = {NULL};

	parse_param(buf,&param[0]);
	sscanf(param[0],"%c",&cmd);
	printk("cmd:%c\n",cmd);
	adapter = i2c_get_adapter(4);
    switch( cmd ){
    	case 'w':
			sscanf(param[1],"%x",&addr);
			sscanf(param[2],"%x",&value);
			my_i2c_put_byte(adapter,0x36,addr,value);
                break;

        case 'r':
			sscanf(param[1],"%x",&addr);
			value = my_i2c_get_byte(adapter,0x36,addr);
			printk("reg:%x,value:%x\n",addr,value);
                break;

        case 'd':
			sscanf(param[1],"%x",&addr);
			sscanf(param[2],"%x",&endaddr);
                        for( ;addr <= endaddr; addr++)
                        {
                                value = my_i2c_get_byte(adapter,0x36,addr);
                                printk("[0x%04x]=0x%08x\n",addr,value);
                        }
                break;

        default :
                break;
	}
	
	return len;
}

static ssize_t i2c_debug_show(struct class *cls,struct class_attribute *attr, char* buf)
{

	size_t len = 0;

	return len;
}

static CLASS_ATTR(camera_debug, 0664, i2c_debug_show, i2c_debug_store);

static void power_down_ov5647(struct ov5647_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	i2c_put_byte(client,0x0103, 0x01);
	msleep(10);
	i2c_put_byte(client,0x0100, 0x00);
}

static ssize_t dg_manual_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	char *param[3] = {NULL};
	struct i2c_adapter *adapter;
	struct sensor_dg_s dg;
	
	parse_param(buf,&param[0]);
	if(param[0] == NULL || param[1] == NULL || param[2] == NULL){
		printk("wrong param\n");
		return len;	
	}
	sscanf(param[0],"%x",(unsigned int *)&dg.r);
	sscanf(param[1],"%x",(unsigned int *)&dg.g);
	sscanf(param[2],"%x",(unsigned int *)&dg.b);	
	adapter = i2c_get_adapter(4);
	my_i2c_put_byte(adapter,0x36,0x5186, (unsigned char)((dg.r >> 8) & 0x000f));
	my_i2c_put_byte(adapter,0x36,0x5187, (unsigned char)((dg.r >> 0) & 0x00ff));
	my_i2c_put_byte(adapter,0x36,0x5188, (unsigned char)((dg.g >> 8) & 0x000f));
	my_i2c_put_byte(adapter,0x36,0x5189, (unsigned char)((dg.g >> 0) & 0x00ff));
	my_i2c_put_byte(adapter,0x36,0x518a, (unsigned char)((dg.b >> 8) & 0x000f));
	my_i2c_put_byte(adapter,0x36,0x518b, (unsigned char)((dg.b >> 0) & 0x00ff));
	my_i2c_put_byte(adapter,0x36,0x5002, 0x41);
	my_i2c_put_byte(adapter,0x36,0x5180, 0x08);
	return len;
}

static ssize_t dg_manual_show(struct class *cls,struct class_attribute *attr, char* buf)
{
	size_t len = 0;
	struct sensor_dg_s dg;
	struct i2c_adapter *adapter;
	unsigned char	dg_r_high,dg_r_low,dg_g_high,dg_g_low,dg_b_high,dg_b_low;
	
	adapter = i2c_get_adapter(4);
	dg_r_high = my_i2c_get_byte(adapter,0x36,0x5186);
	dg_r_low  = my_i2c_get_byte(adapter,0x36,0x5187);
	dg_g_high = my_i2c_get_byte(adapter,0x36,0x5188);
	dg_g_low  = my_i2c_get_byte(adapter,0x36,0x5189);
	dg_b_high = my_i2c_get_byte(adapter,0x36,0x518a);
	dg_b_low  = my_i2c_get_byte(adapter,0x36,0x518b);

  dg.r = ((unsigned short)dg_r_high << 8) | ((unsigned short)dg_r_low << 0);
  dg.g = ((unsigned short)dg_g_high << 8) | ((unsigned short)dg_g_low << 0);
  dg.b = ((unsigned short)dg_b_high << 8) | ((unsigned short)dg_b_low << 0);
  dg.dg_default = 0x0400;
  
	printk("0x%x 0x%x 0x%x 0x%x\n",dg.r,dg.g,dg.b,dg.dg_default);
	return len;
}

static CLASS_ATTR(dg_debug, 0664, dg_manual_show, dg_manual_store);

static ssize_t vcm_manual_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	struct i2c_adapter *adapter;
	char buff[3];
	unsigned int af_step = 0;
	unsigned int diff = 0;
	int codes,vcm_data=0;
	unsigned char byte_h, byte_l;
	sscanf(buf,"%d",&af_step);
    if(af_step == last_af_step)
        return len;
    diff = (af_step > last_af_step) ? af_step - last_af_step : last_af_step - af_step;
    last_af_step = af_step;
    if(diff < 256){
        codes = 1;
    }else if(diff < 512){
        codes = 2;	
    }else
        codes = 3;
    vcm_data |= (codes << 2); // bit[3:2]
    vcm_data |= (last_af_step << 4);  // bit[4:13]
    printk("set vcm step :%x\n",vcm_data);   
    byte_h  = (vcm_data >> 8) & 0x000000ff;
    byte_l  = (vcm_data >> 0) & 0x000000ff;
    buff[0] = byte_h;
    buff[1] = byte_l;
    adapter = i2c_get_adapter(4);
    my_i2c_put_byte_add8(adapter,0x0c,buff,2);
    return len;
}

static ssize_t vcm_manual_show(struct class *cls,struct class_attribute *attr, char* buf)
{
	size_t len = 0;
	struct i2c_adapter *adapter;
	unsigned int af;
	adapter = i2c_get_adapter(4);
	af = my_i2c_get_word(adapter,0x0c);
	printk("current vcm step :%x\n",af);
	return len;
}

static CLASS_ATTR(vcm_debug, 0664, vcm_manual_show, vcm_manual_store);

static ssize_t light_source_freq_manual_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	int freq;
	sscanf(buf,"%d",&freq);
	current_fr = freq ? 1 : 0;
	printk("set current light soure frequency :%d\n",freq);
	return len;
}

static ssize_t light_source_freq_manual_show(struct class *cls,struct class_attribute *attr, char* buf)
{
	size_t len = 0;
	printk("light source frequence :%d\n",current_fr);
	return len;
}

static CLASS_ATTR(light_source_debug, 0664, light_source_freq_manual_show, light_source_freq_manual_store);

//load OV5647 parameters
void OV5647_init_regs(struct ov5647_device *dev)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    int i=0;

	while (1) {
		if (OV5647_script[i].val==0xff&&OV5647_script[i].addr==0xffff)
		{
			printk("OV5647_write_regs success in initial OV5647.\n");
			break;
		}
		if((i2c_put_byte(client,OV5647_script[i].addr, OV5647_script[i].val)) < 0)
		{
	 		printk("fail in initial OV5647. \n");
			return;
		}
		i++;
	}
	
	return;
}
/*init for vcm  mode0:LSC mode1:DLC*/
static void dw9714_init(unsigned char mode)
{
	char buf[3];
	unsigned short dlc[4] = {
		0xeca3,0xf248,0xa10d,0xdc51
	};
	struct i2c_adapter *adapter;
	adapter = i2c_get_adapter(4);
	if(mode){
		buf[0]=dlc[0]>>8&&0xff;
		buf[1]=dlc[0]&&0xff;
    	        my_i2c_put_byte_add8(adapter,0x0c,buf,2);
		buf[0]=dlc[1]>>8&&0xff;
		buf[1]=dlc[1]&&0xff;
    	        my_i2c_put_byte_add8(adapter,0x0c,buf,2);
		buf[0]=dlc[2]>>8&&0xff;
		buf[1]=dlc[2]&&0xff;
    	        my_i2c_put_byte_add8(adapter,0x0c,buf,2);
		buf[0]=dlc[3]>>8&&0xff;
		buf[1]=dlc[3]&&0xff;
    	        my_i2c_put_byte_add8(adapter,0x0c,buf,2);
	}
}
/* power down for dw9714*/
static void dw9714_uninit(void)
{
    char buf[3];
    struct i2c_adapter *adapter;
	buf[0] = 0x80;
	buf[1] = 0x0;
	adapter = i2c_get_adapter(4);
	my_i2c_put_byte_add8(adapter,0x0c,buf,2);	
}



static ssize_t version_info_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	return len;
}

static ssize_t version_info_show(struct class *cls,struct class_attribute *attr, char* buf)
{
	size_t len = 0;
    if(cf->version_info_valid == 0)
        printk("verion info envalid\n");
    else{
        printk("Date %s",cf->version.date);
        printk("Module %s",cf->version.module);
        printk("Version %s",cf->version.version);	
    }
    return len;
}

static CLASS_ATTR(version_debug, 0664, version_info_show, version_info_store);
/*************************************************************************
* FUNCTION
*    OV5647_set_param_wb
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

static wb_pair_t wb_pair[] = {
    {CAM_WB_AUTO,"CAM_WB_AUTO"},
    {CAM_WB_CLOUD,"CAM_WB_CLOUD"},
    {CAM_WB_DAYLIGHT,"CAM_WB_DAYLIGHT"},
    {CAM_WB_INCANDESCENCE,"CAM_WB_INCANDESCENCE"},
    {CAM_WB_TUNGSTEN,"CAM_WB_TUNGSTEN"},
    {CAM_WB_FLUORESCENT,"CAM_WB_FLUORESCENT"},
    {CAM_WB_MANUAL,"CAM_WB_MANUAL"},
    {CAM_WB_SHADE,"CAM_WB_SHADE"},
    {CAM_WB_TWILIGHT,"CAM_WB_TWILIGHT"},
    {CAM_WB_WARM_FLUORESCENT,"CAM_WB_WARM_FLUORESCENT"},
};

void OV5647_set_param_wb(struct ov5647_device *dev,enum  camera_wb_flip_e para)//white balance
{
    int index = 0;
    int i = 0;
    while(i < ARRAY_SIZE(wb_pair)){
        if(wb_pair[i].wb == para){
            break;
        }else
            i++;
    }
    if(i == ARRAY_SIZE(wb_pair)){
        printk("not support\n");
        return;
    }
    if(dev->configure != NULL && dev->configure->wb_valid == 1){
        while(index < dev->configure->wb.sum){
            if(strcmp(wb_pair[i].name, dev->configure->wb.wb[index].name) == 0){
                break;	
            }
            index++;
        }
        if(index == dev->configure->wb.sum){
            printk("invalid wb value\n");
            return;	
        }
        if(para == CAM_WB_AUTO){
            printk("auto wb\n");
            dev->cam_para->cam_command = CAM_COMMAND_AWB;
        }else{
            dev->cam_para->cam_command = CAM_COMMAND_MWB;
        }
	memcpy(dev->cam_para->xml_wb_manual->reg_map,dev->configure->wb.wb[index].export,WB_MAX * sizeof(int));
        printk("set wb :%d\n",index);
        dev->fe_arg.port = TVIN_PORT_ISP;
        dev->fe_arg.index = 0;
        dev->fe_arg.arg = (void *)(dev->cam_para);
        dev->vops->tvin_fe_func(0,&dev->fe_arg);
    }else{
        return;	
    }


} /* OV5647_set_param_wb */
/*************************************************************************
 * FUNCTION
 *    OV5647_set_param_exposure
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
void OV5647_set_param_exposure(struct ov5647_device *dev,enum camera_exposure_e para)
{
    //struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    int value;
    if(0){//para == EXPOSURE_0_STEP){
        dev->cam_para->cam_command = CAM_COMMAND_AE_ON;
        dev->ae_on = true;
    }else{
        if(dev->ae_on == false){ // set ae on
            dev->cam_para->cam_command = CAM_COMMAND_AE_ON;	
            dev->fe_arg.port = TVIN_PORT_ISP;
            dev->fe_arg.index = 0;
            dev->fe_arg.arg = (void *)(dev->cam_para);
            dev->vops->tvin_fe_func(0,&dev->fe_arg);	
            dev->ae_on = true;
        }
        value = para < 8 ? para : 7;
        value = value > 0 ? value : 1;
        value -= 4;
        dev->cam_para->cam_command = CAM_COMMAND_SET_AE_LEVEL;
        dev->cam_para->exposure_level = value;
        printk("set manual exposure level:%d\n",value);
    }
    dev->fe_arg.port = TVIN_PORT_ISP;
    dev->fe_arg.index = 0;
    dev->fe_arg.arg = (void *)(dev->cam_para);
    dev->vops->tvin_fe_func(0,&dev->fe_arg);
} /* ov5647_set_param_exposure */
/*************************************************************************
 * FUNCTION
 *    OV5647_set_param_effect
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
static effect_pair_t effect_pair[] = {
    {SPECIAL_EFFECT_NORMAL,"CAM_EFFECT_ENC_NORMAL"},
    {SPECIAL_EFFECT_BW,NULL},
    {SPECIAL_EFFECT_BLUISH,"CAM_EFFECT_ENC_SEPIABLUE"},
    {SPECIAL_EFFECT_SEPIA,"CAM_EFFECT_ENC_SEPIA"},
    {SPECIAL_EFFECT_REDDISH,NULL},
    {SPECIAL_EFFECT_GREENISH,"CAM_EFFECT_ENC_SEPIAGREEN"},
    {SPECIAL_EFFECT_NEGATIVE,"CAM_EFFECT_ENC_COLORINV"}
};

void OV5647_set_param_effect(struct ov5647_device *dev,enum camera_effect_flip_e para)
{
    int index = 0;
    int i = 0;
    while(i < ARRAY_SIZE(effect_pair)){
        if((unsigned int)effect_pair[i].effect == (unsigned int)para){
            break;
        }else
            i++;
    }
    if(i == ARRAY_SIZE(effect_pair)){
        printk("not support\n");
        return;
    }
    if(dev->configure != NULL && dev->configure->effect_valid == 1){
        while(index < dev->configure->eff.sum){
            if(strcmp(effect_pair[i].name, dev->configure->eff.eff[index].name) == 0){
                break;	
            }
            index++;
        }
        if(index == dev->configure->eff.sum){
            printk("invalid effect value\n");
            return;	
        }
        dev->cam_para->cam_command = CAM_COMMAND_EFFECT;
        memcpy(dev->cam_para->xml_effect_manual->csc.reg_map,dev->configure->eff.eff[index].export,EFFECT_MAX * sizeof(unsigned int));

        dev->fe_arg.port = TVIN_PORT_ISP;
        dev->fe_arg.index = 0;
        dev->fe_arg.arg = (void *)(dev->cam_para);
        dev->vops->tvin_fe_func(0,&dev->fe_arg);
        return;
    } 

} /* OV5647_set_param_effect */

/*************************************************************************
 * FUNCTION
 *    OV5647_NightMode
 *
 * DESCRIPTION
 *    This function night mode of OV5647.
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
void OV5647_set_night_mode(struct ov5647_device *dev,enum  camera_night_mode_flip_e enable)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    if (50) {
        i2c_put_byte(client,0x03a14 , 0xc8); //Camera Enable night mode  1/5 Frame rate
        i2c_put_byte(client,0x03a15 , 0xc8); //Camera Enable night mode  1/5 Frame rate
    }
    else{
        i2c_put_byte(client,0x3a02 , 0x98); //Disable night mode  1/2 Frame rate
        i2c_put_byte(client,0x3a03 , 0x98); //Disable night mode  1/2 Frame rate
    }

}   /* OV5647_NightMode */

static void OV5647_set_param_banding(struct ov5647_device *dev,enum  camera_banding_flip_e banding)
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
        default:
            break;
    }
}

static int OV5647_AutoFocus(struct ov5647_device *dev, int focus_mode)
{
    //struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    int ret = 0;

    switch (focus_mode) {
        case CAM_FOCUS_MODE_AUTO:       
            printk("auto focus mode start\n");
            bDoingAutoFocusMode = true;
            dev->cam_para->cam_command = CAM_COMMAND_FULLSCAN;
            dev->fe_arg.port = TVIN_PORT_ISP;
            dev->fe_arg.index = 0;
            dev->fe_arg.arg = (void *)(dev->cam_para);
            if(dev->vops != NULL){
                dev->vops->tvin_fe_func(0,&dev->fe_arg);
            }
            break;
        case CAM_FOCUS_MODE_CONTI_VID:
        case CAM_FOCUS_MODE_CONTI_PIC:
            printk("continus focus\n");
            dev->cam_para->cam_command = CAM_COMMAND_CONTINUOUS_FOCUS_ON;
            dev->fe_arg.port = TVIN_PORT_ISP;
            dev->fe_arg.index = 0;
            dev->fe_arg.arg = (void *)(dev->cam_para);
            if(dev->vops != NULL){
                dev->vops->tvin_fe_func(0,&dev->fe_arg);
            }
            printk("start continous focus\n");
            break;

        case CAM_FOCUS_MODE_RELEASE:
        case CAM_FOCUS_MODE_FIXED:
            printk("continus focus\n");
            dev->cam_para->cam_command = CAM_COMMAND_CONTINUOUS_FOCUS_OFF;
            dev->fe_arg.port = TVIN_PORT_ISP;
            dev->fe_arg.index = 0;
            dev->fe_arg.arg = (void *)(dev->cam_para);
            if(dev->vops != NULL){
                dev->vops->tvin_fe_func(0,&dev->fe_arg);
            }
            printk("focus release\n");
            break;
        default:
            printk("release focus to infinit\n");
            break;
    }
    return ret;
}

static int set_flip(struct ov5647_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	unsigned char temp;
	temp = i2c_get_byte(client, 0x3821);
	temp &= 0xf9;
	temp |= dev->cam_info.m_flip << 1 | dev->cam_info.m_flip << 2;
	if((i2c_put_byte(client, 0x3821, temp)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
        }
	temp = i2c_get_byte(client, 0x3820);
	temp &= 0xf9;
	temp |= dev->cam_info.v_flip << 1 | dev->cam_info.v_flip << 2;
	if((i2c_put_byte(client, 0x3820, temp)) < 0) {
		printk("fail in setting sensor orientation\n");
		return -1;
        }
        
        return 0;
}

static resolution_size_t get_size_type(int width, int height)
{
	resolution_size_t rv = SIZE_NULL;
	if (width * height >= 2500 * 1900)
		rv = SIZE_2592X1944;
	else if (width * height >= 2048 * 1536)
		rv = SIZE_2048X1536;
	else if (width * height >= 1920 * 1080)
		rv = SIZE_1920X1080;
	else if (width * height >= 1600 * 1200)
		rv = SIZE_1600X1200;
	else if (width * height >= 1280 * 960)
		rv = SIZE_1280X960;
	else if (width * height >= 1280 * 720)
		rv = SIZE_1280X720;
	else if (width * height >= 1024 * 768)
		rv = SIZE_1024X768;
	else if (width * height >= 800 * 600)
		rv = SIZE_800X600;
	else if (width * height >= 640 * 4480)
		rv = SIZE_640X480;
	else if (width * height >= 352 * 288)
		rv = SIZE_352X288;
	else if (width * height >= 320 * 240)
		rv = SIZE_320X240;
	else if (width * height >= 176 * 144)
		rv = SIZE_176X144;
	return rv;
}

static int OV5647_FlashCtrl(struct ov5647_device *dev, int flash_mode)
{
    switch (flash_mode) {
        case FLASHLIGHT_ON:
        case FLASHLIGHT_AUTO:
        case FLASHLIGHT_OFF:
            dev->cam_para->cam_command = CAM_COMMAND_SET_FLASH_MODE;
            dev->cam_para->flash_mode = flash_mode;
            break;
        case FLASHLIGHT_TORCH:
            dev->cam_para->cam_command = CAM_COMMAND_TORCH;
            dev->cam_para->level = 100;
            break;
        default:
            printk("this flash mode not support yet\n");
            return -1;
    }
    dev->fe_arg.port = TVIN_PORT_ISP;
    dev->fe_arg.index = 0;
    dev->fe_arg.arg = (void *)(dev->cam_para);
    if(dev->vops != NULL){
        dev->vops->tvin_fe_func(0,&dev->fe_arg);
    }
    return 0;
}

static resolution_param_t* get_resolution_param(struct ov5647_device *dev, int ov5647_work_mode, int width, int height)
{
    int i = 0;
    int arry_size = 0;
    resolution_param_t* tmp_resolution_param = NULL;
    resolution_size_t res_type = SIZE_NULL;
    printk("target resolution is %dX%d\n", width, height);
    res_type = get_size_type(width, height);
    if (res_type == SIZE_NULL)
        return NULL;
    if (ov5647_work_mode == CAMERA_CAPTURE) {
        tmp_resolution_param = capture_resolution_array;
        arry_size = ARRAY_SIZE(capture_resolution_array);
    } else {
        tmp_resolution_param = debug_prev_resolution_array;
        arry_size = ARRAY_SIZE(debug_prev_resolution_array);
    }
    for (i = 0; i < arry_size; i++) {
        if (tmp_resolution_param[i].size_type == res_type)
            return &tmp_resolution_param[i];
    }
    return NULL;
}


void set_resolution_param(struct ov5647_device *dev, resolution_param_t* res_param)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    //int rc = -1;
    int i=0;
    unsigned char t = dev->cam_info.interface;
	int default_sensor_data[4] = {0x00000668,0x00000400,0x00000400,0x00000878};
    int *sensor_data;
    int addr_start = 0x5186;
    int data = 0;
    int index = 0;

    if(i_index != -1 && ov5647_work_mode != CAMERA_CAPTURE){
    	printk("i_index is %d\n", i_index);
        res_param = &debug_prev_resolution_array[i_index];
    }
    if (!res_param->reg_script[t]) {
        printk("error, resolution reg script is NULL\n");
        return;
    }
    while(1){
        if (res_param->reg_script[t][i].val==0xff&&res_param->reg_script[t][i].addr==0xffff) {
            printk("setting resolutin param complete\n");
            break;
        }
        if((i2c_put_byte(client,res_param->reg_script[t][i].addr, res_param->reg_script[t][i].val)) < 0) {
            printk("fail in setting resolution param. i=%d\n",i);
            break;
        }
        if(res_param->reg_script[t][i].addr == 0x0103) //soft reset,need 5ms delay
        	msleep(5);
        i++;
    }
    
    set_flip(dev);

    if(dev->configure->wb_sensor_data_valid == 1){
        sensor_data = dev->configure->wb_sensor_data.export;
    }else
        sensor_data = default_sensor_data;
    for(i = 0;i < (WB_SENSOR_MAX - 1) * 2;){ // current only rgb valid
        data = (sensor_data[index] >> 8) & 0x0f;
        if((i2c_put_byte(client, addr_start + i, data) < 0)) {
            printk("fail in setting resolution param. i=%d\n",i);
            break;
        }
        data = (sensor_data[index]) & 0xff;
        if((i2c_put_byte(client, addr_start + i + 1, data) < 0)) {
            printk("fail in setting resolution param. i=%d\n",i);
            break;
        }
        if(index == 1)
            index += 2;
        else
            index++;
        i += 2;
    }
    ov5647_frmintervals_active.numerator = 1;
    ov5647_frmintervals_active.denominator = res_param->active_fps;
    ov5647_h_active = res_param->active_frmsize.width;
    ov5647_v_active = res_param->active_frmsize.height;
    OV5647_set_new_format((void *)&dev->camera_priv_data,ov5647_h_active,ov5647_v_active,current_fr);// should set new para
}    /* OV5647_set_resolution */

static int set_focus_zone(struct ov5647_device *dev, int value)
{
	int xc, yc, tx, ty;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//int retry_count = 10;
	//int ret = -1;
	
	xc = (value >> 16) & 0xffff;
	yc = (value & 0xffff);
	if(xc == 1000 && yc == 1000)
		return 0;
	tx = xc * ov5647_h_active /2000;
	ty = yc * ov5647_v_active /2000;
	printk("xc = %d, yc = %d, tx = %d , ty = %d \n", xc, yc, tx, ty);
	
	dev->cam_para->xml_scenes->af.x = tx;
	dev->cam_para->xml_scenes->af.y = ty;	
	dev->cam_para->cam_command = CAM_COMMAND_TOUCH_FOCUS;
	dev->fe_arg.port = TVIN_PORT_ISP;
	dev->fe_arg.index = 0;
	dev->fe_arg.arg = (void *)(dev->cam_para);
	if(dev->vops != NULL){
	  dev->vops->tvin_fe_func(0,&dev->fe_arg);
	}
	return 0;
}

unsigned char v4l_2_ov5647(int val)
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

static int ov5647_setting(struct ov5647_device *dev,int PROP_ID,int value )
{
	int ret=0;
        struct vb2_queue *q = &dev->vb_vidq;
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_ov5647(value));
		break;
	case V4L2_CID_CONTRAST:
		break;
	case V4L2_CID_SATURATION:
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */
		value = value & 0x1;
		if(ov5647_qctrl[2].default_value!=value){
			ov5647_qctrl[2].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
			value = value << 1; //bit[1]
			ret=i2c_put_byte(client,0x3821, value);
			break;
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		if(ov5647_qctrl[4].default_value!=value){
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
			ov5647_qctrl[4].default_value=value;
			if(vb2_is_streaming(q))
				OV5647_set_param_wb(dev,value);
		}
		break;
	case V4L2_CID_EXPOSURE:
		if(ov5647_qctrl[5].default_value!=value){
			ov5647_qctrl[5].default_value=value;
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
			if(vb2_is_streaming(q)) {
				OV5647_set_param_exposure(dev,value);
			}
		}
		break;
	case V4L2_CID_BACKLIGHT_COMPENSATION:
		if(ov5647_qctrl[9].default_value!=value){
			ov5647_qctrl[9].default_value=value;
			ret = OV5647_FlashCtrl(dev,value);
			printk(KERN_INFO " set light compensation =%d. \n ",value);
		}
		break;
	case V4L2_CID_COLORFX:
		if(ov5647_qctrl[6].default_value!=value){
			ov5647_qctrl[6].default_value=value;
			if(vb2_is_streaming(q))
				OV5647_set_param_effect(dev,value);
		}
		break;
	case V4L2_CID_WHITENESS:
		if(ov5647_qctrl[3].default_value!=value){
			ov5647_qctrl[3].default_value=value;
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
			OV5647_set_param_banding(dev,value);
			
		}
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(ov5647_qctrl[10].default_value!=value){
			ov5647_qctrl[10].default_value=value;
			printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(ov5647_qctrl[11].default_value!=value){
			ov5647_qctrl[11].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		printk("V4L2_CID_FOCUS_ABSOLUTE\n");
		if(ov5647_qctrl[13].default_value!=value){
			ov5647_qctrl[13].default_value=value;
			printk(" set camera  focus zone =%d. \n ",value);
			if(vb2_is_streaming(q)) {
				set_focus_zone(dev, value);
			}
		}
		break;
	case V4L2_CID_FOCUS_AUTO:
		printk("V4L2_CID_FOCUS_AUTO\n");
		if(ov5647_qctrl[8].default_value!=value){
			ov5647_qctrl[8].default_value=value;
			if(vb2_is_streaming(q)) {
				OV5647_AutoFocus(dev,value);
			}
		}
	case V4L2_CID_PRIVACY:       
		break;
	default:
   		ret=-1;
		break;
	}
	
	return ret;

}


/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/
extern int vm_fill_buffer2(struct vb2_buffer *vb, vm_output_para_t *para);
static void ov5647_fillbuff(struct ov5647_device *dev, struct ov5647_buffer *buf)
{
	void *vbuf = vb2_plane_cookie(&buf->vb, 0);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
	/*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	if(buf->canvas_id == 0)
	buf->canvas_id = convert_canvas_index(dev->fmt->fourcc, OV5647_RES0_CANVAS_INDEX+buf->vb.v4l2_buf.index*3);
	para.mirror = ov5647_qctrl[2].default_value&3;// not set
	para.v4l2_format = dev->fmt->fourcc;
	para.v4l2_memory = MAGIC_RE_MEM;//0x18221223;
	para.zoom = ov5647_qctrl[10].default_value;
	para.angle = ov5647_qctrl[11].default_value;
	para.vaddr = (unsigned)vbuf;
	para.ext_canvas = buf->canvas_id;
	para.width = dev->width;
	para.height = dev->height;
	vm_fill_buffer2(&buf->vb, &para);
}

static void ov5647_thread_tick(struct ov5647_device *dev)
{
	struct ov5647_buffer *buf;
	struct ov5647_dmaqueue *dma_q = &dev->vidq;
	struct vb2_queue *q = &dev->vb_vidq;

	unsigned long flags = 0;

	dprintk(dev, 1, "Thread tick\n");
	if(!vb2_is_streaming(q)){
		dprintk(dev, 1, "sensor doesn't stream on\n");
		return ;
	}

	spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&dma_q->active)) {
		dprintk(dev, 1, "No active queue to serve\n");
		spin_unlock_irqrestore(&dev->slock, flags);
		return;
	}

    buf = list_entry(dma_q->active.next, struct ov5647_buffer, list);
    dprintk(dev, 1, "%s\n", __func__);
    dprintk(dev, 1, "list entry get buf is %x\n",(unsigned)buf);

	list_del(&buf->list);

	/* Fill buffer */
	spin_unlock_irqrestore(&dev->slock, flags);
	v4l2_get_timestamp(&buf->vb.v4l2_buf.timestamp);
	ov5647_fillbuff(dev, buf);
	dprintk(dev, 1, "filled buffer %p\n", buf);

	vb2_buffer_done(&buf->vb, VB2_BUF_STATE_DONE);
	dprintk(dev, 2, "[%p/%d] wakeup\n", buf, buf->vb.v4l2_buf.index);
	return;
}

static void ov5647_sleep(struct ov5647_device *dev)
{
	struct ov5647_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	ov5647_thread_tick(dev);

	schedule_timeout_interruptible(1);//if fps > 25 , 2->1

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int ov5647_thread(void *data)
{
	struct ov5647_device *dev = data;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		ov5647_sleep(dev);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int ov5647_start_generating(struct ov5647_device *dev)
{
        struct ov5647_dmaqueue *dma_q = &dev->vidq;
        vdin_parm_t para;
        int ret = 0 ;

        dma_q->frame = 0;
        dma_q->ini_jiffies = jiffies;

        dprintk(dev, 1, "%s\n", __func__);

        //start_tvin_service
        if (capture_proc) {
                if (dev->is_vdin_start)
                        goto start;
        }

        memset( &para, 0, sizeof( para ));
        if (CAM_MIPI == dev->cam_info.interface) {
                para.isp_fe_port  = TVIN_PORT_MIPI;
        } else {
                para.isp_fe_port  = TVIN_PORT_CAMERA;
        }
        para.port  = TVIN_PORT_ISP;
        para.fmt = TVIN_SIG_FMT_MAX;
        para.frame_rate = ov5647_frmintervals_active.denominator;
        para.h_active = ov5647_h_active;
        para.v_active = ov5647_v_active;
        if(ov5647_work_mode != CAMERA_CAPTURE){
                para.skip_count = 8;
                para.dest_hactive = dest_hactive;
                para.dest_vactive = dest_vactive;
        }else{
                para.dest_hactive = 0;
                para.dest_vactive = 0;
        }
        dev->cam_para->cam_mode = ov5647_work_mode;
        para.hsync_phase = 1;
        para.vsync_phase  = 1;
        para.hs_bp = 0;
        para.vs_bp = 2;
        para.cfmt = dev->cam_info.bayer_fmt;
        para.dfmt = TVIN_NV21;
        para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
        para.bt_path = dev->cam_info.bt_path;
        current_fmt = 0;
        if(dev->cam_para == NULL)
                return -EINVAL;
        if(update_fmt_para(ov5647_h_active,ov5647_v_active,dev->cam_para,&dev->pindex,dev->configure) != 0)
                return -EINVAL;
        para.reserved = (int)(dev->cam_para);
        if (CAM_MIPI == dev->cam_info.interface)
        {
                para.csi_hw_info.lanes = 2;
                para.csi_hw_info.channel = 1;
                para.csi_hw_info.mode = 1;
                para.csi_hw_info.clock_lane_mode = 1; // 0 clock gate 1: always on
                para.csi_hw_info.active_pixel = ov5647_h_active;
                para.csi_hw_info.active_line = ov5647_v_active;
                para.csi_hw_info.frame_size=0;
                para.csi_hw_info.ui_val = 2; //ns
                para.csi_hw_info.urgent = 1;
                para.csi_hw_info.clk_channel = dev->cam_info.clk_channel; //clock channel a or b
        }
        if(dev->configure->aet_valid == 1){
                dev->cam_para->xml_scenes->ae.aet_fmt_gain = (dev->camera_priv_data).sensor_aet_info->format_transfer_parameter;
        }
        else
                dev->cam_para->xml_scenes->ae.aet_fmt_gain = 100;
        printk("aet_fmt_gain:%d\n",dev->cam_para->xml_scenes->ae.aet_fmt_gain);
        printk("ov5647,h=%d, v=%d, dest_h:%d, dest_v:%d,frame_rate=%d,\n",
                        ov5647_h_active, ov5647_v_active, para.dest_hactive,para.dest_vactive,ov5647_frmintervals_active.denominator);
        if(ret == 0){
                dev->vops->start_tvin_service(0,&para);
                dev->is_vdin_start      = 1;

        }
        /*** 		set cm2 		***/
        dev->vdin_arg.cmd = VDIN_CMD_SET_CM2;
        dev->vdin_arg.cm2 = dev->configure->cm.export;
        dev->vops->tvin_vdin_func(0,&dev->vdin_arg);

        OV5647_set_param_wb(dev,ov5647_qctrl[4].default_value);
        OV5647_set_param_exposure(dev,ov5647_qctrl[5].default_value);
        OV5647_set_param_effect(dev,ov5647_qctrl[6].default_value);
        OV5647_AutoFocus(dev, ov5647_qctrl[8].default_value);
        ////already start tvin service.

start:
        dma_q->kthread = kthread_run(ov5647_thread, dev, "%s",
                        dev->v4l2_dev.name);

        if (IS_ERR(dma_q->kthread)) {
                v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
                return PTR_ERR(dma_q->kthread);
        }
        /* Wakes thread */
        wake_up_interruptible(&dma_q->wq);

        dprintk(dev, 1, "returning from %s\n", __func__);
        return 0;
}

static void ov5647_stop_generating(struct ov5647_device *dev)
{
        struct ov5647_dmaqueue *dma_q = &dev->vidq;
        int ret = 0 ;

        dprintk(dev, 1, "%s\n", __func__);
        /* shutdown control thread */
        if (dma_q->kthread) {
                kthread_stop(dma_q->kthread);
                dma_q->kthread = NULL;
        }

        /*
         * Typical driver might need to wait here until dma engine stops.
         * In this case we can abort imiedetly, so it's just a noop.
         */
        /* Release all active buffers */
        while (!list_empty(&dma_q->active)) {
                struct ov5647_buffer *buf;
                buf = list_entry(dma_q->active.next, struct ov5647_buffer, list);
                list_del(&buf->list);
                vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
                dprintk(dev, 2, "[%p/%d] done\n", buf, buf->vb.v4l2_buf.index);
        }

        //to be continued... stop_tvin_service
        printk(KERN_INFO "stop tvin service\n ");
        last_exp_h = 0;
        last_exp_m = 0;
        last_exp_l = 0;
        last_ag_h = 0;
        last_ag_l = 0;
        last_vts_h = 0;
        last_vts_l = 0;

        if (capture_proc) {
                printk("in capture process\n");
                return ;
        }
        if(ret == 0 ){
                dev->vops->stop_tvin_service(0);
                dev->is_vdin_start      = 0;
        }
        dev->ae_on = false;

}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
                                unsigned int *nbuffers, unsigned int *nplanes,
                                unsigned int sizes[], void *alloc_ctxs[])
{
        struct ov5647_device *dev = vb2_get_drv_priv(vq);
        unsigned long size;
        int width = dev->width;
        int height = dev->height;

        if (1080 == height)
                height = 1088;
				
        if (fmt)
                size = fmt->fmt.pix.sizeimage;
        else
                size = (width * height * dev->fmt->depth)>>3; 

        if (size == 0)
                return -EINVAL;

        if (0 == *nbuffers)
                *nbuffers = 32;

        while (size * *nbuffers > vid_limit * 1024 * 1024)
                (*nbuffers)--;

        *nplanes = 1;

        sizes[0] = size;

        /*
         * videobuf2-vmalloc allocator is context-less so no need to set
         * alloc_ctxs array.
         */
         //to be continued...

        dprintk(dev, 1, "%s, count=%d, size=%ld\n", __func__,
                *nbuffers, size);

        return 0;
}


#define norm_maxw() 3000
#define norm_maxh() 3000
static int buffer_prepare(struct vb2_buffer *vb)
{
	struct ov5647_device *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct ov5647_buffer *buf = container_of(vb, struct ov5647_buffer, vb);
	unsigned long size;

	dprintk(dev, 1, "%s, field=%d\n", __func__, vb->v4l2_buf.field);

	BUG_ON(NULL == dev->fmt);
        /*
         * Theses properties only change when queue is idle, see s_fmt.
         * The below checks should not be performed here, on each
         * buffer_prepare (i.e. on each qbuf). Most of the code in this function
         * should thus be moved to buffer_init and s_fmt.
         */
				if (dev->width  < 48 || dev->width  > norm_maxw() ||
	    			dev->height < 32 || dev->height > norm_maxh())
									return -EINVAL;

				size = (dev->width*dev->height*dev->fmt->depth)>>3;
	      if (vb2_plane_size(vb, 0) < size) {
                dprintk(dev, 1, "%s data will not fit into plane (%lu < %lu)\n",
                                __func__, vb2_plane_size(vb, 0), size);
                return -EINVAL;
        }

        vb2_set_plane_payload(&buf->vb, 0, size);

        buf->fmt = dev->fmt;

        return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
				struct ov5647_buffer    *buf  = container_of(vb, struct ov5647_buffer, vb);
				struct ov5647_device    *dev  = vb2_get_drv_priv(vb->vb2_queue);
				struct ov5647_dmaqueue 	*vidq = &dev->vidq;
        unsigned long flags = 0;

        dprintk(dev, 1, "%s\n", __func__);

        spin_lock_irqsave(&dev->slock, flags);
        list_add_tail(&buf->list, &vidq->active);
        spin_unlock_irqrestore(&dev->slock, flags);
}
static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
        struct ov5647_device *dev = vb2_get_drv_priv(vq);
        dprintk(dev, 1, "%s\n", __func__);
        return ov5647_start_generating(dev);
}

/* abort streaming and wait for last buffer */
static int stop_streaming(struct vb2_queue *vq)
{
        struct ov5647_device *dev = vb2_get_drv_priv(vq);
        dprintk(dev, 1, "%s\n", __func__);
        ov5647_stop_generating(dev);
        return 0;
}

static void ov5647_lock(struct vb2_queue *vq)
{
        struct ov5647_device *dev = vb2_get_drv_priv(vq);
        mutex_lock(&dev->mutex);
}

static void ov5647_unlock(struct vb2_queue *vq)
{
        struct ov5647_device *dev = vb2_get_drv_priv(vq);
        mutex_unlock(&dev->mutex);
}


static const struct vb2_ops ov5647_video_qops = {
        .queue_setup            = queue_setup,
        .buf_prepare            = buffer_prepare,
        .buf_queue              = buffer_queue,
        .start_streaming        = start_streaming,
        .stop_streaming         = stop_streaming,
        .wait_prepare           = ov5647_unlock,
        .wait_finish            = ov5647_lock,
};
/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct ov5647_device *dev = video_drvdata(file);

	strcpy(cap->driver, "ov5647");
	strcpy(cap->card, "ov5647.canvas");
	snprintf(cap->bus_info, sizeof(cap->bus_info),
           "platform:%s", dev->v4l2_dev.name);
	cap->version = OV5647_CAMERA_VERSION;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
                     V4L2_CAP_READWRITE | V4L2_CAP_VIDEO_M2M;
        printk("ov5647 work in ion mode\n");
        cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	const struct ov5647_fmt *fmt;

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

    if(fival->index > ARRAY_SIZE(ov5647_frmivalenum))
        return -EINVAL;

    for(k =0; k< ARRAY_SIZE(ov5647_frmivalenum); k++)
    {
        if( (fival->index==ov5647_frmivalenum[k].index)&&
                (fival->pixel_format ==ov5647_frmivalenum[k].pixel_format )&&
                (fival->width==ov5647_frmivalenum[k].width)&&
                (fival->height==ov5647_frmivalenum[k].height)){
            memcpy( fival, &ov5647_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
            return 0;
        }
    }
    return -EINVAL;

}

static int vidioc_s_crop(struct file *file, void *fh,
					const struct v4l2_crop *a)
{
	if (a->c.width == 0 && a->c.height == 0) {
		printk("disable capture proc\n");
		capture_proc = 0;
	} else {
		printk("enable capture proc\n");
		capture_proc = 1;
	}

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct ov5647_device *dev = video_drvdata(file);

	f->fmt.pix.width        = dev->width;
	f->fmt.pix.height       = dev->height;
	f->fmt.pix.pixelformat  = dev->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * dev->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	return (0);
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct ov5647_device *dev = video_drvdata(file);
	const struct ov5647_fmt *fmt;
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
        struct ov5647_device *dev = video_drvdata(file);
        struct vb2_queue *q = &dev->vb_vidq;
        int ret = 0;
        resolution_param_t* res_param = NULL;

        f->fmt.pix.width = (f->fmt.pix.width + (CANVAS_WIDTH_ALIGN-1) ) & (~(CANVAS_WIDTH_ALIGN-1));
        if ((f->fmt.pix.pixelformat==V4L2_PIX_FMT_YVU420) ||
                        (f->fmt.pix.pixelformat==V4L2_PIX_FMT_YUV420)){
                f->fmt.pix.width = (f->fmt.pix.width + (CANVAS_WIDTH_ALIGN*2-1) ) & (~(CANVAS_WIDTH_ALIGN*2-1));
        }
        
        ret = vidioc_try_fmt_vid_cap(file, priv, f);
        if (ret < 0)
                return ret;

        if (vb2_is_busy(q)) {
                dprintk(dev, 1, "%s queue busy\n", __func__);
                return -EBUSY;
        }

        dev->fmt           = get_format(f);
        dev->width         = f->fmt.pix.width;
        dev->height        = f->fmt.pix.height;
        if(f->fmt.pix.pixelformat==V4L2_PIX_FMT_RGB24){
                ov5647_work_mode = CAMERA_CAPTURE;
        } else {
                printk("preview resolution is %dX%d\n",dev->width,  dev->height);
                if (0 == capture_proc){
                        ov5647_work_mode = CAMERA_RECORD;
                }else {
                        ov5647_work_mode = CAMERA_PREVIEW;
                }

                if (0 == dev->is_vdin_start) {
                        printk("loading sensor setting\n");
                        res_param = get_resolution_param(dev, 0, dev->width,dev->height);
                        if (!res_param) {
                                printk("error, resolution param not get\n");
                                return -EINVAL;
                        }
                        set_resolution_param(dev, res_param);
                        /** set target ***/
                        if(t_index == -1){
                                dest_hactive = 0;
                                dest_vactive = 0;
                        }
                }
        }

        return 0;
}

static int vidioc_g_parm(struct file *file, void *priv,
        struct v4l2_streamparm *parms)
{
    struct ov5647_device *dev = video_drvdata(file);
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = ov5647_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}

//to be continued...
static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
        struct ov5647_device *dev = video_drvdata(file);

        int ret = vb2_ioctl_querybuf(file, priv, p);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
        if(ret == 0){
                p->reserved  = convert_canvas_index(dev->fmt->fourcc, OV5647_RES0_CANVAS_INDEX+p->index*3);
        }else{
                p->reserved = 0;
        }
#endif		
        return ret;
}

char *res_size[]={
	"cif",
	"320p"
	"480p",
	"720p",
	"960p",
	"1080p",
	"5m",
};
static int get_index(char *res){
	int i = 0;
	while(i < ARRAY_SIZE(res_size)){
		if(strcmp(res_size[i],res) == 0){
			break;
		}
		else
			i++;	
	}
	if(i < ARRAY_SIZE(res_size)){
		return i;	
	}else
		return -1;
}

static ssize_t manual_format_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	char input[20];
	char target[20];
	char *param[3] = {NULL};
	resolution_param_t *res_param;
	
	parse_param(buf,&param[0]);
	if(param[0] == NULL || param[1] == NULL){
		printk("wrong param\n");
		return len;	
	}
	sscanf(param[0],"%s",input);
	sscanf(param[1],"%s",target);
	i_index = get_index(input);
	t_index = get_index(target);
	printk("i:%d,t:%x\n",i_index,t_index);
	if(i_index < 0 || t_index <0){
		printk("wrong res\n");
		return len;
	}
	res_param = &debug_prev_resolution_array[t_index];	
	dest_hactive = res_param->active_frmsize.width;
    dest_vactive = res_param->active_frmsize.height;
    printk("d_h:%d,d_v:%d\n",dest_hactive,dest_vactive);
    return len;
}

static ssize_t manual_format_show(struct class *cls,struct class_attribute *attr, char* buf)
{

	size_t len = 0;
	printk("current hactive :%d, v_active :%d, dest_hactive :%d, dest_vactive:%d\n",ov5647_h_active,ov5647_v_active,dest_hactive,dest_vactive);
	return len;
}
static CLASS_ATTR(resolution_debug, 0664, manual_format_show,manual_format_store);

static int vidioc_enum_framesizes(struct file *file, void *fh,struct v4l2_frmsizeenum *fsize)
{

      int ret = 0,i=0;
      struct ov5647_fmt *fmt = NULL;
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
              //printk("ov5647_prev_resolution[fsize->index]"
              //                "   before fsize->index== %d\n",fsize->index);//potti
              if (fsize->index >= ARRAY_SIZE(prev_resolution_array))
                      return -EINVAL;
              frmsize = &prev_resolution_array[fsize->index].frmsize;
              //printk("ov5647_prev_resolution[fsize->index]"
              //                "   after fsize->index== %d\n",fsize->index);
              fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
              fsize->discrete.width = frmsize->width;
              fsize->discrete.height = frmsize->height;
      } else if (fmt->fourcc == V4L2_PIX_FMT_RGB24){
              //printk("ov5647_pic_resolution[fsize->index]"
              //                "   before fsize->index== %d\n",fsize->index);
              if (fsize->index >= ARRAY_SIZE(capture_resolution_array))
                      return -EINVAL;
              frmsize = &capture_resolution_array[fsize->index].frmsize;
              //printk("ov5647_pic_resolution[fsize->index]"
              //                "   after fsize->index== %d\n",fsize->index);
              fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
              fsize->discrete.width = frmsize->width;
              fsize->discrete.height = frmsize->height;
      }
      return ret;
}

/* --- controls ---------------------------------------------- */
static int vidioc_queryctrl(struct file *file, void *priv,
                struct v4l2_queryctrl *qc)
{
        struct ov5647_device *dev = video_drvdata(file);
        int i;

        for (i = 0; i < ARRAY_SIZE(ov5647_qctrl); i++)
                if (qc->id && qc->id == ov5647_qctrl[i].id) {
                        if (V4L2_CID_BACKLIGHT_COMPENSATION == ov5647_qctrl[i].id) {
                                if (dev->cam_info.flash_support)
                                        memcpy(qc, &(ov5647_qctrl[i]),sizeof(*qc));
                        } else {
                                memcpy(qc, &(ov5647_qctrl[i]),sizeof(*qc));
                        }
                        return (0);
                }

        return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct ov5647_device *dev = video_drvdata(file);
	int i, status;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(ov5647_qctrl); i++)
		if (ctrl->id == ov5647_qctrl[i].id) {
            if( (V4L2_CID_FOCUS_AUTO == ctrl->id)
                    && bDoingAutoFocusMode){
                //bDoingAutoFocusMode = false;
                //ret = 0;
                dev->cam_para->cam_command = CAM_COMMAND_GET_STATE;
                dev->fe_arg.port = TVIN_PORT_ISP;
                dev->fe_arg.index = 0;
                dev->fe_arg.arg = (void *)(dev->cam_para);
                status = dev->vops->tvin_fe_func(0,&dev->fe_arg);
                switch(status){
                    case CAM_STATE_DOING:
                        ret = -EBUSY;
                        break;
                    case CAM_STATE_ERROR:
                    case CAM_STATE_NULL:
                        printk("auto mode failed!\n");
                        bDoingAutoFocusMode = false;
                        ret = -EAGAIN;
                        break;
                    case CAM_STATE_SUCCESS:
                        bDoingAutoFocusMode = false;
                        ret = 0;
                        break;
                    default:
                        printk("wrong state\n");
                        ret = 0;
                }
            }else if( V4L2_CID_AUTO_FOCUS_STATUS == ctrl->id){
                dev->cam_para->cam_command = CAM_COMMAND_GET_STATE;
                dev->fe_arg.port = TVIN_PORT_ISP;
                dev->fe_arg.index = 0;
                dev->fe_arg.arg = (void *)(dev->cam_para);
                status = dev->vops->tvin_fe_func(0,&dev->fe_arg);
                switch(status){
                    case CAM_STATE_DOING:
                        ctrl->value = V4L2_AUTO_FOCUS_STATUS_BUSY;
                        break;
                    case CAM_STATE_ERROR:
                        printk("should resart focus\n");
                        ctrl->value = V4L2_AUTO_FOCUS_STATUS_FAILED;
                        break;
                    case CAM_STATE_NULL:
                        ctrl->value = V4L2_AUTO_FOCUS_STATUS_IDLE;
                        break;
                    case CAM_STATE_SUCCESS:
                        ctrl->value = V4L2_AUTO_FOCUS_STATUS_REACHED;
                        break;
                    default:
                        printk("wrong state\n");
                }	
                return 0;
            }
            ctrl->value = dev->qctl_regs[i];
            return ret;
        }

    return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct ov5647_device *dev = video_drvdata(file);
	int i;
	for (i = 0; i < ARRAY_SIZE(ov5647_qctrl); i++)
		if (ctrl->id == ov5647_qctrl[i].id) {
			if (ctrl->value < ov5647_qctrl[i].minimum ||
			    ctrl->value > ov5647_qctrl[i].maximum ||
			    ov5647_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int ov5647_open(struct file *file)
{
    struct ov5647_device *dev = video_drvdata(file);

    int retval = 0;
    capture_proc = 0;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_name("ge2d", 1);
#endif
#if 0
#ifdef CONFIG_CMA
    //ov5647 will using ION mode
    retval = vm_init_buf(24*SZ_1M);
    if(retval <0)
        return -1;
#endif
#endif
    aml_cam_init(&dev->cam_info);
    printk("config path:%s\n",(dev->cam_info).config);
    if((dev->cam_info).config != NULL){
        if((dev->configure = vmalloc(sizeof(configure_t))) != NULL){
            if(parse_config((dev->cam_info).config,dev->configure) == 0){
                printk("parse successfully");
            }else{
                printk("parse failed");
                return -EINVAL;
            }
        }else{
            printk("malloc failed");
            return -ENOMEM;
        }
    }
    if((dev->cam_para = kzalloc(sizeof(cam_parameter_t),0)) == NULL){
        printk("memalloc failed\n");
        return -ENOMEM;
    }
    if(generate_para(dev->cam_para,dev->pindex,dev->configure) != 0){
        printk("generate para failed\n");
        free_para(dev->cam_para);
        kfree(dev->cam_para);
        return -EINVAL;
    }
    dev->cam_para->cam_function.set_aet_new_step = OV5647_set_aet_new_step;
    dev->cam_para->cam_function.check_mains_freq = OV5647_check_mains_freq;
    dev->cam_para->cam_function.set_af_new_step = OV5647_set_af_new_step;
    dev->camera_priv_data.configure = dev->configure;
    dev->cam_para->cam_function.priv_data = (void *)&dev->camera_priv_data;  
    dev->ae_on = false;
    OV5647_init_regs(dev);
    msleep(40);
    dw9714_init(dev->cam_info.vcm_mode);
    dw9714_init(vcm_mod);
    mutex_lock(&dev->mutex);
    dev->users++;
    if (dev->users > 1) {
        dev->users--;
        mutex_unlock(&dev->mutex);
        return -EBUSY;
    }

    dprintk(dev, 1, "open %s type=%s users=%d\n",
            video_device_node_name(&dev->vdev),
            v4l2_type_names[V4L2_BUF_TYPE_VIDEO_CAPTURE], dev->users);

    /* allocate + initialize per filehandle data */
    mutex_unlock(&dev->mutex);

    wake_lock(&(dev->wake_lock));

    dev->fmt      = &formats[0];
    dev->width    = 640;
    dev->height   = 480;

    if( CAM_MIPI == dev->cam_info.interface){ //deprecated; this added for there is no 960p output for mipi
        i_index = 3;
    }

    /* Resets frame counters */
    dev->jiffies = jiffies;
    ov5647_have_open = 1;
    ov5647_work_mode = CAMERA_PREVIEW;
    dev->pindex.effect_index = 0;
    dev->pindex.scenes_index = 0;
    dev->pindex.wb_index = 0;
    dev->pindex.capture_index = 0;
    dev->pindex.nr_index = 0;
    dev->pindex.peaking_index = 0;
    dev->pindex.lens_index = 0;
    /**creat class file**/		
    cam_class = class_create(THIS_MODULE,"camera"); 
    if(IS_ERR(cam_class)){
        return PTR_ERR(cam_class);
    }
    retval = class_create_file(cam_class,&class_attr_ae_debug);
    retval = class_create_file(cam_class,&class_attr_camera_debug);
    retval = class_create_file(cam_class,&class_attr_aet_debug);
    retval = class_create_file(cam_class,&class_attr_dg_debug);
    retval = class_create_file(cam_class,&class_attr_vcm_debug);
    retval = class_create_file(cam_class,&class_attr_resolution_debug);
    retval = class_create_file(cam_class,&class_attr_light_source_debug);
    retval = class_create_file(cam_class,&class_attr_version_debug);
    dev->vops = get_vdin_v4l2_ops();
	bDoingAutoFocusMode=false;
    cf = dev->configure;
    printk("open successfully\n");
    return v4l2_fh_open(file);
}


static int ov5647_close(struct file *file)
{
    struct ov5647_device        *dev = video_drvdata(file);
    int i=0;
    ov5647_have_open = 0;
    capture_proc = 0;

    if (dev->is_vdin_start) {
        dev->vops->stop_tvin_service(0);
    }
    vb2_fop_release(file);

    if(dev->configure != NULL){
        if(dev->configure->aet_valid){
            for(i = 0; i < dev->configure->aet.sum; i++){
                kfree(dev->configure->aet.aet[i].info);
                dev->configure->aet.aet[i].info = NULL;
                vfree(dev->configure->aet.aet[i].aet_table);
                dev->configure->aet.aet[i].aet_table = NULL;
            }
        }
        vfree(dev->configure);
        dev->configure = NULL;
    }
    cf = NULL;
    if(dev->cam_para != NULL ){
        free_para(dev->cam_para);
        kfree(dev->cam_para);
        dev->cam_para = NULL;
    }
	dev->camera_priv_data.sensor_aet_table = NULL;
	dev->camera_priv_data.sensor_aet_info = NULL;
    mutex_lock(&dev->mutex);
    dev->users--;
    mutex_unlock(&dev->mutex);

    //ov5647_h_active=800;
    //ov5647_v_active=600;
    ov5647_qctrl[0].default_value=0;
    ov5647_qctrl[1].default_value=4;
    ov5647_qctrl[2].default_value=0;
    ov5647_qctrl[3].default_value=CAM_BANDING_50HZ;
    ov5647_qctrl[4].default_value=CAM_WB_AUTO;

    ov5647_qctrl[5].default_value=4;
    ov5647_qctrl[6].default_value=0;
    ov5647_qctrl[10].default_value=100;
    ov5647_qctrl[11].default_value=0;
    dev->ae_on = false;
    //ov5647_frmintervals_active.numerator = 1;
    //ov5647_frmintervals_active.denominator = 15;
    power_down_ov5647(dev);
    dw9714_uninit();
    msleep(10);

    aml_cam_uninit(&dev->cam_info);

    msleep(10);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_name("ge2d", 0);
#endif		
    wake_unlock(&(dev->wake_lock));
    class_remove_file(cam_class,&class_attr_ae_debug);
    class_remove_file(cam_class,&class_attr_camera_debug);
    class_remove_file(cam_class,&class_attr_aet_debug);
    class_remove_file(cam_class,&class_attr_dg_debug);
    class_remove_file(cam_class,&class_attr_vcm_debug);
    class_remove_file(cam_class,&class_attr_resolution_debug);   
    class_remove_file(cam_class,&class_attr_version_debug);
    class_destroy(cam_class);
#if 0
//ov5647 will using ION mode
#ifdef CONFIG_CMA
    vm_deinit_buf();
#endif
#endif
    printk("close success\n");
    return 0;
}

//to be continued...
//         .open           = v4l2_fh_open,
//         .release        = vb2_fop_release,
static const struct v4l2_file_operations ov5647_fops = {
	.owner		= THIS_MODULE,
	.open           = ov5647_open,
	.release        = ov5647_close,
	.read           = vb2_fop_read,
	.poll						= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
  .mmap           = vb2_fop_mmap,
};
//to be continued...
static const struct v4l2_ioctl_ops ov5647_ioctl_ops = {
        .vidioc_querycap      = vidioc_querycap,
        .vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
        .vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
        .vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
        .vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
        .vidioc_reqbufs       = vb2_ioctl_reqbufs,
        .vidioc_create_bufs   = vb2_ioctl_create_bufs,
        .vidioc_prepare_buf   = vb2_ioctl_prepare_buf,
        .vidioc_querybuf      = vidioc_querybuf,
        .vidioc_qbuf          = vb2_ioctl_qbuf,
        .vidioc_dqbuf         = vb2_ioctl_dqbuf,
#if 0
        .vidioc_enum_input    = vidioc_enum_input,
        .vidioc_g_input       = vidioc_g_input,
        .vidioc_s_input       = vidioc_s_input,
#endif
        .vidioc_queryctrl     = vidioc_queryctrl,
        .vidioc_querymenu     = vidioc_querymenu,
        .vidioc_g_ctrl        = vidioc_g_ctrl,
        .vidioc_s_ctrl        = vidioc_s_ctrl,
        .vidioc_streamon      = vb2_ioctl_streamon,
        .vidioc_streamoff     = vb2_ioctl_streamoff,
        .vidioc_enum_framesizes = vidioc_enum_framesizes,
        .vidioc_g_parm = vidioc_g_parm,
        .vidioc_enum_frameintervals = vidioc_enum_frameintervals,
        .vidioc_s_crop        = vidioc_s_crop,
#if 0
        .vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
        .vidioc_unsubscribe_event = v4l2_event_unsubscribe,
#endif
};

static const struct video_device ov5647_template = {
        .name		= "ov5647_v4l",
        .fops           = &ov5647_fops,
        .ioctl_ops 	= &ov5647_ioctl_ops,
        .release	= video_device_release_empty,
};

static int ov5647_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_OV5647, 0);
}

static const struct v4l2_subdev_core_ops ov5647_core_ops = {
	.g_chip_ident = ov5647_g_chip_ident,
};

static const struct v4l2_subdev_ops ov5647_ops = {
	.core = &ov5647_core_ops,
};

static ssize_t cam_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        ssize_t len = 0;
	struct ov5647_device *t;

        t = dev_get_drvdata(dev);

        len += sprintf(buf+len, "\t%s parameters below\n", t->cam_info.name);
        len += sprintf(buf+len, "\ti2c_bus_num=%d, front_back=%d,flash=%d, auto_focus=%d, i2c_addr=0x%x\n"
                                "\tmclk=%d, flash_support=%d, flash_ctrl_level=%d, interface=%d, clk_channel=%d\n",
                                t->cam_info.i2c_bus_num,
                                t->cam_info.front_back,
                                t->cam_info.flash,
                                t->cam_info.auto_focus,
                                t->cam_info.i2c_addr,
                                t->cam_info.mclk,
                                t->cam_info.flash_support,
                                t->cam_info.flash_ctrl_level,
                                t->cam_info.interface,
                                t->cam_info.clk_channel);
        return len;
}
static ssize_t cam_info_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t len)
{

	struct ov5647_device *t;
        unsigned char n=0;
        char *buf_orig, *ps, *token;
        char *parm[3] = {NULL};

        if(!buf)
		return len;
        buf_orig = kstrdup(buf, GFP_KERNEL);
        t = dev_get_drvdata(dev);

        ps = buf_orig;
        while (1) {
                if ( n >=ARRAY_SIZE(parm) ){
                        printk("parm array overflow, n=%d, ARRAY_SIZE(parm)=%d\n", n, ARRAY_SIZE(parm));
                        return len;
                }

                token = strsep(&ps, " \n");
                if (token == NULL)
                        break;
                if (*token == '\0')
                        continue;
                parm[n++] = token;
        }

        if ( 0 == strcmp(parm[0],"interface")){
                t->cam_info.interface = simple_strtol(parm[1],NULL,16);
                printk("substitude with %s interface\n", t->cam_info.interface?"mipi":"dvp");
        }else if ( 0 == strcmp(parm[0],"clk")){
                t->cam_info.clk_channel = simple_strtol(parm[1],NULL,16);
                printk("clk channel =%s\n", t->cam_info.clk_channel?"clkB":"clkA");
        }

        kfree(buf_orig);

        return len;

}

static DEVICE_ATTR(cam_info, 0664, cam_info_show, cam_info_store);

static int ov5647_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
        int ret;
        struct ov5647_device *t;
        struct video_device  *vfd;
        struct v4l2_subdev *sd;
        struct vb2_queue *q;
        aml_cam_info_t* plat_dat;

        v4l_info(client, "chip found @ 0x%x (%s)\n",
                        client->addr << 1, client->adapter->name);
        t = kzalloc(sizeof(*t), GFP_KERNEL);
        if (t == NULL)
                return -ENOMEM;
        sd = &t->sd;
        v4l2_i2c_subdev_init(sd, client, &ov5647_ops);

        snprintf(t->v4l2_dev.name, sizeof(t->v4l2_dev.name),
                        "%s", OV5647_CAMERA_MODULE_NAME);
        ret = v4l2_device_register(NULL, &t->v4l2_dev);
        if (ret)
                goto free_dev;

        /* initialize locks */
        spin_lock_init(&t->slock);

        /* initialize queue */
        q = &t->vb_vidq;
        q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
        q->drv_priv = t;
        q->buf_struct_size = sizeof(struct ov5647_buffer);
        q->ops = &ov5647_video_qops;
        q->mem_ops = &vb2_ion_memops;
        q->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

        ret = vb2_queue_init(q);
        if (ret)
                goto unreg_dev;


        mutex_init(&t->mutex);
        /* init video dma queues */
        INIT_LIST_HEAD(&t->vidq.active);
        init_waitqueue_head(&t->vidq.wq);

        /* Now create a video4linux device */
        vfd = &t->vdev;
        memcpy(vfd, &ov5647_template, sizeof(*vfd));
        vfd->v4l2_dev = &t->v4l2_dev;
        vfd->queue = q;
        set_bit(V4L2_FL_USE_FH_PRIO, &vfd->flags);

        /*
         * Provide a mutex to v4l2 core. It will be used to protect
         * all fops and v4l2 ioctls.
         */
        vfd->lock = &t->mutex;
        video_set_drvdata(vfd, t);
        printk("register device\n");

        /* Register it */
        plat_dat = (aml_cam_info_t*)client->dev.platform_data;
        if (plat_dat) {
                memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
                if(plat_dat->front_back>=0)  
                        video_nr=plat_dat->front_back;
        }else {
                printk("camera ov5647: have no platform data\n");
                ret = -EINVAL;
                goto unreg_dev;
        }
        
        t->cam_info.version = OV5647_DRIVER_VERSION;
        if (aml_cam_info_reg(&t->cam_info) < 0)
		printk("reg caminfo error\n");
			
        ret = video_register_device(vfd, VFL_TYPE_GRABBER, video_nr);
        if (ret < 0) {
                goto unreg_dev;
                return ret;
        }

        wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "ov5647");
        device_create_file( &t->vdev.dev, &dev_attr_cam_info);
        return 0;

unreg_dev:
        v4l2_device_unregister(&t->v4l2_dev);
free_dev:
        kfree(t);
        return ret;
}

static int ov5647_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5647_device *t = to_dev(sd);
        device_remove_file( &t->vdev.dev, &dev_attr_cam_info);
	video_unregister_device(&t->vdev);
	v4l2_device_unregister_subdev(sd);
	v4l2_device_unregister(&t->v4l2_dev);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id ov5647_id[] = {
	{ "ov5647", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov5647_id);

static struct i2c_driver ov5647_i2c_driver = {
	.driver = {
		.name = "ov5647",
	},
	.probe = ov5647_probe,
	.remove = ov5647_remove,
	.id_table = ov5647_id,
};

module_i2c_driver(ov5647_i2c_driver);

