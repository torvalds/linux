// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_ptp_hw.h"

static const struct
ice_cgu_pll_params_e82x e822_cgu_params[NUM_ICE_TIME_REF_FREQ] = {
	/* ICE_TIME_REF_FREQ_25_000 -> 25 MHz */
	{
		/* refclk_pre_div */
		1,
		/* feedback_div */
		197,
		/* frac_n_div */
		2621440,
		/* post_pll_div */
		6,
	},

	/* ICE_TIME_REF_FREQ_122_880 -> 122.88 MHz */
	{
		/* refclk_pre_div */
		5,
		/* feedback_div */
		223,
		/* frac_n_div */
		524288,
		/* post_pll_div */
		7,
	},

	/* ICE_TIME_REF_FREQ_125_000 -> 125 MHz */
	{
		/* refclk_pre_div */
		5,
		/* feedback_div */
		223,
		/* frac_n_div */
		524288,
		/* post_pll_div */
		7,
	},

	/* ICE_TIME_REF_FREQ_153_600 -> 153.6 MHz */
	{
		/* refclk_pre_div */
		5,
		/* feedback_div */
		159,
		/* frac_n_div */
		1572864,
		/* post_pll_div */
		6,
	},

	/* ICE_TIME_REF_FREQ_156_250 -> 156.25 MHz */
	{
		/* refclk_pre_div */
		5,
		/* feedback_div */
		159,
		/* frac_n_div */
		1572864,
		/* post_pll_div */
		6,
	},

	/* ICE_TIME_REF_FREQ_245_760 -> 245.76 MHz */
	{
		/* refclk_pre_div */
		10,
		/* feedback_div */
		223,
		/* frac_n_div */
		524288,
		/* post_pll_div */
		7,
	},
};

static const struct
ice_cgu_pll_params_e825c e825c_cgu_params[NUM_ICE_TIME_REF_FREQ] = {
	/* ICE_TIME_REF_FREQ_25_000 -> 25 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x19,
		/* tspll_ndivratio */
		1,
		/* tspll_fbdiv_intgr */
		320,
		/* tspll_fbdiv_frac */
		0,
		/* ref1588_ck_div */
		0,
	},

	/* ICE_TIME_REF_FREQ_122_880 -> 122.88 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x29,
		/* tspll_ndivratio */
		3,
		/* tspll_fbdiv_intgr */
		195,
		/* tspll_fbdiv_frac */
		1342177280UL,
		/* ref1588_ck_div */
		0,
	},

	/* ICE_TIME_REF_FREQ_125_000 -> 125 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x3E,
		/* tspll_ndivratio */
		2,
		/* tspll_fbdiv_intgr */
		128,
		/* tspll_fbdiv_frac */
		0,
		/* ref1588_ck_div */
		0,
	},

	/* ICE_TIME_REF_FREQ_153_600 -> 153.6 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x33,
		/* tspll_ndivratio */
		3,
		/* tspll_fbdiv_intgr */
		156,
		/* tspll_fbdiv_frac */
		1073741824UL,
		/* ref1588_ck_div */
		0,
	},

	/* ICE_TIME_REF_FREQ_156_250 -> 156.25 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x1F,
		/* tspll_ndivratio */
		5,
		/* tspll_fbdiv_intgr */
		256,
		/* tspll_fbdiv_frac */
		0,
		/* ref1588_ck_div */
		0,
	},

	/* ICE_TIME_REF_FREQ_245_760 -> 245.76 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x52,
		/* tspll_ndivratio */
		3,
		/* tspll_fbdiv_intgr */
		97,
		/* tspll_fbdiv_frac */
		2818572288UL,
		/* ref1588_ck_div */
		0,
	},
};

/**
 * ice_clk_freq_str - Convert time_ref_freq to string
 * @clk_freq: Clock frequency
 *
 * Return: specified TIME_REF clock frequency converted to a string
 */
static const char *ice_clk_freq_str(enum ice_time_ref_freq clk_freq)
{
	switch (clk_freq) {
	case ICE_TIME_REF_FREQ_25_000:
		return "25 MHz";
	case ICE_TIME_REF_FREQ_122_880:
		return "122.88 MHz";
	case ICE_TIME_REF_FREQ_125_000:
		return "125 MHz";
	case ICE_TIME_REF_FREQ_153_600:
		return "153.6 MHz";
	case ICE_TIME_REF_FREQ_156_250:
		return "156.25 MHz";
	case ICE_TIME_REF_FREQ_245_760:
		return "245.76 MHz";
	default:
		return "Unknown";
	}
}

/**
 * ice_clk_src_str - Convert time_ref_src to string
 * @clk_src: Clock source
 *
 * Return: specified clock source converted to its string name
 */
static const char *ice_clk_src_str(enum ice_clk_src clk_src)
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
 * ice_cfg_cgu_pll_e82x - Configure the Clock Generation Unit
 * @hw: pointer to the HW struct
 * @clk_freq: Clock frequency to program
 * @clk_src: Clock source to select (TIME_REF, or TCXO)
 *
 * Configure the Clock Generation Unit with the desired clock frequency and
 * time reference, enabling the PLL which drives the PTP hardware clock.
 *
 * Return:
 * * %0       - success
 * * %-EINVAL - input parameters are incorrect
 * * %-EBUSY  - failed to lock TS PLL
 * * %other   - CGU read/write failure
 */
static int ice_cfg_cgu_pll_e82x(struct ice_hw *hw,
				enum ice_time_ref_freq clk_freq,
				enum ice_clk_src clk_src)
{
	union tspll_ro_bwm_lf bwm_lf;
	union nac_cgu_dword19 dw19;
	union nac_cgu_dword22 dw22;
	union nac_cgu_dword24 dw24;
	union nac_cgu_dword9 dw9;
	int err;

	if (clk_freq >= NUM_ICE_TIME_REF_FREQ) {
		dev_warn(ice_hw_to_dev(hw), "Invalid TIME_REF frequency %u\n",
			 clk_freq);
		return -EINVAL;
	}

	if (clk_src >= NUM_ICE_CLK_SRC) {
		dev_warn(ice_hw_to_dev(hw), "Invalid clock source %u\n",
			 clk_src);
		return -EINVAL;
	}

	if (clk_src == ICE_CLK_SRC_TCXO &&
	    clk_freq != ICE_TIME_REF_FREQ_25_000) {
		dev_warn(ice_hw_to_dev(hw),
			 "TCXO only supports 25 MHz frequency\n");
		return -EINVAL;
	}

	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD9, &dw9.val);
	if (err)
		return err;

	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD24, &dw24.val);
	if (err)
		return err;

	err = ice_read_cgu_reg_e82x(hw, TSPLL_RO_BWM_LF, &bwm_lf.val);
	if (err)
		return err;

	/* Log the current clock configuration */
	ice_debug(hw, ICE_DBG_PTP, "Current CGU configuration -- %s, clk_src %s, clk_freq %s, PLL %s\n",
		  str_enabled_disabled(dw24.ts_pll_enable),
		  ice_clk_src_str(dw24.time_ref_sel),
		  ice_clk_freq_str(dw9.time_ref_freq_sel),
		  bwm_lf.plllock_true_lock_cri ? "locked" : "unlocked");

	/* Disable the PLL before changing the clock source or frequency */
	if (dw24.ts_pll_enable) {
		dw24.ts_pll_enable = 0;

		err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD24, dw24.val);
		if (err)
			return err;
	}

	/* Set the frequency */
	dw9.time_ref_freq_sel = clk_freq;
	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD9, dw9.val);
	if (err)
		return err;

	/* Configure the TS PLL feedback divisor */
	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD19, &dw19.val);
	if (err)
		return err;

	dw19.tspll_fbdiv_intgr = e822_cgu_params[clk_freq].feedback_div;
	dw19.tspll_ndivratio = 1;

	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD19, dw19.val);
	if (err)
		return err;

	/* Configure the TS PLL post divisor */
	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD22, &dw22.val);
	if (err)
		return err;

	dw22.time1588clk_div = e822_cgu_params[clk_freq].post_pll_div;
	dw22.time1588clk_sel_div2 = 0;

	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD22, dw22.val);
	if (err)
		return err;

	/* Configure the TS PLL pre divisor and clock source */
	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD24, &dw24.val);
	if (err)
		return err;

	dw24.ref1588_ck_div = e822_cgu_params[clk_freq].refclk_pre_div;
	dw24.tspll_fbdiv_frac = e822_cgu_params[clk_freq].frac_n_div;
	dw24.time_ref_sel = clk_src;

	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD24, dw24.val);
	if (err)
		return err;

	/* Finally, enable the PLL */
	dw24.ts_pll_enable = 1;

	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD24, dw24.val);
	if (err)
		return err;

	/* Wait to verify if the PLL locks */
	usleep_range(1000, 5000);

	err = ice_read_cgu_reg_e82x(hw, TSPLL_RO_BWM_LF, &bwm_lf.val);
	if (err)
		return err;

	if (!bwm_lf.plllock_true_lock_cri) {
		dev_warn(ice_hw_to_dev(hw), "CGU PLL failed to lock\n");
		return -EBUSY;
	}

	/* Log the current clock configuration */
	ice_debug(hw, ICE_DBG_PTP, "New CGU configuration -- %s, clk_src %s, clk_freq %s, PLL %s\n",
		  str_enabled_disabled(dw24.ts_pll_enable),
		  ice_clk_src_str(dw24.time_ref_sel),
		  ice_clk_freq_str(dw9.time_ref_freq_sel),
		  bwm_lf.plllock_true_lock_cri ? "locked" : "unlocked");

	return 0;
}

/**
 * ice_cfg_cgu_pll_dis_sticky_bits_e82x - disable TS PLL sticky bits
 * @hw: pointer to the HW struct
 *
 * Configure the Clock Generation Unit TS PLL sticky bits so they don't latch on
 * losing TS PLL lock, but always show current state.
 *
 * Return: 0 on success, other error codes when failed to read/write CGU
 */
static int ice_cfg_cgu_pll_dis_sticky_bits_e82x(struct ice_hw *hw)
{
	union tspll_cntr_bist_settings cntr_bist;
	int err;

	err = ice_read_cgu_reg_e82x(hw, TSPLL_CNTR_BIST_SETTINGS,
				    &cntr_bist.val);
	if (err)
		return err;

	/* Disable sticky lock detection so lock err reported is accurate */
	cntr_bist.i_plllock_sel_0 = 0;
	cntr_bist.i_plllock_sel_1 = 0;

	return ice_write_cgu_reg_e82x(hw, TSPLL_CNTR_BIST_SETTINGS,
				      cntr_bist.val);
}

/**
 * ice_cfg_cgu_pll_e825c - Configure the Clock Generation Unit for E825-C
 * @hw: pointer to the HW struct
 * @clk_freq: Clock frequency to program
 * @clk_src: Clock source to select (TIME_REF, or TCXO)
 *
 * Configure the Clock Generation Unit with the desired clock frequency and
 * time reference, enabling the PLL which drives the PTP hardware clock.
 *
 * Return:
 * * %0       - success
 * * %-EINVAL - input parameters are incorrect
 * * %-EBUSY  - failed to lock TS PLL
 * * %other   - CGU read/write failure
 */
static int ice_cfg_cgu_pll_e825c(struct ice_hw *hw,
				 enum ice_time_ref_freq clk_freq,
				 enum ice_clk_src clk_src)
{
	union tspll_ro_lock_e825c ro_lock;
	union nac_cgu_dword16_e825c dw16;
	union nac_cgu_dword23_e825c dw23;
	union nac_cgu_dword19 dw19;
	union nac_cgu_dword22 dw22;
	union nac_cgu_dword24 dw24;
	union nac_cgu_dword9 dw9;
	int err;

	if (clk_freq >= NUM_ICE_TIME_REF_FREQ) {
		dev_warn(ice_hw_to_dev(hw), "Invalid TIME_REF frequency %u\n",
			 clk_freq);
		return -EINVAL;
	}

	if (clk_src >= NUM_ICE_CLK_SRC) {
		dev_warn(ice_hw_to_dev(hw), "Invalid clock source %u\n",
			 clk_src);
		return -EINVAL;
	}

	if (clk_src == ICE_CLK_SRC_TCXO &&
	    clk_freq != ICE_TIME_REF_FREQ_156_250) {
		dev_warn(ice_hw_to_dev(hw),
			 "TCXO only supports 156.25 MHz frequency\n");
		return -EINVAL;
	}

	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD9, &dw9.val);
	if (err)
		return err;

	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD24, &dw24.val);
	if (err)
		return err;

	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD16_E825C, &dw16.val);
	if (err)
		return err;

	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD23_E825C, &dw23.val);
	if (err)
		return err;

	err = ice_read_cgu_reg_e82x(hw, TSPLL_RO_LOCK_E825C, &ro_lock.val);
	if (err)
		return err;

	/* Log the current clock configuration */
	ice_debug(hw, ICE_DBG_PTP, "Current CGU configuration -- %s, clk_src %s, clk_freq %s, PLL %s\n",
		  str_enabled_disabled(dw24.ts_pll_enable),
		  ice_clk_src_str(dw23.time_ref_sel),
		  ice_clk_freq_str(dw9.time_ref_freq_sel),
		  ro_lock.plllock_true_lock_cri ? "locked" : "unlocked");

	/* Disable the PLL before changing the clock source or frequency */
	if (dw23.ts_pll_enable) {
		dw23.ts_pll_enable = 0;

		err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD23_E825C,
					     dw23.val);
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
	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD9, dw9.val);
	if (err)
		return err;

	/* Choose the referenced frequency */
	dw16.tspll_ck_refclkfreq =
	e825c_cgu_params[clk_freq].tspll_ck_refclkfreq;
	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD16_E825C, dw16.val);
	if (err)
		return err;

	/* Configure the TS PLL feedback divisor */
	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD19, &dw19.val);
	if (err)
		return err;

	dw19.tspll_fbdiv_intgr =
		e825c_cgu_params[clk_freq].tspll_fbdiv_intgr;
	dw19.tspll_ndivratio =
		e825c_cgu_params[clk_freq].tspll_ndivratio;

	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD19, dw19.val);
	if (err)
		return err;

	/* Configure the TS PLL post divisor */
	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD22, &dw22.val);
	if (err)
		return err;

	/* These two are constant for E825C */
	dw22.time1588clk_div = 5;
	dw22.time1588clk_sel_div2 = 0;

	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD22, dw22.val);
	if (err)
		return err;

	/* Configure the TS PLL pre divisor and clock source */
	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD23_E825C, &dw23.val);
	if (err)
		return err;

	dw23.ref1588_ck_div =
		e825c_cgu_params[clk_freq].ref1588_ck_div;
	dw23.time_ref_sel = clk_src;

	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD23_E825C, dw23.val);
	if (err)
		return err;

	dw24.tspll_fbdiv_frac =
		e825c_cgu_params[clk_freq].tspll_fbdiv_frac;

	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD24, dw24.val);
	if (err)
		return err;

	/* Finally, enable the PLL */
	dw23.ts_pll_enable = 1;

	err = ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD23_E825C, dw23.val);
	if (err)
		return err;

	/* Wait to verify if the PLL locks */
	usleep_range(1000, 5000);

	err = ice_read_cgu_reg_e82x(hw, TSPLL_RO_LOCK_E825C, &ro_lock.val);
	if (err)
		return err;

	if (!ro_lock.plllock_true_lock_cri) {
		dev_warn(ice_hw_to_dev(hw), "CGU PLL failed to lock\n");
		return -EBUSY;
	}

	/* Log the current clock configuration */
	ice_debug(hw, ICE_DBG_PTP, "New CGU configuration -- %s, clk_src %s, clk_freq %s, PLL %s\n",
		  str_enabled_disabled(dw24.ts_pll_enable),
		  ice_clk_src_str(dw23.time_ref_sel),
		  ice_clk_freq_str(dw9.time_ref_freq_sel),
		  ro_lock.plllock_true_lock_cri ? "locked" : "unlocked");

	return 0;
}

/**
 * ice_cfg_cgu_pll_dis_sticky_bits_e825c - disable TS PLL sticky bits for E825-C
 * @hw: pointer to the HW struct
 *
 * Configure the Clock Generation Unit TS PLL sticky bits so they don't latch on
 * losing TS PLL lock, but always show current state.
 *
 * Return: 0 on success, other error codes when failed to read/write CGU
 */
static int ice_cfg_cgu_pll_dis_sticky_bits_e825c(struct ice_hw *hw)
{
	union tspll_bw_tdc_e825c bw_tdc;
	int err;

	err = ice_read_cgu_reg_e82x(hw, TSPLL_BW_TDC_E825C, &bw_tdc.val);
	if (err)
		return err;

	bw_tdc.i_plllock_sel_1_0 = 0;

	return ice_write_cgu_reg_e82x(hw, TSPLL_BW_TDC_E825C, bw_tdc.val);
}

#define ICE_ONE_PPS_OUT_AMP_MAX 3

/**
 * ice_cgu_cfg_pps_out - Configure 1PPS output from CGU
 * @hw: pointer to the HW struct
 * @enable: true to enable 1PPS output, false to disable it
 *
 * Return: 0 on success, other negative error code when CGU read/write failed.
 */
int ice_cgu_cfg_pps_out(struct ice_hw *hw, bool enable)
{
	union nac_cgu_dword9 dw9;
	int err;

	err = ice_read_cgu_reg_e82x(hw, NAC_CGU_DWORD9, &dw9.val);
	if (err)
		return err;

	dw9.one_pps_out_en = enable;
	dw9.one_pps_out_amp = enable * ICE_ONE_PPS_OUT_AMP_MAX;
	return ice_write_cgu_reg_e82x(hw, NAC_CGU_DWORD9, dw9.val);
}

/**
 * ice_init_cgu_e82x - Initialize CGU with settings from firmware
 * @hw: pointer to the HW structure
 *
 * Initialize the Clock Generation Unit of the E822 device.
 *
 * Return: 0 on success, other error codes when failed to read/write/cfg CGU
 */
int ice_init_cgu_e82x(struct ice_hw *hw)
{
	struct ice_ts_func_info *ts_info = &hw->func_caps.ts_func_info;
	int err;

	/* Disable sticky lock detection so lock err reported is accurate */
	if (hw->mac_type == ICE_MAC_GENERIC_3K_E825)
		err = ice_cfg_cgu_pll_dis_sticky_bits_e825c(hw);
	else
		err = ice_cfg_cgu_pll_dis_sticky_bits_e82x(hw);
	if (err)
		return err;

	/* Configure the CGU PLL using the parameters from the function
	 * capabilities.
	 */
	if (hw->mac_type == ICE_MAC_GENERIC_3K_E825)
		err = ice_cfg_cgu_pll_e825c(hw, ts_info->time_ref,
					    (enum ice_clk_src)ts_info->clk_src);
	else
		err = ice_cfg_cgu_pll_e82x(hw, ts_info->time_ref,
					   (enum ice_clk_src)ts_info->clk_src);

	return err;
}
