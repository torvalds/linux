// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Quad GMSL Deserializer Test Pattern Driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 */
#include "maxim4c_api.h"

#define PATTERN_WIDTH		1920
#define PATTERN_HEIGHT		1080

/* pattern generator: 0 or 1 */
enum {
	PATTERN_GENERATOR_0 = 0,
	PATTERN_GENERATOR_1,
};

/* pattern mode: checkerboard or gradient */
enum {
	PATTERN_CHECKERBOARD = 0,
	PATTERN_GRADIENT,
};

/* pattern pclk: 25M or 75M 0r 150M or 375M */
enum {
	PATTERN_PCLK_25M = 0,
	PATTERN_PCLK_75M,
	PATTERN_PCLK_150M,
	PATTERN_PCLK_375M,
};

static const struct maxim4c_mode maxim4c_pattern_mode = {
	.width = PATTERN_WIDTH,
	.height = PATTERN_HEIGHT,
	.max_fps = {
		.numerator = 10000,
		.denominator = 300000,
	},
	.link_freq_idx = 15,
	.bus_fmt = MEDIA_BUS_FMT_RGB888_1X24,
	.bpp = 24,
	.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
};

/* VPG0 or VPG1 register */
#define VPGx_REG(x, reg)	((reg) + 0x30 * (x))

int maxim4c_pattern_enable(maxim4c_t *maxim4c, bool enable)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	struct maxim4c_pattern *pattern = &maxim4c->pattern;
	u32 vpgx;
	u32 pattern_mode;
	u8 reg_mask = 0, reg_val = 0;
	int ret = 0;

	dev_info(dev, "video pattern: enable = %d\n", enable);

	vpgx = pattern->pattern_generator;
	pattern_mode = pattern->pattern_mode;

	reg_mask = BIT(5) | BIT(4);
	if (pattern_mode == PATTERN_CHECKERBOARD) {
		/* Generate checkerboard pattern. */
		reg_val = enable ? BIT(4) : 0;
	} else {
		/* Generate gradient pattern. */
		reg_val = enable ? BIT(5) : 0;
	}
	ret = maxim4c_i2c_update_byte(client,
			VPGx_REG(vpgx, 0x1051), MAXIM4C_I2C_REG_ADDR_16BITS,
			reg_mask, reg_val);

	return ret;
}
EXPORT_SYMBOL(maxim4c_pattern_enable);

static int maxim4c_pattern_previnit(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	int ret = 0;

	// All links disable at beginning.
	ret = maxim4c_i2c_write_byte(client,
			0x0006, MAXIM4C_I2C_REG_ADDR_16BITS,
			0xF0);
	if (ret)
		return ret;

	// MIPI CSI output disable.
	ret = maxim4c_i2c_write_byte(client,
			0x040B, MAXIM4C_I2C_REG_ADDR_16BITS,
			0x00);
	if (ret)
		return ret;

	// MIPI TXPHY standby
	ret = maxim4c_i2c_update_byte(client,
			0x08A2, MAXIM4C_I2C_REG_ADDR_16BITS,
			0xF0, 0x00);
	if (ret)
		return ret;

	return 0;
}

static int maxim4c_pattern_hw_init(maxim4c_t *maxim4c)
{
	const u32 h_active = PATTERN_WIDTH;
	const u32 h_fp = 88;
	const u32 h_sw = 44;
	const u32 h_bp = 148;
	const u32 h_tot = h_active + h_fp + h_sw + h_bp;

	const u32 v_active = PATTERN_HEIGHT;
	const u32 v_fp = 4;
	const u32 v_sw = 5;
	const u32 v_bp = 36;
	const u32 v_tot = v_active + v_fp + v_sw + v_bp;

	struct i2c_client *client = maxim4c->client;
	struct maxim4c_pattern *pattern = &maxim4c->pattern;
	u32 vpgx;
	u32 pattern_mode;
	u32 pattern_pclk;
	u16 reg_addr = 0;
	u8 reg_mask = 0, reg_val = 0;
	int ret = 0, i = 0;

	vpgx = pattern->pattern_generator;
	pattern_mode = pattern->pattern_mode;
	pattern_pclk = pattern->pattern_pclk;

	// PATGEN_MODE = 0, Pattern generator disabled
	//	use video from the serializer input
	ret |= maxim4c_i2c_update_byte(client,
			VPGx_REG(vpgx, 0x1051), MAXIM4C_I2C_REG_ADDR_16BITS,
			BIT(5) | BIT(4), 0x00);

	/* Pattern PCLK:
	 *	0b00 - 25MHz
	 *	0b01 - 75MHz
	 *	0b1x - (PATGEN_CLK_SRC: 0 - 150MHz, 1 - 375MHz).
	 */
	pattern_pclk = (pattern_pclk & 0x03);
	ret |= maxim4c_i2c_write_byte(client,
			0x0009, MAXIM4C_I2C_REG_ADDR_16BITS,
			pattern_pclk);
	if (pattern_pclk >= PATTERN_PCLK_150M) {
		reg_mask = BIT(7);
		if (pattern_pclk == PATTERN_PCLK_375M)
			reg_val = BIT(7);
		else
			reg_val = 0;

		if (vpgx == PATTERN_GENERATOR_0) {
			for (i = 0; i < 4; i++) {
				reg_addr = 0x01DC + i * 0x20;
				ret |= maxim4c_i2c_update_byte(client,
						reg_addr, MAXIM4C_I2C_REG_ADDR_16BITS,
						reg_mask, reg_val);
			}
		} else {
			for (i = 0; i < 4; i++) {
				reg_addr = 0x025C + i * 0x20;
				ret |= maxim4c_i2c_update_byte(client,
						reg_addr, MAXIM4C_I2C_REG_ADDR_16BITS,
						reg_mask, reg_val);
			}
		}
	}

	/* Configure Video Timing Generator for 1920x1080 @ 30 fps. */
	// VS_DLY = 0
	ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x1052), MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_24BITS, 0x000000);
	// VS_HIGH = Vsw * Htot
	ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x1055), MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_24BITS, v_sw * h_tot);
	// VS_LOW = (Vactive + Vfp + Vbp) * Htot
	ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x1058), MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_24BITS, (v_active + v_fp + v_bp) * h_tot);
	// V2H = VS_DLY
	ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x105b), MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_24BITS, 0x000000);
	// HS_HIGH = Hsw
	ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x105e), MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_16BITS, h_sw);
	// HS_LOW = Hactive + Hfp + Hbp
	ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x1060), MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_16BITS, h_active + h_fp + h_bp);
	// HS_CNT = Vtot
	ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x1062), MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_16BITS, v_tot);
	// V2D = VS_DLY + Htot * (Vsw + Vbp) + (Hsw + Hbp)
	ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x1064),	MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_24BITS, h_tot * (v_sw + v_bp) + (h_sw + h_bp));
	// DE_HIGH = Hactive
	ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x1067),	MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_16BITS, h_active);
	// DE_LOW = Hfp + Hsw + Hbp
	ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x1069),	MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_16BITS, h_fp + h_sw + h_bp);
	// DE_CNT = Vactive
	ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x106b),	MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_16BITS, v_active);

	/* Generate VS, HS and DE in free-running mode, Invert HS and VS. */
	ret |= maxim4c_i2c_write_byte(client,
			VPGx_REG(vpgx, 0x1050), MAXIM4C_I2C_REG_ADDR_16BITS,
			0xfb);

	/* Configure Video Pattern Generator. */
	if (pattern_mode == PATTERN_CHECKERBOARD) {
		/* Set checkerboard pattern size. */
		ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x1074),	MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_24BITS, 0x3c3c3c);

		/* Set checkerboard pattern colors. */
		ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x106e), MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_24BITS, 0xfecc00);
		ret |= maxim4c_i2c_write_reg(client,
			VPGx_REG(vpgx, 0x1071), MAXIM4C_I2C_REG_ADDR_16BITS,
			MAXIM4C_I2C_REG_VALUE_24BITS, 0x006aa7);
	} else {
		/* Set gradient increment. */
		ret |= maxim4c_i2c_write_byte(client,
				VPGx_REG(vpgx, 0x106d), MAXIM4C_I2C_REG_ADDR_16BITS,
				0x10);
	}

	return ret;
}

int maxim4c_pattern_init(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	struct device_node *node = NULL;
	struct maxim4c_mode *supported_mode = NULL;
	struct maxim4c_pattern *pattern = NULL;
	int ret = 0;

	// maxim serdes local
	node = of_get_child_by_name(dev->of_node, "serdes-local-device");
	if (IS_ERR_OR_NULL(node))
		return -ENODEV;

	if (!of_device_is_available(node)) {
		dev_info(dev, "%pOF is disabled\n", node);

		of_node_put(node);
		return -ENODEV;
	}

	maxim4c_mipi_txphy_data_init(maxim4c);

	/* mipi txphy parse dt */
	ret = maxim4c_mipi_txphy_parse_dt(maxim4c, node);
	if (ret)
		return ret;

	ret = maxim4c_pattern_previnit(maxim4c);
	if (ret)
		return ret;

	ret = maxim4c_mipi_txphy_hw_init(maxim4c);
	if (ret)
		return ret;

	maxim4c->cfg_modes_num = 1;
	maxim4c->cur_mode = &maxim4c->supported_mode;
	supported_mode = &maxim4c->supported_mode;

	// init using def mode
	memcpy(supported_mode, &maxim4c_pattern_mode, sizeof(struct maxim4c_mode));

	// pattern generator and mode init
	pattern = &maxim4c->pattern;
	pattern->pattern_generator = PATTERN_GENERATOR_0;
	pattern->pattern_mode = PATTERN_CHECKERBOARD;
	pattern->pattern_pclk = PATTERN_PCLK_75M;

	switch (pattern->pattern_pclk) {
	case PATTERN_PCLK_25M:
		supported_mode->max_fps.denominator = 100000;
		break;
	case PATTERN_PCLK_75M:
		supported_mode->max_fps.denominator = 300000;
		break;
	case PATTERN_PCLK_150M:
		supported_mode->max_fps.denominator = 600000;
		if (supported_mode->link_freq_idx < 12)
			dev_info(dev, "link_freq_idx = %d is too low\n",
					supported_mode->link_freq_idx);
		break;
	case PATTERN_PCLK_375M:
		supported_mode->max_fps.denominator = 1500000;
		if (supported_mode->link_freq_idx < 22)
			dev_info(dev, "link_freq_idx = %d is too low\n",
					supported_mode->link_freq_idx);
		break;
	}

	dev_info(dev, "video pattern: generator = %d, mode = %d, pclk = %d\n",
		pattern->pattern_generator, pattern->pattern_mode, pattern->pattern_pclk);

	ret = maxim4c_pattern_hw_init(maxim4c);

	return ret;
}
EXPORT_SYMBOL(maxim4c_pattern_init);
