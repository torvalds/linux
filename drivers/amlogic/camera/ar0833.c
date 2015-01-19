/*
 *ar0833 - This code emulates a real video device with v4l2 api
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
#include <linux/vmalloc.h>

#include <linux/i2c.h>
#include <media/v4l2-chip-ident.h>
#include <linux/amlogic/camera/aml_cam_info.h>

#define MIPI_INTERFACE
#ifdef MIPI_INTERFACE
#include <linux/amlogic/mipi/am_mipi_csi2.h>
#endif
#include "common/vm.h"
#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <mach/gpio.h>
//#include <media/amlogic/656in.h>
#include "common/plat_ctrl.h"
#include <linux/amlogic/vmapi.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif

#include "common/config_parser.h"

#define AR0833_CAMERA_MODULE_NAME "ar0833"
#define MAGIC_RE_MEM 0x123039dc
#define AR0833_RES0_CANVAS_INDEX CAMERA_USER_CANVAS_INDEX


/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define AR0833_CAMERA_MAJOR_VERSION 0
#define AR0833_CAMERA_MINOR_VERSION 7
#define AR0833_CAMERA_RELEASE 0
#define AR0833_CAMERA_VERSION \
	KERNEL_VERSION(AR0833_CAMERA_MAJOR_VERSION, AR0833_CAMERA_MINOR_VERSION, AR0833_CAMERA_RELEASE)

#define AR0833_DRIVER_VERSION "AR0833-COMMON-01-140717"

MODULE_DESCRIPTION("ar0833 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 32;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static int ar0833_h_active = 800;
static int ar0833_v_active = 600;
static struct v4l2_fract ar0833_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

static int ar0833_have_open=0;

static camera_mode_t ar0833_work_mode = CAMERA_PREVIEW;
static struct class *cam_class;
static unsigned int g_ae_manual_exp;
static unsigned int g_ae_manual_ag;
static unsigned int g_ae_manual_vts;
//static unsigned int exp_mode;
//static unsigned int change_cnt;
static unsigned int current_fmt;
static unsigned int current_fr = 0;//50 hz
//static unsigned int aet_index;
static unsigned int last_af_step = 0;

#define HI2056_CAMERA_MODULE_NAME "mipi-hi2056"

/*static struct am_csi2_camera_para ar0833_para = {
    .name = HI2056_CAMERA_MODULE_NAME,
    .output_pixel = 0,
    .output_line = 0,
    .active_pixel = 0,
    .active_line = 0,
    .frame_rate = 0,
    .ui_val = 0,
    .hs_freq = 0,
    .clock_lane_mode = 0,
    .mirror = 0,
    .in_fmt = NULL,
    .out_fmt = NULL,
};*/
static int i_index = -1;
static int t_index = -1;
static int dest_hactive = 640;
static int dest_vactive = 480;
static bool bDoingAutoFocusMode = false;
static configure_t *cf;
/* supported controls */
static struct v4l2_queryctrl ar0833_qctrl[] = {
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
        .id		       = V4L2_CID_FOCUS_ABSOLUTE,
        .type		   = V4L2_CTRL_TYPE_INTEGER,
		.name		   = "focus center",
		.minimum	   = 0,
		.maximum	   = ((2000) << 16) | 2000,
		.step		   = 1,
		.default_value = (1000 << 16) | 1000,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
 	}
};

static struct v4l2_frmivalenum ar0833_frmivalenum[]={
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

struct v4l2_querymenu ar0833_qmenu_wbmode[] = {
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

struct v4l2_querymenu ar0833_qmenu_autofocus[] = {
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

struct v4l2_querymenu ar0833_qmenu_anti_banding_mode[] = {
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

struct v4l2_querymenu ar0833_qmenu_flashmode[] = {
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
    struct v4l2_querymenu* ar0833_qmenu;
}ar0833_qmenu_set_t;

ar0833_qmenu_set_t ar0833_qmenu_set[] = {
    {
        .id         	= V4L2_CID_DO_WHITE_BALANCE,
        .num            = ARRAY_SIZE(ar0833_qmenu_wbmode),
        .ar0833_qmenu   = ar0833_qmenu_wbmode,
    },{
        .id         	= V4L2_CID_POWER_LINE_FREQUENCY,
        .num            = ARRAY_SIZE(ar0833_qmenu_anti_banding_mode),
        .ar0833_qmenu   = ar0833_qmenu_anti_banding_mode,
    }, {
        .id             = V4L2_CID_FOCUS_AUTO,
        .num            = ARRAY_SIZE(ar0833_qmenu_autofocus),
        .ar0833_qmenu   = ar0833_qmenu_autofocus,
    }, {
        .id             = V4L2_CID_BACKLIGHT_COMPENSATION,
        .num            = ARRAY_SIZE(ar0833_qmenu_flashmode),
        .ar0833_qmenu   = ar0833_qmenu_flashmode,
    }
};

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(ar0833_qmenu_set); i++)
	if (a->id && a->id == ar0833_qmenu_set[i].id) {
	    for(j = 0; j < ar0833_qmenu_set[i].num; j++)
		if (a->index == ar0833_qmenu_set[i].ar0833_qmenu[j].index) {
			memcpy(a, &( ar0833_qmenu_set[i].ar0833_qmenu[j]),
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

struct ar0833_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct ar0833_fmt formats[] = {
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

static struct ar0833_fmt *get_format(struct v4l2_format *f)
{
	struct ar0833_fmt *fmt;
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
struct ar0833_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct ar0833_fmt        *fmt;
	unsigned int canvas_id;
};

struct ar0833_dmaqueue {
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
	cam_i2c_msg_t* reg_script[2];
} resolution_param_t;

static LIST_HEAD(ar0833_devicelist);

struct ar0833_device {
	struct list_head			ar0833_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct ar0833_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t cam_info;
	
	cam_parameter_t *cam_para;
	
	para_index_t pindex;
	
	struct vdin_v4l2_ops_s *vops;
	
	fe_arg_t fe_arg;
	
	int stream_on;
	
	vdin_arg_t vdin_arg;
	/* wake lock */
	struct wake_lock	wake_lock;
	/* ae status */
	bool ae_on;
	
	camera_priv_data_t camera_priv_data;
	
	configure_t *configure;
	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(ar0833_qctrl)];
};

static inline struct ar0833_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ar0833_device, sd);
}

struct ar0833_fh {
	struct ar0833_device            *dev;

	/* video capture */
	struct ar0833_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	struct videobuf_res_privdata res;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	unsigned int		f_flags;
};


#define RAW10

/* ------------------------------------------------------------------
	reg spec of AR0833
   ------------------------------------------------------------------*/
static cam_i2c_msg_t AR0833_init_script[] = {

	{END_OF_SCRIPT, 0, 0},
};
#ifdef MIPI_INTERFACE
/*static cam_i2c_msg_t AR0833_mipi_script[] = {
	{END_OF_SCRIPT, 0, 0},
};*/
#endif
static cam_i2c_msg_t AR0833_preview_VGA_script[] = {
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0833_VGA_script_mipi[] = {
	{2, 0x301A, 0x0019}, 	// RESET_REGISTER
	//DELAY 1
	{TIME_DELAY, 0, 1},
	{2, 0x301A, 0x0218}, 	// RESET_REGISTER
	{2, 0x3042, 0x0000}, 	// RESERVED_MFR_3042
	{2, 0x30C0, 0x1810}, 	// RESERVED_MFR_30C0
	{2, 0x30C8, 0x0018}, 	// RESERVED_MFR_30C8
	{2, 0x30D2, 0x0000}, 	// RESERVED_MFR_30D2
	{2, 0x30D4, 0x3030}, 	// RESERVED_MFR_30D4
	{2, 0x30D6, 0x2200}, 	// RESERVED_MFR_30D6
	{2, 0x30DA, 0x0080}, 	// RESERVED_MFR_30DA
	{2, 0x30DC, 0x0080}, 	// RESERVED_MFR_30DC
	{2, 0x30EE, 0x0340}, 	// RESERVED_MFR_30EE
	{2, 0x316A, 0x8800}, 	// RESERVED_MFR_316A
	{2, 0x316C, 0x8200}, 	// RESERVED_MFR_316C
	{2, 0x316E, 0x8200}, 	// RESERVED_MFR_316E
	{2, 0x3172, 0x0286}, 	// ANALOG_CONTROL2
	{2, 0x3174, 0x8000}, 	// RESERVED_MFR_3174
	{2, 0x317C, 0xE103}, 	// RESERVED_MFR_317C
	{2, 0x3180, 0xB080}, 	// RESERVED_MFR_3180
	{2, 0x31E0, 0x0741}, 	// RESERVED_MFR_31E0
	{2, 0x31E6, 0x0000}, 	// RESERVED_MFR_31E6
	{2, 0x3ECC, 0x0056}, 	// RESERVED_MFR_3ECC
	{2, 0x3ED0, 0xA666}, 	// RESERVED_MFR_3ED0
	{2, 0x3ED2, 0x6664}, 	// RESERVED_MFR_3ED2
	{2, 0x3ED4, 0x6ACC}, 	// RESERVED_MFR_3ED4
	{2, 0x3ED8, 0x7488}, 	// RESERVED_MFR_3ED8
	{2, 0x3EDA, 0x77CB}, 	// RESERVED_MFR_3EDA
	{2, 0x3EDE, 0x6664}, 	// RESERVED_MFR_3EDE
	{2, 0x3EE0, 0x26D5}, 	// RESERVED_MFR_3EE0
	{2, 0x3EE4, 0x3548}, 	// RESERVED_MFR_3EE4
	{2, 0x3EE6, 0xB10C}, 	// RESERVED_MFR_3EE6
	{2, 0x3EE8, 0x6E79}, 	// RESERVED_MFR_3EE8
	{2, 0x3EEA, 0xC8B9}, 	// RESERVED_MFR_3EEA
	{2, 0x3EFA, 0xA656}, 	// RESERVED_MFR_3EFA
	{2, 0x3EFE, 0x99CC}, 	// RESERVED_MFR_3EFE
	{2, 0x3F00, 0x0028}, 	// RESERVED_MFR_3F00
	{2, 0x3F02, 0x0140}, 	// RESERVED_MFR_3F02
	{2, 0x3F04, 0x0002}, 	// RESERVED_MFR_3F04
	{2, 0x3F06, 0x0004}, 	// RESERVED_MFR_3F06
	{2, 0x3F08, 0x0008}, 	// RESERVED_MFR_3F08
	{2, 0x3F0A, 0x0B09}, 	// RESERVED_MFR_3F0A
	{2, 0x3F0C, 0x0302}, 	// RESERVED_MFR_3F0C
	{2, 0x3F10, 0x0505}, 	// RESERVED_MFR_3F10
	{2, 0x3F12, 0x0303}, 	// RESERVED_MFR_3F12
	{2, 0x3F14, 0x0101}, 	// RESERVED_MFR_3F14
	{2, 0x3F16, 0x2020}, 	// RESERVED_MFR_3F16
	{2, 0x3F18, 0x0404}, 	// RESERVED_MFR_3F18
	{2, 0x3F1A, 0x7070}, 	// RESERVED_MFR_3F1A
	{2, 0x3F1C, 0x003A}, 	// RESERVED_MFR_3F1C
	{2, 0x3F1E, 0x003C}, 	// RESERVED_MFR_3F1E
	{2, 0x3F20, 0x0209}, 	// RESERVED_MFR_3F20
	{2, 0x3F2C, 0x2210}, 	// RESERVED_MFR_3F2C
	{2, 0x3F38, 0x44A8}, 	// RESERVED_MFR_3F38
	{2, 0x3F40, 0x2020}, 	// RESERVED_MFR_3F40
	{2, 0x3F42, 0x0808}, 	// RESERVED_MFR_3F42
	{2, 0x3F44, 0x0101}, 	// RESERVED_MFR_3F44
	{2, 0x0300, 0x0005}, 	// VT_PIX_CLK_DIV
	{2, 0x0302, 0x0001}, 	// VT_SYS_CLK_DIV
	{2, 0x0304, 0x0004}, 	// PRE_PLL_CLK_DIV
	{2, 0x0306, 0x007A}, 	// PLL_MULTIPLIER
	{2, 0x0308, 0x000A}, 	// OP_PIX_CLK_DIV
	{2, 0x030A, 0x0001}, 	// OP_SYS_CLK_DIV
	{2, 0x3064, 0x7800}, 	// RESERVED_MFR_3064
	//DELAY=1
	{TIME_DELAY, 0, 1}, 
	{2, 0x31B0, 0x0060}, 	// FRAME_PREAMBLE
	{2, 0x31B2, 0x0042}, 	// LINE_PREAMBLE
	{2, 0x31B4, 0x4C36}, 	// MIPI_TIMING_0
	{2, 0x31B6, 0x5218}, 	// MIPI_TIMING_1
	{2, 0x31B8, 0x404A}, 	// MIPI_TIMING_2
	{2, 0x31BA, 0x028A}, 	// MIPI_TIMING_3
	{2, 0x31BC, 0x0008}, 	// MIPI_TIMING_4
	{2, 0x31AE, 0x0202},
	{1, 0x3D00, 0x04},
	{1, 0x3D01, 0x71},
	{1, 0x3D02, 0xC9},
	{1, 0x3D03, 0xFF},
	{1, 0x3D04, 0xFF},
	{1, 0x3D05, 0xFF},
	{1, 0x3D06, 0xFF},
	{1, 0x3D07, 0xFF},
	{1, 0x3D08, 0x6F},
	{1, 0x3D09, 0x40},
	{1, 0x3D0A, 0x14},
	{1, 0x3D0B, 0x0E},
	{1, 0x3D0C, 0x23},
	{1, 0x3D0D, 0xC2},
	{1, 0x3D0E, 0x41},
	{1, 0x3D0F, 0x20},
	{1, 0x3D10, 0x30},
	{1, 0x3D11, 0x54},
	{1, 0x3D12, 0x80},
	{1, 0x3D13, 0x42},
	{1, 0x3D14, 0x00},
	{1, 0x3D15, 0xC0},
	{1, 0x3D16, 0x83},
	{1, 0x3D17, 0x57},
	{1, 0x3D18, 0x84},
	{1, 0x3D19, 0x64},
	{1, 0x3D1A, 0x64},
	{1, 0x3D1B, 0x55},
	{1, 0x3D1C, 0x80},
	{1, 0x3D1D, 0x23},
	{1, 0x3D1E, 0x00},
	{1, 0x3D1F, 0x65},
	{1, 0x3D20, 0x65},
	{1, 0x3D21, 0x82},
	{1, 0x3D22, 0x00},
	{1, 0x3D23, 0xC0},
	{1, 0x3D24, 0x6E},
	{1, 0x3D25, 0x80},
	{1, 0x3D26, 0x50},
	{1, 0x3D27, 0x51},
	{1, 0x3D28, 0x83},
	{1, 0x3D29, 0x42},
	{1, 0x3D2A, 0x83},
	{1, 0x3D2B, 0x58},
	{1, 0x3D2C, 0x6E},
	{1, 0x3D2D, 0x80},
	{1, 0x3D2E, 0x5F},
	{1, 0x3D2F, 0x87},
	{1, 0x3D30, 0x63},
	{1, 0x3D31, 0x82},
	{1, 0x3D32, 0x5B},
	{1, 0x3D33, 0x82},
	{1, 0x3D34, 0x59},
	{1, 0x3D35, 0x80},
	{1, 0x3D36, 0x5A},
	{1, 0x3D37, 0x5E},
	{1, 0x3D38, 0xBD},
	{1, 0x3D39, 0x59},
	{1, 0x3D3A, 0x59},
	{1, 0x3D3B, 0x9D},
	{1, 0x3D3C, 0x6C},
	{1, 0x3D3D, 0x80},
	{1, 0x3D3E, 0x6D},
	{1, 0x3D3F, 0xA3},
	{1, 0x3D40, 0x50},
	{1, 0x3D41, 0x80},
	{1, 0x3D42, 0x51},
	{1, 0x3D43, 0x82},
	{1, 0x3D44, 0x58},
	{1, 0x3D45, 0x80},
	{1, 0x3D46, 0x66},
	{1, 0x3D47, 0x83},
	{1, 0x3D48, 0x64},
	{1, 0x3D49, 0x64},
	{1, 0x3D4A, 0x80},
	{1, 0x3D4B, 0x30},
	{1, 0x3D4C, 0x50},
	{1, 0x3D4D, 0xDC},
	{1, 0x3D4E, 0x6A},
	{1, 0x3D4F, 0x83},
	{1, 0x3D50, 0x6B},
	{1, 0x3D51, 0xAA},
	{1, 0x3D52, 0x30},
	{1, 0x3D53, 0x94},
	{1, 0x3D54, 0x67},
	{1, 0x3D55, 0x84},
	{1, 0x3D56, 0x65},
	{1, 0x3D57, 0x65},
	{1, 0x3D58, 0x81},
	{1, 0x3D59, 0x4D},
	{1, 0x3D5A, 0x68},
	{1, 0x3D5B, 0x6A},
	{1, 0x3D5C, 0xAC},
	{1, 0x3D5D, 0x06},
	{1, 0x3D5E, 0x08},
	{1, 0x3D5F, 0x8D},
	{1, 0x3D60, 0x45},
	{1, 0x3D61, 0x96},
	{1, 0x3D62, 0x45},
	{1, 0x3D63, 0x85},
	{1, 0x3D64, 0x6A},
	{1, 0x3D65, 0x83},
	{1, 0x3D66, 0x6B},
	{1, 0x3D67, 0x06},
	{1, 0x3D68, 0x08},
	{1, 0x3D69, 0xA9},
	{1, 0x3D6A, 0x30},
	{1, 0x3D6B, 0x90},
	{1, 0x3D6C, 0x67},
	{1, 0x3D6D, 0x64},
	{1, 0x3D6E, 0x64},
	{1, 0x3D6F, 0x89},
	{1, 0x3D70, 0x65},
	{1, 0x3D71, 0x65},
	{1, 0x3D72, 0x81},
	{1, 0x3D73, 0x58},
	{1, 0x3D74, 0x88},
	{1, 0x3D75, 0x10},
	{1, 0x3D76, 0xC0},
	{1, 0x3D77, 0xB1},
	{1, 0x3D78, 0x5E},
	{1, 0x3D79, 0x96},
	{1, 0x3D7A, 0x53},
	{1, 0x3D7B, 0x82},
	{1, 0x3D7C, 0x5E},
	{1, 0x3D7D, 0x52},
	{1, 0x3D7E, 0x66},
	{1, 0x3D7F, 0x80},
	{1, 0x3D80, 0x58},
	{1, 0x3D81, 0x83},
	{1, 0x3D82, 0x64},
	{1, 0x3D83, 0x64},
	{1, 0x3D84, 0x80},
	{1, 0x3D85, 0x5B},
	{1, 0x3D86, 0x81},
	{1, 0x3D87, 0x5A},
	{1, 0x3D88, 0x1D},
	{1, 0x3D89, 0x0C},
	{1, 0x3D8A, 0x80},
	{1, 0x3D8B, 0x55},
	{1, 0x3D8C, 0x30},
	{1, 0x3D8D, 0x60},
	{1, 0x3D8E, 0x41},
	{1, 0x3D8F, 0x82},
	{1, 0x3D90, 0x42},
	{1, 0x3D91, 0xB2},
	{1, 0x3D92, 0x42},
	{1, 0x3D93, 0x80},
	{1, 0x3D94, 0x40},
	{1, 0x3D95, 0x81},
	{1, 0x3D96, 0x40},
	{1, 0x3D97, 0x89},
	{1, 0x3D98, 0x06},
	{1, 0x3D99, 0xC0},
	{1, 0x3D9A, 0x41},
	{1, 0x3D9B, 0x80},
	{1, 0x3D9C, 0x42},
	{1, 0x3D9D, 0x85},
	{1, 0x3D9E, 0x44},
	{1, 0x3D9F, 0x83},
	{1, 0x3DA0, 0x43},
	{1, 0x3DA1, 0x82},
	{1, 0x3DA2, 0x6A},
	{1, 0x3DA3, 0x83},
	{1, 0x3DA4, 0x6B},
	{1, 0x3DA5, 0x8D},
	{1, 0x3DA6, 0x43},
	{1, 0x3DA7, 0x83},
	{1, 0x3DA8, 0x44},
	{1, 0x3DA9, 0x81},
	{1, 0x3DAA, 0x41},
	{1, 0x3DAB, 0x85},
	{1, 0x3DAC, 0x06},
	{1, 0x3DAD, 0xC0},
	{1, 0x3DAE, 0x8C},
	{1, 0x3DAF, 0x30},
	{1, 0x3DB0, 0xA4},
	{1, 0x3DB1, 0x67},
	{1, 0x3DB2, 0x81},
	{1, 0x3DB3, 0x42},
	{1, 0x3DB4, 0x82},
	{1, 0x3DB5, 0x65},
	{1, 0x3DB6, 0x65},
	{1, 0x3DB7, 0x81},
	{1, 0x3DB8, 0x69},
	{1, 0x3DB9, 0x6A},
	{1, 0x3DBA, 0x96},
	{1, 0x3DBB, 0x40},
	{1, 0x3DBC, 0x82},
	{1, 0x3DBD, 0x40},
	{1, 0x3DBE, 0x89},
	{1, 0x3DBF, 0x06},
	{1, 0x3DC0, 0xC0},
	{1, 0x3DC1, 0x41},
	{1, 0x3DC2, 0x80},
	{1, 0x3DC3, 0x42},
	{1, 0x3DC4, 0x85},
	{1, 0x3DC5, 0x44},
	{1, 0x3DC6, 0x83},
	{1, 0x3DC7, 0x43},
	{1, 0x3DC8, 0x92},
	{1, 0x3DC9, 0x43},
	{1, 0x3DCA, 0x83},
	{1, 0x3DCB, 0x44},
	{1, 0x3DCC, 0x85},
	{1, 0x3DCD, 0x41},
	{1, 0x3DCE, 0x81},
	{1, 0x3DCF, 0x06},
	{1, 0x3DD0, 0xC0},
	{1, 0x3DD1, 0x81},
	{1, 0x3DD2, 0x6A},
	{1, 0x3DD3, 0x83},
	{1, 0x3DD4, 0x6B},
	{1, 0x3DD5, 0x82},
	{1, 0x3DD6, 0x42},
	{1, 0x3DD7, 0xA0},
	{1, 0x3DD8, 0x40},
	{1, 0x3DD9, 0x84},
	{1, 0x3DDA, 0x38},
	{1, 0x3DDB, 0xA8},
	{1, 0x3DDC, 0x33},
	{1, 0x3DDD, 0x00},
	{1, 0x3DDE, 0x28},
	{1, 0x3DDF, 0x30},
	{1, 0x3DE0, 0x70},
	{1, 0x3DE1, 0x00},
	{1, 0x3DE2, 0x6F},
	{1, 0x3DE3, 0x40},
	{1, 0x3DE4, 0x14},
	{1, 0x3DE5, 0x0E},
	{1, 0x3DE6, 0x23},
	{1, 0x3DE7, 0xC2},
	{1, 0x3DE8, 0x41},
	{1, 0x3DE9, 0x82},
	{1, 0x3DEA, 0x42},
	{1, 0x3DEB, 0x00},
	{1, 0x3DEC, 0xC0},
	{1, 0x3DED, 0x5D},
	{1, 0x3DEE, 0x80},
	{1, 0x3DEF, 0x5A},
	{1, 0x3DF0, 0x80},
	{1, 0x3DF1, 0x57},
	{1, 0x3DF2, 0x84},
	{1, 0x3DF3, 0x64},
	{1, 0x3DF4, 0x80},
	{1, 0x3DF5, 0x55},
	{1, 0x3DF6, 0x86},
	{1, 0x3DF7, 0x64},
	{1, 0x3DF8, 0x80},
	{1, 0x3DF9, 0x65},
	{1, 0x3DFA, 0x88},
	{1, 0x3DFB, 0x65},
	{1, 0x3DFC, 0x82},
	{1, 0x3DFD, 0x54},
	{1, 0x3DFE, 0x80},
	{1, 0x3DFF, 0x58},
	{1, 0x3E00, 0x80},
	{1, 0x3E01, 0x00},
	{1, 0x3E02, 0xC0},
	{1, 0x3E03, 0x86},
	{1, 0x3E04, 0x42},
	{1, 0x3E05, 0x82},
	{1, 0x3E06, 0x10},
	{1, 0x3E07, 0x30},
	{1, 0x3E08, 0x9C},
	{1, 0x3E09, 0x5C},
	{1, 0x3E0A, 0x80},
	{1, 0x3E0B, 0x6E},
	{1, 0x3E0C, 0x86},
	{1, 0x3E0D, 0x5B},
	{1, 0x3E0E, 0x80},
	{1, 0x3E0F, 0x63},
	{1, 0x3E10, 0x9E},
	{1, 0x3E11, 0x59},
	{1, 0x3E12, 0x8C},
	{1, 0x3E13, 0x5E},
	{1, 0x3E14, 0x8A},
	{1, 0x3E15, 0x6C},
	{1, 0x3E16, 0x80},
	{1, 0x3E17, 0x6D},
	{1, 0x3E18, 0x81},
	{1, 0x3E19, 0x5F},
	{1, 0x3E1A, 0x60},
	{1, 0x3E1B, 0x61},
	{1, 0x3E1C, 0x88},
	{1, 0x3E1D, 0x10},
	{1, 0x3E1E, 0x30},
	{1, 0x3E1F, 0x66},
	{1, 0x3E20, 0x83},
	{1, 0x3E21, 0x6E},
	{1, 0x3E22, 0x80},
	{1, 0x3E23, 0x64},
	{1, 0x3E24, 0x87},
	{1, 0x3E25, 0x64},
	{1, 0x3E26, 0x30},
	{1, 0x3E27, 0x50},
	{1, 0x3E28, 0xD3},
	{1, 0x3E29, 0x6A},
	{1, 0x3E2A, 0x6B},
	{1, 0x3E2B, 0xAD},
	{1, 0x3E2C, 0x30},
	{1, 0x3E2D, 0x94},
	{1, 0x3E2E, 0x67},
	{1, 0x3E2F, 0x84},
	{1, 0x3E30, 0x65},
	{1, 0x3E31, 0x82},
	{1, 0x3E32, 0x4D},
	{1, 0x3E33, 0x83},
	{1, 0x3E34, 0x65},
	{1, 0x3E35, 0x30},
	{1, 0x3E36, 0x50},
	{1, 0x3E37, 0xA7},
	{1, 0x3E38, 0x43},
	{1, 0x3E39, 0x06},
	{1, 0x3E3A, 0x00},
	{1, 0x3E3B, 0x8D},
	{1, 0x3E3C, 0x45},
	{1, 0x3E3D, 0x9A},
	{1, 0x3E3E, 0x6A},
	{1, 0x3E3F, 0x6B},
	{1, 0x3E40, 0x45},
	{1, 0x3E41, 0x85},
	{1, 0x3E42, 0x06},
	{1, 0x3E43, 0x00},
	{1, 0x3E44, 0x81},
	{1, 0x3E45, 0x43},
	{1, 0x3E46, 0x8A},
	{1, 0x3E47, 0x6F},
	{1, 0x3E48, 0x96},
	{1, 0x3E49, 0x30},
	{1, 0x3E4A, 0x90},
	{1, 0x3E4B, 0x67},
	{1, 0x3E4C, 0x64},
	{1, 0x3E4D, 0x88},
	{1, 0x3E4E, 0x64},
	{1, 0x3E4F, 0x80},
	{1, 0x3E50, 0x65},
	{1, 0x3E51, 0x82},
	{1, 0x3E52, 0x10},
	{1, 0x3E53, 0xC0},
	{1, 0x3E54, 0x84},
	{1, 0x3E55, 0x65},
	{1, 0x3E56, 0xEF},
	{1, 0x3E57, 0x10},
	{1, 0x3E58, 0xC0},
	{1, 0x3E59, 0x66},
	{1, 0x3E5A, 0x85},
	{1, 0x3E5B, 0x64},
	{1, 0x3E5C, 0x81},
	{1, 0x3E5D, 0x17},
	{1, 0x3E5E, 0x00},
	{1, 0x3E5F, 0x80},
	{1, 0x3E60, 0x20},
	{1, 0x3E61, 0x0D},
	{1, 0x3E62, 0x80},
	{1, 0x3E63, 0x18},
	{1, 0x3E64, 0x0C},
	{1, 0x3E65, 0x80},
	{1, 0x3E66, 0x64},
	{1, 0x3E67, 0x30},
	{1, 0x3E68, 0x60},
	{1, 0x3E69, 0x41},
	{1, 0x3E6A, 0x82},
	{1, 0x3E6B, 0x42},
	{1, 0x3E6C, 0xB2},
	{1, 0x3E6D, 0x42},
	{1, 0x3E6E, 0x80},
	{1, 0x3E6F, 0x40},
	{1, 0x3E70, 0x82},
	{1, 0x3E71, 0x40},
	{1, 0x3E72, 0x4C},
	{1, 0x3E73, 0x45},
	{1, 0x3E74, 0x92},
	{1, 0x3E75, 0x6A},
	{1, 0x3E76, 0x6B},
	{1, 0x3E77, 0x9B},
	{1, 0x3E78, 0x45},
	{1, 0x3E79, 0x81},
	{1, 0x3E7A, 0x4C},
	{1, 0x3E7B, 0x40},
	{1, 0x3E7C, 0x8C},
	{1, 0x3E7D, 0x30},
	{1, 0x3E7E, 0xA4},
	{1, 0x3E7F, 0x67},
	{1, 0x3E80, 0x85},
	{1, 0x3E81, 0x65},
	{1, 0x3E82, 0x87},
	{1, 0x3E83, 0x65},
	{1, 0x3E84, 0x30},
	{1, 0x3E85, 0x60},
	{1, 0x3E86, 0xD3},
	{1, 0x3E87, 0x6A},
	{1, 0x3E88, 0x6B},
	{1, 0x3E89, 0xAC},
	{1, 0x3E8A, 0x6C},
	{1, 0x3E8B, 0x32},
	{1, 0x3E8C, 0xA8},
	{1, 0x3E8D, 0x80},
	{1, 0x3E8E, 0x28},
	{1, 0x3E8F, 0x30},
	{1, 0x3E90, 0x70},
	{1, 0x3E91, 0x00},
	{1, 0x3E92, 0x80},
	{1, 0x3E93, 0x40},
	{1, 0x3E94, 0x4C},
	{1, 0x3E95, 0xBD},
	{1, 0x3E96, 0x00},
	{1, 0x3E97, 0x0E},
	{1, 0x3E98, 0xBE},
	{1, 0x3E99, 0x44},
	{1, 0x3E9A, 0x88},
	{1, 0x3E9B, 0x44},
	{1, 0x3E9C, 0xBC},
	{1, 0x3E9D, 0x78},
	{1, 0x3E9E, 0x09},
	{1, 0x3E9F, 0x00},
	{1, 0x3EA0, 0x89},
	{1, 0x3EA1, 0x04},
	{1, 0x3EA2, 0x80},
	{1, 0x3EA3, 0x80},
	{1, 0x3EA4, 0x02},
	{1, 0x3EA5, 0x40},
	{1, 0x3EA6, 0x86},
	{1, 0x3EA7, 0x09},
	{1, 0x3EA8, 0x00},
	{1, 0x3EA9, 0x8E},
	{1, 0x3EAA, 0x09},
	{1, 0x3EAB, 0x00},
	{1, 0x3EAC, 0x80},
	{1, 0x3EAD, 0x02},
	{1, 0x3EAE, 0x40},
	{1, 0x3EAF, 0x80},
	{1, 0x3EB0, 0x04},
	{1, 0x3EB1, 0x80},
	{1, 0x3EB2, 0x88},
	{1, 0x3EB3, 0x7D},
	{1, 0x3EB4, 0xA0},
	{1, 0x3EB5, 0x86},
	{1, 0x3EB6, 0x09},
	{1, 0x3EB7, 0x00},
	{1, 0x3EB8, 0x87},
	{1, 0x3EB9, 0x7A},
	{1, 0x3EBA, 0x00},
	{1, 0x3EBB, 0x0E},
	{1, 0x3EBC, 0xC3},
	{1, 0x3EBD, 0x79},
	{1, 0x3EBE, 0x4C},
	{1, 0x3EBF, 0x40},
	{1, 0x3EC0, 0xBF},
	{1, 0x3EC1, 0x70},
	{1, 0x3EC2, 0x00},
	{1, 0x3EC3, 0x00},
	{1, 0x3EC4, 0x00},
	{1, 0x3EC5, 0x00},
	{1, 0x3EC6, 0x00},
	{1, 0x3EC7, 0x00},
	{1, 0x3EC8, 0x00},
	{1, 0x3EC9, 0x00},
	{1, 0x3ECA, 0x00},
	{1, 0x3ECB, 0x00},
	{2, 0x0342, 0x1D9E}, 	// LINE_LENGTH_PCK
	{2, 0x0340, 0x0507}, 	// FRAME_LENGTH_LINES
	{2, 0x0202, 0x0507}, 	// COARSE_INTEGRATION_TIME
	{2, 0x0344, 0x0008}, 	// X_ADDR_START
	{2, 0x0348, 0x0CC5}, 	// X_ADDR_END
	{2, 0x0346, 0x0008}, 	// Y_ADDR_START
	{2, 0x034A, 0x0995}, 	// Y_ADDR_END
	{2, 0x034C, 0x0280}, 	// X_OUTPUT_SIZE
	{2, 0x034E, 0x01E0}, 	// Y_OUTPUT_SIZE
	{2, 0x3040, 0x40C3}, 	// READ_MODE
	{2, 0x0400, 0x0002}, 	// SCALING_MODE
	{2, 0x0402, 0x0000}, 	// SPATIAL_SAMPLING
	{2, 0x0404, 0x0028}, 	// SCALE_M
	{2, 0x0408, 0x280C}, 	// RESERVED_CONF_408
	{2, 0x040A, 0x00C7}, 	// RESERVED_CONF_40A
	{2, 0x306E, 0x9090}, 	// DATA_PATH_SELECT
	{2, 0x301A, 0x001C}, 	// RESET_REGISTER
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0833_preview_720P_script[] = {
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0833_720P_script_mipi[] = {
		{2, 0x301A, 0x0019}, 	// RESET_REGISTER
	//DELAY 1   
	{TIME_DELAY,0, 1},
	{2, 0x301A, 0x0218}, 	// RESET_REGISTER
	{2, 0x3042, 0x0000}, 	// RESERVED_MFR_3042
	{2, 0x30C0, 0x1810}, 	// RESERVED_MFR_30C0
	{2, 0x30C8, 0x0018}, 	// RESERVED_MFR_30C8
	{2, 0x30D2, 0x0000}, 	// RESERVED_MFR_30D2
	{2, 0x30D4, 0x3030}, 	// RESERVED_MFR_30D4
	{2, 0x30D6, 0x2200}, 	// RESERVED_MFR_30D6
	{2, 0x30DA, 0x0080}, 	// RESERVED_MFR_30DA
	{2, 0x30DC, 0x0080}, 	// RESERVED_MFR_30DC
	{2, 0x30EE, 0x0340}, 	// RESERVED_MFR_30EE
	{2, 0x316A, 0x8800}, 	// RESERVED_MFR_316A
	{2, 0x316C, 0x8200}, 	// RESERVED_MFR_316C
	{2, 0x316E, 0x8200}, 	// RESERVED_MFR_316E
	{2, 0x3172, 0x0286}, 	// ANALOG_CONTROL2
	{2, 0x3174, 0x8000}, 	// RESERVED_MFR_3174
	{2, 0x317C, 0xE103}, 	// RESERVED_MFR_317C
	{2, 0x3180, 0xB080}, 	// RESERVED_MFR_3180
	{2, 0x31E0, 0x0741}, 	// RESERVED_MFR_31E0
	{2, 0x31E6, 0x0000}, 	// RESERVED_MFR_31E6
	{2, 0x3ECC, 0x0056}, 	// RESERVED_MFR_3ECC
	{2, 0x3ED0, 0xA666}, 	// RESERVED_MFR_3ED0
	{2, 0x3ED2, 0x6664}, 	// RESERVED_MFR_3ED2
	{2, 0x3ED4, 0x6ACC}, 	// RESERVED_MFR_3ED4
	{2, 0x3ED8, 0x7488}, 	// RESERVED_MFR_3ED8
	{2, 0x3EDA, 0x77CB}, 	// RESERVED_MFR_3EDA
	{2, 0x3EDE, 0x6664}, 	// RESERVED_MFR_3EDE
	{2, 0x3EE0, 0x26D5}, 	// RESERVED_MFR_3EE0
	{2, 0x3EE4, 0x3548}, 	// RESERVED_MFR_3EE4
	{2, 0x3EE6, 0xB10C}, 	// RESERVED_MFR_3EE6
	{2, 0x3EE8, 0x6E79}, 	// RESERVED_MFR_3EE8
	{2, 0x3EEA, 0xC8B9}, 	// RESERVED_MFR_3EEA
	{2, 0x3EFA, 0xA656}, 	// RESERVED_MFR_3EFA
	{2, 0x3EFE, 0x99CC}, 	// RESERVED_MFR_3EFE
	{2, 0x3F00, 0x0028}, 	// RESERVED_MFR_3F00
	{2, 0x3F02, 0x0140}, 	// RESERVED_MFR_3F02
	{2, 0x3F04, 0x0002}, 	// RESERVED_MFR_3F04
	{2, 0x3F06, 0x0004}, 	// RESERVED_MFR_3F06
	{2, 0x3F08, 0x0008}, 	// RESERVED_MFR_3F08
	{2, 0x3F0A, 0x0B09}, 	// RESERVED_MFR_3F0A
	{2, 0x3F0C, 0x0302}, 	// RESERVED_MFR_3F0C
	{2, 0x3F10, 0x0505}, 	// RESERVED_MFR_3F10
	{2, 0x3F12, 0x0303}, 	// RESERVED_MFR_3F12
	{2, 0x3F14, 0x0101}, 	// RESERVED_MFR_3F14
	{2, 0x3F16, 0x2020}, 	// RESERVED_MFR_3F16
	{2, 0x3F18, 0x0404}, 	// RESERVED_MFR_3F18
	{2, 0x3F1A, 0x7070}, 	// RESERVED_MFR_3F1A
	{2, 0x3F1C, 0x003A}, 	// RESERVED_MFR_3F1C
	{2, 0x3F1E, 0x003C}, 	// RESERVED_MFR_3F1E
	{2, 0x3F20, 0x0209}, 	// RESERVED_MFR_3F20
	{2, 0x3F2C, 0x2210}, 	// RESERVED_MFR_3F2C
	{2, 0x3F38, 0x44A8}, 	// RESERVED_MFR_3F38
	{2, 0x3F40, 0x2020}, 	// RESERVED_MFR_3F40
	{2, 0x3F42, 0x0808}, 	// RESERVED_MFR_3F42
	{2, 0x3F44, 0x0101}, 	// RESERVED_MFR_3F44
	{2, 0x0300, 0x0005}, 	// VT_PIX_CLK_DIV
	{2, 0x0302, 0x0001}, 	// VT_SYS_CLK_DIV
	{2, 0x0304, 0x0004}, 	// PRE_PLL_CLK_DIV
	{2, 0x0306, 0x007A}, 	// PLL_MULTIPLIER
	{2, 0x0308, 0x000A}, 	// OP_PIX_CLK_DIV
	{2, 0x030A, 0x0001}, 	// OP_SYS_CLK_DIV
	{2, 0x3064, 0x7800}, 	// RESERVED_MFR_3064
	//DELAY=1
	{TIME_DELAY, 0, 1},
	{2, 0x31B0, 0x0060}, 	// FRAME_PREAMBLE
	{2, 0x31B2, 0x0042}, 	// LINE_PREAMBLE
	{2, 0x31B4, 0x4C36}, 	// MIPI_TIMING_0
	{2, 0x31B6, 0x5218}, 	// MIPI_TIMING_1
	{2, 0x31B8, 0x404A}, 	// MIPI_TIMING_2
	{2, 0x31BA, 0x028A}, 	// MIPI_TIMING_3
	{2, 0x31BC, 0x0008}, 	// MIPI_TIMING_4
	{2, 0x31AE, 0x0202},
	{1, 0x3D00, 0x04},
	{1, 0x3D01, 0x71},
	{1, 0x3D02, 0xC9},
	{1, 0x3D03, 0xFF},
	{1, 0x3D04, 0xFF},
	{1, 0x3D05, 0xFF},
	{1, 0x3D06, 0xFF},
	{1, 0x3D07, 0xFF},
	{1, 0x3D08, 0x6F},
	{1, 0x3D09, 0x40},
	{1, 0x3D0A, 0x14},
	{1, 0x3D0B, 0x0E},
	{1, 0x3D0C, 0x23},
	{1, 0x3D0D, 0xC2},
	{1, 0x3D0E, 0x41},
	{1, 0x3D0F, 0x20},
	{1, 0x3D10, 0x30},
	{1, 0x3D11, 0x54},
	{1, 0x3D12, 0x80},
	{1, 0x3D13, 0x42},
	{1, 0x3D14, 0x00},
	{1, 0x3D15, 0xC0},
	{1, 0x3D16, 0x83},
	{1, 0x3D17, 0x57},
	{1, 0x3D18, 0x84},
	{1, 0x3D19, 0x64},
	{1, 0x3D1A, 0x64},
	{1, 0x3D1B, 0x55},
	{1, 0x3D1C, 0x80},
	{1, 0x3D1D, 0x23},
	{1, 0x3D1E, 0x00},
	{1, 0x3D1F, 0x65},
	{1, 0x3D20, 0x65},
	{1, 0x3D21, 0x82},
	{1, 0x3D22, 0x00},
	{1, 0x3D23, 0xC0},
	{1, 0x3D24, 0x6E},
	{1, 0x3D25, 0x80},
	{1, 0x3D26, 0x50},
	{1, 0x3D27, 0x51},
	{1, 0x3D28, 0x83},
	{1, 0x3D29, 0x42},
	{1, 0x3D2A, 0x83},
	{1, 0x3D2B, 0x58},
	{1, 0x3D2C, 0x6E},
	{1, 0x3D2D, 0x80},
	{1, 0x3D2E, 0x5F},
	{1, 0x3D2F, 0x87},
	{1, 0x3D30, 0x63},
	{1, 0x3D31, 0x82},
	{1, 0x3D32, 0x5B},
	{1, 0x3D33, 0x82},
	{1, 0x3D34, 0x59},
	{1, 0x3D35, 0x80},
	{1, 0x3D36, 0x5A},
	{1, 0x3D37, 0x5E},
	{1, 0x3D38, 0xBD},
	{1, 0x3D39, 0x59},
	{1, 0x3D3A, 0x59},
	{1, 0x3D3B, 0x9D},
	{1, 0x3D3C, 0x6C},
	{1, 0x3D3D, 0x80},
	{1, 0x3D3E, 0x6D},
	{1, 0x3D3F, 0xA3},
	{1, 0x3D40, 0x50},
	{1, 0x3D41, 0x80},
	{1, 0x3D42, 0x51},
	{1, 0x3D43, 0x82},
	{1, 0x3D44, 0x58},
	{1, 0x3D45, 0x80},
	{1, 0x3D46, 0x66},
	{1, 0x3D47, 0x83},
	{1, 0x3D48, 0x64},
	{1, 0x3D49, 0x64},
	{1, 0x3D4A, 0x80},
	{1, 0x3D4B, 0x30},
	{1, 0x3D4C, 0x50},
	{1, 0x3D4D, 0xDC},
	{1, 0x3D4E, 0x6A},
	{1, 0x3D4F, 0x83},
	{1, 0x3D50, 0x6B},
	{1, 0x3D51, 0xAA},
	{1, 0x3D52, 0x30},
	{1, 0x3D53, 0x94},
	{1, 0x3D54, 0x67},
	{1, 0x3D55, 0x84},
	{1, 0x3D56, 0x65},
	{1, 0x3D57, 0x65},
	{1, 0x3D58, 0x81},
	{1, 0x3D59, 0x4D},
	{1, 0x3D5A, 0x68},
	{1, 0x3D5B, 0x6A},
	{1, 0x3D5C, 0xAC},
	{1, 0x3D5D, 0x06},
	{1, 0x3D5E, 0x08},
	{1, 0x3D5F, 0x8D},
	{1, 0x3D60, 0x45},
	{1, 0x3D61, 0x96},
	{1, 0x3D62, 0x45},
	{1, 0x3D63, 0x85},
	{1, 0x3D64, 0x6A},
	{1, 0x3D65, 0x83},
	{1, 0x3D66, 0x6B},
	{1, 0x3D67, 0x06},
	{1, 0x3D68, 0x08},
	{1, 0x3D69, 0xA9},
	{1, 0x3D6A, 0x30},
	{1, 0x3D6B, 0x90},
	{1, 0x3D6C, 0x67},
	{1, 0x3D6D, 0x64},
	{1, 0x3D6E, 0x64},
	{1, 0x3D6F, 0x89},
	{1, 0x3D70, 0x65},
	{1, 0x3D71, 0x65},
	{1, 0x3D72, 0x81},
	{1, 0x3D73, 0x58},
	{1, 0x3D74, 0x88},
	{1, 0x3D75, 0x10},
	{1, 0x3D76, 0xC0},
	{1, 0x3D77, 0xB1},
	{1, 0x3D78, 0x5E},
	{1, 0x3D79, 0x96},
	{1, 0x3D7A, 0x53},
	{1, 0x3D7B, 0x82},
	{1, 0x3D7C, 0x5E},
	{1, 0x3D7D, 0x52},
	{1, 0x3D7E, 0x66},
	{1, 0x3D7F, 0x80},
	{1, 0x3D80, 0x58},
	{1, 0x3D81, 0x83},
	{1, 0x3D82, 0x64},
	{1, 0x3D83, 0x64},
	{1, 0x3D84, 0x80},
	{1, 0x3D85, 0x5B},
	{1, 0x3D86, 0x81},
	{1, 0x3D87, 0x5A},
	{1, 0x3D88, 0x1D},
	{1, 0x3D89, 0x0C},
	{1, 0x3D8A, 0x80},
	{1, 0x3D8B, 0x55},
	{1, 0x3D8C, 0x30},
	{1, 0x3D8D, 0x60},
	{1, 0x3D8E, 0x41},
	{1, 0x3D8F, 0x82},
	{1, 0x3D90, 0x42},
	{1, 0x3D91, 0xB2},
	{1, 0x3D92, 0x42},
	{1, 0x3D93, 0x80},
	{1, 0x3D94, 0x40},
	{1, 0x3D95, 0x81},
	{1, 0x3D96, 0x40},
	{1, 0x3D97, 0x89},
	{1, 0x3D98, 0x06},
	{1, 0x3D99, 0xC0},
	{1, 0x3D9A, 0x41},
	{1, 0x3D9B, 0x80},
	{1, 0x3D9C, 0x42},
	{1, 0x3D9D, 0x85},
	{1, 0x3D9E, 0x44},
	{1, 0x3D9F, 0x83},
	{1, 0x3DA0, 0x43},
	{1, 0x3DA1, 0x82},
	{1, 0x3DA2, 0x6A},
	{1, 0x3DA3, 0x83},
	{1, 0x3DA4, 0x6B},
	{1, 0x3DA5, 0x8D},
	{1, 0x3DA6, 0x43},
	{1, 0x3DA7, 0x83},
	{1, 0x3DA8, 0x44},
	{1, 0x3DA9, 0x81},
	{1, 0x3DAA, 0x41},
	{1, 0x3DAB, 0x85},
	{1, 0x3DAC, 0x06},
	{1, 0x3DAD, 0xC0},
	{1, 0x3DAE, 0x8C},
	{1, 0x3DAF, 0x30},
	{1, 0x3DB0, 0xA4},
	{1, 0x3DB1, 0x67},
	{1, 0x3DB2, 0x81},
	{1, 0x3DB3, 0x42},
	{1, 0x3DB4, 0x82},
	{1, 0x3DB5, 0x65},
	{1, 0x3DB6, 0x65},
	{1, 0x3DB7, 0x81},
	{1, 0x3DB8, 0x69},
	{1, 0x3DB9, 0x6A},
	{1, 0x3DBA, 0x96},
	{1, 0x3DBB, 0x40},
	{1, 0x3DBC, 0x82},
	{1, 0x3DBD, 0x40},
	{1, 0x3DBE, 0x89},
	{1, 0x3DBF, 0x06},
	{1, 0x3DC0, 0xC0},
	{1, 0x3DC1, 0x41},
	{1, 0x3DC2, 0x80},
	{1, 0x3DC3, 0x42},
	{1, 0x3DC4, 0x85},
	{1, 0x3DC5, 0x44},
	{1, 0x3DC6, 0x83},
	{1, 0x3DC7, 0x43},
	{1, 0x3DC8, 0x92},
	{1, 0x3DC9, 0x43},
	{1, 0x3DCA, 0x83},
	{1, 0x3DCB, 0x44},
	{1, 0x3DCC, 0x85},
	{1, 0x3DCD, 0x41},
	{1, 0x3DCE, 0x81},
	{1, 0x3DCF, 0x06},
	{1, 0x3DD0, 0xC0},
	{1, 0x3DD1, 0x81},
	{1, 0x3DD2, 0x6A},
	{1, 0x3DD3, 0x83},
	{1, 0x3DD4, 0x6B},
	{1, 0x3DD5, 0x82},
	{1, 0x3DD6, 0x42},
	{1, 0x3DD7, 0xA0},
	{1, 0x3DD8, 0x40},
	{1, 0x3DD9, 0x84},
	{1, 0x3DDA, 0x38},
	{1, 0x3DDB, 0xA8},
	{1, 0x3DDC, 0x33},
	{1, 0x3DDD, 0x00},
	{1, 0x3DDE, 0x28},
	{1, 0x3DDF, 0x30},
	{1, 0x3DE0, 0x70},
	{1, 0x3DE1, 0x00},
	{1, 0x3DE2, 0x6F},
	{1, 0x3DE3, 0x40},
	{1, 0x3DE4, 0x14},
	{1, 0x3DE5, 0x0E},
	{1, 0x3DE6, 0x23},
	{1, 0x3DE7, 0xC2},
	{1, 0x3DE8, 0x41},
	{1, 0x3DE9, 0x82},
	{1, 0x3DEA, 0x42},
	{1, 0x3DEB, 0x00},
	{1, 0x3DEC, 0xC0},
	{1, 0x3DED, 0x5D},
	{1, 0x3DEE, 0x80},
	{1, 0x3DEF, 0x5A},
	{1, 0x3DF0, 0x80},
	{1, 0x3DF1, 0x57},
	{1, 0x3DF2, 0x84},
	{1, 0x3DF3, 0x64},
	{1, 0x3DF4, 0x80},
	{1, 0x3DF5, 0x55},
	{1, 0x3DF6, 0x86},
	{1, 0x3DF7, 0x64},
	{1, 0x3DF8, 0x80},
	{1, 0x3DF9, 0x65},
	{1, 0x3DFA, 0x88},
	{1, 0x3DFB, 0x65},
	{1, 0x3DFC, 0x82},
	{1, 0x3DFD, 0x54},
	{1, 0x3DFE, 0x80},
	{1, 0x3DFF, 0x58},
	{1, 0x3E00, 0x80},
	{1, 0x3E01, 0x00},
	{1, 0x3E02, 0xC0},
	{1, 0x3E03, 0x86},
	{1, 0x3E04, 0x42},
	{1, 0x3E05, 0x82},
	{1, 0x3E06, 0x10},
	{1, 0x3E07, 0x30},
	{1, 0x3E08, 0x9C},
	{1, 0x3E09, 0x5C},
	{1, 0x3E0A, 0x80},
	{1, 0x3E0B, 0x6E},
	{1, 0x3E0C, 0x86},
	{1, 0x3E0D, 0x5B},
	{1, 0x3E0E, 0x80},
	{1, 0x3E0F, 0x63},
	{1, 0x3E10, 0x9E},
	{1, 0x3E11, 0x59},
	{1, 0x3E12, 0x8C},
	{1, 0x3E13, 0x5E},
	{1, 0x3E14, 0x8A},
	{1, 0x3E15, 0x6C},
	{1, 0x3E16, 0x80},
	{1, 0x3E17, 0x6D},
	{1, 0x3E18, 0x81},
	{1, 0x3E19, 0x5F},
	{1, 0x3E1A, 0x60},
	{1, 0x3E1B, 0x61},
	{1, 0x3E1C, 0x88},
	{1, 0x3E1D, 0x10},
	{1, 0x3E1E, 0x30},
	{1, 0x3E1F, 0x66},
	{1, 0x3E20, 0x83},
	{1, 0x3E21, 0x6E},
	{1, 0x3E22, 0x80},
	{1, 0x3E23, 0x64},
	{1, 0x3E24, 0x87},
	{1, 0x3E25, 0x64},
	{1, 0x3E26, 0x30},
	{1, 0x3E27, 0x50},
	{1, 0x3E28, 0xD3},
	{1, 0x3E29, 0x6A},
	{1, 0x3E2A, 0x6B},
	{1, 0x3E2B, 0xAD},
	{1, 0x3E2C, 0x30},
	{1, 0x3E2D, 0x94},
	{1, 0x3E2E, 0x67},
	{1, 0x3E2F, 0x84},
	{1, 0x3E30, 0x65},
	{1, 0x3E31, 0x82},
	{1, 0x3E32, 0x4D},
	{1, 0x3E33, 0x83},
	{1, 0x3E34, 0x65},
	{1, 0x3E35, 0x30},
	{1, 0x3E36, 0x50},
	{1, 0x3E37, 0xA7},
	{1, 0x3E38, 0x43},
	{1, 0x3E39, 0x06},
	{1, 0x3E3A, 0x00},
	{1, 0x3E3B, 0x8D},
	{1, 0x3E3C, 0x45},
	{1, 0x3E3D, 0x9A},
	{1, 0x3E3E, 0x6A},
	{1, 0x3E3F, 0x6B},
	{1, 0x3E40, 0x45},
	{1, 0x3E41, 0x85},
	{1, 0x3E42, 0x06},
	{1, 0x3E43, 0x00},
	{1, 0x3E44, 0x81},
	{1, 0x3E45, 0x43},
	{1, 0x3E46, 0x8A},
	{1, 0x3E47, 0x6F},
	{1, 0x3E48, 0x96},
	{1, 0x3E49, 0x30},
	{1, 0x3E4A, 0x90},
	{1, 0x3E4B, 0x67},
	{1, 0x3E4C, 0x64},
	{1, 0x3E4D, 0x88},
	{1, 0x3E4E, 0x64},
	{1, 0x3E4F, 0x80},
	{1, 0x3E50, 0x65},
	{1, 0x3E51, 0x82},
	{1, 0x3E52, 0x10},
	{1, 0x3E53, 0xC0},
	{1, 0x3E54, 0x84},
	{1, 0x3E55, 0x65},
	{1, 0x3E56, 0xEF},
	{1, 0x3E57, 0x10},
	{1, 0x3E58, 0xC0},
	{1, 0x3E59, 0x66},
	{1, 0x3E5A, 0x85},
	{1, 0x3E5B, 0x64},
	{1, 0x3E5C, 0x81},
	{1, 0x3E5D, 0x17},
	{1, 0x3E5E, 0x00},
	{1, 0x3E5F, 0x80},
	{1, 0x3E60, 0x20},
	{1, 0x3E61, 0x0D},
	{1, 0x3E62, 0x80},
	{1, 0x3E63, 0x18},
	{1, 0x3E64, 0x0C},
	{1, 0x3E65, 0x80},
	{1, 0x3E66, 0x64},
	{1, 0x3E67, 0x30},
	{1, 0x3E68, 0x60},
	{1, 0x3E69, 0x41},
	{1, 0x3E6A, 0x82},
	{1, 0x3E6B, 0x42},
	{1, 0x3E6C, 0xB2},
	{1, 0x3E6D, 0x42},
	{1, 0x3E6E, 0x80},
	{1, 0x3E6F, 0x40},
	{1, 0x3E70, 0x82},
	{1, 0x3E71, 0x40},
	{1, 0x3E72, 0x4C},
	{1, 0x3E73, 0x45},
	{1, 0x3E74, 0x92},
	{1, 0x3E75, 0x6A},
	{1, 0x3E76, 0x6B},
	{1, 0x3E77, 0x9B},
	{1, 0x3E78, 0x45},
	{1, 0x3E79, 0x81},
	{1, 0x3E7A, 0x4C},
	{1, 0x3E7B, 0x40},
	{1, 0x3E7C, 0x8C},
	{1, 0x3E7D, 0x30},
	{1, 0x3E7E, 0xA4},
	{1, 0x3E7F, 0x67},
	{1, 0x3E80, 0x85},
	{1, 0x3E81, 0x65},
	{1, 0x3E82, 0x87},
	{1, 0x3E83, 0x65},
	{1, 0x3E84, 0x30},
	{1, 0x3E85, 0x60},
	{1, 0x3E86, 0xD3},
	{1, 0x3E87, 0x6A},
	{1, 0x3E88, 0x6B},
	{1, 0x3E89, 0xAC},
	{1, 0x3E8A, 0x6C},
	{1, 0x3E8B, 0x32},
	{1, 0x3E8C, 0xA8},
	{1, 0x3E8D, 0x80},
	{1, 0x3E8E, 0x28},
	{1, 0x3E8F, 0x30},
	{1, 0x3E90, 0x70},
	{1, 0x3E91, 0x00},
	{1, 0x3E92, 0x80},
	{1, 0x3E93, 0x40},
	{1, 0x3E94, 0x4C},
	{1, 0x3E95, 0xBD},
	{1, 0x3E96, 0x00},
	{1, 0x3E97, 0x0E},
	{1, 0x3E98, 0xBE},
	{1, 0x3E99, 0x44},
	{1, 0x3E9A, 0x88},
	{1, 0x3E9B, 0x44},
	{1, 0x3E9C, 0xBC},
	{1, 0x3E9D, 0x78},
	{1, 0x3E9E, 0x09},
	{1, 0x3E9F, 0x00},
	{1, 0x3EA0, 0x89},
	{1, 0x3EA1, 0x04},
	{1, 0x3EA2, 0x80},
	{1, 0x3EA3, 0x80},
	{1, 0x3EA4, 0x02},
	{1, 0x3EA5, 0x40},
	{1, 0x3EA6, 0x86},
	{1, 0x3EA7, 0x09},
	{1, 0x3EA8, 0x00},
	{1, 0x3EA9, 0x8E},
	{1, 0x3EAA, 0x09},
	{1, 0x3EAB, 0x00},
	{1, 0x3EAC, 0x80},
	{1, 0x3EAD, 0x02},
	{1, 0x3EAE, 0x40},
	{1, 0x3EAF, 0x80},
	{1, 0x3EB0, 0x04},
	{1, 0x3EB1, 0x80},
	{1, 0x3EB2, 0x88},
	{1, 0x3EB3, 0x7D},
	{1, 0x3EB4, 0xA0},
	{1, 0x3EB5, 0x86},
	{1, 0x3EB6, 0x09},
	{1, 0x3EB7, 0x00},
	{1, 0x3EB8, 0x87},
	{1, 0x3EB9, 0x7A},
	{1, 0x3EBA, 0x00},
	{1, 0x3EBB, 0x0E},
	{1, 0x3EBC, 0xC3},
	{1, 0x3EBD, 0x79},
	{1, 0x3EBE, 0x4C},
	{1, 0x3EBF, 0x40},
	{1, 0x3EC0, 0xBF},
	{1, 0x3EC1, 0x70},
	{1, 0x3EC2, 0x00},
	{1, 0x3EC3, 0x00},
	{1, 0x3EC4, 0x00},
	{1, 0x3EC5, 0x00},
	{1, 0x3EC6, 0x00},
	{1, 0x3EC7, 0x00},
	{1, 0x3EC8, 0x00},
	{1, 0x3EC9, 0x00},
	{1, 0x3ECA, 0x00},
	{1, 0x3ECB, 0x00},
	{2, 0x0342, 0x2500}, 	// LINE_LENGTH_PCK
	{2, 0x0340, 0x0408}, 	// FRAME_LENGTH_LINES
	{2, 0x0202, 0x0300}, 	// COARSE_INTEGRATION_TIME
	{2, 0x0344, 0x0008}, 	// X_ADDR_START
	{2, 0x0348, 0x0CC5}, 	// X_ADDR_END
	{2, 0x0346, 0x0130}, 	// Y_ADDR_START
	{2, 0x034A, 0x085B}, 	// Y_ADDR_END
	{2, 0x034C, 0x0500}, 	// X_OUTPUT_SIZE
	{2, 0x034E, 0x02D0}, 	// Y_OUTPUT_SIZE
	{2, 0x3040, 0x48C3}, 	// READ_MODE
	{2, 0x0400, 0x0002}, 	// SCALING_MODE
	{2, 0x0402, 0x0000}, 	// SPATIAL_SAMPLING
	{2, 0x0404, 0x0014}, 	// SCALE_M
	{2, 0x0408, 0x1414}, 	// RESERVED_CONF_408
	{2, 0x040A, 0x018C}, 	// RESERVED_CONF_40A
	{2, 0x306E, 0x9080}, 	// DATA_PATH_SELECT
	{2, 0x301A, 0x001C}, 	// RESET_REGISTER
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0833_preview_960P_script[] = {
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0833_960P_script_mipi[] = {
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0833_preview_1080P_script[] = {
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0833_preview_6M_script[] = {
	{END_OF_SCRIPT, 0, 0},
};
static cam_i2c_msg_t AR0833_preview_8M_script[] = {
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0833_1080P_script_mipi[] = {
	{2, 0x301A, 0x0019}, 	// RESET_REGISTER
	//DELAY 1
	{TIME_DELAY, 0, 1},
	{2, 0x301A, 0x0218}, 	// RESET_REGISTER
	{2, 0x3042, 0x0000}, 	// RESERVED_MFR_3042
	{2, 0x30C0, 0x1810}, 	// RESERVED_MFR_30C0
	{2, 0x30C8, 0x0018}, 	// RESERVED_MFR_30C8
	{2, 0x30D2, 0x0000}, 	// RESERVED_MFR_30D2
	{2, 0x30D4, 0x3030}, 	// RESERVED_MFR_30D4
	{2, 0x30D6, 0x2200}, 	// RESERVED_MFR_30D6
	{2, 0x30DA, 0x0080}, 	// RESERVED_MFR_30DA
	{2, 0x30DC, 0x0080}, 	// RESERVED_MFR_30DC
	{2, 0x30EE, 0x0340}, 	// RESERVED_MFR_30EE
	{2, 0x316A, 0x8800}, 	// RESERVED_MFR_316A
	{2, 0x316C, 0x8200}, 	// RESERVED_MFR_316C
	{2, 0x316E, 0x8200}, 	// RESERVED_MFR_316E
	{2, 0x3172, 0x0286}, 	// ANALOG_CONTROL2
	{2, 0x3174, 0x8000}, 	// RESERVED_MFR_3174
	{2, 0x317C, 0xE103}, 	// RESERVED_MFR_317C
	{2, 0x3180, 0xB080}, 	// RESERVED_MFR_3180
	{2, 0x31E0, 0x0741}, 	// RESERVED_MFR_31E0
	{2, 0x31E6, 0x0000}, 	// RESERVED_MFR_31E6
	{2, 0x3ECC, 0x0056}, 	// RESERVED_MFR_3ECC
	{2, 0x3ED0, 0xA666}, 	// RESERVED_MFR_3ED0
	{2, 0x3ED2, 0x6664}, 	// RESERVED_MFR_3ED2
	{2, 0x3ED4, 0x6ACC}, 	// RESERVED_MFR_3ED4
	{2, 0x3ED8, 0x7488}, 	// RESERVED_MFR_3ED8
	{2, 0x3EDA, 0x77CB}, 	// RESERVED_MFR_3EDA
	{2, 0x3EDE, 0x6664}, 	// RESERVED_MFR_3EDE
	{2, 0x3EE0, 0x26D5}, 	// RESERVED_MFR_3EE0
	{2, 0x3EE4, 0x3548}, 	// RESERVED_MFR_3EE4
	{2, 0x3EE6, 0xB10C}, 	// RESERVED_MFR_3EE6
	{2, 0x3EE8, 0x6E79}, 	// RESERVED_MFR_3EE8
	{2, 0x3EEA, 0xC8B9}, 	// RESERVED_MFR_3EEA
	{2, 0x3EFA, 0xA656}, 	// RESERVED_MFR_3EFA
	{2, 0x3EFE, 0x99CC}, 	// RESERVED_MFR_3EFE
	{2, 0x3F00, 0x0028}, 	// RESERVED_MFR_3F00
	{2, 0x3F02, 0x0140}, 	// RESERVED_MFR_3F02
	{2, 0x3F04, 0x0002}, 	// RESERVED_MFR_3F04
	{2, 0x3F06, 0x0004}, 	// RESERVED_MFR_3F06
	{2, 0x3F08, 0x0008}, 	// RESERVED_MFR_3F08
	{2, 0x3F0A, 0x0B09}, 	// RESERVED_MFR_3F0A
	{2, 0x3F0C, 0x0302}, 	// RESERVED_MFR_3F0C
	{2, 0x3F10, 0x0505}, 	// RESERVED_MFR_3F10
	{2, 0x3F12, 0x0303}, 	// RESERVED_MFR_3F12
	{2, 0x3F14, 0x0101}, 	// RESERVED_MFR_3F14
	{2, 0x3F16, 0x2020}, 	// RESERVED_MFR_3F16
	{2, 0x3F18, 0x0404}, 	// RESERVED_MFR_3F18
	{2, 0x3F1A, 0x7070}, 	// RESERVED_MFR_3F1A
	{2, 0x3F1C, 0x003A}, 	// RESERVED_MFR_3F1C
	{2, 0x3F1E, 0x003C}, 	// RESERVED_MFR_3F1E
	{2, 0x3F20, 0x0209}, 	// RESERVED_MFR_3F20
	{2, 0x3F2C, 0x2210}, 	// RESERVED_MFR_3F2C
	{2, 0x3F38, 0x44A8}, 	// RESERVED_MFR_3F38
	{2, 0x3F40, 0x2020}, 	// RESERVED_MFR_3F40
	{2, 0x3F42, 0x0808}, 	// RESERVED_MFR_3F42
	{2, 0x3F44, 0x0101}, 	// RESERVED_MFR_3F44
	{2, 0x0300, 0x0005}, 	// VT_PIX_CLK_DIV
	{2, 0x0302, 0x0001}, 	// VT_SYS_CLK_DIV
	{2, 0x0304, 0x0004}, 	// PRE_PLL_CLK_DIV
	{2, 0x0306, 0x007A}, 	// PLL_MULTIPLIER
	{2, 0x0308, 0x000A}, 	// OP_PIX_CLK_DIV
	{2, 0x030A, 0x0001}, 	// OP_SYS_CLK_DIV
	{2, 0x3064, 0x7800}, 	// RESERVED_MFR_3064
	//DELAY=1
	{TIME_DELAY, 0, 1},
	{2, 0x31B0, 0x0060}, 	// FRAME_PREAMBLE
	{2, 0x31B2, 0x0042}, 	// LINE_PREAMBLE
	{2, 0x31B4, 0x4C36}, 	// MIPI_TIMING_0
	{2, 0x31B6, 0x5218}, 	// MIPI_TIMING_1
	{2, 0x31B8, 0x404A}, 	// MIPI_TIMING_2
	{2, 0x31BA, 0x028A}, 	// MIPI_TIMING_3
	{2, 0x31BC, 0x0008}, 	// MIPI_TIMING_4
	{2, 0x31AE, 0x0202},
	{1, 0x3D00, 0x04},
	{1, 0x3D01, 0x71},
	{1, 0x3D02, 0xC9},
	{1, 0x3D03, 0xFF},
	{1, 0x3D04, 0xFF},
	{1, 0x3D05, 0xFF},
	{1, 0x3D06, 0xFF},
	{1, 0x3D07, 0xFF},
	{1, 0x3D08, 0x6F},
	{1, 0x3D09, 0x40},
	{1, 0x3D0A, 0x14},
	{1, 0x3D0B, 0x0E},
	{1, 0x3D0C, 0x23},
	{1, 0x3D0D, 0xC2},
	{1, 0x3D0E, 0x41},
	{1, 0x3D0F, 0x20},
	{1, 0x3D10, 0x30},
	{1, 0x3D11, 0x54},
	{1, 0x3D12, 0x80},
	{1, 0x3D13, 0x42},
	{1, 0x3D14, 0x00},
	{1, 0x3D15, 0xC0},
	{1, 0x3D16, 0x83},
	{1, 0x3D17, 0x57},
	{1, 0x3D18, 0x84},
	{1, 0x3D19, 0x64},
	{1, 0x3D1A, 0x64},
	{1, 0x3D1B, 0x55},
	{1, 0x3D1C, 0x80},
	{1, 0x3D1D, 0x23},
	{1, 0x3D1E, 0x00},
	{1, 0x3D1F, 0x65},
	{1, 0x3D20, 0x65},
	{1, 0x3D21, 0x82},
	{1, 0x3D22, 0x00},
	{1, 0x3D23, 0xC0},
	{1, 0x3D24, 0x6E},
	{1, 0x3D25, 0x80},
	{1, 0x3D26, 0x50},
	{1, 0x3D27, 0x51},
	{1, 0x3D28, 0x83},
	{1, 0x3D29, 0x42},
	{1, 0x3D2A, 0x83},
	{1, 0x3D2B, 0x58},
	{1, 0x3D2C, 0x6E},
	{1, 0x3D2D, 0x80},
	{1, 0x3D2E, 0x5F},
	{1, 0x3D2F, 0x87},
	{1, 0x3D30, 0x63},
	{1, 0x3D31, 0x82},
	{1, 0x3D32, 0x5B},
	{1, 0x3D33, 0x82},
	{1, 0x3D34, 0x59},
	{1, 0x3D35, 0x80},
	{1, 0x3D36, 0x5A},
	{1, 0x3D37, 0x5E},
	{1, 0x3D38, 0xBD},
	{1, 0x3D39, 0x59},
	{1, 0x3D3A, 0x59},
	{1, 0x3D3B, 0x9D},
	{1, 0x3D3C, 0x6C},
	{1, 0x3D3D, 0x80},
	{1, 0x3D3E, 0x6D},
	{1, 0x3D3F, 0xA3},
	{1, 0x3D40, 0x50},
	{1, 0x3D41, 0x80},
	{1, 0x3D42, 0x51},
	{1, 0x3D43, 0x82},
	{1, 0x3D44, 0x58},
	{1, 0x3D45, 0x80},
	{1, 0x3D46, 0x66},
	{1, 0x3D47, 0x83},
	{1, 0x3D48, 0x64},
	{1, 0x3D49, 0x64},
	{1, 0x3D4A, 0x80},
	{1, 0x3D4B, 0x30},
	{1, 0x3D4C, 0x50},
	{1, 0x3D4D, 0xDC},
	{1, 0x3D4E, 0x6A},
	{1, 0x3D4F, 0x83},
	{1, 0x3D50, 0x6B},
	{1, 0x3D51, 0xAA},
	{1, 0x3D52, 0x30},
	{1, 0x3D53, 0x94},
	{1, 0x3D54, 0x67},
	{1, 0x3D55, 0x84},
	{1, 0x3D56, 0x65},
	{1, 0x3D57, 0x65},
	{1, 0x3D58, 0x81},
	{1, 0x3D59, 0x4D},
	{1, 0x3D5A, 0x68},
	{1, 0x3D5B, 0x6A},
	{1, 0x3D5C, 0xAC},
	{1, 0x3D5D, 0x06},
	{1, 0x3D5E, 0x08},
	{1, 0x3D5F, 0x8D},
	{1, 0x3D60, 0x45},
	{1, 0x3D61, 0x96},
	{1, 0x3D62, 0x45},
	{1, 0x3D63, 0x85},
	{1, 0x3D64, 0x6A},
	{1, 0x3D65, 0x83},
	{1, 0x3D66, 0x6B},
	{1, 0x3D67, 0x06},
	{1, 0x3D68, 0x08},
	{1, 0x3D69, 0xA9},
	{1, 0x3D6A, 0x30},
	{1, 0x3D6B, 0x90},
	{1, 0x3D6C, 0x67},
	{1, 0x3D6D, 0x64},
	{1, 0x3D6E, 0x64},
	{1, 0x3D6F, 0x89},
	{1, 0x3D70, 0x65},
	{1, 0x3D71, 0x65},
	{1, 0x3D72, 0x81},
	{1, 0x3D73, 0x58},
	{1, 0x3D74, 0x88},
	{1, 0x3D75, 0x10},
	{1, 0x3D76, 0xC0},
	{1, 0x3D77, 0xB1},
	{1, 0x3D78, 0x5E},
	{1, 0x3D79, 0x96},
	{1, 0x3D7A, 0x53},
	{1, 0x3D7B, 0x82},
	{1, 0x3D7C, 0x5E},
	{1, 0x3D7D, 0x52},
	{1, 0x3D7E, 0x66},
	{1, 0x3D7F, 0x80},
	{1, 0x3D80, 0x58},
	{1, 0x3D81, 0x83},
	{1, 0x3D82, 0x64},
	{1, 0x3D83, 0x64},
	{1, 0x3D84, 0x80},
	{1, 0x3D85, 0x5B},
	{1, 0x3D86, 0x81},
	{1, 0x3D87, 0x5A},
	{1, 0x3D88, 0x1D},
	{1, 0x3D89, 0x0C},
	{1, 0x3D8A, 0x80},
	{1, 0x3D8B, 0x55},
	{1, 0x3D8C, 0x30},
	{1, 0x3D8D, 0x60},
	{1, 0x3D8E, 0x41},
	{1, 0x3D8F, 0x82},
	{1, 0x3D90, 0x42},
	{1, 0x3D91, 0xB2},
	{1, 0x3D92, 0x42},
	{1, 0x3D93, 0x80},
	{1, 0x3D94, 0x40},
	{1, 0x3D95, 0x81},
	{1, 0x3D96, 0x40},
	{1, 0x3D97, 0x89},
	{1, 0x3D98, 0x06},
	{1, 0x3D99, 0xC0},
	{1, 0x3D9A, 0x41},
	{1, 0x3D9B, 0x80},
	{1, 0x3D9C, 0x42},
	{1, 0x3D9D, 0x85},
	{1, 0x3D9E, 0x44},
	{1, 0x3D9F, 0x83},
	{1, 0x3DA0, 0x43},
	{1, 0x3DA1, 0x82},
	{1, 0x3DA2, 0x6A},
	{1, 0x3DA3, 0x83},
	{1, 0x3DA4, 0x6B},
	{1, 0x3DA5, 0x8D},
	{1, 0x3DA6, 0x43},
	{1, 0x3DA7, 0x83},
	{1, 0x3DA8, 0x44},
	{1, 0x3DA9, 0x81},
	{1, 0x3DAA, 0x41},
	{1, 0x3DAB, 0x85},
	{1, 0x3DAC, 0x06},
	{1, 0x3DAD, 0xC0},
	{1, 0x3DAE, 0x8C},
	{1, 0x3DAF, 0x30},
	{1, 0x3DB0, 0xA4},
	{1, 0x3DB1, 0x67},
	{1, 0x3DB2, 0x81},
	{1, 0x3DB3, 0x42},
	{1, 0x3DB4, 0x82},
	{1, 0x3DB5, 0x65},
	{1, 0x3DB6, 0x65},
	{1, 0x3DB7, 0x81},
	{1, 0x3DB8, 0x69},
	{1, 0x3DB9, 0x6A},
	{1, 0x3DBA, 0x96},
	{1, 0x3DBB, 0x40},
	{1, 0x3DBC, 0x82},
	{1, 0x3DBD, 0x40},
	{1, 0x3DBE, 0x89},
	{1, 0x3DBF, 0x06},
	{1, 0x3DC0, 0xC0},
	{1, 0x3DC1, 0x41},
	{1, 0x3DC2, 0x80},
	{1, 0x3DC3, 0x42},
	{1, 0x3DC4, 0x85},
	{1, 0x3DC5, 0x44},
	{1, 0x3DC6, 0x83},
	{1, 0x3DC7, 0x43},
	{1, 0x3DC8, 0x92},
	{1, 0x3DC9, 0x43},
	{1, 0x3DCA, 0x83},
	{1, 0x3DCB, 0x44},
	{1, 0x3DCC, 0x85},
	{1, 0x3DCD, 0x41},
	{1, 0x3DCE, 0x81},
	{1, 0x3DCF, 0x06},
	{1, 0x3DD0, 0xC0},
	{1, 0x3DD1, 0x81},
	{1, 0x3DD2, 0x6A},
	{1, 0x3DD3, 0x83},
	{1, 0x3DD4, 0x6B},
	{1, 0x3DD5, 0x82},
	{1, 0x3DD6, 0x42},
	{1, 0x3DD7, 0xA0},
	{1, 0x3DD8, 0x40},
	{1, 0x3DD9, 0x84},
	{1, 0x3DDA, 0x38},
	{1, 0x3DDB, 0xA8},
	{1, 0x3DDC, 0x33},
	{1, 0x3DDD, 0x00},
	{1, 0x3DDE, 0x28},
	{1, 0x3DDF, 0x30},
	{1, 0x3DE0, 0x70},
	{1, 0x3DE1, 0x00},
	{1, 0x3DE2, 0x6F},
	{1, 0x3DE3, 0x40},
	{1, 0x3DE4, 0x14},
	{1, 0x3DE5, 0x0E},
	{1, 0x3DE6, 0x23},
	{1, 0x3DE7, 0xC2},
	{1, 0x3DE8, 0x41},
	{1, 0x3DE9, 0x82},
	{1, 0x3DEA, 0x42},
	{1, 0x3DEB, 0x00},
	{1, 0x3DEC, 0xC0},
	{1, 0x3DED, 0x5D},
	{1, 0x3DEE, 0x80},
	{1, 0x3DEF, 0x5A},
	{1, 0x3DF0, 0x80},
	{1, 0x3DF1, 0x57},
	{1, 0x3DF2, 0x84},
	{1, 0x3DF3, 0x64},
	{1, 0x3DF4, 0x80},
	{1, 0x3DF5, 0x55},
	{1, 0x3DF6, 0x86},
	{1, 0x3DF7, 0x64},
	{1, 0x3DF8, 0x80},
	{1, 0x3DF9, 0x65},
	{1, 0x3DFA, 0x88},
	{1, 0x3DFB, 0x65},
	{1, 0x3DFC, 0x82},
	{1, 0x3DFD, 0x54},
	{1, 0x3DFE, 0x80},
	{1, 0x3DFF, 0x58},
	{1, 0x3E00, 0x80},
	{1, 0x3E01, 0x00},
	{1, 0x3E02, 0xC0},
	{1, 0x3E03, 0x86},
	{1, 0x3E04, 0x42},
	{1, 0x3E05, 0x82},
	{1, 0x3E06, 0x10},
	{1, 0x3E07, 0x30},
	{1, 0x3E08, 0x9C},
	{1, 0x3E09, 0x5C},
	{1, 0x3E0A, 0x80},
	{1, 0x3E0B, 0x6E},
	{1, 0x3E0C, 0x86},
	{1, 0x3E0D, 0x5B},
	{1, 0x3E0E, 0x80},
	{1, 0x3E0F, 0x63},
	{1, 0x3E10, 0x9E},
	{1, 0x3E11, 0x59},
	{1, 0x3E12, 0x8C},
	{1, 0x3E13, 0x5E},
	{1, 0x3E14, 0x8A},
	{1, 0x3E15, 0x6C},
	{1, 0x3E16, 0x80},
	{1, 0x3E17, 0x6D},
	{1, 0x3E18, 0x81},
	{1, 0x3E19, 0x5F},
	{1, 0x3E1A, 0x60},
	{1, 0x3E1B, 0x61},
	{1, 0x3E1C, 0x88},
	{1, 0x3E1D, 0x10},
	{1, 0x3E1E, 0x30},
	{1, 0x3E1F, 0x66},
	{1, 0x3E20, 0x83},
	{1, 0x3E21, 0x6E},
	{1, 0x3E22, 0x80},
	{1, 0x3E23, 0x64},
	{1, 0x3E24, 0x87},
	{1, 0x3E25, 0x64},
	{1, 0x3E26, 0x30},
	{1, 0x3E27, 0x50},
	{1, 0x3E28, 0xD3},
	{1, 0x3E29, 0x6A},
	{1, 0x3E2A, 0x6B},
	{1, 0x3E2B, 0xAD},
	{1, 0x3E2C, 0x30},
	{1, 0x3E2D, 0x94},
	{1, 0x3E2E, 0x67},
	{1, 0x3E2F, 0x84},
	{1, 0x3E30, 0x65},
	{1, 0x3E31, 0x82},
	{1, 0x3E32, 0x4D},
	{1, 0x3E33, 0x83},
	{1, 0x3E34, 0x65},
	{1, 0x3E35, 0x30},
	{1, 0x3E36, 0x50},
	{1, 0x3E37, 0xA7},
	{1, 0x3E38, 0x43},
	{1, 0x3E39, 0x06},
	{1, 0x3E3A, 0x00},
	{1, 0x3E3B, 0x8D},
	{1, 0x3E3C, 0x45},
	{1, 0x3E3D, 0x9A},
	{1, 0x3E3E, 0x6A},
	{1, 0x3E3F, 0x6B},
	{1, 0x3E40, 0x45},
	{1, 0x3E41, 0x85},
	{1, 0x3E42, 0x06},
	{1, 0x3E43, 0x00},
	{1, 0x3E44, 0x81},
	{1, 0x3E45, 0x43},
	{1, 0x3E46, 0x8A},
	{1, 0x3E47, 0x6F},
	{1, 0x3E48, 0x96},
	{1, 0x3E49, 0x30},
	{1, 0x3E4A, 0x90},
	{1, 0x3E4B, 0x67},
	{1, 0x3E4C, 0x64},
	{1, 0x3E4D, 0x88},
	{1, 0x3E4E, 0x64},
	{1, 0x3E4F, 0x80},
	{1, 0x3E50, 0x65},
	{1, 0x3E51, 0x82},
	{1, 0x3E52, 0x10},
	{1, 0x3E53, 0xC0},
	{1, 0x3E54, 0x84},
	{1, 0x3E55, 0x65},
	{1, 0x3E56, 0xEF},
	{1, 0x3E57, 0x10},
	{1, 0x3E58, 0xC0},
	{1, 0x3E59, 0x66},
	{1, 0x3E5A, 0x85},
	{1, 0x3E5B, 0x64},
	{1, 0x3E5C, 0x81},
	{1, 0x3E5D, 0x17},
	{1, 0x3E5E, 0x00},
	{1, 0x3E5F, 0x80},
	{1, 0x3E60, 0x20},
	{1, 0x3E61, 0x0D},
	{1, 0x3E62, 0x80},
	{1, 0x3E63, 0x18},
	{1, 0x3E64, 0x0C},
	{1, 0x3E65, 0x80},
	{1, 0x3E66, 0x64},
	{1, 0x3E67, 0x30},
	{1, 0x3E68, 0x60},
	{1, 0x3E69, 0x41},
	{1, 0x3E6A, 0x82},
	{1, 0x3E6B, 0x42},
	{1, 0x3E6C, 0xB2},
	{1, 0x3E6D, 0x42},
	{1, 0x3E6E, 0x80},
	{1, 0x3E6F, 0x40},
	{1, 0x3E70, 0x82},
	{1, 0x3E71, 0x40},
	{1, 0x3E72, 0x4C},
	{1, 0x3E73, 0x45},
	{1, 0x3E74, 0x92},
	{1, 0x3E75, 0x6A},
	{1, 0x3E76, 0x6B},
	{1, 0x3E77, 0x9B},
	{1, 0x3E78, 0x45},
	{1, 0x3E79, 0x81},
	{1, 0x3E7A, 0x4C},
	{1, 0x3E7B, 0x40},
	{1, 0x3E7C, 0x8C},
	{1, 0x3E7D, 0x30},
	{1, 0x3E7E, 0xA4},
	{1, 0x3E7F, 0x67},
	{1, 0x3E80, 0x85},
	{1, 0x3E81, 0x65},
	{1, 0x3E82, 0x87},
	{1, 0x3E83, 0x65},
	{1, 0x3E84, 0x30},
	{1, 0x3E85, 0x60},
	{1, 0x3E86, 0xD3},
	{1, 0x3E87, 0x6A},
	{1, 0x3E88, 0x6B},
	{1, 0x3E89, 0xAC},
	{1, 0x3E8A, 0x6C},
	{1, 0x3E8B, 0x32},
	{1, 0x3E8C, 0xA8},
	{1, 0x3E8D, 0x80},
	{1, 0x3E8E, 0x28},
	{1, 0x3E8F, 0x30},
	{1, 0x3E90, 0x70},
	{1, 0x3E91, 0x00},
	{1, 0x3E92, 0x80},
	{1, 0x3E93, 0x40},
	{1, 0x3E94, 0x4C},
	{1, 0x3E95, 0xBD},
	{1, 0x3E96, 0x00},
	{1, 0x3E97, 0x0E},
	{1, 0x3E98, 0xBE},
	{1, 0x3E99, 0x44},
	{1, 0x3E9A, 0x88},
	{1, 0x3E9B, 0x44},
	{1, 0x3E9C, 0xBC},
	{1, 0x3E9D, 0x78},
	{1, 0x3E9E, 0x09},
	{1, 0x3E9F, 0x00},
	{1, 0x3EA0, 0x89},
	{1, 0x3EA1, 0x04},
	{1, 0x3EA2, 0x80},
	{1, 0x3EA3, 0x80},
	{1, 0x3EA4, 0x02},
	{1, 0x3EA5, 0x40},
	{1, 0x3EA6, 0x86},
	{1, 0x3EA7, 0x09},
	{1, 0x3EA8, 0x00},
	{1, 0x3EA9, 0x8E},
	{1, 0x3EAA, 0x09},
	{1, 0x3EAB, 0x00},
	{1, 0x3EAC, 0x80},
	{1, 0x3EAD, 0x02},
	{1, 0x3EAE, 0x40},
	{1, 0x3EAF, 0x80},
	{1, 0x3EB0, 0x04},
	{1, 0x3EB1, 0x80},
	{1, 0x3EB2, 0x88},
	{1, 0x3EB3, 0x7D},
	{1, 0x3EB4, 0xA0},
	{1, 0x3EB5, 0x86},
	{1, 0x3EB6, 0x09},
	{1, 0x3EB7, 0x00},
	{1, 0x3EB8, 0x87},
	{1, 0x3EB9, 0x7A},
	{1, 0x3EBA, 0x00},
	{1, 0x3EBB, 0x0E},
	{1, 0x3EBC, 0xC3},
	{1, 0x3EBD, 0x79},
	{1, 0x3EBE, 0x4C},
	{1, 0x3EBF, 0x40},
	{1, 0x3EC0, 0xBF},
	{1, 0x3EC1, 0x70},
	{1, 0x3EC2, 0x00},
	{1, 0x3EC3, 0x00},
	{1, 0x3EC4, 0x00},
	{1, 0x3EC5, 0x00},
	{1, 0x3EC6, 0x00},
	{1, 0x3EC7, 0x00},
	{1, 0x3EC8, 0x00},
	{1, 0x3EC9, 0x00},
	{1, 0x3ECA, 0x00},
	{1, 0x3ECB, 0x00},
	{2, 0x0342, 0x138C}, 	// LINE_LENGTH_PCK
	{2, 0x0340, 0x079D}, 	// FRAME_LENGTH_LINES
	{2, 0x0202, 0x0700}, 	// COARSE_INTEGRATION_TIME
	{2, 0x0344, 0x0008}, 	// X_ADDR_START
	{2, 0x0348, 0x0CC7}, 	// X_ADDR_END
	{2, 0x0346, 0x0130}, 	// Y_ADDR_START
	{2, 0x034A, 0x085B}, 	// Y_ADDR_END
	{2, 0x034C, 0x0780}, 	// X_OUTPUT_SIZE
	{2, 0x034E, 0x0438}, 	// Y_OUTPUT_SIZE
	{2, 0x3040, 0x4041}, 	// READ_MODE
	{2, 0x0400, 0x0002}, 	// SCALING_MODE
	{2, 0x0402, 0x0000}, 	// SPATIAL_SAMPLING
	{2, 0x0404, 0x001A}, 	// SCALE_M
	{2, 0x0408, 0x0B0C}, 	// RESERVED_CONF_408
	{2, 0x040A, 0x018C}, 	// RESERVED_CONF_40A
	{2, 0x306E, 0x9090}, 	// DATA_PATH_SELECT
	{2, 0x301A, 0x001C}, 	// RESET_REGISTER
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0833_capture_8M_script[] = {
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0833_8M_script_mipi[] = {
	{2, 0x301A, 0x0019}, 	// RESET_REGISTER
	//DELAY=1   
	{TIME_DELAY, 0, 1}, 
	{2, 0x301A, 0x0218}, 	// RESET_REGISTER
	{2, 0x301A, 0x0019}, 	// RESET_REGISTER
	{2, 0x301A, 0x0218}, 	// RESET_REGISTER
	{2, 0x3042, 0x0000}, 	// RESERVED_MFR_3042
	{2, 0x30C0, 0x1810}, 	// RESERVED_MFR_30C0
	{2, 0x30C8, 0x0018}, 	// RESERVED_MFR_30C8
	{2, 0x30D2, 0x0000}, 	// RESERVED_MFR_30D2
	{2, 0x30D4, 0x3030}, 	// RESERVED_MFR_30D4
	{2, 0x30D6, 0x2200}, 	// RESERVED_MFR_30D6
	{2, 0x30DA, 0x0080}, 	// RESERVED_MFR_30DA
	{2, 0x30DC, 0x0080}, 	// RESERVED_MFR_30DC
	{2, 0x30EE, 0x0340}, 	// RESERVED_MFR_30EE
	{2, 0x316A, 0x8800}, 	// RESERVED_MFR_316A
	{2, 0x316C, 0x8200}, 	// RESERVED_MFR_316C
	{2, 0x316E, 0x8200}, 	// RESERVED_MFR_316E
	{2, 0x3172, 0x0286}, 	// ANALOG_CONTROL2
	{2, 0x3174, 0x8000}, 	// RESERVED_MFR_3174
	{2, 0x317C, 0xE103}, 	// RESERVED_MFR_317C
	{2, 0x3180, 0xB080}, 	// RESERVED_MFR_3180
	{2, 0x31E0, 0x0741}, 	// RESERVED_MFR_31E0
	{2, 0x31E6, 0x0000}, 	// RESERVED_MFR_31E6
	{2, 0x3ECC, 0x0056}, 	// RESERVED_MFR_3ECC
	{2, 0x3ED0, 0xA666}, 	// RESERVED_MFR_3ED0
	{2, 0x3ED2, 0x6664}, 	// RESERVED_MFR_3ED2
	{2, 0x3ED4, 0x6ACC}, 	// RESERVED_MFR_3ED4
	{2, 0x3ED8, 0x7488}, 	// RESERVED_MFR_3ED8
	{2, 0x3EDA, 0x77CB}, 	// RESERVED_MFR_3EDA
	{2, 0x3EDE, 0x6664}, 	// RESERVED_MFR_3EDE
	{2, 0x3EE0, 0x26D5}, 	// RESERVED_MFR_3EE0
	{2, 0x3EE4, 0x3548}, 	// RESERVED_MFR_3EE4
	{2, 0x3EE6, 0xB10C}, 	// RESERVED_MFR_3EE6
	{2, 0x3EE8, 0x6E79}, 	// RESERVED_MFR_3EE8
	{2, 0x3EEA, 0xC8B9}, 	// RESERVED_MFR_3EEA
	{2, 0x3EFA, 0xA656}, 	// RESERVED_MFR_3EFA
	{2, 0x3EFE, 0x99CC}, 	// RESERVED_MFR_3EFE
	{2, 0x3F00, 0x0028}, 	// RESERVED_MFR_3F00
	{2, 0x3F02, 0x0140}, 	// RESERVED_MFR_3F02
	{2, 0x3F04, 0x0002}, 	// RESERVED_MFR_3F04
	{2, 0x3F06, 0x0004}, 	// RESERVED_MFR_3F06
	{2, 0x3F08, 0x0008}, 	// RESERVED_MFR_3F08
	{2, 0x3F0A, 0x0B09}, 	// RESERVED_MFR_3F0A
	{2, 0x3F0C, 0x0302}, 	// RESERVED_MFR_3F0C
	{2, 0x3F10, 0x0505}, 	// RESERVED_MFR_3F10
	{2, 0x3F12, 0x0303}, 	// RESERVED_MFR_3F12
	{2, 0x3F14, 0x0101}, 	// RESERVED_MFR_3F14
	{2, 0x3F16, 0x2020}, 	// RESERVED_MFR_3F16
	{2, 0x3F18, 0x0404}, 	// RESERVED_MFR_3F18
	{2, 0x3F1A, 0x7070}, 	// RESERVED_MFR_3F1A
	{2, 0x3F1C, 0x003A}, 	// RESERVED_MFR_3F1C
	{2, 0x3F1E, 0x003C}, 	// RESERVED_MFR_3F1E
	{2, 0x3F20, 0x0209}, 	// RESERVED_MFR_3F20
	{2, 0x3F2C, 0x2210}, 	// RESERVED_MFR_3F2C
	{2, 0x3F38, 0x44A8}, 	// RESERVED_MFR_3F38
	{2, 0x3F40, 0x2020}, 	// RESERVED_MFR_3F40
	{2, 0x3F42, 0x0808}, 	// RESERVED_MFR_3F42
	{2, 0x3F44, 0x0101}, 	// RESERVED_MFR_3F44
	{2, 0x0300, 0x0005}, 	// VT_PIX_CLK_DIV
	{2, 0x0302, 0x0001}, 	// VT_SYS_CLK_DIV
	{2, 0x0304, 0x0004}, 	// PRE_PLL_CLK_DIV
	{2, 0x0306, 0x007A}, 	// PLL_MULTIPLIER
	{2, 0x0308, 0x000A}, 	// OP_PIX_CLK_DIV
	{2, 0x030A, 0x0001}, 	// OP_SYS_CLK_DIV
	{2, 0x3064, 0x7800}, 	// RESERVED_MFR_3064
	//DELAY=1
	{TIME_DELAY, 0, 1},
	{2, 0x31B0, 0x0060}, 	// FRAME_PREAMBLE
	{2, 0x31B2, 0x0042}, 	// LINE_PREAMBLE
	{2, 0x31B4, 0x4C36}, 	// MIPI_TIMING_0
	{2, 0x31B6, 0x5218}, 	// MIPI_TIMING_1
	{2, 0x31B8, 0x404A}, 	// MIPI_TIMING_2
	{2, 0x31BA, 0x028A}, 	// MIPI_TIMING_3
	{2, 0x31BC, 0x0008}, 	// MIPI_TIMING_4
	{2, 0x31AE, 0x0202},
	{1, 0x3D00, 0x04},
	{1, 0x3D01, 0x71},
	{1, 0x3D02, 0xC9},
	{1, 0x3D03, 0xFF},
	{1, 0x3D04, 0xFF},
	{1, 0x3D05, 0xFF},
	{1, 0x3D06, 0xFF},
	{1, 0x3D07, 0xFF},
	{1, 0x3D08, 0x6F},
	{1, 0x3D09, 0x40},
	{1, 0x3D0A, 0x14},
	{1, 0x3D0B, 0x0E},
	{1, 0x3D0C, 0x23},
	{1, 0x3D0D, 0xC2},
	{1, 0x3D0E, 0x41},
	{1, 0x3D0F, 0x20},
	{1, 0x3D10, 0x30},
	{1, 0x3D11, 0x54},
	{1, 0x3D12, 0x80},
	{1, 0x3D13, 0x42},
	{1, 0x3D14, 0x00},
	{1, 0x3D15, 0xC0},
	{1, 0x3D16, 0x83},
	{1, 0x3D17, 0x57},
	{1, 0x3D18, 0x84},
	{1, 0x3D19, 0x64},
	{1, 0x3D1A, 0x64},
	{1, 0x3D1B, 0x55},
	{1, 0x3D1C, 0x80},
	{1, 0x3D1D, 0x23},
	{1, 0x3D1E, 0x00},
	{1, 0x3D1F, 0x65},
	{1, 0x3D20, 0x65},
	{1, 0x3D21, 0x82},
	{1, 0x3D22, 0x00},
	{1, 0x3D23, 0xC0},
	{1, 0x3D24, 0x6E},
	{1, 0x3D25, 0x80},
	{1, 0x3D26, 0x50},
	{1, 0x3D27, 0x51},
	{1, 0x3D28, 0x83},
	{1, 0x3D29, 0x42},
	{1, 0x3D2A, 0x83},
	{1, 0x3D2B, 0x58},
	{1, 0x3D2C, 0x6E},
	{1, 0x3D2D, 0x80},
	{1, 0x3D2E, 0x5F},
	{1, 0x3D2F, 0x87},
	{1, 0x3D30, 0x63},
	{1, 0x3D31, 0x82},
	{1, 0x3D32, 0x5B},
	{1, 0x3D33, 0x82},
	{1, 0x3D34, 0x59},
	{1, 0x3D35, 0x80},
	{1, 0x3D36, 0x5A},
	{1, 0x3D37, 0x5E},
	{1, 0x3D38, 0xBD},
	{1, 0x3D39, 0x59},
	{1, 0x3D3A, 0x59},
	{1, 0x3D3B, 0x9D},
	{1, 0x3D3C, 0x6C},
	{1, 0x3D3D, 0x80},
	{1, 0x3D3E, 0x6D},
	{1, 0x3D3F, 0xA3},
	{1, 0x3D40, 0x50},
	{1, 0x3D41, 0x80},
	{1, 0x3D42, 0x51},
	{1, 0x3D43, 0x82},
	{1, 0x3D44, 0x58},
	{1, 0x3D45, 0x80},
	{1, 0x3D46, 0x66},
	{1, 0x3D47, 0x83},
	{1, 0x3D48, 0x64},
	{1, 0x3D49, 0x64},
	{1, 0x3D4A, 0x80},
	{1, 0x3D4B, 0x30},
	{1, 0x3D4C, 0x50},
	{1, 0x3D4D, 0xDC},
	{1, 0x3D4E, 0x6A},
	{1, 0x3D4F, 0x83},
	{1, 0x3D50, 0x6B},
	{1, 0x3D51, 0xAA},
	{1, 0x3D52, 0x30},
	{1, 0x3D53, 0x94},
	{1, 0x3D54, 0x67},
	{1, 0x3D55, 0x84},
	{1, 0x3D56, 0x65},
	{1, 0x3D57, 0x65},
	{1, 0x3D58, 0x81},
	{1, 0x3D59, 0x4D},
	{1, 0x3D5A, 0x68},
	{1, 0x3D5B, 0x6A},
	{1, 0x3D5C, 0xAC},
	{1, 0x3D5D, 0x06},
	{1, 0x3D5E, 0x08},
	{1, 0x3D5F, 0x8D},
	{1, 0x3D60, 0x45},
	{1, 0x3D61, 0x96},
	{1, 0x3D62, 0x45},
	{1, 0x3D63, 0x85},
	{1, 0x3D64, 0x6A},
	{1, 0x3D65, 0x83},
	{1, 0x3D66, 0x6B},
	{1, 0x3D67, 0x06},
	{1, 0x3D68, 0x08},
	{1, 0x3D69, 0xA9},
	{1, 0x3D6A, 0x30},
	{1, 0x3D6B, 0x90},
	{1, 0x3D6C, 0x67},
	{1, 0x3D6D, 0x64},
	{1, 0x3D6E, 0x64},
	{1, 0x3D6F, 0x89},
	{1, 0x3D70, 0x65},
	{1, 0x3D71, 0x65},
	{1, 0x3D72, 0x81},
	{1, 0x3D73, 0x58},
	{1, 0x3D74, 0x88},
	{1, 0x3D75, 0x10},
	{1, 0x3D76, 0xC0},
	{1, 0x3D77, 0xB1},
	{1, 0x3D78, 0x5E},
	{1, 0x3D79, 0x96},
	{1, 0x3D7A, 0x53},
	{1, 0x3D7B, 0x82},
	{1, 0x3D7C, 0x5E},
	{1, 0x3D7D, 0x52},
	{1, 0x3D7E, 0x66},
	{1, 0x3D7F, 0x80},
	{1, 0x3D80, 0x58},
	{1, 0x3D81, 0x83},
	{1, 0x3D82, 0x64},
	{1, 0x3D83, 0x64},
	{1, 0x3D84, 0x80},
	{1, 0x3D85, 0x5B},
	{1, 0x3D86, 0x81},
	{1, 0x3D87, 0x5A},
	{1, 0x3D88, 0x1D},
	{1, 0x3D89, 0x0C},
	{1, 0x3D8A, 0x80},
	{1, 0x3D8B, 0x55},
	{1, 0x3D8C, 0x30},
	{1, 0x3D8D, 0x60},
	{1, 0x3D8E, 0x41},
	{1, 0x3D8F, 0x82},
	{1, 0x3D90, 0x42},
	{1, 0x3D91, 0xB2},
	{1, 0x3D92, 0x42},
	{1, 0x3D93, 0x80},
	{1, 0x3D94, 0x40},
	{1, 0x3D95, 0x81},
	{1, 0x3D96, 0x40},
	{1, 0x3D97, 0x89},
	{1, 0x3D98, 0x06},
	{1, 0x3D99, 0xC0},
	{1, 0x3D9A, 0x41},
	{1, 0x3D9B, 0x80},
	{1, 0x3D9C, 0x42},
	{1, 0x3D9D, 0x85},
	{1, 0x3D9E, 0x44},
	{1, 0x3D9F, 0x83},
	{1, 0x3DA0, 0x43},
	{1, 0x3DA1, 0x82},
	{1, 0x3DA2, 0x6A},
	{1, 0x3DA3, 0x83},
	{1, 0x3DA4, 0x6B},
	{1, 0x3DA5, 0x8D},
	{1, 0x3DA6, 0x43},
	{1, 0x3DA7, 0x83},
	{1, 0x3DA8, 0x44},
	{1, 0x3DA9, 0x81},
	{1, 0x3DAA, 0x41},
	{1, 0x3DAB, 0x85},
	{1, 0x3DAC, 0x06},
	{1, 0x3DAD, 0xC0},
	{1, 0x3DAE, 0x8C},
	{1, 0x3DAF, 0x30},
	{1, 0x3DB0, 0xA4},
	{1, 0x3DB1, 0x67},
	{1, 0x3DB2, 0x81},
	{1, 0x3DB3, 0x42},
	{1, 0x3DB4, 0x82},
	{1, 0x3DB5, 0x65},
	{1, 0x3DB6, 0x65},
	{1, 0x3DB7, 0x81},
	{1, 0x3DB8, 0x69},
	{1, 0x3DB9, 0x6A},
	{1, 0x3DBA, 0x96},
	{1, 0x3DBB, 0x40},
	{1, 0x3DBC, 0x82},
	{1, 0x3DBD, 0x40},
	{1, 0x3DBE, 0x89},
	{1, 0x3DBF, 0x06},
	{1, 0x3DC0, 0xC0},
	{1, 0x3DC1, 0x41},
	{1, 0x3DC2, 0x80},
	{1, 0x3DC3, 0x42},
	{1, 0x3DC4, 0x85},
	{1, 0x3DC5, 0x44},
	{1, 0x3DC6, 0x83},
	{1, 0x3DC7, 0x43},
	{1, 0x3DC8, 0x92},
	{1, 0x3DC9, 0x43},
	{1, 0x3DCA, 0x83},
	{1, 0x3DCB, 0x44},
	{1, 0x3DCC, 0x85},
	{1, 0x3DCD, 0x41},
	{1, 0x3DCE, 0x81},
	{1, 0x3DCF, 0x06},
	{1, 0x3DD0, 0xC0},
	{1, 0x3DD1, 0x81},
	{1, 0x3DD2, 0x6A},
	{1, 0x3DD3, 0x83},
	{1, 0x3DD4, 0x6B},
	{1, 0x3DD5, 0x82},
	{1, 0x3DD6, 0x42},
	{1, 0x3DD7, 0xA0},
	{1, 0x3DD8, 0x40},
	{1, 0x3DD9, 0x84},
	{1, 0x3DDA, 0x38},
	{1, 0x3DDB, 0xA8},
	{1, 0x3DDC, 0x33},
	{1, 0x3DDD, 0x00},
	{1, 0x3DDE, 0x28},
	{1, 0x3DDF, 0x30},
	{1, 0x3DE0, 0x70},
	{1, 0x3DE1, 0x00},
	{1, 0x3DE2, 0x6F},
	{1, 0x3DE3, 0x40},
	{1, 0x3DE4, 0x14},
	{1, 0x3DE5, 0x0E},
	{1, 0x3DE6, 0x23},
	{1, 0x3DE7, 0xC2},
	{1, 0x3DE8, 0x41},
	{1, 0x3DE9, 0x82},
	{1, 0x3DEA, 0x42},
	{1, 0x3DEB, 0x00},
	{1, 0x3DEC, 0xC0},
	{1, 0x3DED, 0x5D},
	{1, 0x3DEE, 0x80},
	{1, 0x3DEF, 0x5A},
	{1, 0x3DF0, 0x80},
	{1, 0x3DF1, 0x57},
	{1, 0x3DF2, 0x84},
	{1, 0x3DF3, 0x64},
	{1, 0x3DF4, 0x80},
	{1, 0x3DF5, 0x55},
	{1, 0x3DF6, 0x86},
	{1, 0x3DF7, 0x64},
	{1, 0x3DF8, 0x80},
	{1, 0x3DF9, 0x65},
	{1, 0x3DFA, 0x88},
	{1, 0x3DFB, 0x65},
	{1, 0x3DFC, 0x82},
	{1, 0x3DFD, 0x54},
	{1, 0x3DFE, 0x80},
	{1, 0x3DFF, 0x58},
	{1, 0x3E00, 0x80},
	{1, 0x3E01, 0x00},
	{1, 0x3E02, 0xC0},
	{1, 0x3E03, 0x86},
	{1, 0x3E04, 0x42},
	{1, 0x3E05, 0x82},
	{1, 0x3E06, 0x10},
	{1, 0x3E07, 0x30},
	{1, 0x3E08, 0x9C},
	{1, 0x3E09, 0x5C},
	{1, 0x3E0A, 0x80},
	{1, 0x3E0B, 0x6E},
	{1, 0x3E0C, 0x86},
	{1, 0x3E0D, 0x5B},
	{1, 0x3E0E, 0x80},
	{1, 0x3E0F, 0x63},
	{1, 0x3E10, 0x9E},
	{1, 0x3E11, 0x59},
	{1, 0x3E12, 0x8C},
	{1, 0x3E13, 0x5E},
	{1, 0x3E14, 0x8A},
	{1, 0x3E15, 0x6C},
	{1, 0x3E16, 0x80},
	{1, 0x3E17, 0x6D},
	{1, 0x3E18, 0x81},
	{1, 0x3E19, 0x5F},
	{1, 0x3E1A, 0x60},
	{1, 0x3E1B, 0x61},
	{1, 0x3E1C, 0x88},
	{1, 0x3E1D, 0x10},
	{1, 0x3E1E, 0x30},
	{1, 0x3E1F, 0x66},
	{1, 0x3E20, 0x83},
	{1, 0x3E21, 0x6E},
	{1, 0x3E22, 0x80},
	{1, 0x3E23, 0x64},
	{1, 0x3E24, 0x87},
	{1, 0x3E25, 0x64},
	{1, 0x3E26, 0x30},
	{1, 0x3E27, 0x50},
	{1, 0x3E28, 0xD3},
	{1, 0x3E29, 0x6A},
	{1, 0x3E2A, 0x6B},
	{1, 0x3E2B, 0xAD},
	{1, 0x3E2C, 0x30},
	{1, 0x3E2D, 0x94},
	{1, 0x3E2E, 0x67},
	{1, 0x3E2F, 0x84},
	{1, 0x3E30, 0x65},
	{1, 0x3E31, 0x82},
	{1, 0x3E32, 0x4D},
	{1, 0x3E33, 0x83},
	{1, 0x3E34, 0x65},
	{1, 0x3E35, 0x30},
	{1, 0x3E36, 0x50},
	{1, 0x3E37, 0xA7},
	{1, 0x3E38, 0x43},
	{1, 0x3E39, 0x06},
	{1, 0x3E3A, 0x00},
	{1, 0x3E3B, 0x8D},
	{1, 0x3E3C, 0x45},
	{1, 0x3E3D, 0x9A},
	{1, 0x3E3E, 0x6A},
	{1, 0x3E3F, 0x6B},
	{1, 0x3E40, 0x45},
	{1, 0x3E41, 0x85},
	{1, 0x3E42, 0x06},
	{1, 0x3E43, 0x00},
	{1, 0x3E44, 0x81},
	{1, 0x3E45, 0x43},
	{1, 0x3E46, 0x8A},
	{1, 0x3E47, 0x6F},
	{1, 0x3E48, 0x96},
	{1, 0x3E49, 0x30},
	{1, 0x3E4A, 0x90},
	{1, 0x3E4B, 0x67},
	{1, 0x3E4C, 0x64},
	{1, 0x3E4D, 0x88},
	{1, 0x3E4E, 0x64},
	{1, 0x3E4F, 0x80},
	{1, 0x3E50, 0x65},
	{1, 0x3E51, 0x82},
	{1, 0x3E52, 0x10},
	{1, 0x3E53, 0xC0},
	{1, 0x3E54, 0x84},
	{1, 0x3E55, 0x65},
	{1, 0x3E56, 0xEF},
	{1, 0x3E57, 0x10},
	{1, 0x3E58, 0xC0},
	{1, 0x3E59, 0x66},
	{1, 0x3E5A, 0x85},
	{1, 0x3E5B, 0x64},
	{1, 0x3E5C, 0x81},
	{1, 0x3E5D, 0x17},
	{1, 0x3E5E, 0x00},
	{1, 0x3E5F, 0x80},
	{1, 0x3E60, 0x20},
	{1, 0x3E61, 0x0D},
	{1, 0x3E62, 0x80},
	{1, 0x3E63, 0x18},
	{1, 0x3E64, 0x0C},
	{1, 0x3E65, 0x80},
	{1, 0x3E66, 0x64},
	{1, 0x3E67, 0x30},
	{1, 0x3E68, 0x60},
	{1, 0x3E69, 0x41},
	{1, 0x3E6A, 0x82},
	{1, 0x3E6B, 0x42},
	{1, 0x3E6C, 0xB2},
	{1, 0x3E6D, 0x42},
	{1, 0x3E6E, 0x80},
	{1, 0x3E6F, 0x40},
	{1, 0x3E70, 0x82},
	{1, 0x3E71, 0x40},
	{1, 0x3E72, 0x4C},
	{1, 0x3E73, 0x45},
	{1, 0x3E74, 0x92},
	{1, 0x3E75, 0x6A},
	{1, 0x3E76, 0x6B},
	{1, 0x3E77, 0x9B},
	{1, 0x3E78, 0x45},
	{1, 0x3E79, 0x81},
	{1, 0x3E7A, 0x4C},
	{1, 0x3E7B, 0x40},
	{1, 0x3E7C, 0x8C},
	{1, 0x3E7D, 0x30},
	{1, 0x3E7E, 0xA4},
	{1, 0x3E7F, 0x67},
	{1, 0x3E80, 0x85},
	{1, 0x3E81, 0x65},
	{1, 0x3E82, 0x87},
	{1, 0x3E83, 0x65},
	{1, 0x3E84, 0x30},
	{1, 0x3E85, 0x60},
	{1, 0x3E86, 0xD3},
	{1, 0x3E87, 0x6A},
	{1, 0x3E88, 0x6B},
	{1, 0x3E89, 0xAC},
	{1, 0x3E8A, 0x6C},
	{1, 0x3E8B, 0x32},
	{1, 0x3E8C, 0xA8},
	{1, 0x3E8D, 0x80},
	{1, 0x3E8E, 0x28},
	{1, 0x3E8F, 0x30},
	{1, 0x3E90, 0x70},
	{1, 0x3E91, 0x00},
	{1, 0x3E92, 0x80},
	{1, 0x3E93, 0x40},
	{1, 0x3E94, 0x4C},
	{1, 0x3E95, 0xBD},
	{1, 0x3E96, 0x00},
	{1, 0x3E97, 0x0E},
	{1, 0x3E98, 0xBE},
	{1, 0x3E99, 0x44},
	{1, 0x3E9A, 0x88},
	{1, 0x3E9B, 0x44},
	{1, 0x3E9C, 0xBC},
	{1, 0x3E9D, 0x78},
	{1, 0x3E9E, 0x09},
	{1, 0x3E9F, 0x00},
	{1, 0x3EA0, 0x89},
	{1, 0x3EA1, 0x04},
	{1, 0x3EA2, 0x80},
	{1, 0x3EA3, 0x80},
	{1, 0x3EA4, 0x02},
	{1, 0x3EA5, 0x40},
	{1, 0x3EA6, 0x86},
	{1, 0x3EA7, 0x09},
	{1, 0x3EA8, 0x00},
	{1, 0x3EA9, 0x8E},
	{1, 0x3EAA, 0x09},
	{1, 0x3EAB, 0x00},
	{1, 0x3EAC, 0x80},
	{1, 0x3EAD, 0x02},
	{1, 0x3EAE, 0x40},
	{1, 0x3EAF, 0x80},
	{1, 0x3EB0, 0x04},
	{1, 0x3EB1, 0x80},
	{1, 0x3EB2, 0x88},
	{1, 0x3EB3, 0x7D},
	{1, 0x3EB4, 0xA0},
	{1, 0x3EB5, 0x86},
	{1, 0x3EB6, 0x09},
	{1, 0x3EB7, 0x00},
	{1, 0x3EB8, 0x87},
	{1, 0x3EB9, 0x7A},
	{1, 0x3EBA, 0x00},
	{1, 0x3EBB, 0x0E},
	{1, 0x3EBC, 0xC3},
	{1, 0x3EBD, 0x79},
	{1, 0x3EBE, 0x4C},
	{1, 0x3EBF, 0x40},
	{1, 0x3EC0, 0xBF},
	{1, 0x3EC1, 0x70},
	{1, 0x3EC2, 0x00},
	{1, 0x3EC3, 0x00},
	{1, 0x3EC4, 0x00},
	{1, 0x3EC5, 0x00},
	{1, 0x3EC6, 0x00},
	{1, 0x3EC7, 0x00},
	{1, 0x3EC8, 0x00},
	{1, 0x3EC9, 0x00},
	{1, 0x3ECA, 0x00},
	{1, 0x3ECB, 0x00},
	{2, 0x0342, 0x0ECC}, 	// LINE_LENGTH_PCK
	{2, 0x0340, 0x0A10}, 	// FRAME_LENGTH_LINES
	{2, 0x0202, 0x0A01}, 	// COARSE_INTEGRATION_TIME
	{2, 0x0344, 0x0008}, 	// X_ADDR_START
	{2, 0x0348, 0x0CC7}, 	// X_ADDR_END
	{2, 0x0346, 0x0008}, 	// Y_ADDR_START
	{2, 0x034A, 0x0997}, 	// Y_ADDR_END
	{2, 0x034C, 0x0CC0}, 	// X_OUTPUT_SIZE
	{2, 0x034E, 0x0990}, 	// Y_OUTPUT_SIZE
	{2, 0x3040, 0x4041}, 	// READ_MODE
	{2, 0x0400, 0x0000}, 	// SCALING_MODE
	{2, 0x0402, 0x0000}, 	// SPATIAL_SAMPLING
	{2, 0x0404, 0x0010}, 	// SCALE_M
	{2, 0x0408, 0x1010}, 	// RESERVED_CONF_408
	{2, 0x040A, 0x0210}, 	// RESERVED_CONF_40A
	{2, 0x306E, 0x9080}, 	// DATA_PATH_SELECT
	{2, 0x301A, 0x001C}, 	// RESET_REGISTER
	{END_OF_SCRIPT, 0, 0},
};

/*static cam_i2c_msg_t AR0833_capture_6M_script[] = {
	{END_OF_SCRIPT, 0, 0},
};*/

static cam_i2c_msg_t AR0833_6M_script_mipi[] = {
		{2, 0x301A, 0x0019}, 	// RESET_REGISTER
	//DELAY 1
	{TIME_DELAY, 0, 1},
	{2, 0x301A, 0x0218}, 	// RESET_REGISTER
	{2, 0x3042, 0x0000}, 	// RESERVED_MFR_3042
	{2, 0x30C0, 0x1810}, 	// RESERVED_MFR_30C0
	{2, 0x30C8, 0x0018}, 	// RESERVED_MFR_30C8
	{2, 0x30D2, 0x0000}, 	// RESERVED_MFR_30D2
	{2, 0x30D4, 0x3030}, 	// RESERVED_MFR_30D4
	{2, 0x30D6, 0x2200}, 	// RESERVED_MFR_30D6
	{2, 0x30DA, 0x0080}, 	// RESERVED_MFR_30DA
	{2, 0x30DC, 0x0080}, 	// RESERVED_MFR_30DC
	{2, 0x30EE, 0x0340}, 	// RESERVED_MFR_30EE
	{2, 0x316A, 0x8800}, 	// RESERVED_MFR_316A
	{2, 0x316C, 0x8200}, 	// RESERVED_MFR_316C
	{2, 0x316E, 0x8200}, 	// RESERVED_MFR_316E
	{2, 0x3172, 0x0286}, 	// ANALOG_CONTROL2
	{2, 0x3174, 0x8000}, 	// RESERVED_MFR_3174
	{2, 0x317C, 0xE103}, 	// RESERVED_MFR_317C
	{2, 0x3180, 0xB080}, 	// RESERVED_MFR_3180
	{2, 0x31E0, 0x0741}, 	// RESERVED_MFR_31E0
	{2, 0x31E6, 0x0000}, 	// RESERVED_MFR_31E6
	{2, 0x3ECC, 0x0056}, 	// RESERVED_MFR_3ECC
	{2, 0x3ED0, 0xA666}, 	// RESERVED_MFR_3ED0
	{2, 0x3ED2, 0x6664}, 	// RESERVED_MFR_3ED2
	{2, 0x3ED4, 0x6ACC}, 	// RESERVED_MFR_3ED4
	{2, 0x3ED8, 0x7488}, 	// RESERVED_MFR_3ED8
	{2, 0x3EDA, 0x77CB}, 	// RESERVED_MFR_3EDA
	{2, 0x3EDE, 0x6664}, 	// RESERVED_MFR_3EDE
	{2, 0x3EE0, 0x26D5}, 	// RESERVED_MFR_3EE0
	{2, 0x3EE4, 0x3548}, 	// RESERVED_MFR_3EE4
	{2, 0x3EE6, 0xB10C}, 	// RESERVED_MFR_3EE6
	{2, 0x3EE8, 0x6E79}, 	// RESERVED_MFR_3EE8
	{2, 0x3EEA, 0xC8B9}, 	// RESERVED_MFR_3EEA
	{2, 0x3EFA, 0xA656}, 	// RESERVED_MFR_3EFA
	{2, 0x3EFE, 0x99CC}, 	// RESERVED_MFR_3EFE
	{2, 0x3F00, 0x0028}, 	// RESERVED_MFR_3F00
	{2, 0x3F02, 0x0140}, 	// RESERVED_MFR_3F02
	{2, 0x3F04, 0x0002}, 	// RESERVED_MFR_3F04
	{2, 0x3F06, 0x0004}, 	// RESERVED_MFR_3F06
	{2, 0x3F08, 0x0008}, 	// RESERVED_MFR_3F08
	{2, 0x3F0A, 0x0B09}, 	// RESERVED_MFR_3F0A
	{2, 0x3F0C, 0x0302}, 	// RESERVED_MFR_3F0C
	{2, 0x3F10, 0x0505}, 	// RESERVED_MFR_3F10
	{2, 0x3F12, 0x0303}, 	// RESERVED_MFR_3F12
	{2, 0x3F14, 0x0101}, 	// RESERVED_MFR_3F14
	{2, 0x3F16, 0x2020}, 	// RESERVED_MFR_3F16
	{2, 0x3F18, 0x0404}, 	// RESERVED_MFR_3F18
	{2, 0x3F1A, 0x7070}, 	// RESERVED_MFR_3F1A
	{2, 0x3F1C, 0x003A}, 	// RESERVED_MFR_3F1C
	{2, 0x3F1E, 0x003C}, 	// RESERVED_MFR_3F1E
	{2, 0x3F20, 0x0209}, 	// RESERVED_MFR_3F20
	{2, 0x3F2C, 0x2210}, 	// RESERVED_MFR_3F2C
	{2, 0x3F38, 0x44A8}, 	// RESERVED_MFR_3F38
	{2, 0x3F40, 0x2020}, 	// RESERVED_MFR_3F40
	{2, 0x3F42, 0x0808}, 	// RESERVED_MFR_3F42
	{2, 0x3F44, 0x0101}, 	// RESERVED_MFR_3F44
	{2, 0x0300, 0x0005}, 	// VT_PIX_CLK_DIV
	{2, 0x0302, 0x0001}, 	// VT_SYS_CLK_DIV
	{2, 0x0304, 0x0004}, 	// PRE_PLL_CLK_DIV
	{2, 0x0306, 0x007A}, 	// PLL_MULTIPLIER
	{2, 0x0308, 0x000A}, 	// OP_PIX_CLK_DIV
	{2, 0x030A, 0x0001}, 	// OP_SYS_CLK_DIV
	{2, 0x3064, 0x7800}, 	// RESERVED_MFR_3064
	//DELAY=1
	{TIME_DELAY, 0, 1},
	{2, 0x31B0, 0x0060}, 	// FRAME_PREAMBLE
	{2, 0x31B2, 0x0042}, 	// LINE_PREAMBLE
	{2, 0x31B4, 0x4C36}, 	// MIPI_TIMING_0
	{2, 0x31B6, 0x5218}, 	// MIPI_TIMING_1
	{2, 0x31B8, 0x404A}, 	// MIPI_TIMING_2
	{2, 0x31BA, 0x028A}, 	// MIPI_TIMING_3
	{2, 0x31BC, 0x0008}, 	// MIPI_TIMING_4
	{2, 0x31AE, 0x0202},
	{1, 0x3D00, 0x04},
	{1, 0x3D01, 0x71},
	{1, 0x3D02, 0xC9},
	{1, 0x3D03, 0xFF},
	{1, 0x3D04, 0xFF},
	{1, 0x3D05, 0xFF},
	{1, 0x3D06, 0xFF},
	{1, 0x3D07, 0xFF},
	{1, 0x3D08, 0x6F},
	{1, 0x3D09, 0x40},
	{1, 0x3D0A, 0x14},
	{1, 0x3D0B, 0x0E},
	{1, 0x3D0C, 0x23},
	{1, 0x3D0D, 0xC2},
	{1, 0x3D0E, 0x41},
	{1, 0x3D0F, 0x20},
	{1, 0x3D10, 0x30},
	{1, 0x3D11, 0x54},
	{1, 0x3D12, 0x80},
	{1, 0x3D13, 0x42},
	{1, 0x3D14, 0x00},
	{1, 0x3D15, 0xC0},
	{1, 0x3D16, 0x83},
	{1, 0x3D17, 0x57},
	{1, 0x3D18, 0x84},
	{1, 0x3D19, 0x64},
	{1, 0x3D1A, 0x64},
	{1, 0x3D1B, 0x55},
	{1, 0x3D1C, 0x80},
	{1, 0x3D1D, 0x23},
	{1, 0x3D1E, 0x00},
	{1, 0x3D1F, 0x65},
	{1, 0x3D20, 0x65},
	{1, 0x3D21, 0x82},
	{1, 0x3D22, 0x00},
	{1, 0x3D23, 0xC0},
	{1, 0x3D24, 0x6E},
	{1, 0x3D25, 0x80},
	{1, 0x3D26, 0x50},
	{1, 0x3D27, 0x51},
	{1, 0x3D28, 0x83},
	{1, 0x3D29, 0x42},
	{1, 0x3D2A, 0x83},
	{1, 0x3D2B, 0x58},
	{1, 0x3D2C, 0x6E},
	{1, 0x3D2D, 0x80},
	{1, 0x3D2E, 0x5F},
	{1, 0x3D2F, 0x87},
	{1, 0x3D30, 0x63},
	{1, 0x3D31, 0x82},
	{1, 0x3D32, 0x5B},
	{1, 0x3D33, 0x82},
	{1, 0x3D34, 0x59},
	{1, 0x3D35, 0x80},
	{1, 0x3D36, 0x5A},
	{1, 0x3D37, 0x5E},
	{1, 0x3D38, 0xBD},
	{1, 0x3D39, 0x59},
	{1, 0x3D3A, 0x59},
	{1, 0x3D3B, 0x9D},
	{1, 0x3D3C, 0x6C},
	{1, 0x3D3D, 0x80},
	{1, 0x3D3E, 0x6D},
	{1, 0x3D3F, 0xA3},
	{1, 0x3D40, 0x50},
	{1, 0x3D41, 0x80},
	{1, 0x3D42, 0x51},
	{1, 0x3D43, 0x82},
	{1, 0x3D44, 0x58},
	{1, 0x3D45, 0x80},
	{1, 0x3D46, 0x66},
	{1, 0x3D47, 0x83},
	{1, 0x3D48, 0x64},
	{1, 0x3D49, 0x64},
	{1, 0x3D4A, 0x80},
	{1, 0x3D4B, 0x30},
	{1, 0x3D4C, 0x50},
	{1, 0x3D4D, 0xDC},
	{1, 0x3D4E, 0x6A},
	{1, 0x3D4F, 0x83},
	{1, 0x3D50, 0x6B},
	{1, 0x3D51, 0xAA},
	{1, 0x3D52, 0x30},
	{1, 0x3D53, 0x94},
	{1, 0x3D54, 0x67},
	{1, 0x3D55, 0x84},
	{1, 0x3D56, 0x65},
	{1, 0x3D57, 0x65},
	{1, 0x3D58, 0x81},
	{1, 0x3D59, 0x4D},
	{1, 0x3D5A, 0x68},
	{1, 0x3D5B, 0x6A},
	{1, 0x3D5C, 0xAC},
	{1, 0x3D5D, 0x06},
	{1, 0x3D5E, 0x08},
	{1, 0x3D5F, 0x8D},
	{1, 0x3D60, 0x45},
	{1, 0x3D61, 0x96},
	{1, 0x3D62, 0x45},
	{1, 0x3D63, 0x85},
	{1, 0x3D64, 0x6A},
	{1, 0x3D65, 0x83},
	{1, 0x3D66, 0x6B},
	{1, 0x3D67, 0x06},
	{1, 0x3D68, 0x08},
	{1, 0x3D69, 0xA9},
	{1, 0x3D6A, 0x30},
	{1, 0x3D6B, 0x90},
	{1, 0x3D6C, 0x67},
	{1, 0x3D6D, 0x64},
	{1, 0x3D6E, 0x64},
	{1, 0x3D6F, 0x89},
	{1, 0x3D70, 0x65},
	{1, 0x3D71, 0x65},
	{1, 0x3D72, 0x81},
	{1, 0x3D73, 0x58},
	{1, 0x3D74, 0x88},
	{1, 0x3D75, 0x10},
	{1, 0x3D76, 0xC0},
	{1, 0x3D77, 0xB1},
	{1, 0x3D78, 0x5E},
	{1, 0x3D79, 0x96},
	{1, 0x3D7A, 0x53},
	{1, 0x3D7B, 0x82},
	{1, 0x3D7C, 0x5E},
	{1, 0x3D7D, 0x52},
	{1, 0x3D7E, 0x66},
	{1, 0x3D7F, 0x80},
	{1, 0x3D80, 0x58},
	{1, 0x3D81, 0x83},
	{1, 0x3D82, 0x64},
	{1, 0x3D83, 0x64},
	{1, 0x3D84, 0x80},
	{1, 0x3D85, 0x5B},
	{1, 0x3D86, 0x81},
	{1, 0x3D87, 0x5A},
	{1, 0x3D88, 0x1D},
	{1, 0x3D89, 0x0C},
	{1, 0x3D8A, 0x80},
	{1, 0x3D8B, 0x55},
	{1, 0x3D8C, 0x30},
	{1, 0x3D8D, 0x60},
	{1, 0x3D8E, 0x41},
	{1, 0x3D8F, 0x82},
	{1, 0x3D90, 0x42},
	{1, 0x3D91, 0xB2},
	{1, 0x3D92, 0x42},
	{1, 0x3D93, 0x80},
	{1, 0x3D94, 0x40},
	{1, 0x3D95, 0x81},
	{1, 0x3D96, 0x40},
	{1, 0x3D97, 0x89},
	{1, 0x3D98, 0x06},
	{1, 0x3D99, 0xC0},
	{1, 0x3D9A, 0x41},
	{1, 0x3D9B, 0x80},
	{1, 0x3D9C, 0x42},
	{1, 0x3D9D, 0x85},
	{1, 0x3D9E, 0x44},
	{1, 0x3D9F, 0x83},
	{1, 0x3DA0, 0x43},
	{1, 0x3DA1, 0x82},
	{1, 0x3DA2, 0x6A},
	{1, 0x3DA3, 0x83},
	{1, 0x3DA4, 0x6B},
	{1, 0x3DA5, 0x8D},
	{1, 0x3DA6, 0x43},
	{1, 0x3DA7, 0x83},
	{1, 0x3DA8, 0x44},
	{1, 0x3DA9, 0x81},
	{1, 0x3DAA, 0x41},
	{1, 0x3DAB, 0x85},
	{1, 0x3DAC, 0x06},
	{1, 0x3DAD, 0xC0},
	{1, 0x3DAE, 0x8C},
	{1, 0x3DAF, 0x30},
	{1, 0x3DB0, 0xA4},
	{1, 0x3DB1, 0x67},
	{1, 0x3DB2, 0x81},
	{1, 0x3DB3, 0x42},
	{1, 0x3DB4, 0x82},
	{1, 0x3DB5, 0x65},
	{1, 0x3DB6, 0x65},
	{1, 0x3DB7, 0x81},
	{1, 0x3DB8, 0x69},
	{1, 0x3DB9, 0x6A},
	{1, 0x3DBA, 0x96},
	{1, 0x3DBB, 0x40},
	{1, 0x3DBC, 0x82},
	{1, 0x3DBD, 0x40},
	{1, 0x3DBE, 0x89},
	{1, 0x3DBF, 0x06},
	{1, 0x3DC0, 0xC0},
	{1, 0x3DC1, 0x41},
	{1, 0x3DC2, 0x80},
	{1, 0x3DC3, 0x42},
	{1, 0x3DC4, 0x85},
	{1, 0x3DC5, 0x44},
	{1, 0x3DC6, 0x83},
	{1, 0x3DC7, 0x43},
	{1, 0x3DC8, 0x92},
	{1, 0x3DC9, 0x43},
	{1, 0x3DCA, 0x83},
	{1, 0x3DCB, 0x44},
	{1, 0x3DCC, 0x85},
	{1, 0x3DCD, 0x41},
	{1, 0x3DCE, 0x81},
	{1, 0x3DCF, 0x06},
	{1, 0x3DD0, 0xC0},
	{1, 0x3DD1, 0x81},
	{1, 0x3DD2, 0x6A},
	{1, 0x3DD3, 0x83},
	{1, 0x3DD4, 0x6B},
	{1, 0x3DD5, 0x82},
	{1, 0x3DD6, 0x42},
	{1, 0x3DD7, 0xA0},
	{1, 0x3DD8, 0x40},
	{1, 0x3DD9, 0x84},
	{1, 0x3DDA, 0x38},
	{1, 0x3DDB, 0xA8},
	{1, 0x3DDC, 0x33},
	{1, 0x3DDD, 0x00},
	{1, 0x3DDE, 0x28},
	{1, 0x3DDF, 0x30},
	{1, 0x3DE0, 0x70},
	{1, 0x3DE1, 0x00},
	{1, 0x3DE2, 0x6F},
	{1, 0x3DE3, 0x40},
	{1, 0x3DE4, 0x14},
	{1, 0x3DE5, 0x0E},
	{1, 0x3DE6, 0x23},
	{1, 0x3DE7, 0xC2},
	{1, 0x3DE8, 0x41},
	{1, 0x3DE9, 0x82},
	{1, 0x3DEA, 0x42},
	{1, 0x3DEB, 0x00},
	{1, 0x3DEC, 0xC0},
	{1, 0x3DED, 0x5D},
	{1, 0x3DEE, 0x80},
	{1, 0x3DEF, 0x5A},
	{1, 0x3DF0, 0x80},
	{1, 0x3DF1, 0x57},
	{1, 0x3DF2, 0x84},
	{1, 0x3DF3, 0x64},
	{1, 0x3DF4, 0x80},
	{1, 0x3DF5, 0x55},
	{1, 0x3DF6, 0x86},
	{1, 0x3DF7, 0x64},
	{1, 0x3DF8, 0x80},
	{1, 0x3DF9, 0x65},
	{1, 0x3DFA, 0x88},
	{1, 0x3DFB, 0x65},
	{1, 0x3DFC, 0x82},
	{1, 0x3DFD, 0x54},
	{1, 0x3DFE, 0x80},
	{1, 0x3DFF, 0x58},
	{1, 0x3E00, 0x80},
	{1, 0x3E01, 0x00},
	{1, 0x3E02, 0xC0},
	{1, 0x3E03, 0x86},
	{1, 0x3E04, 0x42},
	{1, 0x3E05, 0x82},
	{1, 0x3E06, 0x10},
	{1, 0x3E07, 0x30},
	{1, 0x3E08, 0x9C},
	{1, 0x3E09, 0x5C},
	{1, 0x3E0A, 0x80},
	{1, 0x3E0B, 0x6E},
	{1, 0x3E0C, 0x86},
	{1, 0x3E0D, 0x5B},
	{1, 0x3E0E, 0x80},
	{1, 0x3E0F, 0x63},
	{1, 0x3E10, 0x9E},
	{1, 0x3E11, 0x59},
	{1, 0x3E12, 0x8C},
	{1, 0x3E13, 0x5E},
	{1, 0x3E14, 0x8A},
	{1, 0x3E15, 0x6C},
	{1, 0x3E16, 0x80},
	{1, 0x3E17, 0x6D},
	{1, 0x3E18, 0x81},
	{1, 0x3E19, 0x5F},
	{1, 0x3E1A, 0x60},
	{1, 0x3E1B, 0x61},
	{1, 0x3E1C, 0x88},
	{1, 0x3E1D, 0x10},
	{1, 0x3E1E, 0x30},
	{1, 0x3E1F, 0x66},
	{1, 0x3E20, 0x83},
	{1, 0x3E21, 0x6E},
	{1, 0x3E22, 0x80},
	{1, 0x3E23, 0x64},
	{1, 0x3E24, 0x87},
	{1, 0x3E25, 0x64},
	{1, 0x3E26, 0x30},
	{1, 0x3E27, 0x50},
	{1, 0x3E28, 0xD3},
	{1, 0x3E29, 0x6A},
	{1, 0x3E2A, 0x6B},
	{1, 0x3E2B, 0xAD},
	{1, 0x3E2C, 0x30},
	{1, 0x3E2D, 0x94},
	{1, 0x3E2E, 0x67},
	{1, 0x3E2F, 0x84},
	{1, 0x3E30, 0x65},
	{1, 0x3E31, 0x82},
	{1, 0x3E32, 0x4D},
	{1, 0x3E33, 0x83},
	{1, 0x3E34, 0x65},
	{1, 0x3E35, 0x30},
	{1, 0x3E36, 0x50},
	{1, 0x3E37, 0xA7},
	{1, 0x3E38, 0x43},
	{1, 0x3E39, 0x06},
	{1, 0x3E3A, 0x00},
	{1, 0x3E3B, 0x8D},
	{1, 0x3E3C, 0x45},
	{1, 0x3E3D, 0x9A},
	{1, 0x3E3E, 0x6A},
	{1, 0x3E3F, 0x6B},
	{1, 0x3E40, 0x45},
	{1, 0x3E41, 0x85},
	{1, 0x3E42, 0x06},
	{1, 0x3E43, 0x00},
	{1, 0x3E44, 0x81},
	{1, 0x3E45, 0x43},
	{1, 0x3E46, 0x8A},
	{1, 0x3E47, 0x6F},
	{1, 0x3E48, 0x96},
	{1, 0x3E49, 0x30},
	{1, 0x3E4A, 0x90},
	{1, 0x3E4B, 0x67},
	{1, 0x3E4C, 0x64},
	{1, 0x3E4D, 0x88},
	{1, 0x3E4E, 0x64},
	{1, 0x3E4F, 0x80},
	{1, 0x3E50, 0x65},
	{1, 0x3E51, 0x82},
	{1, 0x3E52, 0x10},
	{1, 0x3E53, 0xC0},
	{1, 0x3E54, 0x84},
	{1, 0x3E55, 0x65},
	{1, 0x3E56, 0xEF},
	{1, 0x3E57, 0x10},
	{1, 0x3E58, 0xC0},
	{1, 0x3E59, 0x66},
	{1, 0x3E5A, 0x85},
	{1, 0x3E5B, 0x64},
	{1, 0x3E5C, 0x81},
	{1, 0x3E5D, 0x17},
	{1, 0x3E5E, 0x00},
	{1, 0x3E5F, 0x80},
	{1, 0x3E60, 0x20},
	{1, 0x3E61, 0x0D},
	{1, 0x3E62, 0x80},
	{1, 0x3E63, 0x18},
	{1, 0x3E64, 0x0C},
	{1, 0x3E65, 0x80},
	{1, 0x3E66, 0x64},
	{1, 0x3E67, 0x30},
	{1, 0x3E68, 0x60},
	{1, 0x3E69, 0x41},
	{1, 0x3E6A, 0x82},
	{1, 0x3E6B, 0x42},
	{1, 0x3E6C, 0xB2},
	{1, 0x3E6D, 0x42},
	{1, 0x3E6E, 0x80},
	{1, 0x3E6F, 0x40},
	{1, 0x3E70, 0x82},
	{1, 0x3E71, 0x40},
	{1, 0x3E72, 0x4C},
	{1, 0x3E73, 0x45},
	{1, 0x3E74, 0x92},
	{1, 0x3E75, 0x6A},
	{1, 0x3E76, 0x6B},
	{1, 0x3E77, 0x9B},
	{1, 0x3E78, 0x45},
	{1, 0x3E79, 0x81},
	{1, 0x3E7A, 0x4C},
	{1, 0x3E7B, 0x40},
	{1, 0x3E7C, 0x8C},
	{1, 0x3E7D, 0x30},
	{1, 0x3E7E, 0xA4},
	{1, 0x3E7F, 0x67},
	{1, 0x3E80, 0x85},
	{1, 0x3E81, 0x65},
	{1, 0x3E82, 0x87},
	{1, 0x3E83, 0x65},
	{1, 0x3E84, 0x30},
	{1, 0x3E85, 0x60},
	{1, 0x3E86, 0xD3},
	{1, 0x3E87, 0x6A},
	{1, 0x3E88, 0x6B},
	{1, 0x3E89, 0xAC},
	{1, 0x3E8A, 0x6C},
	{1, 0x3E8B, 0x32},
	{1, 0x3E8C, 0xA8},
	{1, 0x3E8D, 0x80},
	{1, 0x3E8E, 0x28},
	{1, 0x3E8F, 0x30},
	{1, 0x3E90, 0x70},
	{1, 0x3E91, 0x00},
	{1, 0x3E92, 0x80},
	{1, 0x3E93, 0x40},
	{1, 0x3E94, 0x4C},
	{1, 0x3E95, 0xBD},
	{1, 0x3E96, 0x00},
	{1, 0x3E97, 0x0E},
	{1, 0x3E98, 0xBE},
	{1, 0x3E99, 0x44},
	{1, 0x3E9A, 0x88},
	{1, 0x3E9B, 0x44},
	{1, 0x3E9C, 0xBC},
	{1, 0x3E9D, 0x78},
	{1, 0x3E9E, 0x09},
	{1, 0x3E9F, 0x00},
	{1, 0x3EA0, 0x89},
	{1, 0x3EA1, 0x04},
	{1, 0x3EA2, 0x80},
	{1, 0x3EA3, 0x80},
	{1, 0x3EA4, 0x02},
	{1, 0x3EA5, 0x40},
	{1, 0x3EA6, 0x86},
	{1, 0x3EA7, 0x09},
	{1, 0x3EA8, 0x00},
	{1, 0x3EA9, 0x8E},
	{1, 0x3EAA, 0x09},
	{1, 0x3EAB, 0x00},
	{1, 0x3EAC, 0x80},
	{1, 0x3EAD, 0x02},
	{1, 0x3EAE, 0x40},
	{1, 0x3EAF, 0x80},
	{1, 0x3EB0, 0x04},
	{1, 0x3EB1, 0x80},
	{1, 0x3EB2, 0x88},
	{1, 0x3EB3, 0x7D},
	{1, 0x3EB4, 0xA0},
	{1, 0x3EB5, 0x86},
	{1, 0x3EB6, 0x09},
	{1, 0x3EB7, 0x00},
	{1, 0x3EB8, 0x87},
	{1, 0x3EB9, 0x7A},
	{1, 0x3EBA, 0x00},
	{1, 0x3EBB, 0x0E},
	{1, 0x3EBC, 0xC3},
	{1, 0x3EBD, 0x79},
	{1, 0x3EBE, 0x4C},
	{1, 0x3EBF, 0x40},
	{1, 0x3EC0, 0xBF},
	{1, 0x3EC1, 0x70},
	{1, 0x3EC2, 0x00},
	{1, 0x3EC3, 0x00},
	{1, 0x3EC4, 0x00},
	{1, 0x3EC5, 0x00},
	{1, 0x3EC6, 0x00},
	{1, 0x3EC7, 0x00},
	{1, 0x3EC8, 0x00},
	{1, 0x3EC9, 0x00},
	{1, 0x3ECA, 0x00},
	{1, 0x3ECB, 0x00},
	{2, 0x0342, 0x138C}, 	// LINE_LENGTH_PCK
	{2, 0x0340, 0x079D}, 	// FRAME_LENGTH_LINES
	{2, 0x0202, 0x0700}, 	// COARSE_INTEGRATION_TIME
	{2, 0x0344, 0x0008}, 	// X_ADDR_START
	{2, 0x0348, 0x0CC7}, 	// X_ADDR_END
	{2, 0x0346, 0x0130}, 	// Y_ADDR_START
	{2, 0x034A, 0x085B}, 	// Y_ADDR_END
	{2, 0x034C, 0x0CC0}, 	// X_OUTPUT_SIZE
	{2, 0x034E, 0x072C}, 	// Y_OUTPUT_SIZE
	{2, 0x3040, 0x4041}, 	// READ_MODE
	{2, 0x0400, 0x0000}, 	// SCALING_MODE
	{2, 0x0402, 0x0000}, 	// SPATIAL_SAMPLING
	{2, 0x0404, 0x0010}, 	// SCALE_M
	{2, 0x0408, 0x1010}, 	// RESERVED_CONF_408
	{2, 0x040A, 0x0210}, 	// RESERVED_CONF_40A
	{2, 0x306E, 0x9080}, 	// DATA_PATH_SELECT
	{2, 0x301A, 0x001C}, 	// RESET_REGISTER
	{END_OF_SCRIPT, 0, 0},	
};

static resolution_param_t  prev_resolution_array[] = {
	{
		.frmsize			= {176, 144},
		.active_frmsize		= {640, 480},
		.active_fps			= 30,
		.size_type			= SIZE_176X144,
		.reg_script[0]		= AR0833_preview_VGA_script,
		.reg_script[1]		= AR0833_VGA_script_mipi,
	},{
		.frmsize			= {320, 240},
		.active_frmsize		= {640, 480},
		.active_fps			= 30,
		.size_type			= SIZE_320X240,
		.reg_script[0]		= AR0833_preview_VGA_script,
		.reg_script[1]		= AR0833_VGA_script_mipi,
	},{
		.frmsize			= {352, 288},
		.active_frmsize		= {640, 480},
		.active_fps			= 30,
		.size_type			= SIZE_352X288,
		.reg_script[0]		= AR0833_preview_VGA_script,
		.reg_script[1]		= AR0833_VGA_script_mipi,
	},{
		.frmsize			= {640, 480},
		.active_frmsize		= {640, 480},
		.active_fps			= 30,
		.size_type			= SIZE_640X480,
		.reg_script[0]		= AR0833_preview_VGA_script,
		.reg_script[1]		= AR0833_VGA_script_mipi,
	}, {
		.frmsize			= {1280, 720},
		.active_frmsize		= {1280, 720},
		.active_fps			= 30,
		.size_type			= SIZE_1280X720,
		.reg_script[0]		= AR0833_preview_720P_script,
		.reg_script[1]		= AR0833_720P_script_mipi,
	}, {
		.frmsize			= {1280, 960},
		.active_frmsize		= {1280, 720},
		.active_fps			= 30,
		.size_type			= SIZE_1280X960,
		.reg_script[0]		= AR0833_preview_960P_script,
		.reg_script[1]		= AR0833_960P_script_mipi,
	}, {
		.frmsize			= {1920, 1080},
		.active_frmsize		= {1920, 1080},
		.active_fps			= 15,
		.size_type			= SIZE_1920X1080,
		.reg_script[0]		= AR0833_preview_1080P_script,
		.reg_script[1]		= AR0833_1080P_script_mipi, //AR0833_1080P_script_mipi,
	}
};

static resolution_param_t  debug_prev_resolution_array[] = {
	{
		.frmsize			= {640, 480},
		.active_frmsize		= {640, 480},
		.active_fps			= 30,
		.size_type			= SIZE_640X480,
		.reg_script[0]		= AR0833_preview_VGA_script,
		.reg_script[1]		= AR0833_VGA_script_mipi,
	}, {
		.frmsize			= {1280, 720},
		.active_frmsize		= {1280, 720},
		.active_fps			= 30,
		.size_type			= SIZE_1280X720,
		.reg_script[0]		= AR0833_preview_720P_script,
		.reg_script[1]		= AR0833_720P_script_mipi,
	}, {
		.frmsize			= {1280, 960},
		.active_frmsize		= {1280, 720},
		.active_fps			= 30,
		.size_type			= SIZE_1280X960,
		.reg_script[0]		= AR0833_preview_960P_script,
		.reg_script[1]		= AR0833_960P_script_mipi,
	}, {
		.frmsize			= {1920, 1080},
		.active_frmsize		= {1920, 1080},
		.active_fps			= 15,
		.size_type			= SIZE_1920X1080,
		.reg_script[0]		= AR0833_preview_1080P_script,
		.reg_script[1]		= AR0833_1080P_script_mipi, //AR0833_1080P_script_mipi,
	}, {
		.frmsize			= {3264, 1836},
		.active_frmsize		= {3264, 1836},
		.active_fps			= 15,
		.size_type			= SIZE_3264X1836,
		.reg_script[0]		= AR0833_preview_6M_script,
		.reg_script[1]		= AR0833_6M_script_mipi, //AR0833_1080P_script_mipi,
	}, {
		.frmsize			= {3264, 2448},
		.active_frmsize		= {3264, 2448},
		.active_fps			= 15,
		.size_type			= SIZE_3264X2448,
		.reg_script[0]		= AR0833_preview_8M_script,
		.reg_script[1]		= AR0833_8M_script_mipi, //AR0833_1080P_script_mipi,
	}
};

static resolution_param_t  capture_resolution_array[] = {
	{
		.frmsize			= {2592, 1944},
		.active_frmsize		= {2592, 1944},
		.active_fps			= 7.5,
		.size_type			= SIZE_2592X1944,
		.reg_script[0]		= AR0833_capture_8M_script,
		.reg_script[1]		= AR0833_8M_script_mipi,
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

extern int aml_i2c_put_word(struct i2c_adapter *adapter, 
		unsigned short dev_addr, unsigned short addr, unsigned short data);

void AR0833_manual_set_aet(unsigned int exp,unsigned int ag,unsigned int vts){
	//unsigned char exp_h = 0, exp_m = 0, exp_l = 0, ag_h = 0, ag_l = 0, vts_h = 0, vts_l = 0;
	struct i2c_adapter *adapter;
	adapter = i2c_get_adapter(4);
	
	aml_i2c_put_word(adapter, 0x36, 0x3012, exp & 0xffff);
	
	//aml_i2c_put_word(adapter, 0x36, 0x0204, ag & 0xffff);
	
	aml_i2c_put_word(adapter, 0x36, 0x0340, vts & 0xffff);	
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
	
	AR0833_manual_set_aet(exp,ag,vts);
	return len;
}

static ssize_t aet_manual_show(struct class *cls,struct class_attribute *attr, char* buf)
{
	size_t len = 0;
	return len;
}

static CLASS_ATTR(aet_debug, 0664, aet_manual_show, aet_manual_store);

/* ar0833 uses exp+ag mode */
static bool AR0833_set_aet_new_step(void *priv, unsigned int new_step, bool exp_mode, bool ag_mode){
  	unsigned int exp = 0, ag = 0, vts = 0;
	camera_priv_data_t *camera_priv_data = (camera_priv_data_t *)priv; 
	sensor_aet_t *sensor_aet_table = camera_priv_data->sensor_aet_table;
	sensor_aet_info_t *sensor_aet_info = camera_priv_data->sensor_aet_info;
	
	if(camera_priv_data == NULL || sensor_aet_table == NULL || sensor_aet_info == NULL)
		return false;	
	if (((!exp_mode) && (!ag_mode)) || (new_step > sensor_aet_info[current_fmt].tbl_max_step))
		return(false);
	else
	{
		camera_priv_data->sensor_aet_step = new_step;
		exp = sensor_aet_table[camera_priv_data->sensor_aet_step].exp;
		ag = sensor_aet_table[camera_priv_data->sensor_aet_step].ag;
		vts = sensor_aet_table[camera_priv_data->sensor_aet_step].vts;
		
		AR0833_manual_set_aet(exp,ag,vts);
		return true;
	}
}


static bool AR0833_check_mains_freq(void *priv){// when the fr change,we need to change the aet table
    //int detection; 
    //struct i2c_adapter *adapter;
    return true;
}

bool AR0833_set_af_new_step(void *priv,unsigned int af_step){
    //struct i2c_adapter *adapter;
    char buf[3];
    if(af_step == last_af_step)
        return true;
	/*
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
    byte_h  = (vcm_data >> 8) & 0x000000ff;
    byte_l  = (vcm_data >> 0) & 0x000000ff;
*/	
	last_af_step = af_step;
    buf[0] = (af_step>>4)&0xff;
    buf[1] = (af_step<<4)&0xff;
    //adapter = i2c_get_adapter(4);
   // my_i2c_put_byte_add8(adapter,0x0c,buf,2);
    return true;

}



void AR0833_set_new_format(void *priv,int width,int height,int fr){
    int index = 0;
    camera_priv_data_t *camera_priv_data;
    configure_t *configure;
    
    current_fr = fr;
    camera_priv_data = (camera_priv_data_t *)priv;
    configure = camera_priv_data->configure;
    
    if(camera_priv_data == NULL)
    	return;
    printk("sum:%d,mode:%d,fr:%d\n",configure->aet.sum,ar0833_work_mode,fr);
    while(index < configure->aet.sum){
        if(width == configure->aet.aet[index].info->fmt_hactive && height == configure->aet.aet[index].info->fmt_vactive \
                && fr == configure->aet.aet[index].info->fmt_main_fr && ar0833_work_mode == configure->aet.aet[index].info->fmt_capture){
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



void AR0833_ae_manual_set(char **param){
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
	
	AR0833_manual_set_aet(g_ae_manual_exp,g_ae_manual_ag,g_ae_manual_vts);
}           

static ssize_t ae_manual_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	char *param[3] = {NULL};
	parse_param(buf,&param[0]);
	AR0833_ae_manual_set(&param[0]);
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

static void power_down_ar0833(struct ar0833_device *dev)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//i2c_put_byte(client,0x0104, 0x00);
	//i2c_put_byte(client,0x0100, 0x00);
}


static ssize_t vcm_manual_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	//struct i2c_adapter *adapter;
	char buff[3];
	unsigned int af_step = 0;
	unsigned int diff = 0;
	int codes,vcm_data = 0;
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
    /*adapter = i2c_get_adapter(4);
    my_i2c_put_byte_add8(adapter,0x0c,buff,2);
    */
    return len;
}

static ssize_t vcm_manual_show(struct class *cls,struct class_attribute *attr, char* buf)
{
	size_t len = 0;
	//struct i2c_adapter *adapter;
	//unsigned int af;
	//adapter = i2c_get_adapter(4);
	//af = my_i2c_get_word(adapter,0x0c);
	//printk("current vcm step :%x\n",af);
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


void AR0833_init_regs(struct ar0833_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int i=0;
	
	while (1) {
		if (AR0833_init_script[i].type == END_OF_SCRIPT) {
			printk("success in initial AR0833.\n");
			break;
		}
		if ((cam_i2c_send_msg(client, AR0833_init_script[i])) < 0) {
			printk("fail in initial AR0833. \n");
			return;
		}
		i++;
	}
	return;
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
*    AR0833_set_param_wb
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

void AR0833_set_param_wb(struct ar0833_device *dev,enum  camera_wb_flip_e para)//white balance
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
            memcpy(dev->cam_para->xml_wb_manual->reg_map,dev->configure->wb.wb[index].export,WB_MAX * sizeof(int));
        }
        printk("set wb :%d\n",index);
        dev->fe_arg.port = TVIN_PORT_ISP;
        dev->fe_arg.index = 0;
        dev->fe_arg.arg = (void *)(dev->cam_para);
        dev->vops->tvin_fe_func(0,&dev->fe_arg);
    }else{
        return;	
    }


} /* AR0833_set_param_wb */
/*************************************************************************
 * FUNCTION
 *    AR0833_set_param_exposure
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
void AR0833_set_param_exposure(struct ar0833_device *dev,enum camera_exposure_e para)
{
    //struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    int value;
    if(para == EXPOSURE_0_STEP){
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

} /* ar0833_set_param_exposure */
/*************************************************************************
 * FUNCTION
 *    AR0833_set_param_effect
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

void AR0833_set_param_effect(struct ar0833_device *dev,enum camera_special_effect_e para)
{
    int index = 0;
    int i = 0;
    while(i < ARRAY_SIZE(effect_pair)){
        if(effect_pair[i].effect == para){
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

} /* AR0833_set_param_effect */

/*************************************************************************
 * FUNCTION
 *    AR0833_NightMode
 *
 * DESCRIPTION
 *    This function night mode of AR0833.
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
void AR0833_set_night_mode(struct ar0833_device *dev,enum  camera_night_mode_flip_e enable)
{
    //struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    if (enable) {
        
    }
    else{
       
    }

}   /* AR0833_NightMode */

static void AR0833_set_param_banding(struct ar0833_device *dev,enum  camera_banding_flip_e banding)
{
    //struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    switch(banding){
        case CAM_BANDING_60HZ:
            printk("set banding 60Hz\n");
            
            break;
        case CAM_BANDING_50HZ:
            printk("set banding 50Hz\n");
           
            break;
        default:
            break;
    }
}


static int AR0833_AutoFocus(struct ar0833_device *dev, int focus_mode)
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

static int set_flip(struct ar0833_device *dev)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    unsigned char temp;
    temp = i2c_get_byte(client, 0x0101);
    temp &= 0xfc;
    temp |= dev->cam_info.m_flip << 0;
    temp |= dev->cam_info.v_flip << 1;
    printk("dst temp is 0x%x\n", temp);
    if((i2c_put_byte(client, 0x0101, temp)) < 0) {
        printk("fail in setting sensor orientation \n");
        return -1;
    }
    return 0;
}


static resolution_size_t get_size_type(int width, int height)
{
    resolution_size_t rv = SIZE_NULL;
    if (width * height >= 3200 * 2400)
        rv = SIZE_3264X2448;
    else if (width * height >= 3200 * 1800)
        rv = SIZE_3264X1836;
    else if (width * height >= 2500 * 1900)
        rv = SIZE_2592X1944;
    else if (width * height >= 1900 * 1000)
        rv = SIZE_1920X1080;
    else if (width * height >= 1200 * 900)
        rv = SIZE_1280X960;
    else if (width * height >= 1200 * 700)
        rv = SIZE_1280X720;
    else if (width * height >= 600 * 400)
        rv = SIZE_640X480;
    else if (width * height >= 352 * 288)
        rv = SIZE_352X288;
    else if (width * height >= 320 * 240)
        rv = SIZE_320X240;
    else if (width * height >= 176 * 144)
        rv = SIZE_176X144;
    return rv;
}

static int AR0833_FlashCtrl(struct ar0833_device *dev, int flash_mode)
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

static resolution_param_t* get_resolution_param(struct ar0833_device *dev, int is_capture, int width, int height)
{
    int i = 0;
    int arry_size = 0;
    resolution_param_t* tmp_resolution_param = NULL;
    resolution_size_t res_type = SIZE_NULL;
    printk("target resolution is %dX%d\n", width, height);
    res_type = get_size_type(width, height);
    if (res_type == SIZE_NULL)
        return NULL;
    if (ar0833_work_mode == CAMERA_CAPTURE) {
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

static char *res_size[]={
	"144p",
	"240p",
	"288p",
	"480p",
	"720p",
	"960p",
	"1080p",
	"6m",
	"8m"
};

static void set_resolution_param(struct ar0833_device *dev, resolution_param_t* res_param)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    //int rc = -1;
    int i=0;
    unsigned char t = dev->cam_info.interface;
    printk("%s, %d, interface =%d\n" , __func__, __LINE__, t);

	if(i_index != -1 && ar0833_work_mode != CAMERA_CAPTURE){
        res_param = &debug_prev_resolution_array[i_index];
    }
    if (!res_param->reg_script[t]) {
        printk("error, resolution reg script is NULL\n");
        return;
    }
    while(1){
        if (res_param->reg_script[t][i].type == END_OF_SCRIPT) {
            printk("setting resolutin param complete\n");
            break;
        }
        if((cam_i2c_send_msg(client, res_param->reg_script[t][i])) < 0) {
            printk("fail in setting resolution param. i=%d\n",i);
            break;
        }
        if(res_param->reg_script[t][i].addr == 0x0103) //soft reset,need 5ms delay
        	msleep(5);
        i++;
    }
    set_flip(dev);
    
    ar0833_frmintervals_active.numerator = 1;
    ar0833_frmintervals_active.denominator = res_param->active_fps;
    ar0833_h_active = res_param->frmsize.width;
    ar0833_v_active = res_param->frmsize.height;
    AR0833_set_new_format((void *)&dev->camera_priv_data,ar0833_h_active,ar0833_v_active,current_fr);// should set new para
}    /* AR0833_set_resolution */

static int set_focus_zone(struct ar0833_device *dev, int value)
{
	int xc, yc, tx, ty;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//int retry_count = 10;
	//int ret = -1;
	
	xc = (value >> 16) & 0xffff;
	yc = (value & 0xffff);
	if(xc == 1000 && yc == 1000)
		return 0;
	tx = xc * ar0833_h_active /2000;
	ty = yc * ar0833_v_active /2000;
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

unsigned char v4l_2_ar0833(int val)
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

static int ar0833_setting(struct ar0833_device *dev,int PROP_ID,int value )
{
	int ret=0;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_ar0833(value));
		//ret=i2c_put_byte(client,0x0201,v4l_2_ar0833(value));
		break;
	case V4L2_CID_CONTRAST:
		break;
	case V4L2_CID_SATURATION:
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */
		value = value & 0x1;
		if(ar0833_qctrl[2].default_value!=value){
			 ar0833_qctrl[2].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
			value = value << 1; //bit[1]
			//ret=i2c_put_byte(client,0x3821, value);
			break;
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		if(ar0833_qctrl[4].default_value!=value){
			ar0833_qctrl[4].default_value=value;
			if(dev->stream_on)
			AR0833_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
		}
		break;
	case V4L2_CID_EXPOSURE:
		if(ar0833_qctrl[5].default_value!=value){
			ar0833_qctrl[5].default_value=value;
			if(dev->stream_on)
				AR0833_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
		}
		break;
	case V4L2_CID_BACKLIGHT_COMPENSATION:
        if(ar0833_qctrl[9].default_value!=value){
			ar0833_qctrl[9].default_value=value;
            ret = AR0833_FlashCtrl(dev,value);
			printk(KERN_INFO " set light compensation =%d. \n ",value);
		}
		break;
	case V4L2_CID_COLORFX:
		if(ar0833_qctrl[6].default_value!=value){
			ar0833_qctrl[6].default_value=value;
			if(dev->stream_on)
				AR0833_set_param_effect(dev,value);
		}
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		if(ar0833_qctrl[3].default_value!=value){
			ar0833_qctrl[3].default_value=value;
			AR0833_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(ar0833_qctrl[10].default_value!=value){
			ar0833_qctrl[10].default_value=value;
			printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(ar0833_qctrl[11].default_value!=value){
			ar0833_qctrl[11].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		printk("V4L2_CID_FOCUS_ABSOLUTE\n");
		if(ar0833_qctrl[13].default_value!=value){
			ar0833_qctrl[13].default_value=value;
			printk(" set camera  focus zone =%d. \n ",value);
			if(dev->stream_on) {
				set_focus_zone(dev, value);
			}
		}
		break;
	case V4L2_CID_FOCUS_AUTO:
		printk("V4L2_CID_FOCUS_AUTO\n");
		if(ar0833_qctrl[8].default_value!=value){
			ar0833_qctrl[8].default_value=value;
			if(dev->stream_on)
				AR0833_AutoFocus(dev,value);
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

#define TSTAMP_MIN_Y	24
#define TSTAMP_MAX_Y	(TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X	10
#define TSTAMP_MIN_X	(54 + TSTAMP_INPUT_X)

static void ar0833_fillbuff(struct ar0833_fh *fh, struct ar0833_buffer *buf)
{
	struct ar0833_device *dev = fh->dev;
	//void *vbuf = videobuf_to_vmalloc(&buf->vb);
	void *vbuf = (void *)videobuf_to_res(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
	/*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	if(buf->canvas_id == 0)
		buf->canvas_id = convert_canvas_index(fh->fmt->fourcc, AR0833_RES0_CANVAS_INDEX+buf->vb.i*3);
	para.mirror = ar0833_qctrl[2].default_value&3;// not set
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = MAGIC_RE_MEM;//0x18221223;
	para.zoom = ar0833_qctrl[10].default_value;
	para.angle = ar0833_qctrl[11].default_value;
	para.vaddr = (unsigned)vbuf;
	para.ext_canvas = buf->canvas_id;
	para.width = buf->vb.width;
	para.height = (buf->vb.height==1080)?1088:buf->vb.height;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void ar0833_thread_tick(struct ar0833_fh *fh)
{
	struct ar0833_buffer *buf;
	struct ar0833_device *dev = fh->dev;
	struct ar0833_dmaqueue *dma_q = &dev->vidq;

	unsigned long flags = 0;

	dprintk(dev, 1, "Thread tick\n");
	if(!dev->stream_on){
		dprintk(dev, 1, "sensor doesn't stream on\n");
		return ;
	}

	spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&dma_q->active)) {
		dprintk(dev, 1, "No active queue to serve\n");
		goto unlock;
	}

    buf = list_entry(dma_q->active.next,
            struct ar0833_buffer, vb.queue);
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
	ar0833_fillbuff(fh, buf);
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

static void ar0833_sleep(struct ar0833_fh *fh)
{
	struct ar0833_device *dev = fh->dev;
	struct ar0833_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	ar0833_thread_tick(fh);

	schedule_timeout_interruptible(1);//if fps > 25 , 2->1

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int ar0833_thread(void *data)
{
	struct ar0833_fh  *fh = data;
	struct ar0833_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		ar0833_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int ar0833_start_thread(struct ar0833_fh *fh)
{
	struct ar0833_device *dev = fh->dev;
	struct ar0833_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(ar0833_thread, fh, "ar0833");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void ar0833_stop_thread(struct ar0833_dmaqueue  *dma_q)
{
	struct ar0833_device *dev = container_of(dma_q, struct ar0833_device, vidq);

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
	struct ar0833_fh *fh  = container_of(res, struct ar0833_fh, res);
	struct ar0833_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct ar0833_buffer *buf)
{
	struct videobuf_res_privdata *res = vq->priv_data;
	struct ar0833_fh *fh  = container_of(res, struct ar0833_fh, res);
	struct ar0833_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

	videobuf_waiton(vq, &buf->vb, 0, 0);
	if (in_interrupt())
		BUG();

	videobuf_res_free(vq, &buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 4000
#define norm_maxh() 3000
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct videobuf_res_privdata *res = vq->priv_data;
	struct ar0833_fh *fh  = container_of(res, struct ar0833_fh, res);
	struct ar0833_device    *dev = fh->dev;
	struct ar0833_buffer *buf = container_of(vb, struct ar0833_buffer, vb);
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
	struct ar0833_buffer    *buf  = container_of(vb, struct ar0833_buffer, vb);
	struct videobuf_res_privdata *res = vq->priv_data;
	struct ar0833_fh *fh  = container_of(res, struct ar0833_fh, res);
	struct ar0833_device       *dev  = fh->dev;
	struct ar0833_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct ar0833_buffer   *buf  = container_of(vb, struct ar0833_buffer, vb);
	struct videobuf_res_privdata *res = vq->priv_data;
	struct ar0833_fh *fh  = container_of(res, struct ar0833_fh, res);
	struct ar0833_device      *dev  = (struct ar0833_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops ar0833_video_qops = {
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
	struct ar0833_fh  *fh  = priv;
	struct ar0833_device *dev = fh->dev;

	strcpy(cap->driver, "ar0833");
	strcpy(cap->card, "ar0833.canvas");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = AR0833_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct ar0833_fmt *fmt;

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

    if(fival->index > ARRAY_SIZE(ar0833_frmivalenum))
        return -EINVAL;

    for(k =0; k< ARRAY_SIZE(ar0833_frmivalenum); k++)
    {
        if( (fival->index==ar0833_frmivalenum[k].index)&&
                (fival->pixel_format ==ar0833_frmivalenum[k].pixel_format )&&
                (fival->width==ar0833_frmivalenum[k].width)&&
                (fival->height==ar0833_frmivalenum[k].height)){
            memcpy( fival, &ar0833_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
            return 0;
        }
    }
    return -EINVAL;

}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct ar0833_fh *fh = priv;

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
	struct ar0833_fh  *fh  = priv;
	struct ar0833_device *dev = fh->dev;
	struct ar0833_fmt *fmt;
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
	struct ar0833_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct ar0833_device *dev = fh->dev;
	resolution_param_t* res_param = NULL;
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
    if(f->fmt.pix.pixelformat==V4L2_PIX_FMT_RGB24){
        ar0833_work_mode = CAMERA_CAPTURE;
        res_param = get_resolution_param(dev, 1, fh->width,fh->height);
        if (!res_param) {
            printk("error, resolution param not get\n");
            goto out;
        }
        set_resolution_param(dev, res_param);
    }
    else {
    	printk("preview resolution is %dX%d\n",fh->width,  fh->height);
        if((fh->width == 1280 && fh->height == 720) || 
        	(fh->width == 1920 && fh->height == 1080)){
        	ar0833_work_mode = CAMERA_RECORD;
        }else
        	ar0833_work_mode = CAMERA_PREVIEW;
        res_param = get_resolution_param(dev, 0, fh->width,fh->height);
        if (!res_param) {
            printk("error, resolution param not get\n");
            goto out;
        }
        set_resolution_param(dev, res_param);
        /** set target ***/
        if(t_index == -1){
            dest_hactive = 0;
            dest_vactive = 0;
        }
    }
	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return ret;
}

static int vidioc_g_parm(struct file *file, void *priv,
        struct v4l2_streamparm *parms)
{
    struct ar0833_fh *fh = priv;
    struct ar0833_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = ar0833_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct ar0833_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ar0833_fh  *fh = priv;

	int ret = videobuf_querybuf(&fh->vb_vidq, p);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	if(ret == 0){
		p->reserved  = convert_canvas_index(fh->fmt->fourcc, AR0833_RES0_CANVAS_INDEX+p->index*3);
	}else{
		p->reserved = 0;
	}
#endif		
	return ret;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ar0833_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ar0833_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct ar0833_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif
#ifdef MIPI_INTERFACE
/*static struct ar0833_fmt input_formats_vdin[] = 
{
    // vdin path format
    {
        .name     = "4:2:2, packed, UYVY",
        .fourcc   = V4L2_PIX_FMT_UYVY,
        .depth    = 16,
    },
    {
        .name     = "12  Y/CbCr 4:2:0",
        .fourcc   = V4L2_PIX_FMT_NV21,
        .depth    = 12,
    },
    {
        .name     = "12  Y/CbCr 4:2:0",
        .fourcc   = V4L2_PIX_FMT_NV12,
        .depth    = 12,
    }
};*/
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct ar0833_fh  *fh = priv;
	struct ar0833_device *dev = fh->dev;	
	vdin_parm_t para;
	int ret = 0 ;
	
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	memset( &para, 0, sizeof( para ));
	//para.port  = TVIN_PORT_CAMERA;
	
	if (CAM_MIPI == dev->cam_info.interface) {
	        para.isp_fe_port  = TVIN_PORT_MIPI;
	} else {
	        para.isp_fe_port  = TVIN_PORT_CAMERA;
	}
	para.port  = TVIN_PORT_ISP;    
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = ar0833_frmintervals_active.denominator;
	para.h_active = ar0833_h_active;
	para.v_active = ar0833_v_active;
    if(ar0833_work_mode != CAMERA_CAPTURE){
		para.skip_count = 2;
        para.dest_hactive = dest_hactive;
        para.dest_vactive = dest_vactive;
    }else{
        para.dest_hactive = 0;
        para.dest_vactive = 0;
    }
    dev->cam_para->cam_mode = ar0833_work_mode;
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
   	if(update_fmt_para(ar0833_h_active,ar0833_v_active,dev->cam_para,&dev->pindex,dev->configure) != 0)
   		return -EINVAL;
	    para.reserved = (int)(dev->cam_para);
	
	if (CAM_MIPI == dev->cam_info.interface)
	{
	        para.csi_hw_info.lanes = 2;
	        para.csi_hw_info.channel = 1;
	        para.csi_hw_info.mode = 1;
	        para.csi_hw_info.clock_lane_mode = 1; // 0 clock gate 1: always on
	        para.csi_hw_info.active_pixel = ar0833_h_active;
	        para.csi_hw_info.active_line = ar0833_v_active;
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

	printk("ar0833,h=%d, v=%d, frame_rate=%d\n", 
	        ar0833_h_active, ar0833_v_active, ar0833_frmintervals_active.denominator);
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
		dev->vops->start_tvin_service(0,&para);
		dev->stream_on        = 1;
	}
    /*** 		set cm2 		***/
	dev->vdin_arg.cmd = VDIN_CMD_SET_CM2;
	dev->vdin_arg.cm2 = dev->configure->cm.export;
	dev->vops->tvin_vdin_func(0,&dev->vdin_arg);
	AR0833_set_param_wb(fh->dev,ar0833_qctrl[4].default_value);
	AR0833_set_param_exposure(fh->dev,ar0833_qctrl[5].default_value);
	AR0833_set_param_effect(fh->dev,ar0833_qctrl[6].default_value);

	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct ar0833_fh  *fh = priv;
	struct ar0833_device *dev = fh->dev;
	int ret = 0 ;
	printk(KERN_INFO " vidioc_streamoff+++ \n ");
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;
	ret = videobuf_streamoff(&fh->vb_vidq);
	if(ret == 0 ){
        dev->vops->stop_tvin_service(0);
        dev->stream_on        = 0;
	}
	return ret;
}

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
	printk("current hactive :%d, v_active :%d, dest_hactive :%d, dest_vactive:%d\n",ar0833_h_active,ar0833_v_active,dest_hactive,dest_vactive);
	return len;
}
static CLASS_ATTR(resolution_debug, 0664, manual_format_show,manual_format_store);

static int vidioc_enum_framesizes(struct file *file, void *fh,struct v4l2_frmsizeenum *fsize)
{

      int ret = 0,i=0;
      struct ar0833_fmt *fmt = NULL;
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
              printk("ar0833_prev_resolution[fsize->index]"
                              "   before fsize->index== %d\n",fsize->index);//potti
              if (fsize->index >= ARRAY_SIZE(prev_resolution_array))
                      return -EINVAL;
              frmsize = &prev_resolution_array[fsize->index].frmsize;
              printk("ar0833_prev_resolution[fsize->index]"
                              "   after fsize->index== %d\n",fsize->index);
              fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
              fsize->discrete.width = frmsize->width;
              fsize->discrete.height = frmsize->height;
      } else if (fmt->fourcc == V4L2_PIX_FMT_RGB24){
              printk("ar0833_pic_resolution[fsize->index]"
                              "   before fsize->index== %d\n",fsize->index);
              if (fsize->index >= ARRAY_SIZE(capture_resolution_array))
                      return -EINVAL;
              frmsize = &capture_resolution_array[fsize->index].frmsize;
              printk("ar0833_pic_resolution[fsize->index]"
                              "   after fsize->index== %d\n",fsize->index);
              fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
              fsize->discrete.width = frmsize->width;
              fsize->discrete.height = frmsize->height;
      }
      return ret;
}

static int vidioc_s_std(struct file *file, void *fh, v4l2_std_id norm)
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
	struct ar0833_fh *fh = priv;
	struct ar0833_device *dev = fh->dev;

	*i = dev->input;
	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct ar0833_fh *fh = priv;
	struct ar0833_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(ar0833_qctrl); i++)
		if (qc->id && qc->id == ar0833_qctrl[i].id) {
			memcpy(qc, &(ar0833_qctrl[i]),sizeof(*qc));
			return (0);
		}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct ar0833_fh *fh = priv;
	struct ar0833_device *dev = fh->dev;
	int i;
	//int status;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(ar0833_qctrl); i++)
		if (ctrl->id == ar0833_qctrl[i].id) {
		#if 0
            if( (V4L2_CID_FOCUS_AUTO == ctrl->id)
                    && bDoingAutoFocusMode){
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
			#endif
            ctrl->value = dev->qctl_regs[i];
            return ret;
        }

	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct ar0833_fh *fh = priv;
	struct ar0833_device *dev = fh->dev;
	int i;
	for (i = 0; i < ARRAY_SIZE(ar0833_qctrl); i++)
		if (ctrl->id == ar0833_qctrl[i].id) {
			if (ctrl->value < ar0833_qctrl[i].minimum ||
			    ctrl->value > ar0833_qctrl[i].maximum ||
			    ar0833_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int ar0833_open(struct file *file)
{
    struct ar0833_device *dev = video_drvdata(file);
    struct ar0833_fh *fh = NULL;
    resource_size_t mem_start = 0;
    unsigned int mem_size = 0;
    int retval = 0;
#if CONFIG_CMA
    retval = vm_init_buf(24*SZ_1M);
    if(retval <0) {
    	printk("error: no cma memory\n");
        return -1;
    }
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_name("ge2d", 1);
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
    dev->cam_para->cam_function.set_aet_new_step = AR0833_set_aet_new_step;
    dev->cam_para->cam_function.check_mains_freq = AR0833_check_mains_freq;
    dev->cam_para->cam_function.set_af_new_step = AR0833_set_af_new_step;
    dev->camera_priv_data.configure = dev->configure;
    dev->cam_para->cam_function.priv_data = (void *)&dev->camera_priv_data;  
    dev->ae_on = false;
    AR0833_init_regs(dev);
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
    dev->stream_on = 0 ;
    fh->f_flags  = file->f_flags;
    /* Resets frame counters */
    dev->jiffies = jiffies;

    get_vm_buf_info(&mem_start, &mem_size, NULL);
    fh->res.start = mem_start;
    fh->res.end = mem_start+mem_size-1;
    fh->res.magic = MAGIC_RE_MEM;
    fh->res.priv = NULL;
    videobuf_queue_res_init(&fh->vb_vidq, &ar0833_video_qops,
    			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
    			sizeof(struct ar0833_buffer), (void*)&fh->res, NULL);
    ar0833_start_thread(fh);
    ar0833_have_open = 1;
    ar0833_work_mode = CAMERA_PREVIEW;
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
    retval = class_create_file(cam_class,&class_attr_vcm_debug);
    retval = class_create_file(cam_class,&class_attr_resolution_debug);
    retval = class_create_file(cam_class,&class_attr_light_source_debug);
    retval = class_create_file(cam_class,&class_attr_version_debug);

    dev->vops = get_vdin_v4l2_ops();
	bDoingAutoFocusMode=false;
    cf = dev->configure;
    printk("open successfully\n");
    return 0;
}

static ssize_t
ar0833_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct ar0833_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
ar0833_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ar0833_fh        *fh = file->private_data;
	struct ar0833_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int ar0833_close(struct file *file)
{
    struct ar0833_fh         *fh = file->private_data;
    struct ar0833_device *dev       = fh->dev;
    struct ar0833_dmaqueue *vidq = &dev->vidq;
    struct video_device  *vdev = video_devdata(file);
    int i=0;
    ar0833_have_open = 0;
    ar0833_stop_thread(vidq);
    videobuf_stop(&fh->vb_vidq);
    if(dev->stream_on){
        dev->vops->stop_tvin_service(0);
    }
    videobuf_mmap_free(&fh->vb_vidq);

    kfree(fh);
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
    if(dev->cam_para != NULL ){
        free_para(dev->cam_para);
        kfree(dev->cam_para);
    }

    mutex_lock(&dev->mutex);
    dev->users--;
    mutex_unlock(&dev->mutex);

    dprintk(dev, 1, "close called (dev=%s, users=%d)\n",
            video_device_node_name(vdev), dev->users);
    //ar0833_h_active=800;
    //ar0833_v_active=600;
    ar0833_qctrl[0].default_value=0;
    ar0833_qctrl[1].default_value=4;
    ar0833_qctrl[2].default_value=0;
    ar0833_qctrl[3].default_value=CAM_BANDING_50HZ;
    ar0833_qctrl[4].default_value=CAM_WB_AUTO;

    ar0833_qctrl[5].default_value=4;
    ar0833_qctrl[6].default_value=0;
    ar0833_qctrl[10].default_value=100;
    ar0833_qctrl[11].default_value=0;
    dev->ae_on = false;
    //ar0833_frmintervals_active.numerator = 1;
    //ar0833_frmintervals_active.denominator = 15;
    power_down_ar0833(dev);
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
    class_remove_file(cam_class,&class_attr_vcm_debug);
    class_remove_file(cam_class,&class_attr_resolution_debug);
	class_remove_file(cam_class,&class_attr_light_source_debug);
    class_remove_file(cam_class,&class_attr_version_debug);
    class_destroy(cam_class);
    printk("close success\n");
#ifdef CONFIG_CMA
    vm_deinit_buf();
#endif
    return 0;
}

static int ar0833_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ar0833_fh  *fh = file->private_data;
	struct ar0833_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations ar0833_fops = {
	.owner		= THIS_MODULE,
	.open           = ar0833_open,
	.release        = ar0833_close,
	.read           = ar0833_read,
	.poll		= ar0833_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = ar0833_mmap,
};

static const struct v4l2_ioctl_ops ar0833_ioctl_ops = {
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

static struct video_device ar0833_template = {
	.name		= "ar0833_v4l",
	.fops           = &ar0833_fops,
	.ioctl_ops 	= &ar0833_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int ar0833_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_AR0833, 0);
}

static const struct v4l2_subdev_core_ops ar0833_core_ops = {
	.g_chip_ident = ar0833_g_chip_ident,
};

static const struct v4l2_subdev_ops ar0833_ops = {
	.core = &ar0833_core_ops,
};

static ssize_t cam_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct ar0833_device *t;
	
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

	struct ar0833_device *t;
	unsigned char n=0;
	//unsigned char ret=0;
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
	    printk("clk channel =%s\n", t->cam_info.interface?"clkB":"clkA");
	}
	
	kfree(buf_orig);
	
	return len;

}

static DEVICE_ATTR(cam_info, 0664, cam_info_show, cam_info_store);
static int ar0833_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	struct ar0833_device *t;
	struct v4l2_subdev *sd;
	aml_cam_info_t* plat_dat;
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &ar0833_ops);

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
	memcpy(t->vdev, &ar0833_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);
	
	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "ar0833");
	/* Register it */
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if(plat_dat->front_back>=0)  
			video_nr=plat_dat->front_back;
	}else {
		printk("camera ar0833: have no platform data\n");
		kfree(t);
		kfree(client);
		return -1;
	}
	
	t->cam_info.version = AR0833_DRIVER_VERSION;
	if (aml_cam_info_reg(&t->cam_info) < 0)
		printk("reg caminfo error\n");
	
	printk("register device\n");	
	err = video_register_device(t->vdev, VFL_TYPE_GRABBER, video_nr);
	if (err < 0) {
		video_device_release(t->vdev);
		kfree(t);
		return err;
	}
	device_create_file( &t->vdev->dev, &dev_attr_cam_info);
	return 0;
}

static int ar0833_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0833_device *t = to_dev(sd);
	device_remove_file( &t->vdev->dev, &dev_attr_cam_info);
	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id ar0833_id[] = {
	{ "ar0833", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ar0833_id);

static struct i2c_driver ar0833_i2c_driver = {
	.driver = {
		.name = "ar0833",
	},
	.probe = ar0833_probe,
	.remove = ar0833_remove,
	.id_table = ar0833_id,
};

module_i2c_driver(ar0833_i2c_driver);
