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
#include "dcn201_dpp.h"
#include "basics/conversion.h"

#define REG(reg)\
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name

static void dpp201_cnv_setup(
		struct dpp *dpp_base,
		enum surface_pixel_format format,
		enum expansion_mode mode,
		struct dc_csc_transform input_csc_color_matrix,
		enum dc_color_space input_color_space,
		struct cnv_alpha_2bit_lut *alpha_2bit_lut)
{
	struct dcn201_dpp *dpp = TO_DCN201_DPP(dpp_base);
	uint32_t pixel_format = 0;
	uint32_t alpha_en = 1;
	enum dc_color_space color_space = COLOR_SPACE_SRGB;
	enum dcn10_input_csc_select select = INPUT_CSC_SELECT_BYPASS;
	bool force_disable_cursor = false;
	uint32_t is_2bit = 0;

	REG_SET_2(FORMAT_CONTROL, 0,
		CNVC_BYPASS, 0,
		FORMAT_EXPANSION_MODE, mode);

	REG_UPDATE(FORMAT_CONTROL, FORMAT_CNV16, 0);
	REG_UPDATE(FORMAT_CONTROL, CNVC_BYPASS_MSB_ALIGN, 0);
	REG_UPDATE(FORMAT_CONTROL, CLAMP_POSITIVE, 0);
	REG_UPDATE(FORMAT_CONTROL, CLAMP_POSITIVE_C, 0);

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
		is_2bit = 1;
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
	case SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888:
		pixel_format = 12;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FIX:
		pixel_format = 112;
		alpha_en = 0;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX:
		pixel_format = 113;
		alpha_en = 0;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010:
		pixel_format = 114;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		is_2bit = 1;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA1010102:
		pixel_format = 115;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		is_2bit = 1;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT:
		pixel_format = 118;
		alpha_en = 0;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT:
		pixel_format = 119;
		alpha_en = 0;
		break;
	default:
		break;
	}

	/* Set default color space based on format if none is given. */
	color_space = input_color_space ? input_color_space : color_space;

	if (is_2bit == 1 && alpha_2bit_lut != NULL) {
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT0, alpha_2bit_lut->lut0);
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT1, alpha_2bit_lut->lut1);
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT2, alpha_2bit_lut->lut2);
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT3, alpha_2bit_lut->lut3);
	}

	REG_SET(CNVC_SURFACE_PIXEL_FORMAT, 0,
			CNVC_SURFACE_PIXEL_FORMAT, pixel_format);
	REG_UPDATE(FORMAT_CONTROL, FORMAT_CONTROL__ALPHA_EN, alpha_en);

	dpp1_program_input_csc(dpp_base, color_space, select, NULL);

	if (force_disable_cursor) {
		REG_UPDATE(CURSOR_CONTROL,
				CURSOR_ENABLE, 0);
		REG_UPDATE(CURSOR0_CONTROL,
				CUR0_ENABLE, 0);
	}
	dpp2_power_on_obuf(dpp_base, true);
}

#define IDENTITY_RATIO(ratio) (dc_fixpt_u3d19(ratio) == (1 << 19))

static bool dpp201_get_optimal_number_of_taps(
		struct dpp *dpp,
		struct scaler_data *scl_data,
		const struct scaling_taps *in_taps)
{
	if (scl_data->viewport.width  != scl_data->h_active &&
		scl_data->viewport.height != scl_data->v_active &&
		dpp->caps->dscl_data_proc_format == DSCL_DATA_PRCESSING_FIXED_FORMAT &&
		scl_data->format == PIXEL_FORMAT_FP16)
		return false;

	if (scl_data->viewport.width > scl_data->h_active &&
		dpp->ctx->dc->debug.max_downscale_src_width != 0 &&
		scl_data->viewport.width > dpp->ctx->dc->debug.max_downscale_src_width)
		return false;

	if (scl_data->ratios.horz.value == (8ll << 32))
		scl_data->ratios.horz.value--;
	if (scl_data->ratios.vert.value == (8ll << 32))
		scl_data->ratios.vert.value--;
	if (scl_data->ratios.horz_c.value == (8ll << 32))
		scl_data->ratios.horz_c.value--;
	if (scl_data->ratios.vert_c.value == (8ll << 32))
		scl_data->ratios.vert_c.value--;

	if (in_taps->h_taps == 0) {
		if (dc_fixpt_ceil(scl_data->ratios.horz) > 4)
			scl_data->taps.h_taps = 8;
		else
			scl_data->taps.h_taps = 4;
	} else
		scl_data->taps.h_taps = in_taps->h_taps;

	if (in_taps->v_taps == 0) {
		if (dc_fixpt_ceil(scl_data->ratios.vert) > 4)
			scl_data->taps.v_taps = 8;
		else
			scl_data->taps.v_taps = 4;
	} else
		scl_data->taps.v_taps = in_taps->v_taps;
	if (in_taps->v_taps_c == 0) {
		if (dc_fixpt_ceil(scl_data->ratios.vert_c) > 4)
			scl_data->taps.v_taps_c = 4;
		else
			scl_data->taps.v_taps_c = 2;
	} else
		scl_data->taps.v_taps_c = in_taps->v_taps_c;
	if (in_taps->h_taps_c == 0) {
		if (dc_fixpt_ceil(scl_data->ratios.horz_c) > 4)
			scl_data->taps.h_taps_c = 4;
		else
			scl_data->taps.h_taps_c = 2;
	} else if ((in_taps->h_taps_c % 2) != 0 && in_taps->h_taps_c != 1)
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

static struct dpp_funcs dcn201_dpp_funcs = {
	.dpp_read_state = dpp20_read_state,
	.dpp_reset = dpp_reset,
	.dpp_set_scaler = dpp1_dscl_set_scaler_manual_scale,
	.dpp_get_optimal_number_of_taps = dpp201_get_optimal_number_of_taps,
	.dpp_set_gamut_remap = dpp1_cm_set_gamut_remap,
	.dpp_set_csc_adjustment = NULL,
	.dpp_set_csc_default = NULL,
	.dpp_program_regamma_pwl = oppn20_dummy_program_regamma_pwl,
	.dpp_set_degamma = dpp2_set_degamma,
	.dpp_program_input_lut = dpp2_dummy_program_input_lut,
	.dpp_full_bypass = dpp1_full_bypass,
	.dpp_setup = dpp201_cnv_setup,
	.dpp_program_degamma_pwl = dpp2_set_degamma_pwl,
	.dpp_program_blnd_lut = dpp20_program_blnd_lut,
	.dpp_program_shaper_lut = dpp20_program_shaper,
	.dpp_program_3dlut = dpp20_program_3dlut,
	.dpp_program_bias_and_scale = NULL,
	.dpp_cnv_set_alpha_keyer = dpp2_cnv_set_alpha_keyer,
	.set_cursor_attributes = dpp2_set_cursor_attributes,
	.set_cursor_position = dpp1_set_cursor_position,
	.set_optional_cursor_attributes = dpp1_cnv_set_optional_cursor_attributes,
	.dpp_dppclk_control = dpp1_dppclk_control,
	.dpp_set_hdr_multiplier = dpp2_set_hdr_multiplier,
};

static struct dpp_caps dcn201_dpp_cap = {
	.dscl_data_proc_format = DSCL_DATA_PRCESSING_FLOAT_FORMAT,
	.dscl_calc_lb_num_partitions = dscl2_calc_lb_num_partitions,
};

bool dpp201_construct(
	struct dcn201_dpp *dpp,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn201_dpp_registers *tf_regs,
	const struct dcn201_dpp_shift *tf_shift,
	const struct dcn201_dpp_mask *tf_mask)
{
	dpp->base.ctx = ctx;

	dpp->base.inst = inst;
	dpp->base.funcs = &dcn201_dpp_funcs;
	dpp->base.caps = &dcn201_dpp_cap;

	dpp->tf_regs = tf_regs;
	dpp->tf_shift = tf_shift;
	dpp->tf_mask = tf_mask;

	dpp->lb_pixel_depth_supported =
		LB_PIXEL_DEPTH_18BPP |
		LB_PIXEL_DEPTH_24BPP |
		LB_PIXEL_DEPTH_30BPP;

	dpp->lb_bits_per_entry = LB_BITS_PER_ENTRY;
	dpp->lb_memory_size = LB_TOTAL_NUMBER_OF_ENTRIES;

	return true;
}
