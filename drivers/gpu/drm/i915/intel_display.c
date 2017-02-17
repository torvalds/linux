/*
 * Copyright © 2006-2007 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 */

#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vgaarb.h>
#include <drm/drm_edid.h>
#include <drm/drmP.h>
#include "intel_drv.h"
#include "intel_frontbuffer.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_gem_clflush.h"
#include "intel_dsi.h"
#include "i915_trace.h"
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_rect.h>
#include <linux/dma_remapping.h>
#include <linux/reservation.h>

static bool is_mmio_work(struct intel_flip_work *work)
{
	return work->mmio_work.func;
}

/* Primary plane formats for gen <= 3 */
static const uint32_t i8xx_primary_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XRGB8888,
};

/* Primary plane formats for gen >= 4 */
static const uint32_t i965_primary_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
};

static const uint32_t skl_primary_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

/* Cursor formats */
static const uint32_t intel_cursor_formats[] = {
	DRM_FORMAT_ARGB8888,
};

static void i9xx_crtc_clock_get(struct intel_crtc *crtc,
				struct intel_crtc_state *pipe_config);
static void ironlake_pch_clock_get(struct intel_crtc *crtc,
				   struct intel_crtc_state *pipe_config);

static int intel_framebuffer_init(struct intel_framebuffer *ifb,
				  struct drm_i915_gem_object *obj,
				  struct drm_mode_fb_cmd2 *mode_cmd);
static void i9xx_set_pipeconf(struct intel_crtc *intel_crtc);
static void intel_set_pipe_timings(struct intel_crtc *intel_crtc);
static void intel_set_pipe_src_size(struct intel_crtc *intel_crtc);
static void intel_cpu_transcoder_set_m_n(struct intel_crtc *crtc,
					 struct intel_link_m_n *m_n,
					 struct intel_link_m_n *m2_n2);
static void ironlake_set_pipeconf(struct drm_crtc *crtc);
static void haswell_set_pipeconf(struct drm_crtc *crtc);
static void haswell_set_pipemisc(struct drm_crtc *crtc);
static void vlv_prepare_pll(struct intel_crtc *crtc,
			    const struct intel_crtc_state *pipe_config);
static void chv_prepare_pll(struct intel_crtc *crtc,
			    const struct intel_crtc_state *pipe_config);
static void intel_begin_crtc_commit(struct drm_crtc *, struct drm_crtc_state *);
static void intel_finish_crtc_commit(struct drm_crtc *, struct drm_crtc_state *);
static void intel_crtc_init_scalers(struct intel_crtc *crtc,
				    struct intel_crtc_state *crtc_state);
static void skylake_pfit_enable(struct intel_crtc *crtc);
static void ironlake_pfit_disable(struct intel_crtc *crtc, bool force);
static void ironlake_pfit_enable(struct intel_crtc *crtc);
static void intel_modeset_setup_hw_state(struct drm_device *dev);
static void intel_pre_disable_primary_noatomic(struct drm_crtc *crtc);

struct intel_limit {
	struct {
		int min, max;
	} dot, vco, n, m, m1, m2, p, p1;

	struct {
		int dot_limit;
		int p2_slow, p2_fast;
	} p2;
};

/* returns HPLL frequency in kHz */
int vlv_get_hpll_vco(struct drm_i915_private *dev_priv)
{
	int hpll_freq, vco_freq[] = { 800, 1600, 2000, 2400 };

	/* Obtain SKU information */
	mutex_lock(&dev_priv->sb_lock);
	hpll_freq = vlv_cck_read(dev_priv, CCK_FUSE_REG) &
		CCK_FUSE_HPLL_FREQ_MASK;
	mutex_unlock(&dev_priv->sb_lock);

	return vco_freq[hpll_freq] * 1000;
}

int vlv_get_cck_clock(struct drm_i915_private *dev_priv,
		      const char *name, u32 reg, int ref_freq)
{
	u32 val;
	int divider;

	mutex_lock(&dev_priv->sb_lock);
	val = vlv_cck_read(dev_priv, reg);
	mutex_unlock(&dev_priv->sb_lock);

	divider = val & CCK_FREQUENCY_VALUES;

	WARN((val & CCK_FREQUENCY_STATUS) !=
	     (divider << CCK_FREQUENCY_STATUS_SHIFT),
	     "%s change in progress\n", name);

	return DIV_ROUND_CLOSEST(ref_freq << 1, divider + 1);
}

int vlv_get_cck_clock_hpll(struct drm_i915_private *dev_priv,
			   const char *name, u32 reg)
{
	if (dev_priv->hpll_freq == 0)
		dev_priv->hpll_freq = vlv_get_hpll_vco(dev_priv);

	return vlv_get_cck_clock(dev_priv, name, reg,
				 dev_priv->hpll_freq);
}

static void intel_update_czclk(struct drm_i915_private *dev_priv)
{
	if (!(IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)))
		return;

	dev_priv->czclk_freq = vlv_get_cck_clock_hpll(dev_priv, "czclk",
						      CCK_CZ_CLOCK_CONTROL);

	DRM_DEBUG_DRIVER("CZ clock rate: %d kHz\n", dev_priv->czclk_freq);
}

static inline u32 /* units of 100MHz */
intel_fdi_link_freq(struct drm_i915_private *dev_priv,
		    const struct intel_crtc_state *pipe_config)
{
	if (HAS_DDI(dev_priv))
		return pipe_config->port_clock; /* SPLL */
	else if (IS_GEN5(dev_priv))
		return ((I915_READ(FDI_PLL_BIOS_0) & FDI_PLL_FB_CLOCK_MASK) + 2) * 10000;
	else
		return 270000;
}

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

static const struct intel_limit intel_limits_pineview_sdvo = {
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

static const struct intel_limit intel_limits_pineview_lvds = {
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
static const struct intel_limit intel_limits_ironlake_dac = {
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

static const struct intel_limit intel_limits_ironlake_single_lvds = {
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

static const struct intel_limit intel_limits_ironlake_dual_lvds = {
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
static const struct intel_limit intel_limits_ironlake_single_lvds_100m = {
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

static const struct intel_limit intel_limits_ironlake_dual_lvds_100m = {
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
	  * These are the data rate limits (measured in fast clocks)
	  * since those are the strictest limits we have. The fast
	  * clock and actual rate limits are more relaxed, so checking
	  * them would make no difference.
	  */
	.dot = { .min = 25000 * 5, .max = 270000 * 5 },
	.vco = { .min = 4000000, .max = 6000000 },
	.n = { .min = 1, .max = 7 },
	.m1 = { .min = 2, .max = 3 },
	.m2 = { .min = 11, .max = 156 },
	.p1 = { .min = 2, .max = 3 },
	.p2 = { .p2_slow = 2, .p2_fast = 20 }, /* slow=min, fast=max */
};

static const struct intel_limit intel_limits_chv = {
	/*
	 * These are the data rate limits (measured in fast clocks)
	 * since those are the strictest limits we have.  The fast
	 * clock and actual rate limits are more relaxed, so checking
	 * them would make no difference.
	 */
	.dot = { .min = 25000 * 5, .max = 540000 * 5},
	.vco = { .min = 4800000, .max = 6480000 },
	.n = { .min = 1, .max = 1 },
	.m1 = { .min = 2, .max = 2 },
	.m2 = { .min = 24 << 22, .max = 175 << 22 },
	.p1 = { .min = 2, .max = 4 },
	.p2 = {	.p2_slow = 1, .p2_fast = 14 },
};

static const struct intel_limit intel_limits_bxt = {
	/* FIXME: find real dot limits */
	.dot = { .min = 0, .max = INT_MAX },
	.vco = { .min = 4800000, .max = 6700000 },
	.n = { .min = 1, .max = 1 },
	.m1 = { .min = 2, .max = 2 },
	/* FIXME: find real m2 limits */
	.m2 = { .min = 2 << 22, .max = 255 << 22 },
	.p1 = { .min = 2, .max = 4 },
	.p2 = { .p2_slow = 1, .p2_fast = 20 },
};

static bool
needs_modeset(struct drm_crtc_state *state)
{
	return drm_atomic_crtc_needs_modeset(state);
}

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
	if (WARN_ON(clock->n == 0 || clock->p == 0))
		return 0;
	clock->vco = DIV_ROUND_CLOSEST(refclk * clock->m, clock->n);
	clock->dot = DIV_ROUND_CLOSEST(clock->vco, clock->p);

	return clock->dot;
}

static uint32_t i9xx_dpll_compute_m(struct dpll *dpll)
{
	return 5 * (dpll->m1 + 2) + (dpll->m2 + 2);
}

static int i9xx_calc_dpll_params(int refclk, struct dpll *clock)
{
	clock->m = i9xx_dpll_compute_m(clock);
	clock->p = clock->p1 * clock->p2;
	if (WARN_ON(clock->n + 2 == 0 || clock->p == 0))
		return 0;
	clock->vco = DIV_ROUND_CLOSEST(refclk * clock->m, clock->n + 2);
	clock->dot = DIV_ROUND_CLOSEST(clock->vco, clock->p);

	return clock->dot;
}

static int vlv_calc_dpll_params(int refclk, struct dpll *clock)
{
	clock->m = clock->m1 * clock->m2;
	clock->p = clock->p1 * clock->p2;
	if (WARN_ON(clock->n == 0 || clock->p == 0))
		return 0;
	clock->vco = DIV_ROUND_CLOSEST(refclk * clock->m, clock->n);
	clock->dot = DIV_ROUND_CLOSEST(clock->vco, clock->p);

	return clock->dot / 5;
}

int chv_calc_dpll_params(int refclk, struct dpll *clock)
{
	clock->m = clock->m1 * clock->m2;
	clock->p = clock->p1 * clock->p2;
	if (WARN_ON(clock->n == 0 || clock->p == 0))
		return 0;
	clock->vco = DIV_ROUND_CLOSEST_ULL((uint64_t)refclk * clock->m,
			clock->n << 22);
	clock->dot = DIV_ROUND_CLOSEST(clock->vco, clock->p);

	return clock->dot / 5;
}

#define INTELPllInvalid(s)   do { /* DRM_DEBUG(s); */ return false; } while (0)
/**
 * Returns whether the given set of divisors are valid for a given refclk with
 * the given connectors.
 */

static bool intel_PLL_is_valid(struct drm_i915_private *dev_priv,
			       const struct intel_limit *limit,
			       const struct dpll *clock)
{
	if (clock->n   < limit->n.min   || limit->n.max   < clock->n)
		INTELPllInvalid("n out of range\n");
	if (clock->p1  < limit->p1.min  || limit->p1.max  < clock->p1)
		INTELPllInvalid("p1 out of range\n");
	if (clock->m2  < limit->m2.min  || limit->m2.max  < clock->m2)
		INTELPllInvalid("m2 out of range\n");
	if (clock->m1  < limit->m1.min  || limit->m1.max  < clock->m1)
		INTELPllInvalid("m1 out of range\n");

	if (!IS_PINEVIEW(dev_priv) && !IS_VALLEYVIEW(dev_priv) &&
	    !IS_CHERRYVIEW(dev_priv) && !IS_GEN9_LP(dev_priv))
		if (clock->m1 <= clock->m2)
			INTELPllInvalid("m1 <= m2\n");

	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv) &&
	    !IS_GEN9_LP(dev_priv)) {
		if (clock->p < limit->p.min || limit->p.max < clock->p)
			INTELPllInvalid("p out of range\n");
		if (clock->m < limit->m.min || limit->m.max < clock->m)
			INTELPllInvalid("m out of range\n");
	}

	if (clock->vco < limit->vco.min || limit->vco.max < clock->vco)
		INTELPllInvalid("vco out of range\n");
	/* XXX: We may need to be checking "Dot clock" depending on the multiplier,
	 * connector, etc., rather than just a single range.
	 */
	if (clock->dot < limit->dot.min || limit->dot.max < clock->dot)
		INTELPllInvalid("dot out of range\n");

	return true;
}

static int
i9xx_select_p2_div(const struct intel_limit *limit,
		   const struct intel_crtc_state *crtc_state,
		   int target)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		/*
		 * For LVDS just rely on its current settings for dual-channel.
		 * We haven't figured out how to reliably set up different
		 * single/dual channel state, if we even can.
		 */
		if (intel_is_dual_link_lvds(dev))
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
 * refclk, or FALSE.  The returned values represent the clock equation:
 * reflck * (5 * (m1 + 2) + (m2 + 2)) / (n + 2) / p1 / p2.
 *
 * Target and reference clocks are specified in kHz.
 *
 * If match_clock is provided, then best_clock P divider must match the P
 * divider from @match_clock used for LVDS downclocking.
 */
static bool
i9xx_find_best_dpll(const struct intel_limit *limit,
		    struct intel_crtc_state *crtc_state,
		    int target, int refclk, struct dpll *match_clock,
		    struct dpll *best_clock)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;
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
					if (!intel_PLL_is_valid(to_i915(dev),
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
 * refclk, or FALSE.  The returned values represent the clock equation:
 * reflck * (5 * (m1 + 2) + (m2 + 2)) / (n + 2) / p1 / p2.
 *
 * Target and reference clocks are specified in kHz.
 *
 * If match_clock is provided, then best_clock P divider must match the P
 * divider from @match_clock used for LVDS downclocking.
 */
static bool
pnv_find_best_dpll(const struct intel_limit *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk, struct dpll *match_clock,
		   struct dpll *best_clock)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;
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
					if (!intel_PLL_is_valid(to_i915(dev),
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
 * refclk, or FALSE.  The returned values represent the clock equation:
 * reflck * (5 * (m1 + 2) + (m2 + 2)) / (n + 2) / p1 / p2.
 *
 * Target and reference clocks are specified in kHz.
 *
 * If match_clock is provided, then best_clock P divider must match the P
 * divider from @match_clock used for LVDS downclocking.
 */
static bool
g4x_find_best_dpll(const struct intel_limit *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk, struct dpll *match_clock,
		   struct dpll *best_clock)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;
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
		/* based on hardware requirement, prefere larger m1,m2 */
		for (clock.m1 = limit->m1.max;
		     clock.m1 >= limit->m1.min; clock.m1--) {
			for (clock.m2 = limit->m2.max;
			     clock.m2 >= limit->m2.min; clock.m2--) {
				for (clock.p1 = limit->p1.max;
				     clock.p1 >= limit->p1.min; clock.p1--) {
					int this_err;

					i9xx_calc_dpll_params(refclk, &clock);
					if (!intel_PLL_is_valid(to_i915(dev),
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

	if (WARN_ON_ONCE(!target_freq))
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
 * refclk, or FALSE.  The returned values represent the clock equation:
 * reflck * (5 * (m1 + 2) + (m2 + 2)) / (n + 2) / p1 / p2.
 */
static bool
vlv_find_best_dpll(const struct intel_limit *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk, struct dpll *match_clock,
		   struct dpll *best_clock)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_device *dev = crtc->base.dev;
	struct dpll clock;
	unsigned int bestppm = 1000000;
	/* min update 19.2 MHz */
	int max_n = min(limit->n.max, refclk / 19200);
	bool found = false;

	target *= 5; /* fast clock */

	memset(best_clock, 0, sizeof(*best_clock));

	/* based on hardware requirement, prefer smaller n to precision */
	for (clock.n = limit->n.min; clock.n <= max_n; clock.n++) {
		for (clock.p1 = limit->p1.max; clock.p1 >= limit->p1.min; clock.p1--) {
			for (clock.p2 = limit->p2.p2_fast; clock.p2 >= limit->p2.p2_slow;
			     clock.p2 -= clock.p2 > 10 ? 2 : 1) {
				clock.p = clock.p1 * clock.p2;
				/* based on hardware requirement, prefer bigger m1,m2 values */
				for (clock.m1 = limit->m1.min; clock.m1 <= limit->m1.max; clock.m1++) {
					unsigned int ppm;

					clock.m2 = DIV_ROUND_CLOSEST(target * clock.p * clock.n,
								     refclk * clock.m1);

					vlv_calc_dpll_params(refclk, &clock);

					if (!intel_PLL_is_valid(to_i915(dev),
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
 * refclk, or FALSE.  The returned values represent the clock equation:
 * reflck * (5 * (m1 + 2) + (m2 + 2)) / (n + 2) / p1 / p2.
 */
static bool
chv_find_best_dpll(const struct intel_limit *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk, struct dpll *match_clock,
		   struct dpll *best_clock)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_device *dev = crtc->base.dev;
	unsigned int best_error_ppm;
	struct dpll clock;
	uint64_t m2;
	int found = false;

	memset(best_clock, 0, sizeof(*best_clock));
	best_error_ppm = 1000000;

	/*
	 * Based on hardware doc, the n always set to 1, and m1 always
	 * set to 2.  If requires to support 200Mhz refclk, we need to
	 * revisit this because n may not 1 anymore.
	 */
	clock.n = 1, clock.m1 = 2;
	target *= 5;	/* fast clock */

	for (clock.p1 = limit->p1.max; clock.p1 >= limit->p1.min; clock.p1--) {
		for (clock.p2 = limit->p2.p2_fast;
				clock.p2 >= limit->p2.p2_slow;
				clock.p2 -= clock.p2 > 10 ? 2 : 1) {
			unsigned int error_ppm;

			clock.p = clock.p1 * clock.p2;

			m2 = DIV_ROUND_CLOSEST_ULL(((uint64_t)target * clock.p *
					clock.n) << 22, refclk * clock.m1);

			if (m2 > INT_MAX/clock.m1)
				continue;

			clock.m2 = m2;

			chv_calc_dpll_params(refclk, &clock);

			if (!intel_PLL_is_valid(to_i915(dev), limit, &clock))
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

bool bxt_find_best_dpll(struct intel_crtc_state *crtc_state, int target_clock,
			struct dpll *best_clock)
{
	int refclk = 100000;
	const struct intel_limit *limit = &intel_limits_bxt;

	return chv_find_best_dpll(limit, crtc_state,
				  target_clock, refclk, NULL, best_clock);
}

bool intel_crtc_active(struct intel_crtc *crtc)
{
	/* Be paranoid as we can arrive here with only partial
	 * state retrieved from the hardware during setup.
	 *
	 * We can ditch the adjusted_mode.crtc_clock check as soon
	 * as Haswell has gained clock readout/fastboot support.
	 *
	 * We can ditch the crtc->primary->fb check as soon as we can
	 * properly reconstruct framebuffers.
	 *
	 * FIXME: The intel_crtc->active here should be switched to
	 * crtc->state->active once we have proper CRTC states wired up
	 * for atomic.
	 */
	return crtc->active && crtc->base.primary->state->fb &&
		crtc->config->base.adjusted_mode.crtc_clock;
}

enum transcoder intel_pipe_to_cpu_transcoder(struct drm_i915_private *dev_priv,
					     enum pipe pipe)
{
	struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv, pipe);

	return crtc->config->cpu_transcoder;
}

static bool pipe_dsl_stopped(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	i915_reg_t reg = PIPEDSL(pipe);
	u32 line1, line2;
	u32 line_mask;

	if (IS_GEN2(dev_priv))
		line_mask = DSL_LINEMASK_GEN2;
	else
		line_mask = DSL_LINEMASK_GEN3;

	line1 = I915_READ(reg) & line_mask;
	msleep(5);
	line2 = I915_READ(reg) & line_mask;

	return line1 == line2;
}

/*
 * intel_wait_for_pipe_off - wait for pipe to turn off
 * @crtc: crtc whose pipe to wait for
 *
 * After disabling a pipe, we can't wait for vblank in the usual way,
 * spinning on the vblank interrupt status bit, since we won't actually
 * see an interrupt when the pipe is disabled.
 *
 * On Gen4 and above:
 *   wait for the pipe register state bit to turn off
 *
 * Otherwise:
 *   wait for the display line value to settle (it usually
 *   ends up stopping at the start of the next frame).
 *
 */
static void intel_wait_for_pipe_off(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc->config->cpu_transcoder;
	enum pipe pipe = crtc->pipe;

	if (INTEL_GEN(dev_priv) >= 4) {
		i915_reg_t reg = PIPECONF(cpu_transcoder);

		/* Wait for the Pipe State to go off */
		if (intel_wait_for_register(dev_priv,
					    reg, I965_PIPECONF_ACTIVE, 0,
					    100))
			WARN(1, "pipe_off wait timed out\n");
	} else {
		/* Wait for the display line to settle */
		if (wait_for(pipe_dsl_stopped(dev_priv, pipe), 100))
			WARN(1, "pipe_off wait timed out\n");
	}
}

/* Only for pre-ILK configs */
void assert_pll(struct drm_i915_private *dev_priv,
		enum pipe pipe, bool state)
{
	u32 val;
	bool cur_state;

	val = I915_READ(DPLL(pipe));
	cur_state = !!(val & DPLL_VCO_ENABLE);
	I915_STATE_WARN(cur_state != state,
	     "PLL state assertion failure (expected %s, current %s)\n",
			onoff(state), onoff(cur_state));
}

/* XXX: the dsi pll is shared between MIPI DSI ports */
void assert_dsi_pll(struct drm_i915_private *dev_priv, bool state)
{
	u32 val;
	bool cur_state;

	mutex_lock(&dev_priv->sb_lock);
	val = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	mutex_unlock(&dev_priv->sb_lock);

	cur_state = val & DSI_PLL_VCO_EN;
	I915_STATE_WARN(cur_state != state,
	     "DSI PLL state assertion failure (expected %s, current %s)\n",
			onoff(state), onoff(cur_state));
}

static void assert_fdi_tx(struct drm_i915_private *dev_priv,
			  enum pipe pipe, bool state)
{
	bool cur_state;
	enum transcoder cpu_transcoder = intel_pipe_to_cpu_transcoder(dev_priv,
								      pipe);

	if (HAS_DDI(dev_priv)) {
		/* DDI does not have a specific FDI_TX register */
		u32 val = I915_READ(TRANS_DDI_FUNC_CTL(cpu_transcoder));
		cur_state = !!(val & TRANS_DDI_FUNC_ENABLE);
	} else {
		u32 val = I915_READ(FDI_TX_CTL(pipe));
		cur_state = !!(val & FDI_TX_ENABLE);
	}
	I915_STATE_WARN(cur_state != state,
	     "FDI TX state assertion failure (expected %s, current %s)\n",
			onoff(state), onoff(cur_state));
}
#define assert_fdi_tx_enabled(d, p) assert_fdi_tx(d, p, true)
#define assert_fdi_tx_disabled(d, p) assert_fdi_tx(d, p, false)

static void assert_fdi_rx(struct drm_i915_private *dev_priv,
			  enum pipe pipe, bool state)
{
	u32 val;
	bool cur_state;

	val = I915_READ(FDI_RX_CTL(pipe));
	cur_state = !!(val & FDI_RX_ENABLE);
	I915_STATE_WARN(cur_state != state,
	     "FDI RX state assertion failure (expected %s, current %s)\n",
			onoff(state), onoff(cur_state));
}
#define assert_fdi_rx_enabled(d, p) assert_fdi_rx(d, p, true)
#define assert_fdi_rx_disabled(d, p) assert_fdi_rx(d, p, false)

static void assert_fdi_tx_pll_enabled(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	u32 val;

	/* ILK FDI PLL is always enabled */
	if (IS_GEN5(dev_priv))
		return;

	/* On Haswell, DDI ports are responsible for the FDI PLL setup */
	if (HAS_DDI(dev_priv))
		return;

	val = I915_READ(FDI_TX_CTL(pipe));
	I915_STATE_WARN(!(val & FDI_TX_PLL_ENABLE), "FDI TX PLL assertion failure, should be active but is disabled\n");
}

void assert_fdi_rx_pll(struct drm_i915_private *dev_priv,
		       enum pipe pipe, bool state)
{
	u32 val;
	bool cur_state;

	val = I915_READ(FDI_RX_CTL(pipe));
	cur_state = !!(val & FDI_RX_PLL_ENABLE);
	I915_STATE_WARN(cur_state != state,
	     "FDI RX PLL assertion failure (expected %s, current %s)\n",
			onoff(state), onoff(cur_state));
}

void assert_panel_unlocked(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	i915_reg_t pp_reg;
	u32 val;
	enum pipe panel_pipe = PIPE_A;
	bool locked = true;

	if (WARN_ON(HAS_DDI(dev_priv)))
		return;

	if (HAS_PCH_SPLIT(dev_priv)) {
		u32 port_sel;

		pp_reg = PP_CONTROL(0);
		port_sel = I915_READ(PP_ON_DELAYS(0)) & PANEL_PORT_SELECT_MASK;

		if (port_sel == PANEL_PORT_SELECT_LVDS &&
		    I915_READ(PCH_LVDS) & LVDS_PIPEB_SELECT)
			panel_pipe = PIPE_B;
		/* XXX: else fix for eDP */
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		/* presumably write lock depends on pipe, not port select */
		pp_reg = PP_CONTROL(pipe);
		panel_pipe = pipe;
	} else {
		pp_reg = PP_CONTROL(0);
		if (I915_READ(LVDS) & LVDS_PIPEB_SELECT)
			panel_pipe = PIPE_B;
	}

	val = I915_READ(pp_reg);
	if (!(val & PANEL_POWER_ON) ||
	    ((val & PANEL_UNLOCK_MASK) == PANEL_UNLOCK_REGS))
		locked = false;

	I915_STATE_WARN(panel_pipe == pipe && locked,
	     "panel assertion failure, pipe %c regs locked\n",
	     pipe_name(pipe));
}

static void assert_cursor(struct drm_i915_private *dev_priv,
			  enum pipe pipe, bool state)
{
	bool cur_state;

	if (IS_I845G(dev_priv) || IS_I865G(dev_priv))
		cur_state = I915_READ(CURCNTR(PIPE_A)) & CURSOR_ENABLE;
	else
		cur_state = I915_READ(CURCNTR(pipe)) & CURSOR_MODE;

	I915_STATE_WARN(cur_state != state,
	     "cursor on pipe %c assertion failure (expected %s, current %s)\n",
			pipe_name(pipe), onoff(state), onoff(cur_state));
}
#define assert_cursor_enabled(d, p) assert_cursor(d, p, true)
#define assert_cursor_disabled(d, p) assert_cursor(d, p, false)

void assert_pipe(struct drm_i915_private *dev_priv,
		 enum pipe pipe, bool state)
{
	bool cur_state;
	enum transcoder cpu_transcoder = intel_pipe_to_cpu_transcoder(dev_priv,
								      pipe);
	enum intel_display_power_domain power_domain;

	/* if we need the pipe quirk it must be always on */
	if ((pipe == PIPE_A && dev_priv->quirks & QUIRK_PIPEA_FORCE) ||
	    (pipe == PIPE_B && dev_priv->quirks & QUIRK_PIPEB_FORCE))
		state = true;

	power_domain = POWER_DOMAIN_TRANSCODER(cpu_transcoder);
	if (intel_display_power_get_if_enabled(dev_priv, power_domain)) {
		u32 val = I915_READ(PIPECONF(cpu_transcoder));
		cur_state = !!(val & PIPECONF_ENABLE);

		intel_display_power_put(dev_priv, power_domain);
	} else {
		cur_state = false;
	}

	I915_STATE_WARN(cur_state != state,
	     "pipe %c assertion failure (expected %s, current %s)\n",
			pipe_name(pipe), onoff(state), onoff(cur_state));
}

static void assert_plane(struct drm_i915_private *dev_priv,
			 enum plane plane, bool state)
{
	u32 val;
	bool cur_state;

	val = I915_READ(DSPCNTR(plane));
	cur_state = !!(val & DISPLAY_PLANE_ENABLE);
	I915_STATE_WARN(cur_state != state,
	     "plane %c assertion failure (expected %s, current %s)\n",
			plane_name(plane), onoff(state), onoff(cur_state));
}

#define assert_plane_enabled(d, p) assert_plane(d, p, true)
#define assert_plane_disabled(d, p) assert_plane(d, p, false)

static void assert_planes_disabled(struct drm_i915_private *dev_priv,
				   enum pipe pipe)
{
	int i;

	/* Primary planes are fixed to pipes on gen4+ */
	if (INTEL_GEN(dev_priv) >= 4) {
		u32 val = I915_READ(DSPCNTR(pipe));
		I915_STATE_WARN(val & DISPLAY_PLANE_ENABLE,
		     "plane %c assertion failure, should be disabled but not\n",
		     plane_name(pipe));
		return;
	}

	/* Need to check both planes against the pipe */
	for_each_pipe(dev_priv, i) {
		u32 val = I915_READ(DSPCNTR(i));
		enum pipe cur_pipe = (val & DISPPLANE_SEL_PIPE_MASK) >>
			DISPPLANE_SEL_PIPE_SHIFT;
		I915_STATE_WARN((val & DISPLAY_PLANE_ENABLE) && pipe == cur_pipe,
		     "plane %c assertion failure, should be off on pipe %c but is still active\n",
		     plane_name(i), pipe_name(pipe));
	}
}

static void assert_sprites_disabled(struct drm_i915_private *dev_priv,
				    enum pipe pipe)
{
	int sprite;

	if (INTEL_GEN(dev_priv) >= 9) {
		for_each_sprite(dev_priv, pipe, sprite) {
			u32 val = I915_READ(PLANE_CTL(pipe, sprite));
			I915_STATE_WARN(val & PLANE_CTL_ENABLE,
			     "plane %d assertion failure, should be off on pipe %c but is still active\n",
			     sprite, pipe_name(pipe));
		}
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		for_each_sprite(dev_priv, pipe, sprite) {
			u32 val = I915_READ(SPCNTR(pipe, PLANE_SPRITE0 + sprite));
			I915_STATE_WARN(val & SP_ENABLE,
			     "sprite %c assertion failure, should be off on pipe %c but is still active\n",
			     sprite_name(pipe, sprite), pipe_name(pipe));
		}
	} else if (INTEL_GEN(dev_priv) >= 7) {
		u32 val = I915_READ(SPRCTL(pipe));
		I915_STATE_WARN(val & SPRITE_ENABLE,
		     "sprite %c assertion failure, should be off on pipe %c but is still active\n",
		     plane_name(pipe), pipe_name(pipe));
	} else if (INTEL_GEN(dev_priv) >= 5) {
		u32 val = I915_READ(DVSCNTR(pipe));
		I915_STATE_WARN(val & DVS_ENABLE,
		     "sprite %c assertion failure, should be off on pipe %c but is still active\n",
		     plane_name(pipe), pipe_name(pipe));
	}
}

static void assert_vblank_disabled(struct drm_crtc *crtc)
{
	if (I915_STATE_WARN_ON(drm_crtc_vblank_get(crtc) == 0))
		drm_crtc_vblank_put(crtc);
}

void assert_pch_transcoder_disabled(struct drm_i915_private *dev_priv,
				    enum pipe pipe)
{
	u32 val;
	bool enabled;

	val = I915_READ(PCH_TRANSCONF(pipe));
	enabled = !!(val & TRANS_ENABLE);
	I915_STATE_WARN(enabled,
	     "transcoder assertion failed, should be off on pipe %c but is still active\n",
	     pipe_name(pipe));
}

static bool dp_pipe_enabled(struct drm_i915_private *dev_priv,
			    enum pipe pipe, u32 port_sel, u32 val)
{
	if ((val & DP_PORT_EN) == 0)
		return false;

	if (HAS_PCH_CPT(dev_priv)) {
		u32 trans_dp_ctl = I915_READ(TRANS_DP_CTL(pipe));
		if ((trans_dp_ctl & TRANS_DP_PORT_SEL_MASK) != port_sel)
			return false;
	} else if (IS_CHERRYVIEW(dev_priv)) {
		if ((val & DP_PIPE_MASK_CHV) != DP_PIPE_SELECT_CHV(pipe))
			return false;
	} else {
		if ((val & DP_PIPE_MASK) != (pipe << 30))
			return false;
	}
	return true;
}

static bool hdmi_pipe_enabled(struct drm_i915_private *dev_priv,
			      enum pipe pipe, u32 val)
{
	if ((val & SDVO_ENABLE) == 0)
		return false;

	if (HAS_PCH_CPT(dev_priv)) {
		if ((val & SDVO_PIPE_SEL_MASK_CPT) != SDVO_PIPE_SEL_CPT(pipe))
			return false;
	} else if (IS_CHERRYVIEW(dev_priv)) {
		if ((val & SDVO_PIPE_SEL_MASK_CHV) != SDVO_PIPE_SEL_CHV(pipe))
			return false;
	} else {
		if ((val & SDVO_PIPE_SEL_MASK) != SDVO_PIPE_SEL(pipe))
			return false;
	}
	return true;
}

static bool lvds_pipe_enabled(struct drm_i915_private *dev_priv,
			      enum pipe pipe, u32 val)
{
	if ((val & LVDS_PORT_EN) == 0)
		return false;

	if (HAS_PCH_CPT(dev_priv)) {
		if ((val & PORT_TRANS_SEL_MASK) != PORT_TRANS_SEL_CPT(pipe))
			return false;
	} else {
		if ((val & LVDS_PIPE_MASK) != LVDS_PIPE(pipe))
			return false;
	}
	return true;
}

static bool adpa_pipe_enabled(struct drm_i915_private *dev_priv,
			      enum pipe pipe, u32 val)
{
	if ((val & ADPA_DAC_ENABLE) == 0)
		return false;
	if (HAS_PCH_CPT(dev_priv)) {
		if ((val & PORT_TRANS_SEL_MASK) != PORT_TRANS_SEL_CPT(pipe))
			return false;
	} else {
		if ((val & ADPA_PIPE_SELECT_MASK) != ADPA_PIPE_SELECT(pipe))
			return false;
	}
	return true;
}

static void assert_pch_dp_disabled(struct drm_i915_private *dev_priv,
				   enum pipe pipe, i915_reg_t reg,
				   u32 port_sel)
{
	u32 val = I915_READ(reg);
	I915_STATE_WARN(dp_pipe_enabled(dev_priv, pipe, port_sel, val),
	     "PCH DP (0x%08x) enabled on transcoder %c, should be disabled\n",
	     i915_mmio_reg_offset(reg), pipe_name(pipe));

	I915_STATE_WARN(HAS_PCH_IBX(dev_priv) && (val & DP_PORT_EN) == 0
	     && (val & DP_PIPEB_SELECT),
	     "IBX PCH dp port still using transcoder B\n");
}

static void assert_pch_hdmi_disabled(struct drm_i915_private *dev_priv,
				     enum pipe pipe, i915_reg_t reg)
{
	u32 val = I915_READ(reg);
	I915_STATE_WARN(hdmi_pipe_enabled(dev_priv, pipe, val),
	     "PCH HDMI (0x%08x) enabled on transcoder %c, should be disabled\n",
	     i915_mmio_reg_offset(reg), pipe_name(pipe));

	I915_STATE_WARN(HAS_PCH_IBX(dev_priv) && (val & SDVO_ENABLE) == 0
	     && (val & SDVO_PIPE_B_SELECT),
	     "IBX PCH hdmi port still using transcoder B\n");
}

static void assert_pch_ports_disabled(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	u32 val;

	assert_pch_dp_disabled(dev_priv, pipe, PCH_DP_B, TRANS_DP_PORT_SEL_B);
	assert_pch_dp_disabled(dev_priv, pipe, PCH_DP_C, TRANS_DP_PORT_SEL_C);
	assert_pch_dp_disabled(dev_priv, pipe, PCH_DP_D, TRANS_DP_PORT_SEL_D);

	val = I915_READ(PCH_ADPA);
	I915_STATE_WARN(adpa_pipe_enabled(dev_priv, pipe, val),
	     "PCH VGA enabled on transcoder %c, should be disabled\n",
	     pipe_name(pipe));

	val = I915_READ(PCH_LVDS);
	I915_STATE_WARN(lvds_pipe_enabled(dev_priv, pipe, val),
	     "PCH LVDS enabled on transcoder %c, should be disabled\n",
	     pipe_name(pipe));

	assert_pch_hdmi_disabled(dev_priv, pipe, PCH_HDMIB);
	assert_pch_hdmi_disabled(dev_priv, pipe, PCH_HDMIC);
	assert_pch_hdmi_disabled(dev_priv, pipe, PCH_HDMID);
}

static void _vlv_enable_pll(struct intel_crtc *crtc,
			    const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	I915_WRITE(DPLL(pipe), pipe_config->dpll_hw_state.dpll);
	POSTING_READ(DPLL(pipe));
	udelay(150);

	if (intel_wait_for_register(dev_priv,
				    DPLL(pipe),
				    DPLL_LOCK_VLV,
				    DPLL_LOCK_VLV,
				    1))
		DRM_ERROR("DPLL %d failed to lock\n", pipe);
}

static void vlv_enable_pll(struct intel_crtc *crtc,
			   const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	assert_pipe_disabled(dev_priv, pipe);

	/* PLL is protected by panel, make sure we can write it */
	assert_panel_unlocked(dev_priv, pipe);

	if (pipe_config->dpll_hw_state.dpll & DPLL_VCO_ENABLE)
		_vlv_enable_pll(crtc, pipe_config);

	I915_WRITE(DPLL_MD(pipe), pipe_config->dpll_hw_state.dpll_md);
	POSTING_READ(DPLL_MD(pipe));
}


static void _chv_enable_pll(struct intel_crtc *crtc,
			    const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	enum dpio_channel port = vlv_pipe_to_channel(pipe);
	u32 tmp;

	mutex_lock(&dev_priv->sb_lock);

	/* Enable back the 10bit clock to display controller */
	tmp = vlv_dpio_read(dev_priv, pipe, CHV_CMN_DW14(port));
	tmp |= DPIO_DCLKP_EN;
	vlv_dpio_write(dev_priv, pipe, CHV_CMN_DW14(port), tmp);

	mutex_unlock(&dev_priv->sb_lock);

	/*
	 * Need to wait > 100ns between dclkp clock enable bit and PLL enable.
	 */
	udelay(1);

	/* Enable PLL */
	I915_WRITE(DPLL(pipe), pipe_config->dpll_hw_state.dpll);

	/* Check PLL is locked */
	if (intel_wait_for_register(dev_priv,
				    DPLL(pipe), DPLL_LOCK_VLV, DPLL_LOCK_VLV,
				    1))
		DRM_ERROR("PLL %d failed to lock\n", pipe);
}

static void chv_enable_pll(struct intel_crtc *crtc,
			   const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	assert_pipe_disabled(dev_priv, pipe);

	/* PLL is protected by panel, make sure we can write it */
	assert_panel_unlocked(dev_priv, pipe);

	if (pipe_config->dpll_hw_state.dpll & DPLL_VCO_ENABLE)
		_chv_enable_pll(crtc, pipe_config);

	if (pipe != PIPE_A) {
		/*
		 * WaPixelRepeatModeFixForC0:chv
		 *
		 * DPLLCMD is AWOL. Use chicken bits to propagate
		 * the value from DPLLBMD to either pipe B or C.
		 */
		I915_WRITE(CBR4_VLV, pipe == PIPE_B ? CBR_DPLLBMD_PIPE_B : CBR_DPLLBMD_PIPE_C);
		I915_WRITE(DPLL_MD(PIPE_B), pipe_config->dpll_hw_state.dpll_md);
		I915_WRITE(CBR4_VLV, 0);
		dev_priv->chv_dpll_md[pipe] = pipe_config->dpll_hw_state.dpll_md;

		/*
		 * DPLLB VGA mode also seems to cause problems.
		 * We should always have it disabled.
		 */
		WARN_ON((I915_READ(DPLL(PIPE_B)) & DPLL_VGA_MODE_DIS) == 0);
	} else {
		I915_WRITE(DPLL_MD(pipe), pipe_config->dpll_hw_state.dpll_md);
		POSTING_READ(DPLL_MD(pipe));
	}
}

static int intel_num_dvo_pipes(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc;
	int count = 0;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		count += crtc->base.state->active &&
			intel_crtc_has_type(crtc->config, INTEL_OUTPUT_DVO);
	}

	return count;
}

static void i9xx_enable_pll(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	i915_reg_t reg = DPLL(crtc->pipe);
	u32 dpll = crtc->config->dpll_hw_state.dpll;

	assert_pipe_disabled(dev_priv, crtc->pipe);

	/* PLL is protected by panel, make sure we can write it */
	if (IS_MOBILE(dev_priv) && !IS_I830(dev_priv))
		assert_panel_unlocked(dev_priv, crtc->pipe);

	/* Enable DVO 2x clock on both PLLs if necessary */
	if (IS_I830(dev_priv) && intel_num_dvo_pipes(dev_priv) > 0) {
		/*
		 * It appears to be important that we don't enable this
		 * for the current pipe before otherwise configuring the
		 * PLL. No idea how this should be handled if multiple
		 * DVO outputs are enabled simultaneosly.
		 */
		dpll |= DPLL_DVO_2X_MODE;
		I915_WRITE(DPLL(!crtc->pipe),
			   I915_READ(DPLL(!crtc->pipe)) | DPLL_DVO_2X_MODE);
	}

	/*
	 * Apparently we need to have VGA mode enabled prior to changing
	 * the P1/P2 dividers. Otherwise the DPLL will keep using the old
	 * dividers, even though the register value does change.
	 */
	I915_WRITE(reg, 0);

	I915_WRITE(reg, dpll);

	/* Wait for the clocks to stabilize. */
	POSTING_READ(reg);
	udelay(150);

	if (INTEL_GEN(dev_priv) >= 4) {
		I915_WRITE(DPLL_MD(crtc->pipe),
			   crtc->config->dpll_hw_state.dpll_md);
	} else {
		/* The pixel multiplier can only be updated once the
		 * DPLL is enabled and the clocks are stable.
		 *
		 * So write it again.
		 */
		I915_WRITE(reg, dpll);
	}

	/* We do this three times for luck */
	I915_WRITE(reg, dpll);
	POSTING_READ(reg);
	udelay(150); /* wait for warmup */
	I915_WRITE(reg, dpll);
	POSTING_READ(reg);
	udelay(150); /* wait for warmup */
	I915_WRITE(reg, dpll);
	POSTING_READ(reg);
	udelay(150); /* wait for warmup */
}

/**
 * i9xx_disable_pll - disable a PLL
 * @dev_priv: i915 private structure
 * @pipe: pipe PLL to disable
 *
 * Disable the PLL for @pipe, making sure the pipe is off first.
 *
 * Note!  This is for pre-ILK only.
 */
static void i9xx_disable_pll(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	/* Disable DVO 2x clock on both PLLs if necessary */
	if (IS_I830(dev_priv) &&
	    intel_crtc_has_type(crtc->config, INTEL_OUTPUT_DVO) &&
	    !intel_num_dvo_pipes(dev_priv)) {
		I915_WRITE(DPLL(PIPE_B),
			   I915_READ(DPLL(PIPE_B)) & ~DPLL_DVO_2X_MODE);
		I915_WRITE(DPLL(PIPE_A),
			   I915_READ(DPLL(PIPE_A)) & ~DPLL_DVO_2X_MODE);
	}

	/* Don't disable pipe or pipe PLLs if needed */
	if ((pipe == PIPE_A && dev_priv->quirks & QUIRK_PIPEA_FORCE) ||
	    (pipe == PIPE_B && dev_priv->quirks & QUIRK_PIPEB_FORCE))
		return;

	/* Make sure the pipe isn't still relying on us */
	assert_pipe_disabled(dev_priv, pipe);

	I915_WRITE(DPLL(pipe), DPLL_VGA_MODE_DIS);
	POSTING_READ(DPLL(pipe));
}

static void vlv_disable_pll(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	u32 val;

	/* Make sure the pipe isn't still relying on us */
	assert_pipe_disabled(dev_priv, pipe);

	val = DPLL_INTEGRATED_REF_CLK_VLV |
		DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS;
	if (pipe != PIPE_A)
		val |= DPLL_INTEGRATED_CRI_CLK_VLV;

	I915_WRITE(DPLL(pipe), val);
	POSTING_READ(DPLL(pipe));
}

static void chv_disable_pll(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	enum dpio_channel port = vlv_pipe_to_channel(pipe);
	u32 val;

	/* Make sure the pipe isn't still relying on us */
	assert_pipe_disabled(dev_priv, pipe);

	val = DPLL_SSC_REF_CLK_CHV |
		DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS;
	if (pipe != PIPE_A)
		val |= DPLL_INTEGRATED_CRI_CLK_VLV;

	I915_WRITE(DPLL(pipe), val);
	POSTING_READ(DPLL(pipe));

	mutex_lock(&dev_priv->sb_lock);

	/* Disable 10bit clock to display controller */
	val = vlv_dpio_read(dev_priv, pipe, CHV_CMN_DW14(port));
	val &= ~DPIO_DCLKP_EN;
	vlv_dpio_write(dev_priv, pipe, CHV_CMN_DW14(port), val);

	mutex_unlock(&dev_priv->sb_lock);
}

void vlv_wait_port_ready(struct drm_i915_private *dev_priv,
			 struct intel_digital_port *dport,
			 unsigned int expected_mask)
{
	u32 port_mask;
	i915_reg_t dpll_reg;

	switch (dport->port) {
	case PORT_B:
		port_mask = DPLL_PORTB_READY_MASK;
		dpll_reg = DPLL(0);
		break;
	case PORT_C:
		port_mask = DPLL_PORTC_READY_MASK;
		dpll_reg = DPLL(0);
		expected_mask <<= 4;
		break;
	case PORT_D:
		port_mask = DPLL_PORTD_READY_MASK;
		dpll_reg = DPIO_PHY_STATUS;
		break;
	default:
		BUG();
	}

	if (intel_wait_for_register(dev_priv,
				    dpll_reg, port_mask, expected_mask,
				    1000))
		WARN(1, "timed out waiting for port %c ready: got 0x%x, expected 0x%x\n",
		     port_name(dport->port), I915_READ(dpll_reg) & port_mask, expected_mask);
}

static void ironlake_enable_pch_transcoder(struct drm_i915_private *dev_priv,
					   enum pipe pipe)
{
	struct intel_crtc *intel_crtc = intel_get_crtc_for_pipe(dev_priv,
								pipe);
	i915_reg_t reg;
	uint32_t val, pipeconf_val;

	/* Make sure PCH DPLL is enabled */
	assert_shared_dpll_enabled(dev_priv, intel_crtc->config->shared_dpll);

	/* FDI must be feeding us bits for PCH ports */
	assert_fdi_tx_enabled(dev_priv, pipe);
	assert_fdi_rx_enabled(dev_priv, pipe);

	if (HAS_PCH_CPT(dev_priv)) {
		/* Workaround: Set the timing override bit before enabling the
		 * pch transcoder. */
		reg = TRANS_CHICKEN2(pipe);
		val = I915_READ(reg);
		val |= TRANS_CHICKEN2_TIMING_OVERRIDE;
		I915_WRITE(reg, val);
	}

	reg = PCH_TRANSCONF(pipe);
	val = I915_READ(reg);
	pipeconf_val = I915_READ(PIPECONF(pipe));

	if (HAS_PCH_IBX(dev_priv)) {
		/*
		 * Make the BPC in transcoder be consistent with
		 * that in pipeconf reg. For HDMI we must use 8bpc
		 * here for both 8bpc and 12bpc.
		 */
		val &= ~PIPECONF_BPC_MASK;
		if (intel_crtc_has_type(intel_crtc->config, INTEL_OUTPUT_HDMI))
			val |= PIPECONF_8BPC;
		else
			val |= pipeconf_val & PIPECONF_BPC_MASK;
	}

	val &= ~TRANS_INTERLACE_MASK;
	if ((pipeconf_val & PIPECONF_INTERLACE_MASK) == PIPECONF_INTERLACED_ILK)
		if (HAS_PCH_IBX(dev_priv) &&
		    intel_crtc_has_type(intel_crtc->config, INTEL_OUTPUT_SDVO))
			val |= TRANS_LEGACY_INTERLACED_ILK;
		else
			val |= TRANS_INTERLACED;
	else
		val |= TRANS_PROGRESSIVE;

	I915_WRITE(reg, val | TRANS_ENABLE);
	if (intel_wait_for_register(dev_priv,
				    reg, TRANS_STATE_ENABLE, TRANS_STATE_ENABLE,
				    100))
		DRM_ERROR("failed to enable transcoder %c\n", pipe_name(pipe));
}

static void lpt_enable_pch_transcoder(struct drm_i915_private *dev_priv,
				      enum transcoder cpu_transcoder)
{
	u32 val, pipeconf_val;

	/* FDI must be feeding us bits for PCH ports */
	assert_fdi_tx_enabled(dev_priv, (enum pipe) cpu_transcoder);
	assert_fdi_rx_enabled(dev_priv, TRANSCODER_A);

	/* Workaround: set timing override bit. */
	val = I915_READ(TRANS_CHICKEN2(PIPE_A));
	val |= TRANS_CHICKEN2_TIMING_OVERRIDE;
	I915_WRITE(TRANS_CHICKEN2(PIPE_A), val);

	val = TRANS_ENABLE;
	pipeconf_val = I915_READ(PIPECONF(cpu_transcoder));

	if ((pipeconf_val & PIPECONF_INTERLACE_MASK_HSW) ==
	    PIPECONF_INTERLACED_ILK)
		val |= TRANS_INTERLACED;
	else
		val |= TRANS_PROGRESSIVE;

	I915_WRITE(LPT_TRANSCONF, val);
	if (intel_wait_for_register(dev_priv,
				    LPT_TRANSCONF,
				    TRANS_STATE_ENABLE,
				    TRANS_STATE_ENABLE,
				    100))
		DRM_ERROR("Failed to enable PCH transcoder\n");
}

static void ironlake_disable_pch_transcoder(struct drm_i915_private *dev_priv,
					    enum pipe pipe)
{
	i915_reg_t reg;
	uint32_t val;

	/* FDI relies on the transcoder */
	assert_fdi_tx_disabled(dev_priv, pipe);
	assert_fdi_rx_disabled(dev_priv, pipe);

	/* Ports must be off as well */
	assert_pch_ports_disabled(dev_priv, pipe);

	reg = PCH_TRANSCONF(pipe);
	val = I915_READ(reg);
	val &= ~TRANS_ENABLE;
	I915_WRITE(reg, val);
	/* wait for PCH transcoder off, transcoder state */
	if (intel_wait_for_register(dev_priv,
				    reg, TRANS_STATE_ENABLE, 0,
				    50))
		DRM_ERROR("failed to disable transcoder %c\n", pipe_name(pipe));

	if (HAS_PCH_CPT(dev_priv)) {
		/* Workaround: Clear the timing override chicken bit again. */
		reg = TRANS_CHICKEN2(pipe);
		val = I915_READ(reg);
		val &= ~TRANS_CHICKEN2_TIMING_OVERRIDE;
		I915_WRITE(reg, val);
	}
}

void lpt_disable_pch_transcoder(struct drm_i915_private *dev_priv)
{
	u32 val;

	val = I915_READ(LPT_TRANSCONF);
	val &= ~TRANS_ENABLE;
	I915_WRITE(LPT_TRANSCONF, val);
	/* wait for PCH transcoder off, transcoder state */
	if (intel_wait_for_register(dev_priv,
				    LPT_TRANSCONF, TRANS_STATE_ENABLE, 0,
				    50))
		DRM_ERROR("Failed to disable PCH transcoder\n");

	/* Workaround: clear timing override bit. */
	val = I915_READ(TRANS_CHICKEN2(PIPE_A));
	val &= ~TRANS_CHICKEN2_TIMING_OVERRIDE;
	I915_WRITE(TRANS_CHICKEN2(PIPE_A), val);
}

enum transcoder intel_crtc_pch_transcoder(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	WARN_ON(!crtc->config->has_pch_encoder);

	if (HAS_PCH_LPT(dev_priv))
		return TRANSCODER_A;
	else
		return (enum transcoder) crtc->pipe;
}

/**
 * intel_enable_pipe - enable a pipe, asserting requirements
 * @crtc: crtc responsible for the pipe
 *
 * Enable @crtc's pipe, making sure that various hardware specific requirements
 * are met, if applicable, e.g. PLL enabled, LVDS pairs enabled, etc.
 */
static void intel_enable_pipe(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = crtc->pipe;
	enum transcoder cpu_transcoder = crtc->config->cpu_transcoder;
	i915_reg_t reg;
	u32 val;

	DRM_DEBUG_KMS("enabling pipe %c\n", pipe_name(pipe));

	assert_planes_disabled(dev_priv, pipe);
	assert_cursor_disabled(dev_priv, pipe);
	assert_sprites_disabled(dev_priv, pipe);

	/*
	 * A pipe without a PLL won't actually be able to drive bits from
	 * a plane.  On ILK+ the pipe PLLs are integrated, so we don't
	 * need the check.
	 */
	if (HAS_GMCH_DISPLAY(dev_priv)) {
		if (intel_crtc_has_type(crtc->config, INTEL_OUTPUT_DSI))
			assert_dsi_pll_enabled(dev_priv);
		else
			assert_pll_enabled(dev_priv, pipe);
	} else {
		if (crtc->config->has_pch_encoder) {
			/* if driving the PCH, we need FDI enabled */
			assert_fdi_rx_pll_enabled(dev_priv,
						  (enum pipe) intel_crtc_pch_transcoder(crtc));
			assert_fdi_tx_pll_enabled(dev_priv,
						  (enum pipe) cpu_transcoder);
		}
		/* FIXME: assert CPU port conditions for SNB+ */
	}

	reg = PIPECONF(cpu_transcoder);
	val = I915_READ(reg);
	if (val & PIPECONF_ENABLE) {
		WARN_ON(!((pipe == PIPE_A && dev_priv->quirks & QUIRK_PIPEA_FORCE) ||
			  (pipe == PIPE_B && dev_priv->quirks & QUIRK_PIPEB_FORCE)));
		return;
	}

	I915_WRITE(reg, val | PIPECONF_ENABLE);
	POSTING_READ(reg);

	/*
	 * Until the pipe starts DSL will read as 0, which would cause
	 * an apparent vblank timestamp jump, which messes up also the
	 * frame count when it's derived from the timestamps. So let's
	 * wait for the pipe to start properly before we call
	 * drm_crtc_vblank_on()
	 */
	if (dev->max_vblank_count == 0 &&
	    wait_for(intel_get_crtc_scanline(crtc) != crtc->scanline_offset, 50))
		DRM_ERROR("pipe %c didn't start\n", pipe_name(pipe));
}

/**
 * intel_disable_pipe - disable a pipe, asserting requirements
 * @crtc: crtc whose pipes is to be disabled
 *
 * Disable the pipe of @crtc, making sure that various hardware
 * specific requirements are met, if applicable, e.g. plane
 * disabled, panel fitter off, etc.
 *
 * Will wait until the pipe has shut down before returning.
 */
static void intel_disable_pipe(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc->config->cpu_transcoder;
	enum pipe pipe = crtc->pipe;
	i915_reg_t reg;
	u32 val;

	DRM_DEBUG_KMS("disabling pipe %c\n", pipe_name(pipe));

	/*
	 * Make sure planes won't keep trying to pump pixels to us,
	 * or we might hang the display.
	 */
	assert_planes_disabled(dev_priv, pipe);
	assert_cursor_disabled(dev_priv, pipe);
	assert_sprites_disabled(dev_priv, pipe);

	reg = PIPECONF(cpu_transcoder);
	val = I915_READ(reg);
	if ((val & PIPECONF_ENABLE) == 0)
		return;

	/*
	 * Double wide has implications for planes
	 * so best keep it disabled when not needed.
	 */
	if (crtc->config->double_wide)
		val &= ~PIPECONF_DOUBLE_WIDE;

	/* Don't disable pipe or pipe PLLs if needed */
	if (!(pipe == PIPE_A && dev_priv->quirks & QUIRK_PIPEA_FORCE) &&
	    !(pipe == PIPE_B && dev_priv->quirks & QUIRK_PIPEB_FORCE))
		val &= ~PIPECONF_ENABLE;

	I915_WRITE(reg, val);
	if ((val & PIPECONF_ENABLE) == 0)
		intel_wait_for_pipe_off(crtc);
}

static unsigned int intel_tile_size(const struct drm_i915_private *dev_priv)
{
	return IS_GEN2(dev_priv) ? 2048 : 4096;
}

static unsigned int intel_tile_width_bytes(const struct drm_i915_private *dev_priv,
					   uint64_t fb_modifier, unsigned int cpp)
{
	switch (fb_modifier) {
	case DRM_FORMAT_MOD_NONE:
		return cpp;
	case I915_FORMAT_MOD_X_TILED:
		if (IS_GEN2(dev_priv))
			return 128;
		else
			return 512;
	case I915_FORMAT_MOD_Y_TILED:
		if (IS_GEN2(dev_priv) || HAS_128_BYTE_Y_TILING(dev_priv))
			return 128;
		else
			return 512;
	case I915_FORMAT_MOD_Yf_TILED:
		switch (cpp) {
		case 1:
			return 64;
		case 2:
		case 4:
			return 128;
		case 8:
		case 16:
			return 256;
		default:
			MISSING_CASE(cpp);
			return cpp;
		}
		break;
	default:
		MISSING_CASE(fb_modifier);
		return cpp;
	}
}

unsigned int intel_tile_height(const struct drm_i915_private *dev_priv,
			       uint64_t fb_modifier, unsigned int cpp)
{
	if (fb_modifier == DRM_FORMAT_MOD_NONE)
		return 1;
	else
		return intel_tile_size(dev_priv) /
			intel_tile_width_bytes(dev_priv, fb_modifier, cpp);
}

/* Return the tile dimensions in pixel units */
static void intel_tile_dims(const struct drm_i915_private *dev_priv,
			    unsigned int *tile_width,
			    unsigned int *tile_height,
			    uint64_t fb_modifier,
			    unsigned int cpp)
{
	unsigned int tile_width_bytes =
		intel_tile_width_bytes(dev_priv, fb_modifier, cpp);

	*tile_width = tile_width_bytes / cpp;
	*tile_height = intel_tile_size(dev_priv) / tile_width_bytes;
}

unsigned int
intel_fb_align_height(struct drm_i915_private *dev_priv,
		      unsigned int height,
		      uint32_t pixel_format,
		      uint64_t fb_modifier)
{
	unsigned int cpp = drm_format_plane_cpp(pixel_format, 0);
	unsigned int tile_height = intel_tile_height(dev_priv, fb_modifier, cpp);

	return ALIGN(height, tile_height);
}

unsigned int intel_rotation_info_size(const struct intel_rotation_info *rot_info)
{
	unsigned int size = 0;
	int i;

	for (i = 0 ; i < ARRAY_SIZE(rot_info->plane); i++)
		size += rot_info->plane[i].width * rot_info->plane[i].height;

	return size;
}

static void
intel_fill_fb_ggtt_view(struct i915_ggtt_view *view,
			const struct drm_framebuffer *fb,
			unsigned int rotation)
{
	view->type = I915_GGTT_VIEW_NORMAL;
	if (drm_rotation_90_or_270(rotation)) {
		view->type = I915_GGTT_VIEW_ROTATED;
		view->rotated = to_intel_framebuffer(fb)->rot_info;
	}
}

static unsigned int intel_linear_alignment(const struct drm_i915_private *dev_priv)
{
	if (INTEL_INFO(dev_priv)->gen >= 9)
		return 256 * 1024;
	else if (IS_I965G(dev_priv) || IS_I965GM(dev_priv) ||
		 IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		return 128 * 1024;
	else if (INTEL_INFO(dev_priv)->gen >= 4)
		return 4 * 1024;
	else
		return 0;
}

static unsigned int intel_surf_alignment(const struct drm_i915_private *dev_priv,
					 uint64_t fb_modifier)
{
	switch (fb_modifier) {
	case DRM_FORMAT_MOD_NONE:
		return intel_linear_alignment(dev_priv);
	case I915_FORMAT_MOD_X_TILED:
		if (INTEL_INFO(dev_priv)->gen >= 9)
			return 256 * 1024;
		return 0;
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Yf_TILED:
		return 1 * 1024 * 1024;
	default:
		MISSING_CASE(fb_modifier);
		return 0;
	}
}

struct i915_vma *
intel_pin_and_fence_fb_obj(struct drm_framebuffer *fb, unsigned int rotation)
{
	struct drm_device *dev = fb->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct i915_ggtt_view view;
	struct i915_vma *vma;
	u32 alignment;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	alignment = intel_surf_alignment(dev_priv, fb->modifier);

	intel_fill_fb_ggtt_view(&view, fb, rotation);

	/* Note that the w/a also requires 64 PTE of padding following the
	 * bo. We currently fill all unused PTE with the shadow page and so
	 * we should always have valid PTE following the scanout preventing
	 * the VT-d warning.
	 */
	if (intel_scanout_needs_vtd_wa(dev_priv) && alignment < 256 * 1024)
		alignment = 256 * 1024;

	/*
	 * Global gtt pte registers are special registers which actually forward
	 * writes to a chunk of system memory. Which means that there is no risk
	 * that the register values disappear as soon as we call
	 * intel_runtime_pm_put(), so it is correct to wrap only the
	 * pin/unpin/fence and not more.
	 */
	intel_runtime_pm_get(dev_priv);

	vma = i915_gem_object_pin_to_display_plane(obj, alignment, &view);
	if (IS_ERR(vma))
		goto err;

	if (i915_vma_is_map_and_fenceable(vma)) {
		/* Install a fence for tiled scan-out. Pre-i965 always needs a
		 * fence, whereas 965+ only requires a fence if using
		 * framebuffer compression.  For simplicity, we always, when
		 * possible, install a fence as the cost is not that onerous.
		 *
		 * If we fail to fence the tiled scanout, then either the
		 * modeset will reject the change (which is highly unlikely as
		 * the affected systems, all but one, do not have unmappable
		 * space) or we will not be able to enable full powersaving
		 * techniques (also likely not to apply due to various limits
		 * FBC and the like impose on the size of the buffer, which
		 * presumably we violated anyway with this unmappable buffer).
		 * Anyway, it is presumably better to stumble onwards with
		 * something and try to run the system in a "less than optimal"
		 * mode that matches the user configuration.
		 */
		if (i915_vma_get_fence(vma) == 0)
			i915_vma_pin_fence(vma);
	}

	i915_vma_get(vma);
err:
	intel_runtime_pm_put(dev_priv);
	return vma;
}

void intel_unpin_fb_vma(struct i915_vma *vma)
{
	lockdep_assert_held(&vma->vm->i915->drm.struct_mutex);

	i915_vma_unpin_fence(vma);
	i915_gem_object_unpin_from_display_plane(vma);
	i915_vma_put(vma);
}

static int intel_fb_pitch(const struct drm_framebuffer *fb, int plane,
			  unsigned int rotation)
{
	if (drm_rotation_90_or_270(rotation))
		return to_intel_framebuffer(fb)->rotated[plane].pitch;
	else
		return fb->pitches[plane];
}

/*
 * Convert the x/y offsets into a linear offset.
 * Only valid with 0/180 degree rotation, which is fine since linear
 * offset is only used with linear buffers on pre-hsw and tiled buffers
 * with gen2/3, and 90/270 degree rotations isn't supported on any of them.
 */
u32 intel_fb_xy_to_linear(int x, int y,
			  const struct intel_plane_state *state,
			  int plane)
{
	const struct drm_framebuffer *fb = state->base.fb;
	unsigned int cpp = fb->format->cpp[plane];
	unsigned int pitch = fb->pitches[plane];

	return y * pitch + x * cpp;
}

/*
 * Add the x/y offsets derived from fb->offsets[] to the user
 * specified plane src x/y offsets. The resulting x/y offsets
 * specify the start of scanout from the beginning of the gtt mapping.
 */
void intel_add_fb_offsets(int *x, int *y,
			  const struct intel_plane_state *state,
			  int plane)

{
	const struct intel_framebuffer *intel_fb = to_intel_framebuffer(state->base.fb);
	unsigned int rotation = state->base.rotation;

	if (drm_rotation_90_or_270(rotation)) {
		*x += intel_fb->rotated[plane].x;
		*y += intel_fb->rotated[plane].y;
	} else {
		*x += intel_fb->normal[plane].x;
		*y += intel_fb->normal[plane].y;
	}
}

/*
 * Input tile dimensions and pitch must already be
 * rotated to match x and y, and in pixel units.
 */
static u32 _intel_adjust_tile_offset(int *x, int *y,
				     unsigned int tile_width,
				     unsigned int tile_height,
				     unsigned int tile_size,
				     unsigned int pitch_tiles,
				     u32 old_offset,
				     u32 new_offset)
{
	unsigned int pitch_pixels = pitch_tiles * tile_width;
	unsigned int tiles;

	WARN_ON(old_offset & (tile_size - 1));
	WARN_ON(new_offset & (tile_size - 1));
	WARN_ON(new_offset > old_offset);

	tiles = (old_offset - new_offset) / tile_size;

	*y += tiles / pitch_tiles * tile_height;
	*x += tiles % pitch_tiles * tile_width;

	/* minimize x in case it got needlessly big */
	*y += *x / pitch_pixels * tile_height;
	*x %= pitch_pixels;

	return new_offset;
}

/*
 * Adjust the tile offset by moving the difference into
 * the x/y offsets.
 */
static u32 intel_adjust_tile_offset(int *x, int *y,
				    const struct intel_plane_state *state, int plane,
				    u32 old_offset, u32 new_offset)
{
	const struct drm_i915_private *dev_priv = to_i915(state->base.plane->dev);
	const struct drm_framebuffer *fb = state->base.fb;
	unsigned int cpp = fb->format->cpp[plane];
	unsigned int rotation = state->base.rotation;
	unsigned int pitch = intel_fb_pitch(fb, plane, rotation);

	WARN_ON(new_offset > old_offset);

	if (fb->modifier != DRM_FORMAT_MOD_NONE) {
		unsigned int tile_size, tile_width, tile_height;
		unsigned int pitch_tiles;

		tile_size = intel_tile_size(dev_priv);
		intel_tile_dims(dev_priv, &tile_width, &tile_height,
				fb->modifier, cpp);

		if (drm_rotation_90_or_270(rotation)) {
			pitch_tiles = pitch / tile_height;
			swap(tile_width, tile_height);
		} else {
			pitch_tiles = pitch / (tile_width * cpp);
		}

		_intel_adjust_tile_offset(x, y, tile_width, tile_height,
					  tile_size, pitch_tiles,
					  old_offset, new_offset);
	} else {
		old_offset += *y * pitch + *x * cpp;

		*y = (old_offset - new_offset) / pitch;
		*x = ((old_offset - new_offset) - *y * pitch) / cpp;
	}

	return new_offset;
}

/*
 * Computes the linear offset to the base tile and adjusts
 * x, y. bytes per pixel is assumed to be a power-of-two.
 *
 * In the 90/270 rotated case, x and y are assumed
 * to be already rotated to match the rotated GTT view, and
 * pitch is the tile_height aligned framebuffer height.
 *
 * This function is used when computing the derived information
 * under intel_framebuffer, so using any of that information
 * here is not allowed. Anything under drm_framebuffer can be
 * used. This is why the user has to pass in the pitch since it
 * is specified in the rotated orientation.
 */
static u32 _intel_compute_tile_offset(const struct drm_i915_private *dev_priv,
				      int *x, int *y,
				      const struct drm_framebuffer *fb, int plane,
				      unsigned int pitch,
				      unsigned int rotation,
				      u32 alignment)
{
	uint64_t fb_modifier = fb->modifier;
	unsigned int cpp = fb->format->cpp[plane];
	u32 offset, offset_aligned;

	if (alignment)
		alignment--;

	if (fb_modifier != DRM_FORMAT_MOD_NONE) {
		unsigned int tile_size, tile_width, tile_height;
		unsigned int tile_rows, tiles, pitch_tiles;

		tile_size = intel_tile_size(dev_priv);
		intel_tile_dims(dev_priv, &tile_width, &tile_height,
				fb_modifier, cpp);

		if (drm_rotation_90_or_270(rotation)) {
			pitch_tiles = pitch / tile_height;
			swap(tile_width, tile_height);
		} else {
			pitch_tiles = pitch / (tile_width * cpp);
		}

		tile_rows = *y / tile_height;
		*y %= tile_height;

		tiles = *x / tile_width;
		*x %= tile_width;

		offset = (tile_rows * pitch_tiles + tiles) * tile_size;
		offset_aligned = offset & ~alignment;

		_intel_adjust_tile_offset(x, y, tile_width, tile_height,
					  tile_size, pitch_tiles,
					  offset, offset_aligned);
	} else {
		offset = *y * pitch + *x * cpp;
		offset_aligned = offset & ~alignment;

		*y = (offset & alignment) / pitch;
		*x = ((offset & alignment) - *y * pitch) / cpp;
	}

	return offset_aligned;
}

u32 intel_compute_tile_offset(int *x, int *y,
			      const struct intel_plane_state *state,
			      int plane)
{
	const struct drm_i915_private *dev_priv = to_i915(state->base.plane->dev);
	const struct drm_framebuffer *fb = state->base.fb;
	unsigned int rotation = state->base.rotation;
	int pitch = intel_fb_pitch(fb, plane, rotation);
	u32 alignment;

	/* AUX_DIST needs only 4K alignment */
	if (fb->format->format == DRM_FORMAT_NV12 && plane == 1)
		alignment = 4096;
	else
		alignment = intel_surf_alignment(dev_priv, fb->modifier);

	return _intel_compute_tile_offset(dev_priv, x, y, fb, plane, pitch,
					  rotation, alignment);
}

/* Convert the fb->offset[] linear offset into x/y offsets */
static void intel_fb_offset_to_xy(int *x, int *y,
				  const struct drm_framebuffer *fb, int plane)
{
	unsigned int cpp = fb->format->cpp[plane];
	unsigned int pitch = fb->pitches[plane];
	u32 linear_offset = fb->offsets[plane];

	*y = linear_offset / pitch;
	*x = linear_offset % pitch / cpp;
}

static unsigned int intel_fb_modifier_to_tiling(uint64_t fb_modifier)
{
	switch (fb_modifier) {
	case I915_FORMAT_MOD_X_TILED:
		return I915_TILING_X;
	case I915_FORMAT_MOD_Y_TILED:
		return I915_TILING_Y;
	default:
		return I915_TILING_NONE;
	}
}

static int
intel_fill_fb_info(struct drm_i915_private *dev_priv,
		   struct drm_framebuffer *fb)
{
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct intel_rotation_info *rot_info = &intel_fb->rot_info;
	u32 gtt_offset_rotated = 0;
	unsigned int max_size = 0;
	int i, num_planes = fb->format->num_planes;
	unsigned int tile_size = intel_tile_size(dev_priv);

	for (i = 0; i < num_planes; i++) {
		unsigned int width, height;
		unsigned int cpp, size;
		u32 offset;
		int x, y;

		cpp = fb->format->cpp[i];
		width = drm_framebuffer_plane_width(fb->width, fb, i);
		height = drm_framebuffer_plane_height(fb->height, fb, i);

		intel_fb_offset_to_xy(&x, &y, fb, i);

		/*
		 * The fence (if used) is aligned to the start of the object
		 * so having the framebuffer wrap around across the edge of the
		 * fenced region doesn't really work. We have no API to configure
		 * the fence start offset within the object (nor could we probably
		 * on gen2/3). So it's just easier if we just require that the
		 * fb layout agrees with the fence layout. We already check that the
		 * fb stride matches the fence stride elsewhere.
		 */
		if (i915_gem_object_is_tiled(intel_fb->obj) &&
		    (x + width) * cpp > fb->pitches[i]) {
			DRM_DEBUG("bad fb plane %d offset: 0x%x\n",
				  i, fb->offsets[i]);
			return -EINVAL;
		}

		/*
		 * First pixel of the framebuffer from
		 * the start of the normal gtt mapping.
		 */
		intel_fb->normal[i].x = x;
		intel_fb->normal[i].y = y;

		offset = _intel_compute_tile_offset(dev_priv, &x, &y,
						    fb, 0, fb->pitches[i],
						    DRM_ROTATE_0, tile_size);
		offset /= tile_size;

		if (fb->modifier != DRM_FORMAT_MOD_NONE) {
			unsigned int tile_width, tile_height;
			unsigned int pitch_tiles;
			struct drm_rect r;

			intel_tile_dims(dev_priv, &tile_width, &tile_height,
					fb->modifier, cpp);

			rot_info->plane[i].offset = offset;
			rot_info->plane[i].stride = DIV_ROUND_UP(fb->pitches[i], tile_width * cpp);
			rot_info->plane[i].width = DIV_ROUND_UP(x + width, tile_width);
			rot_info->plane[i].height = DIV_ROUND_UP(y + height, tile_height);

			intel_fb->rotated[i].pitch =
				rot_info->plane[i].height * tile_height;

			/* how many tiles does this plane need */
			size = rot_info->plane[i].stride * rot_info->plane[i].height;
			/*
			 * If the plane isn't horizontally tile aligned,
			 * we need one more tile.
			 */
			if (x != 0)
				size++;

			/* rotate the x/y offsets to match the GTT view */
			r.x1 = x;
			r.y1 = y;
			r.x2 = x + width;
			r.y2 = y + height;
			drm_rect_rotate(&r,
					rot_info->plane[i].width * tile_width,
					rot_info->plane[i].height * tile_height,
					DRM_ROTATE_270);
			x = r.x1;
			y = r.y1;

			/* rotate the tile dimensions to match the GTT view */
			pitch_tiles = intel_fb->rotated[i].pitch / tile_height;
			swap(tile_width, tile_height);

			/*
			 * We only keep the x/y offsets, so push all of the
			 * gtt offset into the x/y offsets.
			 */
			_intel_adjust_tile_offset(&x, &y,
						  tile_width, tile_height,
						  tile_size, pitch_tiles,
						  gtt_offset_rotated * tile_size, 0);

			gtt_offset_rotated += rot_info->plane[i].width * rot_info->plane[i].height;

			/*
			 * First pixel of the framebuffer from
			 * the start of the rotated gtt mapping.
			 */
			intel_fb->rotated[i].x = x;
			intel_fb->rotated[i].y = y;
		} else {
			size = DIV_ROUND_UP((y + height) * fb->pitches[i] +
					    x * cpp, tile_size);
		}

		/* how many tiles in total needed in the bo */
		max_size = max(max_size, offset + size);
	}

	if (max_size * tile_size > to_intel_framebuffer(fb)->obj->base.size) {
		DRM_DEBUG("fb too big for bo (need %u bytes, have %zu bytes)\n",
			  max_size * tile_size, to_intel_framebuffer(fb)->obj->base.size);
		return -EINVAL;
	}

	return 0;
}

static int i9xx_format_to_fourcc(int format)
{
	switch (format) {
	case DISPPLANE_8BPP:
		return DRM_FORMAT_C8;
	case DISPPLANE_BGRX555:
		return DRM_FORMAT_XRGB1555;
	case DISPPLANE_BGRX565:
		return DRM_FORMAT_RGB565;
	default:
	case DISPPLANE_BGRX888:
		return DRM_FORMAT_XRGB8888;
	case DISPPLANE_RGBX888:
		return DRM_FORMAT_XBGR8888;
	case DISPPLANE_BGRX101010:
		return DRM_FORMAT_XRGB2101010;
	case DISPPLANE_RGBX101010:
		return DRM_FORMAT_XBGR2101010;
	}
}

static int skl_format_to_fourcc(int format, bool rgb_order, bool alpha)
{
	switch (format) {
	case PLANE_CTL_FORMAT_RGB_565:
		return DRM_FORMAT_RGB565;
	default:
	case PLANE_CTL_FORMAT_XRGB_8888:
		if (rgb_order) {
			if (alpha)
				return DRM_FORMAT_ABGR8888;
			else
				return DRM_FORMAT_XBGR8888;
		} else {
			if (alpha)
				return DRM_FORMAT_ARGB8888;
			else
				return DRM_FORMAT_XRGB8888;
		}
	case PLANE_CTL_FORMAT_XRGB_2101010:
		if (rgb_order)
			return DRM_FORMAT_XBGR2101010;
		else
			return DRM_FORMAT_XRGB2101010;
	}
}

static bool
intel_alloc_initial_plane_obj(struct intel_crtc *crtc,
			      struct intel_initial_plane_config *plane_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct drm_i915_gem_object *obj = NULL;
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct drm_framebuffer *fb = &plane_config->fb->base;
	u32 base_aligned = round_down(plane_config->base, PAGE_SIZE);
	u32 size_aligned = round_up(plane_config->base + plane_config->size,
				    PAGE_SIZE);

	size_aligned -= base_aligned;

	if (plane_config->size == 0)
		return false;

	/* If the FB is too big, just don't use it since fbdev is not very
	 * important and we should probably use that space with FBC or other
	 * features. */
	if (size_aligned * 2 > ggtt->stolen_usable_size)
		return false;

	mutex_lock(&dev->struct_mutex);
	obj = i915_gem_object_create_stolen_for_preallocated(dev_priv,
							     base_aligned,
							     base_aligned,
							     size_aligned);
	mutex_unlock(&dev->struct_mutex);
	if (!obj)
		return false;

	if (plane_config->tiling == I915_TILING_X)
		obj->tiling_and_stride = fb->pitches[0] | I915_TILING_X;

	mode_cmd.pixel_format = fb->format->format;
	mode_cmd.width = fb->width;
	mode_cmd.height = fb->height;
	mode_cmd.pitches[0] = fb->pitches[0];
	mode_cmd.modifier[0] = fb->modifier;
	mode_cmd.flags = DRM_MODE_FB_MODIFIERS;

	if (intel_framebuffer_init(to_intel_framebuffer(fb), obj, &mode_cmd)) {
		DRM_DEBUG_KMS("intel fb init failed\n");
		goto out_unref_obj;
	}


	DRM_DEBUG_KMS("initial plane fb obj %p\n", obj);
	return true;

out_unref_obj:
	i915_gem_object_put(obj);
	return false;
}

/* Update plane->state->fb to match plane->fb after driver-internal updates */
static void
update_state_fb(struct drm_plane *plane)
{
	if (plane->fb == plane->state->fb)
		return;

	if (plane->state->fb)
		drm_framebuffer_unreference(plane->state->fb);
	plane->state->fb = plane->fb;
	if (plane->state->fb)
		drm_framebuffer_reference(plane->state->fb);
}

static void
intel_find_initial_plane_obj(struct intel_crtc *intel_crtc,
			     struct intel_initial_plane_config *plane_config)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_crtc *c;
	struct drm_i915_gem_object *obj;
	struct drm_plane *primary = intel_crtc->base.primary;
	struct drm_plane_state *plane_state = primary->state;
	struct drm_crtc_state *crtc_state = intel_crtc->base.state;
	struct intel_plane *intel_plane = to_intel_plane(primary);
	struct intel_plane_state *intel_state =
		to_intel_plane_state(plane_state);
	struct drm_framebuffer *fb;

	if (!plane_config->fb)
		return;

	if (intel_alloc_initial_plane_obj(intel_crtc, plane_config)) {
		fb = &plane_config->fb->base;
		goto valid_fb;
	}

	kfree(plane_config->fb);

	/*
	 * Failed to alloc the obj, check to see if we should share
	 * an fb with another CRTC instead
	 */
	for_each_crtc(dev, c) {
		struct intel_plane_state *state;

		if (c == &intel_crtc->base)
			continue;

		if (!to_intel_crtc(c)->active)
			continue;

		state = to_intel_plane_state(c->primary->state);
		if (!state->vma)
			continue;

		if (intel_plane_ggtt_offset(state) == plane_config->base) {
			fb = c->primary->fb;
			drm_framebuffer_reference(fb);
			goto valid_fb;
		}
	}

	/*
	 * We've failed to reconstruct the BIOS FB.  Current display state
	 * indicates that the primary plane is visible, but has a NULL FB,
	 * which will lead to problems later if we don't fix it up.  The
	 * simplest solution is to just disable the primary plane now and
	 * pretend the BIOS never had it enabled.
	 */
	plane_state->visible = false;
	crtc_state->plane_mask &= ~(1 << drm_plane_index(primary));
	intel_pre_disable_primary_noatomic(&intel_crtc->base);
	intel_plane->disable_plane(primary, &intel_crtc->base);

	return;

valid_fb:
	mutex_lock(&dev->struct_mutex);
	intel_state->vma =
		intel_pin_and_fence_fb_obj(fb, primary->state->rotation);
	mutex_unlock(&dev->struct_mutex);
	if (IS_ERR(intel_state->vma)) {
		DRM_ERROR("failed to pin boot fb on pipe %d: %li\n",
			  intel_crtc->pipe, PTR_ERR(intel_state->vma));

		intel_state->vma = NULL;
		drm_framebuffer_unreference(fb);
		return;
	}

	plane_state->src_x = 0;
	plane_state->src_y = 0;
	plane_state->src_w = fb->width << 16;
	plane_state->src_h = fb->height << 16;

	plane_state->crtc_x = 0;
	plane_state->crtc_y = 0;
	plane_state->crtc_w = fb->width;
	plane_state->crtc_h = fb->height;

	intel_state->base.src = drm_plane_state_src(plane_state);
	intel_state->base.dst = drm_plane_state_dest(plane_state);

	obj = intel_fb_obj(fb);
	if (i915_gem_object_is_tiled(obj))
		dev_priv->preserve_bios_swizzle = true;

	drm_framebuffer_reference(fb);
	primary->fb = primary->state->fb = fb;
	primary->crtc = primary->state->crtc = &intel_crtc->base;
	intel_crtc->base.state->plane_mask |= (1 << drm_plane_index(primary));
	atomic_or(to_intel_plane(primary)->frontbuffer_bit,
		  &obj->frontbuffer_bits);
}

static int skl_max_plane_width(const struct drm_framebuffer *fb, int plane,
			       unsigned int rotation)
{
	int cpp = fb->format->cpp[plane];

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_NONE:
	case I915_FORMAT_MOD_X_TILED:
		switch (cpp) {
		case 8:
			return 4096;
		case 4:
		case 2:
		case 1:
			return 8192;
		default:
			MISSING_CASE(cpp);
			break;
		}
		break;
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Yf_TILED:
		switch (cpp) {
		case 8:
			return 2048;
		case 4:
			return 4096;
		case 2:
		case 1:
			return 8192;
		default:
			MISSING_CASE(cpp);
			break;
		}
		break;
	default:
		MISSING_CASE(fb->modifier);
	}

	return 2048;
}

static int skl_check_main_surface(struct intel_plane_state *plane_state)
{
	const struct drm_i915_private *dev_priv = to_i915(plane_state->base.plane->dev);
	const struct drm_framebuffer *fb = plane_state->base.fb;
	unsigned int rotation = plane_state->base.rotation;
	int x = plane_state->base.src.x1 >> 16;
	int y = plane_state->base.src.y1 >> 16;
	int w = drm_rect_width(&plane_state->base.src) >> 16;
	int h = drm_rect_height(&plane_state->base.src) >> 16;
	int max_width = skl_max_plane_width(fb, 0, rotation);
	int max_height = 4096;
	u32 alignment, offset, aux_offset = plane_state->aux.offset;

	if (w > max_width || h > max_height) {
		DRM_DEBUG_KMS("requested Y/RGB source size %dx%d too big (limit %dx%d)\n",
			      w, h, max_width, max_height);
		return -EINVAL;
	}

	intel_add_fb_offsets(&x, &y, plane_state, 0);
	offset = intel_compute_tile_offset(&x, &y, plane_state, 0);

	alignment = intel_surf_alignment(dev_priv, fb->modifier);

	/*
	 * AUX surface offset is specified as the distance from the
	 * main surface offset, and it must be non-negative. Make
	 * sure that is what we will get.
	 */
	if (offset > aux_offset)
		offset = intel_adjust_tile_offset(&x, &y, plane_state, 0,
						  offset, aux_offset & ~(alignment - 1));

	/*
	 * When using an X-tiled surface, the plane blows up
	 * if the x offset + width exceed the stride.
	 *
	 * TODO: linear and Y-tiled seem fine, Yf untested,
	 */
	if (fb->modifier == I915_FORMAT_MOD_X_TILED) {
		int cpp = fb->format->cpp[0];

		while ((x + w) * cpp > fb->pitches[0]) {
			if (offset == 0) {
				DRM_DEBUG_KMS("Unable to find suitable display surface offset\n");
				return -EINVAL;
			}

			offset = intel_adjust_tile_offset(&x, &y, plane_state, 0,
							  offset, offset - alignment);
		}
	}

	plane_state->main.offset = offset;
	plane_state->main.x = x;
	plane_state->main.y = y;

	return 0;
}

static int skl_check_nv12_aux_surface(struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->base.fb;
	unsigned int rotation = plane_state->base.rotation;
	int max_width = skl_max_plane_width(fb, 1, rotation);
	int max_height = 4096;
	int x = plane_state->base.src.x1 >> 17;
	int y = plane_state->base.src.y1 >> 17;
	int w = drm_rect_width(&plane_state->base.src) >> 17;
	int h = drm_rect_height(&plane_state->base.src) >> 17;
	u32 offset;

	intel_add_fb_offsets(&x, &y, plane_state, 1);
	offset = intel_compute_tile_offset(&x, &y, plane_state, 1);

	/* FIXME not quite sure how/if these apply to the chroma plane */
	if (w > max_width || h > max_height) {
		DRM_DEBUG_KMS("CbCr source size %dx%d too big (limit %dx%d)\n",
			      w, h, max_width, max_height);
		return -EINVAL;
	}

	plane_state->aux.offset = offset;
	plane_state->aux.x = x;
	plane_state->aux.y = y;

	return 0;
}

int skl_check_plane_surface(struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->base.fb;
	unsigned int rotation = plane_state->base.rotation;
	int ret;

	if (!plane_state->base.visible)
		return 0;

	/* Rotate src coordinates to match rotated GTT view */
	if (drm_rotation_90_or_270(rotation))
		drm_rect_rotate(&plane_state->base.src,
				fb->width << 16, fb->height << 16,
				DRM_ROTATE_270);

	/*
	 * Handle the AUX surface first since
	 * the main surface setup depends on it.
	 */
	if (fb->format->format == DRM_FORMAT_NV12) {
		ret = skl_check_nv12_aux_surface(plane_state);
		if (ret)
			return ret;
	} else {
		plane_state->aux.offset = ~0xfff;
		plane_state->aux.x = 0;
		plane_state->aux.y = 0;
	}

	ret = skl_check_main_surface(plane_state);
	if (ret)
		return ret;

	return 0;
}

static void i9xx_update_primary_plane(struct drm_plane *primary,
				      const struct intel_crtc_state *crtc_state,
				      const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(primary->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_framebuffer *fb = plane_state->base.fb;
	int plane = intel_crtc->plane;
	u32 linear_offset;
	u32 dspcntr;
	i915_reg_t reg = DSPCNTR(plane);
	unsigned int rotation = plane_state->base.rotation;
	int x = plane_state->base.src.x1 >> 16;
	int y = plane_state->base.src.y1 >> 16;

	dspcntr = DISPPLANE_GAMMA_ENABLE;

	dspcntr |= DISPLAY_PLANE_ENABLE;

	if (INTEL_GEN(dev_priv) < 4) {
		if (intel_crtc->pipe == PIPE_B)
			dspcntr |= DISPPLANE_SEL_PIPE_B;

		/* pipesrc and dspsize control the size that is scaled from,
		 * which should always be the user's requested size.
		 */
		I915_WRITE(DSPSIZE(plane),
			   ((crtc_state->pipe_src_h - 1) << 16) |
			   (crtc_state->pipe_src_w - 1));
		I915_WRITE(DSPPOS(plane), 0);
	} else if (IS_CHERRYVIEW(dev_priv) && plane == PLANE_B) {
		I915_WRITE(PRIMSIZE(plane),
			   ((crtc_state->pipe_src_h - 1) << 16) |
			   (crtc_state->pipe_src_w - 1));
		I915_WRITE(PRIMPOS(plane), 0);
		I915_WRITE(PRIMCNSTALPHA(plane), 0);
	}

	switch (fb->format->format) {
	case DRM_FORMAT_C8:
		dspcntr |= DISPPLANE_8BPP;
		break;
	case DRM_FORMAT_XRGB1555:
		dspcntr |= DISPPLANE_BGRX555;
		break;
	case DRM_FORMAT_RGB565:
		dspcntr |= DISPPLANE_BGRX565;
		break;
	case DRM_FORMAT_XRGB8888:
		dspcntr |= DISPPLANE_BGRX888;
		break;
	case DRM_FORMAT_XBGR8888:
		dspcntr |= DISPPLANE_RGBX888;
		break;
	case DRM_FORMAT_XRGB2101010:
		dspcntr |= DISPPLANE_BGRX101010;
		break;
	case DRM_FORMAT_XBGR2101010:
		dspcntr |= DISPPLANE_RGBX101010;
		break;
	default:
		BUG();
	}

	if (INTEL_GEN(dev_priv) >= 4 &&
	    fb->modifier == I915_FORMAT_MOD_X_TILED)
		dspcntr |= DISPPLANE_TILED;

	if (rotation & DRM_ROTATE_180)
		dspcntr |= DISPPLANE_ROTATE_180;

	if (rotation & DRM_REFLECT_X)
		dspcntr |= DISPPLANE_MIRROR;

	if (IS_G4X(dev_priv))
		dspcntr |= DISPPLANE_TRICKLE_FEED_DISABLE;

	intel_add_fb_offsets(&x, &y, plane_state, 0);

	if (INTEL_GEN(dev_priv) >= 4)
		intel_crtc->dspaddr_offset =
			intel_compute_tile_offset(&x, &y, plane_state, 0);

	if (rotation & DRM_ROTATE_180) {
		x += crtc_state->pipe_src_w - 1;
		y += crtc_state->pipe_src_h - 1;
	} else if (rotation & DRM_REFLECT_X) {
		x += crtc_state->pipe_src_w - 1;
	}

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	if (INTEL_GEN(dev_priv) < 4)
		intel_crtc->dspaddr_offset = linear_offset;

	intel_crtc->adjusted_x = x;
	intel_crtc->adjusted_y = y;

	I915_WRITE(reg, dspcntr);

	I915_WRITE(DSPSTRIDE(plane), fb->pitches[0]);
	if (INTEL_GEN(dev_priv) >= 4) {
		I915_WRITE(DSPSURF(plane),
			   intel_plane_ggtt_offset(plane_state) +
			   intel_crtc->dspaddr_offset);
		I915_WRITE(DSPTILEOFF(plane), (y << 16) | x);
		I915_WRITE(DSPLINOFF(plane), linear_offset);
	} else {
		I915_WRITE(DSPADDR(plane),
			   intel_plane_ggtt_offset(plane_state) +
			   intel_crtc->dspaddr_offset);
	}
	POSTING_READ(reg);
}

static void i9xx_disable_primary_plane(struct drm_plane *primary,
				       struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int plane = intel_crtc->plane;

	I915_WRITE(DSPCNTR(plane), 0);
	if (INTEL_INFO(dev_priv)->gen >= 4)
		I915_WRITE(DSPSURF(plane), 0);
	else
		I915_WRITE(DSPADDR(plane), 0);
	POSTING_READ(DSPCNTR(plane));
}

static void ironlake_update_primary_plane(struct drm_plane *primary,
					  const struct intel_crtc_state *crtc_state,
					  const struct intel_plane_state *plane_state)
{
	struct drm_device *dev = primary->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_framebuffer *fb = plane_state->base.fb;
	int plane = intel_crtc->plane;
	u32 linear_offset;
	u32 dspcntr;
	i915_reg_t reg = DSPCNTR(plane);
	unsigned int rotation = plane_state->base.rotation;
	int x = plane_state->base.src.x1 >> 16;
	int y = plane_state->base.src.y1 >> 16;

	dspcntr = DISPPLANE_GAMMA_ENABLE;
	dspcntr |= DISPLAY_PLANE_ENABLE;

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		dspcntr |= DISPPLANE_PIPE_CSC_ENABLE;

	switch (fb->format->format) {
	case DRM_FORMAT_C8:
		dspcntr |= DISPPLANE_8BPP;
		break;
	case DRM_FORMAT_RGB565:
		dspcntr |= DISPPLANE_BGRX565;
		break;
	case DRM_FORMAT_XRGB8888:
		dspcntr |= DISPPLANE_BGRX888;
		break;
	case DRM_FORMAT_XBGR8888:
		dspcntr |= DISPPLANE_RGBX888;
		break;
	case DRM_FORMAT_XRGB2101010:
		dspcntr |= DISPPLANE_BGRX101010;
		break;
	case DRM_FORMAT_XBGR2101010:
		dspcntr |= DISPPLANE_RGBX101010;
		break;
	default:
		BUG();
	}

	if (fb->modifier == I915_FORMAT_MOD_X_TILED)
		dspcntr |= DISPPLANE_TILED;

	if (rotation & DRM_ROTATE_180)
		dspcntr |= DISPPLANE_ROTATE_180;

	if (!IS_HASWELL(dev_priv) && !IS_BROADWELL(dev_priv))
		dspcntr |= DISPPLANE_TRICKLE_FEED_DISABLE;

	intel_add_fb_offsets(&x, &y, plane_state, 0);

	intel_crtc->dspaddr_offset =
		intel_compute_tile_offset(&x, &y, plane_state, 0);

	/* HSW+ does this automagically in hardware */
	if (!IS_HASWELL(dev_priv) && !IS_BROADWELL(dev_priv) &&
	    rotation & DRM_ROTATE_180) {
		x += crtc_state->pipe_src_w - 1;
		y += crtc_state->pipe_src_h - 1;
	}

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	intel_crtc->adjusted_x = x;
	intel_crtc->adjusted_y = y;

	I915_WRITE(reg, dspcntr);

	I915_WRITE(DSPSTRIDE(plane), fb->pitches[0]);
	I915_WRITE(DSPSURF(plane),
		   intel_plane_ggtt_offset(plane_state) +
		   intel_crtc->dspaddr_offset);
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		I915_WRITE(DSPOFFSET(plane), (y << 16) | x);
	} else {
		I915_WRITE(DSPTILEOFF(plane), (y << 16) | x);
		I915_WRITE(DSPLINOFF(plane), linear_offset);
	}
	POSTING_READ(reg);
}

u32 intel_fb_stride_alignment(const struct drm_i915_private *dev_priv,
			      uint64_t fb_modifier, uint32_t pixel_format)
{
	if (fb_modifier == DRM_FORMAT_MOD_NONE) {
		return 64;
	} else {
		int cpp = drm_format_plane_cpp(pixel_format, 0);

		return intel_tile_width_bytes(dev_priv, fb_modifier, cpp);
	}
}

static void skl_detach_scaler(struct intel_crtc *intel_crtc, int id)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	I915_WRITE(SKL_PS_CTRL(intel_crtc->pipe, id), 0);
	I915_WRITE(SKL_PS_WIN_POS(intel_crtc->pipe, id), 0);
	I915_WRITE(SKL_PS_WIN_SZ(intel_crtc->pipe, id), 0);
}

/*
 * This function detaches (aka. unbinds) unused scalers in hardware
 */
static void skl_detach_scalers(struct intel_crtc *intel_crtc)
{
	struct intel_crtc_scaler_state *scaler_state;
	int i;

	scaler_state = &intel_crtc->config->scaler_state;

	/* loop through and disable scalers that aren't in use */
	for (i = 0; i < intel_crtc->num_scalers; i++) {
		if (!scaler_state->scalers[i].in_use)
			skl_detach_scaler(intel_crtc, i);
	}
}

u32 skl_plane_stride(const struct drm_framebuffer *fb, int plane,
		     unsigned int rotation)
{
	const struct drm_i915_private *dev_priv = to_i915(fb->dev);
	u32 stride = intel_fb_pitch(fb, plane, rotation);

	/*
	 * The stride is either expressed as a multiple of 64 bytes chunks for
	 * linear buffers or in number of tiles for tiled buffers.
	 */
	if (drm_rotation_90_or_270(rotation)) {
		int cpp = fb->format->cpp[plane];

		stride /= intel_tile_height(dev_priv, fb->modifier, cpp);
	} else {
		stride /= intel_fb_stride_alignment(dev_priv, fb->modifier,
						    fb->format->format);
	}

	return stride;
}

u32 skl_plane_ctl_format(uint32_t pixel_format)
{
	switch (pixel_format) {
	case DRM_FORMAT_C8:
		return PLANE_CTL_FORMAT_INDEXED;
	case DRM_FORMAT_RGB565:
		return PLANE_CTL_FORMAT_RGB_565;
	case DRM_FORMAT_XBGR8888:
		return PLANE_CTL_FORMAT_XRGB_8888 | PLANE_CTL_ORDER_RGBX;
	case DRM_FORMAT_XRGB8888:
		return PLANE_CTL_FORMAT_XRGB_8888;
	/*
	 * XXX: For ARBG/ABGR formats we default to expecting scanout buffers
	 * to be already pre-multiplied. We need to add a knob (or a different
	 * DRM_FORMAT) for user-space to configure that.
	 */
	case DRM_FORMAT_ABGR8888:
		return PLANE_CTL_FORMAT_XRGB_8888 | PLANE_CTL_ORDER_RGBX |
			PLANE_CTL_ALPHA_SW_PREMULTIPLY;
	case DRM_FORMAT_ARGB8888:
		return PLANE_CTL_FORMAT_XRGB_8888 |
			PLANE_CTL_ALPHA_SW_PREMULTIPLY;
	case DRM_FORMAT_XRGB2101010:
		return PLANE_CTL_FORMAT_XRGB_2101010;
	case DRM_FORMAT_XBGR2101010:
		return PLANE_CTL_ORDER_RGBX | PLANE_CTL_FORMAT_XRGB_2101010;
	case DRM_FORMAT_YUYV:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_YUYV;
	case DRM_FORMAT_YVYU:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_YVYU;
	case DRM_FORMAT_UYVY:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_UYVY;
	case DRM_FORMAT_VYUY:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_VYUY;
	default:
		MISSING_CASE(pixel_format);
	}

	return 0;
}

u32 skl_plane_ctl_tiling(uint64_t fb_modifier)
{
	switch (fb_modifier) {
	case DRM_FORMAT_MOD_NONE:
		break;
	case I915_FORMAT_MOD_X_TILED:
		return PLANE_CTL_TILED_X;
	case I915_FORMAT_MOD_Y_TILED:
		return PLANE_CTL_TILED_Y;
	case I915_FORMAT_MOD_Yf_TILED:
		return PLANE_CTL_TILED_YF;
	default:
		MISSING_CASE(fb_modifier);
	}

	return 0;
}

u32 skl_plane_ctl_rotation(unsigned int rotation)
{
	switch (rotation) {
	case DRM_ROTATE_0:
		break;
	/*
	 * DRM_ROTATE_ is counter clockwise to stay compatible with Xrandr
	 * while i915 HW rotation is clockwise, thats why this swapping.
	 */
	case DRM_ROTATE_90:
		return PLANE_CTL_ROTATE_270;
	case DRM_ROTATE_180:
		return PLANE_CTL_ROTATE_180;
	case DRM_ROTATE_270:
		return PLANE_CTL_ROTATE_90;
	default:
		MISSING_CASE(rotation);
	}

	return 0;
}

static void skylake_update_primary_plane(struct drm_plane *plane,
					 const struct intel_crtc_state *crtc_state,
					 const struct intel_plane_state *plane_state)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_framebuffer *fb = plane_state->base.fb;
	enum plane_id plane_id = to_intel_plane(plane)->id;
	enum pipe pipe = to_intel_plane(plane)->pipe;
	u32 plane_ctl;
	unsigned int rotation = plane_state->base.rotation;
	u32 stride = skl_plane_stride(fb, 0, rotation);
	u32 surf_addr = plane_state->main.offset;
	int scaler_id = plane_state->scaler_id;
	int src_x = plane_state->main.x;
	int src_y = plane_state->main.y;
	int src_w = drm_rect_width(&plane_state->base.src) >> 16;
	int src_h = drm_rect_height(&plane_state->base.src) >> 16;
	int dst_x = plane_state->base.dst.x1;
	int dst_y = plane_state->base.dst.y1;
	int dst_w = drm_rect_width(&plane_state->base.dst);
	int dst_h = drm_rect_height(&plane_state->base.dst);

	plane_ctl = PLANE_CTL_ENABLE;

	if (IS_GEMINILAKE(dev_priv)) {
		I915_WRITE(PLANE_COLOR_CTL(pipe, plane_id),
			   PLANE_COLOR_PIPE_GAMMA_ENABLE |
			   PLANE_COLOR_PIPE_CSC_ENABLE |
			   PLANE_COLOR_PLANE_GAMMA_DISABLE);
	} else {
		plane_ctl |=
			PLANE_CTL_PIPE_GAMMA_ENABLE |
			PLANE_CTL_PIPE_CSC_ENABLE |
			PLANE_CTL_PLANE_GAMMA_DISABLE;
	}

	plane_ctl |= skl_plane_ctl_format(fb->format->format);
	plane_ctl |= skl_plane_ctl_tiling(fb->modifier);
	plane_ctl |= skl_plane_ctl_rotation(rotation);

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	dst_w--;
	dst_h--;

	intel_crtc->dspaddr_offset = surf_addr;

	intel_crtc->adjusted_x = src_x;
	intel_crtc->adjusted_y = src_y;

	I915_WRITE(PLANE_CTL(pipe, plane_id), plane_ctl);
	I915_WRITE(PLANE_OFFSET(pipe, plane_id), (src_y << 16) | src_x);
	I915_WRITE(PLANE_STRIDE(pipe, plane_id), stride);
	I915_WRITE(PLANE_SIZE(pipe, plane_id), (src_h << 16) | src_w);

	if (scaler_id >= 0) {
		uint32_t ps_ctrl = 0;

		WARN_ON(!dst_w || !dst_h);
		ps_ctrl = PS_SCALER_EN | PS_PLANE_SEL(plane_id) |
			crtc_state->scaler_state.scalers[scaler_id].mode;
		I915_WRITE(SKL_PS_CTRL(pipe, scaler_id), ps_ctrl);
		I915_WRITE(SKL_PS_PWR_GATE(pipe, scaler_id), 0);
		I915_WRITE(SKL_PS_WIN_POS(pipe, scaler_id), (dst_x << 16) | dst_y);
		I915_WRITE(SKL_PS_WIN_SZ(pipe, scaler_id), (dst_w << 16) | dst_h);
		I915_WRITE(PLANE_POS(pipe, plane_id), 0);
	} else {
		I915_WRITE(PLANE_POS(pipe, plane_id), (dst_y << 16) | dst_x);
	}

	I915_WRITE(PLANE_SURF(pipe, plane_id),
		   intel_plane_ggtt_offset(plane_state) + surf_addr);

	POSTING_READ(PLANE_SURF(pipe, plane_id));
}

static void skylake_disable_primary_plane(struct drm_plane *primary,
					  struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum plane_id plane_id = to_intel_plane(primary)->id;
	enum pipe pipe = to_intel_plane(primary)->pipe;

	I915_WRITE(PLANE_CTL(pipe, plane_id), 0);
	I915_WRITE(PLANE_SURF(pipe, plane_id), 0);
	POSTING_READ(PLANE_SURF(pipe, plane_id));
}

/* Assume fb object is pinned & idle & fenced and just update base pointers */
static int
intel_pipe_set_base_atomic(struct drm_crtc *crtc, struct drm_framebuffer *fb,
			   int x, int y, enum mode_set_atomic state)
{
	/* Support for kgdboc is disabled, this needs a major rework. */
	DRM_ERROR("legacy panic handler not supported any more.\n");

	return -ENODEV;
}

static void intel_complete_page_flips(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc;

	for_each_intel_crtc(&dev_priv->drm, crtc)
		intel_finish_page_flip_cs(dev_priv, crtc->pipe);
}

static void intel_update_primary_planes(struct drm_device *dev)
{
	struct drm_crtc *crtc;

	for_each_crtc(dev, crtc) {
		struct intel_plane *plane = to_intel_plane(crtc->primary);
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);

		if (plane_state->base.visible)
			plane->update_plane(&plane->base,
					    to_intel_crtc_state(crtc->state),
					    plane_state);
	}
}

static int
__intel_display_resume(struct drm_device *dev,
		       struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int i, ret;

	intel_modeset_setup_hw_state(dev);
	i915_redisable_vga(to_i915(dev));

	if (!state)
		return 0;

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		/*
		 * Force recalculation even if we restore
		 * current state. With fast modeset this may not result
		 * in a modeset when the state is compatible.
		 */
		crtc_state->mode_changed = true;
	}

	/* ignore any reset values/BIOS leftovers in the WM registers */
	to_intel_atomic_state(state)->skip_intermediate_wm = true;

	ret = drm_atomic_commit(state);

	WARN_ON(ret == -EDEADLK);
	return ret;
}

static bool gpu_reset_clobbers_display(struct drm_i915_private *dev_priv)
{
	return intel_has_gpu_reset(dev_priv) &&
		INTEL_GEN(dev_priv) < 5 && !IS_G4X(dev_priv);
}

void intel_prepare_reset(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct drm_modeset_acquire_ctx *ctx = &dev_priv->reset_ctx;
	struct drm_atomic_state *state;
	int ret;

	/*
	 * Need mode_config.mutex so that we don't
	 * trample ongoing ->detect() and whatnot.
	 */
	mutex_lock(&dev->mode_config.mutex);
	drm_modeset_acquire_init(ctx, 0);
	while (1) {
		ret = drm_modeset_lock_all_ctx(dev, ctx);
		if (ret != -EDEADLK)
			break;

		drm_modeset_backoff(ctx);
	}

	/* reset doesn't touch the display, but flips might get nuked anyway, */
	if (!i915.force_reset_modeset_test &&
	    !gpu_reset_clobbers_display(dev_priv))
		return;

	/*
	 * Disabling the crtcs gracefully seems nicer. Also the
	 * g33 docs say we should at least disable all the planes.
	 */
	state = drm_atomic_helper_duplicate_state(dev, ctx);
	if (IS_ERR(state)) {
		ret = PTR_ERR(state);
		DRM_ERROR("Duplicating state failed with %i\n", ret);
		return;
	}

	ret = drm_atomic_helper_disable_all(dev, ctx);
	if (ret) {
		DRM_ERROR("Suspending crtc's failed with %i\n", ret);
		drm_atomic_state_put(state);
		return;
	}

	dev_priv->modeset_restore_state = state;
	state->acquire_ctx = ctx;
}

void intel_finish_reset(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct drm_modeset_acquire_ctx *ctx = &dev_priv->reset_ctx;
	struct drm_atomic_state *state = dev_priv->modeset_restore_state;
	int ret;

	/*
	 * Flips in the rings will be nuked by the reset,
	 * so complete all pending flips so that user space
	 * will get its events and not get stuck.
	 */
	intel_complete_page_flips(dev_priv);

	dev_priv->modeset_restore_state = NULL;

	/* reset doesn't touch the display */
	if (!gpu_reset_clobbers_display(dev_priv)) {
		if (!state) {
			/*
			 * Flips in the rings have been nuked by the reset,
			 * so update the base address of all primary
			 * planes to the the last fb to make sure we're
			 * showing the correct fb after a reset.
			 *
			 * FIXME: Atomic will make this obsolete since we won't schedule
			 * CS-based flips (which might get lost in gpu resets) any more.
			 */
			intel_update_primary_planes(dev);
		} else {
			ret = __intel_display_resume(dev, state);
			if (ret)
				DRM_ERROR("Restoring old state failed with %i\n", ret);
		}
	} else {
		/*
		 * The display has been reset as well,
		 * so need a full re-initialization.
		 */
		intel_runtime_pm_disable_interrupts(dev_priv);
		intel_runtime_pm_enable_interrupts(dev_priv);

		intel_pps_unlock_regs_wa(dev_priv);
		intel_modeset_init_hw(dev);

		spin_lock_irq(&dev_priv->irq_lock);
		if (dev_priv->display.hpd_irq_setup)
			dev_priv->display.hpd_irq_setup(dev_priv);
		spin_unlock_irq(&dev_priv->irq_lock);

		ret = __intel_display_resume(dev, state);
		if (ret)
			DRM_ERROR("Restoring old state failed with %i\n", ret);

		intel_hpd_init(dev_priv);
	}

	if (state)
		drm_atomic_state_put(state);
	drm_modeset_drop_locks(ctx);
	drm_modeset_acquire_fini(ctx);
	mutex_unlock(&dev->mode_config.mutex);
}

static bool abort_flip_on_reset(struct intel_crtc *crtc)
{
	struct i915_gpu_error *error = &to_i915(crtc->base.dev)->gpu_error;

	if (i915_reset_in_progress(error))
		return true;

	if (crtc->reset_count != i915_reset_count(error))
		return true;

	return false;
}

static bool intel_crtc_has_pending_flip(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	bool pending;

	if (abort_flip_on_reset(intel_crtc))
		return false;

	spin_lock_irq(&dev->event_lock);
	pending = to_intel_crtc(crtc)->flip_work != NULL;
	spin_unlock_irq(&dev->event_lock);

	return pending;
}

static void intel_update_pipe_config(struct intel_crtc *crtc,
				     struct intel_crtc_state *old_crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_crtc_state *pipe_config =
		to_intel_crtc_state(crtc->base.state);

	/* drm_atomic_helper_update_legacy_modeset_state might not be called. */
	crtc->base.mode = crtc->base.state->mode;

	DRM_DEBUG_KMS("Updating pipe size %ix%i -> %ix%i\n",
		      old_crtc_state->pipe_src_w, old_crtc_state->pipe_src_h,
		      pipe_config->pipe_src_w, pipe_config->pipe_src_h);

	/*
	 * Update pipe size and adjust fitter if needed: the reason for this is
	 * that in compute_mode_changes we check the native mode (not the pfit
	 * mode) to see if we can flip rather than do a full mode set. In the
	 * fastboot case, we'll flip, but if we don't update the pipesrc and
	 * pfit state, we'll end up with a big fb scanned out into the wrong
	 * sized surface.
	 */

	I915_WRITE(PIPESRC(crtc->pipe),
		   ((pipe_config->pipe_src_w - 1) << 16) |
		   (pipe_config->pipe_src_h - 1));

	/* on skylake this is done by detaching scalers */
	if (INTEL_GEN(dev_priv) >= 9) {
		skl_detach_scalers(crtc);

		if (pipe_config->pch_pfit.enabled)
			skylake_pfit_enable(crtc);
	} else if (HAS_PCH_SPLIT(dev_priv)) {
		if (pipe_config->pch_pfit.enabled)
			ironlake_pfit_enable(crtc);
		else if (old_crtc_state->pch_pfit.enabled)
			ironlake_pfit_disable(crtc, true);
	}
}

static void intel_fdi_normal_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	i915_reg_t reg;
	u32 temp;

	/* enable normal train */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	if (IS_IVYBRIDGE(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_NONE_IVB;
		temp |= FDI_LINK_TRAIN_NONE_IVB | FDI_TX_ENHANCE_FRAME_ENABLE;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_NONE | FDI_TX_ENHANCE_FRAME_ENABLE;
	}
	I915_WRITE(reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	if (HAS_PCH_CPT(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_NORMAL_CPT;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_NONE;
	}
	I915_WRITE(reg, temp | FDI_RX_ENHANCE_FRAME_ENABLE);

	/* wait one idle pattern time */
	POSTING_READ(reg);
	udelay(1000);

	/* IVB wants error correction enabled */
	if (IS_IVYBRIDGE(dev_priv))
		I915_WRITE(reg, I915_READ(reg) | FDI_FS_ERRC_ENABLE |
			   FDI_FE_ERRC_ENABLE);
}

/* The FDI link training functions for ILK/Ibexpeak. */
static void ironlake_fdi_link_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	i915_reg_t reg;
	u32 temp, tries;

	/* FDI needs bits from pipe first */
	assert_pipe_enabled(dev_priv, pipe);

	/* Train 1: umask FDI RX Interrupt symbol_lock and bit_lock bit
	   for train result */
	reg = FDI_RX_IMR(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_RX_SYMBOL_LOCK;
	temp &= ~FDI_RX_BIT_LOCK;
	I915_WRITE(reg, temp);
	I915_READ(reg);
	udelay(150);

	/* enable CPU FDI TX and PCH FDI RX */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_DP_PORT_WIDTH_MASK;
	temp |= FDI_DP_PORT_WIDTH(intel_crtc->config->fdi_lanes);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_1;
	I915_WRITE(reg, temp | FDI_TX_ENABLE);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_1;
	I915_WRITE(reg, temp | FDI_RX_ENABLE);

	POSTING_READ(reg);
	udelay(150);

	/* Ironlake workaround, enable clock pointer after FDI enable*/
	I915_WRITE(FDI_RX_CHICKEN(pipe), FDI_RX_PHASE_SYNC_POINTER_OVR);
	I915_WRITE(FDI_RX_CHICKEN(pipe), FDI_RX_PHASE_SYNC_POINTER_OVR |
		   FDI_RX_PHASE_SYNC_POINTER_EN);

	reg = FDI_RX_IIR(pipe);
	for (tries = 0; tries < 5; tries++) {
		temp = I915_READ(reg);
		DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);

		if ((temp & FDI_RX_BIT_LOCK)) {
			DRM_DEBUG_KMS("FDI train 1 done.\n");
			I915_WRITE(reg, temp | FDI_RX_BIT_LOCK);
			break;
		}
	}
	if (tries == 5)
		DRM_ERROR("FDI train 1 fail!\n");

	/* Train 2 */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_2;
	I915_WRITE(reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_2;
	I915_WRITE(reg, temp);

	POSTING_READ(reg);
	udelay(150);

	reg = FDI_RX_IIR(pipe);
	for (tries = 0; tries < 5; tries++) {
		temp = I915_READ(reg);
		DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);

		if (temp & FDI_RX_SYMBOL_LOCK) {
			I915_WRITE(reg, temp | FDI_RX_SYMBOL_LOCK);
			DRM_DEBUG_KMS("FDI train 2 done.\n");
			break;
		}
	}
	if (tries == 5)
		DRM_ERROR("FDI train 2 fail!\n");

	DRM_DEBUG_KMS("FDI train done\n");

}

static const int snb_b_fdi_train_param[] = {
	FDI_LINK_TRAIN_400MV_0DB_SNB_B,
	FDI_LINK_TRAIN_400MV_6DB_SNB_B,
	FDI_LINK_TRAIN_600MV_3_5DB_SNB_B,
	FDI_LINK_TRAIN_800MV_0DB_SNB_B,
};

/* The FDI link training functions for SNB/Cougarpoint. */
static void gen6_fdi_link_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	i915_reg_t reg;
	u32 temp, i, retry;

	/* Train 1: umask FDI RX Interrupt symbol_lock and bit_lock bit
	   for train result */
	reg = FDI_RX_IMR(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_RX_SYMBOL_LOCK;
	temp &= ~FDI_RX_BIT_LOCK;
	I915_WRITE(reg, temp);

	POSTING_READ(reg);
	udelay(150);

	/* enable CPU FDI TX and PCH FDI RX */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_DP_PORT_WIDTH_MASK;
	temp |= FDI_DP_PORT_WIDTH(intel_crtc->config->fdi_lanes);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_1;
	temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
	/* SNB-B */
	temp |= FDI_LINK_TRAIN_400MV_0DB_SNB_B;
	I915_WRITE(reg, temp | FDI_TX_ENABLE);

	I915_WRITE(FDI_RX_MISC(pipe),
		   FDI_RX_TP1_TO_TP2_48 | FDI_RX_FDI_DELAY_90);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	if (HAS_PCH_CPT(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_PATTERN_1_CPT;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_1;
	}
	I915_WRITE(reg, temp | FDI_RX_ENABLE);

	POSTING_READ(reg);
	udelay(150);

	for (i = 0; i < 4; i++) {
		reg = FDI_TX_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
		temp |= snb_b_fdi_train_param[i];
		I915_WRITE(reg, temp);

		POSTING_READ(reg);
		udelay(500);

		for (retry = 0; retry < 5; retry++) {
			reg = FDI_RX_IIR(pipe);
			temp = I915_READ(reg);
			DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);
			if (temp & FDI_RX_BIT_LOCK) {
				I915_WRITE(reg, temp | FDI_RX_BIT_LOCK);
				DRM_DEBUG_KMS("FDI train 1 done.\n");
				break;
			}
			udelay(50);
		}
		if (retry < 5)
			break;
	}
	if (i == 4)
		DRM_ERROR("FDI train 1 fail!\n");

	/* Train 2 */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_2;
	if (IS_GEN6(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
		/* SNB-B */
		temp |= FDI_LINK_TRAIN_400MV_0DB_SNB_B;
	}
	I915_WRITE(reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	if (HAS_PCH_CPT(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_PATTERN_2_CPT;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_2;
	}
	I915_WRITE(reg, temp);

	POSTING_READ(reg);
	udelay(150);

	for (i = 0; i < 4; i++) {
		reg = FDI_TX_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
		temp |= snb_b_fdi_train_param[i];
		I915_WRITE(reg, temp);

		POSTING_READ(reg);
		udelay(500);

		for (retry = 0; retry < 5; retry++) {
			reg = FDI_RX_IIR(pipe);
			temp = I915_READ(reg);
			DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);
			if (temp & FDI_RX_SYMBOL_LOCK) {
				I915_WRITE(reg, temp | FDI_RX_SYMBOL_LOCK);
				DRM_DEBUG_KMS("FDI train 2 done.\n");
				break;
			}
			udelay(50);
		}
		if (retry < 5)
			break;
	}
	if (i == 4)
		DRM_ERROR("FDI train 2 fail!\n");

	DRM_DEBUG_KMS("FDI train done.\n");
}

/* Manual link training for Ivy Bridge A0 parts */
static void ivb_manual_fdi_link_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	i915_reg_t reg;
	u32 temp, i, j;

	/* Train 1: umask FDI RX Interrupt symbol_lock and bit_lock bit
	   for train result */
	reg = FDI_RX_IMR(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_RX_SYMBOL_LOCK;
	temp &= ~FDI_RX_BIT_LOCK;
	I915_WRITE(reg, temp);

	POSTING_READ(reg);
	udelay(150);

	DRM_DEBUG_KMS("FDI_RX_IIR before link train 0x%x\n",
		      I915_READ(FDI_RX_IIR(pipe)));

	/* Try each vswing and preemphasis setting twice before moving on */
	for (j = 0; j < ARRAY_SIZE(snb_b_fdi_train_param) * 2; j++) {
		/* disable first in case we need to retry */
		reg = FDI_TX_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~(FDI_LINK_TRAIN_AUTO | FDI_LINK_TRAIN_NONE_IVB);
		temp &= ~FDI_TX_ENABLE;
		I915_WRITE(reg, temp);

		reg = FDI_RX_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~FDI_LINK_TRAIN_AUTO;
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp &= ~FDI_RX_ENABLE;
		I915_WRITE(reg, temp);

		/* enable CPU FDI TX and PCH FDI RX */
		reg = FDI_TX_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~FDI_DP_PORT_WIDTH_MASK;
		temp |= FDI_DP_PORT_WIDTH(intel_crtc->config->fdi_lanes);
		temp |= FDI_LINK_TRAIN_PATTERN_1_IVB;
		temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
		temp |= snb_b_fdi_train_param[j/2];
		temp |= FDI_COMPOSITE_SYNC;
		I915_WRITE(reg, temp | FDI_TX_ENABLE);

		I915_WRITE(FDI_RX_MISC(pipe),
			   FDI_RX_TP1_TO_TP2_48 | FDI_RX_FDI_DELAY_90);

		reg = FDI_RX_CTL(pipe);
		temp = I915_READ(reg);
		temp |= FDI_LINK_TRAIN_PATTERN_1_CPT;
		temp |= FDI_COMPOSITE_SYNC;
		I915_WRITE(reg, temp | FDI_RX_ENABLE);

		POSTING_READ(reg);
		udelay(1); /* should be 0.5us */

		for (i = 0; i < 4; i++) {
			reg = FDI_RX_IIR(pipe);
			temp = I915_READ(reg);
			DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);

			if (temp & FDI_RX_BIT_LOCK ||
			    (I915_READ(reg) & FDI_RX_BIT_LOCK)) {
				I915_WRITE(reg, temp | FDI_RX_BIT_LOCK);
				DRM_DEBUG_KMS("FDI train 1 done, level %i.\n",
					      i);
				break;
			}
			udelay(1); /* should be 0.5us */
		}
		if (i == 4) {
			DRM_DEBUG_KMS("FDI train 1 fail on vswing %d\n", j / 2);
			continue;
		}

		/* Train 2 */
		reg = FDI_TX_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~FDI_LINK_TRAIN_NONE_IVB;
		temp |= FDI_LINK_TRAIN_PATTERN_2_IVB;
		I915_WRITE(reg, temp);

		reg = FDI_RX_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_PATTERN_2_CPT;
		I915_WRITE(reg, temp);

		POSTING_READ(reg);
		udelay(2); /* should be 1.5us */

		for (i = 0; i < 4; i++) {
			reg = FDI_RX_IIR(pipe);
			temp = I915_READ(reg);
			DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);

			if (temp & FDI_RX_SYMBOL_LOCK ||
			    (I915_READ(reg) & FDI_RX_SYMBOL_LOCK)) {
				I915_WRITE(reg, temp | FDI_RX_SYMBOL_LOCK);
				DRM_DEBUG_KMS("FDI train 2 done, level %i.\n",
					      i);
				goto train_done;
			}
			udelay(2); /* should be 1.5us */
		}
		if (i == 4)
			DRM_DEBUG_KMS("FDI train 2 fail on vswing %d\n", j / 2);
	}

train_done:
	DRM_DEBUG_KMS("FDI train done.\n");
}

static void ironlake_fdi_pll_enable(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe = intel_crtc->pipe;
	i915_reg_t reg;
	u32 temp;

	/* enable PCH FDI RX PLL, wait warmup plus DMI latency */
	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~(FDI_DP_PORT_WIDTH_MASK | (0x7 << 16));
	temp |= FDI_DP_PORT_WIDTH(intel_crtc->config->fdi_lanes);
	temp |= (I915_READ(PIPECONF(pipe)) & PIPECONF_BPC_MASK) << 11;
	I915_WRITE(reg, temp | FDI_RX_PLL_ENABLE);

	POSTING_READ(reg);
	udelay(200);

	/* Switch from Rawclk to PCDclk */
	temp = I915_READ(reg);
	I915_WRITE(reg, temp | FDI_PCDCLK);

	POSTING_READ(reg);
	udelay(200);

	/* Enable CPU FDI TX PLL, always on for Ironlake */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	if ((temp & FDI_TX_PLL_ENABLE) == 0) {
		I915_WRITE(reg, temp | FDI_TX_PLL_ENABLE);

		POSTING_READ(reg);
		udelay(100);
	}
}

static void ironlake_fdi_pll_disable(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe = intel_crtc->pipe;
	i915_reg_t reg;
	u32 temp;

	/* Switch from PCDclk to Rawclk */
	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	I915_WRITE(reg, temp & ~FDI_PCDCLK);

	/* Disable CPU FDI TX PLL */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	I915_WRITE(reg, temp & ~FDI_TX_PLL_ENABLE);

	POSTING_READ(reg);
	udelay(100);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	I915_WRITE(reg, temp & ~FDI_RX_PLL_ENABLE);

	/* Wait for the clocks to turn off. */
	POSTING_READ(reg);
	udelay(100);
}

static void ironlake_fdi_disable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	i915_reg_t reg;
	u32 temp;

	/* disable CPU FDI tx and PCH FDI rx */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	I915_WRITE(reg, temp & ~FDI_TX_ENABLE);
	POSTING_READ(reg);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~(0x7 << 16);
	temp |= (I915_READ(PIPECONF(pipe)) & PIPECONF_BPC_MASK) << 11;
	I915_WRITE(reg, temp & ~FDI_RX_ENABLE);

	POSTING_READ(reg);
	udelay(100);

	/* Ironlake workaround, disable clock pointer after downing FDI */
	if (HAS_PCH_IBX(dev_priv))
		I915_WRITE(FDI_RX_CHICKEN(pipe), FDI_RX_PHASE_SYNC_POINTER_OVR);

	/* still set train pattern 1 */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_1;
	I915_WRITE(reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	if (HAS_PCH_CPT(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_PATTERN_1_CPT;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_1;
	}
	/* BPC in FDI rx is consistent with that in PIPECONF */
	temp &= ~(0x07 << 16);
	temp |= (I915_READ(PIPECONF(pipe)) & PIPECONF_BPC_MASK) << 11;
	I915_WRITE(reg, temp);

	POSTING_READ(reg);
	udelay(100);
}

bool intel_has_pending_fb_unpin(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc;

	/* Note that we don't need to be called with mode_config.lock here
	 * as our list of CRTC objects is static for the lifetime of the
	 * device and so cannot disappear as we iterate. Similarly, we can
	 * happily treat the predicates as racy, atomic checks as userspace
	 * cannot claim and pin a new fb without at least acquring the
	 * struct_mutex and so serialising with us.
	 */
	for_each_intel_crtc(&dev_priv->drm, crtc) {
		if (atomic_read(&crtc->unpin_work_count) == 0)
			continue;

		if (crtc->flip_work)
			intel_wait_for_vblank(dev_priv, crtc->pipe);

		return true;
	}

	return false;
}

static void page_flip_completed(struct intel_crtc *intel_crtc)
{
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);
	struct intel_flip_work *work = intel_crtc->flip_work;

	intel_crtc->flip_work = NULL;

	if (work->event)
		drm_crtc_send_vblank_event(&intel_crtc->base, work->event);

	drm_crtc_vblank_put(&intel_crtc->base);

	wake_up_all(&dev_priv->pending_flip_queue);
	trace_i915_flip_complete(intel_crtc->plane,
				 work->pending_flip_obj);

	queue_work(dev_priv->wq, &work->unpin_work);
}

static int intel_crtc_wait_for_pending_flips(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	long ret;

	WARN_ON(waitqueue_active(&dev_priv->pending_flip_queue));

	ret = wait_event_interruptible_timeout(
					dev_priv->pending_flip_queue,
					!intel_crtc_has_pending_flip(crtc),
					60*HZ);

	if (ret < 0)
		return ret;

	if (ret == 0) {
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
		struct intel_flip_work *work;

		spin_lock_irq(&dev->event_lock);
		work = intel_crtc->flip_work;
		if (work && !is_mmio_work(work)) {
			WARN_ONCE(1, "Removing stuck page flip\n");
			page_flip_completed(intel_crtc);
		}
		spin_unlock_irq(&dev->event_lock);
	}

	return 0;
}

void lpt_disable_iclkip(struct drm_i915_private *dev_priv)
{
	u32 temp;

	I915_WRITE(PIXCLK_GATE, PIXCLK_GATE_GATE);

	mutex_lock(&dev_priv->sb_lock);

	temp = intel_sbi_read(dev_priv, SBI_SSCCTL6, SBI_ICLK);
	temp |= SBI_SSCCTL_DISABLE;
	intel_sbi_write(dev_priv, SBI_SSCCTL6, temp, SBI_ICLK);

	mutex_unlock(&dev_priv->sb_lock);
}

/* Program iCLKIP clock to the desired frequency */
static void lpt_program_iclkip(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	int clock = to_intel_crtc(crtc)->config->base.adjusted_mode.crtc_clock;
	u32 divsel, phaseinc, auxdiv, phasedir = 0;
	u32 temp;

	lpt_disable_iclkip(dev_priv);

	/* The iCLK virtual clock root frequency is in MHz,
	 * but the adjusted_mode->crtc_clock in in KHz. To get the
	 * divisors, it is necessary to divide one by another, so we
	 * convert the virtual clock precision to KHz here for higher
	 * precision.
	 */
	for (auxdiv = 0; auxdiv < 2; auxdiv++) {
		u32 iclk_virtual_root_freq = 172800 * 1000;
		u32 iclk_pi_range = 64;
		u32 desired_divisor;

		desired_divisor = DIV_ROUND_CLOSEST(iclk_virtual_root_freq,
						    clock << auxdiv);
		divsel = (desired_divisor / iclk_pi_range) - 2;
		phaseinc = desired_divisor % iclk_pi_range;

		/*
		 * Near 20MHz is a corner case which is
		 * out of range for the 7-bit divisor
		 */
		if (divsel <= 0x7f)
			break;
	}

	/* This should not happen with any sane values */
	WARN_ON(SBI_SSCDIVINTPHASE_DIVSEL(divsel) &
		~SBI_SSCDIVINTPHASE_DIVSEL_MASK);
	WARN_ON(SBI_SSCDIVINTPHASE_DIR(phasedir) &
		~SBI_SSCDIVINTPHASE_INCVAL_MASK);

	DRM_DEBUG_KMS("iCLKIP clock: found settings for %dKHz refresh rate: auxdiv=%x, divsel=%x, phasedir=%x, phaseinc=%x\n",
			clock,
			auxdiv,
			divsel,
			phasedir,
			phaseinc);

	mutex_lock(&dev_priv->sb_lock);

	/* Program SSCDIVINTPHASE6 */
	temp = intel_sbi_read(dev_priv, SBI_SSCDIVINTPHASE6, SBI_ICLK);
	temp &= ~SBI_SSCDIVINTPHASE_DIVSEL_MASK;
	temp |= SBI_SSCDIVINTPHASE_DIVSEL(divsel);
	temp &= ~SBI_SSCDIVINTPHASE_INCVAL_MASK;
	temp |= SBI_SSCDIVINTPHASE_INCVAL(phaseinc);
	temp |= SBI_SSCDIVINTPHASE_DIR(phasedir);
	temp |= SBI_SSCDIVINTPHASE_PROPAGATE;
	intel_sbi_write(dev_priv, SBI_SSCDIVINTPHASE6, temp, SBI_ICLK);

	/* Program SSCAUXDIV */
	temp = intel_sbi_read(dev_priv, SBI_SSCAUXDIV6, SBI_ICLK);
	temp &= ~SBI_SSCAUXDIV_FINALDIV2SEL(1);
	temp |= SBI_SSCAUXDIV_FINALDIV2SEL(auxdiv);
	intel_sbi_write(dev_priv, SBI_SSCAUXDIV6, temp, SBI_ICLK);

	/* Enable modulator and associated divider */
	temp = intel_sbi_read(dev_priv, SBI_SSCCTL6, SBI_ICLK);
	temp &= ~SBI_SSCCTL_DISABLE;
	intel_sbi_write(dev_priv, SBI_SSCCTL6, temp, SBI_ICLK);

	mutex_unlock(&dev_priv->sb_lock);

	/* Wait for initialization time */
	udelay(24);

	I915_WRITE(PIXCLK_GATE, PIXCLK_GATE_UNGATE);
}

int lpt_get_iclkip(struct drm_i915_private *dev_priv)
{
	u32 divsel, phaseinc, auxdiv;
	u32 iclk_virtual_root_freq = 172800 * 1000;
	u32 iclk_pi_range = 64;
	u32 desired_divisor;
	u32 temp;

	if ((I915_READ(PIXCLK_GATE) & PIXCLK_GATE_UNGATE) == 0)
		return 0;

	mutex_lock(&dev_priv->sb_lock);

	temp = intel_sbi_read(dev_priv, SBI_SSCCTL6, SBI_ICLK);
	if (temp & SBI_SSCCTL_DISABLE) {
		mutex_unlock(&dev_priv->sb_lock);
		return 0;
	}

	temp = intel_sbi_read(dev_priv, SBI_SSCDIVINTPHASE6, SBI_ICLK);
	divsel = (temp & SBI_SSCDIVINTPHASE_DIVSEL_MASK) >>
		SBI_SSCDIVINTPHASE_DIVSEL_SHIFT;
	phaseinc = (temp & SBI_SSCDIVINTPHASE_INCVAL_MASK) >>
		SBI_SSCDIVINTPHASE_INCVAL_SHIFT;

	temp = intel_sbi_read(dev_priv, SBI_SSCAUXDIV6, SBI_ICLK);
	auxdiv = (temp & SBI_SSCAUXDIV_FINALDIV2SEL_MASK) >>
		SBI_SSCAUXDIV_FINALDIV2SEL_SHIFT;

	mutex_unlock(&dev_priv->sb_lock);

	desired_divisor = (divsel + 2) * iclk_pi_range + phaseinc;

	return DIV_ROUND_CLOSEST(iclk_virtual_root_freq,
				 desired_divisor << auxdiv);
}

static void ironlake_pch_transcoder_set_timings(struct intel_crtc *crtc,
						enum pipe pch_transcoder)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum transcoder cpu_transcoder = crtc->config->cpu_transcoder;

	I915_WRITE(PCH_TRANS_HTOTAL(pch_transcoder),
		   I915_READ(HTOTAL(cpu_transcoder)));
	I915_WRITE(PCH_TRANS_HBLANK(pch_transcoder),
		   I915_READ(HBLANK(cpu_transcoder)));
	I915_WRITE(PCH_TRANS_HSYNC(pch_transcoder),
		   I915_READ(HSYNC(cpu_transcoder)));

	I915_WRITE(PCH_TRANS_VTOTAL(pch_transcoder),
		   I915_READ(VTOTAL(cpu_transcoder)));
	I915_WRITE(PCH_TRANS_VBLANK(pch_transcoder),
		   I915_READ(VBLANK(cpu_transcoder)));
	I915_WRITE(PCH_TRANS_VSYNC(pch_transcoder),
		   I915_READ(VSYNC(cpu_transcoder)));
	I915_WRITE(PCH_TRANS_VSYNCSHIFT(pch_transcoder),
		   I915_READ(VSYNCSHIFT(cpu_transcoder)));
}

static void cpt_set_fdi_bc_bifurcation(struct drm_device *dev, bool enable)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	uint32_t temp;

	temp = I915_READ(SOUTH_CHICKEN1);
	if (!!(temp & FDI_BC_BIFURCATION_SELECT) == enable)
		return;

	WARN_ON(I915_READ(FDI_RX_CTL(PIPE_B)) & FDI_RX_ENABLE);
	WARN_ON(I915_READ(FDI_RX_CTL(PIPE_C)) & FDI_RX_ENABLE);

	temp &= ~FDI_BC_BIFURCATION_SELECT;
	if (enable)
		temp |= FDI_BC_BIFURCATION_SELECT;

	DRM_DEBUG_KMS("%sabling fdi C rx\n", enable ? "en" : "dis");
	I915_WRITE(SOUTH_CHICKEN1, temp);
	POSTING_READ(SOUTH_CHICKEN1);
}

static void ivybridge_update_fdi_bc_bifurcation(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;

	switch (intel_crtc->pipe) {
	case PIPE_A:
		break;
	case PIPE_B:
		if (intel_crtc->config->fdi_lanes > 2)
			cpt_set_fdi_bc_bifurcation(dev, false);
		else
			cpt_set_fdi_bc_bifurcation(dev, true);

		break;
	case PIPE_C:
		cpt_set_fdi_bc_bifurcation(dev, true);

		break;
	default:
		BUG();
	}
}

/* Return which DP Port should be selected for Transcoder DP control */
static enum port
intel_trans_dp_port_sel(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct intel_encoder *encoder;

	for_each_encoder_on_crtc(dev, crtc, encoder) {
		if (encoder->type == INTEL_OUTPUT_DP ||
		    encoder->type == INTEL_OUTPUT_EDP)
			return enc_to_dig_port(&encoder->base)->port;
	}

	return -1;
}

/*
 * Enable PCH resources required for PCH ports:
 *   - PCH PLLs
 *   - FDI training & RX/TX
 *   - update transcoder timings
 *   - DP transcoding bits
 *   - transcoder
 */
static void ironlake_pch_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 temp;

	assert_pch_transcoder_disabled(dev_priv, pipe);

	if (IS_IVYBRIDGE(dev_priv))
		ivybridge_update_fdi_bc_bifurcation(intel_crtc);

	/* Write the TU size bits before fdi link training, so that error
	 * detection works. */
	I915_WRITE(FDI_RX_TUSIZE1(pipe),
		   I915_READ(PIPE_DATA_M1(pipe)) & TU_SIZE_MASK);

	/* For PCH output, training FDI link */
	dev_priv->display.fdi_link_train(crtc);

	/* We need to program the right clock selection before writing the pixel
	 * mutliplier into the DPLL. */
	if (HAS_PCH_CPT(dev_priv)) {
		u32 sel;

		temp = I915_READ(PCH_DPLL_SEL);
		temp |= TRANS_DPLL_ENABLE(pipe);
		sel = TRANS_DPLLB_SEL(pipe);
		if (intel_crtc->config->shared_dpll ==
		    intel_get_shared_dpll_by_id(dev_priv, DPLL_ID_PCH_PLL_B))
			temp |= sel;
		else
			temp &= ~sel;
		I915_WRITE(PCH_DPLL_SEL, temp);
	}

	/* XXX: pch pll's can be enabled any time before we enable the PCH
	 * transcoder, and we actually should do this to not upset any PCH
	 * transcoder that already use the clock when we share it.
	 *
	 * Note that enable_shared_dpll tries to do the right thing, but
	 * get_shared_dpll unconditionally resets the pll - we need that to have
	 * the right LVDS enable sequence. */
	intel_enable_shared_dpll(intel_crtc);

	/* set transcoder timing, panel must allow it */
	assert_panel_unlocked(dev_priv, pipe);
	ironlake_pch_transcoder_set_timings(intel_crtc, pipe);

	intel_fdi_normal_train(crtc);

	/* For PCH DP, enable TRANS_DP_CTL */
	if (HAS_PCH_CPT(dev_priv) &&
	    intel_crtc_has_dp_encoder(intel_crtc->config)) {
		const struct drm_display_mode *adjusted_mode =
			&intel_crtc->config->base.adjusted_mode;
		u32 bpc = (I915_READ(PIPECONF(pipe)) & PIPECONF_BPC_MASK) >> 5;
		i915_reg_t reg = TRANS_DP_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~(TRANS_DP_PORT_SEL_MASK |
			  TRANS_DP_SYNC_MASK |
			  TRANS_DP_BPC_MASK);
		temp |= TRANS_DP_OUTPUT_ENABLE;
		temp |= bpc << 9; /* same format but at 11:9 */

		if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
			temp |= TRANS_DP_HSYNC_ACTIVE_HIGH;
		if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
			temp |= TRANS_DP_VSYNC_ACTIVE_HIGH;

		switch (intel_trans_dp_port_sel(crtc)) {
		case PORT_B:
			temp |= TRANS_DP_PORT_SEL_B;
			break;
		case PORT_C:
			temp |= TRANS_DP_PORT_SEL_C;
			break;
		case PORT_D:
			temp |= TRANS_DP_PORT_SEL_D;
			break;
		default:
			BUG();
		}

		I915_WRITE(reg, temp);
	}

	ironlake_enable_pch_transcoder(dev_priv, pipe);
}

static void lpt_pch_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;

	assert_pch_transcoder_disabled(dev_priv, TRANSCODER_A);

	lpt_program_iclkip(crtc);

	/* Set transcoder timing. */
	ironlake_pch_transcoder_set_timings(intel_crtc, PIPE_A);

	lpt_enable_pch_transcoder(dev_priv, cpu_transcoder);
}

static void cpt_verify_modeset(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	i915_reg_t dslreg = PIPEDSL(pipe);
	u32 temp;

	temp = I915_READ(dslreg);
	udelay(500);
	if (wait_for(I915_READ(dslreg) != temp, 5)) {
		if (wait_for(I915_READ(dslreg) != temp, 5))
			DRM_ERROR("mode set failed: pipe %c stuck\n", pipe_name(pipe));
	}
}

static int
skl_update_scaler(struct intel_crtc_state *crtc_state, bool force_detach,
		  unsigned scaler_user, int *scaler_id, unsigned int rotation,
		  int src_w, int src_h, int dst_w, int dst_h)
{
	struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	struct intel_crtc *intel_crtc =
		to_intel_crtc(crtc_state->base.crtc);
	int need_scaling;

	need_scaling = drm_rotation_90_or_270(rotation) ?
		(src_h != dst_w || src_w != dst_h):
		(src_w != dst_w || src_h != dst_h);

	/*
	 * if plane is being disabled or scaler is no more required or force detach
	 *  - free scaler binded to this plane/crtc
	 *  - in order to do this, update crtc->scaler_usage
	 *
	 * Here scaler state in crtc_state is set free so that
	 * scaler can be assigned to other user. Actual register
	 * update to free the scaler is done in plane/panel-fit programming.
	 * For this purpose crtc/plane_state->scaler_id isn't reset here.
	 */
	if (force_detach || !need_scaling) {
		if (*scaler_id >= 0) {
			scaler_state->scaler_users &= ~(1 << scaler_user);
			scaler_state->scalers[*scaler_id].in_use = 0;

			DRM_DEBUG_KMS("scaler_user index %u.%u: "
				"Staged freeing scaler id %d scaler_users = 0x%x\n",
				intel_crtc->pipe, scaler_user, *scaler_id,
				scaler_state->scaler_users);
			*scaler_id = -1;
		}
		return 0;
	}

	/* range checks */
	if (src_w < SKL_MIN_SRC_W || src_h < SKL_MIN_SRC_H ||
		dst_w < SKL_MIN_DST_W || dst_h < SKL_MIN_DST_H ||

		src_w > SKL_MAX_SRC_W || src_h > SKL_MAX_SRC_H ||
		dst_w > SKL_MAX_DST_W || dst_h > SKL_MAX_DST_H) {
		DRM_DEBUG_KMS("scaler_user index %u.%u: src %ux%u dst %ux%u "
			"size is out of scaler range\n",
			intel_crtc->pipe, scaler_user, src_w, src_h, dst_w, dst_h);
		return -EINVAL;
	}

	/* mark this plane as a scaler user in crtc_state */
	scaler_state->scaler_users |= (1 << scaler_user);
	DRM_DEBUG_KMS("scaler_user index %u.%u: "
		"staged scaling request for %ux%u->%ux%u scaler_users = 0x%x\n",
		intel_crtc->pipe, scaler_user, src_w, src_h, dst_w, dst_h,
		scaler_state->scaler_users);

	return 0;
}

/**
 * skl_update_scaler_crtc - Stages update to scaler state for a given crtc.
 *
 * @state: crtc's scaler state
 *
 * Return
 *     0 - scaler_usage updated successfully
 *    error - requested scaling cannot be supported or other error condition
 */
int skl_update_scaler_crtc(struct intel_crtc_state *state)
{
	const struct drm_display_mode *adjusted_mode = &state->base.adjusted_mode;

	return skl_update_scaler(state, !state->base.active, SKL_CRTC_INDEX,
		&state->scaler_state.scaler_id, DRM_ROTATE_0,
		state->pipe_src_w, state->pipe_src_h,
		adjusted_mode->crtc_hdisplay, adjusted_mode->crtc_vdisplay);
}

/**
 * skl_update_scaler_plane - Stages update to scaler state for a given plane.
 *
 * @state: crtc's scaler state
 * @plane_state: atomic plane state to update
 *
 * Return
 *     0 - scaler_usage updated successfully
 *    error - requested scaling cannot be supported or other error condition
 */
static int skl_update_scaler_plane(struct intel_crtc_state *crtc_state,
				   struct intel_plane_state *plane_state)
{

	struct intel_plane *intel_plane =
		to_intel_plane(plane_state->base.plane);
	struct drm_framebuffer *fb = plane_state->base.fb;
	int ret;

	bool force_detach = !fb || !plane_state->base.visible;

	ret = skl_update_scaler(crtc_state, force_detach,
				drm_plane_index(&intel_plane->base),
				&plane_state->scaler_id,
				plane_state->base.rotation,
				drm_rect_width(&plane_state->base.src) >> 16,
				drm_rect_height(&plane_state->base.src) >> 16,
				drm_rect_width(&plane_state->base.dst),
				drm_rect_height(&plane_state->base.dst));

	if (ret || plane_state->scaler_id < 0)
		return ret;

	/* check colorkey */
	if (plane_state->ckey.flags != I915_SET_COLORKEY_NONE) {
		DRM_DEBUG_KMS("[PLANE:%d:%s] scaling with color key not allowed",
			      intel_plane->base.base.id,
			      intel_plane->base.name);
		return -EINVAL;
	}

	/* Check src format */
	switch (fb->format->format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		break;
	default:
		DRM_DEBUG_KMS("[PLANE:%d:%s] FB:%d unsupported scaling format 0x%x\n",
			      intel_plane->base.base.id, intel_plane->base.name,
			      fb->base.id, fb->format->format);
		return -EINVAL;
	}

	return 0;
}

static void skylake_scaler_disable(struct intel_crtc *crtc)
{
	int i;

	for (i = 0; i < crtc->num_scalers; i++)
		skl_detach_scaler(crtc, i);
}

static void skylake_pfit_enable(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe = crtc->pipe;
	struct intel_crtc_scaler_state *scaler_state =
		&crtc->config->scaler_state;

	DRM_DEBUG_KMS("for crtc_state = %p\n", crtc->config);

	if (crtc->config->pch_pfit.enabled) {
		int id;

		if (WARN_ON(crtc->config->scaler_state.scaler_id < 0)) {
			DRM_ERROR("Requesting pfit without getting a scaler first\n");
			return;
		}

		id = scaler_state->scaler_id;
		I915_WRITE(SKL_PS_CTRL(pipe, id), PS_SCALER_EN |
			PS_FILTER_MEDIUM | scaler_state->scalers[id].mode);
		I915_WRITE(SKL_PS_WIN_POS(pipe, id), crtc->config->pch_pfit.pos);
		I915_WRITE(SKL_PS_WIN_SZ(pipe, id), crtc->config->pch_pfit.size);

		DRM_DEBUG_KMS("for crtc_state = %p scaler_id = %d\n", crtc->config, id);
	}
}

static void ironlake_pfit_enable(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe = crtc->pipe;

	if (crtc->config->pch_pfit.enabled) {
		/* Force use of hard-coded filter coefficients
		 * as some pre-programmed values are broken,
		 * e.g. x201.
		 */
		if (IS_IVYBRIDGE(dev_priv) || IS_HASWELL(dev_priv))
			I915_WRITE(PF_CTL(pipe), PF_ENABLE | PF_FILTER_MED_3x3 |
						 PF_PIPE_SEL_IVB(pipe));
		else
			I915_WRITE(PF_CTL(pipe), PF_ENABLE | PF_FILTER_MED_3x3);
		I915_WRITE(PF_WIN_POS(pipe), crtc->config->pch_pfit.pos);
		I915_WRITE(PF_WIN_SZ(pipe), crtc->config->pch_pfit.size);
	}
}

void hsw_enable_ips(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (!crtc->config->ips_enabled)
		return;

	/*
	 * We can only enable IPS after we enable a plane and wait for a vblank
	 * This function is called from post_plane_update, which is run after
	 * a vblank wait.
	 */

	assert_plane_enabled(dev_priv, crtc->plane);
	if (IS_BROADWELL(dev_priv)) {
		mutex_lock(&dev_priv->rps.hw_lock);
		WARN_ON(sandybridge_pcode_write(dev_priv, DISPLAY_IPS_CONTROL, 0xc0000000));
		mutex_unlock(&dev_priv->rps.hw_lock);
		/* Quoting Art Runyan: "its not safe to expect any particular
		 * value in IPS_CTL bit 31 after enabling IPS through the
		 * mailbox." Moreover, the mailbox may return a bogus state,
		 * so we need to just enable it and continue on.
		 */
	} else {
		I915_WRITE(IPS_CTL, IPS_ENABLE);
		/* The bit only becomes 1 in the next vblank, so this wait here
		 * is essentially intel_wait_for_vblank. If we don't have this
		 * and don't wait for vblanks until the end of crtc_enable, then
		 * the HW state readout code will complain that the expected
		 * IPS_CTL value is not the one we read. */
		if (intel_wait_for_register(dev_priv,
					    IPS_CTL, IPS_ENABLE, IPS_ENABLE,
					    50))
			DRM_ERROR("Timed out waiting for IPS enable\n");
	}
}

void hsw_disable_ips(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (!crtc->config->ips_enabled)
		return;

	assert_plane_enabled(dev_priv, crtc->plane);
	if (IS_BROADWELL(dev_priv)) {
		mutex_lock(&dev_priv->rps.hw_lock);
		WARN_ON(sandybridge_pcode_write(dev_priv, DISPLAY_IPS_CONTROL, 0));
		mutex_unlock(&dev_priv->rps.hw_lock);
		/* wait for pcode to finish disabling IPS, which may take up to 42ms */
		if (intel_wait_for_register(dev_priv,
					    IPS_CTL, IPS_ENABLE, 0,
					    42))
			DRM_ERROR("Timed out waiting for IPS disable\n");
	} else {
		I915_WRITE(IPS_CTL, 0);
		POSTING_READ(IPS_CTL);
	}

	/* We need to wait for a vblank before we can disable the plane. */
	intel_wait_for_vblank(dev_priv, crtc->pipe);
}

static void intel_crtc_dpms_overlay_disable(struct intel_crtc *intel_crtc)
{
	if (intel_crtc->overlay) {
		struct drm_device *dev = intel_crtc->base.dev;
		struct drm_i915_private *dev_priv = to_i915(dev);

		mutex_lock(&dev->struct_mutex);
		dev_priv->mm.interruptible = false;
		(void) intel_overlay_switch_off(intel_crtc->overlay);
		dev_priv->mm.interruptible = true;
		mutex_unlock(&dev->struct_mutex);
	}

	/* Let userspace switch the overlay on again. In most cases userspace
	 * has to recompute where to put it anyway.
	 */
}

/**
 * intel_post_enable_primary - Perform operations after enabling primary plane
 * @crtc: the CRTC whose primary plane was just enabled
 *
 * Performs potentially sleeping operations that must be done after the primary
 * plane is enabled, such as updating FBC and IPS.  Note that this may be
 * called due to an explicit primary plane update, or due to an implicit
 * re-enable that is caused when a sprite plane is updated to no longer
 * completely hide the primary plane.
 */
static void
intel_post_enable_primary(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;

	/*
	 * FIXME IPS should be fine as long as one plane is
	 * enabled, but in practice it seems to have problems
	 * when going from primary only to sprite only and vice
	 * versa.
	 */
	hsw_enable_ips(intel_crtc);

	/*
	 * Gen2 reports pipe underruns whenever all planes are disabled.
	 * So don't enable underrun reporting before at least some planes
	 * are enabled.
	 * FIXME: Need to fix the logic to work when we turn off all planes
	 * but leave the pipe running.
	 */
	if (IS_GEN2(dev_priv))
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	/* Underruns don't always raise interrupts, so check manually. */
	intel_check_cpu_fifo_underruns(dev_priv);
	intel_check_pch_fifo_underruns(dev_priv);
}

/* FIXME move all this to pre_plane_update() with proper state tracking */
static void
intel_pre_disable_primary(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;

	/*
	 * Gen2 reports pipe underruns whenever all planes are disabled.
	 * So diasble underrun reporting before all the planes get disabled.
	 * FIXME: Need to fix the logic to work when we turn off all planes
	 * but leave the pipe running.
	 */
	if (IS_GEN2(dev_priv))
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);

	/*
	 * FIXME IPS should be fine as long as one plane is
	 * enabled, but in practice it seems to have problems
	 * when going from primary only to sprite only and vice
	 * versa.
	 */
	hsw_disable_ips(intel_crtc);
}

/* FIXME get rid of this and use pre_plane_update */
static void
intel_pre_disable_primary_noatomic(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;

	intel_pre_disable_primary(crtc);

	/*
	 * Vblank time updates from the shadow to live plane control register
	 * are blocked if the memory self-refresh mode is active at that
	 * moment. So to make sure the plane gets truly disabled, disable
	 * first the self-refresh mode. The self-refresh enable bit in turn
	 * will be checked/applied by the HW only at the next frame start
	 * event which is after the vblank start event, so we need to have a
	 * wait-for-vblank between disabling the plane and the pipe.
	 */
	if (HAS_GMCH_DISPLAY(dev_priv) &&
	    intel_set_memory_cxsr(dev_priv, false))
		intel_wait_for_vblank(dev_priv, pipe);
}

static void intel_post_plane_update(struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->base.crtc);
	struct drm_atomic_state *old_state = old_crtc_state->base.state;
	struct intel_crtc_state *pipe_config =
		to_intel_crtc_state(crtc->base.state);
	struct drm_plane *primary = crtc->base.primary;
	struct drm_plane_state *old_pri_state =
		drm_atomic_get_existing_plane_state(old_state, primary);

	intel_frontbuffer_flip(to_i915(crtc->base.dev), pipe_config->fb_bits);

	crtc->wm.cxsr_allowed = true;

	if (pipe_config->update_wm_post && pipe_config->base.active)
		intel_update_watermarks(crtc);

	if (old_pri_state) {
		struct intel_plane_state *primary_state =
			to_intel_plane_state(primary->state);
		struct intel_plane_state *old_primary_state =
			to_intel_plane_state(old_pri_state);

		intel_fbc_post_update(crtc);

		if (primary_state->base.visible &&
		    (needs_modeset(&pipe_config->base) ||
		     !old_primary_state->base.visible))
			intel_post_enable_primary(&crtc->base);
	}
}

static void intel_pre_plane_update(struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->base.crtc);
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc_state *pipe_config =
		to_intel_crtc_state(crtc->base.state);
	struct drm_atomic_state *old_state = old_crtc_state->base.state;
	struct drm_plane *primary = crtc->base.primary;
	struct drm_plane_state *old_pri_state =
		drm_atomic_get_existing_plane_state(old_state, primary);
	bool modeset = needs_modeset(&pipe_config->base);
	struct intel_atomic_state *old_intel_state =
		to_intel_atomic_state(old_state);

	if (old_pri_state) {
		struct intel_plane_state *primary_state =
			to_intel_plane_state(primary->state);
		struct intel_plane_state *old_primary_state =
			to_intel_plane_state(old_pri_state);

		intel_fbc_pre_update(crtc, pipe_config, primary_state);

		if (old_primary_state->base.visible &&
		    (modeset || !primary_state->base.visible))
			intel_pre_disable_primary(&crtc->base);
	}

	if (pipe_config->disable_cxsr && HAS_GMCH_DISPLAY(dev_priv)) {
		crtc->wm.cxsr_allowed = false;

		/*
		 * Vblank time updates from the shadow to live plane control register
		 * are blocked if the memory self-refresh mode is active at that
		 * moment. So to make sure the plane gets truly disabled, disable
		 * first the self-refresh mode. The self-refresh enable bit in turn
		 * will be checked/applied by the HW only at the next frame start
		 * event which is after the vblank start event, so we need to have a
		 * wait-for-vblank between disabling the plane and the pipe.
		 */
		if (old_crtc_state->base.active &&
		    intel_set_memory_cxsr(dev_priv, false))
			intel_wait_for_vblank(dev_priv, crtc->pipe);
	}

	/*
	 * IVB workaround: must disable low power watermarks for at least
	 * one frame before enabling scaling.  LP watermarks can be re-enabled
	 * when scaling is disabled.
	 *
	 * WaCxSRDisabledForSpriteScaling:ivb
	 */
	if (pipe_config->disable_lp_wm && ilk_disable_lp_wm(dev))
		intel_wait_for_vblank(dev_priv, crtc->pipe);

	/*
	 * If we're doing a modeset, we're done.  No need to do any pre-vblank
	 * watermark programming here.
	 */
	if (needs_modeset(&pipe_config->base))
		return;

	/*
	 * For platforms that support atomic watermarks, program the
	 * 'intermediate' watermarks immediately.  On pre-gen9 platforms, these
	 * will be the intermediate values that are safe for both pre- and
	 * post- vblank; when vblank happens, the 'active' values will be set
	 * to the final 'target' values and we'll do this again to get the
	 * optimal watermarks.  For gen9+ platforms, the values we program here
	 * will be the final target values which will get automatically latched
	 * at vblank time; no further programming will be necessary.
	 *
	 * If a platform hasn't been transitioned to atomic watermarks yet,
	 * we'll continue to update watermarks the old way, if flags tell
	 * us to.
	 */
	if (dev_priv->display.initial_watermarks != NULL)
		dev_priv->display.initial_watermarks(old_intel_state,
						     pipe_config);
	else if (pipe_config->update_wm_pre)
		intel_update_watermarks(crtc);
}

static void intel_crtc_disable_planes(struct drm_crtc *crtc, unsigned plane_mask)
{
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_plane *p;
	int pipe = intel_crtc->pipe;

	intel_crtc_dpms_overlay_disable(intel_crtc);

	drm_for_each_plane_mask(p, dev, plane_mask)
		to_intel_plane(p)->disable_plane(p, crtc);

	/*
	 * FIXME: Once we grow proper nuclear flip support out of this we need
	 * to compute the mask of flip planes precisely. For the time being
	 * consider this a flip to a NULL plane.
	 */
	intel_frontbuffer_flip(to_i915(dev), INTEL_FRONTBUFFER_ALL_MASK(pipe));
}

static void intel_encoders_pre_pll_enable(struct drm_crtc *crtc,
					  struct intel_crtc_state *crtc_state,
					  struct drm_atomic_state *old_state)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector *conn;
	int i;

	for_each_connector_in_state(old_state, conn, old_conn_state, i) {
		struct drm_connector_state *conn_state = conn->state;
		struct intel_encoder *encoder =
			to_intel_encoder(conn_state->best_encoder);

		if (conn_state->crtc != crtc)
			continue;

		if (encoder->pre_pll_enable)
			encoder->pre_pll_enable(encoder, crtc_state, conn_state);
	}
}

static void intel_encoders_pre_enable(struct drm_crtc *crtc,
				      struct intel_crtc_state *crtc_state,
				      struct drm_atomic_state *old_state)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector *conn;
	int i;

	for_each_connector_in_state(old_state, conn, old_conn_state, i) {
		struct drm_connector_state *conn_state = conn->state;
		struct intel_encoder *encoder =
			to_intel_encoder(conn_state->best_encoder);

		if (conn_state->crtc != crtc)
			continue;

		if (encoder->pre_enable)
			encoder->pre_enable(encoder, crtc_state, conn_state);
	}
}

static void intel_encoders_enable(struct drm_crtc *crtc,
				  struct intel_crtc_state *crtc_state,
				  struct drm_atomic_state *old_state)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector *conn;
	int i;

	for_each_connector_in_state(old_state, conn, old_conn_state, i) {
		struct drm_connector_state *conn_state = conn->state;
		struct intel_encoder *encoder =
			to_intel_encoder(conn_state->best_encoder);

		if (conn_state->crtc != crtc)
			continue;

		encoder->enable(encoder, crtc_state, conn_state);
		intel_opregion_notify_encoder(encoder, true);
	}
}

static void intel_encoders_disable(struct drm_crtc *crtc,
				   struct intel_crtc_state *old_crtc_state,
				   struct drm_atomic_state *old_state)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector *conn;
	int i;

	for_each_connector_in_state(old_state, conn, old_conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(old_conn_state->best_encoder);

		if (old_conn_state->crtc != crtc)
			continue;

		intel_opregion_notify_encoder(encoder, false);
		encoder->disable(encoder, old_crtc_state, old_conn_state);
	}
}

static void intel_encoders_post_disable(struct drm_crtc *crtc,
					struct intel_crtc_state *old_crtc_state,
					struct drm_atomic_state *old_state)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector *conn;
	int i;

	for_each_connector_in_state(old_state, conn, old_conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(old_conn_state->best_encoder);

		if (old_conn_state->crtc != crtc)
			continue;

		if (encoder->post_disable)
			encoder->post_disable(encoder, old_crtc_state, old_conn_state);
	}
}

static void intel_encoders_post_pll_disable(struct drm_crtc *crtc,
					    struct intel_crtc_state *old_crtc_state,
					    struct drm_atomic_state *old_state)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector *conn;
	int i;

	for_each_connector_in_state(old_state, conn, old_conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(old_conn_state->best_encoder);

		if (old_conn_state->crtc != crtc)
			continue;

		if (encoder->post_pll_disable)
			encoder->post_pll_disable(encoder, old_crtc_state, old_conn_state);
	}
}

static void ironlake_crtc_enable(struct intel_crtc_state *pipe_config,
				 struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc = pipe_config->base.crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	struct intel_atomic_state *old_intel_state =
		to_intel_atomic_state(old_state);

	if (WARN_ON(intel_crtc->active))
		return;

	/*
	 * Sometimes spurious CPU pipe underruns happen during FDI
	 * training, at least with VGA+HDMI cloning. Suppress them.
	 *
	 * On ILK we get an occasional spurious CPU pipe underruns
	 * between eDP port A enable and vdd enable. Also PCH port
	 * enable seems to result in the occasional CPU pipe underrun.
	 *
	 * Spurious PCH underruns also occur during PCH enabling.
	 */
	if (intel_crtc->config->has_pch_encoder || IS_GEN5(dev_priv))
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);
	if (intel_crtc->config->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv, pipe, false);

	if (intel_crtc->config->has_pch_encoder)
		intel_prepare_shared_dpll(intel_crtc);

	if (intel_crtc_has_dp_encoder(intel_crtc->config))
		intel_dp_set_m_n(intel_crtc, M1_N1);

	intel_set_pipe_timings(intel_crtc);
	intel_set_pipe_src_size(intel_crtc);

	if (intel_crtc->config->has_pch_encoder) {
		intel_cpu_transcoder_set_m_n(intel_crtc,
				     &intel_crtc->config->fdi_m_n, NULL);
	}

	ironlake_set_pipeconf(crtc);

	intel_crtc->active = true;

	intel_encoders_pre_enable(crtc, pipe_config, old_state);

	if (intel_crtc->config->has_pch_encoder) {
		/* Note: FDI PLL enabling _must_ be done before we enable the
		 * cpu pipes, hence this is separate from all the other fdi/pch
		 * enabling. */
		ironlake_fdi_pll_enable(intel_crtc);
	} else {
		assert_fdi_tx_disabled(dev_priv, pipe);
		assert_fdi_rx_disabled(dev_priv, pipe);
	}

	ironlake_pfit_enable(intel_crtc);

	/*
	 * On ILK+ LUT must be loaded before the pipe is running but with
	 * clocks enabled
	 */
	intel_color_load_luts(&pipe_config->base);

	if (dev_priv->display.initial_watermarks != NULL)
		dev_priv->display.initial_watermarks(old_intel_state, intel_crtc->config);
	intel_enable_pipe(intel_crtc);

	if (intel_crtc->config->has_pch_encoder)
		ironlake_pch_enable(crtc);

	assert_vblank_disabled(crtc);
	drm_crtc_vblank_on(crtc);

	intel_encoders_enable(crtc, pipe_config, old_state);

	if (HAS_PCH_CPT(dev_priv))
		cpt_verify_modeset(dev, intel_crtc->pipe);

	/* Must wait for vblank to avoid spurious PCH FIFO underruns */
	if (intel_crtc->config->has_pch_encoder)
		intel_wait_for_vblank(dev_priv, pipe);
	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);
	intel_set_pch_fifo_underrun_reporting(dev_priv, pipe, true);
}

/* IPS only exists on ULT machines and is tied to pipe A. */
static bool hsw_crtc_supports_ips(struct intel_crtc *crtc)
{
	return HAS_IPS(to_i915(crtc->base.dev)) && crtc->pipe == PIPE_A;
}

static void haswell_crtc_enable(struct intel_crtc_state *pipe_config,
				struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc = pipe_config->base.crtc;
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe, hsw_workaround_pipe;
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	struct intel_atomic_state *old_intel_state =
		to_intel_atomic_state(old_state);

	if (WARN_ON(intel_crtc->active))
		return;

	if (intel_crtc->config->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv, TRANSCODER_A,
						      false);

	intel_encoders_pre_pll_enable(crtc, pipe_config, old_state);

	if (intel_crtc->config->shared_dpll)
		intel_enable_shared_dpll(intel_crtc);

	if (intel_crtc_has_dp_encoder(intel_crtc->config))
		intel_dp_set_m_n(intel_crtc, M1_N1);

	if (!transcoder_is_dsi(cpu_transcoder))
		intel_set_pipe_timings(intel_crtc);

	intel_set_pipe_src_size(intel_crtc);

	if (cpu_transcoder != TRANSCODER_EDP &&
	    !transcoder_is_dsi(cpu_transcoder)) {
		I915_WRITE(PIPE_MULT(cpu_transcoder),
			   intel_crtc->config->pixel_multiplier - 1);
	}

	if (intel_crtc->config->has_pch_encoder) {
		intel_cpu_transcoder_set_m_n(intel_crtc,
				     &intel_crtc->config->fdi_m_n, NULL);
	}

	if (!transcoder_is_dsi(cpu_transcoder))
		haswell_set_pipeconf(crtc);

	haswell_set_pipemisc(crtc);

	intel_color_set_csc(&pipe_config->base);

	intel_crtc->active = true;

	if (intel_crtc->config->has_pch_encoder)
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);
	else
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	intel_encoders_pre_enable(crtc, pipe_config, old_state);

	if (intel_crtc->config->has_pch_encoder)
		dev_priv->display.fdi_link_train(crtc);

	if (!transcoder_is_dsi(cpu_transcoder))
		intel_ddi_enable_pipe_clock(intel_crtc);

	if (INTEL_GEN(dev_priv) >= 9)
		skylake_pfit_enable(intel_crtc);
	else
		ironlake_pfit_enable(intel_crtc);

	/*
	 * On ILK+ LUT must be loaded before the pipe is running but with
	 * clocks enabled
	 */
	intel_color_load_luts(&pipe_config->base);

	intel_ddi_set_pipe_settings(crtc);
	if (!transcoder_is_dsi(cpu_transcoder))
		intel_ddi_enable_transcoder_func(crtc);

	if (dev_priv->display.initial_watermarks != NULL)
		dev_priv->display.initial_watermarks(old_intel_state, pipe_config);

	/* XXX: Do the pipe assertions at the right place for BXT DSI. */
	if (!transcoder_is_dsi(cpu_transcoder))
		intel_enable_pipe(intel_crtc);

	if (intel_crtc->config->has_pch_encoder)
		lpt_pch_enable(crtc);

	if (intel_crtc_has_type(intel_crtc->config, INTEL_OUTPUT_DP_MST))
		intel_ddi_set_vc_payload_alloc(crtc, true);

	assert_vblank_disabled(crtc);
	drm_crtc_vblank_on(crtc);

	intel_encoders_enable(crtc, pipe_config, old_state);

	if (intel_crtc->config->has_pch_encoder) {
		intel_wait_for_vblank(dev_priv, pipe);
		intel_wait_for_vblank(dev_priv, pipe);
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);
		intel_set_pch_fifo_underrun_reporting(dev_priv, TRANSCODER_A,
						      true);
	}

	/* If we change the relative order between pipe/planes enabling, we need
	 * to change the workaround. */
	hsw_workaround_pipe = pipe_config->hsw_workaround_pipe;
	if (IS_HASWELL(dev_priv) && hsw_workaround_pipe != INVALID_PIPE) {
		intel_wait_for_vblank(dev_priv, hsw_workaround_pipe);
		intel_wait_for_vblank(dev_priv, hsw_workaround_pipe);
	}
}

static void ironlake_pfit_disable(struct intel_crtc *crtc, bool force)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe = crtc->pipe;

	/* To avoid upsetting the power well on haswell only disable the pfit if
	 * it's in use. The hw state code will make sure we get this right. */
	if (force || crtc->config->pch_pfit.enabled) {
		I915_WRITE(PF_CTL(pipe), 0);
		I915_WRITE(PF_WIN_POS(pipe), 0);
		I915_WRITE(PF_WIN_SZ(pipe), 0);
	}
}

static void ironlake_crtc_disable(struct intel_crtc_state *old_crtc_state,
				  struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc = old_crtc_state->base.crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;

	/*
	 * Sometimes spurious CPU pipe underruns happen when the
	 * pipe is already disabled, but FDI RX/TX is still enabled.
	 * Happens at least with VGA+HDMI cloning. Suppress them.
	 */
	if (intel_crtc->config->has_pch_encoder) {
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);
		intel_set_pch_fifo_underrun_reporting(dev_priv, pipe, false);
	}

	intel_encoders_disable(crtc, old_crtc_state, old_state);

	drm_crtc_vblank_off(crtc);
	assert_vblank_disabled(crtc);

	intel_disable_pipe(intel_crtc);

	ironlake_pfit_disable(intel_crtc, false);

	if (intel_crtc->config->has_pch_encoder)
		ironlake_fdi_disable(crtc);

	intel_encoders_post_disable(crtc, old_crtc_state, old_state);

	if (intel_crtc->config->has_pch_encoder) {
		ironlake_disable_pch_transcoder(dev_priv, pipe);

		if (HAS_PCH_CPT(dev_priv)) {
			i915_reg_t reg;
			u32 temp;

			/* disable TRANS_DP_CTL */
			reg = TRANS_DP_CTL(pipe);
			temp = I915_READ(reg);
			temp &= ~(TRANS_DP_OUTPUT_ENABLE |
				  TRANS_DP_PORT_SEL_MASK);
			temp |= TRANS_DP_PORT_SEL_NONE;
			I915_WRITE(reg, temp);

			/* disable DPLL_SEL */
			temp = I915_READ(PCH_DPLL_SEL);
			temp &= ~(TRANS_DPLL_ENABLE(pipe) | TRANS_DPLLB_SEL(pipe));
			I915_WRITE(PCH_DPLL_SEL, temp);
		}

		ironlake_fdi_pll_disable(intel_crtc);
	}

	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);
	intel_set_pch_fifo_underrun_reporting(dev_priv, pipe, true);
}

static void haswell_crtc_disable(struct intel_crtc_state *old_crtc_state,
				 struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc = old_crtc_state->base.crtc;
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;

	if (intel_crtc->config->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv, TRANSCODER_A,
						      false);

	intel_encoders_disable(crtc, old_crtc_state, old_state);

	drm_crtc_vblank_off(crtc);
	assert_vblank_disabled(crtc);

	/* XXX: Do the pipe assertions at the right place for BXT DSI. */
	if (!transcoder_is_dsi(cpu_transcoder))
		intel_disable_pipe(intel_crtc);

	if (intel_crtc_has_type(intel_crtc->config, INTEL_OUTPUT_DP_MST))
		intel_ddi_set_vc_payload_alloc(crtc, false);

	if (!transcoder_is_dsi(cpu_transcoder))
		intel_ddi_disable_transcoder_func(dev_priv, cpu_transcoder);

	if (INTEL_GEN(dev_priv) >= 9)
		skylake_scaler_disable(intel_crtc);
	else
		ironlake_pfit_disable(intel_crtc, false);

	if (!transcoder_is_dsi(cpu_transcoder))
		intel_ddi_disable_pipe_clock(intel_crtc);

	intel_encoders_post_disable(crtc, old_crtc_state, old_state);

	if (old_crtc_state->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv, TRANSCODER_A,
						      true);
}

static void i9xx_pfit_enable(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc_state *pipe_config = crtc->config;

	if (!pipe_config->gmch_pfit.control)
		return;

	/*
	 * The panel fitter should only be adjusted whilst the pipe is disabled,
	 * according to register description and PRM.
	 */
	WARN_ON(I915_READ(PFIT_CONTROL) & PFIT_ENABLE);
	assert_pipe_disabled(dev_priv, crtc->pipe);

	I915_WRITE(PFIT_PGM_RATIOS, pipe_config->gmch_pfit.pgm_ratios);
	I915_WRITE(PFIT_CONTROL, pipe_config->gmch_pfit.control);

	/* Border color in case we don't scale up to the full screen. Black by
	 * default, change to something else for debugging. */
	I915_WRITE(BCLRPAT(crtc->pipe), 0);
}

enum intel_display_power_domain intel_port_to_power_domain(enum port port)
{
	switch (port) {
	case PORT_A:
		return POWER_DOMAIN_PORT_DDI_A_LANES;
	case PORT_B:
		return POWER_DOMAIN_PORT_DDI_B_LANES;
	case PORT_C:
		return POWER_DOMAIN_PORT_DDI_C_LANES;
	case PORT_D:
		return POWER_DOMAIN_PORT_DDI_D_LANES;
	case PORT_E:
		return POWER_DOMAIN_PORT_DDI_E_LANES;
	default:
		MISSING_CASE(port);
		return POWER_DOMAIN_PORT_OTHER;
	}
}

static u64 get_crtc_power_domains(struct drm_crtc *crtc,
				  struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_encoder *encoder;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum pipe pipe = intel_crtc->pipe;
	u64 mask;
	enum transcoder transcoder = crtc_state->cpu_transcoder;

	if (!crtc_state->base.active)
		return 0;

	mask = BIT(POWER_DOMAIN_PIPE(pipe));
	mask |= BIT(POWER_DOMAIN_TRANSCODER(transcoder));
	if (crtc_state->pch_pfit.enabled ||
	    crtc_state->pch_pfit.force_thru)
		mask |= BIT_ULL(POWER_DOMAIN_PIPE_PANEL_FITTER(pipe));

	drm_for_each_encoder_mask(encoder, dev, crtc_state->base.encoder_mask) {
		struct intel_encoder *intel_encoder = to_intel_encoder(encoder);

		mask |= BIT_ULL(intel_encoder->power_domain);
	}

	if (HAS_DDI(dev_priv) && crtc_state->has_audio)
		mask |= BIT(POWER_DOMAIN_AUDIO);

	if (crtc_state->shared_dpll)
		mask |= BIT_ULL(POWER_DOMAIN_PLLS);

	return mask;
}

static u64
modeset_get_crtc_power_domains(struct drm_crtc *crtc,
			       struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum intel_display_power_domain domain;
	u64 domains, new_domains, old_domains;

	old_domains = intel_crtc->enabled_power_domains;
	intel_crtc->enabled_power_domains = new_domains =
		get_crtc_power_domains(crtc, crtc_state);

	domains = new_domains & ~old_domains;

	for_each_power_domain(domain, domains)
		intel_display_power_get(dev_priv, domain);

	return old_domains & ~new_domains;
}

static void modeset_put_power_domains(struct drm_i915_private *dev_priv,
				      u64 domains)
{
	enum intel_display_power_domain domain;

	for_each_power_domain(domain, domains)
		intel_display_power_put(dev_priv, domain);
}

static void valleyview_crtc_enable(struct intel_crtc_state *pipe_config,
				   struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc = pipe_config->base.crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;

	if (WARN_ON(intel_crtc->active))
		return;

	if (intel_crtc_has_dp_encoder(intel_crtc->config))
		intel_dp_set_m_n(intel_crtc, M1_N1);

	intel_set_pipe_timings(intel_crtc);
	intel_set_pipe_src_size(intel_crtc);

	if (IS_CHERRYVIEW(dev_priv) && pipe == PIPE_B) {
		struct drm_i915_private *dev_priv = to_i915(dev);

		I915_WRITE(CHV_BLEND(pipe), CHV_BLEND_LEGACY);
		I915_WRITE(CHV_CANVAS(pipe), 0);
	}

	i9xx_set_pipeconf(intel_crtc);

	intel_crtc->active = true;

	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	intel_encoders_pre_pll_enable(crtc, pipe_config, old_state);

	if (IS_CHERRYVIEW(dev_priv)) {
		chv_prepare_pll(intel_crtc, intel_crtc->config);
		chv_enable_pll(intel_crtc, intel_crtc->config);
	} else {
		vlv_prepare_pll(intel_crtc, intel_crtc->config);
		vlv_enable_pll(intel_crtc, intel_crtc->config);
	}

	intel_encoders_pre_enable(crtc, pipe_config, old_state);

	i9xx_pfit_enable(intel_crtc);

	intel_color_load_luts(&pipe_config->base);

	intel_update_watermarks(intel_crtc);
	intel_enable_pipe(intel_crtc);

	assert_vblank_disabled(crtc);
	drm_crtc_vblank_on(crtc);

	intel_encoders_enable(crtc, pipe_config, old_state);
}

static void i9xx_set_pll_dividers(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	I915_WRITE(FP0(crtc->pipe), crtc->config->dpll_hw_state.fp0);
	I915_WRITE(FP1(crtc->pipe), crtc->config->dpll_hw_state.fp1);
}

static void i9xx_crtc_enable(struct intel_crtc_state *pipe_config,
			     struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc = pipe_config->base.crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum pipe pipe = intel_crtc->pipe;

	if (WARN_ON(intel_crtc->active))
		return;

	i9xx_set_pll_dividers(intel_crtc);

	if (intel_crtc_has_dp_encoder(intel_crtc->config))
		intel_dp_set_m_n(intel_crtc, M1_N1);

	intel_set_pipe_timings(intel_crtc);
	intel_set_pipe_src_size(intel_crtc);

	i9xx_set_pipeconf(intel_crtc);

	intel_crtc->active = true;

	if (!IS_GEN2(dev_priv))
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	intel_encoders_pre_enable(crtc, pipe_config, old_state);

	i9xx_enable_pll(intel_crtc);

	i9xx_pfit_enable(intel_crtc);

	intel_color_load_luts(&pipe_config->base);

	intel_update_watermarks(intel_crtc);
	intel_enable_pipe(intel_crtc);

	assert_vblank_disabled(crtc);
	drm_crtc_vblank_on(crtc);

	intel_encoders_enable(crtc, pipe_config, old_state);
}

static void i9xx_pfit_disable(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (!crtc->config->gmch_pfit.control)
		return;

	assert_pipe_disabled(dev_priv, crtc->pipe);

	DRM_DEBUG_DRIVER("disabling pfit, current: 0x%08x\n",
			 I915_READ(PFIT_CONTROL));
	I915_WRITE(PFIT_CONTROL, 0);
}

static void i9xx_crtc_disable(struct intel_crtc_state *old_crtc_state,
			      struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc = old_crtc_state->base.crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;

	/*
	 * On gen2 planes are double buffered but the pipe isn't, so we must
	 * wait for planes to fully turn off before disabling the pipe.
	 */
	if (IS_GEN2(dev_priv))
		intel_wait_for_vblank(dev_priv, pipe);

	intel_encoders_disable(crtc, old_crtc_state, old_state);

	drm_crtc_vblank_off(crtc);
	assert_vblank_disabled(crtc);

	intel_disable_pipe(intel_crtc);

	i9xx_pfit_disable(intel_crtc);

	intel_encoders_post_disable(crtc, old_crtc_state, old_state);

	if (!intel_crtc_has_type(intel_crtc->config, INTEL_OUTPUT_DSI)) {
		if (IS_CHERRYVIEW(dev_priv))
			chv_disable_pll(dev_priv, pipe);
		else if (IS_VALLEYVIEW(dev_priv))
			vlv_disable_pll(dev_priv, pipe);
		else
			i9xx_disable_pll(intel_crtc);
	}

	intel_encoders_post_pll_disable(crtc, old_crtc_state, old_state);

	if (!IS_GEN2(dev_priv))
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);
}

static void intel_crtc_disable_noatomic(struct drm_crtc *crtc)
{
	struct intel_encoder *encoder;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum intel_display_power_domain domain;
	u64 domains;
	struct drm_atomic_state *state;
	struct intel_crtc_state *crtc_state;
	int ret;

	if (!intel_crtc->active)
		return;

	if (crtc->primary->state->visible) {
		WARN_ON(intel_crtc->flip_work);

		intel_pre_disable_primary_noatomic(crtc);

		intel_crtc_disable_planes(crtc, 1 << drm_plane_index(crtc->primary));
		crtc->primary->state->visible = false;
	}

	state = drm_atomic_state_alloc(crtc->dev);
	if (!state) {
		DRM_DEBUG_KMS("failed to disable [CRTC:%d:%s], out of memory",
			      crtc->base.id, crtc->name);
		return;
	}

	state->acquire_ctx = crtc->dev->mode_config.acquire_ctx;

	/* Everything's already locked, -EDEADLK can't happen. */
	crtc_state = intel_atomic_get_crtc_state(state, intel_crtc);
	ret = drm_atomic_add_affected_connectors(state, crtc);

	WARN_ON(IS_ERR(crtc_state) || ret);

	dev_priv->display.crtc_disable(crtc_state, state);

	drm_atomic_state_put(state);

	DRM_DEBUG_KMS("[CRTC:%d:%s] hw state adjusted, was enabled, now disabled\n",
		      crtc->base.id, crtc->name);

	WARN_ON(drm_atomic_set_mode_for_crtc(crtc->state, NULL) < 0);
	crtc->state->active = false;
	intel_crtc->active = false;
	crtc->enabled = false;
	crtc->state->connector_mask = 0;
	crtc->state->encoder_mask = 0;

	for_each_encoder_on_crtc(crtc->dev, crtc, encoder)
		encoder->base.crtc = NULL;

	intel_fbc_disable(intel_crtc);
	intel_update_watermarks(intel_crtc);
	intel_disable_shared_dpll(intel_crtc);

	domains = intel_crtc->enabled_power_domains;
	for_each_power_domain(domain, domains)
		intel_display_power_put(dev_priv, domain);
	intel_crtc->enabled_power_domains = 0;

	dev_priv->active_crtcs &= ~(1 << intel_crtc->pipe);
	dev_priv->min_pixclk[intel_crtc->pipe] = 0;
}

/*
 * turn all crtc's off, but do not adjust state
 * This has to be paired with a call to intel_modeset_setup_hw_state.
 */
int intel_display_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_atomic_state *state;
	int ret;

	state = drm_atomic_helper_suspend(dev);
	ret = PTR_ERR_OR_ZERO(state);
	if (ret)
		DRM_ERROR("Suspending crtc's failed with %i\n", ret);
	else
		dev_priv->modeset_restore_state = state;
	return ret;
}

void intel_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(encoder);

	drm_encoder_cleanup(encoder);
	kfree(intel_encoder);
}

/* Cross check the actual hw state with our own modeset state tracking (and it's
 * internal consistency). */
static void intel_connector_verify_state(struct intel_connector *connector)
{
	struct drm_crtc *crtc = connector->base.state->crtc;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.base.id,
		      connector->base.name);

	if (connector->get_hw_state(connector)) {
		struct intel_encoder *encoder = connector->encoder;
		struct drm_connector_state *conn_state = connector->base.state;

		I915_STATE_WARN(!crtc,
			 "connector enabled without attached crtc\n");

		if (!crtc)
			return;

		I915_STATE_WARN(!crtc->state->active,
		      "connector is active, but attached crtc isn't\n");

		if (!encoder || encoder->type == INTEL_OUTPUT_DP_MST)
			return;

		I915_STATE_WARN(conn_state->best_encoder != &encoder->base,
			"atomic encoder doesn't match attached encoder\n");

		I915_STATE_WARN(conn_state->crtc != encoder->base.crtc,
			"attached encoder crtc differs from connector crtc\n");
	} else {
		I915_STATE_WARN(crtc && crtc->state->active,
			"attached crtc is active, but connector isn't\n");
		I915_STATE_WARN(!crtc && connector->base.state->best_encoder,
			"best encoder set without crtc!\n");
	}
}

int intel_connector_init(struct intel_connector *connector)
{
	drm_atomic_helper_connector_reset(&connector->base);

	if (!connector->base.state)
		return -ENOMEM;

	return 0;
}

struct intel_connector *intel_connector_alloc(void)
{
	struct intel_connector *connector;

	connector = kzalloc(sizeof *connector, GFP_KERNEL);
	if (!connector)
		return NULL;

	if (intel_connector_init(connector) < 0) {
		kfree(connector);
		return NULL;
	}

	return connector;
}

/* Simple connector->get_hw_state implementation for encoders that support only
 * one connector and no cloning and hence the encoder state determines the state
 * of the connector. */
bool intel_connector_get_hw_state(struct intel_connector *connector)
{
	enum pipe pipe = 0;
	struct intel_encoder *encoder = connector->encoder;

	return encoder->get_hw_state(encoder, &pipe);
}

static int pipe_required_fdi_lanes(struct intel_crtc_state *crtc_state)
{
	if (crtc_state->base.enable && crtc_state->has_pch_encoder)
		return crtc_state->fdi_lanes;

	return 0;
}

static int ironlake_check_fdi_lanes(struct drm_device *dev, enum pipe pipe,
				     struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_atomic_state *state = pipe_config->base.state;
	struct intel_crtc *other_crtc;
	struct intel_crtc_state *other_crtc_state;

	DRM_DEBUG_KMS("checking fdi config on pipe %c, lanes %i\n",
		      pipe_name(pipe), pipe_config->fdi_lanes);
	if (pipe_config->fdi_lanes > 4) {
		DRM_DEBUG_KMS("invalid fdi lane config on pipe %c: %i lanes\n",
			      pipe_name(pipe), pipe_config->fdi_lanes);
		return -EINVAL;
	}

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		if (pipe_config->fdi_lanes > 2) {
			DRM_DEBUG_KMS("only 2 lanes on haswell, required: %i lanes\n",
				      pipe_config->fdi_lanes);
			return -EINVAL;
		} else {
			return 0;
		}
	}

	if (INTEL_INFO(dev_priv)->num_pipes == 2)
		return 0;

	/* Ivybridge 3 pipe is really complicated */
	switch (pipe) {
	case PIPE_A:
		return 0;
	case PIPE_B:
		if (pipe_config->fdi_lanes <= 2)
			return 0;

		other_crtc = intel_get_crtc_for_pipe(dev_priv, PIPE_C);
		other_crtc_state =
			intel_atomic_get_crtc_state(state, other_crtc);
		if (IS_ERR(other_crtc_state))
			return PTR_ERR(other_crtc_state);

		if (pipe_required_fdi_lanes(other_crtc_state) > 0) {
			DRM_DEBUG_KMS("invalid shared fdi lane config on pipe %c: %i lanes\n",
				      pipe_name(pipe), pipe_config->fdi_lanes);
			return -EINVAL;
		}
		return 0;
	case PIPE_C:
		if (pipe_config->fdi_lanes > 2) {
			DRM_DEBUG_KMS("only 2 lanes on pipe %c: required %i lanes\n",
				      pipe_name(pipe), pipe_config->fdi_lanes);
			return -EINVAL;
		}

		other_crtc = intel_get_crtc_for_pipe(dev_priv, PIPE_B);
		other_crtc_state =
			intel_atomic_get_crtc_state(state, other_crtc);
		if (IS_ERR(other_crtc_state))
			return PTR_ERR(other_crtc_state);

		if (pipe_required_fdi_lanes(other_crtc_state) > 2) {
			DRM_DEBUG_KMS("fdi link B uses too many lanes to enable link C\n");
			return -EINVAL;
		}
		return 0;
	default:
		BUG();
	}
}

#define RETRY 1
static int ironlake_fdi_compute_config(struct intel_crtc *intel_crtc,
				       struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = intel_crtc->base.dev;
	const struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	int lane, link_bw, fdi_dotclock, ret;
	bool needs_recompute = false;

retry:
	/* FDI is a binary signal running at ~2.7GHz, encoding
	 * each output octet as 10 bits. The actual frequency
	 * is stored as a divider into a 100MHz clock, and the
	 * mode pixel clock is stored in units of 1KHz.
	 * Hence the bw of each lane in terms of the mode signal
	 * is:
	 */
	link_bw = intel_fdi_link_freq(to_i915(dev), pipe_config);

	fdi_dotclock = adjusted_mode->crtc_clock;

	lane = ironlake_get_lanes_required(fdi_dotclock, link_bw,
					   pipe_config->pipe_bpp);

	pipe_config->fdi_lanes = lane;

	intel_link_compute_m_n(pipe_config->pipe_bpp, lane, fdi_dotclock,
			       link_bw, &pipe_config->fdi_m_n);

	ret = ironlake_check_fdi_lanes(dev, intel_crtc->pipe, pipe_config);
	if (ret == -EINVAL && pipe_config->pipe_bpp > 6*3) {
		pipe_config->pipe_bpp -= 2*3;
		DRM_DEBUG_KMS("fdi link bw constraint, reducing pipe bpp to %i\n",
			      pipe_config->pipe_bpp);
		needs_recompute = true;
		pipe_config->bw_constrained = true;

		goto retry;
	}

	if (needs_recompute)
		return RETRY;

	return ret;
}

static bool pipe_config_supports_ips(struct drm_i915_private *dev_priv,
				     struct intel_crtc_state *pipe_config)
{
	if (pipe_config->pipe_bpp > 24)
		return false;

	/* HSW can handle pixel rate up to cdclk? */
	if (IS_HASWELL(dev_priv))
		return true;

	/*
	 * We compare against max which means we must take
	 * the increased cdclk requirement into account when
	 * calculating the new cdclk.
	 *
	 * Should measure whether using a lower cdclk w/o IPS
	 */
	return pipe_config->pixel_rate <=
		dev_priv->max_cdclk_freq * 95 / 100;
}

static void hsw_compute_ips_config(struct intel_crtc *crtc,
				   struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	pipe_config->ips_enabled = i915.enable_ips &&
		hsw_crtc_supports_ips(crtc) &&
		pipe_config_supports_ips(dev_priv, pipe_config);
}

static bool intel_crtc_supports_double_wide(const struct intel_crtc *crtc)
{
	const struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	/* GDG double wide on either pipe, otherwise pipe A only */
	return INTEL_INFO(dev_priv)->gen < 4 &&
		(crtc->pipe == PIPE_A || IS_I915G(dev_priv));
}

static uint32_t ilk_pipe_pixel_rate(const struct intel_crtc_state *pipe_config)
{
	uint32_t pixel_rate;

	pixel_rate = pipe_config->base.adjusted_mode.crtc_clock;

	/*
	 * We only use IF-ID interlacing. If we ever use
	 * PF-ID we'll need to adjust the pixel_rate here.
	 */

	if (pipe_config->pch_pfit.enabled) {
		uint64_t pipe_w, pipe_h, pfit_w, pfit_h;
		uint32_t pfit_size = pipe_config->pch_pfit.size;

		pipe_w = pipe_config->pipe_src_w;
		pipe_h = pipe_config->pipe_src_h;

		pfit_w = (pfit_size >> 16) & 0xFFFF;
		pfit_h = pfit_size & 0xFFFF;
		if (pipe_w < pfit_w)
			pipe_w = pfit_w;
		if (pipe_h < pfit_h)
			pipe_h = pfit_h;

		if (WARN_ON(!pfit_w || !pfit_h))
			return pixel_rate;

		pixel_rate = div_u64((uint64_t) pixel_rate * pipe_w * pipe_h,
				     pfit_w * pfit_h);
	}

	return pixel_rate;
}

static void intel_crtc_compute_pixel_rate(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);

	if (HAS_GMCH_DISPLAY(dev_priv))
		/* FIXME calculate proper pipe pixel rate for GMCH pfit */
		crtc_state->pixel_rate =
			crtc_state->base.adjusted_mode.crtc_clock;
	else
		crtc_state->pixel_rate =
			ilk_pipe_pixel_rate(crtc_state);
}

static int intel_crtc_compute_config(struct intel_crtc *crtc,
				     struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	const struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	int clock_limit = dev_priv->max_dotclk_freq;

	if (INTEL_GEN(dev_priv) < 4) {
		clock_limit = dev_priv->max_cdclk_freq * 9 / 10;

		/*
		 * Enable double wide mode when the dot clock
		 * is > 90% of the (display) core speed.
		 */
		if (intel_crtc_supports_double_wide(crtc) &&
		    adjusted_mode->crtc_clock > clock_limit) {
			clock_limit = dev_priv->max_dotclk_freq;
			pipe_config->double_wide = true;
		}
	}

	if (adjusted_mode->crtc_clock > clock_limit) {
		DRM_DEBUG_KMS("requested pixel clock (%d kHz) too high (max: %d kHz, double wide: %s)\n",
			      adjusted_mode->crtc_clock, clock_limit,
			      yesno(pipe_config->double_wide));
		return -EINVAL;
	}

	/*
	 * Pipe horizontal size must be even in:
	 * - DVO ganged mode
	 * - LVDS dual channel mode
	 * - Double wide pipe
	 */
	if ((intel_crtc_has_type(pipe_config, INTEL_OUTPUT_LVDS) &&
	     intel_is_dual_link_lvds(dev)) || pipe_config->double_wide)
		pipe_config->pipe_src_w &= ~1;

	/* Cantiga+ cannot handle modes with a hsync front porch of 0.
	 * WaPruneModeWithIncorrectHsyncOffset:ctg,elk,ilk,snb,ivb,vlv,hsw.
	 */
	if ((INTEL_GEN(dev_priv) > 4 || IS_G4X(dev_priv)) &&
		adjusted_mode->crtc_hsync_start == adjusted_mode->crtc_hdisplay)
		return -EINVAL;

	intel_crtc_compute_pixel_rate(pipe_config);

	if (HAS_IPS(dev_priv))
		hsw_compute_ips_config(crtc, pipe_config);

	if (pipe_config->has_pch_encoder)
		return ironlake_fdi_compute_config(crtc, pipe_config);

	return 0;
}

static void
intel_reduce_m_n_ratio(uint32_t *num, uint32_t *den)
{
	while (*num > DATA_LINK_M_N_MASK ||
	       *den > DATA_LINK_M_N_MASK) {
		*num >>= 1;
		*den >>= 1;
	}
}

static void compute_m_n(unsigned int m, unsigned int n,
			uint32_t *ret_m, uint32_t *ret_n)
{
	*ret_n = min_t(unsigned int, roundup_pow_of_two(n), DATA_LINK_N_MAX);
	*ret_m = div_u64((uint64_t) m * *ret_n, n);
	intel_reduce_m_n_ratio(ret_m, ret_n);
}

void
intel_link_compute_m_n(int bits_per_pixel, int nlanes,
		       int pixel_clock, int link_clock,
		       struct intel_link_m_n *m_n)
{
	m_n->tu = 64;

	compute_m_n(bits_per_pixel * pixel_clock,
		    link_clock * nlanes * 8,
		    &m_n->gmch_m, &m_n->gmch_n);

	compute_m_n(pixel_clock, link_clock,
		    &m_n->link_m, &m_n->link_n);
}

static inline bool intel_panel_use_ssc(struct drm_i915_private *dev_priv)
{
	if (i915.panel_use_ssc >= 0)
		return i915.panel_use_ssc != 0;
	return dev_priv->vbt.lvds_use_ssc
		&& !(dev_priv->quirks & QUIRK_LVDS_SSC_DISABLE);
}

static uint32_t pnv_dpll_compute_fp(struct dpll *dpll)
{
	return (1 << dpll->n) << 16 | dpll->m2;
}

static uint32_t i9xx_dpll_compute_fp(struct dpll *dpll)
{
	return dpll->n << 16 | dpll->m1 << 8 | dpll->m2;
}

static void i9xx_update_pll_dividers(struct intel_crtc *crtc,
				     struct intel_crtc_state *crtc_state,
				     struct dpll *reduced_clock)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 fp, fp2 = 0;

	if (IS_PINEVIEW(dev_priv)) {
		fp = pnv_dpll_compute_fp(&crtc_state->dpll);
		if (reduced_clock)
			fp2 = pnv_dpll_compute_fp(reduced_clock);
	} else {
		fp = i9xx_dpll_compute_fp(&crtc_state->dpll);
		if (reduced_clock)
			fp2 = i9xx_dpll_compute_fp(reduced_clock);
	}

	crtc_state->dpll_hw_state.fp0 = fp;

	crtc->lowfreq_avail = false;
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS) &&
	    reduced_clock) {
		crtc_state->dpll_hw_state.fp1 = fp2;
		crtc->lowfreq_avail = true;
	} else {
		crtc_state->dpll_hw_state.fp1 = fp;
	}
}

static void vlv_pllb_recal_opamp(struct drm_i915_private *dev_priv, enum pipe
		pipe)
{
	u32 reg_val;

	/*
	 * PLLB opamp always calibrates to max value of 0x3f, force enable it
	 * and set it to a reasonable value instead.
	 */
	reg_val = vlv_dpio_read(dev_priv, pipe, VLV_PLL_DW9(1));
	reg_val &= 0xffffff00;
	reg_val |= 0x00000030;
	vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW9(1), reg_val);

	reg_val = vlv_dpio_read(dev_priv, pipe, VLV_REF_DW13);
	reg_val &= 0x8cffffff;
	reg_val = 0x8c000000;
	vlv_dpio_write(dev_priv, pipe, VLV_REF_DW13, reg_val);

	reg_val = vlv_dpio_read(dev_priv, pipe, VLV_PLL_DW9(1));
	reg_val &= 0xffffff00;
	vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW9(1), reg_val);

	reg_val = vlv_dpio_read(dev_priv, pipe, VLV_REF_DW13);
	reg_val &= 0x00ffffff;
	reg_val |= 0xb0000000;
	vlv_dpio_write(dev_priv, pipe, VLV_REF_DW13, reg_val);
}

static void intel_pch_transcoder_set_m_n(struct intel_crtc *crtc,
					 struct intel_link_m_n *m_n)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe = crtc->pipe;

	I915_WRITE(PCH_TRANS_DATA_M1(pipe), TU_SIZE(m_n->tu) | m_n->gmch_m);
	I915_WRITE(PCH_TRANS_DATA_N1(pipe), m_n->gmch_n);
	I915_WRITE(PCH_TRANS_LINK_M1(pipe), m_n->link_m);
	I915_WRITE(PCH_TRANS_LINK_N1(pipe), m_n->link_n);
}

static void intel_cpu_transcoder_set_m_n(struct intel_crtc *crtc,
					 struct intel_link_m_n *m_n,
					 struct intel_link_m_n *m2_n2)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	int pipe = crtc->pipe;
	enum transcoder transcoder = crtc->config->cpu_transcoder;

	if (INTEL_GEN(dev_priv) >= 5) {
		I915_WRITE(PIPE_DATA_M1(transcoder), TU_SIZE(m_n->tu) | m_n->gmch_m);
		I915_WRITE(PIPE_DATA_N1(transcoder), m_n->gmch_n);
		I915_WRITE(PIPE_LINK_M1(transcoder), m_n->link_m);
		I915_WRITE(PIPE_LINK_N1(transcoder), m_n->link_n);
		/* M2_N2 registers to be set only for gen < 8 (M2_N2 available
		 * for gen < 8) and if DRRS is supported (to make sure the
		 * registers are not unnecessarily accessed).
		 */
		if (m2_n2 && (IS_CHERRYVIEW(dev_priv) ||
		    INTEL_GEN(dev_priv) < 8) && crtc->config->has_drrs) {
			I915_WRITE(PIPE_DATA_M2(transcoder),
					TU_SIZE(m2_n2->tu) | m2_n2->gmch_m);
			I915_WRITE(PIPE_DATA_N2(transcoder), m2_n2->gmch_n);
			I915_WRITE(PIPE_LINK_M2(transcoder), m2_n2->link_m);
			I915_WRITE(PIPE_LINK_N2(transcoder), m2_n2->link_n);
		}
	} else {
		I915_WRITE(PIPE_DATA_M_G4X(pipe), TU_SIZE(m_n->tu) | m_n->gmch_m);
		I915_WRITE(PIPE_DATA_N_G4X(pipe), m_n->gmch_n);
		I915_WRITE(PIPE_LINK_M_G4X(pipe), m_n->link_m);
		I915_WRITE(PIPE_LINK_N_G4X(pipe), m_n->link_n);
	}
}

void intel_dp_set_m_n(struct intel_crtc *crtc, enum link_m_n_set m_n)
{
	struct intel_link_m_n *dp_m_n, *dp_m2_n2 = NULL;

	if (m_n == M1_N1) {
		dp_m_n = &crtc->config->dp_m_n;
		dp_m2_n2 = &crtc->config->dp_m2_n2;
	} else if (m_n == M2_N2) {

		/*
		 * M2_N2 registers are not supported. Hence m2_n2 divider value
		 * needs to be programmed into M1_N1.
		 */
		dp_m_n = &crtc->config->dp_m2_n2;
	} else {
		DRM_ERROR("Unsupported divider value\n");
		return;
	}

	if (crtc->config->has_pch_encoder)
		intel_pch_transcoder_set_m_n(crtc, &crtc->config->dp_m_n);
	else
		intel_cpu_transcoder_set_m_n(crtc, dp_m_n, dp_m2_n2);
}

static void vlv_compute_dpll(struct intel_crtc *crtc,
			     struct intel_crtc_state *pipe_config)
{
	pipe_config->dpll_hw_state.dpll = DPLL_INTEGRATED_REF_CLK_VLV |
		DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS;
	if (crtc->pipe != PIPE_A)
		pipe_config->dpll_hw_state.dpll |= DPLL_INTEGRATED_CRI_CLK_VLV;

	/* DPLL not used with DSI, but still need the rest set up */
	if (!intel_crtc_has_type(pipe_config, INTEL_OUTPUT_DSI))
		pipe_config->dpll_hw_state.dpll |= DPLL_VCO_ENABLE |
			DPLL_EXT_BUFFER_ENABLE_VLV;

	pipe_config->dpll_hw_state.dpll_md =
		(pipe_config->pixel_multiplier - 1) << DPLL_MD_UDI_MULTIPLIER_SHIFT;
}

static void chv_compute_dpll(struct intel_crtc *crtc,
			     struct intel_crtc_state *pipe_config)
{
	pipe_config->dpll_hw_state.dpll = DPLL_SSC_REF_CLK_CHV |
		DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS;
	if (crtc->pipe != PIPE_A)
		pipe_config->dpll_hw_state.dpll |= DPLL_INTEGRATED_CRI_CLK_VLV;

	/* DPLL not used with DSI, but still need the rest set up */
	if (!intel_crtc_has_type(pipe_config, INTEL_OUTPUT_DSI))
		pipe_config->dpll_hw_state.dpll |= DPLL_VCO_ENABLE;

	pipe_config->dpll_hw_state.dpll_md =
		(pipe_config->pixel_multiplier - 1) << DPLL_MD_UDI_MULTIPLIER_SHIFT;
}

static void vlv_prepare_pll(struct intel_crtc *crtc,
			    const struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = crtc->pipe;
	u32 mdiv;
	u32 bestn, bestm1, bestm2, bestp1, bestp2;
	u32 coreclk, reg_val;

	/* Enable Refclk */
	I915_WRITE(DPLL(pipe),
		   pipe_config->dpll_hw_state.dpll &
		   ~(DPLL_VCO_ENABLE | DPLL_EXT_BUFFER_ENABLE_VLV));

	/* No need to actually set up the DPLL with DSI */
	if ((pipe_config->dpll_hw_state.dpll & DPLL_VCO_ENABLE) == 0)
		return;

	mutex_lock(&dev_priv->sb_lock);

	bestn = pipe_config->dpll.n;
	bestm1 = pipe_config->dpll.m1;
	bestm2 = pipe_config->dpll.m2;
	bestp1 = pipe_config->dpll.p1;
	bestp2 = pipe_config->dpll.p2;

	/* See eDP HDMI DPIO driver vbios notes doc */

	/* PLL B needs special handling */
	if (pipe == PIPE_B)
		vlv_pllb_recal_opamp(dev_priv, pipe);

	/* Set up Tx target for periodic Rcomp update */
	vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW9_BCAST, 0x0100000f);

	/* Disable target IRef on PLL */
	reg_val = vlv_dpio_read(dev_priv, pipe, VLV_PLL_DW8(pipe));
	reg_val &= 0x00ffffff;
	vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW8(pipe), reg_val);

	/* Disable fast lock */
	vlv_dpio_write(dev_priv, pipe, VLV_CMN_DW0, 0x610);

	/* Set idtafcrecal before PLL is enabled */
	mdiv = ((bestm1 << DPIO_M1DIV_SHIFT) | (bestm2 & DPIO_M2DIV_MASK));
	mdiv |= ((bestp1 << DPIO_P1_SHIFT) | (bestp2 << DPIO_P2_SHIFT));
	mdiv |= ((bestn << DPIO_N_SHIFT));
	mdiv |= (1 << DPIO_K_SHIFT);

	/*
	 * Post divider depends on pixel clock rate, DAC vs digital (and LVDS,
	 * but we don't support that).
	 * Note: don't use the DAC post divider as it seems unstable.
	 */
	mdiv |= (DPIO_POST_DIV_HDMIDP << DPIO_POST_DIV_SHIFT);
	vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW3(pipe), mdiv);

	mdiv |= DPIO_ENABLE_CALIBRATION;
	vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW3(pipe), mdiv);

	/* Set HBR and RBR LPF coefficients */
	if (pipe_config->port_clock == 162000 ||
	    intel_crtc_has_type(crtc->config, INTEL_OUTPUT_ANALOG) ||
	    intel_crtc_has_type(crtc->config, INTEL_OUTPUT_HDMI))
		vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW10(pipe),
				 0x009f0003);
	else
		vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW10(pipe),
				 0x00d0000f);

	if (intel_crtc_has_dp_encoder(pipe_config)) {
		/* Use SSC source */
		if (pipe == PIPE_A)
			vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW5(pipe),
					 0x0df40000);
		else
			vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW5(pipe),
					 0x0df70000);
	} else { /* HDMI or VGA */
		/* Use bend source */
		if (pipe == PIPE_A)
			vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW5(pipe),
					 0x0df70000);
		else
			vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW5(pipe),
					 0x0df40000);
	}

	coreclk = vlv_dpio_read(dev_priv, pipe, VLV_PLL_DW7(pipe));
	coreclk = (coreclk & 0x0000ff00) | 0x01c00000;
	if (intel_crtc_has_dp_encoder(crtc->config))
		coreclk |= 0x01000000;
	vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW7(pipe), coreclk);

	vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW11(pipe), 0x87871000);
	mutex_unlock(&dev_priv->sb_lock);
}

static void chv_prepare_pll(struct intel_crtc *crtc,
			    const struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = crtc->pipe;
	enum dpio_channel port = vlv_pipe_to_channel(pipe);
	u32 loopfilter, tribuf_calcntr;
	u32 bestn, bestm1, bestm2, bestp1, bestp2, bestm2_frac;
	u32 dpio_val;
	int vco;

	/* Enable Refclk and SSC */
	I915_WRITE(DPLL(pipe),
		   pipe_config->dpll_hw_state.dpll & ~DPLL_VCO_ENABLE);

	/* No need to actually set up the DPLL with DSI */
	if ((pipe_config->dpll_hw_state.dpll & DPLL_VCO_ENABLE) == 0)
		return;

	bestn = pipe_config->dpll.n;
	bestm2_frac = pipe_config->dpll.m2 & 0x3fffff;
	bestm1 = pipe_config->dpll.m1;
	bestm2 = pipe_config->dpll.m2 >> 22;
	bestp1 = pipe_config->dpll.p1;
	bestp2 = pipe_config->dpll.p2;
	vco = pipe_config->dpll.vco;
	dpio_val = 0;
	loopfilter = 0;

	mutex_lock(&dev_priv->sb_lock);

	/* p1 and p2 divider */
	vlv_dpio_write(dev_priv, pipe, CHV_CMN_DW13(port),
			5 << DPIO_CHV_S1_DIV_SHIFT |
			bestp1 << DPIO_CHV_P1_DIV_SHIFT |
			bestp2 << DPIO_CHV_P2_DIV_SHIFT |
			1 << DPIO_CHV_K_DIV_SHIFT);

	/* Feedback post-divider - m2 */
	vlv_dpio_write(dev_priv, pipe, CHV_PLL_DW0(port), bestm2);

	/* Feedback refclk divider - n and m1 */
	vlv_dpio_write(dev_priv, pipe, CHV_PLL_DW1(port),
			DPIO_CHV_M1_DIV_BY_2 |
			1 << DPIO_CHV_N_DIV_SHIFT);

	/* M2 fraction division */
	vlv_dpio_write(dev_priv, pipe, CHV_PLL_DW2(port), bestm2_frac);

	/* M2 fraction division enable */
	dpio_val = vlv_dpio_read(dev_priv, pipe, CHV_PLL_DW3(port));
	dpio_val &= ~(DPIO_CHV_FEEDFWD_GAIN_MASK | DPIO_CHV_FRAC_DIV_EN);
	dpio_val |= (2 << DPIO_CHV_FEEDFWD_GAIN_SHIFT);
	if (bestm2_frac)
		dpio_val |= DPIO_CHV_FRAC_DIV_EN;
	vlv_dpio_write(dev_priv, pipe, CHV_PLL_DW3(port), dpio_val);

	/* Program digital lock detect threshold */
	dpio_val = vlv_dpio_read(dev_priv, pipe, CHV_PLL_DW9(port));
	dpio_val &= ~(DPIO_CHV_INT_LOCK_THRESHOLD_MASK |
					DPIO_CHV_INT_LOCK_THRESHOLD_SEL_COARSE);
	dpio_val |= (0x5 << DPIO_CHV_INT_LOCK_THRESHOLD_SHIFT);
	if (!bestm2_frac)
		dpio_val |= DPIO_CHV_INT_LOCK_THRESHOLD_SEL_COARSE;
	vlv_dpio_write(dev_priv, pipe, CHV_PLL_DW9(port), dpio_val);

	/* Loop filter */
	if (vco == 5400000) {
		loopfilter |= (0x3 << DPIO_CHV_PROP_COEFF_SHIFT);
		loopfilter |= (0x8 << DPIO_CHV_INT_COEFF_SHIFT);
		loopfilter |= (0x1 << DPIO_CHV_GAIN_CTRL_SHIFT);
		tribuf_calcntr = 0x9;
	} else if (vco <= 6200000) {
		loopfilter |= (0x5 << DPIO_CHV_PROP_COEFF_SHIFT);
		loopfilter |= (0xB << DPIO_CHV_INT_COEFF_SHIFT);
		loopfilter |= (0x3 << DPIO_CHV_GAIN_CTRL_SHIFT);
		tribuf_calcntr = 0x9;
	} else if (vco <= 6480000) {
		loopfilter |= (0x4 << DPIO_CHV_PROP_COEFF_SHIFT);
		loopfilter |= (0x9 << DPIO_CHV_INT_COEFF_SHIFT);
		loopfilter |= (0x3 << DPIO_CHV_GAIN_CTRL_SHIFT);
		tribuf_calcntr = 0x8;
	} else {
		/* Not supported. Apply the same limits as in the max case */
		loopfilter |= (0x4 << DPIO_CHV_PROP_COEFF_SHIFT);
		loopfilter |= (0x9 << DPIO_CHV_INT_COEFF_SHIFT);
		loopfilter |= (0x3 << DPIO_CHV_GAIN_CTRL_SHIFT);
		tribuf_calcntr = 0;
	}
	vlv_dpio_write(dev_priv, pipe, CHV_PLL_DW6(port), loopfilter);

	dpio_val = vlv_dpio_read(dev_priv, pipe, CHV_PLL_DW8(port));
	dpio_val &= ~DPIO_CHV_TDC_TARGET_CNT_MASK;
	dpio_val |= (tribuf_calcntr << DPIO_CHV_TDC_TARGET_CNT_SHIFT);
	vlv_dpio_write(dev_priv, pipe, CHV_PLL_DW8(port), dpio_val);

	/* AFC Recal */
	vlv_dpio_write(dev_priv, pipe, CHV_CMN_DW14(port),
			vlv_dpio_read(dev_priv, pipe, CHV_CMN_DW14(port)) |
			DPIO_AFC_RECAL);

	mutex_unlock(&dev_priv->sb_lock);
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
	struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv, pipe);
	struct intel_crtc_state *pipe_config;

	pipe_config = kzalloc(sizeof(*pipe_config), GFP_KERNEL);
	if (!pipe_config)
		return -ENOMEM;

	pipe_config->base.crtc = &crtc->base;
	pipe_config->pixel_multiplier = 1;
	pipe_config->dpll = *dpll;

	if (IS_CHERRYVIEW(dev_priv)) {
		chv_compute_dpll(crtc, pipe_config);
		chv_prepare_pll(crtc, pipe_config);
		chv_enable_pll(crtc, pipe_config);
	} else {
		vlv_compute_dpll(crtc, pipe_config);
		vlv_prepare_pll(crtc, pipe_config);
		vlv_enable_pll(crtc, pipe_config);
	}

	kfree(pipe_config);

	return 0;
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

static void i9xx_compute_dpll(struct intel_crtc *crtc,
			      struct intel_crtc_state *crtc_state,
			      struct dpll *reduced_clock)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 dpll;
	struct dpll *clock = &crtc_state->dpll;

	i9xx_update_pll_dividers(crtc, crtc_state, reduced_clock);

	dpll = DPLL_VGA_MODE_DIS;

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
	if (IS_PINEVIEW(dev_priv))
		dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT_PINEVIEW;
	else {
		dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
		if (IS_G4X(dev_priv) && reduced_clock)
			dpll |= (1 << (reduced_clock->p1 - 1)) << DPLL_FPA1_P1_POST_DIV_SHIFT;
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
	if (INTEL_GEN(dev_priv) >= 4)
		dpll |= (6 << PLL_LOAD_PULSE_PHASE_SHIFT);

	if (crtc_state->sdvo_tv_clock)
		dpll |= PLL_REF_INPUT_TVCLKINBC;
	else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS) &&
		 intel_panel_use_ssc(dev_priv))
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	dpll |= DPLL_VCO_ENABLE;
	crtc_state->dpll_hw_state.dpll = dpll;

	if (INTEL_GEN(dev_priv) >= 4) {
		u32 dpll_md = (crtc_state->pixel_multiplier - 1)
			<< DPLL_MD_UDI_MULTIPLIER_SHIFT;
		crtc_state->dpll_hw_state.dpll_md = dpll_md;
	}
}

static void i8xx_compute_dpll(struct intel_crtc *crtc,
			      struct intel_crtc_state *crtc_state,
			      struct dpll *reduced_clock)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 dpll;
	struct dpll *clock = &crtc_state->dpll;

	i9xx_update_pll_dividers(crtc, crtc_state, reduced_clock);

	dpll = DPLL_VGA_MODE_DIS;

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

	if (!IS_I830(dev_priv) &&
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DVO))
		dpll |= DPLL_DVO_2X_MODE;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS) &&
	    intel_panel_use_ssc(dev_priv))
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	dpll |= DPLL_VCO_ENABLE;
	crtc_state->dpll_hw_state.dpll = dpll;
}

static void intel_set_pipe_timings(struct intel_crtc *intel_crtc)
{
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);
	enum pipe pipe = intel_crtc->pipe;
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	const struct drm_display_mode *adjusted_mode = &intel_crtc->config->base.adjusted_mode;
	uint32_t crtc_vtotal, crtc_vblank_end;
	int vsyncshift = 0;

	/* We need to be careful not to changed the adjusted mode, for otherwise
	 * the hw state checker will get angry at the mismatch. */
	crtc_vtotal = adjusted_mode->crtc_vtotal;
	crtc_vblank_end = adjusted_mode->crtc_vblank_end;

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		/* the chip adds 2 halflines automatically */
		crtc_vtotal -= 1;
		crtc_vblank_end -= 1;

		if (intel_crtc_has_type(intel_crtc->config, INTEL_OUTPUT_SDVO))
			vsyncshift = (adjusted_mode->crtc_htotal - 1) / 2;
		else
			vsyncshift = adjusted_mode->crtc_hsync_start -
				adjusted_mode->crtc_htotal / 2;
		if (vsyncshift < 0)
			vsyncshift += adjusted_mode->crtc_htotal;
	}

	if (INTEL_GEN(dev_priv) > 3)
		I915_WRITE(VSYNCSHIFT(cpu_transcoder), vsyncshift);

	I915_WRITE(HTOTAL(cpu_transcoder),
		   (adjusted_mode->crtc_hdisplay - 1) |
		   ((adjusted_mode->crtc_htotal - 1) << 16));
	I915_WRITE(HBLANK(cpu_transcoder),
		   (adjusted_mode->crtc_hblank_start - 1) |
		   ((adjusted_mode->crtc_hblank_end - 1) << 16));
	I915_WRITE(HSYNC(cpu_transcoder),
		   (adjusted_mode->crtc_hsync_start - 1) |
		   ((adjusted_mode->crtc_hsync_end - 1) << 16));

	I915_WRITE(VTOTAL(cpu_transcoder),
		   (adjusted_mode->crtc_vdisplay - 1) |
		   ((crtc_vtotal - 1) << 16));
	I915_WRITE(VBLANK(cpu_transcoder),
		   (adjusted_mode->crtc_vblank_start - 1) |
		   ((crtc_vblank_end - 1) << 16));
	I915_WRITE(VSYNC(cpu_transcoder),
		   (adjusted_mode->crtc_vsync_start - 1) |
		   ((adjusted_mode->crtc_vsync_end - 1) << 16));

	/* Workaround: when the EDP input selection is B, the VTOTAL_B must be
	 * programmed with the VTOTAL_EDP value. Same for VTOTAL_C. This is
	 * documented on the DDI_FUNC_CTL register description, EDP Input Select
	 * bits. */
	if (IS_HASWELL(dev_priv) && cpu_transcoder == TRANSCODER_EDP &&
	    (pipe == PIPE_B || pipe == PIPE_C))
		I915_WRITE(VTOTAL(pipe), I915_READ(VTOTAL(cpu_transcoder)));

}

static void intel_set_pipe_src_size(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = intel_crtc->pipe;

	/* pipesrc controls the size that is scaled from, which should
	 * always be the user's requested size.
	 */
	I915_WRITE(PIPESRC(pipe),
		   ((intel_crtc->config->pipe_src_w - 1) << 16) |
		   (intel_crtc->config->pipe_src_h - 1));
}

static void intel_get_pipe_timings(struct intel_crtc *crtc,
				   struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum transcoder cpu_transcoder = pipe_config->cpu_transcoder;
	uint32_t tmp;

	tmp = I915_READ(HTOTAL(cpu_transcoder));
	pipe_config->base.adjusted_mode.crtc_hdisplay = (tmp & 0xffff) + 1;
	pipe_config->base.adjusted_mode.crtc_htotal = ((tmp >> 16) & 0xffff) + 1;
	tmp = I915_READ(HBLANK(cpu_transcoder));
	pipe_config->base.adjusted_mode.crtc_hblank_start = (tmp & 0xffff) + 1;
	pipe_config->base.adjusted_mode.crtc_hblank_end = ((tmp >> 16) & 0xffff) + 1;
	tmp = I915_READ(HSYNC(cpu_transcoder));
	pipe_config->base.adjusted_mode.crtc_hsync_start = (tmp & 0xffff) + 1;
	pipe_config->base.adjusted_mode.crtc_hsync_end = ((tmp >> 16) & 0xffff) + 1;

	tmp = I915_READ(VTOTAL(cpu_transcoder));
	pipe_config->base.adjusted_mode.crtc_vdisplay = (tmp & 0xffff) + 1;
	pipe_config->base.adjusted_mode.crtc_vtotal = ((tmp >> 16) & 0xffff) + 1;
	tmp = I915_READ(VBLANK(cpu_transcoder));
	pipe_config->base.adjusted_mode.crtc_vblank_start = (tmp & 0xffff) + 1;
	pipe_config->base.adjusted_mode.crtc_vblank_end = ((tmp >> 16) & 0xffff) + 1;
	tmp = I915_READ(VSYNC(cpu_transcoder));
	pipe_config->base.adjusted_mode.crtc_vsync_start = (tmp & 0xffff) + 1;
	pipe_config->base.adjusted_mode.crtc_vsync_end = ((tmp >> 16) & 0xffff) + 1;

	if (I915_READ(PIPECONF(cpu_transcoder)) & PIPECONF_INTERLACE_MASK) {
		pipe_config->base.adjusted_mode.flags |= DRM_MODE_FLAG_INTERLACE;
		pipe_config->base.adjusted_mode.crtc_vtotal += 1;
		pipe_config->base.adjusted_mode.crtc_vblank_end += 1;
	}
}

static void intel_get_pipe_src_size(struct intel_crtc *crtc,
				    struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 tmp;

	tmp = I915_READ(PIPESRC(crtc->pipe));
	pipe_config->pipe_src_h = (tmp & 0xffff) + 1;
	pipe_config->pipe_src_w = ((tmp >> 16) & 0xffff) + 1;

	pipe_config->base.mode.vdisplay = pipe_config->pipe_src_h;
	pipe_config->base.mode.hdisplay = pipe_config->pipe_src_w;
}

void intel_mode_from_pipe_config(struct drm_display_mode *mode,
				 struct intel_crtc_state *pipe_config)
{
	mode->hdisplay = pipe_config->base.adjusted_mode.crtc_hdisplay;
	mode->htotal = pipe_config->base.adjusted_mode.crtc_htotal;
	mode->hsync_start = pipe_config->base.adjusted_mode.crtc_hsync_start;
	mode->hsync_end = pipe_config->base.adjusted_mode.crtc_hsync_end;

	mode->vdisplay = pipe_config->base.adjusted_mode.crtc_vdisplay;
	mode->vtotal = pipe_config->base.adjusted_mode.crtc_vtotal;
	mode->vsync_start = pipe_config->base.adjusted_mode.crtc_vsync_start;
	mode->vsync_end = pipe_config->base.adjusted_mode.crtc_vsync_end;

	mode->flags = pipe_config->base.adjusted_mode.flags;
	mode->type = DRM_MODE_TYPE_DRIVER;

	mode->clock = pipe_config->base.adjusted_mode.crtc_clock;

	mode->hsync = drm_mode_hsync(mode);
	mode->vrefresh = drm_mode_vrefresh(mode);
	drm_mode_set_name(mode);
}

static void i9xx_set_pipeconf(struct intel_crtc *intel_crtc)
{
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);
	uint32_t pipeconf;

	pipeconf = 0;

	if ((intel_crtc->pipe == PIPE_A && dev_priv->quirks & QUIRK_PIPEA_FORCE) ||
	    (intel_crtc->pipe == PIPE_B && dev_priv->quirks & QUIRK_PIPEB_FORCE))
		pipeconf |= I915_READ(PIPECONF(intel_crtc->pipe)) & PIPECONF_ENABLE;

	if (intel_crtc->config->double_wide)
		pipeconf |= PIPECONF_DOUBLE_WIDE;

	/* only g4x and later have fancy bpc/dither controls */
	if (IS_G4X(dev_priv) || IS_VALLEYVIEW(dev_priv) ||
	    IS_CHERRYVIEW(dev_priv)) {
		/* Bspec claims that we can't use dithering for 30bpp pipes. */
		if (intel_crtc->config->dither && intel_crtc->config->pipe_bpp != 30)
			pipeconf |= PIPECONF_DITHER_EN |
				    PIPECONF_DITHER_TYPE_SP;

		switch (intel_crtc->config->pipe_bpp) {
		case 18:
			pipeconf |= PIPECONF_6BPC;
			break;
		case 24:
			pipeconf |= PIPECONF_8BPC;
			break;
		case 30:
			pipeconf |= PIPECONF_10BPC;
			break;
		default:
			/* Case prevented by intel_choose_pipe_bpp_dither. */
			BUG();
		}
	}

	if (HAS_PIPE_CXSR(dev_priv)) {
		if (intel_crtc->lowfreq_avail) {
			DRM_DEBUG_KMS("enabling CxSR downclocking\n");
			pipeconf |= PIPECONF_CXSR_DOWNCLOCK;
		} else {
			DRM_DEBUG_KMS("disabling CxSR downclocking\n");
		}
	}

	if (intel_crtc->config->base.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE) {
		if (INTEL_GEN(dev_priv) < 4 ||
		    intel_crtc_has_type(intel_crtc->config, INTEL_OUTPUT_SDVO))
			pipeconf |= PIPECONF_INTERLACE_W_FIELD_INDICATION;
		else
			pipeconf |= PIPECONF_INTERLACE_W_SYNC_SHIFT;
	} else
		pipeconf |= PIPECONF_PROGRESSIVE;

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	     intel_crtc->config->limited_color_range)
		pipeconf |= PIPECONF_COLOR_RANGE_SELECT;

	I915_WRITE(PIPECONF(intel_crtc->pipe), pipeconf);
	POSTING_READ(PIPECONF(intel_crtc->pipe));
}

static int i8xx_crtc_compute_clock(struct intel_crtc *crtc,
				   struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	const struct intel_limit *limit;
	int refclk = 48000;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if (intel_panel_use_ssc(dev_priv)) {
			refclk = dev_priv->vbt.lvds_ssc_freq;
			DRM_DEBUG_KMS("using SSC reference clock of %d kHz\n", refclk);
		}

		limit = &intel_limits_i8xx_lvds;
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DVO)) {
		limit = &intel_limits_i8xx_dvo;
	} else {
		limit = &intel_limits_i8xx_dac;
	}

	if (!crtc_state->clock_set &&
	    !i9xx_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				 refclk, NULL, &crtc_state->dpll)) {
		DRM_ERROR("Couldn't find PLL settings for mode!\n");
		return -EINVAL;
	}

	i8xx_compute_dpll(crtc, crtc_state, NULL);

	return 0;
}

static int g4x_crtc_compute_clock(struct intel_crtc *crtc,
				  struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	const struct intel_limit *limit;
	int refclk = 96000;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if (intel_panel_use_ssc(dev_priv)) {
			refclk = dev_priv->vbt.lvds_ssc_freq;
			DRM_DEBUG_KMS("using SSC reference clock of %d kHz\n", refclk);
		}

		if (intel_is_dual_link_lvds(dev))
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
				refclk, NULL, &crtc_state->dpll)) {
		DRM_ERROR("Couldn't find PLL settings for mode!\n");
		return -EINVAL;
	}

	i9xx_compute_dpll(crtc, crtc_state, NULL);

	return 0;
}

static int pnv_crtc_compute_clock(struct intel_crtc *crtc,
				  struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	const struct intel_limit *limit;
	int refclk = 96000;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if (intel_panel_use_ssc(dev_priv)) {
			refclk = dev_priv->vbt.lvds_ssc_freq;
			DRM_DEBUG_KMS("using SSC reference clock of %d kHz\n", refclk);
		}

		limit = &intel_limits_pineview_lvds;
	} else {
		limit = &intel_limits_pineview_sdvo;
	}

	if (!crtc_state->clock_set &&
	    !pnv_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				refclk, NULL, &crtc_state->dpll)) {
		DRM_ERROR("Couldn't find PLL settings for mode!\n");
		return -EINVAL;
	}

	i9xx_compute_dpll(crtc, crtc_state, NULL);

	return 0;
}

static int i9xx_crtc_compute_clock(struct intel_crtc *crtc,
				   struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	const struct intel_limit *limit;
	int refclk = 96000;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if (intel_panel_use_ssc(dev_priv)) {
			refclk = dev_priv->vbt.lvds_ssc_freq;
			DRM_DEBUG_KMS("using SSC reference clock of %d kHz\n", refclk);
		}

		limit = &intel_limits_i9xx_lvds;
	} else {
		limit = &intel_limits_i9xx_sdvo;
	}

	if (!crtc_state->clock_set &&
	    !i9xx_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				 refclk, NULL, &crtc_state->dpll)) {
		DRM_ERROR("Couldn't find PLL settings for mode!\n");
		return -EINVAL;
	}

	i9xx_compute_dpll(crtc, crtc_state, NULL);

	return 0;
}

static int chv_crtc_compute_clock(struct intel_crtc *crtc,
				  struct intel_crtc_state *crtc_state)
{
	int refclk = 100000;
	const struct intel_limit *limit = &intel_limits_chv;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	if (!crtc_state->clock_set &&
	    !chv_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				refclk, NULL, &crtc_state->dpll)) {
		DRM_ERROR("Couldn't find PLL settings for mode!\n");
		return -EINVAL;
	}

	chv_compute_dpll(crtc, crtc_state);

	return 0;
}

static int vlv_crtc_compute_clock(struct intel_crtc *crtc,
				  struct intel_crtc_state *crtc_state)
{
	int refclk = 100000;
	const struct intel_limit *limit = &intel_limits_vlv;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	if (!crtc_state->clock_set &&
	    !vlv_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				refclk, NULL, &crtc_state->dpll)) {
		DRM_ERROR("Couldn't find PLL settings for mode!\n");
		return -EINVAL;
	}

	vlv_compute_dpll(crtc, crtc_state);

	return 0;
}

static void i9xx_get_pfit_config(struct intel_crtc *crtc,
				 struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	uint32_t tmp;

	if (INTEL_GEN(dev_priv) <= 3 &&
	    (IS_I830(dev_priv) || !IS_MOBILE(dev_priv)))
		return;

	tmp = I915_READ(PFIT_CONTROL);
	if (!(tmp & PFIT_ENABLE))
		return;

	/* Check whether the pfit is attached to our pipe. */
	if (INTEL_GEN(dev_priv) < 4) {
		if (crtc->pipe != PIPE_B)
			return;
	} else {
		if ((tmp & PFIT_PIPE_MASK) != (crtc->pipe << PFIT_PIPE_SHIFT))
			return;
	}

	pipe_config->gmch_pfit.control = tmp;
	pipe_config->gmch_pfit.pgm_ratios = I915_READ(PFIT_PGM_RATIOS);
}

static void vlv_crtc_clock_get(struct intel_crtc *crtc,
			       struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe = pipe_config->cpu_transcoder;
	struct dpll clock;
	u32 mdiv;
	int refclk = 100000;

	/* In case of DSI, DPLL will not be used */
	if ((pipe_config->dpll_hw_state.dpll & DPLL_VCO_ENABLE) == 0)
		return;

	mutex_lock(&dev_priv->sb_lock);
	mdiv = vlv_dpio_read(dev_priv, pipe, VLV_PLL_DW3(pipe));
	mutex_unlock(&dev_priv->sb_lock);

	clock.m1 = (mdiv >> DPIO_M1DIV_SHIFT) & 7;
	clock.m2 = mdiv & DPIO_M2DIV_MASK;
	clock.n = (mdiv >> DPIO_N_SHIFT) & 0xf;
	clock.p1 = (mdiv >> DPIO_P1_SHIFT) & 7;
	clock.p2 = (mdiv >> DPIO_P2_SHIFT) & 0x1f;

	pipe_config->port_clock = vlv_calc_dpll_params(refclk, &clock);
}

static void
i9xx_get_initial_plane_config(struct intel_crtc *crtc,
			      struct intel_initial_plane_config *plane_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 val, base, offset;
	int pipe = crtc->pipe, plane = crtc->plane;
	int fourcc, pixel_format;
	unsigned int aligned_height;
	struct drm_framebuffer *fb;
	struct intel_framebuffer *intel_fb;

	val = I915_READ(DSPCNTR(plane));
	if (!(val & DISPLAY_PLANE_ENABLE))
		return;

	intel_fb = kzalloc(sizeof(*intel_fb), GFP_KERNEL);
	if (!intel_fb) {
		DRM_DEBUG_KMS("failed to alloc fb\n");
		return;
	}

	fb = &intel_fb->base;

	fb->dev = dev;

	if (INTEL_GEN(dev_priv) >= 4) {
		if (val & DISPPLANE_TILED) {
			plane_config->tiling = I915_TILING_X;
			fb->modifier = I915_FORMAT_MOD_X_TILED;
		}
	}

	pixel_format = val & DISPPLANE_PIXFORMAT_MASK;
	fourcc = i9xx_format_to_fourcc(pixel_format);
	fb->format = drm_format_info(fourcc);

	if (INTEL_GEN(dev_priv) >= 4) {
		if (plane_config->tiling)
			offset = I915_READ(DSPTILEOFF(plane));
		else
			offset = I915_READ(DSPLINOFF(plane));
		base = I915_READ(DSPSURF(plane)) & 0xfffff000;
	} else {
		base = I915_READ(DSPADDR(plane));
	}
	plane_config->base = base;

	val = I915_READ(PIPESRC(pipe));
	fb->width = ((val >> 16) & 0xfff) + 1;
	fb->height = ((val >> 0) & 0xfff) + 1;

	val = I915_READ(DSPSTRIDE(pipe));
	fb->pitches[0] = val & 0xffffffc0;

	aligned_height = intel_fb_align_height(dev_priv,
					       fb->height,
					       fb->format->format,
					       fb->modifier);

	plane_config->size = fb->pitches[0] * aligned_height;

	DRM_DEBUG_KMS("pipe/plane %c/%d with fb: size=%dx%d@%d, offset=%x, pitch %d, size 0x%x\n",
		      pipe_name(pipe), plane, fb->width, fb->height,
		      fb->format->cpp[0] * 8, base, fb->pitches[0],
		      plane_config->size);

	plane_config->fb = intel_fb;
}

static void chv_crtc_clock_get(struct intel_crtc *crtc,
			       struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe = pipe_config->cpu_transcoder;
	enum dpio_channel port = vlv_pipe_to_channel(pipe);
	struct dpll clock;
	u32 cmn_dw13, pll_dw0, pll_dw1, pll_dw2, pll_dw3;
	int refclk = 100000;

	/* In case of DSI, DPLL will not be used */
	if ((pipe_config->dpll_hw_state.dpll & DPLL_VCO_ENABLE) == 0)
		return;

	mutex_lock(&dev_priv->sb_lock);
	cmn_dw13 = vlv_dpio_read(dev_priv, pipe, CHV_CMN_DW13(port));
	pll_dw0 = vlv_dpio_read(dev_priv, pipe, CHV_PLL_DW0(port));
	pll_dw1 = vlv_dpio_read(dev_priv, pipe, CHV_PLL_DW1(port));
	pll_dw2 = vlv_dpio_read(dev_priv, pipe, CHV_PLL_DW2(port));
	pll_dw3 = vlv_dpio_read(dev_priv, pipe, CHV_PLL_DW3(port));
	mutex_unlock(&dev_priv->sb_lock);

	clock.m1 = (pll_dw1 & 0x7) == DPIO_CHV_M1_DIV_BY_2 ? 2 : 0;
	clock.m2 = (pll_dw0 & 0xff) << 22;
	if (pll_dw3 & DPIO_CHV_FRAC_DIV_EN)
		clock.m2 |= pll_dw2 & 0x3fffff;
	clock.n = (pll_dw1 >> DPIO_CHV_N_DIV_SHIFT) & 0xf;
	clock.p1 = (cmn_dw13 >> DPIO_CHV_P1_DIV_SHIFT) & 0x7;
	clock.p2 = (cmn_dw13 >> DPIO_CHV_P2_DIV_SHIFT) & 0x1f;

	pipe_config->port_clock = chv_calc_dpll_params(refclk, &clock);
}

static bool i9xx_get_pipe_config(struct intel_crtc *crtc,
				 struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum intel_display_power_domain power_domain;
	uint32_t tmp;
	bool ret;

	power_domain = POWER_DOMAIN_PIPE(crtc->pipe);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;

	pipe_config->cpu_transcoder = (enum transcoder) crtc->pipe;
	pipe_config->shared_dpll = NULL;

	ret = false;

	tmp = I915_READ(PIPECONF(crtc->pipe));
	if (!(tmp & PIPECONF_ENABLE))
		goto out;

	if (IS_G4X(dev_priv) || IS_VALLEYVIEW(dev_priv) ||
	    IS_CHERRYVIEW(dev_priv)) {
		switch (tmp & PIPECONF_BPC_MASK) {
		case PIPECONF_6BPC:
			pipe_config->pipe_bpp = 18;
			break;
		case PIPECONF_8BPC:
			pipe_config->pipe_bpp = 24;
			break;
		case PIPECONF_10BPC:
			pipe_config->pipe_bpp = 30;
			break;
		default:
			break;
		}
	}

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    (tmp & PIPECONF_COLOR_RANGE_SELECT))
		pipe_config->limited_color_range = true;

	if (INTEL_GEN(dev_priv) < 4)
		pipe_config->double_wide = tmp & PIPECONF_DOUBLE_WIDE;

	intel_get_pipe_timings(crtc, pipe_config);
	intel_get_pipe_src_size(crtc, pipe_config);

	i9xx_get_pfit_config(crtc, pipe_config);

	if (INTEL_GEN(dev_priv) >= 4) {
		/* No way to read it out on pipes B and C */
		if (IS_CHERRYVIEW(dev_priv) && crtc->pipe != PIPE_A)
			tmp = dev_priv->chv_dpll_md[crtc->pipe];
		else
			tmp = I915_READ(DPLL_MD(crtc->pipe));
		pipe_config->pixel_multiplier =
			((tmp & DPLL_MD_UDI_MULTIPLIER_MASK)
			 >> DPLL_MD_UDI_MULTIPLIER_SHIFT) + 1;
		pipe_config->dpll_hw_state.dpll_md = tmp;
	} else if (IS_I945G(dev_priv) || IS_I945GM(dev_priv) ||
		   IS_G33(dev_priv) || IS_PINEVIEW(dev_priv)) {
		tmp = I915_READ(DPLL(crtc->pipe));
		pipe_config->pixel_multiplier =
			((tmp & SDVO_MULTIPLIER_MASK)
			 >> SDVO_MULTIPLIER_SHIFT_HIRES) + 1;
	} else {
		/* Note that on i915G/GM the pixel multiplier is in the sdvo
		 * port and will be fixed up in the encoder->get_config
		 * function. */
		pipe_config->pixel_multiplier = 1;
	}
	pipe_config->dpll_hw_state.dpll = I915_READ(DPLL(crtc->pipe));
	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv)) {
		/*
		 * DPLL_DVO_2X_MODE must be enabled for both DPLLs
		 * on 830. Filter it out here so that we don't
		 * report errors due to that.
		 */
		if (IS_I830(dev_priv))
			pipe_config->dpll_hw_state.dpll &= ~DPLL_DVO_2X_MODE;

		pipe_config->dpll_hw_state.fp0 = I915_READ(FP0(crtc->pipe));
		pipe_config->dpll_hw_state.fp1 = I915_READ(FP1(crtc->pipe));
	} else {
		/* Mask out read-only status bits. */
		pipe_config->dpll_hw_state.dpll &= ~(DPLL_LOCK_VLV |
						     DPLL_PORTC_READY_MASK |
						     DPLL_PORTB_READY_MASK);
	}

	if (IS_CHERRYVIEW(dev_priv))
		chv_crtc_clock_get(crtc, pipe_config);
	else if (IS_VALLEYVIEW(dev_priv))
		vlv_crtc_clock_get(crtc, pipe_config);
	else
		i9xx_crtc_clock_get(crtc, pipe_config);

	/*
	 * Normally the dotclock is filled in by the encoder .get_config()
	 * but in case the pipe is enabled w/o any ports we need a sane
	 * default.
	 */
	pipe_config->base.adjusted_mode.crtc_clock =
		pipe_config->port_clock / pipe_config->pixel_multiplier;

	ret = true;

out:
	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

static void ironlake_init_pch_refclk(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;
	int i;
	u32 val, final;
	bool has_lvds = false;
	bool has_cpu_edp = false;
	bool has_panel = false;
	bool has_ck505 = false;
	bool can_ssc = false;
	bool using_ssc_source = false;

	/* We need to take the global config into account */
	for_each_intel_encoder(&dev_priv->drm, encoder) {
		switch (encoder->type) {
		case INTEL_OUTPUT_LVDS:
			has_panel = true;
			has_lvds = true;
			break;
		case INTEL_OUTPUT_EDP:
			has_panel = true;
			if (enc_to_dig_port(&encoder->base)->port == PORT_A)
				has_cpu_edp = true;
			break;
		default:
			break;
		}
	}

	if (HAS_PCH_IBX(dev_priv)) {
		has_ck505 = dev_priv->vbt.display_clock_mode;
		can_ssc = has_ck505;
	} else {
		has_ck505 = false;
		can_ssc = true;
	}

	/* Check if any DPLLs are using the SSC source */
	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		u32 temp = I915_READ(PCH_DPLL(i));

		if (!(temp & DPLL_VCO_ENABLE))
			continue;

		if ((temp & PLL_REF_INPUT_MASK) ==
		    PLLB_REF_INPUT_SPREADSPECTRUMIN) {
			using_ssc_source = true;
			break;
		}
	}

	DRM_DEBUG_KMS("has_panel %d has_lvds %d has_ck505 %d using_ssc_source %d\n",
		      has_panel, has_lvds, has_ck505, using_ssc_source);

	/* Ironlake: try to setup display ref clock before DPLL
	 * enabling. This is only under driver's control after
	 * PCH B stepping, previous chipset stepping should be
	 * ignoring this setting.
	 */
	val = I915_READ(PCH_DREF_CONTROL);

	/* As we must carefully and slowly disable/enable each source in turn,
	 * compute the final state we want first and check if we need to
	 * make any changes at all.
	 */
	final = val;
	final &= ~DREF_NONSPREAD_SOURCE_MASK;
	if (has_ck505)
		final |= DREF_NONSPREAD_CK505_ENABLE;
	else
		final |= DREF_NONSPREAD_SOURCE_ENABLE;

	final &= ~DREF_SSC_SOURCE_MASK;
	final &= ~DREF_CPU_SOURCE_OUTPUT_MASK;
	final &= ~DREF_SSC1_ENABLE;

	if (has_panel) {
		final |= DREF_SSC_SOURCE_ENABLE;

		if (intel_panel_use_ssc(dev_priv) && can_ssc)
			final |= DREF_SSC1_ENABLE;

		if (has_cpu_edp) {
			if (intel_panel_use_ssc(dev_priv) && can_ssc)
				final |= DREF_CPU_SOURCE_OUTPUT_DOWNSPREAD;
			else
				final |= DREF_CPU_SOURCE_OUTPUT_NONSPREAD;
		} else
			final |= DREF_CPU_SOURCE_OUTPUT_DISABLE;
	} else if (using_ssc_source) {
		final |= DREF_SSC_SOURCE_ENABLE;
		final |= DREF_SSC1_ENABLE;
	}

	if (final == val)
		return;

	/* Always enable nonspread source */
	val &= ~DREF_NONSPREAD_SOURCE_MASK;

	if (has_ck505)
		val |= DREF_NONSPREAD_CK505_ENABLE;
	else
		val |= DREF_NONSPREAD_SOURCE_ENABLE;

	if (has_panel) {
		val &= ~DREF_SSC_SOURCE_MASK;
		val |= DREF_SSC_SOURCE_ENABLE;

		/* SSC must be turned on before enabling the CPU output  */
		if (intel_panel_use_ssc(dev_priv) && can_ssc) {
			DRM_DEBUG_KMS("Using SSC on panel\n");
			val |= DREF_SSC1_ENABLE;
		} else
			val &= ~DREF_SSC1_ENABLE;

		/* Get SSC going before enabling the outputs */
		I915_WRITE(PCH_DREF_CONTROL, val);
		POSTING_READ(PCH_DREF_CONTROL);
		udelay(200);

		val &= ~DREF_CPU_SOURCE_OUTPUT_MASK;

		/* Enable CPU source on CPU attached eDP */
		if (has_cpu_edp) {
			if (intel_panel_use_ssc(dev_priv) && can_ssc) {
				DRM_DEBUG_KMS("Using SSC on eDP\n");
				val |= DREF_CPU_SOURCE_OUTPUT_DOWNSPREAD;
			} else
				val |= DREF_CPU_SOURCE_OUTPUT_NONSPREAD;
		} else
			val |= DREF_CPU_SOURCE_OUTPUT_DISABLE;

		I915_WRITE(PCH_DREF_CONTROL, val);
		POSTING_READ(PCH_DREF_CONTROL);
		udelay(200);
	} else {
		DRM_DEBUG_KMS("Disabling CPU source output\n");

		val &= ~DREF_CPU_SOURCE_OUTPUT_MASK;

		/* Turn off CPU output */
		val |= DREF_CPU_SOURCE_OUTPUT_DISABLE;

		I915_WRITE(PCH_DREF_CONTROL, val);
		POSTING_READ(PCH_DREF_CONTROL);
		udelay(200);

		if (!using_ssc_source) {
			DRM_DEBUG_KMS("Disabling SSC source\n");

			/* Turn off the SSC source */
			val &= ~DREF_SSC_SOURCE_MASK;
			val |= DREF_SSC_SOURCE_DISABLE;

			/* Turn off SSC1 */
			val &= ~DREF_SSC1_ENABLE;

			I915_WRITE(PCH_DREF_CONTROL, val);
			POSTING_READ(PCH_DREF_CONTROL);
			udelay(200);
		}
	}

	BUG_ON(val != final);
}

static void lpt_reset_fdi_mphy(struct drm_i915_private *dev_priv)
{
	uint32_t tmp;

	tmp = I915_READ(SOUTH_CHICKEN2);
	tmp |= FDI_MPHY_IOSFSB_RESET_CTL;
	I915_WRITE(SOUTH_CHICKEN2, tmp);

	if (wait_for_us(I915_READ(SOUTH_CHICKEN2) &
			FDI_MPHY_IOSFSB_RESET_STATUS, 100))
		DRM_ERROR("FDI mPHY reset assert timeout\n");

	tmp = I915_READ(SOUTH_CHICKEN2);
	tmp &= ~FDI_MPHY_IOSFSB_RESET_CTL;
	I915_WRITE(SOUTH_CHICKEN2, tmp);

	if (wait_for_us((I915_READ(SOUTH_CHICKEN2) &
			 FDI_MPHY_IOSFSB_RESET_STATUS) == 0, 100))
		DRM_ERROR("FDI mPHY reset de-assert timeout\n");
}

/* WaMPhyProgramming:hsw */
static void lpt_program_fdi_mphy(struct drm_i915_private *dev_priv)
{
	uint32_t tmp;

	tmp = intel_sbi_read(dev_priv, 0x8008, SBI_MPHY);
	tmp &= ~(0xFF << 24);
	tmp |= (0x12 << 24);
	intel_sbi_write(dev_priv, 0x8008, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2008, SBI_MPHY);
	tmp |= (1 << 11);
	intel_sbi_write(dev_priv, 0x2008, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2108, SBI_MPHY);
	tmp |= (1 << 11);
	intel_sbi_write(dev_priv, 0x2108, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x206C, SBI_MPHY);
	tmp |= (1 << 24) | (1 << 21) | (1 << 18);
	intel_sbi_write(dev_priv, 0x206C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x216C, SBI_MPHY);
	tmp |= (1 << 24) | (1 << 21) | (1 << 18);
	intel_sbi_write(dev_priv, 0x216C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2080, SBI_MPHY);
	tmp &= ~(7 << 13);
	tmp |= (5 << 13);
	intel_sbi_write(dev_priv, 0x2080, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2180, SBI_MPHY);
	tmp &= ~(7 << 13);
	tmp |= (5 << 13);
	intel_sbi_write(dev_priv, 0x2180, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x208C, SBI_MPHY);
	tmp &= ~0xFF;
	tmp |= 0x1C;
	intel_sbi_write(dev_priv, 0x208C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x218C, SBI_MPHY);
	tmp &= ~0xFF;
	tmp |= 0x1C;
	intel_sbi_write(dev_priv, 0x218C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2098, SBI_MPHY);
	tmp &= ~(0xFF << 16);
	tmp |= (0x1C << 16);
	intel_sbi_write(dev_priv, 0x2098, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2198, SBI_MPHY);
	tmp &= ~(0xFF << 16);
	tmp |= (0x1C << 16);
	intel_sbi_write(dev_priv, 0x2198, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x20C4, SBI_MPHY);
	tmp |= (1 << 27);
	intel_sbi_write(dev_priv, 0x20C4, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x21C4, SBI_MPHY);
	tmp |= (1 << 27);
	intel_sbi_write(dev_priv, 0x21C4, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x20EC, SBI_MPHY);
	tmp &= ~(0xF << 28);
	tmp |= (4 << 28);
	intel_sbi_write(dev_priv, 0x20EC, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x21EC, SBI_MPHY);
	tmp &= ~(0xF << 28);
	tmp |= (4 << 28);
	intel_sbi_write(dev_priv, 0x21EC, tmp, SBI_MPHY);
}

/* Implements 3 different sequences from BSpec chapter "Display iCLK
 * Programming" based on the parameters passed:
 * - Sequence to enable CLKOUT_DP
 * - Sequence to enable CLKOUT_DP without spread
 * - Sequence to enable CLKOUT_DP for FDI usage and configure PCH FDI I/O
 */
static void lpt_enable_clkout_dp(struct drm_i915_private *dev_priv,
				 bool with_spread, bool with_fdi)
{
	uint32_t reg, tmp;

	if (WARN(with_fdi && !with_spread, "FDI requires downspread\n"))
		with_spread = true;
	if (WARN(HAS_PCH_LPT_LP(dev_priv) &&
	    with_fdi, "LP PCH doesn't have FDI\n"))
		with_fdi = false;

	mutex_lock(&dev_priv->sb_lock);

	tmp = intel_sbi_read(dev_priv, SBI_SSCCTL, SBI_ICLK);
	tmp &= ~SBI_SSCCTL_DISABLE;
	tmp |= SBI_SSCCTL_PATHALT;
	intel_sbi_write(dev_priv, SBI_SSCCTL, tmp, SBI_ICLK);

	udelay(24);

	if (with_spread) {
		tmp = intel_sbi_read(dev_priv, SBI_SSCCTL, SBI_ICLK);
		tmp &= ~SBI_SSCCTL_PATHALT;
		intel_sbi_write(dev_priv, SBI_SSCCTL, tmp, SBI_ICLK);

		if (with_fdi) {
			lpt_reset_fdi_mphy(dev_priv);
			lpt_program_fdi_mphy(dev_priv);
		}
	}

	reg = HAS_PCH_LPT_LP(dev_priv) ? SBI_GEN0 : SBI_DBUFF0;
	tmp = intel_sbi_read(dev_priv, reg, SBI_ICLK);
	tmp |= SBI_GEN0_CFG_BUFFENABLE_DISABLE;
	intel_sbi_write(dev_priv, reg, tmp, SBI_ICLK);

	mutex_unlock(&dev_priv->sb_lock);
}

/* Sequence to disable CLKOUT_DP */
static void lpt_disable_clkout_dp(struct drm_i915_private *dev_priv)
{
	uint32_t reg, tmp;

	mutex_lock(&dev_priv->sb_lock);

	reg = HAS_PCH_LPT_LP(dev_priv) ? SBI_GEN0 : SBI_DBUFF0;
	tmp = intel_sbi_read(dev_priv, reg, SBI_ICLK);
	tmp &= ~SBI_GEN0_CFG_BUFFENABLE_DISABLE;
	intel_sbi_write(dev_priv, reg, tmp, SBI_ICLK);

	tmp = intel_sbi_read(dev_priv, SBI_SSCCTL, SBI_ICLK);
	if (!(tmp & SBI_SSCCTL_DISABLE)) {
		if (!(tmp & SBI_SSCCTL_PATHALT)) {
			tmp |= SBI_SSCCTL_PATHALT;
			intel_sbi_write(dev_priv, SBI_SSCCTL, tmp, SBI_ICLK);
			udelay(32);
		}
		tmp |= SBI_SSCCTL_DISABLE;
		intel_sbi_write(dev_priv, SBI_SSCCTL, tmp, SBI_ICLK);
	}

	mutex_unlock(&dev_priv->sb_lock);
}

#define BEND_IDX(steps) ((50 + (steps)) / 5)

static const uint16_t sscdivintphase[] = {
	[BEND_IDX( 50)] = 0x3B23,
	[BEND_IDX( 45)] = 0x3B23,
	[BEND_IDX( 40)] = 0x3C23,
	[BEND_IDX( 35)] = 0x3C23,
	[BEND_IDX( 30)] = 0x3D23,
	[BEND_IDX( 25)] = 0x3D23,
	[BEND_IDX( 20)] = 0x3E23,
	[BEND_IDX( 15)] = 0x3E23,
	[BEND_IDX( 10)] = 0x3F23,
	[BEND_IDX(  5)] = 0x3F23,
	[BEND_IDX(  0)] = 0x0025,
	[BEND_IDX( -5)] = 0x0025,
	[BEND_IDX(-10)] = 0x0125,
	[BEND_IDX(-15)] = 0x0125,
	[BEND_IDX(-20)] = 0x0225,
	[BEND_IDX(-25)] = 0x0225,
	[BEND_IDX(-30)] = 0x0325,
	[BEND_IDX(-35)] = 0x0325,
	[BEND_IDX(-40)] = 0x0425,
	[BEND_IDX(-45)] = 0x0425,
	[BEND_IDX(-50)] = 0x0525,
};

/*
 * Bend CLKOUT_DP
 * steps -50 to 50 inclusive, in steps of 5
 * < 0 slow down the clock, > 0 speed up the clock, 0 == no bend (135MHz)
 * change in clock period = -(steps / 10) * 5.787 ps
 */
static void lpt_bend_clkout_dp(struct drm_i915_private *dev_priv, int steps)
{
	uint32_t tmp;
	int idx = BEND_IDX(steps);

	if (WARN_ON(steps % 5 != 0))
		return;

	if (WARN_ON(idx >= ARRAY_SIZE(sscdivintphase)))
		return;

	mutex_lock(&dev_priv->sb_lock);

	if (steps % 10 != 0)
		tmp = 0xAAAAAAAB;
	else
		tmp = 0x00000000;
	intel_sbi_write(dev_priv, SBI_SSCDITHPHASE, tmp, SBI_ICLK);

	tmp = intel_sbi_read(dev_priv, SBI_SSCDIVINTPHASE, SBI_ICLK);
	tmp &= 0xffff0000;
	tmp |= sscdivintphase[idx];
	intel_sbi_write(dev_priv, SBI_SSCDIVINTPHASE, tmp, SBI_ICLK);

	mutex_unlock(&dev_priv->sb_lock);
}

#undef BEND_IDX

static void lpt_init_pch_refclk(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;
	bool has_vga = false;

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		switch (encoder->type) {
		case INTEL_OUTPUT_ANALOG:
			has_vga = true;
			break;
		default:
			break;
		}
	}

	if (has_vga) {
		lpt_bend_clkout_dp(dev_priv, 0);
		lpt_enable_clkout_dp(dev_priv, true, true);
	} else {
		lpt_disable_clkout_dp(dev_priv);
	}
}

/*
 * Initialize reference clocks when the driver loads
 */
void intel_init_pch_refclk(struct drm_i915_private *dev_priv)
{
	if (HAS_PCH_IBX(dev_priv) || HAS_PCH_CPT(dev_priv))
		ironlake_init_pch_refclk(dev_priv);
	else if (HAS_PCH_LPT(dev_priv))
		lpt_init_pch_refclk(dev_priv);
}

static void ironlake_set_pipeconf(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	uint32_t val;

	val = 0;

	switch (intel_crtc->config->pipe_bpp) {
	case 18:
		val |= PIPECONF_6BPC;
		break;
	case 24:
		val |= PIPECONF_8BPC;
		break;
	case 30:
		val |= PIPECONF_10BPC;
		break;
	case 36:
		val |= PIPECONF_12BPC;
		break;
	default:
		/* Case prevented by intel_choose_pipe_bpp_dither. */
		BUG();
	}

	if (intel_crtc->config->dither)
		val |= (PIPECONF_DITHER_EN | PIPECONF_DITHER_TYPE_SP);

	if (intel_crtc->config->base.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE)
		val |= PIPECONF_INTERLACED_ILK;
	else
		val |= PIPECONF_PROGRESSIVE;

	if (intel_crtc->config->limited_color_range)
		val |= PIPECONF_COLOR_RANGE_SELECT;

	I915_WRITE(PIPECONF(pipe), val);
	POSTING_READ(PIPECONF(pipe));
}

static void haswell_set_pipeconf(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	u32 val = 0;

	if (IS_HASWELL(dev_priv) && intel_crtc->config->dither)
		val |= (PIPECONF_DITHER_EN | PIPECONF_DITHER_TYPE_SP);

	if (intel_crtc->config->base.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE)
		val |= PIPECONF_INTERLACED_ILK;
	else
		val |= PIPECONF_PROGRESSIVE;

	I915_WRITE(PIPECONF(cpu_transcoder), val);
	POSTING_READ(PIPECONF(cpu_transcoder));
}

static void haswell_set_pipemisc(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	if (IS_BROADWELL(dev_priv) || INTEL_INFO(dev_priv)->gen >= 9) {
		u32 val = 0;

		switch (intel_crtc->config->pipe_bpp) {
		case 18:
			val |= PIPEMISC_DITHER_6_BPC;
			break;
		case 24:
			val |= PIPEMISC_DITHER_8_BPC;
			break;
		case 30:
			val |= PIPEMISC_DITHER_10_BPC;
			break;
		case 36:
			val |= PIPEMISC_DITHER_12_BPC;
			break;
		default:
			/* Case prevented by pipe_config_set_bpp. */
			BUG();
		}

		if (intel_crtc->config->dither)
			val |= PIPEMISC_DITHER_ENABLE | PIPEMISC_DITHER_TYPE_SP;

		I915_WRITE(PIPEMISC(intel_crtc->pipe), val);
	}
}

int ironlake_get_lanes_required(int target_clock, int link_bw, int bpp)
{
	/*
	 * Account for spread spectrum to avoid
	 * oversubscribing the link. Max center spread
	 * is 2.5%; use 5% for safety's sake.
	 */
	u32 bps = target_clock * bpp * 21 / 20;
	return DIV_ROUND_UP(bps, link_bw * 8);
}

static bool ironlake_needs_fb_cb_tune(struct dpll *dpll, int factor)
{
	return i9xx_dpll_compute_m(dpll) < factor * dpll->n;
}

static void ironlake_compute_dpll(struct intel_crtc *intel_crtc,
				  struct intel_crtc_state *crtc_state,
				  struct dpll *reduced_clock)
{
	struct drm_crtc *crtc = &intel_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 dpll, fp, fp2;
	int factor;

	/* Enable autotuning of the PLL clock (if permissible) */
	factor = 21;
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if ((intel_panel_use_ssc(dev_priv) &&
		     dev_priv->vbt.lvds_ssc_freq == 100000) ||
		    (HAS_PCH_IBX(dev_priv) && intel_is_dual_link_lvds(dev)))
			factor = 25;
	} else if (crtc_state->sdvo_tv_clock)
		factor = 20;

	fp = i9xx_dpll_compute_fp(&crtc_state->dpll);

	if (ironlake_needs_fb_cb_tune(&crtc_state->dpll, factor))
		fp |= FP_CB_TUNE;

	if (reduced_clock) {
		fp2 = i9xx_dpll_compute_fp(reduced_clock);

		if (reduced_clock->m < factor * reduced_clock->n)
			fp2 |= FP_CB_TUNE;
	} else {
		fp2 = fp;
	}

	dpll = 0;

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
	if (INTEL_INFO(dev_priv)->num_pipes == 3 &&
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_ANALOG))
		dpll |= DPLL_SDVO_HIGH_SPEED;

	/* compute bitmask from p1 value */
	dpll |= (1 << (crtc_state->dpll.p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
	/* also FPA1 */
	dpll |= (1 << (crtc_state->dpll.p1 - 1)) << DPLL_FPA1_P1_POST_DIV_SHIFT;

	switch (crtc_state->dpll.p2) {
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

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS) &&
	    intel_panel_use_ssc(dev_priv))
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	dpll |= DPLL_VCO_ENABLE;

	crtc_state->dpll_hw_state.dpll = dpll;
	crtc_state->dpll_hw_state.fp0 = fp;
	crtc_state->dpll_hw_state.fp1 = fp2;
}

static int ironlake_crtc_compute_clock(struct intel_crtc *crtc,
				       struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct dpll reduced_clock;
	bool has_reduced_clock = false;
	struct intel_shared_dpll *pll;
	const struct intel_limit *limit;
	int refclk = 120000;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	crtc->lowfreq_avail = false;

	/* CPU eDP is the only output that doesn't need a PCH PLL of its own. */
	if (!crtc_state->has_pch_encoder)
		return 0;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if (intel_panel_use_ssc(dev_priv)) {
			DRM_DEBUG_KMS("using SSC reference clock of %d kHz\n",
				      dev_priv->vbt.lvds_ssc_freq);
			refclk = dev_priv->vbt.lvds_ssc_freq;
		}

		if (intel_is_dual_link_lvds(dev)) {
			if (refclk == 100000)
				limit = &intel_limits_ironlake_dual_lvds_100m;
			else
				limit = &intel_limits_ironlake_dual_lvds;
		} else {
			if (refclk == 100000)
				limit = &intel_limits_ironlake_single_lvds_100m;
			else
				limit = &intel_limits_ironlake_single_lvds;
		}
	} else {
		limit = &intel_limits_ironlake_dac;
	}

	if (!crtc_state->clock_set &&
	    !g4x_find_best_dpll(limit, crtc_state, crtc_state->port_clock,
				refclk, NULL, &crtc_state->dpll)) {
		DRM_ERROR("Couldn't find PLL settings for mode!\n");
		return -EINVAL;
	}

	ironlake_compute_dpll(crtc, crtc_state,
			      has_reduced_clock ? &reduced_clock : NULL);

	pll = intel_get_shared_dpll(crtc, crtc_state, NULL);
	if (pll == NULL) {
		DRM_DEBUG_DRIVER("failed to find PLL for pipe %c\n",
				 pipe_name(crtc->pipe));
		return -EINVAL;
	}

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS) &&
	    has_reduced_clock)
		crtc->lowfreq_avail = true;

	return 0;
}

static void intel_pch_transcoder_get_m_n(struct intel_crtc *crtc,
					 struct intel_link_m_n *m_n)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = crtc->pipe;

	m_n->link_m = I915_READ(PCH_TRANS_LINK_M1(pipe));
	m_n->link_n = I915_READ(PCH_TRANS_LINK_N1(pipe));
	m_n->gmch_m = I915_READ(PCH_TRANS_DATA_M1(pipe))
		& ~TU_SIZE_MASK;
	m_n->gmch_n = I915_READ(PCH_TRANS_DATA_N1(pipe));
	m_n->tu = ((I915_READ(PCH_TRANS_DATA_M1(pipe))
		    & TU_SIZE_MASK) >> TU_SIZE_SHIFT) + 1;
}

static void intel_cpu_transcoder_get_m_n(struct intel_crtc *crtc,
					 enum transcoder transcoder,
					 struct intel_link_m_n *m_n,
					 struct intel_link_m_n *m2_n2)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	if (INTEL_GEN(dev_priv) >= 5) {
		m_n->link_m = I915_READ(PIPE_LINK_M1(transcoder));
		m_n->link_n = I915_READ(PIPE_LINK_N1(transcoder));
		m_n->gmch_m = I915_READ(PIPE_DATA_M1(transcoder))
			& ~TU_SIZE_MASK;
		m_n->gmch_n = I915_READ(PIPE_DATA_N1(transcoder));
		m_n->tu = ((I915_READ(PIPE_DATA_M1(transcoder))
			    & TU_SIZE_MASK) >> TU_SIZE_SHIFT) + 1;
		/* Read M2_N2 registers only for gen < 8 (M2_N2 available for
		 * gen < 8) and if DRRS is supported (to make sure the
		 * registers are not unnecessarily read).
		 */
		if (m2_n2 && INTEL_GEN(dev_priv) < 8 &&
			crtc->config->has_drrs) {
			m2_n2->link_m = I915_READ(PIPE_LINK_M2(transcoder));
			m2_n2->link_n =	I915_READ(PIPE_LINK_N2(transcoder));
			m2_n2->gmch_m =	I915_READ(PIPE_DATA_M2(transcoder))
					& ~TU_SIZE_MASK;
			m2_n2->gmch_n =	I915_READ(PIPE_DATA_N2(transcoder));
			m2_n2->tu = ((I915_READ(PIPE_DATA_M2(transcoder))
					& TU_SIZE_MASK) >> TU_SIZE_SHIFT) + 1;
		}
	} else {
		m_n->link_m = I915_READ(PIPE_LINK_M_G4X(pipe));
		m_n->link_n = I915_READ(PIPE_LINK_N_G4X(pipe));
		m_n->gmch_m = I915_READ(PIPE_DATA_M_G4X(pipe))
			& ~TU_SIZE_MASK;
		m_n->gmch_n = I915_READ(PIPE_DATA_N_G4X(pipe));
		m_n->tu = ((I915_READ(PIPE_DATA_M_G4X(pipe))
			    & TU_SIZE_MASK) >> TU_SIZE_SHIFT) + 1;
	}
}

void intel_dp_get_m_n(struct intel_crtc *crtc,
		      struct intel_crtc_state *pipe_config)
{
	if (pipe_config->has_pch_encoder)
		intel_pch_transcoder_get_m_n(crtc, &pipe_config->dp_m_n);
	else
		intel_cpu_transcoder_get_m_n(crtc, pipe_config->cpu_transcoder,
					     &pipe_config->dp_m_n,
					     &pipe_config->dp_m2_n2);
}

static void ironlake_get_fdi_m_n_config(struct intel_crtc *crtc,
					struct intel_crtc_state *pipe_config)
{
	intel_cpu_transcoder_get_m_n(crtc, pipe_config->cpu_transcoder,
				     &pipe_config->fdi_m_n, NULL);
}

static void skylake_get_pfit_config(struct intel_crtc *crtc,
				    struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc_scaler_state *scaler_state = &pipe_config->scaler_state;
	uint32_t ps_ctrl = 0;
	int id = -1;
	int i;

	/* find scaler attached to this pipe */
	for (i = 0; i < crtc->num_scalers; i++) {
		ps_ctrl = I915_READ(SKL_PS_CTRL(crtc->pipe, i));
		if (ps_ctrl & PS_SCALER_EN && !(ps_ctrl & PS_PLANE_SEL_MASK)) {
			id = i;
			pipe_config->pch_pfit.enabled = true;
			pipe_config->pch_pfit.pos = I915_READ(SKL_PS_WIN_POS(crtc->pipe, i));
			pipe_config->pch_pfit.size = I915_READ(SKL_PS_WIN_SZ(crtc->pipe, i));
			break;
		}
	}

	scaler_state->scaler_id = id;
	if (id >= 0) {
		scaler_state->scaler_users |= (1 << SKL_CRTC_INDEX);
	} else {
		scaler_state->scaler_users &= ~(1 << SKL_CRTC_INDEX);
	}
}

static void
skylake_get_initial_plane_config(struct intel_crtc *crtc,
				 struct intel_initial_plane_config *plane_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 val, base, offset, stride_mult, tiling;
	int pipe = crtc->pipe;
	int fourcc, pixel_format;
	unsigned int aligned_height;
	struct drm_framebuffer *fb;
	struct intel_framebuffer *intel_fb;

	intel_fb = kzalloc(sizeof(*intel_fb), GFP_KERNEL);
	if (!intel_fb) {
		DRM_DEBUG_KMS("failed to alloc fb\n");
		return;
	}

	fb = &intel_fb->base;

	fb->dev = dev;

	val = I915_READ(PLANE_CTL(pipe, 0));
	if (!(val & PLANE_CTL_ENABLE))
		goto error;

	pixel_format = val & PLANE_CTL_FORMAT_MASK;
	fourcc = skl_format_to_fourcc(pixel_format,
				      val & PLANE_CTL_ORDER_RGBX,
				      val & PLANE_CTL_ALPHA_MASK);
	fb->format = drm_format_info(fourcc);

	tiling = val & PLANE_CTL_TILED_MASK;
	switch (tiling) {
	case PLANE_CTL_TILED_LINEAR:
		fb->modifier = DRM_FORMAT_MOD_NONE;
		break;
	case PLANE_CTL_TILED_X:
		plane_config->tiling = I915_TILING_X;
		fb->modifier = I915_FORMAT_MOD_X_TILED;
		break;
	case PLANE_CTL_TILED_Y:
		fb->modifier = I915_FORMAT_MOD_Y_TILED;
		break;
	case PLANE_CTL_TILED_YF:
		fb->modifier = I915_FORMAT_MOD_Yf_TILED;
		break;
	default:
		MISSING_CASE(tiling);
		goto error;
	}

	base = I915_READ(PLANE_SURF(pipe, 0)) & 0xfffff000;
	plane_config->base = base;

	offset = I915_READ(PLANE_OFFSET(pipe, 0));

	val = I915_READ(PLANE_SIZE(pipe, 0));
	fb->height = ((val >> 16) & 0xfff) + 1;
	fb->width = ((val >> 0) & 0x1fff) + 1;

	val = I915_READ(PLANE_STRIDE(pipe, 0));
	stride_mult = intel_fb_stride_alignment(dev_priv, fb->modifier,
						fb->format->format);
	fb->pitches[0] = (val & 0x3ff) * stride_mult;

	aligned_height = intel_fb_align_height(dev_priv,
					       fb->height,
					       fb->format->format,
					       fb->modifier);

	plane_config->size = fb->pitches[0] * aligned_height;

	DRM_DEBUG_KMS("pipe %c with fb: size=%dx%d@%d, offset=%x, pitch %d, size 0x%x\n",
		      pipe_name(pipe), fb->width, fb->height,
		      fb->format->cpp[0] * 8, base, fb->pitches[0],
		      plane_config->size);

	plane_config->fb = intel_fb;
	return;

error:
	kfree(intel_fb);
}

static void ironlake_get_pfit_config(struct intel_crtc *crtc,
				     struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	uint32_t tmp;

	tmp = I915_READ(PF_CTL(crtc->pipe));

	if (tmp & PF_ENABLE) {
		pipe_config->pch_pfit.enabled = true;
		pipe_config->pch_pfit.pos = I915_READ(PF_WIN_POS(crtc->pipe));
		pipe_config->pch_pfit.size = I915_READ(PF_WIN_SZ(crtc->pipe));

		/* We currently do not free assignements of panel fitters on
		 * ivb/hsw (since we don't use the higher upscaling modes which
		 * differentiates them) so just WARN about this case for now. */
		if (IS_GEN7(dev_priv)) {
			WARN_ON((tmp & PF_PIPE_SEL_MASK_IVB) !=
				PF_PIPE_SEL_IVB(crtc->pipe));
		}
	}
}

static void
ironlake_get_initial_plane_config(struct intel_crtc *crtc,
				  struct intel_initial_plane_config *plane_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 val, base, offset;
	int pipe = crtc->pipe;
	int fourcc, pixel_format;
	unsigned int aligned_height;
	struct drm_framebuffer *fb;
	struct intel_framebuffer *intel_fb;

	val = I915_READ(DSPCNTR(pipe));
	if (!(val & DISPLAY_PLANE_ENABLE))
		return;

	intel_fb = kzalloc(sizeof(*intel_fb), GFP_KERNEL);
	if (!intel_fb) {
		DRM_DEBUG_KMS("failed to alloc fb\n");
		return;
	}

	fb = &intel_fb->base;

	fb->dev = dev;

	if (INTEL_GEN(dev_priv) >= 4) {
		if (val & DISPPLANE_TILED) {
			plane_config->tiling = I915_TILING_X;
			fb->modifier = I915_FORMAT_MOD_X_TILED;
		}
	}

	pixel_format = val & DISPPLANE_PIXFORMAT_MASK;
	fourcc = i9xx_format_to_fourcc(pixel_format);
	fb->format = drm_format_info(fourcc);

	base = I915_READ(DSPSURF(pipe)) & 0xfffff000;
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		offset = I915_READ(DSPOFFSET(pipe));
	} else {
		if (plane_config->tiling)
			offset = I915_READ(DSPTILEOFF(pipe));
		else
			offset = I915_READ(DSPLINOFF(pipe));
	}
	plane_config->base = base;

	val = I915_READ(PIPESRC(pipe));
	fb->width = ((val >> 16) & 0xfff) + 1;
	fb->height = ((val >> 0) & 0xfff) + 1;

	val = I915_READ(DSPSTRIDE(pipe));
	fb->pitches[0] = val & 0xffffffc0;

	aligned_height = intel_fb_align_height(dev_priv,
					       fb->height,
					       fb->format->format,
					       fb->modifier);

	plane_config->size = fb->pitches[0] * aligned_height;

	DRM_DEBUG_KMS("pipe %c with fb: size=%dx%d@%d, offset=%x, pitch %d, size 0x%x\n",
		      pipe_name(pipe), fb->width, fb->height,
		      fb->format->cpp[0] * 8, base, fb->pitches[0],
		      plane_config->size);

	plane_config->fb = intel_fb;
}

static bool ironlake_get_pipe_config(struct intel_crtc *crtc,
				     struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum intel_display_power_domain power_domain;
	uint32_t tmp;
	bool ret;

	power_domain = POWER_DOMAIN_PIPE(crtc->pipe);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;

	pipe_config->cpu_transcoder = (enum transcoder) crtc->pipe;
	pipe_config->shared_dpll = NULL;

	ret = false;
	tmp = I915_READ(PIPECONF(crtc->pipe));
	if (!(tmp & PIPECONF_ENABLE))
		goto out;

	switch (tmp & PIPECONF_BPC_MASK) {
	case PIPECONF_6BPC:
		pipe_config->pipe_bpp = 18;
		break;
	case PIPECONF_8BPC:
		pipe_config->pipe_bpp = 24;
		break;
	case PIPECONF_10BPC:
		pipe_config->pipe_bpp = 30;
		break;
	case PIPECONF_12BPC:
		pipe_config->pipe_bpp = 36;
		break;
	default:
		break;
	}

	if (tmp & PIPECONF_COLOR_RANGE_SELECT)
		pipe_config->limited_color_range = true;

	if (I915_READ(PCH_TRANSCONF(crtc->pipe)) & TRANS_ENABLE) {
		struct intel_shared_dpll *pll;
		enum intel_dpll_id pll_id;

		pipe_config->has_pch_encoder = true;

		tmp = I915_READ(FDI_RX_CTL(crtc->pipe));
		pipe_config->fdi_lanes = ((FDI_DP_PORT_WIDTH_MASK & tmp) >>
					  FDI_DP_PORT_WIDTH_SHIFT) + 1;

		ironlake_get_fdi_m_n_config(crtc, pipe_config);

		if (HAS_PCH_IBX(dev_priv)) {
			/*
			 * The pipe->pch transcoder and pch transcoder->pll
			 * mapping is fixed.
			 */
			pll_id = (enum intel_dpll_id) crtc->pipe;
		} else {
			tmp = I915_READ(PCH_DPLL_SEL);
			if (tmp & TRANS_DPLLB_SEL(crtc->pipe))
				pll_id = DPLL_ID_PCH_PLL_B;
			else
				pll_id= DPLL_ID_PCH_PLL_A;
		}

		pipe_config->shared_dpll =
			intel_get_shared_dpll_by_id(dev_priv, pll_id);
		pll = pipe_config->shared_dpll;

		WARN_ON(!pll->funcs.get_hw_state(dev_priv, pll,
						 &pipe_config->dpll_hw_state));

		tmp = pipe_config->dpll_hw_state.dpll;
		pipe_config->pixel_multiplier =
			((tmp & PLL_REF_SDVO_HDMI_MULTIPLIER_MASK)
			 >> PLL_REF_SDVO_HDMI_MULTIPLIER_SHIFT) + 1;

		ironlake_pch_clock_get(crtc, pipe_config);
	} else {
		pipe_config->pixel_multiplier = 1;
	}

	intel_get_pipe_timings(crtc, pipe_config);
	intel_get_pipe_src_size(crtc, pipe_config);

	ironlake_get_pfit_config(crtc, pipe_config);

	ret = true;

out:
	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

static void assert_can_disable_lcpll(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc;

	for_each_intel_crtc(dev, crtc)
		I915_STATE_WARN(crtc->active, "CRTC for pipe %c enabled\n",
		     pipe_name(crtc->pipe));

	I915_STATE_WARN(I915_READ(HSW_PWR_WELL_DRIVER), "Power well on\n");
	I915_STATE_WARN(I915_READ(SPLL_CTL) & SPLL_PLL_ENABLE, "SPLL enabled\n");
	I915_STATE_WARN(I915_READ(WRPLL_CTL(0)) & WRPLL_PLL_ENABLE, "WRPLL1 enabled\n");
	I915_STATE_WARN(I915_READ(WRPLL_CTL(1)) & WRPLL_PLL_ENABLE, "WRPLL2 enabled\n");
	I915_STATE_WARN(I915_READ(PP_STATUS(0)) & PP_ON, "Panel power on\n");
	I915_STATE_WARN(I915_READ(BLC_PWM_CPU_CTL2) & BLM_PWM_ENABLE,
	     "CPU PWM1 enabled\n");
	if (IS_HASWELL(dev_priv))
		I915_STATE_WARN(I915_READ(HSW_BLC_PWM2_CTL) & BLM_PWM_ENABLE,
		     "CPU PWM2 enabled\n");
	I915_STATE_WARN(I915_READ(BLC_PWM_PCH_CTL1) & BLM_PCH_PWM_ENABLE,
	     "PCH PWM1 enabled\n");
	I915_STATE_WARN(I915_READ(UTIL_PIN_CTL) & UTIL_PIN_ENABLE,
	     "Utility pin enabled\n");
	I915_STATE_WARN(I915_READ(PCH_GTC_CTL) & PCH_GTC_ENABLE, "PCH GTC enabled\n");

	/*
	 * In theory we can still leave IRQs enabled, as long as only the HPD
	 * interrupts remain enabled. We used to check for that, but since it's
	 * gen-specific and since we only disable LCPLL after we fully disable
	 * the interrupts, the check below should be enough.
	 */
	I915_STATE_WARN(intel_irqs_enabled(dev_priv), "IRQs enabled\n");
}

static uint32_t hsw_read_dcomp(struct drm_i915_private *dev_priv)
{
	if (IS_HASWELL(dev_priv))
		return I915_READ(D_COMP_HSW);
	else
		return I915_READ(D_COMP_BDW);
}

static void hsw_write_dcomp(struct drm_i915_private *dev_priv, uint32_t val)
{
	if (IS_HASWELL(dev_priv)) {
		mutex_lock(&dev_priv->rps.hw_lock);
		if (sandybridge_pcode_write(dev_priv, GEN6_PCODE_WRITE_D_COMP,
					    val))
			DRM_DEBUG_KMS("Failed to write to D_COMP\n");
		mutex_unlock(&dev_priv->rps.hw_lock);
	} else {
		I915_WRITE(D_COMP_BDW, val);
		POSTING_READ(D_COMP_BDW);
	}
}

/*
 * This function implements pieces of two sequences from BSpec:
 * - Sequence for display software to disable LCPLL
 * - Sequence for display software to allow package C8+
 * The steps implemented here are just the steps that actually touch the LCPLL
 * register. Callers should take care of disabling all the display engine
 * functions, doing the mode unset, fixing interrupts, etc.
 */
static void hsw_disable_lcpll(struct drm_i915_private *dev_priv,
			      bool switch_to_fclk, bool allow_power_down)
{
	uint32_t val;

	assert_can_disable_lcpll(dev_priv);

	val = I915_READ(LCPLL_CTL);

	if (switch_to_fclk) {
		val |= LCPLL_CD_SOURCE_FCLK;
		I915_WRITE(LCPLL_CTL, val);

		if (wait_for_us(I915_READ(LCPLL_CTL) &
				LCPLL_CD_SOURCE_FCLK_DONE, 1))
			DRM_ERROR("Switching to FCLK failed\n");

		val = I915_READ(LCPLL_CTL);
	}

	val |= LCPLL_PLL_DISABLE;
	I915_WRITE(LCPLL_CTL, val);
	POSTING_READ(LCPLL_CTL);

	if (intel_wait_for_register(dev_priv, LCPLL_CTL, LCPLL_PLL_LOCK, 0, 1))
		DRM_ERROR("LCPLL still locked\n");

	val = hsw_read_dcomp(dev_priv);
	val |= D_COMP_COMP_DISABLE;
	hsw_write_dcomp(dev_priv, val);
	ndelay(100);

	if (wait_for((hsw_read_dcomp(dev_priv) & D_COMP_RCOMP_IN_PROGRESS) == 0,
		     1))
		DRM_ERROR("D_COMP RCOMP still in progress\n");

	if (allow_power_down) {
		val = I915_READ(LCPLL_CTL);
		val |= LCPLL_POWER_DOWN_ALLOW;
		I915_WRITE(LCPLL_CTL, val);
		POSTING_READ(LCPLL_CTL);
	}
}

/*
 * Fully restores LCPLL, disallowing power down and switching back to LCPLL
 * source.
 */
static void hsw_restore_lcpll(struct drm_i915_private *dev_priv)
{
	uint32_t val;

	val = I915_READ(LCPLL_CTL);

	if ((val & (LCPLL_PLL_LOCK | LCPLL_PLL_DISABLE | LCPLL_CD_SOURCE_FCLK |
		    LCPLL_POWER_DOWN_ALLOW)) == LCPLL_PLL_LOCK)
		return;

	/*
	 * Make sure we're not on PC8 state before disabling PC8, otherwise
	 * we'll hang the machine. To prevent PC8 state, just enable force_wake.
	 */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	if (val & LCPLL_POWER_DOWN_ALLOW) {
		val &= ~LCPLL_POWER_DOWN_ALLOW;
		I915_WRITE(LCPLL_CTL, val);
		POSTING_READ(LCPLL_CTL);
	}

	val = hsw_read_dcomp(dev_priv);
	val |= D_COMP_COMP_FORCE;
	val &= ~D_COMP_COMP_DISABLE;
	hsw_write_dcomp(dev_priv, val);

	val = I915_READ(LCPLL_CTL);
	val &= ~LCPLL_PLL_DISABLE;
	I915_WRITE(LCPLL_CTL, val);

	if (intel_wait_for_register(dev_priv,
				    LCPLL_CTL, LCPLL_PLL_LOCK, LCPLL_PLL_LOCK,
				    5))
		DRM_ERROR("LCPLL not locked yet\n");

	if (val & LCPLL_CD_SOURCE_FCLK) {
		val = I915_READ(LCPLL_CTL);
		val &= ~LCPLL_CD_SOURCE_FCLK;
		I915_WRITE(LCPLL_CTL, val);

		if (wait_for_us((I915_READ(LCPLL_CTL) &
				 LCPLL_CD_SOURCE_FCLK_DONE) == 0, 1))
			DRM_ERROR("Switching back to LCPLL failed\n");
	}

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
	intel_update_cdclk(dev_priv);
}

/*
 * Package states C8 and deeper are really deep PC states that can only be
 * reached when all the devices on the system allow it, so even if the graphics
 * device allows PC8+, it doesn't mean the system will actually get to these
 * states. Our driver only allows PC8+ when going into runtime PM.
 *
 * The requirements for PC8+ are that all the outputs are disabled, the power
 * well is disabled and most interrupts are disabled, and these are also
 * requirements for runtime PM. When these conditions are met, we manually do
 * the other conditions: disable the interrupts, clocks and switch LCPLL refclk
 * to Fclk. If we're in PC8+ and we get an non-hotplug interrupt, we can hard
 * hang the machine.
 *
 * When we really reach PC8 or deeper states (not just when we allow it) we lose
 * the state of some registers, so when we come back from PC8+ we need to
 * restore this state. We don't get into PC8+ if we're not in RC6, so we don't
 * need to take care of the registers kept by RC6. Notice that this happens even
 * if we don't put the device in PCI D3 state (which is what currently happens
 * because of the runtime PM support).
 *
 * For more, read "Display Sequences for Package C8" on the hardware
 * documentation.
 */
void hsw_enable_pc8(struct drm_i915_private *dev_priv)
{
	uint32_t val;

	DRM_DEBUG_KMS("Enabling package C8+\n");

	if (HAS_PCH_LPT_LP(dev_priv)) {
		val = I915_READ(SOUTH_DSPCLK_GATE_D);
		val &= ~PCH_LP_PARTITION_LEVEL_DISABLE;
		I915_WRITE(SOUTH_DSPCLK_GATE_D, val);
	}

	lpt_disable_clkout_dp(dev_priv);
	hsw_disable_lcpll(dev_priv, true, true);
}

void hsw_disable_pc8(struct drm_i915_private *dev_priv)
{
	uint32_t val;

	DRM_DEBUG_KMS("Disabling package C8+\n");

	hsw_restore_lcpll(dev_priv);
	lpt_init_pch_refclk(dev_priv);

	if (HAS_PCH_LPT_LP(dev_priv)) {
		val = I915_READ(SOUTH_DSPCLK_GATE_D);
		val |= PCH_LP_PARTITION_LEVEL_DISABLE;
		I915_WRITE(SOUTH_DSPCLK_GATE_D, val);
	}
}

static int haswell_crtc_compute_clock(struct intel_crtc *crtc,
				      struct intel_crtc_state *crtc_state)
{
	if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI)) {
		if (!intel_ddi_pll_select(crtc, crtc_state))
			return -EINVAL;
	}

	crtc->lowfreq_avail = false;

	return 0;
}

static void bxt_get_ddi_pll(struct drm_i915_private *dev_priv,
				enum port port,
				struct intel_crtc_state *pipe_config)
{
	enum intel_dpll_id id;

	switch (port) {
	case PORT_A:
		id = DPLL_ID_SKL_DPLL0;
		break;
	case PORT_B:
		id = DPLL_ID_SKL_DPLL1;
		break;
	case PORT_C:
		id = DPLL_ID_SKL_DPLL2;
		break;
	default:
		DRM_ERROR("Incorrect port type\n");
		return;
	}

	pipe_config->shared_dpll = intel_get_shared_dpll_by_id(dev_priv, id);
}

static void skylake_get_ddi_pll(struct drm_i915_private *dev_priv,
				enum port port,
				struct intel_crtc_state *pipe_config)
{
	enum intel_dpll_id id;
	u32 temp;

	temp = I915_READ(DPLL_CTRL2) & DPLL_CTRL2_DDI_CLK_SEL_MASK(port);
	id = temp >> (port * 3 + 1);

	if (WARN_ON(id < SKL_DPLL0 || id > SKL_DPLL3))
		return;

	pipe_config->shared_dpll = intel_get_shared_dpll_by_id(dev_priv, id);
}

static void haswell_get_ddi_pll(struct drm_i915_private *dev_priv,
				enum port port,
				struct intel_crtc_state *pipe_config)
{
	enum intel_dpll_id id;
	uint32_t ddi_pll_sel = I915_READ(PORT_CLK_SEL(port));

	switch (ddi_pll_sel) {
	case PORT_CLK_SEL_WRPLL1:
		id = DPLL_ID_WRPLL1;
		break;
	case PORT_CLK_SEL_WRPLL2:
		id = DPLL_ID_WRPLL2;
		break;
	case PORT_CLK_SEL_SPLL:
		id = DPLL_ID_SPLL;
		break;
	case PORT_CLK_SEL_LCPLL_810:
		id = DPLL_ID_LCPLL_810;
		break;
	case PORT_CLK_SEL_LCPLL_1350:
		id = DPLL_ID_LCPLL_1350;
		break;
	case PORT_CLK_SEL_LCPLL_2700:
		id = DPLL_ID_LCPLL_2700;
		break;
	default:
		MISSING_CASE(ddi_pll_sel);
		/* fall through */
	case PORT_CLK_SEL_NONE:
		return;
	}

	pipe_config->shared_dpll = intel_get_shared_dpll_by_id(dev_priv, id);
}

static bool hsw_get_transcoder_state(struct intel_crtc *crtc,
				     struct intel_crtc_state *pipe_config,
				     u64 *power_domain_mask)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum intel_display_power_domain power_domain;
	u32 tmp;

	/*
	 * The pipe->transcoder mapping is fixed with the exception of the eDP
	 * transcoder handled below.
	 */
	pipe_config->cpu_transcoder = (enum transcoder) crtc->pipe;

	/*
	 * XXX: Do intel_display_power_get_if_enabled before reading this (for
	 * consistency and less surprising code; it's in always on power).
	 */
	tmp = I915_READ(TRANS_DDI_FUNC_CTL(TRANSCODER_EDP));
	if (tmp & TRANS_DDI_FUNC_ENABLE) {
		enum pipe trans_edp_pipe;
		switch (tmp & TRANS_DDI_EDP_INPUT_MASK) {
		default:
			WARN(1, "unknown pipe linked to edp transcoder\n");
		case TRANS_DDI_EDP_INPUT_A_ONOFF:
		case TRANS_DDI_EDP_INPUT_A_ON:
			trans_edp_pipe = PIPE_A;
			break;
		case TRANS_DDI_EDP_INPUT_B_ONOFF:
			trans_edp_pipe = PIPE_B;
			break;
		case TRANS_DDI_EDP_INPUT_C_ONOFF:
			trans_edp_pipe = PIPE_C;
			break;
		}

		if (trans_edp_pipe == crtc->pipe)
			pipe_config->cpu_transcoder = TRANSCODER_EDP;
	}

	power_domain = POWER_DOMAIN_TRANSCODER(pipe_config->cpu_transcoder);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;
	*power_domain_mask |= BIT_ULL(power_domain);

	tmp = I915_READ(PIPECONF(pipe_config->cpu_transcoder));

	return tmp & PIPECONF_ENABLE;
}

static bool bxt_get_dsi_transcoder_state(struct intel_crtc *crtc,
					 struct intel_crtc_state *pipe_config,
					 u64 *power_domain_mask)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum intel_display_power_domain power_domain;
	enum port port;
	enum transcoder cpu_transcoder;
	u32 tmp;

	for_each_port_masked(port, BIT(PORT_A) | BIT(PORT_C)) {
		if (port == PORT_A)
			cpu_transcoder = TRANSCODER_DSI_A;
		else
			cpu_transcoder = TRANSCODER_DSI_C;

		power_domain = POWER_DOMAIN_TRANSCODER(cpu_transcoder);
		if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
			continue;
		*power_domain_mask |= BIT_ULL(power_domain);

		/*
		 * The PLL needs to be enabled with a valid divider
		 * configuration, otherwise accessing DSI registers will hang
		 * the machine. See BSpec North Display Engine
		 * registers/MIPI[BXT]. We can break out here early, since we
		 * need the same DSI PLL to be enabled for both DSI ports.
		 */
		if (!intel_dsi_pll_is_enabled(dev_priv))
			break;

		/* XXX: this works for video mode only */
		tmp = I915_READ(BXT_MIPI_PORT_CTRL(port));
		if (!(tmp & DPI_ENABLE))
			continue;

		tmp = I915_READ(MIPI_CTRL(port));
		if ((tmp & BXT_PIPE_SELECT_MASK) != BXT_PIPE_SELECT(crtc->pipe))
			continue;

		pipe_config->cpu_transcoder = cpu_transcoder;
		break;
	}

	return transcoder_is_dsi(pipe_config->cpu_transcoder);
}

static void haswell_get_ddi_port_state(struct intel_crtc *crtc,
				       struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_shared_dpll *pll;
	enum port port;
	uint32_t tmp;

	tmp = I915_READ(TRANS_DDI_FUNC_CTL(pipe_config->cpu_transcoder));

	port = (tmp & TRANS_DDI_PORT_MASK) >> TRANS_DDI_PORT_SHIFT;

	if (IS_GEN9_BC(dev_priv))
		skylake_get_ddi_pll(dev_priv, port, pipe_config);
	else if (IS_GEN9_LP(dev_priv))
		bxt_get_ddi_pll(dev_priv, port, pipe_config);
	else
		haswell_get_ddi_pll(dev_priv, port, pipe_config);

	pll = pipe_config->shared_dpll;
	if (pll) {
		WARN_ON(!pll->funcs.get_hw_state(dev_priv, pll,
						 &pipe_config->dpll_hw_state));
	}

	/*
	 * Haswell has only FDI/PCH transcoder A. It is which is connected to
	 * DDI E. So just check whether this pipe is wired to DDI E and whether
	 * the PCH transcoder is on.
	 */
	if (INTEL_GEN(dev_priv) < 9 &&
	    (port == PORT_E) && I915_READ(LPT_TRANSCONF) & TRANS_ENABLE) {
		pipe_config->has_pch_encoder = true;

		tmp = I915_READ(FDI_RX_CTL(PIPE_A));
		pipe_config->fdi_lanes = ((FDI_DP_PORT_WIDTH_MASK & tmp) >>
					  FDI_DP_PORT_WIDTH_SHIFT) + 1;

		ironlake_get_fdi_m_n_config(crtc, pipe_config);
	}
}

static bool haswell_get_pipe_config(struct intel_crtc *crtc,
				    struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum intel_display_power_domain power_domain;
	u64 power_domain_mask;
	bool active;

	power_domain = POWER_DOMAIN_PIPE(crtc->pipe);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;
	power_domain_mask = BIT_ULL(power_domain);

	pipe_config->shared_dpll = NULL;

	active = hsw_get_transcoder_state(crtc, pipe_config, &power_domain_mask);

	if (IS_GEN9_LP(dev_priv) &&
	    bxt_get_dsi_transcoder_state(crtc, pipe_config, &power_domain_mask)) {
		WARN_ON(active);
		active = true;
	}

	if (!active)
		goto out;

	if (!transcoder_is_dsi(pipe_config->cpu_transcoder)) {
		haswell_get_ddi_port_state(crtc, pipe_config);
		intel_get_pipe_timings(crtc, pipe_config);
	}

	intel_get_pipe_src_size(crtc, pipe_config);

	pipe_config->gamma_mode =
		I915_READ(GAMMA_MODE(crtc->pipe)) & GAMMA_MODE_MODE_MASK;

	if (INTEL_GEN(dev_priv) >= 9) {
		intel_crtc_init_scalers(crtc, pipe_config);

		pipe_config->scaler_state.scaler_id = -1;
		pipe_config->scaler_state.scaler_users &= ~(1 << SKL_CRTC_INDEX);
	}

	power_domain = POWER_DOMAIN_PIPE_PANEL_FITTER(crtc->pipe);
	if (intel_display_power_get_if_enabled(dev_priv, power_domain)) {
		power_domain_mask |= BIT_ULL(power_domain);
		if (INTEL_GEN(dev_priv) >= 9)
			skylake_get_pfit_config(crtc, pipe_config);
		else
			ironlake_get_pfit_config(crtc, pipe_config);
	}

	if (IS_HASWELL(dev_priv))
		pipe_config->ips_enabled = hsw_crtc_supports_ips(crtc) &&
			(I915_READ(IPS_CTL) & IPS_ENABLE);

	if (pipe_config->cpu_transcoder != TRANSCODER_EDP &&
	    !transcoder_is_dsi(pipe_config->cpu_transcoder)) {
		pipe_config->pixel_multiplier =
			I915_READ(PIPE_MULT(pipe_config->cpu_transcoder)) + 1;
	} else {
		pipe_config->pixel_multiplier = 1;
	}

out:
	for_each_power_domain(power_domain, power_domain_mask)
		intel_display_power_put(dev_priv, power_domain);

	return active;
}

static void i845_update_cursor(struct drm_crtc *crtc, u32 base,
			       const struct intel_plane_state *plane_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	uint32_t cntl = 0, size = 0;

	if (plane_state && plane_state->base.visible) {
		unsigned int width = plane_state->base.crtc_w;
		unsigned int height = plane_state->base.crtc_h;
		unsigned int stride = roundup_pow_of_two(width) * 4;

		switch (stride) {
		default:
			WARN_ONCE(1, "Invalid cursor width/stride, width=%u, stride=%u\n",
				  width, stride);
			stride = 256;
			/* fallthrough */
		case 256:
		case 512:
		case 1024:
		case 2048:
			break;
		}

		cntl |= CURSOR_ENABLE |
			CURSOR_GAMMA_ENABLE |
			CURSOR_FORMAT_ARGB |
			CURSOR_STRIDE(stride);

		size = (height << 12) | width;
	}

	if (intel_crtc->cursor_cntl != 0 &&
	    (intel_crtc->cursor_base != base ||
	     intel_crtc->cursor_size != size ||
	     intel_crtc->cursor_cntl != cntl)) {
		/* On these chipsets we can only modify the base/size/stride
		 * whilst the cursor is disabled.
		 */
		I915_WRITE(CURCNTR(PIPE_A), 0);
		POSTING_READ(CURCNTR(PIPE_A));
		intel_crtc->cursor_cntl = 0;
	}

	if (intel_crtc->cursor_base != base) {
		I915_WRITE(CURBASE(PIPE_A), base);
		intel_crtc->cursor_base = base;
	}

	if (intel_crtc->cursor_size != size) {
		I915_WRITE(CURSIZE, size);
		intel_crtc->cursor_size = size;
	}

	if (intel_crtc->cursor_cntl != cntl) {
		I915_WRITE(CURCNTR(PIPE_A), cntl);
		POSTING_READ(CURCNTR(PIPE_A));
		intel_crtc->cursor_cntl = cntl;
	}
}

static void i9xx_update_cursor(struct drm_crtc *crtc, u32 base,
			       const struct intel_plane_state *plane_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	uint32_t cntl = 0;

	if (plane_state && plane_state->base.visible) {
		cntl = MCURSOR_GAMMA_ENABLE;
		switch (plane_state->base.crtc_w) {
			case 64:
				cntl |= CURSOR_MODE_64_ARGB_AX;
				break;
			case 128:
				cntl |= CURSOR_MODE_128_ARGB_AX;
				break;
			case 256:
				cntl |= CURSOR_MODE_256_ARGB_AX;
				break;
			default:
				MISSING_CASE(plane_state->base.crtc_w);
				return;
		}
		cntl |= pipe << 28; /* Connect to correct pipe */

		if (HAS_DDI(dev_priv))
			cntl |= CURSOR_PIPE_CSC_ENABLE;

		if (plane_state->base.rotation & DRM_ROTATE_180)
			cntl |= CURSOR_ROTATE_180;
	}

	if (intel_crtc->cursor_cntl != cntl) {
		I915_WRITE(CURCNTR(pipe), cntl);
		POSTING_READ(CURCNTR(pipe));
		intel_crtc->cursor_cntl = cntl;
	}

	/* and commit changes on next vblank */
	I915_WRITE(CURBASE(pipe), base);
	POSTING_READ(CURBASE(pipe));

	intel_crtc->cursor_base = base;
}

/* If no-part of the cursor is visible on the framebuffer, then the GPU may hang... */
static void intel_crtc_update_cursor(struct drm_crtc *crtc,
				     const struct intel_plane_state *plane_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 base = intel_crtc->cursor_addr;
	u32 pos = 0;

	if (plane_state) {
		int x = plane_state->base.crtc_x;
		int y = plane_state->base.crtc_y;

		if (x < 0) {
			pos |= CURSOR_POS_SIGN << CURSOR_X_SHIFT;
			x = -x;
		}
		pos |= x << CURSOR_X_SHIFT;

		if (y < 0) {
			pos |= CURSOR_POS_SIGN << CURSOR_Y_SHIFT;
			y = -y;
		}
		pos |= y << CURSOR_Y_SHIFT;

		/* ILK+ do this automagically */
		if (HAS_GMCH_DISPLAY(dev_priv) &&
		    plane_state->base.rotation & DRM_ROTATE_180) {
			base += (plane_state->base.crtc_h *
				 plane_state->base.crtc_w - 1) * 4;
		}
	}

	I915_WRITE(CURPOS(pipe), pos);

	if (IS_I845G(dev_priv) || IS_I865G(dev_priv))
		i845_update_cursor(crtc, base, plane_state);
	else
		i9xx_update_cursor(crtc, base, plane_state);
}

static bool cursor_size_ok(struct drm_i915_private *dev_priv,
			   uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0)
		return false;

	/*
	 * 845g/865g are special in that they are only limited by
	 * the width of their cursors, the height is arbitrary up to
	 * the precision of the register. Everything else requires
	 * square cursors, limited to a few power-of-two sizes.
	 */
	if (IS_I845G(dev_priv) || IS_I865G(dev_priv)) {
		if ((width & 63) != 0)
			return false;

		if (width > (IS_I845G(dev_priv) ? 64 : 512))
			return false;

		if (height > 1023)
			return false;
	} else {
		switch (width | height) {
		case 256:
		case 128:
			if (IS_GEN2(dev_priv))
				return false;
		case 64:
			break;
		default:
			return false;
		}
	}

	return true;
}

/* VESA 640x480x72Hz mode to set on the pipe */
static struct drm_display_mode load_detect_mode = {
	DRM_MODE("640x480", DRM_MODE_TYPE_DEFAULT, 31500, 640, 664,
		 704, 832, 0, 480, 489, 491, 520, 0, DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
};

struct drm_framebuffer *
intel_framebuffer_create(struct drm_i915_gem_object *obj,
			 struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct intel_framebuffer *intel_fb;
	int ret;

	intel_fb = kzalloc(sizeof(*intel_fb), GFP_KERNEL);
	if (!intel_fb)
		return ERR_PTR(-ENOMEM);

	ret = intel_framebuffer_init(intel_fb, obj, mode_cmd);
	if (ret)
		goto err;

	return &intel_fb->base;

err:
	kfree(intel_fb);
	return ERR_PTR(ret);
}

static u32
intel_framebuffer_pitch_for_width(int width, int bpp)
{
	u32 pitch = DIV_ROUND_UP(width * bpp, 8);
	return ALIGN(pitch, 64);
}

static u32
intel_framebuffer_size_for_mode(struct drm_display_mode *mode, int bpp)
{
	u32 pitch = intel_framebuffer_pitch_for_width(mode->hdisplay, bpp);
	return PAGE_ALIGN(pitch * mode->vdisplay);
}

static struct drm_framebuffer *
intel_framebuffer_create_for_mode(struct drm_device *dev,
				  struct drm_display_mode *mode,
				  int depth, int bpp)
{
	struct drm_framebuffer *fb;
	struct drm_i915_gem_object *obj;
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };

	obj = i915_gem_object_create(to_i915(dev),
				    intel_framebuffer_size_for_mode(mode, bpp));
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	mode_cmd.width = mode->hdisplay;
	mode_cmd.height = mode->vdisplay;
	mode_cmd.pitches[0] = intel_framebuffer_pitch_for_width(mode_cmd.width,
								bpp);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(bpp, depth);

	fb = intel_framebuffer_create(obj, &mode_cmd);
	if (IS_ERR(fb))
		i915_gem_object_put(obj);

	return fb;
}

static struct drm_framebuffer *
mode_fits_in_fbdev(struct drm_device *dev,
		   struct drm_display_mode *mode)
{
#ifdef CONFIG_DRM_FBDEV_EMULATION
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_object *obj;
	struct drm_framebuffer *fb;

	if (!dev_priv->fbdev)
		return NULL;

	if (!dev_priv->fbdev->fb)
		return NULL;

	obj = dev_priv->fbdev->fb->obj;
	BUG_ON(!obj);

	fb = &dev_priv->fbdev->fb->base;
	if (fb->pitches[0] < intel_framebuffer_pitch_for_width(mode->hdisplay,
							       fb->format->cpp[0] * 8))
		return NULL;

	if (obj->base.size < mode->vdisplay * fb->pitches[0])
		return NULL;

	drm_framebuffer_reference(fb);
	return fb;
#else
	return NULL;
#endif
}

static int intel_modeset_setup_plane_state(struct drm_atomic_state *state,
					   struct drm_crtc *crtc,
					   struct drm_display_mode *mode,
					   struct drm_framebuffer *fb,
					   int x, int y)
{
	struct drm_plane_state *plane_state;
	int hdisplay, vdisplay;
	int ret;

	plane_state = drm_atomic_get_plane_state(state, crtc->primary);
	if (IS_ERR(plane_state))
		return PTR_ERR(plane_state);

	if (mode)
		drm_mode_get_hv_timing(mode, &hdisplay, &vdisplay);
	else
		hdisplay = vdisplay = 0;

	ret = drm_atomic_set_crtc_for_plane(plane_state, fb ? crtc : NULL);
	if (ret)
		return ret;
	drm_atomic_set_fb_for_plane(plane_state, fb);
	plane_state->crtc_x = 0;
	plane_state->crtc_y = 0;
	plane_state->crtc_w = hdisplay;
	plane_state->crtc_h = vdisplay;
	plane_state->src_x = x << 16;
	plane_state->src_y = y << 16;
	plane_state->src_w = hdisplay << 16;
	plane_state->src_h = vdisplay << 16;

	return 0;
}

bool intel_get_load_detect_pipe(struct drm_connector *connector,
				struct drm_display_mode *mode,
				struct intel_load_detect_pipe *old,
				struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_crtc *intel_crtc;
	struct intel_encoder *intel_encoder =
		intel_attached_encoder(connector);
	struct drm_crtc *possible_crtc;
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_crtc *crtc = NULL;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_framebuffer *fb;
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_atomic_state *state = NULL, *restore_state = NULL;
	struct drm_connector_state *connector_state;
	struct intel_crtc_state *crtc_state;
	int ret, i = -1;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
		      connector->base.id, connector->name,
		      encoder->base.id, encoder->name);

	old->restore_state = NULL;

retry:
	ret = drm_modeset_lock(&config->connection_mutex, ctx);
	if (ret)
		goto fail;

	/*
	 * Algorithm gets a little messy:
	 *
	 *   - if the connector already has an assigned crtc, use it (but make
	 *     sure it's on first)
	 *
	 *   - try to find the first unused crtc that can drive this connector,
	 *     and use that if we find one
	 */

	/* See if we already have a CRTC for this connector */
	if (connector->state->crtc) {
		crtc = connector->state->crtc;

		ret = drm_modeset_lock(&crtc->mutex, ctx);
		if (ret)
			goto fail;

		/* Make sure the crtc and connector are running */
		goto found;
	}

	/* Find an unused one (if possible) */
	for_each_crtc(dev, possible_crtc) {
		i++;
		if (!(encoder->possible_crtcs & (1 << i)))
			continue;

		ret = drm_modeset_lock(&possible_crtc->mutex, ctx);
		if (ret)
			goto fail;

		if (possible_crtc->state->enable) {
			drm_modeset_unlock(&possible_crtc->mutex);
			continue;
		}

		crtc = possible_crtc;
		break;
	}

	/*
	 * If we didn't find an unused CRTC, don't use any.
	 */
	if (!crtc) {
		DRM_DEBUG_KMS("no pipe available for load-detect\n");
		goto fail;
	}

found:
	intel_crtc = to_intel_crtc(crtc);

	ret = drm_modeset_lock(&crtc->primary->mutex, ctx);
	if (ret)
		goto fail;

	state = drm_atomic_state_alloc(dev);
	restore_state = drm_atomic_state_alloc(dev);
	if (!state || !restore_state) {
		ret = -ENOMEM;
		goto fail;
	}

	state->acquire_ctx = ctx;
	restore_state->acquire_ctx = ctx;

	connector_state = drm_atomic_get_connector_state(state, connector);
	if (IS_ERR(connector_state)) {
		ret = PTR_ERR(connector_state);
		goto fail;
	}

	ret = drm_atomic_set_crtc_for_connector(connector_state, crtc);
	if (ret)
		goto fail;

	crtc_state = intel_atomic_get_crtc_state(state, intel_crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto fail;
	}

	crtc_state->base.active = crtc_state->base.enable = true;

	if (!mode)
		mode = &load_detect_mode;

	/* We need a framebuffer large enough to accommodate all accesses
	 * that the plane may generate whilst we perform load detection.
	 * We can not rely on the fbcon either being present (we get called
	 * during its initialisation to detect all boot displays, or it may
	 * not even exist) or that it is large enough to satisfy the
	 * requested mode.
	 */
	fb = mode_fits_in_fbdev(dev, mode);
	if (fb == NULL) {
		DRM_DEBUG_KMS("creating tmp fb for load-detection\n");
		fb = intel_framebuffer_create_for_mode(dev, mode, 24, 32);
	} else
		DRM_DEBUG_KMS("reusing fbdev for load-detection framebuffer\n");
	if (IS_ERR(fb)) {
		DRM_DEBUG_KMS("failed to allocate framebuffer for load-detection\n");
		goto fail;
	}

	ret = intel_modeset_setup_plane_state(state, crtc, mode, fb, 0, 0);
	if (ret)
		goto fail;

	drm_framebuffer_unreference(fb);

	ret = drm_atomic_set_mode_for_crtc(&crtc_state->base, mode);
	if (ret)
		goto fail;

	ret = PTR_ERR_OR_ZERO(drm_atomic_get_connector_state(restore_state, connector));
	if (!ret)
		ret = PTR_ERR_OR_ZERO(drm_atomic_get_crtc_state(restore_state, crtc));
	if (!ret)
		ret = PTR_ERR_OR_ZERO(drm_atomic_get_plane_state(restore_state, crtc->primary));
	if (ret) {
		DRM_DEBUG_KMS("Failed to create a copy of old state to restore: %i\n", ret);
		goto fail;
	}

	ret = drm_atomic_commit(state);
	if (ret) {
		DRM_DEBUG_KMS("failed to set mode on load-detect pipe\n");
		goto fail;
	}

	old->restore_state = restore_state;
	drm_atomic_state_put(state);

	/* let the connector get through one full cycle before testing */
	intel_wait_for_vblank(dev_priv, intel_crtc->pipe);
	return true;

fail:
	if (state) {
		drm_atomic_state_put(state);
		state = NULL;
	}
	if (restore_state) {
		drm_atomic_state_put(restore_state);
		restore_state = NULL;
	}

	if (ret == -EDEADLK) {
		drm_modeset_backoff(ctx);
		goto retry;
	}

	return false;
}

void intel_release_load_detect_pipe(struct drm_connector *connector,
				    struct intel_load_detect_pipe *old,
				    struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_encoder *intel_encoder =
		intel_attached_encoder(connector);
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_atomic_state *state = old->restore_state;
	int ret;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
		      connector->base.id, connector->name,
		      encoder->base.id, encoder->name);

	if (!state)
		return;

	ret = drm_atomic_commit(state);
	if (ret)
		DRM_DEBUG_KMS("Couldn't release load detect pipe: %i\n", ret);
	drm_atomic_state_put(state);
}

static int i9xx_pll_refclk(struct drm_device *dev,
			   const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 dpll = pipe_config->dpll_hw_state.dpll;

	if ((dpll & PLL_REF_INPUT_MASK) == PLLB_REF_INPUT_SPREADSPECTRUMIN)
		return dev_priv->vbt.lvds_ssc_freq;
	else if (HAS_PCH_SPLIT(dev_priv))
		return 120000;
	else if (!IS_GEN2(dev_priv))
		return 96000;
	else
		return 48000;
}

/* Returns the clock of the currently programmed mode of the given pipe. */
static void i9xx_crtc_clock_get(struct intel_crtc *crtc,
				struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe = pipe_config->cpu_transcoder;
	u32 dpll = pipe_config->dpll_hw_state.dpll;
	u32 fp;
	struct dpll clock;
	int port_clock;
	int refclk = i9xx_pll_refclk(dev, pipe_config);

	if ((dpll & DISPLAY_RATE_SELECT_FPA1) == 0)
		fp = pipe_config->dpll_hw_state.fp0;
	else
		fp = pipe_config->dpll_hw_state.fp1;

	clock.m1 = (fp & FP_M1_DIV_MASK) >> FP_M1_DIV_SHIFT;
	if (IS_PINEVIEW(dev_priv)) {
		clock.n = ffs((fp & FP_N_PINEVIEW_DIV_MASK) >> FP_N_DIV_SHIFT) - 1;
		clock.m2 = (fp & FP_M2_PINEVIEW_DIV_MASK) >> FP_M2_DIV_SHIFT;
	} else {
		clock.n = (fp & FP_N_DIV_MASK) >> FP_N_DIV_SHIFT;
		clock.m2 = (fp & FP_M2_DIV_MASK) >> FP_M2_DIV_SHIFT;
	}

	if (!IS_GEN2(dev_priv)) {
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
			DRM_DEBUG_KMS("Unknown DPLL mode %08x in programmed "
				  "mode\n", (int)(dpll & DPLL_MODE_MASK));
			return;
		}

		if (IS_PINEVIEW(dev_priv))
			port_clock = pnv_calc_dpll_params(refclk, &clock);
		else
			port_clock = i9xx_calc_dpll_params(refclk, &clock);
	} else {
		u32 lvds = IS_I830(dev_priv) ? 0 : I915_READ(LVDS);
		bool is_lvds = (pipe == 1) && (lvds & LVDS_PORT_EN);

		if (is_lvds) {
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
	pipe_config->port_clock = port_clock;
}

int intel_dotclock_calculate(int link_freq,
			     const struct intel_link_m_n *m_n)
{
	/*
	 * The calculation for the data clock is:
	 * pixel_clock = ((m/n)*(link_clock * nr_lanes))/bpp
	 * But we want to avoid losing precison if possible, so:
	 * pixel_clock = ((m * link_clock * nr_lanes)/(n*bpp))
	 *
	 * and the link clock is simpler:
	 * link_clock = (m * link_clock) / n
	 */

	if (!m_n->link_n)
		return 0;

	return div_u64((u64)m_n->link_m * link_freq, m_n->link_n);
}

static void ironlake_pch_clock_get(struct intel_crtc *crtc,
				   struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	/* read out port_clock from the DPLL */
	i9xx_crtc_clock_get(crtc, pipe_config);

	/*
	 * In case there is an active pipe without active ports,
	 * we may need some idea for the dotclock anyway.
	 * Calculate one based on the FDI configuration.
	 */
	pipe_config->base.adjusted_mode.crtc_clock =
		intel_dotclock_calculate(intel_fdi_link_freq(dev_priv, pipe_config),
					 &pipe_config->fdi_m_n);
}

/** Returns the currently programmed mode of the given pipe. */
struct drm_display_mode *intel_crtc_mode_get(struct drm_device *dev,
					     struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	struct drm_display_mode *mode;
	struct intel_crtc_state *pipe_config;
	int htot = I915_READ(HTOTAL(cpu_transcoder));
	int hsync = I915_READ(HSYNC(cpu_transcoder));
	int vtot = I915_READ(VTOTAL(cpu_transcoder));
	int vsync = I915_READ(VSYNC(cpu_transcoder));
	enum pipe pipe = intel_crtc->pipe;

	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode)
		return NULL;

	pipe_config = kzalloc(sizeof(*pipe_config), GFP_KERNEL);
	if (!pipe_config) {
		kfree(mode);
		return NULL;
	}

	/*
	 * Construct a pipe_config sufficient for getting the clock info
	 * back out of crtc_clock_get.
	 *
	 * Note, if LVDS ever uses a non-1 pixel multiplier, we'll need
	 * to use a real value here instead.
	 */
	pipe_config->cpu_transcoder = (enum transcoder) pipe;
	pipe_config->pixel_multiplier = 1;
	pipe_config->dpll_hw_state.dpll = I915_READ(DPLL(pipe));
	pipe_config->dpll_hw_state.fp0 = I915_READ(FP0(pipe));
	pipe_config->dpll_hw_state.fp1 = I915_READ(FP1(pipe));
	i9xx_crtc_clock_get(intel_crtc, pipe_config);

	mode->clock = pipe_config->port_clock / pipe_config->pixel_multiplier;
	mode->hdisplay = (htot & 0xffff) + 1;
	mode->htotal = ((htot & 0xffff0000) >> 16) + 1;
	mode->hsync_start = (hsync & 0xffff) + 1;
	mode->hsync_end = ((hsync & 0xffff0000) >> 16) + 1;
	mode->vdisplay = (vtot & 0xffff) + 1;
	mode->vtotal = ((vtot & 0xffff0000) >> 16) + 1;
	mode->vsync_start = (vsync & 0xffff) + 1;
	mode->vsync_end = ((vsync & 0xffff0000) >> 16) + 1;

	drm_mode_set_name(mode);

	kfree(pipe_config);

	return mode;
}

static void intel_crtc_destroy(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct intel_flip_work *work;

	spin_lock_irq(&dev->event_lock);
	work = intel_crtc->flip_work;
	intel_crtc->flip_work = NULL;
	spin_unlock_irq(&dev->event_lock);

	if (work) {
		cancel_work_sync(&work->mmio_work);
		cancel_work_sync(&work->unpin_work);
		kfree(work);
	}

	drm_crtc_cleanup(crtc);

	kfree(intel_crtc);
}

static void intel_unpin_work_fn(struct work_struct *__work)
{
	struct intel_flip_work *work =
		container_of(__work, struct intel_flip_work, unpin_work);
	struct intel_crtc *crtc = to_intel_crtc(work->crtc);
	struct drm_device *dev = crtc->base.dev;
	struct drm_plane *primary = crtc->base.primary;

	if (is_mmio_work(work))
		flush_work(&work->mmio_work);

	mutex_lock(&dev->struct_mutex);
	intel_unpin_fb_vma(work->old_vma);
	i915_gem_object_put(work->pending_flip_obj);
	mutex_unlock(&dev->struct_mutex);

	i915_gem_request_put(work->flip_queued_req);

	intel_frontbuffer_flip_complete(to_i915(dev),
					to_intel_plane(primary)->frontbuffer_bit);
	intel_fbc_post_update(crtc);
	drm_framebuffer_unreference(work->old_fb);

	BUG_ON(atomic_read(&crtc->unpin_work_count) == 0);
	atomic_dec(&crtc->unpin_work_count);

	kfree(work);
}

/* Is 'a' after or equal to 'b'? */
static bool g4x_flip_count_after_eq(u32 a, u32 b)
{
	return !((a - b) & 0x80000000);
}

static bool __pageflip_finished_cs(struct intel_crtc *crtc,
				   struct intel_flip_work *work)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (abort_flip_on_reset(crtc))
		return true;

	/*
	 * The relevant registers doen't exist on pre-ctg.
	 * As the flip done interrupt doesn't trigger for mmio
	 * flips on gmch platforms, a flip count check isn't
	 * really needed there. But since ctg has the registers,
	 * include it in the check anyway.
	 */
	if (INTEL_GEN(dev_priv) < 5 && !IS_G4X(dev_priv))
		return true;

	/*
	 * BDW signals flip done immediately if the plane
	 * is disabled, even if the plane enable is already
	 * armed to occur at the next vblank :(
	 */

	/*
	 * A DSPSURFLIVE check isn't enough in case the mmio and CS flips
	 * used the same base address. In that case the mmio flip might
	 * have completed, but the CS hasn't even executed the flip yet.
	 *
	 * A flip count check isn't enough as the CS might have updated
	 * the base address just after start of vblank, but before we
	 * managed to process the interrupt. This means we'd complete the
	 * CS flip too soon.
	 *
	 * Combining both checks should get us a good enough result. It may
	 * still happen that the CS flip has been executed, but has not
	 * yet actually completed. But in case the base address is the same
	 * anyway, we don't really care.
	 */
	return (I915_READ(DSPSURFLIVE(crtc->plane)) & ~0xfff) ==
		crtc->flip_work->gtt_offset &&
		g4x_flip_count_after_eq(I915_READ(PIPE_FLIPCOUNT_G4X(crtc->pipe)),
				    crtc->flip_work->flip_count);
}

static bool
__pageflip_finished_mmio(struct intel_crtc *crtc,
			       struct intel_flip_work *work)
{
	/*
	 * MMIO work completes when vblank is different from
	 * flip_queued_vblank.
	 *
	 * Reset counter value doesn't matter, this is handled by
	 * i915_wait_request finishing early, so no need to handle
	 * reset here.
	 */
	return intel_crtc_get_vblank_counter(crtc) != work->flip_queued_vblank;
}


static bool pageflip_finished(struct intel_crtc *crtc,
			      struct intel_flip_work *work)
{
	if (!atomic_read(&work->pending))
		return false;

	smp_rmb();

	if (is_mmio_work(work))
		return __pageflip_finished_mmio(crtc, work);
	else
		return __pageflip_finished_cs(crtc, work);
}

void intel_finish_page_flip_cs(struct drm_i915_private *dev_priv, int pipe)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv, pipe);
	struct intel_flip_work *work;
	unsigned long flags;

	/* Ignore early vblank irqs */
	if (!crtc)
		return;

	/*
	 * This is called both by irq handlers and the reset code (to complete
	 * lost pageflips) so needs the full irqsave spinlocks.
	 */
	spin_lock_irqsave(&dev->event_lock, flags);
	work = crtc->flip_work;

	if (work != NULL &&
	    !is_mmio_work(work) &&
	    pageflip_finished(crtc, work))
		page_flip_completed(crtc);

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

void intel_finish_page_flip_mmio(struct drm_i915_private *dev_priv, int pipe)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv, pipe);
	struct intel_flip_work *work;
	unsigned long flags;

	/* Ignore early vblank irqs */
	if (!crtc)
		return;

	/*
	 * This is called both by irq handlers and the reset code (to complete
	 * lost pageflips) so needs the full irqsave spinlocks.
	 */
	spin_lock_irqsave(&dev->event_lock, flags);
	work = crtc->flip_work;

	if (work != NULL &&
	    is_mmio_work(work) &&
	    pageflip_finished(crtc, work))
		page_flip_completed(crtc);

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static inline void intel_mark_page_flip_active(struct intel_crtc *crtc,
					       struct intel_flip_work *work)
{
	work->flip_queued_vblank = intel_crtc_get_vblank_counter(crtc);

	/* Ensure that the work item is consistent when activating it ... */
	smp_mb__before_atomic();
	atomic_set(&work->pending, 1);
}

static int intel_gen2_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct drm_i915_gem_request *req,
				 uint32_t flags)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 flip_mask, *cs;

	cs = intel_ring_begin(req, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* Can't queue multiple flips, so wait for the previous
	 * one to finish before executing the next.
	 */
	if (intel_crtc->plane)
		flip_mask = MI_WAIT_FOR_PLANE_B_FLIP;
	else
		flip_mask = MI_WAIT_FOR_PLANE_A_FLIP;
	*cs++ = MI_WAIT_FOR_EVENT | flip_mask;
	*cs++ = MI_NOOP;
	*cs++ = MI_DISPLAY_FLIP | MI_DISPLAY_FLIP_PLANE(intel_crtc->plane);
	*cs++ = fb->pitches[0];
	*cs++ = intel_crtc->flip_work->gtt_offset;
	*cs++ = 0; /* aux display base address, unused */

	return 0;
}

static int intel_gen3_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct drm_i915_gem_request *req,
				 uint32_t flags)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 flip_mask, *cs;

	cs = intel_ring_begin(req, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	if (intel_crtc->plane)
		flip_mask = MI_WAIT_FOR_PLANE_B_FLIP;
	else
		flip_mask = MI_WAIT_FOR_PLANE_A_FLIP;
	*cs++ = MI_WAIT_FOR_EVENT | flip_mask;
	*cs++ = MI_NOOP;
	*cs++ = MI_DISPLAY_FLIP_I915 | MI_DISPLAY_FLIP_PLANE(intel_crtc->plane);
	*cs++ = fb->pitches[0];
	*cs++ = intel_crtc->flip_work->gtt_offset;
	*cs++ = MI_NOOP;

	return 0;
}

static int intel_gen4_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct drm_i915_gem_request *req,
				 uint32_t flags)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 pf, pipesrc, *cs;

	cs = intel_ring_begin(req, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* i965+ uses the linear or tiled offsets from the
	 * Display Registers (which do not change across a page-flip)
	 * so we need only reprogram the base address.
	 */
	*cs++ = MI_DISPLAY_FLIP | MI_DISPLAY_FLIP_PLANE(intel_crtc->plane);
	*cs++ = fb->pitches[0];
	*cs++ = intel_crtc->flip_work->gtt_offset |
		intel_fb_modifier_to_tiling(fb->modifier);

	/* XXX Enabling the panel-fitter across page-flip is so far
	 * untested on non-native modes, so ignore it for now.
	 * pf = I915_READ(pipe == 0 ? PFA_CTL_1 : PFB_CTL_1) & PF_ENABLE;
	 */
	pf = 0;
	pipesrc = I915_READ(PIPESRC(intel_crtc->pipe)) & 0x0fff0fff;
	*cs++ = pf | pipesrc;

	return 0;
}

static int intel_gen6_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct drm_i915_gem_request *req,
				 uint32_t flags)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 pf, pipesrc, *cs;

	cs = intel_ring_begin(req, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_DISPLAY_FLIP | MI_DISPLAY_FLIP_PLANE(intel_crtc->plane);
	*cs++ = fb->pitches[0] | intel_fb_modifier_to_tiling(fb->modifier);
	*cs++ = intel_crtc->flip_work->gtt_offset;

	/* Contrary to the suggestions in the documentation,
	 * "Enable Panel Fitter" does not seem to be required when page
	 * flipping with a non-native mode, and worse causes a normal
	 * modeset to fail.
	 * pf = I915_READ(PF_CTL(intel_crtc->pipe)) & PF_ENABLE;
	 */
	pf = 0;
	pipesrc = I915_READ(PIPESRC(intel_crtc->pipe)) & 0x0fff0fff;
	*cs++ = pf | pipesrc;

	return 0;
}

static int intel_gen7_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct drm_i915_gem_request *req,
				 uint32_t flags)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 *cs, plane_bit = 0;
	int len, ret;

	switch (intel_crtc->plane) {
	case PLANE_A:
		plane_bit = MI_DISPLAY_FLIP_IVB_PLANE_A;
		break;
	case PLANE_B:
		plane_bit = MI_DISPLAY_FLIP_IVB_PLANE_B;
		break;
	case PLANE_C:
		plane_bit = MI_DISPLAY_FLIP_IVB_PLANE_C;
		break;
	default:
		WARN_ONCE(1, "unknown plane in flip command\n");
		return -ENODEV;
	}

	len = 4;
	if (req->engine->id == RCS) {
		len += 6;
		/*
		 * On Gen 8, SRM is now taking an extra dword to accommodate
		 * 48bits addresses, and we need a NOOP for the batch size to
		 * stay even.
		 */
		if (IS_GEN8(dev_priv))
			len += 2;
	}

	/*
	 * BSpec MI_DISPLAY_FLIP for IVB:
	 * "The full packet must be contained within the same cache line."
	 *
	 * Currently the LRI+SRM+MI_DISPLAY_FLIP all fit within the same
	 * cacheline, if we ever start emitting more commands before
	 * the MI_DISPLAY_FLIP we may need to first emit everything else,
	 * then do the cacheline alignment, and finally emit the
	 * MI_DISPLAY_FLIP.
	 */
	ret = intel_ring_cacheline_align(req);
	if (ret)
		return ret;

	cs = intel_ring_begin(req, len);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* Unmask the flip-done completion message. Note that the bspec says that
	 * we should do this for both the BCS and RCS, and that we must not unmask
	 * more than one flip event at any time (or ensure that one flip message
	 * can be sent by waiting for flip-done prior to queueing new flips).
	 * Experimentation says that BCS works despite DERRMR masking all
	 * flip-done completion events and that unmasking all planes at once
	 * for the RCS also doesn't appear to drop events. Setting the DERRMR
	 * to zero does lead to lockups within MI_DISPLAY_FLIP.
	 */
	if (req->engine->id == RCS) {
		*cs++ = MI_LOAD_REGISTER_IMM(1);
		*cs++ = i915_mmio_reg_offset(DERRMR);
		*cs++ = ~(DERRMR_PIPEA_PRI_FLIP_DONE |
			  DERRMR_PIPEB_PRI_FLIP_DONE |
			  DERRMR_PIPEC_PRI_FLIP_DONE);
		if (IS_GEN8(dev_priv))
			*cs++ = MI_STORE_REGISTER_MEM_GEN8 |
				MI_SRM_LRM_GLOBAL_GTT;
		else
			*cs++ = MI_STORE_REGISTER_MEM | MI_SRM_LRM_GLOBAL_GTT;
		*cs++ = i915_mmio_reg_offset(DERRMR);
		*cs++ = i915_ggtt_offset(req->engine->scratch) + 256;
		if (IS_GEN8(dev_priv)) {
			*cs++ = 0;
			*cs++ = MI_NOOP;
		}
	}

	*cs++ = MI_DISPLAY_FLIP_I915 | plane_bit;
	*cs++ = fb->pitches[0] | intel_fb_modifier_to_tiling(fb->modifier);
	*cs++ = intel_crtc->flip_work->gtt_offset;
	*cs++ = MI_NOOP;

	return 0;
}

static bool use_mmio_flip(struct intel_engine_cs *engine,
			  struct drm_i915_gem_object *obj)
{
	/*
	 * This is not being used for older platforms, because
	 * non-availability of flip done interrupt forces us to use
	 * CS flips. Older platforms derive flip done using some clever
	 * tricks involving the flip_pending status bits and vblank irqs.
	 * So using MMIO flips there would disrupt this mechanism.
	 */

	if (engine == NULL)
		return true;

	if (INTEL_GEN(engine->i915) < 5)
		return false;

	if (i915.use_mmio_flip < 0)
		return false;
	else if (i915.use_mmio_flip > 0)
		return true;
	else if (i915.enable_execlists)
		return true;

	return engine != i915_gem_object_last_write_engine(obj);
}

static void skl_do_mmio_flip(struct intel_crtc *intel_crtc,
			     unsigned int rotation,
			     struct intel_flip_work *work)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_framebuffer *fb = intel_crtc->base.primary->fb;
	const enum pipe pipe = intel_crtc->pipe;
	u32 ctl, stride = skl_plane_stride(fb, 0, rotation);

	ctl = I915_READ(PLANE_CTL(pipe, 0));
	ctl &= ~PLANE_CTL_TILED_MASK;
	switch (fb->modifier) {
	case DRM_FORMAT_MOD_NONE:
		break;
	case I915_FORMAT_MOD_X_TILED:
		ctl |= PLANE_CTL_TILED_X;
		break;
	case I915_FORMAT_MOD_Y_TILED:
		ctl |= PLANE_CTL_TILED_Y;
		break;
	case I915_FORMAT_MOD_Yf_TILED:
		ctl |= PLANE_CTL_TILED_YF;
		break;
	default:
		MISSING_CASE(fb->modifier);
	}

	/*
	 * Both PLANE_CTL and PLANE_STRIDE are not updated on vblank but on
	 * PLANE_SURF updates, the update is then guaranteed to be atomic.
	 */
	I915_WRITE(PLANE_CTL(pipe, 0), ctl);
	I915_WRITE(PLANE_STRIDE(pipe, 0), stride);

	I915_WRITE(PLANE_SURF(pipe, 0), work->gtt_offset);
	POSTING_READ(PLANE_SURF(pipe, 0));
}

static void ilk_do_mmio_flip(struct intel_crtc *intel_crtc,
			     struct intel_flip_work *work)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_framebuffer *fb = intel_crtc->base.primary->fb;
	i915_reg_t reg = DSPCNTR(intel_crtc->plane);
	u32 dspcntr;

	dspcntr = I915_READ(reg);

	if (fb->modifier == I915_FORMAT_MOD_X_TILED)
		dspcntr |= DISPPLANE_TILED;
	else
		dspcntr &= ~DISPPLANE_TILED;

	I915_WRITE(reg, dspcntr);

	I915_WRITE(DSPSURF(intel_crtc->plane), work->gtt_offset);
	POSTING_READ(DSPSURF(intel_crtc->plane));
}

static void intel_mmio_flip_work_func(struct work_struct *w)
{
	struct intel_flip_work *work =
		container_of(w, struct intel_flip_work, mmio_work);
	struct intel_crtc *crtc = to_intel_crtc(work->crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_framebuffer *intel_fb =
		to_intel_framebuffer(crtc->base.primary->fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;

	WARN_ON(i915_gem_object_wait(obj, 0, MAX_SCHEDULE_TIMEOUT, NULL) < 0);

	intel_pipe_update_start(crtc);

	if (INTEL_GEN(dev_priv) >= 9)
		skl_do_mmio_flip(crtc, work->rotation, work);
	else
		/* use_mmio_flip() retricts MMIO flips to ilk+ */
		ilk_do_mmio_flip(crtc, work);

	intel_pipe_update_end(crtc, work);
}

static int intel_default_queue_flip(struct drm_device *dev,
				    struct drm_crtc *crtc,
				    struct drm_framebuffer *fb,
				    struct drm_i915_gem_object *obj,
				    struct drm_i915_gem_request *req,
				    uint32_t flags)
{
	return -ENODEV;
}

static bool __pageflip_stall_check_cs(struct drm_i915_private *dev_priv,
				      struct intel_crtc *intel_crtc,
				      struct intel_flip_work *work)
{
	u32 addr, vblank;

	if (!atomic_read(&work->pending))
		return false;

	smp_rmb();

	vblank = intel_crtc_get_vblank_counter(intel_crtc);
	if (work->flip_ready_vblank == 0) {
		if (work->flip_queued_req &&
		    !i915_gem_request_completed(work->flip_queued_req))
			return false;

		work->flip_ready_vblank = vblank;
	}

	if (vblank - work->flip_ready_vblank < 3)
		return false;

	/* Potential stall - if we see that the flip has happened,
	 * assume a missed interrupt. */
	if (INTEL_GEN(dev_priv) >= 4)
		addr = I915_HI_DISPBASE(I915_READ(DSPSURF(intel_crtc->plane)));
	else
		addr = I915_READ(DSPADDR(intel_crtc->plane));

	/* There is a potential issue here with a false positive after a flip
	 * to the same address. We could address this by checking for a
	 * non-incrementing frame counter.
	 */
	return addr == work->gtt_offset;
}

void intel_check_page_flip(struct drm_i915_private *dev_priv, int pipe)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv, pipe);
	struct intel_flip_work *work;

	WARN_ON(!in_interrupt());

	if (crtc == NULL)
		return;

	spin_lock(&dev->event_lock);
	work = crtc->flip_work;

	if (work != NULL && !is_mmio_work(work) &&
	    __pageflip_stall_check_cs(dev_priv, crtc, work)) {
		WARN_ONCE(1,
			  "Kicking stuck page flip: queued at %d, now %d\n",
			work->flip_queued_vblank, intel_crtc_get_vblank_counter(crtc));
		page_flip_completed(crtc);
		work = NULL;
	}

	if (work != NULL && !is_mmio_work(work) &&
	    intel_crtc_get_vblank_counter(crtc) - work->flip_queued_vblank > 1)
		intel_queue_rps_boost_for_request(work->flip_queued_req);
	spin_unlock(&dev->event_lock);
}

__maybe_unused
static int intel_crtc_page_flip(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				struct drm_pending_vblank_event *event,
				uint32_t page_flip_flags)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_framebuffer *old_fb = crtc->primary->fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_plane *primary = crtc->primary;
	enum pipe pipe = intel_crtc->pipe;
	struct intel_flip_work *work;
	struct intel_engine_cs *engine;
	bool mmio_flip;
	struct drm_i915_gem_request *request;
	struct i915_vma *vma;
	int ret;

	/*
	 * drm_mode_page_flip_ioctl() should already catch this, but double
	 * check to be safe.  In the future we may enable pageflipping from
	 * a disabled primary plane.
	 */
	if (WARN_ON(intel_fb_obj(old_fb) == NULL))
		return -EBUSY;

	/* Can't change pixel format via MI display flips. */
	if (fb->format != crtc->primary->fb->format)
		return -EINVAL;

	/*
	 * TILEOFF/LINOFF registers can't be changed via MI display flips.
	 * Note that pitch changes could also affect these register.
	 */
	if (INTEL_GEN(dev_priv) > 3 &&
	    (fb->offsets[0] != crtc->primary->fb->offsets[0] ||
	     fb->pitches[0] != crtc->primary->fb->pitches[0]))
		return -EINVAL;

	if (i915_terminally_wedged(&dev_priv->gpu_error))
		goto out_hang;

	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (work == NULL)
		return -ENOMEM;

	work->event = event;
	work->crtc = crtc;
	work->old_fb = old_fb;
	INIT_WORK(&work->unpin_work, intel_unpin_work_fn);

	ret = drm_crtc_vblank_get(crtc);
	if (ret)
		goto free_work;

	/* We borrow the event spin lock for protecting flip_work */
	spin_lock_irq(&dev->event_lock);
	if (intel_crtc->flip_work) {
		/* Before declaring the flip queue wedged, check if
		 * the hardware completed the operation behind our backs.
		 */
		if (pageflip_finished(intel_crtc, intel_crtc->flip_work)) {
			DRM_DEBUG_DRIVER("flip queue: previous flip completed, continuing\n");
			page_flip_completed(intel_crtc);
		} else {
			DRM_DEBUG_DRIVER("flip queue: crtc already busy\n");
			spin_unlock_irq(&dev->event_lock);

			drm_crtc_vblank_put(crtc);
			kfree(work);
			return -EBUSY;
		}
	}
	intel_crtc->flip_work = work;
	spin_unlock_irq(&dev->event_lock);

	if (atomic_read(&intel_crtc->unpin_work_count) >= 2)
		flush_workqueue(dev_priv->wq);

	/* Reference the objects for the scheduled work. */
	drm_framebuffer_reference(work->old_fb);

	crtc->primary->fb = fb;
	update_state_fb(crtc->primary);

	work->pending_flip_obj = i915_gem_object_get(obj);

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto cleanup;

	intel_crtc->reset_count = i915_reset_count(&dev_priv->gpu_error);
	if (i915_reset_in_progress_or_wedged(&dev_priv->gpu_error)) {
		ret = -EIO;
		goto unlock;
	}

	atomic_inc(&intel_crtc->unpin_work_count);

	if (INTEL_GEN(dev_priv) >= 5 || IS_G4X(dev_priv))
		work->flip_count = I915_READ(PIPE_FLIPCOUNT_G4X(pipe)) + 1;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		engine = dev_priv->engine[BCS];
		if (fb->modifier != old_fb->modifier)
			/* vlv: DISPLAY_FLIP fails to change tiling */
			engine = NULL;
	} else if (IS_IVYBRIDGE(dev_priv) || IS_HASWELL(dev_priv)) {
		engine = dev_priv->engine[BCS];
	} else if (INTEL_GEN(dev_priv) >= 7) {
		engine = i915_gem_object_last_write_engine(obj);
		if (engine == NULL || engine->id != RCS)
			engine = dev_priv->engine[BCS];
	} else {
		engine = dev_priv->engine[RCS];
	}

	mmio_flip = use_mmio_flip(engine, obj);

	vma = intel_pin_and_fence_fb_obj(fb, primary->state->rotation);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto cleanup_pending;
	}

	work->old_vma = to_intel_plane_state(primary->state)->vma;
	to_intel_plane_state(primary->state)->vma = vma;

	work->gtt_offset = i915_ggtt_offset(vma) + intel_crtc->dspaddr_offset;
	work->rotation = crtc->primary->state->rotation;

	/*
	 * There's the potential that the next frame will not be compatible with
	 * FBC, so we want to call pre_update() before the actual page flip.
	 * The problem is that pre_update() caches some information about the fb
	 * object, so we want to do this only after the object is pinned. Let's
	 * be on the safe side and do this immediately before scheduling the
	 * flip.
	 */
	intel_fbc_pre_update(intel_crtc, intel_crtc->config,
			     to_intel_plane_state(primary->state));

	if (mmio_flip) {
		INIT_WORK(&work->mmio_work, intel_mmio_flip_work_func);
		queue_work(system_unbound_wq, &work->mmio_work);
	} else {
		request = i915_gem_request_alloc(engine,
						 dev_priv->kernel_context);
		if (IS_ERR(request)) {
			ret = PTR_ERR(request);
			goto cleanup_unpin;
		}

		ret = i915_gem_request_await_object(request, obj, false);
		if (ret)
			goto cleanup_request;

		ret = dev_priv->display.queue_flip(dev, crtc, fb, obj, request,
						   page_flip_flags);
		if (ret)
			goto cleanup_request;

		intel_mark_page_flip_active(intel_crtc, work);

		work->flip_queued_req = i915_gem_request_get(request);
		i915_add_request_no_flush(request);
	}

	i915_gem_object_wait_priority(obj, 0, I915_PRIORITY_DISPLAY);
	i915_gem_track_fb(intel_fb_obj(old_fb), obj,
			  to_intel_plane(primary)->frontbuffer_bit);
	mutex_unlock(&dev->struct_mutex);

	intel_frontbuffer_flip_prepare(to_i915(dev),
				       to_intel_plane(primary)->frontbuffer_bit);

	trace_i915_flip_request(intel_crtc->plane, obj);

	return 0;

cleanup_request:
	i915_add_request_no_flush(request);
cleanup_unpin:
	to_intel_plane_state(primary->state)->vma = work->old_vma;
	intel_unpin_fb_vma(vma);
cleanup_pending:
	atomic_dec(&intel_crtc->unpin_work_count);
unlock:
	mutex_unlock(&dev->struct_mutex);
cleanup:
	crtc->primary->fb = old_fb;
	update_state_fb(crtc->primary);

	i915_gem_object_put(obj);
	drm_framebuffer_unreference(work->old_fb);

	spin_lock_irq(&dev->event_lock);
	intel_crtc->flip_work = NULL;
	spin_unlock_irq(&dev->event_lock);

	drm_crtc_vblank_put(crtc);
free_work:
	kfree(work);

	if (ret == -EIO) {
		struct drm_atomic_state *state;
		struct drm_plane_state *plane_state;

out_hang:
		state = drm_atomic_state_alloc(dev);
		if (!state)
			return -ENOMEM;
		state->acquire_ctx = drm_modeset_legacy_acquire_ctx(crtc);

retry:
		plane_state = drm_atomic_get_plane_state(state, primary);
		ret = PTR_ERR_OR_ZERO(plane_state);
		if (!ret) {
			drm_atomic_set_fb_for_plane(plane_state, fb);

			ret = drm_atomic_set_crtc_for_plane(plane_state, crtc);
			if (!ret)
				ret = drm_atomic_commit(state);
		}

		if (ret == -EDEADLK) {
			drm_modeset_backoff(state->acquire_ctx);
			drm_atomic_state_clear(state);
			goto retry;
		}

		drm_atomic_state_put(state);

		if (ret == 0 && event) {
			spin_lock_irq(&dev->event_lock);
			drm_crtc_send_vblank_event(crtc, event);
			spin_unlock_irq(&dev->event_lock);
		}
	}
	return ret;
}


/**
 * intel_wm_need_update - Check whether watermarks need updating
 * @plane: drm plane
 * @state: new plane state
 *
 * Check current plane state versus the new one to determine whether
 * watermarks need to be recalculated.
 *
 * Returns true or false.
 */
static bool intel_wm_need_update(struct drm_plane *plane,
				 struct drm_plane_state *state)
{
	struct intel_plane_state *new = to_intel_plane_state(state);
	struct intel_plane_state *cur = to_intel_plane_state(plane->state);

	/* Update watermarks on tiling or size changes. */
	if (new->base.visible != cur->base.visible)
		return true;

	if (!cur->base.fb || !new->base.fb)
		return false;

	if (cur->base.fb->modifier != new->base.fb->modifier ||
	    cur->base.rotation != new->base.rotation ||
	    drm_rect_width(&new->base.src) != drm_rect_width(&cur->base.src) ||
	    drm_rect_height(&new->base.src) != drm_rect_height(&cur->base.src) ||
	    drm_rect_width(&new->base.dst) != drm_rect_width(&cur->base.dst) ||
	    drm_rect_height(&new->base.dst) != drm_rect_height(&cur->base.dst))
		return true;

	return false;
}

static bool needs_scaling(struct intel_plane_state *state)
{
	int src_w = drm_rect_width(&state->base.src) >> 16;
	int src_h = drm_rect_height(&state->base.src) >> 16;
	int dst_w = drm_rect_width(&state->base.dst);
	int dst_h = drm_rect_height(&state->base.dst);

	return (src_w != dst_w || src_h != dst_h);
}

int intel_plane_atomic_calc_changes(struct drm_crtc_state *crtc_state,
				    struct drm_plane_state *plane_state)
{
	struct intel_crtc_state *pipe_config = to_intel_crtc_state(crtc_state);
	struct drm_crtc *crtc = crtc_state->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_plane *plane = plane_state->plane;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane_state *old_plane_state =
		to_intel_plane_state(plane->state);
	bool mode_changed = needs_modeset(crtc_state);
	bool was_crtc_enabled = crtc->state->active;
	bool is_crtc_enabled = crtc_state->active;
	bool turn_off, turn_on, visible, was_visible;
	struct drm_framebuffer *fb = plane_state->fb;
	int ret;

	if (INTEL_GEN(dev_priv) >= 9 && plane->type != DRM_PLANE_TYPE_CURSOR) {
		ret = skl_update_scaler_plane(
			to_intel_crtc_state(crtc_state),
			to_intel_plane_state(plane_state));
		if (ret)
			return ret;
	}

	was_visible = old_plane_state->base.visible;
	visible = plane_state->visible;

	if (!was_crtc_enabled && WARN_ON(was_visible))
		was_visible = false;

	/*
	 * Visibility is calculated as if the crtc was on, but
	 * after scaler setup everything depends on it being off
	 * when the crtc isn't active.
	 *
	 * FIXME this is wrong for watermarks. Watermarks should also
	 * be computed as if the pipe would be active. Perhaps move
	 * per-plane wm computation to the .check_plane() hook, and
	 * only combine the results from all planes in the current place?
	 */
	if (!is_crtc_enabled)
		plane_state->visible = visible = false;

	if (!was_visible && !visible)
		return 0;

	if (fb != old_plane_state->base.fb)
		pipe_config->fb_changed = true;

	turn_off = was_visible && (!visible || mode_changed);
	turn_on = visible && (!was_visible || mode_changed);

	DRM_DEBUG_ATOMIC("[CRTC:%d:%s] has [PLANE:%d:%s] with fb %i\n",
			 intel_crtc->base.base.id,
			 intel_crtc->base.name,
			 plane->base.id, plane->name,
			 fb ? fb->base.id : -1);

	DRM_DEBUG_ATOMIC("[PLANE:%d:%s] visible %i -> %i, off %i, on %i, ms %i\n",
			 plane->base.id, plane->name,
			 was_visible, visible,
			 turn_off, turn_on, mode_changed);

	if (turn_on) {
		pipe_config->update_wm_pre = true;

		/* must disable cxsr around plane enable/disable */
		if (plane->type != DRM_PLANE_TYPE_CURSOR)
			pipe_config->disable_cxsr = true;
	} else if (turn_off) {
		pipe_config->update_wm_post = true;

		/* must disable cxsr around plane enable/disable */
		if (plane->type != DRM_PLANE_TYPE_CURSOR)
			pipe_config->disable_cxsr = true;
	} else if (intel_wm_need_update(plane, plane_state)) {
		/* FIXME bollocks */
		pipe_config->update_wm_pre = true;
		pipe_config->update_wm_post = true;
	}

	/* Pre-gen9 platforms need two-step watermark updates */
	if ((pipe_config->update_wm_pre || pipe_config->update_wm_post) &&
	    INTEL_GEN(dev_priv) < 9 && dev_priv->display.optimize_watermarks)
		to_intel_crtc_state(crtc_state)->wm.need_postvbl_update = true;

	if (visible || was_visible)
		pipe_config->fb_bits |= to_intel_plane(plane)->frontbuffer_bit;

	/*
	 * WaCxSRDisabledForSpriteScaling:ivb
	 *
	 * cstate->update_wm was already set above, so this flag will
	 * take effect when we commit and program watermarks.
	 */
	if (plane->type == DRM_PLANE_TYPE_OVERLAY && IS_IVYBRIDGE(dev_priv) &&
	    needs_scaling(to_intel_plane_state(plane_state)) &&
	    !needs_scaling(old_plane_state))
		pipe_config->disable_lp_wm = true;

	return 0;
}

static bool encoders_cloneable(const struct intel_encoder *a,
			       const struct intel_encoder *b)
{
	/* masks could be asymmetric, so check both ways */
	return a == b || (a->cloneable & (1 << b->type) &&
			  b->cloneable & (1 << a->type));
}

static bool check_single_encoder_cloning(struct drm_atomic_state *state,
					 struct intel_crtc *crtc,
					 struct intel_encoder *encoder)
{
	struct intel_encoder *source_encoder;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int i;

	for_each_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc != &crtc->base)
			continue;

		source_encoder =
			to_intel_encoder(connector_state->best_encoder);
		if (!encoders_cloneable(encoder, source_encoder))
			return false;
	}

	return true;
}

static int intel_crtc_atomic_check(struct drm_crtc *crtc,
				   struct drm_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_crtc_state *pipe_config =
		to_intel_crtc_state(crtc_state);
	struct drm_atomic_state *state = crtc_state->state;
	int ret;
	bool mode_changed = needs_modeset(crtc_state);

	if (mode_changed && !crtc_state->active)
		pipe_config->update_wm_post = true;

	if (mode_changed && crtc_state->enable &&
	    dev_priv->display.crtc_compute_clock &&
	    !WARN_ON(pipe_config->shared_dpll)) {
		ret = dev_priv->display.crtc_compute_clock(intel_crtc,
							   pipe_config);
		if (ret)
			return ret;
	}

	if (crtc_state->color_mgmt_changed) {
		ret = intel_color_check(crtc, crtc_state);
		if (ret)
			return ret;

		/*
		 * Changing color management on Intel hardware is
		 * handled as part of planes update.
		 */
		crtc_state->planes_changed = true;
	}

	ret = 0;
	if (dev_priv->display.compute_pipe_wm) {
		ret = dev_priv->display.compute_pipe_wm(pipe_config);
		if (ret) {
			DRM_DEBUG_KMS("Target pipe watermarks are invalid\n");
			return ret;
		}
	}

	if (dev_priv->display.compute_intermediate_wm &&
	    !to_intel_atomic_state(state)->skip_intermediate_wm) {
		if (WARN_ON(!dev_priv->display.compute_pipe_wm))
			return 0;

		/*
		 * Calculate 'intermediate' watermarks that satisfy both the
		 * old state and the new state.  We can program these
		 * immediately.
		 */
		ret = dev_priv->display.compute_intermediate_wm(dev,
								intel_crtc,
								pipe_config);
		if (ret) {
			DRM_DEBUG_KMS("No valid intermediate pipe watermarks are possible\n");
			return ret;
		}
	} else if (dev_priv->display.compute_intermediate_wm) {
		if (HAS_PCH_SPLIT(dev_priv) && INTEL_GEN(dev_priv) < 9)
			pipe_config->wm.ilk.intermediate = pipe_config->wm.ilk.optimal;
	}

	if (INTEL_GEN(dev_priv) >= 9) {
		if (mode_changed)
			ret = skl_update_scaler_crtc(pipe_config);

		if (!ret)
			ret = intel_atomic_setup_scalers(dev_priv, intel_crtc,
							 pipe_config);
	}

	return ret;
}

static const struct drm_crtc_helper_funcs intel_helper_funcs = {
	.mode_set_base_atomic = intel_pipe_set_base_atomic,
	.atomic_begin = intel_begin_crtc_commit,
	.atomic_flush = intel_finish_crtc_commit,
	.atomic_check = intel_crtc_atomic_check,
};

static void intel_modeset_update_connector_atomic_state(struct drm_device *dev)
{
	struct intel_connector *connector;

	for_each_intel_connector(dev, connector) {
		if (connector->base.state->crtc)
			drm_connector_unreference(&connector->base);

		if (connector->base.encoder) {
			connector->base.state->best_encoder =
				connector->base.encoder;
			connector->base.state->crtc =
				connector->base.encoder->crtc;

			drm_connector_reference(&connector->base);
		} else {
			connector->base.state->best_encoder = NULL;
			connector->base.state->crtc = NULL;
		}
	}
}

static void
connected_sink_compute_bpp(struct intel_connector *connector,
			   struct intel_crtc_state *pipe_config)
{
	const struct drm_display_info *info = &connector->base.display_info;
	int bpp = pipe_config->pipe_bpp;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] checking for sink bpp constrains\n",
		      connector->base.base.id,
		      connector->base.name);

	/* Don't use an invalid EDID bpc value */
	if (info->bpc != 0 && info->bpc * 3 < bpp) {
		DRM_DEBUG_KMS("clamping display bpp (was %d) to EDID reported max of %d\n",
			      bpp, info->bpc * 3);
		pipe_config->pipe_bpp = info->bpc * 3;
	}

	/* Clamp bpp to 8 on screens without EDID 1.4 */
	if (info->bpc == 0 && bpp > 24) {
		DRM_DEBUG_KMS("clamping display bpp (was %d) to default limit of 24\n",
			      bpp);
		pipe_config->pipe_bpp = 24;
	}
}

static int
compute_baseline_pipe_bpp(struct intel_crtc *crtc,
			  struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct drm_atomic_state *state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int bpp, i;

	if ((IS_G4X(dev_priv) || IS_VALLEYVIEW(dev_priv) ||
	    IS_CHERRYVIEW(dev_priv)))
		bpp = 10*3;
	else if (INTEL_GEN(dev_priv) >= 5)
		bpp = 12*3;
	else
		bpp = 8*3;


	pipe_config->pipe_bpp = bpp;

	state = pipe_config->base.state;

	/* Clamp display bpp to EDID value */
	for_each_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc != &crtc->base)
			continue;

		connected_sink_compute_bpp(to_intel_connector(connector),
					   pipe_config);
	}

	return bpp;
}

static void intel_dump_crtc_timings(const struct drm_display_mode *mode)
{
	DRM_DEBUG_KMS("crtc timings: %d %d %d %d %d %d %d %d %d, "
			"type: 0x%x flags: 0x%x\n",
		mode->crtc_clock,
		mode->crtc_hdisplay, mode->crtc_hsync_start,
		mode->crtc_hsync_end, mode->crtc_htotal,
		mode->crtc_vdisplay, mode->crtc_vsync_start,
		mode->crtc_vsync_end, mode->crtc_vtotal, mode->type, mode->flags);
}

static inline void
intel_dump_m_n_config(struct intel_crtc_state *pipe_config, char *id,
		      unsigned int lane_count, struct intel_link_m_n *m_n)
{
	DRM_DEBUG_KMS("%s: lanes: %i; gmch_m: %u, gmch_n: %u, link_m: %u, link_n: %u, tu: %u\n",
		      id, lane_count,
		      m_n->gmch_m, m_n->gmch_n,
		      m_n->link_m, m_n->link_n, m_n->tu);
}

static void intel_dump_pipe_config(struct intel_crtc *crtc,
				   struct intel_crtc_state *pipe_config,
				   const char *context)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_plane *plane;
	struct intel_plane *intel_plane;
	struct intel_plane_state *state;
	struct drm_framebuffer *fb;

	DRM_DEBUG_KMS("[CRTC:%d:%s]%s\n",
		      crtc->base.base.id, crtc->base.name, context);

	DRM_DEBUG_KMS("cpu_transcoder: %s, pipe bpp: %i, dithering: %i\n",
		      transcoder_name(pipe_config->cpu_transcoder),
		      pipe_config->pipe_bpp, pipe_config->dither);

	if (pipe_config->has_pch_encoder)
		intel_dump_m_n_config(pipe_config, "fdi",
				      pipe_config->fdi_lanes,
				      &pipe_config->fdi_m_n);

	if (intel_crtc_has_dp_encoder(pipe_config)) {
		intel_dump_m_n_config(pipe_config, "dp m_n",
				pipe_config->lane_count, &pipe_config->dp_m_n);
		if (pipe_config->has_drrs)
			intel_dump_m_n_config(pipe_config, "dp m2_n2",
					      pipe_config->lane_count,
					      &pipe_config->dp_m2_n2);
	}

	DRM_DEBUG_KMS("audio: %i, infoframes: %i\n",
		      pipe_config->has_audio, pipe_config->has_infoframe);

	DRM_DEBUG_KMS("requested mode:\n");
	drm_mode_debug_printmodeline(&pipe_config->base.mode);
	DRM_DEBUG_KMS("adjusted mode:\n");
	drm_mode_debug_printmodeline(&pipe_config->base.adjusted_mode);
	intel_dump_crtc_timings(&pipe_config->base.adjusted_mode);
	DRM_DEBUG_KMS("port clock: %d, pipe src size: %dx%d, pixel rate %d\n",
		      pipe_config->port_clock,
		      pipe_config->pipe_src_w, pipe_config->pipe_src_h,
		      pipe_config->pixel_rate);

	if (INTEL_GEN(dev_priv) >= 9)
		DRM_DEBUG_KMS("num_scalers: %d, scaler_users: 0x%x, scaler_id: %d\n",
			      crtc->num_scalers,
			      pipe_config->scaler_state.scaler_users,
		              pipe_config->scaler_state.scaler_id);

	if (HAS_GMCH_DISPLAY(dev_priv))
		DRM_DEBUG_KMS("gmch pfit: control: 0x%08x, ratios: 0x%08x, lvds border: 0x%08x\n",
			      pipe_config->gmch_pfit.control,
			      pipe_config->gmch_pfit.pgm_ratios,
			      pipe_config->gmch_pfit.lvds_border_bits);
	else
		DRM_DEBUG_KMS("pch pfit: pos: 0x%08x, size: 0x%08x, %s\n",
			      pipe_config->pch_pfit.pos,
			      pipe_config->pch_pfit.size,
		              enableddisabled(pipe_config->pch_pfit.enabled));

	DRM_DEBUG_KMS("ips: %i, double wide: %i\n",
		      pipe_config->ips_enabled, pipe_config->double_wide);

	intel_dpll_dump_hw_state(dev_priv, &pipe_config->dpll_hw_state);

	DRM_DEBUG_KMS("planes on this crtc\n");
	list_for_each_entry(plane, &dev->mode_config.plane_list, head) {
		struct drm_format_name_buf format_name;
		intel_plane = to_intel_plane(plane);
		if (intel_plane->pipe != crtc->pipe)
			continue;

		state = to_intel_plane_state(plane->state);
		fb = state->base.fb;
		if (!fb) {
			DRM_DEBUG_KMS("[PLANE:%d:%s] disabled, scaler_id = %d\n",
				      plane->base.id, plane->name, state->scaler_id);
			continue;
		}

		DRM_DEBUG_KMS("[PLANE:%d:%s] FB:%d, fb = %ux%u format = %s\n",
			      plane->base.id, plane->name,
			      fb->base.id, fb->width, fb->height,
			      drm_get_format_name(fb->format->format, &format_name));
		if (INTEL_GEN(dev_priv) >= 9)
			DRM_DEBUG_KMS("\tscaler:%d src %dx%d+%d+%d dst %dx%d+%d+%d\n",
				      state->scaler_id,
				      state->base.src.x1 >> 16,
				      state->base.src.y1 >> 16,
				      drm_rect_width(&state->base.src) >> 16,
				      drm_rect_height(&state->base.src) >> 16,
				      state->base.dst.x1, state->base.dst.y1,
				      drm_rect_width(&state->base.dst),
				      drm_rect_height(&state->base.dst));
	}
}

static bool check_digital_port_conflicts(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_connector *connector;
	unsigned int used_ports = 0;
	unsigned int used_mst_ports = 0;

	/*
	 * Walk the connector list instead of the encoder
	 * list to detect the problem on ddi platforms
	 * where there's just one encoder per digital port.
	 */
	drm_for_each_connector(connector, dev) {
		struct drm_connector_state *connector_state;
		struct intel_encoder *encoder;

		connector_state = drm_atomic_get_existing_connector_state(state, connector);
		if (!connector_state)
			connector_state = connector->state;

		if (!connector_state->best_encoder)
			continue;

		encoder = to_intel_encoder(connector_state->best_encoder);

		WARN_ON(!connector_state->crtc);

		switch (encoder->type) {
			unsigned int port_mask;
		case INTEL_OUTPUT_UNKNOWN:
			if (WARN_ON(!HAS_DDI(to_i915(dev))))
				break;
		case INTEL_OUTPUT_DP:
		case INTEL_OUTPUT_HDMI:
		case INTEL_OUTPUT_EDP:
			port_mask = 1 << enc_to_dig_port(&encoder->base)->port;

			/* the same port mustn't appear more than once */
			if (used_ports & port_mask)
				return false;

			used_ports |= port_mask;
			break;
		case INTEL_OUTPUT_DP_MST:
			used_mst_ports |=
				1 << enc_to_mst(&encoder->base)->primary->port;
			break;
		default:
			break;
		}
	}

	/* can't mix MST and SST/HDMI on the same port */
	if (used_ports & used_mst_ports)
		return false;

	return true;
}

static void
clear_intel_crtc_state(struct intel_crtc_state *crtc_state)
{
	struct drm_crtc_state tmp_state;
	struct intel_crtc_scaler_state scaler_state;
	struct intel_dpll_hw_state dpll_hw_state;
	struct intel_shared_dpll *shared_dpll;
	bool force_thru;

	/* FIXME: before the switch to atomic started, a new pipe_config was
	 * kzalloc'd. Code that depends on any field being zero should be
	 * fixed, so that the crtc_state can be safely duplicated. For now,
	 * only fields that are know to not cause problems are preserved. */

	tmp_state = crtc_state->base;
	scaler_state = crtc_state->scaler_state;
	shared_dpll = crtc_state->shared_dpll;
	dpll_hw_state = crtc_state->dpll_hw_state;
	force_thru = crtc_state->pch_pfit.force_thru;

	memset(crtc_state, 0, sizeof *crtc_state);

	crtc_state->base = tmp_state;
	crtc_state->scaler_state = scaler_state;
	crtc_state->shared_dpll = shared_dpll;
	crtc_state->dpll_hw_state = dpll_hw_state;
	crtc_state->pch_pfit.force_thru = force_thru;
}

static int
intel_modeset_pipe_config(struct drm_crtc *crtc,
			  struct intel_crtc_state *pipe_config)
{
	struct drm_atomic_state *state = pipe_config->base.state;
	struct intel_encoder *encoder;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int base_bpp, ret = -EINVAL;
	int i;
	bool retry = true;

	clear_intel_crtc_state(pipe_config);

	pipe_config->cpu_transcoder =
		(enum transcoder) to_intel_crtc(crtc)->pipe;

	/*
	 * Sanitize sync polarity flags based on requested ones. If neither
	 * positive or negative polarity is requested, treat this as meaning
	 * negative polarity.
	 */
	if (!(pipe_config->base.adjusted_mode.flags &
	      (DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NHSYNC)))
		pipe_config->base.adjusted_mode.flags |= DRM_MODE_FLAG_NHSYNC;

	if (!(pipe_config->base.adjusted_mode.flags &
	      (DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_NVSYNC)))
		pipe_config->base.adjusted_mode.flags |= DRM_MODE_FLAG_NVSYNC;

	base_bpp = compute_baseline_pipe_bpp(to_intel_crtc(crtc),
					     pipe_config);
	if (base_bpp < 0)
		goto fail;

	/*
	 * Determine the real pipe dimensions. Note that stereo modes can
	 * increase the actual pipe size due to the frame doubling and
	 * insertion of additional space for blanks between the frame. This
	 * is stored in the crtc timings. We use the requested mode to do this
	 * computation to clearly distinguish it from the adjusted mode, which
	 * can be changed by the connectors in the below retry loop.
	 */
	drm_mode_get_hv_timing(&pipe_config->base.mode,
			       &pipe_config->pipe_src_w,
			       &pipe_config->pipe_src_h);

	for_each_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc != crtc)
			continue;

		encoder = to_intel_encoder(connector_state->best_encoder);

		if (!check_single_encoder_cloning(state, to_intel_crtc(crtc), encoder)) {
			DRM_DEBUG_KMS("rejecting invalid cloning configuration\n");
			goto fail;
		}

		/*
		 * Determine output_types before calling the .compute_config()
		 * hooks so that the hooks can use this information safely.
		 */
		pipe_config->output_types |= 1 << encoder->type;
	}

encoder_retry:
	/* Ensure the port clock defaults are reset when retrying. */
	pipe_config->port_clock = 0;
	pipe_config->pixel_multiplier = 1;

	/* Fill in default crtc timings, allow encoders to overwrite them. */
	drm_mode_set_crtcinfo(&pipe_config->base.adjusted_mode,
			      CRTC_STEREO_DOUBLE);

	/* Pass our mode to the connectors and the CRTC to give them a chance to
	 * adjust it according to limitations or connector properties, and also
	 * a chance to reject the mode entirely.
	 */
	for_each_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc != crtc)
			continue;

		encoder = to_intel_encoder(connector_state->best_encoder);

		if (!(encoder->compute_config(encoder, pipe_config, connector_state))) {
			DRM_DEBUG_KMS("Encoder config failure\n");
			goto fail;
		}
	}

	/* Set default port clock if not overwritten by the encoder. Needs to be
	 * done afterwards in case the encoder adjusts the mode. */
	if (!pipe_config->port_clock)
		pipe_config->port_clock = pipe_config->base.adjusted_mode.crtc_clock
			* pipe_config->pixel_multiplier;

	ret = intel_crtc_compute_config(to_intel_crtc(crtc), pipe_config);
	if (ret < 0) {
		DRM_DEBUG_KMS("CRTC fixup failed\n");
		goto fail;
	}

	if (ret == RETRY) {
		if (WARN(!retry, "loop in pipe configuration computation\n")) {
			ret = -EINVAL;
			goto fail;
		}

		DRM_DEBUG_KMS("CRTC bw constrained, retrying\n");
		retry = false;
		goto encoder_retry;
	}

	/* Dithering seems to not pass-through bits correctly when it should, so
	 * only enable it on 6bpc panels and when its not a compliance
	 * test requesting 6bpc video pattern.
	 */
	pipe_config->dither = (pipe_config->pipe_bpp == 6*3) &&
		!pipe_config->dither_force_disable;
	DRM_DEBUG_KMS("hw max bpp: %i, pipe bpp: %i, dithering: %i\n",
		      base_bpp, pipe_config->pipe_bpp, pipe_config->dither);

fail:
	return ret;
}

static void
intel_modeset_update_crtc_state(struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int i;

	/* Double check state. */
	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		to_intel_crtc(crtc)->config = to_intel_crtc_state(crtc->state);

		/* Update hwmode for vblank functions */
		if (crtc->state->active)
			crtc->hwmode = crtc->state->adjusted_mode;
		else
			crtc->hwmode.crtc_clock = 0;

		/*
		 * Update legacy state to satisfy fbc code. This can
		 * be removed when fbc uses the atomic state.
		 */
		if (drm_atomic_get_existing_plane_state(state, crtc->primary)) {
			struct drm_plane_state *plane_state = crtc->primary->state;

			crtc->primary->fb = plane_state->fb;
			crtc->x = plane_state->src_x >> 16;
			crtc->y = plane_state->src_y >> 16;
		}
	}
}

static bool intel_fuzzy_clock_check(int clock1, int clock2)
{
	int diff;

	if (clock1 == clock2)
		return true;

	if (!clock1 || !clock2)
		return false;

	diff = abs(clock1 - clock2);

	if (((((diff + clock1 + clock2) * 100)) / (clock1 + clock2)) < 105)
		return true;

	return false;
}

static bool
intel_compare_m_n(unsigned int m, unsigned int n,
		  unsigned int m2, unsigned int n2,
		  bool exact)
{
	if (m == m2 && n == n2)
		return true;

	if (exact || !m || !n || !m2 || !n2)
		return false;

	BUILD_BUG_ON(DATA_LINK_M_N_MASK > INT_MAX);

	if (n > n2) {
		while (n > n2) {
			m2 <<= 1;
			n2 <<= 1;
		}
	} else if (n < n2) {
		while (n < n2) {
			m <<= 1;
			n <<= 1;
		}
	}

	if (n != n2)
		return false;

	return intel_fuzzy_clock_check(m, m2);
}

static bool
intel_compare_link_m_n(const struct intel_link_m_n *m_n,
		       struct intel_link_m_n *m2_n2,
		       bool adjust)
{
	if (m_n->tu == m2_n2->tu &&
	    intel_compare_m_n(m_n->gmch_m, m_n->gmch_n,
			      m2_n2->gmch_m, m2_n2->gmch_n, !adjust) &&
	    intel_compare_m_n(m_n->link_m, m_n->link_n,
			      m2_n2->link_m, m2_n2->link_n, !adjust)) {
		if (adjust)
			*m2_n2 = *m_n;

		return true;
	}

	return false;
}

static void __printf(3, 4)
pipe_config_err(bool adjust, const char *name, const char *format, ...)
{
	char *level;
	unsigned int category;
	struct va_format vaf;
	va_list args;

	if (adjust) {
		level = KERN_DEBUG;
		category = DRM_UT_KMS;
	} else {
		level = KERN_ERR;
		category = DRM_UT_NONE;
	}

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	drm_printk(level, category, "mismatch in %s %pV", name, &vaf);

	va_end(args);
}

static bool
intel_pipe_config_compare(struct drm_i915_private *dev_priv,
			  struct intel_crtc_state *current_config,
			  struct intel_crtc_state *pipe_config,
			  bool adjust)
{
	bool ret = true;

#define PIPE_CONF_CHECK_X(name)	\
	if (current_config->name != pipe_config->name) { \
		pipe_config_err(adjust, __stringify(name), \
			  "(expected 0x%08x, found 0x%08x)\n", \
			  current_config->name, \
			  pipe_config->name); \
		ret = false; \
	}

#define PIPE_CONF_CHECK_I(name)	\
	if (current_config->name != pipe_config->name) { \
		pipe_config_err(adjust, __stringify(name), \
			  "(expected %i, found %i)\n", \
			  current_config->name, \
			  pipe_config->name); \
		ret = false; \
	}

#define PIPE_CONF_CHECK_P(name)	\
	if (current_config->name != pipe_config->name) { \
		pipe_config_err(adjust, __stringify(name), \
			  "(expected %p, found %p)\n", \
			  current_config->name, \
			  pipe_config->name); \
		ret = false; \
	}

#define PIPE_CONF_CHECK_M_N(name) \
	if (!intel_compare_link_m_n(&current_config->name, \
				    &pipe_config->name,\
				    adjust)) { \
		pipe_config_err(adjust, __stringify(name), \
			  "(expected tu %i gmch %i/%i link %i/%i, " \
			  "found tu %i, gmch %i/%i link %i/%i)\n", \
			  current_config->name.tu, \
			  current_config->name.gmch_m, \
			  current_config->name.gmch_n, \
			  current_config->name.link_m, \
			  current_config->name.link_n, \
			  pipe_config->name.tu, \
			  pipe_config->name.gmch_m, \
			  pipe_config->name.gmch_n, \
			  pipe_config->name.link_m, \
			  pipe_config->name.link_n); \
		ret = false; \
	}

/* This is required for BDW+ where there is only one set of registers for
 * switching between high and low RR.
 * This macro can be used whenever a comparison has to be made between one
 * hw state and multiple sw state variables.
 */
#define PIPE_CONF_CHECK_M_N_ALT(name, alt_name) \
	if (!intel_compare_link_m_n(&current_config->name, \
				    &pipe_config->name, adjust) && \
	    !intel_compare_link_m_n(&current_config->alt_name, \
				    &pipe_config->name, adjust)) { \
		pipe_config_err(adjust, __stringify(name), \
			  "(expected tu %i gmch %i/%i link %i/%i, " \
			  "or tu %i gmch %i/%i link %i/%i, " \
			  "found tu %i, gmch %i/%i link %i/%i)\n", \
			  current_config->name.tu, \
			  current_config->name.gmch_m, \
			  current_config->name.gmch_n, \
			  current_config->name.link_m, \
			  current_config->name.link_n, \
			  current_config->alt_name.tu, \
			  current_config->alt_name.gmch_m, \
			  current_config->alt_name.gmch_n, \
			  current_config->alt_name.link_m, \
			  current_config->alt_name.link_n, \
			  pipe_config->name.tu, \
			  pipe_config->name.gmch_m, \
			  pipe_config->name.gmch_n, \
			  pipe_config->name.link_m, \
			  pipe_config->name.link_n); \
		ret = false; \
	}

#define PIPE_CONF_CHECK_FLAGS(name, mask)	\
	if ((current_config->name ^ pipe_config->name) & (mask)) { \
		pipe_config_err(adjust, __stringify(name), \
			  "(%x) (expected %i, found %i)\n", \
			  (mask), \
			  current_config->name & (mask), \
			  pipe_config->name & (mask)); \
		ret = false; \
	}

#define PIPE_CONF_CHECK_CLOCK_FUZZY(name) \
	if (!intel_fuzzy_clock_check(current_config->name, pipe_config->name)) { \
		pipe_config_err(adjust, __stringify(name), \
			  "(expected %i, found %i)\n", \
			  current_config->name, \
			  pipe_config->name); \
		ret = false; \
	}

#define PIPE_CONF_QUIRK(quirk)	\
	((current_config->quirks | pipe_config->quirks) & (quirk))

	PIPE_CONF_CHECK_I(cpu_transcoder);

	PIPE_CONF_CHECK_I(has_pch_encoder);
	PIPE_CONF_CHECK_I(fdi_lanes);
	PIPE_CONF_CHECK_M_N(fdi_m_n);

	PIPE_CONF_CHECK_I(lane_count);
	PIPE_CONF_CHECK_X(lane_lat_optim_mask);

	if (INTEL_GEN(dev_priv) < 8) {
		PIPE_CONF_CHECK_M_N(dp_m_n);

		if (current_config->has_drrs)
			PIPE_CONF_CHECK_M_N(dp_m2_n2);
	} else
		PIPE_CONF_CHECK_M_N_ALT(dp_m_n, dp_m2_n2);

	PIPE_CONF_CHECK_X(output_types);

	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_hdisplay);
	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_htotal);
	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_hblank_start);
	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_hblank_end);
	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_hsync_start);
	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_hsync_end);

	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_vdisplay);
	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_vtotal);
	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_vblank_start);
	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_vblank_end);
	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_vsync_start);
	PIPE_CONF_CHECK_I(base.adjusted_mode.crtc_vsync_end);

	PIPE_CONF_CHECK_I(pixel_multiplier);
	PIPE_CONF_CHECK_I(has_hdmi_sink);
	if ((INTEL_GEN(dev_priv) < 8 && !IS_HASWELL(dev_priv)) ||
	    IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		PIPE_CONF_CHECK_I(limited_color_range);
	PIPE_CONF_CHECK_I(has_infoframe);

	PIPE_CONF_CHECK_I(has_audio);

	PIPE_CONF_CHECK_FLAGS(base.adjusted_mode.flags,
			      DRM_MODE_FLAG_INTERLACE);

	if (!PIPE_CONF_QUIRK(PIPE_CONFIG_QUIRK_MODE_SYNC_FLAGS)) {
		PIPE_CONF_CHECK_FLAGS(base.adjusted_mode.flags,
				      DRM_MODE_FLAG_PHSYNC);
		PIPE_CONF_CHECK_FLAGS(base.adjusted_mode.flags,
				      DRM_MODE_FLAG_NHSYNC);
		PIPE_CONF_CHECK_FLAGS(base.adjusted_mode.flags,
				      DRM_MODE_FLAG_PVSYNC);
		PIPE_CONF_CHECK_FLAGS(base.adjusted_mode.flags,
				      DRM_MODE_FLAG_NVSYNC);
	}

	PIPE_CONF_CHECK_X(gmch_pfit.control);
	/* pfit ratios are autocomputed by the hw on gen4+ */
	if (INTEL_GEN(dev_priv) < 4)
		PIPE_CONF_CHECK_X(gmch_pfit.pgm_ratios);
	PIPE_CONF_CHECK_X(gmch_pfit.lvds_border_bits);

	if (!adjust) {
		PIPE_CONF_CHECK_I(pipe_src_w);
		PIPE_CONF_CHECK_I(pipe_src_h);

		PIPE_CONF_CHECK_I(pch_pfit.enabled);
		if (current_config->pch_pfit.enabled) {
			PIPE_CONF_CHECK_X(pch_pfit.pos);
			PIPE_CONF_CHECK_X(pch_pfit.size);
		}

		PIPE_CONF_CHECK_I(scaler_state.scaler_id);
		PIPE_CONF_CHECK_CLOCK_FUZZY(pixel_rate);
	}

	/* BDW+ don't expose a synchronous way to read the state */
	if (IS_HASWELL(dev_priv))
		PIPE_CONF_CHECK_I(ips_enabled);

	PIPE_CONF_CHECK_I(double_wide);

	PIPE_CONF_CHECK_P(shared_dpll);
	PIPE_CONF_CHECK_X(dpll_hw_state.dpll);
	PIPE_CONF_CHECK_X(dpll_hw_state.dpll_md);
	PIPE_CONF_CHECK_X(dpll_hw_state.fp0);
	PIPE_CONF_CHECK_X(dpll_hw_state.fp1);
	PIPE_CONF_CHECK_X(dpll_hw_state.wrpll);
	PIPE_CONF_CHECK_X(dpll_hw_state.spll);
	PIPE_CONF_CHECK_X(dpll_hw_state.ctrl1);
	PIPE_CONF_CHECK_X(dpll_hw_state.cfgcr1);
	PIPE_CONF_CHECK_X(dpll_hw_state.cfgcr2);

	PIPE_CONF_CHECK_X(dsi_pll.ctrl);
	PIPE_CONF_CHECK_X(dsi_pll.div);

	if (IS_G4X(dev_priv) || INTEL_GEN(dev_priv) >= 5)
		PIPE_CONF_CHECK_I(pipe_bpp);

	PIPE_CONF_CHECK_CLOCK_FUZZY(base.adjusted_mode.crtc_clock);
	PIPE_CONF_CHECK_CLOCK_FUZZY(port_clock);

#undef PIPE_CONF_CHECK_X
#undef PIPE_CONF_CHECK_I
#undef PIPE_CONF_CHECK_P
#undef PIPE_CONF_CHECK_FLAGS
#undef PIPE_CONF_CHECK_CLOCK_FUZZY
#undef PIPE_CONF_QUIRK

	return ret;
}

static void intel_pipe_config_sanity_check(struct drm_i915_private *dev_priv,
					   const struct intel_crtc_state *pipe_config)
{
	if (pipe_config->has_pch_encoder) {
		int fdi_dotclock = intel_dotclock_calculate(intel_fdi_link_freq(dev_priv, pipe_config),
							    &pipe_config->fdi_m_n);
		int dotclock = pipe_config->base.adjusted_mode.crtc_clock;

		/*
		 * FDI already provided one idea for the dotclock.
		 * Yell if the encoder disagrees.
		 */
		WARN(!intel_fuzzy_clock_check(fdi_dotclock, dotclock),
		     "FDI dotclock and encoder dotclock mismatch, fdi: %i, encoder: %i\n",
		     fdi_dotclock, dotclock);
	}
}

static void verify_wm_state(struct drm_crtc *crtc,
			    struct drm_crtc_state *new_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct skl_ddb_allocation hw_ddb, *sw_ddb;
	struct skl_pipe_wm hw_wm, *sw_wm;
	struct skl_plane_wm *hw_plane_wm, *sw_plane_wm;
	struct skl_ddb_entry *hw_ddb_entry, *sw_ddb_entry;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	const enum pipe pipe = intel_crtc->pipe;
	int plane, level, max_level = ilk_wm_max_level(dev_priv);

	if (INTEL_GEN(dev_priv) < 9 || !new_state->active)
		return;

	skl_pipe_wm_get_hw_state(crtc, &hw_wm);
	sw_wm = &to_intel_crtc_state(new_state)->wm.skl.optimal;

	skl_ddb_get_hw_state(dev_priv, &hw_ddb);
	sw_ddb = &dev_priv->wm.skl_hw.ddb;

	/* planes */
	for_each_universal_plane(dev_priv, pipe, plane) {
		hw_plane_wm = &hw_wm.planes[plane];
		sw_plane_wm = &sw_wm->planes[plane];

		/* Watermarks */
		for (level = 0; level <= max_level; level++) {
			if (skl_wm_level_equals(&hw_plane_wm->wm[level],
						&sw_plane_wm->wm[level]))
				continue;

			DRM_ERROR("mismatch in WM pipe %c plane %d level %d (expected e=%d b=%u l=%u, got e=%d b=%u l=%u)\n",
				  pipe_name(pipe), plane + 1, level,
				  sw_plane_wm->wm[level].plane_en,
				  sw_plane_wm->wm[level].plane_res_b,
				  sw_plane_wm->wm[level].plane_res_l,
				  hw_plane_wm->wm[level].plane_en,
				  hw_plane_wm->wm[level].plane_res_b,
				  hw_plane_wm->wm[level].plane_res_l);
		}

		if (!skl_wm_level_equals(&hw_plane_wm->trans_wm,
					 &sw_plane_wm->trans_wm)) {
			DRM_ERROR("mismatch in trans WM pipe %c plane %d (expected e=%d b=%u l=%u, got e=%d b=%u l=%u)\n",
				  pipe_name(pipe), plane + 1,
				  sw_plane_wm->trans_wm.plane_en,
				  sw_plane_wm->trans_wm.plane_res_b,
				  sw_plane_wm->trans_wm.plane_res_l,
				  hw_plane_wm->trans_wm.plane_en,
				  hw_plane_wm->trans_wm.plane_res_b,
				  hw_plane_wm->trans_wm.plane_res_l);
		}

		/* DDB */
		hw_ddb_entry = &hw_ddb.plane[pipe][plane];
		sw_ddb_entry = &sw_ddb->plane[pipe][plane];

		if (!skl_ddb_entry_equal(hw_ddb_entry, sw_ddb_entry)) {
			DRM_ERROR("mismatch in DDB state pipe %c plane %d (expected (%u,%u), found (%u,%u))\n",
				  pipe_name(pipe), plane + 1,
				  sw_ddb_entry->start, sw_ddb_entry->end,
				  hw_ddb_entry->start, hw_ddb_entry->end);
		}
	}

	/*
	 * cursor
	 * If the cursor plane isn't active, we may not have updated it's ddb
	 * allocation. In that case since the ddb allocation will be updated
	 * once the plane becomes visible, we can skip this check
	 */
	if (intel_crtc->cursor_addr) {
		hw_plane_wm = &hw_wm.planes[PLANE_CURSOR];
		sw_plane_wm = &sw_wm->planes[PLANE_CURSOR];

		/* Watermarks */
		for (level = 0; level <= max_level; level++) {
			if (skl_wm_level_equals(&hw_plane_wm->wm[level],
						&sw_plane_wm->wm[level]))
				continue;

			DRM_ERROR("mismatch in WM pipe %c cursor level %d (expected e=%d b=%u l=%u, got e=%d b=%u l=%u)\n",
				  pipe_name(pipe), level,
				  sw_plane_wm->wm[level].plane_en,
				  sw_plane_wm->wm[level].plane_res_b,
				  sw_plane_wm->wm[level].plane_res_l,
				  hw_plane_wm->wm[level].plane_en,
				  hw_plane_wm->wm[level].plane_res_b,
				  hw_plane_wm->wm[level].plane_res_l);
		}

		if (!skl_wm_level_equals(&hw_plane_wm->trans_wm,
					 &sw_plane_wm->trans_wm)) {
			DRM_ERROR("mismatch in trans WM pipe %c cursor (expected e=%d b=%u l=%u, got e=%d b=%u l=%u)\n",
				  pipe_name(pipe),
				  sw_plane_wm->trans_wm.plane_en,
				  sw_plane_wm->trans_wm.plane_res_b,
				  sw_plane_wm->trans_wm.plane_res_l,
				  hw_plane_wm->trans_wm.plane_en,
				  hw_plane_wm->trans_wm.plane_res_b,
				  hw_plane_wm->trans_wm.plane_res_l);
		}

		/* DDB */
		hw_ddb_entry = &hw_ddb.plane[pipe][PLANE_CURSOR];
		sw_ddb_entry = &sw_ddb->plane[pipe][PLANE_CURSOR];

		if (!skl_ddb_entry_equal(hw_ddb_entry, sw_ddb_entry)) {
			DRM_ERROR("mismatch in DDB state pipe %c cursor (expected (%u,%u), found (%u,%u))\n",
				  pipe_name(pipe),
				  sw_ddb_entry->start, sw_ddb_entry->end,
				  hw_ddb_entry->start, hw_ddb_entry->end);
		}
	}
}

static void
verify_connector_state(struct drm_device *dev,
		       struct drm_atomic_state *state,
		       struct drm_crtc *crtc)
{
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state;
	int i;

	for_each_connector_in_state(state, connector, old_conn_state, i) {
		struct drm_encoder *encoder = connector->encoder;
		struct drm_connector_state *state = connector->state;

		if (state->crtc != crtc)
			continue;

		intel_connector_verify_state(to_intel_connector(connector));

		I915_STATE_WARN(state->best_encoder != encoder,
		     "connector's atomic encoder doesn't match legacy encoder\n");
	}
}

static void
verify_encoder_state(struct drm_device *dev)
{
	struct intel_encoder *encoder;
	struct intel_connector *connector;

	for_each_intel_encoder(dev, encoder) {
		bool enabled = false;
		enum pipe pipe;

		DRM_DEBUG_KMS("[ENCODER:%d:%s]\n",
			      encoder->base.base.id,
			      encoder->base.name);

		for_each_intel_connector(dev, connector) {
			if (connector->base.state->best_encoder != &encoder->base)
				continue;
			enabled = true;

			I915_STATE_WARN(connector->base.state->crtc !=
					encoder->base.crtc,
			     "connector's crtc doesn't match encoder crtc\n");
		}

		I915_STATE_WARN(!!encoder->base.crtc != enabled,
		     "encoder's enabled state mismatch "
		     "(expected %i, found %i)\n",
		     !!encoder->base.crtc, enabled);

		if (!encoder->base.crtc) {
			bool active;

			active = encoder->get_hw_state(encoder, &pipe);
			I915_STATE_WARN(active,
			     "encoder detached but still enabled on pipe %c.\n",
			     pipe_name(pipe));
		}
	}
}

static void
verify_crtc_state(struct drm_crtc *crtc,
		  struct drm_crtc_state *old_crtc_state,
		  struct drm_crtc_state *new_crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_encoder *encoder;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_crtc_state *pipe_config, *sw_config;
	struct drm_atomic_state *old_state;
	bool active;

	old_state = old_crtc_state->state;
	__drm_atomic_helper_crtc_destroy_state(old_crtc_state);
	pipe_config = to_intel_crtc_state(old_crtc_state);
	memset(pipe_config, 0, sizeof(*pipe_config));
	pipe_config->base.crtc = crtc;
	pipe_config->base.state = old_state;

	DRM_DEBUG_KMS("[CRTC:%d:%s]\n", crtc->base.id, crtc->name);

	active = dev_priv->display.get_pipe_config(intel_crtc, pipe_config);

	/* hw state is inconsistent with the pipe quirk */
	if ((intel_crtc->pipe == PIPE_A && dev_priv->quirks & QUIRK_PIPEA_FORCE) ||
	    (intel_crtc->pipe == PIPE_B && dev_priv->quirks & QUIRK_PIPEB_FORCE))
		active = new_crtc_state->active;

	I915_STATE_WARN(new_crtc_state->active != active,
	     "crtc active state doesn't match with hw state "
	     "(expected %i, found %i)\n", new_crtc_state->active, active);

	I915_STATE_WARN(intel_crtc->active != new_crtc_state->active,
	     "transitional active state does not match atomic hw state "
	     "(expected %i, found %i)\n", new_crtc_state->active, intel_crtc->active);

	for_each_encoder_on_crtc(dev, crtc, encoder) {
		enum pipe pipe;

		active = encoder->get_hw_state(encoder, &pipe);
		I915_STATE_WARN(active != new_crtc_state->active,
			"[ENCODER:%i] active %i with crtc active %i\n",
			encoder->base.base.id, active, new_crtc_state->active);

		I915_STATE_WARN(active && intel_crtc->pipe != pipe,
				"Encoder connected to wrong pipe %c\n",
				pipe_name(pipe));

		if (active) {
			pipe_config->output_types |= 1 << encoder->type;
			encoder->get_config(encoder, pipe_config);
		}
	}

	intel_crtc_compute_pixel_rate(pipe_config);

	if (!new_crtc_state->active)
		return;

	intel_pipe_config_sanity_check(dev_priv, pipe_config);

	sw_config = to_intel_crtc_state(crtc->state);
	if (!intel_pipe_config_compare(dev_priv, sw_config,
				       pipe_config, false)) {
		I915_STATE_WARN(1, "pipe state doesn't match!\n");
		intel_dump_pipe_config(intel_crtc, pipe_config,
				       "[hw state]");
		intel_dump_pipe_config(intel_crtc, sw_config,
				       "[sw state]");
	}
}

static void
verify_single_dpll_state(struct drm_i915_private *dev_priv,
			 struct intel_shared_dpll *pll,
			 struct drm_crtc *crtc,
			 struct drm_crtc_state *new_state)
{
	struct intel_dpll_hw_state dpll_hw_state;
	unsigned crtc_mask;
	bool active;

	memset(&dpll_hw_state, 0, sizeof(dpll_hw_state));

	DRM_DEBUG_KMS("%s\n", pll->name);

	active = pll->funcs.get_hw_state(dev_priv, pll, &dpll_hw_state);

	if (!(pll->flags & INTEL_DPLL_ALWAYS_ON)) {
		I915_STATE_WARN(!pll->on && pll->active_mask,
		     "pll in active use but not on in sw tracking\n");
		I915_STATE_WARN(pll->on && !pll->active_mask,
		     "pll is on but not used by any active crtc\n");
		I915_STATE_WARN(pll->on != active,
		     "pll on state mismatch (expected %i, found %i)\n",
		     pll->on, active);
	}

	if (!crtc) {
		I915_STATE_WARN(pll->active_mask & ~pll->state.crtc_mask,
				"more active pll users than references: %x vs %x\n",
				pll->active_mask, pll->state.crtc_mask);

		return;
	}

	crtc_mask = 1 << drm_crtc_index(crtc);

	if (new_state->active)
		I915_STATE_WARN(!(pll->active_mask & crtc_mask),
				"pll active mismatch (expected pipe %c in active mask 0x%02x)\n",
				pipe_name(drm_crtc_index(crtc)), pll->active_mask);
	else
		I915_STATE_WARN(pll->active_mask & crtc_mask,
				"pll active mismatch (didn't expect pipe %c in active mask 0x%02x)\n",
				pipe_name(drm_crtc_index(crtc)), pll->active_mask);

	I915_STATE_WARN(!(pll->state.crtc_mask & crtc_mask),
			"pll enabled crtcs mismatch (expected 0x%x in 0x%02x)\n",
			crtc_mask, pll->state.crtc_mask);

	I915_STATE_WARN(pll->on && memcmp(&pll->state.hw_state,
					  &dpll_hw_state,
					  sizeof(dpll_hw_state)),
			"pll hw state mismatch\n");
}

static void
verify_shared_dpll_state(struct drm_device *dev, struct drm_crtc *crtc,
			 struct drm_crtc_state *old_crtc_state,
			 struct drm_crtc_state *new_crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc_state *old_state = to_intel_crtc_state(old_crtc_state);
	struct intel_crtc_state *new_state = to_intel_crtc_state(new_crtc_state);

	if (new_state->shared_dpll)
		verify_single_dpll_state(dev_priv, new_state->shared_dpll, crtc, new_crtc_state);

	if (old_state->shared_dpll &&
	    old_state->shared_dpll != new_state->shared_dpll) {
		unsigned crtc_mask = 1 << drm_crtc_index(crtc);
		struct intel_shared_dpll *pll = old_state->shared_dpll;

		I915_STATE_WARN(pll->active_mask & crtc_mask,
				"pll active mismatch (didn't expect pipe %c in active mask)\n",
				pipe_name(drm_crtc_index(crtc)));
		I915_STATE_WARN(pll->state.crtc_mask & crtc_mask,
				"pll enabled crtcs mismatch (found %x in enabled mask)\n",
				pipe_name(drm_crtc_index(crtc)));
	}
}

static void
intel_modeset_verify_crtc(struct drm_crtc *crtc,
			  struct drm_atomic_state *state,
			  struct drm_crtc_state *old_state,
			  struct drm_crtc_state *new_state)
{
	if (!needs_modeset(new_state) &&
	    !to_intel_crtc_state(new_state)->update_pipe)
		return;

	verify_wm_state(crtc, new_state);
	verify_connector_state(crtc->dev, state, crtc);
	verify_crtc_state(crtc, old_state, new_state);
	verify_shared_dpll_state(crtc->dev, crtc, old_state, new_state);
}

static void
verify_disabled_dpll_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int i;

	for (i = 0; i < dev_priv->num_shared_dpll; i++)
		verify_single_dpll_state(dev_priv, &dev_priv->shared_dplls[i], NULL, NULL);
}

static void
intel_modeset_verify_disabled(struct drm_device *dev,
			      struct drm_atomic_state *state)
{
	verify_encoder_state(dev);
	verify_connector_state(dev, state, NULL);
	verify_disabled_dpll_state(dev);
}

static void update_scanline_offset(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	/*
	 * The scanline counter increments at the leading edge of hsync.
	 *
	 * On most platforms it starts counting from vtotal-1 on the
	 * first active line. That means the scanline counter value is
	 * always one less than what we would expect. Ie. just after
	 * start of vblank, which also occurs at start of hsync (on the
	 * last active line), the scanline counter will read vblank_start-1.
	 *
	 * On gen2 the scanline counter starts counting from 1 instead
	 * of vtotal-1, so we have to subtract one (or rather add vtotal-1
	 * to keep the value positive), instead of adding one.
	 *
	 * On HSW+ the behaviour of the scanline counter depends on the output
	 * type. For DP ports it behaves like most other platforms, but on HDMI
	 * there's an extra 1 line difference. So we need to add two instead of
	 * one to the value.
	 */
	if (IS_GEN2(dev_priv)) {
		const struct drm_display_mode *adjusted_mode = &crtc->config->base.adjusted_mode;
		int vtotal;

		vtotal = adjusted_mode->crtc_vtotal;
		if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
			vtotal /= 2;

		crtc->scanline_offset = vtotal - 1;
	} else if (HAS_DDI(dev_priv) &&
		   intel_crtc_has_type(crtc->config, INTEL_OUTPUT_HDMI)) {
		crtc->scanline_offset = 2;
	} else
		crtc->scanline_offset = 1;
}

static void intel_modeset_clear_plls(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int i;

	if (!dev_priv->display.crtc_compute_clock)
		return;

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
		struct intel_shared_dpll *old_dpll =
			to_intel_crtc_state(crtc->state)->shared_dpll;

		if (!needs_modeset(crtc_state))
			continue;

		to_intel_crtc_state(crtc_state)->shared_dpll = NULL;

		if (!old_dpll)
			continue;

		intel_release_shared_dpll(old_dpll, intel_crtc, state);
	}
}

/*
 * This implements the workaround described in the "notes" section of the mode
 * set sequence documentation. When going from no pipes or single pipe to
 * multiple pipes, and planes are enabled after the pipe, we need to wait at
 * least 2 vblanks on the first pipe before enabling planes on the second pipe.
 */
static int haswell_mode_set_planes_workaround(struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct intel_crtc *intel_crtc;
	struct drm_crtc *crtc;
	struct intel_crtc_state *first_crtc_state = NULL;
	struct intel_crtc_state *other_crtc_state = NULL;
	enum pipe first_pipe = INVALID_PIPE, enabled_pipe = INVALID_PIPE;
	int i;

	/* look at all crtc's that are going to be enabled in during modeset */
	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		intel_crtc = to_intel_crtc(crtc);

		if (!crtc_state->active || !needs_modeset(crtc_state))
			continue;

		if (first_crtc_state) {
			other_crtc_state = to_intel_crtc_state(crtc_state);
			break;
		} else {
			first_crtc_state = to_intel_crtc_state(crtc_state);
			first_pipe = intel_crtc->pipe;
		}
	}

	/* No workaround needed? */
	if (!first_crtc_state)
		return 0;

	/* w/a possibly needed, check how many crtc's are already enabled. */
	for_each_intel_crtc(state->dev, intel_crtc) {
		struct intel_crtc_state *pipe_config;

		pipe_config = intel_atomic_get_crtc_state(state, intel_crtc);
		if (IS_ERR(pipe_config))
			return PTR_ERR(pipe_config);

		pipe_config->hsw_workaround_pipe = INVALID_PIPE;

		if (!pipe_config->base.active ||
		    needs_modeset(&pipe_config->base))
			continue;

		/* 2 or more enabled crtcs means no need for w/a */
		if (enabled_pipe != INVALID_PIPE)
			return 0;

		enabled_pipe = intel_crtc->pipe;
	}

	if (enabled_pipe != INVALID_PIPE)
		first_crtc_state->hsw_workaround_pipe = enabled_pipe;
	else if (other_crtc_state)
		other_crtc_state->hsw_workaround_pipe = first_pipe;

	return 0;
}

static int intel_lock_all_pipes(struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;

	/* Add all pipes to the state */
	for_each_crtc(state->dev, crtc) {
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
	}

	return 0;
}

static int intel_modeset_all_pipes(struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;

	/*
	 * Add all pipes to the state, and force
	 * a modeset on all the active ones.
	 */
	for_each_crtc(state->dev, crtc) {
		struct drm_crtc_state *crtc_state;
		int ret;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		if (!crtc_state->active || needs_modeset(crtc_state))
			continue;

		crtc_state->mode_changed = true;

		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret)
			return ret;

		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret)
			return ret;
	}

	return 0;
}

static int intel_modeset_checks(struct drm_atomic_state *state)
{
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);
	struct drm_i915_private *dev_priv = to_i915(state->dev);
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int ret = 0, i;

	if (!check_digital_port_conflicts(state)) {
		DRM_DEBUG_KMS("rejecting conflicting digital port configuration\n");
		return -EINVAL;
	}

	intel_state->modeset = true;
	intel_state->active_crtcs = dev_priv->active_crtcs;
	intel_state->cdclk.logical = dev_priv->cdclk.logical;
	intel_state->cdclk.actual = dev_priv->cdclk.actual;

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		if (crtc_state->active)
			intel_state->active_crtcs |= 1 << i;
		else
			intel_state->active_crtcs &= ~(1 << i);

		if (crtc_state->active != crtc->state->active)
			intel_state->active_pipe_changes |= drm_crtc_mask(crtc);
	}

	/*
	 * See if the config requires any additional preparation, e.g.
	 * to adjust global state with pipes off.  We need to do this
	 * here so we can get the modeset_pipe updated config for the new
	 * mode set on this crtc.  For other crtcs we need to use the
	 * adjusted_mode bits in the crtc directly.
	 */
	if (dev_priv->display.modeset_calc_cdclk) {
		ret = dev_priv->display.modeset_calc_cdclk(state);
		if (ret < 0)
			return ret;

		/*
		 * Writes to dev_priv->cdclk.logical must protected by
		 * holding all the crtc locks, even if we don't end up
		 * touching the hardware
		 */
		if (!intel_cdclk_state_compare(&dev_priv->cdclk.logical,
					       &intel_state->cdclk.logical)) {
			ret = intel_lock_all_pipes(state);
			if (ret < 0)
				return ret;
		}

		/* All pipes must be switched off while we change the cdclk. */
		if (!intel_cdclk_state_compare(&dev_priv->cdclk.actual,
					       &intel_state->cdclk.actual)) {
			ret = intel_modeset_all_pipes(state);
			if (ret < 0)
				return ret;
		}

		DRM_DEBUG_KMS("New cdclk calculated to be logical %u kHz, actual %u kHz\n",
			      intel_state->cdclk.logical.cdclk,
			      intel_state->cdclk.actual.cdclk);
	} else {
		to_intel_atomic_state(state)->cdclk.logical = dev_priv->cdclk.logical;
	}

	intel_modeset_clear_plls(state);

	if (IS_HASWELL(dev_priv))
		return haswell_mode_set_planes_workaround(state);

	return 0;
}

/*
 * Handle calculation of various watermark data at the end of the atomic check
 * phase.  The code here should be run after the per-crtc and per-plane 'check'
 * handlers to ensure that all derived state has been updated.
 */
static int calc_watermark_data(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	/* Is there platform-specific watermark information to calculate? */
	if (dev_priv->display.compute_global_watermarks)
		return dev_priv->display.compute_global_watermarks(state);

	return 0;
}

/**
 * intel_atomic_check - validate state object
 * @dev: drm device
 * @state: state to validate
 */
static int intel_atomic_check(struct drm_device *dev,
			      struct drm_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int ret, i;
	bool any_ms = false;

	ret = drm_atomic_helper_check_modeset(dev, state);
	if (ret)
		return ret;

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		struct intel_crtc_state *pipe_config =
			to_intel_crtc_state(crtc_state);

		/* Catch I915_MODE_FLAG_INHERITED */
		if (crtc_state->mode.private_flags != crtc->state->mode.private_flags)
			crtc_state->mode_changed = true;

		if (!needs_modeset(crtc_state))
			continue;

		if (!crtc_state->enable) {
			any_ms = true;
			continue;
		}

		/* FIXME: For only active_changed we shouldn't need to do any
		 * state recomputation at all. */

		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret)
			return ret;

		ret = intel_modeset_pipe_config(crtc, pipe_config);
		if (ret) {
			intel_dump_pipe_config(to_intel_crtc(crtc),
					       pipe_config, "[failed]");
			return ret;
		}

		if (i915.fastboot &&
		    intel_pipe_config_compare(dev_priv,
					to_intel_crtc_state(crtc->state),
					pipe_config, true)) {
			crtc_state->mode_changed = false;
			to_intel_crtc_state(crtc_state)->update_pipe = true;
		}

		if (needs_modeset(crtc_state))
			any_ms = true;

		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret)
			return ret;

		intel_dump_pipe_config(to_intel_crtc(crtc), pipe_config,
				       needs_modeset(crtc_state) ?
				       "[modeset]" : "[fastset]");
	}

	if (any_ms) {
		ret = intel_modeset_checks(state);

		if (ret)
			return ret;
	} else {
		intel_state->cdclk.logical = dev_priv->cdclk.logical;
	}

	ret = drm_atomic_helper_check_planes(dev, state);
	if (ret)
		return ret;

	intel_fbc_choose_crtc(dev_priv, state);
	return calc_watermark_data(state);
}

static int intel_atomic_prepare_commit(struct drm_device *dev,
				       struct drm_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int i, ret;

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		if (state->legacy_cursor_update)
			continue;

		ret = intel_crtc_wait_for_pending_flips(crtc);
		if (ret)
			return ret;

		if (atomic_read(&to_intel_crtc(crtc)->unpin_work_count) >= 2)
			flush_workqueue(dev_priv->wq);
	}

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

u32 intel_crtc_get_vblank_counter(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;

	if (!dev->max_vblank_count)
		return drm_accurate_vblank_count(&crtc->base);

	return dev->driver->get_vblank_counter(dev, crtc->pipe);
}

static void intel_atomic_wait_for_vblanks(struct drm_device *dev,
					  struct drm_i915_private *dev_priv,
					  unsigned crtc_mask)
{
	unsigned last_vblank_count[I915_MAX_PIPES];
	enum pipe pipe;
	int ret;

	if (!crtc_mask)
		return;

	for_each_pipe(dev_priv, pipe) {
		struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv,
								  pipe);

		if (!((1 << pipe) & crtc_mask))
			continue;

		ret = drm_crtc_vblank_get(&crtc->base);
		if (WARN_ON(ret != 0)) {
			crtc_mask &= ~(1 << pipe);
			continue;
		}

		last_vblank_count[pipe] = drm_crtc_vblank_count(&crtc->base);
	}

	for_each_pipe(dev_priv, pipe) {
		struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv,
								  pipe);
		long lret;

		if (!((1 << pipe) & crtc_mask))
			continue;

		lret = wait_event_timeout(dev->vblank[pipe].queue,
				last_vblank_count[pipe] !=
					drm_crtc_vblank_count(&crtc->base),
				msecs_to_jiffies(50));

		WARN(!lret, "pipe %c vblank wait timed out\n", pipe_name(pipe));

		drm_crtc_vblank_put(&crtc->base);
	}
}

static bool needs_vblank_wait(struct intel_crtc_state *crtc_state)
{
	/* fb updated, need to unpin old fb */
	if (crtc_state->fb_changed)
		return true;

	/* wm changes, need vblank before final wm's */
	if (crtc_state->update_wm_post)
		return true;

	/*
	 * cxsr is re-enabled after vblank.
	 * This is already handled by crtc_state->update_wm_post,
	 * but added for clarity.
	 */
	if (crtc_state->disable_cxsr)
		return true;

	return false;
}

static void intel_update_crtc(struct drm_crtc *crtc,
			      struct drm_atomic_state *state,
			      struct drm_crtc_state *old_crtc_state,
			      unsigned int *crtc_vblank_mask)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_crtc_state *pipe_config = to_intel_crtc_state(crtc->state);
	bool modeset = needs_modeset(crtc->state);

	if (modeset) {
		update_scanline_offset(intel_crtc);
		dev_priv->display.crtc_enable(pipe_config, state);
	} else {
		intel_pre_plane_update(to_intel_crtc_state(old_crtc_state));
	}

	if (drm_atomic_get_existing_plane_state(state, crtc->primary)) {
		intel_fbc_enable(
		    intel_crtc, pipe_config,
		    to_intel_plane_state(crtc->primary->state));
	}

	drm_atomic_helper_commit_planes_on_crtc(old_crtc_state);

	if (needs_vblank_wait(pipe_config))
		*crtc_vblank_mask |= drm_crtc_mask(crtc);
}

static void intel_update_crtcs(struct drm_atomic_state *state,
			       unsigned int *crtc_vblank_mask)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	int i;

	for_each_crtc_in_state(state, crtc, old_crtc_state, i) {
		if (!crtc->state->active)
			continue;

		intel_update_crtc(crtc, state, old_crtc_state,
				  crtc_vblank_mask);
	}
}

static void skl_update_crtcs(struct drm_atomic_state *state,
			     unsigned int *crtc_vblank_mask)
{
	struct drm_i915_private *dev_priv = to_i915(state->dev);
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);
	struct drm_crtc *crtc;
	struct intel_crtc *intel_crtc;
	struct drm_crtc_state *old_crtc_state;
	struct intel_crtc_state *cstate;
	unsigned int updated = 0;
	bool progress;
	enum pipe pipe;
	int i;

	const struct skl_ddb_entry *entries[I915_MAX_PIPES] = {};

	for_each_crtc_in_state(state, crtc, old_crtc_state, i)
		/* ignore allocations for crtc's that have been turned off. */
		if (crtc->state->active)
			entries[i] = &to_intel_crtc_state(old_crtc_state)->wm.skl.ddb;

	/*
	 * Whenever the number of active pipes changes, we need to make sure we
	 * update the pipes in the right order so that their ddb allocations
	 * never overlap with eachother inbetween CRTC updates. Otherwise we'll
	 * cause pipe underruns and other bad stuff.
	 */
	do {
		progress = false;

		for_each_crtc_in_state(state, crtc, old_crtc_state, i) {
			bool vbl_wait = false;
			unsigned int cmask = drm_crtc_mask(crtc);

			intel_crtc = to_intel_crtc(crtc);
			cstate = to_intel_crtc_state(crtc->state);
			pipe = intel_crtc->pipe;

			if (updated & cmask || !cstate->base.active)
				continue;

			if (skl_ddb_allocation_overlaps(entries, &cstate->wm.skl.ddb, i))
				continue;

			updated |= cmask;
			entries[i] = &cstate->wm.skl.ddb;

			/*
			 * If this is an already active pipe, it's DDB changed,
			 * and this isn't the last pipe that needs updating
			 * then we need to wait for a vblank to pass for the
			 * new ddb allocation to take effect.
			 */
			if (!skl_ddb_entry_equal(&cstate->wm.skl.ddb,
						 &to_intel_crtc_state(old_crtc_state)->wm.skl.ddb) &&
			    !crtc->state->active_changed &&
			    intel_state->wm_results.dirty_pipes != updated)
				vbl_wait = true;

			intel_update_crtc(crtc, state, old_crtc_state,
					  crtc_vblank_mask);

			if (vbl_wait)
				intel_wait_for_vblank(dev_priv, pipe);

			progress = true;
		}
	} while (progress);
}

static void intel_atomic_helper_free_state(struct drm_i915_private *dev_priv)
{
	struct intel_atomic_state *state, *next;
	struct llist_node *freed;

	freed = llist_del_all(&dev_priv->atomic_helper.free_list);
	llist_for_each_entry_safe(state, next, freed, freed)
		drm_atomic_state_put(&state->base);
}

static void intel_atomic_helper_free_state_worker(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), atomic_helper.free_work);

	intel_atomic_helper_free_state(dev_priv);
}

static void intel_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_crtc_state *old_crtc_state;
	struct drm_crtc *crtc;
	struct intel_crtc_state *intel_cstate;
	bool hw_check = intel_state->modeset;
	u64 put_domains[I915_MAX_PIPES] = {};
	unsigned crtc_vblank_mask = 0;
	int i;

	drm_atomic_helper_wait_for_dependencies(state);

	if (intel_state->modeset)
		intel_display_power_get(dev_priv, POWER_DOMAIN_MODESET);

	for_each_crtc_in_state(state, crtc, old_crtc_state, i) {
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

		if (needs_modeset(crtc->state) ||
		    to_intel_crtc_state(crtc->state)->update_pipe) {
			hw_check = true;

			put_domains[to_intel_crtc(crtc)->pipe] =
				modeset_get_crtc_power_domains(crtc,
					to_intel_crtc_state(crtc->state));
		}

		if (!needs_modeset(crtc->state))
			continue;

		intel_pre_plane_update(to_intel_crtc_state(old_crtc_state));

		if (old_crtc_state->active) {
			intel_crtc_disable_planes(crtc, old_crtc_state->plane_mask);
			dev_priv->display.crtc_disable(to_intel_crtc_state(old_crtc_state), state);
			intel_crtc->active = false;
			intel_fbc_disable(intel_crtc);
			intel_disable_shared_dpll(intel_crtc);

			/*
			 * Underruns don't always raise
			 * interrupts, so check manually.
			 */
			intel_check_cpu_fifo_underruns(dev_priv);
			intel_check_pch_fifo_underruns(dev_priv);

			if (!crtc->state->active) {
				/*
				 * Make sure we don't call initial_watermarks
				 * for ILK-style watermark updates.
				 */
				if (dev_priv->display.atomic_update_watermarks)
					dev_priv->display.initial_watermarks(intel_state,
									     to_intel_crtc_state(crtc->state));
				else
					intel_update_watermarks(intel_crtc);
			}
		}
	}

	/* Only after disabling all output pipelines that will be changed can we
	 * update the the output configuration. */
	intel_modeset_update_crtc_state(state);

	if (intel_state->modeset) {
		drm_atomic_helper_update_legacy_modeset_state(state->dev, state);

		intel_set_cdclk(dev_priv, &dev_priv->cdclk.actual);

		/*
		 * SKL workaround: bspec recommends we disable the SAGV when we
		 * have more then one pipe enabled
		 */
		if (!intel_can_enable_sagv(state))
			intel_disable_sagv(dev_priv);

		intel_modeset_verify_disabled(dev, state);
	}

	/* Complete the events for pipes that have now been disabled */
	for_each_crtc_in_state(state, crtc, old_crtc_state, i) {
		bool modeset = needs_modeset(crtc->state);

		/* Complete events for now disable pipes here. */
		if (modeset && !crtc->state->active && crtc->state->event) {
			spin_lock_irq(&dev->event_lock);
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
			spin_unlock_irq(&dev->event_lock);

			crtc->state->event = NULL;
		}
	}

	/* Now enable the clocks, plane, pipe, and connectors that we set up. */
	dev_priv->display.update_crtcs(state, &crtc_vblank_mask);

	/* FIXME: We should call drm_atomic_helper_commit_hw_done() here
	 * already, but still need the state for the delayed optimization. To
	 * fix this:
	 * - wrap the optimization/post_plane_update stuff into a per-crtc work.
	 * - schedule that vblank worker _before_ calling hw_done
	 * - at the start of commit_tail, cancel it _synchrously
	 * - switch over to the vblank wait helper in the core after that since
	 *   we don't need out special handling any more.
	 */
	if (!state->legacy_cursor_update)
		intel_atomic_wait_for_vblanks(dev, dev_priv, crtc_vblank_mask);

	/*
	 * Now that the vblank has passed, we can go ahead and program the
	 * optimal watermarks on platforms that need two-step watermark
	 * programming.
	 *
	 * TODO: Move this (and other cleanup) to an async worker eventually.
	 */
	for_each_crtc_in_state(state, crtc, old_crtc_state, i) {
		intel_cstate = to_intel_crtc_state(crtc->state);

		if (dev_priv->display.optimize_watermarks)
			dev_priv->display.optimize_watermarks(intel_state,
							      intel_cstate);
	}

	for_each_crtc_in_state(state, crtc, old_crtc_state, i) {
		intel_post_plane_update(to_intel_crtc_state(old_crtc_state));

		if (put_domains[i])
			modeset_put_power_domains(dev_priv, put_domains[i]);

		intel_modeset_verify_crtc(crtc, state, old_crtc_state, crtc->state);
	}

	if (intel_state->modeset && intel_can_enable_sagv(state))
		intel_enable_sagv(dev_priv);

	drm_atomic_helper_commit_hw_done(state);

	if (intel_state->modeset)
		intel_display_power_put(dev_priv, POWER_DOMAIN_MODESET);

	mutex_lock(&dev->struct_mutex);
	drm_atomic_helper_cleanup_planes(dev, state);
	mutex_unlock(&dev->struct_mutex);

	drm_atomic_helper_commit_cleanup_done(state);

	drm_atomic_state_put(state);

	/* As one of the primary mmio accessors, KMS has a high likelihood
	 * of triggering bugs in unclaimed access. After we finish
	 * modesetting, see if an error has been flagged, and if so
	 * enable debugging for the next modeset - and hope we catch
	 * the culprit.
	 *
	 * XXX note that we assume display power is on at this point.
	 * This might hold true now but we need to add pm helper to check
	 * unclaimed only when the hardware is on, as atomic commits
	 * can happen also when the device is completely off.
	 */
	intel_uncore_arm_unclaimed_mmio_detection(dev_priv);

	intel_atomic_helper_free_state(dev_priv);
}

static void intel_atomic_commit_work(struct work_struct *work)
{
	struct drm_atomic_state *state =
		container_of(work, struct drm_atomic_state, commit_work);

	intel_atomic_commit_tail(state);
}

static int __i915_sw_fence_call
intel_atomic_commit_ready(struct i915_sw_fence *fence,
			  enum i915_sw_fence_notify notify)
{
	struct intel_atomic_state *state =
		container_of(fence, struct intel_atomic_state, commit_ready);

	switch (notify) {
	case FENCE_COMPLETE:
		if (state->base.commit_work.func)
			queue_work(system_unbound_wq, &state->base.commit_work);
		break;

	case FENCE_FREE:
		{
			struct intel_atomic_helper *helper =
				&to_i915(state->base.dev)->atomic_helper;

			if (llist_add(&state->freed, &helper->free_list))
				schedule_work(&helper->free_work);
			break;
		}
	}

	return NOTIFY_DONE;
}

static void intel_atomic_track_fbs(struct drm_atomic_state *state)
{
	struct drm_plane_state *old_plane_state;
	struct drm_plane *plane;
	int i;

	for_each_plane_in_state(state, plane, old_plane_state, i)
		i915_gem_track_fb(intel_fb_obj(old_plane_state->fb),
				  intel_fb_obj(plane->state->fb),
				  to_intel_plane(plane)->frontbuffer_bit);
}

/**
 * intel_atomic_commit - commit validated state object
 * @dev: DRM device
 * @state: the top-level driver state object
 * @nonblock: nonblocking commit
 *
 * This function commits a top-level state object that has been validated
 * with drm_atomic_helper_check().
 *
 * RETURNS
 * Zero for success or -errno.
 */
static int intel_atomic_commit(struct drm_device *dev,
			       struct drm_atomic_state *state,
			       bool nonblock)
{
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);
	struct drm_i915_private *dev_priv = to_i915(dev);
	int ret = 0;

	/*
	 * The intel_legacy_cursor_update() fast path takes care
	 * of avoiding the vblank waits for simple cursor
	 * movement and flips. For cursor on/off and size changes,
	 * we want to perform the vblank waits so that watermark
	 * updates happen during the correct frames. Gen9+ have
	 * double buffered watermarks and so shouldn't need this.
	 */
	if (INTEL_GEN(dev_priv) < 9)
		state->legacy_cursor_update = false;

	ret = drm_atomic_helper_setup_commit(state, nonblock);
	if (ret)
		return ret;

	drm_atomic_state_get(state);
	i915_sw_fence_init(&intel_state->commit_ready,
			   intel_atomic_commit_ready);

	ret = intel_atomic_prepare_commit(dev, state);
	if (ret) {
		DRM_DEBUG_ATOMIC("Preparing state failed with %i\n", ret);
		i915_sw_fence_commit(&intel_state->commit_ready);
		return ret;
	}

	drm_atomic_helper_swap_state(state, true);
	dev_priv->wm.distrust_bios_wm = false;
	intel_shared_dpll_swap_state(state);
	intel_atomic_track_fbs(state);

	if (intel_state->modeset) {
		memcpy(dev_priv->min_pixclk, intel_state->min_pixclk,
		       sizeof(intel_state->min_pixclk));
		dev_priv->active_crtcs = intel_state->active_crtcs;
		dev_priv->cdclk.logical = intel_state->cdclk.logical;
		dev_priv->cdclk.actual = intel_state->cdclk.actual;
	}

	drm_atomic_state_get(state);
	INIT_WORK(&state->commit_work,
		  nonblock ? intel_atomic_commit_work : NULL);

	i915_sw_fence_commit(&intel_state->commit_ready);
	if (!nonblock) {
		i915_sw_fence_wait(&intel_state->commit_ready);
		intel_atomic_commit_tail(state);
	}

	return 0;
}

void intel_crtc_restore_mode(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	int ret;

	state = drm_atomic_state_alloc(dev);
	if (!state) {
		DRM_DEBUG_KMS("[CRTC:%d:%s] crtc restore failed, out of memory",
			      crtc->base.id, crtc->name);
		return;
	}

	state->acquire_ctx = drm_modeset_legacy_acquire_ctx(crtc);

retry:
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	ret = PTR_ERR_OR_ZERO(crtc_state);
	if (!ret) {
		if (!crtc_state->active)
			goto out;

		crtc_state->mode_changed = true;
		ret = drm_atomic_commit(state);
	}

	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		drm_modeset_backoff(state->acquire_ctx);
		goto retry;
	}

out:
	drm_atomic_state_put(state);
}

/*
 * FIXME: Remove this once i915 is fully DRIVER_ATOMIC by calling
 *        drm_atomic_helper_legacy_gamma_set() directly.
 */
static int intel_atomic_legacy_gamma_set(struct drm_crtc *crtc,
					 u16 *red, u16 *green, u16 *blue,
					 uint32_t size)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_crtc_state *state;
	int ret;

	ret = drm_atomic_helper_legacy_gamma_set(crtc, red, green, blue, size);
	if (ret)
		return ret;

	/*
	 * Make sure we update the legacy properties so this works when
	 * atomic is not enabled.
	 */

	state = crtc->state;

	drm_object_property_set_value(&crtc->base,
				      config->degamma_lut_property,
				      (state->degamma_lut) ?
				      state->degamma_lut->base.id : 0);

	drm_object_property_set_value(&crtc->base,
				      config->ctm_property,
				      (state->ctm) ?
				      state->ctm->base.id : 0);

	drm_object_property_set_value(&crtc->base,
				      config->gamma_lut_property,
				      (state->gamma_lut) ?
				      state->gamma_lut->base.id : 0);

	return 0;
}

static const struct drm_crtc_funcs intel_crtc_funcs = {
	.gamma_set = intel_atomic_legacy_gamma_set,
	.set_config = drm_atomic_helper_set_config,
	.set_property = drm_atomic_helper_crtc_set_property,
	.destroy = intel_crtc_destroy,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = intel_crtc_duplicate_state,
	.atomic_destroy_state = intel_crtc_destroy_state,
	.set_crc_source = intel_crtc_set_crc_source,
};

/**
 * intel_prepare_plane_fb - Prepare fb for usage on plane
 * @plane: drm plane to prepare for
 * @fb: framebuffer to prepare for presentation
 *
 * Prepares a framebuffer for usage on a display plane.  Generally this
 * involves pinning the underlying object and updating the frontbuffer tracking
 * bits.  Some older platforms need special physical address handling for
 * cursor planes.
 *
 * Must be called with struct_mutex held.
 *
 * Returns 0 on success, negative error code on failure.
 */
int
intel_prepare_plane_fb(struct drm_plane *plane,
		       struct drm_plane_state *new_state)
{
	struct intel_atomic_state *intel_state =
		to_intel_atomic_state(new_state->state);
	struct drm_i915_private *dev_priv = to_i915(plane->dev);
	struct drm_framebuffer *fb = new_state->fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct drm_i915_gem_object *old_obj = intel_fb_obj(plane->state->fb);
	int ret;

	if (obj) {
		if (plane->type == DRM_PLANE_TYPE_CURSOR &&
		    INTEL_INFO(dev_priv)->cursor_needs_physical) {
			const int align = IS_I830(dev_priv) ? 16 * 1024 : 256;

			ret = i915_gem_object_attach_phys(obj, align);
			if (ret) {
				DRM_DEBUG_KMS("failed to attach phys object\n");
				return ret;
			}
		} else {
			struct i915_vma *vma;

			vma = intel_pin_and_fence_fb_obj(fb, new_state->rotation);
			if (IS_ERR(vma)) {
				DRM_DEBUG_KMS("failed to pin object\n");
				return PTR_ERR(vma);
			}

			to_intel_plane_state(new_state)->vma = vma;
		}
	}

	if (!obj && !old_obj)
		return 0;

	if (old_obj) {
		struct drm_crtc_state *crtc_state =
			drm_atomic_get_existing_crtc_state(new_state->state,
							   plane->state->crtc);

		/* Big Hammer, we also need to ensure that any pending
		 * MI_WAIT_FOR_EVENT inside a user batch buffer on the
		 * current scanout is retired before unpinning the old
		 * framebuffer. Note that we rely on userspace rendering
		 * into the buffer attached to the pipe they are waiting
		 * on. If not, userspace generates a GPU hang with IPEHR
		 * point to the MI_WAIT_FOR_EVENT.
		 *
		 * This should only fail upon a hung GPU, in which case we
		 * can safely continue.
		 */
		if (needs_modeset(crtc_state)) {
			ret = i915_sw_fence_await_reservation(&intel_state->commit_ready,
							      old_obj->resv, NULL,
							      false, 0,
							      GFP_KERNEL);
			if (ret < 0)
				return ret;
		}
	}

	if (new_state->fence) { /* explicit fencing */
		ret = i915_sw_fence_await_dma_fence(&intel_state->commit_ready,
						    new_state->fence,
						    I915_FENCE_TIMEOUT,
						    GFP_KERNEL);
		if (ret < 0)
			return ret;
	}

	if (!obj)
		return 0;

	if (!new_state->fence) { /* implicit fencing */
		ret = i915_sw_fence_await_reservation(&intel_state->commit_ready,
						      obj->resv, NULL,
						      false, I915_FENCE_TIMEOUT,
						      GFP_KERNEL);
		if (ret < 0)
			return ret;

		i915_gem_object_wait_priority(obj, 0, I915_PRIORITY_DISPLAY);
	}

	return 0;
}

/**
 * intel_cleanup_plane_fb - Cleans up an fb after plane use
 * @plane: drm plane to clean up for
 * @fb: old framebuffer that was on plane
 *
 * Cleans up a framebuffer that has just been removed from a plane.
 *
 * Must be called with struct_mutex held.
 */
void
intel_cleanup_plane_fb(struct drm_plane *plane,
		       struct drm_plane_state *old_state)
{
	struct i915_vma *vma;

	/* Should only be called after a successful intel_prepare_plane_fb()! */
	vma = fetch_and_zero(&to_intel_plane_state(old_state)->vma);
	if (vma)
		intel_unpin_fb_vma(vma);
}

int
skl_max_scale(struct intel_crtc *intel_crtc, struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv;
	int max_scale;
	int crtc_clock, max_dotclk;

	if (!intel_crtc || !crtc_state->base.enable)
		return DRM_PLANE_HELPER_NO_SCALING;

	dev_priv = to_i915(intel_crtc->base.dev);

	crtc_clock = crtc_state->base.adjusted_mode.crtc_clock;
	max_dotclk = to_intel_atomic_state(crtc_state->base.state)->cdclk.logical.cdclk;

	if (IS_GEMINILAKE(dev_priv))
		max_dotclk *= 2;

	if (WARN_ON_ONCE(!crtc_clock || max_dotclk < crtc_clock))
		return DRM_PLANE_HELPER_NO_SCALING;

	/*
	 * skl max scale is lower of:
	 *    close to 3 but not 3, -1 is for that purpose
	 *            or
	 *    cdclk/crtc_clock
	 */
	max_scale = min((1 << 16) * 3 - 1,
			(1 << 8) * ((max_dotclk << 8) / crtc_clock));

	return max_scale;
}

static int
intel_check_primary_plane(struct drm_plane *plane,
			  struct intel_crtc_state *crtc_state,
			  struct intel_plane_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->dev);
	struct drm_crtc *crtc = state->base.crtc;
	int min_scale = DRM_PLANE_HELPER_NO_SCALING;
	int max_scale = DRM_PLANE_HELPER_NO_SCALING;
	bool can_position = false;
	int ret;

	if (INTEL_GEN(dev_priv) >= 9) {
		/* use scaler when colorkey is not required */
		if (state->ckey.flags == I915_SET_COLORKEY_NONE) {
			min_scale = 1;
			max_scale = skl_max_scale(to_intel_crtc(crtc), crtc_state);
		}
		can_position = true;
	}

	ret = drm_plane_helper_check_state(&state->base,
					   &state->clip,
					   min_scale, max_scale,
					   can_position, true);
	if (ret)
		return ret;

	if (!state->base.fb)
		return 0;

	if (INTEL_GEN(dev_priv) >= 9) {
		ret = skl_check_plane_surface(state);
		if (ret)
			return ret;
	}

	return 0;
}

static void intel_begin_crtc_commit(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_crtc_state *intel_cstate =
		to_intel_crtc_state(crtc->state);
	struct intel_crtc_state *old_intel_cstate =
		to_intel_crtc_state(old_crtc_state);
	struct intel_atomic_state *old_intel_state =
		to_intel_atomic_state(old_crtc_state->state);
	bool modeset = needs_modeset(crtc->state);

	/* Perform vblank evasion around commit operation */
	intel_pipe_update_start(intel_crtc);

	if (modeset)
		goto out;

	if (crtc->state->color_mgmt_changed || to_intel_crtc_state(crtc->state)->update_pipe) {
		intel_color_set_csc(crtc->state);
		intel_color_load_luts(crtc->state);
	}

	if (intel_cstate->update_pipe)
		intel_update_pipe_config(intel_crtc, old_intel_cstate);
	else if (INTEL_GEN(dev_priv) >= 9)
		skl_detach_scalers(intel_crtc);

out:
	if (dev_priv->display.atomic_update_watermarks)
		dev_priv->display.atomic_update_watermarks(old_intel_state,
							   intel_cstate);
}

static void intel_finish_crtc_commit(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_crtc_state)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	intel_pipe_update_end(intel_crtc, NULL);
}

/**
 * intel_plane_destroy - destroy a plane
 * @plane: plane to destroy
 *
 * Common destruction function for all types of planes (primary, cursor,
 * sprite).
 */
void intel_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);
	kfree(to_intel_plane(plane));
}

const struct drm_plane_funcs intel_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.set_property = drm_atomic_helper_plane_set_property,
	.atomic_get_property = intel_plane_atomic_get_property,
	.atomic_set_property = intel_plane_atomic_set_property,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
};

static int
intel_legacy_cursor_update(struct drm_plane *plane,
			   struct drm_crtc *crtc,
			   struct drm_framebuffer *fb,
			   int crtc_x, int crtc_y,
			   unsigned int crtc_w, unsigned int crtc_h,
			   uint32_t src_x, uint32_t src_y,
			   uint32_t src_w, uint32_t src_h)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	int ret;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct drm_framebuffer *old_fb;
	struct drm_crtc_state *crtc_state = crtc->state;
	struct i915_vma *old_vma;

	/*
	 * When crtc is inactive or there is a modeset pending,
	 * wait for it to complete in the slowpath
	 */
	if (!crtc_state->active || needs_modeset(crtc_state) ||
	    to_intel_crtc_state(crtc_state)->update_pipe)
		goto slow;

	old_plane_state = plane->state;

	/*
	 * If any parameters change that may affect watermarks,
	 * take the slowpath. Only changing fb or position should be
	 * in the fastpath.
	 */
	if (old_plane_state->crtc != crtc ||
	    old_plane_state->src_w != src_w ||
	    old_plane_state->src_h != src_h ||
	    old_plane_state->crtc_w != crtc_w ||
	    old_plane_state->crtc_h != crtc_h ||
	    !old_plane_state->fb != !fb)
		goto slow;

	new_plane_state = intel_plane_duplicate_state(plane);
	if (!new_plane_state)
		return -ENOMEM;

	drm_atomic_set_fb_for_plane(new_plane_state, fb);

	new_plane_state->src_x = src_x;
	new_plane_state->src_y = src_y;
	new_plane_state->src_w = src_w;
	new_plane_state->src_h = src_h;
	new_plane_state->crtc_x = crtc_x;
	new_plane_state->crtc_y = crtc_y;
	new_plane_state->crtc_w = crtc_w;
	new_plane_state->crtc_h = crtc_h;

	ret = intel_plane_atomic_check_with_state(to_intel_crtc_state(crtc->state),
						  to_intel_plane_state(new_plane_state));
	if (ret)
		goto out_free;

	ret = mutex_lock_interruptible(&dev_priv->drm.struct_mutex);
	if (ret)
		goto out_free;

	if (INTEL_INFO(dev_priv)->cursor_needs_physical) {
		int align = IS_I830(dev_priv) ? 16 * 1024 : 256;

		ret = i915_gem_object_attach_phys(intel_fb_obj(fb), align);
		if (ret) {
			DRM_DEBUG_KMS("failed to attach phys object\n");
			goto out_unlock;
		}
	} else {
		struct i915_vma *vma;

		vma = intel_pin_and_fence_fb_obj(fb, new_plane_state->rotation);
		if (IS_ERR(vma)) {
			DRM_DEBUG_KMS("failed to pin object\n");

			ret = PTR_ERR(vma);
			goto out_unlock;
		}

		to_intel_plane_state(new_plane_state)->vma = vma;
	}

	old_fb = old_plane_state->fb;
	old_vma = to_intel_plane_state(old_plane_state)->vma;

	i915_gem_track_fb(intel_fb_obj(old_fb), intel_fb_obj(fb),
			  intel_plane->frontbuffer_bit);

	/* Swap plane state */
	new_plane_state->fence = old_plane_state->fence;
	*to_intel_plane_state(old_plane_state) = *to_intel_plane_state(new_plane_state);
	new_plane_state->fence = NULL;
	new_plane_state->fb = old_fb;
	to_intel_plane_state(new_plane_state)->vma = old_vma;

	if (plane->state->visible)
		intel_plane->update_plane(plane,
					  to_intel_crtc_state(crtc->state),
					  to_intel_plane_state(plane->state));
	else
		intel_plane->disable_plane(plane, crtc);

	intel_cleanup_plane_fb(plane, new_plane_state);

out_unlock:
	mutex_unlock(&dev_priv->drm.struct_mutex);
out_free:
	intel_plane_destroy_state(plane, new_plane_state);
	return ret;

slow:
	return drm_atomic_helper_update_plane(plane, crtc, fb,
					      crtc_x, crtc_y, crtc_w, crtc_h,
					      src_x, src_y, src_w, src_h);
}

static const struct drm_plane_funcs intel_cursor_plane_funcs = {
	.update_plane = intel_legacy_cursor_update,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.set_property = drm_atomic_helper_plane_set_property,
	.atomic_get_property = intel_plane_atomic_get_property,
	.atomic_set_property = intel_plane_atomic_set_property,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
};

static struct intel_plane *
intel_primary_plane_create(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	struct intel_plane *primary = NULL;
	struct intel_plane_state *state = NULL;
	const uint32_t *intel_primary_formats;
	unsigned int supported_rotations;
	unsigned int num_formats;
	int ret;

	primary = kzalloc(sizeof(*primary), GFP_KERNEL);
	if (!primary) {
		ret = -ENOMEM;
		goto fail;
	}

	state = intel_create_plane_state(&primary->base);
	if (!state) {
		ret = -ENOMEM;
		goto fail;
	}

	primary->base.state = &state->base;

	primary->can_scale = false;
	primary->max_downscale = 1;
	if (INTEL_GEN(dev_priv) >= 9) {
		primary->can_scale = true;
		state->scaler_id = -1;
	}
	primary->pipe = pipe;
	/*
	 * On gen2/3 only plane A can do FBC, but the panel fitter and LVDS
	 * port is hooked to pipe B. Hence we want plane A feeding pipe B.
	 */
	if (HAS_FBC(dev_priv) && INTEL_GEN(dev_priv) < 4)
		primary->plane = (enum plane) !pipe;
	else
		primary->plane = (enum plane) pipe;
	primary->id = PLANE_PRIMARY;
	primary->frontbuffer_bit = INTEL_FRONTBUFFER_PRIMARY(pipe);
	primary->check_plane = intel_check_primary_plane;

	if (INTEL_GEN(dev_priv) >= 9) {
		intel_primary_formats = skl_primary_formats;
		num_formats = ARRAY_SIZE(skl_primary_formats);

		primary->update_plane = skylake_update_primary_plane;
		primary->disable_plane = skylake_disable_primary_plane;
	} else if (HAS_PCH_SPLIT(dev_priv)) {
		intel_primary_formats = i965_primary_formats;
		num_formats = ARRAY_SIZE(i965_primary_formats);

		primary->update_plane = ironlake_update_primary_plane;
		primary->disable_plane = i9xx_disable_primary_plane;
	} else if (INTEL_GEN(dev_priv) >= 4) {
		intel_primary_formats = i965_primary_formats;
		num_formats = ARRAY_SIZE(i965_primary_formats);

		primary->update_plane = i9xx_update_primary_plane;
		primary->disable_plane = i9xx_disable_primary_plane;
	} else {
		intel_primary_formats = i8xx_primary_formats;
		num_formats = ARRAY_SIZE(i8xx_primary_formats);

		primary->update_plane = i9xx_update_primary_plane;
		primary->disable_plane = i9xx_disable_primary_plane;
	}

	if (INTEL_GEN(dev_priv) >= 9)
		ret = drm_universal_plane_init(&dev_priv->drm, &primary->base,
					       0, &intel_plane_funcs,
					       intel_primary_formats, num_formats,
					       DRM_PLANE_TYPE_PRIMARY,
					       "plane 1%c", pipe_name(pipe));
	else if (INTEL_GEN(dev_priv) >= 5 || IS_G4X(dev_priv))
		ret = drm_universal_plane_init(&dev_priv->drm, &primary->base,
					       0, &intel_plane_funcs,
					       intel_primary_formats, num_formats,
					       DRM_PLANE_TYPE_PRIMARY,
					       "primary %c", pipe_name(pipe));
	else
		ret = drm_universal_plane_init(&dev_priv->drm, &primary->base,
					       0, &intel_plane_funcs,
					       intel_primary_formats, num_formats,
					       DRM_PLANE_TYPE_PRIMARY,
					       "plane %c", plane_name(primary->plane));
	if (ret)
		goto fail;

	if (INTEL_GEN(dev_priv) >= 9) {
		supported_rotations =
			DRM_ROTATE_0 | DRM_ROTATE_90 |
			DRM_ROTATE_180 | DRM_ROTATE_270;
	} else if (IS_CHERRYVIEW(dev_priv) && pipe == PIPE_B) {
		supported_rotations =
			DRM_ROTATE_0 | DRM_ROTATE_180 |
			DRM_REFLECT_X;
	} else if (INTEL_GEN(dev_priv) >= 4) {
		supported_rotations =
			DRM_ROTATE_0 | DRM_ROTATE_180;
	} else {
		supported_rotations = DRM_ROTATE_0;
	}

	if (INTEL_GEN(dev_priv) >= 4)
		drm_plane_create_rotation_property(&primary->base,
						   DRM_ROTATE_0,
						   supported_rotations);

	drm_plane_helper_add(&primary->base, &intel_plane_helper_funcs);

	return primary;

fail:
	kfree(state);
	kfree(primary);

	return ERR_PTR(ret);
}

static int
intel_check_cursor_plane(struct drm_plane *plane,
			 struct intel_crtc_state *crtc_state,
			 struct intel_plane_state *state)
{
	struct drm_framebuffer *fb = state->base.fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	enum pipe pipe = to_intel_plane(plane)->pipe;
	unsigned stride;
	int ret;

	ret = drm_plane_helper_check_state(&state->base,
					   &state->clip,
					   DRM_PLANE_HELPER_NO_SCALING,
					   DRM_PLANE_HELPER_NO_SCALING,
					   true, true);
	if (ret)
		return ret;

	/* if we want to turn off the cursor ignore width and height */
	if (!obj)
		return 0;

	/* Check for which cursor types we support */
	if (!cursor_size_ok(to_i915(plane->dev), state->base.crtc_w,
			    state->base.crtc_h)) {
		DRM_DEBUG("Cursor dimension %dx%d not supported\n",
			  state->base.crtc_w, state->base.crtc_h);
		return -EINVAL;
	}

	stride = roundup_pow_of_two(state->base.crtc_w) * 4;
	if (obj->base.size < stride * state->base.crtc_h) {
		DRM_DEBUG_KMS("buffer is too small\n");
		return -ENOMEM;
	}

	if (fb->modifier != DRM_FORMAT_MOD_NONE) {
		DRM_DEBUG_KMS("cursor cannot be tiled\n");
		return -EINVAL;
	}

	/*
	 * There's something wrong with the cursor on CHV pipe C.
	 * If it straddles the left edge of the screen then
	 * moving it away from the edge or disabling it often
	 * results in a pipe underrun, and often that can lead to
	 * dead pipe (constant underrun reported, and it scans
	 * out just a solid color). To recover from that, the
	 * display power well must be turned off and on again.
	 * Refuse the put the cursor into that compromised position.
	 */
	if (IS_CHERRYVIEW(to_i915(plane->dev)) && pipe == PIPE_C &&
	    state->base.visible && state->base.crtc_x < 0) {
		DRM_DEBUG_KMS("CHV cursor C not allowed to straddle the left screen edge\n");
		return -EINVAL;
	}

	return 0;
}

static void
intel_disable_cursor_plane(struct drm_plane *plane,
			   struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	intel_crtc->cursor_addr = 0;
	intel_crtc_update_cursor(crtc, NULL);
}

static void
intel_update_cursor_plane(struct drm_plane *plane,
			  const struct intel_crtc_state *crtc_state,
			  const struct intel_plane_state *state)
{
	struct drm_crtc *crtc = crtc_state->base.crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_private *dev_priv = to_i915(plane->dev);
	struct drm_i915_gem_object *obj = intel_fb_obj(state->base.fb);
	uint32_t addr;

	if (!obj)
		addr = 0;
	else if (!INTEL_INFO(dev_priv)->cursor_needs_physical)
		addr = intel_plane_ggtt_offset(state);
	else
		addr = obj->phys_handle->busaddr;

	intel_crtc->cursor_addr = addr;
	intel_crtc_update_cursor(crtc, state);
}

static struct intel_plane *
intel_cursor_plane_create(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	struct intel_plane *cursor = NULL;
	struct intel_plane_state *state = NULL;
	int ret;

	cursor = kzalloc(sizeof(*cursor), GFP_KERNEL);
	if (!cursor) {
		ret = -ENOMEM;
		goto fail;
	}

	state = intel_create_plane_state(&cursor->base);
	if (!state) {
		ret = -ENOMEM;
		goto fail;
	}

	cursor->base.state = &state->base;

	cursor->can_scale = false;
	cursor->max_downscale = 1;
	cursor->pipe = pipe;
	cursor->plane = pipe;
	cursor->id = PLANE_CURSOR;
	cursor->frontbuffer_bit = INTEL_FRONTBUFFER_CURSOR(pipe);
	cursor->check_plane = intel_check_cursor_plane;
	cursor->update_plane = intel_update_cursor_plane;
	cursor->disable_plane = intel_disable_cursor_plane;

	ret = drm_universal_plane_init(&dev_priv->drm, &cursor->base,
				       0, &intel_cursor_plane_funcs,
				       intel_cursor_formats,
				       ARRAY_SIZE(intel_cursor_formats),
				       DRM_PLANE_TYPE_CURSOR,
				       "cursor %c", pipe_name(pipe));
	if (ret)
		goto fail;

	if (INTEL_GEN(dev_priv) >= 4)
		drm_plane_create_rotation_property(&cursor->base,
						   DRM_ROTATE_0,
						   DRM_ROTATE_0 |
						   DRM_ROTATE_180);

	if (INTEL_GEN(dev_priv) >= 9)
		state->scaler_id = -1;

	drm_plane_helper_add(&cursor->base, &intel_plane_helper_funcs);

	return cursor;

fail:
	kfree(state);
	kfree(cursor);

	return ERR_PTR(ret);
}

static void intel_crtc_init_scalers(struct intel_crtc *crtc,
				    struct intel_crtc_state *crtc_state)
{
	struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	int i;

	crtc->num_scalers = dev_priv->info.num_scalers[crtc->pipe];
	if (!crtc->num_scalers)
		return;

	for (i = 0; i < crtc->num_scalers; i++) {
		struct intel_scaler *scaler = &scaler_state->scalers[i];

		scaler->in_use = 0;
		scaler->mode = PS_SCALER_MODE_DYN;
	}

	scaler_state->scaler_id = -1;
}

static int intel_crtc_init(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	struct intel_crtc *intel_crtc;
	struct intel_crtc_state *crtc_state = NULL;
	struct intel_plane *primary = NULL;
	struct intel_plane *cursor = NULL;
	int sprite, ret;

	intel_crtc = kzalloc(sizeof(*intel_crtc), GFP_KERNEL);
	if (!intel_crtc)
		return -ENOMEM;

	crtc_state = kzalloc(sizeof(*crtc_state), GFP_KERNEL);
	if (!crtc_state) {
		ret = -ENOMEM;
		goto fail;
	}
	intel_crtc->config = crtc_state;
	intel_crtc->base.state = &crtc_state->base;
	crtc_state->base.crtc = &intel_crtc->base;

	primary = intel_primary_plane_create(dev_priv, pipe);
	if (IS_ERR(primary)) {
		ret = PTR_ERR(primary);
		goto fail;
	}
	intel_crtc->plane_ids_mask |= BIT(primary->id);

	for_each_sprite(dev_priv, pipe, sprite) {
		struct intel_plane *plane;

		plane = intel_sprite_plane_create(dev_priv, pipe, sprite);
		if (IS_ERR(plane)) {
			ret = PTR_ERR(plane);
			goto fail;
		}
		intel_crtc->plane_ids_mask |= BIT(plane->id);
	}

	cursor = intel_cursor_plane_create(dev_priv, pipe);
	if (IS_ERR(cursor)) {
		ret = PTR_ERR(cursor);
		goto fail;
	}
	intel_crtc->plane_ids_mask |= BIT(cursor->id);

	ret = drm_crtc_init_with_planes(&dev_priv->drm, &intel_crtc->base,
					&primary->base, &cursor->base,
					&intel_crtc_funcs,
					"pipe %c", pipe_name(pipe));
	if (ret)
		goto fail;

	intel_crtc->pipe = pipe;
	intel_crtc->plane = primary->plane;

	intel_crtc->cursor_base = ~0;
	intel_crtc->cursor_cntl = ~0;
	intel_crtc->cursor_size = ~0;

	intel_crtc->wm.cxsr_allowed = true;

	/* initialize shared scalers */
	intel_crtc_init_scalers(intel_crtc, crtc_state);

	BUG_ON(pipe >= ARRAY_SIZE(dev_priv->plane_to_crtc_mapping) ||
	       dev_priv->plane_to_crtc_mapping[intel_crtc->plane] != NULL);
	dev_priv->plane_to_crtc_mapping[intel_crtc->plane] = intel_crtc;
	dev_priv->pipe_to_crtc_mapping[intel_crtc->pipe] = intel_crtc;

	drm_crtc_helper_add(&intel_crtc->base, &intel_helper_funcs);

	intel_color_init(&intel_crtc->base);

	WARN_ON(drm_crtc_index(&intel_crtc->base) != intel_crtc->pipe);

	return 0;

fail:
	/*
	 * drm_mode_config_cleanup() will free up any
	 * crtcs/planes already initialized.
	 */
	kfree(crtc_state);
	kfree(intel_crtc);

	return ret;
}

enum pipe intel_get_pipe_from_connector(struct intel_connector *connector)
{
	struct drm_encoder *encoder = connector->base.encoder;
	struct drm_device *dev = connector->base.dev;

	WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));

	if (!encoder || WARN_ON(!encoder->crtc))
		return INVALID_PIPE;

	return to_intel_crtc(encoder->crtc)->pipe;
}

int intel_get_pipe_from_crtc_id(struct drm_device *dev, void *data,
				struct drm_file *file)
{
	struct drm_i915_get_pipe_from_crtc_id *pipe_from_crtc_id = data;
	struct drm_crtc *drmmode_crtc;
	struct intel_crtc *crtc;

	drmmode_crtc = drm_crtc_find(dev, pipe_from_crtc_id->crtc_id);
	if (!drmmode_crtc)
		return -ENOENT;

	crtc = to_intel_crtc(drmmode_crtc);
	pipe_from_crtc_id->pipe = crtc->pipe;

	return 0;
}

static int intel_encoder_clones(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct intel_encoder *source_encoder;
	int index_mask = 0;
	int entry = 0;

	for_each_intel_encoder(dev, source_encoder) {
		if (encoders_cloneable(encoder, source_encoder))
			index_mask |= (1 << entry);

		entry++;
	}

	return index_mask;
}

static bool has_edp_a(struct drm_i915_private *dev_priv)
{
	if (!IS_MOBILE(dev_priv))
		return false;

	if ((I915_READ(DP_A) & DP_DETECTED) == 0)
		return false;

	if (IS_GEN5(dev_priv) && (I915_READ(FUSE_STRAP) & ILK_eDP_A_DISABLE))
		return false;

	return true;
}

static bool intel_crt_present(struct drm_i915_private *dev_priv)
{
	if (INTEL_GEN(dev_priv) >= 9)
		return false;

	if (IS_HSW_ULT(dev_priv) || IS_BDW_ULT(dev_priv))
		return false;

	if (IS_CHERRYVIEW(dev_priv))
		return false;

	if (HAS_PCH_LPT_H(dev_priv) &&
	    I915_READ(SFUSE_STRAP) & SFUSE_STRAP_CRT_DISABLED)
		return false;

	/* DDI E can't be used if DDI A requires 4 lanes */
	if (HAS_DDI(dev_priv) && I915_READ(DDI_BUF_CTL(PORT_A)) & DDI_A_4_LANES)
		return false;

	if (!dev_priv->vbt.int_crt_support)
		return false;

	return true;
}

void intel_pps_unlock_regs_wa(struct drm_i915_private *dev_priv)
{
	int pps_num;
	int pps_idx;

	if (HAS_DDI(dev_priv))
		return;
	/*
	 * This w/a is needed at least on CPT/PPT, but to be sure apply it
	 * everywhere where registers can be write protected.
	 */
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		pps_num = 2;
	else
		pps_num = 1;

	for (pps_idx = 0; pps_idx < pps_num; pps_idx++) {
		u32 val = I915_READ(PP_CONTROL(pps_idx));

		val = (val & ~PANEL_UNLOCK_MASK) | PANEL_UNLOCK_REGS;
		I915_WRITE(PP_CONTROL(pps_idx), val);
	}
}

static void intel_pps_init(struct drm_i915_private *dev_priv)
{
	if (HAS_PCH_SPLIT(dev_priv) || IS_GEN9_LP(dev_priv))
		dev_priv->pps_mmio_base = PCH_PPS_BASE;
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		dev_priv->pps_mmio_base = VLV_PPS_BASE;
	else
		dev_priv->pps_mmio_base = PPS_BASE;

	intel_pps_unlock_regs_wa(dev_priv);
}

static void intel_setup_outputs(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;
	bool dpd_is_edp = false;

	intel_pps_init(dev_priv);

	/*
	 * intel_edp_init_connector() depends on this completing first, to
	 * prevent the registeration of both eDP and LVDS and the incorrect
	 * sharing of the PPS.
	 */
	intel_lvds_init(dev_priv);

	if (intel_crt_present(dev_priv))
		intel_crt_init(dev_priv);

	if (IS_GEN9_LP(dev_priv)) {
		/*
		 * FIXME: Broxton doesn't support port detection via the
		 * DDI_BUF_CTL_A or SFUSE_STRAP registers, find another way to
		 * detect the ports.
		 */
		intel_ddi_init(dev_priv, PORT_A);
		intel_ddi_init(dev_priv, PORT_B);
		intel_ddi_init(dev_priv, PORT_C);

		intel_dsi_init(dev_priv);
	} else if (HAS_DDI(dev_priv)) {
		int found;

		/*
		 * Haswell uses DDI functions to detect digital outputs.
		 * On SKL pre-D0 the strap isn't connected, so we assume
		 * it's there.
		 */
		found = I915_READ(DDI_BUF_CTL(PORT_A)) & DDI_INIT_DISPLAY_DETECTED;
		/* WaIgnoreDDIAStrap: skl */
		if (found || IS_GEN9_BC(dev_priv))
			intel_ddi_init(dev_priv, PORT_A);

		/* DDI B, C and D detection is indicated by the SFUSE_STRAP
		 * register */
		found = I915_READ(SFUSE_STRAP);

		if (found & SFUSE_STRAP_DDIB_DETECTED)
			intel_ddi_init(dev_priv, PORT_B);
		if (found & SFUSE_STRAP_DDIC_DETECTED)
			intel_ddi_init(dev_priv, PORT_C);
		if (found & SFUSE_STRAP_DDID_DETECTED)
			intel_ddi_init(dev_priv, PORT_D);
		/*
		 * On SKL we don't have a way to detect DDI-E so we rely on VBT.
		 */
		if (IS_GEN9_BC(dev_priv) &&
		    (dev_priv->vbt.ddi_port_info[PORT_E].supports_dp ||
		     dev_priv->vbt.ddi_port_info[PORT_E].supports_dvi ||
		     dev_priv->vbt.ddi_port_info[PORT_E].supports_hdmi))
			intel_ddi_init(dev_priv, PORT_E);

	} else if (HAS_PCH_SPLIT(dev_priv)) {
		int found;
		dpd_is_edp = intel_dp_is_edp(dev_priv, PORT_D);

		if (has_edp_a(dev_priv))
			intel_dp_init(dev_priv, DP_A, PORT_A);

		if (I915_READ(PCH_HDMIB) & SDVO_DETECTED) {
			/* PCH SDVOB multiplex with HDMIB */
			found = intel_sdvo_init(dev_priv, PCH_SDVOB, PORT_B);
			if (!found)
				intel_hdmi_init(dev_priv, PCH_HDMIB, PORT_B);
			if (!found && (I915_READ(PCH_DP_B) & DP_DETECTED))
				intel_dp_init(dev_priv, PCH_DP_B, PORT_B);
		}

		if (I915_READ(PCH_HDMIC) & SDVO_DETECTED)
			intel_hdmi_init(dev_priv, PCH_HDMIC, PORT_C);

		if (!dpd_is_edp && I915_READ(PCH_HDMID) & SDVO_DETECTED)
			intel_hdmi_init(dev_priv, PCH_HDMID, PORT_D);

		if (I915_READ(PCH_DP_C) & DP_DETECTED)
			intel_dp_init(dev_priv, PCH_DP_C, PORT_C);

		if (I915_READ(PCH_DP_D) & DP_DETECTED)
			intel_dp_init(dev_priv, PCH_DP_D, PORT_D);
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		bool has_edp, has_port;

		/*
		 * The DP_DETECTED bit is the latched state of the DDC
		 * SDA pin at boot. However since eDP doesn't require DDC
		 * (no way to plug in a DP->HDMI dongle) the DDC pins for
		 * eDP ports may have been muxed to an alternate function.
		 * Thus we can't rely on the DP_DETECTED bit alone to detect
		 * eDP ports. Consult the VBT as well as DP_DETECTED to
		 * detect eDP ports.
		 *
		 * Sadly the straps seem to be missing sometimes even for HDMI
		 * ports (eg. on Voyo V3 - CHT x7-Z8700), so check both strap
		 * and VBT for the presence of the port. Additionally we can't
		 * trust the port type the VBT declares as we've seen at least
		 * HDMI ports that the VBT claim are DP or eDP.
		 */
		has_edp = intel_dp_is_edp(dev_priv, PORT_B);
		has_port = intel_bios_is_port_present(dev_priv, PORT_B);
		if (I915_READ(VLV_DP_B) & DP_DETECTED || has_port)
			has_edp &= intel_dp_init(dev_priv, VLV_DP_B, PORT_B);
		if ((I915_READ(VLV_HDMIB) & SDVO_DETECTED || has_port) && !has_edp)
			intel_hdmi_init(dev_priv, VLV_HDMIB, PORT_B);

		has_edp = intel_dp_is_edp(dev_priv, PORT_C);
		has_port = intel_bios_is_port_present(dev_priv, PORT_C);
		if (I915_READ(VLV_DP_C) & DP_DETECTED || has_port)
			has_edp &= intel_dp_init(dev_priv, VLV_DP_C, PORT_C);
		if ((I915_READ(VLV_HDMIC) & SDVO_DETECTED || has_port) && !has_edp)
			intel_hdmi_init(dev_priv, VLV_HDMIC, PORT_C);

		if (IS_CHERRYVIEW(dev_priv)) {
			/*
			 * eDP not supported on port D,
			 * so no need to worry about it
			 */
			has_port = intel_bios_is_port_present(dev_priv, PORT_D);
			if (I915_READ(CHV_DP_D) & DP_DETECTED || has_port)
				intel_dp_init(dev_priv, CHV_DP_D, PORT_D);
			if (I915_READ(CHV_HDMID) & SDVO_DETECTED || has_port)
				intel_hdmi_init(dev_priv, CHV_HDMID, PORT_D);
		}

		intel_dsi_init(dev_priv);
	} else if (!IS_GEN2(dev_priv) && !IS_PINEVIEW(dev_priv)) {
		bool found = false;

		if (I915_READ(GEN3_SDVOB) & SDVO_DETECTED) {
			DRM_DEBUG_KMS("probing SDVOB\n");
			found = intel_sdvo_init(dev_priv, GEN3_SDVOB, PORT_B);
			if (!found && IS_G4X(dev_priv)) {
				DRM_DEBUG_KMS("probing HDMI on SDVOB\n");
				intel_hdmi_init(dev_priv, GEN4_HDMIB, PORT_B);
			}

			if (!found && IS_G4X(dev_priv))
				intel_dp_init(dev_priv, DP_B, PORT_B);
		}

		/* Before G4X SDVOC doesn't have its own detect register */

		if (I915_READ(GEN3_SDVOB) & SDVO_DETECTED) {
			DRM_DEBUG_KMS("probing SDVOC\n");
			found = intel_sdvo_init(dev_priv, GEN3_SDVOC, PORT_C);
		}

		if (!found && (I915_READ(GEN3_SDVOC) & SDVO_DETECTED)) {

			if (IS_G4X(dev_priv)) {
				DRM_DEBUG_KMS("probing HDMI on SDVOC\n");
				intel_hdmi_init(dev_priv, GEN4_HDMIC, PORT_C);
			}
			if (IS_G4X(dev_priv))
				intel_dp_init(dev_priv, DP_C, PORT_C);
		}

		if (IS_G4X(dev_priv) && (I915_READ(DP_D) & DP_DETECTED))
			intel_dp_init(dev_priv, DP_D, PORT_D);
	} else if (IS_GEN2(dev_priv))
		intel_dvo_init(dev_priv);

	if (SUPPORTS_TV(dev_priv))
		intel_tv_init(dev_priv);

	intel_psr_init(dev_priv);

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		encoder->base.possible_crtcs = encoder->crtc_mask;
		encoder->base.possible_clones =
			intel_encoder_clones(encoder);
	}

	intel_init_pch_refclk(dev_priv);

	drm_helper_move_panel_connectors_to_head(&dev_priv->drm);
}

static void intel_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);

	drm_framebuffer_cleanup(fb);

	i915_gem_object_lock(intel_fb->obj);
	WARN_ON(!intel_fb->obj->framebuffer_references--);
	i915_gem_object_unlock(intel_fb->obj);

	i915_gem_object_put(intel_fb->obj);

	kfree(intel_fb);
}

static int intel_user_framebuffer_create_handle(struct drm_framebuffer *fb,
						struct drm_file *file,
						unsigned int *handle)
{
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;

	if (obj->userptr.mm) {
		DRM_DEBUG("attempting to use a userptr for a framebuffer, denied\n");
		return -EINVAL;
	}

	return drm_gem_handle_create(file, &obj->base, handle);
}

static int intel_user_framebuffer_dirty(struct drm_framebuffer *fb,
					struct drm_file *file,
					unsigned flags, unsigned color,
					struct drm_clip_rect *clips,
					unsigned num_clips)
{
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);

	i915_gem_object_flush_if_display(obj);
	intel_fb_obj_flush(obj, ORIGIN_DIRTYFB);

	return 0;
}

static const struct drm_framebuffer_funcs intel_fb_funcs = {
	.destroy = intel_user_framebuffer_destroy,
	.create_handle = intel_user_framebuffer_create_handle,
	.dirty = intel_user_framebuffer_dirty,
};

static
u32 intel_fb_pitch_limit(struct drm_i915_private *dev_priv,
			 uint64_t fb_modifier, uint32_t pixel_format)
{
	u32 gen = INTEL_GEN(dev_priv);

	if (gen >= 9) {
		int cpp = drm_format_plane_cpp(pixel_format, 0);

		/* "The stride in bytes must not exceed the of the size of 8K
		 *  pixels and 32K bytes."
		 */
		return min(8192 * cpp, 32768);
	} else if (gen >= 5 && !HAS_GMCH_DISPLAY(dev_priv)) {
		return 32*1024;
	} else if (gen >= 4) {
		if (fb_modifier == I915_FORMAT_MOD_X_TILED)
			return 16*1024;
		else
			return 32*1024;
	} else if (gen >= 3) {
		if (fb_modifier == I915_FORMAT_MOD_X_TILED)
			return 8*1024;
		else
			return 16*1024;
	} else {
		/* XXX DSPC is limited to 4k tiled */
		return 8*1024;
	}
}

static int intel_framebuffer_init(struct intel_framebuffer *intel_fb,
				  struct drm_i915_gem_object *obj,
				  struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct drm_format_name_buf format_name;
	u32 pitch_limit, stride_alignment;
	unsigned int tiling, stride;
	int ret = -EINVAL;

	i915_gem_object_lock(obj);
	obj->framebuffer_references++;
	tiling = i915_gem_object_get_tiling(obj);
	stride = i915_gem_object_get_stride(obj);
	i915_gem_object_unlock(obj);

	if (mode_cmd->flags & DRM_MODE_FB_MODIFIERS) {
		/*
		 * If there's a fence, enforce that
		 * the fb modifier and tiling mode match.
		 */
		if (tiling != I915_TILING_NONE &&
		    tiling != intel_fb_modifier_to_tiling(mode_cmd->modifier[0])) {
			DRM_DEBUG("tiling_mode doesn't match fb modifier\n");
			goto err;
		}
	} else {
		if (tiling == I915_TILING_X) {
			mode_cmd->modifier[0] = I915_FORMAT_MOD_X_TILED;
		} else if (tiling == I915_TILING_Y) {
			DRM_DEBUG("No Y tiling for legacy addfb\n");
			goto err;
		}
	}

	/* Passed in modifier sanity checking. */
	switch (mode_cmd->modifier[0]) {
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Yf_TILED:
		if (INTEL_GEN(dev_priv) < 9) {
			DRM_DEBUG("Unsupported tiling 0x%llx!\n",
				  mode_cmd->modifier[0]);
			goto err;
		}
	case DRM_FORMAT_MOD_NONE:
	case I915_FORMAT_MOD_X_TILED:
		break;
	default:
		DRM_DEBUG("Unsupported fb modifier 0x%llx!\n",
			  mode_cmd->modifier[0]);
		goto err;
	}

	/*
	 * gen2/3 display engine uses the fence if present,
	 * so the tiling mode must match the fb modifier exactly.
	 */
	if (INTEL_INFO(dev_priv)->gen < 4 &&
	    tiling != intel_fb_modifier_to_tiling(mode_cmd->modifier[0])) {
		DRM_DEBUG("tiling_mode must match fb modifier exactly on gen2/3\n");
		goto err;
	}

	stride_alignment = intel_fb_stride_alignment(dev_priv,
						     mode_cmd->modifier[0],
						     mode_cmd->pixel_format);
	if (mode_cmd->pitches[0] & (stride_alignment - 1)) {
		DRM_DEBUG("pitch (%d) must be at least %u byte aligned\n",
			  mode_cmd->pitches[0], stride_alignment);
		goto err;
	}

	pitch_limit = intel_fb_pitch_limit(dev_priv, mode_cmd->modifier[0],
					   mode_cmd->pixel_format);
	if (mode_cmd->pitches[0] > pitch_limit) {
		DRM_DEBUG("%s pitch (%u) must be at less than %d\n",
			  mode_cmd->modifier[0] != DRM_FORMAT_MOD_NONE ?
			  "tiled" : "linear",
			  mode_cmd->pitches[0], pitch_limit);
		goto err;
	}

	/*
	 * If there's a fence, enforce that
	 * the fb pitch and fence stride match.
	 */
	if (tiling != I915_TILING_NONE && mode_cmd->pitches[0] !=  stride) {
		DRM_DEBUG("pitch (%d) must match tiling stride (%d)\n",
			  mode_cmd->pitches[0], stride);
		goto err;
	}

	/* Reject formats not supported by any plane early. */
	switch (mode_cmd->pixel_format) {
	case DRM_FORMAT_C8:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		break;
	case DRM_FORMAT_XRGB1555:
		if (INTEL_GEN(dev_priv) > 3) {
			DRM_DEBUG("unsupported pixel format: %s\n",
			          drm_get_format_name(mode_cmd->pixel_format, &format_name));
			goto err;
		}
		break;
	case DRM_FORMAT_ABGR8888:
		if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv) &&
		    INTEL_GEN(dev_priv) < 9) {
			DRM_DEBUG("unsupported pixel format: %s\n",
			          drm_get_format_name(mode_cmd->pixel_format, &format_name));
			goto err;
		}
		break;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
		if (INTEL_GEN(dev_priv) < 4) {
			DRM_DEBUG("unsupported pixel format: %s\n",
			          drm_get_format_name(mode_cmd->pixel_format, &format_name));
			goto err;
		}
		break;
	case DRM_FORMAT_ABGR2101010:
		if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv)) {
			DRM_DEBUG("unsupported pixel format: %s\n",
			          drm_get_format_name(mode_cmd->pixel_format, &format_name));
			goto err;
		}
		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_VYUY:
		if (INTEL_GEN(dev_priv) < 5) {
			DRM_DEBUG("unsupported pixel format: %s\n",
			          drm_get_format_name(mode_cmd->pixel_format, &format_name));
			goto err;
		}
		break;
	default:
		DRM_DEBUG("unsupported pixel format: %s\n",
		          drm_get_format_name(mode_cmd->pixel_format, &format_name));
		goto err;
	}

	/* FIXME need to adjust LINOFF/TILEOFF accordingly. */
	if (mode_cmd->offsets[0] != 0)
		goto err;

	drm_helper_mode_fill_fb_struct(&dev_priv->drm,
				       &intel_fb->base, mode_cmd);
	intel_fb->obj = obj;

	ret = intel_fill_fb_info(dev_priv, &intel_fb->base);
	if (ret)
		goto err;

	ret = drm_framebuffer_init(obj->base.dev,
				   &intel_fb->base,
				   &intel_fb_funcs);
	if (ret) {
		DRM_ERROR("framebuffer init failed %d\n", ret);
		goto err;
	}

	return 0;

err:
	i915_gem_object_lock(obj);
	obj->framebuffer_references--;
	i915_gem_object_unlock(obj);
	return ret;
}

static struct drm_framebuffer *
intel_user_framebuffer_create(struct drm_device *dev,
			      struct drm_file *filp,
			      const struct drm_mode_fb_cmd2 *user_mode_cmd)
{
	struct drm_framebuffer *fb;
	struct drm_i915_gem_object *obj;
	struct drm_mode_fb_cmd2 mode_cmd = *user_mode_cmd;

	obj = i915_gem_object_lookup(filp, mode_cmd.handles[0]);
	if (!obj)
		return ERR_PTR(-ENOENT);

	fb = intel_framebuffer_create(obj, &mode_cmd);
	if (IS_ERR(fb))
		i915_gem_object_put(obj);

	return fb;
}

static void intel_atomic_state_free(struct drm_atomic_state *state)
{
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);

	drm_atomic_state_default_release(state);

	i915_sw_fence_fini(&intel_state->commit_ready);

	kfree(state);
}

static const struct drm_mode_config_funcs intel_mode_funcs = {
	.fb_create = intel_user_framebuffer_create,
	.output_poll_changed = intel_fbdev_output_poll_changed,
	.atomic_check = intel_atomic_check,
	.atomic_commit = intel_atomic_commit,
	.atomic_state_alloc = intel_atomic_state_alloc,
	.atomic_state_clear = intel_atomic_state_clear,
	.atomic_state_free = intel_atomic_state_free,
};

/**
 * intel_init_display_hooks - initialize the display modesetting hooks
 * @dev_priv: device private
 */
void intel_init_display_hooks(struct drm_i915_private *dev_priv)
{
	intel_init_cdclk_hooks(dev_priv);

	if (INTEL_INFO(dev_priv)->gen >= 9) {
		dev_priv->display.get_pipe_config = haswell_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			skylake_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock =
			haswell_crtc_compute_clock;
		dev_priv->display.crtc_enable = haswell_crtc_enable;
		dev_priv->display.crtc_disable = haswell_crtc_disable;
	} else if (HAS_DDI(dev_priv)) {
		dev_priv->display.get_pipe_config = haswell_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			ironlake_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock =
			haswell_crtc_compute_clock;
		dev_priv->display.crtc_enable = haswell_crtc_enable;
		dev_priv->display.crtc_disable = haswell_crtc_disable;
	} else if (HAS_PCH_SPLIT(dev_priv)) {
		dev_priv->display.get_pipe_config = ironlake_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			ironlake_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock =
			ironlake_crtc_compute_clock;
		dev_priv->display.crtc_enable = ironlake_crtc_enable;
		dev_priv->display.crtc_disable = ironlake_crtc_disable;
	} else if (IS_CHERRYVIEW(dev_priv)) {
		dev_priv->display.get_pipe_config = i9xx_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			i9xx_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock = chv_crtc_compute_clock;
		dev_priv->display.crtc_enable = valleyview_crtc_enable;
		dev_priv->display.crtc_disable = i9xx_crtc_disable;
	} else if (IS_VALLEYVIEW(dev_priv)) {
		dev_priv->display.get_pipe_config = i9xx_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			i9xx_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock = vlv_crtc_compute_clock;
		dev_priv->display.crtc_enable = valleyview_crtc_enable;
		dev_priv->display.crtc_disable = i9xx_crtc_disable;
	} else if (IS_G4X(dev_priv)) {
		dev_priv->display.get_pipe_config = i9xx_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			i9xx_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock = g4x_crtc_compute_clock;
		dev_priv->display.crtc_enable = i9xx_crtc_enable;
		dev_priv->display.crtc_disable = i9xx_crtc_disable;
	} else if (IS_PINEVIEW(dev_priv)) {
		dev_priv->display.get_pipe_config = i9xx_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			i9xx_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock = pnv_crtc_compute_clock;
		dev_priv->display.crtc_enable = i9xx_crtc_enable;
		dev_priv->display.crtc_disable = i9xx_crtc_disable;
	} else if (!IS_GEN2(dev_priv)) {
		dev_priv->display.get_pipe_config = i9xx_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			i9xx_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock = i9xx_crtc_compute_clock;
		dev_priv->display.crtc_enable = i9xx_crtc_enable;
		dev_priv->display.crtc_disable = i9xx_crtc_disable;
	} else {
		dev_priv->display.get_pipe_config = i9xx_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			i9xx_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock = i8xx_crtc_compute_clock;
		dev_priv->display.crtc_enable = i9xx_crtc_enable;
		dev_priv->display.crtc_disable = i9xx_crtc_disable;
	}

	if (IS_GEN5(dev_priv)) {
		dev_priv->display.fdi_link_train = ironlake_fdi_link_train;
	} else if (IS_GEN6(dev_priv)) {
		dev_priv->display.fdi_link_train = gen6_fdi_link_train;
	} else if (IS_IVYBRIDGE(dev_priv)) {
		/* FIXME: detect B0+ stepping and use auto training */
		dev_priv->display.fdi_link_train = ivb_manual_fdi_link_train;
	} else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		dev_priv->display.fdi_link_train = hsw_fdi_link_train;
	}

	if (dev_priv->info.gen >= 9)
		dev_priv->display.update_crtcs = skl_update_crtcs;
	else
		dev_priv->display.update_crtcs = intel_update_crtcs;

	switch (INTEL_INFO(dev_priv)->gen) {
	case 2:
		dev_priv->display.queue_flip = intel_gen2_queue_flip;
		break;

	case 3:
		dev_priv->display.queue_flip = intel_gen3_queue_flip;
		break;

	case 4:
	case 5:
		dev_priv->display.queue_flip = intel_gen4_queue_flip;
		break;

	case 6:
		dev_priv->display.queue_flip = intel_gen6_queue_flip;
		break;
	case 7:
	case 8: /* FIXME(BDW): Check that the gen8 RCS flip works. */
		dev_priv->display.queue_flip = intel_gen7_queue_flip;
		break;
	case 9:
		/* Drop through - unsupported since execlist only. */
	default:
		/* Default just returns -ENODEV to indicate unsupported */
		dev_priv->display.queue_flip = intel_default_queue_flip;
	}
}

/*
 * Some BIOSes insist on assuming the GPU's pipe A is enabled at suspend,
 * resume, or other times.  This quirk makes sure that's the case for
 * affected systems.
 */
static void quirk_pipea_force(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	dev_priv->quirks |= QUIRK_PIPEA_FORCE;
	DRM_INFO("applying pipe a force quirk\n");
}

static void quirk_pipeb_force(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	dev_priv->quirks |= QUIRK_PIPEB_FORCE;
	DRM_INFO("applying pipe b force quirk\n");
}

/*
 * Some machines (Lenovo U160) do not work with SSC on LVDS for some reason
 */
static void quirk_ssc_force_disable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	dev_priv->quirks |= QUIRK_LVDS_SSC_DISABLE;
	DRM_INFO("applying lvds SSC disable quirk\n");
}

/*
 * A machine (e.g. Acer Aspire 5734Z) may need to invert the panel backlight
 * brightness value
 */
static void quirk_invert_brightness(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	dev_priv->quirks |= QUIRK_INVERT_BRIGHTNESS;
	DRM_INFO("applying inverted panel brightness quirk\n");
}

/* Some VBT's incorrectly indicate no backlight is present */
static void quirk_backlight_present(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	dev_priv->quirks |= QUIRK_BACKLIGHT_PRESENT;
	DRM_INFO("applying backlight present quirk\n");
}

struct intel_quirk {
	int device;
	int subsystem_vendor;
	int subsystem_device;
	void (*hook)(struct drm_device *dev);
};

/* For systems that don't have a meaningful PCI subdevice/subvendor ID */
struct intel_dmi_quirk {
	void (*hook)(struct drm_device *dev);
	const struct dmi_system_id (*dmi_id_list)[];
};

static int intel_dmi_reverse_brightness(const struct dmi_system_id *id)
{
	DRM_INFO("Backlight polarity reversed on %s\n", id->ident);
	return 1;
}

static const struct intel_dmi_quirk intel_dmi_quirks[] = {
	{
		.dmi_id_list = &(const struct dmi_system_id[]) {
			{
				.callback = intel_dmi_reverse_brightness,
				.ident = "NCR Corporation",
				.matches = {DMI_MATCH(DMI_SYS_VENDOR, "NCR Corporation"),
					    DMI_MATCH(DMI_PRODUCT_NAME, ""),
				},
			},
			{ }  /* terminating entry */
		},
		.hook = quirk_invert_brightness,
	},
};

static struct intel_quirk intel_quirks[] = {
	/* Toshiba Protege R-205, S-209 needs pipe A force quirk */
	{ 0x2592, 0x1179, 0x0001, quirk_pipea_force },

	/* ThinkPad T60 needs pipe A force quirk (bug #16494) */
	{ 0x2782, 0x17aa, 0x201a, quirk_pipea_force },

	/* 830 needs to leave pipe A & dpll A up */
	{ 0x3577, PCI_ANY_ID, PCI_ANY_ID, quirk_pipea_force },

	/* 830 needs to leave pipe B & dpll B up */
	{ 0x3577, PCI_ANY_ID, PCI_ANY_ID, quirk_pipeb_force },

	/* Lenovo U160 cannot use SSC on LVDS */
	{ 0x0046, 0x17aa, 0x3920, quirk_ssc_force_disable },

	/* Sony Vaio Y cannot use SSC on LVDS */
	{ 0x0046, 0x104d, 0x9076, quirk_ssc_force_disable },

	/* Acer Aspire 5734Z must invert backlight brightness */
	{ 0x2a42, 0x1025, 0x0459, quirk_invert_brightness },

	/* Acer/eMachines G725 */
	{ 0x2a42, 0x1025, 0x0210, quirk_invert_brightness },

	/* Acer/eMachines e725 */
	{ 0x2a42, 0x1025, 0x0212, quirk_invert_brightness },

	/* Acer/Packard Bell NCL20 */
	{ 0x2a42, 0x1025, 0x034b, quirk_invert_brightness },

	/* Acer Aspire 4736Z */
	{ 0x2a42, 0x1025, 0x0260, quirk_invert_brightness },

	/* Acer Aspire 5336 */
	{ 0x2a42, 0x1025, 0x048a, quirk_invert_brightness },

	/* Acer C720 and C720P Chromebooks (Celeron 2955U) have backlights */
	{ 0x0a06, 0x1025, 0x0a11, quirk_backlight_present },

	/* Acer C720 Chromebook (Core i3 4005U) */
	{ 0x0a16, 0x1025, 0x0a11, quirk_backlight_present },

	/* Apple Macbook 2,1 (Core 2 T7400) */
	{ 0x27a2, 0x8086, 0x7270, quirk_backlight_present },

	/* Apple Macbook 4,1 */
	{ 0x2a02, 0x106b, 0x00a1, quirk_backlight_present },

	/* Toshiba CB35 Chromebook (Celeron 2955U) */
	{ 0x0a06, 0x1179, 0x0a88, quirk_backlight_present },

	/* HP Chromebook 14 (Celeron 2955U) */
	{ 0x0a06, 0x103c, 0x21ed, quirk_backlight_present },

	/* Dell Chromebook 11 */
	{ 0x0a06, 0x1028, 0x0a35, quirk_backlight_present },

	/* Dell Chromebook 11 (2015 version) */
	{ 0x0a16, 0x1028, 0x0a35, quirk_backlight_present },
};

static void intel_init_quirks(struct drm_device *dev)
{
	struct pci_dev *d = dev->pdev;
	int i;

	for (i = 0; i < ARRAY_SIZE(intel_quirks); i++) {
		struct intel_quirk *q = &intel_quirks[i];

		if (d->device == q->device &&
		    (d->subsystem_vendor == q->subsystem_vendor ||
		     q->subsystem_vendor == PCI_ANY_ID) &&
		    (d->subsystem_device == q->subsystem_device ||
		     q->subsystem_device == PCI_ANY_ID))
			q->hook(dev);
	}
	for (i = 0; i < ARRAY_SIZE(intel_dmi_quirks); i++) {
		if (dmi_check_system(*intel_dmi_quirks[i].dmi_id_list) != 0)
			intel_dmi_quirks[i].hook(dev);
	}
}

/* Disable the VGA plane that we never use */
static void i915_disable_vga(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	u8 sr1;
	i915_reg_t vga_reg = i915_vgacntrl_reg(dev_priv);

	/* WaEnableVGAAccessThroughIOPort:ctg,elk,ilk,snb,ivb,vlv,hsw */
	vga_get_uninterruptible(pdev, VGA_RSRC_LEGACY_IO);
	outb(SR01, VGA_SR_INDEX);
	sr1 = inb(VGA_SR_DATA);
	outb(sr1 | 1<<5, VGA_SR_DATA);
	vga_put(pdev, VGA_RSRC_LEGACY_IO);
	udelay(300);

	I915_WRITE(vga_reg, VGA_DISP_DISABLE);
	POSTING_READ(vga_reg);
}

void intel_modeset_init_hw(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	intel_update_cdclk(dev_priv);
	dev_priv->cdclk.logical = dev_priv->cdclk.actual = dev_priv->cdclk.hw;

	intel_init_clock_gating(dev_priv);
}

/*
 * Calculate what we think the watermarks should be for the state we've read
 * out of the hardware and then immediately program those watermarks so that
 * we ensure the hardware settings match our internal state.
 *
 * We can calculate what we think WM's should be by creating a duplicate of the
 * current state (which was constructed during hardware readout) and running it
 * through the atomic check code to calculate new watermark values in the
 * state object.
 */
static void sanitize_watermarks(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_atomic_state *state;
	struct intel_atomic_state *intel_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *cstate;
	struct drm_modeset_acquire_ctx ctx;
	int ret;
	int i;

	/* Only supported on platforms that use atomic watermark design */
	if (!dev_priv->display.optimize_watermarks)
		return;

	/*
	 * We need to hold connection_mutex before calling duplicate_state so
	 * that the connector loop is protected.
	 */
	drm_modeset_acquire_init(&ctx, 0);
retry:
	ret = drm_modeset_lock_all_ctx(dev, &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry;
	} else if (WARN_ON(ret)) {
		goto fail;
	}

	state = drm_atomic_helper_duplicate_state(dev, &ctx);
	if (WARN_ON(IS_ERR(state)))
		goto fail;

	intel_state = to_intel_atomic_state(state);

	/*
	 * Hardware readout is the only time we don't want to calculate
	 * intermediate watermarks (since we don't trust the current
	 * watermarks).
	 */
	intel_state->skip_intermediate_wm = true;

	ret = intel_atomic_check(dev, state);
	if (ret) {
		/*
		 * If we fail here, it means that the hardware appears to be
		 * programmed in a way that shouldn't be possible, given our
		 * understanding of watermark requirements.  This might mean a
		 * mistake in the hardware readout code or a mistake in the
		 * watermark calculations for a given platform.  Raise a WARN
		 * so that this is noticeable.
		 *
		 * If this actually happens, we'll have to just leave the
		 * BIOS-programmed watermarks untouched and hope for the best.
		 */
		WARN(true, "Could not determine valid watermarks for inherited state\n");
		goto put_state;
	}

	/* Write calculated watermark values back */
	for_each_crtc_in_state(state, crtc, cstate, i) {
		struct intel_crtc_state *cs = to_intel_crtc_state(cstate);

		cs->wm.need_postvbl_update = true;
		dev_priv->display.optimize_watermarks(intel_state, cs);
	}

put_state:
	drm_atomic_state_put(state);
fail:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

int intel_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	enum pipe pipe;
	struct intel_crtc *crtc;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;

	dev->mode_config.allow_fb_modifiers = true;

	dev->mode_config.funcs = &intel_mode_funcs;

	INIT_WORK(&dev_priv->atomic_helper.free_work,
		  intel_atomic_helper_free_state_worker);

	intel_init_quirks(dev);

	intel_init_pm(dev_priv);

	if (INTEL_INFO(dev_priv)->num_pipes == 0)
		return 0;

	/*
	 * There may be no VBT; and if the BIOS enabled SSC we can
	 * just keep using it to avoid unnecessary flicker.  Whereas if the
	 * BIOS isn't using it, don't assume it will work even if the VBT
	 * indicates as much.
	 */
	if (HAS_PCH_IBX(dev_priv) || HAS_PCH_CPT(dev_priv)) {
		bool bios_lvds_use_ssc = !!(I915_READ(PCH_DREF_CONTROL) &
					    DREF_SSC1_ENABLE);

		if (dev_priv->vbt.lvds_use_ssc != bios_lvds_use_ssc) {
			DRM_DEBUG_KMS("SSC %sabled by BIOS, overriding VBT which says %sabled\n",
				     bios_lvds_use_ssc ? "en" : "dis",
				     dev_priv->vbt.lvds_use_ssc ? "en" : "dis");
			dev_priv->vbt.lvds_use_ssc = bios_lvds_use_ssc;
		}
	}

	if (IS_GEN2(dev_priv)) {
		dev->mode_config.max_width = 2048;
		dev->mode_config.max_height = 2048;
	} else if (IS_GEN3(dev_priv)) {
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
	} else {
		dev->mode_config.max_width = 8192;
		dev->mode_config.max_height = 8192;
	}

	if (IS_I845G(dev_priv) || IS_I865G(dev_priv)) {
		dev->mode_config.cursor_width = IS_I845G(dev_priv) ? 64 : 512;
		dev->mode_config.cursor_height = 1023;
	} else if (IS_GEN2(dev_priv)) {
		dev->mode_config.cursor_width = GEN2_CURSOR_WIDTH;
		dev->mode_config.cursor_height = GEN2_CURSOR_HEIGHT;
	} else {
		dev->mode_config.cursor_width = MAX_CURSOR_WIDTH;
		dev->mode_config.cursor_height = MAX_CURSOR_HEIGHT;
	}

	dev->mode_config.fb_base = ggtt->mappable_base;

	DRM_DEBUG_KMS("%d display pipe%s available.\n",
		      INTEL_INFO(dev_priv)->num_pipes,
		      INTEL_INFO(dev_priv)->num_pipes > 1 ? "s" : "");

	for_each_pipe(dev_priv, pipe) {
		int ret;

		ret = intel_crtc_init(dev_priv, pipe);
		if (ret) {
			drm_mode_config_cleanup(dev);
			return ret;
		}
	}

	intel_update_czclk(dev_priv);
	intel_update_cdclk(dev_priv);
	dev_priv->cdclk.logical = dev_priv->cdclk.actual = dev_priv->cdclk.hw;

	intel_shared_dpll_init(dev);

	if (dev_priv->max_cdclk_freq == 0)
		intel_update_max_cdclk(dev_priv);

	/* Just disable it once at startup */
	i915_disable_vga(dev_priv);
	intel_setup_outputs(dev_priv);

	drm_modeset_lock_all(dev);
	intel_modeset_setup_hw_state(dev);
	drm_modeset_unlock_all(dev);

	for_each_intel_crtc(dev, crtc) {
		struct intel_initial_plane_config plane_config = {};

		if (!crtc->active)
			continue;

		/*
		 * Note that reserving the BIOS fb up front prevents us
		 * from stuffing other stolen allocations like the ring
		 * on top.  This prevents some ugliness at boot time, and
		 * can even allow for smooth boot transitions if the BIOS
		 * fb is large enough for the active pipe configuration.
		 */
		dev_priv->display.get_initial_plane_config(crtc,
							   &plane_config);

		/*
		 * If the fb is shared between multiple heads, we'll
		 * just get the first one.
		 */
		intel_find_initial_plane_obj(crtc, &plane_config);
	}

	/*
	 * Make sure hardware watermarks really match the state we read out.
	 * Note that we need to do this after reconstructing the BIOS fb's
	 * since the watermark calculation done here will use pstate->fb.
	 */
	sanitize_watermarks(dev);

	return 0;
}

static void intel_enable_pipe_a(struct drm_device *dev)
{
	struct intel_connector *connector;
	struct drm_connector *crt = NULL;
	struct intel_load_detect_pipe load_detect_temp;
	struct drm_modeset_acquire_ctx *ctx = dev->mode_config.acquire_ctx;

	/* We can't just switch on the pipe A, we need to set things up with a
	 * proper mode and output configuration. As a gross hack, enable pipe A
	 * by enabling the load detect pipe once. */
	for_each_intel_connector(dev, connector) {
		if (connector->encoder->type == INTEL_OUTPUT_ANALOG) {
			crt = &connector->base;
			break;
		}
	}

	if (!crt)
		return;

	if (intel_get_load_detect_pipe(crt, NULL, &load_detect_temp, ctx))
		intel_release_load_detect_pipe(crt, &load_detect_temp, ctx);
}

static bool
intel_check_plane_mapping(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 val;

	if (INTEL_INFO(dev_priv)->num_pipes == 1)
		return true;

	val = I915_READ(DSPCNTR(!crtc->plane));

	if ((val & DISPLAY_PLANE_ENABLE) &&
	    (!!(val & DISPPLANE_SEL_PIPE_MASK) == crtc->pipe))
		return false;

	return true;
}

static bool intel_crtc_has_encoders(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_encoder *encoder;

	for_each_encoder_on_crtc(dev, &crtc->base, encoder)
		return true;

	return false;
}

static struct intel_connector *intel_encoder_find_connector(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct intel_connector *connector;

	for_each_connector_on_encoder(dev, &encoder->base, connector)
		return connector;

	return NULL;
}

static bool has_pch_trancoder(struct drm_i915_private *dev_priv,
			      enum transcoder pch_transcoder)
{
	return HAS_PCH_IBX(dev_priv) || HAS_PCH_CPT(dev_priv) ||
		(HAS_PCH_LPT_H(dev_priv) && pch_transcoder == TRANSCODER_A);
}

static void intel_sanitize_crtc(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum transcoder cpu_transcoder = crtc->config->cpu_transcoder;

	/* Clear any frame start delays used for debugging left by the BIOS */
	if (!transcoder_is_dsi(cpu_transcoder)) {
		i915_reg_t reg = PIPECONF(cpu_transcoder);

		I915_WRITE(reg,
			   I915_READ(reg) & ~PIPECONF_FRAME_START_DELAY_MASK);
	}

	/* restore vblank interrupts to correct state */
	drm_crtc_vblank_reset(&crtc->base);
	if (crtc->active) {
		struct intel_plane *plane;

		drm_crtc_vblank_on(&crtc->base);

		/* Disable everything but the primary plane */
		for_each_intel_plane_on_crtc(dev, crtc, plane) {
			if (plane->base.type == DRM_PLANE_TYPE_PRIMARY)
				continue;

			plane->disable_plane(&plane->base, &crtc->base);
		}
	}

	/* We need to sanitize the plane -> pipe mapping first because this will
	 * disable the crtc (and hence change the state) if it is wrong. Note
	 * that gen4+ has a fixed plane -> pipe mapping.  */
	if (INTEL_GEN(dev_priv) < 4 && !intel_check_plane_mapping(crtc)) {
		bool plane;

		DRM_DEBUG_KMS("[CRTC:%d:%s] wrong plane connection detected!\n",
			      crtc->base.base.id, crtc->base.name);

		/* Pipe has the wrong plane attached and the plane is active.
		 * Temporarily change the plane mapping and disable everything
		 * ...  */
		plane = crtc->plane;
		crtc->base.primary->state->visible = true;
		crtc->plane = !plane;
		intel_crtc_disable_noatomic(&crtc->base);
		crtc->plane = plane;
	}

	if (dev_priv->quirks & QUIRK_PIPEA_FORCE &&
	    crtc->pipe == PIPE_A && !crtc->active) {
		/* BIOS forgot to enable pipe A, this mostly happens after
		 * resume. Force-enable the pipe to fix this, the update_dpms
		 * call below we restore the pipe to the right state, but leave
		 * the required bits on. */
		intel_enable_pipe_a(dev);
	}

	/* Adjust the state of the output pipe according to whether we
	 * have active connectors/encoders. */
	if (crtc->active && !intel_crtc_has_encoders(crtc))
		intel_crtc_disable_noatomic(&crtc->base);

	if (crtc->active || HAS_GMCH_DISPLAY(dev_priv)) {
		/*
		 * We start out with underrun reporting disabled to avoid races.
		 * For correct bookkeeping mark this on active crtcs.
		 *
		 * Also on gmch platforms we dont have any hardware bits to
		 * disable the underrun reporting. Which means we need to start
		 * out with underrun reporting disabled also on inactive pipes,
		 * since otherwise we'll complain about the garbage we read when
		 * e.g. coming up after runtime pm.
		 *
		 * No protection against concurrent access is required - at
		 * worst a fifo underrun happens which also sets this to false.
		 */
		crtc->cpu_fifo_underrun_disabled = true;
		/*
		 * We track the PCH trancoder underrun reporting state
		 * within the crtc. With crtc for pipe A housing the underrun
		 * reporting state for PCH transcoder A, crtc for pipe B housing
		 * it for PCH transcoder B, etc. LPT-H has only PCH transcoder A,
		 * and marking underrun reporting as disabled for the non-existing
		 * PCH transcoders B and C would prevent enabling the south
		 * error interrupt (see cpt_can_enable_serr_int()).
		 */
		if (has_pch_trancoder(dev_priv, (enum transcoder)crtc->pipe))
			crtc->pch_fifo_underrun_disabled = true;
	}
}

static void intel_sanitize_encoder(struct intel_encoder *encoder)
{
	struct intel_connector *connector;

	/* We need to check both for a crtc link (meaning that the
	 * encoder is active and trying to read from a pipe) and the
	 * pipe itself being active. */
	bool has_active_crtc = encoder->base.crtc &&
		to_intel_crtc(encoder->base.crtc)->active;

	connector = intel_encoder_find_connector(encoder);
	if (connector && !has_active_crtc) {
		DRM_DEBUG_KMS("[ENCODER:%d:%s] has active connectors but no active pipe!\n",
			      encoder->base.base.id,
			      encoder->base.name);

		/* Connector is active, but has no active pipe. This is
		 * fallout from our resume register restoring. Disable
		 * the encoder manually again. */
		if (encoder->base.crtc) {
			struct drm_crtc_state *crtc_state = encoder->base.crtc->state;

			DRM_DEBUG_KMS("[ENCODER:%d:%s] manually disabled\n",
				      encoder->base.base.id,
				      encoder->base.name);
			encoder->disable(encoder, to_intel_crtc_state(crtc_state), connector->base.state);
			if (encoder->post_disable)
				encoder->post_disable(encoder, to_intel_crtc_state(crtc_state), connector->base.state);
		}
		encoder->base.crtc = NULL;

		/* Inconsistent output/port/pipe state happens presumably due to
		 * a bug in one of the get_hw_state functions. Or someplace else
		 * in our code, like the register restore mess on resume. Clamp
		 * things to off as a safer default. */

		connector->base.dpms = DRM_MODE_DPMS_OFF;
		connector->base.encoder = NULL;
	}
	/* Enabled encoders without active connectors will be fixed in
	 * the crtc fixup. */
}

void i915_redisable_vga_power_on(struct drm_i915_private *dev_priv)
{
	i915_reg_t vga_reg = i915_vgacntrl_reg(dev_priv);

	if (!(I915_READ(vga_reg) & VGA_DISP_DISABLE)) {
		DRM_DEBUG_KMS("Something enabled VGA plane, disabling it\n");
		i915_disable_vga(dev_priv);
	}
}

void i915_redisable_vga(struct drm_i915_private *dev_priv)
{
	/* This function can be called both from intel_modeset_setup_hw_state or
	 * at a very early point in our resume sequence, where the power well
	 * structures are not yet restored. Since this function is at a very
	 * paranoid "someone might have enabled VGA while we were not looking"
	 * level, just check if the power well is enabled instead of trying to
	 * follow the "don't touch the power well if we don't need it" policy
	 * the rest of the driver uses. */
	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_VGA))
		return;

	i915_redisable_vga_power_on(dev_priv);

	intel_display_power_put(dev_priv, POWER_DOMAIN_VGA);
}

static bool primary_get_hw_state(struct intel_plane *plane)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);

	return I915_READ(DSPCNTR(plane->plane)) & DISPLAY_PLANE_ENABLE;
}

/* FIXME read out full plane state for all planes */
static void readout_plane_state(struct intel_crtc *crtc)
{
	struct drm_plane *primary = crtc->base.primary;
	struct intel_plane_state *plane_state =
		to_intel_plane_state(primary->state);

	plane_state->base.visible = crtc->active &&
		primary_get_hw_state(to_intel_plane(primary));

	if (plane_state->base.visible)
		crtc->base.state->plane_mask |= 1 << drm_plane_index(primary);
}

static void intel_modeset_readout_hw_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe;
	struct intel_crtc *crtc;
	struct intel_encoder *encoder;
	struct intel_connector *connector;
	int i;

	dev_priv->active_crtcs = 0;

	for_each_intel_crtc(dev, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		__drm_atomic_helper_crtc_destroy_state(&crtc_state->base);
		memset(crtc_state, 0, sizeof(*crtc_state));
		crtc_state->base.crtc = &crtc->base;

		crtc_state->base.active = crtc_state->base.enable =
			dev_priv->display.get_pipe_config(crtc, crtc_state);

		crtc->base.enabled = crtc_state->base.enable;
		crtc->active = crtc_state->base.active;

		if (crtc_state->base.active)
			dev_priv->active_crtcs |= 1 << crtc->pipe;

		readout_plane_state(crtc);

		DRM_DEBUG_KMS("[CRTC:%d:%s] hw state readout: %s\n",
			      crtc->base.base.id, crtc->base.name,
			      enableddisabled(crtc_state->base.active));
	}

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		struct intel_shared_dpll *pll = &dev_priv->shared_dplls[i];

		pll->on = pll->funcs.get_hw_state(dev_priv, pll,
						  &pll->state.hw_state);
		pll->state.crtc_mask = 0;
		for_each_intel_crtc(dev, crtc) {
			struct intel_crtc_state *crtc_state =
				to_intel_crtc_state(crtc->base.state);

			if (crtc_state->base.active &&
			    crtc_state->shared_dpll == pll)
				pll->state.crtc_mask |= 1 << crtc->pipe;
		}
		pll->active_mask = pll->state.crtc_mask;

		DRM_DEBUG_KMS("%s hw state readout: crtc_mask 0x%08x, on %i\n",
			      pll->name, pll->state.crtc_mask, pll->on);
	}

	for_each_intel_encoder(dev, encoder) {
		pipe = 0;

		if (encoder->get_hw_state(encoder, &pipe)) {
			struct intel_crtc_state *crtc_state;

			crtc = intel_get_crtc_for_pipe(dev_priv, pipe);
			crtc_state = to_intel_crtc_state(crtc->base.state);

			encoder->base.crtc = &crtc->base;
			crtc_state->output_types |= 1 << encoder->type;
			encoder->get_config(encoder, crtc_state);
		} else {
			encoder->base.crtc = NULL;
		}

		DRM_DEBUG_KMS("[ENCODER:%d:%s] hw state readout: %s, pipe %c\n",
			      encoder->base.base.id, encoder->base.name,
			      enableddisabled(encoder->base.crtc),
			      pipe_name(pipe));
	}

	for_each_intel_connector(dev, connector) {
		if (connector->get_hw_state(connector)) {
			connector->base.dpms = DRM_MODE_DPMS_ON;

			encoder = connector->encoder;
			connector->base.encoder = &encoder->base;

			if (encoder->base.crtc &&
			    encoder->base.crtc->state->active) {
				/*
				 * This has to be done during hardware readout
				 * because anything calling .crtc_disable may
				 * rely on the connector_mask being accurate.
				 */
				encoder->base.crtc->state->connector_mask |=
					1 << drm_connector_index(&connector->base);
				encoder->base.crtc->state->encoder_mask |=
					1 << drm_encoder_index(&encoder->base);
			}

		} else {
			connector->base.dpms = DRM_MODE_DPMS_OFF;
			connector->base.encoder = NULL;
		}
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] hw state readout: %s\n",
			      connector->base.base.id, connector->base.name,
			      enableddisabled(connector->base.encoder));
	}

	for_each_intel_crtc(dev, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		int pixclk = 0;

		crtc->base.hwmode = crtc_state->base.adjusted_mode;

		memset(&crtc->base.mode, 0, sizeof(crtc->base.mode));
		if (crtc_state->base.active) {
			intel_mode_from_pipe_config(&crtc->base.mode, crtc_state);
			intel_mode_from_pipe_config(&crtc_state->base.adjusted_mode, crtc_state);
			WARN_ON(drm_atomic_set_mode_for_crtc(crtc->base.state, &crtc->base.mode));

			/*
			 * The initial mode needs to be set in order to keep
			 * the atomic core happy. It wants a valid mode if the
			 * crtc's enabled, so we do the above call.
			 *
			 * But we don't set all the derived state fully, hence
			 * set a flag to indicate that a full recalculation is
			 * needed on the next commit.
			 */
			crtc_state->base.mode.private_flags = I915_MODE_FLAG_INHERITED;

			intel_crtc_compute_pixel_rate(crtc_state);

			if (INTEL_GEN(dev_priv) >= 9 || IS_BROADWELL(dev_priv) ||
			    IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
				pixclk = crtc_state->pixel_rate;
			else
				WARN_ON(dev_priv->display.modeset_calc_cdclk);

			/* pixel rate mustn't exceed 95% of cdclk with IPS on BDW */
			if (IS_BROADWELL(dev_priv) && crtc_state->ips_enabled)
				pixclk = DIV_ROUND_UP(pixclk * 100, 95);

			drm_calc_timestamping_constants(&crtc->base, &crtc->base.hwmode);
			update_scanline_offset(crtc);
		}

		dev_priv->min_pixclk[crtc->pipe] = pixclk;

		intel_pipe_config_sanity_check(dev_priv, crtc_state);
	}
}

static void
get_encoder_power_domains(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		u64 get_domains;
		enum intel_display_power_domain domain;

		if (!encoder->get_power_domains)
			continue;

		get_domains = encoder->get_power_domains(encoder);
		for_each_power_domain(domain, get_domains)
			intel_display_power_get(dev_priv, domain);
	}
}

/* Scan out the current hw modeset state,
 * and sanitizes it to the current state
 */
static void
intel_modeset_setup_hw_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe;
	struct intel_crtc *crtc;
	struct intel_encoder *encoder;
	int i;

	intel_modeset_readout_hw_state(dev);

	/* HW state is read out, now we need to sanitize this mess. */
	get_encoder_power_domains(dev_priv);

	for_each_intel_encoder(dev, encoder) {
		intel_sanitize_encoder(encoder);
	}

	for_each_pipe(dev_priv, pipe) {
		crtc = intel_get_crtc_for_pipe(dev_priv, pipe);

		intel_sanitize_crtc(crtc);
		intel_dump_pipe_config(crtc, crtc->config,
				       "[setup_hw_state]");
	}

	intel_modeset_update_connector_atomic_state(dev);

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		struct intel_shared_dpll *pll = &dev_priv->shared_dplls[i];

		if (!pll->on || pll->active_mask)
			continue;

		DRM_DEBUG_KMS("%s enabled but not in use, disabling\n", pll->name);

		pll->funcs.disable(dev_priv, pll);
		pll->on = false;
	}

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		vlv_wm_get_hw_state(dev);
	else if (IS_GEN9(dev_priv))
		skl_wm_get_hw_state(dev);
	else if (HAS_PCH_SPLIT(dev_priv))
		ilk_wm_get_hw_state(dev);

	for_each_intel_crtc(dev, crtc) {
		u64 put_domains;

		put_domains = modeset_get_crtc_power_domains(&crtc->base, crtc->config);
		if (WARN_ON(put_domains))
			modeset_put_power_domains(dev_priv, put_domains);
	}
	intel_display_set_init_power(dev_priv, false);

	intel_power_domains_verify_state(dev_priv);

	intel_fbc_init_pipe_state(dev_priv);
}

void intel_display_resume(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_atomic_state *state = dev_priv->modeset_restore_state;
	struct drm_modeset_acquire_ctx ctx;
	int ret;

	dev_priv->modeset_restore_state = NULL;
	if (state)
		state->acquire_ctx = &ctx;

	/*
	 * This is a cludge because with real atomic modeset mode_config.mutex
	 * won't be taken. Unfortunately some probed state like
	 * audio_codec_enable is still protected by mode_config.mutex, so lock
	 * it here for now.
	 */
	mutex_lock(&dev->mode_config.mutex);
	drm_modeset_acquire_init(&ctx, 0);

	while (1) {
		ret = drm_modeset_lock_all_ctx(dev, &ctx);
		if (ret != -EDEADLK)
			break;

		drm_modeset_backoff(&ctx);
	}

	if (!ret)
		ret = __intel_display_resume(dev, state);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	mutex_unlock(&dev->mode_config.mutex);

	if (ret)
		DRM_ERROR("Restoring old state failed with %i\n", ret);
	if (state)
		drm_atomic_state_put(state);
}

void intel_modeset_gem_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	intel_init_gt_powersave(dev_priv);

	intel_modeset_init_hw(dev);

	intel_setup_overlay(dev_priv);
}

int intel_connector_register(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	int ret;

	ret = intel_backlight_device_register(intel_connector);
	if (ret)
		goto err;

	return 0;

err:
	return ret;
}

void intel_connector_unregister(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	intel_backlight_device_unregister(intel_connector);
	intel_panel_destroy_backlight(connector);
}

void intel_modeset_cleanup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	flush_work(&dev_priv->atomic_helper.free_work);
	WARN_ON(!llist_empty(&dev_priv->atomic_helper.free_list));

	intel_disable_gt_powersave(dev_priv);

	/*
	 * Interrupts and polling as the first thing to avoid creating havoc.
	 * Too much stuff here (turning of connectors, ...) would
	 * experience fancy races otherwise.
	 */
	intel_irq_uninstall(dev_priv);

	/*
	 * Due to the hpd irq storm handling the hotplug work can re-arm the
	 * poll handlers. Hence disable polling after hpd handling is shut down.
	 */
	drm_kms_helper_poll_fini(dev);

	intel_unregister_dsm_handler();

	intel_fbc_global_disable(dev_priv);

	/* flush any delayed tasks or pending work */
	flush_scheduled_work();

	drm_mode_config_cleanup(dev);

	intel_cleanup_overlay(dev_priv);

	intel_cleanup_gt_powersave(dev_priv);

	intel_teardown_gmbus(dev_priv);
}

void intel_connector_attach_encoder(struct intel_connector *connector,
				    struct intel_encoder *encoder)
{
	connector->encoder = encoder;
	drm_mode_connector_attach_encoder(&connector->base,
					  &encoder->base);
}

/*
 * set vga decode state - true == enable VGA decode
 */
int intel_modeset_vga_set_state(struct drm_i915_private *dev_priv, bool state)
{
	unsigned reg = INTEL_GEN(dev_priv) >= 6 ? SNB_GMCH_CTRL : INTEL_GMCH_CTRL;
	u16 gmch_ctrl;

	if (pci_read_config_word(dev_priv->bridge_dev, reg, &gmch_ctrl)) {
		DRM_ERROR("failed to read control word\n");
		return -EIO;
	}

	if (!!(gmch_ctrl & INTEL_GMCH_VGA_DISABLE) == !state)
		return 0;

	if (state)
		gmch_ctrl &= ~INTEL_GMCH_VGA_DISABLE;
	else
		gmch_ctrl |= INTEL_GMCH_VGA_DISABLE;

	if (pci_write_config_word(dev_priv->bridge_dev, reg, gmch_ctrl)) {
		DRM_ERROR("failed to write control word\n");
		return -EIO;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)

struct intel_display_error_state {

	u32 power_well_driver;

	int num_transcoders;

	struct intel_cursor_error_state {
		u32 control;
		u32 position;
		u32 base;
		u32 size;
	} cursor[I915_MAX_PIPES];

	struct intel_pipe_error_state {
		bool power_domain_on;
		u32 source;
		u32 stat;
	} pipe[I915_MAX_PIPES];

	struct intel_plane_error_state {
		u32 control;
		u32 stride;
		u32 size;
		u32 pos;
		u32 addr;
		u32 surface;
		u32 tile_offset;
	} plane[I915_MAX_PIPES];

	struct intel_transcoder_error_state {
		bool power_domain_on;
		enum transcoder cpu_transcoder;

		u32 conf;

		u32 htotal;
		u32 hblank;
		u32 hsync;
		u32 vtotal;
		u32 vblank;
		u32 vsync;
	} transcoder[4];
};

struct intel_display_error_state *
intel_display_capture_error_state(struct drm_i915_private *dev_priv)
{
	struct intel_display_error_state *error;
	int transcoders[] = {
		TRANSCODER_A,
		TRANSCODER_B,
		TRANSCODER_C,
		TRANSCODER_EDP,
	};
	int i;

	if (INTEL_INFO(dev_priv)->num_pipes == 0)
		return NULL;

	error = kzalloc(sizeof(*error), GFP_ATOMIC);
	if (error == NULL)
		return NULL;

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		error->power_well_driver = I915_READ(HSW_PWR_WELL_DRIVER);

	for_each_pipe(dev_priv, i) {
		error->pipe[i].power_domain_on =
			__intel_display_power_is_enabled(dev_priv,
							 POWER_DOMAIN_PIPE(i));
		if (!error->pipe[i].power_domain_on)
			continue;

		error->cursor[i].control = I915_READ(CURCNTR(i));
		error->cursor[i].position = I915_READ(CURPOS(i));
		error->cursor[i].base = I915_READ(CURBASE(i));

		error->plane[i].control = I915_READ(DSPCNTR(i));
		error->plane[i].stride = I915_READ(DSPSTRIDE(i));
		if (INTEL_GEN(dev_priv) <= 3) {
			error->plane[i].size = I915_READ(DSPSIZE(i));
			error->plane[i].pos = I915_READ(DSPPOS(i));
		}
		if (INTEL_GEN(dev_priv) <= 7 && !IS_HASWELL(dev_priv))
			error->plane[i].addr = I915_READ(DSPADDR(i));
		if (INTEL_GEN(dev_priv) >= 4) {
			error->plane[i].surface = I915_READ(DSPSURF(i));
			error->plane[i].tile_offset = I915_READ(DSPTILEOFF(i));
		}

		error->pipe[i].source = I915_READ(PIPESRC(i));

		if (HAS_GMCH_DISPLAY(dev_priv))
			error->pipe[i].stat = I915_READ(PIPESTAT(i));
	}

	/* Note: this does not include DSI transcoders. */
	error->num_transcoders = INTEL_INFO(dev_priv)->num_pipes;
	if (HAS_DDI(dev_priv))
		error->num_transcoders++; /* Account for eDP. */

	for (i = 0; i < error->num_transcoders; i++) {
		enum transcoder cpu_transcoder = transcoders[i];

		error->transcoder[i].power_domain_on =
			__intel_display_power_is_enabled(dev_priv,
				POWER_DOMAIN_TRANSCODER(cpu_transcoder));
		if (!error->transcoder[i].power_domain_on)
			continue;

		error->transcoder[i].cpu_transcoder = cpu_transcoder;

		error->transcoder[i].conf = I915_READ(PIPECONF(cpu_transcoder));
		error->transcoder[i].htotal = I915_READ(HTOTAL(cpu_transcoder));
		error->transcoder[i].hblank = I915_READ(HBLANK(cpu_transcoder));
		error->transcoder[i].hsync = I915_READ(HSYNC(cpu_transcoder));
		error->transcoder[i].vtotal = I915_READ(VTOTAL(cpu_transcoder));
		error->transcoder[i].vblank = I915_READ(VBLANK(cpu_transcoder));
		error->transcoder[i].vsync = I915_READ(VSYNC(cpu_transcoder));
	}

	return error;
}

#define err_printf(e, ...) i915_error_printf(e, __VA_ARGS__)

void
intel_display_print_error_state(struct drm_i915_error_state_buf *m,
				struct intel_display_error_state *error)
{
	struct drm_i915_private *dev_priv = m->i915;
	int i;

	if (!error)
		return;

	err_printf(m, "Num Pipes: %d\n", INTEL_INFO(dev_priv)->num_pipes);
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		err_printf(m, "PWR_WELL_CTL2: %08x\n",
			   error->power_well_driver);
	for_each_pipe(dev_priv, i) {
		err_printf(m, "Pipe [%d]:\n", i);
		err_printf(m, "  Power: %s\n",
			   onoff(error->pipe[i].power_domain_on));
		err_printf(m, "  SRC: %08x\n", error->pipe[i].source);
		err_printf(m, "  STAT: %08x\n", error->pipe[i].stat);

		err_printf(m, "Plane [%d]:\n", i);
		err_printf(m, "  CNTR: %08x\n", error->plane[i].control);
		err_printf(m, "  STRIDE: %08x\n", error->plane[i].stride);
		if (INTEL_GEN(dev_priv) <= 3) {
			err_printf(m, "  SIZE: %08x\n", error->plane[i].size);
			err_printf(m, "  POS: %08x\n", error->plane[i].pos);
		}
		if (INTEL_GEN(dev_priv) <= 7 && !IS_HASWELL(dev_priv))
			err_printf(m, "  ADDR: %08x\n", error->plane[i].addr);
		if (INTEL_GEN(dev_priv) >= 4) {
			err_printf(m, "  SURF: %08x\n", error->plane[i].surface);
			err_printf(m, "  TILEOFF: %08x\n", error->plane[i].tile_offset);
		}

		err_printf(m, "Cursor [%d]:\n", i);
		err_printf(m, "  CNTR: %08x\n", error->cursor[i].control);
		err_printf(m, "  POS: %08x\n", error->cursor[i].position);
		err_printf(m, "  BASE: %08x\n", error->cursor[i].base);
	}

	for (i = 0; i < error->num_transcoders; i++) {
		err_printf(m, "CPU transcoder: %s\n",
			   transcoder_name(error->transcoder[i].cpu_transcoder));
		err_printf(m, "  Power: %s\n",
			   onoff(error->transcoder[i].power_domain_on));
		err_printf(m, "  CONF: %08x\n", error->transcoder[i].conf);
		err_printf(m, "  HTOTAL: %08x\n", error->transcoder[i].htotal);
		err_printf(m, "  HBLANK: %08x\n", error->transcoder[i].hblank);
		err_printf(m, "  HSYNC: %08x\n", error->transcoder[i].hsync);
		err_printf(m, "  VTOTAL: %08x\n", error->transcoder[i].vtotal);
		err_printf(m, "  VBLANK: %08x\n", error->transcoder[i].vblank);
		err_printf(m, "  VSYNC: %08x\n", error->transcoder[i].vsync);
	}
}

#endif
