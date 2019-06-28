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

#include <linux/types.h>

#include "intel_display.h"

/*FIXME: Move this to a more appropriate place. */
#define abs_diff(a, b) ({			\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	(void) (&__a == &__b);			\
	__a > __b ? (__a - __b) : (__b - __a); })

struct drm_atomic_state;
struct drm_device;
struct drm_i915_private;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_encoder;
struct intel_shared_dpll;

/**
 * enum intel_dpll_id - possible DPLL ids
 *
 * Enumeration of possible IDs for a DPLL. Real shared dpll ids must be >= 0.
 */
enum intel_dpll_id {
	/**
	 * @DPLL_ID_PRIVATE: non-shared dpll in use
	 */
	DPLL_ID_PRIVATE = -1,

	/**
	 * @DPLL_ID_PCH_PLL_A: DPLL A in ILK, SNB and IVB
	 */
	DPLL_ID_PCH_PLL_A = 0,
	/**
	 * @DPLL_ID_PCH_PLL_B: DPLL B in ILK, SNB and IVB
	 */
	DPLL_ID_PCH_PLL_B = 1,


	/**
	 * @DPLL_ID_WRPLL1: HSW and BDW WRPLL1
	 */
	DPLL_ID_WRPLL1 = 0,
	/**
	 * @DPLL_ID_WRPLL2: HSW and BDW WRPLL2
	 */
	DPLL_ID_WRPLL2 = 1,
	/**
	 * @DPLL_ID_SPLL: HSW and BDW SPLL
	 */
	DPLL_ID_SPLL = 2,
	/**
	 * @DPLL_ID_LCPLL_810: HSW and BDW 0.81 GHz LCPLL
	 */
	DPLL_ID_LCPLL_810 = 3,
	/**
	 * @DPLL_ID_LCPLL_1350: HSW and BDW 1.35 GHz LCPLL
	 */
	DPLL_ID_LCPLL_1350 = 4,
	/**
	 * @DPLL_ID_LCPLL_2700: HSW and BDW 2.7 GHz LCPLL
	 */
	DPLL_ID_LCPLL_2700 = 5,


	/**
	 * @DPLL_ID_SKL_DPLL0: SKL and later DPLL0
	 */
	DPLL_ID_SKL_DPLL0 = 0,
	/**
	 * @DPLL_ID_SKL_DPLL1: SKL and later DPLL1
	 */
	DPLL_ID_SKL_DPLL1 = 1,
	/**
	 * @DPLL_ID_SKL_DPLL2: SKL and later DPLL2
	 */
	DPLL_ID_SKL_DPLL2 = 2,
	/**
	 * @DPLL_ID_SKL_DPLL3: SKL and later DPLL3
	 */
	DPLL_ID_SKL_DPLL3 = 3,


	/**
	 * @DPLL_ID_ICL_DPLL0: ICL combo PHY DPLL0
	 */
	DPLL_ID_ICL_DPLL0 = 0,
	/**
	 * @DPLL_ID_ICL_DPLL1: ICL combo PHY DPLL1
	 */
	DPLL_ID_ICL_DPLL1 = 1,
	/**
	 * @DPLL_ID_ICL_TBTPLL: ICL TBT PLL
	 */
	DPLL_ID_ICL_TBTPLL = 2,
	/**
	 * @DPLL_ID_ICL_MGPLL1: ICL MG PLL 1 port 1 (C)
	 */
	DPLL_ID_ICL_MGPLL1 = 3,
	/**
	 * @DPLL_ID_ICL_MGPLL2: ICL MG PLL 1 port 2 (D)
	 */
	DPLL_ID_ICL_MGPLL2 = 4,
	/**
	 * @DPLL_ID_ICL_MGPLL3: ICL MG PLL 1 port 3 (E)
	 */
	DPLL_ID_ICL_MGPLL3 = 5,
	/**
	 * @DPLL_ID_ICL_MGPLL4: ICL MG PLL 1 port 4 (F)
	 */
	DPLL_ID_ICL_MGPLL4 = 6,
};
#define I915_NUM_PLLS 7

enum icl_port_dpll_id {
	ICL_PORT_DPLL_DEFAULT,
	ICL_PORT_DPLL_MG_PHY,

	ICL_PORT_DPLL_COUNT,
};

struct intel_dpll_hw_state {
	/* i9xx, pch plls */
	u32 dpll;
	u32 dpll_md;
	u32 fp0;
	u32 fp1;

	/* hsw, bdw */
	u32 wrpll;
	u32 spll;

	/* skl */
	/*
	 * DPLL_CTRL1 has 6 bits for each each this DPLL. We store those in
	 * lower part of ctrl1 and they get shifted into position when writing
	 * the register.  This allows us to easily compare the state to share
	 * the DPLL.
	 */
	u32 ctrl1;
	/* HDMI only, 0 when used for DP */
	u32 cfgcr1, cfgcr2;

	/* cnl */
	u32 cfgcr0;
	/* CNL also uses cfgcr1 */

	/* bxt */
	u32 ebb0, ebb4, pll0, pll1, pll2, pll3, pll6, pll8, pll9, pll10, pcsdw12;

	/*
	 * ICL uses the following, already defined:
	 * u32 cfgcr0, cfgcr1;
	 */
	u32 mg_refclkin_ctl;
	u32 mg_clktop2_coreclkctl1;
	u32 mg_clktop2_hsclkctl;
	u32 mg_pll_div0;
	u32 mg_pll_div1;
	u32 mg_pll_lf;
	u32 mg_pll_frac_lock;
	u32 mg_pll_ssc;
	u32 mg_pll_bias;
	u32 mg_pll_tdc_coldst_bias;
	u32 mg_pll_bias_mask;
	u32 mg_pll_tdc_coldst_bias_mask;
};

/**
 * struct intel_shared_dpll_state - hold the DPLL atomic state
 *
 * This structure holds an atomic state for the DPLL, that can represent
 * either its current state (in struct &intel_shared_dpll) or a desired
 * future state which would be applied by an atomic mode set (stored in
 * a struct &intel_atomic_state).
 *
 * See also intel_reserve_shared_dplls() and intel_release_shared_dplls().
 */
struct intel_shared_dpll_state {
	/**
	 * @crtc_mask: mask of CRTC using this DPLL, active or not
	 */
	unsigned crtc_mask;

	/**
	 * @hw_state: hardware configuration for the DPLL stored in
	 * struct &intel_dpll_hw_state.
	 */
	struct intel_dpll_hw_state hw_state;
};

/**
 * struct intel_shared_dpll_funcs - platform specific hooks for managing DPLLs
 */
struct intel_shared_dpll_funcs {
	/**
	 * @prepare:
	 *
	 * Optional hook to perform operations prior to enabling the PLL.
	 * Called from intel_prepare_shared_dpll() function unless the PLL
	 * is already enabled.
	 */
	void (*prepare)(struct drm_i915_private *dev_priv,
			struct intel_shared_dpll *pll);

	/**
	 * @enable:
	 *
	 * Hook for enabling the pll, called from intel_enable_shared_dpll()
	 * if the pll is not already enabled.
	 */
	void (*enable)(struct drm_i915_private *dev_priv,
		       struct intel_shared_dpll *pll);

	/**
	 * @disable:
	 *
	 * Hook for disabling the pll, called from intel_disable_shared_dpll()
	 * only when it is safe to disable the pll, i.e., there are no more
	 * tracked users for it.
	 */
	void (*disable)(struct drm_i915_private *dev_priv,
			struct intel_shared_dpll *pll);

	/**
	 * @get_hw_state:
	 *
	 * Hook for reading the values currently programmed to the DPLL
	 * registers. This is used for initial hw state readout and state
	 * verification after a mode set.
	 */
	bool (*get_hw_state)(struct drm_i915_private *dev_priv,
			     struct intel_shared_dpll *pll,
			     struct intel_dpll_hw_state *hw_state);
};

/**
 * struct dpll_info - display PLL platform specific info
 */
struct dpll_info {
	/**
	 * @name: DPLL name; used for logging
	 */
	const char *name;

	/**
	 * @funcs: platform specific hooks
	 */
	const struct intel_shared_dpll_funcs *funcs;

	/**
	 * @id: unique indentifier for this DPLL; should match the index in the
	 * dev_priv->shared_dplls array
	 */
	enum intel_dpll_id id;

#define INTEL_DPLL_ALWAYS_ON	(1 << 0)
	/**
	 * @flags:
	 *
	 * INTEL_DPLL_ALWAYS_ON
	 *     Inform the state checker that the DPLL is kept enabled even if
	 *     not in use by any CRTC.
	 */
	u32 flags;
};

/**
 * struct intel_shared_dpll - display PLL with tracked state and users
 */
struct intel_shared_dpll {
	/**
	 * @state:
	 *
	 * Store the state for the pll, including its hw state
	 * and CRTCs using it.
	 */
	struct intel_shared_dpll_state state;

	/**
	 * @active_mask: mask of active CRTCs (i.e. DPMS on) using this DPLL
	 */
	unsigned active_mask;

	/**
	 * @on: is the PLL actually active? Disabled during modeset
	 */
	bool on;

	/**
	 * @info: platform specific info
	 */
	const struct dpll_info *info;
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
void assert_shared_dpll(struct drm_i915_private *dev_priv,
			struct intel_shared_dpll *pll,
			bool state);
#define assert_shared_dpll_enabled(d, p) assert_shared_dpll(d, p, true)
#define assert_shared_dpll_disabled(d, p) assert_shared_dpll(d, p, false)
bool intel_reserve_shared_dplls(struct intel_atomic_state *state,
				struct intel_crtc *crtc,
				struct intel_encoder *encoder);
void intel_release_shared_dplls(struct intel_atomic_state *state,
				struct intel_crtc *crtc);
void icl_set_active_port_dpll(struct intel_crtc_state *crtc_state,
			      enum icl_port_dpll_id port_dpll_id);
void intel_update_active_dpll(struct intel_atomic_state *state,
			      struct intel_crtc *crtc,
			      struct intel_encoder *encoder);
void intel_prepare_shared_dpll(const struct intel_crtc_state *crtc_state);
void intel_enable_shared_dpll(const struct intel_crtc_state *crtc_state);
void intel_disable_shared_dpll(const struct intel_crtc_state *crtc_state);
void intel_shared_dpll_swap_state(struct drm_atomic_state *state);
void intel_shared_dpll_init(struct drm_device *dev);

void intel_dpll_dump_hw_state(struct drm_i915_private *dev_priv,
			      const struct intel_dpll_hw_state *hw_state);
int cnl_hdmi_pll_ref_clock(struct drm_i915_private *dev_priv);
enum intel_dpll_id icl_tc_port_to_pll_id(enum tc_port tc_port);
bool intel_dpll_is_combophy(enum intel_dpll_id id);

#endif /* _INTEL_DPLL_MGR_H_ */
