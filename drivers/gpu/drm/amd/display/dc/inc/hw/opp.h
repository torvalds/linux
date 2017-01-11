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

#ifndef __DAL_OPP_H__
#define __DAL_OPP_H__

#include "hw_shared.h"
#include "transform.h"

struct fixed31_32;

/* TODO: Need cleanup */
enum clamping_range {
	CLAMPING_FULL_RANGE = 0,	   /* No Clamping */
	CLAMPING_LIMITED_RANGE_8BPC,   /* 8  bpc: Clamping 1  to FE */
	CLAMPING_LIMITED_RANGE_10BPC, /* 10 bpc: Clamping 4  to 3FB */
	CLAMPING_LIMITED_RANGE_12BPC, /* 12 bpc: Clamping 10 to FEF */
	/* Use programmable clampping value on FMT_CLAMP_COMPONENT_R/G/B. */
	CLAMPING_LIMITED_RANGE_PROGRAMMABLE
};

struct clamping_and_pixel_encoding_params {
	enum dc_pixel_encoding pixel_encoding; /* Pixel Encoding */
	enum clamping_range clamping_level; /* Clamping identifier */
	enum dc_color_depth c_depth; /* Deep color use. */
};

struct bit_depth_reduction_params {
	struct {
		/* truncate/round */
		/* trunc/round enabled*/
		uint32_t TRUNCATE_ENABLED:1;
		/* 2 bits: 0=6 bpc, 1=8 bpc, 2 = 10bpc*/
		uint32_t TRUNCATE_DEPTH:2;
		/* truncate or round*/
		uint32_t TRUNCATE_MODE:1;

		/* spatial dither */
		/* Spatial Bit Depth Reduction enabled*/
		uint32_t SPATIAL_DITHER_ENABLED:1;
		/* 2 bits: 0=6 bpc, 1 = 8 bpc, 2 = 10bpc*/
		uint32_t SPATIAL_DITHER_DEPTH:2;
		/* 0-3 to select patterns*/
		uint32_t SPATIAL_DITHER_MODE:2;
		/* Enable RGB random dithering*/
		uint32_t RGB_RANDOM:1;
		/* Enable Frame random dithering*/
		uint32_t FRAME_RANDOM:1;
		/* Enable HighPass random dithering*/
		uint32_t HIGHPASS_RANDOM:1;

		/* temporal dither*/
		 /* frame modulation enabled*/
		uint32_t FRAME_MODULATION_ENABLED:1;
		/* same as for trunc/spatial*/
		uint32_t FRAME_MODULATION_DEPTH:2;
		/* 2/4 gray levels*/
		uint32_t TEMPORAL_LEVEL:1;
		uint32_t FRC25:2;
		uint32_t FRC50:2;
		uint32_t FRC75:2;
	} flags;

	uint32_t r_seed_value;
	uint32_t b_seed_value;
	uint32_t g_seed_value;
};

enum wide_gamut_regamma_mode {
	/*  0x0  - BITS2:0 Bypass */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_BYPASS,
	/*  0x1  - Fixed curve sRGB 2.4 */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_SRGB24,
	/*  0x2  - Fixed curve xvYCC 2.22 */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_XYYCC22,
	/*  0x3  - Programmable control A */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_A,
	/*  0x4  - Programmable control B */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_B,
	/*  0x0  - BITS6:4 Bypass */
	WIDE_GAMUT_REGAMMA_MODE_OVL_BYPASS,
	/*  0x1  - Fixed curve sRGB 2.4 */
	WIDE_GAMUT_REGAMMA_MODE_OVL_SRGB24,
	/*  0x2  - Fixed curve xvYCC 2.22 */
	WIDE_GAMUT_REGAMMA_MODE_OVL_XYYCC22,
	/*  0x3  - Programmable control A */
	WIDE_GAMUT_REGAMMA_MODE_OVL_MATRIX_A,
	/*  0x4  - Programmable control B */
	WIDE_GAMUT_REGAMMA_MODE_OVL_MATRIX_B
};

struct gamma_pixel {
	struct fixed31_32 r;
	struct fixed31_32 g;
	struct fixed31_32 b;
};

enum channel_name {
	CHANNEL_NAME_RED,
	CHANNEL_NAME_GREEN,
	CHANNEL_NAME_BLUE
};

struct custom_float_format {
	uint32_t mantissa_bits;
	uint32_t exponenta_bits;
	bool sign;
};

struct custom_float_value {
	uint32_t mantissa;
	uint32_t exponenta;
	uint32_t value;
	bool negative;
};

struct hw_x_point {
	uint32_t custom_float_x;
	struct fixed31_32 x;
	struct fixed31_32 regamma_y_red;
	struct fixed31_32 regamma_y_green;
	struct fixed31_32 regamma_y_blue;

};

struct pwl_float_data_ex {
	struct fixed31_32 r;
	struct fixed31_32 g;
	struct fixed31_32 b;
	struct fixed31_32 delta_r;
	struct fixed31_32 delta_g;
	struct fixed31_32 delta_b;
};

enum hw_point_position {
	/* hw point sits between left and right sw points */
	HW_POINT_POSITION_MIDDLE,
	/* hw point lays left from left (smaller) sw point */
	HW_POINT_POSITION_LEFT,
	/* hw point lays stays from right (bigger) sw point */
	HW_POINT_POSITION_RIGHT
};

struct gamma_point {
	int32_t left_index;
	int32_t right_index;
	enum hw_point_position pos;
	struct fixed31_32 coeff;
};

struct pixel_gamma_point {
	struct gamma_point r;
	struct gamma_point g;
	struct gamma_point b;
};

struct gamma_coefficients {
	struct fixed31_32 a0[3];
	struct fixed31_32 a1[3];
	struct fixed31_32 a2[3];
	struct fixed31_32 a3[3];
	struct fixed31_32 user_gamma[3];
	struct fixed31_32 user_contrast;
	struct fixed31_32 user_brightness;
};

struct pwl_float_data {
	struct fixed31_32 r;
	struct fixed31_32 g;
	struct fixed31_32 b;
};

enum opp_regamma {
	OPP_REGAMMA_BYPASS = 0,
	OPP_REGAMMA_SRGB,
	OPP_REGAMMA_3_6,
	OPP_REGAMMA_USER,
};

struct output_pixel_processor {
	struct dc_context *ctx;
	uint32_t inst;
	struct pwl_params *regamma_params;
	const struct opp_funcs *funcs;
};

enum fmt_stereo_action {
	FMT_STEREO_ACTION_ENABLE = 0,
	FMT_STEREO_ACTION_DISABLE,
	FMT_STEREO_ACTION_UPDATE_POLARITY
};

enum graphics_csc_adjust_type {
	GRAPHICS_CSC_ADJUST_TYPE_BYPASS = 0,
	GRAPHICS_CSC_ADJUST_TYPE_HW, /* without adjustments */
	GRAPHICS_CSC_ADJUST_TYPE_SW  /*use adjustments */
};

struct default_adjustment {
	enum lb_pixel_depth lb_color_depth;
	enum dc_color_space out_color_space;
	enum dc_color_space in_color_space;
	enum dc_color_depth color_depth;
	enum pixel_format surface_pixel_format;
	enum graphics_csc_adjust_type csc_adjust_type;
	bool force_hw_default;
};

enum grph_color_adjust_option {
	GRPH_COLOR_MATRIX_HW_DEFAULT = 1,
	GRPH_COLOR_MATRIX_SW
};

struct opp_grph_csc_adjustment {
	enum grph_color_adjust_option color_adjust_option;
	enum dc_color_space c_space;
	enum dc_color_depth color_depth; /* clean up to uint32_t */
	enum graphics_csc_adjust_type   csc_adjust_type;
	int32_t adjust_divider;
	int32_t grph_cont;
	int32_t grph_sat;
	int32_t grph_bright;
	int32_t grph_hue;
};

struct out_csc_color_matrix {
	enum dc_color_space color_space;
	uint16_t regval[12];
};

/* Underlay related types */

struct hw_adjustment_range {
	int32_t hw_default;
	int32_t min;
	int32_t max;
	int32_t step;
	uint32_t divider; /* (actually HW range is min/divider; divider !=0) */
};

enum ovl_csc_adjust_item {
	OVERLAY_BRIGHTNESS = 0,
	OVERLAY_GAMMA,
	OVERLAY_CONTRAST,
	OVERLAY_SATURATION,
	OVERLAY_HUE,
	OVERLAY_ALPHA,
	OVERLAY_ALPHA_PER_PIX,
	OVERLAY_COLOR_TEMPERATURE
};

struct opp_funcs {
	void (*opp_power_on_regamma_lut)(
		struct output_pixel_processor *opp,
		bool power_on);

	bool (*opp_program_regamma_pwl)(
		struct output_pixel_processor *opp,
		const struct pwl_params *params);

	void (*opp_set_regamma_mode)(struct output_pixel_processor *opp,
			enum opp_regamma mode);

	void (*opp_set_csc_adjustment)(
		struct output_pixel_processor *opp,
		const struct out_csc_color_matrix *tbl_entry);

	void (*opp_set_csc_default)(
		struct output_pixel_processor *opp,
		const struct default_adjustment *default_adjust);

	/* FORMATTER RELATED */

	void (*opp_program_fmt)(
			struct output_pixel_processor *opp,
			struct bit_depth_reduction_params *fmt_bit_depth,
			struct clamping_and_pixel_encoding_params *clamping);

	void (*opp_set_dyn_expansion)(
		struct output_pixel_processor *opp,
		enum dc_color_space color_sp,
		enum dc_color_depth color_dpth,
		enum signal_type signal);

	void (*opp_program_bit_depth_reduction)(
		struct output_pixel_processor *opp,
		const struct bit_depth_reduction_params *params);

	/* underlay related */
	void (*opp_get_underlay_adjustment_range)(
			struct output_pixel_processor *opp,
			enum ovl_csc_adjust_item overlay_adjust_item,
			struct hw_adjustment_range *range);

	void (*opp_destroy)(struct output_pixel_processor **opp);
};

#endif
