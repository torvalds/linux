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
#include "drmP.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "intel_dp.h"

#include "drm_crtc_helper.h"

#define HAS_eDP (intel_pipe_has_type(crtc, INTEL_OUTPUT_EDP))

bool intel_pipe_has_type (struct drm_crtc *crtc, int type);
static void intel_update_watermarks(struct drm_device *dev);
static void intel_increase_pllclock(struct drm_crtc *crtc, bool schedule);

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
    bool (* find_reduced_pll)(const intel_limit_t *, struct drm_crtc *,
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
#define IGD_VCO_MIN		1700000
#define IGD_VCO_MAX		3500000
#define I9XX_N_MIN		      1
#define I9XX_N_MAX		      6
/* IGD's Ncounter is a ring counter */
#define IGD_N_MIN		      3
#define IGD_N_MAX		      6
#define I9XX_M_MIN		     70
#define I9XX_M_MAX		    120
#define IGD_M_MIN		      2
#define IGD_M_MAX		    256
#define I9XX_M1_MIN		     10
#define I9XX_M1_MAX		     22
#define I9XX_M2_MIN		      5
#define I9XX_M2_MAX		      9
/* IGD M1 is reserved, and must be 0 */
#define IGD_M1_MIN		      0
#define IGD_M1_MAX		      0
#define IGD_M2_MIN		      0
#define IGD_M2_MAX		      254
#define I9XX_P_SDVO_DAC_MIN	      5
#define I9XX_P_SDVO_DAC_MAX	     80
#define I9XX_P_LVDS_MIN		      7
#define I9XX_P_LVDS_MAX		     98
#define IGD_P_LVDS_MIN		      7
#define IGD_P_LVDS_MAX		     112
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

/* IGDNG */
/* as we calculate clock using (register_value + 2) for
   N/M1/M2, so here the range value for them is (actual_value-2).
 */
#define IGDNG_DOT_MIN         25000
#define IGDNG_DOT_MAX         350000
#define IGDNG_VCO_MIN         1760000
#define IGDNG_VCO_MAX         3510000
#define IGDNG_N_MIN           1
#define IGDNG_N_MAX           5
#define IGDNG_M_MIN           79
#define IGDNG_M_MAX           118
#define IGDNG_M1_MIN          12
#define IGDNG_M1_MAX          23
#define IGDNG_M2_MIN          5
#define IGDNG_M2_MAX          9
#define IGDNG_P_SDVO_DAC_MIN  5
#define IGDNG_P_SDVO_DAC_MAX  80
#define IGDNG_P_LVDS_MIN      28
#define IGDNG_P_LVDS_MAX      112
#define IGDNG_P1_MIN          1
#define IGDNG_P1_MAX          8
#define IGDNG_P2_SDVO_DAC_SLOW 10
#define IGDNG_P2_SDVO_DAC_FAST 5
#define IGDNG_P2_LVDS_SLOW    14 /* single channel */
#define IGDNG_P2_LVDS_FAST    7  /* double channel */
#define IGDNG_P2_DOT_LIMIT    225000 /* 225Mhz */

static bool
intel_find_best_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
		    int target, int refclk, intel_clock_t *best_clock);
static bool
intel_find_best_reduced_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
			    int target, int refclk, intel_clock_t *best_clock);
static bool
intel_g4x_find_best_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
			int target, int refclk, intel_clock_t *best_clock);
static bool
intel_igdng_find_best_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
			int target, int refclk, intel_clock_t *best_clock);

static bool
intel_find_pll_g4x_dp(const intel_limit_t *, struct drm_crtc *crtc,
		      int target, int refclk, intel_clock_t *best_clock);
static bool
intel_find_pll_igdng_dp(const intel_limit_t *, struct drm_crtc *crtc,
		      int target, int refclk, intel_clock_t *best_clock);

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
	.find_reduced_pll = intel_find_best_reduced_PLL,
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
	.find_reduced_pll = intel_find_best_reduced_PLL,
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
	.find_reduced_pll = intel_find_best_reduced_PLL,
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
	.find_reduced_pll = intel_find_best_reduced_PLL,
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
	.find_reduced_pll = intel_g4x_find_best_PLL,
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
	.find_reduced_pll = intel_g4x_find_best_PLL,
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
	.find_reduced_pll = intel_g4x_find_best_PLL,
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
	.find_reduced_pll = intel_g4x_find_best_PLL,
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

static const intel_limit_t intel_limits_igd_sdvo = {
        .dot = { .min = I9XX_DOT_MIN,		.max = I9XX_DOT_MAX},
        .vco = { .min = IGD_VCO_MIN,		.max = IGD_VCO_MAX },
        .n   = { .min = IGD_N_MIN,		.max = IGD_N_MAX },
        .m   = { .min = IGD_M_MIN,		.max = IGD_M_MAX },
        .m1  = { .min = IGD_M1_MIN,		.max = IGD_M1_MAX },
        .m2  = { .min = IGD_M2_MIN,		.max = IGD_M2_MAX },
        .p   = { .min = I9XX_P_SDVO_DAC_MIN,    .max = I9XX_P_SDVO_DAC_MAX },
        .p1  = { .min = I9XX_P1_MIN,		.max = I9XX_P1_MAX },
	.p2  = { .dot_limit = I9XX_P2_SDVO_DAC_SLOW_LIMIT,
		 .p2_slow = I9XX_P2_SDVO_DAC_SLOW,	.p2_fast = I9XX_P2_SDVO_DAC_FAST },
	.find_pll = intel_find_best_PLL,
	.find_reduced_pll = intel_find_best_reduced_PLL,
};

static const intel_limit_t intel_limits_igd_lvds = {
        .dot = { .min = I9XX_DOT_MIN,		.max = I9XX_DOT_MAX },
        .vco = { .min = IGD_VCO_MIN,		.max = IGD_VCO_MAX },
        .n   = { .min = IGD_N_MIN,		.max = IGD_N_MAX },
        .m   = { .min = IGD_M_MIN,		.max = IGD_M_MAX },
        .m1  = { .min = IGD_M1_MIN,		.max = IGD_M1_MAX },
        .m2  = { .min = IGD_M2_MIN,		.max = IGD_M2_MAX },
        .p   = { .min = IGD_P_LVDS_MIN,	.max = IGD_P_LVDS_MAX },
        .p1  = { .min = I9XX_P1_MIN,		.max = I9XX_P1_MAX },
	/* IGD only supports single-channel mode. */
	.p2  = { .dot_limit = I9XX_P2_LVDS_SLOW_LIMIT,
		 .p2_slow = I9XX_P2_LVDS_SLOW,	.p2_fast = I9XX_P2_LVDS_SLOW },
	.find_pll = intel_find_best_PLL,
	.find_reduced_pll = intel_find_best_reduced_PLL,
};

static const intel_limit_t intel_limits_igdng_sdvo = {
	.dot = { .min = IGDNG_DOT_MIN,          .max = IGDNG_DOT_MAX },
	.vco = { .min = IGDNG_VCO_MIN,          .max = IGDNG_VCO_MAX },
	.n   = { .min = IGDNG_N_MIN,            .max = IGDNG_N_MAX },
	.m   = { .min = IGDNG_M_MIN,            .max = IGDNG_M_MAX },
	.m1  = { .min = IGDNG_M1_MIN,           .max = IGDNG_M1_MAX },
	.m2  = { .min = IGDNG_M2_MIN,           .max = IGDNG_M2_MAX },
	.p   = { .min = IGDNG_P_SDVO_DAC_MIN,   .max = IGDNG_P_SDVO_DAC_MAX },
	.p1  = { .min = IGDNG_P1_MIN,           .max = IGDNG_P1_MAX },
	.p2  = { .dot_limit = IGDNG_P2_DOT_LIMIT,
		 .p2_slow = IGDNG_P2_SDVO_DAC_SLOW,
		 .p2_fast = IGDNG_P2_SDVO_DAC_FAST },
	.find_pll = intel_igdng_find_best_PLL,
};

static const intel_limit_t intel_limits_igdng_lvds = {
	.dot = { .min = IGDNG_DOT_MIN,          .max = IGDNG_DOT_MAX },
	.vco = { .min = IGDNG_VCO_MIN,          .max = IGDNG_VCO_MAX },
	.n   = { .min = IGDNG_N_MIN,            .max = IGDNG_N_MAX },
	.m   = { .min = IGDNG_M_MIN,            .max = IGDNG_M_MAX },
	.m1  = { .min = IGDNG_M1_MIN,           .max = IGDNG_M1_MAX },
	.m2  = { .min = IGDNG_M2_MIN,           .max = IGDNG_M2_MAX },
	.p   = { .min = IGDNG_P_LVDS_MIN,       .max = IGDNG_P_LVDS_MAX },
	.p1  = { .min = IGDNG_P1_MIN,           .max = IGDNG_P1_MAX },
	.p2  = { .dot_limit = IGDNG_P2_DOT_LIMIT,
		 .p2_slow = IGDNG_P2_LVDS_SLOW,
		 .p2_fast = IGDNG_P2_LVDS_FAST },
	.find_pll = intel_igdng_find_best_PLL,
};

static const intel_limit_t *intel_igdng_limit(struct drm_crtc *crtc)
{
	const intel_limit_t *limit;
	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS))
		limit = &intel_limits_igdng_lvds;
	else
		limit = &intel_limits_igdng_sdvo;

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

	if (IS_IGDNG(dev))
		limit = intel_igdng_limit(crtc);
	else if (IS_G4X(dev)) {
		limit = intel_g4x_limit(crtc);
	} else if (IS_I9XX(dev) && !IS_IGD(dev)) {
		if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_i9xx_lvds;
		else
			limit = &intel_limits_i9xx_sdvo;
	} else if (IS_IGD(dev)) {
		if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_igd_lvds;
		else
			limit = &intel_limits_igd_sdvo;
	} else {
		if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS))
			limit = &intel_limits_i8xx_lvds;
		else
			limit = &intel_limits_i8xx_dvo;
	}
	return limit;
}

/* m1 is reserved as 0 in IGD, n is a ring counter */
static void igd_clock(int refclk, intel_clock_t *clock)
{
	clock->m = clock->m2 + 2;
	clock->p = clock->p1 * clock->p2;
	clock->vco = refclk * clock->m / clock->n;
	clock->dot = clock->vco / clock->p;
}

static void intel_clock(struct drm_device *dev, int refclk, intel_clock_t *clock)
{
	if (IS_IGD(dev)) {
		igd_clock(refclk, clock);
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
bool intel_pipe_has_type (struct drm_crtc *crtc, int type)
{
    struct drm_device *dev = crtc->dev;
    struct drm_mode_config *mode_config = &dev->mode_config;
    struct drm_connector *l_entry;

    list_for_each_entry(l_entry, &mode_config->connector_list, head) {
	    if (l_entry->encoder &&
	        l_entry->encoder->crtc == crtc) {
		    struct intel_output *intel_output = to_intel_output(l_entry);
		    if (intel_output->type == type)
			    return true;
	    }
    }
    return false;
}

struct drm_connector *
intel_pipe_get_output (struct drm_crtc *crtc)
{
    struct drm_device *dev = crtc->dev;
    struct drm_mode_config *mode_config = &dev->mode_config;
    struct drm_connector *l_entry, *ret = NULL;

    list_for_each_entry(l_entry, &mode_config->connector_list, head) {
	    if (l_entry->encoder &&
	        l_entry->encoder->crtc == crtc) {
		    ret = l_entry;
		    break;
	    }
    }
    return ret;
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
	if (clock->m1 <= clock->m2 && !IS_IGD(dev))
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

	for (clock.p1 = limit->p1.max; clock.p1 >= limit->p1.min; clock.p1--) {
		for (clock.m1 = limit->m1.min; clock.m1 <= limit->m1.max;
		     clock.m1++) {
			for (clock.m2 = limit->m2.min;
			     clock.m2 <= limit->m2.max; clock.m2++) {
				/* m1 is always 0 in IGD */
				if (clock.m2 >= clock.m1 && !IS_IGD(dev))
					break;
				for (clock.n = limit->n.min;
				     clock.n <= limit->n.max; clock.n++) {
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
intel_find_best_reduced_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
			    int target, int refclk, intel_clock_t *best_clock)

{
	struct drm_device *dev = crtc->dev;
	intel_clock_t clock;
	int err = target;
	bool found = false;

	memcpy(&clock, best_clock, sizeof(intel_clock_t));

	for (clock.m1 = limit->m1.min; clock.m1 <= limit->m1.max; clock.m1++) {
		for (clock.m2 = limit->m2.min; clock.m2 <= limit->m2.max; clock.m2++) {
			/* m1 is always 0 in IGD */
			if (clock.m2 >= clock.m1 && !IS_IGD(dev))
				break;
			for (clock.n = limit->n.min; clock.n <= limit->n.max;
			     clock.n++) {
				int this_err;

				intel_clock(dev, refclk, &clock);

				if (!intel_PLL_is_valid(crtc, &clock))
					continue;

				this_err = abs(clock.dot - target);
				if (this_err < err) {
					*best_clock = clock;
					err = this_err;
					found = true;
				}
			}
		}
	}

	return found;
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
	/* approximately equals target * 0.00488 */
	int err_most = (target >> 8) + (target >> 10);
	found = false;

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
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

	memset(best_clock, 0, sizeof(*best_clock));
	max_n = limit->n.max;
	/* based on hardware requriment prefer smaller n to precision */
	for (clock.n = limit->n.min; clock.n <= max_n; clock.n++) {
		/* based on hardware requirment prefere larger m1,m2 */
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
intel_find_pll_igdng_dp(const intel_limit_t *limit, struct drm_crtc *crtc,
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

static bool
intel_igdng_find_best_PLL(const intel_limit_t *limit, struct drm_crtc *crtc,
			int target, int refclk, intel_clock_t *best_clock)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	intel_clock_t clock;
	int err_most = 47;
	int err_min = 10000;

	/* eDP has only 2 clock choice, no n/m/p setting */
	if (HAS_eDP)
		return true;

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_DISPLAYPORT))
		return intel_find_pll_igdng_dp(limit, crtc, target,
					       refclk, best_clock);

	if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
		if ((I915_READ(PCH_LVDS) & LVDS_CLKB_POWER_MASK) ==
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
	for (clock.p1 = limit->p1.max; clock.p1 >= limit->p1.min; clock.p1--) {
		/* based on hardware requriment prefer smaller n to precision */
		for (clock.n = limit->n.min; clock.n <= limit->n.max; clock.n++) {
			/* based on hardware requirment prefere larger m1,m2 */
			for (clock.m1 = limit->m1.max;
			     clock.m1 >= limit->m1.min; clock.m1--) {
				for (clock.m2 = limit->m2.max;
				     clock.m2 >= limit->m2.min; clock.m2--) {
					int this_err;

					intel_clock(dev, refclk, &clock);
					if (!intel_PLL_is_valid(crtc, &clock))
						continue;
					this_err = abs((10000 - (target*10000/clock.dot)));
					if (this_err < err_most) {
						*best_clock = clock;
						/* found on first matching */
						goto out;
					} else if (this_err < err_min) {
						*best_clock = clock;
						err_min = this_err;
					}
				}
			}
		}
	}
out:
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

void
intel_wait_for_vblank(struct drm_device *dev)
{
	/* Wait for 20ms, i.e. one cycle at 50hz. */
	mdelay(20);
}

/* Parameters have changed, update FBC info */
static void i8xx_enable_fbc(struct drm_crtc *crtc, unsigned long interval)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj_priv = intel_fb->obj->driver_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int plane, i;
	u32 fbc_ctl, fbc_ctl2;

	dev_priv->cfb_pitch = dev_priv->cfb_size / FBC_LL_SIZE;

	if (fb->pitch < dev_priv->cfb_pitch)
		dev_priv->cfb_pitch = fb->pitch;

	/* FBC_CTL wants 64B units */
	dev_priv->cfb_pitch = (dev_priv->cfb_pitch / 64) - 1;
	dev_priv->cfb_fence = obj_priv->fence_reg;
	dev_priv->cfb_plane = intel_crtc->plane;
	plane = dev_priv->cfb_plane == 0 ? FBC_CTL_PLANEA : FBC_CTL_PLANEB;

	/* Clear old tags */
	for (i = 0; i < (FBC_LL_SIZE / 32) + 1; i++)
		I915_WRITE(FBC_TAG + (i * 4), 0);

	/* Set it up... */
	fbc_ctl2 = FBC_CTL_FENCE_DBL | FBC_CTL_IDLE_IMM | plane;
	if (obj_priv->tiling_mode != I915_TILING_NONE)
		fbc_ctl2 |= FBC_CTL_CPU_FENCE;
	I915_WRITE(FBC_CONTROL2, fbc_ctl2);
	I915_WRITE(FBC_FENCE_OFF, crtc->y);

	/* enable it... */
	fbc_ctl = FBC_CTL_EN | FBC_CTL_PERIODIC;
	fbc_ctl |= (dev_priv->cfb_pitch & 0xff) << FBC_CTL_STRIDE_SHIFT;
	fbc_ctl |= (interval & 0x2fff) << FBC_CTL_INTERVAL_SHIFT;
	if (obj_priv->tiling_mode != I915_TILING_NONE)
		fbc_ctl |= dev_priv->cfb_fence;
	I915_WRITE(FBC_CONTROL, fbc_ctl);

	DRM_DEBUG("enabled FBC, pitch %ld, yoff %d, plane %d, ",
		  dev_priv->cfb_pitch, crtc->y, dev_priv->cfb_plane);
}

void i8xx_disable_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 fbc_ctl;

	if (!I915_HAS_FBC(dev))
		return;

	/* Disable compression */
	fbc_ctl = I915_READ(FBC_CONTROL);
	fbc_ctl &= ~FBC_CTL_EN;
	I915_WRITE(FBC_CONTROL, fbc_ctl);

	/* Wait for compressing bit to clear */
	while (I915_READ(FBC_STATUS) & FBC_STAT_COMPRESSING)
		; /* nothing */

	intel_wait_for_vblank(dev);

	DRM_DEBUG("disabled FBC\n");
}

static bool i8xx_fbc_enabled(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(FBC_CONTROL) & FBC_CTL_EN;
}

static void g4x_enable_fbc(struct drm_crtc *crtc, unsigned long interval)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj_priv = intel_fb->obj->driver_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int plane = (intel_crtc->plane == 0 ? DPFC_CTL_PLANEA :
		     DPFC_CTL_PLANEB);
	unsigned long stall_watermark = 200;
	u32 dpfc_ctl;

	dev_priv->cfb_pitch = (dev_priv->cfb_pitch / 64) - 1;
	dev_priv->cfb_fence = obj_priv->fence_reg;
	dev_priv->cfb_plane = intel_crtc->plane;

	dpfc_ctl = plane | DPFC_SR_EN | DPFC_CTL_LIMIT_1X;
	if (obj_priv->tiling_mode != I915_TILING_NONE) {
		dpfc_ctl |= DPFC_CTL_FENCE_EN | dev_priv->cfb_fence;
		I915_WRITE(DPFC_CHICKEN, DPFC_HT_MODIFY);
	} else {
		I915_WRITE(DPFC_CHICKEN, ~DPFC_HT_MODIFY);
	}

	I915_WRITE(DPFC_CONTROL, dpfc_ctl);
	I915_WRITE(DPFC_RECOMP_CTL, DPFC_RECOMP_STALL_EN |
		   (stall_watermark << DPFC_RECOMP_STALL_WM_SHIFT) |
		   (interval << DPFC_RECOMP_TIMER_COUNT_SHIFT));
	I915_WRITE(DPFC_FENCE_YOFF, crtc->y);

	/* enable it... */
	I915_WRITE(DPFC_CONTROL, I915_READ(DPFC_CONTROL) | DPFC_CTL_EN);

	DRM_DEBUG("enabled fbc on plane %d\n", intel_crtc->plane);
}

void g4x_disable_fbc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpfc_ctl;

	/* Disable compression */
	dpfc_ctl = I915_READ(DPFC_CONTROL);
	dpfc_ctl &= ~DPFC_CTL_EN;
	I915_WRITE(DPFC_CONTROL, dpfc_ctl);
	intel_wait_for_vblank(dev);

	DRM_DEBUG("disabled FBC\n");
}

static bool g4x_fbc_enabled(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	return I915_READ(DPFC_CONTROL) & DPFC_CTL_EN;
}

/**
 * intel_update_fbc - enable/disable FBC as needed
 * @crtc: CRTC to point the compressor at
 * @mode: mode in use
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
static void intel_update_fbc(struct drm_crtc *crtc,
			     struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = crtc->fb;
	struct intel_framebuffer *intel_fb;
	struct drm_i915_gem_object *obj_priv;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int plane = intel_crtc->plane;

	if (!i915_powersave)
		return;

	if (!dev_priv->display.fbc_enabled ||
	    !dev_priv->display.enable_fbc ||
	    !dev_priv->display.disable_fbc)
		return;

	if (!crtc->fb)
		return;

	intel_fb = to_intel_framebuffer(fb);
	obj_priv = intel_fb->obj->driver_private;

	/*
	 * If FBC is already on, we just have to verify that we can
	 * keep it that way...
	 * Need to disable if:
	 *   - changing FBC params (stride, fence, mode)
	 *   - new fb is too large to fit in compressed buffer
	 *   - going to an unsupported config (interlace, pixel multiply, etc.)
	 */
	if (intel_fb->obj->size > dev_priv->cfb_size) {
		DRM_DEBUG("framebuffer too large, disabling compression\n");
		goto out_disable;
	}
	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) ||
	    (mode->flags & DRM_MODE_FLAG_DBLSCAN)) {
		DRM_DEBUG("mode incompatible with compression, disabling\n");
		goto out_disable;
	}
	if ((mode->hdisplay > 2048) ||
	    (mode->vdisplay > 1536)) {
		DRM_DEBUG("mode too large for compression, disabling\n");
		goto out_disable;
	}
	if ((IS_I915GM(dev) || IS_I945GM(dev)) && plane != 0) {
		DRM_DEBUG("plane not 0, disabling compression\n");
		goto out_disable;
	}
	if (obj_priv->tiling_mode != I915_TILING_X) {
		DRM_DEBUG("framebuffer not tiled, disabling compression\n");
		goto out_disable;
	}

	if (dev_priv->display.fbc_enabled(crtc)) {
		/* We can re-enable it in this case, but need to update pitch */
		if (fb->pitch > dev_priv->cfb_pitch)
			dev_priv->display.disable_fbc(dev);
		if (obj_priv->fence_reg != dev_priv->cfb_fence)
			dev_priv->display.disable_fbc(dev);
		if (plane != dev_priv->cfb_plane)
			dev_priv->display.disable_fbc(dev);
	}

	if (!dev_priv->display.fbc_enabled(crtc)) {
		/* Now try to turn it back on if possible */
		dev_priv->display.enable_fbc(crtc, 500);
	}

	return;

out_disable:
	DRM_DEBUG("unsupported config, disabling FBC\n");
	/* Multiple disables should be harmless */
	if (dev_priv->display.fbc_enabled(crtc))
		dev_priv->display.disable_fbc(dev);
}

static int
intel_pipe_set_base(struct drm_crtc *crtc, int x, int y,
		    struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_framebuffer *intel_fb;
	struct drm_i915_gem_object *obj_priv;
	struct drm_gem_object *obj;
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;
	unsigned long Start, Offset;
	int dspbase = (plane == 0 ? DSPAADDR : DSPBADDR);
	int dspsurf = (plane == 0 ? DSPASURF : DSPBSURF);
	int dspstride = (plane == 0) ? DSPASTRIDE : DSPBSTRIDE;
	int dsptileoff = (plane == 0 ? DSPATILEOFF : DSPBTILEOFF);
	int dspcntr_reg = (plane == 0) ? DSPACNTR : DSPBCNTR;
	u32 dspcntr, alignment;
	int ret;

	/* no fb bound */
	if (!crtc->fb) {
		DRM_DEBUG("No FB bound\n");
		return 0;
	}

	switch (plane) {
	case 0:
	case 1:
		break;
	default:
		DRM_ERROR("Can't update plane %d in SAREA\n", plane);
		return -EINVAL;
	}

	intel_fb = to_intel_framebuffer(crtc->fb);
	obj = intel_fb->obj;
	obj_priv = obj->driver_private;

	switch (obj_priv->tiling_mode) {
	case I915_TILING_NONE:
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

	mutex_lock(&dev->struct_mutex);
	ret = i915_gem_object_pin(obj, alignment);
	if (ret != 0) {
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	ret = i915_gem_object_set_to_gtt_domain(obj, 1);
	if (ret != 0) {
		i915_gem_object_unpin(obj);
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	/* Install a fence for tiled scan-out. Pre-i965 always needs a fence,
	 * whereas 965+ only requires a fence if using framebuffer compression.
	 * For simplicity, we always install a fence as the cost is not that onerous.
	 */
	if (obj_priv->fence_reg == I915_FENCE_REG_NONE &&
	    obj_priv->tiling_mode != I915_TILING_NONE) {
		ret = i915_gem_object_get_fence_reg(obj);
		if (ret != 0) {
			i915_gem_object_unpin(obj);
			mutex_unlock(&dev->struct_mutex);
			return ret;
		}
	}

	dspcntr = I915_READ(dspcntr_reg);
	/* Mask out pixel format bits in case we change it */
	dspcntr &= ~DISPPLANE_PIXFORMAT_MASK;
	switch (crtc->fb->bits_per_pixel) {
	case 8:
		dspcntr |= DISPPLANE_8BPP;
		break;
	case 16:
		if (crtc->fb->depth == 15)
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
		i915_gem_object_unpin(obj);
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}
	if (IS_I965G(dev)) {
		if (obj_priv->tiling_mode != I915_TILING_NONE)
			dspcntr |= DISPPLANE_TILED;
		else
			dspcntr &= ~DISPPLANE_TILED;
	}

	if (IS_IGDNG(dev))
		/* must disable */
		dspcntr |= DISPPLANE_TRICKLE_FEED_DISABLE;

	I915_WRITE(dspcntr_reg, dspcntr);

	Start = obj_priv->gtt_offset;
	Offset = y * crtc->fb->pitch + x * (crtc->fb->bits_per_pixel / 8);

	DRM_DEBUG("Writing base %08lX %08lX %d %d\n", Start, Offset, x, y);
	I915_WRITE(dspstride, crtc->fb->pitch);
	if (IS_I965G(dev)) {
		I915_WRITE(dspbase, Offset);
		I915_READ(dspbase);
		I915_WRITE(dspsurf, Start);
		I915_READ(dspsurf);
		I915_WRITE(dsptileoff, (y << 16) | x);
	} else {
		I915_WRITE(dspbase, Start + Offset);
		I915_READ(dspbase);
	}

	if ((IS_I965G(dev) || plane == 0))
		intel_update_fbc(crtc, &crtc->mode);

	intel_wait_for_vblank(dev);

	if (old_fb) {
		intel_fb = to_intel_framebuffer(old_fb);
		obj_priv = intel_fb->obj->driver_private;
		i915_gem_object_unpin(intel_fb->obj);
	}
	intel_increase_pllclock(crtc, true);

	mutex_unlock(&dev->struct_mutex);

	if (!dev->primary->master)
		return 0;

	master_priv = dev->primary->master->driver_priv;
	if (!master_priv->sarea_priv)
		return 0;

	if (pipe) {
		master_priv->sarea_priv->pipeB_x = x;
		master_priv->sarea_priv->pipeB_y = y;
	} else {
		master_priv->sarea_priv->pipeA_x = x;
		master_priv->sarea_priv->pipeA_y = y;
	}

	return 0;
}

/* Disable the VGA plane that we never use */
static void i915_disable_vga (struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u8 sr1;
	u32 vga_reg;

	if (IS_IGDNG(dev))
		vga_reg = CPU_VGACNTRL;
	else
		vga_reg = VGACNTRL;

	if (I915_READ(vga_reg) & VGA_DISP_DISABLE)
		return;

	I915_WRITE8(VGA_SR_INDEX, 1);
	sr1 = I915_READ8(VGA_SR_DATA);
	I915_WRITE8(VGA_SR_DATA, sr1 | (1 << 5));
	udelay(100);

	I915_WRITE(vga_reg, VGA_DISP_DISABLE);
}

static void igdng_disable_pll_edp (struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpa_ctl;

	DRM_DEBUG("\n");
	dpa_ctl = I915_READ(DP_A);
	dpa_ctl &= ~DP_PLL_ENABLE;
	I915_WRITE(DP_A, dpa_ctl);
}

static void igdng_enable_pll_edp (struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpa_ctl;

	dpa_ctl = I915_READ(DP_A);
	dpa_ctl |= DP_PLL_ENABLE;
	I915_WRITE(DP_A, dpa_ctl);
	udelay(200);
}


static void igdng_set_pll_edp (struct drm_crtc *crtc, int clock)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpa_ctl;

	DRM_DEBUG("eDP PLL enable for clock %d\n", clock);
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

	udelay(500);
}

static void igdng_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;
	int pch_dpll_reg = (pipe == 0) ? PCH_DPLL_A : PCH_DPLL_B;
	int pipeconf_reg = (pipe == 0) ? PIPEACONF : PIPEBCONF;
	int dspcntr_reg = (plane == 0) ? DSPACNTR : DSPBCNTR;
	int dspbase_reg = (plane == 0) ? DSPAADDR : DSPBADDR;
	int fdi_tx_reg = (pipe == 0) ? FDI_TXA_CTL : FDI_TXB_CTL;
	int fdi_rx_reg = (pipe == 0) ? FDI_RXA_CTL : FDI_RXB_CTL;
	int fdi_rx_iir_reg = (pipe == 0) ? FDI_RXA_IIR : FDI_RXB_IIR;
	int fdi_rx_imr_reg = (pipe == 0) ? FDI_RXA_IMR : FDI_RXB_IMR;
	int transconf_reg = (pipe == 0) ? TRANSACONF : TRANSBCONF;
	int pf_ctl_reg = (pipe == 0) ? PFA_CTL_1 : PFB_CTL_1;
	int pf_win_size = (pipe == 0) ? PFA_WIN_SZ : PFB_WIN_SZ;
	int pf_win_pos = (pipe == 0) ? PFA_WIN_POS : PFB_WIN_POS;
	int cpu_htot_reg = (pipe == 0) ? HTOTAL_A : HTOTAL_B;
	int cpu_hblank_reg = (pipe == 0) ? HBLANK_A : HBLANK_B;
	int cpu_hsync_reg = (pipe == 0) ? HSYNC_A : HSYNC_B;
	int cpu_vtot_reg = (pipe == 0) ? VTOTAL_A : VTOTAL_B;
	int cpu_vblank_reg = (pipe == 0) ? VBLANK_A : VBLANK_B;
	int cpu_vsync_reg = (pipe == 0) ? VSYNC_A : VSYNC_B;
	int trans_htot_reg = (pipe == 0) ? TRANS_HTOTAL_A : TRANS_HTOTAL_B;
	int trans_hblank_reg = (pipe == 0) ? TRANS_HBLANK_A : TRANS_HBLANK_B;
	int trans_hsync_reg = (pipe == 0) ? TRANS_HSYNC_A : TRANS_HSYNC_B;
	int trans_vtot_reg = (pipe == 0) ? TRANS_VTOTAL_A : TRANS_VTOTAL_B;
	int trans_vblank_reg = (pipe == 0) ? TRANS_VBLANK_A : TRANS_VBLANK_B;
	int trans_vsync_reg = (pipe == 0) ? TRANS_VSYNC_A : TRANS_VSYNC_B;
	u32 temp;
	int tries = 5, j, n;
	u32 pipe_bpc;

	temp = I915_READ(pipeconf_reg);
	pipe_bpc = temp & PIPE_BPC_MASK;

	/* XXX: When our outputs are all unaware of DPMS modes other than off
	 * and on, we should map those modes to DRM_MODE_DPMS_OFF in the CRTC.
	 */
	switch (mode) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		DRM_DEBUG("crtc %d dpms on\n", pipe);

		if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
			temp = I915_READ(PCH_LVDS);
			if ((temp & LVDS_PORT_EN) == 0) {
				I915_WRITE(PCH_LVDS, temp | LVDS_PORT_EN);
				POSTING_READ(PCH_LVDS);
			}
		}

		if (HAS_eDP) {
			/* enable eDP PLL */
			igdng_enable_pll_edp(crtc);
		} else {
			/* enable PCH DPLL */
			temp = I915_READ(pch_dpll_reg);
			if ((temp & DPLL_VCO_ENABLE) == 0) {
				I915_WRITE(pch_dpll_reg, temp | DPLL_VCO_ENABLE);
				I915_READ(pch_dpll_reg);
			}

			/* enable PCH FDI RX PLL, wait warmup plus DMI latency */
			temp = I915_READ(fdi_rx_reg);
			/*
			 * make the BPC in FDI Rx be consistent with that in
			 * pipeconf reg.
			 */
			temp &= ~(0x7 << 16);
			temp |= (pipe_bpc << 11);
			I915_WRITE(fdi_rx_reg, temp | FDI_RX_PLL_ENABLE |
					FDI_SEL_PCDCLK |
					FDI_DP_PORT_WIDTH_X4); /* default 4 lanes */
			I915_READ(fdi_rx_reg);
			udelay(200);

			/* Enable CPU FDI TX PLL, always on for IGDNG */
			temp = I915_READ(fdi_tx_reg);
			if ((temp & FDI_TX_PLL_ENABLE) == 0) {
				I915_WRITE(fdi_tx_reg, temp | FDI_TX_PLL_ENABLE);
				I915_READ(fdi_tx_reg);
				udelay(100);
			}
		}

		/* Enable panel fitting for LVDS */
		if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
			temp = I915_READ(pf_ctl_reg);
			I915_WRITE(pf_ctl_reg, temp | PF_ENABLE | PF_FILTER_MED_3x3);

			/* currently full aspect */
			I915_WRITE(pf_win_pos, 0);

			I915_WRITE(pf_win_size,
				   (dev_priv->panel_fixed_mode->hdisplay << 16) |
				   (dev_priv->panel_fixed_mode->vdisplay));
		}

		/* Enable CPU pipe */
		temp = I915_READ(pipeconf_reg);
		if ((temp & PIPEACONF_ENABLE) == 0) {
			I915_WRITE(pipeconf_reg, temp | PIPEACONF_ENABLE);
			I915_READ(pipeconf_reg);
			udelay(100);
		}

		/* configure and enable CPU plane */
		temp = I915_READ(dspcntr_reg);
		if ((temp & DISPLAY_PLANE_ENABLE) == 0) {
			I915_WRITE(dspcntr_reg, temp | DISPLAY_PLANE_ENABLE);
			/* Flush the plane changes */
			I915_WRITE(dspbase_reg, I915_READ(dspbase_reg));
		}

		if (!HAS_eDP) {
			/* enable CPU FDI TX and PCH FDI RX */
			temp = I915_READ(fdi_tx_reg);
			temp |= FDI_TX_ENABLE;
			temp |= FDI_DP_PORT_WIDTH_X4; /* default */
			temp &= ~FDI_LINK_TRAIN_NONE;
			temp |= FDI_LINK_TRAIN_PATTERN_1;
			I915_WRITE(fdi_tx_reg, temp);
			I915_READ(fdi_tx_reg);

			temp = I915_READ(fdi_rx_reg);
			temp &= ~FDI_LINK_TRAIN_NONE;
			temp |= FDI_LINK_TRAIN_PATTERN_1;
			I915_WRITE(fdi_rx_reg, temp | FDI_RX_ENABLE);
			I915_READ(fdi_rx_reg);

			udelay(150);

			/* Train FDI. */
			/* umask FDI RX Interrupt symbol_lock and bit_lock bit
			   for train result */
			temp = I915_READ(fdi_rx_imr_reg);
			temp &= ~FDI_RX_SYMBOL_LOCK;
			temp &= ~FDI_RX_BIT_LOCK;
			I915_WRITE(fdi_rx_imr_reg, temp);
			I915_READ(fdi_rx_imr_reg);
			udelay(150);

			temp = I915_READ(fdi_rx_iir_reg);
			DRM_DEBUG("FDI_RX_IIR 0x%x\n", temp);

			if ((temp & FDI_RX_BIT_LOCK) == 0) {
				for (j = 0; j < tries; j++) {
					temp = I915_READ(fdi_rx_iir_reg);
					DRM_DEBUG("FDI_RX_IIR 0x%x\n", temp);
					if (temp & FDI_RX_BIT_LOCK)
						break;
					udelay(200);
				}
				if (j != tries)
					I915_WRITE(fdi_rx_iir_reg,
							temp | FDI_RX_BIT_LOCK);
				else
					DRM_DEBUG("train 1 fail\n");
			} else {
				I915_WRITE(fdi_rx_iir_reg,
						temp | FDI_RX_BIT_LOCK);
				DRM_DEBUG("train 1 ok 2!\n");
			}
			temp = I915_READ(fdi_tx_reg);
			temp &= ~FDI_LINK_TRAIN_NONE;
			temp |= FDI_LINK_TRAIN_PATTERN_2;
			I915_WRITE(fdi_tx_reg, temp);

			temp = I915_READ(fdi_rx_reg);
			temp &= ~FDI_LINK_TRAIN_NONE;
			temp |= FDI_LINK_TRAIN_PATTERN_2;
			I915_WRITE(fdi_rx_reg, temp);

			udelay(150);

			temp = I915_READ(fdi_rx_iir_reg);
			DRM_DEBUG("FDI_RX_IIR 0x%x\n", temp);

			if ((temp & FDI_RX_SYMBOL_LOCK) == 0) {
				for (j = 0; j < tries; j++) {
					temp = I915_READ(fdi_rx_iir_reg);
					DRM_DEBUG("FDI_RX_IIR 0x%x\n", temp);
					if (temp & FDI_RX_SYMBOL_LOCK)
						break;
					udelay(200);
				}
				if (j != tries) {
					I915_WRITE(fdi_rx_iir_reg,
							temp | FDI_RX_SYMBOL_LOCK);
					DRM_DEBUG("train 2 ok 1!\n");
				} else
					DRM_DEBUG("train 2 fail\n");
			} else {
				I915_WRITE(fdi_rx_iir_reg,
						temp | FDI_RX_SYMBOL_LOCK);
				DRM_DEBUG("train 2 ok 2!\n");
			}
			DRM_DEBUG("train done\n");

			/* set transcoder timing */
			I915_WRITE(trans_htot_reg, I915_READ(cpu_htot_reg));
			I915_WRITE(trans_hblank_reg, I915_READ(cpu_hblank_reg));
			I915_WRITE(trans_hsync_reg, I915_READ(cpu_hsync_reg));

			I915_WRITE(trans_vtot_reg, I915_READ(cpu_vtot_reg));
			I915_WRITE(trans_vblank_reg, I915_READ(cpu_vblank_reg));
			I915_WRITE(trans_vsync_reg, I915_READ(cpu_vsync_reg));

			/* enable PCH transcoder */
			temp = I915_READ(transconf_reg);
			/*
			 * make the BPC in transcoder be consistent with
			 * that in pipeconf reg.
			 */
			temp &= ~PIPE_BPC_MASK;
			temp |= pipe_bpc;
			I915_WRITE(transconf_reg, temp | TRANS_ENABLE);
			I915_READ(transconf_reg);

			while ((I915_READ(transconf_reg) & TRANS_STATE_ENABLE) == 0)
				;

			/* enable normal */

			temp = I915_READ(fdi_tx_reg);
			temp &= ~FDI_LINK_TRAIN_NONE;
			I915_WRITE(fdi_tx_reg, temp | FDI_LINK_TRAIN_NONE |
					FDI_TX_ENHANCE_FRAME_ENABLE);
			I915_READ(fdi_tx_reg);

			temp = I915_READ(fdi_rx_reg);
			temp &= ~FDI_LINK_TRAIN_NONE;
			I915_WRITE(fdi_rx_reg, temp | FDI_LINK_TRAIN_NONE |
					FDI_RX_ENHANCE_FRAME_ENABLE);
			I915_READ(fdi_rx_reg);

			/* wait one idle pattern time */
			udelay(100);

		}

		intel_crtc_load_lut(crtc);

	break;
	case DRM_MODE_DPMS_OFF:
		DRM_DEBUG("crtc %d dpms off\n", pipe);

		/* Disable display plane */
		temp = I915_READ(dspcntr_reg);
		if ((temp & DISPLAY_PLANE_ENABLE) != 0) {
			I915_WRITE(dspcntr_reg, temp & ~DISPLAY_PLANE_ENABLE);
			/* Flush the plane changes */
			I915_WRITE(dspbase_reg, I915_READ(dspbase_reg));
			I915_READ(dspbase_reg);
		}

		i915_disable_vga(dev);

		/* disable cpu pipe, disable after all planes disabled */
		temp = I915_READ(pipeconf_reg);
		if ((temp & PIPEACONF_ENABLE) != 0) {
			I915_WRITE(pipeconf_reg, temp & ~PIPEACONF_ENABLE);
			I915_READ(pipeconf_reg);
			n = 0;
			/* wait for cpu pipe off, pipe state */
			while ((I915_READ(pipeconf_reg) & I965_PIPECONF_ACTIVE) != 0) {
				n++;
				if (n < 60) {
					udelay(500);
					continue;
				} else {
					DRM_DEBUG("pipe %d off delay\n", pipe);
					break;
				}
			}
		} else
			DRM_DEBUG("crtc %d is disabled\n", pipe);

		udelay(100);

		/* Disable PF */
		temp = I915_READ(pf_ctl_reg);
		if ((temp & PF_ENABLE) != 0) {
			I915_WRITE(pf_ctl_reg, temp & ~PF_ENABLE);
			I915_READ(pf_ctl_reg);
		}
		I915_WRITE(pf_win_size, 0);

		/* disable CPU FDI tx and PCH FDI rx */
		temp = I915_READ(fdi_tx_reg);
		I915_WRITE(fdi_tx_reg, temp & ~FDI_TX_ENABLE);
		I915_READ(fdi_tx_reg);

		temp = I915_READ(fdi_rx_reg);
		/* BPC in FDI rx is consistent with that in pipeconf */
		temp &= ~(0x07 << 16);
		temp |= (pipe_bpc << 11);
		I915_WRITE(fdi_rx_reg, temp & ~FDI_RX_ENABLE);
		I915_READ(fdi_rx_reg);

		udelay(100);

		/* still set train pattern 1 */
		temp = I915_READ(fdi_tx_reg);
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_1;
		I915_WRITE(fdi_tx_reg, temp);

		temp = I915_READ(fdi_rx_reg);
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_1;
		I915_WRITE(fdi_rx_reg, temp);

		udelay(100);

		if (intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
			temp = I915_READ(PCH_LVDS);
			I915_WRITE(PCH_LVDS, temp & ~LVDS_PORT_EN);
			I915_READ(PCH_LVDS);
			udelay(100);
		}

		/* disable PCH transcoder */
		temp = I915_READ(transconf_reg);
		if ((temp & TRANS_ENABLE) != 0) {
			I915_WRITE(transconf_reg, temp & ~TRANS_ENABLE);
			I915_READ(transconf_reg);
			n = 0;
			/* wait for PCH transcoder off, transcoder state */
			while ((I915_READ(transconf_reg) & TRANS_STATE_ENABLE) != 0) {
				n++;
				if (n < 60) {
					udelay(500);
					continue;
				} else {
					DRM_DEBUG("transcoder %d off delay\n", pipe);
					break;
				}
			}
		}
		temp = I915_READ(transconf_reg);
		/* BPC in transcoder is consistent with that in pipeconf */
		temp &= ~PIPE_BPC_MASK;
		temp |= pipe_bpc;
		I915_WRITE(transconf_reg, temp);
		I915_READ(transconf_reg);
		udelay(100);

		/* disable PCH DPLL */
		temp = I915_READ(pch_dpll_reg);
		if ((temp & DPLL_VCO_ENABLE) != 0) {
			I915_WRITE(pch_dpll_reg, temp & ~DPLL_VCO_ENABLE);
			I915_READ(pch_dpll_reg);
		}

		if (HAS_eDP) {
			igdng_disable_pll_edp(crtc);
		}

		temp = I915_READ(fdi_rx_reg);
		temp &= ~FDI_SEL_PCDCLK;
		I915_WRITE(fdi_rx_reg, temp);
		I915_READ(fdi_rx_reg);

		temp = I915_READ(fdi_rx_reg);
		temp &= ~FDI_RX_PLL_ENABLE;
		I915_WRITE(fdi_rx_reg, temp);
		I915_READ(fdi_rx_reg);

		/* Disable CPU FDI TX PLL */
		temp = I915_READ(fdi_tx_reg);
		if ((temp & FDI_TX_PLL_ENABLE) != 0) {
			I915_WRITE(fdi_tx_reg, temp & ~FDI_TX_PLL_ENABLE);
			I915_READ(fdi_tx_reg);
			udelay(100);
		}

		/* Wait for the clocks to turn off. */
		udelay(100);
		break;
	}
}

static void i9xx_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int plane = intel_crtc->plane;
	int dpll_reg = (pipe == 0) ? DPLL_A : DPLL_B;
	int dspcntr_reg = (plane == 0) ? DSPACNTR : DSPBCNTR;
	int dspbase_reg = (plane == 0) ? DSPAADDR : DSPBADDR;
	int pipeconf_reg = (pipe == 0) ? PIPEACONF : PIPEBCONF;
	u32 temp;

	/* XXX: When our outputs are all unaware of DPMS modes other than off
	 * and on, we should map those modes to DRM_MODE_DPMS_OFF in the CRTC.
	 */
	switch (mode) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		intel_update_watermarks(dev);

		/* Enable the DPLL */
		temp = I915_READ(dpll_reg);
		if ((temp & DPLL_VCO_ENABLE) == 0) {
			I915_WRITE(dpll_reg, temp);
			I915_READ(dpll_reg);
			/* Wait for the clocks to stabilize. */
			udelay(150);
			I915_WRITE(dpll_reg, temp | DPLL_VCO_ENABLE);
			I915_READ(dpll_reg);
			/* Wait for the clocks to stabilize. */
			udelay(150);
			I915_WRITE(dpll_reg, temp | DPLL_VCO_ENABLE);
			I915_READ(dpll_reg);
			/* Wait for the clocks to stabilize. */
			udelay(150);
		}

		/* Enable the pipe */
		temp = I915_READ(pipeconf_reg);
		if ((temp & PIPEACONF_ENABLE) == 0)
			I915_WRITE(pipeconf_reg, temp | PIPEACONF_ENABLE);

		/* Enable the plane */
		temp = I915_READ(dspcntr_reg);
		if ((temp & DISPLAY_PLANE_ENABLE) == 0) {
			I915_WRITE(dspcntr_reg, temp | DISPLAY_PLANE_ENABLE);
			/* Flush the plane changes */
			I915_WRITE(dspbase_reg, I915_READ(dspbase_reg));
		}

		intel_crtc_load_lut(crtc);

		if ((IS_I965G(dev) || plane == 0))
			intel_update_fbc(crtc, &crtc->mode);

		/* Give the overlay scaler a chance to enable if it's on this pipe */
		//intel_crtc_dpms_video(crtc, true); TODO
	break;
	case DRM_MODE_DPMS_OFF:
		intel_update_watermarks(dev);
		/* Give the overlay scaler a chance to disable if it's on this pipe */
		//intel_crtc_dpms_video(crtc, FALSE); TODO
		drm_vblank_off(dev, pipe);

		if (dev_priv->cfb_plane == plane &&
		    dev_priv->display.disable_fbc)
			dev_priv->display.disable_fbc(dev);

		/* Disable the VGA plane that we never use */
		i915_disable_vga(dev);

		/* Disable display plane */
		temp = I915_READ(dspcntr_reg);
		if ((temp & DISPLAY_PLANE_ENABLE) != 0) {
			I915_WRITE(dspcntr_reg, temp & ~DISPLAY_PLANE_ENABLE);
			/* Flush the plane changes */
			I915_WRITE(dspbase_reg, I915_READ(dspbase_reg));
			I915_READ(dspbase_reg);
		}

		if (!IS_I9XX(dev)) {
			/* Wait for vblank for the disable to take effect */
			intel_wait_for_vblank(dev);
		}

		/* Next, disable display pipes */
		temp = I915_READ(pipeconf_reg);
		if ((temp & PIPEACONF_ENABLE) != 0) {
			I915_WRITE(pipeconf_reg, temp & ~PIPEACONF_ENABLE);
			I915_READ(pipeconf_reg);
		}

		/* Wait for vblank for the disable to take effect. */
		intel_wait_for_vblank(dev);

		temp = I915_READ(dpll_reg);
		if ((temp & DPLL_VCO_ENABLE) != 0) {
			I915_WRITE(dpll_reg, temp & ~DPLL_VCO_ENABLE);
			I915_READ(dpll_reg);
		}

		/* Wait for the clocks to turn off. */
		udelay(150);
		break;
	}
}

/**
 * Sets the power management mode of the pipe and plane.
 *
 * This code should probably grow support for turning the cursor off and back
 * on appropriately at the same time as we're turning the pipe off/on.
 */
static void intel_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	bool enabled;

	dev_priv->display.dpms(crtc, mode);

	intel_crtc->dpms_mode = mode;

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

static void intel_crtc_prepare (struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void intel_crtc_commit (struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);
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

static bool intel_crtc_mode_fixup(struct drm_crtc *crtc,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	if (IS_IGDNG(dev)) {
		/* FDI link clock is fixed at 2.7G */
		if (mode->clock * 3 > 27000 * 4)
			return MODE_CLOCK_HIGH;
	}
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

/**
 * Return the pipe currently connected to the panel fitter,
 * or -1 if the panel fitter is not present or not in use
 */
static int intel_panel_fitter_pipe (struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32  pfit_control;

	/* i830 doesn't have a panel fitter */
	if (IS_I830(dev))
		return -1;

	pfit_control = I915_READ(PFIT_CONTROL);

	/* See if the panel fitter is in use */
	if ((pfit_control & PFIT_ENABLE) == 0)
		return -1;

	/* 965 can place panel fitter on either pipe */
	if (IS_I965G(dev))
		return (pfit_control >> 29) & 0x3;

	/* older chips can only use pipe 1 */
	return 1;
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
igdng_compute_m_n(int bits_per_pixel, int nlanes,
		int pixel_clock, int link_clock,
		struct fdi_m_n *m_n)
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

/* IGD has different values for various configs */
static struct intel_watermark_params igd_display_wm = {
	IGD_DISPLAY_FIFO,
	IGD_MAX_WM,
	IGD_DFT_WM,
	IGD_GUARD_WM,
	IGD_FIFO_LINE_SIZE
};
static struct intel_watermark_params igd_display_hplloff_wm = {
	IGD_DISPLAY_FIFO,
	IGD_MAX_WM,
	IGD_DFT_HPLLOFF_WM,
	IGD_GUARD_WM,
	IGD_FIFO_LINE_SIZE
};
static struct intel_watermark_params igd_cursor_wm = {
	IGD_CURSOR_FIFO,
	IGD_CURSOR_MAX_WM,
	IGD_CURSOR_DFT_WM,
	IGD_CURSOR_GUARD_WM,
	IGD_FIFO_LINE_SIZE,
};
static struct intel_watermark_params igd_cursor_hplloff_wm = {
	IGD_CURSOR_FIFO,
	IGD_CURSOR_MAX_WM,
	IGD_CURSOR_DFT_WM,
	IGD_CURSOR_GUARD_WM,
	IGD_FIFO_LINE_SIZE
};
static struct intel_watermark_params g4x_wm_info = {
	G4X_FIFO_SIZE,
	G4X_MAX_WM,
	G4X_MAX_WM,
	2,
	G4X_FIFO_LINE_SIZE,
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
	entries_required /= wm->cacheline_size;

	DRM_DEBUG("FIFO entries required for mode: %d\n", entries_required);

	wm_size = wm->fifo_size - (entries_required + wm->guard_size);

	DRM_DEBUG("FIFO watermark level: %d\n", wm_size);

	/* Don't promote wm_size to unsigned... */
	if (wm_size > (long)wm->max_wm)
		wm_size = wm->max_wm;
	if (wm_size <= 0)
		wm_size = wm->default_wm;
	return wm_size;
}

struct cxsr_latency {
	int is_desktop;
	unsigned long fsb_freq;
	unsigned long mem_freq;
	unsigned long display_sr;
	unsigned long display_hpll_disable;
	unsigned long cursor_sr;
	unsigned long cursor_hpll_disable;
};

static struct cxsr_latency cxsr_latency_table[] = {
	{1, 800, 400, 3382, 33382, 3983, 33983},    /* DDR2-400 SC */
	{1, 800, 667, 3354, 33354, 3807, 33807},    /* DDR2-667 SC */
	{1, 800, 800, 3347, 33347, 3763, 33763},    /* DDR2-800 SC */

	{1, 667, 400, 3400, 33400, 4021, 34021},    /* DDR2-400 SC */
	{1, 667, 667, 3372, 33372, 3845, 33845},    /* DDR2-667 SC */
	{1, 667, 800, 3386, 33386, 3822, 33822},    /* DDR2-800 SC */

	{1, 400, 400, 3472, 33472, 4173, 34173},    /* DDR2-400 SC */
	{1, 400, 667, 3443, 33443, 3996, 33996},    /* DDR2-667 SC */
	{1, 400, 800, 3430, 33430, 3946, 33946},    /* DDR2-800 SC */

	{0, 800, 400, 3438, 33438, 4065, 34065},    /* DDR2-400 SC */
	{0, 800, 667, 3410, 33410, 3889, 33889},    /* DDR2-667 SC */
	{0, 800, 800, 3403, 33403, 3845, 33845},    /* DDR2-800 SC */

	{0, 667, 400, 3456, 33456, 4103, 34106},    /* DDR2-400 SC */
	{0, 667, 667, 3428, 33428, 3927, 33927},    /* DDR2-667 SC */
	{0, 667, 800, 3443, 33443, 3905, 33905},    /* DDR2-800 SC */

	{0, 400, 400, 3528, 33528, 4255, 34255},    /* DDR2-400 SC */
	{0, 400, 667, 3500, 33500, 4079, 34079},    /* DDR2-667 SC */
	{0, 400, 800, 3487, 33487, 4029, 34029},    /* DDR2-800 SC */
};

static struct cxsr_latency *intel_get_cxsr_latency(int is_desktop, int fsb,
						   int mem)
{
	int i;
	struct cxsr_latency *latency;

	if (fsb == 0 || mem == 0)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(cxsr_latency_table); i++) {
		latency = &cxsr_latency_table[i];
		if (is_desktop == latency->is_desktop &&
		    fsb == latency->fsb_freq && mem == latency->mem_freq)
			return latency;
	}

	DRM_DEBUG("Unknown FSB/MEM found, disable CxSR\n");

	return NULL;
}

static void igd_disable_cxsr(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg;

	/* deactivate cxsr */
	reg = I915_READ(DSPFW3);
	reg &= ~(IGD_SELF_REFRESH_EN);
	I915_WRITE(DSPFW3, reg);
	DRM_INFO("Big FIFO is disabled\n");
}

static void igd_enable_cxsr(struct drm_device *dev, unsigned long clock,
			    int pixel_size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg;
	unsigned long wm;
	struct cxsr_latency *latency;

	latency = intel_get_cxsr_latency(IS_IGDG(dev), dev_priv->fsb_freq,
		dev_priv->mem_freq);
	if (!latency) {
		DRM_DEBUG("Unknown FSB/MEM found, disable CxSR\n");
		igd_disable_cxsr(dev);
		return;
	}

	/* Display SR */
	wm = intel_calculate_wm(clock, &igd_display_wm, pixel_size,
				latency->display_sr);
	reg = I915_READ(DSPFW1);
	reg &= 0x7fffff;
	reg |= wm << 23;
	I915_WRITE(DSPFW1, reg);
	DRM_DEBUG("DSPFW1 register is %x\n", reg);

	/* cursor SR */
	wm = intel_calculate_wm(clock, &igd_cursor_wm, pixel_size,
				latency->cursor_sr);
	reg = I915_READ(DSPFW3);
	reg &= ~(0x3f << 24);
	reg |= (wm & 0x3f) << 24;
	I915_WRITE(DSPFW3, reg);

	/* Display HPLL off SR */
	wm = intel_calculate_wm(clock, &igd_display_hplloff_wm,
		latency->display_hpll_disable, I915_FIFO_LINE_SIZE);
	reg = I915_READ(DSPFW3);
	reg &= 0xfffffe00;
	reg |= wm & 0x1ff;
	I915_WRITE(DSPFW3, reg);

	/* cursor HPLL off SR */
	wm = intel_calculate_wm(clock, &igd_cursor_hplloff_wm, pixel_size,
				latency->cursor_hpll_disable);
	reg = I915_READ(DSPFW3);
	reg &= ~(0x3f << 16);
	reg |= (wm & 0x3f) << 16;
	I915_WRITE(DSPFW3, reg);
	DRM_DEBUG("DSPFW3 register is %x\n", reg);

	/* activate cxsr */
	reg = I915_READ(DSPFW3);
	reg |= IGD_SELF_REFRESH_EN;
	I915_WRITE(DSPFW3, reg);

	DRM_INFO("Big FIFO is enabled\n");

	return;
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
const static int latency_ns = 5000;

static int i9xx_get_fifo_size(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	if (plane == 0)
		size = dsparb & 0x7f;
	else
		size = ((dsparb >> DSPARB_CSTART_SHIFT) & 0x7f) -
			(dsparb & 0x7f);

	DRM_DEBUG("FIFO size - (0x%08x) %s: %d\n", dsparb, plane ? "B" : "A",
		  size);

	return size;
}

static int i85x_get_fifo_size(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	if (plane == 0)
		size = dsparb & 0x1ff;
	else
		size = ((dsparb >> DSPARB_BEND_SHIFT) & 0x1ff) -
			(dsparb & 0x1ff);
	size >>= 1; /* Convert to cachelines */

	DRM_DEBUG("FIFO size - (0x%08x) %s: %d\n", dsparb, plane ? "B" : "A",
		  size);

	return size;
}

static int i845_get_fifo_size(struct drm_device *dev, int plane)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	size = dsparb & 0x7f;
	size >>= 2; /* Convert to cachelines */

	DRM_DEBUG("FIFO size - (0x%08x) %s: %d\n", dsparb, plane ? "B" : "A",
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

	DRM_DEBUG("FIFO size - (0x%08x) %s: %d\n", dsparb, plane ? "B" : "A",
		  size);

	return size;
}

static void g4x_update_wm(struct drm_device *dev,  int planea_clock,
			  int planeb_clock, int sr_hdisplay, int pixel_size)
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
	entries_required /= G4X_FIFO_LINE_SIZE;
	planea_wm = entries_required + planea_params.guard_size;

	entries_required = ((planeb_clock / 1000) * pixel_size * latency_ns) /
		1000;
	entries_required /= G4X_FIFO_LINE_SIZE;
	planeb_wm = entries_required + planeb_params.guard_size;

	cursora_wm = cursorb_wm = 16;
	cursor_sr = 32;

	DRM_DEBUG("FIFO watermarks - A: %d, B: %d\n", planea_wm, planeb_wm);

	/* Calc sr entries for one plane configs */
	if (sr_hdisplay && (!planea_clock || !planeb_clock)) {
		/* self-refresh has much higher latency */
		const static int sr_latency_ns = 12000;

		sr_clock = planea_clock ? planea_clock : planeb_clock;
		line_time_us = ((sr_hdisplay * 1000) / sr_clock);

		/* Use ns/us then divide to preserve precision */
		sr_entries = (((sr_latency_ns / line_time_us) + 1) *
			      pixel_size * sr_hdisplay) / 1000;
		sr_entries = roundup(sr_entries / cacheline_size, 1);
		DRM_DEBUG("self-refresh entries: %d\n", sr_entries);
		I915_WRITE(FW_BLC_SELF, FW_BLC_SELF_EN);
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

static void i965_update_wm(struct drm_device *dev, int unused, int unused2,
			   int unused3, int unused4)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	DRM_DEBUG("Setting FIFO watermarks - A: 8, B: 8, C: 8, SR 8\n");

	/* 965 has limitations... */
	I915_WRITE(DSPFW1, (8 << 16) | (8 << 8) | (8 << 0));
	I915_WRITE(DSPFW2, (8 << 8) | (8 << 0));
}

static void i9xx_update_wm(struct drm_device *dev, int planea_clock,
			   int planeb_clock, int sr_hdisplay, int pixel_size)
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
	if (IS_I965GM(dev) || IS_I945GM(dev))
		planea_params = planeb_params = i945_wm_info;
	else if (IS_I9XX(dev))
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
	DRM_DEBUG("FIFO watermarks - A: %d, B: %d\n", planea_wm, planeb_wm);

	/*
	 * Overlay gets an aggressive default since video jitter is bad.
	 */
	cwm = 2;

	/* Calc sr entries for one plane configs */
	if (HAS_FW_BLC(dev) && sr_hdisplay &&
	    (!planea_clock || !planeb_clock)) {
		/* self-refresh has much higher latency */
		const static int sr_latency_ns = 6000;

		sr_clock = planea_clock ? planea_clock : planeb_clock;
		line_time_us = ((sr_hdisplay * 1000) / sr_clock);

		/* Use ns/us then divide to preserve precision */
		sr_entries = (((sr_latency_ns / line_time_us) + 1) *
			      pixel_size * sr_hdisplay) / 1000;
		sr_entries = roundup(sr_entries / cacheline_size, 1);
		DRM_DEBUG("self-refresh entries: %d\n", sr_entries);
		srwm = total_size - sr_entries;
		if (srwm < 0)
			srwm = 1;
		I915_WRITE(FW_BLC_SELF, FW_BLC_SELF_EN | (srwm & 0x3f));
	}

	DRM_DEBUG("Setting FIFO watermarks - A: %d, B: %d, C: %d, SR %d\n",
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
			   int unused2, int pixel_size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t fwater_lo = I915_READ(FW_BLC) & ~0xfff;
	int planea_wm;

	i830_wm_info.fifo_size = dev_priv->display.get_fifo_size(dev, 0);

	planea_wm = intel_calculate_wm(planea_clock, &i830_wm_info,
				       pixel_size, latency_ns);
	fwater_lo |= (3<<8) | planea_wm;

	DRM_DEBUG("Setting FIFO watermarks - A: %d\n", planea_wm);

	I915_WRITE(FW_BLC, fwater_lo);
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
	struct intel_crtc *intel_crtc;
	int sr_hdisplay = 0;
	unsigned long planea_clock = 0, planeb_clock = 0, sr_clock = 0;
	int enabled = 0, pixel_size = 0;

	if (!dev_priv->display.update_wm)
		return;

	/* Get the clock config from both planes */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		intel_crtc = to_intel_crtc(crtc);
		if (crtc->enabled) {
			enabled++;
			if (intel_crtc->plane == 0) {
				DRM_DEBUG("plane A (pipe %d) clock: %d\n",
					  intel_crtc->pipe, crtc->mode.clock);
				planea_clock = crtc->mode.clock;
			} else {
				DRM_DEBUG("plane B (pipe %d) clock: %d\n",
					  intel_crtc->pipe, crtc->mode.clock);
				planeb_clock = crtc->mode.clock;
			}
			sr_hdisplay = crtc->mode.hdisplay;
			sr_clock = crtc->mode.clock;
			if (crtc->fb)
				pixel_size = crtc->fb->bits_per_pixel / 8;
			else
				pixel_size = 4; /* by default */
		}
	}

	if (enabled <= 0)
		return;

	/* Single plane configs can enable self refresh */
	if (enabled == 1 && IS_IGD(dev))
		igd_enable_cxsr(dev, sr_clock, pixel_size);
	else if (IS_IGD(dev))
		igd_disable_cxsr(dev);

	dev_priv->display.update_wm(dev, planea_clock, planeb_clock,
				    sr_hdisplay, pixel_size);
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
	int fp_reg = (pipe == 0) ? FPA0 : FPB0;
	int dpll_reg = (pipe == 0) ? DPLL_A : DPLL_B;
	int dpll_md_reg = (intel_crtc->pipe == 0) ? DPLL_A_MD : DPLL_B_MD;
	int dspcntr_reg = (plane == 0) ? DSPACNTR : DSPBCNTR;
	int pipeconf_reg = (pipe == 0) ? PIPEACONF : PIPEBCONF;
	int htot_reg = (pipe == 0) ? HTOTAL_A : HTOTAL_B;
	int hblank_reg = (pipe == 0) ? HBLANK_A : HBLANK_B;
	int hsync_reg = (pipe == 0) ? HSYNC_A : HSYNC_B;
	int vtot_reg = (pipe == 0) ? VTOTAL_A : VTOTAL_B;
	int vblank_reg = (pipe == 0) ? VBLANK_A : VBLANK_B;
	int vsync_reg = (pipe == 0) ? VSYNC_A : VSYNC_B;
	int dspsize_reg = (plane == 0) ? DSPASIZE : DSPBSIZE;
	int dsppos_reg = (plane == 0) ? DSPAPOS : DSPBPOS;
	int pipesrc_reg = (pipe == 0) ? PIPEASRC : PIPEBSRC;
	int refclk, num_outputs = 0;
	intel_clock_t clock, reduced_clock;
	u32 dpll = 0, fp = 0, fp2 = 0, dspcntr, pipeconf;
	bool ok, has_reduced_clock = false, is_sdvo = false, is_dvo = false;
	bool is_crt = false, is_lvds = false, is_tv = false, is_dp = false;
	bool is_edp = false;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;
	const intel_limit_t *limit;
	int ret;
	struct fdi_m_n m_n = {0};
	int data_m1_reg = (pipe == 0) ? PIPEA_DATA_M1 : PIPEB_DATA_M1;
	int data_n1_reg = (pipe == 0) ? PIPEA_DATA_N1 : PIPEB_DATA_N1;
	int link_m1_reg = (pipe == 0) ? PIPEA_LINK_M1 : PIPEB_LINK_M1;
	int link_n1_reg = (pipe == 0) ? PIPEA_LINK_N1 : PIPEB_LINK_N1;
	int pch_fp_reg = (pipe == 0) ? PCH_FPA0 : PCH_FPB0;
	int pch_dpll_reg = (pipe == 0) ? PCH_DPLL_A : PCH_DPLL_B;
	int fdi_rx_reg = (pipe == 0) ? FDI_RXA_CTL : FDI_RXB_CTL;
	int lvds_reg = LVDS;
	u32 temp;
	int sdvo_pixel_multiply;
	int target_clock;

	drm_vblank_pre_modeset(dev, pipe);

	list_for_each_entry(connector, &mode_config->connector_list, head) {
		struct intel_output *intel_output = to_intel_output(connector);

		if (!connector->encoder || connector->encoder->crtc != crtc)
			continue;

		switch (intel_output->type) {
		case INTEL_OUTPUT_LVDS:
			is_lvds = true;
			break;
		case INTEL_OUTPUT_SDVO:
		case INTEL_OUTPUT_HDMI:
			is_sdvo = true;
			if (intel_output->needs_tv_clock)
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
			is_edp = true;
			break;
		}

		num_outputs++;
	}

	if (is_lvds && dev_priv->lvds_use_ssc && num_outputs < 2) {
		refclk = dev_priv->lvds_ssc_freq * 1000;
		DRM_DEBUG("using SSC reference clock of %d MHz\n", refclk / 1000);
	} else if (IS_I9XX(dev)) {
		refclk = 96000;
		if (IS_IGDNG(dev))
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

	if (limit->find_reduced_pll && dev_priv->lvds_downclock_avail) {
		memcpy(&reduced_clock, &clock, sizeof(intel_clock_t));
		has_reduced_clock = limit->find_reduced_pll(limit, crtc,
							    (adjusted_mode->clock*3/4),
							    refclk,
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
	if (IS_IGDNG(dev)) {
		int lane, link_bw, bpp;
		/* eDP doesn't require FDI link, so just set DP M/N
		   according to current link config */
		if (is_edp) {
			struct drm_connector *edp;
			target_clock = mode->clock;
			edp = intel_pipe_get_output(crtc);
			intel_edp_link_config(to_intel_output(edp),
					&lane, &link_bw);
		} else {
			/* DP over FDI requires target mode clock
			   instead of link clock */
			if (is_dp)
				target_clock = mode->clock;
			else
				target_clock = adjusted_mode->clock;
			lane = 4;
			link_bw = 270000;
		}

		/* determine panel color depth */
		temp = I915_READ(pipeconf_reg);
		temp &= ~PIPE_BPC_MASK;
		if (is_lvds) {
			int lvds_reg = I915_READ(PCH_LVDS);
			/* the BPC will be 6 if it is 18-bit LVDS panel */
			if ((lvds_reg & LVDS_A3_POWER_MASK) == LVDS_A3_POWER_UP)
				temp |= PIPE_8BPC;
			else
				temp |= PIPE_6BPC;
		} else
			temp |= PIPE_8BPC;
		I915_WRITE(pipeconf_reg, temp);
		I915_READ(pipeconf_reg);

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

		igdng_compute_m_n(bpp, lane, target_clock,
				  link_bw, &m_n);
	}

	/* Ironlake: try to setup display ref clock before DPLL
	 * enabling. This is only under driver's control after
	 * PCH B stepping, previous chipset stepping should be
	 * ignoring this setting.
	 */
	if (IS_IGDNG(dev)) {
		temp = I915_READ(PCH_DREF_CONTROL);
		/* Always enable nonspread source */
		temp &= ~DREF_NONSPREAD_SOURCE_MASK;
		temp |= DREF_NONSPREAD_SOURCE_ENABLE;
		I915_WRITE(PCH_DREF_CONTROL, temp);
		POSTING_READ(PCH_DREF_CONTROL);

		temp &= ~DREF_SSC_SOURCE_MASK;
		temp |= DREF_SSC_SOURCE_ENABLE;
		I915_WRITE(PCH_DREF_CONTROL, temp);
		POSTING_READ(PCH_DREF_CONTROL);

		udelay(200);

		if (is_edp) {
			if (dev_priv->lvds_use_ssc) {
				temp |= DREF_SSC1_ENABLE;
				I915_WRITE(PCH_DREF_CONTROL, temp);
				POSTING_READ(PCH_DREF_CONTROL);

				udelay(200);

				temp &= ~DREF_CPU_SOURCE_OUTPUT_MASK;
				temp |= DREF_CPU_SOURCE_OUTPUT_DOWNSPREAD;
				I915_WRITE(PCH_DREF_CONTROL, temp);
				POSTING_READ(PCH_DREF_CONTROL);
			} else {
				temp |= DREF_CPU_SOURCE_OUTPUT_NONSPREAD;
				I915_WRITE(PCH_DREF_CONTROL, temp);
				POSTING_READ(PCH_DREF_CONTROL);
			}
		}
	}

	if (IS_IGD(dev)) {
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

	if (!IS_IGDNG(dev))
		dpll = DPLL_VGA_MODE_DIS;

	if (IS_I9XX(dev)) {
		if (is_lvds)
			dpll |= DPLLB_MODE_LVDS;
		else
			dpll |= DPLLB_MODE_DAC_SERIAL;
		if (is_sdvo) {
			dpll |= DPLL_DVO_HIGH_SPEED;
			sdvo_pixel_multiply = adjusted_mode->clock / mode->clock;
			if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
				dpll |= (sdvo_pixel_multiply - 1) << SDVO_MULTIPLIER_SHIFT_HIRES;
			else if (IS_IGDNG(dev))
				dpll |= (sdvo_pixel_multiply - 1) << PLL_REF_SDVO_HDMI_MULTIPLIER_SHIFT;
		}
		if (is_dp)
			dpll |= DPLL_DVO_HIGH_SPEED;

		/* compute bitmask from p1 value */
		if (IS_IGD(dev))
			dpll |= (1 << (clock.p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT_IGD;
		else {
			dpll |= (1 << (clock.p1 - 1)) << DPLL_FPA01_P1_POST_DIV_SHIFT;
			/* also FPA1 */
			if (IS_IGDNG(dev))
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
		if (IS_I965G(dev) && !IS_IGDNG(dev))
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
	else if (is_lvds && dev_priv->lvds_use_ssc && num_outputs < 2)
		dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
		dpll |= PLL_REF_INPUT_DREFCLK;

	/* setup pipeconf */
	pipeconf = I915_READ(pipeconf_reg);

	/* Set up the display plane register */
	dspcntr = DISPPLANE_GAMMA_ENABLE;

	/* IGDNG's plane is forced to pipe, bit 24 is to
	   enable color space conversion */
	if (!IS_IGDNG(dev)) {
		if (pipe == 0)
			dspcntr &= ~DISPPLANE_SEL_PIPE_MASK;
		else
			dspcntr |= DISPPLANE_SEL_PIPE_B;
	}

	if (pipe == 0 && !IS_I965G(dev)) {
		/* Enable pixel doubling when the dot clock is > 90% of the (display)
		 * core speed.
		 *
		 * XXX: No double-wide on 915GM pipe B. Is that the only reason for the
		 * pipe == 0 check?
		 */
		if (mode->clock >
		    dev_priv->display.get_display_clock_speed(dev) * 9 / 10)
			pipeconf |= PIPEACONF_DOUBLE_WIDE;
		else
			pipeconf &= ~PIPEACONF_DOUBLE_WIDE;
	}

	dspcntr |= DISPLAY_PLANE_ENABLE;
	pipeconf |= PIPEACONF_ENABLE;
	dpll |= DPLL_VCO_ENABLE;


	/* Disable the panel fitter if it was on our pipe */
	if (!IS_IGDNG(dev) && intel_panel_fitter_pipe(dev) == pipe)
		I915_WRITE(PFIT_CONTROL, 0);

	DRM_DEBUG("Mode for pipe %c:\n", pipe == 0 ? 'A' : 'B');
	drm_mode_debug_printmodeline(mode);

	/* assign to IGDNG registers */
	if (IS_IGDNG(dev)) {
		fp_reg = pch_fp_reg;
		dpll_reg = pch_dpll_reg;
	}

	if (is_edp) {
		igdng_disable_pll_edp(crtc);
	} else if ((dpll & DPLL_VCO_ENABLE)) {
		I915_WRITE(fp_reg, fp);
		I915_WRITE(dpll_reg, dpll & ~DPLL_VCO_ENABLE);
		I915_READ(dpll_reg);
		udelay(150);
	}

	/* The LVDS pin pair needs to be on before the DPLLs are enabled.
	 * This is an exception to the general rule that mode_set doesn't turn
	 * things on.
	 */
	if (is_lvds) {
		u32 lvds;

		if (IS_IGDNG(dev))
			lvds_reg = PCH_LVDS;

		lvds = I915_READ(lvds_reg);
		lvds |= LVDS_PORT_EN | LVDS_A0A2_CLKA_POWER_UP | LVDS_PIPEB_SELECT;
		/* set the corresponsding LVDS_BORDER bit */
		lvds |= dev_priv->lvds_border_bits;
		/* Set the B0-B3 data pairs corresponding to whether we're going to
		 * set the DPLLs for dual-channel mode or not.
		 */
		if (clock.p2 == 7)
			lvds |= LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP;
		else
			lvds &= ~(LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP);

		/* It would be nice to set 24 vs 18-bit mode (LVDS_A3_POWER_UP)
		 * appropriately here, but we need to look more thoroughly into how
		 * panels behave in the two modes.
		 */
		/* set the dithering flag */
		if (IS_I965G(dev)) {
			if (dev_priv->lvds_dither) {
				if (IS_IGDNG(dev))
					pipeconf |= PIPE_ENABLE_DITHER;
				else
					lvds |= LVDS_ENABLE_DITHER;
			} else {
				if (IS_IGDNG(dev))
					pipeconf &= ~PIPE_ENABLE_DITHER;
				else
					lvds &= ~LVDS_ENABLE_DITHER;
			}
		}
		I915_WRITE(lvds_reg, lvds);
		I915_READ(lvds_reg);
	}
	if (is_dp)
		intel_dp_set_m_n(crtc, mode, adjusted_mode);

	if (!is_edp) {
		I915_WRITE(fp_reg, fp);
		I915_WRITE(dpll_reg, dpll);
		I915_READ(dpll_reg);
		/* Wait for the clocks to stabilize. */
		udelay(150);

		if (IS_I965G(dev) && !IS_IGDNG(dev)) {
			if (is_sdvo) {
				sdvo_pixel_multiply = adjusted_mode->clock / mode->clock;
				I915_WRITE(dpll_md_reg, (0 << DPLL_MD_UDI_DIVIDER_SHIFT) |
					((sdvo_pixel_multiply - 1) << DPLL_MD_UDI_MULTIPLIER_SHIFT));
			} else
				I915_WRITE(dpll_md_reg, 0);
		} else {
			/* write it again -- the BIOS does, after all */
			I915_WRITE(dpll_reg, dpll);
		}
		I915_READ(dpll_reg);
		/* Wait for the clocks to stabilize. */
		udelay(150);
	}

	if (is_lvds && has_reduced_clock && i915_powersave) {
		I915_WRITE(fp_reg + 4, fp2);
		intel_crtc->lowfreq_avail = true;
		if (HAS_PIPE_CXSR(dev)) {
			DRM_DEBUG("enabling CxSR downclocking\n");
			pipeconf |= PIPECONF_CXSR_DOWNCLOCK;
		}
	} else {
		I915_WRITE(fp_reg + 4, fp);
		intel_crtc->lowfreq_avail = false;
		if (HAS_PIPE_CXSR(dev)) {
			DRM_DEBUG("disabling CxSR downclocking\n");
			pipeconf &= ~PIPECONF_CXSR_DOWNCLOCK;
		}
	}

	I915_WRITE(htot_reg, (adjusted_mode->crtc_hdisplay - 1) |
		   ((adjusted_mode->crtc_htotal - 1) << 16));
	I915_WRITE(hblank_reg, (adjusted_mode->crtc_hblank_start - 1) |
		   ((adjusted_mode->crtc_hblank_end - 1) << 16));
	I915_WRITE(hsync_reg, (adjusted_mode->crtc_hsync_start - 1) |
		   ((adjusted_mode->crtc_hsync_end - 1) << 16));
	I915_WRITE(vtot_reg, (adjusted_mode->crtc_vdisplay - 1) |
		   ((adjusted_mode->crtc_vtotal - 1) << 16));
	I915_WRITE(vblank_reg, (adjusted_mode->crtc_vblank_start - 1) |
		   ((adjusted_mode->crtc_vblank_end - 1) << 16));
	I915_WRITE(vsync_reg, (adjusted_mode->crtc_vsync_start - 1) |
		   ((adjusted_mode->crtc_vsync_end - 1) << 16));
	/* pipesrc and dspsize control the size that is scaled from, which should
	 * always be the user's requested size.
	 */
	if (!IS_IGDNG(dev)) {
		I915_WRITE(dspsize_reg, ((mode->vdisplay - 1) << 16) |
				(mode->hdisplay - 1));
		I915_WRITE(dsppos_reg, 0);
	}
	I915_WRITE(pipesrc_reg, ((mode->hdisplay - 1) << 16) | (mode->vdisplay - 1));

	if (IS_IGDNG(dev)) {
		I915_WRITE(data_m1_reg, TU_SIZE(m_n.tu) | m_n.gmch_m);
		I915_WRITE(data_n1_reg, TU_SIZE(m_n.tu) | m_n.gmch_n);
		I915_WRITE(link_m1_reg, m_n.link_m);
		I915_WRITE(link_n1_reg, m_n.link_n);

		if (is_edp) {
			igdng_set_pll_edp(crtc, adjusted_mode->clock);
		} else {
			/* enable FDI RX PLL too */
			temp = I915_READ(fdi_rx_reg);
			I915_WRITE(fdi_rx_reg, temp | FDI_RX_PLL_ENABLE);
			udelay(200);
		}
	}

	I915_WRITE(pipeconf_reg, pipeconf);
	I915_READ(pipeconf_reg);

	intel_wait_for_vblank(dev);

	if (IS_IGDNG(dev)) {
		/* enable address swizzle for tiling buffer */
		temp = I915_READ(DISP_ARB_CTL);
		I915_WRITE(DISP_ARB_CTL, temp | DISP_TILE_SURFACE_SWIZZLING);
	}

	I915_WRITE(dspcntr_reg, dspcntr);

	/* Flush the plane changes */
	ret = intel_pipe_set_base(crtc, x, y, old_fb);

	if ((IS_I965G(dev) || plane == 0))
		intel_update_fbc(crtc, &crtc->mode);

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

	/* use legacy palette for IGDNG */
	if (IS_IGDNG(dev))
		palreg = (intel_crtc->pipe == 0) ? LGC_PALETTE_A :
						   LGC_PALETTE_B;

	for (i = 0; i < 256; i++) {
		I915_WRITE(palreg + 4 * i,
			   (intel_crtc->lut_r[i] << 16) |
			   (intel_crtc->lut_g[i] << 8) |
			   intel_crtc->lut_b[i]);
	}
}

static int intel_crtc_cursor_set(struct drm_crtc *crtc,
				 struct drm_file *file_priv,
				 uint32_t handle,
				 uint32_t width, uint32_t height)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_gem_object *bo;
	struct drm_i915_gem_object *obj_priv;
	int pipe = intel_crtc->pipe;
	uint32_t control = (pipe == 0) ? CURACNTR : CURBCNTR;
	uint32_t base = (pipe == 0) ? CURABASE : CURBBASE;
	uint32_t temp = I915_READ(control);
	size_t addr;
	int ret;

	DRM_DEBUG("\n");

	/* if we want to turn off the cursor ignore width and height */
	if (!handle) {
		DRM_DEBUG("cursor off\n");
		if (IS_MOBILE(dev) || IS_I9XX(dev)) {
			temp &= ~(CURSOR_MODE | MCURSOR_GAMMA_ENABLE);
			temp |= CURSOR_MODE_DISABLE;
		} else {
			temp &= ~(CURSOR_ENABLE | CURSOR_GAMMA_ENABLE);
		}
		addr = 0;
		bo = NULL;
		mutex_lock(&dev->struct_mutex);
		goto finish;
	}

	/* Currently we only support 64x64 cursors */
	if (width != 64 || height != 64) {
		DRM_ERROR("we currently only support 64x64 cursors\n");
		return -EINVAL;
	}

	bo = drm_gem_object_lookup(dev, file_priv, handle);
	if (!bo)
		return -ENOENT;

	obj_priv = bo->driver_private;

	if (bo->size < width * height * 4) {
		DRM_ERROR("buffer is to small\n");
		ret = -ENOMEM;
		goto fail;
	}

	/* we only need to pin inside GTT if cursor is non-phy */
	mutex_lock(&dev->struct_mutex);
	if (!dev_priv->cursor_needs_physical) {
		ret = i915_gem_object_pin(bo, PAGE_SIZE);
		if (ret) {
			DRM_ERROR("failed to pin cursor bo\n");
			goto fail_locked;
		}
		addr = obj_priv->gtt_offset;
	} else {
		ret = i915_gem_attach_phys_object(dev, bo, (pipe == 0) ? I915_GEM_PHYS_CURSOR_0 : I915_GEM_PHYS_CURSOR_1);
		if (ret) {
			DRM_ERROR("failed to attach phys object\n");
			goto fail_locked;
		}
		addr = obj_priv->phys_obj->handle->busaddr;
	}

	if (!IS_I9XX(dev))
		I915_WRITE(CURSIZE, (height << 12) | width);

	/* Hooray for CUR*CNTR differences */
	if (IS_MOBILE(dev) || IS_I9XX(dev)) {
		temp &= ~(CURSOR_MODE | MCURSOR_PIPE_SELECT);
		temp |= CURSOR_MODE_64_ARGB_AX | MCURSOR_GAMMA_ENABLE;
		temp |= (pipe << 28); /* Connect to correct pipe */
	} else {
		temp &= ~(CURSOR_FORMAT_MASK);
		temp |= CURSOR_ENABLE;
		temp |= CURSOR_FORMAT_ARGB | CURSOR_GAMMA_ENABLE;
	}

 finish:
	I915_WRITE(control, temp);
	I915_WRITE(base, addr);

	if (intel_crtc->cursor_bo) {
		if (dev_priv->cursor_needs_physical) {
			if (intel_crtc->cursor_bo != bo)
				i915_gem_detach_phys_object(dev, intel_crtc->cursor_bo);
		} else
			i915_gem_object_unpin(intel_crtc->cursor_bo);
		drm_gem_object_unreference(intel_crtc->cursor_bo);
	}

	mutex_unlock(&dev->struct_mutex);

	intel_crtc->cursor_addr = addr;
	intel_crtc->cursor_bo = bo;

	return 0;
fail:
	mutex_lock(&dev->struct_mutex);
fail_locked:
	drm_gem_object_unreference(bo);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static int intel_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_framebuffer *intel_fb;
	int pipe = intel_crtc->pipe;
	uint32_t temp = 0;
	uint32_t adder;

	if (crtc->fb) {
		intel_fb = to_intel_framebuffer(crtc->fb);
		intel_mark_busy(dev, intel_fb->obj);
	}

	if (x < 0) {
		temp |= CURSOR_POS_SIGN << CURSOR_X_SHIFT;
		x = -x;
	}
	if (y < 0) {
		temp |= CURSOR_POS_SIGN << CURSOR_Y_SHIFT;
		y = -y;
	}

	temp |= x << CURSOR_X_SHIFT;
	temp |= y << CURSOR_Y_SHIFT;

	adder = intel_crtc->cursor_addr;
	I915_WRITE((pipe == 0) ? CURAPOS : CURBPOS, temp);
	I915_WRITE((pipe == 0) ? CURABASE : CURBBASE, adder);

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
				 u16 *blue, uint32_t size)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int i;

	if (size != 256)
		return;

	for (i = 0; i < 256; i++) {
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
 * its requirements.  The pipe will be connected to no other outputs.
 *
 * Currently this code will only succeed if there is a pipe with no outputs
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

struct drm_crtc *intel_get_load_detect_pipe(struct intel_output *intel_output,
					    struct drm_display_mode *mode,
					    int *dpms_mode)
{
	struct intel_crtc *intel_crtc;
	struct drm_crtc *possible_crtc;
	struct drm_crtc *supported_crtc =NULL;
	struct drm_encoder *encoder = &intel_output->enc;
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
	intel_output->base.encoder = encoder;
	intel_output->load_detect_temp = true;

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
	intel_wait_for_vblank(dev);

	return crtc;
}

void intel_release_load_detect_pipe(struct intel_output *intel_output, int dpms_mode)
{
	struct drm_encoder *encoder = &intel_output->enc;
	struct drm_device *dev = encoder->dev;
	struct drm_crtc *crtc = encoder->crtc;
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;

	if (intel_output->load_detect_temp) {
		encoder->crtc = NULL;
		intel_output->base.encoder = NULL;
		intel_output->load_detect_temp = false;
		crtc->enabled = drm_helper_crtc_in_use(crtc);
		drm_helper_disable_unused_functions(dev);
	}

	/* Switch crtc and output back off if necessary */
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
	if (IS_IGD(dev)) {
		clock.n = ffs((fp & FP_N_IGD_DIV_MASK) >> FP_N_DIV_SHIFT) - 1;
		clock.m2 = (fp & FP_M2_IGD_DIV_MASK) >> FP_M2_DIV_SHIFT;
	} else {
		clock.n = (fp & FP_N_DIV_MASK) >> FP_N_DIV_SHIFT;
		clock.m2 = (fp & FP_M2_DIV_MASK) >> FP_M2_DIV_SHIFT;
	}

	if (IS_I9XX(dev)) {
		if (IS_IGD(dev))
			clock.p1 = ffs((dpll & DPLL_FPA01_P1_POST_DIV_MASK_IGD) >>
				DPLL_FPA01_P1_POST_DIV_SHIFT_IGD);
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
			DRM_DEBUG("Unknown DPLL mode %08x in programmed "
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

	DRM_DEBUG("idle timer fired, downclocking\n");

	dev_priv->busy = false;

	queue_work(dev_priv->wq, &dev_priv->idle_work);
}

#define CRTC_IDLE_TIMEOUT 1000 /* ms */

static void intel_crtc_idle_timer(unsigned long arg)
{
	struct intel_crtc *intel_crtc = (struct intel_crtc *)arg;
	struct drm_crtc *crtc = &intel_crtc->base;
	drm_i915_private_t *dev_priv = crtc->dev->dev_private;

	DRM_DEBUG("idle timer fired, downclocking\n");

	intel_crtc->busy = false;

	queue_work(dev_priv->wq, &dev_priv->idle_work);
}

static void intel_increase_pllclock(struct drm_crtc *crtc, bool schedule)
{
	struct drm_device *dev = crtc->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_crtc->pipe;
	int dpll_reg = (pipe == 0) ? DPLL_A : DPLL_B;
	int dpll = I915_READ(dpll_reg);

	if (IS_IGDNG(dev))
		return;

	if (!dev_priv->lvds_downclock_avail)
		return;

	if (!HAS_PIPE_CXSR(dev) && (dpll & DISPLAY_RATE_SELECT_FPA1)) {
		DRM_DEBUG("upclocking LVDS\n");

		/* Unlock panel regs */
		I915_WRITE(PP_CONTROL, I915_READ(PP_CONTROL) | (0xabcd << 16));

		dpll &= ~DISPLAY_RATE_SELECT_FPA1;
		I915_WRITE(dpll_reg, dpll);
		dpll = I915_READ(dpll_reg);
		intel_wait_for_vblank(dev);
		dpll = I915_READ(dpll_reg);
		if (dpll & DISPLAY_RATE_SELECT_FPA1)
			DRM_DEBUG("failed to upclock LVDS!\n");

		/* ...and lock them again */
		I915_WRITE(PP_CONTROL, I915_READ(PP_CONTROL) & 0x3);
	}

	/* Schedule downclock */
	if (schedule)
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

	if (IS_IGDNG(dev))
		return;

	if (!dev_priv->lvds_downclock_avail)
		return;

	/*
	 * Since this is called by a timer, we should never get here in
	 * the manual case.
	 */
	if (!HAS_PIPE_CXSR(dev) && intel_crtc->lowfreq_avail) {
		DRM_DEBUG("downclocking LVDS\n");

		/* Unlock panel regs */
		I915_WRITE(PP_CONTROL, I915_READ(PP_CONTROL) | (0xabcd << 16));

		dpll |= DISPLAY_RATE_SELECT_FPA1;
		I915_WRITE(dpll_reg, dpll);
		dpll = I915_READ(dpll_reg);
		intel_wait_for_vblank(dev);
		dpll = I915_READ(dpll_reg);
		if (!(dpll & DISPLAY_RATE_SELECT_FPA1))
			DRM_DEBUG("failed to downclock LVDS!\n");

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

	if (!i915_powersave)
		return;

	mutex_lock(&dev->struct_mutex);

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
void intel_mark_busy(struct drm_device *dev, struct drm_gem_object *obj)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = NULL;
	struct intel_framebuffer *intel_fb;
	struct intel_crtc *intel_crtc;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	dev_priv->busy = true;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (!crtc->fb)
			continue;

		intel_crtc = to_intel_crtc(crtc);
		intel_fb = to_intel_framebuffer(crtc->fb);
		if (intel_fb->obj == obj) {
			if (!intel_crtc->busy) {
				/* Non-busy -> busy, upclock */
				intel_increase_pllclock(crtc, true);
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

	drm_crtc_cleanup(crtc);
	kfree(intel_crtc);
}

static const struct drm_crtc_helper_funcs intel_helper_funcs = {
	.dpms = intel_crtc_dpms,
	.mode_fixup = intel_crtc_mode_fixup,
	.mode_set = intel_crtc_mode_set,
	.mode_set_base = intel_pipe_set_base,
	.prepare = intel_crtc_prepare,
	.commit = intel_crtc_commit,
	.load_lut = intel_crtc_load_lut,
};

static const struct drm_crtc_funcs intel_crtc_funcs = {
	.cursor_set = intel_crtc_cursor_set,
	.cursor_move = intel_crtc_cursor_move,
	.gamma_set = intel_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = intel_crtc_destroy,
};


static void intel_crtc_init(struct drm_device *dev, int pipe)
{
	struct intel_crtc *intel_crtc;
	int i;

	intel_crtc = kzalloc(sizeof(struct intel_crtc) + (INTELFB_CONN_LIMIT * sizeof(struct drm_connector *)), GFP_KERNEL);
	if (intel_crtc == NULL)
		return;

	drm_crtc_init(dev, &intel_crtc->base, &intel_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&intel_crtc->base, 256);
	intel_crtc->pipe = pipe;
	intel_crtc->plane = pipe;
	for (i = 0; i < 256; i++) {
		intel_crtc->lut_r[i] = i;
		intel_crtc->lut_g[i] = i;
		intel_crtc->lut_b[i] = i;
	}

	/* Swap pipes & planes for FBC on pre-965 */
	intel_crtc->pipe = pipe;
	intel_crtc->plane = pipe;
	if (IS_MOBILE(dev) && (IS_I9XX(dev) && !IS_I965G(dev))) {
		DRM_DEBUG("swapping pipes & planes for FBC\n");
		intel_crtc->plane = ((pipe == 0) ? 1 : 0);
	}

	intel_crtc->cursor_addr = 0;
	intel_crtc->dpms_mode = DRM_MODE_DPMS_OFF;
	drm_crtc_helper_add(&intel_crtc->base, &intel_helper_funcs);

	intel_crtc->busy = false;

	setup_timer(&intel_crtc->idle_timer, intel_crtc_idle_timer,
		    (unsigned long)intel_crtc);
}

int intel_get_pipe_from_crtc_id(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
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

struct drm_crtc *intel_get_crtc_from_pipe(struct drm_device *dev, int pipe)
{
	struct drm_crtc *crtc = NULL;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
		if (intel_crtc->pipe == pipe)
			break;
	}
	return crtc;
}

static int intel_connector_clones(struct drm_device *dev, int type_mask)
{
	int index_mask = 0;
	struct drm_connector *connector;
	int entry = 0;

        list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct intel_output *intel_output = to_intel_output(connector);
		if (type_mask & intel_output->clone_mask)
			index_mask |= (1 << entry);
		entry++;
	}
	return index_mask;
}


static void intel_setup_outputs(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_connector *connector;

	intel_crt_init(dev);

	/* Set up integrated LVDS */
	if (IS_MOBILE(dev) && !IS_I830(dev))
		intel_lvds_init(dev);

	if (IS_IGDNG(dev)) {
		int found;

		if (IS_MOBILE(dev) && (I915_READ(DP_A) & DP_DETECTED))
			intel_dp_init(dev, DP_A);

		if (I915_READ(HDMIB) & PORT_DETECTED) {
			/* check SDVOB */
			/* found = intel_sdvo_init(dev, HDMIB); */
			found = 0;
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

		if (I915_READ(PCH_DP_D) & DP_DETECTED)
			intel_dp_init(dev, PCH_DP_D);

	} else if (SUPPORTS_DIGITAL_OUTPUTS(dev)) {
		bool found = false;

		if (I915_READ(SDVOB) & SDVO_DETECTED) {
			found = intel_sdvo_init(dev, SDVOB);
			if (!found && SUPPORTS_INTEGRATED_HDMI(dev))
				intel_hdmi_init(dev, SDVOB);

			if (!found && SUPPORTS_INTEGRATED_DP(dev))
				intel_dp_init(dev, DP_B);
		}

		/* Before G4X SDVOC doesn't have its own detect register */

		if (I915_READ(SDVOB) & SDVO_DETECTED)
			found = intel_sdvo_init(dev, SDVOC);

		if (!found && (I915_READ(SDVOC) & SDVO_DETECTED)) {

			if (SUPPORTS_INTEGRATED_HDMI(dev))
				intel_hdmi_init(dev, SDVOC);
			if (SUPPORTS_INTEGRATED_DP(dev))
				intel_dp_init(dev, DP_C);
		}

		if (SUPPORTS_INTEGRATED_DP(dev) && (I915_READ(DP_D) & DP_DETECTED))
			intel_dp_init(dev, DP_D);
	} else if (IS_I8XX(dev))
		intel_dvo_init(dev);

	if (SUPPORTS_TV(dev))
		intel_tv_init(dev);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct intel_output *intel_output = to_intel_output(connector);
		struct drm_encoder *encoder = &intel_output->enc;

		encoder->possible_crtcs = intel_output->crtc_mask;
		encoder->possible_clones = intel_connector_clones(dev,
						intel_output->clone_mask);
	}
}

static void intel_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_device *dev = fb->dev;

	if (fb->fbdev)
		intelfb_remove(dev, fb);

	drm_framebuffer_cleanup(fb);
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(intel_fb->obj);
	mutex_unlock(&dev->struct_mutex);

	kfree(intel_fb);
}

static int intel_user_framebuffer_create_handle(struct drm_framebuffer *fb,
						struct drm_file *file_priv,
						unsigned int *handle)
{
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_gem_object *object = intel_fb->obj;

	return drm_gem_handle_create(file_priv, object, handle);
}

static const struct drm_framebuffer_funcs intel_fb_funcs = {
	.destroy = intel_user_framebuffer_destroy,
	.create_handle = intel_user_framebuffer_create_handle,
};

int intel_framebuffer_create(struct drm_device *dev,
			     struct drm_mode_fb_cmd *mode_cmd,
			     struct drm_framebuffer **fb,
			     struct drm_gem_object *obj)
{
	struct intel_framebuffer *intel_fb;
	int ret;

	intel_fb = kzalloc(sizeof(*intel_fb), GFP_KERNEL);
	if (!intel_fb)
		return -ENOMEM;

	ret = drm_framebuffer_init(dev, &intel_fb->base, &intel_fb_funcs);
	if (ret) {
		DRM_ERROR("framebuffer init failed %d\n", ret);
		return ret;
	}

	drm_helper_mode_fill_fb_struct(&intel_fb->base, mode_cmd);

	intel_fb->obj = obj;

	*fb = &intel_fb->base;

	return 0;
}


static struct drm_framebuffer *
intel_user_framebuffer_create(struct drm_device *dev,
			      struct drm_file *filp,
			      struct drm_mode_fb_cmd *mode_cmd)
{
	struct drm_gem_object *obj;
	struct drm_framebuffer *fb;
	int ret;

	obj = drm_gem_object_lookup(dev, filp, mode_cmd->handle);
	if (!obj)
		return NULL;

	ret = intel_framebuffer_create(dev, mode_cmd, &fb, obj);
	if (ret) {
		mutex_lock(&dev->struct_mutex);
		drm_gem_object_unreference(obj);
		mutex_unlock(&dev->struct_mutex);
		return NULL;
	}

	return fb;
}

static const struct drm_mode_config_funcs intel_mode_funcs = {
	.fb_create = intel_user_framebuffer_create,
	.fb_changed = intelfb_probe,
};

void intel_init_clock_gating(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/*
	 * Disable clock gating reported to work incorrectly according to the
	 * specs, but enable as much else as we can.
	 */
	if (IS_IGDNG(dev)) {
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
	} else if (IS_I965GM(dev)) {
		I915_WRITE(RENCLK_GATE_D1, I965_RCC_CLOCK_GATE_DISABLE);
		I915_WRITE(RENCLK_GATE_D2, 0);
		I915_WRITE(DSPCLK_GATE_D, 0);
		I915_WRITE(RAMCLK_GATE_D, 0);
		I915_WRITE16(DEUC, 0);
	} else if (IS_I965G(dev)) {
		I915_WRITE(RENCLK_GATE_D1, I965_RCZ_CLOCK_GATE_DISABLE |
		       I965_RCC_CLOCK_GATE_DISABLE |
		       I965_RCPB_CLOCK_GATE_DISABLE |
		       I965_ISC_CLOCK_GATE_DISABLE |
		       I965_FBC_CLOCK_GATE_DISABLE);
		I915_WRITE(RENCLK_GATE_D2, 0);
	} else if (IS_I9XX(dev)) {
		u32 dstate = I915_READ(D_STATE);

		dstate |= DSTATE_PLL_D3_OFF | DSTATE_GFX_CLOCK_GATING |
			DSTATE_DOT_CLOCK_GATING;
		I915_WRITE(D_STATE, dstate);
	} else if (IS_I855(dev) || IS_I865G(dev)) {
		I915_WRITE(RENCLK_GATE_D1, SV_CLOCK_GATE_DISABLE);
	} else if (IS_I830(dev)) {
		I915_WRITE(DSPCLK_GATE_D, OVRUNIT_CLOCK_GATE_DISABLE);
	}
}

/* Set up chip specific display functions */
static void intel_init_display(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* We always want a DPMS function */
	if (IS_IGDNG(dev))
		dev_priv->display.dpms = igdng_crtc_dpms;
	else
		dev_priv->display.dpms = i9xx_crtc_dpms;

	/* Only mobile has FBC, leave pointers NULL for other chips */
	if (IS_MOBILE(dev)) {
		if (IS_GM45(dev)) {
			dev_priv->display.fbc_enabled = g4x_fbc_enabled;
			dev_priv->display.enable_fbc = g4x_enable_fbc;
			dev_priv->display.disable_fbc = g4x_disable_fbc;
		} else if (IS_I965GM(dev) || IS_I945GM(dev) || IS_I915GM(dev)) {
			dev_priv->display.fbc_enabled = i8xx_fbc_enabled;
			dev_priv->display.enable_fbc = i8xx_enable_fbc;
			dev_priv->display.disable_fbc = i8xx_disable_fbc;
		}
		/* 855GM needs testing */
	}

	/* Returns the core display clock speed */
	if (IS_I945G(dev))
		dev_priv->display.get_display_clock_speed =
			i945_get_display_clock_speed;
	else if (IS_I915G(dev))
		dev_priv->display.get_display_clock_speed =
			i915_get_display_clock_speed;
	else if (IS_I945GM(dev) || IS_845G(dev) || IS_IGDGM(dev))
		dev_priv->display.get_display_clock_speed =
			i9xx_misc_get_display_clock_speed;
	else if (IS_I915GM(dev))
		dev_priv->display.get_display_clock_speed =
			i915gm_get_display_clock_speed;
	else if (IS_I865G(dev))
		dev_priv->display.get_display_clock_speed =
			i865_get_display_clock_speed;
	else if (IS_I855(dev))
		dev_priv->display.get_display_clock_speed =
			i855_get_display_clock_speed;
	else /* 852, 830 */
		dev_priv->display.get_display_clock_speed =
			i830_get_display_clock_speed;

	/* For FIFO watermark updates */
	if (IS_IGDNG(dev))
		dev_priv->display.update_wm = NULL;
	else if (IS_G4X(dev))
		dev_priv->display.update_wm = g4x_update_wm;
	else if (IS_I965G(dev))
		dev_priv->display.update_wm = i965_update_wm;
	else if (IS_I9XX(dev) || IS_MOBILE(dev)) {
		dev_priv->display.update_wm = i9xx_update_wm;
		dev_priv->display.get_fifo_size = i9xx_get_fifo_size;
	} else {
		if (IS_I85X(dev))
			dev_priv->display.get_fifo_size = i85x_get_fifo_size;
		else if (IS_845G(dev))
			dev_priv->display.get_fifo_size = i845_get_fifo_size;
		else
			dev_priv->display.get_fifo_size = i830_get_fifo_size;
		dev_priv->display.update_wm = i830_update_wm;
	}
}

void intel_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int num_pipe;
	int i;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.funcs = (void *)&intel_mode_funcs;

	intel_init_display(dev);

	if (IS_I965G(dev)) {
		dev->mode_config.max_width = 8192;
		dev->mode_config.max_height = 8192;
	} else if (IS_I9XX(dev)) {
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
	} else {
		dev->mode_config.max_width = 2048;
		dev->mode_config.max_height = 2048;
	}

	/* set memory base */
	if (IS_I9XX(dev))
		dev->mode_config.fb_base = pci_resource_start(dev->pdev, 2);
	else
		dev->mode_config.fb_base = pci_resource_start(dev->pdev, 0);

	if (IS_MOBILE(dev) || IS_I9XX(dev))
		num_pipe = 2;
	else
		num_pipe = 1;
	DRM_DEBUG("%d display pipe%s available.\n",
		  num_pipe, num_pipe > 1 ? "s" : "");

	if (IS_I85X(dev))
		pci_read_config_word(dev->pdev, HPLLCC, &dev_priv->orig_clock);
	else if (IS_I9XX(dev) || IS_G4X(dev))
		pci_read_config_word(dev->pdev, GCFGC, &dev_priv->orig_clock);

	for (i = 0; i < num_pipe; i++) {
		intel_crtc_init(dev, i);
	}

	intel_setup_outputs(dev);

	intel_init_clock_gating(dev);

	INIT_WORK(&dev_priv->idle_work, intel_idle_update);
	setup_timer(&dev_priv->idle_timer, intel_gpu_idle_timer,
		    (unsigned long)dev);
}

void intel_modeset_cleanup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct intel_crtc *intel_crtc;

	mutex_lock(&dev->struct_mutex);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		/* Skip inactive CRTCs */
		if (!crtc->fb)
			continue;

		intel_crtc = to_intel_crtc(crtc);
		intel_increase_pllclock(crtc, false);
		del_timer_sync(&intel_crtc->idle_timer);
	}

	del_timer_sync(&dev_priv->idle_timer);

	mutex_unlock(&dev->struct_mutex);

	if (dev_priv->display.disable_fbc)
		dev_priv->display.disable_fbc(dev);

	drm_mode_config_cleanup(dev);
}


/* current intel driver doesn't take advantage of encoders
   always give back the encoder for the connector
*/
struct drm_encoder *intel_best_encoder(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);

	return &intel_output->enc;
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
