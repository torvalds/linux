/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __DML2_MALL_PHANTOM_H__
#define __DML2_MALL_PHANTOM_H__

#include "dml2_dc_types.h"
#include "display_mode_core_structs.h"

struct dml2_svp_helper_select_best_svp_candidate_params {
	const struct dml_display_cfg_st *dml_config;
	const struct dml_mode_support_info_st *mode_support_info;
	const unsigned int blacklist;
	unsigned int *candidate_index;
};

struct dml2_context;

unsigned int dml2_helper_calculate_num_ways_for_subvp(struct dml2_context *ctx, struct dc_state *context);

bool dml2_svp_add_phantom_pipe_to_dc_state(struct dml2_context *ctx, struct dc_state *state, struct dml_mode_support_info_st *mode_support_info);

bool dml2_svp_remove_all_phantom_pipes(struct dml2_context *ctx, struct dc_state *state);

bool dml2_svp_validate_static_schedulability(struct dml2_context *ctx, struct dc_state *context, enum dml_dram_clock_change_support pstate_change_type);

bool dml2_svp_drr_schedulable(struct dml2_context *ctx, struct dc_state *context, struct dc_crtc_timing *drr_timing);

#endif
