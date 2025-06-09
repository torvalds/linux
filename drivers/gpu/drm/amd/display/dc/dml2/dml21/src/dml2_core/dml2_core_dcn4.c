// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_internal_shared_types.h"
#include "dml2_core_shared_types.h"
#include "dml2_core_dcn4.h"
#include "dml2_core_dcn4_calcs.h"
#include "dml2_debug.h"
#include "lib_float_math.h"

struct dml2_core_ip_params core_dcn4_ip_caps_base = {
	// Hardcoded values for DCN3x
	.vblank_nom_default_us = 668,
	.remote_iommu_outstanding_translations = 256,
	.rob_buffer_size_kbytes = 128,
	.config_return_buffer_size_in_kbytes = 1280,
	.config_return_buffer_segment_size_in_kbytes = 64,
	.compressed_buffer_segment_size_in_kbytes = 64,
	.dpte_buffer_size_in_pte_reqs_luma = 68,
	.dpte_buffer_size_in_pte_reqs_chroma = 36,
	.pixel_chunk_size_kbytes = 8,
	.alpha_pixel_chunk_size_kbytes = 4,
	.min_pixel_chunk_size_bytes = 1024,
	.writeback_chunk_size_kbytes = 8,
	.line_buffer_size_bits = 1171920,
	.max_line_buffer_lines = 32,
	.writeback_interface_buffer_size_kbytes = 90,
	//Number of pipes after DCN Pipe harvesting
	.max_num_dpp = 4,
	.max_num_otg = 4,
	.max_num_wb = 1,
	.max_dchub_pscl_bw_pix_per_clk = 4,
	.max_pscl_lb_bw_pix_per_clk = 2,
	.max_lb_vscl_bw_pix_per_clk = 4,
	.max_vscl_hscl_bw_pix_per_clk = 4,
	.max_hscl_ratio = 6,
	.max_vscl_ratio = 6,
	.max_hscl_taps = 8,
	.max_vscl_taps = 8,
	.dispclk_ramp_margin_percent = 1,
	.dppclk_delay_subtotal = 47,
	.dppclk_delay_scl = 50,
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_cnvc_formatter = 28,
	.dppclk_delay_cnvc_cursor = 6,
	.cursor_buffer_size = 24,
	.cursor_chunk_size = 2,
	.dispclk_delay_subtotal = 125,
	.max_inter_dcn_tile_repeaters = 8,
	.writeback_max_hscl_ratio = 1,
	.writeback_max_vscl_ratio = 1,
	.writeback_min_hscl_ratio = 1,
	.writeback_min_vscl_ratio = 1,
	.writeback_max_hscl_taps = 1,
	.writeback_max_vscl_taps = 1,
	.writeback_line_buffer_buffer_size = 0,
	.num_dsc = 4,
	.maximum_dsc_bits_per_component = 12,
	.maximum_pixels_per_line_per_dsc_unit = 5760,
	.dsc422_native_support = true,
	.dcc_supported = true,
	.ptoi_supported = false,

	.cursor_64bpp_support = true,
	.dynamic_metadata_vm_enabled = false,

	.max_num_dp2p0_outputs = 4,
	.max_num_dp2p0_streams = 4,
	.imall_supported = 1,
	.max_flip_time_us = 80,
	.max_flip_time_lines = 32,
	.words_per_channel = 16,

	.subvp_fw_processing_delay_us = 15,
	.subvp_pstate_allow_width_us = 20,
	.subvp_swath_height_margin_lines = 16,
};

static void patch_ip_caps_with_explicit_ip_params(struct dml2_ip_capabilities *ip_caps, const struct dml2_core_ip_params *ip_params)
{
	ip_caps->pipe_count = ip_params->max_num_dpp;
	ip_caps->otg_count = ip_params->max_num_otg;
	ip_caps->num_dsc = ip_params->num_dsc;
	ip_caps->max_num_dp2p0_streams = ip_params->max_num_dp2p0_streams;
	ip_caps->max_num_dp2p0_outputs = ip_params->max_num_dp2p0_outputs;
	ip_caps->max_num_hdmi_frl_outputs = ip_params->max_num_hdmi_frl_outputs;
	ip_caps->rob_buffer_size_kbytes = ip_params->rob_buffer_size_kbytes;
	ip_caps->config_return_buffer_size_in_kbytes = ip_params->config_return_buffer_size_in_kbytes;
	ip_caps->config_return_buffer_segment_size_in_kbytes = ip_params->config_return_buffer_segment_size_in_kbytes;
	ip_caps->meta_fifo_size_in_kentries = ip_params->meta_fifo_size_in_kentries;
	ip_caps->compressed_buffer_segment_size_in_kbytes = ip_params->compressed_buffer_segment_size_in_kbytes;
	ip_caps->cursor_buffer_size = ip_params->cursor_buffer_size;
	ip_caps->max_flip_time_us = ip_params->max_flip_time_us;
	ip_caps->max_flip_time_lines = ip_params->max_flip_time_lines;
	ip_caps->hostvm_mode = ip_params->hostvm_mode;

	// FIXME_STAGE2: cleanup after adding all dv override to ip_caps
	ip_caps->subvp_drr_scheduling_margin_us = 100;
	ip_caps->subvp_prefetch_end_to_mall_start_us = 15;
	ip_caps->subvp_fw_processing_delay = 16;

}

static void patch_ip_params_with_ip_caps(struct dml2_core_ip_params *ip_params, const struct dml2_ip_capabilities *ip_caps)
{
	ip_params->max_num_dpp = ip_caps->pipe_count;
	ip_params->max_num_otg = ip_caps->otg_count;
	ip_params->num_dsc = ip_caps->num_dsc;
	ip_params->max_num_dp2p0_streams = ip_caps->max_num_dp2p0_streams;
	ip_params->max_num_dp2p0_outputs = ip_caps->max_num_dp2p0_outputs;
	ip_params->max_num_hdmi_frl_outputs = ip_caps->max_num_hdmi_frl_outputs;
	ip_params->rob_buffer_size_kbytes = ip_caps->rob_buffer_size_kbytes;
	ip_params->config_return_buffer_size_in_kbytes = ip_caps->config_return_buffer_size_in_kbytes;
	ip_params->config_return_buffer_segment_size_in_kbytes = ip_caps->config_return_buffer_segment_size_in_kbytes;
	ip_params->meta_fifo_size_in_kentries = ip_caps->meta_fifo_size_in_kentries;
	ip_params->compressed_buffer_segment_size_in_kbytes = ip_caps->compressed_buffer_segment_size_in_kbytes;
	ip_params->cursor_buffer_size = ip_caps->cursor_buffer_size;
	ip_params->max_flip_time_us = ip_caps->max_flip_time_us;
	ip_params->max_flip_time_lines = ip_caps->max_flip_time_lines;
	ip_params->hostvm_mode = ip_caps->hostvm_mode;
}

bool core_dcn4_initialize(struct dml2_core_initialize_in_out *in_out)
{
	struct dml2_core_instance *core = in_out->instance;

	if (!in_out->minimum_clock_table)
		return false;
	else
		core->minimum_clock_table = in_out->minimum_clock_table;

	if (in_out->explicit_ip_bb && in_out->explicit_ip_bb_size > 0) {
		memcpy(&core->clean_me_up.mode_lib.ip, in_out->explicit_ip_bb, in_out->explicit_ip_bb_size);

		// FIXME_STAGE2:
		// DV still uses stage1 ip_param_st for each variant, need to patch the ip_caps with ip_param info
		// Should move DV to use ip_caps but need move more overrides to ip_caps
		patch_ip_caps_with_explicit_ip_params(in_out->ip_caps, in_out->explicit_ip_bb);
		core->clean_me_up.mode_lib.ip.subvp_pstate_allow_width_us = core_dcn4_ip_caps_base.subvp_pstate_allow_width_us;
		core->clean_me_up.mode_lib.ip.subvp_fw_processing_delay_us = core_dcn4_ip_caps_base.subvp_pstate_allow_width_us;
		core->clean_me_up.mode_lib.ip.subvp_swath_height_margin_lines = core_dcn4_ip_caps_base.subvp_swath_height_margin_lines;
	} else {
		memcpy(&core->clean_me_up.mode_lib.ip, &core_dcn4_ip_caps_base, sizeof(struct dml2_core_ip_params));
		patch_ip_params_with_ip_caps(&core->clean_me_up.mode_lib.ip, in_out->ip_caps);
		core->clean_me_up.mode_lib.ip.imall_supported = false;
	}

	memcpy(&core->clean_me_up.mode_lib.soc, in_out->soc_bb, sizeof(struct dml2_soc_bb));
	memcpy(&core->clean_me_up.mode_lib.ip_caps, in_out->ip_caps, sizeof(struct dml2_ip_capabilities));

	return true;
}

static void create_phantom_stream_from_main_stream(struct dml2_stream_parameters *phantom, const struct dml2_stream_parameters *main,
	const struct dml2_implicit_svp_meta *meta)
{
	memcpy(phantom, main, sizeof(struct dml2_stream_parameters));

	phantom->timing.v_total = meta->v_total;
	phantom->timing.v_active = meta->v_active;
	phantom->timing.v_front_porch = meta->v_front_porch;
	phantom->timing.v_blank_end = phantom->timing.v_total - phantom->timing.v_front_porch - phantom->timing.v_active;
	phantom->timing.vblank_nom = phantom->timing.v_total - phantom->timing.v_active;
	phantom->timing.drr_config.enabled = false;
}

static void create_phantom_plane_from_main_plane(struct dml2_plane_parameters *phantom, const struct dml2_plane_parameters *main,
	const struct dml2_stream_parameters *phantom_stream, int phantom_stream_index, const struct dml2_stream_parameters *main_stream)
{
	memcpy(phantom, main, sizeof(struct dml2_plane_parameters));

	phantom->stream_index = phantom_stream_index;
	phantom->overrides.refresh_from_mall = dml2_refresh_from_mall_mode_override_force_disable;
	phantom->overrides.legacy_svp_config = dml2_svp_mode_override_phantom_pipe_no_data_return;
	phantom->composition.viewport.plane0.height = (long int unsigned) math_min2(math_ceil2(
		(double)main->composition.scaler_info.plane0.v_ratio * (double)phantom_stream->timing.v_active, 16.0),
		(double)main->composition.viewport.plane0.height);
	phantom->composition.viewport.plane1.height = (long int unsigned) math_min2(math_ceil2(
		(double)main->composition.scaler_info.plane1.v_ratio * (double)phantom_stream->timing.v_active, 16.0),
		(double)main->composition.viewport.plane1.height);
	phantom->immediate_flip = false;
	phantom->dynamic_meta_data.enable = false;
	phantom->cursor.num_cursors = 0;
	phantom->cursor.cursor_width = 0;
	phantom->tdlut.setup_for_tdlut = false;
}

static void expand_implict_subvp(const struct display_configuation_with_meta *display_cfg, struct dml2_display_cfg *svp_expanded_display_cfg,
	struct dml2_core_scratch *scratch)
{
	unsigned int stream_index, plane_index;
	const struct dml2_plane_parameters *main_plane;
	const struct dml2_stream_parameters *main_stream;
	const struct dml2_stream_parameters *phantom_stream;

	memcpy(svp_expanded_display_cfg, &display_cfg->display_config, sizeof(struct dml2_display_cfg));
	memset(scratch->main_stream_index_from_svp_stream_index, 0, sizeof(int) * DML2_MAX_PLANES);
	memset(scratch->svp_stream_index_from_main_stream_index, 0, sizeof(int) * DML2_MAX_PLANES);
	memset(scratch->main_plane_index_to_phantom_plane_index, 0, sizeof(int) * DML2_MAX_PLANES);

	if (!display_cfg->display_config.overrides.enable_subvp_implicit_pmo)
		return;

	/* disable unbounded requesting for all planes until stage 3 has been performed */
	if (!display_cfg->stage3.performed) {
		svp_expanded_display_cfg->overrides.hw.force_unbounded_requesting.enable = true;
		svp_expanded_display_cfg->overrides.hw.force_unbounded_requesting.value = false;
	}
	// Create the phantom streams
	for (stream_index = 0; stream_index < display_cfg->display_config.num_streams; stream_index++) {
		main_stream = &display_cfg->display_config.stream_descriptors[stream_index];
		scratch->main_stream_index_from_svp_stream_index[stream_index] = stream_index;
		scratch->svp_stream_index_from_main_stream_index[stream_index] = stream_index;

		if (display_cfg->stage3.stream_svp_meta[stream_index].valid) {
			// Create the phantom stream
			create_phantom_stream_from_main_stream(&svp_expanded_display_cfg->stream_descriptors[svp_expanded_display_cfg->num_streams],
				main_stream, &display_cfg->stage3.stream_svp_meta[stream_index]);

			// Associate this phantom stream to the main stream
			scratch->main_stream_index_from_svp_stream_index[svp_expanded_display_cfg->num_streams] = stream_index;
			scratch->svp_stream_index_from_main_stream_index[stream_index] = svp_expanded_display_cfg->num_streams;

			// Increment num streams
			svp_expanded_display_cfg->num_streams++;
		}
	}

	// Create the phantom planes
	for (plane_index = 0; plane_index < display_cfg->display_config.num_planes; plane_index++) {
		main_plane = &display_cfg->display_config.plane_descriptors[plane_index];

		if (display_cfg->stage3.stream_svp_meta[main_plane->stream_index].valid) {
			main_stream = &display_cfg->display_config.stream_descriptors[main_plane->stream_index];
			phantom_stream = &svp_expanded_display_cfg->stream_descriptors[scratch->svp_stream_index_from_main_stream_index[main_plane->stream_index]];
			create_phantom_plane_from_main_plane(&svp_expanded_display_cfg->plane_descriptors[svp_expanded_display_cfg->num_planes],
				main_plane, phantom_stream, scratch->svp_stream_index_from_main_stream_index[main_plane->stream_index], main_stream);

			// Associate this phantom plane to the main plane
			scratch->phantom_plane_index_to_main_plane_index[svp_expanded_display_cfg->num_planes] = plane_index;
			scratch->main_plane_index_to_phantom_plane_index[plane_index] = svp_expanded_display_cfg->num_planes;

			// Increment num planes
			svp_expanded_display_cfg->num_planes++;

			// Adjust the main plane settings
			svp_expanded_display_cfg->plane_descriptors[plane_index].overrides.legacy_svp_config = dml2_svp_mode_override_main_pipe;
		}
	}
}

static void pack_mode_programming_params_with_implicit_subvp(struct dml2_core_instance *core, const struct display_configuation_with_meta *display_cfg,
	const struct dml2_display_cfg *svp_expanded_display_cfg, struct dml2_display_cfg_programming *programming, struct dml2_core_scratch *scratch)
{
	unsigned int stream_index, plane_index, pipe_offset, stream_already_populated_mask, main_plane_index, mcache_index;
	unsigned int total_main_mcaches_required = 0;
	int total_pipe_regs_copied = 0;
	int dml_internal_pipe_index = 0;
	const struct dml2_plane_parameters *main_plane;
	const struct dml2_plane_parameters *phantom_plane;
	const struct dml2_stream_parameters *main_stream;
	const struct dml2_stream_parameters *phantom_stream;

	// Copy the unexpanded display config to output
	memcpy(&programming->display_config, &display_cfg->display_config, sizeof(struct dml2_display_cfg));

	// Set the global register values
	dml2_core_calcs_get_arb_params(&display_cfg->display_config, &core->clean_me_up.mode_lib, &programming->global_regs.arb_regs);
	// Get watermarks uses display config for ref clock override, so it doesn't matter whether we pass the pre or post expansion
	// display config
	dml2_core_calcs_get_watermarks(&display_cfg->display_config, &core->clean_me_up.mode_lib, &programming->global_regs.wm_regs[0]);

	// Check if FAMS2 is required
	if (display_cfg->stage3.performed && display_cfg->stage3.success) {
		programming->fams2_required = display_cfg->stage3.fams2_required;

		dml2_core_calcs_get_global_fams2_programming(&core->clean_me_up.mode_lib, display_cfg, &programming->fams2_global_config);
	}

	// Only loop over all the main streams (the implicit svp streams will be packed as part of the main stream)
	for (stream_index = 0; stream_index < programming->display_config.num_streams; stream_index++) {
		main_stream = &svp_expanded_display_cfg->stream_descriptors[stream_index];
		phantom_stream = &svp_expanded_display_cfg->stream_descriptors[scratch->svp_stream_index_from_main_stream_index[stream_index]];

		// Set the descriptor
		programming->stream_programming[stream_index].stream_descriptor = &programming->display_config.stream_descriptors[stream_index];

		// Set the odm combine factor
		programming->stream_programming[stream_index].num_odms_required = display_cfg->mode_support_result.cfg_support_info.stream_support_info[stream_index].odms_used;

		// Check if the stream has implicit SVP enabled
		if (main_stream != phantom_stream) {
			// If so, copy the phantom stream descriptor
			programming->stream_programming[stream_index].phantom_stream.enabled = true;
			memcpy(&programming->stream_programming[stream_index].phantom_stream.descriptor, phantom_stream, sizeof(struct dml2_stream_parameters));
		} else {
			programming->stream_programming[stream_index].phantom_stream.enabled = false;
		}

		// Due to the way DML indexes data internally, it's easier to populate the rest of the display
		// stream programming in the next stage
	}

	dml_internal_pipe_index = 0;
	total_pipe_regs_copied = 0;
	stream_already_populated_mask = 0x0;

	// Loop over all main planes
	for (plane_index = 0; plane_index < programming->display_config.num_planes; plane_index++) {
		main_plane = &svp_expanded_display_cfg->plane_descriptors[plane_index];

		// Set the descriptor
		programming->plane_programming[plane_index].plane_descriptor = &programming->display_config.plane_descriptors[plane_index];

		// Set the mpc combine factor
		programming->plane_programming[plane_index].num_dpps_required = core->clean_me_up.mode_lib.mp.NoOfDPP[plane_index];

		// Setup the appropriate p-state strategy
		if (display_cfg->stage3.performed && display_cfg->stage3.success) {
			programming->plane_programming[plane_index].uclk_pstate_support_method = display_cfg->stage3.pstate_switch_modes[plane_index];
		} else {
			programming->plane_programming[plane_index].uclk_pstate_support_method = dml2_pstate_method_na;
		}

		dml2_core_calcs_get_mall_allocation(&core->clean_me_up.mode_lib, &programming->plane_programming[plane_index].surface_size_mall_bytes, dml_internal_pipe_index);

		memcpy(&programming->plane_programming[plane_index].mcache_allocation,
			&display_cfg->stage2.mcache_allocations[plane_index],
			sizeof(struct dml2_mcache_surface_allocation));
		total_main_mcaches_required += programming->plane_programming[plane_index].mcache_allocation.num_mcaches_plane0 +
			programming->plane_programming[plane_index].mcache_allocation.num_mcaches_plane1 -
			(programming->plane_programming[plane_index].mcache_allocation.last_slice_sharing.plane0_plane1 ? 1 : 0);

		for (pipe_offset = 0; pipe_offset < programming->plane_programming[plane_index].num_dpps_required; pipe_offset++) {
			// Assign storage for this pipe's register values
			programming->plane_programming[plane_index].pipe_regs[pipe_offset] = &programming->pipe_regs[total_pipe_regs_copied];
			memset(programming->plane_programming[plane_index].pipe_regs[pipe_offset], 0, sizeof(struct dml2_dchub_per_pipe_register_set));
			total_pipe_regs_copied++;

			// Populate the main plane regs
			dml2_core_calcs_get_pipe_regs(svp_expanded_display_cfg, &core->clean_me_up.mode_lib, programming->plane_programming[plane_index].pipe_regs[pipe_offset], dml_internal_pipe_index);

			// Multiple planes can refer to the same stream index, so it's only necessary to populate it once
			if (!(stream_already_populated_mask & (0x1 << main_plane->stream_index))) {
				dml2_core_calcs_get_stream_programming(&core->clean_me_up.mode_lib, &programming->stream_programming[main_plane->stream_index], dml_internal_pipe_index);

				programming->stream_programming[main_plane->stream_index].uclk_pstate_method = programming->plane_programming[plane_index].uclk_pstate_support_method;

				/* unconditionally populate fams2 params */
				dml2_core_calcs_get_stream_fams2_programming(&core->clean_me_up.mode_lib,
					display_cfg,
					&programming->stream_programming[main_plane->stream_index].fams2_base_params,
					&programming->stream_programming[main_plane->stream_index].fams2_sub_params,
					programming->stream_programming[main_plane->stream_index].uclk_pstate_method,
					plane_index);

				stream_already_populated_mask |= (0x1 << main_plane->stream_index);
			}
			dml_internal_pipe_index++;
		}
	}

	for (plane_index = programming->display_config.num_planes; plane_index < svp_expanded_display_cfg->num_planes; plane_index++) {
		phantom_plane = &svp_expanded_display_cfg->plane_descriptors[plane_index];
		main_plane_index = scratch->phantom_plane_index_to_main_plane_index[plane_index];
		main_plane = &svp_expanded_display_cfg->plane_descriptors[main_plane_index];

		programming->plane_programming[main_plane_index].phantom_plane.valid = true;
		memcpy(&programming->plane_programming[main_plane_index].phantom_plane.descriptor, phantom_plane, sizeof(struct dml2_plane_parameters));

		dml2_core_calcs_get_mall_allocation(&core->clean_me_up.mode_lib, &programming->plane_programming[main_plane_index].svp_size_mall_bytes, dml_internal_pipe_index);

		/* generate mcache allocation, phantoms use identical mcache configuration, but in the MALL set and unique mcache ID's beginning after all main ID's */
		memcpy(&programming->plane_programming[main_plane_index].phantom_plane.mcache_allocation,
			&programming->plane_programming[main_plane_index].mcache_allocation,
			sizeof(struct dml2_mcache_surface_allocation));
		for (mcache_index = 0; mcache_index < programming->plane_programming[main_plane_index].phantom_plane.mcache_allocation.num_mcaches_plane0; mcache_index++) {
			programming->plane_programming[main_plane_index].phantom_plane.mcache_allocation.global_mcache_ids_plane0[mcache_index] += total_main_mcaches_required;
			programming->plane_programming[main_plane_index].phantom_plane.mcache_allocation.global_mcache_ids_mall_plane0[mcache_index] =
				programming->plane_programming[main_plane_index].phantom_plane.mcache_allocation.global_mcache_ids_plane0[mcache_index];
		}
		for (mcache_index = 0; mcache_index < programming->plane_programming[main_plane_index].phantom_plane.mcache_allocation.num_mcaches_plane1; mcache_index++) {
			programming->plane_programming[main_plane_index].phantom_plane.mcache_allocation.global_mcache_ids_plane1[mcache_index] += total_main_mcaches_required;
			programming->plane_programming[main_plane_index].phantom_plane.mcache_allocation.global_mcache_ids_mall_plane1[mcache_index] =
				programming->plane_programming[main_plane_index].phantom_plane.mcache_allocation.global_mcache_ids_plane1[mcache_index];
		}

		for (pipe_offset = 0; pipe_offset < programming->plane_programming[main_plane_index].num_dpps_required; pipe_offset++) {
			// Assign storage for this pipe's register values
			programming->plane_programming[main_plane_index].phantom_plane.pipe_regs[pipe_offset] = &programming->pipe_regs[total_pipe_regs_copied];
			memset(programming->plane_programming[main_plane_index].phantom_plane.pipe_regs[pipe_offset], 0, sizeof(struct dml2_dchub_per_pipe_register_set));
			total_pipe_regs_copied++;

			// Populate the phantom plane regs
			dml2_core_calcs_get_pipe_regs(svp_expanded_display_cfg, &core->clean_me_up.mode_lib, programming->plane_programming[main_plane_index].phantom_plane.pipe_regs[pipe_offset], dml_internal_pipe_index);
			// Populate the phantom stream specific programming
			if (!(stream_already_populated_mask & (0x1 << phantom_plane->stream_index))) {
				dml2_core_calcs_get_global_sync_programming(&core->clean_me_up.mode_lib, &programming->stream_programming[main_plane->stream_index].phantom_stream.global_sync, dml_internal_pipe_index);

				stream_already_populated_mask |= (0x1 << phantom_plane->stream_index);
			}

			dml_internal_pipe_index++;
		}
	}
}

bool core_dcn4_mode_support(struct dml2_core_mode_support_in_out *in_out)
{
	struct dml2_core_instance *core = (struct dml2_core_instance *)in_out->instance;
	struct dml2_core_mode_support_locals *l = &core->scratch.mode_support_locals;

	bool result;
	unsigned int i, stream_index, stream_bitmask;
	int unsigned odm_count, num_odm_output_segments, dpp_count;

	expand_implict_subvp(in_out->display_cfg, &l->svp_expanded_display_cfg, &core->scratch);

	l->mode_support_ex_params.mode_lib = &core->clean_me_up.mode_lib;
	l->mode_support_ex_params.in_display_cfg = &l->svp_expanded_display_cfg;
	l->mode_support_ex_params.min_clk_table = in_out->min_clk_table;
	l->mode_support_ex_params.min_clk_index = in_out->min_clk_index;
	l->mode_support_ex_params.out_evaluation_info = &in_out->mode_support_result.cfg_support_info.clean_me_up.support_info;

	result = dml2_core_calcs_mode_support_ex(&l->mode_support_ex_params);

	in_out->mode_support_result.cfg_support_info.is_supported = result;

	if (result) {
		in_out->mode_support_result.global.dispclk_khz = (unsigned int)(core->clean_me_up.mode_lib.ms.RequiredDISPCLK * 1000);
		in_out->mode_support_result.global.dcfclk_deepsleep_khz = (unsigned int)(core->clean_me_up.mode_lib.ms.dcfclk_deepsleep * 1000);
		in_out->mode_support_result.global.socclk_khz = (unsigned int)(core->clean_me_up.mode_lib.ms.SOCCLK * 1000);

		in_out->mode_support_result.global.fclk_pstate_supported = l->mode_support_ex_params.out_evaluation_info->global_fclk_change_supported;
		in_out->mode_support_result.global.uclk_pstate_supported = l->mode_support_ex_params.out_evaluation_info->global_dram_clock_change_supported;

		in_out->mode_support_result.global.active.fclk_khz = (unsigned long)(core->clean_me_up.mode_lib.ms.FabricClock * 1000);
		in_out->mode_support_result.global.active.dcfclk_khz = (unsigned long)(core->clean_me_up.mode_lib.ms.DCFCLK * 1000);


		in_out->mode_support_result.global.svp_prefetch.fclk_khz = (unsigned long)core->clean_me_up.mode_lib.ms.FabricClock * 1000;
		in_out->mode_support_result.global.svp_prefetch.dcfclk_khz = (unsigned long)core->clean_me_up.mode_lib.ms.DCFCLK * 1000;

		in_out->mode_support_result.global.active.average_bw_sdp_kbps = 0;
		in_out->mode_support_result.global.active.urgent_bw_dram_kbps = 0;
		in_out->mode_support_result.global.svp_prefetch.average_bw_sdp_kbps = 0;
		in_out->mode_support_result.global.svp_prefetch.urgent_bw_dram_kbps = 0;

		in_out->mode_support_result.global.active.average_bw_sdp_kbps = (unsigned long)math_ceil2((l->mode_support_ex_params.out_evaluation_info->avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp] * 1000), 1.0);
		in_out->mode_support_result.global.active.urgent_bw_sdp_kbps = (unsigned long)math_ceil2((l->mode_support_ex_params.out_evaluation_info->urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp] * 1000), 1.0);
		in_out->mode_support_result.global.svp_prefetch.average_bw_sdp_kbps = (unsigned long)math_ceil2((l->mode_support_ex_params.out_evaluation_info->avg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp] * 1000), 1.0);
		in_out->mode_support_result.global.svp_prefetch.urgent_bw_sdp_kbps = (unsigned long)math_ceil2((l->mode_support_ex_params.out_evaluation_info->urg_bandwidth_required_flip[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp] * 1000), 1.0);

		in_out->mode_support_result.global.active.average_bw_dram_kbps = (unsigned long)math_ceil2((l->mode_support_ex_params.out_evaluation_info->avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram] * 1000), 1.0);
		in_out->mode_support_result.global.active.urgent_bw_dram_kbps = (unsigned long)math_ceil2((l->mode_support_ex_params.out_evaluation_info->urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram] * 1000), 1.0);
		in_out->mode_support_result.global.svp_prefetch.average_bw_dram_kbps = (unsigned long)math_ceil2((l->mode_support_ex_params.out_evaluation_info->avg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram] * 1000), 1.0);
		in_out->mode_support_result.global.svp_prefetch.urgent_bw_dram_kbps = (unsigned long)math_ceil2((l->mode_support_ex_params.out_evaluation_info->urg_bandwidth_required_flip[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram] * 1000), 1.0);
		dml2_printf("DML::%s: in_out->mode_support_result.global.active.urgent_bw_sdp_kbps = %ld\n", __func__, in_out->mode_support_result.global.active.urgent_bw_sdp_kbps);
		dml2_printf("DML::%s: in_out->mode_support_result.global.svp_prefetch.urgent_bw_sdp_kbps = %ld\n", __func__, in_out->mode_support_result.global.svp_prefetch.urgent_bw_sdp_kbps);
		dml2_printf("DML::%s: in_out->mode_support_result.global.active.urgent_bw_dram_kbps = %ld\n", __func__, in_out->mode_support_result.global.active.urgent_bw_dram_kbps);
		dml2_printf("DML::%s: in_out->mode_support_result.global.svp_prefetch.urgent_bw_dram_kbps = %ld\n", __func__, in_out->mode_support_result.global.svp_prefetch.urgent_bw_dram_kbps);

		for (i = 0; i < l->svp_expanded_display_cfg.num_planes; i++) {
			in_out->mode_support_result.per_plane[i].dppclk_khz = (unsigned int)(core->clean_me_up.mode_lib.ms.RequiredDPPCLK[i] * 1000);
		}

		stream_bitmask = 0;
		for (i = 0; i < l->svp_expanded_display_cfg.num_planes; i++) {
			odm_count = 1;
			dpp_count = l->mode_support_ex_params.out_evaluation_info->DPPPerSurface[i];
			num_odm_output_segments = 1;

			switch (l->mode_support_ex_params.out_evaluation_info->ODMMode[i]) {
			case dml2_odm_mode_bypass:
				odm_count = 1;
				dpp_count = l->mode_support_ex_params.out_evaluation_info->DPPPerSurface[i];
				break;
			case dml2_odm_mode_combine_2to1:
				odm_count = 2;
				dpp_count = 2;
				break;
			case dml2_odm_mode_combine_3to1:
				odm_count = 3;
				dpp_count = 3;
				break;
			case dml2_odm_mode_combine_4to1:
				odm_count = 4;
				dpp_count = 4;
				break;
			case dml2_odm_mode_split_1to2:
			case dml2_odm_mode_mso_1to2:
				num_odm_output_segments = 2;
				break;
			case dml2_odm_mode_mso_1to4:
				num_odm_output_segments = 4;
				break;
			case dml2_odm_mode_auto:
			default:
				odm_count = 1;
				dpp_count = l->mode_support_ex_params.out_evaluation_info->DPPPerSurface[i];
				break;
			}

			in_out->mode_support_result.cfg_support_info.plane_support_info[i].dpps_used = dpp_count;

			dml2_core_calcs_get_plane_support_info(&l->svp_expanded_display_cfg, &core->clean_me_up.mode_lib, &in_out->mode_support_result.cfg_support_info.plane_support_info[i], i);

			stream_index = l->svp_expanded_display_cfg.plane_descriptors[i].stream_index;

			in_out->mode_support_result.per_stream[stream_index].dscclk_khz = (unsigned int)core->clean_me_up.mode_lib.ms.required_dscclk_freq_mhz[i] * 1000;
			dml2_printf("CORE_DCN4::%s: i=%d stream_index=%d, in_out->mode_support_result.per_stream[stream_index].dscclk_khz = %u\n", __func__, i, stream_index, in_out->mode_support_result.per_stream[stream_index].dscclk_khz);

			if (!((stream_bitmask >> stream_index) & 0x1)) {
				in_out->mode_support_result.cfg_support_info.stream_support_info[stream_index].odms_used = odm_count;
				in_out->mode_support_result.cfg_support_info.stream_support_info[stream_index].num_odm_output_segments = num_odm_output_segments;
				in_out->mode_support_result.cfg_support_info.stream_support_info[stream_index].dsc_enable = l->mode_support_ex_params.out_evaluation_info->DSCEnabled[i];
				in_out->mode_support_result.cfg_support_info.stream_support_info[stream_index].num_dsc_slices = l->mode_support_ex_params.out_evaluation_info->NumberOfDSCSlices[i];
				dml2_core_calcs_get_stream_support_info(&l->svp_expanded_display_cfg, &core->clean_me_up.mode_lib, &in_out->mode_support_result.cfg_support_info.stream_support_info[stream_index], i);
				in_out->mode_support_result.per_stream[stream_index].dtbclk_khz = (unsigned int)(core->clean_me_up.mode_lib.ms.RequiredDTBCLK[i] * 1000);
				stream_bitmask |= 0x1 << stream_index;
			}
		}
	}

	return result;
}

static int lookup_uclk_dpm_index_by_freq(unsigned long uclk_freq_khz, struct dml2_soc_bb *soc_bb)
{
	int i;

	for (i = 0; i < soc_bb->clk_table.uclk.num_clk_values; i++) {
		if (uclk_freq_khz == soc_bb->clk_table.uclk.clk_values_khz[i])
			return i;
	}
	return 0;
}

bool core_dcn4_mode_programming(struct dml2_core_mode_programming_in_out *in_out)
{
	struct dml2_core_instance *core = (struct dml2_core_instance *)in_out->instance;
	struct dml2_core_mode_programming_locals *l = &core->scratch.mode_programming_locals;

	bool result = false;
	unsigned int pipe_offset;
	int dml_internal_pipe_index;
	int total_pipe_regs_copied = 0;
	int stream_already_populated_mask = 0;

	int main_stream_index;
	unsigned int plane_index;

	expand_implict_subvp(in_out->display_cfg, &l->svp_expanded_display_cfg, &core->scratch);

	l->mode_programming_ex_params.mode_lib = &core->clean_me_up.mode_lib;
	l->mode_programming_ex_params.in_display_cfg = &l->svp_expanded_display_cfg;
	l->mode_programming_ex_params.min_clk_table = in_out->instance->minimum_clock_table;
	l->mode_programming_ex_params.cfg_support_info = in_out->cfg_support_info;
	l->mode_programming_ex_params.programming = in_out->programming;
	l->mode_programming_ex_params.min_clk_index = lookup_uclk_dpm_index_by_freq(in_out->programming->min_clocks.dcn4x.active.uclk_khz,
		&core->clean_me_up.mode_lib.soc);

	result = dml2_core_calcs_mode_programming_ex(&l->mode_programming_ex_params);

	if (result) {
		// If the input display configuration contains implict SVP, we need to use a special packer
		if (in_out->display_cfg->display_config.overrides.enable_subvp_implicit_pmo) {
			pack_mode_programming_params_with_implicit_subvp(core, in_out->display_cfg, &l->svp_expanded_display_cfg, in_out->programming, &core->scratch);
		} else {
			memcpy(&in_out->programming->display_config, in_out->display_cfg, sizeof(struct dml2_display_cfg));

			dml2_core_calcs_get_arb_params(&l->svp_expanded_display_cfg, &core->clean_me_up.mode_lib, &in_out->programming->global_regs.arb_regs);
			dml2_core_calcs_get_watermarks(&l->svp_expanded_display_cfg, &core->clean_me_up.mode_lib, &in_out->programming->global_regs.wm_regs[0]);

			dml_internal_pipe_index = 0;

			for (plane_index = 0; plane_index < in_out->programming->display_config.num_planes; plane_index++) {
				in_out->programming->plane_programming[plane_index].num_dpps_required = core->clean_me_up.mode_lib.mp.NoOfDPP[plane_index];

				if (in_out->programming->display_config.plane_descriptors[plane_index].overrides.legacy_svp_config == dml2_svp_mode_override_main_pipe)
					in_out->programming->plane_programming[plane_index].uclk_pstate_support_method = dml2_pstate_method_fw_svp;
				else if (in_out->programming->display_config.plane_descriptors[plane_index].overrides.legacy_svp_config == dml2_svp_mode_override_phantom_pipe)
					in_out->programming->plane_programming[plane_index].uclk_pstate_support_method = dml2_pstate_method_fw_svp;
				else if (in_out->programming->display_config.plane_descriptors[plane_index].overrides.legacy_svp_config == dml2_svp_mode_override_phantom_pipe_no_data_return)
					in_out->programming->plane_programming[plane_index].uclk_pstate_support_method = dml2_pstate_method_fw_svp;
				else {
					if (core->clean_me_up.mode_lib.mp.MaxActiveDRAMClockChangeLatencySupported[plane_index] >= core->clean_me_up.mode_lib.soc.power_management_parameters.dram_clk_change_blackout_us)
						in_out->programming->plane_programming[plane_index].uclk_pstate_support_method = dml2_pstate_method_vactive;
					else if (core->clean_me_up.mode_lib.mp.TWait[plane_index] >= core->clean_me_up.mode_lib.soc.power_management_parameters.dram_clk_change_blackout_us)
						in_out->programming->plane_programming[plane_index].uclk_pstate_support_method = dml2_pstate_method_vblank;
					else
						in_out->programming->plane_programming[plane_index].uclk_pstate_support_method = dml2_pstate_method_na;
				}

				dml2_core_calcs_get_mall_allocation(&core->clean_me_up.mode_lib, &in_out->programming->plane_programming[plane_index].surface_size_mall_bytes, dml_internal_pipe_index);

				memcpy(&in_out->programming->plane_programming[plane_index].mcache_allocation,
					&in_out->display_cfg->stage2.mcache_allocations[plane_index],
					sizeof(struct dml2_mcache_surface_allocation));

				for (pipe_offset = 0; pipe_offset < in_out->programming->plane_programming[plane_index].num_dpps_required; pipe_offset++) {
					in_out->programming->plane_programming[plane_index].plane_descriptor = &in_out->programming->display_config.plane_descriptors[plane_index];

					// Assign storage for this pipe's register values
					in_out->programming->plane_programming[plane_index].pipe_regs[pipe_offset] = &in_out->programming->pipe_regs[total_pipe_regs_copied];
					memset(in_out->programming->plane_programming[plane_index].pipe_regs[pipe_offset], 0, sizeof(struct dml2_dchub_per_pipe_register_set));
					total_pipe_regs_copied++;

					// Populate
					dml2_core_calcs_get_pipe_regs(&l->svp_expanded_display_cfg, &core->clean_me_up.mode_lib, in_out->programming->plane_programming[plane_index].pipe_regs[pipe_offset], dml_internal_pipe_index);

					main_stream_index = in_out->programming->display_config.plane_descriptors[plane_index].stream_index;

					// Multiple planes can refer to the same stream index, so it's only necessary to populate it once
					if (!(stream_already_populated_mask & (0x1 << main_stream_index))) {
						in_out->programming->stream_programming[main_stream_index].stream_descriptor = &in_out->programming->display_config.stream_descriptors[main_stream_index];
						in_out->programming->stream_programming[main_stream_index].num_odms_required = in_out->cfg_support_info->stream_support_info[main_stream_index].odms_used;
						dml2_core_calcs_get_stream_programming(&core->clean_me_up.mode_lib, &in_out->programming->stream_programming[main_stream_index], dml_internal_pipe_index);

						stream_already_populated_mask |= (0x1 << main_stream_index);
					}
					dml_internal_pipe_index++;
				}
			}
		}
	}

	return result;
}

bool core_dcn4_populate_informative(struct dml2_core_populate_informative_in_out *in_out)
{
	struct dml2_core_internal_display_mode_lib *mode_lib = &in_out->instance->clean_me_up.mode_lib;

	if (in_out->mode_is_supported)
		in_out->programming->informative.voltage_level = in_out->instance->scratch.mode_programming_locals.mode_programming_ex_params.min_clk_index;
	else
		in_out->programming->informative.voltage_level = in_out->instance->scratch.mode_support_locals.mode_support_ex_params.min_clk_index;

	dml2_core_calcs_get_informative(mode_lib, in_out->programming);
	return true;
}

bool core_dcn4_calculate_mcache_allocation(struct dml2_calculate_mcache_allocation_in_out *in_out)
{
	memset(in_out->mcache_allocation, 0, sizeof(struct dml2_mcache_surface_allocation));

	dml2_core_calcs_get_mcache_allocation(&in_out->instance->clean_me_up.mode_lib, in_out->mcache_allocation, in_out->plane_index);

	if (in_out->mcache_allocation->num_mcaches_plane0 > 0)
		in_out->mcache_allocation->mcache_x_offsets_plane0[in_out->mcache_allocation->num_mcaches_plane0 - 1] = in_out->plane_descriptor->surface.plane0.width;

	if (in_out->mcache_allocation->num_mcaches_plane1 > 0)
		in_out->mcache_allocation->mcache_x_offsets_plane1[in_out->mcache_allocation->num_mcaches_plane1 - 1] = in_out->plane_descriptor->surface.plane1.width;

	in_out->mcache_allocation->requires_dedicated_mall_mcache = false;

	return true;
}
