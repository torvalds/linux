/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025, Intel Corporation. */

#ifndef _ICE_TSPLL_H_
#define _ICE_TSPLL_H_

/**
 * struct ice_cgu_pll_params_e82x - E82X CGU parameters
 * @refclk_pre_div: Reference clock pre-divisor
 * @feedback_div: Feedback divisor
 * @frac_n_div: Fractional divisor
 * @post_pll_div: Post PLL divisor
 *
 * Clock Generation Unit parameters used to program the PLL based on the
 * selected TIME_REF frequency.
 */
struct ice_cgu_pll_params_e82x {
	u32 refclk_pre_div;
	u32 feedback_div;
	u32 frac_n_div;
	u32 post_pll_div;
};

/**
 * struct ice_cgu_pll_params_e825c - E825C CGU parameters
 * @tspll_ck_refclkfreq: tspll_ck_refclkfreq selection
 * @tspll_ndivratio: ndiv ratio that goes directly to the pll
 * @tspll_fbdiv_intgr: TS PLL integer feedback divide
 * @tspll_fbdiv_frac:  TS PLL fractional feedback divide
 * @ref1588_ck_div: clock divider for tspll ref
 *
 * Clock Generation Unit parameters used to program the PLL based on the
 * selected TIME_REF/TCXO frequency.
 */
struct ice_cgu_pll_params_e825c {
	u32 tspll_ck_refclkfreq;
	u32 tspll_ndivratio;
	u32 tspll_fbdiv_intgr;
	u32 tspll_fbdiv_frac;
	u32 ref1588_ck_div;
};

int ice_cgu_cfg_pps_out(struct ice_hw *hw, bool enable);
int ice_init_cgu_e82x(struct ice_hw *hw);

#endif /* _ICE_TSPLL_H_ */
