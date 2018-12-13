/*
 * Driver for IDT Versaclock 5
 *
 * Copyright (C) 2017 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Possible optimizations:
 * - Use spread spectrum
 * - Use integer divider in FOD if applicable
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/rational.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* VersaClock5 registers */
#define VC5_OTP_CONTROL				0x00

/* Factory-reserved register block */
#define VC5_RSVD_DEVICE_ID			0x01
#define VC5_RSVD_ADC_GAIN_7_0			0x02
#define VC5_RSVD_ADC_GAIN_15_8			0x03
#define VC5_RSVD_ADC_OFFSET_7_0			0x04
#define VC5_RSVD_ADC_OFFSET_15_8		0x05
#define VC5_RSVD_TEMPY				0x06
#define VC5_RSVD_OFFSET_TBIN			0x07
#define VC5_RSVD_GAIN				0x08
#define VC5_RSVD_TEST_NP			0x09
#define VC5_RSVD_UNUSED				0x0a
#define VC5_RSVD_BANDGAP_TRIM_UP		0x0b
#define VC5_RSVD_BANDGAP_TRIM_DN		0x0c
#define VC5_RSVD_CLK_R_12_CLK_AMP_4		0x0d
#define VC5_RSVD_CLK_R_34_CLK_AMP_4		0x0e
#define VC5_RSVD_CLK_AMP_123			0x0f

/* Configuration register block */
#define VC5_PRIM_SRC_SHDN			0x10
#define VC5_PRIM_SRC_SHDN_EN_XTAL		BIT(7)
#define VC5_PRIM_SRC_SHDN_EN_CLKIN		BIT(6)
#define VC5_PRIM_SRC_SHDN_EN_DOUBLE_XTAL_FREQ	BIT(3)
#define VC5_PRIM_SRC_SHDN_SP			BIT(1)
#define VC5_PRIM_SRC_SHDN_EN_GBL_SHDN		BIT(0)

#define VC5_VCO_BAND				0x11
#define VC5_XTAL_X1_LOAD_CAP			0x12
#define VC5_XTAL_X2_LOAD_CAP			0x13
#define VC5_REF_DIVIDER				0x15
#define VC5_REF_DIVIDER_SEL_PREDIV2		BIT(7)
#define VC5_REF_DIVIDER_REF_DIV(n)		((n) & 0x3f)

#define VC5_VCO_CTRL_AND_PREDIV			0x16
#define VC5_VCO_CTRL_AND_PREDIV_BYPASS_PREDIV	BIT(7)

#define VC5_FEEDBACK_INT_DIV			0x17
#define VC5_FEEDBACK_INT_DIV_BITS		0x18
#define VC5_FEEDBACK_FRAC_DIV(n)		(0x19 + (n))
#define VC5_RC_CONTROL0				0x1e
#define VC5_RC_CONTROL1				0x1f
/* Register 0x20 is factory reserved */

/* Output divider control for divider 1,2,3,4 */
#define VC5_OUT_DIV_CONTROL(idx)	(0x21 + ((idx) * 0x10))
#define VC5_OUT_DIV_CONTROL_RESET	BIT(7)
#define VC5_OUT_DIV_CONTROL_SELB_NORM	BIT(3)
#define VC5_OUT_DIV_CONTROL_SEL_EXT	BIT(2)
#define VC5_OUT_DIV_CONTROL_INT_MODE	BIT(1)
#define VC5_OUT_DIV_CONTROL_EN_FOD	BIT(0)

#define VC5_OUT_DIV_FRAC(idx, n)	(0x22 + ((idx) * 0x10) + (n))
#define VC5_OUT_DIV_FRAC4_OD_SCEE	BIT(1)

#define VC5_OUT_DIV_STEP_SPREAD(idx, n)	(0x26 + ((idx) * 0x10) + (n))
#define VC5_OUT_DIV_SPREAD_MOD(idx, n)	(0x29 + ((idx) * 0x10) + (n))
#define VC5_OUT_DIV_SKEW_INT(idx, n)	(0x2b + ((idx) * 0x10) + (n))
#define VC5_OUT_DIV_INT(idx, n)		(0x2d + ((idx) * 0x10) + (n))
#define VC5_OUT_DIV_SKEW_FRAC(idx)	(0x2f + ((idx) * 0x10))
/* Registers 0x30, 0x40, 0x50 are factory reserved */

/* Clock control register for clock 1,2 */
#define VC5_CLK_OUTPUT_CFG(idx, n)	(0x60 + ((idx) * 0x2) + (n))
#define VC5_CLK_OUTPUT_CFG1_EN_CLKBUF	BIT(0)

#define VC5_CLK_OE_SHDN				0x68
#define VC5_CLK_OS_SHDN				0x69

#define VC5_GLOBAL_REGISTER			0x76
#define VC5_GLOBAL_REGISTER_GLOBAL_RESET	BIT(5)

/* PLL/VCO runs between 2.5 GHz and 3.0 GHz */
#define VC5_PLL_VCO_MIN				2500000000UL
#define VC5_PLL_VCO_MAX				3000000000UL

/* VC5 Input mux settings */
#define VC5_MUX_IN_XIN		BIT(0)
#define VC5_MUX_IN_CLKIN	BIT(1)

/* Maximum number of clk_out supported by this driver */
#define VC5_MAX_CLK_OUT_NUM	5

/* Maximum number of FODs supported by this driver */
#define VC5_MAX_FOD_NUM	4

/* flags to describe chip features */
/* chip has built-in oscilator */
#define VC5_HAS_INTERNAL_XTAL	BIT(0)
/* chip has PFD requency doubler */
#define VC5_HAS_PFD_FREQ_DBL	BIT(1)

/* Supported IDT VC5 models. */
enum vc5_model {
	IDT_VC5_5P49V5923,
	IDT_VC5_5P49V5925,
	IDT_VC5_5P49V5933,
	IDT_VC5_5P49V5935,
	IDT_VC6_5P49V6901,
};

/* Structure to describe features of a particular VC5 model */
struct vc5_chip_info {
	const enum vc5_model	model;
	const unsigned int	clk_fod_cnt;
	const unsigned int	clk_out_cnt;
	const u32		flags;
};

struct vc5_driver_data;

struct vc5_hw_data {
	struct clk_hw		hw;
	struct vc5_driver_data	*vc5;
	u32			div_int;
	u32			div_frc;
	unsigned int		num;
};

struct vc5_driver_data {
	struct i2c_client	*client;
	struct regmap		*regmap;
	const struct vc5_chip_info	*chip_info;

	struct clk		*pin_xin;
	struct clk		*pin_clkin;
	unsigned char		clk_mux_ins;
	struct clk_hw		clk_mux;
	struct clk_hw		clk_mul;
	struct clk_hw		clk_pfd;
	struct vc5_hw_data	clk_pll;
	struct vc5_hw_data	clk_fod[VC5_MAX_FOD_NUM];
	struct vc5_hw_data	clk_out[VC5_MAX_CLK_OUT_NUM];
};

static const char * const vc5_mux_names[] = {
	"mux"
};

static const char * const vc5_dbl_names[] = {
	"dbl"
};

static const char * const vc5_pfd_names[] = {
	"pfd"
};

static const char * const vc5_pll_names[] = {
	"pll"
};

static const char * const vc5_fod_names[] = {
	"fod0", "fod1", "fod2", "fod3",
};

static const char * const vc5_clk_out_names[] = {
	"out0_sel_i2cb", "out1", "out2", "out3", "out4",
};

/*
 * VersaClock5 i2c regmap
 */
static bool vc5_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	/* Factory reserved regs, make them read-only */
	if (reg <= 0xf)
		return false;

	/* Factory reserved regs, make them read-only */
	if (reg == 0x14 || reg == 0x1c || reg == 0x1d)
		return false;

	return true;
}

static const struct regmap_config vc5_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = 0x76,
	.writeable_reg = vc5_regmap_is_writeable,
};

/*
 * VersaClock5 input multiplexer between XTAL and CLKIN divider
 */
static unsigned char vc5_mux_get_parent(struct clk_hw *hw)
{
	struct vc5_driver_data *vc5 =
		container_of(hw, struct vc5_driver_data, clk_mux);
	const u8 mask = VC5_PRIM_SRC_SHDN_EN_XTAL | VC5_PRIM_SRC_SHDN_EN_CLKIN;
	unsigned int src;

	regmap_read(vc5->regmap, VC5_PRIM_SRC_SHDN, &src);
	src &= mask;

	if (src == VC5_PRIM_SRC_SHDN_EN_XTAL)
		return 0;

	if (src == VC5_PRIM_SRC_SHDN_EN_CLKIN)
		return 1;

	dev_warn(&vc5->client->dev,
		 "Invalid clock input configuration (%02x)\n", src);
	return 0;
}

static int vc5_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct vc5_driver_data *vc5 =
		container_of(hw, struct vc5_driver_data, clk_mux);
	const u8 mask = VC5_PRIM_SRC_SHDN_EN_XTAL | VC5_PRIM_SRC_SHDN_EN_CLKIN;
	u8 src;

	if ((index > 1) || !vc5->clk_mux_ins)
		return -EINVAL;

	if (vc5->clk_mux_ins == (VC5_MUX_IN_CLKIN | VC5_MUX_IN_XIN)) {
		if (index == 0)
			src = VC5_PRIM_SRC_SHDN_EN_XTAL;
		if (index == 1)
			src = VC5_PRIM_SRC_SHDN_EN_CLKIN;
	} else {
		if (index != 0)
			return -EINVAL;

		if (vc5->clk_mux_ins == VC5_MUX_IN_XIN)
			src = VC5_PRIM_SRC_SHDN_EN_XTAL;
		if (vc5->clk_mux_ins == VC5_MUX_IN_CLKIN)
			src = VC5_PRIM_SRC_SHDN_EN_CLKIN;
	}

	return regmap_update_bits(vc5->regmap, VC5_PRIM_SRC_SHDN, mask, src);
}

static const struct clk_ops vc5_mux_ops = {
	.set_parent	= vc5_mux_set_parent,
	.get_parent	= vc5_mux_get_parent,
};

static unsigned long vc5_dbl_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct vc5_driver_data *vc5 =
		container_of(hw, struct vc5_driver_data, clk_mul);
	unsigned int premul;

	regmap_read(vc5->regmap, VC5_PRIM_SRC_SHDN, &premul);
	if (premul & VC5_PRIM_SRC_SHDN_EN_DOUBLE_XTAL_FREQ)
		parent_rate *= 2;

	return parent_rate;
}

static long vc5_dbl_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	if ((*parent_rate == rate) || ((*parent_rate * 2) == rate))
		return rate;
	else
		return -EINVAL;
}

static int vc5_dbl_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct vc5_driver_data *vc5 =
		container_of(hw, struct vc5_driver_data, clk_mul);
	u32 mask;

	if ((parent_rate * 2) == rate)
		mask = VC5_PRIM_SRC_SHDN_EN_DOUBLE_XTAL_FREQ;
	else
		mask = 0;

	regmap_update_bits(vc5->regmap, VC5_PRIM_SRC_SHDN,
			   VC5_PRIM_SRC_SHDN_EN_DOUBLE_XTAL_FREQ,
			   mask);

	return 0;
}

static const struct clk_ops vc5_dbl_ops = {
	.recalc_rate	= vc5_dbl_recalc_rate,
	.round_rate	= vc5_dbl_round_rate,
	.set_rate	= vc5_dbl_set_rate,
};

static unsigned long vc5_pfd_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct vc5_driver_data *vc5 =
		container_of(hw, struct vc5_driver_data, clk_pfd);
	unsigned int prediv, div;

	regmap_read(vc5->regmap, VC5_VCO_CTRL_AND_PREDIV, &prediv);

	/* The bypass_prediv is set, PLL fed from Ref_in directly. */
	if (prediv & VC5_VCO_CTRL_AND_PREDIV_BYPASS_PREDIV)
		return parent_rate;

	regmap_read(vc5->regmap, VC5_REF_DIVIDER, &div);

	/* The Sel_prediv2 is set, PLL fed from prediv2 (Ref_in / 2) */
	if (div & VC5_REF_DIVIDER_SEL_PREDIV2)
		return parent_rate / 2;
	else
		return parent_rate / VC5_REF_DIVIDER_REF_DIV(div);
}

static long vc5_pfd_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	unsigned long idiv;

	/* PLL cannot operate with input clock above 50 MHz. */
	if (rate > 50000000)
		return -EINVAL;

	/* CLKIN within range of PLL input, feed directly to PLL. */
	if (*parent_rate <= 50000000)
		return *parent_rate;

	idiv = DIV_ROUND_UP(*parent_rate, rate);
	if (idiv > 127)
		return -EINVAL;

	return *parent_rate / idiv;
}

static int vc5_pfd_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct vc5_driver_data *vc5 =
		container_of(hw, struct vc5_driver_data, clk_pfd);
	unsigned long idiv;
	u8 div;

	/* CLKIN within range of PLL input, feed directly to PLL. */
	if (parent_rate <= 50000000) {
		regmap_update_bits(vc5->regmap, VC5_VCO_CTRL_AND_PREDIV,
				   VC5_VCO_CTRL_AND_PREDIV_BYPASS_PREDIV,
				   VC5_VCO_CTRL_AND_PREDIV_BYPASS_PREDIV);
		regmap_update_bits(vc5->regmap, VC5_REF_DIVIDER, 0xff, 0x00);
		return 0;
	}

	idiv = DIV_ROUND_UP(parent_rate, rate);

	/* We have dedicated div-2 predivider. */
	if (idiv == 2)
		div = VC5_REF_DIVIDER_SEL_PREDIV2;
	else
		div = VC5_REF_DIVIDER_REF_DIV(idiv);

	regmap_update_bits(vc5->regmap, VC5_REF_DIVIDER, 0xff, div);
	regmap_update_bits(vc5->regmap, VC5_VCO_CTRL_AND_PREDIV,
			   VC5_VCO_CTRL_AND_PREDIV_BYPASS_PREDIV, 0);

	return 0;
}

static const struct clk_ops vc5_pfd_ops = {
	.recalc_rate	= vc5_pfd_recalc_rate,
	.round_rate	= vc5_pfd_round_rate,
	.set_rate	= vc5_pfd_set_rate,
};

/*
 * VersaClock5 PLL/VCO
 */
static unsigned long vc5_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct vc5_hw_data *hwdata = container_of(hw, struct vc5_hw_data, hw);
	struct vc5_driver_data *vc5 = hwdata->vc5;
	u32 div_int, div_frc;
	u8 fb[5];

	regmap_bulk_read(vc5->regmap, VC5_FEEDBACK_INT_DIV, fb, 5);

	div_int = (fb[0] << 4) | (fb[1] >> 4);
	div_frc = (fb[2] << 16) | (fb[3] << 8) | fb[4];

	/* The PLL divider has 12 integer bits and 24 fractional bits */
	return (parent_rate * div_int) + ((parent_rate * div_frc) >> 24);
}

static long vc5_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	struct vc5_hw_data *hwdata = container_of(hw, struct vc5_hw_data, hw);
	u32 div_int;
	u64 div_frc;

	if (rate < VC5_PLL_VCO_MIN)
		rate = VC5_PLL_VCO_MIN;
	if (rate > VC5_PLL_VCO_MAX)
		rate = VC5_PLL_VCO_MAX;

	/* Determine integer part, which is 12 bit wide */
	div_int = rate / *parent_rate;
	if (div_int > 0xfff)
		rate = *parent_rate * 0xfff;

	/* Determine best fractional part, which is 24 bit wide */
	div_frc = rate % *parent_rate;
	div_frc *= BIT(24) - 1;
	do_div(div_frc, *parent_rate);

	hwdata->div_int = div_int;
	hwdata->div_frc = (u32)div_frc;

	return (*parent_rate * div_int) + ((*parent_rate * div_frc) >> 24);
}

static int vc5_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct vc5_hw_data *hwdata = container_of(hw, struct vc5_hw_data, hw);
	struct vc5_driver_data *vc5 = hwdata->vc5;
	u8 fb[5];

	fb[0] = hwdata->div_int >> 4;
	fb[1] = hwdata->div_int << 4;
	fb[2] = hwdata->div_frc >> 16;
	fb[3] = hwdata->div_frc >> 8;
	fb[4] = hwdata->div_frc;

	return regmap_bulk_write(vc5->regmap, VC5_FEEDBACK_INT_DIV, fb, 5);
}

static const struct clk_ops vc5_pll_ops = {
	.recalc_rate	= vc5_pll_recalc_rate,
	.round_rate	= vc5_pll_round_rate,
	.set_rate	= vc5_pll_set_rate,
};

static unsigned long vc5_fod_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct vc5_hw_data *hwdata = container_of(hw, struct vc5_hw_data, hw);
	struct vc5_driver_data *vc5 = hwdata->vc5;
	/* VCO frequency is divided by two before entering FOD */
	u32 f_in = parent_rate / 2;
	u32 div_int, div_frc;
	u8 od_int[2];
	u8 od_frc[4];

	regmap_bulk_read(vc5->regmap, VC5_OUT_DIV_INT(hwdata->num, 0),
			 od_int, 2);
	regmap_bulk_read(vc5->regmap, VC5_OUT_DIV_FRAC(hwdata->num, 0),
			 od_frc, 4);

	div_int = (od_int[0] << 4) | (od_int[1] >> 4);
	div_frc = (od_frc[0] << 22) | (od_frc[1] << 14) |
		  (od_frc[2] << 6) | (od_frc[3] >> 2);

	/* Avoid division by zero if the output is not configured. */
	if (div_int == 0 && div_frc == 0)
		return 0;

	/* The PLL divider has 12 integer bits and 30 fractional bits */
	return div64_u64((u64)f_in << 24ULL, ((u64)div_int << 24ULL) + div_frc);
}

static long vc5_fod_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	struct vc5_hw_data *hwdata = container_of(hw, struct vc5_hw_data, hw);
	/* VCO frequency is divided by two before entering FOD */
	u32 f_in = *parent_rate / 2;
	u32 div_int;
	u64 div_frc;

	/* Determine integer part, which is 12 bit wide */
	div_int = f_in / rate;
	/*
	 * WARNING: The clock chip does not output signal if the integer part
	 *          of the divider is 0xfff and fractional part is non-zero.
	 *          Clamp the divider at 0xffe to keep the code simple.
	 */
	if (div_int > 0xffe) {
		div_int = 0xffe;
		rate = f_in / div_int;
	}

	/* Determine best fractional part, which is 30 bit wide */
	div_frc = f_in % rate;
	div_frc <<= 24;
	do_div(div_frc, rate);

	hwdata->div_int = div_int;
	hwdata->div_frc = (u32)div_frc;

	return div64_u64((u64)f_in << 24ULL, ((u64)div_int << 24ULL) + div_frc);
}

static int vc5_fod_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct vc5_hw_data *hwdata = container_of(hw, struct vc5_hw_data, hw);
	struct vc5_driver_data *vc5 = hwdata->vc5;
	u8 data[14] = {
		hwdata->div_frc >> 22, hwdata->div_frc >> 14,
		hwdata->div_frc >> 6, hwdata->div_frc << 2,
		0, 0, 0, 0, 0,
		0, 0,
		hwdata->div_int >> 4, hwdata->div_int << 4,
		0
	};

	regmap_bulk_write(vc5->regmap, VC5_OUT_DIV_FRAC(hwdata->num, 0),
			  data, 14);

	/*
	 * Toggle magic bit in undocumented register for unknown reason.
	 * This is what the IDT timing commander tool does and the chip
	 * datasheet somewhat implies this is needed, but the register
	 * and the bit is not documented.
	 */
	regmap_update_bits(vc5->regmap, VC5_GLOBAL_REGISTER,
			   VC5_GLOBAL_REGISTER_GLOBAL_RESET, 0);
	regmap_update_bits(vc5->regmap, VC5_GLOBAL_REGISTER,
			   VC5_GLOBAL_REGISTER_GLOBAL_RESET,
			   VC5_GLOBAL_REGISTER_GLOBAL_RESET);
	return 0;
}

static const struct clk_ops vc5_fod_ops = {
	.recalc_rate	= vc5_fod_recalc_rate,
	.round_rate	= vc5_fod_round_rate,
	.set_rate	= vc5_fod_set_rate,
};

static int vc5_clk_out_prepare(struct clk_hw *hw)
{
	struct vc5_hw_data *hwdata = container_of(hw, struct vc5_hw_data, hw);
	struct vc5_driver_data *vc5 = hwdata->vc5;
	const u8 mask = VC5_OUT_DIV_CONTROL_SELB_NORM |
			VC5_OUT_DIV_CONTROL_SEL_EXT |
			VC5_OUT_DIV_CONTROL_EN_FOD;
	unsigned int src;
	int ret;

	/*
	 * If the input mux is disabled, enable it first and
	 * select source from matching FOD.
	 */
	regmap_read(vc5->regmap, VC5_OUT_DIV_CONTROL(hwdata->num), &src);
	if ((src & mask) == 0) {
		src = VC5_OUT_DIV_CONTROL_RESET | VC5_OUT_DIV_CONTROL_EN_FOD;
		ret = regmap_update_bits(vc5->regmap,
					 VC5_OUT_DIV_CONTROL(hwdata->num),
					 mask | VC5_OUT_DIV_CONTROL_RESET, src);
		if (ret)
			return ret;
	}

	/* Enable the clock buffer */
	regmap_update_bits(vc5->regmap, VC5_CLK_OUTPUT_CFG(hwdata->num, 1),
			   VC5_CLK_OUTPUT_CFG1_EN_CLKBUF,
			   VC5_CLK_OUTPUT_CFG1_EN_CLKBUF);
	return 0;
}

static void vc5_clk_out_unprepare(struct clk_hw *hw)
{
	struct vc5_hw_data *hwdata = container_of(hw, struct vc5_hw_data, hw);
	struct vc5_driver_data *vc5 = hwdata->vc5;

	/* Disable the clock buffer */
	regmap_update_bits(vc5->regmap, VC5_CLK_OUTPUT_CFG(hwdata->num, 1),
			   VC5_CLK_OUTPUT_CFG1_EN_CLKBUF, 0);
}

static unsigned char vc5_clk_out_get_parent(struct clk_hw *hw)
{
	struct vc5_hw_data *hwdata = container_of(hw, struct vc5_hw_data, hw);
	struct vc5_driver_data *vc5 = hwdata->vc5;
	const u8 mask = VC5_OUT_DIV_CONTROL_SELB_NORM |
			VC5_OUT_DIV_CONTROL_SEL_EXT |
			VC5_OUT_DIV_CONTROL_EN_FOD;
	const u8 fodclkmask = VC5_OUT_DIV_CONTROL_SELB_NORM |
			      VC5_OUT_DIV_CONTROL_EN_FOD;
	const u8 extclk = VC5_OUT_DIV_CONTROL_SELB_NORM |
			  VC5_OUT_DIV_CONTROL_SEL_EXT;
	unsigned int src;

	regmap_read(vc5->regmap, VC5_OUT_DIV_CONTROL(hwdata->num), &src);
	src &= mask;

	if (src == 0)	/* Input mux set to DISABLED */
		return 0;

	if ((src & fodclkmask) == VC5_OUT_DIV_CONTROL_EN_FOD)
		return 0;

	if (src == extclk)
		return 1;

	dev_warn(&vc5->client->dev,
		 "Invalid clock output configuration (%02x)\n", src);
	return 0;
}

static int vc5_clk_out_set_parent(struct clk_hw *hw, u8 index)
{
	struct vc5_hw_data *hwdata = container_of(hw, struct vc5_hw_data, hw);
	struct vc5_driver_data *vc5 = hwdata->vc5;
	const u8 mask = VC5_OUT_DIV_CONTROL_RESET |
			VC5_OUT_DIV_CONTROL_SELB_NORM |
			VC5_OUT_DIV_CONTROL_SEL_EXT |
			VC5_OUT_DIV_CONTROL_EN_FOD;
	const u8 extclk = VC5_OUT_DIV_CONTROL_SELB_NORM |
			  VC5_OUT_DIV_CONTROL_SEL_EXT;
	u8 src = VC5_OUT_DIV_CONTROL_RESET;

	if (index == 0)
		src |= VC5_OUT_DIV_CONTROL_EN_FOD;
	else
		src |= extclk;

	return regmap_update_bits(vc5->regmap, VC5_OUT_DIV_CONTROL(hwdata->num),
				  mask, src);
}

static const struct clk_ops vc5_clk_out_ops = {
	.prepare	= vc5_clk_out_prepare,
	.unprepare	= vc5_clk_out_unprepare,
	.set_parent	= vc5_clk_out_set_parent,
	.get_parent	= vc5_clk_out_get_parent,
};

static struct clk_hw *vc5_of_clk_get(struct of_phandle_args *clkspec,
				     void *data)
{
	struct vc5_driver_data *vc5 = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= vc5->chip_info->clk_out_cnt)
		return ERR_PTR(-EINVAL);

	return &vc5->clk_out[idx].hw;
}

static int vc5_map_index_to_output(const enum vc5_model model,
				   const unsigned int n)
{
	switch (model) {
	case IDT_VC5_5P49V5933:
		return (n == 0) ? 0 : 3;
	case IDT_VC5_5P49V5923:
	case IDT_VC5_5P49V5925:
	case IDT_VC5_5P49V5935:
	case IDT_VC6_5P49V6901:
	default:
		return n;
	}
}

static const struct of_device_id clk_vc5_of_match[];

static int vc5_probe(struct i2c_client *client,
		     const struct i2c_device_id *id)
{
	struct vc5_driver_data *vc5;
	struct clk_init_data init;
	const char *parent_names[2];
	unsigned int n, idx = 0;
	int ret;

	vc5 = devm_kzalloc(&client->dev, sizeof(*vc5), GFP_KERNEL);
	if (vc5 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, vc5);
	vc5->client = client;
	vc5->chip_info = of_device_get_match_data(&client->dev);

	vc5->pin_xin = devm_clk_get(&client->dev, "xin");
	if (PTR_ERR(vc5->pin_xin) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	vc5->pin_clkin = devm_clk_get(&client->dev, "clkin");
	if (PTR_ERR(vc5->pin_clkin) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	vc5->regmap = devm_regmap_init_i2c(client, &vc5_regmap_config);
	if (IS_ERR(vc5->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(vc5->regmap);
	}

	/* Register clock input mux */
	memset(&init, 0, sizeof(init));

	if (!IS_ERR(vc5->pin_xin)) {
		vc5->clk_mux_ins |= VC5_MUX_IN_XIN;
		parent_names[init.num_parents++] = __clk_get_name(vc5->pin_xin);
	} else if (vc5->chip_info->flags & VC5_HAS_INTERNAL_XTAL) {
		vc5->pin_xin = clk_register_fixed_rate(&client->dev,
						       "internal-xtal", NULL,
						       0, 25000000);
		if (IS_ERR(vc5->pin_xin))
			return PTR_ERR(vc5->pin_xin);
		vc5->clk_mux_ins |= VC5_MUX_IN_XIN;
		parent_names[init.num_parents++] = __clk_get_name(vc5->pin_xin);
	}

	if (!IS_ERR(vc5->pin_clkin)) {
		vc5->clk_mux_ins |= VC5_MUX_IN_CLKIN;
		parent_names[init.num_parents++] =
			__clk_get_name(vc5->pin_clkin);
	}

	if (!init.num_parents) {
		dev_err(&client->dev, "no input clock specified!\n");
		return -EINVAL;
	}

	init.name = vc5_mux_names[0];
	init.ops = &vc5_mux_ops;
	init.flags = 0;
	init.parent_names = parent_names;
	vc5->clk_mux.init = &init;
	ret = devm_clk_hw_register(&client->dev, &vc5->clk_mux);
	if (ret) {
		dev_err(&client->dev, "unable to register %s\n", init.name);
		goto err_clk;
	}

	if (vc5->chip_info->flags & VC5_HAS_PFD_FREQ_DBL) {
		/* Register frequency doubler */
		memset(&init, 0, sizeof(init));
		init.name = vc5_dbl_names[0];
		init.ops = &vc5_dbl_ops;
		init.flags = CLK_SET_RATE_PARENT;
		init.parent_names = vc5_mux_names;
		init.num_parents = 1;
		vc5->clk_mul.init = &init;
		ret = devm_clk_hw_register(&client->dev, &vc5->clk_mul);
		if (ret) {
			dev_err(&client->dev, "unable to register %s\n",
				init.name);
			goto err_clk;
		}
	}

	/* Register PFD */
	memset(&init, 0, sizeof(init));
	init.name = vc5_pfd_names[0];
	init.ops = &vc5_pfd_ops;
	init.flags = CLK_SET_RATE_PARENT;
	if (vc5->chip_info->flags & VC5_HAS_PFD_FREQ_DBL)
		init.parent_names = vc5_dbl_names;
	else
		init.parent_names = vc5_mux_names;
	init.num_parents = 1;
	vc5->clk_pfd.init = &init;
	ret = devm_clk_hw_register(&client->dev, &vc5->clk_pfd);
	if (ret) {
		dev_err(&client->dev, "unable to register %s\n", init.name);
		goto err_clk;
	}

	/* Register PLL */
	memset(&init, 0, sizeof(init));
	init.name = vc5_pll_names[0];
	init.ops = &vc5_pll_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = vc5_pfd_names;
	init.num_parents = 1;
	vc5->clk_pll.num = 0;
	vc5->clk_pll.vc5 = vc5;
	vc5->clk_pll.hw.init = &init;
	ret = devm_clk_hw_register(&client->dev, &vc5->clk_pll.hw);
	if (ret) {
		dev_err(&client->dev, "unable to register %s\n", init.name);
		goto err_clk;
	}

	/* Register FODs */
	for (n = 0; n < vc5->chip_info->clk_fod_cnt; n++) {
		idx = vc5_map_index_to_output(vc5->chip_info->model, n);
		memset(&init, 0, sizeof(init));
		init.name = vc5_fod_names[idx];
		init.ops = &vc5_fod_ops;
		init.flags = CLK_SET_RATE_PARENT;
		init.parent_names = vc5_pll_names;
		init.num_parents = 1;
		vc5->clk_fod[n].num = idx;
		vc5->clk_fod[n].vc5 = vc5;
		vc5->clk_fod[n].hw.init = &init;
		ret = devm_clk_hw_register(&client->dev, &vc5->clk_fod[n].hw);
		if (ret) {
			dev_err(&client->dev, "unable to register %s\n",
				init.name);
			goto err_clk;
		}
	}

	/* Register MUX-connected OUT0_I2C_SELB output */
	memset(&init, 0, sizeof(init));
	init.name = vc5_clk_out_names[0];
	init.ops = &vc5_clk_out_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = vc5_mux_names;
	init.num_parents = 1;
	vc5->clk_out[0].num = idx;
	vc5->clk_out[0].vc5 = vc5;
	vc5->clk_out[0].hw.init = &init;
	ret = devm_clk_hw_register(&client->dev, &vc5->clk_out[0].hw);
	if (ret) {
		dev_err(&client->dev, "unable to register %s\n",
			init.name);
		goto err_clk;
	}

	/* Register FOD-connected OUTx outputs */
	for (n = 1; n < vc5->chip_info->clk_out_cnt; n++) {
		idx = vc5_map_index_to_output(vc5->chip_info->model, n - 1);
		parent_names[0] = vc5_fod_names[idx];
		if (n == 1)
			parent_names[1] = vc5_mux_names[0];
		else
			parent_names[1] = vc5_clk_out_names[n - 1];

		memset(&init, 0, sizeof(init));
		init.name = vc5_clk_out_names[idx + 1];
		init.ops = &vc5_clk_out_ops;
		init.flags = CLK_SET_RATE_PARENT;
		init.parent_names = parent_names;
		init.num_parents = 2;
		vc5->clk_out[n].num = idx;
		vc5->clk_out[n].vc5 = vc5;
		vc5->clk_out[n].hw.init = &init;
		ret = devm_clk_hw_register(&client->dev,
					   &vc5->clk_out[n].hw);
		if (ret) {
			dev_err(&client->dev, "unable to register %s\n",
				init.name);
			goto err_clk;
		}
	}

	ret = of_clk_add_hw_provider(client->dev.of_node, vc5_of_clk_get, vc5);
	if (ret) {
		dev_err(&client->dev, "unable to add clk provider\n");
		goto err_clk;
	}

	return 0;

err_clk:
	if (vc5->chip_info->flags & VC5_HAS_INTERNAL_XTAL)
		clk_unregister_fixed_rate(vc5->pin_xin);
	return ret;
}

static int vc5_remove(struct i2c_client *client)
{
	struct vc5_driver_data *vc5 = i2c_get_clientdata(client);

	of_clk_del_provider(client->dev.of_node);

	if (vc5->chip_info->flags & VC5_HAS_INTERNAL_XTAL)
		clk_unregister_fixed_rate(vc5->pin_xin);

	return 0;
}

static int __maybe_unused vc5_suspend(struct device *dev)
{
	struct vc5_driver_data *vc5 = dev_get_drvdata(dev);

	regcache_cache_only(vc5->regmap, true);
	regcache_mark_dirty(vc5->regmap);

	return 0;
}

static int __maybe_unused vc5_resume(struct device *dev)
{
	struct vc5_driver_data *vc5 = dev_get_drvdata(dev);
	int ret;

	regcache_cache_only(vc5->regmap, false);
	ret = regcache_sync(vc5->regmap);
	if (ret)
		dev_err(dev, "Failed to restore register map: %d\n", ret);
	return ret;
}

static const struct vc5_chip_info idt_5p49v5923_info = {
	.model = IDT_VC5_5P49V5923,
	.clk_fod_cnt = 2,
	.clk_out_cnt = 3,
	.flags = 0,
};

static const struct vc5_chip_info idt_5p49v5925_info = {
	.model = IDT_VC5_5P49V5925,
	.clk_fod_cnt = 4,
	.clk_out_cnt = 5,
	.flags = 0,
};

static const struct vc5_chip_info idt_5p49v5933_info = {
	.model = IDT_VC5_5P49V5933,
	.clk_fod_cnt = 2,
	.clk_out_cnt = 3,
	.flags = VC5_HAS_INTERNAL_XTAL,
};

static const struct vc5_chip_info idt_5p49v5935_info = {
	.model = IDT_VC5_5P49V5935,
	.clk_fod_cnt = 4,
	.clk_out_cnt = 5,
	.flags = VC5_HAS_INTERNAL_XTAL,
};

static const struct vc5_chip_info idt_5p49v6901_info = {
	.model = IDT_VC6_5P49V6901,
	.clk_fod_cnt = 4,
	.clk_out_cnt = 5,
	.flags = VC5_HAS_PFD_FREQ_DBL,
};

static const struct i2c_device_id vc5_id[] = {
	{ "5p49v5923", .driver_data = IDT_VC5_5P49V5923 },
	{ "5p49v5925", .driver_data = IDT_VC5_5P49V5925 },
	{ "5p49v5933", .driver_data = IDT_VC5_5P49V5933 },
	{ "5p49v5935", .driver_data = IDT_VC5_5P49V5935 },
	{ "5p49v6901", .driver_data = IDT_VC6_5P49V6901 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, vc5_id);

static const struct of_device_id clk_vc5_of_match[] = {
	{ .compatible = "idt,5p49v5923", .data = &idt_5p49v5923_info },
	{ .compatible = "idt,5p49v5925", .data = &idt_5p49v5925_info },
	{ .compatible = "idt,5p49v5933", .data = &idt_5p49v5933_info },
	{ .compatible = "idt,5p49v5935", .data = &idt_5p49v5935_info },
	{ .compatible = "idt,5p49v6901", .data = &idt_5p49v6901_info },
	{ },
};
MODULE_DEVICE_TABLE(of, clk_vc5_of_match);

static SIMPLE_DEV_PM_OPS(vc5_pm_ops, vc5_suspend, vc5_resume);

static struct i2c_driver vc5_driver = {
	.driver = {
		.name = "vc5",
		.pm	= &vc5_pm_ops,
		.of_match_table = clk_vc5_of_match,
	},
	.probe		= vc5_probe,
	.remove		= vc5_remove,
	.id_table	= vc5_id,
};
module_i2c_driver(vc5_driver);

MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("IDT VersaClock 5 driver");
MODULE_LICENSE("GPL");
