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

#include "dc_hw_types.h"
#include "fixed31_32.h"

#define CSC_TEMPERATURE_MATRIX_SIZE 9

struct bit_depth_reduction_params;

struct transform {
	const struct transform_funcs *funcs;
	struct dc_context *ctx;
	int inst;
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

struct xfm_grph_csc_adjustment {
	struct fixed31_32 temperature_matrix[CSC_TEMPERATURE_MATRIX_SIZE];
	enum graphics_gamut_adjust_type gamut_adjust_type;
};

enum lb_pixel_depth {
	/* do not change the values because it is used as bit vector */
	LB_PIXEL_DEPTH_18BPP = 1,
	LB_PIXEL_DEPTH_24BPP = 2,
	LB_PIXEL_DEPTH_30BPP = 4,
	LB_PIXEL_DEPTH_36BPP = 8
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

struct scaler_data {
	int h_active;
	int v_active;
	struct scaling_taps taps;
	struct rect viewport;
	struct rect recout;
	struct scaling_ratios ratios;
	struct sharpness_adj sharpness;
	enum pixel_format format;
	struct line_buffer_params lb_params;
};

struct transform_funcs {
	void (*transform_reset)(struct transform *xfm);

	void (*transform_set_scaler)(struct transform *xfm,
			const struct scaler_data *scl_data);

	void (*transform_set_gamut_remap)(
			struct transform *xfm,
			const struct xfm_grph_csc_adjustment *adjust);

	void (*transform_set_pixel_storage_depth)(
			struct transform *xfm,
			enum lb_pixel_depth depth,
			const struct bit_depth_reduction_params *bit_depth_params);

	bool (*transform_get_optimal_number_of_taps)(
			struct transform *xfm,
			struct scaler_data *scl_data,
			const struct scaling_taps *in_taps);
};

extern const uint16_t filter_2tap_16p[18];
extern const uint16_t filter_2tap_64p[66];
const uint16_t *get_filter_3tap_16p(struct fixed31_32 ratio);
const uint16_t *get_filter_3tap_64p(struct fixed31_32 ratio);
const uint16_t *get_filter_4tap_16p(struct fixed31_32 ratio);
const uint16_t *get_filter_4tap_64p(struct fixed31_32 ratio);

#endif
