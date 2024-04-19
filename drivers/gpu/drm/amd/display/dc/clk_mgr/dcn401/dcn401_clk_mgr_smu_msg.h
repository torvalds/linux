// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DCN401_CLK_MGR_SMU_MSG_H_
#define __DCN401_CLK_MGR_SMU_MSG_H_

#include "os_types.h"
#include "core_types.h"
#include "dcn32/dcn32_clk_mgr_smu_msg.h"

#define FCLK_PSTATE_NOTSUPPORTED       0x00
#define FCLK_PSTATE_SUPPORTED          0x01

void dcn401_smu_send_fclk_pstate_message(struct clk_mgr_internal *clk_mgr, bool enable);
void dcn401_smu_send_cab_for_uclk_message(struct clk_mgr_internal *clk_mgr, unsigned int num_ways);
void dcn401_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr);
void dcn401_smu_set_pme_workaround(struct clk_mgr_internal *clk_mgr);
unsigned int dcn401_smu_set_hard_min_by_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint16_t freq_mhz);

#endif /* __DCN401_CLK_MGR_SMU_MSG_H_ */
