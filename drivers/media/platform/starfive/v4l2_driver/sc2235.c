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
#include <linux/pm_runtime.h>
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

/* min/typical/max system clock (xclk) frequencies */
#define SC2235_XCLK_MIN			6000000
#define SC2235_XCLK_MAX			27000000

#define SC2235_CHIP_ID		(0x2235)

#define SC2235_REG_CHIP_ID				0x3107
#define SC2235_REG_AEC_PK_MANUAL		0x3e03
#define SC2235_REG_AEC_PK_EXPOSURE_HI	0x3e01
#define SC2235_REG_AEC_PK_EXPOSURE_LO	0x3e02
#define SC2235_REG_AEC_PK_REAL_GAIN		0x3e08
#define SC2235_REG_TIMING_HTS			0x320c
#define SC2235_REG_TIMING_VTS			0x320e
#define SC2235_REG_TEST_SET0			0x4501
#define SC2235_REG_TEST_SET1			0x3902
#define SC2235_REG_TIMING_TC_REG21		0x3221
#define SC2235_REG_SC_PLL_CTRL0			0x3039
#define SC2235_REG_SC_PLL_CTRL1			0x303a
#define SC2235_REG_STREAM_ON            0x0100

enum sc2235_mode_id {
	SC2235_MODE_1080P_1920_1080 = 0,
	SC2235_NUM_MODES,
};

enum sc2235_frame_rate {
	SC2235_15_FPS = 0,
	SC2235_30_FPS,
	SC2235_NUM_FRAMERATES,
};

struct sc2235_pixfmt {
	u32 code;
	u32 colorspace;
};

static const struct sc2235_pixfmt sc2235_formats[] = {
	{ MEDIA_BUS_FMT_SBGGR10_1X10, V4L2_COLORSPACE_SRGB, },
};

static const int sc2235_framerates[] = {
	[SC2235_15_FPS] = 15,
	[SC2235_30_FPS] = 30,
};

/* regulator supplies */
static const char * const sc2235_supply_name[] = {
	"DOVDD", /* Digital I/O (1.8V) supply */
	"AVDD",  /* Analog (2.8V) supply */
	"DVDD",  /* Digital Core (1.5V) supply */
};

#define SC2235_NUM_SUPPLIES ARRAY_SIZE(sc2235_supply_name)

struct reg_value {
	u16 reg_addr;
	u8 val;
	u8 mask;
	u32 delay_ms;
};

struct sc2235_mode_info {
	enum sc2235_mode_id id;
	u32 hact;
	u32 htot;
	u32 vact;
	u32 vtot;
	const struct reg_value *reg_data;
	u32 reg_data_size;
	u32 max_fps;
};

struct sc2235_ctrls {
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl *pixel_rate;
	struct {
		struct v4l2_ctrl *auto_exp;
		struct v4l2_ctrl *exposure;
	};
	struct {
		struct v4l2_ctrl *auto_wb;
		struct v4l2_ctrl *blue_balance;
		struct v4l2_ctrl *red_balance;
	};
	struct {
		struct v4l2_ctrl *auto_gain;
		struct v4l2_ctrl *gain;
	};
	struct v4l2_ctrl *brightness;
	struct v4l2_ctrl *light_freq;
	struct v4l2_ctrl *saturation;
	struct v4l2_ctrl *contrast;
	struct v4l2_ctrl *hue;
	struct v4l2_ctrl *test_pattern;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
};

struct sc2235_dev {
	struct i2c_client *i2c_client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_fwnode_endpoint ep; /* the parsed DT endpoint info */
	struct clk *xclk; /* system clock to SC2235 */
	u32 xclk_freq;

	struct regulator_bulk_data supplies[SC2235_NUM_SUPPLIES];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	bool   upside_down;

	/* lock to protect all members below */
	struct mutex lock;

	struct v4l2_mbus_framefmt fmt;
	bool pending_fmt_change;

	const struct sc2235_mode_info *current_mode;
	const struct sc2235_mode_info *last_mode;
	enum sc2235_frame_rate current_fr;
	struct v4l2_fract frame_interval;

	struct sc2235_ctrls ctrls;

	bool pending_mode_change;
	int streaming;
};

static inline struct sc2235_dev *to_sc2235_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sc2235_dev, sd);
}

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct sc2235_dev,
				ctrls.handler)->sd;
}

/* sc2235 initial register 30fps*/
static struct reg_value sc2235_init_regs_tbl_1080[] = {
	{0x0103, 0x01, 0, 50},
	{0x0100, 0x00, 0, 0},
	{0x3039, 0x80, 0, 0},
	{0x3621, 0x28, 0, 0},

	{0x3309, 0x60, 0, 0},
	{0x331f, 0x4d, 0, 0},
	{0x3321, 0x4f, 0, 0},
	{0x33b5, 0x10, 0, 0},

	{0x3303, 0x20, 0, 0},
	{0x331e, 0x0d, 0, 0},
	{0x3320, 0x0f, 0, 0},

	{0x3622, 0x02, 0, 0},
	{0x3633, 0x42, 0, 0},
	{0x3634, 0x42, 0, 0},

	{0x3306, 0x66, 0, 0},
	{0x330b, 0xd1, 0, 0},

	{0x3301, 0x0e, 0, 0},

	{0x320c, 0x08, 0, 0},
	{0x320d, 0x98, 0, 0},

	{0x3364, 0x05, 0, 0},		// [2] 1: write at sampling ending

	{0x363c, 0x28, 0, 0},		//bypass nvdd
	{0x363b, 0x0a, 0, 0},		//HVDD
	{0x3635, 0xa0, 0, 0},		//TXVDD

	{0x4500, 0x59, 0, 0},
	{0x3d08, 0x00, 0, 0},
	{0x3908, 0x11, 0, 0},

	{0x363c, 0x08, 0, 0},

	{0x3e03, 0x03, 0, 0},
	{0x3e01, 0x46, 0, 0},

	//0703
	{0x3381, 0x0a, 0, 0},
	{0x3348, 0x09, 0, 0},
	{0x3349, 0x50, 0, 0},
	{0x334a, 0x02, 0, 0},
	{0x334b, 0x60, 0, 0},

	{0x3380, 0x04, 0, 0},
	{0x3340, 0x06, 0, 0},
	{0x3341, 0x50, 0, 0},
	{0x3342, 0x02, 0, 0},
	{0x3343, 0x60, 0, 0},

	//0707

	{0x3632, 0x88, 0, 0},		//anti sm
	{0x3309, 0xa0, 0, 0},
	{0x331f, 0x8d, 0, 0},
	{0x3321, 0x8f, 0, 0},

	{0x335e, 0x01, 0, 0},		//ana dithering
	{0x335f, 0x03, 0, 0},
	{0x337c, 0x04, 0, 0},
	{0x337d, 0x06, 0, 0},
	{0x33a0, 0x05, 0, 0},
	{0x3301, 0x05, 0, 0},

	{0x337f, 0x03, 0, 0},
	{0x3368, 0x02, 0, 0},
	{0x3369, 0x00, 0, 0},
	{0x336a, 0x00, 0, 0},
	{0x336b, 0x00, 0, 0},
	{0x3367, 0x08, 0, 0},
	{0x330e, 0x30, 0, 0},

	{0x3366, 0x7c, 0, 0},		// div_rst gap

	{0x3635, 0xc1, 0, 0},
	{0x363b, 0x09, 0, 0},
	{0x363c, 0x07, 0, 0},

	{0x391e, 0x00, 0, 0},

	{0x3637, 0x14, 0, 0},		//fullwell 7K

	{0x3306, 0x54, 0, 0},
	{0x330b, 0xd8, 0, 0},
	{0x366e, 0x08, 0, 0},		// ofs auto en [3]
	{0x366f, 0x2f, 0, 0},

	{0x3631, 0x84, 0, 0},
	{0x3630, 0x48, 0, 0},
	{0x3622, 0x06, 0, 0},

	//ramp by sc
	{0x3638, 0x1f, 0, 0},
	{0x3625, 0x02, 0, 0},
	{0x3636, 0x24, 0, 0},

	//0714
	{0x3348, 0x08, 0, 0},
	{0x3e03, 0x0b, 0, 0},

	//7.17 fpn
	{0x3342, 0x03, 0, 0},
	{0x3343, 0xa0, 0, 0},
	{0x334a, 0x03, 0, 0},
	{0x334b, 0xa0, 0, 0},

	//0718
	{0x3343, 0xb0, 0, 0},
	{0x334b, 0xb0, 0, 0},

	//0720
	//digital ctrl
	{0x3802, 0x01, 0, 0},
	{0x3235, 0x04, 0, 0},
	{0x3236, 0x63, 0, 0},		// vts-2

	//fpn
	{0x3343, 0xd0, 0, 0},
	{0x334b, 0xd0, 0, 0},
	{0x3348, 0x07, 0, 0},
	{0x3349, 0x80, 0, 0},

	//0724
	{0x391b, 0x4d, 0, 0},

	{0x3342, 0x04, 0, 0},
	{0x3343, 0x20, 0, 0},
	{0x334a, 0x04, 0, 0},
	{0x334b, 0x20, 0, 0},

	//0804
	{0x3222, 0x29, 0, 0},
	{0x3901, 0x02, 0, 0},

	//0808

	// auto blc
	{0x3900, 0xD5, 0, 0},		// Bit[0]: blc_enable
	{0x3902, 0x45, 0, 0},		// Bit[6]: blc_auto_en

	// blc target
	{0x3907, 0x00, 0, 0},
	{0x3908, 0x00, 0, 0},

	// auto dpc
	{0x5000, 0x00, 0, 0},		// Bit[2]: white dead pixel cancel enable, Bit[1]: black dead pixel cancel enable

	//digital ctrl
	{0x3f00, 0x07, 0, 0},		// bit[2] = 1
	{0x3f04, 0x08, 0, 0},
	{0x3f05, 0x74, 0, 0},		// hts - { 0x24

	//0809
	{0x330b, 0xc8, 0, 0},

	//0817
	{0x3306, 0x4a, 0, 0},
	{0x330b, 0xca, 0, 0},
	{0x3639, 0x09, 0, 0},

	//manual DPC
	{0x5780, 0xff, 0, 0},
	{0x5781, 0x04, 0, 0},
	{0x5785, 0x18, 0, 0},

	//0822
	{0x3039, 0x35, 0, 0},		//fps
	{0x303a, 0x2e, 0, 0},
	{0x3034, 0x05, 0, 0},
	{0x3035, 0x2a, 0, 0},

	{0x320c, 0x08, 0, 0},
	{0x320d, 0xca, 0, 0},
	{0x320e, 0x04, 0, 0},
	{0x320f, 0xb0, 0, 0},

	{0x3f04, 0x08, 0, 0},
	{0x3f05, 0xa6, 0, 0},		// hts - { 0x24

	{0x3235, 0x04, 0, 0},
	{0x3236, 0xae, 0, 0},		// vts-2

	//0825
	{0x3313, 0x05, 0, 0},
	{0x3678, 0x42, 0, 0},

	//for AE control per frame
	{0x3670, 0x00, 0, 0},
	{0x3633, 0x42, 0, 0},

	{0x3802, 0x00, 0, 0},

	//20180126
	{0x3677, 0x3f, 0, 0},
	{0x3306, 0x44, 0, 0},		//20180126[3c },4a]
	{0x330b, 0xca, 0, 0},		//20180126[c2 },d3]

	//20180202
	{0x3237, 0x08, 0, 0},
	{0x3238, 0x9a, 0, 0},		//hts-0x30

	//20180417
	{0x3640, 0x01, 0, 0},
	{0x3641, 0x02, 0, 0},

	{0x3301, 0x12, 0, 0},		//[8 },15]20180126
	{0x3631, 0x84, 0, 0},
	{0x366f, 0x2f, 0, 0},
	{0x3622, 0xc6, 0, 0},		//20180117

	{0x3e03, 0x03, 0, 0},		// Bit[3]: AGC table mapping method, Bit[1]: AGC manual, BIt[0]: AEC manual

	// {0x0100, 0x00, 0, 0},
	// {0x4501, 0xc8, 0, 0},	//bar testing
	// {0x3902, 0x45, 0, 0},
};

static struct reg_value sc2235_setting_1080P_1920_1080[] = {

};

/* power-on sensor init reg table */
static const struct sc2235_mode_info sc2235_mode_init_data = {
	SC2235_MODE_1080P_1920_1080,
	1920, 0x8ca, 1080, 0x4b0,
	sc2235_init_regs_tbl_1080,
	ARRAY_SIZE(sc2235_init_regs_tbl_1080),
	SC2235_30_FPS,
};

static const struct sc2235_mode_info
sc2235_mode_data[SC2235_NUM_MODES] = {
	{SC2235_MODE_1080P_1920_1080,
	 1920, 0x8ca, 1080, 0x4b0,
	 sc2235_setting_1080P_1920_1080,
	 ARRAY_SIZE(sc2235_setting_1080P_1920_1080),
	 SC2235_30_FPS},
};

static int sc2235_write_reg(struct sc2235_dev *sensor, u16 reg, u8 val)
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

static int sc2235_read_reg(struct sc2235_dev *sensor, u16 reg, u8 *val)
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

static int sc2235_read_reg16(struct sc2235_dev *sensor, u16 reg, u16 *val)
{
	u8 hi, lo;
	int ret;

	ret = sc2235_read_reg(sensor, reg, &hi);
	if (ret)
		return ret;
	ret = sc2235_read_reg(sensor, reg + 1, &lo);
	if (ret)
		return ret;

	*val = ((u16)hi << 8) | (u16)lo;
	return 0;
}

static int sc2235_write_reg16(struct sc2235_dev *sensor, u16 reg, u16 val)
{
	int ret;

	ret = sc2235_write_reg(sensor, reg, val >> 8);
	if (ret)
		return ret;

	return sc2235_write_reg(sensor, reg + 1, val & 0xff);
}

static int sc2235_mod_reg(struct sc2235_dev *sensor, u16 reg,
			u8 mask, u8 val)
{
	u8 readval;
	int ret;

	ret = sc2235_read_reg(sensor, reg, &readval);
	if (ret)
		return ret;

	readval &= ~mask;
	val &= mask;
	val |= readval;

	return sc2235_write_reg(sensor, reg, val);
}

#define SC2235_PLL_PREDIV	3

#define SC2235_SYSDIV_MIN	0
#define SC2235_SYSDIV_MAX	7

#define SC2235_PLL_MULT_MIN	0
#define SC2235_PLL_MULT_MAX	63

#ifdef UNUSED_CODE
static unsigned long sc2235_compute_sys_clk(struct sc2235_dev *sensor,
					u8 pll_pre, u8 pll_mult,
					u8 sysdiv)
{
	unsigned long sysclk =
		sensor->xclk_freq * (64 - pll_mult) / (pll_pre * (sysdiv + 1));

	/* PLL1 output cannot exceed 1GHz. */
	if (sysclk / 1000000 > 1000)
		return 0;

	return sysclk;
}

static unsigned long sc2235_calc_sys_clk(struct sc2235_dev *sensor,
					unsigned long rate,
					u8 *pll_prediv, u8 *pll_mult,
					u8 *sysdiv)
{
	unsigned long best = ~0;
	u8 best_sysdiv = 1, best_mult = 1;
	u8 _sysdiv, _pll_mult;

	for (_sysdiv = SC2235_SYSDIV_MIN;
		_sysdiv <= SC2235_SYSDIV_MAX;
		_sysdiv++) {
		for (_pll_mult = SC2235_PLL_MULT_MIN;
			_pll_mult <= SC2235_PLL_MULT_MAX;
			_pll_mult++) {
			unsigned long _rate;

			_rate = sc2235_compute_sys_clk(sensor,
							SC2235_PLL_PREDIV,
							_pll_mult, _sysdiv);

			/*
			 * We have reached the maximum allowed PLL1 output,
			 * increase sysdiv.
			 */
			if (!_rate)
				break;

			/*
			 * Prefer rates above the expected clock rate than
			 * below, even if that means being less precise.
			 */
			if (_rate < rate)
				continue;

			if (abs(rate - _rate) < abs(rate - best)) {
				best = _rate;
				best_sysdiv = _sysdiv;
				best_mult = _pll_mult;
			}

			if (_rate == rate)
				goto out;
		}
	}

out:
	*sysdiv = best_sysdiv;
	*pll_prediv = SC2235_PLL_PREDIV;
	*pll_mult = best_mult;

	return best;
}
#endif

static int sc2235_set_timings(struct sc2235_dev *sensor,
				const struct sc2235_mode_info *mode)
{
	int ret = 0;

	return ret;
}

static int sc2235_load_regs(struct sc2235_dev *sensor,
				const struct sc2235_mode_info *mode)
{
	const struct reg_value *regs = mode->reg_data;
	unsigned int i;
	u32 delay_ms;
	u16 reg_addr;
	u8 mask, val;
	int ret = 0;

	for (i = 0; i < mode->reg_data_size; ++i, ++regs) {
		delay_ms = regs->delay_ms;
		reg_addr = regs->reg_addr;
		val = regs->val;
		mask = regs->mask;

		if (mask)
			ret = sc2235_mod_reg(sensor, reg_addr, mask, val);
		else
			ret = sc2235_write_reg(sensor, reg_addr, val);
		if (ret)
			break;

		if (delay_ms)
			usleep_range(1000 * delay_ms, 1000 * delay_ms + 100);
	}

	return sc2235_set_timings(sensor, mode);
}

static int sc2235_set_autoexposure(struct sc2235_dev *sensor, bool on)
{
	return sc2235_mod_reg(sensor, SC2235_REG_AEC_PK_MANUAL,
				BIT(0), on ? 0 : BIT(0));
}

static int sc2235_get_exposure(struct sc2235_dev *sensor)
{
	int exp = 0, ret = 0;
	u8 temp;

	ret = sc2235_read_reg(sensor, SC2235_REG_AEC_PK_EXPOSURE_HI, &temp);
	if (ret)
		return ret;
	exp |= (int)temp << 8;
	ret = sc2235_read_reg(sensor, SC2235_REG_AEC_PK_EXPOSURE_LO, &temp);
	if (ret)
		return ret;
	exp |= (int)temp;

	return exp >> 4;
}

static int sc2235_set_exposure(struct sc2235_dev *sensor, u32 exposure)
{
	int ret;

	exposure <<= 4;

	ret = sc2235_write_reg(sensor,
				SC2235_REG_AEC_PK_EXPOSURE_LO,
				exposure & 0xff);
	if (ret)
		return ret;
	return sc2235_write_reg(sensor,
				SC2235_REG_AEC_PK_EXPOSURE_HI,
				(exposure >> 8) & 0xff);
}

static int sc2235_get_gain(struct sc2235_dev *sensor)
{
	u16 gain;
	int ret;

	ret = sc2235_read_reg16(sensor, SC2235_REG_AEC_PK_REAL_GAIN, &gain);
	if (ret)
		return ret;

	return gain & 0x1fff;
}

static int sc2235_set_gain(struct sc2235_dev *sensor, int gain)
{
	return sc2235_write_reg16(sensor, SC2235_REG_AEC_PK_REAL_GAIN,
					(u16)gain & 0x1fff);
}

static int sc2235_set_autogain(struct sc2235_dev *sensor, bool on)
{
	return sc2235_mod_reg(sensor, SC2235_REG_AEC_PK_MANUAL,
				BIT(1), on ? 0 : BIT(1));
}

#ifdef UNUSED_CODE
static int sc2235_get_sysclk(struct sc2235_dev *sensor)
{
	return 0;
}

static int sc2235_set_night_mode(struct sc2235_dev *sensor)
{
	return 0;
}

static int sc2235_get_hts(struct sc2235_dev *sensor)
{
	u16 hts;
	int ret;

	ret = sc2235_read_reg16(sensor, SC2235_REG_TIMING_HTS, &hts);
	if (ret)
		return ret;
	return hts;
}
#endif

static int sc2235_get_vts(struct sc2235_dev *sensor)
{
	u16 vts;
	int ret;

	ret = sc2235_read_reg16(sensor, SC2235_REG_TIMING_VTS, &vts);
	if (ret)
		return ret;
	return vts;
}

#ifdef UNUSED_CODE
static int sc2235_set_vts(struct sc2235_dev *sensor, int vts)
{
	return sc2235_write_reg16(sensor, SC2235_REG_TIMING_VTS, vts);
}

static int sc2235_get_light_freq(struct sc2235_dev *sensor)
{
	return 0;
}

static int sc2235_set_bandingfilter(struct sc2235_dev *sensor)
{
	return 0;
}

static int sc2235_set_ae_target(struct sc2235_dev *sensor, int target)
{
	return 0;
}

static int sc2235_get_binning(struct sc2235_dev *sensor)
{
	return 0;
}

static int sc2235_set_binning(struct sc2235_dev *sensor, bool enable)
{
	return 0;
}

#endif

static const struct sc2235_mode_info *
sc2235_find_mode(struct sc2235_dev *sensor, enum sc2235_frame_rate fr,
		 int width, int height, bool nearest)
{
	const struct sc2235_mode_info *mode;

	mode = v4l2_find_nearest_size(sc2235_mode_data,
					ARRAY_SIZE(sc2235_mode_data),
					hact, vact,
					width, height);

	if (!mode ||
		(!nearest && (mode->hact != width || mode->vact != height)))
		return NULL;

	/* Check to see if the current mode exceeds the max frame rate */
	if (sc2235_framerates[fr] > sc2235_framerates[mode->max_fps])
		return NULL;

	return mode;
}

static u64 sc2235_calc_pixel_rate(struct sc2235_dev *sensor)
{
	u64 rate;

	rate = sensor->current_mode->vtot * sensor->current_mode->htot;
	rate *= sc2235_framerates[sensor->current_fr];

	return rate;
}

#ifdef UNUSED_CODE
/*
 * sc2235_set_dvp_pclk() - Calculate the clock tree configuration values
 *				for the dvp output.
 *
 * @rate: The requested bandwidth per lane in bytes per second.
 *	'Bandwidth Per Lane' is calculated as:
 *	rate = HTOT * VTOT * FPS;
 *
 * This function use the requested bandwidth to calculate:
 * - rate = xclk * (64 - M) / (N * (S + 1));
 *
 */

#define PLL_PREDIV  1
#define PLL_SYSEL   0

static int sc2235_set_dvp_pclk(struct sc2235_dev *sensor,
				unsigned long rate)
{
	u8 prediv, mult, sysdiv;
	int ret = 0;

	sc2235_calc_sys_clk(sensor, rate, &prediv, &mult,
				&sysdiv);


	return ret;
}

/*
 * if sensor changes inside scaling or subsampling
 * change mode directly
 */
static int sc2235_set_mode_direct(struct sc2235_dev *sensor,
				const struct sc2235_mode_info *mode)
{
	if (!mode->reg_data)
		return -EINVAL;

	/* Write capture setting */
	return sc2235_load_regs(sensor, mode);
}
#endif

static int sc2235_set_mode(struct sc2235_dev *sensor)
{
#ifdef UNUSED_CODE
	bool auto_exp =  sensor->ctrls.auto_exp->val == V4L2_EXPOSURE_AUTO;
	const struct sc2235_mode_info *mode = sensor->current_mode;
#endif
	bool auto_gain = sensor->ctrls.auto_gain->val == 1;
	int ret = 0;

	/* auto gain and exposure must be turned off when changing modes */
	if (auto_gain) {
		ret = sc2235_set_autogain(sensor, false);
		if (ret)
			return ret;
	}
#ifdef UNUSED_CODE
	/* This issue will be addressed in the EVB board*/
	/* This action will result in poor image display 2021 1111*/
	if (auto_exp) {
		ret = sc2235_set_autoexposure(sensor, false);
		if (ret)
			goto restore_auto_gain;
	}

	rate = sc2235_calc_pixel_rate(sensor);

	ret = sc2235_set_dvp_pclk(sensor, rate);
	if (ret < 0)
		return 0;

	ret = sc2235_set_mode_direct(sensor, mode);
	if (ret < 0)
		goto restore_auto_exp_gain;

	/* restore auto gain and exposure */
	if (auto_gain)
		sc2235_set_autogain(sensor, true);
	if (auto_exp)
		sc2235_set_autoexposure(sensor, true);


	sensor->pending_mode_change = false;
	sensor->last_mode = mode;
	return 0;

restore_auto_exp_gain:
	if (auto_exp)
		sc2235_set_autoexposure(sensor, true);
restore_auto_gain:
	if (auto_gain)
		sc2235_set_autogain(sensor, true);
#endif
	return ret;
}

static int sc2235_set_framefmt(struct sc2235_dev *sensor,
				struct v4l2_mbus_framefmt *format);

/* restore the last set video mode after chip power-on */
static int sc2235_restore_mode(struct sc2235_dev *sensor)
{
	int ret;

	/* first load the initial register values */
	ret = sc2235_load_regs(sensor, &sc2235_mode_init_data);
	if (ret < 0)
		return ret;
	sensor->last_mode = &sc2235_mode_init_data;
	/* now restore the last capture mode */
	ret = sc2235_set_mode(sensor);
	if (ret < 0)
		return ret;

	return sc2235_set_framefmt(sensor, &sensor->fmt);
}

static void sc2235_power(struct sc2235_dev *sensor, bool enable)
{
	if (!sensor->pwdn_gpio)
		return;
	gpiod_set_value_cansleep(sensor->pwdn_gpio, enable ? 0 : 1);
}

static void sc2235_reset(struct sc2235_dev *sensor)
{
	if (!sensor->reset_gpio)
		return;

	gpiod_set_value_cansleep(sensor->reset_gpio, 0);

	/* camera power cycle */
	sc2235_power(sensor, false);
	usleep_range(5000, 10000);
	sc2235_power(sensor, true);
	usleep_range(5000, 10000);

	gpiod_set_value_cansleep(sensor->reset_gpio, 1);
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(sensor->reset_gpio, 0);
	usleep_range(20000, 25000);
}

static int sc2235_set_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc2235_dev *sensor = to_sc2235_dev(sd);
	int ret;

	ret = clk_prepare_enable(sensor->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		return ret;
	}

	ret = regulator_bulk_enable(SC2235_NUM_SUPPLIES,
					sensor->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		goto xclk_off;
	}

	sc2235_reset(sensor);
	sc2235_power(sensor, true);

	return 0;

xclk_off:
	clk_disable_unprepare(sensor->xclk);
	return ret;
}

static int sc2235_set_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc2235_dev *sensor = to_sc2235_dev(sd);

	sc2235_power(sensor, false);
	regulator_bulk_disable(SC2235_NUM_SUPPLIES, sensor->supplies);
	clk_disable_unprepare(sensor->xclk);

	return 0;
}

static int sc2235_set_power(struct sc2235_dev *sensor, bool on)
{
	int ret = 0;

	if (on) {
		pm_runtime_get_sync(&sensor->i2c_client->dev);

		ret = sc2235_restore_mode(sensor);
		if (ret)
			goto power_off;
	}

	if (!on)
		pm_runtime_put_sync(&sensor->i2c_client->dev);

	return 0;

power_off:
	pm_runtime_put_sync(&sensor->i2c_client->dev);

	return ret;
}

static int sc2235_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc2235_dev *sensor = to_sc2235_dev(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);

	ret = sc2235_set_power(sensor, !!on);
	if (ret)
		goto out;

	mutex_unlock(&sensor->lock);
	return 0;

out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int sc2235_try_frame_interval(struct sc2235_dev *sensor,
				struct v4l2_fract *fi,
				u32 width, u32 height)
{
	const struct sc2235_mode_info *mode;
	enum sc2235_frame_rate rate = SC2235_15_FPS;
	int minfps, maxfps, best_fps, fps;
	int i;

	minfps = sc2235_framerates[SC2235_15_FPS];
	maxfps = sc2235_framerates[SC2235_30_FPS];

	if (fi->numerator == 0) {
		fi->denominator = maxfps;
		fi->numerator = 1;
		rate = SC2235_30_FPS;
		goto find_mode;
	}

	fps = clamp_val(DIV_ROUND_CLOSEST(fi->denominator, fi->numerator),
			minfps, maxfps);

	best_fps = minfps;
	for (i = 0; i < ARRAY_SIZE(sc2235_framerates); i++) {
		int curr_fps = sc2235_framerates[i];

		if (abs(curr_fps - fps) < abs(best_fps - fps)) {
			best_fps = curr_fps;
			rate = i;
		}
	}

	fi->numerator = 1;
	fi->denominator = best_fps;

find_mode:
	mode = sc2235_find_mode(sensor, rate, width, height, false);
	return mode ? rate : -EINVAL;
}

static int sc2235_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *format)
{
	struct sc2235_dev *sensor = to_sc2235_dev(sd);
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

static int sc2235_try_fmt_internal(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt,
				enum sc2235_frame_rate fr,
				const struct sc2235_mode_info **new_mode)
{
	struct sc2235_dev *sensor = to_sc2235_dev(sd);
	const struct sc2235_mode_info *mode;
	int i;

	mode = sc2235_find_mode(sensor, fr, fmt->width, fmt->height, true);
	if (!mode)
		return -EINVAL;
	fmt->width = mode->hact;
	fmt->height = mode->vact;

	if (new_mode)
		*new_mode = mode;

	for (i = 0; i < ARRAY_SIZE(sc2235_formats); i++)
		if (sc2235_formats[i].code == fmt->code)
			break;
	if (i >= ARRAY_SIZE(sc2235_formats))
		i = 0;

	fmt->code = sc2235_formats[i].code;
	fmt->colorspace = sc2235_formats[i].colorspace;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);

	return 0;
}

static int sc2235_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *format)
{
	struct sc2235_dev *sensor = to_sc2235_dev(sd);
	const struct sc2235_mode_info *new_mode;
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

	ret = sc2235_try_fmt_internal(sd, mbus_fmt, 0, &new_mode);
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
			sc2235_framerates[sensor->current_fr];
		sensor->current_mode = new_mode;
		sensor->pending_mode_change = true;
	}

	__v4l2_ctrl_s_ctrl_int64(sensor->ctrls.pixel_rate,
				sc2235_calc_pixel_rate(sensor));
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int sc2235_set_framefmt(struct sc2235_dev *sensor,
				struct v4l2_mbus_framefmt *format)
{
	int ret = 0;

	switch (format->code) {
	default:
		return ret;
	}
	return ret;
}

/*
 * Sensor Controls.
 */

static int sc2235_set_ctrl_hue(struct sc2235_dev *sensor, int value)
{
	int ret = 0;
	return ret;
}

static int sc2235_set_ctrl_contrast(struct sc2235_dev *sensor, int value)
{
	int ret = 0;
	return ret;
}

static int sc2235_set_ctrl_saturation(struct sc2235_dev *sensor, int value)
{
	int ret  = 0;
	return ret;
}

static int sc2235_set_ctrl_white_balance(struct sc2235_dev *sensor, int awb)
{
	int ret = 0;
	return ret;
}

static int sc2235_set_ctrl_exposure(struct sc2235_dev *sensor,
				enum v4l2_exposure_auto_type auto_exposure)
{
	struct sc2235_ctrls *ctrls = &sensor->ctrls;
	bool auto_exp = (auto_exposure == V4L2_EXPOSURE_AUTO);
	int ret = 0;

	if (ctrls->auto_exp->is_new) {
		ret = sc2235_set_autoexposure(sensor, auto_exp);
		if (ret)
			return ret;
	}

	if (!auto_exp && ctrls->exposure->is_new) {
		u16 max_exp = 0;

		ret = sc2235_get_vts(sensor);
		if (ret < 0)
			return ret;
		max_exp += ret - 4;
		ret = 0;

		if (ctrls->exposure->val < max_exp)
			ret = sc2235_set_exposure(sensor, ctrls->exposure->val);
	}

	return ret;
}

static int sc2235_set_ctrl_gain(struct sc2235_dev *sensor, bool auto_gain)
{
	struct sc2235_ctrls *ctrls = &sensor->ctrls;
	int ret = 0;

	if (ctrls->auto_gain->is_new) {
		ret = sc2235_set_autogain(sensor, auto_gain);
		if (ret)
			return ret;
	}

	if (!auto_gain && ctrls->gain->is_new)
		ret = sc2235_set_gain(sensor, ctrls->gain->val);

	return ret;
}

static const char * const test_pattern_menu[] = {
	"Disabled",
	"Black bars",
	"Auto Black bars",
};

#define SC2235_TEST_ENABLE		BIT(3)
#define SC2235_TEST_BLACK		(3 << 0)

static int sc2235_set_ctrl_test_pattern(struct sc2235_dev *sensor, int value)
{
	int ret = 0;
	/*
	 *For 7110 platform, refer to 1125 FW code configuration. This operation will cause the image to be white.
	 */
#ifdef UNUSED_CODE
	ret = sc2235_mod_reg(sensor, SC2235_REG_TEST_SET0, BIT(3),
				!!value << 3);

	ret |= sc2235_mod_reg(sensor, SC2235_REG_TEST_SET1, BIT(6),
				(value >> 1) << 6);
#endif
	return ret;
}

static int sc2235_set_ctrl_light_freq(struct sc2235_dev *sensor, int value)
{
	return 0;
}

static int sc2235_set_ctrl_hflip(struct sc2235_dev *sensor, int value)
{
	return sc2235_mod_reg(sensor, SC2235_REG_TIMING_TC_REG21,
				BIT(2) | BIT(1),
				(!(value ^ sensor->upside_down)) ?
				(BIT(2) | BIT(1)) : 0);
}

static int sc2235_set_ctrl_vflip(struct sc2235_dev *sensor, int value)
{
	return sc2235_mod_reg(sensor, SC2235_REG_TIMING_TC_REG21,
				BIT(6) | BIT(5),
				(value ^ sensor->upside_down) ?
				(BIT(6) | BIT(5)) : 0);
}

static int sc2235_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct sc2235_dev *sensor = to_sc2235_dev(sd);
	int val;

	/* v4l2_ctrl_lock() locks our own mutex */

	if (!pm_runtime_get_if_in_use(&sensor->i2c_client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		val = sc2235_get_gain(sensor);
		if (val < 0)
			return val;
		sensor->ctrls.gain->val = val;
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		val = sc2235_get_exposure(sensor);
		if (val < 0)
			return val;
		sensor->ctrls.exposure->val = val;
		break;
	}

	pm_runtime_put(&sensor->i2c_client->dev);

	return 0;
}

static int sc2235_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct sc2235_dev *sensor = to_sc2235_dev(sd);
	int ret;

	/* v4l2_ctrl_lock() locks our own mutex */

	/*
	 * If the device is not powered up by the host driver do
	 * not apply any controls to H/W at this time. Instead
	 * the controls will be restored at start streaming time.
	 */
	if (!pm_runtime_get_if_in_use(&sensor->i2c_client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		ret = sc2235_set_ctrl_gain(sensor, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ret = sc2235_set_ctrl_exposure(sensor, ctrl->val);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret = sc2235_set_ctrl_white_balance(sensor, ctrl->val);
		break;
	case V4L2_CID_HUE:
		ret = sc2235_set_ctrl_hue(sensor, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		ret = sc2235_set_ctrl_contrast(sensor, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		ret = sc2235_set_ctrl_saturation(sensor, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc2235_set_ctrl_test_pattern(sensor, ctrl->val);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		ret = sc2235_set_ctrl_light_freq(sensor, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = sc2235_set_ctrl_hflip(sensor, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ret = sc2235_set_ctrl_vflip(sensor, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&sensor->i2c_client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc2235_ctrl_ops = {
	.g_volatile_ctrl = sc2235_g_volatile_ctrl,
	.s_ctrl = sc2235_s_ctrl,
};

static int sc2235_init_controls(struct sc2235_dev *sensor)
{
	const struct v4l2_ctrl_ops *ops = &sc2235_ctrl_ops;
	struct sc2235_ctrls *ctrls = &sensor->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret;

	v4l2_ctrl_handler_init(hdl, 32);

	/* we can use our own mutex for the ctrl lock */
	hdl->lock = &sensor->lock;

	/* Clock related controls */
	ctrls->pixel_rate = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_PIXEL_RATE,
						0, INT_MAX, 1,
						sc2235_calc_pixel_rate(sensor));

	/* Auto/manual white balance */
	ctrls->auto_wb = v4l2_ctrl_new_std(hdl, ops,
					V4L2_CID_AUTO_WHITE_BALANCE,
					0, 1, 1, 1);
	ctrls->blue_balance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BLUE_BALANCE,
						0, 4095, 1, 0);
	ctrls->red_balance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_RED_BALANCE,
						0, 4095, 1, 0);
	/* Auto/manual exposure */
#ifdef UNUSED_CODE
	/*
	 *For 7110 platform, This operation will cause the image to be white.
	 */
	ctrls->auto_exp = v4l2_ctrl_new_std_menu(hdl, ops,
						V4L2_CID_EXPOSURE_AUTO,
						V4L2_EXPOSURE_MANUAL, 0,
						V4L2_EXPOSURE_AUTO);
	ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE,
					0, 65535, 1, 0);
	/* Auto/manual gain */
	ctrls->auto_gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_AUTOGAIN,
						0, 1, 1, 1);
	ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN,
					0, 1023, 1, 0);
#else
	ctrls->auto_exp = v4l2_ctrl_new_std_menu(hdl, ops,
						V4L2_CID_EXPOSURE_AUTO,
						V4L2_EXPOSURE_MANUAL, 0,
						1);
	ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE,
					0, 65535, 1, 720);
	/* Auto/manual gain */
	ctrls->auto_gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_AUTOGAIN,
						0, 1, 1, 0);
	ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN,
					0, 1023, 1, 0x10);
#endif
	ctrls->saturation = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SATURATION,
						0, 255, 1, 64);
	ctrls->hue = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HUE,
					0, 359, 1, 0);
	ctrls->contrast = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_CONTRAST,
						0, 255, 1, 0);
	ctrls->test_pattern =
		v4l2_ctrl_new_std_menu_items(hdl, ops, V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(test_pattern_menu) - 1,
					0, 0, test_pattern_menu);   //0x02
	ctrls->hflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HFLIP,
					0, 1, 1, 1);
	ctrls->vflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VFLIP,
					0, 1, 1, 0);

	ctrls->light_freq =
		v4l2_ctrl_new_std_menu(hdl, ops,
					V4L2_CID_POWER_LINE_FREQUENCY,
					V4L2_CID_POWER_LINE_FREQUENCY_AUTO, 0,
					V4L2_CID_POWER_LINE_FREQUENCY_50HZ);

	if (hdl->error) {
		ret = hdl->error;
		goto free_ctrls;
	}

	ctrls->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	ctrls->gain->flags |= V4L2_CTRL_FLAG_VOLATILE;
	ctrls->exposure->flags |= V4L2_CTRL_FLAG_VOLATILE;

	v4l2_ctrl_auto_cluster(3, &ctrls->auto_wb, 0, false);
	v4l2_ctrl_auto_cluster(2, &ctrls->auto_gain, 0, true);
	v4l2_ctrl_auto_cluster(2, &ctrls->auto_exp, 1, true);

	sensor->sd.ctrl_handler = hdl;
	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(hdl);
	return ret;
}

static int sc2235_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->pad != 0)
		return -EINVAL;
	if (fse->index >= SC2235_NUM_MODES)
		return -EINVAL;

	fse->min_width =
		sc2235_mode_data[fse->index].hact;
	fse->max_width = fse->min_width;
	fse->min_height =
		sc2235_mode_data[fse->index].vact;
	fse->max_height = fse->min_height;

	return 0;
}

static int sc2235_enum_frame_interval(
	struct v4l2_subdev *sd,
	struct v4l2_subdev_state *state,
	struct v4l2_subdev_frame_interval_enum *fie)
{
	struct v4l2_fract tpf;
	int i;

	if (fie->pad != 0)
		return -EINVAL;

	if (fie->index >= SC2235_NUM_FRAMERATES)
		return -EINVAL;

	tpf.numerator = 1;
	tpf.denominator = sc2235_framerates[fie->index];

	for (i = 0; i < SC2235_NUM_MODES; i++) {
		if (fie->width == sc2235_mode_data[i].hact &&
			fie->height == sc2235_mode_data[i].vact)
			break;
	}
	if (i == SC2235_NUM_MODES)
		return -ENOTTY;

	fie->interval = tpf;
	return 0;
}

static int sc2235_g_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *fi)
{
	struct sc2235_dev *sensor = to_sc2235_dev(sd);

	mutex_lock(&sensor->lock);
	fi->interval = sensor->frame_interval;
	mutex_unlock(&sensor->lock);

	return 0;
}

static int sc2235_s_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *fi)
{
	struct sc2235_dev *sensor = to_sc2235_dev(sd);
	const struct sc2235_mode_info *mode;
	int frame_rate, ret = 0;

	if (fi->pad != 0)
		return -EINVAL;

	mutex_lock(&sensor->lock);

	if (sensor->streaming) {
		ret = -EBUSY;
		goto out;
	}

	mode = sensor->current_mode;

	frame_rate = sc2235_try_frame_interval(sensor, &fi->interval,
						mode->hact, mode->vact);
	if (frame_rate < 0) {
		/* Always return a valid frame interval value */
		fi->interval = sensor->frame_interval;
		goto out;
	}

	mode = sc2235_find_mode(sensor, frame_rate, mode->hact,
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
					sc2235_calc_pixel_rate(sensor));
	}
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int sc2235_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad != 0)
		return -EINVAL;
	if (code->index >= ARRAY_SIZE(sc2235_formats))
		return -EINVAL;

	code->code = sc2235_formats[code->index].code;
	return 0;
}

static int sc2235_stream_start(struct sc2235_dev *sensor, int enable)
{
	return sc2235_mod_reg(sensor, SC2235_REG_STREAM_ON, BIT(0), !!enable);
}

static int sc2235_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sc2235_dev *sensor = to_sc2235_dev(sd);
	int ret = 0;

	if (enable) {
		ret = v4l2_ctrl_handler_setup(&sensor->ctrls.handler);
		if (ret)
			return ret;
	}

	mutex_lock(&sensor->lock);

	if (sensor->streaming == !enable) {
		if (enable && sensor->pending_mode_change) {
			ret = sc2235_set_mode(sensor);
			if (ret)
				goto out;
		}

		if (enable && sensor->pending_fmt_change) {
			ret = sc2235_set_framefmt(sensor, &sensor->fmt);
			if (ret)
				goto out;
			sensor->pending_fmt_change = false;
		}

		ret = sc2235_stream_start(sensor, enable);
		if (ret)
			goto out;
	}
	sensor->streaming += enable ? 1 : -1;
	WARN_ON(sensor->streaming < 0);
out:
	mutex_unlock(&sensor->lock);

	return ret;
}

static const struct v4l2_subdev_core_ops sc2235_core_ops = {
	.s_power = sc2235_s_power,
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops sc2235_video_ops = {
	.g_frame_interval = sc2235_g_frame_interval,
	.s_frame_interval = sc2235_s_frame_interval,
	.s_stream = sc2235_s_stream,
};

static const struct v4l2_subdev_pad_ops sc2235_pad_ops = {
	.enum_mbus_code = sc2235_enum_mbus_code,
	.get_fmt = sc2235_get_fmt,
	.set_fmt = sc2235_set_fmt,
	.enum_frame_size = sc2235_enum_frame_size,
	.enum_frame_interval = sc2235_enum_frame_interval,
};

static const struct v4l2_subdev_ops sc2235_subdev_ops = {
	.core = &sc2235_core_ops,
	.video = &sc2235_video_ops,
	.pad = &sc2235_pad_ops,
};

static int sc2235_get_regulators(struct sc2235_dev *sensor)
{
	int i;

	for (i = 0; i < SC2235_NUM_SUPPLIES; i++)
		sensor->supplies[i].supply = sc2235_supply_name[i];

	return devm_regulator_bulk_get(&sensor->i2c_client->dev,
					SC2235_NUM_SUPPLIES,
					sensor->supplies);
}

static int sc2235_check_chip_id(struct sc2235_dev *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	int ret = 0;
	u16 chip_id;

	ret = sc2235_read_reg16(sensor, SC2235_REG_CHIP_ID, &chip_id);
	if (ret) {
		dev_err(&client->dev, "%s: failed to read chip identifier\n",
			__func__);
		return ret;
	}

	if (chip_id != SC2235_CHIP_ID) {
		dev_err(&client->dev, "%s: wrong chip identifier, expected 0x%x, got 0x%x\n",
			__func__, SC2235_CHIP_ID, chip_id);
		return -ENXIO;
	}
	dev_err(&client->dev, "%s: chip identifier, got 0x%x\n",
		__func__, chip_id);

	return 0;
}

static int sc2235_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	struct sc2235_dev *sensor;
	struct v4l2_mbus_framefmt *fmt;
	u32 rotation;
	int ret;

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
	sensor->frame_interval.denominator = sc2235_framerates[SC2235_30_FPS];
	sensor->current_fr = SC2235_30_FPS;
	sensor->current_mode =
		&sc2235_mode_data[SC2235_MODE_1080P_1920_1080];
	sensor->last_mode = sensor->current_mode;

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

	if (sensor->ep.bus_type != V4L2_MBUS_PARALLEL) {
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
	if (sensor->xclk_freq < SC2235_XCLK_MIN ||
	    sensor->xclk_freq > SC2235_XCLK_MAX) {
		dev_err(dev, "xclk frequency out of range: %d Hz\n",
			sensor->xclk_freq);
		return -EINVAL;
	}

	sensor->pwdn_gpio = devm_gpiod_get_optional(dev, "powerdown",
						GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->pwdn_gpio))
		return PTR_ERR(sensor->pwdn_gpio);

	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset_gpio))
		return PTR_ERR(sensor->reset_gpio);

	v4l2_i2c_subdev_init(&sensor->sd, client, &sc2235_subdev_ops);

	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	ret = sc2235_get_regulators(sensor);
	if (ret)
		return ret;
	mutex_init(&sensor->lock);

	ret = sc2235_set_power_on(dev);
	if (ret) {
		dev_err(dev, "failed to power on\n");
		goto entity_cleanup;
	}

	ret = sc2235_check_chip_id(sensor);
	if (ret)
		goto entity_power_off;

	ret = sc2235_init_controls(sensor);
	if (ret)
		goto entity_power_off;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto free_ctrls;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
entity_power_off:
	sc2235_set_power_off(dev);
entity_cleanup:
	media_entity_cleanup(&sensor->sd.entity);
	mutex_destroy(&sensor->lock);
	return ret;
}

static int sc2235_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc2235_dev *sensor = to_sc2235_dev(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
	mutex_destroy(&sensor->lock);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		sc2235_set_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct i2c_device_id sc2235_id[] = {
	{ "sc2235", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sc2235_id);

static const struct of_device_id sc2235_dt_ids[] = {
	{ .compatible = "smartsens,sc2235" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sc2235_dt_ids);

static const struct dev_pm_ops sc2235_pm_ops = {
	SET_RUNTIME_PM_OPS(sc2235_set_power_off, sc2235_set_power_on, NULL)
};

static struct i2c_driver sc2235_i2c_driver = {
	.driver = {
		.name  = "sc2235",
		.of_match_table	= sc2235_dt_ids,
		.pm = &sc2235_pm_ops,
	},
	.id_table = sc2235_id,
	.probe_new = sc2235_probe,
	.remove   = sc2235_remove,
};

module_i2c_driver(sc2235_i2c_driver);

MODULE_DESCRIPTION("SC2235 Camera Subdev Driver");
MODULE_LICENSE("GPL");
