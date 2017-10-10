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
#define SUN4I_HDMI_IRQ_STA_MASK			0x73
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
#define SUN4I_HDMI_PAD_CTRL1_AMP_OPT		BIT(23)
#define SUN4I_HDMI_PAD_CTRL1_AMPCK_OPT		BIT(22)
#define SUN4I_HDMI_PAD_CTRL1_EMP_OPT		BIT(20)
#define SUN4I_HDMI_PAD_CTRL1_EMPCK_OPT		BIT(19)
#define SUN4I_HDMI_PAD_CTRL1_REG_DEN		BIT(15)
#define SUN4I_HDMI_PAD_CTRL1_REG_DENCK		BIT(14)
#define SUN4I_HDMI_PAD_CTRL1_REG_EMP(n)		(((n) & 7) << 10)
#define SUN4I_HDMI_PAD_CTRL1_HALVE_CLK		BIT(6)
#define SUN4I_HDMI_PAD_CTRL1_REG_AMP(n)		(((n) & 7) << 3)

#define SUN4I_HDMI_PLL_CTRL_REG		0x208
#define SUN4I_HDMI_PLL_CTRL_PLL_EN		BIT(31)
#define SUN4I_HDMI_PLL_CTRL_BWS			BIT(30)
#define SUN4I_HDMI_PLL_CTRL_HV_IS_33		BIT(29)
#define SUN4I_HDMI_PLL_CTRL_LDO1_EN		BIT(28)
#define SUN4I_HDMI_PLL_CTRL_LDO2_EN		BIT(27)
#define SUN4I_HDMI_PLL_CTRL_SDIV2		BIT(25)
#define SUN4I_HDMI_PLL_CTRL_VCO_GAIN(n)		(((n) & 7) << 20)
#define SUN4I_HDMI_PLL_CTRL_S(n)		(((n) & 7) << 17)
#define SUN4I_HDMI_PLL_CTRL_CP_S(n)		(((n) & 0x1f) << 12)
#define SUN4I_HDMI_PLL_CTRL_CS(n)		(((n) & 0xf) << 8)
#define SUN4I_HDMI_PLL_CTRL_DIV(n)		(((n) & 0xf) << 4)
#define SUN4I_HDMI_PLL_CTRL_DIV_MASK		GENMASK(7, 4)
#define SUN4I_HDMI_PLL_CTRL_VCO_S(n)		((n) & 0xf)

#define SUN4I_HDMI_PLL_DBG0_REG		0x20c
#define SUN4I_HDMI_PLL_DBG0_TMDS_PARENT(n)	(((n) & 1) << 21)
#define SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_MASK	BIT(21)
#define SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_SHIFT	21

#define SUN4I_HDMI_CEC			0x214
#define SUN4I_HDMI_CEC_ENABLE			BIT(11)
#define SUN4I_HDMI_CEC_TX			BIT(9)
#define SUN4I_HDMI_CEC_RX			BIT(8)

#define SUN4I_HDMI_PKT_CTRL_REG(n)	(0x2f0 + (4 * (n)))
#define SUN4I_HDMI_PKT_CTRL_TYPE(n, t)		((t) << (((n) % 4) * 4))

#define SUN4I_HDMI_UNKNOWN_REG		0x300
#define SUN4I_HDMI_UNKNOWN_INPUT_SYNC		BIT(27)

#define SUN4I_HDMI_DDC_CTRL_REG		0x500
#define SUN4I_HDMI_DDC_CTRL_ENABLE		BIT(31)
#define SUN4I_HDMI_DDC_CTRL_START_CMD		BIT(30)
#define SUN4I_HDMI_DDC_CTRL_FIFO_DIR_MASK	BIT(8)
#define SUN4I_HDMI_DDC_CTRL_FIFO_DIR_WRITE	(1 << 8)
#define SUN4I_HDMI_DDC_CTRL_FIFO_DIR_READ	(0 << 8)
#define SUN4I_HDMI_DDC_CTRL_RESET		BIT(0)

#define SUN4I_HDMI_DDC_ADDR_REG		0x504
#define SUN4I_HDMI_DDC_ADDR_SEGMENT(seg)	(((seg) & 0xff) << 24)
#define SUN4I_HDMI_DDC_ADDR_EDDC(addr)		(((addr) & 0xff) << 16)
#define SUN4I_HDMI_DDC_ADDR_OFFSET(off)		(((off) & 0xff) << 8)
#define SUN4I_HDMI_DDC_ADDR_SLAVE(addr)		((addr) & 0xff)

#define SUN4I_HDMI_DDC_INT_STATUS_REG		0x50c
#define SUN4I_HDMI_DDC_INT_STATUS_ILLEGAL_FIFO_OPERATION	BIT(7)
#define SUN4I_HDMI_DDC_INT_STATUS_DDC_RX_FIFO_UNDERFLOW		BIT(6)
#define SUN4I_HDMI_DDC_INT_STATUS_DDC_TX_FIFO_OVERFLOW		BIT(5)
#define SUN4I_HDMI_DDC_INT_STATUS_FIFO_REQUEST			BIT(4)
#define SUN4I_HDMI_DDC_INT_STATUS_ARBITRATION_ERROR		BIT(3)
#define SUN4I_HDMI_DDC_INT_STATUS_ACK_ERROR			BIT(2)
#define SUN4I_HDMI_DDC_INT_STATUS_BUS_ERROR			BIT(1)
#define SUN4I_HDMI_DDC_INT_STATUS_TRANSFER_COMPLETE		BIT(0)

#define SUN4I_HDMI_DDC_FIFO_CTRL_REG	0x510
#define SUN4I_HDMI_DDC_FIFO_CTRL_CLEAR		BIT(31)
#define SUN4I_HDMI_DDC_FIFO_CTRL_RX_THRES(n)	(((n) & 0xf) << 4)
#define SUN4I_HDMI_DDC_FIFO_CTRL_RX_THRES_MASK	GENMASK(7, 4)
#define SUN4I_HDMI_DDC_FIFO_CTRL_RX_THRES_MAX	(BIT(4) - 1)
#define SUN4I_HDMI_DDC_FIFO_CTRL_TX_THRES(n)	((n) & 0xf)
#define SUN4I_HDMI_DDC_FIFO_CTRL_TX_THRES_MASK	GENMASK(3, 0)
#define SUN4I_HDMI_DDC_FIFO_CTRL_TX_THRES_MAX	(BIT(4) - 1)

#define SUN4I_HDMI_DDC_FIFO_DATA_REG	0x518

#define SUN4I_HDMI_DDC_BYTE_COUNT_REG	0x51c
#define SUN4I_HDMI_DDC_BYTE_COUNT_MAX		(BIT(10) - 1)

#define SUN4I_HDMI_DDC_CMD_REG		0x520
#define SUN4I_HDMI_DDC_CMD_EXPLICIT_EDDC_READ	6
#define SUN4I_HDMI_DDC_CMD_IMPLICIT_READ	5
#define SUN4I_HDMI_DDC_CMD_IMPLICIT_WRITE	3

#define SUN4I_HDMI_DDC_CLK_REG		0x528
#define SUN4I_HDMI_DDC_CLK_M(m)			(((m) & 0x7) << 3)
#define SUN4I_HDMI_DDC_CLK_N(n)			((n) & 0x7)

#define SUN4I_HDMI_DDC_LINE_CTRL_REG	0x540
#define SUN4I_HDMI_DDC_LINE_CTRL_SDA_ENABLE	BIT(9)
#define SUN4I_HDMI_DDC_LINE_CTRL_SCL_ENABLE	BIT(8)

#define SUN4I_HDMI_DDC_FIFO_SIZE	16

/* A31 specific */
#define SUN6I_HDMI_DDC_CTRL_REG		0x500
#define SUN6I_HDMI_DDC_CTRL_RESET		BIT(31)
#define SUN6I_HDMI_DDC_CTRL_START_CMD		BIT(27)
#define SUN6I_HDMI_DDC_CTRL_SDA_ENABLE		BIT(6)
#define SUN6I_HDMI_DDC_CTRL_SCL_ENABLE		BIT(4)
#define SUN6I_HDMI_DDC_CTRL_ENABLE		BIT(0)

#define SUN6I_HDMI_DDC_CMD_REG		0x508
#define SUN6I_HDMI_DDC_CMD_BYTE_COUNT(count)	((count) << 16)
/* command types in lower 3 bits are the same as sun4i */

#define SUN6I_HDMI_DDC_ADDR_REG		0x50c
#define SUN6I_HDMI_DDC_ADDR_SEGMENT(seg)	(((seg) & 0xff) << 24)
#define SUN6I_HDMI_DDC_ADDR_EDDC(addr)		(((addr) & 0xff) << 16)
#define SUN6I_HDMI_DDC_ADDR_OFFSET(off)		(((off) & 0xff) << 8)
#define SUN6I_HDMI_DDC_ADDR_SLAVE(addr)		(((addr) & 0xff) << 1)

#define SUN6I_HDMI_DDC_INT_STATUS_REG	0x514
#define SUN6I_HDMI_DDC_INT_STATUS_TIMEOUT	BIT(8)
/* lower 8 bits are the same as sun4i */

#define SUN6I_HDMI_DDC_FIFO_CTRL_REG	0x518
#define SUN6I_HDMI_DDC_FIFO_CTRL_CLEAR		BIT(15)
/* lower 9 bits are the same as sun4i */

#define SUN6I_HDMI_DDC_CLK_REG		0x520
/* DDC CLK bit fields are the same, but the formula is not */

#define SUN6I_HDMI_DDC_FIFO_DATA_REG	0x580

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
