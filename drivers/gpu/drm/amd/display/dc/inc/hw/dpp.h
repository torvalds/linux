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
	struct pwl_params degamma_params;

};

struct dpp_input_csc_matrix {
	enum dc_color_space color_space;
	uint16_t regval[12];
};

struct dpp_grph_csc_adjustment {
	struct fixed31_32 temperature_matrix[CSC_TEMPERATURE_MATRIX_SIZE];
	enum graphics_gamut_adjust_type gamut_adjust_type;
};

struct dcn_dpp_state {
	uint32_t is_enabled;
	uint32_t igam_lut_mode;
	uint32_t igam_input_format;
	uint32_t dgam_lut_mode;
	uint32_t rgam_lut_mode;
	uint32_t gamut_remap_mode;
	uint32_t gamut_remap_c11_c12;
	uint32_t gamut_remap_c13_c14;
	uint32_t gamut_remap_c21_c22;
	uint32_t gamut_remap_c23_c24;
	uint32_t gamut_remap_c31_c32;
	uint32_t gamut_remap_c33_c34;
};

struct dpp_funcs {
	void (*dpp_read_state)(struct dpp *dpp, struct dcn_dpp_state *s);

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

	void (*dpp_set_csc_default)(
		struct dpp *dpp,
		enum dc_color_space colorspace);

	void (*dpp_set_csc_adjustment)(
		struct dpp *dpp,
		const uint16_t *regval);

	void (*dpp_power_on_regamma_lut)(
		struct dpp *dpp,
		bool power_on);

	void (*dpp_program_regamma_lut)(
			struct dpp *dpp,
			const struct pwl_result_data *rgb,
			uint32_t num);

	void (*dpp_configure_regamma_lut)(
			struct dpp *dpp,
			bool is_ram_a);

	void (*dpp_program_regamma_lutb_settings)(
			struct dpp *dpp,
			const struct pwl_params *params);

	void (*dpp_program_regamma_luta_settings)(
			struct dpp *dpp,
			const struct pwl_params *params);

	void (*dpp_program_regamma_pwl)(
		struct dpp *dpp,
		const struct pwl_params *params,
		enum opp_regamma mode);

	void (*dpp_program_bias_and_scale)(
			struct dpp *dpp,
			struct dc_bias_and_scale *params);

	void (*dpp_set_degamma)(
			struct dpp *dpp_base,
			enum ipp_degamma_mode mode);

	void (*dpp_program_input_lut)(
			struct dpp *dpp_base,
			const struct dc_gamma *gamma);

	void (*dpp_program_degamma_pwl)(struct dpp *dpp_base,
									 const struct pwl_params *params);

	void (*dpp_setup)(
			struct dpp *dpp_base,
			enum surface_pixel_format format,
			enum expansion_mode mode,
			struct dc_csc_transform input_csc_color_matrix,
			enum dc_color_space input_color_space);

	void (*dpp_full_bypass)(struct dpp *dpp_base);

	void (*set_cursor_attributes)(
			struct dpp *dpp_base,
			enum dc_cursor_color_format color_format);

	void (*set_cursor_position)(
			struct dpp *dpp_base,
			const struct dc_cursor_position *pos,
			const struct dc_cursor_mi_param *param,
			uint32_t width,
			uint32_t height
			);
	void (*dpp_set_hdr_multiplier)(
			struct dpp *dpp_base,
			uint32_t multiplier);
	void (*set_optional_cursor_attributes)(
			struct dpp *dpp_base,
			struct dpp_cursor_attributes *attr);

	void (*dpp_dppclk_control)(
			struct dpp *dpp_base,
			bool dppclk_div,
			bool enable);

};



#endif
