/*
 * Copyright © 2012-2016 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef _INTEL_DPLL_MGR_H_
#define _INTEL_DPLL_MGR_H_

/*FIXME: Move this to a more appropriate place. */
#define abs_diff(a, b) ({			\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	(void) (&__a == &__b);			\
	__a > __b ? (__a - __b) : (__b - __a); })

struct drm_i915_private;
struct intel_crtc;
struct intel_crtc_state;
struct intel_encoder;

struct intel_shared_dpll;
struct intel_dpll_mgr;

enum intel_dpll_id {
	DPLL_ID_PRIVATE = -1, /* non-shared dpll in use */
	/* real shared dpll ids must be >= 0 */
	DPLL_ID_PCH_PLL_A = 0,
	DPLL_ID_PCH_PLL_B = 1,
	/* hsw/bdw */
	DPLL_ID_WRPLL1 = 0,
	DPLL_ID_WRPLL2 = 1,
	DPLL_ID_SPLL = 2,
	DPLL_ID_LCPLL_810 = 3,
	DPLL_ID_LCPLL_1350 = 4,
	DPLL_ID_LCPLL_2700 = 5,

	/* skl */
	DPLL_ID_SKL_DPLL0 = 0,
	DPLL_ID_SKL_DPLL1 = 1,
	DPLL_ID_SKL_DPLL2 = 2,
	DPLL_ID_SKL_DPLL3 = 3,
};
#define I915_NUM_PLLS 6

/** Inform the state checker that the DPLL is kept enabled even if not
 * in use by any crtc.
 */
#define INTEL_DPLL_ALWAYS_ON	(1 << 0)

struct intel_dpll_hw_state {
	/* i9xx, pch plls */
	uint32_t dpll;
	uint32_t dpll_md;
	uint32_t fp0;
	uint32_t fp1;

	/* hsw, bdw */
	uint32_t wrpll;
	uint32_t spll;

	/* skl */
	/*
	 * DPLL_CTRL1 has 6 bits for each each this DPLL. We store those in
	 * lower part of ctrl1 and they get shifted into position when writing
	 * the register.  This allows us to easily compare the state to share
	 * the DPLL.
	 */
	uint32_t ctrl1;
	/* HDMI only, 0 when used for DP */
	uint32_t cfgcr1, cfgcr2;

	/* bxt */
	uint32_t ebb0, ebb4, pll0, pll1, pll2, pll3, pll6, pll8, pll9, pll10,
		 pcsdw12;
};

struct intel_shared_dpll_config {
	unsigned crtc_mask; /* mask of CRTCs sharing this PLL */
	struct intel_dpll_hw_state hw_state;
};

struct intel_shared_dpll_funcs {
	/* The mode_set hook is optional and should be used together with the
	 * intel_prepare_shared_dpll function. */
	void (*mode_set)(struct drm_i915_private *dev_priv,
			 struct intel_shared_dpll *pll);
	void (*enable)(struct drm_i915_private *dev_priv,
		       struct intel_shared_dpll *pll);
	void (*disable)(struct drm_i915_private *dev_priv,
			struct intel_shared_dpll *pll);
	bool (*get_hw_state)(struct drm_i915_private *dev_priv,
			     struct intel_shared_dpll *pll,
			     struct intel_dpll_hw_state *hw_state);
};

struct intel_shared_dpll {
	struct intel_shared_dpll_config config;

	int active; /* count of number of active CRTCs (i.e. DPMS on) */
	bool on; /* is the PLL actually active? Disabled during modeset */
	const char *name;
	/* should match the index in the dev_priv->shared_dplls array */
	enum intel_dpll_id id;

	struct intel_shared_dpll_funcs funcs;

	uint32_t flags;
};

#define SKL_DPLL0 0
#define SKL_DPLL1 1
#define SKL_DPLL2 2
#define SKL_DPLL3 3

/* shared dpll functions */
struct intel_shared_dpll *
intel_get_shared_dpll_by_id(struct drm_i915_private *dev_priv,
			    enum intel_dpll_id id);
enum intel_dpll_id
intel_get_shared_dpll_id(struct drm_i915_private *dev_priv,
			 struct intel_shared_dpll *pll);
void
intel_shared_dpll_config_get(struct intel_shared_dpll_config *config,
			     struct intel_shared_dpll *pll,
			     struct intel_crtc *crtc);
void
intel_shared_dpll_config_put(struct intel_shared_dpll_config *config,
			     struct intel_shared_dpll *pll,
			     struct intel_crtc *crtc);
void assert_shared_dpll(struct drm_i915_private *dev_priv,
			struct intel_shared_dpll *pll,
			bool state);
#define assert_shared_dpll_enabled(d, p) assert_shared_dpll(d, p, true)
#define assert_shared_dpll_disabled(d, p) assert_shared_dpll(d, p, false)
struct intel_shared_dpll *intel_get_shared_dpll(struct intel_crtc *crtc,
						struct intel_crtc_state *state,
						struct intel_encoder *encoder);
void intel_prepare_shared_dpll(struct intel_crtc *crtc);
void intel_enable_shared_dpll(struct intel_crtc *crtc);
void intel_disable_shared_dpll(struct intel_crtc *crtc);
void intel_shared_dpll_commit(struct drm_atomic_state *state);
void intel_shared_dpll_init(struct drm_device *dev);


#endif /* _INTEL_DPLL_MGR_H_ */
