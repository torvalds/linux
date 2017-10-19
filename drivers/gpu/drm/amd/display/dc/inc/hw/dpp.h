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


#ifndef __DAL_DPP_H__
#define __DAL_DPP_H__

#include "transform.h"

struct dpp {
	const struct dpp_funcs *funcs;
	struct dc_context *ctx;
	int inst;
	struct dpp_caps *caps;
	struct pwl_params regamma_params;
};

struct dpp_grph_csc_adjustment {
	struct fixed31_32 temperature_matrix[CSC_TEMPERATURE_MATRIX_SIZE];
	enum graphics_gamut_adjust_type gamut_adjust_type;
};

struct dpp_funcs {
	void (*dpp_reset)(struct dpp *dpp);

	void (*dpp_set_scaler)(struct dpp *dpp,
			const struct scaler_data *scl_data);

	void (*dpp_set_pixel_storage_depth)(
			struct dpp *dpp,
			enum lb_pixel_depth depth,
			const struct bit_depth_reduction_params *bit_depth_params);

	bool (*dpp_get_optimal_number_of_taps)(
			struct dpp *dpp,
			struct scaler_data *scl_data,
			const struct scaling_taps *in_taps);

	void (*dpp_set_gamut_remap)(
			struct dpp *dpp,
			const struct dpp_grph_csc_adjustment *adjust);

	void (*opp_set_csc_default)(
		struct dpp *dpp,
		const struct default_adjustment *default_adjust);

	void (*opp_set_csc_adjustment)(
		struct dpp *dpp,
		const struct out_csc_color_matrix *tbl_entry);

	void (*opp_power_on_regamma_lut)(
		struct dpp *dpp,
		bool power_on);

	void (*opp_program_regamma_lut)(
			struct dpp *dpp,
			const struct pwl_result_data *rgb,
			uint32_t num);

	void (*opp_configure_regamma_lut)(
			struct dpp *dpp,
			bool is_ram_a);

	void (*opp_program_regamma_lutb_settings)(
			struct dpp *dpp,
			const struct pwl_params *params);

	void (*opp_program_regamma_luta_settings)(
			struct dpp *dpp,
			const struct pwl_params *params);

	void (*opp_program_regamma_pwl)(
		struct dpp *dpp, const struct pwl_params *params);

	void (*opp_set_regamma_mode)(
			struct dpp *dpp_base,
			enum opp_regamma mode);

	void (*ipp_program_bias_and_scale)(
			struct dpp *dpp,
			struct dc_bias_and_scale *params);

	void (*ipp_set_degamma)(
			struct dpp *dpp_base,
			enum ipp_degamma_mode mode);

	void (*ipp_program_input_lut)(
			struct dpp *dpp_base,
			const struct dc_gamma *gamma);

	void (*ipp_program_degamma_pwl)(struct dpp *dpp_base,
									 const struct pwl_params *params);

	void (*ipp_setup)(
			struct dpp *dpp_base,
			enum surface_pixel_format format,
			enum expansion_mode mode,
			struct csc_transform input_csc_color_matrix,
			enum dc_color_space input_color_space);

	void (*ipp_full_bypass)(struct dpp *dpp_base);

	void (*set_cursor_attributes)(
			struct dpp *dpp_base,
			const struct dc_cursor_attributes *attr);

	void (*set_cursor_position)(
			struct dpp *dpp_base,
			const struct dc_cursor_position *pos,
			const struct dc_cursor_mi_param *param,
			uint32_t width
			);

};



#endif
