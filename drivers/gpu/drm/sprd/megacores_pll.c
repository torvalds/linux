// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <asm/div64.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/string.h>

#include "sprd_dsi.h"

#define L						0
#define H						1
#define CLK						0
#define DATA					1
#define INFINITY				0xffffffff
#define MIN_OUTPUT_FREQ			(100)

#define AVERAGE(a, b) (min(a, b) + abs((b) - (a)) / 2)

/* sharkle */
#define VCO_BAND_LOW	750
#define VCO_BAND_MID	1100
#define VCO_BAND_HIGH	1500
#define PHY_REF_CLK	26000

static int dphy_calc_pll_param(struct dphy_pll *pll)
{
	const u32 khz = 1000;
	const u32 mhz = 1000000;
	const unsigned long long factor = 100;
	unsigned long long tmp;
	int i;

	pll->potential_fvco = pll->freq / khz;
	pll->ref_clk = PHY_REF_CLK / khz;

	for (i = 0; i < 4; ++i) {
		if (pll->potential_fvco >= VCO_BAND_LOW &&
		    pll->potential_fvco <= VCO_BAND_HIGH) {
			pll->fvco = pll->potential_fvco;
			pll->out_sel = BIT(i);
			break;
		}
		pll->potential_fvco <<= 1;
	}
	if (pll->fvco == 0)
		return -EINVAL;

	if (pll->fvco >= VCO_BAND_LOW && pll->fvco <= VCO_BAND_MID) {
		/* vco band control */
		pll->vco_band = 0x0;
		/* low pass filter control */
		pll->lpf_sel = 1;
	} else if (pll->fvco > VCO_BAND_MID && pll->fvco <= VCO_BAND_HIGH) {
		pll->vco_band = 0x1;
		pll->lpf_sel = 0;
	} else {
		return -EINVAL;
	}

	pll->nint = pll->fvco / pll->ref_clk;
	tmp = pll->fvco * factor * mhz;
	do_div(tmp, pll->ref_clk);
	tmp = tmp - pll->nint * factor * mhz;
	tmp *= BIT(20);
	do_div(tmp, 100000000);
	pll->kint = (u32)tmp;
	pll->refin = 3; /* pre-divider bypass */
	pll->sdm_en = true; /* use fraction N PLL */
	pll->fdk_s = 0x1; /* fraction */
	pll->cp_s = 0x0;
	pll->det_delay = 0x1;

	return 0;
}

static void dphy_set_pll_reg(struct dphy_pll *pll, struct regmap *regmap)
{
	u8 reg_val[9] = {0};
	int i;

	u8 reg_addr[] = {
		0x03, 0x04, 0x06, 0x08, 0x09,
		0x0a, 0x0b, 0x0e, 0x0f
	};

	reg_val[0] = 1 | (1 << 1) |  (pll->lpf_sel << 2);
	reg_val[1] = pll->div | (1 << 3) | (pll->cp_s << 5) | (pll->fdk_s << 7);
	reg_val[2] = pll->nint;
	reg_val[3] = pll->vco_band | (pll->sdm_en << 1) | (pll->refin << 2);
	reg_val[4] = pll->kint >> 12;
	reg_val[5] = pll->kint >> 4;
	reg_val[6] = pll->out_sel | ((pll->kint << 4) & 0xf);
	reg_val[7] = 1 << 4;
	reg_val[8] = pll->det_delay;

	for (i = 0; i < sizeof(reg_addr); ++i) {
		regmap_write(regmap, reg_addr[i], reg_val[i]);
		DRM_DEBUG("%02x: %02x\n", reg_addr[i], reg_val[i]);
	}
}

int dphy_pll_config(struct dsi_context *ctx)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	struct regmap *regmap = ctx->regmap;
	struct dphy_pll *pll = &ctx->pll;
	int ret;

	pll->freq = dsi->slave->hs_rate;

	/* FREQ = 26M * (NINT + KINT / 2^20) / out_sel */
	ret = dphy_calc_pll_param(pll);
	if (ret) {
		drm_err(dsi->drm, "failed to calculate dphy pll parameters\n");
		return ret;
	}
	dphy_set_pll_reg(pll, regmap);

	return 0;
}

static void dphy_set_timing_reg(struct regmap *regmap, int type, u8 val[])
{
	switch (type) {
	case REQUEST_TIME:
		regmap_write(regmap, 0x31, val[CLK]);
		regmap_write(regmap, 0x41, val[DATA]);
		regmap_write(regmap, 0x51, val[DATA]);
		regmap_write(regmap, 0x61, val[DATA]);
		regmap_write(regmap, 0x71, val[DATA]);

		regmap_write(regmap, 0x90, val[CLK]);
		regmap_write(regmap, 0xa0, val[DATA]);
		regmap_write(regmap, 0xb0, val[DATA]);
		regmap_write(regmap, 0xc0, val[DATA]);
		regmap_write(regmap, 0xd0, val[DATA]);
		break;
	case PREPARE_TIME:
		regmap_write(regmap, 0x32, val[CLK]);
		regmap_write(regmap, 0x42, val[DATA]);
		regmap_write(regmap, 0x52, val[DATA]);
		regmap_write(regmap, 0x62, val[DATA]);
		regmap_write(regmap, 0x72, val[DATA]);

		regmap_write(regmap, 0x91, val[CLK]);
		regmap_write(regmap, 0xa1, val[DATA]);
		regmap_write(regmap, 0xb1, val[DATA]);
		regmap_write(regmap, 0xc1, val[DATA]);
		regmap_write(regmap, 0xd1, val[DATA]);
		break;
	case ZERO_TIME:
		regmap_write(regmap, 0x33, val[CLK]);
		regmap_write(regmap, 0x43, val[DATA]);
		regmap_write(regmap, 0x53, val[DATA]);
		regmap_write(regmap, 0x63, val[DATA]);
		regmap_write(regmap, 0x73, val[DATA]);

		regmap_write(regmap, 0x92, val[CLK]);
		regmap_write(regmap, 0xa2, val[DATA]);
		regmap_write(regmap, 0xb2, val[DATA]);
		regmap_write(regmap, 0xc2, val[DATA]);
		regmap_write(regmap, 0xd2, val[DATA]);
		break;
	case TRAIL_TIME:
		regmap_write(regmap, 0x34, val[CLK]);
		regmap_write(regmap, 0x44, val[DATA]);
		regmap_write(regmap, 0x54, val[DATA]);
		regmap_write(regmap, 0x64, val[DATA]);
		regmap_write(regmap, 0x74, val[DATA]);

		regmap_write(regmap, 0x93, val[CLK]);
		regmap_write(regmap, 0xa3, val[DATA]);
		regmap_write(regmap, 0xb3, val[DATA]);
		regmap_write(regmap, 0xc3, val[DATA]);
		regmap_write(regmap, 0xd3, val[DATA]);
		break;
	case EXIT_TIME:
		regmap_write(regmap, 0x36, val[CLK]);
		regmap_write(regmap, 0x46, val[DATA]);
		regmap_write(regmap, 0x56, val[DATA]);
		regmap_write(regmap, 0x66, val[DATA]);
		regmap_write(regmap, 0x76, val[DATA]);

		regmap_write(regmap, 0x95, val[CLK]);
		regmap_write(regmap, 0xA5, val[DATA]);
		regmap_write(regmap, 0xB5, val[DATA]);
		regmap_write(regmap, 0xc5, val[DATA]);
		regmap_write(regmap, 0xd5, val[DATA]);
		break;
	case CLKPOST_TIME:
		regmap_write(regmap, 0x35, val[CLK]);
		regmap_write(regmap, 0x94, val[CLK]);
		break;

	/* the following just use default value */
	case SETTLE_TIME:
		fallthrough;
	case TA_GET:
		fallthrough;
	case TA_GO:
		fallthrough;
	case TA_SURE:
		fallthrough;
	default:
		break;
	}
}

void dphy_timing_config(struct dsi_context *ctx)
{
	struct regmap *regmap = ctx->regmap;
	struct dphy_pll *pll = &ctx->pll;
	const u32 factor = 2;
	const u32 scale = 100;
	u32 t_ui, t_byteck, t_half_byteck;
	u32 range[2], constant;
	u8 val[2];
	u32 tmp = 0;

	/* t_ui: 1 ui, byteck: 8 ui, half byteck: 4 ui */
	t_ui = 1000 * scale / (pll->freq / 1000);
	t_byteck = t_ui << 3;
	t_half_byteck = t_ui << 2;
	constant = t_ui << 1;

	/* REQUEST_TIME: HS T-LPX: LP-01
	 * For T-LPX, mipi spec defined min value is 50ns,
	 * but maybe it shouldn't be too small, because BTA,
	 * LP-10, LP-00, LP-01, all of this is related to T-LPX.
	 */
	range[L] = 50 * scale;
	range[H] = INFINITY;
	val[CLK] = DIV_ROUND_UP(range[L] * (factor << 1), t_byteck) - 2;
	val[DATA] = val[CLK];
	dphy_set_timing_reg(regmap, REQUEST_TIME, val);

	/* PREPARE_TIME: HS sequence: LP-00 */
	range[L] = 38 * scale;
	range[H] = 95 * scale;
	tmp = AVERAGE(range[L], range[H]);
	val[CLK] = DIV_ROUND_UP(AVERAGE(range[L], range[H]), t_half_byteck) - 1;
	range[L] = 40 * scale + 4 * t_ui;
	range[H] = 85 * scale + 6 * t_ui;
	tmp |= AVERAGE(range[L], range[H]) << 16;
	val[DATA] = DIV_ROUND_UP(AVERAGE(range[L], range[H]), t_half_byteck) - 1;
	dphy_set_timing_reg(regmap, PREPARE_TIME, val);

	/* ZERO_TIME: HS-ZERO */
	range[L] = 300 * scale;
	range[H] = INFINITY;
	val[CLK] = DIV_ROUND_UP(range[L] * factor + (tmp & 0xffff)
			- 525 * t_byteck / 100, t_byteck) - 2;
	range[L] = 145 * scale + 10 * t_ui;
	val[DATA] = DIV_ROUND_UP(range[L] * factor
			+ ((tmp >> 16) & 0xffff) - 525 * t_byteck / 100,
			t_byteck) - 2;
	dphy_set_timing_reg(regmap, ZERO_TIME, val);

	/* TRAIL_TIME: HS-TRAIL */
	range[L] = 60 * scale;
	range[H] = INFINITY;
	val[CLK] = DIV_ROUND_UP(range[L] * factor - constant, t_half_byteck);
	range[L] = max(8 * t_ui, 60 * scale + 4 * t_ui);
	val[DATA] = DIV_ROUND_UP(range[L] * 3 / 2 - constant, t_half_byteck) - 2;
	dphy_set_timing_reg(regmap, TRAIL_TIME, val);

	/* EXIT_TIME: */
	range[L] = 100 * scale;
	range[H] = INFINITY;
	val[CLK] = DIV_ROUND_UP(range[L] * factor, t_byteck) - 2;
	val[DATA] = val[CLK];
	dphy_set_timing_reg(regmap, EXIT_TIME, val);

	/* CLKPOST_TIME: */
	range[L] = 60 * scale + 52 * t_ui;
	range[H] = INFINITY;
	val[CLK] = DIV_ROUND_UP(range[L] * factor, t_byteck) - 2;
	val[DATA] = val[CLK];
	dphy_set_timing_reg(regmap, CLKPOST_TIME, val);

	/* SETTLE_TIME:
	 * This time is used for receiver. So for transmitter,
	 * it can be ignored.
	 */

	/* TA_GO:
	 * transmitter drives bridge state(LP-00) before releasing control,
	 * reg 0x1f default value: 0x04, which is good.
	 */

	/* TA_SURE:
	 * After LP-10 state and before bridge state(LP-00),
	 * reg 0x20 default value: 0x01, which is good.
	 */

	/* TA_GET:
	 * receiver drives Bridge state(LP-00) before releasing control
	 * reg 0x21 default value: 0x03, which is good.
	 */
}
