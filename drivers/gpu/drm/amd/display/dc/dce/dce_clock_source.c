/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <linux/slab.h>

#include "dm_services.h"


#include "dc_types.h"
#include "core_types.h"

#include "include/grph_object_id.h"
#include "include/logger_interface.h"

#include "dce_clock_source.h"
#include "clk_mgr.h"

#include "reg_helper.h"

#define REG(reg)\
	(clk_src->regs->reg)

#define CTX \
	clk_src->base.ctx

#define DC_LOGGER_INIT()

#undef FN
#define FN(reg_name, field_name) \
	clk_src->cs_shift->field_name, clk_src->cs_mask->field_name

#define FRACT_FB_DIVIDER_DEC_POINTS_MAX_NUM 6
#define CALC_PLL_CLK_SRC_ERR_TOLERANCE 1
#define MAX_PLL_CALC_ERROR 0xFFFFFFFF

#define NUM_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))

static const struct spread_spectrum_data *get_ss_data_entry(
		struct dce110_clk_src *clk_src,
		enum signal_type signal,
		uint32_t pix_clk_khz)
{

	uint32_t entrys_num;
	uint32_t i;
	struct spread_spectrum_data *ss_parm = NULL;
	struct spread_spectrum_data *ret = NULL;

	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		ss_parm = clk_src->dvi_ss_params;
		entrys_num = clk_src->dvi_ss_params_cnt;
		break;

	case SIGNAL_TYPE_HDMI_TYPE_A:
		ss_parm = clk_src->hdmi_ss_params;
		entrys_num = clk_src->hdmi_ss_params_cnt;
		break;

	case SIGNAL_TYPE_LVDS:
		ss_parm = clk_src->lvds_ss_params;
		entrys_num = clk_src->lvds_ss_params_cnt;
		break;

	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_VIRTUAL:
		ss_parm = clk_src->dp_ss_params;
		entrys_num = clk_src->dp_ss_params_cnt;
		break;

	default:
		ss_parm = NULL;
		entrys_num = 0;
		break;
	}

	if (ss_parm == NULL)
		return ret;

	for (i = 0; i < entrys_num; ++i, ++ss_parm) {
		if (ss_parm->freq_range_khz >= pix_clk_khz) {
			ret = ss_parm;
			break;
		}
	}

	return ret;
}

/**
 * Function: calculate_fb_and_fractional_fb_divider
 *
 * * DESCRIPTION: Calculates feedback and fractional feedback dividers values
 *
 *PARAMETERS:
 * targetPixelClock             Desired frequency in 100 Hz
 * ref_divider                  Reference divider (already known)
 * postDivider                  Post Divider (already known)
 * feedback_divider_param       Pointer where to store
 *					calculated feedback divider value
 * fract_feedback_divider_param Pointer where to store
 *					calculated fract feedback divider value
 *
 *RETURNS:
 * It fills the locations pointed by feedback_divider_param
 *					and fract_feedback_divider_param
 * It returns	- true if feedback divider not 0
 *		- false should never happen)
 */
static bool calculate_fb_and_fractional_fb_divider(
		struct calc_pll_clock_source *calc_pll_cs,
		uint32_t target_pix_clk_100hz,
		uint32_t ref_divider,
		uint32_t post_divider,
		uint32_t *feedback_divider_param,
		uint32_t *fract_feedback_divider_param)
{
	uint64_t feedback_divider;

	feedback_divider =
		(uint64_t)target_pix_clk_100hz * ref_divider * post_divider;
	feedback_divider *= 10;
	/* additional factor, since we divide by 10 afterwards */
	feedback_divider *= (uint64_t)(calc_pll_cs->fract_fb_divider_factor);
	feedback_divider = div_u64(feedback_divider, calc_pll_cs->ref_freq_khz * 10ull);

/*Round to the number of precision
 * The following code replace the old code (ullfeedbackDivider + 5)/10
 * for example if the difference between the number
 * of fractional feedback decimal point and the fractional FB Divider precision
 * is 2 then the equation becomes (ullfeedbackDivider + 5*100) / (10*100))*/

	feedback_divider += 5ULL *
			    calc_pll_cs->fract_fb_divider_precision_factor;
	feedback_divider =
		div_u64(feedback_divider,
			calc_pll_cs->fract_fb_divider_precision_factor * 10);
	feedback_divider *= (uint64_t)
			(calc_pll_cs->fract_fb_divider_precision_factor);

	*feedback_divider_param =
		div_u64_rem(
			feedback_divider,
			calc_pll_cs->fract_fb_divider_factor,
			fract_feedback_divider_param);

	if (*feedback_divider_param != 0)
		return true;
	return false;
}

/**
*calc_fb_divider_checking_tolerance
*
*DESCRIPTION: Calculates Feedback and Fractional Feedback divider values
*		for passed Reference and Post divider, checking for tolerance.
*PARAMETERS:
* pll_settings		Pointer to structure
* ref_divider		Reference divider (already known)
* postDivider		Post Divider (already known)
* tolerance		Tolerance for Calculated Pixel Clock to be within
*
*RETURNS:
* It fills the PLLSettings structure with PLL Dividers values
* if calculated values are within required tolerance
* It returns	- true if error is within tolerance
*		- false if error is not within tolerance
*/
static bool calc_fb_divider_checking_tolerance(
		struct calc_pll_clock_source *calc_pll_cs,
		struct pll_settings *pll_settings,
		uint32_t ref_divider,
		uint32_t post_divider,
		uint32_t tolerance)
{
	uint32_t feedback_divider;
	uint32_t fract_feedback_divider;
	uint32_t actual_calculated_clock_100hz;
	uint32_t abs_err;
	uint64_t actual_calc_clk_100hz;

	calculate_fb_and_fractional_fb_divider(
			calc_pll_cs,
			pll_settings->adjusted_pix_clk_100hz,
			ref_divider,
			post_divider,
			&feedback_divider,
			&fract_feedback_divider);

	/*Actual calculated value*/
	actual_calc_clk_100hz = (uint64_t)feedback_divider *
					calc_pll_cs->fract_fb_divider_factor +
							fract_feedback_divider;
	actual_calc_clk_100hz *= calc_pll_cs->ref_freq_khz * 10;
	actual_calc_clk_100hz =
		div_u64(actual_calc_clk_100hz,
			ref_divider * post_divider *
				calc_pll_cs->fract_fb_divider_factor);

	actual_calculated_clock_100hz = (uint32_t)(actual_calc_clk_100hz);

	abs_err = (actual_calculated_clock_100hz >
					pll_settings->adjusted_pix_clk_100hz)
			? actual_calculated_clock_100hz -
					pll_settings->adjusted_pix_clk_100hz
			: pll_settings->adjusted_pix_clk_100hz -
						actual_calculated_clock_100hz;

	if (abs_err <= tolerance) {
		/*found good values*/
		pll_settings->reference_freq = calc_pll_cs->ref_freq_khz;
		pll_settings->reference_divider = ref_divider;
		pll_settings->feedback_divider = feedback_divider;
		pll_settings->fract_feedback_divider = fract_feedback_divider;
		pll_settings->pix_clk_post_divider = post_divider;
		pll_settings->calculated_pix_clk_100hz =
			actual_calculated_clock_100hz;
		pll_settings->vco_freq =
			actual_calculated_clock_100hz * post_divider / 10;
		return true;
	}
	return false;
}

static bool calc_pll_dividers_in_range(
		struct calc_pll_clock_source *calc_pll_cs,
		struct pll_settings *pll_settings,
		uint32_t min_ref_divider,
		uint32_t max_ref_divider,
		uint32_t min_post_divider,
		uint32_t max_post_divider,
		uint32_t err_tolerance)
{
	uint32_t ref_divider;
	uint32_t post_divider;
	uint32_t tolerance;

/* This is err_tolerance / 10000 = 0.0025 - acceptable error of 0.25%
 * This is errorTolerance / 10000 = 0.0001 - acceptable error of 0.01%*/
	tolerance = (pll_settings->adjusted_pix_clk_100hz * err_tolerance) /
									100000;
	if (tolerance < CALC_PLL_CLK_SRC_ERR_TOLERANCE)
		tolerance = CALC_PLL_CLK_SRC_ERR_TOLERANCE;

	for (
			post_divider = max_post_divider;
			post_divider >= min_post_divider;
			--post_divider) {
		for (
				ref_divider = min_ref_divider;
				ref_divider <= max_ref_divider;
				++ref_divider) {
			if (calc_fb_divider_checking_tolerance(
					calc_pll_cs,
					pll_settings,
					ref_divider,
					post_divider,
					tolerance)) {
				return true;
			}
		}
	}

	return false;
}

static uint32_t calculate_pixel_clock_pll_dividers(
		struct calc_pll_clock_source *calc_pll_cs,
		struct pll_settings *pll_settings)
{
	uint32_t err_tolerance;
	uint32_t min_post_divider;
	uint32_t max_post_divider;
	uint32_t min_ref_divider;
	uint32_t max_ref_divider;

	if (pll_settings->adjusted_pix_clk_100hz == 0) {
		DC_LOG_ERROR(
			"%s Bad requested pixel clock", __func__);
		return MAX_PLL_CALC_ERROR;
	}

/* 1) Find Post divider ranges */
	if (pll_settings->pix_clk_post_divider) {
		min_post_divider = pll_settings->pix_clk_post_divider;
		max_post_divider = pll_settings->pix_clk_post_divider;
	} else {
		min_post_divider = calc_pll_cs->min_pix_clock_pll_post_divider;
		if (min_post_divider * pll_settings->adjusted_pix_clk_100hz <
						calc_pll_cs->min_vco_khz * 10) {
			min_post_divider = calc_pll_cs->min_vco_khz * 10 /
					pll_settings->adjusted_pix_clk_100hz;
			if ((min_post_divider *
					pll_settings->adjusted_pix_clk_100hz) <
						calc_pll_cs->min_vco_khz * 10)
				min_post_divider++;
		}

		max_post_divider = calc_pll_cs->max_pix_clock_pll_post_divider;
		if (max_post_divider * pll_settings->adjusted_pix_clk_100hz
				> calc_pll_cs->max_vco_khz * 10)
			max_post_divider = calc_pll_cs->max_vco_khz * 10 /
					pll_settings->adjusted_pix_clk_100hz;
	}

/* 2) Find Reference divider ranges
 * When SS is enabled, or for Display Port even without SS,
 * pll_settings->referenceDivider is not zero.
 * So calculate PPLL FB and fractional FB divider
 * using the passed reference divider*/

	if (pll_settings->reference_divider) {
		min_ref_divider = pll_settings->reference_divider;
		max_ref_divider = pll_settings->reference_divider;
	} else {
		min_ref_divider = ((calc_pll_cs->ref_freq_khz
				/ calc_pll_cs->max_pll_input_freq_khz)
				> calc_pll_cs->min_pll_ref_divider)
			? calc_pll_cs->ref_freq_khz
					/ calc_pll_cs->max_pll_input_freq_khz
			: calc_pll_cs->min_pll_ref_divider;

		max_ref_divider = ((calc_pll_cs->ref_freq_khz
				/ calc_pll_cs->min_pll_input_freq_khz)
				< calc_pll_cs->max_pll_ref_divider)
			? calc_pll_cs->ref_freq_khz /
					calc_pll_cs->min_pll_input_freq_khz
			: calc_pll_cs->max_pll_ref_divider;
	}

/* If some parameters are invalid we could have scenario when  "min">"max"
 * which produced endless loop later.
 * We should investigate why we get the wrong parameters.
 * But to follow the similar logic when "adjustedPixelClock" is set to be 0
 * it is better to return here than cause system hang/watchdog timeout later.
 *  ## SVS Wed 15 Jul 2009 */

	if (min_post_divider > max_post_divider) {
		DC_LOG_ERROR(
			"%s Post divider range is invalid", __func__);
		return MAX_PLL_CALC_ERROR;
	}

	if (min_ref_divider > max_ref_divider) {
		DC_LOG_ERROR(
			"%s Reference divider range is invalid", __func__);
		return MAX_PLL_CALC_ERROR;
	}

/* 3) Try to find PLL dividers given ranges
 * starting with minimal error tolerance.
 * Increase error tolerance until PLL dividers found*/
	err_tolerance = MAX_PLL_CALC_ERROR;

	while (!calc_pll_dividers_in_range(
			calc_pll_cs,
			pll_settings,
			min_ref_divider,
			max_ref_divider,
			min_post_divider,
			max_post_divider,
			err_tolerance))
		err_tolerance += (err_tolerance > 10)
				? (err_tolerance / 10)
				: 1;

	return err_tolerance;
}

static bool pll_adjust_pix_clk(
		struct dce110_clk_src *clk_src,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	uint32_t actual_pix_clk_100hz = 0;
	uint32_t requested_clk_100hz = 0;
	struct bp_adjust_pixel_clock_parameters bp_adjust_pixel_clock_params = {
							0 };
	enum bp_result bp_result;
	switch (pix_clk_params->signal_type) {
	case SIGNAL_TYPE_HDMI_TYPE_A: {
		requested_clk_100hz = pix_clk_params->requested_pix_clk_100hz;
		if (pix_clk_params->pixel_encoding != PIXEL_ENCODING_YCBCR422) {
			switch (pix_clk_params->color_depth) {
			case COLOR_DEPTH_101010:
				requested_clk_100hz = (requested_clk_100hz * 5) >> 2;
				break; /* x1.25*/
			case COLOR_DEPTH_121212:
				requested_clk_100hz = (requested_clk_100hz * 6) >> 2;
				break; /* x1.5*/
			case COLOR_DEPTH_161616:
				requested_clk_100hz = requested_clk_100hz * 2;
				break; /* x2.0*/
			default:
				break;
			}
		}
		actual_pix_clk_100hz = requested_clk_100hz;
	}
		break;

	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		requested_clk_100hz = pix_clk_params->requested_sym_clk * 10;
		actual_pix_clk_100hz = pix_clk_params->requested_pix_clk_100hz;
		break;

	default:
		requested_clk_100hz = pix_clk_params->requested_pix_clk_100hz;
		actual_pix_clk_100hz = pix_clk_params->requested_pix_clk_100hz;
		break;
	}

	bp_adjust_pixel_clock_params.pixel_clock = requested_clk_100hz / 10;
	bp_adjust_pixel_clock_params.
		encoder_object_id = pix_clk_params->encoder_object_id;
	bp_adjust_pixel_clock_params.signal_type = pix_clk_params->signal_type;
	bp_adjust_pixel_clock_params.
		ss_enable = pix_clk_params->flags.ENABLE_SS;
	bp_result = clk_src->bios->funcs->adjust_pixel_clock(
			clk_src->bios, &bp_adjust_pixel_clock_params);
	if (bp_result == BP_RESULT_OK) {
		pll_settings->actual_pix_clk_100hz = actual_pix_clk_100hz;
		pll_settings->adjusted_pix_clk_100hz =
			bp_adjust_pixel_clock_params.adjusted_pixel_clock * 10;
		pll_settings->reference_divider =
			bp_adjust_pixel_clock_params.reference_divider;
		pll_settings->pix_clk_post_divider =
			bp_adjust_pixel_clock_params.pixel_clock_post_divider;

		return true;
	}

	return false;
}

/**
 * Calculate PLL Dividers for given Clock Value.
 * First will call VBIOS Adjust Exec table to check if requested Pixel clock
 * will be Adjusted based on usage.
 * Then it will calculate PLL Dividers for this Adjusted clock using preferred
 * method (Maximum VCO frequency).
 *
 * \return
 *     Calculation error in units of 0.01%
 */

static uint32_t dce110_get_pix_clk_dividers_helper (
		struct dce110_clk_src *clk_src,
		struct pll_settings *pll_settings,
		struct pixel_clk_params *pix_clk_params)
{
	uint32_t field = 0;
	uint32_t pll_calc_error = MAX_PLL_CALC_ERROR;
	DC_LOGGER_INIT();
	/* Check if reference clock is external (not pcie/xtalin)
	* HW Dce80 spec:
	* 00 - PCIE_REFCLK, 01 - XTALIN,    02 - GENERICA,    03 - GENERICB
	* 04 - HSYNCA,      05 - GENLK_CLK, 06 - PCIE_REFCLK, 07 - DVOCLK0 */
	REG_GET(PLL_CNTL, PLL_REF_DIV_SRC, &field);
	pll_settings->use_external_clk = (field > 1);

	/* VBIOS by default enables DP SS (spread on IDCLK) for DCE 8.0 always
	 * (we do not care any more from SI for some older DP Sink which
	 * does not report SS support, no known issues) */
	if ((pix_clk_params->flags.ENABLE_SS) ||
			(dc_is_dp_signal(pix_clk_params->signal_type))) {

		const struct spread_spectrum_data *ss_data = get_ss_data_entry(
					clk_src,
					pix_clk_params->signal_type,
					pll_settings->adjusted_pix_clk_100hz / 10);

		if (NULL != ss_data)
			pll_settings->ss_percentage = ss_data->percentage;
	}

	/* Check VBIOS AdjustPixelClock Exec table */
	if (!pll_adjust_pix_clk(clk_src, pix_clk_params, pll_settings)) {
		/* Should never happen, ASSERT and fill up values to be able
		 * to continue. */
		DC_LOG_ERROR(
			"%s: Failed to adjust pixel clock!!", __func__);
		pll_settings->actual_pix_clk_100hz =
				pix_clk_params->requested_pix_clk_100hz;
		pll_settings->adjusted_pix_clk_100hz =
				pix_clk_params->requested_pix_clk_100hz;

		if (dc_is_dp_signal(pix_clk_params->signal_type))
			pll_settings->adjusted_pix_clk_100hz = 1000000;
	}

	/* Calculate Dividers */
	if (pix_clk_params->signal_type == SIGNAL_TYPE_HDMI_TYPE_A)
		/*Calculate Dividers by HDMI object, no SS case or SS case */
		pll_calc_error =
			calculate_pixel_clock_pll_dividers(
					&clk_src->calc_pll_hdmi,
					pll_settings);
	else
		/*Calculate Dividers by default object, no SS case or SS case */
		pll_calc_error =
			calculate_pixel_clock_pll_dividers(
					&clk_src->calc_pll,
					pll_settings);

	return pll_calc_error;
}

static void dce112_get_pix_clk_dividers_helper (
		struct dce110_clk_src *clk_src,
		struct pll_settings *pll_settings,
		struct pixel_clk_params *pix_clk_params)
{
	uint32_t actual_pixel_clock_100hz;

	actual_pixel_clock_100hz = pix_clk_params->requested_pix_clk_100hz;
	/* Calculate Dividers */
	if (pix_clk_params->signal_type == SIGNAL_TYPE_HDMI_TYPE_A) {
		switch (pix_clk_params->color_depth) {
		case COLOR_DEPTH_101010:
			actual_pixel_clock_100hz = (actual_pixel_clock_100hz * 5) >> 2;
			break;
		case COLOR_DEPTH_121212:
			actual_pixel_clock_100hz = (actual_pixel_clock_100hz * 6) >> 2;
			break;
		case COLOR_DEPTH_161616:
			actual_pixel_clock_100hz = actual_pixel_clock_100hz * 2;
			break;
		default:
			break;
		}
	}
	pll_settings->actual_pix_clk_100hz = actual_pixel_clock_100hz;
	pll_settings->adjusted_pix_clk_100hz = actual_pixel_clock_100hz;
	pll_settings->calculated_pix_clk_100hz = pix_clk_params->requested_pix_clk_100hz;
}

static uint32_t dce110_get_pix_clk_dividers(
		struct clock_source *cs,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct dce110_clk_src *clk_src = TO_DCE110_CLK_SRC(cs);
	uint32_t pll_calc_error = MAX_PLL_CALC_ERROR;
	DC_LOGGER_INIT();

	if (pix_clk_params == NULL || pll_settings == NULL
			|| pix_clk_params->requested_pix_clk_100hz == 0) {
		DC_LOG_ERROR(
			"%s: Invalid parameters!!\n", __func__);
		return pll_calc_error;
	}

	memset(pll_settings, 0, sizeof(*pll_settings));

	if (cs->id == CLOCK_SOURCE_ID_DP_DTO ||
			cs->id == CLOCK_SOURCE_ID_EXTERNAL) {
		pll_settings->adjusted_pix_clk_100hz = clk_src->ext_clk_khz * 10;
		pll_settings->calculated_pix_clk_100hz = clk_src->ext_clk_khz * 10;
		pll_settings->actual_pix_clk_100hz =
					pix_clk_params->requested_pix_clk_100hz;
		return 0;
	}

	pll_calc_error = dce110_get_pix_clk_dividers_helper(clk_src,
			pll_settings, pix_clk_params);

	return pll_calc_error;
}

static uint32_t dce112_get_pix_clk_dividers(
		struct clock_source *cs,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct dce110_clk_src *clk_src = TO_DCE110_CLK_SRC(cs);
	DC_LOGGER_INIT();

	if (pix_clk_params == NULL || pll_settings == NULL
			|| pix_clk_params->requested_pix_clk_100hz == 0) {
		DC_LOG_ERROR(
			"%s: Invalid parameters!!\n", __func__);
		return -1;
	}

	memset(pll_settings, 0, sizeof(*pll_settings));

	if (cs->id == CLOCK_SOURCE_ID_DP_DTO ||
			cs->id == CLOCK_SOURCE_ID_EXTERNAL) {
		pll_settings->adjusted_pix_clk_100hz = clk_src->ext_clk_khz * 10;
		pll_settings->calculated_pix_clk_100hz = clk_src->ext_clk_khz * 10;
		pll_settings->actual_pix_clk_100hz =
					pix_clk_params->requested_pix_clk_100hz;
		return -1;
	}

	dce112_get_pix_clk_dividers_helper(clk_src,
			pll_settings, pix_clk_params);

	return 0;
}

static bool disable_spread_spectrum(struct dce110_clk_src *clk_src)
{
	enum bp_result result;
	struct bp_spread_spectrum_parameters bp_ss_params = {0};

	bp_ss_params.pll_id = clk_src->base.id;

	/*Call ASICControl to process ATOMBIOS Exec table*/
	result = clk_src->bios->funcs->enable_spread_spectrum_on_ppll(
			clk_src->bios,
			&bp_ss_params,
			false);

	return result == BP_RESULT_OK;
}

static bool calculate_ss(
		const struct pll_settings *pll_settings,
		const struct spread_spectrum_data *ss_data,
		struct delta_sigma_data *ds_data)
{
	struct fixed31_32 fb_div;
	struct fixed31_32 ss_amount;
	struct fixed31_32 ss_nslip_amount;
	struct fixed31_32 ss_ds_frac_amount;
	struct fixed31_32 ss_step_size;
	struct fixed31_32 modulation_time;

	if (ds_data == NULL)
		return false;
	if (ss_data == NULL)
		return false;
	if (ss_data->percentage == 0)
		return false;
	if (pll_settings == NULL)
		return false;

	memset(ds_data, 0, sizeof(struct delta_sigma_data));

	/* compute SS_AMOUNT_FBDIV & SS_AMOUNT_NFRAC_SLIP & SS_AMOUNT_DSFRAC*/
	/* 6 decimal point support in fractional feedback divider */
	fb_div  = dc_fixpt_from_fraction(
		pll_settings->fract_feedback_divider, 1000000);
	fb_div = dc_fixpt_add_int(fb_div, pll_settings->feedback_divider);

	ds_data->ds_frac_amount = 0;
	/*spreadSpectrumPercentage is in the unit of .01%,
	 * so have to divided by 100 * 100*/
	ss_amount = dc_fixpt_mul(
		fb_div, dc_fixpt_from_fraction(ss_data->percentage,
					100 * ss_data->percentage_divider));
	ds_data->feedback_amount = dc_fixpt_floor(ss_amount);

	ss_nslip_amount = dc_fixpt_sub(ss_amount,
		dc_fixpt_from_int(ds_data->feedback_amount));
	ss_nslip_amount = dc_fixpt_mul_int(ss_nslip_amount, 10);
	ds_data->nfrac_amount = dc_fixpt_floor(ss_nslip_amount);

	ss_ds_frac_amount = dc_fixpt_sub(ss_nslip_amount,
		dc_fixpt_from_int(ds_data->nfrac_amount));
	ss_ds_frac_amount = dc_fixpt_mul_int(ss_ds_frac_amount, 65536);
	ds_data->ds_frac_amount = dc_fixpt_floor(ss_ds_frac_amount);

	/* compute SS_STEP_SIZE_DSFRAC */
	modulation_time = dc_fixpt_from_fraction(
		pll_settings->reference_freq * 1000,
		pll_settings->reference_divider * ss_data->modulation_freq_hz);

	if (ss_data->flags.CENTER_SPREAD)
		modulation_time = dc_fixpt_div_int(modulation_time, 4);
	else
		modulation_time = dc_fixpt_div_int(modulation_time, 2);

	ss_step_size = dc_fixpt_div(ss_amount, modulation_time);
	/* SS_STEP_SIZE_DSFRAC_DEC = Int(SS_STEP_SIZE * 2 ^ 16 * 10)*/
	ss_step_size = dc_fixpt_mul_int(ss_step_size, 65536 * 10);
	ds_data->ds_frac_size =  dc_fixpt_floor(ss_step_size);

	return true;
}

static bool enable_spread_spectrum(
		struct dce110_clk_src *clk_src,
		enum signal_type signal, struct pll_settings *pll_settings)
{
	struct bp_spread_spectrum_parameters bp_params = {0};
	struct delta_sigma_data d_s_data;
	const struct spread_spectrum_data *ss_data = NULL;

	ss_data = get_ss_data_entry(
			clk_src,
			signal,
			pll_settings->calculated_pix_clk_100hz / 10);

/* Pixel clock PLL has been programmed to generate desired pixel clock,
 * now enable SS on pixel clock */
/* TODO is it OK to return true not doing anything ??*/
	if (ss_data != NULL && pll_settings->ss_percentage != 0) {
		if (calculate_ss(pll_settings, ss_data, &d_s_data)) {
			bp_params.ds.feedback_amount =
					d_s_data.feedback_amount;
			bp_params.ds.nfrac_amount =
					d_s_data.nfrac_amount;
			bp_params.ds.ds_frac_size = d_s_data.ds_frac_size;
			bp_params.ds_frac_amount =
					d_s_data.ds_frac_amount;
			bp_params.flags.DS_TYPE = 1;
			bp_params.pll_id = clk_src->base.id;
			bp_params.percentage = ss_data->percentage;
			if (ss_data->flags.CENTER_SPREAD)
				bp_params.flags.CENTER_SPREAD = 1;
			if (ss_data->flags.EXTERNAL_SS)
				bp_params.flags.EXTERNAL_SS = 1;

			if (BP_RESULT_OK !=
				clk_src->bios->funcs->
					enable_spread_spectrum_on_ppll(
							clk_src->bios,
							&bp_params,
							true))
				return false;
		} else
			return false;
	}
	return true;
}

static void dce110_program_pixel_clk_resync(
		struct dce110_clk_src *clk_src,
		enum signal_type signal_type,
		enum dc_color_depth colordepth)
{
	REG_UPDATE(RESYNC_CNTL,
			DCCG_DEEP_COLOR_CNTL1, 0);
	/*
	 24 bit mode: TMDS clock = 1.0 x pixel clock  (1:1)
	 30 bit mode: TMDS clock = 1.25 x pixel clock (5:4)
	 36 bit mode: TMDS clock = 1.5 x pixel clock  (3:2)
	 48 bit mode: TMDS clock = 2 x pixel clock    (2:1)
	 */
	if (signal_type != SIGNAL_TYPE_HDMI_TYPE_A)
		return;

	switch (colordepth) {
	case COLOR_DEPTH_888:
		REG_UPDATE(RESYNC_CNTL,
				DCCG_DEEP_COLOR_CNTL1, 0);
		break;
	case COLOR_DEPTH_101010:
		REG_UPDATE(RESYNC_CNTL,
				DCCG_DEEP_COLOR_CNTL1, 1);
		break;
	case COLOR_DEPTH_121212:
		REG_UPDATE(RESYNC_CNTL,
				DCCG_DEEP_COLOR_CNTL1, 2);
		break;
	case COLOR_DEPTH_161616:
		REG_UPDATE(RESYNC_CNTL,
				DCCG_DEEP_COLOR_CNTL1, 3);
		break;
	default:
		break;
	}
}

static void dce112_program_pixel_clk_resync(
		struct dce110_clk_src *clk_src,
		enum signal_type signal_type,
		enum dc_color_depth colordepth,
		bool enable_ycbcr420)
{
	uint32_t deep_color_cntl = 0;
	uint32_t double_rate_enable = 0;

	/*
	 24 bit mode: TMDS clock = 1.0 x pixel clock  (1:1)
	 30 bit mode: TMDS clock = 1.25 x pixel clock (5:4)
	 36 bit mode: TMDS clock = 1.5 x pixel clock  (3:2)
	 48 bit mode: TMDS clock = 2 x pixel clock    (2:1)
	 */
	if (signal_type == SIGNAL_TYPE_HDMI_TYPE_A) {
		double_rate_enable = enable_ycbcr420 ? 1 : 0;

		switch (colordepth) {
		case COLOR_DEPTH_888:
			deep_color_cntl = 0;
			break;
		case COLOR_DEPTH_101010:
			deep_color_cntl = 1;
			break;
		case COLOR_DEPTH_121212:
			deep_color_cntl = 2;
			break;
		case COLOR_DEPTH_161616:
			deep_color_cntl = 3;
			break;
		default:
			break;
		}
	}

	if (clk_src->cs_mask->PHYPLLA_PIXCLK_DOUBLE_RATE_ENABLE)
		REG_UPDATE_2(PIXCLK_RESYNC_CNTL,
				PHYPLLA_DCCG_DEEP_COLOR_CNTL, deep_color_cntl,
				PHYPLLA_PIXCLK_DOUBLE_RATE_ENABLE, double_rate_enable);
	else
		REG_UPDATE(PIXCLK_RESYNC_CNTL,
				PHYPLLA_DCCG_DEEP_COLOR_CNTL, deep_color_cntl);

}

static bool dce110_program_pix_clk(
		struct clock_source *clock_source,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct dce110_clk_src *clk_src = TO_DCE110_CLK_SRC(clock_source);
	struct bp_pixel_clock_parameters bp_pc_params = {0};

	/* First disable SS
	 * ATOMBIOS will enable by default SS on PLL for DP,
	 * do not disable it here
	 */
	if (clock_source->id != CLOCK_SOURCE_ID_EXTERNAL &&
			!dc_is_dp_signal(pix_clk_params->signal_type) &&
			clock_source->ctx->dce_version <= DCE_VERSION_11_0)
		disable_spread_spectrum(clk_src);

	/*ATOMBIOS expects pixel rate adjusted by deep color ratio)*/
	bp_pc_params.controller_id = pix_clk_params->controller_id;
	bp_pc_params.pll_id = clock_source->id;
	bp_pc_params.target_pixel_clock_100hz = pll_settings->actual_pix_clk_100hz;
	bp_pc_params.encoder_object_id = pix_clk_params->encoder_object_id;
	bp_pc_params.signal_type = pix_clk_params->signal_type;

	bp_pc_params.reference_divider = pll_settings->reference_divider;
	bp_pc_params.feedback_divider = pll_settings->feedback_divider;
	bp_pc_params.fractional_feedback_divider =
			pll_settings->fract_feedback_divider;
	bp_pc_params.pixel_clock_post_divider =
			pll_settings->pix_clk_post_divider;
	bp_pc_params.flags.SET_EXTERNAL_REF_DIV_SRC =
					pll_settings->use_external_clk;

	if (clk_src->bios->funcs->set_pixel_clock(
			clk_src->bios, &bp_pc_params) != BP_RESULT_OK)
		return false;
	/* Enable SS
	 * ATOMBIOS will enable by default SS for DP on PLL ( DP ID clock),
	 * based on HW display PLL team, SS control settings should be programmed
	 * during PLL Reset, but they do not have effect
	 * until SS_EN is asserted.*/
	if (clock_source->id != CLOCK_SOURCE_ID_EXTERNAL
			&& !dc_is_dp_signal(pix_clk_params->signal_type)) {

		if (pix_clk_params->flags.ENABLE_SS)
			if (!enable_spread_spectrum(clk_src,
							pix_clk_params->signal_type,
							pll_settings))
				return false;

		/* Resync deep color DTO */
		dce110_program_pixel_clk_resync(clk_src,
					pix_clk_params->signal_type,
					pix_clk_params->color_depth);
	}

	return true;
}

static bool dce112_program_pix_clk(
		struct clock_source *clock_source,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	struct dce110_clk_src *clk_src = TO_DCE110_CLK_SRC(clock_source);
	struct bp_pixel_clock_parameters bp_pc_params = {0};

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	if (IS_FPGA_MAXIMUS_DC(clock_source->ctx->dce_environment)) {
		unsigned int inst = pix_clk_params->controller_id - CONTROLLER_ID_D0;
		unsigned dp_dto_ref_100hz = 7000000;
		unsigned clock_100hz = pll_settings->actual_pix_clk_100hz;

		/* Set DTO values: phase = target clock, modulo = reference clock */
		REG_WRITE(PHASE[inst], clock_100hz);
		REG_WRITE(MODULO[inst], dp_dto_ref_100hz);

		/* Enable DTO */
		REG_UPDATE(PIXEL_RATE_CNTL[inst], DP_DTO0_ENABLE, 1);
		return true;
	}
#endif
	/* First disable SS
	 * ATOMBIOS will enable by default SS on PLL for DP,
	 * do not disable it here
	 */
	if (clock_source->id != CLOCK_SOURCE_ID_EXTERNAL &&
			!dc_is_dp_signal(pix_clk_params->signal_type) &&
			clock_source->ctx->dce_version <= DCE_VERSION_11_0)
		disable_spread_spectrum(clk_src);

	/*ATOMBIOS expects pixel rate adjusted by deep color ratio)*/
	bp_pc_params.controller_id = pix_clk_params->controller_id;
	bp_pc_params.pll_id = clock_source->id;
	bp_pc_params.target_pixel_clock_100hz = pll_settings->actual_pix_clk_100hz;
	bp_pc_params.encoder_object_id = pix_clk_params->encoder_object_id;
	bp_pc_params.signal_type = pix_clk_params->signal_type;

	if (clock_source->id != CLOCK_SOURCE_ID_DP_DTO) {
		bp_pc_params.flags.SET_GENLOCK_REF_DIV_SRC =
						pll_settings->use_external_clk;
		bp_pc_params.flags.SET_XTALIN_REF_SRC =
						!pll_settings->use_external_clk;
		if (pix_clk_params->flags.SUPPORT_YCBCR420) {
			bp_pc_params.flags.SUPPORT_YUV_420 = 1;
		}
	}
	if (clk_src->bios->funcs->set_pixel_clock(
			clk_src->bios, &bp_pc_params) != BP_RESULT_OK)
		return false;
	/* Resync deep color DTO */
	if (clock_source->id != CLOCK_SOURCE_ID_DP_DTO)
		dce112_program_pixel_clk_resync(clk_src,
					pix_clk_params->signal_type,
					pix_clk_params->color_depth,
					pix_clk_params->flags.SUPPORT_YCBCR420);

	return true;
}


static bool dce110_clock_source_power_down(
		struct clock_source *clk_src)
{
	struct dce110_clk_src *dce110_clk_src = TO_DCE110_CLK_SRC(clk_src);
	enum bp_result bp_result;
	struct bp_pixel_clock_parameters bp_pixel_clock_params = {0};

	if (clk_src->dp_clk_src)
		return true;

	/* If Pixel Clock is 0 it means Power Down Pll*/
	bp_pixel_clock_params.controller_id = CONTROLLER_ID_UNDEFINED;
	bp_pixel_clock_params.pll_id = clk_src->id;
	bp_pixel_clock_params.flags.FORCE_PROGRAMMING_OF_PLL = 1;

	/*Call ASICControl to process ATOMBIOS Exec table*/
	bp_result = dce110_clk_src->bios->funcs->set_pixel_clock(
			dce110_clk_src->bios,
			&bp_pixel_clock_params);

	return bp_result == BP_RESULT_OK;
}

static bool get_pixel_clk_frequency_100hz(
		const struct clock_source *clock_source,
		unsigned int inst,
		unsigned int *pixel_clk_khz)
{
	struct dce110_clk_src *clk_src = TO_DCE110_CLK_SRC(clock_source);
	unsigned int clock_hz = 0;

	if (clock_source->id == CLOCK_SOURCE_ID_DP_DTO) {
		clock_hz = REG_READ(PHASE[inst]);

		/* NOTE: There is agreement with VBIOS here that MODULO is
		 * programmed equal to DPREFCLK, in which case PHASE will be
		 * equivalent to pixel clock.
		 */
		*pixel_clk_khz = clock_hz / 100;
		return true;
	}

	return false;
}

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)

/* this table is use to find *1.001 and /1.001 pixel rates from non-precise pixel rate */
struct pixel_rate_range_table_entry {
	unsigned int range_min_khz;
	unsigned int range_max_khz;
	unsigned int target_pixel_rate_khz;
	unsigned short mult_factor;
	unsigned short div_factor;
};

static const struct pixel_rate_range_table_entry video_optimized_pixel_rates[] = {
	// /1.001 rates
	{25170, 25180, 25200, 1000, 1001},	//25.2MHz   ->   25.17
	{59340, 59350, 59400, 1000, 1001},	//59.4Mhz   ->   59.340
	{74170, 74180, 74250, 1000, 1001},	//74.25Mhz  ->   74.1758
	{125870, 125880, 126000, 1000, 1001},	//126Mhz    ->  125.87
	{148350, 148360, 148500, 1000, 1001},	//148.5Mhz  ->  148.3516
	{167830, 167840, 168000, 1000, 1001},	//168Mhz    ->  167.83
	{222520, 222530, 222750, 1000, 1001},	//222.75Mhz ->  222.527
	{257140, 257150, 257400, 1000, 1001},	//257.4Mhz  ->  257.1429
	{296700, 296710, 297000, 1000, 1001},	//297Mhz    ->  296.7033
	{342850, 342860, 343200, 1000, 1001},	//343.2Mhz  ->  342.857
	{395600, 395610, 396000, 1000, 1001},	//396Mhz    ->  395.6
	{409090, 409100, 409500, 1000, 1001},	//409.5Mhz  ->  409.091
	{445050, 445060, 445500, 1000, 1001},	//445.5Mhz  ->  445.055
	{467530, 467540, 468000, 1000, 1001},	//468Mhz    ->  467.5325
	{519230, 519240, 519750, 1000, 1001},	//519.75Mhz ->  519.231
	{525970, 525980, 526500, 1000, 1001},	//526.5Mhz  ->  525.974
	{545450, 545460, 546000, 1000, 1001},	//546Mhz    ->  545.455
	{593400, 593410, 594000, 1000, 1001},	//594Mhz    ->  593.4066
	{623370, 623380, 624000, 1000, 1001},	//624Mhz    ->  623.377
	{692300, 692310, 693000, 1000, 1001},	//693Mhz    ->  692.308
	{701290, 701300, 702000, 1000, 1001},	//702Mhz    ->  701.2987
	{791200, 791210, 792000, 1000, 1001},	//792Mhz    ->  791.209
	{890100, 890110, 891000, 1000, 1001},	//891Mhz    ->  890.1099
	{1186810, 1186820, 1188000, 1000, 1001},//1188Mhz   -> 1186.8131

	// *1.001 rates
	{27020, 27030, 27000, 1001, 1000}, //27Mhz
	{54050, 54060, 54000, 1001, 1000}, //54Mhz
	{108100, 108110, 108000, 1001, 1000},//108Mhz
};

static bool dcn20_program_pix_clk(
		struct clock_source *clock_source,
		struct pixel_clk_params *pix_clk_params,
		struct pll_settings *pll_settings)
{
	dce112_program_pix_clk(clock_source, pix_clk_params, pll_settings);

	return true;
}

static const struct clock_source_funcs dcn20_clk_src_funcs = {
	.cs_power_down = dce110_clock_source_power_down,
	.program_pix_clk = dcn20_program_pix_clk,
	.get_pix_clk_dividers = dce112_get_pix_clk_dividers,
	.get_pixel_clk_frequency_100hz = get_pixel_clk_frequency_100hz
};
#endif

/*****************************************/
/* Constructor                           */
/*****************************************/

static const struct clock_source_funcs dce112_clk_src_funcs = {
	.cs_power_down = dce110_clock_source_power_down,
	.program_pix_clk = dce112_program_pix_clk,
	.get_pix_clk_dividers = dce112_get_pix_clk_dividers,
	.get_pixel_clk_frequency_100hz = get_pixel_clk_frequency_100hz
};
static const struct clock_source_funcs dce110_clk_src_funcs = {
	.cs_power_down = dce110_clock_source_power_down,
	.program_pix_clk = dce110_program_pix_clk,
	.get_pix_clk_dividers = dce110_get_pix_clk_dividers,
	.get_pixel_clk_frequency_100hz = get_pixel_clk_frequency_100hz
};


static void get_ss_info_from_atombios(
		struct dce110_clk_src *clk_src,
		enum as_signal_type as_signal,
		struct spread_spectrum_data *spread_spectrum_data[],
		uint32_t *ss_entries_num)
{
	enum bp_result bp_result = BP_RESULT_FAILURE;
	struct spread_spectrum_info *ss_info;
	struct spread_spectrum_data *ss_data;
	struct spread_spectrum_info *ss_info_cur;
	struct spread_spectrum_data *ss_data_cur;
	uint32_t i;
	DC_LOGGER_INIT();
	if (ss_entries_num == NULL) {
		DC_LOG_SYNC(
			"Invalid entry !!!\n");
		return;
	}
	if (spread_spectrum_data == NULL) {
		DC_LOG_SYNC(
			"Invalid array pointer!!!\n");
		return;
	}

	spread_spectrum_data[0] = NULL;
	*ss_entries_num = 0;

	*ss_entries_num = clk_src->bios->funcs->get_ss_entry_number(
			clk_src->bios,
			as_signal);

	if (*ss_entries_num == 0)
		return;

	ss_info = kcalloc(*ss_entries_num,
			  sizeof(struct spread_spectrum_info),
			  GFP_KERNEL);
	ss_info_cur = ss_info;
	if (ss_info == NULL)
		return;

	ss_data = kcalloc(*ss_entries_num,
			  sizeof(struct spread_spectrum_data),
			  GFP_KERNEL);
	if (ss_data == NULL)
		goto out_free_info;

	for (i = 0, ss_info_cur = ss_info;
		i < (*ss_entries_num);
		++i, ++ss_info_cur) {

		bp_result = clk_src->bios->funcs->get_spread_spectrum_info(
				clk_src->bios,
				as_signal,
				i,
				ss_info_cur);

		if (bp_result != BP_RESULT_OK)
			goto out_free_data;
	}

	for (i = 0, ss_info_cur = ss_info, ss_data_cur = ss_data;
		i < (*ss_entries_num);
		++i, ++ss_info_cur, ++ss_data_cur) {

		if (ss_info_cur->type.STEP_AND_DELAY_INFO != false) {
			DC_LOG_SYNC(
				"Invalid ATOMBIOS SS Table!!!\n");
			goto out_free_data;
		}

		/* for HDMI check SS percentage,
		 * if it is > 6 (0.06%), the ATOMBIOS table info is invalid*/
		if (as_signal == AS_SIGNAL_TYPE_HDMI
				&& ss_info_cur->spread_spectrum_percentage > 6){
			/* invalid input, do nothing */
			DC_LOG_SYNC(
				"Invalid SS percentage ");
			DC_LOG_SYNC(
				"for HDMI in ATOMBIOS info Table!!!\n");
			continue;
		}
		if (ss_info_cur->spread_percentage_divider == 1000) {
			/* Keep previous precision from ATOMBIOS for these
			* in case new precision set by ATOMBIOS for these
			* (otherwise all code in DCE specific classes
			* for all previous ASICs would need
			* to be updated for SS calculations,
			* Audio SS compensation and DP DTO SS compensation
			* which assumes fixed SS percentage Divider = 100)*/
			ss_info_cur->spread_spectrum_percentage /= 10;
			ss_info_cur->spread_percentage_divider = 100;
		}

		ss_data_cur->freq_range_khz = ss_info_cur->target_clock_range;
		ss_data_cur->percentage =
				ss_info_cur->spread_spectrum_percentage;
		ss_data_cur->percentage_divider =
				ss_info_cur->spread_percentage_divider;
		ss_data_cur->modulation_freq_hz =
				ss_info_cur->spread_spectrum_range;

		if (ss_info_cur->type.CENTER_MODE)
			ss_data_cur->flags.CENTER_SPREAD = 1;

		if (ss_info_cur->type.EXTERNAL)
			ss_data_cur->flags.EXTERNAL_SS = 1;

	}

	*spread_spectrum_data = ss_data;
	kfree(ss_info);
	return;

out_free_data:
	kfree(ss_data);
	*ss_entries_num = 0;
out_free_info:
	kfree(ss_info);
}

static void ss_info_from_atombios_create(
	struct dce110_clk_src *clk_src)
{
	get_ss_info_from_atombios(
		clk_src,
		AS_SIGNAL_TYPE_DISPLAY_PORT,
		&clk_src->dp_ss_params,
		&clk_src->dp_ss_params_cnt);
	get_ss_info_from_atombios(
		clk_src,
		AS_SIGNAL_TYPE_HDMI,
		&clk_src->hdmi_ss_params,
		&clk_src->hdmi_ss_params_cnt);
	get_ss_info_from_atombios(
		clk_src,
		AS_SIGNAL_TYPE_DVI,
		&clk_src->dvi_ss_params,
		&clk_src->dvi_ss_params_cnt);
	get_ss_info_from_atombios(
		clk_src,
		AS_SIGNAL_TYPE_LVDS,
		&clk_src->lvds_ss_params,
		&clk_src->lvds_ss_params_cnt);
}

static bool calc_pll_max_vco_construct(
			struct calc_pll_clock_source *calc_pll_cs,
			struct calc_pll_clock_source_init_data *init_data)
{
	uint32_t i;
	struct dc_firmware_info *fw_info;
	if (calc_pll_cs == NULL ||
			init_data == NULL ||
			init_data->bp == NULL)
		return false;

	if (!init_data->bp->fw_info_valid)
		return false;

	fw_info = &init_data->bp->fw_info;
	calc_pll_cs->ctx = init_data->ctx;
	calc_pll_cs->ref_freq_khz = fw_info->pll_info.crystal_frequency;
	calc_pll_cs->min_vco_khz =
			fw_info->pll_info.min_output_pxl_clk_pll_frequency;
	calc_pll_cs->max_vco_khz =
			fw_info->pll_info.max_output_pxl_clk_pll_frequency;

	if (init_data->max_override_input_pxl_clk_pll_freq_khz != 0)
		calc_pll_cs->max_pll_input_freq_khz =
			init_data->max_override_input_pxl_clk_pll_freq_khz;
	else
		calc_pll_cs->max_pll_input_freq_khz =
			fw_info->pll_info.max_input_pxl_clk_pll_frequency;

	if (init_data->min_override_input_pxl_clk_pll_freq_khz != 0)
		calc_pll_cs->min_pll_input_freq_khz =
			init_data->min_override_input_pxl_clk_pll_freq_khz;
	else
		calc_pll_cs->min_pll_input_freq_khz =
			fw_info->pll_info.min_input_pxl_clk_pll_frequency;

	calc_pll_cs->min_pix_clock_pll_post_divider =
			init_data->min_pix_clk_pll_post_divider;
	calc_pll_cs->max_pix_clock_pll_post_divider =
			init_data->max_pix_clk_pll_post_divider;
	calc_pll_cs->min_pll_ref_divider =
			init_data->min_pll_ref_divider;
	calc_pll_cs->max_pll_ref_divider =
			init_data->max_pll_ref_divider;

	if (init_data->num_fract_fb_divider_decimal_point == 0 ||
		init_data->num_fract_fb_divider_decimal_point_precision >
				init_data->num_fract_fb_divider_decimal_point) {
		DC_LOG_ERROR(
			"The dec point num or precision is incorrect!");
		return false;
	}
	if (init_data->num_fract_fb_divider_decimal_point_precision == 0) {
		DC_LOG_ERROR(
			"Incorrect fract feedback divider precision num!");
		return false;
	}

	calc_pll_cs->fract_fb_divider_decimal_points_num =
				init_data->num_fract_fb_divider_decimal_point;
	calc_pll_cs->fract_fb_divider_precision =
			init_data->num_fract_fb_divider_decimal_point_precision;
	calc_pll_cs->fract_fb_divider_factor = 1;
	for (i = 0; i < calc_pll_cs->fract_fb_divider_decimal_points_num; ++i)
		calc_pll_cs->fract_fb_divider_factor *= 10;

	calc_pll_cs->fract_fb_divider_precision_factor = 1;
	for (
		i = 0;
		i < (calc_pll_cs->fract_fb_divider_decimal_points_num -
				calc_pll_cs->fract_fb_divider_precision);
		++i)
		calc_pll_cs->fract_fb_divider_precision_factor *= 10;

	return true;
}

bool dce110_clk_src_construct(
	struct dce110_clk_src *clk_src,
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce110_clk_src_regs *regs,
	const struct dce110_clk_src_shift *cs_shift,
	const struct dce110_clk_src_mask *cs_mask)
{
	struct calc_pll_clock_source_init_data calc_pll_cs_init_data_hdmi;
	struct calc_pll_clock_source_init_data calc_pll_cs_init_data;

	clk_src->base.ctx = ctx;
	clk_src->bios = bios;
	clk_src->base.id = id;
	clk_src->base.funcs = &dce110_clk_src_funcs;

	clk_src->regs = regs;
	clk_src->cs_shift = cs_shift;
	clk_src->cs_mask = cs_mask;

	if (!clk_src->bios->fw_info_valid) {
		ASSERT_CRITICAL(false);
		goto unexpected_failure;
	}

	clk_src->ext_clk_khz = clk_src->bios->fw_info.external_clock_source_frequency_for_dp;

	/* structure normally used with PLL ranges from ATOMBIOS; DS on by default */
	calc_pll_cs_init_data.bp = bios;
	calc_pll_cs_init_data.min_pix_clk_pll_post_divider = 1;
	calc_pll_cs_init_data.max_pix_clk_pll_post_divider =
			clk_src->cs_mask->PLL_POST_DIV_PIXCLK;
	calc_pll_cs_init_data.min_pll_ref_divider =	1;
	calc_pll_cs_init_data.max_pll_ref_divider =	clk_src->cs_mask->PLL_REF_DIV;
	/* when 0 use minInputPxlClkPLLFrequencyInKHz from firmwareInfo*/
	calc_pll_cs_init_data.min_override_input_pxl_clk_pll_freq_khz =	0;
	/* when 0 use maxInputPxlClkPLLFrequencyInKHz from firmwareInfo*/
	calc_pll_cs_init_data.max_override_input_pxl_clk_pll_freq_khz =	0;
	/*numberOfFractFBDividerDecimalPoints*/
	calc_pll_cs_init_data.num_fract_fb_divider_decimal_point =
			FRACT_FB_DIVIDER_DEC_POINTS_MAX_NUM;
	/*number of decimal point to round off for fractional feedback divider value*/
	calc_pll_cs_init_data.num_fract_fb_divider_decimal_point_precision =
			FRACT_FB_DIVIDER_DEC_POINTS_MAX_NUM;
	calc_pll_cs_init_data.ctx =	ctx;

	/*structure for HDMI, no SS or SS% <= 0.06% for 27 MHz Ref clock */
	calc_pll_cs_init_data_hdmi.bp = bios;
	calc_pll_cs_init_data_hdmi.min_pix_clk_pll_post_divider = 1;
	calc_pll_cs_init_data_hdmi.max_pix_clk_pll_post_divider =
			clk_src->cs_mask->PLL_POST_DIV_PIXCLK;
	calc_pll_cs_init_data_hdmi.min_pll_ref_divider = 1;
	calc_pll_cs_init_data_hdmi.max_pll_ref_divider = clk_src->cs_mask->PLL_REF_DIV;
	/* when 0 use minInputPxlClkPLLFrequencyInKHz from firmwareInfo*/
	calc_pll_cs_init_data_hdmi.min_override_input_pxl_clk_pll_freq_khz = 13500;
	/* when 0 use maxInputPxlClkPLLFrequencyInKHz from firmwareInfo*/
	calc_pll_cs_init_data_hdmi.max_override_input_pxl_clk_pll_freq_khz = 27000;
	/*numberOfFractFBDividerDecimalPoints*/
	calc_pll_cs_init_data_hdmi.num_fract_fb_divider_decimal_point =
			FRACT_FB_DIVIDER_DEC_POINTS_MAX_NUM;
	/*number of decimal point to round off for fractional feedback divider value*/
	calc_pll_cs_init_data_hdmi.num_fract_fb_divider_decimal_point_precision =
			FRACT_FB_DIVIDER_DEC_POINTS_MAX_NUM;
	calc_pll_cs_init_data_hdmi.ctx = ctx;

	clk_src->ref_freq_khz = clk_src->bios->fw_info.pll_info.crystal_frequency;

	if (clk_src->base.id == CLOCK_SOURCE_ID_EXTERNAL)
		return true;

	/* PLL only from here on */
	ss_info_from_atombios_create(clk_src);

	if (!calc_pll_max_vco_construct(
			&clk_src->calc_pll,
			&calc_pll_cs_init_data)) {
		ASSERT_CRITICAL(false);
		goto unexpected_failure;
	}


	calc_pll_cs_init_data_hdmi.
			min_override_input_pxl_clk_pll_freq_khz = clk_src->ref_freq_khz/2;
	calc_pll_cs_init_data_hdmi.
			max_override_input_pxl_clk_pll_freq_khz = clk_src->ref_freq_khz;


	if (!calc_pll_max_vco_construct(
			&clk_src->calc_pll_hdmi, &calc_pll_cs_init_data_hdmi)) {
		ASSERT_CRITICAL(false);
		goto unexpected_failure;
	}

	return true;

unexpected_failure:
	return false;
}

bool dce112_clk_src_construct(
	struct dce110_clk_src *clk_src,
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce110_clk_src_regs *regs,
	const struct dce110_clk_src_shift *cs_shift,
	const struct dce110_clk_src_mask *cs_mask)
{
	clk_src->base.ctx = ctx;
	clk_src->bios = bios;
	clk_src->base.id = id;
	clk_src->base.funcs = &dce112_clk_src_funcs;

	clk_src->regs = regs;
	clk_src->cs_shift = cs_shift;
	clk_src->cs_mask = cs_mask;

	if (!clk_src->bios->fw_info_valid) {
		ASSERT_CRITICAL(false);
		return false;
	}

	clk_src->ext_clk_khz = clk_src->bios->fw_info.external_clock_source_frequency_for_dp;

	return true;
}

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
bool dcn20_clk_src_construct(
	struct dce110_clk_src *clk_src,
	struct dc_context *ctx,
	struct dc_bios *bios,
	enum clock_source_id id,
	const struct dce110_clk_src_regs *regs,
	const struct dce110_clk_src_shift *cs_shift,
	const struct dce110_clk_src_mask *cs_mask)
{
	bool ret = dce112_clk_src_construct(clk_src, ctx, bios, id, regs, cs_shift, cs_mask);

	clk_src->base.funcs = &dcn20_clk_src_funcs;

	return ret;
}
#endif
