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

#ifndef __DAL_DISPLAY_CLOCK_H__
#define __DAL_DISPLAY_CLOCK_H__

#include "include/display_clock_interface.h"

struct display_clock_funcs {
	void (*destroy)(struct display_clock **to_destroy);
	void (*set_clock)(struct display_clock *disp_clk,
		uint32_t requested_clock_khz);
	enum clocks_state (*get_min_clocks_state)(
		struct display_clock *disp_clk);
	enum clocks_state (*get_required_clocks_state)(
		struct display_clock *disp_clk,
		struct state_dependent_clocks *req_clocks);
	bool (*set_min_clocks_state)(struct display_clock *disp_clk,
		enum clocks_state clocks_state);
	uint32_t (*get_dp_ref_clk_frequency)(struct display_clock *disp_clk);
	void (*store_max_clocks_state)(struct display_clock *disp_clk,
		enum clocks_state max_clocks_state);

};

struct display_clock {
	struct dc_context *ctx;
	const struct display_clock_funcs *funcs;
	uint32_t min_display_clk_threshold_khz;
	enum clock_source_id id;

	enum clocks_state cur_min_clks_state;
};
void dal_display_clock_store_max_clocks_state(
	struct display_clock *disp_clk,
	enum clocks_state max_clocks_state);


#endif /* __DAL_DISPLAY_CLOCK_H__*/
