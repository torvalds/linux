/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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

#ifndef __DISPLAY_CLOCK_H__
#define __DISPLAY_CLOCK_H__

#include "dm_services_types.h"


struct clocks_value {
	int dispclk_in_khz;
	int max_pixelclk_in_khz;
	int max_non_dp_phyclk_in_khz;
	int max_dp_phyclk_in_khz;
	bool dispclk_notify_pplib_done;
	bool pixelclk_notify_pplib_done;
	bool phyclk_notigy_pplib_done;
	int dcfclock_in_khz;
	int dppclk_in_khz;
	int mclk_in_khz;
	int phyclk_in_khz;
	int common_vdd_level;
};


/* Structure containing all state-dependent clocks
 * (dependent on "enum clocks_state") */
struct state_dependent_clocks {
	int display_clk_khz;
	int pixel_clk_khz;
};

struct display_clock {
	struct dc_context *ctx;
	const struct display_clock_funcs *funcs;

	enum dm_pp_clocks_state max_clks_state;
	enum dm_pp_clocks_state cur_min_clks_state;
	struct clocks_value cur_clocks_value;
};

struct display_clock_funcs {
	int (*set_clock)(struct display_clock *disp_clk,
		int requested_clock_khz);

	enum dm_pp_clocks_state (*get_required_clocks_state)(
		struct display_clock *disp_clk,
		struct state_dependent_clocks *req_clocks);

	bool (*set_min_clocks_state)(struct display_clock *disp_clk,
		enum dm_pp_clocks_state dm_pp_clocks_state);

	int (*get_dp_ref_clk_frequency)(struct display_clock *disp_clk);

	bool (*apply_clock_voltage_request)(
		struct display_clock *disp_clk,
		enum dm_pp_clock_type clocks_type,
		int clocks_in_khz,
		bool pre_mode_set,
		bool update_dp_phyclk);
};

#endif /* __DISPLAY_CLOCK_H__ */
