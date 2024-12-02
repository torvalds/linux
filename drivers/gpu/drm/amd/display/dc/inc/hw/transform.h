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

#ifndef __DAL_TRANSFORM_H__
#define __DAL_TRANSFORM_H__

#include "hw_shared.h"
#include "dc_hw_types.h"
#include "fixed31_32.h"

#define CSC_TEMPERATURE_MATRIX_SIZE 12

struct bit_depth_reduction_params;

struct transform {
	const struct transform_funcs *funcs;
	struct dc_context *ctx;
	int inst;
	struct dpp_caps *caps;
	struct pwl_params regamma_params;
};

/* Colorimetry */
enum colorimetry {
	COLORIMETRY_NO_DATA = 0,
	COLORIMETRY_ITU601 = 1,
	COLORIMETRY_ITU709 = 2,
	COLORIMETRY_EXTENDED = 3
};

enum colorimetry_ext {
	COLORIMETRYEX_XVYCC601 = 0,
	COLORIMETRYEX_XVYCC709 = 1,
	COLORIMETRYEX_SYCC601 = 2,
	COLORIMETRYEX_ADOBEYCC601 = 3,
	COLORIMETRYEX_ADOBERGB = 4,
	COLORIMETRYEX_BT2020YCC = 5,
	COLORIMETRYEX_BT2020RGBYCBCR = 6,
	COLORIMETRYEX_RESERVED = 7
};

enum active_format_info {
	ACTIVE_FORMAT_NO_DATA = 0,
	ACTIVE_FORMAT_VALID = 1
};

/* Active format aspect ratio */
enum active_format_aspect_ratio {
	ACTIVE_FORMAT_ASPECT_RATIO_SAME_AS_PICTURE = 8,
	ACTIVE_FORMAT_ASPECT_RATIO_4_3 = 9,
	ACTIVE_FORMAT_ASPECT_RATIO_16_9 = 0XA,
	ACTIVE_FORMAT_ASPECT_RATIO_14_9 = 0XB
};

enum bar_info {
	BAR_INFO_NOT_VALID = 0,
	BAR_INFO_VERTICAL_VALID = 1,
	BAR_INFO_HORIZONTAL_VALID = 2,
	BAR_INFO_BOTH_VALID = 3
};

enum picture_scaling {
	PICTURE_SCALING_UNIFORM = 0,
	PICTURE_SCALING_HORIZONTAL = 1,
	PICTURE_SCALING_VERTICAL = 2,
	PICTURE_SCALING_BOTH = 3
};

/* RGB quantization range */
enum rgb_quantization_range {
	RGB_QUANTIZATION_DEFAULT_RANGE = 0,
	RGB_QUANTIZATION_LIMITED_RANGE = 1,
	RGB_QUANTIZATION_FULL_RANGE = 2,
	RGB_QUANTIZATION_RESERVED = 3
};

/* YYC quantization range */
enum yyc_quantization_range {
	YYC_QUANTIZATION_LIMITED_RANGE = 0,
	YYC_QUANTIZATION_FULL_RANGE = 1,
	YYC_QUANTIZATION_RESERVED2 = 2,
	YYC_QUANTIZATION_RESERVED3 = 3
};

enum graphics_gamut_adjust_type {
	GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS = 0,
	GRAPHICS_GAMUT_ADJUST_TYPE_HW, /* without adjustments */
	GRAPHICS_GAMUT_ADJUST_TYPE_SW /* use adjustments */
};

enum lb_memory_config {
	/* Enable all 3 pieces of memory */
	LB_MEMORY_CONFIG_0 = 0,

	/* Enable only the first piece of memory */
	LB_MEMORY_CONFIG_1 = 1,

	/* Enable only the second piece of memory */
	LB_MEMORY_CONFIG_2 = 2,

	/* Only applicable in 4:2:0 mode, enable all 3 pieces of memory and the
	 * last piece of chroma memory used for the luma storage
	 */
	LB_MEMORY_CONFIG_3 = 3
};

struct xfm_grph_csc_adjustment {
	struct fixed31_32 temperature_matrix[CSC_TEMPERATURE_MATRIX_SIZE];
	enum graphics_gamut_adjust_type gamut_adjust_type;
};

struct overscan_info {
	int left;
	int right;
	int top;
	int bottom;
};

struct scaling_ratios {
	struct fixed31_32 horz;
	struct fixed31_32 vert;
	struct fixed31_32 horz_c;
	struct fixed31_32 vert_c;
};

struct sharpness_adj {
	int horz;
	int vert;
};

struct line_buffer_params {
	bool alpha_en;
	bool pixel_expan_mode;
	bool interleave_en;
	int dynamic_pixel_depth;
	enum lb_pixel_depth depth;
};

struct scl_inits {
	struct fixed31_32 h;
	struct fixed31_32 h_c;
	struct fixed31_32 v;
	struct fixed31_32 v_c;
};

struct scaler_data {
	int h_active;
	int v_active;
	struct scaling_taps taps;
	struct rect viewport;
	struct rect viewport_c;
	struct rect recout;
	struct scaling_ratios ratios;
	struct scl_inits inits;
	struct sharpness_adj sharpness;
	enum pixel_format format;
	struct line_buffer_params lb_params;
};

struct transform_funcs {
	void (*transform_reset)(struct transform *xfm);

	void (*transform_set_scaler)(struct transform *xfm,
			const struct scaler_data *scl_data);

	void (*transform_set_pixel_storage_depth)(
			struct transform *xfm,
			enum lb_pixel_depth depth,
			const struct bit_depth_reduction_params *bit_depth_params);

	bool (*transform_get_optimal_number_of_taps)(
			struct transform *xfm,
			struct scaler_data *scl_data,
			const struct scaling_taps *in_taps);

	void (*transform_set_gamut_remap)(
			struct transform *xfm,
			const struct xfm_grph_csc_adjustment *adjust);

	void (*opp_set_csc_default)(
		struct transform *xfm,
		const struct default_adjustment *default_adjust);

	void (*opp_set_csc_adjustment)(
		struct transform *xfm,
		const struct out_csc_color_matrix *tbl_entry);

	void (*opp_power_on_regamma_lut)(
		struct transform *xfm,
		bool power_on);

	void (*opp_program_regamma_lut)(
			struct transform *xfm,
			const struct pwl_result_data *rgb,
			uint32_t num);

	void (*opp_configure_regamma_lut)(
			struct transform *xfm,
			bool is_ram_a);

	void (*opp_program_regamma_lutb_settings)(
			struct transform *xfm,
			const struct pwl_params *params);

	void (*opp_program_regamma_luta_settings)(
			struct transform *xfm,
			const struct pwl_params *params);

	void (*opp_program_regamma_pwl)(
		struct transform *xfm, const struct pwl_params *params);

	void (*opp_set_regamma_mode)(
			struct transform *xfm_base,
			enum opp_regamma mode);

	void (*ipp_set_degamma)(
			struct transform *xfm_base,
			enum ipp_degamma_mode mode);

	void (*ipp_program_input_lut)(
			struct transform *xfm_base,
			const struct dc_gamma *gamma);

	void (*ipp_program_degamma_pwl)(struct transform *xfm_base,
									 const struct pwl_params *params);

	void (*ipp_setup)(
			struct transform *xfm_base,
			enum surface_pixel_format format,
			enum expansion_mode mode,
			struct dc_csc_transform input_csc_color_matrix,
			enum dc_color_space input_color_space);

	void (*ipp_full_bypass)(struct transform *xfm_base);

	void (*set_cursor_attributes)(
			struct transform *xfm_base,
			const struct dc_cursor_attributes *attr);

};

const uint16_t *get_filter_2tap_16p(void);
const uint16_t *get_filter_2tap_64p(void);
const uint16_t *get_filter_3tap_16p(struct fixed31_32 ratio);
const uint16_t *get_filter_3tap_64p(struct fixed31_32 ratio);
const uint16_t *get_filter_4tap_16p(struct fixed31_32 ratio);
const uint16_t *get_filter_4tap_64p(struct fixed31_32 ratio);
const uint16_t *get_filter_5tap_64p(struct fixed31_32 ratio);
const uint16_t *get_filter_6tap_64p(struct fixed31_32 ratio);
const uint16_t *get_filter_7tap_64p(struct fixed31_32 ratio);
const uint16_t *get_filter_8tap_64p(struct fixed31_32 ratio);


/* Defines the pixel processing capability of the DSCL */
enum dscl_data_processing_format {
	DSCL_DATA_PRCESSING_FIXED_FORMAT,	/* The DSCL processes pixel data in fixed format */
	DSCL_DATA_PRCESSING_FLOAT_FORMAT,	/* The DSCL processes pixel data in float format */
};

/*
 * The DPP capabilities structure contains enumerations to specify the
 * HW processing features and an associated function pointers to
 * provide the function interface that can be overloaded for implementations
 * based on different capabilities
 */
struct dpp_caps {
	/* DSCL processing pixel data in fixed or float format */
	enum dscl_data_processing_format dscl_data_proc_format;

	/* max LB partitions */
	unsigned int max_lb_partitions;

	/* Calculates the number of partitions in the line buffer.
	 * The implementation of this function is overloaded for
	 * different versions of DSCL LB.
	 */
	void (*dscl_calc_lb_num_partitions)(
			const struct scaler_data *scl_data,
			enum lb_memory_config lb_config,
			int *num_part_y,
			int *num_part_c);
};


#endif
