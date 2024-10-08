// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_core_utils.h"

double dml2_core_utils_div_rem(double dividend, unsigned int divisor, unsigned int *remainder)
{
	*remainder = ((dividend / divisor) - (int)(dividend / divisor) > 0);
	return dividend / divisor;

}

const char *dml2_core_utils_internal_bw_type_str(enum dml2_core_internal_bw_type bw_type)
{
	switch (bw_type) {
	case (dml2_core_internal_bw_sdp):
		return("dml2_core_internal_bw_sdp");
	case (dml2_core_internal_bw_dram):
		return("dml2_core_internal_bw_dram");
	case (dml2_core_internal_bw_max):
		return("dml2_core_internal_bw_max");
	default:
		return("dml2_core_internal_bw_unknown");
	}
}

bool dml2_core_utils_is_420(enum dml2_source_format_class source_format)
{
	bool val = false;

	switch (source_format) {
	case dml2_444_8:
		val = 0;
		break;
	case dml2_444_16:
		val = 0;
		break;
	case dml2_444_32:
		val = 0;
		break;
	case dml2_444_64:
		val = 0;
		break;
	case dml2_420_8:
		val = 1;
		break;
	case dml2_420_10:
		val = 1;
		break;
	case dml2_420_12:
		val = 1;
		break;
	case dml2_rgbe_alpha:
		val = 0;
		break;
	case dml2_rgbe:
		val = 0;
		break;
	case dml2_mono_8:
		val = 0;
		break;
	case dml2_mono_16:
		val = 0;
		break;
	default:
		DML2_ASSERT(0);
		break;
	}
	return val;
}

void dml2_core_utils_print_mode_support_info(const struct dml2_core_internal_mode_support_info *support, bool fail_only)
{
	dml2_printf("DML: ===================================== \n");
	dml2_printf("DML: DML_MODE_SUPPORT_INFO_ST\n");
	if (!fail_only || support->ScaleRatioAndTapsSupport == 0)
		dml2_printf("DML: support: ScaleRatioAndTapsSupport = %d\n", support->ScaleRatioAndTapsSupport);
	if (!fail_only || support->SourceFormatPixelAndScanSupport == 0)
		dml2_printf("DML: support: SourceFormatPixelAndScanSupport = %d\n", support->SourceFormatPixelAndScanSupport);
	if (!fail_only || support->ViewportSizeSupport == 0)
		dml2_printf("DML: support: ViewportSizeSupport = %d\n", support->ViewportSizeSupport);
	if (!fail_only || support->LinkRateDoesNotMatchDPVersion == 1)
		dml2_printf("DML: support: LinkRateDoesNotMatchDPVersion = %d\n", support->LinkRateDoesNotMatchDPVersion);
	if (!fail_only || support->LinkRateForMultistreamNotIndicated == 1)
		dml2_printf("DML: support: LinkRateForMultistreamNotIndicated = %d\n", support->LinkRateForMultistreamNotIndicated);
	if (!fail_only || support->BPPForMultistreamNotIndicated == 1)
		dml2_printf("DML: support: BPPForMultistreamNotIndicated = %d\n", support->BPPForMultistreamNotIndicated);
	if (!fail_only || support->MultistreamWithHDMIOreDP == 1)
		dml2_printf("DML: support: MultistreamWithHDMIOreDP = %d\n", support->MultistreamWithHDMIOreDP);
	if (!fail_only || support->ExceededMultistreamSlots == 1)
		dml2_printf("DML: support: ExceededMultistreamSlots = %d\n", support->ExceededMultistreamSlots);
	if (!fail_only || support->MSOOrODMSplitWithNonDPLink == 1)
		dml2_printf("DML: support: MSOOrODMSplitWithNonDPLink = %d\n", support->MSOOrODMSplitWithNonDPLink);
	if (!fail_only || support->NotEnoughLanesForMSO == 1)
		dml2_printf("DML: support: NotEnoughLanesForMSO = %d\n", support->NotEnoughLanesForMSO);
	if (!fail_only || support->P2IWith420 == 1)
		dml2_printf("DML: support: P2IWith420 = %d\n", support->P2IWith420);
	if (!fail_only || support->DSC422NativeNotSupported == 1)
		dml2_printf("DML: support: DSC422NativeNotSupported = %d\n", support->DSC422NativeNotSupported);
	if (!fail_only || support->DSCSlicesODMModeSupported == 0)
		dml2_printf("DML: support: DSCSlicesODMModeSupported = %d\n", support->DSCSlicesODMModeSupported);
	if (!fail_only || support->NotEnoughDSCUnits == 1)
		dml2_printf("DML: support: NotEnoughDSCUnits = %d\n", support->NotEnoughDSCUnits);
	if (!fail_only || support->NotEnoughDSCSlices == 1)
		dml2_printf("DML: support: NotEnoughDSCSlices = %d\n", support->NotEnoughDSCSlices);
	if (!fail_only || support->ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe == 1)
		dml2_printf("DML: support: ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe = %d\n", support->ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe);
	if (!fail_only || support->InvalidCombinationOfMALLUseForPStateAndStaticScreen == 1)
		dml2_printf("DML: support: InvalidCombinationOfMALLUseForPStateAndStaticScreen = %d\n", support->InvalidCombinationOfMALLUseForPStateAndStaticScreen);
	if (!fail_only || support->DSCCLKRequiredMoreThanSupported == 1)
		dml2_printf("DML: support: DSCCLKRequiredMoreThanSupported = %d\n", support->DSCCLKRequiredMoreThanSupported);
	if (!fail_only || support->PixelsPerLinePerDSCUnitSupport == 0)
		dml2_printf("DML: support: PixelsPerLinePerDSCUnitSupport = %d\n", support->PixelsPerLinePerDSCUnitSupport);
	if (!fail_only || support->DTBCLKRequiredMoreThanSupported == 1)
		dml2_printf("DML: support: DTBCLKRequiredMoreThanSupported = %d\n", support->DTBCLKRequiredMoreThanSupported);
	if (!fail_only || support->InvalidCombinationOfMALLUseForPState == 1)
		dml2_printf("DML: support: InvalidCombinationOfMALLUseForPState = %d\n", support->InvalidCombinationOfMALLUseForPState);
	if (!fail_only || support->ROBSupport == 0)
		dml2_printf("DML: support: ROBSupport = %d\n", support->ROBSupport);
	if (!fail_only || support->OutstandingRequestsSupport == 0)
		dml2_printf("DML: support: OutstandingRequestsSupport = %d\n", support->OutstandingRequestsSupport);
	if (!fail_only || support->OutstandingRequestsUrgencyAvoidance == 0)
		dml2_printf("DML: support: OutstandingRequestsUrgencyAvoidance = %d\n", support->OutstandingRequestsUrgencyAvoidance);
	if (!fail_only || support->DISPCLK_DPPCLK_Support == 0)
		dml2_printf("DML: support: DISPCLK_DPPCLK_Support = %d\n", support->DISPCLK_DPPCLK_Support);
	if (!fail_only || support->TotalAvailablePipesSupport == 0)
		dml2_printf("DML: support: TotalAvailablePipesSupport = %d\n", support->TotalAvailablePipesSupport);
	if (!fail_only || support->NumberOfOTGSupport == 0)
		dml2_printf("DML: support: NumberOfOTGSupport = %d\n", support->NumberOfOTGSupport);
	if (!fail_only || support->NumberOfHDMIFRLSupport == 0)
		dml2_printf("DML: support: NumberOfHDMIFRLSupport = %d\n", support->NumberOfHDMIFRLSupport);
	if (!fail_only || support->NumberOfDP2p0Support == 0)
		dml2_printf("DML: support: NumberOfDP2p0Support = %d\n", support->NumberOfDP2p0Support);
	if (!fail_only || support->EnoughWritebackUnits == 0)
		dml2_printf("DML: support: EnoughWritebackUnits = %d\n", support->EnoughWritebackUnits);
	if (!fail_only || support->WritebackScaleRatioAndTapsSupport == 0)
		dml2_printf("DML: support: WritebackScaleRatioAndTapsSupport = %d\n", support->WritebackScaleRatioAndTapsSupport);
	if (!fail_only || support->WritebackLatencySupport == 0)
		dml2_printf("DML: support: WritebackLatencySupport = %d\n", support->WritebackLatencySupport);
	if (!fail_only || support->CursorSupport == 0)
		dml2_printf("DML: support: CursorSupport = %d\n", support->CursorSupport);
	if (!fail_only || support->PitchSupport == 0)
		dml2_printf("DML: support: PitchSupport = %d\n", support->PitchSupport);
	if (!fail_only || support->ViewportExceedsSurface == 1)
		dml2_printf("DML: support: ViewportExceedsSurface = %d\n", support->ViewportExceedsSurface);
	if (!fail_only || support->PrefetchSupported == 0)
		dml2_printf("DML: support: PrefetchSupported = %d\n", support->PrefetchSupported);
	if (!fail_only || support->EnoughUrgentLatencyHidingSupport == 0)
		dml2_printf("DML: support: EnoughUrgentLatencyHidingSupport = %d\n", support->EnoughUrgentLatencyHidingSupport);
	if (!fail_only || support->AvgBandwidthSupport == 0)
		dml2_printf("DML: support: AvgBandwidthSupport = %d\n", support->AvgBandwidthSupport);
	if (!fail_only || support->DynamicMetadataSupported == 0)
		dml2_printf("DML: support: DynamicMetadataSupported = %d\n", support->DynamicMetadataSupported);
	if (!fail_only || support->VRatioInPrefetchSupported == 0)
		dml2_printf("DML: support: VRatioInPrefetchSupported = %d\n", support->VRatioInPrefetchSupported);
	if (!fail_only || support->PTEBufferSizeNotExceeded == 1)
		dml2_printf("DML: support: PTEBufferSizeNotExceeded = %d\n", support->PTEBufferSizeNotExceeded);
	if (!fail_only || support->DCCMetaBufferSizeNotExceeded == 1)
		dml2_printf("DML: support: DCCMetaBufferSizeNotExceeded = %d\n", support->DCCMetaBufferSizeNotExceeded);
	if (!fail_only || support->ExceededMALLSize == 1)
		dml2_printf("DML: support: ExceededMALLSize = %d\n", support->ExceededMALLSize);
	if (!fail_only || support->g6_temp_read_support == 0)
		dml2_printf("DML: support: g6_temp_read_support = %d\n", support->g6_temp_read_support);
	if (!fail_only || support->ImmediateFlipSupport == 0)
		dml2_printf("DML: support: ImmediateFlipSupport = %d\n", support->ImmediateFlipSupport);
	if (!fail_only || support->LinkCapacitySupport == 0)
		dml2_printf("DML: support: LinkCapacitySupport = %d\n", support->LinkCapacitySupport);

	if (!fail_only || support->ModeSupport == 0)
		dml2_printf("DML: support: ModeSupport = %d\n", support->ModeSupport);
	dml2_printf("DML: ===================================== \n");
}

const char *dml2_core_utils_internal_soc_state_type_str(enum dml2_core_internal_soc_state_type dml2_core_internal_soc_state_type)
{
	switch (dml2_core_internal_soc_state_type) {
	case (dml2_core_internal_soc_state_sys_idle):
		return("dml2_core_internal_soc_state_sys_idle");
	case (dml2_core_internal_soc_state_sys_active):
		return("dml2_core_internal_soc_state_sys_active");
	case (dml2_core_internal_soc_state_svp_prefetch):
		return("dml2_core_internal_soc_state_svp_prefetch");
	case dml2_core_internal_soc_state_max:
	default:
		return("dml2_core_internal_soc_state_unknown");
	}
}


void dml2_core_utils_get_stream_output_bpp(double *out_bpp, const struct dml2_display_cfg *display_cfg)
{
	for (unsigned int k = 0; k < display_cfg->num_planes; k++) {
		double bpc = (double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.bpc;
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.enable == dml2_dsc_disable) {
			switch (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format) {
			case dml2_444:
				out_bpp[k] = bpc * 3;
				break;
			case dml2_s422:
				out_bpp[k] = bpc * 2;
				break;
			case dml2_n422:
				out_bpp[k] = bpc * 2;
				break;
			case dml2_420:
			default:
				out_bpp[k] = bpc * 1.5;
				break;
			}
		} else if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.enable == dml2_dsc_enable) {
			out_bpp[k] = (double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.dsc_compressed_bpp_x16 / 16;
		} else {
			out_bpp[k] = 0;
		}
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%d bpc=%f\n", __func__, k, bpc);
		dml2_printf("DML::%s: k=%d dsc.enable=%d\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.enable);
		dml2_printf("DML::%s: k=%d out_bpp=%f\n", __func__, k, out_bpp[k]);
#endif
	}
}

unsigned int dml2_core_utils_round_to_multiple(unsigned int num, unsigned int multiple, bool up)
{
	unsigned int remainder;

	if (multiple == 0)
		return num;

	remainder = num % multiple;
	if (remainder == 0)
		return num;

	if (up)
		return (num + multiple - remainder);
	else
		return (num - remainder);
}

unsigned int dml2_core_util_get_num_active_pipes(int unsigned num_planes, const struct core_display_cfg_support_info *cfg_support_info)
{
	unsigned int num_active_pipes = 0;

	for (unsigned int k = 0; k < num_planes; k++) {
		num_active_pipes = num_active_pipes + (unsigned int)cfg_support_info->plane_support_info[k].dpps_used;
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: num_active_pipes = %d\n", __func__, num_active_pipes);
#endif
	return num_active_pipes;
}

void dml2_core_utils_pipe_plane_mapping(const struct core_display_cfg_support_info *cfg_support_info, unsigned int *pipe_plane)
{
	unsigned int pipe_idx = 0;

	for (unsigned int k = 0; k < DML2_MAX_PLANES; ++k) {
		pipe_plane[k] = __DML2_CALCS_PIPE_NO_PLANE__;
	}

	for (unsigned int plane_idx = 0; plane_idx < DML2_MAX_PLANES; plane_idx++) {
		for (int i = 0; i < cfg_support_info->plane_support_info[plane_idx].dpps_used; i++) {
			pipe_plane[pipe_idx] = plane_idx;
			pipe_idx++;
		}
	}
}

bool dml2_core_utils_is_phantom_pipe(const struct dml2_plane_parameters *plane_cfg)
{
	bool is_phantom = false;

	if (plane_cfg->overrides.legacy_svp_config == dml2_svp_mode_override_phantom_pipe ||
		plane_cfg->overrides.legacy_svp_config == dml2_svp_mode_override_phantom_pipe_no_data_return) {
		is_phantom = true;
	}

	return is_phantom;
}

unsigned int dml2_core_utils_get_tile_block_size_bytes(enum dml2_swizzle_mode sw_mode)
{
	switch (sw_mode) {
	case (dml2_sw_linear):
		return 256; break;
	case (dml2_sw_256b_2d):
		return 256; break;
	case (dml2_sw_4kb_2d):
		return 4096; break;
	case (dml2_sw_64kb_2d):
		return 65536; break;
	case (dml2_sw_256kb_2d):
		return 262144; break;
	case (dml2_gfx11_sw_linear):
		return 256; break;
	case (dml2_gfx11_sw_64kb_d):
		return 65536; break;
	case (dml2_gfx11_sw_64kb_d_t):
		return 65536; break;
	case (dml2_gfx11_sw_64kb_d_x):
		return 65536; break;
	case (dml2_gfx11_sw_64kb_r_x):
		return 65536; break;
	case (dml2_gfx11_sw_256kb_d_x):
		return 262144; break;
	case (dml2_gfx11_sw_256kb_r_x):
		return 262144; break;
	default:
		DML2_ASSERT(0);
		return 256;
	};
}


bool dml2_core_utils_is_vertical_rotation(enum dml2_rotation_angle Scan)
{
	bool is_vert = false;
	if (Scan == dml2_rotation_90 || Scan == dml2_rotation_270) {
		is_vert = true;
	} else {
		is_vert = false;
	}
	return is_vert;
}


int unsigned dml2_core_utils_get_gfx_version(enum dml2_swizzle_mode sw_mode)
{
	int unsigned version = 0;

	if (sw_mode == dml2_sw_linear ||
		sw_mode == dml2_sw_256b_2d ||
		sw_mode == dml2_sw_4kb_2d ||
		sw_mode == dml2_sw_64kb_2d ||
		sw_mode == dml2_sw_256kb_2d) {
		version = 12;
	} else if (sw_mode == dml2_gfx11_sw_linear ||
		sw_mode == dml2_gfx11_sw_64kb_d ||
		sw_mode == dml2_gfx11_sw_64kb_d_t ||
		sw_mode == dml2_gfx11_sw_64kb_d_x ||
		sw_mode == dml2_gfx11_sw_64kb_r_x ||
		sw_mode == dml2_gfx11_sw_256kb_d_x ||
		sw_mode == dml2_gfx11_sw_256kb_r_x) {
		version = 11;
	} else {
		dml2_printf("ERROR: Invalid sw_mode setting! val=%u\n", sw_mode);
		DML2_ASSERT(0);
	}

	return version;
}

unsigned int dml2_core_utils_get_qos_param_index(unsigned long uclk_freq_khz, const struct dml2_dcn4_uclk_dpm_dependent_qos_params *per_uclk_dpm_params)
{
	unsigned int i;
	unsigned int index = 0;

	for (i = 0; i < DML_MAX_CLK_TABLE_SIZE; i++) {
		dml2_printf("DML::%s: per_uclk_dpm_params[%d].minimum_uclk_khz = %d\n", __func__, i, per_uclk_dpm_params[i].minimum_uclk_khz);

		if (i == 0)
			index = 0;
		else
			index = i - 1;

		if (uclk_freq_khz < per_uclk_dpm_params[i].minimum_uclk_khz ||
			per_uclk_dpm_params[i].minimum_uclk_khz == 0) {
			break;
		}
	}
#if defined(__DML_VBA_DEBUG__)
	dml2_printf("DML::%s: uclk_freq_khz = %d\n", __func__, uclk_freq_khz);
	dml2_printf("DML::%s: index = %d\n", __func__, index);
#endif
	return index;
}

unsigned int dml2_core_utils_get_active_min_uclk_dpm_index(unsigned long uclk_freq_khz, const struct dml2_soc_state_table *clk_table)
{
	unsigned int i;
	bool clk_entry_found = 0;

	for (i = 0; i < clk_table->uclk.num_clk_values; i++) {
		dml2_printf("DML::%s: clk_table.uclk.clk_values_khz[%d] = %d\n", __func__, i, clk_table->uclk.clk_values_khz[i]);

		if (uclk_freq_khz == clk_table->uclk.clk_values_khz[i]) {
			clk_entry_found = 1;
			break;
		}
	}

	dml2_assert(clk_entry_found);
#if defined(__DML_VBA_DEBUG__)
	dml2_printf("DML::%s: uclk_freq_khz = %ld\n", __func__, uclk_freq_khz);
	dml2_printf("DML::%s: index = %d\n", __func__, i);
#endif
	return i;
}

bool dml2_core_utils_is_dual_plane(enum dml2_source_format_class source_format)
{
	bool ret_val = 0;

	if ((source_format == dml2_420_12) || (source_format == dml2_420_8) || (source_format == dml2_420_10) || (source_format == dml2_rgbe_alpha))
		ret_val = 1;

	return ret_val;
}

unsigned int dml2_core_utils_log_and_substract_if_non_zero(unsigned int a, unsigned int subtrahend)
{
	if (a == 0)
		return 0;

	return (math_log2_approx(a) - subtrahend);
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

void dml2_core_utils_expand_implict_subvp(const struct display_configuation_with_meta *display_cfg, struct dml2_display_cfg *svp_expanded_display_cfg,
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

bool dml2_core_utils_is_stream_encoder_required(const struct dml2_stream_parameters *stream_descriptor)
{
	switch (stream_descriptor->output.output_encoder) {
	case dml2_dp:
	case dml2_dp2p0:
	case dml2_edp:
	case dml2_hdmi:
	case dml2_hdmifrl:
		return true;
	case dml2_none:
	default:
		return false;
	}
}
bool dml2_core_utils_is_encoder_dsc_capable(const struct dml2_stream_parameters *stream_descriptor)
{
	switch (stream_descriptor->output.output_encoder) {
	case dml2_dp:
	case dml2_dp2p0:
	case dml2_edp:
	case dml2_hdmifrl:
		return true;
	case dml2_hdmi:
	case dml2_none:
	default:
		return false;
	}
}


bool dml2_core_utils_is_dio_dp_encoder(const struct dml2_stream_parameters *stream_descriptor)
{
	switch (stream_descriptor->output.output_encoder) {
	case dml2_dp:
	case dml2_edp:
		return true;
	case dml2_dp2p0:
	case dml2_hdmi:
	case dml2_hdmifrl:
	case dml2_none:
	default:
		return false;
	}
}

bool dml2_core_utils_is_hpo_dp_encoder(const struct dml2_stream_parameters *stream_descriptor)
{
	switch (stream_descriptor->output.output_encoder) {
	case dml2_dp2p0:
		return true;
	case dml2_dp:
	case dml2_edp:
	case dml2_hdmi:
	case dml2_hdmifrl:
	case dml2_none:
	default:
		return false;
	}
}

bool dml2_core_utils_is_dp_encoder(const struct dml2_stream_parameters *stream_descriptor)
{
	return dml2_core_utils_is_dio_dp_encoder(stream_descriptor)
			|| dml2_core_utils_is_hpo_dp_encoder(stream_descriptor);
}


bool dml2_core_utils_is_dp_8b_10b_link_rate(enum dml2_output_link_dp_rate rate)
{
	switch (rate) {
	case dml2_dp_rate_hbr:
	case dml2_dp_rate_hbr2:
	case dml2_dp_rate_hbr3:
		return true;
	case dml2_dp_rate_na:
	case dml2_dp_rate_uhbr10:
	case dml2_dp_rate_uhbr13p5:
	case dml2_dp_rate_uhbr20:
	default:
		return false;
	}
}

bool dml2_core_utils_is_dp_128b_132b_link_rate(enum dml2_output_link_dp_rate rate)
{
	switch (rate) {
	case dml2_dp_rate_uhbr10:
	case dml2_dp_rate_uhbr13p5:
	case dml2_dp_rate_uhbr20:
		return true;
	case dml2_dp_rate_hbr:
	case dml2_dp_rate_hbr2:
	case dml2_dp_rate_hbr3:
	case dml2_dp_rate_na:
	default:
		return false;
	}
}

bool dml2_core_utils_is_odm_split(enum dml2_odm_mode odm_mode)
{
	switch (odm_mode) {
	case dml2_odm_mode_split_1to2:
	case dml2_odm_mode_mso_1to2:
	case dml2_odm_mode_mso_1to4:
		return true;
	case dml2_odm_mode_auto:
	case dml2_odm_mode_bypass:
	case dml2_odm_mode_combine_2to1:
	case dml2_odm_mode_combine_3to1:
	case dml2_odm_mode_combine_4to1:
	default:
		return false;
	}
}
