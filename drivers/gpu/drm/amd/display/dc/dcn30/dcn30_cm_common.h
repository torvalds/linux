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

#include "dcn10/dcn10_cm_common.h"

#ifndef __DAL_DCN30_CM_COMMON_H__
#define __DAL_DCN30_CM_COMMON_H__

#define TF_HELPER_REG_FIELD_LIST_DCN3(type) \
	TF_HELPER_REG_FIELD_LIST(type);\
	type field_region_start_base;\
	type field_offset

struct DCN3_xfer_func_shift {
	TF_HELPER_REG_FIELD_LIST_DCN3(uint8_t);
};

struct DCN3_xfer_func_mask {
	TF_HELPER_REG_FIELD_LIST_DCN3(uint32_t);
};

struct dcn3_xfer_func_reg {
	struct DCN3_xfer_func_shift shifts;
	struct DCN3_xfer_func_mask masks;

	TF_HELPER_REG_LIST;
	uint32_t offset_b;
	uint32_t offset_g;
	uint32_t offset_r;
	uint32_t start_base_cntl_b;
	uint32_t start_base_cntl_g;
	uint32_t start_base_cntl_r;
};

void cm_helper_program_gamcor_xfer_func(
	struct dc_context *ctx,
	const struct pwl_params *params,
	const struct dcn3_xfer_func_reg *reg);

bool cm3_helper_translate_curve_to_hw_format(
	const struct dc_transfer_func *output_tf,
	struct pwl_params *lut_params, bool fixpoint);

bool cm3_helper_translate_curve_to_degamma_hw_format(
				const struct dc_transfer_func *output_tf,
				struct pwl_params *lut_params);

bool cm3_helper_convert_to_custom_float(
		struct pwl_result_data *rgb_resulted,
		struct curve_points3 *corner_points,
		uint32_t hw_points_num,
		bool fixpoint);

bool is_rgb_equal(const struct pwl_result_data *rgb, uint32_t num);

#endif
