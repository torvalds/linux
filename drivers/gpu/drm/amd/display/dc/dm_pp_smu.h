/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#ifndef DM_PP_SMU_IF__H
#define DM_PP_SMU_IF__H

/*
 * interface to PPLIB/SMU to setup clocks and pstate requirements on SoC
 */


struct pp_smu {
	struct dc_context *ctx;
};

enum wm_set_id {
	WM_A,
	WM_B,
	WM_C,
	WM_D,
	WM_SET_COUNT,
};

struct pp_smu_wm_set_range {
	enum wm_set_id wm_inst;
	uint32_t min_fill_clk_khz;
	uint32_t max_fill_clk_khz;
	uint32_t min_drain_clk_khz;
	uint32_t max_drain_clk_khz;
};

struct pp_smu_wm_range_sets {
	uint32_t num_reader_wm_sets;
	struct pp_smu_wm_set_range reader_wm_sets[WM_SET_COUNT];

	uint32_t num_writer_wm_sets;
	struct pp_smu_wm_set_range writer_wm_sets[WM_SET_COUNT];
};

struct pp_smu_display_requirement_rv {
	/* PPSMC_MSG_SetDisplayCount: count
	 *  0 triggers S0i2 optimization
	 */
	unsigned int display_count;

	/* PPSMC_MSG_SetHardMinFclkByFreq: khz
	 *  FCLK will vary with DPM, but never below requested hard min
	 */
	unsigned int hard_min_fclk_khz;

	/* PPSMC_MSG_SetHardMinDcefclkByFreq: khz
	 *  fixed clock at requested freq, either from FCH bypass or DFS
	 */
	unsigned int hard_min_dcefclk_khz;

	/* PPSMC_MSG_SetMinDeepSleepDcefclk: mhz
	 *  when DF is in cstate, dcf clock is further divided down
	 *  to just above given frequency
	 */
	unsigned int min_deep_sleep_dcefclk_mhz;
};

struct pp_smu_funcs_rv {
	struct pp_smu pp_smu;

	void (*set_display_requirement)(struct pp_smu *pp,
			struct pp_smu_display_requirement_rv *req);

	/* which SMU message?  are reader and writer WM separate SMU msg? */
	void (*set_wm_ranges)(struct pp_smu *pp,
			struct pp_smu_wm_range_sets *ranges);
	/* PME w/a */
	void (*set_pme_wa_enable)(struct pp_smu *pp);
};

#if 0
struct pp_smu_funcs_rv {

	/* PPSMC_MSG_SetDisplayCount
	 *  0 triggers S0i2 optimization
	 */
	void (*set_display_count)(struct pp_smu *pp, int count);

	/* PPSMC_MSG_SetHardMinFclkByFreq
	 *  FCLK will vary with DPM, but never below requested hard min
	 */
	void (*set_hard_min_fclk_by_freq)(struct pp_smu *pp, int khz);

	/* PPSMC_MSG_SetHardMinDcefclkByFreq
	 *  fixed clock at requested freq, either from FCH bypass or DFS
	 */
	void (*set_hard_min_dcefclk_by_freq)(struct pp_smu *pp, int khz);

	/* PPSMC_MSG_SetMinDeepSleepDcefclk
	 *  when DF is in cstate, dcf clock is further divided down
	 *  to just above given frequency
	 */
	void (*set_min_deep_sleep_dcefclk)(struct pp_smu *pp, int mhz);

	/* todo: aesthetic
	 * watermark range table
	 */

	/* todo: functional/feature
	 * PPSMC_MSG_SetHardMinSocclkByFreq: required to support DWB
	 */
};
#endif

#endif /* DM_PP_SMU_IF__H */
