// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DCN401_FPU_H__
#define __DCN401_FPU_H__

#include "clk_mgr.h"

void dcn401_build_wm_range_table_fpu(struct clk_mgr *clk_mgr);

void dcn401_update_bw_bounding_box_fpu(struct dc *dc, struct clk_bw_params *bw_params);

#endif
