/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#include "dm_services.h"

#include "core_types.h"

#include "reg_helper.h"
#include "dcn10_dpp.h"
#include "basics/conversion.h"

#define NUM_PHASES    64
#define HORZ_MAX_TAPS 8
#define VERT_MAX_TAPS 8

#define BLACK_OFFSET_RGB_Y 0x0
#define BLACK_OFFSET_CBCR  0x8000

#define REG(reg)\
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name

enum pixel_format_description {
	PIXEL_FORMAT_FIXED = 0,
	PIXEL_FORMAT_FIXED16,
	PIXEL_FORMAT_FLOAT

};

enum dcn10_coef_filter_type_sel {
	SCL_COEF_LUMA_VERT_FILTER = 0,
	SCL_COEF_LUMA_HORZ_FILTER = 1,
	SCL_COEF_CHROMA_VERT_FILTER = 2,
	SCL_COEF_CHROMA_HORZ_FILTER = 3,
	SCL_COEF_ALPHA_VERT_FILTER = 4,
	SCL_COEF_ALPHA_HORZ_FILTER = 5
};

enum dscl_autocal_mode {
	AUTOCAL_MODE_OFF = 0,

	/* Autocal calculate the scaling ratio and initial phase and the
	 * DSCL_MODE_SEL must be set to 1
	 */
	AUTOCAL_MODE_AUTOSCALE = 1,
	/* Autocal perform auto centering without replication and the
	 * DSCL_MODE_SEL must be set to 0
	 */
	AUTOCAL_MODE_AUTOCENTER = 2,
	/* Autocal perform auto centering and auto replication and the
	 * DSCL_MODE_SEL must be set to 0
	 */
	AUTOCAL_MODE_AUTOREPLICATE = 3
};

enum dscl_mode_sel {
	DSCL_MODE_SCALING_444_BYPASS = 0,
	DSCL_MODE_SCALING_444_RGB_ENABLE = 1,
	DSCL_MODE_SCALING_444_YCBCR_ENABLE = 2,
	DSCL_MODE_SCALING_420_YCBCR_ENABLE = 3,
	DSCL_MODE_SCALING_420_LUMA_BYPASS = 4,
	DSCL_MODE_SCALING_420_CHROMA_BYPASS = 5,
	DSCL_MODE_DSCL_BYPASS = 6
};

enum gamut_remap_select {
	GAMUT_REMAP_BYPASS = 0,
	GAMUT_REMAP_COEFF,
	GAMUT_REMAP_COMA_COEFF,
	GAMUT_REMAP_COMB_COEFF
};

/* Program gamut remap in bypass mode */
void dpp_set_gamut_remap_bypass(struct dcn10_dpp *dpp)
{
	REG_SET(CM_GAMUT_REMAP_CONTROL, 0,
			CM_GAMUT_REMAP_MODE, 0);
	/* Gamut remap in bypass */
}

#define IDENTITY_RATIO(ratio) (dal_fixed31_32_u2d19(ratio) == (1 << 19))


bool dpp_get_optimal_number_of_taps(
		struct dpp *dpp,
		struct scaler_data *scl_data,
		const struct scaling_taps *in_taps)
{
	uint32_t pixel_width;

	if (scl_data->viewport.width > scl_data->recout.width)
		pixel_width = scl_data->recout.width;
	else
		pixel_width = scl_data->viewport.width;

	/* Some ASICs does not support  FP16 scaling, so we reject modes require this*/
	if (scl_data->viewport.width  != scl_data->h_active &&
		scl_data->viewport.height != scl_data->v_active &&
		dpp->caps->dscl_data_proc_format == DSCL_DATA_PRCESSING_FIXED_FORMAT &&
		scl_data->format == PIXEL_FORMAT_FP16)
		return false;

	/* TODO: add lb check */

	/* No support for programming ratio of 4, drop to 3.99999.. */
	if (scl_data->ratios.horz.value == (4ll << 32))
		scl_data->ratios.horz.value--;
	if (scl_data->ratios.vert.value == (4ll << 32))
		scl_data->ratios.vert.value--;
	if (scl_data->ratios.horz_c.value == (4ll << 32))
		scl_data->ratios.horz_c.value--;
	if (scl_data->ratios.vert_c.value == (4ll << 32))
		scl_data->ratios.vert_c.value--;

	/* Set default taps if none are provided */
	if (in_taps->h_taps == 0)
		scl_data->taps.h_taps = 4;
	else
		scl_data->taps.h_taps = in_taps->h_taps;
	if (in_taps->v_taps == 0)
		scl_data->taps.v_taps = 4;
	else
		scl_data->taps.v_taps = in_taps->v_taps;
	if (in_taps->v_taps_c == 0)
		scl_data->taps.v_taps_c = 2;
	else
		scl_data->taps.v_taps_c = in_taps->v_taps_c;
	if (in_taps->h_taps_c == 0)
		scl_data->taps.h_taps_c = 2;
	/* Only 1 and even h_taps_c are supported by hw */
	else if ((in_taps->h_taps_c % 2) != 0 && in_taps->h_taps_c != 1)
		scl_data->taps.h_taps_c = in_taps->h_taps_c - 1;
	else
		scl_data->taps.h_taps_c = in_taps->h_taps_c;

	if (!dpp->ctx->dc->debug.always_scale) {
		if (IDENTITY_RATIO(scl_data->ratios.horz))
			scl_data->taps.h_taps = 1;
		if (IDENTITY_RATIO(scl_data->ratios.vert))
			scl_data->taps.v_taps = 1;
		if (IDENTITY_RATIO(scl_data->ratios.horz_c))
			scl_data->taps.h_taps_c = 1;
		if (IDENTITY_RATIO(scl_data->ratios.vert_c))
			scl_data->taps.v_taps_c = 1;
	}

	return true;
}

void dpp_reset(struct dpp *dpp_base)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	dpp->filter_h_c = NULL;
	dpp->filter_v_c = NULL;
	dpp->filter_h = NULL;
	dpp->filter_v = NULL;

	memset(&dpp->scl_data, 0, sizeof(dpp->scl_data));
	memset(&dpp->pwl_data, 0, sizeof(dpp->pwl_data));
}



static void dpp1_cm_set_regamma_pwl(
	struct dpp *dpp_base, const struct pwl_params *params, enum opp_regamma mode)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	uint32_t re_mode = 0;

	switch (mode) {
	case OPP_REGAMMA_BYPASS:
		re_mode = 0;
		break;
	case OPP_REGAMMA_SRGB:
		re_mode = 1;
		break;
	case OPP_REGAMMA_XVYCC:
		re_mode = 2;
		break;
	case OPP_REGAMMA_USER:
		re_mode = dpp->is_write_to_ram_a_safe ? 4 : 3;
		if (memcmp(&dpp->pwl_data, params, sizeof(*params)) == 0)
			break;

		dpp1_cm_power_on_regamma_lut(dpp_base, true);
		dpp1_cm_configure_regamma_lut(dpp_base, dpp->is_write_to_ram_a_safe);

		if (dpp->is_write_to_ram_a_safe)
			dpp1_cm_program_regamma_luta_settings(dpp_base, params);
		else
			dpp1_cm_program_regamma_lutb_settings(dpp_base, params);

		dpp1_cm_program_regamma_lut(dpp_base, params->rgb_resulted,
					    params->hw_points_num);
		dpp->pwl_data = *params;

		re_mode = dpp->is_write_to_ram_a_safe ? 3 : 4;
		dpp->is_write_to_ram_a_safe = !dpp->is_write_to_ram_a_safe;
		break;
	default:
		break;
	}
	REG_SET(CM_RGAM_CONTROL, 0, CM_RGAM_LUT_MODE, re_mode);
}

static void dpp1_setup_format_flags(enum surface_pixel_format input_format,\
						enum pixel_format_description *fmt)
{

	if (input_format == SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F ||
		input_format == SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F)
		*fmt = PIXEL_FORMAT_FLOAT;
	else if (input_format == SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616)
		*fmt = PIXEL_FORMAT_FIXED16;
	else
		*fmt = PIXEL_FORMAT_FIXED;
}

static void dpp1_set_degamma_format_float(
		struct dpp *dpp_base,
		bool is_float)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	if (is_float) {
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_INPUT_FORMAT, 3);
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_LUT_MODE, 1);
	} else {
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_INPUT_FORMAT, 2);
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_LUT_MODE, 0);
	}
}

void dpp1_cnv_setup (
		struct dpp *dpp_base,
		enum surface_pixel_format format,
		enum expansion_mode mode,
		struct csc_transform input_csc_color_matrix,
		enum dc_color_space input_color_space)
{
	uint32_t pixel_format;
	uint32_t alpha_en;
	enum pixel_format_description fmt ;
	enum dc_color_space color_space;
	enum dcn10_input_csc_select select;
	bool is_float;
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	bool force_disable_cursor = false;
	struct out_csc_color_matrix tbl_entry;
	int i = 0;

	dpp1_setup_format_flags(format, &fmt);
	alpha_en = 1;
	pixel_format = 0;
	color_space = COLOR_SPACE_SRGB;
	select = INPUT_CSC_SELECT_BYPASS;
	is_float = false;

	switch (fmt) {
	case PIXEL_FORMAT_FIXED:
	case PIXEL_FORMAT_FIXED16:
	/*when output is float then FORMAT_CONTROL__OUTPUT_FP=1*/
		REG_SET_3(FORMAT_CONTROL, 0,
			CNVC_BYPASS, 0,
			FORMAT_EXPANSION_MODE, mode,
			OUTPUT_FP, 0);
		break;
	case PIXEL_FORMAT_FLOAT:
		REG_SET_3(FORMAT_CONTROL, 0,
			CNVC_BYPASS, 0,
			FORMAT_EXPANSION_MODE, mode,
			OUTPUT_FP, 1);
		is_float = true;
		break;
	default:

		break;
	}

	dpp1_set_degamma_format_float(dpp_base, is_float);

	switch (format) {
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
		pixel_format = 1;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		pixel_format = 3;
		alpha_en = 0;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
		pixel_format = 8;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		pixel_format = 10;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
		force_disable_cursor = false;
		pixel_format = 65;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		force_disable_cursor = true;
		pixel_format = 64;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
		force_disable_cursor = true;
		pixel_format = 67;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		force_disable_cursor = true;
		pixel_format = 66;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
		pixel_format = 22;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
		pixel_format = 24;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		pixel_format = 25;
		break;
	default:
		break;
	}
	REG_SET(CNVC_SURFACE_PIXEL_FORMAT, 0,
			CNVC_SURFACE_PIXEL_FORMAT, pixel_format);
	REG_UPDATE(FORMAT_CONTROL, FORMAT_CONTROL__ALPHA_EN, alpha_en);

	// if input adjustments exist, program icsc with those values

	if (input_csc_color_matrix.enable_adjustment
				== true) {
		for (i = 0; i < 12; i++)
			tbl_entry.regval[i] = input_csc_color_matrix.matrix[i];

		tbl_entry.color_space = input_color_space;

		if (color_space >= COLOR_SPACE_YCBCR601)
			select = INPUT_CSC_SELECT_ICSC;
		else
			select = INPUT_CSC_SELECT_BYPASS;

		dpp1_program_input_csc(dpp_base, color_space, select, &tbl_entry);
	} else
		dpp1_program_input_csc(dpp_base, color_space, select, NULL);

	if (force_disable_cursor) {
		REG_UPDATE(CURSOR_CONTROL,
				CURSOR_ENABLE, 0);
		REG_UPDATE(CURSOR0_CONTROL,
				CUR0_ENABLE, 0);
	}
}

void dpp1_set_cursor_attributes(
		struct dpp *dpp_base,
		enum dc_cursor_color_format color_format)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	REG_UPDATE_2(CURSOR0_CONTROL,
			CUR0_MODE, color_format,
			CUR0_EXPANSION_MODE, 0);

	if (color_format == CURSOR_MODE_MONO) {
		/* todo: clarify what to program these to */
		REG_UPDATE(CURSOR0_COLOR0,
				CUR0_COLOR0, 0x00000000);
		REG_UPDATE(CURSOR0_COLOR1,
				CUR0_COLOR1, 0xFFFFFFFF);
	}
}


void dpp1_set_cursor_position(
		struct dpp *dpp_base,
		const struct dc_cursor_position *pos,
		const struct dc_cursor_mi_param *param,
		uint32_t width)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	int src_x_offset = pos->x - pos->x_hotspot - param->viewport_x_start;
	uint32_t cur_en = pos->enable ? 1 : 0;

	if (src_x_offset >= (int)param->viewport_width)
		cur_en = 0;  /* not visible beyond right edge*/

	if (src_x_offset + (int)width <= 0)
		cur_en = 0;  /* not visible beyond left edge*/

	REG_UPDATE(CURSOR0_CONTROL,
			CUR0_ENABLE, cur_en);

}

void dpp1_dppclk_control(
		struct dpp *dpp_base,
		bool dppclk_div,
		bool enable)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	if (enable) {
		if (dpp->tf_mask->DPPCLK_RATE_CONTROL)
			REG_UPDATE_2(DPP_CONTROL,
				DPPCLK_RATE_CONTROL, dppclk_div,
				DPP_CLOCK_ENABLE, 1);
		else
			REG_UPDATE(DPP_CONTROL, DPP_CLOCK_ENABLE, 1);
	} else
		REG_UPDATE(DPP_CONTROL, DPP_CLOCK_ENABLE, 0);
}

static const struct dpp_funcs dcn10_dpp_funcs = {
		.dpp_reset = dpp_reset,
		.dpp_set_scaler = dpp1_dscl_set_scaler_manual_scale,
		.dpp_get_optimal_number_of_taps = dpp_get_optimal_number_of_taps,
		.dpp_set_gamut_remap = dpp1_cm_set_gamut_remap,
		.dpp_set_csc_adjustment = dpp1_cm_set_output_csc_adjustment,
		.dpp_set_csc_default = dpp1_cm_set_output_csc_default,
		.dpp_power_on_regamma_lut = dpp1_cm_power_on_regamma_lut,
		.dpp_program_regamma_lut = dpp1_cm_program_regamma_lut,
		.dpp_configure_regamma_lut = dpp1_cm_configure_regamma_lut,
		.dpp_program_regamma_lutb_settings = dpp1_cm_program_regamma_lutb_settings,
		.dpp_program_regamma_luta_settings = dpp1_cm_program_regamma_luta_settings,
		.dpp_program_regamma_pwl = dpp1_cm_set_regamma_pwl,
		.dpp_program_bias_and_scale = dpp1_program_bias_and_scale,
		.dpp_set_degamma = dpp1_set_degamma,
		.dpp_program_input_lut		= dpp1_program_input_lut,
		.dpp_program_degamma_pwl	= dpp1_set_degamma_pwl,
		.dpp_setup			= dpp1_cnv_setup,
		.dpp_full_bypass		= dpp1_full_bypass,
		.set_cursor_attributes = dpp1_set_cursor_attributes,
		.set_cursor_position = dpp1_set_cursor_position,
		.dpp_dppclk_control = dpp1_dppclk_control,
		.dpp_set_hdr_multiplier = dpp1_set_hdr_multiplier,
};

static struct dpp_caps dcn10_dpp_cap = {
	.dscl_data_proc_format = DSCL_DATA_PRCESSING_FIXED_FORMAT,
	.dscl_calc_lb_num_partitions = dpp1_dscl_calc_lb_num_partitions,
};

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

void dpp1_construct(
	struct dcn10_dpp *dpp,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_dpp_registers *tf_regs,
	const struct dcn_dpp_shift *tf_shift,
	const struct dcn_dpp_mask *tf_mask)
{
	dpp->base.ctx = ctx;

	dpp->base.inst = inst;
	dpp->base.funcs = &dcn10_dpp_funcs;
	dpp->base.caps = &dcn10_dpp_cap;

	dpp->tf_regs = tf_regs;
	dpp->tf_shift = tf_shift;
	dpp->tf_mask = tf_mask;

	dpp->lb_pixel_depth_supported =
		LB_PIXEL_DEPTH_18BPP |
		LB_PIXEL_DEPTH_24BPP |
		LB_PIXEL_DEPTH_30BPP;

	dpp->lb_bits_per_entry = LB_BITS_PER_ENTRY;
	dpp->lb_memory_size = LB_TOTAL_NUMBER_OF_ENTRIES; /*0x1404*/
}
