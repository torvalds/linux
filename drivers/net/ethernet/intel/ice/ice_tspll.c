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
 * ice_tspll_default_freq - Return default frequency for a MAC type
 * @mac_type: MAC type
 *
 * Return: default TSPLL frequency for a correct MAC type, -ERANGE otherwise.
 */
static enum ice_tspll_freq ice_tspll_default_freq(enum ice_mac_type mac_type)
{
	switch (mac_type) {
	case ICE_MAC_GENERIC:
		return ICE_TSPLL_FREQ_25_000;
	case ICE_MAC_GENERIC_3K_E825:
		return ICE_TSPLL_FREQ_156_250;
	default:
		return -ERANGE;
	}
}

/**
 * ice_tspll_check_params - Check if TSPLL params are correct
 * @hw: Pointer to the HW struct
 * @clk_freq: Clock frequency to program
 * @clk_src: Clock source to select (TIME_REF or TCXO)
 *
 * Return: true if TSPLL params are correct, false otherwise.
 */
static bool ice_tspll_check_params(struct ice_hw *hw,
				   enum ice_tspll_freq clk_freq,
				   enum ice_clk_src clk_src)
{
	if (clk_freq >= NUM_ICE_TSPLL_FREQ) {
		dev_warn(ice_hw_to_dev(hw), "Invalid TSPLL frequency %u\n",
			 clk_freq);
		return false;
	}

	if (clk_src >= NUM_ICE_CLK_SRC) {
		dev_warn(ice_hw_to_dev(hw), "Invalid clock source %u\n",
			 clk_src);
		return false;
	}

	if ((hw->mac_type == ICE_MAC_GENERIC_3K_E825 ||
	     clk_src == ICE_CLK_SRC_TCXO) &&
	    clk_freq != ice_tspll_default_freq(hw->mac_type)) {
		dev_warn(ice_hw_to_dev(hw), "Unsupported frequency for this clock source\n");
		return false;
	}

	return true;
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
 * ice_tspll_log_cfg - Log current/new TSPLL configuration
 * @hw: Pointer to the HW struct
 * @enable: CGU enabled/disabled
 * @clk_src: Current clock source
 * @tspll_freq: Current clock frequency
 * @lock: CGU lock status
 * @new_cfg: true if this is a new config
 */
static void ice_tspll_log_cfg(struct ice_hw *hw, bool enable, u8 clk_src,
			      u8 tspll_freq, bool lock, bool new_cfg)
{
	dev_dbg(ice_hw_to_dev(hw),
		"%s TSPLL configuration -- %s, src %s, freq %s, PLL %s\n",
		new_cfg ? "New" : "Current", str_enabled_disabled(enable),
		ice_tspll_clk_src_str((enum ice_clk_src)clk_src),
		ice_tspll_clk_freq_str((enum ice_tspll_freq)tspll_freq),
		lock ? "locked" : "unlocked");
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
	u32 val, r9, r24;
	int err;

	err = ice_read_cgu_reg(hw, ICE_CGU_R9, &r9);
	if (err)
		return err;

	err = ice_read_cgu_reg(hw, ICE_CGU_R24, &r24);
	if (err)
		return err;

	err = ice_read_cgu_reg(hw, ICE_CGU_RO_BWM_LF, &val);
	if (err)
		return err;

	ice_tspll_log_cfg(hw, !!FIELD_GET(ICE_CGU_R23_R24_TSPLL_ENABLE, r24),
			  FIELD_GET(ICE_CGU_R23_R24_TIME_REF_SEL, r24),
			  FIELD_GET(ICE_CGU_R9_TIME_REF_FREQ_SEL, r9),
			  !!FIELD_GET(ICE_CGU_RO_BWM_LF_TRUE_LOCK, val),
			  false);

	/* Disable the PLL before changing the clock source or frequency */
	if (FIELD_GET(ICE_CGU_R23_R24_TSPLL_ENABLE, r24)) {
		r24 &= ~ICE_CGU_R23_R24_TSPLL_ENABLE;

		err = ice_write_cgu_reg(hw, ICE_CGU_R24, r24);
		if (err)
			return err;
	}

	/* Set the frequency */
	r9 &= ~ICE_CGU_R9_TIME_REF_FREQ_SEL;
	r9 |= FIELD_PREP(ICE_CGU_R9_TIME_REF_FREQ_SEL, clk_freq);
	err = ice_write_cgu_reg(hw, ICE_CGU_R9, r9);
	if (err)
		return err;

	/* Configure the TSPLL feedback divisor */
	err = ice_read_cgu_reg(hw, ICE_CGU_R19, &val);
	if (err)
		return err;

	val &= ~(ICE_CGU_R19_TSPLL_FBDIV_INTGR_E82X | ICE_CGU_R19_TSPLL_NDIVRATIO);
	val |= FIELD_PREP(ICE_CGU_R19_TSPLL_FBDIV_INTGR_E82X,
			  e82x_tspll_params[clk_freq].feedback_div);
	val |= FIELD_PREP(ICE_CGU_R19_TSPLL_NDIVRATIO, 1);

	err = ice_write_cgu_reg(hw, ICE_CGU_R19, val);
	if (err)
		return err;

	/* Configure the TSPLL post divisor */
	err = ice_read_cgu_reg(hw, ICE_CGU_R22, &val);
	if (err)
		return err;

	val &= ~(ICE_CGU_R22_TIME1588CLK_DIV |
		 ICE_CGU_R22_TIME1588CLK_DIV2);
	val |= FIELD_PREP(ICE_CGU_R22_TIME1588CLK_DIV,
			  e82x_tspll_params[clk_freq].post_pll_div);

	err = ice_write_cgu_reg(hw, ICE_CGU_R22, val);
	if (err)
		return err;

	/* Configure the TSPLL pre divisor and clock source */
	err = ice_read_cgu_reg(hw, ICE_CGU_R24, &r24);
	if (err)
		return err;

	r24 &= ~(ICE_CGU_R23_R24_REF1588_CK_DIV | ICE_CGU_R24_FBDIV_FRAC |
		 ICE_CGU_R23_R24_TIME_REF_SEL);
	r24 |= FIELD_PREP(ICE_CGU_R23_R24_REF1588_CK_DIV,
			  e82x_tspll_params[clk_freq].refclk_pre_div);
	r24 |= FIELD_PREP(ICE_CGU_R24_FBDIV_FRAC,
			  e82x_tspll_params[clk_freq].frac_n_div);
	r24 |= FIELD_PREP(ICE_CGU_R23_R24_TIME_REF_SEL, clk_src);

	err = ice_write_cgu_reg(hw, ICE_CGU_R24, r24);
	if (err)
		return err;

	/* Wait to ensure everything is stable */
	usleep_range(10, 20);

	/* Finally, enable the PLL */
	r24 |= ICE_CGU_R23_R24_TSPLL_ENABLE;

	err = ice_write_cgu_reg(hw, ICE_CGU_R24, r24);
	if (err)
		return err;

	/* Wait at least 1 ms to verify if the PLL locks */
	usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);

	err = ice_read_cgu_reg(hw, ICE_CGU_RO_BWM_LF, &val);
	if (err)
		return err;

	if (!(val & ICE_CGU_RO_BWM_LF_TRUE_LOCK)) {
		dev_warn(ice_hw_to_dev(hw), "CGU PLL failed to lock\n");
		return -EBUSY;
	}

	err = ice_read_cgu_reg(hw, ICE_CGU_R9, &r9);
	if (err)
		return err;
	err = ice_read_cgu_reg(hw, ICE_CGU_R24, &r24);
	if (err)
		return err;

	ice_tspll_log_cfg(hw, !!FIELD_GET(ICE_CGU_R23_R24_TSPLL_ENABLE, r24),
			  FIELD_GET(ICE_CGU_R23_R24_TIME_REF_SEL, r24),
			  FIELD_GET(ICE_CGU_R9_TIME_REF_FREQ_SEL, r9),
			  true, true);

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
	u32 val;
	int err;

	err = ice_read_cgu_reg(hw, ICE_CGU_CNTR_BIST, &val);
	if (err)
		return err;

	val &= ~(ICE_CGU_CNTR_BIST_PLLLOCK_SEL_0 |
		 ICE_CGU_CNTR_BIST_PLLLOCK_SEL_1);

	return ice_write_cgu_reg(hw, ICE_CGU_CNTR_BIST, val);
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
	u32 val, r9, r23;
	int err;

	err = ice_read_cgu_reg(hw, ICE_CGU_R9, &r9);
	if (err)
		return err;

	err = ice_read_cgu_reg(hw, ICE_CGU_R23, &r23);
	if (err)
		return err;

	err = ice_read_cgu_reg(hw, ICE_CGU_RO_LOCK, &val);
	if (err)
		return err;

	ice_tspll_log_cfg(hw, !!FIELD_GET(ICE_CGU_R23_R24_TSPLL_ENABLE, r23),
			  FIELD_GET(ICE_CGU_R23_R24_TIME_REF_SEL, r23),
			  FIELD_GET(ICE_CGU_R9_TIME_REF_FREQ_SEL, r9),
			  !!FIELD_GET(ICE_CGU_RO_LOCK_TRUE_LOCK, val),
			  false);

	/* Disable the PLL before changing the clock source or frequency */
	if (FIELD_GET(ICE_CGU_R23_R24_TSPLL_ENABLE, r23)) {
		r23 &= ~ICE_CGU_R23_R24_TSPLL_ENABLE;

		err = ice_write_cgu_reg(hw, ICE_CGU_R23, r23);
		if (err)
			return err;
	}

	if (FIELD_GET(ICE_CGU_R9_TIME_SYNC_EN, r9)) {
		r9 &= ~ICE_CGU_R9_TIME_SYNC_EN;

		err = ice_write_cgu_reg(hw, ICE_CGU_R9, r9);
		if (err)
			return err;
	}

	/* Set the frequency and enable the correct receiver */
	r9 &= ~(ICE_CGU_R9_TIME_REF_FREQ_SEL | ICE_CGU_R9_CLK_EREF0_EN |
		ICE_CGU_R9_TIME_REF_EN);
	r9 |= FIELD_PREP(ICE_CGU_R9_TIME_REF_FREQ_SEL, clk_freq);
	if (clk_src == ICE_CLK_SRC_TCXO)
		r9 |= ICE_CGU_R9_CLK_EREF0_EN;
	else
		r9 |= ICE_CGU_R9_TIME_REF_EN;
	r9 |= ICE_CGU_R9_TIME_SYNC_EN;
	err = ice_write_cgu_reg(hw, ICE_CGU_R9, r9);
	if (err)
		return err;

	/* Choose the referenced frequency */
	err = ice_read_cgu_reg(hw, ICE_CGU_R16, &val);
	if (err)
		return err;
	val &= ~ICE_CGU_R16_TSPLL_CK_REFCLKFREQ;
	val |= FIELD_PREP(ICE_CGU_R16_TSPLL_CK_REFCLKFREQ,
			  ICE_TSPLL_CK_REFCLKFREQ_E825);
	err = ice_write_cgu_reg(hw, ICE_CGU_R16, val);
	if (err)
		return err;

	/* Configure the TSPLL feedback divisor */
	err = ice_read_cgu_reg(hw, ICE_CGU_R19, &val);
	if (err)
		return err;

	val &= ~(ICE_CGU_R19_TSPLL_FBDIV_INTGR_E825 |
		 ICE_CGU_R19_TSPLL_NDIVRATIO);
	val |= FIELD_PREP(ICE_CGU_R19_TSPLL_FBDIV_INTGR_E825,
			  ICE_TSPLL_FBDIV_INTGR_E825);
	val |= FIELD_PREP(ICE_CGU_R19_TSPLL_NDIVRATIO,
			  ICE_TSPLL_NDIVRATIO_E825);

	err = ice_write_cgu_reg(hw, ICE_CGU_R19, val);
	if (err)
		return err;

	/* Configure the TSPLL post divisor, these two are constant */
	err = ice_read_cgu_reg(hw, ICE_CGU_R22, &val);
	if (err)
		return err;

	val &= ~(ICE_CGU_R22_TIME1588CLK_DIV |
		 ICE_CGU_R22_TIME1588CLK_DIV2);
	val |= FIELD_PREP(ICE_CGU_R22_TIME1588CLK_DIV, 5);

	err = ice_write_cgu_reg(hw, ICE_CGU_R22, val);
	if (err)
		return err;

	/* Configure the TSPLL pre divisor (constant) and clock source */
	err = ice_read_cgu_reg(hw, ICE_CGU_R23, &r23);
	if (err)
		return err;

	r23 &= ~(ICE_CGU_R23_R24_REF1588_CK_DIV | ICE_CGU_R23_R24_TIME_REF_SEL);
	r23 |= FIELD_PREP(ICE_CGU_R23_R24_TIME_REF_SEL, clk_src);

	err = ice_write_cgu_reg(hw, ICE_CGU_R23, r23);
	if (err)
		return err;

	/* Clear the R24 register. */
	err = ice_write_cgu_reg(hw, ICE_CGU_R24, 0);
	if (err)
		return err;

	/* Wait to ensure everything is stable */
	usleep_range(10, 20);

	/* Finally, enable the PLL */
	r23 |= ICE_CGU_R23_R24_TSPLL_ENABLE;

	err = ice_write_cgu_reg(hw, ICE_CGU_R23, r23);
	if (err)
		return err;

	/* Wait at least 1 ms to verify if the PLL locks */
	usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);

	err = ice_read_cgu_reg(hw, ICE_CGU_RO_LOCK, &val);
	if (err)
		return err;

	if (!(val & ICE_CGU_RO_LOCK_TRUE_LOCK)) {
		dev_warn(ice_hw_to_dev(hw), "CGU PLL failed to lock\n");
		return -EBUSY;
	}

	err = ice_read_cgu_reg(hw, ICE_CGU_R9, &r9);
	if (err)
		return err;
	err = ice_read_cgu_reg(hw, ICE_CGU_R23, &r23);
	if (err)
		return err;

	ice_tspll_log_cfg(hw, !!FIELD_GET(ICE_CGU_R23_R24_TSPLL_ENABLE, r23),
			  FIELD_GET(ICE_CGU_R23_R24_TIME_REF_SEL, r23),
			  FIELD_GET(ICE_CGU_R9_TIME_REF_FREQ_SEL, r9),
			  true, true);

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
	u32 val;
	int err;

	err = ice_read_cgu_reg(hw, ICE_CGU_BW_TDC, &val);
	if (err)
		return err;

	val &= ~ICE_CGU_BW_TDC_PLLLOCK_SEL;

	return ice_write_cgu_reg(hw, ICE_CGU_BW_TDC, val);
}

/**
 * ice_tspll_cfg_pps_out_e825c - Enable/disable 1PPS output and set amplitude
 * @hw: pointer to the HW struct
 * @enable: true to enable 1PPS output, false to disable it
 *
 * Return: 0 on success, other negative error code when CGU read/write failed.
 */
int ice_tspll_cfg_pps_out_e825c(struct ice_hw *hw, bool enable)
{
	u32 val;
	int err;

	err = ice_read_cgu_reg(hw, ICE_CGU_R9, &val);
	if (err)
		return err;

	val &= ~(ICE_CGU_R9_ONE_PPS_OUT_EN | ICE_CGU_R9_ONE_PPS_OUT_AMP);
	val |= FIELD_PREP(ICE_CGU_R9_ONE_PPS_OUT_EN, enable) |
	       ICE_CGU_R9_ONE_PPS_OUT_AMP;

	return ice_write_cgu_reg(hw, ICE_CGU_R9, val);
}

/**
 * ice_tspll_cfg - Configure the Clock Generation Unit TSPLL
 * @hw: Pointer to the HW struct
 * @clk_freq: Clock frequency to program
 * @clk_src: Clock source to select (TIME_REF, or TCXO)
 *
 * Configure the Clock Generation Unit with the desired clock frequency and
 * time reference, enabling the TSPLL which drives the PTP hardware clock.
 *
 * Return: 0 on success, -ERANGE on unsupported MAC type, other negative error
 *         codes when failed to configure CGU.
 */
static int ice_tspll_cfg(struct ice_hw *hw, enum ice_tspll_freq clk_freq,
			 enum ice_clk_src clk_src)
{
	switch (hw->mac_type) {
	case ICE_MAC_GENERIC:
		return ice_tspll_cfg_e82x(hw, clk_freq, clk_src);
	case ICE_MAC_GENERIC_3K_E825:
		return ice_tspll_cfg_e825c(hw, clk_freq, clk_src);
	default:
		return -ERANGE;
	}
}

/**
 * ice_tspll_dis_sticky_bits - disable TSPLL sticky bits
 * @hw: Pointer to the HW struct
 *
 * Configure the Clock Generation Unit TSPLL sticky bits so they don't latch on
 * losing TSPLL lock, but always show current state.
 *
 * Return: 0 on success, -ERANGE on unsupported MAC type.
 */
static int ice_tspll_dis_sticky_bits(struct ice_hw *hw)
{
	switch (hw->mac_type) {
	case ICE_MAC_GENERIC:
		return ice_tspll_dis_sticky_bits_e82x(hw);
	case ICE_MAC_GENERIC_3K_E825:
		return ice_tspll_dis_sticky_bits_e825c(hw);
	default:
		return -ERANGE;
	}
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
	enum ice_tspll_freq tspll_freq;
	enum ice_clk_src clk_src;
	int err;

	/* Only E822, E823 and E825 products support TSPLL */
	if (hw->mac_type != ICE_MAC_GENERIC &&
	    hw->mac_type != ICE_MAC_GENERIC_3K_E825)
		return 0;

	tspll_freq = (enum ice_tspll_freq)ts_info->time_ref;
	clk_src = (enum ice_clk_src)ts_info->clk_src;
	if (!ice_tspll_check_params(hw, tspll_freq, clk_src))
		return -EINVAL;

	/* Disable sticky lock detection so lock status reported is accurate */
	err = ice_tspll_dis_sticky_bits(hw);
	if (err)
		return err;

	/* Configure the TSPLL using the parameters from the function
	 * capabilities.
	 */
	err = ice_tspll_cfg(hw, tspll_freq, clk_src);
	if (err) {
		dev_warn(ice_hw_to_dev(hw), "Failed to lock TSPLL to predefined frequency. Retrying with fallback frequency.\n");

		/* Try to lock to internal TCXO as a fallback. */
		tspll_freq = ice_tspll_default_freq(hw->mac_type);
		clk_src = ICE_CLK_SRC_TCXO;
		err = ice_tspll_cfg(hw, tspll_freq, clk_src);
		if (err)
			dev_warn(ice_hw_to_dev(hw), "Failed to lock TSPLL to fallback frequency.\n");
	}

	return err;
}

/**
 * ice_tspll_bypass_mux_active_e825c - check if the given port is set active
 * @hw: Pointer to the HW struct
 * @port: Number of the port
 * @active: Output flag showing if port is active
 * @output: Output pin, we have two in E825C
 *
 * Check if given port is selected as recovered clock source for given output.
 *
 * Return:
 * * 0 - success
 * * negative - error
 */
int ice_tspll_bypass_mux_active_e825c(struct ice_hw *hw, u8 port, bool *active,
				      enum ice_synce_clk output)
{
	u8 active_clk;
	u32 val;
	int err;

	switch (output) {
	case ICE_SYNCE_CLK0:
		err = ice_read_cgu_reg(hw, ICE_CGU_R10, &val);
		if (err)
			return err;
		active_clk = FIELD_GET(ICE_CGU_R10_SYNCE_S_REF_CLK, val);
		break;
	case ICE_SYNCE_CLK1:
		err = ice_read_cgu_reg(hw, ICE_CGU_R11, &val);
		if (err)
			return err;
		active_clk = FIELD_GET(ICE_CGU_R11_SYNCE_S_BYP_CLK, val);
		break;
	default:
		return -EINVAL;
	}

	if (active_clk == port % hw->ptp.ports_per_phy +
			  ICE_CGU_BYPASS_MUX_OFFSET_E825C)
		*active = true;
	else
		*active = false;

	return 0;
}

/**
 * ice_tspll_cfg_bypass_mux_e825c - configure reference clock mux
 * @hw: Pointer to the HW struct
 * @ena: true to enable the reference, false if disable
 * @port_num: Number of the port
 * @output: Output pin, we have two in E825C
 *
 * Set reference clock source and output clock selection.
 *
 * Context: Called under pf->dplls.lock
 * Return:
 * * 0 - success
 * * negative - error
 */
int ice_tspll_cfg_bypass_mux_e825c(struct ice_hw *hw, bool ena, u32 port_num,
				   enum ice_synce_clk output)
{
	u8 first_mux;
	int err;
	u32 r10;

	err = ice_read_cgu_reg(hw, ICE_CGU_R10, &r10);
	if (err)
		return err;

	if (!ena)
		first_mux = ICE_CGU_NET_REF_CLK0;
	else
		first_mux = port_num + ICE_CGU_BYPASS_MUX_OFFSET_E825C;

	r10 &= ~(ICE_CGU_R10_SYNCE_DCK_RST | ICE_CGU_R10_SYNCE_DCK2_RST);

	switch (output) {
	case ICE_SYNCE_CLK0:
		r10 &= ~(ICE_CGU_R10_SYNCE_ETHCLKO_SEL |
			 ICE_CGU_R10_SYNCE_ETHDIV_LOAD |
			 ICE_CGU_R10_SYNCE_S_REF_CLK);
		r10 |= FIELD_PREP(ICE_CGU_R10_SYNCE_S_REF_CLK, first_mux);
		r10 |= FIELD_PREP(ICE_CGU_R10_SYNCE_ETHCLKO_SEL,
				  ICE_CGU_REF_CLK_BYP0_DIV);
		break;
	case ICE_SYNCE_CLK1:
	{
		u32 val;

		err = ice_read_cgu_reg(hw, ICE_CGU_R11, &val);
		if (err)
			return err;
		val &= ~ICE_CGU_R11_SYNCE_S_BYP_CLK;
		val |= FIELD_PREP(ICE_CGU_R11_SYNCE_S_BYP_CLK, first_mux);
		err = ice_write_cgu_reg(hw, ICE_CGU_R11, val);
		if (err)
			return err;
		r10 &= ~(ICE_CGU_R10_SYNCE_CLKODIV_LOAD |
			 ICE_CGU_R10_SYNCE_CLKO_SEL);
		r10 |= FIELD_PREP(ICE_CGU_R10_SYNCE_CLKO_SEL,
				  ICE_CGU_REF_CLK_BYP1_DIV);
		break;
	}
	default:
		return -EINVAL;
	}

	err = ice_write_cgu_reg(hw, ICE_CGU_R10, r10);
	if (err)
		return err;

	return 0;
}

/**
 * ice_tspll_get_div_e825c - get the divider for the given speed
 * @link_speed: link speed of the port
 * @divider: output value, calculated divider
 *
 * Get CGU divider value based on the link speed.
 *
 * Return:
 * * 0 - success
 * * negative - error
 */
static int ice_tspll_get_div_e825c(u16 link_speed, unsigned int *divider)
{
	switch (link_speed) {
	case ICE_AQ_LINK_SPEED_100GB:
	case ICE_AQ_LINK_SPEED_50GB:
	case ICE_AQ_LINK_SPEED_25GB:
		*divider = 10;
		break;
	case ICE_AQ_LINK_SPEED_40GB:
	case ICE_AQ_LINK_SPEED_10GB:
		*divider = 4;
		break;
	case ICE_AQ_LINK_SPEED_5GB:
	case ICE_AQ_LINK_SPEED_2500MB:
	case ICE_AQ_LINK_SPEED_1000MB:
		*divider = 2;
		break;
	case ICE_AQ_LINK_SPEED_100MB:
		*divider = 1;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * ice_tspll_cfg_synce_ethdiv_e825c - set the divider on the mux
 * @hw: Pointer to the HW struct
 * @output: Output pin, we have two in E825C
 *
 * Set the correct CGU divider for RCLKA or RCLKB.
 *
 * Context: Called under pf->dplls.lock
 * Return:
 * * 0 - success
 * * negative - error
 */
int ice_tspll_cfg_synce_ethdiv_e825c(struct ice_hw *hw,
				     enum ice_synce_clk output)
{
	unsigned int divider;
	u16 link_speed;
	u32 val;
	int err;

	link_speed = hw->port_info->phy.link_info.link_speed;
	if (!link_speed)
		return 0;

	err = ice_tspll_get_div_e825c(link_speed, &divider);
	if (err)
		return err;

	err = ice_read_cgu_reg(hw, ICE_CGU_R10, &val);
	if (err)
		return err;

	/* programmable divider value (from 2 to 16) minus 1 for ETHCLKOUT */
	switch (output) {
	case ICE_SYNCE_CLK0:
		val &= ~(ICE_CGU_R10_SYNCE_ETHDIV_M1 |
			 ICE_CGU_R10_SYNCE_ETHDIV_LOAD);
		val |= FIELD_PREP(ICE_CGU_R10_SYNCE_ETHDIV_M1, divider - 1);
		err = ice_write_cgu_reg(hw, ICE_CGU_R10, val);
		if (err)
			return err;
		val |= ICE_CGU_R10_SYNCE_ETHDIV_LOAD;
		break;
	case ICE_SYNCE_CLK1:
		val &= ~(ICE_CGU_R10_SYNCE_CLKODIV_M1 |
			 ICE_CGU_R10_SYNCE_CLKODIV_LOAD);
		val |= FIELD_PREP(ICE_CGU_R10_SYNCE_CLKODIV_M1, divider - 1);
		err = ice_write_cgu_reg(hw, ICE_CGU_R10, val);
		if (err)
			return err;
		val |= ICE_CGU_R10_SYNCE_CLKODIV_LOAD;
		break;
	default:
		return -EINVAL;
	}

	err = ice_write_cgu_reg(hw, ICE_CGU_R10, val);
	if (err)
		return err;

	return 0;
}
