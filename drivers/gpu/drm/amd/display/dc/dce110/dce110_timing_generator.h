/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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

#ifndef __DC_TIMING_GENERATOR_DCE110_H__
#define __DC_TIMING_GENERATOR_DCE110_H__

#include "timing_generator.h"
#include "../include/grph_object_id.h"

/* GSL Sync related values */

/* In VSync mode, after 4 units of time, master pipe will generate
 * flip_ready signal */
#define VFLIP_READY_DELAY 4
/* In HSync mode, after 2 units of time, master pipe will generate
 * flip_ready signal */
#define HFLIP_READY_DELAY 2
/* 6 lines delay between forcing flip and checking all pipes ready */
#define HFLIP_CHECK_DELAY 6
/* 3 lines before end of frame */
#define FLIP_READY_BACK_LOOKUP 3

/* Trigger Source Select - ASIC-defendant, actual values for the
 * register programming */
enum trigger_source_select {
	TRIGGER_SOURCE_SELECT_LOGIC_ZERO = 0,
	TRIGGER_SOURCE_SELECT_CRTC_VSYNCA = 1,
	TRIGGER_SOURCE_SELECT_CRTC_HSYNCA = 2,
	TRIGGER_SOURCE_SELECT_CRTC_VSYNCB = 3,
	TRIGGER_SOURCE_SELECT_CRTC_HSYNCB = 4,
	TRIGGER_SOURCE_SELECT_GENERICF = 5,
	TRIGGER_SOURCE_SELECT_GENERICE = 6,
	TRIGGER_SOURCE_SELECT_VSYNCA = 7,
	TRIGGER_SOURCE_SELECT_HSYNCA = 8,
	TRIGGER_SOURCE_SELECT_VSYNCB = 9,
	TRIGGER_SOURCE_SELECT_HSYNCB = 10,
	TRIGGER_SOURCE_SELECT_HPD1 = 11,
	TRIGGER_SOURCE_SELECT_HPD2 = 12,
	TRIGGER_SOURCE_SELECT_GENERICD = 13,
	TRIGGER_SOURCE_SELECT_GENERICC = 14,
	TRIGGER_SOURCE_SELECT_VIDEO_CAPTURE = 15,
	TRIGGER_SOURCE_SELECT_GSL_GROUP0 = 16,
	TRIGGER_SOURCE_SELECT_GSL_GROUP1 = 17,
	TRIGGER_SOURCE_SELECT_GSL_GROUP2 = 18,
	TRIGGER_SOURCE_SELECT_BLONY = 19,
	TRIGGER_SOURCE_SELECT_GENERICA = 20,
	TRIGGER_SOURCE_SELECT_GENERICB = 21,
	TRIGGER_SOURCE_SELECT_GSL_ALLOW_FLIP = 22,
	TRIGGER_SOURCE_SELECT_MANUAL_TRIGGER = 23
};

/* Trigger Source Select - ASIC-dependant, actual values for the
 * register programming */
enum trigger_polarity_select {
	TRIGGER_POLARITY_SELECT_LOGIC_ZERO = 0,
	TRIGGER_POLARITY_SELECT_CRTC = 1,
	TRIGGER_POLARITY_SELECT_GENERICA = 2,
	TRIGGER_POLARITY_SELECT_GENERICB = 3,
	TRIGGER_POLARITY_SELECT_HSYNCA = 4,
	TRIGGER_POLARITY_SELECT_HSYNCB = 5,
	TRIGGER_POLARITY_SELECT_VIDEO_CAPTURE = 6,
	TRIGGER_POLARITY_SELECT_GENERICC = 7
};


struct dce110_timing_generator_offsets {
	int32_t crtc;
	int32_t dcp;

	/* DCE80 use only */
	int32_t dmif;
};

struct dce110_timing_generator {
	struct timing_generator base;
	struct dce110_timing_generator_offsets offsets;
	struct dce110_timing_generator_offsets derived_offsets;

	enum controller_id controller_id;

	uint32_t max_h_total;
	uint32_t max_v_total;

	uint32_t min_h_blank;
	uint32_t min_h_front_porch;
	uint32_t min_h_back_porch;

	/* DCE 12 */
	uint32_t min_h_sync_width;
	uint32_t min_v_sync_width;
	uint32_t min_v_blank;

};

#define DCE110TG_FROM_TG(tg)\
	container_of(tg, struct dce110_timing_generator, base)

void dce110_timing_generator_construct(
	struct dce110_timing_generator *tg,
	struct dc_context *ctx,
	uint32_t instance,
	const struct dce110_timing_generator_offsets *offsets);

/* determine if given timing can be supported by TG */
bool dce110_timing_generator_validate_timing(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing,
	enum signal_type signal);

/******** HW programming ************/

/* Program timing generator with given timing */
bool dce110_timing_generator_program_timing_generator(
	struct timing_generator *tg,
	const struct dc_crtc_timing *dc_crtc_timing);

/* Disable/Enable Timing Generator */
bool dce110_timing_generator_enable_crtc(struct timing_generator *tg);
bool dce110_timing_generator_disable_crtc(struct timing_generator *tg);

void dce110_timing_generator_set_early_control(
		struct timing_generator *tg,
		uint32_t early_cntl);

/**************** TG current status ******************/

/* return the current frame counter. Used by Linux kernel DRM */
uint32_t dce110_timing_generator_get_vblank_counter(
		struct timing_generator *tg);

void dce110_timing_generator_get_position(
	struct timing_generator *tg,
	struct crtc_position *position);

/* return true if TG counter is moving. false if TG is stopped */
bool dce110_timing_generator_is_counter_moving(struct timing_generator *tg);

/* wait until TG is in beginning of vertical blank region */
void dce110_timing_generator_wait_for_vblank(struct timing_generator *tg);

/* wait until TG is in beginning of active region */
void dce110_timing_generator_wait_for_vactive(struct timing_generator *tg);

/*********** Timing Generator Synchronization routines ****/

/* Setups Global Swap Lock group, TimingServer or TimingClient*/
void dce110_timing_generator_setup_global_swap_lock(
	struct timing_generator *tg,
	const struct dcp_gsl_params *gsl_params);

/* Clear all the register writes done by setup_global_swap_lock */
void dce110_timing_generator_tear_down_global_swap_lock(
	struct timing_generator *tg);

/* Reset crtc position on master VSync */
void dce110_timing_generator_enable_crtc_reset(
	struct timing_generator *tg,
	int source,
	struct crtc_trigger_info *crtc_tp);

/* Reset slave controllers on master VSync */
void dce110_timing_generator_enable_reset_trigger(
	struct timing_generator *tg,
	int source);

/* disabling trigger-reset */
void dce110_timing_generator_disable_reset_trigger(
	struct timing_generator *tg);

/* Checks whether CRTC triggered reset occurred */
bool dce110_timing_generator_did_triggered_reset_occur(
	struct timing_generator *tg);

/******** Stuff to move to other virtual HW objects *****************/
/* Move to enable accelerated mode */
void dce110_timing_generator_disable_vga(struct timing_generator *tg);
/* TODO: Should we move it to transform */
/* Fully program CRTC timing in timing generator */
void dce110_timing_generator_program_blanking(
	struct timing_generator *tg,
	const struct dc_crtc_timing *timing);

/* TODO: Should we move it to opp? */
/* Combine with below and move YUV/RGB color conversion to SW layer */
void dce110_timing_generator_program_blank_color(
	struct timing_generator *tg,
	const struct tg_color *black_color);
/* Combine with above and move YUV/RGB color conversion to SW layer */
void dce110_timing_generator_set_overscan_color_black(
	struct timing_generator *tg,
	const struct tg_color *color);
void dce110_timing_generator_color_space_to_black_color(
		enum dc_color_space colorspace,
	struct tg_color *black_color);
/*************** End-of-move ********************/

/* Not called yet */
void dce110_timing_generator_set_test_pattern(
	struct timing_generator *tg,
	/* TODO: replace 'controller_dp_test_pattern' by 'test_pattern_mode'
	 * because this is not DP-specific (which is probably somewhere in DP
	 * encoder) */
	enum controller_dp_test_pattern test_pattern,
	enum dc_color_depth color_depth);

void dce110_timing_generator_set_drr(
	struct timing_generator *tg,
	const struct drr_params *params);

void dce110_timing_generator_set_static_screen_control(
	struct timing_generator *tg,
	uint32_t value);

void dce110_timing_generator_get_crtc_scanoutpos(
	struct timing_generator *tg,
	uint32_t *v_blank_start,
	uint32_t *v_blank_end,
	uint32_t *h_position,
	uint32_t *v_position);

void dce110_timing_generator_enable_advanced_request(
	struct timing_generator *tg,
	bool enable,
	const struct dc_crtc_timing *timing);

void dce110_timing_generator_set_lock_master(struct timing_generator *tg,
		bool lock);

void dce110_tg_program_blank_color(struct timing_generator *tg,
	const struct tg_color *black_color);

void dce110_tg_set_overscan_color(struct timing_generator *tg,
	const struct tg_color *overscan_color);

void dce110_tg_program_timing(struct timing_generator *tg,
	const struct dc_crtc_timing *timing,
	bool use_vbios);

bool dce110_tg_is_blanked(struct timing_generator *tg);

void dce110_tg_set_blank(struct timing_generator *tg,
		bool enable_blanking);

bool dce110_tg_validate_timing(struct timing_generator *tg,
	const struct dc_crtc_timing *timing);

void dce110_tg_wait_for_state(struct timing_generator *tg,
	enum crtc_state state);

void dce110_tg_set_colors(struct timing_generator *tg,
	const struct tg_color *blank_color,
	const struct tg_color *overscan_color);

bool dce110_arm_vert_intr(
		struct timing_generator *tg, uint8_t width);

bool dce110_configure_crc(struct timing_generator *tg,
			  const struct crc_params *params);

bool dce110_get_crc(struct timing_generator *tg,
		    uint32_t *r_cr, uint32_t *g_y, uint32_t *b_cb);

#endif /* __DC_TIMING_GENERATOR_DCE110_H__ */
