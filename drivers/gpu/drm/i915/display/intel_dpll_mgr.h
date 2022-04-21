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

#include "intel_wakeref.h"

/*FIXME: Move this to a more appropriate place. */
#define abs_diff(a, b) ({			\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	(void) (&__a == &__b);			\
	__a > __b ? (__a - __b) : (__b - __a); })

enum tc_port;
struct drm_device;
struct drm_i915_private;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_encoder;
struct intel_shared_dpll;
struct intel_shared_dpll_funcs;

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
	 * @DPLL_ID_ICL_DPLL0: ICL/TGL combo PHY DPLL0
	 */
	DPLL_ID_ICL_DPLL0 = 0,
	/**
	 * @DPLL_ID_ICL_DPLL1: ICL/TGL combo PHY DPLL1
	 */
	DPLL_ID_ICL_DPLL1 = 1,
	/**
	 * @DPLL_ID_EHL_DPLL4: EHL combo PHY DPLL4
	 */
	DPLL_ID_EHL_DPLL4 = 2,
	/**
	 * @DPLL_ID_ICL_TBTPLL: ICL/TGL TBT PLL
	 */
	DPLL_ID_ICL_TBTPLL = 2,
	/**
	 * @DPLL_ID_ICL_MGPLL1: ICL MG PLL 1 port 1 (C),
	 *                      TGL TC PLL 1 port 1 (TC1)
	 */
	DPLL_ID_ICL_MGPLL1 = 3,
	/**
	 * @DPLL_ID_ICL_MGPLL2: ICL MG PLL 1 port 2 (D)
	 *                      TGL TC PLL 1 port 2 (TC2)
	 */
	DPLL_ID_ICL_MGPLL2 = 4,
	/**
	 * @DPLL_ID_ICL_MGPLL3: ICL MG PLL 1 port 3 (E)
	 *                      TGL TC PLL 1 port 3 (TC3)
	 */
	DPLL_ID_ICL_MGPLL3 = 5,
	/**
	 * @DPLL_ID_ICL_MGPLL4: ICL MG PLL 1 port 4 (F)
	 *                      TGL TC PLL 1 port 4 (TC4)
	 */
	DPLL_ID_ICL_MGPLL4 = 6,
	/**
	 * @DPLL_ID_TGL_MGPLL5: TGL TC PLL port 5 (TC5)
	 */
	DPLL_ID_TGL_MGPLL5 = 7,
	/**
	 * @DPLL_ID_TGL_MGPLL6: TGL TC PLL port 6 (TC6)
	 */
	DPLL_ID_TGL_MGPLL6 = 8,

	/**
	 * @DPLL_ID_DG1_DPLL0: DG1 combo PHY DPLL0
	 */
	DPLL_ID_DG1_DPLL0 = 0,
	/**
	 * @DPLL_ID_DG1_DPLL1: DG1 combo PHY DPLL1
	 */
	DPLL_ID_DG1_DPLL1 = 1,
	/**
	 * @DPLL_ID_DG1_DPLL2: DG1 combo PHY DPLL2
	 */
	DPLL_ID_DG1_DPLL2 = 2,
	/**
	 * @DPLL_ID_DG1_DPLL3: DG1 combo PHY DPLL3
	 */
	DPLL_ID_DG1_DPLL3 = 3,
};

#define I915_NUM_PLLS 9

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

	/* icl */
	u32 cfgcr0;

	/* tgl */
	u32 div0;

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
	 * @pipe_mask: mask of pipes using this DPLL, active or not
	 */
	u8 pipe_mask;

	/**
	 * @hw_state: hardware configuration for the DPLL stored in
	 * struct &intel_dpll_hw_state.
	 */
	struct intel_dpll_hw_state hw_state;
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
	 * @active_mask: mask of active pipes (i.e. DPMS on) using this DPLL
	 */
	u8 active_mask;

	/**
	 * @on: is the PLL actually active? Disabled during modeset
	 */
	bool on;

	/**
	 * @info: platform specific info
	 */
	const struct dpll_info *info;

	/**
	 * @wakeref: In some platforms a device-level runtime pm reference may
	 * need to be grabbed to disable DC states while this DPLL is enabled
	 */
	intel_wakeref_t wakeref;
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
int intel_dpll_get_freq(struct drm_i915_private *i915,
			const struct intel_shared_dpll *pll,
			const struct intel_dpll_hw_state *pll_state);
bool intel_dpll_get_hw_state(struct drm_i915_private *i915,
			     struct intel_shared_dpll *pll,
			     struct intel_dpll_hw_state *hw_state);
void intel_enable_shared_dpll(const struct intel_crtc_state *crtc_state);
void intel_disable_shared_dpll(const struct intel_crtc_state *crtc_state);
void intel_shared_dpll_swap_state(struct intel_atomic_state *state);
void intel_shared_dpll_init(struct drm_device *dev);
void intel_dpll_update_ref_clks(struct drm_i915_private *dev_priv);
void intel_dpll_readout_hw_state(struct drm_i915_private *dev_priv);
void intel_dpll_sanitize_state(struct drm_i915_private *dev_priv);

void intel_dpll_dump_hw_state(struct drm_i915_private *dev_priv,
			      const struct intel_dpll_hw_state *hw_state);
enum intel_dpll_id icl_tc_port_to_pll_id(enum tc_port tc_port);
bool intel_dpll_is_combophy(enum intel_dpll_id id);

#endif /* _INTEL_DPLL_MGR_H_ */
