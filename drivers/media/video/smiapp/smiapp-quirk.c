/*
 * drivers/media/video/smiapp/smiapp-quirk.c
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@maxwell.research.nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "smiapp-debug.h"

#include <linux/delay.h>

#include "smiapp.h"

static int smiapp_write_8(struct smiapp_sensor *sensor, u16 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);

	return smiapp_write(client, (SMIA_REG_8BIT << 16) | reg, val);
}

static int smiapp_write_8s(struct smiapp_sensor *sensor,
			   struct smiapp_reg_8 *regs, int len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;

	for (; len > 0; len--, regs++) {
		rval = smiapp_write_8(sensor, regs->reg, regs->val);
		if (rval < 0) {
			dev_err(&client->dev,
				"error %d writing reg 0x%4.4x, val 0x%2.2x",
				rval, regs->reg, regs->val);
			return rval;
		}
	}

	return 0;
}

void smiapp_replace_limit(struct smiapp_sensor *sensor,
			  u32 limit, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);

	dev_dbg(&client->dev, "quirk: 0x%8.8x \"%s\" = %d, 0x%x\n",
		smiapp_reg_limits[limit].addr,
		smiapp_reg_limits[limit].what, val, val);
	sensor->limits[limit] = val;
}

int smiapp_replace_limit_at(struct smiapp_sensor *sensor,
			    u32 reg, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int i;

	for (i = 0; smiapp_reg_limits[i].addr; i++) {
		if ((smiapp_reg_limits[i].addr & 0xffff) != reg)
			continue;

		smiapp_replace_limit(sensor, i, val);

		return 0;
	}

	dev_dbg(&client->dev, "quirk: bad register 0x%4.4x\n", reg);

	return -EINVAL;
}

static int jt8ew9_limits(struct smiapp_sensor *sensor)
{
	if (sensor->minfo.revision_number_major < 0x03)
		sensor->frame_skip = 1;

	/* Below 24 gain doesn't have effect at all, */
	/* but ~59 is needed for full dynamic range */
	smiapp_replace_limit(sensor, SMIAPP_LIMIT_ANALOGUE_GAIN_CODE_MIN, 59);
	smiapp_replace_limit(
		sensor, SMIAPP_LIMIT_ANALOGUE_GAIN_CODE_MAX, 6000);

	return 0;
}

static int jt8ew9_post_poweron(struct smiapp_sensor *sensor)
{
	struct smiapp_reg_8 regs[] = {
		{ 0x30a3, 0xd8 }, /* Output port control : LVDS ports only */
		{ 0x30ae, 0x00 }, /* 0x0307 pll_multiplier maximum value on PLL input 9.6MHz ( 19.2MHz is divided on pre_pll_div) */
		{ 0x30af, 0xd0 }, /* 0x0307 pll_multiplier maximum value on PLL input 9.6MHz ( 19.2MHz is divided on pre_pll_div) */
		{ 0x322d, 0x04 }, /* Adjusting Processing Image Size to Scaler Toshiba Recommendation Setting */
		{ 0x3255, 0x0f }, /* Horizontal Noise Reduction Control Toshiba Recommendation Setting */
		{ 0x3256, 0x15 }, /* Horizontal Noise Reduction Control Toshiba Recommendation Setting */
		{ 0x3258, 0x70 }, /* Analog Gain Control Toshiba Recommendation Setting */
		{ 0x3259, 0x70 }, /* Analog Gain Control Toshiba Recommendation Setting */
		{ 0x325f, 0x7c }, /* Analog Gain Control Toshiba Recommendation Setting */
		{ 0x3302, 0x06 }, /* Pixel Reference Voltage Control Toshiba Recommendation Setting */
		{ 0x3304, 0x00 }, /* Pixel Reference Voltage Control Toshiba Recommendation Setting */
		{ 0x3307, 0x22 }, /* Pixel Reference Voltage Control Toshiba Recommendation Setting */
		{ 0x3308, 0x8d }, /* Pixel Reference Voltage Control Toshiba Recommendation Setting */
		{ 0x331e, 0x0f }, /* Black Hole Sun Correction Control Toshiba Recommendation Setting */
		{ 0x3320, 0x30 }, /* Black Hole Sun Correction Control Toshiba Recommendation Setting */
		{ 0x3321, 0x11 }, /* Black Hole Sun Correction Control Toshiba Recommendation Setting */
		{ 0x3322, 0x98 }, /* Black Hole Sun Correction Control Toshiba Recommendation Setting */
		{ 0x3323, 0x64 }, /* Black Hole Sun Correction Control Toshiba Recommendation Setting */
		{ 0x3325, 0x83 }, /* Read Out Timing Control Toshiba Recommendation Setting */
		{ 0x3330, 0x18 }, /* Read Out Timing Control Toshiba Recommendation Setting */
		{ 0x333c, 0x01 }, /* Read Out Timing Control Toshiba Recommendation Setting */
		{ 0x3345, 0x2f }, /* Black Hole Sun Correction Control Toshiba Recommendation Setting */
		{ 0x33de, 0x38 }, /* Horizontal Noise Reduction Control Toshiba Recommendation Setting */
		/* Taken from v03. No idea what the rest are. */
		{ 0x32e0, 0x05 },
		{ 0x32e1, 0x05 },
		{ 0x32e2, 0x04 },
		{ 0x32e5, 0x04 },
		{ 0x32e6, 0x04 },

	};

	return smiapp_write_8s(sensor, regs, ARRAY_SIZE(regs));
}

const struct smiapp_quirk smiapp_jt8ew9_quirk = {
	.limits = jt8ew9_limits,
	.post_poweron = jt8ew9_post_poweron,
};

static int imx125es_post_poweron(struct smiapp_sensor *sensor)
{
	/* Taken from v02. No idea what the other two are. */
	struct smiapp_reg_8 regs[] = {
		/*
		 * 0x3302: clk during frame blanking:
		 * 0x00 - HS mode, 0x01 - LP11
		 */
		{ 0x3302, 0x01 },
		{ 0x302d, 0x00 },
		{ 0x3b08, 0x8c },
	};

	return smiapp_write_8s(sensor, regs, ARRAY_SIZE(regs));
}

const struct smiapp_quirk smiapp_imx125es_quirk = {
	.post_poweron = imx125es_post_poweron,
};

static int jt8ev1_limits(struct smiapp_sensor *sensor)
{
	smiapp_replace_limit(sensor, SMIAPP_LIMIT_X_ADDR_MAX, 4271);
	smiapp_replace_limit(sensor,
			     SMIAPP_LIMIT_MIN_LINE_BLANKING_PCK_BIN, 184);

	return 0;
}

static int jt8ev1_post_poweron(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;

	struct smiapp_reg_8 regs[] = {
		{ 0x3031, 0xcd }, /* For digital binning (EQ_MONI) */
		{ 0x30a3, 0xd0 }, /* FLASH STROBE enable */
		{ 0x3237, 0x00 }, /* For control of pulse timing for ADC */
		{ 0x3238, 0x43 },
		{ 0x3301, 0x06 }, /* For analog bias for sensor */
		{ 0x3302, 0x06 },
		{ 0x3304, 0x00 },
		{ 0x3305, 0x88 },
		{ 0x332a, 0x14 },
		{ 0x332c, 0x6b },
		{ 0x3336, 0x01 },
		{ 0x333f, 0x1f },
		{ 0x3355, 0x00 },
		{ 0x3356, 0x20 },
		{ 0x33bf, 0x20 }, /* Adjust the FBC speed */
		{ 0x33c9, 0x20 },
		{ 0x33ce, 0x30 }, /* Adjust the parameter for logic function */
		{ 0x33cf, 0xec }, /* For Black sun */
		{ 0x3328, 0x80 }, /* Ugh. No idea what's this. */
	};

	struct smiapp_reg_8 regs_96[] = {
		{ 0x30ae, 0x00 }, /* For control of ADC clock */
		{ 0x30af, 0xd0 },
		{ 0x30b0, 0x01 },
	};

	rval = smiapp_write_8s(sensor, regs, ARRAY_SIZE(regs));
	if (rval < 0)
		return rval;

	switch (sensor->platform_data->ext_clk) {
	case 9600000:
		return smiapp_write_8s(sensor, regs_96,
				       ARRAY_SIZE(regs_96));
	default:
		dev_warn(&client->dev, "no MSRs for %d Hz ext_clk\n",
			 sensor->platform_data->ext_clk);
		return 0;
	}
}

static int jt8ev1_pre_streamon(struct smiapp_sensor *sensor)
{
	return smiapp_write_8(sensor, 0x3328, 0x00);
}

static int jt8ev1_post_streamoff(struct smiapp_sensor *sensor)
{
	int rval;

	/* Workaround: allows fast standby to work properly */
	rval = smiapp_write_8(sensor, 0x3205, 0x04);
	if (rval < 0)
		return rval;

	/* Wait for 1 ms + one line => 2 ms is likely enough */
	usleep_range(2000, 2000);

	/* Restore it */
	rval = smiapp_write_8(sensor, 0x3205, 0x00);
	if (rval < 0)
		return rval;

	return smiapp_write_8(sensor, 0x3328, 0x80);
}

const struct smiapp_quirk smiapp_jt8ev1_quirk = {
	.limits = jt8ev1_limits,
	.post_poweron = jt8ev1_post_poweron,
	.pre_streamon = jt8ev1_pre_streamon,
	.post_streamoff = jt8ev1_post_streamoff,
	.flags = SMIAPP_QUIRK_FLAG_OP_PIX_CLOCK_PER_LANE,
};

static int tcm8500md_limits(struct smiapp_sensor *sensor)
{
	smiapp_replace_limit(sensor, SMIAPP_LIMIT_MIN_PLL_IP_FREQ_HZ, 2700000);

	return 0;
}

const struct smiapp_quirk smiapp_tcm8500md_quirk = {
	.limits = tcm8500md_limits,
};
