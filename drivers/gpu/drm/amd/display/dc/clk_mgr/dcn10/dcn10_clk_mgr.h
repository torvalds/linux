/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#ifndef __DCN10_CLK_MGR_H__
#define __DCN10_CLK_MGR_H__

#include "dc.h"

struct clk_mgr_internal;

/*
 * Shared clock helpers referenced by both the DCE and DCN clock managers.
 */
int dcn10_adjust_dp_ref_freq_for_ss(struct clk_mgr_internal *clk_mgr_dce, int dp_ref_clk_khz);

void dcn10_clock_read_ss_info(struct clk_mgr_internal *clk_mgr_dce);

int dcn10_get_dp_ref_freq_khz(struct clk_mgr *clk_mgr_base);

unsigned int dcn10_dentist_get_divider_from_did(unsigned int did);

int dcn10_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_clk_khz);

int dcn10_set_dprefclk(struct clk_mgr_internal *clk_mgr);

#endif /* __DCN10_CLK_MGR_H__ */
