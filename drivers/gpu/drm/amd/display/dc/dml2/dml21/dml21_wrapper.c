// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#include "dml2_internal_types.h"
#include "dml_top.h"
#include "dml2_core_dcn4_calcs.h"
#include "dml2_internal_shared_types.h"
#include "dml21_utils.h"
#include "dml21_translation_helper.h"
#include "dml2_dc_resource_mgmt.h"

static bool dml21_allocate_memory(struct dml2_context **dml_ctx)
{
	*dml_ctx = (struct dml2_context *)kzalloc(sizeof(struct dml2_context), GFP_KERNEL);
	if (!(*dml_ctx))
		return false;

	(*dml_ctx)->v21.dml_init.dml2_instance = (struct dml2_instance *)kzalloc(sizeof(struct dml2_instance), GFP_KERNEL);
	if (!((*dml_ctx)->v21.dml_init.dml2_instance))
		return false;

	(*dml_ctx)->v21.mode_support.dml2_instance = (*dml_ctx)->v21.dml_init.dml2_instance;
	(*dml_ctx)->v21.mode_programming.dml2_instance = (*dml_ctx)->v21.dml_init.dml2_instance;

	(*dml_ctx)->v21.mode_support.display_config = &(*dml_ctx)->v21.display_config;
	(*dml_ctx)->v21.mode_programming.display_config = (*dml_ctx)->v21.mode_support.display_config;

	(*dml_ctx)->v21.mode_programming.programming = (struct dml2_display_cfg_programming *)kzalloc(sizeof(struct dml2_display_cfg_programming), GFP_KERNEL);
	if (!((*dml_ctx)->v21.mode_programming.programming))
		return false;

	return true;
}

static void dml21_apply_debug_options(const struct dc *in_dc, struct dml2_context *dml_ctx, const struct dml2_configuration_options *config)
{
	bool disable_fams2;
	struct dml2_pmo_options *pmo_options = &dml_ctx->v21.dml_init.options.pmo_options;

	/* ODM options */
	pmo_options->disable_dyn_odm = !config->minimize_dispclk_using_odm;
	pmo_options->disable_dyn_odm_for_multi_stream = true;
	pmo_options->disable_dyn_odm_for_stream_with_svp = true;

	/* UCLK P-State options */
	if (in_dc->debug.dml21_force_pstate_method) {
		dml_ctx->config.pmo.force_pstate_method_enable = true;
		for (int i = 0; i < MAX_PIPES; i++)
			dml_ctx->config.pmo.force_pstate_method_values[i] = in_dc->debug.dml21_force_pstate_method_values[i];
	} else {
		dml_ctx->config.pmo.force_pstate_method_enable = false;
	}

	pmo_options->disable_vblank = ((in_dc->debug.dml21_disable_pstate_method_mask >> 1) & 1);

	/* NOTE: DRR and SubVP Require FAMS2 */
	disable_fams2 = !in_dc->debug.fams2_config.bits.enable;
	pmo_options->disable_svp = ((in_dc->debug.dml21_disable_pstate_method_mask >> 2) & 1) ||
			in_dc->debug.force_disable_subvp ||
			disable_fams2;
	pmo_options->disable_drr_clamped = ((in_dc->debug.dml21_disable_pstate_method_mask >> 3) & 1) ||
			disable_fams2;
	pmo_options->disable_drr_var = ((in_dc->debug.dml21_disable_pstate_method_mask >> 4) & 1) ||
			disable_fams2;
	pmo_options->disable_fams2 = disable_fams2;

	pmo_options->disable_drr_var_when_var_active = in_dc->debug.disable_fams_gaming;
}

static void dml21_init(const struct dc *in_dc, struct dml2_context **dml_ctx, const struct dml2_configuration_options *config)
{
	switch (in_dc->ctx->dce_version) {
	case DCN_VERSION_4_01:
	case DCN_VERSION_3_2:	// TODO : Temporary for N-1 validation. Remove this after N-1 validation phase is complete.
		(*dml_ctx)->v21.dml_init.options.project_id = dml2_project_dcn4x_stage2_auto_drr_svp;
		break;
	default:
		(*dml_ctx)->v21.dml_init.options.project_id = dml2_project_invalid;
	}

	(*dml_ctx)->architecture = dml2_architecture_21;

	/* Store configuration options */
	(*dml_ctx)->config = *config;

	/*Initialize SOCBB and DCNIP params */
	dml21_initialize_soc_bb_params(&(*dml_ctx)->v21.dml_init, config, in_dc);
	dml21_initialize_ip_params(&(*dml_ctx)->v21.dml_init, config, in_dc);
	dml21_apply_soc_bb_overrides(&(*dml_ctx)->v21.dml_init, config, in_dc);

	/* apply debug overrides */
	dml21_apply_debug_options(in_dc, *dml_ctx, config);

	/*Initialize DML21 instance */
	dml2_initialize_instance(&(*dml_ctx)->v21.dml_init);
}

bool dml21_create(const struct dc *in_dc, struct dml2_context **dml_ctx, const struct dml2_configuration_options *config)
{
	/* Allocate memory for initializing DML21 instance */
	if (!dml21_allocate_memory(dml_ctx))
		return false;

	dml21_init(in_dc, dml_ctx, config);

	return true;
}

void dml21_destroy(struct dml2_context *dml2)
{
	kfree(dml2->v21.dml_init.dml2_instance);
	kfree(dml2->v21.mode_programming.programming);
}

static void dml21_calculate_rq_and_dlg_params(const struct dc *dc, struct dc_state *context, struct resource_context *out_new_hw_state,
	struct dml2_context *in_ctx, unsigned int pipe_cnt)
{
	unsigned int dml_prog_idx = 0, dc_pipe_index = 0, num_dpps_required = 0;
	struct dml2_per_plane_programming *pln_prog = NULL;
	struct dml2_per_stream_programming *stream_prog = NULL;
	struct pipe_ctx *dc_main_pipes[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
	struct pipe_ctx *dc_phantom_pipes[__DML2_WRAPPER_MAX_STREAMS_PLANES__] = {0};
	int num_pipes;

	context->bw_ctx.bw.dcn.clk.dppclk_khz = 0;

	/* copy global DCHUBBUB arbiter registers */
	memcpy(&context->bw_ctx.bw.dcn.arb_regs, &in_ctx->v21.mode_programming.programming->global_regs.arb_regs, sizeof(struct dml2_display_arb_regs));

	/* legacy only */
	context->bw_ctx.bw.dcn.compbuf_size_kb = (int)in_ctx->v21.mode_programming.programming->global_regs.arb_regs.compbuf_size * 64;

	context->bw_ctx.bw.dcn.mall_ss_size_bytes = 0;
	context->bw_ctx.bw.dcn.mall_ss_psr_active_size_bytes = 0;
	context->bw_ctx.bw.dcn.mall_subvp_size_bytes = 0;

	for (dml_prog_idx = 0; dml_prog_idx < DML2_MAX_PLANES; dml_prog_idx++) {
		pln_prog = &in_ctx->v21.mode_programming.programming->plane_programming[dml_prog_idx];

		if (!pln_prog->plane_descriptor)
			continue;

		stream_prog = &in_ctx->v21.mode_programming.programming->stream_programming[pln_prog->plane_descriptor->stream_index];
		num_dpps_required = pln_prog->num_dpps_required;

		if (num_dpps_required == 0) {
			continue;
		}
		num_pipes = dml21_find_dc_pipes_for_plane(dc, context, in_ctx, dc_main_pipes, dc_phantom_pipes, dml_prog_idx);

		if (num_pipes <= 0)
			continue;

		/* program each pipe */
		for (dc_pipe_index = 0; dc_pipe_index < num_pipes; dc_pipe_index++) {
			dml21_program_dc_pipe(in_ctx, context, dc_main_pipes[dc_pipe_index], pln_prog, stream_prog);

			if (pln_prog->phantom_plane.valid && dc_phantom_pipes[dc_pipe_index]) {
				dml21_program_dc_pipe(in_ctx, context, dc_phantom_pipes[dc_pipe_index], pln_prog, stream_prog);
			}
		}
	}

	/* assign global clocks */
	context->bw_ctx.bw.dcn.clk.bw_dppclk_khz = context->bw_ctx.bw.dcn.clk.dppclk_khz;
	context->bw_ctx.bw.dcn.clk.bw_dispclk_khz = context->bw_ctx.bw.dcn.clk.dispclk_khz;
	if (in_ctx->v21.dml_init.soc_bb.clk_table.dispclk.num_clk_values > 1) {
		context->bw_ctx.bw.dcn.clk.max_supported_dispclk_khz =
			in_ctx->v21.dml_init.soc_bb.clk_table.dispclk.clk_values_khz[in_ctx->v21.dml_init.soc_bb.clk_table.dispclk.num_clk_values] * 1000;
	} else {
		context->bw_ctx.bw.dcn.clk.max_supported_dispclk_khz = in_ctx->v21.dml_init.soc_bb.clk_table.dispclk.clk_values_khz[0] * 1000;
	}

	if (in_ctx->v21.dml_init.soc_bb.clk_table.dppclk.num_clk_values > 1) {
		context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz =
			in_ctx->v21.dml_init.soc_bb.clk_table.dppclk.clk_values_khz[in_ctx->v21.dml_init.soc_bb.clk_table.dppclk.num_clk_values] * 1000;
	} else {
		context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz = in_ctx->v21.dml_init.soc_bb.clk_table.dppclk.clk_values_khz[0] * 1000;
	}

	/* get global mall allocation */
	if (dc->res_pool->funcs->calculate_mall_ways_from_bytes) {
		context->bw_ctx.bw.dcn.clk.num_ways = dc->res_pool->funcs->calculate_mall_ways_from_bytes(dc, context->bw_ctx.bw.dcn.mall_subvp_size_bytes);
	} else {
		context->bw_ctx.bw.dcn.clk.num_ways = 0;
	}
}

static bool dml21_mode_check_and_programming(const struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx)
{
	bool result = false;
	struct dml2_build_mode_programming_in_out *mode_programming = &dml_ctx->v21.mode_programming;

	memset(&dml_ctx->v21.display_config, 0, sizeof(struct dml2_display_cfg));
	memset(&dml_ctx->v21.dml_to_dc_pipe_mapping, 0, sizeof(struct dml2_dml_to_dc_pipe_mapping));
	memset(&dml_ctx->v21.mode_programming.dml2_instance->scratch.build_mode_programming_locals.mode_programming_params, 0, sizeof(struct dml2_core_mode_programming_in_out));

	if (!context)
		return true;

	if (context->stream_count == 0) {
		dml21_build_fams2_programming(in_dc, context, dml_ctx);
		return true;
	}

	/* scrub phantom's from current dc_state */
	dml_ctx->config.svp_pstate.callbacks.remove_phantom_streams_and_planes(in_dc, context);
	dml_ctx->config.svp_pstate.callbacks.release_phantom_streams_and_planes(in_dc, context);

	/* Populate stream, plane mappings and other fields in display config. */
	result = dml21_map_dc_state_into_dml_display_cfg(in_dc, context, dml_ctx);
	if (!result)
		return false;

	result = dml2_build_mode_programming(mode_programming);
	if (!result)
		return false;

	/* Check and map HW resources */
	if (result && !dml_ctx->config.skip_hw_state_mapping) {
		dml21_map_hw_resources(dml_ctx);
		dml2_map_dc_pipes(dml_ctx, context, NULL, &dml_ctx->v21.dml_to_dc_pipe_mapping, in_dc->current_state);
		/* if subvp phantoms are present, expand them into dc context */
		dml21_handle_phantom_streams_planes(in_dc, context, dml_ctx);
	}

	/* Copy DML CLK, WM and REG outputs to bandwidth context */
	if (result && !dml_ctx->config.skip_hw_state_mapping) {
		dml21_calculate_rq_and_dlg_params(in_dc, context, &context->res_ctx, dml_ctx, in_dc->res_pool->pipe_count);
		dml21_copy_clocks_to_dc_state(dml_ctx, context);
		dml21_extract_watermark_sets(in_dc, &context->bw_ctx.bw.dcn.watermarks, dml_ctx);
		if (in_dc->ctx->dce_version == DCN_VERSION_3_2) {
			dml21_extract_legacy_watermark_set(in_dc, &context->bw_ctx.bw.dcn.watermarks.a, DML2_DCHUB_WATERMARK_SET_A, dml_ctx);
			dml21_extract_legacy_watermark_set(in_dc, &context->bw_ctx.bw.dcn.watermarks.b, DML2_DCHUB_WATERMARK_SET_A, dml_ctx);
			dml21_extract_legacy_watermark_set(in_dc, &context->bw_ctx.bw.dcn.watermarks.c, DML2_DCHUB_WATERMARK_SET_A, dml_ctx);
			dml21_extract_legacy_watermark_set(in_dc, &context->bw_ctx.bw.dcn.watermarks.d, DML2_DCHUB_WATERMARK_SET_A, dml_ctx);
		}

		dml21_build_fams2_programming(in_dc, context, dml_ctx);
	}

	return true;
}

static bool dml21_check_mode_support(const struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx)
{
	bool is_supported = false;
	struct dml2_initialize_instance_in_out *dml_init = &dml_ctx->v21.dml_init;
	struct dml2_check_mode_supported_in_out *mode_support = &dml_ctx->v21.mode_support;

	memset(&dml_ctx->v21.display_config, 0, sizeof(struct dml2_display_cfg));
	memset(&dml_ctx->v21.dml_to_dc_pipe_mapping, 0, sizeof(struct dml2_dml_to_dc_pipe_mapping));
	memset(&dml_ctx->v21.mode_programming.dml2_instance->scratch.check_mode_supported_locals.mode_support_params, 0, sizeof(struct dml2_core_mode_support_in_out));

	if (!context || context->stream_count == 0)
		return true;

	/* Scrub phantom's from current dc_state */
	dml_ctx->config.svp_pstate.callbacks.remove_phantom_streams_and_planes(in_dc, context);
	dml_ctx->config.svp_pstate.callbacks.release_phantom_streams_and_planes(in_dc, context);

	mode_support->dml2_instance = dml_init->dml2_instance;
	dml21_map_dc_state_into_dml_display_cfg(in_dc, context, dml_ctx);
	dml_ctx->v21.mode_programming.dml2_instance->scratch.build_mode_programming_locals.mode_programming_params.programming = dml_ctx->v21.mode_programming.programming;
	is_supported = dml2_check_mode_supported(mode_support);
	if (!is_supported)
		return false;

	return true;
}

bool dml21_validate(const struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx, bool fast_validate)
{
	bool out = false;

	/* Use dml_validate_only for fast_validate path */
	if (fast_validate) {
		out = dml21_check_mode_support(in_dc, context, dml_ctx);
	} else
		out = dml21_mode_check_and_programming(in_dc, context, dml_ctx);
	return out;
}

void dml21_prepare_mcache_programming(struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx)
{
	unsigned int dml_prog_idx, dml_phantom_prog_idx, dc_pipe_index;
	int num_pipes;
	struct pipe_ctx *dc_main_pipes[__DML2_WRAPPER_MAX_STREAMS_PLANES__];
	struct pipe_ctx *dc_phantom_pipes[__DML2_WRAPPER_MAX_STREAMS_PLANES__] = {0};

	struct dml2_per_plane_programming *pln_prog = NULL;
	struct dml2_plane_mcache_configuration_descriptor *mcache_config = NULL;
	struct prepare_mcache_programming_locals *l = &dml_ctx->v21.scratch.prepare_mcache_locals;

	if (context->stream_count == 0) {
		return;
	}

	memset(&l->build_mcache_programming_params, 0, sizeof(struct dml2_build_mcache_programming_in_out));
	l->build_mcache_programming_params.dml2_instance = dml_ctx->v21.dml_init.dml2_instance;

	/* phantom's start after main planes */
	dml_phantom_prog_idx = dml_ctx->v21.mode_programming.programming->display_config.num_planes;

	/* Build mcache programming parameters per plane per pipe */
	for (dml_prog_idx = 0; dml_prog_idx < dml_ctx->v21.mode_programming.programming->display_config.num_planes; dml_prog_idx++) {
		pln_prog = &dml_ctx->v21.mode_programming.programming->plane_programming[dml_prog_idx];

		mcache_config = &l->build_mcache_programming_params.mcache_configurations[dml_prog_idx];
		memset(mcache_config, 0, sizeof(struct dml2_plane_mcache_configuration_descriptor));
		mcache_config->plane_descriptor = pln_prog->plane_descriptor;
		mcache_config->mcache_allocation = &context->bw_ctx.bw.dcn.mcache_allocations[dml_prog_idx];
		mcache_config->num_pipes = pln_prog->num_dpps_required;
		l->build_mcache_programming_params.num_configurations++;

		if (pln_prog->num_dpps_required == 0) {
			continue;
		}

		num_pipes = dml21_find_dc_pipes_for_plane(in_dc, context, dml_ctx, dc_main_pipes, dc_phantom_pipes, dml_prog_idx);
		if (num_pipes <= 0 || dc_main_pipes[0]->stream == NULL ||
		    dc_main_pipes[0]->plane_state == NULL)
			continue;

		/* get config for each pipe */
		for (dc_pipe_index = 0; dc_pipe_index < num_pipes; dc_pipe_index++) {
			ASSERT(dc_main_pipes[dc_pipe_index]);
			dml21_get_pipe_mcache_config(context, dc_main_pipes[dc_pipe_index], pln_prog, &mcache_config->pipe_configurations[dc_pipe_index]);
		}

		/* get config for each phantom pipe */
		if (pln_prog->phantom_plane.valid &&
				dc_phantom_pipes[0] &&
				dc_main_pipes[0]->stream &&
				dc_phantom_pipes[0]->plane_state) {
			mcache_config = &l->build_mcache_programming_params.mcache_configurations[dml_phantom_prog_idx];
			memset(mcache_config, 0, sizeof(struct dml2_plane_mcache_configuration_descriptor));
			mcache_config->plane_descriptor = pln_prog->plane_descriptor;
			mcache_config->mcache_allocation = &context->bw_ctx.bw.dcn.mcache_allocations[dml_phantom_prog_idx];
			mcache_config->num_pipes = pln_prog->num_dpps_required;
			l->build_mcache_programming_params.num_configurations++;

			for (dc_pipe_index = 0; dc_pipe_index < num_pipes; dc_pipe_index++) {
				ASSERT(dc_phantom_pipes[dc_pipe_index]);
				dml21_get_pipe_mcache_config(context, dc_phantom_pipes[dc_pipe_index], pln_prog, &mcache_config->pipe_configurations[dc_pipe_index]);
			}

			/* increment phantom index */
			dml_phantom_prog_idx++;
		}
	}

	/* Call to generate mcache programming per plane per pipe for the given display configuration */
	dml2_build_mcache_programming(&l->build_mcache_programming_params);

	/* get per plane per pipe mcache programming */
	for (dml_prog_idx = 0; dml_prog_idx < dml_ctx->v21.mode_programming.programming->display_config.num_planes; dml_prog_idx++) {
		pln_prog = &dml_ctx->v21.mode_programming.programming->plane_programming[dml_prog_idx];

		num_pipes = dml21_find_dc_pipes_for_plane(in_dc, context, dml_ctx, dc_main_pipes, dc_phantom_pipes, dml_prog_idx);
		if (num_pipes <= 0 || dc_main_pipes[0]->stream == NULL ||
		    dc_main_pipes[0]->plane_state == NULL)
			continue;

		/* get config for each pipe */
		for (dc_pipe_index = 0; dc_pipe_index < num_pipes; dc_pipe_index++) {
			ASSERT(dc_main_pipes[dc_pipe_index]);
			if (l->build_mcache_programming_params.per_plane_pipe_mcache_regs[dml_prog_idx][dc_pipe_index]) {
				memcpy(&dc_main_pipes[dc_pipe_index]->mcache_regs,
						l->build_mcache_programming_params.per_plane_pipe_mcache_regs[dml_prog_idx][dc_pipe_index],
						sizeof(struct dml2_hubp_pipe_mcache_regs));
			}
		}

		/* get config for each phantom pipe */
		if (pln_prog->phantom_plane.valid &&
				dc_phantom_pipes[0] &&
				dc_main_pipes[0]->stream &&
				dc_phantom_pipes[0]->plane_state) {
			for (dc_pipe_index = 0; dc_pipe_index < num_pipes; dc_pipe_index++) {
				ASSERT(dc_phantom_pipes[dc_pipe_index]);
				if (l->build_mcache_programming_params.per_plane_pipe_mcache_regs[dml_phantom_prog_idx][dc_pipe_index]) {
					memcpy(&dc_phantom_pipes[dc_pipe_index]->mcache_regs,
							l->build_mcache_programming_params.per_plane_pipe_mcache_regs[dml_phantom_prog_idx][dc_pipe_index],
							sizeof(struct dml2_hubp_pipe_mcache_regs));
				}
			}
			/* increment phantom index */
			dml_phantom_prog_idx++;
		}
	}
}

void dml21_copy(struct dml2_context *dst_dml_ctx,
	struct dml2_context *src_dml_ctx)
{
	/* Preserve references to internals */
	struct dml2_instance *dst_dml2_instance = dst_dml_ctx->v21.dml_init.dml2_instance;
	struct dml2_display_cfg_programming *dst_dml2_programming = dst_dml_ctx->v21.mode_programming.programming;

	/* Copy context */
	memcpy(dst_dml_ctx, src_dml_ctx, sizeof(struct dml2_context));

	/* Copy Internals */
	memcpy(dst_dml2_instance, src_dml_ctx->v21.dml_init.dml2_instance, sizeof(struct dml2_instance));
	memcpy(dst_dml2_programming, src_dml_ctx->v21.mode_programming.programming, sizeof(struct dml2_display_cfg_programming));

	/* Restore references to internals */
	dst_dml_ctx->v21.dml_init.dml2_instance = dst_dml2_instance;

	dst_dml_ctx->v21.mode_support.dml2_instance = dst_dml2_instance;
	dst_dml_ctx->v21.mode_programming.dml2_instance = dst_dml2_instance;

	dst_dml_ctx->v21.mode_support.display_config = &dst_dml_ctx->v21.display_config;
	dst_dml_ctx->v21.mode_programming.display_config = dst_dml_ctx->v21.mode_support.display_config;

	dst_dml_ctx->v21.mode_programming.programming = dst_dml2_programming;

	/* need to initialize copied instance for internal references to be correct */
	dml2_initialize_instance(&dst_dml_ctx->v21.dml_init);
}

bool dml21_create_copy(struct dml2_context **dst_dml_ctx,
	struct dml2_context *src_dml_ctx)
{
	/* Allocate memory for initializing DML21 instance */
	if (!dml21_allocate_memory(dst_dml_ctx))
		return false;

	dml21_copy(*dst_dml_ctx, src_dml_ctx);

	return true;
}

void dml21_reinit(const struct dc *in_dc, struct dml2_context **dml_ctx, const struct dml2_configuration_options *config)
{
	dml21_init(in_dc, dml_ctx, config);
}

