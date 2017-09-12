/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_OPP_DCE110_H__
#define __DC_OPP_DCE110_H__

#include "dc_types.h"
#include "opp.h"
#include "core_types.h"

#include "gamma_types.h" /* decprecated */

struct gamma_parameters;

#define FROM_DCE11_OPP(opp)\
	container_of(opp, struct dce110_opp, base)

enum dce110_opp_reg_type {
	DCE110_OPP_REG_DCP = 0,
	DCE110_OPP_REG_DCFE,
	DCE110_OPP_REG_FMT,

	DCE110_OPP_REG_MAX
};

struct dce110_regamma {
	struct gamma_curve arr_curve_points[16];
	struct curve_points arr_points[3];
	uint32_t hw_points_num;
	struct hw_x_point *coordinates_x;
	struct pwl_result_data *rgb_resulted;

	/* re-gamma curve */
	struct pwl_float_data_ex *rgb_regamma;
	/* coeff used to map user evenly distributed points
	 * to our hardware points (predefined) for gamma 256 */
	struct pixel_gamma_point *coeff128;
	struct pixel_gamma_point *coeff128_oem;
	/* coeff used to map user evenly distributed points
	 * to our hardware points (predefined) for gamma 1025 */
	struct pixel_gamma_point *coeff128_dx;
	/* evenly distributed points, gamma 256 software points 0-255 */
	struct gamma_pixel *axis_x_256;
	/* evenly distributed points, gamma 1025 software points 0-1025 */
	struct gamma_pixel *axis_x_1025;
	/* OEM supplied gamma for regamma LUT */
	struct pwl_float_data *rgb_oem;
	/* user supplied gamma */
	struct pwl_float_data *rgb_user;
	uint32_t extra_points;
	bool use_half_points;
	struct fixed31_32 x_max1;
	struct fixed31_32 x_max2;
	struct fixed31_32 x_min;
	struct fixed31_32 divider1;
	struct fixed31_32 divider2;
	struct fixed31_32 divider3;
};

/* OPP RELATED */
#define TO_DCE110_OPP(opp)\
	container_of(opp, struct dce110_opp, base)

struct dce110_opp_reg_offsets {
	uint32_t fmt_offset;
	uint32_t fmt_mem_offset;
	uint32_t dcp_offset;
	uint32_t dcfe_offset;
};

struct dce110_opp {
	struct output_pixel_processor base;
	struct dce110_opp_reg_offsets offsets;
	struct dce110_regamma regamma;
};

bool dce110_opp_construct(struct dce110_opp *opp110,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_opp_reg_offsets *offsets);

void dce110_opp_destroy(struct output_pixel_processor **opp);

/* REGAMMA RELATED */
void dce110_opp_power_on_regamma_lut(
	struct output_pixel_processor *opp,
	bool power_on);

bool dce110_opp_program_regamma_pwl(
	struct output_pixel_processor *opp,
	const struct pwl_params *params);

void dce110_opp_set_regamma_mode(struct output_pixel_processor *opp,
		enum opp_regamma mode);

void dce110_opp_set_csc_adjustment(
	struct output_pixel_processor *opp,
	const struct out_csc_color_matrix *tbl_entry);

void dce110_opp_set_csc_default(
	struct output_pixel_processor *opp,
	const struct default_adjustment *default_adjust);

/* FORMATTER RELATED */
void dce110_opp_program_bit_depth_reduction(
	struct output_pixel_processor *opp,
	const struct bit_depth_reduction_params *params);

void dce110_opp_program_clamping_and_pixel_encoding(
	struct output_pixel_processor *opp,
	const struct clamping_and_pixel_encoding_params *params);

void dce110_opp_set_dyn_expansion(
	struct output_pixel_processor *opp,
	enum dc_color_space color_sp,
	enum dc_color_depth color_dpth,
	enum signal_type signal);

void dce110_opp_program_fmt(
	struct output_pixel_processor *opp,
	struct bit_depth_reduction_params *fmt_bit_depth,
	struct clamping_and_pixel_encoding_params *clamping);

void dce110_opp_set_clamping(
	struct dce110_opp *opp110,
	const struct clamping_and_pixel_encoding_params *params);

#endif
