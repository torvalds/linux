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
#include "dcn401/dcn401_dpp.h"
#include "basics/conversion.h"
#include "dcn10/dcn10_cm_common.h"

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

#define NUM_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))


enum dcn401_coef_filter_type_sel {
	SCL_COEF_LUMA_VERT_FILTER = 0,
	SCL_COEF_LUMA_HORZ_FILTER = 1,
	SCL_COEF_CHROMA_VERT_FILTER = 2,
	SCL_COEF_CHROMA_HORZ_FILTER = 3,
	SCL_COEF_SC_VERT_FILTER = 4,
	SCL_COEF_SC_HORZ_FILTER = 5
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
	DSCL_MODE_SCALING_YCBCR_ENABLE = 3,
	DSCL_MODE_LUMA_SCALING_BYPASS = 4,
	DSCL_MODE_CHROMA_SCALING_BYPASS = 5,
	DSCL_MODE_DSCL_BYPASS = 6
};

void dpp401_set_cursor_attributes(
	struct dpp *dpp_base,
	struct dc_cursor_attributes *cursor_attributes)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);
	enum dc_cursor_color_format color_format = cursor_attributes->color_format;
	int cur_rom_en = 0;

	if (color_format == CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA ||
		color_format == CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA) {
		if (cursor_attributes->attribute_flags.bits.ENABLE_CURSOR_DEGAMMA) {
			cur_rom_en = 1;
		}
	}

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

	dpp_base->att.cur0_ctl.bits.expansion_mode = 0;
	dpp_base->att.cur0_ctl.bits.cur0_rom_en = cur_rom_en;
	dpp_base->att.cur0_ctl.bits.mode = color_format;
}

void dpp401_set_cursor_position(
	struct dpp *dpp_base,
	const struct dc_cursor_position *pos,
	const struct dc_cursor_mi_param *param,
	uint32_t width,
	uint32_t height)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);
	uint32_t cur_en = pos->enable ? 1 : 0;

	if (dpp_base->pos.cur0_ctl.bits.cur0_enable != cur_en) {
		REG_UPDATE(CURSOR0_CONTROL, CUR0_ENABLE, cur_en);

		dpp_base->pos.cur0_ctl.bits.cur0_enable = cur_en;
	}
}

void dpp401_set_optional_cursor_attributes(
	struct dpp *dpp_base,
	struct dpp_cursor_attributes *attr)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);

	if (attr) {
		REG_UPDATE(CURSOR0_FP_SCALE_BIAS_G_Y, CUR0_FP_BIAS_G_Y, attr->bias);
		REG_UPDATE(CURSOR0_FP_SCALE_BIAS_G_Y, CUR0_FP_SCALE_G_Y, attr->scale);
		REG_UPDATE(CURSOR0_FP_SCALE_BIAS_RB_CRCB, CUR0_FP_BIAS_RB_CRCB, attr->bias);
		REG_UPDATE(CURSOR0_FP_SCALE_BIAS_RB_CRCB, CUR0_FP_SCALE_RB_CRCB, attr->scale);
	}
}

/* Program Cursor matrix block in DPP CM */
static void dpp401_program_cursor_csc(
	struct dpp *dpp_base,
	enum dc_color_space color_space,
	const struct dpp_input_csc_matrix *tbl_entry)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);
	uint32_t mode_select = 0;
	struct color_matrices_reg cur_matrix_regs;
	unsigned int i;
	const uint16_t *regval = NULL;
	int arr_size = sizeof(dpp_input_csc_matrix) / sizeof(struct dpp_input_csc_matrix);

	if (color_space < COLOR_SPACE_YCBCR601) {
		REG_SET(CUR0_MATRIX_MODE, 0, CUR0_MATRIX_MODE, CUR_MATRIX_BYPASS);
		return;
	}

	/* If adjustments not provided use hardcoded table for color space conversion */
	if (tbl_entry == NULL) {

		for (i = 0; i < arr_size; i++)
			if (dpp_input_csc_matrix[i].color_space == color_space) {
				regval = dpp_input_csc_matrix[i].regval;
				break;
			}

		if (regval == NULL) {
			BREAK_TO_DEBUGGER();
			REG_SET(CUR0_MATRIX_MODE, 0, CUR0_MATRIX_MODE, CUR_MATRIX_BYPASS);
			return;
		}
	} else {
		regval = tbl_entry->regval;
	}

	REG_GET(CUR0_MATRIX_MODE, CUR0_MATRIX_MODE_CURRENT, &mode_select);

	//If current set in use not set A, then use set A, otherwise use set B
	if (mode_select != CUR_MATRIX_SET_A)
		mode_select = CUR_MATRIX_SET_A;
	else
		mode_select = CUR_MATRIX_SET_B;

	cur_matrix_regs.shifts.csc_c11 = dpp->tf_shift->CUR0_MATRIX_C11_A;
	cur_matrix_regs.masks.csc_c11 = dpp->tf_mask->CUR0_MATRIX_C11_A;
	cur_matrix_regs.shifts.csc_c12 = dpp->tf_shift->CUR0_MATRIX_C12_A;
	cur_matrix_regs.masks.csc_c12 = dpp->tf_mask->CUR0_MATRIX_C12_A;

	if (mode_select == CUR_MATRIX_SET_A) {
		cur_matrix_regs.csc_c11_c12 = REG(CUR0_MATRIX_C11_C12_A);
		cur_matrix_regs.csc_c33_c34 = REG(CUR0_MATRIX_C33_C34_A);
	} else {
		cur_matrix_regs.csc_c11_c12 = REG(CUR0_MATRIX_C11_C12_B);
		cur_matrix_regs.csc_c33_c34 = REG(CUR0_MATRIX_C33_C34_B);
	}

	cm_helper_program_color_matrices(
		dpp->base.ctx,
		regval,
		&cur_matrix_regs);

	//select coefficient set to use
	REG_SET(CUR0_MATRIX_MODE, 0, CUR0_MATRIX_MODE, mode_select);
}

/* Program Cursor matrix block in DPP CM */
void dpp401_set_cursor_matrix(
	struct dpp *dpp_base,
	enum dc_color_space color_space,
	struct dc_csc_transform cursor_csc_color_matrix)
{
	//Since we don't have cursor matrix information, force bypass mode by passing in unknown color space
	dpp401_program_cursor_csc(dpp_base, COLOR_SPACE_UNKNOWN, NULL);
}
