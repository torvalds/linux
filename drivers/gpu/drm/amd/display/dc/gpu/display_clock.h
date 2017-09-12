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
	bool (*validate)(struct display_clock *disp_clk,
		struct min_clock_params *params);
	uint32_t (*calculate_min_clock)(struct display_clock *disp_clk,
		uint32_t path_num, struct min_clock_params *params);
	uint32_t (*get_validation_clock)(struct display_clock *disp_clk);
	void (*set_clock)(struct display_clock *disp_clk,
		uint32_t requested_clock_khz);
	uint32_t (*get_clock)(struct display_clock *disp_clk);
	enum clocks_state (*get_min_clocks_state)(
		struct display_clock *disp_clk);
	enum clocks_state (*get_required_clocks_state)(
		struct display_clock *disp_clk,
		struct state_dependent_clocks *req_clocks);
	bool (*set_min_clocks_state)(struct display_clock *disp_clk,
		enum clocks_state clocks_state);
	uint32_t (*get_dp_ref_clk_frequency)(struct display_clock *disp_clk);
	void (*set_dp_ref_clock_source)(struct display_clock *disp_clk,
		enum clock_source_id clk_src);
	void (*store_max_clocks_state)(struct display_clock *disp_clk,
		enum clocks_state max_clocks_state);
	void (*set_clock_state)(struct display_clock *disp_clk,
		struct display_clock_state clk_state);
	struct display_clock_state (*get_clock_state)(
		struct display_clock *disp_clk);
	uint32_t (*get_dfs_bypass_threshold)(struct display_clock *disp_clk);

};

struct display_clock {
	struct dc_context *ctx;
	const struct display_clock_funcs *funcs;
	uint32_t min_display_clk_threshold_khz;
	enum clock_source_id id;

	enum clocks_state cur_min_clks_state;
};
void dal_display_clock_base_set_dp_ref_clock_source(
	struct display_clock *disp_clk,
	enum clock_source_id clk_src);
struct display_clock_state dal_display_clock_base_get_clock_state(
	struct display_clock *disp_clk);
uint32_t dal_display_clock_base_get_dfs_bypass_threshold(
	struct display_clock *disp_clk);
void dal_display_clock_base_set_clock_state(struct display_clock *disp_clk,
	struct display_clock_state clk_state);
bool dal_display_clock_construct_base(
	struct display_clock *base,
	struct dc_context *ctx);

uint32_t dal_display_clock_get_validation_clock(struct display_clock *disp_clk);

void dal_display_clock_store_max_clocks_state(
	struct display_clock *disp_clk,
	enum clocks_state max_clocks_state);


#endif /* __DAL_DISPLAY_CLOCK_H__*/
