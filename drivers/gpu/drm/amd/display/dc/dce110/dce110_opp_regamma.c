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

#define DCP_REG(reg)\
	(reg + opp110->offsets.dcp_offset)

#define DCFE_REG(reg)\
	(reg + opp110->offsets.dcfe_offset)

enum {
	MAX_PWL_ENTRY = 128,
	MAX_REGIONS_NUMBER = 16

};

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
	struct dce110_opp *opp110,
	const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	uint32_t value = 0;

	{
		set_reg_field_value(
			value,
			params->arr_points[0].custom_float_x,
			REGAMMA_CNTLA_START_CNTL,
			REGAMMA_CNTLA_EXP_REGION_START);

		set_reg_field_value(
			value,
			0,
			REGAMMA_CNTLA_START_CNTL,
			REGAMMA_CNTLA_EXP_REGION_START_SEGMENT);

		dm_write_reg(opp110->base.ctx,
				DCP_REG(mmREGAMMA_CNTLA_START_CNTL),
				value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			params->arr_points[0].custom_float_slope,
			REGAMMA_CNTLA_SLOPE_CNTL,
			REGAMMA_CNTLA_EXP_REGION_LINEAR_SLOPE);

		dm_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_SLOPE_CNTL), value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			params->arr_points[1].custom_float_x,
			REGAMMA_CNTLA_END_CNTL1,
			REGAMMA_CNTLA_EXP_REGION_END);

		dm_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_END_CNTL1), value);
	}
	{
		value = 0;
		set_reg_field_value(
			value,
			params->arr_points[2].custom_float_slope,
			REGAMMA_CNTLA_END_CNTL2,
			REGAMMA_CNTLA_EXP_REGION_END_BASE);

		set_reg_field_value(
			value,
			params->arr_points[1].custom_float_y,
			REGAMMA_CNTLA_END_CNTL2,
			REGAMMA_CNTLA_EXP_REGION_END_SLOPE);

		dm_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_END_CNTL2), value);
	}

	curve = params->arr_curve_points;

	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION0_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION0_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION1_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_0_1,
			REGAMMA_CNTLA_EXP_REGION1_NUM_SEGMENTS);

		dm_write_reg(
			opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_0_1),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION2_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION2_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION3_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_2_3,
			REGAMMA_CNTLA_EXP_REGION3_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_2_3),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION4_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION4_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION5_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_4_5,
			REGAMMA_CNTLA_EXP_REGION5_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_4_5),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION6_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION6_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION7_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_6_7,
			REGAMMA_CNTLA_EXP_REGION7_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_6_7),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION8_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION8_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION9_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_8_9,
			REGAMMA_CNTLA_EXP_REGION9_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_8_9),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION10_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION10_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION11_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_10_11,
			REGAMMA_CNTLA_EXP_REGION11_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_10_11),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION12_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION12_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION13_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_12_13,
			REGAMMA_CNTLA_EXP_REGION13_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_12_13),
			value);
	}

	curve += 2;
	{
		value = 0;
		set_reg_field_value(
			value,
			curve[0].offset,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION14_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[0].segments_num,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION14_NUM_SEGMENTS);

		set_reg_field_value(
			value,
			curve[1].offset,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION15_LUT_OFFSET);

		set_reg_field_value(
			value,
			curve[1].segments_num,
			REGAMMA_CNTLA_REGION_14_15,
			REGAMMA_CNTLA_EXP_REGION15_NUM_SEGMENTS);

		dm_write_reg(opp110->base.ctx,
			DCP_REG(mmREGAMMA_CNTLA_REGION_14_15),
			value);
	}
}

static void program_pwl(
	struct dce110_opp *opp110,
	const struct pwl_params *params)
{
	uint32_t value;

	{
		uint8_t max_tries = 10;
		uint8_t counter = 0;

		/* Power on LUT memory */
		value = dm_read_reg(opp110->base.ctx,
				DCFE_REG(mmDCFE_MEM_PWR_CTRL));

		set_reg_field_value(
			value,
			1,
			DCFE_MEM_PWR_CTRL,
			DCP_REGAMMA_MEM_PWR_DIS);

		dm_write_reg(opp110->base.ctx,
				DCFE_REG(mmDCFE_MEM_PWR_CTRL), value);

		while (counter < max_tries) {
			value =
				dm_read_reg(
					opp110->base.ctx,
					DCFE_REG(mmDCFE_MEM_PWR_STATUS));

			if (get_reg_field_value(
				value,
				DCFE_MEM_PWR_STATUS,
				DCP_REGAMMA_MEM_PWR_STATE) == 0)
				break;

			++counter;
		}

		if (counter == max_tries) {
			dm_logger_write(opp110->base.ctx->logger, LOG_WARNING,
				"%s: regamma lut was not powered on "
				"in a timely manner,"
				" programming still proceeds\n",
				__func__);
		}
	}

	value = 0;

	set_reg_field_value(
		value,
		7,
		REGAMMA_LUT_WRITE_EN_MASK,
		REGAMMA_LUT_WRITE_EN_MASK);

	dm_write_reg(opp110->base.ctx,
		DCP_REG(mmREGAMMA_LUT_WRITE_EN_MASK), value);
	dm_write_reg(opp110->base.ctx,
		DCP_REG(mmREGAMMA_LUT_INDEX), 0);

	/* Program REGAMMA_LUT_DATA */
	{
		const uint32_t addr = DCP_REG(mmREGAMMA_LUT_DATA);

		uint32_t i = 0;

		const struct pwl_result_data *rgb = params->rgb_resulted;

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

	/*  we are done with DCP LUT memory; re-enable low power mode */
	value = dm_read_reg(opp110->base.ctx, DCFE_REG(mmDCFE_MEM_PWR_CTRL));

	set_reg_field_value(
		value,
		0,
		DCFE_MEM_PWR_CTRL,
		DCP_REGAMMA_MEM_PWR_DIS);

	dm_write_reg(opp110->base.ctx, DCFE_REG(mmDCFE_MEM_PWR_CTRL), value);
}

bool dce110_opp_program_regamma_pwl(
	struct output_pixel_processor *opp,
	const struct pwl_params *params)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);

	/* Setup regions */
	regamma_config_regions_and_segments(opp110, params);

	/* Program PWL */
	program_pwl(opp110, params);

	return true;
}

void dce110_opp_power_on_regamma_lut(
	struct output_pixel_processor *opp,
	bool power_on)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);

	uint32_t value =
		dm_read_reg(opp->ctx, DCFE_REG(mmDCFE_MEM_PWR_CTRL));

	set_reg_field_value(
		value,
		power_on,
		DCFE_MEM_PWR_CTRL,
		DCP_REGAMMA_MEM_PWR_DIS);

	set_reg_field_value(
		value,
		power_on,
		DCFE_MEM_PWR_CTRL,
		DCP_LUT_MEM_PWR_DIS);

	dm_write_reg(opp->ctx, DCFE_REG(mmDCFE_MEM_PWR_CTRL), value);
}

void dce110_opp_set_regamma_mode(struct output_pixel_processor *opp,
		enum opp_regamma mode)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);
	uint32_t value = dm_read_reg(opp110->base.ctx,
				DCP_REG(mmREGAMMA_CONTROL));

	set_reg_field_value(
		value,
		mode,
		REGAMMA_CONTROL,
		GRPH_REGAMMA_MODE);

	dm_write_reg(opp110->base.ctx, DCP_REG(mmREGAMMA_CONTROL),
			value);
}
