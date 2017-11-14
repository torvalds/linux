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

#ifndef __DAL_DCN10_CM_COMMON_H__
#define __DAL_DCN10_CM_COMMON_H__

#define TF_HELPER_REG_FIELD_LIST(type) \
	type exp_region0_lut_offset; \
	type exp_region0_num_segments; \
	type exp_region1_lut_offset; \
	type exp_region1_num_segments;\
	type field_region_end;\
	type field_region_end_slope;\
	type field_region_end_base;\
	type exp_region_start;\
	type exp_resion_start_segment;\
	type field_region_linear_slope

#define TF_CM_REG_FIELD_LIST(type) \
	type csc_c11; \
	type csc_c12

struct xfer_func_shift {
	TF_HELPER_REG_FIELD_LIST(uint8_t);
};

struct xfer_func_mask {
	TF_HELPER_REG_FIELD_LIST(uint32_t);
};

struct xfer_func_reg {
	struct xfer_func_shift shifts;
	struct xfer_func_mask masks;

	uint32_t start_cntl_b;
	uint32_t start_cntl_g;
	uint32_t start_cntl_r;
	uint32_t start_slope_cntl_b;
	uint32_t start_slope_cntl_g;
	uint32_t start_slope_cntl_r;
	uint32_t start_end_cntl1_b;
	uint32_t start_end_cntl2_b;
	uint32_t start_end_cntl1_g;
	uint32_t start_end_cntl2_g;
	uint32_t start_end_cntl1_r;
	uint32_t start_end_cntl2_r;
	uint32_t region_start;
	uint32_t region_end;
};

struct cm_color_matrix_shift {
	TF_CM_REG_FIELD_LIST(uint8_t);
};

struct cm_color_matrix_mask {
	TF_CM_REG_FIELD_LIST(uint32_t);
};

struct color_matrices_reg{
	struct cm_color_matrix_shift shifts;
	struct cm_color_matrix_mask masks;

	uint32_t csc_c11_c12;
	uint32_t csc_c33_c34;
};

void cm_helper_program_color_matrices(
		struct dc_context *ctx,
		const uint16_t *regval,
		const struct color_matrices_reg *reg);

void cm_helper_program_xfer_func(
		struct dc_context *ctx,
		const struct pwl_params *params,
		const struct xfer_func_reg *reg);

bool cm_helper_convert_to_custom_float(
		struct pwl_result_data *rgb_resulted,
		struct curve_points *arr_points,
		uint32_t hw_points_num,
		bool fixpoint);

bool cm_helper_translate_curve_to_hw_format(
		const struct dc_transfer_func *output_tf,
		struct pwl_params *lut_params, bool fixpoint);

#endif
