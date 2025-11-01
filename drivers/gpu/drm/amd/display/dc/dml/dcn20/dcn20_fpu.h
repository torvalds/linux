/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "core_types.h"

#ifndef __DCN20_FPU_H__
#define __DCN20_FPU_H__

void dcn20_populate_dml_writeback_from_context(struct dc *dc,
					       struct resource_context *res_ctx,
					       display_e2e_pipe_params_st *pipes);

void dcn20_fpu_set_wb_arb_params(struct mcif_arb_params *wb_arb_params,
				 struct dc_state *context,
				 display_e2e_pipe_params_st *pipes,
				 int pipe_cnt, int i);
void dcn20_calculate_dlg_params(struct dc *dc,
				struct dc_state *context,
				display_e2e_pipe_params_st *pipes,
				int pipe_cnt,
				int vlevel);
int dcn20_populate_dml_pipes_from_context(struct dc *dc,
					  struct dc_state *context,
					  display_e2e_pipe_params_st *pipes,
					  enum dc_validate_mode validate_mode);
void dcn20_calculate_wm(struct dc *dc,
			struct dc_state *context,
			display_e2e_pipe_params_st *pipes,
			int *out_pipe_cnt,
			int *pipe_split_from,
			int vlevel,
			enum dc_validate_mode validate_mode);
void dcn20_cap_soc_clocks(struct _vcs_dpi_soc_bounding_box_st *bb,
			  struct pp_smu_nv_clock_table max_clocks);
void dcn20_update_bounding_box(struct dc *dc,
			       struct _vcs_dpi_soc_bounding_box_st *bb,
			       struct pp_smu_nv_clock_table *max_clocks,
			       unsigned int *uclk_states,
			       unsigned int num_states);
void dcn20_patch_bounding_box(struct dc *dc,
			      struct _vcs_dpi_soc_bounding_box_st *bb);
bool dcn20_validate_bandwidth_fp(struct dc *dc, struct dc_state *context,
				 enum dc_validate_mode validate_mode, display_e2e_pipe_params_st *pipes);
void dcn20_fpu_set_wm_ranges(int i,
			     struct pp_smu_wm_range_sets *ranges,
			     struct _vcs_dpi_soc_bounding_box_st *loaded_bb);
void dcn20_fpu_adjust_dppclk(struct vba_vars_st *v,
			     int vlevel,
			     int max_mpc_comb,
			     int pipe_idx,
			     bool is_validating_bw);

int dcn21_populate_dml_pipes_from_context(struct dc *dc,
					  struct dc_state *context,
					  display_e2e_pipe_params_st *pipes,
					  enum dc_validate_mode validate_mode);
bool dcn21_validate_bandwidth_fp(struct dc *dc, struct dc_state *context, enum
				 dc_validate_mode, display_e2e_pipe_params_st *pipes);
void dcn21_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params);

void dcn21_clk_mgr_set_bw_params_wm_table(struct clk_bw_params *bw_params);

void dcn201_populate_dml_writeback_from_context_fpu(struct dc *dc,
						struct resource_context *res_ctx,
						display_e2e_pipe_params_st *pipes);

#endif /* __DCN20_FPU_H__ */
