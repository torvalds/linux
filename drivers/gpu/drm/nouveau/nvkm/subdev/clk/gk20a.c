/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION. All rights reserved.
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
#include "priv.h"
#include "gk20a.h"

#include <core/tegra.h>
#include <subdev/timer.h>

static const u8 _pl_to_div[] = {
/* PL:   0, 1, 2, 3, 4, 5, 6,  7,  8,  9, 10, 11, 12, 13, 14 */
/* p: */ 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 12, 16, 20, 24, 32,
};

static u32 pl_to_div(u32 pl)
{
	if (pl >= ARRAY_SIZE(_pl_to_div))
		return 1;

	return _pl_to_div[pl];
}

static u32 div_to_pl(u32 div)
{
	u32 pl;

	for (pl = 0; pl < ARRAY_SIZE(_pl_to_div) - 1; pl++) {
		if (_pl_to_div[pl] >= div)
			return pl;
	}

	return ARRAY_SIZE(_pl_to_div) - 1;
}

static const struct gk20a_clk_pllg_params gk20a_pllg_params = {
	.min_vco = 1000000, .max_vco = 2064000,
	.min_u = 12000, .max_u = 38000,
	.min_m = 1, .max_m = 255,
	.min_n = 8, .max_n = 255,
	.min_pl = 1, .max_pl = 32,
};

void
gk20a_pllg_read_mnp(struct gk20a_clk *clk, struct gk20a_pll *pll)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 val;

	val = nvkm_rd32(device, GPCPLL_COEFF);
	pll->m = (val >> GPCPLL_COEFF_M_SHIFT) & MASK(GPCPLL_COEFF_M_WIDTH);
	pll->n = (val >> GPCPLL_COEFF_N_SHIFT) & MASK(GPCPLL_COEFF_N_WIDTH);
	pll->pl = (val >> GPCPLL_COEFF_P_SHIFT) & MASK(GPCPLL_COEFF_P_WIDTH);
}

void
gk20a_pllg_write_mnp(struct gk20a_clk *clk, const struct gk20a_pll *pll)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 val;

	val = (pll->m & MASK(GPCPLL_COEFF_M_WIDTH)) << GPCPLL_COEFF_M_SHIFT;
	val |= (pll->n & MASK(GPCPLL_COEFF_N_WIDTH)) << GPCPLL_COEFF_N_SHIFT;
	val |= (pll->pl & MASK(GPCPLL_COEFF_P_WIDTH)) << GPCPLL_COEFF_P_SHIFT;
	nvkm_wr32(device, GPCPLL_COEFF, val);
}

u32
gk20a_pllg_calc_rate(struct gk20a_clk *clk, struct gk20a_pll *pll)
{
	u32 rate;
	u32 divider;

	rate = clk->parent_rate * pll->n;
	divider = pll->m * clk->pl_to_div(pll->pl);

	return rate / divider / 2;
}

int
gk20a_pllg_calc_mnp(struct gk20a_clk *clk, unsigned long rate,
		    struct gk20a_pll *pll)
{
	struct nvkm_subdev *subdev = &clk->base.subdev;
	u32 target_clk_f, ref_clk_f, target_freq;
	u32 min_vco_f, max_vco_f;
	u32 low_pl, high_pl, best_pl;
	u32 target_vco_f;
	u32 best_m, best_n;
	u32 best_delta = ~0;
	u32 pl;

	target_clk_f = rate * 2 / KHZ;
	ref_clk_f = clk->parent_rate / KHZ;

	target_vco_f = target_clk_f + target_clk_f / 50;
	max_vco_f = max(clk->params->max_vco, target_vco_f);
	min_vco_f = clk->params->min_vco;
	best_m = clk->params->max_m;
	best_n = clk->params->min_n;
	best_pl = clk->params->min_pl;

	/* min_pl <= high_pl <= max_pl */
	high_pl = (max_vco_f + target_vco_f - 1) / target_vco_f;
	high_pl = min(high_pl, clk->params->max_pl);
	high_pl = max(high_pl, clk->params->min_pl);
	high_pl = clk->div_to_pl(high_pl);

	/* min_pl <= low_pl <= max_pl */
	low_pl = min_vco_f / target_vco_f;
	low_pl = min(low_pl, clk->params->max_pl);
	low_pl = max(low_pl, clk->params->min_pl);
	low_pl = clk->div_to_pl(low_pl);

	nvkm_debug(subdev, "low_PL %d(div%d), high_PL %d(div%d)", low_pl,
		   clk->pl_to_div(low_pl), high_pl, clk->pl_to_div(high_pl));

	/* Select lowest possible VCO */
	for (pl = low_pl; pl <= high_pl; pl++) {
		u32 m, n, n2;

		target_vco_f = target_clk_f * clk->pl_to_div(pl);

		for (m = clk->params->min_m; m <= clk->params->max_m; m++) {
			u32 u_f = ref_clk_f / m;

			if (u_f < clk->params->min_u)
				break;
			if (u_f > clk->params->max_u)
				continue;

			n = (target_vco_f * m) / ref_clk_f;
			n2 = ((target_vco_f * m) + (ref_clk_f - 1)) / ref_clk_f;

			if (n > clk->params->max_n)
				break;

			for (; n <= n2; n++) {
				u32 vco_f;

				if (n < clk->params->min_n)
					continue;
				if (n > clk->params->max_n)
					break;

				vco_f = ref_clk_f * n / m;

				if (vco_f >= min_vco_f && vco_f <= max_vco_f) {
					u32 delta, lwv;

					lwv = (vco_f + (clk->pl_to_div(pl) / 2))
						/ clk->pl_to_div(pl);
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
			   target_clk_f / KHZ);

	pll->m = best_m;
	pll->n = best_n;
	pll->pl = best_pl;

	target_freq = gk20a_pllg_calc_rate(clk, pll);

	nvkm_debug(subdev,
		   "actual target freq %d KHz, M %d, N %d, PL %d(div%d)\n",
		   target_freq / KHZ, pll->m, pll->n, pll->pl,
		   clk->pl_to_div(pll->pl));
	return 0;
}

static int
gk20a_pllg_slide(struct gk20a_clk *clk, u32 n)
{
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	struct gk20a_pll pll;
	int ret = 0;

	/* get old coefficients */
	gk20a_pllg_read_mnp(clk, &pll);
	/* do nothing if NDIV is the same */
	if (n == pll.n)
		return 0;

	/* pll slowdown mode */
	nvkm_mask(device, GPCPLL_NDIV_SLOWDOWN,
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT),
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT));

	/* new ndiv ready for ramp */
	pll.n = n;
	udelay(1);
	gk20a_pllg_write_mnp(clk, &pll);

	/* dynamic ramp to new ndiv */
	udelay(1);
	nvkm_mask(device, GPCPLL_NDIV_SLOWDOWN,
		  BIT(GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT),
		  BIT(GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT));

	/* wait for ramping to complete */
	if (nvkm_wait_usec(device, 500, GPC_BCAST_NDIV_SLOWDOWN_DEBUG,
		GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_MASK,
		GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_MASK) < 0)
		ret = -ETIMEDOUT;

	/* exit slowdown mode */
	nvkm_mask(device, GPCPLL_NDIV_SLOWDOWN,
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT) |
		BIT(GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT), 0);
	nvkm_rd32(device, GPCPLL_NDIV_SLOWDOWN);

	return ret;
}

static int
gk20a_pllg_enable(struct gk20a_clk *clk)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 val;

	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_ENABLE, GPCPLL_CFG_ENABLE);
	nvkm_rd32(device, GPCPLL_CFG);

	/* enable lock detection */
	val = nvkm_rd32(device, GPCPLL_CFG);
	if (val & GPCPLL_CFG_LOCK_DET_OFF) {
		val &= ~GPCPLL_CFG_LOCK_DET_OFF;
		nvkm_wr32(device, GPCPLL_CFG, val);
	}

	/* wait for lock */
	if (nvkm_wait_usec(device, 300, GPCPLL_CFG, GPCPLL_CFG_LOCK,
			   GPCPLL_CFG_LOCK) < 0)
		return -ETIMEDOUT;

	/* switch to VCO mode */
	nvkm_mask(device, SEL_VCO, BIT(SEL_VCO_GPC2CLK_OUT_SHIFT),
		BIT(SEL_VCO_GPC2CLK_OUT_SHIFT));

	return 0;
}

static void
gk20a_pllg_disable(struct gk20a_clk *clk)
{
	struct nvkm_device *device = clk->base.subdev.device;

	/* put PLL in bypass before disabling it */
	nvkm_mask(device, SEL_VCO, BIT(SEL_VCO_GPC2CLK_OUT_SHIFT), 0);

	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_ENABLE, 0);
	nvkm_rd32(device, GPCPLL_CFG);
}

static int
gk20a_pllg_program_mnp(struct gk20a_clk *clk, const struct gk20a_pll *pll)
{
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	struct gk20a_pll cur_pll;
	int ret;

	gk20a_pllg_read_mnp(clk, &cur_pll);

	/* split VCO-to-bypass jump in half by setting out divider 1:2 */
	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_VCODIV_MASK,
		  GPC2CLK_OUT_VCODIV2 << GPC2CLK_OUT_VCODIV_SHIFT);
	/* Intentional 2nd write to assure linear divider operation */
	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_VCODIV_MASK,
		  GPC2CLK_OUT_VCODIV2 << GPC2CLK_OUT_VCODIV_SHIFT);
	nvkm_rd32(device, GPC2CLK_OUT);
	udelay(2);

	gk20a_pllg_disable(clk);

	gk20a_pllg_write_mnp(clk, pll);

	ret = gk20a_pllg_enable(clk);
	if (ret)
		return ret;

	/* restore out divider 1:1 */
	udelay(2);
	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_VCODIV_MASK,
		  GPC2CLK_OUT_VCODIV1 << GPC2CLK_OUT_VCODIV_SHIFT);
	/* Intentional 2nd write to assure linear divider operation */
	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_VCODIV_MASK,
		  GPC2CLK_OUT_VCODIV1 << GPC2CLK_OUT_VCODIV_SHIFT);
	nvkm_rd32(device, GPC2CLK_OUT);

	return 0;
}

static int
gk20a_pllg_program_mnp_slide(struct gk20a_clk *clk, const struct gk20a_pll *pll)
{
	struct gk20a_pll cur_pll;
	int ret;

	if (gk20a_pllg_is_enabled(clk)) {
		gk20a_pllg_read_mnp(clk, &cur_pll);

		/* just do NDIV slide if there is no change to M and PL */
		if (pll->m == cur_pll.m && pll->pl == cur_pll.pl)
			return gk20a_pllg_slide(clk, pll->n);

		/* slide down to current NDIV_LO */
		cur_pll.n = gk20a_pllg_n_lo(clk, &cur_pll);
		ret = gk20a_pllg_slide(clk, cur_pll.n);
		if (ret)
			return ret;
	}

	/* program MNP with the new clock parameters and new NDIV_LO */
	cur_pll = *pll;
	cur_pll.n = gk20a_pllg_n_lo(clk, &cur_pll);
	ret = gk20a_pllg_program_mnp(clk, &cur_pll);
	if (ret)
		return ret;

	/* slide up to new NDIV */
	return gk20a_pllg_slide(clk, pll->n);
}

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

int
gk20a_clk_read(struct nvkm_clk *base, enum nv_clk_src src)
{
	struct gk20a_clk *clk = gk20a_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	struct gk20a_pll pll;

	switch (src) {
	case nv_clk_src_crystal:
		return device->crystal;
	case nv_clk_src_gpc:
		gk20a_pllg_read_mnp(clk, &pll);
		return gk20a_pllg_calc_rate(clk, &pll) / GK20A_CLK_GPC_MDIV;
	default:
		nvkm_error(subdev, "invalid clock source %d\n", src);
		return -EINVAL;
	}
}

int
gk20a_clk_calc(struct nvkm_clk *base, struct nvkm_cstate *cstate)
{
	struct gk20a_clk *clk = gk20a_clk(base);

	return gk20a_pllg_calc_mnp(clk, cstate->domain[nv_clk_src_gpc] *
					 GK20A_CLK_GPC_MDIV, &clk->pll);
}

int
gk20a_clk_prog(struct nvkm_clk *base)
{
	struct gk20a_clk *clk = gk20a_clk(base);
	int ret;

	ret = gk20a_pllg_program_mnp_slide(clk, &clk->pll);
	if (ret)
		ret = gk20a_pllg_program_mnp(clk, &clk->pll);

	return ret;
}

void
gk20a_clk_tidy(struct nvkm_clk *base)
{
}

int
gk20a_clk_setup_slide(struct gk20a_clk *clk)
{
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	u32 step_a, step_b;

	switch (clk->parent_rate) {
	case 12000000:
	case 12800000:
	case 13000000:
		step_a = 0x2b;
		step_b = 0x0b;
		break;
	case 19200000:
		step_a = 0x12;
		step_b = 0x08;
		break;
	case 38400000:
		step_a = 0x04;
		step_b = 0x05;
		break;
	default:
		nvkm_error(subdev, "invalid parent clock rate %u KHz",
			   clk->parent_rate / KHZ);
		return -EINVAL;
	}

	nvkm_mask(device, GPCPLL_CFG2, 0xff << GPCPLL_CFG2_PLL_STEPA_SHIFT,
		step_a << GPCPLL_CFG2_PLL_STEPA_SHIFT);
	nvkm_mask(device, GPCPLL_CFG3, 0xff << GPCPLL_CFG3_PLL_STEPB_SHIFT,
		step_b << GPCPLL_CFG3_PLL_STEPB_SHIFT);

	return 0;
}

void
gk20a_clk_fini(struct nvkm_clk *base)
{
	struct nvkm_device *device = base->subdev.device;
	struct gk20a_clk *clk = gk20a_clk(base);

	/* slide to VCO min */
	if (gk20a_pllg_is_enabled(clk)) {
		struct gk20a_pll pll;
		u32 n_lo;

		gk20a_pllg_read_mnp(clk, &pll);
		n_lo = gk20a_pllg_n_lo(clk, &pll);
		gk20a_pllg_slide(clk, n_lo);
	}

	gk20a_pllg_disable(clk);

	/* set IDDQ */
	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_IDDQ, 1);
}

static int
gk20a_clk_init(struct nvkm_clk *base)
{
	struct gk20a_clk *clk = gk20a_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;
	int ret;

	/* get out from IDDQ */
	nvkm_mask(device, GPCPLL_CFG, GPCPLL_CFG_IDDQ, 0);
	nvkm_rd32(device, GPCPLL_CFG);
	udelay(5);

	nvkm_mask(device, GPC2CLK_OUT, GPC2CLK_OUT_INIT_MASK,
		  GPC2CLK_OUT_INIT_VAL);

	ret = gk20a_clk_setup_slide(clk);
	if (ret)
		return ret;

	/* Start with lowest frequency */
	base->func->calc(base, &base->func->pstates[0].base);
	ret = base->func->prog(&clk->base);
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
gk20a_clk_ctor(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       const struct nvkm_clk_func *func, const struct gk20a_clk_pllg_params *params,
	       struct gk20a_clk *clk)
{
	struct nvkm_device_tegra *tdev = device->func->tegra(device);
	int ret;
	int i;

	/* Finish initializing the pstates */
	for (i = 0; i < func->nr_pstates; i++) {
		INIT_LIST_HEAD(&func->pstates[i].list);
		func->pstates[i].pstate = i + 1;
	}

	clk->params = params;
	clk->parent_rate = clk_get_rate(tdev->clk);

	ret = nvkm_clk_ctor(func, device, type, inst, true, &clk->base);
	if (ret)
		return ret;

	nvkm_debug(&clk->base.subdev, "parent clock rate: %d Khz\n",
		   clk->parent_rate / KHZ);

	return 0;
}

int
gk20a_clk_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_clk **pclk)
{
	struct gk20a_clk *clk;
	int ret;

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return -ENOMEM;
	*pclk = &clk->base;

	ret = gk20a_clk_ctor(device, type, inst, &gk20a_clk, &gk20a_pllg_params, clk);

	clk->pl_to_div = pl_to_div;
	clk->div_to_pl = div_to_pl;
	return ret;
}
