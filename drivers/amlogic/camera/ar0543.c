/*
 *ar0543 - This code emulates a real video device with v4l2 api
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

#define AR0543_CAMERA_MODULE_NAME "ar0543"
#define MAGIC_RE_MEM 0x123039dc
#define AR0543_RES0_CANVAS_INDEX CAMERA_USER_CANVAS_INDEX

static int capture_proc = 0;
/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define AR0543_CAMERA_MAJOR_VERSION 0
#define AR0543_CAMERA_MINOR_VERSION 7
#define AR0543_CAMERA_RELEASE 0
#define AR0543_CAMERA_VERSION \
	KERNEL_VERSION(AR0543_CAMERA_MAJOR_VERSION, AR0543_CAMERA_MINOR_VERSION, AR0543_CAMERA_RELEASE)

#define AR0543_DRIVER_VERSION "AR0543-COMMON-01-140717"

MODULE_DESCRIPTION("ar0543 On Board");
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

static int ar0543_h_active = 800;
static int ar0543_v_active = 600;
static struct v4l2_fract ar0543_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

static int ar0543_have_open=0;


static camera_mode_t ar0543_work_mode = CAMERA_PREVIEW;
static struct class *cam_class;
static unsigned int g_ae_manual_exp;
static unsigned int g_ae_manual_ag;
static unsigned int g_ae_manual_vts;
static unsigned int current_fmt;
static unsigned int current_fr = 0;//50 hz
static unsigned int aet_index;
static unsigned int last_af_step = 0;

static int i_index = -1;
static int t_index = -1;
static int dest_hactive = 640;
static int dest_vactive = 480;
static bool bDoingAutoFocusMode = false;
static configure_t *cf;
/* supported controls */
static struct v4l2_queryctrl ar0543_qctrl[] = {
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
		.flags         = V4L2_CTRL_FLAG_DISABLED,
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
        .id			   = V4L2_CID_FOCUS_ABSOLUTE,
        .type		   = V4L2_CTRL_TYPE_INTEGER,
		.name		   = "focus center",
		.minimum	   = 0,
		.maximum	   = ((2000) << 16) | 2000,
		.step		   = 1,
		.default_value = (1000 << 16) | 1000,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
 	}
};

static struct v4l2_frmivalenum ar0543_frmivalenum[]={
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

struct v4l2_querymenu ar0543_qmenu_wbmode[] = {
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

struct v4l2_querymenu ar0543_qmenu_autofocus[] = {
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

struct v4l2_querymenu ar0543_qmenu_anti_banding_mode[] = {
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

struct v4l2_querymenu ar0543_qmenu_flashmode[] = {
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
    struct v4l2_querymenu* ar0543_qmenu;
}ar0543_qmenu_set_t;

ar0543_qmenu_set_t ar0543_qmenu_set[] = {
    {
        .id         	= V4L2_CID_DO_WHITE_BALANCE,
        .num            = ARRAY_SIZE(ar0543_qmenu_wbmode),
        .ar0543_qmenu   = ar0543_qmenu_wbmode,
    },{
        .id         	= V4L2_CID_POWER_LINE_FREQUENCY,
        .num            = ARRAY_SIZE(ar0543_qmenu_anti_banding_mode),
        .ar0543_qmenu   = ar0543_qmenu_anti_banding_mode,
    }, {
        .id             = V4L2_CID_FOCUS_AUTO,
        .num            = ARRAY_SIZE(ar0543_qmenu_autofocus),
        .ar0543_qmenu   = ar0543_qmenu_autofocus,
    }, {
        .id             = V4L2_CID_BACKLIGHT_COMPENSATION,
        .num            = ARRAY_SIZE(ar0543_qmenu_flashmode),
        .ar0543_qmenu   = ar0543_qmenu_flashmode,
    }
};

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(ar0543_qmenu_set); i++)
	if (a->id && a->id == ar0543_qmenu_set[i].id) {
	    for(j = 0; j < ar0543_qmenu_set[i].num; j++)
		if (a->index == ar0543_qmenu_set[i].ar0543_qmenu[j].index) {
			memcpy(a, &( ar0543_qmenu_set[i].ar0543_qmenu[j]),
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

struct ar0543_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct ar0543_fmt formats[] = {
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

static struct ar0543_fmt *get_format(struct v4l2_format *f)
{
	struct ar0543_fmt *fmt;
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
struct ar0543_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct ar0543_fmt        *fmt;
	unsigned int canvas_id;
};

struct ar0543_dmaqueue {
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

static LIST_HEAD(ar0543_devicelist);

struct ar0543_device {
	struct list_head			ar0543_devicelist;
	struct v4l2_subdev			sd;
	struct v4l2_device			v4l2_dev;

	spinlock_t                 slock;
	struct mutex				mutex;

	int                        users;

	/* various device info */
	struct video_device        *vdev;

	struct ar0543_dmaqueue       vidq;

	/* Several counters */
	unsigned long              jiffies;

	/* Input Number */
	int			   input;

	/* platform device data from board initting. */
	aml_cam_info_t cam_info;
	
	cam_parameter_t *cam_para;
	
	para_index_t pindex;
	
	struct vdin_v4l2_ops_s *vops;
	
	unsigned int            is_vdin_start;
	
	int stream_on;

	fe_arg_t fe_arg;
	
	vdin_arg_t vdin_arg;
	/* wake lock */
	struct wake_lock	wake_lock;
	/* ae status */
	bool ae_on;
	
	camera_priv_data_t camera_priv_data;
	
	configure_t *configure;
	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(ar0543_qctrl)];
};

static inline struct ar0543_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ar0543_device, sd);
}

struct ar0543_fh {
	struct ar0543_device            *dev;

	/* video capture */
	struct ar0543_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	struct videobuf_res_privdata res;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
	unsigned int		f_flags;
};


#define RAW10

/* ------------------------------------------------------------------
	reg spec of AR0543
   ------------------------------------------------------------------*/
static cam_i2c_msg_t AR0543_init_script[] = {

	{END_OF_SCRIPT, 0, 0},
};
#ifdef MIPI_INTERFACE
/*static cam_i2c_msg_t AR0543_mipi_script[] = {
	{END_OF_SCRIPT, 0, 0},
};*/
#endif
static cam_i2c_msg_t AR0543_preview_VGA_script[] = {
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0543_VGA_script_mipi[] = {
        // This file was generated by: AR0542 (A-5141) Register Wizard
        //   Version: 4.5.13.36518    Build Date: 07/08/2013
        //
        // [PLL PARAMETERS]
        //
        // Bypass PLL: Unchecked
        // Input Frequency: 24.000
        // Use Min Freq.: Unchecked
        // Target VT Frequency: 96.000
        // Target op_sys_clk Frequency: Unspecified
        //
        // Target PLL VT Frequency: 96 MHz
        // Target PLL OP Frequency: 96 MHz
        // MT9P017 Input Clock Frequency: 24 MHz
        // MT9P017 VT (Internal) Pixel Clock Frequency: 96 MHz
        // MT9P017 OP (Output) Pixel Clock Frequency: 48 MHz
        // pre_pll_clk_div = 2
        // pll_multiplier = 40
        // vt_sys_clk_div = 1
        // vt_pix_clk_div = 5
        // op_sys_clk_div = 1
        // op_pix_clk_div = 10
        // ip_clk = 12 MHz
        // op_clk = 480 MHz
        // op_sys_clk = 480 MHz
        //
        // [SENSOR PARAMETERS]
        //
        // Requested Frames Per Second: 30.000
        // Output Columns: 640
        // Output Rows: 480
        // Use Binning: Checked
        // Allow Skipping: Unchecked
        // Blanking Computation: HB Max then VB
        //
        // Max Frame Time: 33.3333 msec
        // Max Frame Clocks: 3200000.0 clocks (96 MHz)
        // Maximun Frame Rate: 199.602 fps
        // Pixel Clock: divided by 1
        // Skip Mode: 4x cols, 4x rows, Bin Mode: Yes
        // Horiz clks:  640 active + 5146 blank = 5786 total
        // Vert  rows:  480 active + 73 blank = 553 total
        //
        // Actual Frame Clocks: 3200000 clocks
        // Row Time: 60.271 usec / 5786 clocks
        // Frame time: 33.333333 msec
        // Frames per Sec: 30 fps
        //
        //
        //



        //[AR0542 (A-5141) Register Wizard Defaults]
        {1,0x0103, 0x01},    //SOFTWARE_RESET (clears itself)
        {TIME_DELAY, 0, 5},      //Initialization Time

        //stop_streaming
        {1,0x0100, 0x00},    // MODE_SELECT
        {2,0x301A, 0x0218},    //RESET_REGISTER enable mipi interface  bit[9] mask bad frame
        {2,0x3064, 0xB800},    // SMIA_TEST
        {2,0x31AE, 0x0202},    // two lane
        {2,0x0112, 0x0A0A},    // 10bit raw output

#if 1
        //1.)    Add the ?¡ãRecommended Settings?¡À
        //[=========== Recommended Settings ==============]
        //[REV1_recommended_settings]
        {2, 0x316A, 0x8400}, // RESERVED
        {2, 0x316C, 0x8400}, // RESERVED
        {2, 0x316E, 0x8400}, // RESERVED
        {2, 0x3EFA, 0x1A1F}, // RESERVED
        {2, 0x3ED2, 0xD965}, // RESERVED
        {2, 0x3ED8, 0x7F1B}, // RESERVED
        {2, 0x3EDA, 0x2F11}, // RESERVED
        {2, 0x3EE2, 0x0060}, // RESERVED
        {2, 0x3EF2, 0xD965}, // RESERVED
        {2, 0x3EF8, 0x797F}, // RESERVED
        {2, 0x3EFC, 0x286F}, // Customer_Request 35298
        {2, 0x3EFE, 0x2C01},
        //[REV1_pixel_timing]
        // @00 Jump Table  // Customer_Request 35298 - Start
        {2, 0x3E00, 0x042F},
        {2, 0x3E02, 0xFFFF},
        {2, 0x3E04, 0xFFFF},
        {2, 0x3E06, 0xFFFF},
        // @04 Read
        {2, 0x3E08, 0x8071},
        {2, 0x3E0A, 0x7281},
        {2, 0x3E0C, 0x4011},
        {2, 0x3E0E, 0x8010},
        {2, 0x3E10, 0x60A5},
        {2, 0x3E12, 0x4080},
        {2, 0x3E14, 0x4180},
        {2, 0x3E16, 0x0018},
        {2, 0x3E18, 0x46B7},
        {2, 0x3E1A, 0x4994},
        {2, 0x3E1C, 0x4997},
        {2, 0x3E1E, 0x4682},
        {2, 0x3E20, 0x0018},
        {2, 0x3E22, 0x4241},
        {2, 0x3E24, 0x8000},
        {2, 0x3E26, 0x1880},
        {2, 0x3E28, 0x4785},
        {2, 0x3E2A, 0x4992},
        {2, 0x3E2C, 0x4997},
        {2, 0x3E2E, 0x4780},
        {2, 0x3E30, 0x4D80},
        {2, 0x3E32, 0x100C},
        {2, 0x3E34, 0x8000},
        {2, 0x3E36, 0x184A},
        {2, 0x3E38, 0x8042},
        {2, 0x3E3A, 0x001A},
        {2, 0x3E3C, 0x9610},
        {2, 0x3E3E, 0x0C80},
        {2, 0x3E40, 0x4DC6},
        {2, 0x3E42, 0x4A80},
        {2, 0x3E44, 0x0018},
        {2, 0x3E46, 0x8042},
        {2, 0x3E48, 0x8041},
        {2, 0x3E4A, 0x0018},
        {2, 0x3E4C, 0x804B},
        {2, 0x3E4E, 0xB74B},
        {2, 0x3E50, 0x8010},
        {2, 0x3E52, 0x6056},
        {2, 0x3E54, 0x001C},
        {2, 0x3E56, 0x8211},
        {2, 0x3E58, 0x8056},
        {2, 0x3E5A, 0x827C},
        {2, 0x3E5C, 0x0970},
        {2, 0x3E5E, 0x8082},
        {2, 0x3E60, 0x7281},
        {2, 0x3E62, 0x4C40},
        {2, 0x3E64, 0x8E4D},
        {2, 0x3E66, 0x8110},
        {2, 0x3E68, 0x0CAF},
        {2, 0x3E6A, 0x4D80},
        {2, 0x3E6C, 0x100C},
        {2, 0x3E6E, 0x8440},
        {2, 0x3E70, 0x4C81},
        {2, 0x3E72, 0x7C5F},
        {2, 0x3E74, 0x7000},
        {2, 0x3E76, 0x0000},
        {2, 0x3E78, 0x0000},
        {2, 0x3E7A, 0x0000},
        {2, 0x3E7C, 0x0000},
        {2, 0x3E7E, 0x0000},
        {2, 0x3E80, 0x0000},
        {2, 0x3E82, 0x0000},
        {2, 0x3E84, 0x0000},
        {2, 0x3E86, 0x0000},
        {2, 0x3E88, 0x0000},
        {2, 0x3E8A, 0x0000},
        {2, 0x3E8C, 0x0000},
        {2, 0x3E8E, 0x0000},
        {2, 0x3E90, 0x0000},
        {2, 0x3E92, 0x0000},
        {2, 0x3E94, 0x0000},
        {2, 0x3E96, 0x0000},
        {2, 0x3E98, 0x0000},
        {2, 0x3E9A, 0x0000},
        {2, 0x3E9C, 0x0000},
        {2, 0x3E9E, 0x0000},
        {2, 0x3EA0, 0x0000},
        {2, 0x3EA2, 0x0000},
        {2, 0x3EA4, 0x0000},
        {2, 0x3EA6, 0x0000},
        {2, 0x3EA8, 0x0000},
        {2, 0x3EAA, 0x0000},
        {2, 0x3EAC, 0x0000},
        {2, 0x3EAE, 0x0000},
        {2, 0x3EB0, 0x0000},
        {2, 0x3EB2, 0x0000},
        {2, 0x3EB4, 0x0000},
        {2, 0x3EB6, 0x0000},
        {2, 0x3EB8, 0x0000},
        {2, 0x3EBA, 0x0000},
        {2, 0x3EBC, 0x0000},
        {2, 0x3EBE, 0x0000},
        {2, 0x3EC0, 0x0000},
        {2, 0x3EC2, 0x0000},
        {2, 0x3EC4, 0x0000},
        {2, 0x3EC6, 0x0000},
        {2, 0x3EC8, 0x0000},
        {2, 0x3ECA, 0x0000}, // Customer_Request 35298 - End

        {2, 0x3170, 0x2150},
        {2, 0x317A, 0x0150},
        {2, 0x3ECC, 0x2200},
        {2, 0x3174, 0x0000},
        {2, 0x3176, 0X0000},
        {2, 0x30BC, 0x0384},
        {2, 0x30C0, 0x1220},
        {2, 0x30D4, 0x9200},
        {2, 0x30B2, 0xC000},
        {2, 0x31B0, 0x00C4},
        {2, 0x31B2, 0x0064},
        {2, 0x31B4, 0x0E77},
        {2, 0x31B6, 0x0D24},
        {2, 0x31B8, 0x020E},
        {2, 0x31BA, 0x0710},
        {2, 0x31BC, 0x2A0D},
        {2, 0x31BE, 0xC003},
        {2, 0x3ECE, 0x000A}, // DAC_LD_2_3

        //2.)    Add the 2DDC setting
        // add 2DDC setting_05132014
        // AR0543 ADACD and 2DDC settings
        // updated June 2013--ADACD and 2DDC settings
        // ADACD: low gain
        {2, 0x3100, 0x0002},     // ADACD_CONTROL
        {2, 0x3102, 0x0064},     // ADACD_NOISE_MODEL1
        {2, 0x3104, 0x0B6D},     // ADACD_NOISE_MODEL2
        {2, 0x3106, 0x0201},     // ADACD_NOISE_FLOOR1
        {2, 0x3108, 0x0905},     // ADACD_NOISE_FLOOR2
        {2, 0x310A, 0x002A},     // ADACD_PEDESTAL
        //2DDC: low gain
        {2, 0x31E0, 0x1F01},      // PIX_DEF_ID
        {2, 0x3F02, 0x0001},      // PIX_DEF_2D_DDC_THRESH_HI3
        {2, 0x3F04, 0x0032},      // PIX_DEF_2D_DDC_THRESH_LO3
        {2, 0x3F06, 0x015E},      // PIX_DEF_2D_DDC_THRESH_HI4
        {2, 0x3F08, 0x0190},      // PIX_DEF_2D_DDC_THRESH_LO4
        //3.)    Add Minimum Analog Gain ( Normalized 1x Gain)
        //>> Just test in normal light environment
        // add Minimum Analog Gain
        {2, 0x305E, 0x1127}, // GLOBAL_GAIN
#endif

        {1,0x0104, 0x01},    // GROUPED_PARAMETER_HOLD = 0x1
#if 1 //85h lens shading
        {2, 0x3780, 0x0000}, 	// POLY_SC_ENABLE
        {2, 0x3600, 0x0170}, 	// P_GR_P0Q0
        {2, 0x3602, 0x2348}, 	// P_GR_P0Q1
        {2, 0x3604, 0x1D71}, 	// P_GR_P0Q2
        {2, 0x3606, 0x2808}, 	// P_GR_P0Q3
        {2, 0x3608, 0xC4B0}, 	// P_GR_P0Q4
        {2, 0x360A, 0x0170}, 	// P_RD_P0Q0
        {2, 0x360C, 0xC04C}, 	// P_RD_P0Q1
        {2, 0x360E, 0x1CD1}, 	// P_RD_P0Q2
        {2, 0x3610, 0x15EF}, 	// P_RD_P0Q3
        {2, 0x3612, 0xFD30}, 	// P_RD_P0Q4
        {2, 0x3614, 0x00D0}, 	// P_BL_P0Q0
        {2, 0x3616, 0x64EA}, 	// P_BL_P0Q1
        {2, 0x3618, 0x2FF0}, 	// P_BL_P0Q2
        {2, 0x361A, 0xB6EE}, 	// P_BL_P0Q3
        {2, 0x361C, 0xABCF}, 	// P_BL_P0Q4
        {2, 0x361E, 0x01F0}, 	// P_GB_P0Q0
        {2, 0x3620, 0xF76C}, 	// P_GB_P0Q1
        {2, 0x3622, 0x2751}, 	// P_GB_P0Q2
        {2, 0x3624, 0x0FAE}, 	// P_GB_P0Q3
        {2, 0x3626, 0xFFD0}, 	// P_GB_P0Q4
        {2, 0x3640, 0x80ED}, 	// P_GR_P1Q0
        {2, 0x3642, 0x834E}, 	// P_GR_P1Q1
        {2, 0x3644, 0x8AEF}, 	// P_GR_P1Q2
        {2, 0x3646, 0x3DAC}, 	// P_GR_P1Q3
        {2, 0x3648, 0x2D50}, 	// P_GR_P1Q4
        {2, 0x364A, 0xF32C}, 	// P_RD_P1Q0
        {2, 0x364C, 0x5ECE}, 	// P_RD_P1Q1
        {2, 0x364E, 0x8B0E}, 	// P_RD_P1Q2
        {2, 0x3650, 0xA7AF}, 	// P_RD_P1Q3
        {2, 0x3652, 0x2A6F}, 	// P_RD_P1Q4
        {2, 0x3654, 0xA2AC}, 	// P_BL_P1Q0
        {2, 0x3656, 0x134E}, 	// P_BL_P1Q1
        {2, 0x3658, 0x3E6E}, 	// P_BL_P1Q2
        {2, 0x365A, 0xCF6D}, 	// P_BL_P1Q3
        {2, 0x365C, 0xA1CE}, 	// P_BL_P1Q4
        {2, 0x365E, 0x9D2C}, 	// P_GB_P1Q0
        {2, 0x3660, 0xC0CE}, 	// P_GB_P1Q1
        {2, 0x3662, 0x310E}, 	// P_GB_P1Q2
        {2, 0x3664, 0x2B6E}, 	// P_GB_P1Q3
        {2, 0x3666, 0xA46E}, 	// P_GB_P1Q4
        {2, 0x3680, 0x25F1}, 	// P_GR_P2Q0
        {2, 0x3682, 0x612E}, 	// P_GR_P2Q1
        {2, 0x3684, 0x166F}, 	// P_GR_P2Q2
        {2, 0x3686, 0x544E}, 	// P_GR_P2Q3
        {2, 0x3688, 0x92D3}, 	// P_GR_P2Q4
        {2, 0x368A, 0x4DB1}, 	// P_RD_P2Q0
        {2, 0x368C, 0xFE2D}, 	// P_RD_P2Q1
        {2, 0x368E, 0x39CE}, 	// P_RD_P2Q2
        {2, 0x3690, 0xCACF}, 	// P_RD_P2Q3
        {2, 0x3692, 0x8073}, 	// P_RD_P2Q4
        {2, 0x3694, 0x7BD0}, 	// P_BL_P2Q0
        {2, 0x3696, 0x1A2E}, 	// P_BL_P2Q1
        {2, 0x3698, 0x4BEF}, 	// P_BL_P2Q2
        {2, 0x369A, 0x2FF0}, 	// P_BL_P2Q3
        {2, 0x369C, 0xB072}, 	// P_BL_P2Q4
        {2, 0x369E, 0x1EB1}, 	// P_GB_P2Q0
        {2, 0x36A0, 0xB10D}, 	// P_GB_P2Q1
        {2, 0x36A2, 0x532E}, 	// P_GB_P2Q2
        {2, 0x36A4, 0xDEEE}, 	// P_GB_P2Q3
        {2, 0x36A6, 0xEC12}, 	// P_GB_P2Q4
        {2, 0x36C0, 0x39EC}, 	// P_GR_P3Q0
        {2, 0x36C2, 0x89CD}, 	// P_GR_P3Q1
        {2, 0x36C4, 0x39D0}, 	// P_GR_P3Q2
        {2, 0x36C6, 0x7E10}, 	// P_GR_P3Q3
        {2, 0x36C8, 0xD031}, 	// P_GR_P3Q4
        {2, 0x36CA, 0x3BAF}, 	// P_RD_P3Q0
        {2, 0x36CC, 0xEC8D}, 	// P_RD_P3Q1
        {2, 0x36CE, 0x448E}, 	// P_RD_P3Q2
        {2, 0x36D0, 0x402F}, 	// P_RD_P3Q3
        {2, 0x36D2, 0xD691}, 	// P_RD_P3Q4
        {2, 0x36D4, 0x6D49}, 	// P_BL_P3Q0
        {2, 0x36D6, 0xF42D}, 	// P_BL_P3Q1
        {2, 0x36D8, 0x2FB0}, 	// P_BL_P3Q2
        {2, 0x36DA, 0x598B}, 	// P_BL_P3Q3
        {2, 0x36DC, 0xCE91}, 	// P_BL_P3Q4
        {2, 0x36DE, 0x0E2C}, 	// P_GB_P3Q0
        {2, 0x36E0, 0x1189}, 	// P_GB_P3Q1
        {2, 0x36E2, 0x0571}, 	// P_GB_P3Q2
        {2, 0x36E4, 0x1DD0}, 	// P_GB_P3Q3
        {2, 0x36E6, 0x9CB2}, 	// P_GB_P3Q4
        {2, 0x3700, 0xD830}, 	// P_GR_P4Q0
        {2, 0x3702, 0x8D0D}, 	// P_GR_P4Q1
        {2, 0x3704, 0xB9F3}, 	// P_GR_P4Q2
        {2, 0x3706, 0x9CB1}, 	// P_GR_P4Q3
        {2, 0x3708, 0x5CF3}, 	// P_GR_P4Q4
        {2, 0x370A, 0x8291}, 	// P_RD_P4Q0
        {2, 0x370C, 0x65EE}, 	// P_RD_P4Q1
        {2, 0x370E, 0xEDD3}, 	// P_RD_P4Q2
        {2, 0x3710, 0xD82E}, 	// P_RD_P4Q3
        {2, 0x3712, 0x3D14}, 	// P_RD_P4Q4
        {2, 0x3714, 0xA150}, 	// P_BL_P4Q0
        {2, 0x3716, 0xB06F}, 	// P_BL_P4Q1
        {2, 0x3718, 0xFC12}, 	// P_BL_P4Q2
        {2, 0x371A, 0x9170}, 	// P_BL_P4Q3
        {2, 0x371C, 0x07B3}, 	// P_BL_P4Q4
        {2, 0x371E, 0x9E90}, 	// P_GB_P4Q0
        {2, 0x3720, 0xBD8B}, 	// P_GB_P4Q1
        {2, 0x3722, 0xB413}, 	// P_GB_P4Q2
        {2, 0x3724, 0x1611}, 	// P_GB_P4Q3
        {2, 0x3726, 0x1B53}, 	// P_GB_P4Q4
        {2, 0x3782, 0x04F4}, 	// POLY_ORIGIN_C
        {2, 0x3784, 0x03C8}, 	// POLY_ORIGIN_R
        {2, 0x37C0, 0x3E29}, 	// P_GR_Q5
        {2, 0x37C2, 0x0FC8}, 	// P_RD_Q5
        {2, 0x37C4, 0x0F2A}, 	// P_BL_Q5
        {2, 0x37C6, 0x5209}, 	// P_GB_Q5
        {2, 0x3780, 0x8000}, 	// POLY_SC_ENABLE
#endif

        //1296 x 972  Timing settings 30fps
        {2,0x3064, 0xB800},    // SMIA_TEST
        {2,0x31AE, 0x0202},    // two lane 201 tow 202
        {2,0x0112, 0x0A0A},    // 10bit raw output
        //PLL MCLK=26MHZ, PCLK = 104MHZ, VT = 104MHZ
        {2,0x0300, 0x05},    //vt_pix_clk_div = 5
        {2,0x0302, 0x01},    //vt_sys_clk_div = 1
        {2,0x0304, 0x02},    //pre_pll_clk_div = 2
        {2,0x0306, 0x50},    //pll_multiplier    =  40
        {2,0x0308, 0x0A},    //op_pix_clk_div =  10
        {2,0x030A, 0x01},    //op_sys_clk_div = 1

        {2,0x0344, 0x0018},    // X_ADDR_START   =  8
        {2,0x0346, 0x0014},    // Y_ADDR_START   =  8
        {2,0x0348, 0x0A11},    // X_ADDR_END      = 2597
        {2,0x034A, 0x078D},    // Y_ADDR_END       =  1949
        {2,0x3040, 0x85C7},    // READ_MODE  10 011 000011 xy binning enable xodd=3, yodd=3
        {2,0x034C, 0x0280},    // X_OUTPUT_SIZE    = 1296
        {2,0x034E, 0x01E0},    // Y_OUTPUT_SIZE    =  972
        {2,0x300C, 0x169A},    // LINE_LENGTH  3151
        {2,0x300A, 0x0229},    // FRAME_LINEs  1100

        {2,0x3014, 0x1356},    // fine_integration_time
        {2,0x3010, 0x0184},    // fine_correction
        {2,0x3012, 0x0228},    // Coarse Integration Time = 0x228
        {1,0x0104, 0x00},    // GROUPED_PARAMETER_HOLD

        //start_streaming
        {1,0x0100, 0x01},    // MODE_SELECT
        {END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0543_preview_720P_script[] = {
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0543_720P_script_mipi[] = {
        // This file was generated by: AR0542 (A-5141) Register Wizard
        //   Version: 4.5.13.36518    Build Date: 07/08/2013
        //
        // [PLL PARAMETERS]
        //
        // Bypass PLL: Unchecked
        // Input Frequency: 24.000
        // Use Min Freq.: Unchecked
        // Target VT Frequency: 96.000
        // Target op_sys_clk Frequency: Unspecified
        //
        // Target PLL VT Frequency: 96 MHz
        // Target PLL OP Frequency: 96 MHz
        // MT9P017 Input Clock Frequency: 24 MHz
        // MT9P017 VT (Internal) Pixel Clock Frequency: 96 MHz
        // MT9P017 OP (Output) Pixel Clock Frequency: 48 MHz
        // pre_pll_clk_div = 2
        // pll_multiplier = 40
        // vt_sys_clk_div = 1
        // vt_pix_clk_div = 5
        // op_sys_clk_div = 1
        // op_pix_clk_div = 10
        // ip_clk = 12 MHz
        // op_clk = 480 MHz
        // op_sys_clk = 480 MHz
        //
        // [SENSOR PARAMETERS]
        //
        // Requested Frames Per Second: 30.000
        // Output Columns: 1280
        // Output Rows: 720
        // Use Binning: Checked
        // Allow Skipping: Unchecked
        // Blanking Computation: HB Max then VB
        //
        // Max Frame Time: 33.3333 msec
        // Max Frame Clocks: 3200000.0 clocks (96 MHz)
        // Maximun Frame Rate: 70.788 fps
        // Pixel Clock: divided by 1
        // Skip Mode: 2x cols, 2x rows, Bin Mode: Yes
        // Horiz clks:  1280 active + 2755 blank = 4035 total
        // Vert  rows:  720 active + 73 blank = 793 total
        //
        // Actual Frame Clocks: 3200000 clocks
        // Row Time: 42.031 usec / 4035 clocks
        // Frame time: 33.333333 msec
        // Frames per Sec: 30 fps
        //
        //
        //



        {1,0x0103, 0x01},    //SOFTWARE_RESET (clears itself)
        {TIME_DELAY, 0, 5},      //Initialization Time

        //stop_streaming
        {1,0x0100, 0x00},    // MODE_SELECT
        {2,0x301A, 0x0218},    //RESET_REGISTER enable mipi interface  bit[9] mask bad frame
        {2,0x3064, 0xB800},    // SMIA_TEST
        {2,0x31AE, 0x0202},    // two lane
        {2,0x0112, 0x0A0A},    // 10bit raw output
#if 1
        //1.)    Add the ?¡ãRecommended Settings?¡À
        //[=========== Recommended Settings ==============]
        //[REV1_recommended_settings]
        {2, 0x316A, 0x8400}, // RESERVED
        {2, 0x316C, 0x8400}, // RESERVED
        {2, 0x316E, 0x8400}, // RESERVED
        {2, 0x3EFA, 0x1A1F}, // RESERVED
        {2, 0x3ED2, 0xD965}, // RESERVED
        {2, 0x3ED8, 0x7F1B}, // RESERVED
        {2, 0x3EDA, 0x2F11}, // RESERVED
        {2, 0x3EE2, 0x0060}, // RESERVED
        {2, 0x3EF2, 0xD965}, // RESERVED
        {2, 0x3EF8, 0x797F}, // RESERVED
        {2, 0x3EFC, 0x286F}, // Customer_Request 35298
        {2, 0x3EFE, 0x2C01},
        //[REV1_pixel_timing]
        // @00 Jump Table  // Customer_Request 35298 - Start
        {2, 0x3E00, 0x042F},
        {2, 0x3E02, 0xFFFF},
        {2, 0x3E04, 0xFFFF},
        {2, 0x3E06, 0xFFFF},
        // @04 Read
        {2, 0x3E08, 0x8071},
        {2, 0x3E0A, 0x7281},
        {2, 0x3E0C, 0x4011},
        {2, 0x3E0E, 0x8010},
        {2, 0x3E10, 0x60A5},
        {2, 0x3E12, 0x4080},
        {2, 0x3E14, 0x4180},
        {2, 0x3E16, 0x0018},
        {2, 0x3E18, 0x46B7},
        {2, 0x3E1A, 0x4994},
        {2, 0x3E1C, 0x4997},
        {2, 0x3E1E, 0x4682},
        {2, 0x3E20, 0x0018},
        {2, 0x3E22, 0x4241},
        {2, 0x3E24, 0x8000},
        {2, 0x3E26, 0x1880},
        {2, 0x3E28, 0x4785},
        {2, 0x3E2A, 0x4992},
        {2, 0x3E2C, 0x4997},
        {2, 0x3E2E, 0x4780},
        {2, 0x3E30, 0x4D80},
        {2, 0x3E32, 0x100C},
        {2, 0x3E34, 0x8000},
        {2, 0x3E36, 0x184A},
        {2, 0x3E38, 0x8042},
        {2, 0x3E3A, 0x001A},
        {2, 0x3E3C, 0x9610},
        {2, 0x3E3E, 0x0C80},
        {2, 0x3E40, 0x4DC6},
        {2, 0x3E42, 0x4A80},
        {2, 0x3E44, 0x0018},
        {2, 0x3E46, 0x8042},
        {2, 0x3E48, 0x8041},
        {2, 0x3E4A, 0x0018},
        {2, 0x3E4C, 0x804B},
        {2, 0x3E4E, 0xB74B},
        {2, 0x3E50, 0x8010},
        {2, 0x3E52, 0x6056},
        {2, 0x3E54, 0x001C},
        {2, 0x3E56, 0x8211},
        {2, 0x3E58, 0x8056},
        {2, 0x3E5A, 0x827C},
        {2, 0x3E5C, 0x0970},
        {2, 0x3E5E, 0x8082},
        {2, 0x3E60, 0x7281},
        {2, 0x3E62, 0x4C40},
        {2, 0x3E64, 0x8E4D},
        {2, 0x3E66, 0x8110},
        {2, 0x3E68, 0x0CAF},
        {2, 0x3E6A, 0x4D80},
        {2, 0x3E6C, 0x100C},
        {2, 0x3E6E, 0x8440},
        {2, 0x3E70, 0x4C81},
        {2, 0x3E72, 0x7C5F},
        {2, 0x3E74, 0x7000},
        {2, 0x3E76, 0x0000},
        {2, 0x3E78, 0x0000},
        {2, 0x3E7A, 0x0000},
        {2, 0x3E7C, 0x0000},
        {2, 0x3E7E, 0x0000},
        {2, 0x3E80, 0x0000},
        {2, 0x3E82, 0x0000},
        {2, 0x3E84, 0x0000},
        {2, 0x3E86, 0x0000},
        {2, 0x3E88, 0x0000},
        {2, 0x3E8A, 0x0000},
        {2, 0x3E8C, 0x0000},
        {2, 0x3E8E, 0x0000},
        {2, 0x3E90, 0x0000},
        {2, 0x3E92, 0x0000},
        {2, 0x3E94, 0x0000},
        {2, 0x3E96, 0x0000},
        {2, 0x3E98, 0x0000},
        {2, 0x3E9A, 0x0000},
        {2, 0x3E9C, 0x0000},
        {2, 0x3E9E, 0x0000},
        {2, 0x3EA0, 0x0000},
        {2, 0x3EA2, 0x0000},
        {2, 0x3EA4, 0x0000},
        {2, 0x3EA6, 0x0000},
        {2, 0x3EA8, 0x0000},
        {2, 0x3EAA, 0x0000},
        {2, 0x3EAC, 0x0000},
        {2, 0x3EAE, 0x0000},
        {2, 0x3EB0, 0x0000},
        {2, 0x3EB2, 0x0000},
        {2, 0x3EB4, 0x0000},
        {2, 0x3EB6, 0x0000},
        {2, 0x3EB8, 0x0000},
        {2, 0x3EBA, 0x0000},
        {2, 0x3EBC, 0x0000},
        {2, 0x3EBE, 0x0000},
        {2, 0x3EC0, 0x0000},
        {2, 0x3EC2, 0x0000},
        {2, 0x3EC4, 0x0000},
        {2, 0x3EC6, 0x0000},
        {2, 0x3EC8, 0x0000},
        {2, 0x3ECA, 0x0000}, // Customer_Request 35298 - End

        {2, 0x3170, 0x2150},
        {2, 0x317A, 0x0150},
        {2, 0x3ECC, 0x2200},
        {2, 0x3174, 0x0000},
        {2, 0x3176, 0X0000},
        {2, 0x30BC, 0x0384},
        {2, 0x30C0, 0x1220},
        {2, 0x30D4, 0x9200},
        {2, 0x30B2, 0xC000},
        {2, 0x31B0, 0x00C4},
        {2, 0x31B2, 0x0064},
        {2, 0x31B4, 0x0E77},
        {2, 0x31B6, 0x0D24},
        {2, 0x31B8, 0x020E},
        {2, 0x31BA, 0x0710},
        {2, 0x31BC, 0x2A0D},
        {2, 0x31BE, 0xC003},
        {2, 0x3ECE, 0x000A}, // DAC_LD_2_3

        //2.)    Add the 2DDC setting
        // add 2DDC setting_05132014
        // AR0543 ADACD and 2DDC settings
        // updated June 2013--ADACD and 2DDC settings
        // ADACD: low gain
        {2, 0x3100, 0x0002},     // ADACD_CONTROL
        {2, 0x3102, 0x0064},     // ADACD_NOISE_MODEL1
        {2, 0x3104, 0x0B6D},     // ADACD_NOISE_MODEL2
        {2, 0x3106, 0x0201},     // ADACD_NOISE_FLOOR1
        {2, 0x3108, 0x0905},     // ADACD_NOISE_FLOOR2
        {2, 0x310A, 0x002A},     // ADACD_PEDESTAL
        //2DDC: low gain
        {2, 0x31E0, 0x1F01},      // PIX_DEF_ID
        {2, 0x3F02, 0x0001},      // PIX_DEF_2D_DDC_THRESH_HI3
        {2, 0x3F04, 0x0032},      // PIX_DEF_2D_DDC_THRESH_LO3
        {2, 0x3F06, 0x015E},      // PIX_DEF_2D_DDC_THRESH_HI4
        {2, 0x3F08, 0x0190},      // PIX_DEF_2D_DDC_THRESH_LO4
        //3.)    Add Minimum Analog Gain ( Normalized 1x Gain)
        //>> Just test in normal light environment
        // add Minimum Analog Gain
        {2, 0x305E, 0x1127}, // GLOBAL_GAIN
#endif

        {1,0x0104, 0x01},    // GROUPED_PARAMETER_HOLD = 0x1
#if 1 //85h lens shading
        {2, 0x3780, 0x0000}, 	// POLY_SC_ENABLE
        {2, 0x3600, 0x0170}, 	// P_GR_P0Q0
        {2, 0x3602, 0x2348}, 	// P_GR_P0Q1
        {2, 0x3604, 0x1D71}, 	// P_GR_P0Q2
        {2, 0x3606, 0x2808}, 	// P_GR_P0Q3
        {2, 0x3608, 0xC4B0}, 	// P_GR_P0Q4
        {2, 0x360A, 0x0170}, 	// P_RD_P0Q0
        {2, 0x360C, 0xC04C}, 	// P_RD_P0Q1
        {2, 0x360E, 0x1CD1}, 	// P_RD_P0Q2
        {2, 0x3610, 0x15EF}, 	// P_RD_P0Q3
        {2, 0x3612, 0xFD30}, 	// P_RD_P0Q4
        {2, 0x3614, 0x00D0}, 	// P_BL_P0Q0
        {2, 0x3616, 0x64EA}, 	// P_BL_P0Q1
        {2, 0x3618, 0x2FF0}, 	// P_BL_P0Q2
        {2, 0x361A, 0xB6EE}, 	// P_BL_P0Q3
        {2, 0x361C, 0xABCF}, 	// P_BL_P0Q4
        {2, 0x361E, 0x01F0}, 	// P_GB_P0Q0
        {2, 0x3620, 0xF76C}, 	// P_GB_P0Q1
        {2, 0x3622, 0x2751}, 	// P_GB_P0Q2
        {2, 0x3624, 0x0FAE}, 	// P_GB_P0Q3
        {2, 0x3626, 0xFFD0}, 	// P_GB_P0Q4
        {2, 0x3640, 0x80ED}, 	// P_GR_P1Q0
        {2, 0x3642, 0x834E}, 	// P_GR_P1Q1
        {2, 0x3644, 0x8AEF}, 	// P_GR_P1Q2
        {2, 0x3646, 0x3DAC}, 	// P_GR_P1Q3
        {2, 0x3648, 0x2D50}, 	// P_GR_P1Q4
        {2, 0x364A, 0xF32C}, 	// P_RD_P1Q0
        {2, 0x364C, 0x5ECE}, 	// P_RD_P1Q1
        {2, 0x364E, 0x8B0E}, 	// P_RD_P1Q2
        {2, 0x3650, 0xA7AF}, 	// P_RD_P1Q3
        {2, 0x3652, 0x2A6F}, 	// P_RD_P1Q4
        {2, 0x3654, 0xA2AC}, 	// P_BL_P1Q0
        {2, 0x3656, 0x134E}, 	// P_BL_P1Q1
        {2, 0x3658, 0x3E6E}, 	// P_BL_P1Q2
        {2, 0x365A, 0xCF6D}, 	// P_BL_P1Q3
        {2, 0x365C, 0xA1CE}, 	// P_BL_P1Q4
        {2, 0x365E, 0x9D2C}, 	// P_GB_P1Q0
        {2, 0x3660, 0xC0CE}, 	// P_GB_P1Q1
        {2, 0x3662, 0x310E}, 	// P_GB_P1Q2
        {2, 0x3664, 0x2B6E}, 	// P_GB_P1Q3
        {2, 0x3666, 0xA46E}, 	// P_GB_P1Q4
        {2, 0x3680, 0x25F1}, 	// P_GR_P2Q0
        {2, 0x3682, 0x612E}, 	// P_GR_P2Q1
        {2, 0x3684, 0x166F}, 	// P_GR_P2Q2
        {2, 0x3686, 0x544E}, 	// P_GR_P2Q3
        {2, 0x3688, 0x92D3}, 	// P_GR_P2Q4
        {2, 0x368A, 0x4DB1}, 	// P_RD_P2Q0
        {2, 0x368C, 0xFE2D}, 	// P_RD_P2Q1
        {2, 0x368E, 0x39CE}, 	// P_RD_P2Q2
        {2, 0x3690, 0xCACF}, 	// P_RD_P2Q3
        {2, 0x3692, 0x8073}, 	// P_RD_P2Q4
        {2, 0x3694, 0x7BD0}, 	// P_BL_P2Q0
        {2, 0x3696, 0x1A2E}, 	// P_BL_P2Q1
        {2, 0x3698, 0x4BEF}, 	// P_BL_P2Q2
        {2, 0x369A, 0x2FF0}, 	// P_BL_P2Q3
        {2, 0x369C, 0xB072}, 	// P_BL_P2Q4
        {2, 0x369E, 0x1EB1}, 	// P_GB_P2Q0
        {2, 0x36A0, 0xB10D}, 	// P_GB_P2Q1
        {2, 0x36A2, 0x532E}, 	// P_GB_P2Q2
        {2, 0x36A4, 0xDEEE}, 	// P_GB_P2Q3
        {2, 0x36A6, 0xEC12}, 	// P_GB_P2Q4
        {2, 0x36C0, 0x39EC}, 	// P_GR_P3Q0
        {2, 0x36C2, 0x89CD}, 	// P_GR_P3Q1
        {2, 0x36C4, 0x39D0}, 	// P_GR_P3Q2
        {2, 0x36C6, 0x7E10}, 	// P_GR_P3Q3
        {2, 0x36C8, 0xD031}, 	// P_GR_P3Q4
        {2, 0x36CA, 0x3BAF}, 	// P_RD_P3Q0
        {2, 0x36CC, 0xEC8D}, 	// P_RD_P3Q1
        {2, 0x36CE, 0x448E}, 	// P_RD_P3Q2
        {2, 0x36D0, 0x402F}, 	// P_RD_P3Q3
        {2, 0x36D2, 0xD691}, 	// P_RD_P3Q4
        {2, 0x36D4, 0x6D49}, 	// P_BL_P3Q0
        {2, 0x36D6, 0xF42D}, 	// P_BL_P3Q1
        {2, 0x36D8, 0x2FB0}, 	// P_BL_P3Q2
        {2, 0x36DA, 0x598B}, 	// P_BL_P3Q3
        {2, 0x36DC, 0xCE91}, 	// P_BL_P3Q4
        {2, 0x36DE, 0x0E2C}, 	// P_GB_P3Q0
        {2, 0x36E0, 0x1189}, 	// P_GB_P3Q1
        {2, 0x36E2, 0x0571}, 	// P_GB_P3Q2
        {2, 0x36E4, 0x1DD0}, 	// P_GB_P3Q3
        {2, 0x36E6, 0x9CB2}, 	// P_GB_P3Q4
        {2, 0x3700, 0xD830}, 	// P_GR_P4Q0
        {2, 0x3702, 0x8D0D}, 	// P_GR_P4Q1
        {2, 0x3704, 0xB9F3}, 	// P_GR_P4Q2
        {2, 0x3706, 0x9CB1}, 	// P_GR_P4Q3
        {2, 0x3708, 0x5CF3}, 	// P_GR_P4Q4
        {2, 0x370A, 0x8291}, 	// P_RD_P4Q0
        {2, 0x370C, 0x65EE}, 	// P_RD_P4Q1
        {2, 0x370E, 0xEDD3}, 	// P_RD_P4Q2
        {2, 0x3710, 0xD82E}, 	// P_RD_P4Q3
        {2, 0x3712, 0x3D14}, 	// P_RD_P4Q4
        {2, 0x3714, 0xA150}, 	// P_BL_P4Q0
        {2, 0x3716, 0xB06F}, 	// P_BL_P4Q1
        {2, 0x3718, 0xFC12}, 	// P_BL_P4Q2
        {2, 0x371A, 0x9170}, 	// P_BL_P4Q3
        {2, 0x371C, 0x07B3}, 	// P_BL_P4Q4
        {2, 0x371E, 0x9E90}, 	// P_GB_P4Q0
        {2, 0x3720, 0xBD8B}, 	// P_GB_P4Q1
        {2, 0x3722, 0xB413}, 	// P_GB_P4Q2
        {2, 0x3724, 0x1611}, 	// P_GB_P4Q3
        {2, 0x3726, 0x1B53}, 	// P_GB_P4Q4
        {2, 0x3782, 0x04F4}, 	// POLY_ORIGIN_C
        {2, 0x3784, 0x03C8}, 	// POLY_ORIGIN_R
        {2, 0x37C0, 0x3E29}, 	// P_GR_Q5
        {2, 0x37C2, 0x0FC8}, 	// P_RD_Q5
        {2, 0x37C4, 0x0F2A}, 	// P_BL_Q5
        {2, 0x37C6, 0x5209}, 	// P_GB_Q5
        {2, 0x3780, 0x8000}, 	// POLY_SC_ENABLE
#endif

        //1296 x 972  Timing settings 30fps
        {2,0x3064, 0xB800},    // SMIA_TEST
        {2,0x31AE, 0x0202},    // two lane 201 tow 202
        {2,0x0112, 0x0A0A},    // 10bit raw output
        //PLL MCLK=26MHZ, PCLK = 104MHZ, VT = 104MHZ
        {2,0x0300, 0x04},    //vt_pix_clk_div = 5
        {2,0x0302, 0x01},    //vt_sys_clk_div = 1
        {2,0x0304, 0x02},    //pre_pll_clk_div = 2
        {2,0x0306, 0x40},    //pll_multiplier    =  40
        {2,0x0308, 0x0A},    //op_pix_clk_div =  10
        {2,0x030A, 0x01},    //op_sys_clk_div = 1

        {2,0x0344, 0x0018},    // X_ADDR_START   =  8
        {2,0x0346, 0x0104},    // Y_ADDR_START   =  8
        {2,0x0348, 0x0A15},    // X_ADDR_END      = 2597
        {2,0x034A, 0x06A1},    // Y_ADDR_END       =  1949
        {2,0x3040, 0x84C3},    // READ_MODE  10 011 000011 xy binning enable xodd=3, yodd=3
        {2,0x034C, 0x0500},    // X_OUTPUT_SIZE    = 1296
        {2,0x034E, 0x02D0},    // Y_OUTPUT_SIZE    =  972
        {2,0x300C, 0x0FC3},    // LINE_LENGTH  3151
        {2,0x300A, 0x0319},    // FRAME_LINEs  1100

        {2,0x3014, 0x0C7F},    // fine_integration_time
        {2,0x3010, 0x0319},    // fine_correction
        {1,0x0104, 0x00},    // GROUPED_PARAMETER_HOLD

        //start_streaming
        {1,0x0100, 0x01},    // MODE_SELECT
        {END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0543_preview_960P_script[] = {
	{END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0543_960P_script_mipi[] = {
        // This file was generated by: AR0542 (A-5141) Register Wizard
        //   Version: 4.5.13.36518    Build Date: 07/08/2013
        //
        // [PLL PARAMETERS]
        //
        // Bypass PLL: Unchecked
        // Input Frequency: 24.000
        // Use Min Freq.: Unchecked
        // Target VT Frequency: 96.000
        // Target op_sys_clk Frequency: Unspecified
        //
        // Target PLL VT Frequency: 96 MHz
        // Target PLL OP Frequency: 96 MHz
        // MT9P017 Input Clock Frequency: 24 MHz
        // MT9P017 VT (Internal) Pixel Clock Frequency: 96 MHz
        // MT9P017 OP (Output) Pixel Clock Frequency: 48 MHz
        // pre_pll_clk_div = 2
        // pll_multiplier = 40
        // vt_sys_clk_div = 1
        // vt_pix_clk_div = 5
        // op_sys_clk_div = 1
        // op_pix_clk_div = 10
        // ip_clk = 12 MHz
        // op_clk = 480 MHz
        // op_sys_clk = 480 MHz
        //
        // [SENSOR PARAMETERS]
        //
        // Requested Frames Per Second: 29.000
        // Output Columns: 1296
        // Output Rows: 972
        // Use Binning: Checked
        // Allow Skipping: Unchecked
        // Blanking Computation: HB Max then VB
        //
        // Max Frame Time: 34.4828 msec
        // Max Frame Clocks: 3310344.8 clocks (96 MHz)
        // Maximun Frame Rate: 54.554 fps
        // Pixel Clock: divided by 1
        // Skip Mode: 2x cols, 2x rows, Bin Mode: Yes
        // Horiz clks:  1296 active + 1871 blank = 3167 total
        // Vert  rows:  972 active + 73 blank = 1045 total
        //
        // Actual Frame Clocks: 3310344 clocks
        // Row Time: 32.990 usec / 3167 clocks
        // Frame time: 34.482750 msec
        // Frames per Sec: 29 fps
        //
        //
        //


        //[AR0542 (A-5141) Register Wizard Defaults]
#if 1
        {1,0x0103, 0x01},    //SOFTWARE_RESET (clears itself)
        {TIME_DELAY, 0, 5},      //Initialization Time

        //stop_streaming

        {1,0x0100, 0x00},    // MODE_SELECT
        //{2,0x301A, 0x0218},    //RESET_REGISTER enable mipi interface  bit[9] mask bad frame
        {2,0x3064, 0xB800},    // SMIA_TEST
        {2,0x31AE, 0x0202},    // two lane
        {2,0x0112, 0x0A0A},    // 10bit raw output
#if 1
        //1.)    Add the ?¡ãRecommended Settings?¡À
        //[=========== Recommended Settings ==============]
        //[REV1_recommended_settings]
        {2, 0x316A, 0x8400}, // RESERVED
        {2, 0x316C, 0x8400}, // RESERVED
        {2, 0x316E, 0x8400}, // RESERVED
        {2, 0x3EFA, 0x1A1F}, // RESERVED
        {2, 0x3ED2, 0xD965}, // RESERVED
        {2, 0x3ED8, 0x7F1B}, // RESERVED
        {2, 0x3EDA, 0x2F11}, // RESERVED
        {2, 0x3EE2, 0x0060}, // RESERVED
        {2, 0x3EF2, 0xD965}, // RESERVED
        {2, 0x3EF8, 0x797F}, // RESERVED
        {2, 0x3EFC, 0x286F}, // Customer_Request 35298
        {2, 0x3EFE, 0x2C01},
        //[REV1_pixel_timing]
        // @00 Jump Table  // Customer_Request 35298 - Start
        {2, 0x3E00, 0x042F},
        {2, 0x3E02, 0xFFFF},
        {2, 0x3E04, 0xFFFF},
        {2, 0x3E06, 0xFFFF},
        // @04 Read
        {2, 0x3E08, 0x8071},
        {2, 0x3E0A, 0x7281},
        {2, 0x3E0C, 0x4011},
        {2, 0x3E0E, 0x8010},
        {2, 0x3E10, 0x60A5},
        {2, 0x3E12, 0x4080},
        {2, 0x3E14, 0x4180},
        {2, 0x3E16, 0x0018},
        {2, 0x3E18, 0x46B7},
        {2, 0x3E1A, 0x4994},
        {2, 0x3E1C, 0x4997},
        {2, 0x3E1E, 0x4682},
        {2, 0x3E20, 0x0018},
        {2, 0x3E22, 0x4241},
        {2, 0x3E24, 0x8000},
        {2, 0x3E26, 0x1880},
        {2, 0x3E28, 0x4785},
        {2, 0x3E2A, 0x4992},
        {2, 0x3E2C, 0x4997},
        {2, 0x3E2E, 0x4780},
        {2, 0x3E30, 0x4D80},
        {2, 0x3E32, 0x100C},
        {2, 0x3E34, 0x8000},
        {2, 0x3E36, 0x184A},
        {2, 0x3E38, 0x8042},
        {2, 0x3E3A, 0x001A},
        {2, 0x3E3C, 0x9610},
        {2, 0x3E3E, 0x0C80},
        {2, 0x3E40, 0x4DC6},
        {2, 0x3E42, 0x4A80},
        {2, 0x3E44, 0x0018},
        {2, 0x3E46, 0x8042},
        {2, 0x3E48, 0x8041},
        {2, 0x3E4A, 0x0018},
        {2, 0x3E4C, 0x804B},
        {2, 0x3E4E, 0xB74B},
        {2, 0x3E50, 0x8010},
        {2, 0x3E52, 0x6056},
        {2, 0x3E54, 0x001C},
        {2, 0x3E56, 0x8211},
        {2, 0x3E58, 0x8056},
        {2, 0x3E5A, 0x827C},
        {2, 0x3E5C, 0x0970},
        {2, 0x3E5E, 0x8082},
        {2, 0x3E60, 0x7281},
        {2, 0x3E62, 0x4C40},
        {2, 0x3E64, 0x8E4D},
        {2, 0x3E66, 0x8110},
        {2, 0x3E68, 0x0CAF},
        {2, 0x3E6A, 0x4D80},
        {2, 0x3E6C, 0x100C},
        {2, 0x3E6E, 0x8440},
        {2, 0x3E70, 0x4C81},
        {2, 0x3E72, 0x7C5F},
        {2, 0x3E74, 0x7000},
        {2, 0x3E76, 0x0000},
        {2, 0x3E78, 0x0000},
        {2, 0x3E7A, 0x0000},
        {2, 0x3E7C, 0x0000},
        {2, 0x3E7E, 0x0000},
        {2, 0x3E80, 0x0000},
        {2, 0x3E82, 0x0000},
        {2, 0x3E84, 0x0000},
        {2, 0x3E86, 0x0000},
        {2, 0x3E88, 0x0000},
        {2, 0x3E8A, 0x0000},
        {2, 0x3E8C, 0x0000},
        {2, 0x3E8E, 0x0000},
        {2, 0x3E90, 0x0000},
        {2, 0x3E92, 0x0000},
        {2, 0x3E94, 0x0000},
        {2, 0x3E96, 0x0000},
        {2, 0x3E98, 0x0000},
        {2, 0x3E9A, 0x0000},
        {2, 0x3E9C, 0x0000},
        {2, 0x3E9E, 0x0000},
        {2, 0x3EA0, 0x0000},
        {2, 0x3EA2, 0x0000},
        {2, 0x3EA4, 0x0000},
        {2, 0x3EA6, 0x0000},
        {2, 0x3EA8, 0x0000},
        {2, 0x3EAA, 0x0000},
        {2, 0x3EAC, 0x0000},
        {2, 0x3EAE, 0x0000},
        {2, 0x3EB0, 0x0000},
        {2, 0x3EB2, 0x0000},
        {2, 0x3EB4, 0x0000},
        {2, 0x3EB6, 0x0000},
        {2, 0x3EB8, 0x0000},
        {2, 0x3EBA, 0x0000},
        {2, 0x3EBC, 0x0000},
        {2, 0x3EBE, 0x0000},
        {2, 0x3EC0, 0x0000},
        {2, 0x3EC2, 0x0000},
        {2, 0x3EC4, 0x0000},
        {2, 0x3EC6, 0x0000},
        {2, 0x3EC8, 0x0000},
        {2, 0x3ECA, 0x0000}, // Customer_Request 35298 - End

        {2, 0x3170, 0x2150},
        {2, 0x317A, 0x0150},
        {2, 0x3ECC, 0x2200},
        {2, 0x3174, 0x0000},
        {2, 0x3176, 0X0000},
        {2, 0x30BC, 0x0384},
        {2, 0x30C0, 0x1220},
        {2, 0x30D4, 0x9200},
        {2, 0x30B2, 0xC000},
        {2, 0x31B0, 0x00C4},
        {2, 0x31B2, 0x0064},
        {2, 0x31B4, 0x0E77},
        {2, 0x31B6, 0x0D24},
        {2, 0x31B8, 0x020E},
        {2, 0x31BA, 0x0710},
        {2, 0x31BC, 0x2A0D},
        {2, 0x31BE, 0xC003},
        {2, 0x3ECE, 0x000A}, // DAC_LD_2_3

        //2.)    Add the 2DDC setting
        // add 2DDC setting_05132014
        // AR0543 ADACD and 2DDC settings
        // updated June 2013--ADACD and 2DDC settings
        // ADACD: low gain
        {2, 0x3100, 0x0002},     // ADACD_CONTROL
        {2, 0x3102, 0x0064},     // ADACD_NOISE_MODEL1
        {2, 0x3104, 0x0B6D},     // ADACD_NOISE_MODEL2
        {2, 0x3106, 0x0201},     // ADACD_NOISE_FLOOR1
        {2, 0x3108, 0x0905},     // ADACD_NOISE_FLOOR2
        {2, 0x310A, 0x002A},     // ADACD_PEDESTAL
        //2DDC: low gain
        {2, 0x31E0, 0x1F01},      // PIX_DEF_ID
        {2, 0x3F02, 0x0001},      // PIX_DEF_2D_DDC_THRESH_HI3
        {2, 0x3F04, 0x0032},      // PIX_DEF_2D_DDC_THRESH_LO3
        {2, 0x3F06, 0x015E},      // PIX_DEF_2D_DDC_THRESH_HI4
        {2, 0x3F08, 0x0190},      // PIX_DEF_2D_DDC_THRESH_LO4
        //3.)    Add Minimum Analog Gain ( Normalized 1x Gain)
        //>> Just test in normal light environment
        // add Minimum Analog Gain
        {2, 0x305E, 0x1127}, // GLOBAL_GAIN
#endif

        {1,0x0104, 0x01},    // GROUPED_PARAMETER_HOLD = 0x1
#if 1 //85h lens shading
        {2, 0x3780, 0x0000}, 	// POLY_SC_ENABLE
        {2, 0x3600, 0x0170}, 	// P_GR_P0Q0
        {2, 0x3602, 0x2348}, 	// P_GR_P0Q1
        {2, 0x3604, 0x1D71}, 	// P_GR_P0Q2
        {2, 0x3606, 0x2808}, 	// P_GR_P0Q3
        {2, 0x3608, 0xC4B0}, 	// P_GR_P0Q4
        {2, 0x360A, 0x0170}, 	// P_RD_P0Q0
        {2, 0x360C, 0xC04C}, 	// P_RD_P0Q1
        {2, 0x360E, 0x1CD1}, 	// P_RD_P0Q2
        {2, 0x3610, 0x15EF}, 	// P_RD_P0Q3
        {2, 0x3612, 0xFD30}, 	// P_RD_P0Q4
        {2, 0x3614, 0x00D0}, 	// P_BL_P0Q0
        {2, 0x3616, 0x64EA}, 	// P_BL_P0Q1
        {2, 0x3618, 0x2FF0}, 	// P_BL_P0Q2
        {2, 0x361A, 0xB6EE}, 	// P_BL_P0Q3
        {2, 0x361C, 0xABCF}, 	// P_BL_P0Q4
        {2, 0x361E, 0x01F0}, 	// P_GB_P0Q0
        {2, 0x3620, 0xF76C}, 	// P_GB_P0Q1
        {2, 0x3622, 0x2751}, 	// P_GB_P0Q2
        {2, 0x3624, 0x0FAE}, 	// P_GB_P0Q3
        {2, 0x3626, 0xFFD0}, 	// P_GB_P0Q4
        {2, 0x3640, 0x80ED}, 	// P_GR_P1Q0
        {2, 0x3642, 0x834E}, 	// P_GR_P1Q1
        {2, 0x3644, 0x8AEF}, 	// P_GR_P1Q2
        {2, 0x3646, 0x3DAC}, 	// P_GR_P1Q3
        {2, 0x3648, 0x2D50}, 	// P_GR_P1Q4
        {2, 0x364A, 0xF32C}, 	// P_RD_P1Q0
        {2, 0x364C, 0x5ECE}, 	// P_RD_P1Q1
        {2, 0x364E, 0x8B0E}, 	// P_RD_P1Q2
        {2, 0x3650, 0xA7AF}, 	// P_RD_P1Q3
        {2, 0x3652, 0x2A6F}, 	// P_RD_P1Q4
        {2, 0x3654, 0xA2AC}, 	// P_BL_P1Q0
        {2, 0x3656, 0x134E}, 	// P_BL_P1Q1
        {2, 0x3658, 0x3E6E}, 	// P_BL_P1Q2
        {2, 0x365A, 0xCF6D}, 	// P_BL_P1Q3
        {2, 0x365C, 0xA1CE}, 	// P_BL_P1Q4
        {2, 0x365E, 0x9D2C}, 	// P_GB_P1Q0
        {2, 0x3660, 0xC0CE}, 	// P_GB_P1Q1
        {2, 0x3662, 0x310E}, 	// P_GB_P1Q2
        {2, 0x3664, 0x2B6E}, 	// P_GB_P1Q3
        {2, 0x3666, 0xA46E}, 	// P_GB_P1Q4
        {2, 0x3680, 0x25F1}, 	// P_GR_P2Q0
        {2, 0x3682, 0x612E}, 	// P_GR_P2Q1
        {2, 0x3684, 0x166F}, 	// P_GR_P2Q2
        {2, 0x3686, 0x544E}, 	// P_GR_P2Q3
        {2, 0x3688, 0x92D3}, 	// P_GR_P2Q4
        {2, 0x368A, 0x4DB1}, 	// P_RD_P2Q0
        {2, 0x368C, 0xFE2D}, 	// P_RD_P2Q1
        {2, 0x368E, 0x39CE}, 	// P_RD_P2Q2
        {2, 0x3690, 0xCACF}, 	// P_RD_P2Q3
        {2, 0x3692, 0x8073}, 	// P_RD_P2Q4
        {2, 0x3694, 0x7BD0}, 	// P_BL_P2Q0
        {2, 0x3696, 0x1A2E}, 	// P_BL_P2Q1
        {2, 0x3698, 0x4BEF}, 	// P_BL_P2Q2
        {2, 0x369A, 0x2FF0}, 	// P_BL_P2Q3
        {2, 0x369C, 0xB072}, 	// P_BL_P2Q4
        {2, 0x369E, 0x1EB1}, 	// P_GB_P2Q0
        {2, 0x36A0, 0xB10D}, 	// P_GB_P2Q1
        {2, 0x36A2, 0x532E}, 	// P_GB_P2Q2
        {2, 0x36A4, 0xDEEE}, 	// P_GB_P2Q3
        {2, 0x36A6, 0xEC12}, 	// P_GB_P2Q4
        {2, 0x36C0, 0x39EC}, 	// P_GR_P3Q0
        {2, 0x36C2, 0x89CD}, 	// P_GR_P3Q1
        {2, 0x36C4, 0x39D0}, 	// P_GR_P3Q2
        {2, 0x36C6, 0x7E10}, 	// P_GR_P3Q3
        {2, 0x36C8, 0xD031}, 	// P_GR_P3Q4
        {2, 0x36CA, 0x3BAF}, 	// P_RD_P3Q0
        {2, 0x36CC, 0xEC8D}, 	// P_RD_P3Q1
        {2, 0x36CE, 0x448E}, 	// P_RD_P3Q2
        {2, 0x36D0, 0x402F}, 	// P_RD_P3Q3
        {2, 0x36D2, 0xD691}, 	// P_RD_P3Q4
        {2, 0x36D4, 0x6D49}, 	// P_BL_P3Q0
        {2, 0x36D6, 0xF42D}, 	// P_BL_P3Q1
        {2, 0x36D8, 0x2FB0}, 	// P_BL_P3Q2
        {2, 0x36DA, 0x598B}, 	// P_BL_P3Q3
        {2, 0x36DC, 0xCE91}, 	// P_BL_P3Q4
        {2, 0x36DE, 0x0E2C}, 	// P_GB_P3Q0
        {2, 0x36E0, 0x1189}, 	// P_GB_P3Q1
        {2, 0x36E2, 0x0571}, 	// P_GB_P3Q2
        {2, 0x36E4, 0x1DD0}, 	// P_GB_P3Q3
        {2, 0x36E6, 0x9CB2}, 	// P_GB_P3Q4
        {2, 0x3700, 0xD830}, 	// P_GR_P4Q0
        {2, 0x3702, 0x8D0D}, 	// P_GR_P4Q1
        {2, 0x3704, 0xB9F3}, 	// P_GR_P4Q2
        {2, 0x3706, 0x9CB1}, 	// P_GR_P4Q3
        {2, 0x3708, 0x5CF3}, 	// P_GR_P4Q4
        {2, 0x370A, 0x8291}, 	// P_RD_P4Q0
        {2, 0x370C, 0x65EE}, 	// P_RD_P4Q1
        {2, 0x370E, 0xEDD3}, 	// P_RD_P4Q2
        {2, 0x3710, 0xD82E}, 	// P_RD_P4Q3
        {2, 0x3712, 0x3D14}, 	// P_RD_P4Q4
        {2, 0x3714, 0xA150}, 	// P_BL_P4Q0
        {2, 0x3716, 0xB06F}, 	// P_BL_P4Q1
        {2, 0x3718, 0xFC12}, 	// P_BL_P4Q2
        {2, 0x371A, 0x9170}, 	// P_BL_P4Q3
        {2, 0x371C, 0x07B3}, 	// P_BL_P4Q4
        {2, 0x371E, 0x9E90}, 	// P_GB_P4Q0
        {2, 0x3720, 0xBD8B}, 	// P_GB_P4Q1
        {2, 0x3722, 0xB413}, 	// P_GB_P4Q2
        {2, 0x3724, 0x1611}, 	// P_GB_P4Q3
        {2, 0x3726, 0x1B53}, 	// P_GB_P4Q4
        {2, 0x3782, 0x04F4}, 	// POLY_ORIGIN_C
        {2, 0x3784, 0x03C8}, 	// POLY_ORIGIN_R
        {2, 0x37C0, 0x3E29}, 	// P_GR_Q5
        {2, 0x37C2, 0x0FC8}, 	// P_RD_Q5
        {2, 0x37C4, 0x0F2A}, 	// P_BL_Q5
        {2, 0x37C6, 0x5209}, 	// P_GB_Q5
        {2, 0x3780, 0x8000}, 	// POLY_SC_ENABLE
#endif


        //1296 x 972  Timing settings 30fps
        {2,0x3064, 0xB800},    // SMIA_TEST
        {2,0x31AE, 0x0202},    // two lane 201 tow 202
        {2,0x0112, 0x0A0A},    // 10bit raw output
        //PLL MCLK=24MHZ, PCLK = 96MHZ, VT = 96MHZ
        {2,0x0300, 0x05},    //vt_pix_clk_div = 5
        {2,0x0302, 0x01},    //vt_sys_clk_div = 1
        {2,0x0304, 0x02},    //pre_pll_clk_div = 2
        {2,0x0306, 0x53},    //pll_multiplier    =  40
        {2,0x0308, 0x0A},    //op_pix_clk_div =  10
        {2,0x030A, 0x01},    //op_sys_clk_div = 1



        // Timing Settings


        {2,0x0344, 0x0008},    // X_ADDR_START   =  8
        {2,0x0346, 0x0008},    // Y_ADDR_START   =  8
        {2,0x0348, 0x0A25},    // X_ADDR_END      = 2597
        {2,0x034A, 0x079D},    // Y_ADDR_END       =  1949
        {2,0x3040, 0x84C3},    // READ_MODE  10 011 000011 xy binning enable xodd=3, yodd=3
        {2,0x034C, 0x0510},    // X_OUTPUT_SIZE    = 1296
        {2,0x034E, 0x03CC},    // Y_OUTPUT_SIZE    =  972

        {2,0x300C, 0x0C5F},    // LINE_LENGTH  3151
        {2,0x300A, 0x0415},    // FRAME_LINEs  1100

        {2,0x3014, 0x091B},    // fine_integration_time
        {2,0x3010, 0x0184},    // fine_correction
        {1,0x0104, 0x00},    // GROUPED_PARAMETER_HOLD

        //start_streaming
        {1,0x0100, 0x01},    // MODE_SELECT
#endif
        {END_OF_SCRIPT, 0, 0},

};

static cam_i2c_msg_t AR0543_preview_1080P_script[] = {
        {END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0543_1080P_script_mipi[] = {
        // This file was generated by: AR0542 (A-5141) Register Wizard
        //   Version: 4.5.13.36518    Build Date: 07/08/2013
        //
        // [PLL PARAMETERS]
        //
        // Bypass PLL: Unchecked
        // Input Frequency: 24.000
        // Use Min Freq.: Unchecked
        // Target VT Frequency: 96.000
        // Target op_sys_clk Frequency: Unspecified
        //
        // Target PLL VT Frequency: 96 MHz
        // Target PLL OP Frequency: 96 MHz
        // MT9P017 Input Clock Frequency: 24 MHz
        // MT9P017 VT (Internal) Pixel Clock Frequency: 96 MHz
        // MT9P017 OP (Output) Pixel Clock Frequency: 48 MHz
        // pre_pll_clk_div = 2
        // pll_multiplier = 40
        // vt_sys_clk_div = 1
        // vt_pix_clk_div = 5
        // op_sys_clk_div = 1
        // op_pix_clk_div = 10
        // ip_clk = 12 MHz
        // op_clk = 480 MHz
        // op_sys_clk = 480 MHz
        //
        // [SENSOR PARAMETERS]
        //
        // Requested Frames Per Second: 27.000
        // Output Columns: 1920
        // Output Rows: 1080
        // Use Binning: Unchecked
        // Allow Skipping: Unchecked
        // Blanking Computation: HB Max then VB
        //
        // Max Frame Time: 37.037 msec
        // Max Frame Clocks: 3555555.5 clocks (96 MHz)
        // Maximun Frame Rate: 27.456 fps
        // Pixel Clock: divided by 1
        // Skip Mode: 1x cols, 1x rows, Bin Mode: No
        // Horiz clks:  1920 active + 1153 blank = 3073 total
        // Vert  rows:  1080 active + 77 blank = 1157 total
        //
        // Actual Frame Clocks: 3555555 clocks
        // Row Time: 32.010 usec / 3073 clocks
        // Frame time: 37.037031 msec
        // Frames per Sec: 27 fps
        //
        //
        //



        //[AR0542 (A-5141) Register Wizard Defaults]



        {1,0x0103, 0x01},    //SOFTWARE_RESET (clears itself)
        {TIME_DELAY, 0, 5},      //Initialization Time

        //stop_streaming
        {1,0x0100, 0x00},    // MODE_SELECT
        {2,0x301A, 0x0218},    //RESET_REGISTER enable mipi interface  bit[9] mask bad frame
        {2,0x3064, 0xB800},    // SMIA_TEST
        {2,0x31AE, 0x0202},    // two lane
        {2,0x0112, 0x0A0A},    // 10bit raw output
        //Silicon Recommendation
#if 1
        //1.)    Add the ?¡ãRecommended Settings?¡À
        //[=========== Recommended Settings ==============]
        //[REV1_recommended_settings]
        {2, 0x316A, 0x8400}, // RESERVED
        {2, 0x316C, 0x8400}, // RESERVED
        {2, 0x316E, 0x8400}, // RESERVED
        {2, 0x3EFA, 0x1A1F}, // RESERVED
        {2, 0x3ED2, 0xD965}, // RESERVED
        {2, 0x3ED8, 0x7F1B}, // RESERVED
        {2, 0x3EDA, 0x2F11}, // RESERVED
        {2, 0x3EE2, 0x0060}, // RESERVED
        {2, 0x3EF2, 0xD965}, // RESERVED
        {2, 0x3EF8, 0x797F}, // RESERVED
        {2, 0x3EFC, 0x286F}, // Customer_Request 35298
        {2, 0x3EFE, 0x2C01},
        //[REV1_pixel_timing]
        // @00 Jump Table  // Customer_Request 35298 - Start
        {2, 0x3E00, 0x042F},
        {2, 0x3E02, 0xFFFF},
        {2, 0x3E04, 0xFFFF},
        {2, 0x3E06, 0xFFFF},
        // @04 Read
        {2, 0x3E08, 0x8071},
        {2, 0x3E0A, 0x7281},
        {2, 0x3E0C, 0x4011},
        {2, 0x3E0E, 0x8010},
        {2, 0x3E10, 0x60A5},
        {2, 0x3E12, 0x4080},
        {2, 0x3E14, 0x4180},
        {2, 0x3E16, 0x0018},
        {2, 0x3E18, 0x46B7},
        {2, 0x3E1A, 0x4994},
        {2, 0x3E1C, 0x4997},
        {2, 0x3E1E, 0x4682},
        {2, 0x3E20, 0x0018},
        {2, 0x3E22, 0x4241},
        {2, 0x3E24, 0x8000},
        {2, 0x3E26, 0x1880},
        {2, 0x3E28, 0x4785},
        {2, 0x3E2A, 0x4992},
        {2, 0x3E2C, 0x4997},
        {2, 0x3E2E, 0x4780},
        {2, 0x3E30, 0x4D80},
        {2, 0x3E32, 0x100C},
        {2, 0x3E34, 0x8000},
        {2, 0x3E36, 0x184A},
        {2, 0x3E38, 0x8042},
        {2, 0x3E3A, 0x001A},
        {2, 0x3E3C, 0x9610},
        {2, 0x3E3E, 0x0C80},
        {2, 0x3E40, 0x4DC6},
        {2, 0x3E42, 0x4A80},
        {2, 0x3E44, 0x0018},
        {2, 0x3E46, 0x8042},
        {2, 0x3E48, 0x8041},
        {2, 0x3E4A, 0x0018},
        {2, 0x3E4C, 0x804B},
        {2, 0x3E4E, 0xB74B},
        {2, 0x3E50, 0x8010},
        {2, 0x3E52, 0x6056},
        {2, 0x3E54, 0x001C},
        {2, 0x3E56, 0x8211},
        {2, 0x3E58, 0x8056},
        {2, 0x3E5A, 0x827C},
        {2, 0x3E5C, 0x0970},
        {2, 0x3E5E, 0x8082},
        {2, 0x3E60, 0x7281},
        {2, 0x3E62, 0x4C40},
        {2, 0x3E64, 0x8E4D},
        {2, 0x3E66, 0x8110},
        {2, 0x3E68, 0x0CAF},
        {2, 0x3E6A, 0x4D80},
        {2, 0x3E6C, 0x100C},
        {2, 0x3E6E, 0x8440},
        {2, 0x3E70, 0x4C81},
        {2, 0x3E72, 0x7C5F},
        {2, 0x3E74, 0x7000},
        {2, 0x3E76, 0x0000},
        {2, 0x3E78, 0x0000},
        {2, 0x3E7A, 0x0000},
        {2, 0x3E7C, 0x0000},
        {2, 0x3E7E, 0x0000},
        {2, 0x3E80, 0x0000},
        {2, 0x3E82, 0x0000},
        {2, 0x3E84, 0x0000},
        {2, 0x3E86, 0x0000},
        {2, 0x3E88, 0x0000},
        {2, 0x3E8A, 0x0000},
        {2, 0x3E8C, 0x0000},
        {2, 0x3E8E, 0x0000},
        {2, 0x3E90, 0x0000},
        {2, 0x3E92, 0x0000},
        {2, 0x3E94, 0x0000},
        {2, 0x3E96, 0x0000},
        {2, 0x3E98, 0x0000},
        {2, 0x3E9A, 0x0000},
        {2, 0x3E9C, 0x0000},
        {2, 0x3E9E, 0x0000},
        {2, 0x3EA0, 0x0000},
        {2, 0x3EA2, 0x0000},
        {2, 0x3EA4, 0x0000},
        {2, 0x3EA6, 0x0000},
        {2, 0x3EA8, 0x0000},
        {2, 0x3EAA, 0x0000},
        {2, 0x3EAC, 0x0000},
        {2, 0x3EAE, 0x0000},
        {2, 0x3EB0, 0x0000},
        {2, 0x3EB2, 0x0000},
        {2, 0x3EB4, 0x0000},
        {2, 0x3EB6, 0x0000},
        {2, 0x3EB8, 0x0000},
        {2, 0x3EBA, 0x0000},
        {2, 0x3EBC, 0x0000},
        {2, 0x3EBE, 0x0000},
        {2, 0x3EC0, 0x0000},
        {2, 0x3EC2, 0x0000},
        {2, 0x3EC4, 0x0000},
        {2, 0x3EC6, 0x0000},
        {2, 0x3EC8, 0x0000},
        {2, 0x3ECA, 0x0000}, // Customer_Request 35298 - End

        {2, 0x3170, 0x2150},
        {2, 0x317A, 0x0150},
        {2, 0x3ECC, 0x2200},
        {2, 0x3174, 0x0000},
        {2, 0x3176, 0X0000},
        {2, 0x30BC, 0x0384},
        {2, 0x30C0, 0x1220},
        {2, 0x30D4, 0x9200},
        {2, 0x30B2, 0xC000},
        {2, 0x31B0, 0x00C4},
        {2, 0x31B2, 0x0064},
        {2, 0x31B4, 0x0E77},
        {2, 0x31B6, 0x0D24},
        {2, 0x31B8, 0x020E},
        {2, 0x31BA, 0x0710},
        {2, 0x31BC, 0x2A0D},
        {2, 0x31BE, 0xC003},
        {2, 0x3ECE, 0x000A}, // DAC_LD_2_3

        //2.)    Add the 2DDC setting
        // add 2DDC setting_05132014
        // AR0543 ADACD and 2DDC settings
        // updated June 2013--ADACD and 2DDC settings
        // ADACD: low gain
        {2, 0x3100, 0x0002},     // ADACD_CONTROL
        {2, 0x3102, 0x0064},     // ADACD_NOISE_MODEL1
        {2, 0x3104, 0x0B6D},     // ADACD_NOISE_MODEL2
        {2, 0x3106, 0x0201},     // ADACD_NOISE_FLOOR1
        {2, 0x3108, 0x0905},     // ADACD_NOISE_FLOOR2
        {2, 0x310A, 0x002A},     // ADACD_PEDESTAL
        //2DDC: low gain
        {2, 0x31E0, 0x1F01},      // PIX_DEF_ID
        {2, 0x3F02, 0x0001},      // PIX_DEF_2D_DDC_THRESH_HI3
        {2, 0x3F04, 0x0032},      // PIX_DEF_2D_DDC_THRESH_LO3
        {2, 0x3F06, 0x015E},      // PIX_DEF_2D_DDC_THRESH_HI4
        {2, 0x3F08, 0x0190},      // PIX_DEF_2D_DDC_THRESH_LO4
        //3.)    Add Minimum Analog Gain ( Normalized 1x Gain)
        //>> Just test in normal light environment
        // add Minimum Analog Gain
        {2, 0x305E, 0x1127}, // GLOBAL_GAIN
#endif
        {1,0x0104, 0x01},    // GROUPED_PARAMETER_HOLD = 0x1
#if 1 //85h lens shading
        {2, 0x3780, 0x0000}, 	// POLY_SC_ENABLE
        {2, 0x3600, 0x0170}, 	// P_GR_P0Q0
        {2, 0x3602, 0x2348}, 	// P_GR_P0Q1
        {2, 0x3604, 0x1D71}, 	// P_GR_P0Q2
        {2, 0x3606, 0x2808}, 	// P_GR_P0Q3
        {2, 0x3608, 0xC4B0}, 	// P_GR_P0Q4
        {2, 0x360A, 0x0170}, 	// P_RD_P0Q0
        {2, 0x360C, 0xC04C}, 	// P_RD_P0Q1
        {2, 0x360E, 0x1CD1}, 	// P_RD_P0Q2
        {2, 0x3610, 0x15EF}, 	// P_RD_P0Q3
        {2, 0x3612, 0xFD30}, 	// P_RD_P0Q4
        {2, 0x3614, 0x00D0}, 	// P_BL_P0Q0
        {2, 0x3616, 0x64EA}, 	// P_BL_P0Q1
        {2, 0x3618, 0x2FF0}, 	// P_BL_P0Q2
        {2, 0x361A, 0xB6EE}, 	// P_BL_P0Q3
        {2, 0x361C, 0xABCF}, 	// P_BL_P0Q4
        {2, 0x361E, 0x01F0}, 	// P_GB_P0Q0
        {2, 0x3620, 0xF76C}, 	// P_GB_P0Q1
        {2, 0x3622, 0x2751}, 	// P_GB_P0Q2
        {2, 0x3624, 0x0FAE}, 	// P_GB_P0Q3
        {2, 0x3626, 0xFFD0}, 	// P_GB_P0Q4
        {2, 0x3640, 0x80ED}, 	// P_GR_P1Q0
        {2, 0x3642, 0x834E}, 	// P_GR_P1Q1
        {2, 0x3644, 0x8AEF}, 	// P_GR_P1Q2
        {2, 0x3646, 0x3DAC}, 	// P_GR_P1Q3
        {2, 0x3648, 0x2D50}, 	// P_GR_P1Q4
        {2, 0x364A, 0xF32C}, 	// P_RD_P1Q0
        {2, 0x364C, 0x5ECE}, 	// P_RD_P1Q1
        {2, 0x364E, 0x8B0E}, 	// P_RD_P1Q2
        {2, 0x3650, 0xA7AF}, 	// P_RD_P1Q3
        {2, 0x3652, 0x2A6F}, 	// P_RD_P1Q4
        {2, 0x3654, 0xA2AC}, 	// P_BL_P1Q0
        {2, 0x3656, 0x134E}, 	// P_BL_P1Q1
        {2, 0x3658, 0x3E6E}, 	// P_BL_P1Q2
        {2, 0x365A, 0xCF6D}, 	// P_BL_P1Q3
        {2, 0x365C, 0xA1CE}, 	// P_BL_P1Q4
        {2, 0x365E, 0x9D2C}, 	// P_GB_P1Q0
        {2, 0x3660, 0xC0CE}, 	// P_GB_P1Q1
        {2, 0x3662, 0x310E}, 	// P_GB_P1Q2
        {2, 0x3664, 0x2B6E}, 	// P_GB_P1Q3
        {2, 0x3666, 0xA46E}, 	// P_GB_P1Q4
        {2, 0x3680, 0x25F1}, 	// P_GR_P2Q0
        {2, 0x3682, 0x612E}, 	// P_GR_P2Q1
        {2, 0x3684, 0x166F}, 	// P_GR_P2Q2
        {2, 0x3686, 0x544E}, 	// P_GR_P2Q3
        {2, 0x3688, 0x92D3}, 	// P_GR_P2Q4
        {2, 0x368A, 0x4DB1}, 	// P_RD_P2Q0
        {2, 0x368C, 0xFE2D}, 	// P_RD_P2Q1
        {2, 0x368E, 0x39CE}, 	// P_RD_P2Q2
        {2, 0x3690, 0xCACF}, 	// P_RD_P2Q3
        {2, 0x3692, 0x8073}, 	// P_RD_P2Q4
        {2, 0x3694, 0x7BD0}, 	// P_BL_P2Q0
        {2, 0x3696, 0x1A2E}, 	// P_BL_P2Q1
        {2, 0x3698, 0x4BEF}, 	// P_BL_P2Q2
        {2, 0x369A, 0x2FF0}, 	// P_BL_P2Q3
        {2, 0x369C, 0xB072}, 	// P_BL_P2Q4
        {2, 0x369E, 0x1EB1}, 	// P_GB_P2Q0
        {2, 0x36A0, 0xB10D}, 	// P_GB_P2Q1
        {2, 0x36A2, 0x532E}, 	// P_GB_P2Q2
        {2, 0x36A4, 0xDEEE}, 	// P_GB_P2Q3
        {2, 0x36A6, 0xEC12}, 	// P_GB_P2Q4
        {2, 0x36C0, 0x39EC}, 	// P_GR_P3Q0
        {2, 0x36C2, 0x89CD}, 	// P_GR_P3Q1
        {2, 0x36C4, 0x39D0}, 	// P_GR_P3Q2
        {2, 0x36C6, 0x7E10}, 	// P_GR_P3Q3
        {2, 0x36C8, 0xD031}, 	// P_GR_P3Q4
        {2, 0x36CA, 0x3BAF}, 	// P_RD_P3Q0
        {2, 0x36CC, 0xEC8D}, 	// P_RD_P3Q1
        {2, 0x36CE, 0x448E}, 	// P_RD_P3Q2
        {2, 0x36D0, 0x402F}, 	// P_RD_P3Q3
        {2, 0x36D2, 0xD691}, 	// P_RD_P3Q4
        {2, 0x36D4, 0x6D49}, 	// P_BL_P3Q0
        {2, 0x36D6, 0xF42D}, 	// P_BL_P3Q1
        {2, 0x36D8, 0x2FB0}, 	// P_BL_P3Q2
        {2, 0x36DA, 0x598B}, 	// P_BL_P3Q3
        {2, 0x36DC, 0xCE91}, 	// P_BL_P3Q4
        {2, 0x36DE, 0x0E2C}, 	// P_GB_P3Q0
        {2, 0x36E0, 0x1189}, 	// P_GB_P3Q1
        {2, 0x36E2, 0x0571}, 	// P_GB_P3Q2
        {2, 0x36E4, 0x1DD0}, 	// P_GB_P3Q3
        {2, 0x36E6, 0x9CB2}, 	// P_GB_P3Q4
        {2, 0x3700, 0xD830}, 	// P_GR_P4Q0
        {2, 0x3702, 0x8D0D}, 	// P_GR_P4Q1
        {2, 0x3704, 0xB9F3}, 	// P_GR_P4Q2
        {2, 0x3706, 0x9CB1}, 	// P_GR_P4Q3
        {2, 0x3708, 0x5CF3}, 	// P_GR_P4Q4
        {2, 0x370A, 0x8291}, 	// P_RD_P4Q0
        {2, 0x370C, 0x65EE}, 	// P_RD_P4Q1
        {2, 0x370E, 0xEDD3}, 	// P_RD_P4Q2
        {2, 0x3710, 0xD82E}, 	// P_RD_P4Q3
        {2, 0x3712, 0x3D14}, 	// P_RD_P4Q4
        {2, 0x3714, 0xA150}, 	// P_BL_P4Q0
        {2, 0x3716, 0xB06F}, 	// P_BL_P4Q1
        {2, 0x3718, 0xFC12}, 	// P_BL_P4Q2
        {2, 0x371A, 0x9170}, 	// P_BL_P4Q3
        {2, 0x371C, 0x07B3}, 	// P_BL_P4Q4
        {2, 0x371E, 0x9E90}, 	// P_GB_P4Q0
        {2, 0x3720, 0xBD8B}, 	// P_GB_P4Q1
        {2, 0x3722, 0xB413}, 	// P_GB_P4Q2
        {2, 0x3724, 0x1611}, 	// P_GB_P4Q3
        {2, 0x3726, 0x1B53}, 	// P_GB_P4Q4
        {2, 0x3782, 0x04F4}, 	// POLY_ORIGIN_C
        {2, 0x3784, 0x03C8}, 	// POLY_ORIGIN_R
        {2, 0x37C0, 0x3E29}, 	// P_GR_Q5
        {2, 0x37C2, 0x0FC8}, 	// P_RD_Q5
        {2, 0x37C4, 0x0F2A}, 	// P_BL_Q5
        {2, 0x37C6, 0x5209}, 	// P_GB_Q5
        {2, 0x3780, 0x8000}, 	// POLY_SC_ENABLE
#endif
        //REG= 0x301C, 0x01 	//Turn-on streamming


        //1296 x 972  Timing settings 30fps
        {2,0x3064, 0xB800},    // SMIA_TEST
        {2,0x31AE, 0x0202},    // two lane 201 tow 202
        {2,0x0112, 0x0A0A},    // 10bit raw output
        //PLL MCLK=26MHZ, PCLK = 104MHZ, VT = 104MHZ
        {2,0x0300, 0x05},    //vt_pix_clk_div = 5
        {2,0x0302, 0x01},    //vt_sys_clk_div = 1
        {2,0x0304, 0x02},    //pre_pll_clk_div = 2
        {2,0x0306, 0x5A},    //pll_multiplier    =  40
        {2,0x0308, 0x0A},    //op_pix_clk_div =  10
        {2,0x030A, 0x01},    //op_sys_clk_div = 1

        {2,0x0344, 0x0158},    // X_ADDR_START   =  8
        {2,0x0346, 0x01B8},    // Y_ADDR_START   =  8
        {2,0x0348, 0x08D7},    // X_ADDR_END      = 2597
        {2,0x034A, 0x05EF},    // Y_ADDR_END       =  1949
        {2,0x3040, 0x8041},    // READ_MODE  10 011 000011 xy binning enable xodd=3, yodd=3
        {2,0x034C, 0x0780},    // X_OUTPUT_SIZE    = 1296
        {2,0x034E, 0x0438},    // Y_OUTPUT_SIZE    =  972
        {2,0x300C, 0x0C01},    // LINE_LENGTH  3151
        {2,0x300A, 0x0485},    // FRAME_LINEs  1100

        {2,0x3014, 0x0A1F},    // fine_integration_time
        {2,0x3010, 0x00A0},    // fine_correction
        {1,0x0104, 0x00},    // GROUPED_PARAMETER_HOLD

        //start_streaming
        {1,0x0100, 0x01},    // MODE_SELECT
        {END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0543_capture_5M_script[] = {
        {END_OF_SCRIPT, 0, 0},
};

static cam_i2c_msg_t AR0543_5M_script_mipi[] = {
        // This file was generated by: AR0542 (A-5141) Register Wizard
        //   Version: 4.5.13.36518    Build Date: 07/08/2013
        //
        // [PLL PARAMETERS]
        //
        // Bypass PLL: Unchecked
        // Input Frequency: 24.000
        // Use Min Freq.: Unchecked
        // Target VT Frequency: 96.000
        // Target op_sys_clk Frequency: Unspecified
        //
        // Target PLL VT Frequency: 96 MHz
        // Target PLL OP Frequency: 76.800 MHz
        // MT9P017 Input Clock Frequency: 24 MHz
        // MT9P017 VT (Internal) Pixel Clock Frequency: 96 MHz
        // MT9P017 OP (Output) Pixel Clock Frequency: 76.800 MHz
        // pre_pll_clk_div = 2
        // pll_multiplier = 64
        // vt_sys_clk_div = 1
        // vt_pix_clk_div = 8
        // op_sys_clk_div = 1
        // op_pix_clk_div = 10
        // ip_clk = 12 MHz
        // op_clk = 768 MHz
        // op_sys_clk = 768 MHz
        //
        // [SENSOR PARAMETERS]
        //
        // Requested Frames Per Second: 12.250
        // Output Columns: 2592
        // Output Rows: 1944
        // Use Binning: Unchecked
        // Allow Skipping: Unchecked
        // Blanking Computation: HB Max then VB
        //
        // Max Frame Time: 81.6327 msec
        // Max Frame Clocks: 7836734.6 clocks (96 MHz)
        // Maximun Frame Rate: 12.859 fps
        // Pixel Clock: divided by 1
        // Skip Mode: 1x cols, 1x rows, Bin Mode: No
        // Horiz clks:  2592 active + 1285 blank = 3877 total
        // Vert  rows:  1944 active + 77 blank = 2021 total
        //
        // Actual Frame Clocks: 7836734 clocks
        // Row Time: 40.385 usec / 3877 clocks
        // Frame time: 81.632646 msec
        // Frames per Sec: 12.250 fps

        {1,0x0103, 0x01},    //SOFTWARE_RESET (clears itself)
        {TIME_DELAY, 0, 5},      //Initialization Time
#if 1
        //[AR0542 (A-5141) Register Wizard Defaults]
        {1, 0x0100, 0x0},	//Mode Select = 0x0
        //REG = 0x301A, 0x0018	//Reset Register = 0x18
        {2, 0x0112, 0x0A0A},	//CCP Data Format = 0xA0A
        {2, 0x3064, 0x7800},	//SMIA_Test = 0x7800
        {2, 0x31AE, 0x0202},	//Serial Format = 0x202

        //Silicon Recommendation
        //REG= 0x301C, 0x00 	//Turn-off streamming
        {2, 0x316A, 0x8400}, 	//RESERVED
        {2, 0x316C, 0x8400}, 	//RESERVED
        {2, 0x316E, 0x8400}, 	//RESERVED
        {2, 0x3EFA, 0x171F}, 	//RESERVED
        {2, 0x3ED2, 0xD965}, 	//Manufacturer-Specific
        {2, 0x3ED8, 0x7F1B}, 	//Manufacturer-Specific
        {2, 0x3EDA, 0x2F11}, 	//Manufacturer-Specific
        {2, 0x3EDE, 0xB000}, 	//Manufacturer-Specific
        {2, 0x3EE2, 0x0060}, 	//Manufacturer-Specific
        {2, 0x3EF2, 0xD965}, 	//Manufacturer-Specific
        {2, 0x3EF8, 0x797F}, 	//Manufacturer-Specific
        {2, 0x3EFC, 0x246F}, 	//Manufacturer-Specific
        {2, 0x3EFE, 0x6F01}, 	//Manufacturer-Specific
        //LOAD= A-5141_pixel_timing
        {2, 0x3E00, 0x0428},
        {2, 0x3E02, 0xFFFF},
        {2, 0x3E04, 0xFFFF},
        {2, 0x3E06, 0xFFFF},
        {2, 0x3E08, 0x8071},
        {2, 0x3E0A, 0x7281},
        {2, 0x3E0C, 0x0041},
        {2, 0x3E0E, 0x5355},
        {2, 0x3E10, 0x8710},
        {2, 0x3E12, 0x6085},
        {2, 0x3E14, 0x4080},
        {2, 0x3E16, 0x41A0},
        {2, 0x3E18, 0x0018},
        {2, 0x3E1A, 0x9057},
        {2, 0x3E1C, 0xA049},
        {2, 0x3E1E, 0xA649},
        {2, 0x3E20, 0x8846},
        {2, 0x3E22, 0x8142},
        {2, 0x3E24, 0x0082},
        {2, 0x3E26, 0x8B49},
        {2, 0x3E28, 0x9C49},
        {2, 0x3E2A, 0x8A10},
        {2, 0x3E2C, 0x0C82},
        {2, 0x3E2E, 0x4784},
        {2, 0x3E30, 0x4D85},
        {2, 0x3E32, 0x0406},
        {2, 0x3E34, 0x9510},
        {2, 0x3E36, 0x0EC3},
        {2, 0x3E38, 0x4A42},
        {2, 0x3E3A, 0x8341},
        {2, 0x3E3C, 0x8B4B},
        {2, 0x3E3E, 0xA84B},
        {2, 0x3E40, 0x8056},
        {2, 0x3E42, 0x8000},
        {2, 0x3E44, 0x1C81},
        {2, 0x3E46, 0x10E0},
        {2, 0x3E48, 0x8055},
        {2, 0x3E4A, 0x1C00},
        {2, 0x3E4C, 0x827C},
        {2, 0x3E4E, 0x0970},
        {2, 0x3E50, 0x8082},
        {2, 0x3E52, 0x7281},
        {2, 0x3E54, 0x4C40},
        {2, 0x3E56, 0x9110},
        {2, 0x3E58, 0x0C85},
        {2, 0x3E5A, 0x4D9E},
        {2, 0x3E5C, 0x4D80},
        {2, 0x3E5E, 0x100C},
        {2, 0x3E60, 0x8E40},
        {2, 0x3E62, 0x4C81},
        {2, 0x3E64, 0x7C51},
        {2, 0x3E66, 0x7000},
        {2, 0x3E68, 0x0000},
        {2, 0x3E6A, 0x0000},
        {2, 0x3E6C, 0x0000},
        {2, 0x3E6E, 0x0000},
        {2, 0x3E70, 0x0000},
        {2, 0x3E72, 0x0000},
        {2, 0x3E74, 0x0000},
        {2, 0x3E76, 0x0000},
        {2, 0x3E78, 0x0000},
        {2, 0x3E7A, 0x0000},
        {2, 0x3E7C, 0x0000},
        {2, 0x3E7E, 0x0000},
        {2, 0x3E80, 0x0000},
        {2, 0x3E82, 0x0000},
        {2, 0x3E84, 0x0000},
        {2, 0x3E86, 0x0000},
        {2, 0x3E88, 0x0000},
        {2, 0x3E8A, 0x0000},
        {2, 0x3E8C, 0x0000},
        {2, 0x3E8E, 0x0000},
        {2, 0x3E90, 0x0000},
        {2, 0x3E92, 0x0000},
        {2, 0x3E94, 0x0000},
        {2, 0x3E96, 0x0000},
        {2, 0x3E98, 0x0000},
        {2, 0x3E9A, 0x0000},
        {2, 0x3E9C, 0x0000},
        {2, 0x3E9E, 0x0000},
        {2, 0x3EA0, 0x0000},
        {2, 0x3EA2, 0x0000},
        {2, 0x3EA4, 0x0000},
        {2, 0x3EA6, 0x0000},
        {2, 0x3EA8, 0x0000},
        {2, 0x3EAA, 0x0000},
        {2, 0x3EAC, 0x0000},
        {2, 0x3EAE, 0x0000},
        {2, 0x3EB0, 0x0000},
        {2, 0x3EB2, 0x0000},
        {2, 0x3EB4, 0x0000},
        {2, 0x3EB6, 0x0000},
        {2, 0x3EB8, 0x0000},
        {2, 0x3EBA, 0x0000},
        {2, 0x3EBC, 0x0000},
        {2, 0x3EBE, 0x0000},
        {2, 0x3EC0, 0x0000},
        {2, 0x3EC2, 0x0000},
        {2, 0x3EC4, 0x0000},
        {2, 0x3EC6, 0x0000},
        {2, 0x3EC8, 0x0000},
        {2, 0x3ECA, 0x0000},
        {2, 0x3170, 0x2150},	//Manufacturer-Specific
        {2, 0x317A, 0x0150},	//Manufacturer-Specific
        {2, 0x3ECC, 0x2200},	//Manufacturer-Specific
        {2, 0x3174, 0x0000},	//Manufacturer-Specific
        {2, 0x3176, 0X0000},	//Manufacturer-Specific
        {2, 0x30BC, 0x0384},	//CALIB_GLOBAL
        {2, 0x30C0, 0x1220},	//CALIB_CONTROL

        //REG= 0x301C, 0x01 	//Turn-on streamming
#if 1 //85h lens shading
        {1,0x0104, 0x01},    // GROUPED_PARAMETER_HOLD = 0x1
        {2, 0x3780, 0x0000}, 	// POLY_SC_ENABLE
        {2, 0x3600, 0x0170}, 	// P_GR_P0Q0
        {2, 0x3602, 0x2348}, 	// P_GR_P0Q1
        {2, 0x3604, 0x1D71}, 	// P_GR_P0Q2
        {2, 0x3606, 0x2808}, 	// P_GR_P0Q3
        {2, 0x3608, 0xC4B0}, 	// P_GR_P0Q4
        {2, 0x360A, 0x0170}, 	// P_RD_P0Q0
        {2, 0x360C, 0xC04C}, 	// P_RD_P0Q1
        {2, 0x360E, 0x1CD1}, 	// P_RD_P0Q2
        {2, 0x3610, 0x15EF}, 	// P_RD_P0Q3
        {2, 0x3612, 0xFD30}, 	// P_RD_P0Q4
        {2, 0x3614, 0x00D0}, 	// P_BL_P0Q0
        {2, 0x3616, 0x64EA}, 	// P_BL_P0Q1
        {2, 0x3618, 0x2FF0}, 	// P_BL_P0Q2
        {2, 0x361A, 0xB6EE}, 	// P_BL_P0Q3
        {2, 0x361C, 0xABCF}, 	// P_BL_P0Q4
        {2, 0x361E, 0x01F0}, 	// P_GB_P0Q0
        {2, 0x3620, 0xF76C}, 	// P_GB_P0Q1
        {2, 0x3622, 0x2751}, 	// P_GB_P0Q2
        {2, 0x3624, 0x0FAE}, 	// P_GB_P0Q3
        {2, 0x3626, 0xFFD0}, 	// P_GB_P0Q4
        {2, 0x3640, 0x80ED}, 	// P_GR_P1Q0
        {2, 0x3642, 0x834E}, 	// P_GR_P1Q1
        {2, 0x3644, 0x8AEF}, 	// P_GR_P1Q2
        {2, 0x3646, 0x3DAC}, 	// P_GR_P1Q3
        {2, 0x3648, 0x2D50}, 	// P_GR_P1Q4
        {2, 0x364A, 0xF32C}, 	// P_RD_P1Q0
        {2, 0x364C, 0x5ECE}, 	// P_RD_P1Q1
        {2, 0x364E, 0x8B0E}, 	// P_RD_P1Q2
        {2, 0x3650, 0xA7AF}, 	// P_RD_P1Q3
        {2, 0x3652, 0x2A6F}, 	// P_RD_P1Q4
        {2, 0x3654, 0xA2AC}, 	// P_BL_P1Q0
        {2, 0x3656, 0x134E}, 	// P_BL_P1Q1
        {2, 0x3658, 0x3E6E}, 	// P_BL_P1Q2
        {2, 0x365A, 0xCF6D}, 	// P_BL_P1Q3
        {2, 0x365C, 0xA1CE}, 	// P_BL_P1Q4
        {2, 0x365E, 0x9D2C}, 	// P_GB_P1Q0
        {2, 0x3660, 0xC0CE}, 	// P_GB_P1Q1
        {2, 0x3662, 0x310E}, 	// P_GB_P1Q2
        {2, 0x3664, 0x2B6E}, 	// P_GB_P1Q3
        {2, 0x3666, 0xA46E}, 	// P_GB_P1Q4
        {2, 0x3680, 0x25F1}, 	// P_GR_P2Q0
        {2, 0x3682, 0x612E}, 	// P_GR_P2Q1
        {2, 0x3684, 0x166F}, 	// P_GR_P2Q2
        {2, 0x3686, 0x544E}, 	// P_GR_P2Q3
        {2, 0x3688, 0x92D3}, 	// P_GR_P2Q4
        {2, 0x368A, 0x4DB1}, 	// P_RD_P2Q0
        {2, 0x368C, 0xFE2D}, 	// P_RD_P2Q1
        {2, 0x368E, 0x39CE}, 	// P_RD_P2Q2
        {2, 0x3690, 0xCACF}, 	// P_RD_P2Q3
        {2, 0x3692, 0x8073}, 	// P_RD_P2Q4
        {2, 0x3694, 0x7BD0}, 	// P_BL_P2Q0
        {2, 0x3696, 0x1A2E}, 	// P_BL_P2Q1
        {2, 0x3698, 0x4BEF}, 	// P_BL_P2Q2
        {2, 0x369A, 0x2FF0}, 	// P_BL_P2Q3
        {2, 0x369C, 0xB072}, 	// P_BL_P2Q4
        {2, 0x369E, 0x1EB1}, 	// P_GB_P2Q0
        {2, 0x36A0, 0xB10D}, 	// P_GB_P2Q1
        {2, 0x36A2, 0x532E}, 	// P_GB_P2Q2
        {2, 0x36A4, 0xDEEE}, 	// P_GB_P2Q3
        {2, 0x36A6, 0xEC12}, 	// P_GB_P2Q4
        {2, 0x36C0, 0x39EC}, 	// P_GR_P3Q0
        {2, 0x36C2, 0x89CD}, 	// P_GR_P3Q1
        {2, 0x36C4, 0x39D0}, 	// P_GR_P3Q2
        {2, 0x36C6, 0x7E10}, 	// P_GR_P3Q3
        {2, 0x36C8, 0xD031}, 	// P_GR_P3Q4
        {2, 0x36CA, 0x3BAF}, 	// P_RD_P3Q0
        {2, 0x36CC, 0xEC8D}, 	// P_RD_P3Q1
        {2, 0x36CE, 0x448E}, 	// P_RD_P3Q2
        {2, 0x36D0, 0x402F}, 	// P_RD_P3Q3
        {2, 0x36D2, 0xD691}, 	// P_RD_P3Q4
        {2, 0x36D4, 0x6D49}, 	// P_BL_P3Q0
        {2, 0x36D6, 0xF42D}, 	// P_BL_P3Q1
        {2, 0x36D8, 0x2FB0}, 	// P_BL_P3Q2
        {2, 0x36DA, 0x598B}, 	// P_BL_P3Q3
        {2, 0x36DC, 0xCE91}, 	// P_BL_P3Q4
        {2, 0x36DE, 0x0E2C}, 	// P_GB_P3Q0
        {2, 0x36E0, 0x1189}, 	// P_GB_P3Q1
        {2, 0x36E2, 0x0571}, 	// P_GB_P3Q2
        {2, 0x36E4, 0x1DD0}, 	// P_GB_P3Q3
        {2, 0x36E6, 0x9CB2}, 	// P_GB_P3Q4
        {2, 0x3700, 0xD830}, 	// P_GR_P4Q0
        {2, 0x3702, 0x8D0D}, 	// P_GR_P4Q1
        {2, 0x3704, 0xB9F3}, 	// P_GR_P4Q2
        {2, 0x3706, 0x9CB1}, 	// P_GR_P4Q3
        {2, 0x3708, 0x5CF3}, 	// P_GR_P4Q4
        {2, 0x370A, 0x8291}, 	// P_RD_P4Q0
        {2, 0x370C, 0x65EE}, 	// P_RD_P4Q1
        {2, 0x370E, 0xEDD3}, 	// P_RD_P4Q2
        {2, 0x3710, 0xD82E}, 	// P_RD_P4Q3
        {2, 0x3712, 0x3D14}, 	// P_RD_P4Q4
        {2, 0x3714, 0xA150}, 	// P_BL_P4Q0
        {2, 0x3716, 0xB06F}, 	// P_BL_P4Q1
        {2, 0x3718, 0xFC12}, 	// P_BL_P4Q2
        {2, 0x371A, 0x9170}, 	// P_BL_P4Q3
        {2, 0x371C, 0x07B3}, 	// P_BL_P4Q4
        {2, 0x371E, 0x9E90}, 	// P_GB_P4Q0
        {2, 0x3720, 0xBD8B}, 	// P_GB_P4Q1
        {2, 0x3722, 0xB413}, 	// P_GB_P4Q2
        {2, 0x3724, 0x1611}, 	// P_GB_P4Q3
        {2, 0x3726, 0x1B53}, 	// P_GB_P4Q4
        {2, 0x3782, 0x04F4}, 	// POLY_ORIGIN_C
        {2, 0x3784, 0x03C8}, 	// POLY_ORIGIN_R
        {2, 0x37C0, 0x3E29}, 	// P_GR_Q5
        {2, 0x37C2, 0x0FC8}, 	// P_RD_Q5
        {2, 0x37C4, 0x0F2A}, 	// P_BL_Q5
        {2, 0x37C6, 0x5209}, 	// P_GB_Q5
        {2, 0x3780, 0x8000}, 	// POLY_SC_ENABLE
#endif
        // PLL Settings
        {2, 0x0300, 0x04},	//vt_pix_clk_div = 0x8
        {2, 0x0302, 0x01},	//vt_sys_clk_div = 0x1
        {2, 0x0304, 0x02},	//pre_pll_clk_div = 0x2
        {2, 0x0306, 0x50},	//pll_multiplier = 0x40
        {2, 0x0308, 0x0A},	//op_pix_clk_div = 0xA
        {2, 0x030A, 0x01},	//op_sys_clk_div = 0x1
        //DELAY = 1               // Allow PLL to lock
        {TIME_DELAY, 0, 1},

        // Timing Settings
        {2, 0x034C, 0x0A20},	//Output Width = 0xA20
        {2, 0x034E, 0x0798},	//Output Height = 0x798
        {2, 0x0344, 0x008 },      //Column Start = 0x8
        {2, 0x0346, 0x008 },      //Row Start = 0x8
        {2, 0x0348, 0xA27 },      //Column End = 0xA27
        {2, 0x034A, 0x79F },      //Row End = 0x79F
        {2, 0x3040, 0x8041},	//Read Mode = 0x41
        {2, 0x3010, 0x00A0},	//Fine Correction = 0xA0
        {2, 0x3012, 0x07E4},	//Coarse Integration Time = 0x7E4
        {2, 0x3014, 0x0D43},	//Fine Integration Time = 0xD43
        {2, 0x0340, 0x07E5},	//Frame Lines = 0x7E5
        {2, 0x0342, 0x0F25},	//Line Length = 0xF25
        {1, 0x0104, 0x0},	//Grouped Parameter Hold = 0x0
        {1, 0x0100, 0x1},	//Mode Select = 0x1
#endif

        {END_OF_SCRIPT, 0, 0},
};

static resolution_param_t  prev_resolution_array[] = {
	{
		.frmsize			= {176, 144},
		.active_frmsize		= {640, 480},
		.active_fps			= 30,
		.size_type			= SIZE_176X144,
		.reg_script[0]		= AR0543_preview_VGA_script,
		.reg_script[1]		= AR0543_VGA_script_mipi,
	},{
		.frmsize			= {320, 240},
		.active_frmsize		= {640, 480},
		.active_fps			= 30,
		.size_type			= SIZE_320X240,
		.reg_script[0]		= AR0543_preview_VGA_script,
		.reg_script[1]		= AR0543_VGA_script_mipi,
	},{
		.frmsize			= {352, 288},
		.active_frmsize		= {640, 480},
		.active_fps			= 30,
		.size_type			= SIZE_352X288,
		.reg_script[0]		= AR0543_preview_VGA_script,
		.reg_script[1]		= AR0543_VGA_script_mipi,
	}, {
		.frmsize			= {640, 480},
		.active_frmsize		= {640, 480},
		.active_fps			= 30,
		.size_type			= SIZE_640X480,
		.reg_script[0]		= AR0543_preview_VGA_script,
		.reg_script[1]		= AR0543_VGA_script_mipi,
	}, {
		.frmsize			= {1280, 720},
		.active_frmsize		= {1280, 720},
		.active_fps			= 30,
		.size_type			= SIZE_1280X720,
		.reg_script[0]		= AR0543_preview_720P_script,
		.reg_script[1]		= AR0543_720P_script_mipi,
	}, {
		.frmsize			= {1280, 960},
		.active_frmsize		= {1296, 972},
		.active_fps			= 30,
		.size_type			= SIZE_1280X960,
		.reg_script[0]		= AR0543_preview_960P_script,
		.reg_script[1]		= AR0543_960P_script_mipi,
	}, {
		.frmsize			= {1920, 1080},
		.active_frmsize		= {1920, 1080},
		.active_fps			= 15,
		.size_type			= SIZE_1920X1080,
		.reg_script[0]		= AR0543_preview_1080P_script,
		.reg_script[1]		= AR0543_1080P_script_mipi,
	}
};

static resolution_param_t  debug_prev_resolution_array[] = {
	{
		.frmsize			= {176, 144},
		.active_frmsize		= {1296, 972},
		.active_fps			= 30,
		.size_type			= SIZE_176X144,
		.reg_script[0]		= AR0543_preview_VGA_script,
		.reg_script[1]		= AR0543_960P_script_mipi,
	},{
		.frmsize			= {320, 240},
		.active_frmsize		= {1296, 972},
		.active_fps			= 30,
		.size_type			= SIZE_320X240,
		.reg_script[0]		= AR0543_preview_VGA_script,
		.reg_script[1]		= AR0543_960P_script_mipi,
	},{
		.frmsize			= {352, 288},
		.active_frmsize		= {1296, 972},
		.active_fps			= 30,
		.size_type			= SIZE_352X288,
		.reg_script[0]		= AR0543_preview_VGA_script,
		.reg_script[1]		= AR0543_960P_script_mipi,
	}, {
		.frmsize			= {640, 480},
		.active_frmsize		= {1296, 972},
		.active_fps			= 30,
		.size_type			= SIZE_640X480,
		.reg_script[0]		= AR0543_preview_VGA_script,
		.reg_script[1]		= AR0543_960P_script_mipi,
	}, {
		.frmsize			= {1280, 720},
		.active_frmsize		= {1280, 720},
		.active_fps			= 30,
		.size_type			= SIZE_1280X720,
		.reg_script[0]		= AR0543_preview_720P_script,
		.reg_script[1]		= AR0543_720P_script_mipi,
	}, {
		.frmsize			= {1280, 960},
		.active_frmsize		= {2592, 1944},
		.active_fps			= 15,
		.size_type			= SIZE_1280X960,
		.reg_script[0]		= AR0543_preview_960P_script,
		.reg_script[1]		= AR0543_5M_script_mipi,
	}, {
		.frmsize			= {1920, 1080},
		.active_frmsize		= {1920, 1080},
		.active_fps			= 30,
		.size_type			= SIZE_1920X1080,
		.reg_script[0]		= AR0543_preview_1080P_script,
		.reg_script[1]		= AR0543_1080P_script_mipi,
	},{
		.frmsize			= {2592, 1944},
		.active_frmsize		= {2592, 1944},
		.active_fps			= 15,
		.size_type			= SIZE_2592X1944,
		.reg_script[0]		= AR0543_capture_5M_script,
		.reg_script[1]		= AR0543_5M_script_mipi,
	},
};	

static resolution_param_t  capture_resolution_array[] = {
	{
		.frmsize			= {2592, 1944},
		.active_frmsize		= {2592, 1944},
		.active_fps			= 15,
		.size_type			= SIZE_2592X1944,
		.reg_script[0]			= AR0543_capture_5M_script,
		.reg_script[1]			= AR0543_5M_script_mipi,
       },{
               .frmsize                        = {2048, 1536},
               .active_frmsize         = {2592, 1944},
               .active_fps                     = 15,
               .size_type                      = SIZE_2048X1536,
               .reg_script[0]                  = AR0543_capture_5M_script,
               .reg_script[1]                  = AR0543_5M_script_mipi,
       },{
               .frmsize                        = {1600, 1200},
               .active_frmsize         = {2592, 1944},
               .active_fps                     = 15,
               .size_type                      = SIZE_1600X1200,
               .reg_script[0]                  = AR0543_capture_5M_script,
               .reg_script[1]                  = AR0543_5M_script_mipi,

        },
};

static char *vstrdup(const char *buf)
{
	char * buf_orig = NULL;
	int n = strlen(buf)+1;
	buf_orig = vmalloc(n);
	if(buf_orig){
		memset(buf_orig, 0, n);
		memcpy(buf_orig, buf, n-1);
	}
	return buf_orig;
}

static void parse_param(const char *buf,char **parm){
	char *buf_orig, *ps, *token;
	unsigned int n=0;

	//buf_orig = kstrdup(buf, GFP_KERNEL);
	buf_orig = vstrdup(buf);
	ps = buf_orig;
	n=0;
	while(1) {
	        token = strsep(&ps, " \n");
	        if (token == NULL)
	                break;
	        if (*token == '\0')
	                continue;
	        parm[n++] = token;
	        printk("%s\n",parm[n-1]);
	}
	vfree(buf_orig);
	//kfree(buf_orig);
}

extern int aml_i2c_put_word(struct i2c_adapter *adapter, 
		unsigned short dev_addr, unsigned short addr, unsigned short data);

void AR0543_manual_set_aet(unsigned int exp,unsigned int ag,unsigned int vts){
	//unsigned char exp_h = 0, exp_m = 0, exp_l = 0, ag_h = 0, ag_l = 0, vts_h = 0, vts_l = 0;
	struct i2c_adapter *adapter;
	adapter = i2c_get_adapter(4);
	
	aml_i2c_put_word(adapter, 0x36, 0x3012, exp & 0xffff);
	
	aml_i2c_put_word(adapter, 0x36, 0x0204, ag & 0xffff);
	
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
	
	AR0543_manual_set_aet(exp,ag,vts);
	return len;
}

static ssize_t aet_manual_show(struct class *cls,struct class_attribute *attr, char* buf)
{
	size_t len = 0;
	return len;
}

static CLASS_ATTR(aet_debug, 0664, aet_manual_show, aet_manual_store);

/* ar0543 uses exp+ag mode */
static bool AR0543_set_aet_new_step(void *priv,unsigned int new_step, bool exp_mode, bool ag_mode){
  	unsigned int exp = 0, ag = 0, vts = 0;
	camera_priv_data_t *camera_priv_data = (camera_priv_data_t *)priv; 
	sensor_aet_t *sensor_aet_table = camera_priv_data->sensor_aet_table;
	sensor_aet_info_t *sensor_aet_info = camera_priv_data->sensor_aet_info;
	
	if(camera_priv_data == NULL || sensor_aet_table == NULL || sensor_aet_info == NULL)
		return false;	
  	if (((!exp_mode) && (!ag_mode))  || (new_step > sensor_aet_info[aet_index].tbl_max_step))
		return(false);
	else
	{
		camera_priv_data->sensor_aet_step = new_step;
		exp = sensor_aet_table[camera_priv_data->sensor_aet_step].exp;
		ag = sensor_aet_table[camera_priv_data->sensor_aet_step].ag;
		vts = sensor_aet_table[camera_priv_data->sensor_aet_step].vts;

		
		AR0543_manual_set_aet(exp,ag,vts);
		return true;
	}
}


static bool AR0543_check_mains_freq(void *priv){// when the fr change,we need to change the aet table
    //int detection; 
    //struct i2c_adapter *adapter;
    return true;
}

bool AR0543_set_af_new_step(void *priv,unsigned int af_step){
    struct i2c_adapter *adapter;
    char buf[3];
    unsigned int diff,vcm_data = 0,codes;
    if(af_step == last_af_step)
        return true;
    if(vcm_mod == 0){
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



void AR0543_set_new_format(void *priv,int width,int height,int fr){
    int index = 0;
    camera_priv_data_t *camera_priv_data;
    configure_t *configure;
    
    current_fr = fr;
    
    camera_priv_data = (camera_priv_data_t *)priv;
    configure = camera_priv_data->configure;
    
    if(camera_priv_data == NULL)
    	return;
    printk("sum:%d,mode:%d,fr:%d\n",configure->aet.sum,ar0543_work_mode,fr);
    while(index < configure->aet.sum){
        if(width == configure->aet.aet[index].info->fmt_hactive && height == configure->aet.aet[index].info->fmt_vactive \
                && fr == configure->aet.aet[index].info->fmt_main_fr && ar0543_work_mode == configure->aet.aet[index].info->fmt_capture){
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



void AR0543_ae_manual_set(char **param){
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
	
	AR0543_manual_set_aet(g_ae_manual_exp,g_ae_manual_ag,g_ae_manual_vts);
}           

static ssize_t ae_manual_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	char *param[3] = {NULL};
	parse_param(buf,&param[0]);
	AR0543_ae_manual_set(&param[0]);
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

static void power_down_ar0543(struct ar0543_device *dev)
{
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//i2c_put_byte(client,0x0104, 0x00);
	//i2c_put_byte(client,0x0100, 0x00);
}

static ssize_t vcm_manual_store(struct class *cls,struct class_attribute *attr, const char* buf, size_t len)
{
	struct i2c_adapter *adapter;
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


void AR0543_init_regs(struct ar0543_device *dev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int i=0;
	
	while (1) {
		if (AR0543_init_script[i].type == END_OF_SCRIPT) {
			printk("success in initial AR0543.\n");
			break;
		}
		if ((cam_i2c_send_msg(client, AR0543_init_script[i])) < 0) {
			printk("fail in initial AR0543. \n");
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
	int vcm_data;
	adapter = i2c_get_adapter(4);
	
	// set the step to 100
	vcm_data = 100 << 4 | 1 << 2;
	buf[0] = vcm_data >> 8 & 0xff;
	buf[1] = vcm_data & 0xff;
	my_i2c_put_byte_add8(adapter,0x0c,buf,2);
	
	msleep(10);
	// close the vcm
	buf[0] = buf[0] | 0x80;
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
*    AR0543_set_param_wb
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

void AR0543_set_param_wb(struct ar0543_device *dev,enum  camera_wb_flip_e para)//white balance
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
        printk("not support this wb\n");
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

} /* AR0543_set_param_wb */
/*************************************************************************
 * FUNCTION
 *    AR0543_set_param_exposure
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
void AR0543_set_param_exposure(struct ar0543_device *dev,enum camera_exposure_e para)
{
    //struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    int value;

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

    dev->fe_arg.port = TVIN_PORT_ISP;
    dev->fe_arg.index = 0;
    dev->fe_arg.arg = (void *)(dev->cam_para);
    dev->vops->tvin_fe_func(0,&dev->fe_arg);	

} /* ar0543_set_param_exposure */
/*************************************************************************
 * FUNCTION
 *    AR0543_set_param_effect
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

void AR0543_set_param_effect(struct ar0543_device *dev,enum camera_special_effect_e para)
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

} /* AR0543_set_param_effect */

/*************************************************************************
 * FUNCTION
 *    AR0543_NightMode
 *
 * DESCRIPTION
 *    This function night mode of AR0543.
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
void AR0543_set_night_mode(struct ar0543_device *dev,enum  camera_night_mode_flip_e enable)
{
    //struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    if (enable) {
        
    }
    else{
       
    }

}   /* AR0543_NightMode */

static void AR0543_set_param_banding(struct ar0543_device *dev, enum  camera_banding_flip_e banding)
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


static int AR0543_AutoFocus(struct ar0543_device *dev, int focus_mode)
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

static int set_flip(struct ar0543_device *dev)
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
    if (width * height >= 2500 * 1900)
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

static int AR0543_FlashCtrl(struct ar0543_device *dev, int flash_mode)
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

static resolution_param_t* get_resolution_param(struct ar0543_device *dev, int ar0543_work_mode, int width, int height)
{
    int i = 0;
    int arry_size = 0;
    resolution_param_t* tmp_resolution_param = NULL;
    resolution_size_t res_type = SIZE_NULL;
    printk("target resolution is %dX%d\n", width, height);
    res_type = get_size_type(width, height);
    if (res_type == SIZE_NULL)
        return NULL;
    if (ar0543_work_mode == CAMERA_CAPTURE) {
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
	"5m",
};

static void set_resolution_param(struct ar0543_device *dev, resolution_param_t* res_param)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//int rc = -1;
	int i=0;
	unsigned char t = 1;//dev->cam_info.interface;
	printk("%s, %d, interface =%d\n" , __func__, __LINE__, t);
	

	if(i_index != -1 && ar0543_work_mode != CAMERA_CAPTURE){
        res_param = &debug_prev_resolution_array[i_index];
    }
	if (!res_param->reg_script[t]) {
		printk("error, resolution reg script is NULL\n");
		return;
	}
	while(1) {
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
	#if 0
    int default_sensor_data[4] = {0x00000100,0x000001a0,0x000001ff,0x00000108};
	int *sensor_data;
	int data = 0;
	int index = 0;
	if(cf->wb_sensor_data_valid == 1){
		sensor_data = cf->wb_sensor_data.export;
	}else
		sensor_data = default_sensor_data;
		
	i2c_put_word(client, 0x206, sensor_data[0] & 0xffff);
	i2c_put_word(client, 0x208, sensor_data[1] & 0xffff);
	i2c_put_word(client, 0x20a, sensor_data[2] & 0xffff);
	i2c_put_word(client, 0x20c, sensor_data[3] & 0xffff);
	#endif
	ar0543_frmintervals_active.numerator = 1;
	ar0543_frmintervals_active.denominator = res_param->active_fps;
	ar0543_h_active = res_param->active_frmsize.width;
	ar0543_v_active = res_param->active_frmsize.height;
	AR0543_set_new_format((void *)&dev->camera_priv_data,ar0543_h_active,ar0543_v_active,current_fr);// should set new para
	
}    /* AR0543_set_resolution */

static int set_focus_zone(struct ar0543_device *dev, int value)
{
	int xc, yc, tx, ty;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	//int retry_count = 10;
	//int ret = -1;
	
	xc = (value >> 16) & 0xffff;
	yc = (value & 0xffff);
	if(xc == 1000 && yc == 1000)
		return 0;
	tx = xc * ar0543_h_active /2000;
	ty = yc * ar0543_v_active /2000;
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

unsigned char v4l_2_ar0543(int val)
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

static int ar0543_setting(struct ar0543_device *dev,int PROP_ID,int value )
{
	int ret=0;
	//struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	switch(PROP_ID)  {
	case V4L2_CID_BRIGHTNESS:
		dprintk(dev, 1, "setting brightned:%d\n",v4l_2_ar0543(value));
		//ret=i2c_put_byte(client,0x0201,v4l_2_ar0543(value));
		break;
	case V4L2_CID_CONTRAST:
		break;
	case V4L2_CID_SATURATION:
		break;
	case V4L2_CID_HFLIP:    /* set flip on H. */
		value = value & 0x1;
		if(ar0543_qctrl[2].default_value!=value){
			 ar0543_qctrl[2].default_value=value;
			printk(" set camera  h filp =%d. \n ",value);
			value = value << 1; //bit[1]
			//ret=i2c_put_byte(client,0x3821, value);
			break;
		}
		break;
	case V4L2_CID_VFLIP:    /* set flip on V. */
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		if(ar0543_qctrl[4].default_value!=value){
			ar0543_qctrl[4].default_value=value;
			if(dev->stream_on)
				AR0543_set_param_wb(dev,value);
			printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
		}
		break;
	case V4L2_CID_EXPOSURE:
		if(ar0543_qctrl[5].default_value!=value){
			ar0543_qctrl[5].default_value=value;
			if(dev->stream_on)
				AR0543_set_param_exposure(dev,value);
			printk(KERN_INFO " set camera  exposure=%d. \n ",value);
		}
		break;
	case V4L2_CID_BACKLIGHT_COMPENSATION:
		if(ar0543_qctrl[9].default_value!=value){
			ar0543_qctrl[9].default_value=value;
			ret = AR0543_FlashCtrl(dev,value);
			printk(KERN_INFO " set light compensation =%d. \n ",value);
		}
		break;
	case V4L2_CID_COLORFX:
		if(ar0543_qctrl[6].default_value!=value){
			ar0543_qctrl[6].default_value=value;
			if(dev->stream_on)
				AR0543_set_param_effect(dev,value);
		}
		break;
	
	case V4L2_CID_POWER_LINE_FREQUENCY:
		if(ar0543_qctrl[3].default_value!=value){
			ar0543_qctrl[3].default_value=value;
			AR0543_set_param_banding(dev,value);
			printk(KERN_INFO " set camera  banding=%d. \n ",value);
		}
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		if(ar0543_qctrl[10].default_value!=value){
			ar0543_qctrl[10].default_value=value;
			printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
		}
		break;
	case V4L2_CID_ROTATE:
		if(ar0543_qctrl[11].default_value!=value){
			ar0543_qctrl[11].default_value=value;
			printk(" set camera  rotate =%d. \n ",value);
		}
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		printk("V4L2_CID_FOCUS_ABSOLUTE\n");
		if(ar0543_qctrl[13].default_value!=value){
			ar0543_qctrl[13].default_value=value;
			printk(" set camera  focus zone =%d. \n ",value);
			if(dev->stream_on)
            	set_focus_zone(dev, value);
		}
		break;
	case V4L2_CID_FOCUS_AUTO:
		printk("V4L2_CID_FOCUS_AUTO\n");
		if(ar0543_qctrl[8].default_value!=value){
			ar0543_qctrl[8].default_value=value;
			if(dev->stream_on)
				AR0543_AutoFocus(dev,value);
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

static void ar0543_fillbuff(struct ar0543_fh *fh, struct ar0543_buffer *buf)
{
	struct ar0543_device *dev = fh->dev;
	//void *vbuf = videobuf_to_vmalloc(&buf->vb);
	void *vbuf = (void *)videobuf_to_res(&buf->vb);
	vm_output_para_t para = {0};
	dprintk(dev,1,"%s\n", __func__);
	if (!vbuf)
		return;
	/*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
	if(buf->canvas_id == 0)
		buf->canvas_id = convert_canvas_index(fh->fmt->fourcc, AR0543_RES0_CANVAS_INDEX+buf->vb.i*3);
	para.mirror = ar0543_qctrl[2].default_value&3;// not set
	para.v4l2_format = fh->fmt->fourcc;
	para.v4l2_memory = MAGIC_RE_MEM;//0x18221223;
	para.zoom = ar0543_qctrl[10].default_value;
	para.angle = ar0543_qctrl[11].default_value;
	para.vaddr = (unsigned)vbuf;
	para.ext_canvas = buf->canvas_id;
	para.width = buf->vb.width;
	para.height = (buf->vb.height==1080)?1088:buf->vb.height;
	vm_fill_buffer(&buf->vb,&para);
	buf->vb.state = VIDEOBUF_DONE;
}

static void ar0543_thread_tick(struct ar0543_fh *fh)
{
	struct ar0543_buffer *buf;
	struct ar0543_device *dev = fh->dev;
	struct ar0543_dmaqueue *dma_q = &dev->vidq;

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
            struct ar0543_buffer, vb.queue);
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
	ar0543_fillbuff(fh, buf);
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

static void ar0543_sleep(struct ar0543_fh *fh)
{
	struct ar0543_device *dev = fh->dev;
	struct ar0543_dmaqueue *dma_q = &dev->vidq;

	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	//timeout = msecs_to_jiffies(frames_to_ms(1));

	ar0543_thread_tick(fh);

	schedule_timeout_interruptible(1);//if fps > 25 , 2->1

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int ar0543_thread(void *data)
{
	struct ar0543_fh  *fh = data;
	struct ar0543_device *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		ar0543_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int ar0543_start_thread(struct ar0543_fh *fh)
{
	struct ar0543_device *dev = fh->dev;
	struct ar0543_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(ar0543_thread, fh, "ar0543");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void ar0543_stop_thread(struct ar0543_dmaqueue  *dma_q)
{
	struct ar0543_device *dev = container_of(dma_q, struct ar0543_device, vidq);

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
	struct ar0543_fh *fh  = container_of(res, struct ar0543_fh, res);
	struct ar0543_device *dev  = fh->dev;
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

static void free_buffer(struct videobuf_queue *vq, struct ar0543_buffer *buf)
{
	struct videobuf_res_privdata *res = vq->priv_data;
	struct ar0543_fh *fh  = container_of(res, struct ar0543_fh, res);
	struct ar0543_device *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

	videobuf_waiton(vq, &buf->vb, 0, 0);
	if (in_interrupt())
		BUG();

	videobuf_res_free(vq, &buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 3000
#define norm_maxh() 3000
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct videobuf_res_privdata *res = vq->priv_data;
	struct ar0543_fh *fh  = container_of(res, struct ar0543_fh, res);
	struct ar0543_device    *dev = fh->dev;
	struct ar0543_buffer *buf = container_of(vb, struct ar0543_buffer, vb);
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
	struct ar0543_buffer    *buf  = container_of(vb, struct ar0543_buffer, vb);
	struct videobuf_res_privdata *res = vq->priv_data;
	struct ar0543_fh *fh  = container_of(res, struct ar0543_fh, res);
	struct ar0543_device       *dev  = fh->dev;
	struct ar0543_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct ar0543_buffer   *buf  = container_of(vb, struct ar0543_buffer, vb);
	struct videobuf_res_privdata *res = vq->priv_data;
	struct ar0543_fh *fh  = container_of(res, struct ar0543_fh, res);
	struct ar0543_device      *dev  = (struct ar0543_device *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops ar0543_video_qops = {
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
	struct ar0543_fh  *fh  = priv;
	struct ar0543_device *dev = fh->dev;

	strcpy(cap->driver, "ar0543");
	strcpy(cap->card, "ar0543.canvas");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = AR0543_CAMERA_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct ar0543_fmt *fmt;

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

    if(fival->index > ARRAY_SIZE(ar0543_frmivalenum))
        return -EINVAL;

    for(k =0; k< ARRAY_SIZE(ar0543_frmivalenum); k++)
    {
        if( (fival->index==ar0543_frmivalenum[k].index)&&
                (fival->pixel_format ==ar0543_frmivalenum[k].pixel_format )&&
                (fival->width==ar0543_frmivalenum[k].width)&&
                (fival->height==ar0543_frmivalenum[k].height)){
            memcpy( fival, &ar0543_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
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
	struct ar0543_fh *fh = priv;

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
	struct ar0543_fh  *fh  = priv;
	struct ar0543_device *dev = fh->dev;
	struct ar0543_fmt *fmt;
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
	struct ar0543_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct ar0543_device *dev = fh->dev;
	int ret;
	resolution_param_t* res_param = NULL;

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
        ar0543_work_mode = CAMERA_CAPTURE;
    }
    else {
    	printk("preview resolution is %dX%d\n",fh->width,  fh->height);
        if (0 == capture_proc) {
        	ar0543_work_mode = CAMERA_RECORD;
        }else {
        	ar0543_work_mode = CAMERA_PREVIEW;
        }
        if (0 == dev->is_vdin_start) {
			printk("loading sensor setting\n");
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
    }
	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return ret;
}

static int vidioc_g_parm(struct file *file, void *priv,
        struct v4l2_streamparm *parms)
{
    struct ar0543_fh *fh = priv;
    struct ar0543_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = ar0543_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct ar0543_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ar0543_fh  *fh = priv;

	int ret = videobuf_querybuf(&fh->vb_vidq, p);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	if(ret == 0){
		p->reserved  = convert_canvas_index(fh->fmt->fourcc, AR0543_RES0_CANVAS_INDEX+p->index*3);
	}else{
		p->reserved = 0;
	}
#endif		
	return ret;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ar0543_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct ar0543_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct ar0543_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif
#ifdef MIPI_INTERFACE
/*static struct ar0543_fmt input_formats_vdin[] = 
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
static unsigned int settle = 24;
module_param(settle, uint, 0644);
MODULE_PARM_DESC(settle, "settle time info");

static unsigned int skip_count = 6;
module_param(skip_count, uint, 0644);
MODULE_PARM_DESC(skip_count, "activates skip_count info");

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct ar0543_fh  *fh = priv;
	struct ar0543_device *dev = fh->dev;	
	vdin_parm_t para;
	int ret = 0 ;
	
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

        if (dev->is_vdin_start) {
		printk("vidioc_streamon in capture process\n");
		ret =  videobuf_streamon(&fh->vb_vidq);
		if(ret == 0){
			dev->stream_on        = 1;
		}

		return 0;
	}
	memset( &para, 0, sizeof( para ));
	//para.port  = TVIN_PORT_CAMERA;
	
	if (CAM_MIPI == dev->cam_info.interface) {
	        para.isp_fe_port  = TVIN_PORT_MIPI;
	} else {
	        para.isp_fe_port  = TVIN_PORT_CAMERA;
	}
	para.port  = TVIN_PORT_ISP;   
	para.fmt = TVIN_SIG_FMT_MAX;
	para.frame_rate = ar0543_frmintervals_active.denominator;
	para.h_active = ar0543_h_active;
	para.v_active = ar0543_v_active;
    if(ar0543_work_mode != CAMERA_CAPTURE){
		para.skip_count = 2;
        para.dest_hactive = dest_hactive;
        para.dest_vactive = dest_vactive;
    }else{
        para.dest_hactive = 0;
        para.dest_vactive = 0;
    }
    dev->cam_para->cam_mode = ar0543_work_mode;
	para.hsync_phase = 1;
	para.vsync_phase  = 1;
	para.hs_bp = 0;
	para.vs_bp = 2;
	para.cfmt = dev->cam_info.bayer_fmt;
	//para.dfmt = TVIN_NV21;
	para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
	para.bt_path = dev->cam_info.bt_path;
	current_fmt = 0;
	if(dev->cam_para == NULL)
		return -EINVAL;
   	if(update_fmt_para(ar0543_h_active,ar0543_v_active,dev->cam_para,&dev->pindex,dev->configure) != 0)
   		return -EINVAL;
	para.reserved = (int)(dev->cam_para);	
	if (CAM_MIPI == dev->cam_info.interface)
	{
			printk("mipi param init\n");
	        para.csi_hw_info.lanes = 2;
	        para.csi_hw_info.channel = 1;
	        para.csi_hw_info.mode = 1;
	        para.csi_hw_info.clock_lane_mode = 1; // 0 clock gate 1: always on
	        para.csi_hw_info.active_pixel = ar0543_h_active;
	        para.csi_hw_info.active_line = ar0543_v_active;
	        para.csi_hw_info.frame_size=0;
	        para.csi_hw_info.ui_val = 2; //ns
	        para.csi_hw_info.urgent = 1;

	        para.csi_hw_info.settle = settle;
	        para.csi_hw_info.hs_freq = 410; //MHz
	        para.csi_hw_info.clk_channel = dev->cam_info.clk_channel; //clock channel a or b
	}
    if(dev->configure->aet_valid == 1){
        dev->cam_para->xml_scenes->ae.aet_fmt_gain = (dev->camera_priv_data).sensor_aet_info->format_transfer_parameter;        	
    }
    else
        dev->cam_para->xml_scenes->ae.aet_fmt_gain = 100;
	printk("aet_fmt_gain:%d\n",dev->cam_para->xml_scenes->ae.aet_fmt_gain);

	printk("ar0543,h=%d, v=%d, frame_rate=%d\n", 
	        ar0543_h_active, ar0543_v_active, ar0543_frmintervals_active.denominator);
	ret =  videobuf_streamon(&fh->vb_vidq);
	if(ret == 0){
		dev->vops->start_tvin_service(0,&para);
        dev->is_vdin_start      = 1;
		dev->stream_on        = 1;
	}
	dev->vdin_arg.cmd = VDIN_CMD_SET_CM2;
	dev->vdin_arg.cm2 = dev->configure->cm.export;
	dev->vops->tvin_vdin_func(0,&dev->vdin_arg);
	printk("call set cm2\n");
	AR0543_set_param_wb(fh->dev,ar0543_qctrl[4].default_value);
	AR0543_set_param_exposure(fh->dev,ar0543_qctrl[5].default_value);
	AR0543_set_param_effect(fh->dev,ar0543_qctrl[6].default_value);
    AR0543_AutoFocus(fh->dev, ar0543_qctrl[8].default_value);
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct ar0543_fh  *fh = priv;
	struct ar0543_device *dev = fh->dev;
	int ret = 0 ;
	printk(KERN_INFO " vidioc_streamoff+++ \n ");
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;
	ret = videobuf_streamoff(&fh->vb_vidq);
	if (capture_proc) {
		printk("vidioc_streamoff in capture process\n");
		dev->stream_on        = 0;
		return 0;
	}
	if(ret == 0 ){
    	dev->vops->stop_tvin_service(0);
 		dev->is_vdin_start      = 0;
		dev->stream_on        = 0;
	}
	dev->ae_on = false;
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
	printk("current hactive :%d, v_active :%d, dest_hactive :%d, dest_vactive:%d\n",ar0543_h_active,ar0543_v_active,dest_hactive,dest_vactive);
	return len;
}
static CLASS_ATTR(resolution_debug, 0664, manual_format_show,manual_format_store);

static int vidioc_enum_framesizes(struct file *file, void *fh,struct v4l2_frmsizeenum *fsize)
{

      int ret = 0,i=0;
      struct ar0543_fmt *fmt = NULL;
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
              printk("ar0543_prev_resolution[fsize->index]"
                              "   before fsize->index== %d\n",fsize->index);//potti
              if (fsize->index >= ARRAY_SIZE(prev_resolution_array))
                      return -EINVAL;
              frmsize = &prev_resolution_array[fsize->index].frmsize;
              //printk("ar0543_prev_resolution[fsize->index]"
              //                "   after fsize->index== %d, wxh=%5dx%5d\n",
              //                fsize->index, frmsize->width, frmsize->height);
              fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
              fsize->discrete.width = frmsize->width;
              fsize->discrete.height = frmsize->height;
      } else if (fmt->fourcc == V4L2_PIX_FMT_RGB24){
              printk("ar0543_pic_resolution[fsize->index]"
                              "   before fsize->index== %d\n",fsize->index);
              if (fsize->index >= ARRAY_SIZE(capture_resolution_array))
                      return -EINVAL;
              frmsize = &capture_resolution_array[fsize->index].frmsize;
              printk("ar0543_pic_resolution[fsize->index]"
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
	struct ar0543_fh *fh = priv;
	struct ar0543_device *dev = fh->dev;

	*i = dev->input;
	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct ar0543_fh *fh = priv;
	struct ar0543_device *dev = fh->dev;

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

	for (i = 0; i < ARRAY_SIZE(ar0543_qctrl); i++)
		if (qc->id && qc->id == ar0543_qctrl[i].id) {
			memcpy(qc, &(ar0543_qctrl[i]),sizeof(*qc));
			return (0);
		}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct ar0543_fh *fh = priv;
	struct ar0543_device *dev = fh->dev;
	int i, status;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(ar0543_qctrl); i++)
		if (ctrl->id == ar0543_qctrl[i].id) {
		#if 1
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
	struct ar0543_fh *fh = priv;
	struct ar0543_device *dev = fh->dev;
	int i;
	for (i = 0; i < ARRAY_SIZE(ar0543_qctrl); i++)
		if (ctrl->id == ar0543_qctrl[i].id) {
			if (ctrl->value < ar0543_qctrl[i].minimum ||
			    ctrl->value > ar0543_qctrl[i].maximum ||
			    ar0543_setting(dev,ctrl->id,ctrl->value)<0) {
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

static int ar0543_open(struct file *file)
{
    struct ar0543_device *dev = video_drvdata(file);
    struct ar0543_fh *fh = NULL;
    resource_size_t mem_start = 0;
    unsigned int mem_size = 0;
    int retval = 0;
    capture_proc = 0;
#ifdef CONFIG_CMA
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
                printk("parse successfully, pointer:%p\n", dev->configure);
            }else{
                printk("parse failed\n");
                return -EINVAL;
            }
        }else{
            printk("malloc failed");
            return -ENOMEM;
        }      
    }
 
		if((dev->cam_para = vmalloc(sizeof(cam_parameter_t))) == NULL){
			printk("memalloc failed\n");
			return -ENOMEM;
		}
		if(generate_para(dev->cam_para,dev->pindex,dev->configure) != 0){
			printk("generate para failed\n");
			free_para(dev->cam_para);
			vfree(dev->cam_para);
			return -EINVAL;
		}
	
    dev->cam_para->cam_function.set_aet_new_step = AR0543_set_aet_new_step;
    dev->cam_para->cam_function.check_mains_freq = AR0543_check_mains_freq;
    dev->cam_para->cam_function.set_af_new_step = AR0543_set_af_new_step;
    dev->camera_priv_data.configure = dev->configure;
    dev->cam_para->cam_function.priv_data = (void *)&dev->camera_priv_data;  

    dev->ae_on = false;
    AR0543_init_regs(dev);
    msleep(40);
    dw9714_init(vcm_mod);
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
    videobuf_queue_res_init(&fh->vb_vidq, &ar0543_video_qops,
    			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
    			sizeof(struct ar0543_buffer), (void*)&fh->res, NULL);
    ar0543_start_thread(fh);
    ar0543_have_open = 1;
    ar0543_work_mode = CAMERA_PREVIEW;
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
    dev->is_vdin_start = 0;
    return 0;
}

static ssize_t
ar0543_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct ar0543_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
ar0543_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ar0543_fh        *fh = file->private_data;
	struct ar0543_device       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int ar0543_close(struct file *file)
{
    struct ar0543_fh         *fh = file->private_data;
    struct ar0543_device *dev       = fh->dev;
    struct ar0543_dmaqueue *vidq = &dev->vidq;
    struct video_device  *vdev = video_devdata(file);
    int i=0;
    ar0543_have_open = 0;
    capture_proc = 0;
    ar0543_stop_thread(vidq);
    videobuf_stop(&fh->vb_vidq);
    if (dev->is_vdin_start) {
        dev->vops->stop_tvin_service(0);
        dev->is_vdin_start = 0;
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

    dprintk(dev, 1, "close called (dev=%s, users=%d)\n",
            video_device_node_name(vdev), dev->users);
    //ar0543_h_active=800;
    //ar0543_v_active=600;
    ar0543_qctrl[0].default_value=0;
    ar0543_qctrl[1].default_value=4;
    ar0543_qctrl[2].default_value=0;
    ar0543_qctrl[3].default_value=CAM_BANDING_50HZ;
    ar0543_qctrl[4].default_value=CAM_WB_AUTO;

    ar0543_qctrl[5].default_value=4;
    ar0543_qctrl[6].default_value=0;
    ar0543_qctrl[10].default_value=100;
    ar0543_qctrl[11].default_value=0;
    dev->ae_on = false;
    //ar0543_frmintervals_active.numerator = 1;
    //ar0543_frmintervals_active.denominator = 15;
    power_down_ar0543(dev);
    dw9714_uninit();

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
    printk("%s close success\n", __func__);
#ifdef CONFIG_CMA
    vm_deinit_buf();
#endif
    return 0;
}

static int ar0543_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ar0543_fh  *fh = file->private_data;
	struct ar0543_device *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations ar0543_fops = {
	.owner		= THIS_MODULE,
	.open           = ar0543_open,
	.release        = ar0543_close,
	.read           = ar0543_read,
	.poll		= ar0543_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = ar0543_mmap,
};

static const struct v4l2_ioctl_ops ar0543_ioctl_ops = {
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
    .vidioc_s_crop        = vidioc_s_crop,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
};

static struct video_device ar0543_template = {
	.name		= "ar0543_v4l",
	.fops           = &ar0543_fops,
	.ioctl_ops 	= &ar0543_ioctl_ops,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

static int ar0543_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_AR0543, 0);
}

static const struct v4l2_subdev_core_ops ar0543_core_ops = {
	.g_chip_ident = ar0543_g_chip_ident,
};

static const struct v4l2_subdev_ops ar0543_ops = {
	.core = &ar0543_core_ops,
};

static ssize_t cam_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct ar0543_device *t;
	
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

	struct ar0543_device *t;
	unsigned char n=0;
	//unsigned char ret=0;
	char *buf_orig, *ps, *token;
	char *parm[3] = {NULL};
	
	if(!buf)
		return len;
	//buf_orig = kstrdup(buf, GFP_KERNEL);
	buf_orig = vstrdup(buf);
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
	
	vfree(buf_orig);
	
	return len;

}

static DEVICE_ATTR(cam_info, 0664, cam_info_show, cam_info_store);
static int ar0543_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	struct ar0543_device *t;
	struct v4l2_subdev *sd;
	aml_cam_info_t* plat_dat;
	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &ar0543_ops);

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
	memcpy(t->vdev, &ar0543_template, sizeof(*t->vdev));

	video_set_drvdata(t->vdev, t);
	
	wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "ar0543");
	/* Register it */
	if (plat_dat) {
		memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
		if(plat_dat->front_back>=0)  
			video_nr=plat_dat->front_back;
	}else {
		printk("camera ar0543: have no platform data\n");
		kfree(t);
		kfree(client);
		return -1;
	}
	
	t->cam_info.version = AR0543_DRIVER_VERSION;
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

static int ar0543_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0543_device *t = to_dev(sd);
	device_remove_file( &t->vdev->dev, &dev_attr_cam_info);
	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	wake_lock_destroy(&(t->wake_lock));
	aml_cam_info_unreg(&t->cam_info);
	kfree(t);
	return 0;
}

static const struct i2c_device_id ar0543_id[] = {
	{ "ar0543", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ar0543_id);

static struct i2c_driver ar0543_i2c_driver = {
	.driver = {
		.name = "ar0543",
	},
	.probe = ar0543_probe,
	.remove = ar0543_remove,
	.id_table = ar0543_id,
};

module_i2c_driver(ar0543_i2c_driver);

