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

#include <subdev/clock.h>
#include <subdev/timer.h>

#ifdef __KERNEL__
#include <nouveau_platform.h>
#endif

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
	.min_vco = 1000, .max_vco = 1700,
	.min_u = 12, .max_u = 38,
	.min_m = 1, .max_m = 255,
	.min_n = 8, .max_n = 255,
	.min_pl = 1, .max_pl = 32,
};

struct gk20a_clock_priv {
	struct nouveau_clock base;
	const struct gk20a_clk_pllg_params *params;
	u32 m, n, pl;
	u32 parent_rate;
};
#define to_gk20a_clock(base) container_of(base, struct gk20a_clock_priv, base)

static void
gk20a_pllg_read_mnp(struct gk20a_clock_priv *priv)
{
	u32 val;

	val = nv_rd32(priv, GPCPLL_COEFF);
	priv->m = (val >> GPCPLL_COEFF_M_SHIFT) & MASK(GPCPLL_COEFF_M_WIDTH);
	priv->n = (val >> GPCPLL_COEFF_N_SHIFT) & MASK(GPCPLL_COEFF_N_WIDTH);
	priv->pl = (val >> GPCPLL_COEFF_P_SHIFT) & MASK(GPCPLL_COEFF_P_WIDTH);
}

static u32
gk20a_pllg_calc_rate(struct gk20a_clock_priv *priv)
{
	u32 rate;
	u32 divider;

	rate = priv->parent_rate * priv->n;
	divider = priv->m * pl_to_div[priv->pl];
	do_div(rate, divider);

	return rate / 2;
}

static int
gk20a_pllg_calc_mnp(struct gk20a_clock_priv *priv, unsigned long rate)
{
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
	ref_clk_f = priv->parent_rate / MHZ;

	max_vco_f = priv->params->max_vco;
	min_vco_f = priv->params->min_vco;
	best_m = priv->params->max_m;
	best_n = priv->params->min_n;
	best_pl = priv->params->min_pl;

	target_vco_f = target_clk_f + target_clk_f / 50;
	if (max_vco_f < target_vco_f)
		max_vco_f = target_vco_f;

	/* min_pl <= high_pl <= max_pl */
	high_pl = (max_vco_f + target_vco_f - 1) / target_vco_f;
	high_pl = min(high_pl, priv->params->max_pl);
	high_pl = max(high_pl, priv->params->min_pl);

	/* min_pl <= low_pl <= max_pl */
	low_pl = min_vco_f / target_vco_f;
	low_pl = min(low_pl, priv->params->max_pl);
	low_pl = max(low_pl, priv->params->min_pl);

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

	nv_debug(priv, "low_PL %d(div%d), high_PL %d(div%d)", low_pl,
		 pl_to_div[low_pl], high_pl, pl_to_div[high_pl]);

	/* Select lowest possible VCO */
	for (pl = low_pl; pl <= high_pl; pl++) {
		target_vco_f = target_clk_f * pl_to_div[pl];
		for (m = priv->params->min_m; m <= priv->params->max_m; m++) {
			u_f = ref_clk_f / m;

			if (u_f < priv->params->min_u)
				break;
			if (u_f > priv->params->max_u)
				continue;

			n = (target_vco_f * m) / ref_clk_f;
			n2 = ((target_vco_f * m) + (ref_clk_f - 1)) / ref_clk_f;

			if (n > priv->params->max_n)
				break;

			for (; n <= n2; n++) {
				if (n < priv->params->min_n)
					continue;
				if (n > priv->params->max_n)
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
		nv_debug(priv, "no best match for target @ %dMHz on gpc_pll",
			 target_clk_f);

	priv->m = best_m;
	priv->n = best_n;
	priv->pl = best_pl;

	target_freq = gk20a_pllg_calc_rate(priv) / MHZ;

	nv_debug(priv, "actual target freq %d MHz, M %d, N %d, PL %d(div%d)\n",
		 target_freq, priv->m, priv->n, priv->pl, pl_to_div[priv->pl]);

	return 0;
}

static int
gk20a_pllg_slide(struct gk20a_clock_priv *priv, u32 n)
{
	u32 val;
	int ramp_timeout;

	/* get old coefficients */
	val = nv_rd32(priv, GPCPLL_COEFF);
	/* do nothing if NDIV is the same */
	if (n == ((val >> GPCPLL_COEFF_N_SHIFT) & MASK(GPCPLL_COEFF_N_WIDTH)))
		return 0;

	/* setup */
	nv_mask(priv, GPCPLL_CFG2, 0xff << GPCPLL_CFG2_PLL_STEPA_SHIFT,
		0x2b << GPCPLL_CFG2_PLL_STEPA_SHIFT);
	nv_mask(priv, GPCPLL_CFG3, 0xff << GPCPLL_CFG3_PLL_STEPB_SHIFT,
		0xb << GPCPLL_CFG3_PLL_STEPB_SHIFT);

	/* pll slowdown mode */
	nv_mask(priv, GPCPLL_NDIV_SLOWDOWN,
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT),
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT));

	/* new ndiv ready for ramp */
	val = nv_rd32(priv, GPCPLL_COEFF);
	val &= ~(MASK(GPCPLL_COEFF_N_WIDTH) << GPCPLL_COEFF_N_SHIFT);
	val |= (n & MASK(GPCPLL_COEFF_N_WIDTH)) << GPCPLL_COEFF_N_SHIFT;
	udelay(1);
	nv_wr32(priv, GPCPLL_COEFF, val);

	/* dynamic ramp to new ndiv */
	val = nv_rd32(priv, GPCPLL_NDIV_SLOWDOWN);
	val |= 0x1 << GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT;
	udelay(1);
	nv_wr32(priv, GPCPLL_NDIV_SLOWDOWN, val);

	for (ramp_timeout = 500; ramp_timeout > 0; ramp_timeout--) {
		udelay(1);
		val = nv_rd32(priv, GPC_BCAST_NDIV_SLOWDOWN_DEBUG);
		if (val & GPC_BCAST_NDIV_SLOWDOWN_DEBUG_PLL_DYNRAMP_DONE_SYNCED_MASK)
			break;
	}

	/* exit slowdown mode */
	nv_mask(priv, GPCPLL_NDIV_SLOWDOWN,
		BIT(GPCPLL_NDIV_SLOWDOWN_SLOWDOWN_USING_PLL_SHIFT) |
		BIT(GPCPLL_NDIV_SLOWDOWN_EN_DYNRAMP_SHIFT), 0);
	nv_rd32(priv, GPCPLL_NDIV_SLOWDOWN);

	if (ramp_timeout <= 0) {
		nv_error(priv, "gpcpll dynamic ramp timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void
_gk20a_pllg_enable(struct gk20a_clock_priv *priv)
{
	nv_mask(priv, GPCPLL_CFG, GPCPLL_CFG_ENABLE, GPCPLL_CFG_ENABLE);
	nv_rd32(priv, GPCPLL_CFG);
}

static void
_gk20a_pllg_disable(struct gk20a_clock_priv *priv)
{
	nv_mask(priv, GPCPLL_CFG, GPCPLL_CFG_ENABLE, 0);
	nv_rd32(priv, GPCPLL_CFG);
}

static int
_gk20a_pllg_program_mnp(struct gk20a_clock_priv *priv, bool allow_slide)
{
	u32 val, cfg;
	u32 m_old, pl_old, n_lo;

	/* get old coefficients */
	val = nv_rd32(priv, GPCPLL_COEFF);
	m_old = (val >> GPCPLL_COEFF_M_SHIFT) & MASK(GPCPLL_COEFF_M_WIDTH);
	pl_old = (val >> GPCPLL_COEFF_P_SHIFT) & MASK(GPCPLL_COEFF_P_WIDTH);

	/* do NDIV slide if there is no change in M and PL */
	cfg = nv_rd32(priv, GPCPLL_CFG);
	if (allow_slide && priv->m == m_old && priv->pl == pl_old &&
	    (cfg & GPCPLL_CFG_ENABLE)) {
		return gk20a_pllg_slide(priv, priv->n);
	}

	/* slide down to NDIV_LO */
	n_lo = DIV_ROUND_UP(m_old * priv->params->min_vco,
			    priv->parent_rate / MHZ);
	if (allow_slide && (cfg & GPCPLL_CFG_ENABLE)) {
		int ret = gk20a_pllg_slide(priv, n_lo);

		if (ret)
			return ret;
	}

	/* split FO-to-bypass jump in halfs by setting out divider 1:2 */
	nv_mask(priv, GPC2CLK_OUT, GPC2CLK_OUT_VCODIV_MASK,
		0x2 << GPC2CLK_OUT_VCODIV_SHIFT);

	/* put PLL in bypass before programming it */
	val = nv_rd32(priv, SEL_VCO);
	val &= ~(BIT(SEL_VCO_GPC2CLK_OUT_SHIFT));
	udelay(2);
	nv_wr32(priv, SEL_VCO, val);

	/* get out from IDDQ */
	val = nv_rd32(priv, GPCPLL_CFG);
	if (val & GPCPLL_CFG_IDDQ) {
		val &= ~GPCPLL_CFG_IDDQ;
		nv_wr32(priv, GPCPLL_CFG, val);
		nv_rd32(priv, GPCPLL_CFG);
		udelay(2);
	}

	_gk20a_pllg_disable(priv);

	nv_debug(priv, "%s: m=%d n=%d pl=%d\n", __func__, priv->m, priv->n,
		 priv->pl);

	n_lo = DIV_ROUND_UP(priv->m * priv->params->min_vco,
			    priv->parent_rate / MHZ);
	val = priv->m << GPCPLL_COEFF_M_SHIFT;
	val |= (allow_slide ? n_lo : priv->n) << GPCPLL_COEFF_N_SHIFT;
	val |= priv->pl << GPCPLL_COEFF_P_SHIFT;
	nv_wr32(priv, GPCPLL_COEFF, val);

	_gk20a_pllg_enable(priv);

	val = nv_rd32(priv, GPCPLL_CFG);
	if (val & GPCPLL_CFG_LOCK_DET_OFF) {
		val &= ~GPCPLL_CFG_LOCK_DET_OFF;
		nv_wr32(priv, GPCPLL_CFG, val);
	}

	if (!nouveau_timer_wait_eq(priv, 300000, GPCPLL_CFG, GPCPLL_CFG_LOCK,
				   GPCPLL_CFG_LOCK)) {
		nv_error(priv, "%s: timeout waiting for pllg lock\n", __func__);
		return -ETIMEDOUT;
	}

	/* switch to VCO mode */
	nv_mask(priv, SEL_VCO, 0, BIT(SEL_VCO_GPC2CLK_OUT_SHIFT));

	/* restore out divider 1:1 */
	val = nv_rd32(priv, GPC2CLK_OUT);
	val &= ~GPC2CLK_OUT_VCODIV_MASK;
	udelay(2);
	nv_wr32(priv, GPC2CLK_OUT, val);

	/* slide up to new NDIV */
	return allow_slide ? gk20a_pllg_slide(priv, priv->n) : 0;
}

static int
gk20a_pllg_program_mnp(struct gk20a_clock_priv *priv)
{
	int err;

	err = _gk20a_pllg_program_mnp(priv, true);
	if (err)
		err = _gk20a_pllg_program_mnp(priv, false);

	return err;
}

static void
gk20a_pllg_disable(struct gk20a_clock_priv *priv)
{
	u32 val;

	/* slide to VCO min */
	val = nv_rd32(priv, GPCPLL_CFG);
	if (val & GPCPLL_CFG_ENABLE) {
		u32 coeff, m, n_lo;

		coeff = nv_rd32(priv, GPCPLL_COEFF);
		m = (coeff >> GPCPLL_COEFF_M_SHIFT) & MASK(GPCPLL_COEFF_M_WIDTH);
		n_lo = DIV_ROUND_UP(m * priv->params->min_vco,
				    priv->parent_rate / MHZ);
		gk20a_pllg_slide(priv, n_lo);
	}

	/* put PLL in bypass before disabling it */
	nv_mask(priv, SEL_VCO, BIT(SEL_VCO_GPC2CLK_OUT_SHIFT), 0);

	_gk20a_pllg_disable(priv);
}

#define GK20A_CLK_GPC_MDIV 1000

static struct nouveau_clocks
gk20a_domains[] = {
	{ nv_clk_src_crystal, 0xff },
	{ nv_clk_src_gpc, 0xff, 0, "core", GK20A_CLK_GPC_MDIV },
	{ nv_clk_src_max }
};

static struct nouveau_pstate
gk20a_pstates[] = {
	{
		.base = {
			.domain[nv_clk_src_gpc] = 72000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 108000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 180000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 252000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 324000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 396000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 468000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 540000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 612000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 648000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 684000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 708000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 756000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 804000,
		},
	},
	{
		.base = {
			.domain[nv_clk_src_gpc] = 852000,
		},
	},
};

static int
gk20a_clock_read(struct nouveau_clock *clk, enum nv_clk_src src)
{
	struct gk20a_clock_priv *priv = (void *)clk;

	switch (src) {
	case nv_clk_src_crystal:
		return nv_device(clk)->crystal;
	case nv_clk_src_gpc:
		gk20a_pllg_read_mnp(priv);
		return gk20a_pllg_calc_rate(priv) / GK20A_CLK_GPC_MDIV;
	default:
		nv_error(clk, "invalid clock source %d\n", src);
		return -EINVAL;
	}
}

static int
gk20a_clock_calc(struct nouveau_clock *clk, struct nouveau_cstate *cstate)
{
	struct gk20a_clock_priv *priv = (void *)clk;

	return gk20a_pllg_calc_mnp(priv, cstate->domain[nv_clk_src_gpc] *
					 GK20A_CLK_GPC_MDIV);
}

static int
gk20a_clock_prog(struct nouveau_clock *clk)
{
	struct gk20a_clock_priv *priv = (void *)clk;

	return gk20a_pllg_program_mnp(priv);
}

static void
gk20a_clock_tidy(struct nouveau_clock *clk)
{
}

static int
gk20a_clock_fini(struct nouveau_object *object, bool suspend)
{
	struct gk20a_clock_priv *priv = (void *)object;
	int ret;

	ret = nouveau_clock_fini(&priv->base, false);

	gk20a_pllg_disable(priv);

	return ret;
}

static int
gk20a_clock_init(struct nouveau_object *object)
{
	struct gk20a_clock_priv *priv = (void *)object;
	int ret;

	nv_mask(priv, GPC2CLK_OUT, GPC2CLK_OUT_INIT_MASK, GPC2CLK_OUT_INIT_VAL);

	ret = nouveau_clock_init(&priv->base);
	if (ret)
		return ret;

	ret = gk20a_clock_prog(&priv->base);
	if (ret) {
		nv_error(priv, "cannot initialize clock\n");
		return ret;
	}

	return 0;
}

static int
gk20a_clock_ctor(struct nouveau_object *parent,  struct nouveau_object *engine,
		 struct nouveau_oclass *oclass, void *data, u32 size,
		 struct nouveau_object **pobject)
{
	struct gk20a_clock_priv *priv;
	struct nouveau_platform_device *plat;
	int ret;
	int i;

	/* Finish initializing the pstates */
	for (i = 0; i < ARRAY_SIZE(gk20a_pstates); i++) {
		INIT_LIST_HEAD(&gk20a_pstates[i].list);
		gk20a_pstates[i].pstate = i + 1;
	}

	ret = nouveau_clock_create(parent, engine, oclass, gk20a_domains,
			gk20a_pstates, ARRAY_SIZE(gk20a_pstates), true, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->params = &gk20a_pllg_params;

	plat = nv_device_to_platform(nv_device(parent));
	priv->parent_rate = clk_get_rate(plat->gpu->clk);
	nv_info(priv, "parent clock rate: %d Mhz\n", priv->parent_rate / MHZ);

	priv->base.read = gk20a_clock_read;
	priv->base.calc = gk20a_clock_calc;
	priv->base.prog = gk20a_clock_prog;
	priv->base.tidy = gk20a_clock_tidy;

	return 0;
}

struct nouveau_oclass
gk20a_clock_oclass = {
	.handle = NV_SUBDEV(CLOCK, 0xea),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = gk20a_clock_ctor,
		.dtor = _nouveau_subdev_dtor,
		.init = gk20a_clock_init,
		.fini = gk20a_clock_fini,
	},
};
