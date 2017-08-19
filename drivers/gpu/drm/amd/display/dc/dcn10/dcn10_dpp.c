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

#include "include/grph_object_id.h"
#include "include/fixed31_32.h"
#include "include/logger_interface.h"

#include "reg_helper.h"
#include "dcn10_dpp.h"
#include "basics/conversion.h"

#define NUM_PHASES    64
#define HORZ_MAX_TAPS 8
#define VERT_MAX_TAPS 8

#define BLACK_OFFSET_RGB_Y 0x0
#define BLACK_OFFSET_CBCR  0x8000

#define REG(reg)\
	xfm->tf_regs->reg

#define CTX \
	xfm->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	xfm->tf_shift->field_name, xfm->tf_mask->field_name

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

enum lb_memory_config {
	/* Enable all 3 pieces of memory */
	LB_MEMORY_CONFIG_0 = 0,

	/* Enable only the first piece of memory */
	LB_MEMORY_CONFIG_1 = 1,

	/* Enable only the second piece of memory */
	LB_MEMORY_CONFIG_2 = 2,

	/* Only applicable in 4:2:0 mode, enable all 3 pieces of memory and the
	 * last piece of chroma memory used for the luma storage
	 */
	LB_MEMORY_CONFIG_3 = 3
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
void dpp_set_gamut_remap_bypass(struct dcn10_dpp *xfm)
{
	REG_SET(CM_GAMUT_REMAP_CONTROL, 0,
			CM_GAMUT_REMAP_MODE, 0);
	/* Gamut remap in bypass */
}

#define IDENTITY_RATIO(ratio) (dal_fixed31_32_u2d19(ratio) == (1 << 19))


bool dpp_get_optimal_number_of_taps(
		struct transform *xfm,
		struct scaler_data *scl_data,
		const struct scaling_taps *in_taps)
{
	uint32_t pixel_width;

	if (scl_data->viewport.width > scl_data->recout.width)
		pixel_width = scl_data->recout.width;
	else
		pixel_width = scl_data->viewport.width;

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

	if (!xfm->ctx->dc->debug.always_scale) {
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

void dpp_reset(struct transform *xfm_base)
{
	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);

	xfm->filter_h_c = NULL;
	xfm->filter_v_c = NULL;
	xfm->filter_h = NULL;
	xfm->filter_v = NULL;

	/* set boundary mode to 0 */
	REG_SET(DSCL_CONTROL, 0, SCL_BOUNDARY_MODE, 0);
}



static bool dcn10_dpp_cm_set_regamma_pwl(
	struct transform *xfm_base, const struct pwl_params *params)
{
	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);

	dcn10_dpp_cm_power_on_regamma_lut(xfm_base, true);
	dcn10_dpp_cm_configure_regamma_lut(xfm_base, xfm->is_write_to_ram_a_safe);

	if (xfm->is_write_to_ram_a_safe)
		dcn10_dpp_cm_program_regamma_luta_settings(xfm_base, params);
	else
		dcn10_dpp_cm_program_regamma_lutb_settings(xfm_base, params);

	dcn10_dpp_cm_program_regamma_lut(
			xfm_base, params->rgb_resulted, params->hw_points_num);

	return true;
}

static void dcn10_dpp_cm_set_regamma_mode(
	struct transform *xfm_base,
	enum opp_regamma mode)
{
	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);
	uint32_t re_mode = 0;
	uint32_t obuf_bypass = 0; /* need for pipe split */
	uint32_t obuf_hupscale = 0;

	switch (mode) {
	case OPP_REGAMMA_BYPASS:
		re_mode = 0;
		break;
	case OPP_REGAMMA_SRGB:
		re_mode = 1;
		break;
	case OPP_REGAMMA_3_6:
		re_mode = 2;
		break;
	case OPP_REGAMMA_USER:
		re_mode = xfm->is_write_to_ram_a_safe ? 3 : 4;
		xfm->is_write_to_ram_a_safe = !xfm->is_write_to_ram_a_safe;
		break;
	default:
		break;
	}

	REG_SET(CM_RGAM_CONTROL, 0, CM_RGAM_LUT_MODE, re_mode);
	REG_UPDATE_2(OBUF_CONTROL,
			OBUF_BYPASS, obuf_bypass,
			OBUF_H_2X_UPSCALE_EN, obuf_hupscale);
}

static void ippn10_setup_format_flags(enum surface_pixel_format input_format,\
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

static void ippn10_set_degamma_format_float(
		struct transform *xfm_base,
		bool is_float)
{
	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);

	if (is_float) {
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_INPUT_FORMAT, 3);
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_LUT_MODE, 1);
	} else {
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_INPUT_FORMAT, 2);
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_LUT_MODE, 0);
	}
}

void ippn10_cnv_setup (
		struct transform *xfm_base,
		enum surface_pixel_format input_format,
		enum expansion_mode mode,
		enum ipp_output_format cnv_out_format)
{
	uint32_t pixel_format;
	uint32_t alpha_en;
	enum pixel_format_description fmt ;
	enum dc_color_space color_space;
	enum dcn10_input_csc_select select;
	bool is_float;
	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);
	bool force_disable_cursor = false;

	ippn10_setup_format_flags(input_format, &fmt);
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

	ippn10_set_degamma_format_float(xfm_base, is_float);

	switch (input_format) {
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

	ippn10_program_input_csc(xfm_base, color_space, select);

	if (force_disable_cursor) {
		REG_UPDATE(CURSOR_CONTROL,
				CURSOR_ENABLE, 0);
		REG_UPDATE(CURSOR0_CONTROL,
				CUR0_ENABLE, 0);
	}
}

static struct transform_funcs dcn10_dpp_funcs = {
		.transform_reset = dpp_reset,
		.transform_set_scaler = dcn10_dpp_dscl_set_scaler_manual_scale,
		.transform_get_optimal_number_of_taps = dpp_get_optimal_number_of_taps,
		.transform_set_gamut_remap = dcn10_dpp_cm_set_gamut_remap,
		.opp_set_csc_adjustment = dcn10_dpp_cm_set_output_csc_adjustment,
		.opp_set_csc_default = dcn10_dpp_cm_set_output_csc_default,
		.opp_power_on_regamma_lut = dcn10_dpp_cm_power_on_regamma_lut,
		.opp_program_regamma_lut = dcn10_dpp_cm_program_regamma_lut,
		.opp_configure_regamma_lut = dcn10_dpp_cm_configure_regamma_lut,
		.opp_program_regamma_lutb_settings = dcn10_dpp_cm_program_regamma_lutb_settings,
		.opp_program_regamma_luta_settings = dcn10_dpp_cm_program_regamma_luta_settings,
		.opp_program_regamma_pwl = dcn10_dpp_cm_set_regamma_pwl,
		.opp_set_regamma_mode = dcn10_dpp_cm_set_regamma_mode,
		.ipp_set_degamma = ippn10_set_degamma,
		.ipp_program_input_lut		= ippn10_program_input_lut,
		.ipp_program_degamma_pwl	= ippn10_set_degamma_pwl,
		.ipp_setup			= ippn10_cnv_setup,
		.ipp_full_bypass		= ippn10_full_bypass,
};


/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dcn10_dpp_construct(
	struct dcn10_dpp *xfm,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_dpp_registers *tf_regs,
	const struct dcn_dpp_shift *tf_shift,
	const struct dcn_dpp_mask *tf_mask)
{
	xfm->base.ctx = ctx;

	xfm->base.inst = inst;
	xfm->base.funcs = &dcn10_dpp_funcs;

	xfm->tf_regs = tf_regs;
	xfm->tf_shift = tf_shift;
	xfm->tf_mask = tf_mask;

	xfm->lb_pixel_depth_supported =
		LB_PIXEL_DEPTH_18BPP |
		LB_PIXEL_DEPTH_24BPP |
		LB_PIXEL_DEPTH_30BPP;

	xfm->lb_bits_per_entry = LB_BITS_PER_ENTRY;
	xfm->lb_memory_size = LB_TOTAL_NUMBER_OF_ENTRIES; /*0x1404*/

	return true;
}
