// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Dual GMSL Deserializer Test Pattern Driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 */
#include "maxim2c_api.h"

#define PATTERN_WIDTH		1920
#define PATTERN_HEIGHT		1080

/* pattern mode: checkerboard or gradient */
enum {
	PATTERN_CHECKERBOARD = 0,
	PATTERN_GRADIENT,
};

/* pattern pclk: 25M or 75M 0r 150M or 600M */
enum {
	PATTERN_PCLK_25M = 0,
	PATTERN_PCLK_75M,
	PATTERN_PCLK_150M,
	PATTERN_PCLK_600M,
};

static const struct maxim2c_mode maxim2c_pattern_mode = {
	.width = PATTERN_WIDTH,
	.height = PATTERN_HEIGHT,
	.max_fps = {
		.numerator = 10000,
		.denominator = 300000,
	},
	.link_freq_idx = 24,
	.bus_fmt = MEDIA_BUS_FMT_RGB888_1X24,
	.bpp = 24,
	.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
};

int maxim2c_pattern_enable(maxim2c_t *maxim2c, bool enable)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	struct maxim2c_pattern *pattern = &maxim2c->pattern;
	u32 pattern_mode;
	u8 reg_mask = 0, reg_val = 0;
	int ret = 0;

	dev_info(dev, "video pattern: enable = %d\n", enable);

	pattern_mode = pattern->pattern_mode;

	reg_mask = BIT(5) | BIT(4);
	if (pattern_mode == PATTERN_CHECKERBOARD) {
		/* Generate checkerboard pattern. */
		reg_val = enable ? BIT(4) : 0;
	} else {
		/* Generate gradient pattern. */
		reg_val = enable ? BIT(5) : 0;
	}
	ret = maxim2c_i2c_update_byte(client,
			0x0241, MAXIM2C_I2C_REG_ADDR_16BITS,
			reg_mask, reg_val);

	return ret;
}
EXPORT_SYMBOL(maxim2c_pattern_enable);

static int maxim2c_pattern_previnit(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	int ret = 0;

	// Disable data transmission through video pipe.
	ret = maxim2c_i2c_update_byte(client,
			0x0002, MAXIM2C_I2C_REG_ADDR_16BITS,
			0xF0, 0x00);
	if (ret)
		return ret;

	// video pipe disable.
	ret = maxim2c_i2c_write_byte(client,
			0x0160, MAXIM2C_I2C_REG_ADDR_16BITS,
			0x00);
	if (ret)
		return ret;

	// MIPI CSI output disable.
	ret = maxim2c_i2c_write_byte(client,
			0x0313, MAXIM2C_I2C_REG_ADDR_16BITS,
			0x00);
	if (ret)
		return ret;

	// MIPI TXPHY standby
	ret = maxim2c_i2c_update_byte(client,
			0x0332, MAXIM2C_I2C_REG_ADDR_16BITS,
			0xF0, 0x00);
	if (ret)
		return ret;

	return 0;
}

static int maxim2c_pattern_config(maxim2c_t *maxim2c)
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

	struct i2c_client *client = maxim2c->client;
	struct maxim2c_pattern *pattern = &maxim2c->pattern;
	u32 pattern_mode;
	u32 pattern_pclk;
	u16 reg_addr = 0;
	u8 reg_mask = 0, reg_val = 0;
	int ret = 0, i = 0;

	pattern_mode = pattern->pattern_mode;
	pattern_pclk = pattern->pattern_pclk;

	// PATGEN_MODE = 0, Pattern generator disabled
	//	use video from the serializer input
	ret |= maxim2c_i2c_update_byte(client,
			0x0241, MAXIM2C_I2C_REG_ADDR_16BITS,
			BIT(5) | BIT(4), 0x00);

	/* Pattern PCLK:
	 *	0b00 - 25MHz
	 *	0b01 - 75MHz
	 *	0b1x - (PATGEN_CLK_SRC: 0 - 150MHz, 1 - 600MHz).
	 */
	pattern_pclk = (pattern_pclk & 0x03);
	ret |= maxim2c_i2c_write_byte(client,
			0x0038, MAXIM2C_I2C_REG_ADDR_16BITS,
			pattern_pclk);
	if (pattern_pclk >= PATTERN_PCLK_150M) {
		reg_mask = BIT(7);
		if (pattern_pclk == PATTERN_PCLK_600M)
			reg_val = BIT(7);
		else
			reg_val = 0;

		for (i = 0; i < 2; i++) {
			reg_addr = 0x01FC + i * 0x20;
			ret |= maxim2c_i2c_update_byte(client,
					reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
					reg_mask, reg_val);
		}
	}

	/* Configure Video Timing Generator for 1920x1080 @ 30 fps. */
	// VS_DLY = 0
	ret |= maxim2c_i2c_write_reg(client,
			0x0242, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_24BITS, 0x000000);
	// VS_HIGH = Vsw * Htot
	ret |= maxim2c_i2c_write_reg(client,
			0x0245, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_24BITS, v_sw * h_tot);
	// VS_LOW = (Vactive + Vfp + Vbp) * Htot
	ret |= maxim2c_i2c_write_reg(client,
			0x0248, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_24BITS, (v_active + v_fp + v_bp) * h_tot);
	// V2H = VS_DLY
	ret |= maxim2c_i2c_write_reg(client,
			0x024B, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_24BITS, 0x000000);
	// HS_HIGH = Hsw
	ret |= maxim2c_i2c_write_reg(client,
			0x024E, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_16BITS, h_sw);
	// HS_LOW = Hactive + Hfp + Hbp
	ret |= maxim2c_i2c_write_reg(client,
			0x0250, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_16BITS, h_active + h_fp + h_bp);
	// HS_CNT = Vtot
	ret |= maxim2c_i2c_write_reg(client,
			0x0252, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_16BITS, v_tot);
	// V2D = VS_DLY + Htot * (Vsw + Vbp) + (Hsw + Hbp)
	ret |= maxim2c_i2c_write_reg(client,
			0x0254, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_24BITS, h_tot * (v_sw + v_bp) + (h_sw + h_bp));
	// DE_HIGH = Hactive
	ret |= maxim2c_i2c_write_reg(client,
			0x0257, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_16BITS, h_active);
	// DE_LOW = Hfp + Hsw + Hbp
	ret |= maxim2c_i2c_write_reg(client,
			0x0259, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_16BITS, h_fp + h_sw + h_bp);
	// DE_CNT = Vactive
	ret |= maxim2c_i2c_write_reg(client,
			0x025B, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_16BITS, v_active);

	/* Generate VS, HS and DE in free-running mode, Invert HS and VS. */
	ret |= maxim2c_i2c_write_byte(client,
			0x0240, MAXIM2C_I2C_REG_ADDR_16BITS,
			0xfb);

	/* Configure Video Pattern Generator. */
	if (pattern_mode == PATTERN_CHECKERBOARD) {
		/* Set checkerboard pattern size. */
		ret |= maxim2c_i2c_write_reg(client,
			0x0264, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_24BITS, 0x3c3c3c);

		/* Set checkerboard pattern colors. */
		ret |= maxim2c_i2c_write_reg(client,
			0x025E, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_24BITS, 0xfecc00);
		ret |= maxim2c_i2c_write_reg(client,
			0x0261, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_24BITS, 0x006aa7);
	} else {
		/* Set gradient increment. */
		ret |= maxim2c_i2c_write_byte(client,
				0x025D, MAXIM2C_I2C_REG_ADDR_16BITS,
				0x10);
	}

	return ret;
}

int maxim2c_pattern_support_mode_init(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;
	struct maxim2c_mode *supported_mode = NULL;

	dev_info(dev, "=== maxim2c pattern support mode init ===\n");

	maxim2c->cfg_modes_num = 1;
	maxim2c->cur_mode = &maxim2c->supported_mode;
	supported_mode = &maxim2c->supported_mode;

	// init using def mode
	memcpy(supported_mode, &maxim2c_pattern_mode, sizeof(struct maxim2c_mode));

	return 0;
}
EXPORT_SYMBOL(maxim2c_pattern_support_mode_init);

int maxim2c_pattern_data_init(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;
	struct device_node *node = NULL;
	struct maxim2c_mode *supported_mode = NULL;
	struct maxim2c_pattern *pattern = NULL;
	maxim2c_mipi_txphy_t *mipi_txphy = &maxim2c->mipi_txphy;
	int ret = 0;

	// maxim serdes local
	node = of_get_child_by_name(dev->of_node, "serdes-local-device");
	if (IS_ERR_OR_NULL(node)) {
		dev_err(dev, "%pOF has no child node: serdes-local-device\n",
				dev->of_node);
		return -ENODEV;
	}

	if (!of_device_is_available(node)) {
		dev_info(dev, "%pOF is disabled\n", node);

		of_node_put(node);
		return -ENODEV;
	}

	maxim2c_mipi_txphy_data_init(maxim2c);

	/* mipi txphy parse dt */
	ret = maxim2c_mipi_txphy_parse_dt(maxim2c, node);
	if (ret) {
		dev_err(dev, "%s: txphy parse dt error\n", __func__);
		return ret;
	}

	// pattern need enable force_clock_out_en
	dev_info(dev, "Pattern mode force_clock_out_en default enable\n");
	mipi_txphy->force_clock_out_en = 1;

	// pattern generator and mode init
	pattern = &maxim2c->pattern;
	pattern->pattern_mode = PATTERN_CHECKERBOARD;
	pattern->pattern_pclk = PATTERN_PCLK_75M;

	supported_mode = &maxim2c->supported_mode;
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
			dev_warn(dev, "link_freq_idx = %d is too low\n",
					supported_mode->link_freq_idx);
		break;
	case PATTERN_PCLK_600M:
		supported_mode->max_fps.denominator = 1500000;
		if (supported_mode->link_freq_idx < 22)
			dev_warn(dev, "link_freq_idx = %d is too low\n",
					supported_mode->link_freq_idx);
		break;
	}

	dev_info(dev, "video pattern: mode = %d, pclk = %d\n",
		pattern->pattern_mode, pattern->pattern_pclk);

	return 0;
}
EXPORT_SYMBOL(maxim2c_pattern_data_init);

int maxim2c_pattern_hw_init(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;
	int ret = 0;

	ret = maxim2c_pattern_previnit(maxim2c);
	if (ret) {
		dev_err(dev, "%s: pattern previnit error\n", __func__);
		return ret;
	}

	ret = maxim2c_mipi_txphy_hw_init(maxim2c);
	if (ret) {
		dev_err(dev, "%s: txphy hw init error\n", __func__);
		return ret;
	}

	ret = maxim2c_pattern_config(maxim2c);
	if (ret) {
		dev_err(dev, "%s: pattern config error\n", __func__);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(maxim2c_pattern_hw_init);
