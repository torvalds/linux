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
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_trace.h"
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_rect.h>
#include <linux/dma_remapping.h>
#include <linux/reservation.h>
#include <linux/dma-buf.h>

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

static void intel_crtc_update_cursor(struct drm_crtc *crtc, bool on);

static void i9xx_crtc_clock_get(struct intel_crtc *crtc,
				struct intel_crtc_state *pipe_config);
static void ironlake_pch_clock_get(struct intel_crtc *crtc,
				   struct intel_crtc_state *pipe_config);

static int intel_framebuffer_init(struct drm_device *dev,
				  struct intel_framebuffer *ifb,
				  struct drm_mode_fb_cmd2 *mode_cmd,
				  struct drm_i915_gem_object *obj);
static void i9xx_set_pipeconf(struct intel_crtc *intel_crtc);
static void intel_set_pipe_timings(struct intel_crtc *intel_crtc);
static void intel_cpu_transcoder_set_m_n(struct intel_crtc *crtc,
					 struct intel_link_m_n *m_n,
					 struct intel_link_m_n *m2_n2);
static void ironlake_set_pipeconf(struct drm_crtc *crtc);
static void haswell_set_pipeconf(struct drm_crtc *crtc);
static void intel_set_pipe_csc(struct drm_crtc *crtc);
static void vlv_prepare_pll(struct intel_crtc *crtc,
			    const struct intel_crtc_state *pipe_config);
static void chv_prepare_pll(struct intel_crtc *crtc,
			    const struct intel_crtc_state *pipe_config);
static void intel_begin_crtc_commit(struct drm_crtc *, struct drm_crtc_state *);
static void intel_finish_crtc_commit(struct drm_crtc *, struct drm_crtc_state *);
static void skl_init_scalers(struct drm_device *dev, struct intel_crtc *intel_crtc,
	struct intel_crtc_state *crtc_state);
static int i9xx_get_refclk(const struct intel_crtc_state *crtc_state,
			   int num_connectors);
static void skylake_pfit_enable(struct intel_crtc *crtc);
static void ironlake_pfit_disable(struct intel_crtc *crtc, bool force);
static void ironlake_pfit_enable(struct intel_crtc *crtc);
static void intel_modeset_setup_hw_state(struct drm_device *dev);

typedef struct {
	int	min, max;
} intel_range_t;

typedef struct {
	int	dot_limit;
	int	p2_slow, p2_fast;
} intel_p2_t;

typedef struct intel_limit intel_limit_t;
struct intel_limit {
	intel_range_t   dot, vco, n, m, m1, m2, p, p1;
	intel_p2_t	    p2;
};

/* returns HPLL frequency in kHz */
static int valleyview_get_vco(struct drm_i915_private *dev_priv)
{
	int hpll_freq, vco_freq[] = { 800, 1600, 2000, 2400 };

	/* Obtain SKU information */
	mutex_lock(&dev_priv->sb_lock);
	hpll_freq = vlv_cck_read(dev_priv, CCK_FUSE_REG) &
		CCK_FUSE_HPLL_FREQ_MASK;
	mutex_unlock(&dev_priv->sb_lock);

	return vco_freq[hpll_freq] * 1000;
}

static int vlv_get_cck_clock_hpll(struct drm_i915_private *dev_priv,
				  const char *name, u32 reg)
{
	u32 val;
	int divider;

	if (dev_priv->hpll_freq == 0)
		dev_priv->hpll_freq = valleyview_get_vco(dev_priv);

	mutex_lock(&dev_priv->sb_lock);
	val = vlv_cck_read(dev_priv, reg);
	mutex_unlock(&dev_priv->sb_lock);

	divider = val & CCK_FREQUENCY_VALUES;

	WARN((val & CCK_FREQUENCY_STATUS) !=
	     (divider << CCK_FREQUENCY_STATUS_SHIFT),
	     "%s change in progress\n", name);

	return DIV_ROUND_CLOSEST(dev_priv->hpll_freq << 1, divider + 1);
}

int
intel_pch_rawclk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	WARN_ON(!HAS_PCH_SPLIT(dev));

	return I915_READ(PCH_RAWCLK_FREQ) & RAWCLK_FREQ_MASK;
}

/* hrawclock is 1/4 the FSB frequency */
int intel_hrawclk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t clkcfg;

	/* There is no CLKCFG reg in Valleyview. VLV hrawclk is 200 MHz */
	if (IS_VALLEYVIEW(dev))
		return 200;

	clkcfg = I915_READ(CLKCFG);
	switch (clkcfg & CLKCFG_FSB_MASK) {
	case CLKCFG_FSB_400:
		return 100;
	case CLKCFG_FSB_533:
		return 133;
	case CLKCFG_FSB_667:
		return 166;
	case CLKCFG_FSB_800:
		return 200;
	case CLKCFG_FSB_1067:
		return 266;
	case CLKCFG_FSB_1333:
		return 333;
	/* these two are just a guess; one of them might be right */
	case CLKCFG_FSB_1600:
	case CLKCFG_FSB_1600_ALT:
		return 400;
	default:
		return 133;
	}
}

static void intel_update_czclk(struct drm_i915_private *dev_priv)
{
	if (!IS_VALLEYVIEW(dev_priv))
		return;

	dev_priv->czclk_freq = vlv_get_cck_clock_hpll(dev_priv, "czclk",
						      CCK_CZ_CLOCK_CONTROL);

	DRM_DEBUG_DRIVER("CZ clock rate: %d kHz\n", dev_priv->czclk_freq);
}

static inline u32 /* units of 100MHz */
intel_fdi_link_freq(struct drm_device *dev)
{
	if (IS_GEN5(dev)) {
		struct drm_i915_private *dev_priv = dev->dev_private;
		return (I915_READ(FDI_PLL_BIOS_0) & FDI_PLL_FB_CLOCK_MASK) + 2;
	} else
		return 27;
}

static const intel_limit_t intel_limits_i8xx_dac = {
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

static const intel_limit_t intel_limits_i8xx_dvo = {
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

static const intel_limit_t intel_limits_i8xx_lvds = {
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

static const intel_limit_t intel_limits_i9xx_sdvo = {
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

static const intel_limit_t intel_limits_i9xx_lvds = {
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


static const intel_limit_t intel_limits_g4x_sdvo = {
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

static const intel_limit_t intel_limits_g4x_hdmi = {
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

static const intel_limit_t intel_limits_g4x_single_channel_lvds = {
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

static const intel_limit_t intel_limits_g4x_dual_channel_lvds = {
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

static const intel_limit_t intel_limits_pineview_sdvo = {
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

static const intel_limit_t intel_limits_pineview_lvds = {
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
static const intel_limit_t intel_limits_ironlake_dac = {
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

static const intel_limit_t intel_limits_ironlake_single_lvds = {
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

static const intel_limit_t intel_limits_ironlake_dual_lvds = {
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
static const intel_limit_t intel_limits_ironlake_single_lvds_100m = {
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

static const intel_limit_t intel_limits_ironlake_dual_lvds_100m = {
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

static const intel_limit_t intel_limits_vlv = {
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

static const intel_limit_t intel_limits_chv = {
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

static const intel_limit_t intel_limits_bxt = {
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

/**
 * Returns whether any output on the specified pipe is of the specified type
 */
bool intel_pipe_has_type(struct intel_crtc *crtc, enum intel_output_type type)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_encoder *encoder;

	for_each_encoder_on_crtc(dev, &crtc->base, encoder)
		if (encoder->type == type)
			return true;

	return false;
}

/**
 * Returns whether any output on the specified pipe will have the specified
 * type after a staged modeset is complete, i.e., the same as
 * intel_pipe_has_type() but looking at encoder->new_crtc instead of
 * encoder->crtc.
 */
static bool intel_pipe_will_have_type(const struct intel_crtc_state *crtc_state,
				      int type)
{
	struct drm_atomic_state *state = crtc_state->base.state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	struct intel_encoder *encoder;
	int i, num_connectors = 0;

	for_each_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc != crtc_state->base.crtc)
			continue;

		num_connectors++;

		encoder = to_intel_encoder(connector_state->best_encoder);
		if (encoder->type == type)
			return true;
	}

	WARN_ON(num_connectors == 0);

	return false;
}

static const intel_limit_t *
intel_ironlake_limit(struct intel_crtc_state *crtc_state, int refclk)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;
	const intel_limit_t *limit;

	if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS)) {
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
	} else
		limit = &intel_limits_ironlake_dac;

	return limit;
}

static const intel_limit_t *
intel_g4x_limit(struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;
	const intel_limit_t *limit;

	if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		if (intel_is_dual_link_lvds(dev))
			limit = &intel_limits_g4x_dual_channel_lvds;
		else
			limit = &intel_limits_g4x_single_channel_lvds;
	} else if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_HDMI) ||
		   intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_ANALOG)) {
		limit = &intel_limits_g4x_hdmi;
	} else if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_SDVO)) {
		limit = &intel_limits_g4x_sdvo;
	} else /* The option is for other outputs */
		limit = &intel_limits_i9xx_sdvo;

	return limit;
}

static const intel_limit_t *
intel_limit(struct intel_crtc_state *crtc_state, int refclk)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;
	const intel_limit_t *limit;

	if (IS_BROXTON(dev))
		limit = &intel_limits_bxt;
	else if (HAS_PCH_SPLIT(dev))
		limit = intel_ironlake_limit(crtc_state, refclk);
	else if (IS_G4X(dev)) {
		limit = intel_g4x_limit(crtc_state);
	} else if (IS_PINEVIEW(dev)) {
		if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_pineview_lvds;
		else
			limit = &intel_limits_pineview_sdvo;
	} else if (IS_CHERRYVIEW(dev)) {
		limit = &intel_limits_chv;
	} else if (IS_VALLEYVIEW(dev)) {
		limit = &intel_limits_vlv;
	} else if (!IS_GEN2(dev)) {
		if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_i9xx_lvds;
		else
			limit = &intel_limits_i9xx_sdvo;
	} else {
		if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_i8xx_lvds;
		else if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_DVO))
			limit = &intel_limits_i8xx_dvo;
		else
			limit = &intel_limits_i8xx_dac;
	}
	return limit;
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
static int pnv_calc_dpll_params(int refclk, intel_clock_t *clock)
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

static int i9xx_calc_dpll_params(int refclk, intel_clock_t *clock)
{
	clock->m = i9xx_dpll_compute_m(clock);
	clock->p = clock->p1 * clock->p2;
	if (WARN_ON(clock->n + 2 == 0 || clock->p == 0))
		return 0;
	clock->vco = DIV_ROUND_CLOSEST(refclk * clock->m, clock->n + 2);
	clock->dot = DIV_ROUND_CLOSEST(clock->vco, clock->p);

	return clock->dot;
}

static int vlv_calc_dpll_params(int refclk, intel_clock_t *clock)
{
	clock->m = clock->m1 * clock->m2;
	clock->p = clock->p1 * clock->p2;
	if (WARN_ON(clock->n == 0 || clock->p == 0))
		return 0;
	clock->vco = DIV_ROUND_CLOSEST(refclk * clock->m, clock->n);
	clock->dot = DIV_ROUND_CLOSEST(clock->vco, clock->p);

	return clock->dot / 5;
}

int chv_calc_dpll_params(int refclk, intel_clock_t *clock)
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

static bool intel_PLL_is_valid(struct drm_device *dev,
			       const intel_limit_t *limit,
			       const intel_clock_t *clock)
{
	if (clock->n   < limit->n.min   || limit->n.max   < clock->n)
		INTELPllInvalid("n out of range\n");
	if (clock->p1  < limit->p1.min  || limit->p1.max  < clock->p1)
		INTELPllInvalid("p1 out of range\n");
	if (clock->m2  < limit->m2.min  || limit->m2.max  < clock->m2)
		INTELPllInvalid("m2 out of range\n");
	if (clock->m1  < limit->m1.min  || limit->m1.max  < clock->m1)
		INTELPllInvalid("m1 out of range\n");

	if (!IS_PINEVIEW(dev) && !IS_VALLEYVIEW(dev) && !IS_BROXTON(dev))
		if (clock->m1 <= clock->m2)
			INTELPllInvalid("m1 <= m2\n");

	if (!IS_VALLEYVIEW(dev) && !IS_BROXTON(dev)) {
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
i9xx_select_p2_div(const intel_limit_t *limit,
		   const struct intel_crtc_state *crtc_state,
		   int target)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;

	if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS)) {
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

static bool
i9xx_find_best_dpll(const intel_limit_t *limit,
		    struct intel_crtc_state *crtc_state,
		    int target, int refclk, intel_clock_t *match_clock,
		    intel_clock_t *best_clock)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;
	intel_clock_t clock;
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
					if (!intel_PLL_is_valid(dev, limit,
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

static bool
pnv_find_best_dpll(const intel_limit_t *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk, intel_clock_t *match_clock,
		   intel_clock_t *best_clock)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;
	intel_clock_t clock;
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
					if (!intel_PLL_is_valid(dev, limit,
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

static bool
g4x_find_best_dpll(const intel_limit_t *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk, intel_clock_t *match_clock,
		   intel_clock_t *best_clock)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;
	intel_clock_t clock;
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
					if (!intel_PLL_is_valid(dev, limit,
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
			       const intel_clock_t *calculated_clock,
			       const intel_clock_t *best_clock,
			       unsigned int best_error_ppm,
			       unsigned int *error_ppm)
{
	/*
	 * For CHV ignore the error and consider only the P value.
	 * Prefer a bigger P value based on HW requirements.
	 */
	if (IS_CHERRYVIEW(dev)) {
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

static bool
vlv_find_best_dpll(const intel_limit_t *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk, intel_clock_t *match_clock,
		   intel_clock_t *best_clock)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_device *dev = crtc->base.dev;
	intel_clock_t clock;
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

					if (!intel_PLL_is_valid(dev, limit,
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

static bool
chv_find_best_dpll(const intel_limit_t *limit,
		   struct intel_crtc_state *crtc_state,
		   int target, int refclk, intel_clock_t *match_clock,
		   intel_clock_t *best_clock)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_device *dev = crtc->base.dev;
	unsigned int best_error_ppm;
	intel_clock_t clock;
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

			if (!intel_PLL_is_valid(dev, limit, &clock))
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
			intel_clock_t *best_clock)
{
	int refclk = i9xx_get_refclk(crtc_state, 0);

	return chv_find_best_dpll(intel_limit(crtc_state, refclk), crtc_state,
				  target_clock, refclk, NULL, best_clock);
}

bool intel_crtc_active(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

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
	return intel_crtc->active && crtc->primary->state->fb &&
		intel_crtc->config->base.adjusted_mode.crtc_clock;
}

enum transcoder intel_pipe_to_cpu_transcoder(struct drm_i915_private *dev_priv,
					     enum pipe pipe)
{
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	return intel_crtc->config->cpu_transcoder;
}

static bool pipe_dsl_stopped(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	i915_reg_t reg = PIPEDSL(pipe);
	u32 line1, line2;
	u32 line_mask;

	if (IS_GEN2(dev))
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
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum transcoder cpu_transcoder = crtc->config->cpu_transcoder;
	enum pipe pipe = crtc->pipe;

	if (INTEL_INFO(dev)->gen >= 4) {
		i915_reg_t reg = PIPECONF(cpu_transcoder);

		/* Wait for the Pipe State to go off */
		if (wait_for((I915_READ(reg) & I965_PIPECONF_ACTIVE) == 0,
			     100))
			WARN(1, "pipe_off wait timed out\n");
	} else {
		/* Wait for the display line to settle */
		if (wait_for(pipe_dsl_stopped(dev, pipe), 100))
			WARN(1, "pipe_off wait timed out\n");
	}
}

static const char *state_string(bool enabled)
{
	return enabled ? "on" : "off";
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
	     state_string(state), state_string(cur_state));
}

/* XXX: the dsi pll is shared between MIPI DSI ports */
static void assert_dsi_pll(struct drm_i915_private *dev_priv, bool state)
{
	u32 val;
	bool cur_state;

	mutex_lock(&dev_priv->sb_lock);
	val = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	mutex_unlock(&dev_priv->sb_lock);

	cur_state = val & DSI_PLL_VCO_EN;
	I915_STATE_WARN(cur_state != state,
	     "DSI PLL state assertion failure (expected %s, current %s)\n",
	     state_string(state), state_string(cur_state));
}
#define assert_dsi_pll_enabled(d) assert_dsi_pll(d, true)
#define assert_dsi_pll_disabled(d) assert_dsi_pll(d, false)

struct intel_shared_dpll *
intel_crtc_to_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = crtc->base.dev->dev_private;

	if (crtc->config->shared_dpll < 0)
		return NULL;

	return &dev_priv->shared_dplls[crtc->config->shared_dpll];
}

/* For ILK+ */
void assert_shared_dpll(struct drm_i915_private *dev_priv,
			struct intel_shared_dpll *pll,
			bool state)
{
	bool cur_state;
	struct intel_dpll_hw_state hw_state;

	if (WARN (!pll,
		  "asserting DPLL %s with no DPLL\n", state_string(state)))
		return;

	cur_state = pll->get_hw_state(dev_priv, pll, &hw_state);
	I915_STATE_WARN(cur_state != state,
	     "%s assertion failure (expected %s, current %s)\n",
	     pll->name, state_string(state), state_string(cur_state));
}

static void assert_fdi_tx(struct drm_i915_private *dev_priv,
			  enum pipe pipe, bool state)
{
	bool cur_state;
	enum transcoder cpu_transcoder = intel_pipe_to_cpu_transcoder(dev_priv,
								      pipe);

	if (HAS_DDI(dev_priv->dev)) {
		/* DDI does not have a specific FDI_TX register */
		u32 val = I915_READ(TRANS_DDI_FUNC_CTL(cpu_transcoder));
		cur_state = !!(val & TRANS_DDI_FUNC_ENABLE);
	} else {
		u32 val = I915_READ(FDI_TX_CTL(pipe));
		cur_state = !!(val & FDI_TX_ENABLE);
	}
	I915_STATE_WARN(cur_state != state,
	     "FDI TX state assertion failure (expected %s, current %s)\n",
	     state_string(state), state_string(cur_state));
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
	     state_string(state), state_string(cur_state));
}
#define assert_fdi_rx_enabled(d, p) assert_fdi_rx(d, p, true)
#define assert_fdi_rx_disabled(d, p) assert_fdi_rx(d, p, false)

static void assert_fdi_tx_pll_enabled(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	u32 val;

	/* ILK FDI PLL is always enabled */
	if (INTEL_INFO(dev_priv->dev)->gen == 5)
		return;

	/* On Haswell, DDI ports are responsible for the FDI PLL setup */
	if (HAS_DDI(dev_priv->dev))
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
	     state_string(state), state_string(cur_state));
}

void assert_panel_unlocked(struct drm_i915_private *dev_priv,
			   enum pipe pipe)
{
	struct drm_device *dev = dev_priv->dev;
	i915_reg_t pp_reg;
	u32 val;
	enum pipe panel_pipe = PIPE_A;
	bool locked = true;

	if (WARN_ON(HAS_DDI(dev)))
		return;

	if (HAS_PCH_SPLIT(dev)) {
		u32 port_sel;

		pp_reg = PCH_PP_CONTROL;
		port_sel = I915_READ(PCH_PP_ON_DELAYS) & PANEL_PORT_SELECT_MASK;

		if (port_sel == PANEL_PORT_SELECT_LVDS &&
		    I915_READ(PCH_LVDS) & LVDS_PIPEB_SELECT)
			panel_pipe = PIPE_B;
		/* XXX: else fix for eDP */
	} else if (IS_VALLEYVIEW(dev)) {
		/* presumably write lock depends on pipe, not port select */
		pp_reg = VLV_PIPE_PP_CONTROL(pipe);
		panel_pipe = pipe;
	} else {
		pp_reg = PP_CONTROL;
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
	struct drm_device *dev = dev_priv->dev;
	bool cur_state;

	if (IS_845G(dev) || IS_I865G(dev))
		cur_state = I915_READ(CURCNTR(PIPE_A)) & CURSOR_ENABLE;
	else
		cur_state = I915_READ(CURCNTR(pipe)) & CURSOR_MODE;

	I915_STATE_WARN(cur_state != state,
	     "cursor on pipe %c assertion failure (expected %s, current %s)\n",
	     pipe_name(pipe), state_string(state), state_string(cur_state));
}
#define assert_cursor_enabled(d, p) assert_cursor(d, p, true)
#define assert_cursor_disabled(d, p) assert_cursor(d, p, false)

void assert_pipe(struct drm_i915_private *dev_priv,
		 enum pipe pipe, bool state)
{
	bool cur_state;
	enum transcoder cpu_transcoder = intel_pipe_to_cpu_transcoder(dev_priv,
								      pipe);

	/* if we need the pipe quirk it must be always on */
	if ((pipe == PIPE_A && dev_priv->quirks & QUIRK_PIPEA_FORCE) ||
	    (pipe == PIPE_B && dev_priv->quirks & QUIRK_PIPEB_FORCE))
		state = true;

	if (!intel_display_power_is_enabled(dev_priv,
				POWER_DOMAIN_TRANSCODER(cpu_transcoder))) {
		cur_state = false;
	} else {
		u32 val = I915_READ(PIPECONF(cpu_transcoder));
		cur_state = !!(val & PIPECONF_ENABLE);
	}

	I915_STATE_WARN(cur_state != state,
	     "pipe %c assertion failure (expected %s, current %s)\n",
	     pipe_name(pipe), state_string(state), state_string(cur_state));
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
	     plane_name(plane), state_string(state), state_string(cur_state));
}

#define assert_plane_enabled(d, p) assert_plane(d, p, true)
#define assert_plane_disabled(d, p) assert_plane(d, p, false)

static void assert_planes_disabled(struct drm_i915_private *dev_priv,
				   enum pipe pipe)
{
	struct drm_device *dev = dev_priv->dev;
	int i;

	/* Primary planes are fixed to pipes on gen4+ */
	if (INTEL_INFO(dev)->gen >= 4) {
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
	struct drm_device *dev = dev_priv->dev;
	int sprite;

	if (INTEL_INFO(dev)->gen >= 9) {
		for_each_sprite(dev_priv, pipe, sprite) {
			u32 val = I915_READ(PLANE_CTL(pipe, sprite));
			I915_STATE_WARN(val & PLANE_CTL_ENABLE,
			     "plane %d assertion failure, should be off on pipe %c but is still active\n",
			     sprite, pipe_name(pipe));
		}
	} else if (IS_VALLEYVIEW(dev)) {
		for_each_sprite(dev_priv, pipe, sprite) {
			u32 val = I915_READ(SPCNTR(pipe, sprite));
			I915_STATE_WARN(val & SP_ENABLE,
			     "sprite %c assertion failure, should be off on pipe %c but is still active\n",
			     sprite_name(pipe, sprite), pipe_name(pipe));
		}
	} else if (INTEL_INFO(dev)->gen >= 7) {
		u32 val = I915_READ(SPRCTL(pipe));
		I915_STATE_WARN(val & SPRITE_ENABLE,
		     "sprite %c assertion failure, should be off on pipe %c but is still active\n",
		     plane_name(pipe), pipe_name(pipe));
	} else if (INTEL_INFO(dev)->gen >= 5) {
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

static void ibx_assert_pch_refclk_enabled(struct drm_i915_private *dev_priv)
{
	u32 val;
	bool enabled;

	I915_STATE_WARN_ON(!(HAS_PCH_IBX(dev_priv->dev) || HAS_PCH_CPT(dev_priv->dev)));

	val = I915_READ(PCH_DREF_CONTROL);
	enabled = !!(val & (DREF_SSC_SOURCE_MASK | DREF_NONSPREAD_SOURCE_MASK |
			    DREF_SUPERSPREAD_SOURCE_MASK));
	I915_STATE_WARN(!enabled, "PCH refclk assertion failure, should be active but is disabled\n");
}

static void assert_pch_transcoder_disabled(struct drm_i915_private *dev_priv,
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

	if (HAS_PCH_CPT(dev_priv->dev)) {
		u32 trans_dp_ctl = I915_READ(TRANS_DP_CTL(pipe));
		if ((trans_dp_ctl & TRANS_DP_PORT_SEL_MASK) != port_sel)
			return false;
	} else if (IS_CHERRYVIEW(dev_priv->dev)) {
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

	if (HAS_PCH_CPT(dev_priv->dev)) {
		if ((val & SDVO_PIPE_SEL_MASK_CPT) != SDVO_PIPE_SEL_CPT(pipe))
			return false;
	} else if (IS_CHERRYVIEW(dev_priv->dev)) {
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

	if (HAS_PCH_CPT(dev_priv->dev)) {
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
	if (HAS_PCH_CPT(dev_priv->dev)) {
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

	I915_STATE_WARN(HAS_PCH_IBX(dev_priv->dev) && (val & DP_PORT_EN) == 0
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

	I915_STATE_WARN(HAS_PCH_IBX(dev_priv->dev) && (val & SDVO_ENABLE) == 0
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

static void vlv_enable_pll(struct intel_crtc *crtc,
			   const struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	i915_reg_t reg = DPLL(crtc->pipe);
	u32 dpll = pipe_config->dpll_hw_state.dpll;

	assert_pipe_disabled(dev_priv, crtc->pipe);

	/* No really, not for ILK+ */
	BUG_ON(!IS_VALLEYVIEW(dev_priv->dev));

	/* PLL is protected by panel, make sure we can write it */
	if (IS_MOBILE(dev_priv->dev))
		assert_panel_unlocked(dev_priv, crtc->pipe);

	I915_WRITE(reg, dpll);
	POSTING_READ(reg);
	udelay(150);

	if (wait_for(((I915_READ(reg) & DPLL_LOCK_VLV) == DPLL_LOCK_VLV), 1))
		DRM_ERROR("DPLL %d failed to lock\n", crtc->pipe);

	I915_WRITE(DPLL_MD(crtc->pipe), pipe_config->dpll_hw_state.dpll_md);
	POSTING_READ(DPLL_MD(crtc->pipe));

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

static void chv_enable_pll(struct intel_crtc *crtc,
			   const struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = crtc->pipe;
	enum dpio_channel port = vlv_pipe_to_channel(pipe);
	u32 tmp;

	assert_pipe_disabled(dev_priv, crtc->pipe);

	BUG_ON(!IS_CHERRYVIEW(dev_priv->dev));

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
	if (wait_for(((I915_READ(DPLL(pipe)) & DPLL_LOCK_VLV) == DPLL_LOCK_VLV), 1))
		DRM_ERROR("PLL %d failed to lock\n", pipe);

	/* not sure when this should be written */
	I915_WRITE(DPLL_MD(pipe), pipe_config->dpll_hw_state.dpll_md);
	POSTING_READ(DPLL_MD(pipe));
}

static int intel_num_dvo_pipes(struct drm_device *dev)
{
	struct intel_crtc *crtc;
	int count = 0;

	for_each_intel_crtc(dev, crtc)
		count += crtc->base.state->active &&
			intel_pipe_has_type(crtc, INTEL_OUTPUT_DVO);

	return count;
}

static void i9xx_enable_pll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	i915_reg_t reg = DPLL(crtc->pipe);
	u32 dpll = crtc->config->dpll_hw_state.dpll;

	assert_pipe_disabled(dev_priv, crtc->pipe);

	/* No really, not for ILK+ */
	BUG_ON(INTEL_INFO(dev)->gen >= 5);

	/* PLL is protected by panel, make sure we can write it */
	if (IS_MOBILE(dev) && !IS_I830(dev))
		assert_panel_unlocked(dev_priv, crtc->pipe);

	/* Enable DVO 2x clock on both PLLs if necessary */
	if (IS_I830(dev) && intel_num_dvo_pipes(dev) > 0) {
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

	if (INTEL_INFO(dev)->gen >= 4) {
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
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe = crtc->pipe;

	/* Disable DVO 2x clock on both PLLs if necessary */
	if (IS_I830(dev) &&
	    intel_pipe_has_type(crtc, INTEL_OUTPUT_DVO) &&
	    !intel_num_dvo_pipes(dev)) {
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

	/*
	 * Leave integrated clock source and reference clock enabled for pipe B.
	 * The latter is needed for VGA hotplug / manual detection.
	 */
	val = DPLL_VGA_MODE_DIS;
	if (pipe == PIPE_B)
		val = DPLL_INTEGRATED_CRI_CLK_VLV | DPLL_REF_CLK_ENABLE_VLV;
	I915_WRITE(DPLL(pipe), val);
	POSTING_READ(DPLL(pipe));

}

static void chv_disable_pll(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	enum dpio_channel port = vlv_pipe_to_channel(pipe);
	u32 val;

	/* Make sure the pipe isn't still relying on us */
	assert_pipe_disabled(dev_priv, pipe);

	/* Set PLL en = 0 */
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

	if (wait_for((I915_READ(dpll_reg) & port_mask) == expected_mask, 1000))
		WARN(1, "timed out waiting for port %c ready: got 0x%x, expected 0x%x\n",
		     port_name(dport->port), I915_READ(dpll_reg) & port_mask, expected_mask);
}

static void intel_prepare_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_shared_dpll *pll = intel_crtc_to_shared_dpll(crtc);

	if (WARN_ON(pll == NULL))
		return;

	WARN_ON(!pll->config.crtc_mask);
	if (pll->active == 0) {
		DRM_DEBUG_DRIVER("setting up %s\n", pll->name);
		WARN_ON(pll->on);
		assert_shared_dpll_disabled(dev_priv, pll);

		pll->mode_set(dev_priv, pll);
	}
}

/**
 * intel_enable_shared_dpll - enable PCH PLL
 * @dev_priv: i915 private structure
 * @pipe: pipe PLL to enable
 *
 * The PCH PLL needs to be enabled before the PCH transcoder, since it
 * drives the transcoder clock.
 */
static void intel_enable_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_shared_dpll *pll = intel_crtc_to_shared_dpll(crtc);

	if (WARN_ON(pll == NULL))
		return;

	if (WARN_ON(pll->config.crtc_mask == 0))
		return;

	DRM_DEBUG_KMS("enable %s (active %d, on? %d) for crtc %d\n",
		      pll->name, pll->active, pll->on,
		      crtc->base.base.id);

	if (pll->active++) {
		WARN_ON(!pll->on);
		assert_shared_dpll_enabled(dev_priv, pll);
		return;
	}
	WARN_ON(pll->on);

	intel_display_power_get(dev_priv, POWER_DOMAIN_PLLS);

	DRM_DEBUG_KMS("enabling %s\n", pll->name);
	pll->enable(dev_priv, pll);
	pll->on = true;
}

static void intel_disable_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_shared_dpll *pll = intel_crtc_to_shared_dpll(crtc);

	/* PCH only available on ILK+ */
	if (INTEL_INFO(dev)->gen < 5)
		return;

	if (pll == NULL)
		return;

	if (WARN_ON(!(pll->config.crtc_mask & (1 << drm_crtc_index(&crtc->base)))))
		return;

	DRM_DEBUG_KMS("disable %s (active %d, on? %d) for crtc %d\n",
		      pll->name, pll->active, pll->on,
		      crtc->base.base.id);

	if (WARN_ON(pll->active == 0)) {
		assert_shared_dpll_disabled(dev_priv, pll);
		return;
	}

	assert_shared_dpll_enabled(dev_priv, pll);
	WARN_ON(!pll->on);
	if (--pll->active)
		return;

	DRM_DEBUG_KMS("disabling %s\n", pll->name);
	pll->disable(dev_priv, pll);
	pll->on = false;

	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);
}

static void ironlake_enable_pch_transcoder(struct drm_i915_private *dev_priv,
					   enum pipe pipe)
{
	struct drm_device *dev = dev_priv->dev;
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	i915_reg_t reg;
	uint32_t val, pipeconf_val;

	/* PCH only available on ILK+ */
	BUG_ON(!HAS_PCH_SPLIT(dev));

	/* Make sure PCH DPLL is enabled */
	assert_shared_dpll_enabled(dev_priv,
				   intel_crtc_to_shared_dpll(intel_crtc));

	/* FDI must be feeding us bits for PCH ports */
	assert_fdi_tx_enabled(dev_priv, pipe);
	assert_fdi_rx_enabled(dev_priv, pipe);

	if (HAS_PCH_CPT(dev)) {
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

	if (HAS_PCH_IBX(dev_priv->dev)) {
		/*
		 * Make the BPC in transcoder be consistent with
		 * that in pipeconf reg. For HDMI we must use 8bpc
		 * here for both 8bpc and 12bpc.
		 */
		val &= ~PIPECONF_BPC_MASK;
		if (intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_HDMI))
			val |= PIPECONF_8BPC;
		else
			val |= pipeconf_val & PIPECONF_BPC_MASK;
	}

	val &= ~TRANS_INTERLACE_MASK;
	if ((pipeconf_val & PIPECONF_INTERLACE_MASK) == PIPECONF_INTERLACED_ILK)
		if (HAS_PCH_IBX(dev_priv->dev) &&
		    intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_SDVO))
			val |= TRANS_LEGACY_INTERLACED_ILK;
		else
			val |= TRANS_INTERLACED;
	else
		val |= TRANS_PROGRESSIVE;

	I915_WRITE(reg, val | TRANS_ENABLE);
	if (wait_for(I915_READ(reg) & TRANS_STATE_ENABLE, 100))
		DRM_ERROR("failed to enable transcoder %c\n", pipe_name(pipe));
}

static void lpt_enable_pch_transcoder(struct drm_i915_private *dev_priv,
				      enum transcoder cpu_transcoder)
{
	u32 val, pipeconf_val;

	/* PCH only available on ILK+ */
	BUG_ON(!HAS_PCH_SPLIT(dev_priv->dev));

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
	if (wait_for(I915_READ(LPT_TRANSCONF) & TRANS_STATE_ENABLE, 100))
		DRM_ERROR("Failed to enable PCH transcoder\n");
}

static void ironlake_disable_pch_transcoder(struct drm_i915_private *dev_priv,
					    enum pipe pipe)
{
	struct drm_device *dev = dev_priv->dev;
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
	if (wait_for((I915_READ(reg) & TRANS_STATE_ENABLE) == 0, 50))
		DRM_ERROR("failed to disable transcoder %c\n", pipe_name(pipe));

	if (HAS_PCH_CPT(dev)) {
		/* Workaround: Clear the timing override chicken bit again. */
		reg = TRANS_CHICKEN2(pipe);
		val = I915_READ(reg);
		val &= ~TRANS_CHICKEN2_TIMING_OVERRIDE;
		I915_WRITE(reg, val);
	}
}

static void lpt_disable_pch_transcoder(struct drm_i915_private *dev_priv)
{
	u32 val;

	val = I915_READ(LPT_TRANSCONF);
	val &= ~TRANS_ENABLE;
	I915_WRITE(LPT_TRANSCONF, val);
	/* wait for PCH transcoder off, transcoder state */
	if (wait_for((I915_READ(LPT_TRANSCONF) & TRANS_STATE_ENABLE) == 0, 50))
		DRM_ERROR("Failed to disable PCH transcoder\n");

	/* Workaround: clear timing override bit. */
	val = I915_READ(TRANS_CHICKEN2(PIPE_A));
	val &= ~TRANS_CHICKEN2_TIMING_OVERRIDE;
	I915_WRITE(TRANS_CHICKEN2(PIPE_A), val);
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
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe = crtc->pipe;
	enum transcoder cpu_transcoder = crtc->config->cpu_transcoder;
	enum pipe pch_transcoder;
	i915_reg_t reg;
	u32 val;

	DRM_DEBUG_KMS("enabling pipe %c\n", pipe_name(pipe));

	assert_planes_disabled(dev_priv, pipe);
	assert_cursor_disabled(dev_priv, pipe);
	assert_sprites_disabled(dev_priv, pipe);

	if (HAS_PCH_LPT(dev_priv->dev))
		pch_transcoder = TRANSCODER_A;
	else
		pch_transcoder = pipe;

	/*
	 * A pipe without a PLL won't actually be able to drive bits from
	 * a plane.  On ILK+ the pipe PLLs are integrated, so we don't
	 * need the check.
	 */
	if (HAS_GMCH_DISPLAY(dev_priv->dev))
		if (crtc->config->has_dsi_encoder)
			assert_dsi_pll_enabled(dev_priv);
		else
			assert_pll_enabled(dev_priv, pipe);
	else {
		if (crtc->config->has_pch_encoder) {
			/* if driving the PCH, we need FDI enabled */
			assert_fdi_rx_pll_enabled(dev_priv, pch_transcoder);
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
	struct drm_i915_private *dev_priv = crtc->base.dev->dev_private;
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

static bool need_vtd_wa(struct drm_device *dev)
{
#ifdef CONFIG_INTEL_IOMMU
	if (INTEL_INFO(dev)->gen >= 6 && intel_iommu_gfx_mapped)
		return true;
#endif
	return false;
}

unsigned int
intel_tile_height(struct drm_device *dev, uint32_t pixel_format,
		  uint64_t fb_format_modifier, unsigned int plane)
{
	unsigned int tile_height;
	uint32_t pixel_bytes;

	switch (fb_format_modifier) {
	case DRM_FORMAT_MOD_NONE:
		tile_height = 1;
		break;
	case I915_FORMAT_MOD_X_TILED:
		tile_height = IS_GEN2(dev) ? 16 : 8;
		break;
	case I915_FORMAT_MOD_Y_TILED:
		tile_height = 32;
		break;
	case I915_FORMAT_MOD_Yf_TILED:
		pixel_bytes = drm_format_plane_cpp(pixel_format, plane);
		switch (pixel_bytes) {
		default:
		case 1:
			tile_height = 64;
			break;
		case 2:
		case 4:
			tile_height = 32;
			break;
		case 8:
			tile_height = 16;
			break;
		case 16:
			WARN_ONCE(1,
				  "128-bit pixels are not supported for display!");
			tile_height = 16;
			break;
		}
		break;
	default:
		MISSING_CASE(fb_format_modifier);
		tile_height = 1;
		break;
	}

	return tile_height;
}

unsigned int
intel_fb_align_height(struct drm_device *dev, unsigned int height,
		      uint32_t pixel_format, uint64_t fb_format_modifier)
{
	return ALIGN(height, intel_tile_height(dev, pixel_format,
					       fb_format_modifier, 0));
}

static void
intel_fill_fb_ggtt_view(struct i915_ggtt_view *view, struct drm_framebuffer *fb,
			const struct drm_plane_state *plane_state)
{
	struct intel_rotation_info *info = &view->params.rotation_info;
	unsigned int tile_height, tile_pitch;

	*view = i915_ggtt_view_normal;

	if (!plane_state)
		return;

	if (!intel_rotation_90_or_270(plane_state->rotation))
		return;

	*view = i915_ggtt_view_rotated;

	info->height = fb->height;
	info->pixel_format = fb->pixel_format;
	info->pitch = fb->pitches[0];
	info->uv_offset = fb->offsets[1];
	info->fb_modifier = fb->modifier[0];

	tile_height = intel_tile_height(fb->dev, fb->pixel_format,
					fb->modifier[0], 0);
	tile_pitch = PAGE_SIZE / tile_height;
	info->width_pages = DIV_ROUND_UP(fb->pitches[0], tile_pitch);
	info->height_pages = DIV_ROUND_UP(fb->height, tile_height);
	info->size = info->width_pages * info->height_pages * PAGE_SIZE;

	if (info->pixel_format == DRM_FORMAT_NV12) {
		tile_height = intel_tile_height(fb->dev, fb->pixel_format,
						fb->modifier[0], 1);
		tile_pitch = PAGE_SIZE / tile_height;
		info->width_pages_uv = DIV_ROUND_UP(fb->pitches[0], tile_pitch);
		info->height_pages_uv = DIV_ROUND_UP(fb->height / 2,
						     tile_height);
		info->size_uv = info->width_pages_uv * info->height_pages_uv *
				PAGE_SIZE;
	}
}

static unsigned int intel_linear_alignment(struct drm_i915_private *dev_priv)
{
	if (INTEL_INFO(dev_priv)->gen >= 9)
		return 256 * 1024;
	else if (IS_BROADWATER(dev_priv) || IS_CRESTLINE(dev_priv) ||
		 IS_VALLEYVIEW(dev_priv))
		return 128 * 1024;
	else if (INTEL_INFO(dev_priv)->gen >= 4)
		return 4 * 1024;
	else
		return 0;
}

int
intel_pin_and_fence_fb_obj(struct drm_plane *plane,
			   struct drm_framebuffer *fb,
			   const struct drm_plane_state *plane_state)
{
	struct drm_device *dev = fb->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct i915_ggtt_view view;
	u32 alignment;
	int ret;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	switch (fb->modifier[0]) {
	case DRM_FORMAT_MOD_NONE:
		alignment = intel_linear_alignment(dev_priv);
		break;
	case I915_FORMAT_MOD_X_TILED:
		if (INTEL_INFO(dev)->gen >= 9)
			alignment = 256 * 1024;
		else {
			/* pin() will align the object as required by fence */
			alignment = 0;
		}
		break;
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Yf_TILED:
		if (WARN_ONCE(INTEL_INFO(dev)->gen < 9,
			  "Y tiling bo slipped through, driver bug!\n"))
			return -EINVAL;
		alignment = 1 * 1024 * 1024;
		break;
	default:
		MISSING_CASE(fb->modifier[0]);
		return -EINVAL;
	}

	intel_fill_fb_ggtt_view(&view, fb, plane_state);

	/* Note that the w/a also requires 64 PTE of padding following the
	 * bo. We currently fill all unused PTE with the shadow page and so
	 * we should always have valid PTE following the scanout preventing
	 * the VT-d warning.
	 */
	if (need_vtd_wa(dev) && alignment < 256 * 1024)
		alignment = 256 * 1024;

	/*
	 * Global gtt pte registers are special registers which actually forward
	 * writes to a chunk of system memory. Which means that there is no risk
	 * that the register values disappear as soon as we call
	 * intel_runtime_pm_put(), so it is correct to wrap only the
	 * pin/unpin/fence and not more.
	 */
	intel_runtime_pm_get(dev_priv);

	ret = i915_gem_object_pin_to_display_plane(obj, alignment,
						   &view);
	if (ret)
		goto err_pm;

	/* Install a fence for tiled scan-out. Pre-i965 always needs a
	 * fence, whereas 965+ only requires a fence if using
	 * framebuffer compression.  For simplicity, we always install
	 * a fence as the cost is not that onerous.
	 */
	if (view.type == I915_GGTT_VIEW_NORMAL) {
		ret = i915_gem_object_get_fence(obj);
		if (ret == -EDEADLK) {
			/*
			 * -EDEADLK means there are no free fences
			 * no pending flips.
			 *
			 * This is propagated to atomic, but it uses
			 * -EDEADLK to force a locking recovery, so
			 * change the returned error to -EBUSY.
			 */
			ret = -EBUSY;
			goto err_unpin;
		} else if (ret)
			goto err_unpin;

		i915_gem_object_pin_fence(obj);
	}

	intel_runtime_pm_put(dev_priv);
	return 0;

err_unpin:
	i915_gem_object_unpin_from_display_plane(obj, &view);
err_pm:
	intel_runtime_pm_put(dev_priv);
	return ret;
}

static void intel_unpin_fb_obj(struct drm_framebuffer *fb,
			       const struct drm_plane_state *plane_state)
{
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct i915_ggtt_view view;

	WARN_ON(!mutex_is_locked(&obj->base.dev->struct_mutex));

	intel_fill_fb_ggtt_view(&view, fb, plane_state);

	if (view.type == I915_GGTT_VIEW_NORMAL)
		i915_gem_object_unpin_fence(obj);

	i915_gem_object_unpin_from_display_plane(obj, &view);
}

/* Computes the linear offset to the base tile and adjusts x, y. bytes per pixel
 * is assumed to be a power-of-two. */
unsigned long intel_gen4_compute_page_offset(struct drm_i915_private *dev_priv,
					     int *x, int *y,
					     unsigned int tiling_mode,
					     unsigned int cpp,
					     unsigned int pitch)
{
	if (tiling_mode != I915_TILING_NONE) {
		unsigned int tile_rows, tiles;

		tile_rows = *y / 8;
		*y %= 8;

		tiles = *x / (512/cpp);
		*x %= 512/cpp;

		return tile_rows * pitch * 8 + tiles * 4096;
	} else {
		unsigned int alignment = intel_linear_alignment(dev_priv) - 1;
		unsigned int offset;

		offset = *y * pitch + *x * cpp;
		*y = (offset & alignment) / pitch;
		*x = ((offset & alignment) - *y * pitch) / cpp;
		return offset & ~alignment;
	}
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
	if (size_aligned * 2 > dev_priv->gtt.stolen_usable_size)
		return false;

	obj = i915_gem_object_create_stolen_for_preallocated(dev,
							     base_aligned,
							     base_aligned,
							     size_aligned);
	if (!obj)
		return false;

	obj->tiling_mode = plane_config->tiling;
	if (obj->tiling_mode == I915_TILING_X)
		obj->stride = fb->pitches[0];

	mode_cmd.pixel_format = fb->pixel_format;
	mode_cmd.width = fb->width;
	mode_cmd.height = fb->height;
	mode_cmd.pitches[0] = fb->pitches[0];
	mode_cmd.modifier[0] = fb->modifier[0];
	mode_cmd.flags = DRM_MODE_FB_MODIFIERS;

	mutex_lock(&dev->struct_mutex);
	if (intel_framebuffer_init(dev, to_intel_framebuffer(fb),
				   &mode_cmd, obj)) {
		DRM_DEBUG_KMS("intel fb init failed\n");
		goto out_unref_obj;
	}
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG_KMS("initial plane fb obj %p\n", obj);
	return true;

out_unref_obj:
	drm_gem_object_unreference(&obj->base);
	mutex_unlock(&dev->struct_mutex);
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
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *c;
	struct intel_crtc *i;
	struct drm_i915_gem_object *obj;
	struct drm_plane *primary = intel_crtc->base.primary;
	struct drm_plane_state *plane_state = primary->state;
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
		i = to_intel_crtc(c);

		if (c == &intel_crtc->base)
			continue;

		if (!i->active)
			continue;

		fb = c->primary->fb;
		if (!fb)
			continue;

		obj = intel_fb_obj(fb);
		if (i915_gem_obj_ggtt_offset(obj) == plane_config->base) {
			drm_framebuffer_reference(fb);
			goto valid_fb;
		}
	}

	return;

valid_fb:
	plane_state->src_x = 0;
	plane_state->src_y = 0;
	plane_state->src_w = fb->width << 16;
	plane_state->src_h = fb->height << 16;

	plane_state->crtc_x = 0;
	plane_state->crtc_y = 0;
	plane_state->crtc_w = fb->width;
	plane_state->crtc_h = fb->height;

	obj = intel_fb_obj(fb);
	if (obj->tiling_mode != I915_TILING_NONE)
		dev_priv->preserve_bios_swizzle = true;

	drm_framebuffer_reference(fb);
	primary->fb = primary->state->fb = fb;
	primary->crtc = primary->state->crtc = &intel_crtc->base;
	intel_crtc->base.state->plane_mask |= (1 << drm_plane_index(primary));
	obj->frontbuffer_bits |= to_intel_plane(primary)->frontbuffer_bit;
}

static void i9xx_update_primary_plane(struct drm_crtc *crtc,
				      struct drm_framebuffer *fb,
				      int x, int y)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_plane *primary = crtc->primary;
	bool visible = to_intel_plane_state(primary->state)->visible;
	struct drm_i915_gem_object *obj;
	int plane = intel_crtc->plane;
	unsigned long linear_offset;
	u32 dspcntr;
	i915_reg_t reg = DSPCNTR(plane);
	int pixel_size;

	if (!visible || !fb) {
		I915_WRITE(reg, 0);
		if (INTEL_INFO(dev)->gen >= 4)
			I915_WRITE(DSPSURF(plane), 0);
		else
			I915_WRITE(DSPADDR(plane), 0);
		POSTING_READ(reg);
		return;
	}

	obj = intel_fb_obj(fb);
	if (WARN_ON(obj == NULL))
		return;

	pixel_size = drm_format_plane_cpp(fb->pixel_format, 0);

	dspcntr = DISPPLANE_GAMMA_ENABLE;

	dspcntr |= DISPLAY_PLANE_ENABLE;

	if (INTEL_INFO(dev)->gen < 4) {
		if (intel_crtc->pipe == PIPE_B)
			dspcntr |= DISPPLANE_SEL_PIPE_B;

		/* pipesrc and dspsize control the size that is scaled from,
		 * which should always be the user's requested size.
		 */
		I915_WRITE(DSPSIZE(plane),
			   ((intel_crtc->config->pipe_src_h - 1) << 16) |
			   (intel_crtc->config->pipe_src_w - 1));
		I915_WRITE(DSPPOS(plane), 0);
	} else if (IS_CHERRYVIEW(dev) && plane == PLANE_B) {
		I915_WRITE(PRIMSIZE(plane),
			   ((intel_crtc->config->pipe_src_h - 1) << 16) |
			   (intel_crtc->config->pipe_src_w - 1));
		I915_WRITE(PRIMPOS(plane), 0);
		I915_WRITE(PRIMCNSTALPHA(plane), 0);
	}

	switch (fb->pixel_format) {
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

	if (INTEL_INFO(dev)->gen >= 4 &&
	    obj->tiling_mode != I915_TILING_NONE)
		dspcntr |= DISPPLANE_TILED;

	if (IS_G4X(dev))
		dspcntr |= DISPPLANE_TRICKLE_FEED_DISABLE;

	linear_offset = y * fb->pitches[0] + x * pixel_size;

	if (INTEL_INFO(dev)->gen >= 4) {
		intel_crtc->dspaddr_offset =
			intel_gen4_compute_page_offset(dev_priv,
						       &x, &y, obj->tiling_mode,
						       pixel_size,
						       fb->pitches[0]);
		linear_offset -= intel_crtc->dspaddr_offset;
	} else {
		intel_crtc->dspaddr_offset = linear_offset;
	}

	if (crtc->primary->state->rotation == BIT(DRM_ROTATE_180)) {
		dspcntr |= DISPPLANE_ROTATE_180;

		x += (intel_crtc->config->pipe_src_w - 1);
		y += (intel_crtc->config->pipe_src_h - 1);

		/* Finding the last pixel of the last line of the display
		data and adding to linear_offset*/
		linear_offset +=
			(intel_crtc->config->pipe_src_h - 1) * fb->pitches[0] +
			(intel_crtc->config->pipe_src_w - 1) * pixel_size;
	}

	intel_crtc->adjusted_x = x;
	intel_crtc->adjusted_y = y;

	I915_WRITE(reg, dspcntr);

	I915_WRITE(DSPSTRIDE(plane), fb->pitches[0]);
	if (INTEL_INFO(dev)->gen >= 4) {
		I915_WRITE(DSPSURF(plane),
			   i915_gem_obj_ggtt_offset(obj) + intel_crtc->dspaddr_offset);
		I915_WRITE(DSPTILEOFF(plane), (y << 16) | x);
		I915_WRITE(DSPLINOFF(plane), linear_offset);
	} else
		I915_WRITE(DSPADDR(plane), i915_gem_obj_ggtt_offset(obj) + linear_offset);
	POSTING_READ(reg);
}

static void ironlake_update_primary_plane(struct drm_crtc *crtc,
					  struct drm_framebuffer *fb,
					  int x, int y)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_plane *primary = crtc->primary;
	bool visible = to_intel_plane_state(primary->state)->visible;
	struct drm_i915_gem_object *obj;
	int plane = intel_crtc->plane;
	unsigned long linear_offset;
	u32 dspcntr;
	i915_reg_t reg = DSPCNTR(plane);
	int pixel_size;

	if (!visible || !fb) {
		I915_WRITE(reg, 0);
		I915_WRITE(DSPSURF(plane), 0);
		POSTING_READ(reg);
		return;
	}

	obj = intel_fb_obj(fb);
	if (WARN_ON(obj == NULL))
		return;

	pixel_size = drm_format_plane_cpp(fb->pixel_format, 0);

	dspcntr = DISPPLANE_GAMMA_ENABLE;

	dspcntr |= DISPLAY_PLANE_ENABLE;

	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		dspcntr |= DISPPLANE_PIPE_CSC_ENABLE;

	switch (fb->pixel_format) {
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

	if (obj->tiling_mode != I915_TILING_NONE)
		dspcntr |= DISPPLANE_TILED;

	if (!IS_HASWELL(dev) && !IS_BROADWELL(dev))
		dspcntr |= DISPPLANE_TRICKLE_FEED_DISABLE;

	linear_offset = y * fb->pitches[0] + x * pixel_size;
	intel_crtc->dspaddr_offset =
		intel_gen4_compute_page_offset(dev_priv,
					       &x, &y, obj->tiling_mode,
					       pixel_size,
					       fb->pitches[0]);
	linear_offset -= intel_crtc->dspaddr_offset;
	if (crtc->primary->state->rotation == BIT(DRM_ROTATE_180)) {
		dspcntr |= DISPPLANE_ROTATE_180;

		if (!IS_HASWELL(dev) && !IS_BROADWELL(dev)) {
			x += (intel_crtc->config->pipe_src_w - 1);
			y += (intel_crtc->config->pipe_src_h - 1);

			/* Finding the last pixel of the last line of the display
			data and adding to linear_offset*/
			linear_offset +=
				(intel_crtc->config->pipe_src_h - 1) * fb->pitches[0] +
				(intel_crtc->config->pipe_src_w - 1) * pixel_size;
		}
	}

	intel_crtc->adjusted_x = x;
	intel_crtc->adjusted_y = y;

	I915_WRITE(reg, dspcntr);

	I915_WRITE(DSPSTRIDE(plane), fb->pitches[0]);
	I915_WRITE(DSPSURF(plane),
		   i915_gem_obj_ggtt_offset(obj) + intel_crtc->dspaddr_offset);
	if (IS_HASWELL(dev) || IS_BROADWELL(dev)) {
		I915_WRITE(DSPOFFSET(plane), (y << 16) | x);
	} else {
		I915_WRITE(DSPTILEOFF(plane), (y << 16) | x);
		I915_WRITE(DSPLINOFF(plane), linear_offset);
	}
	POSTING_READ(reg);
}

u32 intel_fb_stride_alignment(struct drm_device *dev, uint64_t fb_modifier,
			      uint32_t pixel_format)
{
	u32 bits_per_pixel = drm_format_plane_cpp(pixel_format, 0) * 8;

	/*
	 * The stride is either expressed as a multiple of 64 bytes
	 * chunks for linear buffers or in number of tiles for tiled
	 * buffers.
	 */
	switch (fb_modifier) {
	case DRM_FORMAT_MOD_NONE:
		return 64;
	case I915_FORMAT_MOD_X_TILED:
		if (INTEL_INFO(dev)->gen == 2)
			return 128;
		return 512;
	case I915_FORMAT_MOD_Y_TILED:
		/* No need to check for old gens and Y tiling since this is
		 * about the display engine and those will be blocked before
		 * we get here.
		 */
		return 128;
	case I915_FORMAT_MOD_Yf_TILED:
		if (bits_per_pixel == 8)
			return 64;
		else
			return 128;
	default:
		MISSING_CASE(fb_modifier);
		return 64;
	}
}

u32 intel_plane_obj_offset(struct intel_plane *intel_plane,
			   struct drm_i915_gem_object *obj,
			   unsigned int plane)
{
	struct i915_ggtt_view view;
	struct i915_vma *vma;
	u64 offset;

	intel_fill_fb_ggtt_view(&view, intel_plane->base.fb,
				intel_plane->base.state);

	vma = i915_gem_obj_to_ggtt_view(obj, &view);
	if (WARN(!vma, "ggtt vma for display object not found! (view=%u)\n",
		view.type))
		return -1;

	offset = vma->node.start;

	if (plane == 1) {
		offset += vma->ggtt_view.params.rotation_info.uv_start_page *
			  PAGE_SIZE;
	}

	WARN_ON(upper_32_bits(offset));

	return lower_32_bits(offset);
}

static void skl_detach_scaler(struct intel_crtc *intel_crtc, int id)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

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
	case BIT(DRM_ROTATE_0):
		break;
	/*
	 * DRM_ROTATE_ is counter clockwise to stay compatible with Xrandr
	 * while i915 HW rotation is clockwise, thats why this swapping.
	 */
	case BIT(DRM_ROTATE_90):
		return PLANE_CTL_ROTATE_270;
	case BIT(DRM_ROTATE_180):
		return PLANE_CTL_ROTATE_180;
	case BIT(DRM_ROTATE_270):
		return PLANE_CTL_ROTATE_90;
	default:
		MISSING_CASE(rotation);
	}

	return 0;
}

static void skylake_update_primary_plane(struct drm_crtc *crtc,
					 struct drm_framebuffer *fb,
					 int x, int y)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_plane *plane = crtc->primary;
	bool visible = to_intel_plane_state(plane->state)->visible;
	struct drm_i915_gem_object *obj;
	int pipe = intel_crtc->pipe;
	u32 plane_ctl, stride_div, stride;
	u32 tile_height, plane_offset, plane_size;
	unsigned int rotation;
	int x_offset, y_offset;
	u32 surf_addr;
	struct intel_crtc_state *crtc_state = intel_crtc->config;
	struct intel_plane_state *plane_state;
	int src_x = 0, src_y = 0, src_w = 0, src_h = 0;
	int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;
	int scaler_id = -1;

	plane_state = to_intel_plane_state(plane->state);

	if (!visible || !fb) {
		I915_WRITE(PLANE_CTL(pipe, 0), 0);
		I915_WRITE(PLANE_SURF(pipe, 0), 0);
		POSTING_READ(PLANE_CTL(pipe, 0));
		return;
	}

	plane_ctl = PLANE_CTL_ENABLE |
		    PLANE_CTL_PIPE_GAMMA_ENABLE |
		    PLANE_CTL_PIPE_CSC_ENABLE;

	plane_ctl |= skl_plane_ctl_format(fb->pixel_format);
	plane_ctl |= skl_plane_ctl_tiling(fb->modifier[0]);
	plane_ctl |= PLANE_CTL_PLANE_GAMMA_DISABLE;

	rotation = plane->state->rotation;
	plane_ctl |= skl_plane_ctl_rotation(rotation);

	obj = intel_fb_obj(fb);
	stride_div = intel_fb_stride_alignment(dev, fb->modifier[0],
					       fb->pixel_format);
	surf_addr = intel_plane_obj_offset(to_intel_plane(plane), obj, 0);

	WARN_ON(drm_rect_width(&plane_state->src) == 0);

	scaler_id = plane_state->scaler_id;
	src_x = plane_state->src.x1 >> 16;
	src_y = plane_state->src.y1 >> 16;
	src_w = drm_rect_width(&plane_state->src) >> 16;
	src_h = drm_rect_height(&plane_state->src) >> 16;
	dst_x = plane_state->dst.x1;
	dst_y = plane_state->dst.y1;
	dst_w = drm_rect_width(&plane_state->dst);
	dst_h = drm_rect_height(&plane_state->dst);

	WARN_ON(x != src_x || y != src_y);

	if (intel_rotation_90_or_270(rotation)) {
		/* stride = Surface height in tiles */
		tile_height = intel_tile_height(dev, fb->pixel_format,
						fb->modifier[0], 0);
		stride = DIV_ROUND_UP(fb->height, tile_height);
		x_offset = stride * tile_height - y - src_h;
		y_offset = x;
		plane_size = (src_w - 1) << 16 | (src_h - 1);
	} else {
		stride = fb->pitches[0] / stride_div;
		x_offset = x;
		y_offset = y;
		plane_size = (src_h - 1) << 16 | (src_w - 1);
	}
	plane_offset = y_offset << 16 | x_offset;

	intel_crtc->adjusted_x = x_offset;
	intel_crtc->adjusted_y = y_offset;

	I915_WRITE(PLANE_CTL(pipe, 0), plane_ctl);
	I915_WRITE(PLANE_OFFSET(pipe, 0), plane_offset);
	I915_WRITE(PLANE_SIZE(pipe, 0), plane_size);
	I915_WRITE(PLANE_STRIDE(pipe, 0), stride);

	if (scaler_id >= 0) {
		uint32_t ps_ctrl = 0;

		WARN_ON(!dst_w || !dst_h);
		ps_ctrl = PS_SCALER_EN | PS_PLANE_SEL(0) |
			crtc_state->scaler_state.scalers[scaler_id].mode;
		I915_WRITE(SKL_PS_CTRL(pipe, scaler_id), ps_ctrl);
		I915_WRITE(SKL_PS_PWR_GATE(pipe, scaler_id), 0);
		I915_WRITE(SKL_PS_WIN_POS(pipe, scaler_id), (dst_x << 16) | dst_y);
		I915_WRITE(SKL_PS_WIN_SZ(pipe, scaler_id), (dst_w << 16) | dst_h);
		I915_WRITE(PLANE_POS(pipe, 0), 0);
	} else {
		I915_WRITE(PLANE_POS(pipe, 0), (dst_y << 16) | dst_x);
	}

	I915_WRITE(PLANE_SURF(pipe, 0), surf_addr);

	POSTING_READ(PLANE_SURF(pipe, 0));
}

/* Assume fb object is pinned & idle & fenced and just update base pointers */
static int
intel_pipe_set_base_atomic(struct drm_crtc *crtc, struct drm_framebuffer *fb,
			   int x, int y, enum mode_set_atomic state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->fbc.deactivate)
		dev_priv->fbc.deactivate(dev_priv);

	dev_priv->display.update_primary_plane(crtc, fb, x, y);

	return 0;
}

static void intel_complete_page_flips(struct drm_device *dev)
{
	struct drm_crtc *crtc;

	for_each_crtc(dev, crtc) {
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
		enum plane plane = intel_crtc->plane;

		intel_prepare_page_flip(dev, plane);
		intel_finish_page_flip_plane(dev, plane);
	}
}

static void intel_update_primary_planes(struct drm_device *dev)
{
	struct drm_crtc *crtc;

	for_each_crtc(dev, crtc) {
		struct intel_plane *plane = to_intel_plane(crtc->primary);
		struct intel_plane_state *plane_state;

		drm_modeset_lock_crtc(crtc, &plane->base);
		plane_state = to_intel_plane_state(plane->base.state);

		if (crtc->state->active && plane_state->base.fb)
			plane->commit_plane(&plane->base, plane_state);

		drm_modeset_unlock_crtc(crtc);
	}
}

void intel_prepare_reset(struct drm_device *dev)
{
	/* no reset support for gen2 */
	if (IS_GEN2(dev))
		return;

	/* reset doesn't touch the display */
	if (INTEL_INFO(dev)->gen >= 5 || IS_G4X(dev))
		return;

	drm_modeset_lock_all(dev);
	/*
	 * Disabling the crtcs gracefully seems nicer. Also the
	 * g33 docs say we should at least disable all the planes.
	 */
	intel_display_suspend(dev);
}

void intel_finish_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	/*
	 * Flips in the rings will be nuked by the reset,
	 * so complete all pending flips so that user space
	 * will get its events and not get stuck.
	 */
	intel_complete_page_flips(dev);

	/* no reset support for gen2 */
	if (IS_GEN2(dev))
		return;

	/* reset doesn't touch the display */
	if (INTEL_INFO(dev)->gen >= 5 || IS_G4X(dev)) {
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
		return;
	}

	/*
	 * The display has been reset as well,
	 * so need a full re-initialization.
	 */
	intel_runtime_pm_disable_interrupts(dev_priv);
	intel_runtime_pm_enable_interrupts(dev_priv);

	intel_modeset_init_hw(dev);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display.hpd_irq_setup)
		dev_priv->display.hpd_irq_setup(dev);
	spin_unlock_irq(&dev_priv->irq_lock);

	intel_display_resume(dev);

	intel_hpd_init(dev_priv);

	drm_modeset_unlock_all(dev);
}

static bool intel_crtc_has_pending_flip(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	bool pending;

	if (i915_reset_in_progress(&dev_priv->gpu_error) ||
	    intel_crtc->reset_counter != atomic_read(&dev_priv->gpu_error.reset_counter))
		return false;

	spin_lock_irq(&dev->event_lock);
	pending = to_intel_crtc(crtc)->unpin_work != NULL;
	spin_unlock_irq(&dev->event_lock);

	return pending;
}

static void intel_update_pipe_config(struct intel_crtc *crtc,
				     struct intel_crtc_state *old_crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc_state *pipe_config =
		to_intel_crtc_state(crtc->base.state);

	/* drm_atomic_helper_update_legacy_modeset_state might not be called. */
	crtc->base.mode = crtc->base.state->mode;

	DRM_DEBUG_KMS("Updating pipe size %ix%i -> %ix%i\n",
		      old_crtc_state->pipe_src_w, old_crtc_state->pipe_src_h,
		      pipe_config->pipe_src_w, pipe_config->pipe_src_h);

	if (HAS_DDI(dev))
		intel_set_pipe_csc(&crtc->base);

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
	if (INTEL_INFO(dev)->gen >= 9) {
		skl_detach_scalers(crtc);

		if (pipe_config->pch_pfit.enabled)
			skylake_pfit_enable(crtc);
	} else if (HAS_PCH_SPLIT(dev)) {
		if (pipe_config->pch_pfit.enabled)
			ironlake_pfit_enable(crtc);
		else if (old_crtc_state->pch_pfit.enabled)
			ironlake_pfit_disable(crtc, true);
	}
}

static void intel_fdi_normal_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	i915_reg_t reg;
	u32 temp;

	/* enable normal train */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	if (IS_IVYBRIDGE(dev)) {
		temp &= ~FDI_LINK_TRAIN_NONE_IVB;
		temp |= FDI_LINK_TRAIN_NONE_IVB | FDI_TX_ENHANCE_FRAME_ENABLE;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_NONE | FDI_TX_ENHANCE_FRAME_ENABLE;
	}
	I915_WRITE(reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	if (HAS_PCH_CPT(dev)) {
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
	if (IS_IVYBRIDGE(dev))
		I915_WRITE(reg, I915_READ(reg) | FDI_FS_ERRC_ENABLE |
			   FDI_FE_ERRC_ENABLE);
}

/* The FDI link training functions for ILK/Ibexpeak. */
static void ironlake_fdi_link_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	if (HAS_PCH_CPT(dev)) {
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
	if (IS_GEN6(dev)) {
		temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
		/* SNB-B */
		temp |= FDI_LINK_TRAIN_400MV_0DB_SNB_B;
	}
	I915_WRITE(reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	if (HAS_PCH_CPT(dev)) {
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	if (HAS_PCH_IBX(dev))
		I915_WRITE(FDI_RX_CHICKEN(pipe), FDI_RX_PHASE_SYNC_POINTER_OVR);

	/* still set train pattern 1 */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_1;
	I915_WRITE(reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	if (HAS_PCH_CPT(dev)) {
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

bool intel_has_pending_fb_unpin(struct drm_device *dev)
{
	struct intel_crtc *crtc;

	/* Note that we don't need to be called with mode_config.lock here
	 * as our list of CRTC objects is static for the lifetime of the
	 * device and so cannot disappear as we iterate. Similarly, we can
	 * happily treat the predicates as racy, atomic checks as userspace
	 * cannot claim and pin a new fb without at least acquring the
	 * struct_mutex and so serialising with us.
	 */
	for_each_intel_crtc(dev, crtc) {
		if (atomic_read(&crtc->unpin_work_count) == 0)
			continue;

		if (crtc->unpin_work)
			intel_wait_for_vblank(dev, crtc->pipe);

		return true;
	}

	return false;
}

static void page_flip_completed(struct intel_crtc *intel_crtc)
{
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);
	struct intel_unpin_work *work = intel_crtc->unpin_work;

	/* ensure that the unpin work is consistent wrt ->pending. */
	smp_rmb();
	intel_crtc->unpin_work = NULL;

	if (work->event)
		drm_send_vblank_event(intel_crtc->base.dev,
				      intel_crtc->pipe,
				      work->event);

	drm_crtc_vblank_put(&intel_crtc->base);

	wake_up_all(&dev_priv->pending_flip_queue);
	queue_work(dev_priv->wq, &work->work);

	trace_i915_flip_complete(intel_crtc->plane,
				 work->pending_flip_obj);
}

static int intel_crtc_wait_for_pending_flips(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
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

		spin_lock_irq(&dev->event_lock);
		if (intel_crtc->unpin_work) {
			WARN_ONCE(1, "Removing stuck page flip\n");
			page_flip_completed(intel_crtc);
		}
		spin_unlock_irq(&dev->event_lock);
	}

	return 0;
}

/* Program iCLKIP clock to the desired frequency */
static void lpt_program_iclkip(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int clock = to_intel_crtc(crtc)->config->base.adjusted_mode.crtc_clock;
	u32 divsel, phaseinc, auxdiv, phasedir = 0;
	u32 temp;

	mutex_lock(&dev_priv->sb_lock);

	/* It is necessary to ungate the pixclk gate prior to programming
	 * the divisors, and gate it back when it is done.
	 */
	I915_WRITE(PIXCLK_GATE, PIXCLK_GATE_GATE);

	/* Disable SSCCTL */
	intel_sbi_write(dev_priv, SBI_SSCCTL6,
			intel_sbi_read(dev_priv, SBI_SSCCTL6, SBI_ICLK) |
				SBI_SSCCTL_DISABLE,
			SBI_ICLK);

	/* 20MHz is a corner case which is out of range for the 7-bit divisor */
	if (clock == 20000) {
		auxdiv = 1;
		divsel = 0x41;
		phaseinc = 0x20;
	} else {
		/* The iCLK virtual clock root frequency is in MHz,
		 * but the adjusted_mode->crtc_clock in in KHz. To get the
		 * divisors, it is necessary to divide one by another, so we
		 * convert the virtual clock precision to KHz here for higher
		 * precision.
		 */
		u32 iclk_virtual_root_freq = 172800 * 1000;
		u32 iclk_pi_range = 64;
		u32 desired_divisor, msb_divisor_value, pi_value;

		desired_divisor = (iclk_virtual_root_freq / clock);
		msb_divisor_value = desired_divisor / iclk_pi_range;
		pi_value = desired_divisor % iclk_pi_range;

		auxdiv = 0;
		divsel = msb_divisor_value - 2;
		phaseinc = pi_value;
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

	/* Wait for initialization time */
	udelay(24);

	I915_WRITE(PIXCLK_GATE, PIXCLK_GATE_UNGATE);

	mutex_unlock(&dev_priv->sb_lock);
}

static void ironlake_pch_transcoder_set_timings(struct intel_crtc *crtc,
						enum pipe pch_transcoder)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
		if (encoder->type == INTEL_OUTPUT_DISPLAYPORT ||
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
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 temp;

	assert_pch_transcoder_disabled(dev_priv, pipe);

	if (IS_IVYBRIDGE(dev))
		ivybridge_update_fdi_bc_bifurcation(intel_crtc);

	/* Write the TU size bits before fdi link training, so that error
	 * detection works. */
	I915_WRITE(FDI_RX_TUSIZE1(pipe),
		   I915_READ(PIPE_DATA_M1(pipe)) & TU_SIZE_MASK);

	/*
	 * Sometimes spurious CPU pipe underruns happen during FDI
	 * training, at least with VGA+HDMI cloning. Suppress them.
	 */
	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);

	/* For PCH output, training FDI link */
	dev_priv->display.fdi_link_train(crtc);

	/* We need to program the right clock selection before writing the pixel
	 * mutliplier into the DPLL. */
	if (HAS_PCH_CPT(dev)) {
		u32 sel;

		temp = I915_READ(PCH_DPLL_SEL);
		temp |= TRANS_DPLL_ENABLE(pipe);
		sel = TRANS_DPLLB_SEL(pipe);
		if (intel_crtc->config->shared_dpll == DPLL_ID_PCH_PLL_B)
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

	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	/* For PCH DP, enable TRANS_DP_CTL */
	if (HAS_PCH_CPT(dev) && intel_crtc->config->has_dp_encoder) {
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
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;

	assert_pch_transcoder_disabled(dev_priv, TRANSCODER_A);

	lpt_program_iclkip(crtc);

	/* Set transcoder timing. */
	ironlake_pch_transcoder_set_timings(intel_crtc, PIPE_A);

	lpt_enable_pch_transcoder(dev_priv, cpu_transcoder);
}

struct intel_shared_dpll *intel_get_shared_dpll(struct intel_crtc *crtc,
						struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = crtc->base.dev->dev_private;
	struct intel_shared_dpll *pll;
	struct intel_shared_dpll_config *shared_dpll;
	enum intel_dpll_id i;
	int max = dev_priv->num_shared_dpll;

	shared_dpll = intel_atomic_get_shared_dpll_state(crtc_state->base.state);

	if (HAS_PCH_IBX(dev_priv->dev)) {
		/* Ironlake PCH has a fixed PLL->PCH pipe mapping. */
		i = (enum intel_dpll_id) crtc->pipe;
		pll = &dev_priv->shared_dplls[i];

		DRM_DEBUG_KMS("CRTC:%d using pre-allocated %s\n",
			      crtc->base.base.id, pll->name);

		WARN_ON(shared_dpll[i].crtc_mask);

		goto found;
	}

	if (IS_BROXTON(dev_priv->dev)) {
		/* PLL is attached to port in bxt */
		struct intel_encoder *encoder;
		struct intel_digital_port *intel_dig_port;

		encoder = intel_ddi_get_crtc_new_encoder(crtc_state);
		if (WARN_ON(!encoder))
			return NULL;

		intel_dig_port = enc_to_dig_port(&encoder->base);
		/* 1:1 mapping between ports and PLLs */
		i = (enum intel_dpll_id)intel_dig_port->port;
		pll = &dev_priv->shared_dplls[i];
		DRM_DEBUG_KMS("CRTC:%d using pre-allocated %s\n",
			crtc->base.base.id, pll->name);
		WARN_ON(shared_dpll[i].crtc_mask);

		goto found;
	} else if (INTEL_INFO(dev_priv)->gen < 9 && HAS_DDI(dev_priv))
		/* Do not consider SPLL */
		max = 2;

	for (i = 0; i < max; i++) {
		pll = &dev_priv->shared_dplls[i];

		/* Only want to check enabled timings first */
		if (shared_dpll[i].crtc_mask == 0)
			continue;

		if (memcmp(&crtc_state->dpll_hw_state,
			   &shared_dpll[i].hw_state,
			   sizeof(crtc_state->dpll_hw_state)) == 0) {
			DRM_DEBUG_KMS("CRTC:%d sharing existing %s (crtc mask 0x%08x, ative %d)\n",
				      crtc->base.base.id, pll->name,
				      shared_dpll[i].crtc_mask,
				      pll->active);
			goto found;
		}
	}

	/* Ok no matching timings, maybe there's a free one? */
	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		pll = &dev_priv->shared_dplls[i];
		if (shared_dpll[i].crtc_mask == 0) {
			DRM_DEBUG_KMS("CRTC:%d allocated %s\n",
				      crtc->base.base.id, pll->name);
			goto found;
		}
	}

	return NULL;

found:
	if (shared_dpll[i].crtc_mask == 0)
		shared_dpll[i].hw_state =
			crtc_state->dpll_hw_state;

	crtc_state->shared_dpll = i;
	DRM_DEBUG_DRIVER("using %s for pipe %c\n", pll->name,
			 pipe_name(crtc->pipe));

	shared_dpll[i].crtc_mask |= 1 << crtc->pipe;

	return pll;
}

static void intel_shared_dpll_commit(struct drm_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->dev);
	struct intel_shared_dpll_config *shared_dpll;
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	if (!to_intel_atomic_state(state)->dpll_set)
		return;

	shared_dpll = to_intel_atomic_state(state)->shared_dpll;
	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		pll = &dev_priv->shared_dplls[i];
		pll->config = shared_dpll[i];
	}
}

static void cpt_verify_modeset(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
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

	need_scaling = intel_rotation_90_or_270(rotation) ?
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
	struct intel_crtc *intel_crtc = to_intel_crtc(state->base.crtc);
	const struct drm_display_mode *adjusted_mode = &state->base.adjusted_mode;

	DRM_DEBUG_KMS("Updating scaler for [CRTC:%i] scaler_user index %u.%u\n",
		      intel_crtc->base.base.id, intel_crtc->pipe, SKL_CRTC_INDEX);

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

	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);
	struct intel_plane *intel_plane =
		to_intel_plane(plane_state->base.plane);
	struct drm_framebuffer *fb = plane_state->base.fb;
	int ret;

	bool force_detach = !fb || !plane_state->visible;

	DRM_DEBUG_KMS("Updating scaler for [PLANE:%d] scaler_user index %u.%u\n",
		      intel_plane->base.base.id, intel_crtc->pipe,
		      drm_plane_index(&intel_plane->base));

	ret = skl_update_scaler(crtc_state, force_detach,
				drm_plane_index(&intel_plane->base),
				&plane_state->scaler_id,
				plane_state->base.rotation,
				drm_rect_width(&plane_state->src) >> 16,
				drm_rect_height(&plane_state->src) >> 16,
				drm_rect_width(&plane_state->dst),
				drm_rect_height(&plane_state->dst));

	if (ret || plane_state->scaler_id < 0)
		return ret;

	/* check colorkey */
	if (plane_state->ckey.flags != I915_SET_COLORKEY_NONE) {
		DRM_DEBUG_KMS("[PLANE:%d] scaling with color key not allowed",
			      intel_plane->base.base.id);
		return -EINVAL;
	}

	/* Check src format */
	switch (fb->pixel_format) {
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
		DRM_DEBUG_KMS("[PLANE:%d] FB:%d unsupported scaling format 0x%x\n",
			intel_plane->base.base.id, fb->base.id, fb->pixel_format);
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = crtc->pipe;

	if (crtc->config->pch_pfit.enabled) {
		/* Force use of hard-coded filter coefficients
		 * as some pre-programmed values are broken,
		 * e.g. x201.
		 */
		if (IS_IVYBRIDGE(dev) || IS_HASWELL(dev))
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
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!crtc->config->ips_enabled)
		return;

	/* We can only enable IPS after we enable a plane and wait for a vblank */
	intel_wait_for_vblank(dev, crtc->pipe);

	assert_plane_enabled(dev_priv, crtc->plane);
	if (IS_BROADWELL(dev)) {
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
		if (wait_for(I915_READ_NOTRACE(IPS_CTL) & IPS_ENABLE, 50))
			DRM_ERROR("Timed out waiting for IPS enable\n");
	}
}

void hsw_disable_ips(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!crtc->config->ips_enabled)
		return;

	assert_plane_enabled(dev_priv, crtc->plane);
	if (IS_BROADWELL(dev)) {
		mutex_lock(&dev_priv->rps.hw_lock);
		WARN_ON(sandybridge_pcode_write(dev_priv, DISPLAY_IPS_CONTROL, 0));
		mutex_unlock(&dev_priv->rps.hw_lock);
		/* wait for pcode to finish disabling IPS, which may take up to 42ms */
		if (wait_for((I915_READ(IPS_CTL) & IPS_ENABLE) == 0, 42))
			DRM_ERROR("Timed out waiting for IPS disable\n");
	} else {
		I915_WRITE(IPS_CTL, 0);
		POSTING_READ(IPS_CTL);
	}

	/* We need to wait for a vblank before we can disable the plane. */
	intel_wait_for_vblank(dev, crtc->pipe);
}

/** Loads the palette/gamma unit for the CRTC with the prepared values */
static void intel_crtc_load_lut(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum pipe pipe = intel_crtc->pipe;
	int i;
	bool reenable_ips = false;

	/* The clocks have to be on to load the palette. */
	if (!crtc->state->active)
		return;

	if (HAS_GMCH_DISPLAY(dev_priv->dev)) {
		if (intel_crtc->config->has_dsi_encoder)
			assert_dsi_pll_enabled(dev_priv);
		else
			assert_pll_enabled(dev_priv, pipe);
	}

	/* Workaround : Do not read or write the pipe palette/gamma data while
	 * GAMMA_MODE is configured for split gamma and IPS_CTL has IPS enabled.
	 */
	if (IS_HASWELL(dev) && intel_crtc->config->ips_enabled &&
	    ((I915_READ(GAMMA_MODE(pipe)) & GAMMA_MODE_MODE_MASK) ==
	     GAMMA_MODE_MODE_SPLIT)) {
		hsw_disable_ips(intel_crtc);
		reenable_ips = true;
	}

	for (i = 0; i < 256; i++) {
		i915_reg_t palreg;

		if (HAS_GMCH_DISPLAY(dev))
			palreg = PALETTE(pipe, i);
		else
			palreg = LGC_PALETTE(pipe, i);

		I915_WRITE(palreg,
			   (intel_crtc->lut_r[i] << 16) |
			   (intel_crtc->lut_g[i] << 8) |
			   intel_crtc->lut_b[i]);
	}

	if (reenable_ips)
		hsw_enable_ips(intel_crtc);
}

static void intel_crtc_dpms_overlay_disable(struct intel_crtc *intel_crtc)
{
	if (intel_crtc->overlay) {
		struct drm_device *dev = intel_crtc->base.dev;
		struct drm_i915_private *dev_priv = dev->dev_private;

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
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;

	/*
	 * BDW signals flip done immediately if the plane
	 * is disabled, even if the plane enable is already
	 * armed to occur at the next vblank :(
	 */
	if (IS_BROADWELL(dev))
		intel_wait_for_vblank(dev, pipe);

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
	if (IS_GEN2(dev))
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	/* Underruns don't always raise interrupts, so check manually. */
	intel_check_cpu_fifo_underruns(dev_priv);
	intel_check_pch_fifo_underruns(dev_priv);
}

/**
 * intel_pre_disable_primary - Perform operations before disabling primary plane
 * @crtc: the CRTC whose primary plane is to be disabled
 *
 * Performs potentially sleeping operations that must be done before the
 * primary plane is disabled, such as updating FBC and IPS.  Note that this may
 * be called due to an explicit primary plane update, or due to an implicit
 * disable that is caused when a sprite plane completely hides the primary
 * plane.
 */
static void
intel_pre_disable_primary(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;

	/*
	 * Gen2 reports pipe underruns whenever all planes are disabled.
	 * So diasble underrun reporting before all the planes get disabled.
	 * FIXME: Need to fix the logic to work when we turn off all planes
	 * but leave the pipe running.
	 */
	if (IS_GEN2(dev))
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);

	/*
	 * Vblank time updates from the shadow to live plane control register
	 * are blocked if the memory self-refresh mode is active at that
	 * moment. So to make sure the plane gets truly disabled, disable
	 * first the self-refresh mode. The self-refresh enable bit in turn
	 * will be checked/applied by the HW only at the next frame start
	 * event which is after the vblank start event, so we need to have a
	 * wait-for-vblank between disabling the plane and the pipe.
	 */
	if (HAS_GMCH_DISPLAY(dev)) {
		intel_set_memory_cxsr(dev_priv, false);
		dev_priv->wm.vlv.cxsr = false;
		intel_wait_for_vblank(dev, pipe);
	}

	/*
	 * FIXME IPS should be fine as long as one plane is
	 * enabled, but in practice it seems to have problems
	 * when going from primary only to sprite only and vice
	 * versa.
	 */
	hsw_disable_ips(intel_crtc);
}

static void intel_post_plane_update(struct intel_crtc *crtc)
{
	struct intel_crtc_atomic_commit *atomic = &crtc->atomic;
	struct drm_device *dev = crtc->base.dev;

	if (atomic->wait_vblank)
		intel_wait_for_vblank(dev, crtc->pipe);

	intel_frontbuffer_flip(dev, atomic->fb_bits);

	if (atomic->disable_cxsr)
		crtc->wm.cxsr_allowed = true;

	if (crtc->atomic.update_wm_post)
		intel_update_watermarks(&crtc->base);

	if (atomic->update_fbc)
		intel_fbc_update(crtc);

	if (atomic->post_enable_primary)
		intel_post_enable_primary(&crtc->base);

	memset(atomic, 0, sizeof(*atomic));
}

static void intel_pre_plane_update(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc_atomic_commit *atomic = &crtc->atomic;

	if (atomic->disable_fbc)
		intel_fbc_deactivate(crtc);

	if (crtc->atomic.disable_ips)
		hsw_disable_ips(crtc);

	if (atomic->pre_disable_primary)
		intel_pre_disable_primary(&crtc->base);

	if (atomic->disable_cxsr) {
		crtc->wm.cxsr_allowed = false;
		intel_set_memory_cxsr(dev_priv, false);
	}
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
	intel_frontbuffer_flip(dev, INTEL_FRONTBUFFER_ALL_MASK(pipe));
}

static void ironlake_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *encoder;
	int pipe = intel_crtc->pipe;

	if (WARN_ON(intel_crtc->active))
		return;

	if (intel_crtc->config->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv, pipe, false);

	if (intel_crtc->config->has_pch_encoder)
		intel_prepare_shared_dpll(intel_crtc);

	if (intel_crtc->config->has_dp_encoder)
		intel_dp_set_m_n(intel_crtc, M1_N1);

	intel_set_pipe_timings(intel_crtc);

	if (intel_crtc->config->has_pch_encoder) {
		intel_cpu_transcoder_set_m_n(intel_crtc,
				     &intel_crtc->config->fdi_m_n, NULL);
	}

	ironlake_set_pipeconf(crtc);

	intel_crtc->active = true;

	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->pre_enable)
			encoder->pre_enable(encoder);

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
	intel_crtc_load_lut(crtc);

	intel_update_watermarks(crtc);
	intel_enable_pipe(intel_crtc);

	if (intel_crtc->config->has_pch_encoder)
		ironlake_pch_enable(crtc);

	assert_vblank_disabled(crtc);
	drm_crtc_vblank_on(crtc);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		encoder->enable(encoder);

	if (HAS_PCH_CPT(dev))
		cpt_verify_modeset(dev, intel_crtc->pipe);

	/* Must wait for vblank to avoid spurious PCH FIFO underruns */
	if (intel_crtc->config->has_pch_encoder)
		intel_wait_for_vblank(dev, pipe);
	intel_set_pch_fifo_underrun_reporting(dev_priv, pipe, true);

	intel_fbc_enable(intel_crtc);
}

/* IPS only exists on ULT machines and is tied to pipe A. */
static bool hsw_crtc_supports_ips(struct intel_crtc *crtc)
{
	return HAS_IPS(crtc->base.dev) && crtc->pipe == PIPE_A;
}

static void haswell_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *encoder;
	int pipe = intel_crtc->pipe, hsw_workaround_pipe;
	struct intel_crtc_state *pipe_config =
		to_intel_crtc_state(crtc->state);

	if (WARN_ON(intel_crtc->active))
		return;

	if (intel_crtc->config->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv, TRANSCODER_A,
						      false);

	if (intel_crtc_to_shared_dpll(intel_crtc))
		intel_enable_shared_dpll(intel_crtc);

	if (intel_crtc->config->has_dp_encoder)
		intel_dp_set_m_n(intel_crtc, M1_N1);

	intel_set_pipe_timings(intel_crtc);

	if (intel_crtc->config->cpu_transcoder != TRANSCODER_EDP) {
		I915_WRITE(PIPE_MULT(intel_crtc->config->cpu_transcoder),
			   intel_crtc->config->pixel_multiplier - 1);
	}

	if (intel_crtc->config->has_pch_encoder) {
		intel_cpu_transcoder_set_m_n(intel_crtc,
				     &intel_crtc->config->fdi_m_n, NULL);
	}

	haswell_set_pipeconf(crtc);

	intel_set_pipe_csc(crtc);

	intel_crtc->active = true;

	if (intel_crtc->config->has_pch_encoder)
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);
	else
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	for_each_encoder_on_crtc(dev, crtc, encoder) {
		if (encoder->pre_enable)
			encoder->pre_enable(encoder);
	}

	if (intel_crtc->config->has_pch_encoder)
		dev_priv->display.fdi_link_train(crtc);

	if (!intel_crtc->config->has_dsi_encoder)
		intel_ddi_enable_pipe_clock(intel_crtc);

	if (INTEL_INFO(dev)->gen >= 9)
		skylake_pfit_enable(intel_crtc);
	else
		ironlake_pfit_enable(intel_crtc);

	/*
	 * On ILK+ LUT must be loaded before the pipe is running but with
	 * clocks enabled
	 */
	intel_crtc_load_lut(crtc);

	intel_ddi_set_pipe_settings(crtc);
	if (!intel_crtc->config->has_dsi_encoder)
		intel_ddi_enable_transcoder_func(crtc);

	intel_update_watermarks(crtc);
	intel_enable_pipe(intel_crtc);

	if (intel_crtc->config->has_pch_encoder)
		lpt_pch_enable(crtc);

	if (intel_crtc->config->dp_encoder_is_mst)
		intel_ddi_set_vc_payload_alloc(crtc, true);

	assert_vblank_disabled(crtc);
	drm_crtc_vblank_on(crtc);

	for_each_encoder_on_crtc(dev, crtc, encoder) {
		encoder->enable(encoder);
		intel_opregion_notify_encoder(encoder, true);
	}

	if (intel_crtc->config->has_pch_encoder) {
		intel_wait_for_vblank(dev, pipe);
		intel_wait_for_vblank(dev, pipe);
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);
		intel_set_pch_fifo_underrun_reporting(dev_priv, TRANSCODER_A,
						      true);
	}

	/* If we change the relative order between pipe/planes enabling, we need
	 * to change the workaround. */
	hsw_workaround_pipe = pipe_config->hsw_workaround_pipe;
	if (IS_HASWELL(dev) && hsw_workaround_pipe != INVALID_PIPE) {
		intel_wait_for_vblank(dev, hsw_workaround_pipe);
		intel_wait_for_vblank(dev, hsw_workaround_pipe);
	}

	intel_fbc_enable(intel_crtc);
}

static void ironlake_pfit_disable(struct intel_crtc *crtc, bool force)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = crtc->pipe;

	/* To avoid upsetting the power well on haswell only disable the pfit if
	 * it's in use. The hw state code will make sure we get this right. */
	if (force || crtc->config->pch_pfit.enabled) {
		I915_WRITE(PF_CTL(pipe), 0);
		I915_WRITE(PF_WIN_POS(pipe), 0);
		I915_WRITE(PF_WIN_SZ(pipe), 0);
	}
}

static void ironlake_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *encoder;
	int pipe = intel_crtc->pipe;

	if (intel_crtc->config->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv, pipe, false);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		encoder->disable(encoder);

	drm_crtc_vblank_off(crtc);
	assert_vblank_disabled(crtc);

	/*
	 * Sometimes spurious CPU pipe underruns happen when the
	 * pipe is already disabled, but FDI RX/TX is still enabled.
	 * Happens at least with VGA+HDMI cloning. Suppress them.
	 */
	if (intel_crtc->config->has_pch_encoder)
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);

	intel_disable_pipe(intel_crtc);

	ironlake_pfit_disable(intel_crtc, false);

	if (intel_crtc->config->has_pch_encoder) {
		ironlake_fdi_disable(crtc);
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);
	}

	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->post_disable)
			encoder->post_disable(encoder);

	if (intel_crtc->config->has_pch_encoder) {
		ironlake_disable_pch_transcoder(dev_priv, pipe);

		if (HAS_PCH_CPT(dev)) {
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

	intel_set_pch_fifo_underrun_reporting(dev_priv, pipe, true);

	intel_fbc_disable_crtc(intel_crtc);
}

static void haswell_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *encoder;
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;

	if (intel_crtc->config->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv, TRANSCODER_A,
						      false);

	for_each_encoder_on_crtc(dev, crtc, encoder) {
		intel_opregion_notify_encoder(encoder, false);
		encoder->disable(encoder);
	}

	drm_crtc_vblank_off(crtc);
	assert_vblank_disabled(crtc);

	intel_disable_pipe(intel_crtc);

	if (intel_crtc->config->dp_encoder_is_mst)
		intel_ddi_set_vc_payload_alloc(crtc, false);

	if (!intel_crtc->config->has_dsi_encoder)
		intel_ddi_disable_transcoder_func(dev_priv, cpu_transcoder);

	if (INTEL_INFO(dev)->gen >= 9)
		skylake_scaler_disable(intel_crtc);
	else
		ironlake_pfit_disable(intel_crtc, false);

	if (!intel_crtc->config->has_dsi_encoder)
		intel_ddi_disable_pipe_clock(intel_crtc);

	if (intel_crtc->config->has_pch_encoder) {
		lpt_disable_pch_transcoder(dev_priv);
		intel_ddi_fdi_disable(crtc);
	}

	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->post_disable)
			encoder->post_disable(encoder);

	if (intel_crtc->config->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv, TRANSCODER_A,
						      true);

	intel_fbc_disable_crtc(intel_crtc);
}

static void i9xx_pfit_enable(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
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

static enum intel_display_power_domain port_to_power_domain(enum port port)
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

static enum intel_display_power_domain port_to_aux_power_domain(enum port port)
{
	switch (port) {
	case PORT_A:
		return POWER_DOMAIN_AUX_A;
	case PORT_B:
		return POWER_DOMAIN_AUX_B;
	case PORT_C:
		return POWER_DOMAIN_AUX_C;
	case PORT_D:
		return POWER_DOMAIN_AUX_D;
	case PORT_E:
		/* FIXME: Check VBT for actual wiring of PORT E */
		return POWER_DOMAIN_AUX_D;
	default:
		MISSING_CASE(port);
		return POWER_DOMAIN_AUX_A;
	}
}

enum intel_display_power_domain
intel_display_port_power_domain(struct intel_encoder *intel_encoder)
{
	struct drm_device *dev = intel_encoder->base.dev;
	struct intel_digital_port *intel_dig_port;

	switch (intel_encoder->type) {
	case INTEL_OUTPUT_UNKNOWN:
		/* Only DDI platforms should ever use this output type */
		WARN_ON_ONCE(!HAS_DDI(dev));
	case INTEL_OUTPUT_DISPLAYPORT:
	case INTEL_OUTPUT_HDMI:
	case INTEL_OUTPUT_EDP:
		intel_dig_port = enc_to_dig_port(&intel_encoder->base);
		return port_to_power_domain(intel_dig_port->port);
	case INTEL_OUTPUT_DP_MST:
		intel_dig_port = enc_to_mst(&intel_encoder->base)->primary;
		return port_to_power_domain(intel_dig_port->port);
	case INTEL_OUTPUT_ANALOG:
		return POWER_DOMAIN_PORT_CRT;
	case INTEL_OUTPUT_DSI:
		return POWER_DOMAIN_PORT_DSI;
	default:
		return POWER_DOMAIN_PORT_OTHER;
	}
}

enum intel_display_power_domain
intel_display_port_aux_power_domain(struct intel_encoder *intel_encoder)
{
	struct drm_device *dev = intel_encoder->base.dev;
	struct intel_digital_port *intel_dig_port;

	switch (intel_encoder->type) {
	case INTEL_OUTPUT_UNKNOWN:
	case INTEL_OUTPUT_HDMI:
		/*
		 * Only DDI platforms should ever use these output types.
		 * We can get here after the HDMI detect code has already set
		 * the type of the shared encoder. Since we can't be sure
		 * what's the status of the given connectors, play safe and
		 * run the DP detection too.
		 */
		WARN_ON_ONCE(!HAS_DDI(dev));
	case INTEL_OUTPUT_DISPLAYPORT:
	case INTEL_OUTPUT_EDP:
		intel_dig_port = enc_to_dig_port(&intel_encoder->base);
		return port_to_aux_power_domain(intel_dig_port->port);
	case INTEL_OUTPUT_DP_MST:
		intel_dig_port = enc_to_mst(&intel_encoder->base)->primary;
		return port_to_aux_power_domain(intel_dig_port->port);
	default:
		MISSING_CASE(intel_encoder->type);
		return POWER_DOMAIN_AUX_A;
	}
}

static unsigned long get_crtc_power_domains(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct intel_encoder *intel_encoder;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum pipe pipe = intel_crtc->pipe;
	unsigned long mask;
	enum transcoder transcoder = intel_crtc->config->cpu_transcoder;

	if (!crtc->state->active)
		return 0;

	mask = BIT(POWER_DOMAIN_PIPE(pipe));
	mask |= BIT(POWER_DOMAIN_TRANSCODER(transcoder));
	if (intel_crtc->config->pch_pfit.enabled ||
	    intel_crtc->config->pch_pfit.force_thru)
		mask |= BIT(POWER_DOMAIN_PIPE_PANEL_FITTER(pipe));

	for_each_encoder_on_crtc(dev, crtc, intel_encoder)
		mask |= BIT(intel_display_port_power_domain(intel_encoder));

	return mask;
}

static unsigned long modeset_get_crtc_power_domains(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = crtc->dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum intel_display_power_domain domain;
	unsigned long domains, new_domains, old_domains;

	old_domains = intel_crtc->enabled_power_domains;
	intel_crtc->enabled_power_domains = new_domains = get_crtc_power_domains(crtc);

	domains = new_domains & ~old_domains;

	for_each_power_domain(domain, domains)
		intel_display_power_get(dev_priv, domain);

	return old_domains & ~new_domains;
}

static void modeset_put_power_domains(struct drm_i915_private *dev_priv,
				      unsigned long domains)
{
	enum intel_display_power_domain domain;

	for_each_power_domain(domain, domains)
		intel_display_power_put(dev_priv, domain);
}

static void modeset_update_crtc_power_domains(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long put_domains[I915_MAX_PIPES] = {};
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		if (needs_modeset(crtc->state))
			put_domains[to_intel_crtc(crtc)->pipe] =
				modeset_get_crtc_power_domains(crtc);
	}

	if (dev_priv->display.modeset_commit_cdclk) {
		unsigned int cdclk = to_intel_atomic_state(state)->cdclk;

		if (cdclk != dev_priv->cdclk_freq &&
		    !WARN_ON(!state->allow_modeset))
			dev_priv->display.modeset_commit_cdclk(state);
	}

	for (i = 0; i < I915_MAX_PIPES; i++)
		if (put_domains[i])
			modeset_put_power_domains(dev_priv, put_domains[i]);
}

static int intel_compute_max_dotclk(struct drm_i915_private *dev_priv)
{
	int max_cdclk_freq = dev_priv->max_cdclk_freq;

	if (INTEL_INFO(dev_priv)->gen >= 9 ||
	    IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		return max_cdclk_freq;
	else if (IS_CHERRYVIEW(dev_priv))
		return max_cdclk_freq*95/100;
	else if (INTEL_INFO(dev_priv)->gen < 4)
		return 2*max_cdclk_freq*90/100;
	else
		return max_cdclk_freq*90/100;
}

static void intel_update_max_cdclk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev)) {
		u32 limit = I915_READ(SKL_DFSM) & SKL_DFSM_CDCLK_LIMIT_MASK;

		if (limit == SKL_DFSM_CDCLK_LIMIT_675)
			dev_priv->max_cdclk_freq = 675000;
		else if (limit == SKL_DFSM_CDCLK_LIMIT_540)
			dev_priv->max_cdclk_freq = 540000;
		else if (limit == SKL_DFSM_CDCLK_LIMIT_450)
			dev_priv->max_cdclk_freq = 450000;
		else
			dev_priv->max_cdclk_freq = 337500;
	} else if (IS_BROADWELL(dev))  {
		/*
		 * FIXME with extra cooling we can allow
		 * 540 MHz for ULX and 675 Mhz for ULT.
		 * How can we know if extra cooling is
		 * available? PCI ID, VTB, something else?
		 */
		if (I915_READ(FUSE_STRAP) & HSW_CDCLK_LIMIT)
			dev_priv->max_cdclk_freq = 450000;
		else if (IS_BDW_ULX(dev))
			dev_priv->max_cdclk_freq = 450000;
		else if (IS_BDW_ULT(dev))
			dev_priv->max_cdclk_freq = 540000;
		else
			dev_priv->max_cdclk_freq = 675000;
	} else if (IS_CHERRYVIEW(dev)) {
		dev_priv->max_cdclk_freq = 320000;
	} else if (IS_VALLEYVIEW(dev)) {
		dev_priv->max_cdclk_freq = 400000;
	} else {
		/* otherwise assume cdclk is fixed */
		dev_priv->max_cdclk_freq = dev_priv->cdclk_freq;
	}

	dev_priv->max_dotclk_freq = intel_compute_max_dotclk(dev_priv);

	DRM_DEBUG_DRIVER("Max CD clock rate: %d kHz\n",
			 dev_priv->max_cdclk_freq);

	DRM_DEBUG_DRIVER("Max dotclock rate: %d kHz\n",
			 dev_priv->max_dotclk_freq);
}

static void intel_update_cdclk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->cdclk_freq = dev_priv->display.get_display_clock_speed(dev);
	DRM_DEBUG_DRIVER("Current CD clock rate: %d kHz\n",
			 dev_priv->cdclk_freq);

	/*
	 * Program the gmbus_freq based on the cdclk frequency.
	 * BSpec erroneously claims we should aim for 4MHz, but
	 * in fact 1MHz is the correct frequency.
	 */
	if (IS_VALLEYVIEW(dev)) {
		/*
		 * Program the gmbus_freq based on the cdclk frequency.
		 * BSpec erroneously claims we should aim for 4MHz, but
		 * in fact 1MHz is the correct frequency.
		 */
		I915_WRITE(GMBUSFREQ_VLV, DIV_ROUND_UP(dev_priv->cdclk_freq, 1000));
	}

	if (dev_priv->max_cdclk_freq == 0)
		intel_update_max_cdclk(dev);
}

static void broxton_set_cdclk(struct drm_device *dev, int frequency)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t divider;
	uint32_t ratio;
	uint32_t current_freq;
	int ret;

	/* frequency = 19.2MHz * ratio / 2 / div{1,1.5,2,4} */
	switch (frequency) {
	case 144000:
		divider = BXT_CDCLK_CD2X_DIV_SEL_4;
		ratio = BXT_DE_PLL_RATIO(60);
		break;
	case 288000:
		divider = BXT_CDCLK_CD2X_DIV_SEL_2;
		ratio = BXT_DE_PLL_RATIO(60);
		break;
	case 384000:
		divider = BXT_CDCLK_CD2X_DIV_SEL_1_5;
		ratio = BXT_DE_PLL_RATIO(60);
		break;
	case 576000:
		divider = BXT_CDCLK_CD2X_DIV_SEL_1;
		ratio = BXT_DE_PLL_RATIO(60);
		break;
	case 624000:
		divider = BXT_CDCLK_CD2X_DIV_SEL_1;
		ratio = BXT_DE_PLL_RATIO(65);
		break;
	case 19200:
		/*
		 * Bypass frequency with DE PLL disabled. Init ratio, divider
		 * to suppress GCC warning.
		 */
		ratio = 0;
		divider = 0;
		break;
	default:
		DRM_ERROR("unsupported CDCLK freq %d", frequency);

		return;
	}

	mutex_lock(&dev_priv->rps.hw_lock);
	/* Inform power controller of upcoming frequency change */
	ret = sandybridge_pcode_write(dev_priv, HSW_PCODE_DE_WRITE_FREQ_REQ,
				      0x80000000);
	mutex_unlock(&dev_priv->rps.hw_lock);

	if (ret) {
		DRM_ERROR("PCode CDCLK freq change notify failed (err %d, freq %d)\n",
			  ret, frequency);
		return;
	}

	current_freq = I915_READ(CDCLK_CTL) & CDCLK_FREQ_DECIMAL_MASK;
	/* convert from .1 fixpoint MHz with -1MHz offset to kHz */
	current_freq = current_freq * 500 + 1000;

	/*
	 * DE PLL has to be disabled when
	 * - setting to 19.2MHz (bypass, PLL isn't used)
	 * - before setting to 624MHz (PLL needs toggling)
	 * - before setting to any frequency from 624MHz (PLL needs toggling)
	 */
	if (frequency == 19200 || frequency == 624000 ||
	    current_freq == 624000) {
		I915_WRITE(BXT_DE_PLL_ENABLE, ~BXT_DE_PLL_PLL_ENABLE);
		/* Timeout 200us */
		if (wait_for(!(I915_READ(BXT_DE_PLL_ENABLE) & BXT_DE_PLL_LOCK),
			     1))
			DRM_ERROR("timout waiting for DE PLL unlock\n");
	}

	if (frequency != 19200) {
		uint32_t val;

		val = I915_READ(BXT_DE_PLL_CTL);
		val &= ~BXT_DE_PLL_RATIO_MASK;
		val |= ratio;
		I915_WRITE(BXT_DE_PLL_CTL, val);

		I915_WRITE(BXT_DE_PLL_ENABLE, BXT_DE_PLL_PLL_ENABLE);
		/* Timeout 200us */
		if (wait_for(I915_READ(BXT_DE_PLL_ENABLE) & BXT_DE_PLL_LOCK, 1))
			DRM_ERROR("timeout waiting for DE PLL lock\n");

		val = I915_READ(CDCLK_CTL);
		val &= ~BXT_CDCLK_CD2X_DIV_SEL_MASK;
		val |= divider;
		/*
		 * Disable SSA Precharge when CD clock frequency < 500 MHz,
		 * enable otherwise.
		 */
		val &= ~BXT_CDCLK_SSA_PRECHARGE_ENABLE;
		if (frequency >= 500000)
			val |= BXT_CDCLK_SSA_PRECHARGE_ENABLE;

		val &= ~CDCLK_FREQ_DECIMAL_MASK;
		/* convert from kHz to .1 fixpoint MHz with -1MHz offset */
		val |= (frequency - 1000) / 500;
		I915_WRITE(CDCLK_CTL, val);
	}

	mutex_lock(&dev_priv->rps.hw_lock);
	ret = sandybridge_pcode_write(dev_priv, HSW_PCODE_DE_WRITE_FREQ_REQ,
				      DIV_ROUND_UP(frequency, 25000));
	mutex_unlock(&dev_priv->rps.hw_lock);

	if (ret) {
		DRM_ERROR("PCode CDCLK freq set failed, (err %d, freq %d)\n",
			  ret, frequency);
		return;
	}

	intel_update_cdclk(dev);
}

void broxton_init_cdclk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t val;

	/*
	 * NDE_RSTWRN_OPT RST PCH Handshake En must always be 0b on BXT
	 * or else the reset will hang because there is no PCH to respond.
	 * Move the handshake programming to initialization sequence.
	 * Previously was left up to BIOS.
	 */
	val = I915_READ(HSW_NDE_RSTWRN_OPT);
	val &= ~RESET_PCH_HANDSHAKE_ENABLE;
	I915_WRITE(HSW_NDE_RSTWRN_OPT, val);

	/* Enable PG1 for cdclk */
	intel_display_power_get(dev_priv, POWER_DOMAIN_PLLS);

	/* check if cd clock is enabled */
	if (I915_READ(BXT_DE_PLL_ENABLE) & BXT_DE_PLL_PLL_ENABLE) {
		DRM_DEBUG_KMS("Display already initialized\n");
		return;
	}

	/*
	 * FIXME:
	 * - The initial CDCLK needs to be read from VBT.
	 *   Need to make this change after VBT has changes for BXT.
	 * - check if setting the max (or any) cdclk freq is really necessary
	 *   here, it belongs to modeset time
	 */
	broxton_set_cdclk(dev, 624000);

	I915_WRITE(DBUF_CTL, I915_READ(DBUF_CTL) | DBUF_POWER_REQUEST);
	POSTING_READ(DBUF_CTL);

	udelay(10);

	if (!(I915_READ(DBUF_CTL) & DBUF_POWER_STATE))
		DRM_ERROR("DBuf power enable timeout!\n");
}

void broxton_uninit_cdclk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(DBUF_CTL, I915_READ(DBUF_CTL) & ~DBUF_POWER_REQUEST);
	POSTING_READ(DBUF_CTL);

	udelay(10);

	if (I915_READ(DBUF_CTL) & DBUF_POWER_STATE)
		DRM_ERROR("DBuf power disable timeout!\n");

	/* Set minimum (bypass) frequency, in effect turning off the DE PLL */
	broxton_set_cdclk(dev, 19200);

	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);
}

static const struct skl_cdclk_entry {
	unsigned int freq;
	unsigned int vco;
} skl_cdclk_frequencies[] = {
	{ .freq = 308570, .vco = 8640 },
	{ .freq = 337500, .vco = 8100 },
	{ .freq = 432000, .vco = 8640 },
	{ .freq = 450000, .vco = 8100 },
	{ .freq = 540000, .vco = 8100 },
	{ .freq = 617140, .vco = 8640 },
	{ .freq = 675000, .vco = 8100 },
};

static unsigned int skl_cdclk_decimal(unsigned int freq)
{
	return (freq - 1000) / 500;
}

static unsigned int skl_cdclk_get_vco(unsigned int freq)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(skl_cdclk_frequencies); i++) {
		const struct skl_cdclk_entry *e = &skl_cdclk_frequencies[i];

		if (e->freq == freq)
			return e->vco;
	}

	return 8100;
}

static void
skl_dpll0_enable(struct drm_i915_private *dev_priv, unsigned int required_vco)
{
	unsigned int min_freq;
	u32 val;

	/* select the minimum CDCLK before enabling DPLL 0 */
	val = I915_READ(CDCLK_CTL);
	val &= ~CDCLK_FREQ_SEL_MASK | ~CDCLK_FREQ_DECIMAL_MASK;
	val |= CDCLK_FREQ_337_308;

	if (required_vco == 8640)
		min_freq = 308570;
	else
		min_freq = 337500;

	val = CDCLK_FREQ_337_308 | skl_cdclk_decimal(min_freq);

	I915_WRITE(CDCLK_CTL, val);
	POSTING_READ(CDCLK_CTL);

	/*
	 * We always enable DPLL0 with the lowest link rate possible, but still
	 * taking into account the VCO required to operate the eDP panel at the
	 * desired frequency. The usual DP link rates operate with a VCO of
	 * 8100 while the eDP 1.4 alternate link rates need a VCO of 8640.
	 * The modeset code is responsible for the selection of the exact link
	 * rate later on, with the constraint of choosing a frequency that
	 * works with required_vco.
	 */
	val = I915_READ(DPLL_CTRL1);

	val &= ~(DPLL_CTRL1_HDMI_MODE(SKL_DPLL0) | DPLL_CTRL1_SSC(SKL_DPLL0) |
		 DPLL_CTRL1_LINK_RATE_MASK(SKL_DPLL0));
	val |= DPLL_CTRL1_OVERRIDE(SKL_DPLL0);
	if (required_vco == 8640)
		val |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_1080,
					    SKL_DPLL0);
	else
		val |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_810,
					    SKL_DPLL0);

	I915_WRITE(DPLL_CTRL1, val);
	POSTING_READ(DPLL_CTRL1);

	I915_WRITE(LCPLL1_CTL, I915_READ(LCPLL1_CTL) | LCPLL_PLL_ENABLE);

	if (wait_for(I915_READ(LCPLL1_CTL) & LCPLL_PLL_LOCK, 5))
		DRM_ERROR("DPLL0 not locked\n");
}

static bool skl_cdclk_pcu_ready(struct drm_i915_private *dev_priv)
{
	int ret;
	u32 val;

	/* inform PCU we want to change CDCLK */
	val = SKL_CDCLK_PREPARE_FOR_CHANGE;
	mutex_lock(&dev_priv->rps.hw_lock);
	ret = sandybridge_pcode_read(dev_priv, SKL_PCODE_CDCLK_CONTROL, &val);
	mutex_unlock(&dev_priv->rps.hw_lock);

	return ret == 0 && (val & SKL_CDCLK_READY_FOR_CHANGE);
}

static bool skl_cdclk_wait_for_pcu_ready(struct drm_i915_private *dev_priv)
{
	unsigned int i;

	for (i = 0; i < 15; i++) {
		if (skl_cdclk_pcu_ready(dev_priv))
			return true;
		udelay(10);
	}

	return false;
}

static void skl_set_cdclk(struct drm_i915_private *dev_priv, unsigned int freq)
{
	struct drm_device *dev = dev_priv->dev;
	u32 freq_select, pcu_ack;

	DRM_DEBUG_DRIVER("Changing CDCLK to %dKHz\n", freq);

	if (!skl_cdclk_wait_for_pcu_ready(dev_priv)) {
		DRM_ERROR("failed to inform PCU about cdclk change\n");
		return;
	}

	/* set CDCLK_CTL */
	switch(freq) {
	case 450000:
	case 432000:
		freq_select = CDCLK_FREQ_450_432;
		pcu_ack = 1;
		break;
	case 540000:
		freq_select = CDCLK_FREQ_540;
		pcu_ack = 2;
		break;
	case 308570:
	case 337500:
	default:
		freq_select = CDCLK_FREQ_337_308;
		pcu_ack = 0;
		break;
	case 617140:
	case 675000:
		freq_select = CDCLK_FREQ_675_617;
		pcu_ack = 3;
		break;
	}

	I915_WRITE(CDCLK_CTL, freq_select | skl_cdclk_decimal(freq));
	POSTING_READ(CDCLK_CTL);

	/* inform PCU of the change */
	mutex_lock(&dev_priv->rps.hw_lock);
	sandybridge_pcode_write(dev_priv, SKL_PCODE_CDCLK_CONTROL, pcu_ack);
	mutex_unlock(&dev_priv->rps.hw_lock);

	intel_update_cdclk(dev);
}

void skl_uninit_cdclk(struct drm_i915_private *dev_priv)
{
	/* disable DBUF power */
	I915_WRITE(DBUF_CTL, I915_READ(DBUF_CTL) & ~DBUF_POWER_REQUEST);
	POSTING_READ(DBUF_CTL);

	udelay(10);

	if (I915_READ(DBUF_CTL) & DBUF_POWER_STATE)
		DRM_ERROR("DBuf power disable timeout\n");

	/* disable DPLL0 */
	I915_WRITE(LCPLL1_CTL, I915_READ(LCPLL1_CTL) & ~LCPLL_PLL_ENABLE);
	if (wait_for(!(I915_READ(LCPLL1_CTL) & LCPLL_PLL_LOCK), 1))
		DRM_ERROR("Couldn't disable DPLL0\n");
}

void skl_init_cdclk(struct drm_i915_private *dev_priv)
{
	unsigned int required_vco;

	/* DPLL0 not enabled (happens on early BIOS versions) */
	if (!(I915_READ(LCPLL1_CTL) & LCPLL_PLL_ENABLE)) {
		/* enable DPLL0 */
		required_vco = skl_cdclk_get_vco(dev_priv->skl_boot_cdclk);
		skl_dpll0_enable(dev_priv, required_vco);
	}

	/* set CDCLK to the frequency the BIOS chose */
	skl_set_cdclk(dev_priv, dev_priv->skl_boot_cdclk);

	/* enable DBUF power */
	I915_WRITE(DBUF_CTL, I915_READ(DBUF_CTL) | DBUF_POWER_REQUEST);
	POSTING_READ(DBUF_CTL);

	udelay(10);

	if (!(I915_READ(DBUF_CTL) & DBUF_POWER_STATE))
		DRM_ERROR("DBuf power enable timeout\n");
}

int skl_sanitize_cdclk(struct drm_i915_private *dev_priv)
{
	uint32_t lcpll1 = I915_READ(LCPLL1_CTL);
	uint32_t cdctl = I915_READ(CDCLK_CTL);
	int freq = dev_priv->skl_boot_cdclk;

	/*
	 * check if the pre-os intialized the display
	 * There is SWF18 scratchpad register defined which is set by the
	 * pre-os which can be used by the OS drivers to check the status
	 */
	if ((I915_READ(SWF_ILK(0x18)) & 0x00FFFFFF) == 0)
		goto sanitize;

	/* Is PLL enabled and locked ? */
	if (!((lcpll1 & LCPLL_PLL_ENABLE) && (lcpll1 & LCPLL_PLL_LOCK)))
		goto sanitize;

	/* DPLL okay; verify the cdclock
	 *
	 * Noticed in some instances that the freq selection is correct but
	 * decimal part is programmed wrong from BIOS where pre-os does not
	 * enable display. Verify the same as well.
	 */
	if (cdctl == ((cdctl & CDCLK_FREQ_SEL_MASK) | skl_cdclk_decimal(freq)))
		/* All well; nothing to sanitize */
		return false;
sanitize:
	/*
	 * As of now initialize with max cdclk till
	 * we get dynamic cdclk support
	 * */
	dev_priv->skl_boot_cdclk = dev_priv->max_cdclk_freq;
	skl_init_cdclk(dev_priv);

	/* we did have to sanitize */
	return true;
}

/* Adjust CDclk dividers to allow high res or save power if possible */
static void valleyview_set_cdclk(struct drm_device *dev, int cdclk)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val, cmd;

	WARN_ON(dev_priv->display.get_display_clock_speed(dev)
					!= dev_priv->cdclk_freq);

	if (cdclk >= 320000) /* jump to highest voltage for 400MHz too */
		cmd = 2;
	else if (cdclk == 266667)
		cmd = 1;
	else
		cmd = 0;

	mutex_lock(&dev_priv->rps.hw_lock);
	val = vlv_punit_read(dev_priv, PUNIT_REG_DSPFREQ);
	val &= ~DSPFREQGUAR_MASK;
	val |= (cmd << DSPFREQGUAR_SHIFT);
	vlv_punit_write(dev_priv, PUNIT_REG_DSPFREQ, val);
	if (wait_for((vlv_punit_read(dev_priv, PUNIT_REG_DSPFREQ) &
		      DSPFREQSTAT_MASK) == (cmd << DSPFREQSTAT_SHIFT),
		     50)) {
		DRM_ERROR("timed out waiting for CDclk change\n");
	}
	mutex_unlock(&dev_priv->rps.hw_lock);

	mutex_lock(&dev_priv->sb_lock);

	if (cdclk == 400000) {
		u32 divider;

		divider = DIV_ROUND_CLOSEST(dev_priv->hpll_freq << 1, cdclk) - 1;

		/* adjust cdclk divider */
		val = vlv_cck_read(dev_priv, CCK_DISPLAY_CLOCK_CONTROL);
		val &= ~CCK_FREQUENCY_VALUES;
		val |= divider;
		vlv_cck_write(dev_priv, CCK_DISPLAY_CLOCK_CONTROL, val);

		if (wait_for((vlv_cck_read(dev_priv, CCK_DISPLAY_CLOCK_CONTROL) &
			      CCK_FREQUENCY_STATUS) == (divider << CCK_FREQUENCY_STATUS_SHIFT),
			     50))
			DRM_ERROR("timed out waiting for CDclk change\n");
	}

	/* adjust self-refresh exit latency value */
	val = vlv_bunit_read(dev_priv, BUNIT_REG_BISOC);
	val &= ~0x7f;

	/*
	 * For high bandwidth configs, we set a higher latency in the bunit
	 * so that the core display fetch happens in time to avoid underruns.
	 */
	if (cdclk == 400000)
		val |= 4500 / 250; /* 4.5 usec */
	else
		val |= 3000 / 250; /* 3.0 usec */
	vlv_bunit_write(dev_priv, BUNIT_REG_BISOC, val);

	mutex_unlock(&dev_priv->sb_lock);

	intel_update_cdclk(dev);
}

static void cherryview_set_cdclk(struct drm_device *dev, int cdclk)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val, cmd;

	WARN_ON(dev_priv->display.get_display_clock_speed(dev)
						!= dev_priv->cdclk_freq);

	switch (cdclk) {
	case 333333:
	case 320000:
	case 266667:
	case 200000:
		break;
	default:
		MISSING_CASE(cdclk);
		return;
	}

	/*
	 * Specs are full of misinformation, but testing on actual
	 * hardware has shown that we just need to write the desired
	 * CCK divider into the Punit register.
	 */
	cmd = DIV_ROUND_CLOSEST(dev_priv->hpll_freq << 1, cdclk) - 1;

	mutex_lock(&dev_priv->rps.hw_lock);
	val = vlv_punit_read(dev_priv, PUNIT_REG_DSPFREQ);
	val &= ~DSPFREQGUAR_MASK_CHV;
	val |= (cmd << DSPFREQGUAR_SHIFT_CHV);
	vlv_punit_write(dev_priv, PUNIT_REG_DSPFREQ, val);
	if (wait_for((vlv_punit_read(dev_priv, PUNIT_REG_DSPFREQ) &
		      DSPFREQSTAT_MASK_CHV) == (cmd << DSPFREQSTAT_SHIFT_CHV),
		     50)) {
		DRM_ERROR("timed out waiting for CDclk change\n");
	}
	mutex_unlock(&dev_priv->rps.hw_lock);

	intel_update_cdclk(dev);
}

static int valleyview_calc_cdclk(struct drm_i915_private *dev_priv,
				 int max_pixclk)
{
	int freq_320 = (dev_priv->hpll_freq <<  1) % 320000 != 0 ? 333333 : 320000;
	int limit = IS_CHERRYVIEW(dev_priv) ? 95 : 90;

	/*
	 * Really only a few cases to deal with, as only 4 CDclks are supported:
	 *   200MHz
	 *   267MHz
	 *   320/333MHz (depends on HPLL freq)
	 *   400MHz (VLV only)
	 * So we check to see whether we're above 90% (VLV) or 95% (CHV)
	 * of the lower bin and adjust if needed.
	 *
	 * We seem to get an unstable or solid color picture at 200MHz.
	 * Not sure what's wrong. For now use 200MHz only when all pipes
	 * are off.
	 */
	if (!IS_CHERRYVIEW(dev_priv) &&
	    max_pixclk > freq_320*limit/100)
		return 400000;
	else if (max_pixclk > 266667*limit/100)
		return freq_320;
	else if (max_pixclk > 0)
		return 266667;
	else
		return 200000;
}

static int broxton_calc_cdclk(struct drm_i915_private *dev_priv,
			      int max_pixclk)
{
	/*
	 * FIXME:
	 * - remove the guardband, it's not needed on BXT
	 * - set 19.2MHz bypass frequency if there are no active pipes
	 */
	if (max_pixclk > 576000*9/10)
		return 624000;
	else if (max_pixclk > 384000*9/10)
		return 576000;
	else if (max_pixclk > 288000*9/10)
		return 384000;
	else if (max_pixclk > 144000*9/10)
		return 288000;
	else
		return 144000;
}

/* Compute the max pixel clock for new configuration. Uses atomic state if
 * that's non-NULL, look at current state otherwise. */
static int intel_mode_max_pixclk(struct drm_device *dev,
				 struct drm_atomic_state *state)
{
	struct intel_crtc *intel_crtc;
	struct intel_crtc_state *crtc_state;
	int max_pixclk = 0;

	for_each_intel_crtc(dev, intel_crtc) {
		crtc_state = intel_atomic_get_crtc_state(state, intel_crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		if (!crtc_state->base.enable)
			continue;

		max_pixclk = max(max_pixclk,
				 crtc_state->base.adjusted_mode.crtc_clock);
	}

	return max_pixclk;
}

static int valleyview_modeset_calc_cdclk(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int max_pixclk = intel_mode_max_pixclk(dev, state);

	if (max_pixclk < 0)
		return max_pixclk;

	to_intel_atomic_state(state)->cdclk =
		valleyview_calc_cdclk(dev_priv, max_pixclk);

	return 0;
}

static int broxton_modeset_calc_cdclk(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int max_pixclk = intel_mode_max_pixclk(dev, state);

	if (max_pixclk < 0)
		return max_pixclk;

	to_intel_atomic_state(state)->cdclk =
		broxton_calc_cdclk(dev_priv, max_pixclk);

	return 0;
}

static void vlv_program_pfi_credits(struct drm_i915_private *dev_priv)
{
	unsigned int credits, default_credits;

	if (IS_CHERRYVIEW(dev_priv))
		default_credits = PFI_CREDIT(12);
	else
		default_credits = PFI_CREDIT(8);

	if (dev_priv->cdclk_freq >= dev_priv->czclk_freq) {
		/* CHV suggested value is 31 or 63 */
		if (IS_CHERRYVIEW(dev_priv))
			credits = PFI_CREDIT_63;
		else
			credits = PFI_CREDIT(15);
	} else {
		credits = default_credits;
	}

	/*
	 * WA - write default credits before re-programming
	 * FIXME: should we also set the resend bit here?
	 */
	I915_WRITE(GCI_CONTROL, VGA_FAST_MODE_DISABLE |
		   default_credits);

	I915_WRITE(GCI_CONTROL, VGA_FAST_MODE_DISABLE |
		   credits | PFI_CREDIT_RESEND);

	/*
	 * FIXME is this guaranteed to clear
	 * immediately or should we poll for it?
	 */
	WARN_ON(I915_READ(GCI_CONTROL) & PFI_CREDIT_RESEND);
}

static void valleyview_modeset_commit_cdclk(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;
	unsigned int req_cdclk = to_intel_atomic_state(old_state)->cdclk;
	struct drm_i915_private *dev_priv = dev->dev_private;

	/*
	 * FIXME: We can end up here with all power domains off, yet
	 * with a CDCLK frequency other than the minimum. To account
	 * for this take the PIPE-A power domain, which covers the HW
	 * blocks needed for the following programming. This can be
	 * removed once it's guaranteed that we get here either with
	 * the minimum CDCLK set, or the required power domains
	 * enabled.
	 */
	intel_display_power_get(dev_priv, POWER_DOMAIN_PIPE_A);

	if (IS_CHERRYVIEW(dev))
		cherryview_set_cdclk(dev, req_cdclk);
	else
		valleyview_set_cdclk(dev, req_cdclk);

	vlv_program_pfi_credits(dev_priv);

	intel_display_power_put(dev_priv, POWER_DOMAIN_PIPE_A);
}

static void valleyview_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *encoder;
	int pipe = intel_crtc->pipe;

	if (WARN_ON(intel_crtc->active))
		return;

	if (intel_crtc->config->has_dp_encoder)
		intel_dp_set_m_n(intel_crtc, M1_N1);

	intel_set_pipe_timings(intel_crtc);

	if (IS_CHERRYVIEW(dev) && pipe == PIPE_B) {
		struct drm_i915_private *dev_priv = dev->dev_private;

		I915_WRITE(CHV_BLEND(pipe), CHV_BLEND_LEGACY);
		I915_WRITE(CHV_CANVAS(pipe), 0);
	}

	i9xx_set_pipeconf(intel_crtc);

	intel_crtc->active = true;

	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->pre_pll_enable)
			encoder->pre_pll_enable(encoder);

	if (!intel_crtc->config->has_dsi_encoder) {
		if (IS_CHERRYVIEW(dev)) {
			chv_prepare_pll(intel_crtc, intel_crtc->config);
			chv_enable_pll(intel_crtc, intel_crtc->config);
		} else {
			vlv_prepare_pll(intel_crtc, intel_crtc->config);
			vlv_enable_pll(intel_crtc, intel_crtc->config);
		}
	}

	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->pre_enable)
			encoder->pre_enable(encoder);

	i9xx_pfit_enable(intel_crtc);

	intel_crtc_load_lut(crtc);

	intel_enable_pipe(intel_crtc);

	assert_vblank_disabled(crtc);
	drm_crtc_vblank_on(crtc);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		encoder->enable(encoder);
}

static void i9xx_set_pll_dividers(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(FP0(crtc->pipe), crtc->config->dpll_hw_state.fp0);
	I915_WRITE(FP1(crtc->pipe), crtc->config->dpll_hw_state.fp1);
}

static void i9xx_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *encoder;
	int pipe = intel_crtc->pipe;

	if (WARN_ON(intel_crtc->active))
		return;

	i9xx_set_pll_dividers(intel_crtc);

	if (intel_crtc->config->has_dp_encoder)
		intel_dp_set_m_n(intel_crtc, M1_N1);

	intel_set_pipe_timings(intel_crtc);

	i9xx_set_pipeconf(intel_crtc);

	intel_crtc->active = true;

	if (!IS_GEN2(dev))
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->pre_enable)
			encoder->pre_enable(encoder);

	i9xx_enable_pll(intel_crtc);

	i9xx_pfit_enable(intel_crtc);

	intel_crtc_load_lut(crtc);

	intel_update_watermarks(crtc);
	intel_enable_pipe(intel_crtc);

	assert_vblank_disabled(crtc);
	drm_crtc_vblank_on(crtc);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		encoder->enable(encoder);

	intel_fbc_enable(intel_crtc);
}

static void i9xx_pfit_disable(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!crtc->config->gmch_pfit.control)
		return;

	assert_pipe_disabled(dev_priv, crtc->pipe);

	DRM_DEBUG_DRIVER("disabling pfit, current: 0x%08x\n",
			 I915_READ(PFIT_CONTROL));
	I915_WRITE(PFIT_CONTROL, 0);
}

static void i9xx_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *encoder;
	int pipe = intel_crtc->pipe;

	/*
	 * On gen2 planes are double buffered but the pipe isn't, so we must
	 * wait for planes to fully turn off before disabling the pipe.
	 * We also need to wait on all gmch platforms because of the
	 * self-refresh mode constraint explained above.
	 */
	intel_wait_for_vblank(dev, pipe);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		encoder->disable(encoder);

	drm_crtc_vblank_off(crtc);
	assert_vblank_disabled(crtc);

	intel_disable_pipe(intel_crtc);

	i9xx_pfit_disable(intel_crtc);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->post_disable)
			encoder->post_disable(encoder);

	if (!intel_crtc->config->has_dsi_encoder) {
		if (IS_CHERRYVIEW(dev))
			chv_disable_pll(dev_priv, pipe);
		else if (IS_VALLEYVIEW(dev))
			vlv_disable_pll(dev_priv, pipe);
		else
			i9xx_disable_pll(intel_crtc);
	}

	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->post_pll_disable)
			encoder->post_pll_disable(encoder);

	if (!IS_GEN2(dev))
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);

	intel_fbc_disable_crtc(intel_crtc);
}

static void intel_crtc_disable_noatomic(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum intel_display_power_domain domain;
	unsigned long domains;

	if (!intel_crtc->active)
		return;

	if (to_intel_plane_state(crtc->primary->state)->visible) {
		WARN_ON(intel_crtc->unpin_work);

		intel_pre_disable_primary(crtc);
	}

	intel_crtc_disable_planes(crtc, crtc->state->plane_mask);
	dev_priv->display.crtc_disable(crtc);
	intel_crtc->active = false;
	intel_update_watermarks(crtc);
	intel_disable_shared_dpll(intel_crtc);

	domains = intel_crtc->enabled_power_domains;
	for_each_power_domain(domain, domains)
		intel_display_power_put(dev_priv, domain);
	intel_crtc->enabled_power_domains = 0;
}

/*
 * turn all crtc's off, but do not adjust state
 * This has to be paired with a call to intel_modeset_setup_hw_state.
 */
int intel_display_suspend(struct drm_device *dev)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_modeset_acquire_ctx *ctx = config->acquire_ctx;
	struct drm_atomic_state *state;
	struct drm_crtc *crtc;
	unsigned crtc_mask = 0;
	int ret = 0;

	if (WARN_ON(!ctx))
		return 0;

	lockdep_assert_held(&ctx->ww_ctx);
	state = drm_atomic_state_alloc(dev);
	if (WARN_ON(!state))
		return -ENOMEM;

	state->acquire_ctx = ctx;
	state->allow_modeset = true;

	for_each_crtc(dev, crtc) {
		struct drm_crtc_state *crtc_state =
			drm_atomic_get_crtc_state(state, crtc);

		ret = PTR_ERR_OR_ZERO(crtc_state);
		if (ret)
			goto free;

		if (!crtc_state->active)
			continue;

		crtc_state->active = false;
		crtc_mask |= 1 << drm_crtc_index(crtc);
	}

	if (crtc_mask) {
		ret = drm_atomic_commit(state);

		if (!ret) {
			for_each_crtc(dev, crtc)
				if (crtc_mask & (1 << drm_crtc_index(crtc)))
					crtc->state->active = true;

			return ret;
		}
	}

free:
	if (ret)
		DRM_ERROR("Suspending crtc's failed with %i\n", ret);
	drm_atomic_state_free(state);
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
static void intel_connector_check_state(struct intel_connector *connector)
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
	struct drm_connector_state *connector_state;

	connector_state = kzalloc(sizeof *connector_state, GFP_KERNEL);
	if (!connector_state)
		return -ENOMEM;

	connector->base.state = connector_state;
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

	if (IS_HASWELL(dev) || IS_BROADWELL(dev)) {
		if (pipe_config->fdi_lanes > 2) {
			DRM_DEBUG_KMS("only 2 lanes on haswell, required: %i lanes\n",
				      pipe_config->fdi_lanes);
			return -EINVAL;
		} else {
			return 0;
		}
	}

	if (INTEL_INFO(dev)->num_pipes == 2)
		return 0;

	/* Ivybridge 3 pipe is really complicated */
	switch (pipe) {
	case PIPE_A:
		return 0;
	case PIPE_B:
		if (pipe_config->fdi_lanes <= 2)
			return 0;

		other_crtc = to_intel_crtc(intel_get_crtc_for_pipe(dev, PIPE_C));
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

		other_crtc = to_intel_crtc(intel_get_crtc_for_pipe(dev, PIPE_B));
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
	link_bw = intel_fdi_link_freq(dev) * MHz(100)/KHz(1)/10;

	fdi_dotclock = adjusted_mode->crtc_clock;

	lane = ironlake_get_lanes_required(fdi_dotclock, link_bw,
					   pipe_config->pipe_bpp);

	pipe_config->fdi_lanes = lane;

	intel_link_compute_m_n(pipe_config->pipe_bpp, lane, fdi_dotclock,
			       link_bw, &pipe_config->fdi_m_n);

	ret = ironlake_check_fdi_lanes(intel_crtc->base.dev,
				       intel_crtc->pipe, pipe_config);
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
	if (IS_HASWELL(dev_priv->dev))
		return true;

	/*
	 * We compare against max which means we must take
	 * the increased cdclk requirement into account when
	 * calculating the new cdclk.
	 *
	 * Should measure whether using a lower cdclk w/o IPS
	 */
	return ilk_pipe_pixel_rate(pipe_config) <=
		dev_priv->max_cdclk_freq * 95 / 100;
}

static void hsw_compute_ips_config(struct intel_crtc *crtc,
				   struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

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

static int intel_crtc_compute_config(struct intel_crtc *crtc,
				     struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	const struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;

	/* FIXME should check pixel clock limits on all platforms */
	if (INTEL_INFO(dev)->gen < 4) {
		int clock_limit = dev_priv->max_cdclk_freq * 9 / 10;

		/*
		 * Enable double wide mode when the dot clock
		 * is > 90% of the (display) core speed.
		 */
		if (intel_crtc_supports_double_wide(crtc) &&
		    adjusted_mode->crtc_clock > clock_limit) {
			clock_limit *= 2;
			pipe_config->double_wide = true;
		}

		if (adjusted_mode->crtc_clock > clock_limit) {
			DRM_DEBUG_KMS("requested pixel clock (%d kHz) too high (max: %d kHz, double wide: %s)\n",
				      adjusted_mode->crtc_clock, clock_limit,
				      yesno(pipe_config->double_wide));
			return -EINVAL;
		}
	}

	/*
	 * Pipe horizontal size must be even in:
	 * - DVO ganged mode
	 * - LVDS dual channel mode
	 * - Double wide pipe
	 */
	if ((intel_pipe_will_have_type(pipe_config, INTEL_OUTPUT_LVDS) &&
	     intel_is_dual_link_lvds(dev)) || pipe_config->double_wide)
		pipe_config->pipe_src_w &= ~1;

	/* Cantiga+ cannot handle modes with a hsync front porch of 0.
	 * WaPruneModeWithIncorrectHsyncOffset:ctg,elk,ilk,snb,ivb,vlv,hsw.
	 */
	if ((INTEL_INFO(dev)->gen > 4 || IS_G4X(dev)) &&
		adjusted_mode->crtc_hsync_start == adjusted_mode->crtc_hdisplay)
		return -EINVAL;

	if (HAS_IPS(dev))
		hsw_compute_ips_config(crtc, pipe_config);

	if (pipe_config->has_pch_encoder)
		return ironlake_fdi_compute_config(crtc, pipe_config);

	return 0;
}

static int skylake_get_display_clock_speed(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	uint32_t lcpll1 = I915_READ(LCPLL1_CTL);
	uint32_t cdctl = I915_READ(CDCLK_CTL);
	uint32_t linkrate;

	if (!(lcpll1 & LCPLL_PLL_ENABLE))
		return 24000; /* 24MHz is the cd freq with NSSC ref */

	if ((cdctl & CDCLK_FREQ_SEL_MASK) == CDCLK_FREQ_540)
		return 540000;

	linkrate = (I915_READ(DPLL_CTRL1) &
		    DPLL_CTRL1_LINK_RATE_MASK(SKL_DPLL0)) >> 1;

	if (linkrate == DPLL_CTRL1_LINK_RATE_2160 ||
	    linkrate == DPLL_CTRL1_LINK_RATE_1080) {
		/* vco 8640 */
		switch (cdctl & CDCLK_FREQ_SEL_MASK) {
		case CDCLK_FREQ_450_432:
			return 432000;
		case CDCLK_FREQ_337_308:
			return 308570;
		case CDCLK_FREQ_675_617:
			return 617140;
		default:
			WARN(1, "Unknown cd freq selection\n");
		}
	} else {
		/* vco 8100 */
		switch (cdctl & CDCLK_FREQ_SEL_MASK) {
		case CDCLK_FREQ_450_432:
			return 450000;
		case CDCLK_FREQ_337_308:
			return 337500;
		case CDCLK_FREQ_675_617:
			return 675000;
		default:
			WARN(1, "Unknown cd freq selection\n");
		}
	}

	/* error case, do as if DPLL0 isn't enabled */
	return 24000;
}

static int broxton_get_display_clock_speed(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	uint32_t cdctl = I915_READ(CDCLK_CTL);
	uint32_t pll_ratio = I915_READ(BXT_DE_PLL_CTL) & BXT_DE_PLL_RATIO_MASK;
	uint32_t pll_enab = I915_READ(BXT_DE_PLL_ENABLE);
	int cdclk;

	if (!(pll_enab & BXT_DE_PLL_PLL_ENABLE))
		return 19200;

	cdclk = 19200 * pll_ratio / 2;

	switch (cdctl & BXT_CDCLK_CD2X_DIV_SEL_MASK) {
	case BXT_CDCLK_CD2X_DIV_SEL_1:
		return cdclk;  /* 576MHz or 624MHz */
	case BXT_CDCLK_CD2X_DIV_SEL_1_5:
		return cdclk * 2 / 3; /* 384MHz */
	case BXT_CDCLK_CD2X_DIV_SEL_2:
		return cdclk / 2; /* 288MHz */
	case BXT_CDCLK_CD2X_DIV_SEL_4:
		return cdclk / 4; /* 144MHz */
	}

	/* error case, do as if DE PLL isn't enabled */
	return 19200;
}

static int broadwell_get_display_clock_speed(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t lcpll = I915_READ(LCPLL_CTL);
	uint32_t freq = lcpll & LCPLL_CLK_FREQ_MASK;

	if (lcpll & LCPLL_CD_SOURCE_FCLK)
		return 800000;
	else if (I915_READ(FUSE_STRAP) & HSW_CDCLK_LIMIT)
		return 450000;
	else if (freq == LCPLL_CLK_FREQ_450)
		return 450000;
	else if (freq == LCPLL_CLK_FREQ_54O_BDW)
		return 540000;
	else if (freq == LCPLL_CLK_FREQ_337_5_BDW)
		return 337500;
	else
		return 675000;
}

static int haswell_get_display_clock_speed(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t lcpll = I915_READ(LCPLL_CTL);
	uint32_t freq = lcpll & LCPLL_CLK_FREQ_MASK;

	if (lcpll & LCPLL_CD_SOURCE_FCLK)
		return 800000;
	else if (I915_READ(FUSE_STRAP) & HSW_CDCLK_LIMIT)
		return 450000;
	else if (freq == LCPLL_CLK_FREQ_450)
		return 450000;
	else if (IS_HSW_ULT(dev))
		return 337500;
	else
		return 540000;
}

static int valleyview_get_display_clock_speed(struct drm_device *dev)
{
	return vlv_get_cck_clock_hpll(to_i915(dev), "cdclk",
				      CCK_DISPLAY_CLOCK_CONTROL);
}

static int ilk_get_display_clock_speed(struct drm_device *dev)
{
	return 450000;
}

static int i945_get_display_clock_speed(struct drm_device *dev)
{
	return 400000;
}

static int i915_get_display_clock_speed(struct drm_device *dev)
{
	return 333333;
}

static int i9xx_misc_get_display_clock_speed(struct drm_device *dev)
{
	return 200000;
}

static int pnv_get_display_clock_speed(struct drm_device *dev)
{
	u16 gcfgc = 0;

	pci_read_config_word(dev->pdev, GCFGC, &gcfgc);

	switch (gcfgc & GC_DISPLAY_CLOCK_MASK) {
	case GC_DISPLAY_CLOCK_267_MHZ_PNV:
		return 266667;
	case GC_DISPLAY_CLOCK_333_MHZ_PNV:
		return 333333;
	case GC_DISPLAY_CLOCK_444_MHZ_PNV:
		return 444444;
	case GC_DISPLAY_CLOCK_200_MHZ_PNV:
		return 200000;
	default:
		DRM_ERROR("Unknown pnv display core clock 0x%04x\n", gcfgc);
	case GC_DISPLAY_CLOCK_133_MHZ_PNV:
		return 133333;
	case GC_DISPLAY_CLOCK_167_MHZ_PNV:
		return 166667;
	}
}

static int i915gm_get_display_clock_speed(struct drm_device *dev)
{
	u16 gcfgc = 0;

	pci_read_config_word(dev->pdev, GCFGC, &gcfgc);

	if (gcfgc & GC_LOW_FREQUENCY_ENABLE)
		return 133333;
	else {
		switch (gcfgc & GC_DISPLAY_CLOCK_MASK) {
		case GC_DISPLAY_CLOCK_333_MHZ:
			return 333333;
		default:
		case GC_DISPLAY_CLOCK_190_200_MHZ:
			return 190000;
		}
	}
}

static int i865_get_display_clock_speed(struct drm_device *dev)
{
	return 266667;
}

static int i85x_get_display_clock_speed(struct drm_device *dev)
{
	u16 hpllcc = 0;

	/*
	 * 852GM/852GMV only supports 133 MHz and the HPLLCC
	 * encoding is different :(
	 * FIXME is this the right way to detect 852GM/852GMV?
	 */
	if (dev->pdev->revision == 0x1)
		return 133333;

	pci_bus_read_config_word(dev->pdev->bus,
				 PCI_DEVFN(0, 3), HPLLCC, &hpllcc);

	/* Assume that the hardware is in the high speed state.  This
	 * should be the default.
	 */
	switch (hpllcc & GC_CLOCK_CONTROL_MASK) {
	case GC_CLOCK_133_200:
	case GC_CLOCK_133_200_2:
	case GC_CLOCK_100_200:
		return 200000;
	case GC_CLOCK_166_250:
		return 250000;
	case GC_CLOCK_100_133:
		return 133333;
	case GC_CLOCK_133_266:
	case GC_CLOCK_133_266_2:
	case GC_CLOCK_166_266:
		return 266667;
	}

	/* Shouldn't happen */
	return 0;
}

static int i830_get_display_clock_speed(struct drm_device *dev)
{
	return 133333;
}

static unsigned int intel_hpll_vco(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	static const unsigned int blb_vco[8] = {
		[0] = 3200000,
		[1] = 4000000,
		[2] = 5333333,
		[3] = 4800000,
		[4] = 6400000,
	};
	static const unsigned int pnv_vco[8] = {
		[0] = 3200000,
		[1] = 4000000,
		[2] = 5333333,
		[3] = 4800000,
		[4] = 2666667,
	};
	static const unsigned int cl_vco[8] = {
		[0] = 3200000,
		[1] = 4000000,
		[2] = 5333333,
		[3] = 6400000,
		[4] = 3333333,
		[5] = 3566667,
		[6] = 4266667,
	};
	static const unsigned int elk_vco[8] = {
		[0] = 3200000,
		[1] = 4000000,
		[2] = 5333333,
		[3] = 4800000,
	};
	static const unsigned int ctg_vco[8] = {
		[0] = 3200000,
		[1] = 4000000,
		[2] = 5333333,
		[3] = 6400000,
		[4] = 2666667,
		[5] = 4266667,
	};
	const unsigned int *vco_table;
	unsigned int vco;
	uint8_t tmp = 0;

	/* FIXME other chipsets? */
	if (IS_GM45(dev))
		vco_table = ctg_vco;
	else if (IS_G4X(dev))
		vco_table = elk_vco;
	else if (IS_CRESTLINE(dev))
		vco_table = cl_vco;
	else if (IS_PINEVIEW(dev))
		vco_table = pnv_vco;
	else if (IS_G33(dev))
		vco_table = blb_vco;
	else
		return 0;

	tmp = I915_READ(IS_MOBILE(dev) ? HPLLVCO_MOBILE : HPLLVCO);

	vco = vco_table[tmp & 0x7];
	if (vco == 0)
		DRM_ERROR("Bad HPLL VCO (HPLLVCO=0x%02x)\n", tmp);
	else
		DRM_DEBUG_KMS("HPLL VCO %u kHz\n", vco);

	return vco;
}

static int gm45_get_display_clock_speed(struct drm_device *dev)
{
	unsigned int cdclk_sel, vco = intel_hpll_vco(dev);
	uint16_t tmp = 0;

	pci_read_config_word(dev->pdev, GCFGC, &tmp);

	cdclk_sel = (tmp >> 12) & 0x1;

	switch (vco) {
	case 2666667:
	case 4000000:
	case 5333333:
		return cdclk_sel ? 333333 : 222222;
	case 3200000:
		return cdclk_sel ? 320000 : 228571;
	default:
		DRM_ERROR("Unable to determine CDCLK. HPLL VCO=%u, CFGC=0x%04x\n", vco, tmp);
		return 222222;
	}
}

static int i965gm_get_display_clock_speed(struct drm_device *dev)
{
	static const uint8_t div_3200[] = { 16, 10,  8 };
	static const uint8_t div_4000[] = { 20, 12, 10 };
	static const uint8_t div_5333[] = { 24, 16, 14 };
	const uint8_t *div_table;
	unsigned int cdclk_sel, vco = intel_hpll_vco(dev);
	uint16_t tmp = 0;

	pci_read_config_word(dev->pdev, GCFGC, &tmp);

	cdclk_sel = ((tmp >> 8) & 0x1f) - 1;

	if (cdclk_sel >= ARRAY_SIZE(div_3200))
		goto fail;

	switch (vco) {
	case 3200000:
		div_table = div_3200;
		break;
	case 4000000:
		div_table = div_4000;
		break;
	case 5333333:
		div_table = div_5333;
		break;
	default:
		goto fail;
	}

	return DIV_ROUND_CLOSEST(vco, div_table[cdclk_sel]);

fail:
	DRM_ERROR("Unable to determine CDCLK. HPLL VCO=%u kHz, CFGC=0x%04x\n", vco, tmp);
	return 200000;
}

static int g33_get_display_clock_speed(struct drm_device *dev)
{
	static const uint8_t div_3200[] = { 12, 10,  8,  7, 5, 16 };
	static const uint8_t div_4000[] = { 14, 12, 10,  8, 6, 20 };
	static const uint8_t div_4800[] = { 20, 14, 12, 10, 8, 24 };
	static const uint8_t div_5333[] = { 20, 16, 12, 12, 8, 28 };
	const uint8_t *div_table;
	unsigned int cdclk_sel, vco = intel_hpll_vco(dev);
	uint16_t tmp = 0;

	pci_read_config_word(dev->pdev, GCFGC, &tmp);

	cdclk_sel = (tmp >> 4) & 0x7;

	if (cdclk_sel >= ARRAY_SIZE(div_3200))
		goto fail;

	switch (vco) {
	case 3200000:
		div_table = div_3200;
		break;
	case 4000000:
		div_table = div_4000;
		break;
	case 4800000:
		div_table = div_4800;
		break;
	case 5333333:
		div_table = div_5333;
		break;
	default:
		goto fail;
	}

	return DIV_ROUND_CLOSEST(vco, div_table[cdclk_sel]);

fail:
	DRM_ERROR("Unable to determine CDCLK. HPLL VCO=%u kHz, CFGC=0x%08x\n", vco, tmp);
	return 190476;
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

static int i9xx_get_refclk(const struct intel_crtc_state *crtc_state,
			   int num_connectors)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int refclk;

	WARN_ON(!crtc_state->base.state);

	if (IS_VALLEYVIEW(dev) || IS_BROXTON(dev)) {
		refclk = 100000;
	} else if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS) &&
	    intel_panel_use_ssc(dev_priv) && num_connectors < 2) {
		refclk = dev_priv->vbt.lvds_ssc_freq;
		DRM_DEBUG_KMS("using SSC reference clock of %d kHz\n", refclk);
	} else if (!IS_GEN2(dev)) {
		refclk = 96000;
	} else {
		refclk = 48000;
	}

	return refclk;
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
				     intel_clock_t *reduced_clock)
{
	struct drm_device *dev = crtc->base.dev;
	u32 fp, fp2 = 0;

	if (IS_PINEVIEW(dev)) {
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
	if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS) &&
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = crtc->pipe;
	enum transcoder transcoder = crtc->config->cpu_transcoder;

	if (INTEL_INFO(dev)->gen >= 5) {
		I915_WRITE(PIPE_DATA_M1(transcoder), TU_SIZE(m_n->tu) | m_n->gmch_m);
		I915_WRITE(PIPE_DATA_N1(transcoder), m_n->gmch_n);
		I915_WRITE(PIPE_LINK_M1(transcoder), m_n->link_m);
		I915_WRITE(PIPE_LINK_N1(transcoder), m_n->link_n);
		/* M2_N2 registers to be set only for gen < 8 (M2_N2 available
		 * for gen < 8) and if DRRS is supported (to make sure the
		 * registers are not unnecessarily accessed).
		 */
		if (m2_n2 && (IS_CHERRYVIEW(dev) || INTEL_INFO(dev)->gen < 8) &&
			crtc->config->has_drrs) {
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
	u32 dpll, dpll_md;

	/*
	 * Enable DPIO clock input. We should never disable the reference
	 * clock for pipe B, since VGA hotplug / manual detection depends
	 * on it.
	 */
	dpll = DPLL_EXT_BUFFER_ENABLE_VLV | DPLL_REF_CLK_ENABLE_VLV |
		DPLL_VGA_MODE_DIS | DPLL_INTEGRATED_REF_CLK_VLV;
	/* We should never disable this, set it here for state tracking */
	if (crtc->pipe == PIPE_B)
		dpll |= DPLL_INTEGRATED_CRI_CLK_VLV;
	dpll |= DPLL_VCO_ENABLE;
	pipe_config->dpll_hw_state.dpll = dpll;

	dpll_md = (pipe_config->pixel_multiplier - 1)
		<< DPLL_MD_UDI_MULTIPLIER_SHIFT;
	pipe_config->dpll_hw_state.dpll_md = dpll_md;
}

static void vlv_prepare_pll(struct intel_crtc *crtc,
			    const struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = crtc->pipe;
	u32 mdiv;
	u32 bestn, bestm1, bestm2, bestp1, bestp2;
	u32 coreclk, reg_val;

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
	    intel_pipe_has_type(crtc, INTEL_OUTPUT_ANALOG) ||
	    intel_pipe_has_type(crtc, INTEL_OUTPUT_HDMI))
		vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW10(pipe),
				 0x009f0003);
	else
		vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW10(pipe),
				 0x00d0000f);

	if (pipe_config->has_dp_encoder) {
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
	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_DISPLAYPORT) ||
	    intel_pipe_has_type(crtc, INTEL_OUTPUT_EDP))
		coreclk |= 0x01000000;
	vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW7(pipe), coreclk);

	vlv_dpio_write(dev_priv, pipe, VLV_PLL_DW11(pipe), 0x87871000);
	mutex_unlock(&dev_priv->sb_lock);
}

static void chv_compute_dpll(struct intel_crtc *crtc,
			     struct intel_crtc_state *pipe_config)
{
	pipe_config->dpll_hw_state.dpll = DPLL_SSC_REF_CLK_CHV |
		DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS |
		DPLL_VCO_ENABLE;
	if (crtc->pipe != PIPE_A)
		pipe_config->dpll_hw_state.dpll |= DPLL_INTEGRATED_CRI_CLK_VLV;

	pipe_config->dpll_hw_state.dpll_md =
		(pipe_config->pixel_multiplier - 1) << DPLL_MD_UDI_MULTIPLIER_SHIFT;
}

static void chv_prepare_pll(struct intel_crtc *crtc,
			    const struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = crtc->pipe;
	i915_reg_t dpll_reg = DPLL(crtc->pipe);
	enum dpio_channel port = vlv_pipe_to_channel(pipe);
	u32 loopfilter, tribuf_calcntr;
	u32 bestn, bestm1, bestm2, bestp1, bestp2, bestm2_frac;
	u32 dpio_val;
	int vco;

	bestn = pipe_config->dpll.n;
	bestm2_frac = pipe_config->dpll.m2 & 0x3fffff;
	bestm1 = pipe_config->dpll.m1;
	bestm2 = pipe_config->dpll.m2 >> 22;
	bestp1 = pipe_config->dpll.p1;
	bestp2 = pipe_config->dpll.p2;
	vco = pipe_config->dpll.vco;
	dpio_val = 0;
	loopfilter = 0;

	/*
	 * Enable Refclk and SSC
	 */
	I915_WRITE(dpll_reg,
		   pipe_config->dpll_hw_state.dpll & ~DPLL_VCO_ENABLE);

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
void vlv_force_pll_on(struct drm_device *dev, enum pipe pipe,
		      const struct dpll *dpll)
{
	struct intel_crtc *crtc =
		to_intel_crtc(intel_get_crtc_for_pipe(dev, pipe));
	struct intel_crtc_state pipe_config = {
		.base.crtc = &crtc->base,
		.pixel_multiplier = 1,
		.dpll = *dpll,
	};

	if (IS_CHERRYVIEW(dev)) {
		chv_compute_dpll(crtc, &pipe_config);
		chv_prepare_pll(crtc, &pipe_config);
		chv_enable_pll(crtc, &pipe_config);
	} else {
		vlv_compute_dpll(crtc, &pipe_config);
		vlv_prepare_pll(crtc, &pipe_config);
		vlv_enable_pll(crtc, &pipe_config);
	}
}

/**
 * vlv_force_pll_off - forcibly disable just the PLL
 * @dev_priv: i915 private structure
 * @pipe: pipe PLL to disable
 *
 * Disable the PLL for @pipe. To be used in cases where we need
 * the PLL enabled even when @pipe is not going to be enabled.
 */
void vlv_force_pll_off(struct drm_device *dev, enum pipe pipe)
{
	if (IS_CHERRYVIEW(dev))
		chv_disable_pll(to_i915(dev), pipe);
	else
		vlv_disable_pll(to_i915(dev), pipe);
}

static void i9xx_compute_dpll(struct intel_crtc *crtc,
			      struct intel_crtc_state *crtc_state,
			      intel_clock_t *reduced_clock,
			      int num_connectors)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpll;
	bool is_sdvo;
	struct dpll *clock = &crtc_state->dpll;

	i9xx_update_pll_dividers(crtc, crtc_state, reduced_clock);

	is_sdvo = intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_SDVO) ||
		intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_HDMI);

	dpll = DPLL_VGA_MODE_DIS;

	if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS))
		dpll |= DPLLB_MODE_LVDS;
	else
		dpll |= DPLLB_MODE_DAC_SERIAL;

	if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev)) {
		dpll |= (crtc_state->pixel_multiplier - 1)
			<< SDVO_MULTIPLIER_SHIFT_HIRES;
	}

	if (is_sdvo)
		dpll |= DPLL_SDVO_HIGH_SPEED;

	if (crtc_state->has_dp_encoder)
		dpll |= DPLL_SDVO_HIGH_SPEED;

	/* compute bitmask from p1 value */
	if (IS_PINEVIEW(dev))
		dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT_PINEVIEW;
	else {
		dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
		if (IS_G4X(dev) && reduced_clock)
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
	if (INTEL_INFO(dev)->gen >= 4)
		dpll |= (6 << PLL_LOAD_PULSE_PHASE_SHIFT);

	if (crtc_state->sdvo_tv_clock)
		dpll |= PLL_REF_INPUT_TVCLKINBC;
	else if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS) &&
		 intel_panel_use_ssc(dev_priv) && num_connectors < 2)
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	dpll |= DPLL_VCO_ENABLE;
	crtc_state->dpll_hw_state.dpll = dpll;

	if (INTEL_INFO(dev)->gen >= 4) {
		u32 dpll_md = (crtc_state->pixel_multiplier - 1)
			<< DPLL_MD_UDI_MULTIPLIER_SHIFT;
		crtc_state->dpll_hw_state.dpll_md = dpll_md;
	}
}

static void i8xx_compute_dpll(struct intel_crtc *crtc,
			      struct intel_crtc_state *crtc_state,
			      intel_clock_t *reduced_clock,
			      int num_connectors)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpll;
	struct dpll *clock = &crtc_state->dpll;

	i9xx_update_pll_dividers(crtc, crtc_state, reduced_clock);

	dpll = DPLL_VGA_MODE_DIS;

	if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS)) {
		dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
	} else {
		if (clock->p1 == 2)
			dpll |= PLL_P1_DIVIDE_BY_TWO;
		else
			dpll |= (clock->p1 - 2) << DPLL_FPA01_P1_POST_DIV_SHIFT;
		if (clock->p2 == 4)
			dpll |= PLL_P2_DIVIDE_BY_4;
	}

	if (!IS_I830(dev) && intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_DVO))
		dpll |= DPLL_DVO_2X_MODE;

	if (intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS) &&
		 intel_panel_use_ssc(dev_priv) && num_connectors < 2)
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	dpll |= DPLL_VCO_ENABLE;
	crtc_state->dpll_hw_state.dpll = dpll;
}

static void intel_set_pipe_timings(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
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

		if (intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_SDVO))
			vsyncshift = (adjusted_mode->crtc_htotal - 1) / 2;
		else
			vsyncshift = adjusted_mode->crtc_hsync_start -
				adjusted_mode->crtc_htotal / 2;
		if (vsyncshift < 0)
			vsyncshift += adjusted_mode->crtc_htotal;
	}

	if (INTEL_INFO(dev)->gen > 3)
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
	if (IS_HASWELL(dev) && cpu_transcoder == TRANSCODER_EDP &&
	    (pipe == PIPE_B || pipe == PIPE_C))
		I915_WRITE(VTOTAL(pipe), I915_READ(VTOTAL(cpu_transcoder)));

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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	mode->flags |= pipe_config->base.adjusted_mode.flags;

	mode->hsync = drm_mode_hsync(mode);
	mode->vrefresh = drm_mode_vrefresh(mode);
	drm_mode_set_name(mode);
}

static void i9xx_set_pipeconf(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t pipeconf;

	pipeconf = 0;

	if ((intel_crtc->pipe == PIPE_A && dev_priv->quirks & QUIRK_PIPEA_FORCE) ||
	    (intel_crtc->pipe == PIPE_B && dev_priv->quirks & QUIRK_PIPEB_FORCE))
		pipeconf |= I915_READ(PIPECONF(intel_crtc->pipe)) & PIPECONF_ENABLE;

	if (intel_crtc->config->double_wide)
		pipeconf |= PIPECONF_DOUBLE_WIDE;

	/* only g4x and later have fancy bpc/dither controls */
	if (IS_G4X(dev) || IS_VALLEYVIEW(dev)) {
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

	if (HAS_PIPE_CXSR(dev)) {
		if (intel_crtc->lowfreq_avail) {
			DRM_DEBUG_KMS("enabling CxSR downclocking\n");
			pipeconf |= PIPECONF_CXSR_DOWNCLOCK;
		} else {
			DRM_DEBUG_KMS("disabling CxSR downclocking\n");
		}
	}

	if (intel_crtc->config->base.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE) {
		if (INTEL_INFO(dev)->gen < 4 ||
		    intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_SDVO))
			pipeconf |= PIPECONF_INTERLACE_W_FIELD_INDICATION;
		else
			pipeconf |= PIPECONF_INTERLACE_W_SYNC_SHIFT;
	} else
		pipeconf |= PIPECONF_PROGRESSIVE;

	if (IS_VALLEYVIEW(dev) && intel_crtc->config->limited_color_range)
		pipeconf |= PIPECONF_COLOR_RANGE_SELECT;

	I915_WRITE(PIPECONF(intel_crtc->pipe), pipeconf);
	POSTING_READ(PIPECONF(intel_crtc->pipe));
}

static int i9xx_crtc_compute_clock(struct intel_crtc *crtc,
				   struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int refclk, num_connectors = 0;
	intel_clock_t clock;
	bool ok;
	const intel_limit_t *limit;
	struct drm_atomic_state *state = crtc_state->base.state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int i;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	if (crtc_state->has_dsi_encoder)
		return 0;

	for_each_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc == &crtc->base)
			num_connectors++;
	}

	if (!crtc_state->clock_set) {
		refclk = i9xx_get_refclk(crtc_state, num_connectors);

		/*
		 * Returns a set of divisors for the desired target clock with
		 * the given refclk, or FALSE.  The returned values represent
		 * the clock equation: reflck * (5 * (m1 + 2) + (m2 + 2)) / (n +
		 * 2) / p1 / p2.
		 */
		limit = intel_limit(crtc_state, refclk);
		ok = dev_priv->display.find_dpll(limit, crtc_state,
						 crtc_state->port_clock,
						 refclk, NULL, &clock);
		if (!ok) {
			DRM_ERROR("Couldn't find PLL settings for mode!\n");
			return -EINVAL;
		}

		/* Compat-code for transition, will disappear. */
		crtc_state->dpll.n = clock.n;
		crtc_state->dpll.m1 = clock.m1;
		crtc_state->dpll.m2 = clock.m2;
		crtc_state->dpll.p1 = clock.p1;
		crtc_state->dpll.p2 = clock.p2;
	}

	if (IS_GEN2(dev)) {
		i8xx_compute_dpll(crtc, crtc_state, NULL,
				  num_connectors);
	} else if (IS_CHERRYVIEW(dev)) {
		chv_compute_dpll(crtc, crtc_state);
	} else if (IS_VALLEYVIEW(dev)) {
		vlv_compute_dpll(crtc, crtc_state);
	} else {
		i9xx_compute_dpll(crtc, crtc_state, NULL,
				  num_connectors);
	}

	return 0;
}

static void i9xx_get_pfit_config(struct intel_crtc *crtc,
				 struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t tmp;

	if (INTEL_INFO(dev)->gen <= 3 && (IS_I830(dev) || !IS_MOBILE(dev)))
		return;

	tmp = I915_READ(PFIT_CONTROL);
	if (!(tmp & PFIT_ENABLE))
		return;

	/* Check whether the pfit is attached to our pipe. */
	if (INTEL_INFO(dev)->gen < 4) {
		if (crtc->pipe != PIPE_B)
			return;
	} else {
		if ((tmp & PFIT_PIPE_MASK) != (crtc->pipe << PFIT_PIPE_SHIFT))
			return;
	}

	pipe_config->gmch_pfit.control = tmp;
	pipe_config->gmch_pfit.pgm_ratios = I915_READ(PFIT_PGM_RATIOS);
	if (INTEL_INFO(dev)->gen < 5)
		pipe_config->gmch_pfit.lvds_border_bits =
			I915_READ(LVDS) & LVDS_BORDER_ENABLE;
}

static void vlv_crtc_clock_get(struct intel_crtc *crtc,
			       struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = pipe_config->cpu_transcoder;
	intel_clock_t clock;
	u32 mdiv;
	int refclk = 100000;

	/* In case of MIPI DPLL will not even be used */
	if (!(pipe_config->dpll_hw_state.dpll & DPLL_VCO_ENABLE))
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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

	if (INTEL_INFO(dev)->gen >= 4) {
		if (val & DISPPLANE_TILED) {
			plane_config->tiling = I915_TILING_X;
			fb->modifier[0] = I915_FORMAT_MOD_X_TILED;
		}
	}

	pixel_format = val & DISPPLANE_PIXFORMAT_MASK;
	fourcc = i9xx_format_to_fourcc(pixel_format);
	fb->pixel_format = fourcc;
	fb->bits_per_pixel = drm_format_plane_cpp(fourcc, 0) * 8;

	if (INTEL_INFO(dev)->gen >= 4) {
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

	aligned_height = intel_fb_align_height(dev, fb->height,
					       fb->pixel_format,
					       fb->modifier[0]);

	plane_config->size = fb->pitches[0] * aligned_height;

	DRM_DEBUG_KMS("pipe/plane %c/%d with fb: size=%dx%d@%d, offset=%x, pitch %d, size 0x%x\n",
		      pipe_name(pipe), plane, fb->width, fb->height,
		      fb->bits_per_pixel, base, fb->pitches[0],
		      plane_config->size);

	plane_config->fb = intel_fb;
}

static void chv_crtc_clock_get(struct intel_crtc *crtc,
			       struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = pipe_config->cpu_transcoder;
	enum dpio_channel port = vlv_pipe_to_channel(pipe);
	intel_clock_t clock;
	u32 cmn_dw13, pll_dw0, pll_dw1, pll_dw2, pll_dw3;
	int refclk = 100000;

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
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t tmp;

	if (!intel_display_power_is_enabled(dev_priv,
					    POWER_DOMAIN_PIPE(crtc->pipe)))
		return false;

	pipe_config->cpu_transcoder = (enum transcoder) crtc->pipe;
	pipe_config->shared_dpll = DPLL_ID_PRIVATE;

	tmp = I915_READ(PIPECONF(crtc->pipe));
	if (!(tmp & PIPECONF_ENABLE))
		return false;

	if (IS_G4X(dev) || IS_VALLEYVIEW(dev)) {
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

	if (IS_VALLEYVIEW(dev) && (tmp & PIPECONF_COLOR_RANGE_SELECT))
		pipe_config->limited_color_range = true;

	if (INTEL_INFO(dev)->gen < 4)
		pipe_config->double_wide = tmp & PIPECONF_DOUBLE_WIDE;

	intel_get_pipe_timings(crtc, pipe_config);

	i9xx_get_pfit_config(crtc, pipe_config);

	if (INTEL_INFO(dev)->gen >= 4) {
		tmp = I915_READ(DPLL_MD(crtc->pipe));
		pipe_config->pixel_multiplier =
			((tmp & DPLL_MD_UDI_MULTIPLIER_MASK)
			 >> DPLL_MD_UDI_MULTIPLIER_SHIFT) + 1;
		pipe_config->dpll_hw_state.dpll_md = tmp;
	} else if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev)) {
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
	if (!IS_VALLEYVIEW(dev)) {
		/*
		 * DPLL_DVO_2X_MODE must be enabled for both DPLLs
		 * on 830. Filter it out here so that we don't
		 * report errors due to that.
		 */
		if (IS_I830(dev))
			pipe_config->dpll_hw_state.dpll &= ~DPLL_DVO_2X_MODE;

		pipe_config->dpll_hw_state.fp0 = I915_READ(FP0(crtc->pipe));
		pipe_config->dpll_hw_state.fp1 = I915_READ(FP1(crtc->pipe));
	} else {
		/* Mask out read-only status bits. */
		pipe_config->dpll_hw_state.dpll &= ~(DPLL_LOCK_VLV |
						     DPLL_PORTC_READY_MASK |
						     DPLL_PORTB_READY_MASK);
	}

	if (IS_CHERRYVIEW(dev))
		chv_crtc_clock_get(crtc, pipe_config);
	else if (IS_VALLEYVIEW(dev))
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

	return true;
}

static void ironlake_init_pch_refclk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *encoder;
	u32 val, final;
	bool has_lvds = false;
	bool has_cpu_edp = false;
	bool has_panel = false;
	bool has_ck505 = false;
	bool can_ssc = false;

	/* We need to take the global config into account */
	for_each_intel_encoder(dev, encoder) {
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

	if (HAS_PCH_IBX(dev)) {
		has_ck505 = dev_priv->vbt.display_clock_mode;
		can_ssc = has_ck505;
	} else {
		has_ck505 = false;
		can_ssc = true;
	}

	DRM_DEBUG_KMS("has_panel %d has_lvds %d has_ck505 %d\n",
		      has_panel, has_lvds, has_ck505);

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
	} else {
		final |= DREF_SSC_SOURCE_DISABLE;
		final |= DREF_CPU_SOURCE_OUTPUT_DISABLE;
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
		DRM_DEBUG_KMS("Disabling SSC entirely\n");

		val &= ~DREF_CPU_SOURCE_OUTPUT_MASK;

		/* Turn off CPU output */
		val |= DREF_CPU_SOURCE_OUTPUT_DISABLE;

		I915_WRITE(PCH_DREF_CONTROL, val);
		POSTING_READ(PCH_DREF_CONTROL);
		udelay(200);

		/* Turn off the SSC source */
		val &= ~DREF_SSC_SOURCE_MASK;
		val |= DREF_SSC_SOURCE_DISABLE;

		/* Turn off SSC1 */
		val &= ~DREF_SSC1_ENABLE;

		I915_WRITE(PCH_DREF_CONTROL, val);
		POSTING_READ(PCH_DREF_CONTROL);
		udelay(200);
	}

	BUG_ON(val != final);
}

static void lpt_reset_fdi_mphy(struct drm_i915_private *dev_priv)
{
	uint32_t tmp;

	tmp = I915_READ(SOUTH_CHICKEN2);
	tmp |= FDI_MPHY_IOSFSB_RESET_CTL;
	I915_WRITE(SOUTH_CHICKEN2, tmp);

	if (wait_for_atomic_us(I915_READ(SOUTH_CHICKEN2) &
			       FDI_MPHY_IOSFSB_RESET_STATUS, 100))
		DRM_ERROR("FDI mPHY reset assert timeout\n");

	tmp = I915_READ(SOUTH_CHICKEN2);
	tmp &= ~FDI_MPHY_IOSFSB_RESET_CTL;
	I915_WRITE(SOUTH_CHICKEN2, tmp);

	if (wait_for_atomic_us((I915_READ(SOUTH_CHICKEN2) &
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
static void lpt_enable_clkout_dp(struct drm_device *dev, bool with_spread,
				 bool with_fdi)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t reg, tmp;

	if (WARN(with_fdi && !with_spread, "FDI requires downspread\n"))
		with_spread = true;
	if (WARN(HAS_PCH_LPT_LP(dev) && with_fdi, "LP PCH doesn't have FDI\n"))
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

	reg = HAS_PCH_LPT_LP(dev) ? SBI_GEN0 : SBI_DBUFF0;
	tmp = intel_sbi_read(dev_priv, reg, SBI_ICLK);
	tmp |= SBI_GEN0_CFG_BUFFENABLE_DISABLE;
	intel_sbi_write(dev_priv, reg, tmp, SBI_ICLK);

	mutex_unlock(&dev_priv->sb_lock);
}

/* Sequence to disable CLKOUT_DP */
static void lpt_disable_clkout_dp(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t reg, tmp;

	mutex_lock(&dev_priv->sb_lock);

	reg = HAS_PCH_LPT_LP(dev) ? SBI_GEN0 : SBI_DBUFF0;
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

static void lpt_init_pch_refclk(struct drm_device *dev)
{
	struct intel_encoder *encoder;
	bool has_vga = false;

	for_each_intel_encoder(dev, encoder) {
		switch (encoder->type) {
		case INTEL_OUTPUT_ANALOG:
			has_vga = true;
			break;
		default:
			break;
		}
	}

	if (has_vga)
		lpt_enable_clkout_dp(dev, true, true);
	else
		lpt_disable_clkout_dp(dev);
}

/*
 * Initialize reference clocks when the driver loads
 */
void intel_init_pch_refclk(struct drm_device *dev)
{
	if (HAS_PCH_IBX(dev) || HAS_PCH_CPT(dev))
		ironlake_init_pch_refclk(dev);
	else if (HAS_PCH_LPT(dev))
		lpt_init_pch_refclk(dev);
}

static int ironlake_get_refclk(struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc_state->base.crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_atomic_state *state = crtc_state->base.state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	struct intel_encoder *encoder;
	int num_connectors = 0, i;
	bool is_lvds = false;

	for_each_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc != crtc_state->base.crtc)
			continue;

		encoder = to_intel_encoder(connector_state->best_encoder);

		switch (encoder->type) {
		case INTEL_OUTPUT_LVDS:
			is_lvds = true;
			break;
		default:
			break;
		}
		num_connectors++;
	}

	if (is_lvds && intel_panel_use_ssc(dev_priv) && num_connectors < 2) {
		DRM_DEBUG_KMS("using SSC reference clock of %d kHz\n",
			      dev_priv->vbt.lvds_ssc_freq);
		return dev_priv->vbt.lvds_ssc_freq;
	}

	return 120000;
}

static void ironlake_set_pipeconf(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = crtc->dev->dev_private;
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

/*
 * Set up the pipe CSC unit.
 *
 * Currently only full range RGB to limited range RGB conversion
 * is supported, but eventually this should handle various
 * RGB<->YCbCr scenarios as well.
 */
static void intel_set_pipe_csc(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	uint16_t coeff = 0x7800; /* 1.0 */

	/*
	 * TODO: Check what kind of values actually come out of the pipe
	 * with these coeff/postoff values and adjust to get the best
	 * accuracy. Perhaps we even need to take the bpc value into
	 * consideration.
	 */

	if (intel_crtc->config->limited_color_range)
		coeff = ((235 - 16) * (1 << 12) / 255) & 0xff8; /* 0.xxx... */

	/*
	 * GY/GU and RY/RU should be the other way around according
	 * to BSpec, but reality doesn't agree. Just set them up in
	 * a way that results in the correct picture.
	 */
	I915_WRITE(PIPE_CSC_COEFF_RY_GY(pipe), coeff << 16);
	I915_WRITE(PIPE_CSC_COEFF_BY(pipe), 0);

	I915_WRITE(PIPE_CSC_COEFF_RU_GU(pipe), coeff);
	I915_WRITE(PIPE_CSC_COEFF_BU(pipe), 0);

	I915_WRITE(PIPE_CSC_COEFF_RV_GV(pipe), 0);
	I915_WRITE(PIPE_CSC_COEFF_BV(pipe), coeff << 16);

	I915_WRITE(PIPE_CSC_PREOFF_HI(pipe), 0);
	I915_WRITE(PIPE_CSC_PREOFF_ME(pipe), 0);
	I915_WRITE(PIPE_CSC_PREOFF_LO(pipe), 0);

	if (INTEL_INFO(dev)->gen > 6) {
		uint16_t postoff = 0;

		if (intel_crtc->config->limited_color_range)
			postoff = (16 * (1 << 12) / 255) & 0x1fff;

		I915_WRITE(PIPE_CSC_POSTOFF_HI(pipe), postoff);
		I915_WRITE(PIPE_CSC_POSTOFF_ME(pipe), postoff);
		I915_WRITE(PIPE_CSC_POSTOFF_LO(pipe), postoff);

		I915_WRITE(PIPE_CSC_MODE(pipe), 0);
	} else {
		uint32_t mode = CSC_MODE_YUV_TO_RGB;

		if (intel_crtc->config->limited_color_range)
			mode |= CSC_BLACK_SCREEN_OFFSET;

		I915_WRITE(PIPE_CSC_MODE(pipe), mode);
	}
}

static void haswell_set_pipeconf(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum pipe pipe = intel_crtc->pipe;
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	uint32_t val;

	val = 0;

	if (IS_HASWELL(dev) && intel_crtc->config->dither)
		val |= (PIPECONF_DITHER_EN | PIPECONF_DITHER_TYPE_SP);

	if (intel_crtc->config->base.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE)
		val |= PIPECONF_INTERLACED_ILK;
	else
		val |= PIPECONF_PROGRESSIVE;

	I915_WRITE(PIPECONF(cpu_transcoder), val);
	POSTING_READ(PIPECONF(cpu_transcoder));

	I915_WRITE(GAMMA_MODE(intel_crtc->pipe), GAMMA_MODE_MODE_8BIT);
	POSTING_READ(GAMMA_MODE(intel_crtc->pipe));

	if (IS_BROADWELL(dev) || INTEL_INFO(dev)->gen >= 9) {
		val = 0;

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

		I915_WRITE(PIPEMISC(pipe), val);
	}
}

static bool ironlake_compute_clocks(struct drm_crtc *crtc,
				    struct intel_crtc_state *crtc_state,
				    intel_clock_t *clock,
				    bool *has_reduced_clock,
				    intel_clock_t *reduced_clock)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int refclk;
	const intel_limit_t *limit;
	bool ret;

	refclk = ironlake_get_refclk(crtc_state);

	/*
	 * Returns a set of divisors for the desired target clock with the given
	 * refclk, or FALSE.  The returned values represent the clock equation:
	 * reflck * (5 * (m1 + 2) + (m2 + 2)) / (n + 2) / p1 / p2.
	 */
	limit = intel_limit(crtc_state, refclk);
	ret = dev_priv->display.find_dpll(limit, crtc_state,
					  crtc_state->port_clock,
					  refclk, NULL, clock);
	if (!ret)
		return false;

	return true;
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

static uint32_t ironlake_compute_dpll(struct intel_crtc *intel_crtc,
				      struct intel_crtc_state *crtc_state,
				      u32 *fp,
				      intel_clock_t *reduced_clock, u32 *fp2)
{
	struct drm_crtc *crtc = &intel_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_atomic_state *state = crtc_state->base.state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	struct intel_encoder *encoder;
	uint32_t dpll;
	int factor, num_connectors = 0, i;
	bool is_lvds = false, is_sdvo = false;

	for_each_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc != crtc_state->base.crtc)
			continue;

		encoder = to_intel_encoder(connector_state->best_encoder);

		switch (encoder->type) {
		case INTEL_OUTPUT_LVDS:
			is_lvds = true;
			break;
		case INTEL_OUTPUT_SDVO:
		case INTEL_OUTPUT_HDMI:
			is_sdvo = true;
			break;
		default:
			break;
		}

		num_connectors++;
	}

	/* Enable autotuning of the PLL clock (if permissible) */
	factor = 21;
	if (is_lvds) {
		if ((intel_panel_use_ssc(dev_priv) &&
		     dev_priv->vbt.lvds_ssc_freq == 100000) ||
		    (HAS_PCH_IBX(dev) && intel_is_dual_link_lvds(dev)))
			factor = 25;
	} else if (crtc_state->sdvo_tv_clock)
		factor = 20;

	if (ironlake_needs_fb_cb_tune(&crtc_state->dpll, factor))
		*fp |= FP_CB_TUNE;

	if (fp2 && (reduced_clock->m < factor * reduced_clock->n))
		*fp2 |= FP_CB_TUNE;

	dpll = 0;

	if (is_lvds)
		dpll |= DPLLB_MODE_LVDS;
	else
		dpll |= DPLLB_MODE_DAC_SERIAL;

	dpll |= (crtc_state->pixel_multiplier - 1)
		<< PLL_REF_SDVO_HDMI_MULTIPLIER_SHIFT;

	if (is_sdvo)
		dpll |= DPLL_SDVO_HIGH_SPEED;
	if (crtc_state->has_dp_encoder)
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

	if (is_lvds && intel_panel_use_ssc(dev_priv) && num_connectors < 2)
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	return dpll | DPLL_VCO_ENABLE;
}

static int ironlake_crtc_compute_clock(struct intel_crtc *crtc,
				       struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	intel_clock_t clock, reduced_clock;
	u32 dpll = 0, fp = 0, fp2 = 0;
	bool ok, has_reduced_clock = false;
	bool is_lvds = false;
	struct intel_shared_dpll *pll;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	is_lvds = intel_pipe_will_have_type(crtc_state, INTEL_OUTPUT_LVDS);

	WARN(!(HAS_PCH_IBX(dev) || HAS_PCH_CPT(dev)),
	     "Unexpected PCH type %d\n", INTEL_PCH_TYPE(dev));

	ok = ironlake_compute_clocks(&crtc->base, crtc_state, &clock,
				     &has_reduced_clock, &reduced_clock);
	if (!ok && !crtc_state->clock_set) {
		DRM_ERROR("Couldn't find PLL settings for mode!\n");
		return -EINVAL;
	}
	/* Compat-code for transition, will disappear. */
	if (!crtc_state->clock_set) {
		crtc_state->dpll.n = clock.n;
		crtc_state->dpll.m1 = clock.m1;
		crtc_state->dpll.m2 = clock.m2;
		crtc_state->dpll.p1 = clock.p1;
		crtc_state->dpll.p2 = clock.p2;
	}

	/* CPU eDP is the only output that doesn't need a PCH PLL of its own. */
	if (crtc_state->has_pch_encoder) {
		fp = i9xx_dpll_compute_fp(&crtc_state->dpll);
		if (has_reduced_clock)
			fp2 = i9xx_dpll_compute_fp(&reduced_clock);

		dpll = ironlake_compute_dpll(crtc, crtc_state,
					     &fp, &reduced_clock,
					     has_reduced_clock ? &fp2 : NULL);

		crtc_state->dpll_hw_state.dpll = dpll;
		crtc_state->dpll_hw_state.fp0 = fp;
		if (has_reduced_clock)
			crtc_state->dpll_hw_state.fp1 = fp2;
		else
			crtc_state->dpll_hw_state.fp1 = fp;

		pll = intel_get_shared_dpll(crtc, crtc_state);
		if (pll == NULL) {
			DRM_DEBUG_DRIVER("failed to find PLL for pipe %c\n",
					 pipe_name(crtc->pipe));
			return -EINVAL;
		}
	}

	if (is_lvds && has_reduced_clock)
		crtc->lowfreq_avail = true;
	else
		crtc->lowfreq_avail = false;

	return 0;
}

static void intel_pch_transcoder_get_m_n(struct intel_crtc *crtc,
					 struct intel_link_m_n *m_n)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe = crtc->pipe;

	if (INTEL_INFO(dev)->gen >= 5) {
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
		if (m2_n2 && INTEL_INFO(dev)->gen < 8 &&
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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

	val = I915_READ(PLANE_CTL(pipe, 0));
	if (!(val & PLANE_CTL_ENABLE))
		goto error;

	pixel_format = val & PLANE_CTL_FORMAT_MASK;
	fourcc = skl_format_to_fourcc(pixel_format,
				      val & PLANE_CTL_ORDER_RGBX,
				      val & PLANE_CTL_ALPHA_MASK);
	fb->pixel_format = fourcc;
	fb->bits_per_pixel = drm_format_plane_cpp(fourcc, 0) * 8;

	tiling = val & PLANE_CTL_TILED_MASK;
	switch (tiling) {
	case PLANE_CTL_TILED_LINEAR:
		fb->modifier[0] = DRM_FORMAT_MOD_NONE;
		break;
	case PLANE_CTL_TILED_X:
		plane_config->tiling = I915_TILING_X;
		fb->modifier[0] = I915_FORMAT_MOD_X_TILED;
		break;
	case PLANE_CTL_TILED_Y:
		fb->modifier[0] = I915_FORMAT_MOD_Y_TILED;
		break;
	case PLANE_CTL_TILED_YF:
		fb->modifier[0] = I915_FORMAT_MOD_Yf_TILED;
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
	stride_mult = intel_fb_stride_alignment(dev, fb->modifier[0],
						fb->pixel_format);
	fb->pitches[0] = (val & 0x3ff) * stride_mult;

	aligned_height = intel_fb_align_height(dev, fb->height,
					       fb->pixel_format,
					       fb->modifier[0]);

	plane_config->size = fb->pitches[0] * aligned_height;

	DRM_DEBUG_KMS("pipe %c with fb: size=%dx%d@%d, offset=%x, pitch %d, size 0x%x\n",
		      pipe_name(pipe), fb->width, fb->height,
		      fb->bits_per_pixel, base, fb->pitches[0],
		      plane_config->size);

	plane_config->fb = intel_fb;
	return;

error:
	kfree(fb);
}

static void ironlake_get_pfit_config(struct intel_crtc *crtc,
				     struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t tmp;

	tmp = I915_READ(PF_CTL(crtc->pipe));

	if (tmp & PF_ENABLE) {
		pipe_config->pch_pfit.enabled = true;
		pipe_config->pch_pfit.pos = I915_READ(PF_WIN_POS(crtc->pipe));
		pipe_config->pch_pfit.size = I915_READ(PF_WIN_SZ(crtc->pipe));

		/* We currently do not free assignements of panel fitters on
		 * ivb/hsw (since we don't use the higher upscaling modes which
		 * differentiates them) so just WARN about this case for now. */
		if (IS_GEN7(dev)) {
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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

	if (INTEL_INFO(dev)->gen >= 4) {
		if (val & DISPPLANE_TILED) {
			plane_config->tiling = I915_TILING_X;
			fb->modifier[0] = I915_FORMAT_MOD_X_TILED;
		}
	}

	pixel_format = val & DISPPLANE_PIXFORMAT_MASK;
	fourcc = i9xx_format_to_fourcc(pixel_format);
	fb->pixel_format = fourcc;
	fb->bits_per_pixel = drm_format_plane_cpp(fourcc, 0) * 8;

	base = I915_READ(DSPSURF(pipe)) & 0xfffff000;
	if (IS_HASWELL(dev) || IS_BROADWELL(dev)) {
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

	aligned_height = intel_fb_align_height(dev, fb->height,
					       fb->pixel_format,
					       fb->modifier[0]);

	plane_config->size = fb->pitches[0] * aligned_height;

	DRM_DEBUG_KMS("pipe %c with fb: size=%dx%d@%d, offset=%x, pitch %d, size 0x%x\n",
		      pipe_name(pipe), fb->width, fb->height,
		      fb->bits_per_pixel, base, fb->pitches[0],
		      plane_config->size);

	plane_config->fb = intel_fb;
}

static bool ironlake_get_pipe_config(struct intel_crtc *crtc,
				     struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t tmp;

	if (!intel_display_power_is_enabled(dev_priv,
					    POWER_DOMAIN_PIPE(crtc->pipe)))
		return false;

	pipe_config->cpu_transcoder = (enum transcoder) crtc->pipe;
	pipe_config->shared_dpll = DPLL_ID_PRIVATE;

	tmp = I915_READ(PIPECONF(crtc->pipe));
	if (!(tmp & PIPECONF_ENABLE))
		return false;

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

		pipe_config->has_pch_encoder = true;

		tmp = I915_READ(FDI_RX_CTL(crtc->pipe));
		pipe_config->fdi_lanes = ((FDI_DP_PORT_WIDTH_MASK & tmp) >>
					  FDI_DP_PORT_WIDTH_SHIFT) + 1;

		ironlake_get_fdi_m_n_config(crtc, pipe_config);

		if (HAS_PCH_IBX(dev_priv->dev)) {
			pipe_config->shared_dpll =
				(enum intel_dpll_id) crtc->pipe;
		} else {
			tmp = I915_READ(PCH_DPLL_SEL);
			if (tmp & TRANS_DPLLB_SEL(crtc->pipe))
				pipe_config->shared_dpll = DPLL_ID_PCH_PLL_B;
			else
				pipe_config->shared_dpll = DPLL_ID_PCH_PLL_A;
		}

		pll = &dev_priv->shared_dplls[pipe_config->shared_dpll];

		WARN_ON(!pll->get_hw_state(dev_priv, pll,
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

	ironlake_get_pfit_config(crtc, pipe_config);

	return true;
}

static void assert_can_disable_lcpll(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct intel_crtc *crtc;

	for_each_intel_crtc(dev, crtc)
		I915_STATE_WARN(crtc->active, "CRTC for pipe %c enabled\n",
		     pipe_name(crtc->pipe));

	I915_STATE_WARN(I915_READ(HSW_PWR_WELL_DRIVER), "Power well on\n");
	I915_STATE_WARN(I915_READ(SPLL_CTL) & SPLL_PLL_ENABLE, "SPLL enabled\n");
	I915_STATE_WARN(I915_READ(WRPLL_CTL(0)) & WRPLL_PLL_ENABLE, "WRPLL1 enabled\n");
	I915_STATE_WARN(I915_READ(WRPLL_CTL(1)) & WRPLL_PLL_ENABLE, "WRPLL2 enabled\n");
	I915_STATE_WARN(I915_READ(PCH_PP_STATUS) & PP_ON, "Panel power on\n");
	I915_STATE_WARN(I915_READ(BLC_PWM_CPU_CTL2) & BLM_PWM_ENABLE,
	     "CPU PWM1 enabled\n");
	if (IS_HASWELL(dev))
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
	struct drm_device *dev = dev_priv->dev;

	if (IS_HASWELL(dev))
		return I915_READ(D_COMP_HSW);
	else
		return I915_READ(D_COMP_BDW);
}

static void hsw_write_dcomp(struct drm_i915_private *dev_priv, uint32_t val)
{
	struct drm_device *dev = dev_priv->dev;

	if (IS_HASWELL(dev)) {
		mutex_lock(&dev_priv->rps.hw_lock);
		if (sandybridge_pcode_write(dev_priv, GEN6_PCODE_WRITE_D_COMP,
					    val))
			DRM_ERROR("Failed to write to D_COMP\n");
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

		if (wait_for_atomic_us(I915_READ(LCPLL_CTL) &
				       LCPLL_CD_SOURCE_FCLK_DONE, 1))
			DRM_ERROR("Switching to FCLK failed\n");

		val = I915_READ(LCPLL_CTL);
	}

	val |= LCPLL_PLL_DISABLE;
	I915_WRITE(LCPLL_CTL, val);
	POSTING_READ(LCPLL_CTL);

	if (wait_for((I915_READ(LCPLL_CTL) & LCPLL_PLL_LOCK) == 0, 1))
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

	if (wait_for(I915_READ(LCPLL_CTL) & LCPLL_PLL_LOCK, 5))
		DRM_ERROR("LCPLL not locked yet\n");

	if (val & LCPLL_CD_SOURCE_FCLK) {
		val = I915_READ(LCPLL_CTL);
		val &= ~LCPLL_CD_SOURCE_FCLK;
		I915_WRITE(LCPLL_CTL, val);

		if (wait_for_atomic_us((I915_READ(LCPLL_CTL) &
					LCPLL_CD_SOURCE_FCLK_DONE) == 0, 1))
			DRM_ERROR("Switching back to LCPLL failed\n");
	}

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
	intel_update_cdclk(dev_priv->dev);
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
	struct drm_device *dev = dev_priv->dev;
	uint32_t val;

	DRM_DEBUG_KMS("Enabling package C8+\n");

	if (HAS_PCH_LPT_LP(dev)) {
		val = I915_READ(SOUTH_DSPCLK_GATE_D);
		val &= ~PCH_LP_PARTITION_LEVEL_DISABLE;
		I915_WRITE(SOUTH_DSPCLK_GATE_D, val);
	}

	lpt_disable_clkout_dp(dev);
	hsw_disable_lcpll(dev_priv, true, true);
}

void hsw_disable_pc8(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	uint32_t val;

	DRM_DEBUG_KMS("Disabling package C8+\n");

	hsw_restore_lcpll(dev_priv);
	lpt_init_pch_refclk(dev);

	if (HAS_PCH_LPT_LP(dev)) {
		val = I915_READ(SOUTH_DSPCLK_GATE_D);
		val |= PCH_LP_PARTITION_LEVEL_DISABLE;
		I915_WRITE(SOUTH_DSPCLK_GATE_D, val);
	}

	intel_prepare_ddi(dev);
}

static void broxton_modeset_commit_cdclk(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;
	unsigned int req_cdclk = to_intel_atomic_state(old_state)->cdclk;

	broxton_set_cdclk(dev, req_cdclk);
}

/* compute the max rate for new configuration */
static int ilk_max_pixel_rate(struct drm_atomic_state *state)
{
	struct intel_crtc *intel_crtc;
	struct intel_crtc_state *crtc_state;
	int max_pixel_rate = 0;

	for_each_intel_crtc(state->dev, intel_crtc) {
		int pixel_rate;

		crtc_state = intel_atomic_get_crtc_state(state, intel_crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		if (!crtc_state->base.enable)
			continue;

		pixel_rate = ilk_pipe_pixel_rate(crtc_state);

		/* pixel rate mustn't exceed 95% of cdclk with IPS on BDW */
		if (IS_BROADWELL(state->dev) && crtc_state->ips_enabled)
			pixel_rate = DIV_ROUND_UP(pixel_rate * 100, 95);

		max_pixel_rate = max(max_pixel_rate, pixel_rate);
	}

	return max_pixel_rate;
}

static void broadwell_set_cdclk(struct drm_device *dev, int cdclk)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t val, data;
	int ret;

	if (WARN((I915_READ(LCPLL_CTL) &
		  (LCPLL_PLL_DISABLE | LCPLL_PLL_LOCK |
		   LCPLL_CD_CLOCK_DISABLE | LCPLL_ROOT_CD_CLOCK_DISABLE |
		   LCPLL_CD2X_CLOCK_DISABLE | LCPLL_POWER_DOWN_ALLOW |
		   LCPLL_CD_SOURCE_FCLK)) != LCPLL_PLL_LOCK,
		 "trying to change cdclk frequency with cdclk not enabled\n"))
		return;

	mutex_lock(&dev_priv->rps.hw_lock);
	ret = sandybridge_pcode_write(dev_priv,
				      BDW_PCODE_DISPLAY_FREQ_CHANGE_REQ, 0x0);
	mutex_unlock(&dev_priv->rps.hw_lock);
	if (ret) {
		DRM_ERROR("failed to inform pcode about cdclk change\n");
		return;
	}

	val = I915_READ(LCPLL_CTL);
	val |= LCPLL_CD_SOURCE_FCLK;
	I915_WRITE(LCPLL_CTL, val);

	if (wait_for_atomic_us(I915_READ(LCPLL_CTL) &
			       LCPLL_CD_SOURCE_FCLK_DONE, 1))
		DRM_ERROR("Switching to FCLK failed\n");

	val = I915_READ(LCPLL_CTL);
	val &= ~LCPLL_CLK_FREQ_MASK;

	switch (cdclk) {
	case 450000:
		val |= LCPLL_CLK_FREQ_450;
		data = 0;
		break;
	case 540000:
		val |= LCPLL_CLK_FREQ_54O_BDW;
		data = 1;
		break;
	case 337500:
		val |= LCPLL_CLK_FREQ_337_5_BDW;
		data = 2;
		break;
	case 675000:
		val |= LCPLL_CLK_FREQ_675_BDW;
		data = 3;
		break;
	default:
		WARN(1, "invalid cdclk frequency\n");
		return;
	}

	I915_WRITE(LCPLL_CTL, val);

	val = I915_READ(LCPLL_CTL);
	val &= ~LCPLL_CD_SOURCE_FCLK;
	I915_WRITE(LCPLL_CTL, val);

	if (wait_for_atomic_us((I915_READ(LCPLL_CTL) &
				LCPLL_CD_SOURCE_FCLK_DONE) == 0, 1))
		DRM_ERROR("Switching back to LCPLL failed\n");

	mutex_lock(&dev_priv->rps.hw_lock);
	sandybridge_pcode_write(dev_priv, HSW_PCODE_DE_WRITE_FREQ_REQ, data);
	mutex_unlock(&dev_priv->rps.hw_lock);

	intel_update_cdclk(dev);

	WARN(cdclk != dev_priv->cdclk_freq,
	     "cdclk requested %d kHz but got %d kHz\n",
	     cdclk, dev_priv->cdclk_freq);
}

static int broadwell_modeset_calc_cdclk(struct drm_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->dev);
	int max_pixclk = ilk_max_pixel_rate(state);
	int cdclk;

	/*
	 * FIXME should also account for plane ratio
	 * once 64bpp pixel formats are supported.
	 */
	if (max_pixclk > 540000)
		cdclk = 675000;
	else if (max_pixclk > 450000)
		cdclk = 540000;
	else if (max_pixclk > 337500)
		cdclk = 450000;
	else
		cdclk = 337500;

	if (cdclk > dev_priv->max_cdclk_freq) {
		DRM_DEBUG_KMS("requested cdclk (%d kHz) exceeds max (%d kHz)\n",
			      cdclk, dev_priv->max_cdclk_freq);
		return -EINVAL;
	}

	to_intel_atomic_state(state)->cdclk = cdclk;

	return 0;
}

static void broadwell_modeset_commit_cdclk(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;
	unsigned int req_cdclk = to_intel_atomic_state(old_state)->cdclk;

	broadwell_set_cdclk(dev, req_cdclk);
}

static int haswell_crtc_compute_clock(struct intel_crtc *crtc,
				      struct intel_crtc_state *crtc_state)
{
	if (!intel_ddi_pll_select(crtc, crtc_state))
		return -EINVAL;

	crtc->lowfreq_avail = false;

	return 0;
}

static void bxt_get_ddi_pll(struct drm_i915_private *dev_priv,
				enum port port,
				struct intel_crtc_state *pipe_config)
{
	switch (port) {
	case PORT_A:
		pipe_config->ddi_pll_sel = SKL_DPLL0;
		pipe_config->shared_dpll = DPLL_ID_SKL_DPLL1;
		break;
	case PORT_B:
		pipe_config->ddi_pll_sel = SKL_DPLL1;
		pipe_config->shared_dpll = DPLL_ID_SKL_DPLL2;
		break;
	case PORT_C:
		pipe_config->ddi_pll_sel = SKL_DPLL2;
		pipe_config->shared_dpll = DPLL_ID_SKL_DPLL3;
		break;
	default:
		DRM_ERROR("Incorrect port type\n");
	}
}

static void skylake_get_ddi_pll(struct drm_i915_private *dev_priv,
				enum port port,
				struct intel_crtc_state *pipe_config)
{
	u32 temp, dpll_ctl1;

	temp = I915_READ(DPLL_CTRL2) & DPLL_CTRL2_DDI_CLK_SEL_MASK(port);
	pipe_config->ddi_pll_sel = temp >> (port * 3 + 1);

	switch (pipe_config->ddi_pll_sel) {
	case SKL_DPLL0:
		/*
		 * On SKL the eDP DPLL (DPLL0 as we don't use SSC) is not part
		 * of the shared DPLL framework and thus needs to be read out
		 * separately
		 */
		dpll_ctl1 = I915_READ(DPLL_CTRL1);
		pipe_config->dpll_hw_state.ctrl1 = dpll_ctl1 & 0x3f;
		break;
	case SKL_DPLL1:
		pipe_config->shared_dpll = DPLL_ID_SKL_DPLL1;
		break;
	case SKL_DPLL2:
		pipe_config->shared_dpll = DPLL_ID_SKL_DPLL2;
		break;
	case SKL_DPLL3:
		pipe_config->shared_dpll = DPLL_ID_SKL_DPLL3;
		break;
	}
}

static void haswell_get_ddi_pll(struct drm_i915_private *dev_priv,
				enum port port,
				struct intel_crtc_state *pipe_config)
{
	pipe_config->ddi_pll_sel = I915_READ(PORT_CLK_SEL(port));

	switch (pipe_config->ddi_pll_sel) {
	case PORT_CLK_SEL_WRPLL1:
		pipe_config->shared_dpll = DPLL_ID_WRPLL1;
		break;
	case PORT_CLK_SEL_WRPLL2:
		pipe_config->shared_dpll = DPLL_ID_WRPLL2;
		break;
	case PORT_CLK_SEL_SPLL:
		pipe_config->shared_dpll = DPLL_ID_SPLL;
		break;
	}
}

static void haswell_get_ddi_port_state(struct intel_crtc *crtc,
				       struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_shared_dpll *pll;
	enum port port;
	uint32_t tmp;

	tmp = I915_READ(TRANS_DDI_FUNC_CTL(pipe_config->cpu_transcoder));

	port = (tmp & TRANS_DDI_PORT_MASK) >> TRANS_DDI_PORT_SHIFT;

	if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev))
		skylake_get_ddi_pll(dev_priv, port, pipe_config);
	else if (IS_BROXTON(dev))
		bxt_get_ddi_pll(dev_priv, port, pipe_config);
	else
		haswell_get_ddi_pll(dev_priv, port, pipe_config);

	if (pipe_config->shared_dpll >= 0) {
		pll = &dev_priv->shared_dplls[pipe_config->shared_dpll];

		WARN_ON(!pll->get_hw_state(dev_priv, pll,
					   &pipe_config->dpll_hw_state));
	}

	/*
	 * Haswell has only FDI/PCH transcoder A. It is which is connected to
	 * DDI E. So just check whether this pipe is wired to DDI E and whether
	 * the PCH transcoder is on.
	 */
	if (INTEL_INFO(dev)->gen < 9 &&
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
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum intel_display_power_domain pfit_domain;
	uint32_t tmp;

	if (!intel_display_power_is_enabled(dev_priv,
					 POWER_DOMAIN_PIPE(crtc->pipe)))
		return false;

	pipe_config->cpu_transcoder = (enum transcoder) crtc->pipe;
	pipe_config->shared_dpll = DPLL_ID_PRIVATE;

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

	if (!intel_display_power_is_enabled(dev_priv,
			POWER_DOMAIN_TRANSCODER(pipe_config->cpu_transcoder)))
		return false;

	tmp = I915_READ(PIPECONF(pipe_config->cpu_transcoder));
	if (!(tmp & PIPECONF_ENABLE))
		return false;

	haswell_get_ddi_port_state(crtc, pipe_config);

	intel_get_pipe_timings(crtc, pipe_config);

	if (INTEL_INFO(dev)->gen >= 9) {
		skl_init_scalers(dev, crtc, pipe_config);
	}

	pfit_domain = POWER_DOMAIN_PIPE_PANEL_FITTER(crtc->pipe);

	if (INTEL_INFO(dev)->gen >= 9) {
		pipe_config->scaler_state.scaler_id = -1;
		pipe_config->scaler_state.scaler_users &= ~(1 << SKL_CRTC_INDEX);
	}

	if (intel_display_power_is_enabled(dev_priv, pfit_domain)) {
		if (INTEL_INFO(dev)->gen >= 9)
			skylake_get_pfit_config(crtc, pipe_config);
		else
			ironlake_get_pfit_config(crtc, pipe_config);
	}

	if (IS_HASWELL(dev))
		pipe_config->ips_enabled = hsw_crtc_supports_ips(crtc) &&
			(I915_READ(IPS_CTL) & IPS_ENABLE);

	if (pipe_config->cpu_transcoder != TRANSCODER_EDP) {
		pipe_config->pixel_multiplier =
			I915_READ(PIPE_MULT(pipe_config->cpu_transcoder)) + 1;
	} else {
		pipe_config->pixel_multiplier = 1;
	}

	return true;
}

static void i845_update_cursor(struct drm_crtc *crtc, u32 base)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	uint32_t cntl = 0, size = 0;

	if (base) {
		unsigned int width = intel_crtc->base.cursor->state->crtc_w;
		unsigned int height = intel_crtc->base.cursor->state->crtc_h;
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

static void i9xx_update_cursor(struct drm_crtc *crtc, u32 base)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	uint32_t cntl;

	cntl = 0;
	if (base) {
		cntl = MCURSOR_GAMMA_ENABLE;
		switch (intel_crtc->base.cursor->state->crtc_w) {
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
				MISSING_CASE(intel_crtc->base.cursor->state->crtc_w);
				return;
		}
		cntl |= pipe << 28; /* Connect to correct pipe */

		if (HAS_DDI(dev))
			cntl |= CURSOR_PIPE_CSC_ENABLE;
	}

	if (crtc->cursor->state->rotation == BIT(DRM_ROTATE_180))
		cntl |= CURSOR_ROTATE_180;

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
				     bool on)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	struct drm_plane_state *cursor_state = crtc->cursor->state;
	int x = cursor_state->crtc_x;
	int y = cursor_state->crtc_y;
	u32 base = 0, pos = 0;

	if (on)
		base = intel_crtc->cursor_addr;

	if (x >= intel_crtc->config->pipe_src_w)
		base = 0;

	if (y >= intel_crtc->config->pipe_src_h)
		base = 0;

	if (x < 0) {
		if (x + cursor_state->crtc_w <= 0)
			base = 0;

		pos |= CURSOR_POS_SIGN << CURSOR_X_SHIFT;
		x = -x;
	}
	pos |= x << CURSOR_X_SHIFT;

	if (y < 0) {
		if (y + cursor_state->crtc_h <= 0)
			base = 0;

		pos |= CURSOR_POS_SIGN << CURSOR_Y_SHIFT;
		y = -y;
	}
	pos |= y << CURSOR_Y_SHIFT;

	if (base == 0 && intel_crtc->cursor_base == 0)
		return;

	I915_WRITE(CURPOS(pipe), pos);

	/* ILK+ do this automagically */
	if (HAS_GMCH_DISPLAY(dev) &&
	    crtc->cursor->state->rotation == BIT(DRM_ROTATE_180)) {
		base += (cursor_state->crtc_h *
			 cursor_state->crtc_w - 1) * 4;
	}

	if (IS_845G(dev) || IS_I865G(dev))
		i845_update_cursor(crtc, base);
	else
		i9xx_update_cursor(crtc, base);
}

static bool cursor_size_ok(struct drm_device *dev,
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
	if (IS_845G(dev) || IS_I865G(dev)) {
		if ((width & 63) != 0)
			return false;

		if (width > (IS_845G(dev) ? 64 : 512))
			return false;

		if (height > 1023)
			return false;
	} else {
		switch (width | height) {
		case 256:
		case 128:
			if (IS_GEN2(dev))
				return false;
		case 64:
			break;
		default:
			return false;
		}
	}

	return true;
}

static void intel_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				 u16 *blue, uint32_t start, uint32_t size)
{
	int end = (start + size > 256) ? 256 : start + size, i;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	for (i = start; i < end; i++) {
		intel_crtc->lut_r[i] = red[i] >> 8;
		intel_crtc->lut_g[i] = green[i] >> 8;
		intel_crtc->lut_b[i] = blue[i] >> 8;
	}

	intel_crtc_load_lut(crtc);
}

/* VESA 640x480x72Hz mode to set on the pipe */
static struct drm_display_mode load_detect_mode = {
	DRM_MODE("640x480", DRM_MODE_TYPE_DEFAULT, 31500, 640, 664,
		 704, 832, 0, 480, 489, 491, 520, 0, DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
};

struct drm_framebuffer *
__intel_framebuffer_create(struct drm_device *dev,
			   struct drm_mode_fb_cmd2 *mode_cmd,
			   struct drm_i915_gem_object *obj)
{
	struct intel_framebuffer *intel_fb;
	int ret;

	intel_fb = kzalloc(sizeof(*intel_fb), GFP_KERNEL);
	if (!intel_fb)
		return ERR_PTR(-ENOMEM);

	ret = intel_framebuffer_init(dev, intel_fb, mode_cmd, obj);
	if (ret)
		goto err;

	return &intel_fb->base;

err:
	kfree(intel_fb);
	return ERR_PTR(ret);
}

static struct drm_framebuffer *
intel_framebuffer_create(struct drm_device *dev,
			 struct drm_mode_fb_cmd2 *mode_cmd,
			 struct drm_i915_gem_object *obj)
{
	struct drm_framebuffer *fb;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ERR_PTR(ret);
	fb = __intel_framebuffer_create(dev, mode_cmd, obj);
	mutex_unlock(&dev->struct_mutex);

	return fb;
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

	obj = i915_gem_alloc_object(dev,
				    intel_framebuffer_size_for_mode(mode, bpp));
	if (obj == NULL)
		return ERR_PTR(-ENOMEM);

	mode_cmd.width = mode->hdisplay;
	mode_cmd.height = mode->vdisplay;
	mode_cmd.pitches[0] = intel_framebuffer_pitch_for_width(mode_cmd.width,
								bpp);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(bpp, depth);

	fb = intel_framebuffer_create(dev, &mode_cmd, obj);
	if (IS_ERR(fb))
		drm_gem_object_unreference_unlocked(&obj->base);

	return fb;
}

static struct drm_framebuffer *
mode_fits_in_fbdev(struct drm_device *dev,
		   struct drm_display_mode *mode)
{
#ifdef CONFIG_DRM_FBDEV_EMULATION
	struct drm_i915_private *dev_priv = dev->dev_private;
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
							       fb->bits_per_pixel))
		return NULL;

	if (obj->base.size < mode->vdisplay * fb->pitches[0])
		return NULL;

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
		drm_crtc_get_hv_timing(mode, &hdisplay, &vdisplay);
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
	struct drm_framebuffer *fb;
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_atomic_state *state = NULL;
	struct drm_connector_state *connector_state;
	struct intel_crtc_state *crtc_state;
	int ret, i = -1;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
		      connector->base.id, connector->name,
		      encoder->base.id, encoder->name);

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
	if (encoder->crtc) {
		crtc = encoder->crtc;

		ret = drm_modeset_lock(&crtc->mutex, ctx);
		if (ret)
			goto fail;
		ret = drm_modeset_lock(&crtc->primary->mutex, ctx);
		if (ret)
			goto fail;

		old->dpms_mode = connector->dpms;
		old->load_detect_temp = false;

		/* Make sure the crtc and connector are running */
		if (connector->dpms != DRM_MODE_DPMS_ON)
			connector->funcs->dpms(connector, DRM_MODE_DPMS_ON);

		return true;
	}

	/* Find an unused one (if possible) */
	for_each_crtc(dev, possible_crtc) {
		i++;
		if (!(encoder->possible_crtcs & (1 << i)))
			continue;
		if (possible_crtc->state->enable)
			continue;

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

	ret = drm_modeset_lock(&crtc->mutex, ctx);
	if (ret)
		goto fail;
	ret = drm_modeset_lock(&crtc->primary->mutex, ctx);
	if (ret)
		goto fail;

	intel_crtc = to_intel_crtc(crtc);
	old->dpms_mode = connector->dpms;
	old->load_detect_temp = true;
	old->release_fb = NULL;

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return false;

	state->acquire_ctx = ctx;

	connector_state = drm_atomic_get_connector_state(state, connector);
	if (IS_ERR(connector_state)) {
		ret = PTR_ERR(connector_state);
		goto fail;
	}

	connector_state->crtc = crtc;
	connector_state->best_encoder = &intel_encoder->base;

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
		old->release_fb = fb;
	} else
		DRM_DEBUG_KMS("reusing fbdev for load-detection framebuffer\n");
	if (IS_ERR(fb)) {
		DRM_DEBUG_KMS("failed to allocate framebuffer for load-detection\n");
		goto fail;
	}

	ret = intel_modeset_setup_plane_state(state, crtc, mode, fb, 0, 0);
	if (ret)
		goto fail;

	drm_mode_copy(&crtc_state->base.mode, mode);

	if (drm_atomic_commit(state)) {
		DRM_DEBUG_KMS("failed to set mode on load-detect pipe\n");
		if (old->release_fb)
			old->release_fb->funcs->destroy(old->release_fb);
		goto fail;
	}
	crtc->primary->crtc = crtc;

	/* let the connector get through one full cycle before testing */
	intel_wait_for_vblank(dev, intel_crtc->pipe);
	return true;

fail:
	drm_atomic_state_free(state);
	state = NULL;

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
	struct drm_device *dev = connector->dev;
	struct intel_encoder *intel_encoder =
		intel_attached_encoder(connector);
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_crtc *crtc = encoder->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_atomic_state *state;
	struct drm_connector_state *connector_state;
	struct intel_crtc_state *crtc_state;
	int ret;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
		      connector->base.id, connector->name,
		      encoder->base.id, encoder->name);

	if (old->load_detect_temp) {
		state = drm_atomic_state_alloc(dev);
		if (!state)
			goto fail;

		state->acquire_ctx = ctx;

		connector_state = drm_atomic_get_connector_state(state, connector);
		if (IS_ERR(connector_state))
			goto fail;

		crtc_state = intel_atomic_get_crtc_state(state, intel_crtc);
		if (IS_ERR(crtc_state))
			goto fail;

		connector_state->best_encoder = NULL;
		connector_state->crtc = NULL;

		crtc_state->base.enable = crtc_state->base.active = false;

		ret = intel_modeset_setup_plane_state(state, crtc, NULL, NULL,
						      0, 0);
		if (ret)
			goto fail;

		ret = drm_atomic_commit(state);
		if (ret)
			goto fail;

		if (old->release_fb) {
			drm_framebuffer_unregister_private(old->release_fb);
			drm_framebuffer_unreference(old->release_fb);
		}

		return;
	}

	/* Switch crtc and encoder back off if necessary */
	if (old->dpms_mode != DRM_MODE_DPMS_ON)
		connector->funcs->dpms(connector, old->dpms_mode);

	return;
fail:
	DRM_DEBUG_KMS("Couldn't release load detect pipe.\n");
	drm_atomic_state_free(state);
}

static int i9xx_pll_refclk(struct drm_device *dev,
			   const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpll = pipe_config->dpll_hw_state.dpll;

	if ((dpll & PLL_REF_INPUT_MASK) == PLLB_REF_INPUT_SPREADSPECTRUMIN)
		return dev_priv->vbt.lvds_ssc_freq;
	else if (HAS_PCH_SPLIT(dev))
		return 120000;
	else if (!IS_GEN2(dev))
		return 96000;
	else
		return 48000;
}

/* Returns the clock of the currently programmed mode of the given pipe. */
static void i9xx_crtc_clock_get(struct intel_crtc *crtc,
				struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = pipe_config->cpu_transcoder;
	u32 dpll = pipe_config->dpll_hw_state.dpll;
	u32 fp;
	intel_clock_t clock;
	int port_clock;
	int refclk = i9xx_pll_refclk(dev, pipe_config);

	if ((dpll & DISPLAY_RATE_SELECT_FPA1) == 0)
		fp = pipe_config->dpll_hw_state.fp0;
	else
		fp = pipe_config->dpll_hw_state.fp1;

	clock.m1 = (fp & FP_M1_DIV_MASK) >> FP_M1_DIV_SHIFT;
	if (IS_PINEVIEW(dev)) {
		clock.n = ffs((fp & FP_N_PINEVIEW_DIV_MASK) >> FP_N_DIV_SHIFT) - 1;
		clock.m2 = (fp & FP_M2_PINEVIEW_DIV_MASK) >> FP_M2_DIV_SHIFT;
	} else {
		clock.n = (fp & FP_N_DIV_MASK) >> FP_N_DIV_SHIFT;
		clock.m2 = (fp & FP_M2_DIV_MASK) >> FP_M2_DIV_SHIFT;
	}

	if (!IS_GEN2(dev)) {
		if (IS_PINEVIEW(dev))
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

		if (IS_PINEVIEW(dev))
			port_clock = pnv_calc_dpll_params(refclk, &clock);
		else
			port_clock = i9xx_calc_dpll_params(refclk, &clock);
	} else {
		u32 lvds = IS_I830(dev) ? 0 : I915_READ(LVDS);
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
	struct drm_device *dev = crtc->base.dev;

	/* read out port_clock from the DPLL */
	i9xx_crtc_clock_get(crtc, pipe_config);

	/*
	 * This value does not include pixel_multiplier.
	 * We will check that port_clock and adjusted_mode.crtc_clock
	 * agree once we know their relationship in the encoder's
	 * get_config() function.
	 */
	pipe_config->base.adjusted_mode.crtc_clock =
		intel_dotclock_calculate(intel_fdi_link_freq(dev) * 10000,
					 &pipe_config->fdi_m_n);
}

/** Returns the currently programmed mode of the given pipe. */
struct drm_display_mode *intel_crtc_mode_get(struct drm_device *dev,
					     struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	struct drm_display_mode *mode;
	struct intel_crtc_state pipe_config;
	int htot = I915_READ(HTOTAL(cpu_transcoder));
	int hsync = I915_READ(HSYNC(cpu_transcoder));
	int vtot = I915_READ(VTOTAL(cpu_transcoder));
	int vsync = I915_READ(VSYNC(cpu_transcoder));
	enum pipe pipe = intel_crtc->pipe;

	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode)
		return NULL;

	/*
	 * Construct a pipe_config sufficient for getting the clock info
	 * back out of crtc_clock_get.
	 *
	 * Note, if LVDS ever uses a non-1 pixel multiplier, we'll need
	 * to use a real value here instead.
	 */
	pipe_config.cpu_transcoder = (enum transcoder) pipe;
	pipe_config.pixel_multiplier = 1;
	pipe_config.dpll_hw_state.dpll = I915_READ(DPLL(pipe));
	pipe_config.dpll_hw_state.fp0 = I915_READ(FP0(pipe));
	pipe_config.dpll_hw_state.fp1 = I915_READ(FP1(pipe));
	i9xx_crtc_clock_get(intel_crtc, &pipe_config);

	mode->clock = pipe_config.port_clock / pipe_config.pixel_multiplier;
	mode->hdisplay = (htot & 0xffff) + 1;
	mode->htotal = ((htot & 0xffff0000) >> 16) + 1;
	mode->hsync_start = (hsync & 0xffff) + 1;
	mode->hsync_end = ((hsync & 0xffff0000) >> 16) + 1;
	mode->vdisplay = (vtot & 0xffff) + 1;
	mode->vtotal = ((vtot & 0xffff0000) >> 16) + 1;
	mode->vsync_start = (vsync & 0xffff) + 1;
	mode->vsync_end = ((vsync & 0xffff0000) >> 16) + 1;

	drm_mode_set_name(mode);

	return mode;
}

void intel_mark_busy(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->mm.busy)
		return;

	intel_runtime_pm_get(dev_priv);
	i915_update_gfx_val(dev_priv);
	if (INTEL_INFO(dev)->gen >= 6)
		gen6_rps_busy(dev_priv);
	dev_priv->mm.busy = true;
}

void intel_mark_idle(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv->mm.busy)
		return;

	dev_priv->mm.busy = false;

	if (INTEL_INFO(dev)->gen >= 6)
		gen6_rps_idle(dev->dev_private);

	intel_runtime_pm_put(dev_priv);
}

static void intel_crtc_destroy(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct intel_unpin_work *work;

	spin_lock_irq(&dev->event_lock);
	work = intel_crtc->unpin_work;
	intel_crtc->unpin_work = NULL;
	spin_unlock_irq(&dev->event_lock);

	if (work) {
		cancel_work_sync(&work->work);
		kfree(work);
	}

	drm_crtc_cleanup(crtc);

	kfree(intel_crtc);
}

static void intel_unpin_work_fn(struct work_struct *__work)
{
	struct intel_unpin_work *work =
		container_of(__work, struct intel_unpin_work, work);
	struct intel_crtc *crtc = to_intel_crtc(work->crtc);
	struct drm_device *dev = crtc->base.dev;
	struct drm_plane *primary = crtc->base.primary;

	mutex_lock(&dev->struct_mutex);
	intel_unpin_fb_obj(work->old_fb, primary->state);
	drm_gem_object_unreference(&work->pending_flip_obj->base);

	if (work->flip_queued_req)
		i915_gem_request_assign(&work->flip_queued_req, NULL);
	mutex_unlock(&dev->struct_mutex);

	intel_frontbuffer_flip_complete(dev, to_intel_plane(primary)->frontbuffer_bit);
	drm_framebuffer_unreference(work->old_fb);

	BUG_ON(atomic_read(&crtc->unpin_work_count) == 0);
	atomic_dec(&crtc->unpin_work_count);

	kfree(work);
}

static void do_intel_finish_page_flip(struct drm_device *dev,
				      struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_unpin_work *work;
	unsigned long flags;

	/* Ignore early vblank irqs */
	if (intel_crtc == NULL)
		return;

	/*
	 * This is called both by irq handlers and the reset code (to complete
	 * lost pageflips) so needs the full irqsave spinlocks.
	 */
	spin_lock_irqsave(&dev->event_lock, flags);
	work = intel_crtc->unpin_work;

	/* Ensure we don't miss a work->pending update ... */
	smp_rmb();

	if (work == NULL || atomic_read(&work->pending) < INTEL_FLIP_COMPLETE) {
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return;
	}

	page_flip_completed(intel_crtc);

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

void intel_finish_page_flip(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];

	do_intel_finish_page_flip(dev, crtc);
}

void intel_finish_page_flip_plane(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dev_priv->plane_to_crtc_mapping[plane];

	do_intel_finish_page_flip(dev, crtc);
}

/* Is 'a' after or equal to 'b'? */
static bool g4x_flip_count_after_eq(u32 a, u32 b)
{
	return !((a - b) & 0x80000000);
}

static bool page_flip_finished(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (i915_reset_in_progress(&dev_priv->gpu_error) ||
	    crtc->reset_counter != atomic_read(&dev_priv->gpu_error.reset_counter))
		return true;

	/*
	 * The relevant registers doen't exist on pre-ctg.
	 * As the flip done interrupt doesn't trigger for mmio
	 * flips on gmch platforms, a flip count check isn't
	 * really needed there. But since ctg has the registers,
	 * include it in the check anyway.
	 */
	if (INTEL_INFO(dev)->gen < 5 && !IS_G4X(dev))
		return true;

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
		crtc->unpin_work->gtt_offset &&
		g4x_flip_count_after_eq(I915_READ(PIPE_FLIPCOUNT_G4X(crtc->pipe)),
				    crtc->unpin_work->flip_count);
}

void intel_prepare_page_flip(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc =
		to_intel_crtc(dev_priv->plane_to_crtc_mapping[plane]);
	unsigned long flags;


	/*
	 * This is called both by irq handlers and the reset code (to complete
	 * lost pageflips) so needs the full irqsave spinlocks.
	 *
	 * NB: An MMIO update of the plane base pointer will also
	 * generate a page-flip completion irq, i.e. every modeset
	 * is also accompanied by a spurious intel_prepare_page_flip().
	 */
	spin_lock_irqsave(&dev->event_lock, flags);
	if (intel_crtc->unpin_work && page_flip_finished(intel_crtc))
		atomic_inc_not_zero(&intel_crtc->unpin_work->pending);
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static inline void intel_mark_page_flip_active(struct intel_unpin_work *work)
{
	/* Ensure that the work item is consistent when activating it ... */
	smp_wmb();
	atomic_set(&work->pending, INTEL_FLIP_PENDING);
	/* and that it is marked active as soon as the irq could fire. */
	smp_wmb();
}

static int intel_gen2_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct drm_i915_gem_request *req,
				 uint32_t flags)
{
	struct intel_engine_cs *ring = req->ring;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 flip_mask;
	int ret;

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	/* Can't queue multiple flips, so wait for the previous
	 * one to finish before executing the next.
	 */
	if (intel_crtc->plane)
		flip_mask = MI_WAIT_FOR_PLANE_B_FLIP;
	else
		flip_mask = MI_WAIT_FOR_PLANE_A_FLIP;
	intel_ring_emit(ring, MI_WAIT_FOR_EVENT | flip_mask);
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_emit(ring, MI_DISPLAY_FLIP |
			MI_DISPLAY_FLIP_PLANE(intel_crtc->plane));
	intel_ring_emit(ring, fb->pitches[0]);
	intel_ring_emit(ring, intel_crtc->unpin_work->gtt_offset);
	intel_ring_emit(ring, 0); /* aux display base address, unused */

	intel_mark_page_flip_active(intel_crtc->unpin_work);
	return 0;
}

static int intel_gen3_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct drm_i915_gem_request *req,
				 uint32_t flags)
{
	struct intel_engine_cs *ring = req->ring;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 flip_mask;
	int ret;

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	if (intel_crtc->plane)
		flip_mask = MI_WAIT_FOR_PLANE_B_FLIP;
	else
		flip_mask = MI_WAIT_FOR_PLANE_A_FLIP;
	intel_ring_emit(ring, MI_WAIT_FOR_EVENT | flip_mask);
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_emit(ring, MI_DISPLAY_FLIP_I915 |
			MI_DISPLAY_FLIP_PLANE(intel_crtc->plane));
	intel_ring_emit(ring, fb->pitches[0]);
	intel_ring_emit(ring, intel_crtc->unpin_work->gtt_offset);
	intel_ring_emit(ring, MI_NOOP);

	intel_mark_page_flip_active(intel_crtc->unpin_work);
	return 0;
}

static int intel_gen4_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct drm_i915_gem_request *req,
				 uint32_t flags)
{
	struct intel_engine_cs *ring = req->ring;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	uint32_t pf, pipesrc;
	int ret;

	ret = intel_ring_begin(req, 4);
	if (ret)
		return ret;

	/* i965+ uses the linear or tiled offsets from the
	 * Display Registers (which do not change across a page-flip)
	 * so we need only reprogram the base address.
	 */
	intel_ring_emit(ring, MI_DISPLAY_FLIP |
			MI_DISPLAY_FLIP_PLANE(intel_crtc->plane));
	intel_ring_emit(ring, fb->pitches[0]);
	intel_ring_emit(ring, intel_crtc->unpin_work->gtt_offset |
			obj->tiling_mode);

	/* XXX Enabling the panel-fitter across page-flip is so far
	 * untested on non-native modes, so ignore it for now.
	 * pf = I915_READ(pipe == 0 ? PFA_CTL_1 : PFB_CTL_1) & PF_ENABLE;
	 */
	pf = 0;
	pipesrc = I915_READ(PIPESRC(intel_crtc->pipe)) & 0x0fff0fff;
	intel_ring_emit(ring, pf | pipesrc);

	intel_mark_page_flip_active(intel_crtc->unpin_work);
	return 0;
}

static int intel_gen6_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct drm_i915_gem_request *req,
				 uint32_t flags)
{
	struct intel_engine_cs *ring = req->ring;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	uint32_t pf, pipesrc;
	int ret;

	ret = intel_ring_begin(req, 4);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_DISPLAY_FLIP |
			MI_DISPLAY_FLIP_PLANE(intel_crtc->plane));
	intel_ring_emit(ring, fb->pitches[0] | obj->tiling_mode);
	intel_ring_emit(ring, intel_crtc->unpin_work->gtt_offset);

	/* Contrary to the suggestions in the documentation,
	 * "Enable Panel Fitter" does not seem to be required when page
	 * flipping with a non-native mode, and worse causes a normal
	 * modeset to fail.
	 * pf = I915_READ(PF_CTL(intel_crtc->pipe)) & PF_ENABLE;
	 */
	pf = 0;
	pipesrc = I915_READ(PIPESRC(intel_crtc->pipe)) & 0x0fff0fff;
	intel_ring_emit(ring, pf | pipesrc);

	intel_mark_page_flip_active(intel_crtc->unpin_work);
	return 0;
}

static int intel_gen7_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct drm_i915_gem_request *req,
				 uint32_t flags)
{
	struct intel_engine_cs *ring = req->ring;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	uint32_t plane_bit = 0;
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
	if (ring->id == RCS) {
		len += 6;
		/*
		 * On Gen 8, SRM is now taking an extra dword to accommodate
		 * 48bits addresses, and we need a NOOP for the batch size to
		 * stay even.
		 */
		if (IS_GEN8(dev))
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

	ret = intel_ring_begin(req, len);
	if (ret)
		return ret;

	/* Unmask the flip-done completion message. Note that the bspec says that
	 * we should do this for both the BCS and RCS, and that we must not unmask
	 * more than one flip event at any time (or ensure that one flip message
	 * can be sent by waiting for flip-done prior to queueing new flips).
	 * Experimentation says that BCS works despite DERRMR masking all
	 * flip-done completion events and that unmasking all planes at once
	 * for the RCS also doesn't appear to drop events. Setting the DERRMR
	 * to zero does lead to lockups within MI_DISPLAY_FLIP.
	 */
	if (ring->id == RCS) {
		intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(1));
		intel_ring_emit_reg(ring, DERRMR);
		intel_ring_emit(ring, ~(DERRMR_PIPEA_PRI_FLIP_DONE |
					DERRMR_PIPEB_PRI_FLIP_DONE |
					DERRMR_PIPEC_PRI_FLIP_DONE));
		if (IS_GEN8(dev))
			intel_ring_emit(ring, MI_STORE_REGISTER_MEM_GEN8 |
					      MI_SRM_LRM_GLOBAL_GTT);
		else
			intel_ring_emit(ring, MI_STORE_REGISTER_MEM |
					      MI_SRM_LRM_GLOBAL_GTT);
		intel_ring_emit_reg(ring, DERRMR);
		intel_ring_emit(ring, ring->scratch.gtt_offset + 256);
		if (IS_GEN8(dev)) {
			intel_ring_emit(ring, 0);
			intel_ring_emit(ring, MI_NOOP);
		}
	}

	intel_ring_emit(ring, MI_DISPLAY_FLIP_I915 | plane_bit);
	intel_ring_emit(ring, (fb->pitches[0] | obj->tiling_mode));
	intel_ring_emit(ring, intel_crtc->unpin_work->gtt_offset);
	intel_ring_emit(ring, (MI_NOOP));

	intel_mark_page_flip_active(intel_crtc->unpin_work);
	return 0;
}

static bool use_mmio_flip(struct intel_engine_cs *ring,
			  struct drm_i915_gem_object *obj)
{
	/*
	 * This is not being used for older platforms, because
	 * non-availability of flip done interrupt forces us to use
	 * CS flips. Older platforms derive flip done using some clever
	 * tricks involving the flip_pending status bits and vblank irqs.
	 * So using MMIO flips there would disrupt this mechanism.
	 */

	if (ring == NULL)
		return true;

	if (INTEL_INFO(ring->dev)->gen < 5)
		return false;

	if (i915.use_mmio_flip < 0)
		return false;
	else if (i915.use_mmio_flip > 0)
		return true;
	else if (i915.enable_execlists)
		return true;
	else if (obj->base.dma_buf &&
		 !reservation_object_test_signaled_rcu(obj->base.dma_buf->resv,
						       false))
		return true;
	else
		return ring != i915_gem_request_get_ring(obj->last_write_req);
}

static void skl_do_mmio_flip(struct intel_crtc *intel_crtc,
			     unsigned int rotation,
			     struct intel_unpin_work *work)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = intel_crtc->base.primary->fb;
	const enum pipe pipe = intel_crtc->pipe;
	u32 ctl, stride, tile_height;

	ctl = I915_READ(PLANE_CTL(pipe, 0));
	ctl &= ~PLANE_CTL_TILED_MASK;
	switch (fb->modifier[0]) {
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
		MISSING_CASE(fb->modifier[0]);
	}

	/*
	 * The stride is either expressed as a multiple of 64 bytes chunks for
	 * linear buffers or in number of tiles for tiled buffers.
	 */
	if (intel_rotation_90_or_270(rotation)) {
		/* stride = Surface height in tiles */
		tile_height = intel_tile_height(dev, fb->pixel_format,
						fb->modifier[0], 0);
		stride = DIV_ROUND_UP(fb->height, tile_height);
	} else {
		stride = fb->pitches[0] /
				intel_fb_stride_alignment(dev, fb->modifier[0],
							  fb->pixel_format);
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
			     struct intel_unpin_work *work)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_framebuffer *intel_fb =
		to_intel_framebuffer(intel_crtc->base.primary->fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	i915_reg_t reg = DSPCNTR(intel_crtc->plane);
	u32 dspcntr;

	dspcntr = I915_READ(reg);

	if (obj->tiling_mode != I915_TILING_NONE)
		dspcntr |= DISPPLANE_TILED;
	else
		dspcntr &= ~DISPPLANE_TILED;

	I915_WRITE(reg, dspcntr);

	I915_WRITE(DSPSURF(intel_crtc->plane), work->gtt_offset);
	POSTING_READ(DSPSURF(intel_crtc->plane));
}

/*
 * XXX: This is the temporary way to update the plane registers until we get
 * around to using the usual plane update functions for MMIO flips
 */
static void intel_do_mmio_flip(struct intel_mmio_flip *mmio_flip)
{
	struct intel_crtc *crtc = mmio_flip->crtc;
	struct intel_unpin_work *work;

	spin_lock_irq(&crtc->base.dev->event_lock);
	work = crtc->unpin_work;
	spin_unlock_irq(&crtc->base.dev->event_lock);
	if (work == NULL)
		return;

	intel_mark_page_flip_active(work);

	intel_pipe_update_start(crtc);

	if (INTEL_INFO(mmio_flip->i915)->gen >= 9)
		skl_do_mmio_flip(crtc, mmio_flip->rotation, work);
	else
		/* use_mmio_flip() retricts MMIO flips to ilk+ */
		ilk_do_mmio_flip(crtc, work);

	intel_pipe_update_end(crtc);
}

static void intel_mmio_flip_work_func(struct work_struct *work)
{
	struct intel_mmio_flip *mmio_flip =
		container_of(work, struct intel_mmio_flip, work);
	struct intel_framebuffer *intel_fb =
		to_intel_framebuffer(mmio_flip->crtc->base.primary->fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;

	if (mmio_flip->req) {
		WARN_ON(__i915_wait_request(mmio_flip->req,
					    mmio_flip->crtc->reset_counter,
					    false, NULL,
					    &mmio_flip->i915->rps.mmioflips));
		i915_gem_request_unreference__unlocked(mmio_flip->req);
	}

	/* For framebuffer backed by dmabuf, wait for fence */
	if (obj->base.dma_buf)
		WARN_ON(reservation_object_wait_timeout_rcu(obj->base.dma_buf->resv,
							    false, false,
							    MAX_SCHEDULE_TIMEOUT) < 0);

	intel_do_mmio_flip(mmio_flip);
	kfree(mmio_flip);
}

static int intel_queue_mmio_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_i915_gem_object *obj)
{
	struct intel_mmio_flip *mmio_flip;

	mmio_flip = kmalloc(sizeof(*mmio_flip), GFP_KERNEL);
	if (mmio_flip == NULL)
		return -ENOMEM;

	mmio_flip->i915 = to_i915(dev);
	mmio_flip->req = i915_gem_request_reference(obj->last_write_req);
	mmio_flip->crtc = to_intel_crtc(crtc);
	mmio_flip->rotation = crtc->primary->state->rotation;

	INIT_WORK(&mmio_flip->work, intel_mmio_flip_work_func);
	schedule_work(&mmio_flip->work);

	return 0;
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

static bool __intel_pageflip_stall_check(struct drm_device *dev,
					 struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_unpin_work *work = intel_crtc->unpin_work;
	u32 addr;

	if (atomic_read(&work->pending) >= INTEL_FLIP_COMPLETE)
		return true;

	if (atomic_read(&work->pending) < INTEL_FLIP_PENDING)
		return false;

	if (!work->enable_stall_check)
		return false;

	if (work->flip_ready_vblank == 0) {
		if (work->flip_queued_req &&
		    !i915_gem_request_completed(work->flip_queued_req, true))
			return false;

		work->flip_ready_vblank = drm_crtc_vblank_count(crtc);
	}

	if (drm_crtc_vblank_count(crtc) - work->flip_ready_vblank < 3)
		return false;

	/* Potential stall - if we see that the flip has happened,
	 * assume a missed interrupt. */
	if (INTEL_INFO(dev)->gen >= 4)
		addr = I915_HI_DISPBASE(I915_READ(DSPSURF(intel_crtc->plane)));
	else
		addr = I915_READ(DSPADDR(intel_crtc->plane));

	/* There is a potential issue here with a false positive after a flip
	 * to the same address. We could address this by checking for a
	 * non-incrementing frame counter.
	 */
	return addr == work->gtt_offset;
}

void intel_check_page_flip(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_unpin_work *work;

	WARN_ON(!in_interrupt());

	if (crtc == NULL)
		return;

	spin_lock(&dev->event_lock);
	work = intel_crtc->unpin_work;
	if (work != NULL && __intel_pageflip_stall_check(dev, crtc)) {
		WARN_ONCE(1, "Kicking stuck page flip: queued at %d, now %d\n",
			 work->flip_queued_vblank, drm_vblank_count(dev, pipe));
		page_flip_completed(intel_crtc);
		work = NULL;
	}
	if (work != NULL &&
	    drm_vblank_count(dev, pipe) - work->flip_queued_vblank > 1)
		intel_queue_rps_boost_for_request(dev, work->flip_queued_req);
	spin_unlock(&dev->event_lock);
}

static int intel_crtc_page_flip(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				struct drm_pending_vblank_event *event,
				uint32_t page_flip_flags)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *old_fb = crtc->primary->fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_plane *primary = crtc->primary;
	enum pipe pipe = intel_crtc->pipe;
	struct intel_unpin_work *work;
	struct intel_engine_cs *ring;
	bool mmio_flip;
	struct drm_i915_gem_request *request = NULL;
	int ret;

	/*
	 * drm_mode_page_flip_ioctl() should already catch this, but double
	 * check to be safe.  In the future we may enable pageflipping from
	 * a disabled primary plane.
	 */
	if (WARN_ON(intel_fb_obj(old_fb) == NULL))
		return -EBUSY;

	/* Can't change pixel format via MI display flips. */
	if (fb->pixel_format != crtc->primary->fb->pixel_format)
		return -EINVAL;

	/*
	 * TILEOFF/LINOFF registers can't be changed via MI display flips.
	 * Note that pitch changes could also affect these register.
	 */
	if (INTEL_INFO(dev)->gen > 3 &&
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
	INIT_WORK(&work->work, intel_unpin_work_fn);

	ret = drm_crtc_vblank_get(crtc);
	if (ret)
		goto free_work;

	/* We borrow the event spin lock for protecting unpin_work */
	spin_lock_irq(&dev->event_lock);
	if (intel_crtc->unpin_work) {
		/* Before declaring the flip queue wedged, check if
		 * the hardware completed the operation behind our backs.
		 */
		if (__intel_pageflip_stall_check(dev, crtc)) {
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
	intel_crtc->unpin_work = work;
	spin_unlock_irq(&dev->event_lock);

	if (atomic_read(&intel_crtc->unpin_work_count) >= 2)
		flush_workqueue(dev_priv->wq);

	/* Reference the objects for the scheduled work. */
	drm_framebuffer_reference(work->old_fb);
	drm_gem_object_reference(&obj->base);

	crtc->primary->fb = fb;
	update_state_fb(crtc->primary);

	work->pending_flip_obj = obj;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto cleanup;

	atomic_inc(&intel_crtc->unpin_work_count);
	intel_crtc->reset_counter = atomic_read(&dev_priv->gpu_error.reset_counter);

	if (INTEL_INFO(dev)->gen >= 5 || IS_G4X(dev))
		work->flip_count = I915_READ(PIPE_FLIPCOUNT_G4X(pipe)) + 1;

	if (IS_VALLEYVIEW(dev)) {
		ring = &dev_priv->ring[BCS];
		if (obj->tiling_mode != intel_fb_obj(work->old_fb)->tiling_mode)
			/* vlv: DISPLAY_FLIP fails to change tiling */
			ring = NULL;
	} else if (IS_IVYBRIDGE(dev) || IS_HASWELL(dev)) {
		ring = &dev_priv->ring[BCS];
	} else if (INTEL_INFO(dev)->gen >= 7) {
		ring = i915_gem_request_get_ring(obj->last_write_req);
		if (ring == NULL || ring->id != RCS)
			ring = &dev_priv->ring[BCS];
	} else {
		ring = &dev_priv->ring[RCS];
	}

	mmio_flip = use_mmio_flip(ring, obj);

	/* When using CS flips, we want to emit semaphores between rings.
	 * However, when using mmio flips we will create a task to do the
	 * synchronisation, so all we want here is to pin the framebuffer
	 * into the display plane and skip any waits.
	 */
	if (!mmio_flip) {
		ret = i915_gem_object_sync(obj, ring, &request);
		if (ret)
			goto cleanup_pending;
	}

	ret = intel_pin_and_fence_fb_obj(crtc->primary, fb,
					 crtc->primary->state);
	if (ret)
		goto cleanup_pending;

	work->gtt_offset = intel_plane_obj_offset(to_intel_plane(primary),
						  obj, 0);
	work->gtt_offset += intel_crtc->dspaddr_offset;

	if (mmio_flip) {
		ret = intel_queue_mmio_flip(dev, crtc, obj);
		if (ret)
			goto cleanup_unpin;

		i915_gem_request_assign(&work->flip_queued_req,
					obj->last_write_req);
	} else {
		if (!request) {
			ret = i915_gem_request_alloc(ring, ring->default_context, &request);
			if (ret)
				goto cleanup_unpin;
		}

		ret = dev_priv->display.queue_flip(dev, crtc, fb, obj, request,
						   page_flip_flags);
		if (ret)
			goto cleanup_unpin;

		i915_gem_request_assign(&work->flip_queued_req, request);
	}

	if (request)
		i915_add_request_no_flush(request);

	work->flip_queued_vblank = drm_crtc_vblank_count(crtc);
	work->enable_stall_check = true;

	i915_gem_track_fb(intel_fb_obj(work->old_fb), obj,
			  to_intel_plane(primary)->frontbuffer_bit);
	mutex_unlock(&dev->struct_mutex);

	intel_fbc_deactivate(intel_crtc);
	intel_frontbuffer_flip_prepare(dev,
				       to_intel_plane(primary)->frontbuffer_bit);

	trace_i915_flip_request(intel_crtc->plane, obj);

	return 0;

cleanup_unpin:
	intel_unpin_fb_obj(fb, crtc->primary->state);
cleanup_pending:
	if (request)
		i915_gem_request_cancel(request);
	atomic_dec(&intel_crtc->unpin_work_count);
	mutex_unlock(&dev->struct_mutex);
cleanup:
	crtc->primary->fb = old_fb;
	update_state_fb(crtc->primary);

	drm_gem_object_unreference_unlocked(&obj->base);
	drm_framebuffer_unreference(work->old_fb);

	spin_lock_irq(&dev->event_lock);
	intel_crtc->unpin_work = NULL;
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

		if (ret)
			drm_atomic_state_free(state);

		if (ret == 0 && event) {
			spin_lock_irq(&dev->event_lock);
			drm_send_vblank_event(dev, pipe, event);
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
	if (!plane->state->fb || !state->fb ||
	    plane->state->fb->modifier[0] != state->fb->modifier[0] ||
	    plane->state->rotation != state->rotation ||
	    drm_rect_width(&new->src) != drm_rect_width(&cur->src) ||
	    drm_rect_height(&new->src) != drm_rect_height(&cur->src) ||
	    drm_rect_width(&new->dst) != drm_rect_width(&cur->dst) ||
	    drm_rect_height(&new->dst) != drm_rect_height(&cur->dst))
		return true;

	return false;
}

static bool needs_scaling(struct intel_plane_state *state)
{
	int src_w = drm_rect_width(&state->src) >> 16;
	int src_h = drm_rect_height(&state->src) >> 16;
	int dst_w = drm_rect_width(&state->dst);
	int dst_h = drm_rect_height(&state->dst);

	return (src_w != dst_w || src_h != dst_h);
}

int intel_plane_atomic_calc_changes(struct drm_crtc_state *crtc_state,
				    struct drm_plane_state *plane_state)
{
	struct drm_crtc *crtc = crtc_state->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_plane *plane = plane_state->plane;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane_state *old_plane_state =
		to_intel_plane_state(plane->state);
	int idx = intel_crtc->base.base.id, ret;
	int i = drm_plane_index(plane);
	bool mode_changed = needs_modeset(crtc_state);
	bool was_crtc_enabled = crtc->state->active;
	bool is_crtc_enabled = crtc_state->active;
	bool turn_off, turn_on, visible, was_visible;
	struct drm_framebuffer *fb = plane_state->fb;

	if (crtc_state && INTEL_INFO(dev)->gen >= 9 &&
	    plane->type != DRM_PLANE_TYPE_CURSOR) {
		ret = skl_update_scaler_plane(
			to_intel_crtc_state(crtc_state),
			to_intel_plane_state(plane_state));
		if (ret)
			return ret;
	}

	was_visible = old_plane_state->visible;
	visible = to_intel_plane_state(plane_state)->visible;

	if (!was_crtc_enabled && WARN_ON(was_visible))
		was_visible = false;

	if (!is_crtc_enabled && WARN_ON(visible))
		visible = false;

	if (!was_visible && !visible)
		return 0;

	turn_off = was_visible && (!visible || mode_changed);
	turn_on = visible && (!was_visible || mode_changed);

	DRM_DEBUG_ATOMIC("[CRTC:%i] has [PLANE:%i] with fb %i\n", idx,
			 plane->base.id, fb ? fb->base.id : -1);

	DRM_DEBUG_ATOMIC("[PLANE:%i] visible %i -> %i, off %i, on %i, ms %i\n",
			 plane->base.id, was_visible, visible,
			 turn_off, turn_on, mode_changed);

	if (turn_on) {
		intel_crtc->atomic.update_wm_pre = true;
		/* must disable cxsr around plane enable/disable */
		if (plane->type != DRM_PLANE_TYPE_CURSOR) {
			intel_crtc->atomic.disable_cxsr = true;
			/* to potentially re-enable cxsr */
			intel_crtc->atomic.wait_vblank = true;
			intel_crtc->atomic.update_wm_post = true;
		}
	} else if (turn_off) {
		intel_crtc->atomic.update_wm_post = true;
		/* must disable cxsr around plane enable/disable */
		if (plane->type != DRM_PLANE_TYPE_CURSOR) {
			if (is_crtc_enabled)
				intel_crtc->atomic.wait_vblank = true;
			intel_crtc->atomic.disable_cxsr = true;
		}
	} else if (intel_wm_need_update(plane, plane_state)) {
		intel_crtc->atomic.update_wm_pre = true;
	}

	if (visible || was_visible)
		intel_crtc->atomic.fb_bits |=
			to_intel_plane(plane)->frontbuffer_bit;

	switch (plane->type) {
	case DRM_PLANE_TYPE_PRIMARY:
		intel_crtc->atomic.pre_disable_primary = turn_off;
		intel_crtc->atomic.post_enable_primary = turn_on;

		if (turn_off) {
			/*
			 * FIXME: Actually if we will still have any other
			 * plane enabled on the pipe we could let IPS enabled
			 * still, but for now lets consider that when we make
			 * primary invisible by setting DSPCNTR to 0 on
			 * update_primary_plane function IPS needs to be
			 * disable.
			 */
			intel_crtc->atomic.disable_ips = true;

			intel_crtc->atomic.disable_fbc = true;
		}

		/*
		 * FBC does not work on some platforms for rotated
		 * planes, so disable it when rotation is not 0 and
		 * update it when rotation is set back to 0.
		 *
		 * FIXME: This is redundant with the fbc update done in
		 * the primary plane enable function except that that
		 * one is done too late. We eventually need to unify
		 * this.
		 */

		if (visible &&
		    INTEL_INFO(dev)->gen <= 4 && !IS_G4X(dev) &&
		    dev_priv->fbc.crtc == intel_crtc &&
		    plane_state->rotation != BIT(DRM_ROTATE_0))
			intel_crtc->atomic.disable_fbc = true;

		/*
		 * BDW signals flip done immediately if the plane
		 * is disabled, even if the plane enable is already
		 * armed to occur at the next vblank :(
		 */
		if (turn_on && IS_BROADWELL(dev))
			intel_crtc->atomic.wait_vblank = true;

		intel_crtc->atomic.update_fbc |= visible || mode_changed;
		break;
	case DRM_PLANE_TYPE_CURSOR:
		break;
	case DRM_PLANE_TYPE_OVERLAY:
		/*
		 * WaCxSRDisabledForSpriteScaling:ivb
		 *
		 * cstate->update_wm was already set above, so this flag will
		 * take effect when we commit and program watermarks.
		 */
		if (IS_IVYBRIDGE(dev) &&
		    needs_scaling(to_intel_plane_state(plane_state)) &&
		    !needs_scaling(old_plane_state)) {
			to_intel_crtc_state(crtc_state)->disable_lp_wm = true;
		} else if (turn_off && !mode_changed) {
			intel_crtc->atomic.wait_vblank = true;
			intel_crtc->atomic.update_sprite_watermarks |=
				1 << i;
		}

		break;
	}
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

static bool check_encoder_cloning(struct drm_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct intel_encoder *encoder;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int i;

	for_each_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc != &crtc->base)
			continue;

		encoder = to_intel_encoder(connector_state->best_encoder);
		if (!check_single_encoder_cloning(state, crtc, encoder))
			return false;
	}

	return true;
}

static int intel_crtc_atomic_check(struct drm_crtc *crtc,
				   struct drm_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_crtc_state *pipe_config =
		to_intel_crtc_state(crtc_state);
	struct drm_atomic_state *state = crtc_state->state;
	int ret;
	bool mode_changed = needs_modeset(crtc_state);

	if (mode_changed && !check_encoder_cloning(state, intel_crtc)) {
		DRM_DEBUG_KMS("rejecting invalid cloning configuration\n");
		return -EINVAL;
	}

	if (mode_changed && !crtc_state->active)
		intel_crtc->atomic.update_wm_post = true;

	if (mode_changed && crtc_state->enable &&
	    dev_priv->display.crtc_compute_clock &&
	    !WARN_ON(pipe_config->shared_dpll != DPLL_ID_PRIVATE)) {
		ret = dev_priv->display.crtc_compute_clock(intel_crtc,
							   pipe_config);
		if (ret)
			return ret;
	}

	ret = 0;
	if (dev_priv->display.compute_pipe_wm) {
		ret = dev_priv->display.compute_pipe_wm(intel_crtc, state);
		if (ret)
			return ret;
	}

	if (INTEL_INFO(dev)->gen >= 9) {
		if (mode_changed)
			ret = skl_update_scaler_crtc(pipe_config);

		if (!ret)
			ret = intel_atomic_setup_scalers(dev, intel_crtc,
							 pipe_config);
	}

	return ret;
}

static const struct drm_crtc_helper_funcs intel_helper_funcs = {
	.mode_set_base_atomic = intel_pipe_set_base_atomic,
	.load_lut = intel_crtc_load_lut,
	.atomic_begin = intel_begin_crtc_commit,
	.atomic_flush = intel_finish_crtc_commit,
	.atomic_check = intel_crtc_atomic_check,
};

static void intel_modeset_update_connector_atomic_state(struct drm_device *dev)
{
	struct intel_connector *connector;

	for_each_intel_connector(dev, connector) {
		if (connector->base.encoder) {
			connector->base.state->best_encoder =
				connector->base.encoder;
			connector->base.state->crtc =
				connector->base.encoder->crtc;
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
	int bpp = pipe_config->pipe_bpp;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] checking for sink bpp constrains\n",
		connector->base.base.id,
		connector->base.name);

	/* Don't use an invalid EDID bpc value */
	if (connector->base.display_info.bpc &&
	    connector->base.display_info.bpc * 3 < bpp) {
		DRM_DEBUG_KMS("clamping display bpp (was %d) to EDID reported max of %d\n",
			      bpp, connector->base.display_info.bpc*3);
		pipe_config->pipe_bpp = connector->base.display_info.bpc*3;
	}

	/* Clamp bpp to 8 on screens without EDID 1.4 */
	if (connector->base.display_info.bpc == 0 && bpp > 24) {
		DRM_DEBUG_KMS("clamping display bpp (was %d) to default limit of 24\n",
			      bpp);
		pipe_config->pipe_bpp = 24;
	}
}

static int
compute_baseline_pipe_bpp(struct intel_crtc *crtc,
			  struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_atomic_state *state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int bpp, i;

	if ((IS_G4X(dev) || IS_VALLEYVIEW(dev)))
		bpp = 10*3;
	else if (INTEL_INFO(dev)->gen >= 5)
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

static void intel_dump_pipe_config(struct intel_crtc *crtc,
				   struct intel_crtc_state *pipe_config,
				   const char *context)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_plane *plane;
	struct intel_plane *intel_plane;
	struct intel_plane_state *state;
	struct drm_framebuffer *fb;

	DRM_DEBUG_KMS("[CRTC:%d]%s config %p for pipe %c\n", crtc->base.base.id,
		      context, pipe_config, pipe_name(crtc->pipe));

	DRM_DEBUG_KMS("cpu_transcoder: %c\n", transcoder_name(pipe_config->cpu_transcoder));
	DRM_DEBUG_KMS("pipe bpp: %i, dithering: %i\n",
		      pipe_config->pipe_bpp, pipe_config->dither);
	DRM_DEBUG_KMS("fdi/pch: %i, lanes: %i, gmch_m: %u, gmch_n: %u, link_m: %u, link_n: %u, tu: %u\n",
		      pipe_config->has_pch_encoder,
		      pipe_config->fdi_lanes,
		      pipe_config->fdi_m_n.gmch_m, pipe_config->fdi_m_n.gmch_n,
		      pipe_config->fdi_m_n.link_m, pipe_config->fdi_m_n.link_n,
		      pipe_config->fdi_m_n.tu);
	DRM_DEBUG_KMS("dp: %i, lanes: %i, gmch_m: %u, gmch_n: %u, link_m: %u, link_n: %u, tu: %u\n",
		      pipe_config->has_dp_encoder,
		      pipe_config->lane_count,
		      pipe_config->dp_m_n.gmch_m, pipe_config->dp_m_n.gmch_n,
		      pipe_config->dp_m_n.link_m, pipe_config->dp_m_n.link_n,
		      pipe_config->dp_m_n.tu);

	DRM_DEBUG_KMS("dp: %i, lanes: %i, gmch_m2: %u, gmch_n2: %u, link_m2: %u, link_n2: %u, tu2: %u\n",
		      pipe_config->has_dp_encoder,
		      pipe_config->lane_count,
		      pipe_config->dp_m2_n2.gmch_m,
		      pipe_config->dp_m2_n2.gmch_n,
		      pipe_config->dp_m2_n2.link_m,
		      pipe_config->dp_m2_n2.link_n,
		      pipe_config->dp_m2_n2.tu);

	DRM_DEBUG_KMS("audio: %i, infoframes: %i\n",
		      pipe_config->has_audio,
		      pipe_config->has_infoframe);

	DRM_DEBUG_KMS("requested mode:\n");
	drm_mode_debug_printmodeline(&pipe_config->base.mode);
	DRM_DEBUG_KMS("adjusted mode:\n");
	drm_mode_debug_printmodeline(&pipe_config->base.adjusted_mode);
	intel_dump_crtc_timings(&pipe_config->base.adjusted_mode);
	DRM_DEBUG_KMS("port clock: %d\n", pipe_config->port_clock);
	DRM_DEBUG_KMS("pipe src size: %dx%d\n",
		      pipe_config->pipe_src_w, pipe_config->pipe_src_h);
	DRM_DEBUG_KMS("num_scalers: %d, scaler_users: 0x%x, scaler_id: %d\n",
		      crtc->num_scalers,
		      pipe_config->scaler_state.scaler_users,
		      pipe_config->scaler_state.scaler_id);
	DRM_DEBUG_KMS("gmch pfit: control: 0x%08x, ratios: 0x%08x, lvds border: 0x%08x\n",
		      pipe_config->gmch_pfit.control,
		      pipe_config->gmch_pfit.pgm_ratios,
		      pipe_config->gmch_pfit.lvds_border_bits);
	DRM_DEBUG_KMS("pch pfit: pos: 0x%08x, size: 0x%08x, %s\n",
		      pipe_config->pch_pfit.pos,
		      pipe_config->pch_pfit.size,
		      pipe_config->pch_pfit.enabled ? "enabled" : "disabled");
	DRM_DEBUG_KMS("ips: %i\n", pipe_config->ips_enabled);
	DRM_DEBUG_KMS("double wide: %i\n", pipe_config->double_wide);

	if (IS_BROXTON(dev)) {
		DRM_DEBUG_KMS("ddi_pll_sel: %u; dpll_hw_state: ebb0: 0x%x, ebb4: 0x%x,"
			      "pll0: 0x%x, pll1: 0x%x, pll2: 0x%x, pll3: 0x%x, "
			      "pll6: 0x%x, pll8: 0x%x, pll9: 0x%x, pll10: 0x%x, pcsdw12: 0x%x\n",
			      pipe_config->ddi_pll_sel,
			      pipe_config->dpll_hw_state.ebb0,
			      pipe_config->dpll_hw_state.ebb4,
			      pipe_config->dpll_hw_state.pll0,
			      pipe_config->dpll_hw_state.pll1,
			      pipe_config->dpll_hw_state.pll2,
			      pipe_config->dpll_hw_state.pll3,
			      pipe_config->dpll_hw_state.pll6,
			      pipe_config->dpll_hw_state.pll8,
			      pipe_config->dpll_hw_state.pll9,
			      pipe_config->dpll_hw_state.pll10,
			      pipe_config->dpll_hw_state.pcsdw12);
	} else if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev)) {
		DRM_DEBUG_KMS("ddi_pll_sel: %u; dpll_hw_state: "
			      "ctrl1: 0x%x, cfgcr1: 0x%x, cfgcr2: 0x%x\n",
			      pipe_config->ddi_pll_sel,
			      pipe_config->dpll_hw_state.ctrl1,
			      pipe_config->dpll_hw_state.cfgcr1,
			      pipe_config->dpll_hw_state.cfgcr2);
	} else if (HAS_DDI(dev)) {
		DRM_DEBUG_KMS("ddi_pll_sel: %u; dpll_hw_state: wrpll: 0x%x spll: 0x%x\n",
			      pipe_config->ddi_pll_sel,
			      pipe_config->dpll_hw_state.wrpll,
			      pipe_config->dpll_hw_state.spll);
	} else {
		DRM_DEBUG_KMS("dpll_hw_state: dpll: 0x%x, dpll_md: 0x%x, "
			      "fp0: 0x%x, fp1: 0x%x\n",
			      pipe_config->dpll_hw_state.dpll,
			      pipe_config->dpll_hw_state.dpll_md,
			      pipe_config->dpll_hw_state.fp0,
			      pipe_config->dpll_hw_state.fp1);
	}

	DRM_DEBUG_KMS("planes on this crtc\n");
	list_for_each_entry(plane, &dev->mode_config.plane_list, head) {
		intel_plane = to_intel_plane(plane);
		if (intel_plane->pipe != crtc->pipe)
			continue;

		state = to_intel_plane_state(plane->state);
		fb = state->base.fb;
		if (!fb) {
			DRM_DEBUG_KMS("%s PLANE:%d plane: %u.%u idx: %d "
				"disabled, scaler_id = %d\n",
				plane->type == DRM_PLANE_TYPE_CURSOR ? "CURSOR" : "STANDARD",
				plane->base.id, intel_plane->pipe,
				(crtc->base.primary == plane) ? 0 : intel_plane->plane + 1,
				drm_plane_index(plane), state->scaler_id);
			continue;
		}

		DRM_DEBUG_KMS("%s PLANE:%d plane: %u.%u idx: %d enabled",
			plane->type == DRM_PLANE_TYPE_CURSOR ? "CURSOR" : "STANDARD",
			plane->base.id, intel_plane->pipe,
			crtc->base.primary == plane ? 0 : intel_plane->plane + 1,
			drm_plane_index(plane));
		DRM_DEBUG_KMS("\tFB:%d, fb = %ux%u format = 0x%x",
			fb->base.id, fb->width, fb->height, fb->pixel_format);
		DRM_DEBUG_KMS("\tscaler:%d src (%u, %u) %ux%u dst (%u, %u) %ux%u\n",
			state->scaler_id,
			state->src.x1 >> 16, state->src.y1 >> 16,
			drm_rect_width(&state->src) >> 16,
			drm_rect_height(&state->src) >> 16,
			state->dst.x1, state->dst.y1,
			drm_rect_width(&state->dst), drm_rect_height(&state->dst));
	}
}

static bool check_digital_port_conflicts(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct intel_encoder *encoder;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	unsigned int used_ports = 0;
	int i;

	/*
	 * Walk the connector list instead of the encoder
	 * list to detect the problem on ddi platforms
	 * where there's just one encoder per digital port.
	 */
	for_each_connector_in_state(state, connector, connector_state, i) {
		if (!connector_state->best_encoder)
			continue;

		encoder = to_intel_encoder(connector_state->best_encoder);

		WARN_ON(!connector_state->crtc);

		switch (encoder->type) {
			unsigned int port_mask;
		case INTEL_OUTPUT_UNKNOWN:
			if (WARN_ON(!HAS_DDI(dev)))
				break;
		case INTEL_OUTPUT_DISPLAYPORT:
		case INTEL_OUTPUT_HDMI:
		case INTEL_OUTPUT_EDP:
			port_mask = 1 << enc_to_dig_port(&encoder->base)->port;

			/* the same port mustn't appear more than once */
			if (used_ports & port_mask)
				return false;

			used_ports |= port_mask;
		default:
			break;
		}
	}

	return true;
}

static void
clear_intel_crtc_state(struct intel_crtc_state *crtc_state)
{
	struct drm_crtc_state tmp_state;
	struct intel_crtc_scaler_state scaler_state;
	struct intel_dpll_hw_state dpll_hw_state;
	enum intel_dpll_id shared_dpll;
	uint32_t ddi_pll_sel;
	bool force_thru;

	/* FIXME: before the switch to atomic started, a new pipe_config was
	 * kzalloc'd. Code that depends on any field being zero should be
	 * fixed, so that the crtc_state can be safely duplicated. For now,
	 * only fields that are know to not cause problems are preserved. */

	tmp_state = crtc_state->base;
	scaler_state = crtc_state->scaler_state;
	shared_dpll = crtc_state->shared_dpll;
	dpll_hw_state = crtc_state->dpll_hw_state;
	ddi_pll_sel = crtc_state->ddi_pll_sel;
	force_thru = crtc_state->pch_pfit.force_thru;

	memset(crtc_state, 0, sizeof *crtc_state);

	crtc_state->base = tmp_state;
	crtc_state->scaler_state = scaler_state;
	crtc_state->shared_dpll = shared_dpll;
	crtc_state->dpll_hw_state = dpll_hw_state;
	crtc_state->ddi_pll_sel = ddi_pll_sel;
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
	drm_crtc_get_hv_timing(&pipe_config->base.mode,
			       &pipe_config->pipe_src_w,
			       &pipe_config->pipe_src_h);

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

		if (!(encoder->compute_config(encoder, pipe_config))) {
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
	 * only enable it on 6bpc panels. */
	pipe_config->dither = pipe_config->pipe_bpp == 6*3;
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

#define for_each_intel_crtc_masked(dev, mask, intel_crtc) \
	list_for_each_entry((intel_crtc), \
			    &(dev)->mode_config.crtc_list, \
			    base.head) \
		if (mask & (1 <<(intel_crtc)->pipe))

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

	if (m > m2) {
		while (m > m2) {
			m2 <<= 1;
			n2 <<= 1;
		}
	} else if (m < m2) {
		while (m < m2) {
			m <<= 1;
			n <<= 1;
		}
	}

	return m == m2 && n == n2;
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

static bool
intel_pipe_config_compare(struct drm_device *dev,
			  struct intel_crtc_state *current_config,
			  struct intel_crtc_state *pipe_config,
			  bool adjust)
{
	bool ret = true;

#define INTEL_ERR_OR_DBG_KMS(fmt, ...) \
	do { \
		if (!adjust) \
			DRM_ERROR(fmt, ##__VA_ARGS__); \
		else \
			DRM_DEBUG_KMS(fmt, ##__VA_ARGS__); \
	} while (0)

#define PIPE_CONF_CHECK_X(name)	\
	if (current_config->name != pipe_config->name) { \
		INTEL_ERR_OR_DBG_KMS("mismatch in " #name " " \
			  "(expected 0x%08x, found 0x%08x)\n", \
			  current_config->name, \
			  pipe_config->name); \
		ret = false; \
	}

#define PIPE_CONF_CHECK_I(name)	\
	if (current_config->name != pipe_config->name) { \
		INTEL_ERR_OR_DBG_KMS("mismatch in " #name " " \
			  "(expected %i, found %i)\n", \
			  current_config->name, \
			  pipe_config->name); \
		ret = false; \
	}

#define PIPE_CONF_CHECK_M_N(name) \
	if (!intel_compare_link_m_n(&current_config->name, \
				    &pipe_config->name,\
				    adjust)) { \
		INTEL_ERR_OR_DBG_KMS("mismatch in " #name " " \
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

#define PIPE_CONF_CHECK_M_N_ALT(name, alt_name) \
	if (!intel_compare_link_m_n(&current_config->name, \
				    &pipe_config->name, adjust) && \
	    !intel_compare_link_m_n(&current_config->alt_name, \
				    &pipe_config->name, adjust)) { \
		INTEL_ERR_OR_DBG_KMS("mismatch in " #name " " \
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

/* This is required for BDW+ where there is only one set of registers for
 * switching between high and low RR.
 * This macro can be used whenever a comparison has to be made between one
 * hw state and multiple sw state variables.
 */
#define PIPE_CONF_CHECK_I_ALT(name, alt_name) \
	if ((current_config->name != pipe_config->name) && \
		(current_config->alt_name != pipe_config->name)) { \
			INTEL_ERR_OR_DBG_KMS("mismatch in " #name " " \
				  "(expected %i or %i, found %i)\n", \
				  current_config->name, \
				  current_config->alt_name, \
				  pipe_config->name); \
			ret = false; \
	}

#define PIPE_CONF_CHECK_FLAGS(name, mask)	\
	if ((current_config->name ^ pipe_config->name) & (mask)) { \
		INTEL_ERR_OR_DBG_KMS("mismatch in " #name "(" #mask ") " \
			  "(expected %i, found %i)\n", \
			  current_config->name & (mask), \
			  pipe_config->name & (mask)); \
		ret = false; \
	}

#define PIPE_CONF_CHECK_CLOCK_FUZZY(name) \
	if (!intel_fuzzy_clock_check(current_config->name, pipe_config->name)) { \
		INTEL_ERR_OR_DBG_KMS("mismatch in " #name " " \
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

	PIPE_CONF_CHECK_I(has_dp_encoder);
	PIPE_CONF_CHECK_I(lane_count);

	if (INTEL_INFO(dev)->gen < 8) {
		PIPE_CONF_CHECK_M_N(dp_m_n);

		PIPE_CONF_CHECK_I(has_drrs);
		if (current_config->has_drrs)
			PIPE_CONF_CHECK_M_N(dp_m2_n2);
	} else
		PIPE_CONF_CHECK_M_N_ALT(dp_m_n, dp_m2_n2);

	PIPE_CONF_CHECK_I(has_dsi_encoder);

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
	if ((INTEL_INFO(dev)->gen < 8 && !IS_HASWELL(dev)) ||
	    IS_VALLEYVIEW(dev))
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
	if (INTEL_INFO(dev)->gen < 4)
		PIPE_CONF_CHECK_I(gmch_pfit.pgm_ratios);
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
	}

	/* BDW+ don't expose a synchronous way to read the state */
	if (IS_HASWELL(dev))
		PIPE_CONF_CHECK_I(ips_enabled);

	PIPE_CONF_CHECK_I(double_wide);

	PIPE_CONF_CHECK_X(ddi_pll_sel);

	PIPE_CONF_CHECK_I(shared_dpll);
	PIPE_CONF_CHECK_X(dpll_hw_state.dpll);
	PIPE_CONF_CHECK_X(dpll_hw_state.dpll_md);
	PIPE_CONF_CHECK_X(dpll_hw_state.fp0);
	PIPE_CONF_CHECK_X(dpll_hw_state.fp1);
	PIPE_CONF_CHECK_X(dpll_hw_state.wrpll);
	PIPE_CONF_CHECK_X(dpll_hw_state.spll);
	PIPE_CONF_CHECK_X(dpll_hw_state.ctrl1);
	PIPE_CONF_CHECK_X(dpll_hw_state.cfgcr1);
	PIPE_CONF_CHECK_X(dpll_hw_state.cfgcr2);

	if (IS_G4X(dev) || INTEL_INFO(dev)->gen >= 5)
		PIPE_CONF_CHECK_I(pipe_bpp);

	PIPE_CONF_CHECK_CLOCK_FUZZY(base.adjusted_mode.crtc_clock);
	PIPE_CONF_CHECK_CLOCK_FUZZY(port_clock);

#undef PIPE_CONF_CHECK_X
#undef PIPE_CONF_CHECK_I
#undef PIPE_CONF_CHECK_I_ALT
#undef PIPE_CONF_CHECK_FLAGS
#undef PIPE_CONF_CHECK_CLOCK_FUZZY
#undef PIPE_CONF_QUIRK
#undef INTEL_ERR_OR_DBG_KMS

	return ret;
}

static void check_wm_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct skl_ddb_allocation hw_ddb, *sw_ddb;
	struct intel_crtc *intel_crtc;
	int plane;

	if (INTEL_INFO(dev)->gen < 9)
		return;

	skl_ddb_get_hw_state(dev_priv, &hw_ddb);
	sw_ddb = &dev_priv->wm.skl_hw.ddb;

	for_each_intel_crtc(dev, intel_crtc) {
		struct skl_ddb_entry *hw_entry, *sw_entry;
		const enum pipe pipe = intel_crtc->pipe;

		if (!intel_crtc->active)
			continue;

		/* planes */
		for_each_plane(dev_priv, pipe, plane) {
			hw_entry = &hw_ddb.plane[pipe][plane];
			sw_entry = &sw_ddb->plane[pipe][plane];

			if (skl_ddb_entry_equal(hw_entry, sw_entry))
				continue;

			DRM_ERROR("mismatch in DDB state pipe %c plane %d "
				  "(expected (%u,%u), found (%u,%u))\n",
				  pipe_name(pipe), plane + 1,
				  sw_entry->start, sw_entry->end,
				  hw_entry->start, hw_entry->end);
		}

		/* cursor */
		hw_entry = &hw_ddb.plane[pipe][PLANE_CURSOR];
		sw_entry = &sw_ddb->plane[pipe][PLANE_CURSOR];

		if (skl_ddb_entry_equal(hw_entry, sw_entry))
			continue;

		DRM_ERROR("mismatch in DDB state pipe %c cursor "
			  "(expected (%u,%u), found (%u,%u))\n",
			  pipe_name(pipe),
			  sw_entry->start, sw_entry->end,
			  hw_entry->start, hw_entry->end);
	}
}

static void
check_connector_state(struct drm_device *dev,
		      struct drm_atomic_state *old_state)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector *connector;
	int i;

	for_each_connector_in_state(old_state, connector, old_conn_state, i) {
		struct drm_encoder *encoder = connector->encoder;
		struct drm_connector_state *state = connector->state;

		/* This also checks the encoder/connector hw state with the
		 * ->get_hw_state callbacks. */
		intel_connector_check_state(to_intel_connector(connector));

		I915_STATE_WARN(state->best_encoder != encoder,
		     "connector's atomic encoder doesn't match legacy encoder\n");
	}
}

static void
check_encoder_state(struct drm_device *dev)
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
check_crtc_state(struct drm_device *dev, struct drm_atomic_state *old_state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *encoder;
	struct drm_crtc_state *old_crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
		struct intel_crtc_state *pipe_config, *sw_config;
		bool active;

		if (!needs_modeset(crtc->state) &&
		    !to_intel_crtc_state(crtc->state)->update_pipe)
			continue;

		__drm_atomic_helper_crtc_destroy_state(crtc, old_crtc_state);
		pipe_config = to_intel_crtc_state(old_crtc_state);
		memset(pipe_config, 0, sizeof(*pipe_config));
		pipe_config->base.crtc = crtc;
		pipe_config->base.state = old_state;

		DRM_DEBUG_KMS("[CRTC:%d]\n",
			      crtc->base.id);

		active = dev_priv->display.get_pipe_config(intel_crtc,
							   pipe_config);

		/* hw state is inconsistent with the pipe quirk */
		if ((intel_crtc->pipe == PIPE_A && dev_priv->quirks & QUIRK_PIPEA_FORCE) ||
		    (intel_crtc->pipe == PIPE_B && dev_priv->quirks & QUIRK_PIPEB_FORCE))
			active = crtc->state->active;

		I915_STATE_WARN(crtc->state->active != active,
		     "crtc active state doesn't match with hw state "
		     "(expected %i, found %i)\n", crtc->state->active, active);

		I915_STATE_WARN(intel_crtc->active != crtc->state->active,
		     "transitional active state does not match atomic hw state "
		     "(expected %i, found %i)\n", crtc->state->active, intel_crtc->active);

		for_each_encoder_on_crtc(dev, crtc, encoder) {
			enum pipe pipe;

			active = encoder->get_hw_state(encoder, &pipe);
			I915_STATE_WARN(active != crtc->state->active,
				"[ENCODER:%i] active %i with crtc active %i\n",
				encoder->base.base.id, active, crtc->state->active);

			I915_STATE_WARN(active && intel_crtc->pipe != pipe,
					"Encoder connected to wrong pipe %c\n",
					pipe_name(pipe));

			if (active)
				encoder->get_config(encoder, pipe_config);
		}

		if (!crtc->state->active)
			continue;

		sw_config = to_intel_crtc_state(crtc->state);
		if (!intel_pipe_config_compare(dev, sw_config,
					       pipe_config, false)) {
			I915_STATE_WARN(1, "pipe state doesn't match!\n");
			intel_dump_pipe_config(intel_crtc, pipe_config,
					       "[hw state]");
			intel_dump_pipe_config(intel_crtc, sw_config,
					       "[sw state]");
		}
	}
}

static void
check_shared_dpll_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc;
	struct intel_dpll_hw_state dpll_hw_state;
	int i;

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		struct intel_shared_dpll *pll = &dev_priv->shared_dplls[i];
		int enabled_crtcs = 0, active_crtcs = 0;
		bool active;

		memset(&dpll_hw_state, 0, sizeof(dpll_hw_state));

		DRM_DEBUG_KMS("%s\n", pll->name);

		active = pll->get_hw_state(dev_priv, pll, &dpll_hw_state);

		I915_STATE_WARN(pll->active > hweight32(pll->config.crtc_mask),
		     "more active pll users than references: %i vs %i\n",
		     pll->active, hweight32(pll->config.crtc_mask));
		I915_STATE_WARN(pll->active && !pll->on,
		     "pll in active use but not on in sw tracking\n");
		I915_STATE_WARN(pll->on && !pll->active,
		     "pll in on but not on in use in sw tracking\n");
		I915_STATE_WARN(pll->on != active,
		     "pll on state mismatch (expected %i, found %i)\n",
		     pll->on, active);

		for_each_intel_crtc(dev, crtc) {
			if (crtc->base.state->enable && intel_crtc_to_shared_dpll(crtc) == pll)
				enabled_crtcs++;
			if (crtc->active && intel_crtc_to_shared_dpll(crtc) == pll)
				active_crtcs++;
		}
		I915_STATE_WARN(pll->active != active_crtcs,
		     "pll active crtcs mismatch (expected %i, found %i)\n",
		     pll->active, active_crtcs);
		I915_STATE_WARN(hweight32(pll->config.crtc_mask) != enabled_crtcs,
		     "pll enabled crtcs mismatch (expected %i, found %i)\n",
		     hweight32(pll->config.crtc_mask), enabled_crtcs);

		I915_STATE_WARN(pll->on && memcmp(&pll->config.hw_state, &dpll_hw_state,
				       sizeof(dpll_hw_state)),
		     "pll hw state mismatch\n");
	}
}

static void
intel_modeset_check_state(struct drm_device *dev,
			  struct drm_atomic_state *old_state)
{
	check_wm_state(dev);
	check_connector_state(dev, old_state);
	check_encoder_state(dev);
	check_crtc_state(dev, old_state);
	check_shared_dpll_state(dev);
}

void ironlake_check_encoder_dotclock(const struct intel_crtc_state *pipe_config,
				     int dotclock)
{
	/*
	 * FDI already provided one idea for the dotclock.
	 * Yell if the encoder disagrees.
	 */
	WARN(!intel_fuzzy_clock_check(pipe_config->base.adjusted_mode.crtc_clock, dotclock),
	     "FDI dotclock and encoder dotclock mismatch, fdi: %i, encoder: %i\n",
	     pipe_config->base.adjusted_mode.crtc_clock, dotclock);
}

static void update_scanline_offset(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;

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
	if (IS_GEN2(dev)) {
		const struct drm_display_mode *adjusted_mode = &crtc->config->base.adjusted_mode;
		int vtotal;

		vtotal = adjusted_mode->crtc_vtotal;
		if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
			vtotal /= 2;

		crtc->scanline_offset = vtotal - 1;
	} else if (HAS_DDI(dev) &&
		   intel_pipe_has_type(crtc, INTEL_OUTPUT_HDMI)) {
		crtc->scanline_offset = 2;
	} else
		crtc->scanline_offset = 1;
}

static void intel_modeset_clear_plls(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_shared_dpll_config *shared_dpll = NULL;
	struct intel_crtc *intel_crtc;
	struct intel_crtc_state *intel_crtc_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int i;

	if (!dev_priv->display.crtc_compute_clock)
		return;

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		int dpll;

		intel_crtc = to_intel_crtc(crtc);
		intel_crtc_state = to_intel_crtc_state(crtc_state);
		dpll = intel_crtc_state->shared_dpll;

		if (!needs_modeset(crtc_state) || dpll == DPLL_ID_PRIVATE)
			continue;

		intel_crtc_state->shared_dpll = DPLL_ID_PRIVATE;

		if (!shared_dpll)
			shared_dpll = intel_atomic_get_shared_dpll_state(state);

		shared_dpll[dpll].crtc_mask &= ~(1 << intel_crtc->pipe);
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

static int intel_modeset_all_pipes(struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int ret = 0;

	/* add all active pipes to the state */
	for_each_crtc(state->dev, crtc) {
		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		if (!crtc_state->active || needs_modeset(crtc_state))
			continue;

		crtc_state->mode_changed = true;

		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret)
			break;

		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret)
			break;
	}

	return ret;
}

static int intel_modeset_checks(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	if (!check_digital_port_conflicts(state)) {
		DRM_DEBUG_KMS("rejecting conflicting digital port configuration\n");
		return -EINVAL;
	}

	/*
	 * See if the config requires any additional preparation, e.g.
	 * to adjust global state with pipes off.  We need to do this
	 * here so we can get the modeset_pipe updated config for the new
	 * mode set on this crtc.  For other crtcs we need to use the
	 * adjusted_mode bits in the crtc directly.
	 */
	if (dev_priv->display.modeset_calc_cdclk) {
		unsigned int cdclk;

		ret = dev_priv->display.modeset_calc_cdclk(state);

		cdclk = to_intel_atomic_state(state)->cdclk;
		if (!ret && cdclk != dev_priv->cdclk_freq)
			ret = intel_modeset_all_pipes(state);

		if (ret < 0)
			return ret;
	} else
		to_intel_atomic_state(state)->cdclk = dev_priv->cdclk_freq;

	intel_modeset_clear_plls(state);

	if (IS_HASWELL(dev))
		return haswell_mode_set_planes_workaround(state);

	return 0;
}

/*
 * Handle calculation of various watermark data at the end of the atomic check
 * phase.  The code here should be run after the per-crtc and per-plane 'check'
 * handlers to ensure that all derived state has been updated.
 */
static void calc_watermark_data(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);
	struct drm_crtc *crtc;
	struct drm_crtc_state *cstate;
	struct drm_plane *plane;
	struct drm_plane_state *pstate;

	/*
	 * Calculate watermark configuration details now that derived
	 * plane/crtc state is all properly updated.
	 */
	drm_for_each_crtc(crtc, dev) {
		cstate = drm_atomic_get_existing_crtc_state(state, crtc) ?:
			crtc->state;

		if (cstate->active)
			intel_state->wm_config.num_pipes_active++;
	}
	drm_for_each_legacy_plane(plane, dev) {
		pstate = drm_atomic_get_existing_plane_state(state, plane) ?:
			plane->state;

		if (!to_intel_plane_state(pstate)->visible)
			continue;

		intel_state->wm_config.sprites_enabled = true;
		if (pstate->crtc_w != pstate->src_w >> 16 ||
		    pstate->crtc_h != pstate->src_h >> 16)
			intel_state->wm_config.sprites_scaled = true;
	}
}

/**
 * intel_atomic_check - validate state object
 * @dev: drm device
 * @state: state to validate
 */
static int intel_atomic_check(struct drm_device *dev,
			      struct drm_atomic_state *state)
{
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

		memset(&to_intel_crtc(crtc)->atomic, 0,
		       sizeof(struct intel_crtc_atomic_commit));

		/* Catch I915_MODE_FLAG_INHERITED */
		if (crtc_state->mode.private_flags != crtc->state->mode.private_flags)
			crtc_state->mode_changed = true;

		if (!crtc_state->enable) {
			if (needs_modeset(crtc_state))
				any_ms = true;
			continue;
		}

		if (!needs_modeset(crtc_state))
			continue;

		/* FIXME: For only active_changed we shouldn't need to do any
		 * state recomputation at all. */

		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret)
			return ret;

		ret = intel_modeset_pipe_config(crtc, pipe_config);
		if (ret)
			return ret;

		if (i915.fastboot &&
		    intel_pipe_config_compare(state->dev,
					to_intel_crtc_state(crtc->state),
					pipe_config, true)) {
			crtc_state->mode_changed = false;
			to_intel_crtc_state(crtc_state)->update_pipe = true;
		}

		if (needs_modeset(crtc_state)) {
			any_ms = true;

			ret = drm_atomic_add_affected_planes(state, crtc);
			if (ret)
				return ret;
		}

		intel_dump_pipe_config(to_intel_crtc(crtc), pipe_config,
				       needs_modeset(crtc_state) ?
				       "[modeset]" : "[fastset]");
	}

	if (any_ms) {
		ret = intel_modeset_checks(state);

		if (ret)
			return ret;
	} else
		intel_state->cdclk = to_i915(state->dev)->cdclk_freq;

	ret = drm_atomic_helper_check_planes(state->dev, state);
	if (ret)
		return ret;

	calc_watermark_data(state);

	return 0;
}

static int intel_atomic_prepare_commit(struct drm_device *dev,
				       struct drm_atomic_state *state,
				       bool async)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_plane_state *plane_state;
	struct drm_crtc_state *crtc_state;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	int i, ret;

	if (async) {
		DRM_DEBUG_KMS("i915 does not yet support async commit\n");
		return -EINVAL;
	}

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
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
	if (!ret && !async && !i915_reset_in_progress(&dev_priv->gpu_error)) {
		u32 reset_counter;

		reset_counter = atomic_read(&dev_priv->gpu_error.reset_counter);
		mutex_unlock(&dev->struct_mutex);

		for_each_plane_in_state(state, plane, plane_state, i) {
			struct intel_plane_state *intel_plane_state =
				to_intel_plane_state(plane_state);

			if (!intel_plane_state->wait_req)
				continue;

			ret = __i915_wait_request(intel_plane_state->wait_req,
						  reset_counter, true,
						  NULL, NULL);

			/* Swallow -EIO errors to allow updates during hw lockup. */
			if (ret == -EIO)
				ret = 0;

			if (ret)
				break;
		}

		if (!ret)
			return 0;

		mutex_lock(&dev->struct_mutex);
		drm_atomic_helper_cleanup_planes(dev, state);
	}

	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * intel_atomic_commit - commit validated state object
 * @dev: DRM device
 * @state: the top-level driver state object
 * @async: asynchronous commit
 *
 * This function commits a top-level state object that has been validated
 * with drm_atomic_helper_check().
 *
 * FIXME:  Atomic modeset support for i915 is not yet complete.  At the moment
 * we can only handle plane-related operations and do not yet support
 * asynchronous commit.
 *
 * RETURNS
 * Zero for success or -errno.
 */
static int intel_atomic_commit(struct drm_device *dev,
			       struct drm_atomic_state *state,
			       bool async)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int ret = 0;
	int i;
	bool any_ms = false;

	ret = intel_atomic_prepare_commit(dev, state, async);
	if (ret) {
		DRM_DEBUG_ATOMIC("Preparing state failed with %i\n", ret);
		return ret;
	}

	drm_atomic_helper_swap_state(dev, state);
	dev_priv->wm.config = to_intel_atomic_state(state)->wm_config;

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

		if (!needs_modeset(crtc->state))
			continue;

		any_ms = true;
		intel_pre_plane_update(intel_crtc);

		if (crtc_state->active) {
			intel_crtc_disable_planes(crtc, crtc_state->plane_mask);
			dev_priv->display.crtc_disable(crtc);
			intel_crtc->active = false;
			intel_disable_shared_dpll(intel_crtc);

			/*
			 * Underruns don't always raise
			 * interrupts, so check manually.
			 */
			intel_check_cpu_fifo_underruns(dev_priv);
			intel_check_pch_fifo_underruns(dev_priv);
		}
	}

	/* Only after disabling all output pipelines that will be changed can we
	 * update the the output configuration. */
	intel_modeset_update_crtc_state(state);

	if (any_ms) {
		intel_shared_dpll_commit(state);

		drm_atomic_helper_update_legacy_modeset_state(state->dev, state);
		modeset_update_crtc_power_domains(state);
	}

	/* Now enable the clocks, plane, pipe, and connectors that we set up. */
	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
		bool modeset = needs_modeset(crtc->state);
		bool update_pipe = !modeset &&
			to_intel_crtc_state(crtc->state)->update_pipe;
		unsigned long put_domains = 0;

		if (modeset)
			intel_display_power_get(dev_priv, POWER_DOMAIN_MODESET);

		if (modeset && crtc->state->active) {
			update_scanline_offset(to_intel_crtc(crtc));
			dev_priv->display.crtc_enable(crtc);
		}

		if (update_pipe) {
			put_domains = modeset_get_crtc_power_domains(crtc);

			/* make sure intel_modeset_check_state runs */
			any_ms = true;
		}

		if (!modeset)
			intel_pre_plane_update(intel_crtc);

		if (crtc->state->active &&
		    (crtc->state->planes_changed || update_pipe))
			drm_atomic_helper_commit_planes_on_crtc(crtc_state);

		if (put_domains)
			modeset_put_power_domains(dev_priv, put_domains);

		intel_post_plane_update(intel_crtc);

		if (modeset)
			intel_display_power_put(dev_priv, POWER_DOMAIN_MODESET);
	}

	/* FIXME: add subpixel order */

	drm_atomic_helper_wait_for_vblanks(dev, state);

	mutex_lock(&dev->struct_mutex);
	drm_atomic_helper_cleanup_planes(dev, state);
	mutex_unlock(&dev->struct_mutex);

	if (any_ms)
		intel_modeset_check_state(dev, state);

	drm_atomic_state_free(state);

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
		DRM_DEBUG_KMS("[CRTC:%d] crtc restore failed, out of memory",
			      crtc->base.id);
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

	if (ret)
out:
		drm_atomic_state_free(state);
}

#undef for_each_intel_crtc_masked

static const struct drm_crtc_funcs intel_crtc_funcs = {
	.gamma_set = intel_crtc_gamma_set,
	.set_config = drm_atomic_helper_set_config,
	.destroy = intel_crtc_destroy,
	.page_flip = intel_crtc_page_flip,
	.atomic_duplicate_state = intel_crtc_duplicate_state,
	.atomic_destroy_state = intel_crtc_destroy_state,
};

static bool ibx_pch_dpll_get_hw_state(struct drm_i915_private *dev_priv,
				      struct intel_shared_dpll *pll,
				      struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;

	if (!intel_display_power_is_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	val = I915_READ(PCH_DPLL(pll->id));
	hw_state->dpll = val;
	hw_state->fp0 = I915_READ(PCH_FP0(pll->id));
	hw_state->fp1 = I915_READ(PCH_FP1(pll->id));

	return val & DPLL_VCO_ENABLE;
}

static void ibx_pch_dpll_mode_set(struct drm_i915_private *dev_priv,
				  struct intel_shared_dpll *pll)
{
	I915_WRITE(PCH_FP0(pll->id), pll->config.hw_state.fp0);
	I915_WRITE(PCH_FP1(pll->id), pll->config.hw_state.fp1);
}

static void ibx_pch_dpll_enable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	/* PCH refclock must be enabled first */
	ibx_assert_pch_refclk_enabled(dev_priv);

	I915_WRITE(PCH_DPLL(pll->id), pll->config.hw_state.dpll);

	/* Wait for the clocks to stabilize. */
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(150);

	/* The pixel multiplier can only be updated once the
	 * DPLL is enabled and the clocks are stable.
	 *
	 * So write it again.
	 */
	I915_WRITE(PCH_DPLL(pll->id), pll->config.hw_state.dpll);
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(200);
}

static void ibx_pch_dpll_disable(struct drm_i915_private *dev_priv,
				 struct intel_shared_dpll *pll)
{
	struct drm_device *dev = dev_priv->dev;
	struct intel_crtc *crtc;

	/* Make sure no transcoder isn't still depending on us. */
	for_each_intel_crtc(dev, crtc) {
		if (intel_crtc_to_shared_dpll(crtc) == pll)
			assert_pch_transcoder_disabled(dev_priv, crtc->pipe);
	}

	I915_WRITE(PCH_DPLL(pll->id), 0);
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(200);
}

static char *ibx_pch_dpll_names[] = {
	"PCH DPLL A",
	"PCH DPLL B",
};

static void ibx_pch_dpll_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	dev_priv->num_shared_dpll = 2;

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		dev_priv->shared_dplls[i].id = i;
		dev_priv->shared_dplls[i].name = ibx_pch_dpll_names[i];
		dev_priv->shared_dplls[i].mode_set = ibx_pch_dpll_mode_set;
		dev_priv->shared_dplls[i].enable = ibx_pch_dpll_enable;
		dev_priv->shared_dplls[i].disable = ibx_pch_dpll_disable;
		dev_priv->shared_dplls[i].get_hw_state =
			ibx_pch_dpll_get_hw_state;
	}
}

static void intel_shared_dpll_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (HAS_DDI(dev))
		intel_ddi_pll_init(dev);
	else if (HAS_PCH_IBX(dev) || HAS_PCH_CPT(dev))
		ibx_pch_dpll_init(dev);
	else
		dev_priv->num_shared_dpll = 0;

	BUG_ON(dev_priv->num_shared_dpll > I915_NUM_PLLS);
}

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
		       const struct drm_plane_state *new_state)
{
	struct drm_device *dev = plane->dev;
	struct drm_framebuffer *fb = new_state->fb;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct drm_i915_gem_object *old_obj = intel_fb_obj(plane->state->fb);
	int ret = 0;

	if (!obj && !old_obj)
		return 0;

	if (old_obj) {
		struct drm_crtc_state *crtc_state =
			drm_atomic_get_existing_crtc_state(new_state->state, plane->state->crtc);

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
		if (needs_modeset(crtc_state))
			ret = i915_gem_object_wait_rendering(old_obj, true);

		/* Swallow -EIO errors to allow updates during hw lockup. */
		if (ret && ret != -EIO)
			return ret;
	}

	/* For framebuffer backed by dmabuf, wait for fence */
	if (obj && obj->base.dma_buf) {
		ret = reservation_object_wait_timeout_rcu(obj->base.dma_buf->resv,
							  false, true,
							  MAX_SCHEDULE_TIMEOUT);
		if (ret == -ERESTARTSYS)
			return ret;

		WARN_ON(ret < 0);
	}

	if (!obj) {
		ret = 0;
	} else if (plane->type == DRM_PLANE_TYPE_CURSOR &&
	    INTEL_INFO(dev)->cursor_needs_physical) {
		int align = IS_I830(dev) ? 16 * 1024 : 256;
		ret = i915_gem_object_attach_phys(obj, align);
		if (ret)
			DRM_DEBUG_KMS("failed to attach phys object\n");
	} else {
		ret = intel_pin_and_fence_fb_obj(plane, fb, new_state);
	}

	if (ret == 0) {
		if (obj) {
			struct intel_plane_state *plane_state =
				to_intel_plane_state(new_state);

			i915_gem_request_assign(&plane_state->wait_req,
						obj->last_write_req);
		}

		i915_gem_track_fb(old_obj, obj, intel_plane->frontbuffer_bit);
	}

	return ret;
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
		       const struct drm_plane_state *old_state)
{
	struct drm_device *dev = plane->dev;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_plane_state *old_intel_state;
	struct drm_i915_gem_object *old_obj = intel_fb_obj(old_state->fb);
	struct drm_i915_gem_object *obj = intel_fb_obj(plane->state->fb);

	old_intel_state = to_intel_plane_state(old_state);

	if (!obj && !old_obj)
		return;

	if (old_obj && (plane->type != DRM_PLANE_TYPE_CURSOR ||
	    !INTEL_INFO(dev)->cursor_needs_physical))
		intel_unpin_fb_obj(old_state->fb, old_state);

	/* prepare_fb aborted? */
	if ((old_obj && (old_obj->frontbuffer_bits & intel_plane->frontbuffer_bit)) ||
	    (obj && !(obj->frontbuffer_bits & intel_plane->frontbuffer_bit)))
		i915_gem_track_fb(old_obj, obj, intel_plane->frontbuffer_bit);

	i915_gem_request_assign(&old_intel_state->wait_req, NULL);

}

int
skl_max_scale(struct intel_crtc *intel_crtc, struct intel_crtc_state *crtc_state)
{
	int max_scale;
	struct drm_device *dev;
	struct drm_i915_private *dev_priv;
	int crtc_clock, cdclk;

	if (!intel_crtc || !crtc_state)
		return DRM_PLANE_HELPER_NO_SCALING;

	dev = intel_crtc->base.dev;
	dev_priv = dev->dev_private;
	crtc_clock = crtc_state->base.adjusted_mode.crtc_clock;
	cdclk = to_intel_atomic_state(crtc_state->base.state)->cdclk;

	if (WARN_ON_ONCE(!crtc_clock || cdclk < crtc_clock))
		return DRM_PLANE_HELPER_NO_SCALING;

	/*
	 * skl max scale is lower of:
	 *    close to 3 but not 3, -1 is for that purpose
	 *            or
	 *    cdclk/crtc_clock
	 */
	max_scale = min((1 << 16) * 3 - 1, (1 << 8) * ((cdclk << 8) / crtc_clock));

	return max_scale;
}

static int
intel_check_primary_plane(struct drm_plane *plane,
			  struct intel_crtc_state *crtc_state,
			  struct intel_plane_state *state)
{
	struct drm_crtc *crtc = state->base.crtc;
	struct drm_framebuffer *fb = state->base.fb;
	int min_scale = DRM_PLANE_HELPER_NO_SCALING;
	int max_scale = DRM_PLANE_HELPER_NO_SCALING;
	bool can_position = false;

	/* use scaler when colorkey is not required */
	if (INTEL_INFO(plane->dev)->gen >= 9 &&
	    state->ckey.flags == I915_SET_COLORKEY_NONE) {
		min_scale = 1;
		max_scale = skl_max_scale(to_intel_crtc(crtc), crtc_state);
		can_position = true;
	}

	return drm_plane_helper_check_update(plane, crtc, fb, &state->src,
					     &state->dst, &state->clip,
					     min_scale, max_scale,
					     can_position, true,
					     &state->visible);
}

static void
intel_commit_primary_plane(struct drm_plane *plane,
			   struct intel_plane_state *state)
{
	struct drm_crtc *crtc = state->base.crtc;
	struct drm_framebuffer *fb = state->base.fb;
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	crtc = crtc ? crtc : plane->crtc;

	dev_priv->display.update_primary_plane(crtc, fb,
					       state->src.x1 >> 16,
					       state->src.y1 >> 16);
}

static void
intel_disable_primary_plane(struct drm_plane *plane,
			    struct drm_crtc *crtc)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->display.update_primary_plane(crtc, NULL, 0, 0);
}

static void intel_begin_crtc_commit(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_crtc_state *old_intel_state =
		to_intel_crtc_state(old_crtc_state);
	bool modeset = needs_modeset(crtc->state);

	if (intel_crtc->atomic.update_wm_pre)
		intel_update_watermarks(crtc);

	/* Perform vblank evasion around commit operation */
	intel_pipe_update_start(intel_crtc);

	if (modeset)
		return;

	if (to_intel_crtc_state(crtc->state)->update_pipe)
		intel_update_pipe_config(intel_crtc, old_intel_state);
	else if (INTEL_INFO(dev)->gen >= 9)
		skl_detach_scalers(intel_crtc);
}

static void intel_finish_crtc_commit(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_crtc_state)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	intel_pipe_update_end(intel_crtc);
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
	struct intel_plane *intel_plane = to_intel_plane(plane);
	drm_plane_cleanup(plane);
	kfree(intel_plane);
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

static struct drm_plane *intel_primary_plane_create(struct drm_device *dev,
						    int pipe)
{
	struct intel_plane *primary;
	struct intel_plane_state *state;
	const uint32_t *intel_primary_formats;
	unsigned int num_formats;

	primary = kzalloc(sizeof(*primary), GFP_KERNEL);
	if (primary == NULL)
		return NULL;

	state = intel_create_plane_state(&primary->base);
	if (!state) {
		kfree(primary);
		return NULL;
	}
	primary->base.state = &state->base;

	primary->can_scale = false;
	primary->max_downscale = 1;
	if (INTEL_INFO(dev)->gen >= 9) {
		primary->can_scale = true;
		state->scaler_id = -1;
	}
	primary->pipe = pipe;
	primary->plane = pipe;
	primary->frontbuffer_bit = INTEL_FRONTBUFFER_PRIMARY(pipe);
	primary->check_plane = intel_check_primary_plane;
	primary->commit_plane = intel_commit_primary_plane;
	primary->disable_plane = intel_disable_primary_plane;
	if (HAS_FBC(dev) && INTEL_INFO(dev)->gen < 4)
		primary->plane = !pipe;

	if (INTEL_INFO(dev)->gen >= 9) {
		intel_primary_formats = skl_primary_formats;
		num_formats = ARRAY_SIZE(skl_primary_formats);
	} else if (INTEL_INFO(dev)->gen >= 4) {
		intel_primary_formats = i965_primary_formats;
		num_formats = ARRAY_SIZE(i965_primary_formats);
	} else {
		intel_primary_formats = i8xx_primary_formats;
		num_formats = ARRAY_SIZE(i8xx_primary_formats);
	}

	drm_universal_plane_init(dev, &primary->base, 0,
				 &intel_plane_funcs,
				 intel_primary_formats, num_formats,
				 DRM_PLANE_TYPE_PRIMARY);

	if (INTEL_INFO(dev)->gen >= 4)
		intel_create_rotation_property(dev, primary);

	drm_plane_helper_add(&primary->base, &intel_plane_helper_funcs);

	return &primary->base;
}

void intel_create_rotation_property(struct drm_device *dev, struct intel_plane *plane)
{
	if (!dev->mode_config.rotation_property) {
		unsigned long flags = BIT(DRM_ROTATE_0) |
			BIT(DRM_ROTATE_180);

		if (INTEL_INFO(dev)->gen >= 9)
			flags |= BIT(DRM_ROTATE_90) | BIT(DRM_ROTATE_270);

		dev->mode_config.rotation_property =
			drm_mode_create_rotation_property(dev, flags);
	}
	if (dev->mode_config.rotation_property)
		drm_object_attach_property(&plane->base.base,
				dev->mode_config.rotation_property,
				plane->base.state->rotation);
}

static int
intel_check_cursor_plane(struct drm_plane *plane,
			 struct intel_crtc_state *crtc_state,
			 struct intel_plane_state *state)
{
	struct drm_crtc *crtc = crtc_state->base.crtc;
	struct drm_framebuffer *fb = state->base.fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	unsigned stride;
	int ret;

	ret = drm_plane_helper_check_update(plane, crtc, fb, &state->src,
					    &state->dst, &state->clip,
					    DRM_PLANE_HELPER_NO_SCALING,
					    DRM_PLANE_HELPER_NO_SCALING,
					    true, true, &state->visible);
	if (ret)
		return ret;

	/* if we want to turn off the cursor ignore width and height */
	if (!obj)
		return 0;

	/* Check for which cursor types we support */
	if (!cursor_size_ok(plane->dev, state->base.crtc_w, state->base.crtc_h)) {
		DRM_DEBUG("Cursor dimension %dx%d not supported\n",
			  state->base.crtc_w, state->base.crtc_h);
		return -EINVAL;
	}

	stride = roundup_pow_of_two(state->base.crtc_w) * 4;
	if (obj->base.size < stride * state->base.crtc_h) {
		DRM_DEBUG_KMS("buffer is too small\n");
		return -ENOMEM;
	}

	if (fb->modifier[0] != DRM_FORMAT_MOD_NONE) {
		DRM_DEBUG_KMS("cursor cannot be tiled\n");
		return -EINVAL;
	}

	return 0;
}

static void
intel_disable_cursor_plane(struct drm_plane *plane,
			   struct drm_crtc *crtc)
{
	intel_crtc_update_cursor(crtc, false);
}

static void
intel_commit_cursor_plane(struct drm_plane *plane,
			  struct intel_plane_state *state)
{
	struct drm_crtc *crtc = state->base.crtc;
	struct drm_device *dev = plane->dev;
	struct intel_crtc *intel_crtc;
	struct drm_i915_gem_object *obj = intel_fb_obj(state->base.fb);
	uint32_t addr;

	crtc = crtc ? crtc : plane->crtc;
	intel_crtc = to_intel_crtc(crtc);

	if (intel_crtc->cursor_bo == obj)
		goto update;

	if (!obj)
		addr = 0;
	else if (!INTEL_INFO(dev)->cursor_needs_physical)
		addr = i915_gem_obj_ggtt_offset(obj);
	else
		addr = obj->phys_handle->busaddr;

	intel_crtc->cursor_addr = addr;
	intel_crtc->cursor_bo = obj;

update:
	intel_crtc_update_cursor(crtc, state->visible);
}

static struct drm_plane *intel_cursor_plane_create(struct drm_device *dev,
						   int pipe)
{
	struct intel_plane *cursor;
	struct intel_plane_state *state;

	cursor = kzalloc(sizeof(*cursor), GFP_KERNEL);
	if (cursor == NULL)
		return NULL;

	state = intel_create_plane_state(&cursor->base);
	if (!state) {
		kfree(cursor);
		return NULL;
	}
	cursor->base.state = &state->base;

	cursor->can_scale = false;
	cursor->max_downscale = 1;
	cursor->pipe = pipe;
	cursor->plane = pipe;
	cursor->frontbuffer_bit = INTEL_FRONTBUFFER_CURSOR(pipe);
	cursor->check_plane = intel_check_cursor_plane;
	cursor->commit_plane = intel_commit_cursor_plane;
	cursor->disable_plane = intel_disable_cursor_plane;

	drm_universal_plane_init(dev, &cursor->base, 0,
				 &intel_plane_funcs,
				 intel_cursor_formats,
				 ARRAY_SIZE(intel_cursor_formats),
				 DRM_PLANE_TYPE_CURSOR);

	if (INTEL_INFO(dev)->gen >= 4) {
		if (!dev->mode_config.rotation_property)
			dev->mode_config.rotation_property =
				drm_mode_create_rotation_property(dev,
							BIT(DRM_ROTATE_0) |
							BIT(DRM_ROTATE_180));
		if (dev->mode_config.rotation_property)
			drm_object_attach_property(&cursor->base.base,
				dev->mode_config.rotation_property,
				state->base.rotation);
	}

	if (INTEL_INFO(dev)->gen >=9)
		state->scaler_id = -1;

	drm_plane_helper_add(&cursor->base, &intel_plane_helper_funcs);

	return &cursor->base;
}

static void skl_init_scalers(struct drm_device *dev, struct intel_crtc *intel_crtc,
	struct intel_crtc_state *crtc_state)
{
	int i;
	struct intel_scaler *intel_scaler;
	struct intel_crtc_scaler_state *scaler_state = &crtc_state->scaler_state;

	for (i = 0; i < intel_crtc->num_scalers; i++) {
		intel_scaler = &scaler_state->scalers[i];
		intel_scaler->in_use = 0;
		intel_scaler->mode = PS_SCALER_MODE_DYN;
	}

	scaler_state->scaler_id = -1;
}

static void intel_crtc_init(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc;
	struct intel_crtc_state *crtc_state = NULL;
	struct drm_plane *primary = NULL;
	struct drm_plane *cursor = NULL;
	int i, ret;

	intel_crtc = kzalloc(sizeof(*intel_crtc), GFP_KERNEL);
	if (intel_crtc == NULL)
		return;

	crtc_state = kzalloc(sizeof(*crtc_state), GFP_KERNEL);
	if (!crtc_state)
		goto fail;
	intel_crtc->config = crtc_state;
	intel_crtc->base.state = &crtc_state->base;
	crtc_state->base.crtc = &intel_crtc->base;

	/* initialize shared scalers */
	if (INTEL_INFO(dev)->gen >= 9) {
		if (pipe == PIPE_C)
			intel_crtc->num_scalers = 1;
		else
			intel_crtc->num_scalers = SKL_NUM_SCALERS;

		skl_init_scalers(dev, intel_crtc, crtc_state);
	}

	primary = intel_primary_plane_create(dev, pipe);
	if (!primary)
		goto fail;

	cursor = intel_cursor_plane_create(dev, pipe);
	if (!cursor)
		goto fail;

	ret = drm_crtc_init_with_planes(dev, &intel_crtc->base, primary,
					cursor, &intel_crtc_funcs);
	if (ret)
		goto fail;

	drm_mode_crtc_set_gamma_size(&intel_crtc->base, 256);
	for (i = 0; i < 256; i++) {
		intel_crtc->lut_r[i] = i;
		intel_crtc->lut_g[i] = i;
		intel_crtc->lut_b[i] = i;
	}

	/*
	 * On gen2/3 only plane A can do fbc, but the panel fitter and lvds port
	 * is hooked to pipe B. Hence we want plane A feeding pipe B.
	 */
	intel_crtc->pipe = pipe;
	intel_crtc->plane = pipe;
	if (HAS_FBC(dev) && INTEL_INFO(dev)->gen < 4) {
		DRM_DEBUG_KMS("swapping pipes & planes for FBC\n");
		intel_crtc->plane = !pipe;
	}

	intel_crtc->cursor_base = ~0;
	intel_crtc->cursor_cntl = ~0;
	intel_crtc->cursor_size = ~0;

	intel_crtc->wm.cxsr_allowed = true;

	BUG_ON(pipe >= ARRAY_SIZE(dev_priv->plane_to_crtc_mapping) ||
	       dev_priv->plane_to_crtc_mapping[intel_crtc->plane] != NULL);
	dev_priv->plane_to_crtc_mapping[intel_crtc->plane] = &intel_crtc->base;
	dev_priv->pipe_to_crtc_mapping[intel_crtc->pipe] = &intel_crtc->base;

	drm_crtc_helper_add(&intel_crtc->base, &intel_helper_funcs);

	WARN_ON(drm_crtc_index(&intel_crtc->base) != intel_crtc->pipe);
	return;

fail:
	if (primary)
		drm_plane_cleanup(primary);
	if (cursor)
		drm_plane_cleanup(cursor);
	kfree(crtc_state);
	kfree(intel_crtc);
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

	if (!drmmode_crtc) {
		DRM_ERROR("no such CRTC id\n");
		return -ENOENT;
	}

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

static bool has_edp_a(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!IS_MOBILE(dev))
		return false;

	if ((I915_READ(DP_A) & DP_DETECTED) == 0)
		return false;

	if (IS_GEN5(dev) && (I915_READ(FUSE_STRAP) & ILK_eDP_A_DISABLE))
		return false;

	return true;
}

static bool intel_crt_present(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (INTEL_INFO(dev)->gen >= 9)
		return false;

	if (IS_HSW_ULT(dev) || IS_BDW_ULT(dev))
		return false;

	if (IS_CHERRYVIEW(dev))
		return false;

	if (HAS_PCH_LPT_H(dev) && I915_READ(SFUSE_STRAP) & SFUSE_STRAP_CRT_DISABLED)
		return false;

	/* DDI E can't be used if DDI A requires 4 lanes */
	if (HAS_DDI(dev) && I915_READ(DDI_BUF_CTL(PORT_A)) & DDI_A_4_LANES)
		return false;

	if (!dev_priv->vbt.int_crt_support)
		return false;

	return true;
}

static void intel_setup_outputs(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *encoder;
	bool dpd_is_edp = false;

	intel_lvds_init(dev);

	if (intel_crt_present(dev))
		intel_crt_init(dev);

	if (IS_BROXTON(dev)) {
		/*
		 * FIXME: Broxton doesn't support port detection via the
		 * DDI_BUF_CTL_A or SFUSE_STRAP registers, find another way to
		 * detect the ports.
		 */
		intel_ddi_init(dev, PORT_A);
		intel_ddi_init(dev, PORT_B);
		intel_ddi_init(dev, PORT_C);
	} else if (HAS_DDI(dev)) {
		int found;

		/*
		 * Haswell uses DDI functions to detect digital outputs.
		 * On SKL pre-D0 the strap isn't connected, so we assume
		 * it's there.
		 */
		found = I915_READ(DDI_BUF_CTL(PORT_A)) & DDI_INIT_DISPLAY_DETECTED;
		/* WaIgnoreDDIAStrap: skl */
		if (found || IS_SKYLAKE(dev) || IS_KABYLAKE(dev))
			intel_ddi_init(dev, PORT_A);

		/* DDI B, C and D detection is indicated by the SFUSE_STRAP
		 * register */
		found = I915_READ(SFUSE_STRAP);

		if (found & SFUSE_STRAP_DDIB_DETECTED)
			intel_ddi_init(dev, PORT_B);
		if (found & SFUSE_STRAP_DDIC_DETECTED)
			intel_ddi_init(dev, PORT_C);
		if (found & SFUSE_STRAP_DDID_DETECTED)
			intel_ddi_init(dev, PORT_D);
		/*
		 * On SKL we don't have a way to detect DDI-E so we rely on VBT.
		 */
		if ((IS_SKYLAKE(dev) || IS_KABYLAKE(dev)) &&
		    (dev_priv->vbt.ddi_port_info[PORT_E].supports_dp ||
		     dev_priv->vbt.ddi_port_info[PORT_E].supports_dvi ||
		     dev_priv->vbt.ddi_port_info[PORT_E].supports_hdmi))
			intel_ddi_init(dev, PORT_E);

	} else if (HAS_PCH_SPLIT(dev)) {
		int found;
		dpd_is_edp = intel_dp_is_edp(dev, PORT_D);

		if (has_edp_a(dev))
			intel_dp_init(dev, DP_A, PORT_A);

		if (I915_READ(PCH_HDMIB) & SDVO_DETECTED) {
			/* PCH SDVOB multiplex with HDMIB */
			found = intel_sdvo_init(dev, PCH_SDVOB, PORT_B);
			if (!found)
				intel_hdmi_init(dev, PCH_HDMIB, PORT_B);
			if (!found && (I915_READ(PCH_DP_B) & DP_DETECTED))
				intel_dp_init(dev, PCH_DP_B, PORT_B);
		}

		if (I915_READ(PCH_HDMIC) & SDVO_DETECTED)
			intel_hdmi_init(dev, PCH_HDMIC, PORT_C);

		if (!dpd_is_edp && I915_READ(PCH_HDMID) & SDVO_DETECTED)
			intel_hdmi_init(dev, PCH_HDMID, PORT_D);

		if (I915_READ(PCH_DP_C) & DP_DETECTED)
			intel_dp_init(dev, PCH_DP_C, PORT_C);

		if (I915_READ(PCH_DP_D) & DP_DETECTED)
			intel_dp_init(dev, PCH_DP_D, PORT_D);
	} else if (IS_VALLEYVIEW(dev)) {
		/*
		 * The DP_DETECTED bit is the latched state of the DDC
		 * SDA pin at boot. However since eDP doesn't require DDC
		 * (no way to plug in a DP->HDMI dongle) the DDC pins for
		 * eDP ports may have been muxed to an alternate function.
		 * Thus we can't rely on the DP_DETECTED bit alone to detect
		 * eDP ports. Consult the VBT as well as DP_DETECTED to
		 * detect eDP ports.
		 */
		if (I915_READ(VLV_HDMIB) & SDVO_DETECTED &&
		    !intel_dp_is_edp(dev, PORT_B))
			intel_hdmi_init(dev, VLV_HDMIB, PORT_B);
		if (I915_READ(VLV_DP_B) & DP_DETECTED ||
		    intel_dp_is_edp(dev, PORT_B))
			intel_dp_init(dev, VLV_DP_B, PORT_B);

		if (I915_READ(VLV_HDMIC) & SDVO_DETECTED &&
		    !intel_dp_is_edp(dev, PORT_C))
			intel_hdmi_init(dev, VLV_HDMIC, PORT_C);
		if (I915_READ(VLV_DP_C) & DP_DETECTED ||
		    intel_dp_is_edp(dev, PORT_C))
			intel_dp_init(dev, VLV_DP_C, PORT_C);

		if (IS_CHERRYVIEW(dev)) {
			/* eDP not supported on port D, so don't check VBT */
			if (I915_READ(CHV_HDMID) & SDVO_DETECTED)
				intel_hdmi_init(dev, CHV_HDMID, PORT_D);
			if (I915_READ(CHV_DP_D) & DP_DETECTED)
				intel_dp_init(dev, CHV_DP_D, PORT_D);
		}

		intel_dsi_init(dev);
	} else if (!IS_GEN2(dev) && !IS_PINEVIEW(dev)) {
		bool found = false;

		if (I915_READ(GEN3_SDVOB) & SDVO_DETECTED) {
			DRM_DEBUG_KMS("probing SDVOB\n");
			found = intel_sdvo_init(dev, GEN3_SDVOB, PORT_B);
			if (!found && IS_G4X(dev)) {
				DRM_DEBUG_KMS("probing HDMI on SDVOB\n");
				intel_hdmi_init(dev, GEN4_HDMIB, PORT_B);
			}

			if (!found && IS_G4X(dev))
				intel_dp_init(dev, DP_B, PORT_B);
		}

		/* Before G4X SDVOC doesn't have its own detect register */

		if (I915_READ(GEN3_SDVOB) & SDVO_DETECTED) {
			DRM_DEBUG_KMS("probing SDVOC\n");
			found = intel_sdvo_init(dev, GEN3_SDVOC, PORT_C);
		}

		if (!found && (I915_READ(GEN3_SDVOC) & SDVO_DETECTED)) {

			if (IS_G4X(dev)) {
				DRM_DEBUG_KMS("probing HDMI on SDVOC\n");
				intel_hdmi_init(dev, GEN4_HDMIC, PORT_C);
			}
			if (IS_G4X(dev))
				intel_dp_init(dev, DP_C, PORT_C);
		}

		if (IS_G4X(dev) &&
		    (I915_READ(DP_D) & DP_DETECTED))
			intel_dp_init(dev, DP_D, PORT_D);
	} else if (IS_GEN2(dev))
		intel_dvo_init(dev);

	if (SUPPORTS_TV(dev))
		intel_tv_init(dev);

	intel_psr_init(dev);

	for_each_intel_encoder(dev, encoder) {
		encoder->base.possible_crtcs = encoder->crtc_mask;
		encoder->base.possible_clones =
			intel_encoder_clones(encoder);
	}

	intel_init_pch_refclk(dev);

	drm_helper_move_panel_connectors_to_head(dev);
}

static void intel_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct drm_device *dev = fb->dev;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);

	drm_framebuffer_cleanup(fb);
	mutex_lock(&dev->struct_mutex);
	WARN_ON(!intel_fb->obj->framebuffer_references--);
	drm_gem_object_unreference(&intel_fb->obj->base);
	mutex_unlock(&dev->struct_mutex);
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
	struct drm_device *dev = fb->dev;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;

	mutex_lock(&dev->struct_mutex);
	intel_fb_obj_flush(obj, false, ORIGIN_DIRTYFB);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static const struct drm_framebuffer_funcs intel_fb_funcs = {
	.destroy = intel_user_framebuffer_destroy,
	.create_handle = intel_user_framebuffer_create_handle,
	.dirty = intel_user_framebuffer_dirty,
};

static
u32 intel_fb_pitch_limit(struct drm_device *dev, uint64_t fb_modifier,
			 uint32_t pixel_format)
{
	u32 gen = INTEL_INFO(dev)->gen;

	if (gen >= 9) {
		/* "The stride in bytes must not exceed the of the size of 8K
		 *  pixels and 32K bytes."
		 */
		 return min(8192*drm_format_plane_cpp(pixel_format, 0), 32768);
	} else if (gen >= 5 && !IS_VALLEYVIEW(dev)) {
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

static int intel_framebuffer_init(struct drm_device *dev,
				  struct intel_framebuffer *intel_fb,
				  struct drm_mode_fb_cmd2 *mode_cmd,
				  struct drm_i915_gem_object *obj)
{
	unsigned int aligned_height;
	int ret;
	u32 pitch_limit, stride_alignment;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	if (mode_cmd->flags & DRM_MODE_FB_MODIFIERS) {
		/* Enforce that fb modifier and tiling mode match, but only for
		 * X-tiled. This is needed for FBC. */
		if (!!(obj->tiling_mode == I915_TILING_X) !=
		    !!(mode_cmd->modifier[0] == I915_FORMAT_MOD_X_TILED)) {
			DRM_DEBUG("tiling_mode doesn't match fb modifier\n");
			return -EINVAL;
		}
	} else {
		if (obj->tiling_mode == I915_TILING_X)
			mode_cmd->modifier[0] = I915_FORMAT_MOD_X_TILED;
		else if (obj->tiling_mode == I915_TILING_Y) {
			DRM_DEBUG("No Y tiling for legacy addfb\n");
			return -EINVAL;
		}
	}

	/* Passed in modifier sanity checking. */
	switch (mode_cmd->modifier[0]) {
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Yf_TILED:
		if (INTEL_INFO(dev)->gen < 9) {
			DRM_DEBUG("Unsupported tiling 0x%llx!\n",
				  mode_cmd->modifier[0]);
			return -EINVAL;
		}
	case DRM_FORMAT_MOD_NONE:
	case I915_FORMAT_MOD_X_TILED:
		break;
	default:
		DRM_DEBUG("Unsupported fb modifier 0x%llx!\n",
			  mode_cmd->modifier[0]);
		return -EINVAL;
	}

	stride_alignment = intel_fb_stride_alignment(dev, mode_cmd->modifier[0],
						     mode_cmd->pixel_format);
	if (mode_cmd->pitches[0] & (stride_alignment - 1)) {
		DRM_DEBUG("pitch (%d) must be at least %u byte aligned\n",
			  mode_cmd->pitches[0], stride_alignment);
		return -EINVAL;
	}

	pitch_limit = intel_fb_pitch_limit(dev, mode_cmd->modifier[0],
					   mode_cmd->pixel_format);
	if (mode_cmd->pitches[0] > pitch_limit) {
		DRM_DEBUG("%s pitch (%u) must be at less than %d\n",
			  mode_cmd->modifier[0] != DRM_FORMAT_MOD_NONE ?
			  "tiled" : "linear",
			  mode_cmd->pitches[0], pitch_limit);
		return -EINVAL;
	}

	if (mode_cmd->modifier[0] == I915_FORMAT_MOD_X_TILED &&
	    mode_cmd->pitches[0] != obj->stride) {
		DRM_DEBUG("pitch (%d) must match tiling stride (%d)\n",
			  mode_cmd->pitches[0], obj->stride);
		return -EINVAL;
	}

	/* Reject formats not supported by any plane early. */
	switch (mode_cmd->pixel_format) {
	case DRM_FORMAT_C8:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		break;
	case DRM_FORMAT_XRGB1555:
		if (INTEL_INFO(dev)->gen > 3) {
			DRM_DEBUG("unsupported pixel format: %s\n",
				  drm_get_format_name(mode_cmd->pixel_format));
			return -EINVAL;
		}
		break;
	case DRM_FORMAT_ABGR8888:
		if (!IS_VALLEYVIEW(dev) && INTEL_INFO(dev)->gen < 9) {
			DRM_DEBUG("unsupported pixel format: %s\n",
				  drm_get_format_name(mode_cmd->pixel_format));
			return -EINVAL;
		}
		break;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
		if (INTEL_INFO(dev)->gen < 4) {
			DRM_DEBUG("unsupported pixel format: %s\n",
				  drm_get_format_name(mode_cmd->pixel_format));
			return -EINVAL;
		}
		break;
	case DRM_FORMAT_ABGR2101010:
		if (!IS_VALLEYVIEW(dev)) {
			DRM_DEBUG("unsupported pixel format: %s\n",
				  drm_get_format_name(mode_cmd->pixel_format));
			return -EINVAL;
		}
		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_VYUY:
		if (INTEL_INFO(dev)->gen < 5) {
			DRM_DEBUG("unsupported pixel format: %s\n",
				  drm_get_format_name(mode_cmd->pixel_format));
			return -EINVAL;
		}
		break;
	default:
		DRM_DEBUG("unsupported pixel format: %s\n",
			  drm_get_format_name(mode_cmd->pixel_format));
		return -EINVAL;
	}

	/* FIXME need to adjust LINOFF/TILEOFF accordingly. */
	if (mode_cmd->offsets[0] != 0)
		return -EINVAL;

	aligned_height = intel_fb_align_height(dev, mode_cmd->height,
					       mode_cmd->pixel_format,
					       mode_cmd->modifier[0]);
	/* FIXME drm helper for size checks (especially planar formats)? */
	if (obj->base.size < aligned_height * mode_cmd->pitches[0])
		return -EINVAL;

	drm_helper_mode_fill_fb_struct(&intel_fb->base, mode_cmd);
	intel_fb->obj = obj;
	intel_fb->obj->framebuffer_references++;

	ret = drm_framebuffer_init(dev, &intel_fb->base, &intel_fb_funcs);
	if (ret) {
		DRM_ERROR("framebuffer init failed %d\n", ret);
		return ret;
	}

	return 0;
}

static struct drm_framebuffer *
intel_user_framebuffer_create(struct drm_device *dev,
			      struct drm_file *filp,
			      struct drm_mode_fb_cmd2 *user_mode_cmd)
{
	struct drm_framebuffer *fb;
	struct drm_i915_gem_object *obj;
	struct drm_mode_fb_cmd2 mode_cmd = *user_mode_cmd;

	obj = to_intel_bo(drm_gem_object_lookup(dev, filp,
						mode_cmd.handles[0]));
	if (&obj->base == NULL)
		return ERR_PTR(-ENOENT);

	fb = intel_framebuffer_create(dev, &mode_cmd, obj);
	if (IS_ERR(fb))
		drm_gem_object_unreference_unlocked(&obj->base);

	return fb;
}

#ifndef CONFIG_DRM_FBDEV_EMULATION
static inline void intel_fbdev_output_poll_changed(struct drm_device *dev)
{
}
#endif

static const struct drm_mode_config_funcs intel_mode_funcs = {
	.fb_create = intel_user_framebuffer_create,
	.output_poll_changed = intel_fbdev_output_poll_changed,
	.atomic_check = intel_atomic_check,
	.atomic_commit = intel_atomic_commit,
	.atomic_state_alloc = intel_atomic_state_alloc,
	.atomic_state_clear = intel_atomic_state_clear,
};

/* Set up chip specific display functions */
static void intel_init_display(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (HAS_PCH_SPLIT(dev) || IS_G4X(dev))
		dev_priv->display.find_dpll = g4x_find_best_dpll;
	else if (IS_CHERRYVIEW(dev))
		dev_priv->display.find_dpll = chv_find_best_dpll;
	else if (IS_VALLEYVIEW(dev))
		dev_priv->display.find_dpll = vlv_find_best_dpll;
	else if (IS_PINEVIEW(dev))
		dev_priv->display.find_dpll = pnv_find_best_dpll;
	else
		dev_priv->display.find_dpll = i9xx_find_best_dpll;

	if (INTEL_INFO(dev)->gen >= 9) {
		dev_priv->display.get_pipe_config = haswell_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			skylake_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock =
			haswell_crtc_compute_clock;
		dev_priv->display.crtc_enable = haswell_crtc_enable;
		dev_priv->display.crtc_disable = haswell_crtc_disable;
		dev_priv->display.update_primary_plane =
			skylake_update_primary_plane;
	} else if (HAS_DDI(dev)) {
		dev_priv->display.get_pipe_config = haswell_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			ironlake_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock =
			haswell_crtc_compute_clock;
		dev_priv->display.crtc_enable = haswell_crtc_enable;
		dev_priv->display.crtc_disable = haswell_crtc_disable;
		dev_priv->display.update_primary_plane =
			ironlake_update_primary_plane;
	} else if (HAS_PCH_SPLIT(dev)) {
		dev_priv->display.get_pipe_config = ironlake_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			ironlake_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock =
			ironlake_crtc_compute_clock;
		dev_priv->display.crtc_enable = ironlake_crtc_enable;
		dev_priv->display.crtc_disable = ironlake_crtc_disable;
		dev_priv->display.update_primary_plane =
			ironlake_update_primary_plane;
	} else if (IS_VALLEYVIEW(dev)) {
		dev_priv->display.get_pipe_config = i9xx_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			i9xx_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock = i9xx_crtc_compute_clock;
		dev_priv->display.crtc_enable = valleyview_crtc_enable;
		dev_priv->display.crtc_disable = i9xx_crtc_disable;
		dev_priv->display.update_primary_plane =
			i9xx_update_primary_plane;
	} else {
		dev_priv->display.get_pipe_config = i9xx_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			i9xx_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock = i9xx_crtc_compute_clock;
		dev_priv->display.crtc_enable = i9xx_crtc_enable;
		dev_priv->display.crtc_disable = i9xx_crtc_disable;
		dev_priv->display.update_primary_plane =
			i9xx_update_primary_plane;
	}

	/* Returns the core display clock speed */
	if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev))
		dev_priv->display.get_display_clock_speed =
			skylake_get_display_clock_speed;
	else if (IS_BROXTON(dev))
		dev_priv->display.get_display_clock_speed =
			broxton_get_display_clock_speed;
	else if (IS_BROADWELL(dev))
		dev_priv->display.get_display_clock_speed =
			broadwell_get_display_clock_speed;
	else if (IS_HASWELL(dev))
		dev_priv->display.get_display_clock_speed =
			haswell_get_display_clock_speed;
	else if (IS_VALLEYVIEW(dev))
		dev_priv->display.get_display_clock_speed =
			valleyview_get_display_clock_speed;
	else if (IS_GEN5(dev))
		dev_priv->display.get_display_clock_speed =
			ilk_get_display_clock_speed;
	else if (IS_I945G(dev) || IS_BROADWATER(dev) ||
		 IS_GEN6(dev) || IS_IVYBRIDGE(dev))
		dev_priv->display.get_display_clock_speed =
			i945_get_display_clock_speed;
	else if (IS_GM45(dev))
		dev_priv->display.get_display_clock_speed =
			gm45_get_display_clock_speed;
	else if (IS_CRESTLINE(dev))
		dev_priv->display.get_display_clock_speed =
			i965gm_get_display_clock_speed;
	else if (IS_PINEVIEW(dev))
		dev_priv->display.get_display_clock_speed =
			pnv_get_display_clock_speed;
	else if (IS_G33(dev) || IS_G4X(dev))
		dev_priv->display.get_display_clock_speed =
			g33_get_display_clock_speed;
	else if (IS_I915G(dev))
		dev_priv->display.get_display_clock_speed =
			i915_get_display_clock_speed;
	else if (IS_I945GM(dev) || IS_845G(dev))
		dev_priv->display.get_display_clock_speed =
			i9xx_misc_get_display_clock_speed;
	else if (IS_I915GM(dev))
		dev_priv->display.get_display_clock_speed =
			i915gm_get_display_clock_speed;
	else if (IS_I865G(dev))
		dev_priv->display.get_display_clock_speed =
			i865_get_display_clock_speed;
	else if (IS_I85X(dev))
		dev_priv->display.get_display_clock_speed =
			i85x_get_display_clock_speed;
	else { /* 830 */
		WARN(!IS_I830(dev), "Unknown platform. Assuming 133 MHz CDCLK\n");
		dev_priv->display.get_display_clock_speed =
			i830_get_display_clock_speed;
	}

	if (IS_GEN5(dev)) {
		dev_priv->display.fdi_link_train = ironlake_fdi_link_train;
	} else if (IS_GEN6(dev)) {
		dev_priv->display.fdi_link_train = gen6_fdi_link_train;
	} else if (IS_IVYBRIDGE(dev)) {
		/* FIXME: detect B0+ stepping and use auto training */
		dev_priv->display.fdi_link_train = ivb_manual_fdi_link_train;
	} else if (IS_HASWELL(dev) || IS_BROADWELL(dev)) {
		dev_priv->display.fdi_link_train = hsw_fdi_link_train;
		if (IS_BROADWELL(dev)) {
			dev_priv->display.modeset_commit_cdclk =
				broadwell_modeset_commit_cdclk;
			dev_priv->display.modeset_calc_cdclk =
				broadwell_modeset_calc_cdclk;
		}
	} else if (IS_VALLEYVIEW(dev)) {
		dev_priv->display.modeset_commit_cdclk =
			valleyview_modeset_commit_cdclk;
		dev_priv->display.modeset_calc_cdclk =
			valleyview_modeset_calc_cdclk;
	} else if (IS_BROXTON(dev)) {
		dev_priv->display.modeset_commit_cdclk =
			broxton_modeset_commit_cdclk;
		dev_priv->display.modeset_calc_cdclk =
			broxton_modeset_calc_cdclk;
	}

	switch (INTEL_INFO(dev)->gen) {
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

	mutex_init(&dev_priv->pps_mutex);
}

/*
 * Some BIOSes insist on assuming the GPU's pipe A is enabled at suspend,
 * resume, or other times.  This quirk makes sure that's the case for
 * affected systems.
 */
static void quirk_pipea_force(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->quirks |= QUIRK_PIPEA_FORCE;
	DRM_INFO("applying pipe a force quirk\n");
}

static void quirk_pipeb_force(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->quirks |= QUIRK_PIPEB_FORCE;
	DRM_INFO("applying pipe b force quirk\n");
}

/*
 * Some machines (Lenovo U160) do not work with SSC on LVDS for some reason
 */
static void quirk_ssc_force_disable(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	dev_priv->quirks |= QUIRK_LVDS_SSC_DISABLE;
	DRM_INFO("applying lvds SSC disable quirk\n");
}

/*
 * A machine (e.g. Acer Aspire 5734Z) may need to invert the panel backlight
 * brightness value
 */
static void quirk_invert_brightness(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	dev_priv->quirks |= QUIRK_INVERT_BRIGHTNESS;
	DRM_INFO("applying inverted panel brightness quirk\n");
}

/* Some VBT's incorrectly indicate no backlight is present */
static void quirk_backlight_present(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
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
static void i915_disable_vga(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u8 sr1;
	i915_reg_t vga_reg = i915_vgacntrl_reg(dev);

	/* WaEnableVGAAccessThroughIOPort:ctg,elk,ilk,snb,ivb,vlv,hsw */
	vga_get_uninterruptible(dev->pdev, VGA_RSRC_LEGACY_IO);
	outb(SR01, VGA_SR_INDEX);
	sr1 = inb(VGA_SR_DATA);
	outb(sr1 | 1<<5, VGA_SR_DATA);
	vga_put(dev->pdev, VGA_RSRC_LEGACY_IO);
	udelay(300);

	I915_WRITE(vga_reg, VGA_DISP_DISABLE);
	POSTING_READ(vga_reg);
}

void intel_modeset_init_hw(struct drm_device *dev)
{
	intel_update_cdclk(dev);
	intel_prepare_ddi(dev);
	intel_init_clock_gating(dev);
	intel_enable_gt_powersave(dev);
}

void intel_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int sprite, ret;
	enum pipe pipe;
	struct intel_crtc *crtc;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;

	dev->mode_config.allow_fb_modifiers = true;

	dev->mode_config.funcs = &intel_mode_funcs;

	intel_init_quirks(dev);

	intel_init_pm(dev);

	if (INTEL_INFO(dev)->num_pipes == 0)
		return;

	/*
	 * There may be no VBT; and if the BIOS enabled SSC we can
	 * just keep using it to avoid unnecessary flicker.  Whereas if the
	 * BIOS isn't using it, don't assume it will work even if the VBT
	 * indicates as much.
	 */
	if (HAS_PCH_IBX(dev) || HAS_PCH_CPT(dev)) {
		bool bios_lvds_use_ssc = !!(I915_READ(PCH_DREF_CONTROL) &
					    DREF_SSC1_ENABLE);

		if (dev_priv->vbt.lvds_use_ssc != bios_lvds_use_ssc) {
			DRM_DEBUG_KMS("SSC %sabled by BIOS, overriding VBT which says %sabled\n",
				     bios_lvds_use_ssc ? "en" : "dis",
				     dev_priv->vbt.lvds_use_ssc ? "en" : "dis");
			dev_priv->vbt.lvds_use_ssc = bios_lvds_use_ssc;
		}
	}

	intel_init_display(dev);
	intel_init_audio(dev);

	if (IS_GEN2(dev)) {
		dev->mode_config.max_width = 2048;
		dev->mode_config.max_height = 2048;
	} else if (IS_GEN3(dev)) {
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
	} else {
		dev->mode_config.max_width = 8192;
		dev->mode_config.max_height = 8192;
	}

	if (IS_845G(dev) || IS_I865G(dev)) {
		dev->mode_config.cursor_width = IS_845G(dev) ? 64 : 512;
		dev->mode_config.cursor_height = 1023;
	} else if (IS_GEN2(dev)) {
		dev->mode_config.cursor_width = GEN2_CURSOR_WIDTH;
		dev->mode_config.cursor_height = GEN2_CURSOR_HEIGHT;
	} else {
		dev->mode_config.cursor_width = MAX_CURSOR_WIDTH;
		dev->mode_config.cursor_height = MAX_CURSOR_HEIGHT;
	}

	dev->mode_config.fb_base = dev_priv->gtt.mappable_base;

	DRM_DEBUG_KMS("%d display pipe%s available.\n",
		      INTEL_INFO(dev)->num_pipes,
		      INTEL_INFO(dev)->num_pipes > 1 ? "s" : "");

	for_each_pipe(dev_priv, pipe) {
		intel_crtc_init(dev, pipe);
		for_each_sprite(dev_priv, pipe, sprite) {
			ret = intel_plane_init(dev, pipe, sprite);
			if (ret)
				DRM_DEBUG_KMS("pipe %c sprite %c init failed: %d\n",
					      pipe_name(pipe), sprite_name(pipe, sprite), ret);
		}
	}

	intel_update_czclk(dev_priv);
	intel_update_cdclk(dev);

	intel_shared_dpll_init(dev);

	/* Just disable it once at startup */
	i915_disable_vga(dev);
	intel_setup_outputs(dev);

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
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val;

	if (INTEL_INFO(dev)->num_pipes == 1)
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

static void intel_sanitize_crtc(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	i915_reg_t reg = PIPECONF(crtc->config->cpu_transcoder);

	/* Clear any frame start delays used for debugging left by the BIOS */
	I915_WRITE(reg, I915_READ(reg) & ~PIPECONF_FRAME_START_DELAY_MASK);

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
	if (INTEL_INFO(dev)->gen < 4 && !intel_check_plane_mapping(crtc)) {
		bool plane;

		DRM_DEBUG_KMS("[CRTC:%d] wrong plane connection detected!\n",
			      crtc->base.base.id);

		/* Pipe has the wrong plane attached and the plane is active.
		 * Temporarily change the plane mapping and disable everything
		 * ...  */
		plane = crtc->plane;
		to_intel_plane_state(crtc->base.primary->state)->visible = true;
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
	if (!intel_crtc_has_encoders(crtc))
		intel_crtc_disable_noatomic(&crtc->base);

	if (crtc->active != crtc->base.state->active) {
		struct intel_encoder *encoder;

		/* This can happen either due to bugs in the get_hw_state
		 * functions or because of calls to intel_crtc_disable_noatomic,
		 * or because the pipe is force-enabled due to the
		 * pipe A quirk. */
		DRM_DEBUG_KMS("[CRTC:%d] hw state adjusted, was %s, now %s\n",
			      crtc->base.base.id,
			      crtc->base.state->enable ? "enabled" : "disabled",
			      crtc->active ? "enabled" : "disabled");

		WARN_ON(drm_atomic_set_mode_for_crtc(crtc->base.state, NULL) < 0);
		crtc->base.state->active = crtc->active;
		crtc->base.enabled = crtc->active;

		/* Because we only establish the connector -> encoder ->
		 * crtc links if something is active, this means the
		 * crtc is now deactivated. Break the links. connector
		 * -> encoder links are only establish when things are
		 *  actually up, hence no need to break them. */
		WARN_ON(crtc->active);

		for_each_encoder_on_crtc(dev, &crtc->base, encoder)
			encoder->base.crtc = NULL;
	}

	if (crtc->active || HAS_GMCH_DISPLAY(dev)) {
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
		crtc->pch_fifo_underrun_disabled = true;
	}
}

static void intel_sanitize_encoder(struct intel_encoder *encoder)
{
	struct intel_connector *connector;
	struct drm_device *dev = encoder->base.dev;
	bool active = false;

	/* We need to check both for a crtc link (meaning that the
	 * encoder is active and trying to read from a pipe) and the
	 * pipe itself being active. */
	bool has_active_crtc = encoder->base.crtc &&
		to_intel_crtc(encoder->base.crtc)->active;

	for_each_intel_connector(dev, connector) {
		if (connector->base.encoder != &encoder->base)
			continue;

		active = true;
		break;
	}

	if (active && !has_active_crtc) {
		DRM_DEBUG_KMS("[ENCODER:%d:%s] has active connectors but no active pipe!\n",
			      encoder->base.base.id,
			      encoder->base.name);

		/* Connector is active, but has no active pipe. This is
		 * fallout from our resume register restoring. Disable
		 * the encoder manually again. */
		if (encoder->base.crtc) {
			DRM_DEBUG_KMS("[ENCODER:%d:%s] manually disabled\n",
				      encoder->base.base.id,
				      encoder->base.name);
			encoder->disable(encoder);
			if (encoder->post_disable)
				encoder->post_disable(encoder);
		}
		encoder->base.crtc = NULL;

		/* Inconsistent output/port/pipe state happens presumably due to
		 * a bug in one of the get_hw_state functions. Or someplace else
		 * in our code, like the register restore mess on resume. Clamp
		 * things to off as a safer default. */
		for_each_intel_connector(dev, connector) {
			if (connector->encoder != encoder)
				continue;
			connector->base.dpms = DRM_MODE_DPMS_OFF;
			connector->base.encoder = NULL;
		}
	}
	/* Enabled encoders without active connectors will be fixed in
	 * the crtc fixup. */
}

void i915_redisable_vga_power_on(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	i915_reg_t vga_reg = i915_vgacntrl_reg(dev);

	if (!(I915_READ(vga_reg) & VGA_DISP_DISABLE)) {
		DRM_DEBUG_KMS("Something enabled VGA plane, disabling it\n");
		i915_disable_vga(dev);
	}
}

void i915_redisable_vga(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* This function can be called both from intel_modeset_setup_hw_state or
	 * at a very early point in our resume sequence, where the power well
	 * structures are not yet restored. Since this function is at a very
	 * paranoid "someone might have enabled VGA while we were not looking"
	 * level, just check if the power well is enabled instead of trying to
	 * follow the "don't touch the power well if we don't need it" policy
	 * the rest of the driver uses. */
	if (!intel_display_power_is_enabled(dev_priv, POWER_DOMAIN_VGA))
		return;

	i915_redisable_vga_power_on(dev);
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

	plane_state->visible = crtc->active &&
		primary_get_hw_state(to_intel_plane(primary));

	if (plane_state->visible)
		crtc->base.state->plane_mask |= 1 << drm_plane_index(primary);
}

static void intel_modeset_readout_hw_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe;
	struct intel_crtc *crtc;
	struct intel_encoder *encoder;
	struct intel_connector *connector;
	int i;

	for_each_intel_crtc(dev, crtc) {
		__drm_atomic_helper_crtc_destroy_state(&crtc->base, crtc->base.state);
		memset(crtc->config, 0, sizeof(*crtc->config));
		crtc->config->base.crtc = &crtc->base;

		crtc->active = dev_priv->display.get_pipe_config(crtc,
								 crtc->config);

		crtc->base.state->active = crtc->active;
		crtc->base.enabled = crtc->active;

		readout_plane_state(crtc);

		DRM_DEBUG_KMS("[CRTC:%d] hw state readout: %s\n",
			      crtc->base.base.id,
			      crtc->active ? "enabled" : "disabled");
	}

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		struct intel_shared_dpll *pll = &dev_priv->shared_dplls[i];

		pll->on = pll->get_hw_state(dev_priv, pll,
					    &pll->config.hw_state);
		pll->active = 0;
		pll->config.crtc_mask = 0;
		for_each_intel_crtc(dev, crtc) {
			if (crtc->active && intel_crtc_to_shared_dpll(crtc) == pll) {
				pll->active++;
				pll->config.crtc_mask |= 1 << crtc->pipe;
			}
		}

		DRM_DEBUG_KMS("%s hw state readout: crtc_mask 0x%08x, on %i\n",
			      pll->name, pll->config.crtc_mask, pll->on);

		if (pll->config.crtc_mask)
			intel_display_power_get(dev_priv, POWER_DOMAIN_PLLS);
	}

	for_each_intel_encoder(dev, encoder) {
		pipe = 0;

		if (encoder->get_hw_state(encoder, &pipe)) {
			crtc = to_intel_crtc(dev_priv->pipe_to_crtc_mapping[pipe]);
			encoder->base.crtc = &crtc->base;
			encoder->get_config(encoder, crtc->config);
		} else {
			encoder->base.crtc = NULL;
		}

		DRM_DEBUG_KMS("[ENCODER:%d:%s] hw state readout: %s, pipe %c\n",
			      encoder->base.base.id,
			      encoder->base.name,
			      encoder->base.crtc ? "enabled" : "disabled",
			      pipe_name(pipe));
	}

	for_each_intel_connector(dev, connector) {
		if (connector->get_hw_state(connector)) {
			connector->base.dpms = DRM_MODE_DPMS_ON;
			connector->base.encoder = &connector->encoder->base;
		} else {
			connector->base.dpms = DRM_MODE_DPMS_OFF;
			connector->base.encoder = NULL;
		}
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] hw state readout: %s\n",
			      connector->base.base.id,
			      connector->base.name,
			      connector->base.encoder ? "enabled" : "disabled");
	}

	for_each_intel_crtc(dev, crtc) {
		crtc->base.hwmode = crtc->config->base.adjusted_mode;

		memset(&crtc->base.mode, 0, sizeof(crtc->base.mode));
		if (crtc->base.state->active) {
			intel_mode_from_pipe_config(&crtc->base.mode, crtc->config);
			intel_mode_from_pipe_config(&crtc->base.state->adjusted_mode, crtc->config);
			WARN_ON(drm_atomic_set_mode_for_crtc(crtc->base.state, &crtc->base.mode));

			/*
			 * The initial mode needs to be set in order to keep
			 * the atomic core happy. It wants a valid mode if the
			 * crtc's enabled, so we do the above call.
			 *
			 * At this point some state updated by the connectors
			 * in their ->detect() callback has not run yet, so
			 * no recalculation can be done yet.
			 *
			 * Even if we could do a recalculation and modeset
			 * right now it would cause a double modeset if
			 * fbdev or userspace chooses a different initial mode.
			 *
			 * If that happens, someone indicated they wanted a
			 * mode change, which means it's safe to do a full
			 * recalculation.
			 */
			crtc->base.state->mode.private_flags = I915_MODE_FLAG_INHERITED;

			drm_calc_timestamping_constants(&crtc->base, &crtc->base.hwmode);
			update_scanline_offset(crtc);
		}
	}
}

/* Scan out the current hw modeset state,
 * and sanitizes it to the current state
 */
static void
intel_modeset_setup_hw_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe;
	struct intel_crtc *crtc;
	struct intel_encoder *encoder;
	int i;

	intel_modeset_readout_hw_state(dev);

	/* HW state is read out, now we need to sanitize this mess. */
	for_each_intel_encoder(dev, encoder) {
		intel_sanitize_encoder(encoder);
	}

	for_each_pipe(dev_priv, pipe) {
		crtc = to_intel_crtc(dev_priv->pipe_to_crtc_mapping[pipe]);
		intel_sanitize_crtc(crtc);
		intel_dump_pipe_config(crtc, crtc->config,
				       "[setup_hw_state]");
	}

	intel_modeset_update_connector_atomic_state(dev);

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		struct intel_shared_dpll *pll = &dev_priv->shared_dplls[i];

		if (!pll->on || pll->active)
			continue;

		DRM_DEBUG_KMS("%s enabled but not in use, disabling\n", pll->name);

		pll->disable(dev_priv, pll);
		pll->on = false;
	}

	if (IS_VALLEYVIEW(dev))
		vlv_wm_get_hw_state(dev);
	else if (IS_GEN9(dev))
		skl_wm_get_hw_state(dev);
	else if (HAS_PCH_SPLIT(dev))
		ilk_wm_get_hw_state(dev);

	for_each_intel_crtc(dev, crtc) {
		unsigned long put_domains;

		put_domains = modeset_get_crtc_power_domains(&crtc->base);
		if (WARN_ON(put_domains))
			modeset_put_power_domains(dev_priv, put_domains);
	}
	intel_display_set_init_power(dev_priv, false);
}

void intel_display_resume(struct drm_device *dev)
{
	struct drm_atomic_state *state = drm_atomic_state_alloc(dev);
	struct intel_connector *conn;
	struct intel_plane *plane;
	struct drm_crtc *crtc;
	int ret;

	if (!state)
		return;

	state->acquire_ctx = dev->mode_config.acquire_ctx;

	/* preserve complete old state, including dpll */
	intel_atomic_get_shared_dpll_state(state);

	for_each_crtc(dev, crtc) {
		struct drm_crtc_state *crtc_state =
			drm_atomic_get_crtc_state(state, crtc);

		ret = PTR_ERR_OR_ZERO(crtc_state);
		if (ret)
			goto err;

		/* force a restore */
		crtc_state->mode_changed = true;
	}

	for_each_intel_plane(dev, plane) {
		ret = PTR_ERR_OR_ZERO(drm_atomic_get_plane_state(state, &plane->base));
		if (ret)
			goto err;
	}

	for_each_intel_connector(dev, conn) {
		ret = PTR_ERR_OR_ZERO(drm_atomic_get_connector_state(state, &conn->base));
		if (ret)
			goto err;
	}

	intel_modeset_setup_hw_state(dev);

	i915_redisable_vga(dev);
	ret = drm_atomic_commit(state);
	if (!ret)
		return;

err:
	DRM_ERROR("Restoring old state failed with %i\n", ret);
	drm_atomic_state_free(state);
}

void intel_modeset_gem_init(struct drm_device *dev)
{
	struct drm_crtc *c;
	struct drm_i915_gem_object *obj;
	int ret;

	mutex_lock(&dev->struct_mutex);
	intel_init_gt_powersave(dev);
	mutex_unlock(&dev->struct_mutex);

	intel_modeset_init_hw(dev);

	intel_setup_overlay(dev);

	/*
	 * Make sure any fbs we allocated at startup are properly
	 * pinned & fenced.  When we do the allocation it's too early
	 * for this.
	 */
	for_each_crtc(dev, c) {
		obj = intel_fb_obj(c->primary->fb);
		if (obj == NULL)
			continue;

		mutex_lock(&dev->struct_mutex);
		ret = intel_pin_and_fence_fb_obj(c->primary,
						 c->primary->fb,
						 c->primary->state);
		mutex_unlock(&dev->struct_mutex);
		if (ret) {
			DRM_ERROR("failed to pin boot fb on pipe %d\n",
				  to_intel_crtc(c)->pipe);
			drm_framebuffer_unreference(c->primary->fb);
			c->primary->fb = NULL;
			c->primary->crtc = c->primary->state->crtc = NULL;
			update_state_fb(c->primary);
			c->state->plane_mask &= ~(1 << drm_plane_index(c->primary));
		}
	}

	intel_backlight_register(dev);
}

void intel_connector_unregister(struct intel_connector *intel_connector)
{
	struct drm_connector *connector = &intel_connector->base;

	intel_panel_destroy_backlight(connector);
	drm_connector_unregister(connector);
}

void intel_modeset_cleanup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_connector *connector;

	intel_disable_gt_powersave(dev);

	intel_backlight_unregister(dev);

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

	intel_fbc_disable(dev_priv);

	/* flush any delayed tasks or pending work */
	flush_scheduled_work();

	/* destroy the backlight and sysfs files before encoders/connectors */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct intel_connector *intel_connector;

		intel_connector = to_intel_connector(connector);
		intel_connector->unregister(intel_connector);
	}

	drm_mode_config_cleanup(dev);

	intel_cleanup_overlay(dev);

	mutex_lock(&dev->struct_mutex);
	intel_cleanup_gt_powersave(dev);
	mutex_unlock(&dev->struct_mutex);
}

/*
 * Return which encoder is currently attached for connector.
 */
struct drm_encoder *intel_best_encoder(struct drm_connector *connector)
{
	return &intel_attached_encoder(connector)->base;
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
int intel_modeset_vga_set_state(struct drm_device *dev, bool state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned reg = INTEL_INFO(dev)->gen >= 6 ? SNB_GMCH_CTRL : INTEL_GMCH_CTRL;
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
intel_display_capture_error_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_display_error_state *error;
	int transcoders[] = {
		TRANSCODER_A,
		TRANSCODER_B,
		TRANSCODER_C,
		TRANSCODER_EDP,
	};
	int i;

	if (INTEL_INFO(dev)->num_pipes == 0)
		return NULL;

	error = kzalloc(sizeof(*error), GFP_ATOMIC);
	if (error == NULL)
		return NULL;

	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
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
		if (INTEL_INFO(dev)->gen <= 3) {
			error->plane[i].size = I915_READ(DSPSIZE(i));
			error->plane[i].pos = I915_READ(DSPPOS(i));
		}
		if (INTEL_INFO(dev)->gen <= 7 && !IS_HASWELL(dev))
			error->plane[i].addr = I915_READ(DSPADDR(i));
		if (INTEL_INFO(dev)->gen >= 4) {
			error->plane[i].surface = I915_READ(DSPSURF(i));
			error->plane[i].tile_offset = I915_READ(DSPTILEOFF(i));
		}

		error->pipe[i].source = I915_READ(PIPESRC(i));

		if (HAS_GMCH_DISPLAY(dev))
			error->pipe[i].stat = I915_READ(PIPESTAT(i));
	}

	error->num_transcoders = INTEL_INFO(dev)->num_pipes;
	if (HAS_DDI(dev_priv->dev))
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
				struct drm_device *dev,
				struct intel_display_error_state *error)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	if (!error)
		return;

	err_printf(m, "Num Pipes: %d\n", INTEL_INFO(dev)->num_pipes);
	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		err_printf(m, "PWR_WELL_CTL2: %08x\n",
			   error->power_well_driver);
	for_each_pipe(dev_priv, i) {
		err_printf(m, "Pipe [%d]:\n", i);
		err_printf(m, "  Power: %s\n",
			   error->pipe[i].power_domain_on ? "on" : "off");
		err_printf(m, "  SRC: %08x\n", error->pipe[i].source);
		err_printf(m, "  STAT: %08x\n", error->pipe[i].stat);

		err_printf(m, "Plane [%d]:\n", i);
		err_printf(m, "  CNTR: %08x\n", error->plane[i].control);
		err_printf(m, "  STRIDE: %08x\n", error->plane[i].stride);
		if (INTEL_INFO(dev)->gen <= 3) {
			err_printf(m, "  SIZE: %08x\n", error->plane[i].size);
			err_printf(m, "  POS: %08x\n", error->plane[i].pos);
		}
		if (INTEL_INFO(dev)->gen <= 7 && !IS_HASWELL(dev))
			err_printf(m, "  ADDR: %08x\n", error->plane[i].addr);
		if (INTEL_INFO(dev)->gen >= 4) {
			err_printf(m, "  SURF: %08x\n", error->plane[i].surface);
			err_printf(m, "  TILEOFF: %08x\n", error->plane[i].tile_offset);
		}

		err_printf(m, "Cursor [%d]:\n", i);
		err_printf(m, "  CNTR: %08x\n", error->cursor[i].control);
		err_printf(m, "  POS: %08x\n", error->cursor[i].position);
		err_printf(m, "  BASE: %08x\n", error->cursor[i].base);
	}

	for (i = 0; i < error->num_transcoders; i++) {
		err_printf(m, "CPU transcoder: %c\n",
			   transcoder_name(error->transcoder[i].cpu_transcoder));
		err_printf(m, "  Power: %s\n",
			   error->transcoder[i].power_domain_on ? "on" : "off");
		err_printf(m, "  CONF: %08x\n", error->transcoder[i].conf);
		err_printf(m, "  HTOTAL: %08x\n", error->transcoder[i].htotal);
		err_printf(m, "  HBLANK: %08x\n", error->transcoder[i].hblank);
		err_printf(m, "  HSYNC: %08x\n", error->transcoder[i].hsync);
		err_printf(m, "  VTOTAL: %08x\n", error->transcoder[i].vtotal);
		err_printf(m, "  VBLANK: %08x\n", error->transcoder[i].vblank);
		err_printf(m, "  VSYNC: %08x\n", error->transcoder[i].vsync);
	}
}

void intel_modeset_preclose(struct drm_device *dev, struct drm_file *file)
{
	struct intel_crtc *crtc;

	for_each_intel_crtc(dev, crtc) {
		struct intel_unpin_work *work;

		spin_lock_irq(&dev->event_lock);

		work = crtc->unpin_work;

		if (work && work->event &&
		    work->event->base.file_priv == file) {
			kfree(work->event);
			work->event = NULL;
		}

		spin_unlock_irq(&dev->event_lock);
	}
}
