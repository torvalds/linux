/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include "dce_ipp.h"
#include "reg_helper.h"
#include "dm_services.h"

#define REG(reg) \
	(ipp_dce->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	ipp_dce->ipp_shift->field_name, ipp_dce->ipp_mask->field_name

#define CTX \
	ipp_dce->base.ctx

static void dce_ipp_cursor_set_position(
	struct input_pixel_processor *ipp,
	const struct dc_cursor_position *position,
	const struct dc_cursor_mi_param *param)
{
	struct dce_ipp *ipp_dce = TO_DCE_IPP(ipp);

	/* lock cursor registers */
	REG_UPDATE(CUR_UPDATE, CURSOR_UPDATE_LOCK, true);

	/* Flag passed in structure differentiates cursor enable/disable. */
	/* Update if it differs from cached state. */
	REG_UPDATE(CUR_CONTROL, CURSOR_EN, position->enable);

	REG_SET_2(CUR_POSITION, 0,
		CURSOR_X_POSITION, position->x,
		CURSOR_Y_POSITION, position->y);

	REG_SET_2(CUR_HOT_SPOT, 0,
		CURSOR_HOT_SPOT_X, position->x_hotspot,
		CURSOR_HOT_SPOT_Y, position->y_hotspot);

	/* unlock cursor registers */
	REG_UPDATE(CUR_UPDATE, CURSOR_UPDATE_LOCK, false);
}

static void dce_ipp_cursor_set_attributes(
	struct input_pixel_processor *ipp,
	const struct dc_cursor_attributes *attributes)
{
	struct dce_ipp *ipp_dce = TO_DCE_IPP(ipp);
	int mode;

	/* Lock cursor registers */
	REG_UPDATE(CUR_UPDATE, CURSOR_UPDATE_LOCK, true);

	/* Program cursor control */
	switch (attributes->color_format) {
	case CURSOR_MODE_MONO:
		mode = 0;
		break;
	case CURSOR_MODE_COLOR_1BIT_AND:
		mode = 1;
		break;
	case CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA:
		mode = 2;
		break;
	case CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA:
		mode = 3;
		break;
	default:
		BREAK_TO_DEBUGGER(); /* unsupported */
		mode = 0;
	}

	REG_UPDATE_3(CUR_CONTROL,
		CURSOR_MODE, mode,
		CURSOR_2X_MAGNIFY, attributes->attribute_flags.bits.ENABLE_MAGNIFICATION,
		CUR_INV_TRANS_CLAMP, attributes->attribute_flags.bits.INVERSE_TRANSPARENT_CLAMPING);

	if (attributes->color_format == CURSOR_MODE_MONO) {
		REG_SET_3(CUR_COLOR1, 0,
			CUR_COLOR1_BLUE, 0,
			CUR_COLOR1_GREEN, 0,
			CUR_COLOR1_RED, 0);

		REG_SET_3(CUR_COLOR2, 0,
			CUR_COLOR2_BLUE, 0xff,
			CUR_COLOR2_GREEN, 0xff,
			CUR_COLOR2_RED, 0xff);
	}

	/*
	 * Program cursor size -- NOTE: HW spec specifies that HW register
	 * stores size as (height - 1, width - 1)
	 */
	REG_SET_2(CUR_SIZE, 0,
		CURSOR_WIDTH, attributes->width-1,
		CURSOR_HEIGHT, attributes->height-1);

	/* Program cursor surface address */
	/* SURFACE_ADDRESS_HIGH: Higher order bits (39:32) of hardware cursor
	 * surface base address in byte. It is 4K byte aligned.
	 * The correct way to program cursor surface address is to first write
	 * to CUR_SURFACE_ADDRESS_HIGH, and then write to CUR_SURFACE_ADDRESS
	 */
	REG_SET(CUR_SURFACE_ADDRESS_HIGH, 0,
		CURSOR_SURFACE_ADDRESS_HIGH, attributes->address.high_part);

	REG_SET(CUR_SURFACE_ADDRESS, 0,
		CURSOR_SURFACE_ADDRESS, attributes->address.low_part);

	/* Unlock Cursor registers. */
	REG_UPDATE(CUR_UPDATE, CURSOR_UPDATE_LOCK, false);
}

static void dce_ipp_program_prescale(
	struct input_pixel_processor *ipp,
	struct ipp_prescale_params *params)
{
	struct dce_ipp *ipp_dce = TO_DCE_IPP(ipp);

	/* set to bypass mode first before change */
	REG_UPDATE(PRESCALE_GRPH_CONTROL,
		GRPH_PRESCALE_BYPASS,
		1);

	REG_SET_2(PRESCALE_VALUES_GRPH_R, 0,
		GRPH_PRESCALE_SCALE_R, params->scale,
		GRPH_PRESCALE_BIAS_R, params->bias);

	REG_SET_2(PRESCALE_VALUES_GRPH_G, 0,
		GRPH_PRESCALE_SCALE_G, params->scale,
		GRPH_PRESCALE_BIAS_G, params->bias);

	REG_SET_2(PRESCALE_VALUES_GRPH_B, 0,
		GRPH_PRESCALE_SCALE_B, params->scale,
		GRPH_PRESCALE_BIAS_B, params->bias);

	if (params->mode != IPP_PRESCALE_MODE_BYPASS) {
		REG_UPDATE(PRESCALE_GRPH_CONTROL,
				GRPH_PRESCALE_BYPASS, 0);

		/* If prescale is in use, then legacy lut should be bypassed */
		REG_UPDATE(INPUT_GAMMA_CONTROL,
				GRPH_INPUT_GAMMA_MODE, 1);
	}
}

static void dce_ipp_program_input_lut(
	struct input_pixel_processor *ipp,
	const struct dc_gamma *gamma)
{
	int i;
	struct dce_ipp *ipp_dce = TO_DCE_IPP(ipp);

	/* power on LUT memory */
	if (REG(DCFE_MEM_PWR_CTRL))
		REG_SET(DCFE_MEM_PWR_CTRL, 0, DCP_LUT_MEM_PWR_DIS, 1);

	/* enable all */
	REG_SET(DC_LUT_WRITE_EN_MASK, 0, DC_LUT_WRITE_EN_MASK, 0x7);

	/* 256 entry mode */
	REG_UPDATE(DC_LUT_RW_MODE, DC_LUT_RW_MODE, 0);

	/* LUT-256, unsigned, integer, new u0.12 format */
	REG_SET_3(DC_LUT_CONTROL, 0,
		DC_LUT_DATA_R_FORMAT, 3,
		DC_LUT_DATA_G_FORMAT, 3,
		DC_LUT_DATA_B_FORMAT, 3);

	/* start from index 0 */
	REG_SET(DC_LUT_RW_INDEX, 0,
		DC_LUT_RW_INDEX, 0);

	for (i = 0; i < INPUT_LUT_ENTRIES; i++) {
		REG_SET(DC_LUT_SEQ_COLOR, 0, DC_LUT_SEQ_COLOR, gamma->red[i]);
		REG_SET(DC_LUT_SEQ_COLOR, 0, DC_LUT_SEQ_COLOR, gamma->green[i]);
		REG_SET(DC_LUT_SEQ_COLOR, 0, DC_LUT_SEQ_COLOR, gamma->blue[i]);
	}

	/* power off LUT memory */
	if (REG(DCFE_MEM_PWR_CTRL))
		REG_SET(DCFE_MEM_PWR_CTRL, 0, DCP_LUT_MEM_PWR_DIS, 0);

	/* bypass prescale, enable legacy LUT */
	REG_UPDATE(PRESCALE_GRPH_CONTROL, GRPH_PRESCALE_BYPASS, 1);
	REG_UPDATE(INPUT_GAMMA_CONTROL, GRPH_INPUT_GAMMA_MODE, 0);
}

static void dce_ipp_set_degamma(
	struct input_pixel_processor *ipp,
	enum ipp_degamma_mode mode)
{
	struct dce_ipp *ipp_dce = TO_DCE_IPP(ipp);
	uint32_t degamma_type = (mode == IPP_DEGAMMA_MODE_HW_sRGB) ? 1 : 0;

	ASSERT(mode == IPP_DEGAMMA_MODE_BYPASS ||
			mode == IPP_DEGAMMA_MODE_HW_sRGB);

	REG_SET_3(DEGAMMA_CONTROL, 0,
		GRPH_DEGAMMA_MODE, degamma_type,
		CURSOR_DEGAMMA_MODE, degamma_type,
		CURSOR2_DEGAMMA_MODE, degamma_type);
}

static const struct ipp_funcs dce_ipp_funcs = {
	.ipp_cursor_set_attributes = dce_ipp_cursor_set_attributes,
	.ipp_cursor_set_position = dce_ipp_cursor_set_position,
	.ipp_program_prescale = dce_ipp_program_prescale,
	.ipp_program_input_lut = dce_ipp_program_input_lut,
	.ipp_set_degamma = dce_ipp_set_degamma
};

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

void dce_ipp_construct(
	struct dce_ipp *ipp_dce,
	struct dc_context *ctx,
	int inst,
	const struct dce_ipp_registers *regs,
	const struct dce_ipp_shift *ipp_shift,
	const struct dce_ipp_mask *ipp_mask)
{
	ipp_dce->base.ctx = ctx;
	ipp_dce->base.inst = inst;
	ipp_dce->base.funcs = &dce_ipp_funcs;

	ipp_dce->regs = regs;
	ipp_dce->ipp_shift = ipp_shift;
	ipp_dce->ipp_mask = ipp_mask;
}

void dce_ipp_destroy(struct input_pixel_processor **ipp)
{
	dm_free(TO_DCE_IPP(*ipp));
	*ipp = NULL;
}
