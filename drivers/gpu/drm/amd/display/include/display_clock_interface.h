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

#include "dm_services_types.h"
#include "hw_sequencer_types.h"
#include "grph_object_defs.h"
#include "signal_types.h"

/* Structure containing all state-dependent clocks
 * (dependent on "enum clocks_state") */
struct state_dependent_clocks {
	uint32_t display_clk_khz;
	uint32_t pixel_clk_khz;
};

struct display_clock {
	struct dc_context *ctx;
	const struct display_clock_funcs *funcs;
	uint32_t min_display_clk_threshold_khz;
	/* Max display block clocks state*/
	enum dm_pp_clocks_state max_clks_state;

	enum dm_pp_clocks_state cur_min_clks_state;
};

struct display_clock_funcs {
	void (*destroy)(struct display_clock **to_destroy);
	void (*set_clock)(struct display_clock *disp_clk,
		uint32_t requested_clock_khz);
	enum dm_pp_clocks_state (*get_required_clocks_state)(
		struct display_clock *disp_clk,
		struct state_dependent_clocks *req_clocks);
	bool (*set_min_clocks_state)(struct display_clock *disp_clk,
		enum dm_pp_clocks_state dm_pp_clocks_state);
	uint32_t (*get_dp_ref_clk_frequency)(struct display_clock *disp_clk);

};

struct display_clock *dal_display_clock_dce112_create(
	struct dc_context *ctx);

struct display_clock *dal_display_clock_dce110_create(
	struct dc_context *ctx);

struct display_clock *dal_display_clock_dce80_create(
	struct dc_context *ctx);

void dal_display_clock_destroy(struct display_clock **to_destroy);

#endif /* __DISPLAY_CLOCK_INTERFACE_H__ */
