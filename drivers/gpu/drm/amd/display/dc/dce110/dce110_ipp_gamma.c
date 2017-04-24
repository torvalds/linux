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

#include "dm_services.h"
#include "include/logger_interface.h"
#include "include/fixed31_32.h"
#include "basics/conversion.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "dce110_ipp.h"

#define DCP_REG(reg)\
	(mm##reg + ipp110->offsets.dcp_offset)

#define DCP_REG_SET_N(reg_name, n, ...)	\
	generic_reg_update_ex(ipp110->base.ctx, \
			DCP_REG(reg_name), \
			0, n, __VA_ARGS__)

#define DCP_REG_SET(reg, field1, val1) \
		DCP_REG_SET_N(reg, 1, FD(reg##__##field1), val1)

#define DCP_REG_SET_2(reg, field1, val1, field2, val2) \
		DCP_REG_SET_N(reg, 2, \
			FD(reg##__##field1), val1, \
			FD(reg##__##field2), val2)

#define DCP_REG_SET_3(reg, field1, val1, field2, val2, field3, val3) \
		DCP_REG_SET_N(reg, 3, \
			FD(reg##__##field1), val1, \
			FD(reg##__##field2), val2, \
			FD(reg##__##field3), val3)

#define DCP_REG_UPDATE_N(reg_name, n, ...)	\
	generic_reg_update_ex(ipp110->base.ctx, \
			DCP_REG(reg_name), \
			dm_read_reg(ipp110->base.ctx, DCP_REG(reg_name)), \
			n, __VA_ARGS__)

#define DCP_REG_UPDATE(reg, field, val)	\
		DCP_REG_UPDATE_N(reg, 1, FD(reg##__##field), val)



void dce110_ipp_set_degamma(
	struct input_pixel_processor *ipp,
	enum ipp_degamma_mode mode)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);

	uint32_t degamma_type = (mode == IPP_DEGAMMA_MODE_HW_sRGB) ? 1 : 0;

	ASSERT(mode == IPP_DEGAMMA_MODE_BYPASS ||
			mode == IPP_DEGAMMA_MODE_HW_sRGB);

	DCP_REG_SET_3(
		DEGAMMA_CONTROL,
		GRPH_DEGAMMA_MODE, degamma_type,
		CURSOR_DEGAMMA_MODE, degamma_type,
		CURSOR2_DEGAMMA_MODE, degamma_type);
}

void dce110_ipp_program_prescale(
	struct input_pixel_processor *ipp,
	struct ipp_prescale_params *params)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);

	/* set to bypass mode first before change */
	DCP_REG_UPDATE(PRESCALE_GRPH_CONTROL,
		GRPH_PRESCALE_BYPASS, 1);

	DCP_REG_SET_2(PRESCALE_VALUES_GRPH_R,
		GRPH_PRESCALE_SCALE_R, params->scale,
		GRPH_PRESCALE_BIAS_R, params->bias);

	DCP_REG_SET_2(PRESCALE_VALUES_GRPH_G,
		GRPH_PRESCALE_SCALE_G, params->scale,
		GRPH_PRESCALE_BIAS_G, params->bias);

	DCP_REG_SET_2(PRESCALE_VALUES_GRPH_B,
		GRPH_PRESCALE_SCALE_B, params->scale,
		GRPH_PRESCALE_BIAS_B, params->bias);

	if (params->mode != IPP_PRESCALE_MODE_BYPASS) {
		/* If prescale is in use, then legacy lut should be bypassed */
		DCP_REG_UPDATE(PRESCALE_GRPH_CONTROL, GRPH_PRESCALE_BYPASS, 0);
		DCP_REG_UPDATE(INPUT_GAMMA_CONTROL, GRPH_INPUT_GAMMA_MODE, 1);
	}
}

static void dce110_helper_select_lut(struct dce110_ipp *ipp110)
{
	/* enable all */
	DCP_REG_SET(DC_LUT_WRITE_EN_MASK, DC_LUT_WRITE_EN_MASK, 0x7);

	/* 256 entry mode */
	DCP_REG_UPDATE(DC_LUT_RW_MODE, DC_LUT_RW_MODE, 0);

	/* LUT-256, unsigned, integer, new u0.12 format */
	DCP_REG_SET_3(DC_LUT_CONTROL,
		DC_LUT_DATA_R_FORMAT, 3,
		DC_LUT_DATA_G_FORMAT, 3,
		DC_LUT_DATA_B_FORMAT, 3);

	/* start from index 0 */
	DCP_REG_SET(DC_LUT_RW_INDEX, DC_LUT_RW_INDEX, 0);
}

void dce110_ipp_program_input_lut(
	struct input_pixel_processor *ipp,
	const struct dc_gamma *gamma)
{
	int i;
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);

	dce110_helper_select_lut(ipp110);

	/* power on LUT memory and give it time to settle */
	DCP_REG_SET(DCFE_MEM_PWR_CTRL, DCP_LUT_MEM_PWR_DIS, 1);
	udelay(10);

	for (i = 0; i < INPUT_LUT_ENTRIES; i++) {
		DCP_REG_SET(DC_LUT_SEQ_COLOR, DC_LUT_SEQ_COLOR, gamma->red[i]);
		DCP_REG_SET(DC_LUT_SEQ_COLOR, DC_LUT_SEQ_COLOR, gamma->green[i]);
		DCP_REG_SET(DC_LUT_SEQ_COLOR, DC_LUT_SEQ_COLOR, gamma->blue[i]);
	}

	/* power off LUT memory */
	DCP_REG_SET(DCFE_MEM_PWR_CTRL, DCP_LUT_MEM_PWR_DIS, 0);

	/* bypass prescale, enable legacy LUT */
	DCP_REG_UPDATE(PRESCALE_GRPH_CONTROL, GRPH_PRESCALE_BYPASS, 1);
	DCP_REG_UPDATE(INPUT_GAMMA_CONTROL, GRPH_INPUT_GAMMA_MODE, 0);
}
