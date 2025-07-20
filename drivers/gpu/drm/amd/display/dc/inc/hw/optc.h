/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
 * Output Pipe Timing Combiner (OPTC) includes two major functional blocks:
 * Output Data Mapper (ODM) and Output Timing Generator (OTG).
 *
 * - ODM: It is Output Data Mapping block. It can combine input data from
 *   multiple OPP data pipes into one single data stream or split data from one
 *   OPP data pipe into multiple data streams or just bypass OPP data to DIO.
 * - OTG: It is Output Timing Generator. It generates display timing signals to
 *   drive the display output.
 */

#ifndef __DC_OPTC_H__
#define __DC_OPTC_H__

#include "timing_generator.h"

struct optc {
	struct timing_generator base;

	const struct dcn_optc_registers *tg_regs;
	const struct dcn_optc_shift *tg_shift;
	const struct dcn_optc_mask *tg_mask;

	int opp_count;

	uint32_t max_h_total;
	uint32_t max_v_total;

	uint32_t min_h_blank;

	uint32_t min_h_sync_width;
	uint32_t min_v_sync_width;
	uint32_t min_v_blank;
	uint32_t min_v_blank_interlace;

	int vstartup_start;
	int vupdate_offset;
	int vupdate_width;
	int vready_offset;
	int pstate_keepout;
	struct dc_crtc_timing orginal_patched_timing;
	enum signal_type signal;
	uint32_t max_frame_count;
};

void optc1_read_otg_state(struct timing_generator *optc, struct dcn_otg_state *s);

bool optc1_get_hw_timing(struct timing_generator *tg, struct dc_crtc_timing *hw_crtc_timing);

bool optc1_validate_timing(struct timing_generator *optc,
			   const struct dc_crtc_timing *timing);

void optc1_program_timing(struct timing_generator *optc,
			  const struct dc_crtc_timing *dc_crtc_timing,
			  int vready_offset,
			  int vstartup_start,
			  int vupdate_offset,
			  int vupdate_width,
			  int pstate_keepout,
			  const enum signal_type signal,
			  bool use_vbios);

void optc1_setup_vertical_interrupt0(struct timing_generator *optc,
				     uint32_t start_line,
				     uint32_t end_line);

void optc1_setup_vertical_interrupt1(struct timing_generator *optc,
				     uint32_t start_line);

void optc1_setup_vertical_interrupt2(struct timing_generator *optc,
				     uint32_t start_line);

void optc1_program_global_sync(struct timing_generator *optc,
			       int vready_offset,
			       int vstartup_start,
			       int vupdate_offset,
			       int vupdate_width,
				   int pstate_keepout);

bool optc1_disable_crtc(struct timing_generator *optc);

bool optc1_is_counter_moving(struct timing_generator *optc);

void optc1_get_position(struct timing_generator *optc,
			struct crtc_position *position);

uint32_t optc1_get_vblank_counter(struct timing_generator *optc);

void optc1_get_crtc_scanoutpos(struct timing_generator *optc,
			       uint32_t *v_blank_start,
			       uint32_t *v_blank_end,
			       uint32_t *h_position,
			       uint32_t *v_position);

void optc1_set_early_control(struct timing_generator *optc,
			     uint32_t early_cntl);

void optc1_wait_for_state(struct timing_generator *optc,
			  enum crtc_state state);

void optc1_set_blank(struct timing_generator *optc,
		     bool enable_blanking);

bool optc1_is_blanked(struct timing_generator *optc);

void optc1_program_blank_color(struct timing_generator *optc,
			       const struct tg_color *black_color);

bool optc1_did_triggered_reset_occur(struct timing_generator *optc);

void optc1_enable_reset_trigger(struct timing_generator *optc, int source_tg_inst);

void optc1_disable_reset_trigger(struct timing_generator *optc);

void optc1_lock(struct timing_generator *optc);

void optc1_unlock(struct timing_generator *optc);

void optc1_enable_optc_clock(struct timing_generator *optc, bool enable);

void optc1_set_drr(struct timing_generator *optc,
		   const struct drr_params *params);

void optc1_set_vtotal_min_max(struct timing_generator *optc, int vtotal_min, int vtotal_max);

void optc1_set_static_screen_control(struct timing_generator *optc,
				     uint32_t event_triggers,
				     uint32_t num_frames);

void optc1_program_stereo(struct timing_generator *optc,
			  const struct dc_crtc_timing *timing,
			  struct crtc_stereo_flags *flags);

bool optc1_is_stereo_left_eye(struct timing_generator *optc);

void optc1_clear_optc_underflow(struct timing_generator *optc);

void optc1_tg_init(struct timing_generator *optc);

bool optc1_is_tg_enabled(struct timing_generator *optc);

bool optc1_is_optc_underflow_occurred(struct timing_generator *optc);

void optc1_set_blank_data_double_buffer(struct timing_generator *optc, bool enable);

void optc1_set_timing_double_buffer(struct timing_generator *optc, bool enable);

bool optc1_get_otg_active_size(struct timing_generator *optc,
			       uint32_t *otg_active_width,
			       uint32_t *otg_active_height);

void optc1_enable_crtc_reset(struct timing_generator *optc,
			     int source_tg_inst,
			     struct crtc_trigger_info *crtc_tp);

bool optc1_configure_crc(struct timing_generator *optc, const struct crc_params *params);

bool optc1_get_crc(struct timing_generator *optc, uint8_t idx,
		   uint32_t *r_cr,
		   uint32_t *g_y,
		   uint32_t *b_cb);

void optc1_set_vtg_params(struct timing_generator *optc,
			  const struct dc_crtc_timing *dc_crtc_timing,
			  bool program_fp2);

bool optc1_is_two_pixels_per_container(const struct dc_crtc_timing *timing);

#endif
