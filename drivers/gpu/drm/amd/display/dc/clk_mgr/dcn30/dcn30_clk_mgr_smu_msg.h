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

struct clk_mgr_internal;

bool         dcn30_smu_test_message(struct clk_mgr_internal *clk_mgr, uint32_t input);
bool         dcn30_smu_get_smu_version(struct clk_mgr_internal *clk_mgr, unsigned int *version);
bool         dcn30_smu_check_driver_if_version(struct clk_mgr_internal *clk_mgr);
bool         dcn30_smu_check_msg_header_version(struct clk_mgr_internal *clk_mgr);
void         dcn30_smu_set_dram_addr_high(struct clk_mgr_internal *clk_mgr, uint32_t addr_high);
void         dcn30_smu_set_dram_addr_low(struct clk_mgr_internal *clk_mgr, uint32_t addr_low);
void         dcn30_smu_transfer_wm_table_smu_2_dram(struct clk_mgr_internal *clk_mgr);
void         dcn30_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr);
unsigned int dcn30_smu_set_hard_min_by_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint16_t freq_mhz);
unsigned int dcn30_smu_set_hard_max_by_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint16_t freq_mhz);
unsigned int dcn30_smu_get_dpm_freq_by_index(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint8_t dpm_level);
unsigned int dcn30_smu_get_dc_mode_max_dpm_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk);
void         dcn30_smu_set_min_deep_sleep_dcef_clk(struct clk_mgr_internal *clk_mgr, uint32_t freq_mhz);
void         dcn30_smu_set_num_of_displays(struct clk_mgr_internal *clk_mgr, uint32_t num_displays);
void         dcn30_smu_set_display_refresh_from_mall(struct clk_mgr_internal *clk_mgr, bool enable, uint8_t cache_timer_delay, uint8_t cache_timer_scale);
void         dcn30_smu_set_external_client_df_cstate_allow(struct clk_mgr_internal *clk_mgr, bool enable);
void         dcn30_smu_set_pme_workaround(struct clk_mgr_internal *clk_mgr);

#endif /* DAL_DC_DCN30_CLK_MGR_SMU_MSG_H_ */
