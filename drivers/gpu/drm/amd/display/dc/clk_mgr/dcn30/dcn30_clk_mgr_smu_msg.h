/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef DAL_DC_DCN30_CLK_MGR_SMU_MSG_H_
#define DAL_DC_DCN30_CLK_MGR_SMU_MSG_H_

#include "core_types.h"

#define SMU11_DRIVER_IF_VERSION 0x1F

typedef enum {
	PPCLK_GFXCLK = 0,
	PPCLK_SOCCLK,
	PPCLK_UCLK,
	PPCLK_FCLK,
	PPCLK_DCLK_0,
	PPCLK_VCLK_0,
	PPCLK_DCLK_1,
	PPCLK_VCLK_1,
	PPCLK_DCEFCLK,
	PPCLK_DISPCLK,
	PPCLK_PIXCLK,
	PPCLK_PHYCLK,
	PPCLK_DTBCLK,
	PPCLK_COUNT,
} PPCLK_e;

typedef struct {
	uint16_t MinClock; // This is either DCEFCLK or SOCCLK (in MHz)
	uint16_t MaxClock; // This is either DCEFCLK or SOCCLK (in MHz)
	uint16_t MinUclk;
	uint16_t MaxUclk;

	uint8_t  WmSetting;
	uint8_t  Flags;
	uint8_t  Padding[2];

} WatermarkRowGeneric_t;

#define NUM_WM_RANGES 4

typedef enum {
	WM_SOCCLK = 0,
	WM_DCEFCLK,
	WM_COUNT,
} WM_CLOCK_e;

typedef enum {
	WATERMARKS_CLOCK_RANGE = 0,
	WATERMARKS_DUMMY_PSTATE,
	WATERMARKS_COUNT,
} WATERMARKS_FLAGS_e;

typedef struct {
	// Watermarks
	WatermarkRowGeneric_t WatermarkRow[WM_COUNT][NUM_WM_RANGES];
} Watermarks_t;

typedef struct {
	Watermarks_t Watermarks;

	uint32_t     MmHubPadding[8]; // SMU internal use
} WatermarksExternal_t;

#define TABLE_WATERMARKS 1

struct clk_mgr_internal;

bool         dcn30_smu_test_message(struct clk_mgr_internal *clk_mgr, uint32_t input);
bool         dcn30_smu_get_smu_version(struct clk_mgr_internal *clk_mgr, unsigned int *version);
bool         dcn30_smu_check_driver_if_version(struct clk_mgr_internal *clk_mgr);
bool         dcn30_smu_check_msg_header_version(struct clk_mgr_internal *clk_mgr);
void         dcn30_smu_set_dram_addr_high(struct clk_mgr_internal *clk_mgr, uint32_t addr_high);
void         dcn30_smu_set_dram_addr_low(struct clk_mgr_internal *clk_mgr, uint32_t addr_low);
void         dcn30_smu_transfer_wm_table_smu_2_dram(struct clk_mgr_internal *clk_mgr);
void         dcn30_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr);
unsigned int dcn30_smu_set_hard_min_by_freq(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, uint16_t freq_mhz);
unsigned int dcn30_smu_set_hard_max_by_freq(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, uint16_t freq_mhz);
unsigned int dcn30_smu_get_dpm_freq_by_index(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, uint8_t dpm_level);
unsigned int dcn30_smu_get_dc_mode_max_dpm_freq(struct clk_mgr_internal *clk_mgr, PPCLK_e clk);
void         dcn30_smu_set_min_deep_sleep_dcef_clk(struct clk_mgr_internal *clk_mgr, uint32_t freq_mhz);
void         dcn30_smu_set_num_of_displays(struct clk_mgr_internal *clk_mgr, uint32_t num_displays);
void         dcn30_smu_set_external_client_df_cstate_allow(struct clk_mgr_internal *clk_mgr, bool enable);
void         dcn30_smu_set_pme_workaround(struct clk_mgr_internal *clk_mgr);

#endif /* DAL_DC_DCN30_CLK_MGR_SMU_MSG_H_ */
