/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
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

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/pm_runtime.h>
#include <linux/mfd/syscon.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include "../../pinctrl/pinctrl-utils.h"

#define UPDATE(x, h, l)	(((x) << (l)) & GENMASK((h), (l)))
#define HIWORD_UPDATE(v, h, l)	(((v) << (l)) | (GENMASK(h, l) << 16))

#define RK3368_GRF_SOC_CON7		0x041c
#define VIDEO_PHY_TTL_MODE_ENBALE	HIWORD_UPDATE(1, 15, 15)
#define VIDEO_PHY_TTL_MODE_DISABLE	HIWORD_UPDATE(0, 15, 15)

/* Innosilicon MIPI D-PHY registers */
#define INNO_PHY_LANE_CTRL	0x00000
#define MIPI_BGPD		BIT(7)
#define PWROK_BP		BIT(1)
#define PWROK			BIT(0)
#define INNO_PHY_POWER_CTRL	0x00004
#define ANALOG_RESET_MASK	BIT(2)
#define ANALOG_RESET		BIT(2)
#define ANALOG_NORMAL		0
#define LDO_POWER_MASK		BIT(1)
#define LDO_POWER_DOWN		BIT(1)
#define LDO_POWER_ON		0
#define PLL_POWER_MASK		BIT(0)
#define PLL_POWER_DOWN		BIT(0)
#define PLL_POWER_ON		0
#define INNO_PHY_PLL_CTRL_0	0x0000c
#define FBDIV_HI_MASK		BIT(5)
#define FBDIV_HI(x)		UPDATE(x, 5, 5)
#define PREDIV_MASK		GENMASK(4, 0)
#define PREDIV(x)		UPDATE(x, 4, 0)
#define INNO_PHY_PLL_CTRL_1	0x00010
#define FBDIV_LO_MASK		GENMASK(7, 0)
#define FBDIV_LO(x)		UPDATE(x, 7, 0)
#define INNO_PHY_DIG_CTRL	0x00080
#define DIGITAL_RESET_MASK	BIT(0)
#define DIGITAL_NORMAL		BIT(0)
#define DIGITAL_RESET		0
#define INNO_PHY_MODE		0x0038c
#define TTL_MODE_ENABLE		BIT(2)
#define LVDS_MODE_ENABLE	BIT(1)
#define MIPI_MODE_ENABLE	BIT(0)
#define INNO_PHY_LVDS_CTRL	0x003ac
#define LVDS_BGPD		BIT(0)

#define INNO_CLOCK_LANE_REG_BASE	0x00100
#define INNO_DATA_LANE_0_REG_BASE	0x00180
#define INNO_DATA_LANE_1_REG_BASE	0x00200
#define INNO_DATA_LANE_2_REG_BASE	0x00280
#define INNO_DATA_LANE_3_REG_BASE	0x00300

#define T_LPX_OFFSET		0x00014
#define T_HS_PREPARE_OFFSET	0x00018
#define T_HS_ZERO_OFFSET	0x0001c
#define T_HS_TRAIL_OFFSET	0x00020
#define T_HS_EXIT_OFFSET	0x00024
#define T_CLK_POST_OFFSET	0x00028
#define T_WAKUP_H_OFFSET	0x00030
#define T_WAKUP_L_OFFSET	0x00034
#define T_CLK_PRE_OFFSET	0x00038
#define T_TA_GO_OFFSET		0x00040
#define T_TA_SURE_OFFSET	0x00044
#define T_TA_WAIT_OFFSET	0x00048

#define T_LPX_MASK		GENMASK(5, 0)
#define T_LPX(x)		UPDATE(x, 5, 0)
#define T_HS_PREPARE_MASK	GENMASK(6, 0)
#define T_HS_PREPARE(x)		UPDATE(x, 6, 0)
#define T_HS_ZERO_MASK		GENMASK(5, 0)
#define T_HS_ZERO(x)		UPDATE(x, 5, 0)
#define T_HS_TRAIL_MASK		GENMASK(6, 0)
#define T_HS_TRAIL(x)		UPDATE(x, 6, 0)
#define T_HS_EXIT_MASK		GENMASK(4, 0)
#define T_HS_EXIT(x)		UPDATE(x, 4, 0)
#define T_CLK_POST_MASK		GENMASK(3, 0)
#define T_CLK_POST(x)		UPDATE(x, 3, 0)
#define T_WAKUP_H_MASK		GENMASK(1, 0)
#define T_WAKUP_H(x)		UPDATE(x, 1, 0)
#define T_WAKUP_L_MASK		GENMASK(7, 0)
#define T_WAKUP_L(x)		UPDATE(x, 7, 0)
#define T_CLK_PRE_MASK		GENMASK(3, 0)
#define T_CLK_PRE(x)		UPDATE(x, 3, 0)
#define T_TA_GO_MASK		GENMASK(5, 0)
#define T_TA_GO(x)		UPDATE(x, 5, 0)
#define T_TA_SURE_MASK		GENMASK(5, 0)
#define T_TA_SURE(x)		UPDATE(x, 5, 0)
#define T_TA_WAIT_MASK		GENMASK(5, 0)
#define T_TA_WAIT(x)		UPDATE(x, 5, 0)

enum lane_type {
	CLOCK_LANE,
	DATA_LANE_0,
	DATA_LANE_1,
	DATA_LANE_2,
	DATA_LANE_3,
};

enum inno_video_phy_functions {
	INNO_PHY_PADCTL_FUNC_MIPI,
	INNO_PHY_PADCTL_FUNC_LVDS,
	INNO_PHY_PADCTL_FUNC_TTL,
	INNO_PHY_PADCTL_FUNC_IDLE,
};

struct mipi_dphy_timing {
	unsigned int clkmiss;
	unsigned int clkpost;
	unsigned int clkpre;
	unsigned int clkprepare;
	unsigned int clksettle;
	unsigned int clktermen;
	unsigned int clktrail;
	unsigned int clkzero;
	unsigned int dtermen;
	unsigned int eot;
	unsigned int hsexit;
	unsigned int hsprepare;
	unsigned int hszero;
	unsigned int hssettle;
	unsigned int hsskip;
	unsigned int hstrail;
	unsigned int init;
	unsigned int lpx;
	unsigned int taget;
	unsigned int tago;
	unsigned int tasure;
	unsigned int wakeup;
};

struct inno_mipi_dphy_timing {
	u8 lpx;
	u8 hs_prepare;
	u8 hs_zero;
	u8 hs_trail;
	u8 hs_exit;
	u8 clk_post;
	u8 wakup_h;
	u8 wakup_l;
	u8 clk_pre;
	u8 ta_go;
	u8 ta_sure;
	u8 ta_wait;
};

struct inno_video_phy_socdata {
	bool pinmux;
	bool has_h2p_clk;
};

struct inno_mipi_dphy {
	struct device *dev;
	struct clk *ref_clk;
	struct clk *pclk;
	struct clk *h2p_clk;
	struct regmap *regmap;
	struct reset_control *rst;
	struct regmap *grf;
	struct pinctrl_dev *pinctrl;
	struct pinctrl_desc desc;

	unsigned int lanes;
	unsigned long lane_rate;

	struct {
		struct clk_hw hw;
		u8 prediv;
		u16 fbdiv;
	} pll;

	const struct inno_video_phy_socdata *socdata;
};

static const u32 lane_reg_offset[] = {
	[CLOCK_LANE]  = INNO_CLOCK_LANE_REG_BASE,
	[DATA_LANE_0] = INNO_DATA_LANE_0_REG_BASE,
	[DATA_LANE_1] = INNO_DATA_LANE_1_REG_BASE,
	[DATA_LANE_2] = INNO_DATA_LANE_2_REG_BASE,
	[DATA_LANE_3] = INNO_DATA_LANE_3_REG_BASE,
};

#define FIXED_PARAM(_freq, _prepare, _clk_zero, _data_zero, _trail)	\
{	\
	.max_freq = _freq,	\
	.hs_prepare = _prepare,	\
	.clk_lane = {	\
		.hs_zero = _clk_zero,	\
	},	\
	.data_lane = {	\
		.hs_zero = _data_zero,	\
	},	\
	.hs_trail = _trail,	\
}

static const struct {
	unsigned long max_freq;
	u8 hs_prepare;
	struct {
		u8 hs_zero;
	} clk_lane;
	struct {
		u8 hs_zero;
	} data_lane;
	u8 hs_trail;
} fixed_param_table[] = {
	FIXED_PARAM(110,  0x20, 0x16, 0x02, 0x22),
	FIXED_PARAM(150,  0x06, 0x16, 0x03, 0x45),
	FIXED_PARAM(200,  0x18, 0x17, 0x04, 0x0b),
	FIXED_PARAM(250,  0x05, 0x17, 0x05, 0x16),
	FIXED_PARAM(300,  0x51, 0x18, 0x06, 0x2c),
	FIXED_PARAM(400,  0x64, 0x19, 0x07, 0x33),
	FIXED_PARAM(500,  0x20, 0x1b, 0x07, 0x4e),
	FIXED_PARAM(600,  0x6a, 0x1d, 0x08, 0x3a),
	FIXED_PARAM(700,  0x3e, 0x1e, 0x08, 0x6a),
	FIXED_PARAM(800,  0x21, 0x1f, 0x09, 0x29),
	FIXED_PARAM(1000, 0x09, 0x20, 0x09, 0x27),
};

static const struct pinctrl_pin_desc inno_video_phy_pins[] = {
	PINCTRL_PIN(0, "DATAP0"),	/* pin_ttl_data[0] */
	PINCTRL_PIN(1, "DATAN0"),	/* pin_ttl_data[1] */
	PINCTRL_PIN(2, "DATAP1"),	/* pin_ttl_data[2] */
	PINCTRL_PIN(3, "DATAN1"),	/* pin_ttl_data[3] */
	PINCTRL_PIN(4, "DATAP2"),	/* pin_ttl_data[4] */
	PINCTRL_PIN(5, "DATAN2"),	/* pin_ttl_data[5] */
	PINCTRL_PIN(6, "DATAP3"),	/* pin_ttl_data[6] */
	PINCTRL_PIN(7, "DATAN3"),	/* pin_ttl_data[7] */
	PINCTRL_PIN(8, "CLKP"),		/* pin_ttl_data[8] */
	PINCTRL_PIN(9, "CLKN"),		/* pin_ttl_data[9] */
};

static const char * const inno_video_phy_groups[] = {
	"video-phy-io",
};

static const unsigned int inno_video_phy_pin_numbers[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9
};

static const char * const inno_video_phy_functions[] = {
	"mipi",
	"lvds",
	"ttl",
	"idle",
};

static int
inno_video_phy_pad_config(struct inno_mipi_dphy *inno, unsigned int function)
{
	switch (function) {
	case INNO_PHY_PADCTL_FUNC_TTL:
		pm_runtime_get_sync(inno->dev);
		regmap_write(inno->grf, RK3368_GRF_SOC_CON7,
			     VIDEO_PHY_TTL_MODE_ENBALE);
		regmap_write(inno->regmap, INNO_PHY_MODE, TTL_MODE_ENABLE);
		/* Enable analog driver */
		regmap_write(inno->regmap, 0x3ac, 0xfd);
		break;
	case INNO_PHY_PADCTL_FUNC_MIPI:
		pm_runtime_get_sync(inno->dev);
		regmap_write(inno->grf, RK3368_GRF_SOC_CON7,
			     VIDEO_PHY_TTL_MODE_DISABLE);
		regmap_write(inno->regmap, INNO_PHY_MODE, MIPI_MODE_ENABLE);
		break;
	case INNO_PHY_PADCTL_FUNC_LVDS:
		pm_runtime_get_sync(inno->dev);
		regmap_write(inno->grf, RK3368_GRF_SOC_CON7,
			     VIDEO_PHY_TTL_MODE_DISABLE);
		regmap_write(inno->regmap, INNO_PHY_MODE, LVDS_MODE_ENABLE);
		/* Enable LVDS analog driver */
		regmap_write(inno->regmap, 0x3ac, 0xf8);
		break;
	case INNO_PHY_PADCTL_FUNC_IDLE:
		/* Disable analog driver */
		regmap_write(inno->regmap, 0x3ac, 0x04);
		regmap_write(inno->regmap, INNO_PHY_MODE, 0x00);
		pm_runtime_put(inno->dev);
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int inno_video_phy_get_groups_count(struct pinctrl_dev *pinctrl)
{
	return ARRAY_SIZE(inno_video_phy_groups);
}

static const char *
inno_video_phy_get_group_name(struct pinctrl_dev *pinctrl, unsigned int group)
{
	return inno_video_phy_groups[group];
}

static int
inno_video_phy_get_group_pins(struct pinctrl_dev *pinctrl, unsigned int group,
			      const unsigned int **pins, unsigned int *num_pins)
{
	*pins = inno_video_phy_pin_numbers;
	*num_pins = ARRAY_SIZE(inno_video_phy_pin_numbers);

	return 0;
}

static const struct pinctrl_ops inno_video_phy_pinctrl_ops = {
	.get_groups_count = inno_video_phy_get_groups_count,
	.get_group_name = inno_video_phy_get_group_name,
	.get_group_pins = inno_video_phy_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_group,
	.dt_free_map = pinctrl_utils_free_map,
};

static int inno_video_phy_get_functions_count(struct pinctrl_dev *pinctrl)
{
	return ARRAY_SIZE(inno_video_phy_functions);
}

static const char *
inno_video_phy_get_function_name(struct pinctrl_dev *pinctrl,
				 unsigned int function)
{
	return inno_video_phy_functions[function];
}

static int
inno_video_phy_get_function_groups(struct pinctrl_dev *pinctrl,
				   unsigned int function,
				   const char * const **groups,
				   unsigned * const num_groups)
{
	*num_groups = ARRAY_SIZE(inno_video_phy_groups);
	*groups = inno_video_phy_groups;

	return 0;
}

static int
inno_video_phy_set_mux(struct pinctrl_dev *pinctrl, unsigned int function,
		       unsigned int group)
{
	struct inno_mipi_dphy *inno = pinctrl_dev_get_drvdata(pinctrl);

	return inno_video_phy_pad_config(inno, function);
}

static const struct pinmux_ops inno_video_phy_pinmux_ops = {
	.get_functions_count = inno_video_phy_get_functions_count,
	.get_function_name = inno_video_phy_get_function_name,
	.get_function_groups = inno_video_phy_get_function_groups,
	.set_mux = inno_video_phy_set_mux,
};

static inline struct inno_mipi_dphy *hw_to_inno(struct clk_hw *hw)
{
	return container_of(hw, struct inno_mipi_dphy, pll.hw);
}

static inline void inno_mipi_dphy_reset(struct inno_mipi_dphy *inno)
{
	/* Reset analog */
	regmap_update_bits(inno->regmap, INNO_PHY_POWER_CTRL,
			   ANALOG_RESET_MASK, ANALOG_RESET);
	udelay(1);
	regmap_update_bits(inno->regmap, INNO_PHY_POWER_CTRL,
			   ANALOG_RESET_MASK, ANALOG_NORMAL);
	/* Reset digital */
	regmap_update_bits(inno->regmap, INNO_PHY_DIG_CTRL,
			   DIGITAL_RESET_MASK, DIGITAL_RESET);
	udelay(1);
	regmap_update_bits(inno->regmap, INNO_PHY_DIG_CTRL,
			   DIGITAL_RESET_MASK, DIGITAL_NORMAL);
}

static inline void inno_mipi_dphy_da_pwrok_enable(struct inno_mipi_dphy *inno)
{
	regmap_update_bits(inno->regmap, INNO_PHY_LANE_CTRL,
			   PWROK_BP | PWROK, PWROK);
}

static inline void inno_mipi_dphy_da_pwrok_disable(struct inno_mipi_dphy *inno)
{
	regmap_update_bits(inno->regmap, INNO_PHY_LANE_CTRL,
			   PWROK_BP | PWROK, PWROK_BP);
}

static inline void inno_mipi_dphy_bgpd_enable(struct inno_mipi_dphy *inno)
{
	regmap_update_bits(inno->regmap, INNO_PHY_LANE_CTRL, MIPI_BGPD, 0);
}

static inline void inno_mipi_dphy_bgpd_disable(struct inno_mipi_dphy *inno)
{
	regmap_update_bits(inno->regmap, INNO_PHY_LANE_CTRL,
			   MIPI_BGPD, MIPI_BGPD);
	regmap_update_bits(inno->regmap, INNO_PHY_LVDS_CTRL,
			   LVDS_BGPD, LVDS_BGPD);
}

static inline void inno_mipi_dphy_lane_enable(struct inno_mipi_dphy *inno)
{
	u8 map[] = {0x44, 0x4c, 0x5c, 0x7c};

	regmap_update_bits(inno->regmap, INNO_PHY_LANE_CTRL,
			   0x7c, map[inno->lanes - 1]);
}

static inline void inno_mipi_dphy_lane_disable(struct inno_mipi_dphy *inno)
{
	regmap_update_bits(inno->regmap, INNO_PHY_LANE_CTRL, 0x7c, 0x00);
}

static void inno_mipi_dphy_pll_enable(struct inno_mipi_dphy *inno)
{
	regmap_update_bits(inno->regmap, INNO_PHY_PLL_CTRL_0, FBDIV_HI_MASK |
			   PREDIV_MASK, FBDIV_HI(inno->pll.fbdiv >> 8) |
			   PREDIV(inno->pll.prediv));
	regmap_update_bits(inno->regmap, INNO_PHY_PLL_CTRL_1,
			   FBDIV_LO_MASK, FBDIV_LO(inno->pll.fbdiv));
	regmap_update_bits(inno->regmap, INNO_PHY_POWER_CTRL,
			   PLL_POWER_MASK | LDO_POWER_MASK,
			   PLL_POWER_ON | LDO_POWER_ON);
}

static void inno_mipi_dphy_pll_disable(struct inno_mipi_dphy *inno)
{
	regmap_update_bits(inno->regmap, INNO_PHY_POWER_CTRL,
			   PLL_POWER_MASK | LDO_POWER_MASK,
			   PLL_POWER_DOWN | LDO_POWER_DOWN);
}

static void mipi_dphy_timing_get_default(struct mipi_dphy_timing *timing,
					 unsigned long period)
{
	/* Global Operation Timing Parameters */
	timing->clkmiss = 0;
	timing->clkpost = 70 + 52 * period;
	timing->clkpre = 8 * period;
	timing->clkprepare = 65;
	timing->clksettle = 95;
	timing->clktermen = 0;
	timing->clktrail = 80;
	timing->clkzero = 260;
	timing->dtermen = 0;
	timing->eot = 0;
	timing->hsexit = 120;
	timing->hsprepare = 65 + 4 * period;
	timing->hszero = 145 + 6 * period;
	timing->hssettle = 85 + 6 * period;
	timing->hsskip = 40;
	timing->hstrail = max(8 * period, 60 + 4 * period);
	timing->init = 100000;
	timing->lpx = 60;
	timing->taget = 5 * timing->lpx;
	timing->tago = 4 * timing->lpx;
	timing->tasure = 2 * timing->lpx;
	timing->wakeup = 1000000;
}

static void inno_mipi_dphy_timing_update(struct inno_mipi_dphy *inno,
					 enum lane_type lane_type,
					 struct inno_mipi_dphy_timing *t)
{
	u32 base = lane_reg_offset[lane_type];

	regmap_update_bits(inno->regmap, base + T_HS_PREPARE_OFFSET,
			   T_HS_PREPARE_MASK, T_HS_PREPARE(t->hs_prepare));
	regmap_update_bits(inno->regmap, base + T_HS_ZERO_OFFSET,
			   T_HS_ZERO_MASK, T_HS_ZERO(t->hs_zero));
	regmap_update_bits(inno->regmap, base + T_HS_TRAIL_OFFSET,
			   T_HS_TRAIL_MASK, T_HS_TRAIL(t->hs_trail));
	regmap_update_bits(inno->regmap, base + T_HS_EXIT_OFFSET,
			   T_HS_EXIT_MASK, T_HS_EXIT(t->hs_exit));

	if (lane_type == CLOCK_LANE) {
		regmap_update_bits(inno->regmap, base + T_CLK_POST_OFFSET,
				   T_CLK_POST_MASK, T_CLK_POST(t->clk_post));
		regmap_update_bits(inno->regmap, base + T_CLK_PRE_OFFSET,
				   T_CLK_PRE_MASK, T_CLK_PRE(t->clk_pre));
	}

	regmap_update_bits(inno->regmap, base + T_WAKUP_H_OFFSET,
			   T_WAKUP_H_MASK, T_WAKUP_H(t->wakup_h));
	regmap_update_bits(inno->regmap, base + T_WAKUP_L_OFFSET,
			   T_WAKUP_L_MASK, T_WAKUP_L(t->wakup_l));
	regmap_update_bits(inno->regmap, base + T_LPX_OFFSET,
			   T_LPX_MASK, T_LPX(t->lpx));
	regmap_update_bits(inno->regmap, base + T_TA_GO_OFFSET,
			   T_TA_GO_MASK, T_TA_GO(t->ta_go));
	regmap_update_bits(inno->regmap, base + T_TA_SURE_OFFSET,
			   T_TA_SURE_MASK, T_TA_SURE(t->ta_sure));
	regmap_update_bits(inno->regmap, base + T_TA_WAIT_OFFSET,
			   T_TA_WAIT_MASK, T_TA_WAIT(t->ta_wait));
}

static void inno_mipi_dphy_get_fixed_param(struct inno_mipi_dphy_timing *t,
					   unsigned long freq,
					   enum lane_type lane_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fixed_param_table); i++)
		if (freq <= fixed_param_table[i].max_freq)
			break;

	if (i == ARRAY_SIZE(fixed_param_table))
		--i;

	if (lane_type == CLOCK_LANE)
		t->hs_zero = fixed_param_table[i].clk_lane.hs_zero;
	else
		t->hs_zero = fixed_param_table[i].data_lane.hs_zero;

	t->hs_prepare = fixed_param_table[i].hs_prepare;
	t->hs_trail = fixed_param_table[i].hs_trail;
}

static void inno_mipi_dphy_lane_timing_init(struct inno_mipi_dphy *inno,
					    enum lane_type lane_type)
{
	struct mipi_dphy_timing timing;
	struct inno_mipi_dphy_timing data;
	unsigned long txbyteclk, txclkesc, UI;
	unsigned int esc_clk_div;

	memset(&timing, 0, sizeof(timing));
	memset(&data, 0, sizeof(data));

	txbyteclk = inno->lane_rate / 8;
	esc_clk_div = DIV_ROUND_UP(txbyteclk, 20000000);
	txclkesc = txbyteclk / esc_clk_div;
	UI = DIV_ROUND_CLOSEST_ULL(NSEC_PER_SEC, inno->lane_rate);

	mipi_dphy_timing_get_default(&timing, UI);
	inno_mipi_dphy_get_fixed_param(&data, inno->lane_rate / USEC_PER_SEC,
				       lane_type);

	data.hs_exit = DIV_ROUND_UP(timing.hsexit * txbyteclk, NSEC_PER_SEC);
	data.clk_post = DIV_ROUND_UP(timing.clkpost * txbyteclk, NSEC_PER_SEC);
	data.clk_pre = DIV_ROUND_UP(timing.clkpre * txbyteclk, NSEC_PER_SEC);
	data.wakup_h = 0x3;
	data.wakup_l = 0xff;
	data.lpx = DIV_ROUND_UP(txbyteclk * timing.lpx, NSEC_PER_SEC);
	if (data.lpx >= 2)
		data.lpx -= 2;
	data.ta_go = DIV_ROUND_UP(timing.tago * txclkesc, NSEC_PER_SEC);
	data.ta_sure = DIV_ROUND_UP(timing.tasure * txclkesc, NSEC_PER_SEC);
	data.ta_wait = DIV_ROUND_UP(timing.taget * txclkesc, NSEC_PER_SEC);

	inno_mipi_dphy_timing_update(inno, lane_type, &data);

#define TIMING_NS(x, freq) (((x) * (DIV_ROUND_CLOSEST(NSEC_PER_SEC, freq))))
	dev_dbg(inno->dev, "hs-exit=%lu, clk-post=%lu, clk-pre=%lu, lpx=%lu\n",
		TIMING_NS(data.hs_exit, txbyteclk),
		TIMING_NS(data.clk_post, txbyteclk),
		TIMING_NS(data.clk_pre, txbyteclk),
		TIMING_NS(data.lpx + 2, txbyteclk));
	dev_dbg(inno->dev, "ta-go=%lu, ta-sure=%lu, ta-wait=%lu\n",
		TIMING_NS(data.ta_go, txclkesc),
		TIMING_NS(data.ta_sure, txclkesc),
		TIMING_NS(data.ta_wait, txclkesc));
}

static unsigned long inno_mipi_dphy_pll_rate_fixup(unsigned long fin,
						   unsigned long rate,
						   u8 *prediv, u16 *fbdiv)
{
	unsigned long best_freq = 0;
	unsigned long fout;
	u8 min_prediv, max_prediv;
	u8 _prediv, uninitialized_var(best_prediv);
	u16 _fbdiv, uninitialized_var(best_fbdiv);
	u32 min_delta = UINT_MAX;

	/*
	 * The PLL output frequency can be calculated using a simple formula:
	 * PLL_Output_Frequency = (FREF / PREDIV * FBDIV) / 2
	 * PLL_Output_Frequency: it is equal to DDR-Clock-Frequency * 2
	 */
	fout = 2 * rate;

	/* constraint: 5Mhz < Fref / prediv < 40MHz */
	min_prediv = DIV_ROUND_UP(fin, 40000000);
	max_prediv = fin / 5000000;

	for (_prediv = min_prediv; _prediv <= max_prediv; _prediv++) {
		u64 tmp;
		u32 delta;

		tmp = (u64)fout * _prediv;
		do_div(tmp, fin);
		_fbdiv = tmp;
		/*
		 * The all possible settings of feedback divider are
		 * 12, 13, 14, 16, ~ 511
		 */
		if ((_fbdiv == 15) || (_fbdiv < 12) || (_fbdiv > 511))
			continue;
		tmp = (u64)_fbdiv * fin;
		do_div(tmp, _prediv);

		delta = abs(fout - tmp);
		if (delta < min_delta) {
			best_prediv = _prediv;
			best_fbdiv = _fbdiv;
			min_delta = delta;
			best_freq = tmp;
		}
	}

	if (best_freq) {
		*prediv = best_prediv;
		*fbdiv = best_fbdiv;
	}

	return best_freq / 2;
}

static void inno_mipi_dphy_timing_init(struct inno_mipi_dphy *inno)
{
	switch (inno->lanes) {
	case 4:
		inno_mipi_dphy_lane_timing_init(inno, DATA_LANE_3);
		/* Fall through */
	case 3:
		inno_mipi_dphy_lane_timing_init(inno, DATA_LANE_2);
		/* Fall through */
	case 2:
		inno_mipi_dphy_lane_timing_init(inno, DATA_LANE_1);
		/* Fall through */
	case 1:
	default:
		inno_mipi_dphy_lane_timing_init(inno, DATA_LANE_0);
		inno_mipi_dphy_lane_timing_init(inno, CLOCK_LANE);
		break;
	}
}

static int inno_mipi_dphy_power_on(struct phy *phy)
{
	struct inno_mipi_dphy *inno = phy_get_drvdata(phy);

	clk_prepare_enable(inno->h2p_clk);
	clk_prepare_enable(inno->pclk);
	pm_runtime_get_sync(inno->dev);
	inno_mipi_dphy_bgpd_enable(inno);
	inno_mipi_dphy_da_pwrok_enable(inno);
	inno_mipi_dphy_pll_enable(inno);
	inno_mipi_dphy_lane_enable(inno);
	inno_mipi_dphy_reset(inno);
	inno_mipi_dphy_timing_init(inno);
	udelay(1);

	return 0;
}

static int inno_mipi_dphy_power_off(struct phy *phy)
{
	struct inno_mipi_dphy *inno = phy_get_drvdata(phy);

	inno_mipi_dphy_lane_disable(inno);
	inno_mipi_dphy_pll_disable(inno);
	inno_mipi_dphy_da_pwrok_disable(inno);
	inno_mipi_dphy_bgpd_disable(inno);
	pm_runtime_put(inno->dev);
	clk_disable_unprepare(inno->pclk);
	clk_disable_unprepare(inno->h2p_clk);

	return 0;
}

static const struct phy_ops inno_mipi_dphy_ops = {
	.power_on  = inno_mipi_dphy_power_on,
	.power_off = inno_mipi_dphy_power_off,
	.owner	   = THIS_MODULE,
};

static long inno_mipi_dphy_pll_clk_round_rate(struct clk_hw *hw,
					      unsigned long rate,
					      unsigned long *prate)
{
	struct inno_mipi_dphy *inno = hw_to_inno(hw);
	unsigned long fin = *prate;
	unsigned long fout;
	u16 fbdiv;
	u8 prediv;

	fout = inno_mipi_dphy_pll_rate_fixup(fin, rate, &prediv, &fbdiv);

	dev_dbg(inno->dev, "%s: fin=%lu, req_rate=%lu\n",
		__func__, *prate, rate);
	dev_dbg(inno->dev, "%s: fout=%lu, prediv=%u, fbdiv=%u\n",
		__func__, fout, prediv, fbdiv);

	inno->pll.prediv = prediv;
	inno->pll.fbdiv = fbdiv;

	return fout;
}

static int inno_mipi_dphy_pll_clk_set_rate(struct clk_hw *hw,
					   unsigned long rate,
					   unsigned long parent_rate)
{
	struct inno_mipi_dphy *inno = hw_to_inno(hw);

	dev_dbg(inno->dev, "%s: rate: %lu Hz\n", __func__, rate);

	inno->lane_rate = rate;

	return 0;
}

static unsigned long
inno_mipi_dphy_pll_clk_recalc_rate(struct clk_hw *hw, unsigned long prate)
{
	struct inno_mipi_dphy *inno = hw_to_inno(hw);

	dev_dbg(inno->dev, "%s: rate: %lu Hz\n", __func__, inno->lane_rate);

	return inno->lane_rate;
}

static const struct clk_ops inno_mipi_dphy_pll_clk_ops = {
	.round_rate = inno_mipi_dphy_pll_clk_round_rate,
	.set_rate = inno_mipi_dphy_pll_clk_set_rate,
	.recalc_rate = inno_mipi_dphy_pll_clk_recalc_rate,
};

static int inno_mipi_dphy_pll_register(struct inno_mipi_dphy *inno)
{
	struct device *dev = inno->dev;
	struct device_node *np = dev->of_node;
	struct clk *clk;
	const char *parent_name;
	struct clk_init_data init;
	int ret;

	parent_name = __clk_get_name(inno->ref_clk);

	ret = of_property_read_string(np, "clock-output-names", &init.name);
	if (ret < 0) {
		dev_err(dev, "Missing clock-output-names property: %d\n", ret);
		return ret;
	}

	init.ops = &inno_mipi_dphy_pll_clk_ops;
	init.parent_names = (const char * const *)&parent_name;
	init.num_parents = 1;
	init.flags = 0;

	inno->pll.hw.init = &init;
	clk = devm_clk_register(dev, &inno->pll.hw);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(dev, "failed to register PLL: %d\n", ret);
		return ret;
	}

	return of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static void inno_mipi_dphy_pll_unregister(struct inno_mipi_dphy *inno)
{
	of_clk_del_provider(inno->dev->of_node);
}

static int inno_mipi_dphy_parse_dt(struct device_node *np,
				   struct inno_mipi_dphy *inno)
{
	if (of_property_read_u32(np, "inno,lanes", &inno->lanes))
		inno->lanes = 4;

	return 0;
}

static const struct regmap_config inno_mipi_dphy_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x3ac,
};

static int inno_mipi_dphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct inno_mipi_dphy *inno;
	struct phy_provider *phy_provider;
	struct phy *phy;
	struct resource *res;
	void __iomem *regs;
	int ret;

	inno = devm_kzalloc(dev, sizeof(*inno), GFP_KERNEL);
	if (!inno)
		return -ENOMEM;

	inno->dev = dev;
	inno->socdata = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, inno);

	ret = inno_mipi_dphy_parse_dt(dev->of_node, inno);
	if (ret) {
		dev_err(dev, "failed to parse DT\n");
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	inno->regmap = devm_regmap_init_mmio(dev, regs,
					     &inno_mipi_dphy_regmap_config);
	if (IS_ERR(inno->regmap)) {
		ret = PTR_ERR(inno->regmap);
		dev_err(dev, "failed to init regmap: %d\n", ret);
		return ret;
	}

	inno->ref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(inno->ref_clk)) {
		dev_err(dev, "failed to get reference clock\n");
		return PTR_ERR(inno->ref_clk);
	}

	inno->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(inno->pclk)) {
		dev_err(dev, "failed to get pclk\n");
		return PTR_ERR(inno->pclk);
	}

	if (inno->socdata->has_h2p_clk) {
		inno->h2p_clk = devm_clk_get(dev, "h2p");
		if (IS_ERR(inno->h2p_clk)) {
			dev_err(dev, "failed to get h2p clock\n");
			return PTR_ERR(inno->h2p_clk);
		}
	}

	inno->rst = devm_reset_control_get(dev, "apb");
	if (IS_ERR(inno->rst)) {
		dev_err(dev, "failed to get system reset control\n");
		return PTR_ERR(inno->rst);
	}

	inno->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						    "rockchip,grf");
	if (IS_ERR(inno->grf)) {
		dev_err(dev, "failed to get grf regmap\n");
		return PTR_ERR(inno->grf);
	}

	phy = devm_phy_create(dev, NULL, &inno_mipi_dphy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create MIPI D-PHY\n");
		return PTR_ERR(phy);
	}

	phy_set_drvdata(phy, inno);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	ret = inno_mipi_dphy_pll_register(inno);
	if (ret)
		return ret;

	if (inno->socdata->pinmux) {
		inno->desc.name = dev_name(dev);
		inno->desc.pins = inno_video_phy_pins;
		inno->desc.npins = ARRAY_SIZE(inno_video_phy_pins);
		inno->desc.pctlops = &inno_video_phy_pinctrl_ops;
		inno->desc.pmxops = &inno_video_phy_pinmux_ops;
		inno->desc.owner = THIS_MODULE;

		inno->pinctrl = pinctrl_register(&inno->desc, dev, inno);
		if (IS_ERR(inno->pinctrl)) {
			dev_err(dev, "failed to register pincontrol\n");
			return PTR_ERR(inno->pinctrl);
		}
	}

	pm_runtime_enable(dev);

	return 0;
}

static int inno_mipi_dphy_remove(struct platform_device *pdev)
{
	struct inno_mipi_dphy *inno = platform_get_drvdata(pdev);

	if (inno->socdata->pinmux)
		pinctrl_unregister(inno->pinctrl);

	inno_mipi_dphy_pll_unregister(inno);
	pm_runtime_disable(inno->dev);

	return 0;
}

static const struct inno_video_phy_socdata px30_socdata = {
	.has_h2p_clk = false,
	.pinmux = false,
};

static const struct inno_video_phy_socdata rk3128_socdata = {
	.has_h2p_clk = true,
	.pinmux = false,
};

static const struct inno_video_phy_socdata rk3366_socdata = {
	.has_h2p_clk = false,
	.pinmux = false,
};

static const struct inno_video_phy_socdata rk3368_socdata = {
	.has_h2p_clk = false,
	.pinmux = true,
};

static const struct of_device_id inno_mipi_dphy_of_match[] = {
	{ .compatible = "rockchip,px30-mipi-dphy", .data = &px30_socdata },
	{ .compatible = "rockchip,rk3128-mipi-dphy", .data = &rk3128_socdata },
	{ .compatible = "rockchip,rk3366-mipi-dphy", .data = &rk3366_socdata },
	{ .compatible = "rockchip,rk3368-mipi-dphy", .data = &rk3368_socdata },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, inno_mipi_dphy_of_match);

static struct platform_driver inno_mipi_dphy_driver = {
	.driver = {
		.name = "inno-mipi-dphy",
		.of_match_table	= of_match_ptr(inno_mipi_dphy_of_match),
	},
	.probe	= inno_mipi_dphy_probe,
	.remove = inno_mipi_dphy_remove,
};

module_platform_driver(inno_mipi_dphy_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Innosilicon MIPI D-PHY Driver");
MODULE_LICENSE("GPL v2");
