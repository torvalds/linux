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
#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_rect.h>
#include <linux/dma_remapping.h>

/* Primary plane formats supported by all gen */
#define COMMON_PRIMARY_FORMATS \
	DRM_FORMAT_C8, \
	DRM_FORMAT_RGB565, \
	DRM_FORMAT_XRGB8888, \
	DRM_FORMAT_ARGB8888

/* Primary plane formats for gen <= 3 */
static const uint32_t intel_primary_formats_gen2[] = {
	COMMON_PRIMARY_FORMATS,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB1555,
};

/* Primary plane formats for gen >= 4 */
static const uint32_t intel_primary_formats_gen4[] = {
	COMMON_PRIMARY_FORMATS, \
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
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

static int intel_set_mode(struct drm_crtc *crtc, struct drm_display_mode *mode,
			  int x, int y, struct drm_framebuffer *old_fb);
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
static void intel_begin_crtc_commit(struct drm_crtc *crtc);
static void intel_finish_crtc_commit(struct drm_crtc *crtc);

static struct intel_encoder *intel_find_encoder(struct intel_connector *connector, int pipe)
{
	if (!connector->mst_port)
		return connector->encoder;
	else
		return &connector->mst_port->mst_encoders[pipe]->base;
}

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

int
intel_pch_rawclk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	WARN_ON(!HAS_PCH_SPLIT(dev));

	return I915_READ(PCH_RAWCLK_FREQ) & RAWCLK_FREQ_MASK;
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

static void vlv_clock(int refclk, intel_clock_t *clock)
{
	clock->m = clock->m1 * clock->m2;
	clock->p = clock->p1 * clock->p2;
	if (WARN_ON(clock->n == 0 || clock->p == 0))
		return;
	clock->vco = DIV_ROUND_CLOSEST(refclk * clock->m, clock->n);
	clock->dot = DIV_ROUND_CLOSEST(clock->vco, clock->p);
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
static bool intel_pipe_will_have_type(struct intel_crtc *crtc, int type)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_encoder *encoder;

	for_each_intel_encoder(dev, encoder)
		if (encoder->new_crtc == crtc && encoder->type == type)
			return true;

	return false;
}

static const intel_limit_t *intel_ironlake_limit(struct intel_crtc *crtc,
						int refclk)
{
	struct drm_device *dev = crtc->base.dev;
	const intel_limit_t *limit;

	if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS)) {
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

static const intel_limit_t *intel_g4x_limit(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	const intel_limit_t *limit;

	if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS)) {
		if (intel_is_dual_link_lvds(dev))
			limit = &intel_limits_g4x_dual_channel_lvds;
		else
			limit = &intel_limits_g4x_single_channel_lvds;
	} else if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_HDMI) ||
		   intel_pipe_will_have_type(crtc, INTEL_OUTPUT_ANALOG)) {
		limit = &intel_limits_g4x_hdmi;
	} else if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_SDVO)) {
		limit = &intel_limits_g4x_sdvo;
	} else /* The option is for other outputs */
		limit = &intel_limits_i9xx_sdvo;

	return limit;
}

static const intel_limit_t *intel_limit(struct intel_crtc *crtc, int refclk)
{
	struct drm_device *dev = crtc->base.dev;
	const intel_limit_t *limit;

	if (HAS_PCH_SPLIT(dev))
		limit = intel_ironlake_limit(crtc, refclk);
	else if (IS_G4X(dev)) {
		limit = intel_g4x_limit(crtc);
	} else if (IS_PINEVIEW(dev)) {
		if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_pineview_lvds;
		else
			limit = &intel_limits_pineview_sdvo;
	} else if (IS_CHERRYVIEW(dev)) {
		limit = &intel_limits_chv;
	} else if (IS_VALLEYVIEW(dev)) {
		limit = &intel_limits_vlv;
	} else if (!IS_GEN2(dev)) {
		if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_i9xx_lvds;
		else
			limit = &intel_limits_i9xx_sdvo;
	} else {
		if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_i8xx_lvds;
		else if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_DVO))
			limit = &intel_limits_i8xx_dvo;
		else
			limit = &intel_limits_i8xx_dac;
	}
	return limit;
}

/* m1 is reserved as 0 in Pineview, n is a ring counter */
static void pineview_clock(int refclk, intel_clock_t *clock)
{
	clock->m = clock->m2 + 2;
	clock->p = clock->p1 * clock->p2;
	if (WARN_ON(clock->n == 0 || clock->p == 0))
		return;
	clock->vco = DIV_ROUND_CLOSEST(refclk * clock->m, clock->n);
	clock->dot = DIV_ROUND_CLOSEST(clock->vco, clock->p);
}

static uint32_t i9xx_dpll_compute_m(struct dpll *dpll)
{
	return 5 * (dpll->m1 + 2) + (dpll->m2 + 2);
}

static void i9xx_clock(int refclk, intel_clock_t *clock)
{
	clock->m = i9xx_dpll_compute_m(clock);
	clock->p = clock->p1 * clock->p2;
	if (WARN_ON(clock->n + 2 == 0 || clock->p == 0))
		return;
	clock->vco = DIV_ROUND_CLOSEST(refclk * clock->m, clock->n + 2);
	clock->dot = DIV_ROUND_CLOSEST(clock->vco, clock->p);
}

static void chv_clock(int refclk, intel_clock_t *clock)
{
	clock->m = clock->m1 * clock->m2;
	clock->p = clock->p1 * clock->p2;
	if (WARN_ON(clock->n == 0 || clock->p == 0))
		return;
	clock->vco = DIV_ROUND_CLOSEST_ULL((uint64_t)refclk * clock->m,
			clock->n << 22);
	clock->dot = DIV_ROUND_CLOSEST(clock->vco, clock->p);
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

	if (!IS_PINEVIEW(dev) && !IS_VALLEYVIEW(dev))
		if (clock->m1 <= clock->m2)
			INTELPllInvalid("m1 <= m2\n");

	if (!IS_VALLEYVIEW(dev)) {
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

static bool
i9xx_find_best_dpll(const intel_limit_t *limit, struct intel_crtc *crtc,
		    int target, int refclk, intel_clock_t *match_clock,
		    intel_clock_t *best_clock)
{
	struct drm_device *dev = crtc->base.dev;
	intel_clock_t clock;
	int err = target;

	if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS)) {
		/*
		 * For LVDS just rely on its current settings for dual-channel.
		 * We haven't figured out how to reliably set up different
		 * single/dual channel state, if we even can.
		 */
		if (intel_is_dual_link_lvds(dev))
			clock.p2 = limit->p2.p2_fast;
		else
			clock.p2 = limit->p2.p2_slow;
	} else {
		if (target < limit->p2.dot_limit)
			clock.p2 = limit->p2.p2_slow;
		else
			clock.p2 = limit->p2.p2_fast;
	}

	memset(best_clock, 0, sizeof(*best_clock));

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

					i9xx_clock(refclk, &clock);
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
pnv_find_best_dpll(const intel_limit_t *limit, struct intel_crtc *crtc,
		   int target, int refclk, intel_clock_t *match_clock,
		   intel_clock_t *best_clock)
{
	struct drm_device *dev = crtc->base.dev;
	intel_clock_t clock;
	int err = target;

	if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS)) {
		/*
		 * For LVDS just rely on its current settings for dual-channel.
		 * We haven't figured out how to reliably set up different
		 * single/dual channel state, if we even can.
		 */
		if (intel_is_dual_link_lvds(dev))
			clock.p2 = limit->p2.p2_fast;
		else
			clock.p2 = limit->p2.p2_slow;
	} else {
		if (target < limit->p2.dot_limit)
			clock.p2 = limit->p2.p2_slow;
		else
			clock.p2 = limit->p2.p2_fast;
	}

	memset(best_clock, 0, sizeof(*best_clock));

	for (clock.m1 = limit->m1.min; clock.m1 <= limit->m1.max;
	     clock.m1++) {
		for (clock.m2 = limit->m2.min;
		     clock.m2 <= limit->m2.max; clock.m2++) {
			for (clock.n = limit->n.min;
			     clock.n <= limit->n.max; clock.n++) {
				for (clock.p1 = limit->p1.min;
					clock.p1 <= limit->p1.max; clock.p1++) {
					int this_err;

					pineview_clock(refclk, &clock);
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
g4x_find_best_dpll(const intel_limit_t *limit, struct intel_crtc *crtc,
		   int target, int refclk, intel_clock_t *match_clock,
		   intel_clock_t *best_clock)
{
	struct drm_device *dev = crtc->base.dev;
	intel_clock_t clock;
	int max_n;
	bool found;
	/* approximately equals target * 0.00585 */
	int err_most = (target >> 8) + (target >> 9);
	found = false;

	if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS)) {
		if (intel_is_dual_link_lvds(dev))
			clock.p2 = limit->p2.p2_fast;
		else
			clock.p2 = limit->p2.p2_slow;
	} else {
		if (target < limit->p2.dot_limit)
			clock.p2 = limit->p2.p2_slow;
		else
			clock.p2 = limit->p2.p2_fast;
	}

	memset(best_clock, 0, sizeof(*best_clock));
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

					i9xx_clock(refclk, &clock);
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

static bool
vlv_find_best_dpll(const intel_limit_t *limit, struct intel_crtc *crtc,
		   int target, int refclk, intel_clock_t *match_clock,
		   intel_clock_t *best_clock)
{
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
					unsigned int ppm, diff;

					clock.m2 = DIV_ROUND_CLOSEST(target * clock.p * clock.n,
								     refclk * clock.m1);

					vlv_clock(refclk, &clock);

					if (!intel_PLL_is_valid(dev, limit,
								&clock))
						continue;

					diff = abs(clock.dot - target);
					ppm = div_u64(1000000ULL * diff, target);

					if (ppm < 100 && clock.p > best_clock->p) {
						bestppm = 0;
						*best_clock = clock;
						found = true;
					}

					if (bestppm >= 10 && ppm < bestppm - 10) {
						bestppm = ppm;
						*best_clock = clock;
						found = true;
					}
				}
			}
		}
	}

	return found;
}

static bool
chv_find_best_dpll(const intel_limit_t *limit, struct intel_crtc *crtc,
		   int target, int refclk, intel_clock_t *match_clock,
		   intel_clock_t *best_clock)
{
	struct drm_device *dev = crtc->base.dev;
	intel_clock_t clock;
	uint64_t m2;
	int found = false;

	memset(best_clock, 0, sizeof(*best_clock));

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

			clock.p = clock.p1 * clock.p2;

			m2 = DIV_ROUND_CLOSEST_ULL(((uint64_t)target * clock.p *
					clock.n) << 22, refclk * clock.m1);

			if (m2 > INT_MAX/clock.m1)
				continue;

			clock.m2 = m2;

			chv_clock(refclk, &clock);

			if (!intel_PLL_is_valid(dev, limit, &clock))
				continue;

			/* based on hardware requirement, prefer bigger p
			 */
			if (clock.p > best_clock->p) {
				*best_clock = clock;
				found = true;
			}
		}
	}

	return found;
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
	 */
	return intel_crtc->active && crtc->primary->fb &&
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
	u32 reg = PIPEDSL(pipe);
	u32 line1, line2;
	u32 line_mask;

	if (IS_GEN2(dev))
		line_mask = DSL_LINEMASK_GEN2;
	else
		line_mask = DSL_LINEMASK_GEN3;

	line1 = I915_READ(reg) & line_mask;
	mdelay(5);
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
		int reg = PIPECONF(cpu_transcoder);

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

/*
 * ibx_digital_port_connected - is the specified port connected?
 * @dev_priv: i915 private structure
 * @port: the port to test
 *
 * Returns true if @port is connected, false otherwise.
 */
bool ibx_digital_port_connected(struct drm_i915_private *dev_priv,
				struct intel_digital_port *port)
{
	u32 bit;

	if (HAS_PCH_IBX(dev_priv->dev)) {
		switch (port->port) {
		case PORT_B:
			bit = SDE_PORTB_HOTPLUG;
			break;
		case PORT_C:
			bit = SDE_PORTC_HOTPLUG;
			break;
		case PORT_D:
			bit = SDE_PORTD_HOTPLUG;
			break;
		default:
			return true;
		}
	} else {
		switch (port->port) {
		case PORT_B:
			bit = SDE_PORTB_HOTPLUG_CPT;
			break;
		case PORT_C:
			bit = SDE_PORTC_HOTPLUG_CPT;
			break;
		case PORT_D:
			bit = SDE_PORTD_HOTPLUG_CPT;
			break;
		default:
			return true;
		}
	}

	return I915_READ(SDEISR) & bit;
}

static const char *state_string(bool enabled)
{
	return enabled ? "on" : "off";
}

/* Only for pre-ILK configs */
void assert_pll(struct drm_i915_private *dev_priv,
		enum pipe pipe, bool state)
{
	int reg;
	u32 val;
	bool cur_state;

	reg = DPLL(pipe);
	val = I915_READ(reg);
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

	mutex_lock(&dev_priv->dpio_lock);
	val = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	mutex_unlock(&dev_priv->dpio_lock);

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
	int reg;
	u32 val;
	bool cur_state;
	enum transcoder cpu_transcoder = intel_pipe_to_cpu_transcoder(dev_priv,
								      pipe);

	if (HAS_DDI(dev_priv->dev)) {
		/* DDI does not have a specific FDI_TX register */
		reg = TRANS_DDI_FUNC_CTL(cpu_transcoder);
		val = I915_READ(reg);
		cur_state = !!(val & TRANS_DDI_FUNC_ENABLE);
	} else {
		reg = FDI_TX_CTL(pipe);
		val = I915_READ(reg);
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
	int reg;
	u32 val;
	bool cur_state;

	reg = FDI_RX_CTL(pipe);
	val = I915_READ(reg);
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
	int reg;
	u32 val;

	/* ILK FDI PLL is always enabled */
	if (INTEL_INFO(dev_priv->dev)->gen == 5)
		return;

	/* On Haswell, DDI ports are responsible for the FDI PLL setup */
	if (HAS_DDI(dev_priv->dev))
		return;

	reg = FDI_TX_CTL(pipe);
	val = I915_READ(reg);
	I915_STATE_WARN(!(val & FDI_TX_PLL_ENABLE), "FDI TX PLL assertion failure, should be active but is disabled\n");
}

void assert_fdi_rx_pll(struct drm_i915_private *dev_priv,
		       enum pipe pipe, bool state)
{
	int reg;
	u32 val;
	bool cur_state;

	reg = FDI_RX_CTL(pipe);
	val = I915_READ(reg);
	cur_state = !!(val & FDI_RX_PLL_ENABLE);
	I915_STATE_WARN(cur_state != state,
	     "FDI RX PLL assertion failure (expected %s, current %s)\n",
	     state_string(state), state_string(cur_state));
}

void assert_panel_unlocked(struct drm_i915_private *dev_priv,
			   enum pipe pipe)
{
	struct drm_device *dev = dev_priv->dev;
	int pp_reg;
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
		cur_state = I915_READ(_CURACNTR) & CURSOR_ENABLE;
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
	int reg;
	u32 val;
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
		reg = PIPECONF(cpu_transcoder);
		val = I915_READ(reg);
		cur_state = !!(val & PIPECONF_ENABLE);
	}

	I915_STATE_WARN(cur_state != state,
	     "pipe %c assertion failure (expected %s, current %s)\n",
	     pipe_name(pipe), state_string(state), state_string(cur_state));
}

static void assert_plane(struct drm_i915_private *dev_priv,
			 enum plane plane, bool state)
{
	int reg;
	u32 val;
	bool cur_state;

	reg = DSPCNTR(plane);
	val = I915_READ(reg);
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
	int reg, i;
	u32 val;
	int cur_pipe;

	/* Primary planes are fixed to pipes on gen4+ */
	if (INTEL_INFO(dev)->gen >= 4) {
		reg = DSPCNTR(pipe);
		val = I915_READ(reg);
		I915_STATE_WARN(val & DISPLAY_PLANE_ENABLE,
		     "plane %c assertion failure, should be disabled but not\n",
		     plane_name(pipe));
		return;
	}

	/* Need to check both planes against the pipe */
	for_each_pipe(dev_priv, i) {
		reg = DSPCNTR(i);
		val = I915_READ(reg);
		cur_pipe = (val & DISPPLANE_SEL_PIPE_MASK) >>
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
	int reg, sprite;
	u32 val;

	if (INTEL_INFO(dev)->gen >= 9) {
		for_each_sprite(dev_priv, pipe, sprite) {
			val = I915_READ(PLANE_CTL(pipe, sprite));
			I915_STATE_WARN(val & PLANE_CTL_ENABLE,
			     "plane %d assertion failure, should be off on pipe %c but is still active\n",
			     sprite, pipe_name(pipe));
		}
	} else if (IS_VALLEYVIEW(dev)) {
		for_each_sprite(dev_priv, pipe, sprite) {
			reg = SPCNTR(pipe, sprite);
			val = I915_READ(reg);
			I915_STATE_WARN(val & SP_ENABLE,
			     "sprite %c assertion failure, should be off on pipe %c but is still active\n",
			     sprite_name(pipe, sprite), pipe_name(pipe));
		}
	} else if (INTEL_INFO(dev)->gen >= 7) {
		reg = SPRCTL(pipe);
		val = I915_READ(reg);
		I915_STATE_WARN(val & SPRITE_ENABLE,
		     "sprite %c assertion failure, should be off on pipe %c but is still active\n",
		     plane_name(pipe), pipe_name(pipe));
	} else if (INTEL_INFO(dev)->gen >= 5) {
		reg = DVSCNTR(pipe);
		val = I915_READ(reg);
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
	int reg;
	u32 val;
	bool enabled;

	reg = PCH_TRANSCONF(pipe);
	val = I915_READ(reg);
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
		u32	trans_dp_ctl_reg = TRANS_DP_CTL(pipe);
		u32	trans_dp_ctl = I915_READ(trans_dp_ctl_reg);
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
				   enum pipe pipe, int reg, u32 port_sel)
{
	u32 val = I915_READ(reg);
	I915_STATE_WARN(dp_pipe_enabled(dev_priv, pipe, port_sel, val),
	     "PCH DP (0x%08x) enabled on transcoder %c, should be disabled\n",
	     reg, pipe_name(pipe));

	I915_STATE_WARN(HAS_PCH_IBX(dev_priv->dev) && (val & DP_PORT_EN) == 0
	     && (val & DP_PIPEB_SELECT),
	     "IBX PCH dp port still using transcoder B\n");
}

static void assert_pch_hdmi_disabled(struct drm_i915_private *dev_priv,
				     enum pipe pipe, int reg)
{
	u32 val = I915_READ(reg);
	I915_STATE_WARN(hdmi_pipe_enabled(dev_priv, pipe, val),
	     "PCH HDMI (0x%08x) enabled on transcoder %c, should be disabled\n",
	     reg, pipe_name(pipe));

	I915_STATE_WARN(HAS_PCH_IBX(dev_priv->dev) && (val & SDVO_ENABLE) == 0
	     && (val & SDVO_PIPE_B_SELECT),
	     "IBX PCH hdmi port still using transcoder B\n");
}

static void assert_pch_ports_disabled(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	int reg;
	u32 val;

	assert_pch_dp_disabled(dev_priv, pipe, PCH_DP_B, TRANS_DP_PORT_SEL_B);
	assert_pch_dp_disabled(dev_priv, pipe, PCH_DP_C, TRANS_DP_PORT_SEL_C);
	assert_pch_dp_disabled(dev_priv, pipe, PCH_DP_D, TRANS_DP_PORT_SEL_D);

	reg = PCH_ADPA;
	val = I915_READ(reg);
	I915_STATE_WARN(adpa_pipe_enabled(dev_priv, pipe, val),
	     "PCH VGA enabled on transcoder %c, should be disabled\n",
	     pipe_name(pipe));

	reg = PCH_LVDS;
	val = I915_READ(reg);
	I915_STATE_WARN(lvds_pipe_enabled(dev_priv, pipe, val),
	     "PCH LVDS enabled on transcoder %c, should be disabled\n",
	     pipe_name(pipe));

	assert_pch_hdmi_disabled(dev_priv, pipe, PCH_HDMIB);
	assert_pch_hdmi_disabled(dev_priv, pipe, PCH_HDMIC);
	assert_pch_hdmi_disabled(dev_priv, pipe, PCH_HDMID);
}

static void intel_init_dpio(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!IS_VALLEYVIEW(dev))
		return;

	/*
	 * IOSF_PORT_DPIO is used for VLV x2 PHY (DP/HDMI B and C),
	 * CHV x1 PHY (DP/HDMI D)
	 * IOSF_PORT_DPIO_2 is used for CHV x2 PHY (DP/HDMI B and C)
	 */
	if (IS_CHERRYVIEW(dev)) {
		DPIO_PHY_IOSF_PORT(DPIO_PHY0) = IOSF_PORT_DPIO_2;
		DPIO_PHY_IOSF_PORT(DPIO_PHY1) = IOSF_PORT_DPIO;
	} else {
		DPIO_PHY_IOSF_PORT(DPIO_PHY0) = IOSF_PORT_DPIO;
	}
}

static void vlv_enable_pll(struct intel_crtc *crtc,
			   const struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int reg = DPLL(crtc->pipe);
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

	mutex_lock(&dev_priv->dpio_lock);

	/* Enable back the 10bit clock to display controller */
	tmp = vlv_dpio_read(dev_priv, pipe, CHV_CMN_DW14(port));
	tmp |= DPIO_DCLKP_EN;
	vlv_dpio_write(dev_priv, pipe, CHV_CMN_DW14(port), tmp);

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

	mutex_unlock(&dev_priv->dpio_lock);
}

static int intel_num_dvo_pipes(struct drm_device *dev)
{
	struct intel_crtc *crtc;
	int count = 0;

	for_each_intel_crtc(dev, crtc)
		count += crtc->active &&
			intel_pipe_has_type(crtc, INTEL_OUTPUT_DVO);

	return count;
}

static void i9xx_enable_pll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int reg = DPLL(crtc->pipe);
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
	    intel_num_dvo_pipes(dev) == 1) {
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

	I915_WRITE(DPLL(pipe), 0);
	POSTING_READ(DPLL(pipe));
}

static void vlv_disable_pll(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	u32 val = 0;

	/* Make sure the pipe isn't still relying on us */
	assert_pipe_disabled(dev_priv, pipe);

	/*
	 * Leave integrated clock source and reference clock enabled for pipe B.
	 * The latter is needed for VGA hotplug / manual detection.
	 */
	if (pipe == PIPE_B)
		val = DPLL_INTEGRATED_CRI_CLK_VLV | DPLL_REFA_CLK_ENABLE_VLV;
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
	val = DPLL_SSC_REF_CLOCK_CHV | DPLL_REFA_CLK_ENABLE_VLV;
	if (pipe != PIPE_A)
		val |= DPLL_INTEGRATED_CRI_CLK_VLV;
	I915_WRITE(DPLL(pipe), val);
	POSTING_READ(DPLL(pipe));

	mutex_lock(&dev_priv->dpio_lock);

	/* Disable 10bit clock to display controller */
	val = vlv_dpio_read(dev_priv, pipe, CHV_CMN_DW14(port));
	val &= ~DPIO_DCLKP_EN;
	vlv_dpio_write(dev_priv, pipe, CHV_CMN_DW14(port), val);

	/* disable left/right clock distribution */
	if (pipe != PIPE_B) {
		val = vlv_dpio_read(dev_priv, pipe, _CHV_CMN_DW5_CH0);
		val &= ~(CHV_BUFLEFTENA1_MASK | CHV_BUFRIGHTENA1_MASK);
		vlv_dpio_write(dev_priv, pipe, _CHV_CMN_DW5_CH0, val);
	} else {
		val = vlv_dpio_read(dev_priv, pipe, _CHV_CMN_DW1_CH1);
		val &= ~(CHV_BUFLEFTENA2_MASK | CHV_BUFRIGHTENA2_MASK);
		vlv_dpio_write(dev_priv, pipe, _CHV_CMN_DW1_CH1, val);
	}

	mutex_unlock(&dev_priv->dpio_lock);
}

void vlv_wait_port_ready(struct drm_i915_private *dev_priv,
		struct intel_digital_port *dport)
{
	u32 port_mask;
	int dpll_reg;

	switch (dport->port) {
	case PORT_B:
		port_mask = DPLL_PORTB_READY_MASK;
		dpll_reg = DPLL(0);
		break;
	case PORT_C:
		port_mask = DPLL_PORTC_READY_MASK;
		dpll_reg = DPLL(0);
		break;
	case PORT_D:
		port_mask = DPLL_PORTD_READY_MASK;
		dpll_reg = DPIO_PHY_STATUS;
		break;
	default:
		BUG();
	}

	if (wait_for((I915_READ(dpll_reg) & port_mask) == 0, 1000))
		WARN(1, "timed out waiting for port %c ready: 0x%08x\n",
		     port_name(dport->port), I915_READ(dpll_reg));
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
	BUG_ON(INTEL_INFO(dev)->gen < 5);
	if (WARN_ON(pll == NULL))
	       return;

	if (WARN_ON(pll->config.crtc_mask == 0))
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
	uint32_t reg, val, pipeconf_val;

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
		 * make the BPC in transcoder be consistent with
		 * that in pipeconf reg.
		 */
		val &= ~PIPECONF_BPC_MASK;
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
	val = I915_READ(_TRANSA_CHICKEN2);
	val |= TRANS_CHICKEN2_TIMING_OVERRIDE;
	I915_WRITE(_TRANSA_CHICKEN2, val);

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
	uint32_t reg, val;

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

	if (!HAS_PCH_IBX(dev)) {
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
	val = I915_READ(_TRANSA_CHICKEN2);
	val &= ~TRANS_CHICKEN2_TIMING_OVERRIDE;
	I915_WRITE(_TRANSA_CHICKEN2, val);
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
	enum transcoder cpu_transcoder = intel_pipe_to_cpu_transcoder(dev_priv,
								      pipe);
	enum pipe pch_transcoder;
	int reg;
	u32 val;

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
	if (!HAS_PCH_SPLIT(dev_priv->dev))
		if (intel_pipe_has_type(crtc, INTEL_OUTPUT_DSI))
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
	int reg;
	u32 val;

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

/*
 * Plane regs are double buffered, going from enabled->disabled needs a
 * trigger in order to latch.  The display address reg provides this.
 */
void intel_flush_primary_plane(struct drm_i915_private *dev_priv,
			       enum plane plane)
{
	struct drm_device *dev = dev_priv->dev;
	u32 reg = INTEL_INFO(dev)->gen >= 4 ? DSPSURF(plane) : DSPADDR(plane);

	I915_WRITE(reg, I915_READ(reg));
	POSTING_READ(reg);
}

/**
 * intel_enable_primary_hw_plane - enable the primary plane on a given pipe
 * @plane:  plane to be enabled
 * @crtc: crtc for the plane
 *
 * Enable @plane on @crtc, making sure that the pipe is running first.
 */
static void intel_enable_primary_hw_plane(struct drm_plane *plane,
					  struct drm_crtc *crtc)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	/* If the pipe isn't enabled, we can't pump pixels and may hang */
	assert_pipe_enabled(dev_priv, intel_crtc->pipe);

	if (intel_crtc->primary_enabled)
		return;

	intel_crtc->primary_enabled = true;

	dev_priv->display.update_primary_plane(crtc, plane->fb,
					       crtc->x, crtc->y);

	/*
	 * BDW signals flip done immediately if the plane
	 * is disabled, even if the plane enable is already
	 * armed to occur at the next vblank :(
	 */
	if (IS_BROADWELL(dev))
		intel_wait_for_vblank(dev, intel_crtc->pipe);
}

/**
 * intel_disable_primary_hw_plane - disable the primary hardware plane
 * @plane: plane to be disabled
 * @crtc: crtc for the plane
 *
 * Disable @plane on @crtc, making sure that the pipe is running first.
 */
static void intel_disable_primary_hw_plane(struct drm_plane *plane,
					   struct drm_crtc *crtc)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	if (WARN_ON(!intel_crtc->active))
		return;

	if (!intel_crtc->primary_enabled)
		return;

	intel_crtc->primary_enabled = false;

	dev_priv->display.update_primary_plane(crtc, plane->fb,
					       crtc->x, crtc->y);
}

static bool need_vtd_wa(struct drm_device *dev)
{
#ifdef CONFIG_INTEL_IOMMU
	if (INTEL_INFO(dev)->gen >= 6 && intel_iommu_gfx_mapped)
		return true;
#endif
	return false;
}

int
intel_fb_align_height(struct drm_device *dev, int height,
		      uint32_t pixel_format,
		      uint64_t fb_format_modifier)
{
	int tile_height;
	uint32_t bits_per_pixel;

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
		bits_per_pixel = drm_format_plane_cpp(pixel_format, 0) * 8;
		switch (bits_per_pixel) {
		default:
		case 8:
			tile_height = 64;
			break;
		case 16:
		case 32:
			tile_height = 32;
			break;
		case 64:
			tile_height = 16;
			break;
		case 128:
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

	return ALIGN(height, tile_height);
}

int
intel_pin_and_fence_fb_obj(struct drm_plane *plane,
			   struct drm_framebuffer *fb,
			   struct intel_engine_cs *pipelined)
{
	struct drm_device *dev = fb->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	u32 alignment;
	int ret;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	switch (fb->modifier[0]) {
	case DRM_FORMAT_MOD_NONE:
		if (INTEL_INFO(dev)->gen >= 9)
			alignment = 256 * 1024;
		else if (IS_BROADWATER(dev) || IS_CRESTLINE(dev))
			alignment = 128 * 1024;
		else if (INTEL_INFO(dev)->gen >= 4)
			alignment = 4 * 1024;
		else
			alignment = 64 * 1024;
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

	dev_priv->mm.interruptible = false;
	ret = i915_gem_object_pin_to_display_plane(obj, alignment, pipelined);
	if (ret)
		goto err_interruptible;

	/* Install a fence for tiled scan-out. Pre-i965 always needs a
	 * fence, whereas 965+ only requires a fence if using
	 * framebuffer compression.  For simplicity, we always install
	 * a fence as the cost is not that onerous.
	 */
	ret = i915_gem_object_get_fence(obj);
	if (ret)
		goto err_unpin;

	i915_gem_object_pin_fence(obj);

	dev_priv->mm.interruptible = true;
	intel_runtime_pm_put(dev_priv);
	return 0;

err_unpin:
	i915_gem_object_unpin_from_display_plane(obj);
err_interruptible:
	dev_priv->mm.interruptible = true;
	intel_runtime_pm_put(dev_priv);
	return ret;
}

static void intel_unpin_fb_obj(struct drm_i915_gem_object *obj)
{
	WARN_ON(!mutex_is_locked(&obj->base.dev->struct_mutex));

	i915_gem_object_unpin_fence(obj);
	i915_gem_object_unpin_from_display_plane(obj);
}

/* Computes the linear offset to the base tile and adjusts x, y. bytes per pixel
 * is assumed to be a power-of-two. */
unsigned long intel_gen4_compute_page_offset(int *x, int *y,
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
		unsigned int offset;

		offset = *y * pitch + *x * cpp;
		*y = 0;
		*x = (offset & 4095) / cpp;
		return offset & -4096;
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
intel_alloc_plane_obj(struct intel_crtc *crtc,
		      struct intel_initial_plane_config *plane_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_gem_object *obj = NULL;
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct drm_framebuffer *fb = &plane_config->fb->base;
	u32 base_aligned = round_down(plane_config->base, PAGE_SIZE);
	u32 size_aligned = round_up(plane_config->base + plane_config->size,
				    PAGE_SIZE);

	size_aligned -= base_aligned;

	if (plane_config->size == 0)
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

	obj->frontbuffer_bits = INTEL_FRONTBUFFER_PRIMARY(crtc->pipe);
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG_KMS("plane fb obj %p\n", obj);
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
intel_find_plane_obj(struct intel_crtc *intel_crtc,
		     struct intel_initial_plane_config *plane_config)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *c;
	struct intel_crtc *i;
	struct drm_i915_gem_object *obj;

	if (!plane_config->fb)
		return;

	if (intel_alloc_plane_obj(intel_crtc, plane_config)) {
		struct drm_plane *primary = intel_crtc->base.primary;

		primary->fb = &plane_config->fb->base;
		primary->state->crtc = &intel_crtc->base;
		update_state_fb(primary);

		return;
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

		obj = intel_fb_obj(c->primary->fb);
		if (obj == NULL)
			continue;

		if (i915_gem_obj_ggtt_offset(obj) == plane_config->base) {
			struct drm_plane *primary = intel_crtc->base.primary;

			if (obj->tiling_mode != I915_TILING_NONE)
				dev_priv->preserve_bios_swizzle = true;

			drm_framebuffer_reference(c->primary->fb);
			primary->fb = c->primary->fb;
			primary->state->crtc = &intel_crtc->base;
			update_state_fb(intel_crtc->base.primary);
			obj->frontbuffer_bits |= INTEL_FRONTBUFFER_PRIMARY(intel_crtc->pipe);
			break;
		}
	}

}

static void i9xx_update_primary_plane(struct drm_crtc *crtc,
				      struct drm_framebuffer *fb,
				      int x, int y)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_gem_object *obj;
	int plane = intel_crtc->plane;
	unsigned long linear_offset;
	u32 dspcntr;
	u32 reg = DSPCNTR(plane);
	int pixel_size;

	if (!intel_crtc->primary_enabled) {
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
	case DRM_FORMAT_ARGB1555:
		dspcntr |= DISPPLANE_BGRX555;
		break;
	case DRM_FORMAT_RGB565:
		dspcntr |= DISPPLANE_BGRX565;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		dspcntr |= DISPPLANE_BGRX888;
		break;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		dspcntr |= DISPPLANE_RGBX888;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		dspcntr |= DISPPLANE_BGRX101010;
		break;
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
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
			intel_gen4_compute_page_offset(&x, &y, obj->tiling_mode,
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

	I915_WRITE(reg, dspcntr);

	DRM_DEBUG_KMS("Writing base %08lX %08lX %d %d %d\n",
		      i915_gem_obj_ggtt_offset(obj), linear_offset, x, y,
		      fb->pitches[0]);
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
	struct drm_i915_gem_object *obj;
	int plane = intel_crtc->plane;
	unsigned long linear_offset;
	u32 dspcntr;
	u32 reg = DSPCNTR(plane);
	int pixel_size;

	if (!intel_crtc->primary_enabled) {
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
	case DRM_FORMAT_ARGB8888:
		dspcntr |= DISPPLANE_BGRX888;
		break;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		dspcntr |= DISPPLANE_RGBX888;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		dspcntr |= DISPPLANE_BGRX101010;
		break;
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
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
		intel_gen4_compute_page_offset(&x, &y, obj->tiling_mode,
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

	I915_WRITE(reg, dspcntr);

	DRM_DEBUG_KMS("Writing base %08lX %08lX %d %d %d\n",
		      i915_gem_obj_ggtt_offset(obj), linear_offset, x, y,
		      fb->pitches[0]);
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

static void skylake_update_primary_plane(struct drm_crtc *crtc,
					 struct drm_framebuffer *fb,
					 int x, int y)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_gem_object *obj;
	int pipe = intel_crtc->pipe;
	u32 plane_ctl, stride_div;

	if (!intel_crtc->primary_enabled) {
		I915_WRITE(PLANE_CTL(pipe, 0), 0);
		I915_WRITE(PLANE_SURF(pipe, 0), 0);
		POSTING_READ(PLANE_CTL(pipe, 0));
		return;
	}

	plane_ctl = PLANE_CTL_ENABLE |
		    PLANE_CTL_PIPE_GAMMA_ENABLE |
		    PLANE_CTL_PIPE_CSC_ENABLE;

	switch (fb->pixel_format) {
	case DRM_FORMAT_RGB565:
		plane_ctl |= PLANE_CTL_FORMAT_RGB_565;
		break;
	case DRM_FORMAT_XRGB8888:
		plane_ctl |= PLANE_CTL_FORMAT_XRGB_8888;
		break;
	case DRM_FORMAT_ARGB8888:
		plane_ctl |= PLANE_CTL_FORMAT_XRGB_8888;
		plane_ctl |= PLANE_CTL_ALPHA_SW_PREMULTIPLY;
		break;
	case DRM_FORMAT_XBGR8888:
		plane_ctl |= PLANE_CTL_ORDER_RGBX;
		plane_ctl |= PLANE_CTL_FORMAT_XRGB_8888;
		break;
	case DRM_FORMAT_ABGR8888:
		plane_ctl |= PLANE_CTL_ORDER_RGBX;
		plane_ctl |= PLANE_CTL_FORMAT_XRGB_8888;
		plane_ctl |= PLANE_CTL_ALPHA_SW_PREMULTIPLY;
		break;
	case DRM_FORMAT_XRGB2101010:
		plane_ctl |= PLANE_CTL_FORMAT_XRGB_2101010;
		break;
	case DRM_FORMAT_XBGR2101010:
		plane_ctl |= PLANE_CTL_ORDER_RGBX;
		plane_ctl |= PLANE_CTL_FORMAT_XRGB_2101010;
		break;
	default:
		BUG();
	}

	switch (fb->modifier[0]) {
	case DRM_FORMAT_MOD_NONE:
		break;
	case I915_FORMAT_MOD_X_TILED:
		plane_ctl |= PLANE_CTL_TILED_X;
		break;
	case I915_FORMAT_MOD_Y_TILED:
		plane_ctl |= PLANE_CTL_TILED_Y;
		break;
	case I915_FORMAT_MOD_Yf_TILED:
		plane_ctl |= PLANE_CTL_TILED_YF;
		break;
	default:
		MISSING_CASE(fb->modifier[0]);
	}

	plane_ctl |= PLANE_CTL_PLANE_GAMMA_DISABLE;
	if (crtc->primary->state->rotation == BIT(DRM_ROTATE_180))
		plane_ctl |= PLANE_CTL_ROTATE_180;

	obj = intel_fb_obj(fb);
	stride_div = intel_fb_stride_alignment(dev, fb->modifier[0],
					       fb->pixel_format);

	I915_WRITE(PLANE_CTL(pipe, 0), plane_ctl);

	DRM_DEBUG_KMS("Writing base %08lX %d,%d,%d,%d pitch=%d\n",
		      i915_gem_obj_ggtt_offset(obj),
		      x, y, fb->width, fb->height,
		      fb->pitches[0]);

	I915_WRITE(PLANE_POS(pipe, 0), 0);
	I915_WRITE(PLANE_OFFSET(pipe, 0), (y << 16) | x);
	I915_WRITE(PLANE_SIZE(pipe, 0),
		   (intel_crtc->config->pipe_src_h - 1) << 16 |
		   (intel_crtc->config->pipe_src_w - 1));
	I915_WRITE(PLANE_STRIDE(pipe, 0), fb->pitches[0] / stride_div);
	I915_WRITE(PLANE_SURF(pipe, 0), i915_gem_obj_ggtt_offset(obj));

	POSTING_READ(PLANE_SURF(pipe, 0));
}

/* Assume fb object is pinned & idle & fenced and just update base pointers */
static int
intel_pipe_set_base_atomic(struct drm_crtc *crtc, struct drm_framebuffer *fb,
			   int x, int y, enum mode_set_atomic state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->display.disable_fbc)
		dev_priv->display.disable_fbc(dev);

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
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;

	for_each_crtc(dev, crtc) {
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

		drm_modeset_lock(&crtc->mutex, NULL);
		/*
		 * FIXME: Once we have proper support for primary planes (and
		 * disabling them without disabling the entire crtc) allow again
		 * a NULL crtc->primary->fb.
		 */
		if (intel_crtc->active && crtc->primary->fb)
			dev_priv->display.update_primary_plane(crtc,
							       crtc->primary->fb,
							       crtc->x,
							       crtc->y);
		drm_modeset_unlock(&crtc->mutex);
	}
}

void intel_prepare_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *crtc;

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
	for_each_intel_crtc(dev, crtc) {
		if (crtc->active)
			dev_priv->display.crtc_disable(&crtc->base);
	}
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

	intel_modeset_setup_hw_state(dev, true);

	intel_hpd_init(dev_priv);

	drm_modeset_unlock_all(dev);
}

static int
intel_finish_fb(struct drm_framebuffer *old_fb)
{
	struct drm_i915_gem_object *obj = intel_fb_obj(old_fb);
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
	bool was_interruptible = dev_priv->mm.interruptible;
	int ret;

	/* Big Hammer, we also need to ensure that any pending
	 * MI_WAIT_FOR_EVENT inside a user batch buffer on the
	 * current scanout is retired before unpinning the old
	 * framebuffer.
	 *
	 * This should only fail upon a hung GPU, in which case we
	 * can safely continue.
	 */
	dev_priv->mm.interruptible = false;
	ret = i915_gem_object_finish_gpu(obj);
	dev_priv->mm.interruptible = was_interruptible;

	return ret;
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

static void intel_update_pipe_size(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	const struct drm_display_mode *adjusted_mode;

	if (!i915.fastboot)
		return;

	/*
	 * Update pipe size and adjust fitter if needed: the reason for this is
	 * that in compute_mode_changes we check the native mode (not the pfit
	 * mode) to see if we can flip rather than do a full mode set. In the
	 * fastboot case, we'll flip, but if we don't update the pipesrc and
	 * pfit state, we'll end up with a big fb scanned out into the wrong
	 * sized surface.
	 *
	 * To fix this properly, we need to hoist the checks up into
	 * compute_mode_changes (or above), check the actual pfit state and
	 * whether the platform allows pfit disable with pipe active, and only
	 * then update the pipesrc and pfit state, even on the flip path.
	 */

	adjusted_mode = &crtc->config->base.adjusted_mode;

	I915_WRITE(PIPESRC(crtc->pipe),
		   ((adjusted_mode->crtc_hdisplay - 1) << 16) |
		   (adjusted_mode->crtc_vdisplay - 1));
	if (!crtc->config->pch_pfit.enabled &&
	    (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS) ||
	     intel_pipe_has_type(crtc, INTEL_OUTPUT_EDP))) {
		I915_WRITE(PF_CTL(crtc->pipe), 0);
		I915_WRITE(PF_WIN_POS(crtc->pipe), 0);
		I915_WRITE(PF_WIN_SZ(crtc->pipe), 0);
	}
	crtc->config->pipe_src_w = adjusted_mode->crtc_hdisplay;
	crtc->config->pipe_src_h = adjusted_mode->crtc_vdisplay;
}

static void intel_fdi_normal_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 reg, temp;

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

static bool pipe_has_enabled_pch(struct intel_crtc *crtc)
{
	return crtc->base.state->enable && crtc->active &&
		crtc->config->has_pch_encoder;
}

static void ivb_modeset_global_resources(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *pipe_B_crtc =
		to_intel_crtc(dev_priv->pipe_to_crtc_mapping[PIPE_B]);
	struct intel_crtc *pipe_C_crtc =
		to_intel_crtc(dev_priv->pipe_to_crtc_mapping[PIPE_C]);
	uint32_t temp;

	/*
	 * When everything is off disable fdi C so that we could enable fdi B
	 * with all lanes. Note that we don't care about enabled pipes without
	 * an enabled pch encoder.
	 */
	if (!pipe_has_enabled_pch(pipe_B_crtc) &&
	    !pipe_has_enabled_pch(pipe_C_crtc)) {
		WARN_ON(I915_READ(FDI_RX_CTL(PIPE_B)) & FDI_RX_ENABLE);
		WARN_ON(I915_READ(FDI_RX_CTL(PIPE_C)) & FDI_RX_ENABLE);

		temp = I915_READ(SOUTH_CHICKEN1);
		temp &= ~FDI_BC_BIFURCATION_SELECT;
		DRM_DEBUG_KMS("disabling fdi C rx\n");
		I915_WRITE(SOUTH_CHICKEN1, temp);
	}
}

/* The FDI link training functions for ILK/Ibexpeak. */
static void ironlake_fdi_link_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 reg, temp, tries;

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
	u32 reg, temp, i, retry;

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
	u32 reg, temp, i, j;

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
	u32 reg, temp;


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
	u32 reg, temp;

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
	u32 reg, temp;

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

void intel_crtc_wait_for_pending_flips(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	WARN_ON(waitqueue_active(&dev_priv->pending_flip_queue));
	if (WARN_ON(wait_event_timeout(dev_priv->pending_flip_queue,
				       !intel_crtc_has_pending_flip(crtc),
				       60*HZ) == 0)) {
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

		spin_lock_irq(&dev->event_lock);
		if (intel_crtc->unpin_work) {
			WARN_ONCE(1, "Removing stuck page flip\n");
			page_flip_completed(intel_crtc);
		}
		spin_unlock_irq(&dev->event_lock);
	}

	if (crtc->primary->fb) {
		mutex_lock(&dev->struct_mutex);
		intel_finish_fb(crtc->primary->fb);
		mutex_unlock(&dev->struct_mutex);
	}
}

/* Program iCLKIP clock to the desired frequency */
static void lpt_program_iclkip(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int clock = to_intel_crtc(crtc)->config->base.adjusted_mode.crtc_clock;
	u32 divsel, phaseinc, auxdiv, phasedir = 0;
	u32 temp;

	mutex_lock(&dev_priv->dpio_lock);

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

	mutex_unlock(&dev_priv->dpio_lock);
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

static void cpt_enable_fdi_bc_bifurcation(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t temp;

	temp = I915_READ(SOUTH_CHICKEN1);
	if (temp & FDI_BC_BIFURCATION_SELECT)
		return;

	WARN_ON(I915_READ(FDI_RX_CTL(PIPE_B)) & FDI_RX_ENABLE);
	WARN_ON(I915_READ(FDI_RX_CTL(PIPE_C)) & FDI_RX_ENABLE);

	temp |= FDI_BC_BIFURCATION_SELECT;
	DRM_DEBUG_KMS("enabling fdi C rx\n");
	I915_WRITE(SOUTH_CHICKEN1, temp);
	POSTING_READ(SOUTH_CHICKEN1);
}

static void ivybridge_update_fdi_bc_bifurcation(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	switch (intel_crtc->pipe) {
	case PIPE_A:
		break;
	case PIPE_B:
		if (intel_crtc->config->fdi_lanes > 2)
			WARN_ON(I915_READ(SOUTH_CHICKEN1) & FDI_BC_BIFURCATION_SELECT);
		else
			cpt_enable_fdi_bc_bifurcation(dev);

		break;
	case PIPE_C:
		cpt_enable_fdi_bc_bifurcation(dev);

		break;
	default:
		BUG();
	}
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
	u32 reg, temp;

	assert_pch_transcoder_disabled(dev_priv, pipe);

	if (IS_IVYBRIDGE(dev))
		ivybridge_update_fdi_bc_bifurcation(intel_crtc);

	/* Write the TU size bits before fdi link training, so that error
	 * detection works. */
	I915_WRITE(FDI_RX_TUSIZE1(pipe),
		   I915_READ(PIPE_DATA_M1(pipe)) & TU_SIZE_MASK);

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

	/* For PCH DP, enable TRANS_DP_CTL */
	if (HAS_PCH_CPT(dev) && intel_crtc->config->has_dp_encoder) {
		u32 bpc = (I915_READ(PIPECONF(pipe)) & PIPECONF_BPC_MASK) >> 5;
		reg = TRANS_DP_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~(TRANS_DP_PORT_SEL_MASK |
			  TRANS_DP_SYNC_MASK |
			  TRANS_DP_BPC_MASK);
		temp |= (TRANS_DP_OUTPUT_ENABLE |
			 TRANS_DP_ENH_FRAMING);
		temp |= bpc << 9; /* same format but at 11:9 */

		if (crtc->mode.flags & DRM_MODE_FLAG_PHSYNC)
			temp |= TRANS_DP_HSYNC_ACTIVE_HIGH;
		if (crtc->mode.flags & DRM_MODE_FLAG_PVSYNC)
			temp |= TRANS_DP_VSYNC_ACTIVE_HIGH;

		switch (intel_trans_dp_port_sel(crtc)) {
		case PCH_DP_B:
			temp |= TRANS_DP_PORT_SEL_B;
			break;
		case PCH_DP_C:
			temp |= TRANS_DP_PORT_SEL_C;
			break;
		case PCH_DP_D:
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

void intel_put_shared_dpll(struct intel_crtc *crtc)
{
	struct intel_shared_dpll *pll = intel_crtc_to_shared_dpll(crtc);

	if (pll == NULL)
		return;

	if (!(pll->config.crtc_mask & (1 << crtc->pipe))) {
		WARN(1, "bad %s crtc mask\n", pll->name);
		return;
	}

	pll->config.crtc_mask &= ~(1 << crtc->pipe);
	if (pll->config.crtc_mask == 0) {
		WARN_ON(pll->on);
		WARN_ON(pll->active);
	}

	crtc->config->shared_dpll = DPLL_ID_PRIVATE;
}

struct intel_shared_dpll *intel_get_shared_dpll(struct intel_crtc *crtc,
						struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = crtc->base.dev->dev_private;
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	if (HAS_PCH_IBX(dev_priv->dev)) {
		/* Ironlake PCH has a fixed PLL->PCH pipe mapping. */
		i = (enum intel_dpll_id) crtc->pipe;
		pll = &dev_priv->shared_dplls[i];

		DRM_DEBUG_KMS("CRTC:%d using pre-allocated %s\n",
			      crtc->base.base.id, pll->name);

		WARN_ON(pll->new_config->crtc_mask);

		goto found;
	}

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		pll = &dev_priv->shared_dplls[i];

		/* Only want to check enabled timings first */
		if (pll->new_config->crtc_mask == 0)
			continue;

		if (memcmp(&crtc_state->dpll_hw_state,
			   &pll->new_config->hw_state,
			   sizeof(pll->new_config->hw_state)) == 0) {
			DRM_DEBUG_KMS("CRTC:%d sharing existing %s (crtc mask 0x%08x, ative %d)\n",
				      crtc->base.base.id, pll->name,
				      pll->new_config->crtc_mask,
				      pll->active);
			goto found;
		}
	}

	/* Ok no matching timings, maybe there's a free one? */
	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		pll = &dev_priv->shared_dplls[i];
		if (pll->new_config->crtc_mask == 0) {
			DRM_DEBUG_KMS("CRTC:%d allocated %s\n",
				      crtc->base.base.id, pll->name);
			goto found;
		}
	}

	return NULL;

found:
	if (pll->new_config->crtc_mask == 0)
		pll->new_config->hw_state = crtc_state->dpll_hw_state;

	crtc_state->shared_dpll = i;
	DRM_DEBUG_DRIVER("using %s for pipe %c\n", pll->name,
			 pipe_name(crtc->pipe));

	pll->new_config->crtc_mask |= 1 << crtc->pipe;

	return pll;
}

/**
 * intel_shared_dpll_start_config - start a new PLL staged config
 * @dev_priv: DRM device
 * @clear_pipes: mask of pipes that will have their PLLs freed
 *
 * Starts a new PLL staged config, copying the current config but
 * releasing the references of pipes specified in clear_pipes.
 */
static int intel_shared_dpll_start_config(struct drm_i915_private *dev_priv,
					  unsigned clear_pipes)
{
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		pll = &dev_priv->shared_dplls[i];

		pll->new_config = kmemdup(&pll->config, sizeof pll->config,
					  GFP_KERNEL);
		if (!pll->new_config)
			goto cleanup;

		pll->new_config->crtc_mask &= ~clear_pipes;
	}

	return 0;

cleanup:
	while (--i >= 0) {
		pll = &dev_priv->shared_dplls[i];
		kfree(pll->new_config);
		pll->new_config = NULL;
	}

	return -ENOMEM;
}

static void intel_shared_dpll_commit(struct drm_i915_private *dev_priv)
{
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		pll = &dev_priv->shared_dplls[i];

		WARN_ON(pll->new_config == &pll->config);

		pll->config = *pll->new_config;
		kfree(pll->new_config);
		pll->new_config = NULL;
	}
}

static void intel_shared_dpll_abort_config(struct drm_i915_private *dev_priv)
{
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		pll = &dev_priv->shared_dplls[i];

		WARN_ON(pll->new_config == &pll->config);

		kfree(pll->new_config);
		pll->new_config = NULL;
	}
}

static void cpt_verify_modeset(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int dslreg = PIPEDSL(pipe);
	u32 temp;

	temp = I915_READ(dslreg);
	udelay(500);
	if (wait_for(I915_READ(dslreg) != temp, 5)) {
		if (wait_for(I915_READ(dslreg) != temp, 5))
			DRM_ERROR("mode set failed: pipe %c stuck\n", pipe_name(pipe));
	}
}

static void skylake_pfit_enable(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = crtc->pipe;

	if (crtc->config->pch_pfit.enabled) {
		I915_WRITE(PS_CTL(pipe), PS_ENABLE);
		I915_WRITE(PS_WIN_POS(pipe), crtc->config->pch_pfit.pos);
		I915_WRITE(PS_WIN_SZ(pipe), crtc->config->pch_pfit.size);
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

static void intel_enable_sprite_planes(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	struct drm_plane *plane;
	struct intel_plane *intel_plane;

	drm_for_each_legacy_plane(plane, &dev->mode_config.plane_list) {
		intel_plane = to_intel_plane(plane);
		if (intel_plane->pipe == pipe)
			intel_plane_restore(&intel_plane->base);
	}
}

/*
 * Disable a plane internally without actually modifying the plane's state.
 * This will allow us to easily restore the plane later by just reprogramming
 * its state.
 */
static void disable_plane_internal(struct drm_plane *plane)
{
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct drm_plane_state *state =
		plane->funcs->atomic_duplicate_state(plane);
	struct intel_plane_state *intel_state = to_intel_plane_state(state);

	intel_state->visible = false;
	intel_plane->commit_plane(plane, intel_state);

	intel_plane_destroy_state(plane, state);
}

static void intel_disable_sprite_planes(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	struct drm_plane *plane;
	struct intel_plane *intel_plane;

	drm_for_each_legacy_plane(plane, &dev->mode_config.plane_list) {
		intel_plane = to_intel_plane(plane);
		if (plane->fb && intel_plane->pipe == pipe)
			disable_plane_internal(plane);
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
	int palreg = PALETTE(pipe);
	int i;
	bool reenable_ips = false;

	/* The clocks have to be on to load the palette. */
	if (!crtc->state->enable || !intel_crtc->active)
		return;

	if (!HAS_PCH_SPLIT(dev_priv->dev)) {
		if (intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_DSI))
			assert_dsi_pll_enabled(dev_priv);
		else
			assert_pll_enabled(dev_priv, pipe);
	}

	/* use legacy palette for Ironlake */
	if (!HAS_GMCH_DISPLAY(dev))
		palreg = LGC_PALETTE(pipe);

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
		I915_WRITE(palreg + 4 * i,
			   (intel_crtc->lut_r[i] << 16) |
			   (intel_crtc->lut_g[i] << 8) |
			   intel_crtc->lut_b[i]);
	}

	if (reenable_ips)
		hsw_enable_ips(intel_crtc);
}

static void intel_crtc_dpms_overlay(struct intel_crtc *intel_crtc, bool enable)
{
	if (!enable && intel_crtc->overlay) {
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

static void intel_crtc_enable_planes(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;

	intel_enable_primary_hw_plane(crtc->primary, crtc);
	intel_enable_sprite_planes(crtc);
	intel_crtc_update_cursor(crtc, true);
	intel_crtc_dpms_overlay(intel_crtc, true);

	hsw_enable_ips(intel_crtc);

	mutex_lock(&dev->struct_mutex);
	intel_fbc_update(dev);
	mutex_unlock(&dev->struct_mutex);

	/*
	 * FIXME: Once we grow proper nuclear flip support out of this we need
	 * to compute the mask of flip planes precisely. For the time being
	 * consider this a flip from a NULL plane.
	 */
	intel_frontbuffer_flip(dev, INTEL_FRONTBUFFER_ALL_MASK(pipe));
}

static void intel_crtc_disable_planes(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;

	intel_crtc_wait_for_pending_flips(crtc);

	if (dev_priv->fbc.crtc == intel_crtc)
		intel_fbc_disable(dev);

	hsw_disable_ips(intel_crtc);

	intel_crtc_dpms_overlay(intel_crtc, false);
	intel_crtc_update_cursor(crtc, false);
	intel_disable_sprite_planes(crtc);
	intel_disable_primary_hw_plane(crtc->primary, crtc);

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

	WARN_ON(!crtc->state->enable);

	if (intel_crtc->active)
		return;

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
	intel_set_pch_fifo_underrun_reporting(dev_priv, pipe, true);

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

	intel_crtc_enable_planes(crtc);
}

/* IPS only exists on ULT machines and is tied to pipe A. */
static bool hsw_crtc_supports_ips(struct intel_crtc *crtc)
{
	return HAS_IPS(crtc->base.dev) && crtc->pipe == PIPE_A;
}

/*
 * This implements the workaround described in the "notes" section of the mode
 * set sequence documentation. When going from no pipes or single pipe to
 * multiple pipes, and planes are enabled after the pipe, we need to wait at
 * least 2 vblanks on the first pipe before enabling planes on the second pipe.
 */
static void haswell_mode_set_planes_workaround(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_crtc *crtc_it, *other_active_crtc = NULL;

	/* We want to get the other_active_crtc only if there's only 1 other
	 * active crtc. */
	for_each_intel_crtc(dev, crtc_it) {
		if (!crtc_it->active || crtc_it == crtc)
			continue;

		if (other_active_crtc)
			return;

		other_active_crtc = crtc_it;
	}
	if (!other_active_crtc)
		return;

	intel_wait_for_vblank(dev, other_active_crtc->pipe);
	intel_wait_for_vblank(dev, other_active_crtc->pipe);
}

static void haswell_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *encoder;
	int pipe = intel_crtc->pipe;

	WARN_ON(!crtc->state->enable);

	if (intel_crtc->active)
		return;

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

	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);
	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->pre_enable)
			encoder->pre_enable(encoder);

	if (intel_crtc->config->has_pch_encoder) {
		intel_set_pch_fifo_underrun_reporting(dev_priv, TRANSCODER_A,
						      true);
		dev_priv->display.fdi_link_train(crtc);
	}

	intel_ddi_enable_pipe_clock(intel_crtc);

	if (IS_SKYLAKE(dev))
		skylake_pfit_enable(intel_crtc);
	else
		ironlake_pfit_enable(intel_crtc);

	/*
	 * On ILK+ LUT must be loaded before the pipe is running but with
	 * clocks enabled
	 */
	intel_crtc_load_lut(crtc);

	intel_ddi_set_pipe_settings(crtc);
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

	/* If we change the relative order between pipe/planes enabling, we need
	 * to change the workaround. */
	haswell_mode_set_planes_workaround(intel_crtc);
	intel_crtc_enable_planes(crtc);
}

static void skylake_pfit_disable(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = crtc->pipe;

	/* To avoid upsetting the power well on haswell only disable the pfit if
	 * it's in use. The hw state code will make sure we get this right. */
	if (crtc->config->pch_pfit.enabled) {
		I915_WRITE(PS_CTL(pipe), 0);
		I915_WRITE(PS_WIN_POS(pipe), 0);
		I915_WRITE(PS_WIN_SZ(pipe), 0);
	}
}

static void ironlake_pfit_disable(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = crtc->pipe;

	/* To avoid upsetting the power well on haswell only disable the pfit if
	 * it's in use. The hw state code will make sure we get this right. */
	if (crtc->config->pch_pfit.enabled) {
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
	u32 reg, temp;

	if (!intel_crtc->active)
		return;

	intel_crtc_disable_planes(crtc);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		encoder->disable(encoder);

	drm_crtc_vblank_off(crtc);
	assert_vblank_disabled(crtc);

	if (intel_crtc->config->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv, pipe, false);

	intel_disable_pipe(intel_crtc);

	ironlake_pfit_disable(intel_crtc);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->post_disable)
			encoder->post_disable(encoder);

	if (intel_crtc->config->has_pch_encoder) {
		ironlake_fdi_disable(crtc);

		ironlake_disable_pch_transcoder(dev_priv, pipe);

		if (HAS_PCH_CPT(dev)) {
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

		/* disable PCH DPLL */
		intel_disable_shared_dpll(intel_crtc);

		ironlake_fdi_pll_disable(intel_crtc);
	}

	intel_crtc->active = false;
	intel_update_watermarks(crtc);

	mutex_lock(&dev->struct_mutex);
	intel_fbc_update(dev);
	mutex_unlock(&dev->struct_mutex);
}

static void haswell_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *encoder;
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;

	if (!intel_crtc->active)
		return;

	intel_crtc_disable_planes(crtc);

	for_each_encoder_on_crtc(dev, crtc, encoder) {
		intel_opregion_notify_encoder(encoder, false);
		encoder->disable(encoder);
	}

	drm_crtc_vblank_off(crtc);
	assert_vblank_disabled(crtc);

	if (intel_crtc->config->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv, TRANSCODER_A,
						      false);
	intel_disable_pipe(intel_crtc);

	if (intel_crtc->config->dp_encoder_is_mst)
		intel_ddi_set_vc_payload_alloc(crtc, false);

	intel_ddi_disable_transcoder_func(dev_priv, cpu_transcoder);

	if (IS_SKYLAKE(dev))
		skylake_pfit_disable(intel_crtc);
	else
		ironlake_pfit_disable(intel_crtc);

	intel_ddi_disable_pipe_clock(intel_crtc);

	if (intel_crtc->config->has_pch_encoder) {
		lpt_disable_pch_transcoder(dev_priv);
		intel_ddi_fdi_disable(crtc);
	}

	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->post_disable)
			encoder->post_disable(encoder);

	intel_crtc->active = false;
	intel_update_watermarks(crtc);

	mutex_lock(&dev->struct_mutex);
	intel_fbc_update(dev);
	mutex_unlock(&dev->struct_mutex);

	if (intel_crtc_to_shared_dpll(intel_crtc))
		intel_disable_shared_dpll(intel_crtc);
}

static void ironlake_crtc_off(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	intel_put_shared_dpll(intel_crtc);
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
		return POWER_DOMAIN_PORT_DDI_A_4_LANES;
	case PORT_B:
		return POWER_DOMAIN_PORT_DDI_B_4_LANES;
	case PORT_C:
		return POWER_DOMAIN_PORT_DDI_C_4_LANES;
	case PORT_D:
		return POWER_DOMAIN_PORT_DDI_D_4_LANES;
	default:
		WARN_ON_ONCE(1);
		return POWER_DOMAIN_PORT_OTHER;
	}
}

#define for_each_power_domain(domain, mask)				\
	for ((domain) = 0; (domain) < POWER_DOMAIN_NUM; (domain)++)	\
		if ((1 << (domain)) & (mask))

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

static unsigned long get_crtc_power_domains(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct intel_encoder *intel_encoder;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum pipe pipe = intel_crtc->pipe;
	unsigned long mask;
	enum transcoder transcoder;

	transcoder = intel_pipe_to_cpu_transcoder(dev->dev_private, pipe);

	mask = BIT(POWER_DOMAIN_PIPE(pipe));
	mask |= BIT(POWER_DOMAIN_TRANSCODER(transcoder));
	if (intel_crtc->config->pch_pfit.enabled ||
	    intel_crtc->config->pch_pfit.force_thru)
		mask |= BIT(POWER_DOMAIN_PIPE_PANEL_FITTER(pipe));

	for_each_encoder_on_crtc(dev, crtc, intel_encoder)
		mask |= BIT(intel_display_port_power_domain(intel_encoder));

	return mask;
}

static void modeset_update_crtc_power_domains(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long pipe_domains[I915_MAX_PIPES] = { 0, };
	struct intel_crtc *crtc;

	/*
	 * First get all needed power domains, then put all unneeded, to avoid
	 * any unnecessary toggling of the power wells.
	 */
	for_each_intel_crtc(dev, crtc) {
		enum intel_display_power_domain domain;

		if (!crtc->base.state->enable)
			continue;

		pipe_domains[crtc->pipe] = get_crtc_power_domains(&crtc->base);

		for_each_power_domain(domain, pipe_domains[crtc->pipe])
			intel_display_power_get(dev_priv, domain);
	}

	if (dev_priv->display.modeset_global_resources)
		dev_priv->display.modeset_global_resources(dev);

	for_each_intel_crtc(dev, crtc) {
		enum intel_display_power_domain domain;

		for_each_power_domain(domain, crtc->enabled_power_domains)
			intel_display_power_put(dev_priv, domain);

		crtc->enabled_power_domains = pipe_domains[crtc->pipe];
	}

	intel_display_set_init_power(dev_priv, false);
}

/* returns HPLL frequency in kHz */
static int valleyview_get_vco(struct drm_i915_private *dev_priv)
{
	int hpll_freq, vco_freq[] = { 800, 1600, 2000, 2400 };

	/* Obtain SKU information */
	mutex_lock(&dev_priv->dpio_lock);
	hpll_freq = vlv_cck_read(dev_priv, CCK_FUSE_REG) &
		CCK_FUSE_HPLL_FREQ_MASK;
	mutex_unlock(&dev_priv->dpio_lock);

	return vco_freq[hpll_freq] * 1000;
}

static void vlv_update_cdclk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->vlv_cdclk_freq = dev_priv->display.get_display_clock_speed(dev);
	DRM_DEBUG_DRIVER("Current CD clock rate: %d kHz\n",
			 dev_priv->vlv_cdclk_freq);

	/*
	 * Program the gmbus_freq based on the cdclk frequency.
	 * BSpec erroneously claims we should aim for 4MHz, but
	 * in fact 1MHz is the correct frequency.
	 */
	I915_WRITE(GMBUSFREQ_VLV, DIV_ROUND_UP(dev_priv->vlv_cdclk_freq, 1000));
}

/* Adjust CDclk dividers to allow high res or save power if possible */
static void valleyview_set_cdclk(struct drm_device *dev, int cdclk)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val, cmd;

	WARN_ON(dev_priv->display.get_display_clock_speed(dev) != dev_priv->vlv_cdclk_freq);

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

	if (cdclk == 400000) {
		u32 divider;

		divider = DIV_ROUND_CLOSEST(dev_priv->hpll_freq << 1, cdclk) - 1;

		mutex_lock(&dev_priv->dpio_lock);
		/* adjust cdclk divider */
		val = vlv_cck_read(dev_priv, CCK_DISPLAY_CLOCK_CONTROL);
		val &= ~DISPLAY_FREQUENCY_VALUES;
		val |= divider;
		vlv_cck_write(dev_priv, CCK_DISPLAY_CLOCK_CONTROL, val);

		if (wait_for((vlv_cck_read(dev_priv, CCK_DISPLAY_CLOCK_CONTROL) &
			      DISPLAY_FREQUENCY_STATUS) == (divider << DISPLAY_FREQUENCY_STATUS_SHIFT),
			     50))
			DRM_ERROR("timed out waiting for CDclk change\n");
		mutex_unlock(&dev_priv->dpio_lock);
	}

	mutex_lock(&dev_priv->dpio_lock);
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
	mutex_unlock(&dev_priv->dpio_lock);

	vlv_update_cdclk(dev);
}

static void cherryview_set_cdclk(struct drm_device *dev, int cdclk)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val, cmd;

	WARN_ON(dev_priv->display.get_display_clock_speed(dev) != dev_priv->vlv_cdclk_freq);

	switch (cdclk) {
	case 400000:
		cmd = 3;
		break;
	case 333333:
	case 320000:
		cmd = 2;
		break;
	case 266667:
		cmd = 1;
		break;
	case 200000:
		cmd = 0;
		break;
	default:
		MISSING_CASE(cdclk);
		return;
	}

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

	vlv_update_cdclk(dev);
}

static int valleyview_calc_cdclk(struct drm_i915_private *dev_priv,
				 int max_pixclk)
{
	int freq_320 = (dev_priv->hpll_freq <<  1) % 320000 != 0 ? 333333 : 320000;

	/* FIXME: Punit isn't quite ready yet */
	if (IS_CHERRYVIEW(dev_priv->dev))
		return 400000;

	/*
	 * Really only a few cases to deal with, as only 4 CDclks are supported:
	 *   200MHz
	 *   267MHz
	 *   320/333MHz (depends on HPLL freq)
	 *   400MHz
	 * So we check to see whether we're above 90% of the lower bin and
	 * adjust if needed.
	 *
	 * We seem to get an unstable or solid color picture at 200MHz.
	 * Not sure what's wrong. For now use 200MHz only when all pipes
	 * are off.
	 */
	if (max_pixclk > freq_320*9/10)
		return 400000;
	else if (max_pixclk > 266667*9/10)
		return freq_320;
	else if (max_pixclk > 0)
		return 266667;
	else
		return 200000;
}

/* compute the max pixel clock for new configuration */
static int intel_mode_max_pixclk(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct intel_crtc *intel_crtc;
	int max_pixclk = 0;

	for_each_intel_crtc(dev, intel_crtc) {
		if (intel_crtc->new_enabled)
			max_pixclk = max(max_pixclk,
					 intel_crtc->new_config->base.adjusted_mode.crtc_clock);
	}

	return max_pixclk;
}

static void valleyview_modeset_global_pipes(struct drm_device *dev,
					    unsigned *prepare_pipes)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc;
	int max_pixclk = intel_mode_max_pixclk(dev_priv);

	if (valleyview_calc_cdclk(dev_priv, max_pixclk) ==
	    dev_priv->vlv_cdclk_freq)
		return;

	/* disable/enable all currently active pipes while we change cdclk */
	for_each_intel_crtc(dev, intel_crtc)
		if (intel_crtc->base.state->enable)
			*prepare_pipes |= (1 << intel_crtc->pipe);
}

static void valleyview_modeset_global_resources(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int max_pixclk = intel_mode_max_pixclk(dev_priv);
	int req_cdclk = valleyview_calc_cdclk(dev_priv, max_pixclk);

	if (req_cdclk != dev_priv->vlv_cdclk_freq) {
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

		intel_display_power_put(dev_priv, POWER_DOMAIN_PIPE_A);
	}
}

static void valleyview_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *encoder;
	int pipe = intel_crtc->pipe;
	bool is_dsi;

	WARN_ON(!crtc->state->enable);

	if (intel_crtc->active)
		return;

	is_dsi = intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_DSI);

	if (!is_dsi) {
		if (IS_CHERRYVIEW(dev))
			chv_prepare_pll(intel_crtc, intel_crtc->config);
		else
			vlv_prepare_pll(intel_crtc, intel_crtc->config);
	}

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

	if (!is_dsi) {
		if (IS_CHERRYVIEW(dev))
			chv_enable_pll(intel_crtc, intel_crtc->config);
		else
			vlv_enable_pll(intel_crtc, intel_crtc->config);
	}

	for_each_encoder_on_crtc(dev, crtc, encoder)
		if (encoder->pre_enable)
			encoder->pre_enable(encoder);

	i9xx_pfit_enable(intel_crtc);

	intel_crtc_load_lut(crtc);

	intel_update_watermarks(crtc);
	intel_enable_pipe(intel_crtc);

	assert_vblank_disabled(crtc);
	drm_crtc_vblank_on(crtc);

	for_each_encoder_on_crtc(dev, crtc, encoder)
		encoder->enable(encoder);

	intel_crtc_enable_planes(crtc);

	/* Underruns don't raise interrupts, so check manually. */
	i9xx_check_fifo_underruns(dev_priv);
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

	WARN_ON(!crtc->state->enable);

	if (intel_crtc->active)
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

	intel_crtc_enable_planes(crtc);

	/*
	 * Gen2 reports pipe underruns whenever all planes are disabled.
	 * So don't enable underrun reporting before at least some planes
	 * are enabled.
	 * FIXME: Need to fix the logic to work when we turn off all planes
	 * but leave the pipe running.
	 */
	if (IS_GEN2(dev))
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	/* Underruns don't raise interrupts, so check manually. */
	i9xx_check_fifo_underruns(dev_priv);
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

	if (!intel_crtc->active)
		return;

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
	intel_set_memory_cxsr(dev_priv, false);
	intel_crtc_disable_planes(crtc);

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

	if (!intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_DSI)) {
		if (IS_CHERRYVIEW(dev))
			chv_disable_pll(dev_priv, pipe);
		else if (IS_VALLEYVIEW(dev))
			vlv_disable_pll(dev_priv, pipe);
		else
			i9xx_disable_pll(intel_crtc);
	}

	if (!IS_GEN2(dev))
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);

	intel_crtc->active = false;
	intel_update_watermarks(crtc);

	mutex_lock(&dev->struct_mutex);
	intel_fbc_update(dev);
	mutex_unlock(&dev->struct_mutex);
}

static void i9xx_crtc_off(struct drm_crtc *crtc)
{
}

/* Master function to enable/disable CRTC and corresponding power wells */
void intel_crtc_control(struct drm_crtc *crtc, bool enable)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum intel_display_power_domain domain;
	unsigned long domains;

	if (enable) {
		if (!intel_crtc->active) {
			domains = get_crtc_power_domains(crtc);
			for_each_power_domain(domain, domains)
				intel_display_power_get(dev_priv, domain);
			intel_crtc->enabled_power_domains = domains;

			dev_priv->display.crtc_enable(crtc);
		}
	} else {
		if (intel_crtc->active) {
			dev_priv->display.crtc_disable(crtc);

			domains = intel_crtc->enabled_power_domains;
			for_each_power_domain(domain, domains)
				intel_display_power_put(dev_priv, domain);
			intel_crtc->enabled_power_domains = 0;
		}
	}
}

/**
 * Sets the power management mode of the pipe and plane.
 */
void intel_crtc_update_dpms(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct intel_encoder *intel_encoder;
	bool enable = false;

	for_each_encoder_on_crtc(dev, crtc, intel_encoder)
		enable |= intel_encoder->connectors_active;

	intel_crtc_control(crtc, enable);
}

static void intel_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_connector *connector;
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* crtc should still be enabled when we disable it. */
	WARN_ON(!crtc->state->enable);

	dev_priv->display.crtc_disable(crtc);
	dev_priv->display.off(crtc);

	crtc->primary->funcs->disable_plane(crtc->primary);

	/* Update computed state. */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (!connector->encoder || !connector->encoder->crtc)
			continue;

		if (connector->encoder->crtc != crtc)
			continue;

		connector->dpms = DRM_MODE_DPMS_OFF;
		to_intel_encoder(connector->encoder)->connectors_active = false;
	}
}

void intel_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(encoder);

	drm_encoder_cleanup(encoder);
	kfree(intel_encoder);
}

/* Simple dpms helper for encoders with just one connector, no cloning and only
 * one kind of off state. It clamps all !ON modes to fully OFF and changes the
 * state of the entire output pipe. */
static void intel_encoder_dpms(struct intel_encoder *encoder, int mode)
{
	if (mode == DRM_MODE_DPMS_ON) {
		encoder->connectors_active = true;

		intel_crtc_update_dpms(encoder->base.crtc);
	} else {
		encoder->connectors_active = false;

		intel_crtc_update_dpms(encoder->base.crtc);
	}
}

/* Cross check the actual hw state with our own modeset state tracking (and it's
 * internal consistency). */
static void intel_connector_check_state(struct intel_connector *connector)
{
	if (connector->get_hw_state(connector)) {
		struct intel_encoder *encoder = connector->encoder;
		struct drm_crtc *crtc;
		bool encoder_enabled;
		enum pipe pipe;

		DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
			      connector->base.base.id,
			      connector->base.name);

		/* there is no real hw state for MST connectors */
		if (connector->mst_port)
			return;

		I915_STATE_WARN(connector->base.dpms == DRM_MODE_DPMS_OFF,
		     "wrong connector dpms state\n");
		I915_STATE_WARN(connector->base.encoder != &encoder->base,
		     "active connector not linked to encoder\n");

		if (encoder) {
			I915_STATE_WARN(!encoder->connectors_active,
			     "encoder->connectors_active not set\n");

			encoder_enabled = encoder->get_hw_state(encoder, &pipe);
			I915_STATE_WARN(!encoder_enabled, "encoder not enabled\n");
			if (I915_STATE_WARN_ON(!encoder->base.crtc))
				return;

			crtc = encoder->base.crtc;

			I915_STATE_WARN(!crtc->state->enable,
					"crtc not enabled\n");
			I915_STATE_WARN(!to_intel_crtc(crtc)->active, "crtc not active\n");
			I915_STATE_WARN(pipe != to_intel_crtc(crtc)->pipe,
			     "encoder active on the wrong pipe\n");
		}
	}
}

/* Even simpler default implementation, if there's really no special case to
 * consider. */
void intel_connector_dpms(struct drm_connector *connector, int mode)
{
	/* All the simple cases only support two dpms states. */
	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;

	if (mode == connector->dpms)
		return;

	connector->dpms = mode;

	/* Only need to change hw state when actually enabled */
	if (connector->encoder)
		intel_encoder_dpms(to_intel_encoder(connector->encoder), mode);

	intel_modeset_check_state(connector->dev);
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

static bool ironlake_check_fdi_lanes(struct drm_device *dev, enum pipe pipe,
				     struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *pipe_B_crtc =
		to_intel_crtc(dev_priv->pipe_to_crtc_mapping[PIPE_B]);

	DRM_DEBUG_KMS("checking fdi config on pipe %c, lanes %i\n",
		      pipe_name(pipe), pipe_config->fdi_lanes);
	if (pipe_config->fdi_lanes > 4) {
		DRM_DEBUG_KMS("invalid fdi lane config on pipe %c: %i lanes\n",
			      pipe_name(pipe), pipe_config->fdi_lanes);
		return false;
	}

	if (IS_HASWELL(dev) || IS_BROADWELL(dev)) {
		if (pipe_config->fdi_lanes > 2) {
			DRM_DEBUG_KMS("only 2 lanes on haswell, required: %i lanes\n",
				      pipe_config->fdi_lanes);
			return false;
		} else {
			return true;
		}
	}

	if (INTEL_INFO(dev)->num_pipes == 2)
		return true;

	/* Ivybridge 3 pipe is really complicated */
	switch (pipe) {
	case PIPE_A:
		return true;
	case PIPE_B:
		if (dev_priv->pipe_to_crtc_mapping[PIPE_C]->enabled &&
		    pipe_config->fdi_lanes > 2) {
			DRM_DEBUG_KMS("invalid shared fdi lane config on pipe %c: %i lanes\n",
				      pipe_name(pipe), pipe_config->fdi_lanes);
			return false;
		}
		return true;
	case PIPE_C:
		if (!pipe_has_enabled_pch(pipe_B_crtc) ||
		    pipe_B_crtc->config->fdi_lanes <= 2) {
			if (pipe_config->fdi_lanes > 2) {
				DRM_DEBUG_KMS("invalid shared fdi lane config on pipe %c: %i lanes\n",
					      pipe_name(pipe), pipe_config->fdi_lanes);
				return false;
			}
		} else {
			DRM_DEBUG_KMS("fdi link B uses too many lanes to enable link C\n");
			return false;
		}
		return true;
	default:
		BUG();
	}
}

#define RETRY 1
static int ironlake_fdi_compute_config(struct intel_crtc *intel_crtc,
				       struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	int lane, link_bw, fdi_dotclock;
	bool setup_ok, needs_recompute = false;

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

	setup_ok = ironlake_check_fdi_lanes(intel_crtc->base.dev,
					    intel_crtc->pipe, pipe_config);
	if (!setup_ok && pipe_config->pipe_bpp > 6*3) {
		pipe_config->pipe_bpp -= 2*3;
		DRM_DEBUG_KMS("fdi link bw constraint, reducing pipe bpp to %i\n",
			      pipe_config->pipe_bpp);
		needs_recompute = true;
		pipe_config->bw_constrained = true;

		goto retry;
	}

	if (needs_recompute)
		return RETRY;

	return setup_ok ? 0 : -EINVAL;
}

static void hsw_compute_ips_config(struct intel_crtc *crtc,
				   struct intel_crtc_state *pipe_config)
{
	pipe_config->ips_enabled = i915.enable_ips &&
				   hsw_crtc_supports_ips(crtc) &&
				   pipe_config->pipe_bpp <= 24;
}

static int intel_crtc_compute_config(struct intel_crtc *crtc,
				     struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;

	/* FIXME should check pixel clock limits on all platforms */
	if (INTEL_INFO(dev)->gen < 4) {
		int clock_limit =
			dev_priv->display.get_display_clock_speed(dev);

		/*
		 * Enable pixel doubling when the dot clock
		 * is > 90% of the (display) core speed.
		 *
		 * GDG double wide on either pipe,
		 * otherwise pipe A only.
		 */
		if ((crtc->pipe == PIPE_A || IS_I915G(dev)) &&
		    adjusted_mode->crtc_clock > clock_limit * 9 / 10) {
			clock_limit *= 2;
			pipe_config->double_wide = true;
		}

		if (adjusted_mode->crtc_clock > clock_limit * 9 / 10)
			return -EINVAL;
	}

	/*
	 * Pipe horizontal size must be even in:
	 * - DVO ganged mode
	 * - LVDS dual channel mode
	 * - Double wide pipe
	 */
	if ((intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS) &&
	     intel_is_dual_link_lvds(dev)) || pipe_config->double_wide)
		pipe_config->pipe_src_w &= ~1;

	/* Cantiga+ cannot handle modes with a hsync front porch of 0.
	 * WaPruneModeWithIncorrectHsyncOffset:ctg,elk,ilk,snb,ivb,vlv,hsw.
	 */
	if ((INTEL_INFO(dev)->gen > 4 || IS_G4X(dev)) &&
		adjusted_mode->hsync_start == adjusted_mode->hdisplay)
		return -EINVAL;

	if ((IS_G4X(dev) || IS_VALLEYVIEW(dev)) && pipe_config->pipe_bpp > 10*3) {
		pipe_config->pipe_bpp = 10*3; /* 12bpc is gen5+ */
	} else if (INTEL_INFO(dev)->gen <= 4 && pipe_config->pipe_bpp > 8*3) {
		/* only a 8bpc pipe, with 6bpc dither through the panel fitter
		 * for lvds. */
		pipe_config->pipe_bpp = 8*3;
	}

	if (HAS_IPS(dev))
		hsw_compute_ips_config(crtc, pipe_config);

	if (pipe_config->has_pch_encoder)
		return ironlake_fdi_compute_config(crtc, pipe_config);

	return 0;
}

static int valleyview_get_display_clock_speed(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val;
	int divider;

	/* FIXME: Punit isn't quite ready yet */
	if (IS_CHERRYVIEW(dev))
		return 400000;

	if (dev_priv->hpll_freq == 0)
		dev_priv->hpll_freq = valleyview_get_vco(dev_priv);

	mutex_lock(&dev_priv->dpio_lock);
	val = vlv_cck_read(dev_priv, CCK_DISPLAY_CLOCK_CONTROL);
	mutex_unlock(&dev_priv->dpio_lock);

	divider = val & DISPLAY_FREQUENCY_VALUES;

	WARN((val & DISPLAY_FREQUENCY_STATUS) !=
	     (divider << DISPLAY_FREQUENCY_STATUS_SHIFT),
	     "cdclk change in progress\n");

	return DIV_ROUND_CLOSEST(dev_priv->hpll_freq << 1, divider + 1);
}

static int i945_get_display_clock_speed(struct drm_device *dev)
{
	return 400000;
}

static int i915_get_display_clock_speed(struct drm_device *dev)
{
	return 333000;
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
		return 267000;
	case GC_DISPLAY_CLOCK_333_MHZ_PNV:
		return 333000;
	case GC_DISPLAY_CLOCK_444_MHZ_PNV:
		return 444000;
	case GC_DISPLAY_CLOCK_200_MHZ_PNV:
		return 200000;
	default:
		DRM_ERROR("Unknown pnv display core clock 0x%04x\n", gcfgc);
	case GC_DISPLAY_CLOCK_133_MHZ_PNV:
		return 133000;
	case GC_DISPLAY_CLOCK_167_MHZ_PNV:
		return 167000;
	}
}

static int i915gm_get_display_clock_speed(struct drm_device *dev)
{
	u16 gcfgc = 0;

	pci_read_config_word(dev->pdev, GCFGC, &gcfgc);

	if (gcfgc & GC_LOW_FREQUENCY_ENABLE)
		return 133000;
	else {
		switch (gcfgc & GC_DISPLAY_CLOCK_MASK) {
		case GC_DISPLAY_CLOCK_333_MHZ:
			return 333000;
		default:
		case GC_DISPLAY_CLOCK_190_200_MHZ:
			return 190000;
		}
	}
}

static int i865_get_display_clock_speed(struct drm_device *dev)
{
	return 266000;
}

static int i855_get_display_clock_speed(struct drm_device *dev)
{
	u16 hpllcc = 0;
	/* Assume that the hardware is in the high speed state.  This
	 * should be the default.
	 */
	switch (hpllcc & GC_CLOCK_CONTROL_MASK) {
	case GC_CLOCK_133_200:
	case GC_CLOCK_100_200:
		return 200000;
	case GC_CLOCK_166_250:
		return 250000;
	case GC_CLOCK_100_133:
		return 133000;
	}

	/* Shouldn't happen */
	return 0;
}

static int i830_get_display_clock_speed(struct drm_device *dev)
{
	return 133000;
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

static int i9xx_get_refclk(struct intel_crtc *crtc, int num_connectors)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int refclk;

	if (IS_VALLEYVIEW(dev)) {
		refclk = 100000;
	} else if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS) &&
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
	if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS) &&
	    reduced_clock && i915.powersave) {
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

static void vlv_update_pll(struct intel_crtc *crtc,
			   struct intel_crtc_state *pipe_config)
{
	u32 dpll, dpll_md;

	/*
	 * Enable DPIO clock input. We should never disable the reference
	 * clock for pipe B, since VGA hotplug / manual detection depends
	 * on it.
	 */
	dpll = DPLL_EXT_BUFFER_ENABLE_VLV | DPLL_REFA_CLK_ENABLE_VLV |
		DPLL_VGA_MODE_DIS | DPLL_INTEGRATED_CLOCK_VLV;
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

	mutex_lock(&dev_priv->dpio_lock);

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
	mutex_unlock(&dev_priv->dpio_lock);
}

static void chv_update_pll(struct intel_crtc *crtc,
			   struct intel_crtc_state *pipe_config)
{
	pipe_config->dpll_hw_state.dpll = DPLL_SSC_REF_CLOCK_CHV |
		DPLL_REFA_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS |
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
	int dpll_reg = DPLL(crtc->pipe);
	enum dpio_channel port = vlv_pipe_to_channel(pipe);
	u32 loopfilter, intcoeff;
	u32 bestn, bestm1, bestm2, bestp1, bestp2, bestm2_frac;
	int refclk;

	bestn = pipe_config->dpll.n;
	bestm2_frac = pipe_config->dpll.m2 & 0x3fffff;
	bestm1 = pipe_config->dpll.m1;
	bestm2 = pipe_config->dpll.m2 >> 22;
	bestp1 = pipe_config->dpll.p1;
	bestp2 = pipe_config->dpll.p2;

	/*
	 * Enable Refclk and SSC
	 */
	I915_WRITE(dpll_reg,
		   pipe_config->dpll_hw_state.dpll & ~DPLL_VCO_ENABLE);

	mutex_lock(&dev_priv->dpio_lock);

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
	vlv_dpio_write(dev_priv, pipe, CHV_PLL_DW3(port),
		       DPIO_CHV_FRAC_DIV_EN |
		       (2 << DPIO_CHV_FEEDFWD_GAIN_SHIFT));

	/* Loop filter */
	refclk = i9xx_get_refclk(crtc, 0);
	loopfilter = 5 << DPIO_CHV_PROP_COEFF_SHIFT |
		2 << DPIO_CHV_GAIN_CTRL_SHIFT;
	if (refclk == 100000)
		intcoeff = 11;
	else if (refclk == 38400)
		intcoeff = 10;
	else
		intcoeff = 9;
	loopfilter |= intcoeff << DPIO_CHV_INT_COEFF_SHIFT;
	vlv_dpio_write(dev_priv, pipe, CHV_PLL_DW6(port), loopfilter);

	/* AFC Recal */
	vlv_dpio_write(dev_priv, pipe, CHV_CMN_DW14(port),
			vlv_dpio_read(dev_priv, pipe, CHV_CMN_DW14(port)) |
			DPIO_AFC_RECAL);

	mutex_unlock(&dev_priv->dpio_lock);
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
		.pixel_multiplier = 1,
		.dpll = *dpll,
	};

	if (IS_CHERRYVIEW(dev)) {
		chv_update_pll(crtc, &pipe_config);
		chv_prepare_pll(crtc, &pipe_config);
		chv_enable_pll(crtc, &pipe_config);
	} else {
		vlv_update_pll(crtc, &pipe_config);
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

static void i9xx_update_pll(struct intel_crtc *crtc,
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

	is_sdvo = intel_pipe_will_have_type(crtc, INTEL_OUTPUT_SDVO) ||
		intel_pipe_will_have_type(crtc, INTEL_OUTPUT_HDMI);

	dpll = DPLL_VGA_MODE_DIS;

	if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS))
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
	else if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS) &&
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

static void i8xx_update_pll(struct intel_crtc *crtc,
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

	if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS)) {
		dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
	} else {
		if (clock->p1 == 2)
			dpll |= PLL_P1_DIVIDE_BY_TWO;
		else
			dpll |= (clock->p1 - 2) << DPLL_FPA01_P1_POST_DIV_SHIFT;
		if (clock->p2 == 4)
			dpll |= PLL_P2_DIVIDE_BY_4;
	}

	if (!IS_I830(dev) && intel_pipe_will_have_type(crtc, INTEL_OUTPUT_DVO))
		dpll |= DPLL_DVO_2X_MODE;

	if (intel_pipe_will_have_type(crtc, INTEL_OUTPUT_LVDS) &&
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
	struct drm_display_mode *adjusted_mode =
		&intel_crtc->config->base.adjusted_mode;
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

	mode->clock = pipe_config->base.adjusted_mode.crtc_clock;
	mode->flags |= pipe_config->base.adjusted_mode.flags;
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
	intel_clock_t clock, reduced_clock;
	bool ok, has_reduced_clock = false;
	bool is_lvds = false, is_dsi = false;
	struct intel_encoder *encoder;
	const intel_limit_t *limit;

	for_each_intel_encoder(dev, encoder) {
		if (encoder->new_crtc != crtc)
			continue;

		switch (encoder->type) {
		case INTEL_OUTPUT_LVDS:
			is_lvds = true;
			break;
		case INTEL_OUTPUT_DSI:
			is_dsi = true;
			break;
		default:
			break;
		}

		num_connectors++;
	}

	if (is_dsi)
		return 0;

	if (!crtc_state->clock_set) {
		refclk = i9xx_get_refclk(crtc, num_connectors);

		/*
		 * Returns a set of divisors for the desired target clock with
		 * the given refclk, or FALSE.  The returned values represent
		 * the clock equation: reflck * (5 * (m1 + 2) + (m2 + 2)) / (n +
		 * 2) / p1 / p2.
		 */
		limit = intel_limit(crtc, refclk);
		ok = dev_priv->display.find_dpll(limit, crtc,
						 crtc_state->port_clock,
						 refclk, NULL, &clock);
		if (!ok) {
			DRM_ERROR("Couldn't find PLL settings for mode!\n");
			return -EINVAL;
		}

		if (is_lvds && dev_priv->lvds_downclock_avail) {
			/*
			 * Ensure we match the reduced clock's P to the target
			 * clock.  If the clocks don't match, we can't switch
			 * the display clock by using the FP0/FP1. In such case
			 * we will disable the LVDS downclock feature.
			 */
			has_reduced_clock =
				dev_priv->display.find_dpll(limit, crtc,
							    dev_priv->lvds_downclock,
							    refclk, &clock,
							    &reduced_clock);
		}
		/* Compat-code for transition, will disappear. */
		crtc_state->dpll.n = clock.n;
		crtc_state->dpll.m1 = clock.m1;
		crtc_state->dpll.m2 = clock.m2;
		crtc_state->dpll.p1 = clock.p1;
		crtc_state->dpll.p2 = clock.p2;
	}

	if (IS_GEN2(dev)) {
		i8xx_update_pll(crtc, crtc_state,
				has_reduced_clock ? &reduced_clock : NULL,
				num_connectors);
	} else if (IS_CHERRYVIEW(dev)) {
		chv_update_pll(crtc, crtc_state);
	} else if (IS_VALLEYVIEW(dev)) {
		vlv_update_pll(crtc, crtc_state);
	} else {
		i9xx_update_pll(crtc, crtc_state,
				has_reduced_clock ? &reduced_clock : NULL,
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

	mutex_lock(&dev_priv->dpio_lock);
	mdiv = vlv_dpio_read(dev_priv, pipe, VLV_PLL_DW3(pipe));
	mutex_unlock(&dev_priv->dpio_lock);

	clock.m1 = (mdiv >> DPIO_M1DIV_SHIFT) & 7;
	clock.m2 = mdiv & DPIO_M2DIV_MASK;
	clock.n = (mdiv >> DPIO_N_SHIFT) & 0xf;
	clock.p1 = (mdiv >> DPIO_P1_SHIFT) & 7;
	clock.p2 = (mdiv >> DPIO_P2_SHIFT) & 0x1f;

	vlv_clock(refclk, &clock);

	/* clock.dot is the fast clock */
	pipe_config->port_clock = clock.dot / 5;
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
	int aligned_height;
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
	u32 cmn_dw13, pll_dw0, pll_dw1, pll_dw2;
	int refclk = 100000;

	mutex_lock(&dev_priv->dpio_lock);
	cmn_dw13 = vlv_dpio_read(dev_priv, pipe, CHV_CMN_DW13(port));
	pll_dw0 = vlv_dpio_read(dev_priv, pipe, CHV_PLL_DW0(port));
	pll_dw1 = vlv_dpio_read(dev_priv, pipe, CHV_PLL_DW1(port));
	pll_dw2 = vlv_dpio_read(dev_priv, pipe, CHV_PLL_DW2(port));
	mutex_unlock(&dev_priv->dpio_lock);

	clock.m1 = (pll_dw1 & 0x7) == DPIO_CHV_M1_DIV_BY_2 ? 2 : 0;
	clock.m2 = ((pll_dw0 & 0xff) << 22) | (pll_dw2 & 0x3fffff);
	clock.n = (pll_dw1 >> DPIO_CHV_N_DIV_SHIFT) & 0xf;
	clock.p1 = (cmn_dw13 >> DPIO_CHV_P1_DIV_SHIFT) & 0x7;
	clock.p2 = (cmn_dw13 >> DPIO_CHV_P2_DIV_SHIFT) & 0x1f;

	chv_clock(refclk, &clock);

	/* clock.dot is the fast clock */
	pipe_config->port_clock = clock.dot / 5;
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
	if (WARN(dev_priv->pch_id == INTEL_PCH_LPT_LP_DEVICE_ID_TYPE &&
		 with_fdi, "LP PCH doesn't have FDI\n"))
		with_fdi = false;

	mutex_lock(&dev_priv->dpio_lock);

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

	reg = (dev_priv->pch_id == INTEL_PCH_LPT_LP_DEVICE_ID_TYPE) ?
	       SBI_GEN0 : SBI_DBUFF0;
	tmp = intel_sbi_read(dev_priv, reg, SBI_ICLK);
	tmp |= SBI_GEN0_CFG_BUFFENABLE_DISABLE;
	intel_sbi_write(dev_priv, reg, tmp, SBI_ICLK);

	mutex_unlock(&dev_priv->dpio_lock);
}

/* Sequence to disable CLKOUT_DP */
static void lpt_disable_clkout_dp(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t reg, tmp;

	mutex_lock(&dev_priv->dpio_lock);

	reg = (dev_priv->pch_id == INTEL_PCH_LPT_LP_DEVICE_ID_TYPE) ?
	       SBI_GEN0 : SBI_DBUFF0;
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

	mutex_unlock(&dev_priv->dpio_lock);
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

static int ironlake_get_refclk(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *encoder;
	int num_connectors = 0;
	bool is_lvds = false;

	for_each_intel_encoder(dev, encoder) {
		if (encoder->new_crtc != to_intel_crtc(crtc))
			continue;

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
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int refclk;
	const intel_limit_t *limit;
	bool ret, is_lvds = false;

	is_lvds = intel_pipe_will_have_type(intel_crtc, INTEL_OUTPUT_LVDS);

	refclk = ironlake_get_refclk(crtc);

	/*
	 * Returns a set of divisors for the desired target clock with the given
	 * refclk, or FALSE.  The returned values represent the clock equation:
	 * reflck * (5 * (m1 + 2) + (m2 + 2)) / (n + 2) / p1 / p2.
	 */
	limit = intel_limit(intel_crtc, refclk);
	ret = dev_priv->display.find_dpll(limit, intel_crtc,
					  crtc_state->port_clock,
					  refclk, NULL, clock);
	if (!ret)
		return false;

	if (is_lvds && dev_priv->lvds_downclock_avail) {
		/*
		 * Ensure we match the reduced clock's P to the target clock.
		 * If the clocks don't match, we can't switch the display clock
		 * by using the FP0/FP1. In such case we will disable the LVDS
		 * downclock feature.
		*/
		*has_reduced_clock =
			dev_priv->display.find_dpll(limit, intel_crtc,
						    dev_priv->lvds_downclock,
						    refclk, clock,
						    reduced_clock);
	}

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
	struct intel_encoder *intel_encoder;
	uint32_t dpll;
	int factor, num_connectors = 0;
	bool is_lvds = false, is_sdvo = false;

	for_each_intel_encoder(dev, intel_encoder) {
		if (intel_encoder->new_crtc != to_intel_crtc(crtc))
			continue;

		switch (intel_encoder->type) {
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

	is_lvds = intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS);

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

	if (is_lvds && has_reduced_clock && i915.powersave)
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
	uint32_t tmp;

	tmp = I915_READ(PS_CTL(crtc->pipe));

	if (tmp & PS_ENABLE) {
		pipe_config->pch_pfit.enabled = true;
		pipe_config->pch_pfit.pos = I915_READ(PS_WIN_POS(crtc->pipe));
		pipe_config->pch_pfit.size = I915_READ(PS_WIN_SZ(crtc->pipe));
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
	int aligned_height;
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
	int aligned_height;
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
	I915_STATE_WARN(I915_READ(WRPLL_CTL1) & WRPLL_PLL_ENABLE, "WRPLL1 enabled\n");
	I915_STATE_WARN(I915_READ(WRPLL_CTL2) & WRPLL_PLL_ENABLE, "WRPLL2 enabled\n");
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

	if (dev_priv->pch_id == INTEL_PCH_LPT_LP_DEVICE_ID_TYPE) {
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

	if (dev_priv->pch_id == INTEL_PCH_LPT_LP_DEVICE_ID_TYPE) {
		val = I915_READ(SOUTH_DSPCLK_GATE_D);
		val |= PCH_LP_PARTITION_LEVEL_DISABLE;
		I915_WRITE(SOUTH_DSPCLK_GATE_D, val);
	}

	intel_prepare_ddi(dev);
}

static int haswell_crtc_compute_clock(struct intel_crtc *crtc,
				      struct intel_crtc_state *crtc_state)
{
	if (!intel_ddi_pll_select(crtc, crtc_state))
		return -EINVAL;

	crtc->lowfreq_avail = false;

	return 0;
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

	if (IS_SKYLAKE(dev))
		skylake_get_ddi_pll(dev_priv, port, pipe_config);
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

	pfit_domain = POWER_DOMAIN_PIPE_PANEL_FITTER(crtc->pipe);
	if (intel_display_power_is_enabled(dev_priv, pfit_domain)) {
		if (IS_SKYLAKE(dev))
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
		I915_WRITE(_CURACNTR, 0);
		POSTING_READ(_CURACNTR);
		intel_crtc->cursor_cntl = 0;
	}

	if (intel_crtc->cursor_base != base) {
		I915_WRITE(_CURABASE, base);
		intel_crtc->cursor_base = base;
	}

	if (intel_crtc->cursor_size != size) {
		I915_WRITE(CURSIZE, size);
		intel_crtc->cursor_size = size;
	}

	if (intel_crtc->cursor_cntl != cntl) {
		I915_WRITE(_CURACNTR, cntl);
		POSTING_READ(_CURACNTR);
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

		if (IS_HASWELL(dev) || IS_BROADWELL(dev))
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
	int x = crtc->cursor_x;
	int y = crtc->cursor_y;
	u32 base = 0, pos = 0;

	if (on)
		base = intel_crtc->cursor_addr;

	if (x >= intel_crtc->config->pipe_src_w)
		base = 0;

	if (y >= intel_crtc->config->pipe_src_h)
		base = 0;

	if (x < 0) {
		if (x + intel_crtc->base.cursor->state->crtc_w <= 0)
			base = 0;

		pos |= CURSOR_POS_SIGN << CURSOR_X_SHIFT;
		x = -x;
	}
	pos |= x << CURSOR_X_SHIFT;

	if (y < 0) {
		if (y + intel_crtc->base.cursor->state->crtc_h <= 0)
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
		base += (intel_crtc->base.cursor->state->crtc_h *
			intel_crtc->base.cursor->state->crtc_w - 1) * 4;
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
	if (!intel_fb) {
		drm_gem_object_unreference(&obj->base);
		return ERR_PTR(-ENOMEM);
	}

	ret = intel_framebuffer_init(dev, intel_fb, mode_cmd, obj);
	if (ret)
		goto err;

	return &intel_fb->base;
err:
	drm_gem_object_unreference(&obj->base);
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

	return intel_framebuffer_create(dev, &mode_cmd, obj);
}

static struct drm_framebuffer *
mode_fits_in_fbdev(struct drm_device *dev,
		   struct drm_display_mode *mode)
{
#ifdef CONFIG_DRM_I915_FBDEV
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
	int ret, i = -1;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
		      connector->base.id, connector->name,
		      encoder->base.id, encoder->name);

retry:
	ret = drm_modeset_lock(&config->connection_mutex, ctx);
	if (ret)
		goto fail_unlock;

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
			goto fail_unlock;
		ret = drm_modeset_lock(&crtc->primary->mutex, ctx);
		if (ret)
			goto fail_unlock;

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
		/* This can occur when applying the pipe A quirk on resume. */
		if (to_intel_crtc(possible_crtc)->new_enabled)
			continue;

		crtc = possible_crtc;
		break;
	}

	/*
	 * If we didn't find an unused CRTC, don't use any.
	 */
	if (!crtc) {
		DRM_DEBUG_KMS("no pipe available for load-detect\n");
		goto fail_unlock;
	}

	ret = drm_modeset_lock(&crtc->mutex, ctx);
	if (ret)
		goto fail_unlock;
	ret = drm_modeset_lock(&crtc->primary->mutex, ctx);
	if (ret)
		goto fail_unlock;
	intel_encoder->new_crtc = to_intel_crtc(crtc);
	to_intel_connector(connector)->new_encoder = intel_encoder;

	intel_crtc = to_intel_crtc(crtc);
	intel_crtc->new_enabled = true;
	intel_crtc->new_config = intel_crtc->config;
	old->dpms_mode = connector->dpms;
	old->load_detect_temp = true;
	old->release_fb = NULL;

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

	if (intel_set_mode(crtc, mode, 0, 0, fb)) {
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
	intel_crtc->new_enabled = crtc->state->enable;
	if (intel_crtc->new_enabled)
		intel_crtc->new_config = intel_crtc->config;
	else
		intel_crtc->new_config = NULL;
fail_unlock:
	if (ret == -EDEADLK) {
		drm_modeset_backoff(ctx);
		goto retry;
	}

	return false;
}

void intel_release_load_detect_pipe(struct drm_connector *connector,
				    struct intel_load_detect_pipe *old)
{
	struct intel_encoder *intel_encoder =
		intel_attached_encoder(connector);
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_crtc *crtc = encoder->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
		      connector->base.id, connector->name,
		      encoder->base.id, encoder->name);

	if (old->load_detect_temp) {
		to_intel_connector(connector)->new_encoder = NULL;
		intel_encoder->new_crtc = NULL;
		intel_crtc->new_enabled = false;
		intel_crtc->new_config = NULL;
		intel_set_mode(crtc, NULL, 0, 0, NULL);

		if (old->release_fb) {
			drm_framebuffer_unregister_private(old->release_fb);
			drm_framebuffer_unreference(old->release_fb);
		}

		return;
	}

	/* Switch crtc and encoder back off if necessary */
	if (old->dpms_mode != DRM_MODE_DPMS_ON)
		connector->funcs->dpms(connector, old->dpms_mode);
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
			pineview_clock(refclk, &clock);
		else
			i9xx_clock(refclk, &clock);
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

		i9xx_clock(refclk, &clock);
	}

	/*
	 * This value includes pixel_multiplier. We will use
	 * port_clock to compute adjusted_mode.crtc_clock in the
	 * encoder's get_config() function.
	 */
	pipe_config->port_clock = clock.dot;
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

static void intel_decrease_pllclock(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	if (!HAS_GMCH_DISPLAY(dev))
		return;

	if (!dev_priv->lvds_downclock_avail)
		return;

	/*
	 * Since this is called by a timer, we should never get here in
	 * the manual case.
	 */
	if (!HAS_PIPE_CXSR(dev) && intel_crtc->lowfreq_avail) {
		int pipe = intel_crtc->pipe;
		int dpll_reg = DPLL(pipe);
		int dpll;

		DRM_DEBUG_DRIVER("downclocking LVDS\n");

		assert_panel_unlocked(dev_priv, pipe);

		dpll = I915_READ(dpll_reg);
		dpll |= DISPLAY_RATE_SELECT_FPA1;
		I915_WRITE(dpll_reg, dpll);
		intel_wait_for_vblank(dev, pipe);
		dpll = I915_READ(dpll_reg);
		if (!(dpll & DISPLAY_RATE_SELECT_FPA1))
			DRM_DEBUG_DRIVER("failed to downclock LVDS!\n");
	}

}

void intel_mark_busy(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->mm.busy)
		return;

	intel_runtime_pm_get(dev_priv);
	i915_update_gfx_val(dev_priv);
	dev_priv->mm.busy = true;
}

void intel_mark_idle(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;

	if (!dev_priv->mm.busy)
		return;

	dev_priv->mm.busy = false;

	if (!i915.powersave)
		goto out;

	for_each_crtc(dev, crtc) {
		if (!crtc->primary->fb)
			continue;

		intel_decrease_pllclock(crtc);
	}

	if (INTEL_INFO(dev)->gen >= 6)
		gen6_rps_idle(dev->dev_private);

out:
	intel_runtime_pm_put(dev_priv);
}

static void intel_crtc_set_state(struct intel_crtc *crtc,
				 struct intel_crtc_state *crtc_state)
{
	kfree(crtc->config);
	crtc->config = crtc_state;
	crtc->base.state = &crtc_state->base;
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

	intel_crtc_set_state(intel_crtc, NULL);
	drm_crtc_cleanup(crtc);

	kfree(intel_crtc);
}

static void intel_unpin_work_fn(struct work_struct *__work)
{
	struct intel_unpin_work *work =
		container_of(__work, struct intel_unpin_work, work);
	struct drm_device *dev = work->crtc->dev;
	enum pipe pipe = to_intel_crtc(work->crtc)->pipe;

	mutex_lock(&dev->struct_mutex);
	intel_unpin_fb_obj(intel_fb_obj(work->old_fb));
	drm_gem_object_unreference(&work->pending_flip_obj->base);
	drm_framebuffer_unreference(work->old_fb);

	intel_fbc_update(dev);

	if (work->flip_queued_req)
		i915_gem_request_assign(&work->flip_queued_req, NULL);
	mutex_unlock(&dev->struct_mutex);

	intel_frontbuffer_flip_complete(dev, INTEL_FRONTBUFFER_PRIMARY(pipe));

	BUG_ON(atomic_read(&to_intel_crtc(work->crtc)->unpin_work_count) == 0);
	atomic_dec(&to_intel_crtc(work->crtc)->unpin_work_count);

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
		g4x_flip_count_after_eq(I915_READ(PIPE_FLIPCOUNT_GM45(crtc->pipe)),
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

static inline void intel_mark_page_flip_active(struct intel_crtc *intel_crtc)
{
	/* Ensure that the work item is consistent when activating it ... */
	smp_wmb();
	atomic_set(&intel_crtc->unpin_work->pending, INTEL_FLIP_PENDING);
	/* and that it is marked active as soon as the irq could fire. */
	smp_wmb();
}

static int intel_gen2_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct intel_engine_cs *ring,
				 uint32_t flags)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 flip_mask;
	int ret;

	ret = intel_ring_begin(ring, 6);
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

	intel_mark_page_flip_active(intel_crtc);
	__intel_ring_advance(ring);
	return 0;
}

static int intel_gen3_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct intel_engine_cs *ring,
				 uint32_t flags)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 flip_mask;
	int ret;

	ret = intel_ring_begin(ring, 6);
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

	intel_mark_page_flip_active(intel_crtc);
	__intel_ring_advance(ring);
	return 0;
}

static int intel_gen4_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct intel_engine_cs *ring,
				 uint32_t flags)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	uint32_t pf, pipesrc;
	int ret;

	ret = intel_ring_begin(ring, 4);
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

	intel_mark_page_flip_active(intel_crtc);
	__intel_ring_advance(ring);
	return 0;
}

static int intel_gen6_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct intel_engine_cs *ring,
				 uint32_t flags)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	uint32_t pf, pipesrc;
	int ret;

	ret = intel_ring_begin(ring, 4);
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

	intel_mark_page_flip_active(intel_crtc);
	__intel_ring_advance(ring);
	return 0;
}

static int intel_gen7_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct intel_engine_cs *ring,
				 uint32_t flags)
{
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
	ret = intel_ring_cacheline_align(ring);
	if (ret)
		return ret;

	ret = intel_ring_begin(ring, len);
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
		intel_ring_emit(ring, DERRMR);
		intel_ring_emit(ring, ~(DERRMR_PIPEA_PRI_FLIP_DONE |
					DERRMR_PIPEB_PRI_FLIP_DONE |
					DERRMR_PIPEC_PRI_FLIP_DONE));
		if (IS_GEN8(dev))
			intel_ring_emit(ring, MI_STORE_REGISTER_MEM_GEN8(1) |
					      MI_SRM_LRM_GLOBAL_GTT);
		else
			intel_ring_emit(ring, MI_STORE_REGISTER_MEM(1) |
					      MI_SRM_LRM_GLOBAL_GTT);
		intel_ring_emit(ring, DERRMR);
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

	intel_mark_page_flip_active(intel_crtc);
	__intel_ring_advance(ring);
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
	else
		return ring != i915_gem_request_get_ring(obj->last_read_req);
}

static void skl_do_mmio_flip(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = intel_crtc->base.primary->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	const enum pipe pipe = intel_crtc->pipe;
	u32 ctl, stride;

	ctl = I915_READ(PLANE_CTL(pipe, 0));
	ctl &= ~PLANE_CTL_TILED_MASK;
	if (obj->tiling_mode == I915_TILING_X)
		ctl |= PLANE_CTL_TILED_X;

	/*
	 * The stride is either expressed as a multiple of 64 bytes chunks for
	 * linear buffers or in number of tiles for tiled buffers.
	 */
	stride = fb->pitches[0] >> 6;
	if (obj->tiling_mode == I915_TILING_X)
		stride = fb->pitches[0] >> 9; /* X tiles are 512 bytes wide */

	/*
	 * Both PLANE_CTL and PLANE_STRIDE are not updated on vblank but on
	 * PLANE_SURF updates, the update is then guaranteed to be atomic.
	 */
	I915_WRITE(PLANE_CTL(pipe, 0), ctl);
	I915_WRITE(PLANE_STRIDE(pipe, 0), stride);

	I915_WRITE(PLANE_SURF(pipe, 0), intel_crtc->unpin_work->gtt_offset);
	POSTING_READ(PLANE_SURF(pipe, 0));
}

static void ilk_do_mmio_flip(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_framebuffer *intel_fb =
		to_intel_framebuffer(intel_crtc->base.primary->fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	u32 dspcntr;
	u32 reg;

	reg = DSPCNTR(intel_crtc->plane);
	dspcntr = I915_READ(reg);

	if (obj->tiling_mode != I915_TILING_NONE)
		dspcntr |= DISPPLANE_TILED;
	else
		dspcntr &= ~DISPPLANE_TILED;

	I915_WRITE(reg, dspcntr);

	I915_WRITE(DSPSURF(intel_crtc->plane),
		   intel_crtc->unpin_work->gtt_offset);
	POSTING_READ(DSPSURF(intel_crtc->plane));

}

/*
 * XXX: This is the temporary way to update the plane registers until we get
 * around to using the usual plane update functions for MMIO flips
 */
static void intel_do_mmio_flip(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;
	bool atomic_update;
	u32 start_vbl_count;

	intel_mark_page_flip_active(intel_crtc);

	atomic_update = intel_pipe_update_start(intel_crtc, &start_vbl_count);

	if (INTEL_INFO(dev)->gen >= 9)
		skl_do_mmio_flip(intel_crtc);
	else
		/* use_mmio_flip() retricts MMIO flips to ilk+ */
		ilk_do_mmio_flip(intel_crtc);

	if (atomic_update)
		intel_pipe_update_end(intel_crtc, start_vbl_count);
}

static void intel_mmio_flip_work_func(struct work_struct *work)
{
	struct intel_crtc *crtc =
		container_of(work, struct intel_crtc, mmio_flip.work);
	struct intel_mmio_flip *mmio_flip;

	mmio_flip = &crtc->mmio_flip;
	if (mmio_flip->req)
		WARN_ON(__i915_wait_request(mmio_flip->req,
					    crtc->reset_counter,
					    false, NULL, NULL) != 0);

	intel_do_mmio_flip(crtc);
	if (mmio_flip->req) {
		mutex_lock(&crtc->base.dev->struct_mutex);
		i915_gem_request_assign(&mmio_flip->req, NULL);
		mutex_unlock(&crtc->base.dev->struct_mutex);
	}
}

static int intel_queue_mmio_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj,
				 struct intel_engine_cs *ring,
				 uint32_t flags)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	i915_gem_request_assign(&intel_crtc->mmio_flip.req,
				obj->last_write_req);

	schedule_work(&intel_crtc->mmio_flip.work);

	return 0;
}

static int intel_default_queue_flip(struct drm_device *dev,
				    struct drm_crtc *crtc,
				    struct drm_framebuffer *fb,
				    struct drm_i915_gem_object *obj,
				    struct intel_engine_cs *ring,
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

	WARN_ON(!in_irq());

	if (crtc == NULL)
		return;

	spin_lock(&dev->event_lock);
	if (intel_crtc->unpin_work && __intel_pageflip_stall_check(dev, crtc)) {
		WARN_ONCE(1, "Kicking stuck page flip: queued at %d, now %d\n",
			 intel_crtc->unpin_work->flip_queued_vblank,
			 drm_vblank_count(dev, pipe));
		page_flip_completed(intel_crtc);
	}
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

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto cleanup;

	/* Reference the objects for the scheduled work. */
	drm_framebuffer_reference(work->old_fb);
	drm_gem_object_reference(&obj->base);

	crtc->primary->fb = fb;
	update_state_fb(crtc->primary);

	work->pending_flip_obj = obj;

	atomic_inc(&intel_crtc->unpin_work_count);
	intel_crtc->reset_counter = atomic_read(&dev_priv->gpu_error.reset_counter);

	if (INTEL_INFO(dev)->gen >= 5 || IS_G4X(dev))
		work->flip_count = I915_READ(PIPE_FLIPCOUNT_GM45(pipe)) + 1;

	if (IS_VALLEYVIEW(dev)) {
		ring = &dev_priv->ring[BCS];
		if (obj->tiling_mode != intel_fb_obj(work->old_fb)->tiling_mode)
			/* vlv: DISPLAY_FLIP fails to change tiling */
			ring = NULL;
	} else if (IS_IVYBRIDGE(dev) || IS_HASWELL(dev)) {
		ring = &dev_priv->ring[BCS];
	} else if (INTEL_INFO(dev)->gen >= 7) {
		ring = i915_gem_request_get_ring(obj->last_read_req);
		if (ring == NULL || ring->id != RCS)
			ring = &dev_priv->ring[BCS];
	} else {
		ring = &dev_priv->ring[RCS];
	}

	ret = intel_pin_and_fence_fb_obj(crtc->primary, fb, ring);
	if (ret)
		goto cleanup_pending;

	work->gtt_offset =
		i915_gem_obj_ggtt_offset(obj) + intel_crtc->dspaddr_offset;

	if (use_mmio_flip(ring, obj)) {
		ret = intel_queue_mmio_flip(dev, crtc, fb, obj, ring,
					    page_flip_flags);
		if (ret)
			goto cleanup_unpin;

		i915_gem_request_assign(&work->flip_queued_req,
					obj->last_write_req);
	} else {
		ret = dev_priv->display.queue_flip(dev, crtc, fb, obj, ring,
						   page_flip_flags);
		if (ret)
			goto cleanup_unpin;

		i915_gem_request_assign(&work->flip_queued_req,
					intel_ring_get_request(ring));
	}

	work->flip_queued_vblank = drm_crtc_vblank_count(crtc);
	work->enable_stall_check = true;

	i915_gem_track_fb(intel_fb_obj(work->old_fb), obj,
			  INTEL_FRONTBUFFER_PRIMARY(pipe));

	intel_fbc_disable(dev);
	intel_frontbuffer_flip_prepare(dev, INTEL_FRONTBUFFER_PRIMARY(pipe));
	mutex_unlock(&dev->struct_mutex);

	trace_i915_flip_request(intel_crtc->plane, obj);

	return 0;

cleanup_unpin:
	intel_unpin_fb_obj(obj);
cleanup_pending:
	atomic_dec(&intel_crtc->unpin_work_count);
	crtc->primary->fb = old_fb;
	update_state_fb(crtc->primary);
	drm_framebuffer_unreference(work->old_fb);
	drm_gem_object_unreference(&obj->base);
	mutex_unlock(&dev->struct_mutex);

cleanup:
	spin_lock_irq(&dev->event_lock);
	intel_crtc->unpin_work = NULL;
	spin_unlock_irq(&dev->event_lock);

	drm_crtc_vblank_put(crtc);
free_work:
	kfree(work);

	if (ret == -EIO) {
out_hang:
		ret = intel_plane_restore(primary);
		if (ret == 0 && event) {
			spin_lock_irq(&dev->event_lock);
			drm_send_vblank_event(dev, pipe, event);
			spin_unlock_irq(&dev->event_lock);
		}
	}
	return ret;
}

static struct drm_crtc_helper_funcs intel_helper_funcs = {
	.mode_set_base_atomic = intel_pipe_set_base_atomic,
	.load_lut = intel_crtc_load_lut,
	.atomic_begin = intel_begin_crtc_commit,
	.atomic_flush = intel_finish_crtc_commit,
};

/**
 * intel_modeset_update_staged_output_state
 *
 * Updates the staged output configuration state, e.g. after we've read out the
 * current hw state.
 */
static void intel_modeset_update_staged_output_state(struct drm_device *dev)
{
	struct intel_crtc *crtc;
	struct intel_encoder *encoder;
	struct intel_connector *connector;

	for_each_intel_connector(dev, connector) {
		connector->new_encoder =
			to_intel_encoder(connector->base.encoder);
	}

	for_each_intel_encoder(dev, encoder) {
		encoder->new_crtc =
			to_intel_crtc(encoder->base.crtc);
	}

	for_each_intel_crtc(dev, crtc) {
		crtc->new_enabled = crtc->base.state->enable;

		if (crtc->new_enabled)
			crtc->new_config = crtc->config;
		else
			crtc->new_config = NULL;
	}
}

/**
 * intel_modeset_commit_output_state
 *
 * This function copies the stage display pipe configuration to the real one.
 */
static void intel_modeset_commit_output_state(struct drm_device *dev)
{
	struct intel_crtc *crtc;
	struct intel_encoder *encoder;
	struct intel_connector *connector;

	for_each_intel_connector(dev, connector) {
		connector->base.encoder = &connector->new_encoder->base;
	}

	for_each_intel_encoder(dev, encoder) {
		encoder->base.crtc = &encoder->new_crtc->base;
	}

	for_each_intel_crtc(dev, crtc) {
		crtc->base.state->enable = crtc->new_enabled;
		crtc->base.enabled = crtc->new_enabled;
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
			  struct drm_framebuffer *fb,
			  struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_connector *connector;
	int bpp;

	switch (fb->pixel_format) {
	case DRM_FORMAT_C8:
		bpp = 8*3; /* since we go through a colormap */
		break;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_ARGB1555:
		/* checked in intel_framebuffer_init already */
		if (WARN_ON(INTEL_INFO(dev)->gen > 3))
			return -EINVAL;
	case DRM_FORMAT_RGB565:
		bpp = 6*3; /* min is 18bpp */
		break;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		/* checked in intel_framebuffer_init already */
		if (WARN_ON(INTEL_INFO(dev)->gen < 4))
			return -EINVAL;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		bpp = 8*3;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
		/* checked in intel_framebuffer_init already */
		if (WARN_ON(INTEL_INFO(dev)->gen < 4))
			return -EINVAL;
		bpp = 10*3;
		break;
	/* TODO: gen4+ supports 16 bpc floating point, too. */
	default:
		DRM_DEBUG_KMS("unsupported depth\n");
		return -EINVAL;
	}

	pipe_config->pipe_bpp = bpp;

	/* Clamp display bpp to EDID value */
	for_each_intel_connector(dev, connector) {
		if (!connector->new_encoder ||
		    connector->new_encoder->new_crtc != crtc)
			continue;

		connected_sink_compute_bpp(connector, pipe_config);
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
	DRM_DEBUG_KMS("[CRTC:%d]%s config for pipe %c\n", crtc->base.base.id,
		      context, pipe_name(crtc->pipe));

	DRM_DEBUG_KMS("cpu_transcoder: %c\n", transcoder_name(pipe_config->cpu_transcoder));
	DRM_DEBUG_KMS("pipe bpp: %i, dithering: %i\n",
		      pipe_config->pipe_bpp, pipe_config->dither);
	DRM_DEBUG_KMS("fdi/pch: %i, lanes: %i, gmch_m: %u, gmch_n: %u, link_m: %u, link_n: %u, tu: %u\n",
		      pipe_config->has_pch_encoder,
		      pipe_config->fdi_lanes,
		      pipe_config->fdi_m_n.gmch_m, pipe_config->fdi_m_n.gmch_n,
		      pipe_config->fdi_m_n.link_m, pipe_config->fdi_m_n.link_n,
		      pipe_config->fdi_m_n.tu);
	DRM_DEBUG_KMS("dp: %i, gmch_m: %u, gmch_n: %u, link_m: %u, link_n: %u, tu: %u\n",
		      pipe_config->has_dp_encoder,
		      pipe_config->dp_m_n.gmch_m, pipe_config->dp_m_n.gmch_n,
		      pipe_config->dp_m_n.link_m, pipe_config->dp_m_n.link_n,
		      pipe_config->dp_m_n.tu);

	DRM_DEBUG_KMS("dp: %i, gmch_m2: %u, gmch_n2: %u, link_m2: %u, link_n2: %u, tu2: %u\n",
		      pipe_config->has_dp_encoder,
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
}

static bool encoders_cloneable(const struct intel_encoder *a,
			       const struct intel_encoder *b)
{
	/* masks could be asymmetric, so check both ways */
	return a == b || (a->cloneable & (1 << b->type) &&
			  b->cloneable & (1 << a->type));
}

static bool check_single_encoder_cloning(struct intel_crtc *crtc,
					 struct intel_encoder *encoder)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_encoder *source_encoder;

	for_each_intel_encoder(dev, source_encoder) {
		if (source_encoder->new_crtc != crtc)
			continue;

		if (!encoders_cloneable(encoder, source_encoder))
			return false;
	}

	return true;
}

static bool check_encoder_cloning(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_encoder *encoder;

	for_each_intel_encoder(dev, encoder) {
		if (encoder->new_crtc != crtc)
			continue;

		if (!check_single_encoder_cloning(crtc, encoder))
			return false;
	}

	return true;
}

static bool check_digital_port_conflicts(struct drm_device *dev)
{
	struct intel_connector *connector;
	unsigned int used_ports = 0;

	/*
	 * Walk the connector list instead of the encoder
	 * list to detect the problem on ddi platforms
	 * where there's just one encoder per digital port.
	 */
	for_each_intel_connector(dev, connector) {
		struct intel_encoder *encoder = connector->new_encoder;

		if (!encoder)
			continue;

		WARN_ON(!encoder->new_crtc);

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

static struct intel_crtc_state *
intel_modeset_pipe_config(struct drm_crtc *crtc,
			  struct drm_framebuffer *fb,
			  struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	struct intel_encoder *encoder;
	struct intel_crtc_state *pipe_config;
	int plane_bpp, ret = -EINVAL;
	bool retry = true;

	if (!check_encoder_cloning(to_intel_crtc(crtc))) {
		DRM_DEBUG_KMS("rejecting invalid cloning configuration\n");
		return ERR_PTR(-EINVAL);
	}

	if (!check_digital_port_conflicts(dev)) {
		DRM_DEBUG_KMS("rejecting conflicting digital port configuration\n");
		return ERR_PTR(-EINVAL);
	}

	pipe_config = kzalloc(sizeof(*pipe_config), GFP_KERNEL);
	if (!pipe_config)
		return ERR_PTR(-ENOMEM);

	pipe_config->base.crtc = crtc;
	drm_mode_copy(&pipe_config->base.adjusted_mode, mode);
	drm_mode_copy(&pipe_config->base.mode, mode);

	pipe_config->cpu_transcoder =
		(enum transcoder) to_intel_crtc(crtc)->pipe;
	pipe_config->shared_dpll = DPLL_ID_PRIVATE;

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

	/* Compute a starting value for pipe_config->pipe_bpp taking the source
	 * plane pixel format and any sink constraints into account. Returns the
	 * source plane bpp so that dithering can be selected on mismatches
	 * after encoders and crtc also have had their say. */
	plane_bpp = compute_baseline_pipe_bpp(to_intel_crtc(crtc),
					      fb, pipe_config);
	if (plane_bpp < 0)
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
	for_each_intel_encoder(dev, encoder) {

		if (&encoder->new_crtc->base != crtc)
			continue;

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

	pipe_config->dither = pipe_config->pipe_bpp != plane_bpp;
	DRM_DEBUG_KMS("plane bpp: %i, pipe bpp: %i, dithering: %i\n",
		      plane_bpp, pipe_config->pipe_bpp, pipe_config->dither);

	return pipe_config;
fail:
	kfree(pipe_config);
	return ERR_PTR(ret);
}

/* Computes which crtcs are affected and sets the relevant bits in the mask. For
 * simplicity we use the crtc's pipe number (because it's easier to obtain). */
static void
intel_modeset_affected_pipes(struct drm_crtc *crtc, unsigned *modeset_pipes,
			     unsigned *prepare_pipes, unsigned *disable_pipes)
{
	struct intel_crtc *intel_crtc;
	struct drm_device *dev = crtc->dev;
	struct intel_encoder *encoder;
	struct intel_connector *connector;
	struct drm_crtc *tmp_crtc;

	*disable_pipes = *modeset_pipes = *prepare_pipes = 0;

	/* Check which crtcs have changed outputs connected to them, these need
	 * to be part of the prepare_pipes mask. We don't (yet) support global
	 * modeset across multiple crtcs, so modeset_pipes will only have one
	 * bit set at most. */
	for_each_intel_connector(dev, connector) {
		if (connector->base.encoder == &connector->new_encoder->base)
			continue;

		if (connector->base.encoder) {
			tmp_crtc = connector->base.encoder->crtc;

			*prepare_pipes |= 1 << to_intel_crtc(tmp_crtc)->pipe;
		}

		if (connector->new_encoder)
			*prepare_pipes |=
				1 << connector->new_encoder->new_crtc->pipe;
	}

	for_each_intel_encoder(dev, encoder) {
		if (encoder->base.crtc == &encoder->new_crtc->base)
			continue;

		if (encoder->base.crtc) {
			tmp_crtc = encoder->base.crtc;

			*prepare_pipes |= 1 << to_intel_crtc(tmp_crtc)->pipe;
		}

		if (encoder->new_crtc)
			*prepare_pipes |= 1 << encoder->new_crtc->pipe;
	}

	/* Check for pipes that will be enabled/disabled ... */
	for_each_intel_crtc(dev, intel_crtc) {
		if (intel_crtc->base.state->enable == intel_crtc->new_enabled)
			continue;

		if (!intel_crtc->new_enabled)
			*disable_pipes |= 1 << intel_crtc->pipe;
		else
			*prepare_pipes |= 1 << intel_crtc->pipe;
	}


	/* set_mode is also used to update properties on life display pipes. */
	intel_crtc = to_intel_crtc(crtc);
	if (intel_crtc->new_enabled)
		*prepare_pipes |= 1 << intel_crtc->pipe;

	/*
	 * For simplicity do a full modeset on any pipe where the output routing
	 * changed. We could be more clever, but that would require us to be
	 * more careful with calling the relevant encoder->mode_set functions.
	 */
	if (*prepare_pipes)
		*modeset_pipes = *prepare_pipes;

	/* ... and mask these out. */
	*modeset_pipes &= ~(*disable_pipes);
	*prepare_pipes &= ~(*disable_pipes);

	/*
	 * HACK: We don't (yet) fully support global modesets. intel_set_config
	 * obies this rule, but the modeset restore mode of
	 * intel_modeset_setup_hw_state does not.
	 */
	*modeset_pipes &= 1 << intel_crtc->pipe;
	*prepare_pipes &= 1 << intel_crtc->pipe;

	DRM_DEBUG_KMS("set mode pipe masks: modeset: %x, prepare: %x, disable: %x\n",
		      *modeset_pipes, *prepare_pipes, *disable_pipes);
}

static bool intel_crtc_in_use(struct drm_crtc *crtc)
{
	struct drm_encoder *encoder;
	struct drm_device *dev = crtc->dev;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head)
		if (encoder->crtc == crtc)
			return true;

	return false;
}

static void
intel_modeset_update_state(struct drm_device *dev, unsigned prepare_pipes)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder;
	struct intel_crtc *intel_crtc;
	struct drm_connector *connector;

	intel_shared_dpll_commit(dev_priv);

	for_each_intel_encoder(dev, intel_encoder) {
		if (!intel_encoder->base.crtc)
			continue;

		intel_crtc = to_intel_crtc(intel_encoder->base.crtc);

		if (prepare_pipes & (1 << intel_crtc->pipe))
			intel_encoder->connectors_active = false;
	}

	intel_modeset_commit_output_state(dev);

	/* Double check state. */
	for_each_intel_crtc(dev, intel_crtc) {
		WARN_ON(intel_crtc->base.state->enable != intel_crtc_in_use(&intel_crtc->base));
		WARN_ON(intel_crtc->new_config &&
			intel_crtc->new_config != intel_crtc->config);
		WARN_ON(intel_crtc->base.state->enable != !!intel_crtc->new_config);
	}

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (!connector->encoder || !connector->encoder->crtc)
			continue;

		intel_crtc = to_intel_crtc(connector->encoder->crtc);

		if (prepare_pipes & (1 << intel_crtc->pipe)) {
			struct drm_property *dpms_property =
				dev->mode_config.dpms_property;

			connector->dpms = DRM_MODE_DPMS_ON;
			drm_object_property_set_value(&connector->base,
							 dpms_property,
							 DRM_MODE_DPMS_ON);

			intel_encoder = to_intel_encoder(connector->encoder);
			intel_encoder->connectors_active = true;
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
intel_pipe_config_compare(struct drm_device *dev,
			  struct intel_crtc_state *current_config,
			  struct intel_crtc_state *pipe_config)
{
#define PIPE_CONF_CHECK_X(name)	\
	if (current_config->name != pipe_config->name) { \
		DRM_ERROR("mismatch in " #name " " \
			  "(expected 0x%08x, found 0x%08x)\n", \
			  current_config->name, \
			  pipe_config->name); \
		return false; \
	}

#define PIPE_CONF_CHECK_I(name)	\
	if (current_config->name != pipe_config->name) { \
		DRM_ERROR("mismatch in " #name " " \
			  "(expected %i, found %i)\n", \
			  current_config->name, \
			  pipe_config->name); \
		return false; \
	}

/* This is required for BDW+ where there is only one set of registers for
 * switching between high and low RR.
 * This macro can be used whenever a comparison has to be made between one
 * hw state and multiple sw state variables.
 */
#define PIPE_CONF_CHECK_I_ALT(name, alt_name) \
	if ((current_config->name != pipe_config->name) && \
		(current_config->alt_name != pipe_config->name)) { \
			DRM_ERROR("mismatch in " #name " " \
				  "(expected %i or %i, found %i)\n", \
				  current_config->name, \
				  current_config->alt_name, \
				  pipe_config->name); \
			return false; \
	}

#define PIPE_CONF_CHECK_FLAGS(name, mask)	\
	if ((current_config->name ^ pipe_config->name) & (mask)) { \
		DRM_ERROR("mismatch in " #name "(" #mask ") "	   \
			  "(expected %i, found %i)\n", \
			  current_config->name & (mask), \
			  pipe_config->name & (mask)); \
		return false; \
	}

#define PIPE_CONF_CHECK_CLOCK_FUZZY(name) \
	if (!intel_fuzzy_clock_check(current_config->name, pipe_config->name)) { \
		DRM_ERROR("mismatch in " #name " " \
			  "(expected %i, found %i)\n", \
			  current_config->name, \
			  pipe_config->name); \
		return false; \
	}

#define PIPE_CONF_QUIRK(quirk)	\
	((current_config->quirks | pipe_config->quirks) & (quirk))

	PIPE_CONF_CHECK_I(cpu_transcoder);

	PIPE_CONF_CHECK_I(has_pch_encoder);
	PIPE_CONF_CHECK_I(fdi_lanes);
	PIPE_CONF_CHECK_I(fdi_m_n.gmch_m);
	PIPE_CONF_CHECK_I(fdi_m_n.gmch_n);
	PIPE_CONF_CHECK_I(fdi_m_n.link_m);
	PIPE_CONF_CHECK_I(fdi_m_n.link_n);
	PIPE_CONF_CHECK_I(fdi_m_n.tu);

	PIPE_CONF_CHECK_I(has_dp_encoder);

	if (INTEL_INFO(dev)->gen < 8) {
		PIPE_CONF_CHECK_I(dp_m_n.gmch_m);
		PIPE_CONF_CHECK_I(dp_m_n.gmch_n);
		PIPE_CONF_CHECK_I(dp_m_n.link_m);
		PIPE_CONF_CHECK_I(dp_m_n.link_n);
		PIPE_CONF_CHECK_I(dp_m_n.tu);

		if (current_config->has_drrs) {
			PIPE_CONF_CHECK_I(dp_m2_n2.gmch_m);
			PIPE_CONF_CHECK_I(dp_m2_n2.gmch_n);
			PIPE_CONF_CHECK_I(dp_m2_n2.link_m);
			PIPE_CONF_CHECK_I(dp_m2_n2.link_n);
			PIPE_CONF_CHECK_I(dp_m2_n2.tu);
		}
	} else {
		PIPE_CONF_CHECK_I_ALT(dp_m_n.gmch_m, dp_m2_n2.gmch_m);
		PIPE_CONF_CHECK_I_ALT(dp_m_n.gmch_n, dp_m2_n2.gmch_n);
		PIPE_CONF_CHECK_I_ALT(dp_m_n.link_m, dp_m2_n2.link_m);
		PIPE_CONF_CHECK_I_ALT(dp_m_n.link_n, dp_m2_n2.link_n);
		PIPE_CONF_CHECK_I_ALT(dp_m_n.tu, dp_m2_n2.tu);
	}

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

	PIPE_CONF_CHECK_I(pipe_src_w);
	PIPE_CONF_CHECK_I(pipe_src_h);

	/*
	 * FIXME: BIOS likes to set up a cloned config with lvds+external
	 * screen. Since we don't yet re-compute the pipe config when moving
	 * just the lvds port away to another pipe the sw tracking won't match.
	 *
	 * Proper atomic modesets with recomputed global state will fix this.
	 * Until then just don't check gmch state for inherited modes.
	 */
	if (!PIPE_CONF_QUIRK(PIPE_CONFIG_QUIRK_INHERITED_MODE)) {
		PIPE_CONF_CHECK_I(gmch_pfit.control);
		/* pfit ratios are autocomputed by the hw on gen4+ */
		if (INTEL_INFO(dev)->gen < 4)
			PIPE_CONF_CHECK_I(gmch_pfit.pgm_ratios);
		PIPE_CONF_CHECK_I(gmch_pfit.lvds_border_bits);
	}

	PIPE_CONF_CHECK_I(pch_pfit.enabled);
	if (current_config->pch_pfit.enabled) {
		PIPE_CONF_CHECK_I(pch_pfit.pos);
		PIPE_CONF_CHECK_I(pch_pfit.size);
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

	return true;
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
		hw_entry = &hw_ddb.cursor[pipe];
		sw_entry = &sw_ddb->cursor[pipe];

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
check_connector_state(struct drm_device *dev)
{
	struct intel_connector *connector;

	for_each_intel_connector(dev, connector) {
		/* This also checks the encoder/connector hw state with the
		 * ->get_hw_state callbacks. */
		intel_connector_check_state(connector);

		I915_STATE_WARN(&connector->new_encoder->base != connector->base.encoder,
		     "connector's staged encoder doesn't match current encoder\n");
	}
}

static void
check_encoder_state(struct drm_device *dev)
{
	struct intel_encoder *encoder;
	struct intel_connector *connector;

	for_each_intel_encoder(dev, encoder) {
		bool enabled = false;
		bool active = false;
		enum pipe pipe, tracked_pipe;

		DRM_DEBUG_KMS("[ENCODER:%d:%s]\n",
			      encoder->base.base.id,
			      encoder->base.name);

		I915_STATE_WARN(&encoder->new_crtc->base != encoder->base.crtc,
		     "encoder's stage crtc doesn't match current crtc\n");
		I915_STATE_WARN(encoder->connectors_active && !encoder->base.crtc,
		     "encoder's active_connectors set, but no crtc\n");

		for_each_intel_connector(dev, connector) {
			if (connector->base.encoder != &encoder->base)
				continue;
			enabled = true;
			if (connector->base.dpms != DRM_MODE_DPMS_OFF)
				active = true;
		}
		/*
		 * for MST connectors if we unplug the connector is gone
		 * away but the encoder is still connected to a crtc
		 * until a modeset happens in response to the hotplug.
		 */
		if (!enabled && encoder->base.encoder_type == DRM_MODE_ENCODER_DPMST)
			continue;

		I915_STATE_WARN(!!encoder->base.crtc != enabled,
		     "encoder's enabled state mismatch "
		     "(expected %i, found %i)\n",
		     !!encoder->base.crtc, enabled);
		I915_STATE_WARN(active && !encoder->base.crtc,
		     "active encoder with no crtc\n");

		I915_STATE_WARN(encoder->connectors_active != active,
		     "encoder's computed active state doesn't match tracked active state "
		     "(expected %i, found %i)\n", active, encoder->connectors_active);

		active = encoder->get_hw_state(encoder, &pipe);
		I915_STATE_WARN(active != encoder->connectors_active,
		     "encoder's hw state doesn't match sw tracking "
		     "(expected %i, found %i)\n",
		     encoder->connectors_active, active);

		if (!encoder->base.crtc)
			continue;

		tracked_pipe = to_intel_crtc(encoder->base.crtc)->pipe;
		I915_STATE_WARN(active && pipe != tracked_pipe,
		     "active encoder's pipe doesn't match"
		     "(expected %i, found %i)\n",
		     tracked_pipe, pipe);

	}
}

static void
check_crtc_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc;
	struct intel_encoder *encoder;
	struct intel_crtc_state pipe_config;

	for_each_intel_crtc(dev, crtc) {
		bool enabled = false;
		bool active = false;

		memset(&pipe_config, 0, sizeof(pipe_config));

		DRM_DEBUG_KMS("[CRTC:%d]\n",
			      crtc->base.base.id);

		I915_STATE_WARN(crtc->active && !crtc->base.state->enable,
		     "active crtc, but not enabled in sw tracking\n");

		for_each_intel_encoder(dev, encoder) {
			if (encoder->base.crtc != &crtc->base)
				continue;
			enabled = true;
			if (encoder->connectors_active)
				active = true;
		}

		I915_STATE_WARN(active != crtc->active,
		     "crtc's computed active state doesn't match tracked active state "
		     "(expected %i, found %i)\n", active, crtc->active);
		I915_STATE_WARN(enabled != crtc->base.state->enable,
		     "crtc's computed enabled state doesn't match tracked enabled state "
		     "(expected %i, found %i)\n", enabled,
				crtc->base.state->enable);

		active = dev_priv->display.get_pipe_config(crtc,
							   &pipe_config);

		/* hw state is inconsistent with the pipe quirk */
		if ((crtc->pipe == PIPE_A && dev_priv->quirks & QUIRK_PIPEA_FORCE) ||
		    (crtc->pipe == PIPE_B && dev_priv->quirks & QUIRK_PIPEB_FORCE))
			active = crtc->active;

		for_each_intel_encoder(dev, encoder) {
			enum pipe pipe;
			if (encoder->base.crtc != &crtc->base)
				continue;
			if (encoder->get_hw_state(encoder, &pipe))
				encoder->get_config(encoder, &pipe_config);
		}

		I915_STATE_WARN(crtc->active != active,
		     "crtc active state doesn't match with hw state "
		     "(expected %i, found %i)\n", crtc->active, active);

		if (active &&
		    !intel_pipe_config_compare(dev, crtc->config, &pipe_config)) {
			I915_STATE_WARN(1, "pipe state doesn't match!\n");
			intel_dump_pipe_config(crtc, &pipe_config,
					       "[hw state]");
			intel_dump_pipe_config(crtc, crtc->config,
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

void
intel_modeset_check_state(struct drm_device *dev)
{
	check_wm_state(dev);
	check_connector_state(dev);
	check_encoder_state(dev);
	check_crtc_state(dev);
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
		const struct drm_display_mode *mode = &crtc->config->base.adjusted_mode;
		int vtotal;

		vtotal = mode->crtc_vtotal;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			vtotal /= 2;

		crtc->scanline_offset = vtotal - 1;
	} else if (HAS_DDI(dev) &&
		   intel_pipe_has_type(crtc, INTEL_OUTPUT_HDMI)) {
		crtc->scanline_offset = 2;
	} else
		crtc->scanline_offset = 1;
}

static struct intel_crtc_state *
intel_modeset_compute_config(struct drm_crtc *crtc,
			     struct drm_display_mode *mode,
			     struct drm_framebuffer *fb,
			     unsigned *modeset_pipes,
			     unsigned *prepare_pipes,
			     unsigned *disable_pipes)
{
	struct intel_crtc_state *pipe_config = NULL;

	intel_modeset_affected_pipes(crtc, modeset_pipes,
				     prepare_pipes, disable_pipes);

	if ((*modeset_pipes) == 0)
		goto out;

	/*
	 * Note this needs changes when we start tracking multiple modes
	 * and crtcs.  At that point we'll need to compute the whole config
	 * (i.e. one pipe_config for each crtc) rather than just the one
	 * for this crtc.
	 */
	pipe_config = intel_modeset_pipe_config(crtc, fb, mode);
	if (IS_ERR(pipe_config)) {
		goto out;
	}
	intel_dump_pipe_config(to_intel_crtc(crtc), pipe_config,
			       "[modeset]");

out:
	return pipe_config;
}

static int __intel_set_mode_setup_plls(struct drm_device *dev,
				       unsigned modeset_pipes,
				       unsigned disable_pipes)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned clear_pipes = modeset_pipes | disable_pipes;
	struct intel_crtc *intel_crtc;
	int ret = 0;

	if (!dev_priv->display.crtc_compute_clock)
		return 0;

	ret = intel_shared_dpll_start_config(dev_priv, clear_pipes);
	if (ret)
		goto done;

	for_each_intel_crtc_masked(dev, modeset_pipes, intel_crtc) {
		struct intel_crtc_state *state = intel_crtc->new_config;
		ret = dev_priv->display.crtc_compute_clock(intel_crtc,
							   state);
		if (ret) {
			intel_shared_dpll_abort_config(dev_priv);
			goto done;
		}
	}

done:
	return ret;
}

static int __intel_set_mode(struct drm_crtc *crtc,
			    struct drm_display_mode *mode,
			    int x, int y, struct drm_framebuffer *fb,
			    struct intel_crtc_state *pipe_config,
			    unsigned modeset_pipes,
			    unsigned prepare_pipes,
			    unsigned disable_pipes)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_display_mode *saved_mode;
	struct intel_crtc *intel_crtc;
	int ret = 0;

	saved_mode = kmalloc(sizeof(*saved_mode), GFP_KERNEL);
	if (!saved_mode)
		return -ENOMEM;

	*saved_mode = crtc->mode;

	if (modeset_pipes)
		to_intel_crtc(crtc)->new_config = pipe_config;

	/*
	 * See if the config requires any additional preparation, e.g.
	 * to adjust global state with pipes off.  We need to do this
	 * here so we can get the modeset_pipe updated config for the new
	 * mode set on this crtc.  For other crtcs we need to use the
	 * adjusted_mode bits in the crtc directly.
	 */
	if (IS_VALLEYVIEW(dev)) {
		valleyview_modeset_global_pipes(dev, &prepare_pipes);

		/* may have added more to prepare_pipes than we should */
		prepare_pipes &= ~disable_pipes;
	}

	ret = __intel_set_mode_setup_plls(dev, modeset_pipes, disable_pipes);
	if (ret)
		goto done;

	for_each_intel_crtc_masked(dev, disable_pipes, intel_crtc)
		intel_crtc_disable(&intel_crtc->base);

	for_each_intel_crtc_masked(dev, prepare_pipes, intel_crtc) {
		if (intel_crtc->base.state->enable)
			dev_priv->display.crtc_disable(&intel_crtc->base);
	}

	/* crtc->mode is already used by the ->mode_set callbacks, hence we need
	 * to set it here already despite that we pass it down the callchain.
	 *
	 * Note we'll need to fix this up when we start tracking multiple
	 * pipes; here we assume a single modeset_pipe and only track the
	 * single crtc and mode.
	 */
	if (modeset_pipes) {
		crtc->mode = *mode;
		/* mode_set/enable/disable functions rely on a correct pipe
		 * config. */
		intel_crtc_set_state(to_intel_crtc(crtc), pipe_config);

		/*
		 * Calculate and store various constants which
		 * are later needed by vblank and swap-completion
		 * timestamping. They are derived from true hwmode.
		 */
		drm_calc_timestamping_constants(crtc,
						&pipe_config->base.adjusted_mode);
	}

	/* Only after disabling all output pipelines that will be changed can we
	 * update the the output configuration. */
	intel_modeset_update_state(dev, prepare_pipes);

	modeset_update_crtc_power_domains(dev);

	/* Set up the DPLL and any encoders state that needs to adjust or depend
	 * on the DPLL.
	 */
	for_each_intel_crtc_masked(dev, modeset_pipes, intel_crtc) {
		struct drm_plane *primary = intel_crtc->base.primary;
		int vdisplay, hdisplay;

		drm_crtc_get_hv_timing(mode, &hdisplay, &vdisplay);
		ret = primary->funcs->update_plane(primary, &intel_crtc->base,
						   fb, 0, 0,
						   hdisplay, vdisplay,
						   x << 16, y << 16,
						   hdisplay << 16, vdisplay << 16);
	}

	/* Now enable the clocks, plane, pipe, and connectors that we set up. */
	for_each_intel_crtc_masked(dev, prepare_pipes, intel_crtc) {
		update_scanline_offset(intel_crtc);

		dev_priv->display.crtc_enable(&intel_crtc->base);
	}

	/* FIXME: add subpixel order */
done:
	if (ret && crtc->state->enable)
		crtc->mode = *saved_mode;

	kfree(saved_mode);
	return ret;
}

static int intel_set_mode_pipes(struct drm_crtc *crtc,
				struct drm_display_mode *mode,
				int x, int y, struct drm_framebuffer *fb,
				struct intel_crtc_state *pipe_config,
				unsigned modeset_pipes,
				unsigned prepare_pipes,
				unsigned disable_pipes)
{
	int ret;

	ret = __intel_set_mode(crtc, mode, x, y, fb, pipe_config, modeset_pipes,
			       prepare_pipes, disable_pipes);

	if (ret == 0)
		intel_modeset_check_state(crtc->dev);

	return ret;
}

static int intel_set_mode(struct drm_crtc *crtc,
			  struct drm_display_mode *mode,
			  int x, int y, struct drm_framebuffer *fb)
{
	struct intel_crtc_state *pipe_config;
	unsigned modeset_pipes, prepare_pipes, disable_pipes;

	pipe_config = intel_modeset_compute_config(crtc, mode, fb,
						   &modeset_pipes,
						   &prepare_pipes,
						   &disable_pipes);

	if (IS_ERR(pipe_config))
		return PTR_ERR(pipe_config);

	return intel_set_mode_pipes(crtc, mode, x, y, fb, pipe_config,
				    modeset_pipes, prepare_pipes,
				    disable_pipes);
}

void intel_crtc_restore_mode(struct drm_crtc *crtc)
{
	intel_set_mode(crtc, &crtc->mode, crtc->x, crtc->y, crtc->primary->fb);
}

#undef for_each_intel_crtc_masked

static void intel_set_config_free(struct intel_set_config *config)
{
	if (!config)
		return;

	kfree(config->save_connector_encoders);
	kfree(config->save_encoder_crtcs);
	kfree(config->save_crtc_enabled);
	kfree(config);
}

static int intel_set_config_save_state(struct drm_device *dev,
				       struct intel_set_config *config)
{
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int count;

	config->save_crtc_enabled =
		kcalloc(dev->mode_config.num_crtc,
			sizeof(bool), GFP_KERNEL);
	if (!config->save_crtc_enabled)
		return -ENOMEM;

	config->save_encoder_crtcs =
		kcalloc(dev->mode_config.num_encoder,
			sizeof(struct drm_crtc *), GFP_KERNEL);
	if (!config->save_encoder_crtcs)
		return -ENOMEM;

	config->save_connector_encoders =
		kcalloc(dev->mode_config.num_connector,
			sizeof(struct drm_encoder *), GFP_KERNEL);
	if (!config->save_connector_encoders)
		return -ENOMEM;

	/* Copy data. Note that driver private data is not affected.
	 * Should anything bad happen only the expected state is
	 * restored, not the drivers personal bookkeeping.
	 */
	count = 0;
	for_each_crtc(dev, crtc) {
		config->save_crtc_enabled[count++] = crtc->state->enable;
	}

	count = 0;
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		config->save_encoder_crtcs[count++] = encoder->crtc;
	}

	count = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		config->save_connector_encoders[count++] = connector->encoder;
	}

	return 0;
}

static void intel_set_config_restore_state(struct drm_device *dev,
					   struct intel_set_config *config)
{
	struct intel_crtc *crtc;
	struct intel_encoder *encoder;
	struct intel_connector *connector;
	int count;

	count = 0;
	for_each_intel_crtc(dev, crtc) {
		crtc->new_enabled = config->save_crtc_enabled[count++];

		if (crtc->new_enabled)
			crtc->new_config = crtc->config;
		else
			crtc->new_config = NULL;
	}

	count = 0;
	for_each_intel_encoder(dev, encoder) {
		encoder->new_crtc =
			to_intel_crtc(config->save_encoder_crtcs[count++]);
	}

	count = 0;
	for_each_intel_connector(dev, connector) {
		connector->new_encoder =
			to_intel_encoder(config->save_connector_encoders[count++]);
	}
}

static bool
is_crtc_connector_off(struct drm_mode_set *set)
{
	int i;

	if (set->num_connectors == 0)
		return false;

	if (WARN_ON(set->connectors == NULL))
		return false;

	for (i = 0; i < set->num_connectors; i++)
		if (set->connectors[i]->encoder &&
		    set->connectors[i]->encoder->crtc == set->crtc &&
		    set->connectors[i]->dpms != DRM_MODE_DPMS_ON)
			return true;

	return false;
}

static void
intel_set_config_compute_mode_changes(struct drm_mode_set *set,
				      struct intel_set_config *config)
{

	/* We should be able to check here if the fb has the same properties
	 * and then just flip_or_move it */
	if (is_crtc_connector_off(set)) {
		config->mode_changed = true;
	} else if (set->crtc->primary->fb != set->fb) {
		/*
		 * If we have no fb, we can only flip as long as the crtc is
		 * active, otherwise we need a full mode set.  The crtc may
		 * be active if we've only disabled the primary plane, or
		 * in fastboot situations.
		 */
		if (set->crtc->primary->fb == NULL) {
			struct intel_crtc *intel_crtc =
				to_intel_crtc(set->crtc);

			if (intel_crtc->active) {
				DRM_DEBUG_KMS("crtc has no fb, will flip\n");
				config->fb_changed = true;
			} else {
				DRM_DEBUG_KMS("inactive crtc, full mode set\n");
				config->mode_changed = true;
			}
		} else if (set->fb == NULL) {
			config->mode_changed = true;
		} else if (set->fb->pixel_format !=
			   set->crtc->primary->fb->pixel_format) {
			config->mode_changed = true;
		} else {
			config->fb_changed = true;
		}
	}

	if (set->fb && (set->x != set->crtc->x || set->y != set->crtc->y))
		config->fb_changed = true;

	if (set->mode && !drm_mode_equal(set->mode, &set->crtc->mode)) {
		DRM_DEBUG_KMS("modes are different, full mode set\n");
		drm_mode_debug_printmodeline(&set->crtc->mode);
		drm_mode_debug_printmodeline(set->mode);
		config->mode_changed = true;
	}

	DRM_DEBUG_KMS("computed changes for [CRTC:%d], mode_changed=%d, fb_changed=%d\n",
			set->crtc->base.id, config->mode_changed, config->fb_changed);
}

static int
intel_modeset_stage_output_state(struct drm_device *dev,
				 struct drm_mode_set *set,
				 struct intel_set_config *config)
{
	struct intel_connector *connector;
	struct intel_encoder *encoder;
	struct intel_crtc *crtc;
	int ro;

	/* The upper layers ensure that we either disable a crtc or have a list
	 * of connectors. For paranoia, double-check this. */
	WARN_ON(!set->fb && (set->num_connectors != 0));
	WARN_ON(set->fb && (set->num_connectors == 0));

	for_each_intel_connector(dev, connector) {
		/* Otherwise traverse passed in connector list and get encoders
		 * for them. */
		for (ro = 0; ro < set->num_connectors; ro++) {
			if (set->connectors[ro] == &connector->base) {
				connector->new_encoder = intel_find_encoder(connector, to_intel_crtc(set->crtc)->pipe);
				break;
			}
		}

		/* If we disable the crtc, disable all its connectors. Also, if
		 * the connector is on the changing crtc but not on the new
		 * connector list, disable it. */
		if ((!set->fb || ro == set->num_connectors) &&
		    connector->base.encoder &&
		    connector->base.encoder->crtc == set->crtc) {
			connector->new_encoder = NULL;

			DRM_DEBUG_KMS("[CONNECTOR:%d:%s] to [NOCRTC]\n",
				connector->base.base.id,
				connector->base.name);
		}


		if (&connector->new_encoder->base != connector->base.encoder) {
			DRM_DEBUG_KMS("[CONNECTOR:%d:%s] encoder changed, full mode switch\n",
				      connector->base.base.id,
				      connector->base.name);
			config->mode_changed = true;
		}
	}
	/* connector->new_encoder is now updated for all connectors. */

	/* Update crtc of enabled connectors. */
	for_each_intel_connector(dev, connector) {
		struct drm_crtc *new_crtc;

		if (!connector->new_encoder)
			continue;

		new_crtc = connector->new_encoder->base.crtc;

		for (ro = 0; ro < set->num_connectors; ro++) {
			if (set->connectors[ro] == &connector->base)
				new_crtc = set->crtc;
		}

		/* Make sure the new CRTC will work with the encoder */
		if (!drm_encoder_crtc_ok(&connector->new_encoder->base,
					 new_crtc)) {
			return -EINVAL;
		}
		connector->new_encoder->new_crtc = to_intel_crtc(new_crtc);

		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] to [CRTC:%d]\n",
			connector->base.base.id,
			connector->base.name,
			new_crtc->base.id);
	}

	/* Check for any encoders that needs to be disabled. */
	for_each_intel_encoder(dev, encoder) {
		int num_connectors = 0;
		for_each_intel_connector(dev, connector) {
			if (connector->new_encoder == encoder) {
				WARN_ON(!connector->new_encoder->new_crtc);
				num_connectors++;
			}
		}

		if (num_connectors == 0)
			encoder->new_crtc = NULL;
		else if (num_connectors > 1)
			return -EINVAL;

		/* Only now check for crtc changes so we don't miss encoders
		 * that will be disabled. */
		if (&encoder->new_crtc->base != encoder->base.crtc) {
			DRM_DEBUG_KMS("[ENCODER:%d:%s] crtc changed, full mode switch\n",
				      encoder->base.base.id,
				      encoder->base.name);
			config->mode_changed = true;
		}
	}
	/* Now we've also updated encoder->new_crtc for all encoders. */
	for_each_intel_connector(dev, connector) {
		if (connector->new_encoder)
			if (connector->new_encoder != connector->encoder)
				connector->encoder = connector->new_encoder;
	}
	for_each_intel_crtc(dev, crtc) {
		crtc->new_enabled = false;

		for_each_intel_encoder(dev, encoder) {
			if (encoder->new_crtc == crtc) {
				crtc->new_enabled = true;
				break;
			}
		}

		if (crtc->new_enabled != crtc->base.state->enable) {
			DRM_DEBUG_KMS("[CRTC:%d] %sabled, full mode switch\n",
				      crtc->base.base.id,
				      crtc->new_enabled ? "en" : "dis");
			config->mode_changed = true;
		}

		if (crtc->new_enabled)
			crtc->new_config = crtc->config;
		else
			crtc->new_config = NULL;
	}

	return 0;
}

static void disable_crtc_nofb(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_encoder *encoder;
	struct intel_connector *connector;

	DRM_DEBUG_KMS("Trying to restore without FB -> disabling pipe %c\n",
		      pipe_name(crtc->pipe));

	for_each_intel_connector(dev, connector) {
		if (connector->new_encoder &&
		    connector->new_encoder->new_crtc == crtc)
			connector->new_encoder = NULL;
	}

	for_each_intel_encoder(dev, encoder) {
		if (encoder->new_crtc == crtc)
			encoder->new_crtc = NULL;
	}

	crtc->new_enabled = false;
	crtc->new_config = NULL;
}

static int intel_crtc_set_config(struct drm_mode_set *set)
{
	struct drm_device *dev;
	struct drm_mode_set save_set;
	struct intel_set_config *config;
	struct intel_crtc_state *pipe_config;
	unsigned modeset_pipes, prepare_pipes, disable_pipes;
	int ret;

	BUG_ON(!set);
	BUG_ON(!set->crtc);
	BUG_ON(!set->crtc->helper_private);

	/* Enforce sane interface api - has been abused by the fb helper. */
	BUG_ON(!set->mode && set->fb);
	BUG_ON(set->fb && set->num_connectors == 0);

	if (set->fb) {
		DRM_DEBUG_KMS("[CRTC:%d] [FB:%d] #connectors=%d (x y) (%i %i)\n",
				set->crtc->base.id, set->fb->base.id,
				(int)set->num_connectors, set->x, set->y);
	} else {
		DRM_DEBUG_KMS("[CRTC:%d] [NOFB]\n", set->crtc->base.id);
	}

	dev = set->crtc->dev;

	ret = -ENOMEM;
	config = kzalloc(sizeof(*config), GFP_KERNEL);
	if (!config)
		goto out_config;

	ret = intel_set_config_save_state(dev, config);
	if (ret)
		goto out_config;

	save_set.crtc = set->crtc;
	save_set.mode = &set->crtc->mode;
	save_set.x = set->crtc->x;
	save_set.y = set->crtc->y;
	save_set.fb = set->crtc->primary->fb;

	/* Compute whether we need a full modeset, only an fb base update or no
	 * change at all. In the future we might also check whether only the
	 * mode changed, e.g. for LVDS where we only change the panel fitter in
	 * such cases. */
	intel_set_config_compute_mode_changes(set, config);

	ret = intel_modeset_stage_output_state(dev, set, config);
	if (ret)
		goto fail;

	pipe_config = intel_modeset_compute_config(set->crtc, set->mode,
						   set->fb,
						   &modeset_pipes,
						   &prepare_pipes,
						   &disable_pipes);
	if (IS_ERR(pipe_config)) {
		ret = PTR_ERR(pipe_config);
		goto fail;
	} else if (pipe_config) {
		if (pipe_config->has_audio !=
		    to_intel_crtc(set->crtc)->config->has_audio)
			config->mode_changed = true;

		/*
		 * Note we have an issue here with infoframes: current code
		 * only updates them on the full mode set path per hw
		 * requirements.  So here we should be checking for any
		 * required changes and forcing a mode set.
		 */
	}

	/* set_mode will free it in the mode_changed case */
	if (!config->mode_changed)
		kfree(pipe_config);

	intel_update_pipe_size(to_intel_crtc(set->crtc));

	if (config->mode_changed) {
		ret = intel_set_mode_pipes(set->crtc, set->mode,
					   set->x, set->y, set->fb, pipe_config,
					   modeset_pipes, prepare_pipes,
					   disable_pipes);
	} else if (config->fb_changed) {
		struct intel_crtc *intel_crtc = to_intel_crtc(set->crtc);
		struct drm_plane *primary = set->crtc->primary;
		int vdisplay, hdisplay;

		drm_crtc_get_hv_timing(set->mode, &hdisplay, &vdisplay);
		ret = primary->funcs->update_plane(primary, set->crtc, set->fb,
						   0, 0, hdisplay, vdisplay,
						   set->x << 16, set->y << 16,
						   hdisplay << 16, vdisplay << 16);

		/*
		 * We need to make sure the primary plane is re-enabled if it
		 * has previously been turned off.
		 */
		if (!intel_crtc->primary_enabled && ret == 0) {
			WARN_ON(!intel_crtc->active);
			intel_enable_primary_hw_plane(set->crtc->primary, set->crtc);
		}

		/*
		 * In the fastboot case this may be our only check of the
		 * state after boot.  It would be better to only do it on
		 * the first update, but we don't have a nice way of doing that
		 * (and really, set_config isn't used much for high freq page
		 * flipping, so increasing its cost here shouldn't be a big
		 * deal).
		 */
		if (i915.fastboot && ret == 0)
			intel_modeset_check_state(set->crtc->dev);
	}

	if (ret) {
		DRM_DEBUG_KMS("failed to set mode on [CRTC:%d], err = %d\n",
			      set->crtc->base.id, ret);
fail:
		intel_set_config_restore_state(dev, config);

		/*
		 * HACK: if the pipe was on, but we didn't have a framebuffer,
		 * force the pipe off to avoid oopsing in the modeset code
		 * due to fb==NULL. This should only happen during boot since
		 * we don't yet reconstruct the FB from the hardware state.
		 */
		if (to_intel_crtc(save_set.crtc)->new_enabled && !save_set.fb)
			disable_crtc_nofb(to_intel_crtc(save_set.crtc));

		/* Try to restore the config */
		if (config->mode_changed &&
		    intel_set_mode(save_set.crtc, save_set.mode,
				   save_set.x, save_set.y, save_set.fb))
			DRM_ERROR("failed to restore config after modeset failure\n");
	}

out_config:
	intel_set_config_free(config);
	return ret;
}

static const struct drm_crtc_funcs intel_crtc_funcs = {
	.gamma_set = intel_crtc_gamma_set,
	.set_config = intel_crtc_set_config,
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
 * Returns 0 on success, negative error code on failure.
 */
int
intel_prepare_plane_fb(struct drm_plane *plane,
		       struct drm_framebuffer *fb,
		       const struct drm_plane_state *new_state)
{
	struct drm_device *dev = plane->dev;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	enum pipe pipe = intel_plane->pipe;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct drm_i915_gem_object *old_obj = intel_fb_obj(plane->fb);
	unsigned frontbuffer_bits = 0;
	int ret = 0;

	if (!obj)
		return 0;

	switch (plane->type) {
	case DRM_PLANE_TYPE_PRIMARY:
		frontbuffer_bits = INTEL_FRONTBUFFER_PRIMARY(pipe);
		break;
	case DRM_PLANE_TYPE_CURSOR:
		frontbuffer_bits = INTEL_FRONTBUFFER_CURSOR(pipe);
		break;
	case DRM_PLANE_TYPE_OVERLAY:
		frontbuffer_bits = INTEL_FRONTBUFFER_SPRITE(pipe);
		break;
	}

	mutex_lock(&dev->struct_mutex);

	if (plane->type == DRM_PLANE_TYPE_CURSOR &&
	    INTEL_INFO(dev)->cursor_needs_physical) {
		int align = IS_I830(dev) ? 16 * 1024 : 256;
		ret = i915_gem_object_attach_phys(obj, align);
		if (ret)
			DRM_DEBUG_KMS("failed to attach phys object\n");
	} else {
		ret = intel_pin_and_fence_fb_obj(plane, fb, NULL);
	}

	if (ret == 0)
		i915_gem_track_fb(old_obj, obj, frontbuffer_bits);

	mutex_unlock(&dev->struct_mutex);

	return ret;
}

/**
 * intel_cleanup_plane_fb - Cleans up an fb after plane use
 * @plane: drm plane to clean up for
 * @fb: old framebuffer that was on plane
 *
 * Cleans up a framebuffer that has just been removed from a plane.
 */
void
intel_cleanup_plane_fb(struct drm_plane *plane,
		       struct drm_framebuffer *fb,
		       const struct drm_plane_state *old_state)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);

	if (WARN_ON(!obj))
		return;

	if (plane->type != DRM_PLANE_TYPE_CURSOR ||
	    !INTEL_INFO(dev)->cursor_needs_physical) {
		mutex_lock(&dev->struct_mutex);
		intel_unpin_fb_obj(obj);
		mutex_unlock(&dev->struct_mutex);
	}
}

static int
intel_check_primary_plane(struct drm_plane *plane,
			  struct intel_plane_state *state)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = state->base.crtc;
	struct intel_crtc *intel_crtc;
	struct drm_framebuffer *fb = state->base.fb;
	struct drm_rect *dest = &state->dst;
	struct drm_rect *src = &state->src;
	const struct drm_rect *clip = &state->clip;
	int ret;

	crtc = crtc ? crtc : plane->crtc;
	intel_crtc = to_intel_crtc(crtc);

	ret = drm_plane_helper_check_update(plane, crtc, fb,
					    src, dest, clip,
					    DRM_PLANE_HELPER_NO_SCALING,
					    DRM_PLANE_HELPER_NO_SCALING,
					    false, true, &state->visible);
	if (ret)
		return ret;

	if (intel_crtc->active) {
		intel_crtc->atomic.wait_for_flips = true;

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
		if (intel_crtc->primary_enabled &&
		    INTEL_INFO(dev)->gen <= 4 && !IS_G4X(dev) &&
		    dev_priv->fbc.crtc == intel_crtc &&
		    state->base.rotation != BIT(DRM_ROTATE_0)) {
			intel_crtc->atomic.disable_fbc = true;
		}

		if (state->visible) {
			/*
			 * BDW signals flip done immediately if the plane
			 * is disabled, even if the plane enable is already
			 * armed to occur at the next vblank :(
			 */
			if (IS_BROADWELL(dev) && !intel_crtc->primary_enabled)
				intel_crtc->atomic.wait_vblank = true;
		}

		intel_crtc->atomic.fb_bits |=
			INTEL_FRONTBUFFER_PRIMARY(intel_crtc->pipe);

		intel_crtc->atomic.update_fbc = true;

		/* Update watermarks on tiling changes. */
		if (!plane->state->fb || !state->base.fb ||
		    plane->state->fb->modifier[0] !=
		    state->base.fb->modifier[0])
			intel_crtc->atomic.update_wm = true;
	}

	return 0;
}

static void
intel_commit_primary_plane(struct drm_plane *plane,
			   struct intel_plane_state *state)
{
	struct drm_crtc *crtc = state->base.crtc;
	struct drm_framebuffer *fb = state->base.fb;
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct drm_rect *src = &state->src;

	crtc = crtc ? crtc : plane->crtc;
	intel_crtc = to_intel_crtc(crtc);

	plane->fb = fb;
	crtc->x = src->x1 >> 16;
	crtc->y = src->y1 >> 16;

	intel_plane->obj = obj;

	if (intel_crtc->active) {
		if (state->visible) {
			/* FIXME: kill this fastboot hack */
			intel_update_pipe_size(intel_crtc);

			intel_crtc->primary_enabled = true;

			dev_priv->display.update_primary_plane(crtc, plane->fb,
					crtc->x, crtc->y);
		} else {
			/*
			 * If clipping results in a non-visible primary plane,
			 * we'll disable the primary plane.  Note that this is
			 * a bit different than what happens if userspace
			 * explicitly disables the plane by passing fb=0
			 * because plane->fb still gets set and pinned.
			 */
			intel_disable_primary_hw_plane(plane, crtc);
		}
	}
}

static void intel_begin_crtc_commit(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_plane *intel_plane;
	struct drm_plane *p;
	unsigned fb_bits = 0;

	/* Track fb's for any planes being disabled */
	list_for_each_entry(p, &dev->mode_config.plane_list, head) {
		intel_plane = to_intel_plane(p);

		if (intel_crtc->atomic.disabled_planes &
		    (1 << drm_plane_index(p))) {
			switch (p->type) {
			case DRM_PLANE_TYPE_PRIMARY:
				fb_bits = INTEL_FRONTBUFFER_PRIMARY(intel_plane->pipe);
				break;
			case DRM_PLANE_TYPE_CURSOR:
				fb_bits = INTEL_FRONTBUFFER_CURSOR(intel_plane->pipe);
				break;
			case DRM_PLANE_TYPE_OVERLAY:
				fb_bits = INTEL_FRONTBUFFER_SPRITE(intel_plane->pipe);
				break;
			}

			mutex_lock(&dev->struct_mutex);
			i915_gem_track_fb(intel_fb_obj(p->fb), NULL, fb_bits);
			mutex_unlock(&dev->struct_mutex);
		}
	}

	if (intel_crtc->atomic.wait_for_flips)
		intel_crtc_wait_for_pending_flips(crtc);

	if (intel_crtc->atomic.disable_fbc)
		intel_fbc_disable(dev);

	if (intel_crtc->atomic.pre_disable_primary)
		intel_pre_disable_primary(crtc);

	if (intel_crtc->atomic.update_wm)
		intel_update_watermarks(crtc);

	intel_runtime_pm_get(dev_priv);

	/* Perform vblank evasion around commit operation */
	if (intel_crtc->active)
		intel_crtc->atomic.evade =
			intel_pipe_update_start(intel_crtc,
						&intel_crtc->atomic.start_vbl_count);
}

static void intel_finish_crtc_commit(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_plane *p;

	if (intel_crtc->atomic.evade)
		intel_pipe_update_end(intel_crtc,
				      intel_crtc->atomic.start_vbl_count);

	intel_runtime_pm_put(dev_priv);

	if (intel_crtc->atomic.wait_vblank)
		intel_wait_for_vblank(dev, intel_crtc->pipe);

	intel_frontbuffer_flip(dev, intel_crtc->atomic.fb_bits);

	if (intel_crtc->atomic.update_fbc) {
		mutex_lock(&dev->struct_mutex);
		intel_fbc_update(dev);
		mutex_unlock(&dev->struct_mutex);
	}

	if (intel_crtc->atomic.post_enable_primary)
		intel_post_enable_primary(crtc);

	drm_for_each_legacy_plane(p, &dev->mode_config.plane_list)
		if (intel_crtc->atomic.update_sprite_watermarks & drm_plane_index(p))
			intel_update_sprite_watermarks(p, crtc, 0, 0, 0,
						       false, false);

	memset(&intel_crtc->atomic, 0, sizeof(intel_crtc->atomic));
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
	.update_plane = drm_plane_helper_update,
	.disable_plane = drm_plane_helper_disable,
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
	int num_formats;

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
	primary->pipe = pipe;
	primary->plane = pipe;
	primary->check_plane = intel_check_primary_plane;
	primary->commit_plane = intel_commit_primary_plane;
	if (HAS_FBC(dev) && INTEL_INFO(dev)->gen < 4)
		primary->plane = !pipe;

	if (INTEL_INFO(dev)->gen <= 3) {
		intel_primary_formats = intel_primary_formats_gen2;
		num_formats = ARRAY_SIZE(intel_primary_formats_gen2);
	} else {
		intel_primary_formats = intel_primary_formats_gen4;
		num_formats = ARRAY_SIZE(intel_primary_formats_gen4);
	}

	drm_universal_plane_init(dev, &primary->base, 0,
				 &intel_plane_funcs,
				 intel_primary_formats, num_formats,
				 DRM_PLANE_TYPE_PRIMARY);

	if (INTEL_INFO(dev)->gen >= 4) {
		if (!dev->mode_config.rotation_property)
			dev->mode_config.rotation_property =
				drm_mode_create_rotation_property(dev,
							BIT(DRM_ROTATE_0) |
							BIT(DRM_ROTATE_180));
		if (dev->mode_config.rotation_property)
			drm_object_attach_property(&primary->base.base,
				dev->mode_config.rotation_property,
				state->base.rotation);
	}

	drm_plane_helper_add(&primary->base, &intel_plane_helper_funcs);

	return &primary->base;
}

static int
intel_check_cursor_plane(struct drm_plane *plane,
			 struct intel_plane_state *state)
{
	struct drm_crtc *crtc = state->base.crtc;
	struct drm_device *dev = plane->dev;
	struct drm_framebuffer *fb = state->base.fb;
	struct drm_rect *dest = &state->dst;
	struct drm_rect *src = &state->src;
	const struct drm_rect *clip = &state->clip;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct intel_crtc *intel_crtc;
	unsigned stride;
	int ret;

	crtc = crtc ? crtc : plane->crtc;
	intel_crtc = to_intel_crtc(crtc);

	ret = drm_plane_helper_check_update(plane, crtc, fb,
					    src, dest, clip,
					    DRM_PLANE_HELPER_NO_SCALING,
					    DRM_PLANE_HELPER_NO_SCALING,
					    true, true, &state->visible);
	if (ret)
		return ret;


	/* if we want to turn off the cursor ignore width and height */
	if (!obj)
		goto finish;

	/* Check for which cursor types we support */
	if (!cursor_size_ok(dev, state->base.crtc_w, state->base.crtc_h)) {
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
		ret = -EINVAL;
	}

finish:
	if (intel_crtc->active) {
		if (intel_crtc->base.cursor->state->crtc_w != state->base.crtc_w)
			intel_crtc->atomic.update_wm = true;

		intel_crtc->atomic.fb_bits |=
			INTEL_FRONTBUFFER_CURSOR(intel_crtc->pipe);
	}

	return ret;
}

static void
intel_commit_cursor_plane(struct drm_plane *plane,
			  struct intel_plane_state *state)
{
	struct drm_crtc *crtc = state->base.crtc;
	struct drm_device *dev = plane->dev;
	struct intel_crtc *intel_crtc;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct drm_i915_gem_object *obj = intel_fb_obj(state->base.fb);
	uint32_t addr;

	crtc = crtc ? crtc : plane->crtc;
	intel_crtc = to_intel_crtc(crtc);

	plane->fb = state->base.fb;
	crtc->cursor_x = state->base.crtc_x;
	crtc->cursor_y = state->base.crtc_y;

	intel_plane->obj = obj;

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

	if (intel_crtc->active)
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
	cursor->check_plane = intel_check_cursor_plane;
	cursor->commit_plane = intel_commit_cursor_plane;

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

	drm_plane_helper_add(&cursor->base, &intel_plane_helper_funcs);

	return &cursor->base;
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
	intel_crtc_set_state(intel_crtc, crtc_state);
	crtc_state->base.crtc = &intel_crtc->base;

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

	BUG_ON(pipe >= ARRAY_SIZE(dev_priv->plane_to_crtc_mapping) ||
	       dev_priv->plane_to_crtc_mapping[intel_crtc->plane] != NULL);
	dev_priv->plane_to_crtc_mapping[intel_crtc->plane] = &intel_crtc->base;
	dev_priv->pipe_to_crtc_mapping[intel_crtc->pipe] = &intel_crtc->base;

	INIT_WORK(&intel_crtc->mmio_flip.work, intel_mmio_flip_work_func);

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

	if (IS_VALLEYVIEW(dev) && !dev_priv->vbt.int_crt_support)
		return false;

	return true;
}

static void intel_setup_outputs(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *encoder;
	struct drm_connector *connector;
	bool dpd_is_edp = false;

	intel_lvds_init(dev);

	if (intel_crt_present(dev))
		intel_crt_init(dev);

	if (HAS_DDI(dev)) {
		int found;

		/* Haswell uses DDI functions to detect digital outputs */
		found = I915_READ(DDI_BUF_CTL_A) & DDI_INIT_DISPLAY_DETECTED;
		/* DDI A only supports eDP */
		if (found)
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
	} else if (HAS_PCH_SPLIT(dev)) {
		int found;
		dpd_is_edp = intel_dp_is_edp(dev, PORT_D);

		if (has_edp_a(dev))
			intel_dp_init(dev, DP_A, PORT_A);

		if (I915_READ(PCH_HDMIB) & SDVO_DETECTED) {
			/* PCH SDVOB multiplex with HDMIB */
			found = intel_sdvo_init(dev, PCH_SDVOB, true);
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
		if (I915_READ(VLV_DISPLAY_BASE + GEN4_HDMIB) & SDVO_DETECTED &&
		    !intel_dp_is_edp(dev, PORT_B))
			intel_hdmi_init(dev, VLV_DISPLAY_BASE + GEN4_HDMIB,
					PORT_B);
		if (I915_READ(VLV_DISPLAY_BASE + DP_B) & DP_DETECTED ||
		    intel_dp_is_edp(dev, PORT_B))
			intel_dp_init(dev, VLV_DISPLAY_BASE + DP_B, PORT_B);

		if (I915_READ(VLV_DISPLAY_BASE + GEN4_HDMIC) & SDVO_DETECTED &&
		    !intel_dp_is_edp(dev, PORT_C))
			intel_hdmi_init(dev, VLV_DISPLAY_BASE + GEN4_HDMIC,
					PORT_C);
		if (I915_READ(VLV_DISPLAY_BASE + DP_C) & DP_DETECTED ||
		    intel_dp_is_edp(dev, PORT_C))
			intel_dp_init(dev, VLV_DISPLAY_BASE + DP_C, PORT_C);

		if (IS_CHERRYVIEW(dev)) {
			if (I915_READ(VLV_DISPLAY_BASE + CHV_HDMID) & SDVO_DETECTED)
				intel_hdmi_init(dev, VLV_DISPLAY_BASE + CHV_HDMID,
						PORT_D);
			/* eDP not supported on port D, so don't check VBT */
			if (I915_READ(VLV_DISPLAY_BASE + DP_D) & DP_DETECTED)
				intel_dp_init(dev, VLV_DISPLAY_BASE + DP_D, PORT_D);
		}

		intel_dsi_init(dev);
	} else if (SUPPORTS_DIGITAL_OUTPUTS(dev)) {
		bool found = false;

		if (I915_READ(GEN3_SDVOB) & SDVO_DETECTED) {
			DRM_DEBUG_KMS("probing SDVOB\n");
			found = intel_sdvo_init(dev, GEN3_SDVOB, true);
			if (!found && SUPPORTS_INTEGRATED_HDMI(dev)) {
				DRM_DEBUG_KMS("probing HDMI on SDVOB\n");
				intel_hdmi_init(dev, GEN4_HDMIB, PORT_B);
			}

			if (!found && SUPPORTS_INTEGRATED_DP(dev))
				intel_dp_init(dev, DP_B, PORT_B);
		}

		/* Before G4X SDVOC doesn't have its own detect register */

		if (I915_READ(GEN3_SDVOB) & SDVO_DETECTED) {
			DRM_DEBUG_KMS("probing SDVOC\n");
			found = intel_sdvo_init(dev, GEN3_SDVOC, false);
		}

		if (!found && (I915_READ(GEN3_SDVOC) & SDVO_DETECTED)) {

			if (SUPPORTS_INTEGRATED_HDMI(dev)) {
				DRM_DEBUG_KMS("probing HDMI on SDVOC\n");
				intel_hdmi_init(dev, GEN4_HDMIC, PORT_C);
			}
			if (SUPPORTS_INTEGRATED_DP(dev))
				intel_dp_init(dev, DP_C, PORT_C);
		}

		if (SUPPORTS_INTEGRATED_DP(dev) &&
		    (I915_READ(DP_D) & DP_DETECTED))
			intel_dp_init(dev, DP_D, PORT_D);
	} else if (IS_GEN2(dev))
		intel_dvo_init(dev);

	if (SUPPORTS_TV(dev))
		intel_tv_init(dev);

	/*
	 * FIXME:  We don't have full atomic support yet, but we want to be
	 * able to enable/test plane updates via the atomic interface in the
	 * meantime.  However as soon as we flip DRIVER_ATOMIC on, the DRM core
	 * will take some atomic codepaths to lookup properties during
	 * drmModeGetConnector() that unconditionally dereference
	 * connector->state.
	 *
	 * We create a dummy connector state here for each connector to ensure
	 * the DRM core doesn't try to dereference a NULL connector->state.
	 * The actual connector properties will never be updated or contain
	 * useful information, but since we're doing this specifically for
	 * testing/debug of the plane operations (and only when a specific
	 * kernel module option is given), that shouldn't really matter.
	 *
	 * Once atomic support for crtc's + connectors lands, this loop should
	 * be removed since we'll be setting up real connector state, which
	 * will contain Intel-specific properties.
	 */
	if (drm_core_check_feature(dev, DRIVER_ATOMIC)) {
		list_for_each_entry(connector,
				    &dev->mode_config.connector_list,
				    head) {
			if (!WARN_ON(connector->state)) {
				connector->state =
					kzalloc(sizeof(*connector->state),
						GFP_KERNEL);
			}
		}
	}

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

	return drm_gem_handle_create(file, &obj->base, handle);
}

static const struct drm_framebuffer_funcs intel_fb_funcs = {
	.destroy = intel_user_framebuffer_destroy,
	.create_handle = intel_user_framebuffer_create_handle,
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
	int aligned_height;
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
		DRM_ERROR("Unsupported fb modifier 0x%llx!\n",
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
	case DRM_FORMAT_ARGB1555:
		if (INTEL_INFO(dev)->gen > 3) {
			DRM_DEBUG("unsupported pixel format: %s\n",
				  drm_get_format_name(mode_cmd->pixel_format));
			return -EINVAL;
		}
		break;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
		if (INTEL_INFO(dev)->gen < 4) {
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
			      struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_i915_gem_object *obj;

	obj = to_intel_bo(drm_gem_object_lookup(dev, filp,
						mode_cmd->handles[0]));
	if (&obj->base == NULL)
		return ERR_PTR(-ENOENT);

	return intel_framebuffer_create(dev, mode_cmd, obj);
}

#ifndef CONFIG_DRM_I915_FBDEV
static inline void intel_fbdev_output_poll_changed(struct drm_device *dev)
{
}
#endif

static const struct drm_mode_config_funcs intel_mode_funcs = {
	.fb_create = intel_user_framebuffer_create,
	.output_poll_changed = intel_fbdev_output_poll_changed,
	.atomic_check = intel_atomic_check,
	.atomic_commit = intel_atomic_commit,
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
		dev_priv->display.off = ironlake_crtc_off;
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
		dev_priv->display.off = ironlake_crtc_off;
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
		dev_priv->display.off = ironlake_crtc_off;
		dev_priv->display.update_primary_plane =
			ironlake_update_primary_plane;
	} else if (IS_VALLEYVIEW(dev)) {
		dev_priv->display.get_pipe_config = i9xx_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			i9xx_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock = i9xx_crtc_compute_clock;
		dev_priv->display.crtc_enable = valleyview_crtc_enable;
		dev_priv->display.crtc_disable = i9xx_crtc_disable;
		dev_priv->display.off = i9xx_crtc_off;
		dev_priv->display.update_primary_plane =
			i9xx_update_primary_plane;
	} else {
		dev_priv->display.get_pipe_config = i9xx_get_pipe_config;
		dev_priv->display.get_initial_plane_config =
			i9xx_get_initial_plane_config;
		dev_priv->display.crtc_compute_clock = i9xx_crtc_compute_clock;
		dev_priv->display.crtc_enable = i9xx_crtc_enable;
		dev_priv->display.crtc_disable = i9xx_crtc_disable;
		dev_priv->display.off = i9xx_crtc_off;
		dev_priv->display.update_primary_plane =
			i9xx_update_primary_plane;
	}

	/* Returns the core display clock speed */
	if (IS_VALLEYVIEW(dev))
		dev_priv->display.get_display_clock_speed =
			valleyview_get_display_clock_speed;
	else if (IS_I945G(dev) || (IS_G33(dev) && !IS_PINEVIEW_M(dev)))
		dev_priv->display.get_display_clock_speed =
			i945_get_display_clock_speed;
	else if (IS_I915G(dev))
		dev_priv->display.get_display_clock_speed =
			i915_get_display_clock_speed;
	else if (IS_I945GM(dev) || IS_845G(dev))
		dev_priv->display.get_display_clock_speed =
			i9xx_misc_get_display_clock_speed;
	else if (IS_PINEVIEW(dev))
		dev_priv->display.get_display_clock_speed =
			pnv_get_display_clock_speed;
	else if (IS_I915GM(dev))
		dev_priv->display.get_display_clock_speed =
			i915gm_get_display_clock_speed;
	else if (IS_I865G(dev))
		dev_priv->display.get_display_clock_speed =
			i865_get_display_clock_speed;
	else if (IS_I85X(dev))
		dev_priv->display.get_display_clock_speed =
			i855_get_display_clock_speed;
	else /* 852, 830 */
		dev_priv->display.get_display_clock_speed =
			i830_get_display_clock_speed;

	if (IS_GEN5(dev)) {
		dev_priv->display.fdi_link_train = ironlake_fdi_link_train;
	} else if (IS_GEN6(dev)) {
		dev_priv->display.fdi_link_train = gen6_fdi_link_train;
	} else if (IS_IVYBRIDGE(dev)) {
		/* FIXME: detect B0+ stepping and use auto training */
		dev_priv->display.fdi_link_train = ivb_manual_fdi_link_train;
		dev_priv->display.modeset_global_resources =
			ivb_modeset_global_resources;
	} else if (IS_HASWELL(dev) || IS_BROADWELL(dev)) {
		dev_priv->display.fdi_link_train = hsw_fdi_link_train;
	} else if (IS_VALLEYVIEW(dev)) {
		dev_priv->display.modeset_global_resources =
			valleyview_modeset_global_resources;
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

	intel_panel_init_backlight_funcs(dev);

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
	/* HP Mini needs pipe A force quirk (LP: #322104) */
	{ 0x27ae, 0x103c, 0x361a, quirk_pipea_force },

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

	/* Toshiba CB35 Chromebook (Celeron 2955U) */
	{ 0x0a06, 0x1179, 0x0a88, quirk_backlight_present },

	/* HP Chromebook 14 (Celeron 2955U) */
	{ 0x0a06, 0x103c, 0x21ed, quirk_backlight_present },

	/* Dell Chromebook 11 */
	{ 0x0a06, 0x1028, 0x0a35, quirk_backlight_present },
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
	u32 vga_reg = i915_vgacntrl_reg(dev);

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
	intel_prepare_ddi(dev);

	if (IS_VALLEYVIEW(dev))
		vlv_update_cdclk(dev);

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

	intel_init_dpio(dev);

	intel_shared_dpll_init(dev);

	/* Just disable it once at startup */
	i915_disable_vga(dev);
	intel_setup_outputs(dev);

	/* Just in case the BIOS is doing something questionable. */
	intel_fbc_disable(dev);

	drm_modeset_lock_all(dev);
	intel_modeset_setup_hw_state(dev, false);
	drm_modeset_unlock_all(dev);

	for_each_intel_crtc(dev, crtc) {
		if (!crtc->active)
			continue;

		/*
		 * Note that reserving the BIOS fb up front prevents us
		 * from stuffing other stolen allocations like the ring
		 * on top.  This prevents some ugliness at boot time, and
		 * can even allow for smooth boot transitions if the BIOS
		 * fb is large enough for the active pipe configuration.
		 */
		if (dev_priv->display.get_initial_plane_config) {
			dev_priv->display.get_initial_plane_config(crtc,
							   &crtc->plane_config);
			/*
			 * If the fb is shared between multiple heads, we'll
			 * just get the first one.
			 */
			intel_find_plane_obj(crtc, &crtc->plane_config);
		}
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
		intel_release_load_detect_pipe(crt, &load_detect_temp);
}

static bool
intel_check_plane_mapping(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg, val;

	if (INTEL_INFO(dev)->num_pipes == 1)
		return true;

	reg = DSPCNTR(!crtc->plane);
	val = I915_READ(reg);

	if ((val & DISPLAY_PLANE_ENABLE) &&
	    (!!(val & DISPPLANE_SEL_PIPE_MASK) == crtc->pipe))
		return false;

	return true;
}

static void intel_sanitize_crtc(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg;

	/* Clear any frame start delays used for debugging left by the BIOS */
	reg = PIPECONF(crtc->config->cpu_transcoder);
	I915_WRITE(reg, I915_READ(reg) & ~PIPECONF_FRAME_START_DELAY_MASK);

	/* restore vblank interrupts to correct state */
	drm_crtc_vblank_reset(&crtc->base);
	if (crtc->active) {
		update_scanline_offset(crtc);
		drm_crtc_vblank_on(&crtc->base);
	}

	/* We need to sanitize the plane -> pipe mapping first because this will
	 * disable the crtc (and hence change the state) if it is wrong. Note
	 * that gen4+ has a fixed plane -> pipe mapping.  */
	if (INTEL_INFO(dev)->gen < 4 && !intel_check_plane_mapping(crtc)) {
		struct intel_connector *connector;
		bool plane;

		DRM_DEBUG_KMS("[CRTC:%d] wrong plane connection detected!\n",
			      crtc->base.base.id);

		/* Pipe has the wrong plane attached and the plane is active.
		 * Temporarily change the plane mapping and disable everything
		 * ...  */
		plane = crtc->plane;
		crtc->plane = !plane;
		crtc->primary_enabled = true;
		dev_priv->display.crtc_disable(&crtc->base);
		crtc->plane = plane;

		/* ... and break all links. */
		for_each_intel_connector(dev, connector) {
			if (connector->encoder->base.crtc != &crtc->base)
				continue;

			connector->base.dpms = DRM_MODE_DPMS_OFF;
			connector->base.encoder = NULL;
		}
		/* multiple connectors may have the same encoder:
		 *  handle them and break crtc link separately */
		for_each_intel_connector(dev, connector)
			if (connector->encoder->base.crtc == &crtc->base) {
				connector->encoder->base.crtc = NULL;
				connector->encoder->connectors_active = false;
			}

		WARN_ON(crtc->active);
		crtc->base.state->enable = false;
		crtc->base.enabled = false;
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
	intel_crtc_update_dpms(&crtc->base);

	if (crtc->active != crtc->base.state->enable) {
		struct intel_encoder *encoder;

		/* This can happen either due to bugs in the get_hw_state
		 * functions or because the pipe is force-enabled due to the
		 * pipe A quirk. */
		DRM_DEBUG_KMS("[CRTC:%d] hw state adjusted, was %s, now %s\n",
			      crtc->base.base.id,
			      crtc->base.state->enable ? "enabled" : "disabled",
			      crtc->active ? "enabled" : "disabled");

		crtc->base.state->enable = crtc->active;
		crtc->base.enabled = crtc->active;

		/* Because we only establish the connector -> encoder ->
		 * crtc links if something is active, this means the
		 * crtc is now deactivated. Break the links. connector
		 * -> encoder links are only establish when things are
		 *  actually up, hence no need to break them. */
		WARN_ON(crtc->active);

		for_each_encoder_on_crtc(dev, &crtc->base, encoder) {
			WARN_ON(encoder->connectors_active);
			encoder->base.crtc = NULL;
		}
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

	/* We need to check both for a crtc link (meaning that the
	 * encoder is active and trying to read from a pipe) and the
	 * pipe itself being active. */
	bool has_active_crtc = encoder->base.crtc &&
		to_intel_crtc(encoder->base.crtc)->active;

	if (encoder->connectors_active && !has_active_crtc) {
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
		encoder->connectors_active = false;

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
	u32 vga_reg = i915_vgacntrl_reg(dev);

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

static bool primary_get_hw_state(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = crtc->base.dev->dev_private;

	if (!crtc->active)
		return false;

	return I915_READ(DSPCNTR(crtc->plane)) & DISPLAY_PLANE_ENABLE;
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
		memset(crtc->config, 0, sizeof(*crtc->config));

		crtc->config->quirks |= PIPE_CONFIG_QUIRK_INHERITED_MODE;

		crtc->active = dev_priv->display.get_pipe_config(crtc,
								 crtc->config);

		crtc->base.state->enable = crtc->active;
		crtc->base.enabled = crtc->active;
		crtc->primary_enabled = primary_get_hw_state(crtc);

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

		encoder->connectors_active = false;
		DRM_DEBUG_KMS("[ENCODER:%d:%s] hw state readout: %s, pipe %c\n",
			      encoder->base.base.id,
			      encoder->base.name,
			      encoder->base.crtc ? "enabled" : "disabled",
			      pipe_name(pipe));
	}

	for_each_intel_connector(dev, connector) {
		if (connector->get_hw_state(connector)) {
			connector->base.dpms = DRM_MODE_DPMS_ON;
			connector->encoder->connectors_active = true;
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
}

/* Scan out the current hw modeset state, sanitizes it and maps it into the drm
 * and i915 state tracking structures. */
void intel_modeset_setup_hw_state(struct drm_device *dev,
				  bool force_restore)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe;
	struct intel_crtc *crtc;
	struct intel_encoder *encoder;
	int i;

	intel_modeset_readout_hw_state(dev);

	/*
	 * Now that we have the config, copy it to each CRTC struct
	 * Note that this could go away if we move to using crtc_config
	 * checking everywhere.
	 */
	for_each_intel_crtc(dev, crtc) {
		if (crtc->active && i915.fastboot) {
			intel_mode_from_pipe_config(&crtc->base.mode,
						    crtc->config);
			DRM_DEBUG_KMS("[CRTC:%d] found active mode: ",
				      crtc->base.base.id);
			drm_mode_debug_printmodeline(&crtc->base.mode);
		}
	}

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

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		struct intel_shared_dpll *pll = &dev_priv->shared_dplls[i];

		if (!pll->on || pll->active)
			continue;

		DRM_DEBUG_KMS("%s enabled but not in use, disabling\n", pll->name);

		pll->disable(dev_priv, pll);
		pll->on = false;
	}

	if (IS_GEN9(dev))
		skl_wm_get_hw_state(dev);
	else if (HAS_PCH_SPLIT(dev))
		ilk_wm_get_hw_state(dev);

	if (force_restore) {
		i915_redisable_vga(dev);

		/*
		 * We need to use raw interfaces for restoring state to avoid
		 * checking (bogus) intermediate states.
		 */
		for_each_pipe(dev_priv, pipe) {
			struct drm_crtc *crtc =
				dev_priv->pipe_to_crtc_mapping[pipe];

			intel_set_mode(crtc, &crtc->mode, crtc->x, crtc->y,
				       crtc->primary->fb);
		}
	} else {
		intel_modeset_update_staged_output_state(dev);
	}

	intel_modeset_check_state(dev);
}

void intel_modeset_gem_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *c;
	struct drm_i915_gem_object *obj;

	mutex_lock(&dev->struct_mutex);
	intel_init_gt_powersave(dev);
	mutex_unlock(&dev->struct_mutex);

	/*
	 * There may be no VBT; and if the BIOS enabled SSC we can
	 * just keep using it to avoid unnecessary flicker.  Whereas if the
	 * BIOS isn't using it, don't assume it will work even if the VBT
	 * indicates as much.
	 */
	if (HAS_PCH_IBX(dev) || HAS_PCH_CPT(dev))
		dev_priv->vbt.lvds_use_ssc = !!(I915_READ(PCH_DREF_CONTROL) &
						DREF_SSC1_ENABLE);

	intel_modeset_init_hw(dev);

	intel_setup_overlay(dev);

	/*
	 * Make sure any fbs we allocated at startup are properly
	 * pinned & fenced.  When we do the allocation it's too early
	 * for this.
	 */
	mutex_lock(&dev->struct_mutex);
	for_each_crtc(dev, c) {
		obj = intel_fb_obj(c->primary->fb);
		if (obj == NULL)
			continue;

		if (intel_pin_and_fence_fb_obj(c->primary,
					       c->primary->fb,
					       NULL)) {
			DRM_ERROR("failed to pin boot fb on pipe %d\n",
				  to_intel_crtc(c)->pipe);
			drm_framebuffer_unreference(c->primary->fb);
			c->primary->fb = NULL;
			update_state_fb(c->primary);
		}
	}
	mutex_unlock(&dev->struct_mutex);

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

	mutex_lock(&dev->struct_mutex);

	intel_unregister_dsm_handler();

	intel_fbc_disable(dev);

	ironlake_teardown_rc6(dev);

	mutex_unlock(&dev->struct_mutex);

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
