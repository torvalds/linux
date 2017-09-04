/*
 * Copyright (C) 2016 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef _SUN4I_HDMI_H_
#define _SUN4I_HDMI_H_

#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <linux/regmap.h>

#include <media/cec-pin.h>

#define SUN4I_HDMI_CTRL_REG		0x004
#define SUN4I_HDMI_CTRL_ENABLE			BIT(31)

#define SUN4I_HDMI_IRQ_REG		0x008
#define SUN4I_HDMI_IRQ_STA_MASK			(GENMASK(6, 4) | GENMASK(1, 0))
#define SUN4I_HDMI_IRQ_STA_FIFO_OF		BIT(1)
#define SUN4I_HDMI_IRQ_STA_FIFO_UF		BIT(0)

#define SUN4I_HDMI_HPD_REG		0x00c
#define SUN4I_HDMI_HPD_HIGH			BIT(0)

#define SUN4I_HDMI_VID_CTRL_REG		0x010
#define SUN4I_HDMI_VID_CTRL_ENABLE		BIT(31)
#define SUN4I_HDMI_VID_CTRL_HDMI_MODE		BIT(30)

#define SUN4I_HDMI_VID_TIMING_ACT_REG	0x014
#define SUN4I_HDMI_VID_TIMING_BP_REG	0x018
#define SUN4I_HDMI_VID_TIMING_FP_REG	0x01c
#define SUN4I_HDMI_VID_TIMING_SPW_REG	0x020

#define SUN4I_HDMI_VID_TIMING_X(x)		((((x) - 1) & GENMASK(11, 0)))
#define SUN4I_HDMI_VID_TIMING_Y(y)		((((y) - 1) & GENMASK(11, 0)) << 16)

#define SUN4I_HDMI_VID_TIMING_POL_REG	0x024
#define SUN4I_HDMI_VID_TIMING_POL_TX_CLK        (0x3e0 << 16)
#define SUN4I_HDMI_VID_TIMING_POL_VSYNC		BIT(1)
#define SUN4I_HDMI_VID_TIMING_POL_HSYNC		BIT(0)

#define SUN4I_HDMI_AVI_INFOFRAME_REG(n)	(0x080 + (n))

#define SUN4I_HDMI_PAD_CTRL0_REG	0x200
#define SUN4I_HDMI_PAD_CTRL0_BIASEN		BIT(31)
#define SUN4I_HDMI_PAD_CTRL0_LDOCEN		BIT(30)
#define SUN4I_HDMI_PAD_CTRL0_LDODEN		BIT(29)
#define SUN4I_HDMI_PAD_CTRL0_PWENC		BIT(28)
#define SUN4I_HDMI_PAD_CTRL0_PWEND		BIT(27)
#define SUN4I_HDMI_PAD_CTRL0_PWENG		BIT(26)
#define SUN4I_HDMI_PAD_CTRL0_CKEN		BIT(25)
#define SUN4I_HDMI_PAD_CTRL0_TXEN		BIT(23)

#define SUN4I_HDMI_PAD_CTRL1_REG	0x204
#define SUN4I_HDMI_PAD_CTRL1_UNKNOWN		BIT(24)	/* set on A31 */
#define SUN4I_HDMI_PAD_CTRL1_AMP_OPT		BIT(23)
#define SUN4I_HDMI_PAD_CTRL1_AMPCK_OPT		BIT(22)
#define SUN4I_HDMI_PAD_CTRL1_EMP_OPT		BIT(20)
#define SUN4I_HDMI_PAD_CTRL1_EMPCK_OPT		BIT(19)
#define SUN4I_HDMI_PAD_CTRL1_PWSCK		BIT(18)
#define SUN4I_HDMI_PAD_CTRL1_PWSDT		BIT(17)
#define SUN4I_HDMI_PAD_CTRL1_REG_DEN		BIT(15)
#define SUN4I_HDMI_PAD_CTRL1_REG_DENCK		BIT(14)
#define SUN4I_HDMI_PAD_CTRL1_REG_EMP(n)		(((n) << 10) & GENMASK(12, 10))
#define SUN4I_HDMI_PAD_CTRL1_HALVE_CLK		BIT(6)
#define SUN4I_HDMI_PAD_CTRL1_REG_AMP(n)		(((n) << 3) & GENMASK(5, 3))

/* These bits seem to invert the TMDS data channels */
#define SUN4I_HDMI_PAD_CTRL1_INVERT_R		BIT(2)
#define SUN4I_HDMI_PAD_CTRL1_INVERT_G		BIT(1)
#define SUN4I_HDMI_PAD_CTRL1_INVERT_B		BIT(0)

#define SUN4I_HDMI_PLL_CTRL_REG		0x208
#define SUN4I_HDMI_PLL_CTRL_PLL_EN		BIT(31)
#define SUN4I_HDMI_PLL_CTRL_BWS			BIT(30)
#define SUN4I_HDMI_PLL_CTRL_HV_IS_33		BIT(29)
#define SUN4I_HDMI_PLL_CTRL_LDO1_EN		BIT(28)
#define SUN4I_HDMI_PLL_CTRL_LDO2_EN		BIT(27)
#define SUN4I_HDMI_PLL_CTRL_SDIV2		BIT(25)
#define SUN4I_HDMI_PLL_CTRL_VCO_GAIN(n)		(((n) << 20) & GENMASK(22, 20))
#define SUN4I_HDMI_PLL_CTRL_S(n)		(((n) << 17) & GENMASK(19, 17))
#define SUN4I_HDMI_PLL_CTRL_CP_S(n)		(((n) << 12) & GENMASK(16, 12))
#define SUN4I_HDMI_PLL_CTRL_CS(n)		(((n) << 8) & GENMASK(11, 8))
#define SUN4I_HDMI_PLL_CTRL_DIV(n)		(((n) << 4) & GENMASK(7, 4))
#define SUN4I_HDMI_PLL_CTRL_DIV_MASK		GENMASK(7, 4)
#define SUN4I_HDMI_PLL_CTRL_VCO_S(n)		((n) & GENMASK(3, 0))

#define SUN4I_HDMI_PLL_DBG0_REG		0x20c
#define SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_MASK	BIT(21)
#define SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_OFFSET	21
#define SUN4I_HDMI_PLL_DBG0_TMDS_PARENT(n)	\
	(((n) << SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_OFFSET) & \
	 SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_MASK)
#define SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_GET(n)	\
	(((n) & SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_MASK) >> \
	 SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_OFFSET)

#define SUN4I_HDMI_CEC			0x214
#define SUN4I_HDMI_CEC_ENABLE			BIT(11)
#define SUN4I_HDMI_CEC_TX			BIT(9)
#define SUN4I_HDMI_CEC_RX			BIT(8)

#define SUN4I_HDMI_PKT_CTRL_REG(n)	(0x2f0 + (4 * (n)))
#define SUN4I_HDMI_PKT_CTRL_TYPE(n, t)		((t) << (((n) % 4) * 4))

#define SUN4I_HDMI_UNKNOWN_REG		0x300
#define SUN4I_HDMI_UNKNOWN_INPUT_SYNC		BIT(27)

enum sun4i_hdmi_pkt_type {
	SUN4I_HDMI_PKT_AVI = 2,
	SUN4I_HDMI_PKT_END = 15,
};

struct sun4i_hdmi_variant {
	bool has_ddc_parent_clk;
	bool has_reset_control;

	u32 pad_ctrl0_init_val;
	u32 pad_ctrl1_init_val;
	u32 pll_ctrl_init_val;

	struct reg_field ddc_clk_reg;
	u8 ddc_clk_pre_divider;
	u8 ddc_clk_m_offset;

	u8 tmds_clk_div_offset;

	/* Register fields for I2C adapter */
	struct reg_field	field_ddc_en;
	struct reg_field	field_ddc_start;
	struct reg_field	field_ddc_reset;
	struct reg_field	field_ddc_addr_reg;
	struct reg_field	field_ddc_slave_addr;
	struct reg_field	field_ddc_int_mask;
	struct reg_field	field_ddc_int_status;
	struct reg_field	field_ddc_fifo_clear;
	struct reg_field	field_ddc_fifo_rx_thres;
	struct reg_field	field_ddc_fifo_tx_thres;
	struct reg_field	field_ddc_byte_count;
	struct reg_field	field_ddc_cmd;
	struct reg_field	field_ddc_sda_en;
	struct reg_field	field_ddc_sck_en;

	/* DDC FIFO register offset */
	u32			ddc_fifo_reg;

	/*
	 * DDC FIFO threshold boundary conditions
	 *
	 * This is used to cope with the threshold boundary condition
	 * being slightly different on sun5i and sun6i.
	 *
	 * On sun5i the threshold is exclusive, i.e. does not include,
	 * the value of the threshold. ( > for RX; < for TX )
	 * On sun6i the threshold is inclusive, i.e. includes, the
	 * value of the threshold. ( >= for RX; <= for TX )
	 */
	bool			ddc_fifo_thres_incl;

	bool			ddc_fifo_has_dir;
};

struct sun4i_hdmi {
	struct drm_connector	connector;
	struct drm_encoder	encoder;
	struct device		*dev;

	void __iomem		*base;
	struct regmap		*regmap;

	/* Reset control */
	struct reset_control	*reset;

	/* Parent clocks */
	struct clk		*bus_clk;
	struct clk		*mod_clk;
	struct clk		*ddc_parent_clk;
	struct clk		*pll0_clk;
	struct clk		*pll1_clk;

	/* And the clocks we create */
	struct clk		*ddc_clk;
	struct clk		*tmds_clk;

	struct i2c_adapter	*i2c;

	/* Regmap fields for I2C adapter */
	struct regmap_field	*field_ddc_en;
	struct regmap_field	*field_ddc_start;
	struct regmap_field	*field_ddc_reset;
	struct regmap_field	*field_ddc_addr_reg;
	struct regmap_field	*field_ddc_slave_addr;
	struct regmap_field	*field_ddc_int_mask;
	struct regmap_field	*field_ddc_int_status;
	struct regmap_field	*field_ddc_fifo_clear;
	struct regmap_field	*field_ddc_fifo_rx_thres;
	struct regmap_field	*field_ddc_fifo_tx_thres;
	struct regmap_field	*field_ddc_byte_count;
	struct regmap_field	*field_ddc_cmd;
	struct regmap_field	*field_ddc_sda_en;
	struct regmap_field	*field_ddc_sck_en;

	struct sun4i_drv	*drv;

	bool			hdmi_monitor;
	struct cec_adapter	*cec_adap;

	const struct sun4i_hdmi_variant	*variant;
};

int sun4i_ddc_create(struct sun4i_hdmi *hdmi, struct clk *clk);
int sun4i_tmds_create(struct sun4i_hdmi *hdmi);
int sun4i_hdmi_i2c_create(struct device *dev, struct sun4i_hdmi *hdmi);

#endif /* _SUN4I_HDMI_H_ */
