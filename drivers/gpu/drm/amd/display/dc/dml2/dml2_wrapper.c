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

#include "display_mode_core.h"
#include "dml2_internal_types.h"
#include "dml2_utils.h"
#include "dml2_policy.h"
#include "dml2_translation_helper.h"
#include "dml2_mall_phantom.h"
#include "dml2_dc_resource_mgmt.h"
#include "dml21_wrapper.h"

static void initialize_dml2_ip_params(struct dml2_context *dml2, const struct dc *in_dc, struct ip_params_st *out)
{
	if (dml2->config.use_native_soc_bb_construction)
		dml2_init_ip_params(dml2, in_dc, out);
	else
		dml2_translate_ip_params(in_dc, out);
}

static void initialize_dml2_soc_bbox(struct dml2_context *dml2, const struct dc *in_dc, struct soc_bounding_box_st *out)
{
	if (dml2->config.use_native_soc_bb_construction)
		dml2_init_socbb_params(dml2, in_dc, out);
	else
		dml2_translate_socbb_params(in_dc, out);
}

static void initialize_dml2_soc_states(struct dml2_context *dml2,
	const struct dc *in_dc, const struct soc_bounding_box_st *in_bbox, struct soc_states_st *out)
{
	if (dml2->config.use_native_soc_bb_construction)
		dml2_init_soc_states(dml2, in_dc, in_bbox, out);
	else
		dml2_translate_soc_states(in_dc, out, in_dc->dml.soc.num_states);
}

static void map_hw_resources(struct dml2_context *dml2,
		struct dml_display_cfg_st *in_out_display_cfg, struct dml_mode_support_info_st *mode_support_info)
{
	unsigned int num_pipes = 0;
	int i, j;

	for (i = 0; i < __DML_NUM_PLANES__; i++) {
		in_out_display_cfg->hw.ODMMode[i] = mode_support_info->ODMMode[i];
		in_out_display_cfg->hw.DPPPerSurface[i] = mode_support_info->DPPPerSurface[i];
		in_out_display_cfg->hw.DSCEnabled[i] = mode_support_info->DSCEnabled[i];
		in_out_display_cfg->hw.NumberOfDSCSlices[i] = mode_support_info->NumberOfDSCSlices[i];
		in_out_display_cfg->hw.DLGRefClkFreqMHz = 24;
		if (dml2->v20.dml_core_ctx.project != dml_project_dcn35 &&
			dml2->v20.dml_core_ctx.project != dml_project_dcn36 &&
			dml2->v20.dml_core_ctx.project != dml_project_dcn351) {
			/*dGPU default as 50Mhz*/
			in_out_display_cfg->hw.DLGRefClkFreqMHz = 50;
		}
		for (j = 0; j < mode_support_info->DPPPerSurface[i]; j++) {
			if (i >= __DML2_WRAPPER_MAX_STREAMS_PLANES__) {
				dml_print("DML::%s: Index out of bounds: i=%d, __DML2_WRAPPER_MAX_STREAMS_PLANES__=%d\n",
					  __func__, i, __DML2_WRAPPER_MAX_STREAMS_PLANES__);
				break;
			}
			dml2->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id[num_pipes] = dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id[i];
			dml2->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id_valid[num_pipes] = true;
			dml2->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_id[num_pipes] = dml2->v20.scratch.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id[i];
			dml2->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_id_valid[num_pipes] = true;
			num_pipes++;
		}
	}
}

static unsigned int pack_and_call_dml_mode_support_ex(struct dml2_context *dml2,
	const struct dml_display_cfg_st *display_cfg,
	struct dml_mode_support_info_st *evaluation_info,
	enum dc_validate_mode validate_mode)
{
	struct dml2_wrapper_scratch *s = &dml2->v20.scratch;

	s->mode_support_params.mode_lib = &dml2->v20.dml_core_ctx;
	s->mode_support_params.in_display_cfg = display_cfg;
	if (validate_mode == DC_VALIDATE_MODE_ONLY)
		s->mode_support_params.in_start_state_idx = dml2->v20.dml_core_ctx.states.num_states - 1;
	else
		s->mode_support_params.in_start_state_idx = 0;
	s->mode_support_params.out_evaluation_info = evaluation_info;

	memset(evaluation_info, 0, sizeof(struct dml_mode_support_info_st));
	s->mode_support_params.out_lowest_state_idx = 0;

	return dml_mode_support_ex(&s->mode_support_params);
}

static bool optimize_configuration(struct dml2_context *dml2, struct dml2_wrapper_optimize_configuration_params *p)
{
	int unused_dpps = p->ip_params->max_num_dpp;
	int i;
	int odms_needed;
	int largest_blend_and_timing = 0;
	bool optimization_done = false;

	for (i = 0; i < (int) p->cur_display_config->num_timings; i++) {
		if (p->cur_display_config->plane.BlendingAndTiming[i] > largest_blend_and_timing)
			largest_blend_and_timing = p->cur_display_config->plane.BlendingAndTiming[i];
	}

	if (p->new_policy != p->cur_policy)
		*p->new_policy = *p->cur_policy;

	if (p->new_display_config != p->cur_display_config)
		*p->new_display_config = *p->cur_display_config;


	// Optimize Clocks
	if (!optimization_done) {
		if (largest_blend_and_timing == 0 && p->cur_policy->ODMUse[0] == dml_odm_use_policy_combine_as_needed && dml2->config.minimize_dispclk_using_odm) {
			odms_needed = dml2_util_get_maximum_odm_combine_for_output(dml2->config.optimize_odm_4to1,
				p->cur_display_config->output.OutputEncoder[0], p->cur_mode_support_info->DSCEnabled[0]) - 1;

			if (odms_needed <= unused_dpps) {
				if (odms_needed == 1) {
					p->new_policy->ODMUse[0] = dml_odm_use_policy_combine_2to1;
					optimization_done = true;
				} else if (odms_needed == 3) {
					p->new_policy->ODMUse[0] = dml_odm_use_policy_combine_4to1;
					optimization_done = true;
				} else
					optimization_done = false;
			}
		}
	}

	return optimization_done;
}

static int calculate_lowest_supported_state_for_temp_read(struct dml2_context *dml2, struct dc_state *display_state,
		enum dc_validate_mode validate_mode)
{
	struct dml2_calculate_lowest_supported_state_for_temp_read_scratch *s = &dml2->v20.scratch.dml2_calculate_lowest_supported_state_for_temp_read_scratch;
	struct dml2_wrapper_scratch *s_global = &dml2->v20.scratch;

	unsigned int dml_result = 0;
	int result = -1, i, j;

	build_unoptimized_policy_settings(dml2->v20.dml_core_ctx.project, &dml2->v20.dml_core_ctx.policy);

	/* Zero out before each call before proceeding */
	memset(s, 0, sizeof(struct dml2_calculate_lowest_supported_state_for_temp_read_scratch));
	memset(&s_global->mode_support_params, 0, sizeof(struct dml_mode_support_ex_params_st));
	memset(&s_global->dml_to_dc_pipe_mapping, 0, sizeof(struct dml2_dml_to_dc_pipe_mapping));

	for (i = 0; i < dml2->config.dcn_pipe_count; i++) {
		/* Calling resource_build_scaling_params will populate the pipe params
		 * with the necessary information needed for correct DML calculations
		 * This is also done in DML1 driver code path and hence display_state
		 * cannot be const.
		 */
		struct pipe_ctx *pipe = &display_state->res_ctx.pipe_ctx[i];

		if (pipe->plane_state) {
			if (!dml2->config.callbacks.build_scaling_params(pipe)) {
				ASSERT(false);
				return false;
			}
		}
	}

	map_dc_state_into_dml_display_cfg(dml2, display_state, &s->cur_display_config);

	for (i = 0; i < dml2->v20.dml_core_ctx.states.num_states; i++) {
		s->uclk_change_latencies[i] = dml2->v20.dml_core_ctx.states.state_array[i].dram_clock_change_latency_us;
	}

	for (i = 0; i < 4; i++) {
		for (j = 0; j < dml2->v20.dml_core_ctx.states.num_states; j++) {
			dml2->v20.dml_core_ctx.states.state_array[j].dram_clock_change_latency_us = s_global->dummy_pstate_table[i].dummy_pstate_latency_us;
		}

		dml_result = pack_and_call_dml_mode_support_ex(dml2, &s->cur_display_config, &s->evaluation_info,
						validate_mode);

		if (dml_result && s->evaluation_info.DRAMClockChangeSupport[0] == dml_dram_clock_change_vactive) {
			map_hw_resources(dml2, &s->cur_display_config, &s->evaluation_info);
			dml_result = dml_mode_programming(&dml2->v20.dml_core_ctx, s_global->mode_support_params.out_lowest_state_idx, &s->cur_display_config, true);

			ASSERT(dml_result);

			dml2_extract_watermark_set(&dml2->v20.g6_temp_read_watermark_set, &dml2->v20.dml_core_ctx);
			dml2->v20.g6_temp_read_watermark_set.cstate_pstate.fclk_pstate_change_ns = dml2->v20.g6_temp_read_watermark_set.cstate_pstate.pstate_change_ns;

			result = s_global->mode_support_params.out_lowest_state_idx;

			while (dml2->v20.dml_core_ctx.states.state_array[result].dram_speed_mts < s_global->dummy_pstate_table[i].dram_speed_mts)
				result++;

			break;
		}
	}

	for (i = 0; i < dml2->v20.dml_core_ctx.states.num_states; i++) {
		dml2->v20.dml_core_ctx.states.state_array[i].dram_clock_change_latency_us = s->uclk_change_latencies[i];
	}

	return result;
}

static void copy_dummy_pstate_table(struct dummy_pstate_entry *dest, struct dummy_pstate_entry *src, unsigned int num_entries)
{
	for (int i = 0; i < num_entries; i++) {
		dest[i] = src[i];
	}
}

static bool are_timings_requiring_odm_doing_blending(const struct dml_display_cfg_st *display_cfg,
		const struct dml_mode_support_info_st *evaluation_info)
{
	unsigned int planes_per_timing[__DML_NUM_PLANES__] = {0};
	int i;

	for (i = 0; i < display_cfg->num_surfaces; i++)
		planes_per_timing[display_cfg->plane.BlendingAndTiming[i]]++;

	for (i = 0; i < __DML_NUM_PLANES__; i++) {
		if (planes_per_timing[i] > 1 && evaluation_info->ODMMode[i] != dml_odm_mode_bypass)
			return true;
	}

	return false;
}

static bool does_configuration_meet_sw_policies(struct dml2_context *ctx, const struct dml_display_cfg_st *display_cfg,
	const struct dml_mode_support_info_st *evaluation_info)
{
	bool pass = true;

	if (!ctx->config.enable_windowed_mpo_odm) {
		if (are_timings_requiring_odm_doing_blending(display_cfg, evaluation_info))
			pass = false;
	}

	return pass;
}

static bool dml_mode_support_wrapper(struct dml2_context *dml2,
		struct dc_state *display_state,
		enum dc_validate_mode validate_mode)
{
	struct dml2_wrapper_scratch *s = &dml2->v20.scratch;
	unsigned int result = 0, i;
	unsigned int optimized_result = true;

	build_unoptimized_policy_settings(dml2->v20.dml_core_ctx.project, &dml2->v20.dml_core_ctx.policy);

	/* Zero out before each call before proceeding */
	memset(&s->cur_display_config, 0, sizeof(struct dml_display_cfg_st));
	memset(&s->mode_support_params, 0, sizeof(struct dml_mode_support_ex_params_st));
	memset(&s->dml_to_dc_pipe_mapping, 0, sizeof(struct dml2_dml_to_dc_pipe_mapping));
	memset(&s->optimize_configuration_params, 0, sizeof(struct dml2_wrapper_optimize_configuration_params));

	for (i = 0; i < dml2->config.dcn_pipe_count; i++) {
		/* Calling resource_build_scaling_params will populate the pipe params
		 * with the necessary information needed for correct DML calculations
		 * This is also done in DML1 driver code path and hence display_state
		 * cannot be const.
		 */
		struct pipe_ctx *pipe = &display_state->res_ctx.pipe_ctx[i];

		if (pipe->plane_state) {
			if (!dml2->config.callbacks.build_scaling_params(pipe)) {
				ASSERT(false);
				return false;
			}
		}
	}

	map_dc_state_into_dml_display_cfg(dml2, display_state, &s->cur_display_config);
	if (!dml2->config.skip_hw_state_mapping)
		dml2_apply_det_buffer_allocation_policy(dml2, &s->cur_display_config);

	result = pack_and_call_dml_mode_support_ex(dml2,
		&s->cur_display_config,
		&s->mode_support_info,
		validate_mode);

	if (result)
		result = does_configuration_meet_sw_policies(dml2, &s->cur_display_config, &s->mode_support_info);

	// Try to optimize
	if (result) {
		s->cur_policy = dml2->v20.dml_core_ctx.policy;
		s->optimize_configuration_params.dml_core_ctx = &dml2->v20.dml_core_ctx;
		s->optimize_configuration_params.config = &dml2->config;
		s->optimize_configuration_params.ip_params = &dml2->v20.dml_core_ctx.ip;
		s->optimize_configuration_params.cur_display_config = &s->cur_display_config;
		s->optimize_configuration_params.cur_mode_support_info = &s->mode_support_info;
		s->optimize_configuration_params.cur_policy = &s->cur_policy;
		s->optimize_configuration_params.new_display_config = &s->new_display_config;
		s->optimize_configuration_params.new_policy = &s->new_policy;

		while (optimized_result && optimize_configuration(dml2, &s->optimize_configuration_params)) {
			dml2->v20.dml_core_ctx.policy = s->new_policy;
			optimized_result = pack_and_call_dml_mode_support_ex(dml2,
				&s->new_display_config,
				&s->mode_support_info,
				validate_mode);

			if (optimized_result)
				optimized_result = does_configuration_meet_sw_policies(dml2, &s->new_display_config, &s->mode_support_info);

			// If the new optimized state is supposed, then set current = new
			if (optimized_result) {
				s->cur_display_config = s->new_display_config;
				s->cur_policy = s->new_policy;
			} else {
				// Else, restore policy to current
				dml2->v20.dml_core_ctx.policy = s->cur_policy;
			}
		}

		// Optimize ended with a failed config, so we need to restore DML state to last passing
		if (!optimized_result) {
			result = pack_and_call_dml_mode_support_ex(dml2,
				&s->cur_display_config,
				&s->mode_support_info,
				validate_mode);
		}
	}

	if (result)
		map_hw_resources(dml2, &s->cur_display_config, &s->mode_support_info);

	return result;
}

static bool call_dml_mode_support_and_programming(struct dc_state *context, enum dc_validate_mode validate_mode)
{
	unsigned int result = 0;
	unsigned int min_state = 0;
	int min_state_for_g6_temp_read = 0;


	if (!context)
		return false;

	struct dml2_context *dml2 = context->bw_ctx.dml2;
	struct dml2_wrapper_scratch *s = &dml2->v20.scratch;

	if (!context->streams[0]->sink->link->dc->caps.is_apu) {
		min_state_for_g6_temp_read = calculate_lowest_supported_state_for_temp_read(dml2, context,
										validate_mode);

		ASSERT(min_state_for_g6_temp_read >= 0);
	}

	result = dml_mode_support_wrapper(dml2, context, validate_mode);

	/* Upon trying to sett certain frequencies in FRL, min_state_for_g6_temp_read is reported as -1. This leads to an invalid value of min_state causing crashes later on.
	 * Use the default logic for min_state only when min_state_for_g6_temp_read is a valid value. In other cases, use the value calculated by the DML directly.
	 */
	if (!context->streams[0]->sink->link->dc->caps.is_apu) {
		if (min_state_for_g6_temp_read >= 0)
			min_state = min_state_for_g6_temp_read > s->mode_support_params.out_lowest_state_idx ? min_state_for_g6_temp_read : s->mode_support_params.out_lowest_state_idx;
		else
			min_state = s->mode_support_params.out_lowest_state_idx;
	}

	if (result) {
		if (!context->streams[0]->sink->link->dc->caps.is_apu) {
			result = dml_mode_programming(&dml2->v20.dml_core_ctx, min_state, &s->cur_display_config, true);
		} else {
			result = dml_mode_programming(&dml2->v20.dml_core_ctx, s->mode_support_params.out_lowest_state_idx, &s->cur_display_config, true);
		}
	}
	return result;
}

static bool dml2_validate_and_build_resource(const struct dc *in_dc, struct dc_state *context,
		enum dc_validate_mode validate_mode)
{
	struct dml2_context *dml2 = context->bw_ctx.dml2;
	struct dml2_wrapper_scratch *s = &dml2->v20.scratch;
	struct dml2_dcn_clocks out_clks;
	unsigned int result = 0;
	bool need_recalculation = false;
	uint32_t cstate_enter_plus_exit_z8_ns;

	if (context->stream_count == 0) {
		unsigned int lowest_state_idx = 0;

		out_clks.p_state_supported = true;
		out_clks.dispclk_khz = 0; /* No requirement, and lowest index will generally be maximum dispclk. */
		out_clks.dcfclk_khz = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].dcfclk_mhz * 1000;
		out_clks.fclk_khz = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].fabricclk_mhz * 1000;
		out_clks.uclk_mts = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].dram_speed_mts;
		out_clks.phyclk_khz = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].phyclk_mhz * 1000;
		out_clks.socclk_khz = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].socclk_mhz * 1000;
		out_clks.ref_dtbclk_khz = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].dtbclk_mhz * 1000;
		context->bw_ctx.bw.dcn.clk.dtbclk_en = false;
		dml2_copy_clocks_to_dc_state(&out_clks, context);
		return true;
	}

	/* Zero out before each call before proceeding */
	memset(&dml2->v20.scratch, 0, sizeof(struct dml2_wrapper_scratch));
	memset(&dml2->v20.dml_core_ctx.policy, 0, sizeof(struct dml_mode_eval_policy_st));
	memset(&dml2->v20.dml_core_ctx.ms, 0, sizeof(struct mode_support_st));
	memset(&dml2->v20.dml_core_ctx.mp, 0, sizeof(struct mode_program_st));

	/* Initialize DET scratch */
	dml2_initialize_det_scratch(dml2);

	copy_dummy_pstate_table(s->dummy_pstate_table, in_dc->clk_mgr->bw_params->dummy_pstate_table, 4);

	result = call_dml_mode_support_and_programming(context, validate_mode);
	/* Call map dc pipes to map the pipes based on the DML output. For correctly determining if recalculation
	 * is required or not, the resource context needs to correctly reflect the number of active pipes. We would
	 * only know the correct number if active pipes after dml2_map_dc_pipes is called.
	 */
	if (result && !dml2->config.skip_hw_state_mapping)
		dml2_map_dc_pipes(dml2, context, &s->cur_display_config, &s->dml_to_dc_pipe_mapping, in_dc->current_state);

	/* Verify and update DET Buffer configuration if needed. dml2_verify_det_buffer_configuration will check if DET Buffer
	 * size needs to be updated. If yes it will update the DETOverride variable and set need_recalculation flag to true.
	 * Based on that flag, run mode support again. Verification needs to be run after dml_mode_programming because the getters
	 * return correct det buffer values only after dml_mode_programming is called.
	 */
	if (result && !dml2->config.skip_hw_state_mapping) {
		need_recalculation = dml2_verify_det_buffer_configuration(dml2, context, &dml2->det_helper_scratch);
		if (need_recalculation) {
			/* Engage the DML again if recalculation is required. */
			call_dml_mode_support_and_programming(context, validate_mode);
			if (!dml2->config.skip_hw_state_mapping) {
				dml2_map_dc_pipes(dml2, context, &s->cur_display_config, &s->dml_to_dc_pipe_mapping, in_dc->current_state);
			}
			need_recalculation = dml2_verify_det_buffer_configuration(dml2, context, &dml2->det_helper_scratch);
			ASSERT(need_recalculation == false);
		}
	}

	if (result) {
		unsigned int lowest_state_idx = s->mode_support_params.out_lowest_state_idx;
		out_clks.dispclk_khz = (unsigned int)dml2->v20.dml_core_ctx.mp.Dispclk_calculated * 1000;
		out_clks.p_state_supported = s->mode_support_info.DRAMClockChangeSupport[0] != dml_dram_clock_change_unsupported;
		if (in_dc->config.use_default_clock_table &&
			(lowest_state_idx < dml2->v20.dml_core_ctx.states.num_states - 1)) {
			lowest_state_idx = dml2->v20.dml_core_ctx.states.num_states - 1;
			out_clks.dispclk_khz = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].dispclk_mhz * 1000;
		}

		out_clks.dcfclk_khz = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].dcfclk_mhz * 1000;
		out_clks.fclk_khz = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].fabricclk_mhz * 1000;
		out_clks.uclk_mts = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].dram_speed_mts;
		out_clks.phyclk_khz = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].phyclk_mhz * 1000;
		out_clks.socclk_khz = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].socclk_mhz * 1000;
		out_clks.ref_dtbclk_khz = (unsigned int)dml2->v20.dml_core_ctx.states.state_array[lowest_state_idx].dtbclk_mhz * 1000;
		context->bw_ctx.bw.dcn.clk.dtbclk_en = is_dtbclk_required(in_dc, context);

		if (!dml2->config.skip_hw_state_mapping) {
			/* Call dml2_calculate_rq_and_dlg_params */
			dml2_calculate_rq_and_dlg_params(in_dc, context, &context->res_ctx, dml2, in_dc->res_pool->pipe_count);
		}

		dml2_copy_clocks_to_dc_state(&out_clks, context);
		dml2_extract_watermark_set(&context->bw_ctx.bw.dcn.watermarks.a, &dml2->v20.dml_core_ctx);
		dml2_extract_watermark_set(&context->bw_ctx.bw.dcn.watermarks.b, &dml2->v20.dml_core_ctx);
		if (context->streams[0]->sink->link->dc->caps.is_apu)
			dml2_extract_watermark_set(&context->bw_ctx.bw.dcn.watermarks.c, &dml2->v20.dml_core_ctx);
		else
			memcpy(&context->bw_ctx.bw.dcn.watermarks.c, &dml2->v20.g6_temp_read_watermark_set, sizeof(context->bw_ctx.bw.dcn.watermarks.c));
		dml2_extract_watermark_set(&context->bw_ctx.bw.dcn.watermarks.d, &dml2->v20.dml_core_ctx);
		dml2_extract_writeback_wm(context, &dml2->v20.dml_core_ctx);
		//copy for deciding zstate use
		context->bw_ctx.dml.vba.StutterPeriod = context->bw_ctx.dml2->v20.dml_core_ctx.mp.StutterPeriod;

		cstate_enter_plus_exit_z8_ns = context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_z8_ns;

		if (context->bw_ctx.dml.vba.StutterPeriod < in_dc->debug.minimum_z8_residency_time &&
				cstate_enter_plus_exit_z8_ns < in_dc->debug.minimum_z8_residency_time * 1000)
			cstate_enter_plus_exit_z8_ns = in_dc->debug.minimum_z8_residency_time * 1000;

		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_z8_ns = cstate_enter_plus_exit_z8_ns;
	}

	return result;
}

static bool dml2_validate_only(struct dc_state *context, enum dc_validate_mode validate_mode)
{
	struct dml2_context *dml2;
	unsigned int result = 0;

	if (!context || context->stream_count == 0)
		return true;

	dml2 = context->bw_ctx.dml2;

	/* Zero out before each call before proceeding */
	memset(&dml2->v20.scratch, 0, sizeof(struct dml2_wrapper_scratch));
	memset(&dml2->v20.dml_core_ctx.policy, 0, sizeof(struct dml_mode_eval_policy_st));
	memset(&dml2->v20.dml_core_ctx.ms, 0, sizeof(struct mode_support_st));
	memset(&dml2->v20.dml_core_ctx.mp, 0, sizeof(struct mode_program_st));

	build_unoptimized_policy_settings(dml2->v20.dml_core_ctx.project, &dml2->v20.dml_core_ctx.policy);

	map_dc_state_into_dml_display_cfg(dml2, context, &dml2->v20.scratch.cur_display_config);
	 if (!dml2->config.skip_hw_state_mapping)
		 dml2_apply_det_buffer_allocation_policy(dml2, &dml2->v20.scratch.cur_display_config);

	result = pack_and_call_dml_mode_support_ex(dml2,
		&dml2->v20.scratch.cur_display_config,
		&dml2->v20.scratch.mode_support_info,
		validate_mode);

	if (result)
		result = does_configuration_meet_sw_policies(dml2, &dml2->v20.scratch.cur_display_config, &dml2->v20.scratch.mode_support_info);

	return result == 1;
}

static void dml2_apply_debug_options(const struct dc *dc, struct dml2_context *dml2)
{
	if (dc->debug.override_odm_optimization) {
		dml2->config.minimize_dispclk_using_odm = dc->debug.minimize_dispclk_using_odm;
	}
}

bool dml2_validate(const struct dc *in_dc, struct dc_state *context, struct dml2_context *dml2,
	enum dc_validate_mode validate_mode)
{
	bool out = false;

	if (!dml2)
		return false;
	dml2_apply_debug_options(in_dc, dml2);

	/* DML2.1 validation path */
	if (dml2->architecture == dml2_architecture_21) {
		out = dml21_validate(in_dc, context, dml2, validate_mode);
		return out;
	}

	DC_FP_START();

	/* Use dml_validate_only for DC_VALIDATE_MODE_ONLY and DC_VALIDATE_MODE_AND_STATE_INDEX path */
	if (validate_mode != DC_VALIDATE_MODE_AND_PROGRAMMING)
		out = dml2_validate_only(context, validate_mode);
	else
		out = dml2_validate_and_build_resource(in_dc, context, validate_mode);

	DC_FP_END();

	return out;
}

static inline struct dml2_context *dml2_allocate_memory(void)
{
	return (struct dml2_context *) vzalloc(sizeof(struct dml2_context));
}

static void dml2_init(const struct dc *in_dc, const struct dml2_configuration_options *config, struct dml2_context **dml2)
{
	if ((in_dc->debug.using_dml21) && (in_dc->ctx->dce_version >= DCN_VERSION_4_01)) {
		dml21_reinit(in_dc, *dml2, config);
		return;
	}

	// Store config options
	(*dml2)->config = *config;

	switch (in_dc->ctx->dce_version) {
	case DCN_VERSION_3_5:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn35;
		break;
	case DCN_VERSION_3_51:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn351;
		break;
	case DCN_VERSION_3_6:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn36;
		break;
	case DCN_VERSION_3_2:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn32;
		break;
	case DCN_VERSION_3_21:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn321;
		break;
	case DCN_VERSION_4_01:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn401;
		break;
	default:
		(*dml2)->v20.dml_core_ctx.project = dml_project_default;
		break;
	}

	DC_FP_START();

	initialize_dml2_ip_params(*dml2, in_dc, &(*dml2)->v20.dml_core_ctx.ip);

	initialize_dml2_soc_bbox(*dml2, in_dc, &(*dml2)->v20.dml_core_ctx.soc);

	initialize_dml2_soc_states(*dml2, in_dc, &(*dml2)->v20.dml_core_ctx.soc, &(*dml2)->v20.dml_core_ctx.states);

	DC_FP_END();
}

bool dml2_create(const struct dc *in_dc, const struct dml2_configuration_options *config, struct dml2_context **dml2)
{
	// TODO : Temporarily add DCN_VERSION_3_2 for N-1 validation. Remove DCN_VERSION_3_2 after N-1 validation phase is complete.
	if ((in_dc->debug.using_dml21) && (in_dc->ctx->dce_version >= DCN_VERSION_4_01))
		return dml21_create(in_dc, dml2, config);

	// Allocate Mode Lib Ctx
	*dml2 = dml2_allocate_memory();

	if (!(*dml2))
		return false;

	dml2_init(in_dc, config, dml2);

	return true;
}

void dml2_destroy(struct dml2_context *dml2)
{
	if (!dml2)
		return;

	if (dml2->architecture == dml2_architecture_21)
		dml21_destroy(dml2);
	vfree(dml2);
}

void dml2_extract_dram_and_fclk_change_support(struct dml2_context *dml2,
	unsigned int *fclk_change_support, unsigned int *dram_clk_change_support)
{
	*fclk_change_support = (unsigned int) dml2->v20.dml_core_ctx.ms.support.FCLKChangeSupport[0];
	*dram_clk_change_support = (unsigned int) dml2->v20.dml_core_ctx.ms.support.DRAMClockChangeSupport[0];
}

void dml2_prepare_mcache_programming(struct dc *in_dc, struct dc_state *context, struct dml2_context *dml2)
{
	if (dml2->architecture == dml2_architecture_21)
		dml21_prepare_mcache_programming(in_dc, context, dml2);
}

void dml2_copy(struct dml2_context *dst_dml2,
	struct dml2_context *src_dml2)
{
	if (src_dml2->architecture == dml2_architecture_21) {
		dml21_copy(dst_dml2, src_dml2);
		return;
	}
	/* copy Mode Lib Ctx */
	memcpy(dst_dml2, src_dml2, sizeof(struct dml2_context));
}

bool dml2_create_copy(struct dml2_context **dst_dml2,
	struct dml2_context *src_dml2)
{
	if (src_dml2->architecture == dml2_architecture_21)
		return dml21_create_copy(dst_dml2, src_dml2);
	/* Allocate Mode Lib Ctx */
	*dst_dml2 = dml2_allocate_memory();

	if (!(*dst_dml2))
		return false;

	/* copy Mode Lib Ctx */
	dml2_copy(*dst_dml2, src_dml2);

	return true;
}

void dml2_reinit(const struct dc *in_dc,
				 const struct dml2_configuration_options *config,
				 struct dml2_context **dml2)
{
	if ((in_dc->debug.using_dml21) && (in_dc->ctx->dce_version >= DCN_VERSION_4_01)) {
		dml21_reinit(in_dc, *dml2, config);
		return;
	}

	dml2_init(in_dc, config, dml2);
}
