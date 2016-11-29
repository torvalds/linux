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

#ifndef __DISPLAY_CLOCK_INTERFACE_H__
#define __DISPLAY_CLOCK_INTERFACE_H__

#include "hw_sequencer_types.h"
#include "grph_object_defs.h"
#include "signal_types.h"

/* Enumeration of all clocks states */
enum clocks_state {
	CLOCKS_STATE_INVALID = 0,
	CLOCKS_STATE_ULTRA_LOW,
	CLOCKS_STATE_LOW,
	CLOCKS_STATE_NOMINAL,
	CLOCKS_STATE_PERFORMANCE,
	/* Starting from DCE11, Max 8 level DPM state supported */
	CLOCKS_DPM_STATE_LEVEL_INVALID = CLOCKS_STATE_INVALID,
	CLOCKS_DPM_STATE_LEVEL_0 = CLOCKS_STATE_ULTRA_LOW,
	CLOCKS_DPM_STATE_LEVEL_1 = CLOCKS_STATE_LOW,
	CLOCKS_DPM_STATE_LEVEL_2 = CLOCKS_STATE_NOMINAL,
	CLOCKS_DPM_STATE_LEVEL_3 = CLOCKS_STATE_PERFORMANCE,
	CLOCKS_DPM_STATE_LEVEL_4 = CLOCKS_DPM_STATE_LEVEL_3 + 1,
	CLOCKS_DPM_STATE_LEVEL_5 = CLOCKS_DPM_STATE_LEVEL_4 + 1,
	CLOCKS_DPM_STATE_LEVEL_6 = CLOCKS_DPM_STATE_LEVEL_5 + 1,
	CLOCKS_DPM_STATE_LEVEL_7 = CLOCKS_DPM_STATE_LEVEL_6 + 1,
};

/* Structure containing all state-dependent clocks
 * (dependent on "enum clocks_state") */
struct state_dependent_clocks {
	uint32_t display_clk_khz;
	uint32_t pixel_clk_khz;
};

struct display_clock;

struct display_clock *dal_display_clock_dce112_create(
	struct dc_context *ctx);

struct display_clock *dal_display_clock_dce110_create(
	struct dc_context *ctx);

struct display_clock *dal_display_clock_dce80_create(
	struct dc_context *ctx);

void dal_display_clock_destroy(struct display_clock **to_destroy);
void dal_display_clock_set_clock(
	struct display_clock *disp_clk,
	uint32_t requested_clock_khz);
bool dal_display_clock_get_min_clocks_state(
	struct display_clock *disp_clk,
	enum clocks_state *clocks_state);
bool dal_display_clock_get_required_clocks_state(
	struct display_clock *disp_clk,
	struct state_dependent_clocks *req_clocks,
	enum clocks_state *clocks_state);
bool dal_display_clock_set_min_clocks_state(
	struct display_clock *disp_clk,
	enum clocks_state clocks_state);
uint32_t dal_display_clock_get_dp_ref_clk_frequency(
	struct display_clock *disp_clk);
void dal_display_clock_store_max_clocks_state(
	struct display_clock *disp_clk,
	enum clocks_state max_clocks_state);

#endif /* __DISPLAY_CLOCK_INTERFACE_H__ */
