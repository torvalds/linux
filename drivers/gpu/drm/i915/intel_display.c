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
#include "drmP.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "i915_trace.h"
#include "drm_dp_helper.h"
#include "drm_crtc_helper.h"
#include <linux/dma_remapping.h>

#define HAS_eDP (intel_pipe_has_type(crtc, INTEL_OUTPUT_EDP))

bool intel_pipe_has_type(struct drm_crtc *crtc, int type);
static void intel_increase_pllclock(struct drm_crtc *crtc);
static void intel_crtc_update_cursor(struct drm_crtc *crtc, bool on);

typedef struct {
	/* given values */
	int n;
	int m1, m2;
	int p1, p2;
	/* derived values */
	int	dot;
	int	vco;
	int	m;
	int	p;
} intel_clock_t;

typedef struct {
	int	min, max;
} intel_range_t;

typedef struct {
	int	dot_limit;
	int	p2_slow, p2_fast;
} intel_p2_t;

#define INTEL_P2_NUM		      2
typedef struct intel_limit intel_limit_t;
struct intel_limit {
	intel_range_t   dot, vco, n, m, m1, m2, p, p1;
	intel_p2_t	    p2;
	bool (* find_pll)(const intel_limit_t *, struct drm_crtc *,
			int, int, intel_clock_t *, intel_clock_t *);
};

/* FDI */
#define IRONLAKE_FDI_FREQ		2700000 /* in kHz for mode->clock */

static bool
intel_find_best_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
		    int target, int refclk, intel_clock_t *match_clock,
		    intel_clock_t *best_clock);
static bool
intel_g4x_find_best_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
			int target, int refclk, intel_clock_t *match_clock,
			intel_clock_t *best_clock);

static bool
intel_find_pll_g4x_dp(const intel_limit_t *, struct drm_crtc *crtc,
		      int target, int refclk, intel_clock_t *match_clock,
		      intel_clock_t *best_clock);
static bool
intel_find_pll_ironlake_dp(const intel_limit_t *, struct drm_crtc *crtc,
			   int target, int refclk, intel_clock_t *match_clock,
			   intel_clock_t *best_clock);

static inline u32 /* units of 100MHz */
intel_fdi_link_freq(struct drm_device *dev)
{
	if (IS_GEN5(dev)) {
		struct drm_i915_private *dev_priv = dev->dev_private;
		return (I915_READ(FDI_PLL_BIOS_0) & FDI_PLL_FB_CLOCK_MASK) + 2;
	} else
		return 27;
}

static const intel_limit_t intel_limits_i8xx_dvo = {
	.dot = { .min = 25000, .max = 350000 },
	.vco = { .min = 930000, .max = 1400000 },
	.n = { .min = 3, .max = 16 },
	.m = { .min = 96, .max = 140 },
	.m1 = { .min = 18, .max = 26 },
	.m2 = { .min = 6, .max = 16 },
	.p = { .min = 4, .max = 128 },
	.p1 = { .min = 2, .max = 33 },
	.p2 = { .dot_limit = 165000,
		.p2_slow = 4, .p2_fast = 2 },
	.find_pll = intel_find_best_PLL,
};

static const intel_limit_t intel_limits_i8xx_lvds = {
	.dot = { .min = 25000, .max = 350000 },
	.vco = { .min = 930000, .max = 1400000 },
	.n = { .min = 3, .max = 16 },
	.m = { .min = 96, .max = 140 },
	.m1 = { .min = 18, .max = 26 },
	.m2 = { .min = 6, .max = 16 },
	.p = { .min = 4, .max = 128 },
	.p1 = { .min = 1, .max = 6 },
	.p2 = { .dot_limit = 165000,
		.p2_slow = 14, .p2_fast = 7 },
	.find_pll = intel_find_best_PLL,
};

static const intel_limit_t intel_limits_i9xx_sdvo = {
	.dot = { .min = 20000, .max = 400000 },
	.vco = { .min = 1400000, .max = 2800000 },
	.n = { .min = 1, .max = 6 },
	.m = { .min = 70, .max = 120 },
	.m1 = { .min = 10, .max = 22 },
	.m2 = { .min = 5, .max = 9 },
	.p = { .min = 5, .max = 80 },
	.p1 = { .min = 1, .max = 8 },
	.p2 = { .dot_limit = 200000,
		.p2_slow = 10, .p2_fast = 5 },
	.find_pll = intel_find_best_PLL,
};

static const intel_limit_t intel_limits_i9xx_lvds = {
	.dot = { .min = 20000, .max = 400000 },
	.vco = { .min = 1400000, .max = 2800000 },
	.n = { .min = 1, .max = 6 },
	.m = { .min = 70, .max = 120 },
	.m1 = { .min = 10, .max = 22 },
	.m2 = { .min = 5, .max = 9 },
	.p = { .min = 7, .max = 98 },
	.p1 = { .min = 1, .max = 8 },
	.p2 = { .dot_limit = 112000,
		.p2_slow = 14, .p2_fast = 7 },
	.find_pll = intel_find_best_PLL,
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
	.find_pll = intel_g4x_find_best_PLL,
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
	.find_pll = intel_g4x_find_best_PLL,
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
	.find_pll = intel_g4x_find_best_PLL,
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
	.find_pll = intel_g4x_find_best_PLL,
};

static const intel_limit_t intel_limits_g4x_display_port = {
	.dot = { .min = 161670, .max = 227000 },
	.vco = { .min = 1750000, .max = 3500000},
	.n = { .min = 1, .max = 2 },
	.m = { .min = 97, .max = 108 },
	.m1 = { .min = 0x10, .max = 0x12 },
	.m2 = { .min = 0x05, .max = 0x06 },
	.p = { .min = 10, .max = 20 },
	.p1 = { .min = 1, .max = 2},
	.p2 = { .dot_limit = 0,
		.p2_slow = 10, .p2_fast = 10 },
	.find_pll = intel_find_pll_g4x_dp,
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
	.find_pll = intel_find_best_PLL,
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
	.find_pll = intel_find_best_PLL,
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
	.find_pll = intel_g4x_find_best_PLL,
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
	.find_pll = intel_g4x_find_best_PLL,
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
	.find_pll = intel_g4x_find_best_PLL,
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
	.find_pll = intel_g4x_find_best_PLL,
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
	.find_pll = intel_g4x_find_best_PLL,
};

static const intel_limit_t intel_limits_ironlake_display_port = {
	.dot = { .min = 25000, .max = 350000 },
	.vco = { .min = 1760000, .max = 3510000},
	.n = { .min = 1, .max = 2 },
	.m = { .min = 81, .max = 90 },
	.m1 = { .min = 12, .max = 22 },
	.m2 = { .min = 5, .max = 9 },
	.p = { .min = 10, .max = 20 },
	.p1 = { .min = 1, .max = 2},
	.p2 = { .dot_limit = 0,
		.p2_slow = 10, .p2_fast = 10 },
	.find_pll = intel_find_pll_ironlake_dp,
};

u32 intel_dpio_read(struct drm_i915_private *dev_priv, int reg)
{
	unsigned long flags;
	u32 val = 0;

	spin_lock_irqsave(&dev_priv->dpio_lock, flags);
	if (wait_for_atomic_us((I915_READ(DPIO_PKT) & DPIO_BUSY) == 0, 100)) {
		DRM_ERROR("DPIO idle wait timed out\n");
		goto out_unlock;
	}

	I915_WRITE(DPIO_REG, reg);
	I915_WRITE(DPIO_PKT, DPIO_RID | DPIO_OP_READ | DPIO_PORTID |
		   DPIO_BYTE);
	if (wait_for_atomic_us((I915_READ(DPIO_PKT) & DPIO_BUSY) == 0, 100)) {
		DRM_ERROR("DPIO read wait timed out\n");
		goto out_unlock;
	}
	val = I915_READ(DPIO_DATA);

out_unlock:
	spin_unlock_irqrestore(&dev_priv->dpio_lock, flags);
	return val;
}

static void vlv_init_dpio(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Reset the DPIO config */
	I915_WRITE(DPIO_CTL, 0);
	POSTING_READ(DPIO_CTL);
	I915_WRITE(DPIO_CTL, 1);
	POSTING_READ(DPIO_CTL);
}

static int intel_dual_link_lvds_callback(const struct dmi_system_id *id)
{
	DRM_INFO("Forcing lvds to dual link mode on %s\n", id->ident);
	return 1;
}

static const struct dmi_system_id intel_dual_link_lvds[] = {
	{
		.callback = intel_dual_link_lvds_callback,
		.ident = "Apple MacBook Pro (Core i5/i7 Series)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro8,2"),
		},
	},
	{ }	/* terminating entry */
};

static bool is_dual_link_lvds(struct drm_i915_private *dev_priv,
			      unsigned int reg)
{
	unsigned int val;

	/* use the module option value if specified */
	if (i915_lvds_channel_mode > 0)
		return i915_lvds_channel_mode == 2;

	if (dmi_check_system(intel_dual_link_lvds))
		return true;

	if (dev_priv->lvds_val)
		val = dev_priv->lvds_val;
	else {
		/* BIOS should set the proper LVDS register value at boot, but
		 * in reality, it doesn't set the value when the lid is closed;
		 * we need to check "the value to be set" in VBT when LVDS
		 * register is uninitialized.
		 */
		val = I915_READ(reg);
		if (!(val & ~LVDS_DETECTED))
			val = dev_priv->bios_lvds_val;
		dev_priv->lvds_val = val;
	}
	return (val & LVDS_CLKB_POWER_MASK) == LVDS_CLKB_POWER_UP;
}

static const intel_limit_t *intel_ironlake_limit(struct drm_crtc *crtc,
						int refclk)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	const intel_limit_t *limit;

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
		if (is_dual_link_lvds(dev_priv, PCH_LVDS)) {
			/* LVDS dual channel */
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
	} else if (intel_pipe_has_type(crtc, INTEL_OUTPUT_DISPLAYPORT) ||
			HAS_eDP)
		limit = &intel_limits_ironlake_display_port;
	else
		limit = &intel_limits_ironlake_dac;

	return limit;
}

static const intel_limit_t *intel_g4x_limit(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	const intel_limit_t *limit;

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
		if (is_dual_link_lvds(dev_priv, LVDS))
			/* LVDS with dual channel */
			limit = &intel_limits_g4x_dual_channel_lvds;
		else
			/* LVDS with dual channel */
			limit = &intel_limits_g4x_single_channel_lvds;
	} else if (intel_pipe_has_type(crtc, INTEL_OUTPUT_HDMI) ||
		   intel_pipe_has_type(crtc, INTEL_OUTPUT_ANALOG)) {
		limit = &intel_limits_g4x_hdmi;
	} else if (intel_pipe_has_type(crtc, INTEL_OUTPUT_SDVO)) {
		limit = &intel_limits_g4x_sdvo;
	} else if (intel_pipe_has_type(crtc, INTEL_OUTPUT_DISPLAYPORT)) {
		limit = &intel_limits_g4x_display_port;
	} else /* The option is for other outputs */
		limit = &intel_limits_i9xx_sdvo;

	return limit;
}

static const intel_limit_t *intel_limit(struct drm_crtc *crtc, int refclk)
{
	struct drm_device *dev = crtc->dev;
	const intel_limit_t *limit;

	if (HAS_PCH_SPLIT(dev))
		limit = intel_ironlake_limit(crtc, refclk);
	else if (IS_G4X(dev)) {
		limit = intel_g4x_limit(crtc);
	} else if (IS_PINEVIEW(dev)) {
		if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_pineview_lvds;
		else
			limit = &intel_limits_pineview_sdvo;
	} else if (!IS_GEN2(dev)) {
		if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_i9xx_lvds;
		else
			limit = &intel_limits_i9xx_sdvo;
	} else {
		if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_i8xx_lvds;
		else
			limit = &intel_limits_i8xx_dvo;
	}
	return limit;
}

/* m1 is reserved as 0 in Pineview, n is a ring counter */
static void pineview_clock(int refclk, intel_clock_t *clock)
{
	clock->m = clock->m2 + 2;
	clock->p = clock->p1 * clock->p2;
	clock->vco = refclk * clock->m / clock->n;
	clock->dot = clock->vco / clock->p;
}

static void intel_clock(struct drm_device *dev, int refclk, intel_clock_t *clock)
{
	if (IS_PINEVIEW(dev)) {
		pineview_clock(refclk, clock);
		return;
	}
	clock->m = 5 * (clock->m1 + 2) + (clock->m2 + 2);
	clock->p = clock->p1 * clock->p2;
	clock->vco = refclk * clock->m / (clock->n + 2);
	clock->dot = clock->vco / clock->p;
}

/**
 * Returns whether any output on the specified pipe is of the specified type
 */
bool intel_pipe_has_type(struct drm_crtc *crtc, int type)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_encoder *encoder;

	list_for_each_entry(encoder, &mode_config->encoder_list, base.head)
		if (encoder->base.crtc == crtc && encoder->type == type)
			return true;

	return false;
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
	if (clock->p1  < limit->p1.min  || limit->p1.max  < clock->p1)
		INTELPllInvalid("p1 out of range\n");
	if (clock->p   < limit->p.min   || limit->p.max   < clock->p)
		INTELPllInvalid("p out of range\n");
	if (clock->m2  < limit->m2.min  || limit->m2.max  < clock->m2)
		INTELPllInvalid("m2 out of range\n");
	if (clock->m1  < limit->m1.min  || limit->m1.max  < clock->m1)
		INTELPllInvalid("m1 out of range\n");
	if (clock->m1 <= clock->m2 && !IS_PINEVIEW(dev))
		INTELPllInvalid("m1 <= m2\n");
	if (clock->m   < limit->m.min   || limit->m.max   < clock->m)
		INTELPllInvalid("m out of range\n");
	if (clock->n   < limit->n.min   || limit->n.max   < clock->n)
		INTELPllInvalid("n out of range\n");
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
intel_find_best_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
		    int target, int refclk, intel_clock_t *match_clock,
		    intel_clock_t *best_clock)

{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	intel_clock_t clock;
	int err = target;

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS) &&
	    (I915_READ(LVDS)) != 0) {
		/*
		 * For LVDS, if the panel is on, just rely on its current
		 * settings for dual-channel.  We haven't figured out how to
		 * reliably set up different single/dual channel state, if we
		 * even can.
		 */
		if (is_dual_link_lvds(dev_priv, LVDS))
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
			/* m1 is always 0 in Pineview */
			if (clock.m2 >= clock.m1 && !IS_PINEVIEW(dev))
				break;
			for (clock.n = limit->n.min;
			     clock.n <= limit->n.max; clock.n++) {
				for (clock.p1 = limit->p1.min;
					clock.p1 <= limit->p1.max; clock.p1++) {
					int this_err;

					intel_clock(dev, refclk, &clock);
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
intel_g4x_find_best_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
			int target, int refclk, intel_clock_t *match_clock,
			intel_clock_t *best_clock)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	intel_clock_t clock;
	int max_n;
	bool found;
	/* approximately equals target * 0.00585 */
	int err_most = (target >> 8) + (target >> 9);
	found = false;

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
		int lvds_reg;

		if (HAS_PCH_SPLIT(dev))
			lvds_reg = PCH_LVDS;
		else
			lvds_reg = LVDS;
		if ((I915_READ(lvds_reg) & LVDS_CLKB_POWER_MASK) ==
		    LVDS_CLKB_POWER_UP)
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

					intel_clock(dev, refclk, &clock);
					if (!intel_PLL_is_valid(dev, limit,
								&clock))
						continue;
					if (match_clock &&
					    clock.p != match_clock->p)
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
intel_find_pll_ironlake_dp(const intel_limit_t *limit, struct drm_crtc *crtc,
			   int target, int refclk, intel_clock_t *match_clock,
			   intel_clock_t *best_clock)
{
	struct drm_device *dev = crtc->dev;
	intel_clock_t clock;

	if (target < 200000) {
		clock.n = 1;
		clock.p1 = 2;
		clock.p2 = 10;
		clock.m1 = 12;
		clock.m2 = 9;
	} else {
		clock.n = 2;
		clock.p1 = 1;
		clock.p2 = 10;
		clock.m1 = 14;
		clock.m2 = 8;
	}
	intel_clock(dev, refclk, &clock);
	memcpy(best_clock, &clock, sizeof(intel_clock_t));
	return true;
}

/* DisplayPort has only two frequencies, 162MHz and 270MHz */
static bool
intel_find_pll_g4x_dp(const intel_limit_t *limit, struct drm_crtc *crtc,
		      int target, int refclk, intel_clock_t *match_clock,
		      intel_clock_t *best_clock)
{
	intel_clock_t clock;
	if (target < 200000) {
		clock.p1 = 2;
		clock.p2 = 10;
		clock.n = 2;
		clock.m1 = 23;
		clock.m2 = 8;
	} else {
		clock.p1 = 1;
		clock.p2 = 10;
		clock.n = 1;
		clock.m1 = 14;
		clock.m2 = 2;
	}
	clock.m = 5 * (clock.m1 + 2) + (clock.m2 + 2);
	clock.p = (clock.p1 * clock.p2);
	clock.dot = 96000 * clock.m / (clock.n + 2) / clock.p;
	clock.vco = 0;
	memcpy(best_clock, &clock, sizeof(intel_clock_t));
	return true;
}

static void ironlake_wait_for_vblank(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 frame, frame_reg = PIPEFRAME(pipe);

	frame = I915_READ(frame_reg);

	if (wait_for(I915_READ_NOTRACE(frame_reg) != frame, 50))
		DRM_DEBUG_KMS("vblank wait timed out\n");
}

/**
 * intel_wait_for_vblank - wait for vblank on a given pipe
 * @dev: drm device
 * @pipe: pipe to wait for
 *
 * Wait for vblank to occur on a given pipe.  Needed for various bits of
 * mode setting code.
 */
void intel_wait_for_vblank(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipestat_reg = PIPESTAT(pipe);

	if (INTEL_INFO(dev)->gen >= 5) {
		ironlake_wait_for_vblank(dev, pipe);
		return;
	}

	/* Clear existing vblank status. Note this will clear any other
	 * sticky status fields as well.
	 *
	 * This races with i915_driver_irq_handler() with the result
	 * that either function could miss a vblank event.  Here it is not
	 * fatal, as we will either wait upon the next vblank interrupt or
	 * timeout.  Generally speaking intel_wait_for_vblank() is only
	 * called during modeset at which time the GPU should be idle and
	 * should *not* be performing page flips and thus not waiting on
	 * vblanks...
	 * Currently, the result of us stealing a vblank from the irq
	 * handler is that a single frame will be skipped during swapbuffers.
	 */
	I915_WRITE(pipestat_reg,
		   I915_READ(pipestat_reg) | PIPE_VBLANK_INTERRUPT_STATUS);

	/* Wait for vblank interrupt bit to set */
	if (wait_for(I915_READ(pipestat_reg) &
		     PIPE_VBLANK_INTERRUPT_STATUS,
		     50))
		DRM_DEBUG_KMS("vblank wait timed out\n");
}

/*
 * intel_wait_for_pipe_off - wait for pipe to turn off
 * @dev: drm device
 * @pipe: pipe to wait for
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
void intel_wait_for_pipe_off(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (INTEL_INFO(dev)->gen >= 4) {
		int reg = PIPECONF(pipe);

		/* Wait for the Pipe State to go off */
		if (wait_for((I915_READ(reg) & I965_PIPECONF_ACTIVE) == 0,
			     100))
			DRM_DEBUG_KMS("pipe_off wait timed out\n");
	} else {
		u32 last_line, line_mask;
		int reg = PIPEDSL(pipe);
		unsigned long timeout = jiffies + msecs_to_jiffies(100);

		if (IS_GEN2(dev))
			line_mask = DSL_LINEMASK_GEN2;
		else
			line_mask = DSL_LINEMASK_GEN3;

		/* Wait for the display line to settle */
		do {
			last_line = I915_READ(reg) & line_mask;
			mdelay(5);
		} while (((I915_READ(reg) & line_mask) != last_line) &&
			 time_after(timeout, jiffies));
		if (time_after(jiffies, timeout))
			DRM_DEBUG_KMS("pipe_off wait timed out\n");
	}
}

static const char *state_string(bool enabled)
{
	return enabled ? "on" : "off";
}

/* Only for pre-ILK configs */
static void assert_pll(struct drm_i915_private *dev_priv,
		       enum pipe pipe, bool state)
{
	int reg;
	u32 val;
	bool cur_state;

	reg = DPLL(pipe);
	val = I915_READ(reg);
	cur_state = !!(val & DPLL_VCO_ENABLE);
	WARN(cur_state != state,
	     "PLL state assertion failure (expected %s, current %s)\n",
	     state_string(state), state_string(cur_state));
}
#define assert_pll_enabled(d, p) assert_pll(d, p, true)
#define assert_pll_disabled(d, p) assert_pll(d, p, false)

/* For ILK+ */
static void assert_pch_pll(struct drm_i915_private *dev_priv,
			   struct intel_crtc *intel_crtc, bool state)
{
	int reg;
	u32 val;
	bool cur_state;

	if (HAS_PCH_LPT(dev_priv->dev)) {
		DRM_DEBUG_DRIVER("LPT detected: skipping PCH PLL test\n");
		return;
	}

	if (!intel_crtc->pch_pll) {
		WARN(1, "asserting PCH PLL enabled with no PLL\n");
		return;
	}

	if (HAS_PCH_CPT(dev_priv->dev)) {
		u32 pch_dpll;

		pch_dpll = I915_READ(PCH_DPLL_SEL);

		/* Make sure the selected PLL is enabled to the transcoder */
		WARN(!((pch_dpll >> (4 * intel_crtc->pipe)) & 8),
		     "transcoder %d PLL not enabled\n", intel_crtc->pipe);
	}

	reg = intel_crtc->pch_pll->pll_reg;
	val = I915_READ(reg);
	cur_state = !!(val & DPLL_VCO_ENABLE);
	WARN(cur_state != state,
	     "PCH PLL state assertion failure (expected %s, current %s)\n",
	     state_string(state), state_string(cur_state));
}
#define assert_pch_pll_enabled(d, p) assert_pch_pll(d, p, true)
#define assert_pch_pll_disabled(d, p) assert_pch_pll(d, p, false)

static void assert_fdi_tx(struct drm_i915_private *dev_priv,
			  enum pipe pipe, bool state)
{
	int reg;
	u32 val;
	bool cur_state;

	if (IS_HASWELL(dev_priv->dev)) {
		/* On Haswell, DDI is used instead of FDI_TX_CTL */
		reg = DDI_FUNC_CTL(pipe);
		val = I915_READ(reg);
		cur_state = !!(val & PIPE_DDI_FUNC_ENABLE);
	} else {
		reg = FDI_TX_CTL(pipe);
		val = I915_READ(reg);
		cur_state = !!(val & FDI_TX_ENABLE);
	}
	WARN(cur_state != state,
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

	if (IS_HASWELL(dev_priv->dev) && pipe > 0) {
			DRM_ERROR("Attempting to enable FDI_RX on Haswell pipe > 0\n");
			return;
	} else {
		reg = FDI_RX_CTL(pipe);
		val = I915_READ(reg);
		cur_state = !!(val & FDI_RX_ENABLE);
	}
	WARN(cur_state != state,
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
	if (dev_priv->info->gen == 5)
		return;

	/* On Haswell, DDI ports are responsible for the FDI PLL setup */
	if (IS_HASWELL(dev_priv->dev))
		return;

	reg = FDI_TX_CTL(pipe);
	val = I915_READ(reg);
	WARN(!(val & FDI_TX_PLL_ENABLE), "FDI TX PLL assertion failure, should be active but is disabled\n");
}

static void assert_fdi_rx_pll_enabled(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	int reg;
	u32 val;

	if (IS_HASWELL(dev_priv->dev) && pipe > 0) {
		DRM_ERROR("Attempting to enable FDI on Haswell with pipe > 0\n");
		return;
	}
	reg = FDI_RX_CTL(pipe);
	val = I915_READ(reg);
	WARN(!(val & FDI_RX_PLL_ENABLE), "FDI RX PLL assertion failure, should be active but is disabled\n");
}

static void assert_panel_unlocked(struct drm_i915_private *dev_priv,
				  enum pipe pipe)
{
	int pp_reg, lvds_reg;
	u32 val;
	enum pipe panel_pipe = PIPE_A;
	bool locked = true;

	if (HAS_PCH_SPLIT(dev_priv->dev)) {
		pp_reg = PCH_PP_CONTROL;
		lvds_reg = PCH_LVDS;
	} else {
		pp_reg = PP_CONTROL;
		lvds_reg = LVDS;
	}

	val = I915_READ(pp_reg);
	if (!(val & PANEL_POWER_ON) ||
	    ((val & PANEL_UNLOCK_REGS) == PANEL_UNLOCK_REGS))
		locked = false;

	if (I915_READ(lvds_reg) & LVDS_PIPEB_SELECT)
		panel_pipe = PIPE_B;

	WARN(panel_pipe == pipe && locked,
	     "panel assertion failure, pipe %c regs locked\n",
	     pipe_name(pipe));
}

void assert_pipe(struct drm_i915_private *dev_priv,
		 enum pipe pipe, bool state)
{
	int reg;
	u32 val;
	bool cur_state;

	/* if we need the pipe A quirk it must be always on */
	if (pipe == PIPE_A && dev_priv->quirks & QUIRK_PIPEA_FORCE)
		state = true;

	reg = PIPECONF(pipe);
	val = I915_READ(reg);
	cur_state = !!(val & PIPECONF_ENABLE);
	WARN(cur_state != state,
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
	WARN(cur_state != state,
	     "plane %c assertion failure (expected %s, current %s)\n",
	     plane_name(plane), state_string(state), state_string(cur_state));
}

#define assert_plane_enabled(d, p) assert_plane(d, p, true)
#define assert_plane_disabled(d, p) assert_plane(d, p, false)

static void assert_planes_disabled(struct drm_i915_private *dev_priv,
				   enum pipe pipe)
{
	int reg, i;
	u32 val;
	int cur_pipe;

	/* Planes are fixed to pipes on ILK+ */
	if (HAS_PCH_SPLIT(dev_priv->dev)) {
		reg = DSPCNTR(pipe);
		val = I915_READ(reg);
		WARN((val & DISPLAY_PLANE_ENABLE),
		     "plane %c assertion failure, should be disabled but not\n",
		     plane_name(pipe));
		return;
	}

	/* Need to check both planes against the pipe */
	for (i = 0; i < 2; i++) {
		reg = DSPCNTR(i);
		val = I915_READ(reg);
		cur_pipe = (val & DISPPLANE_SEL_PIPE_MASK) >>
			DISPPLANE_SEL_PIPE_SHIFT;
		WARN((val & DISPLAY_PLANE_ENABLE) && pipe == cur_pipe,
		     "plane %c assertion failure, should be off on pipe %c but is still active\n",
		     plane_name(i), pipe_name(pipe));
	}
}

static void assert_pch_refclk_enabled(struct drm_i915_private *dev_priv)
{
	u32 val;
	bool enabled;

	if (HAS_PCH_LPT(dev_priv->dev)) {
		DRM_DEBUG_DRIVER("LPT does not has PCH refclk, skipping check\n");
		return;
	}

	val = I915_READ(PCH_DREF_CONTROL);
	enabled = !!(val & (DREF_SSC_SOURCE_MASK | DREF_NONSPREAD_SOURCE_MASK |
			    DREF_SUPERSPREAD_SOURCE_MASK));
	WARN(!enabled, "PCH refclk assertion failure, should be active but is disabled\n");
}

static void assert_transcoder_disabled(struct drm_i915_private *dev_priv,
				       enum pipe pipe)
{
	int reg;
	u32 val;
	bool enabled;

	reg = TRANSCONF(pipe);
	val = I915_READ(reg);
	enabled = !!(val & TRANS_ENABLE);
	WARN(enabled,
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
	} else {
		if ((val & DP_PIPE_MASK) != (pipe << 30))
			return false;
	}
	return true;
}

static bool hdmi_pipe_enabled(struct drm_i915_private *dev_priv,
			      enum pipe pipe, u32 val)
{
	if ((val & PORT_ENABLE) == 0)
		return false;

	if (HAS_PCH_CPT(dev_priv->dev)) {
		if ((val & PORT_TRANS_SEL_MASK) != PORT_TRANS_SEL_CPT(pipe))
			return false;
	} else {
		if ((val & TRANSCODER_MASK) != TRANSCODER(pipe))
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
	WARN(dp_pipe_enabled(dev_priv, pipe, port_sel, val),
	     "PCH DP (0x%08x) enabled on transcoder %c, should be disabled\n",
	     reg, pipe_name(pipe));
}

static void assert_pch_hdmi_disabled(struct drm_i915_private *dev_priv,
				     enum pipe pipe, int reg)
{
	u32 val = I915_READ(reg);
	WARN(hdmi_pipe_enabled(dev_priv, val, pipe),
	     "PCH HDMI (0x%08x) enabled on transcoder %c, should be disabled\n",
	     reg, pipe_name(pipe));
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
	WARN(adpa_pipe_enabled(dev_priv, val, pipe),
	     "PCH VGA enabled on transcoder %c, should be disabled\n",
	     pipe_name(pipe));

	reg = PCH_LVDS;
	val = I915_READ(reg);
	WARN(lvds_pipe_enabled(dev_priv, val, pipe),
	     "PCH LVDS enabled on transcoder %c, should be disabled\n",
	     pipe_name(pipe));

	assert_pch_hdmi_disabled(dev_priv, pipe, HDMIB);
	assert_pch_hdmi_disabled(dev_priv, pipe, HDMIC);
	assert_pch_hdmi_disabled(dev_priv, pipe, HDMID);
}

/**
 * intel_enable_pll - enable a PLL
 * @dev_priv: i915 private structure
 * @pipe: pipe PLL to enable
 *
 * Enable @pipe's PLL so we can start pumping pixels from a plane.  Check to
 * make sure the PLL reg is writable first though, since the panel write
 * protect mechanism may be enabled.
 *
 * Note!  This is for pre-ILK only.
 */
static void intel_enable_pll(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	int reg;
	u32 val;

	/* No really, not for ILK+ */
	BUG_ON(dev_priv->info->gen >= 5);

	/* PLL is protected by panel, make sure we can write it */
	if (IS_MOBILE(dev_priv->dev) && !IS_I830(dev_priv->dev))
		assert_panel_unlocked(dev_priv, pipe);

	reg = DPLL(pipe);
	val = I915_READ(reg);
	val |= DPLL_VCO_ENABLE;

	/* We do this three times for luck */
	I915_WRITE(reg, val);
	POSTING_READ(reg);
	udelay(150); /* wait for warmup */
	I915_WRITE(reg, val);
	POSTING_READ(reg);
	udelay(150); /* wait for warmup */
	I915_WRITE(reg, val);
	POSTING_READ(reg);
	udelay(150); /* wait for warmup */
}

/**
 * intel_disable_pll - disable a PLL
 * @dev_priv: i915 private structure
 * @pipe: pipe PLL to disable
 *
 * Disable the PLL for @pipe, making sure the pipe is off first.
 *
 * Note!  This is for pre-ILK only.
 */
static void intel_disable_pll(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	int reg;
	u32 val;

	/* Don't disable pipe A or pipe A PLLs if needed */
	if (pipe == PIPE_A && (dev_priv->quirks & QUIRK_PIPEA_FORCE))
		return;

	/* Make sure the pipe isn't still relying on us */
	assert_pipe_disabled(dev_priv, pipe);

	reg = DPLL(pipe);
	val = I915_READ(reg);
	val &= ~DPLL_VCO_ENABLE;
	I915_WRITE(reg, val);
	POSTING_READ(reg);
}

/* SBI access */
static void
intel_sbi_write(struct drm_i915_private *dev_priv, u16 reg, u32 value)
{
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->dpio_lock, flags);
	if (wait_for((I915_READ(SBI_CTL_STAT) & SBI_READY) == 0,
				100)) {
		DRM_ERROR("timeout waiting for SBI to become ready\n");
		goto out_unlock;
	}

	I915_WRITE(SBI_ADDR,
			(reg << 16));
	I915_WRITE(SBI_DATA,
			value);
	I915_WRITE(SBI_CTL_STAT,
			SBI_BUSY |
			SBI_CTL_OP_CRWR);

	if (wait_for((I915_READ(SBI_CTL_STAT) & (SBI_READY | SBI_RESPONSE_SUCCESS)) == 0,
				100)) {
		DRM_ERROR("timeout waiting for SBI to complete write transaction\n");
		goto out_unlock;
	}

out_unlock:
	spin_unlock_irqrestore(&dev_priv->dpio_lock, flags);
}

static u32
intel_sbi_read(struct drm_i915_private *dev_priv, u16 reg)
{
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&dev_priv->dpio_lock, flags);
	if (wait_for((I915_READ(SBI_CTL_STAT) & SBI_READY) == 0,
				100)) {
		DRM_ERROR("timeout waiting for SBI to become ready\n");
		goto out_unlock;
	}

	I915_WRITE(SBI_ADDR,
			(reg << 16));
	I915_WRITE(SBI_CTL_STAT,
			SBI_BUSY |
			SBI_CTL_OP_CRRD);

	if (wait_for((I915_READ(SBI_CTL_STAT) & (SBI_READY | SBI_RESPONSE_SUCCESS)) == 0,
				100)) {
		DRM_ERROR("timeout waiting for SBI to complete read transaction\n");
		goto out_unlock;
	}

	value = I915_READ(SBI_DATA);

out_unlock:
	spin_unlock_irqrestore(&dev_priv->dpio_lock, flags);
	return value;
}

/**
 * intel_enable_pch_pll - enable PCH PLL
 * @dev_priv: i915 private structure
 * @pipe: pipe PLL to enable
 *
 * The PCH PLL needs to be enabled before the PCH transcoder, since it
 * drives the transcoder clock.
 */
static void intel_enable_pch_pll(struct intel_crtc *intel_crtc)
{
	struct drm_i915_private *dev_priv = intel_crtc->base.dev->dev_private;
	struct intel_pch_pll *pll = intel_crtc->pch_pll;
	int reg;
	u32 val;

	/* PCH only available on ILK+ */
	BUG_ON(dev_priv->info->gen < 5);
	BUG_ON(pll == NULL);
	BUG_ON(pll->refcount == 0);

	DRM_DEBUG_KMS("enable PCH PLL %x (active %d, on? %d)for crtc %d\n",
		      pll->pll_reg, pll->active, pll->on,
		      intel_crtc->base.base.id);

	/* PCH refclock must be enabled first */
	assert_pch_refclk_enabled(dev_priv);

	if (pll->active++ && pll->on) {
		assert_pch_pll_enabled(dev_priv, intel_crtc);
		return;
	}

	DRM_DEBUG_KMS("enabling PCH PLL %x\n", pll->pll_reg);

	reg = pll->pll_reg;
	val = I915_READ(reg);
	val |= DPLL_VCO_ENABLE;
	I915_WRITE(reg, val);
	POSTING_READ(reg);
	udelay(200);

	pll->on = true;
}

static void intel_disable_pch_pll(struct intel_crtc *intel_crtc)
{
	struct drm_i915_private *dev_priv = intel_crtc->base.dev->dev_private;
	struct intel_pch_pll *pll = intel_crtc->pch_pll;
	int reg;
	u32 val;

	/* PCH only available on ILK+ */
	BUG_ON(dev_priv->info->gen < 5);
	if (pll == NULL)
	       return;

	BUG_ON(pll->refcount == 0);

	DRM_DEBUG_KMS("disable PCH PLL %x (active %d, on? %d) for crtc %d\n",
		      pll->pll_reg, pll->active, pll->on,
		      intel_crtc->base.base.id);

	BUG_ON(pll->active == 0);
	if (--pll->active) {
		assert_pch_pll_enabled(dev_priv, intel_crtc);
		return;
	}

	DRM_DEBUG_KMS("disabling PCH PLL %x\n", pll->pll_reg);

	/* Make sure transcoder isn't still depending on us */
	assert_transcoder_disabled(dev_priv, intel_crtc->pipe);

	reg = pll->pll_reg;
	val = I915_READ(reg);
	val &= ~DPLL_VCO_ENABLE;
	I915_WRITE(reg, val);
	POSTING_READ(reg);
	udelay(200);

	pll->on = false;
}

static void intel_enable_transcoder(struct drm_i915_private *dev_priv,
				    enum pipe pipe)
{
	int reg;
	u32 val, pipeconf_val;
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];

	/* PCH only available on ILK+ */
	BUG_ON(dev_priv->info->gen < 5);

	/* Make sure PCH DPLL is enabled */
	assert_pch_pll_enabled(dev_priv, to_intel_crtc(crtc));

	/* FDI must be feeding us bits for PCH ports */
	assert_fdi_tx_enabled(dev_priv, pipe);
	assert_fdi_rx_enabled(dev_priv, pipe);

	if (IS_HASWELL(dev_priv->dev) && pipe > 0) {
		DRM_ERROR("Attempting to enable transcoder on Haswell with pipe > 0\n");
		return;
	}
	reg = TRANSCONF(pipe);
	val = I915_READ(reg);
	pipeconf_val = I915_READ(PIPECONF(pipe));

	if (HAS_PCH_IBX(dev_priv->dev)) {
		/*
		 * make the BPC in transcoder be consistent with
		 * that in pipeconf reg.
		 */
		val &= ~PIPE_BPC_MASK;
		val |= pipeconf_val & PIPE_BPC_MASK;
	}

	val &= ~TRANS_INTERLACE_MASK;
	if ((pipeconf_val & PIPECONF_INTERLACE_MASK) == PIPECONF_INTERLACED_ILK)
		if (HAS_PCH_IBX(dev_priv->dev) &&
		    intel_pipe_has_type(crtc, INTEL_OUTPUT_SDVO))
			val |= TRANS_LEGACY_INTERLACED_ILK;
		else
			val |= TRANS_INTERLACED;
	else
		val |= TRANS_PROGRESSIVE;

	I915_WRITE(reg, val | TRANS_ENABLE);
	if (wait_for(I915_READ(reg) & TRANS_STATE_ENABLE, 100))
		DRM_ERROR("failed to enable transcoder %d\n", pipe);
}

static void intel_disable_transcoder(struct drm_i915_private *dev_priv,
				     enum pipe pipe)
{
	int reg;
	u32 val;

	/* FDI relies on the transcoder */
	assert_fdi_tx_disabled(dev_priv, pipe);
	assert_fdi_rx_disabled(dev_priv, pipe);

	/* Ports must be off as well */
	assert_pch_ports_disabled(dev_priv, pipe);

	reg = TRANSCONF(pipe);
	val = I915_READ(reg);
	val &= ~TRANS_ENABLE;
	I915_WRITE(reg, val);
	/* wait for PCH transcoder off, transcoder state */
	if (wait_for((I915_READ(reg) & TRANS_STATE_ENABLE) == 0, 50))
		DRM_ERROR("failed to disable transcoder %d\n", pipe);
}

/**
 * intel_enable_pipe - enable a pipe, asserting requirements
 * @dev_priv: i915 private structure
 * @pipe: pipe to enable
 * @pch_port: on ILK+, is this pipe driving a PCH port or not
 *
 * Enable @pipe, making sure that various hardware specific requirements
 * are met, if applicable, e.g. PLL enabled, LVDS pairs enabled, etc.
 *
 * @pipe should be %PIPE_A or %PIPE_B.
 *
 * Will wait until the pipe is actually running (i.e. first vblank) before
 * returning.
 */
static void intel_enable_pipe(struct drm_i915_private *dev_priv, enum pipe pipe,
			      bool pch_port)
{
	int reg;
	u32 val;

	/*
	 * A pipe without a PLL won't actually be able to drive bits from
	 * a plane.  On ILK+ the pipe PLLs are integrated, so we don't
	 * need the check.
	 */
	if (!HAS_PCH_SPLIT(dev_priv->dev))
		assert_pll_enabled(dev_priv, pipe);
	else {
		if (pch_port) {
			/* if driving the PCH, we need FDI enabled */
			assert_fdi_rx_pll_enabled(dev_priv, pipe);
			assert_fdi_tx_pll_enabled(dev_priv, pipe);
		}
		/* FIXME: assert CPU port conditions for SNB+ */
	}

	reg = PIPECONF(pipe);
	val = I915_READ(reg);
	if (val & PIPECONF_ENABLE)
		return;

	I915_WRITE(reg, val | PIPECONF_ENABLE);
	intel_wait_for_vblank(dev_priv->dev, pipe);
}

/**
 * intel_disable_pipe - disable a pipe, asserting requirements
 * @dev_priv: i915 private structure
 * @pipe: pipe to disable
 *
 * Disable @pipe, making sure that various hardware specific requirements
 * are met, if applicable, e.g. plane disabled, panel fitter off, etc.
 *
 * @pipe should be %PIPE_A or %PIPE_B.
 *
 * Will wait until the pipe has shut down before returning.
 */
static void intel_disable_pipe(struct drm_i915_private *dev_priv,
			       enum pipe pipe)
{
	int reg;
	u32 val;

	/*
	 * Make sure planes won't keep trying to pump pixels to us,
	 * or we might hang the display.
	 */
	assert_planes_disabled(dev_priv, pipe);

	/* Don't disable pipe A or pipe A PLLs if needed */
	if (pipe == PIPE_A && (dev_priv->quirks & QUIRK_PIPEA_FORCE))
		return;

	reg = PIPECONF(pipe);
	val = I915_READ(reg);
	if ((val & PIPECONF_ENABLE) == 0)
		return;

	I915_WRITE(reg, val & ~PIPECONF_ENABLE);
	intel_wait_for_pipe_off(dev_priv->dev, pipe);
}

/*
 * Plane regs are double buffered, going from enabled->disabled needs a
 * trigger in order to latch.  The display address reg provides this.
 */
void intel_flush_display_plane(struct drm_i915_private *dev_priv,
				      enum plane plane)
{
	I915_WRITE(DSPADDR(plane), I915_READ(DSPADDR(plane)));
	I915_WRITE(DSPSURF(plane), I915_READ(DSPSURF(plane)));
}

/**
 * intel_enable_plane - enable a display plane on a given pipe
 * @dev_priv: i915 private structure
 * @plane: plane to enable
 * @pipe: pipe being fed
 *
 * Enable @plane on @pipe, making sure that @pipe is running first.
 */
static void intel_enable_plane(struct drm_i915_private *dev_priv,
			       enum plane plane, enum pipe pipe)
{
	int reg;
	u32 val;

	/* If the pipe isn't enabled, we can't pump pixels and may hang */
	assert_pipe_enabled(dev_priv, pipe);

	reg = DSPCNTR(plane);
	val = I915_READ(reg);
	if (val & DISPLAY_PLANE_ENABLE)
		return;

	I915_WRITE(reg, val | DISPLAY_PLANE_ENABLE);
	intel_flush_display_plane(dev_priv, plane);
	intel_wait_for_vblank(dev_priv->dev, pipe);
}

/**
 * intel_disable_plane - disable a display plane
 * @dev_priv: i915 private structure
 * @plane: plane to disable
 * @pipe: pipe consuming the data
 *
 * Disable @plane; should be an independent operation.
 */
static void intel_disable_plane(struct drm_i915_private *dev_priv,
				enum plane plane, enum pipe pipe)
{
	int reg;
	u32 val;

	reg = DSPCNTR(plane);
	val = I915_READ(reg);
	if ((val & DISPLAY_PLANE_ENABLE) == 0)
		return;

	I915_WRITE(reg, val & ~DISPLAY_PLANE_ENABLE);
	intel_flush_display_plane(dev_priv, plane);
	intel_wait_for_vblank(dev_priv->dev, pipe);
}

static void disable_pch_dp(struct drm_i915_private *dev_priv,
			   enum pipe pipe, int reg, u32 port_sel)
{
	u32 val = I915_READ(reg);
	if (dp_pipe_enabled(dev_priv, pipe, port_sel, val)) {
		DRM_DEBUG_KMS("Disabling pch dp %x on pipe %d\n", reg, pipe);
		I915_WRITE(reg, val & ~DP_PORT_EN);
	}
}

static void disable_pch_hdmi(struct drm_i915_private *dev_priv,
			     enum pipe pipe, int reg)
{
	u32 val = I915_READ(reg);
	if (hdmi_pipe_enabled(dev_priv, val, pipe)) {
		DRM_DEBUG_KMS("Disabling pch HDMI %x on pipe %d\n",
			      reg, pipe);
		I915_WRITE(reg, val & ~PORT_ENABLE);
	}
}

/* Disable any ports connected to this transcoder */
static void intel_disable_pch_ports(struct drm_i915_private *dev_priv,
				    enum pipe pipe)
{
	u32 reg, val;

	val = I915_READ(PCH_PP_CONTROL);
	I915_WRITE(PCH_PP_CONTROL, val | PANEL_UNLOCK_REGS);

	disable_pch_dp(dev_priv, pipe, PCH_DP_B, TRANS_DP_PORT_SEL_B);
	disable_pch_dp(dev_priv, pipe, PCH_DP_C, TRANS_DP_PORT_SEL_C);
	disable_pch_dp(dev_priv, pipe, PCH_DP_D, TRANS_DP_PORT_SEL_D);

	reg = PCH_ADPA;
	val = I915_READ(reg);
	if (adpa_pipe_enabled(dev_priv, val, pipe))
		I915_WRITE(reg, val & ~ADPA_DAC_ENABLE);

	reg = PCH_LVDS;
	val = I915_READ(reg);
	if (lvds_pipe_enabled(dev_priv, val, pipe)) {
		DRM_DEBUG_KMS("disable lvds on pipe %d val 0x%08x\n", pipe, val);
		I915_WRITE(reg, val & ~LVDS_PORT_EN);
		POSTING_READ(reg);
		udelay(100);
	}

	disable_pch_hdmi(dev_priv, pipe, HDMIB);
	disable_pch_hdmi(dev_priv, pipe, HDMIC);
	disable_pch_hdmi(dev_priv, pipe, HDMID);
}

int
intel_pin_and_fence_fb_obj(struct drm_device *dev,
			   struct drm_i915_gem_object *obj,
			   struct intel_ring_buffer *pipelined)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 alignment;
	int ret;

	switch (obj->tiling_mode) {
	case I915_TILING_NONE:
		if (IS_BROADWATER(dev) || IS_CRESTLINE(dev))
			alignment = 128 * 1024;
		else if (INTEL_INFO(dev)->gen >= 4)
			alignment = 4 * 1024;
		else
			alignment = 64 * 1024;
		break;
	case I915_TILING_X:
		/* pin() will align the object as required by fence */
		alignment = 0;
		break;
	case I915_TILING_Y:
		/* FIXME: Is this true? */
		DRM_ERROR("Y tiled not allowed for scan out buffers\n");
		return -EINVAL;
	default:
		BUG();
	}

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
	return 0;

err_unpin:
	i915_gem_object_unpin(obj);
err_interruptible:
	dev_priv->mm.interruptible = true;
	return ret;
}

void intel_unpin_fb_obj(struct drm_i915_gem_object *obj)
{
	i915_gem_object_unpin_fence(obj);
	i915_gem_object_unpin(obj);
}

static int i9xx_update_plane(struct drm_crtc *crtc, struct drm_framebuffer *fb,
			     int x, int y)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_framebuffer *intel_fb;
	struct drm_i915_gem_object *obj;
	int plane = intel_crtc->plane;
	unsigned long Start, Offset;
	u32 dspcntr;
	u32 reg;

	switch (plane) {
	case 0:
	case 1:
		break;
	default:
		DRM_ERROR("Can't update plane %d in SAREA\n", plane);
		return -EINVAL;
	}

	intel_fb = to_intel_framebuffer(fb);
	obj = intel_fb->obj;

	reg = DSPCNTR(plane);
	dspcntr = I915_READ(reg);
	/* Mask out pixel format bits in case we change it */
	dspcntr &= ~DISPPLANE_PIXFORMAT_MASK;
	switch (fb->bits_per_pixel) {
	case 8:
		dspcntr |= DISPPLANE_8BPP;
		break;
	case 16:
		if (fb->depth == 15)
			dspcntr |= DISPPLANE_15_16BPP;
		else
			dspcntr |= DISPPLANE_16BPP;
		break;
	case 24:
	case 32:
		dspcntr |= DISPPLANE_32BPP_NO_ALPHA;
		break;
	default:
		DRM_ERROR("Unknown color depth %d\n", fb->bits_per_pixel);
		return -EINVAL;
	}
	if (INTEL_INFO(dev)->gen >= 4) {
		if (obj->tiling_mode != I915_TILING_NONE)
			dspcntr |= DISPPLANE_TILED;
		else
			dspcntr &= ~DISPPLANE_TILED;
	}

	I915_WRITE(reg, dspcntr);

	Start = obj->gtt_offset;
	Offset = y * fb->pitches[0] + x * (fb->bits_per_pixel / 8);

	DRM_DEBUG_KMS("Writing base %08lX %08lX %d %d %d\n",
		      Start, Offset, x, y, fb->pitches[0]);
	I915_WRITE(DSPSTRIDE(plane), fb->pitches[0]);
	if (INTEL_INFO(dev)->gen >= 4) {
		I915_MODIFY_DISPBASE(DSPSURF(plane), Start);
		I915_WRITE(DSPTILEOFF(plane), (y << 16) | x);
		I915_WRITE(DSPADDR(plane), Offset);
	} else
		I915_WRITE(DSPADDR(plane), Start + Offset);
	POSTING_READ(reg);

	return 0;
}

static int ironlake_update_plane(struct drm_crtc *crtc,
				 struct drm_framebuffer *fb, int x, int y)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_framebuffer *intel_fb;
	struct drm_i915_gem_object *obj;
	int plane = intel_crtc->plane;
	unsigned long Start, Offset;
	u32 dspcntr;
	u32 reg;

	switch (plane) {
	case 0:
	case 1:
	case 2:
		break;
	default:
		DRM_ERROR("Can't update plane %d in SAREA\n", plane);
		return -EINVAL;
	}

	intel_fb = to_intel_framebuffer(fb);
	obj = intel_fb->obj;

	reg = DSPCNTR(plane);
	dspcntr = I915_READ(reg);
	/* Mask out pixel format bits in case we change it */
	dspcntr &= ~DISPPLANE_PIXFORMAT_MASK;
	switch (fb->bits_per_pixel) {
	case 8:
		dspcntr |= DISPPLANE_8BPP;
		break;
	case 16:
		if (fb->depth != 16)
			return -EINVAL;

		dspcntr |= DISPPLANE_16BPP;
		break;
	case 24:
	case 32:
		if (fb->depth == 24)
			dspcntr |= DISPPLANE_32BPP_NO_ALPHA;
		else if (fb->depth == 30)
			dspcntr |= DISPPLANE_32BPP_30BIT_NO_ALPHA;
		else
			return -EINVAL;
		break;
	default:
		DRM_ERROR("Unknown color depth %d\n", fb->bits_per_pixel);
		return -EINVAL;
	}

	if (obj->tiling_mode != I915_TILING_NONE)
		dspcntr |= DISPPLANE_TILED;
	else
		dspcntr &= ~DISPPLANE_TILED;

	/* must disable */
	dspcntr |= DISPPLANE_TRICKLE_FEED_DISABLE;

	I915_WRITE(reg, dspcntr);

	Start = obj->gtt_offset;
	Offset = y * fb->pitches[0] + x * (fb->bits_per_pixel / 8);

	DRM_DEBUG_KMS("Writing base %08lX %08lX %d %d %d\n",
		      Start, Offset, x, y, fb->pitches[0]);
	I915_WRITE(DSPSTRIDE(plane), fb->pitches[0]);
	I915_MODIFY_DISPBASE(DSPSURF(plane), Start);
	I915_WRITE(DSPTILEOFF(plane), (y << 16) | x);
	I915_WRITE(DSPADDR(plane), Offset);
	POSTING_READ(reg);

	return 0;
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
	intel_increase_pllclock(crtc);

	return dev_priv->display.update_plane(crtc, fb, x, y);
}

static int
intel_finish_fb(struct drm_framebuffer *old_fb)
{
	struct drm_i915_gem_object *obj = to_intel_framebuffer(old_fb)->obj;
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
	bool was_interruptible = dev_priv->mm.interruptible;
	int ret;

	wait_event(dev_priv->pending_flip_queue,
		   atomic_read(&dev_priv->mm.wedged) ||
		   atomic_read(&obj->pending_flip) == 0);

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

static int
intel_pipe_set_base(struct drm_crtc *crtc, int x, int y,
		    struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int ret;

	/* no fb bound */
	if (!crtc->fb) {
		DRM_ERROR("No FB bound\n");
		return 0;
	}

	if(intel_crtc->plane > dev_priv->num_pipe) {
		DRM_ERROR("no plane for crtc: plane %d, num_pipes %d\n",
				intel_crtc->plane,
				dev_priv->num_pipe);
		return -EINVAL;
	}

	mutex_lock(&dev->struct_mutex);
	ret = intel_pin_and_fence_fb_obj(dev,
					 to_intel_framebuffer(crtc->fb)->obj,
					 NULL);
	if (ret != 0) {
		mutex_unlock(&dev->struct_mutex);
		DRM_ERROR("pin & fence failed\n");
		return ret;
	}

	if (old_fb)
		intel_finish_fb(old_fb);

	ret = dev_priv->display.update_plane(crtc, crtc->fb, x, y);
	if (ret) {
		intel_unpin_fb_obj(to_intel_framebuffer(crtc->fb)->obj);
		mutex_unlock(&dev->struct_mutex);
		DRM_ERROR("failed to update base address\n");
		return ret;
	}

	if (old_fb) {
		intel_wait_for_vblank(dev, intel_crtc->pipe);
		intel_unpin_fb_obj(to_intel_framebuffer(old_fb)->obj);
	}

	intel_update_fbc(dev);
	mutex_unlock(&dev->struct_mutex);

	if (!dev->primary->master)
		return 0;

	master_priv = dev->primary->master->driver_priv;
	if (!master_priv->sarea_priv)
		return 0;

	if (intel_crtc->pipe) {
		master_priv->sarea_priv->pipeB_x = x;
		master_priv->sarea_priv->pipeB_y = y;
	} else {
		master_priv->sarea_priv->pipeA_x = x;
		master_priv->sarea_priv->pipeA_y = y;
	}

	return 0;
}

static void ironlake_set_pll_edp(struct drm_crtc *crtc, int clock)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpa_ctl;

	DRM_DEBUG_KMS("eDP PLL enable for clock %d\n", clock);
	dpa_ctl = I915_READ(DP_A);
	dpa_ctl &= ~DP_PLL_FREQ_MASK;

	if (clock < 200000) {
		u32 temp;
		dpa_ctl |= DP_PLL_FREQ_160MHZ;
		/* workaround for 160Mhz:
		   1) program 0x4600c bits 15:0 = 0x8124
		   2) program 0x46010 bit 0 = 1
		   3) program 0x46034 bit 24 = 1
		   4) program 0x64000 bit 14 = 1
		   */
		temp = I915_READ(0x4600c);
		temp &= 0xffff0000;
		I915_WRITE(0x4600c, temp | 0x8124);

		temp = I915_READ(0x46010);
		I915_WRITE(0x46010, temp | 1);

		temp = I915_READ(0x46034);
		I915_WRITE(0x46034, temp | (1 << 24));
	} else {
		dpa_ctl |= DP_PLL_FREQ_270MHZ;
	}
	I915_WRITE(DP_A, dpa_ctl);

	POSTING_READ(DP_A);
	udelay(500);
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

static void cpt_phase_pointer_enable(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 flags = I915_READ(SOUTH_CHICKEN1);

	flags |= FDI_PHASE_SYNC_OVR(pipe);
	I915_WRITE(SOUTH_CHICKEN1, flags); /* once to unlock... */
	flags |= FDI_PHASE_SYNC_EN(pipe);
	I915_WRITE(SOUTH_CHICKEN1, flags); /* then again to enable */
	POSTING_READ(SOUTH_CHICKEN1);
}

/* The FDI link training functions for ILK/Ibexpeak. */
static void ironlake_fdi_link_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;
	u32 reg, temp, tries;

	/* FDI needs bits from pipe & plane first */
	assert_pipe_enabled(dev_priv, pipe);
	assert_plane_enabled(dev_priv, plane);

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
	temp &= ~(7 << 19);
	temp |= (intel_crtc->fdi_lanes - 1) << 19;
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
	if (HAS_PCH_IBX(dev)) {
		I915_WRITE(FDI_RX_CHICKEN(pipe), FDI_RX_PHASE_SYNC_POINTER_OVR);
		I915_WRITE(FDI_RX_CHICKEN(pipe), FDI_RX_PHASE_SYNC_POINTER_OVR |
			   FDI_RX_PHASE_SYNC_POINTER_EN);
	}

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
	temp &= ~(7 << 19);
	temp |= (intel_crtc->fdi_lanes - 1) << 19;
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_1;
	temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
	/* SNB-B */
	temp |= FDI_LINK_TRAIN_400MV_0DB_SNB_B;
	I915_WRITE(reg, temp | FDI_TX_ENABLE);

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

	if (HAS_PCH_CPT(dev))
		cpt_phase_pointer_enable(dev, pipe);

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
	u32 reg, temp, i;

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
	temp &= ~(7 << 19);
	temp |= (intel_crtc->fdi_lanes - 1) << 19;
	temp &= ~(FDI_LINK_TRAIN_AUTO | FDI_LINK_TRAIN_NONE_IVB);
	temp |= FDI_LINK_TRAIN_PATTERN_1_IVB;
	temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
	temp |= FDI_LINK_TRAIN_400MV_0DB_SNB_B;
	temp |= FDI_COMPOSITE_SYNC;
	I915_WRITE(reg, temp | FDI_TX_ENABLE);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_LINK_TRAIN_AUTO;
	temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
	temp |= FDI_LINK_TRAIN_PATTERN_1_CPT;
	temp |= FDI_COMPOSITE_SYNC;
	I915_WRITE(reg, temp | FDI_RX_ENABLE);

	POSTING_READ(reg);
	udelay(150);

	if (HAS_PCH_CPT(dev))
		cpt_phase_pointer_enable(dev, pipe);

	for (i = 0; i < 4; i++) {
		reg = FDI_TX_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
		temp |= snb_b_fdi_train_param[i];
		I915_WRITE(reg, temp);

		POSTING_READ(reg);
		udelay(500);

		reg = FDI_RX_IIR(pipe);
		temp = I915_READ(reg);
		DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);

		if (temp & FDI_RX_BIT_LOCK ||
		    (I915_READ(reg) & FDI_RX_BIT_LOCK)) {
			I915_WRITE(reg, temp | FDI_RX_BIT_LOCK);
			DRM_DEBUG_KMS("FDI train 1 done.\n");
			break;
		}
	}
	if (i == 4)
		DRM_ERROR("FDI train 1 fail!\n");

	/* Train 2 */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_LINK_TRAIN_NONE_IVB;
	temp |= FDI_LINK_TRAIN_PATTERN_2_IVB;
	temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
	temp |= FDI_LINK_TRAIN_400MV_0DB_SNB_B;
	I915_WRITE(reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
	temp |= FDI_LINK_TRAIN_PATTERN_2_CPT;
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

		reg = FDI_RX_IIR(pipe);
		temp = I915_READ(reg);
		DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);

		if (temp & FDI_RX_SYMBOL_LOCK) {
			I915_WRITE(reg, temp | FDI_RX_SYMBOL_LOCK);
			DRM_DEBUG_KMS("FDI train 2 done.\n");
			break;
		}
	}
	if (i == 4)
		DRM_ERROR("FDI train 2 fail!\n");

	DRM_DEBUG_KMS("FDI train done.\n");
}

static void ironlake_fdi_pll_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 reg, temp;

	/* Write the TU size bits so error detection works */
	I915_WRITE(FDI_RX_TUSIZE1(pipe),
		   I915_READ(PIPE_DATA_M1(pipe)) & TU_SIZE_MASK);

	/* enable PCH FDI RX PLL, wait warmup plus DMI latency */
	reg = FDI_RX_CTL(pipe);
	temp = I915_READ(reg);
	temp &= ~((0x7 << 19) | (0x7 << 16));
	temp |= (intel_crtc->fdi_lanes - 1) << 19;
	temp |= (I915_READ(PIPECONF(pipe)) & PIPE_BPC_MASK) << 11;
	I915_WRITE(reg, temp | FDI_RX_PLL_ENABLE);

	POSTING_READ(reg);
	udelay(200);

	/* Switch from Rawclk to PCDclk */
	temp = I915_READ(reg);
	I915_WRITE(reg, temp | FDI_PCDCLK);

	POSTING_READ(reg);
	udelay(200);

	/* On Haswell, the PLL configuration for ports and pipes is handled
	 * separately, as part of DDI setup */
	if (!IS_HASWELL(dev)) {
		/* Enable CPU FDI TX PLL, always on for Ironlake */
		reg = FDI_TX_CTL(pipe);
		temp = I915_READ(reg);
		if ((temp & FDI_TX_PLL_ENABLE) == 0) {
			I915_WRITE(reg, temp | FDI_TX_PLL_ENABLE);

			POSTING_READ(reg);
			udelay(100);
		}
	}
}

static void cpt_phase_pointer_disable(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 flags = I915_READ(SOUTH_CHICKEN1);

	flags &= ~(FDI_PHASE_SYNC_EN(pipe));
	I915_WRITE(SOUTH_CHICKEN1, flags); /* once to disable... */
	flags &= ~(FDI_PHASE_SYNC_OVR(pipe));
	I915_WRITE(SOUTH_CHICKEN1, flags); /* then again to lock */
	POSTING_READ(SOUTH_CHICKEN1);
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
	temp |= (I915_READ(PIPECONF(pipe)) & PIPE_BPC_MASK) << 11;
	I915_WRITE(reg, temp & ~FDI_RX_ENABLE);

	POSTING_READ(reg);
	udelay(100);

	/* Ironlake workaround, disable clock pointer after downing FDI */
	if (HAS_PCH_IBX(dev)) {
		I915_WRITE(FDI_RX_CHICKEN(pipe), FDI_RX_PHASE_SYNC_POINTER_OVR);
		I915_WRITE(FDI_RX_CHICKEN(pipe),
			   I915_READ(FDI_RX_CHICKEN(pipe) &
				     ~FDI_RX_PHASE_SYNC_POINTER_EN));
	} else if (HAS_PCH_CPT(dev)) {
		cpt_phase_pointer_disable(dev, pipe);
	}

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
	temp |= (I915_READ(PIPECONF(pipe)) & PIPE_BPC_MASK) << 11;
	I915_WRITE(reg, temp);

	POSTING_READ(reg);
	udelay(100);
}

static void intel_crtc_wait_for_pending_flips(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;

	if (crtc->fb == NULL)
		return;

	mutex_lock(&dev->struct_mutex);
	intel_finish_fb(crtc->fb);
	mutex_unlock(&dev->struct_mutex);
}

static bool intel_crtc_driving_pch(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_encoder *encoder;

	/*
	 * If there's a non-PCH eDP on this crtc, it must be DP_A, and that
	 * must be driven by its own crtc; no sharing is possible.
	 */
	list_for_each_entry(encoder, &mode_config->encoder_list, base.head) {
		if (encoder->base.crtc != crtc)
			continue;

		/* On Haswell, LPT PCH handles the VGA connection via FDI, and Haswell
		 * CPU handles all others */
		if (IS_HASWELL(dev)) {
			/* It is still unclear how this will work on PPT, so throw up a warning */
			WARN_ON(!HAS_PCH_LPT(dev));

			if (encoder->type == DRM_MODE_ENCODER_DAC) {
				DRM_DEBUG_KMS("Haswell detected DAC encoder, assuming is PCH\n");
				return true;
			} else {
				DRM_DEBUG_KMS("Haswell detected encoder %d, assuming is CPU\n",
						encoder->type);
				return false;
			}
		}

		switch (encoder->type) {
		case INTEL_OUTPUT_EDP:
			if (!intel_encoder_is_pch_edp(&encoder->base))
				return false;
			continue;
		}
	}

	return true;
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

	/* For PCH output, training FDI link */
	dev_priv->display.fdi_link_train(crtc);

	intel_enable_pch_pll(intel_crtc);

	if (HAS_PCH_CPT(dev)) {
		u32 sel;

		temp = I915_READ(PCH_DPLL_SEL);
		switch (pipe) {
		default:
		case 0:
			temp |= TRANSA_DPLL_ENABLE;
			sel = TRANSA_DPLLB_SEL;
			break;
		case 1:
			temp |= TRANSB_DPLL_ENABLE;
			sel = TRANSB_DPLLB_SEL;
			break;
		case 2:
			temp |= TRANSC_DPLL_ENABLE;
			sel = TRANSC_DPLLB_SEL;
			break;
		}
		if (intel_crtc->pch_pll->pll_reg == _PCH_DPLL_B)
			temp |= sel;
		else
			temp &= ~sel;
		I915_WRITE(PCH_DPLL_SEL, temp);
	}

	/* set transcoder timing, panel must allow it */
	assert_panel_unlocked(dev_priv, pipe);
	I915_WRITE(TRANS_HTOTAL(pipe), I915_READ(HTOTAL(pipe)));
	I915_WRITE(TRANS_HBLANK(pipe), I915_READ(HBLANK(pipe)));
	I915_WRITE(TRANS_HSYNC(pipe),  I915_READ(HSYNC(pipe)));

	I915_WRITE(TRANS_VTOTAL(pipe), I915_READ(VTOTAL(pipe)));
	I915_WRITE(TRANS_VBLANK(pipe), I915_READ(VBLANK(pipe)));
	I915_WRITE(TRANS_VSYNC(pipe),  I915_READ(VSYNC(pipe)));
	I915_WRITE(TRANS_VSYNCSHIFT(pipe),  I915_READ(VSYNCSHIFT(pipe)));

	if (!IS_HASWELL(dev))
		intel_fdi_normal_train(crtc);

	/* For PCH DP, enable TRANS_DP_CTL */
	if (HAS_PCH_CPT(dev) &&
	    (intel_pipe_has_type(crtc, INTEL_OUTPUT_DISPLAYPORT) ||
	     intel_pipe_has_type(crtc, INTEL_OUTPUT_EDP))) {
		u32 bpc = (I915_READ(PIPECONF(pipe)) & PIPE_BPC_MASK) >> 5;
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
			DRM_DEBUG_KMS("Wrong PCH DP port return. Guess port B\n");
			temp |= TRANS_DP_PORT_SEL_B;
			break;
		}

		I915_WRITE(reg, temp);
	}

	intel_enable_transcoder(dev_priv, pipe);
}

static void intel_put_pch_pll(struct intel_crtc *intel_crtc)
{
	struct intel_pch_pll *pll = intel_crtc->pch_pll;

	if (pll == NULL)
		return;

	if (pll->refcount == 0) {
		WARN(1, "bad PCH PLL refcount\n");
		return;
	}

	--pll->refcount;
	intel_crtc->pch_pll = NULL;
}

static struct intel_pch_pll *intel_get_pch_pll(struct intel_crtc *intel_crtc, u32 dpll, u32 fp)
{
	struct drm_i915_private *dev_priv = intel_crtc->base.dev->dev_private;
	struct intel_pch_pll *pll;
	int i;

	pll = intel_crtc->pch_pll;
	if (pll) {
		DRM_DEBUG_KMS("CRTC:%d reusing existing PCH PLL %x\n",
			      intel_crtc->base.base.id, pll->pll_reg);
		goto prepare;
	}

	for (i = 0; i < dev_priv->num_pch_pll; i++) {
		pll = &dev_priv->pch_plls[i];

		/* Only want to check enabled timings first */
		if (pll->refcount == 0)
			continue;

		if (dpll == (I915_READ(pll->pll_reg) & 0x7fffffff) &&
		    fp == I915_READ(pll->fp0_reg)) {
			DRM_DEBUG_KMS("CRTC:%d sharing existing PCH PLL %x (refcount %d, ative %d)\n",
				      intel_crtc->base.base.id,
				      pll->pll_reg, pll->refcount, pll->active);

			goto found;
		}
	}

	/* Ok no matching timings, maybe there's a free one? */
	for (i = 0; i < dev_priv->num_pch_pll; i++) {
		pll = &dev_priv->pch_plls[i];
		if (pll->refcount == 0) {
			DRM_DEBUG_KMS("CRTC:%d allocated PCH PLL %x\n",
				      intel_crtc->base.base.id, pll->pll_reg);
			goto found;
		}
	}

	return NULL;

found:
	intel_crtc->pch_pll = pll;
	pll->refcount++;
	DRM_DEBUG_DRIVER("using pll %d for pipe %d\n", i, intel_crtc->pipe);
prepare: /* separate function? */
	DRM_DEBUG_DRIVER("switching PLL %x off\n", pll->pll_reg);

	/* Wait for the clocks to stabilize before rewriting the regs */
	I915_WRITE(pll->pll_reg, dpll & ~DPLL_VCO_ENABLE);
	POSTING_READ(pll->pll_reg);
	udelay(150);

	I915_WRITE(pll->fp0_reg, fp);
	I915_WRITE(pll->pll_reg, dpll & ~DPLL_VCO_ENABLE);
	pll->on = false;
	return pll;
}

void intel_cpt_verify_modeset(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int dslreg = PIPEDSL(pipe), tc2reg = TRANS_CHICKEN2(pipe);
	u32 temp;

	temp = I915_READ(dslreg);
	udelay(500);
	if (wait_for(I915_READ(dslreg) != temp, 5)) {
		/* Without this, mode sets may fail silently on FDI */
		I915_WRITE(tc2reg, TRANS_AUTOTRAIN_GEN_STALL_DIS);
		udelay(250);
		I915_WRITE(tc2reg, 0);
		if (wait_for(I915_READ(dslreg) != temp, 5))
			DRM_ERROR("mode set failed: pipe %d stuck\n", pipe);
	}
}

static void ironlake_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;
	u32 temp;
	bool is_pch_port;

	if (intel_crtc->active)
		return;

	intel_crtc->active = true;
	intel_update_watermarks(dev);

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
		temp = I915_READ(PCH_LVDS);
		if ((temp & LVDS_PORT_EN) == 0)
			I915_WRITE(PCH_LVDS, temp | LVDS_PORT_EN);
	}

	is_pch_port = intel_crtc_driving_pch(crtc);

	if (is_pch_port)
		ironlake_fdi_pll_enable(crtc);
	else
		ironlake_fdi_disable(crtc);

	/* Enable panel fitting for LVDS */
	if (dev_priv->pch_pf_size &&
	    (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS) || HAS_eDP)) {
		/* Force use of hard-coded filter coefficients
		 * as some pre-programmed values are broken,
		 * e.g. x201.
		 */
		I915_WRITE(PF_CTL(pipe), PF_ENABLE | PF_FILTER_MED_3x3);
		I915_WRITE(PF_WIN_POS(pipe), dev_priv->pch_pf_pos);
		I915_WRITE(PF_WIN_SZ(pipe), dev_priv->pch_pf_size);
	}

	/*
	 * On ILK+ LUT must be loaded before the pipe is running but with
	 * clocks enabled
	 */
	intel_crtc_load_lut(crtc);

	intel_enable_pipe(dev_priv, pipe, is_pch_port);
	intel_enable_plane(dev_priv, plane, pipe);

	if (is_pch_port)
		ironlake_pch_enable(crtc);

	mutex_lock(&dev->struct_mutex);
	intel_update_fbc(dev);
	mutex_unlock(&dev->struct_mutex);

	intel_crtc_update_cursor(crtc, true);
}

static void ironlake_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;
	u32 reg, temp;

	if (!intel_crtc->active)
		return;

	intel_crtc_wait_for_pending_flips(crtc);
	drm_vblank_off(dev, pipe);
	intel_crtc_update_cursor(crtc, false);

	intel_disable_plane(dev_priv, plane, pipe);

	if (dev_priv->cfb_plane == plane)
		intel_disable_fbc(dev);

	intel_disable_pipe(dev_priv, pipe);

	/* Disable PF */
	I915_WRITE(PF_CTL(pipe), 0);
	I915_WRITE(PF_WIN_SZ(pipe), 0);

	ironlake_fdi_disable(crtc);

	/* This is a horrible layering violation; we should be doing this in
	 * the connector/encoder ->prepare instead, but we don't always have
	 * enough information there about the config to know whether it will
	 * actually be necessary or just cause undesired flicker.
	 */
	intel_disable_pch_ports(dev_priv, pipe);

	intel_disable_transcoder(dev_priv, pipe);

	if (HAS_PCH_CPT(dev)) {
		/* disable TRANS_DP_CTL */
		reg = TRANS_DP_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~(TRANS_DP_OUTPUT_ENABLE | TRANS_DP_PORT_SEL_MASK);
		temp |= TRANS_DP_PORT_SEL_NONE;
		I915_WRITE(reg, temp);

		/* disable DPLL_SEL */
		temp = I915_READ(PCH_DPLL_SEL);
		switch (pipe) {
		case 0:
			temp &= ~(TRANSA_DPLL_ENABLE | TRANSA_DPLLB_SEL);
			break;
		case 1:
			temp &= ~(TRANSB_DPLL_ENABLE | TRANSB_DPLLB_SEL);
			break;
		case 2:
			/* C shares PLL A or B */
			temp &= ~(TRANSC_DPLL_ENABLE | TRANSC_DPLLB_SEL);
			break;
		default:
			BUG(); /* wtf */
		}
		I915_WRITE(PCH_DPLL_SEL, temp);
	}

	/* disable PCH DPLL */
	intel_disable_pch_pll(intel_crtc);

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

	intel_crtc->active = false;
	intel_update_watermarks(dev);

	mutex_lock(&dev->struct_mutex);
	intel_update_fbc(dev);
	mutex_unlock(&dev->struct_mutex);
}

static void ironlake_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;

	/* XXX: When our outputs are all unaware of DPMS modes other than off
	 * and on, we should map those modes to DRM_MODE_DPMS_OFF in the CRTC.
	 */
	switch (mode) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		DRM_DEBUG_KMS("crtc %d/%d dpms on\n", pipe, plane);
		ironlake_crtc_enable(crtc);
		break;

	case DRM_MODE_DPMS_OFF:
		DRM_DEBUG_KMS("crtc %d/%d dpms off\n", pipe, plane);
		ironlake_crtc_disable(crtc);
		break;
	}
}

static void ironlake_crtc_off(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	intel_put_pch_pll(intel_crtc);
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

static void i9xx_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;

	if (intel_crtc->active)
		return;

	intel_crtc->active = true;
	intel_update_watermarks(dev);

	intel_enable_pll(dev_priv, pipe);
	intel_enable_pipe(dev_priv, pipe, false);
	intel_enable_plane(dev_priv, plane, pipe);

	intel_crtc_load_lut(crtc);
	intel_update_fbc(dev);

	/* Give the overlay scaler a chance to enable if it's on this pipe */
	intel_crtc_dpms_overlay(intel_crtc, true);
	intel_crtc_update_cursor(crtc, true);
}

static void i9xx_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;

	if (!intel_crtc->active)
		return;

	/* Give the overlay scaler a chance to disable if it's on this pipe */
	intel_crtc_wait_for_pending_flips(crtc);
	drm_vblank_off(dev, pipe);
	intel_crtc_dpms_overlay(intel_crtc, false);
	intel_crtc_update_cursor(crtc, false);

	if (dev_priv->cfb_plane == plane)
		intel_disable_fbc(dev);

	intel_disable_plane(dev_priv, plane, pipe);
	intel_disable_pipe(dev_priv, pipe);
	intel_disable_pll(dev_priv, pipe);

	intel_crtc->active = false;
	intel_update_fbc(dev);
	intel_update_watermarks(dev);
}

static void i9xx_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	/* XXX: When our outputs are all unaware of DPMS modes other than off
	 * and on, we should map those modes to DRM_MODE_DPMS_OFF in the CRTC.
	 */
	switch (mode) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		i9xx_crtc_enable(crtc);
		break;
	case DRM_MODE_DPMS_OFF:
		i9xx_crtc_disable(crtc);
		break;
	}
}

static void i9xx_crtc_off(struct drm_crtc *crtc)
{
}

/**
 * Sets the power management mode of the pipe and plane.
 */
static void intel_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	bool enabled;

	if (intel_crtc->dpms_mode == mode)
		return;

	intel_crtc->dpms_mode = mode;

	dev_priv->display.dpms(crtc, mode);

	if (!dev->primary->master)
		return;

	master_priv = dev->primary->master->driver_priv;
	if (!master_priv->sarea_priv)
		return;

	enabled = crtc->enabled && mode != DRM_MODE_DPMS_OFF;

	switch (pipe) {
	case 0:
		master_priv->sarea_priv->pipeA_w = enabled ? crtc->mode.hdisplay : 0;
		master_priv->sarea_priv->pipeA_h = enabled ? crtc->mode.vdisplay : 0;
		break;
	case 1:
		master_priv->sarea_priv->pipeB_w = enabled ? crtc->mode.hdisplay : 0;
		master_priv->sarea_priv->pipeB_h = enabled ? crtc->mode.vdisplay : 0;
		break;
	default:
		DRM_ERROR("Can't update pipe %c in SAREA\n", pipe_name(pipe));
		break;
	}
}

static void intel_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);
	dev_priv->display.off(crtc);

	assert_plane_disabled(dev->dev_private, to_intel_crtc(crtc)->plane);
	assert_pipe_disabled(dev->dev_private, to_intel_crtc(crtc)->pipe);

	if (crtc->fb) {
		mutex_lock(&dev->struct_mutex);
		intel_unpin_fb_obj(to_intel_framebuffer(crtc->fb)->obj);
		mutex_unlock(&dev->struct_mutex);
	}
}

/* Prepare for a mode set.
 *
 * Note we could be a lot smarter here.  We need to figure out which outputs
 * will be enabled, which disabled (in short, how the config will changes)
 * and perform the minimum necessary steps to accomplish that, e.g. updating
 * watermarks, FBC configuration, making sure PLLs are programmed correctly,
 * panel fitting is in the proper state, etc.
 */
static void i9xx_crtc_prepare(struct drm_crtc *crtc)
{
	i9xx_crtc_disable(crtc);
}

static void i9xx_crtc_commit(struct drm_crtc *crtc)
{
	i9xx_crtc_enable(crtc);
}

static void ironlake_crtc_prepare(struct drm_crtc *crtc)
{
	ironlake_crtc_disable(crtc);
}

static void ironlake_crtc_commit(struct drm_crtc *crtc)
{
	ironlake_crtc_enable(crtc);
}

void intel_encoder_prepare(struct drm_encoder *encoder)
{
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
	/* lvds has its own version of prepare see intel_lvds_prepare */
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_OFF);
}

void intel_encoder_commit(struct drm_encoder *encoder)
{
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
	struct drm_device *dev = encoder->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);

	/* lvds has its own version of commit see intel_lvds_commit */
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_ON);

	if (HAS_PCH_CPT(dev))
		intel_cpt_verify_modeset(dev, intel_crtc->pipe);
}

void intel_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(encoder);

	drm_encoder_cleanup(encoder);
	kfree(intel_encoder);
}

static bool intel_crtc_mode_fixup(struct drm_crtc *crtc,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;

	if (HAS_PCH_SPLIT(dev)) {
		/* FDI link clock is fixed at 2.7G */
		if (mode->clock * 3 > IRONLAKE_FDI_FREQ * 4)
			return false;
	}

	/* All interlaced capable intel hw wants timings in frames. Note though
	 * that intel_lvds_mode_fixup does some funny tricks with the crtc
	 * timings, so we need to be careful not to clobber these.*/
	if (!(adjusted_mode->private_flags & INTEL_MODE_CRTC_TIMINGS_SET))
		drm_mode_set_crtcinfo(adjusted_mode, 0);

	return true;
}

static int valleyview_get_display_clock_speed(struct drm_device *dev)
{
	return 400000; /* FIXME */
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

struct fdi_m_n {
	u32        tu;
	u32        gmch_m;
	u32        gmch_n;
	u32        link_m;
	u32        link_n;
};

static void
fdi_reduce_ratio(u32 *num, u32 *den)
{
	while (*num > 0xffffff || *den > 0xffffff) {
		*num >>= 1;
		*den >>= 1;
	}
}

static void
ironlake_compute_m_n(int bits_per_pixel, int nlanes, int pixel_clock,
		     int link_clock, struct fdi_m_n *m_n)
{
	m_n->tu = 64; /* default size */

	/* BUG_ON(pixel_clock > INT_MAX / 36); */
	m_n->gmch_m = bits_per_pixel * pixel_clock;
	m_n->gmch_n = link_clock * nlanes * 8;
	fdi_reduce_ratio(&m_n->gmch_m, &m_n->gmch_n);

	m_n->link_m = pixel_clock;
	m_n->link_n = link_clock;
	fdi_reduce_ratio(&m_n->link_m, &m_n->link_n);
}

static inline bool intel_panel_use_ssc(struct drm_i915_private *dev_priv)
{
	if (i915_panel_use_ssc >= 0)
		return i915_panel_use_ssc != 0;
	return dev_priv->lvds_use_ssc
		&& !(dev_priv->quirks & QUIRK_LVDS_SSC_DISABLE);
}

/**
 * intel_choose_pipe_bpp_dither - figure out what color depth the pipe should send
 * @crtc: CRTC structure
 * @mode: requested mode
 *
 * A pipe may be connected to one or more outputs.  Based on the depth of the
 * attached framebuffer, choose a good color depth to use on the pipe.
 *
 * If possible, match the pipe depth to the fb depth.  In some cases, this
 * isn't ideal, because the connected output supports a lesser or restricted
 * set of depths.  Resolve that here:
 *    LVDS typically supports only 6bpc, so clamp down in that case
 *    HDMI supports only 8bpc or 12bpc, so clamp to 8bpc with dither for 10bpc
 *    Displays may support a restricted set as well, check EDID and clamp as
 *      appropriate.
 *    DP may want to dither down to 6bpc to fit larger modes
 *
 * RETURNS:
 * Dithering requirement (i.e. false if display bpc and pipe bpc match,
 * true if they don't match).
 */
static bool intel_choose_pipe_bpp_dither(struct drm_crtc *crtc,
					 unsigned int *pipe_bpp,
					 struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	unsigned int display_bpc = UINT_MAX, bpc;

	/* Walk the encoders & connectors on this crtc, get min bpc */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct intel_encoder *intel_encoder = to_intel_encoder(encoder);

		if (encoder->crtc != crtc)
			continue;

		if (intel_encoder->type == INTEL_OUTPUT_LVDS) {
			unsigned int lvds_bpc;

			if ((I915_READ(PCH_LVDS) & LVDS_A3_POWER_MASK) ==
			    LVDS_A3_POWER_UP)
				lvds_bpc = 8;
			else
				lvds_bpc = 6;

			if (lvds_bpc < display_bpc) {
				DRM_DEBUG_KMS("clamping display bpc (was %d) to LVDS (%d)\n", display_bpc, lvds_bpc);
				display_bpc = lvds_bpc;
			}
			continue;
		}

		if (intel_encoder->type == INTEL_OUTPUT_EDP) {
			/* Use VBT settings if we have an eDP panel */
			unsigned int edp_bpc = dev_priv->edp.bpp / 3;

			if (edp_bpc < display_bpc) {
				DRM_DEBUG_KMS("clamping display bpc (was %d) to eDP (%d)\n", display_bpc, edp_bpc);
				display_bpc = edp_bpc;
			}
			continue;
		}

		/* Not one of the known troublemakers, check the EDID */
		list_for_each_entry(connector, &dev->mode_config.connector_list,
				    head) {
			if (connector->encoder != encoder)
				continue;

			/* Don't use an invalid EDID bpc value */
			if (connector->display_info.bpc &&
			    connector->display_info.bpc < display_bpc) {
				DRM_DEBUG_KMS("clamping display bpc (was %d) to EDID reported max of %d\n", display_bpc, connector->display_info.bpc);
				display_bpc = connector->display_info.bpc;
			}
		}

		/*
		 * HDMI is either 12 or 8, so if the display lets 10bpc sneak
		 * through, clamp it down.  (Note: >12bpc will be caught below.)
		 */
		if (intel_encoder->type == INTEL_OUTPUT_HDMI) {
			if (display_bpc > 8 && display_bpc < 12) {
				DRM_DEBUG_KMS("forcing bpc to 12 for HDMI\n");
				display_bpc = 12;
			} else {
				DRM_DEBUG_KMS("forcing bpc to 8 for HDMI\n");
				display_bpc = 8;
			}
		}
	}

	if (mode->private_flags & INTEL_MODE_DP_FORCE_6BPC) {
		DRM_DEBUG_KMS("Dithering DP to 6bpc\n");
		display_bpc = 6;
	}

	/*
	 * We could just drive the pipe at the highest bpc all the time and
	 * enable dithering as needed, but that costs bandwidth.  So choose
	 * the minimum value that expresses the full color range of the fb but
	 * also stays within the max display bpc discovered above.
	 */

	switch (crtc->fb->depth) {
	case 8:
		bpc = 8; /* since we go through a colormap */
		break;
	case 15:
	case 16:
		bpc = 6; /* min is 18bpp */
		break;
	case 24:
		bpc = 8;
		break;
	case 30:
		bpc = 10;
		break;
	case 48:
		bpc = 12;
		break;
	default:
		DRM_DEBUG("unsupported depth, assuming 24 bits\n");
		bpc = min((unsigned int)8, display_bpc);
		break;
	}

	display_bpc = min(display_bpc, bpc);

	DRM_DEBUG_KMS("setting pipe bpc to %d (max display bpc %d)\n",
		      bpc, display_bpc);

	*pipe_bpp = display_bpc * 3;

	return display_bpc != bpc;
}

static int i9xx_get_refclk(struct drm_crtc *crtc, int num_connectors)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int refclk;

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS) &&
	    intel_panel_use_ssc(dev_priv) && num_connectors < 2) {
		refclk = dev_priv->lvds_ssc_freq * 1000;
		DRM_DEBUG_KMS("using SSC reference clock of %d MHz\n",
			      refclk / 1000);
	} else if (!IS_GEN2(dev)) {
		refclk = 96000;
	} else {
		refclk = 48000;
	}

	return refclk;
}

static void i9xx_adjust_sdvo_tv_clock(struct drm_display_mode *adjusted_mode,
				      intel_clock_t *clock)
{
	/* SDVO TV has fixed PLL values depend on its clock range,
	   this mirrors vbios setting. */
	if (adjusted_mode->clock >= 100000
	    && adjusted_mode->clock < 140500) {
		clock->p1 = 2;
		clock->p2 = 10;
		clock->n = 3;
		clock->m1 = 16;
		clock->m2 = 8;
	} else if (adjusted_mode->clock >= 140500
		   && adjusted_mode->clock <= 200000) {
		clock->p1 = 1;
		clock->p2 = 10;
		clock->n = 6;
		clock->m1 = 12;
		clock->m2 = 8;
	}
}

static void i9xx_update_pll_dividers(struct drm_crtc *crtc,
				     intel_clock_t *clock,
				     intel_clock_t *reduced_clock)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 fp, fp2 = 0;

	if (IS_PINEVIEW(dev)) {
		fp = (1 << clock->n) << 16 | clock->m1 << 8 | clock->m2;
		if (reduced_clock)
			fp2 = (1 << reduced_clock->n) << 16 |
				reduced_clock->m1 << 8 | reduced_clock->m2;
	} else {
		fp = clock->n << 16 | clock->m1 << 8 | clock->m2;
		if (reduced_clock)
			fp2 = reduced_clock->n << 16 | reduced_clock->m1 << 8 |
				reduced_clock->m2;
	}

	I915_WRITE(FP0(pipe), fp);

	intel_crtc->lowfreq_avail = false;
	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS) &&
	    reduced_clock && i915_powersave) {
		I915_WRITE(FP1(pipe), fp2);
		intel_crtc->lowfreq_avail = true;
	} else {
		I915_WRITE(FP1(pipe), fp);
	}
}

static void intel_update_lvds(struct drm_crtc *crtc, intel_clock_t *clock,
			      struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 temp;

	temp = I915_READ(LVDS);
	temp |= LVDS_PORT_EN | LVDS_A0A2_CLKA_POWER_UP;
	if (pipe == 1) {
		temp |= LVDS_PIPEB_SELECT;
	} else {
		temp &= ~LVDS_PIPEB_SELECT;
	}
	/* set the corresponsding LVDS_BORDER bit */
	temp |= dev_priv->lvds_border_bits;
	/* Set the B0-B3 data pairs corresponding to whether we're going to
	 * set the DPLLs for dual-channel mode or not.
	 */
	if (clock->p2 == 7)
		temp |= LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP;
	else
		temp &= ~(LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP);

	/* It would be nice to set 24 vs 18-bit mode (LVDS_A3_POWER_UP)
	 * appropriately here, but we need to look more thoroughly into how
	 * panels behave in the two modes.
	 */
	/* set the dithering flag on LVDS as needed */
	if (INTEL_INFO(dev)->gen >= 4) {
		if (dev_priv->lvds_dither)
			temp |= LVDS_ENABLE_DITHER;
		else
			temp &= ~LVDS_ENABLE_DITHER;
	}
	temp &= ~(LVDS_HSYNC_POLARITY | LVDS_VSYNC_POLARITY);
	if (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC)
		temp |= LVDS_HSYNC_POLARITY;
	if (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC)
		temp |= LVDS_VSYNC_POLARITY;
	I915_WRITE(LVDS, temp);
}

static void i9xx_update_pll(struct drm_crtc *crtc,
			    struct drm_display_mode *mode,
			    struct drm_display_mode *adjusted_mode,
			    intel_clock_t *clock, intel_clock_t *reduced_clock,
			    int num_connectors)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 dpll;
	bool is_sdvo;

	is_sdvo = intel_pipe_has_type(crtc, INTEL_OUTPUT_SDVO) ||
		intel_pipe_has_type(crtc, INTEL_OUTPUT_HDMI);

	dpll = DPLL_VGA_MODE_DIS;

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS))
		dpll |= DPLLB_MODE_LVDS;
	else
		dpll |= DPLLB_MODE_DAC_SERIAL;
	if (is_sdvo) {
		int pixel_multiplier = intel_mode_get_pixel_multiplier(adjusted_mode);
		if (pixel_multiplier > 1) {
			if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
				dpll |= (pixel_multiplier - 1) << SDVO_MULTIPLIER_SHIFT_HIRES;
		}
		dpll |= DPLL_DVO_HIGH_SPEED;
	}
	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_DISPLAYPORT))
		dpll |= DPLL_DVO_HIGH_SPEED;

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

	if (is_sdvo && intel_pipe_has_type(crtc, INTEL_OUTPUT_TVOUT))
		dpll |= PLL_REF_INPUT_TVCLKINBC;
	else if (intel_pipe_has_type(crtc, INTEL_OUTPUT_TVOUT))
		/* XXX: just matching BIOS for now */
		/*	dpll |= PLL_REF_INPUT_TVCLKINBC; */
		dpll |= 3;
	else if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS) &&
		 intel_panel_use_ssc(dev_priv) && num_connectors < 2)
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	dpll |= DPLL_VCO_ENABLE;
	I915_WRITE(DPLL(pipe), dpll & ~DPLL_VCO_ENABLE);
	POSTING_READ(DPLL(pipe));
	udelay(150);

	/* The LVDS pin pair needs to be on before the DPLLs are enabled.
	 * This is an exception to the general rule that mode_set doesn't turn
	 * things on.
	 */
	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS))
		intel_update_lvds(crtc, clock, adjusted_mode);

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_DISPLAYPORT))
		intel_dp_set_m_n(crtc, mode, adjusted_mode);

	I915_WRITE(DPLL(pipe), dpll);

	/* Wait for the clocks to stabilize. */
	POSTING_READ(DPLL(pipe));
	udelay(150);

	if (INTEL_INFO(dev)->gen >= 4) {
		u32 temp = 0;
		if (is_sdvo) {
			temp = intel_mode_get_pixel_multiplier(adjusted_mode);
			if (temp > 1)
				temp = (temp - 1) << DPLL_MD_UDI_MULTIPLIER_SHIFT;
			else
				temp = 0;
		}
		I915_WRITE(DPLL_MD(pipe), temp);
	} else {
		/* The pixel multiplier can only be updated once the
		 * DPLL is enabled and the clocks are stable.
		 *
		 * So write it again.
		 */
		I915_WRITE(DPLL(pipe), dpll);
	}
}

static void i8xx_update_pll(struct drm_crtc *crtc,
			    struct drm_display_mode *adjusted_mode,
			    intel_clock_t *clock,
			    int num_connectors)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 dpll;

	dpll = DPLL_VGA_MODE_DIS;

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
		dpll |= (1 << (clock->p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
	} else {
		if (clock->p1 == 2)
			dpll |= PLL_P1_DIVIDE_BY_TWO;
		else
			dpll |= (clock->p1 - 2) << DPLL_FPA01_P1_POST_DIV_SHIFT;
		if (clock->p2 == 4)
			dpll |= PLL_P2_DIVIDE_BY_4;
	}

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_TVOUT))
		/* XXX: just matching BIOS for now */
		/*	dpll |= PLL_REF_INPUT_TVCLKINBC; */
		dpll |= 3;
	else if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS) &&
		 intel_panel_use_ssc(dev_priv) && num_connectors < 2)
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	dpll |= DPLL_VCO_ENABLE;
	I915_WRITE(DPLL(pipe), dpll & ~DPLL_VCO_ENABLE);
	POSTING_READ(DPLL(pipe));
	udelay(150);

	I915_WRITE(DPLL(pipe), dpll);

	/* Wait for the clocks to stabilize. */
	POSTING_READ(DPLL(pipe));
	udelay(150);

	/* The LVDS pin pair needs to be on before the DPLLs are enabled.
	 * This is an exception to the general rule that mode_set doesn't turn
	 * things on.
	 */
	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS))
		intel_update_lvds(crtc, clock, adjusted_mode);

	/* The pixel multiplier can only be updated once the
	 * DPLL is enabled and the clocks are stable.
	 *
	 * So write it again.
	 */
	I915_WRITE(DPLL(pipe), dpll);
}

static int i9xx_crtc_mode_set(struct drm_crtc *crtc,
			      struct drm_display_mode *mode,
			      struct drm_display_mode *adjusted_mode,
			      int x, int y,
			      struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;
	int refclk, num_connectors = 0;
	intel_clock_t clock, reduced_clock;
	u32 dspcntr, pipeconf, vsyncshift;
	bool ok, has_reduced_clock = false, is_sdvo = false;
	bool is_lvds = false, is_tv = false, is_dp = false;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_encoder *encoder;
	const intel_limit_t *limit;
	int ret;

	list_for_each_entry(encoder, &mode_config->encoder_list, base.head) {
		if (encoder->base.crtc != crtc)
			continue;

		switch (encoder->type) {
		case INTEL_OUTPUT_LVDS:
			is_lvds = true;
			break;
		case INTEL_OUTPUT_SDVO:
		case INTEL_OUTPUT_HDMI:
			is_sdvo = true;
			if (encoder->needs_tv_clock)
				is_tv = true;
			break;
		case INTEL_OUTPUT_TVOUT:
			is_tv = true;
			break;
		case INTEL_OUTPUT_DISPLAYPORT:
			is_dp = true;
			break;
		}

		num_connectors++;
	}

	refclk = i9xx_get_refclk(crtc, num_connectors);

	/*
	 * Returns a set of divisors for the desired target clock with the given
	 * refclk, or FALSE.  The returned values represent the clock equation:
	 * reflck * (5 * (m1 + 2) + (m2 + 2)) / (n + 2) / p1 / p2.
	 */
	limit = intel_limit(crtc, refclk);
	ok = limit->find_pll(limit, crtc, adjusted_mode->clock, refclk, NULL,
			     &clock);
	if (!ok) {
		DRM_ERROR("Couldn't find PLL settings for mode!\n");
		return -EINVAL;
	}

	/* Ensure that the cursor is valid for the new mode before changing... */
	intel_crtc_update_cursor(crtc, true);

	if (is_lvds && dev_priv->lvds_downclock_avail) {
		/*
		 * Ensure we match the reduced clock's P to the target clock.
		 * If the clocks don't match, we can't switch the display clock
		 * by using the FP0/FP1. In such case we will disable the LVDS
		 * downclock feature.
		*/
		has_reduced_clock = limit->find_pll(limit, crtc,
						    dev_priv->lvds_downclock,
						    refclk,
						    &clock,
						    &reduced_clock);
	}

	if (is_sdvo && is_tv)
		i9xx_adjust_sdvo_tv_clock(adjusted_mode, &clock);

	i9xx_update_pll_dividers(crtc, &clock, has_reduced_clock ?
				 &reduced_clock : NULL);

	if (IS_GEN2(dev))
		i8xx_update_pll(crtc, adjusted_mode, &clock, num_connectors);
	else
		i9xx_update_pll(crtc, mode, adjusted_mode, &clock,
				has_reduced_clock ? &reduced_clock : NULL,
				num_connectors);

	/* setup pipeconf */
	pipeconf = I915_READ(PIPECONF(pipe));

	/* Set up the display plane register */
	dspcntr = DISPPLANE_GAMMA_ENABLE;

	if (pipe == 0)
		dspcntr &= ~DISPPLANE_SEL_PIPE_MASK;
	else
		dspcntr |= DISPPLANE_SEL_PIPE_B;

	if (pipe == 0 && INTEL_INFO(dev)->gen < 4) {
		/* Enable pixel doubling when the dot clock is > 90% of the (display)
		 * core speed.
		 *
		 * XXX: No double-wide on 915GM pipe B. Is that the only reason for the
		 * pipe == 0 check?
		 */
		if (mode->clock >
		    dev_priv->display.get_display_clock_speed(dev) * 9 / 10)
			pipeconf |= PIPECONF_DOUBLE_WIDE;
		else
			pipeconf &= ~PIPECONF_DOUBLE_WIDE;
	}

	/* default to 8bpc */
	pipeconf &= ~(PIPECONF_BPP_MASK | PIPECONF_DITHER_EN);
	if (is_dp) {
		if (mode->private_flags & INTEL_MODE_DP_FORCE_6BPC) {
			pipeconf |= PIPECONF_BPP_6 |
				    PIPECONF_DITHER_EN |
				    PIPECONF_DITHER_TYPE_SP;
		}
	}

	DRM_DEBUG_KMS("Mode for pipe %c:\n", pipe == 0 ? 'A' : 'B');
	drm_mode_debug_printmodeline(mode);

	if (HAS_PIPE_CXSR(dev)) {
		if (intel_crtc->lowfreq_avail) {
			DRM_DEBUG_KMS("enabling CxSR downclocking\n");
			pipeconf |= PIPECONF_CXSR_DOWNCLOCK;
		} else {
			DRM_DEBUG_KMS("disabling CxSR downclocking\n");
			pipeconf &= ~PIPECONF_CXSR_DOWNCLOCK;
		}
	}

	pipeconf &= ~PIPECONF_INTERLACE_MASK;
	if (!IS_GEN2(dev) &&
	    adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		pipeconf |= PIPECONF_INTERLACE_W_FIELD_INDICATION;
		/* the chip adds 2 halflines automatically */
		adjusted_mode->crtc_vtotal -= 1;
		adjusted_mode->crtc_vblank_end -= 1;
		vsyncshift = adjusted_mode->crtc_hsync_start
			     - adjusted_mode->crtc_htotal/2;
	} else {
		pipeconf |= PIPECONF_PROGRESSIVE;
		vsyncshift = 0;
	}

	if (!IS_GEN3(dev))
		I915_WRITE(VSYNCSHIFT(pipe), vsyncshift);

	I915_WRITE(HTOTAL(pipe),
		   (adjusted_mode->crtc_hdisplay - 1) |
		   ((adjusted_mode->crtc_htotal - 1) << 16));
	I915_WRITE(HBLANK(pipe),
		   (adjusted_mode->crtc_hblank_start - 1) |
		   ((adjusted_mode->crtc_hblank_end - 1) << 16));
	I915_WRITE(HSYNC(pipe),
		   (adjusted_mode->crtc_hsync_start - 1) |
		   ((adjusted_mode->crtc_hsync_end - 1) << 16));

	I915_WRITE(VTOTAL(pipe),
		   (adjusted_mode->crtc_vdisplay - 1) |
		   ((adjusted_mode->crtc_vtotal - 1) << 16));
	I915_WRITE(VBLANK(pipe),
		   (adjusted_mode->crtc_vblank_start - 1) |
		   ((adjusted_mode->crtc_vblank_end - 1) << 16));
	I915_WRITE(VSYNC(pipe),
		   (adjusted_mode->crtc_vsync_start - 1) |
		   ((adjusted_mode->crtc_vsync_end - 1) << 16));

	/* pipesrc and dspsize control the size that is scaled from,
	 * which should always be the user's requested size.
	 */
	I915_WRITE(DSPSIZE(plane),
		   ((mode->vdisplay - 1) << 16) |
		   (mode->hdisplay - 1));
	I915_WRITE(DSPPOS(plane), 0);
	I915_WRITE(PIPESRC(pipe),
		   ((mode->hdisplay - 1) << 16) | (mode->vdisplay - 1));

	I915_WRITE(PIPECONF(pipe), pipeconf);
	POSTING_READ(PIPECONF(pipe));
	intel_enable_pipe(dev_priv, pipe, false);

	intel_wait_for_vblank(dev, pipe);

	I915_WRITE(DSPCNTR(plane), dspcntr);
	POSTING_READ(DSPCNTR(plane));

	ret = intel_pipe_set_base(crtc, x, y, old_fb);

	intel_update_watermarks(dev);

	return ret;
}

/*
 * Initialize reference clocks when the driver loads
 */
void ironlake_init_pch_refclk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_encoder *encoder;
	u32 temp;
	bool has_lvds = false;
	bool has_cpu_edp = false;
	bool has_pch_edp = false;
	bool has_panel = false;
	bool has_ck505 = false;
	bool can_ssc = false;

	/* We need to take the global config into account */
	list_for_each_entry(encoder, &mode_config->encoder_list,
			    base.head) {
		switch (encoder->type) {
		case INTEL_OUTPUT_LVDS:
			has_panel = true;
			has_lvds = true;
			break;
		case INTEL_OUTPUT_EDP:
			has_panel = true;
			if (intel_encoder_is_pch_edp(&encoder->base))
				has_pch_edp = true;
			else
				has_cpu_edp = true;
			break;
		}
	}

	if (HAS_PCH_IBX(dev)) {
		has_ck505 = dev_priv->display_clock_mode;
		can_ssc = has_ck505;
	} else {
		has_ck505 = false;
		can_ssc = true;
	}

	DRM_DEBUG_KMS("has_panel %d has_lvds %d has_pch_edp %d has_cpu_edp %d has_ck505 %d\n",
		      has_panel, has_lvds, has_pch_edp, has_cpu_edp,
		      has_ck505);

	/* Ironlake: try to setup display ref clock before DPLL
	 * enabling. This is only under driver's control after
	 * PCH B stepping, previous chipset stepping should be
	 * ignoring this setting.
	 */
	temp = I915_READ(PCH_DREF_CONTROL);
	/* Always enable nonspread source */
	temp &= ~DREF_NONSPREAD_SOURCE_MASK;

	if (has_ck505)
		temp |= DREF_NONSPREAD_CK505_ENABLE;
	else
		temp |= DREF_NONSPREAD_SOURCE_ENABLE;

	if (has_panel) {
		temp &= ~DREF_SSC_SOURCE_MASK;
		temp |= DREF_SSC_SOURCE_ENABLE;

		/* SSC must be turned on before enabling the CPU output  */
		if (intel_panel_use_ssc(dev_priv) && can_ssc) {
			DRM_DEBUG_KMS("Using SSC on panel\n");
			temp |= DREF_SSC1_ENABLE;
		} else
			temp &= ~DREF_SSC1_ENABLE;

		/* Get SSC going before enabling the outputs */
		I915_WRITE(PCH_DREF_CONTROL, temp);
		POSTING_READ(PCH_DREF_CONTROL);
		udelay(200);

		temp &= ~DREF_CPU_SOURCE_OUTPUT_MASK;

		/* Enable CPU source on CPU attached eDP */
		if (has_cpu_edp) {
			if (intel_panel_use_ssc(dev_priv) && can_ssc) {
				DRM_DEBUG_KMS("Using SSC on eDP\n");
				temp |= DREF_CPU_SOURCE_OUTPUT_DOWNSPREAD;
			}
			else
				temp |= DREF_CPU_SOURCE_OUTPUT_NONSPREAD;
		} else
			temp |= DREF_CPU_SOURCE_OUTPUT_DISABLE;

		I915_WRITE(PCH_DREF_CONTROL, temp);
		POSTING_READ(PCH_DREF_CONTROL);
		udelay(200);
	} else {
		DRM_DEBUG_KMS("Disabling SSC entirely\n");

		temp &= ~DREF_CPU_SOURCE_OUTPUT_MASK;

		/* Turn off CPU output */
		temp |= DREF_CPU_SOURCE_OUTPUT_DISABLE;

		I915_WRITE(PCH_DREF_CONTROL, temp);
		POSTING_READ(PCH_DREF_CONTROL);
		udelay(200);

		/* Turn off the SSC source */
		temp &= ~DREF_SSC_SOURCE_MASK;
		temp |= DREF_SSC_SOURCE_DISABLE;

		/* Turn off SSC1 */
		temp &= ~ DREF_SSC1_ENABLE;

		I915_WRITE(PCH_DREF_CONTROL, temp);
		POSTING_READ(PCH_DREF_CONTROL);
		udelay(200);
	}
}

static int ironlake_get_refclk(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *encoder;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_encoder *edp_encoder = NULL;
	int num_connectors = 0;
	bool is_lvds = false;

	list_for_each_entry(encoder, &mode_config->encoder_list, base.head) {
		if (encoder->base.crtc != crtc)
			continue;

		switch (encoder->type) {
		case INTEL_OUTPUT_LVDS:
			is_lvds = true;
			break;
		case INTEL_OUTPUT_EDP:
			edp_encoder = encoder;
			break;
		}
		num_connectors++;
	}

	if (is_lvds && intel_panel_use_ssc(dev_priv) && num_connectors < 2) {
		DRM_DEBUG_KMS("using SSC reference clock of %d MHz\n",
			      dev_priv->lvds_ssc_freq);
		return dev_priv->lvds_ssc_freq * 1000;
	}

	return 120000;
}

static int ironlake_crtc_mode_set(struct drm_crtc *crtc,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode,
				  int x, int y,
				  struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;
	int refclk, num_connectors = 0;
	intel_clock_t clock, reduced_clock;
	u32 dpll, fp = 0, fp2 = 0, dspcntr, pipeconf;
	bool ok, has_reduced_clock = false, is_sdvo = false;
	bool is_crt = false, is_lvds = false, is_tv = false, is_dp = false;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_encoder *encoder, *edp_encoder = NULL;
	const intel_limit_t *limit;
	int ret;
	struct fdi_m_n m_n = {0};
	u32 temp;
	int target_clock, pixel_multiplier, lane, link_bw, factor;
	unsigned int pipe_bpp;
	bool dither;
	bool is_cpu_edp = false, is_pch_edp = false;

	list_for_each_entry(encoder, &mode_config->encoder_list, base.head) {
		if (encoder->base.crtc != crtc)
			continue;

		switch (encoder->type) {
		case INTEL_OUTPUT_LVDS:
			is_lvds = true;
			break;
		case INTEL_OUTPUT_SDVO:
		case INTEL_OUTPUT_HDMI:
			is_sdvo = true;
			if (encoder->needs_tv_clock)
				is_tv = true;
			break;
		case INTEL_OUTPUT_TVOUT:
			is_tv = true;
			break;
		case INTEL_OUTPUT_ANALOG:
			is_crt = true;
			break;
		case INTEL_OUTPUT_DISPLAYPORT:
			is_dp = true;
			break;
		case INTEL_OUTPUT_EDP:
			is_dp = true;
			if (intel_encoder_is_pch_edp(&encoder->base))
				is_pch_edp = true;
			else
				is_cpu_edp = true;
			edp_encoder = encoder;
			break;
		}

		num_connectors++;
	}

	refclk = ironlake_get_refclk(crtc);

	/*
	 * Returns a set of divisors for the desired target clock with the given
	 * refclk, or FALSE.  The returned values represent the clock equation:
	 * reflck * (5 * (m1 + 2) + (m2 + 2)) / (n + 2) / p1 / p2.
	 */
	limit = intel_limit(crtc, refclk);
	ok = limit->find_pll(limit, crtc, adjusted_mode->clock, refclk, NULL,
			     &clock);
	if (!ok) {
		DRM_ERROR("Couldn't find PLL settings for mode!\n");
		return -EINVAL;
	}

	/* Ensure that the cursor is valid for the new mode before changing... */
	intel_crtc_update_cursor(crtc, true);

	if (is_lvds && dev_priv->lvds_downclock_avail) {
		/*
		 * Ensure we match the reduced clock's P to the target clock.
		 * If the clocks don't match, we can't switch the display clock
		 * by using the FP0/FP1. In such case we will disable the LVDS
		 * downclock feature.
		*/
		has_reduced_clock = limit->find_pll(limit, crtc,
						    dev_priv->lvds_downclock,
						    refclk,
						    &clock,
						    &reduced_clock);
	}
	/* SDVO TV has fixed PLL values depend on its clock range,
	   this mirrors vbios setting. */
	if (is_sdvo && is_tv) {
		if (adjusted_mode->clock >= 100000
		    && adjusted_mode->clock < 140500) {
			clock.p1 = 2;
			clock.p2 = 10;
			clock.n = 3;
			clock.m1 = 16;
			clock.m2 = 8;
		} else if (adjusted_mode->clock >= 140500
			   && adjusted_mode->clock <= 200000) {
			clock.p1 = 1;
			clock.p2 = 10;
			clock.n = 6;
			clock.m1 = 12;
			clock.m2 = 8;
		}
	}

	/* FDI link */
	pixel_multiplier = intel_mode_get_pixel_multiplier(adjusted_mode);
	lane = 0;
	/* CPU eDP doesn't require FDI link, so just set DP M/N
	   according to current link config */
	if (is_cpu_edp) {
		target_clock = mode->clock;
		intel_edp_link_config(edp_encoder, &lane, &link_bw);
	} else {
		/* [e]DP over FDI requires target mode clock
		   instead of link clock */
		if (is_dp)
			target_clock = mode->clock;
		else
			target_clock = adjusted_mode->clock;

		/* FDI is a binary signal running at ~2.7GHz, encoding
		 * each output octet as 10 bits. The actual frequency
		 * is stored as a divider into a 100MHz clock, and the
		 * mode pixel clock is stored in units of 1KHz.
		 * Hence the bw of each lane in terms of the mode signal
		 * is:
		 */
		link_bw = intel_fdi_link_freq(dev) * MHz(100)/KHz(1)/10;
	}

	/* determine panel color depth */
	temp = I915_READ(PIPECONF(pipe));
	temp &= ~PIPE_BPC_MASK;
	dither = intel_choose_pipe_bpp_dither(crtc, &pipe_bpp, mode);
	switch (pipe_bpp) {
	case 18:
		temp |= PIPE_6BPC;
		break;
	case 24:
		temp |= PIPE_8BPC;
		break;
	case 30:
		temp |= PIPE_10BPC;
		break;
	case 36:
		temp |= PIPE_12BPC;
		break;
	default:
		WARN(1, "intel_choose_pipe_bpp returned invalid value %d\n",
			pipe_bpp);
		temp |= PIPE_8BPC;
		pipe_bpp = 24;
		break;
	}

	intel_crtc->bpp = pipe_bpp;
	I915_WRITE(PIPECONF(pipe), temp);

	if (!lane) {
		/*
		 * Account for spread spectrum to avoid
		 * oversubscribing the link. Max center spread
		 * is 2.5%; use 5% for safety's sake.
		 */
		u32 bps = target_clock * intel_crtc->bpp * 21 / 20;
		lane = bps / (link_bw * 8) + 1;
	}

	intel_crtc->fdi_lanes = lane;

	if (pixel_multiplier > 1)
		link_bw *= pixel_multiplier;
	ironlake_compute_m_n(intel_crtc->bpp, lane, target_clock, link_bw,
			     &m_n);

	fp = clock.n << 16 | clock.m1 << 8 | clock.m2;
	if (has_reduced_clock)
		fp2 = reduced_clock.n << 16 | reduced_clock.m1 << 8 |
			reduced_clock.m2;

	/* Enable autotuning of the PLL clock (if permissible) */
	factor = 21;
	if (is_lvds) {
		if ((intel_panel_use_ssc(dev_priv) &&
		     dev_priv->lvds_ssc_freq == 100) ||
		    (I915_READ(PCH_LVDS) & LVDS_CLKB_POWER_MASK) == LVDS_CLKB_POWER_UP)
			factor = 25;
	} else if (is_sdvo && is_tv)
		factor = 20;

	if (clock.m < factor * clock.n)
		fp |= FP_CB_TUNE;

	dpll = 0;

	if (is_lvds)
		dpll |= DPLLB_MODE_LVDS;
	else
		dpll |= DPLLB_MODE_DAC_SERIAL;
	if (is_sdvo) {
		int pixel_multiplier = intel_mode_get_pixel_multiplier(adjusted_mode);
		if (pixel_multiplier > 1) {
			dpll |= (pixel_multiplier - 1) << PLL_REF_SDVO_HDMI_MULTIPLIER_SHIFT;
		}
		dpll |= DPLL_DVO_HIGH_SPEED;
	}
	if (is_dp && !is_cpu_edp)
		dpll |= DPLL_DVO_HIGH_SPEED;

	/* compute bitmask from p1 value */
	dpll |= (1 << (clock.p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
	/* also FPA1 */
	dpll |= (1 << (clock.p1 - 1)) << DPLL_FPA1_P1_POST_DIV_SHIFT;

	switch (clock.p2) {
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

	if (is_sdvo && is_tv)
		dpll |= PLL_REF_INPUT_TVCLKINBC;
	else if (is_tv)
		/* XXX: just matching BIOS for now */
		/*	dpll |= PLL_REF_INPUT_TVCLKINBC; */
		dpll |= 3;
	else if (is_lvds && intel_panel_use_ssc(dev_priv) && num_connectors < 2)
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	/* setup pipeconf */
	pipeconf = I915_READ(PIPECONF(pipe));

	/* Set up the display plane register */
	dspcntr = DISPPLANE_GAMMA_ENABLE;

	DRM_DEBUG_KMS("Mode for pipe %d:\n", pipe);
	drm_mode_debug_printmodeline(mode);

	/* CPU eDP is the only output that doesn't need a PCH PLL of its own on
	 * pre-Haswell/LPT generation */
	if (HAS_PCH_LPT(dev)) {
		DRM_DEBUG_KMS("LPT detected: no PLL for pipe %d necessary\n",
				pipe);
	} else if (!is_cpu_edp) {
		struct intel_pch_pll *pll;

		pll = intel_get_pch_pll(intel_crtc, dpll, fp);
		if (pll == NULL) {
			DRM_DEBUG_DRIVER("failed to find PLL for pipe %d\n",
					 pipe);
			return -EINVAL;
		}
	} else
		intel_put_pch_pll(intel_crtc);

	/* The LVDS pin pair needs to be on before the DPLLs are enabled.
	 * This is an exception to the general rule that mode_set doesn't turn
	 * things on.
	 */
	if (is_lvds) {
		temp = I915_READ(PCH_LVDS);
		temp |= LVDS_PORT_EN | LVDS_A0A2_CLKA_POWER_UP;
		if (HAS_PCH_CPT(dev)) {
			temp &= ~PORT_TRANS_SEL_MASK;
			temp |= PORT_TRANS_SEL_CPT(pipe);
		} else {
			if (pipe == 1)
				temp |= LVDS_PIPEB_SELECT;
			else
				temp &= ~LVDS_PIPEB_SELECT;
		}

		/* set the corresponsding LVDS_BORDER bit */
		temp |= dev_priv->lvds_border_bits;
		/* Set the B0-B3 data pairs corresponding to whether we're going to
		 * set the DPLLs for dual-channel mode or not.
		 */
		if (clock.p2 == 7)
			temp |= LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP;
		else
			temp &= ~(LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP);

		/* It would be nice to set 24 vs 18-bit mode (LVDS_A3_POWER_UP)
		 * appropriately here, but we need to look more thoroughly into how
		 * panels behave in the two modes.
		 */
		temp &= ~(LVDS_HSYNC_POLARITY | LVDS_VSYNC_POLARITY);
		if (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC)
			temp |= LVDS_HSYNC_POLARITY;
		if (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC)
			temp |= LVDS_VSYNC_POLARITY;
		I915_WRITE(PCH_LVDS, temp);
	}

	pipeconf &= ~PIPECONF_DITHER_EN;
	pipeconf &= ~PIPECONF_DITHER_TYPE_MASK;
	if ((is_lvds && dev_priv->lvds_dither) || dither) {
		pipeconf |= PIPECONF_DITHER_EN;
		pipeconf |= PIPECONF_DITHER_TYPE_SP;
	}
	if (is_dp && !is_cpu_edp) {
		intel_dp_set_m_n(crtc, mode, adjusted_mode);
	} else {
		/* For non-DP output, clear any trans DP clock recovery setting.*/
		I915_WRITE(TRANSDATA_M1(pipe), 0);
		I915_WRITE(TRANSDATA_N1(pipe), 0);
		I915_WRITE(TRANSDPLINK_M1(pipe), 0);
		I915_WRITE(TRANSDPLINK_N1(pipe), 0);
	}

	if (intel_crtc->pch_pll) {
		I915_WRITE(intel_crtc->pch_pll->pll_reg, dpll);

		/* Wait for the clocks to stabilize. */
		POSTING_READ(intel_crtc->pch_pll->pll_reg);
		udelay(150);

		/* The pixel multiplier can only be updated once the
		 * DPLL is enabled and the clocks are stable.
		 *
		 * So write it again.
		 */
		I915_WRITE(intel_crtc->pch_pll->pll_reg, dpll);
	}

	intel_crtc->lowfreq_avail = false;
	if (intel_crtc->pch_pll) {
		if (is_lvds && has_reduced_clock && i915_powersave) {
			I915_WRITE(intel_crtc->pch_pll->fp1_reg, fp2);
			intel_crtc->lowfreq_avail = true;
			if (HAS_PIPE_CXSR(dev)) {
				DRM_DEBUG_KMS("enabling CxSR downclocking\n");
				pipeconf |= PIPECONF_CXSR_DOWNCLOCK;
			}
		} else {
			I915_WRITE(intel_crtc->pch_pll->fp1_reg, fp);
			if (HAS_PIPE_CXSR(dev)) {
				DRM_DEBUG_KMS("disabling CxSR downclocking\n");
				pipeconf &= ~PIPECONF_CXSR_DOWNCLOCK;
			}
		}
	}

	pipeconf &= ~PIPECONF_INTERLACE_MASK;
	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		pipeconf |= PIPECONF_INTERLACED_ILK;
		/* the chip adds 2 halflines automatically */
		adjusted_mode->crtc_vtotal -= 1;
		adjusted_mode->crtc_vblank_end -= 1;
		I915_WRITE(VSYNCSHIFT(pipe),
			   adjusted_mode->crtc_hsync_start
			   - adjusted_mode->crtc_htotal/2);
	} else {
		pipeconf |= PIPECONF_PROGRESSIVE;
		I915_WRITE(VSYNCSHIFT(pipe), 0);
	}

	I915_WRITE(HTOTAL(pipe),
		   (adjusted_mode->crtc_hdisplay - 1) |
		   ((adjusted_mode->crtc_htotal - 1) << 16));
	I915_WRITE(HBLANK(pipe),
		   (adjusted_mode->crtc_hblank_start - 1) |
		   ((adjusted_mode->crtc_hblank_end - 1) << 16));
	I915_WRITE(HSYNC(pipe),
		   (adjusted_mode->crtc_hsync_start - 1) |
		   ((adjusted_mode->crtc_hsync_end - 1) << 16));

	I915_WRITE(VTOTAL(pipe),
		   (adjusted_mode->crtc_vdisplay - 1) |
		   ((adjusted_mode->crtc_vtotal - 1) << 16));
	I915_WRITE(VBLANK(pipe),
		   (adjusted_mode->crtc_vblank_start - 1) |
		   ((adjusted_mode->crtc_vblank_end - 1) << 16));
	I915_WRITE(VSYNC(pipe),
		   (adjusted_mode->crtc_vsync_start - 1) |
		   ((adjusted_mode->crtc_vsync_end - 1) << 16));

	/* pipesrc controls the size that is scaled from, which should
	 * always be the user's requested size.
	 */
	I915_WRITE(PIPESRC(pipe),
		   ((mode->hdisplay - 1) << 16) | (mode->vdisplay - 1));

	I915_WRITE(PIPE_DATA_M1(pipe), TU_SIZE(m_n.tu) | m_n.gmch_m);
	I915_WRITE(PIPE_DATA_N1(pipe), m_n.gmch_n);
	I915_WRITE(PIPE_LINK_M1(pipe), m_n.link_m);
	I915_WRITE(PIPE_LINK_N1(pipe), m_n.link_n);

	if (is_cpu_edp)
		ironlake_set_pll_edp(crtc, adjusted_mode->clock);

	I915_WRITE(PIPECONF(pipe), pipeconf);
	POSTING_READ(PIPECONF(pipe));

	intel_wait_for_vblank(dev, pipe);

	I915_WRITE(DSPCNTR(plane), dspcntr);
	POSTING_READ(DSPCNTR(plane));

	ret = intel_pipe_set_base(crtc, x, y, old_fb);

	intel_update_watermarks(dev);

	return ret;
}

static int intel_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode,
			       int x, int y,
			       struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int ret;

	drm_vblank_pre_modeset(dev, pipe);

	ret = dev_priv->display.crtc_mode_set(crtc, mode, adjusted_mode,
					      x, y, old_fb);
	drm_vblank_post_modeset(dev, pipe);

	if (ret)
		intel_crtc->dpms_mode = DRM_MODE_DPMS_OFF;
	else
		intel_crtc->dpms_mode = DRM_MODE_DPMS_ON;

	return ret;
}

static bool intel_eld_uptodate(struct drm_connector *connector,
			       int reg_eldv, uint32_t bits_eldv,
			       int reg_elda, uint32_t bits_elda,
			       int reg_edid)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	uint8_t *eld = connector->eld;
	uint32_t i;

	i = I915_READ(reg_eldv);
	i &= bits_eldv;

	if (!eld[0])
		return !i;

	if (!i)
		return false;

	i = I915_READ(reg_elda);
	i &= ~bits_elda;
	I915_WRITE(reg_elda, i);

	for (i = 0; i < eld[2]; i++)
		if (I915_READ(reg_edid) != *((uint32_t *)eld + i))
			return false;

	return true;
}

static void g4x_write_eld(struct drm_connector *connector,
			  struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	uint8_t *eld = connector->eld;
	uint32_t eldv;
	uint32_t len;
	uint32_t i;

	i = I915_READ(G4X_AUD_VID_DID);

	if (i == INTEL_AUDIO_DEVBLC || i == INTEL_AUDIO_DEVCL)
		eldv = G4X_ELDV_DEVCL_DEVBLC;
	else
		eldv = G4X_ELDV_DEVCTG;

	if (intel_eld_uptodate(connector,
			       G4X_AUD_CNTL_ST, eldv,
			       G4X_AUD_CNTL_ST, G4X_ELD_ADDR,
			       G4X_HDMIW_HDMIEDID))
		return;

	i = I915_READ(G4X_AUD_CNTL_ST);
	i &= ~(eldv | G4X_ELD_ADDR);
	len = (i >> 9) & 0x1f;		/* ELD buffer size */
	I915_WRITE(G4X_AUD_CNTL_ST, i);

	if (!eld[0])
		return;

	len = min_t(uint8_t, eld[2], len);
	DRM_DEBUG_DRIVER("ELD size %d\n", len);
	for (i = 0; i < len; i++)
		I915_WRITE(G4X_HDMIW_HDMIEDID, *((uint32_t *)eld + i));

	i = I915_READ(G4X_AUD_CNTL_ST);
	i |= eldv;
	I915_WRITE(G4X_AUD_CNTL_ST, i);
}

static void ironlake_write_eld(struct drm_connector *connector,
				     struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	uint8_t *eld = connector->eld;
	uint32_t eldv;
	uint32_t i;
	int len;
	int hdmiw_hdmiedid;
	int aud_config;
	int aud_cntl_st;
	int aud_cntrl_st2;

	if (HAS_PCH_IBX(connector->dev)) {
		hdmiw_hdmiedid = IBX_HDMIW_HDMIEDID_A;
		aud_config = IBX_AUD_CONFIG_A;
		aud_cntl_st = IBX_AUD_CNTL_ST_A;
		aud_cntrl_st2 = IBX_AUD_CNTL_ST2;
	} else {
		hdmiw_hdmiedid = CPT_HDMIW_HDMIEDID_A;
		aud_config = CPT_AUD_CONFIG_A;
		aud_cntl_st = CPT_AUD_CNTL_ST_A;
		aud_cntrl_st2 = CPT_AUD_CNTRL_ST2;
	}

	i = to_intel_crtc(crtc)->pipe;
	hdmiw_hdmiedid += i * 0x100;
	aud_cntl_st += i * 0x100;
	aud_config += i * 0x100;

	DRM_DEBUG_DRIVER("ELD on pipe %c\n", pipe_name(i));

	i = I915_READ(aud_cntl_st);
	i = (i >> 29) & 0x3;		/* DIP_Port_Select, 0x1 = PortB */
	if (!i) {
		DRM_DEBUG_DRIVER("Audio directed to unknown port\n");
		/* operate blindly on all ports */
		eldv = IBX_ELD_VALIDB;
		eldv |= IBX_ELD_VALIDB << 4;
		eldv |= IBX_ELD_VALIDB << 8;
	} else {
		DRM_DEBUG_DRIVER("ELD on port %c\n", 'A' + i);
		eldv = IBX_ELD_VALIDB << ((i - 1) * 4);
	}

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_DISPLAYPORT)) {
		DRM_DEBUG_DRIVER("ELD: DisplayPort detected\n");
		eld[5] |= (1 << 2);	/* Conn_Type, 0x1 = DisplayPort */
		I915_WRITE(aud_config, AUD_CONFIG_N_VALUE_INDEX); /* 0x1 = DP */
	} else
		I915_WRITE(aud_config, 0);

	if (intel_eld_uptodate(connector,
			       aud_cntrl_st2, eldv,
			       aud_cntl_st, IBX_ELD_ADDRESS,
			       hdmiw_hdmiedid))
		return;

	i = I915_READ(aud_cntrl_st2);
	i &= ~eldv;
	I915_WRITE(aud_cntrl_st2, i);

	if (!eld[0])
		return;

	i = I915_READ(aud_cntl_st);
	i &= ~IBX_ELD_ADDRESS;
	I915_WRITE(aud_cntl_st, i);

	len = min_t(uint8_t, eld[2], 21);	/* 84 bytes of hw ELD buffer */
	DRM_DEBUG_DRIVER("ELD size %d\n", len);
	for (i = 0; i < len; i++)
		I915_WRITE(hdmiw_hdmiedid, *((uint32_t *)eld + i));

	i = I915_READ(aud_cntrl_st2);
	i |= eldv;
	I915_WRITE(aud_cntrl_st2, i);
}

void intel_write_eld(struct drm_encoder *encoder,
		     struct drm_display_mode *mode)
{
	struct drm_crtc *crtc = encoder->crtc;
	struct drm_connector *connector;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	connector = drm_select_eld(encoder, mode);
	if (!connector)
		return;

	DRM_DEBUG_DRIVER("ELD on [CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
			 connector->base.id,
			 drm_get_connector_name(connector),
			 connector->encoder->base.id,
			 drm_get_encoder_name(connector->encoder));

	connector->eld[6] = drm_av_sync_delay(connector, mode) / 2;

	if (dev_priv->display.write_eld)
		dev_priv->display.write_eld(connector, crtc);
}

/** Loads the palette/gamma unit for the CRTC with the prepared values */
void intel_crtc_load_lut(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int palreg = PALETTE(intel_crtc->pipe);
	int i;

	/* The clocks have to be on to load the palette. */
	if (!crtc->enabled || !intel_crtc->active)
		return;

	/* use legacy palette for Ironlake */
	if (HAS_PCH_SPLIT(dev))
		palreg = LGC_PALETTE(intel_crtc->pipe);

	for (i = 0; i < 256; i++) {
		I915_WRITE(palreg + 4 * i,
			   (intel_crtc->lut_r[i] << 16) |
			   (intel_crtc->lut_g[i] << 8) |
			   intel_crtc->lut_b[i]);
	}
}

static void i845_update_cursor(struct drm_crtc *crtc, u32 base)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	bool visible = base != 0;
	u32 cntl;

	if (intel_crtc->cursor_visible == visible)
		return;

	cntl = I915_READ(_CURACNTR);
	if (visible) {
		/* On these chipsets we can only modify the base whilst
		 * the cursor is disabled.
		 */
		I915_WRITE(_CURABASE, base);

		cntl &= ~(CURSOR_FORMAT_MASK);
		/* XXX width must be 64, stride 256 => 0x00 << 28 */
		cntl |= CURSOR_ENABLE |
			CURSOR_GAMMA_ENABLE |
			CURSOR_FORMAT_ARGB;
	} else
		cntl &= ~(CURSOR_ENABLE | CURSOR_GAMMA_ENABLE);
	I915_WRITE(_CURACNTR, cntl);

	intel_crtc->cursor_visible = visible;
}

static void i9xx_update_cursor(struct drm_crtc *crtc, u32 base)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	bool visible = base != 0;

	if (intel_crtc->cursor_visible != visible) {
		uint32_t cntl = I915_READ(CURCNTR(pipe));
		if (base) {
			cntl &= ~(CURSOR_MODE | MCURSOR_PIPE_SELECT);
			cntl |= CURSOR_MODE_64_ARGB_AX | MCURSOR_GAMMA_ENABLE;
			cntl |= pipe << 28; /* Connect to correct pipe */
		} else {
			cntl &= ~(CURSOR_MODE | MCURSOR_GAMMA_ENABLE);
			cntl |= CURSOR_MODE_DISABLE;
		}
		I915_WRITE(CURCNTR(pipe), cntl);

		intel_crtc->cursor_visible = visible;
	}
	/* and commit changes on next vblank */
	I915_WRITE(CURBASE(pipe), base);
}

static void ivb_update_cursor(struct drm_crtc *crtc, u32 base)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	bool visible = base != 0;

	if (intel_crtc->cursor_visible != visible) {
		uint32_t cntl = I915_READ(CURCNTR_IVB(pipe));
		if (base) {
			cntl &= ~CURSOR_MODE;
			cntl |= CURSOR_MODE_64_ARGB_AX | MCURSOR_GAMMA_ENABLE;
		} else {
			cntl &= ~(CURSOR_MODE | MCURSOR_GAMMA_ENABLE);
			cntl |= CURSOR_MODE_DISABLE;
		}
		I915_WRITE(CURCNTR_IVB(pipe), cntl);

		intel_crtc->cursor_visible = visible;
	}
	/* and commit changes on next vblank */
	I915_WRITE(CURBASE_IVB(pipe), base);
}

/* If no-part of the cursor is visible on the framebuffer, then the GPU may hang... */
static void intel_crtc_update_cursor(struct drm_crtc *crtc,
				     bool on)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int x = intel_crtc->cursor_x;
	int y = intel_crtc->cursor_y;
	u32 base, pos;
	bool visible;

	pos = 0;

	if (on && crtc->enabled && crtc->fb) {
		base = intel_crtc->cursor_addr;
		if (x > (int) crtc->fb->width)
			base = 0;

		if (y > (int) crtc->fb->height)
			base = 0;
	} else
		base = 0;

	if (x < 0) {
		if (x + intel_crtc->cursor_width < 0)
			base = 0;

		pos |= CURSOR_POS_SIGN << CURSOR_X_SHIFT;
		x = -x;
	}
	pos |= x << CURSOR_X_SHIFT;

	if (y < 0) {
		if (y + intel_crtc->cursor_height < 0)
			base = 0;

		pos |= CURSOR_POS_SIGN << CURSOR_Y_SHIFT;
		y = -y;
	}
	pos |= y << CURSOR_Y_SHIFT;

	visible = base != 0;
	if (!visible && !intel_crtc->cursor_visible)
		return;

	if (IS_IVYBRIDGE(dev) || IS_HASWELL(dev)) {
		I915_WRITE(CURPOS_IVB(pipe), pos);
		ivb_update_cursor(crtc, base);
	} else {
		I915_WRITE(CURPOS(pipe), pos);
		if (IS_845G(dev) || IS_I865G(dev))
			i845_update_cursor(crtc, base);
		else
			i9xx_update_cursor(crtc, base);
	}
}

static int intel_crtc_cursor_set(struct drm_crtc *crtc,
				 struct drm_file *file,
				 uint32_t handle,
				 uint32_t width, uint32_t height)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_gem_object *obj;
	uint32_t addr;
	int ret;

	DRM_DEBUG_KMS("\n");

	/* if we want to turn off the cursor ignore width and height */
	if (!handle) {
		DRM_DEBUG_KMS("cursor off\n");
		addr = 0;
		obj = NULL;
		mutex_lock(&dev->struct_mutex);
		goto finish;
	}

	/* Currently we only support 64x64 cursors */
	if (width != 64 || height != 64) {
		DRM_ERROR("we currently only support 64x64 cursors\n");
		return -EINVAL;
	}

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, handle));
	if (&obj->base == NULL)
		return -ENOENT;

	if (obj->base.size < width * height * 4) {
		DRM_ERROR("buffer is to small\n");
		ret = -ENOMEM;
		goto fail;
	}

	/* we only need to pin inside GTT if cursor is non-phy */
	mutex_lock(&dev->struct_mutex);
	if (!dev_priv->info->cursor_needs_physical) {
		if (obj->tiling_mode) {
			DRM_ERROR("cursor cannot be tiled\n");
			ret = -EINVAL;
			goto fail_locked;
		}

		ret = i915_gem_object_pin_to_display_plane(obj, 0, NULL);
		if (ret) {
			DRM_ERROR("failed to move cursor bo into the GTT\n");
			goto fail_locked;
		}

		ret = i915_gem_object_put_fence(obj);
		if (ret) {
			DRM_ERROR("failed to release fence for cursor");
			goto fail_unpin;
		}

		addr = obj->gtt_offset;
	} else {
		int align = IS_I830(dev) ? 16 * 1024 : 256;
		ret = i915_gem_attach_phys_object(dev, obj,
						  (intel_crtc->pipe == 0) ? I915_GEM_PHYS_CURSOR_0 : I915_GEM_PHYS_CURSOR_1,
						  align);
		if (ret) {
			DRM_ERROR("failed to attach phys object\n");
			goto fail_locked;
		}
		addr = obj->phys_obj->handle->busaddr;
	}

	if (IS_GEN2(dev))
		I915_WRITE(CURSIZE, (height << 12) | width);

 finish:
	if (intel_crtc->cursor_bo) {
		if (dev_priv->info->cursor_needs_physical) {
			if (intel_crtc->cursor_bo != obj)
				i915_gem_detach_phys_object(dev, intel_crtc->cursor_bo);
		} else
			i915_gem_object_unpin(intel_crtc->cursor_bo);
		drm_gem_object_unreference(&intel_crtc->cursor_bo->base);
	}

	mutex_unlock(&dev->struct_mutex);

	intel_crtc->cursor_addr = addr;
	intel_crtc->cursor_bo = obj;
	intel_crtc->cursor_width = width;
	intel_crtc->cursor_height = height;

	intel_crtc_update_cursor(crtc, true);

	return 0;
fail_unpin:
	i915_gem_object_unpin(obj);
fail_locked:
	mutex_unlock(&dev->struct_mutex);
fail:
	drm_gem_object_unreference_unlocked(&obj->base);
	return ret;
}

static int intel_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	intel_crtc->cursor_x = x;
	intel_crtc->cursor_y = y;

	intel_crtc_update_cursor(crtc, true);

	return 0;
}

/** Sets the color ramps on behalf of RandR */
void intel_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
				 u16 blue, int regno)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	intel_crtc->lut_r[regno] = red >> 8;
	intel_crtc->lut_g[regno] = green >> 8;
	intel_crtc->lut_b[regno] = blue >> 8;
}

void intel_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			     u16 *blue, int regno)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	*red = intel_crtc->lut_r[regno] << 8;
	*green = intel_crtc->lut_g[regno] << 8;
	*blue = intel_crtc->lut_b[regno] << 8;
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

/**
 * Get a pipe with a simple mode set on it for doing load-based monitor
 * detection.
 *
 * It will be up to the load-detect code to adjust the pipe as appropriate for
 * its requirements.  The pipe will be connected to no other encoders.
 *
 * Currently this code will only succeed if there is a pipe with no encoders
 * configured for it.  In the future, it could choose to temporarily disable
 * some outputs to free up a pipe for its use.
 *
 * \return crtc, or NULL if no pipes are available.
 */

/* VESA 640x480x72Hz mode to set on the pipe */
static struct drm_display_mode load_detect_mode = {
	DRM_MODE("640x480", DRM_MODE_TYPE_DEFAULT, 31500, 640, 664,
		 704, 832, 0, 480, 489, 491, 520, 0, DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
};

static struct drm_framebuffer *
intel_framebuffer_create(struct drm_device *dev,
			 struct drm_mode_fb_cmd2 *mode_cmd,
			 struct drm_i915_gem_object *obj)
{
	struct intel_framebuffer *intel_fb;
	int ret;

	intel_fb = kzalloc(sizeof(*intel_fb), GFP_KERNEL);
	if (!intel_fb) {
		drm_gem_object_unreference_unlocked(&obj->base);
		return ERR_PTR(-ENOMEM);
	}

	ret = intel_framebuffer_init(dev, intel_fb, mode_cmd, obj);
	if (ret) {
		drm_gem_object_unreference_unlocked(&obj->base);
		kfree(intel_fb);
		return ERR_PTR(ret);
	}

	return &intel_fb->base;
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
	return ALIGN(pitch * mode->vdisplay, PAGE_SIZE);
}

static struct drm_framebuffer *
intel_framebuffer_create_for_mode(struct drm_device *dev,
				  struct drm_display_mode *mode,
				  int depth, int bpp)
{
	struct drm_i915_gem_object *obj;
	struct drm_mode_fb_cmd2 mode_cmd;

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
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	struct drm_framebuffer *fb;

	if (dev_priv->fbdev == NULL)
		return NULL;

	obj = dev_priv->fbdev->ifb.obj;
	if (obj == NULL)
		return NULL;

	fb = &dev_priv->fbdev->ifb.base;
	if (fb->pitches[0] < intel_framebuffer_pitch_for_width(mode->hdisplay,
							       fb->bits_per_pixel))
		return NULL;

	if (obj->base.size < mode->vdisplay * fb->pitches[0])
		return NULL;

	return fb;
}

bool intel_get_load_detect_pipe(struct intel_encoder *intel_encoder,
				struct drm_connector *connector,
				struct drm_display_mode *mode,
				struct intel_load_detect_pipe *old)
{
	struct intel_crtc *intel_crtc;
	struct drm_crtc *possible_crtc;
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_crtc *crtc = NULL;
	struct drm_device *dev = encoder->dev;
	struct drm_framebuffer *old_fb;
	int i = -1;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
		      connector->base.id, drm_get_connector_name(connector),
		      encoder->base.id, drm_get_encoder_name(encoder));

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

		intel_crtc = to_intel_crtc(crtc);
		old->dpms_mode = intel_crtc->dpms_mode;
		old->load_detect_temp = false;

		/* Make sure the crtc and connector are running */
		if (intel_crtc->dpms_mode != DRM_MODE_DPMS_ON) {
			struct drm_encoder_helper_funcs *encoder_funcs;
			struct drm_crtc_helper_funcs *crtc_funcs;

			crtc_funcs = crtc->helper_private;
			crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);

			encoder_funcs = encoder->helper_private;
			encoder_funcs->dpms(encoder, DRM_MODE_DPMS_ON);
		}

		return true;
	}

	/* Find an unused one (if possible) */
	list_for_each_entry(possible_crtc, &dev->mode_config.crtc_list, head) {
		i++;
		if (!(encoder->possible_crtcs & (1 << i)))
			continue;
		if (!possible_crtc->enabled) {
			crtc = possible_crtc;
			break;
		}
	}

	/*
	 * If we didn't find an unused CRTC, don't use any.
	 */
	if (!crtc) {
		DRM_DEBUG_KMS("no pipe available for load-detect\n");
		return false;
	}

	encoder->crtc = crtc;
	connector->encoder = encoder;

	intel_crtc = to_intel_crtc(crtc);
	old->dpms_mode = intel_crtc->dpms_mode;
	old->load_detect_temp = true;
	old->release_fb = NULL;

	if (!mode)
		mode = &load_detect_mode;

	old_fb = crtc->fb;

	/* We need a framebuffer large enough to accommodate all accesses
	 * that the plane may generate whilst we perform load detection.
	 * We can not rely on the fbcon either being present (we get called
	 * during its initialisation to detect all boot displays, or it may
	 * not even exist) or that it is large enough to satisfy the
	 * requested mode.
	 */
	crtc->fb = mode_fits_in_fbdev(dev, mode);
	if (crtc->fb == NULL) {
		DRM_DEBUG_KMS("creating tmp fb for load-detection\n");
		crtc->fb = intel_framebuffer_create_for_mode(dev, mode, 24, 32);
		old->release_fb = crtc->fb;
	} else
		DRM_DEBUG_KMS("reusing fbdev for load-detection framebuffer\n");
	if (IS_ERR(crtc->fb)) {
		DRM_DEBUG_KMS("failed to allocate framebuffer for load-detection\n");
		crtc->fb = old_fb;
		return false;
	}

	if (!drm_crtc_helper_set_mode(crtc, mode, 0, 0, old_fb)) {
		DRM_DEBUG_KMS("failed to set mode on load-detect pipe\n");
		if (old->release_fb)
			old->release_fb->funcs->destroy(old->release_fb);
		crtc->fb = old_fb;
		return false;
	}

	/* let the connector get through one full cycle before testing */
	intel_wait_for_vblank(dev, intel_crtc->pipe);

	return true;
}

void intel_release_load_detect_pipe(struct intel_encoder *intel_encoder,
				    struct drm_connector *connector,
				    struct intel_load_detect_pipe *old)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_device *dev = encoder->dev;
	struct drm_crtc *crtc = encoder->crtc;
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
		      connector->base.id, drm_get_connector_name(connector),
		      encoder->base.id, drm_get_encoder_name(encoder));

	if (old->load_detect_temp) {
		connector->encoder = NULL;
		drm_helper_disable_unused_functions(dev);

		if (old->release_fb)
			old->release_fb->funcs->destroy(old->release_fb);

		return;
	}

	/* Switch crtc and encoder back off if necessary */
	if (old->dpms_mode != DRM_MODE_DPMS_ON) {
		encoder_funcs->dpms(encoder, old->dpms_mode);
		crtc_funcs->dpms(crtc, old->dpms_mode);
	}
}

/* Returns the clock of the currently programmed mode of the given pipe. */
static int intel_crtc_clock_get(struct drm_device *dev, struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 dpll = I915_READ(DPLL(pipe));
	u32 fp;
	intel_clock_t clock;

	if ((dpll & DISPLAY_RATE_SELECT_FPA1) == 0)
		fp = I915_READ(FP0(pipe));
	else
		fp = I915_READ(FP1(pipe));

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
			return 0;
		}

		/* XXX: Handle the 100Mhz refclk */
		intel_clock(dev, 96000, &clock);
	} else {
		bool is_lvds = (pipe == 1) && (I915_READ(LVDS) & LVDS_PORT_EN);

		if (is_lvds) {
			clock.p1 = ffs((dpll & DPLL_FPA01_P1_POST_DIV_MASK_I830_LVDS) >>
				       DPLL_FPA01_P1_POST_DIV_SHIFT);
			clock.p2 = 14;

			if ((dpll & PLL_REF_INPUT_MASK) ==
			    PLLB_REF_INPUT_SPREADSPECTRUMIN) {
				/* XXX: might not be 66MHz */
				intel_clock(dev, 66000, &clock);
			} else
				intel_clock(dev, 48000, &clock);
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

			intel_clock(dev, 48000, &clock);
		}
	}

	/* XXX: It would be nice to validate the clocks, but we can't reuse
	 * i830PllIsValid() because it relies on the xf86_config connector
	 * configuration being accurate, which it isn't necessarily.
	 */

	return clock.dot;
}

/** Returns the currently programmed mode of the given pipe. */
struct drm_display_mode *intel_crtc_mode_get(struct drm_device *dev,
					     struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	struct drm_display_mode *mode;
	int htot = I915_READ(HTOTAL(pipe));
	int hsync = I915_READ(HSYNC(pipe));
	int vtot = I915_READ(VTOTAL(pipe));
	int vsync = I915_READ(VSYNC(pipe));

	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode)
		return NULL;

	mode->clock = intel_crtc_clock_get(dev, crtc);
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

#define GPU_IDLE_TIMEOUT 500 /* ms */

/* When this timer fires, we've been idle for awhile */
static void intel_gpu_idle_timer(unsigned long arg)
{
	struct drm_device *dev = (struct drm_device *)arg;
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (!list_empty(&dev_priv->mm.active_list)) {
		/* Still processing requests, so just re-arm the timer. */
		mod_timer(&dev_priv->idle_timer, jiffies +
			  msecs_to_jiffies(GPU_IDLE_TIMEOUT));
		return;
	}

	dev_priv->busy = false;
	queue_work(dev_priv->wq, &dev_priv->idle_work);
}

#define CRTC_IDLE_TIMEOUT 1000 /* ms */

static void intel_crtc_idle_timer(unsigned long arg)
{
	struct intel_crtc *intel_crtc = (struct intel_crtc *)arg;
	struct drm_crtc *crtc = &intel_crtc->base;
	drm_i915_private_t *dev_priv = crtc->dev->dev_private;
	struct intel_framebuffer *intel_fb;

	intel_fb = to_intel_framebuffer(crtc->fb);
	if (intel_fb && intel_fb->obj->active) {
		/* The framebuffer is still being accessed by the GPU. */
		mod_timer(&intel_crtc->idle_timer, jiffies +
			  msecs_to_jiffies(CRTC_IDLE_TIMEOUT));
		return;
	}

	intel_crtc->busy = false;
	queue_work(dev_priv->wq, &dev_priv->idle_work);
}

static void intel_increase_pllclock(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int dpll_reg = DPLL(pipe);
	int dpll;

	if (HAS_PCH_SPLIT(dev))
		return;

	if (!dev_priv->lvds_downclock_avail)
		return;

	dpll = I915_READ(dpll_reg);
	if (!HAS_PIPE_CXSR(dev) && (dpll & DISPLAY_RATE_SELECT_FPA1)) {
		DRM_DEBUG_DRIVER("upclocking LVDS\n");

		assert_panel_unlocked(dev_priv, pipe);

		dpll &= ~DISPLAY_RATE_SELECT_FPA1;
		I915_WRITE(dpll_reg, dpll);
		intel_wait_for_vblank(dev, pipe);

		dpll = I915_READ(dpll_reg);
		if (dpll & DISPLAY_RATE_SELECT_FPA1)
			DRM_DEBUG_DRIVER("failed to upclock LVDS!\n");
	}

	/* Schedule downclock */
	mod_timer(&intel_crtc->idle_timer, jiffies +
		  msecs_to_jiffies(CRTC_IDLE_TIMEOUT));
}

static void intel_decrease_pllclock(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	if (HAS_PCH_SPLIT(dev))
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

/**
 * intel_idle_update - adjust clocks for idleness
 * @work: work struct
 *
 * Either the GPU or display (or both) went idle.  Check the busy status
 * here and adjust the CRTC and GPU clocks as necessary.
 */
static void intel_idle_update(struct work_struct *work)
{
	drm_i915_private_t *dev_priv = container_of(work, drm_i915_private_t,
						    idle_work);
	struct drm_device *dev = dev_priv->dev;
	struct drm_crtc *crtc;
	struct intel_crtc *intel_crtc;

	if (!i915_powersave)
		return;

	mutex_lock(&dev->struct_mutex);

	i915_update_gfx_val(dev_priv);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		/* Skip inactive CRTCs */
		if (!crtc->fb)
			continue;

		intel_crtc = to_intel_crtc(crtc);
		if (!intel_crtc->busy)
			intel_decrease_pllclock(crtc);
	}


	mutex_unlock(&dev->struct_mutex);
}

/**
 * intel_mark_busy - mark the GPU and possibly the display busy
 * @dev: drm device
 * @obj: object we're operating on
 *
 * Callers can use this function to indicate that the GPU is busy processing
 * commands.  If @obj matches one of the CRTC objects (i.e. it's a scanout
 * buffer), we'll also mark the display as busy, so we know to increase its
 * clock frequency.
 */
void intel_mark_busy(struct drm_device *dev, struct drm_i915_gem_object *obj)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = NULL;
	struct intel_framebuffer *intel_fb;
	struct intel_crtc *intel_crtc;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	if (!dev_priv->busy) {
		intel_sanitize_pm(dev);
		dev_priv->busy = true;
	} else
		mod_timer(&dev_priv->idle_timer, jiffies +
			  msecs_to_jiffies(GPU_IDLE_TIMEOUT));

	if (obj == NULL)
		return;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (!crtc->fb)
			continue;

		intel_crtc = to_intel_crtc(crtc);
		intel_fb = to_intel_framebuffer(crtc->fb);
		if (intel_fb->obj == obj) {
			if (!intel_crtc->busy) {
				/* Non-busy -> busy, upclock */
				intel_increase_pllclock(crtc);
				intel_crtc->busy = true;
			} else {
				/* Busy -> busy, put off timer */
				mod_timer(&intel_crtc->idle_timer, jiffies +
					  msecs_to_jiffies(CRTC_IDLE_TIMEOUT));
			}
		}
	}
}

static void intel_crtc_destroy(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct intel_unpin_work *work;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	work = intel_crtc->unpin_work;
	intel_crtc->unpin_work = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

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

	mutex_lock(&work->dev->struct_mutex);
	intel_unpin_fb_obj(work->old_fb_obj);
	drm_gem_object_unreference(&work->pending_flip_obj->base);
	drm_gem_object_unreference(&work->old_fb_obj->base);

	intel_update_fbc(work->dev);
	mutex_unlock(&work->dev->struct_mutex);
	kfree(work);
}

static void do_intel_finish_page_flip(struct drm_device *dev,
				      struct drm_crtc *crtc)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_unpin_work *work;
	struct drm_i915_gem_object *obj;
	struct drm_pending_vblank_event *e;
	struct timeval tnow, tvbl;
	unsigned long flags;

	/* Ignore early vblank irqs */
	if (intel_crtc == NULL)
		return;

	do_gettimeofday(&tnow);

	spin_lock_irqsave(&dev->event_lock, flags);
	work = intel_crtc->unpin_work;
	if (work == NULL || !work->pending) {
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return;
	}

	intel_crtc->unpin_work = NULL;

	if (work->event) {
		e = work->event;
		e->event.sequence = drm_vblank_count_and_time(dev, intel_crtc->pipe, &tvbl);

		/* Called before vblank count and timestamps have
		 * been updated for the vblank interval of flip
		 * completion? Need to increment vblank count and
		 * add one videorefresh duration to returned timestamp
		 * to account for this. We assume this happened if we
		 * get called over 0.9 frame durations after the last
		 * timestamped vblank.
		 *
		 * This calculation can not be used with vrefresh rates
		 * below 5Hz (10Hz to be on the safe side) without
		 * promoting to 64 integers.
		 */
		if (10 * (timeval_to_ns(&tnow) - timeval_to_ns(&tvbl)) >
		    9 * crtc->framedur_ns) {
			e->event.sequence++;
			tvbl = ns_to_timeval(timeval_to_ns(&tvbl) +
					     crtc->framedur_ns);
		}

		e->event.tv_sec = tvbl.tv_sec;
		e->event.tv_usec = tvbl.tv_usec;

		list_add_tail(&e->base.link,
			      &e->base.file_priv->event_list);
		wake_up_interruptible(&e->base.file_priv->event_wait);
	}

	drm_vblank_put(dev, intel_crtc->pipe);

	spin_unlock_irqrestore(&dev->event_lock, flags);

	obj = work->old_fb_obj;

	atomic_clear_mask(1 << intel_crtc->plane,
			  &obj->pending_flip.counter);
	if (atomic_read(&obj->pending_flip) == 0)
		wake_up(&dev_priv->pending_flip_queue);

	schedule_work(&work->work);

	trace_i915_flip_complete(intel_crtc->plane, work->pending_flip_obj);
}

void intel_finish_page_flip(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];

	do_intel_finish_page_flip(dev, crtc);
}

void intel_finish_page_flip_plane(struct drm_device *dev, int plane)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dev_priv->plane_to_crtc_mapping[plane];

	do_intel_finish_page_flip(dev, crtc);
}

void intel_prepare_page_flip(struct drm_device *dev, int plane)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc =
		to_intel_crtc(dev_priv->plane_to_crtc_mapping[plane]);
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (intel_crtc->unpin_work) {
		if ((++intel_crtc->unpin_work->pending) > 1)
			DRM_ERROR("Prepared flip multiple times\n");
	} else {
		DRM_DEBUG_DRIVER("preparing flip with no unpin work?\n");
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static int intel_gen2_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	unsigned long offset;
	u32 flip_mask;
	struct intel_ring_buffer *ring = &dev_priv->ring[RCS];
	int ret;

	ret = intel_pin_and_fence_fb_obj(dev, obj, ring);
	if (ret)
		goto err;

	/* Offset into the new buffer for cases of shared fbs between CRTCs */
	offset = crtc->y * fb->pitches[0] + crtc->x * fb->bits_per_pixel/8;

	ret = intel_ring_begin(ring, 6);
	if (ret)
		goto err_unpin;

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
	intel_ring_emit(ring, obj->gtt_offset + offset);
	intel_ring_emit(ring, 0); /* aux display base address, unused */
	intel_ring_advance(ring);
	return 0;

err_unpin:
	intel_unpin_fb_obj(obj);
err:
	return ret;
}

static int intel_gen3_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	unsigned long offset;
	u32 flip_mask;
	struct intel_ring_buffer *ring = &dev_priv->ring[RCS];
	int ret;

	ret = intel_pin_and_fence_fb_obj(dev, obj, ring);
	if (ret)
		goto err;

	/* Offset into the new buffer for cases of shared fbs between CRTCs */
	offset = crtc->y * fb->pitches[0] + crtc->x * fb->bits_per_pixel/8;

	ret = intel_ring_begin(ring, 6);
	if (ret)
		goto err_unpin;

	if (intel_crtc->plane)
		flip_mask = MI_WAIT_FOR_PLANE_B_FLIP;
	else
		flip_mask = MI_WAIT_FOR_PLANE_A_FLIP;
	intel_ring_emit(ring, MI_WAIT_FOR_EVENT | flip_mask);
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_emit(ring, MI_DISPLAY_FLIP_I915 |
			MI_DISPLAY_FLIP_PLANE(intel_crtc->plane));
	intel_ring_emit(ring, fb->pitches[0]);
	intel_ring_emit(ring, obj->gtt_offset + offset);
	intel_ring_emit(ring, MI_NOOP);

	intel_ring_advance(ring);
	return 0;

err_unpin:
	intel_unpin_fb_obj(obj);
err:
	return ret;
}

static int intel_gen4_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	uint32_t pf, pipesrc;
	struct intel_ring_buffer *ring = &dev_priv->ring[RCS];
	int ret;

	ret = intel_pin_and_fence_fb_obj(dev, obj, ring);
	if (ret)
		goto err;

	ret = intel_ring_begin(ring, 4);
	if (ret)
		goto err_unpin;

	/* i965+ uses the linear or tiled offsets from the
	 * Display Registers (which do not change across a page-flip)
	 * so we need only reprogram the base address.
	 */
	intel_ring_emit(ring, MI_DISPLAY_FLIP |
			MI_DISPLAY_FLIP_PLANE(intel_crtc->plane));
	intel_ring_emit(ring, fb->pitches[0]);
	intel_ring_emit(ring, obj->gtt_offset | obj->tiling_mode);

	/* XXX Enabling the panel-fitter across page-flip is so far
	 * untested on non-native modes, so ignore it for now.
	 * pf = I915_READ(pipe == 0 ? PFA_CTL_1 : PFB_CTL_1) & PF_ENABLE;
	 */
	pf = 0;
	pipesrc = I915_READ(PIPESRC(intel_crtc->pipe)) & 0x0fff0fff;
	intel_ring_emit(ring, pf | pipesrc);
	intel_ring_advance(ring);
	return 0;

err_unpin:
	intel_unpin_fb_obj(obj);
err:
	return ret;
}

static int intel_gen6_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_ring_buffer *ring = &dev_priv->ring[RCS];
	uint32_t pf, pipesrc;
	int ret;

	ret = intel_pin_and_fence_fb_obj(dev, obj, ring);
	if (ret)
		goto err;

	ret = intel_ring_begin(ring, 4);
	if (ret)
		goto err_unpin;

	intel_ring_emit(ring, MI_DISPLAY_FLIP |
			MI_DISPLAY_FLIP_PLANE(intel_crtc->plane));
	intel_ring_emit(ring, fb->pitches[0] | obj->tiling_mode);
	intel_ring_emit(ring, obj->gtt_offset);

	/* Contrary to the suggestions in the documentation,
	 * "Enable Panel Fitter" does not seem to be required when page
	 * flipping with a non-native mode, and worse causes a normal
	 * modeset to fail.
	 * pf = I915_READ(PF_CTL(intel_crtc->pipe)) & PF_ENABLE;
	 */
	pf = 0;
	pipesrc = I915_READ(PIPESRC(intel_crtc->pipe)) & 0x0fff0fff;
	intel_ring_emit(ring, pf | pipesrc);
	intel_ring_advance(ring);
	return 0;

err_unpin:
	intel_unpin_fb_obj(obj);
err:
	return ret;
}

/*
 * On gen7 we currently use the blit ring because (in early silicon at least)
 * the render ring doesn't give us interrpts for page flip completion, which
 * means clients will hang after the first flip is queued.  Fortunately the
 * blit ring generates interrupts properly, so use it instead.
 */
static int intel_gen7_queue_flip(struct drm_device *dev,
				 struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_ring_buffer *ring = &dev_priv->ring[BCS];
	int ret;

	ret = intel_pin_and_fence_fb_obj(dev, obj, ring);
	if (ret)
		goto err;

	ret = intel_ring_begin(ring, 4);
	if (ret)
		goto err_unpin;

	intel_ring_emit(ring, MI_DISPLAY_FLIP_I915 | (intel_crtc->plane << 19));
	intel_ring_emit(ring, (fb->pitches[0] | obj->tiling_mode));
	intel_ring_emit(ring, (obj->gtt_offset));
	intel_ring_emit(ring, (MI_NOOP));
	intel_ring_advance(ring);
	return 0;

err_unpin:
	intel_unpin_fb_obj(obj);
err:
	return ret;
}

static int intel_default_queue_flip(struct drm_device *dev,
				    struct drm_crtc *crtc,
				    struct drm_framebuffer *fb,
				    struct drm_i915_gem_object *obj)
{
	return -ENODEV;
}

static int intel_crtc_page_flip(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				struct drm_pending_vblank_event *event)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_framebuffer *intel_fb;
	struct drm_i915_gem_object *obj;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_unpin_work *work;
	unsigned long flags;
	int ret;

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (work == NULL)
		return -ENOMEM;

	work->event = event;
	work->dev = crtc->dev;
	intel_fb = to_intel_framebuffer(crtc->fb);
	work->old_fb_obj = intel_fb->obj;
	INIT_WORK(&work->work, intel_unpin_work_fn);

	ret = drm_vblank_get(dev, intel_crtc->pipe);
	if (ret)
		goto free_work;

	/* We borrow the event spin lock for protecting unpin_work */
	spin_lock_irqsave(&dev->event_lock, flags);
	if (intel_crtc->unpin_work) {
		spin_unlock_irqrestore(&dev->event_lock, flags);
		kfree(work);
		drm_vblank_put(dev, intel_crtc->pipe);

		DRM_DEBUG_DRIVER("flip queue: crtc already busy\n");
		return -EBUSY;
	}
	intel_crtc->unpin_work = work;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	intel_fb = to_intel_framebuffer(fb);
	obj = intel_fb->obj;

	mutex_lock(&dev->struct_mutex);

	/* Reference the objects for the scheduled work. */
	drm_gem_object_reference(&work->old_fb_obj->base);
	drm_gem_object_reference(&obj->base);

	crtc->fb = fb;

	work->pending_flip_obj = obj;

	work->enable_stall_check = true;

	/* Block clients from rendering to the new back buffer until
	 * the flip occurs and the object is no longer visible.
	 */
	atomic_add(1 << intel_crtc->plane, &work->old_fb_obj->pending_flip);

	ret = dev_priv->display.queue_flip(dev, crtc, fb, obj);
	if (ret)
		goto cleanup_pending;

	intel_disable_fbc(dev);
	intel_mark_busy(dev, obj);
	mutex_unlock(&dev->struct_mutex);

	trace_i915_flip_request(intel_crtc->plane, obj);

	return 0;

cleanup_pending:
	atomic_sub(1 << intel_crtc->plane, &work->old_fb_obj->pending_flip);
	drm_gem_object_unreference(&work->old_fb_obj->base);
	drm_gem_object_unreference(&obj->base);
	mutex_unlock(&dev->struct_mutex);

	spin_lock_irqsave(&dev->event_lock, flags);
	intel_crtc->unpin_work = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	drm_vblank_put(dev, intel_crtc->pipe);
free_work:
	kfree(work);

	return ret;
}

static void intel_sanitize_modesetting(struct drm_device *dev,
				       int pipe, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg, val;

	/* Clear any frame start delays used for debugging left by the BIOS */
	for_each_pipe(pipe) {
		reg = PIPECONF(pipe);
		I915_WRITE(reg, I915_READ(reg) & ~PIPECONF_FRAME_START_DELAY_MASK);
	}

	if (HAS_PCH_SPLIT(dev))
		return;

	/* Who knows what state these registers were left in by the BIOS or
	 * grub?
	 *
	 * If we leave the registers in a conflicting state (e.g. with the
	 * display plane reading from the other pipe than the one we intend
	 * to use) then when we attempt to teardown the active mode, we will
	 * not disable the pipes and planes in the correct order -- leaving
	 * a plane reading from a disabled pipe and possibly leading to
	 * undefined behaviour.
	 */

	reg = DSPCNTR(plane);
	val = I915_READ(reg);

	if ((val & DISPLAY_PLANE_ENABLE) == 0)
		return;
	if (!!(val & DISPPLANE_SEL_PIPE_MASK) == pipe)
		return;

	/* This display plane is active and attached to the other CPU pipe. */
	pipe = !pipe;

	/* Disable the plane and wait for it to stop reading from the pipe. */
	intel_disable_plane(dev_priv, plane, pipe);
	intel_disable_pipe(dev_priv, pipe);
}

static void intel_crtc_reset(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	/* Reset flags back to the 'unknown' status so that they
	 * will be correctly set on the initial modeset.
	 */
	intel_crtc->dpms_mode = -1;

	/* We need to fix up any BIOS configuration that conflicts with
	 * our expectations.
	 */
	intel_sanitize_modesetting(dev, intel_crtc->pipe, intel_crtc->plane);
}

static struct drm_crtc_helper_funcs intel_helper_funcs = {
	.dpms = intel_crtc_dpms,
	.mode_fixup = intel_crtc_mode_fixup,
	.mode_set = intel_crtc_mode_set,
	.mode_set_base = intel_pipe_set_base,
	.mode_set_base_atomic = intel_pipe_set_base_atomic,
	.load_lut = intel_crtc_load_lut,
	.disable = intel_crtc_disable,
};

static const struct drm_crtc_funcs intel_crtc_funcs = {
	.reset = intel_crtc_reset,
	.cursor_set = intel_crtc_cursor_set,
	.cursor_move = intel_crtc_cursor_move,
	.gamma_set = intel_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = intel_crtc_destroy,
	.page_flip = intel_crtc_page_flip,
};

static void intel_pch_pll_init(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;

	if (dev_priv->num_pch_pll == 0) {
		DRM_DEBUG_KMS("No PCH PLLs on this hardware, skipping initialisation\n");
		return;
	}

	for (i = 0; i < dev_priv->num_pch_pll; i++) {
		dev_priv->pch_plls[i].pll_reg = _PCH_DPLL(i);
		dev_priv->pch_plls[i].fp0_reg = _PCH_FP0(i);
		dev_priv->pch_plls[i].fp1_reg = _PCH_FP1(i);
	}
}

static void intel_crtc_init(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc;
	int i;

	intel_crtc = kzalloc(sizeof(struct intel_crtc) + (INTELFB_CONN_LIMIT * sizeof(struct drm_connector *)), GFP_KERNEL);
	if (intel_crtc == NULL)
		return;

	drm_crtc_init(dev, &intel_crtc->base, &intel_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&intel_crtc->base, 256);
	for (i = 0; i < 256; i++) {
		intel_crtc->lut_r[i] = i;
		intel_crtc->lut_g[i] = i;
		intel_crtc->lut_b[i] = i;
	}

	/* Swap pipes & planes for FBC on pre-965 */
	intel_crtc->pipe = pipe;
	intel_crtc->plane = pipe;
	if (IS_MOBILE(dev) && IS_GEN3(dev)) {
		DRM_DEBUG_KMS("swapping pipes & planes for FBC\n");
		intel_crtc->plane = !pipe;
	}

	BUG_ON(pipe >= ARRAY_SIZE(dev_priv->plane_to_crtc_mapping) ||
	       dev_priv->plane_to_crtc_mapping[intel_crtc->plane] != NULL);
	dev_priv->plane_to_crtc_mapping[intel_crtc->plane] = &intel_crtc->base;
	dev_priv->pipe_to_crtc_mapping[intel_crtc->pipe] = &intel_crtc->base;

	intel_crtc_reset(&intel_crtc->base);
	intel_crtc->active = true; /* force the pipe off on setup_init_config */
	intel_crtc->bpp = 24; /* default for pre-Ironlake */

	if (HAS_PCH_SPLIT(dev)) {
		intel_helper_funcs.prepare = ironlake_crtc_prepare;
		intel_helper_funcs.commit = ironlake_crtc_commit;
	} else {
		intel_helper_funcs.prepare = i9xx_crtc_prepare;
		intel_helper_funcs.commit = i9xx_crtc_commit;
	}

	drm_crtc_helper_add(&intel_crtc->base, &intel_helper_funcs);

	intel_crtc->busy = false;

	setup_timer(&intel_crtc->idle_timer, intel_crtc_idle_timer,
		    (unsigned long)intel_crtc);
}

int intel_get_pipe_from_crtc_id(struct drm_device *dev, void *data,
				struct drm_file *file)
{
	struct drm_i915_get_pipe_from_crtc_id *pipe_from_crtc_id = data;
	struct drm_mode_object *drmmode_obj;
	struct intel_crtc *crtc;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	drmmode_obj = drm_mode_object_find(dev, pipe_from_crtc_id->crtc_id,
			DRM_MODE_OBJECT_CRTC);

	if (!drmmode_obj) {
		DRM_ERROR("no such CRTC id\n");
		return -EINVAL;
	}

	crtc = to_intel_crtc(obj_to_crtc(drmmode_obj));
	pipe_from_crtc_id->pipe = crtc->pipe;

	return 0;
}

static int intel_encoder_clones(struct drm_device *dev, int type_mask)
{
	struct intel_encoder *encoder;
	int index_mask = 0;
	int entry = 0;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, base.head) {
		if (type_mask & encoder->clone_mask)
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

	if (IS_GEN5(dev) &&
	    (I915_READ(ILK_DISPLAY_CHICKEN_FUSES) & ILK_eDP_A_DISABLE))
		return false;

	return true;
}

static void intel_setup_outputs(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *encoder;
	bool dpd_is_edp = false;
	bool has_lvds;

	has_lvds = intel_lvds_init(dev);
	if (!has_lvds && !HAS_PCH_SPLIT(dev)) {
		/* disable the panel fitter on everything but LVDS */
		I915_WRITE(PFIT_CONTROL, 0);
	}

	if (HAS_PCH_SPLIT(dev)) {
		dpd_is_edp = intel_dpd_is_edp(dev);

		if (has_edp_a(dev))
			intel_dp_init(dev, DP_A);

		if (dpd_is_edp && (I915_READ(PCH_DP_D) & DP_DETECTED))
			intel_dp_init(dev, PCH_DP_D);
	}

	intel_crt_init(dev);

	if (HAS_PCH_SPLIT(dev)) {
		int found;

		if (I915_READ(HDMIB) & PORT_DETECTED) {
			/* PCH SDVOB multiplex with HDMIB */
			found = intel_sdvo_init(dev, PCH_SDVOB, true);
			if (!found)
				intel_hdmi_init(dev, HDMIB);
			if (!found && (I915_READ(PCH_DP_B) & DP_DETECTED))
				intel_dp_init(dev, PCH_DP_B);
		}

		if (I915_READ(HDMIC) & PORT_DETECTED)
			intel_hdmi_init(dev, HDMIC);

		if (I915_READ(HDMID) & PORT_DETECTED)
			intel_hdmi_init(dev, HDMID);

		if (I915_READ(PCH_DP_C) & DP_DETECTED)
			intel_dp_init(dev, PCH_DP_C);

		if (!dpd_is_edp && (I915_READ(PCH_DP_D) & DP_DETECTED))
			intel_dp_init(dev, PCH_DP_D);

	} else if (SUPPORTS_DIGITAL_OUTPUTS(dev)) {
		bool found = false;

		if (I915_READ(SDVOB) & SDVO_DETECTED) {
			DRM_DEBUG_KMS("probing SDVOB\n");
			found = intel_sdvo_init(dev, SDVOB, true);
			if (!found && SUPPORTS_INTEGRATED_HDMI(dev)) {
				DRM_DEBUG_KMS("probing HDMI on SDVOB\n");
				intel_hdmi_init(dev, SDVOB);
			}

			if (!found && SUPPORTS_INTEGRATED_DP(dev)) {
				DRM_DEBUG_KMS("probing DP_B\n");
				intel_dp_init(dev, DP_B);
			}
		}

		/* Before G4X SDVOC doesn't have its own detect register */

		if (I915_READ(SDVOB) & SDVO_DETECTED) {
			DRM_DEBUG_KMS("probing SDVOC\n");
			found = intel_sdvo_init(dev, SDVOC, false);
		}

		if (!found && (I915_READ(SDVOC) & SDVO_DETECTED)) {

			if (SUPPORTS_INTEGRATED_HDMI(dev)) {
				DRM_DEBUG_KMS("probing HDMI on SDVOC\n");
				intel_hdmi_init(dev, SDVOC);
			}
			if (SUPPORTS_INTEGRATED_DP(dev)) {
				DRM_DEBUG_KMS("probing DP_C\n");
				intel_dp_init(dev, DP_C);
			}
		}

		if (SUPPORTS_INTEGRATED_DP(dev) &&
		    (I915_READ(DP_D) & DP_DETECTED)) {
			DRM_DEBUG_KMS("probing DP_D\n");
			intel_dp_init(dev, DP_D);
		}
	} else if (IS_GEN2(dev))
		intel_dvo_init(dev);

	if (SUPPORTS_TV(dev))
		intel_tv_init(dev);

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, base.head) {
		encoder->base.possible_crtcs = encoder->crtc_mask;
		encoder->base.possible_clones =
			intel_encoder_clones(dev, encoder->clone_mask);
	}

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	if (HAS_PCH_SPLIT(dev))
		ironlake_init_pch_refclk(dev);
}

static void intel_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);

	drm_framebuffer_cleanup(fb);
	drm_gem_object_unreference_unlocked(&intel_fb->obj->base);

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

int intel_framebuffer_init(struct drm_device *dev,
			   struct intel_framebuffer *intel_fb,
			   struct drm_mode_fb_cmd2 *mode_cmd,
			   struct drm_i915_gem_object *obj)
{
	int ret;

	if (obj->tiling_mode == I915_TILING_Y)
		return -EINVAL;

	if (mode_cmd->pitches[0] & 63)
		return -EINVAL;

	switch (mode_cmd->pixel_format) {
	case DRM_FORMAT_RGB332:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		/* RGB formats are common across chipsets */
		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_VYUY:
		break;
	default:
		DRM_DEBUG_KMS("unsupported pixel format %u\n",
				mode_cmd->pixel_format);
		return -EINVAL;
	}

	ret = drm_framebuffer_init(dev, &intel_fb->base, &intel_fb_funcs);
	if (ret) {
		DRM_ERROR("framebuffer init failed %d\n", ret);
		return ret;
	}

	drm_helper_mode_fill_fb_struct(&intel_fb->base, mode_cmd);
	intel_fb->obj = obj;
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

static const struct drm_mode_config_funcs intel_mode_funcs = {
	.fb_create = intel_user_framebuffer_create,
	.output_poll_changed = intel_fb_output_poll_changed,
};

/* Set up chip specific display functions */
static void intel_init_display(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* We always want a DPMS function */
	if (HAS_PCH_SPLIT(dev)) {
		dev_priv->display.dpms = ironlake_crtc_dpms;
		dev_priv->display.crtc_mode_set = ironlake_crtc_mode_set;
		dev_priv->display.off = ironlake_crtc_off;
		dev_priv->display.update_plane = ironlake_update_plane;
	} else {
		dev_priv->display.dpms = i9xx_crtc_dpms;
		dev_priv->display.crtc_mode_set = i9xx_crtc_mode_set;
		dev_priv->display.off = i9xx_crtc_off;
		dev_priv->display.update_plane = i9xx_update_plane;
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
	else if (IS_I945GM(dev) || IS_845G(dev) || IS_PINEVIEW_M(dev))
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
			i855_get_display_clock_speed;
	else /* 852, 830 */
		dev_priv->display.get_display_clock_speed =
			i830_get_display_clock_speed;

	if (HAS_PCH_SPLIT(dev)) {
		if (IS_GEN5(dev)) {
			dev_priv->display.fdi_link_train = ironlake_fdi_link_train;
			dev_priv->display.write_eld = ironlake_write_eld;
		} else if (IS_GEN6(dev)) {
			dev_priv->display.fdi_link_train = gen6_fdi_link_train;
			dev_priv->display.write_eld = ironlake_write_eld;
		} else if (IS_IVYBRIDGE(dev)) {
			/* FIXME: detect B0+ stepping and use auto training */
			dev_priv->display.fdi_link_train = ivb_manual_fdi_link_train;
			dev_priv->display.write_eld = ironlake_write_eld;
		} else
			dev_priv->display.update_wm = NULL;
	} else if (IS_VALLEYVIEW(dev)) {
		dev_priv->display.force_wake_get = vlv_force_wake_get;
		dev_priv->display.force_wake_put = vlv_force_wake_put;
	} else if (IS_G4X(dev)) {
		dev_priv->display.write_eld = g4x_write_eld;
	}

	/* Default just returns -ENODEV to indicate unsupported */
	dev_priv->display.queue_flip = intel_default_queue_flip;

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
		dev_priv->display.queue_flip = intel_gen7_queue_flip;
		break;
	}
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

struct intel_quirk {
	int device;
	int subsystem_vendor;
	int subsystem_device;
	void (*hook)(struct drm_device *dev);
};

static struct intel_quirk intel_quirks[] = {
	/* HP Mini needs pipe A force quirk (LP: #322104) */
	{ 0x27ae, 0x103c, 0x361a, quirk_pipea_force },

	/* Thinkpad R31 needs pipe A force quirk */
	{ 0x3577, 0x1014, 0x0505, quirk_pipea_force },
	/* Toshiba Protege R-205, S-209 needs pipe A force quirk */
	{ 0x2592, 0x1179, 0x0001, quirk_pipea_force },

	/* ThinkPad X30 needs pipe A force quirk (LP: #304614) */
	{ 0x3577,  0x1014, 0x0513, quirk_pipea_force },
	/* ThinkPad X40 needs pipe A force quirk */

	/* ThinkPad T60 needs pipe A force quirk (bug #16494) */
	{ 0x2782, 0x17aa, 0x201a, quirk_pipea_force },

	/* 855 & before need to leave pipe A & dpll A up */
	{ 0x3582, PCI_ANY_ID, PCI_ANY_ID, quirk_pipea_force },
	{ 0x2562, PCI_ANY_ID, PCI_ANY_ID, quirk_pipea_force },

	/* Lenovo U160 cannot use SSC on LVDS */
	{ 0x0046, 0x17aa, 0x3920, quirk_ssc_force_disable },

	/* Sony Vaio Y cannot use SSC on LVDS */
	{ 0x0046, 0x104d, 0x9076, quirk_ssc_force_disable },

	/* Acer Aspire 5734Z must invert backlight brightness */
	{ 0x2a42, 0x1025, 0x0459, quirk_invert_brightness },
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
}

/* Disable the VGA plane that we never use */
static void i915_disable_vga(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u8 sr1;
	u32 vga_reg;

	if (HAS_PCH_SPLIT(dev))
		vga_reg = CPU_VGACNTRL;
	else
		vga_reg = VGACNTRL;

	vga_get_uninterruptible(dev->pdev, VGA_RSRC_LEGACY_IO);
	outb(SR01, VGA_SR_INDEX);
	sr1 = inb(VGA_SR_DATA);
	outb(sr1 | 1<<5, VGA_SR_DATA);
	vga_put(dev->pdev, VGA_RSRC_LEGACY_IO);
	udelay(300);

	I915_WRITE(vga_reg, VGA_DISP_DISABLE);
	POSTING_READ(vga_reg);
}

static void ivb_pch_pwm_override(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/*
	 * IVB has CPU eDP backlight regs too, set things up to let the
	 * PCH regs control the backlight
	 */
	I915_WRITE(BLC_PWM_CPU_CTL2, PWM_ENABLE);
	I915_WRITE(BLC_PWM_CPU_CTL, 0);
	I915_WRITE(BLC_PWM_PCH_CTL1, PWM_ENABLE | (1<<30));
}

void intel_modeset_init_hw(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	intel_init_clock_gating(dev);

	if (IS_IRONLAKE_M(dev)) {
		ironlake_enable_drps(dev);
		ironlake_enable_rc6(dev);
		intel_init_emon(dev);
	}

	if ((IS_GEN6(dev) || IS_GEN7(dev)) && !IS_VALLEYVIEW(dev)) {
		gen6_enable_rps(dev_priv);
		gen6_update_ring_freq(dev_priv);
	}

	if (IS_IVYBRIDGE(dev))
		ivb_pch_pwm_override(dev);
}

void intel_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i, ret;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;

	dev->mode_config.funcs = (void *)&intel_mode_funcs;

	intel_init_quirks(dev);

	intel_init_pm(dev);

	intel_init_display(dev);

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
	dev->mode_config.fb_base = dev->agp->base;

	DRM_DEBUG_KMS("%d display pipe%s available.\n",
		      dev_priv->num_pipe, dev_priv->num_pipe > 1 ? "s" : "");

	for (i = 0; i < dev_priv->num_pipe; i++) {
		intel_crtc_init(dev, i);
		ret = intel_plane_init(dev, i);
		if (ret)
			DRM_DEBUG_KMS("plane %d init failed: %d\n", i, ret);
	}

	intel_pch_pll_init(dev);

	/* Just disable it once at startup */
	i915_disable_vga(dev);
	intel_setup_outputs(dev);

	INIT_WORK(&dev_priv->idle_work, intel_idle_update);
	setup_timer(&dev_priv->idle_timer, intel_gpu_idle_timer,
		    (unsigned long)dev);
}

void intel_modeset_gem_init(struct drm_device *dev)
{
	intel_modeset_init_hw(dev);

	intel_setup_overlay(dev);
}

void intel_modeset_cleanup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct intel_crtc *intel_crtc;

	drm_kms_helper_poll_fini(dev);
	mutex_lock(&dev->struct_mutex);

	intel_unregister_dsm_handler();


	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		/* Skip inactive CRTCs */
		if (!crtc->fb)
			continue;

		intel_crtc = to_intel_crtc(crtc);
		intel_increase_pllclock(crtc);
	}

	intel_disable_fbc(dev);

	if (IS_IRONLAKE_M(dev))
		ironlake_disable_drps(dev);
	if ((IS_GEN6(dev) || IS_GEN7(dev)) && !IS_VALLEYVIEW(dev))
		gen6_disable_rps(dev);

	if (IS_IRONLAKE_M(dev))
		ironlake_disable_rc6(dev);

	if (IS_VALLEYVIEW(dev))
		vlv_init_dpio(dev);

	mutex_unlock(&dev->struct_mutex);

	/* Disable the irq before mode object teardown, for the irq might
	 * enqueue unpin/hotplug work. */
	drm_irq_uninstall(dev);
	cancel_work_sync(&dev_priv->hotplug_work);
	cancel_work_sync(&dev_priv->rps_work);

	/* flush any delayed tasks or pending work */
	flush_scheduled_work();

	/* Shut off idle work before the crtcs get freed. */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		intel_crtc = to_intel_crtc(crtc);
		del_timer_sync(&intel_crtc->idle_timer);
	}
	del_timer_sync(&dev_priv->idle_timer);
	cancel_work_sync(&dev_priv->idle_work);

	drm_mode_config_cleanup(dev);
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
	u16 gmch_ctrl;

	pci_read_config_word(dev_priv->bridge_dev, INTEL_GMCH_CTRL, &gmch_ctrl);
	if (state)
		gmch_ctrl &= ~INTEL_GMCH_VGA_DISABLE;
	else
		gmch_ctrl |= INTEL_GMCH_VGA_DISABLE;
	pci_write_config_word(dev_priv->bridge_dev, INTEL_GMCH_CTRL, gmch_ctrl);
	return 0;
}

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

struct intel_display_error_state {
	struct intel_cursor_error_state {
		u32 control;
		u32 position;
		u32 base;
		u32 size;
	} cursor[2];

	struct intel_pipe_error_state {
		u32 conf;
		u32 source;

		u32 htotal;
		u32 hblank;
		u32 hsync;
		u32 vtotal;
		u32 vblank;
		u32 vsync;
	} pipe[2];

	struct intel_plane_error_state {
		u32 control;
		u32 stride;
		u32 size;
		u32 pos;
		u32 addr;
		u32 surface;
		u32 tile_offset;
	} plane[2];
};

struct intel_display_error_state *
intel_display_capture_error_state(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_display_error_state *error;
	int i;

	error = kmalloc(sizeof(*error), GFP_ATOMIC);
	if (error == NULL)
		return NULL;

	for (i = 0; i < 2; i++) {
		error->cursor[i].control = I915_READ(CURCNTR(i));
		error->cursor[i].position = I915_READ(CURPOS(i));
		error->cursor[i].base = I915_READ(CURBASE(i));

		error->plane[i].control = I915_READ(DSPCNTR(i));
		error->plane[i].stride = I915_READ(DSPSTRIDE(i));
		error->plane[i].size = I915_READ(DSPSIZE(i));
		error->plane[i].pos = I915_READ(DSPPOS(i));
		error->plane[i].addr = I915_READ(DSPADDR(i));
		if (INTEL_INFO(dev)->gen >= 4) {
			error->plane[i].surface = I915_READ(DSPSURF(i));
			error->plane[i].tile_offset = I915_READ(DSPTILEOFF(i));
		}

		error->pipe[i].conf = I915_READ(PIPECONF(i));
		error->pipe[i].source = I915_READ(PIPESRC(i));
		error->pipe[i].htotal = I915_READ(HTOTAL(i));
		error->pipe[i].hblank = I915_READ(HBLANK(i));
		error->pipe[i].hsync = I915_READ(HSYNC(i));
		error->pipe[i].vtotal = I915_READ(VTOTAL(i));
		error->pipe[i].vblank = I915_READ(VBLANK(i));
		error->pipe[i].vsync = I915_READ(VSYNC(i));
	}

	return error;
}

void
intel_display_print_error_state(struct seq_file *m,
				struct drm_device *dev,
				struct intel_display_error_state *error)
{
	int i;

	for (i = 0; i < 2; i++) {
		seq_printf(m, "Pipe [%d]:\n", i);
		seq_printf(m, "  CONF: %08x\n", error->pipe[i].conf);
		seq_printf(m, "  SRC: %08x\n", error->pipe[i].source);
		seq_printf(m, "  HTOTAL: %08x\n", error->pipe[i].htotal);
		seq_printf(m, "  HBLANK: %08x\n", error->pipe[i].hblank);
		seq_printf(m, "  HSYNC: %08x\n", error->pipe[i].hsync);
		seq_printf(m, "  VTOTAL: %08x\n", error->pipe[i].vtotal);
		seq_printf(m, "  VBLANK: %08x\n", error->pipe[i].vblank);
		seq_printf(m, "  VSYNC: %08x\n", error->pipe[i].vsync);

		seq_printf(m, "Plane [%d]:\n", i);
		seq_printf(m, "  CNTR: %08x\n", error->plane[i].control);
		seq_printf(m, "  STRIDE: %08x\n", error->plane[i].stride);
		seq_printf(m, "  SIZE: %08x\n", error->plane[i].size);
		seq_printf(m, "  POS: %08x\n", error->plane[i].pos);
		seq_printf(m, "  ADDR: %08x\n", error->plane[i].addr);
		if (INTEL_INFO(dev)->gen >= 4) {
			seq_printf(m, "  SURF: %08x\n", error->plane[i].surface);
			seq_printf(m, "  TILEOFF: %08x\n", error->plane[i].tile_offset);
		}

		seq_printf(m, "Cursor [%d]:\n", i);
		seq_printf(m, "  CNTR: %08x\n", error->cursor[i].control);
		seq_printf(m, "  POS: %08x\n", error->cursor[i].position);
		seq_printf(m, "  BASE: %08x\n", error->cursor[i].base);
	}
}
#endif
