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

/**
 * DOC: overview
 *
 * The Output Plane Processor (OPP) block groups have functions that format
 * pixel streams such that they are suitable for display at the display device.
 * The key functions contained in the OPP are:
 *
 * - Adaptive Backlight Modulation (ABM)
 * - Formatter (FMT) which provide pixel-by-pixel operations for format the
 *   incoming pixel stream.
 * - Output Buffer that provide pixel replication, and overlapping.
 * - Interface between MPC and OPTC.
 * - Clock and reset generation.
 * - CRC generation.
 */

#ifndef __DAL_OPP_H__
#define __DAL_OPP_H__

#include "hw_shared.h"
#include "dc_hw_types.h"
#include "transform.h"
#include "mpc.h"

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
	enum dc_pixel_encoding pixel_encoding;
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

/**
 * struct pwl_float_data - Fixed point RGB color
 */
struct pwl_float_data {
	/**
	 * @r: Component Red.
	 */
	struct fixed31_32 r;

	/**
	 * @g: Component Green.
	 */

	struct fixed31_32 g;

	/**
	 * @b: Component Blue.
	 */
	struct fixed31_32 b;
};

struct mpc_tree_cfg {
	int num_pipes;
	int dpp[MAX_PIPES];
	int mpcc[MAX_PIPES];
};

struct output_pixel_processor {
	struct dc_context *ctx;
	uint32_t inst;
	struct pwl_params regamma_params;
	struct mpc_tree mpc_tree_params;
	bool mpcc_disconnect_pending[MAX_PIPES];
	const struct opp_funcs *funcs;
	uint32_t dyn_expansion;
};

enum fmt_stereo_action {
	FMT_STEREO_ACTION_ENABLE = 0,
	FMT_STEREO_ACTION_DISABLE,
	FMT_STEREO_ACTION_UPDATE_POLARITY
};

struct opp_grph_csc_adjustment {
	//enum grph_color_adjust_option color_adjust_option;
	enum dc_color_space c_space;
	enum dc_color_depth color_depth; /* clean up to uint32_t */
	enum graphics_csc_adjust_type   csc_adjust_type;
	int32_t adjust_divider;
	int32_t grph_cont;
	int32_t grph_sat;
	int32_t grph_bright;
	int32_t grph_hue;
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

enum oppbuf_display_segmentation {
	OPPBUF_DISPLAY_SEGMENTATION_1_SEGMENT = 0,
	OPPBUF_DISPLAY_SEGMENTATION_2_SEGMENT = 1,
	OPPBUF_DISPLAY_SEGMENTATION_4_SEGMENT = 2,
	OPPBUF_DISPLAY_SEGMENTATION_4_SEGMENT_SPLIT_LEFT = 3,
	OPPBUF_DISPLAY_SEGMENTATION_4_SEGMENT_SPLIT_RIGHT = 4
};

struct oppbuf_params {
	uint32_t active_width;
	enum oppbuf_display_segmentation mso_segmentation;
	uint32_t mso_overlap_pixel_num;
	uint32_t pixel_repetition;
	uint32_t num_segment_padded_pixels;
};

struct opp_funcs {


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

	void (*opp_program_stereo)(
		struct output_pixel_processor *opp,
		bool enable,
		const struct dc_crtc_timing *timing);

	void (*opp_pipe_clock_control)(
			struct output_pixel_processor *opp,
			bool enable);

	void (*opp_set_disp_pattern_generator)(
			struct output_pixel_processor *opp,
			enum controller_dp_test_pattern test_pattern,
			enum controller_dp_color_space color_space,
			enum dc_color_depth color_depth,
			const struct tg_color *solid_color,
			int width,
			int height,
			int offset);

	void (*opp_program_dpg_dimensions)(
				struct output_pixel_processor *opp,
				int width,
				int height);

	bool (*dpg_is_blanked)(
			struct output_pixel_processor *opp);

	bool (*dpg_is_pending)(struct output_pixel_processor *opp);


	void (*opp_dpg_set_blank_color)(
			struct output_pixel_processor *opp,
			const struct tg_color *color);

	void (*opp_program_left_edge_extra_pixel)(
			struct output_pixel_processor *opp,
			enum dc_pixel_encoding pixel_encoding,
			bool is_primary);

	uint32_t (*opp_get_left_edge_extra_pixel_count)(
			struct output_pixel_processor *opp,
			enum dc_pixel_encoding pixel_encoding,
			bool is_primary);
};

#endif
