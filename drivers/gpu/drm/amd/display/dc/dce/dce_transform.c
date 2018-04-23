/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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

#include "dce_transform.h"
#include "reg_helper.h"
#include "opp.h"
#include "basics/conversion.h"
#include "dc.h"

#define REG(reg) \
	(xfm_dce->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	xfm_dce->xfm_shift->field_name, xfm_dce->xfm_mask->field_name

#define CTX \
	xfm_dce->base.ctx
#define DC_LOGGER \
	xfm_dce->base.ctx->logger

#define IDENTITY_RATIO(ratio) (dal_fixed31_32_u2d19(ratio) == (1 << 19))
#define GAMUT_MATRIX_SIZE 12
#define SCL_PHASES 16

enum dcp_out_trunc_round_mode {
	DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE,
	DCP_OUT_TRUNC_ROUND_MODE_ROUND
};

enum dcp_out_trunc_round_depth {
	DCP_OUT_TRUNC_ROUND_DEPTH_14BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_13BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_12BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_11BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_10BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_9BIT,
	DCP_OUT_TRUNC_ROUND_DEPTH_8BIT
};

/*  defines the various methods of bit reduction available for use */
enum dcp_bit_depth_reduction_mode {
	DCP_BIT_DEPTH_REDUCTION_MODE_DITHER,
	DCP_BIT_DEPTH_REDUCTION_MODE_ROUND,
	DCP_BIT_DEPTH_REDUCTION_MODE_TRUNCATE,
	DCP_BIT_DEPTH_REDUCTION_MODE_DISABLED,
	DCP_BIT_DEPTH_REDUCTION_MODE_INVALID
};

enum dcp_spatial_dither_mode {
	DCP_SPATIAL_DITHER_MODE_AAAA,
	DCP_SPATIAL_DITHER_MODE_A_AA_A,
	DCP_SPATIAL_DITHER_MODE_AABBAABB,
	DCP_SPATIAL_DITHER_MODE_AABBCCAABBCC,
	DCP_SPATIAL_DITHER_MODE_INVALID
};

enum dcp_spatial_dither_depth {
	DCP_SPATIAL_DITHER_DEPTH_30BPP,
	DCP_SPATIAL_DITHER_DEPTH_24BPP
};

enum csc_color_mode {
	/* 00 - BITS2:0 Bypass */
	CSC_COLOR_MODE_GRAPHICS_BYPASS,
	/* 01 - hard coded coefficient TV RGB */
	CSC_COLOR_MODE_GRAPHICS_PREDEFINED,
	/* 04 - programmable OUTPUT CSC coefficient */
	CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC,
};

enum grph_color_adjust_option {
	GRPH_COLOR_MATRIX_HW_DEFAULT = 1,
	GRPH_COLOR_MATRIX_SW
};

static const struct out_csc_color_matrix global_color_matrix[] = {
{ COLOR_SPACE_SRGB,
	{ 0x2000, 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0x2000, 0} },
{ COLOR_SPACE_SRGB_LIMITED,
	{ 0x1B60, 0, 0, 0x200, 0, 0x1B60, 0, 0x200, 0, 0, 0x1B60, 0x200} },
{ COLOR_SPACE_YCBCR601,
	{ 0xE00, 0xF447, 0xFDB9, 0x1000, 0x82F, 0x1012, 0x31F, 0x200, 0xFB47,
		0xF6B9, 0xE00, 0x1000} },
{ COLOR_SPACE_YCBCR709, { 0xE00, 0xF349, 0xFEB7, 0x1000, 0x5D2, 0x1394, 0x1FA,
	0x200, 0xFCCB, 0xF535, 0xE00, 0x1000} },
/* TODO: correct values below */
{ COLOR_SPACE_YCBCR601_LIMITED, { 0xE00, 0xF447, 0xFDB9, 0x1000, 0x991,
	0x12C9, 0x3A6, 0x200, 0xFB47, 0xF6B9, 0xE00, 0x1000} },
{ COLOR_SPACE_YCBCR709_LIMITED, { 0xE00, 0xF349, 0xFEB7, 0x1000, 0x6CE, 0x16E3,
	0x24F, 0x200, 0xFCCB, 0xF535, 0xE00, 0x1000} }
};

static bool setup_scaling_configuration(
	struct dce_transform *xfm_dce,
	const struct scaler_data *data)
{
	REG_SET(SCL_BYPASS_CONTROL, 0, SCL_BYPASS_MODE, 0);

	if (data->taps.h_taps + data->taps.v_taps <= 2) {
		/* Set bypass */
		if (xfm_dce->xfm_mask->SCL_PSCL_EN != 0)
			REG_UPDATE_2(SCL_MODE, SCL_MODE, 0, SCL_PSCL_EN, 0);
		else
			REG_UPDATE(SCL_MODE, SCL_MODE, 0);
		return false;
	}

	REG_SET_2(SCL_TAP_CONTROL, 0,
			SCL_H_NUM_OF_TAPS, data->taps.h_taps - 1,
			SCL_V_NUM_OF_TAPS, data->taps.v_taps - 1);

	if (data->format <= PIXEL_FORMAT_GRPH_END)
		REG_UPDATE(SCL_MODE, SCL_MODE, 1);
	else
		REG_UPDATE(SCL_MODE, SCL_MODE, 2);

	if (xfm_dce->xfm_mask->SCL_PSCL_EN != 0)
		REG_UPDATE(SCL_MODE, SCL_PSCL_EN, 1);

	/* 1 - Replace out of bound pixels with edge */
	REG_SET(SCL_CONTROL, 0, SCL_BOUNDARY_MODE, 1);

	return true;
}

static void program_overscan(
		struct dce_transform *xfm_dce,
		const struct scaler_data *data)
{
	int overscan_right = data->h_active
			- data->recout.x - data->recout.width;
	int overscan_bottom = data->v_active
			- data->recout.y - data->recout.height;

	if (xfm_dce->base.ctx->dc->debug.surface_visual_confirm) {
		overscan_bottom += 2;
		overscan_right += 2;
	}

	if (overscan_right < 0) {
		BREAK_TO_DEBUGGER();
		overscan_right = 0;
	}
	if (overscan_bottom < 0) {
		BREAK_TO_DEBUGGER();
		overscan_bottom = 0;
	}

	REG_SET_2(EXT_OVERSCAN_LEFT_RIGHT, 0,
			EXT_OVERSCAN_LEFT, data->recout.x,
			EXT_OVERSCAN_RIGHT, overscan_right);
	REG_SET_2(EXT_OVERSCAN_TOP_BOTTOM, 0,
			EXT_OVERSCAN_TOP, data->recout.y,
			EXT_OVERSCAN_BOTTOM, overscan_bottom);
}

static void program_multi_taps_filter(
	struct dce_transform *xfm_dce,
	int taps,
	const uint16_t *coeffs,
	enum ram_filter_type filter_type)
{
	int phase, pair;
	int array_idx = 0;
	int taps_pairs = (taps + 1) / 2;
	int phases_to_program = SCL_PHASES / 2 + 1;

	uint32_t power_ctl = 0;

	if (!coeffs)
		return;

	/*We need to disable power gating on coeff memory to do programming*/
	if (REG(DCFE_MEM_PWR_CTRL)) {
		power_ctl = REG_READ(DCFE_MEM_PWR_CTRL);
		REG_SET(DCFE_MEM_PWR_CTRL, power_ctl, SCL_COEFF_MEM_PWR_DIS, 1);

		REG_WAIT(DCFE_MEM_PWR_STATUS, SCL_COEFF_MEM_PWR_STATE, 0, 1, 10);
	}
	for (phase = 0; phase < phases_to_program; phase++) {
		/*we always program N/2 + 1 phases, total phases N, but N/2-1 are just mirror
		phase 0 is unique and phase N/2 is unique if N is even*/
		for (pair = 0; pair < taps_pairs; pair++) {
			uint16_t odd_coeff = 0;
			uint16_t even_coeff = coeffs[array_idx];

			REG_SET_3(SCL_COEF_RAM_SELECT, 0,
					SCL_C_RAM_FILTER_TYPE, filter_type,
					SCL_C_RAM_PHASE, phase,
					SCL_C_RAM_TAP_PAIR_IDX, pair);

			if (taps % 2 && pair == taps_pairs - 1)
				array_idx++;
			else {
				odd_coeff = coeffs[array_idx + 1];
				array_idx += 2;
			}

			REG_SET_4(SCL_COEF_RAM_TAP_DATA, 0,
					SCL_C_RAM_EVEN_TAP_COEF_EN, 1,
					SCL_C_RAM_EVEN_TAP_COEF, even_coeff,
					SCL_C_RAM_ODD_TAP_COEF_EN, 1,
					SCL_C_RAM_ODD_TAP_COEF, odd_coeff);
		}
	}

	/*We need to restore power gating on coeff memory to initial state*/
	if (REG(DCFE_MEM_PWR_CTRL))
		REG_WRITE(DCFE_MEM_PWR_CTRL, power_ctl);
}

static void program_viewport(
	struct dce_transform *xfm_dce,
	const struct rect *view_port)
{
	REG_SET_2(VIEWPORT_START, 0,
			VIEWPORT_X_START, view_port->x,
			VIEWPORT_Y_START, view_port->y);

	REG_SET_2(VIEWPORT_SIZE, 0,
			VIEWPORT_HEIGHT, view_port->height,
			VIEWPORT_WIDTH, view_port->width);

	/* TODO: add stereo support */
}

static void calculate_inits(
	struct dce_transform *xfm_dce,
	const struct scaler_data *data,
	struct scl_ratios_inits *inits)
{
	struct fixed31_32 h_init;
	struct fixed31_32 v_init;

	inits->h_int_scale_ratio =
		dal_fixed31_32_u2d19(data->ratios.horz) << 5;
	inits->v_int_scale_ratio =
		dal_fixed31_32_u2d19(data->ratios.vert) << 5;

	h_init =
		dal_fixed31_32_div_int(
			dal_fixed31_32_add(
				data->ratios.horz,
				dal_fixed31_32_from_int(data->taps.h_taps + 1)),
				2);
	inits->h_init.integer = dal_fixed31_32_floor(h_init);
	inits->h_init.fraction = dal_fixed31_32_u0d19(h_init) << 5;

	v_init =
		dal_fixed31_32_div_int(
			dal_fixed31_32_add(
				data->ratios.vert,
				dal_fixed31_32_from_int(data->taps.v_taps + 1)),
				2);
	inits->v_init.integer = dal_fixed31_32_floor(v_init);
	inits->v_init.fraction = dal_fixed31_32_u0d19(v_init) << 5;
}

static void program_scl_ratios_inits(
	struct dce_transform *xfm_dce,
	struct scl_ratios_inits *inits)
{

	REG_SET(SCL_HORZ_FILTER_SCALE_RATIO, 0,
			SCL_H_SCALE_RATIO, inits->h_int_scale_ratio);

	REG_SET(SCL_VERT_FILTER_SCALE_RATIO, 0,
			SCL_V_SCALE_RATIO, inits->v_int_scale_ratio);

	REG_SET_2(SCL_HORZ_FILTER_INIT, 0,
			SCL_H_INIT_INT, inits->h_init.integer,
			SCL_H_INIT_FRAC, inits->h_init.fraction);

	REG_SET_2(SCL_VERT_FILTER_INIT, 0,
			SCL_V_INIT_INT, inits->v_init.integer,
			SCL_V_INIT_FRAC, inits->v_init.fraction);

	REG_WRITE(SCL_AUTOMATIC_MODE_CONTROL, 0);
}

static const uint16_t *get_filter_coeffs_16p(int taps, struct fixed31_32 ratio)
{
	if (taps == 4)
		return get_filter_4tap_16p(ratio);
	else if (taps == 3)
		return get_filter_3tap_16p(ratio);
	else if (taps == 2)
		return get_filter_2tap_16p();
	else if (taps == 1)
		return NULL;
	else {
		/* should never happen, bug */
		BREAK_TO_DEBUGGER();
		return NULL;
	}
}

static void dce_transform_set_scaler(
	struct transform *xfm,
	const struct scaler_data *data)
{
	struct dce_transform *xfm_dce = TO_DCE_TRANSFORM(xfm);
	bool is_scaling_required;
	bool filter_updated = false;
	const uint16_t *coeffs_v, *coeffs_h;

	/*Use all three pieces of memory always*/
	REG_SET_2(LB_MEMORY_CTRL, 0,
			LB_MEMORY_CONFIG, 0,
			LB_MEMORY_SIZE, xfm_dce->lb_memory_size);

	/* Clear SCL_F_SHARP_CONTROL value to 0 */
	REG_WRITE(SCL_F_SHARP_CONTROL, 0);

	/* 1. Program overscan */
	program_overscan(xfm_dce, data);

	/* 2. Program taps and configuration */
	is_scaling_required = setup_scaling_configuration(xfm_dce, data);

	if (is_scaling_required) {
		/* 3. Calculate and program ratio, filter initialization */
		struct scl_ratios_inits inits = { 0 };

		calculate_inits(xfm_dce, data, &inits);

		program_scl_ratios_inits(xfm_dce, &inits);

		coeffs_v = get_filter_coeffs_16p(data->taps.v_taps, data->ratios.vert);
		coeffs_h = get_filter_coeffs_16p(data->taps.h_taps, data->ratios.horz);

		if (coeffs_v != xfm_dce->filter_v || coeffs_h != xfm_dce->filter_h) {
			/* 4. Program vertical filters */
			if (xfm_dce->filter_v == NULL)
				REG_SET(SCL_VERT_FILTER_CONTROL, 0,
						SCL_V_2TAP_HARDCODE_COEF_EN, 0);
			program_multi_taps_filter(
					xfm_dce,
					data->taps.v_taps,
					coeffs_v,
					FILTER_TYPE_RGB_Y_VERTICAL);
			program_multi_taps_filter(
					xfm_dce,
					data->taps.v_taps,
					coeffs_v,
					FILTER_TYPE_ALPHA_VERTICAL);

			/* 5. Program horizontal filters */
			if (xfm_dce->filter_h == NULL)
				REG_SET(SCL_HORZ_FILTER_CONTROL, 0,
						SCL_H_2TAP_HARDCODE_COEF_EN, 0);
			program_multi_taps_filter(
					xfm_dce,
					data->taps.h_taps,
					coeffs_h,
					FILTER_TYPE_RGB_Y_HORIZONTAL);
			program_multi_taps_filter(
					xfm_dce,
					data->taps.h_taps,
					coeffs_h,
					FILTER_TYPE_ALPHA_HORIZONTAL);

			xfm_dce->filter_v = coeffs_v;
			xfm_dce->filter_h = coeffs_h;
			filter_updated = true;
		}
	}

	/* 6. Program the viewport */
	program_viewport(xfm_dce, &data->viewport);

	/* 7. Set bit to flip to new coefficient memory */
	if (filter_updated)
		REG_UPDATE(SCL_UPDATE, SCL_COEF_UPDATE_COMPLETE, 1);

	REG_UPDATE(LB_DATA_FORMAT, ALPHA_EN, data->lb_params.alpha_en);
}

/*****************************************************************************
 * set_clamp
 *
 * @param depth : bit depth to set the clamp to (should match denorm)
 *
 * @brief
 *     Programs clamp according to panel bit depth.
 *
 *******************************************************************************/
static void set_clamp(
	struct dce_transform *xfm_dce,
	enum dc_color_depth depth)
{
	int clamp_max = 0;

	/* At the clamp block the data will be MSB aligned, so we set the max
	 * clamp accordingly.
	 * For example, the max value for 6 bits MSB aligned (14 bit bus) would
	 * be "11 1111 0000 0000" in binary, so 0x3F00.
	 */
	switch (depth) {
	case COLOR_DEPTH_666:
		/* 6bit MSB aligned on 14 bit bus '11 1111 0000 0000' */
		clamp_max = 0x3F00;
		break;
	case COLOR_DEPTH_888:
		/* 8bit MSB aligned on 14 bit bus '11 1111 1100 0000' */
		clamp_max = 0x3FC0;
		break;
	case COLOR_DEPTH_101010:
		/* 10bit MSB aligned on 14 bit bus '11 1111 1111 1100' */
		clamp_max = 0x3FFC;
		break;
	case COLOR_DEPTH_121212:
		/* 12bit MSB aligned on 14 bit bus '11 1111 1111 1111' */
		clamp_max = 0x3FFF;
		break;
	default:
		clamp_max = 0x3FC0;
		BREAK_TO_DEBUGGER(); /* Invalid clamp bit depth */
	}
	REG_SET_2(OUT_CLAMP_CONTROL_B_CB, 0,
			OUT_CLAMP_MIN_B_CB, 0,
			OUT_CLAMP_MAX_B_CB, clamp_max);

	REG_SET_2(OUT_CLAMP_CONTROL_G_Y, 0,
			OUT_CLAMP_MIN_G_Y, 0,
			OUT_CLAMP_MAX_G_Y, clamp_max);

	REG_SET_2(OUT_CLAMP_CONTROL_R_CR, 0,
			OUT_CLAMP_MIN_R_CR, 0,
			OUT_CLAMP_MAX_R_CR, clamp_max);
}

/*******************************************************************************
 * set_round
 *
 * @brief
 *     Programs Round/Truncate
 *
 * @param [in] mode  :round or truncate
 * @param [in] depth :bit depth to round/truncate to
 OUT_ROUND_TRUNC_MODE 3:0 0xA Output data round or truncate mode
 POSSIBLE VALUES:
      00 - truncate to u0.12
      01 - truncate to u0.11
      02 - truncate to u0.10
      03 - truncate to u0.9
      04 - truncate to u0.8
      05 - reserved
      06 - truncate to u0.14
      07 - truncate to u0.13		set_reg_field_value(
			value,
			clamp_max,
			OUT_CLAMP_CONTROL_R_CR,
			OUT_CLAMP_MAX_R_CR);
      08 - round to u0.12
      09 - round to u0.11
      10 - round to u0.10
      11 - round to u0.9
      12 - round to u0.8
      13 - reserved
      14 - round to u0.14
      15 - round to u0.13

 ******************************************************************************/
static void set_round(
	struct dce_transform *xfm_dce,
	enum dcp_out_trunc_round_mode mode,
	enum dcp_out_trunc_round_depth depth)
{
	int depth_bits = 0;
	int mode_bit = 0;

	/*  set up bit depth */
	switch (depth) {
	case DCP_OUT_TRUNC_ROUND_DEPTH_14BIT:
		depth_bits = 6;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_13BIT:
		depth_bits = 7;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_12BIT:
		depth_bits = 0;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_11BIT:
		depth_bits = 1;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_10BIT:
		depth_bits = 2;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_9BIT:
		depth_bits = 3;
		break;
	case DCP_OUT_TRUNC_ROUND_DEPTH_8BIT:
		depth_bits = 4;
		break;
	default:
		depth_bits = 4;
		BREAK_TO_DEBUGGER(); /* Invalid dcp_out_trunc_round_depth */
	}

	/*  set up round or truncate */
	switch (mode) {
	case DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE:
		mode_bit = 0;
		break;
	case DCP_OUT_TRUNC_ROUND_MODE_ROUND:
		mode_bit = 1;
		break;
	default:
		BREAK_TO_DEBUGGER(); /* Invalid dcp_out_trunc_round_mode */
	}

	depth_bits |= mode_bit << 3;

	REG_SET(OUT_ROUND_CONTROL, 0, OUT_ROUND_TRUNC_MODE, depth_bits);
}

/*****************************************************************************
 * set_dither
 *
 * @brief
 *     Programs Dither
 *
 * @param [in] dither_enable        : enable dither
 * @param [in] dither_mode           : dither mode to set
 * @param [in] dither_depth          : bit depth to dither to
 * @param [in] frame_random_enable    : enable frame random
 * @param [in] rgb_random_enable      : enable rgb random
 * @param [in] highpass_random_enable : enable highpass random
 *
 ******************************************************************************/

static void set_dither(
	struct dce_transform *xfm_dce,
	bool dither_enable,
	enum dcp_spatial_dither_mode dither_mode,
	enum dcp_spatial_dither_depth dither_depth,
	bool frame_random_enable,
	bool rgb_random_enable,
	bool highpass_random_enable)
{
	int dither_depth_bits = 0;
	int dither_mode_bits = 0;

	switch (dither_mode) {
	case DCP_SPATIAL_DITHER_MODE_AAAA:
		dither_mode_bits = 0;
		break;
	case DCP_SPATIAL_DITHER_MODE_A_AA_A:
		dither_mode_bits = 1;
		break;
	case DCP_SPATIAL_DITHER_MODE_AABBAABB:
		dither_mode_bits = 2;
		break;
	case DCP_SPATIAL_DITHER_MODE_AABBCCAABBCC:
		dither_mode_bits = 3;
		break;
	default:
		/* Invalid dcp_spatial_dither_mode */
		BREAK_TO_DEBUGGER();
	}

	switch (dither_depth) {
	case DCP_SPATIAL_DITHER_DEPTH_30BPP:
		dither_depth_bits = 0;
		break;
	case DCP_SPATIAL_DITHER_DEPTH_24BPP:
		dither_depth_bits = 1;
		break;
	default:
		/* Invalid dcp_spatial_dither_depth */
		BREAK_TO_DEBUGGER();
	}

	/*  write the register */
	REG_SET_6(DCP_SPATIAL_DITHER_CNTL, 0,
			DCP_SPATIAL_DITHER_EN, dither_enable,
			DCP_SPATIAL_DITHER_MODE, dither_mode_bits,
			DCP_SPATIAL_DITHER_DEPTH, dither_depth_bits,
			DCP_FRAME_RANDOM_ENABLE, frame_random_enable,
			DCP_RGB_RANDOM_ENABLE, rgb_random_enable,
			DCP_HIGHPASS_RANDOM_ENABLE, highpass_random_enable);
}

/*****************************************************************************
 * dce_transform_bit_depth_reduction_program
 *
 * @brief
 *     Programs the DCP bit depth reduction registers (Clamp, Round/Truncate,
 *      Dither) for dce
 *
 * @param depth : bit depth to set the clamp to (should match denorm)
 *
 ******************************************************************************/
static void program_bit_depth_reduction(
	struct dce_transform *xfm_dce,
	enum dc_color_depth depth,
	const struct bit_depth_reduction_params *bit_depth_params)
{
	enum dcp_out_trunc_round_depth trunc_round_depth;
	enum dcp_out_trunc_round_mode trunc_mode;
	bool spatial_dither_enable;

	ASSERT(depth < COLOR_DEPTH_121212); /* Invalid clamp bit depth */

	spatial_dither_enable = bit_depth_params->flags.SPATIAL_DITHER_ENABLED;
	/* Default to 12 bit truncation without rounding */
	trunc_round_depth = DCP_OUT_TRUNC_ROUND_DEPTH_12BIT;
	trunc_mode = DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE;

	if (bit_depth_params->flags.TRUNCATE_ENABLED) {
		/* Don't enable dithering if truncation is enabled */
		spatial_dither_enable = false;
		trunc_mode = bit_depth_params->flags.TRUNCATE_MODE ?
			     DCP_OUT_TRUNC_ROUND_MODE_ROUND :
			     DCP_OUT_TRUNC_ROUND_MODE_TRUNCATE;

		if (bit_depth_params->flags.TRUNCATE_DEPTH == 0 ||
		    bit_depth_params->flags.TRUNCATE_DEPTH == 1)
			trunc_round_depth = DCP_OUT_TRUNC_ROUND_DEPTH_8BIT;
		else if (bit_depth_params->flags.TRUNCATE_DEPTH == 2)
			trunc_round_depth = DCP_OUT_TRUNC_ROUND_DEPTH_10BIT;
		else {
			/*
			 * Invalid truncate/round depth. Setting here to 12bit
			 * to prevent use-before-initialize errors.
			 */
			trunc_round_depth = DCP_OUT_TRUNC_ROUND_DEPTH_12BIT;
			BREAK_TO_DEBUGGER();
		}
	}

	set_clamp(xfm_dce, depth);
	set_round(xfm_dce, trunc_mode, trunc_round_depth);
	set_dither(xfm_dce,
		   spatial_dither_enable,
		   DCP_SPATIAL_DITHER_MODE_A_AA_A,
		   DCP_SPATIAL_DITHER_DEPTH_30BPP,
		   bit_depth_params->flags.FRAME_RANDOM,
		   bit_depth_params->flags.RGB_RANDOM,
		   bit_depth_params->flags.HIGHPASS_RANDOM);
}

static int dce_transform_get_max_num_of_supported_lines(
	struct dce_transform *xfm_dce,
	enum lb_pixel_depth depth,
	int pixel_width)
{
	int pixels_per_entries = 0;
	int max_pixels_supports = 0;

	ASSERT(pixel_width);

	/* Find number of pixels that can fit into a single LB entry and
	 * take floor of the value since we cannot store a single pixel
	 * across multiple entries. */
	switch (depth) {
	case LB_PIXEL_DEPTH_18BPP:
		pixels_per_entries = xfm_dce->lb_bits_per_entry / 18;
		break;

	case LB_PIXEL_DEPTH_24BPP:
		pixels_per_entries = xfm_dce->lb_bits_per_entry / 24;
		break;

	case LB_PIXEL_DEPTH_30BPP:
		pixels_per_entries = xfm_dce->lb_bits_per_entry / 30;
		break;

	case LB_PIXEL_DEPTH_36BPP:
		pixels_per_entries = xfm_dce->lb_bits_per_entry / 36;
		break;

	default:
		DC_LOG_WARNING("%s: Invalid LB pixel depth",
			__func__);
		BREAK_TO_DEBUGGER();
		break;
	}

	ASSERT(pixels_per_entries);

	max_pixels_supports =
			pixels_per_entries *
			xfm_dce->lb_memory_size;

	return (max_pixels_supports / pixel_width);
}

static void set_denormalization(
	struct dce_transform *xfm_dce,
	enum dc_color_depth depth)
{
	int denorm_mode = 0;

	switch (depth) {
	case COLOR_DEPTH_666:
		/* 63/64 for 6 bit output color depth */
		denorm_mode = 1;
		break;
	case COLOR_DEPTH_888:
		/* Unity for 8 bit output color depth
		 * because prescale is disabled by default */
		denorm_mode = 0;
		break;
	case COLOR_DEPTH_101010:
		/* 1023/1024 for 10 bit output color depth */
		denorm_mode = 3;
		break;
	case COLOR_DEPTH_121212:
		/* 4095/4096 for 12 bit output color depth */
		denorm_mode = 5;
		break;
	case COLOR_DEPTH_141414:
	case COLOR_DEPTH_161616:
	default:
		/* not valid used case! */
		break;
	}

	REG_SET(DENORM_CONTROL, 0, DENORM_MODE, denorm_mode);
}

static void dce_transform_set_pixel_storage_depth(
	struct transform *xfm,
	enum lb_pixel_depth depth,
	const struct bit_depth_reduction_params *bit_depth_params)
{
	struct dce_transform *xfm_dce = TO_DCE_TRANSFORM(xfm);
	int pixel_depth, expan_mode;
	enum dc_color_depth color_depth;

	switch (depth) {
	case LB_PIXEL_DEPTH_18BPP:
		color_depth = COLOR_DEPTH_666;
		pixel_depth = 2;
		expan_mode  = 1;
		break;
	case LB_PIXEL_DEPTH_24BPP:
		color_depth = COLOR_DEPTH_888;
		pixel_depth = 1;
		expan_mode  = 1;
		break;
	case LB_PIXEL_DEPTH_30BPP:
		color_depth = COLOR_DEPTH_101010;
		pixel_depth = 0;
		expan_mode  = 1;
		break;
	case LB_PIXEL_DEPTH_36BPP:
		color_depth = COLOR_DEPTH_121212;
		pixel_depth = 3;
		expan_mode  = 0;
		break;
	default:
		color_depth = COLOR_DEPTH_101010;
		pixel_depth = 0;
		expan_mode  = 1;
		BREAK_TO_DEBUGGER();
		break;
	}

	set_denormalization(xfm_dce, color_depth);
	program_bit_depth_reduction(xfm_dce, color_depth, bit_depth_params);

	REG_UPDATE_2(LB_DATA_FORMAT,
			PIXEL_DEPTH, pixel_depth,
			PIXEL_EXPAN_MODE, expan_mode);

	if (!(xfm_dce->lb_pixel_depth_supported & depth)) {
		/*we should use unsupported capabilities
		 *  unless it is required by w/a*/
		DC_LOG_WARNING("%s: Capability not supported",
			__func__);
	}
}

static void program_gamut_remap(
	struct dce_transform *xfm_dce,
	const uint16_t *reg_val)
{
	if (reg_val) {
		REG_SET_2(GAMUT_REMAP_C11_C12, 0,
				GAMUT_REMAP_C11, reg_val[0],
				GAMUT_REMAP_C12, reg_val[1]);
		REG_SET_2(GAMUT_REMAP_C13_C14, 0,
				GAMUT_REMAP_C13, reg_val[2],
				GAMUT_REMAP_C14, reg_val[3]);
		REG_SET_2(GAMUT_REMAP_C21_C22, 0,
				GAMUT_REMAP_C21, reg_val[4],
				GAMUT_REMAP_C22, reg_val[5]);
		REG_SET_2(GAMUT_REMAP_C23_C24, 0,
				GAMUT_REMAP_C23, reg_val[6],
				GAMUT_REMAP_C24, reg_val[7]);
		REG_SET_2(GAMUT_REMAP_C31_C32, 0,
				GAMUT_REMAP_C31, reg_val[8],
				GAMUT_REMAP_C32, reg_val[9]);
		REG_SET_2(GAMUT_REMAP_C33_C34, 0,
				GAMUT_REMAP_C33, reg_val[10],
				GAMUT_REMAP_C34, reg_val[11]);

		REG_SET(GAMUT_REMAP_CONTROL, 0, GRPH_GAMUT_REMAP_MODE, 1);
	} else
		REG_SET(GAMUT_REMAP_CONTROL, 0, GRPH_GAMUT_REMAP_MODE, 0);

}

/**
 *****************************************************************************
 *  Function: dal_transform_wide_gamut_set_gamut_remap
 *
 *  @param [in] const struct xfm_grph_csc_adjustment *adjust
 *
 *  @return
 *     void
 *
 *  @note calculate and apply color temperature adjustment to in Rgb color space
 *
 *  @see
 *
 *****************************************************************************
 */
static void dce_transform_set_gamut_remap(
	struct transform *xfm,
	const struct xfm_grph_csc_adjustment *adjust)
{
	struct dce_transform *xfm_dce = TO_DCE_TRANSFORM(xfm);
	int i = 0;

	if (adjust->gamut_adjust_type != GRAPHICS_GAMUT_ADJUST_TYPE_SW)
		/* Bypass if type is bypass or hw */
		program_gamut_remap(xfm_dce, NULL);
	else {
		struct fixed31_32 arr_matrix[GAMUT_MATRIX_SIZE];
		uint16_t arr_reg_val[GAMUT_MATRIX_SIZE];

		for (i = 0; i < GAMUT_MATRIX_SIZE; i++)
			arr_matrix[i] = adjust->temperature_matrix[i];

		convert_float_matrix(
			arr_reg_val, arr_matrix, GAMUT_MATRIX_SIZE);

		program_gamut_remap(xfm_dce, arr_reg_val);
	}
}

static uint32_t decide_taps(struct fixed31_32 ratio, uint32_t in_taps, bool chroma)
{
	uint32_t taps;

	if (IDENTITY_RATIO(ratio)) {
		return 1;
	} else if (in_taps != 0) {
		taps = in_taps;
	} else {
		taps = 4;
	}

	if (chroma) {
		taps /= 2;
		if (taps < 2)
			taps = 2;
	}

	return taps;
}


bool dce_transform_get_optimal_number_of_taps(
	struct transform *xfm,
	struct scaler_data *scl_data,
	const struct scaling_taps *in_taps)
{
	struct dce_transform *xfm_dce = TO_DCE_TRANSFORM(xfm);
	int pixel_width = scl_data->viewport.width;
	int max_num_of_lines;

	if (xfm_dce->prescaler_on &&
			(scl_data->viewport.width > scl_data->recout.width))
		pixel_width = scl_data->recout.width;

	max_num_of_lines = dce_transform_get_max_num_of_supported_lines(
		xfm_dce,
		scl_data->lb_params.depth,
		pixel_width);

	/* Fail if in_taps are impossible */
	if (in_taps->v_taps >= max_num_of_lines)
		return false;

	/*
	 * Set taps according to this policy (in this order)
	 * - Use 1 for no scaling
	 * - Use input taps
	 * - Use 4 and reduce as required by line buffer size
	 * - Decide chroma taps if chroma is scaled
	 *
	 * Ignore input chroma taps. Decide based on non-chroma
	 */
	scl_data->taps.h_taps = decide_taps(scl_data->ratios.horz, in_taps->h_taps, false);
	scl_data->taps.v_taps = decide_taps(scl_data->ratios.vert, in_taps->v_taps, false);
	scl_data->taps.h_taps_c = decide_taps(scl_data->ratios.horz_c, in_taps->h_taps, true);
	scl_data->taps.v_taps_c = decide_taps(scl_data->ratios.vert_c, in_taps->v_taps, true);

	if (!IDENTITY_RATIO(scl_data->ratios.vert)) {
		/* reduce v_taps if needed but ensure we have at least two */
		if (in_taps->v_taps == 0
				&& max_num_of_lines <= scl_data->taps.v_taps
				&& scl_data->taps.v_taps > 1) {
			scl_data->taps.v_taps = max_num_of_lines - 1;
		}

		if (scl_data->taps.v_taps <= 1)
			return false;
	}

	if (!IDENTITY_RATIO(scl_data->ratios.vert_c)) {
		/* reduce chroma v_taps if needed but ensure we have at least two */
		if (max_num_of_lines <= scl_data->taps.v_taps_c && scl_data->taps.v_taps_c > 1) {
			scl_data->taps.v_taps_c = max_num_of_lines - 1;
		}

		if (scl_data->taps.v_taps_c <= 1)
			return false;
	}

	/* we've got valid taps */
	return true;
}

static void dce_transform_reset(struct transform *xfm)
{
	struct dce_transform *xfm_dce = TO_DCE_TRANSFORM(xfm);

	xfm_dce->filter_h = NULL;
	xfm_dce->filter_v = NULL;
}

static void program_color_matrix(
	struct dce_transform *xfm_dce,
	const struct out_csc_color_matrix *tbl_entry,
	enum grph_color_adjust_option options)
{
	{
		REG_SET_2(OUTPUT_CSC_C11_C12, 0,
			OUTPUT_CSC_C11, tbl_entry->regval[0],
			OUTPUT_CSC_C12, tbl_entry->regval[1]);
	}
	{
		REG_SET_2(OUTPUT_CSC_C13_C14, 0,
			OUTPUT_CSC_C11, tbl_entry->regval[2],
			OUTPUT_CSC_C12, tbl_entry->regval[3]);
	}
	{
		REG_SET_2(OUTPUT_CSC_C21_C22, 0,
			OUTPUT_CSC_C11, tbl_entry->regval[4],
			OUTPUT_CSC_C12, tbl_entry->regval[5]);
	}
	{
		REG_SET_2(OUTPUT_CSC_C23_C24, 0,
			OUTPUT_CSC_C11, tbl_entry->regval[6],
			OUTPUT_CSC_C12, tbl_entry->regval[7]);
	}
	{
		REG_SET_2(OUTPUT_CSC_C31_C32, 0,
			OUTPUT_CSC_C11, tbl_entry->regval[8],
			OUTPUT_CSC_C12, tbl_entry->regval[9]);
	}
	{
		REG_SET_2(OUTPUT_CSC_C33_C34, 0,
			OUTPUT_CSC_C11, tbl_entry->regval[10],
			OUTPUT_CSC_C12, tbl_entry->regval[11]);
	}
}

static bool configure_graphics_mode(
	struct dce_transform *xfm_dce,
	enum csc_color_mode config,
	enum graphics_csc_adjust_type csc_adjust_type,
	enum dc_color_space color_space)
{
	REG_SET(OUTPUT_CSC_CONTROL, 0,
		OUTPUT_CSC_GRPH_MODE, 0);

	if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_SW) {
		if (config == CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC) {
			REG_SET(OUTPUT_CSC_CONTROL, 0,
				OUTPUT_CSC_GRPH_MODE, 4);
		} else {

			switch (color_space) {
			case COLOR_SPACE_SRGB:
				/* by pass */
				REG_SET(OUTPUT_CSC_CONTROL, 0,
					OUTPUT_CSC_GRPH_MODE, 0);
				break;
			case COLOR_SPACE_SRGB_LIMITED:
				/* TV RGB */
				REG_SET(OUTPUT_CSC_CONTROL, 0,
					OUTPUT_CSC_GRPH_MODE, 1);
				break;
			case COLOR_SPACE_YCBCR601:
			case COLOR_SPACE_YCBCR601_LIMITED:
				/* YCbCr601 */
				REG_SET(OUTPUT_CSC_CONTROL, 0,
					OUTPUT_CSC_GRPH_MODE, 2);
				break;
			case COLOR_SPACE_YCBCR709:
			case COLOR_SPACE_YCBCR709_LIMITED:
				/* YCbCr709 */
				REG_SET(OUTPUT_CSC_CONTROL, 0,
					OUTPUT_CSC_GRPH_MODE, 3);
				break;
			default:
				return false;
			}
		}
	} else if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_HW) {
		switch (color_space) {
		case COLOR_SPACE_SRGB:
			/* by pass */
			REG_SET(OUTPUT_CSC_CONTROL, 0,
				OUTPUT_CSC_GRPH_MODE, 0);
			break;
			break;
		case COLOR_SPACE_SRGB_LIMITED:
			/* TV RGB */
			REG_SET(OUTPUT_CSC_CONTROL, 0,
				OUTPUT_CSC_GRPH_MODE, 1);
			break;
		case COLOR_SPACE_YCBCR601:
		case COLOR_SPACE_YCBCR601_LIMITED:
			/* YCbCr601 */
			REG_SET(OUTPUT_CSC_CONTROL, 0,
				OUTPUT_CSC_GRPH_MODE, 2);
			break;
		case COLOR_SPACE_YCBCR709:
		case COLOR_SPACE_YCBCR709_LIMITED:
			 /* YCbCr709 */
			REG_SET(OUTPUT_CSC_CONTROL, 0,
				OUTPUT_CSC_GRPH_MODE, 3);
			break;
		default:
			return false;
		}

	} else
		/* by pass */
		REG_SET(OUTPUT_CSC_CONTROL, 0,
			OUTPUT_CSC_GRPH_MODE, 0);

	return true;
}

void dce110_opp_set_csc_adjustment(
	struct transform *xfm,
	const struct out_csc_color_matrix *tbl_entry)
{
	struct dce_transform *xfm_dce = TO_DCE_TRANSFORM(xfm);
	enum csc_color_mode config =
			CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC;

	program_color_matrix(
			xfm_dce, tbl_entry, GRPH_COLOR_MATRIX_SW);

	/*  We did everything ,now program DxOUTPUT_CSC_CONTROL */
	configure_graphics_mode(xfm_dce, config, GRAPHICS_CSC_ADJUST_TYPE_SW,
			tbl_entry->color_space);
}

void dce110_opp_set_csc_default(
	struct transform *xfm,
	const struct default_adjustment *default_adjust)
{
	struct dce_transform *xfm_dce = TO_DCE_TRANSFORM(xfm);
	enum csc_color_mode config =
			CSC_COLOR_MODE_GRAPHICS_PREDEFINED;

	if (default_adjust->force_hw_default == false) {
		const struct out_csc_color_matrix *elm;
		/* currently parameter not in use */
		enum grph_color_adjust_option option =
			GRPH_COLOR_MATRIX_HW_DEFAULT;
		uint32_t i;
		/*
		 * HW default false we program locally defined matrix
		 * HW default true  we use predefined hw matrix and we
		 * do not need to program matrix
		 * OEM wants the HW default via runtime parameter.
		 */
		option = GRPH_COLOR_MATRIX_SW;

		for (i = 0; i < ARRAY_SIZE(global_color_matrix); ++i) {
			elm = &global_color_matrix[i];
			if (elm->color_space != default_adjust->out_color_space)
				continue;
			/* program the matrix with default values from this
			 * file */
			program_color_matrix(xfm_dce, elm, option);
			config = CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC;
			break;
		}
	}

	/* configure the what we programmed :
	 * 1. Default values from this file
	 * 2. Use hardware default from ROM_A and we do not need to program
	 * matrix */

	configure_graphics_mode(xfm_dce, config,
		default_adjust->csc_adjust_type,
		default_adjust->out_color_space);
}

static void program_pwl(struct dce_transform *xfm_dce,
			const struct pwl_params *params)
{
	int retval;
	uint8_t max_tries = 10;
	uint8_t counter = 0;
	uint32_t i = 0;
	const struct pwl_result_data *rgb = params->rgb_resulted;

	/* Power on LUT memory */
	if (REG(DCFE_MEM_PWR_CTRL))
		REG_UPDATE(DCFE_MEM_PWR_CTRL,
			   DCP_REGAMMA_MEM_PWR_DIS, 1);
	else
		REG_UPDATE(DCFE_MEM_LIGHT_SLEEP_CNTL,
			   REGAMMA_LUT_LIGHT_SLEEP_DIS, 1);

	while (counter < max_tries) {
		if (REG(DCFE_MEM_PWR_STATUS)) {
			REG_GET(DCFE_MEM_PWR_STATUS,
				DCP_REGAMMA_MEM_PWR_STATE,
				&retval);

			if (retval == 0)
				break;
			++counter;
		} else {
			REG_GET(DCFE_MEM_LIGHT_SLEEP_CNTL,
				REGAMMA_LUT_MEM_PWR_STATE,
				&retval);

			if (retval == 0)
				break;
			++counter;
		}
	}

	if (counter == max_tries) {
		DC_LOG_WARNING("%s: regamma lut was not powered on "
				"in a timely manner,"
				" programming still proceeds\n",
				__func__);
	}

	REG_UPDATE(REGAMMA_LUT_WRITE_EN_MASK,
		   REGAMMA_LUT_WRITE_EN_MASK, 7);

	REG_WRITE(REGAMMA_LUT_INDEX, 0);

	/* Program REGAMMA_LUT_DATA */
	while (i != params->hw_points_num) {

		REG_WRITE(REGAMMA_LUT_DATA, rgb->red_reg);
		REG_WRITE(REGAMMA_LUT_DATA, rgb->green_reg);
		REG_WRITE(REGAMMA_LUT_DATA, rgb->blue_reg);
		REG_WRITE(REGAMMA_LUT_DATA, rgb->delta_red_reg);
		REG_WRITE(REGAMMA_LUT_DATA, rgb->delta_green_reg);
		REG_WRITE(REGAMMA_LUT_DATA, rgb->delta_blue_reg);

		++rgb;
		++i;
	}

	/*  we are done with DCP LUT memory; re-enable low power mode */
	if (REG(DCFE_MEM_PWR_CTRL))
		REG_UPDATE(DCFE_MEM_PWR_CTRL,
			   DCP_REGAMMA_MEM_PWR_DIS, 0);
	else
		REG_UPDATE(DCFE_MEM_LIGHT_SLEEP_CNTL,
			   REGAMMA_LUT_LIGHT_SLEEP_DIS, 0);
}

static void regamma_config_regions_and_segments(struct dce_transform *xfm_dce,
						const struct pwl_params *params)
{
	const struct gamma_curve *curve;

	REG_SET_2(REGAMMA_CNTLA_START_CNTL, 0,
		  REGAMMA_CNTLA_EXP_REGION_START, params->arr_points[0].custom_float_x,
		  REGAMMA_CNTLA_EXP_REGION_START_SEGMENT, 0);

	REG_SET(REGAMMA_CNTLA_SLOPE_CNTL, 0,
		REGAMMA_CNTLA_EXP_REGION_LINEAR_SLOPE, params->arr_points[0].custom_float_slope);

	REG_SET(REGAMMA_CNTLA_END_CNTL1, 0,
		REGAMMA_CNTLA_EXP_REGION_END, params->arr_points[1].custom_float_x);

	REG_SET_2(REGAMMA_CNTLA_END_CNTL2, 0,
		  REGAMMA_CNTLA_EXP_REGION_END_BASE, params->arr_points[1].custom_float_y,
		  REGAMMA_CNTLA_EXP_REGION_END_SLOPE, params->arr_points[1].custom_float_slope);

	curve = params->arr_curve_points;

	REG_SET_4(REGAMMA_CNTLA_REGION_0_1, 0,
		  REGAMMA_CNTLA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		  REGAMMA_CNTLA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		  REGAMMA_CNTLA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		  REGAMMA_CNTLA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
	curve += 2;

	REG_SET_4(REGAMMA_CNTLA_REGION_2_3, 0,
		  REGAMMA_CNTLA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		  REGAMMA_CNTLA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		  REGAMMA_CNTLA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		  REGAMMA_CNTLA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
	curve += 2;

	REG_SET_4(REGAMMA_CNTLA_REGION_4_5, 0,
		  REGAMMA_CNTLA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		  REGAMMA_CNTLA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		  REGAMMA_CNTLA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		  REGAMMA_CNTLA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
	curve += 2;

	REG_SET_4(REGAMMA_CNTLA_REGION_6_7, 0,
		  REGAMMA_CNTLA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		  REGAMMA_CNTLA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		  REGAMMA_CNTLA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		  REGAMMA_CNTLA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
	curve += 2;

	REG_SET_4(REGAMMA_CNTLA_REGION_8_9, 0,
		  REGAMMA_CNTLA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		  REGAMMA_CNTLA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		  REGAMMA_CNTLA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		  REGAMMA_CNTLA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
	curve += 2;

	REG_SET_4(REGAMMA_CNTLA_REGION_10_11, 0,
		  REGAMMA_CNTLA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		  REGAMMA_CNTLA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		  REGAMMA_CNTLA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		  REGAMMA_CNTLA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
	curve += 2;

	REG_SET_4(REGAMMA_CNTLA_REGION_12_13, 0,
		  REGAMMA_CNTLA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		  REGAMMA_CNTLA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		  REGAMMA_CNTLA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		  REGAMMA_CNTLA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
	curve += 2;

	REG_SET_4(REGAMMA_CNTLA_REGION_14_15, 0,
		  REGAMMA_CNTLA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		  REGAMMA_CNTLA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		  REGAMMA_CNTLA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		  REGAMMA_CNTLA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
}



void dce110_opp_program_regamma_pwl(struct transform *xfm,
				    const struct pwl_params *params)
{
	struct dce_transform *xfm_dce = TO_DCE_TRANSFORM(xfm);

	/* Setup regions */
	regamma_config_regions_and_segments(xfm_dce, params);

	/* Program PWL */
	program_pwl(xfm_dce, params);
}

void dce110_opp_power_on_regamma_lut(struct transform *xfm,
				     bool power_on)
{
	struct dce_transform *xfm_dce = TO_DCE_TRANSFORM(xfm);

	if (REG(DCFE_MEM_PWR_CTRL))
		REG_UPDATE_2(DCFE_MEM_PWR_CTRL,
			     DCP_REGAMMA_MEM_PWR_DIS, power_on,
			     DCP_LUT_MEM_PWR_DIS, power_on);
	else
		REG_UPDATE_2(DCFE_MEM_LIGHT_SLEEP_CNTL,
			    REGAMMA_LUT_LIGHT_SLEEP_DIS, power_on,
			    DCP_LUT_LIGHT_SLEEP_DIS, power_on);

}

void dce110_opp_set_regamma_mode(struct transform *xfm,
				 enum opp_regamma mode)
{
	struct dce_transform *xfm_dce = TO_DCE_TRANSFORM(xfm);

	REG_SET(REGAMMA_CONTROL, 0,
		GRPH_REGAMMA_MODE, mode);
}

static const struct transform_funcs dce_transform_funcs = {
	.transform_reset = dce_transform_reset,
	.transform_set_scaler = dce_transform_set_scaler,
	.transform_set_gamut_remap = dce_transform_set_gamut_remap,
	.opp_set_csc_adjustment = dce110_opp_set_csc_adjustment,
	.opp_set_csc_default = dce110_opp_set_csc_default,
	.opp_power_on_regamma_lut = dce110_opp_power_on_regamma_lut,
	.opp_program_regamma_pwl = dce110_opp_program_regamma_pwl,
	.opp_set_regamma_mode = dce110_opp_set_regamma_mode,
	.transform_set_pixel_storage_depth = dce_transform_set_pixel_storage_depth,
	.transform_get_optimal_number_of_taps = dce_transform_get_optimal_number_of_taps
};

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

void dce_transform_construct(
	struct dce_transform *xfm_dce,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce_transform_registers *regs,
	const struct dce_transform_shift *xfm_shift,
	const struct dce_transform_mask *xfm_mask)
{
	xfm_dce->base.ctx = ctx;

	xfm_dce->base.inst = inst;
	xfm_dce->base.funcs = &dce_transform_funcs;

	xfm_dce->regs = regs;
	xfm_dce->xfm_shift = xfm_shift;
	xfm_dce->xfm_mask = xfm_mask;

	xfm_dce->prescaler_on = true;
	xfm_dce->lb_pixel_depth_supported =
			LB_PIXEL_DEPTH_18BPP |
			LB_PIXEL_DEPTH_24BPP |
			LB_PIXEL_DEPTH_30BPP;

	xfm_dce->lb_bits_per_entry = LB_BITS_PER_ENTRY;
	xfm_dce->lb_memory_size = LB_TOTAL_NUMBER_OF_ENTRIES; /*0x6B0*/
}
