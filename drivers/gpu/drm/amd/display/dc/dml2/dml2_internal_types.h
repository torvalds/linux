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
 * Authors: AMD
 *
 */
 
#ifndef __DML2_INTERNAL_TYPES_H__
#define __DML2_INTERNAL_TYPES_H__

#include "dml2_dc_types.h"
#include "display_mode_core.h"
#include "dml2_wrapper.h"
#include "dml2_policy.h"


struct dml2_wrapper_optimize_configuration_params {
	struct display_mode_lib_st *dml_core_ctx;
	struct dml2_configuration_options *config;
	struct ip_params_st *ip_params;
	struct dml_display_cfg_st *cur_display_config;
	struct dml_display_cfg_st *new_display_config;
	const struct dml_mode_support_info_st *cur_mode_support_info;
	struct dml_mode_eval_policy_st *cur_policy;
	struct dml_mode_eval_policy_st *new_policy;
};

struct dml2_calculate_lowest_supported_state_for_temp_read_scratch {
	struct dml_mode_support_info_st evaluation_info;
	dml_float_t uclk_change_latencies[__DML_MAX_STATE_ARRAY_SIZE__];
	struct dml_display_cfg_st cur_display_config;
	struct dml_display_cfg_st new_display_config;
	struct dml_mode_eval_policy_st new_policy;
	struct dml_mode_eval_policy_st cur_policy;
};

struct dml2_create_scratch {
	struct dml2_policy_build_synthetic_soc_states_scratch build_synthetic_socbb_scratch;
	struct soc_states_st in_states;
};

struct dml2_calculate_rq_and_dlg_params_scratch {
	struct _vcs_dpi_dml_display_rq_regs_st rq_regs;
	struct _vcs_dpi_dml_display_dlg_regs_st disp_dlg_regs;
	struct _vcs_dpi_dml_display_ttu_regs_st disp_ttu_regs;
};

#define __DML2_WRAPPER_MAX_STREAMS_PLANES__ 6

struct dml2_dml_to_dc_pipe_mapping {
	unsigned int disp_cfg_to_stream_id[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
	bool disp_cfg_to_stream_id_valid[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
	unsigned int disp_cfg_to_plane_id[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
	bool disp_cfg_to_plane_id_valid[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
	unsigned int dml_pipe_idx_to_stream_id[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
	bool dml_pipe_idx_to_stream_id_valid[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
	unsigned int dml_pipe_idx_to_plane_id[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
	bool dml_pipe_idx_to_plane_id_valid[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
	unsigned int dml_pipe_idx_to_plane_index[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
	bool dml_pipe_idx_to_plane_index_valid[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
};

struct dml2_wrapper_scratch {
	struct dml_display_cfg_st cur_display_config;
	struct dml_display_cfg_st new_display_config;
	struct dml_mode_eval_policy_st new_policy;
	struct dml_mode_eval_policy_st cur_policy;
	struct dml_mode_support_info_st mode_support_info;
	struct dml_mode_support_ex_params_st mode_support_params;

	struct dummy_pstate_entry dummy_pstate_table[4];

	struct dml2_create_scratch create_scratch;
	struct dml2_calculate_lowest_supported_state_for_temp_read_scratch dml2_calculate_lowest_supported_state_for_temp_read_scratch;
	struct dml2_calculate_rq_and_dlg_params_scratch calculate_rq_and_dlg_params_scratch;

	struct dml2_wrapper_optimize_configuration_params optimize_configuration_params;
	struct dml2_policy_build_synthetic_soc_states_params build_synthetic_socbb_params;

	struct dml2_dml_to_dc_pipe_mapping dml_to_dc_pipe_mapping;
	bool enable_flexible_pipe_mapping;
	bool plane_duplicate_exists;
};

struct dml2_helper_det_policy_scratch {
	int dpps_per_surface[MAX_PLANES];
};

enum dml2_architecture {
	dml2_architecture_20,
};

struct dml2_pipe_combine_factor {
	unsigned int source;
	unsigned int target;
};

struct dml2_pipe_combine_scratch {
	struct dml2_pipe_combine_factor odm_factors[MAX_PIPES];
	struct dml2_pipe_combine_factor mpc_factors[MAX_PIPES][MAX_PIPES];
};

struct dml2_context {
	enum dml2_architecture architecture;
	struct dml2_configuration_options config;
	struct dml2_helper_det_policy_scratch det_helper_scratch;
	struct dml2_pipe_combine_scratch pipe_combine_scratch;
	union {
		struct {
			struct display_mode_lib_st dml_core_ctx;
			struct dml2_wrapper_scratch scratch;
			struct dcn_watermarks g6_temp_read_watermark_set;
		} v20;
	};
};

#endif
