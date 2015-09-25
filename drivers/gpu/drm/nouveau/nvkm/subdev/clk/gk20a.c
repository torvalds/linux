/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Shamelessly ripped off from ChromeOS's gk20a/clk_pllg.c
 *
 */
#define gk20a_clk(p) container_of((p), struct gk20a_clk, base)
#include "priv.h"

#include <core/tegra.h>
#include <subdev/timer.h>

#define MHZ (1000 * 1000)

#define MASK(w)	((1 << w) - 1)

#define SYS_GPCPLL_CFG_BASE			0x00137000
#define GPC_BCASE_GPCPLL_CFG_BASE		0x00132800

#define GPCPLL_CFG		(SYS_GPCPLL_CFG_BASE + 0)
#define GPCPLL_CFG_ENABLE	BIT(0)
#define GPCPLL_CFG_IDDQ		BIT(1)
#define GPCPLL_CFG_LOCK_DET_OFF	BIT(4)
#define GPCPLL_CFG_LOCK		BIT(17)

#define GPCPLL_COEFF		(SYS_GPCPLL_CFG_BASE + 4)
#define GPCPLL_COEFF_M_SHIFT	0
#define GPCPLL_COEFF_M_WIDTH	8
#define GPCPLL_COEFF_N_SHIFT	8
#define GPCPLL_COEFF_N_WIDTH	8
#define GPCPLL_COEFF_P_SHIFT	16
#define GPCPLL_COEFF_P_WIDTH	6

#define GPCPLL_CFG2			(SYS_GPCPLL_CFG_BASE + 0xc)
#define GPCPLL_CFG2_SETUP2_SHIFT	16
#define GPCPLL_CFG2_PLL_STEPA_SHIFT	24

#define GPCPLL_CFG3			(SYS_GPCPLL_CFG_BASE + 0x18)
#define GPCPLL_CFG3_PLL_STEPB_SHIFT	16

#define GPCPLL_NDIV_SLOWDOWN			(SYS_GPCPLL_CFG_BASE + 0x1c)
#define GPCPLL_NDIV_SLOWDOWN_NDIV_LO_SHIFT	0
#define GPCPLL_NDIV_SLOWDOWN_NDIV_MID_SHIFT	8
#define GPCPLL_NDIV_SLOWDOWN_STEP_SIZE_LO2MID_SHIFT	16
#define GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT	22
#define GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT	31

#define SEL_VCO				(SYS_GPCPLL_CFG_BASE + 0x100)
#define SEL_VCO_GPC2CLK_OUT_SHIFT	0

#define GPC2CLK_OUT			(SYS_GPCPLL_CFG_BASE + 0x250)
#define GPC2CLK_OUT_SDIV14_INDIV4_WIDTH	1
#define GPC2CLK_OUT_SDIV14_INDIV4_SHIFT	31
#define GPC2CLK_OUT_SDIV14_INDIV4_MODE	1
#define GPC2CLK_OUT_VCODIV_WIDTH	6
#define GPC2CLK_OUT_VCODIV_SHIFT	8
#define GPC2CLK_OUT_VCODIV1		0
#define GPC2CLK_OUT_VCODIV_MASK		(MASK(GPC2CLK_OUT_VCODIV_WIDTH) << \
					GPC2CLK_OUT_VCODIV_SHIFT)
#define	GPC2CLK_OUT_BYPDIV_WIDTH	6
#define GPC2CLK_OUT_BYPDIV_SHIFT	0
#define GPC2CLK_OUT_BYPDIV31		0x3c
#define GPC2CLK_OUT_INIT_MASK	((MASK(GPC2CLK_OUT_SDIV14_INDIV4_WIDTH) << \
		GPC2CLK_OUT_SDIV14_INDIV4_SHIFT)\
		| (MASK(GPC2CLK_OUT_VCODIV_WIDTH) << GPC2CLK_OUT_VCODIV_SHIFT)\
		| (MASK(GPC2CLK_OUT_BYPDIV_WIDTH) << GPC2CLK_OUT_BYPDIV_SHIFT))
#define GPC2CLK_OUT_INIT_VAL	((GPC2CLK_OUT_SDIV14_INDIV4_MODE << \
		GPC2CLK_OUT_SDIV14_INDIV4_SHIFT) \
		| (GPC2CLK_OUT_VCODIV1 << GPC2CLK_OUT_VCODIV_SHIFT) \
		| (GPC2CLK_OUT_BYPDIV31 << GPC2CLK_OUT_BYPDIV_SHIFT))

#define GPC_BCAST_NDIV_SLOWDOWN_DEBUG	(GPC_BCASE_GPCPLL_CFG_BASE + 0xa0)
#define GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_SHIFT	24
#define GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_MASK \
	    (0x1 << GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_SHIFT)

static const u8 pl_to_div[] = {
/* PL:   0, 1, 2, 3, 4, 5, 6,  7,  8,  9, 10, 11, 12, 13, 14 */
/* p: */ 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 12, 16, 20, 24, 32,
};

/* All frequencies in Mhz */
struct gk20a_clk_pllg_params {
	u32 min_vco, max_vco;
	u32 min_u, max_u;
	u32 min_m, max_m;
	u32 min_n, max_n;
	u32 min_pl, max_pl;
};

static const struct gk20a_clk_pllg_params gk20a_pllg_params = {
	.min_vco = 1000, .max_vco = 2064,
	.min_u = 12, .max_u = 38,
	.min_m = 1, .max_m = 255,
	.min_n = 8, .max_n = 255,
	.min_pl = 1, .max_pl = 32,
};

struct gk20a_clk {
	struct nvkm_clk base;
	const struct gk20a_clk_pllg_params *params;
	u32 m, n, pl;
	u32 parent_rate;
};

static void
gk20a_pllg_read_mnp(struct gk20a_clk *clk)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 val;

	val = nvkm_rd32(device, GPCPLL_COEFF);
	clk->m = (val >> GPCPLL_COEFF_M_SHIFT) & MASK(GPCPLL_COEFF_M_WIDTH);
	clk->n = (val >> GPCPLL_COEFF_N_SHIFT) & MASK(GPCPLL_COEFF_N_WIDTH);
	clk->pl = (val >> GPCPLL_COEFF_P_SHIFT) & MASK(GPCPLL_COEFF_P_WIDTH);
}

static u32
gk20a_pllg_calc_rate(struct gk20a_clk *clk)
{
	u32 rate;
	u32 divider;

	rate = clk->parent_rate * clk->n;
	divider = clk->m * pl_to_div[clk->pl];
	do_div(rate, divider);

	return rate / 2;
}

static int
gk20a_pllg_calc_mnp(struct gk20a_clk *clk, unsigned long rate)
{
	struct nvkm_subdev *subdev = &clk->base.subdev;
	u32 target_clk_f, ref_clk_f, target_freq;
	u32 min_vco_f, max_vco_f;
	u32 low_pl, high_pl, best_pl;
	u32 target_vco_f, vco_f;
	u32 best_m, best_n;
	u32 u_f;
	u32 m, n, n2;
	u32 delta, lwv, best_delta = ~0;
	u32 pl;

	target_clk_f = rate * 2 / MHZ;
	ref_clk_f = clk->parent_rate / MHZ;

	max_vco_f = clk->params->max_vco;
	min_vco_f = clk->params->min_vco;
	best_m = clk->params->max_m;
	best_n = clk->params->min_n;
	best_pl = clk->params->min_pl;

	target_vco_f = target_clk_f + target_clk_f / 50;
	if (max_vco_f < target_vco_f)
		max_vco_f = target_vco_f;

	/* min_pl <= high_pl <= max_pl */
	high_pl = (max_vco_f + target_vco_f - 1) / target_vco_f;
	high_pl = min(high_pl, clk->params->max_pl);
	high_pl = max(high_pl, clk->params->min_pl);

	/* min_pl <= low_pl <= max_pl */
	low_pl = min_vco_f / target_vco_f;
	low_pl = min(low_pl, clk->params->max_pl);
	low_pl = max(low_pl, clk->params->min_pl);

	/* Find Indices of high_pl and low_pl */
	for (pl = 0; pl < ARRAY_SIZE(pl_to_div) - 1; pl++) {
		if (pl_to_div[pl] >= low_pl) {
			low_pl = pl;
			break;
		}
	}
	for (pl = 0; pl < ARRAY_SIZE(pl_to_div) - 1; pl++) {
		if (pl_to_div[pl] >= high_pl) {
			high_pl = pl;
			break;
		}
	}

	nvkm_debug(subdev, "low_PL %d(div%d), high_PL %d(div%d)", low_pl,
		   pl_to_div[low_pl], high_pl, pl_to_div[high_pl]);

	/* Select lowest possible VCO */
	for (pl = low_pl; pl <= high_pl; pl++) {
		target_vco_f = target_clk_f * pl_to_div[pl];
		for (m = clk->params->min_m; m <= clk->params->max_m; m++) {
			u_f = ref_clk_f / m;

			if (u_f < clk->params->min_u)
				break;
			if (u_f > clk->params->max_u)
				continue;

			n = (target_vco_f * m) / ref_clk_f;
			n2 = ((target_vco_f * m) + (ref_clk_f - 1)) / ref_clk_f;

			if (n > clk->params->max_n)
				break;

			for (; n <= n2; n++) {
				if (n < clk->params->min_n)
					continue;
				if (n > clk->params->max_n)
					break;

				vco_f = ref_clk_f * n / m;

				if (vco_f >= min_vco_f && vco_f <= max_vco_f) {
					lwv = (vco_f + (pl_to_div[pl] / 2))
						/ pl_to_div[pl];
					delta = abs(lwv - target_clk_f);

					if (delta < best_delta) {
						best_delta = delta;
						best_m = m;
						best_n = n;
						best_pl = pl;

						if (best_delta == 0)
							goto found_match;
					}
				}
			}
		}
	}

found_match:
	WARN_ON(best_delta == ~0);

	if (best_delta != 0)
		nvkm_debug(subdev,
			   "no best match for target @ %dMHz on gpc_pll",
			   target_clk_f);

	clk->m = best_m;
	clk->n = best_n;
	clk->pl = best_pl;

	target_freq = gk20a_pllg_calc_rate(clk) / MHZ;

	nvkm_debug(subdev,
		   "actual target freq %d MHz, M %d, N %d, PL %d(div%d)\n",
		   target_freq, clk->m, clk->n, clk->pl, pl_to_div[clk->pl]);
	return 0;
}

static int
gk20a_pllg_slide(struct gk20a_clk *clk, u32 n)
{
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	u32 val;
	int ramp_timeout;

	/* get old coefficients */
	val = nvkm_rd32(device, GPCPLL_COEFF);
	/* do nothing if NDIV is the same */
	if (n == ((val >> GPCPLL_COEFF_N_SHIFT) & MASK(GPCPLL_COEFF_N_WIDTH)))
		return 0;

	/* setup */
	nvkm_mask(device, GPCPLL_CFG2, 0xff << GPCPLL_CFG2_PLL_STEPA_SHIFT,
		0x2b << GPCPLL_CFG2_PLL_STEPA_SHIFT);
	nvkm_mask(device, GPCPLL_CFG3, 0xff << GPCPLL_CFG3_PLL_STEPB_SHIFT,
		0xb << GPCPLL_CFG3_PLL_STEPB_SHIFT);

	/* pll slowdown mode */
	nvkm_mask(device, GPCPLL_NDIV_SLOWDOWN,
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT),
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT));

	/* new ndiv ready for ramp */
	val = nvkm_rd32(device, GPCPLL_COEFF);
	val &= ~(MASK(GPCPLL_COEFF_N_WIDTH) << GPCPLL_COEFF_N_SHIFT);
	val |= (n & MASK(GPCPLL_COEFF_N_WIDTH)) << GPCPLL_COEFF_N_SHIFT;
	udelay(1);
	nvkm_wr32(device, GPCPLL_COEFF, val);

	/* dynamic ramp to new ndiv */
	val = nvkm_rd32(device, GPCPLL_NDIV_SLOWDOWN);
	val |= 0x1 << GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT;
	udelay(1);
	nvkm_wr32(device, GPCPLL_NDIV_SLOWDOWN, val);

	for (ramp_timeout = 500; ramp_timeout > 0; ramp_timeout--) {
		udelay(1);
		val = nvkm_rd32(device, GPC_BCAST_NDIV_SLOWDOWN_DEBUG);
		if (val & GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_MASK)
			break;
	}

	/* exit slowdown mode */
	nvkm_mask(device, GPCPLL_NDIV_SLOWDOWN,
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT) |
		BIT(GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT), 0);
	nvkm_rd32(device, GPCPLL_NDIV_SLOWDOWN);

	if (ramp_timeout <= 0) {
		nvkm_error(subdev, "gpcpll dynamic ramp timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void
_gk20a_pllg_enable(struct gk20a_clk *clk)
{
	struct nvkm_device *device = clk->base.subdev.device;
	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_ENABLE, GPCPLL_CFG_ENABLE);
	nvkm_rd32(device, GPCPLL_CFG);
}

static void
_gk20a_pllg_disable(struct gk20a_clk *clk)
{
	struct nvkm_device *device = clk->base.subdev.device;
	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_ENABLE, 0);
	nvkm_rd32(device, GPCPLL_CFG);
}

static int
_gk20a_pllg_program_mnp(struct gk20a_clk *clk, bool allow_slide)
{
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	u32 val, cfg;
	u32 m_old, pl_old, n_lo;

	/* get old coefficients */
	val = nvkm_rd32(device, GPCPLL_COEFF);
	m_old = (val >> GPCPLL_COEFF_M_SHIFT) & MASK(GPCPLL_COEFF_M_WIDTH);
	pl_old = (val >> GPCPLL_COEFF_P_SHIFT) & MASK(GPCPLL_COEFF_P_WIDTH);

	/* do NDIV slide if there is no change in M and PL */
	cfg = nvkm_rd32(device, GPCPLL_CFG);
	if (allow_slide && clk->m == m_old && clk->pl == pl_old &&
	    (cfg & GPCPLL_CFG_ENABLE)) {
		return gk20a_pllg_slide(clk, clk->n);
	}

	/* slide down to NDIV_LO */
	n_lo = DIV_ROUND_UP(m_old * clk->params->min_vco,
			    clk->parent_rate / MHZ);
	if (allow_slide && (cfg & GPCPLL_CFG_ENABLE)) {
		int ret = gk20a_pllg_slide(clk, n_lo);

		if (ret)
			return ret;
	}

	/* split FO-to-bypass jump in halfs by setting out divider 1:2 */
	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_VCODIV_MASK,
		0x2 << GPC2CLK_OUT_VCODIV_SHIFT);

	/* put PLL in bypass before programming it */
	val = nvkm_rd32(device, SEL_VCO);
	val &= ~(BIT(SEL_VCO_GPC2CLK_OUT_SHIFT));
	udelay(2);
	nvkm_wr32(device, SEL_VCO, val);

	/* get out from IDDQ */
	val = nvkm_rd32(device, GPCPLL_CFG);
	if (val & GPCPLL_CFG_IDDQ) {
		val &= ~GPCPLL_CFG_IDDQ;
		nvkm_wr32(device, GPCPLL_CFG, val);
		nvkm_rd32(device, GPCPLL_CFG);
		udelay(2);
	}

	_gk20a_pllg_disable(clk);

	nvkm_debug(subdev, "%s: m=%d n=%d pl=%d\n", __func__,
		   clk->m, clk->n, clk->pl);

	n_lo = DIV_ROUND_UP(clk->m * clk->params->min_vco,
			    clk->parent_rate / MHZ);
	val = clk->m << GPCPLL_COEFF_M_SHIFT;
	val |= (allow_slide ? n_lo : clk->n) << GPCPLL_COEFF_N_SHIFT;
	val |= clk->pl << GPCPLL_COEFF_P_SHIFT;
	nvkm_wr32(device, GPCPLL_COEFF, val);

	_gk20a_pllg_enable(clk);

	val = nvkm_rd32(device, GPCPLL_CFG);
	if (val & GPCPLL_CFG_LOCK_DET_OFF) {
		val &= ~GPCPLL_CFG_LOCK_DET_OFF;
		nvkm_wr32(device, GPCPLL_CFG, val);
	}

	if (nvkm_usec(device, 300,
		if (nvkm_rd32(device, GPCPLL_CFG) & GPCPLL_CFG_LOCK)
			break;
	) < 0)
		return -ETIMEDOUT;

	/* switch to VCO mode */
	nvkm_mask(device, SEL_VCO, 0, BIT(SEL_VCO_GPC2CLK_OUT_SHIFT));

	/* restore out divider 1:1 */
	val = nvkm_rd32(device, GPC2CLK_OUT);
	val &= ~GPC2CLK_OUT_VCODIV_MASK;
	udelay(2);
	nvkm_wr32(device, GPC2CLK_OUT, val);

	/* slide up to new NDIV */
	return allow_slide ? gk20a_pllg_slide(clk, clk->n) : 0;
}

static int
gk20a_pllg_program_mnp(struct gk20a_clk *clk)
{
	int err;

	err = _gk20a_pllg_program_mnp(clk, true);
	if (err)
		err = _gk20a_pllg_program_mnp(clk, false);

	return err;
}

static void
gk20a_pllg_disable(struct gk20a_clk *clk)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 val;

	/* slide to VCO min */
	val = nvkm_rd32(device, GPCPLL_CFG);
	if (val & GPCPLL_CFG_ENABLE) {
		u32 coeff, m, n_lo;

		coeff = nvkm_rd32(device, GPCPLL_COEFF);
		m = (coeff >> GPCPLL_COEFF_M_SHIFT) & MASK(GPCPLL_COEFF_M_WIDTH);
		n_lo = DIV_ROUND_UP(m * clk->params->min_vco,
				    clk->parent_rate / MHZ);
		gk20a_pllg_slide(clk, n_lo);
	}

	/* put PLL in bypass before disabling it */
	nvkm_mask(device, SEL_VCO, BIT(SEL_VCO_GPC2CLK_OUT_SHIFT), 0);

	_gk20a_pllg_disable(clk);
}

#define GK20A_CLK_GPC_MDIV 1000

static struct nvkm_pstate
gk20a_pstates[] = {
	{
		.base = {
			.domain[nv_clk_src_gpc] = 72000,
			.voltage = 0,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 108000,
			.voltage = 1,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 180000,
			.voltage = 2,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 252000,
			.voltage = 3,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 324000,
			.voltage = 4,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 396000,
			.voltage = 5,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 468000,
			.voltage = 6,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 540000,
			.voltage = 7,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 612000,
			.voltage = 8,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 648000,
			.voltage = 9,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 684000,
			.voltage = 10,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 708000,
			.voltage = 11,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 756000,
			.voltage = 12,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 804000,
			.voltage = 13,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 852000,
			.voltage = 14,
		},
	},
};

static int
gk20a_clk_read(struct nvkm_clk *base, enum nv_clk_src src)
{
	struct gk20a_clk *clk = gk20a_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;

	switch (src) {
	case nv_clk_src_crystal:
		return device->crystal;
	case nv_clk_src_gpc:
		gk20a_pllg_read_mnp(clk);
		return gk20a_pllg_calc_rate(clk) / GK20A_CLK_GPC_MDIV;
	default:
		nvkm_error(subdev, "invalid clock source %d\n", src);
		return -EINVAL;
	}
}

static int
gk20a_clk_calc(struct nvkm_clk *base, struct nvkm_cstate *cstate)
{
	struct gk20a_clk *clk = gk20a_clk(base);

	return gk20a_pllg_calc_mnp(clk, cstate->domain[nv_clk_src_gpc] *
					 GK20A_CLK_GPC_MDIV);
}

static int
gk20a_clk_prog(struct nvkm_clk *base)
{
	struct gk20a_clk *clk = gk20a_clk(base);

	return gk20a_pllg_program_mnp(clk);
}

static void
gk20a_clk_tidy(struct nvkm_clk *base)
{
}

static void
gk20a_clk_fini(struct nvkm_clk *base)
{
	struct gk20a_clk *clk = gk20a_clk(base);
	gk20a_pllg_disable(clk);
}

static int
gk20a_clk_init(struct nvkm_clk *base)
{
	struct gk20a_clk *clk = gk20a_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	int ret;

	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_INIT_MASK, GPC2CLK_OUT_INIT_VAL);

	ret = gk20a_clk_prog(&clk->base);
	if (ret) {
		nvkm_error(subdev, "cannot initialize clock\n");
		return ret;
	}

	return 0;
}

static const struct nvkm_clk_func
gk20a_clk = {
	.init = gk20a_clk_init,
	.fini = gk20a_clk_fini,
	.read = gk20a_clk_read,
	.calc = gk20a_clk_calc,
	.prog = gk20a_clk_prog,
	.tidy = gk20a_clk_tidy,
	.pstates = gk20a_pstates,
	.nr_pstates = ARRAY_SIZE(gk20a_pstates),
	.domains = {
		{ nv_clk_src_crystal, 0xff },
		{ nv_clk_src_gpc, 0xff, 0, "core", GK20A_CLK_GPC_MDIV },
		{ nv_clk_src_max }
	}
};

int
gk20a_clk_new(struct nvkm_device *device, int index, struct nvkm_clk **pclk)
{
	struct nvkm_device_tegra *tdev = device->func->tegra(device);
	struct gk20a_clk *clk;
	int ret, i;

	if (!(clk = kzalloc(sizeof(*clk), GFP_KERNEL)))
		return -ENOMEM;
	*pclk = &clk->base;

	/* Finish initializing the pstates */
	for (i = 0; i < ARRAY_SIZE(gk20a_pstates); i++) {
		INIT_LIST_HEAD(&gk20a_pstates[i].list);
		gk20a_pstates[i].pstate = i + 1;
	}

	clk->params = &gk20a_pllg_params;
	clk->parent_rate = clk_get_rate(tdev->clk);

	ret = nvkm_clk_ctor(&gk20a_clk, device, index, true, &clk->base);
	nvkm_info(&clk->base.subdev, "parent clock rate: %d Mhz\n",
		  clk->parent_rate / MHZ);
	return ret;
}
