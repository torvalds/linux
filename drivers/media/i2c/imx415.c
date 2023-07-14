// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Sony IMX415 CMOS Image Sensor.
 *
 * Copyright (C) 2023 WolfVision GmbH.
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define IMX415_PIXEL_ARRAY_TOP	  0
#define IMX415_PIXEL_ARRAY_LEFT	  0
#define IMX415_PIXEL_ARRAY_WIDTH  3864
#define IMX415_PIXEL_ARRAY_HEIGHT 2192
#define IMX415_PIXEL_ARRAY_VBLANK 58

#define IMX415_NUM_CLK_PARAM_REGS 11

#define IMX415_REG_8BIT(n)	  ((1 << 16) | (n))
#define IMX415_REG_16BIT(n)	  ((2 << 16) | (n))
#define IMX415_REG_24BIT(n)	  ((3 << 16) | (n))
#define IMX415_REG_SIZE_SHIFT	  16
#define IMX415_REG_ADDR_MASK	  0xffff

#define IMX415_MODE		  IMX415_REG_8BIT(0x3000)
#define IMX415_MODE_OPERATING	  (0)
#define IMX415_MODE_STANDBY	  BIT(0)
#define IMX415_REGHOLD		  IMX415_REG_8BIT(0x3001)
#define IMX415_REGHOLD_INVALID	  (0)
#define IMX415_REGHOLD_VALID	  BIT(0)
#define IMX415_XMSTA		  IMX415_REG_8BIT(0x3002)
#define IMX415_XMSTA_START	  (0)
#define IMX415_XMSTA_STOP	  BIT(0)
#define IMX415_BCWAIT_TIME	  IMX415_REG_16BIT(0x3008)
#define IMX415_CPWAIT_TIME	  IMX415_REG_16BIT(0x300A)
#define IMX415_WINMODE		  IMX415_REG_8BIT(0x301C)
#define IMX415_ADDMODE		  IMX415_REG_8BIT(0x3022)
#define IMX415_REVERSE		  IMX415_REG_8BIT(0x3030)
#define IMX415_HREVERSE_SHIFT	  (0)
#define IMX415_VREVERSE_SHIFT	  BIT(0)
#define IMX415_ADBIT		  IMX415_REG_8BIT(0x3031)
#define IMX415_MDBIT		  IMX415_REG_8BIT(0x3032)
#define IMX415_SYS_MODE		  IMX415_REG_8BIT(0x3033)
#define IMX415_OUTSEL		  IMX415_REG_8BIT(0x30C0)
#define IMX415_DRV		  IMX415_REG_8BIT(0x30C1)
#define IMX415_VMAX		  IMX415_REG_24BIT(0x3024)
#define IMX415_HMAX		  IMX415_REG_16BIT(0x3028)
#define IMX415_SHR0		  IMX415_REG_24BIT(0x3050)
#define IMX415_GAIN_PCG_0	  IMX415_REG_16BIT(0x3090)
#define IMX415_AGAIN_MIN	  0
#define IMX415_AGAIN_MAX	  100
#define IMX415_AGAIN_STEP	  1
#define IMX415_BLKLEVEL		  IMX415_REG_16BIT(0x30E2)
#define IMX415_BLKLEVEL_DEFAULT	  50
#define IMX415_TPG_EN_DUOUT	  IMX415_REG_8BIT(0x30E4)
#define IMX415_TPG_PATSEL_DUOUT	  IMX415_REG_8BIT(0x30E6)
#define IMX415_TPG_COLORWIDTH	  IMX415_REG_8BIT(0x30E8)
#define IMX415_TESTCLKEN_MIPI	  IMX415_REG_8BIT(0x3110)
#define IMX415_INCKSEL1		  IMX415_REG_8BIT(0x3115)
#define IMX415_INCKSEL2		  IMX415_REG_8BIT(0x3116)
#define IMX415_INCKSEL3		  IMX415_REG_16BIT(0x3118)
#define IMX415_INCKSEL4		  IMX415_REG_16BIT(0x311A)
#define IMX415_INCKSEL5		  IMX415_REG_8BIT(0x311E)
#define IMX415_DIG_CLP_MODE	  IMX415_REG_8BIT(0x32C8)
#define IMX415_WRJ_OPEN		  IMX415_REG_8BIT(0x3390)
#define IMX415_SENSOR_INFO	  IMX415_REG_16BIT(0x3F12)
#define IMX415_SENSOR_INFO_MASK	  0xFFF
#define IMX415_CHIP_ID		  0x514
#define IMX415_LANEMODE		  IMX415_REG_16BIT(0x4001)
#define IMX415_LANEMODE_2	  1
#define IMX415_LANEMODE_4	  3
#define IMX415_TXCLKESC_FREQ	  IMX415_REG_16BIT(0x4004)
#define IMX415_INCKSEL6		  IMX415_REG_8BIT(0x400C)
#define IMX415_TCLKPOST		  IMX415_REG_16BIT(0x4018)
#define IMX415_TCLKPREPARE	  IMX415_REG_16BIT(0x401A)
#define IMX415_TCLKTRAIL	  IMX415_REG_16BIT(0x401C)
#define IMX415_TCLKZERO		  IMX415_REG_16BIT(0x401E)
#define IMX415_THSPREPARE	  IMX415_REG_16BIT(0x4020)
#define IMX415_THSZERO		  IMX415_REG_16BIT(0x4022)
#define IMX415_THSTRAIL		  IMX415_REG_16BIT(0x4024)
#define IMX415_THSEXIT		  IMX415_REG_16BIT(0x4026)
#define IMX415_TLPX		  IMX415_REG_16BIT(0x4028)
#define IMX415_INCKSEL7		  IMX415_REG_8BIT(0x4074)

struct imx415_reg {
	u32 address;
	u32 val;
};

static const char *const imx415_supply_names[] = {
	"dvdd",
	"ovdd",
	"avdd",
};

/*
 * The IMX415 data sheet uses lane rates but v4l2 uses link frequency to
 * describe MIPI CSI-2 speed. This driver uses lane rates wherever possible
 * and converts them to link frequencies by a factor of two when needed.
 */
static const s64 link_freq_menu_items[] = {
	594000000 / 2,	720000000 / 2,	891000000 / 2,
	1440000000 / 2, 1485000000 / 2,
};

struct imx415_clk_params {
	u64 lane_rate;
	u64 inck;
	struct imx415_reg regs[IMX415_NUM_CLK_PARAM_REGS];
};

/* INCK Settings - includes all lane rate and INCK dependent registers */
static const struct imx415_clk_params imx415_clk_params[] = {
	{
		.lane_rate = 594000000,
		.inck = 27000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x05D },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x042 },
		.regs[2] = { IMX415_SYS_MODE, 0x7 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x084 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E7 },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x0 },
		.regs[9] = { IMX415_INCKSEL7, 0x1 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x06C0 },
	},
	{
		.lane_rate = 720000000,
		.inck = 24000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x054 },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x03B },
		.regs[2] = { IMX415_SYS_MODE, 0x9 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x0B4 },
		.regs[6] = { IMX415_INCKSEL4, 0x0FC },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x0 },
		.regs[9] = { IMX415_INCKSEL7, 0x1 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x0600 },
	},
	{
		.lane_rate = 891000000,
		.inck = 27000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x05D },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x042 },
		.regs[2] = { IMX415_SYS_MODE, 0x5 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x0C6 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E7 },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x0 },
		.regs[9] = { IMX415_INCKSEL7, 0x1 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x06C0 },
	},
	{
		.lane_rate = 1440000000,
		.inck = 24000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x054 },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x03B },
		.regs[2] = { IMX415_SYS_MODE, 0x8 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x0B4 },
		.regs[6] = { IMX415_INCKSEL4, 0x0FC },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x0600 },
	},
	{
		.lane_rate = 1485000000,
		.inck = 27000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x05D },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x042 },
		.regs[2] = { IMX415_SYS_MODE, 0x8 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x0A5 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E7 },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x06C0 },
	},
};

/* all-pixel 2-lane 720 Mbps 15.74 Hz mode */
static const struct imx415_reg imx415_mode_2_720[] = {
	{ IMX415_VMAX, 0x08CA },
	{ IMX415_HMAX, 0x07F0 },
	{ IMX415_LANEMODE, IMX415_LANEMODE_2 },
	{ IMX415_TCLKPOST, 0x006F },
	{ IMX415_TCLKPREPARE, 0x002F },
	{ IMX415_TCLKTRAIL, 0x002F },
	{ IMX415_TCLKZERO, 0x00BF },
	{ IMX415_THSPREPARE, 0x002F },
	{ IMX415_THSZERO, 0x0057 },
	{ IMX415_THSTRAIL, 0x002F },
	{ IMX415_THSEXIT, 0x004F },
	{ IMX415_TLPX, 0x0027 },
};

/* all-pixel 2-lane 1440 Mbps 30.01 Hz mode */
static const struct imx415_reg imx415_mode_2_1440[] = {
	{ IMX415_VMAX, 0x08CA },
	{ IMX415_HMAX, 0x042A },
	{ IMX415_LANEMODE, IMX415_LANEMODE_2 },
	{ IMX415_TCLKPOST, 0x009F },
	{ IMX415_TCLKPREPARE, 0x0057 },
	{ IMX415_TCLKTRAIL, 0x0057 },
	{ IMX415_TCLKZERO, 0x0187 },
	{ IMX415_THSPREPARE, 0x005F },
	{ IMX415_THSZERO, 0x00A7 },
	{ IMX415_THSTRAIL, 0x005F },
	{ IMX415_THSEXIT, 0x0097 },
	{ IMX415_TLPX, 0x004F },
};

/* all-pixel 4-lane 891 Mbps 30 Hz mode */
static const struct imx415_reg imx415_mode_4_891[] = {
	{ IMX415_VMAX, 0x08CA },
	{ IMX415_HMAX, 0x044C },
	{ IMX415_LANEMODE, IMX415_LANEMODE_4 },
	{ IMX415_TCLKPOST, 0x007F },
	{ IMX415_TCLKPREPARE, 0x0037 },
	{ IMX415_TCLKTRAIL, 0x0037 },
	{ IMX415_TCLKZERO, 0x00F7 },
	{ IMX415_THSPREPARE, 0x003F },
	{ IMX415_THSZERO, 0x006F },
	{ IMX415_THSTRAIL, 0x003F },
	{ IMX415_THSEXIT, 0x005F },
	{ IMX415_TLPX, 0x002F },
};

struct imx415_mode_reg_list {
	u32 num_of_regs;
	const struct imx415_reg *regs;
};

/*
 * Mode : number of lanes, lane rate and frame rate dependent settings
 *
 * pixel_rate and hmax_pix are needed to calculate hblank for the v4l2 ctrl
 * interface. These values can not be found in the data sheet and should be
 * treated as virtual values. Use following table when adding new modes.
 *
 * lane_rate  lanes    fps     hmax_pix   pixel_rate
 *
 *     594      2     10.000     4400       99000000
 *     891      2     15.000     4400      148500000
 *     720      2     15.748     4064      144000000
 *    1782      2     30.000     4400      297000000
 *    2079      2     30.000     4400      297000000
 *    1440      2     30.019     4510      304615385
 *
 *     594      4     20.000     5500      247500000
 *     594      4     25.000     4400      247500000
 *     720      4     25.000     4400      247500000
 *     720      4     30.019     4510      304615385
 *     891      4     30.000     4400      297000000
 *    1440      4     30.019     4510      304615385
 *    1440      4     60.038     4510      609230769
 *    1485      4     60.000     4400      594000000
 *    1782      4     60.000     4400      594000000
 *    2079      4     60.000     4400      594000000
 *    2376      4     90.164     4392      891000000
 */
struct imx415_mode {
	u64 lane_rate;
	u32 lanes;
	u32 hmax_pix;
	u64 pixel_rate;
	struct imx415_mode_reg_list reg_list;
};

/* mode configs */
static const struct imx415_mode supported_modes[] = {
	{
		.lane_rate = 720000000,
		.lanes = 2,
		.hmax_pix = 4064,
		.pixel_rate = 144000000,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_mode_2_720),
			.regs = imx415_mode_2_720,
		},
	},
	{
		.lane_rate = 1440000000,
		.lanes = 2,
		.hmax_pix = 4510,
		.pixel_rate = 304615385,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_mode_2_1440),
			.regs = imx415_mode_2_1440,
		},
	},
	{
		.lane_rate = 891000000,
		.lanes = 4,
		.hmax_pix = 4400,
		.pixel_rate = 297000000,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_mode_4_891),
			.regs = imx415_mode_4_891,
		},
	},
};

static const struct regmap_config imx415_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
};

static const char *const imx415_test_pattern_menu[] = {
	"disabled",
	"solid black",
	"solid white",
	"solid dark gray",
	"solid light gray",
	"stripes light/dark grey",
	"stripes dark/light grey",
	"stripes black/dark grey",
	"stripes dark grey/black",
	"stripes black/white",
	"stripes white/black",
	"horizontal color bar",
	"vertical color bar",
};

struct imx415 {
	struct device *dev;
	struct clk *clk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(imx415_supply_names)];
	struct gpio_desc *reset;
	struct regmap *regmap;

	const struct imx415_clk_params *clk_params;

	bool streaming;

	struct v4l2_subdev subdev;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;

	unsigned int cur_mode;
	unsigned int num_data_lanes;
};

/*
 * This table includes fixed register settings and a bunch of undocumented
 * registers that have to be set to another value than default.
 */
static const struct imx415_reg imx415_init_table[] = {
	/* use all-pixel readout mode, no flip */
	{ IMX415_WINMODE, 0x00 },
	{ IMX415_ADDMODE, 0x00 },
	{ IMX415_REVERSE, 0x00 },
	/* use RAW 10-bit mode */
	{ IMX415_ADBIT, 0x00 },
	{ IMX415_MDBIT, 0x00 },
	/* output VSYNC on XVS and low on XHS */
	{ IMX415_OUTSEL, 0x22 },
	{ IMX415_DRV, 0x00 },

	/* SONY magic registers */
	{ IMX415_REG_8BIT(0x32D4), 0x21 },
	{ IMX415_REG_8BIT(0x32EC), 0xA1 },
	{ IMX415_REG_8BIT(0x3452), 0x7F },
	{ IMX415_REG_8BIT(0x3453), 0x03 },
	{ IMX415_REG_8BIT(0x358A), 0x04 },
	{ IMX415_REG_8BIT(0x35A1), 0x02 },
	{ IMX415_REG_8BIT(0x36BC), 0x0C },
	{ IMX415_REG_8BIT(0x36CC), 0x53 },
	{ IMX415_REG_8BIT(0x36CD), 0x00 },
	{ IMX415_REG_8BIT(0x36CE), 0x3C },
	{ IMX415_REG_8BIT(0x36D0), 0x8C },
	{ IMX415_REG_8BIT(0x36D1), 0x00 },
	{ IMX415_REG_8BIT(0x36D2), 0x71 },
	{ IMX415_REG_8BIT(0x36D4), 0x3C },
	{ IMX415_REG_8BIT(0x36D6), 0x53 },
	{ IMX415_REG_8BIT(0x36D7), 0x00 },
	{ IMX415_REG_8BIT(0x36D8), 0x71 },
	{ IMX415_REG_8BIT(0x36DA), 0x8C },
	{ IMX415_REG_8BIT(0x36DB), 0x00 },
	{ IMX415_REG_8BIT(0x3724), 0x02 },
	{ IMX415_REG_8BIT(0x3726), 0x02 },
	{ IMX415_REG_8BIT(0x3732), 0x02 },
	{ IMX415_REG_8BIT(0x3734), 0x03 },
	{ IMX415_REG_8BIT(0x3736), 0x03 },
	{ IMX415_REG_8BIT(0x3742), 0x03 },
	{ IMX415_REG_8BIT(0x3862), 0xE0 },
	{ IMX415_REG_8BIT(0x38CC), 0x30 },
	{ IMX415_REG_8BIT(0x38CD), 0x2F },
	{ IMX415_REG_8BIT(0x395C), 0x0C },
	{ IMX415_REG_8BIT(0x3A42), 0xD1 },
	{ IMX415_REG_8BIT(0x3A4C), 0x77 },
	{ IMX415_REG_8BIT(0x3AE0), 0x02 },
	{ IMX415_REG_8BIT(0x3AEC), 0x0C },
	{ IMX415_REG_8BIT(0x3B00), 0x2E },
	{ IMX415_REG_8BIT(0x3B06), 0x29 },
	{ IMX415_REG_8BIT(0x3B98), 0x25 },
	{ IMX415_REG_8BIT(0x3B99), 0x21 },
	{ IMX415_REG_8BIT(0x3B9B), 0x13 },
	{ IMX415_REG_8BIT(0x3B9C), 0x13 },
	{ IMX415_REG_8BIT(0x3B9D), 0x13 },
	{ IMX415_REG_8BIT(0x3B9E), 0x13 },
	{ IMX415_REG_8BIT(0x3BA1), 0x00 },
	{ IMX415_REG_8BIT(0x3BA2), 0x06 },
	{ IMX415_REG_8BIT(0x3BA3), 0x0B },
	{ IMX415_REG_8BIT(0x3BA4), 0x10 },
	{ IMX415_REG_8BIT(0x3BA5), 0x14 },
	{ IMX415_REG_8BIT(0x3BA6), 0x18 },
	{ IMX415_REG_8BIT(0x3BA7), 0x1A },
	{ IMX415_REG_8BIT(0x3BA8), 0x1A },
	{ IMX415_REG_8BIT(0x3BA9), 0x1A },
	{ IMX415_REG_8BIT(0x3BAC), 0xED },
	{ IMX415_REG_8BIT(0x3BAD), 0x01 },
	{ IMX415_REG_8BIT(0x3BAE), 0xF6 },
	{ IMX415_REG_8BIT(0x3BAF), 0x02 },
	{ IMX415_REG_8BIT(0x3BB0), 0xA2 },
	{ IMX415_REG_8BIT(0x3BB1), 0x03 },
	{ IMX415_REG_8BIT(0x3BB2), 0xE0 },
	{ IMX415_REG_8BIT(0x3BB3), 0x03 },
	{ IMX415_REG_8BIT(0x3BB4), 0xE0 },
	{ IMX415_REG_8BIT(0x3BB5), 0x03 },
	{ IMX415_REG_8BIT(0x3BB6), 0xE0 },
	{ IMX415_REG_8BIT(0x3BB7), 0x03 },
	{ IMX415_REG_8BIT(0x3BB8), 0xE0 },
	{ IMX415_REG_8BIT(0x3BBA), 0xE0 },
	{ IMX415_REG_8BIT(0x3BBC), 0xDA },
	{ IMX415_REG_8BIT(0x3BBE), 0x88 },
	{ IMX415_REG_8BIT(0x3BC0), 0x44 },
	{ IMX415_REG_8BIT(0x3BC2), 0x7B },
	{ IMX415_REG_8BIT(0x3BC4), 0xA2 },
	{ IMX415_REG_8BIT(0x3BC8), 0xBD },
	{ IMX415_REG_8BIT(0x3BCA), 0xBD },
};

static inline struct imx415 *to_imx415(struct v4l2_subdev *sd)
{
	return container_of(sd, struct imx415, subdev);
}

static int imx415_read(struct imx415 *sensor, u32 addr)
{
	u8 data[3] = { 0 };
	int ret;

	ret = regmap_raw_read(sensor->regmap, addr & IMX415_REG_ADDR_MASK, data,
			      (addr >> IMX415_REG_SIZE_SHIFT) & 3);
	if (ret < 0)
		return ret;

	return (data[2] << 16) | (data[1] << 8) | data[0];
}

static int imx415_write(struct imx415 *sensor, u32 addr, u32 value)
{
	u8 data[3] = { value & 0xff, (value >> 8) & 0xff, value >> 16 };
	int ret;

	ret = regmap_raw_write(sensor->regmap, addr & IMX415_REG_ADDR_MASK,
			       data, (addr >> IMX415_REG_SIZE_SHIFT) & 3);
	if (ret < 0)
		dev_err_ratelimited(sensor->dev,
				    "%u-bit write to 0x%04x failed: %d\n",
				    ((addr >> IMX415_REG_SIZE_SHIFT) & 3) * 8,
				    addr & IMX415_REG_ADDR_MASK, ret);

	return 0;
}

static int imx415_set_testpattern(struct imx415 *sensor, int val)
{
	int ret;

	if (val) {
		ret = imx415_write(sensor, IMX415_BLKLEVEL, 0x00);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TPG_EN_DUOUT, 0x01);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TPG_PATSEL_DUOUT, val - 1);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TPG_COLORWIDTH, 0x01);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TESTCLKEN_MIPI, 0x20);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_DIG_CLP_MODE, 0x00);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_WRJ_OPEN, 0x00);
	} else {
		ret = imx415_write(sensor, IMX415_BLKLEVEL,
				   IMX415_BLKLEVEL_DEFAULT);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TPG_EN_DUOUT, 0x00);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TESTCLKEN_MIPI, 0x00);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_DIG_CLP_MODE, 0x01);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_WRJ_OPEN, 0x01);
	}
	return 0;
}

static int imx415_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx415 *sensor = container_of(ctrl->handler, struct imx415,
					     ctrls);
	const struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	unsigned int vmax;
	unsigned int flip;

	if (!sensor->streaming)
		return 0;

	state = v4l2_subdev_get_locked_active_state(&sensor->subdev);
	format = v4l2_subdev_get_pad_format(&sensor->subdev, state, 0);

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* clamp the exposure value to VMAX. */
		vmax = format->height + sensor->vblank->cur.val;
		ctrl->val = min_t(int, ctrl->val, vmax);
		return imx415_write(sensor, IMX415_SHR0, vmax - ctrl->val);

	case V4L2_CID_ANALOGUE_GAIN:
		/* analogue gain in 0.3 dB step size */
		return imx415_write(sensor, IMX415_GAIN_PCG_0, ctrl->val);

	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		flip = (sensor->hflip->val << IMX415_HREVERSE_SHIFT) |
		       (sensor->vflip->val << IMX415_VREVERSE_SHIFT);
		return imx415_write(sensor, IMX415_REVERSE, flip);

	case V4L2_CID_TEST_PATTERN:
		return imx415_set_testpattern(sensor, ctrl->val);

	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops imx415_ctrl_ops = {
	.s_ctrl = imx415_s_ctrl,
};

static int imx415_ctrls_init(struct imx415 *sensor)
{
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl *ctrl;
	u64 pixel_rate = supported_modes[sensor->cur_mode].pixel_rate;
	u64 lane_rate = supported_modes[sensor->cur_mode].lane_rate;
	u32 exposure_max = IMX415_PIXEL_ARRAY_HEIGHT +
			   IMX415_PIXEL_ARRAY_VBLANK - 8;
	u32 hblank;
	unsigned int i;
	int ret;

	ret = v4l2_fwnode_device_parse(sensor->dev, &props);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&sensor->ctrls, 10);

	for (i = 0; i < ARRAY_SIZE(link_freq_menu_items); ++i) {
		if (lane_rate == link_freq_menu_items[i] * 2)
			break;
	}
	if (i == ARRAY_SIZE(link_freq_menu_items)) {
		return dev_err_probe(sensor->dev, -EINVAL,
				     "lane rate %llu not supported\n",
				     lane_rate);
	}

	ctrl = v4l2_ctrl_new_int_menu(&sensor->ctrls, &imx415_ctrl_ops,
				      V4L2_CID_LINK_FREQ,
				      ARRAY_SIZE(link_freq_menu_items) - 1, i,
				      link_freq_menu_items);

	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops, V4L2_CID_EXPOSURE,
			  4, exposure_max, 1, exposure_max);

	v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, IMX415_AGAIN_MIN,
			  IMX415_AGAIN_MAX, IMX415_AGAIN_STEP,
			  IMX415_AGAIN_MIN);

	hblank = supported_modes[sensor->cur_mode].hmax_pix -
		 IMX415_PIXEL_ARRAY_WIDTH;
	ctrl = v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
				 V4L2_CID_HBLANK, hblank, hblank, 1, hblank);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	sensor->vblank = v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
					   V4L2_CID_VBLANK,
					   IMX415_PIXEL_ARRAY_VBLANK,
					   IMX415_PIXEL_ARRAY_VBLANK, 1,
					   IMX415_PIXEL_ARRAY_VBLANK);
	if (sensor->vblank)
		sensor->vblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/*
	 * The pixel rate used here is a virtual value and can be used for
	 * calculating the frame rate together with hblank. It may not
	 * necessarily be the physically correct pixel clock.
	 */
	v4l2_ctrl_new_std(&sensor->ctrls, NULL, V4L2_CID_PIXEL_RATE, pixel_rate,
			  pixel_rate, 1, pixel_rate);

	sensor->hflip = v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	sensor->vflip = v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std_menu_items(&sensor->ctrls, &imx415_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx415_test_pattern_menu) - 1,
				     0, 0, imx415_test_pattern_menu);

	v4l2_ctrl_new_fwnode_properties(&sensor->ctrls, &imx415_ctrl_ops,
					&props);

	if (sensor->ctrls.error) {
		dev_err_probe(sensor->dev, sensor->ctrls.error,
			      "failed to add controls\n");
		v4l2_ctrl_handler_free(&sensor->ctrls);
		return sensor->ctrls.error;
	}
	sensor->subdev.ctrl_handler = &sensor->ctrls;

	return 0;
}

static int imx415_set_mode(struct imx415 *sensor, int mode)
{
	const struct imx415_reg *reg;
	unsigned int i;
	int ret = 0;

	if (mode >= ARRAY_SIZE(supported_modes)) {
		dev_err(sensor->dev, "Mode %d not supported\n", mode);
		return -EINVAL;
	}

	for (i = 0; i < supported_modes[mode].reg_list.num_of_regs; ++i) {
		reg = &supported_modes[mode].reg_list.regs[i];
		ret = imx415_write(sensor, reg->address, reg->val);
		if (ret)
			return ret;
	}

	for (i = 0; i < IMX415_NUM_CLK_PARAM_REGS; ++i) {
		reg = &sensor->clk_params->regs[i];
		ret = imx415_write(sensor, reg->address, reg->val);
		if (ret)
			return ret;
	}

	return 0;
}

static int imx415_setup(struct imx415 *sensor, struct v4l2_subdev_state *state)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(imx415_init_table); ++i) {
		ret = imx415_write(sensor, imx415_init_table[i].address,
				   imx415_init_table[i].val);
		if (ret)
			return ret;
	}

	return imx415_set_mode(sensor, sensor->cur_mode);
}

static int imx415_wakeup(struct imx415 *sensor)
{
	int ret;

	ret = imx415_write(sensor, IMX415_MODE, IMX415_MODE_OPERATING);
	if (ret)
		return ret;

	/*
	 * According to the datasheet we have to wait at least 63 us after
	 * leaving standby mode. But this doesn't work even after 30 ms.
	 * So probably this should be 63 ms and therefore we wait for 80 ms.
	 */
	msleep(80);

	return 0;
}

static int imx415_stream_on(struct imx415 *sensor)
{
	int ret;

	ret = imx415_wakeup(sensor);
	if (ret)
		return ret;

	return imx415_write(sensor, IMX415_XMSTA, IMX415_XMSTA_START);
}

static int imx415_stream_off(struct imx415 *sensor)
{
	int ret;

	ret = imx415_write(sensor, IMX415_XMSTA, IMX415_XMSTA_STOP);
	if (ret)
		return ret;

	return imx415_write(sensor, IMX415_MODE, IMX415_MODE_STANDBY);
}

static int imx415_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx415 *sensor = to_imx415(sd);
	struct v4l2_subdev_state *state;
	int ret;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (!enable) {
		ret = imx415_stream_off(sensor);

		pm_runtime_mark_last_busy(sensor->dev);
		pm_runtime_put_autosuspend(sensor->dev);

		sensor->streaming = false;

		goto unlock;
	}

	ret = pm_runtime_resume_and_get(sensor->dev);
	if (ret < 0)
		goto unlock;

	ret = imx415_setup(sensor, state);
	if (ret)
		goto err_pm;

	/*
	 * Set streaming to true to ensure __v4l2_ctrl_handler_setup() will set
	 * the controls. The flag is reset to false further down if an error
	 * occurs.
	 */
	sensor->streaming = true;

	ret = __v4l2_ctrl_handler_setup(&sensor->ctrls);
	if (ret < 0)
		goto err_pm;

	ret = imx415_stream_on(sensor);
	if (ret)
		goto err_pm;

	ret = 0;

unlock:
	v4l2_subdev_unlock_state(state);

	return ret;

err_pm:
	/*
	 * In case of error, turn the power off synchronously as the device
	 * likely has no other chance to recover.
	 */
	pm_runtime_put_sync(sensor->dev);
	sensor->streaming = false;

	goto unlock;
}

static int imx415_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGBRG10_1X10;

	return 0;
}

static int imx415_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	const struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_pad_format(sd, state, fse->pad);

	if (fse->index > 0 || fse->code != format->code)
		return -EINVAL;

	fse->min_width = IMX415_PIXEL_ARRAY_WIDTH;
	fse->max_width = fse->min_width;
	fse->min_height = IMX415_PIXEL_ARRAY_HEIGHT;
	fse->max_height = fse->min_height;
	return 0;
}

static int imx415_get_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_format *fmt)
{
	fmt->format = *v4l2_subdev_get_pad_format(sd, state, fmt->pad);

	return 0;
}

static int imx415_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_pad_format(sd, state, fmt->pad);

	format->width = fmt->format.width;
	format->height = fmt->format.height;
	format->code = MEDIA_BUS_FMT_SGBRG10_1X10;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_RAW;
	format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	format->quantization = V4L2_QUANTIZATION_DEFAULT;
	format->xfer_func = V4L2_XFER_FUNC_NONE;

	fmt->format = *format;
	return 0;
}

static int imx415_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = IMX415_PIXEL_ARRAY_TOP;
		sel->r.left = IMX415_PIXEL_ARRAY_LEFT;
		sel->r.width = IMX415_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX415_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

static int imx415_init_cfg(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format format = {
		.format = {
			.width = IMX415_PIXEL_ARRAY_WIDTH,
			.height = IMX415_PIXEL_ARRAY_HEIGHT,
		},
	};

	imx415_set_format(sd, state, &format);

	return 0;
}

static const struct v4l2_subdev_video_ops imx415_subdev_video_ops = {
	.s_stream = imx415_s_stream,
};

static const struct v4l2_subdev_pad_ops imx415_subdev_pad_ops = {
	.enum_mbus_code = imx415_enum_mbus_code,
	.enum_frame_size = imx415_enum_frame_size,
	.get_fmt = imx415_get_format,
	.set_fmt = imx415_set_format,
	.get_selection = imx415_get_selection,
	.init_cfg = imx415_init_cfg,
};

static const struct v4l2_subdev_ops imx415_subdev_ops = {
	.video = &imx415_subdev_video_ops,
	.pad = &imx415_subdev_pad_ops,
};

static int imx415_subdev_init(struct imx415 *sensor)
{
	struct i2c_client *client = to_i2c_client(sensor->dev);
	int ret;

	v4l2_i2c_subdev_init(&sensor->subdev, client, &imx415_subdev_ops);

	ret = imx415_ctrls_init(sensor);
	if (ret)
		return ret;

	sensor->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
				V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->subdev.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->subdev.entity, 1, &sensor->pad);
	if (ret < 0) {
		v4l2_ctrl_handler_free(&sensor->ctrls);
		return ret;
	}

	sensor->subdev.state_lock = sensor->subdev.ctrl_handler->lock;
	v4l2_subdev_init_finalize(&sensor->subdev);

	return 0;
}

static void imx415_subdev_cleanup(struct imx415 *sensor)
{
	media_entity_cleanup(&sensor->subdev.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls);
}

static int imx415_power_on(struct imx415 *sensor)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(sensor->supplies),
				    sensor->supplies);
	if (ret < 0)
		return ret;

	gpiod_set_value_cansleep(sensor->reset, 0);

	udelay(1);

	ret = clk_prepare_enable(sensor->clk);
	if (ret < 0)
		goto err_reset;

	/*
	 * Data sheet states that 20 us are required before communication start,
	 * but this doesn't work in all cases. Use 100 us to be on the safe
	 * side.
	 */
	usleep_range(100, 200);

	return 0;

err_reset:
	gpiod_set_value_cansleep(sensor->reset, 1);
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
	return ret;
}

static void imx415_power_off(struct imx415 *sensor)
{
	clk_disable_unprepare(sensor->clk);
	gpiod_set_value_cansleep(sensor->reset, 1);
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
}

static int imx415_identify_model(struct imx415 *sensor)
{
	int model, ret;

	/*
	 * While most registers can be read when the sensor is in standby, this
	 * is not the case of the sensor info register :-(
	 */
	ret = imx415_wakeup(sensor);
	if (ret)
		return dev_err_probe(sensor->dev, ret,
				     "failed to get sensor out of standby\n");

	ret = imx415_read(sensor, IMX415_SENSOR_INFO);
	if (ret < 0) {
		dev_err_probe(sensor->dev, ret,
			      "failed to read sensor information\n");
		goto done;
	}

	model = ret & IMX415_SENSOR_INFO_MASK;

	switch (model) {
	case IMX415_CHIP_ID:
		dev_info(sensor->dev, "Detected IMX415 image sensor\n");
		break;
	default:
		ret = dev_err_probe(sensor->dev, -ENODEV,
				    "invalid device model 0x%04x\n", model);
		goto done;
	}

	ret = 0;

done:
	imx415_write(sensor, IMX415_MODE, IMX415_MODE_STANDBY);
	return ret;
}

static int imx415_check_inck(unsigned long inck, u64 link_frequency)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx415_clk_params); ++i) {
		if ((imx415_clk_params[i].lane_rate == link_frequency * 2) &&
		    imx415_clk_params[i].inck == inck)
			break;
	}

	if (i == ARRAY_SIZE(imx415_clk_params))
		return -EINVAL;
	else
		return 0;
}

static int imx415_parse_hw_config(struct imx415 *sensor)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *ep;
	u64 lane_rate;
	unsigned long inck;
	unsigned int i, j;
	int ret;

	for (i = 0; i < ARRAY_SIZE(sensor->supplies); ++i)
		sensor->supplies[i].supply = imx415_supply_names[i];

	ret = devm_regulator_bulk_get(sensor->dev, ARRAY_SIZE(sensor->supplies),
				      sensor->supplies);
	if (ret)
		return dev_err_probe(sensor->dev, ret,
				     "failed to get supplies\n");

	sensor->reset = devm_gpiod_get_optional(sensor->dev, "reset",
						GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset))
		return dev_err_probe(sensor->dev, PTR_ERR(sensor->reset),
				     "failed to get reset GPIO\n");

	sensor->clk = devm_clk_get(sensor->dev, "inck");
	if (IS_ERR(sensor->clk))
		return dev_err_probe(sensor->dev, PTR_ERR(sensor->clk),
				     "failed to get clock\n");

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(sensor->dev), NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	switch (bus_cfg.bus.mipi_csi2.num_data_lanes) {
	case 2:
	case 4:
		sensor->num_data_lanes = bus_cfg.bus.mipi_csi2.num_data_lanes;
		break;
	default:
		ret = dev_err_probe(sensor->dev, -EINVAL,
				    "invalid number of CSI2 data lanes %d\n",
				    bus_cfg.bus.mipi_csi2.num_data_lanes);
		goto done_endpoint_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		ret = dev_err_probe(sensor->dev, -EINVAL,
				    "no link frequencies defined");
		goto done_endpoint_free;
	}

	/*
	 * Check if there exists a sensor mode defined for current INCK,
	 * number of lanes and given lane rates.
	 */
	inck = clk_get_rate(sensor->clk);
	for (i = 0; i < bus_cfg.nr_of_link_frequencies; ++i) {
		if (imx415_check_inck(inck, bus_cfg.link_frequencies[i])) {
			dev_dbg(sensor->dev,
				"INCK %lu Hz not supported for this link freq",
				inck);
			continue;
		}

		for (j = 0; j < ARRAY_SIZE(supported_modes); ++j) {
			if (sensor->num_data_lanes != supported_modes[j].lanes)
				continue;
			if (bus_cfg.link_frequencies[i] * 2 !=
			    supported_modes[j].lane_rate)
				continue;
			sensor->cur_mode = j;
			break;
		}
		if (j < ARRAY_SIZE(supported_modes))
			break;
	}
	if (i == bus_cfg.nr_of_link_frequencies) {
		ret = dev_err_probe(sensor->dev, -EINVAL,
				    "no valid sensor mode defined\n");
		goto done_endpoint_free;
	}

	lane_rate = supported_modes[sensor->cur_mode].lane_rate;
	for (i = 0; i < ARRAY_SIZE(imx415_clk_params); ++i) {
		if (lane_rate == imx415_clk_params[i].lane_rate &&
		    inck == imx415_clk_params[i].inck) {
			sensor->clk_params = &imx415_clk_params[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(imx415_clk_params)) {
		ret = dev_err_probe(sensor->dev, -EINVAL,
				    "Mode %d not supported\n",
				    sensor->cur_mode);
		goto done_endpoint_free;
	}

	ret = 0;
	dev_dbg(sensor->dev, "clock: %lu Hz, lane_rate: %llu bps, lanes: %d\n",
		inck, lane_rate, sensor->num_data_lanes);

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int imx415_probe(struct i2c_client *client)
{
	struct imx415 *sensor;
	int ret;

	sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->dev = &client->dev;

	ret = imx415_parse_hw_config(sensor);
	if (ret)
		return ret;

	sensor->regmap = devm_regmap_init_i2c(client, &imx415_regmap_config);
	if (IS_ERR(sensor->regmap))
		return PTR_ERR(sensor->regmap);

	/*
	 * Enable power management. The driver supports runtime PM, but needs to
	 * work when runtime PM is disabled in the kernel. To that end, power
	 * the sensor on manually here, identify it, and fully initialize it.
	 */
	ret = imx415_power_on(sensor);
	if (ret)
		return ret;

	ret = imx415_identify_model(sensor);
	if (ret)
		goto err_power;

	ret = imx415_subdev_init(sensor);
	if (ret)
		goto err_power;

	/*
	 * Enable runtime PM. As the device has been powered manually, mark it
	 * as active, and increase the usage count without resuming the device.
	 */
	pm_runtime_set_active(sensor->dev);
	pm_runtime_get_noresume(sensor->dev);
	pm_runtime_enable(sensor->dev);

	ret = v4l2_async_register_subdev_sensor(&sensor->subdev);
	if (ret < 0)
		goto err_pm;

	/*
	 * Finally, enable autosuspend and decrease the usage count. The device
	 * will get suspended after the autosuspend delay, turning the power
	 * off.
	 */
	pm_runtime_set_autosuspend_delay(sensor->dev, 1000);
	pm_runtime_use_autosuspend(sensor->dev);
	pm_runtime_put_autosuspend(sensor->dev);

	return 0;

err_pm:
	pm_runtime_disable(sensor->dev);
	pm_runtime_put_noidle(sensor->dev);
	imx415_subdev_cleanup(sensor);
err_power:
	imx415_power_off(sensor);
	return ret;
}

static void imx415_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct imx415 *sensor = to_imx415(subdev);

	v4l2_async_unregister_subdev(subdev);

	imx415_subdev_cleanup(sensor);

	/*
	 * Disable runtime PM. In case runtime PM is disabled in the kernel,
	 * make sure to turn power off manually.
	 */
	pm_runtime_disable(sensor->dev);
	if (!pm_runtime_status_suspended(sensor->dev))
		imx415_power_off(sensor);
	pm_runtime_set_suspended(sensor->dev);
}

static int imx415_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct imx415 *sensor = to_imx415(subdev);

	return imx415_power_on(sensor);
}

static int imx415_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct imx415 *sensor = to_imx415(subdev);

	imx415_power_off(sensor);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(imx415_pm_ops, imx415_runtime_suspend,
				 imx415_runtime_resume, NULL);

static const struct of_device_id imx415_of_match[] = {
	{ .compatible = "sony,imx415" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, imx415_of_match);

static struct i2c_driver imx415_driver = {
	.probe = imx415_probe,
	.remove = imx415_remove,
	.driver = {
		.name = "imx415",
		.of_match_table = imx415_of_match,
		.pm = pm_ptr(&imx415_pm_ops),
	},
};

module_i2c_driver(imx415_driver);

MODULE_DESCRIPTION("Sony IMX415 image sensor driver");
MODULE_AUTHOR("Gerald Loacker <gerald.loacker@wolfvision.net>");
MODULE_AUTHOR("Michael Riesch <michael.riesch@wolfvision.net>");
MODULE_LICENSE("GPL");
