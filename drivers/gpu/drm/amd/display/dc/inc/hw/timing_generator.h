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

#ifndef __DAL_TIMING_GENERATOR_TYPES_H__
#define __DAL_TIMING_GENERATOR_TYPES_H__

struct dc_bios;

/**
 *  These parameters are required as input when doing blanking/Unblanking
*/
#define MAX_TG_COLOR_VALUE 0x3FF

struct tg_color {
	/* Maximum 10 bits color value */
	uint16_t color_r_cr;
	uint16_t color_g_y;
	uint16_t color_b_cb;
};

/* Contains CRTC vertical/horizontal pixel counters */
struct crtc_position {
	uint32_t vertical_count;
	uint32_t horizontal_count;
	uint32_t nominal_vcount;
};

struct dcp_gsl_params {
	int gsl_group;
	int gsl_master;
};

#define LEFT_EYE_3D_PRIMARY_SURFACE 1
#define RIGHT_EYE_3D_PRIMARY_SURFACE 0

enum test_pattern_dyn_range {
	TEST_PATTERN_DYN_RANGE_VESA = 0,
	TEST_PATTERN_DYN_RANGE_CEA
};

enum test_pattern_mode {
	TEST_PATTERN_MODE_COLORSQUARES_RGB = 0,
	TEST_PATTERN_MODE_COLORSQUARES_YCBCR601,
	TEST_PATTERN_MODE_COLORSQUARES_YCBCR709,
	TEST_PATTERN_MODE_VERTICALBARS,
	TEST_PATTERN_MODE_HORIZONTALBARS,
	TEST_PATTERN_MODE_SINGLERAMP_RGB,
	TEST_PATTERN_MODE_DUALRAMP_RGB
};

enum test_pattern_color_format {
	TEST_PATTERN_COLOR_FORMAT_BPC_6 = 0,
	TEST_PATTERN_COLOR_FORMAT_BPC_8,
	TEST_PATTERN_COLOR_FORMAT_BPC_10,
	TEST_PATTERN_COLOR_FORMAT_BPC_12
};

enum controller_dp_test_pattern {
	CONTROLLER_DP_TEST_PATTERN_D102 = 0,
	CONTROLLER_DP_TEST_PATTERN_SYMBOLERROR,
	CONTROLLER_DP_TEST_PATTERN_PRBS7,
	CONTROLLER_DP_TEST_PATTERN_COLORSQUARES,
	CONTROLLER_DP_TEST_PATTERN_VERTICALBARS,
	CONTROLLER_DP_TEST_PATTERN_HORIZONTALBARS,
	CONTROLLER_DP_TEST_PATTERN_COLORRAMP,
	CONTROLLER_DP_TEST_PATTERN_VIDEOMODE,
	CONTROLLER_DP_TEST_PATTERN_RESERVED_8,
	CONTROLLER_DP_TEST_PATTERN_RESERVED_9,
	CONTROLLER_DP_TEST_PATTERN_RESERVED_A,
	CONTROLLER_DP_TEST_PATTERN_COLORSQUARES_CEA
};

enum crtc_state {
	CRTC_STATE_VBLANK = 0,
	CRTC_STATE_VACTIVE
};

struct timing_generator {
	const struct timing_generator_funcs *funcs;
	struct dc_bios *bp;
	struct dc_context *ctx;
	int inst;
};

struct dc_crtc_timing;

struct drr_params;

struct timing_generator_funcs {
	bool (*validate_timing)(struct timing_generator *tg,
							const struct dc_crtc_timing *timing);
	void (*program_timing)(struct timing_generator *tg,
							const struct dc_crtc_timing *timing,
							bool use_vbios);
	bool (*enable_crtc)(struct timing_generator *tg);
	bool (*disable_crtc)(struct timing_generator *tg);
	bool (*is_counter_moving)(struct timing_generator *tg);
	void (*get_position)(struct timing_generator *tg,
								int32_t *h_position,
								int32_t *v_position);
	uint32_t (*get_frame_count)(struct timing_generator *tg);
	uint32_t (*get_scanoutpos)(
		struct timing_generator *tg,
		uint32_t *vbl,
		uint32_t *position);
	void (*set_early_control)(struct timing_generator *tg,
							   uint32_t early_cntl);
	void (*wait_for_state)(struct timing_generator *tg,
							enum crtc_state state);
	void (*set_blank)(struct timing_generator *tg,
					bool enable_blanking);
	bool (*is_blanked)(struct timing_generator *tg);
	void (*set_overscan_blank_color) (struct timing_generator *tg, const struct tg_color *color);
	void (*set_blank_color)(struct timing_generator *tg, const struct tg_color *color);
	void (*set_colors)(struct timing_generator *tg,
						const struct tg_color *blank_color,
						const struct tg_color *overscan_color);

	void (*disable_vga)(struct timing_generator *tg);
	bool (*did_triggered_reset_occur)(struct timing_generator *tg);
	void (*setup_global_swap_lock)(struct timing_generator *tg,
							const struct dcp_gsl_params *gsl_params);
	void (*unlock)(struct timing_generator *tg);
	void (*lock)(struct timing_generator *tg);
	void (*enable_reset_trigger)(struct timing_generator *tg, int source_tg_inst);
	void (*disable_reset_trigger)(struct timing_generator *tg);
	void (*tear_down_global_swap_lock)(struct timing_generator *tg);
	void (*enable_advanced_request)(struct timing_generator *tg,
					bool enable, const struct dc_crtc_timing *timing);
	void (*set_drr)(struct timing_generator *tg, const struct drr_params *params);
	void (*set_static_screen_control)(struct timing_generator *tg,
							uint32_t value);
	void (*set_test_pattern)(
		struct timing_generator *tg,
		enum controller_dp_test_pattern test_pattern,
		enum dc_color_depth color_depth);

};

#endif
