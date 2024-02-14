/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "dcn32_dpp.h"
#include "basics/conversion.h"
#include "dcn30/dcn30_cm_common.h"

/* Compute the maximum number of lines that we can fit in the line buffer */
static void dscl32_calc_lb_num_partitions(
		const struct scaler_data *scl_data,
		enum lb_memory_config lb_config,
		int *num_part_y,
		int *num_part_c)
{
	int memory_line_size_y, memory_line_size_c, memory_line_size_a,
	lb_memory_size, lb_memory_size_c, lb_memory_size_a, num_partitions_a;

	int line_size = scl_data->viewport.width < scl_data->recout.width ?
			scl_data->viewport.width : scl_data->recout.width;
	int line_size_c = scl_data->viewport_c.width < scl_data->recout.width ?
			scl_data->viewport_c.width : scl_data->recout.width;

	if (line_size == 0)
		line_size = 1;

	if (line_size_c == 0)
		line_size_c = 1;

	memory_line_size_y = (line_size + 5) / 6; /* +5 to ceil */
	memory_line_size_c = (line_size_c + 5) / 6; /* +5 to ceil */
	memory_line_size_a = (line_size + 5) / 6; /* +5 to ceil */

	if (lb_config == LB_MEMORY_CONFIG_1) {
		lb_memory_size = 970;
		lb_memory_size_c = 970;
		lb_memory_size_a = 970;
	} else if (lb_config == LB_MEMORY_CONFIG_2) {
		lb_memory_size = 1290;
		lb_memory_size_c = 1290;
		lb_memory_size_a = 1290;
	} else if (lb_config == LB_MEMORY_CONFIG_3) {
		if (scl_data->viewport.width  == scl_data->h_active &&
			scl_data->viewport.height == scl_data->v_active) {
			/* 420 mode: luma using all 3 mem from Y, plus 3rd mem from Cr and Cb */
			/* use increased LB size for calculation only if Scaler not enabled */
			lb_memory_size = 970 + 1290 + 1170 + 1170 + 1170;
			lb_memory_size_c = 970 + 1290;
			lb_memory_size_a = 970 + 1290 + 1170;
		} else {
			/* 420 mode: luma using all 3 mem from Y, plus 3rd mem from Cr and Cb */
			lb_memory_size = 970 + 1290 + 484 + 484 + 484;
			lb_memory_size_c = 970 + 1290;
			lb_memory_size_a = 970 + 1290 + 484;
		}
	} else {
		if (scl_data->viewport.width  == scl_data->h_active &&
			scl_data->viewport.height == scl_data->v_active) {
			/* use increased LB size for calculation only if Scaler not enabled */
			lb_memory_size = 970 + 1290 + 1170;
			lb_memory_size_c = 970 + 1290 + 1170;
			lb_memory_size_a = 970 + 1290 + 1170;
		} else {
			lb_memory_size = 970 + 1290 + 484;
			lb_memory_size_c = 970 + 1290 + 484;
			lb_memory_size_a = 970 + 1290 + 484;
		}
	}
	*num_part_y = lb_memory_size / memory_line_size_y;
	*num_part_c = lb_memory_size_c / memory_line_size_c;
	num_partitions_a = lb_memory_size_a / memory_line_size_a;

	if (scl_data->lb_params.alpha_en
			&& (num_partitions_a < *num_part_y))
		*num_part_y = num_partitions_a;

	if (*num_part_y > 32)
		*num_part_y = 32;
	if (*num_part_c > 32)
		*num_part_c = 32;
}

static struct dpp_funcs dcn32_dpp_funcs = {
	.dpp_program_gamcor_lut		= dpp3_program_gamcor_lut,
	.dpp_read_state				= dpp30_read_state,
	.dpp_reset					= dpp_reset,
	.dpp_set_scaler				= dpp1_dscl_set_scaler_manual_scale,
	.dpp_get_optimal_number_of_taps	= dpp3_get_optimal_number_of_taps,
	.dpp_set_gamut_remap		= dpp3_cm_set_gamut_remap,
	.dpp_set_csc_adjustment		= NULL,
	.dpp_set_csc_default		= NULL,
	.dpp_program_regamma_pwl	= NULL,
	.dpp_set_pre_degam			= dpp3_set_pre_degam,
	.dpp_program_input_lut		= NULL,
	.dpp_full_bypass			= dpp1_full_bypass,
	.dpp_setup					= dpp3_cnv_setup,
	.dpp_program_degamma_pwl	= NULL,
	.dpp_program_cm_dealpha		= dpp3_program_cm_dealpha,
	.dpp_program_cm_bias		= dpp3_program_cm_bias,

	.dpp_program_blnd_lut		= NULL, // BLNDGAM is removed completely in DCN3.2 DPP
	.dpp_program_shaper_lut		= NULL, // CM SHAPER block is removed in DCN3.2 DPP, (it is in MPCC, programmable before or after BLND)
	.dpp_program_3dlut			= NULL, // CM 3DLUT block is removed in DCN3.2 DPP, (it is in MPCC, programmable before or after BLND)

	.dpp_program_bias_and_scale	= NULL,
	.dpp_cnv_set_alpha_keyer	= dpp2_cnv_set_alpha_keyer,
	.set_cursor_attributes		= dpp3_set_cursor_attributes,
	.set_cursor_position		= dpp1_set_cursor_position,
	.set_optional_cursor_attributes	= dpp1_cnv_set_optional_cursor_attributes,
	.dpp_dppclk_control			= dpp1_dppclk_control,
	.dpp_set_hdr_multiplier		= dpp3_set_hdr_multiplier,
};


static struct dpp_caps dcn32_dpp_cap = {
	.dscl_data_proc_format = DSCL_DATA_PRCESSING_FLOAT_FORMAT,
	.max_lb_partitions = 31,
	.dscl_calc_lb_num_partitions = dscl32_calc_lb_num_partitions,
};

bool dpp32_construct(
	struct dcn3_dpp *dpp,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn3_dpp_registers *tf_regs,
	const struct dcn3_dpp_shift *tf_shift,
	const struct dcn3_dpp_mask *tf_mask)
{
	dpp->base.ctx = ctx;

	dpp->base.inst = inst;
	dpp->base.funcs = &dcn32_dpp_funcs;
	dpp->base.caps = &dcn32_dpp_cap;

	dpp->tf_regs = tf_regs;
	dpp->tf_shift = tf_shift;
	dpp->tf_mask = tf_mask;

	return true;
}
