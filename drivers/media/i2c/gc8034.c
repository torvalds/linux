// SPDX-License-Identifier: GPL-2.0
/*
 * gc8034 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04 add quick stream on/off
 * V0.0X01.0X05 add function g_mbus_config
 * V0.0X01.0X06
 * 1. add 2lane support.
 * 2. add some debug info.
 * 3. adjust gc8034_g_mbus_config function.
 * V0.0X01.0X07 support get channel info
 * V0.0X01.0X08
 * 1. default support 2lane full 30fps.
 * 2. default support rk otp spec.
 * V0.0X01.0X09 adjust supply sequence to suit spec
 */
//#define DEBUG
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <linux/of_graph.h>
#include "otp_eeprom.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x09)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define GC8034_LANES			4
#define GC8034_BITS_PER_SAMPLE		10
#define GC8034_MIPI_FREQ_336MHZ		336000000U
#define GC8034_MIPI_FREQ_634MHZ		634000000U

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define GC8034_PIXEL_RATE		288000000
#define GC8034_XVCLK_FREQ		24000000

#define CHIP_ID				0x8044
#define GC8034_REG_CHIP_ID_H		0xf0
#define GC8034_REG_CHIP_ID_L		0xf1

#define GC8034_REG_SET_PAGE		0xfe
#define GC8034_SET_PAGE_ZERO		0x00

#define GC8034_REG_CTRL_MODE		0x3f
#define GC8034_MODE_SW_STANDBY		0x00
#define GC8034_MODE_STREAMING		0xd0

#define GC8034_REG_EXPOSURE_H		0x03
#define GC8034_REG_EXPOSURE_L		0x04
#define GC8034_FETCH_HIGH_BYTE_EXP(VAL) (((VAL) >> 8) & 0x7F)	/* 4 Bits */
#define GC8034_FETCH_LOW_BYTE_EXP(VAL)	((VAL) & 0xFF)	/* 8 Bits */
#define	GC8034_EXPOSURE_MIN		4
#define	GC8034_EXPOSURE_STEP		1
#define GC8034_VTS_MAX			0x1fff

#define GC8034_REG_AGAIN		0xb6
#define GC8034_REG_DGAIN_INT		0xb1
#define GC8034_REG_DGAIN_FRAC		0xb2
#define GC8034_GAIN_MIN			64
#define GC8034_GAIN_MAX			1092
#define GC8034_GAIN_STEP		1
#define GC8034_GAIN_DEFAULT		64

#define GC8034_REG_VTS_H		0x07
#define GC8034_REG_VTS_L		0x08

#define REG_NULL			0xFF

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define GC8034_NAME			"gc8034"
#define GC8034_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SRGGB10_1X10

/* use RK_OTP or old mode */
#define RK_OTP
/* choose 2lane support full 30fps or 15fps */
#define GC8034_2LANE_30FPS

static const char * const gc8034_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
	"avdd",		/* Analog power */
};

#define GC8034_NUM_SUPPLIES ARRAY_SIZE(gc8034_supply_names)

#ifndef RK_OTP
struct gc8034_dd {
	u16 x;
	u16 y;
	u16 t;
};

struct gc8034_otp_info {
	int flag; //bit[7]: info bit[6]:wb bit[5]:vcm bit[4]:lenc
		//bit[3] dd bit[2] chip version
	u32 module_id;
	u32 lens_id;
	u32 year;
	u32 month;
	u32 day;
	u32 rg_ratio;
	u32 bg_ratio;
	u32 golden_rg;
	u32 golden_bg;
	u8 lsc[396];
	u32 vcm_start;
	u32 vcm_end;
	u32 vcm_dir;
	u32 dd_cnt;
	struct gc8034_dd dd_param[160];
	u16 reg_page[5];
	u16 reg_addr[5];
	u16 reg_value[5];
	u16 reg_num;
};

struct gc8034_id_name {
	u32 id;
	char name[RKMODULE_NAME_LEN];
};

static const struct gc8034_id_name gc8034_module_info[] = {
	{0x0d, "CameraKing"},
	{0x00, "Unknown"}
};

static const struct gc8034_id_name gc8034_lens_info[] = {
	{0xd0, "CK8401"},
	{0x00, "Unknown"}
};
#endif

struct regval {
	u8 addr;
	u8 val;
};

struct gc8034_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 mipi_freq_idx;
	const struct regval *global_reg_list;
	const struct regval *reg_list;
	u32 vc[PAD_MAX];
};

struct gc8034 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[GC8034_NUM_SUPPLIES];
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;
	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	bool			streaming;
	unsigned int		lane_num;
	unsigned int		cfg_num;
	unsigned int		pixel_rate;
	bool			power_on;
	const struct gc8034_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32 Dgain_ratio;
#ifdef RK_OTP
	struct otp_info		*otp;
#else
	struct gc8034_otp_info *otp;
#endif
	struct rkmodule_inf	module_inf;
	struct rkmodule_awb_cfg	awb_cfg;
};

#define to_gc8034(sd) container_of(sd, struct gc8034, subdev)

#undef GC8034_MIRROR_NORMAL
#undef GC8034_MIRROR_H
#undef GC8034_MIRROR_V
#undef GC8034_MIRROR_HV
/* If you use the otp function, keep the otp_drv ->
 * gc8034_common_otp_drv.h consistent.
 */
#define GC8034_MIRROR_NORMAL

#if defined(GC8034_MIRROR_NORMAL)
	#define GC8034_MIRROR	0xc0
	#define BINNING_STARTY	0x04
	#define BINNING_STARTX	0x05
	#define FULL_STARTY	0x08
	#define FULL_STARTX	0x09
#elif defined(GC8034_MIRROR_H)
	#define GC8034_MIRROR	0xc1
	#define BINNING_STARTY	0x04
	#define BINNING_STARTX	0x04
	#define FULL_STARTY	0x08
	#define FULL_STARTX	0x08
#elif defined(GC8034_MIRROR_V)
	#define GC8034_MIRROR	0xc2
	#define BINNING_STARTY	0x05
	#define BINNING_STARTX	0x05
	#define FULL_STARTY	0x09
	#define FULL_STARTX	0x09
#elif defined(GC8034_MIRROR_HV)
	#define GC8034_MIRROR	0xc3
	#define BINNING_STARTY	0x05
	#define BINNING_STARTX	0x04
	#define FULL_STARTY	0x09
	#define FULL_STARTX	0x08
#else
	#define GC8034_MIRROR	0xc0
	#define BINNING_STARTY	0x04
	#define BINNING_STARTX	0x05
	#define FULL_STARTY	0x08
	#define FULL_STARTX	0x09
#endif

/*
 * Xclk 24Mhz
 */
static const struct regval gc8034_global_regs_2lane[] = {
#ifdef GC8034_2LANE_30FPS
	/* SYS */
	{0xf2, 0x00},
	{0xf4, 0x90},
	{0xf5, 0x3d},
	{0xf6, 0x44},
	{0xf8, 0x63},
	{0xfa, 0x42},
	{0xf9, 0x00},
	{0xf7, 0x95},
	{0xfc, 0x00},
	{0xfc, 0x00},
	{0xfc, 0xea},
	{0xfe, 0x03},
	{0x03, 0x9a},
	{0xfc, 0xee},
	{0xfe, 0x00},
	{0x88, 0x03},

	/*Cisctl&Analog*/
	{0xfe, 0x00},
	{0x03, 0x08},
	{0x04, 0xc6},
	{0x05, 0x02},
	{0x06, 0x16},
	{0x07, 0x00},
	{0x08, 0x10},
	{0x0a, 0x3a}, //row start
	{0x0b, 0x00},
	{0x0c, 0x04}, //col start
	{0x0d, 0x09},
	{0x0e, 0xa0}, //win_height 2464
	{0x0f, 0x0c},
	{0x10, 0xd4}, //win_width 3284
	{0x17, GC8034_MIRROR},
	{0x18, 0x02},
	{0x19, 0x17},
	{0x1e, 0x50},
	{0x1f, 0x80},
	{0x21, 0x4c},
	{0x25, 0x00},
	{0x28, 0x4a},
	{0x2d, 0x89},
	{0xca, 0x02},
	{0xcb, 0x00},
	{0xcc, 0x39},
	{0xce, 0xd0},
	{0xcf, 0x93},
	{0xd0, 0x1b},
	{0xd1, 0xaa},
	{0xd2, 0xcb},
	{0xd8, 0x40},
	{0xd9, 0xff},
	{0xda, 0x0e},
	{0xdb, 0xb0},
	{0xdc, 0x0e},
	{0xde, 0x08},
	{0xe4, 0xc6},
	{0xe5, 0x08},
	{0xe6, 0x10},
	{0xed, 0x2a},
	{0xfe, 0x02},
	{0x59, 0x02},
	{0x5a, 0x04},
	{0x5b, 0x08},
	{0x5c, 0x20},
	{0xfe, 0x00},
	{0x1a, 0x09},
	{0x1d, 0x13},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfe, 0x10},
	{0xfe, 0x00},

	/* Gamma */
	{0xfe, 0x00},
	{0x20, 0x54},
	{0x33, 0x82},
	{0xfe, 0x01},
	{0xdf, 0x06},
	{0xe7, 0x18},
	{0xe8, 0x20},
	{0xe9, 0x16},
	{0xea, 0x17},
	{0xeb, 0x50},
	{0xec, 0x6c},
	{0xed, 0x9b},
	{0xee, 0xd8},

	/*ISP*/
	{0xfe, 0x00},
	{0x80, 0x13},
	{0x84, 0x01},
	{0x89, 0x03},
	{0x8d, 0x03},
	{0x8f, 0x14},
	{0xad, 0x00},
	{0x66, 0x0c},
	{0xbc, 0x09},
	{0xc2, 0x7f},
	{0xc3, 0xff},

	/*Crop window*/
	{0x90, 0x01},
	{0x92, FULL_STARTY},
	{0x94, FULL_STARTX},
	{0x95, 0x09},
	{0x96, 0x90},
	{0x97, 0x0c},
	{0x98, 0xc0},

	/*Gain*/
	{0xb0, 0x90},
	{0xb1, 0x01},
	{0xb2, 0x00},
	{0xb6, 0x00},

	/*BLK*/
	{0xfe, 0x00},
	{0x40, 0x22},
	{0x41, 0x20},
	{0x42, 0x02},
	{0x43, 0x08},
	{0x4e, 0x0f},
	{0x4f, 0xf0},
	{0x58, 0x80},
	{0x59, 0x80},
	{0x5a, 0x80},
	{0x5b, 0x80},
	{0x5c, 0x00},
	{0x5d, 0x00},
	{0x5e, 0x00},
	{0x5f, 0x00},
	{0x6b, 0x01},
	{0x6c, 0x00},
	{0x6d, 0x0c},

	/*WB offset*/
	{0xfe, 0x01},
	{0xbf, 0x40},

	/*Dark Sun*/
	{0xfe, 0x01},
	{0x68, 0x77},

	/*DPC*/
	{0xfe, 0x01},
	{0x60, 0x00},
	{0x61, 0x10},
	{0x62, 0x60},
	{0x63, 0x30},
	{0x64, 0x00},

	/* LSC */
	{0xfe, 0x01},
	{0xa8, 0x60},
	{0xa2, 0xd1},
	{0xc8, 0x57},
	{0xa1, 0xb8},
	{0xa3, 0x91},
	{0xc0, 0x50},
	{0xd0, 0x05},
	{0xd1, 0xb2},
	{0xd2, 0x1f},
	{0xd3, 0x00},
	{0xd4, 0x00},
	{0xd5, 0x00},
	{0xd6, 0x00},
	{0xd7, 0x00},
	{0xd8, 0x00},
	{0xd9, 0x00},
	{0xa4, 0x10},
	{0xa5, 0x20},
	{0xa6, 0x60},
	{0xa7, 0x80},
	{0xab, 0x18},
	{0xc7, 0xc0},

	/*ABB*/
	{0xfe, 0x01},
	{0x20, 0x02},
	{0x21, 0x02},
	{0x23, 0x42},

	/*MIPI*/
	{0xfe, 0x03},
	{0x01, 0x07},
	{0x02, 0x04},
	{0x04, 0x80},
	{0x11, 0x2b},
	{0x12, 0xf0}, //lwc 3264*5/4
	{0x13, 0x0f},
	{0x15, 0x10}, //LP
	{0x16, 0x29},
	{0x17, 0xff},
	{0x18, 0x01},
	{0x19, 0xaa},
	{0x1a, 0x02},
	{0x21, 0x0c},
	{0x22, 0x0e},
	{0x23, 0x45},
	{0x24, 0x01},
	{0x25, 0x1c},
	{0x26, 0x0b},
	{0x29, 0x0e},
	{0x2a, 0x1d},
	{0x2b, 0x0b},
	{0xfe, 0x00},
	//{0x3f, 0x91},
	{0x3f, 0x00},
#else
	/*SYS*/
	{0xf2, 0x00},
	{0xf4, 0x80},
	{0xf5, 0x19},
	{0xf6, 0x44},
	{0xf7, 0x95}, //pll enable
	{0xf8, 0x63}, //pll mode
	{0xf9, 0x00},
	{0xfa, 0x45},
	{0xfc, 0xfe},

	/*Cisctl&Analog*/
	{0xfe, 0x00},
	{0x03, 0x08},
	{0x04, 0xc6},
	{0x05, 0x02},
	{0x06, 0x16},
	{0x07, 0x00},
	{0x08, 0x10},
	{0x0a, 0x3a}, //row start
	{0x0b, 0x00},
	{0x0c, 0x04}, //col start
	{0x0d, 0x09},
	{0x0e, 0xa0}, //win_height 2464
	{0x0f, 0x0c},
	{0x10, 0xd4}, //win_width 3284
	{0x17, GC8034_MIRROR},
	{0x18, 0x02},
	{0x19, 0x17},
	{0x1e, 0x50},
	{0x1f, 0x80},
	{0x21, 0x4c},
	{0x25, 0x00},
	{0x28, 0x4a},
	{0x2d, 0x89},
	{0xca, 0x02},
	{0xcb, 0x00},
	{0xcc, 0x39},
	{0xce, 0xd0},
	{0xcf, 0x93},
	{0xd0, 0x1b},
	{0xd1, 0xaa},
	{0xd2, 0xcb},
	{0xd8, 0x40},
	{0xd9, 0xff},
	{0xda, 0x0e},
	{0xdb, 0xb0},
	{0xdc, 0x0e},
	{0xde, 0x08},
	{0xe4, 0xc6},
	{0xe5, 0x08},
	{0xe6, 0x10},
	{0xed, 0x2a},
	{0xfe, 0x02},
	{0x59, 0x02},
	{0x5a, 0x04},
	{0x5b, 0x08},
	{0x5c, 0x20},
	{0xfe, 0x00},
	{0x1a, 0x09},
	{0x1d, 0x13},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfe, 0x10},
	{0xfe, 0x00},

	/* Gamma */
	{0xfe, 0x00},
	{0x20, 0x54},
	{0x33, 0x82},
	{0xfe, 0x01},
	{0xdf, 0x06},
	{0xe7, 0x18},
	{0xe8, 0x20},
	{0xe9, 0x16},
	{0xea, 0x17},
	{0xeb, 0x50},
	{0xec, 0x6c},
	{0xed, 0x9b},
	{0xee, 0xd8},

	/*ISP*/
	{0xfe, 0x00},
	{0x80, 0x13},
	{0x84, 0x01},
	{0x89, 0x03},
	{0x8d, 0x03},
	{0x8f, 0x14},
	{0xad, 0x00},

	/*Crop window*/
	{0x90, 0x01},
	{0x92, FULL_STARTY},
	{0x94, FULL_STARTX},
	{0x95, 0x09},
	{0x96, 0x90},
	{0x97, 0x0c},
	{0x98, 0xc0},

	/*Gain*/
	{0xb0, 0x90},
	{0xb1, 0x01},
	{0xb2, 0x00},
	{0xb6, 0x00},

	/*BLK*/
	{0xfe, 0x00},
	{0x40, 0x22},
	{0x43, 0x03}, //add_offset
	{0x4e, 0x00}, //row_bits[15:8]
	{0x4f, 0x3c}, //row_bits[7:0]
	{0x58, 0x80}, //dark current ratio
	{0x59, 0x80},
	{0x5a, 0x80},
	{0x5b, 0x80},
	{0x5c, 0x00},
	{0x5d, 0x00},
	{0x5e, 0x00},
	{0x5f, 0x00},

	/*WB offset*/
	{0xfe, 0x01},
	{0xbf, 0x40},

	/*Dark Sun*/
	{0xfe, 0x01},
	{0x68, 0x77},

	/*DPC*/
	{0xfe, 0x01},
	{0x60, 0x15},
	{0x61, 0x10},
	{0x62, 0x60},
	{0x63, 0x48},
	{0x64, 0x02},

	/*LSC*/
	{0xfe, 0x01},
	{0xa0, 0x10}, //[6]segment_width[8], 0x[5:4]segment_height[9:8]
	{0xa8, 0x60}, //segment_height[7:0]
	{0xa2, 0xd1}, //height_ratio[7:0]
	{0xc8, 0x5b}, //[7:4]height_ratio[11:8]
	{0xa1, 0xb8}, //segment_width[7:0]
	{0xa3, 0x91}, //width_ratio[7:0]
	{0xc0, 0x50}, //[7:4]width_ratio[11:8]
	{0xd0, 0x05}, //segment_width_end[11:8]
	{0xd1, 0xb2}, //segment_width_end[7:0]
	{0xd2, 0x1f}, //col_segment
	{0xd3, 0x00}, //row_num_start[7:0]
	{0xd4, 0x00}, //[5:4]row_num_start[9:8] [3:0]col_seg_start
	{0xd5, 0x00}, //[7:2]col_num_start[7:2]
	{0xd6, 0x00}, //[2:0]col_num_start[10:8]
	{0xd7, 0x00}, //row_seg_start
	{0xd8, 0x00}, //col_cal_start[7:0]
	{0xd9, 0x00}, //[2:0]col_cal_start[10:8]

	/*ABB*/
	{0xfe, 0x01},
	{0x20, 0x02},
	{0x21, 0x02},
	{0x23, 0x43},

	/*MIPI*/
	{0xfe, 0x03},
	{0x01, 0x07},
	{0x02, 0x07},
	{0x03, 0x92},
	{0x04, 0x80},
	{0x11, 0x2b},
	{0x12, 0xf0}, //lwc 3264*5/4
	{0x13, 0x0f},
	{0x15, 0x10}, //LP
	{0x16, 0x29},
	{0x17, 0xff},
	{0x18, 0x01},
	{0x19, 0xaa},
	{0x1a, 0x02},
	{0x21, 0x05},
	{0x22, 0x05},
	{0x23, 0x16},
	{0x24, 0x00},
	{0x25, 0x12},
	{0x26, 0x07},
	{0x29, 0x07},
	{0x2a, 0x08},
	{0x2b, 0x07},
	{0xfe, 0x00},
	//{0x3f, 0x91},
	{0x3f, 0x00},
#endif
	{REG_NULL, 0x00},
};

#ifndef GC8034_2LANE_30FPS
/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 672Mbps
 */
static const struct regval gc8034_1632x1224_regs_2lane[] = {
	/*SYS*/
	{0xf2, 0x00},
	{0xf4, 0x80},
	{0xf5, 0x19},
	{0xf6, 0x44},
	{0xf8, 0x63},
	{0xfa, 0x45},
	{0xf9, 0x00},
	{0xf7, 0x95},
	{0xfc, 0x00},
	{0xfc, 0x00},
	{0xfc, 0xea},
	{0xfe, 0x03},
	{0x03, 0x9a},
	{0xfc, 0xee},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfe, 0x10},
	{0xfe, 0x00},

	/*ISP*/
	{0xfe, 0x00},
	{0x80, 0x10},
	{0xad, 0x30},
	{0x66, 0x2c},
	{0xbc, 0x49},

	/*Crop window*/
	{0x90, 0x01},
	{0x92, BINNING_STARTY}, //crop y
	{0x94, BINNING_STARTX}, //crop x
	{0x95, 0x04},
	{0x96, 0xc8},
	{0x97, 0x06},
	{0x98, 0x60},

	/*MIPI*/
	{0xfe, 0x03},
	{0x01, 0x07},
	{0x02, 0x03},
	{0x04, 0x80},
	{0x11, 0x2b},
	{0x12, 0xf8},
	{0x13, 0x07},
	{0x15, 0x10}, //LP mode
	{0x16, 0x29},
	{0x17, 0xff},
	{0x18, 0x01},
	{0x19, 0xaa},
	{0x1a, 0x02},
	{0x21, 0x05},
	{0x22, 0x06},
	{0x23, 0x16},
	{0x24, 0x00},
	{0x25, 0x12},
	{0x26, 0x07},
	{0x29, 0x07},
	{0x2a, 0x08},
	{0x2b, 0x07},
	{0xfe, 0x00},
	{0x3f, 0x00},

	{REG_NULL, 0x00},
};
#endif

/*
 * Xclk 24Mhz
 * max_framerate 15fps
 * mipi_datarate per lane 672Mbps
 */
static const struct regval gc8034_3264x2448_regs_2lane[] = {
#ifdef GC8034_2LANE_30FPS
	/* SYS */
	{0xf2, 0x00},
	{0xf4, 0x90},
	{0xf5, 0x3d},
	{0xf6, 0x44},
	{0xf8, 0x63},
	{0xfa, 0x42},
	{0xf9, 0x00},
	{0xf7, 0x95},
	{0xfc, 0x00},
	{0xfc, 0x00},
	{0xfc, 0xea},
	{0xfe, 0x03},
	{0x03, 0x9a},
	{0xfc, 0xee},
	{0xfe, 0x00},
	{0x3f, 0x00},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfe, 0x10},
	{0xfe, 0x00},

	/* ISP */
	{0xfe, 0x00},
	{0x80, 0x13},
	{0xad, 0x00},
	{0x66, 0x0c},
	{0xbc, 0x06},

	/* Crop window */
	{0x90, 0x01},
	{0x92, FULL_STARTY},
	{0x94, FULL_STARTX},
	{0x95, 0x09},
	{0x96, 0x90},
	{0x97, 0x0c},
	{0x98, 0xc0},

	/* MIPI */
	{0xfe, 0x03},
	{0x01, 0x07},
	{0x02, 0x04},
	{0x04, 0x80},
	{0x11, 0x2b},
	{0x12, 0xf0}, //lwc 3264*5/4
	{0x13, 0x0f},
	{0x15, 0x10}, //LP
	{0x16, 0x29},
	{0x17, 0xff},
	{0x18, 0x01},
	{0x19, 0xaa},
	{0x1a, 0x02},
	{0x21, 0x0c},
	{0x22, 0x0c},
	{0x23, 0x56},
	{0x24, 0x00},
	{0x25, 0x1c},
	{0x26, 0x0b},
	{0x29, 0x0e},
	{0x2a, 0x1d},
	{0x2b, 0x0b},
	{0xfe, 0x00},
	//{0x3f, 0x91},
	{0x3f, 0x00},
#else
	/*SYS*/
	{0xf2, 0x00},
	{0xf4, 0x80},
	{0xf5, 0x19},
	{0xf6, 0x44},
	{0xf7, 0x95}, //pll enable
	{0xf8, 0x63}, //pll mode
	{0xf9, 0x00},
	{0xfa, 0x45},
	{0xfc, 0x00},
	{0xfc, 0x00},
	{0xfc, 0xfe},

	/* ISP */
	{0xfe, 0x00},
	{0x80, 0x13},
	{0xad, 0x00},
	{0x66, 0x0c},
	{0xbc, 0x09},

	/* Crop window */
	{0x90, 0x01},
	{0x92, FULL_STARTY},
	{0x94, FULL_STARTX},
	{0x95, 0x09},
	{0x96, 0x90},
	{0x97, 0x0c},
	{0x98, 0xc0},

	/* MIPI */
	{0xfe, 0x03},
	{0x01, 0x07},
	{0x02, 0x03},
	{0x03, 0x92},
	{0x04, 0x80},
	{0x11, 0x2b},
	{0x12, 0xf0}, //lwc 3264*5/4
	{0x13, 0x0f},
	{0x15, 0x10}, //LP
	{0x16, 0x29},
	{0x17, 0xff},
	{0x18, 0x01},
	{0x19, 0xaa},
	{0x1a, 0x02},
	{0x21, 0x05},
	{0x22, 0x05},
	{0x23, 0x16},
	{0x24, 0x00},
	{0x25, 0x12},
	{0x26, 0x07},
	{0x29, 0x07},
	{0x2a, 0x08},
	{0x2b, 0x07},
	{0xfe, 0x00},
	//{0x3f, 0x91},
	{0x3f, 0x00},
#endif
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 */
static const struct regval gc8034_global_regs_4lane[] = {
	/*SYS*/
	{0xf2, 0x00},
	{0xf4, 0x80},
	{0xf5, 0x19},
	{0xf6, 0x44},
	{0xf8, 0x63},
	{0xfa, 0x45},
	{0xf9, 0x00},
	{0xf7, 0x9d},
	{0xfc, 0x00},
	{0xfc, 0x00},
	{0xfc, 0xea},
	{0xfe, 0x03},
	{0x03, 0x9a},
	{0x18, 0x07},
	{0x01, 0x07},
	{0xfc, 0xee},
	/*Cisctl&Analog*/
	{0xfe, 0x00},
	{0x03, 0x08},
	{0x04, 0xc6},
	{0x05, 0x02},
	{0x06, 0x16},
	{0x07, 0x00},
	{0x08, 0x10},
	{0x0a, 0x3a},
	{0x0b, 0x00},
	{0x0c, 0x04},
	{0x0d, 0x09},
	{0x0e, 0xa0},
	{0x0f, 0x0c},
	{0x10, 0xd4},
	{0x17, 0xc0},
	{0x18, 0x02},
	{0x19, 0x17},
	{0x1e, 0x50},
	{0x1f, 0x80},
	{0x21, 0x4c},
	{0x25, 0x00},
	{0x28, 0x4a},
	{0x2d, 0x89},
	{0xca, 0x02},
	{0xcb, 0x00},
	{0xcc, 0x39},
	{0xce, 0xd0},
	{0xcf, 0x93},
	{0xd0, 0x19},
	{0xd1, 0xaa},
	{0xd2, 0xcb},
	{0xd8, 0x40},
	{0xd9, 0xff},
	{0xda, 0x0e},
	{0xdb, 0xb0},
	{0xdc, 0x0e},
	{0xde, 0x08},
	{0xe4, 0xc6},
	{0xe5, 0x08},
	{0xe6, 0x10},
	{0xed, 0x2a},
	{0xfe, 0x02},
	{0x59, 0x02},
	{0x5a, 0x04},
	{0x5b, 0x08},
	{0x5c, 0x20},
	{0xfe, 0x00},
	{0x1a, 0x09},
	{0x1d, 0x13},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfe, 0x10},
	{0xfe, 0x00},
	/*Gamma*/
	{0xfe, 0x00},
	{0x20, 0x55},
	{0x33, 0x83},
	{0xfe, 0x01},
	{0xdf, 0x06},
	{0xe7, 0x18},
	{0xe8, 0x20},
	{0xe9, 0x16},
	{0xea, 0x17},
	{0xeb, 0x50},
	{0xec, 0x6c},
	{0xed, 0x9b},
	{0xee, 0xd8},
	/*ISP*/
	{0xfe, 0x00},
	{0x80, 0x10},
	{0x84, 0x01},
	{0x88, 0x03},
	{0x89, 0x03},
	{0x8d, 0x03},
	{0x8f, 0x14},
	{0xad, 0x30},
	{0x66, 0x2c},
	{0xbc, 0x49},
	{0xc2, 0x7f},
	{0xc3, 0xff},
	/*Crop window*/
	{0x90, 0x01},
	{0x92, 0x08},
	{0x94, 0x09},
	{0x95, 0x04},
	{0x96, 0xc8},
	{0x97, 0x06},
	{0x98, 0x60},
	/*Gain*/
	{0xb0, 0x90},
	{0xb1, 0x01},
	{0xb2, 0x00},
	{0xb6, 0x00},
	/*BLK*/
	{0xfe, 0x00},
	{0x40, 0x22},
	{0x41, 0x20},
	{0x42, 0x02},
	{0x43, 0x08},
	{0x4e, 0x0f},
	{0x4f, 0xf0},
	{0x58, 0x80},
	{0x59, 0x80},
	{0x5a, 0x80},
	{0x5b, 0x80},
	{0x5c, 0x00},
	{0x5d, 0x00},
	{0x5e, 0x00},
	{0x5f, 0x00},
	{0x6b, 0x01},
	{0x6c, 0x00},
	{0x6d, 0x0c},
	/*WB offset*/
	{0xfe, 0x01},
	{0xbf, 0x40},
	/*Dark Sun*/
	{0xfe, 0x01},
	{0x68, 0x77},
	/*DPC*/
	{0xfe, 0x01},
	{0x60, 0x00},
	{0x61, 0x10},
	{0x62, 0x28},
	{0x63, 0x10},
	{0x64, 0x02},
	/*LSC*/
	{0xfe, 0x01},
	{0xa8, 0x60},
	{0xa2, 0xd1},
	{0xc8, 0x57},
	{0xa1, 0xb8},
	{0xa3, 0x91},
	{0xc0, 0x50},
	{0xd0, 0x05},
	{0xd1, 0xb2},
	{0xd2, 0x1f},
	{0xd3, 0x00},
	{0xd4, 0x00},
	{0xd5, 0x00},
	{0xd6, 0x00},
	{0xd7, 0x00},
	{0xd8, 0x00},
	{0xd9, 0x00},
	{0xa4, 0x10},
	{0xa5, 0x20},
	{0xa6, 0x60},
	{0xa7, 0x80},
	{0xab, 0x18},
	{0xc7, 0xc0},
	/*ABB*/
	{0xfe, 0x01},
	{0x20, 0x02},
	{0x21, 0x02},
	{0x23, 0x42},
	/*MIPI*/
	{0xfe, 0x03},
	{0x02, 0x03},
	{0x04, 0x80},
	{0x11, 0x2b},
	{0x12, 0xf8},
	{0x13, 0x07},
	{0x15, 0x10},
	{0x16, 0x29},
	{0x17, 0xff},
	{0x19, 0xaa},
	{0x1a, 0x02},
	{0x21, 0x02},
	{0x22, 0x03},
	{0x23, 0x0a},
	{0x24, 0x00},
	{0x25, 0x12},
	{0x26, 0x04},
	{0x29, 0x04},
	{0x2a, 0x02},
	{0x2b, 0x04},
	{0xfe, 0x00},
	{0x3f, 0x00},

	/*SYS*/
	{0xf2, 0x00},
	{0xf4, 0x80},
	{0xf5, 0x19},
	{0xf6, 0x44},
	{0xf8, 0x63},
	{0xfa, 0x45},
	{0xf9, 0x00},
	{0xf7, 0x95},
	{0xfc, 0x00},
	{0xfc, 0x00},
	{0xfc, 0xea},
	{0xfe, 0x03},
	{0x03, 0x9a},
	{0x18, 0x07},
	{0x01, 0x07},
	{0xfc, 0xee},
	/*ISP*/
	{0xfe, 0x00},
	{0x80, 0x13},
	{0xad, 0x00},
	/*Crop window*/
	{0x90, 0x01},
	{0x92, 0x08},
	{0x94, 0x09},
	{0x95, 0x09},
	{0x96, 0x90},
	{0x97, 0x0c},
	{0x98, 0xc0},
	/*DPC*/
	{0xfe, 0x01},
	{0x62, 0x60},
	{0x63, 0x48},
	/*MIPI*/
	{0xfe, 0x03},
	{0x02, 0x03},
	{0x04, 0x80},
	{0x11, 0x2b},
	{0x12, 0xf0},
	{0x13, 0x0f},
	{0x15, 0x10},
	{0x16, 0x29},
	{0x17, 0xff},
	{0x19, 0xaa},
	{0x1a, 0x02},
	{0x21, 0x05},
	{0x22, 0x06},
	{0x23, 0x2b},
	{0x24, 0x00},
	{0x25, 0x12},
	{0x26, 0x07},
	{0x29, 0x07},
	{0x2a, 0x12},
	{0x2b, 0x07},
	{0xfe, 0x00},
	{0x3f, 0x00},

	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 656Mbps
 */
static const struct regval gc8034_3264x2448_regs_4lane[] = {
	{REG_NULL, 0x00},
};

static const struct gc8034_mode supported_modes_2lane[] = {
#ifdef GC8034_2LANE_30FPS
	{
		.width = 3264,
		.height = 2448,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0900,
		.hts_def = 0x0858 * 2,
		.vts_def = 0x09c0,
		.mipi_freq_idx = 1,
		.global_reg_list = gc8034_global_regs_2lane,
		.reg_list = gc8034_3264x2448_regs_2lane,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
#else
	{
		.width = 3264,
		.height = 2448,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
		.exp_def = 0x09a0,
		.hts_def = 0x0858 * 2,
		.vts_def = 0x09c4,
		.mipi_freq_idx = 0,
		.global_reg_list = gc8034_global_regs_2lane,
		.reg_list = gc8034_3264x2448_regs_2lane,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 1632,
		.height = 1224,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x09a0,
		.hts_def = 0x0858 * 2,
		.vts_def = 0x09c4,
		.mipi_freq_idx = 0,
		.global_reg_list = gc8034_global_regs_2lane,
		.reg_list = gc8034_1632x1224_regs_2lane,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
#endif
};

static const struct gc8034_mode supported_modes_4lane[] = {
	{
		.width = 3264,
		.height = 2448,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x08c6,
		.hts_def = 0x10b0,
		.vts_def = 0x09c0,
		.mipi_freq_idx = 0,
		.global_reg_list = gc8034_global_regs_4lane,
		.reg_list = gc8034_3264x2448_regs_4lane,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const struct gc8034_mode *supported_modes;

static const s64 link_freq_menu_items[] = {
	GC8034_MIPI_FREQ_336MHZ,
	GC8034_MIPI_FREQ_634MHZ
};

/* Write registers up to 4 at a time */
static int gc8034_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	dev_err(&client->dev,
		"gc8034 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int gc8034_write_array(struct i2c_client *client,
	const struct regval *regs)
{
	u32 i = 0;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc8034_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int gc8034_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev,
		"gc8034 read reg:0x%x failed !\n", reg);

	return ret;
}

static int gc8034_get_reso_dist(const struct gc8034_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		abs(mode->height - framefmt->height);
}

static const struct gc8034_mode *
gc8034_find_best_fit(struct gc8034 *gc8034,
		     struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < gc8034->cfg_num; i++) {
		dist = gc8034_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc8034_set_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *fmt)
{
	struct gc8034 *gc8034 = to_gc8034(sd);
	const struct gc8034_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc8034->mutex);

	mode = gc8034_find_best_fit(gc8034, fmt);
	fmt->format.code = GC8034_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc8034->mutex);
		return -ENOTTY;
#endif
	} else {
		gc8034->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc8034->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc8034->vblank, vblank_def,
					 GC8034_VTS_MAX - mode->height,
					 1, vblank_def);
		__v4l2_ctrl_s_ctrl(gc8034->vblank, vblank_def);
		__v4l2_ctrl_s_ctrl(gc8034->link_freq, mode->mipi_freq_idx);
	}

	mutex_unlock(&gc8034->mutex);

	return 0;
}

static int gc8034_get_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *fmt)
{
	struct gc8034 *gc8034 = to_gc8034(sd);
	const struct gc8034_mode *mode = gc8034->cur_mode;

	mutex_lock(&gc8034->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc8034->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = GC8034_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc8034->mutex);

	return 0;
}

static int gc8034_enum_mbus_code(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = GC8034_MEDIA_BUS_FMT;

	return 0;
}

static int gc8034_enum_frame_sizes(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc8034 *gc8034 = to_gc8034(sd);

	if (fse->index >= gc8034->cfg_num)
		return -EINVAL;

	if (fse->code != GC8034_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int gc8034_g_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_frame_interval *fi)
{
	struct gc8034 *gc8034 = to_gc8034(sd);
	const struct gc8034_mode *mode = gc8034->cur_mode;

	fi->interval = mode->max_fps;

	return 0;
}

#ifdef RK_OTP
static void gc8034_get_otp(struct otp_info *otp,
				   struct rkmodule_inf *inf)
{
	u32 i, j;
	u32 w, h;

	/* awb */
	if (otp->awb_data.flag) {
		inf->awb.flag = 1;
		inf->awb.r_value = otp->awb_data.r_ratio;
		inf->awb.b_value = otp->awb_data.b_ratio;
		inf->awb.gr_value = otp->awb_data.g_ratio;
		inf->awb.gb_value = 0x0;

		inf->awb.golden_r_value = otp->awb_data.r_golden;
		inf->awb.golden_b_value = otp->awb_data.b_golden;
		inf->awb.golden_gr_value = otp->awb_data.g_golden;
		inf->awb.golden_gb_value = 0x0;
	}

	/* lsc */
	if (otp->lsc_data.flag) {
		inf->lsc.flag = 1;
		inf->lsc.width = otp->basic_data.size.width;
		inf->lsc.height = otp->basic_data.size.height;
		inf->lsc.table_size = otp->lsc_data.table_size;

		for (i = 0; i < 289; i++) {
			inf->lsc.lsc_r[i] = (otp->lsc_data.data[i * 2] << 8) |
						 otp->lsc_data.data[i * 2 + 1];
			inf->lsc.lsc_gr[i] = (otp->lsc_data.data[i * 2 + 578] << 8) |
						  otp->lsc_data.data[i * 2 + 579];
			inf->lsc.lsc_gb[i] = (otp->lsc_data.data[i * 2 + 1156] << 8) |
						  otp->lsc_data.data[i * 2 + 1157];
			inf->lsc.lsc_b[i] = (otp->lsc_data.data[i * 2 + 1734] << 8) |
						 otp->lsc_data.data[i * 2 + 1735];
		}
	}

	/* pdaf */
	if (otp->pdaf_data.flag) {
		inf->pdaf.flag = 1;
		inf->pdaf.gainmap_width = otp->pdaf_data.gainmap_width;
		inf->pdaf.gainmap_height = otp->pdaf_data.gainmap_height;
		inf->pdaf.pd_offset = otp->pdaf_data.pd_offset;
		inf->pdaf.dcc_mode = otp->pdaf_data.dcc_mode;
		inf->pdaf.dcc_dir = otp->pdaf_data.dcc_dir;
		inf->pdaf.dccmap_width = otp->pdaf_data.dccmap_width;
		inf->pdaf.dccmap_height = otp->pdaf_data.dccmap_height;
		w = otp->pdaf_data.gainmap_width;
		h = otp->pdaf_data.gainmap_height;
		for (i = 0; i < h; i++) {
			for (j = 0; j < w; j++) {
				inf->pdaf.gainmap[i * w + j] =
					(otp->pdaf_data.gainmap[(i * w + j) * 2] << 8) |
					otp->pdaf_data.gainmap[(i * w + j) * 2 + 1];
			}
		}
		w = otp->pdaf_data.dccmap_width;
		h = otp->pdaf_data.dccmap_height;
		for (i = 0; i < h; i++) {
			for (j = 0; j < w; j++) {
				inf->pdaf.dccmap[i * w + j] =
					(otp->pdaf_data.dccmap[(i * w + j) * 2] << 8) |
					otp->pdaf_data.dccmap[(i * w + j) * 2 + 1];
			}
		}
	}

	/* af */
	if (otp->af_data.flag) {
		inf->af.flag = 1;
		inf->af.dir_cnt = 1;
		inf->af.af_otp[0].vcm_start = otp->af_data.af_inf;
		inf->af.af_otp[0].vcm_end = otp->af_data.af_macro;
		inf->af.af_otp[0].vcm_dir = 0;
	}

}
#else
#define DD_WIDTH 3284
#define DD_HEIGHT 2464

#define DD_PARAM_QTY		350
#define WINDOW_WIDTH		0x0cd4//3284 max effective pixels
#define WINDOW_HEIGHT		0x09a0//2462
#define REG_ROM_START		0x4e
#define INFO_ROM_START		0x70
#define INFO_WIDTH		0x08
#define WB_ROM_START		0x5f
#define WB_WIDTH		0x04
#define GOLDEN_ROM_START	0x67//golden R/G ratio
#define GOLDEN_WIDTH		0x04
#define LSC_NUM			99//0x63 //(7+2)*(9+2)
#define VCM_START		0x3B
#define VCM_WIDTH		0x04

static int gc8034_otp_read_reg(struct i2c_client *client,
	int page, int address)
{
	int ret = 0;
	u8 val = 0;

	ret = gc8034_write_reg(client, 0xfe, 0x00);
	ret |= gc8034_write_reg(client, 0xD4,
		((page << 2) & 0x3c) + ((address >> 5) & 0x03));
	ret |= gc8034_write_reg(client, 0xD5,
		(address << 3) & 0xff);
	ret |= gc8034_write_reg(client, 0xF3,
		0x20);
	ret |= gc8034_read_reg(client, 0xD7, &val);
	if (ret != 0)
		return ret;
	return val;
}

static int gc8034_otp_read_group(struct i2c_client *client,
	int page, int address, u8 *buf, int size)
{
	int i = 0;
	int val = 0;

	for (i = 0; i < size; i++) {
		if ((address % 0x80) == 0) {
			page += 1;
			address = 0;
		}
		val = gc8034_otp_read_reg(client, page, address);
		if (val >= 0)
			buf[i] = val;
		else
			return val;
		address += 1;
	}
	return 0;
}

static int gc8034_otp_enable(struct gc8034 *gc8034)
{
	struct i2c_client *client = gc8034->client;
	u8 otp_clk = 0;
	u8 otp_en = 0;
	int ret = 0;

	ret = gc8034_write_reg(client, 0xf2, 0x00);
	ret |= gc8034_write_reg(client, 0xf4, 0x80);
	ret |= gc8034_write_reg(client, 0xfc, 0x00);
	ret |= gc8034_write_reg(client, 0xf7, 0x97);
	ret |= gc8034_write_reg(client, 0xfc, 0x00);
	ret |= gc8034_write_reg(client, 0xfc, 0x00);
	ret |= gc8034_write_reg(client, 0xfc, 0xee);
	ret |= gc8034_read_reg(client, 0xF2, &otp_clk);
	ret |= gc8034_read_reg(client, 0xF4, &otp_en);
	otp_clk |= 0x01;
	otp_en |= 0x08;
	ret |= gc8034_write_reg(client, 0xF2, otp_clk);
	ret |= gc8034_write_reg(client, 0xF4, otp_en);
	usleep_range(100, 200);
	return ret;
}

static int gc8034_otp_disable(struct gc8034 *gc8034)
{
	struct i2c_client *client = gc8034->client;
	u8 otp_clk = 0;
	u8 otp_en = 0;
	int ret = 0;

	ret = gc8034_read_reg(client, 0xF2, &otp_clk);
	ret |= gc8034_read_reg(client, 0xF4, &otp_en);
	otp_clk &= 0xFE;
	otp_en &= 0xF7;
	ret |= gc8034_write_reg(client, 0xF2, otp_clk);
	ret |= gc8034_write_reg(client, 0xF4, otp_en);
	ret |= gc8034_write_reg(client, 0xfc, 0x00);
	ret |= gc8034_write_reg(client, 0xf7, 0x95);
	ret |= gc8034_write_reg(client, 0xfc, 0x00);
	ret |= gc8034_write_reg(client, 0xfc, 0x00);
	ret |= gc8034_write_reg(client, 0xfc, 0xee);
	return ret;
}

static void gc8034_check_prsel(struct gc8034 *gc8034)
{
	struct i2c_client *client = gc8034->client;
	u8 product_level = 0;

	gc8034_write_reg(client, 0xfe, 0x02);
	gc8034_read_reg(client, 0x68, &product_level);
	product_level &= 0x07;

	if (product_level == 0x00 || product_level == 0x01) {
		gc8034_write_reg(client, 0xfe, 0x00);
		gc8034_write_reg(client, 0xd2, 0xcb);
	} else {
		gc8034_write_reg(client, 0xfe, 0x00);
		gc8034_write_reg(client, 0xd2, 0xc3);
	}
}

static int gc8034_otp_read(struct gc8034 *gc8034)
{
	int otp_flag, i, j, index, temp;
	struct gc8034_otp_info *otp_ptr;
	struct device *dev = &gc8034->client->dev;
	struct i2c_client *client = gc8034->client;
	int ret = 0;
	int cnt = 0;
	int checksum = 0;
	u8 info[8] = {0};
	u8 wb[4] = {0};
	u8 vcm[4] = {0};
	u8 golden[4] = {0};
	int total_number = 0;
	u8 ddtempbuff[4 * 80] = { 0 };

	otp_ptr = devm_kzalloc(dev, sizeof(*otp_ptr), GFP_KERNEL);
	if (!otp_ptr)
		return -ENOMEM;

	/* OTP base information*/
	otp_flag = gc8034_otp_read_reg(client, 9, 0x6f);
	for (index = 0; index < 2; index++) {
		switch ((otp_flag << (2 * index)) & 0x0c) {
		case 0x00:
			dev_err(dev, "%s GC8034_OTP_INFO group %d is Empty!\n",
				__func__, index + 1);
			break;
		case 0x04:
			dev_err(dev, "%s GC8034_OTP_INFO group %d is Valid!\n",
				__func__, index + 1);
			checksum = 0;
			ret |= gc8034_otp_read_group(client, 9,
				(INFO_ROM_START + index * INFO_WIDTH),
				&info[0], INFO_WIDTH);
			if (ret < 0) {
				dev_err(dev, "%s read otp error!\n", __func__);
				return ret;
			}
			for (i = 0; i < INFO_WIDTH - 1; i++)
				checksum += info[i];
			if ((checksum % 255 + 1) == info[INFO_WIDTH - 1]) {
				otp_ptr->flag = 0x80;
				otp_ptr->module_id = info[0];
				otp_ptr->lens_id = info[1];
				otp_ptr->year = info[4];
				otp_ptr->month = info[5];
				otp_ptr->day = info[6];
				dev_err(dev, "fac info: module(0x%x) lens(0x%x) time(%d_%d_%d)!\n",
					otp_ptr->module_id,
					otp_ptr->lens_id,
					otp_ptr->year,
					otp_ptr->month,
					otp_ptr->day);
			} else {
				dev_err(dev, "%s GC8034_OTP_INFO Check sum %d Error!\n",
					__func__, index + 1);
			}
			break;
		case 0x08:
		case 0x0c:
			dev_err(dev, "%s GC8034_OTP_INFO group %d is Invalid !!\n",
				__func__, index + 1);
			break;
		default:
			break;
		}
	}

	/* OTP WB calibration data */
	otp_flag = gc8034_otp_read_reg(client, 9, 0x5e);
	for (index = 0; index < 2; index++) {
		switch ((otp_flag << (2 * index)) & 0x0c) {
		case 0x00:
			dev_err(dev, "%s GC8034_OTP_WB group %d is Empty !\n",
				__func__, index + 1);
			break;
		case 0x04:
			dev_err(dev, "%s GC8034_OTP_WB group %d is Valid !!\n",
				__func__, index + 1);
			checksum = 0;
			ret |= gc8034_otp_read_group(client,
				9,
				(WB_ROM_START + index * WB_WIDTH),
				&wb[0],
				WB_WIDTH);
			if (ret < 0) {
				dev_err(dev, "%s read otp error!\n", __func__);
				return ret;
			}
			for (i = 0; i < WB_WIDTH - 1; i++)
				checksum += wb[i];
			if ((checksum % 255 + 1) == wb[WB_WIDTH - 1]) {
				otp_ptr->flag |= 0x40; /* valid AWB in OTP */
				otp_ptr->rg_ratio =
					((wb[1] & 0xf0) << 4) | wb[0];
				otp_ptr->bg_ratio =
					((wb[1] & 0x0f) << 8) | wb[2];
				dev_err(dev, "otp:(rg_ratio 0x%x, bg_ratio 0x%x)\n",
					otp_ptr->rg_ratio, otp_ptr->bg_ratio);
			} else {
				dev_err(dev, "%s GC8034_OTP_WB Check sum %d Error !!\n",
					__func__, index + 1);
			}
			break;
		case 0x08:
		case 0x0c:
			dev_err(dev, "%s GC8034_OTP_WB group %d is Invalid !!\n",
				__func__, index + 1);
			break;
		default:
			break;
		}
		switch ((otp_flag << (2 * index)) & 0xc0) {
		case 0x00:
			dev_err(dev,  "%s GC8034_OTP_GOLDEN group %d is Empty!\n",
				__func__, index + 1);
			break;
		case 0x40:
			dev_err(dev, "%s GC8034_OTP_GOLDEN group %d is Valid !!\n",
				__func__, index + 1);
			checksum = 0;
			ret = gc8034_otp_read_group(client, 9,
				(GOLDEN_ROM_START + index * GOLDEN_WIDTH),
				&golden[0], GOLDEN_WIDTH);
			if (ret < 0) {
				dev_err(dev, "%s read otp error!\n", __func__);
				return ret;
			}
			for (i = 0; i < GOLDEN_WIDTH - 1; i++)
				checksum += golden[i];
			if ((checksum % 255 + 1) == golden[GOLDEN_WIDTH - 1]) {
				otp_ptr->golden_rg =
					golden[0] | ((golden[1] & 0xf0) << 4);
				otp_ptr->golden_bg =
					((golden[1] & 0x0f) << 8) | golden[2];
				dev_err(dev, "otp:(golden_rg 0x%x, golden_bg 0x%x)\n",
					otp_ptr->golden_rg, otp_ptr->golden_bg);
			} else {
				dev_err(dev, "%s GC8034_OTP_GOLDEN Check sum %d Error !!\n",
					__func__, index + 1);
			}
			break;
		case 0x80:
		case 0xc0:
			dev_err(dev, "%s GC8034_OTP_GOLDEN group %d is Invalid !!\n",
				__func__, index + 1);
			break;
		default:
			break;
		}
	}

	/* OTP VCM calibration data */
	otp_flag = gc8034_otp_read_reg(client, 3, 0x3A);
	for (index = 0; index < 2; index++) {
		switch ((otp_flag << (2 * index)) & 0x0c) {
		case 0x00:
			dev_err(dev, "%s GC8034_OTP_VCM group %d is Empty !\n",
				__func__, index + 1);
			break;
		case 0x04:
			dev_err(dev, "%s GC8034_OTP_VCM group %d is Valid !\n",
				__func__, index + 1);
			ret |= gc8034_otp_read_group(client,
				3,
				(VCM_START + index * VCM_WIDTH),
				&vcm[0],
				VCM_WIDTH);
			if (ret < 0) {
				dev_err(dev, "%s read otp error!\n", __func__);
				return ret;
			}
			checksum = 0;
			for (i = 0; i < 3; i++)
				checksum += vcm[i];
			if ((checksum % 255 + 1) == vcm[3]) {
				otp_ptr->flag |= 0x20; /* valid LSC in OTP */
				otp_ptr->vcm_dir = 0;//not dir register
				otp_ptr->vcm_start =
					((vcm[0] & 0x0f) << 8) + vcm[2];
				otp_ptr->vcm_end =
					((vcm[0] & 0xf0) << 4) + vcm[1];
				dev_err(dev, "%s GC8034_OTP_VCM check sum success\n",
					__func__);
				dev_err(dev, "vcm_info: 0x%x, 0x%x, 0x%x!\n",
					otp_ptr->vcm_start,
					otp_ptr->vcm_end,
					otp_ptr->vcm_dir);
			} else {
				dev_err(dev, "VCM check sum read: 0x%x, calculate:0x%x\n",
					vcm[3], checksum % 255 + 1);
			}
			break;
		case 0x08:
		case 0x0c:
			dev_err(dev, "%s GC8034_OTP_VCM group %d is Invalid !\n",
				__func__, index + 1);
			break;
		default:
			break;
		}
	}

	/* OTP LSC calibration data */
	otp_flag = gc8034_otp_read_reg(client, 3, 0x43);
	for (index = 0; index < 2; index++) {
		switch ((otp_flag << (2 * index)) & 0x0c) {
		case 0x00:
			dev_err(dev, "%s GC8034_OTP_LSC group %d is Empty !\n",
				__func__, index + 1);
			break;
		case 0x04:
			dev_err(dev, "%s GC8034_OTP_LSC	group %d is Valid !\n",
				__func__, index	+ 1);
			if (index == 0)	{
				ret |= gc8034_otp_read_group(client,
					3, 0x44, otp_ptr->lsc, 396);
				temp = gc8034_otp_read_reg(client, 6, 0x50);
			} else {
				ret |= gc8034_otp_read_group(client,
					6, 0x51, otp_ptr->lsc, 396);
				temp = gc8034_otp_read_reg(client, 9, 0x5d);
			}
			checksum = 0;
			for (i = 0; i <	396; i++) {
				checksum += otp_ptr->lsc[i];
				usleep_range(100, 200);
				dev_err(dev, "otp lsc[%d] = %d\n",
					i, otp_ptr->lsc[i]);
			}
			if ((checksum %	255 + 1) == temp) {
				otp_ptr->flag |= 0x10; /* valid	LSC in OTP */
				dev_err(dev, "%s GC8034_OTP_LSC	check sum success\n",
					__func__);
			} else {
				dev_err(dev, "LSC check	sum read: 0x%x,	calculate:0x%x\n",
					temp, checksum % 255 + 1);
			}
			break;
		case 0x08:
		case 0x0c:
			dev_err(dev, "%s GC8034_OTP_LSC	group %d is Invalid !\n",
				__func__, index	+ 1);
			break;
		default:
			break;
		}
	}
	/* OTP DD calibration data */
	otp_flag = gc8034_otp_read_reg(client, 0, 0x0b);
	for (index = 0; index < 2; index++) {
		switch (otp_flag & 0x03) {
		case 0x00:
			dev_err(dev, "%s GC8034 OTP:flag_dd is EMPTY!\n",
				__func__);
			break;
		case 0x04:
			dev_err(dev, "%s GC8034_OTP_DD group %d is valid!\n",
				__func__, index + 1);
			total_number = gc8034_otp_read_reg(client, 0, 0x0c) +
				gc8034_otp_read_reg(client, 0, 0x0d);
			ret |= gc8034_otp_read_group(client, 0, 0x0e,
				&ddtempbuff[0], 4 * total_number);
			for (i = 0; i < total_number; i++) {
				if ((ddtempbuff[4 * i + 3] & 0x80) == 0x80) {
					if ((ddtempbuff[4 * i + 3] & 0x03) == 0x03) {
						otp_ptr->dd_param[cnt].x =
							(((u16)ddtempbuff[4 * i + 1] & 0x0f) << 8) +
							ddtempbuff[4 * i];
						otp_ptr->dd_param[cnt].y =
							((u16)ddtempbuff[4 * i + 2] << 4) +
							((ddtempbuff[4 * i + 1] & 0xf0) >> 4);
						otp_ptr->dd_param[cnt++].t = 2;
						otp_ptr->dd_param[cnt].x =
							(((u16)ddtempbuff[4 * i + 1] & 0x0f) << 8) +
							ddtempbuff[4 * i];
						otp_ptr->dd_param[cnt].y =
							((u16)ddtempbuff[4 * i + 2] << 4) +
							((ddtempbuff[4 * i + 1] & 0xf0) >> 4) + 1;
						otp_ptr->dd_param[cnt++].t = 2;
					} else {
						otp_ptr->dd_param[cnt].x =
							(((u16)ddtempbuff[4 * i + 1] & 0x0f) << 8) +
							ddtempbuff[4 * i];
						otp_ptr->dd_param[cnt].y =
							((u16)ddtempbuff[4 * i + 2] << 4) +
							((ddtempbuff[4 * i + 1] & 0xf0) >> 4);
						otp_ptr->dd_param[cnt++].t =
							ddtempbuff[4 * i + 3] & 0x03;
					}
				}
			}
			otp_ptr->dd_cnt = total_number;
			otp_ptr->flag |= 0x08;
			dev_err(dev, "%s GC8034 OTP:total_number = %d!\n",
				__func__, total_number);
			break;
		case 0x08:
		case 0x0c:
			dev_err(dev, "%s GC8034_OTP_DD group %d is Invalid!\n",
				__func__, index + 1);
			break;
		default:
			break;
		}
	}
	/* OTP Chip Register*/
	otp_flag = gc8034_otp_read_reg(client, 2, 0x4e);
	if (otp_flag == 1) {
		for (i = 0; i < 5; i++) {
			dev_err(dev, "%s GC8034 reg is valid!\n", __func__);
			temp = gc8034_otp_read_reg(client, 2, (0x4f + 5 * i));
			for (j = 0; j < 2; j++) {
				if (((temp >> (4 * j + 3)) & 0x01) == 0x01) {
					otp_ptr->reg_page[otp_ptr->reg_num] =
						(temp >> (4 * j)) & 0x03;
					otp_ptr->reg_addr[otp_ptr->reg_num] =
						gc8034_otp_read_reg(client,
						2,
						0x50 + 5 * i + 2 * j);
					otp_ptr->reg_value[otp_ptr->reg_num] =
						gc8034_otp_read_reg(client,
						2,
						0x50 + 5 * i + 2 * j + 1);
					otp_ptr->reg_num++;
				}
			}
		}
		otp_ptr->flag |= 0x04;
	}

	if (otp_ptr->flag) {
		gc8034->otp = otp_ptr;
	} else {
		gc8034->otp = NULL;
		devm_kfree(dev, otp_ptr);
	}

	return 0;
}

static void gc8034_get_otp(struct gc8034_otp_info *otp,
	struct rkmodule_inf *inf)
{
	u32 i;

	/* fac */
	if (otp->flag & 0x80) {
		inf->fac.flag = 1;
		inf->fac.year = otp->year;
		inf->fac.month = otp->month;
		inf->fac.day = otp->day;
		for (i = 0; i < ARRAY_SIZE(gc8034_module_info) - 1; i++) {
			if (gc8034_module_info[i].id == otp->module_id)
				break;
		}
		strlcpy(inf->fac.module, gc8034_module_info[i].name,
			sizeof(inf->fac.module));

		for (i = 0; i < ARRAY_SIZE(gc8034_lens_info) - 1; i++) {
			if (gc8034_lens_info[i].id == otp->lens_id)
				break;
		}
		strlcpy(inf->fac.lens, gc8034_lens_info[i].name,
			sizeof(inf->fac.lens));
	}
	/* awb */
	if (otp->flag & 0x40) {
		inf->awb.flag = 1;
		inf->awb.r_value = otp->rg_ratio;
		inf->awb.b_value = otp->bg_ratio;
		inf->awb.gr_value = 0;
		inf->awb.gb_value = 0;

		inf->awb.golden_r_value = 0;
		inf->awb.golden_b_value = 0;
		inf->awb.golden_gr_value = 0;
		inf->awb.golden_gb_value = 0;
	}
	/* af */
	if (otp->flag & 0x20) {
		inf->af.flag = 1;
		inf->af.dir_cnt = 1;
		inf->af.af_otp[0].vcm_start = otp->vcm_start;
		inf->af.af_otp[0].vcm_end = otp->vcm_end;
		inf->af.af_otp[0].vcm_dir = otp->vcm_dir;
	}
}
#endif

static void gc8034_get_module_inf(struct gc8034 *gc8034,
				struct rkmodule_inf *inf)
{
#ifdef RK_OTP
	struct otp_info *otp = gc8034->otp;
#else
	struct gc8034_otp_info *otp = gc8034->otp;
#endif

	strlcpy(inf->base.sensor,
		GC8034_NAME,
		sizeof(inf->base.sensor));
	strlcpy(inf->base.module,
		gc8034->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens,
		gc8034->len_name,
		sizeof(inf->base.lens));
	if (otp)
		gc8034_get_otp(otp, inf);
}

static void gc8034_set_module_inf(struct gc8034 *gc8034,
				struct rkmodule_awb_cfg *cfg)
{
	mutex_lock(&gc8034->mutex);
	memcpy(&gc8034->awb_cfg, cfg, sizeof(*cfg));
	mutex_unlock(&gc8034->mutex);
}

static int gc8034_get_channel_info(struct gc8034 *gc8034, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = gc8034->cur_mode->vc[ch_info->index];
	ch_info->width = gc8034->cur_mode->width;
	ch_info->height = gc8034->cur_mode->height;
	ch_info->bus_fmt = GC8034_MEDIA_BUS_FMT;
	return 0;
}

static long gc8034_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc8034 *gc8034 = to_gc8034(sd);
	long ret = 0;
	u32 stream = 0;
	struct rkmodule_channel_info *ch_info;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc8034_get_module_inf(gc8034, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_AWB_CFG:
		gc8034_set_module_inf(gc8034, (struct rkmodule_awb_cfg *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream) {
			ret = gc8034_write_reg(gc8034->client,
					       GC8034_REG_SET_PAGE,
					       GC8034_SET_PAGE_ZERO);
			if (2 == gc8034->lane_num) {
				ret |= gc8034_write_reg(gc8034->client,
							GC8034_REG_CTRL_MODE,
							0x91);
			} else {
				ret |= gc8034_write_reg(gc8034->client,
							GC8034_REG_CTRL_MODE,
							GC8034_MODE_STREAMING);
			}
		} else {
			ret = gc8034_write_reg(gc8034->client,
					       GC8034_REG_SET_PAGE,
					       GC8034_SET_PAGE_ZERO);
			ret |= gc8034_write_reg(gc8034->client,
						GC8034_REG_CTRL_MODE,
						GC8034_MODE_SW_STANDBY);
		}
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = gc8034_get_channel_info(gc8034, ch_info);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc8034_compat_ioctl32(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret = 0;
	u32 stream = 0;
	struct rkmodule_channel_info *ch_info;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc8034_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = gc8034_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc8034_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc8034_ioctl(sd, cmd, ch_info);
		if (!ret) {
			ret = copy_to_user(up, ch_info, sizeof(*ch_info));
			if (ret)
				ret = -EFAULT;
		}
		kfree(ch_info);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}
#endif

#ifndef RK_OTP
/*--------------------------------------------------------------------------*/
static int gc8034_apply_otp(struct gc8034 *gc8034)
{
	int R_gain, G_gain, B_gain, base_gain;
	struct i2c_client *client = gc8034->client;
	struct gc8034_otp_info *otp_ptr = gc8034->otp;
	struct rkmodule_awb_cfg *awb_cfg = &gc8034->awb_cfg;
	u32 golden_bg_ratio;
	u32 golden_rg_ratio;
	u32 golden_g_value;
	u32 i, j;
	u16 base = 0;
	u32 dd_cnt = 0;
	u8 temp_val0 = 0;
	u8 temp_val1 = 0;
	u8 temp_val2 = 0;
	struct gc8034_dd dd_temp = {0, 0, 0};

	if (!gc8034->awb_cfg.enable)
		return 0;

	golden_g_value = (awb_cfg->golden_gb_value +
		 awb_cfg->golden_gr_value) / 2;
	golden_bg_ratio = awb_cfg->golden_b_value * 0x400 / golden_g_value;
	golden_rg_ratio = awb_cfg->golden_r_value * 0x400 / golden_g_value;
	/* apply OTP WB Calibration */
	if ((otp_ptr->flag & 0x40) && golden_bg_ratio && golden_rg_ratio) {
		/* calculate G gain */
		R_gain = golden_rg_ratio * 1000 / otp_ptr->rg_ratio;
		B_gain = golden_bg_ratio * 1000 / otp_ptr->bg_ratio;
		G_gain = 1000;
		base_gain = (R_gain < B_gain) ? R_gain : B_gain;
		base_gain = (base_gain < G_gain) ? base_gain : G_gain;

		R_gain = 0x400 * R_gain / (base_gain);
		B_gain = 0x400 * B_gain / (base_gain);
		G_gain = 0x400 * G_gain / (base_gain);

		/* update sensor WB gain */
		gc8034_write_reg(client, 0xfe, 0x01);

		gc8034_write_reg(client, 0x84, G_gain >> 3);
		gc8034_write_reg(client, 0x85, R_gain >> 3);
		gc8034_write_reg(client, 0x86, B_gain >> 3);
		gc8034_write_reg(client, 0x87, G_gain >> 3);
		gc8034_write_reg(client, 0x88,
			((G_gain & 0X07) << 4) + (R_gain & 0x07));
		gc8034_write_reg(client, 0x89,
			((B_gain & 0X07) << 4) + (G_gain & 0x07));

		gc8034_write_reg(client, 0xfe, 0x00);

		dev_dbg(&client->dev, "apply awb gain: 0x%x, 0x%x, 0x%x\n",
			R_gain, G_gain, B_gain);
	}
	/* apply OTP Lenc Calibration */
	if (otp_ptr->flag & 0x10) {
		gc8034_write_reg(client, 0xfe, 0x01);
		gc8034_write_reg(client, 0xcf, 0x00);
		gc8034_write_reg(client, 0xc9, 0x01);
		for (i = 0; i < 9; i++) {
			gc8034_write_reg(client, 0xca, i * 0x0c);
			for (j = 0; j < 11; j++) {
#if defined(GC8034_MIRROR_NORMAL)
				base = 4 * (11 * i + j);
#elif defined(GC8034_MIRROR_H)
				base = 4 * (11 * i + 10 - j);
#elif defined(GC8034_MIRROR_V)
				base = 4 * (11 * (8 - i) + j);
#elif defined(GC8034_MIRROR_HV)
				base = 4 * (11 * (8 - i) + 10 - j);
#endif
				gc8034_write_reg(client, 0xcc,
					otp_ptr->lsc[base + 0]);
				gc8034_write_reg(client, 0xcc,
					otp_ptr->lsc[base + 1]);
				gc8034_write_reg(client, 0xcc,
					otp_ptr->lsc[base + 2]);
				gc8034_write_reg(client, 0xcc,
					otp_ptr->lsc[base + 3]);
				dev_dbg(&client->dev,
					"apply lsc otp_ptr->lsc[%d]=%d\n",
					base + 0,
					otp_ptr->lsc[base + 0]);
				dev_dbg(&client->dev,
					"apply lsc otp_ptr->lsc[%d]=%d\n",
					base + 1,
					otp_ptr->lsc[base + 1]);
				dev_dbg(&client->dev,
					"apply lsc otp_ptr->lsc[%d]=%d\n",
					base + 2,
					otp_ptr->lsc[base + 2]);
				dev_dbg(&client->dev,
					"apply lsc otp_ptr->lsc[%d]=%d\n",
					base + 3,
					otp_ptr->lsc[base + 3]);
			}
		}
		gc8034_write_reg(client, 0xcf, 0x01);
		gc8034_write_reg(client, 0xa0, 0x13);
		gc8034_write_reg(client, 0xfe, 0x00);
		dev_err(&client->dev, "apply lsc\n");
	}
	/* apply OTP DD Calibration */
	if (otp_ptr->flag & 0x08) {
		dd_cnt = otp_ptr->dd_cnt;
		for (i = 0; i < dd_cnt; i++) {
#if defined(GC8034_MIRROR_H) || defined(GC8034_MIRROR_HV)
			switch (otp_ptr->dd_param[i].t) {
			case 0:
				otp_ptr->dd_param[i].x =
					DD_WIDTH - otp_ptr->dd_param[i].x + 1;
				break;
			case 1:
				otp_ptr->dd_param[i].x =
					DD_WIDTH - otp_ptr->dd_param[i].x - 1;
				break;
			default:
				otp_ptr->dd_param[i].x =
					DD_WIDTH - otp_ptr->dd_param[i].x;
				break;
			}
#endif
#if defined(GC8034_MIRROR_V) || defined(GC8034_MIRROR_HV)
			otp_ptr->dd_param[i].y =
				DD_HEIGHT - otp_ptr->dd_param[i].y + 1;
#endif
		}
		for (i = 0; i < dd_cnt - 1; i++) {
			for (j = i + 1; j < dd_cnt; j++) {
				if (otp_ptr->dd_param[i].y *
					DD_WIDTH + otp_ptr->dd_param[i].x >
					otp_ptr->dd_param[j].y * DD_WIDTH +
					otp_ptr->dd_param[j].x) {
					dd_temp.x = otp_ptr->dd_param[i].x;
					dd_temp.y = otp_ptr->dd_param[i].y;
					dd_temp.t = otp_ptr->dd_param[i].t;
					otp_ptr->dd_param[i].x =
						otp_ptr->dd_param[j].x;
					otp_ptr->dd_param[i].y =
						otp_ptr->dd_param[j].y;
					otp_ptr->dd_param[i].t =
						otp_ptr->dd_param[j].t;
					otp_ptr->dd_param[j].x = dd_temp.x;
					otp_ptr->dd_param[j].y = dd_temp.y;
					otp_ptr->dd_param[j].t = dd_temp.t;
				}
			}
		}
		gc8034_write_reg(client, 0xfe, 0x01);
		gc8034_write_reg(client, 0xbe, 0x00);
		gc8034_write_reg(client, 0xa9, 0x01);
		for (i = 0; i < dd_cnt; i++) {
			temp_val0 = otp_ptr->dd_param[i].x & 0x00ff;
			temp_val1 = ((otp_ptr->dd_param[i].y & 0x000f) << 4) +
				((otp_ptr->dd_param[i].x & 0x0f00) >> 8);
			temp_val2 = (otp_ptr->dd_param[i].y & 0x0ff0) >> 4;
			gc8034_write_reg(client, 0xaa, i);
			gc8034_write_reg(client, 0xac, temp_val0);
			gc8034_write_reg(client, 0xac, temp_val1);
			gc8034_write_reg(client, 0xac, temp_val2);
			gc8034_write_reg(client, 0xac, otp_ptr->dd_param[i].t);
		}
		gc8034_write_reg(client, 0xbe, 0x01);
		gc8034_write_reg(client, 0xfe, 0x00);
		dev_err(&client->dev, "apply dd\n");
	}

	if (otp_ptr->flag & 0x04) {
		gc8034_write_reg(client, 0xfe, 0x00);
		for (i = 0; i < otp_ptr->reg_num; i++) {
			gc8034_write_reg(client, 0xfe, otp_ptr->reg_page[i]);
			gc8034_write_reg(client,
				otp_ptr->reg_addr[i],
				otp_ptr->reg_value[i]);
		}
		dev_err(&client->dev, "apply chip reg\n");
	}
	return 0;
}
#endif

static int __gc8034_start_stream(struct gc8034 *gc8034)
{
	int ret;
#ifndef RK_OTP
	if (gc8034->otp) {
		ret = gc8034_otp_enable(gc8034);
		gc8034_check_prsel(gc8034);
		ret |= gc8034_apply_otp(gc8034);
		usleep_range(1000, 2000);
		ret |= gc8034_otp_disable(gc8034);
		if (ret)
			return ret;
	}
#endif
	ret = gc8034_write_array(gc8034->client, gc8034->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&gc8034->mutex);
	ret = v4l2_ctrl_handler_setup(&gc8034->ctrl_handler);
	mutex_lock(&gc8034->mutex);
	ret |= gc8034_write_reg(gc8034->client,
		GC8034_REG_SET_PAGE,
		GC8034_SET_PAGE_ZERO);
	if (2 == gc8034->lane_num) {
		ret |= gc8034_write_reg(gc8034->client,
			GC8034_REG_CTRL_MODE,
			0x91);
	} else {
		ret |= gc8034_write_reg(gc8034->client,
			GC8034_REG_CTRL_MODE,
			GC8034_MODE_STREAMING);
	}
	return ret;
}

static int __gc8034_stop_stream(struct gc8034 *gc8034)
{
	int ret;

	ret = gc8034_write_reg(gc8034->client,
		GC8034_REG_SET_PAGE,
		GC8034_SET_PAGE_ZERO);
	ret |= gc8034_write_reg(gc8034->client,
		GC8034_REG_CTRL_MODE,
		GC8034_MODE_SW_STANDBY);

	return ret;
}

static int gc8034_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc8034 *gc8034 = to_gc8034(sd);
	struct i2c_client *client = gc8034->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				gc8034->cur_mode->width,
				gc8034->cur_mode->height,
		DIV_ROUND_CLOSEST(gc8034->cur_mode->max_fps.denominator,
		gc8034->cur_mode->max_fps.numerator));

	mutex_lock(&gc8034->mutex);
	on = !!on;
	if (on == gc8034->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc8034_start_stream(gc8034);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc8034_stop_stream(gc8034);
		pm_runtime_put(&client->dev);
	}

	gc8034->streaming = on;

unlock_and_return:
	mutex_unlock(&gc8034->mutex);

	return ret;
}

static int gc8034_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc8034 *gc8034 = to_gc8034(sd);
	struct i2c_client *client = gc8034->client;
	const struct gc8034_mode *mode = gc8034->cur_mode;
	int ret = 0;

	dev_info(&client->dev, "%s(%d) on(%d)\n", __func__, __LINE__, on);
	mutex_lock(&gc8034->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc8034->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc8034_write_array(gc8034->client, mode->global_reg_list);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		gc8034->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc8034->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc8034->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc8034_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC8034_XVCLK_FREQ / 1000 / 1000);
}

static int gc8034_enable_regulators(struct gc8034 *gc8034,
				    struct regulator_bulk_data *consumers)
{
	int i, j;
	int ret = 0;
	struct device *dev = &gc8034->client->dev;
	int num_consumers = GC8034_NUM_SUPPLIES;

	for (i = 0; i < num_consumers; i++) {

		ret = regulator_enable(consumers[i].consumer);
		if (ret < 0) {
			dev_err(dev, "Failed to enable regulator: %s\n",
				consumers[i].supply);
			goto err;
		}
	}
	return 0;
err:
	for (j = 0; j < i; j++)
		regulator_disable(consumers[j].consumer);

	return ret;
}

static int __gc8034_power_on(struct gc8034 *gc8034)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc8034->client->dev;

	if (!IS_ERR(gc8034->power_gpio))
		gpiod_set_value_cansleep(gc8034->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(gc8034->pins_default)) {
		ret = pinctrl_select_state(gc8034->pinctrl,
					   gc8034->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(gc8034->xvclk, GC8034_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc8034->xvclk) != GC8034_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	if (!IS_ERR(gc8034->reset_gpio))
		gpiod_set_value_cansleep(gc8034->reset_gpio, 1);

	ret = gc8034_enable_regulators(gc8034, gc8034->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	usleep_range(100, 200);
	ret = clk_prepare_enable(gc8034->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	usleep_range(1000, 1100);
	if (!IS_ERR(gc8034->pwdn_gpio))
		gpiod_set_value_cansleep(gc8034->pwdn_gpio, 0);

	usleep_range(500, 1000);
	if (!IS_ERR(gc8034->reset_gpio))
		gpiod_set_value_cansleep(gc8034->reset_gpio, 0);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc8034_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(gc8034->xvclk);

	return ret;
}

static void __gc8034_power_off(struct gc8034 *gc8034)
{
	int ret;

	if (!IS_ERR(gc8034->pwdn_gpio))
		gpiod_set_value_cansleep(gc8034->pwdn_gpio, 1);

	if (!IS_ERR(gc8034->reset_gpio))
		gpiod_set_value_cansleep(gc8034->reset_gpio, 1);

	clk_disable_unprepare(gc8034->xvclk);
	if (!IS_ERR_OR_NULL(gc8034->pins_sleep)) {
		ret = pinctrl_select_state(gc8034->pinctrl,
					   gc8034->pins_sleep);
		if (ret < 0)
			dev_dbg(&gc8034->client->dev, "could not set pins\n");
	}
	if (!IS_ERR(gc8034->power_gpio))
		gpiod_set_value_cansleep(gc8034->power_gpio, 0);

	regulator_bulk_disable(GC8034_NUM_SUPPLIES, gc8034->supplies);
}

static int __maybe_unused gc8034_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc8034 *gc8034 = to_gc8034(sd);

	return __gc8034_power_on(gc8034);
}

static int __maybe_unused gc8034_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc8034 *gc8034 = to_gc8034(sd);

	__gc8034_power_off(gc8034);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc8034_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc8034 *gc8034 = to_gc8034(sd);
	struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc8034_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc8034->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = GC8034_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc8034->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int gc8034_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc8034 *gc8034 = to_gc8034(sd);

	if (fie->index >= gc8034->cfg_num)
		return -EINVAL;

	fie->code = GC8034_MEDIA_BUS_FMT;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int gc8034_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct gc8034 *sensor = to_gc8034(sd);
	struct device *dev = &sensor->client->dev;

	dev_info(dev, "%s(%d) enter!\n", __func__, __LINE__);

	if (2 == sensor->lane_num) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_2_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else if (4 == sensor->lane_num) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_4_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else {
		dev_err(&sensor->client->dev,
			"unsupported lane_num(%d)\n", sensor->lane_num);
	}
	return 0;
}

static const struct dev_pm_ops gc8034_pm_ops = {
	SET_RUNTIME_PM_OPS(gc8034_runtime_suspend,
			gc8034_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc8034_internal_ops = {
	.open = gc8034_open,
};
#endif

static const struct v4l2_subdev_core_ops gc8034_core_ops = {
	.s_power = gc8034_s_power,
	.ioctl = gc8034_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc8034_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc8034_video_ops = {
	.s_stream = gc8034_s_stream,
	.g_frame_interval = gc8034_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc8034_pad_ops = {
	.enum_mbus_code = gc8034_enum_mbus_code,
	.enum_frame_size = gc8034_enum_frame_sizes,
	.enum_frame_interval = gc8034_enum_frame_interval,
	.get_fmt = gc8034_get_fmt,
	.set_fmt = gc8034_set_fmt,
	.get_mbus_config = gc8034_g_mbus_config,
};

static const struct v4l2_subdev_ops gc8034_subdev_ops = {
	.core	= &gc8034_core_ops,
	.video	= &gc8034_video_ops,
	.pad	= &gc8034_pad_ops,
};

static int gc8034_set_exposure_reg(struct gc8034 *gc8034, u32 exposure)
{
	int ret = 0;
	u32 cal_shutter = 0;

	cal_shutter = exposure >> 1;
	cal_shutter = cal_shutter << 1;

	gc8034->Dgain_ratio = 256 * exposure / cal_shutter;
	ret = gc8034_write_reg(gc8034->client,
		GC8034_REG_SET_PAGE, GC8034_SET_PAGE_ZERO);
	ret |= gc8034_write_reg(gc8034->client,
		GC8034_REG_EXPOSURE_H,
		GC8034_FETCH_HIGH_BYTE_EXP(cal_shutter));
	ret |= gc8034_write_reg(gc8034->client,
		GC8034_REG_EXPOSURE_L,
		GC8034_FETCH_LOW_BYTE_EXP(cal_shutter));
	return ret;
}

#define MAX_AG_INDEX		9
#define AGC_REG_NUM		14
#define MEAG_INDEX		7

u16 gain_level[MAX_AG_INDEX] = {
		0x0040, /* 1.000*/
		0x0058, /* 1.375*/
		0x007d, /* 1.950*/
		0x00ad, /* 2.700*/
		0x00f3, /* 3.800*/
		0x0159, /* 5.400*/
		0x01ea, /* 7.660*/
		0x02ac, /*10.688*/
		0x03c2, /*15.030*/
};

u8 agc_register[MAX_AG_INDEX][AGC_REG_NUM] = {
	/* fullsize */
	{ 0x00, 0x55, 0x83, 0x01, 0x06, 0x18, 0x20,
		0x16, 0x17, 0x50, 0x6c, 0x9b, 0xd8, 0x00 },
	{ 0x00, 0x55, 0x83, 0x01, 0x06, 0x18, 0x20,
		0x16, 0x17, 0x50, 0x6c, 0x9b, 0xd8, 0x00 },
	{ 0x00, 0x4e, 0x84, 0x01, 0x0c, 0x2e, 0x2d,
		0x15, 0x19, 0x47, 0x70, 0x9f, 0xd8, 0x00 },
	{ 0x00, 0x51, 0x80, 0x01, 0x07, 0x28, 0x32,
		0x22, 0x20, 0x49, 0x70, 0x91, 0xd9, 0x00 },
	{ 0x00, 0x4d, 0x83, 0x01, 0x0f, 0x3b, 0x3b,
		0x1c, 0x1f, 0x47, 0x6f, 0x9b, 0xd3, 0x00 },
	{ 0x00, 0x50, 0x83, 0x01, 0x08, 0x35, 0x46,
		0x1e, 0x22, 0x4c, 0x70, 0x9a, 0xd2, 0x00 },
	{ 0x00, 0x52, 0x80, 0x01, 0x0c, 0x35, 0x3a,
		0x2b, 0x2d, 0x4c, 0x67, 0x8d, 0xc0, 0x00 },
	{ 0x00, 0x52, 0x80, 0x01, 0x0c, 0x35, 0x3a,
		0x2b, 0x2d, 0x4c, 0x67, 0x8d, 0xc0, 0x00 },
	{ 0x00, 0x52, 0x80, 0x01, 0x0c, 0x35, 0x3a,
		0x2b, 0x2d, 0x4c, 0x67, 0x8d, 0xc0, 0x00 }
};

static int gc8034_set_gain_reg(struct gc8034 *gc8034, u32 a_gain)
{
	int ret = 0;
	u32 temp_gain = 0;
	int gain_index = 0;
	u32 Dgain_ratio = 0;

	Dgain_ratio = gc8034->Dgain_ratio;
	for (gain_index = MEAG_INDEX - 1; gain_index >= 0; gain_index--) {
		if (a_gain >= gain_level[gain_index]) {
			ret = gc8034_write_reg(gc8034->client,
				GC8034_REG_SET_PAGE, GC8034_SET_PAGE_ZERO);
			ret |= gc8034_write_reg(gc8034->client,
				0xb6, gain_index);
			temp_gain = 256 * a_gain / gain_level[gain_index];
			temp_gain = temp_gain * Dgain_ratio / 256;
			ret |= gc8034_write_reg(gc8034->client,
				0xb1, temp_gain >> 8);
			ret |= gc8034_write_reg(gc8034->client,
				0xb2, temp_gain & 0xff);

			ret |= gc8034_write_reg(gc8034->client, 0xfe,
				agc_register[gain_index][0]);
			ret |= gc8034_write_reg(gc8034->client, 0x20,
				agc_register[gain_index][1]);
			ret |= gc8034_write_reg(gc8034->client, 0x33,
				agc_register[gain_index][2]);
			ret |= gc8034_write_reg(gc8034->client, 0xfe,
				agc_register[gain_index][3]);
			ret |= gc8034_write_reg(gc8034->client, 0xdf,
				agc_register[gain_index][4]);
			ret |= gc8034_write_reg(gc8034->client, 0xe7,
				agc_register[gain_index][5]);
			ret |= gc8034_write_reg(gc8034->client, 0xe8,
				agc_register[gain_index][6]);
			ret |= gc8034_write_reg(gc8034->client, 0xe9,
				agc_register[gain_index][7]);
			ret |= gc8034_write_reg(gc8034->client, 0xea,
				agc_register[gain_index][8]);
			ret |= gc8034_write_reg(gc8034->client, 0xeb,
				agc_register[gain_index][9]);
			ret |= gc8034_write_reg(gc8034->client, 0xec,
				agc_register[gain_index][10]);
			ret |= gc8034_write_reg(gc8034->client, 0xed,
				agc_register[gain_index][11]);
			ret |= gc8034_write_reg(gc8034->client, 0xee,
				agc_register[gain_index][12]);
			ret |= gc8034_write_reg(gc8034->client, 0xfe,
				agc_register[gain_index][13]);
			break;
		}
	}
	return ret;
}

static int gc8034_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc8034 *gc8034 = container_of(ctrl->handler,
					struct gc8034, ctrl_handler);
	struct i2c_client *client = gc8034->client;
	s64 max;
	int ret = 0;
	s32 temp;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = gc8034->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc8034->exposure,
					 gc8034->exposure->minimum, max,
					 gc8034->exposure->step,
					 gc8034->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		dev_dbg(&client->dev, "set exposure value 0x%x\n", ctrl->val);
		ret = gc8034_set_exposure_reg(gc8034, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		dev_dbg(&client->dev, "set analog gain value 0x%x\n", ctrl->val);
		ret = gc8034_set_gain_reg(gc8034, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set vb value 0x%x\n", ctrl->val);
		/* VB = VTS - 2448 -36, according android8.1 driver */
		temp = ctrl->val + gc8034->cur_mode->height - 2448 - 36;
		ret = gc8034_write_reg(gc8034->client,
					GC8034_REG_SET_PAGE,
					GC8034_SET_PAGE_ZERO);
		ret |= gc8034_write_reg(gc8034->client,
					GC8034_REG_VTS_H,
					(temp >> 8) & 0xff);
		ret |= gc8034_write_reg(gc8034->client,
					GC8034_REG_VTS_L,
					temp & 0xff);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc8034_ctrl_ops = {
	.s_ctrl = gc8034_set_ctrl,
};

static int gc8034_initialize_controls(struct gc8034 *gc8034)
{
	const struct gc8034_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc8034->ctrl_handler;
	mode = gc8034->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &gc8034->mutex;

	gc8034->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_menu_items) - 1, 0,
				link_freq_menu_items);
	v4l2_ctrl_s_ctrl(gc8034->link_freq, mode->mipi_freq_idx);

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			0, gc8034->pixel_rate, 1, gc8034->pixel_rate);

	h_blank = mode->hts_def - mode->width;
	gc8034->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (gc8034->hblank)
		gc8034->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc8034->vblank = v4l2_ctrl_new_std(handler, &gc8034_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				GC8034_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc8034->exposure = v4l2_ctrl_new_std(handler, &gc8034_ctrl_ops,
				V4L2_CID_EXPOSURE, GC8034_EXPOSURE_MIN,
				exposure_max, GC8034_EXPOSURE_STEP,
				mode->exp_def);

	gc8034->anal_gain = v4l2_ctrl_new_std(handler, &gc8034_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, GC8034_GAIN_MIN,
				GC8034_GAIN_MAX, GC8034_GAIN_STEP,
				GC8034_GAIN_DEFAULT);
	if (handler->error) {
		ret = handler->error;
		dev_err(&gc8034->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc8034->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc8034_check_sensor_id(struct gc8034 *gc8034,
				struct i2c_client *client)
{
	struct device *dev = &gc8034->client->dev;
	u16 id = 0;
	u8 reg_H = 0;
	u8 reg_L = 0;
	int ret;

	ret = gc8034_read_reg(client, GC8034_REG_CHIP_ID_H, &reg_H);
	ret |= gc8034_read_reg(client, GC8034_REG_CHIP_ID_L, &reg_L);
	id = ((reg_H << 8) & 0xff00) | (reg_L & 0xff);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "detected gc%04x sensor\n", id);
	return ret;
}

static int gc8034_configure_regulators(struct gc8034 *gc8034)
{
	unsigned int i;

	for (i = 0; i < GC8034_NUM_SUPPLIES; i++)
		gc8034->supplies[i].supply = gc8034_supply_names[i];

	return devm_regulator_bulk_get(&gc8034->client->dev,
		GC8034_NUM_SUPPLIES,
		gc8034->supplies);
}

static int gc8034_parse_of(struct gc8034 *gc8034)
{
	struct device *dev = &gc8034->client->dev;
	struct device_node *endpoint;
	struct fwnode_handle *fwnode;
	int rval;
	unsigned int fps;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	fwnode = of_fwnode_handle(endpoint);
	rval = fwnode_property_read_u32_array(fwnode, "data-lanes", NULL, 0);
	if (rval <= 0) {
		dev_warn(dev, " Get mipi lane num failed!\n");
		return -1;
	}

	gc8034->lane_num = rval;
	if (4 == gc8034->lane_num) {
		gc8034->cur_mode = &supported_modes_4lane[0];
		supported_modes = supported_modes_4lane;
		gc8034->cfg_num = ARRAY_SIZE(supported_modes_4lane);
		/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		fps = DIV_ROUND_CLOSEST(gc8034->cur_mode->max_fps.denominator,
					gc8034->cur_mode->max_fps.numerator);
		gc8034->pixel_rate = gc8034->cur_mode->vts_def *
				     gc8034->cur_mode->hts_def * fps;

		dev_info(dev, "lane_num(%d)  pixel_rate(%u)\n",
			 gc8034->lane_num, gc8034->pixel_rate);
	} else if (2 == gc8034->lane_num) {
		gc8034->cur_mode = &supported_modes_2lane[0];
		supported_modes = supported_modes_2lane;
		gc8034->cfg_num = ARRAY_SIZE(supported_modes_2lane);
		/*pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		fps = DIV_ROUND_CLOSEST(gc8034->cur_mode->max_fps.denominator,
					gc8034->cur_mode->max_fps.numerator);
		gc8034->pixel_rate = gc8034->cur_mode->vts_def *
				     gc8034->cur_mode->hts_def * fps;
		dev_info(dev, "lane_num(%d)  pixel_rate(%u)\n",
			 gc8034->lane_num, gc8034->pixel_rate);
	} else {
		dev_err(dev, "unsupported lane_num(%d)\n", gc8034->lane_num);
		return -1;
	}

	return 0;
}

static int gc8034_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc8034 *gc8034;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	struct device_node *eeprom_ctrl_node;
	struct i2c_client *eeprom_ctrl_client;
	struct v4l2_subdev *eeprom_ctrl;
	struct otp_info *otp_ptr;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc8034 = devm_kzalloc(dev, sizeof(*gc8034), GFP_KERNEL);
	if (!gc8034)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
		&gc8034->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
		&gc8034->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
		&gc8034->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
		&gc8034->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	gc8034->client = client;

	gc8034->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc8034->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc8034->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(gc8034->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");
	gc8034->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc8034->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc8034->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc8034->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = gc8034_configure_regulators(gc8034);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	ret = gc8034_parse_of(gc8034);
	if (ret != 0)
		return -EINVAL;

	gc8034->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc8034->pinctrl)) {
		gc8034->pins_default =
			pinctrl_lookup_state(gc8034->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc8034->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc8034->pins_sleep =
			pinctrl_lookup_state(gc8034->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc8034->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&gc8034->mutex);

	sd = &gc8034->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc8034_subdev_ops);
	ret = gc8034_initialize_controls(gc8034);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc8034_power_on(gc8034);
	if (ret)
		goto err_free_handler;

	ret = gc8034_check_sensor_id(gc8034, client);
	if (ret)
		goto err_power_off;
#ifdef RK_OTP
	eeprom_ctrl_node = of_parse_phandle(node, "eeprom-ctrl", 0);
	if (eeprom_ctrl_node) {
		eeprom_ctrl_client =
			of_find_i2c_device_by_node(eeprom_ctrl_node);
		of_node_put(eeprom_ctrl_node);
		if (IS_ERR_OR_NULL(eeprom_ctrl_client)) {
			dev_err(dev, "can not get node\n");
			goto continue_probe;
		}
		eeprom_ctrl = i2c_get_clientdata(eeprom_ctrl_client);
		if (IS_ERR_OR_NULL(eeprom_ctrl)) {
			dev_err(dev, "can not get eeprom i2c client\n");
		} else {
			otp_ptr = devm_kzalloc(dev, sizeof(*otp_ptr), GFP_KERNEL);
			if (!otp_ptr)
				return -ENOMEM;
			ret = v4l2_subdev_call(eeprom_ctrl,
				core, ioctl, 0, otp_ptr);
			if (!ret) {
				gc8034->otp = otp_ptr;
			} else {
				gc8034->otp = NULL;
				devm_kfree(dev, otp_ptr);
				dev_warn(dev, "can not get otp info, skip!\n");
			}
		}
	}
continue_probe:
#else
	gc8034_otp_enable(gc8034);
	gc8034_otp_read(gc8034);
	gc8034_otp_disable(gc8034);
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc8034_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc8034->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc8034->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc8034->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc8034->module_index, facing,
		 GC8034_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__gc8034_power_off(gc8034);
err_free_handler:
	v4l2_ctrl_handler_free(&gc8034->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc8034->mutex);

	return ret;
}

static int gc8034_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc8034 *gc8034 = to_gc8034(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc8034->ctrl_handler);
	mutex_destroy(&gc8034->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc8034_power_off(gc8034);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc8034_of_match[] = {
	{ .compatible = "galaxycore,gc8034" },
	{},
};
MODULE_DEVICE_TABLE(of, gc8034_of_match);
#endif

static const struct i2c_device_id gc8034_match_id[] = {
	{ "galaxycore,gc8034", 0},
	{ },
};

static struct i2c_driver gc8034_i2c_driver = {
	.driver = {
		.name = GC8034_NAME,
		.pm = &gc8034_pm_ops,
		.of_match_table = of_match_ptr(gc8034_of_match),
	},
	.probe		= &gc8034_probe,
	.remove		= &gc8034_remove,
	.id_table	= gc8034_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc8034_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc8034_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("GalaxyCore gc8034 sensor driver");
MODULE_LICENSE("GPL v2");
