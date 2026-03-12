/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025, Intel Corporation. */

#ifndef _ICE_TSPLL_H_
#define _ICE_TSPLL_H_

/**
 * struct ice_tspll_params_e82x - E82X TSPLL parameters
 * @refclk_pre_div: Reference clock pre-divisor
 * @post_pll_div: Post PLL divisor
 * @feedback_div: Feedback divisor
 * @frac_n_div: Fractional divisor
 *
 * Clock Generation Unit parameters used to program the PLL based on the
 * selected TIME_REF/TCXO frequency.
 */
struct ice_tspll_params_e82x {
	u8 refclk_pre_div;
	u8 post_pll_div;
	u8 feedback_div;
	u32 frac_n_div;
};

#define ICE_CGU_NET_REF_CLK0		0x0
#define ICE_CGU_REF_CLK_BYP0		0x5
#define ICE_CGU_REF_CLK_BYP0_DIV	0x0
#define ICE_CGU_REF_CLK_BYP1		0x4
#define ICE_CGU_REF_CLK_BYP1_DIV	0x1

#define ICE_TSPLL_CK_REFCLKFREQ_E825		0x1F
#define ICE_TSPLL_NDIVRATIO_E825		5
#define ICE_TSPLL_FBDIV_INTGR_E825		256

int ice_tspll_cfg_pps_out_e825c(struct ice_hw *hw, bool enable);
int ice_tspll_init(struct ice_hw *hw);
int ice_tspll_bypass_mux_active_e825c(struct ice_hw *hw, u8 port, bool *active,
				      enum ice_synce_clk output);
int ice_tspll_cfg_bypass_mux_e825c(struct ice_hw *hw, bool ena, u32 port_num,
				   enum ice_synce_clk output);
int ice_tspll_cfg_synce_ethdiv_e825c(struct ice_hw *hw,
				     enum ice_synce_clk output);
#endif /* _ICE_TSPLL_H_ */
