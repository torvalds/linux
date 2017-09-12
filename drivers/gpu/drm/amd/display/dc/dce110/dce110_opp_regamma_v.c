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

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "dce110_opp.h"
#include "gamma_types.h"

static void power_on_lut(struct output_pixel_processor *opp,
	bool power_on, bool inputgamma, bool regamma)
{
	uint32_t value = dm_read_reg(opp->ctx, mmDCFEV_MEM_PWR_CTRL);
	int i;

	if (power_on) {
		if (inputgamma)
			set_reg_field_value(
				value,
				1,
				DCFEV_MEM_PWR_CTRL,
				COL_MAN_INPUT_GAMMA_MEM_PWR_DIS);
		if (regamma)
			set_reg_field_value(
				value,
				1,
				DCFEV_MEM_PWR_CTRL,
				COL_MAN_GAMMA_CORR_MEM_PWR_DIS);
	} else {
		if (inputgamma)
			set_reg_field_value(
				value,
				0,
				DCFEV_MEM_PWR_CTRL,
				COL_MAN_INPUT_GAMMA_MEM_PWR_DIS);
		if (regamma)
			set_reg_field_value(
				value,
				0,
				DCFEV_MEM_PWR_CTRL,
				COL_MAN_GAMMA_CORR_MEM_PWR_DIS);
	}

	dm_write_reg(opp->ctx, mmDCFEV_MEM_PWR_CTRL, value);

	for (i = 0; i < 3; i++) {
		value = dm_read_reg(opp->ctx, mmDCFEV_MEM_PWR_CTRL);
		if (get_reg_field_value(value,
				DCFEV_MEM_PWR_CTRL,
				COL_MAN_INPUT_GAMMA_MEM_PWR_DIS) &&
			get_reg_field_value(value,
					DCFEV_MEM_PWR_CTRL,
					COL_MAN_GAMMA_CORR_MEM_PWR_DIS))
			break;

		udelay(2);
	}
}

static void set_bypass_input_gamma(struct dce110_opp *opp110)
{
	uint32_t value;

	value = dm_read_reg(opp110->base.ctx,
			mmCOL_MAN_INPUT_GAMMA_CONTROL1);

	set_reg_field_value(
				value,
				0,
				COL_MAN_INPUT_GAMMA_CONTROL1,
				INPUT_GAMMA_MODE);

	dm_write_reg(opp110->base.ctx,
			mmCOL_MAN_INPUT_GAMMA_CONTROL1, value);
}

static void configure_regamma_mode(struct dce110_opp *opp110, uint32_t mode)
{
	uint32_t value = 0;

	set_reg_field_value(
				value,
				mode,
				GAMMA_CORR_CONTROL,
				GAMMA_CORR_MODE);

	dm_write_reg(opp110->base.ctx, mmGAMMA_CORR_CONTROL, 0);
}

/*
 *****************************************************************************
 *  Function: regamma_config_regions_and_segments
 *
 *     build regamma curve by using predefined hw points
 *     uses interface parameters ,like EDID coeff.
 *
 * @param   : parameters   interface parameters
 *  @return void
 *
 *  @note
 *
 *  @see
 *
 *****************************************************************************
 */
static void regamma_config_regions_and_segments(
	struct dce110_opp *opp110, const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	uint32_t value = 0;

	{
		set_reg_field_value(
			value,
			params->arr_points[0].custom_float_x,
			GAMMA_CORR_CNTLA_START_CNTL,
			GAMMA_CORR_CNTLA_EXP_REGION_START);

		set_reg_field_value(
			value,
			0,
			GAMMA_CORR_CNTLA_START_CNTL,
			GAMMA_CORR_CNTLA_EXP_REGION_START_SEGMENT);

		dm_write_reg(opp110->base.ctx, mmGAMMA_CORR_CNTLA_START_CNTL,
				value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			params->arr_points[0].custom_float_slope,
			GAMMA_CORR_CNTLA_SLOPE_CNTL,
			GAMMA_CORR_CNTLA_EXP_REGION_LINEAR_SLOPE);

		dm_write_reg(opp110->base.ctx,
			mmGAMMA_CORR_CNTLA_SLOPE_CNTL, value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			params->arr_points[1].custom_float_x,
			GAMMA_CORR_CNTLA_END_CNTL1,
			GAMMA_CORR_CNTLA_EXP_REGION_END);

		dm_write_reg(opp110->base.ctx,
			mmGAMMA_CORR_CNTLA_END_CNTL1, value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			params->arr_points[2].custom_float_slope,
			GAMMA_CORR_CNTLA_END_CNTL2,
			GAMMA_CORR_CNTLA_EXP_REGION_END_BASE);

		set_reg_field_value(
			value,
			params->arr_points[1].custom_float_y,
			GAMMA_CORR_CNTLA_END_CNTL2,
			GAMMA_CORR_CNTLA_EXP_REGION_END_SLOPE);

		dm_write_reg(opp110->base.ctx,
			mmGAMMA_CORR_CNTLA_END_CNTL2, value);
	}

	curve = params->arr_curve_points;

	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			GAMMA_CORR_CNTLA_REGION_0_1,
			GAMMA_CORR_CNTLA_EXP_REGION0_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			GAMMA_CORR_CNTLA_REGION_0_1,
			GAMMA_CORR_CNTLA_EXP_REGION0_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			GAMMA_CORR_CNTLA_REGION_0_1,
			GAMMA_CORR_CNTLA_EXP_REGION1_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			GAMMA_CORR_CNTLA_REGION_0_1,
			GAMMA_CORR_CNTLA_EXP_REGION1_NUM_SEGMENTS);

		dm_write_reg(
			opp110->base.ctx,
			mmGAMMA_CORR_CNTLA_REGION_0_1,
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			GAMMA_CORR_CNTLA_REGION_2_3,
			GAMMA_CORR_CNTLA_EXP_REGION2_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			GAMMA_CORR_CNTLA_REGION_2_3,
			GAMMA_CORR_CNTLA_EXP_REGION2_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			GAMMA_CORR_CNTLA_REGION_2_3,
			GAMMA_CORR_CNTLA_EXP_REGION3_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			GAMMA_CORR_CNTLA_REGION_2_3,
			GAMMA_CORR_CNTLA_EXP_REGION3_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			mmGAMMA_CORR_CNTLA_REGION_2_3,
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			GAMMA_CORR_CNTLA_REGION_4_5,
			GAMMA_CORR_CNTLA_EXP_REGION4_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			GAMMA_CORR_CNTLA_REGION_4_5,
			GAMMA_CORR_CNTLA_EXP_REGION4_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			GAMMA_CORR_CNTLA_REGION_4_5,
			GAMMA_CORR_CNTLA_EXP_REGION5_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			GAMMA_CORR_CNTLA_REGION_4_5,
			GAMMA_CORR_CNTLA_EXP_REGION5_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			mmGAMMA_CORR_CNTLA_REGION_4_5,
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			GAMMA_CORR_CNTLA_REGION_6_7,
			GAMMA_CORR_CNTLA_EXP_REGION6_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			GAMMA_CORR_CNTLA_REGION_6_7,
			GAMMA_CORR_CNTLA_EXP_REGION6_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			GAMMA_CORR_CNTLA_REGION_6_7,
			GAMMA_CORR_CNTLA_EXP_REGION7_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			GAMMA_CORR_CNTLA_REGION_6_7,
			GAMMA_CORR_CNTLA_EXP_REGION7_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			mmGAMMA_CORR_CNTLA_REGION_6_7,
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			GAMMA_CORR_CNTLA_REGION_8_9,
			GAMMA_CORR_CNTLA_EXP_REGION8_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			GAMMA_CORR_CNTLA_REGION_8_9,
			GAMMA_CORR_CNTLA_EXP_REGION8_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			GAMMA_CORR_CNTLA_REGION_8_9,
			GAMMA_CORR_CNTLA_EXP_REGION9_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			GAMMA_CORR_CNTLA_REGION_8_9,
			GAMMA_CORR_CNTLA_EXP_REGION9_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			mmGAMMA_CORR_CNTLA_REGION_8_9,
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			GAMMA_CORR_CNTLA_REGION_10_11,
			GAMMA_CORR_CNTLA_EXP_REGION10_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			GAMMA_CORR_CNTLA_REGION_10_11,
			GAMMA_CORR_CNTLA_EXP_REGION10_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			GAMMA_CORR_CNTLA_REGION_10_11,
			GAMMA_CORR_CNTLA_EXP_REGION11_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			GAMMA_CORR_CNTLA_REGION_10_11,
			GAMMA_CORR_CNTLA_EXP_REGION11_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			mmGAMMA_CORR_CNTLA_REGION_10_11,
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			GAMMA_CORR_CNTLA_REGION_12_13,
			GAMMA_CORR_CNTLA_EXP_REGION12_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			GAMMA_CORR_CNTLA_REGION_12_13,
			GAMMA_CORR_CNTLA_EXP_REGION12_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			GAMMA_CORR_CNTLA_REGION_12_13,
			GAMMA_CORR_CNTLA_EXP_REGION13_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			GAMMA_CORR_CNTLA_REGION_12_13,
			GAMMA_CORR_CNTLA_EXP_REGION13_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			mmGAMMA_CORR_CNTLA_REGION_12_13,
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			GAMMA_CORR_CNTLA_REGION_14_15,
			GAMMA_CORR_CNTLA_EXP_REGION14_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			GAMMA_CORR_CNTLA_REGION_14_15,
			GAMMA_CORR_CNTLA_EXP_REGION14_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			GAMMA_CORR_CNTLA_REGION_14_15,
			GAMMA_CORR_CNTLA_EXP_REGION15_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			GAMMA_CORR_CNTLA_REGION_14_15,
			GAMMA_CORR_CNTLA_EXP_REGION15_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			mmGAMMA_CORR_CNTLA_REGION_14_15,
			value);
	}
}

static void program_pwl(struct dce110_opp *opp110,
		const struct pwl_params *params)
{
	uint32_t value = 0;

	set_reg_field_value(
		value,
		7,
		GAMMA_CORR_LUT_WRITE_EN_MASK,
		GAMMA_CORR_LUT_WRITE_EN_MASK);

	dm_write_reg(opp110->base.ctx,
		mmGAMMA_CORR_LUT_WRITE_EN_MASK, value);

	dm_write_reg(opp110->base.ctx,
		mmGAMMA_CORR_LUT_INDEX, 0);

	/* Program REGAMMA_LUT_DATA */
	{
		const uint32_t addr = mmGAMMA_CORR_LUT_DATA;
		uint32_t i = 0;
		const struct pwl_result_data *rgb =
				params->rgb_resulted;

		while (i != params->hw_points_num) {
			dm_write_reg(opp110->base.ctx, addr, rgb->red_reg);
			dm_write_reg(opp110->base.ctx, addr, rgb->green_reg);
			dm_write_reg(opp110->base.ctx, addr, rgb->blue_reg);

			dm_write_reg(opp110->base.ctx, addr,
				rgb->delta_red_reg);
			dm_write_reg(opp110->base.ctx, addr,
				rgb->delta_green_reg);
			dm_write_reg(opp110->base.ctx, addr,
				rgb->delta_blue_reg);

			++rgb;
			++i;
		}
	}
}

bool dce110_opp_program_regamma_pwl_v(
	struct output_pixel_processor *opp,
	const struct pwl_params *params)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);

	/* Setup regions */
	regamma_config_regions_and_segments(opp110, params);

	set_bypass_input_gamma(opp110);

	/* Power on gamma LUT memory */
	power_on_lut(opp, true, false, true);

	/* Program PWL */
	program_pwl(opp110, params);

	/* program regamma config */
	configure_regamma_mode(opp110, 1);

	/* Power return to auto back */
	power_on_lut(opp, false, false, true);

	return true;
}

void dce110_opp_power_on_regamma_lut_v(
	struct output_pixel_processor *opp,
	bool power_on)
{
	uint32_t value = dm_read_reg(opp->ctx, mmDCFEV_MEM_PWR_CTRL);

	set_reg_field_value(
		value,
		0,
		DCFEV_MEM_PWR_CTRL,
		COL_MAN_GAMMA_CORR_MEM_PWR_FORCE);

	set_reg_field_value(
		value,
		power_on,
		DCFEV_MEM_PWR_CTRL,
		COL_MAN_GAMMA_CORR_MEM_PWR_DIS);

	set_reg_field_value(
		value,
		0,
		DCFEV_MEM_PWR_CTRL,
		COL_MAN_INPUT_GAMMA_MEM_PWR_FORCE);

	set_reg_field_value(
		value,
		power_on,
		DCFEV_MEM_PWR_CTRL,
		COL_MAN_INPUT_GAMMA_MEM_PWR_DIS);

	dm_write_reg(opp->ctx, mmDCFEV_MEM_PWR_CTRL, value);
}
