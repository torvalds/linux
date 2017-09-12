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
#include "gamma_types.h"

#define DCP_REG(reg)\
	(reg + ipp110->offsets.dcp_offset)

enum {
	MAX_INPUT_LUT_ENTRY = 256
};

/*PROTOTYPE DECLARATIONS*/
static void set_lut_inc(
	struct dce110_ipp *ipp110,
	uint8_t inc,
	bool is_float,
	bool is_signed);

bool dce110_ipp_set_degamma(
	struct input_pixel_processor *ipp,
	enum ipp_degamma_mode mode)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);

	uint32_t value = 0;

	uint32_t degamma_type = (mode == IPP_DEGAMMA_MODE_HW_sRGB) ? 1 : 0;

	ASSERT(mode == IPP_DEGAMMA_MODE_BYPASS ||
			mode == IPP_DEGAMMA_MODE_HW_sRGB);

	set_reg_field_value(
		value,
		degamma_type,
		DEGAMMA_CONTROL,
		GRPH_DEGAMMA_MODE);

	set_reg_field_value(
		value,
		degamma_type,
		DEGAMMA_CONTROL,
		CURSOR_DEGAMMA_MODE);

	set_reg_field_value(
		value,
		degamma_type,
		DEGAMMA_CONTROL,
		CURSOR2_DEGAMMA_MODE);

	dm_write_reg(ipp110->base.ctx, DCP_REG(mmDEGAMMA_CONTROL), value);

	return true;
}

void dce110_ipp_program_prescale(
	struct input_pixel_processor *ipp,
	struct ipp_prescale_params *params)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);

	uint32_t prescale_control = 0;
	uint32_t prescale_value = 0;
	uint32_t legacy_lut_control = 0;

	prescale_control = dm_read_reg(ipp110->base.ctx,
			DCP_REG(mmPRESCALE_GRPH_CONTROL));

	if (params->mode != IPP_PRESCALE_MODE_BYPASS) {

		set_reg_field_value(
			prescale_control,
			0,
			PRESCALE_GRPH_CONTROL,
			GRPH_PRESCALE_BYPASS);

		/*
		 * If prescale is in use, then legacy lut should
		 * be bypassed
		 */
		legacy_lut_control = dm_read_reg(ipp110->base.ctx,
			DCP_REG(mmINPUT_GAMMA_CONTROL));

		set_reg_field_value(
			legacy_lut_control,
			1,
			INPUT_GAMMA_CONTROL,
			GRPH_INPUT_GAMMA_MODE);

		dm_write_reg(ipp110->base.ctx,
			DCP_REG(mmINPUT_GAMMA_CONTROL),
			legacy_lut_control);
	} else {
		set_reg_field_value(
			prescale_control,
			1,
			PRESCALE_GRPH_CONTROL,
			GRPH_PRESCALE_BYPASS);
	}

	set_reg_field_value(
		prescale_value,
		params->scale,
		PRESCALE_VALUES_GRPH_R,
		GRPH_PRESCALE_SCALE_R);

	set_reg_field_value(
		prescale_value,
		params->bias,
		PRESCALE_VALUES_GRPH_R,
		GRPH_PRESCALE_BIAS_R);

	dm_write_reg(ipp110->base.ctx,
		DCP_REG(mmPRESCALE_GRPH_CONTROL),
		prescale_control);

	dm_write_reg(ipp110->base.ctx,
		DCP_REG(mmPRESCALE_VALUES_GRPH_R),
		prescale_value);

	dm_write_reg(ipp110->base.ctx,
		DCP_REG(mmPRESCALE_VALUES_GRPH_G),
		prescale_value);

	dm_write_reg(ipp110->base.ctx,
		DCP_REG(mmPRESCALE_VALUES_GRPH_B),
		prescale_value);
}

static void set_lut_inc(
	struct dce110_ipp *ipp110,
	uint8_t inc,
	bool is_float,
	bool is_signed)
{
	const uint32_t addr = DCP_REG(mmDC_LUT_CONTROL);

	uint32_t value = dm_read_reg(ipp110->base.ctx, addr);

	set_reg_field_value(
		value,
		inc,
		DC_LUT_CONTROL,
		DC_LUT_INC_R);

	set_reg_field_value(
		value,
		inc,
		DC_LUT_CONTROL,
		DC_LUT_INC_G);

	set_reg_field_value(
		value,
		inc,
		DC_LUT_CONTROL,
		DC_LUT_INC_B);

	set_reg_field_value(
		value,
		is_float,
		DC_LUT_CONTROL,
		DC_LUT_DATA_R_FLOAT_POINT_EN);

	set_reg_field_value(
		value,
		is_float,
		DC_LUT_CONTROL,
		DC_LUT_DATA_G_FLOAT_POINT_EN);

	set_reg_field_value(
		value,
		is_float,
		DC_LUT_CONTROL,
		DC_LUT_DATA_B_FLOAT_POINT_EN);

	set_reg_field_value(
		value,
		is_signed,
		DC_LUT_CONTROL,
		DC_LUT_DATA_R_SIGNED_EN);

	set_reg_field_value(
		value,
		is_signed,
		DC_LUT_CONTROL,
		DC_LUT_DATA_G_SIGNED_EN);

	set_reg_field_value(
		value,
		is_signed,
		DC_LUT_CONTROL,
		DC_LUT_DATA_B_SIGNED_EN);

	dm_write_reg(ipp110->base.ctx, addr, value);
}

void dce110_helper_select_lut(struct dce110_ipp *ipp110)
{
	uint32_t value = 0;

	set_lut_inc(ipp110, 0, false, false);

	{
		const uint32_t addr = DCP_REG(mmDC_LUT_WRITE_EN_MASK);

		value = dm_read_reg(ipp110->base.ctx, addr);

		/* enable all */
		set_reg_field_value(
			value,
			0x7,
			DC_LUT_WRITE_EN_MASK,
			DC_LUT_WRITE_EN_MASK);

		dm_write_reg(ipp110->base.ctx, addr, value);
	}

	{
		const uint32_t addr = DCP_REG(mmDC_LUT_RW_MODE);

		value = dm_read_reg(ipp110->base.ctx, addr);

		set_reg_field_value(
			value,
			0,
			DC_LUT_RW_MODE,
			DC_LUT_RW_MODE);

		dm_write_reg(ipp110->base.ctx, addr, value);
	}

	{
		const uint32_t addr = DCP_REG(mmDC_LUT_CONTROL);

		value = dm_read_reg(ipp110->base.ctx, addr);

		/* 00 - new u0.12 */
		set_reg_field_value(
			value,
			3,
			DC_LUT_CONTROL,
			DC_LUT_DATA_R_FORMAT);

		set_reg_field_value(
			value,
			3,
			DC_LUT_CONTROL,
			DC_LUT_DATA_G_FORMAT);

		set_reg_field_value(
			value,
			3,
			DC_LUT_CONTROL,
			DC_LUT_DATA_B_FORMAT);

		dm_write_reg(ipp110->base.ctx, addr, value);
	}

	{
		const uint32_t addr = DCP_REG(mmDC_LUT_RW_INDEX);

		value = dm_read_reg(ipp110->base.ctx, addr);

		set_reg_field_value(
			value,
			0,
			DC_LUT_RW_INDEX,
			DC_LUT_RW_INDEX);

		dm_write_reg(ipp110->base.ctx, addr, value);
	}
}
