/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#include "dcn30_dpp.h"
#include "basics/conversion.h"
#include "dcn30_cm_common.h"

#define REG(reg)\
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name


static void dpp30_read_state(struct dpp *dpp_base, struct dcn_dpp_state *s)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_GET(DPP_CONTROL,
			DPP_CLOCK_ENABLE, &s->is_enabled);

	// TODO: Implement for DCN3
}
/*program post scaler scs block in dpp CM*/
void dpp3_program_post_csc(
		struct dpp *dpp_base,
		enum dc_color_space color_space,
		enum dcn10_input_csc_select input_select,
		const struct out_csc_color_matrix *tbl_entry)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	int i;
	int arr_size = sizeof(dpp_input_csc_matrix)/sizeof(struct dpp_input_csc_matrix);
	const uint16_t *regval = NULL;
	uint32_t cur_select = 0;
	enum dcn10_input_csc_select select;
	struct color_matrices_reg gam_regs;

	if (input_select == INPUT_CSC_SELECT_BYPASS) {
		REG_SET(CM_POST_CSC_CONTROL, 0, CM_POST_CSC_MODE, 0);
		return;
	}

	if (tbl_entry == NULL) {
		for (i = 0; i < arr_size; i++)
			if (dpp_input_csc_matrix[i].color_space == color_space) {
				regval = dpp_input_csc_matrix[i].regval;
				break;
			}

		if (regval == NULL) {
			BREAK_TO_DEBUGGER();
			return;
		}
	} else {
		regval = tbl_entry->regval;
	}

	/* determine which CSC matrix (icsc or coma) we are using
	 * currently.  select the alternate set to double buffer
	 * the CSC update so CSC is updated on frame boundary
	 */
	REG_GET(CM_POST_CSC_CONTROL,
			CM_POST_CSC_MODE_CURRENT, &cur_select);

	if (cur_select != INPUT_CSC_SELECT_ICSC)
		select = INPUT_CSC_SELECT_ICSC;
	else
		select = INPUT_CSC_SELECT_COMA;

	gam_regs.shifts.csc_c11 = dpp->tf_shift->CM_POST_CSC_C11;
	gam_regs.masks.csc_c11  = dpp->tf_mask->CM_POST_CSC_C11;
	gam_regs.shifts.csc_c12 = dpp->tf_shift->CM_POST_CSC_C12;
	gam_regs.masks.csc_c12 = dpp->tf_mask->CM_POST_CSC_C12;

	if (select == INPUT_CSC_SELECT_ICSC) {

		gam_regs.csc_c11_c12 = REG(CM_POST_CSC_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_POST_CSC_C33_C34);

	} else {

		gam_regs.csc_c11_c12 = REG(CM_POST_CSC_B_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_POST_CSC_B_C33_C34);

	}

	cm_helper_program_color_matrices(
			dpp->base.ctx,
			regval,
			&gam_regs);

	REG_SET(CM_POST_CSC_CONTROL, 0,
			CM_POST_CSC_MODE, select);
}


/*CNVC degam unit has read only LUTs*/
void dpp3_set_pre_degam(struct dpp *dpp_base, enum dc_transfer_func_predefined tr)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	int pre_degam_en = 1;
	int degamma_lut_selection = 0;

	switch (tr) {
	case TRANSFER_FUNCTION_LINEAR:
	case TRANSFER_FUNCTION_UNITY:
		pre_degam_en = 0; //bypass
		break;
	case TRANSFER_FUNCTION_SRGB:
		degamma_lut_selection = 0;
		break;
	case TRANSFER_FUNCTION_BT709:
		degamma_lut_selection = 4;
		break;
	case TRANSFER_FUNCTION_PQ:
		degamma_lut_selection = 5;
		break;
	case TRANSFER_FUNCTION_HLG:
		degamma_lut_selection = 6;
		break;
	case TRANSFER_FUNCTION_GAMMA22:
		degamma_lut_selection = 1;
		break;
	case TRANSFER_FUNCTION_GAMMA24:
		degamma_lut_selection = 2;
		break;
	case TRANSFER_FUNCTION_GAMMA26:
		degamma_lut_selection = 3;
		break;
	default:
		pre_degam_en = 0;
		break;
	}

	REG_SET_2(PRE_DEGAM, 0,
			PRE_DEGAM_MODE, pre_degam_en,
			PRE_DEGAM_SELECT, degamma_lut_selection);
}

static void dpp3_cnv_setup (
		struct dpp *dpp_base,
		enum surface_pixel_format format,
		enum expansion_mode mode,
		struct dc_csc_transform input_csc_color_matrix,
		enum dc_color_space input_color_space,
		struct cnv_alpha_2bit_lut *alpha_2bit_lut)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	uint32_t pixel_format = 0;
	uint32_t alpha_en = 1;
	enum dc_color_space color_space = COLOR_SPACE_SRGB;
	enum dcn10_input_csc_select select = INPUT_CSC_SELECT_BYPASS;
	bool force_disable_cursor = false;
	uint32_t is_2bit = 0;
	uint32_t alpha_plane_enable = 0;
	uint32_t dealpha_en = 0, dealpha_ablnd_en = 0;
	uint32_t realpha_en = 0, realpha_ablnd_en = 0;
	uint32_t program_prealpha_dealpha = 0;
	struct out_csc_color_matrix tbl_entry;
	int i;

	REG_SET_2(FORMAT_CONTROL, 0,
		CNVC_BYPASS, 0,
		FORMAT_EXPANSION_MODE, mode);

	REG_UPDATE(FORMAT_CONTROL, FORMAT_CNV16, 0);
	REG_UPDATE(FORMAT_CONTROL, CNVC_BYPASS_MSB_ALIGN, 0);
	REG_UPDATE(FORMAT_CONTROL, CLAMP_POSITIVE, 0);
	REG_UPDATE(FORMAT_CONTROL, CLAMP_POSITIVE_C, 0);

	REG_UPDATE(FORMAT_CONTROL, FORMAT_CROSSBAR_R, 0);
	REG_UPDATE(FORMAT_CONTROL, FORMAT_CROSSBAR_G, 1);
	REG_UPDATE(FORMAT_CONTROL, FORMAT_CROSSBAR_B, 2);

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
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
		pixel_format = 26; /* ARGB16161616_UNORM */
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
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX:
		pixel_format = 113;
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
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE:
		pixel_format = 116;
		alpha_plane_enable = 0;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
		pixel_format = 116;
		alpha_plane_enable = 1;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT:
		pixel_format = 118;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT:
		pixel_format = 119;
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

	REG_SET_2(CNVC_SURFACE_PIXEL_FORMAT, 0,
			CNVC_SURFACE_PIXEL_FORMAT, pixel_format,
			CNVC_ALPHA_PLANE_ENABLE, alpha_plane_enable);
	REG_UPDATE(FORMAT_CONTROL, FORMAT_CONTROL__ALPHA_EN, alpha_en);

	if (program_prealpha_dealpha) {
		dealpha_en = 1;
		realpha_en = 1;
	}
	REG_SET_2(PRE_DEALPHA, 0,
			PRE_DEALPHA_EN, dealpha_en,
			PRE_DEALPHA_ABLND_EN, dealpha_ablnd_en);
	REG_SET_2(PRE_REALPHA, 0,
			PRE_REALPHA_EN, realpha_en,
			PRE_REALPHA_ABLND_EN, realpha_ablnd_en);

	/* If input adjustment exists, program the ICSC with those values. */
	if (input_csc_color_matrix.enable_adjustment == true) {
		for (i = 0; i < 12; i++)
			tbl_entry.regval[i] = input_csc_color_matrix.matrix[i];

		tbl_entry.color_space = input_color_space;

		if (color_space >= COLOR_SPACE_YCBCR601)
			select = INPUT_CSC_SELECT_ICSC;
		else
			select = INPUT_CSC_SELECT_BYPASS;

		dpp3_program_post_csc(dpp_base, color_space, select,
				      &tbl_entry);
	} else {
		dpp3_program_post_csc(dpp_base, color_space, select, NULL);
	}

	if (force_disable_cursor) {
		REG_UPDATE(CURSOR_CONTROL,
				CURSOR_ENABLE, 0);
		REG_UPDATE(CURSOR0_CONTROL,
				CUR0_ENABLE, 0);
	}
}

#define IDENTITY_RATIO(ratio) (dc_fixpt_u3d19(ratio) == (1 << 19))

void dpp3_set_cursor_attributes(
		struct dpp *dpp_base,
		struct dc_cursor_attributes *cursor_attributes)
{
	enum dc_cursor_color_format color_format = cursor_attributes->color_format;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	int cur_rom_en = 0;

	if (color_format == CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA ||
		color_format == CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA)
		cur_rom_en = 1;

	REG_UPDATE_3(CURSOR0_CONTROL,
			CUR0_MODE, color_format,
			CUR0_EXPANSION_MODE, 0,
			CUR0_ROM_EN, cur_rom_en);

	if (color_format == CURSOR_MODE_MONO) {
		/* todo: clarify what to program these to */
		REG_UPDATE(CURSOR0_COLOR0,
				CUR0_COLOR0, 0x00000000);
		REG_UPDATE(CURSOR0_COLOR1,
				CUR0_COLOR1, 0xFFFFFFFF);
	}
}


static bool dpp3_get_optimal_number_of_taps(
		struct dpp *dpp,
		struct scaler_data *scl_data,
		const struct scaling_taps *in_taps)
{
	int num_part_y, num_part_c;
	int max_taps_y, max_taps_c;
	int min_taps_y, min_taps_c;
	enum lb_memory_config lb_config;

	if (scl_data->viewport.width > scl_data->h_active &&
		dpp->ctx->dc->debug.max_downscale_src_width != 0 &&
		scl_data->viewport.width > dpp->ctx->dc->debug.max_downscale_src_width)
		return false;

	/*
	 * Set default taps if none are provided
	 * From programming guide: taps = min{ ceil(2*H_RATIO,1), 8} for downscaling
	 * taps = 4 for upscaling
	 */
	if (in_taps->h_taps == 0) {
		if (dc_fixpt_ceil(scl_data->ratios.horz) > 1)
			scl_data->taps.h_taps = min(2 * dc_fixpt_ceil(scl_data->ratios.horz), 8);
		else
			scl_data->taps.h_taps = 4;
	} else
		scl_data->taps.h_taps = in_taps->h_taps;
	if (in_taps->v_taps == 0) {
		if (dc_fixpt_ceil(scl_data->ratios.vert) > 1)
			scl_data->taps.v_taps = min(dc_fixpt_ceil(dc_fixpt_mul_int(scl_data->ratios.vert, 2)), 8);
		else
			scl_data->taps.v_taps = 4;
	} else
		scl_data->taps.v_taps = in_taps->v_taps;
	if (in_taps->v_taps_c == 0) {
		if (dc_fixpt_ceil(scl_data->ratios.vert_c) > 1)
			scl_data->taps.v_taps_c = min(dc_fixpt_ceil(dc_fixpt_mul_int(scl_data->ratios.vert_c, 2)), 8);
		else
			scl_data->taps.v_taps_c = 4;
	} else
		scl_data->taps.v_taps_c = in_taps->v_taps_c;
	if (in_taps->h_taps_c == 0) {
		if (dc_fixpt_ceil(scl_data->ratios.horz_c) > 1)
			scl_data->taps.h_taps_c = min(2 * dc_fixpt_ceil(scl_data->ratios.horz_c), 8);
		else
			scl_data->taps.h_taps_c = 4;
	} else if ((in_taps->h_taps_c % 2) != 0 && in_taps->h_taps_c != 1)
		/* Only 1 and even h_taps_c are supported by hw */
		scl_data->taps.h_taps_c = in_taps->h_taps_c - 1;
	else
		scl_data->taps.h_taps_c = in_taps->h_taps_c;

	/*Ensure we can support the requested number of vtaps*/
	min_taps_y = dc_fixpt_ceil(scl_data->ratios.vert);
	min_taps_c = dc_fixpt_ceil(scl_data->ratios.vert_c);

	/* Use LB_MEMORY_CONFIG_3 for 4:2:0 */
	if ((scl_data->format == PIXEL_FORMAT_420BPP8) || (scl_data->format == PIXEL_FORMAT_420BPP10))
		lb_config = LB_MEMORY_CONFIG_3;
	else
		lb_config = LB_MEMORY_CONFIG_0;

	dpp->caps->dscl_calc_lb_num_partitions(
			scl_data, lb_config, &num_part_y, &num_part_c);

	/* MAX_V_TAPS = MIN (NUM_LINES - MAX(CEILING(V_RATIO,1)-2, 0), 8) */
	if (dc_fixpt_ceil(scl_data->ratios.vert) > 2)
		max_taps_y = num_part_y - (dc_fixpt_ceil(scl_data->ratios.vert) - 2);
	else
		max_taps_y = num_part_y;

	if (dc_fixpt_ceil(scl_data->ratios.vert_c) > 2)
		max_taps_c = num_part_c - (dc_fixpt_ceil(scl_data->ratios.vert_c) - 2);
	else
		max_taps_c = num_part_c;

	if (max_taps_y < min_taps_y)
		return false;
	else if (max_taps_c < min_taps_c)
		return false;

	if (scl_data->taps.v_taps > max_taps_y)
		scl_data->taps.v_taps = max_taps_y;

	if (scl_data->taps.v_taps_c > max_taps_c)
		scl_data->taps.v_taps_c = max_taps_c;

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

static void dpp3_deferred_update(struct dpp *dpp_base)
{
	int bypass_state;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	if (dpp_base->deferred_reg_writes.bits.disable_dscl) {
		REG_UPDATE(DSCL_MEM_PWR_CTRL, LUT_MEM_PWR_FORCE, 3);
		dpp_base->deferred_reg_writes.bits.disable_dscl = false;
	}

	if (dpp_base->deferred_reg_writes.bits.disable_gamcor) {
		REG_GET(CM_GAMCOR_CONTROL, CM_GAMCOR_MODE_CURRENT, &bypass_state);
		if (bypass_state == 0) {	// only program if bypass was latched
			REG_UPDATE(CM_MEM_PWR_CTRL, GAMCOR_MEM_PWR_FORCE, 3);
		} else
			ASSERT(0); // LUT select was updated again before vupdate
		dpp_base->deferred_reg_writes.bits.disable_gamcor = false;
	}

	if (dpp_base->deferred_reg_writes.bits.disable_blnd_lut) {
		REG_GET(CM_BLNDGAM_CONTROL, CM_BLNDGAM_MODE_CURRENT, &bypass_state);
		if (bypass_state == 0) {	// only program if bypass was latched
			REG_UPDATE(CM_MEM_PWR_CTRL, BLNDGAM_MEM_PWR_FORCE, 3);
		} else
			ASSERT(0); // LUT select was updated again before vupdate
		dpp_base->deferred_reg_writes.bits.disable_blnd_lut = false;
	}

	if (dpp_base->deferred_reg_writes.bits.disable_3dlut) {
		REG_GET(CM_3DLUT_MODE, CM_3DLUT_MODE_CURRENT, &bypass_state);
		if (bypass_state == 0) {	// only program if bypass was latched
			REG_UPDATE(CM_MEM_PWR_CTRL2, HDR3DLUT_MEM_PWR_FORCE, 3);
		} else
			ASSERT(0); // LUT select was updated again before vupdate
		dpp_base->deferred_reg_writes.bits.disable_3dlut = false;
	}

	if (dpp_base->deferred_reg_writes.bits.disable_shaper) {
		REG_GET(CM_SHAPER_CONTROL, CM_SHAPER_MODE_CURRENT, &bypass_state);
		if (bypass_state == 0) {	// only program if bypass was latched
			REG_UPDATE(CM_MEM_PWR_CTRL2, SHAPER_MEM_PWR_FORCE, 3);
		} else
			ASSERT(0); // LUT select was updated again before vupdate
		dpp_base->deferred_reg_writes.bits.disable_shaper = false;
	}
}

static void dpp3_power_on_blnd_lut(
	struct dpp *dpp_base,
	bool power_on)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.cm) {
		if (power_on) {
			REG_UPDATE(CM_MEM_PWR_CTRL, BLNDGAM_MEM_PWR_FORCE, 0);
			REG_WAIT(CM_MEM_PWR_STATUS, BLNDGAM_MEM_PWR_STATE, 0, 1, 5);
		} else {
			dpp_base->ctx->dc->optimized_required = true;
			dpp_base->deferred_reg_writes.bits.disable_blnd_lut = true;
		}
	} else {
		REG_SET(CM_MEM_PWR_CTRL, 0,
				BLNDGAM_MEM_PWR_FORCE, power_on == true ? 0 : 1);
	}
}

static void dpp3_power_on_hdr3dlut(
	struct dpp *dpp_base,
	bool power_on)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.cm) {
		if (power_on) {
			REG_UPDATE(CM_MEM_PWR_CTRL2, HDR3DLUT_MEM_PWR_FORCE, 0);
			REG_WAIT(CM_MEM_PWR_STATUS2, HDR3DLUT_MEM_PWR_STATE, 0, 1, 5);
		} else {
			dpp_base->ctx->dc->optimized_required = true;
			dpp_base->deferred_reg_writes.bits.disable_3dlut = true;
		}
	}
}

static void dpp3_power_on_shaper(
	struct dpp *dpp_base,
	bool power_on)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.cm) {
		if (power_on) {
			REG_UPDATE(CM_MEM_PWR_CTRL2, SHAPER_MEM_PWR_FORCE, 0);
			REG_WAIT(CM_MEM_PWR_STATUS2, SHAPER_MEM_PWR_STATE, 0, 1, 5);
		} else {
			dpp_base->ctx->dc->optimized_required = true;
			dpp_base->deferred_reg_writes.bits.disable_shaper = true;
		}
	}
}

static void dpp3_configure_blnd_lut(
		struct dpp *dpp_base,
		bool is_ram_a)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_UPDATE_2(CM_BLNDGAM_LUT_CONTROL,
			CM_BLNDGAM_LUT_WRITE_COLOR_MASK, 7,
			CM_BLNDGAM_LUT_HOST_SEL, is_ram_a == true ? 0 : 1);

	REG_SET(CM_BLNDGAM_LUT_INDEX, 0, CM_BLNDGAM_LUT_INDEX, 0);
}

static void dpp3_program_blnd_pwl(
		struct dpp *dpp_base,
		const struct pwl_result_data *rgb,
		uint32_t num)
{
	uint32_t i;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	uint32_t last_base_value_red = rgb[num-1].red_reg + rgb[num-1].delta_red_reg;
	uint32_t last_base_value_green = rgb[num-1].green_reg + rgb[num-1].delta_green_reg;
	uint32_t last_base_value_blue = rgb[num-1].blue_reg + rgb[num-1].delta_blue_reg;

	if (is_rgb_equal(rgb, num)) {
		for (i = 0 ; i < num; i++)
			REG_SET(CM_BLNDGAM_LUT_DATA, 0, CM_BLNDGAM_LUT_DATA, rgb[i].red_reg);
		REG_SET(CM_BLNDGAM_LUT_DATA, 0, CM_BLNDGAM_LUT_DATA, last_base_value_red);
	} else {
		REG_UPDATE(CM_BLNDGAM_LUT_CONTROL, CM_BLNDGAM_LUT_WRITE_COLOR_MASK, 4);
		for (i = 0 ; i < num; i++)
			REG_SET(CM_BLNDGAM_LUT_DATA, 0, CM_BLNDGAM_LUT_DATA, rgb[i].red_reg);
		REG_SET(CM_BLNDGAM_LUT_DATA, 0, CM_BLNDGAM_LUT_DATA, last_base_value_red);

		REG_UPDATE(CM_BLNDGAM_LUT_CONTROL, CM_BLNDGAM_LUT_WRITE_COLOR_MASK, 2);
		for (i = 0 ; i < num; i++)
			REG_SET(CM_BLNDGAM_LUT_DATA, 0, CM_BLNDGAM_LUT_DATA, rgb[i].green_reg);
		REG_SET(CM_BLNDGAM_LUT_DATA, 0, CM_BLNDGAM_LUT_DATA, last_base_value_green);

		REG_UPDATE(CM_BLNDGAM_LUT_CONTROL, CM_BLNDGAM_LUT_WRITE_COLOR_MASK, 1);
		for (i = 0 ; i < num; i++)
			REG_SET(CM_BLNDGAM_LUT_DATA, 0, CM_BLNDGAM_LUT_DATA, rgb[i].blue_reg);
		REG_SET(CM_BLNDGAM_LUT_DATA, 0, CM_BLNDGAM_LUT_DATA, last_base_value_blue);
	}
}

static void dcn3_dpp_cm_get_reg_field(
		struct dcn3_dpp *dpp,
		struct dcn3_xfer_func_reg *reg)
{
	reg->shifts.exp_region0_lut_offset = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->masks.exp_region0_lut_offset = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->shifts.exp_region0_num_segments = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->masks.exp_region0_num_segments = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->shifts.exp_region1_lut_offset = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->masks.exp_region1_lut_offset = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->shifts.exp_region1_num_segments = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;
	reg->masks.exp_region1_num_segments = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;

	reg->shifts.field_region_end = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_END_B;
	reg->masks.field_region_end = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_END_B;
	reg->shifts.field_region_end_slope = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_END_SLOPE_B;
	reg->masks.field_region_end_slope = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_END_SLOPE_B;
	reg->shifts.field_region_end_base = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_END_BASE_B;
	reg->masks.field_region_end_base = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_END_BASE_B;
	reg->shifts.field_region_linear_slope = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_START_SLOPE_B;
	reg->masks.field_region_linear_slope = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_START_SLOPE_B;
	reg->shifts.exp_region_start = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_START_B;
	reg->masks.exp_region_start = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_START_B;
	reg->shifts.exp_resion_start_segment = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_START_SEGMENT_B;
	reg->masks.exp_resion_start_segment = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_START_SEGMENT_B;
}

/*program blnd lut RAM A*/
static void dpp3_program_blnd_luta_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	struct dcn3_xfer_func_reg gam_regs;

	dcn3_dpp_cm_get_reg_field(dpp, &gam_regs);

	gam_regs.start_cntl_b = REG(CM_BLNDGAM_RAMA_START_CNTL_B);
	gam_regs.start_cntl_g = REG(CM_BLNDGAM_RAMA_START_CNTL_G);
	gam_regs.start_cntl_r = REG(CM_BLNDGAM_RAMA_START_CNTL_R);
	gam_regs.start_slope_cntl_b = REG(CM_BLNDGAM_RAMA_START_SLOPE_CNTL_B);
	gam_regs.start_slope_cntl_g = REG(CM_BLNDGAM_RAMA_START_SLOPE_CNTL_G);
	gam_regs.start_slope_cntl_r = REG(CM_BLNDGAM_RAMA_START_SLOPE_CNTL_R);
	gam_regs.start_end_cntl1_b = REG(CM_BLNDGAM_RAMA_END_CNTL1_B);
	gam_regs.start_end_cntl2_b = REG(CM_BLNDGAM_RAMA_END_CNTL2_B);
	gam_regs.start_end_cntl1_g = REG(CM_BLNDGAM_RAMA_END_CNTL1_G);
	gam_regs.start_end_cntl2_g = REG(CM_BLNDGAM_RAMA_END_CNTL2_G);
	gam_regs.start_end_cntl1_r = REG(CM_BLNDGAM_RAMA_END_CNTL1_R);
	gam_regs.start_end_cntl2_r = REG(CM_BLNDGAM_RAMA_END_CNTL2_R);
	gam_regs.region_start = REG(CM_BLNDGAM_RAMA_REGION_0_1);
	gam_regs.region_end = REG(CM_BLNDGAM_RAMA_REGION_32_33);

	cm_helper_program_gamcor_xfer_func(dpp->base.ctx, params, &gam_regs);
}

/*program blnd lut RAM B*/
static void dpp3_program_blnd_lutb_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	struct dcn3_xfer_func_reg gam_regs;

	dcn3_dpp_cm_get_reg_field(dpp, &gam_regs);

	gam_regs.start_cntl_b = REG(CM_BLNDGAM_RAMB_START_CNTL_B);
	gam_regs.start_cntl_g = REG(CM_BLNDGAM_RAMB_START_CNTL_G);
	gam_regs.start_cntl_r = REG(CM_BLNDGAM_RAMB_START_CNTL_R);
	gam_regs.start_slope_cntl_b = REG(CM_BLNDGAM_RAMB_START_SLOPE_CNTL_B);
	gam_regs.start_slope_cntl_g = REG(CM_BLNDGAM_RAMB_START_SLOPE_CNTL_G);
	gam_regs.start_slope_cntl_r = REG(CM_BLNDGAM_RAMB_START_SLOPE_CNTL_R);
	gam_regs.start_end_cntl1_b = REG(CM_BLNDGAM_RAMB_END_CNTL1_B);
	gam_regs.start_end_cntl2_b = REG(CM_BLNDGAM_RAMB_END_CNTL2_B);
	gam_regs.start_end_cntl1_g = REG(CM_BLNDGAM_RAMB_END_CNTL1_G);
	gam_regs.start_end_cntl2_g = REG(CM_BLNDGAM_RAMB_END_CNTL2_G);
	gam_regs.start_end_cntl1_r = REG(CM_BLNDGAM_RAMB_END_CNTL1_R);
	gam_regs.start_end_cntl2_r = REG(CM_BLNDGAM_RAMB_END_CNTL2_R);
	gam_regs.region_start = REG(CM_BLNDGAM_RAMB_REGION_0_1);
	gam_regs.region_end = REG(CM_BLNDGAM_RAMB_REGION_32_33);

	cm_helper_program_gamcor_xfer_func(dpp->base.ctx, params, &gam_regs);
}

static enum dc_lut_mode dpp3_get_blndgam_current(struct dpp *dpp_base)
{
	enum dc_lut_mode mode;
	uint32_t mode_current = 0;
	uint32_t in_use = 0;

	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_GET(CM_BLNDGAM_CONTROL,
			CM_BLNDGAM_MODE_CURRENT, &mode_current);
	REG_GET(CM_BLNDGAM_CONTROL,
			CM_BLNDGAM_SELECT_CURRENT, &in_use);

		switch (mode_current) {
		case 0:
		case 1:
			mode = LUT_BYPASS;
			break;

		case 2:
			if (in_use == 0)
				mode = LUT_RAM_A;
			else
				mode = LUT_RAM_B;
			break;
		default:
			mode = LUT_BYPASS;
			break;
		}
		return mode;
}

static bool dpp3_program_blnd_lut(struct dpp *dpp_base,
				  const struct pwl_params *params)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	if (params == NULL) {
		REG_SET(CM_BLNDGAM_CONTROL, 0, CM_BLNDGAM_MODE, 0);
		if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.cm)
			dpp3_power_on_blnd_lut(dpp_base, false);
		return false;
	}

	current_mode = dpp3_get_blndgam_current(dpp_base);
	if (current_mode == LUT_BYPASS || current_mode == LUT_RAM_B)
		next_mode = LUT_RAM_A;
	else
		next_mode = LUT_RAM_B;

	dpp3_power_on_blnd_lut(dpp_base, true);
	dpp3_configure_blnd_lut(dpp_base, next_mode == LUT_RAM_A);

	if (next_mode == LUT_RAM_A)
		dpp3_program_blnd_luta_settings(dpp_base, params);
	else
		dpp3_program_blnd_lutb_settings(dpp_base, params);

	dpp3_program_blnd_pwl(
			dpp_base, params->rgb_resulted, params->hw_points_num);

	REG_UPDATE_2(CM_BLNDGAM_CONTROL,
			CM_BLNDGAM_MODE, 2,
			CM_BLNDGAM_SELECT, next_mode == LUT_RAM_A ? 0 : 1);

	return true;
}


static void dpp3_program_shaper_lut(
		struct dpp *dpp_base,
		const struct pwl_result_data *rgb,
		uint32_t num)
{
	uint32_t i, red, green, blue;
	uint32_t  red_delta, green_delta, blue_delta;
	uint32_t  red_value, green_value, blue_value;

	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	for (i = 0 ; i < num; i++) {

		red   = rgb[i].red_reg;
		green = rgb[i].green_reg;
		blue  = rgb[i].blue_reg;

		red_delta   = rgb[i].delta_red_reg;
		green_delta = rgb[i].delta_green_reg;
		blue_delta  = rgb[i].delta_blue_reg;

		red_value   = ((red_delta   & 0x3ff) << 14) | (red   & 0x3fff);
		green_value = ((green_delta & 0x3ff) << 14) | (green & 0x3fff);
		blue_value  = ((blue_delta  & 0x3ff) << 14) | (blue  & 0x3fff);

		REG_SET(CM_SHAPER_LUT_DATA, 0, CM_SHAPER_LUT_DATA, red_value);
		REG_SET(CM_SHAPER_LUT_DATA, 0, CM_SHAPER_LUT_DATA, green_value);
		REG_SET(CM_SHAPER_LUT_DATA, 0, CM_SHAPER_LUT_DATA, blue_value);
	}

}

static enum dc_lut_mode dpp3_get_shaper_current(struct dpp *dpp_base)
{
	enum dc_lut_mode mode;
	uint32_t state_mode;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_GET(CM_SHAPER_CONTROL,
			CM_SHAPER_MODE_CURRENT, &state_mode);

		switch (state_mode) {
		case 0:
			mode = LUT_BYPASS;
			break;
		case 1:
			mode = LUT_RAM_A;
			break;
		case 2:
			mode = LUT_RAM_B;
			break;
		default:
			mode = LUT_BYPASS;
			break;
		}
		return mode;
}

static void dpp3_configure_shaper_lut(
		struct dpp *dpp_base,
		bool is_ram_a)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_UPDATE(CM_SHAPER_LUT_WRITE_EN_MASK,
			CM_SHAPER_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(CM_SHAPER_LUT_WRITE_EN_MASK,
			CM_SHAPER_LUT_WRITE_SEL, is_ram_a == true ? 0:1);
	REG_SET(CM_SHAPER_LUT_INDEX, 0, CM_SHAPER_LUT_INDEX, 0);
}

/*program shaper RAM A*/

static void dpp3_program_shaper_luta_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_SET_2(CM_SHAPER_RAMA_START_CNTL_B, 0,
		CM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(CM_SHAPER_RAMA_START_CNTL_G, 0,
		CM_SHAPER_RAMA_EXP_REGION_START_G, params->corner_points[0].green.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_G, 0);
	REG_SET_2(CM_SHAPER_RAMA_START_CNTL_R, 0,
		CM_SHAPER_RAMA_EXP_REGION_START_R, params->corner_points[0].red.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_R, 0);

	REG_SET_2(CM_SHAPER_RAMA_END_CNTL_B, 0,
		CM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);

	REG_SET_2(CM_SHAPER_RAMA_END_CNTL_G, 0,
		CM_SHAPER_RAMA_EXP_REGION_END_G, params->corner_points[1].green.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_END_BASE_G, params->corner_points[1].green.custom_float_y);

	REG_SET_2(CM_SHAPER_RAMA_END_CNTL_R, 0,
		CM_SHAPER_RAMA_EXP_REGION_END_R, params->corner_points[1].red.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_END_BASE_R, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(CM_SHAPER_RAMA_REGION_0_1, 0,
		CM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_2_3, 0,
		CM_SHAPER_RAMA_EXP_REGION2_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION2_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION3_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION3_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_4_5, 0,
		CM_SHAPER_RAMA_EXP_REGION4_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION4_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION5_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION5_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_6_7, 0,
		CM_SHAPER_RAMA_EXP_REGION6_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION6_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION7_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION7_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_8_9, 0,
		CM_SHAPER_RAMA_EXP_REGION8_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION8_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION9_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION9_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_10_11, 0,
		CM_SHAPER_RAMA_EXP_REGION10_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION10_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION11_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION11_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_12_13, 0,
		CM_SHAPER_RAMA_EXP_REGION12_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION12_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION13_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION13_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_14_15, 0,
		CM_SHAPER_RAMA_EXP_REGION14_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION14_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION15_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION15_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_16_17, 0,
		CM_SHAPER_RAMA_EXP_REGION16_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION16_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION17_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION17_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_18_19, 0,
		CM_SHAPER_RAMA_EXP_REGION18_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION18_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION19_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION19_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_20_21, 0,
		CM_SHAPER_RAMA_EXP_REGION20_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION20_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION21_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION21_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_22_23, 0,
		CM_SHAPER_RAMA_EXP_REGION22_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION22_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION23_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION23_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_24_25, 0,
		CM_SHAPER_RAMA_EXP_REGION24_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION24_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION25_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION25_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_26_27, 0,
		CM_SHAPER_RAMA_EXP_REGION26_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION26_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION27_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION27_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_28_29, 0,
		CM_SHAPER_RAMA_EXP_REGION28_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION28_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION29_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION29_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_30_31, 0,
		CM_SHAPER_RAMA_EXP_REGION30_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION30_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION31_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION31_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_32_33, 0,
		CM_SHAPER_RAMA_EXP_REGION32_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION32_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION33_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION33_NUM_SEGMENTS, curve[1].segments_num);
}

/*program shaper RAM B*/
static void dpp3_program_shaper_lutb_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_SET_2(CM_SHAPER_RAMB_START_CNTL_B, 0,
		CM_SHAPER_RAMB_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(CM_SHAPER_RAMB_START_CNTL_G, 0,
		CM_SHAPER_RAMB_EXP_REGION_START_G, params->corner_points[0].green.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_G, 0);
	REG_SET_2(CM_SHAPER_RAMB_START_CNTL_R, 0,
		CM_SHAPER_RAMB_EXP_REGION_START_R, params->corner_points[0].red.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_R, 0);

	REG_SET_2(CM_SHAPER_RAMB_END_CNTL_B, 0,
		CM_SHAPER_RAMB_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);

	REG_SET_2(CM_SHAPER_RAMB_END_CNTL_G, 0,
		CM_SHAPER_RAMB_EXP_REGION_END_G, params->corner_points[1].green.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_END_BASE_G, params->corner_points[1].green.custom_float_y);

	REG_SET_2(CM_SHAPER_RAMB_END_CNTL_R, 0,
		CM_SHAPER_RAMB_EXP_REGION_END_R, params->corner_points[1].red.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_END_BASE_R, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(CM_SHAPER_RAMB_REGION_0_1, 0,
		CM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_2_3, 0,
		CM_SHAPER_RAMB_EXP_REGION2_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION2_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION3_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION3_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_4_5, 0,
		CM_SHAPER_RAMB_EXP_REGION4_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION4_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION5_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION5_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_6_7, 0,
		CM_SHAPER_RAMB_EXP_REGION6_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION6_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION7_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION7_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_8_9, 0,
		CM_SHAPER_RAMB_EXP_REGION8_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION8_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION9_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION9_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_10_11, 0,
		CM_SHAPER_RAMB_EXP_REGION10_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION10_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION11_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION11_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_12_13, 0,
		CM_SHAPER_RAMB_EXP_REGION12_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION12_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION13_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION13_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_14_15, 0,
		CM_SHAPER_RAMB_EXP_REGION14_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION14_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION15_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION15_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_16_17, 0,
		CM_SHAPER_RAMB_EXP_REGION16_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION16_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION17_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION17_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_18_19, 0,
		CM_SHAPER_RAMB_EXP_REGION18_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION18_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION19_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION19_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_20_21, 0,
		CM_SHAPER_RAMB_EXP_REGION20_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION20_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION21_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION21_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_22_23, 0,
		CM_SHAPER_RAMB_EXP_REGION22_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION22_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION23_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION23_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_24_25, 0,
		CM_SHAPER_RAMB_EXP_REGION24_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION24_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION25_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION25_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_26_27, 0,
		CM_SHAPER_RAMB_EXP_REGION26_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION26_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION27_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION27_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_28_29, 0,
		CM_SHAPER_RAMB_EXP_REGION28_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION28_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION29_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION29_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_30_31, 0,
		CM_SHAPER_RAMB_EXP_REGION30_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION30_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION31_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION31_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_32_33, 0,
		CM_SHAPER_RAMB_EXP_REGION32_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION32_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION33_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION33_NUM_SEGMENTS, curve[1].segments_num);

}


static bool dpp3_program_shaper(struct dpp *dpp_base,
				const struct pwl_params *params)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;

	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	if (params == NULL) {
		REG_SET(CM_SHAPER_CONTROL, 0, CM_SHAPER_LUT_MODE, 0);
		if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.cm)
			dpp3_power_on_shaper(dpp_base, false);
		return false;
	}

	if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.cm)
		dpp3_power_on_shaper(dpp_base, true);

	current_mode = dpp3_get_shaper_current(dpp_base);

	if (current_mode == LUT_BYPASS || current_mode == LUT_RAM_A)
		next_mode = LUT_RAM_B;
	else
		next_mode = LUT_RAM_A;

	dpp3_configure_shaper_lut(dpp_base, next_mode == LUT_RAM_A);

	if (next_mode == LUT_RAM_A)
		dpp3_program_shaper_luta_settings(dpp_base, params);
	else
		dpp3_program_shaper_lutb_settings(dpp_base, params);

	dpp3_program_shaper_lut(
			dpp_base, params->rgb_resulted, params->hw_points_num);

	REG_SET(CM_SHAPER_CONTROL, 0, CM_SHAPER_LUT_MODE, next_mode == LUT_RAM_A ? 1:2);

	return true;

}

static enum dc_lut_mode get3dlut_config(
			struct dpp *dpp_base,
			bool *is_17x17x17,
			bool *is_12bits_color_channel)
{
	uint32_t i_mode, i_enable_10bits, lut_size;
	enum dc_lut_mode mode;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_GET(CM_3DLUT_READ_WRITE_CONTROL,
			CM_3DLUT_30BIT_EN, &i_enable_10bits);
	REG_GET(CM_3DLUT_MODE,
			CM_3DLUT_MODE_CURRENT, &i_mode);

	switch (i_mode) {
	case 0:
		mode = LUT_BYPASS;
		break;
	case 1:
		mode = LUT_RAM_A;
		break;
	case 2:
		mode = LUT_RAM_B;
		break;
	default:
		mode = LUT_BYPASS;
		break;
	}
	if (i_enable_10bits > 0)
		*is_12bits_color_channel = false;
	else
		*is_12bits_color_channel = true;

	REG_GET(CM_3DLUT_MODE, CM_3DLUT_SIZE, &lut_size);

	if (lut_size == 0)
		*is_17x17x17 = true;
	else
		*is_17x17x17 = false;

	return mode;
}
/*
 * select ramA or ramB, or bypass
 * select color channel size 10 or 12 bits
 * select 3dlut size 17x17x17 or 9x9x9
 */
static void dpp3_set_3dlut_mode(
		struct dpp *dpp_base,
		enum dc_lut_mode mode,
		bool is_color_channel_12bits,
		bool is_lut_size17x17x17)
{
	uint32_t lut_mode;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	if (mode == LUT_BYPASS)
		lut_mode = 0;
	else if (mode == LUT_RAM_A)
		lut_mode = 1;
	else
		lut_mode = 2;

	REG_UPDATE_2(CM_3DLUT_MODE,
			CM_3DLUT_MODE, lut_mode,
			CM_3DLUT_SIZE, is_lut_size17x17x17 == true ? 0 : 1);
}

static void dpp3_select_3dlut_ram(
		struct dpp *dpp_base,
		enum dc_lut_mode mode,
		bool is_color_channel_12bits)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_UPDATE_2(CM_3DLUT_READ_WRITE_CONTROL,
			CM_3DLUT_RAM_SEL, mode == LUT_RAM_A ? 0 : 1,
			CM_3DLUT_30BIT_EN,
			is_color_channel_12bits == true ? 0:1);
}



static void dpp3_set3dlut_ram12(
		struct dpp *dpp_base,
		const struct dc_rgb *lut,
		uint32_t entries)
{
	uint32_t i, red, green, blue, red1, green1, blue1;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	for (i = 0 ; i < entries; i += 2) {
		red   = lut[i].red<<4;
		green = lut[i].green<<4;
		blue  = lut[i].blue<<4;
		red1   = lut[i+1].red<<4;
		green1 = lut[i+1].green<<4;
		blue1  = lut[i+1].blue<<4;

		REG_SET_2(CM_3DLUT_DATA, 0,
				CM_3DLUT_DATA0, red,
				CM_3DLUT_DATA1, red1);

		REG_SET_2(CM_3DLUT_DATA, 0,
				CM_3DLUT_DATA0, green,
				CM_3DLUT_DATA1, green1);

		REG_SET_2(CM_3DLUT_DATA, 0,
				CM_3DLUT_DATA0, blue,
				CM_3DLUT_DATA1, blue1);

	}
}

/*
 * load selected lut with 10 bits color channels
 */
static void dpp3_set3dlut_ram10(
		struct dpp *dpp_base,
		const struct dc_rgb *lut,
		uint32_t entries)
{
	uint32_t i, red, green, blue, value;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	for (i = 0; i < entries; i++) {
		red   = lut[i].red;
		green = lut[i].green;
		blue  = lut[i].blue;

		value = (red<<20) | (green<<10) | blue;

		REG_SET(CM_3DLUT_DATA_30BIT, 0, CM_3DLUT_DATA_30BIT, value);
	}

}


static void dpp3_select_3dlut_ram_mask(
		struct dpp *dpp_base,
		uint32_t ram_selection_mask)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_UPDATE(CM_3DLUT_READ_WRITE_CONTROL, CM_3DLUT_WRITE_EN_MASK,
			ram_selection_mask);
	REG_SET(CM_3DLUT_INDEX, 0, CM_3DLUT_INDEX, 0);
}

static bool dpp3_program_3dlut(struct dpp *dpp_base,
			       struct tetrahedral_params *params)
{
	enum dc_lut_mode mode;
	bool is_17x17x17;
	bool is_12bits_color_channel;
	struct dc_rgb *lut0;
	struct dc_rgb *lut1;
	struct dc_rgb *lut2;
	struct dc_rgb *lut3;
	int lut_size0;
	int lut_size;

	if (params == NULL) {
		dpp3_set_3dlut_mode(dpp_base, LUT_BYPASS, false, false);
		if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.cm)
			dpp3_power_on_hdr3dlut(dpp_base, false);
		return false;
	}

	if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.cm)
		dpp3_power_on_hdr3dlut(dpp_base, true);

	mode = get3dlut_config(dpp_base, &is_17x17x17, &is_12bits_color_channel);

	if (mode == LUT_BYPASS || mode == LUT_RAM_B)
		mode = LUT_RAM_A;
	else
		mode = LUT_RAM_B;

	is_17x17x17 = !params->use_tetrahedral_9;
	is_12bits_color_channel = params->use_12bits;
	if (is_17x17x17) {
		lut0 = params->tetrahedral_17.lut0;
		lut1 = params->tetrahedral_17.lut1;
		lut2 = params->tetrahedral_17.lut2;
		lut3 = params->tetrahedral_17.lut3;
		lut_size0 = sizeof(params->tetrahedral_17.lut0)/
					sizeof(params->tetrahedral_17.lut0[0]);
		lut_size  = sizeof(params->tetrahedral_17.lut1)/
					sizeof(params->tetrahedral_17.lut1[0]);
	} else {
		lut0 = params->tetrahedral_9.lut0;
		lut1 = params->tetrahedral_9.lut1;
		lut2 = params->tetrahedral_9.lut2;
		lut3 = params->tetrahedral_9.lut3;
		lut_size0 = sizeof(params->tetrahedral_9.lut0)/
				sizeof(params->tetrahedral_9.lut0[0]);
		lut_size  = sizeof(params->tetrahedral_9.lut1)/
				sizeof(params->tetrahedral_9.lut1[0]);
		}

	dpp3_select_3dlut_ram(dpp_base, mode,
				is_12bits_color_channel);
	dpp3_select_3dlut_ram_mask(dpp_base, 0x1);
	if (is_12bits_color_channel)
		dpp3_set3dlut_ram12(dpp_base, lut0, lut_size0);
	else
		dpp3_set3dlut_ram10(dpp_base, lut0, lut_size0);

	dpp3_select_3dlut_ram_mask(dpp_base, 0x2);
	if (is_12bits_color_channel)
		dpp3_set3dlut_ram12(dpp_base, lut1, lut_size);
	else
		dpp3_set3dlut_ram10(dpp_base, lut1, lut_size);

	dpp3_select_3dlut_ram_mask(dpp_base, 0x4);
	if (is_12bits_color_channel)
		dpp3_set3dlut_ram12(dpp_base, lut2, lut_size);
	else
		dpp3_set3dlut_ram10(dpp_base, lut2, lut_size);

	dpp3_select_3dlut_ram_mask(dpp_base, 0x8);
	if (is_12bits_color_channel)
		dpp3_set3dlut_ram12(dpp_base, lut3, lut_size);
	else
		dpp3_set3dlut_ram10(dpp_base, lut3, lut_size);


	dpp3_set_3dlut_mode(dpp_base, mode, is_12bits_color_channel,
					is_17x17x17);

	return true;
}
static struct dpp_funcs dcn30_dpp_funcs = {
	.dpp_program_gamcor_lut = dpp3_program_gamcor_lut,
	.dpp_read_state			= dpp30_read_state,
	.dpp_reset			= dpp_reset,
	.dpp_set_scaler			= dpp1_dscl_set_scaler_manual_scale,
	.dpp_get_optimal_number_of_taps	= dpp3_get_optimal_number_of_taps,
	.dpp_set_gamut_remap		= dpp3_cm_set_gamut_remap,
	.dpp_set_csc_adjustment		= NULL,
	.dpp_set_csc_default		= NULL,
	.dpp_program_regamma_pwl	= NULL,
	.dpp_set_pre_degam		= dpp3_set_pre_degam,
	.dpp_program_input_lut		= NULL,
	.dpp_full_bypass		= dpp1_full_bypass,
	.dpp_setup			= dpp3_cnv_setup,
	.dpp_program_degamma_pwl	= NULL,
	.dpp_program_cm_dealpha = dpp3_program_cm_dealpha,
	.dpp_program_cm_bias = dpp3_program_cm_bias,
	.dpp_program_blnd_lut = dpp3_program_blnd_lut,
	.dpp_program_shaper_lut = dpp3_program_shaper,
	.dpp_program_3dlut = dpp3_program_3dlut,
	.dpp_deferred_update = dpp3_deferred_update,
	.dpp_program_bias_and_scale	= NULL,
	.dpp_cnv_set_alpha_keyer	= dpp2_cnv_set_alpha_keyer,
	.set_cursor_attributes		= dpp3_set_cursor_attributes,
	.set_cursor_position		= dpp1_set_cursor_position,
	.set_optional_cursor_attributes	= dpp1_cnv_set_optional_cursor_attributes,
	.dpp_dppclk_control		= dpp1_dppclk_control,
	.dpp_set_hdr_multiplier		= dpp3_set_hdr_multiplier,
};


static struct dpp_caps dcn30_dpp_cap = {
	.dscl_data_proc_format = DSCL_DATA_PRCESSING_FLOAT_FORMAT,
	.dscl_calc_lb_num_partitions = dscl2_calc_lb_num_partitions,
};

bool dpp3_construct(
	struct dcn3_dpp *dpp,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn3_dpp_registers *tf_regs,
	const struct dcn3_dpp_shift *tf_shift,
	const struct dcn3_dpp_mask *tf_mask)
{
	dpp->base.ctx = ctx;

	dpp->base.inst = inst;
	dpp->base.funcs = &dcn30_dpp_funcs;
	dpp->base.caps = &dcn30_dpp_cap;

	dpp->tf_regs = tf_regs;
	dpp->tf_shift = tf_shift;
	dpp->tf_mask = tf_mask;

	return true;
}

