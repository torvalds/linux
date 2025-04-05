// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DCN401_CLK_MGR_SMU_MSG_H_
#define __DCN401_CLK_MGR_SMU_MSG_H_

#include "os_types.h"
#include "core_types.h"
#include "dcn32/dcn32_clk_mgr_smu_msg.h"

void dcn401_smu_send_fclk_pstate_message(struct clk_mgr_internal *clk_mgr, bool support);
void dcn401_smu_send_uclk_pstate_message(struct clk_mgr_internal *clk_mgr, bool support);
void dcn401_smu_send_cab_for_uclk_message(struct clk_mgr_internal *clk_mgr, unsigned int num_ways);
void dcn401_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr);
void dcn401_smu_set_pme_workaround(struct clk_mgr_internal *clk_mgr);
unsigned int dcn401_smu_set_hard_min_by_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint16_t freq_mhz);
void dcn401_smu_wait_for_dmub_ack_mclk(struct clk_mgr_internal *clk_mgr, bool enable);
void dcn401_smu_indicate_drr_status(struct clk_mgr_internal *clk_mgr, bool mod_drr_for_pstate);
bool dcn401_smu_set_idle_uclk_fclk_hardmin(struct clk_mgr_internal *clk_mgr,
		uint16_t uclk_freq_mhz,
		uint16_t fclk_freq_mhz);
bool dcn401_smu_set_active_uclk_fclk_hardmin(struct clk_mgr_internal *clk_mgr,
		uint16_t uclk_freq_mhz,
		uint16_t fclk_freq_mhz);
bool dcn401_smu_set_subvp_uclk_fclk_hardmin(struct clk_mgr_internal *clk_mgr,
		uint16_t uclk_freq_mhz,
		uint16_t fclk_freq_mhz);
void dcn401_smu_set_min_deep_sleep_dcef_clk(struct clk_mgr_internal *clk_mgr, uint32_t freq_mhz);
void dcn401_smu_set_num_of_displays(struct clk_mgr_internal *clk_mgr, uint32_t num_displays);
unsigned int dcn401_smu_get_num_of_umc_channels(struct clk_mgr_internal *clk_mgr);

#endif /* __DCN401_CLK_MGR_SMU_MSG_H_ */
