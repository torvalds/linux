/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 */
#ifndef _DM_PP_INTERFACE_
#define _DM_PP_INTERFACE_

#define PP_MAX_CLOCK_LEVELS 8

struct pp_clock_with_latency {
	uint32_t clocks_in_khz;
	uint32_t latency_in_us;
};

struct pp_clock_levels_with_latency {
	uint32_t num_levels;
	struct pp_clock_with_latency data[PP_MAX_CLOCK_LEVELS];
};

struct pp_clock_with_voltage {
	uint32_t clocks_in_khz;
	uint32_t voltage_in_mv;
};

struct pp_clock_levels_with_voltage {
	uint32_t num_levels;
	struct pp_clock_with_voltage data[PP_MAX_CLOCK_LEVELS];
};

#define PP_MAX_WM_SETS 4

enum pp_wm_set_id {
	DC_WM_SET_A = 0,
	DC_WM_SET_B,
	DC_WM_SET_C,
	DC_WM_SET_D,
	DC_WM_SET_INVALID = 0xffff,
};

struct pp_wm_set_with_dmif_clock_range_soc15 {
	enum pp_wm_set_id wm_set_id;
	uint32_t wm_min_dcefclk_in_khz;
	uint32_t wm_max_dcefclk_in_khz;
	uint32_t wm_min_memclk_in_khz;
	uint32_t wm_max_memclk_in_khz;
};

struct pp_wm_set_with_mcif_clock_range_soc15 {
	enum pp_wm_set_id wm_set_id;
	uint32_t wm_min_socclk_in_khz;
	uint32_t wm_max_socclk_in_khz;
	uint32_t wm_min_memclk_in_khz;
	uint32_t wm_max_memclk_in_khz;
};

struct pp_wm_sets_with_clock_ranges_soc15 {
	uint32_t num_wm_sets_dmif;
	uint32_t num_wm_sets_mcif;
	struct pp_wm_set_with_dmif_clock_range_soc15
		wm_sets_dmif[PP_MAX_WM_SETS];
	struct pp_wm_set_with_mcif_clock_range_soc15
		wm_sets_mcif[PP_MAX_WM_SETS];
};

#endif /* _DM_PP_INTERFACE_ */
