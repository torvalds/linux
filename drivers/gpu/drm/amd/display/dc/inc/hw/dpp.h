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

/**
 * DOC: overview
 *
 * The DPP (Display Pipe and Plane) block is the unified display data
 * processing engine in DCN for processing graphic or video data on per DPP
 * rectangle base. This rectangle can be a part of SLS (Single Large Surface),
 * or a layer to be blended with other DPP, or a rectangle associated with a
 * display tile.
 *
 * It provides various functions including:
 * - graphic color keyer
 * - graphic cursor compositing
 * - graphic or video image source to destination scaling
 * - image sharping
 * - video format conversion from 4:2:0 or 4:2:2 to 4:4:4
 * - Color Space Conversion
 * - Host LUT gamma adjustment
 * - Color Gamut Remap
 * - brightness and contrast adjustment.
 *
 * DPP pipe consists of Converter and Cursor (CNVC), Scaler (DSCL), Color
 * Management (CM), Output Buffer (OBUF) and Digital Bypass (DPB) module
 * connected in a video/graphics pipeline.
 */

#include "transform.h"
#include "cursor_reg_cache.h"

union defer_reg_writes {
	struct {
		bool disable_blnd_lut:1;
		bool disable_3dlut:1;
		bool disable_shaper:1;
		bool disable_gamcor:1;
		bool disable_dscl:1;
	} bits;
	uint32_t raw;
};

struct dpp {
	const struct dpp_funcs *funcs;
	struct dc_context *ctx;
	/**
	 * @inst:
	 *
	 * inst stands for "instance," and it is an id number that references a
	 * specific DPP.
	 */
	int inst;
	struct dpp_caps *caps;
	struct pwl_params regamma_params;
	struct pwl_params degamma_params;
	struct dpp_cursor_attributes cur_attr;
	union defer_reg_writes deferred_reg_writes;

	struct pwl_params shaper_params;
	bool cm_bypass_mode;

	struct cursor_position_cache_dpp  pos;
	struct cursor_attribute_cache_dpp att;
};

struct dpp_input_csc_matrix {
	enum dc_color_space color_space;
	uint16_t regval[12];
};

static const struct dpp_input_csc_matrix __maybe_unused dpp_input_csc_matrix[] = {
	{ COLOR_SPACE_SRGB,
		{ 0x2000, 0,      0,      0,
		  0,      0x2000, 0,      0,
		  0,      0,      0x2000, 0 } },
	{ COLOR_SPACE_SRGB_LIMITED,
		{ 0x2000, 0,      0,      0,
		  0,      0x2000, 0,      0,
		  0,      0,      0x2000, 0 } },
	{ COLOR_SPACE_YCBCR601,
		{ 0x2cdd, 0x2000, 0,      0xe991,
		  0xe926, 0x2000, 0xf4fd, 0x10ef,
		  0,      0x2000, 0x38b4, 0xe3a6 } },
	{ COLOR_SPACE_YCBCR601_LIMITED,
		{ 0x3353, 0x2568, 0,      0xe400,
		  0xe5dc, 0x2568, 0xf367, 0x1108,
		  0,      0x2568, 0x40de, 0xdd3a } },
	{ COLOR_SPACE_YCBCR709,
		{ 0x3265, 0x2000, 0,      0xe6ce,
		  0xf105, 0x2000, 0xfa01, 0xa7d,
		  0,      0x2000, 0x3b61, 0xe24f } },
	{ COLOR_SPACE_YCBCR709_LIMITED,
		{ 0x39a6, 0x2568, 0,      0xe0d6,
		  0xeedd, 0x2568, 0xf925, 0x9a8,
		  0,      0x2568, 0x43ee, 0xdbb2 } },
	{ COLOR_SPACE_2020_YCBCR_FULL,
		{ 0x2F30, 0x2000, 0,      0xE869,
		  0xEDB7, 0x2000, 0xFABC, 0xBC6,
		  0,      0x2000, 0x3C34, 0xE1E6 } },
	{ COLOR_SPACE_2020_YCBCR_LIMITED,
		{ 0x35B9, 0x2543, 0,      0xE2B2,
		  0xEB2F, 0x2543, 0xFA01, 0x0B1F,
		  0,      0x2543, 0x4489, 0xDB42 } },
	{ COLOR_SPACE_2020_RGB_LIMITEDRANGE,
		{ 0x35E0, 0x255F, 0,      0xE2B3,
		  0xEB20, 0x255F, 0xF9FD, 0xB1E,
		  0,      0x255F, 0x44BD, 0xDB43 } }
};

struct dpp_grph_csc_adjustment {
	struct fixed31_32 temperature_matrix[CSC_TEMPERATURE_MATRIX_SIZE];
	enum graphics_gamut_adjust_type gamut_adjust_type;
};

struct cnv_color_keyer_params {
	int color_keyer_en;
	int color_keyer_mode;
	int color_keyer_alpha_low;
	int color_keyer_alpha_high;
	int color_keyer_red_low;
	int color_keyer_red_high;
	int color_keyer_green_low;
	int color_keyer_green_high;
	int color_keyer_blue_low;
	int color_keyer_blue_high;
};

/**
 * struct cnv_alpha_2bit_lut - Set the 8bit alpha values based on the 2 bit alpha
 */
struct cnv_alpha_2bit_lut {
	/**
	* @lut0: ALPHA_2BIT_LUT. ALPHA_2BIT_LUT0. Default: 0b00000000
	*/
	int lut0;

	/**
	 * @lut1: ALPHA_2BIT_LUT. ALPHA_2BIT_LUT1. Default: 0b01010101
	 */
	int lut1;

	/**
	 * @lut2: ALPHA_2BIT_LUT. ALPHA_2BIT_LUT2. Default: 0b10101010
	 */
	int lut2;

	/**
	 * @lut3: ALPHA_2BIT_LUT. ALPHA_2BIT_LUT3. Default: 0b11111111
	 */
	int lut3;
};

struct dcn_dpp_state {
	uint32_t is_enabled;
	uint32_t igam_lut_mode;
	uint32_t igam_input_format;
	uint32_t dgam_lut_mode;
	uint32_t rgam_lut_mode;
	// gamut_remap data for dcn10_get_cm_states()
	uint32_t gamut_remap_mode;
	uint32_t gamut_remap_c11_c12;
	uint32_t gamut_remap_c13_c14;
	uint32_t gamut_remap_c21_c22;
	uint32_t gamut_remap_c23_c24;
	uint32_t gamut_remap_c31_c32;
	uint32_t gamut_remap_c33_c34;
	// gamut_remap data for dcn*_log_color_state()
	struct dpp_grph_csc_adjustment gamut_remap;
	uint32_t shaper_lut_mode;
	uint32_t lut3d_mode;
	uint32_t lut3d_bit_depth;
	uint32_t lut3d_size;
	uint32_t blnd_lut_mode;
	uint32_t pre_dgam_mode;
	uint32_t pre_dgam_select;
	uint32_t gamcor_mode;
};

struct CM_bias_params {
	uint32_t cm_bias_cr_r;
	uint32_t cm_bias_y_g;
	uint32_t cm_bias_cb_b;
	uint32_t cm_bias_format;
};

struct dpp_funcs {
	bool (*dpp_program_gamcor_lut)(
		struct dpp *dpp_base, const struct pwl_params *params);

	void (*dpp_set_pre_degam)(struct dpp *dpp_base,
			enum dc_transfer_func_predefined tr);

	void (*dpp_program_cm_dealpha)(struct dpp *dpp_base,
		uint32_t enable, uint32_t additive_blending);

	void (*dpp_program_cm_bias)(
		struct dpp *dpp_base,
		struct CM_bias_params *bias_params);

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
			enum dc_color_space input_color_space,
			struct cnv_alpha_2bit_lut *alpha_2bit_lut);

	void (*dpp_full_bypass)(struct dpp *dpp_base);

	void (*set_cursor_attributes)(
			struct dpp *dpp_base,
			struct dc_cursor_attributes *cursor_attributes);

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

	void (*dpp_deferred_update)(
			struct dpp *dpp);
	bool (*dpp_program_blnd_lut)(
			struct dpp *dpp,
			const struct pwl_params *params);
	bool (*dpp_program_shaper_lut)(
			struct dpp *dpp,
			const struct pwl_params *params);
	bool (*dpp_program_3dlut)(
			struct dpp *dpp,
			const struct tetrahedral_params *params);
	void (*dpp_cnv_set_alpha_keyer)(
			struct dpp *dpp_base,
			struct cnv_color_keyer_params *color_keyer);

	void (*dpp_get_gamut_remap)(struct dpp *dpp_base,
				    struct dpp_grph_csc_adjustment *adjust);
	void (*set_cursor_matrix)(
		struct dpp *dpp_base,
		enum dc_color_space color_space,
		struct dc_csc_transform cursor_csc_color_matrix);
};



#endif
