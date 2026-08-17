// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "core_types.h"
#include "reg_helper.h"
#include "dcn42_dpp.h"
#include "dcn35/dcn35_dpp.h"

#define REG(reg)\
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name

static const uint32_t *get_hist_rgb_luma_coefs(enum dc_color_space color_space)
{
	// Coefs in s.6.12.
	// Y = 0.2126R + 0.7152G + 0.0722B
	static const uint32_t luma_transform_bt_709[] = {0x1cb36, 0x1e6e2, 0x1b27b};
	// Y = 0.2627R + 0.678G + 0.0593B
	static const uint32_t luma_transform_bt_2020[] = {0x1d0d0, 0x1e5b2, 0x1ae5c};

	switch (color_space) {
	case COLOR_SPACE_2020_RGB_FULLRANGE:
	case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
		return luma_transform_bt_2020;
	default:
		return luma_transform_bt_709;
	}
}

static void dpp42_dpp_cm_hist_control(
	struct dpp *dpp_base,
	struct cm_hist_control cntl,
	enum dc_color_space color_space)
{
	struct dcn42_dpp *dpp = TO_DCN42_DPP(dpp_base);

	REG_UPDATE_10(
		CM_HIST_CNTL,
		CM_HIST_SEL, cntl.tap_point,
		CM_HIST_CH_EN, cntl.channels_enabled,
		CM_HIST_SRC1_SEL, cntl.src_1_select,
		CM_HIST_SRC2_SEL, cntl.src_2_select,
		CM_HIST_SRC3_SEL, cntl.src_3_select,
		CM_HIST_CH1_XBAR, cntl.ch1_src,
		CM_HIST_CH2_XBAR, cntl.ch2_src,
		CM_HIST_CH3_XBAR, cntl.ch3_src,
		CM_HIST_FORMAT, cntl.format,
		CM_HIST_READ_CHANNEL_MASK, cntl.read_channel_mask);

	if (cntl.src_2_select == CM_HIST_SRC2_MODE_RGB_TO_Y) {
		const uint32_t *luma_transform = get_hist_rgb_luma_coefs(color_space);

		REG_UPDATE(CM_HIST_COEFA_SRC2, CM_HIST_COEFA_SRC2, luma_transform[0]);
		REG_UPDATE(CM_HIST_COEFB_SRC2, CM_HIST_COEFB_SRC2, luma_transform[1]);
		REG_UPDATE(CM_HIST_COEFC_SRC2, CM_HIST_COEFC_SRC2, luma_transform[2]);
	} else {
		REG_UPDATE(CM_HIST_COEFA_SRC2, CM_HIST_COEFA_SRC2, 0);
		REG_UPDATE(CM_HIST_COEFB_SRC2, CM_HIST_COEFB_SRC2, 0x1f000); // 1 in s.6.12
		REG_UPDATE(CM_HIST_COEFC_SRC2, CM_HIST_COEFC_SRC2, 0);
	}
}

static bool dpp42_dpp_cm_hist_read(struct dpp *dpp_base, struct cm_hist *hist_out)
{
	struct dcn42_dpp *dpp = TO_DCN42_DPP(dpp_base);
	uint32_t channel_mask = 0;
	uint32_t rdy_status = 0, rdy_status_a = 0, rdy_status_b = 0;
	bool ch1, ch2, ch3;

	if (hist_out == NULL)
		return false;


	REG_GET(CM_HIST_CNTL, CM_HIST_READ_CHANNEL_MASK, &channel_mask);
	ch1 = (channel_mask & 1) > 0;
	ch2 = (channel_mask & 2) > 0;
	ch3 = (channel_mask & 4) > 0;

	REG_GET(CM_HIST_STATUS, CM_HIST_BUFA_RDY_STATUS, &rdy_status_a);
	REG_GET(CM_HIST_STATUS, CM_HIST_BUFB_RDY_STATUS, &rdy_status_b);

	rdy_status = rdy_status_a || rdy_status_b;

	if (rdy_status) {
		REG_UPDATE(CM_HIST_LOCK, CM_HIST_LOCK, 1);
		REG_UPDATE(CM_HIST_INDEX, CM_HIST_INDEX, 0);
		for (int i = 0; i < 256; i++) {
			uint32_t temp;

			if (ch1) {
				REG_GET(CM_HIST_DATA, CM_HIST_DATA, &temp);
				hist_out->ch1[i] += temp;
			}
			if (ch2) {
				REG_GET(CM_HIST_DATA, CM_HIST_DATA, &temp);
				hist_out->ch2[i] += temp;
			}
			if (ch3) {
				REG_GET(CM_HIST_DATA, CM_HIST_DATA, &temp);
				hist_out->ch3[i] += temp;
			}
		}
		REG_UPDATE(CM_HIST_LOCK, CM_HIST_LOCK, 0);
		return true;
	} else {
		return false;
	}
}

static void dpp42_dpp_setup(
	struct dpp *dpp_base,
	enum surface_pixel_format format,
	enum expansion_mode mode,
	struct dc_csc_transform input_csc_color_matrix,
	enum dc_color_space input_color_space,
	struct cnv_alpha_2bit_lut *alpha_2bit_lut)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);
	uint32_t pixel_format = 0;
	uint32_t alpha_en = 1;
	enum dc_color_space color_space = COLOR_SPACE_SRGB;
	enum dcn10_input_csc_select select = INPUT_CSC_SELECT_BYPASS;
	uint32_t is_2bit = 0;
	uint32_t alpha_plane_enable = 0;
	uint32_t dealpha_en = 0, dealpha_ablnd_en = 0;
	uint32_t realpha_en = 0, realpha_ablnd_en = 0;
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
		pixel_format = 65;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		pixel_format = 64;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
		pixel_format = 67;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
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
		REG_UPDATE(ALPHA_2BIT_LUT01, ALPHA_2BIT_LUT0, alpha_2bit_lut->lut0);
		REG_UPDATE(ALPHA_2BIT_LUT01, ALPHA_2BIT_LUT1, alpha_2bit_lut->lut1);
		REG_UPDATE(ALPHA_2BIT_LUT23, ALPHA_2BIT_LUT2, alpha_2bit_lut->lut2);
		REG_UPDATE(ALPHA_2BIT_LUT23, ALPHA_2BIT_LUT3, alpha_2bit_lut->lut3);
	}

	REG_SET_2(CNVC_SURFACE_PIXEL_FORMAT, 0,
		CNVC_SURFACE_PIXEL_FORMAT, pixel_format,
		CNVC_ALPHA_PLANE_ENABLE, alpha_plane_enable);
	REG_UPDATE(FORMAT_CONTROL, FORMAT_CONTROL__ALPHA_EN, alpha_en);

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

		if (dpp3_should_bypass_post_csc_for_colorspace(color_space))
			select = INPUT_CSC_SELECT_BYPASS;
		else
			select = INPUT_CSC_SELECT_ICSC;

		dpp3_program_post_csc(dpp_base, color_space, select,
			&tbl_entry);
	} else {
		dpp3_program_post_csc(dpp_base, color_space, select, NULL);
	}
}
static void dcn42_dpp_force_disable_cursor(struct dpp *dpp_base)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);

	/* Force disable cursor */
	REG_UPDATE(CURSOR0_CONTROL, CUR0_ENABLE, 0);
	dpp_base->pos.cur0_ctl.bits.cur0_enable = 0;
}

static struct dpp_funcs dcn42_dpp_funcs = {
	.dpp_program_gamcor_lut		= dpp3_program_gamcor_lut,
	.dpp_read_state				= dpp401_read_state,
	.dpp_reset					= dpp_reset,
	.dpp_set_scaler				= dpp401_dscl_set_scaler_manual_scale,
	.dpp_get_optimal_number_of_taps	= dpp3_get_optimal_number_of_taps,
	.dpp_set_pre_degam			= dpp3_set_pre_degam,
	.dpp_setup					= dpp42_dpp_setup,
	.dpp_program_cm_dealpha		= dpp3_program_cm_dealpha,
	.dpp_program_cm_bias		= dpp3_program_cm_bias,
	.dpp_program_bias_and_scale	= dpp35_program_bias_and_scale_fcnv,
	.dpp_cnv_set_alpha_keyer	= dpp2_cnv_set_alpha_keyer,
	.set_cursor_attributes		= dpp401_set_cursor_attributes,
	.set_cursor_position		= dpp401_set_cursor_position,
	.set_optional_cursor_attributes	= dpp401_set_optional_cursor_attributes,
	.dpp_dppclk_control			= dpp35_dppclk_control,
	.dpp_set_hdr_multiplier		= dpp3_set_hdr_multiplier,
	.set_cursor_matrix			= dpp401_set_cursor_matrix,
	.dpp_cm_hist_control        = dpp42_dpp_cm_hist_control,
	.dpp_cm_hist_read           = dpp42_dpp_cm_hist_read,
	.dpp_read_reg_state			= dpp30_read_reg_state,
	.dpp_force_disable_cursor	= dcn42_dpp_force_disable_cursor,
};


static struct dpp_caps dcn42_dpp_cap = {
	.dscl_data_proc_format = DSCL_DATA_PRCESSING_FLOAT_FORMAT,
	.max_lb_partitions = 63,
	.dscl_calc_lb_num_partitions = dscl401_calc_lb_num_partitions,
};

bool dpp42_construct(
	struct dcn42_dpp *dpp,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn42_dpp_registers *tf_regs,
	const struct dcn42_dpp_shift *tf_shift,
	const struct dcn42_dpp_mask *tf_mask)
{
	dpp->base.ctx = ctx;

	dpp->base.inst = inst;
	dpp->base.funcs = &dcn42_dpp_funcs;
	dpp->base.caps = &dcn42_dpp_cap;

	dpp->tf_regs = tf_regs;
	dpp->tf_shift = tf_shift;
	dpp->tf_mask = tf_mask;

	return true;
}
