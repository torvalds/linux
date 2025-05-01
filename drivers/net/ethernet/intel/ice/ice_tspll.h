/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025, Intel Corporation. */

#ifndef _ICE_TSPLL_H_
#define _ICE_TSPLL_H_

/**
 * struct ice_tspll_params_e82x - E82X TSPLL parameters
 * @refclk_pre_div: Reference clock pre-divisor
 * @feedback_div: Feedback divisor
 * @frac_n_div: Fractional divisor
 * @post_pll_div: Post PLL divisor
 *
 * Clock Generation Unit parameters used to program the PLL based on the
 * selected TIME_REF/TCXO frequency.
 */
struct ice_tspll_params_e82x {
	u32 refclk_pre_div;
	u32 feedback_div;
	u32 frac_n_div;
	u32 post_pll_div;
};

/**
 * struct ice_tspll_params_e825c - E825-C TSPLL parameters
 * @ck_refclkfreq: ck_refclkfreq selection
 * @ndivratio: ndiv ratio that goes directly to the PLL
 * @fbdiv_intgr: TSPLL integer feedback divisor
 * @fbdiv_frac: TSPLL fractional feedback divisor
 * @ref1588_ck_div: clock divisor for tspll ref
 *
 * Clock Generation Unit parameters used to program the PLL based on the
 * selected TIME_REF/TCXO frequency.
 */
struct ice_tspll_params_e825c {
	u32 ck_refclkfreq;
	u32 ndivratio;
	u32 fbdiv_intgr;
	u32 fbdiv_frac;
	u32 ref1588_ck_div;
};

int ice_tspll_cfg_pps_out_e825c(struct ice_hw *hw, bool enable);
int ice_tspll_init(struct ice_hw *hw);

#endif /* _ICE_TSPLL_H_ */
