// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_ptp_hw.h"

static const struct
ice_tspll_params_e82x e82x_tspll_params[NUM_ICE_TSPLL_FREQ] = {
	[ICE_TSPLL_FREQ_25_000] = {
		.refclk_pre_div = 1,
		.post_pll_div = 6,
		.feedback_div = 197,
		.frac_n_div = 2621440,
	},
	[ICE_TSPLL_FREQ_122_880] = {
		.refclk_pre_div = 5,
		.post_pll_div = 7,
		.feedback_div = 223,
		.frac_n_div = 524288
	},
	[ICE_TSPLL_FREQ_125_000] = {
		.refclk_pre_div = 5,
		.post_pll_div = 7,
		.feedback_div = 223,
		.frac_n_div = 524288
	},
	[ICE_TSPLL_FREQ_153_600] = {
		.refclk_pre_div = 5,
		.post_pll_div = 6,
		.feedback_div = 159,
		.frac_n_div = 1572864
	},
	[ICE_TSPLL_FREQ_156_250] = {
		.refclk_pre_div = 5,
		.post_pll_div = 6,
		.feedback_div = 159,
		.frac_n_div = 1572864
	},
	[ICE_TSPLL_FREQ_245_760] = {
		.refclk_pre_div = 10,
		.post_pll_div = 7,
		.feedback_div = 223,
		.frac_n_div = 524288
	},
};

/**
 * ice_tspll_clk_freq_str - Convert time_ref_freq to string
 * @clk_freq: Clock frequency
 *
 * Return: specified TIME_REF clock frequency converted to a string.
 */
static const char *ice_tspll_clk_freq_str(enum ice_tspll_freq clk_freq)
{
	switch (clk_freq) {
	case ICE_TSPLL_FREQ_25_000:
		return "25 MHz";
	case ICE_TSPLL_FREQ_122_880:
		return "122.88 MHz";
	case ICE_TSPLL_FREQ_125_000:
		return "125 MHz";
	case ICE_TSPLL_FREQ_153_600:
		return "153.6 MHz";
	case ICE_TSPLL_FREQ_156_250:
		return "156.25 MHz";
	case ICE_TSPLL_FREQ_245_760:
		return "245.76 MHz";
	default:
		return "Unknown";
	}
}

/**
 * ice_tspll_clk_src_str - Convert time_ref_src to string
 * @clk_src: Clock source
 *
 * Return: specified clock source converted to its string name
 */
static const char *ice_tspll_clk_src_str(enum ice_clk_src clk_src)
{
	switch (clk_src) {
	case ICE_CLK_SRC_TCXO:
		return "TCXO";
	case ICE_CLK_SRC_TIME_REF:
		return "TIME_REF";
	default:
		return "Unknown";
	}
}

/**
 * ice_tspll_cfg_e82x - Configure the Clock Generation Unit TSPLL
 * @hw: Pointer to the HW struct
 * @clk_freq: Clock frequency to program
 * @clk_src: Clock source to select (TIME_REF, or TCXO)
 *
 * Configure the Clock Generation Unit with the desired clock frequency and
 * time reference, enabling the PLL which drives the PTP hardware clock.
 *
 * Return:
 * * %0       - success
 * * %-EINVAL - input parameters are incorrect
 * * %-EBUSY  - failed to lock TSPLL
 * * %other   - CGU read/write failure
 */
static int ice_tspll_cfg_e82x(struct ice_hw *hw, enum ice_tspll_freq clk_freq,
			      enum ice_clk_src clk_src)
{
	union tspll_ro_bwm_lf bwm_lf;
	union ice_cgu_r19_e82x dw19;
	union ice_cgu_r22 dw22;
	union ice_cgu_r24 dw24;
	union ice_cgu_r9 dw9;
	int err;

	if (clk_freq >= NUM_ICE_TSPLL_FREQ) {
		dev_warn(ice_hw_to_dev(hw), "Invalid TIME_REF frequency %u\n",
			 clk_freq);
		return -EINVAL;
	}

	if (clk_src >= NUM_ICE_CLK_SRC) {
		dev_warn(ice_hw_to_dev(hw), "Invalid clock source %u\n",
			 clk_src);
		return -EINVAL;
	}

	if (clk_src == ICE_CLK_SRC_TCXO && clk_freq != ICE_TSPLL_FREQ_25_000) {
		dev_warn(ice_hw_to_dev(hw),
			 "TCXO only supports 25 MHz frequency\n");
		return -EINVAL;
	}

	err = ice_read_cgu_reg(hw, ICE_CGU_R9, &dw9.val);
	if (err)
		return err;

	err = ice_read_cgu_reg(hw, ICE_CGU_R24, &dw24.val);
	if (err)
		return err;

	err = ice_read_cgu_reg(hw, TSPLL_RO_BWM_LF, &bwm_lf.val);
	if (err)
		return err;

	/* Log the current clock configuration */
	ice_debug(hw, ICE_DBG_PTP, "Current TSPLL configuration -- %s, clk_src %s, clk_freq %s, PLL %s\n",
		  str_enabled_disabled(dw24.ts_pll_enable),
		  ice_tspll_clk_src_str(dw24.time_ref_sel),
		  ice_tspll_clk_freq_str(dw9.time_ref_freq_sel),
		  bwm_lf.plllock_true_lock_cri ? "locked" : "unlocked");

	/* Disable the PLL before changing the clock source or frequency */
	if (dw24.ts_pll_enable) {
		dw24.ts_pll_enable = 0;

		err = ice_write_cgu_reg(hw, ICE_CGU_R24, dw24.val);
		if (err)
			return err;
	}

	/* Set the frequency */
	dw9.time_ref_freq_sel = clk_freq;
	err = ice_write_cgu_reg(hw, ICE_CGU_R9, dw9.val);
	if (err)
		return err;

	/* Configure the TSPLL feedback divisor */
	err = ice_read_cgu_reg(hw, ICE_CGU_R19, &dw19.val);
	if (err)
		return err;

	dw19.fbdiv_intgr = e82x_tspll_params[clk_freq].feedback_div;
	dw19.ndivratio = 1;

	err = ice_write_cgu_reg(hw, ICE_CGU_R19, dw19.val);
	if (err)
		return err;

	/* Configure the TSPLL post divisor */
	err = ice_read_cgu_reg(hw, ICE_CGU_R22, &dw22.val);
	if (err)
		return err;

	dw22.time1588clk_div = e82x_tspll_params[clk_freq].post_pll_div;
	dw22.time1588clk_sel_div2 = 0;

	err = ice_write_cgu_reg(hw, ICE_CGU_R22, dw22.val);
	if (err)
		return err;

	/* Configure the TSPLL pre divisor and clock source */
	err = ice_read_cgu_reg(hw, ICE_CGU_R24, &dw24.val);
	if (err)
		return err;

	dw24.ref1588_ck_div = e82x_tspll_params[clk_freq].refclk_pre_div;
	dw24.fbdiv_frac = e82x_tspll_params[clk_freq].frac_n_div;
	dw24.time_ref_sel = clk_src;

	err = ice_write_cgu_reg(hw, ICE_CGU_R24, dw24.val);
	if (err)
		return err;

	/* Finally, enable the PLL */
	dw24.ts_pll_enable = 1;

	err = ice_write_cgu_reg(hw, ICE_CGU_R24, dw24.val);
	if (err)
		return err;

	/* Wait to verify if the PLL locks */
	usleep_range(1000, 5000);

	err = ice_read_cgu_reg(hw, TSPLL_RO_BWM_LF, &bwm_lf.val);
	if (err)
		return err;

	if (!bwm_lf.plllock_true_lock_cri) {
		dev_warn(ice_hw_to_dev(hw), "TSPLL failed to lock\n");
		return -EBUSY;
	}

	/* Log the current clock configuration */
	ice_debug(hw, ICE_DBG_PTP, "New TSPLL configuration -- %s, clk_src %s, clk_freq %s, PLL %s\n",
		  str_enabled_disabled(dw24.ts_pll_enable),
		  ice_tspll_clk_src_str(dw24.time_ref_sel),
		  ice_tspll_clk_freq_str(dw9.time_ref_freq_sel),
		  bwm_lf.plllock_true_lock_cri ? "locked" : "unlocked");

	return 0;
}

/**
 * ice_tspll_dis_sticky_bits_e82x - disable TSPLL sticky bits
 * @hw: Pointer to the HW struct
 *
 * Configure the Clock Generation Unit TSPLL sticky bits so they don't latch on
 * losing TSPLL lock, but always show current state.
 *
 * Return: 0 on success, other error codes when failed to read/write CGU.
 */
static int ice_tspll_dis_sticky_bits_e82x(struct ice_hw *hw)
{
	union tspll_cntr_bist_settings cntr_bist;
	int err;

	err = ice_read_cgu_reg(hw, TSPLL_CNTR_BIST_SETTINGS, &cntr_bist.val);
	if (err)
		return err;

	/* Disable sticky lock detection so lock err reported is accurate */
	cntr_bist.i_plllock_sel_0 = 0;
	cntr_bist.i_plllock_sel_1 = 0;

	return ice_write_cgu_reg(hw, TSPLL_CNTR_BIST_SETTINGS, cntr_bist.val);
}

/**
 * ice_tspll_cfg_e825c - Configure the TSPLL for E825-C
 * @hw: Pointer to the HW struct
 * @clk_freq: Clock frequency to program
 * @clk_src: Clock source to select (TIME_REF, or TCXO)
 *
 * Configure the Clock Generation Unit with the desired clock frequency and
 * time reference, enabling the PLL which drives the PTP hardware clock.
 *
 * Return:
 * * %0       - success
 * * %-EINVAL - input parameters are incorrect
 * * %-EBUSY  - failed to lock TSPLL
 * * %other   - CGU read/write failure
 */
static int ice_tspll_cfg_e825c(struct ice_hw *hw, enum ice_tspll_freq clk_freq,
			       enum ice_clk_src clk_src)
{
	union tspll_ro_lock_e825c ro_lock;
	union ice_cgu_r19_e825 dw19;
	union ice_cgu_r16 dw16;
	union ice_cgu_r23 dw23;
	union ice_cgu_r22 dw22;
	union ice_cgu_r9 dw9;
	int err;

	if (clk_freq >= NUM_ICE_TSPLL_FREQ) {
		dev_warn(ice_hw_to_dev(hw), "Invalid TIME_REF frequency %u\n",
			 clk_freq);
		return -EINVAL;
	}

	if (clk_src >= NUM_ICE_CLK_SRC) {
		dev_warn(ice_hw_to_dev(hw), "Invalid clock source %u\n",
			 clk_src);
		return -EINVAL;
	}

	if (clk_freq != ICE_TSPLL_FREQ_156_250) {
		dev_warn(ice_hw_to_dev(hw), "Adapter only supports 156.25 MHz frequency\n");
		return -EINVAL;
	}

	err = ice_read_cgu_reg(hw, ICE_CGU_R9, &dw9.val);
	if (err)
		return err;

	err = ice_read_cgu_reg(hw, ICE_CGU_R16, &dw16.val);
	if (err)
		return err;

	err = ice_read_cgu_reg(hw, ICE_CGU_R23, &dw23.val);
	if (err)
		return err;

	err = ice_read_cgu_reg(hw, TSPLL_RO_LOCK_E825C, &ro_lock.val);
	if (err)
		return err;

	/* Log the current clock configuration */
	ice_debug(hw, ICE_DBG_PTP, "Current TSPLL configuration -- %s, clk_src %s, clk_freq %s, PLL %s\n",
		  str_enabled_disabled(dw23.ts_pll_enable),
		  ice_tspll_clk_src_str(dw23.time_ref_sel),
		  ice_tspll_clk_freq_str(dw9.time_ref_freq_sel),
		  ro_lock.plllock_true_lock_cri ? "locked" : "unlocked");

	/* Disable the PLL before changing the clock source or frequency */
	if (dw23.ts_pll_enable) {
		dw23.ts_pll_enable = 0;

		err = ice_write_cgu_reg(hw, ICE_CGU_R23, dw23.val);
		if (err)
			return err;
	}

	/* Set the frequency */
	dw9.time_ref_freq_sel = clk_freq;

	/* Enable the correct receiver */
	if (clk_src == ICE_CLK_SRC_TCXO) {
		dw9.time_ref_en = 0;
		dw9.clk_eref0_en = 1;
	} else {
		dw9.time_ref_en = 1;
		dw9.clk_eref0_en = 0;
	}
	err = ice_write_cgu_reg(hw, ICE_CGU_R9, dw9.val);
	if (err)
		return err;

	/* Choose the referenced frequency */
	dw16.ck_refclkfreq = ICE_TSPLL_CK_REFCLKFREQ_E825;
	err = ice_write_cgu_reg(hw, ICE_CGU_R16, dw16.val);
	if (err)
		return err;

	/* Configure the TSPLL feedback divisor */
	err = ice_read_cgu_reg(hw, ICE_CGU_R19, &dw19.val);
	if (err)
		return err;

	dw19.tspll_fbdiv_intgr = ICE_TSPLL_FBDIV_INTGR_E825;
	dw19.tspll_ndivratio = ICE_TSPLL_NDIVRATIO_E825;

	err = ice_write_cgu_reg(hw, ICE_CGU_R19, dw19.val);
	if (err)
		return err;

	/* Configure the TSPLL post divisor */
	err = ice_read_cgu_reg(hw, ICE_CGU_R22, &dw22.val);
	if (err)
		return err;

	/* These two are constant for E825C */
	dw22.time1588clk_div = 5;
	dw22.time1588clk_sel_div2 = 0;

	err = ice_write_cgu_reg(hw, ICE_CGU_R22, dw22.val);
	if (err)
		return err;

	/* Configure the TSPLL pre divisor and clock source */
	err = ice_read_cgu_reg(hw, ICE_CGU_R23, &dw23.val);
	if (err)
		return err;

	dw23.ref1588_ck_div = 0;
	dw23.time_ref_sel = clk_src;

	err = ice_write_cgu_reg(hw, ICE_CGU_R23, dw23.val);
	if (err)
		return err;

	/* Clear the R24 register. */
	err = ice_write_cgu_reg(hw, ICE_CGU_R24, 0);
	if (err)
		return err;

	/* Finally, enable the PLL */
	dw23.ts_pll_enable = 1;

	err = ice_write_cgu_reg(hw, ICE_CGU_R23, dw23.val);
	if (err)
		return err;

	/* Wait to verify if the PLL locks */
	usleep_range(1000, 5000);

	err = ice_read_cgu_reg(hw, TSPLL_RO_LOCK_E825C, &ro_lock.val);
	if (err)
		return err;

	if (!ro_lock.plllock_true_lock_cri) {
		dev_warn(ice_hw_to_dev(hw), "TSPLL failed to lock\n");
		return -EBUSY;
	}

	/* Log the current clock configuration */
	ice_debug(hw, ICE_DBG_PTP, "New TSPLL configuration -- %s, clk_src %s, clk_freq %s, PLL %s\n",
		  str_enabled_disabled(dw23.ts_pll_enable),
		  ice_tspll_clk_src_str(dw23.time_ref_sel),
		  ice_tspll_clk_freq_str(dw9.time_ref_freq_sel),
		  ro_lock.plllock_true_lock_cri ? "locked" : "unlocked");

	return 0;
}

/**
 * ice_tspll_dis_sticky_bits_e825c - disable TSPLL sticky bits for E825-C
 * @hw: Pointer to the HW struct
 *
 * Configure the Clock Generation Unit TSPLL sticky bits so they don't latch on
 * losing TSPLL lock, but always show current state.
 *
 * Return: 0 on success, other error codes when failed to read/write CGU.
 */
static int ice_tspll_dis_sticky_bits_e825c(struct ice_hw *hw)
{
	union tspll_bw_tdc_e825c bw_tdc;
	int err;

	err = ice_read_cgu_reg(hw, TSPLL_BW_TDC_E825C, &bw_tdc.val);
	if (err)
		return err;

	bw_tdc.i_plllock_sel_1_0 = 0;

	return ice_write_cgu_reg(hw, TSPLL_BW_TDC_E825C, bw_tdc.val);
}

#define ICE_ONE_PPS_OUT_AMP_MAX 3

/**
 * ice_tspll_cfg_pps_out_e825c - Enable/disable 1PPS output and set amplitude
 * @hw: pointer to the HW struct
 * @enable: true to enable 1PPS output, false to disable it
 *
 * Return: 0 on success, other negative error code when CGU read/write failed.
 */
int ice_tspll_cfg_pps_out_e825c(struct ice_hw *hw, bool enable)
{
	union ice_cgu_r9 r9;
	int err;

	err = ice_read_cgu_reg(hw, ICE_CGU_R9, &r9.val);
	if (err)
		return err;

	r9.one_pps_out_en = enable;
	r9.one_pps_out_amp = enable * ICE_ONE_PPS_OUT_AMP_MAX;
	return ice_write_cgu_reg(hw, ICE_CGU_R9, r9.val);
}

/**
 * ice_tspll_init - Initialize TSPLL with settings from firmware
 * @hw: Pointer to the HW structure
 *
 * Initialize the Clock Generation Unit of the E82X/E825 device.
 *
 * Return: 0 on success, other error codes when failed to read/write/cfg CGU.
 */
int ice_tspll_init(struct ice_hw *hw)
{
	struct ice_ts_func_info *ts_info = &hw->func_caps.ts_func_info;
	int err;

	/* Disable sticky lock detection so lock err reported is accurate. */
	if (hw->mac_type == ICE_MAC_GENERIC_3K_E825)
		err = ice_tspll_dis_sticky_bits_e825c(hw);
	else
		err = ice_tspll_dis_sticky_bits_e82x(hw);
	if (err)
		return err;

	/* Configure the TSPLL using the parameters from the function
	 * capabilities.
	 */
	if (hw->mac_type == ICE_MAC_GENERIC_3K_E825)
		err = ice_tspll_cfg_e825c(hw, ts_info->time_ref,
					  (enum ice_clk_src)ts_info->clk_src);
	else
		err = ice_tspll_cfg_e82x(hw, ts_info->time_ref,
					 (enum ice_clk_src)ts_info->clk_src);

	return err;
}
