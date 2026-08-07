// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DCN60_CLK_MGR_SMU_MSG_H_
#define __DCN60_CLK_MGR_SMU_MSG_H_

#include "os_types.h"
#include "core_types.h"
#include "dalsmc.h"

struct clk_mgr_internal;

unsigned int dcn60_smu_set_hard_min_by_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint16_t freq_mhz);
void dcn60_smu_set_stutter_efficiency(struct clk_mgr_internal *clk_mgr,
		uint8_t base_efficiency, uint8_t low_power_efficiency);
void dcn60_smu_set_min_deep_sleep_dcfclk(struct clk_mgr_internal *clk_mgr, uint32_t freq_mhz);
void dcn60_smu_set_pme_workaround(struct clk_mgr_internal *clk_mgr);
void dcn60_smu_indicate_pstate_status(struct clk_mgr_internal *clk_mgr,
		bool allow_fclk, bool allow_uclk,
		bool wait_resp, bool drr_enable, bool alt_ch_enable);
bool dcn60_smu_update_utm_qos_request(struct clk_mgr_internal *clk_mgr,
		uint32_t latency_sop_index,
		uint32_t nominal_bandwidth_KBps,
		uint32_t urgent_bandwidth_KBps,
		uint32_t lsdma_bandwidth_KBps);
bool dcn60_smu_set_soc_utm_table(struct clk_mgr_internal *clk_mgr,
		long long dram_addr);
bool dcn60_smu_get_dal_init_table(struct clk_mgr_internal *clk_mgr,
		const DalInitTable_t **init_table);
bool dcn60_smu_get_msg_header_version(struct clk_mgr_internal *clk_mgr,
		uint32_t *version);
void dcn60_smu_set_display_idle_optimization(struct clk_mgr_internal *clk_mgr, bool is_idle);

#endif /* __DCN60_CLK_MGR_SMU_MSG_H_ */
