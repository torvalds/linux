// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include "stfcamss.h"

#define OV13850_XCLK_MIN  6000000
#define OV13850_XCLK_MAX 54000000

#define OV13850_LINK_FREQ_500MHZ         500000000LL

/**
 *OV13850 PLL
 *
 *PLL1:
 *
 *REF_CLK -> /PREDIVP[0] -> /PREDIV[2:0] -> *DIVP[9:8,7:0] -> /DIVM[3:0] -> /DIV_MIPI[1:0]  -> PCLK
 *(6-64M)     0x030A         0x0300          0x0301,0x302      0x0303        0x0304
 *                                                                      `-> MIPI_PHY_CLK
 *
 *
 *PLL2:
 *                           000: 1
 *                           001: 1.5
 *                           010: 2
 *                           011: 2.5
 *                           100: 3
 *                           101: 4
 *            0: /1          110: 6
 *            1: /2          111: 8
 *REF_CLK -> /PREDIVP[3] -> /PREDIV[2:0] -> /DIVP[9:0] -> /DIVDAC[3:0] -> DAC_CLK =
 *(6~64M)    0x3611
 *                                                     -> /DIVSP[3:0] -> /DIVS[2:0] -> SCLK
 *
 *                                                     -> /(1+DIVSRAM[3:0]) -> SRAM_CLK
 */

// PREDIVP
#define OV13850_REG_PLL1_PREDIVP        0x030a
#define OV13850_PREDIVP_1   0
#define OV13850_PREDIVP_2   1

// PREDIV
#define OV13850_REG_PLL1_PREDIV         0x0300
#define OV13850_PREDIV_1    0
#define OV13850_PREDIV_1_5  1
#define OV13850_PREDIV_2    2
#define OV13850_PREDIV_2_5  3
#define OV13850_PREDIV_3    4
#define OV13850_PREDIV_4    5
#define OV13850_PREDIV_6    6
#define OV13850_PREDIV_8    7

// DIVP
#define OV13850_REG_PLL1_DIVP_H         0x0301
#define OV13850_REG_PLL1_DIVP_L         0x0302
#define OV13850_REG_PLL1_DIVP           OV13850_REG_PLL1_DIVP_H

// DIVM
#define OV13850_REG_PLL1_DIVM           0x0303
#define OV13850_DIVM(n)     ((n)-1) // n=1~16

// DIV_MIPI
#define OV13850_REG_PLL1_DIV_MIPI       0x0304
#define OV13850_DIV_MIPI_4  0
#define OV13850_DIV_MIPI_5  1
#define OV13850_DIV_MIPI_6  2
#define OV13850_DIV_MIPI_8  3

// system control
#define OV13850_STREAM_CTRL             0x0100
#define OV13850_REG_MIPI_SC             0x300f
#define OV13850_MIPI_SC_8_BIT           0x0
#define OV13850_MIPI_SC_10_BIT          0x1
#define OV13850_MIPI_SC_12_BIT          0x2
#define OV13850_GET_MIPI_SC_MIPI_BIT(v)         ((v) & 0x3)
#define OV13850_REG_MIPI_SC_CTRL0       0x3012
#define OV13850_GET_MIPI_SC_CTRL0_LANE_NUM(v)   ((v)>>4 & 0xf)

// timing
#define OV13850_REG_H_CROP_START_H      0x3800
#define OV13850_REG_H_CROP_START_L      0x3801
#define OV13850_REG_H_CROP_START        OV13850_REG_H_CROP_START_H
#define OV13850_REG_V_CROP_START_H      0x3802
#define OV13850_REG_V_CROP_START_L      0x3803
#define OV13850_REG_V_CROP_START        OV13850_REG_V_CROP_START_H

#define OV13850_REG_H_CROP_END_H        0x3804
#define OV13850_REG_H_CROP_END_L        0x3805
#define OV13850_REG_H_CROP_END          OV13850_REG_H_CROP_END_H
#define OV13850_REG_V_CROP_END_H        0x3806
#define OV13850_REG_V_CROP_END_L        0x3807
#define OV13850_REG_V_CROP_END          OV13850_REG_V_CROP_END_H

#define OV13850_REG_H_OUTPUT_SIZE_H     0x3808
#define OV13850_REG_H_OUTPUT_SIZE_L     0x3809
#define OV13850_REG_H_OUTPUT_SIZE       OV13850_REG_H_OUTPUT_SIZE_H
#define OV13850_REG_V_OUTPUT_SIZE_H     0x380a
#define OV13850_REG_V_OUTPUT_SIZE_L     0x380b
#define OV13850_REG_V_OUTPUT_SIZE       OV13850_REG_V_OUTPUT_SIZE_H

#define OV13850_REG_TIMING_HTS_H        0x380c
#define OV13850_REG_TIMING_HTS_L        0x380d
#define OV13850_REG_TIMING_HTS          OV13850_REG_TIMING_HTS_H
#define OV13850_REG_TIMING_VTS_H        0x380e
#define OV13850_REG_TIMING_VTS_L        0x380f
#define OV13850_REG_TIMING_VTS          OV13850_REG_TIMING_VTS_H


#define OV13850_REG_H_WIN_OFF_H         0x3810
#define OV13850_REG_H_WIN_OFF_L         0x3811
#define OV13850_REG_V_WIN_OFF_H         0x3812
#define OV13850_REG_V_WIN_OFF_L         0x3813

#define OV13850_REG_H_INC               0x3814
#define OV13850_REG_V_INC               0x3815

enum ov13850_mode_id {
	OV13850_MODE_1080P_1920_1080 = 0,
	OV13850_NUM_MODES,
};

enum ov13850_frame_rate {
	OV13850_15_FPS = 0,
	OV13850_30_FPS,
	OV13850_60_FPS,
	OV13850_NUM_FRAMERATES,
};

static const int ov13850_framerates[] = {
	[OV13850_15_FPS] = 15,
	[OV13850_30_FPS] = 30,
	[OV13850_60_FPS] = 60,
};

struct ov13850_pixfmt {
	u32 code;
	u32 colorspace;
};

static const struct ov13850_pixfmt ov13850_formats[] = {
	{ MEDIA_BUS_FMT_SBGGR10_1X10, V4L2_COLORSPACE_SRGB, },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, V4L2_COLORSPACE_SRGB, },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, V4L2_COLORSPACE_SRGB, },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, V4L2_COLORSPACE_SRGB, },
};

/* regulator supplies */
static const char * const ov13850_supply_name[] = {
	"DOVDD", /* Digital I/O (1.8V) supply */
	"AVDD",  /* Analog (2.8V) supply */
	"DVDD",  /* Digital Core (1.5V) supply */
};

#define OV13850_NUM_SUPPLIES ARRAY_SIZE(ov13850_supply_name)

/*
 * Image size under 1280 * 960 are SUBSAMPLING
 * Image size upper 1280 * 960 are SCALING
 */
enum ov13850_downsize_mode {
	SUBSAMPLING,
	SCALING,
};

struct reg_value {
	u16 reg_addr;
	u8 val;
	u8 mask;
	u32 delay_ms;
};

struct ov13850_mode_info {
	enum ov13850_mode_id id;
	enum ov13850_downsize_mode dn_mode;
	u32 hact;
	u32 htot;
	u32 vact;
	u32 vtot;
	const struct reg_value *reg_data;
	u32 reg_data_size;
	u32 max_fps;
};

struct ov13850_ctrls {
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl *pixel_rate;
	struct {
		struct v4l2_ctrl *exposure;
	};
	struct {
		struct v4l2_ctrl *auto_wb;
		struct v4l2_ctrl *blue_balance;
		struct v4l2_ctrl *red_balance;
	};
	struct {
		struct v4l2_ctrl *anal_gain;
	};
	struct v4l2_ctrl *brightness;
	struct v4l2_ctrl *light_freq;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *saturation;
	struct v4l2_ctrl *contrast;
	struct v4l2_ctrl *hue;
	struct v4l2_ctrl *test_pattern;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
};

struct ov13850_dev {
	struct i2c_client *i2c_client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_fwnode_endpoint ep; /* the parsed DT endpoint info */
	struct clk *xclk; /* system clock to OV13850 */
	u32 xclk_freq;

	struct regulator_bulk_data supplies[OV13850_NUM_SUPPLIES];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	bool   upside_down;

	/* lock to protect all members below */
	struct mutex lock;

	int power_count;

	struct v4l2_mbus_framefmt fmt;
	bool pending_fmt_change;

	const struct ov13850_mode_info *current_mode;
	const struct ov13850_mode_info *last_mode;
	enum ov13850_frame_rate current_fr;
	struct v4l2_fract frame_interval;

	struct ov13850_ctrls ctrls;

	u32 prev_sysclk, prev_hts;
	u32 ae_low, ae_high, ae_target;

	bool pending_mode_change;
	bool streaming;
};

static inline struct ov13850_dev *to_ov13850_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov13850_dev, sd);
}

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov13850_dev,
			ctrls.handler)->sd;
}

/* ov13850 initial register */
static const struct reg_value ov13850_init_setting_30fps_1080P[] = {

};

static const struct reg_value ov13850_setting_1080P_1920_1080[] = {
//;XVCLK=24Mhz, SCLK=4x120Mhz, MIPI 640Mbps, DACCLK=240Mhz
/*
 * using quarter size to scale down
 */
	{0x0103, 0x01, 0, 0}, // ; software reset

	{0x0300, 0x01, 0, 0}, //; PLL
	{0x0301, 0x00, 0, 0}, //; PLL1_DIVP_hi
	{0x0302, 0x28, 0, 0}, //; PLL1_DIVP_lo
	{0x0303, 0x00, 0, 0}, // ; PLL
	{0x030a, 0x00, 0, 0}, // ; PLL
	//{0xffff, 20, 0, 0},
	{0x300f, 0x11, 0, 0}, // SFC modified, MIPI_SRC, [1:0] 00-8bit, 01-10bit, 10-12bit
	{0x3010, 0x01, 0, 0}, // ; MIPI PHY
	{0x3011, 0x76, 0, 0}, // ; MIPI PHY
	{0x3012, 0x41, 0, 0}, // ; MIPI 4 lane
	{0x3013, 0x12, 0, 0}, // ; MIPI control
	{0x3014, 0x11, 0, 0}, // ; MIPI control
	{0x301f, 0x03, 0, 0}, //
	{0x3106, 0x00, 0, 0}, //
	{0x3210, 0x47, 0, 0}, //
	{0x3500, 0x00, 0, 0}, // ; exposure HH
	{0x3501, 0x67, 0, 0}, // ; exposure H
	{0x3502, 0x80, 0, 0}, // ; exposure L
	{0x3506, 0x00, 0, 0}, // ; short exposure HH
	{0x3507, 0x02, 0, 0}, // ; short exposure H
	{0x3508, 0x00, 0, 0}, // ; shour exposure L
	{0x3509, 0x10, 0, 0},//00},//8},
	{0x350a, 0x00, 0, 0}, // ; gain H
	{0x350b, 0x10, 0, 0}, // ; gain L
	{0x350e, 0x00, 0, 0}, // ; short gain H
	{0x350f, 0x10, 0, 0}, // ; short gain L
	{0x3600, 0x40, 0, 0}, // ; analog control
	{0x3601, 0xfc, 0, 0}, // ; analog control
	{0x3602, 0x02, 0, 0}, // ; analog control
	{0x3603, 0x48, 0, 0}, // ; analog control
	{0x3604, 0xa5, 0, 0}, // ; analog control
	{0x3605, 0x9f, 0, 0}, // ; analog control
	{0x3607, 0x00, 0, 0}, // ; analog control
	{0x360a, 0x40, 0, 0}, // ; analog control
	{0x360b, 0x91, 0, 0}, // ; analog control
	{0x360c, 0x49, 0, 0}, // ; analog control
	{0x360f, 0x8a, 0, 0}, //
	{0x3611, 0x10, 0, 0}, // ; PLL2
	//{0x3612, 0x23, 0, 0}, // ; PLL2
	{0x3612, 0x13, 0, 0}, // ; PLL2
	//{0x3613, 0x33, 0, 0}, // ; PLL2
	{0x3613, 0x22, 0, 0}, // ; PLL2
	//{0xffff, 50, 0, 0},
	{0x3614, 0x28, 0, 0}, //[7:0] PLL2_DIVP lo
	{0x3615, 0x08, 0, 0}, //[7:6] Debug mode, [5:4] N_pump clock div, [3:2] P_pump clock div, [1:0] PLL2_DIVP hi
	{0x3641, 0x02, 0, 0},
	{0x3660, 0x82, 0, 0},
	{0x3668, 0x54, 0, 0},
	{0x3669, 0x40, 0, 0},
	{0x3667, 0xa0, 0, 0},
	{0x3702, 0x40, 0, 0},
	{0x3703, 0x44, 0, 0},
	{0x3704, 0x2c, 0, 0},
	{0x3705, 0x24, 0, 0},
	{0x3706, 0x50, 0, 0},
	{0x3707, 0x44, 0, 0},
	{0x3708, 0x3c, 0, 0},
	{0x3709, 0x1f, 0, 0},
	{0x370a, 0x26, 0, 0},
	{0x370b, 0x3c, 0, 0},
	{0x3720, 0x66, 0, 0},
	{0x3722, 0x84, 0, 0},
	{0x3728, 0x40, 0, 0},
	{0x372a, 0x00, 0, 0},
	{0x372f, 0x90, 0, 0},
	{0x3710, 0x28, 0, 0},
	{0x3716, 0x03, 0, 0},
	{0x3718, 0x10, 0, 0},
	{0x3719, 0x08, 0, 0},
	{0x371c, 0xfc, 0, 0},
	{0x3760, 0x13, 0, 0},
	{0x3761, 0x34, 0, 0},
	{0x3767, 0x24, 0, 0},
	{0x3768, 0x06, 0, 0},
	{0x3769, 0x45, 0, 0},
	{0x376c, 0x23, 0, 0},
	{0x3d84, 0x00, 0, 0}, // ; OTP program disable
	{0x3d85, 0x17, 0, 0}, // ; OTP power up load data enable, power load setting enable, software load setting
	{0x3d8c, 0x73, 0, 0}, // ; OTP start address H
	{0x3d8d, 0xbf, 0, 0}, // ; OTP start address L
	{0x3800, 0x00, 0, 0}, // ; H crop start H
	{0x3801, 0x08, 0, 0}, // ; H crop start L
	{0x3802, 0x00, 0, 0}, // ; V crop start H
	{0x3803, 0x04, 0, 0}, // ; V crop start L
	{0x3804, 0x10, 0, 0}, // ; H crop end H
	{0x3805, 0x97, 0, 0}, // ; H crop end L
	{0x3806, 0x0c, 0, 0}, // ; V crop end H
	{0x3807, 0x4b, 0, 0}, // ; V crop end L
	{0x3808, 0x08, 0, 0}, // ; H output size H
	{0x3809, 0x40, 0, 0}, // ; H output size L
	{0x380a, 0x06, 0, 0}, // ; V output size H
	{0x380b, 0x20, 0, 0}, // ; V output size L
	{0x380c, 0x25, 0, 0}, // ; HTS H
	{0x380d, 0x80, 0, 0}, // ; HTS L
	{0x380e, 0x06, 0, 0}, // ; VTS H
	{0x380f, 0x80, 0, 0}, // ; VTS L
	{0x3810, 0x00, 0, 0}, // ; H win off H
	{0x3811, 0x04, 0, 0}, // ; H win off L
	{0x3812, 0x00, 0, 0}, // ; V win off H
	{0x3813, 0x02, 0, 0}, // ; V win off L
	{0x3814, 0x31, 0, 0}, // ; H inc
	{0x3815, 0x31, 0, 0}, // ; V inc
	{0x3820, 0x02, 0, 0}, // ; V flip off, V bin on
	{0x3821, 0x05, 0, 0}, // ; H mirror on, H bin on
	{0x3834, 0x00, 0, 0}, //
	{0x3835, 0x1c, 0, 0}, // ; cut_en, vts_auto, blk_col_dis
	{0x3836, 0x08, 0, 0}, //
	{0x3837, 0x02, 0, 0}, //
	{0x4000, 0xf1, 0, 0},//c1}, // ; BLC offset trig en, format change trig en, gain trig en, exp trig en, median en
	{0x4001, 0x00, 0, 0}, // ; BLC
	{0x400b, 0x0c, 0, 0}, // ; BLC
	{0x4011, 0x00, 0, 0}, // ; BLC
	{0x401a, 0x00, 0, 0}, // ; BLC
	{0x401b, 0x00, 0, 0}, // ; BLC
	{0x401c, 0x00, 0, 0}, // ; BLC
	{0x401d, 0x00, 0, 0}, // ; BLC
	{0x4020, 0x00, 0, 0}, // ; BLC
	{0x4021, 0xe4, 0, 0}, // ; BLC
	{0x4022, 0x07, 0, 0}, // ; BLC
	{0x4023, 0x5f, 0, 0}, // ; BLC
	{0x4024, 0x08, 0, 0}, // ; BLC
	{0x4025, 0x44, 0, 0}, // ; BLC
	{0x4026, 0x08, 0, 0}, // ; BLC
	{0x4027, 0x47, 0, 0}, // ; BLC
	{0x4028, 0x00, 0, 0}, // ; BLC
	{0x4029, 0x02, 0, 0}, // ; BLC
	{0x402a, 0x04, 0, 0}, // ; BLC
	{0x402b, 0x08, 0, 0}, // ; BLC
	{0x402c, 0x02, 0, 0}, // ; BLC
	{0x402d, 0x02, 0, 0}, // ; BLC
	{0x402e, 0x0c, 0, 0}, // ; BLC
	{0x402f, 0x08, 0, 0}, // ; BLC
	{0x403d, 0x2c, 0, 0}, //
	{0x403f, 0x7f, 0, 0}, //
	{0x4500, 0x82, 0, 0}, // ; BLC
	{0x4501, 0x38, 0, 0}, // ; BLC
	{0x4601, 0x04, 0, 0}, //
	{0x4602, 0x22, 0, 0}, //
	{0x4603, 0x01, 0, 0}, //; VFIFO
	{0x4837, 0x19, 0, 0}, //; MIPI global timing
	{0x4d00, 0x04, 0, 0}, // ; temperature monitor
	{0x4d01, 0x42, 0, 0}, //  ; temperature monitor
	{0x4d02, 0xd1, 0, 0}, //  ; temperature monitor
	{0x4d03, 0x90, 0, 0}, //  ; temperature monitor
	{0x4d04, 0x66, 0, 0}, //  ; temperature monitor
	{0x4d05, 0x65, 0, 0}, // ; temperature monitor
	{0x5000, 0x0e, 0, 0}, // ; windowing enable, BPC on, WPC on, Lenc on
	{0x5001, 0x03, 0, 0}, // ; BLC enable, MWB on
	{0x5002, 0x07, 0, 0}, //
	{0x5013, 0x40, 0, 0},
	{0x501c, 0x00, 0, 0},
	{0x501d, 0x10, 0, 0},
	//{0x5057, 0x56, 0, 0},//add
	{0x5056, 0x08, 0, 0},
	{0x5058, 0x08, 0, 0},
	{0x505a, 0x08, 0, 0},
	{0x5242, 0x00, 0, 0},
	{0x5243, 0xb8, 0, 0},
	{0x5244, 0x00, 0, 0},
	{0x5245, 0xf9, 0, 0},
	{0x5246, 0x00, 0, 0},
	{0x5247, 0xf6, 0, 0},
	{0x5248, 0x00, 0, 0},
	{0x5249, 0xa6, 0, 0},
	{0x5300, 0xfc, 0, 0},
	{0x5301, 0xdf, 0, 0},
	{0x5302, 0x3f, 0, 0},
	{0x5303, 0x08, 0, 0},
	{0x5304, 0x0c, 0, 0},
	{0x5305, 0x10, 0, 0},
	{0x5306, 0x20, 0, 0},
	{0x5307, 0x40, 0, 0},
	{0x5308, 0x08, 0, 0},
	{0x5309, 0x08, 0, 0},
	{0x530a, 0x02, 0, 0},
	{0x530b, 0x01, 0, 0},
	{0x530c, 0x01, 0, 0},
	{0x530d, 0x0c, 0, 0},
	{0x530e, 0x02, 0, 0},
	{0x530f, 0x01, 0, 0},
	{0x5310, 0x01, 0, 0},
	{0x5400, 0x00, 0, 0},
	{0x5401, 0x61, 0, 0},
	{0x5402, 0x00, 0, 0},
	{0x5403, 0x00, 0, 0},
	{0x5404, 0x00, 0, 0},
	{0x5405, 0x40, 0, 0},
	{0x540c, 0x05, 0, 0},
	{0x5b00, 0x00, 0, 0},
	{0x5b01, 0x00, 0, 0},
	{0x5b02, 0x01, 0, 0},
	{0x5b03, 0xff, 0, 0},
	{0x5b04, 0x02, 0, 0},
	{0x5b05, 0x6c, 0, 0},
	{0x5b09, 0x02, 0, 0}, //
	//{0x5e00, 0x00, 0, 0}, // ; test pattern disable
	//{0x5e00, 0x80, 0, 0}, // ; test pattern enable
	{0x5e10, 0x1c, 0, 0}, // ; ISP test disable

	//{0x0300, 0x01, 0, 0},// ; PLL
	//{0x0302, 0x28, 0, 0},// ; PLL
	//{0xffff,  50, 0, 0},
	{0x3501, 0x67, 0, 0},// ; Exposure H
	{0x370a, 0x26, 0, 0},//
	{0x372a, 0x00, 0, 0},
	{0x372f, 0x90, 0, 0},
	{0x3801, 0x08, 0, 0}, //; H crop start L
	{0x3803, 0x04, 0, 0}, //; V crop start L
	{0x3805, 0x97, 0, 0}, //; H crop end L
	{0x3807, 0x4b, 0, 0}, //; V crop end L
	{0x3808, 0x08, 0, 0}, //; H output size H
	{0x3809, 0x40, 0, 0}, //; H output size L
	{0x380a, 0x06, 0, 0}, //; V output size H
	{0x380b, 0x20, 0, 0}, //; V output size L
	{0x380c, 0x25, 0, 0}, //; HTS H
	{0x380d, 0x80, 0, 0}, //; HTS L
	{0x380e, 0x0a, 0, 0},//6}, //; VTS H
	{0x380f, 0x80, 0, 0}, //; VTS L
	{0x3813, 0x02, 0, 0}, //; V win off
	{0x3814, 0x31, 0, 0}, //; H inc
	{0x3815, 0x31, 0, 0}, //; V inc
	{0x3820, 0x02, 0, 0}, //; V flip off, V bin on
	{0x3821, 0x05, 0, 0}, //; H mirror on, H bin on
	{0x3836, 0x08, 0, 0}, //
	{0x3837, 0x02, 0, 0}, //
	{0x4020, 0x00, 0, 0}, //
	{0x4021, 0xe4, 0, 0}, //
	{0x4022, 0x07, 0, 0}, //
	{0x4023, 0x5f, 0, 0}, //
	{0x4024, 0x08, 0, 0}, //
	{0x4025, 0x44, 0, 0}, //
	{0x4026, 0x08, 0, 0}, //
	{0x4027, 0x47, 0, 0}, //
	{0x4603, 0x01, 0, 0}, //; VFIFO
	{0x4837, 0x19, 0, 0}, //; MIPI global timing
	{0x4802, 0x42, 0, 0},  //default 0x00
	{0x481a, 0x00, 0, 0},
	{0x481b, 0x1c, 0, 0},   //default 0x3c  prepare
	{0x4826, 0x12, 0, 0},   //default 0x32  trail
	{0x5401, 0x61, 0, 0}, //
	{0x5405, 0x40, 0, 0}, //

	//{0xffff, 200, 0, 0},
	//{0xffff, 200, 0, 0},
	//{0xffff, 200, 0, 0},

	//{0x0100, 0x01, 0, 0}, //; wake up, streaming
};

/* power-on sensor init reg table */
static const struct ov13850_mode_info ov13850_mode_init_data = {
	OV13850_MODE_1080P_1920_1080, SCALING,
	1920, 0x6e0, 1080, 0x470,
	ov13850_init_setting_30fps_1080P,
	ARRAY_SIZE(ov13850_init_setting_30fps_1080P),
	OV13850_30_FPS,
};

static const struct ov13850_mode_info
ov13850_mode_data[OV13850_NUM_MODES] = {
	{OV13850_MODE_1080P_1920_1080, SCALING,
	1920, 0x6e0, 1080, 0x470,
	ov13850_setting_1080P_1920_1080,
	ARRAY_SIZE(ov13850_setting_1080P_1920_1080),
	OV13850_30_FPS},
};

static int ov13850_write_reg(struct ov13850_dev *sensor, u16 reg, u8 val)
{
	struct i2c_client *client = sensor->i2c_client;
	struct i2c_msg msg;
	u8 buf[3];
	int ret;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	buf[2] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "%s: error: reg=%x, val=%x\n",
			__func__, reg, val);
		return ret;
	}

	return 0;
}

static int ov13850_read_reg(struct ov13850_dev *sensor, u16 reg, u8 *val)
{
	struct i2c_client *client = sensor->i2c_client;
	struct i2c_msg msg[2];
	u8 buf[2];
	int ret;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: error: reg=%x\n",
			__func__, reg);
		return ret;
	}

	*val = buf[0];
	return 0;
}

static int ov13850_read_reg16(struct ov13850_dev *sensor, u16 reg, u16 *val)
{
	u8 hi, lo;
	int ret;

	ret = ov13850_read_reg(sensor, reg, &hi);
	if (ret)
		return ret;
	ret = ov13850_read_reg(sensor, reg + 1, &lo);
	if (ret)
		return ret;

	*val = ((u16)hi << 8) | (u16)lo;
	return 0;
}

static int ov13850_write_reg16(struct ov13850_dev *sensor, u16 reg, u16 val)
{
	int ret;

	ret = ov13850_write_reg(sensor, reg, val >> 8);
	if (ret)
		return ret;

	return ov13850_write_reg(sensor, reg + 1, val & 0xff);
}

static int ov13850_mod_reg(struct ov13850_dev *sensor, u16 reg,
			u8 mask, u8 val)
{
	u8 readval;
	int ret;

	ret = ov13850_read_reg(sensor, reg, &readval);
	if (ret)
		return ret;

	readval &= ~mask;
	val &= mask;
	val |= readval;

	return ov13850_write_reg(sensor, reg, val);
}

static int ov13850_set_timings(struct ov13850_dev *sensor,
			const struct ov13850_mode_info *mode)
{
	int ret;

	ret = ov13850_write_reg16(sensor, OV13850_REG_H_OUTPUT_SIZE, mode->hact);
	if (ret < 0)
		return ret;

	ret = ov13850_write_reg16(sensor, OV13850_REG_V_OUTPUT_SIZE, mode->vact);
	if (ret < 0)
		return ret;

	return 0;
}

static int ov13850_load_regs(struct ov13850_dev *sensor,
			const struct ov13850_mode_info *mode)
{
	const struct reg_value *regs = mode->reg_data;
	unsigned int i;
	u32 delay_ms;
	u16 reg_addr;
	u8 mask, val;
	int ret = 0;

	st_info(ST_SENSOR, "%s, mode = 0x%x\n", __func__, mode->id);
	for (i = 0; i < mode->reg_data_size; ++i, ++regs) {
		delay_ms = regs->delay_ms;
		reg_addr = regs->reg_addr;
		val = regs->val;
		mask = regs->mask;

		if (mask)
			ret = ov13850_mod_reg(sensor, reg_addr, mask, val);
		else
			ret = ov13850_write_reg(sensor, reg_addr, val);
		if (ret)
			break;

		if (delay_ms)
			usleep_range(1000 * delay_ms, 1000 * delay_ms + 100);
	}

	return ov13850_set_timings(sensor, mode);
}



static int ov13850_get_gain(struct ov13850_dev *sensor)
{
	u32 gain = 0;
	return gain;
}

static int ov13850_set_gain(struct ov13850_dev *sensor, int gain)
{
	return 0;
}

static int ov13850_set_stream_mipi(struct ov13850_dev *sensor, bool on)
{
	return 0;
}

static int ov13850_get_sysclk(struct ov13850_dev *sensor)
{
	return 0;
}

static int ov13850_set_night_mode(struct ov13850_dev *sensor)
{
	return 0;
}

static int ov13850_get_hts(struct ov13850_dev *sensor)
{
	/* read HTS from register settings */
	u16 hts;
	int ret;

	ret = ov13850_read_reg16(sensor, OV13850_REG_TIMING_HTS, &hts);
	if (ret)
		return ret;
	return hts;
}

static int ov13850_set_hts(struct ov13850_dev *sensor, int hts)
{
	return ov13850_write_reg16(sensor, OV13850_REG_TIMING_HTS, hts);
}


static int ov13850_get_vts(struct ov13850_dev *sensor)
{
	u16 vts;
	int ret;

	ret = ov13850_read_reg16(sensor, OV13850_REG_TIMING_VTS, &vts);
	if (ret)
		return ret;
	return vts;
}

static int ov13850_set_vts(struct ov13850_dev *sensor, int vts)
{
	return ov13850_write_reg16(sensor, OV13850_REG_TIMING_VTS, vts);
}

static int ov13850_get_light_freq(struct ov13850_dev *sensor)
{
	return 0;
}

static int ov13850_set_bandingfilter(struct ov13850_dev *sensor)
{
	return 0;
}

static int ov13850_set_ae_target(struct ov13850_dev *sensor, int target)
{
	return 0;
}

static int ov13850_get_binning(struct ov13850_dev *sensor)
{
	return 0;
}

static int ov13850_set_binning(struct ov13850_dev *sensor, bool enable)
{
	return 0;
}

static const struct ov13850_mode_info *
ov13850_find_mode(struct ov13850_dev *sensor, enum ov13850_frame_rate fr,
		int width, int height, bool nearest)
{
	const struct ov13850_mode_info *mode;

	mode = v4l2_find_nearest_size(ov13850_mode_data,
				ARRAY_SIZE(ov13850_mode_data),
				hact, vact,
				width, height);

	if (!mode ||
		(!nearest && (mode->hact != width || mode->vact != height)))
		return NULL;

	/* Check to see if the current mode exceeds the max frame rate */
	if (ov13850_framerates[fr] > ov13850_framerates[mode->max_fps])
		return NULL;

	return mode;
}

static u64 ov13850_calc_pixel_rate(struct ov13850_dev *sensor)
{
	u64 rate;

	rate = sensor->current_mode->vact * sensor->current_mode->hact;
	rate *= ov13850_framerates[sensor->current_fr];

	return rate;
}

/*
 * After trying the various combinations, reading various
 * documentations spread around the net, and from the various
 * feedback, the clock tree is probably as follows:
 *
 *   +--------------+
 *   |  Ext. Clock  |
 *   +-+------------+
 *     |  +----------+
 *     +->|   PLL1   | - reg 0x030a, bit0 for the pre-dividerp
 *        +-+--------+ - reg 0x0300, bits 0-2 for the pre-divider
 *        +-+--------+ - reg 0x0301~0x0302, for the multiplier
 *          |  +--------------+
 *          +->| MIPI Divider |  - reg 0x0303, bits 0-3 for the pre-divider
 *               | +---------> MIPI PHY CLK
 *               |    +-----+
 *               | +->| PLL1_DIV_MIPI | - reg 0x0304, bits 0-1 for the divider
 *                 |    +----------------> PCLK
 *               |    +-----+
 *
 *   +--------------+
 *   |  Ext. Clock  |
 *   +-+------------+
 *     |  +----------+
 *     +->|   PLL2  | - reg 0x0311, bit0 for the pre-dividerp
 *        +-+--------+ - reg 0x030b, bits 0-2 for the pre-divider
 *        +-+--------+ - reg 0x030c~0x030d, for the multiplier
 *          |  +--------------+
 *          +->| SCLK Divider |  - reg 0x030F, bits 0-3 for the pre-divider
 *               +-+--------+    - reg 0x030E, bits 0-2 for the divider
 *               |    +---------> SCLK
 *
 *          |       +-----+
 *          +->| DAC Divider | - reg 0x0312, bits 0-3 for the divider
 *                    |    +----------------> DACCLK
 **
 */

/*
 * ov13850_set_mipi_pclk() - Calculate the clock tree configuration values
 *			for the MIPI CSI-2 output.
 *
 * @rate: The requested bandwidth per lane in bytes per second.
 *	'Bandwidth Per Lane' is calculated as:
 *	bpl = HTOT * VTOT * FPS * bpp / num_lanes;
 *
 * This function use the requested bandwidth to calculate:
 *
 * - mipi_pclk   = bpl / 2; ( / 2 is for CSI-2 DDR)
 * - mipi_phy_clk   = mipi_pclk * PLL1_DIV_MIPI;
 *
 * with these fixed parameters:
 *	PLL1_PREDIVP    = 1;
 *	PLL1_PREDIV     = 1; (MIPI_BIT_MODE == 8 ? 2 : 2,5);
 *	PLL1_DIVM       = 1;
 *	PLL1_DIV_MIPI   = 4;
 *
 * FIXME: this have been tested with 10-bit raw and 2 lanes setup only.
 * MIPI_DIV is fixed to value 2, but it -might- be changed according to the
 * above formula for setups with 1 lane or image formats with different bpp.
 *
 * FIXME: this deviates from the sensor manual documentation which is quite
 * thin on the MIPI clock tree generation part.
 */



static int ov13850_set_mipi_pclk(struct ov13850_dev *sensor,
				unsigned long rate)
{

	return 0;
}

/*
 * if sensor changes inside scaling or subsampling
 * change mode directly
 */
static int ov13850_set_mode_direct(struct ov13850_dev *sensor,
				const struct ov13850_mode_info *mode)
{
	if (!mode->reg_data)
		return -EINVAL;

	/* Write capture setting */
	return ov13850_load_regs(sensor, mode);
}

static int ov13850_set_mode(struct ov13850_dev *sensor)
{
	const struct ov13850_mode_info *mode = sensor->current_mode;
	const struct ov13850_mode_info *orig_mode = sensor->last_mode;
	int ret = 0;

	ret = ov13850_set_mode_direct(sensor, mode);
	if (ret < 0)
		return ret;

	/*
	 * we support have 10 bits raw RGB(mipi)
	 */
	if (sensor->ep.bus_type == V4L2_MBUS_CSI2_DPHY)
		ret = ov13850_set_mipi_pclk(sensor, 0);

	if (ret < 0)
		return 0;

	sensor->pending_mode_change = false;
	sensor->last_mode = mode;
	return 0;
}

static int ov13850_set_framefmt(struct ov13850_dev *sensor,
			       struct v4l2_mbus_framefmt *format);

/* restore the last set video mode after chip power-on */
static int ov13850_restore_mode(struct ov13850_dev *sensor)
{
	int ret;

	/* first load the initial register values */
	ret = ov13850_load_regs(sensor, &ov13850_mode_init_data);
	if (ret < 0)
		return ret;
	sensor->last_mode = &ov13850_mode_init_data;

	/* now restore the last capture mode */
	ret = ov13850_set_mode(sensor);
	if (ret < 0)
		return ret;

	return ov13850_set_framefmt(sensor, &sensor->fmt);
}

static void ov13850_power(struct ov13850_dev *sensor, bool enable)
{
	if (!sensor->pwdn_gpio)
		return;
	if (enable) {
		gpiod_set_value_cansleep(sensor->pwdn_gpio, 0);
		gpiod_set_value_cansleep(sensor->pwdn_gpio, 1);
	} else {
		gpiod_set_value_cansleep(sensor->pwdn_gpio, 0);
	}

	mdelay(100);
}

static void ov13850_reset(struct ov13850_dev *sensor)
{
	if (!sensor->reset_gpio)
		return;

	gpiod_set_value_cansleep(sensor->reset_gpio, 0);
	gpiod_set_value_cansleep(sensor->reset_gpio, 1);
	mdelay(100);
}

static int ov13850_set_power_on(struct ov13850_dev *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	int ret;

	ret = clk_prepare_enable(sensor->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		return ret;
	}

	ret = regulator_bulk_enable(OV13850_NUM_SUPPLIES,
				sensor->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		goto xclk_off;
	}

	ov13850_reset(sensor);
	ov13850_power(sensor, true);

	return 0;

xclk_off:
	clk_disable_unprepare(sensor->xclk);
	return ret;
}

static void ov13850_set_power_off(struct ov13850_dev *sensor)
{
	ov13850_power(sensor, false);
	regulator_bulk_disable(OV13850_NUM_SUPPLIES, sensor->supplies);
	clk_disable_unprepare(sensor->xclk);
}

static int ov13850_set_power_mipi(struct ov13850_dev *sensor, bool on)
{
	return 0;
}

static int ov13850_set_power(struct ov13850_dev *sensor, bool on)
{
	int ret = 0;
	u16 chip_id;

	if (on) {
		ret = ov13850_set_power_on(sensor);
		if (ret)
			return ret;

#ifdef UNUSED_CODE
		ret = ov13850_read_reg16(sensor, OV13850_REG_CHIP_ID, &chip_id);
		if (ret) {
			dev_err(&sensor->i2c_client->dev, "%s: failed to read chip identifier\n",
				__func__);
			ret = -ENODEV;
			goto power_off;
		}

		if (chip_id != OV13850_CHIP_ID) {
			dev_err(&sensor->i2c_client->dev,
					"%s: wrong chip identifier, expected 0x%x, got 0x%x\n",
					__func__, OV13850_CHIP_ID, chip_id);
			ret = -ENXIO;
			goto power_off;
		}
		dev_err(&sensor->i2c_client->dev, "%s: chip identifier, got 0x%x\n",
			__func__, chip_id);
#endif

		ret = ov13850_restore_mode(sensor);
		if (ret)
			goto power_off;
	}

	if (sensor->ep.bus_type == V4L2_MBUS_CSI2_DPHY)
		ret = ov13850_set_power_mipi(sensor, on);
	if (ret)
		goto power_off;

	if (!on)
		ov13850_set_power_off(sensor);

	return 0;

power_off:
	ov13850_set_power_off(sensor);
	return ret;
}

static int ov13850_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov13850_dev *sensor = to_ov13850_dev(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);

	/*
	 * If the power count is modified from 0 to != 0 or from != 0 to 0,
	 * update the power state.
	 */
	if (sensor->power_count == !on) {
		ret = ov13850_set_power(sensor, !!on);
		if (ret)
			goto out;
	}

	/* Update the power count. */
	sensor->power_count += on ? 1 : -1;
	WARN_ON(sensor->power_count < 0);
out:
	mutex_unlock(&sensor->lock);

	if (on && !ret && sensor->power_count == 1) {
		/* restore controls */
		ret = v4l2_ctrl_handler_setup(&sensor->ctrls.handler);
	}

	return ret;
}

static int ov13850_try_frame_interval(struct ov13850_dev *sensor,
				struct v4l2_fract *fi,
				u32 width, u32 height)
{
	const struct ov13850_mode_info *mode;
	enum ov13850_frame_rate rate = OV13850_15_FPS;
	int minfps, maxfps, best_fps, fps;
	int i;

	minfps = ov13850_framerates[OV13850_15_FPS];
	maxfps = ov13850_framerates[OV13850_NUM_FRAMERATES - 1];

	if (fi->numerator == 0) {
		fi->denominator = maxfps;
		fi->numerator = 1;
		rate = OV13850_60_FPS;
		goto find_mode;
	}

	fps = clamp_val(DIV_ROUND_CLOSEST(fi->denominator, fi->numerator),
			minfps, maxfps);

	best_fps = minfps;
	for (i = 0; i < ARRAY_SIZE(ov13850_framerates); i++) {
		int curr_fps = ov13850_framerates[i];

		if (abs(curr_fps - fps) < abs(best_fps - fps)) {
			best_fps = curr_fps;
			rate = i;
		}
	}
	st_info(ST_SENSOR, "best_fps = %d, fps = %d\n", best_fps, fps);

	fi->numerator = 1;
	fi->denominator = best_fps;

find_mode:
	mode = ov13850_find_mode(sensor, rate, width, height, false);
	return mode ? rate : -EINVAL;
}

static int ov13850_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad != 0)
		return -EINVAL;

	if (code->index >= ARRAY_SIZE(ov13850_formats))
		return -EINVAL;

	code->code = ov13850_formats[code->index].code;
	return 0;
}

static int ov13850_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *format)
{
	struct ov13850_dev *sensor = to_ov13850_dev(sd);
	struct v4l2_mbus_framefmt *fmt;

	if (format->pad != 0)
		return -EINVAL;

	mutex_lock(&sensor->lock);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt = v4l2_subdev_get_try_format(&sensor->sd, state,
						format->pad);
	else
		fmt = &sensor->fmt;

	format->format = *fmt;

	mutex_unlock(&sensor->lock);

	return 0;
}

static int ov13850_try_fmt_internal(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt,
				enum ov13850_frame_rate fr,
				const struct ov13850_mode_info **new_mode)
{
	struct ov13850_dev *sensor = to_ov13850_dev(sd);
	const struct ov13850_mode_info *mode;
	int i;

	mode = ov13850_find_mode(sensor, fr, fmt->width, fmt->height, true);
	if (!mode)
		return -EINVAL;
	fmt->width = mode->hact;
	fmt->height = mode->vact;

	if (new_mode)
		*new_mode = mode;

	for (i = 0; i < ARRAY_SIZE(ov13850_formats); i++)
		if (ov13850_formats[i].code == fmt->code)
			break;
	if (i >= ARRAY_SIZE(ov13850_formats))
		i = 0;

	fmt->code = ov13850_formats[i].code;
	fmt->colorspace = ov13850_formats[i].colorspace;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);

	return 0;
}

static int ov13850_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *format)
{
	struct ov13850_dev *sensor = to_ov13850_dev(sd);
	const struct ov13850_mode_info *new_mode;
	struct v4l2_mbus_framefmt *mbus_fmt = &format->format;
	struct v4l2_mbus_framefmt *fmt;
	int ret;

	if (format->pad != 0)
		return -EINVAL;

	mutex_lock(&sensor->lock);

	if (sensor->streaming) {
		ret = -EBUSY;
		goto out;
	}

	ret = ov13850_try_fmt_internal(sd, mbus_fmt, 0, &new_mode);
	if (ret)
		goto out;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt = v4l2_subdev_get_try_format(sd, state, 0);
	else
		fmt = &sensor->fmt;

	if (mbus_fmt->code != sensor->fmt.code)
		sensor->pending_fmt_change = true;

	*fmt = *mbus_fmt;

	if (new_mode != sensor->current_mode) {
		sensor->current_mode = new_mode;
		sensor->pending_mode_change = true;
	}

	if (new_mode->max_fps < sensor->current_fr) {
		sensor->current_fr = new_mode->max_fps;
		sensor->frame_interval.numerator = 1;
		sensor->frame_interval.denominator =
			ov13850_framerates[sensor->current_fr];
		sensor->current_mode = new_mode;
		sensor->pending_mode_change = true;
	}

	__v4l2_ctrl_s_ctrl_int64(sensor->ctrls.pixel_rate,
				ov13850_calc_pixel_rate(sensor));
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int ov13850_set_framefmt(struct ov13850_dev *sensor,
			       struct v4l2_mbus_framefmt *format)
{
	u8 fmt;

	switch (format->code) {
	/* Raw, BGBG... / GRGR... */
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		fmt = 0x0;
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		fmt = 0x1;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		fmt = 0x2;
	default:
		return -EINVAL;
	}

	return ov13850_mod_reg(sensor, OV13850_REG_MIPI_SC,
			BIT(1) | BIT(0), fmt);
}

/*
 * Sensor Controls.
 */

static int ov13850_set_ctrl_hue(struct ov13850_dev *sensor, int value)
{
	int ret = 0;

	return ret;
}

static int ov13850_set_ctrl_contrast(struct ov13850_dev *sensor, int value)
{
	int ret = 0;

	return ret;
}

static int ov13850_set_ctrl_saturation(struct ov13850_dev *sensor, int value)
{
	int ret  = 0;

	return ret;
}

static int ov13850_set_ctrl_white_balance(struct ov13850_dev *sensor, int awb)
{
	struct ov13850_ctrls *ctrls = &sensor->ctrls;
	int ret = 0;

	return ret;
}

static int ov13850_set_ctrl_exposure(struct ov13850_dev *sensor,
				enum v4l2_exposure_auto_type auto_exposure)
{
	struct ov13850_ctrls *ctrls = &sensor->ctrls;
	int ret = 0;

	return ret;
}

static const s64 link_freq_menu_items[] = {
	OV13850_LINK_FREQ_500MHZ
};

static const char * const test_pattern_menu[] = {
	"Disabled",
	"Color bars",
	"Color bars w/ rolling bar",
	"Color squares",
	"Color squares w/ rolling bar",
};

static int ov13850_set_ctrl_test_pattern(struct ov13850_dev *sensor, int value)
{
	return 0;
}

static int ov13850_set_ctrl_light_freq(struct ov13850_dev *sensor, int value)
{
	return 0;
}

static int ov13850_set_ctrl_hflip(struct ov13850_dev *sensor, int value)
{
	return 0;
}

static int ov13850_set_ctrl_vflip(struct ov13850_dev *sensor, int value)
{
	return 0;
}

static int ov13850_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct ov13850_dev *sensor = to_ov13850_dev(sd);
	int val;

	/* v4l2_ctrl_lock() locks our own mutex */

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		val = ov13850_get_gain(sensor);
		break;
	}

	return 0;
}

static int ov13850_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct ov13850_dev *sensor = to_ov13850_dev(sd);
	int ret;

	/* v4l2_ctrl_lock() locks our own mutex */

	/*
	 * If the device is not powered up by the host driver do
	 * not apply any controls to H/W at this time. Instead
	 * the controls will be restored right after power-up.
	 */
	if (sensor->power_count == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov13850_set_gain(sensor, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = ov13850_set_ctrl_exposure(sensor, V4L2_EXPOSURE_MANUAL);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret = ov13850_set_ctrl_white_balance(sensor, ctrl->val);
		break;
	case V4L2_CID_HUE:
		ret = ov13850_set_ctrl_hue(sensor, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		ret = ov13850_set_ctrl_contrast(sensor, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		ret = ov13850_set_ctrl_saturation(sensor, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov13850_set_ctrl_test_pattern(sensor, ctrl->val);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		ret = ov13850_set_ctrl_light_freq(sensor, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = ov13850_set_ctrl_hflip(sensor, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ret = ov13850_set_ctrl_vflip(sensor, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ov13850_ctrl_ops = {
	.g_volatile_ctrl = ov13850_g_volatile_ctrl,
	.s_ctrl = ov13850_s_ctrl,
};

static int ov13850_init_controls(struct ov13850_dev *sensor)
{
	const struct v4l2_ctrl_ops *ops = &ov13850_ctrl_ops;
	struct ov13850_ctrls *ctrls = &sensor->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret;

	v4l2_ctrl_handler_init(hdl, 32);

	/* we can use our own mutex for the ctrl lock */
	hdl->lock = &sensor->lock;

	/* Clock related controls */
	ctrls->pixel_rate = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_PIXEL_RATE,
					0, INT_MAX, 1,
					ov13850_calc_pixel_rate(sensor));

	/* Auto/manual white balance */
	ctrls->auto_wb = v4l2_ctrl_new_std(hdl, ops,
					V4L2_CID_AUTO_WHITE_BALANCE,
					0, 1, 1, 0);
	ctrls->blue_balance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BLUE_BALANCE,
						0, 4095, 1, 1024);
	ctrls->red_balance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_RED_BALANCE,
						0, 4095, 1, 1024);

	ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE,
					4, 0xfff8, 1, 0x4c00);
	ctrls->anal_gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_ANALOGUE_GAIN,
					0x10, 0xfff8, 1, 0x0080);
	ctrls->test_pattern =
		v4l2_ctrl_new_std_menu_items(hdl, ops, V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(test_pattern_menu) - 1,
					0, 0, test_pattern_menu);
	ctrls->hflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HFLIP,
					0, 1, 1, 0);
	ctrls->vflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VFLIP,
					0, 1, 1, 0);
	ctrls->light_freq =
		v4l2_ctrl_new_std_menu(hdl, ops,
					V4L2_CID_POWER_LINE_FREQUENCY,
					V4L2_CID_POWER_LINE_FREQUENCY_AUTO, 0,
					V4L2_CID_POWER_LINE_FREQUENCY_50HZ);
	ctrls->link_freq = v4l2_ctrl_new_int_menu(hdl, ops, V4L2_CID_LINK_FREQ,
					0, 0, link_freq_menu_items);
	if (hdl->error) {
		ret = hdl->error;
		goto free_ctrls;
	}

	ctrls->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	ctrls->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	// ctrls->exposure->flags |= V4L2_CTRL_FLAG_VOLATILE;
	// ctrls->anal_gain->flags |= V4L2_CTRL_FLAG_VOLATILE;

	v4l2_ctrl_auto_cluster(3, &ctrls->auto_wb, 0, false);

	sensor->sd.ctrl_handler = hdl;
	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(hdl);
	return ret;
}

static int ov13850_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->pad != 0)
		return -EINVAL;
	if (fse->index >= OV13850_NUM_MODES)
		return -EINVAL;

	fse->min_width =
		ov13850_mode_data[fse->index].hact;
	fse->max_width = fse->min_width;
	fse->min_height =
		ov13850_mode_data[fse->index].vact;
	fse->max_height = fse->min_height;

	return 0;
}

static int ov13850_enum_frame_interval(
	struct v4l2_subdev *sd,
	struct v4l2_subdev_state *state,
	struct v4l2_subdev_frame_interval_enum *fie)
{
	struct ov13850_dev *sensor = to_ov13850_dev(sd);
	struct v4l2_fract tpf;
	int ret;

	if (fie->pad != 0)
		return -EINVAL;
	if (fie->index >= OV13850_NUM_FRAMERATES)
		return -EINVAL;

	tpf.numerator = 1;
	tpf.denominator = ov13850_framerates[fie->index];

/*	ret = ov13850_try_frame_interval(sensor, &tpf,
 *					fie->width, fie->height);
 *	if (ret < 0)
 *		return -EINVAL;
 */
	fie->interval = tpf;

	return 0;
}

static int ov13850_g_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *fi)
{
	struct ov13850_dev *sensor = to_ov13850_dev(sd);

	mutex_lock(&sensor->lock);
	fi->interval = sensor->frame_interval;
	mutex_unlock(&sensor->lock);

	return 0;
}

static int ov13850_s_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *fi)
{
	struct ov13850_dev *sensor = to_ov13850_dev(sd);
	const struct ov13850_mode_info *mode;
	int frame_rate, ret = 0;

	if (fi->pad != 0)
		return -EINVAL;

	mutex_lock(&sensor->lock);

	if (sensor->streaming) {
		ret = -EBUSY;
		goto out;
	}

	mode = sensor->current_mode;

	frame_rate = ov13850_try_frame_interval(sensor, &fi->interval,
					mode->hact, mode->vact);
	if (frame_rate < 0) {
		/* Always return a valid frame interval value */
		fi->interval = sensor->frame_interval;
		goto out;
	}

	mode = ov13850_find_mode(sensor, frame_rate, mode->hact,
				mode->vact, true);
	if (!mode) {
		ret = -EINVAL;
		goto out;
	}

	if (mode != sensor->current_mode ||
		frame_rate != sensor->current_fr) {
		sensor->current_fr = frame_rate;
		sensor->frame_interval = fi->interval;
		sensor->current_mode = mode;
		sensor->pending_mode_change = true;

		__v4l2_ctrl_s_ctrl_int64(sensor->ctrls.pixel_rate,
					ov13850_calc_pixel_rate(sensor));
	}
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int ov13850_stream_start(struct ov13850_dev *sensor, int enable)
{
	int ret;

	if (enable) {		//stream on
		mdelay(1000);
		ret = ov13850_write_reg(sensor, OV13850_STREAM_CTRL, enable);
	} else {			//stream off
		ret = ov13850_write_reg(sensor, OV13850_STREAM_CTRL, enable);
		mdelay(100);
	}

	return ret;
}

static int ov13850_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov13850_dev *sensor = to_ov13850_dev(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);

	if (sensor->streaming == !enable) {
		if (enable && sensor->pending_mode_change) {
			ret = ov13850_set_mode(sensor);
			if (ret)
				goto out;
		}

		if (enable && sensor->pending_fmt_change) {
			ret = ov13850_set_framefmt(sensor, &sensor->fmt);
			if (ret)
				goto out;
			sensor->pending_fmt_change = false;
		}

		if (sensor->ep.bus_type == V4L2_MBUS_CSI2_DPHY)
			ret = ov13850_set_stream_mipi(sensor, enable);

		ret = ov13850_stream_start(sensor, enable);

		if (!ret)
			sensor->streaming = enable;
	}
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static const struct v4l2_subdev_core_ops ov13850_core_ops = {
	.s_power = ov13850_s_power,
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops ov13850_video_ops = {
	.g_frame_interval = ov13850_g_frame_interval,
	.s_frame_interval = ov13850_s_frame_interval,
	.s_stream = ov13850_s_stream,
};

static const struct v4l2_subdev_pad_ops ov13850_pad_ops = {
	.enum_mbus_code = ov13850_enum_mbus_code,
	.get_fmt = ov13850_get_fmt,
	.set_fmt = ov13850_set_fmt,
	.enum_frame_size = ov13850_enum_frame_size,
	.enum_frame_interval = ov13850_enum_frame_interval,
};

static const struct v4l2_subdev_ops ov13850_subdev_ops = {
	.core = &ov13850_core_ops,
	.video = &ov13850_video_ops,
	.pad = &ov13850_pad_ops,
};

static int ov13850_get_regulators(struct ov13850_dev *sensor)
{
	int i;

	for (i = 0; i < OV13850_NUM_SUPPLIES; i++)
		sensor->supplies[i].supply = ov13850_supply_name[i];

	return devm_regulator_bulk_get(&sensor->i2c_client->dev,
					OV13850_NUM_SUPPLIES,
					sensor->supplies);
}

static int ov13850_check_chip_id(struct ov13850_dev *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	int ret = 0;
	u16 chip_id;

	ret = ov13850_set_power_on(sensor);
	if (ret)
		return ret;

#ifdef UNUSED_CODE
	ret = ov13850_read_reg16(sensor, OV13850_REG_CHIP_ID, &chip_id);
	if (ret) {
		dev_err(&client->dev, "%s: failed to read chip identifier\n",
			__func__);
		goto power_off;
	}

	if (chip_id != OV13850_CHIP_ID) {
		dev_err(&client->dev, "%s: wrong chip identifier, expected 0x%x,  got 0x%x\n",
			__func__, OV13850_CHIP_ID, chip_id);
		ret = -ENXIO;
	}
	dev_err(&client->dev, "%s: chip identifier, got 0x%x\n",
		__func__, chip_id);
#endif

power_off:
	ov13850_set_power_off(sensor);
	return ret;
}

static int ov13850_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	struct ov13850_dev *sensor;
	struct v4l2_mbus_framefmt *fmt;
	u32 rotation;
	int ret;
	u8 chip_id_high, chip_id_low;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->i2c_client = client;

	fmt = &sensor->fmt;
	fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->width = 1920;
	fmt->height = 1080;
	fmt->field = V4L2_FIELD_NONE;
	sensor->frame_interval.numerator = 1;
	sensor->frame_interval.denominator = ov13850_framerates[OV13850_30_FPS];
	sensor->current_fr = OV13850_30_FPS;
	sensor->current_mode =
		&ov13850_mode_data[OV13850_MODE_1080P_1920_1080];
	sensor->last_mode = sensor->current_mode;

	sensor->ae_target = 52;

	/* optional indication of physical rotation of sensor */
	ret = fwnode_property_read_u32(dev_fwnode(&client->dev), "rotation",
					&rotation);
	if (!ret) {
		switch (rotation) {
		case 180:
			sensor->upside_down = true;
			fallthrough;
		case 0:
			break;
		default:
			dev_warn(dev, "%u degrees rotation is not supported, ignoring...\n",
				rotation);
		}
	}

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev),
						NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &sensor->ep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(dev, "Could not parse endpoint\n");
		return ret;
	}

	if (sensor->ep.bus_type != V4L2_MBUS_PARALLEL &&
		sensor->ep.bus_type != V4L2_MBUS_CSI2_DPHY &&
		sensor->ep.bus_type != V4L2_MBUS_BT656) {
		dev_err(dev, "Unsupported bus type %d\n", sensor->ep.bus_type);
		return -EINVAL;
	}

	/* get system clock (xclk) */
	sensor->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(sensor->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(sensor->xclk);
	}

	sensor->xclk_freq = clk_get_rate(sensor->xclk);
	if (sensor->xclk_freq < OV13850_XCLK_MIN ||
		sensor->xclk_freq > OV13850_XCLK_MAX) {
		dev_err(dev, "xclk frequency out of range: %d Hz\n",
			sensor->xclk_freq);
		return -EINVAL;
	}

	/* request optional power down pin */
	sensor->pwdn_gpio = devm_gpiod_get_optional(dev, "powerdown",
						GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->pwdn_gpio))
		return PTR_ERR(sensor->pwdn_gpio);

	/* request optional reset pin */
	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset_gpio))
		return PTR_ERR(sensor->reset_gpio);

	v4l2_i2c_subdev_init(&sensor->sd, client, &ov13850_subdev_ops);

	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	ret = ov13850_get_regulators(sensor);
	if (ret)
		return ret;

	mutex_init(&sensor->lock);

	ret = ov13850_check_chip_id(sensor);
	if (ret)
		goto entity_cleanup;

	ret = ov13850_init_controls(sensor);
	if (ret)
		goto entity_cleanup;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto free_ctrls;

	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
entity_cleanup:
	media_entity_cleanup(&sensor->sd.entity);
	mutex_destroy(&sensor->lock);
	return ret;
}

static int ov13850_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov13850_dev *sensor = to_ov13850_dev(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
	mutex_destroy(&sensor->lock);

	return 0;
}

static const struct i2c_device_id ov13850_id[] = {
	{"ov13850", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ov13850_id);

static const struct of_device_id ov13850_dt_ids[] = {
	{ .compatible = "ovti,ov13850" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ov13850_dt_ids);

static struct i2c_driver ov13850_i2c_driver = {
	.driver = {
		.name  = "ov13850",
		.of_match_table = ov13850_dt_ids,
	},
	.id_table = ov13850_id,
	.probe_new = ov13850_probe,
	.remove   = ov13850_remove,
};

module_i2c_driver(ov13850_i2c_driver);

MODULE_DESCRIPTION("OV13850 MIPI Camera Subdev Driver");
MODULE_LICENSE("GPL");
