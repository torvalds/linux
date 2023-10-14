// SPDX-License-Identifier: GPL-2.0
/*
 * os02k10 driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 */
//#define DEBUG

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <linux/rk-preisp.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include "../platform/rockchip/isp/rkisp_tb_helper.h"
#include <linux/rk-preisp.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define OS02K10_LANES			2
#define OS02K10_BITS_PER_SAMPLE		10
#define OS02K10_LINK_FREQ_360M		360000000
#define OS02K10_LINK_FREQ_480M		480000000

#define PIXEL_RATE_WITH_360M_10BIT		(OS02K10_LINK_FREQ_360M * 2 * \
					OS02K10_LANES / OS02K10_BITS_PER_SAMPLE)

#define PIXEL_RATE_WITH_480M_10BIT		(OS02K10_LINK_FREQ_480M * 2 * \
					OS02K10_LANES / OS02K10_BITS_PER_SAMPLE)

#define OS02K10_XVCLK_FREQ		24000000

#define CHIP_ID				0x005302
#define OS02K10_REG_CHIP_ID		0x300a

#define OS02K10_REG_CTRL_MODE		0x0100
#define OS02K10_MODE_SW_STANDBY		0x0
#define OS02K10_MODE_STREAMING		BIT(0)

#define OS02K10_REG_DCG_CONVER_H	0x376c
#define OS02K10_REG_DCG_CONVER_L	0x3c55
#define OS02K10_REG_DCG_RATIO		0x73fe

#define OS02K10_AEC_LONG_EXP_REG_H           0x3501 //bit[7:0] to long exposure[15:8]
#define OS02K10_AEC_LONG_EXP_REG_L           0x3502 //bit[7:0] to long exposure[7:0]
#define OS02K10_AEC_LONG_REL_GAIN_REG_H      0x3508 //bit[3:0] to long real gain[7:4]
#define OS02K10_AEC_LONG_REL_GAIN_REG_L      0x3509 //bit[7:4] to long real gain[3:0]
#define OS02K10_AEC_LONG_DIG_GAIN_REG_H      0x350a //bit[3:0] to long digital gain[13:10]
#define OS02K10_AEC_LONG_DIG_GAIN_REG_M      0x350b //bit[7:0] to long digital gain[9:2]
#define OS02K10_AEC_LONG_DIG_GAIN_REG_L      0x350c //bit[7:6] to long digital gain[1:0]

//#define OS02K10_AEC_SHORT_EXP_REG_H        0x3541 //bit[7:0] to short exposure[15:8]
//#define OS02K10_AEC_SHORT_EXP_REG_L        0x3542 //bit[7:0] to short exposure[7:0]
#define OS02K10_AEC_SHORT_REL_GAIN_REG_H     0x3548 //bit[3:0] to short real gain[7:4]
#define OS02K10_AEC_SHORT_REL_GAIN_REG_L     0x3549 //bit[7:4] to short real gain[3:0]
#define OS02K10_AEC_SHORT_DIG_GAIN_REG_H     0x354a //bit[3:0] to short digital gain[13:10]
#define OS02K10_AEC_SHORT_DIG_GAIN_REG_M     0x354b //bit[7:0] to short digital gain[9:2]
#define OS02K10_AEC_SHORT_DIG_GAIN_REG_L     0x354c //bit[7:6] to short digital gain[1:0]

#define OS02K10_AEC_VERY_SHORT_EXP_REG_H         0x3581 //bit[7:0] to very short exposure[15:8]
#define OS02K10_AEC_VERY_SHORT_EXP_REG_L         0x3582 //bit[7:0] to very short exposure[7:0]
#define OS02K10_AEC_VERY_SHORT_REL_GAIN_REG_H    0x3588 //bit[3:0] to very short real gain[7:4]
#define OS02K10_AEC_VERY_SHORT_REL_GAIN_REG_L    0x3589 //bit[7:4] to very short real gain[3:0]
#define OS02K10_AEC_VERY_SHORT_DIG_GAIN_REG_H    0x358a //bit[3:0] to very short digital gain[13:10]
#define OS02K10_AEC_VERY_SHORT_DIG_GAIN_REG_M    0x358b //bit[7:0] to very short digital gain[9:2]
#define OS02K10_AEC_VERY_SHORT_DIG_GAIN_REG_L    0x358c //bit[7:6] to very short digital gain[1:0]

#define OS02K10_AEC_TIMING_HTS_REG_H         0x380c //bit[7:0] to horizontal timing[15:8] for log and short exposure
#define OS02K10_AEC_TIMING_HTS_REG_L         0x380d //bit[7:0] to horizontal timing[7:0] for log and short exposure
#define OS02K10_AEC_TIMING_VTS_REG_H         0x380e //bit[7:0] to vertical timing[15:8] for log and short exposure
#define OS02K10_AEC_TIMING_VTS_REG_L         0x380f //bit[7:0] to vertical timing[7:0] for log and short exposure
#define OS02K10_AEC_TIMING_VERY_HTS_REG_H    0x384c //bit[7:0] to horizontal timing[15:8] for very short exposure
#define OS02K10_AEC_TIMING_VERY_HTS_REG_L    0x384d //bit[7:0] to horizontal timing[7:0] for very short exposure

#define OS02K10_HORIZONTAL_START_REG_H           0x3800 //bit[7:0] to array horizontal start[15:8]
#define OS02K10_HORIZONTAL_START_REG_L           0x3801 //bit[7:0] to array horizontal start[7:0]
#define OS02K10_VERTICAL_START_REG_H             0x3802 //bit[7:0] to array vertical start[15:8]
#define OS02K10_VERTICAL_START_REG_L             0x3803 //bit[7:0] to array vertical start[15:8]
#define OS02K10_HORIZONTAL_END_REG_H             0x3804 //bit[7:0] to array horizontal end[15:8]
#define OS02K10_HORIZONTAL_END_REG_L             0x3805 //bit[7:0] to array horizontal end[15:8]
#define OS02K10_VERTICAL_END_REG_H               0x3806 //bit[7:0] to array vertical end[15:8]
#define OS02K10_VERTICAL_END_REG_L               0x3807 //bit[7:0] to array vertical end[15:8]
#define OS02K10_HORIZONTAL_OUTPUT_SIZE_REG_H     0x3808 //bit[7:0] to array horizontal output size for final image[15:8]
#define OS02K10_HORIZONTAL_OUTPUT_SIZE_REG_L     0x3809 //bit[7:0] to array horizontal output size for final image[7:0]
#define OS02K10_VERTICAL_OUTPUT_SIZE_REG_H       0x380a //bit[7:0] to array vertical output size for final image[15:8]
#define OS02K10_VERTICAL_OUTPUT_SIZE_REG_L       0x380b //bit[7:0] to array vertical output size for final image[7:0]

#define OS02K10_WINDOW_HORIZONTAL_TRUNC_SIZE_REG_H      0x3810 //bit[7:0] to isp window horizontal truncation size[15:8]
#define OS02K10_WINDOW_HORIZONTAL_TRUNC_SIZE_REG_L      0x3811 //bit[7:0] to isp window horizontal truncation size[7:0]
#define OS02K10_WINDOW_VERTICAL_TRUNC_SIZE_REG_H        0x3812 //bit[7:0] to isp window vertical truncation size[15:8]
#define OS02K10_WINDOW_VERTICAL_TRUNC_SIZE_REG_L        0x3813 //bit[7:0] to isp window vertical truncation size[15:8]

#define OS02K10_FLIPMIRROR_REG                   0x3820
#define OS02K10_FLIPMIRROR_REGBIT_MIRROR         0x02   //bit[1] = 2'b1, mirror
#define OS02K10_FLIPMIRROR_REGBIT_FLIP           0x0c   //bit[3:2] = 2'b11, flip
#define OS02K10_FLIPMIRROR_REGBIT_MIRROR_FLIP    0x0e   //bit[3:1] = 2'b111, mirror and flip
#define OS02K10_FLIPMIRROR_STATE_NORMAL          0
#define OS02K10_FLIPMIRROR_STATE_FLIPMIRROR      1
#define OS02K10_FLIPMIRROR_STATE_MIRROR          2
#define OS02K10_FLIPMIRROR_STATE_FLIP            3

#define OS02K10_FINE_INTG_TIME_MIN               1
#define OS02K10_FINE_INTG_TIME_MAX_MARGIN        0
#define OS02K10_COARSE_INTG_TIME_MIN             16
#define OS02K10_COARSE_INTG_TIME_MAX_MARGIN      8

#define OS02K10_GAIN_MIN            0x10
#define OS02K10_GAIN_MAX	    15863
#define OS02K10_GAIN_STEP           1
#define OS02K10_GAIN_DEFAULT        0x10
#define OS02K10_REAL_GAIN           1
#define OS02K10_SENSOR_GAIN         0
#define OS02K10_VTS_MAX				0xffff
#define	OS02K10_EXPOSURE_MIN		1
#define	OS02K10_EXPOSURE_STEP		1

#define OS02K10_GROUP_UPDATE_ADDRESS	0x3208
#define OS02K10_GROUP_UPDATE_START_DATA	0x00
#define OS02K10_GROUP_UPDATE_END_DATA	0x10
#define OS02K10_GROUP_UPDATE_LAUNCH		0xA0

#define OS02K10_REG_TEST_PATTERN				 0x50C0
#define OS02K10_TEST_PATTERN_BIT_MASK			 BIT(7)	// bit[7] test pattern enable

#define OS02K10_FETCH_MSB_BYTE_EXP(VAL)	(((VAL) >> 8) & 0xFF)	/* 8 Bits */
#define OS02K10_FETCH_LSB_BYTE_EXP(VAL)	((VAL) & 0xFF)	/* 8 Bits */

#define OS02K10_FETCH_LSB_GAIN(VAL)	(((VAL) << 4) & 0xf0)
#define OS02K10_FETCH_MSB_GAIN(VAL)	(((VAL) >> 4) & 0x1f)

#define OS02K10_FETCH_MIRROR(VAL, ENABLE)        (ENABLE ? VAL | 0x02 : VAL & 0xfd)
#define OS02K10_FETCH_FLIP(VAL, ENABLE)          (ENABLE ? VAL | 0x0c : VAL & 0xf3)

#define REG_DELAY			0xFFFE
#define REG_NULL			0xFFFF

#define OS02K10_REG_VALUE_08BIT		1
#define OS02K10_REG_VALUE_16BIT		2
#define OS02K10_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define OS02K10_NAME			"os02k10"

static const char * const os02k10_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OS02K10_NUM_SUPPLIES ARRAY_SIZE(os02k10_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct os02k10_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 mipi_freq_idx;
	u32 bpp;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct os02k10 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OS02K10_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	struct v4l2_fract	cur_fps;
	bool			long_hcg;
	bool			middle_hcg;
	bool			short_hcg;
	bool			streaming;
	bool			power_on;
	const struct os02k10_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	u32			dcg_ratio;
	bool			has_init_exp;
	bool			is_thunderboot;
	bool			is_first_streamoff;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

#define to_os02k10(sd) container_of(sd, struct os02k10, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval os02k10_global_regs[] = {
	{REG_NULL, 0x00},
};

static const struct regval os02k10_linear_10_640x480_regs[] = {
	{0x0103,0x01},
	{REG_DELAY, 0x0a},
	{0x0100,0x00},
	{0x0109,0x01},
	{0x0104,0x02},
	{0x0102,0x00},
	{0x0305,0x5c},
	{0x0306,0x00},
	{0x0307,0x00},
	{0x030a,0x01},
	{0x0317,0x09},
	{0x0323,0x07},
	{0x0324,0x01},
	{0x0325,0xb0},
	{0x0327,0x07},
	{0x032c,0x02},
	{0x032d,0x02},
	{0x032e,0x05},
	{0x300f,0x11},
	{0x3012,0x21},
	{0x3026,0x10},
	{0x3027,0x08},
	{0x302d,0x24},
	{0x3103,0x29},
	{0x3106,0x10},
	{0x3400,0x00},
	{0x3406,0x08},
	{0x3408,0x05},
	{0x340c,0x05},
	{0x3425,0x51},
	{0x3426,0x10},
	{0x3427,0x14},
	{0x3428,0x10},
	{0x3429,0x10},
	{0x342a,0x10},
	{0x342b,0x04},
	{0x3504,0x08},
	{0x3508,0x01},
	{0x3509,0x00},
	{0x3544,0x08},
	{0x3548,0x01},
	{0x3549,0x00},
	{0x3584,0x08},
	{0x3588,0x01},
	{0x3589,0x00},
	{0x3601,0x70},
	{0x3604,0xe3},
	{0x3605,0x7f},
	{0x3606,0x00},
	{0x3608,0xa8},
	{0x360a,0xd0},
	{0x360b,0x08},
	{0x360e,0xc8},
	{0x360f,0x66},
	{0x3610,0x81},
	{0x3611,0x89},
	{0x3612,0x4e},
	{0x3613,0xbd},
	{0x362a,0x0e},
	{0x362b,0x0e},
	{0x362c,0x0e},
	{0x362d,0x0e},
	{0x362e,0x0c},
	{0x362f,0x1a},
	{0x3630,0x32},
	{0x3631,0x64},
	{0x3638,0x00},
	{0x3643,0x00},
	{0x3644,0x00},
	{0x3645,0x00},
	{0x3646,0x00},
	{0x3647,0x00},
	{0x3648,0x00},
	{0x3649,0x00},
	{0x364a,0x04},
	{0x364c,0x0e},
	{0x364d,0x0e},
	{0x364e,0x0e},
	{0x364f,0x0e},
	{0x3650,0xff},
	{0x3651,0xff},
	{0x3661,0x07},
	{0x3662,0x02},
	{0x3663,0x20},
	{0x3665,0x12},
	{0x3667,0xd4},
	{0x3668,0x80},
	{0x366f,0x00},
	{0x3670,0xc7},
	{0x3671,0x08},
	{0x3673,0x2a},
	{0x3681,0x80},
	{0x3700,0x26},
	{0x3701,0x1e},
	{0x3702,0x25},
	{0x3703,0x28},
	{0x3706,0x3e},
	{0x3707,0x0a},
	{0x3708,0x36},
	{0x3709,0x55},
	{0x370a,0x00},
	{0x370b,0xa3},
	{0x3714,0x03},
	{0x371b,0x16},
	{0x371c,0x00},
	{0x371d,0x08},
	{0x3756,0x9b},
	{0x3757,0x9b},
	{0x3762,0x1d},
	{0x376c,0x10},
	{0x3776,0x05},
	{0x3777,0x22},
	{0x3779,0x60},
	{0x377c,0x48},
	{0x3783,0x02},
	{0x3784,0x06},
	{0x3785,0x0a},
	{0x3790,0x10},
	{0x3793,0x04},
	{0x3794,0x07},
	{0x3796,0x00},
	{0x3797,0x02},
	{0x379c,0x4d},
	{0x37a1,0x80},
	{0x37bb,0x88},
	{0x37bd,0x01},
	{0x37be,0x01},
	{0x37bf,0x00},
	{0x37c0,0x01},
	{0x37c7,0x56},
	{0x37ca,0x21},
	{0x37cc,0x13},
	{0x37cd,0x90},
	{0x37cf,0x04},
	{0x37d1,0x3e},
	{0x37d2,0x00},
	{0x37d3,0xa3},
	{0x37d5,0x3e},
	{0x37d6,0x00},
	{0x37d7,0xa3},
	{0x37d8,0x01},
	{0x37da,0x00},
	{0x37db,0x00},
	{0x37dc,0x00},
	{0x37dd,0x00},
	{0x3800,0x00},
	{0x3801,0x00},
	{0x3802,0x00},
	{0x3803,0x04},
	{0x3804,0x07},
	{0x3805,0x8f},
	{0x3806,0x04},
	{0x3807,0x43},
	{0x3808,0x07},
	{0x3809,0x80},
	{0x380a,0x04},
	{0x380b,0x38},
	{0x380c,0x02},
	{0x380d,0xd0},
	{0x380e,0x04},
	{0x380f,0xe2},
	{0x3811,0x08},
	{0x3813,0x04},
	{0x3814,0x03},
	{0x3815,0x01},
	{0x3816,0x03},
	{0x3817,0x01},
	{0x381c,0x00},
	{0x3820,0x02},
	{0x3821,0x09},
	{0x3822,0x14},
	{0x3833,0x41},
	{0x384c,0x02},
	{0x384d,0xd0},
	{0x3858,0x0d},
	{0x3865,0x00},
	{0x3866,0xc0},
	{0x3867,0x00},
	{0x3868,0xc0},
	{0x3900,0x13},
	{0x3940,0x13},
	{0x3980,0x13},
	{0x390c,0x03},
	{0x390d,0x02},
	{0x390e,0x01},
	{0x390f,0x03},
	{0x3910,0x02},
	{0x3911,0x01},
	{0x394c,0x02},
	{0x394d,0x02},
	{0x394e,0x01},
	{0x394f,0x02},
	{0x3950,0x02},
	{0x3951,0x01},
	{0x398c,0x02},
	{0x398d,0x01},
	{0x398e,0x01},
	{0x398f,0x02},
	{0x3990,0x01},
	{0x3991,0x01},
	{0x5395,0x38},
	{0x5392,0x14},
	{0x5396,0x02},
	{0x5397,0x01},
	{0x5398,0x01},
	{0x5399,0x02},
	{0x539a,0x01},
	{0x539b,0x01},
	{0x5415,0x38},
	{0x5412,0x14},
	{0x5416,0x01},
	{0x5417,0x01},
	{0x5418,0x01},
	{0x5419,0x01},
	{0x541a,0x01},
	{0x541b,0x01},
	{0x5495,0x38},
	{0x5492,0x14},
	{0x5496,0x01},
	{0x5497,0x01},
	{0x5498,0x01},
	{0x5499,0x01},
	{0x549a,0x01},
	{0x549b,0x01},
	{0x3c01,0x11},
	{0x3c05,0x00},
	{0x3c0f,0x1c},
	{0x3c12,0x0d},
	{0x3c14,0x21},
	{0x3c19,0x01},
	{0x3c21,0x40},
	{0x3c3b,0x18},
	{0x3c3d,0xc9},
	{0x3c55,0x08},
	{0x3c5d,0xcf},
	{0x3c5e,0xcf},
	{0x3ce0,0x00},
	{0x3ce1,0x00},
	{0x3ce2,0x00},
	{0x3ce3,0x00},
	{0x3d8c,0x70},
	{0x3d8d,0x10},
	{0x4001,0x2f},
	{0x4033,0x80},
	{0x4008,0x02},
	{0x4009,0x07},
	{0x4004,0x00},
	{0x4005,0x40},
	{0x400a,0x01},
	{0x400b,0x3c},
	{0x400e,0x40},
	{0x4011,0xbb},
	{0x410f,0x01},
	{0x4028,0x6f},
	{0x4029,0x0f},
	{0x402a,0x3f},
	{0x402b,0x01},
	{0x402e,0x00},
	{0x402f,0x40},
	{0x4030,0x00},
	{0x4031,0x40},
	{0x4032,0x2f},
	{0x4050,0x00},
	{0x4051,0x03},
	{0x4288,0xcf},
	{0x4289,0x03},
	{0x428a,0x46},
	{0x430b,0x0f},
	{0x430c,0xfc},
	{0x430d,0x00},
	{0x430e,0x00},
	{0x4314,0x04},
	{0x4500,0x1a},
	{0x4501,0x18},
	{0x4504,0x00},
	{0x4507,0x02},
	{0x4508,0x1a},
	{0x4603,0x00},
	{0x4640,0x62},
	{0x4646,0xaa},
	{0x4647,0x55},
	{0x4648,0x99},
	{0x4649,0x66},
	{0x464d,0x00},
	{0x4654,0x11},
	{0x4655,0x22},
	{0x4800,0x04},
	{0x4810,0xff},
	{0x4811,0xff},
	{0x480e,0x00},
	{0x4813,0x00},
	{0x4837,0x0e},
	{0x484b,0x27},
	{0x4d00,0x4e},
	{0x4d01,0x0c},
	{0x4d02,0xb8},
	{0x4d03,0xea},
	{0x4d04,0x74},
	{0x4d05,0xb7},
	{0x4d09,0x4f},
	{0x5000,0x1f},
	{0x5080,0x00},
	{0x50c0,0x00},
	{0x5100,0x00},
	{0x5200,0x00},
	{0x5201,0x70},
	{0x5202,0x03},
	{0x5203,0x7f},
	{0x5780,0x53},
	{0x5786,0x01},
	{0x3501,0x02},
	{0x3800,0x00},
	{0x3801,0x00},
	{0x3802,0x00},
	{0x3803,0x00},
	{0x3804,0x07},
	{0x3805,0x8f},
	{0x3806,0x04},
	{0x3807,0x47},
	{0x3808,0x02},
	{0x3809,0x80},
	{0x380a,0x01},
	{0x380b,0xe0},
	{0x3811,0xa0},
	{0x3813,0x1e},
	{0x0305,0x50},
	{0x4837,0x10},
	{0x380c,0x05},
	{0x380d,0xa0},
	{0x380e,0x02},
	{0x380f,0x71},
	{0x3501,0x02},
	{0x3502,0x69},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 360Mbps, 2lane
 */
static const struct regval os02k10_linear_10_1920x1080_regs[] = {
	{0x0103,0x01},
	{REG_DELAY, 0x0a},
	{0x0100,0x00},
	{0x0109,0x01},
	{0x0104,0x02},
	{0x0102,0x00},
	{0x0305,0x5c},
	{0x0306,0x00},
	{0x0307,0x00},
	{0x030a,0x01},
	{0x0317,0x09},
	{0x0323,0x07},
	{0x0324,0x01},
	{0x0325,0xb0},
	{0x0327,0x07},
	{0x032c,0x02},
	{0x032d,0x02},
	{0x032e,0x05},
	{0x300f,0x11},
	{0x3012,0x21},
	{0x3026,0x10},
	{0x3027,0x08},
	{0x302d,0x24},
	{0x3103,0x29},
	{0x3106,0x10},
	{0x3400,0x00},
	{0x3406,0x08},
	{0x3408,0x05},
	{0x340c,0x05},
	{0x3425,0x51},
	{0x3426,0x10},
	{0x3427,0x14},
	{0x3428,0x10},
	{0x3429,0x10},
	{0x342a,0x10},
	{0x342b,0x04},
	{0x3504,0x08},
	{0x3508,0x01},
	{0x3509,0x00},
	{0x3544,0x08},
	{0x3548,0x01},
	{0x3549,0x00},
	{0x3584,0x08},
	{0x3588,0x01},
	{0x3589,0x00},
	{0x3601,0x70},
	{0x3604,0xe3},
	{0x3605,0x7f},
	{0x3606,0x00},
	{0x3608,0xa8},
	{0x360a,0xd0},
	{0x360b,0x08},
	{0x360e,0xc8},
	{0x360f,0x66},
	{0x3610,0x81},
	{0x3611,0x89},
	{0x3612,0x4e},
	{0x3613,0xbd},
	{0x362a,0x0e},
	{0x362b,0x0e},
	{0x362c,0x0e},
	{0x362d,0x0e},
	{0x362e,0x0c},
	{0x362f,0x1a},
	{0x3630,0x32},
	{0x3631,0x64},
	{0x3638,0x00},
	{0x3643,0x00},
	{0x3644,0x00},
	{0x3645,0x00},
	{0x3646,0x00},
	{0x3647,0x00},
	{0x3648,0x00},
	{0x3649,0x00},
	{0x364a,0x04},
	{0x364c,0x0e},
	{0x364d,0x0e},
	{0x364e,0x0e},
	{0x364f,0x0e},
	{0x3650,0xff},
	{0x3651,0xff},
	{0x3661,0x07},
	{0x3662,0x02},
	{0x3663,0x20},
	{0x3665,0x12},
	{0x3667,0xd4},
	{0x3668,0x80},
	{0x366f,0x00},
	{0x3670,0xc7},
	{0x3671,0x08},
	{0x3673,0x2a},
	{0x3681,0x80},
	{0x3700,0x26},
	{0x3701,0x1e},
	{0x3702,0x25},
	{0x3703,0x28},
	{0x3706,0x3e},
	{0x3707,0x0a},
	{0x3708,0x36},
	{0x3709,0x55},
	{0x370a,0x00},
	{0x370b,0xa3},
	{0x3714,0x01},
	{0x371b,0x16},
	{0x371c,0x00},
	{0x371d,0x08},
	{0x3756,0x9b},
	{0x3757,0x9b},
	{0x3762,0x1d},
	{0x376c,0x00},
	{0x3776,0x05},
	{0x3777,0x22},
	{0x3779,0x60},
	{0x377c,0x48},
	{0x3783,0x02},
	{0x3784,0x06},
	{0x3785,0x0a},
	{0x3790,0x10},
	{0x3793,0x04},
	{0x3794,0x07},
	{0x3796,0x00},
	{0x3797,0x02},
	{0x379c,0x4d},
	{0x37a1,0x80},
	{0x37bb,0x88},
	{0x37bd,0x01},
	{0x37be,0x01},
	{0x37bf,0x00},
	{0x37c0,0x01},
	{0x37c7,0x56},
	{0x37ca,0x21},
	{0x37cc,0x13},
	{0x37cd,0x90},
	{0x37cf,0x02},
	{0x37d1,0x3e},
	{0x37d2,0x00},
	{0x37d3,0xa3},
	{0x37d5,0x3e},
	{0x37d6,0x00},
	{0x37d7,0xa3},
	{0x37d8,0x01},
	{0x37da,0x00},
	{0x37db,0x00},
	{0x37dc,0x00},
	{0x37dd,0x00},
	{0x3800,0x00},
	{0x3801,0x00},
	{0x3802,0x00},
	{0x3803,0x04},
	{0x3804,0x07},
	{0x3805,0x8f},
	{0x3806,0x04},
	{0x3807,0x43},
	{0x3808,0x07},
	{0x3809,0x80},
	{0x380a,0x04},
	{0x380b,0x38},
	{0x380c,0x05},
	{0x380d,0xa0},
	{0x380e,0x09},
	{0x380f,0xc0},
	{0x3811,0x08},
	{0x3813,0x04},
	{0x3814,0x01},
	{0x3815,0x01},
	{0x3816,0x01},
	{0x3817,0x01},
	{0x381c,0x00},
	{0x3820,0x02},
	{0x3821,0x00},
	{0x3822,0x14},
	{0x3833,0x41},
	{0x384c,0x02},
	{0x384d,0xd0},
	{0x3858,0x0d},
	{0x3865,0x00},
	{0x3866,0xc0},
	{0x3867,0x00},
	{0x3868,0xc0},
	{0x3900,0x13},
	{0x3940,0x13},
	{0x3980,0x13},
	{0x390c,0x03},
	{0x390d,0x02},
	{0x390e,0x01},
	{0x390f,0x03},
	{0x3910,0x02},
	{0x3911,0x01},
	{0x394c,0x02},
	{0x394d,0x02},
	{0x394e,0x01},
	{0x394f,0x02},
	{0x3950,0x02},
	{0x3951,0x01},
	{0x398c,0x02},
	{0x398d,0x01},
	{0x398e,0x01},
	{0x398f,0x02},
	{0x3990,0x01},
	{0x3991,0x01},
	{0x5395,0x38},
	{0x5392,0x14},
	{0x5396,0x02},
	{0x5397,0x01},
	{0x5398,0x01},
	{0x5399,0x02},
	{0x539a,0x01},
	{0x539b,0x01},
	{0x5415,0x38},
	{0x5412,0x14},
	{0x5416,0x01},
	{0x5417,0x01},
	{0x5418,0x01},
	{0x5419,0x01},
	{0x541a,0x01},
	{0x541b,0x01},
	{0x5495,0x38},
	{0x5492,0x14},
	{0x5496,0x01},
	{0x5497,0x01},
	{0x5498,0x01},
	{0x5499,0x01},
	{0x549a,0x01},
	{0x549b,0x01},
	{0x3c01,0x11},
	{0x3c05,0x00},
	{0x3c0f,0x1c},
	{0x3c12,0x0d},
	{0x3c14,0x21},
	{0x3c19,0x01},
	{0x3c21,0x40},
	{0x3c3b,0x18},
	{0x3c3d,0xc9},
	{0x3c55,0xcb},
	{0x3c5d,0xcf},
	{0x3c5e,0xcf},
	{0x3ce0,0x00},
	{0x3ce1,0x00},
	{0x3ce2,0x00},
	{0x3ce3,0x00},
	{0x3d8c,0x70},
	{0x3d8d,0x10},
	{0x4001,0x2f},
	{0x4033,0x80},
	{0x4008,0x02},
	{0x4009,0x11},
	{0x4004,0x00},
	{0x4005,0x40},
	{0x400a,0x01},
	{0x400b,0x3c},
	{0x400e,0x40},
	{0x4011,0xbb},
	{0x410f,0x01},
	{0x4028,0x6f},
	{0x4029,0x0f},
	{0x402a,0x3f},
	{0x402b,0x01},
	{0x402e,0x00},
	{0x402f,0x40},
	{0x4030,0x00},
	{0x4031,0x40},
	{0x4032,0x2f},
	{0x4050,0x00},
	{0x4051,0x07},
	{0x4288,0xcf},
	{0x4289,0x03},
	{0x428a,0x46},
	{0x430b,0x0f},
	{0x430c,0xfc},
	{0x430d,0x00},
	{0x430e,0x00},
	{0x4314,0x04},
	{0x4500,0x18},
	{0x4501,0x18},
	{0x4504,0x00},
	{0x4507,0x02},
	{0x4508,0x1a},
	{0x4603,0x00},
	{0x4640,0x62},
	{0x4646,0xaa},
	{0x4647,0x55},
	{0x4648,0x99},
	{0x4649,0x66},
	{0x464d,0x00},
	{0x4654,0x11},
	{0x4655,0x22},
	{0x4800,0x04},
	{0x4810,0xff},
	{0x4811,0xff},
	{0x480e,0x00},
	{0x4813,0x00},
	{0x4837,0x0e},
	{0x484b,0x27},
	{0x4d00,0x4e},
	{0x4d01,0x0c},
	{0x4d02,0xb8},
	{0x4d03,0xea},
	{0x4d04,0x74},
	{0x4d05,0xb7},
	{0x4d09,0x4f},
	{0x5000,0x1f},
	{0x5080,0x00},
	{0x50c0,0x00},
	{0x5100,0x00},
	{0x5200,0x00},
	{0x5201,0x70},
	{0x5202,0x03},
	{0x5203,0x7f},
	{0x5780,0x53},
	{0x5786,0x01},
	{0x3501,0x02},
	{0x0305,0x50},
	{0x4837,0x10},
	{0x380c,0x05},
	{0x380d,0xa0},
	{0x380e,0x09},
	{0x380f,0xc0},
	{0x3501,0x04},
	{0x3502,0xda},
	{REG_NULL, 0x00},
};

static const struct os02k10_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x04da,
		.hts_def = 0x05a0,
		.vts_def = 0x09c0,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = os02k10_linear_10_1920x1080_regs,
		.hdr_mode = NO_HDR,
		.bpp = 10,
		.mipi_freq_idx = 1,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 640,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 1200000,
		},
		.exp_def = 0x0269,
		.hts_def = 0x05a0,
		.vts_def = 0x0271,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = os02k10_linear_10_640x480_regs,
		.hdr_mode = NO_HDR,
		.bpp = 10,
		.mipi_freq_idx = 1,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	OS02K10_LINK_FREQ_360M,
	OS02K10_LINK_FREQ_480M
};

static const char * const os02k10_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int os02k10_write_reg(struct i2c_client *client, u16 reg,
			    u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int os02k10_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		if(regs[i].addr == REG_DELAY){
			usleep_range(regs[i].val * 1000, (regs[i].val + 1)  * 1000);
		}
		else{
			ret = os02k10_write_reg(client, regs[i].addr,
					OS02K10_REG_VALUE_08BIT, regs[i].val);
		}
	return ret;
}

/* Read registers up to 4 at a time */
static int os02k10_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
			    u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static int os02k10_set_hdrae(struct os02k10 *os02k10,
			    struct preisp_hdrae_exp_s *ae)
{
	int ret = 0;

	return ret;
}

static int os02k10_get_reso_dist(const struct os02k10_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct os02k10_mode *
os02k10_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = os02k10_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int os02k10_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct os02k10 *os02k10 = to_os02k10(sd);
	const struct os02k10_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;

	mutex_lock(&os02k10->mutex);

	mode = os02k10_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&os02k10->mutex);
		return -ENOTTY;
#endif
	} else {
		os02k10->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(os02k10->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(os02k10->vblank, vblank_def,
					 OS02K10_VTS_MAX - mode->height,
					 1, vblank_def);

		__v4l2_ctrl_s_ctrl(os02k10->link_freq, mode->mipi_freq_idx);
		pixel_rate = (u32)link_freq_menu_items[mode->mipi_freq_idx] /
			     mode->bpp * 2 * OS02K10_LANES;
		__v4l2_ctrl_s_ctrl_int64(os02k10->pixel_rate, pixel_rate);
		os02k10->cur_fps = mode->max_fps;
	}

	mutex_unlock(&os02k10->mutex);

	return 0;
}

static int os02k10_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct os02k10 *os02k10 = to_os02k10(sd);
	const struct os02k10_mode *mode = os02k10->cur_mode;

	mutex_lock(&os02k10->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&os02k10->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		/* format info: width/height/data type/virctual channel */
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&os02k10->mutex);

	return 0;
}

static int os02k10_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct os02k10 *os02k10 = to_os02k10(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = os02k10->cur_mode->bus_fmt;

	return 0;
}

static int os02k10_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int os02k10_enable_test_pattern(struct os02k10 *os02k10, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = os02k10_read_reg(os02k10->client, OS02K10_REG_TEST_PATTERN,
			       OS02K10_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= OS02K10_TEST_PATTERN_BIT_MASK;
	else
		val &= ~OS02K10_TEST_PATTERN_BIT_MASK;

	ret |= os02k10_write_reg(os02k10->client, OS02K10_REG_TEST_PATTERN,
				 OS02K10_REG_VALUE_08BIT, val);
	return ret;
}

static int os02k10_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct os02k10 *os02k10 = to_os02k10(sd);
	const struct os02k10_mode *mode = os02k10->cur_mode;

	if (os02k10->streaming)
		fi->interval = os02k10->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int os02k10_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				 struct v4l2_mbus_config *config)
{
	struct os02k10 *os02k10 = to_os02k10(sd);
	const struct os02k10_mode *mode = os02k10->cur_mode;
	u32 val = 1 << (OS02K10_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	if (mode->hdr_mode != NO_HDR)
		val |= V4L2_MBUS_CSI2_CHANNEL_1;
	if (mode->hdr_mode == HDR_X3)
		val |= V4L2_MBUS_CSI2_CHANNEL_2;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void os02k10_get_module_inf(struct os02k10 *os02k10,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, OS02K10_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, os02k10->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, os02k10->len_name, sizeof(inf->base.lens));
}

static int os02k10_set_conversion_gain(struct os02k10 *os02k10, u32 *cg)
{
	int ret = 0;
	struct i2c_client *client = os02k10->client;
	u32 cur_cg = *cg;
	u32 val_h = 0, val_l = 0;

	mutex_lock(&os02k10->mutex);
	dev_dbg(&os02k10->client->dev, "set conversion gain %d, long_hcg: %d\n", cur_cg, os02k10->long_hcg);
	if (cur_cg == GAIN_MODE_LCG) {
		val_l = 0x08;
		val_h = 0x10;
		os02k10->long_hcg = false;
	} else if (cur_cg == GAIN_MODE_HCG) {
		val_l = 0xcb;
		val_h = 0x00;
		os02k10->long_hcg = true;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	ret |= os02k10_write_reg(client,
				 OS02K10_REG_DCG_CONVER_H,
				 OS02K10_REG_VALUE_08BIT,
				 val_h);
	ret |= os02k10_write_reg(client,
				 OS02K10_REG_DCG_CONVER_L,
				 OS02K10_REG_VALUE_08BIT,
				 val_l);
	dev_dbg(&client->dev, "set conversion gain %d, (reg_h reg_l, val_h val_l)=(0x%x 0x%x, 0x%x 0x%x)\n",
		cur_cg, OS02K10_REG_DCG_CONVER_H, OS02K10_REG_DCG_CONVER_L,val_h, val_l);
	pm_runtime_put(&client->dev);
	mutex_unlock(&os02k10->mutex);
	return ret;
}

static long os02k10_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct os02k10 *os02k10 = to_os02k10(sd);
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_dcg_ratio *dcg;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		os02k10_get_module_inf(os02k10, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = os02k10->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = os02k10->cur_mode->width;
		h = os02k10->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				os02k10->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&os02k10->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = os02k10->cur_mode->hts_def - os02k10->cur_mode->width;
			h = os02k10->cur_mode->vts_def - os02k10->cur_mode->height;
			__v4l2_ctrl_modify_range(os02k10->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(os02k10->vblank, h,
						 OS02K10_VTS_MAX - os02k10->cur_mode->height, 1, h);
			os02k10->cur_fps = os02k10->cur_mode->max_fps;
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		os02k10_set_hdrae(os02k10, arg);
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = os02k10_set_conversion_gain(os02k10, (u32 *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = os02k10_write_reg(os02k10->client, OS02K10_REG_CTRL_MODE,
				 OS02K10_REG_VALUE_08BIT, OS02K10_MODE_STREAMING);
		else
			ret = os02k10_write_reg(os02k10->client, OS02K10_REG_CTRL_MODE,
				 OS02K10_REG_VALUE_08BIT, OS02K10_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_DCG_RATIO:
		if (os02k10->dcg_ratio == 0)
			return -EINVAL;
		dcg = (struct rkmodule_dcg_ratio *)arg;
		dcg->integer = (os02k10->dcg_ratio >> 8) & 0xff;
		dcg->decimal = os02k10->dcg_ratio & 0xff;
		dcg->div_coeff = 256;
		dev_info(&os02k10->client->dev,
			 "get dcg ratio integer %d, decimal %d div_coeff %d\n",
			 dcg->integer, dcg->decimal, dcg->div_coeff);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long os02k10_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_dcg_ratio *dcg;
	struct preisp_hdrae_exp_s *hdrae;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = os02k10_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				return -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = os02k10_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret)
				return -EFAULT;
		}
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(hdr, up, sizeof(*hdr))) {
			kfree(hdr);
			return -EFAULT;
		}

		ret = os02k10_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae = kzalloc(sizeof(*hdrae), GFP_KERNEL);
		if (!hdrae) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(hdrae, up, sizeof(*hdrae))) {
			kfree(hdrae);
			return -EFAULT;
		}

		ret = os02k10_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;

		ret = os02k10_ioctl(sd, cmd, &stream);
		break;
	case RKMODULE_GET_DCG_RATIO:
		dcg = kzalloc(sizeof(*dcg), GFP_KERNEL);
		if (!dcg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = os02k10_ioctl(sd, cmd, dcg);
		if (!ret) {
			ret = copy_to_user(up, dcg, sizeof(*dcg));
			if (ret)
				return -EFAULT;
		}
		kfree(dcg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int os02k10_init_conversion_gain(struct os02k10 *os02k10, u32 cur_cg)
{
	int ret = 0;
	struct i2c_client *client = os02k10->client;
	u32 val_h = 0, val_l = 0;

	if (cur_cg == GAIN_MODE_LCG) {
		val_l = 0x08;
		val_h = 0x10;
	} else if (cur_cg == GAIN_MODE_HCG) {
		val_l = 0xcb;
		val_h = 0x00;
	}
	ret |= os02k10_write_reg(client,
				 OS02K10_REG_DCG_CONVER_H,
				 OS02K10_REG_VALUE_08BIT,
				 val_h);
	ret |= os02k10_write_reg(client,
				 OS02K10_REG_DCG_CONVER_L,
				 OS02K10_REG_VALUE_08BIT,
				 val_l);
	dev_dbg(&client->dev, "init conversion gain %d, (reg_h reg_l, val_h val_l)=(0x%x 0x%x, 0x%x 0x%x)\n",
		cur_cg, OS02K10_REG_DCG_CONVER_H, OS02K10_REG_DCG_CONVER_L, val_h, val_l);
	return ret;
}

static int __os02k10_start_stream(struct os02k10 *os02k10)
{
	int ret;

	if (!os02k10->is_thunderboot) {
		ret = os02k10_write_array(os02k10->client, os02k10->cur_mode->reg_list);
		if (ret)
			return ret;
		/* In case these controls are set before streaming */
		ret = __v4l2_ctrl_handler_setup(&os02k10->ctrl_handler);
		if (ret)
			return ret;
		if (os02k10->has_init_exp && os02k10->cur_mode->hdr_mode != NO_HDR) {
			ret = os02k10_ioctl(&os02k10->subdev, PREISP_CMD_SET_HDRAE_EXP,
				&os02k10->init_hdrae_exp);
			if (ret) {
				dev_err(&os02k10->client->dev,
					"init exp fail in hdr mode\n");
				return ret;
			}
		}
		os02k10_init_conversion_gain(os02k10, os02k10->long_hcg);
	}
	return os02k10_write_reg(os02k10->client, OS02K10_REG_CTRL_MODE,
				 OS02K10_REG_VALUE_08BIT, OS02K10_MODE_STREAMING);
}

static int __os02k10_stop_stream(struct os02k10 *os02k10)
{
	os02k10->has_init_exp = false;
	if (os02k10->is_thunderboot) {
		os02k10->is_first_streamoff = true;
		pm_runtime_put(&os02k10->client->dev);
	}
	return os02k10_write_reg(os02k10->client, OS02K10_REG_CTRL_MODE,
				 OS02K10_REG_VALUE_08BIT, OS02K10_MODE_SW_STANDBY);
}

static int __os02k10_power_on(struct os02k10 *os02k10);
static int os02k10_s_stream(struct v4l2_subdev *sd, int on)
{
	struct os02k10 *os02k10 = to_os02k10(sd);
	struct i2c_client *client = os02k10->client;
	int ret = 0;

	mutex_lock(&os02k10->mutex);
	on = !!on;
	if (on == os02k10->streaming)
		goto unlock_and_return;

	if (on) {
		if (os02k10->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			os02k10->is_thunderboot = false;
			__os02k10_power_on(os02k10);
		}
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __os02k10_start_stream(os02k10);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__os02k10_stop_stream(os02k10);
		pm_runtime_put(&client->dev);
	}

	os02k10->streaming = on;

unlock_and_return:
	mutex_unlock(&os02k10->mutex);

	return ret;
}

static int os02k10_s_power(struct v4l2_subdev *sd, int on)
{
	struct os02k10 *os02k10 = to_os02k10(sd);
	struct i2c_client *client = os02k10->client;
	int ret = 0;

	mutex_lock(&os02k10->mutex);

	/* If the power state is not modified - no work to do. */
	if (os02k10->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		if (!os02k10->is_thunderboot) {
			ret = os02k10_write_array(os02k10->client, os02k10_global_regs);
			if (ret) {
				v4l2_err(sd, "could not set init registers\n");
				pm_runtime_put_noidle(&client->dev);
				goto unlock_and_return;
			}
		}

		os02k10->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		os02k10->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&os02k10->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 os02k10_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OS02K10_XVCLK_FREQ / 1000 / 1000);
}

static int __os02k10_power_on(struct os02k10 *os02k10)
{
	int ret;
	u32 delay_us;
	struct device *dev = &os02k10->client->dev;

	if (!IS_ERR_OR_NULL(os02k10->pins_default)) {
		ret = pinctrl_select_state(os02k10->pinctrl,
					   os02k10->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(os02k10->xvclk, OS02K10_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(os02k10->xvclk) != OS02K10_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(os02k10->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (os02k10->is_thunderboot)
		return 0;

	if (!IS_ERR(os02k10->reset_gpio))
		gpiod_set_value_cansleep(os02k10->reset_gpio, 0);

	ret = regulator_bulk_enable(OS02K10_NUM_SUPPLIES, os02k10->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(os02k10->reset_gpio))
		gpiod_set_value_cansleep(os02k10->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(os02k10->pwdn_gpio))
		gpiod_set_value_cansleep(os02k10->pwdn_gpio, 1);

	if (!IS_ERR(os02k10->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = os02k10_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(os02k10->xvclk);

	return ret;
}

static void __os02k10_power_off(struct os02k10 *os02k10)
{
	int ret;
	struct device *dev = &os02k10->client->dev;

	clk_disable_unprepare(os02k10->xvclk);
	if (os02k10->is_thunderboot) {
		if (os02k10->is_first_streamoff) {
			os02k10->is_thunderboot = false;
			os02k10->is_first_streamoff = false;
		} else {
			return;
		}
	}
	if (!IS_ERR(os02k10->pwdn_gpio))
		gpiod_set_value_cansleep(os02k10->pwdn_gpio, 0);
	if (!IS_ERR(os02k10->reset_gpio))
		gpiod_set_value_cansleep(os02k10->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(os02k10->pins_sleep)) {
		ret = pinctrl_select_state(os02k10->pinctrl,
					   os02k10->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(OS02K10_NUM_SUPPLIES, os02k10->supplies);
}

static int os02k10_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os02k10 *os02k10 = to_os02k10(sd);

	return __os02k10_power_on(os02k10);
}

static int os02k10_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os02k10 *os02k10 = to_os02k10(sd);

	__os02k10_power_off(os02k10);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int os02k10_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct os02k10 *os02k10 = to_os02k10(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct os02k10_mode *def_mode = &supported_modes[0];

	mutex_lock(&os02k10->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&os02k10->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int os02k10_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops os02k10_pm_ops = {
	SET_RUNTIME_PM_OPS(os02k10_runtime_suspend,
			   os02k10_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops os02k10_internal_ops = {
	.open = os02k10_open,
};
#endif

static const struct v4l2_subdev_core_ops os02k10_core_ops = {
	.s_power = os02k10_s_power,
	.ioctl = os02k10_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = os02k10_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops os02k10_video_ops = {
	.s_stream = os02k10_s_stream,
	.g_frame_interval = os02k10_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops os02k10_pad_ops = {
	.enum_mbus_code = os02k10_enum_mbus_code,
	.enum_frame_size = os02k10_enum_frame_sizes,
	.enum_frame_interval = os02k10_enum_frame_interval,
	.get_fmt = os02k10_get_fmt,
	.set_fmt = os02k10_set_fmt,
	.get_mbus_config = os02k10_g_mbus_config,
};

static const struct v4l2_subdev_ops os02k10_subdev_ops = {
	.core	= &os02k10_core_ops,
	.video	= &os02k10_video_ops,
	.pad	= &os02k10_pad_ops,
};

static void os02k10_modify_fps_info(struct os02k10 *os02k10)
{
	const struct os02k10_mode *mode = os02k10->cur_mode;

	os02k10->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
				       os02k10->cur_vts;
}

static int os02k10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct os02k10 *os02k10 = container_of(ctrl->handler,
					       struct os02k10, ctrl_handler);
	struct i2c_client *client = os02k10->client;
	u32 again = 0, dgain = 0;
	s64 max;
	int ret = 0;
	u32 val = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = os02k10->cur_mode->height + ctrl->val - 8;
		__v4l2_ctrl_modify_range(os02k10->exposure,
					 os02k10->exposure->minimum, max,
					 os02k10->exposure->step,
					 os02k10->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "set exposure value 0x%x\n", ctrl->val);
		if (os02k10->cur_mode->hdr_mode == NO_HDR) {
			/* 4 least significant bits of expsoure are fractional part */
			ret = os02k10_write_reg(os02k10->client,
						OS02K10_AEC_LONG_EXP_REG_H,
						OS02K10_REG_VALUE_16BIT,
						ctrl->val);
			dev_dbg(&client->dev, "set exposure 0x%x\n", ctrl->val);
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (ctrl->val > 992) {
			dgain = ctrl->val * 1024 / 992;
			again = 992;
		} else {
			dgain = 1024;
			again = ctrl->val;
		}
		dev_dbg(&client->dev, "gain %d, ag 0x%x, dg 0x%x\n",
			ctrl->val, again, dgain);
		ret = os02k10_write_reg(os02k10->client,
					OS02K10_AEC_LONG_REL_GAIN_REG_H,
					OS02K10_REG_VALUE_16BIT,
					(again << 2) & 0xff0);
		ret |= os02k10_write_reg(os02k10->client,
					 OS02K10_AEC_LONG_DIG_GAIN_REG_H,
					 OS02K10_REG_VALUE_24BIT,
					 (dgain << 6) & 0xfffc0);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set blank value 0x%x\n", ctrl->val);
		ret = os02k10_write_reg(os02k10->client,
					OS02K10_AEC_TIMING_VTS_REG_H,
					OS02K10_REG_VALUE_16BIT,
					ctrl->val + os02k10->cur_mode->height);
		os02k10->cur_vts = ctrl->val + os02k10->cur_mode->height;
		if (os02k10->cur_vts != os02k10->cur_mode->vts_def)
			os02k10_modify_fps_info(os02k10);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = os02k10_enable_test_pattern(os02k10, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = os02k10_read_reg(os02k10->client, OS02K10_FLIPMIRROR_REG,
				       OS02K10_REG_VALUE_08BIT, &val);
		ret |= os02k10_write_reg(os02k10->client, OS02K10_FLIPMIRROR_REG,
					 OS02K10_REG_VALUE_08BIT,
					 OS02K10_FETCH_MIRROR(val, ctrl->val));
		break;
	case V4L2_CID_VFLIP:
		ret = os02k10_read_reg(os02k10->client, OS02K10_FLIPMIRROR_REG,
				       OS02K10_REG_VALUE_08BIT, &val);
		ret |= os02k10_write_reg(os02k10->client, OS02K10_FLIPMIRROR_REG,
					 OS02K10_REG_VALUE_08BIT,
					 OS02K10_FETCH_FLIP(val, ctrl->val));
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops os02k10_ctrl_ops = {
	.s_ctrl = os02k10_set_ctrl,
};

static int os02k10_initialize_controls(struct os02k10 *os02k10)
{
	const struct os02k10_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u64 dst_pixel_rate = 0;
	u32 h_blank;
	int ret;

	handler = &os02k10->ctrl_handler;
	mode = os02k10->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &os02k10->mutex;

	os02k10->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_menu_items) - 1, 0,
				link_freq_menu_items);
	__v4l2_ctrl_s_ctrl(os02k10->link_freq, mode->mipi_freq_idx);

	if (mode->mipi_freq_idx == 0)
		dst_pixel_rate = OS02K10_LINK_FREQ_360M;
	else if (mode->mipi_freq_idx == 1)
		dst_pixel_rate = OS02K10_LINK_FREQ_480M;

	os02k10->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
						V4L2_CID_PIXEL_RATE, 0,
						OS02K10_LINK_FREQ_480M,
						1, dst_pixel_rate);

	h_blank = mode->hts_def - mode->width;
	os02k10->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (os02k10->hblank)
		os02k10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	vblank_def = mode->vts_def - mode->height;
	os02k10->vblank = v4l2_ctrl_new_std(handler, &os02k10_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    OS02K10_VTS_MAX - mode->height,
					    1, vblank_def);
	exposure_max = mode->vts_def - 8;
	os02k10->exposure = v4l2_ctrl_new_std(handler, &os02k10_ctrl_ops,
					      V4L2_CID_EXPOSURE, OS02K10_EXPOSURE_MIN,
					      exposure_max, OS02K10_EXPOSURE_STEP,
					      mode->exp_def);
	os02k10->anal_gain = v4l2_ctrl_new_std(handler, &os02k10_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, OS02K10_GAIN_MIN,
					       OS02K10_GAIN_MAX, OS02K10_GAIN_STEP,
					       OS02K10_GAIN_DEFAULT);
	os02k10->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &os02k10_ctrl_ops,
					V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(os02k10_test_pattern_menu) - 1,
					0, 0, os02k10_test_pattern_menu);
	v4l2_ctrl_new_std(handler, &os02k10_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &os02k10_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&os02k10->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	os02k10->subdev.ctrl_handler = handler;
	os02k10->has_init_exp = false;
	os02k10->cur_fps = mode->max_fps;
	os02k10->long_hcg = false;
	os02k10->middle_hcg = false;
	os02k10->short_hcg = false;
	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int os02k10_check_sensor_id(struct os02k10 *os02k10,
				   struct i2c_client *client)
{
	struct device *dev = &os02k10->client->dev;
	u32 id = 0;
	int ret;

	if (os02k10->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	ret = os02k10_read_reg(client, OS02K10_REG_CHIP_ID,
			       OS02K10_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OS%06x sensor\n", CHIP_ID);

	return 0;
}

static int os02k10_configure_regulators(struct os02k10 *os02k10)
{
	unsigned int i;

	for (i = 0; i < OS02K10_NUM_SUPPLIES; i++)
		os02k10->supplies[i].supply = os02k10_supply_names[i];

	return devm_regulator_bulk_get(&os02k10->client->dev,
				       OS02K10_NUM_SUPPLIES,
				       os02k10->supplies);
}

static int os02k10_get_dcg_ratio(struct os02k10 *os02k10)
{
	struct device *dev = &os02k10->client->dev;
	u32 val = 0;
	int ret = 0;

	if (os02k10->is_thunderboot) {
		ret = os02k10_read_reg(os02k10->client, OS02K10_REG_DCG_RATIO,
					OS02K10_REG_VALUE_16BIT, &val);
	} else {
		ret = os02k10_write_reg(os02k10->client, OS02K10_REG_CTRL_MODE,
					OS02K10_REG_VALUE_08BIT, OS02K10_MODE_STREAMING);
		usleep_range(5000, 6000);
		ret |= os02k10_read_reg(os02k10->client, OS02K10_REG_DCG_RATIO,
					OS02K10_REG_VALUE_16BIT, &val);
		ret |= os02k10_write_reg(os02k10->client, OS02K10_REG_CTRL_MODE,
					OS02K10_REG_VALUE_08BIT, OS02K10_MODE_SW_STANDBY);
	}

	if (ret != 0 || val == 0) {
		os02k10->dcg_ratio = 0;
		dev_err(dev, "get dcg ratio fail, ret %d, dcg ratio %d\n", ret, val);
	} else {
		os02k10->dcg_ratio = val;
		dev_info(dev, "get dcg ratio reg val 0x%04x\n", val);
	}

	return ret;
}

static int os02k10_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct os02k10 *os02k10;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	os02k10 = devm_kzalloc(dev, sizeof(*os02k10), GFP_KERNEL);
	if (!os02k10)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &os02k10->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &os02k10->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &os02k10->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &os02k10->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	os02k10->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);
	dev_info(dev, "is_thunderboot: %d\n", os02k10->is_thunderboot);
	os02k10->client = client;
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			os02k10->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(supported_modes))
		os02k10->cur_mode = &supported_modes[0];

	os02k10->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(os02k10->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	os02k10->reset_gpio = devm_gpiod_get(dev, "reset", os02k10->is_thunderboot ? GPIOD_ASIS : GPIOD_OUT_LOW);
	if (IS_ERR(os02k10->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	os02k10->pwdn_gpio = devm_gpiod_get(dev, "pwdn", os02k10->is_thunderboot ? GPIOD_ASIS : GPIOD_OUT_LOW);
	if (IS_ERR(os02k10->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	os02k10->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(os02k10->pinctrl)) {
		os02k10->pins_default =
			pinctrl_lookup_state(os02k10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(os02k10->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		os02k10->pins_sleep =
			pinctrl_lookup_state(os02k10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(os02k10->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = os02k10_configure_regulators(os02k10);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&os02k10->mutex);

	sd = &os02k10->subdev;
	v4l2_i2c_subdev_init(sd, client, &os02k10_subdev_ops);
	ret = os02k10_initialize_controls(os02k10);
	if (ret)
		goto err_destroy_mutex;

	ret = __os02k10_power_on(os02k10);
	if (ret)
		goto err_free_handler;

	ret = os02k10_check_sensor_id(os02k10, client);
	if (ret)
		goto err_power_off;

	ret = os02k10_get_dcg_ratio(os02k10);
	if (ret)
		dev_warn(dev, "get dcg ratio failed\n");

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &os02k10_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	os02k10->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &os02k10->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(os02k10->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 os02k10->module_index, facing,
		 OS02K10_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	if (os02k10->is_thunderboot)
		pm_runtime_get_sync(dev);
	else
		pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__os02k10_power_off(os02k10);
err_free_handler:
	v4l2_ctrl_handler_free(&os02k10->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&os02k10->mutex);

	return ret;
}

static int os02k10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os02k10 *os02k10 = to_os02k10(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&os02k10->ctrl_handler);
	mutex_destroy(&os02k10->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__os02k10_power_off(os02k10);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id os02k10_of_match[] = {
	{ .compatible = "ovti,os02k10" },
	{},
};
MODULE_DEVICE_TABLE(of, os02k10_of_match);
#endif

static const struct i2c_device_id os02k10_match_id[] = {
	{ "ovti,os02k10", 0 },
	{ },
};

static struct i2c_driver os02k10_i2c_driver = {
	.driver = {
		.name = OS02K10_NAME,
		.pm = &os02k10_pm_ops,
		.of_match_table = of_match_ptr(os02k10_of_match),
	},
	.probe		= &os02k10_probe,
	.remove		= &os02k10_remove,
	.id_table	= os02k10_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&os02k10_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&os02k10_i2c_driver);
}

#if defined(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP) && !defined(CONFIG_INITCALL_ASYNC)
subsys_initcall(sensor_mod_init);
#else
device_initcall_sync(sensor_mod_init);
#endif
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("ovti os02k10 sensor driver");
MODULE_LICENSE("GPL");
