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

#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vgaarb.h>
#include "drmP.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "i915_trace.h"
#include "drm_dp_helper.h"

#include "drm_crtc_helper.h"

#define HAS_eDP (intel_pipe_has_type(crtc, INTEL_OUTPUT_EDP))

bool intel_pipe_has_type (struct drm_crtc *crtc, int type);
static void intel_update_watermarks(struct drm_device *dev);
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
		      int, int, intel_clock_t *);
};

#define I8XX_DOT_MIN		  25000
#define I8XX_DOT_MAX		 350000
#define I8XX_VCO_MIN		 930000
#define I8XX_VCO_MAX		1400000
#define I8XX_N_MIN		      3
#define I8XX_N_MAX		     16
#define I8XX_M_MIN		     96
#define I8XX_M_MAX		    140
#define I8XX_M1_MIN		     18
#define I8XX_M1_MAX		     26
#define I8XX_M2_MIN		      6
#define I8XX_M2_MAX		     16
#define I8XX_P_MIN		      4
#define I8XX_P_MAX		    128
#define I8XX_P1_MIN		      2
#define I8XX_P1_MAX		     33
#define I8XX_P1_LVDS_MIN	      1
#define I8XX_P1_LVDS_MAX	      6
#define I8XX_P2_SLOW		      4
#define I8XX_P2_FAST		      2
#define I8XX_P2_LVDS_SLOW	      14
#define I8XX_P2_LVDS_FAST	      7
#define I8XX_P2_SLOW_LIMIT	 165000

#define I9XX_DOT_MIN		  20000
#define I9XX_DOT_MAX		 400000
#define I9XX_VCO_MIN		1400000
#define I9XX_VCO_MAX		2800000
#define PINEVIEW_VCO_MIN		1700000
#define PINEVIEW_VCO_MAX		3500000
#define I9XX_N_MIN		      1
#define I9XX_N_MAX		      6
/* Pineview's Ncounter is a ring counter */
#define PINEVIEW_N_MIN		      3
#define PINEVIEW_N_MAX		      6
#define I9XX_M_MIN		     70
#define I9XX_M_MAX		    120
#define PINEVIEW_M_MIN		      2
#define PINEVIEW_M_MAX		    256
#define I9XX_M1_MIN		     10
#define I9XX_M1_MAX		     22
#define I9XX_M2_MIN		      5
#define I9XX_M2_MAX		      9
/* Pineview M1 is reserved, and must be 0 */
#define PINEVIEW_M1_MIN		      0
#define PINEVIEW_M1_MAX		      0
#define PINEVIEW_M2_MIN		      0
#define PINEVIEW_M2_MAX		      254
#define I9XX_P_SDVO_DAC_MIN	      5
#define I9XX_P_SDVO_DAC_MAX	     80
#define I9XX_P_LVDS_MIN		      7
#define I9XX_P_LVDS_MAX		     98
#define PINEVIEW_P_LVDS_MIN		      7
#define PINEVIEW_P_LVDS_MAX		     112
#define I9XX_P1_MIN		      1
#define I9XX_P1_MAX		      8
#define I9XX_P2_SDVO_DAC_SLOW		     10
#define I9XX_P2_SDVO_DAC_FAST		      5
#define I9XX_P2_SDVO_DAC_SLOW_LIMIT	 200000
#define I9XX_P2_LVDS_SLOW		     14
#define I9XX_P2_LVDS_FAST		      7
#define I9XX_P2_LVDS_SLOW_LIMIT		 112000

/*The parameter is for SDVO on G4x platform*/
#define G4X_DOT_SDVO_MIN           25000
#define G4X_DOT_SDVO_MAX           270000
#define G4X_VCO_MIN                1750000
#define G4X_VCO_MAX                3500000
#define G4X_N_SDVO_MIN             1
#define G4X_N_SDVO_MAX             4
#define G4X_M_SDVO_MIN             104
#define G4X_M_SDVO_MAX             138
#define G4X_M1_SDVO_MIN            17
#define G4X_M1_SDVO_MAX            23
#define G4X_M2_SDVO_MIN            5
#define G4X_M2_SDVO_MAX            11
#define G4X_P_SDVO_MIN             10
#define G4X_P_SDVO_MAX             30
#define G4X_P1_SDVO_MIN            1
#define G4X_P1_SDVO_MAX            3
#define G4X_P2_SDVO_SLOW           10
#define G4X_P2_SDVO_FAST           10
#define G4X_P2_SDVO_LIMIT          270000

/*The parameter is for HDMI_DAC on G4x platform*/
#define G4X_DOT_HDMI_DAC_MIN           22000
#define G4X_DOT_HDMI_DAC_MAX           400000
#define G4X_N_HDMI_DAC_MIN             1
#define G4X_N_HDMI_DAC_MAX             4
#define G4X_M_HDMI_DAC_MIN             104
#define G4X_M_HDMI_DAC_MAX             138
#define G4X_M1_HDMI_DAC_MIN            16
#define G4X_M1_HDMI_DAC_MAX            23
#define G4X_M2_HDMI_DAC_MIN            5
#define G4X_M2_HDMI_DAC_MAX            11
#define G4X_P_HDMI_DAC_MIN             5
#define G4X_P_HDMI_DAC_MAX             80
#define G4X_P1_HDMI_DAC_MIN            1
#define G4X_P1_HDMI_DAC_MAX            8
#define G4X_P2_HDMI_DAC_SLOW           10
#define G4X_P2_HDMI_DAC_FAST           5
#define G4X_P2_HDMI_DAC_LIMIT          165000

/*The parameter is for SINGLE_CHANNEL_LVDS on G4x platform*/
#define G4X_DOT_SINGLE_CHANNEL_LVDS_MIN           20000
#define G4X_DOT_SINGLE_CHANNEL_LVDS_MAX           115000
#define G4X_N_SINGLE_CHANNEL_LVDS_MIN             1
#define G4X_N_SINGLE_CHANNEL_LVDS_MAX             3
#define G4X_M_SINGLE_CHANNEL_LVDS_MIN             104
#define G4X_M_SINGLE_CHANNEL_LVDS_MAX             138
#define G4X_M1_SINGLE_CHANNEL_LVDS_MIN            17
#define G4X_M1_SINGLE_CHANNEL_LVDS_MAX            23
#define G4X_M2_SINGLE_CHANNEL_LVDS_MIN            5
#define G4X_M2_SINGLE_CHANNEL_LVDS_MAX            11
#define G4X_P_SINGLE_CHANNEL_LVDS_MIN             28
#define G4X_P_SINGLE_CHANNEL_LVDS_MAX             112
#define G4X_P1_SINGLE_CHANNEL_LVDS_MIN            2
#define G4X_P1_SINGLE_CHANNEL_LVDS_MAX            8
#define G4X_P2_SINGLE_CHANNEL_LVDS_SLOW           14
#define G4X_P2_SINGLE_CHANNEL_LVDS_FAST           14
#define G4X_P2_SINGLE_CHANNEL_LVDS_LIMIT          0

/*The parameter is for DUAL_CHANNEL_LVDS on G4x platform*/
#define G4X_DOT_DUAL_CHANNEL_LVDS_MIN           80000
#define G4X_DOT_DUAL_CHANNEL_LVDS_MAX           224000
#define G4X_N_DUAL_CHANNEL_LVDS_MIN             1
#define G4X_N_DUAL_CHANNEL_LVDS_MAX             3
#define G4X_M_DUAL_CHANNEL_LVDS_MIN             104
#define G4X_M_DUAL_CHANNEL_LVDS_MAX             138
#define G4X_M1_DUAL_CHANNEL_LVDS_MIN            17
#define G4X_M1_DUAL_CHANNEL_LVDS_MAX            23
#define G4X_M2_DUAL_CHANNEL_LVDS_MIN            5
#define G4X_M2_DUAL_CHANNEL_LVDS_MAX            11
#define G4X_P_DUAL_CHANNEL_LVDS_MIN             14
#define G4X_P_DUAL_CHANNEL_LVDS_MAX             42
#define G4X_P1_DUAL_CHANNEL_LVDS_MIN            2
#define G4X_P1_DUAL_CHANNEL_LVDS_MAX            6
#define G4X_P2_DUAL_CHANNEL_LVDS_SLOW           7
#define G4X_P2_DUAL_CHANNEL_LVDS_FAST           7
#define G4X_P2_DUAL_CHANNEL_LVDS_LIMIT          0

/*The parameter is for DISPLAY PORT on G4x platform*/
#define G4X_DOT_DISPLAY_PORT_MIN           161670
#define G4X_DOT_DISPLAY_PORT_MAX           227000
#define G4X_N_DISPLAY_PORT_MIN             1
#define G4X_N_DISPLAY_PORT_MAX             2
#define G4X_M_DISPLAY_PORT_MIN             97
#define G4X_M_DISPLAY_PORT_MAX             108
#define G4X_M1_DISPLAY_PORT_MIN            0x10
#define G4X_M1_DISPLAY_PORT_MAX            0x12
#define G4X_M2_DISPLAY_PORT_MIN            0x05
#define G4X_M2_DISPLAY_PORT_MAX            0x06
#define G4X_P_DISPLAY_PORT_MIN             10
#define G4X_P_DISPLAY_PORT_MAX             20
#define G4X_P1_DISPLAY_PORT_MIN            1
#define G4X_P1_DISPLAY_PORT_MAX            2
#define G4X_P2_DISPLAY_PORT_SLOW           10
#define G4X_P2_DISPLAY_PORT_FAST           10
#define G4X_P2_DISPLAY_PORT_LIMIT          0

/* Ironlake / Sandybridge */
/* as we calculate clock using (register_value + 2) for
   N/M1/M2, so here the range value for them is (actual_value-2).
 */
#define IRONLAKE_DOT_MIN         25000
#define IRONLAKE_DOT_MAX         350000
#define IRONLAKE_VCO_MIN         1760000
#define IRONLAKE_VCO_MAX         3510000
#define IRONLAKE_M1_MIN          12
#define IRONLAKE_M1_MAX          22
#define IRONLAKE_M2_MIN          5
#define IRONLAKE_M2_MAX          9
#define IRONLAKE_P2_DOT_LIMIT    225000 /* 225Mhz */

/* We have parameter ranges for different type of outputs. */

/* DAC & HDMI Refclk 120Mhz */
#define IRONLAKE_DAC_N_MIN	1
#define IRONLAKE_DAC_N_MAX	5
#define IRONLAKE_DAC_M_MIN	79
#define IRONLAKE_DAC_M_MAX	127
#define IRONLAKE_DAC_P_MIN	5
#define IRONLAKE_DAC_P_MAX	80
#define IRONLAKE_DAC_P1_MIN	1
#define IRONLAKE_DAC_P1_MAX	8
#define IRONLAKE_DAC_P2_SLOW	10
#define IRONLAKE_DAC_P2_FAST	5

/* LVDS single-channel 120Mhz refclk */
#define IRONLAKE_LVDS_S_N_MIN	1
#define IRONLAKE_LVDS_S_N_MAX	3
#define IRONLAKE_LVDS_S_M_MIN	79
#define IRONLAKE_LVDS_S_M_MAX	118
#define IRONLAKE_LVDS_S_P_MIN	28
#define IRONLAKE_LVDS_S_P_MAX	112
#define IRONLAKE_LVDS_S_P1_MIN	2
#define IRONLAKE_LVDS_S_P1_MAX	8
#define IRONLAKE_LVDS_S_P2_SLOW	14
#define IRONLAKE_LVDS_S_P2_FAST	14

/* LVDS dual-channel 120Mhz refclk */
#define IRONLAKE_LVDS_D_N_MIN	1
#define IRONLAKE_LVDS_D_N_MAX	3
#define IRONLAKE_LVDS_D_M_MIN	79
#define IRONLAKE_LVDS_D_M_MAX	127
#define IRONLAKE_LVDS_D_P_MIN	14
#define IRONLAKE_LVDS_D_P_MAX	56
#define IRONLAKE_LVDS_D_P1_MIN	2
#define IRONLAKE_LVDS_D_P1_MAX	8
#define IRONLAKE_LVDS_D_P2_SLOW	7
#define IRONLAKE_LVDS_D_P2_FAST	7

/* LVDS single-channel 100Mhz refclk */
#define IRONLAKE_LVDS_S_SSC_N_MIN	1
#define IRONLAKE_LVDS_S_SSC_N_MAX	2
#define IRONLAKE_LVDS_S_SSC_M_MIN	79
#define IRONLAKE_LVDS_S_SSC_M_MAX	126
#define IRONLAKE_LVDS_S_SSC_P_MIN	28
#define IRONLAKE_LVDS_S_SSC_P_MAX	112
#define IRONLAKE_LVDS_S_SSC_P1_MIN	2
#define IRONLAKE_LVDS_S_SSC_P1_MAX	8
#define IRONLAKE_LVDS_S_SSC_P2_SLOW	14
#define IRONLAKE_LVDS_S_SSC_P2_FAST	14

/* LVDS dual-channel 100Mhz refclk */
#define IRONLAKE_LVDS_D_SSC_N_MIN	1
#define IRONLAKE_LVDS_D_SSC_N_MAX	3
#define IRONLAKE_LVDS_D_SSC_M_MIN	79
#define IRONLAKE_LVDS_D_SSC_M_MAX	126
#define IRONLAKE_LVDS_D_SSC_P_MIN	14
#define IRONLAKE_LVDS_D_SSC_P_MAX	42
#define IRONLAKE_LVDS_D_SSC_P1_MIN	2
#define IRONLAKE_LVDS_D_SSC_P1_MAX	6
#define IRONLAKE_LVDS_D_SSC_P2_SLOW	7
#define IRONLAKE_LVDS_D_SSC_P2_FAST	7

/* DisplayPort */
#define IRONLAKE_DP_N_MIN		1
#define IRONLAKE_DP_N_MAX		2
#define IRONLAKE_DP_M_MIN		81
#define IRONLAKE_DP_M_MAX		90
#define IRONLAKE_DP_P_MIN		10
#define IRONLAKE_DP_P_MAX		20
#define IRONLAKE_DP_P2_FAST		10
#define IRONLAKE_DP_P2_SLOW		10
#define IRONLAKE_DP_P2_LIMIT		0
#define IRONLAKE_DP_P1_MIN		1
#define IRONLAKE_DP_P1_MAX		2

/* FDI */
#define IRONLAKE_FDI_FREQ		2700000 /* in kHz for mode->clock */

static bool
intel_find_best_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
		    int target, int refclk, intel_clock_t *best_clock);
static bool
intel_g4x_find_best_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
			int target, int refclk, intel_clock_t *best_clock);

static bool
intel_find_pll_g4x_dp(const intel_limit_t *, struct drm_crtc *crtc,
		      int target, int refclk, intel_clock_t *best_clock);
static bool
intel_find_pll_ironlake_dp(const intel_limit_t *, struct drm_crtc *crtc,
			   int target, int refclk, intel_clock_t *best_clock);

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
        .dot = { .min = I8XX_DOT_MIN,		.max = I8XX_DOT_MAX },
        .vco = { .min = I8XX_VCO_MIN,		.max = I8XX_VCO_MAX },
        .n   = { .min = I8XX_N_MIN,		.max = I8XX_N_MAX },
        .m   = { .min = I8XX_M_MIN,		.max = I8XX_M_MAX },
        .m1  = { .min = I8XX_M1_MIN,		.max = I8XX_M1_MAX },
        .m2  = { .min = I8XX_M2_MIN,		.max = I8XX_M2_MAX },
        .p   = { .min = I8XX_P_MIN,		.max = I8XX_P_MAX },
        .p1  = { .min = I8XX_P1_MIN,		.max = I8XX_P1_MAX },
	.p2  = { .dot_limit = I8XX_P2_SLOW_LIMIT,
		 .p2_slow = I8XX_P2_SLOW,	.p2_fast = I8XX_P2_FAST },
	.find_pll = intel_find_best_PLL,
};

static const intel_limit_t intel_limits_i8xx_lvds = {
        .dot = { .min = I8XX_DOT_MIN,		.max = I8XX_DOT_MAX },
        .vco = { .min = I8XX_VCO_MIN,		.max = I8XX_VCO_MAX },
        .n   = { .min = I8XX_N_MIN,		.max = I8XX_N_MAX },
        .m   = { .min = I8XX_M_MIN,		.max = I8XX_M_MAX },
        .m1  = { .min = I8XX_M1_MIN,		.max = I8XX_M1_MAX },
        .m2  = { .min = I8XX_M2_MIN,		.max = I8XX_M2_MAX },
        .p   = { .min = I8XX_P_MIN,		.max = I8XX_P_MAX },
        .p1  = { .min = I8XX_P1_LVDS_MIN,	.max = I8XX_P1_LVDS_MAX },
	.p2  = { .dot_limit = I8XX_P2_SLOW_LIMIT,
		 .p2_slow = I8XX_P2_LVDS_SLOW,	.p2_fast = I8XX_P2_LVDS_FAST },
	.find_pll = intel_find_best_PLL,
};
	
static const intel_limit_t intel_limits_i9xx_sdvo = {
        .dot = { .min = I9XX_DOT_MIN,		.max = I9XX_DOT_MAX },
        .vco = { .min = I9XX_VCO_MIN,		.max = I9XX_VCO_MAX },
        .n   = { .min = I9XX_N_MIN,		.max = I9XX_N_MAX },
        .m   = { .min = I9XX_M_MIN,		.max = I9XX_M_MAX },
        .m1  = { .min = I9XX_M1_MIN,		.max = I9XX_M1_MAX },
        .m2  = { .min = I9XX_M2_MIN,		.max = I9XX_M2_MAX },
        .p   = { .min = I9XX_P_SDVO_DAC_MIN,	.max = I9XX_P_SDVO_DAC_MAX },
        .p1  = { .min = I9XX_P1_MIN,		.max = I9XX_P1_MAX },
	.p2  = { .dot_limit = I9XX_P2_SDVO_DAC_SLOW_LIMIT,
		 .p2_slow = I9XX_P2_SDVO_DAC_SLOW,	.p2_fast = I9XX_P2_SDVO_DAC_FAST },
	.find_pll = intel_find_best_PLL,
};

static const intel_limit_t intel_limits_i9xx_lvds = {
        .dot = { .min = I9XX_DOT_MIN,		.max = I9XX_DOT_MAX },
        .vco = { .min = I9XX_VCO_MIN,		.max = I9XX_VCO_MAX },
        .n   = { .min = I9XX_N_MIN,		.max = I9XX_N_MAX },
        .m   = { .min = I9XX_M_MIN,		.max = I9XX_M_MAX },
        .m1  = { .min = I9XX_M1_MIN,		.max = I9XX_M1_MAX },
        .m2  = { .min = I9XX_M2_MIN,		.max = I9XX_M2_MAX },
        .p   = { .min = I9XX_P_LVDS_MIN,	.max = I9XX_P_LVDS_MAX },
        .p1  = { .min = I9XX_P1_MIN,		.max = I9XX_P1_MAX },
	/* The single-channel range is 25-112Mhz, and dual-channel
	 * is 80-224Mhz.  Prefer single channel as much as possible.
	 */
	.p2  = { .dot_limit = I9XX_P2_LVDS_SLOW_LIMIT,
		 .p2_slow = I9XX_P2_LVDS_SLOW,	.p2_fast = I9XX_P2_LVDS_FAST },
	.find_pll = intel_find_best_PLL,
};

    /* below parameter and function is for G4X Chipset Family*/
static const intel_limit_t intel_limits_g4x_sdvo = {
	.dot = { .min = G4X_DOT_SDVO_MIN,	.max = G4X_DOT_SDVO_MAX },
	.vco = { .min = G4X_VCO_MIN,	        .max = G4X_VCO_MAX},
	.n   = { .min = G4X_N_SDVO_MIN,	        .max = G4X_N_SDVO_MAX },
	.m   = { .min = G4X_M_SDVO_MIN,         .max = G4X_M_SDVO_MAX },
	.m1  = { .min = G4X_M1_SDVO_MIN,	.max = G4X_M1_SDVO_MAX },
	.m2  = { .min = G4X_M2_SDVO_MIN,	.max = G4X_M2_SDVO_MAX },
	.p   = { .min = G4X_P_SDVO_MIN,         .max = G4X_P_SDVO_MAX },
	.p1  = { .min = G4X_P1_SDVO_MIN,	.max = G4X_P1_SDVO_MAX},
	.p2  = { .dot_limit = G4X_P2_SDVO_LIMIT,
		 .p2_slow = G4X_P2_SDVO_SLOW,
		 .p2_fast = G4X_P2_SDVO_FAST
	},
	.find_pll = intel_g4x_find_best_PLL,
};

static const intel_limit_t intel_limits_g4x_hdmi = {
	.dot = { .min = G4X_DOT_HDMI_DAC_MIN,	.max = G4X_DOT_HDMI_DAC_MAX },
	.vco = { .min = G4X_VCO_MIN,	        .max = G4X_VCO_MAX},
	.n   = { .min = G4X_N_HDMI_DAC_MIN,	.max = G4X_N_HDMI_DAC_MAX },
	.m   = { .min = G4X_M_HDMI_DAC_MIN,	.max = G4X_M_HDMI_DAC_MAX },
	.m1  = { .min = G4X_M1_HDMI_DAC_MIN,	.max = G4X_M1_HDMI_DAC_MAX },
	.m2  = { .min = G4X_M2_HDMI_DAC_MIN,	.max = G4X_M2_HDMI_DAC_MAX },
	.p   = { .min = G4X_P_HDMI_DAC_MIN,	.max = G4X_P_HDMI_DAC_MAX },
	.p1  = { .min = G4X_P1_HDMI_DAC_MIN,	.max = G4X_P1_HDMI_DAC_MAX},
	.p2  = { .dot_limit = G4X_P2_HDMI_DAC_LIMIT,
		 .p2_slow = G4X_P2_HDMI_DAC_SLOW,
		 .p2_fast = G4X_P2_HDMI_DAC_FAST
	},
	.find_pll = intel_g4x_find_best_PLL,
};

static const intel_limit_t intel_limits_g4x_single_channel_lvds = {
	.dot = { .min = G4X_DOT_SINGLE_CHANNEL_LVDS_MIN,
		 .max = G4X_DOT_SINGLE_CHANNEL_LVDS_MAX },
	.vco = { .min = G4X_VCO_MIN,
		 .max = G4X_VCO_MAX },
	.n   = { .min = G4X_N_SINGLE_CHANNEL_LVDS_MIN,
		 .max = G4X_N_SINGLE_CHANNEL_LVDS_MAX },
	.m   = { .min = G4X_M_SINGLE_CHANNEL_LVDS_MIN,
		 .max = G4X_M_SINGLE_CHANNEL_LVDS_MAX },
	.m1  = { .min = G4X_M1_SINGLE_CHANNEL_LVDS_MIN,
		 .max = G4X_M1_SINGLE_CHANNEL_LVDS_MAX },
	.m2  = { .min = G4X_M2_SINGLE_CHANNEL_LVDS_MIN,
		 .max = G4X_M2_SINGLE_CHANNEL_LVDS_MAX },
	.p   = { .min = G4X_P_SINGLE_CHANNEL_LVDS_MIN,
		 .max = G4X_P_SINGLE_CHANNEL_LVDS_MAX },
	.p1  = { .min = G4X_P1_SINGLE_CHANNEL_LVDS_MIN,
		 .max = G4X_P1_SINGLE_CHANNEL_LVDS_MAX },
	.p2  = { .dot_limit = G4X_P2_SINGLE_CHANNEL_LVDS_LIMIT,
		 .p2_slow = G4X_P2_SINGLE_CHANNEL_LVDS_SLOW,
		 .p2_fast = G4X_P2_SINGLE_CHANNEL_LVDS_FAST
	},
	.find_pll = intel_g4x_find_best_PLL,
};

static const intel_limit_t intel_limits_g4x_dual_channel_lvds = {
	.dot = { .min = G4X_DOT_DUAL_CHANNEL_LVDS_MIN,
		 .max = G4X_DOT_DUAL_CHANNEL_LVDS_MAX },
	.vco = { .min = G4X_VCO_MIN,
		 .max = G4X_VCO_MAX },
	.n   = { .min = G4X_N_DUAL_CHANNEL_LVDS_MIN,
		 .max = G4X_N_DUAL_CHANNEL_LVDS_MAX },
	.m   = { .min = G4X_M_DUAL_CHANNEL_LVDS_MIN,
		 .max = G4X_M_DUAL_CHANNEL_LVDS_MAX },
	.m1  = { .min = G4X_M1_DUAL_CHANNEL_LVDS_MIN,
		 .max = G4X_M1_DUAL_CHANNEL_LVDS_MAX },
	.m2  = { .min = G4X_M2_DUAL_CHANNEL_LVDS_MIN,
		 .max = G4X_M2_DUAL_CHANNEL_LVDS_MAX },
	.p   = { .min = G4X_P_DUAL_CHANNEL_LVDS_MIN,
		 .max = G4X_P_DUAL_CHANNEL_LVDS_MAX },
	.p1  = { .min = G4X_P1_DUAL_CHANNEL_LVDS_MIN,
		 .max = G4X_P1_DUAL_CHANNEL_LVDS_MAX },
	.p2  = { .dot_limit = G4X_P2_DUAL_CHANNEL_LVDS_LIMIT,
		 .p2_slow = G4X_P2_DUAL_CHANNEL_LVDS_SLOW,
		 .p2_fast = G4X_P2_DUAL_CHANNEL_LVDS_FAST
	},
	.find_pll = intel_g4x_find_best_PLL,
};

static const intel_limit_t intel_limits_g4x_display_port = {
        .dot = { .min = G4X_DOT_DISPLAY_PORT_MIN,
                 .max = G4X_DOT_DISPLAY_PORT_MAX },
        .vco = { .min = G4X_VCO_MIN,
                 .max = G4X_VCO_MAX},
        .n   = { .min = G4X_N_DISPLAY_PORT_MIN,
                 .max = G4X_N_DISPLAY_PORT_MAX },
        .m   = { .min = G4X_M_DISPLAY_PORT_MIN,
                 .max = G4X_M_DISPLAY_PORT_MAX },
        .m1  = { .min = G4X_M1_DISPLAY_PORT_MIN,
                 .max = G4X_M1_DISPLAY_PORT_MAX },
        .m2  = { .min = G4X_M2_DISPLAY_PORT_MIN,
                 .max = G4X_M2_DISPLAY_PORT_MAX },
        .p   = { .min = G4X_P_DISPLAY_PORT_MIN,
                 .max = G4X_P_DISPLAY_PORT_MAX },
        .p1  = { .min = G4X_P1_DISPLAY_PORT_MIN,
                 .max = G4X_P1_DISPLAY_PORT_MAX},
        .p2  = { .dot_limit = G4X_P2_DISPLAY_PORT_LIMIT,
                 .p2_slow = G4X_P2_DISPLAY_PORT_SLOW,
                 .p2_fast = G4X_P2_DISPLAY_PORT_FAST },
        .find_pll = intel_find_pll_g4x_dp,
};

static const intel_limit_t intel_limits_pineview_sdvo = {
        .dot = { .min = I9XX_DOT_MIN,		.max = I9XX_DOT_MAX},
        .vco = { .min = PINEVIEW_VCO_MIN,		.max = PINEVIEW_VCO_MAX },
        .n   = { .min = PINEVIEW_N_MIN,		.max = PINEVIEW_N_MAX },
        .m   = { .min = PINEVIEW_M_MIN,		.max = PINEVIEW_M_MAX },
        .m1  = { .min = PINEVIEW_M1_MIN,		.max = PINEVIEW_M1_MAX },
        .m2  = { .min = PINEVIEW_M2_MIN,		.max = PINEVIEW_M2_MAX },
        .p   = { .min = I9XX_P_SDVO_DAC_MIN,    .max = I9XX_P_SDVO_DAC_MAX },
        .p1  = { .min = I9XX_P1_MIN,		.max = I9XX_P1_MAX },
	.p2  = { .dot_limit = I9XX_P2_SDVO_DAC_SLOW_LIMIT,
		 .p2_slow = I9XX_P2_SDVO_DAC_SLOW,	.p2_fast = I9XX_P2_SDVO_DAC_FAST },
	.find_pll = intel_find_best_PLL,
};

static const intel_limit_t intel_limits_pineview_lvds = {
        .dot = { .min = I9XX_DOT_MIN,		.max = I9XX_DOT_MAX },
        .vco = { .min = PINEVIEW_VCO_MIN,		.max = PINEVIEW_VCO_MAX },
        .n   = { .min = PINEVIEW_N_MIN,		.max = PINEVIEW_N_MAX },
        .m   = { .min = PINEVIEW_M_MIN,		.max = PINEVIEW_M_MAX },
        .m1  = { .min = PINEVIEW_M1_MIN,		.max = PINEVIEW_M1_MAX },
        .m2  = { .min = PINEVIEW_M2_MIN,		.max = PINEVIEW_M2_MAX },
        .p   = { .min = PINEVIEW_P_LVDS_MIN,	.max = PINEVIEW_P_LVDS_MAX },
        .p1  = { .min = I9XX_P1_MIN,		.max = I9XX_P1_MAX },
	/* Pineview only supports single-channel mode. */
	.p2  = { .dot_limit = I9XX_P2_LVDS_SLOW_LIMIT,
		 .p2_slow = I9XX_P2_LVDS_SLOW,	.p2_fast = I9XX_P2_LVDS_SLOW },
	.find_pll = intel_find_best_PLL,
};

static const intel_limit_t intel_limits_ironlake_dac = {
	.dot = { .min = IRONLAKE_DOT_MIN,          .max = IRONLAKE_DOT_MAX },
	.vco = { .min = IRONLAKE_VCO_MIN,          .max = IRONLAKE_VCO_MAX },
	.n   = { .min = IRONLAKE_DAC_N_MIN,        .max = IRONLAKE_DAC_N_MAX },
	.m   = { .min = IRONLAKE_DAC_M_MIN,        .max = IRONLAKE_DAC_M_MAX },
	.m1  = { .min = IRONLAKE_M1_MIN,           .max = IRONLAKE_M1_MAX },
	.m2  = { .min = IRONLAKE_M2_MIN,           .max = IRONLAKE_M2_MAX },
	.p   = { .min = IRONLAKE_DAC_P_MIN,	   .max = IRONLAKE_DAC_P_MAX },
	.p1  = { .min = IRONLAKE_DAC_P1_MIN,       .max = IRONLAKE_DAC_P1_MAX },
	.p2  = { .dot_limit = IRONLAKE_P2_DOT_LIMIT,
		 .p2_slow = IRONLAKE_DAC_P2_SLOW,
		 .p2_fast = IRONLAKE_DAC_P2_FAST },
	.find_pll = intel_g4x_find_best_PLL,
};

static const intel_limit_t intel_limits_ironlake_single_lvds = {
	.dot = { .min = IRONLAKE_DOT_MIN,          .max = IRONLAKE_DOT_MAX },
	.vco = { .min = IRONLAKE_VCO_MIN,          .max = IRONLAKE_VCO_MAX },
	.n   = { .min = IRONLAKE_LVDS_S_N_MIN,     .max = IRONLAKE_LVDS_S_N_MAX },
	.m   = { .min = IRONLAKE_LVDS_S_M_MIN,     .max = IRONLAKE_LVDS_S_M_MAX },
	.m1  = { .min = IRONLAKE_M1_MIN,           .max = IRONLAKE_M1_MAX },
	.m2  = { .min = IRONLAKE_M2_MIN,           .max = IRONLAKE_M2_MAX },
	.p   = { .min = IRONLAKE_LVDS_S_P_MIN,     .max = IRONLAKE_LVDS_S_P_MAX },
	.p1  = { .min = IRONLAKE_LVDS_S_P1_MIN,    .max = IRONLAKE_LVDS_S_P1_MAX },
	.p2  = { .dot_limit = IRONLAKE_P2_DOT_LIMIT,
		 .p2_slow = IRONLAKE_LVDS_S_P2_SLOW,
		 .p2_fast = IRONLAKE_LVDS_S_P2_FAST },
	.find_pll = intel_g4x_find_best_PLL,
};

static const intel_limit_t intel_limits_ironlake_dual_lvds = {
	.dot = { .min = IRONLAKE_DOT_MIN,          .max = IRONLAKE_DOT_MAX },
	.vco = { .min = IRONLAKE_VCO_MIN,          .max = IRONLAKE_VCO_MAX },
	.n   = { .min = IRONLAKE_LVDS_D_N_MIN,     .max = IRONLAKE_LVDS_D_N_MAX },
	.m   = { .min = IRONLAKE_LVDS_D_M_MIN,     .max = IRONLAKE_LVDS_D_M_MAX },
	.m1  = { .min = IRONLAKE_M1_MIN,           .max = IRONLAKE_M1_MAX },
	.m2  = { .min = IRONLAKE_M2_MIN,           .max = IRONLAKE_M2_MAX },
	.p   = { .min = IRONLAKE_LVDS_D_P_MIN,     .max = IRONLAKE_LVDS_D_P_MAX },
	.p1  = { .min = IRONLAKE_LVDS_D_P1_MIN,    .max = IRONLAKE_LVDS_D_P1_MAX },
	.p2  = { .dot_limit = IRONLAKE_P2_DOT_LIMIT,
		 .p2_slow = IRONLAKE_LVDS_D_P2_SLOW,
		 .p2_fast = IRONLAKE_LVDS_D_P2_FAST },
	.find_pll = intel_g4x_find_best_PLL,
};

static const intel_limit_t intel_limits_ironlake_single_lvds_100m = {
	.dot = { .min = IRONLAKE_DOT_MIN,          .max = IRONLAKE_DOT_MAX },
	.vco = { .min = IRONLAKE_VCO_MIN,          .max = IRONLAKE_VCO_MAX },
	.n   = { .min = IRONLAKE_LVDS_S_SSC_N_MIN, .max = IRONLAKE_LVDS_S_SSC_N_MAX },
	.m   = { .min = IRONLAKE_LVDS_S_SSC_M_MIN, .max = IRONLAKE_LVDS_S_SSC_M_MAX },
	.m1  = { .min = IRONLAKE_M1_MIN,           .max = IRONLAKE_M1_MAX },
	.m2  = { .min = IRONLAKE_M2_MIN,           .max = IRONLAKE_M2_MAX },
	.p   = { .min = IRONLAKE_LVDS_S_SSC_P_MIN, .max = IRONLAKE_LVDS_S_SSC_P_MAX },
	.p1  = { .min = IRONLAKE_LVDS_S_SSC_P1_MIN,.max = IRONLAKE_LVDS_S_SSC_P1_MAX },
	.p2  = { .dot_limit = IRONLAKE_P2_DOT_LIMIT,
		 .p2_slow = IRONLAKE_LVDS_S_SSC_P2_SLOW,
		 .p2_fast = IRONLAKE_LVDS_S_SSC_P2_FAST },
	.find_pll = intel_g4x_find_best_PLL,
};

static const intel_limit_t intel_limits_ironlake_dual_lvds_100m = {
	.dot = { .min = IRONLAKE_DOT_MIN,          .max = IRONLAKE_DOT_MAX },
	.vco = { .min = IRONLAKE_VCO_MIN,          .max = IRONLAKE_VCO_MAX },
	.n   = { .min = IRONLAKE_LVDS_D_SSC_N_MIN, .max = IRONLAKE_LVDS_D_SSC_N_MAX },
	.m   = { .min = IRONLAKE_LVDS_D_SSC_M_MIN, .max = IRONLAKE_LVDS_D_SSC_M_MAX },
	.m1  = { .min = IRONLAKE_M1_MIN,           .max = IRONLAKE_M1_MAX },
	.m2  = { .min = IRONLAKE_M2_MIN,           .max = IRONLAKE_M2_MAX },
	.p   = { .min = IRONLAKE_LVDS_D_SSC_P_MIN, .max = IRONLAKE_LVDS_D_SSC_P_MAX },
	.p1  = { .min = IRONLAKE_LVDS_D_SSC_P1_MIN,.max = IRONLAKE_LVDS_D_SSC_P1_MAX },
	.p2  = { .dot_limit = IRONLAKE_P2_DOT_LIMIT,
		 .p2_slow = IRONLAKE_LVDS_D_SSC_P2_SLOW,
		 .p2_fast = IRONLAKE_LVDS_D_SSC_P2_FAST },
	.find_pll = intel_g4x_find_best_PLL,
};

static const intel_limit_t intel_limits_ironlake_display_port = {
        .dot = { .min = IRONLAKE_DOT_MIN,
                 .max = IRONLAKE_DOT_MAX },
        .vco = { .min = IRONLAKE_VCO_MIN,
                 .max = IRONLAKE_VCO_MAX},
        .n   = { .min = IRONLAKE_DP_N_MIN,
                 .max = IRONLAKE_DP_N_MAX },
        .m   = { .min = IRONLAKE_DP_M_MIN,
                 .max = IRONLAKE_DP_M_MAX },
        .m1  = { .min = IRONLAKE_M1_MIN,
                 .max = IRONLAKE_M1_MAX },
        .m2  = { .min = IRONLAKE_M2_MIN,
                 .max = IRONLAKE_M2_MAX },
        .p   = { .min = IRONLAKE_DP_P_MIN,
                 .max = IRONLAKE_DP_P_MAX },
        .p1  = { .min = IRONLAKE_DP_P1_MIN,
                 .max = IRONLAKE_DP_P1_MAX},
        .p2  = { .dot_limit = IRONLAKE_DP_P2_LIMIT,
                 .p2_slow = IRONLAKE_DP_P2_SLOW,
                 .p2_fast = IRONLAKE_DP_P2_FAST },
        .find_pll = intel_find_pll_ironlake_dp,
};

static const intel_limit_t *intel_ironlake_limit(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	const intel_limit_t *limit;
	int refclk = 120;

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
		if (dev_priv->lvds_use_ssc && dev_priv->lvds_ssc_freq == 100)
			refclk = 100;

		if ((I915_READ(PCH_LVDS) & LVDS_CLKB_POWER_MASK) ==
		    LVDS_CLKB_POWER_UP) {
			/* LVDS dual channel */
			if (refclk == 100)
				limit = &intel_limits_ironlake_dual_lvds_100m;
			else
				limit = &intel_limits_ironlake_dual_lvds;
		} else {
			if (refclk == 100)
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
		if ((I915_READ(LVDS) & LVDS_CLKB_POWER_MASK) ==
		    LVDS_CLKB_POWER_UP)
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
	} else if (intel_pipe_has_type (crtc, INTEL_OUTPUT_DISPLAYPORT)) {
		limit = &intel_limits_g4x_display_port;
	} else /* The option is for other outputs */
		limit = &intel_limits_i9xx_sdvo;

	return limit;
}

static const intel_limit_t *intel_limit(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	const intel_limit_t *limit;

	if (HAS_PCH_SPLIT(dev))
		limit = intel_ironlake_limit(crtc);
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

static bool intel_PLL_is_valid(struct drm_crtc *crtc, intel_clock_t *clock)
{
	const intel_limit_t *limit = intel_limit (crtc);
	struct drm_device *dev = crtc->dev;

	if (clock->p1  < limit->p1.min  || limit->p1.max  < clock->p1)
		INTELPllInvalid ("p1 out of range\n");
	if (clock->p   < limit->p.min   || limit->p.max   < clock->p)
		INTELPllInvalid ("p out of range\n");
	if (clock->m2  < limit->m2.min  || limit->m2.max  < clock->m2)
		INTELPllInvalid ("m2 out of range\n");
	if (clock->m1  < limit->m1.min  || limit->m1.max  < clock->m1)
		INTELPllInvalid ("m1 out of range\n");
	if (clock->m1 <= clock->m2 && !IS_PINEVIEW(dev))
		INTELPllInvalid ("m1 <= m2\n");
	if (clock->m   < limit->m.min   || limit->m.max   < clock->m)
		INTELPllInvalid ("m out of range\n");
	if (clock->n   < limit->n.min   || limit->n.max   < clock->n)
		INTELPllInvalid ("n out of range\n");
	if (clock->vco < limit->vco.min || limit->vco.max < clock->vco)
		INTELPllInvalid ("vco out of range\n");
	/* XXX: We may need to be checking "Dot clock" depending on the multiplier,
	 * connector, etc., rather than just a single range.
	 */
	if (clock->dot < limit->dot.min || limit->dot.max < clock->dot)
		INTELPllInvalid ("dot out of range\n");

	return true;
}

static bool
intel_find_best_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
		    int target, int refclk, intel_clock_t *best_clock)

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
		if ((I915_READ(LVDS) & LVDS_CLKB_POWER_MASK) ==
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

	memset (best_clock, 0, sizeof (*best_clock));

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

					if (!intel_PLL_is_valid(crtc, &clock))
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
			int target, int refclk, intel_clock_t *best_clock)
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
					if (!intel_PLL_is_valid(crtc, &clock))
						continue;
					this_err = abs(clock.dot - target) ;
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
			   int target, int refclk, intel_clock_t *best_clock)
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
		      int target, int refclk, intel_clock_t *best_clock)
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
	int pipestat_reg = (pipe == 0 ? PIPEASTAT : PIPEBSTAT);

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
		u32 last_line;
		int reg = PIPEDSL(pipe);
		unsigned long timeout = jiffies + msecs_to_jiffies(100);

		/* Wait for the display line to settle */
		do {
			last_line = I915_READ(reg) & DSL_LINEMASK;
			mdelay(5);
		} while (((I915_READ(reg) & DSL_LINEMASK) != last_line) &&
			 time_after(timeout, jiffies));
		if (time_after(jiffies, timeout))
			DRM_DEBUG_KMS("pipe_off wait timed out\n");
	}
}

static void i8xx_enable_fbc(struct drm_crtc *crtc, unsigned long interval)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int plane, i;
	u32 fbc_ctl, fbc_ctl2;

	if (fb->pitch == dev_priv->cfb_pitch &&
	    obj->fence_reg == dev_priv->cfb_fence &&
	    intel_crtc->plane == dev_priv->cfb_plane &&
	    I915_READ(FBC_CONTROL) & FBC_CTL_EN)
		return;

	i8xx_disable_fbc(dev);

	dev_priv->cfb_pitch = dev_priv->cfb_size / FBC_LL_SIZE;

	if (fb->pitch < dev_priv->cfb_pitch)
		dev_priv->cfb_pitch = fb->pitch;

	/* FBC_CTL wants 64B units */
	dev_priv->cfb_pitch = (dev_priv->cfb_pitch / 64) - 1;
	dev_priv->cfb_fence = obj->fence_reg;
	dev_priv->cfb_plane = intel_crtc->plane;
	plane = dev_priv->cfb_plane == 0 ? FBC_CTL_PLANEA : FBC_CTL_PLANEB;

	/* Clear old tags */
	for (i = 0; i < (FBC_LL_SIZE / 32) + 1; i++)
		I915_WRITE(FBC_TAG + (i * 4), 0);

	/* Set it up... */
	fbc_ctl2 = FBC_CTL_FENCE_DBL | FBC_CTL_IDLE_IMM | plane;
	if (obj->tiling_mode != I915_TILING_NONE)
		fbc_ctl2 |= FBC_CTL_CPU_FENCE;
	I915_WRITE(FBC_CONTROL2, fbc_ctl2);
	I915_WRITE(FBC_FENCE_OFF, crtc->y);

	/* enable it... */
	fbc_ctl = FBC_CTL_EN | FBC_CTL_PERIODIC;
	if (IS_I945GM(dev))
		fbc_ctl |= FBC_CTL_C3_IDLE; /* 945 needs special SR handling */
	fbc_ctl |= (dev_priv->cfb_pitch & 0xff) << FBC_CTL_STRIDE_SHIFT;
	fbc_ctl |= (interval & 0x2fff) << FBC_CTL_INTERVAL_SHIFT;
	if (obj->tiling_mode != I915_TILING_NONE)
		fbc_ctl |= dev_priv->cfb_fence;
	I915_WRITE(FBC_CONTROL, fbc_ctl);

	DRM_DEBUG_KMS("enabled FBC, pitch %ld, yoff %d, plane %d, ",
		      dev_priv->cfb_pitch, crtc->y, dev_priv->cfb_plane);
}

void i8xx_disable_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 fbc_ctl;

	/* Disable compression */
	fbc_ctl = I915_READ(FBC_CONTROL);
	if ((fbc_ctl & FBC_CTL_EN) == 0)
		return;

	fbc_ctl &= ~FBC_CTL_EN;
	I915_WRITE(FBC_CONTROL, fbc_ctl);

	/* Wait for compressing bit to clear */
	if (wait_for((I915_READ(FBC_STATUS) & FBC_STAT_COMPRESSING) == 0, 10)) {
		DRM_DEBUG_KMS("FBC idle timed out\n");
		return;
	}

	DRM_DEBUG_KMS("disabled FBC\n");
}

static bool i8xx_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(FBC_CONTROL) & FBC_CTL_EN;
}

static void g4x_enable_fbc(struct drm_crtc *crtc, unsigned long interval)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int plane = intel_crtc->plane == 0 ? DPFC_CTL_PLANEA : DPFC_CTL_PLANEB;
	unsigned long stall_watermark = 200;
	u32 dpfc_ctl;

	dpfc_ctl = I915_READ(DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		if (dev_priv->cfb_pitch == dev_priv->cfb_pitch / 64 - 1 &&
		    dev_priv->cfb_fence == obj->fence_reg &&
		    dev_priv->cfb_plane == intel_crtc->plane &&
		    dev_priv->cfb_y == crtc->y)
			return;

		I915_WRITE(DPFC_CONTROL, dpfc_ctl & ~DPFC_CTL_EN);
		POSTING_READ(DPFC_CONTROL);
		intel_wait_for_vblank(dev, intel_crtc->pipe);
	}

	dev_priv->cfb_pitch = (dev_priv->cfb_pitch / 64) - 1;
	dev_priv->cfb_fence = obj->fence_reg;
	dev_priv->cfb_plane = intel_crtc->plane;
	dev_priv->cfb_y = crtc->y;

	dpfc_ctl = plane | DPFC_SR_EN | DPFC_CTL_LIMIT_1X;
	if (obj->tiling_mode != I915_TILING_NONE) {
		dpfc_ctl |= DPFC_CTL_FENCE_EN | dev_priv->cfb_fence;
		I915_WRITE(DPFC_CHICKEN, DPFC_HT_MODIFY);
	} else {
		I915_WRITE(DPFC_CHICKEN, ~DPFC_HT_MODIFY);
	}

	I915_WRITE(DPFC_RECOMP_CTL, DPFC_RECOMP_STALL_EN |
		   (stall_watermark << DPFC_RECOMP_STALL_WM_SHIFT) |
		   (interval << DPFC_RECOMP_TIMER_COUNT_SHIFT));
	I915_WRITE(DPFC_FENCE_YOFF, crtc->y);

	/* enable it... */
	I915_WRITE(DPFC_CONTROL, I915_READ(DPFC_CONTROL) | DPFC_CTL_EN);

	DRM_DEBUG_KMS("enabled fbc on plane %d\n", intel_crtc->plane);
}

void g4x_disable_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpfc_ctl;

	/* Disable compression */
	dpfc_ctl = I915_READ(DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		I915_WRITE(DPFC_CONTROL, dpfc_ctl);

		DRM_DEBUG_KMS("disabled FBC\n");
	}
}

static bool g4x_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(DPFC_CONTROL) & DPFC_CTL_EN;
}

static void ironlake_enable_fbc(struct drm_crtc *crtc, unsigned long interval)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb->obj;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int plane = intel_crtc->plane == 0 ? DPFC_CTL_PLANEA : DPFC_CTL_PLANEB;
	unsigned long stall_watermark = 200;
	u32 dpfc_ctl;

	dpfc_ctl = I915_READ(ILK_DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		if (dev_priv->cfb_pitch == dev_priv->cfb_pitch / 64 - 1 &&
		    dev_priv->cfb_fence == obj->fence_reg &&
		    dev_priv->cfb_plane == intel_crtc->plane &&
		    dev_priv->cfb_offset == obj->gtt_offset &&
		    dev_priv->cfb_y == crtc->y)
			return;

		I915_WRITE(ILK_DPFC_CONTROL, dpfc_ctl & ~DPFC_CTL_EN);
		POSTING_READ(ILK_DPFC_CONTROL);
		intel_wait_for_vblank(dev, intel_crtc->pipe);
	}

	dev_priv->cfb_pitch = (dev_priv->cfb_pitch / 64) - 1;
	dev_priv->cfb_fence = obj->fence_reg;
	dev_priv->cfb_plane = intel_crtc->plane;
	dev_priv->cfb_offset = obj->gtt_offset;
	dev_priv->cfb_y = crtc->y;

	dpfc_ctl &= DPFC_RESERVED;
	dpfc_ctl |= (plane | DPFC_CTL_LIMIT_1X);
	if (obj->tiling_mode != I915_TILING_NONE) {
		dpfc_ctl |= (DPFC_CTL_FENCE_EN | dev_priv->cfb_fence);
		I915_WRITE(ILK_DPFC_CHICKEN, DPFC_HT_MODIFY);
	} else {
		I915_WRITE(ILK_DPFC_CHICKEN, ~DPFC_HT_MODIFY);
	}

	I915_WRITE(ILK_DPFC_RECOMP_CTL, DPFC_RECOMP_STALL_EN |
		   (stall_watermark << DPFC_RECOMP_STALL_WM_SHIFT) |
		   (interval << DPFC_RECOMP_TIMER_COUNT_SHIFT));
	I915_WRITE(ILK_DPFC_FENCE_YOFF, crtc->y);
	I915_WRITE(ILK_FBC_RT_BASE, obj->gtt_offset | ILK_FBC_RT_VALID);
	/* enable it... */
	I915_WRITE(ILK_DPFC_CONTROL, dpfc_ctl | DPFC_CTL_EN);

	DRM_DEBUG_KMS("enabled fbc on plane %d\n", intel_crtc->plane);
}

void ironlake_disable_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpfc_ctl;

	/* Disable compression */
	dpfc_ctl = I915_READ(ILK_DPFC_CONTROL);
	if (dpfc_ctl & DPFC_CTL_EN) {
		dpfc_ctl &= ~DPFC_CTL_EN;
		I915_WRITE(ILK_DPFC_CONTROL, dpfc_ctl);

		DRM_DEBUG_KMS("disabled FBC\n");
	}
}

static bool ironlake_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(ILK_DPFC_CONTROL) & DPFC_CTL_EN;
}

bool intel_fbc_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv->display.fbc_enabled)
		return false;

	return dev_priv->display.fbc_enabled(dev);
}

void intel_enable_fbc(struct drm_crtc *crtc, unsigned long interval)
{
	struct drm_i915_private *dev_priv = crtc->dev->dev_private;

	if (!dev_priv->display.enable_fbc)
		return;

	dev_priv->display.enable_fbc(crtc, interval);
}

void intel_disable_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv->display.disable_fbc)
		return;

	dev_priv->display.disable_fbc(dev);
}

/**
 * intel_update_fbc - enable/disable FBC as needed
 * @dev: the drm_device
 *
 * Set up the framebuffer compression hardware at mode set time.  We
 * enable it if possible:
 *   - plane A only (on pre-965)
 *   - no pixel mulitply/line duplication
 *   - no alpha buffer discard
 *   - no dual wide
 *   - framebuffer <= 2048 in width, 1536 in height
 *
 * We can't assume that any compression will take place (worst case),
 * so the compressed buffer has to be the same size as the uncompressed
 * one.  It also must reside (along with the line length buffer) in
 * stolen memory.
 *
 * We need to enable/disable FBC on a global basis.
 */
static void intel_update_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = NULL, *tmp_crtc;
	struct intel_crtc *intel_crtc;
	struct drm_framebuffer *fb;
	struct intel_framebuffer *intel_fb;
	struct drm_i915_gem_object *obj;

	DRM_DEBUG_KMS("\n");

	if (!i915_powersave)
		return;

	if (!I915_HAS_FBC(dev))
		return;

	/*
	 * If FBC is already on, we just have to verify that we can
	 * keep it that way...
	 * Need to disable if:
	 *   - more than one pipe is active
	 *   - changing FBC params (stride, fence, mode)
	 *   - new fb is too large to fit in compressed buffer
	 *   - going to an unsupported config (interlace, pixel multiply, etc.)
	 */
	list_for_each_entry(tmp_crtc, &dev->mode_config.crtc_list, head) {
		if (tmp_crtc->enabled) {
			if (crtc) {
				DRM_DEBUG_KMS("more than one pipe active, disabling compression\n");
				dev_priv->no_fbc_reason = FBC_MULTIPLE_PIPES;
				goto out_disable;
			}
			crtc = tmp_crtc;
		}
	}

	if (!crtc || crtc->fb == NULL) {
		DRM_DEBUG_KMS("no output, disabling\n");
		dev_priv->no_fbc_reason = FBC_NO_OUTPUT;
		goto out_disable;
	}

	intel_crtc = to_intel_crtc(crtc);
	fb = crtc->fb;
	intel_fb = to_intel_framebuffer(fb);
	obj = intel_fb->obj;

	if (intel_fb->obj->base.size > dev_priv->cfb_size) {
		DRM_DEBUG_KMS("framebuffer too large, disabling "
			      "compression\n");
		dev_priv->no_fbc_reason = FBC_STOLEN_TOO_SMALL;
		goto out_disable;
	}
	if ((crtc->mode.flags & DRM_MODE_FLAG_INTERLACE) ||
	    (crtc->mode.flags & DRM_MODE_FLAG_DBLSCAN)) {
		DRM_DEBUG_KMS("mode incompatible with compression, "
			      "disabling\n");
		dev_priv->no_fbc_reason = FBC_UNSUPPORTED_MODE;
		goto out_disable;
	}
	if ((crtc->mode.hdisplay > 2048) ||
	    (crtc->mode.vdisplay > 1536)) {
		DRM_DEBUG_KMS("mode too large for compression, disabling\n");
		dev_priv->no_fbc_reason = FBC_MODE_TOO_LARGE;
		goto out_disable;
	}
	if ((IS_I915GM(dev) || IS_I945GM(dev)) && intel_crtc->plane != 0) {
		DRM_DEBUG_KMS("plane not 0, disabling compression\n");
		dev_priv->no_fbc_reason = FBC_BAD_PLANE;
		goto out_disable;
	}
	if (obj->tiling_mode != I915_TILING_X) {
		DRM_DEBUG_KMS("framebuffer not tiled, disabling compression\n");
		dev_priv->no_fbc_reason = FBC_NOT_TILED;
		goto out_disable;
	}

	/* If the kernel debugger is active, always disable compression */
	if (in_dbg_master())
		goto out_disable;

	intel_enable_fbc(crtc, 500);
	return;

out_disable:
	/* Multiple disables should be harmless */
	if (intel_fbc_enabled(dev)) {
		DRM_DEBUG_KMS("unsupported config, disabling FBC\n");
		intel_disable_fbc(dev);
	}
}

int
intel_pin_and_fence_fb_obj(struct drm_device *dev,
			   struct drm_i915_gem_object *obj,
			   struct intel_ring_buffer *pipelined)
{
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

	ret = i915_gem_object_pin(obj, alignment, true);
	if (ret)
		return ret;

	ret = i915_gem_object_set_to_display_plane(obj, pipelined);
	if (ret)
		goto err_unpin;

	/* Install a fence for tiled scan-out. Pre-i965 always needs a
	 * fence, whereas 965+ only requires a fence if using
	 * framebuffer compression.  For simplicity, we always install
	 * a fence as the cost is not that onerous.
	 */
	if (obj->tiling_mode != I915_TILING_NONE) {
		ret = i915_gem_object_get_fence(obj, pipelined, false);
		if (ret)
			goto err_unpin;
	}

	return 0;

err_unpin:
	i915_gem_object_unpin(obj);
	return ret;
}

/* Assume fb object is pinned & idle & fenced and just update base pointers */
static int
intel_pipe_set_base_atomic(struct drm_crtc *crtc, struct drm_framebuffer *fb,
			   int x, int y, enum mode_set_atomic state)
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
		DRM_ERROR("Unknown color depth\n");
		return -EINVAL;
	}
	if (INTEL_INFO(dev)->gen >= 4) {
		if (obj->tiling_mode != I915_TILING_NONE)
			dspcntr |= DISPPLANE_TILED;
		else
			dspcntr &= ~DISPPLANE_TILED;
	}

	if (HAS_PCH_SPLIT(dev))
		/* must disable */
		dspcntr |= DISPPLANE_TRICKLE_FEED_DISABLE;

	I915_WRITE(reg, dspcntr);

	Start = obj->gtt_offset;
	Offset = y * fb->pitch + x * (fb->bits_per_pixel / 8);

	DRM_DEBUG_KMS("Writing base %08lX %08lX %d %d %d\n",
		      Start, Offset, x, y, fb->pitch);
	I915_WRITE(DSPSTRIDE(plane), fb->pitch);
	if (INTEL_INFO(dev)->gen >= 4) {
		I915_WRITE(DSPSURF(plane), Start);
		I915_WRITE(DSPTILEOFF(plane), (y << 16) | x);
		I915_WRITE(DSPADDR(plane), Offset);
	} else
		I915_WRITE(DSPADDR(plane), Start + Offset);
	POSTING_READ(reg);

	intel_update_fbc(dev);
	intel_increase_pllclock(crtc);

	return 0;
}

static int
intel_pipe_set_base(struct drm_crtc *crtc, int x, int y,
		    struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_master_private *master_priv;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int ret;

	/* no fb bound */
	if (!crtc->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	switch (intel_crtc->plane) {
	case 0:
	case 1:
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&dev->struct_mutex);
	ret = intel_pin_and_fence_fb_obj(dev,
					 to_intel_framebuffer(crtc->fb)->obj,
					 NULL);
	if (ret != 0) {
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	if (old_fb) {
		struct drm_i915_private *dev_priv = dev->dev_private;
		struct drm_i915_gem_object *obj = to_intel_framebuffer(old_fb)->obj;

		wait_event(dev_priv->pending_flip_queue,
			   atomic_read(&obj->pending_flip) == 0);

		/* Big Hammer, we also need to ensure that any pending
		 * MI_WAIT_FOR_EVENT inside a user batch buffer on the
		 * current scanout is retired before unpinning the old
		 * framebuffer.
		 */
		ret = i915_gem_object_flush_gpu(obj, false);
		if (ret) {
			i915_gem_object_unpin(to_intel_framebuffer(crtc->fb)->obj);
			mutex_unlock(&dev->struct_mutex);
			return ret;
		}
	}

	ret = intel_pipe_set_base_atomic(crtc, crtc->fb, x, y,
					 LEAVE_ATOMIC_MODE_SET);
	if (ret) {
		i915_gem_object_unpin(to_intel_framebuffer(crtc->fb)->obj);
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	if (old_fb)
		i915_gem_object_unpin(to_intel_framebuffer(old_fb)->obj);

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
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_NONE | FDI_TX_ENHANCE_FRAME_ENABLE;
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
}

/* The FDI link training functions for ILK/Ibexpeak. */
static void ironlake_fdi_link_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 reg, temp, tries;

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
	I915_WRITE(FDI_RX_CHICKEN(pipe), FDI_RX_PHASE_SYNC_POINTER_ENABLE);

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

static const int const snb_b_fdi_train_param [] = {
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

	for (i = 0; i < 4; i++ ) {
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

		if (temp & FDI_RX_BIT_LOCK) {
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

	for (i = 0; i < 4; i++ ) {
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

static void ironlake_fdi_enable(struct drm_crtc *crtc)
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

	/* Enable CPU FDI TX PLL, always on for Ironlake */
	reg = FDI_TX_CTL(pipe);
	temp = I915_READ(reg);
	if ((temp & FDI_TX_PLL_ENABLE) == 0) {
		I915_WRITE(reg, temp | FDI_TX_PLL_ENABLE);

		POSTING_READ(reg);
		udelay(100);
	}
}

static void intel_flush_display_plane(struct drm_device *dev,
				      int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg = DSPADDR(plane);
	I915_WRITE(reg, I915_READ(reg));
}

/*
 * When we disable a pipe, we need to clear any pending scanline wait events
 * to avoid hanging the ring, which we assume we are waiting on.
 */
static void intel_clear_scanline_wait(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	u32 tmp;

	if (IS_GEN2(dev))
		/* Can't break the hang on i8xx */
		return;

	ring = &dev_priv->render_ring;
	tmp = I915_READ_CTL(ring);
	if (tmp & RING_WAIT)
		I915_WRITE_CTL(ring, tmp);
}

static void intel_crtc_wait_for_pending_flips(struct drm_crtc *crtc)
{
	struct drm_i915_gem_object *obj;
	struct drm_i915_private *dev_priv;

	if (crtc->fb == NULL)
		return;

	obj = to_intel_framebuffer(crtc->fb)->obj;
	dev_priv = crtc->dev->dev_private;
	wait_event(dev_priv->pending_flip_queue,
		   atomic_read(&obj->pending_flip) == 0);
}

static void ironlake_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;
	u32 reg, temp;

	if (intel_crtc->active)
		return;

	intel_crtc->active = true;
	intel_update_watermarks(dev);

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
		temp = I915_READ(PCH_LVDS);
		if ((temp & LVDS_PORT_EN) == 0)
			I915_WRITE(PCH_LVDS, temp | LVDS_PORT_EN);
	}

	ironlake_fdi_enable(crtc);

	/* Enable panel fitting for LVDS */
	if (dev_priv->pch_pf_size &&
	    (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS) || HAS_eDP)) {
		/* Force use of hard-coded filter coefficients
		 * as some pre-programmed values are broken,
		 * e.g. x201.
		 */
		I915_WRITE(pipe ? PFB_CTL_1 : PFA_CTL_1,
			   PF_ENABLE | PF_FILTER_MED_3x3);
		I915_WRITE(pipe ? PFB_WIN_POS : PFA_WIN_POS,
			   dev_priv->pch_pf_pos);
		I915_WRITE(pipe ? PFB_WIN_SZ : PFA_WIN_SZ,
			   dev_priv->pch_pf_size);
	}

	/* Enable CPU pipe */
	reg = PIPECONF(pipe);
	temp = I915_READ(reg);
	if ((temp & PIPECONF_ENABLE) == 0) {
		I915_WRITE(reg, temp | PIPECONF_ENABLE);
		POSTING_READ(reg);
		intel_wait_for_vblank(dev, intel_crtc->pipe);
	}

	/* configure and enable CPU plane */
	reg = DSPCNTR(plane);
	temp = I915_READ(reg);
	if ((temp & DISPLAY_PLANE_ENABLE) == 0) {
		I915_WRITE(reg, temp | DISPLAY_PLANE_ENABLE);
		intel_flush_display_plane(dev, plane);
	}

	/* For PCH output, training FDI link */
	if (IS_GEN6(dev))
		gen6_fdi_link_train(crtc);
	else
		ironlake_fdi_link_train(crtc);

	/* enable PCH DPLL */
	reg = PCH_DPLL(pipe);
	temp = I915_READ(reg);
	if ((temp & DPLL_VCO_ENABLE) == 0) {
		I915_WRITE(reg, temp | DPLL_VCO_ENABLE);
		POSTING_READ(reg);
		udelay(200);
	}

	if (HAS_PCH_CPT(dev)) {
		/* Be sure PCH DPLL SEL is set */
		temp = I915_READ(PCH_DPLL_SEL);
		if (pipe == 0 && (temp & TRANSA_DPLL_ENABLE) == 0)
			temp |= (TRANSA_DPLL_ENABLE | TRANSA_DPLLA_SEL);
		else if (pipe == 1 && (temp & TRANSB_DPLL_ENABLE) == 0)
			temp |= (TRANSB_DPLL_ENABLE | TRANSB_DPLLB_SEL);
		I915_WRITE(PCH_DPLL_SEL, temp);
	}

	/* set transcoder timing */
	I915_WRITE(TRANS_HTOTAL(pipe), I915_READ(HTOTAL(pipe)));
	I915_WRITE(TRANS_HBLANK(pipe), I915_READ(HBLANK(pipe)));
	I915_WRITE(TRANS_HSYNC(pipe),  I915_READ(HSYNC(pipe)));

	I915_WRITE(TRANS_VTOTAL(pipe), I915_READ(VTOTAL(pipe)));
	I915_WRITE(TRANS_VBLANK(pipe), I915_READ(VBLANK(pipe)));
	I915_WRITE(TRANS_VSYNC(pipe),  I915_READ(VSYNC(pipe)));

	intel_fdi_normal_train(crtc);

	/* For PCH DP, enable TRANS_DP_CTL */
	if (HAS_PCH_CPT(dev) &&
	    intel_pipe_has_type(crtc, INTEL_OUTPUT_DISPLAYPORT)) {
		reg = TRANS_DP_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~(TRANS_DP_PORT_SEL_MASK |
			  TRANS_DP_SYNC_MASK);
		temp |= (TRANS_DP_OUTPUT_ENABLE |
			 TRANS_DP_ENH_FRAMING);

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

	/* enable PCH transcoder */
	reg = TRANSCONF(pipe);
	temp = I915_READ(reg);
	/*
	 * make the BPC in transcoder be consistent with
	 * that in pipeconf reg.
	 */
	temp &= ~PIPE_BPC_MASK;
	temp |= I915_READ(PIPECONF(pipe)) & PIPE_BPC_MASK;
	I915_WRITE(reg, temp | TRANS_ENABLE);
	if (wait_for(I915_READ(reg) & TRANS_STATE_ENABLE, 100))
		DRM_ERROR("failed to enable transcoder %d\n", pipe);

	intel_crtc_load_lut(crtc);
	intel_update_fbc(dev);
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

	/* Disable display plane */
	reg = DSPCNTR(plane);
	temp = I915_READ(reg);
	if (temp & DISPLAY_PLANE_ENABLE) {
		I915_WRITE(reg, temp & ~DISPLAY_PLANE_ENABLE);
		intel_flush_display_plane(dev, plane);
	}

	if (dev_priv->cfb_plane == plane &&
	    dev_priv->display.disable_fbc)
		dev_priv->display.disable_fbc(dev);

	/* disable cpu pipe, disable after all planes disabled */
	reg = PIPECONF(pipe);
	temp = I915_READ(reg);
	if (temp & PIPECONF_ENABLE) {
		I915_WRITE(reg, temp & ~PIPECONF_ENABLE);
		POSTING_READ(reg);
		/* wait for cpu pipe off, pipe state */
		intel_wait_for_pipe_off(dev, intel_crtc->pipe);
	}

	/* Disable PF */
	I915_WRITE(pipe ? PFB_CTL_1 : PFA_CTL_1, 0);
	I915_WRITE(pipe ? PFB_WIN_SZ : PFA_WIN_SZ, 0);

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
	if (HAS_PCH_IBX(dev))
		I915_WRITE(FDI_RX_CHICKEN(pipe),
			   I915_READ(FDI_RX_CHICKEN(pipe) &
				     ~FDI_RX_PHASE_SYNC_POINTER_ENABLE));

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

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
		temp = I915_READ(PCH_LVDS);
		if (temp & LVDS_PORT_EN) {
			I915_WRITE(PCH_LVDS, temp & ~LVDS_PORT_EN);
			POSTING_READ(PCH_LVDS);
			udelay(100);
		}
	}

	/* disable PCH transcoder */
	reg = TRANSCONF(plane);
	temp = I915_READ(reg);
	if (temp & TRANS_ENABLE) {
		I915_WRITE(reg, temp & ~TRANS_ENABLE);
		/* wait for PCH transcoder off, transcoder state */
		if (wait_for((I915_READ(reg) & TRANS_STATE_ENABLE) == 0, 50))
			DRM_ERROR("failed to disable transcoder\n");
	}

	if (HAS_PCH_CPT(dev)) {
		/* disable TRANS_DP_CTL */
		reg = TRANS_DP_CTL(pipe);
		temp = I915_READ(reg);
		temp &= ~(TRANS_DP_OUTPUT_ENABLE | TRANS_DP_PORT_SEL_MASK);
		I915_WRITE(reg, temp);

		/* disable DPLL_SEL */
		temp = I915_READ(PCH_DPLL_SEL);
		if (pipe == 0)
			temp &= ~(TRANSA_DPLL_ENABLE | TRANSA_DPLLB_SEL);
		else
			temp &= ~(TRANSB_DPLL_ENABLE | TRANSB_DPLLB_SEL);
		I915_WRITE(PCH_DPLL_SEL, temp);
	}

	/* disable PCH DPLL */
	reg = PCH_DPLL(pipe);
	temp = I915_READ(reg);
	I915_WRITE(reg, temp & ~DPLL_VCO_ENABLE);

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
	intel_update_fbc(dev);
	intel_clear_scanline_wait(dev);
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

static void intel_crtc_dpms_overlay(struct intel_crtc *intel_crtc, bool enable)
{
	if (!enable && intel_crtc->overlay) {
		struct drm_device *dev = intel_crtc->base.dev;

		mutex_lock(&dev->struct_mutex);
		(void) intel_overlay_switch_off(intel_crtc->overlay, false);
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
	u32 reg, temp;

	if (intel_crtc->active)
		return;

	intel_crtc->active = true;
	intel_update_watermarks(dev);

	/* Enable the DPLL */
	reg = DPLL(pipe);
	temp = I915_READ(reg);
	if ((temp & DPLL_VCO_ENABLE) == 0) {
		I915_WRITE(reg, temp);

		/* Wait for the clocks to stabilize. */
		POSTING_READ(reg);
		udelay(150);

		I915_WRITE(reg, temp | DPLL_VCO_ENABLE);

		/* Wait for the clocks to stabilize. */
		POSTING_READ(reg);
		udelay(150);

		I915_WRITE(reg, temp | DPLL_VCO_ENABLE);

		/* Wait for the clocks to stabilize. */
		POSTING_READ(reg);
		udelay(150);
	}

	/* Enable the pipe */
	reg = PIPECONF(pipe);
	temp = I915_READ(reg);
	if ((temp & PIPECONF_ENABLE) == 0)
		I915_WRITE(reg, temp | PIPECONF_ENABLE);

	/* Enable the plane */
	reg = DSPCNTR(plane);
	temp = I915_READ(reg);
	if ((temp & DISPLAY_PLANE_ENABLE) == 0) {
		I915_WRITE(reg, temp | DISPLAY_PLANE_ENABLE);
		intel_flush_display_plane(dev, plane);
	}

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
	u32 reg, temp;

	if (!intel_crtc->active)
		return;

	/* Give the overlay scaler a chance to disable if it's on this pipe */
	intel_crtc_wait_for_pending_flips(crtc);
	drm_vblank_off(dev, pipe);
	intel_crtc_dpms_overlay(intel_crtc, false);
	intel_crtc_update_cursor(crtc, false);

	if (dev_priv->cfb_plane == plane &&
	    dev_priv->display.disable_fbc)
		dev_priv->display.disable_fbc(dev);

	/* Disable display plane */
	reg = DSPCNTR(plane);
	temp = I915_READ(reg);
	if (temp & DISPLAY_PLANE_ENABLE) {
		I915_WRITE(reg, temp & ~DISPLAY_PLANE_ENABLE);
		/* Flush the plane changes */
		intel_flush_display_plane(dev, plane);

		/* Wait for vblank for the disable to take effect */
		if (IS_GEN2(dev))
			intel_wait_for_vblank(dev, pipe);
	}

	/* Don't disable pipe A or pipe A PLLs if needed */
	if (pipe == 0 && (dev_priv->quirks & QUIRK_PIPEA_FORCE))
		goto done;

	/* Next, disable display pipes */
	reg = PIPECONF(pipe);
	temp = I915_READ(reg);
	if (temp & PIPECONF_ENABLE) {
		I915_WRITE(reg, temp & ~PIPECONF_ENABLE);

		/* Wait for the pipe to turn off */
		POSTING_READ(reg);
		intel_wait_for_pipe_off(dev, pipe);
	}

	reg = DPLL(pipe);
	temp = I915_READ(reg);
	if (temp & DPLL_VCO_ENABLE) {
		I915_WRITE(reg, temp & ~DPLL_VCO_ENABLE);

		/* Wait for the clocks to turn off. */
		POSTING_READ(reg);
		udelay(150);
	}

done:
	intel_crtc->active = false;
	intel_update_fbc(dev);
	intel_update_watermarks(dev);
	intel_clear_scanline_wait(dev);
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
		DRM_ERROR("Can't update pipe %d in SAREA\n", pipe);
		break;
	}
}

static void intel_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	struct drm_device *dev = crtc->dev;

	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);

	if (crtc->fb) {
		mutex_lock(&dev->struct_mutex);
		i915_gem_object_unpin(to_intel_framebuffer(crtc->fb)->obj);
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

void intel_encoder_prepare (struct drm_encoder *encoder)
{
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
	/* lvds has its own version of prepare see intel_lvds_prepare */
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_OFF);
}

void intel_encoder_commit (struct drm_encoder *encoder)
{
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
	/* lvds has its own version of commit see intel_lvds_commit */
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_ON);
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

	/* XXX some encoders set the crtcinfo, others don't.
	 * Obviously we need some form of conflict resolution here...
	 */
	if (adjusted_mode->crtc_htotal == 0)
		drm_mode_set_crtcinfo(adjusted_mode, 0);

	return true;
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

#define DATA_N 0x800000
#define LINK_N 0x80000

static void
ironlake_compute_m_n(int bits_per_pixel, int nlanes, int pixel_clock,
		     int link_clock, struct fdi_m_n *m_n)
{
	u64 temp;

	m_n->tu = 64; /* default size */

	temp = (u64) DATA_N * pixel_clock;
	temp = div_u64(temp, link_clock);
	m_n->gmch_m = div_u64(temp * bits_per_pixel, nlanes);
	m_n->gmch_m >>= 3; /* convert to bytes_per_pixel */
	m_n->gmch_n = DATA_N;
	fdi_reduce_ratio(&m_n->gmch_m, &m_n->gmch_n);

	temp = (u64) LINK_N * pixel_clock;
	m_n->link_m = div_u64(temp, link_clock);
	m_n->link_n = LINK_N;
	fdi_reduce_ratio(&m_n->link_m, &m_n->link_n);
}


struct intel_watermark_params {
	unsigned long fifo_size;
	unsigned long max_wm;
	unsigned long default_wm;
	unsigned long guard_size;
	unsigned long cacheline_size;
};

/* Pineview has different values for various configs */
static struct intel_watermark_params pineview_display_wm = {
	PINEVIEW_DISPLAY_FIFO,
	PINEVIEW_MAX_WM,
	PINEVIEW_DFT_WM,
	PINEVIEW_GUARD_WM,
	PINEVIEW_FIFO_LINE_SIZE
};
static struct intel_watermark_params pineview_display_hplloff_wm = {
	PINEVIEW_DISPLAY_FIFO,
	PINEVIEW_MAX_WM,
	PINEVIEW_DFT_HPLLOFF_WM,
	PINEVIEW_GUARD_WM,
	PINEVIEW_FIFO_LINE_SIZE
};
static struct intel_watermark_params pineview_cursor_wm = {
	PINEVIEW_CURSOR_FIFO,
	PINEVIEW_CURSOR_MAX_WM,
	PINEVIEW_CURSOR_DFT_WM,
	PINEVIEW_CURSOR_GUARD_WM,
	PINEVIEW_FIFO_LINE_SIZE,
};
static struct intel_watermark_params pineview_cursor_hplloff_wm = {
	PINEVIEW_CURSOR_FIFO,
	PINEVIEW_CURSOR_MAX_WM,
	PINEVIEW_CURSOR_DFT_WM,
	PINEVIEW_CURSOR_GUARD_WM,
	PINEVIEW_FIFO_LINE_SIZE
};
static struct intel_watermark_params g4x_wm_info = {
	G4X_FIFO_SIZE,
	G4X_MAX_WM,
	G4X_MAX_WM,
	2,
	G4X_FIFO_LINE_SIZE,
};
static struct intel_watermark_params g4x_cursor_wm_info = {
	I965_CURSOR_FIFO,
	I965_CURSOR_MAX_WM,
	I965_CURSOR_DFT_WM,
	2,
	G4X_FIFO_LINE_SIZE,
};
static struct intel_watermark_params i965_cursor_wm_info = {
	I965_CURSOR_FIFO,
	I965_CURSOR_MAX_WM,
	I965_CURSOR_DFT_WM,
	2,
	I915_FIFO_LINE_SIZE,
};
static struct intel_watermark_params i945_wm_info = {
	I945_FIFO_SIZE,
	I915_MAX_WM,
	1,
	2,
	I915_FIFO_LINE_SIZE
};
static struct intel_watermark_params i915_wm_info = {
	I915_FIFO_SIZE,
	I915_MAX_WM,
	1,
	2,
	I915_FIFO_LINE_SIZE
};
static struct intel_watermark_params i855_wm_info = {
	I855GM_FIFO_SIZE,
	I915_MAX_WM,
	1,
	2,
	I830_FIFO_LINE_SIZE
};
static struct intel_watermark_params i830_wm_info = {
	I830_FIFO_SIZE,
	I915_MAX_WM,
	1,
	2,
	I830_FIFO_LINE_SIZE
};

static struct intel_watermark_params ironlake_display_wm_info = {
	ILK_DISPLAY_FIFO,
	ILK_DISPLAY_MAXWM,
	ILK_DISPLAY_DFTWM,
	2,
	ILK_FIFO_LINE_SIZE
};

static struct intel_watermark_params ironlake_cursor_wm_info = {
	ILK_CURSOR_FIFO,
	ILK_CURSOR_MAXWM,
	ILK_CURSOR_DFTWM,
	2,
	ILK_FIFO_LINE_SIZE
};

static struct intel_watermark_params ironlake_display_srwm_info = {
	ILK_DISPLAY_SR_FIFO,
	ILK_DISPLAY_MAX_SRWM,
	ILK_DISPLAY_DFT_SRWM,
	2,
	ILK_FIFO_LINE_SIZE
};

static struct intel_watermark_params ironlake_cursor_srwm_info = {
	ILK_CURSOR_SR_FIFO,
	ILK_CURSOR_MAX_SRWM,
	ILK_CURSOR_DFT_SRWM,
	2,
	ILK_FIFO_LINE_SIZE
};

/**
 * intel_calculate_wm - calculate watermark level
 * @clock_in_khz: pixel clock
 * @wm: chip FIFO params
 * @pixel_size: display pixel size
 * @latency_ns: memory latency for the platform
 *
 * Calculate the watermark level (the level at which the display plane will
 * start fetching from memory again).  Each chip has a different display
 * FIFO size and allocation, so the caller needs to figure that out and pass
 * in the correct intel_watermark_params structure.
 *
 * As the pixel clock runs, the FIFO will be drained at a rate that depends
 * on the pixel size.  When it reaches the watermark level, it'll start
 * fetching FIFO line sized based chunks from memory until the FIFO fills
 * past the watermark point.  If the FIFO drains completely, a FIFO underrun
 * will occur, and a display engine hang could result.
 */
static unsigned long intel_calculate_wm(unsigned long clock_in_khz,
					struct intel_watermark_params *wm,
					int pixel_size,
					unsigned long latency_ns)
{
	long entries_required, wm_size;

	/*
	 * Note: we need to make sure we don't overflow for various clock &
	 * latency values.
	 * clocks go from a few thousand to several hundred thousand.
	 * latency is usually a few thousand
	 */
	entries_required = ((clock_in_khz / 1000) * pixel_size * latency_ns) /
		1000;
	entries_required = DIV_ROUND_UP(entries_required, wm->cacheline_size);

	DRM_DEBUG_KMS("FIFO entries required for mode: %d\n", entries_required);

	wm_size = wm->fifo_size - (entries_required + wm->guard_size);

	DRM_DEBUG_KMS("FIFO watermark level: %d\n", wm_size);

	/* Don't promote wm_size to unsigned... */
	if (wm_size > (long)wm->max_wm)
		wm_size = wm->max_wm;
	if (wm_size <= 0)
		wm_size = wm->default_wm;
	return wm_size;
}

struct cxsr_latency {
	int is_desktop;
	int is_ddr3;
	unsigned long fsb_freq;
	unsigned long mem_freq;
	unsigned long display_sr;
	unsigned long display_hpll_disable;
	unsigned long cursor_sr;
	unsigned long cursor_hpll_disable;
};

static const struct cxsr_latency cxsr_latency_table[] = {
	{1, 0, 800, 400, 3382, 33382, 3983, 33983},    /* DDR2-400 SC */
	{1, 0, 800, 667, 3354, 33354, 3807, 33807},    /* DDR2-667 SC */
	{1, 0, 800, 800, 3347, 33347, 3763, 33763},    /* DDR2-800 SC */
	{1, 1, 800, 667, 6420, 36420, 6873, 36873},    /* DDR3-667 SC */
	{1, 1, 800, 800, 5902, 35902, 6318, 36318},    /* DDR3-800 SC */

	{1, 0, 667, 400, 3400, 33400, 4021, 34021},    /* DDR2-400 SC */
	{1, 0, 667, 667, 3372, 33372, 3845, 33845},    /* DDR2-667 SC */
	{1, 0, 667, 800, 3386, 33386, 3822, 33822},    /* DDR2-800 SC */
	{1, 1, 667, 667, 6438, 36438, 6911, 36911},    /* DDR3-667 SC */
	{1, 1, 667, 800, 5941, 35941, 6377, 36377},    /* DDR3-800 SC */

	{1, 0, 400, 400, 3472, 33472, 4173, 34173},    /* DDR2-400 SC */
	{1, 0, 400, 667, 3443, 33443, 3996, 33996},    /* DDR2-667 SC */
	{1, 0, 400, 800, 3430, 33430, 3946, 33946},    /* DDR2-800 SC */
	{1, 1, 400, 667, 6509, 36509, 7062, 37062},    /* DDR3-667 SC */
	{1, 1, 400, 800, 5985, 35985, 6501, 36501},    /* DDR3-800 SC */

	{0, 0, 800, 400, 3438, 33438, 4065, 34065},    /* DDR2-400 SC */
	{0, 0, 800, 667, 3410, 33410, 3889, 33889},    /* DDR2-667 SC */
	{0, 0, 800, 800, 3403, 33403, 3845, 33845},    /* DDR2-800 SC */
	{0, 1, 800, 667, 6476, 36476, 6955, 36955},    /* DDR3-667 SC */
	{0, 1, 800, 800, 5958, 35958, 6400, 36400},    /* DDR3-800 SC */

	{0, 0, 667, 400, 3456, 33456, 4103, 34106},    /* DDR2-400 SC */
	{0, 0, 667, 667, 3428, 33428, 3927, 33927},    /* DDR2-667 SC */
	{0, 0, 667, 800, 3443, 33443, 3905, 33905},    /* DDR2-800 SC */
	{0, 1, 667, 667, 6494, 36494, 6993, 36993},    /* DDR3-667 SC */
	{0, 1, 667, 800, 5998, 35998, 6460, 36460},    /* DDR3-800 SC */

	{0, 0, 400, 400, 3528, 33528, 4255, 34255},    /* DDR2-400 SC */
	{0, 0, 400, 667, 3500, 33500, 4079, 34079},    /* DDR2-667 SC */
	{0, 0, 400, 800, 3487, 33487, 4029, 34029},    /* DDR2-800 SC */
	{0, 1, 400, 667, 6566, 36566, 7145, 37145},    /* DDR3-667 SC */
	{0, 1, 400, 800, 6042, 36042, 6584, 36584},    /* DDR3-800 SC */
};

static const struct cxsr_latency *intel_get_cxsr_latency(int is_desktop,
							 int is_ddr3,
							 int fsb,
							 int mem)
{
	const struct cxsr_latency *latency;
	int i;

	if (fsb == 0 || mem == 0)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(cxsr_latency_table); i++) {
		latency = &cxsr_latency_table[i];
		if (is_desktop == latency->is_desktop &&
		    is_ddr3 == latency->is_ddr3 &&
		    fsb == latency->fsb_freq && mem == latency->mem_freq)
			return latency;
	}

	DRM_DEBUG_KMS("Unknown FSB/MEM found, disable CxSR\n");

	return NULL;
}

static void pineview_disable_cxsr(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* deactivate cxsr */
	I915_WRITE(DSPFW3, I915_READ(DSPFW3) & ~PINEVIEW_SELF_REFRESH_EN);
}

/*
 * Latency for FIFO fetches is dependent on several factors:
 *   - memory configuration (speed, channels)
 *   - chipset
 *   - current MCH state
 * It can be fairly high in some situations, so here we assume a fairly
 * pessimal value.  It's a tradeoff between extra memory fetches (if we
 * set this value too high, the FIFO will fetch frequently to stay full)
 * and power consumption (set it too low to save power and we might see
 * FIFO underruns and display "flicker").
 *
 * A value of 5us seems to be a good balance; safe for very low end
 * platforms but not overly aggressive on lower latency configs.
 */
static const int latency_ns = 5000;

static int i9xx_get_fifo_size(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	size = dsparb & 0x7f;
	if (plane)
		size = ((dsparb >> DSPARB_CSTART_SHIFT) & 0x7f) - size;

	DRM_DEBUG_KMS("FIFO size - (0x%08x) %s: %d\n", dsparb,
		      plane ? "B" : "A", size);

	return size;
}

static int i85x_get_fifo_size(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	size = dsparb & 0x1ff;
	if (plane)
		size = ((dsparb >> DSPARB_BEND_SHIFT) & 0x1ff) - size;
	size >>= 1; /* Convert to cachelines */

	DRM_DEBUG_KMS("FIFO size - (0x%08x) %s: %d\n", dsparb,
		      plane ? "B" : "A", size);

	return size;
}

static int i845_get_fifo_size(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	size = dsparb & 0x7f;
	size >>= 2; /* Convert to cachelines */

	DRM_DEBUG_KMS("FIFO size - (0x%08x) %s: %d\n", dsparb,
		      plane ? "B" : "A",
		      size);

	return size;
}

static int i830_get_fifo_size(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	size = dsparb & 0x7f;
	size >>= 1; /* Convert to cachelines */

	DRM_DEBUG_KMS("FIFO size - (0x%08x) %s: %d\n", dsparb,
		      plane ? "B" : "A", size);

	return size;
}

static void pineview_update_wm(struct drm_device *dev,  int planea_clock,
			       int planeb_clock, int sr_hdisplay, int unused,
			       int pixel_size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	const struct cxsr_latency *latency;
	u32 reg;
	unsigned long wm;
	int sr_clock;

	latency = intel_get_cxsr_latency(IS_PINEVIEW_G(dev), dev_priv->is_ddr3,
					 dev_priv->fsb_freq, dev_priv->mem_freq);
	if (!latency) {
		DRM_DEBUG_KMS("Unknown FSB/MEM found, disable CxSR\n");
		pineview_disable_cxsr(dev);
		return;
	}

	if (!planea_clock || !planeb_clock) {
		sr_clock = planea_clock ? planea_clock : planeb_clock;

		/* Display SR */
		wm = intel_calculate_wm(sr_clock, &pineview_display_wm,
					pixel_size, latency->display_sr);
		reg = I915_READ(DSPFW1);
		reg &= ~DSPFW_SR_MASK;
		reg |= wm << DSPFW_SR_SHIFT;
		I915_WRITE(DSPFW1, reg);
		DRM_DEBUG_KMS("DSPFW1 register is %x\n", reg);

		/* cursor SR */
		wm = intel_calculate_wm(sr_clock, &pineview_cursor_wm,
					pixel_size, latency->cursor_sr);
		reg = I915_READ(DSPFW3);
		reg &= ~DSPFW_CURSOR_SR_MASK;
		reg |= (wm & 0x3f) << DSPFW_CURSOR_SR_SHIFT;
		I915_WRITE(DSPFW3, reg);

		/* Display HPLL off SR */
		wm = intel_calculate_wm(sr_clock, &pineview_display_hplloff_wm,
					pixel_size, latency->display_hpll_disable);
		reg = I915_READ(DSPFW3);
		reg &= ~DSPFW_HPLL_SR_MASK;
		reg |= wm & DSPFW_HPLL_SR_MASK;
		I915_WRITE(DSPFW3, reg);

		/* cursor HPLL off SR */
		wm = intel_calculate_wm(sr_clock, &pineview_cursor_hplloff_wm,
					pixel_size, latency->cursor_hpll_disable);
		reg = I915_READ(DSPFW3);
		reg &= ~DSPFW_HPLL_CURSOR_MASK;
		reg |= (wm & 0x3f) << DSPFW_HPLL_CURSOR_SHIFT;
		I915_WRITE(DSPFW3, reg);
		DRM_DEBUG_KMS("DSPFW3 register is %x\n", reg);

		/* activate cxsr */
		I915_WRITE(DSPFW3,
			   I915_READ(DSPFW3) | PINEVIEW_SELF_REFRESH_EN);
		DRM_DEBUG_KMS("Self-refresh is enabled\n");
	} else {
		pineview_disable_cxsr(dev);
		DRM_DEBUG_KMS("Self-refresh is disabled\n");
	}
}

static void g4x_update_wm(struct drm_device *dev,  int planea_clock,
			  int planeb_clock, int sr_hdisplay, int sr_htotal,
			  int pixel_size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int total_size, cacheline_size;
	int planea_wm, planeb_wm, cursora_wm, cursorb_wm, cursor_sr;
	struct intel_watermark_params planea_params, planeb_params;
	unsigned long line_time_us;
	int sr_clock, sr_entries = 0, entries_required;

	/* Create copies of the base settings for each pipe */
	planea_params = planeb_params = g4x_wm_info;

	/* Grab a couple of global values before we overwrite them */
	total_size = planea_params.fifo_size;
	cacheline_size = planea_params.cacheline_size;

	/*
	 * Note: we need to make sure we don't overflow for various clock &
	 * latency values.
	 * clocks go from a few thousand to several hundred thousand.
	 * latency is usually a few thousand
	 */
	entries_required = ((planea_clock / 1000) * pixel_size * latency_ns) /
		1000;
	entries_required = DIV_ROUND_UP(entries_required, G4X_FIFO_LINE_SIZE);
	planea_wm = entries_required + planea_params.guard_size;

	entries_required = ((planeb_clock / 1000) * pixel_size * latency_ns) /
		1000;
	entries_required = DIV_ROUND_UP(entries_required, G4X_FIFO_LINE_SIZE);
	planeb_wm = entries_required + planeb_params.guard_size;

	cursora_wm = cursorb_wm = 16;
	cursor_sr = 32;

	DRM_DEBUG("FIFO watermarks - A: %d, B: %d\n", planea_wm, planeb_wm);

	/* Calc sr entries for one plane configs */
	if (sr_hdisplay && (!planea_clock || !planeb_clock)) {
		/* self-refresh has much higher latency */
		static const int sr_latency_ns = 12000;

		sr_clock = planea_clock ? planea_clock : planeb_clock;
		line_time_us = ((sr_htotal * 1000) / sr_clock);

		/* Use ns/us then divide to preserve precision */
		sr_entries = (((sr_latency_ns / line_time_us) + 1000) / 1000) *
			pixel_size * sr_hdisplay;
		sr_entries = DIV_ROUND_UP(sr_entries, cacheline_size);

		entries_required = (((sr_latency_ns / line_time_us) +
				     1000) / 1000) * pixel_size * 64;
		entries_required = DIV_ROUND_UP(entries_required,
						g4x_cursor_wm_info.cacheline_size);
		cursor_sr = entries_required + g4x_cursor_wm_info.guard_size;

		if (cursor_sr > g4x_cursor_wm_info.max_wm)
			cursor_sr = g4x_cursor_wm_info.max_wm;
		DRM_DEBUG_KMS("self-refresh watermark: display plane %d "
			      "cursor %d\n", sr_entries, cursor_sr);

		I915_WRITE(FW_BLC_SELF, FW_BLC_SELF_EN);
	} else {
		/* Turn off self refresh if both pipes are enabled */
		I915_WRITE(FW_BLC_SELF, I915_READ(FW_BLC_SELF)
			   & ~FW_BLC_SELF_EN);
	}

	DRM_DEBUG("Setting FIFO watermarks - A: %d, B: %d, SR %d\n",
		  planea_wm, planeb_wm, sr_entries);

	planea_wm &= 0x3f;
	planeb_wm &= 0x3f;

	I915_WRITE(DSPFW1, (sr_entries << DSPFW_SR_SHIFT) |
		   (cursorb_wm << DSPFW_CURSORB_SHIFT) |
		   (planeb_wm << DSPFW_PLANEB_SHIFT) | planea_wm);
	I915_WRITE(DSPFW2, (I915_READ(DSPFW2) & DSPFW_CURSORA_MASK) |
		   (cursora_wm << DSPFW_CURSORA_SHIFT));
	/* HPLL off in SR has some issues on G4x... disable it */
	I915_WRITE(DSPFW3, (I915_READ(DSPFW3) & ~DSPFW_HPLL_SR_EN) |
		   (cursor_sr << DSPFW_CURSOR_SR_SHIFT));
}

static void i965_update_wm(struct drm_device *dev, int planea_clock,
			   int planeb_clock, int sr_hdisplay, int sr_htotal,
			   int pixel_size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long line_time_us;
	int sr_clock, sr_entries, srwm = 1;
	int cursor_sr = 16;

	/* Calc sr entries for one plane configs */
	if (sr_hdisplay && (!planea_clock || !planeb_clock)) {
		/* self-refresh has much higher latency */
		static const int sr_latency_ns = 12000;

		sr_clock = planea_clock ? planea_clock : planeb_clock;
		line_time_us = ((sr_htotal * 1000) / sr_clock);

		/* Use ns/us then divide to preserve precision */
		sr_entries = (((sr_latency_ns / line_time_us) + 1000) / 1000) *
			pixel_size * sr_hdisplay;
		sr_entries = DIV_ROUND_UP(sr_entries, I915_FIFO_LINE_SIZE);
		DRM_DEBUG("self-refresh entries: %d\n", sr_entries);
		srwm = I965_FIFO_SIZE - sr_entries;
		if (srwm < 0)
			srwm = 1;
		srwm &= 0x1ff;

		sr_entries = (((sr_latency_ns / line_time_us) + 1000) / 1000) *
			pixel_size * 64;
		sr_entries = DIV_ROUND_UP(sr_entries,
					  i965_cursor_wm_info.cacheline_size);
		cursor_sr = i965_cursor_wm_info.fifo_size -
			(sr_entries + i965_cursor_wm_info.guard_size);

		if (cursor_sr > i965_cursor_wm_info.max_wm)
			cursor_sr = i965_cursor_wm_info.max_wm;

		DRM_DEBUG_KMS("self-refresh watermark: display plane %d "
			      "cursor %d\n", srwm, cursor_sr);

		if (IS_CRESTLINE(dev))
			I915_WRITE(FW_BLC_SELF, FW_BLC_SELF_EN);
	} else {
		/* Turn off self refresh if both pipes are enabled */
		if (IS_CRESTLINE(dev))
			I915_WRITE(FW_BLC_SELF, I915_READ(FW_BLC_SELF)
				   & ~FW_BLC_SELF_EN);
	}

	DRM_DEBUG_KMS("Setting FIFO watermarks - A: 8, B: 8, C: 8, SR %d\n",
		      srwm);

	/* 965 has limitations... */
	I915_WRITE(DSPFW1, (srwm << DSPFW_SR_SHIFT) | (8 << 16) | (8 << 8) |
		   (8 << 0));
	I915_WRITE(DSPFW2, (8 << 8) | (8 << 0));
	/* update cursor SR watermark */
	I915_WRITE(DSPFW3, (cursor_sr << DSPFW_CURSOR_SR_SHIFT));
}

static void i9xx_update_wm(struct drm_device *dev, int planea_clock,
			   int planeb_clock, int sr_hdisplay, int sr_htotal,
			   int pixel_size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t fwater_lo;
	uint32_t fwater_hi;
	int total_size, cacheline_size, cwm, srwm = 1;
	int planea_wm, planeb_wm;
	struct intel_watermark_params planea_params, planeb_params;
	unsigned long line_time_us;
	int sr_clock, sr_entries = 0;

	/* Create copies of the base settings for each pipe */
	if (IS_CRESTLINE(dev) || IS_I945GM(dev))
		planea_params = planeb_params = i945_wm_info;
	else if (!IS_GEN2(dev))
		planea_params = planeb_params = i915_wm_info;
	else
		planea_params = planeb_params = i855_wm_info;

	/* Grab a couple of global values before we overwrite them */
	total_size = planea_params.fifo_size;
	cacheline_size = planea_params.cacheline_size;

	/* Update per-plane FIFO sizes */
	planea_params.fifo_size = dev_priv->display.get_fifo_size(dev, 0);
	planeb_params.fifo_size = dev_priv->display.get_fifo_size(dev, 1);

	planea_wm = intel_calculate_wm(planea_clock, &planea_params,
				       pixel_size, latency_ns);
	planeb_wm = intel_calculate_wm(planeb_clock, &planeb_params,
				       pixel_size, latency_ns);
	DRM_DEBUG_KMS("FIFO watermarks - A: %d, B: %d\n", planea_wm, planeb_wm);

	/*
	 * Overlay gets an aggressive default since video jitter is bad.
	 */
	cwm = 2;

	/* Calc sr entries for one plane configs */
	if (HAS_FW_BLC(dev) && sr_hdisplay &&
	    (!planea_clock || !planeb_clock)) {
		/* self-refresh has much higher latency */
		static const int sr_latency_ns = 6000;

		sr_clock = planea_clock ? planea_clock : planeb_clock;
		line_time_us = ((sr_htotal * 1000) / sr_clock);

		/* Use ns/us then divide to preserve precision */
		sr_entries = (((sr_latency_ns / line_time_us) + 1000) / 1000) *
			pixel_size * sr_hdisplay;
		sr_entries = DIV_ROUND_UP(sr_entries, cacheline_size);
		DRM_DEBUG_KMS("self-refresh entries: %d\n", sr_entries);
		srwm = total_size - sr_entries;
		if (srwm < 0)
			srwm = 1;

		if (IS_I945G(dev) || IS_I945GM(dev))
			I915_WRITE(FW_BLC_SELF, FW_BLC_SELF_FIFO_MASK | (srwm & 0xff));
		else if (IS_I915GM(dev)) {
			/* 915M has a smaller SRWM field */
			I915_WRITE(FW_BLC_SELF, srwm & 0x3f);
			I915_WRITE(INSTPM, I915_READ(INSTPM) | INSTPM_SELF_EN);
		}
	} else {
		/* Turn off self refresh if both pipes are enabled */
		if (IS_I945G(dev) || IS_I945GM(dev)) {
			I915_WRITE(FW_BLC_SELF, I915_READ(FW_BLC_SELF)
				   & ~FW_BLC_SELF_EN);
		} else if (IS_I915GM(dev)) {
			I915_WRITE(INSTPM, I915_READ(INSTPM) & ~INSTPM_SELF_EN);
		}
	}

	DRM_DEBUG_KMS("Setting FIFO watermarks - A: %d, B: %d, C: %d, SR %d\n",
		      planea_wm, planeb_wm, cwm, srwm);

	fwater_lo = ((planeb_wm & 0x3f) << 16) | (planea_wm & 0x3f);
	fwater_hi = (cwm & 0x1f);

	/* Set request length to 8 cachelines per fetch */
	fwater_lo = fwater_lo | (1 << 24) | (1 << 8);
	fwater_hi = fwater_hi | (1 << 8);

	I915_WRITE(FW_BLC, fwater_lo);
	I915_WRITE(FW_BLC2, fwater_hi);
}

static void i830_update_wm(struct drm_device *dev, int planea_clock, int unused,
			   int unused2, int unused3, int pixel_size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t fwater_lo = I915_READ(FW_BLC) & ~0xfff;
	int planea_wm;

	i830_wm_info.fifo_size = dev_priv->display.get_fifo_size(dev, 0);

	planea_wm = intel_calculate_wm(planea_clock, &i830_wm_info,
				       pixel_size, latency_ns);
	fwater_lo |= (3<<8) | planea_wm;

	DRM_DEBUG_KMS("Setting FIFO watermarks - A: %d\n", planea_wm);

	I915_WRITE(FW_BLC, fwater_lo);
}

#define ILK_LP0_PLANE_LATENCY		700
#define ILK_LP0_CURSOR_LATENCY		1300

static bool ironlake_compute_wm0(struct drm_device *dev,
				 int pipe,
				 int *plane_wm,
				 int *cursor_wm)
{
	struct drm_crtc *crtc;
	int htotal, hdisplay, clock, pixel_size = 0;
	int line_time_us, line_count, entries;

	crtc = intel_get_crtc_for_pipe(dev, pipe);
	if (crtc->fb == NULL || !crtc->enabled)
		return false;

	htotal = crtc->mode.htotal;
	hdisplay = crtc->mode.hdisplay;
	clock = crtc->mode.clock;
	pixel_size = crtc->fb->bits_per_pixel / 8;

	/* Use the small buffer method to calculate plane watermark */
	entries = ((clock * pixel_size / 1000) * ILK_LP0_PLANE_LATENCY) / 1000;
	entries = DIV_ROUND_UP(entries,
			       ironlake_display_wm_info.cacheline_size);
	*plane_wm = entries + ironlake_display_wm_info.guard_size;
	if (*plane_wm > (int)ironlake_display_wm_info.max_wm)
		*plane_wm = ironlake_display_wm_info.max_wm;

	/* Use the large buffer method to calculate cursor watermark */
	line_time_us = ((htotal * 1000) / clock);
	line_count = (ILK_LP0_CURSOR_LATENCY / line_time_us + 1000) / 1000;
	entries = line_count * 64 * pixel_size;
	entries = DIV_ROUND_UP(entries,
			       ironlake_cursor_wm_info.cacheline_size);
	*cursor_wm = entries + ironlake_cursor_wm_info.guard_size;
	if (*cursor_wm > ironlake_cursor_wm_info.max_wm)
		*cursor_wm = ironlake_cursor_wm_info.max_wm;

	return true;
}

static void ironlake_update_wm(struct drm_device *dev,
			       int planea_clock, int planeb_clock,
			       int sr_hdisplay, int sr_htotal,
			       int pixel_size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int plane_wm, cursor_wm, enabled;
	int tmp;

	enabled = 0;
	if (ironlake_compute_wm0(dev, 0, &plane_wm, &cursor_wm)) {
		I915_WRITE(WM0_PIPEA_ILK,
			   (plane_wm << WM0_PIPE_PLANE_SHIFT) | cursor_wm);
		DRM_DEBUG_KMS("FIFO watermarks For pipe A -"
			      " plane %d, " "cursor: %d\n",
			      plane_wm, cursor_wm);
		enabled++;
	}

	if (ironlake_compute_wm0(dev, 1, &plane_wm, &cursor_wm)) {
		I915_WRITE(WM0_PIPEB_ILK,
			   (plane_wm << WM0_PIPE_PLANE_SHIFT) | cursor_wm);
		DRM_DEBUG_KMS("FIFO watermarks For pipe B -"
			      " plane %d, cursor: %d\n",
			      plane_wm, cursor_wm);
		enabled++;
	}

	/*
	 * Calculate and update the self-refresh watermark only when one
	 * display plane is used.
	 */
	tmp = 0;
	if (enabled == 1 && /* XXX disabled due to buggy implmentation? */ 0) {
		unsigned long line_time_us;
		int small, large, plane_fbc;
		int sr_clock, entries;
		int line_count, line_size;
		/* Read the self-refresh latency. The unit is 0.5us */
		int ilk_sr_latency = I915_READ(MLTR_ILK) & ILK_SRLT_MASK;

		sr_clock = planea_clock ? planea_clock : planeb_clock;
		line_time_us = (sr_htotal * 1000) / sr_clock;

		/* Use ns/us then divide to preserve precision */
		line_count = ((ilk_sr_latency * 500) / line_time_us + 1000)
			/ 1000;
		line_size = sr_hdisplay * pixel_size;

		/* Use the minimum of the small and large buffer method for primary */
		small = ((sr_clock * pixel_size / 1000) * (ilk_sr_latency * 500)) / 1000;
		large = line_count * line_size;

		entries = DIV_ROUND_UP(min(small, large),
				       ironlake_display_srwm_info.cacheline_size);

		plane_fbc = entries * 64;
		plane_fbc = DIV_ROUND_UP(plane_fbc, line_size);

		plane_wm = entries + ironlake_display_srwm_info.guard_size;
		if (plane_wm > (int)ironlake_display_srwm_info.max_wm)
			plane_wm = ironlake_display_srwm_info.max_wm;

		/* calculate the self-refresh watermark for display cursor */
		entries = line_count * pixel_size * 64;
		entries = DIV_ROUND_UP(entries,
				       ironlake_cursor_srwm_info.cacheline_size);

		cursor_wm = entries + ironlake_cursor_srwm_info.guard_size;
		if (cursor_wm > (int)ironlake_cursor_srwm_info.max_wm)
			cursor_wm = ironlake_cursor_srwm_info.max_wm;

		/* configure watermark and enable self-refresh */
		tmp = (WM1_LP_SR_EN |
		       (ilk_sr_latency << WM1_LP_LATENCY_SHIFT) |
		       (plane_fbc << WM1_LP_FBC_SHIFT) |
		       (plane_wm << WM1_LP_SR_SHIFT) |
		       cursor_wm);
		DRM_DEBUG_KMS("self-refresh watermark: display plane %d, fbc lines %d,"
			      " cursor %d\n", plane_wm, plane_fbc, cursor_wm);
	}
	I915_WRITE(WM1_LP_ILK, tmp);
	/* XXX setup WM2 and WM3 */
}

/**
 * intel_update_watermarks - update FIFO watermark values based on current modes
 *
 * Calculate watermark values for the various WM regs based on current mode
 * and plane configuration.
 *
 * There are several cases to deal with here:
 *   - normal (i.e. non-self-refresh)
 *   - self-refresh (SR) mode
 *   - lines are large relative to FIFO size (buffer can hold up to 2)
 *   - lines are small relative to FIFO size (buffer can hold more than 2
 *     lines), so need to account for TLB latency
 *
 *   The normal calculation is:
 *     watermark = dotclock * bytes per pixel * latency
 *   where latency is platform & configuration dependent (we assume pessimal
 *   values here).
 *
 *   The SR calculation is:
 *     watermark = (trunc(latency/line time)+1) * surface width *
 *       bytes per pixel
 *   where
 *     line time = htotal / dotclock
 *     surface width = hdisplay for normal plane and 64 for cursor
 *   and latency is assumed to be high, as above.
 *
 * The final value programmed to the register should always be rounded up,
 * and include an extra 2 entries to account for clock crossings.
 *
 * We don't use the sprite, so we can ignore that.  And on Crestline we have
 * to set the non-SR watermarks to 8.
 */
static void intel_update_watermarks(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	int sr_hdisplay = 0;
	unsigned long planea_clock = 0, planeb_clock = 0, sr_clock = 0;
	int enabled = 0, pixel_size = 0;
	int sr_htotal = 0;

	if (!dev_priv->display.update_wm)
		return;

	/* Get the clock config from both planes */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
		if (intel_crtc->active) {
			enabled++;
			if (intel_crtc->plane == 0) {
				DRM_DEBUG_KMS("plane A (pipe %d) clock: %d\n",
					      intel_crtc->pipe, crtc->mode.clock);
				planea_clock = crtc->mode.clock;
			} else {
				DRM_DEBUG_KMS("plane B (pipe %d) clock: %d\n",
					      intel_crtc->pipe, crtc->mode.clock);
				planeb_clock = crtc->mode.clock;
			}
			sr_hdisplay = crtc->mode.hdisplay;
			sr_clock = crtc->mode.clock;
			sr_htotal = crtc->mode.htotal;
			if (crtc->fb)
				pixel_size = crtc->fb->bits_per_pixel / 8;
			else
				pixel_size = 4; /* by default */
		}
	}

	if (enabled <= 0)
		return;

	dev_priv->display.update_wm(dev, planea_clock, planeb_clock,
				    sr_hdisplay, sr_htotal, pixel_size);
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
	int plane = intel_crtc->plane;
	u32 fp_reg, dpll_reg;
	int refclk, num_connectors = 0;
	intel_clock_t clock, reduced_clock;
	u32 dpll, fp = 0, fp2 = 0, dspcntr, pipeconf;
	bool ok, has_reduced_clock = false, is_sdvo = false, is_dvo = false;
	bool is_crt = false, is_lvds = false, is_tv = false, is_dp = false;
	struct intel_encoder *has_edp_encoder = NULL;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_encoder *encoder;
	const intel_limit_t *limit;
	int ret;
	struct fdi_m_n m_n = {0};
	u32 reg, temp;
	int target_clock;

	drm_vblank_pre_modeset(dev, pipe);

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
		case INTEL_OUTPUT_DVO:
			is_dvo = true;
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
			has_edp_encoder = encoder;
			break;
		}

		num_connectors++;
	}

	if (is_lvds && dev_priv->lvds_use_ssc && num_connectors < 2) {
		refclk = dev_priv->lvds_ssc_freq * 1000;
		DRM_DEBUG_KMS("using SSC reference clock of %d MHz\n",
			      refclk / 1000);
	} else if (!IS_GEN2(dev)) {
		refclk = 96000;
		if (HAS_PCH_SPLIT(dev) &&
		    (!has_edp_encoder || intel_encoder_is_pch_edp(&has_edp_encoder->base)))
			refclk = 120000; /* 120Mhz refclk */
	} else {
		refclk = 48000;
	}

	/*
	 * Returns a set of divisors for the desired target clock with the given
	 * refclk, or FALSE.  The returned values represent the clock equation:
	 * reflck * (5 * (m1 + 2) + (m2 + 2)) / (n + 2) / p1 / p2.
	 */
	limit = intel_limit(crtc);
	ok = limit->find_pll(limit, crtc, adjusted_mode->clock, refclk, &clock);
	if (!ok) {
		DRM_ERROR("Couldn't find PLL settings for mode!\n");
		drm_vblank_post_modeset(dev, pipe);
		return -EINVAL;
	}

	/* Ensure that the cursor is valid for the new mode before changing... */
	intel_crtc_update_cursor(crtc, true);

	if (is_lvds && dev_priv->lvds_downclock_avail) {
		has_reduced_clock = limit->find_pll(limit, crtc,
						    dev_priv->lvds_downclock,
						    refclk,
						    &reduced_clock);
		if (has_reduced_clock && (clock.p != reduced_clock.p)) {
			/*
			 * If the different P is found, it means that we can't
			 * switch the display clock by using the FP0/FP1.
			 * In such case we will disable the LVDS downclock
			 * feature.
			 */
			DRM_DEBUG_KMS("Different P is found for "
				      "LVDS clock/downclock\n");
			has_reduced_clock = 0;
		}
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
	if (HAS_PCH_SPLIT(dev)) {
		int lane = 0, link_bw, bpp;
		/* CPU eDP doesn't require FDI link, so just set DP M/N
		   according to current link config */
		if (has_edp_encoder && !intel_encoder_is_pch_edp(&encoder->base)) {
			target_clock = mode->clock;
			intel_edp_link_config(has_edp_encoder,
					      &lane, &link_bw);
		} else {
			/* [e]DP over FDI requires target mode clock
			   instead of link clock */
			if (is_dp || intel_encoder_is_pch_edp(&has_edp_encoder->base))
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
		if (is_lvds) {
			/* the BPC will be 6 if it is 18-bit LVDS panel */
			if ((I915_READ(PCH_LVDS) & LVDS_A3_POWER_MASK) == LVDS_A3_POWER_UP)
				temp |= PIPE_8BPC;
			else
				temp |= PIPE_6BPC;
		} else if (has_edp_encoder) {
			switch (dev_priv->edp.bpp/3) {
			case 8:
				temp |= PIPE_8BPC;
				break;
			case 10:
				temp |= PIPE_10BPC;
				break;
			case 6:
				temp |= PIPE_6BPC;
				break;
			case 12:
				temp |= PIPE_12BPC;
				break;
			}
		} else
			temp |= PIPE_8BPC;
		I915_WRITE(PIPECONF(pipe), temp);

		switch (temp & PIPE_BPC_MASK) {
		case PIPE_8BPC:
			bpp = 24;
			break;
		case PIPE_10BPC:
			bpp = 30;
			break;
		case PIPE_6BPC:
			bpp = 18;
			break;
		case PIPE_12BPC:
			bpp = 36;
			break;
		default:
			DRM_ERROR("unknown pipe bpc value\n");
			bpp = 24;
		}

		if (!lane) {
			/* 
			 * Account for spread spectrum to avoid
			 * oversubscribing the link. Max center spread
			 * is 2.5%; use 5% for safety's sake.
			 */
			u32 bps = target_clock * bpp * 21 / 20;
			lane = bps / (link_bw * 8) + 1;
		}

		intel_crtc->fdi_lanes = lane;

		ironlake_compute_m_n(bpp, lane, target_clock, link_bw, &m_n);
	}

	/* Ironlake: try to setup display ref clock before DPLL
	 * enabling. This is only under driver's control after
	 * PCH B stepping, previous chipset stepping should be
	 * ignoring this setting.
	 */
	if (HAS_PCH_SPLIT(dev)) {
		temp = I915_READ(PCH_DREF_CONTROL);
		/* Always enable nonspread source */
		temp &= ~DREF_NONSPREAD_SOURCE_MASK;
		temp |= DREF_NONSPREAD_SOURCE_ENABLE;
		temp &= ~DREF_SSC_SOURCE_MASK;
		temp |= DREF_SSC_SOURCE_ENABLE;
		I915_WRITE(PCH_DREF_CONTROL, temp);

		POSTING_READ(PCH_DREF_CONTROL);
		udelay(200);

		if (has_edp_encoder) {
			if (dev_priv->lvds_use_ssc) {
				temp |= DREF_SSC1_ENABLE;
				I915_WRITE(PCH_DREF_CONTROL, temp);

				POSTING_READ(PCH_DREF_CONTROL);
				udelay(200);
			}
			temp &= ~DREF_CPU_SOURCE_OUTPUT_MASK;

			/* Enable CPU source on CPU attached eDP */
			if (!intel_encoder_is_pch_edp(&has_edp_encoder->base)) {
				if (dev_priv->lvds_use_ssc)
					temp |= DREF_CPU_SOURCE_OUTPUT_DOWNSPREAD;
				else
					temp |= DREF_CPU_SOURCE_OUTPUT_NONSPREAD;
			} else {
				/* Enable SSC on PCH eDP if needed */
				if (dev_priv->lvds_use_ssc) {
					DRM_ERROR("enabling SSC on PCH\n");
					temp |= DREF_SUPERSPREAD_SOURCE_ENABLE;
				}
			}
			I915_WRITE(PCH_DREF_CONTROL, temp);
			POSTING_READ(PCH_DREF_CONTROL);
			udelay(200);
		}
	}

	if (IS_PINEVIEW(dev)) {
		fp = (1 << clock.n) << 16 | clock.m1 << 8 | clock.m2;
		if (has_reduced_clock)
			fp2 = (1 << reduced_clock.n) << 16 |
				reduced_clock.m1 << 8 | reduced_clock.m2;
	} else {
		fp = clock.n << 16 | clock.m1 << 8 | clock.m2;
		if (has_reduced_clock)
			fp2 = reduced_clock.n << 16 | reduced_clock.m1 << 8 |
				reduced_clock.m2;
	}

	dpll = 0;
	if (!HAS_PCH_SPLIT(dev))
		dpll = DPLL_VGA_MODE_DIS;

	if (!IS_GEN2(dev)) {
		if (is_lvds)
			dpll |= DPLLB_MODE_LVDS;
		else
			dpll |= DPLLB_MODE_DAC_SERIAL;
		if (is_sdvo) {
			int pixel_multiplier = intel_mode_get_pixel_multiplier(adjusted_mode);
			if (pixel_multiplier > 1) {
				if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
					dpll |= (pixel_multiplier - 1) << SDVO_MULTIPLIER_SHIFT_HIRES;
				else if (HAS_PCH_SPLIT(dev))
					dpll |= (pixel_multiplier - 1) << PLL_REF_SDVO_HDMI_MULTIPLIER_SHIFT;
			}
			dpll |= DPLL_DVO_HIGH_SPEED;
		}
		if (is_dp || intel_encoder_is_pch_edp(&has_edp_encoder->base))
			dpll |= DPLL_DVO_HIGH_SPEED;

		/* compute bitmask from p1 value */
		if (IS_PINEVIEW(dev))
			dpll |= (1 << (clock.p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT_PINEVIEW;
		else {
			dpll |= (1 << (clock.p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
			/* also FPA1 */
			if (HAS_PCH_SPLIT(dev))
				dpll |= (1 << (clock.p1 - 1)) << DPLL_FPA1_P1_POST_DIV_SHIFT;
			if (IS_G4X(dev) && has_reduced_clock)
				dpll |= (1 << (reduced_clock.p1 - 1)) << DPLL_FPA1_P1_POST_DIV_SHIFT;
		}
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
		if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev))
			dpll |= (6 << PLL_LOAD_PULSE_PHASE_SHIFT);
	} else {
		if (is_lvds) {
			dpll |= (1 << (clock.p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
		} else {
			if (clock.p1 == 2)
				dpll |= PLL_P1_DIVIDE_BY_TWO;
			else
				dpll |= (clock.p1 - 2) << DPLL_FPA01_P1_POST_DIV_SHIFT;
			if (clock.p2 == 4)
				dpll |= PLL_P2_DIVIDE_BY_4;
		}
	}

	if (is_sdvo && is_tv)
		dpll |= PLL_REF_INPUT_TVCLKINBC;
	else if (is_tv)
		/* XXX: just matching BIOS for now */
		/*	dpll |= PLL_REF_INPUT_TVCLKINBC; */
		dpll |= 3;
	else if (is_lvds && dev_priv->lvds_use_ssc && num_connectors < 2)
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	/* setup pipeconf */
	pipeconf = I915_READ(PIPECONF(pipe));

	/* Set up the display plane register */
	dspcntr = DISPPLANE_GAMMA_ENABLE;

	/* Ironlake's plane is forced to pipe, bit 24 is to
	   enable color space conversion */
	if (!HAS_PCH_SPLIT(dev)) {
		if (pipe == 0)
			dspcntr &= ~DISPPLANE_SEL_PIPE_MASK;
		else
			dspcntr |= DISPPLANE_SEL_PIPE_B;
	}

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

	dspcntr |= DISPLAY_PLANE_ENABLE;
	pipeconf |= PIPECONF_ENABLE;
	dpll |= DPLL_VCO_ENABLE;

	DRM_DEBUG_KMS("Mode for pipe %c:\n", pipe == 0 ? 'A' : 'B');
	drm_mode_debug_printmodeline(mode);

	/* assign to Ironlake registers */
	if (HAS_PCH_SPLIT(dev)) {
		fp_reg = PCH_FP0(pipe);
		dpll_reg = PCH_DPLL(pipe);
	} else {
		fp_reg = FP0(pipe);
		dpll_reg = DPLL(pipe);
	}

	/* PCH eDP needs FDI, but CPU eDP does not */
	if (!has_edp_encoder || intel_encoder_is_pch_edp(&has_edp_encoder->base)) {
		I915_WRITE(fp_reg, fp);
		I915_WRITE(dpll_reg, dpll & ~DPLL_VCO_ENABLE);

		POSTING_READ(dpll_reg);
		udelay(150);
	}

	/* enable transcoder DPLL */
	if (HAS_PCH_CPT(dev)) {
		temp = I915_READ(PCH_DPLL_SEL);
		if (pipe == 0)
			temp |= TRANSA_DPLL_ENABLE | TRANSA_DPLLA_SEL;
		else
			temp |=	TRANSB_DPLL_ENABLE | TRANSB_DPLLB_SEL;
		I915_WRITE(PCH_DPLL_SEL, temp);

		POSTING_READ(PCH_DPLL_SEL);
		udelay(150);
	}

	/* The LVDS pin pair needs to be on before the DPLLs are enabled.
	 * This is an exception to the general rule that mode_set doesn't turn
	 * things on.
	 */
	if (is_lvds) {
		reg = LVDS;
		if (HAS_PCH_SPLIT(dev))
			reg = PCH_LVDS;

		temp = I915_READ(reg);
		temp |= LVDS_PORT_EN | LVDS_A0A2_CLKA_POWER_UP;
		if (pipe == 1) {
			if (HAS_PCH_CPT(dev))
				temp |= PORT_TRANS_B_SEL_CPT;
			else
				temp |= LVDS_PIPEB_SELECT;
		} else {
			if (HAS_PCH_CPT(dev))
				temp &= ~PORT_TRANS_SEL_MASK;
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
		/* set the dithering flag on non-PCH LVDS as needed */
		if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev)) {
			if (dev_priv->lvds_dither)
				temp |= LVDS_ENABLE_DITHER;
			else
				temp &= ~LVDS_ENABLE_DITHER;
		}
		I915_WRITE(reg, temp);
	}

	/* set the dithering flag and clear for anything other than a panel. */
	if (HAS_PCH_SPLIT(dev)) {
		pipeconf &= ~PIPECONF_DITHER_EN;
		pipeconf &= ~PIPECONF_DITHER_TYPE_MASK;
		if (dev_priv->lvds_dither && (is_lvds || has_edp_encoder)) {
			pipeconf |= PIPECONF_DITHER_EN;
			pipeconf |= PIPECONF_DITHER_TYPE_ST1;
		}
	}

	if (is_dp || intel_encoder_is_pch_edp(&has_edp_encoder->base)) {
		intel_dp_set_m_n(crtc, mode, adjusted_mode);
	} else if (HAS_PCH_SPLIT(dev)) {
		/* For non-DP output, clear any trans DP clock recovery setting.*/
		if (pipe == 0) {
			I915_WRITE(TRANSA_DATA_M1, 0);
			I915_WRITE(TRANSA_DATA_N1, 0);
			I915_WRITE(TRANSA_DP_LINK_M1, 0);
			I915_WRITE(TRANSA_DP_LINK_N1, 0);
		} else {
			I915_WRITE(TRANSB_DATA_M1, 0);
			I915_WRITE(TRANSB_DATA_N1, 0);
			I915_WRITE(TRANSB_DP_LINK_M1, 0);
			I915_WRITE(TRANSB_DP_LINK_N1, 0);
		}
	}

	if (!has_edp_encoder || intel_encoder_is_pch_edp(&has_edp_encoder->base)) {
		I915_WRITE(fp_reg, fp);
		I915_WRITE(dpll_reg, dpll);

		/* Wait for the clocks to stabilize. */
		POSTING_READ(dpll_reg);
		udelay(150);

		if (INTEL_INFO(dev)->gen >= 4 && !HAS_PCH_SPLIT(dev)) {
			temp = 0;
			if (is_sdvo) {
				temp = intel_mode_get_pixel_multiplier(adjusted_mode);
				if (temp > 1)
					temp = (temp - 1) << DPLL_MD_UDI_MULTIPLIER_SHIFT;
				else
					temp = 0;
			}
			I915_WRITE(DPLL_MD(pipe), temp);
		} else {
			/* write it again -- the BIOS does, after all */
			I915_WRITE(dpll_reg, dpll);
		}

		/* Wait for the clocks to stabilize. */
		POSTING_READ(dpll_reg);
		udelay(150);
	}

	intel_crtc->lowfreq_avail = false;
	if (is_lvds && has_reduced_clock && i915_powersave) {
		I915_WRITE(fp_reg + 4, fp2);
		intel_crtc->lowfreq_avail = true;
		if (HAS_PIPE_CXSR(dev)) {
			DRM_DEBUG_KMS("enabling CxSR downclocking\n");
			pipeconf |= PIPECONF_CXSR_DOWNCLOCK;
		}
	} else {
		I915_WRITE(fp_reg + 4, fp);
		if (HAS_PIPE_CXSR(dev)) {
			DRM_DEBUG_KMS("disabling CxSR downclocking\n");
			pipeconf &= ~PIPECONF_CXSR_DOWNCLOCK;
		}
	}

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		pipeconf |= PIPECONF_INTERLACE_W_FIELD_INDICATION;
		/* the chip adds 2 halflines automatically */
		adjusted_mode->crtc_vdisplay -= 1;
		adjusted_mode->crtc_vtotal -= 1;
		adjusted_mode->crtc_vblank_start -= 1;
		adjusted_mode->crtc_vblank_end -= 1;
		adjusted_mode->crtc_vsync_end -= 1;
		adjusted_mode->crtc_vsync_start -= 1;
	} else
		pipeconf &= ~PIPECONF_INTERLACE_W_FIELD_INDICATION; /* progressive */

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
	if (!HAS_PCH_SPLIT(dev)) {
		I915_WRITE(DSPSIZE(plane),
			   ((mode->vdisplay - 1) << 16) |
			   (mode->hdisplay - 1));
		I915_WRITE(DSPPOS(plane), 0);
	}
	I915_WRITE(PIPESRC(pipe),
		   ((mode->hdisplay - 1) << 16) | (mode->vdisplay - 1));

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(PIPE_DATA_M1(pipe), TU_SIZE(m_n.tu) | m_n.gmch_m);
		I915_WRITE(PIPE_DATA_N1(pipe), m_n.gmch_n);
		I915_WRITE(PIPE_LINK_M1(pipe), m_n.link_m);
		I915_WRITE(PIPE_LINK_N1(pipe), m_n.link_n);

		if (has_edp_encoder && !intel_encoder_is_pch_edp(&has_edp_encoder->base)) {
			ironlake_set_pll_edp(crtc, adjusted_mode->clock);
		}
	}

	I915_WRITE(PIPECONF(pipe), pipeconf);
	POSTING_READ(PIPECONF(pipe));

	intel_wait_for_vblank(dev, pipe);

	if (IS_GEN5(dev)) {
		/* enable address swizzle for tiling buffer */
		temp = I915_READ(DISP_ARB_CTL);
		I915_WRITE(DISP_ARB_CTL, temp | DISP_TILE_SURFACE_SWIZZLING);
	}

	I915_WRITE(DSPCNTR(plane), dspcntr);

	ret = intel_pipe_set_base(crtc, x, y, old_fb);

	intel_update_watermarks(dev);

	drm_vblank_post_modeset(dev, pipe);

	return ret;
}

/** Loads the palette/gamma unit for the CRTC with the prepared values */
void intel_crtc_load_lut(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int palreg = (intel_crtc->pipe == 0) ? PALETTE_A : PALETTE_B;
	int i;

	/* The clocks have to be on to load the palette. */
	if (!crtc->enabled)
		return;

	/* use legacy palette for Ironlake */
	if (HAS_PCH_SPLIT(dev))
		palreg = (intel_crtc->pipe == 0) ? LGC_PALETTE_A :
						   LGC_PALETTE_B;

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

	cntl = I915_READ(CURACNTR);
	if (visible) {
		/* On these chipsets we can only modify the base whilst
		 * the cursor is disabled.
		 */
		I915_WRITE(CURABASE, base);

		cntl &= ~(CURSOR_FORMAT_MASK);
		/* XXX width must be 64, stride 256 => 0x00 << 28 */
		cntl |= CURSOR_ENABLE |
			CURSOR_GAMMA_ENABLE |
			CURSOR_FORMAT_ARGB;
	} else
		cntl &= ~(CURSOR_ENABLE | CURSOR_GAMMA_ENABLE);
	I915_WRITE(CURACNTR, cntl);

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
		uint32_t cntl = I915_READ(pipe == 0 ? CURACNTR : CURBCNTR);
		if (base) {
			cntl &= ~(CURSOR_MODE | MCURSOR_PIPE_SELECT);
			cntl |= CURSOR_MODE_64_ARGB_AX | MCURSOR_GAMMA_ENABLE;
			cntl |= pipe << 28; /* Connect to correct pipe */
		} else {
			cntl &= ~(CURSOR_MODE | MCURSOR_GAMMA_ENABLE);
			cntl |= CURSOR_MODE_DISABLE;
		}
		I915_WRITE(pipe == 0 ? CURACNTR : CURBCNTR, cntl);

		intel_crtc->cursor_visible = visible;
	}
	/* and commit changes on next vblank */
	I915_WRITE(pipe == 0 ? CURABASE : CURBBASE, base);
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

	I915_WRITE(pipe == 0 ? CURAPOS : CURBPOS, pos);
	if (IS_845G(dev) || IS_I865G(dev))
		i845_update_cursor(crtc, base);
	else
		i9xx_update_cursor(crtc, base);

	if (visible)
		intel_mark_busy(dev, to_intel_framebuffer(crtc->fb)->obj);
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
	if (!obj)
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

		ret = i915_gem_object_pin(obj, PAGE_SIZE, true);
		if (ret) {
			DRM_ERROR("failed to pin cursor bo\n");
			goto fail_locked;
		}

		ret = i915_gem_object_set_to_gtt_domain(obj, 0);
		if (ret) {
			DRM_ERROR("failed to move cursor bo into the GTT\n");
			goto fail_unpin;
		}

		ret = i915_gem_object_put_fence(obj);
		if (ret) {
			DRM_ERROR("failed to move cursor bo into the GTT\n");
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

struct drm_crtc *intel_get_load_detect_pipe(struct intel_encoder *intel_encoder,
					    struct drm_connector *connector,
					    struct drm_display_mode *mode,
					    int *dpms_mode)
{
	struct intel_crtc *intel_crtc;
	struct drm_crtc *possible_crtc;
	struct drm_crtc *supported_crtc =NULL;
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_crtc *crtc = NULL;
	struct drm_device *dev = encoder->dev;
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
	struct drm_crtc_helper_funcs *crtc_funcs;
	int i = -1;

	/*
	 * Algorithm gets a little messy:
	 *   - if the connector already has an assigned crtc, use it (but make
	 *     sure it's on first)
	 *   - try to find the first unused crtc that can drive this connector,
	 *     and use that if we find one
	 *   - if there are no unused crtcs available, try to use the first
	 *     one we found that supports the connector
	 */

	/* See if we already have a CRTC for this connector */
	if (encoder->crtc) {
		crtc = encoder->crtc;
		/* Make sure the crtc and connector are running */
		intel_crtc = to_intel_crtc(crtc);
		*dpms_mode = intel_crtc->dpms_mode;
		if (intel_crtc->dpms_mode != DRM_MODE_DPMS_ON) {
			crtc_funcs = crtc->helper_private;
			crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);
			encoder_funcs->dpms(encoder, DRM_MODE_DPMS_ON);
		}
		return crtc;
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
		if (!supported_crtc)
			supported_crtc = possible_crtc;
	}

	/*
	 * If we didn't find an unused CRTC, don't use any.
	 */
	if (!crtc) {
		return NULL;
	}

	encoder->crtc = crtc;
	connector->encoder = encoder;
	intel_encoder->load_detect_temp = true;

	intel_crtc = to_intel_crtc(crtc);
	*dpms_mode = intel_crtc->dpms_mode;

	if (!crtc->enabled) {
		if (!mode)
			mode = &load_detect_mode;
		drm_crtc_helper_set_mode(crtc, mode, 0, 0, crtc->fb);
	} else {
		if (intel_crtc->dpms_mode != DRM_MODE_DPMS_ON) {
			crtc_funcs = crtc->helper_private;
			crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);
		}

		/* Add this connector to the crtc */
		encoder_funcs->mode_set(encoder, &crtc->mode, &crtc->mode);
		encoder_funcs->commit(encoder);
	}
	/* let the connector get through one full cycle before testing */
	intel_wait_for_vblank(dev, intel_crtc->pipe);

	return crtc;
}

void intel_release_load_detect_pipe(struct intel_encoder *intel_encoder,
				    struct drm_connector *connector, int dpms_mode)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_device *dev = encoder->dev;
	struct drm_crtc *crtc = encoder->crtc;
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;

	if (intel_encoder->load_detect_temp) {
		encoder->crtc = NULL;
		connector->encoder = NULL;
		intel_encoder->load_detect_temp = false;
		crtc->enabled = drm_helper_crtc_in_use(crtc);
		drm_helper_disable_unused_functions(dev);
	}

	/* Switch crtc and encoder back off if necessary */
	if (crtc->enabled && dpms_mode != DRM_MODE_DPMS_ON) {
		if (encoder->crtc == crtc)
			encoder_funcs->dpms(encoder, dpms_mode);
		crtc_funcs->dpms(crtc, dpms_mode);
	}
}

/* Returns the clock of the currently programmed mode of the given pipe. */
static int intel_crtc_clock_get(struct drm_device *dev, struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	u32 dpll = I915_READ((pipe == 0) ? DPLL_A : DPLL_B);
	u32 fp;
	intel_clock_t clock;

	if ((dpll & DISPLAY_RATE_SELECT_FPA1) == 0)
		fp = I915_READ((pipe == 0) ? FPA0 : FPB0);
	else
		fp = I915_READ((pipe == 0) ? FPA1 : FPB1);

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
	int htot = I915_READ((pipe == 0) ? HTOTAL_A : HTOTAL_B);
	int hsync = I915_READ((pipe == 0) ? HSYNC_A : HSYNC_B);
	int vtot = I915_READ((pipe == 0) ? VTOTAL_A : VTOTAL_B);
	int vsync = I915_READ((pipe == 0) ? VSYNC_A : VSYNC_B);

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
	drm_mode_set_crtcinfo(mode, 0);

	return mode;
}

#define GPU_IDLE_TIMEOUT 500 /* ms */

/* When this timer fires, we've been idle for awhile */
static void intel_gpu_idle_timer(unsigned long arg)
{
	struct drm_device *dev = (struct drm_device *)arg;
	drm_i915_private_t *dev_priv = dev->dev_private;

	dev_priv->busy = false;

	queue_work(dev_priv->wq, &dev_priv->idle_work);
}

#define CRTC_IDLE_TIMEOUT 1000 /* ms */

static void intel_crtc_idle_timer(unsigned long arg)
{
	struct intel_crtc *intel_crtc = (struct intel_crtc *)arg;
	struct drm_crtc *crtc = &intel_crtc->base;
	drm_i915_private_t *dev_priv = crtc->dev->dev_private;

	intel_crtc->busy = false;

	queue_work(dev_priv->wq, &dev_priv->idle_work);
}

static void intel_increase_pllclock(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int dpll_reg = (pipe == 0) ? DPLL_A : DPLL_B;
	int dpll = I915_READ(dpll_reg);

	if (HAS_PCH_SPLIT(dev))
		return;

	if (!dev_priv->lvds_downclock_avail)
		return;

	if (!HAS_PIPE_CXSR(dev) && (dpll & DISPLAY_RATE_SELECT_FPA1)) {
		DRM_DEBUG_DRIVER("upclocking LVDS\n");

		/* Unlock panel regs */
		I915_WRITE(PP_CONTROL, I915_READ(PP_CONTROL) |
			   PANEL_UNLOCK_REGS);

		dpll &= ~DISPLAY_RATE_SELECT_FPA1;
		I915_WRITE(dpll_reg, dpll);
		dpll = I915_READ(dpll_reg);
		intel_wait_for_vblank(dev, pipe);
		dpll = I915_READ(dpll_reg);
		if (dpll & DISPLAY_RATE_SELECT_FPA1)
			DRM_DEBUG_DRIVER("failed to upclock LVDS!\n");

		/* ...and lock them again */
		I915_WRITE(PP_CONTROL, I915_READ(PP_CONTROL) & 0x3);
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
	int pipe = intel_crtc->pipe;
	int dpll_reg = (pipe == 0) ? DPLL_A : DPLL_B;
	int dpll = I915_READ(dpll_reg);

	if (HAS_PCH_SPLIT(dev))
		return;

	if (!dev_priv->lvds_downclock_avail)
		return;

	/*
	 * Since this is called by a timer, we should never get here in
	 * the manual case.
	 */
	if (!HAS_PIPE_CXSR(dev) && intel_crtc->lowfreq_avail) {
		DRM_DEBUG_DRIVER("downclocking LVDS\n");

		/* Unlock panel regs */
		I915_WRITE(PP_CONTROL, I915_READ(PP_CONTROL) |
			   PANEL_UNLOCK_REGS);

		dpll |= DISPLAY_RATE_SELECT_FPA1;
		I915_WRITE(dpll_reg, dpll);
		dpll = I915_READ(dpll_reg);
		intel_wait_for_vblank(dev, pipe);
		dpll = I915_READ(dpll_reg);
		if (!(dpll & DISPLAY_RATE_SELECT_FPA1))
			DRM_DEBUG_DRIVER("failed to downclock LVDS!\n");

		/* ...and lock them again */
		I915_WRITE(PP_CONTROL, I915_READ(PP_CONTROL) & 0x3);
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
	int enabled = 0;

	if (!i915_powersave)
		return;

	mutex_lock(&dev->struct_mutex);

	i915_update_gfx_val(dev_priv);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		/* Skip inactive CRTCs */
		if (!crtc->fb)
			continue;

		enabled++;
		intel_crtc = to_intel_crtc(crtc);
		if (!intel_crtc->busy)
			intel_decrease_pllclock(crtc);
	}

	if ((enabled == 1) && (IS_I945G(dev) || IS_I945GM(dev))) {
		DRM_DEBUG_DRIVER("enable memory self refresh on 945\n");
		I915_WRITE(FW_BLC_SELF, FW_BLC_SELF_EN_MASK | FW_BLC_SELF_EN);
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
		if (IS_I945G(dev) || IS_I945GM(dev)) {
			u32 fw_blc_self;

			DRM_DEBUG_DRIVER("disable memory self refresh on 945\n");
			fw_blc_self = I915_READ(FW_BLC_SELF);
			fw_blc_self &= ~FW_BLC_SELF_EN;
			I915_WRITE(FW_BLC_SELF, fw_blc_self | FW_BLC_SELF_EN_MASK);
		}
		dev_priv->busy = true;
	} else
		mod_timer(&dev_priv->idle_timer, jiffies +
			  msecs_to_jiffies(GPU_IDLE_TIMEOUT));

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (!crtc->fb)
			continue;

		intel_crtc = to_intel_crtc(crtc);
		intel_fb = to_intel_framebuffer(crtc->fb);
		if (intel_fb->obj == obj) {
			if (!intel_crtc->busy) {
				if (IS_I945G(dev) || IS_I945GM(dev)) {
					u32 fw_blc_self;

					DRM_DEBUG_DRIVER("disable memory self refresh on 945\n");
					fw_blc_self = I915_READ(FW_BLC_SELF);
					fw_blc_self &= ~FW_BLC_SELF_EN;
					I915_WRITE(FW_BLC_SELF, fw_blc_self | FW_BLC_SELF_EN_MASK);
				}
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
	i915_gem_object_unpin(work->old_fb_obj);
	drm_gem_object_unreference(&work->pending_flip_obj->base);
	drm_gem_object_unreference(&work->old_fb_obj->base);

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
	struct timeval now;
	unsigned long flags;

	/* Ignore early vblank irqs */
	if (intel_crtc == NULL)
		return;

	spin_lock_irqsave(&dev->event_lock, flags);
	work = intel_crtc->unpin_work;
	if (work == NULL || !work->pending) {
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return;
	}

	intel_crtc->unpin_work = NULL;
	drm_vblank_put(dev, intel_crtc->pipe);

	if (work->event) {
		e = work->event;
		do_gettimeofday(&now);
		e->event.sequence = drm_vblank_count(dev, intel_crtc->pipe);
		e->event.tv_sec = now.tv_sec;
		e->event.tv_usec = now.tv_usec;
		list_add_tail(&e->base.link,
			      &e->base.file_priv->event_list);
		wake_up_interruptible(&e->base.file_priv->event_wait);
	}

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
	unsigned long flags, offset;
	int pipe = intel_crtc->pipe;
	u32 pf, pipesrc;
	int ret;

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (work == NULL)
		return -ENOMEM;

	work->event = event;
	work->dev = crtc->dev;
	intel_fb = to_intel_framebuffer(crtc->fb);
	work->old_fb_obj = intel_fb->obj;
	INIT_WORK(&work->work, intel_unpin_work_fn);

	/* We borrow the event spin lock for protecting unpin_work */
	spin_lock_irqsave(&dev->event_lock, flags);
	if (intel_crtc->unpin_work) {
		spin_unlock_irqrestore(&dev->event_lock, flags);
		kfree(work);

		DRM_DEBUG_DRIVER("flip queue: crtc already busy\n");
		return -EBUSY;
	}
	intel_crtc->unpin_work = work;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	intel_fb = to_intel_framebuffer(fb);
	obj = intel_fb->obj;

	mutex_lock(&dev->struct_mutex);
	ret = intel_pin_and_fence_fb_obj(dev, obj, &dev_priv->render_ring);
	if (ret)
		goto cleanup_work;

	/* Reference the objects for the scheduled work. */
	drm_gem_object_reference(&work->old_fb_obj->base);
	drm_gem_object_reference(&obj->base);

	crtc->fb = fb;

	ret = drm_vblank_get(dev, intel_crtc->pipe);
	if (ret)
		goto cleanup_objs;

	if (IS_GEN3(dev) || IS_GEN2(dev)) {
		u32 flip_mask;

		/* Can't queue multiple flips, so wait for the previous
		 * one to finish before executing the next.
		 */
		ret = BEGIN_LP_RING(2);
		if (ret)
			goto cleanup_objs;

		if (intel_crtc->plane)
			flip_mask = MI_WAIT_FOR_PLANE_B_FLIP;
		else
			flip_mask = MI_WAIT_FOR_PLANE_A_FLIP;
		OUT_RING(MI_WAIT_FOR_EVENT | flip_mask);
		OUT_RING(MI_NOOP);
		ADVANCE_LP_RING();
	}

	work->pending_flip_obj = obj;

	work->enable_stall_check = true;

	/* Offset into the new buffer for cases of shared fbs between CRTCs */
	offset = crtc->y * fb->pitch + crtc->x * fb->bits_per_pixel/8;

	ret = BEGIN_LP_RING(4);
	if (ret)
		goto cleanup_objs;

	/* Block clients from rendering to the new back buffer until
	 * the flip occurs and the object is no longer visible.
	 */
	atomic_add(1 << intel_crtc->plane, &work->old_fb_obj->pending_flip);

	switch (INTEL_INFO(dev)->gen) {
	case 2:
		OUT_RING(MI_DISPLAY_FLIP |
			 MI_DISPLAY_FLIP_PLANE(intel_crtc->plane));
		OUT_RING(fb->pitch);
		OUT_RING(obj->gtt_offset + offset);
		OUT_RING(MI_NOOP);
		break;

	case 3:
		OUT_RING(MI_DISPLAY_FLIP_I915 |
			 MI_DISPLAY_FLIP_PLANE(intel_crtc->plane));
		OUT_RING(fb->pitch);
		OUT_RING(obj->gtt_offset + offset);
		OUT_RING(MI_NOOP);
		break;

	case 4:
	case 5:
		/* i965+ uses the linear or tiled offsets from the
		 * Display Registers (which do not change across a page-flip)
		 * so we need only reprogram the base address.
		 */
		OUT_RING(MI_DISPLAY_FLIP |
			 MI_DISPLAY_FLIP_PLANE(intel_crtc->plane));
		OUT_RING(fb->pitch);
		OUT_RING(obj->gtt_offset | obj->tiling_mode);

		/* XXX Enabling the panel-fitter across page-flip is so far
		 * untested on non-native modes, so ignore it for now.
		 * pf = I915_READ(pipe == 0 ? PFA_CTL_1 : PFB_CTL_1) & PF_ENABLE;
		 */
		pf = 0;
		pipesrc = I915_READ(pipe == 0 ? PIPEASRC : PIPEBSRC) & 0x0fff0fff;
		OUT_RING(pf | pipesrc);
		break;

	case 6:
		OUT_RING(MI_DISPLAY_FLIP |
			 MI_DISPLAY_FLIP_PLANE(intel_crtc->plane));
		OUT_RING(fb->pitch | obj->tiling_mode);
		OUT_RING(obj->gtt_offset);

		pf = I915_READ(pipe == 0 ? PFA_CTL_1 : PFB_CTL_1) & PF_ENABLE;
		pipesrc = I915_READ(pipe == 0 ? PIPEASRC : PIPEBSRC) & 0x0fff0fff;
		OUT_RING(pf | pipesrc);
		break;
	}
	ADVANCE_LP_RING();

	mutex_unlock(&dev->struct_mutex);

	trace_i915_flip_request(intel_crtc->plane, obj);

	return 0;

cleanup_objs:
	drm_gem_object_unreference(&work->old_fb_obj->base);
	drm_gem_object_unreference(&obj->base);
cleanup_work:
	mutex_unlock(&dev->struct_mutex);

	spin_lock_irqsave(&dev->event_lock, flags);
	intel_crtc->unpin_work = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	kfree(work);

	return ret;
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
	.cursor_set = intel_crtc_cursor_set,
	.cursor_move = intel_crtc_cursor_move,
	.gamma_set = intel_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = intel_crtc_destroy,
	.page_flip = intel_crtc_page_flip,
};


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

	intel_crtc->cursor_addr = 0;
	intel_crtc->dpms_mode = -1;
	intel_crtc->active = true; /* force the pipe off on setup_init_config */

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
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_get_pipe_from_crtc_id *pipe_from_crtc_id = data;
	struct drm_mode_object *drmmode_obj;
	struct intel_crtc *crtc;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

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

static void intel_setup_outputs(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *encoder;
	bool dpd_is_edp = false;

	if (IS_MOBILE(dev) && !IS_I830(dev))
		intel_lvds_init(dev);

	if (HAS_PCH_SPLIT(dev)) {
		dpd_is_edp = intel_dpd_is_edp(dev);

		if (IS_MOBILE(dev) && (I915_READ(DP_A) & DP_DETECTED))
			intel_dp_init(dev, DP_A);

		if (dpd_is_edp && (I915_READ(PCH_DP_D) & DP_DETECTED))
			intel_dp_init(dev, PCH_DP_D);
	}

	intel_crt_init(dev);

	if (HAS_PCH_SPLIT(dev)) {
		int found;

		if (I915_READ(HDMIB) & PORT_DETECTED) {
			/* PCH SDVOB multiplex with HDMIB */
			found = intel_sdvo_init(dev, PCH_SDVOB);
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
			found = intel_sdvo_init(dev, SDVOB);
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
			found = intel_sdvo_init(dev, SDVOC);
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
			   struct drm_mode_fb_cmd *mode_cmd,
			   struct drm_i915_gem_object *obj)
{
	int ret;

	if (obj->tiling_mode == I915_TILING_Y)
		return -EINVAL;

	if (mode_cmd->pitch & 63)
		return -EINVAL;

	switch (mode_cmd->bpp) {
	case 8:
	case 16:
	case 24:
	case 32:
		break;
	default:
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
			      struct drm_mode_fb_cmd *mode_cmd)
{
	struct drm_i915_gem_object *obj;
	struct intel_framebuffer *intel_fb;
	int ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, filp, mode_cmd->handle));
	if (!obj)
		return ERR_PTR(-ENOENT);

	intel_fb = kzalloc(sizeof(*intel_fb), GFP_KERNEL);
	if (!intel_fb)
		return ERR_PTR(-ENOMEM);

	ret = intel_framebuffer_init(dev, intel_fb, mode_cmd, obj);
	if (ret) {
		drm_gem_object_unreference_unlocked(&obj->base);
		kfree(intel_fb);
		return ERR_PTR(ret);
	}

	return &intel_fb->base;
}

static const struct drm_mode_config_funcs intel_mode_funcs = {
	.fb_create = intel_user_framebuffer_create,
	.output_poll_changed = intel_fb_output_poll_changed,
};

static struct drm_i915_gem_object *
intel_alloc_context_page(struct drm_device *dev)
{
	struct drm_i915_gem_object *ctx;
	int ret;

	ctx = i915_gem_alloc_object(dev, 4096);
	if (!ctx) {
		DRM_DEBUG("failed to alloc power context, RC6 disabled\n");
		return NULL;
	}

	mutex_lock(&dev->struct_mutex);
	ret = i915_gem_object_pin(ctx, 4096, true);
	if (ret) {
		DRM_ERROR("failed to pin power context: %d\n", ret);
		goto err_unref;
	}

	ret = i915_gem_object_set_to_gtt_domain(ctx, 1);
	if (ret) {
		DRM_ERROR("failed to set-domain on power context: %d\n", ret);
		goto err_unpin;
	}
	mutex_unlock(&dev->struct_mutex);

	return ctx;

err_unpin:
	i915_gem_object_unpin(ctx);
err_unref:
	drm_gem_object_unreference(&ctx->base);
	mutex_unlock(&dev->struct_mutex);
	return NULL;
}

bool ironlake_set_drps(struct drm_device *dev, u8 val)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u16 rgvswctl;

	rgvswctl = I915_READ16(MEMSWCTL);
	if (rgvswctl & MEMCTL_CMD_STS) {
		DRM_DEBUG("gpu busy, RCS change rejected\n");
		return false; /* still busy with another command */
	}

	rgvswctl = (MEMCTL_CMD_CHFREQ << MEMCTL_CMD_SHIFT) |
		(val << MEMCTL_FREQ_SHIFT) | MEMCTL_SFCAVM;
	I915_WRITE16(MEMSWCTL, rgvswctl);
	POSTING_READ16(MEMSWCTL);

	rgvswctl |= MEMCTL_CMD_STS;
	I915_WRITE16(MEMSWCTL, rgvswctl);

	return true;
}

void ironlake_enable_drps(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 rgvmodectl = I915_READ(MEMMODECTL);
	u8 fmax, fmin, fstart, vstart;

	/* Enable temp reporting */
	I915_WRITE16(PMMISC, I915_READ(PMMISC) | MCPPCE_EN);
	I915_WRITE16(TSC1, I915_READ(TSC1) | TSE);

	/* 100ms RC evaluation intervals */
	I915_WRITE(RCUPEI, 100000);
	I915_WRITE(RCDNEI, 100000);

	/* Set max/min thresholds to 90ms and 80ms respectively */
	I915_WRITE(RCBMAXAVG, 90000);
	I915_WRITE(RCBMINAVG, 80000);

	I915_WRITE(MEMIHYST, 1);

	/* Set up min, max, and cur for interrupt handling */
	fmax = (rgvmodectl & MEMMODE_FMAX_MASK) >> MEMMODE_FMAX_SHIFT;
	fmin = (rgvmodectl & MEMMODE_FMIN_MASK);
	fstart = (rgvmodectl & MEMMODE_FSTART_MASK) >>
		MEMMODE_FSTART_SHIFT;

	vstart = (I915_READ(PXVFREQ_BASE + (fstart * 4)) & PXVFREQ_PX_MASK) >>
		PXVFREQ_PX_SHIFT;

	dev_priv->fmax = fmax; /* IPS callback will increase this */
	dev_priv->fstart = fstart;

	dev_priv->max_delay = fstart;
	dev_priv->min_delay = fmin;
	dev_priv->cur_delay = fstart;

	DRM_DEBUG_DRIVER("fmax: %d, fmin: %d, fstart: %d\n",
			 fmax, fmin, fstart);

	I915_WRITE(MEMINTREN, MEMINT_CX_SUPR_EN | MEMINT_EVAL_CHG_EN);

	/*
	 * Interrupts will be enabled in ironlake_irq_postinstall
	 */

	I915_WRITE(VIDSTART, vstart);
	POSTING_READ(VIDSTART);

	rgvmodectl |= MEMMODE_SWMODE_EN;
	I915_WRITE(MEMMODECTL, rgvmodectl);

	if (wait_for((I915_READ(MEMSWCTL) & MEMCTL_CMD_STS) == 0, 10))
		DRM_ERROR("stuck trying to change perf mode\n");
	msleep(1);

	ironlake_set_drps(dev, fstart);

	dev_priv->last_count1 = I915_READ(0x112e4) + I915_READ(0x112e8) +
		I915_READ(0x112e0);
	dev_priv->last_time1 = jiffies_to_msecs(jiffies);
	dev_priv->last_count2 = I915_READ(0x112f4);
	getrawmonotonic(&dev_priv->last_time2);
}

void ironlake_disable_drps(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u16 rgvswctl = I915_READ16(MEMSWCTL);

	/* Ack interrupts, disable EFC interrupt */
	I915_WRITE(MEMINTREN, I915_READ(MEMINTREN) & ~MEMINT_EVAL_CHG_EN);
	I915_WRITE(MEMINTRSTS, MEMINT_EVAL_CHG);
	I915_WRITE(DEIER, I915_READ(DEIER) & ~DE_PCU_EVENT);
	I915_WRITE(DEIIR, DE_PCU_EVENT);
	I915_WRITE(DEIMR, I915_READ(DEIMR) | DE_PCU_EVENT);

	/* Go back to the starting frequency */
	ironlake_set_drps(dev, dev_priv->fstart);
	msleep(1);
	rgvswctl |= MEMCTL_CMD_STS;
	I915_WRITE(MEMSWCTL, rgvswctl);
	msleep(1);

}

static unsigned long intel_pxfreq(u32 vidfreq)
{
	unsigned long freq;
	int div = (vidfreq & 0x3f0000) >> 16;
	int post = (vidfreq & 0x3000) >> 12;
	int pre = (vidfreq & 0x7);

	if (!pre)
		return 0;

	freq = ((div * 133333) / ((1<<post) * pre));

	return freq;
}

void intel_init_emon(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 lcfuse;
	u8 pxw[16];
	int i;

	/* Disable to program */
	I915_WRITE(ECR, 0);
	POSTING_READ(ECR);

	/* Program energy weights for various events */
	I915_WRITE(SDEW, 0x15040d00);
	I915_WRITE(CSIEW0, 0x007f0000);
	I915_WRITE(CSIEW1, 0x1e220004);
	I915_WRITE(CSIEW2, 0x04000004);

	for (i = 0; i < 5; i++)
		I915_WRITE(PEW + (i * 4), 0);
	for (i = 0; i < 3; i++)
		I915_WRITE(DEW + (i * 4), 0);

	/* Program P-state weights to account for frequency power adjustment */
	for (i = 0; i < 16; i++) {
		u32 pxvidfreq = I915_READ(PXVFREQ_BASE + (i * 4));
		unsigned long freq = intel_pxfreq(pxvidfreq);
		unsigned long vid = (pxvidfreq & PXVFREQ_PX_MASK) >>
			PXVFREQ_PX_SHIFT;
		unsigned long val;

		val = vid * vid;
		val *= (freq / 1000);
		val *= 255;
		val /= (127*127*900);
		if (val > 0xff)
			DRM_ERROR("bad pxval: %ld\n", val);
		pxw[i] = val;
	}
	/* Render standby states get 0 weight */
	pxw[14] = 0;
	pxw[15] = 0;

	for (i = 0; i < 4; i++) {
		u32 val = (pxw[i*4] << 24) | (pxw[(i*4)+1] << 16) |
			(pxw[(i*4)+2] << 8) | (pxw[(i*4)+3]);
		I915_WRITE(PXW + (i * 4), val);
	}

	/* Adjust magic regs to magic values (more experimental results) */
	I915_WRITE(OGW0, 0);
	I915_WRITE(OGW1, 0);
	I915_WRITE(EG0, 0x00007f00);
	I915_WRITE(EG1, 0x0000000e);
	I915_WRITE(EG2, 0x000e0000);
	I915_WRITE(EG3, 0x68000300);
	I915_WRITE(EG4, 0x42000000);
	I915_WRITE(EG5, 0x00140031);
	I915_WRITE(EG6, 0);
	I915_WRITE(EG7, 0);

	for (i = 0; i < 8; i++)
		I915_WRITE(PXWL + (i * 4), 0);

	/* Enable PMON + select events */
	I915_WRITE(ECR, 0x80000019);

	lcfuse = I915_READ(LCFUSE02);

	dev_priv->corr = (lcfuse & LCFUSE_HIV_MASK);
}

void intel_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/*
	 * Disable clock gating reported to work incorrectly according to the
	 * specs, but enable as much else as we can.
	 */
	if (HAS_PCH_SPLIT(dev)) {
		uint32_t dspclk_gate = VRHUNIT_CLOCK_GATE_DISABLE;

		if (IS_GEN5(dev)) {
			/* Required for FBC */
			dspclk_gate |= DPFDUNIT_CLOCK_GATE_DISABLE;
			/* Required for CxSR */
			dspclk_gate |= DPARBUNIT_CLOCK_GATE_DISABLE;

			I915_WRITE(PCH_3DCGDIS0,
				   MARIUNIT_CLOCK_GATE_DISABLE |
				   SVSMUNIT_CLOCK_GATE_DISABLE);
		}

		I915_WRITE(PCH_DSPCLK_GATE_D, dspclk_gate);

		/*
		 * On Ibex Peak and Cougar Point, we need to disable clock
		 * gating for the panel power sequencer or it will fail to
		 * start up when no ports are active.
		 */
		I915_WRITE(SOUTH_DSPCLK_GATE_D, PCH_DPLSUNIT_CLOCK_GATE_DISABLE);

		/*
		 * According to the spec the following bits should be set in
		 * order to enable memory self-refresh
		 * The bit 22/21 of 0x42004
		 * The bit 5 of 0x42020
		 * The bit 15 of 0x45000
		 */
		if (IS_GEN5(dev)) {
			I915_WRITE(ILK_DISPLAY_CHICKEN2,
					(I915_READ(ILK_DISPLAY_CHICKEN2) |
					ILK_DPARB_GATE | ILK_VSDPFD_FULL));
			I915_WRITE(ILK_DSPCLK_GATE,
					(I915_READ(ILK_DSPCLK_GATE) |
						ILK_DPARB_CLK_GATE));
			I915_WRITE(DISP_ARB_CTL,
					(I915_READ(DISP_ARB_CTL) |
						DISP_FBC_WM_DIS));
		I915_WRITE(WM3_LP_ILK, 0);
		I915_WRITE(WM2_LP_ILK, 0);
		I915_WRITE(WM1_LP_ILK, 0);
		}
		/*
		 * Based on the document from hardware guys the following bits
		 * should be set unconditionally in order to enable FBC.
		 * The bit 22 of 0x42000
		 * The bit 22 of 0x42004
		 * The bit 7,8,9 of 0x42020.
		 */
		if (IS_IRONLAKE_M(dev)) {
			I915_WRITE(ILK_DISPLAY_CHICKEN1,
				   I915_READ(ILK_DISPLAY_CHICKEN1) |
				   ILK_FBCQ_DIS);
			I915_WRITE(ILK_DISPLAY_CHICKEN2,
				   I915_READ(ILK_DISPLAY_CHICKEN2) |
				   ILK_DPARB_GATE);
			I915_WRITE(ILK_DSPCLK_GATE,
				   I915_READ(ILK_DSPCLK_GATE) |
				   ILK_DPFC_DIS1 |
				   ILK_DPFC_DIS2 |
				   ILK_CLK_FBC);
		}

		I915_WRITE(ILK_DISPLAY_CHICKEN2,
			   I915_READ(ILK_DISPLAY_CHICKEN2) |
			   ILK_ELPIN_409_SELECT);

		if (IS_GEN5(dev)) {
			I915_WRITE(_3D_CHICKEN2,
				   _3D_CHICKEN2_WM_READ_PIPELINED << 16 |
				   _3D_CHICKEN2_WM_READ_PIPELINED);
		}
		return;
	} else if (IS_G4X(dev)) {
		uint32_t dspclk_gate;
		I915_WRITE(RENCLK_GATE_D1, 0);
		I915_WRITE(RENCLK_GATE_D2, VF_UNIT_CLOCK_GATE_DISABLE |
		       GS_UNIT_CLOCK_GATE_DISABLE |
		       CL_UNIT_CLOCK_GATE_DISABLE);
		I915_WRITE(RAMCLK_GATE_D, 0);
		dspclk_gate = VRHUNIT_CLOCK_GATE_DISABLE |
			OVRUNIT_CLOCK_GATE_DISABLE |
			OVCUNIT_CLOCK_GATE_DISABLE;
		if (IS_GM45(dev))
			dspclk_gate |= DSSUNIT_CLOCK_GATE_DISABLE;
		I915_WRITE(DSPCLK_GATE_D, dspclk_gate);
	} else if (IS_CRESTLINE(dev)) {
		I915_WRITE(RENCLK_GATE_D1, I965_RCC_CLOCK_GATE_DISABLE);
		I915_WRITE(RENCLK_GATE_D2, 0);
		I915_WRITE(DSPCLK_GATE_D, 0);
		I915_WRITE(RAMCLK_GATE_D, 0);
		I915_WRITE16(DEUC, 0);
	} else if (IS_BROADWATER(dev)) {
		I915_WRITE(RENCLK_GATE_D1, I965_RCZ_CLOCK_GATE_DISABLE |
		       I965_RCC_CLOCK_GATE_DISABLE |
		       I965_RCPB_CLOCK_GATE_DISABLE |
		       I965_ISC_CLOCK_GATE_DISABLE |
		       I965_FBC_CLOCK_GATE_DISABLE);
		I915_WRITE(RENCLK_GATE_D2, 0);
	} else if (IS_GEN3(dev)) {
		u32 dstate = I915_READ(D_STATE);

		dstate |= DSTATE_PLL_D3_OFF | DSTATE_GFX_CLOCK_GATING |
			DSTATE_DOT_CLOCK_GATING;
		I915_WRITE(D_STATE, dstate);
	} else if (IS_I85X(dev) || IS_I865G(dev)) {
		I915_WRITE(RENCLK_GATE_D1, SV_CLOCK_GATE_DISABLE);
	} else if (IS_I830(dev)) {
		I915_WRITE(DSPCLK_GATE_D, OVRUNIT_CLOCK_GATE_DISABLE);
	}

	/*
	 * GPU can automatically power down the render unit if given a page
	 * to save state.
	 */
	if (IS_IRONLAKE_M(dev)) {
		if (dev_priv->renderctx == NULL)
			dev_priv->renderctx = intel_alloc_context_page(dev);
		if (dev_priv->renderctx) {
			struct drm_i915_gem_object *obj = dev_priv->renderctx;
			if (BEGIN_LP_RING(4) == 0) {
				OUT_RING(MI_SET_CONTEXT);
				OUT_RING(obj->gtt_offset |
					 MI_MM_SPACE_GTT |
					 MI_SAVE_EXT_STATE_EN |
					 MI_RESTORE_EXT_STATE_EN |
					 MI_RESTORE_INHIBIT);
				OUT_RING(MI_NOOP);
				OUT_RING(MI_FLUSH);
				ADVANCE_LP_RING();
			}
		} else
			DRM_DEBUG_KMS("Failed to allocate render context."
				       "Disable RC6\n");
	}

	if (I915_HAS_RC6(dev) && drm_core_check_feature(dev, DRIVER_MODESET)) {
		if (dev_priv->pwrctx == NULL)
			dev_priv->pwrctx = intel_alloc_context_page(dev);
		if (dev_priv->pwrctx) {
			struct drm_i915_gem_object *obj = dev_priv->pwrctx;
			I915_WRITE(PWRCTXA, obj->gtt_offset | PWRCTX_EN);
			I915_WRITE(MCHBAR_RENDER_STANDBY,
				   I915_READ(MCHBAR_RENDER_STANDBY) & ~RCX_SW_EXIT);
		}
	}
}

/* Set up chip specific display functions */
static void intel_init_display(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* We always want a DPMS function */
	if (HAS_PCH_SPLIT(dev))
		dev_priv->display.dpms = ironlake_crtc_dpms;
	else
		dev_priv->display.dpms = i9xx_crtc_dpms;

	if (I915_HAS_FBC(dev)) {
		if (IS_IRONLAKE_M(dev)) {
			dev_priv->display.fbc_enabled = ironlake_fbc_enabled;
			dev_priv->display.enable_fbc = ironlake_enable_fbc;
			dev_priv->display.disable_fbc = ironlake_disable_fbc;
		} else if (IS_GM45(dev)) {
			dev_priv->display.fbc_enabled = g4x_fbc_enabled;
			dev_priv->display.enable_fbc = g4x_enable_fbc;
			dev_priv->display.disable_fbc = g4x_disable_fbc;
		} else if (IS_CRESTLINE(dev)) {
			dev_priv->display.fbc_enabled = i8xx_fbc_enabled;
			dev_priv->display.enable_fbc = i8xx_enable_fbc;
			dev_priv->display.disable_fbc = i8xx_disable_fbc;
		}
		/* 855GM needs testing */
	}

	/* Returns the core display clock speed */
	if (IS_I945G(dev) || (IS_G33(dev) && ! IS_PINEVIEW_M(dev)))
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

	/* For FIFO watermark updates */
	if (HAS_PCH_SPLIT(dev)) {
		if (IS_GEN5(dev)) {
			if (I915_READ(MLTR_ILK) & ILK_SRLT_MASK)
				dev_priv->display.update_wm = ironlake_update_wm;
			else {
				DRM_DEBUG_KMS("Failed to get proper latency. "
					      "Disable CxSR\n");
				dev_priv->display.update_wm = NULL;
			}
		} else
			dev_priv->display.update_wm = NULL;
	} else if (IS_PINEVIEW(dev)) {
		if (!intel_get_cxsr_latency(IS_PINEVIEW_G(dev),
					    dev_priv->is_ddr3,
					    dev_priv->fsb_freq,
					    dev_priv->mem_freq)) {
			DRM_INFO("failed to find known CxSR latency "
				 "(found ddr%s fsb freq %d, mem freq %d), "
				 "disabling CxSR\n",
				 (dev_priv->is_ddr3 == 1) ? "3": "2",
				 dev_priv->fsb_freq, dev_priv->mem_freq);
			/* Disable CxSR and never update its watermark again */
			pineview_disable_cxsr(dev);
			dev_priv->display.update_wm = NULL;
		} else
			dev_priv->display.update_wm = pineview_update_wm;
	} else if (IS_G4X(dev))
		dev_priv->display.update_wm = g4x_update_wm;
	else if (IS_GEN4(dev))
		dev_priv->display.update_wm = i965_update_wm;
	else if (IS_GEN3(dev)) {
		dev_priv->display.update_wm = i9xx_update_wm;
		dev_priv->display.get_fifo_size = i9xx_get_fifo_size;
	} else if (IS_I85X(dev)) {
		dev_priv->display.update_wm = i9xx_update_wm;
		dev_priv->display.get_fifo_size = i85x_get_fifo_size;
	} else {
		dev_priv->display.update_wm = i830_update_wm;
		if (IS_845G(dev))
			dev_priv->display.get_fifo_size = i845_get_fifo_size;
		else
			dev_priv->display.get_fifo_size = i830_get_fifo_size;
	}
}

/*
 * Some BIOSes insist on assuming the GPU's pipe A is enabled at suspend,
 * resume, or other times.  This quirk makes sure that's the case for
 * affected systems.
 */
static void quirk_pipea_force (struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->quirks |= QUIRK_PIPEA_FORCE;
	DRM_DEBUG_DRIVER("applying pipe a force quirk\n");
}

struct intel_quirk {
	int device;
	int subsystem_vendor;
	int subsystem_device;
	void (*hook)(struct drm_device *dev);
};

struct intel_quirk intel_quirks[] = {
	/* HP Compaq 2730p needs pipe A force quirk (LP: #291555) */
	{ 0x2a42, 0x103c, 0x30eb, quirk_pipea_force },
	/* HP Mini needs pipe A force quirk (LP: #322104) */
	{ 0x27ae,0x103c, 0x361a, quirk_pipea_force },

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
	outb(1, VGA_SR_INDEX);
	sr1 = inb(VGA_SR_DATA);
	outb(sr1 | 1<<5, VGA_SR_DATA);
	vga_put(dev->pdev, VGA_RSRC_LEGACY_IO);
	udelay(300);

	I915_WRITE(vga_reg, VGA_DISP_DISABLE);
	POSTING_READ(vga_reg);
}

void intel_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.funcs = (void *)&intel_mode_funcs;

	intel_init_quirks(dev);

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

	/* set memory base */
	if (IS_GEN2(dev))
		dev->mode_config.fb_base = pci_resource_start(dev->pdev, 0);
	else
		dev->mode_config.fb_base = pci_resource_start(dev->pdev, 2);

	if (IS_MOBILE(dev) || !IS_GEN2(dev))
		dev_priv->num_pipe = 2;
	else
		dev_priv->num_pipe = 1;
	DRM_DEBUG_KMS("%d display pipe%s available.\n",
		      dev_priv->num_pipe, dev_priv->num_pipe > 1 ? "s" : "");

	for (i = 0; i < dev_priv->num_pipe; i++) {
		intel_crtc_init(dev, i);
	}

	intel_setup_outputs(dev);

	intel_init_clock_gating(dev);

	/* Just disable it once at startup */
	i915_disable_vga(dev);

	if (IS_IRONLAKE_M(dev)) {
		ironlake_enable_drps(dev);
		intel_init_emon(dev);
	}

	INIT_WORK(&dev_priv->idle_work, intel_idle_update);
	setup_timer(&dev_priv->idle_timer, intel_gpu_idle_timer,
		    (unsigned long)dev);

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

	if (dev_priv->display.disable_fbc)
		dev_priv->display.disable_fbc(dev);

	if (dev_priv->renderctx) {
		struct drm_i915_gem_object *obj = dev_priv->renderctx;

		I915_WRITE(CCID, obj->gtt_offset &~ CCID_EN);
		POSTING_READ(CCID);

		i915_gem_object_unpin(obj);
		drm_gem_object_unreference(&obj->base);
		dev_priv->renderctx = NULL;
	}

	if (dev_priv->pwrctx) {
		struct drm_i915_gem_object *obj = dev_priv->pwrctx;

		I915_WRITE(PWRCTXA, obj->gtt_offset &~ PWRCTX_EN);
		POSTING_READ(PWRCTXA);

		i915_gem_object_unpin(obj);
		drm_gem_object_unreference(&obj->base);
		dev_priv->pwrctx = NULL;
	}

	if (IS_IRONLAKE_M(dev))
		ironlake_disable_drps(dev);

	mutex_unlock(&dev->struct_mutex);

	/* Disable the irq before mode object teardown, for the irq might
	 * enqueue unpin/hotplug work. */
	drm_irq_uninstall(dev);
	cancel_work_sync(&dev_priv->hotplug_work);

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
		error->plane[i].pos= I915_READ(DSPPOS(i));
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
