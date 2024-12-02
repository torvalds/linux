// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/string_helpers.h>

#include "i915_reg.h"
#include "intel_atomic.h"
#include "intel_crtc.h"
#include "intel_cx0_phy.h"
#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_dpio_phy.h"
#include "intel_dpll.h"
#include "intel_lvds.h"
#include "intel_lvds_regs.h"
#include "intel_panel.h"
#include "intel_pps.h"
#include "intel_snps_phy.h"
#include "vlv_dpio_phy_regs.h"
#include "vlv_sideband.h"

struct intel_dpll_funcs {
	int (*crtc_compute_clock)(struct intel_atomic_state *state,
				  struct intel_crtc *crtc);
	int (*crtc_get_shared_dpll)(struct intel_atomic_state *state,
				    struct intel_crtc *crtc);
};

struct intel_limit {
	struct {
		int min, max;
	} dot, vco, n, m, m1, m2, p, p1;

	struct {
		int dot_limit;
		int p2_slow, p2_fast;
	} p2;
};
static const struct intel_limit intel_limits_i8xx_dac = {
	.dot = { .min = 25000, .max = 350000 },
	.vco = { .min = 908000, .max = 1512000 },
	.n = { .min = 2, .max = 16 },
	.m = { .min = 96, .max = 140 },
	.m1 = { .min = 18, .max = 26 },
	.m2 = { .min = 6, .max = 16 },
	.p = { .min = 4, .max = 128 },
	.p1 = { .min = 2, .max = 33 },
	.p2 = { .dot_limit = 165000,
		.p2_slow = 4, .p2_fast = 2 },
};

static const struct intel_limit intel_limits_i8xx_dvo = {
	.dot = { .min = 25000, .max = 350000 },
	.vco = { .min = 908000, .max = 1512000 },
	.n = { .min = 2, .max = 16 },
	.m = { .min = 96, .max = 140 },
	.m1 = { .min = 18, .max = 26 },
	.m2 = { .min = 6, .max = 16 },
	.p = { .min = 4, .max = 128 },
	.p1 = { .min = 2, .max = 33 },
	.p2 = { .dot_limit = 165000,
		.p2_slow = 4, .p2_fast = 4 },
};

static const struct intel_limit intel_limits_i8xx_lvds = {
	.dot = { .min = 25000, .max = 350000 },
	.vco = { .min = 908000, .max = 1512000 },
	.n = { .min = 2, .max = 16 },
	.m = { .min = 96, .max = 140 },
	.m1 = { .min = 18, .max = 26 },
	.m2 = { .min = 6, .max = 16 },
	.p = { .min = 4, .max = 128 },
	.p1 = { .min = 1, .max = 6 },
	.p2 = { .dot_limit = 165000,
		.p2_slow = 14, .p2_fast = 7 },
};

static const struct intel_limit intel_limits_i9xx_sdvo = {
	.dot = { .min = 20000, .max = 400000 },
	.vco = { .min = 1400000, .max = 2800000 },
	.n = { .min = 1, .max = 6 },
	.m = { .min = 70, .max = 120 },
	.m1 = { .min = 8, .max = 18 },
	.m2 = { .min = 3, .max = 7 },
	.p = { .min = 5, .max = 80 },
	.p1 = { .min = 1, .max = 8 },
	.p2 = { .dot_limit = 200000,
		.p2_slow = 10, .p2_fast = 5 },
};

static const struct intel_limit intel_limits_i9xx_lvds = {
	.dot = { .min = 20000, .max = 400000 },
	.vco = { .min = 1400000, .max = 2800000 },
	.n = { .min = 1, .max = 6 },
	.m = { .min = 70, .max = 120 },
	.m1 = { .min = 8, .max = 18 },
	.m2 = { .min = 3, .max = 7 },
	.p = { .min = 7, .max = 98 },
	.p1 = { .min = 1, .max = 8 },
	.p2 = { .dot_limit = 112000,
		.p2_slow = 14, .p2_fast = 7 },
};


static const struct intel_limit intel_limits_g4x_sdvo = {
	.dot = { .min = 25000, .max = 270000 },
	.vco = { .min = 1750000, .max = 3500000},
	.n = { .min = 1, .max = 4 },
	.m = { .min = 104, .max = 138 },
	.m1 = { .min = 17, .max = 23 },
	.m2 = { .min = 5, .max = 11 },
	.p = { .min = 10, .max = 30 },
	.p1 = { .min = 1, .max = 3},
	.p2 = { .dot_limit = 270000,
		.p2_slow = 10,
		.p2_fast = 10
	},
};

static const struct intel_limit intel_limits_g4x_hdmi = {
	.dot = { .min = 22000, .max = 400000 },
	.vco = { .min = 1750000, .max = 3500000},
	.n = { .min = 1, .max = 4 },
	.m = { .min = 104, .max = 138 },
	.m1 = { .min = 16, .max = 23 },
	.m2 = { .min = 5, .max = 11 },
	.p = { .min = 5, .max = 80 },
	.p1 = { .min = 1, .max = 8},
	.p2 = { .dot_limit = 165000,
		.p2_slow = 10, .p2_fast = 5 },
};

static const struct intel_limit intel_limits_g4x_single_channel_lvds = {
	.dot = { .min = 20000, .max = 115000 },
	.vco = { .min = 1750000, .max = 3500000 },
	.n = { .min = 1, .max = 3 },
	.m = { .min = 104, .max = 138 },
	.m1 = { .min = 17, .max = 23 },
	.m2 = { .min = 5, .max = 11 },
	.p = { .min = 28, .max = 112 },
	.p1 = { .min = 2, .max = 8 },
	.p2 = { .dot_limit = 0,
		.p2_slow = 14, .p2_fast = 14
	},
};

static const struct intel_limit intel_limits_g4x_dual_channel_lvds = {
	.dot = { .min = 80000, .max = 224000 },
	.vco = { .min = 1750000, .max = 3500000 },
	.n = { .min = 1, .max = 3 },
	.m = { .min = 104, .max = 138 },
	.m1 = { .min = 17, .max = 23 },
	.m2 = { .min = 5, .max = 11 },
	.p = { .min = 14, .max = 42 },
	.p1 = { .min = 2, .max = 6 },
	.p2 = { .dot_limit = 0,
		.p2_slow = 7, .p2_fast = 7
	},
};

static const struct intel_limit pnv_limits_sdvo = {
	.dot = { .min = 20000, .max = 400000},
	.vco = { .min = 1700000, .max = 3500000 },
	/* Pineview's Ncounter is a ring counter */
	.n = { .min = 3, .max = 6 },
	.m = { .min = 2, .max = 256 },
	/* Pineview only has one combined m divider, which we treat as m2. */
	.m1 = { .min = 0, .max = 0 },
	.m2 = { .min = 0, .max = 254 },
	.p = { .min = 5, .max = 80 },
	.p1 = { .min = 1, .max = 8 },
	.p2 = { .dot_limit = 200000,
		.p2_slow = 10, .p2_fast = 5 },
};

static const struct intel_limit pnv_limits_lvds = {
	.dot = { .min = 20000, .max = 400000 },
	.vco = { .min = 1700000, .max = 3500000 },
	.n = { .min = 3, .max = 6 },
	.m = { .min = 2, .max = 256 },
	.m1 = { .min = 0, .max = 0 },
	.m2 = { .min = 0, .max = 254 },
	.p = { .min = 7, .max = 112 },
	.p1 = { .min = 1, .max = 8 },
	.p2 = { .dot_limit = 112000,
		.p2_slow = 14, .p2_fast = 14 },
};

/* Ironlake / Sandybridge
 *
 * We calculate clock using (register_value + 2) for N/M1/M2, so here
 * the range value for them is (actual_value - 2).
 */
static const struct intel_limit ilk_limits_dac = {
	.dot = { .min = 25000, .max = 350000 },
	.vco = { .min = 1760000, .max = 3510000 },
	.n = { .min = 1, .max = 5 },
	.m = { .min = 79, .max = 127 },
	.m1 = { .min = 12, .max = 22 },
	.m2 = { .min = 5, .max = 9 },
	.p = { .min = 5, .max = 80 },
	.p1 = { .min = 1, .max = 8 },
	.p2 = { .dot_limit = 225000,
		.p2_slow = 10, .p2_fast = 5 },
};

static const struct intel_limit ilk_limits_single_lvds = {
	.dot = { .min = 25000, .max = 350000 },
	.vco = { .min = 1760000, .max = 3510000 },
	.n = { .min = 1, .max = 3 },
	.m = { .min = 79, .max = 118 },
	.m1 = { .min = 12, .max = 22 },
	.m2 = { .min = 5, .max = 9 },
	.p = { .min = 28, .max = 112 },
	.p1 = { .min = 2, .max = 8 },
	.p2 = { .dot_limit = 225000,
		.p2_slow = 14, .p2_fast = 14 },
};

static const struct intel_limit ilk_limits_dual_lvds = {
	.dot = { .min = 25000, .max = 350000 },
	.vco = { .min = 1760000, .max = 3510000 },
	.n = { .min = 1, .max = 3 },
	.m = { .min = 79, .max = 127 },
	.m1 = { .min = 12, .max = 22 },
	.m2 = { .min = 5, .max = 9 },
	.p = { .min = 14, .max = 56 },
	.p1 = { .min = 2, .max = 8 },
	.p2 = { .dot_limit = 225000,
		.p2_slow = 7, .p2_fast = 7 },
};

/* LVDS 100mhz refclk limits. */
static const struct intel_limit ilk_limits_single_lvds_100m = {
	.dot = { .min = 25000, .max = 350000 },
	.vco = { .min = 1760000, .max = 3510000 },
	.n = { .min = 1, .max = 2 },
	.m = { .min = 79, .max = 126 },
	.m1 = { .min = 12, .max = 22 },
	.m2 = { .min = 5, .max = 9 },
	.p = { .min = 28, .max = 112 },
	.p1 = { .min = 2, .max = 8 },
	.p2 = { .dot_limit = 225000,
		.p2_slow = 14, .p2_fast = 14 },
};

static const struct intel_limit ilk_limits_dual_lvds_100m = {
	.dot = { .min = 25000, .max = 350000 },
	.vco = { .min = 1760000, .max = 3510000 },
	.n = { .min = 1, .max = 3 },
	.m = { .min = 79, .max = 126 },
	.m1 = { .min = 12, .max = 22 },
	.m2 = { .min = 5, .max = 9 },
	.p = { .min = 14, .max = 42 },
	.p1 = { .min = 2, .max = 6 },
	.p2 = { .dot_limit = 225000,
		.p2_slow = 7, .p2_fast = 7 },
};

static const struct intel_limit intel_limits_vlv = {
	 /*
	  * These are based on the data rate limits (measured in fast clocks)
	  * since those are the strictest limits we have. The fast
	  * clock and actual rate limits are more relaxed, so checking
	  * them would make no difference.
	  */
	.dot = { .min = 25000, .max = 270000 },
	.vco = { .min = 4000000, .max = 6000000 },
	.n = { .min = 1, .max = 7 },
	.m1 = { .min = 2, .max = 3 },
	.m2 = { .min = 11, .max = 156 },
	.p1 = { .min = 2, .max = 3 },
	.p2 = { .p2_slow = 2, .p2_fast = 20 }, /* slow=min, fast=max */
};

static const struct intel_limit intel_limits_chv = {
	/*
	 * These are based on the data rate limits (measured in fast clocks)
	 * since those are the strictest limits we have.  The fast
	 * clock and actual rate limits are more relaxed, so checking
	 * them would make no difference.
	 */
	.dot = { .min = 25000, .max = 540000 },
	.vco = { .min = 4800000, .max = 6480000 },
	.n = { .min = 1, .max = 1 },
	.m1 = { .min = 2, .max = 2 },
	.m2 = { .min = 24 << 22, .max = 175 << 22 },
	.p1 = { .min = 2, .max = 4 },
	.p2 = {	.p2_slow = 1, .p2_fast = 14 },
};

static const struct intel_limit intel_limits_bxt = {
	.dot = { .min = 25000, .max = 594000 },
	.vco = { .min = 4800000, .max = 6700000 },
	.n = { .min = 1, .max = 1 },
	.m1 = { .min = 2, .max = 2 },
	/* FIXME: find real m2 limits */
	.m2 = { .min = 2 << 22, .max = 255 << 22 },
	.p1 = { .min = 2, .max = 4 },
	.p2 = { .p2_slow = 1, .p2_fast = 20 },
};

/*
 * Platform specific helpers to calculate the port PLL loopback- (clock.m),
 * and post-divider (clock.p) values, pre- (clock.vco) and post-divided fast
 * (clock.dot) clock rates. This fast dot clock is fed to the port's IO logic.
 * The helpers' return value is the rate of the clock that is fed to the
 * display engine's pipe which can be the above fast dot clock rate or a
 * divided-down version of it.
 */
/* m1 is reserved as 0 in Pineview, n is a ring counter */
static int pnv_calc_dpll_params(int refclk, struct dpll *clock)
{
	clock->m = clock->m2 + 2;
	clock->p = clock->p1 * clock->p2;

	clock->vco = clock->n == 0 ? 0 :
		DIV_ROUND_CLOSEST(refclk * clock->m, clock->n);
	clock->dot = clock->p == 0 ? 0 :
		DIV_ROUND_CLOSEST(clock->vco, clock->p);

	return clock->dot;
}

static u32 i9xx_dpll_compute_m(const struct dpll *dpll)
{
	return 5 * (dpll->m1 + 2) + (dpll->m2 + 2);
}

int i9xx_calc_dpll_params(int refclk, struct dpll *clock)
{
	clock->m = i9xx_dpll_compute_m(clock);
	clock->p = clock->p1 * clock->p2;

	clock->vco = clock->n + 2 == 0 ? 0 :
		DIV_ROUND_CLOSEST(refclk * clock->m, clock->n + 2);
	clock->dot = clock->p == 0 ? 0 :
		DIV_ROUND_CLOSEST(clock->vco, clock->p);

	return clock->dot;
}

static int vlv_calc_dpll_params(int refclk, struct dpll *clock)
{
	clock->m = clock->m1 * clock->m2;
	clock->p = clock->p1 * clock->p2 * 5;

	clock->vco = clock->n == 0 ? 0 :
		DIV_ROUND_CLOSEST(refclk * clock->m, clock->n);
	clock->dot = clock->p == 0 ? 0 :
		DIV_ROUND_CLOSEST(clock->vco, clock->p);

	return clock->dot;
}

int chv_calc_dpll_params(int refclk, struct dpll *clock)
{
	clock->m = clock->m1 * clock->m2;
	clock->p = clock->p1 * clock->p2 * 5;

	clock->vco = clock->n == 0 ? 0 :
		DIV_ROUND_CLOSEST_ULL(mul_u32_u32(refclk, clock->m), clock->n << 22);
	clock->dot = clock->p == 0 ? 0 :
		DIV_ROUND_CLOSEST(clock->vco, clock->p);

	return clock->dot;
}

static int i9xx_pll_refclk(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	const struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;

	if ((hw_state->dpll & PLL_REF_INPUT_MASK) == PLLB_REF_INPUT_SPREADSPECTRUMIN)
		return i915->display.vbt.lvds_ssc_freq;
	else if (HAS_PCH_SPLIT(i915))
		return 120000;
	else if (DISPLAY_VER(i915) != 2)
		return 96000;
	else
		return 48000;
}

void i9xx_dpll_get_hw_state(struct intel_crtc *crtc,
			    struct intel_dpll_hw_state *dpll_hw_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct i9xx_dpll_hw_state *hw_state = &dpll_hw_state->i9xx;

	if (DISPLAY_VER(dev_priv) >= 4) {
		u32 tmp;

		/* No way to read it out on pipes B and C */
		if (IS_CHERRYVIEW(dev_priv) && crtc->pipe != PIPE_A)
			tmp = dev_priv->display.state.chv_dpll_md[crtc->pipe];
		else
			tmp = intel_de_read(dev_priv,
					    DPLL_MD(dev_priv, crtc->pipe));

		hw_state->dpll_md = tmp;
	}

	hw_state->dpll = intel_de_read(dev_priv, DPLL(dev_priv, crtc->pipe));

	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv)) {
		hw_state->fp0 = intel_de_read(dev_priv, FP0(crtc->pipe));
		hw_state->fp1 = intel_de_read(dev_priv, FP1(crtc->pipe));
	} else {
		/* Mask out read-only status bits. */
		hw_state->dpll &= ~(DPLL_LOCK_VLV |
				    DPLL_PORTC_READY_MASK |
				    DPLL_PORTB_READY_MASK);
	}
}

/* Returns the clock of the currently programmed mode of the given pipe. */
void i9xx_crtc_clock_get(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;
	u32 dpll = hw_state->dpll;
	u32 fp;
	struct dpll clock;
	int port_clock;
	int refclk = i9xx_pll_refclk(crtc_state);

	if ((dpll & DISPLAY_RATE_SELECT_FPA1) == 0)
		fp = hw_state->fp0;
	else
		fp = hw_state->fp1;

	clock.m1 = (fp & FP_M1_DIV_MASK) >> FP_M1_DIV_SHIFT;
	if (IS_PINEVIEW(dev_priv)) {
		clock.n = ffs((fp & FP_N_PINEVIEW_DIV_MASK) >> FP_N_DIV_SHIFT) - 1;
		clock.m2 = (fp & FP_M2_PINEVIEW_DIV_MASK) >> FP_M2_DIV_SHIFT;
	} else {
		clock.n = (fp & FP_N_DIV_MASK) >> FP_N_DIV_SHIFT;
		clock.m2 = (fp & FP_M2_DIV_MASK) >> FP_M2_DIV_SHIFT;
	}

	if (DISPLAY_VER(dev_priv) != 2) {
		if (IS_PINEVIEW(dev_priv))
			clock.p1 = ffs((dpll & DPLL_FPA01_P1_POST_DIV_MASK_PINEVIEW) >>
				DPLL_FPA01_P1_POST_DIV_SHIFT_PINEVIEW);
		else
			clock.p1 = ffs((dpll & DPLL_FPA01_P1_POST_DIV_MASK) >>
			       DPLL_FPA01_P1_POST_DIV_SHIFT);

		switch (dpll & DPLL_MODE_MASK) {
		case DPLLB_MODE_DAC_SERIAL:
			clock.p2 = dpll & DPLL_DAC_SERIAL_P2_CLOCK_DIV_5 ?
				5 : 10;
			break;
		case DPLLB_MODE_LVDS:
			clock.p2 = dpll & DPLLB_LVDS_P2_CLOCK_DIV_7 ?
				7 : 14;
			break;
		default:
			drm_dbg_kms(&dev_priv->drm,
				    "Unknown DPLL mode %08x in programmed "
				    "mode\n", (int)(dpll & DPLL_MODE_MASK));
			return;
		}

		if (IS_PINEVIEW(dev_priv))
			port_clock = pnv_calc_dpll_params(refclk, &clock);
		else
			port_clock = i9xx_calc_dpll_params(refclk, &clock);
	} else {
		enum pipe lvds_pipe;

		if (IS_I85X(dev_priv) &&
		    intel_lvds_port_enabled(dev_priv, LVDS, &lvds_pipe) &&
		    lvds_pipe == crtc->pipe) {
			u32 lvds = intel_de_read(dev_priv, LVDS);

			clock.p1 = ffs((dpll & DPLL_FPA01_P1_POST_DIV_MASK_I830_LVDS) >>
				       DPLL_FPA01_P1_POST_DIV_SHIFT);

			if (lvds & LVDS_CLKB_POWER_UP)
				clock.p2 = 7;
			else
				clock.p2 = 14;
		} else {
			if (dpll & PLL_P1_DIVIDE_BY_TWO)
				clock.p1 = 2;
			else {
				clock.p1 = ((dpll & DPLL_FPA01_P1_POST_DIV_MASK_I830) >>
					    DPLL_FPA01_P1_POST_DIV_SHIFT) + 2;
			}
			if (dpll & PLL_P2_DIVIDE_BY_4)
				clock.p2 = 4;
			else
				clock.p2 = 2;
		}

		port_clock = i9xx_calc_dpll_params(refclk, &clock);
	}

	/*
	 * This value includes pixel_multiplier. We will use
	 * port_clock to compute adjusted_mode.crtc_clock in the
	 * encoder's get_config() function.
	 */
	crtc_state->port_clock = port_clock;
}

void vlv_crtc_clock_get(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum dpio_channel ch = vlv_pipe_to_channel(crtc->pipe);
	enum dpio_phy phy = vlv_pipe_to_phy(crtc->pipe);
	const struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;
	int refclk = 100000;
	struct dpll clock;
	u32 tmp;

	/* In case of DSI, DPLL will not be used */
	if ((hw_state->dpll & DPLL_VCO_ENABLE) == 0)
		return;

	vlv_dpio_get(dev_priv);
	tmp = vlv_dpio_read(dev_priv, phy, VLV_PLL_DW3(ch));
	vlv_dpio_put(dev_priv);

	clock.m1 = REG_FIELD_GET(DPIO_M1_DIV_MASK, tmp);
	clock.m2 = REG_FIELD_GET(DPIO_M2_DIV_MASK, tmp);
	clock.n = REG_FIELD_GET(DPIO_N_DIV_MASK, tmp);
	clock.p1 = REG_FIELD_GET(DPIO_P1_DIV_MASK, tmp);
	clock.p2 = REG_FIELD_GET(DPIO_P2_DIV_MASK, tmp);

	crtc_state->port_clock = vlv_calc_dpll_params(refclk, &clock);
}

void chv_crtc_clock_get(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum dpio_channel ch = vlv_pipe_to_channel(crtc->pipe);
	enum dpio_phy phy = vlv_pipe_to_phy(crtc->pipe);
	const struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;
	struct dpll clock;
	u32 cmn_dw13, pll_dw0, pll_dw1, pll_dw2, pll_dw3;
	int refclk = 100000;

	/* In case of DSI, DPLL will not be used */
	if ((hw_state->dpll & DPLL_VCO_ENABLE) == 0)
		return;

	vlv_dpio_get(dev_priv);
	cmn_dw13 = vlv_dpio_read(dev_priv, phy, CHV_CMN_DW13(ch));
	pll_dw0 = vlv_dpio_read(dev_priv, phy, CHV_PLL_DW0(ch));
	pll_dw1 = vlv_dpio_read(dev_priv, phy, CHV_PLL_DW1(ch));
	pll_dw2 = vlv_dpio_read(dev_priv, phy, CHV_PLL_DW2(ch));
	pll_dw3 = vlv_dpio_read(dev_priv, phy, CHV_PLL_DW3(ch));
	vlv_dpio_put(dev_priv);

	clock.m1 = REG_FIELD_GET(DPIO_CHV_M1_DIV_MASK, pll_dw1) == DPIO_CHV_M1_DIV_BY_2 ? 2 : 0;
	clock.m2 = REG_FIELD_GET(DPIO_CHV_M2_DIV_MASK, pll_dw0) << 22;
	if (pll_dw3 & DPIO_CHV_FRAC_DIV_EN)
		clock.m2 |= REG_FIELD_GET(DPIO_CHV_M2_FRAC_DIV_MASK, pll_dw2);
	clock.n = REG_FIELD_GET(DPIO_CHV_N_DIV_MASK, pll_dw1);
	clock.p1 = REG_FIELD_GET(DPIO_CHV_P1_DIV_MASK, cmn_dw13);
	clock.p2 = REG_FIELD_GET(DPIO_CHV_P2_DIV_MASK, cmn_dw13);

	crtc_state->port_clock = chv_calc_dpll_params(refclk, &clock);
}

/*
 * Returns whether the given set of divisors are valid for a given refclk with
 * the given connectors.
 */
static bool intel_pll_is_valid(struct drm_i915_private *dev_priv,
			       const struct intel_limit *limit,
			       const struct dpll *clock)
{
	if (clock->n < limit->n.min || limit->n.max < clock->n)
		return false;
	if (clock->p1 < limit->p1.min || limit->p1.max < clock->p1)
		return false;
	if (clock->m2 < limit->m2.min || limit->m2.max < clock->m2)
		return false;
	if (clock->m1 < limit->m1.min || limit->m1.max < clock->m1)
		return false;

	if (!IS_PINEVIEW(dev_priv) &&
	    !IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv) &&
	    !IS_BROXTON(dev_priv) && !IS_GEMINILAKE(dev_priv))
		if (clock->m1 <= clock->m2)
			return false;

	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv) &&
	    !IS_BROXTON(dev_priv) && !IS_GEMINILAKE(dev_priv)) {
		if (clock->p < limit->p.min || limit->p.max < clock->p)
			return false;
		if (clock->m < limit->m.min || limit->m.max < clock->m)
			return false;
	}

	if (clock->vco < limit->vco.min || limit->vco.max < clock->vco)
		return false;
	/* XXX: We may need to be checking "Dot clock" depending on the multiplier,
	 * connector, etc., rather than just a single range.
	 */
	if (clock->dot < limit->dot.min || limit->dot.max < clock->dot)
		return false;

	return true;
}

static int
i9xx_select_p2_div(const struct intel_limit *limit,
		   const struct intel_crtc_state *crtc_state,
		   int target)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		/*
		 * For LVDS just rely on its current settings for dual-channel.
		 * We haven't figured out how to reliably set up different
		 * single/dual channel state, if we even can.
		 */
		if (intel_is_dual_link_lvds(dev_priv))
			return limit->p2.p2_fast;
		else
			return limit->p2.p2_slow;
	} else {
		if (target < limit->p2.dot_limit)
			return limit->p2.p2_slow;
		else
			return limit->p2.p2_fast;
	}
}

/*
 * Returns a set of divisors for the desired target clock with the given
 * refclk, or FALSE.
 *
 * Target and reference clocks are specified in kHz.
 *
 * If match_clock is provided, then best_clock P divider must match the P
 * divider from @match_clock used for LVDS downclocking.
 */
static bool
i9xx_find_best_dpll(const struct intel_limit *limit,
		    struct intel_crtc_state *crtc_state,
		    int target, int refclk,
		    const struct dpll *match_clock,
		    struct dpll *best_clock)
{
	struct drm_device *dev = crtc_state->uapi.crtc->dev;
	struct dpll clock;
	int err = target;

	memset(best_clock, 0, sizeof(*best_clock));

	clock.p2 = i9xx_select_p2_div(limit, crtc_state, target);

	for (clock.m1 = limit->m1.min; clock.m1 <= limit->m1.max;
	     clock.m1++) {
		for (clock.m2 = limit->m2.min;
		     clock.m2 <= limit->m2.max; clock.m2++) {
			if (clock.m2 >= clock.m1)
				break;
			for (clock.n = limit->n.min;
			     clock.n <= limit->n.max; clock.n++) {
				for (clock.p1 = limit->p1.min;
					clock.p1 <= limit->p1.max; clock.p1++) {
					int this_err;

					i9xx_calc_dpll_params(refclk, &clock);
					if (!intel_pll_is_valid(to_i915(dev),
								limit,
								&clock))
						continue;
					if (match_clock &&
					    clock.p != match_clock->p)
						continue;

					this_err = abs(clock.dot - target);
					if (this_err < err) {
						*best_clock = clock;
						err = this_err;
					}
				}
			}
		}
	}

	return (err != target);
}

/*
 * Returns a set of divisors for the desired target clock with the given
 * refclk, or FALSE.
 *
 * Target and reference clocks are specified in kHz.
 *
 * If match_clock is provided, then best_clock P divider must match the P
 * divider from @match_clock used for LVDS downclocking.
 */
static bool
pnv_find_best_dpll(const struct intel_limit *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk,
		   const struct dpll *match_clock,
		   struct dpll *best_clock)
{
	struct drm_device *dev = crtc_state->uapi.crtc->dev;
	struct dpll clock;
	int err = target;

	memset(best_clock, 0, sizeof(*best_clock));

	clock.p2 = i9xx_select_p2_div(limit, crtc_state, target);

	for (clock.m1 = limit->m1.min; clock.m1 <= limit->m1.max;
	     clock.m1++) {
		for (clock.m2 = limit->m2.min;
		     clock.m2 <= limit->m2.max; clock.m2++) {
			for (clock.n = limit->n.min;
			     clock.n <= limit->n.max; clock.n++) {
				for (clock.p1 = limit->p1.min;
					clock.p1 <= limit->p1.max; clock.p1++) {
					int this_err;

					pnv_calc_dpll_params(refclk, &clock);
					if (!intel_pll_is_valid(to_i915(dev),
								limit,
								&clock))
						continue;
					if (match_clock &&
					    clock.p != match_clock->p)
						continue;

					this_err = abs(clock.dot - target);
					if (this_err < err) {
						*best_clock = clock;
						err = this_err;
					}
				}
			}
		}
	}

	return (err != target);
}

/*
 * Returns a set of divisors for the desired target clock with the given
 * refclk, or FALSE.
 *
 * Target and reference clocks are specified in kHz.
 *
 * If match_clock is provided, then best_clock P divider must match the P
 * divider from @match_clock used for LVDS downclocking.
 */
static bool
g4x_find_best_dpll(const struct intel_limit *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk,
		   const struct dpll *match_clock,
		   struct dpll *best_clock)
{
	struct drm_device *dev = crtc_state->uapi.crtc->dev;
	struct dpll clock;
	int max_n;
	bool found = false;
	/* approximately equals target * 0.00585 */
	int err_most = (target >> 8) + (target >> 9);

	memset(best_clock, 0, sizeof(*best_clock));

	clock.p2 = i9xx_select_p2_div(limit, crtc_state, target);

	max_n = limit->n.max;
	/* based on hardware requirement, prefer smaller n to precision */
	for (clock.n = limit->n.min; clock.n <= max_n; clock.n++) {
		/* based on hardware requirement, prefer larger m1,m2 */
		for (clock.m1 = limit->m1.max;
		     clock.m1 >= limit->m1.min; clock.m1--) {
			for (clock.m2 = limit->m2.max;
			     clock.m2 >= limit->m2.min; clock.m2--) {
				for (clock.p1 = limit->p1.max;
				     clock.p1 >= limit->p1.min; clock.p1--) {
					int this_err;

					i9xx_calc_dpll_params(refclk, &clock);
					if (!intel_pll_is_valid(to_i915(dev),
								limit,
								&clock))
						continue;

					this_err = abs(clock.dot - target);
					if (this_err < err_most) {
						*best_clock = clock;
						err_most = this_err;
						max_n = clock.n;
						found = true;
					}
				}
			}
		}
	}
	return found;
}

/*
 * Check if the calculated PLL configuration is more optimal compared to the
 * best configuration and error found so far. Return the calculated error.
 */
static bool vlv_PLL_is_optimal(struct drm_device *dev, int target_freq,
			       const struct dpll *calculated_clock,
			       const struct dpll *best_clock,
			       unsigned int best_error_ppm,
			       unsigned int *error_ppm)
{
	/*
	 * For CHV ignore the error and consider only the P value.
	 * Prefer a bigger P value based on HW requirements.
	 */
	if (IS_CHERRYVIEW(to_i915(dev))) {
		*error_ppm = 0;

		return calculated_clock->p > best_clock->p;
	}

	if (drm_WARN_ON_ONCE(dev, !target_freq))
		return false;

	*error_ppm = div_u64(1000000ULL *
				abs(target_freq - calculated_clock->dot),
			     target_freq);
	/*
	 * Prefer a better P value over a better (smaller) error if the error
	 * is small. Ensure this preference for future configurations too by
	 * setting the error to 0.
	 */
	if (*error_ppm < 100 && calculated_clock->p > best_clock->p) {
		*error_ppm = 0;

		return true;
	}

	return *error_ppm + 10 < best_error_ppm;
}

/*
 * Returns a set of divisors for the desired target clock with the given
 * refclk, or FALSE.
 */
static bool
vlv_find_best_dpll(const struct intel_limit *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk,
		   const struct dpll *match_clock,
		   struct dpll *best_clock)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_device *dev = crtc->base.dev;
	struct dpll clock;
	unsigned int bestppm = 1000000;
	/* min update 19.2 MHz */
	int max_n = min(limit->n.max, refclk / 19200);
	bool found = false;

	memset(best_clock, 0, sizeof(*best_clock));

	/* based on hardware requirement, prefer smaller n to precision */
	for (clock.n = limit->n.min; clock.n <= max_n; clock.n++) {
		for (clock.p1 = limit->p1.max; clock.p1 >= limit->p1.min; clock.p1--) {
			for (clock.p2 = limit->p2.p2_fast; clock.p2 >= limit->p2.p2_slow;
			     clock.p2 -= clock.p2 > 10 ? 2 : 1) {
				clock.p = clock.p1 * clock.p2 * 5;
				/* based on hardware requirement, prefer bigger m1,m2 values */
				for (clock.m1 = limit->m1.min; clock.m1 <= limit->m1.max; clock.m1++) {
					unsigned int ppm;

					clock.m2 = DIV_ROUND_CLOSEST(target * clock.p * clock.n,
								     refclk * clock.m1);

					vlv_calc_dpll_params(refclk, &clock);

					if (!intel_pll_is_valid(to_i915(dev),
								limit,
								&clock))
						continue;

					if (!vlv_PLL_is_optimal(dev, target,
								&clock,
								best_clock,
								bestppm, &ppm))
						continue;

					*best_clock = clock;
					bestppm = ppm;
					found = true;
				}
			}
		}
	}

	return found;
}

/*
 * Returns a set of divisors for the desired target clock with the given
 * refclk, or FALSE.
 */
static bool
chv_find_best_dpll(const struct intel_limit *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk,
		   const struct dpll *match_clock,
		   struct dpll *best_clock)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_device *dev = crtc->base.dev;
	unsigned int best_error_ppm;
	struct dpll clock;
	u64 m2;
	int found = false;

	memset(best_clock, 0, sizeof(*best_clock));
	best_error_ppm = 1000000;

	/*
	 * Based on hardware doc, the n always set to 1, and m1 always
	 * set to 2.  If requires to support 200Mhz refclk, we need to
	 * revisit this because n may not 1 anymore.
	 */
	clock.n = 1;
	clock.m1 = 2;

	for (clock.p1 = limit->p1.max; clock.p1 >= limit->p1.min; clock.p1--) {
		for (clock.p2 = limit->p2.p2_fast;
				clock.p2 >= limit->p2.p2_slow;
				clock.p2 -= clock.p2 > 10 ? 2 : 1) {
			unsigned int error_ppm;

			clock.p = clock.p1 * clock.p2 * 5;

			m2 = DIV_ROUND_CLOSEST_ULL(mul_u32_u32(target, clock.p * clock.n) << 22,
						   refclk * clock.m1);

			if (m2 > INT_MAX/clock.m1)
				continue;

			clock.m2 = m2;

			chv_calc_dpll_params(refclk, &clock);

			if (!intel_pll_is_valid(to_i915(dev), limit, &clock))
				continue;

			if (!vlv_PLL_is_optimal(dev, target, &clock, best_clock,
						best_error_ppm, &error_ppm))
				continue;

			*best_clock = clock;
			best_error_ppm = error_ppm;
			found = true;
		}
	}

	return found;
}

bool bxt_find_best_dpll(struct intel_crtc_state *crtc_state,
			struct dpll *best_clock)
{
	const struct intel_limit *limit = &intel_limits_bxt;
	int refclk = 100000;

	return chv_find_best_dpll(limit, crtc_state,
				  crtc_state->port_clock, refclk,
				  NULL, best_clock);
}

u32 i9xx_dpll_compute_fp(const struct dpll *dpll)
{
	return dpll->n << 16 | dpll->m1 << 8 | dpll->m2;
}

static u32 pnv_dpll_compute_fp(const struct dpll *dpll)
{
	return (1 << dpll->n) << 16 | dpll->m2;
}

static u32 i965_dpll_md(const struct intel_crtc_state *crtc_state)
{
	return (crtc_state->pixel_multiplier - 1) << DPLL_MD_UDI_MULTIPLIER_SHIFT;
}

static u32 i9xx_dpll(const struct intel_crtc_state *crtc_state,
		     const struct dpll *clock,
		     const struct dpll *reduced_clock)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 dpll;

	dpll = DPLL_VCO_ENABLE | DPLL_VGA_MODE_DIS;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS))
		dpll |= DPLLB_MODE_LVDS;
	else
		dpll |= DPLLB_MODE_DAC_SERIAL;

	if (IS_I945G(dev_priv) || IS_I945GM(dev_priv) ||
	    IS_G33(dev_priv) || IS_PINEVIEW(dev_priv)) {
		dpll |= (crtc_state->pixel_multiplier - 1)
			<< SDVO_MULTIPLIER_SHIFT_HIRES;
	}

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_SDVO) ||
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		dpll |= DPLL_SDVO_HIGH_SPEED;

	if (intel_crtc_has_dp_encoder(crtc_state))
		dpll |= DPLL_SDVO_HIGH_SPEED;

	/* compute bitmask from p1 value */
	if (IS_G4X(dev_priv)) {
		dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
		dpll |= (1 << (reduced_clock->p1 - 1)) << DPLL_FPA1_P1_POST_DIV_SHIFT;
	} else if (IS_PINEVIEW(dev_priv)) {
		dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT_PINEVIEW;
		WARN_ON(reduced_clock->p1 != clock->p1);
	} else {
		dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
		WARN_ON(reduced_clock->p1 != clock->p1);
	}

	switch (clock->p2) {
	case 5:
		dpll |= DPLL_DAC_SERIAL_P2_CLOCK_DIV_5;
		break;
	case 7:
		dpll |= DPLLB_LVDS_P2_CLOCK_DIV_7;
		break;
	case 10:
		dpll |= DPLL_DAC_SERIAL_P2_CLOCK_DIV_10;
		break;
	case 14:
		dpll |= DPLLB_LVDS_P2_CLOCK_DIV_14;
		break;
	}
	WARN_ON(reduced_clock->p2 != clock->p2);

	if (DISPLAY_VER(dev_priv) >= 4)
		dpll |= (6 << PLL_LOAD_PULSE_PHASE_SHIFT);

	if (crtc_state->sdvo_tv_clock)
		dpll |= PLL_REF_INPUT_TVCLKINBC;
	else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS) &&
		 intel_panel_use_ssc(display))
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	return dpll;
}

static void i9xx_compute_dpll(struct intel_crtc_state *crtc_state,
			      const struct dpll *clock,
			      const struct dpll *reduced_clock)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;

	if (IS_PINEVIEW(dev_priv)) {
		hw_state->fp0 = pnv_dpll_compute_fp(clock);
		hw_state->fp1 = pnv_dpll_compute_fp(reduced_clock);
	} else {
		hw_state->fp0 = i9xx_dpll_compute_fp(clock);
		hw_state->fp1 = i9xx_dpll_compute_fp(reduced_clock);
	}

	hw_state->dpll = i9xx_dpll(crtc_state, clock, reduced_clock);

	if (DISPLAY_VER(dev_priv) >= 4)
		hw_state->dpll_md = i965_dpll_md(crtc_state);
}

static u32 i8xx_dpll(const struct intel_crtc_state *crtc_state,
		     const struct dpll *clock,
		     const struct dpll *reduced_clock)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 dpll;

	dpll = DPLL_VCO_ENABLE | DPLL_VGA_MODE_DIS;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
	} else {
		if (clock->p1 == 2)
			dpll |= PLL_P1_DIVIDE_BY_TWO;
		else
			dpll |= (clock->p1 - 2) << DPLL_FPA01_P1_POST_DIV_SHIFT;
		if (clock->p2 == 4)
			dpll |= PLL_P2_DIVIDE_BY_4;
	}
	WARN_ON(reduced_clock->p1 != clock->p1);
	WARN_ON(reduced_clock->p2 != clock->p2);

	/*
	 * Bspec:
	 * "[Almador Errata}: For the correct operation of the muxed DVO pins
	 *  (GDEVSELB/I2Cdata, GIRDBY/I2CClk) and (GFRAMEB/DVI_Data,
	 *  GTRDYB/DVI_Clk): Bit 31 (DPLL VCO Enable) and Bit 30 (2X Clock
	 *  Enable) must be set to “1” in both the DPLL A Control Register
	 *  (06014h-06017h) and DPLL B Control Register (06018h-0601Bh)."
	 *
	 * For simplicity We simply keep both bits always enabled in
	 * both DPLLS. The spec says we should disable the DVO 2X clock
	 * when not needed, but this seems to work fine in practice.
	 */
	if (IS_I830(dev_priv) ||
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DVO))
		dpll |= DPLL_DVO_2X_MODE;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS) &&
	    intel_panel_use_ssc(display))
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	return dpll;
}

static void i8xx_compute_dpll(struct intel_crtc_state *crtc_state,
			      const struct dpll *clock,
			      const struct dpll *reduced_clock)
{
	struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;

	hw_state->fp0 = i9xx_dpll_compute_fp(clock);
	hw_state->fp1 = i9xx_dpll_compute_fp(reduced_clock);

	hw_state->dpll = i8xx_dpll(crtc_state, clock, reduced_clock);
}

static int hsw_crtc_compute_clock(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_encoder *encoder =
		intel_get_crtc_new_encoder(state, crtc_state);
	int ret;

	if (DISPLAY_VER(dev_priv) < 11 &&
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI))
		return 0;

	ret = intel_compute_shared_dplls(state, crtc, encoder);
	if (ret)
		return ret;

	/* FIXME this is a mess */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI))
		return 0;

	/* CRT dotclock is determined via other means */
	if (!crtc_state->has_pch_encoder)
		crtc_state->hw.adjusted_mode.crtc_clock = intel_crtc_dotclock(crtc_state);

	return 0;
}

static int hsw_crtc_get_shared_dpll(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_encoder *encoder =
		intel_get_crtc_new_encoder(state, crtc_state);

	if (DISPLAY_VER(dev_priv) < 11 &&
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI))
		return 0;

	return intel_reserve_shared_dplls(state, crtc, encoder);
}

static int dg2_crtc_compute_clock(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_encoder *encoder =
		intel_get_crtc_new_encoder(state, crtc_state);
	int ret;

	ret = intel_mpllb_calc_state(crtc_state, encoder);
	if (ret)
		return ret;

	crtc_state->hw.adjusted_mode.crtc_clock = intel_crtc_dotclock(crtc_state);

	return 0;
}

static int mtl_crtc_compute_clock(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_encoder *encoder =
		intel_get_crtc_new_encoder(state, crtc_state);
	int ret;

	ret = intel_cx0pll_calc_state(crtc_state, encoder);
	if (ret)
		return ret;

	/* TODO: Do the readback via intel_compute_shared_dplls() */
	crtc_state->port_clock = intel_cx0pll_calc_port_clock(encoder, &crtc_state->dpll_hw_state.cx0pll);

	crtc_state->hw.adjusted_mode.crtc_clock = intel_crtc_dotclock(crtc_state);

	return 0;
}

static int ilk_fb_cb_factor(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS) &&
	    ((intel_panel_use_ssc(display) && i915->display.vbt.lvds_ssc_freq == 100000) ||
	     (HAS_PCH_IBX(i915) && intel_is_dual_link_lvds(i915))))
		return 25;

	if (crtc_state->sdvo_tv_clock)
		return 20;

	return 21;
}

static bool ilk_needs_fb_cb_tune(const struct dpll *dpll, int factor)
{
	return dpll->m < factor * dpll->n;
}

static u32 ilk_dpll_compute_fp(const struct dpll *clock, int factor)
{
	u32 fp;

	fp = i9xx_dpll_compute_fp(clock);
	if (ilk_needs_fb_cb_tune(clock, factor))
		fp |= FP_CB_TUNE;

	return fp;
}

static u32 ilk_dpll(const struct intel_crtc_state *crtc_state,
		    const struct dpll *clock,
		    const struct dpll *reduced_clock)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 dpll;

	dpll = DPLL_VCO_ENABLE;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS))
		dpll |= DPLLB_MODE_LVDS;
	else
		dpll |= DPLLB_MODE_DAC_SERIAL;

	dpll |= (crtc_state->pixel_multiplier - 1)
		<< PLL_REF_SDVO_HDMI_MULTIPLIER_SHIFT;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_SDVO) ||
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		dpll |= DPLL_SDVO_HIGH_SPEED;

	if (intel_crtc_has_dp_encoder(crtc_state))
		dpll |= DPLL_SDVO_HIGH_SPEED;

	/*
	 * The high speed IO clock is only really required for
	 * SDVO/HDMI/DP, but we also enable it for CRT to make it
	 * possible to share the DPLL between CRT and HDMI. Enabling
	 * the clock needlessly does no real harm, except use up a
	 * bit of power potentially.
	 *
	 * We'll limit this to IVB with 3 pipes, since it has only two
	 * DPLLs and so DPLL sharing is the only way to get three pipes
	 * driving PCH ports at the same time. On SNB we could do this,
	 * and potentially avoid enabling the second DPLL, but it's not
	 * clear if it''s a win or loss power wise. No point in doing
	 * this on ILK at all since it has a fixed DPLL<->pipe mapping.
	 */
	if (INTEL_NUM_PIPES(dev_priv) == 3 &&
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_ANALOG))
		dpll |= DPLL_SDVO_HIGH_SPEED;

	/* compute bitmask from p1 value */
	dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
	/* also FPA1 */
	dpll |= (1 << (reduced_clock->p1 - 1)) << DPLL_FPA1_P1_POST_DIV_SHIFT;

	switch (clock->p2) {
	case 5:
		dpll |= DPLL_DAC_SERIAL_P2_CLOCK_DIV_5;
		break;
	case 7:
		dpll |= DPLLB_LVDS_P2_CLOCK_DIV_7;
		break;
	case 10:
		dpll |= DPLL_DAC_SERIAL_P2_CLOCK_DIV_10;
		break;
	case 14:
		dpll |= DPLLB_LVDS_P2_CLOCK_DIV_14;
		break;
	}
	WARN_ON(reduced_clock->p2 != clock->p2);

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS) &&
	    intel_panel_use_ssc(display))
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	return dpll;
}

static void ilk_compute_dpll(struct intel_crtc_state *crtc_state,
			     const struct dpll *clock,
			     const struct dpll *reduced_clock)
{
	struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;
	int factor = ilk_fb_cb_factor(crtc_state);

	hw_state->fp0 = ilk_dpll_compute_fp(clock, factor);
	hw_state->fp1 = ilk_dpll_compute_fp(reduced_clock, factor);

	hw_state->dpll = ilk_dpll(crtc_state, clock, reduced_clock);
}

static int ilk_crtc_compute_clock(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_limit *limit;
	int refclk = 120000;
	int ret;

	/* CPU eDP is the only output that doesn't need a PCH PLL of its own. */
	if (!crtc_state->has_pch_encoder)
		return 0;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if (intel_panel_use_ssc(display)) {
			drm_dbg_kms(&dev_priv->drm,
				    "using SSC reference clock of %d kHz\n",
				    dev_priv->display.vbt.lvds_ssc_freq);
			refclk = dev_priv->display.vbt.lvds_ssc_freq;
		}

		if (intel_is_dual_link_lvds(dev_priv)) {
			if (refclk == 100000)
				limit = &ilk_limits_dual_lvds_100m;
			else
				limit = &ilk_limits_dual_lvds;
		} else {
			if (refclk == 100000)
				limit = &ilk_limits_single_lvds_100m;
			else
				limit = &ilk_limits_single_lvds;
		}
	} else {
		limit = &ilk_limits_dac;
	}

	if (!crtc_state->clock_set &&
	    !g4x_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				refclk, NULL, &crtc_state->dpll))
		return -EINVAL;

	i9xx_calc_dpll_params(refclk, &crtc_state->dpll);

	ilk_compute_dpll(crtc_state, &crtc_state->dpll,
			 &crtc_state->dpll);

	ret = intel_compute_shared_dplls(state, crtc, NULL);
	if (ret)
		return ret;

	crtc_state->port_clock = crtc_state->dpll.dot;
	crtc_state->hw.adjusted_mode.crtc_clock = intel_crtc_dotclock(crtc_state);

	return ret;
}

static int ilk_crtc_get_shared_dpll(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	/* CPU eDP is the only output that doesn't need a PCH PLL of its own. */
	if (!crtc_state->has_pch_encoder)
		return 0;

	return intel_reserve_shared_dplls(state, crtc, NULL);
}

static u32 vlv_dpll(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u32 dpll;

	dpll = DPLL_INTEGRATED_REF_CLK_VLV |
		DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS;

	if (crtc->pipe != PIPE_A)
		dpll |= DPLL_INTEGRATED_CRI_CLK_VLV;

	/* DPLL not used with DSI, but still need the rest set up */
	if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI))
		dpll |= DPLL_VCO_ENABLE | DPLL_EXT_BUFFER_ENABLE_VLV;

	return dpll;
}

void vlv_compute_dpll(struct intel_crtc_state *crtc_state)
{
	struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;

	hw_state->dpll = vlv_dpll(crtc_state);
	hw_state->dpll_md = i965_dpll_md(crtc_state);
}

static u32 chv_dpll(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u32 dpll;

	dpll = DPLL_SSC_REF_CLK_CHV |
		DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS;

	if (crtc->pipe != PIPE_A)
		dpll |= DPLL_INTEGRATED_CRI_CLK_VLV;

	/* DPLL not used with DSI, but still need the rest set up */
	if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI))
		dpll |= DPLL_VCO_ENABLE;

	return dpll;
}

void chv_compute_dpll(struct intel_crtc_state *crtc_state)
{
	struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;

	hw_state->dpll = chv_dpll(crtc_state);
	hw_state->dpll_md = i965_dpll_md(crtc_state);
}

static int chv_crtc_compute_clock(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_limit *limit = &intel_limits_chv;
	int refclk = 100000;

	if (!crtc_state->clock_set &&
	    !chv_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				refclk, NULL, &crtc_state->dpll))
		return -EINVAL;

	chv_calc_dpll_params(refclk, &crtc_state->dpll);

	chv_compute_dpll(crtc_state);

	/* FIXME this is a mess */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI))
		return 0;

	crtc_state->port_clock = crtc_state->dpll.dot;
	crtc_state->hw.adjusted_mode.crtc_clock = intel_crtc_dotclock(crtc_state);

	return 0;
}

static int vlv_crtc_compute_clock(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_limit *limit = &intel_limits_vlv;
	int refclk = 100000;

	if (!crtc_state->clock_set &&
	    !vlv_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				refclk, NULL, &crtc_state->dpll))
		return -EINVAL;

	vlv_calc_dpll_params(refclk, &crtc_state->dpll);

	vlv_compute_dpll(crtc_state);

	/* FIXME this is a mess */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI))
		return 0;

	crtc_state->port_clock = crtc_state->dpll.dot;
	crtc_state->hw.adjusted_mode.crtc_clock = intel_crtc_dotclock(crtc_state);

	return 0;
}

static int g4x_crtc_compute_clock(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_limit *limit;
	int refclk = 96000;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if (intel_panel_use_ssc(display)) {
			refclk = dev_priv->display.vbt.lvds_ssc_freq;
			drm_dbg_kms(&dev_priv->drm,
				    "using SSC reference clock of %d kHz\n",
				    refclk);
		}

		if (intel_is_dual_link_lvds(dev_priv))
			limit = &intel_limits_g4x_dual_channel_lvds;
		else
			limit = &intel_limits_g4x_single_channel_lvds;
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI) ||
		   intel_crtc_has_type(crtc_state, INTEL_OUTPUT_ANALOG)) {
		limit = &intel_limits_g4x_hdmi;
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_SDVO)) {
		limit = &intel_limits_g4x_sdvo;
	} else {
		/* The option is for other outputs */
		limit = &intel_limits_i9xx_sdvo;
	}

	if (!crtc_state->clock_set &&
	    !g4x_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				refclk, NULL, &crtc_state->dpll))
		return -EINVAL;

	i9xx_calc_dpll_params(refclk, &crtc_state->dpll);

	i9xx_compute_dpll(crtc_state, &crtc_state->dpll,
			  &crtc_state->dpll);

	crtc_state->port_clock = crtc_state->dpll.dot;
	/* FIXME this is a mess */
	if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_TVOUT))
		crtc_state->hw.adjusted_mode.crtc_clock = intel_crtc_dotclock(crtc_state);

	return 0;
}

static int pnv_crtc_compute_clock(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_limit *limit;
	int refclk = 96000;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if (intel_panel_use_ssc(display)) {
			refclk = dev_priv->display.vbt.lvds_ssc_freq;
			drm_dbg_kms(&dev_priv->drm,
				    "using SSC reference clock of %d kHz\n",
				    refclk);
		}

		limit = &pnv_limits_lvds;
	} else {
		limit = &pnv_limits_sdvo;
	}

	if (!crtc_state->clock_set &&
	    !pnv_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				refclk, NULL, &crtc_state->dpll))
		return -EINVAL;

	pnv_calc_dpll_params(refclk, &crtc_state->dpll);

	i9xx_compute_dpll(crtc_state, &crtc_state->dpll,
			  &crtc_state->dpll);

	crtc_state->port_clock = crtc_state->dpll.dot;
	crtc_state->hw.adjusted_mode.crtc_clock = intel_crtc_dotclock(crtc_state);

	return 0;
}

static int i9xx_crtc_compute_clock(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_limit *limit;
	int refclk = 96000;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if (intel_panel_use_ssc(display)) {
			refclk = dev_priv->display.vbt.lvds_ssc_freq;
			drm_dbg_kms(&dev_priv->drm,
				    "using SSC reference clock of %d kHz\n",
				    refclk);
		}

		limit = &intel_limits_i9xx_lvds;
	} else {
		limit = &intel_limits_i9xx_sdvo;
	}

	if (!crtc_state->clock_set &&
	    !i9xx_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				 refclk, NULL, &crtc_state->dpll))
		return -EINVAL;

	i9xx_calc_dpll_params(refclk, &crtc_state->dpll);

	i9xx_compute_dpll(crtc_state, &crtc_state->dpll,
			  &crtc_state->dpll);

	crtc_state->port_clock = crtc_state->dpll.dot;
	/* FIXME this is a mess */
	if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_TVOUT))
		crtc_state->hw.adjusted_mode.crtc_clock = intel_crtc_dotclock(crtc_state);

	return 0;
}

static int i8xx_crtc_compute_clock(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_limit *limit;
	int refclk = 48000;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if (intel_panel_use_ssc(display)) {
			refclk = dev_priv->display.vbt.lvds_ssc_freq;
			drm_dbg_kms(&dev_priv->drm,
				    "using SSC reference clock of %d kHz\n",
				    refclk);
		}

		limit = &intel_limits_i8xx_lvds;
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DVO)) {
		limit = &intel_limits_i8xx_dvo;
	} else {
		limit = &intel_limits_i8xx_dac;
	}

	if (!crtc_state->clock_set &&
	    !i9xx_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				 refclk, NULL, &crtc_state->dpll))
		return -EINVAL;

	i9xx_calc_dpll_params(refclk, &crtc_state->dpll);

	i8xx_compute_dpll(crtc_state, &crtc_state->dpll,
			  &crtc_state->dpll);

	crtc_state->port_clock = crtc_state->dpll.dot;
	crtc_state->hw.adjusted_mode.crtc_clock = intel_crtc_dotclock(crtc_state);

	return 0;
}

static const struct intel_dpll_funcs mtl_dpll_funcs = {
	.crtc_compute_clock = mtl_crtc_compute_clock,
};

static const struct intel_dpll_funcs dg2_dpll_funcs = {
	.crtc_compute_clock = dg2_crtc_compute_clock,
};

static const struct intel_dpll_funcs hsw_dpll_funcs = {
	.crtc_compute_clock = hsw_crtc_compute_clock,
	.crtc_get_shared_dpll = hsw_crtc_get_shared_dpll,
};

static const struct intel_dpll_funcs ilk_dpll_funcs = {
	.crtc_compute_clock = ilk_crtc_compute_clock,
	.crtc_get_shared_dpll = ilk_crtc_get_shared_dpll,
};

static const struct intel_dpll_funcs chv_dpll_funcs = {
	.crtc_compute_clock = chv_crtc_compute_clock,
};

static const struct intel_dpll_funcs vlv_dpll_funcs = {
	.crtc_compute_clock = vlv_crtc_compute_clock,
};

static const struct intel_dpll_funcs g4x_dpll_funcs = {
	.crtc_compute_clock = g4x_crtc_compute_clock,
};

static const struct intel_dpll_funcs pnv_dpll_funcs = {
	.crtc_compute_clock = pnv_crtc_compute_clock,
};

static const struct intel_dpll_funcs i9xx_dpll_funcs = {
	.crtc_compute_clock = i9xx_crtc_compute_clock,
};

static const struct intel_dpll_funcs i8xx_dpll_funcs = {
	.crtc_compute_clock = i8xx_crtc_compute_clock,
};

int intel_dpll_crtc_compute_clock(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	int ret;

	drm_WARN_ON(&i915->drm, !intel_crtc_needs_modeset(crtc_state));

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	if (!crtc_state->hw.enable)
		return 0;

	ret = i915->display.funcs.dpll->crtc_compute_clock(state, crtc);
	if (ret) {
		drm_dbg_kms(&i915->drm, "[CRTC:%d:%s] Couldn't calculate DPLL settings\n",
			    crtc->base.base.id, crtc->base.name);
		return ret;
	}

	return 0;
}

int intel_dpll_crtc_get_shared_dpll(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	int ret;

	drm_WARN_ON(&i915->drm, !intel_crtc_needs_modeset(crtc_state));
	drm_WARN_ON(&i915->drm, !crtc_state->hw.enable && crtc_state->shared_dpll);

	if (!crtc_state->hw.enable || crtc_state->shared_dpll)
		return 0;

	if (!i915->display.funcs.dpll->crtc_get_shared_dpll)
		return 0;

	ret = i915->display.funcs.dpll->crtc_get_shared_dpll(state, crtc);
	if (ret) {
		drm_dbg_kms(&i915->drm, "[CRTC:%d:%s] Couldn't get a shared DPLL\n",
			    crtc->base.base.id, crtc->base.name);
		return ret;
	}

	return 0;
}

void
intel_dpll_init_clock_hook(struct drm_i915_private *dev_priv)
{
	if (DISPLAY_VER(dev_priv) >= 14)
		dev_priv->display.funcs.dpll = &mtl_dpll_funcs;
	else if (IS_DG2(dev_priv))
		dev_priv->display.funcs.dpll = &dg2_dpll_funcs;
	else if (DISPLAY_VER(dev_priv) >= 9 || HAS_DDI(dev_priv))
		dev_priv->display.funcs.dpll = &hsw_dpll_funcs;
	else if (HAS_PCH_SPLIT(dev_priv))
		dev_priv->display.funcs.dpll = &ilk_dpll_funcs;
	else if (IS_CHERRYVIEW(dev_priv))
		dev_priv->display.funcs.dpll = &chv_dpll_funcs;
	else if (IS_VALLEYVIEW(dev_priv))
		dev_priv->display.funcs.dpll = &vlv_dpll_funcs;
	else if (IS_G4X(dev_priv))
		dev_priv->display.funcs.dpll = &g4x_dpll_funcs;
	else if (IS_PINEVIEW(dev_priv))
		dev_priv->display.funcs.dpll = &pnv_dpll_funcs;
	else if (DISPLAY_VER(dev_priv) != 2)
		dev_priv->display.funcs.dpll = &i9xx_dpll_funcs;
	else
		dev_priv->display.funcs.dpll = &i8xx_dpll_funcs;
}

static bool i9xx_has_pps(struct drm_i915_private *dev_priv)
{
	if (IS_I830(dev_priv))
		return false;

	return IS_PINEVIEW(dev_priv) || IS_MOBILE(dev_priv);
}

void i9xx_enable_pll(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;
	enum pipe pipe = crtc->pipe;
	int i;

	assert_transcoder_disabled(dev_priv, crtc_state->cpu_transcoder);

	/* PLL is protected by panel, make sure we can write it */
	if (i9xx_has_pps(dev_priv))
		assert_pps_unlocked(display, pipe);

	intel_de_write(dev_priv, FP0(pipe), hw_state->fp0);
	intel_de_write(dev_priv, FP1(pipe), hw_state->fp1);

	/*
	 * Apparently we need to have VGA mode enabled prior to changing
	 * the P1/P2 dividers. Otherwise the DPLL will keep using the old
	 * dividers, even though the register value does change.
	 */
	intel_de_write(dev_priv, DPLL(dev_priv, pipe),
		       hw_state->dpll & ~DPLL_VGA_MODE_DIS);
	intel_de_write(dev_priv, DPLL(dev_priv, pipe), hw_state->dpll);

	/* Wait for the clocks to stabilize. */
	intel_de_posting_read(dev_priv, DPLL(dev_priv, pipe));
	udelay(150);

	if (DISPLAY_VER(dev_priv) >= 4) {
		intel_de_write(dev_priv, DPLL_MD(dev_priv, pipe),
			       hw_state->dpll_md);
	} else {
		/* The pixel multiplier can only be updated once the
		 * DPLL is enabled and the clocks are stable.
		 *
		 * So write it again.
		 */
		intel_de_write(dev_priv, DPLL(dev_priv, pipe), hw_state->dpll);
	}

	/* We do this three times for luck */
	for (i = 0; i < 3; i++) {
		intel_de_write(dev_priv, DPLL(dev_priv, pipe), hw_state->dpll);
		intel_de_posting_read(dev_priv, DPLL(dev_priv, pipe));
		udelay(150); /* wait for warmup */
	}
}

static void vlv_pllb_recal_opamp(struct drm_i915_private *dev_priv,
				 enum dpio_phy phy, enum dpio_channel ch)
{
	u32 tmp;

	/*
	 * PLLB opamp always calibrates to max value of 0x3f, force enable it
	 * and set it to a reasonable value instead.
	 */
	tmp = vlv_dpio_read(dev_priv, phy, VLV_PLL_DW17(ch));
	tmp &= 0xffffff00;
	tmp |= 0x00000030;
	vlv_dpio_write(dev_priv, phy, VLV_PLL_DW17(ch), tmp);

	tmp = vlv_dpio_read(dev_priv, phy, VLV_REF_DW11);
	tmp &= 0x00ffffff;
	tmp |= 0x8c000000;
	vlv_dpio_write(dev_priv, phy, VLV_REF_DW11, tmp);

	tmp = vlv_dpio_read(dev_priv, phy, VLV_PLL_DW17(ch));
	tmp &= 0xffffff00;
	vlv_dpio_write(dev_priv, phy, VLV_PLL_DW17(ch), tmp);

	tmp = vlv_dpio_read(dev_priv, phy, VLV_REF_DW11);
	tmp &= 0x00ffffff;
	tmp |= 0xb0000000;
	vlv_dpio_write(dev_priv, phy, VLV_REF_DW11, tmp);
}

static void vlv_prepare_pll(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct dpll *clock = &crtc_state->dpll;
	enum dpio_channel ch = vlv_pipe_to_channel(crtc->pipe);
	enum dpio_phy phy = vlv_pipe_to_phy(crtc->pipe);
	enum pipe pipe = crtc->pipe;
	u32 tmp, coreclk;

	vlv_dpio_get(dev_priv);

	/* See eDP HDMI DPIO driver vbios notes doc */

	/* PLL B needs special handling */
	if (pipe == PIPE_B)
		vlv_pllb_recal_opamp(dev_priv, phy, ch);

	/* Set up Tx target for periodic Rcomp update */
	vlv_dpio_write(dev_priv, phy, VLV_PCS_DW17_BCAST, 0x0100000f);

	/* Disable target IRef on PLL */
	tmp = vlv_dpio_read(dev_priv, phy, VLV_PLL_DW16(ch));
	tmp &= 0x00ffffff;
	vlv_dpio_write(dev_priv, phy, VLV_PLL_DW16(ch), tmp);

	/* Disable fast lock */
	vlv_dpio_write(dev_priv, phy, VLV_CMN_DW0, 0x610);

	/* Set idtafcrecal before PLL is enabled */
	tmp = DPIO_M1_DIV(clock->m1) |
		DPIO_M2_DIV(clock->m2) |
		DPIO_P1_DIV(clock->p1) |
		DPIO_P2_DIV(clock->p2) |
		DPIO_N_DIV(clock->n) |
		DPIO_K_DIV(1);

	/*
	 * Post divider depends on pixel clock rate, DAC vs digital (and LVDS,
	 * but we don't support that).
	 * Note: don't use the DAC post divider as it seems unstable.
	 */
	tmp |= DPIO_S1_DIV(DPIO_S1_DIV_HDMIDP);
	vlv_dpio_write(dev_priv, phy, VLV_PLL_DW3(ch), tmp);

	tmp |= DPIO_ENABLE_CALIBRATION;
	vlv_dpio_write(dev_priv, phy, VLV_PLL_DW3(ch), tmp);

	/* Set HBR and RBR LPF coefficients */
	if (crtc_state->port_clock == 162000 ||
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_ANALOG) ||
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		vlv_dpio_write(dev_priv, phy, VLV_PLL_DW18(ch),
				 0x009f0003);
	else
		vlv_dpio_write(dev_priv, phy, VLV_PLL_DW18(ch),
				 0x00d0000f);

	if (intel_crtc_has_dp_encoder(crtc_state)) {
		/* Use SSC source */
		if (pipe == PIPE_A)
			vlv_dpio_write(dev_priv, phy, VLV_PLL_DW5(ch),
					 0x0df40000);
		else
			vlv_dpio_write(dev_priv, phy, VLV_PLL_DW5(ch),
					 0x0df70000);
	} else { /* HDMI or VGA */
		/* Use bend source */
		if (pipe == PIPE_A)
			vlv_dpio_write(dev_priv, phy, VLV_PLL_DW5(ch),
					 0x0df70000);
		else
			vlv_dpio_write(dev_priv, phy, VLV_PLL_DW5(ch),
					 0x0df40000);
	}

	coreclk = vlv_dpio_read(dev_priv, phy, VLV_PLL_DW7(ch));
	coreclk = (coreclk & 0x0000ff00) | 0x01c00000;
	if (intel_crtc_has_dp_encoder(crtc_state))
		coreclk |= 0x01000000;
	vlv_dpio_write(dev_priv, phy, VLV_PLL_DW7(ch), coreclk);

	vlv_dpio_write(dev_priv, phy, VLV_PLL_DW19(ch), 0x87871000);

	vlv_dpio_put(dev_priv);
}

static void _vlv_enable_pll(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;
	enum pipe pipe = crtc->pipe;

	intel_de_write(dev_priv, DPLL(dev_priv, pipe), hw_state->dpll);
	intel_de_posting_read(dev_priv, DPLL(dev_priv, pipe));
	udelay(150);

	if (intel_de_wait_for_set(dev_priv, DPLL(dev_priv, pipe), DPLL_LOCK_VLV, 1))
		drm_err(&dev_priv->drm, "DPLL %d failed to lock\n", pipe);
}

void vlv_enable_pll(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;
	enum pipe pipe = crtc->pipe;

	assert_transcoder_disabled(dev_priv, crtc_state->cpu_transcoder);

	/* PLL is protected by panel, make sure we can write it */
	assert_pps_unlocked(display, pipe);

	/* Enable Refclk */
	intel_de_write(dev_priv, DPLL(dev_priv, pipe),
		       hw_state->dpll & ~(DPLL_VCO_ENABLE | DPLL_EXT_BUFFER_ENABLE_VLV));

	if (hw_state->dpll & DPLL_VCO_ENABLE) {
		vlv_prepare_pll(crtc_state);
		_vlv_enable_pll(crtc_state);
	}

	intel_de_write(dev_priv, DPLL_MD(dev_priv, pipe), hw_state->dpll_md);
	intel_de_posting_read(dev_priv, DPLL_MD(dev_priv, pipe));
}

static void chv_prepare_pll(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct dpll *clock = &crtc_state->dpll;
	enum dpio_channel ch = vlv_pipe_to_channel(crtc->pipe);
	enum dpio_phy phy = vlv_pipe_to_phy(crtc->pipe);
	u32 tmp, loopfilter, tribuf_calcntr;
	u32 m2_frac;

	m2_frac = clock->m2 & 0x3fffff;

	vlv_dpio_get(dev_priv);

	/* p1 and p2 divider */
	vlv_dpio_write(dev_priv, phy, CHV_CMN_DW13(ch),
		       DPIO_CHV_S1_DIV(5) |
		       DPIO_CHV_P1_DIV(clock->p1) |
		       DPIO_CHV_P2_DIV(clock->p2) |
		       DPIO_CHV_K_DIV(1));

	/* Feedback post-divider - m2 */
	vlv_dpio_write(dev_priv, phy, CHV_PLL_DW0(ch),
		       DPIO_CHV_M2_DIV(clock->m2 >> 22));

	/* Feedback refclk divider - n and m1 */
	vlv_dpio_write(dev_priv, phy, CHV_PLL_DW1(ch),
		       DPIO_CHV_M1_DIV(DPIO_CHV_M1_DIV_BY_2) |
		       DPIO_CHV_N_DIV(1));

	/* M2 fraction division */
	vlv_dpio_write(dev_priv, phy, CHV_PLL_DW2(ch),
		       DPIO_CHV_M2_FRAC_DIV(m2_frac));

	/* M2 fraction division enable */
	tmp = vlv_dpio_read(dev_priv, phy, CHV_PLL_DW3(ch));
	tmp &= ~(DPIO_CHV_FEEDFWD_GAIN_MASK | DPIO_CHV_FRAC_DIV_EN);
	tmp |= DPIO_CHV_FEEDFWD_GAIN(2);
	if (m2_frac)
		tmp |= DPIO_CHV_FRAC_DIV_EN;
	vlv_dpio_write(dev_priv, phy, CHV_PLL_DW3(ch), tmp);

	/* Program digital lock detect threshold */
	tmp = vlv_dpio_read(dev_priv, phy, CHV_PLL_DW9(ch));
	tmp &= ~(DPIO_CHV_INT_LOCK_THRESHOLD_MASK |
		      DPIO_CHV_INT_LOCK_THRESHOLD_SEL_COARSE);
	tmp |= DPIO_CHV_INT_LOCK_THRESHOLD(0x5);
	if (!m2_frac)
		tmp |= DPIO_CHV_INT_LOCK_THRESHOLD_SEL_COARSE;
	vlv_dpio_write(dev_priv, phy, CHV_PLL_DW9(ch), tmp);

	/* Loop filter */
	if (clock->vco == 5400000) {
		loopfilter = DPIO_CHV_PROP_COEFF(0x3) |
			DPIO_CHV_INT_COEFF(0x8) |
			DPIO_CHV_GAIN_CTRL(0x1);
		tribuf_calcntr = 0x9;
	} else if (clock->vco <= 6200000) {
		loopfilter = DPIO_CHV_PROP_COEFF(0x5) |
			DPIO_CHV_INT_COEFF(0xB) |
			DPIO_CHV_GAIN_CTRL(0x3);
		tribuf_calcntr = 0x9;
	} else if (clock->vco <= 6480000) {
		loopfilter = DPIO_CHV_PROP_COEFF(0x4) |
			DPIO_CHV_INT_COEFF(0x9) |
			DPIO_CHV_GAIN_CTRL(0x3);
		tribuf_calcntr = 0x8;
	} else {
		/* Not supported. Apply the same limits as in the max case */
		loopfilter = DPIO_CHV_PROP_COEFF(0x4) |
			DPIO_CHV_INT_COEFF(0x9) |
			DPIO_CHV_GAIN_CTRL(0x3);
		tribuf_calcntr = 0;
	}
	vlv_dpio_write(dev_priv, phy, CHV_PLL_DW6(ch), loopfilter);

	tmp = vlv_dpio_read(dev_priv, phy, CHV_PLL_DW8(ch));
	tmp &= ~DPIO_CHV_TDC_TARGET_CNT_MASK;
	tmp |= DPIO_CHV_TDC_TARGET_CNT(tribuf_calcntr);
	vlv_dpio_write(dev_priv, phy, CHV_PLL_DW8(ch), tmp);

	/* AFC Recal */
	vlv_dpio_write(dev_priv, phy, CHV_CMN_DW14(ch),
		       vlv_dpio_read(dev_priv, phy, CHV_CMN_DW14(ch)) |
		       DPIO_AFC_RECAL);

	vlv_dpio_put(dev_priv);
}

static void _chv_enable_pll(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;
	enum dpio_channel ch = vlv_pipe_to_channel(crtc->pipe);
	enum dpio_phy phy = vlv_pipe_to_phy(crtc->pipe);
	enum pipe pipe = crtc->pipe;
	u32 tmp;

	vlv_dpio_get(dev_priv);

	/* Enable back the 10bit clock to display controller */
	tmp = vlv_dpio_read(dev_priv, phy, CHV_CMN_DW14(ch));
	tmp |= DPIO_DCLKP_EN;
	vlv_dpio_write(dev_priv, phy, CHV_CMN_DW14(ch), tmp);

	vlv_dpio_put(dev_priv);

	/*
	 * Need to wait > 100ns between dclkp clock enable bit and PLL enable.
	 */
	udelay(1);

	/* Enable PLL */
	intel_de_write(dev_priv, DPLL(dev_priv, pipe), hw_state->dpll);

	/* Check PLL is locked */
	if (intel_de_wait_for_set(dev_priv, DPLL(dev_priv, pipe), DPLL_LOCK_VLV, 1))
		drm_err(&dev_priv->drm, "PLL %d failed to lock\n", pipe);
}

void chv_enable_pll(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct i9xx_dpll_hw_state *hw_state = &crtc_state->dpll_hw_state.i9xx;
	enum pipe pipe = crtc->pipe;

	assert_transcoder_disabled(dev_priv, crtc_state->cpu_transcoder);

	/* PLL is protected by panel, make sure we can write it */
	assert_pps_unlocked(display, pipe);

	/* Enable Refclk and SSC */
	intel_de_write(dev_priv, DPLL(dev_priv, pipe),
		       hw_state->dpll & ~DPLL_VCO_ENABLE);

	if (hw_state->dpll & DPLL_VCO_ENABLE) {
		chv_prepare_pll(crtc_state);
		_chv_enable_pll(crtc_state);
	}

	if (pipe != PIPE_A) {
		/*
		 * WaPixelRepeatModeFixForC0:chv
		 *
		 * DPLLCMD is AWOL. Use chicken bits to propagate
		 * the value from DPLLBMD to either pipe B or C.
		 */
		intel_de_write(dev_priv, CBR4_VLV, CBR_DPLLBMD_PIPE(pipe));
		intel_de_write(dev_priv, DPLL_MD(dev_priv, PIPE_B),
			       hw_state->dpll_md);
		intel_de_write(dev_priv, CBR4_VLV, 0);
		dev_priv->display.state.chv_dpll_md[pipe] = hw_state->dpll_md;

		/*
		 * DPLLB VGA mode also seems to cause problems.
		 * We should always have it disabled.
		 */
		drm_WARN_ON(&dev_priv->drm,
			    (intel_de_read(dev_priv, DPLL(dev_priv, PIPE_B)) &
			     DPLL_VGA_MODE_DIS) == 0);
	} else {
		intel_de_write(dev_priv, DPLL_MD(dev_priv, pipe),
			       hw_state->dpll_md);
		intel_de_posting_read(dev_priv, DPLL_MD(dev_priv, pipe));
	}
}

/**
 * vlv_force_pll_on - forcibly enable just the PLL
 * @dev_priv: i915 private structure
 * @pipe: pipe PLL to enable
 * @dpll: PLL configuration
 *
 * Enable the PLL for @pipe using the supplied @dpll config. To be used
 * in cases where we need the PLL enabled even when @pipe is not going to
 * be enabled.
 */
int vlv_force_pll_on(struct drm_i915_private *dev_priv, enum pipe pipe,
		     const struct dpll *dpll)
{
	struct intel_display *display = &dev_priv->display;
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);
	struct intel_crtc_state *crtc_state;

	crtc_state = intel_crtc_state_alloc(crtc);
	if (!crtc_state)
		return -ENOMEM;

	crtc_state->cpu_transcoder = (enum transcoder)pipe;
	crtc_state->pixel_multiplier = 1;
	crtc_state->dpll = *dpll;
	crtc_state->output_types = BIT(INTEL_OUTPUT_EDP);

	if (IS_CHERRYVIEW(dev_priv)) {
		chv_compute_dpll(crtc_state);
		chv_enable_pll(crtc_state);
	} else {
		vlv_compute_dpll(crtc_state);
		vlv_enable_pll(crtc_state);
	}

	intel_crtc_destroy_state(&crtc->base, &crtc_state->uapi);

	return 0;
}

void vlv_disable_pll(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	u32 val;

	/* Make sure the pipe isn't still relying on us */
	assert_transcoder_disabled(dev_priv, (enum transcoder)pipe);

	val = DPLL_INTEGRATED_REF_CLK_VLV |
		DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS;
	if (pipe != PIPE_A)
		val |= DPLL_INTEGRATED_CRI_CLK_VLV;

	intel_de_write(dev_priv, DPLL(dev_priv, pipe), val);
	intel_de_posting_read(dev_priv, DPLL(dev_priv, pipe));
}

void chv_disable_pll(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	enum dpio_channel ch = vlv_pipe_to_channel(pipe);
	enum dpio_phy phy = vlv_pipe_to_phy(pipe);
	u32 val;

	/* Make sure the pipe isn't still relying on us */
	assert_transcoder_disabled(dev_priv, (enum transcoder)pipe);

	val = DPLL_SSC_REF_CLK_CHV |
		DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS;
	if (pipe != PIPE_A)
		val |= DPLL_INTEGRATED_CRI_CLK_VLV;

	intel_de_write(dev_priv, DPLL(dev_priv, pipe), val);
	intel_de_posting_read(dev_priv, DPLL(dev_priv, pipe));

	vlv_dpio_get(dev_priv);

	/* Disable 10bit clock to display controller */
	val = vlv_dpio_read(dev_priv, phy, CHV_CMN_DW14(ch));
	val &= ~DPIO_DCLKP_EN;
	vlv_dpio_write(dev_priv, phy, CHV_CMN_DW14(ch), val);

	vlv_dpio_put(dev_priv);
}

void i9xx_disable_pll(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	/* Don't disable pipe or pipe PLLs if needed */
	if (IS_I830(dev_priv))
		return;

	/* Make sure the pipe isn't still relying on us */
	assert_transcoder_disabled(dev_priv, crtc_state->cpu_transcoder);

	intel_de_write(dev_priv, DPLL(dev_priv, pipe), DPLL_VGA_MODE_DIS);
	intel_de_posting_read(dev_priv, DPLL(dev_priv, pipe));
}


/**
 * vlv_force_pll_off - forcibly disable just the PLL
 * @dev_priv: i915 private structure
 * @pipe: pipe PLL to disable
 *
 * Disable the PLL for @pipe. To be used in cases where we need
 * the PLL enabled even when @pipe is not going to be enabled.
 */
void vlv_force_pll_off(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	if (IS_CHERRYVIEW(dev_priv))
		chv_disable_pll(dev_priv, pipe);
	else
		vlv_disable_pll(dev_priv, pipe);
}

/* Only for pre-ILK configs */
static void assert_pll(struct drm_i915_private *dev_priv,
		       enum pipe pipe, bool state)
{
	struct intel_display *display = &dev_priv->display;
	bool cur_state;

	cur_state = intel_de_read(display, DPLL(display, pipe)) & DPLL_VCO_ENABLE;
	INTEL_DISPLAY_STATE_WARN(display, cur_state != state,
				 "PLL state assertion failure (expected %s, current %s)\n",
				 str_on_off(state), str_on_off(cur_state));
}

void assert_pll_enabled(struct drm_i915_private *i915, enum pipe pipe)
{
	assert_pll(i915, pipe, true);
}

void assert_pll_disabled(struct drm_i915_private *i915, enum pipe pipe)
{
	assert_pll(i915, pipe, false);
}
