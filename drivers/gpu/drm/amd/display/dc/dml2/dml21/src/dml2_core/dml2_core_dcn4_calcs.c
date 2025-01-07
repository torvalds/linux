// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#include "dml2_internal_shared_types.h"
#include "dml2_core_dcn4_calcs.h"
#include "dml2_debug.h"
#include "lib_float_math.h"
#include "dml_top_types.h"

#define DML2_MAX_FMT_420_BUFFER_WIDTH 4096
#define DML_MAX_NUM_OF_SLICES_PER_DSC 4
#define DML_MAX_COMPRESSION_RATIO 4
//#define DML_MODE_SUPPORT_USE_DPM_DRAM_BW
//#define DML_GLOBAL_PREFETCH_CHECK
#define ALLOW_SDPIF_RATE_LIMIT_PRE_CSTATE

const char *dml2_core_internal_bw_type_str(enum dml2_core_internal_bw_type bw_type)
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

const char *dml2_core_internal_soc_state_type_str(enum dml2_core_internal_soc_state_type dml2_core_internal_soc_state_type)
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

static double dml2_core_div_rem(double dividend, unsigned int divisor, unsigned int *remainder)
{
	*remainder = ((dividend / divisor) - (int)(dividend / divisor) > 0);
	return dividend / divisor;
}

static void dml2_print_mode_support_info(const struct dml2_core_internal_mode_support_info *support, bool fail_only)
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
	if (!fail_only || support->PTEBufferSizeNotExceeded == 0)
		dml2_printf("DML: support: PTEBufferSizeNotExceeded = %d\n", support->PTEBufferSizeNotExceeded);
	if (!fail_only || support->DCCMetaBufferSizeNotExceeded == 0)
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

static void get_stream_output_bpp(double *out_bpp, const struct dml2_display_cfg *display_cfg)
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

static unsigned int dml_round_to_multiple(unsigned int num, unsigned int multiple, bool up)
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

static unsigned int dml_get_num_active_pipes(int unsigned num_planes, const struct core_display_cfg_support_info *cfg_support_info)
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

static void dml_calc_pipe_plane_mapping(const struct core_display_cfg_support_info *cfg_support_info, unsigned int *pipe_plane)
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

static bool dml_is_phantom_pipe(const struct dml2_plane_parameters *plane_cfg)
{
	bool is_phantom = false;

	if (plane_cfg->overrides.legacy_svp_config == dml2_svp_mode_override_phantom_pipe ||
		plane_cfg->overrides.legacy_svp_config == dml2_svp_mode_override_phantom_pipe_no_data_return) {
		is_phantom = true;
	}

	return is_phantom;
}

static bool dml_get_is_phantom_pipe(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, unsigned int pipe_idx)
{
	unsigned int plane_idx = mode_lib->mp.pipe_plane[pipe_idx];

	bool is_phantom = dml_is_phantom_pipe(&display_cfg->plane_descriptors[plane_idx]);
	dml2_printf("DML::%s: pipe_idx=%d legacy_svp_config=%0d is_phantom=%d\n", __func__, pipe_idx, display_cfg->plane_descriptors[plane_idx].overrides.legacy_svp_config, is_phantom);
	return is_phantom;
}

#define dml_get_per_pipe_var_func(variable, type, interval_var) static type dml_get_##variable(const struct dml2_core_internal_display_mode_lib *mode_lib, unsigned int pipe_idx) \
{ \
unsigned int plane_idx; \
plane_idx = mode_lib->mp.pipe_plane[pipe_idx]; \
return (type) interval_var[plane_idx]; \
}

dml_get_per_pipe_var_func(dpte_group_size_in_bytes, unsigned int, mode_lib->mp.dpte_group_bytes);
dml_get_per_pipe_var_func(vm_group_size_in_bytes, unsigned int, mode_lib->mp.vm_group_bytes);
dml_get_per_pipe_var_func(swath_height_l, unsigned int, mode_lib->mp.SwathHeightY);
dml_get_per_pipe_var_func(swath_height_c, unsigned int, mode_lib->mp.SwathHeightC);
dml_get_per_pipe_var_func(dpte_row_height_linear_l, unsigned int, mode_lib->mp.dpte_row_height_linear);
dml_get_per_pipe_var_func(dpte_row_height_linear_c, unsigned int, mode_lib->mp.dpte_row_height_linear_chroma);

dml_get_per_pipe_var_func(vstartup_calculated, unsigned int, mode_lib->mp.VStartup);
dml_get_per_pipe_var_func(vupdate_offset, unsigned int, mode_lib->mp.VUpdateOffsetPix);
dml_get_per_pipe_var_func(vupdate_width, unsigned int, mode_lib->mp.VUpdateWidthPix);
dml_get_per_pipe_var_func(vready_offset, unsigned int, mode_lib->mp.VReadyOffsetPix);
dml_get_per_pipe_var_func(pstate_keepout_dst_lines, unsigned int, mode_lib->mp.pstate_keepout_dst_lines);
dml_get_per_pipe_var_func(det_stored_buffer_size_l_bytes, unsigned int, mode_lib->mp.DETBufferSizeY);
dml_get_per_pipe_var_func(det_stored_buffer_size_c_bytes, unsigned int, mode_lib->mp.DETBufferSizeC);
dml_get_per_pipe_var_func(det_buffer_size_kbytes, unsigned int, mode_lib->mp.DETBufferSizeInKByte);
dml_get_per_pipe_var_func(surface_size_in_mall_bytes, unsigned int, mode_lib->mp.SurfaceSizeInTheMALL);

#define dml_get_per_plane_var_func(variable, type, interval_var) static type dml_get_plane_##variable(const struct dml2_core_internal_display_mode_lib *mode_lib, unsigned int plane_idx) \
{ \
return (type) interval_var[plane_idx]; \
}

dml_get_per_plane_var_func(num_mcaches_plane0, unsigned int, mode_lib->ms.num_mcaches_l);
dml_get_per_plane_var_func(mcache_row_bytes_plane0, unsigned int, mode_lib->ms.mcache_row_bytes_l);
dml_get_per_plane_var_func(mcache_shift_granularity_plane0, unsigned int, mode_lib->ms.mcache_shift_granularity_l);
dml_get_per_plane_var_func(num_mcaches_plane1, unsigned int, mode_lib->ms.num_mcaches_c);
dml_get_per_plane_var_func(mcache_row_bytes_plane1, unsigned int, mode_lib->ms.mcache_row_bytes_c);
dml_get_per_plane_var_func(mcache_shift_granularity_plane1, unsigned int, mode_lib->ms.mcache_shift_granularity_c);
dml_get_per_plane_var_func(mall_comb_mcache_l, unsigned int, mode_lib->ms.mall_comb_mcache_l);
dml_get_per_plane_var_func(mall_comb_mcache_c, unsigned int, mode_lib->ms.mall_comb_mcache_c);
dml_get_per_plane_var_func(lc_comb_mcache, unsigned int, mode_lib->ms.lc_comb_mcache);
dml_get_per_plane_var_func(subviewport_lines_needed_in_mall, unsigned int, mode_lib->ms.SubViewportLinesNeededInMALL);
dml_get_per_plane_var_func(max_vstartup_lines, unsigned int, mode_lib->ms.MaxVStartupLines);

#define dml_get_per_plane_array_var_func(variable, type, interval_var) static type dml_get_plane_array_##variable(const struct dml2_core_internal_display_mode_lib *mode_lib, unsigned int plane_idx, unsigned int array_idx) \
{ \
return (type) interval_var[plane_idx][array_idx]; \
}

dml_get_per_plane_array_var_func(mcache_offsets_plane0, unsigned int, mode_lib->ms.mcache_offsets_l);
dml_get_per_plane_array_var_func(mcache_offsets_plane1, unsigned int, mode_lib->ms.mcache_offsets_c);

#define dml_get_var_func(var, type, internal_var) static type dml_get_##var(const struct dml2_core_internal_display_mode_lib *mode_lib) \
{ \
return (type) internal_var; \
}

dml_get_var_func(wm_urgent, double, mode_lib->mp.Watermark.UrgentWatermark);
dml_get_var_func(wm_stutter_exit, double, mode_lib->mp.Watermark.StutterExitWatermark);
dml_get_var_func(wm_stutter_enter_exit, double, mode_lib->mp.Watermark.StutterEnterPlusExitWatermark);
dml_get_var_func(wm_z8_stutter_exit, double, mode_lib->mp.Watermark.Z8StutterExitWatermark);
dml_get_var_func(wm_z8_stutter_enter_exit, double, mode_lib->mp.Watermark.Z8StutterEnterPlusExitWatermark);
dml_get_var_func(wm_memory_trip, double, mode_lib->mp.UrgentLatency);
dml_get_var_func(meta_trip_memory_us, double, mode_lib->mp.MetaTripToMemory);

dml_get_var_func(wm_fclk_change, double, mode_lib->mp.Watermark.FCLKChangeWatermark);
dml_get_var_func(wm_usr_retraining, double, mode_lib->mp.Watermark.USRRetrainingWatermark);
dml_get_var_func(wm_temp_read_or_ppt, double, mode_lib->mp.Watermark.temp_read_or_ppt_watermark_us);
dml_get_var_func(wm_dram_clock_change, double, mode_lib->mp.Watermark.DRAMClockChangeWatermark);
dml_get_var_func(fraction_of_urgent_bandwidth, double, mode_lib->mp.FractionOfUrgentBandwidth);
dml_get_var_func(fraction_of_urgent_bandwidth_imm_flip, double, mode_lib->mp.FractionOfUrgentBandwidthImmediateFlip);
dml_get_var_func(fraction_of_urgent_bandwidth_mall, double, mode_lib->mp.FractionOfUrgentBandwidthMALL);
dml_get_var_func(wm_writeback_dram_clock_change, double, mode_lib->mp.Watermark.WritebackDRAMClockChangeWatermark);
dml_get_var_func(wm_writeback_fclk_change, double, mode_lib->mp.Watermark.WritebackFCLKChangeWatermark);
dml_get_var_func(stutter_efficiency, double, mode_lib->mp.StutterEfficiency);
dml_get_var_func(stutter_efficiency_no_vblank, double, mode_lib->mp.StutterEfficiencyNotIncludingVBlank);
dml_get_var_func(stutter_num_bursts, double, mode_lib->mp.NumberOfStutterBurstsPerFrame);
dml_get_var_func(stutter_efficiency_z8, double, mode_lib->mp.Z8StutterEfficiency);
dml_get_var_func(stutter_num_bursts_z8, double, mode_lib->mp.Z8NumberOfStutterBurstsPerFrame);
dml_get_var_func(stutter_period, double, mode_lib->mp.StutterPeriod);
dml_get_var_func(stutter_efficiency_z8_bestcase, double, mode_lib->mp.Z8StutterEfficiencyBestCase);
dml_get_var_func(stutter_num_bursts_z8_bestcase, double, mode_lib->mp.Z8NumberOfStutterBurstsPerFrameBestCase);
dml_get_var_func(stutter_period_bestcase, double, mode_lib->mp.StutterPeriodBestCase);
dml_get_var_func(fclk_change_latency, double, mode_lib->mp.MaxActiveFCLKChangeLatencySupported);
dml_get_var_func(global_dppclk_khz, double, mode_lib->mp.GlobalDPPCLK * 1000.0);

dml_get_var_func(sys_active_avg_bw_required_sdp, double, mode_lib->ms.support.avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]);
dml_get_var_func(sys_active_avg_bw_required_dram, double, mode_lib->ms.support.avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram]);

dml_get_var_func(svp_prefetch_avg_bw_required_sdp, double, mode_lib->ms.support.avg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp]);
dml_get_var_func(svp_prefetch_avg_bw_required_dram, double, mode_lib->ms.support.avg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram]);

dml_get_var_func(sys_active_avg_bw_available_sdp, double, mode_lib->mp.avg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]);
dml_get_var_func(sys_active_avg_bw_available_dram, double, mode_lib->mp.avg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram]);

dml_get_var_func(svp_prefetch_avg_bw_available_sdp, double, mode_lib->mp.avg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp]);
dml_get_var_func(svp_prefetch_avg_bw_available_dram, double, mode_lib->mp.avg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram]);

dml_get_var_func(sys_active_urg_bw_available_sdp, double, mode_lib->mp.urg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]);
dml_get_var_func(sys_active_urg_bw_available_dram, double, mode_lib->mp.urg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram]);
dml_get_var_func(sys_active_urg_bw_available_dram_vm_only, double, mode_lib->mp.urg_bandwidth_available_vm_only[dml2_core_internal_soc_state_sys_active]);

dml_get_var_func(svp_prefetch_urg_bw_available_sdp, double, mode_lib->mp.urg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp]);
dml_get_var_func(svp_prefetch_urg_bw_available_dram, double, mode_lib->mp.urg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram]);
dml_get_var_func(svp_prefetch_urg_bw_available_dram_vm_only, double, mode_lib->mp.urg_bandwidth_available_vm_only[dml2_core_internal_soc_state_svp_prefetch]);

dml_get_var_func(urgent_latency, double, mode_lib->mp.UrgentLatency);
dml_get_var_func(max_urgent_latency_us, double, mode_lib->ms.support.max_urgent_latency_us);
dml_get_var_func(max_non_urgent_latency_us, double, mode_lib->ms.support.max_non_urgent_latency_us);
dml_get_var_func(avg_non_urgent_latency_us, double, mode_lib->ms.support.avg_non_urgent_latency_us);
dml_get_var_func(avg_urgent_latency_us, double, mode_lib->ms.support.avg_urgent_latency_us);

dml_get_var_func(sys_active_urg_bw_required_sdp, double, mode_lib->mp.urg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]);
dml_get_var_func(sys_active_urg_bw_required_dram, double, mode_lib->mp.urg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram]);
dml_get_var_func(svp_prefetch_urg_bw_required_sdp, double, mode_lib->mp.urg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp]);
dml_get_var_func(svp_prefetch_urg_bw_required_dram, double, mode_lib->mp.urg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram]);

dml_get_var_func(sys_active_non_urg_required_sdp, double, mode_lib->mp.non_urg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]);
dml_get_var_func(sys_active_non_urg_required_dram, double, mode_lib->mp.non_urg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram]);
dml_get_var_func(svp_prefetch_non_urg_bw_required_sdp, double, mode_lib->mp.non_urg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp]);
dml_get_var_func(svp_prefetch_non_urg_bw_required_dram, double, mode_lib->mp.non_urg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram]);

dml_get_var_func(sys_active_urg_bw_required_sdp_flip, double, mode_lib->mp.urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]);
dml_get_var_func(sys_active_urg_bw_required_dram_flip, double, mode_lib->mp.urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram]);
dml_get_var_func(svp_prefetch_urg_bw_required_sdp_flip, double, mode_lib->mp.urg_bandwidth_required_flip[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp]);
dml_get_var_func(svp_prefetch_urg_bw_required_dram_flip, double, mode_lib->mp.urg_bandwidth_required_flip[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram]);

dml_get_var_func(sys_active_non_urg_required_sdp_flip, double, mode_lib->mp.non_urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]);
dml_get_var_func(sys_active_non_urg_required_dram_flip, double, mode_lib->mp.non_urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram]);
dml_get_var_func(svp_prefetch_non_urg_bw_required_sdp_flip, double, mode_lib->mp.non_urg_bandwidth_required_flip[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp]);
dml_get_var_func(svp_prefetch_non_urg_bw_required_dram_flip, double, mode_lib->mp.non_urg_bandwidth_required_flip[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram]);

dml_get_var_func(comp_buffer_size_kbytes, unsigned int, mode_lib->mp.CompressedBufferSizeInkByte);

dml_get_var_func(unbounded_request_enabled, bool, mode_lib->mp.UnboundedRequestEnabled);
dml_get_var_func(wm_writeback_urgent, double, mode_lib->mp.Watermark.WritebackUrgentWatermark);
dml_get_var_func(cstate_max_cap_mode, bool, mode_lib->mp.DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE);
dml_get_var_func(compbuf_reserved_space_64b, unsigned int, mode_lib->mp.compbuf_reserved_space_64b);
dml_get_var_func(hw_debug5, bool, mode_lib->mp.hw_debug5);
dml_get_var_func(dcfclk_deep_sleep_hysteresis, unsigned int, mode_lib->mp.dcfclk_deep_sleep_hysteresis);

static void CalculateMaxDETAndMinCompressedBufferSize(
	unsigned int ConfigReturnBufferSizeInKByte,
	unsigned int ConfigReturnBufferSegmentSizeInKByte,
	unsigned int ROBBufferSizeInKByte,
	unsigned int MaxNumDPP,
	unsigned int nomDETInKByteOverrideEnable, // VBA_DELTA, allow DV to override default DET size
	unsigned int nomDETInKByteOverrideValue, // VBA_DELTA
	bool is_mrq_present,

	// Output
	unsigned int *MaxTotalDETInKByte,
	unsigned int *nomDETInKByte,
	unsigned int *MinCompressedBufferSizeInKByte)
{
	if (is_mrq_present)
		*MaxTotalDETInKByte = (unsigned int) math_ceil2((double)(ConfigReturnBufferSizeInKByte + ROBBufferSizeInKByte)*4/5, 64);
	else
		*MaxTotalDETInKByte = ConfigReturnBufferSizeInKByte - ConfigReturnBufferSegmentSizeInKByte;

	*nomDETInKByte = (unsigned int)(math_floor2((double)*MaxTotalDETInKByte / (double)MaxNumDPP, ConfigReturnBufferSegmentSizeInKByte));
	*MinCompressedBufferSizeInKByte = ConfigReturnBufferSizeInKByte - *MaxTotalDETInKByte;

#if defined(__DML_VBA_DEBUG__)
	dml2_printf("DML::%s: is_mrq_present = %u\n", __func__, is_mrq_present);
	dml2_printf("DML::%s: ConfigReturnBufferSizeInKByte = %u\n", __func__, ConfigReturnBufferSizeInKByte);
	dml2_printf("DML::%s: ROBBufferSizeInKByte = %u\n", __func__, ROBBufferSizeInKByte);
	dml2_printf("DML::%s: MaxNumDPP = %u\n", __func__, MaxNumDPP);
	dml2_printf("DML::%s: MaxTotalDETInKByte = %u\n", __func__, *MaxTotalDETInKByte);
	dml2_printf("DML::%s: nomDETInKByte = %u\n", __func__, *nomDETInKByte);
	dml2_printf("DML::%s: MinCompressedBufferSizeInKByte = %u\n", __func__, *MinCompressedBufferSizeInKByte);
#endif

	if (nomDETInKByteOverrideEnable) {
		*nomDETInKByte = nomDETInKByteOverrideValue;
		dml2_printf("DML::%s: nomDETInKByte = %u (overrided)\n", __func__, *nomDETInKByte);
	}
}

static void PixelClockAdjustmentForProgressiveToInterlaceUnit(const struct dml2_display_cfg *display_cfg, bool ptoi_supported, double *PixelClockBackEnd)
{
	//unsigned int num_active_planes = display_cfg->num_planes;

	//Progressive To Interlace Unit Effect
	for (unsigned int k = 0; k < display_cfg->num_planes; ++k) {
		PixelClockBackEnd[k] = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced == 1 && ptoi_supported == true) {
			// FIXME_STAGE2... can sw pass the pixel rate for interlaced directly
			//display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz = 2 * display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz;
		}
	}
}

static bool dml_is_420(enum dml2_source_format_class source_format)
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
	case dml2_422_planar_8:
		val = 0;
		break;
	case dml2_422_planar_10:
		val = 0;
		break;
	case dml2_422_planar_12:
		val = 0;
		break;
	case dml2_422_packed_8:
		val = 0;
		break;
	case dml2_422_packed_10:
		val = 0;
		break;
	case dml2_422_packed_12:
		val = 0;
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

static unsigned int dml_get_tile_block_size_bytes(enum dml2_swizzle_mode sw_mode)
{
	if (sw_mode == dml2_sw_linear)
		return 256;
	else if (sw_mode == dml2_sw_256b_2d)
		return 256;
	else if (sw_mode == dml2_sw_4kb_2d)
		return 4096;
	else if (sw_mode == dml2_sw_64kb_2d)
		return 65536;
	else if (sw_mode == dml2_sw_256kb_2d)
		return 262144;
	else if (sw_mode == dml2_gfx11_sw_linear)
		return 256;
	else if (sw_mode == dml2_gfx11_sw_64kb_d)
		return 65536;
	else if (sw_mode == dml2_gfx11_sw_64kb_d_t)
		return 65536;
	else if (sw_mode == dml2_gfx11_sw_64kb_d_x)
		return 65536;
	else if (sw_mode == dml2_gfx11_sw_64kb_r_x)
		return 65536;
	else if (sw_mode == dml2_gfx11_sw_256kb_d_x)
		return 262144;
	else if (sw_mode == dml2_gfx11_sw_256kb_r_x)
		return 262144;
	else {
		DML2_ASSERT(0);
		return 256;
	}
}

static bool dml_is_vertical_rotation(enum dml2_rotation_angle Scan)
{
	bool is_vert = false;
	if (Scan == dml2_rotation_90 || Scan == dml2_rotation_270) {
		is_vert = true;
	} else {
		is_vert = false;
	}
	return is_vert;
}

static int unsigned dml_get_gfx_version(enum dml2_swizzle_mode sw_mode)
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

static void CalculateBytePerPixelAndBlockSizes(
	enum dml2_source_format_class SourcePixelFormat,
	enum dml2_swizzle_mode SurfaceTiling,
	unsigned int pitch_y,
	unsigned int pitch_c,

	// Output
	unsigned int *BytePerPixelY,
	unsigned int *BytePerPixelC,
	double *BytePerPixelDETY,
	double *BytePerPixelDETC,
	unsigned int *BlockHeight256BytesY,
	unsigned int *BlockHeight256BytesC,
	unsigned int *BlockWidth256BytesY,
	unsigned int *BlockWidth256BytesC,
	unsigned int *MacroTileHeightY,
	unsigned int *MacroTileHeightC,
	unsigned int *MacroTileWidthY,
	unsigned int *MacroTileWidthC,
	bool *surf_linear128_l,
	bool *surf_linear128_c)
{
	*BytePerPixelDETY = 0;
	*BytePerPixelDETC = 0;
	*BytePerPixelY = 0;
	*BytePerPixelC = 0;

	if (SourcePixelFormat == dml2_444_64) {
		*BytePerPixelDETY = 8;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 8;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml2_444_32 || SourcePixelFormat == dml2_rgbe) {
		*BytePerPixelDETY = 4;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 4;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml2_444_16 || SourcePixelFormat == dml2_mono_16) {
		*BytePerPixelDETY = 2;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 2;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml2_444_8 || SourcePixelFormat == dml2_mono_8) {
		*BytePerPixelDETY = 1;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 1;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml2_rgbe_alpha) {
		*BytePerPixelDETY = 4;
		*BytePerPixelDETC = 1;
		*BytePerPixelY = 4;
		*BytePerPixelC = 1;
	} else if (SourcePixelFormat == dml2_420_8) {
		*BytePerPixelDETY = 1;
		*BytePerPixelDETC = 2;
		*BytePerPixelY = 1;
		*BytePerPixelC = 2;
	} else if (SourcePixelFormat == dml2_420_12) {
		*BytePerPixelDETY = 2;
		*BytePerPixelDETC = 4;
		*BytePerPixelY = 2;
		*BytePerPixelC = 4;
	} else if (SourcePixelFormat == dml2_420_10) {
		*BytePerPixelDETY = (double)(4.0 / 3);
		*BytePerPixelDETC = (double)(8.0 / 3);
		*BytePerPixelY = 2;
		*BytePerPixelC = 4;
	} else {
		dml2_printf("ERROR: DML::%s: SourcePixelFormat = %u not supported!\n", __func__, SourcePixelFormat);
		DML2_ASSERT(0);
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: SourcePixelFormat = %u\n", __func__, SourcePixelFormat);
	dml2_printf("DML::%s: BytePerPixelDETY = %f\n", __func__, *BytePerPixelDETY);
	dml2_printf("DML::%s: BytePerPixelDETC = %f\n", __func__, *BytePerPixelDETC);
	dml2_printf("DML::%s: BytePerPixelY = %u\n", __func__, *BytePerPixelY);
	dml2_printf("DML::%s: BytePerPixelC = %u\n", __func__, *BytePerPixelC);
	dml2_printf("DML::%s: pitch_y = %u\n", __func__, pitch_y);
	dml2_printf("DML::%s: pitch_c = %u\n", __func__, pitch_c);
	dml2_printf("DML::%s: surf_linear128_l = %u\n", __func__, *surf_linear128_l);
	dml2_printf("DML::%s: surf_linear128_c = %u\n", __func__, *surf_linear128_c);
#endif

	if (dml_get_gfx_version(SurfaceTiling) == 11) {
		*surf_linear128_l = 0;
		*surf_linear128_c = 0;
	} else {
		if (SurfaceTiling == dml2_sw_linear) {
			*surf_linear128_l = (((pitch_y * *BytePerPixelY) % 256) != 0);

			if (dml_is_420(SourcePixelFormat) || SourcePixelFormat == dml2_rgbe_alpha)
				*surf_linear128_c = (((pitch_c * *BytePerPixelC) % 256) != 0);
		}
	}

	if (!(dml_is_420(SourcePixelFormat) || SourcePixelFormat == dml2_rgbe_alpha)) {
		if (SurfaceTiling == dml2_sw_linear) {
			*BlockHeight256BytesY = 1;
		} else if (SourcePixelFormat == dml2_444_64) {
			*BlockHeight256BytesY = 4;
		} else if (SourcePixelFormat == dml2_444_8) {
			*BlockHeight256BytesY = 16;
		} else {
			*BlockHeight256BytesY = 8;
		}
		*BlockWidth256BytesY = 256U / *BytePerPixelY / *BlockHeight256BytesY;
		*BlockHeight256BytesC = 0;
		*BlockWidth256BytesC = 0;
	} else { // dual plane
		if (SurfaceTiling == dml2_sw_linear) {
			*BlockHeight256BytesY = 1;
			*BlockHeight256BytesC = 1;
		} else if (SourcePixelFormat == dml2_rgbe_alpha) {
			*BlockHeight256BytesY = 8;
			*BlockHeight256BytesC = 16;
		} else if (SourcePixelFormat == dml2_420_8) {
			*BlockHeight256BytesY = 16;
			*BlockHeight256BytesC = 8;
		} else {
			*BlockHeight256BytesY = 8;
			*BlockHeight256BytesC = 8;
		}
		*BlockWidth256BytesY = 256U / *BytePerPixelY / *BlockHeight256BytesY;
		*BlockWidth256BytesC = 256U / *BytePerPixelC / *BlockHeight256BytesC;
	}
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: BlockWidth256BytesY = %u\n", __func__, *BlockWidth256BytesY);
	dml2_printf("DML::%s: BlockHeight256BytesY = %u\n", __func__, *BlockHeight256BytesY);
	dml2_printf("DML::%s: BlockWidth256BytesC = %u\n", __func__, *BlockWidth256BytesC);
	dml2_printf("DML::%s: BlockHeight256BytesC = %u\n", __func__, *BlockHeight256BytesC);
#endif

	if (dml_get_gfx_version(SurfaceTiling) == 11) {
		if (SurfaceTiling == dml2_gfx11_sw_linear) {
			*MacroTileHeightY = *BlockHeight256BytesY;
			*MacroTileWidthY = 256 / *BytePerPixelY / *MacroTileHeightY;
			*MacroTileHeightC = *BlockHeight256BytesC;
			if (*MacroTileHeightC == 0) {
				*MacroTileWidthC = 0;
			} else {
				*MacroTileWidthC = 256 / *BytePerPixelC / *MacroTileHeightC;
			}
		} else if (SurfaceTiling == dml2_gfx11_sw_64kb_d || SurfaceTiling == dml2_gfx11_sw_64kb_d_t || SurfaceTiling == dml2_gfx11_sw_64kb_d_x || SurfaceTiling == dml2_gfx11_sw_64kb_r_x) {
			*MacroTileHeightY = 16 * *BlockHeight256BytesY;
			*MacroTileWidthY = 65536 / *BytePerPixelY / *MacroTileHeightY;
			*MacroTileHeightC = 16 * *BlockHeight256BytesC;
			if (*MacroTileHeightC == 0) {
				*MacroTileWidthC = 0;
			} else {
				*MacroTileWidthC = 65536 / *BytePerPixelC / *MacroTileHeightC;
			}
		} else {
			*MacroTileHeightY = 32 * *BlockHeight256BytesY;
			*MacroTileWidthY = 65536 * 4 / *BytePerPixelY / *MacroTileHeightY;
			*MacroTileHeightC = 32 * *BlockHeight256BytesC;
			if (*MacroTileHeightC == 0) {
				*MacroTileWidthC = 0;
			} else {
				*MacroTileWidthC = 65536 * 4 / *BytePerPixelC / *MacroTileHeightC;
			}
		}
	} else {
		unsigned int macro_tile_size_bytes = dml_get_tile_block_size_bytes(SurfaceTiling);
		unsigned int macro_tile_scale = 1; // macro tile to 256B req scaling

		if (SurfaceTiling == dml2_sw_linear) {
			macro_tile_scale = 1;
		} else if (SurfaceTiling == dml2_sw_4kb_2d) {
			macro_tile_scale = 4;
		} else if (SurfaceTiling == dml2_sw_64kb_2d) {
			macro_tile_scale = 16;
		} else if (SurfaceTiling == dml2_sw_256kb_2d) {
			macro_tile_scale = 32;
		} else {
			dml2_printf("ERROR: Invalid SurfaceTiling setting! val=%u\n", SurfaceTiling);
			DML2_ASSERT(0);
		}

		*MacroTileHeightY = macro_tile_scale * *BlockHeight256BytesY;
		*MacroTileWidthY = macro_tile_size_bytes / *BytePerPixelY / *MacroTileHeightY;
		*MacroTileHeightC = macro_tile_scale * *BlockHeight256BytesC;
		if (*MacroTileHeightC == 0) {
			*MacroTileWidthC = 0;
		} else {
			*MacroTileWidthC = macro_tile_size_bytes / *BytePerPixelC / *MacroTileHeightC;
		}
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: MacroTileWidthY = %u\n", __func__, *MacroTileWidthY);
	dml2_printf("DML::%s: MacroTileHeightY = %u\n", __func__, *MacroTileHeightY);
	dml2_printf("DML::%s: MacroTileWidthC = %u\n", __func__, *MacroTileWidthC);
	dml2_printf("DML::%s: MacroTileHeightC = %u\n", __func__, *MacroTileHeightC);
#endif
}

static void CalculateSinglePipeDPPCLKAndSCLThroughput(
	double HRatio,
	double HRatioChroma,
	double VRatio,
	double VRatioChroma,
	double MaxDCHUBToPSCLThroughput,
	double MaxPSCLToLBThroughput,
	double PixelClock,
	enum dml2_source_format_class SourcePixelFormat,
	unsigned int HTaps,
	unsigned int HTapsChroma,
	unsigned int VTaps,
	unsigned int VTapsChroma,

	// Output
	double *PSCL_THROUGHPUT,
	double *PSCL_THROUGHPUT_CHROMA,
	double *DPPCLKUsingSingleDPP)
{
	double DPPCLKUsingSingleDPPLuma;
	double DPPCLKUsingSingleDPPChroma;

	if (HRatio > 1) {
		*PSCL_THROUGHPUT = math_min2(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput * HRatio / math_ceil2((double)HTaps / 6.0, 1.0));
	} else {
		*PSCL_THROUGHPUT = math_min2(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput);
	}

	DPPCLKUsingSingleDPPLuma = PixelClock * math_max3(VTaps / 6 * math_min2(1, HRatio), HRatio * VRatio / *PSCL_THROUGHPUT, 1);

	if ((HTaps > 6 || VTaps > 6) && DPPCLKUsingSingleDPPLuma < 2 * PixelClock)
		DPPCLKUsingSingleDPPLuma = 2 * PixelClock;

	if (!dml_is_420(SourcePixelFormat) && SourcePixelFormat != dml2_rgbe_alpha) {
		*PSCL_THROUGHPUT_CHROMA = 0;
		*DPPCLKUsingSingleDPP = DPPCLKUsingSingleDPPLuma;
	} else {
		if (HRatioChroma > 1) {
			*PSCL_THROUGHPUT_CHROMA = math_min2(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput * HRatioChroma / math_ceil2((double)HTapsChroma / 6.0, 1.0));
		} else {
			*PSCL_THROUGHPUT_CHROMA = math_min2(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput);
		}
		DPPCLKUsingSingleDPPChroma = PixelClock * math_max3(VTapsChroma / 6 * math_min2(1, HRatioChroma),
			HRatioChroma * VRatioChroma / *PSCL_THROUGHPUT_CHROMA, 1);
		if ((HTapsChroma > 6 || VTapsChroma > 6) && DPPCLKUsingSingleDPPChroma < 2 * PixelClock)
			DPPCLKUsingSingleDPPChroma = 2 * PixelClock;
		*DPPCLKUsingSingleDPP = math_max2(DPPCLKUsingSingleDPPLuma, DPPCLKUsingSingleDPPChroma);
	}
}

static void CalculateSwathWidth(
	const struct dml2_display_cfg *display_cfg,
	bool ForceSingleDPP,
	unsigned int NumberOfActiveSurfaces,
	enum dml2_odm_mode ODMMode[],
	unsigned int BytePerPixY[],
	unsigned int BytePerPixC[],
	unsigned int Read256BytesBlockHeightY[],
	unsigned int Read256BytesBlockHeightC[],
	unsigned int Read256BytesBlockWidthY[],
	unsigned int Read256BytesBlockWidthC[],
	bool surf_linear128_l[],
	bool surf_linear128_c[],
	unsigned int DPPPerSurface[],

	// Output
	unsigned int req_per_swath_ub_l[],
	unsigned int req_per_swath_ub_c[],
	unsigned int SwathWidthSingleDPPY[], // post-rotated plane width
	unsigned int SwathWidthSingleDPPC[],
	unsigned int SwathWidthY[], // per-pipe
	unsigned int SwathWidthC[], // per-pipe
	unsigned int MaximumSwathHeightY[],
	unsigned int MaximumSwathHeightC[],
	unsigned int swath_width_luma_ub[], // per-pipe
	unsigned int swath_width_chroma_ub[]) // per-pipe
{
	enum dml2_odm_mode MainSurfaceODMMode;
	double odm_hactive_factor = 1.0;
	unsigned int req_width_horz_y;
	unsigned int req_width_horz_c;
	unsigned int surface_width_ub_l;
	unsigned int surface_height_ub_l;
	unsigned int surface_width_ub_c;
	unsigned int surface_height_ub_c;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: ForceSingleDPP = %u\n", __func__, ForceSingleDPP);
	dml2_printf("DML::%s: NumberOfActiveSurfaces = %u\n", __func__, NumberOfActiveSurfaces);
#endif

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (!dml_is_vertical_rotation(display_cfg->plane_descriptors[k].composition.rotation_angle)) {
			SwathWidthSingleDPPY[k] = (unsigned int)display_cfg->plane_descriptors[k].composition.viewport.plane0.width;
		} else {
			SwathWidthSingleDPPY[k] = (unsigned int)display_cfg->plane_descriptors[k].composition.viewport.plane0.height;
		}

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u ViewportWidth=%u\n", __func__, k, display_cfg->plane_descriptors[k].composition.viewport.plane0.width);
		dml2_printf("DML::%s: k=%u ViewportHeight=%u\n", __func__, k, display_cfg->plane_descriptors[k].composition.viewport.plane0.height);
		dml2_printf("DML::%s: k=%u DPPPerSurface=%u\n", __func__, k, DPPPerSurface[k]);
#endif

		MainSurfaceODMMode = ODMMode[k];

		if (ForceSingleDPP) {
			SwathWidthY[k] = SwathWidthSingleDPPY[k];
		} else {
			if (MainSurfaceODMMode == dml2_odm_mode_combine_4to1)
				odm_hactive_factor = 4.0;
			else if (MainSurfaceODMMode == dml2_odm_mode_combine_3to1)
				odm_hactive_factor = 3.0;
			else if (MainSurfaceODMMode == dml2_odm_mode_combine_2to1)
				odm_hactive_factor = 2.0;

			if (MainSurfaceODMMode == dml2_odm_mode_combine_4to1 || MainSurfaceODMMode == dml2_odm_mode_combine_3to1 || MainSurfaceODMMode == dml2_odm_mode_combine_2to1) {
				SwathWidthY[k] = (unsigned int)(math_min2((double)SwathWidthSingleDPPY[k], math_round((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active / odm_hactive_factor * display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio)));
			} else if (DPPPerSurface[k] == 2) {
				SwathWidthY[k] = SwathWidthSingleDPPY[k] / 2;
			} else {
				SwathWidthY[k] = SwathWidthSingleDPPY[k];
			}
		}

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u HActive=%u\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active);
		dml2_printf("DML::%s: k=%u HRatio=%f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio);
		dml2_printf("DML::%s: k=%u MainSurfaceODMMode=%u\n", __func__, k, MainSurfaceODMMode);
		dml2_printf("DML::%s: k=%u SwathWidthSingleDPPY=%u\n", __func__, k, SwathWidthSingleDPPY[k]);
		dml2_printf("DML::%s: k=%u SwathWidthY=%u\n", __func__, k, SwathWidthY[k]);
#endif

		if (dml_is_420(display_cfg->plane_descriptors[k].pixel_format)) {
			SwathWidthC[k] = SwathWidthY[k] / 2;
			SwathWidthSingleDPPC[k] = SwathWidthSingleDPPY[k] / 2;
		} else {
			SwathWidthC[k] = SwathWidthY[k];
			SwathWidthSingleDPPC[k] = SwathWidthSingleDPPY[k];
		}

		if (ForceSingleDPP == true) {
			SwathWidthY[k] = SwathWidthSingleDPPY[k];
			SwathWidthC[k] = SwathWidthSingleDPPC[k];
		}

		req_width_horz_y = Read256BytesBlockWidthY[k];
		req_width_horz_c = Read256BytesBlockWidthC[k];

		if (surf_linear128_l[k])
			req_width_horz_y = req_width_horz_y / 2;

		if (surf_linear128_c[k])
			req_width_horz_c = req_width_horz_c / 2;

		surface_width_ub_l = (unsigned int)math_ceil2((double)display_cfg->plane_descriptors[k].surface.plane0.width, req_width_horz_y);
		surface_height_ub_l = (unsigned int)math_ceil2((double)display_cfg->plane_descriptors[k].surface.plane0.height, Read256BytesBlockHeightY[k]);
		surface_width_ub_c = (unsigned int)math_ceil2((double)display_cfg->plane_descriptors[k].surface.plane1.width, req_width_horz_c);
		surface_height_ub_c = (unsigned int)math_ceil2((double)display_cfg->plane_descriptors[k].surface.plane1.height, Read256BytesBlockHeightC[k]);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u surface_width_ub_l=%u\n", __func__, k, surface_width_ub_l);
		dml2_printf("DML::%s: k=%u surface_height_ub_l=%u\n", __func__, k, surface_height_ub_l);
		dml2_printf("DML::%s: k=%u surface_width_ub_c=%u\n", __func__, k, surface_width_ub_c);
		dml2_printf("DML::%s: k=%u surface_height_ub_c=%u\n", __func__, k, surface_height_ub_c);
		dml2_printf("DML::%s: k=%u req_width_horz_y=%u\n", __func__, k, req_width_horz_y);
		dml2_printf("DML::%s: k=%u req_width_horz_c=%u\n", __func__, k, req_width_horz_c);
		dml2_printf("DML::%s: k=%u Read256BytesBlockWidthY=%u\n", __func__, k, Read256BytesBlockWidthY[k]);
		dml2_printf("DML::%s: k=%u Read256BytesBlockHeightY=%u\n", __func__, k, Read256BytesBlockHeightY[k]);
		dml2_printf("DML::%s: k=%u Read256BytesBlockWidthC=%u\n", __func__, k, Read256BytesBlockWidthC[k]);
		dml2_printf("DML::%s: k=%u Read256BytesBlockHeightC=%u\n", __func__, k, Read256BytesBlockHeightC[k]);
		dml2_printf("DML::%s: k=%u req_width_horz_y=%u\n", __func__, k, req_width_horz_y);
		dml2_printf("DML::%s: k=%u req_width_horz_c=%u\n", __func__, k, req_width_horz_c);
		dml2_printf("DML::%s: k=%u ViewportStationary=%u\n", __func__, k, display_cfg->plane_descriptors[k].composition.viewport.stationary);
		dml2_printf("DML::%s: k=%u DPPPerSurface=%u\n", __func__, k, DPPPerSurface[k]);
#endif

		req_per_swath_ub_l[k] = 0;
		req_per_swath_ub_c[k] = 0;
		if (!dml_is_vertical_rotation(display_cfg->plane_descriptors[k].composition.rotation_angle)) {
			MaximumSwathHeightY[k] = Read256BytesBlockHeightY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockHeightC[k];
			if (display_cfg->plane_descriptors[k].composition.viewport.stationary && DPPPerSurface[k] == 1) {
				swath_width_luma_ub[k] = (unsigned int)(math_min2(surface_width_ub_l, math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane0.x_start + SwathWidthY[k] + req_width_horz_y - 1, req_width_horz_y) - math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane0.x_start, req_width_horz_y)));
			} else {
				swath_width_luma_ub[k] = (unsigned int)(math_min2(surface_width_ub_l, math_ceil2((double)SwathWidthY[k] - 1, req_width_horz_y) + req_width_horz_y));
			}
			req_per_swath_ub_l[k] = swath_width_luma_ub[k] / req_width_horz_y;

			if (BytePerPixC[k] > 0) {
				if (display_cfg->plane_descriptors[k].composition.viewport.stationary && DPPPerSurface[k] == 1) {
					swath_width_chroma_ub[k] = (unsigned int)(math_min2(surface_width_ub_c, math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start + SwathWidthC[k] + req_width_horz_c - 1, req_width_horz_c) - math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start, req_width_horz_c)));
				} else {
					swath_width_chroma_ub[k] = (unsigned int)(math_min2(surface_width_ub_c, math_ceil2((double)SwathWidthC[k] - 1, req_width_horz_c) + req_width_horz_c));
				}
				req_per_swath_ub_c[k] = swath_width_chroma_ub[k] / req_width_horz_c;
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		} else {
			MaximumSwathHeightY[k] = Read256BytesBlockWidthY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockWidthC[k];

			if (display_cfg->plane_descriptors[k].composition.viewport.stationary && DPPPerSurface[k] == 1) {
				swath_width_luma_ub[k] = (unsigned int)(math_min2(surface_height_ub_l, math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane0.y_start + SwathWidthY[k] + Read256BytesBlockHeightY[k] - 1, Read256BytesBlockHeightY[k]) - math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane0.y_start, Read256BytesBlockHeightY[k])));
			} else {
				swath_width_luma_ub[k] = (unsigned int)(math_min2(surface_height_ub_l, math_ceil2((double)SwathWidthY[k] - 1, Read256BytesBlockHeightY[k]) + Read256BytesBlockHeightY[k]));
			}
			req_per_swath_ub_l[k] = swath_width_luma_ub[k] / Read256BytesBlockHeightY[k];
			if (BytePerPixC[k] > 0) {
				if (display_cfg->plane_descriptors[k].composition.viewport.stationary && DPPPerSurface[k] == 1) {
					swath_width_chroma_ub[k] = (unsigned int)(math_min2(surface_height_ub_c, math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start + SwathWidthC[k] + Read256BytesBlockHeightC[k] - 1, Read256BytesBlockHeightC[k]) - math_floor2(display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start, Read256BytesBlockHeightC[k])));
				} else {
					swath_width_chroma_ub[k] = (unsigned int)(math_min2(surface_height_ub_c, math_ceil2((double)SwathWidthC[k] - 1, Read256BytesBlockHeightC[k]) + Read256BytesBlockHeightC[k]));
				}
				req_per_swath_ub_c[k] = swath_width_chroma_ub[k] / Read256BytesBlockHeightC[k];
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		}

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u swath_width_luma_ub=%u\n", __func__, k, swath_width_luma_ub[k]);
		dml2_printf("DML::%s: k=%u swath_width_chroma_ub=%u\n", __func__, k, swath_width_chroma_ub[k]);
		dml2_printf("DML::%s: k=%u MaximumSwathHeightY=%u\n", __func__, k, MaximumSwathHeightY[k]);
		dml2_printf("DML::%s: k=%u MaximumSwathHeightC=%u\n", __func__, k, MaximumSwathHeightC[k]);
		dml2_printf("DML::%s: k=%u req_per_swath_ub_l=%u\n", __func__, k, req_per_swath_ub_l[k]);
		dml2_printf("DML::%s: k=%u req_per_swath_ub_c=%u\n", __func__, k, req_per_swath_ub_c[k]);
#endif

	}
}

static bool UnboundedRequest(bool unb_req_force_en, bool unb_req_force_val, unsigned int TotalNumberOfActiveDPP, bool NoChromaOrLinear)
{
	bool unb_req_ok = false;
	bool unb_req_en = false;

	unb_req_ok = (TotalNumberOfActiveDPP == 1 && NoChromaOrLinear);
	unb_req_en = unb_req_ok;

	if (unb_req_force_en) {
		unb_req_en = unb_req_force_val && unb_req_ok;
	}
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: unb_req_force_en = %u\n", __func__, unb_req_force_en);
	dml2_printf("DML::%s: unb_req_force_val = %u\n", __func__, unb_req_force_val);
	dml2_printf("DML::%s: unb_req_ok = %u\n", __func__, unb_req_ok);
	dml2_printf("DML::%s: unb_req_en = %u\n", __func__, unb_req_en);
#endif
	return (unb_req_en);
}

static void CalculateDETBufferSize(
	struct dml2_core_shared_CalculateDETBufferSize_locals *l,
	const struct dml2_display_cfg *display_cfg,
	bool ForceSingleDPP,
	unsigned int NumberOfActiveSurfaces,
	bool UnboundedRequestEnabled,
	unsigned int nomDETInKByte,
	unsigned int MaxTotalDETInKByte,
	unsigned int ConfigReturnBufferSizeInKByte,
	unsigned int MinCompressedBufferSizeInKByte,
	unsigned int ConfigReturnBufferSegmentSizeInkByte,
	unsigned int CompressedBufferSegmentSizeInkByte,
	double ReadBandwidthLuma[],
	double ReadBandwidthChroma[],
	unsigned int full_swath_bytes_l[],
	unsigned int full_swath_bytes_c[],
	unsigned int DPPPerSurface[],
	// Output
	unsigned int DETBufferSizeInKByte[],
	unsigned int *CompressedBufferSizeInkByte)
{
	memset(l, 0, sizeof(struct dml2_core_shared_CalculateDETBufferSize_locals));

	bool DETPieceAssignedToThisSurfaceAlready[DML2_MAX_PLANES];
	bool NextPotentialSurfaceToAssignDETPieceFound;
	bool MinimizeReallocationSuccess = false;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: ForceSingleDPP = %u\n", __func__, ForceSingleDPP);
	dml2_printf("DML::%s: nomDETInKByte = %u\n", __func__, nomDETInKByte);
	dml2_printf("DML::%s: NumberOfActiveSurfaces = %u\n", __func__, NumberOfActiveSurfaces);
	dml2_printf("DML::%s: UnboundedRequestEnabled = %u\n", __func__, UnboundedRequestEnabled);
	dml2_printf("DML::%s: MaxTotalDETInKByte = %u\n", __func__, MaxTotalDETInKByte);
	dml2_printf("DML::%s: ConfigReturnBufferSizeInKByte = %u\n", __func__, ConfigReturnBufferSizeInKByte);
	dml2_printf("DML::%s: MinCompressedBufferSizeInKByte = %u\n", __func__, MinCompressedBufferSizeInKByte);
	dml2_printf("DML::%s: CompressedBufferSegmentSizeInkByte = %u\n", __func__, CompressedBufferSegmentSizeInkByte);
#endif

	// Note: Will use default det size if that fits 2 swaths
	if (UnboundedRequestEnabled) {
		if (display_cfg->plane_descriptors[0].overrides.det_size_override_kb > 0) {
			DETBufferSizeInKByte[0] = display_cfg->plane_descriptors[0].overrides.det_size_override_kb;
		} else {
			DETBufferSizeInKByte[0] = (unsigned int)math_max2(128.0, math_ceil2(2.0 * ((double)full_swath_bytes_l[0] + (double)full_swath_bytes_c[0]) / 1024.0, ConfigReturnBufferSegmentSizeInkByte));
		}
		*CompressedBufferSizeInkByte = ConfigReturnBufferSizeInKByte - DETBufferSizeInKByte[0];
	} else {
		l->DETBufferSizePoolInKByte = MaxTotalDETInKByte;
		for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
			DETBufferSizeInKByte[k] = 0;
			if (dml_is_420(display_cfg->plane_descriptors[k].pixel_format)) {
				l->max_minDET = nomDETInKByte - ConfigReturnBufferSegmentSizeInkByte;
			} else {
				l->max_minDET = nomDETInKByte;
			}
			l->minDET = 128;
			l->minDET_pipe = 0;

			// add DET resource until can hold 2 full swaths
			while (l->minDET <= l->max_minDET && l->minDET_pipe == 0) {
				if (2.0 * ((double)full_swath_bytes_l[k] + (double)full_swath_bytes_c[k]) / 1024.0 <= l->minDET)
					l->minDET_pipe = l->minDET;
				l->minDET = l->minDET + ConfigReturnBufferSegmentSizeInkByte;
			}

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u minDET = %u\n", __func__, k, l->minDET);
			dml2_printf("DML::%s: k=%u max_minDET = %u\n", __func__, k, l->max_minDET);
			dml2_printf("DML::%s: k=%u minDET_pipe = %u\n", __func__, k, l->minDET_pipe);
			dml2_printf("DML::%s: k=%u full_swath_bytes_l = %u\n", __func__, k, full_swath_bytes_l[k]);
			dml2_printf("DML::%s: k=%u full_swath_bytes_c = %u\n", __func__, k, full_swath_bytes_c[k]);
#endif

			if (l->minDET_pipe == 0) {
				l->minDET_pipe = (unsigned int)(math_max2(128, math_ceil2(((double)full_swath_bytes_l[k] + (double)full_swath_bytes_c[k]) / 1024.0, ConfigReturnBufferSegmentSizeInkByte)));
#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: k=%u minDET_pipe = %u (assume each plane take half DET)\n", __func__, k, l->minDET_pipe);
#endif
			}

			if (dml_is_phantom_pipe(&display_cfg->plane_descriptors[k])) {
				DETBufferSizeInKByte[k] = 0;
			} else if (display_cfg->plane_descriptors[k].overrides.det_size_override_kb > 0) {
				DETBufferSizeInKByte[k] = display_cfg->plane_descriptors[k].overrides.det_size_override_kb;
				l->DETBufferSizePoolInKByte = l->DETBufferSizePoolInKByte - (ForceSingleDPP ? 1 : DPPPerSurface[k]) * display_cfg->plane_descriptors[k].overrides.det_size_override_kb;
			} else if ((ForceSingleDPP ? 1 : DPPPerSurface[k]) * l->minDET_pipe <= l->DETBufferSizePoolInKByte) {
				DETBufferSizeInKByte[k] = l->minDET_pipe;
				l->DETBufferSizePoolInKByte = l->DETBufferSizePoolInKByte - (ForceSingleDPP ? 1 : DPPPerSurface[k]) * l->minDET_pipe;
			}

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u DPPPerSurface = %u\n", __func__, k, DPPPerSurface[k]);
			dml2_printf("DML::%s: k=%u DETSizeOverride = %u\n", __func__, k, display_cfg->plane_descriptors[k].overrides.det_size_override_kb);
			dml2_printf("DML::%s: k=%u DETBufferSizeInKByte = %u\n", __func__, k, DETBufferSizeInKByte[k]);
			dml2_printf("DML::%s: DETBufferSizePoolInKByte = %u\n", __func__, l->DETBufferSizePoolInKByte);
#endif
		}

		if (display_cfg->minimize_det_reallocation) {
			MinimizeReallocationSuccess = true;
			// To minimize det reallocation, we don't distribute based on each surfaces bandwidth proportional to the global
			// but rather distribute DET across streams proportionally based on pixel rate, and only distribute based on
			// bandwidth between the planes on the same stream.  This ensures that large scale re-distribution only on a
			// stream count and/or pixel rate change, which is must less likely then general bandwidth changes per plane.

			// Calculate total pixel rate
			for (unsigned int k = 0; k < display_cfg->num_streams; ++k) {
				l->TotalPixelRate += display_cfg->stream_descriptors[k].timing.pixel_clock_khz;
			}

			// Calculate per stream DET budget
			for (unsigned int k = 0; k < display_cfg->num_streams; ++k) {
				l->DETBudgetPerStream[k] = (unsigned int)((double) display_cfg->stream_descriptors[k].timing.pixel_clock_khz * MaxTotalDETInKByte / l->TotalPixelRate);
				l->RemainingDETBudgetPerStream[k] = l->DETBudgetPerStream[k];
			}

			// Calculate the per stream total bandwidth
			for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
				if (!dml_is_phantom_pipe(&display_cfg->plane_descriptors[k])) {
					l->TotalBandwidthPerStream[display_cfg->plane_descriptors[k].stream_index] += (unsigned int)(ReadBandwidthLuma[k] + ReadBandwidthChroma[k]);

					// Check the minimum can be satisfied by budget
					if (l->RemainingDETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index] >= DETBufferSizeInKByte[k] * (ForceSingleDPP ? 1 : DPPPerSurface[k])) {
						l->RemainingDETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index] -= DETBufferSizeInKByte[k] * (ForceSingleDPP ? 1 : DPPPerSurface[k]);
					} else {
						MinimizeReallocationSuccess = false;
						break;
					}
				}
			}

			if (MinimizeReallocationSuccess) {
				// Since a fixed budget per stream is sufficient to satisfy the minimums, just re-distribute each streams
				// budget proportionally across its planes
				l->ResidualDETAfterRounding = MaxTotalDETInKByte;

				for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
					if (!dml_is_phantom_pipe(&display_cfg->plane_descriptors[k])) {
						l->IdealDETBudget = (unsigned int)(((ReadBandwidthLuma[k] + ReadBandwidthChroma[k]) / l->TotalBandwidthPerStream[display_cfg->plane_descriptors[k].stream_index])
							* l->DETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index]);

						if (l->IdealDETBudget > DETBufferSizeInKByte[k]) {
							l->DeltaDETBudget = l->IdealDETBudget - DETBufferSizeInKByte[k];
							if (l->DeltaDETBudget > l->RemainingDETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index])
								l->DeltaDETBudget = l->RemainingDETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index];

							/* split the additional budgeted DET among the pipes per plane */
							DETBufferSizeInKByte[k] += (unsigned int)((double)l->DeltaDETBudget / (ForceSingleDPP ? 1 : DPPPerSurface[k]));
							l->RemainingDETBudgetPerStream[display_cfg->plane_descriptors[k].stream_index] -= l->DeltaDETBudget;
						}

						// Round down to segment size
						DETBufferSizeInKByte[k] = (DETBufferSizeInKByte[k] / ConfigReturnBufferSegmentSizeInkByte) * ConfigReturnBufferSegmentSizeInkByte;

						l->ResidualDETAfterRounding -= DETBufferSizeInKByte[k] * (ForceSingleDPP ? 1 : DPPPerSurface[k]);
					}
				}
			}
		}

		if (!MinimizeReallocationSuccess) {
			l->TotalBandwidth = 0;
			for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
				if (!dml_is_phantom_pipe(&display_cfg->plane_descriptors[k])) {
					l->TotalBandwidth = l->TotalBandwidth + ReadBandwidthLuma[k] + ReadBandwidthChroma[k];
				}
			}
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: --- Before bandwidth adjustment ---\n", __func__);
			for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
				dml2_printf("DML::%s: k=%u DETBufferSizeInKByte = %u\n", __func__, k, DETBufferSizeInKByte[k]);
			}
			dml2_printf("DML::%s: --- DET allocation with bandwidth ---\n", __func__);
#endif
			dml2_printf("DML::%s: TotalBandwidth = %f\n", __func__, l->TotalBandwidth);
			l->BandwidthOfSurfacesNotAssignedDETPiece = l->TotalBandwidth;
			for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {

				if (dml_is_phantom_pipe(&display_cfg->plane_descriptors[k])) {
					DETPieceAssignedToThisSurfaceAlready[k] = true;
				} else if (display_cfg->plane_descriptors[k].overrides.det_size_override_kb > 0 || (((double)(ForceSingleDPP ? 1 : DPPPerSurface[k]) * (double)DETBufferSizeInKByte[k] / (double)MaxTotalDETInKByte) >= ((ReadBandwidthLuma[k] + ReadBandwidthChroma[k]) / l->TotalBandwidth))) {
					DETPieceAssignedToThisSurfaceAlready[k] = true;
					l->BandwidthOfSurfacesNotAssignedDETPiece = l->BandwidthOfSurfacesNotAssignedDETPiece - ReadBandwidthLuma[k] - ReadBandwidthChroma[k];
				} else {
					DETPieceAssignedToThisSurfaceAlready[k] = false;
				}
#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: k=%u DETPieceAssignedToThisSurfaceAlready = %u\n", __func__, k, DETPieceAssignedToThisSurfaceAlready[k]);
				dml2_printf("DML::%s: k=%u BandwidthOfSurfacesNotAssignedDETPiece = %f\n", __func__, k, l->BandwidthOfSurfacesNotAssignedDETPiece);
#endif
			}

			for (unsigned int j = 0; j < NumberOfActiveSurfaces; ++j) {
				NextPotentialSurfaceToAssignDETPieceFound = false;
				l->NextSurfaceToAssignDETPiece = 0;

				for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
#ifdef __DML_VBA_DEBUG__
					dml2_printf("DML::%s: j=%u k=%u, ReadBandwidthLuma[k] = %f\n", __func__, j, k, ReadBandwidthLuma[k]);
					dml2_printf("DML::%s: j=%u k=%u, ReadBandwidthChroma[k] = %f\n", __func__, j, k, ReadBandwidthChroma[k]);
					dml2_printf("DML::%s: j=%u k=%u, ReadBandwidthLuma[Next] = %f\n", __func__, j, k, ReadBandwidthLuma[l->NextSurfaceToAssignDETPiece]);
					dml2_printf("DML::%s: j=%u k=%u, ReadBandwidthChroma[Next] = %f\n", __func__, j, k, ReadBandwidthChroma[l->NextSurfaceToAssignDETPiece]);
					dml2_printf("DML::%s: j=%u k=%u, NextSurfaceToAssignDETPiece = %u\n", __func__, j, k, l->NextSurfaceToAssignDETPiece);
#endif
					if (!DETPieceAssignedToThisSurfaceAlready[k] && (!NextPotentialSurfaceToAssignDETPieceFound ||
						ReadBandwidthLuma[k] + ReadBandwidthChroma[k] < ReadBandwidthLuma[l->NextSurfaceToAssignDETPiece] + ReadBandwidthChroma[l->NextSurfaceToAssignDETPiece])) {
						l->NextSurfaceToAssignDETPiece = k;
						NextPotentialSurfaceToAssignDETPieceFound = true;
					}
#ifdef __DML_VBA_DEBUG__
					dml2_printf("DML::%s: j=%u k=%u, DETPieceAssignedToThisSurfaceAlready = %u\n", __func__, j, k, DETPieceAssignedToThisSurfaceAlready[k]);
					dml2_printf("DML::%s: j=%u k=%u, NextPotentialSurfaceToAssignDETPieceFound = %u\n", __func__, j, k, NextPotentialSurfaceToAssignDETPieceFound);
#endif
				}

				if (NextPotentialSurfaceToAssignDETPieceFound) {
					l->NextDETBufferPieceInKByte = (unsigned int)(math_min2(
						math_round((double)l->DETBufferSizePoolInKByte * (ReadBandwidthLuma[l->NextSurfaceToAssignDETPiece] + ReadBandwidthChroma[l->NextSurfaceToAssignDETPiece]) / l->BandwidthOfSurfacesNotAssignedDETPiece /
							((ForceSingleDPP ? 1 : DPPPerSurface[l->NextSurfaceToAssignDETPiece]) * ConfigReturnBufferSegmentSizeInkByte))
						* (ForceSingleDPP ? 1 : DPPPerSurface[l->NextSurfaceToAssignDETPiece]) * ConfigReturnBufferSegmentSizeInkByte,
						math_floor2((double)l->DETBufferSizePoolInKByte, (ForceSingleDPP ? 1 : DPPPerSurface[l->NextSurfaceToAssignDETPiece]) * ConfigReturnBufferSegmentSizeInkByte)));

#ifdef __DML_VBA_DEBUG__
					dml2_printf("DML::%s: j=%u, DETBufferSizePoolInKByte = %u\n", __func__, j, l->DETBufferSizePoolInKByte);
					dml2_printf("DML::%s: j=%u, NextSurfaceToAssignDETPiece = %u\n", __func__, j, l->NextSurfaceToAssignDETPiece);
					dml2_printf("DML::%s: j=%u, ReadBandwidthLuma[%u] = %f\n", __func__, j, l->NextSurfaceToAssignDETPiece, ReadBandwidthLuma[l->NextSurfaceToAssignDETPiece]);
					dml2_printf("DML::%s: j=%u, ReadBandwidthChroma[%u] = %f\n", __func__, j, l->NextSurfaceToAssignDETPiece, ReadBandwidthChroma[l->NextSurfaceToAssignDETPiece]);
					dml2_printf("DML::%s: j=%u, BandwidthOfSurfacesNotAssignedDETPiece = %f\n", __func__, j, l->BandwidthOfSurfacesNotAssignedDETPiece);
					dml2_printf("DML::%s: j=%u, NextDETBufferPieceInKByte = %u\n", __func__, j, l->NextDETBufferPieceInKByte);
					dml2_printf("DML::%s: j=%u, DETBufferSizeInKByte[%u] increases from %u ", __func__, j, l->NextSurfaceToAssignDETPiece, DETBufferSizeInKByte[l->NextSurfaceToAssignDETPiece]);
#endif

					DETBufferSizeInKByte[l->NextSurfaceToAssignDETPiece] = DETBufferSizeInKByte[l->NextSurfaceToAssignDETPiece] + l->NextDETBufferPieceInKByte / (ForceSingleDPP ? 1 : DPPPerSurface[l->NextSurfaceToAssignDETPiece]);
#ifdef __DML_VBA_DEBUG__
					dml2_printf("to %u\n", DETBufferSizeInKByte[l->NextSurfaceToAssignDETPiece]);
#endif

					l->DETBufferSizePoolInKByte = l->DETBufferSizePoolInKByte - l->NextDETBufferPieceInKByte;
					DETPieceAssignedToThisSurfaceAlready[l->NextSurfaceToAssignDETPiece] = true;
					l->BandwidthOfSurfacesNotAssignedDETPiece = l->BandwidthOfSurfacesNotAssignedDETPiece - (ReadBandwidthLuma[l->NextSurfaceToAssignDETPiece] + ReadBandwidthChroma[l->NextSurfaceToAssignDETPiece]);
				}
			}
		}
		*CompressedBufferSizeInkByte = MinCompressedBufferSizeInKByte;
	}
	*CompressedBufferSizeInkByte = *CompressedBufferSizeInkByte * CompressedBufferSegmentSizeInkByte / ConfigReturnBufferSegmentSizeInkByte;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: --- After bandwidth adjustment ---\n", __func__);
	dml2_printf("DML::%s: CompressedBufferSizeInkByte = %u\n", __func__, *CompressedBufferSizeInkByte);
	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		dml2_printf("DML::%s: k=%u DETBufferSizeInKByte = %u (TotalReadBandWidth=%f)\n", __func__, k, DETBufferSizeInKByte[k], ReadBandwidthLuma[k] + ReadBandwidthChroma[k]);
	}
#endif
}

static double CalculateRequiredDispclk(
	enum dml2_odm_mode ODMMode,
	double PixelClock)
{

	if (ODMMode == dml2_odm_mode_combine_4to1) {
		return PixelClock / 4.0;
	} else if (ODMMode == dml2_odm_mode_combine_3to1) {
		return PixelClock / 3.0;
	} else if (ODMMode == dml2_odm_mode_combine_2to1) {
		return PixelClock / 2.0;
	} else {
		return PixelClock;
	}
}

static double TruncToValidBPP(
	struct dml2_core_shared_TruncToValidBPP_locals *l,
	double LinkBitRate,
	unsigned int Lanes,
	unsigned int HTotal,
	unsigned int HActive,
	double PixelClock,
	double DesiredBPP,
	bool DSCEnable,
	enum dml2_output_encoder_class Output,
	enum dml2_output_format_class Format,
	unsigned int DSCInputBitPerComponent,
	unsigned int DSCSlices,
	unsigned int AudioRate,
	unsigned int AudioLayout,
	enum dml2_odm_mode ODMModeNoDSC,
	enum dml2_odm_mode ODMModeDSC,

	// Output
	unsigned int *RequiredSlots)
{
	double MaxLinkBPP;
	unsigned int MinDSCBPP;
	double MaxDSCBPP;
	unsigned int NonDSCBPP0;
	unsigned int NonDSCBPP1;
	unsigned int NonDSCBPP2;
	enum dml2_odm_mode ODMMode;

	if (Format == dml2_420) {
		NonDSCBPP0 = 12;
		NonDSCBPP1 = 15;
		NonDSCBPP2 = 18;
		MinDSCBPP = 6;
		MaxDSCBPP = 16;
	} else if (Format == dml2_444) {
		NonDSCBPP0 = 24;
		NonDSCBPP1 = 30;
		NonDSCBPP2 = 36;
		MinDSCBPP = 8;
		MaxDSCBPP = 16;
	} else {
		if (Output == dml2_hdmi || Output == dml2_hdmifrl) {
			NonDSCBPP0 = 24;
			NonDSCBPP1 = 24;
			NonDSCBPP2 = 24;
		} else {
			NonDSCBPP0 = 16;
			NonDSCBPP1 = 20;
			NonDSCBPP2 = 24;
		}
		if (Format == dml2_n422 || Output == dml2_hdmifrl) {
			MinDSCBPP = 7;
			MaxDSCBPP = 16;
		} else {
			MinDSCBPP = 8;
			MaxDSCBPP = 16;
		}
	}
	if (Output == dml2_dp2p0) {
		MaxLinkBPP = LinkBitRate * Lanes / PixelClock * 128.0 / 132.0 * 383.0 / 384.0 * 65536.0 / 65540.0;
	} else if (DSCEnable && Output == dml2_dp) {
		MaxLinkBPP = LinkBitRate / 10.0 * 8.0 * Lanes / PixelClock * (1 - 2.4 / 100);
	} else {
		MaxLinkBPP = LinkBitRate / 10.0 * 8.0 * Lanes / PixelClock;
	}

	ODMMode = DSCEnable ? ODMModeDSC : ODMModeNoDSC;

	if (ODMMode == dml2_odm_mode_split_1to2) {
		MaxLinkBPP = 2 * MaxLinkBPP;
	}

	if (DesiredBPP == 0) {
		if (DSCEnable) {
			if (MaxLinkBPP < MinDSCBPP) {
				return __DML2_CALCS_DPP_INVALID__;
			} else if (MaxLinkBPP >= MaxDSCBPP) {
				return MaxDSCBPP;
			} else {
				return math_floor2(16.0 * MaxLinkBPP, 1.0) / 16.0;
			}
		} else {
			if (MaxLinkBPP >= NonDSCBPP2) {
				return NonDSCBPP2;
			} else if (MaxLinkBPP >= NonDSCBPP1) {
				return NonDSCBPP1;
			} else if (MaxLinkBPP >= NonDSCBPP0) {
				return NonDSCBPP0;
			} else {
				return __DML2_CALCS_DPP_INVALID__;
			}
		}
	} else {
		if (!((DSCEnable == false && (DesiredBPP == NonDSCBPP2 || DesiredBPP == NonDSCBPP1 || DesiredBPP == NonDSCBPP0)) ||
			(DSCEnable && DesiredBPP >= MinDSCBPP && DesiredBPP <= MaxDSCBPP))) {
			return __DML2_CALCS_DPP_INVALID__;
		} else {
			return DesiredBPP;
		}
	}
}

// updated for dcn4
static unsigned int dscceComputeDelay(
	unsigned int bpc,
	double BPP,
	unsigned int sliceWidth,
	unsigned int numSlices,
	enum dml2_output_format_class pixelFormat,
	enum dml2_output_encoder_class Output)
{
	// valid bpc = source bits per component in the set of {8, 10, 12}
	// valid bpp = increments of 1/16 of a bit
	// min = 6/7/8 in N420/N422/444, respectively
	// max = such that compression is 1:1
	//valid sliceWidth = number of pixels per slice line, must be less than or equal to 5184/numSlices (or 4096/numSlices in 420 mode)
	//valid numSlices = number of slices in the horiziontal direction per DSC engine in the set of {1, 2, 3, 4}
	//valid pixelFormat = pixel/color format in the set of {:N444_RGB, :S422, :N422, :N420}

	// fixed value
	unsigned int rcModelSize = 8192;

	// N422/N420 operate at 2 pixels per clock
	unsigned int pixelsPerClock, padding_pixels, ssm_group_priming_delay, ssm_pipeline_delay, obsm_pipeline_delay, slice_padded_pixels, ixd_plus_padding, ixd_plus_padding_groups, cycles_per_group, group_delay, pipeline_delay, pixels, additional_group_delay, lines_to_reach_ixd, groups_to_reach_ixd, slice_width_groups, initial_xmit_delay, number_of_lines_to_reach_ixd, slice_width_modified;

	if (pixelFormat == dml2_420)
		pixelsPerClock = 2;
	// #all other modes operate at 1 pixel per clock
	else if (pixelFormat == dml2_444)
		pixelsPerClock = 1;
	else if (pixelFormat == dml2_n422 || Output == dml2_hdmifrl)
		pixelsPerClock = 2;
	else
		pixelsPerClock = 1;

	//initial transmit delay as per PPS
	initial_xmit_delay = (unsigned int)(math_round(rcModelSize / 2.0 / BPP / pixelsPerClock));

	//slice width as seen by dscc_bcl in pixels or pixels pairs (depending on number of pixels per pixel container based on pixel format)
	slice_width_modified = (pixelFormat == dml2_444 || pixelFormat == dml2_420 || Output == dml2_hdmifrl) ? sliceWidth / 2 : sliceWidth;

	padding_pixels = ((slice_width_modified % 3) != 0) ? (3 - (slice_width_modified % 3)) * (initial_xmit_delay / slice_width_modified) : 0;

	if ((3.0 * pixelsPerClock * BPP) >= ((double)((initial_xmit_delay + 2) / 3) * (double)(3 + (pixelFormat == dml2_n422)))) {
		if ((initial_xmit_delay + padding_pixels) % 3 == 1) {
			initial_xmit_delay++;
		}
	}

	//sub-stream multiplexer balance fifo priming delay in groups as per dsc standard
	if (bpc == 8)
		ssm_group_priming_delay = 83;
	else if (bpc == 10)
		ssm_group_priming_delay = 91;
	else if (bpc == 12)
		ssm_group_priming_delay = 115;
	else if (bpc == 14)
		ssm_group_priming_delay = 123;
	else
		ssm_group_priming_delay = 128;

	//slice width in groups is rounded up to the nearest group as DSC adds padded pixels such that there are an integer number of groups per slice
	slice_width_groups = (slice_width_modified + 2) / 3;

	//determine number of padded pixels in the last group of a slice line, computed as
	slice_padded_pixels = 3 * slice_width_groups - slice_width_modified;

	//determine integer number of complete slice lines required to reach initial transmit delay without ssm delay considered
	number_of_lines_to_reach_ixd = initial_xmit_delay / slice_width_modified;

	//increase initial transmit delay by the number of padded pixels added to a slice line multipled by the integer number of complete lines to reach initial transmit delay
	//this step is necessary as each padded pixel added takes up a clock cycle and, therefore, adds to the overall delay
	ixd_plus_padding = initial_xmit_delay + slice_padded_pixels * number_of_lines_to_reach_ixd;

	//convert the padded initial transmit delay from pixels to groups by rounding up to the nearest group as DSC processes in groups of pixels
	ixd_plus_padding_groups = (ixd_plus_padding + 2) / 3;

	//number of groups required for a slice to reach initial transmit delay is the sum of the padded initial transmit delay plus the ssm group priming delay
	groups_to_reach_ixd = ixd_plus_padding_groups + ssm_group_priming_delay;

	//number of lines required to reach padded initial transmit delay in groups in slices to the left of the last horizontal slice
	//needs to be rounded up as a complete slice lines are buffered prior to initial transmit delay being reached in the last horizontal slice
	lines_to_reach_ixd = (groups_to_reach_ixd + slice_width_groups - 1) / slice_width_groups; //round up lines to reach ixd to next

	//determine if there are non-zero number of pixels reached in the group where initial transmit delay is reached
	//an additional group time (i.e., 3 pixel times) is required before the first output if there are no additional pixels beyond initial transmit delay
	additional_group_delay = ((initial_xmit_delay - number_of_lines_to_reach_ixd * slice_width_modified) % 3) == 0 ? 1 : 0;

	//number of pipeline delay cycles in the ssm block (can be determined empirically or analytically by inspecting the ssm block)
	ssm_pipeline_delay = 2;

	//number of pipe delay cycles in the obsm block (can be determined empirically or analytically by inspecting the obsm block)
	obsm_pipeline_delay = 1;

	//a group of pixels is worth 6 pixels in N422/N420 mode or 3 pixels in all other modes
	if (pixelFormat == dml2_420 || pixelFormat == dml2_444 || pixelFormat == dml2_n422 || Output == dml2_hdmifrl)
		cycles_per_group = 6;
	else
		cycles_per_group = 3;
	//delay of the bit stream contruction layer in pixels is the sum of:
	//1. number of pixel containers in a slice line multipled by the number of lines required to reach initial transmit delay multipled by number of slices to the left of the last horizontal slice
	//2. number of pixel containers required to reach initial transmit delay (specifically, in the last horizontal slice)
	//3. additional group of delay if initial transmit delay is reached exactly in a group
	//4. ssm and obsm pipeline delay (i.e., clock cycles of delay)
	group_delay = (lines_to_reach_ixd * slice_width_groups * (numSlices - 1)) + groups_to_reach_ixd + additional_group_delay;
	pipeline_delay = ssm_pipeline_delay + obsm_pipeline_delay;

	//pixel delay is group_delay (converted to pixels) + pipeline, however, first group is a special case since it is processed as soon as it arrives (i.e., in 3 cycles regardless of pixel format)
	pixels = (group_delay - 1) * cycles_per_group + 3 + pipeline_delay;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: bpc: %u\n", __func__, bpc);
	dml2_printf("DML::%s: BPP: %f\n", __func__, BPP);
	dml2_printf("DML::%s: sliceWidth: %u\n", __func__, sliceWidth);
	dml2_printf("DML::%s: numSlices: %u\n", __func__, numSlices);
	dml2_printf("DML::%s: pixelFormat: %u\n", __func__, pixelFormat);
	dml2_printf("DML::%s: Output: %u\n", __func__, Output);
	dml2_printf("DML::%s: pixels: %u\n", __func__, pixels);
#endif
	return pixels;
}

//updated in dcn4
static unsigned int dscComputeDelay(enum dml2_output_format_class pixelFormat, enum dml2_output_encoder_class Output)
{
	unsigned int Delay = 0;
	unsigned int dispclk_per_dscclk = 3;

	// sfr
	Delay = Delay + 2;

	if (pixelFormat == dml2_420 || pixelFormat == dml2_n422 || (Output == dml2_hdmifrl && pixelFormat != dml2_444)) {
		dispclk_per_dscclk = 3 * 2;
	}

	if (pixelFormat == dml2_420) {
		//dscc top delay for pixel compression layer
		Delay = Delay + 16 * dispclk_per_dscclk;

		// dscc - input deserializer
		Delay = Delay + 5;

		// dscc - input cdc fifo
		Delay = Delay + 1 + 4 * dispclk_per_dscclk;

		// dscc - output cdc fifo
		Delay = Delay + 3 + 1 * dispclk_per_dscclk;

		// dscc - cdc uncertainty
		Delay = Delay + 3 + 3 * dispclk_per_dscclk;
	} else if (pixelFormat == dml2_n422 || (Output == dml2_hdmifrl && pixelFormat != dml2_444)) {
		//dscc top delay for pixel compression layer
		Delay = Delay + 16 * dispclk_per_dscclk;
		// dsccif
		Delay = Delay + 1;
		// dscc - input deserializer
		Delay = Delay + 5;
		// dscc - input cdc fifo
		Delay = Delay + 1 + 4 * dispclk_per_dscclk;


		// dscc - output cdc fifo
		Delay = Delay + 3 + 1 * dispclk_per_dscclk;
		// dscc - cdc uncertainty
		Delay = Delay + 3 + 3 * dispclk_per_dscclk;
	} else if (pixelFormat == dml2_s422) {
		//dscc top delay for pixel compression layer
		Delay = Delay + 17 * dispclk_per_dscclk;

		// dscc - input deserializer
		Delay = Delay + 3;
		// dscc - input cdc fifo
		Delay = Delay + 1 + 4 * dispclk_per_dscclk;
		// dscc - output cdc fifo
		Delay = Delay + 3 + 1 * dispclk_per_dscclk;
		// dscc - cdc uncertainty
		Delay = Delay + 3 + 3 * dispclk_per_dscclk;
	} else {
		//dscc top delay for pixel compression layer
		Delay = Delay + 16 * dispclk_per_dscclk;
		// dscc - input deserializer
		Delay = Delay + 3;
		// dscc - input cdc fifo
		Delay = Delay + 1 + 4 * dispclk_per_dscclk;
		// dscc - output cdc fifo
		Delay = Delay + 3 + 1 * dispclk_per_dscclk;

		// dscc - cdc uncertainty
		Delay = Delay + 3 + 3 * dispclk_per_dscclk;
	}

	// sft
	Delay = Delay + 1;
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: pixelFormat = %u\n", __func__, pixelFormat);
	dml2_printf("DML::%s: Delay = %u\n", __func__, Delay);
#endif

	return Delay;
}

static unsigned int CalculateHostVMDynamicLevels(
	bool GPUVMEnable,
	bool HostVMEnable,
	unsigned int HostVMMinPageSize,
	unsigned int HostVMMaxNonCachedPageTableLevels)
{
	unsigned int HostVMDynamicLevels = 0;

	if (GPUVMEnable && HostVMEnable) {
		if (HostVMMinPageSize < 2048)
			HostVMDynamicLevels = HostVMMaxNonCachedPageTableLevels;
		else if (HostVMMinPageSize >= 2048 && HostVMMinPageSize < 1048576)
			HostVMDynamicLevels = (unsigned int)math_max2(0, (double)HostVMMaxNonCachedPageTableLevels - 1);
		else
			HostVMDynamicLevels = (unsigned int)math_max2(0, (double)HostVMMaxNonCachedPageTableLevels - 2);
	} else {
		HostVMDynamicLevels = 0;
	}
	return HostVMDynamicLevels;
}

static unsigned int CalculateVMAndRowBytes(struct dml2_core_shared_calculate_vm_and_row_bytes_params *p)
{
	unsigned int extra_dpde_bytes;
	unsigned int extra_mpde_bytes;
	unsigned int MacroTileSizeBytes;
	unsigned int vp_height_dpte_ub;

	unsigned int meta_surface_bytes;
	unsigned int vm_bytes;
	unsigned int vp_height_meta_ub;
	unsigned int PixelPTEReqWidth_linear = 0; // VBA_DELTA. VBA doesn't calculate this

	*p->MetaRequestHeight = 8 * p->BlockHeight256Bytes;
	*p->MetaRequestWidth = 8 * p->BlockWidth256Bytes;
	if (p->SurfaceTiling == dml2_sw_linear) {
		*p->meta_row_height = 32;
		*p->meta_row_width = (unsigned int)(math_floor2(p->ViewportXStart + p->SwathWidth + *p->MetaRequestWidth - 1, *p->MetaRequestWidth) - math_floor2(p->ViewportXStart, *p->MetaRequestWidth));
		*p->meta_row_bytes = (unsigned int)(*p->meta_row_width * *p->MetaRequestHeight * p->BytePerPixel / 256.0); // FIXME_DCN4SW missing in old code but no dcc for linear anyways?
	} else if (!dml_is_vertical_rotation(p->RotationAngle)) {
		*p->meta_row_height = *p->MetaRequestHeight;
		if (p->ViewportStationary && p->NumberOfDPPs == 1) {
			*p->meta_row_width = (unsigned int)(math_floor2(p->ViewportXStart + p->SwathWidth + *p->MetaRequestWidth - 1, *p->MetaRequestWidth) - math_floor2(p->ViewportXStart, *p->MetaRequestWidth));
		} else {
			*p->meta_row_width = (unsigned int)(math_ceil2(p->SwathWidth - 1, *p->MetaRequestWidth) + *p->MetaRequestWidth);
		}
		*p->meta_row_bytes = (unsigned int)(*p->meta_row_width * *p->MetaRequestHeight * p->BytePerPixel / 256.0);
	} else {
		*p->meta_row_height = *p->MetaRequestWidth;
		if (p->ViewportStationary && p->NumberOfDPPs == 1) {
			*p->meta_row_width = (unsigned int)(math_floor2(p->ViewportYStart + p->ViewportHeight + *p->MetaRequestHeight - 1, *p->MetaRequestHeight) - math_floor2(p->ViewportYStart, *p->MetaRequestHeight));
		} else {
			*p->meta_row_width = (unsigned int)(math_ceil2(p->SwathWidth - 1, *p->MetaRequestHeight) + *p->MetaRequestHeight);
		}
		*p->meta_row_bytes = (unsigned int)(*p->meta_row_width * *p->MetaRequestWidth * p->BytePerPixel / 256.0);
	}

	if (p->ViewportStationary && p->is_phantom && (p->NumberOfDPPs == 1 || !dml_is_vertical_rotation(p->RotationAngle))) {
		vp_height_meta_ub = (unsigned int)(math_floor2(p->ViewportYStart + p->ViewportHeight + 64 * p->BlockHeight256Bytes - 1, 64 * p->BlockHeight256Bytes) - math_floor2(p->ViewportYStart, 64 * p->BlockHeight256Bytes));
	} else if (!dml_is_vertical_rotation(p->RotationAngle)) {
		vp_height_meta_ub = (unsigned int)(math_ceil2(p->ViewportHeight - 1, 64 * p->BlockHeight256Bytes) + 64 * p->BlockHeight256Bytes);
	} else {
		vp_height_meta_ub = (unsigned int)(math_ceil2(p->SwathWidth - 1, 64 * p->BlockHeight256Bytes) + 64 * p->BlockHeight256Bytes);
	}

	meta_surface_bytes = (unsigned int)(p->DCCMetaPitch * vp_height_meta_ub * p->BytePerPixel / 256.0);
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: DCCMetaPitch = %u\n", __func__, p->DCCMetaPitch);
	dml2_printf("DML::%s: meta_surface_bytes = %u\n", __func__, meta_surface_bytes);
#endif
	if (p->GPUVMEnable == true) {
		double meta_vmpg_bytes = 4.0 * 1024.0;
		*p->meta_pte_bytes_per_frame_ub = (unsigned int)((math_ceil2((double) (meta_surface_bytes - meta_vmpg_bytes) / (8 * meta_vmpg_bytes), 1) + 1) * 64);
		extra_mpde_bytes = 128 * (p->GPUVMMaxPageTableLevels - 1);
	} else {
		*p->meta_pte_bytes_per_frame_ub = 0;
		extra_mpde_bytes = 0;
	}

	if (!p->DCCEnable || !p->mrq_present) {
		*p->meta_pte_bytes_per_frame_ub = 0;
		extra_mpde_bytes = 0;
		*p->meta_row_bytes = 0;
	}

	if (!p->GPUVMEnable) {
		*p->PixelPTEBytesPerRow = 0;
		*p->PixelPTEBytesPerRowStorage = 0;
		*p->dpte_row_width_ub = 0;
		*p->dpte_row_height = 0;
		*p->dpte_row_height_linear = 0;
		*p->PixelPTEBytesPerRow_one_row_per_frame = 0;
		*p->dpte_row_width_ub_one_row_per_frame = 0;
		*p->dpte_row_height_one_row_per_frame = 0;
		*p->vmpg_width = 0;
		*p->vmpg_height = 0;
		*p->PixelPTEReqWidth = 0;
		*p->PixelPTEReqHeight = 0;
		*p->PTERequestSize = 0;
		*p->dpde0_bytes_per_frame_ub = 0;
		return 0;
	}

	MacroTileSizeBytes = p->MacroTileWidth * p->BytePerPixel * p->MacroTileHeight;

	if (p->ViewportStationary && p->is_phantom && (p->NumberOfDPPs == 1 || !dml_is_vertical_rotation(p->RotationAngle))) {
		vp_height_dpte_ub = (unsigned int)(math_floor2(p->ViewportYStart + p->ViewportHeight + p->MacroTileHeight - 1, p->MacroTileHeight) - math_floor2(p->ViewportYStart, p->MacroTileHeight));
	} else if (!dml_is_vertical_rotation(p->RotationAngle)) {
		vp_height_dpte_ub = (unsigned int)(math_ceil2((double)p->ViewportHeight - 1, p->MacroTileHeight) + p->MacroTileHeight);
	} else {
		vp_height_dpte_ub = (unsigned int)(math_ceil2((double)p->SwathWidth - 1, p->MacroTileHeight) + p->MacroTileHeight);
	}

	if (p->GPUVMEnable == true && p->GPUVMMaxPageTableLevels > 1) {
		*p->dpde0_bytes_per_frame_ub = (unsigned int)(64 * (math_ceil2((double)(p->Pitch * vp_height_dpte_ub * p->BytePerPixel - MacroTileSizeBytes) / (double)(8 * 2097152), 1) + 1));
		extra_dpde_bytes = 128 * (p->GPUVMMaxPageTableLevels - 2);
	} else {
		*p->dpde0_bytes_per_frame_ub = 0;
		extra_dpde_bytes = 0;
	}

	vm_bytes = *p->meta_pte_bytes_per_frame_ub + extra_mpde_bytes + *p->dpde0_bytes_per_frame_ub + extra_dpde_bytes;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: DCCEnable = %u\n", __func__, p->DCCEnable);
	dml2_printf("DML::%s: GPUVMEnable = %u\n", __func__, p->GPUVMEnable);
	dml2_printf("DML::%s: SwModeLinear = %u\n", __func__, p->SurfaceTiling == dml2_sw_linear);
	dml2_printf("DML::%s: BytePerPixel = %u\n", __func__, p->BytePerPixel);
	dml2_printf("DML::%s: GPUVMMaxPageTableLevels = %u\n", __func__, p->GPUVMMaxPageTableLevels);
	dml2_printf("DML::%s: BlockHeight256Bytes = %u\n", __func__, p->BlockHeight256Bytes);
	dml2_printf("DML::%s: BlockWidth256Bytes = %u\n", __func__, p->BlockWidth256Bytes);
	dml2_printf("DML::%s: MacroTileHeight = %u\n", __func__, p->MacroTileHeight);
	dml2_printf("DML::%s: MacroTileWidth = %u\n", __func__, p->MacroTileWidth);
	dml2_printf("DML::%s: meta_pte_bytes_per_frame_ub = %u\n", __func__, *p->meta_pte_bytes_per_frame_ub);
	dml2_printf("DML::%s: dpde0_bytes_per_frame_ub = %u\n", __func__, *p->dpde0_bytes_per_frame_ub);
	dml2_printf("DML::%s: extra_mpde_bytes = %u\n", __func__, extra_mpde_bytes);
	dml2_printf("DML::%s: extra_dpde_bytes = %u\n", __func__, extra_dpde_bytes);
	dml2_printf("DML::%s: vm_bytes = %u\n", __func__, vm_bytes);
	dml2_printf("DML::%s: ViewportHeight = %u\n", __func__, p->ViewportHeight);
	dml2_printf("DML::%s: SwathWidth = %u\n", __func__, p->SwathWidth);
	dml2_printf("DML::%s: vp_height_dpte_ub = %u\n", __func__, vp_height_dpte_ub);
#endif

	if (p->SurfaceTiling == dml2_sw_linear) {
		*p->PixelPTEReqHeight = 1;
		*p->PixelPTEReqWidth = p->GPUVMMinPageSizeKBytes * 1024 * 8 / p->BytePerPixel;
		PixelPTEReqWidth_linear = p->GPUVMMinPageSizeKBytes * 1024 * 8 / p->BytePerPixel;
		*p->PTERequestSize = 64;

		*p->vmpg_height = 1;
		*p->vmpg_width = p->GPUVMMinPageSizeKBytes * 1024 / p->BytePerPixel;
	} else if (p->GPUVMMinPageSizeKBytes * 1024 >= dml_get_tile_block_size_bytes(p->SurfaceTiling)) { // 1 64B 8x1 PTE
		*p->PixelPTEReqHeight = p->MacroTileHeight;
		*p->PixelPTEReqWidth = 8 * 1024 * p->GPUVMMinPageSizeKBytes / (p->MacroTileHeight * p->BytePerPixel);
		*p->PTERequestSize = 64;

		*p->vmpg_height = p->MacroTileHeight;
		*p->vmpg_width = 1024 * p->GPUVMMinPageSizeKBytes / (p->MacroTileHeight * p->BytePerPixel);

	} else if (p->GPUVMMinPageSizeKBytes == 4 && dml_get_tile_block_size_bytes(p->SurfaceTiling) == 65536) { // 2 64B PTE requests to get 16 PTEs to cover the 64K tile
		// one 64KB tile, is 16x16x256B req
		*p->PixelPTEReqHeight = 16 * p->BlockHeight256Bytes;
		*p->PixelPTEReqWidth = 16 * p->BlockWidth256Bytes;
		*p->PTERequestSize = 128;

		*p->vmpg_height = *p->PixelPTEReqHeight;
		*p->vmpg_width = *p->PixelPTEReqWidth;
	} else {
		// default for rest of calculation to go through, when vm is disable, the calulated pte related values shouldnt be used anyways
		*p->PixelPTEReqHeight = p->MacroTileHeight;
		*p->PixelPTEReqWidth = 8 * 1024 * p->GPUVMMinPageSizeKBytes / (p->MacroTileHeight * p->BytePerPixel);
		*p->PTERequestSize = 64;

		*p->vmpg_height = p->MacroTileHeight;
		*p->vmpg_width = 1024 * p->GPUVMMinPageSizeKBytes / (p->MacroTileHeight * p->BytePerPixel);

		if (p->GPUVMEnable == true) {
			dml2_printf("DML::%s: GPUVMMinPageSizeKBytes=%u and sw_mode=%u (tile_size=%d) not supported!\n",
				__func__, p->GPUVMMinPageSizeKBytes, p->SurfaceTiling, dml_get_tile_block_size_bytes(p->SurfaceTiling));
			DML2_ASSERT(0);
		}
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: GPUVMMinPageSizeKBytes = %u\n", __func__, p->GPUVMMinPageSizeKBytes);
	dml2_printf("DML::%s: PixelPTEReqHeight = %u\n", __func__, *p->PixelPTEReqHeight);
	dml2_printf("DML::%s: PixelPTEReqWidth = %u\n", __func__, *p->PixelPTEReqWidth);
	dml2_printf("DML::%s: PixelPTEReqWidth_linear = %u\n", __func__, PixelPTEReqWidth_linear);
	dml2_printf("DML::%s: PTERequestSize = %u\n", __func__, *p->PTERequestSize);
	dml2_printf("DML::%s: Pitch = %u\n", __func__, p->Pitch);
	dml2_printf("DML::%s: vmpg_width = %u\n", __func__, *p->vmpg_width);
	dml2_printf("DML::%s: vmpg_height = %u\n", __func__, *p->vmpg_height);
#endif

	*p->dpte_row_height_one_row_per_frame = vp_height_dpte_ub;
	*p->dpte_row_width_ub_one_row_per_frame = (unsigned int)((math_ceil2(((double)p->Pitch * (double)*p->dpte_row_height_one_row_per_frame / (double)*p->PixelPTEReqHeight - 1) / (double)*p->PixelPTEReqWidth, 1) + 1) * (double)*p->PixelPTEReqWidth);
	*p->PixelPTEBytesPerRow_one_row_per_frame = (unsigned int)((double)*p->dpte_row_width_ub_one_row_per_frame / (double)*p->PixelPTEReqWidth * *p->PTERequestSize);
	*p->dpte_row_height_linear = 0;

	if (p->SurfaceTiling == dml2_sw_linear) {
		*p->dpte_row_height = (unsigned int)(math_min2(128, (double)(1ULL << (unsigned int)math_floor2(math_log((float)(p->PTEBufferSizeInRequests * *p->PixelPTEReqWidth / p->Pitch), 2.0), 1))));
		*p->dpte_row_width_ub = (unsigned int)(math_ceil2(((double)p->Pitch * (double)*p->dpte_row_height - 1), (double)*p->PixelPTEReqWidth) + *p->PixelPTEReqWidth);
		*p->PixelPTEBytesPerRow = (unsigned int)((double)*p->dpte_row_width_ub / (double)*p->PixelPTEReqWidth * *p->PTERequestSize);

		// VBA_DELTA, VBA doesn't have programming value for pte row height linear.
		*p->dpte_row_height_linear = (unsigned int)1 << (unsigned int)math_floor2(math_log((float)(p->PTEBufferSizeInRequests * PixelPTEReqWidth_linear / p->Pitch), 2.0), 1);
		if (*p->dpte_row_height_linear > 128)
			*p->dpte_row_height_linear = 128;

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: dpte_row_width_ub = %u (linear)\n", __func__, *p->dpte_row_width_ub);
#endif

	} else if (!dml_is_vertical_rotation(p->RotationAngle)) {
		*p->dpte_row_height = *p->PixelPTEReqHeight;

		if (p->GPUVMMinPageSizeKBytes > 64) {
			*p->dpte_row_width_ub = (unsigned int)((math_ceil2(((double)p->Pitch * (double)*p->dpte_row_height / (double)*p->PixelPTEReqHeight - 1) / (double)*p->PixelPTEReqWidth, 1) + 1) * *p->PixelPTEReqWidth);
		} else if (p->ViewportStationary && (p->NumberOfDPPs == 1)) {
			*p->dpte_row_width_ub = (unsigned int)(math_floor2(p->ViewportXStart + p->SwathWidth + *p->PixelPTEReqWidth - 1, *p->PixelPTEReqWidth) - math_floor2(p->ViewportXStart, *p->PixelPTEReqWidth));
		} else {
			*p->dpte_row_width_ub = (unsigned int)((math_ceil2((double)(p->SwathWidth - 1) / (double)*p->PixelPTEReqWidth, 1) + 1.0) * *p->PixelPTEReqWidth);
		}
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: dpte_row_width_ub = %u (tiled horz)\n", __func__, *p->dpte_row_width_ub);
#endif

		*p->PixelPTEBytesPerRow = *p->dpte_row_width_ub / *p->PixelPTEReqWidth * *p->PTERequestSize;
	} else {
		*p->dpte_row_height = (unsigned int)(math_min2(*p->PixelPTEReqWidth, p->MacroTileWidth));

		if (p->ViewportStationary && (p->NumberOfDPPs == 1)) {
			*p->dpte_row_width_ub = (unsigned int)(math_floor2(p->ViewportYStart + p->ViewportHeight + *p->PixelPTEReqHeight - 1, *p->PixelPTEReqHeight) - math_floor2(p->ViewportYStart, *p->PixelPTEReqHeight));
		} else {
			*p->dpte_row_width_ub = (unsigned int)((math_ceil2((double)(p->SwathWidth - 1) / (double)*p->PixelPTEReqHeight, 1) + 1) * *p->PixelPTEReqHeight);
		}

		*p->PixelPTEBytesPerRow = (unsigned int)((double)*p->dpte_row_width_ub / (double)*p->PixelPTEReqHeight * *p->PTERequestSize);
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: dpte_row_width_ub = %u (tiled vert)\n", __func__, *p->dpte_row_width_ub);
#endif
	}

	if (p->GPUVMEnable != true) {
		*p->PixelPTEBytesPerRow = 0;
		*p->PixelPTEBytesPerRow_one_row_per_frame = 0;
	}

	*p->PixelPTEBytesPerRowStorage = *p->PixelPTEBytesPerRow;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: GPUVMMinPageSizeKBytes = %u\n", __func__, p->GPUVMMinPageSizeKBytes);
	dml2_printf("DML::%s: GPUVMEnable = %u\n", __func__, p->GPUVMEnable);
	dml2_printf("DML::%s: meta_row_height = %u\n", __func__, *p->meta_row_height);
	dml2_printf("DML::%s: dpte_row_height = %u\n", __func__, *p->dpte_row_height);
	dml2_printf("DML::%s: dpte_row_height_linear = %u\n", __func__, *p->dpte_row_height_linear);
	dml2_printf("DML::%s: dpte_row_width_ub = %u\n", __func__, *p->dpte_row_width_ub);
	dml2_printf("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, *p->PixelPTEBytesPerRow);
	dml2_printf("DML::%s: PixelPTEBytesPerRowStorage = %u\n", __func__, *p->PixelPTEBytesPerRowStorage);
	dml2_printf("DML::%s: PTEBufferSizeInRequests = %u\n", __func__, p->PTEBufferSizeInRequests);
	dml2_printf("DML::%s: dpte_row_height_one_row_per_frame = %u\n", __func__, *p->dpte_row_height_one_row_per_frame);
	dml2_printf("DML::%s: dpte_row_width_ub_one_row_per_frame = %u\n", __func__, *p->dpte_row_width_ub_one_row_per_frame);
	dml2_printf("DML::%s: PixelPTEBytesPerRow_one_row_per_frame = %u\n", __func__, *p->PixelPTEBytesPerRow_one_row_per_frame);
#endif

	return vm_bytes;
} // CalculateVMAndRowBytes

static unsigned int CalculatePrefetchSourceLines(
	double VRatio,
	unsigned int VTaps,
	bool Interlace,
	bool ProgressiveToInterlaceUnitInOPP,
	unsigned int SwathHeight,
	enum dml2_rotation_angle RotationAngle,
	bool mirrored,
	bool ViewportStationary,
	unsigned int SwathWidth,
	unsigned int ViewportHeight,
	unsigned int ViewportXStart,
	unsigned int ViewportYStart,

	// Output
	unsigned int *VInitPreFill,
	unsigned int *MaxNumSwath)
{

	unsigned int vp_start_rot = 0;
	unsigned int sw0_tmp = 0;
	unsigned int MaxPartialSwath = 0;
	double numLines = 0;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: VRatio = %f\n", __func__, VRatio);
	dml2_printf("DML::%s: VTaps = %u\n", __func__, VTaps);
	dml2_printf("DML::%s: ViewportXStart = %u\n", __func__, ViewportXStart);
	dml2_printf("DML::%s: ViewportYStart = %u\n", __func__, ViewportYStart);
	dml2_printf("DML::%s: ViewportStationary = %u\n", __func__, ViewportStationary);
	dml2_printf("DML::%s: SwathHeight = %u\n", __func__, SwathHeight);
#endif
	if (ProgressiveToInterlaceUnitInOPP)
		*VInitPreFill = (unsigned int)(math_floor2((VRatio + (double)VTaps + 1) / 2.0, 1));
	else
		*VInitPreFill = (unsigned int)(math_floor2((VRatio + (double)VTaps + 1 + (Interlace ? 1 : 0) * 0.5 * VRatio) / 2.0, 1));

	if (ViewportStationary) {
		if (RotationAngle == dml2_rotation_180) {
			vp_start_rot = SwathHeight - (((unsigned int)(ViewportYStart + ViewportHeight - 1) % SwathHeight) + 1);
		} else if ((RotationAngle == dml2_rotation_270 && !mirrored) || (RotationAngle == dml2_rotation_90 && mirrored)) {
			vp_start_rot = ViewportXStart;
		} else if ((RotationAngle == dml2_rotation_90 && !mirrored) || (RotationAngle == dml2_rotation_270 && mirrored)) {
			vp_start_rot = SwathHeight - (((unsigned int)(ViewportYStart + SwathWidth - 1) % SwathHeight) + 1);
		} else {
			vp_start_rot = ViewportYStart;
		}
		sw0_tmp = SwathHeight - (vp_start_rot % SwathHeight);
		if (sw0_tmp < *VInitPreFill) {
			*MaxNumSwath = (unsigned int)(math_ceil2((*VInitPreFill - sw0_tmp) / (double)SwathHeight, 1) + 1);
		} else {
			*MaxNumSwath = 1;
		}
		MaxPartialSwath = (unsigned int)(math_max2(1, (unsigned int)(vp_start_rot + *VInitPreFill - 1) % SwathHeight));
	} else {
		*MaxNumSwath = (unsigned int)(math_ceil2((*VInitPreFill - 1.0) / (double)SwathHeight, 1) + 1);
		if (*VInitPreFill > 1) {
			MaxPartialSwath = (unsigned int)(math_max2(1, (unsigned int)(*VInitPreFill - 2) % SwathHeight));
		} else {
			MaxPartialSwath = (unsigned int)(math_max2(1, (unsigned int)(*VInitPreFill + SwathHeight - 2) % SwathHeight));
		}
	}
	numLines = *MaxNumSwath * SwathHeight + MaxPartialSwath;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: vp_start_rot = %u\n", __func__, vp_start_rot);
	dml2_printf("DML::%s: VInitPreFill = %u\n", __func__, *VInitPreFill);
	dml2_printf("DML::%s: MaxPartialSwath = %u\n", __func__, MaxPartialSwath);
	dml2_printf("DML::%s: MaxNumSwath = %u\n", __func__, *MaxNumSwath);
	dml2_printf("DML::%s: Prefetch source lines = %3.2f\n", __func__, numLines);
#endif
	return (unsigned int)(numLines);

}

static void CalculateRowBandwidth(
	bool GPUVMEnable,
	bool use_one_row_for_frame,
	enum dml2_source_format_class SourcePixelFormat,
	double VRatio,
	double VRatioChroma,
	bool DCCEnable,
	double LineTime,
	unsigned int PixelPTEBytesPerRowLuma,
	unsigned int PixelPTEBytesPerRowChroma,
	unsigned int dpte_row_height_luma,
	unsigned int dpte_row_height_chroma,

	bool mrq_present,
	unsigned int meta_row_bytes_per_row_ub_l,
	unsigned int meta_row_bytes_per_row_ub_c,
	unsigned int meta_row_height_luma,
	unsigned int meta_row_height_chroma,

	// Output
	double *dpte_row_bw,
	double *meta_row_bw)
{
	if (!DCCEnable || !mrq_present) {
		*meta_row_bw = 0;
	} else if (dml_is_420(SourcePixelFormat) || SourcePixelFormat == dml2_rgbe_alpha) {
		*meta_row_bw = VRatio * meta_row_bytes_per_row_ub_l / (meta_row_height_luma * LineTime)
				+ VRatioChroma * meta_row_bytes_per_row_ub_c / (meta_row_height_chroma * LineTime);
	} else {
		*meta_row_bw = VRatio * meta_row_bytes_per_row_ub_l / (meta_row_height_luma * LineTime);
	}

	if (GPUVMEnable != true) {
		*dpte_row_bw = 0;
	} else if (dml_is_420(SourcePixelFormat) || SourcePixelFormat == dml2_rgbe_alpha) {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime)
			+ VRatioChroma * PixelPTEBytesPerRowChroma / (dpte_row_height_chroma * LineTime);
	} else {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime);
	}
}

static void CalculateMALLUseForStaticScreen(
	const struct dml2_display_cfg *display_cfg,
	unsigned int NumberOfActiveSurfaces,
	unsigned int MALLAllocatedForDCN,
	unsigned int SurfaceSizeInMALL[],
	bool one_row_per_frame_fits_in_buffer[],

	// Output
	bool is_using_mall_for_ss[])
{

	unsigned int SurfaceToAddToMALL;
	bool CanAddAnotherSurfaceToMALL;
	unsigned int TotalSurfaceSizeInMALL;

	TotalSurfaceSizeInMALL = 0;
	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		is_using_mall_for_ss[k] = (display_cfg->plane_descriptors[k].overrides.refresh_from_mall == dml2_refresh_from_mall_mode_override_force_enable);
		if (is_using_mall_for_ss[k])
			TotalSurfaceSizeInMALL = TotalSurfaceSizeInMALL + SurfaceSizeInMALL[k];
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, is_using_mall_for_ss = %u\n", __func__, k, is_using_mall_for_ss[k]);
		dml2_printf("DML::%s: k=%u, TotalSurfaceSizeInMALL = %u\n", __func__, k, TotalSurfaceSizeInMALL);
#endif
	}

	SurfaceToAddToMALL = 0;
	CanAddAnotherSurfaceToMALL = true;
	while (CanAddAnotherSurfaceToMALL) {
		CanAddAnotherSurfaceToMALL = false;
		for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
			if (TotalSurfaceSizeInMALL + SurfaceSizeInMALL[k] <= MALLAllocatedForDCN * 1024 * 1024 &&
				!is_using_mall_for_ss[k] && display_cfg->plane_descriptors[k].overrides.refresh_from_mall != dml2_refresh_from_mall_mode_override_force_disable && one_row_per_frame_fits_in_buffer[k] &&
				(!CanAddAnotherSurfaceToMALL || SurfaceSizeInMALL[k] < SurfaceSizeInMALL[SurfaceToAddToMALL])) {
				CanAddAnotherSurfaceToMALL = true;
				SurfaceToAddToMALL = k;
				dml2_printf("DML::%s: k=%u, UseMALLForStaticScreen = %u (dis, en, optimize)\n", __func__, k, display_cfg->plane_descriptors[k].overrides.refresh_from_mall);
			}
		}
		if (CanAddAnotherSurfaceToMALL) {
			is_using_mall_for_ss[SurfaceToAddToMALL] = true;
			TotalSurfaceSizeInMALL = TotalSurfaceSizeInMALL + SurfaceSizeInMALL[SurfaceToAddToMALL];

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: SurfaceToAddToMALL = %u\n", __func__, SurfaceToAddToMALL);
			dml2_printf("DML::%s: TotalSurfaceSizeInMALL = %u\n", __func__, TotalSurfaceSizeInMALL);
#endif
		}
	}
}

static void CalculateDCCConfiguration(
	bool DCCEnabled,
	bool DCCProgrammingAssumesScanDirectionUnknown,
	enum dml2_source_format_class SourcePixelFormat,
	unsigned int SurfaceWidthLuma,
	unsigned int SurfaceWidthChroma,
	unsigned int SurfaceHeightLuma,
	unsigned int SurfaceHeightChroma,
	unsigned int nomDETInKByte,
	unsigned int RequestHeight256ByteLuma,
	unsigned int RequestHeight256ByteChroma,
	enum dml2_swizzle_mode TilingFormat,
	unsigned int BytePerPixelY,
	unsigned int BytePerPixelC,
	double BytePerPixelDETY,
	double BytePerPixelDETC,
	enum dml2_rotation_angle RotationAngle,

	// Output
	enum dml2_core_internal_request_type *RequestLuma,
	enum dml2_core_internal_request_type *RequestChroma,
	unsigned int *MaxUncompressedBlockLuma,
	unsigned int *MaxUncompressedBlockChroma,
	unsigned int *MaxCompressedBlockLuma,
	unsigned int *MaxCompressedBlockChroma,
	unsigned int *IndependentBlockLuma,
	unsigned int *IndependentBlockChroma)
{
	unsigned int DETBufferSizeForDCC = nomDETInKByte * 1024;

	unsigned int segment_order_horz_contiguous_luma;
	unsigned int segment_order_horz_contiguous_chroma;
	unsigned int segment_order_vert_contiguous_luma;
	unsigned int segment_order_vert_contiguous_chroma;

	unsigned int req128_horz_wc_l;
	unsigned int req128_horz_wc_c;
	unsigned int req128_vert_wc_l;
	unsigned int req128_vert_wc_c;

	unsigned int yuv420;
	unsigned int horz_div_l;
	unsigned int horz_div_c;
	unsigned int vert_div_l;
	unsigned int vert_div_c;

	unsigned int swath_buf_size;
	double detile_buf_vp_horz_limit;
	double detile_buf_vp_vert_limit;

	unsigned int MAS_vp_horz_limit;
	unsigned int MAS_vp_vert_limit;
	unsigned int max_vp_horz_width;
	unsigned int max_vp_vert_height;
	unsigned int eff_surf_width_l;
	unsigned int eff_surf_width_c;
	unsigned int eff_surf_height_l;
	unsigned int eff_surf_height_c;

	unsigned int full_swath_bytes_horz_wc_l;
	unsigned int full_swath_bytes_horz_wc_c;
	unsigned int full_swath_bytes_vert_wc_l;
	unsigned int full_swath_bytes_vert_wc_c;

	if (dml_is_420(SourcePixelFormat))
		yuv420 = 1;
	else
		yuv420 = 0;
	horz_div_l = 1;
	horz_div_c = 1;
	vert_div_l = 1;
	vert_div_c = 1;

	if (BytePerPixelY == 1)
		vert_div_l = 0;
	if (BytePerPixelC == 1)
		vert_div_c = 0;

	if (BytePerPixelC == 0) {
		swath_buf_size = DETBufferSizeForDCC / 2 - 2 * 256;
		detile_buf_vp_horz_limit = (double)swath_buf_size / ((double)RequestHeight256ByteLuma * BytePerPixelY / (1 + horz_div_l));
		detile_buf_vp_vert_limit = (double)swath_buf_size / (256.0 / RequestHeight256ByteLuma / (1 + vert_div_l));
	} else {
		swath_buf_size = DETBufferSizeForDCC / 2 - 2 * 2 * 256;
		detile_buf_vp_horz_limit = (double)swath_buf_size / ((double)RequestHeight256ByteLuma * BytePerPixelY / (1 + horz_div_l) + (double)RequestHeight256ByteChroma * BytePerPixelC / (1 + horz_div_c) / (1 + yuv420));
		detile_buf_vp_vert_limit = (double)swath_buf_size / (256.0 / RequestHeight256ByteLuma / (1 + vert_div_l) + 256.0 / RequestHeight256ByteChroma / (1 + vert_div_c) / (1 + yuv420));
	}

	if (SourcePixelFormat == dml2_420_10) {
		detile_buf_vp_horz_limit = 1.5 * detile_buf_vp_horz_limit;
		detile_buf_vp_vert_limit = 1.5 * detile_buf_vp_vert_limit;
	}

	detile_buf_vp_horz_limit = math_floor2(detile_buf_vp_horz_limit - 1, 16);
	detile_buf_vp_vert_limit = math_floor2(detile_buf_vp_vert_limit - 1, 16);

	MAS_vp_horz_limit = SourcePixelFormat == dml2_rgbe_alpha ? 3840 : 6144;
	MAS_vp_vert_limit = SourcePixelFormat == dml2_rgbe_alpha ? 3840 : (BytePerPixelY == 8 ? 3072 : 6144);
	max_vp_horz_width = (unsigned int)(math_min2((double)MAS_vp_horz_limit, detile_buf_vp_horz_limit));
	max_vp_vert_height = (unsigned int)(math_min2((double)MAS_vp_vert_limit, detile_buf_vp_vert_limit));
	eff_surf_width_l = (SurfaceWidthLuma > max_vp_horz_width ? max_vp_horz_width : SurfaceWidthLuma);
	eff_surf_width_c = eff_surf_width_l / (1 + yuv420);
	eff_surf_height_l = (SurfaceHeightLuma > max_vp_vert_height ? max_vp_vert_height : SurfaceHeightLuma);
	eff_surf_height_c = eff_surf_height_l / (1 + yuv420);

	full_swath_bytes_horz_wc_l = eff_surf_width_l * RequestHeight256ByteLuma * BytePerPixelY;
	full_swath_bytes_vert_wc_l = eff_surf_height_l * 256 / RequestHeight256ByteLuma;
	if (BytePerPixelC > 0) {
		full_swath_bytes_horz_wc_c = eff_surf_width_c * RequestHeight256ByteChroma * BytePerPixelC;
		full_swath_bytes_vert_wc_c = eff_surf_height_c * 256 / RequestHeight256ByteChroma;
	} else {
		full_swath_bytes_horz_wc_c = 0;
		full_swath_bytes_vert_wc_c = 0;
	}

	if (SourcePixelFormat == dml2_420_10) {
		full_swath_bytes_horz_wc_l = (unsigned int)(math_ceil2((double)full_swath_bytes_horz_wc_l * 2.0 / 3.0, 256.0));
		full_swath_bytes_horz_wc_c = (unsigned int)(math_ceil2((double)full_swath_bytes_horz_wc_c * 2.0 / 3.0, 256.0));
		full_swath_bytes_vert_wc_l = (unsigned int)(math_ceil2((double)full_swath_bytes_vert_wc_l * 2.0 / 3.0, 256.0));
		full_swath_bytes_vert_wc_c = (unsigned int)(math_ceil2((double)full_swath_bytes_vert_wc_c * 2.0 / 3.0, 256.0));
	}

	if (2 * full_swath_bytes_horz_wc_l + 2 * full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 0;
		req128_horz_wc_c = 0;
	} else if (full_swath_bytes_horz_wc_l < 1.5 * full_swath_bytes_horz_wc_c && 2 * full_swath_bytes_horz_wc_l + full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 0;
		req128_horz_wc_c = 1;
	} else if (full_swath_bytes_horz_wc_l >= 1.5 * full_swath_bytes_horz_wc_c && full_swath_bytes_horz_wc_l + 2 * full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 0;
	} else {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 1;
	}

	if (2 * full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 0;
	} else if (full_swath_bytes_vert_wc_l < 1.5 * full_swath_bytes_vert_wc_c && 2 * full_swath_bytes_vert_wc_l + full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 1;
	} else if (full_swath_bytes_vert_wc_l >= 1.5 * full_swath_bytes_vert_wc_c && full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 1;
		req128_vert_wc_c = 0;
	} else {
		req128_vert_wc_l = 1;
		req128_vert_wc_c = 1;
	}

	if (BytePerPixelY == 2) {
		segment_order_horz_contiguous_luma = 0;
		segment_order_vert_contiguous_luma = 1;
	} else {
		segment_order_horz_contiguous_luma = 1;
		segment_order_vert_contiguous_luma = 0;
	}

	if (BytePerPixelC == 2) {
		segment_order_horz_contiguous_chroma = 0;
		segment_order_vert_contiguous_chroma = 1;
	} else {
		segment_order_horz_contiguous_chroma = 1;
		segment_order_vert_contiguous_chroma = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: DCCEnabled = %u\n", __func__, DCCEnabled);
	dml2_printf("DML::%s: nomDETInKByte = %u\n", __func__, nomDETInKByte);
	dml2_printf("DML::%s: DETBufferSizeForDCC = %u\n", __func__, DETBufferSizeForDCC);
	dml2_printf("DML::%s: req128_horz_wc_l = %u\n", __func__, req128_horz_wc_l);
	dml2_printf("DML::%s: req128_horz_wc_c = %u\n", __func__, req128_horz_wc_c);
	dml2_printf("DML::%s: full_swath_bytes_horz_wc_l = %u\n", __func__, full_swath_bytes_horz_wc_l);
	dml2_printf("DML::%s: full_swath_bytes_vert_wc_c = %u\n", __func__, full_swath_bytes_vert_wc_c);
	dml2_printf("DML::%s: segment_order_horz_contiguous_luma = %u\n", __func__, segment_order_horz_contiguous_luma);
	dml2_printf("DML::%s: segment_order_horz_contiguous_chroma = %u\n", __func__, segment_order_horz_contiguous_chroma);
#endif
	if (DCCProgrammingAssumesScanDirectionUnknown == true) {
		if (req128_horz_wc_l == 0 && req128_vert_wc_l == 0) {
			*RequestLuma = dml2_core_internal_request_type_256_bytes;
		} else if ((req128_horz_wc_l == 1 && segment_order_horz_contiguous_luma == 0) || (req128_vert_wc_l == 1 && segment_order_vert_contiguous_luma == 0)) {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
		if (req128_horz_wc_c == 0 && req128_vert_wc_c == 0) {
			*RequestChroma = dml2_core_internal_request_type_256_bytes;
		} else if ((req128_horz_wc_c == 1 && segment_order_horz_contiguous_chroma == 0) || (req128_vert_wc_c == 1 && segment_order_vert_contiguous_chroma == 0)) {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
	} else if (!dml_is_vertical_rotation(RotationAngle)) {
		if (req128_horz_wc_l == 0) {
			*RequestLuma = dml2_core_internal_request_type_256_bytes;
		} else if (segment_order_horz_contiguous_luma == 0) {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
		if (req128_horz_wc_c == 0) {
			*RequestChroma = dml2_core_internal_request_type_256_bytes;
		} else if (segment_order_horz_contiguous_chroma == 0) {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
	} else {
		if (req128_vert_wc_l == 0) {
			*RequestLuma = dml2_core_internal_request_type_256_bytes;
		} else if (segment_order_vert_contiguous_luma == 0) {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestLuma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
		if (req128_vert_wc_c == 0) {
			*RequestChroma = dml2_core_internal_request_type_256_bytes;
		} else if (segment_order_vert_contiguous_chroma == 0) {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_non_contiguous;
		} else {
			*RequestChroma = dml2_core_internal_request_type_128_bytes_contiguous;
		}
	}

	if (*RequestLuma == dml2_core_internal_request_type_256_bytes) {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 256;
		*IndependentBlockLuma = 0;
	} else if (*RequestLuma == dml2_core_internal_request_type_128_bytes_contiguous) {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 128;
		*IndependentBlockLuma = 128;
	} else {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 64;
		*IndependentBlockLuma = 64;
	}

	if (*RequestChroma == dml2_core_internal_request_type_256_bytes) {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 256;
		*IndependentBlockChroma = 0;
	} else if (*RequestChroma == dml2_core_internal_request_type_128_bytes_contiguous) {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 128;
		*IndependentBlockChroma = 128;
	} else {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 64;
		*IndependentBlockChroma = 64;
	}

	if (DCCEnabled != true || BytePerPixelC == 0) {
		*MaxUncompressedBlockChroma = 0;
		*MaxCompressedBlockChroma = 0;
		*IndependentBlockChroma = 0;
	}

	if (DCCEnabled != true) {
		*MaxUncompressedBlockLuma = 0;
		*MaxCompressedBlockLuma = 0;
		*IndependentBlockLuma = 0;
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: MaxUncompressedBlockLuma = %u\n", __func__, *MaxUncompressedBlockLuma);
	dml2_printf("DML::%s: MaxCompressedBlockLuma = %u\n", __func__, *MaxCompressedBlockLuma);
	dml2_printf("DML::%s: IndependentBlockLuma = %u\n", __func__, *IndependentBlockLuma);
	dml2_printf("DML::%s: MaxUncompressedBlockChroma = %u\n", __func__, *MaxUncompressedBlockChroma);
	dml2_printf("DML::%s: MaxCompressedBlockChroma = %u\n", __func__, *MaxCompressedBlockChroma);
	dml2_printf("DML::%s: IndependentBlockChroma = %u\n", __func__, *IndependentBlockChroma);
#endif

}

static void calculate_mcache_row_bytes(
	struct dml2_core_internal_scratch *scratch,
	struct dml2_core_calcs_calculate_mcache_row_bytes_params *p)
{
	unsigned int vmpg_bytes = 0;
	unsigned int blk_bytes = 0;
	float meta_per_mvmpg_per_channel = 0;
	unsigned int est_blk_per_vmpg = 2;
	unsigned int mvmpg_per_row_ub = 0;
	unsigned int full_vp_width_mvmpg_aligned = 0;
	unsigned int full_vp_height_mvmpg_aligned = 0;
	unsigned int meta_per_mvmpg_per_channel_ub = 0;
	unsigned int mvmpg_per_mcache;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: num_chans = %u\n", __func__, p->num_chans);
	dml2_printf("DML::%s: mem_word_bytes = %u\n", __func__, p->mem_word_bytes);
	dml2_printf("DML::%s: mcache_line_size_bytes = %u\n", __func__, p->mcache_line_size_bytes);
	dml2_printf("DML::%s: mcache_size_bytes = %u\n", __func__, p->mcache_size_bytes);
	dml2_printf("DML::%s: gpuvm_enable = %u\n", __func__, p->gpuvm_enable);
	dml2_printf("DML::%s: gpuvm_page_size_kbytes = %u\n", __func__, p->gpuvm_page_size_kbytes);
	dml2_printf("DML::%s: vp_stationary = %u\n", __func__, p->vp_stationary);
	dml2_printf("DML::%s: tiling_mode = %u\n", __func__, p->tiling_mode);
	dml2_printf("DML::%s: vp_start_x = %u\n", __func__, p->vp_start_x);
	dml2_printf("DML::%s: vp_start_y = %u\n", __func__, p->vp_start_y);
	dml2_printf("DML::%s: full_vp_width = %u\n", __func__, p->full_vp_width);
	dml2_printf("DML::%s: full_vp_height = %u\n", __func__, p->full_vp_height);
	dml2_printf("DML::%s: blk_width = %u\n", __func__, p->blk_width);
	dml2_printf("DML::%s: blk_height = %u\n", __func__, p->blk_height);
	dml2_printf("DML::%s: vmpg_width = %u\n", __func__, p->vmpg_width);
	dml2_printf("DML::%s: vmpg_height = %u\n", __func__, p->vmpg_height);
	dml2_printf("DML::%s: full_swath_bytes = %u\n", __func__, p->full_swath_bytes);
#endif
	DML2_ASSERT(p->mcache_line_size_bytes != 0);
	DML2_ASSERT(p->mcache_size_bytes != 0);

	*p->mvmpg_width = 0;
	*p->mvmpg_height = 0;

	if (p->full_vp_height == 0 && p->full_vp_width == 0) {
		*p->num_mcaches = 0;
		*p->mcache_row_bytes = 0;
	} else {
		blk_bytes = dml_get_tile_block_size_bytes(p->tiling_mode);

		// if gpuvm is not enable, the alignment boundary should be in terms of tiling block size
		vmpg_bytes = p->gpuvm_page_size_kbytes * 1024;

		//With vmpg_bytes >= tile blk_bytes, the meta_row_width alignment equations are relative to the vmpg_width/height.
		// But for 4KB page with 64KB tile block, we need the meta for all pages in the tile block.
		// Therefore, the alignment is relative to the blk_width/height. The factor of 16 vmpg per 64KB tile block is applied at the end.
		*p->mvmpg_width = p->blk_width;
		*p->mvmpg_height = p->blk_height;
		if (p->gpuvm_enable) {
			if (vmpg_bytes >= blk_bytes) {
				*p->mvmpg_width = p->vmpg_width;
				*p->mvmpg_height = p->vmpg_height;
			} else if (!((blk_bytes == 65536) && (vmpg_bytes == 4096))) {
				dml2_printf("ERROR: DML::%s: Tiling size and vm page size combination not supported\n", __func__);
				DML2_ASSERT(0);
			}
		}

		//For plane0 & 1, first calculate full_vp_width/height_l/c aligned to vmpg_width/height_l/c
		full_vp_width_mvmpg_aligned = (unsigned int)(math_floor2((p->vp_start_x + p->full_vp_width) + *p->mvmpg_width - 1, *p->mvmpg_width) - math_floor2(p->vp_start_x, *p->mvmpg_width));
		full_vp_height_mvmpg_aligned = (unsigned int)(math_floor2((p->vp_start_y + p->full_vp_height) + *p->mvmpg_height - 1, *p->mvmpg_height) - math_floor2(p->vp_start_y, *p->mvmpg_height));

		*p->full_vp_access_width_mvmpg_aligned = p->surf_vert ? full_vp_height_mvmpg_aligned : full_vp_width_mvmpg_aligned;

		//Use the equation for the exact alignment when possible. Note that the exact alignment cannot be used for horizontal access if vmpg_bytes > blk_bytes.
		if (!p->surf_vert) { //horizontal access
			if (p->vp_stationary == 1 && vmpg_bytes <= blk_bytes)
				*p->meta_row_width_ub = full_vp_width_mvmpg_aligned;
			else
				*p->meta_row_width_ub = (unsigned int)math_ceil2((double)p->full_vp_width - 1, *p->mvmpg_width) + *p->mvmpg_width;
			mvmpg_per_row_ub = *p->meta_row_width_ub / *p->mvmpg_width;
		} else { //vertical access
			if (p->vp_stationary == 1)
				*p->meta_row_width_ub = full_vp_height_mvmpg_aligned;
			else
				*p->meta_row_width_ub = (unsigned int)math_ceil2((double)p->full_vp_height - 1, *p->mvmpg_height) + *p->mvmpg_height;
			mvmpg_per_row_ub = *p->meta_row_width_ub / *p->mvmpg_height;
		}

		if (p->gpuvm_enable) {
			meta_per_mvmpg_per_channel = (float)vmpg_bytes / (float)256 / p->num_chans;

			//but using the est_blk_per_vmpg between 2 and 4, to be not as pessimestic
			if (p->surf_vert && vmpg_bytes > blk_bytes) {
				meta_per_mvmpg_per_channel = (float)est_blk_per_vmpg * blk_bytes / (float)256 / p->num_chans;
			}

			*p->dcc_dram_bw_nom_overhead_factor = 1 + math_max2(1.0 / 256.0, math_ceil2(meta_per_mvmpg_per_channel, p->mem_word_bytes) / (256 * meta_per_mvmpg_per_channel)); // dcc_dr_oh_nom
		} else {
			meta_per_mvmpg_per_channel = (float) blk_bytes / (float)256 / p->num_chans;

			if (!p->surf_vert)
				*p->dcc_dram_bw_nom_overhead_factor = 1 + 1.0 / 256.0;
			else
				 *p->dcc_dram_bw_nom_overhead_factor = 1 + math_max2(1.0 / 256.0, math_ceil2(meta_per_mvmpg_per_channel, p->mem_word_bytes) / (256 * meta_per_mvmpg_per_channel));
		}

		meta_per_mvmpg_per_channel_ub = (unsigned int)math_ceil2((double)meta_per_mvmpg_per_channel, p->mcache_line_size_bytes);

		//but for 4KB vmpg with 64KB tile blk
		if (p->gpuvm_enable && (blk_bytes == 65536) && (vmpg_bytes == 4096))
			meta_per_mvmpg_per_channel_ub = 16 * meta_per_mvmpg_per_channel_ub;

		// If this mcache_row_bytes for the full viewport of the surface is less than or equal to mcache_bytes,
		// then one mcache can be used for this request stream. If not, it is useful to know the width of the viewport that can be supported in the mcache_bytes.
		if (p->gpuvm_enable || !p->surf_vert) {
			*p->mcache_row_bytes = mvmpg_per_row_ub * meta_per_mvmpg_per_channel_ub;
		} else { // horizontal and gpuvm disable
			*p->mcache_row_bytes = *p->meta_row_width_ub * p->blk_height * p->bytes_per_pixel / 256;
			*p->mcache_row_bytes = (unsigned int)math_ceil2((double)*p->mcache_row_bytes / p->num_chans, p->mcache_line_size_bytes);
		}

		*p->dcc_dram_bw_pref_overhead_factor = 1 + math_max2(1.0 / 256.0, *p->mcache_row_bytes / p->full_swath_bytes); // dcc_dr_oh_pref
		*p->num_mcaches = (unsigned int)math_ceil2((double)*p->mcache_row_bytes / p->mcache_size_bytes, 1);

		mvmpg_per_mcache = p->mcache_size_bytes / meta_per_mvmpg_per_channel_ub;
		*p->mvmpg_per_mcache_lb = (unsigned int)math_floor2(mvmpg_per_mcache, 1);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: gpuvm_enable = %u\n", __func__, p->gpuvm_enable);
		dml2_printf("DML::%s: vmpg_bytes = %u\n", __func__, vmpg_bytes);
		dml2_printf("DML::%s: blk_bytes = %u\n", __func__, blk_bytes);
		dml2_printf("DML::%s: meta_per_mvmpg_per_channel = %f\n", __func__, meta_per_mvmpg_per_channel);
		dml2_printf("DML::%s: mvmpg_per_row_ub = %u\n", __func__, mvmpg_per_row_ub);
		dml2_printf("DML::%s: meta_row_width_ub = %u\n", __func__, *p->meta_row_width_ub);
		dml2_printf("DML::%s: mvmpg_width = %u\n", __func__, *p->mvmpg_width);
		dml2_printf("DML::%s: mvmpg_height = %u\n", __func__, *p->mvmpg_height);
		dml2_printf("DML::%s: dcc_dram_bw_nom_overhead_factor = %f\n", __func__, *p->dcc_dram_bw_nom_overhead_factor);
		dml2_printf("DML::%s: dcc_dram_bw_pref_overhead_factor = %f\n", __func__, *p->dcc_dram_bw_pref_overhead_factor);
#endif
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: mcache_row_bytes = %u\n", __func__, *p->mcache_row_bytes);
	dml2_printf("DML::%s: num_mcaches = %u\n", __func__, *p->num_mcaches);
#endif
	DML2_ASSERT(*p->num_mcaches > 0);
}

static void calculate_mcache_setting(
	struct dml2_core_internal_scratch *scratch,
	struct dml2_core_calcs_calculate_mcache_setting_params *p)
{
	unsigned int n;

	struct dml2_core_shared_calculate_mcache_setting_locals *l = &scratch->calculate_mcache_setting_locals;
	memset(l, 0, sizeof(struct dml2_core_shared_calculate_mcache_setting_locals));

	*p->num_mcaches_l = 0;
	*p->mcache_row_bytes_l = 0;
	*p->dcc_dram_bw_nom_overhead_factor_l = 1.0;
	*p->dcc_dram_bw_pref_overhead_factor_l = 1.0;

	*p->num_mcaches_c = 0;
	*p->mcache_row_bytes_c = 0;
	*p->dcc_dram_bw_nom_overhead_factor_c = 1.0;
	*p->dcc_dram_bw_pref_overhead_factor_c = 1.0;

	*p->mall_comb_mcache_l = 0;
	*p->mall_comb_mcache_c = 0;
	*p->lc_comb_mcache = 0;

	if (!p->dcc_enable)
		return;

	l->is_dual_plane = dml_is_420(p->source_format) || p->source_format == dml2_rgbe_alpha;

	l->l_p.num_chans = p->num_chans;
	l->l_p.mem_word_bytes = p->mem_word_bytes;
	l->l_p.mcache_size_bytes = p->mcache_size_bytes;
	l->l_p.mcache_line_size_bytes = p->mcache_line_size_bytes;
	l->l_p.gpuvm_enable = p->gpuvm_enable;
	l->l_p.gpuvm_page_size_kbytes = p->gpuvm_page_size_kbytes;
	l->l_p.surf_vert = p->surf_vert;
	l->l_p.vp_stationary = p->vp_stationary;
	l->l_p.tiling_mode = p->tiling_mode;
	l->l_p.vp_start_x = p->vp_start_x_l;
	l->l_p.vp_start_y = p->vp_start_y_l;
	l->l_p.full_vp_width = p->full_vp_width_l;
	l->l_p.full_vp_height = p->full_vp_height_l;
	l->l_p.blk_width = p->blk_width_l;
	l->l_p.blk_height = p->blk_height_l;
	l->l_p.vmpg_width = p->vmpg_width_l;
	l->l_p.vmpg_height = p->vmpg_height_l;
	l->l_p.full_swath_bytes = p->full_swath_bytes_l;
	l->l_p.bytes_per_pixel = p->bytes_per_pixel_l;

	// output
	l->l_p.num_mcaches = p->num_mcaches_l;
	l->l_p.mcache_row_bytes = p->mcache_row_bytes_l;
	l->l_p.dcc_dram_bw_nom_overhead_factor = p->dcc_dram_bw_nom_overhead_factor_l;
	l->l_p.dcc_dram_bw_pref_overhead_factor = p->dcc_dram_bw_pref_overhead_factor_l;
	l->l_p.mvmpg_width = &l->mvmpg_width_l;
	l->l_p.mvmpg_height = &l->mvmpg_height_l;
	l->l_p.full_vp_access_width_mvmpg_aligned = &l->full_vp_access_width_mvmpg_aligned_l;
	l->l_p.meta_row_width_ub = &l->meta_row_width_l;
	l->l_p.mvmpg_per_mcache_lb = &l->mvmpg_per_mcache_lb_l;

	calculate_mcache_row_bytes(scratch, &l->l_p);
	dml2_assert(*p->num_mcaches_l > 0);

	if (l->is_dual_plane) {
		l->c_p.num_chans = p->num_chans;
		l->c_p.mem_word_bytes = p->mem_word_bytes;
		l->c_p.mcache_size_bytes = p->mcache_size_bytes;
		l->c_p.mcache_line_size_bytes = p->mcache_line_size_bytes;
		l->c_p.gpuvm_enable = p->gpuvm_enable;
		l->c_p.gpuvm_page_size_kbytes = p->gpuvm_page_size_kbytes;
		l->c_p.surf_vert = p->surf_vert;
		l->c_p.vp_stationary = p->vp_stationary;
		l->c_p.tiling_mode = p->tiling_mode;
		l->c_p.vp_start_x = p->vp_start_x_c;
		l->c_p.vp_start_y = p->vp_start_y_c;
		l->c_p.full_vp_width = p->full_vp_width_c;
		l->c_p.full_vp_height = p->full_vp_height_c;
		l->c_p.blk_width = p->blk_width_c;
		l->c_p.blk_height = p->blk_height_c;
		l->c_p.vmpg_width = p->vmpg_width_c;
		l->c_p.vmpg_height = p->vmpg_height_c;
		l->c_p.full_swath_bytes = p->full_swath_bytes_c;
		l->c_p.bytes_per_pixel = p->bytes_per_pixel_c;

		// output
		l->c_p.num_mcaches = p->num_mcaches_c;
		l->c_p.mcache_row_bytes = p->mcache_row_bytes_c;
		l->c_p.dcc_dram_bw_nom_overhead_factor = p->dcc_dram_bw_nom_overhead_factor_c;
		l->c_p.dcc_dram_bw_pref_overhead_factor = p->dcc_dram_bw_pref_overhead_factor_c;
		l->c_p.mvmpg_width = &l->mvmpg_width_c;
		l->c_p.mvmpg_height = &l->mvmpg_height_c;
		l->c_p.full_vp_access_width_mvmpg_aligned = &l->full_vp_access_width_mvmpg_aligned_c;
		l->c_p.meta_row_width_ub = &l->meta_row_width_c;
		l->c_p.mvmpg_per_mcache_lb = &l->mvmpg_per_mcache_lb_c;

		calculate_mcache_row_bytes(scratch, &l->c_p);
		dml2_assert(*p->num_mcaches_c > 0);
	}

	// Sharing for iMALL access
	l->mcache_remainder_l = *p->mcache_row_bytes_l % p->mcache_size_bytes;
	l->mcache_remainder_c = *p->mcache_row_bytes_c % p->mcache_size_bytes;
	l->mvmpg_access_width_l = p->surf_vert ? l->mvmpg_height_l : l->mvmpg_width_l;
	l->mvmpg_access_width_c = p->surf_vert ? l->mvmpg_height_c : l->mvmpg_width_c;

	if (p->imall_enable) {
		*p->mall_comb_mcache_l = (2 * l->mcache_remainder_l <= p->mcache_size_bytes);

		if (l->is_dual_plane)
			*p->mall_comb_mcache_c = (2 * l->mcache_remainder_c <= p->mcache_size_bytes);
	}

	if (!p->surf_vert) // horizonatal access
		l->luma_time_factor = (double)l->mvmpg_height_c / l->mvmpg_height_l * 2;
	else // vertical access
		l->luma_time_factor = (double)l->mvmpg_width_c / l->mvmpg_width_l * 2;

	// The algorithm starts with computing a non-integer, avg_mcache_element_size_l/c:
	if (*p->num_mcaches_l) {
		l->avg_mcache_element_size_l = l->meta_row_width_l / *p->num_mcaches_l;
	}
	if (l->is_dual_plane) {
		l->avg_mcache_element_size_c = l->meta_row_width_c / *p->num_mcaches_c;

		if (!p->imall_enable || (*p->mall_comb_mcache_l == *p->mall_comb_mcache_c)) {
			l->lc_comb_last_mcache_size = (unsigned int)((l->mcache_remainder_l * (*p->mall_comb_mcache_l ? 2 : 1) * l->luma_time_factor) +
				(l->mcache_remainder_c * (*p->mall_comb_mcache_c ? 2 : 1)));
		}
		*p->lc_comb_mcache = (l->lc_comb_last_mcache_size <= p->mcache_size_bytes) && (*p->mall_comb_mcache_l == *p->mall_comb_mcache_c);
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: imall_enable = %u\n", __func__, p->imall_enable);
	dml2_printf("DML::%s: is_dual_plane = %u\n", __func__, l->is_dual_plane);
	dml2_printf("DML::%s: surf_vert = %u\n", __func__, p->surf_vert);
	dml2_printf("DML::%s: mvmpg_width_l = %u\n", __func__, l->mvmpg_width_l);
	dml2_printf("DML::%s: mvmpg_height_l = %u\n", __func__, l->mvmpg_height_l);
	dml2_printf("DML::%s: mcache_remainder_l = %f\n", __func__, l->mcache_remainder_l);
	dml2_printf("DML::%s: num_mcaches_l = %u\n", __func__, *p->num_mcaches_l);
	dml2_printf("DML::%s: avg_mcache_element_size_l = %u\n", __func__, l->avg_mcache_element_size_l);
	dml2_printf("DML::%s: mvmpg_access_width_l = %u\n", __func__, l->mvmpg_access_width_l);
	dml2_printf("DML::%s: mall_comb_mcache_l = %u\n", __func__, *p->mall_comb_mcache_l);

	if (l->is_dual_plane) {
		dml2_printf("DML::%s: mvmpg_width_c = %u\n", __func__, l->mvmpg_width_c);
		dml2_printf("DML::%s: mvmpg_height_c = %u\n", __func__, l->mvmpg_height_c);
		dml2_printf("DML::%s: mcache_remainder_c = %f\n", __func__, l->mcache_remainder_c);
		dml2_printf("DML::%s: luma_time_factor = %f\n", __func__, l->luma_time_factor);
		dml2_printf("DML::%s: num_mcaches_c = %u\n", __func__, *p->num_mcaches_c);
		dml2_printf("DML::%s: avg_mcache_element_size_c = %u\n", __func__, l->avg_mcache_element_size_c);
		dml2_printf("DML::%s: mvmpg_access_width_c = %u\n", __func__, l->mvmpg_access_width_c);
		dml2_printf("DML::%s: mall_comb_mcache_c = %u\n", __func__, *p->mall_comb_mcache_c);
		dml2_printf("DML::%s: lc_comb_last_mcache_size = %u\n", __func__, l->lc_comb_last_mcache_size);
		dml2_printf("DML::%s: lc_comb_mcache = %u\n", __func__, *p->lc_comb_mcache);
	}
#endif
	// calculate split_coordinate
	l->full_vp_access_width_l = p->surf_vert ? p->full_vp_height_l : p->full_vp_width_l;
	l->full_vp_access_width_c = p->surf_vert ? p->full_vp_height_c : p->full_vp_width_c;

	for (n = 0; n < *p->num_mcaches_l - 1; n++) {
		p->mcache_offsets_l[n] = (unsigned int)(math_floor2((n + 1) * l->avg_mcache_element_size_l / l->mvmpg_access_width_l, 1)) * l->mvmpg_access_width_l;
	}
	p->mcache_offsets_l[*p->num_mcaches_l - 1] = l->full_vp_access_width_l;

	if (l->is_dual_plane) {
		for (n = 0; n < *p->num_mcaches_c - 1; n++) {
			p->mcache_offsets_c[n] = (unsigned int)(math_floor2((n + 1) * l->avg_mcache_element_size_c / l->mvmpg_access_width_c, 1)) * l->mvmpg_access_width_c;
		}
		p->mcache_offsets_c[*p->num_mcaches_c - 1] = l->full_vp_access_width_c;
	}
#ifdef __DML_VBA_DEBUG__
	for (n = 0; n < *p->num_mcaches_l; n++)
		dml2_printf("DML::%s: mcache_offsets_l[%u] = %u\n", __func__, n, p->mcache_offsets_l[n]);

	if (l->is_dual_plane) {
		for (n = 0; n < *p->num_mcaches_c; n++)
			dml2_printf("DML::%s: mcache_offsets_c[%u] = %u\n", __func__, n, p->mcache_offsets_c[n]);
	}
#endif

	// Luma/Chroma combine in the last mcache
	// In the case of Luma/Chroma combine-mCache (with lc_comb_mcache==1), all mCaches except the last segment are filled as much as possible, when stay aligned to mvmpg boundary
	if (*p->lc_comb_mcache && l->is_dual_plane) {
		for (n = 0; n < *p->num_mcaches_l - 1; n++)
			p->mcache_offsets_l[n] = (n + 1) * l->mvmpg_per_mcache_lb_l * l->mvmpg_access_width_l;
		p->mcache_offsets_l[*p->num_mcaches_l - 1] = l->full_vp_access_width_l;

		for (n = 0; n < *p->num_mcaches_c - 1; n++)
			p->mcache_offsets_c[n] = (n + 1) * l->mvmpg_per_mcache_lb_c * l->mvmpg_access_width_c;
		p->mcache_offsets_c[*p->num_mcaches_c - 1] = l->full_vp_access_width_c;

#ifdef __DML_VBA_DEBUG__
		for (n = 0; n < *p->num_mcaches_l; n++)
			dml2_printf("DML::%s: mcache_offsets_l[%u] = %u\n", __func__, n, p->mcache_offsets_l[n]);

		for (n = 0; n < *p->num_mcaches_c; n++)
			dml2_printf("DML::%s: mcache_offsets_c[%u] = %u\n", __func__, n, p->mcache_offsets_c[n]);
#endif
	}

	*p->mcache_shift_granularity_l = l->mvmpg_access_width_l;
	*p->mcache_shift_granularity_c = l->mvmpg_access_width_c;
}

static void calculate_mall_bw_overhead_factor(
	double mall_prefetch_sdp_overhead_factor[], //mall_sdp_oh_nom/pref
	double mall_prefetch_dram_overhead_factor[], //mall_dram_oh_nom/pref

	// input
	const struct dml2_display_cfg *display_cfg,
	unsigned int num_active_planes)
{
	for (unsigned int k = 0; k < num_active_planes; ++k) {
		mall_prefetch_sdp_overhead_factor[k] = 1.0;
		mall_prefetch_dram_overhead_factor[k] = 1.0;

		// SDP - on the return side
		if (display_cfg->plane_descriptors[k].overrides.legacy_svp_config == dml2_svp_mode_override_imall) // always no data return
			mall_prefetch_sdp_overhead_factor[k] = 1.25;
		else if (display_cfg->plane_descriptors[k].overrides.legacy_svp_config == dml2_svp_mode_override_phantom_pipe_no_data_return)
			mall_prefetch_sdp_overhead_factor[k] = 0.25;

		// DRAM
		if (display_cfg->plane_descriptors[k].overrides.legacy_svp_config == dml2_svp_mode_override_imall)
			mall_prefetch_dram_overhead_factor[k] = 2.0;

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, mall_prefetch_sdp_overhead_factor = %f\n", __func__, k, mall_prefetch_sdp_overhead_factor[k]);
		dml2_printf("DML::%s: k=%u, mall_prefetch_dram_overhead_factor = %f\n", __func__, k, mall_prefetch_dram_overhead_factor[k]);
#endif
	}
}

static double dml_get_return_bandwidth_available(
	const struct dml2_soc_bb *soc,
	enum dml2_core_internal_soc_state_type state_type,
	enum dml2_core_internal_bw_type bw_type,
	bool is_avg_bw,
	bool is_hvm_en,
	bool is_hvm_only,
	double dcfclk_mhz,
	double fclk_mhz,
	double dram_bw_mbps)
{
	double return_bw_mbps = 0.;
	double ideal_sdp_bandwidth = (double)soc->return_bus_width_bytes * dcfclk_mhz;
	double ideal_fabric_bandwidth = fclk_mhz * (double)soc->fabric_datapath_to_dcn_data_return_bytes;
	double ideal_dram_bandwidth = dram_bw_mbps; //dram_speed_mts * soc->clk_table.dram_config.channel_count * soc->clk_table.dram_config.channel_width_bytes;

	double derate_sdp_factor;
	double derate_fabric_factor;
	double derate_dram_factor;

	double derate_sdp_bandwidth;
	double derate_fabric_bandwidth;
	double derate_dram_bandwidth;

	if (is_avg_bw) {
		if (state_type == dml2_core_internal_soc_state_svp_prefetch) {
			derate_sdp_factor = soc->qos_parameters.derate_table.dcn_mall_prefetch_average.dcfclk_derate_percent / 100.0;
			derate_fabric_factor = soc->qos_parameters.derate_table.dcn_mall_prefetch_average.fclk_derate_percent / 100.0;
			derate_dram_factor = soc->qos_parameters.derate_table.dcn_mall_prefetch_average.dram_derate_percent_pixel / 100.0;
		} else { // just assume sys_active
			derate_sdp_factor = soc->qos_parameters.derate_table.system_active_average.dcfclk_derate_percent / 100.0;
			derate_fabric_factor = soc->qos_parameters.derate_table.system_active_average.fclk_derate_percent / 100.0;
			derate_dram_factor = soc->qos_parameters.derate_table.system_active_average.dram_derate_percent_pixel / 100.0;
		}
	} else { // urgent bw
		if (state_type == dml2_core_internal_soc_state_svp_prefetch) {
			derate_sdp_factor = soc->qos_parameters.derate_table.dcn_mall_prefetch_urgent.dcfclk_derate_percent / 100.0;
			derate_fabric_factor = soc->qos_parameters.derate_table.dcn_mall_prefetch_urgent.fclk_derate_percent / 100.0;
			derate_dram_factor = soc->qos_parameters.derate_table.dcn_mall_prefetch_urgent.dram_derate_percent_pixel / 100.0;

			if (is_hvm_en) {
				if (is_hvm_only)
					derate_dram_factor = soc->qos_parameters.derate_table.dcn_mall_prefetch_urgent.dram_derate_percent_vm / 100.0;
				else
					derate_dram_factor = soc->qos_parameters.derate_table.dcn_mall_prefetch_urgent.dram_derate_percent_pixel_and_vm / 100.0;
			} else {
				derate_dram_factor = soc->qos_parameters.derate_table.dcn_mall_prefetch_urgent.dram_derate_percent_pixel / 100.0;
			}
		} else { // just assume sys_active
			derate_sdp_factor = soc->qos_parameters.derate_table.system_active_urgent.dcfclk_derate_percent / 100.0;
			derate_fabric_factor = soc->qos_parameters.derate_table.system_active_urgent.fclk_derate_percent / 100.0;

			if (is_hvm_en) {
				if (is_hvm_only)
					derate_dram_factor = soc->qos_parameters.derate_table.system_active_urgent.dram_derate_percent_vm / 100.0;
				else
					derate_dram_factor = soc->qos_parameters.derate_table.system_active_urgent.dram_derate_percent_pixel_and_vm / 100.0;
			} else {
				derate_dram_factor = soc->qos_parameters.derate_table.system_active_urgent.dram_derate_percent_pixel / 100.0;
			}
		}
	}

	derate_sdp_bandwidth = ideal_sdp_bandwidth * derate_sdp_factor;
	derate_fabric_bandwidth = ideal_fabric_bandwidth * derate_fabric_factor;
	derate_dram_bandwidth = ideal_dram_bandwidth * derate_dram_factor;

	if (bw_type == dml2_core_internal_bw_sdp)
		return_bw_mbps = math_min2(derate_sdp_bandwidth, derate_fabric_bandwidth);
	else // dml2_core_internal_bw_dram
		return_bw_mbps = derate_dram_bandwidth;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: is_avg_bw = %u\n", __func__, is_avg_bw);
	dml2_printf("DML::%s: is_hvm_en = %u\n", __func__, is_hvm_en);
	dml2_printf("DML::%s: is_hvm_only = %u\n", __func__, is_hvm_only);
	dml2_printf("DML::%s: state_type = %s\n", __func__, dml2_core_internal_soc_state_type_str(state_type));
	dml2_printf("DML::%s: bw_type = %s\n", __func__, dml2_core_internal_bw_type_str(bw_type));
	dml2_printf("DML::%s: dcfclk_mhz = %f\n", __func__, dcfclk_mhz);
	dml2_printf("DML::%s: fclk_mhz = %f\n", __func__, fclk_mhz);
	dml2_printf("DML::%s: ideal_sdp_bandwidth = %f\n", __func__, ideal_sdp_bandwidth);
	dml2_printf("DML::%s: ideal_fabric_bandwidth = %f\n", __func__, ideal_fabric_bandwidth);
	dml2_printf("DML::%s: ideal_dram_bandwidth = %f\n", __func__, ideal_dram_bandwidth);
	dml2_printf("DML::%s: derate_sdp_bandwidth = %f (derate %f)\n", __func__, derate_sdp_bandwidth, derate_sdp_factor);
	dml2_printf("DML::%s: derate_fabric_bandwidth = %f (derate %f)\n", __func__, derate_fabric_bandwidth, derate_fabric_factor);
	dml2_printf("DML::%s: derate_dram_bandwidth = %f (derate %f)\n", __func__, derate_dram_bandwidth, derate_dram_factor);
	dml2_printf("DML::%s: return_bw_mbps = %f\n", __func__, return_bw_mbps);
#endif
	return return_bw_mbps;
}

static void calculate_bandwidth_available(
	double avg_bandwidth_available_min[dml2_core_internal_soc_state_max],
	double avg_bandwidth_available[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max],
	double urg_bandwidth_available_min[dml2_core_internal_soc_state_max], // min between SDP and DRAM
	double urg_bandwidth_available[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max],
	double urg_bandwidth_available_vm_only[dml2_core_internal_soc_state_max],
	double urg_bandwidth_available_pixel_and_vm[dml2_core_internal_soc_state_max],

	const struct dml2_soc_bb *soc,
	bool HostVMEnable,
	double dcfclk_mhz,
	double fclk_mhz,
	double dram_bw_mbps)
{
	unsigned int n, m;

	dml2_printf("DML::%s: dcfclk_mhz = %f\n", __func__, dcfclk_mhz);
	dml2_printf("DML::%s: fclk_mhz = %f\n", __func__, fclk_mhz);
	dml2_printf("DML::%s: dram_bw_mbps = %f\n", __func__, dram_bw_mbps);

	// Calculate all the bandwidth availabe
	for (m = 0; m < dml2_core_internal_soc_state_max; m++) {
		for (n = 0; n < dml2_core_internal_bw_max; n++) {
			avg_bandwidth_available[m][n] = dml_get_return_bandwidth_available(soc,
				m, // soc_state
				n, // bw_type
				1, // avg_bw
				HostVMEnable,
				0, // hvm_only
				dcfclk_mhz,
				fclk_mhz,
				dram_bw_mbps);

			urg_bandwidth_available[m][n] = dml_get_return_bandwidth_available(soc, m, n, 0, HostVMEnable, 0, dcfclk_mhz, fclk_mhz, dram_bw_mbps);


#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: avg_bandwidth_available[%s][%s]=%f\n", __func__, dml2_core_internal_soc_state_type_str(m), dml2_core_internal_bw_type_str(n), avg_bandwidth_available[m][n]);
			dml2_printf("DML::%s: urg_bandwidth_available[%s][%s]=%f\n", __func__, dml2_core_internal_soc_state_type_str(m), dml2_core_internal_bw_type_str(n), urg_bandwidth_available[m][n]);
#endif

			// urg_bandwidth_available_vm_only is indexed by soc_state
			if (n == dml2_core_internal_bw_dram) {
				urg_bandwidth_available_vm_only[m] = dml_get_return_bandwidth_available(soc, m, n, 0, HostVMEnable, 1, dcfclk_mhz, fclk_mhz, dram_bw_mbps);
				urg_bandwidth_available_pixel_and_vm[m] = dml_get_return_bandwidth_available(soc, m, n, 0, HostVMEnable, 0, dcfclk_mhz, fclk_mhz, dram_bw_mbps);
			}
		}

		avg_bandwidth_available_min[m] = math_min2(avg_bandwidth_available[m][dml2_core_internal_bw_dram], avg_bandwidth_available[m][dml2_core_internal_bw_sdp]);
		urg_bandwidth_available_min[m] = math_min2(urg_bandwidth_available[m][dml2_core_internal_bw_dram], urg_bandwidth_available[m][dml2_core_internal_bw_sdp]);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: avg_bandwidth_available_min[%s]=%f\n", __func__, dml2_core_internal_soc_state_type_str(m), avg_bandwidth_available_min[m]);
		dml2_printf("DML::%s: urg_bandwidth_available_min[%s]=%f\n", __func__, dml2_core_internal_soc_state_type_str(m), urg_bandwidth_available_min[m]);
		dml2_printf("DML::%s: urg_bandwidth_available_vm_only[%s]=%f\n", __func__, dml2_core_internal_soc_state_type_str(m), urg_bandwidth_available_vm_only[n]);
#endif
	}
}

static void calculate_avg_bandwidth_required(
	double avg_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max],

	// input
	const struct dml2_display_cfg *display_cfg,
	unsigned int num_active_planes,
	double ReadBandwidthLuma[],
	double ReadBandwidthChroma[],
	double cursor_bw[],
	double dcc_dram_bw_nom_overhead_factor_p0[],
	double dcc_dram_bw_nom_overhead_factor_p1[],
	double mall_prefetch_dram_overhead_factor[],
	double mall_prefetch_sdp_overhead_factor[])
{
	unsigned int n, m, k;
	double sdp_overhead_factor;
	double dram_overhead_factor_p0;
	double dram_overhead_factor_p1;

	// Average BW support check
	for (m = 0; m < dml2_core_internal_soc_state_max; m++) {
		for (n = 0; n < dml2_core_internal_bw_max; n++) { // sdp, dram
			avg_bandwidth_required[m][n] = 0;
		}
	}

	// SysActive and SVP Prefetch AVG bandwidth Check
	for (k = 0; k < num_active_planes; ++k) {
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: plane %0d\n", __func__, k);
		dml2_printf("DML::%s: ReadBandwidthLuma=%f\n", __func__, ReadBandwidthLuma[k]);
		dml2_printf("DML::%s: ReadBandwidthChroma=%f\n", __func__, ReadBandwidthChroma[k]);
		dml2_printf("DML::%s: dcc_dram_bw_nom_overhead_factor_p0=%f\n", __func__, dcc_dram_bw_nom_overhead_factor_p0[k]);
		dml2_printf("DML::%s: dcc_dram_bw_nom_overhead_factor_p1=%f\n", __func__, dcc_dram_bw_nom_overhead_factor_p1[k]);
		dml2_printf("DML::%s: mall_prefetch_dram_overhead_factor=%f\n", __func__, mall_prefetch_dram_overhead_factor[k]);
		dml2_printf("DML::%s: mall_prefetch_sdp_overhead_factor=%f\n", __func__, mall_prefetch_sdp_overhead_factor[k]);
#endif

		sdp_overhead_factor = mall_prefetch_sdp_overhead_factor[k];
		dram_overhead_factor_p0 = dcc_dram_bw_nom_overhead_factor_p0[k] * mall_prefetch_dram_overhead_factor[k];
		dram_overhead_factor_p1 = dcc_dram_bw_nom_overhead_factor_p1[k] * mall_prefetch_dram_overhead_factor[k];

		// FIXME_DCN4, was missing cursor_bw in here, but do I actually need that and tdlut bw for average bandwidth calculation?
		// active avg bw not include phantom, but svp_prefetch avg bw should include phantom pipes
		if (!dml_is_phantom_pipe(&display_cfg->plane_descriptors[k])) {
			avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp] += sdp_overhead_factor * (ReadBandwidthLuma[k] + ReadBandwidthChroma[k]) + cursor_bw[k];
			avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram] += dram_overhead_factor_p0 * ReadBandwidthLuma[k] + dram_overhead_factor_p1 * ReadBandwidthChroma[k] + cursor_bw[k];
		}
		avg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp] += sdp_overhead_factor * (ReadBandwidthLuma[k] + ReadBandwidthChroma[k]) + cursor_bw[k];
		avg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram] += dram_overhead_factor_p0 * ReadBandwidthLuma[k] + dram_overhead_factor_p1 * ReadBandwidthChroma[k] + cursor_bw[k];

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: avg_bandwidth_required[%s][%s]=%f\n", __func__, dml2_core_internal_soc_state_type_str(dml2_core_internal_soc_state_sys_active), dml2_core_internal_bw_type_str(dml2_core_internal_bw_sdp), avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]);
		dml2_printf("DML::%s: avg_bandwidth_required[%s][%s]=%f\n", __func__, dml2_core_internal_soc_state_type_str(dml2_core_internal_soc_state_sys_active), dml2_core_internal_bw_type_str(dml2_core_internal_bw_dram), avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram]);
		dml2_printf("DML::%s: avg_bandwidth_required[%s][%s]=%f\n", __func__, dml2_core_internal_soc_state_type_str(dml2_core_internal_soc_state_svp_prefetch), dml2_core_internal_bw_type_str(dml2_core_internal_bw_sdp), avg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp]);
		dml2_printf("DML::%s: avg_bandwidth_required[%s][%s]=%f\n", __func__, dml2_core_internal_soc_state_type_str(dml2_core_internal_soc_state_svp_prefetch), dml2_core_internal_bw_type_str(dml2_core_internal_bw_dram), avg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram]);
#endif
	}
}

static void CalculateVMRowAndSwath(struct dml2_core_internal_scratch *scratch,
	struct dml2_core_calcs_CalculateVMRowAndSwath_params *p)
{
	struct dml2_core_calcs_CalculateVMRowAndSwath_locals *s = &scratch->CalculateVMRowAndSwath_locals;

	s->HostVMDynamicLevels = CalculateHostVMDynamicLevels(p->display_cfg->gpuvm_enable, p->display_cfg->hostvm_enable, p->HostVMMinPageSize, p->display_cfg->hostvm_max_non_cached_page_table_levels);

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->display_cfg->gpuvm_enable == true) {
			p->vm_group_bytes[k] = 512;
			p->dpte_group_bytes[k] = 512;
		} else {
			p->vm_group_bytes[k] = 0;
			p->dpte_group_bytes[k] = 0;
		}

		if (dml_is_420(p->myPipe[k].SourcePixelFormat) || p->myPipe[k].SourcePixelFormat == dml2_rgbe_alpha) {
			if ((p->myPipe[k].SourcePixelFormat == dml2_420_10 || p->myPipe[k].SourcePixelFormat == dml2_420_12) && !dml_is_vertical_rotation(p->myPipe[k].RotationAngle)) {
				s->PTEBufferSizeInRequestsForLuma[k] = (p->PTEBufferSizeInRequestsLuma + p->PTEBufferSizeInRequestsChroma) / 2;
				s->PTEBufferSizeInRequestsForChroma[k] = s->PTEBufferSizeInRequestsForLuma[k];
			} else {
				s->PTEBufferSizeInRequestsForLuma[k] = p->PTEBufferSizeInRequestsLuma;
				s->PTEBufferSizeInRequestsForChroma[k] = p->PTEBufferSizeInRequestsChroma;
			}

			scratch->calculate_vm_and_row_bytes_params.ViewportStationary = p->myPipe[k].ViewportStationary;
			scratch->calculate_vm_and_row_bytes_params.DCCEnable = p->myPipe[k].DCCEnable;
			scratch->calculate_vm_and_row_bytes_params.NumberOfDPPs = p->myPipe[k].DPPPerSurface;
			scratch->calculate_vm_and_row_bytes_params.BlockHeight256Bytes = p->myPipe[k].BlockHeight256BytesC;
			scratch->calculate_vm_and_row_bytes_params.BlockWidth256Bytes = p->myPipe[k].BlockWidth256BytesC;
			scratch->calculate_vm_and_row_bytes_params.SourcePixelFormat = p->myPipe[k].SourcePixelFormat;
			scratch->calculate_vm_and_row_bytes_params.SurfaceTiling = p->myPipe[k].SurfaceTiling;
			scratch->calculate_vm_and_row_bytes_params.BytePerPixel = p->myPipe[k].BytePerPixelC;
			scratch->calculate_vm_and_row_bytes_params.RotationAngle = p->myPipe[k].RotationAngle;
			scratch->calculate_vm_and_row_bytes_params.SwathWidth = p->SwathWidthC[k];
			scratch->calculate_vm_and_row_bytes_params.ViewportHeight = p->myPipe[k].ViewportHeightC;
			scratch->calculate_vm_and_row_bytes_params.ViewportXStart = p->myPipe[k].ViewportXStartC;
			scratch->calculate_vm_and_row_bytes_params.ViewportYStart = p->myPipe[k].ViewportYStartC;
			scratch->calculate_vm_and_row_bytes_params.GPUVMEnable = p->display_cfg->gpuvm_enable;
			scratch->calculate_vm_and_row_bytes_params.GPUVMMaxPageTableLevels = p->display_cfg->gpuvm_max_page_table_levels;
			scratch->calculate_vm_and_row_bytes_params.GPUVMMinPageSizeKBytes = p->display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes;
			scratch->calculate_vm_and_row_bytes_params.PTEBufferSizeInRequests = s->PTEBufferSizeInRequestsForChroma[k];
			scratch->calculate_vm_and_row_bytes_params.Pitch = p->myPipe[k].PitchC;
			scratch->calculate_vm_and_row_bytes_params.MacroTileWidth = p->myPipe[k].BlockWidthC;
			scratch->calculate_vm_and_row_bytes_params.MacroTileHeight = p->myPipe[k].BlockHeightC;
			scratch->calculate_vm_and_row_bytes_params.is_phantom = dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[k]);
			scratch->calculate_vm_and_row_bytes_params.DCCMetaPitch = p->myPipe[k].DCCMetaPitchC;
			scratch->calculate_vm_and_row_bytes_params.mrq_present = p->mrq_present;

			scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRow = &s->PixelPTEBytesPerRowC[k];
			scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRowStorage = &s->PixelPTEBytesPerRowStorageC[k];
			scratch->calculate_vm_and_row_bytes_params.dpte_row_width_ub = &p->dpte_row_width_chroma_ub[k];
			scratch->calculate_vm_and_row_bytes_params.dpte_row_height = &p->dpte_row_height_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.dpte_row_height_linear = &p->dpte_row_height_linear_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRow_one_row_per_frame = &s->PixelPTEBytesPerRowC_one_row_per_frame[k];
			scratch->calculate_vm_and_row_bytes_params.dpte_row_width_ub_one_row_per_frame = &s->dpte_row_width_chroma_ub_one_row_per_frame[k];
			scratch->calculate_vm_and_row_bytes_params.dpte_row_height_one_row_per_frame = &s->dpte_row_height_chroma_one_row_per_frame[k];
			scratch->calculate_vm_and_row_bytes_params.vmpg_width = &p->vmpg_width_c[k];
			scratch->calculate_vm_and_row_bytes_params.vmpg_height = &p->vmpg_height_c[k];
			scratch->calculate_vm_and_row_bytes_params.PixelPTEReqWidth = &p->PixelPTEReqWidthC[k];
			scratch->calculate_vm_and_row_bytes_params.PixelPTEReqHeight = &p->PixelPTEReqHeightC[k];
			scratch->calculate_vm_and_row_bytes_params.PTERequestSize = &p->PTERequestSizeC[k];
			scratch->calculate_vm_and_row_bytes_params.dpde0_bytes_per_frame_ub = &p->dpde0_bytes_per_frame_ub_c[k];

			scratch->calculate_vm_and_row_bytes_params.meta_row_bytes = &s->meta_row_bytes_per_row_ub_c[k];
			scratch->calculate_vm_and_row_bytes_params.MetaRequestWidth = &p->meta_req_width_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.MetaRequestHeight = &p->meta_req_height_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.meta_row_width = &p->meta_row_width_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.meta_row_height = &p->meta_row_height_chroma[k];
			scratch->calculate_vm_and_row_bytes_params.meta_pte_bytes_per_frame_ub = &p->meta_pte_bytes_per_frame_ub_c[k];

			s->vm_bytes_c = CalculateVMAndRowBytes(&scratch->calculate_vm_and_row_bytes_params);

			p->PrefetchSourceLinesC[k] = CalculatePrefetchSourceLines(
				p->myPipe[k].VRatioChroma,
				p->myPipe[k].VTapsChroma,
				p->myPipe[k].InterlaceEnable,
				p->myPipe[k].ProgressiveToInterlaceUnitInOPP,
				p->myPipe[k].SwathHeightC,
				p->myPipe[k].RotationAngle,
				p->myPipe[k].mirrored,
				p->myPipe[k].ViewportStationary,
				p->SwathWidthC[k],
				p->myPipe[k].ViewportHeightC,
				p->myPipe[k].ViewportXStartC,
				p->myPipe[k].ViewportYStartC,

				// Output
				&p->VInitPreFillC[k],
				&p->MaxNumSwathC[k]);
		} else {
			s->PTEBufferSizeInRequestsForLuma[k] = p->PTEBufferSizeInRequestsLuma + p->PTEBufferSizeInRequestsChroma;
			s->PTEBufferSizeInRequestsForChroma[k] = 0;
			s->PixelPTEBytesPerRowC[k] = 0;
			s->PixelPTEBytesPerRowStorageC[k] = 0;
			s->vm_bytes_c = 0;
			p->MaxNumSwathC[k] = 0;
			p->PrefetchSourceLinesC[k] = 0;
			s->dpte_row_height_chroma_one_row_per_frame[k] = 0;
			s->dpte_row_width_chroma_ub_one_row_per_frame[k] = 0;
			s->PixelPTEBytesPerRowC_one_row_per_frame[k] = 0;
		}

		scratch->calculate_vm_and_row_bytes_params.ViewportStationary = p->myPipe[k].ViewportStationary;
		scratch->calculate_vm_and_row_bytes_params.DCCEnable = p->myPipe[k].DCCEnable;
		scratch->calculate_vm_and_row_bytes_params.NumberOfDPPs = p->myPipe[k].DPPPerSurface;
		scratch->calculate_vm_and_row_bytes_params.BlockHeight256Bytes = p->myPipe[k].BlockHeight256BytesY;
		scratch->calculate_vm_and_row_bytes_params.BlockWidth256Bytes = p->myPipe[k].BlockWidth256BytesY;
		scratch->calculate_vm_and_row_bytes_params.SourcePixelFormat = p->myPipe[k].SourcePixelFormat;
		scratch->calculate_vm_and_row_bytes_params.SurfaceTiling = p->myPipe[k].SurfaceTiling;
		scratch->calculate_vm_and_row_bytes_params.BytePerPixel = p->myPipe[k].BytePerPixelY;
		scratch->calculate_vm_and_row_bytes_params.RotationAngle = p->myPipe[k].RotationAngle;
		scratch->calculate_vm_and_row_bytes_params.SwathWidth = p->SwathWidthY[k];
		scratch->calculate_vm_and_row_bytes_params.ViewportHeight = p->myPipe[k].ViewportHeight;
		scratch->calculate_vm_and_row_bytes_params.ViewportXStart = p->myPipe[k].ViewportXStart;
		scratch->calculate_vm_and_row_bytes_params.ViewportYStart = p->myPipe[k].ViewportYStart;
		scratch->calculate_vm_and_row_bytes_params.GPUVMEnable = p->display_cfg->gpuvm_enable;
		scratch->calculate_vm_and_row_bytes_params.GPUVMMaxPageTableLevels = p->display_cfg->gpuvm_max_page_table_levels;
		scratch->calculate_vm_and_row_bytes_params.GPUVMMinPageSizeKBytes = p->display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes;
		scratch->calculate_vm_and_row_bytes_params.PTEBufferSizeInRequests = s->PTEBufferSizeInRequestsForLuma[k];
		scratch->calculate_vm_and_row_bytes_params.Pitch = p->myPipe[k].PitchY;
		scratch->calculate_vm_and_row_bytes_params.MacroTileWidth = p->myPipe[k].BlockWidthY;
		scratch->calculate_vm_and_row_bytes_params.MacroTileHeight = p->myPipe[k].BlockHeightY;
		scratch->calculate_vm_and_row_bytes_params.is_phantom = dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[k]);
		scratch->calculate_vm_and_row_bytes_params.DCCMetaPitch = p->myPipe[k].DCCMetaPitchY;
		scratch->calculate_vm_and_row_bytes_params.mrq_present = p->mrq_present;

		scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRow = &s->PixelPTEBytesPerRowY[k];
		scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRowStorage = &s->PixelPTEBytesPerRowStorageY[k];
		scratch->calculate_vm_and_row_bytes_params.dpte_row_width_ub = &p->dpte_row_width_luma_ub[k];
		scratch->calculate_vm_and_row_bytes_params.dpte_row_height = &p->dpte_row_height_luma[k];
		scratch->calculate_vm_and_row_bytes_params.dpte_row_height_linear = &p->dpte_row_height_linear_luma[k];
		scratch->calculate_vm_and_row_bytes_params.PixelPTEBytesPerRow_one_row_per_frame = &s->PixelPTEBytesPerRowY_one_row_per_frame[k];
		scratch->calculate_vm_and_row_bytes_params.dpte_row_width_ub_one_row_per_frame = &s->dpte_row_width_luma_ub_one_row_per_frame[k];
		scratch->calculate_vm_and_row_bytes_params.dpte_row_height_one_row_per_frame = &s->dpte_row_height_luma_one_row_per_frame[k];
		scratch->calculate_vm_and_row_bytes_params.vmpg_width = &p->vmpg_width_y[k];
		scratch->calculate_vm_and_row_bytes_params.vmpg_height = &p->vmpg_height_y[k];
		scratch->calculate_vm_and_row_bytes_params.PixelPTEReqWidth = &p->PixelPTEReqWidthY[k];
		scratch->calculate_vm_and_row_bytes_params.PixelPTEReqHeight = &p->PixelPTEReqHeightY[k];
		scratch->calculate_vm_and_row_bytes_params.PTERequestSize = &p->PTERequestSizeY[k];
		scratch->calculate_vm_and_row_bytes_params.dpde0_bytes_per_frame_ub = &p->dpde0_bytes_per_frame_ub_l[k];

		scratch->calculate_vm_and_row_bytes_params.meta_row_bytes = &s->meta_row_bytes_per_row_ub_l[k];
		scratch->calculate_vm_and_row_bytes_params.MetaRequestWidth = &p->meta_req_width_luma[k];
		scratch->calculate_vm_and_row_bytes_params.MetaRequestHeight = &p->meta_req_height_luma[k];
		scratch->calculate_vm_and_row_bytes_params.meta_row_width = &p->meta_row_width_luma[k];
		scratch->calculate_vm_and_row_bytes_params.meta_row_height = &p->meta_row_height_luma[k];
		scratch->calculate_vm_and_row_bytes_params.meta_pte_bytes_per_frame_ub = &p->meta_pte_bytes_per_frame_ub_l[k];

		s->vm_bytes_l = CalculateVMAndRowBytes(&scratch->calculate_vm_and_row_bytes_params);

		p->PrefetchSourceLinesY[k] = CalculatePrefetchSourceLines(
			p->myPipe[k].VRatio,
			p->myPipe[k].VTaps,
			p->myPipe[k].InterlaceEnable,
			p->myPipe[k].ProgressiveToInterlaceUnitInOPP,
			p->myPipe[k].SwathHeightY,
			p->myPipe[k].RotationAngle,
			p->myPipe[k].mirrored,
			p->myPipe[k].ViewportStationary,
			p->SwathWidthY[k],
			p->myPipe[k].ViewportHeight,
			p->myPipe[k].ViewportXStart,
			p->myPipe[k].ViewportYStart,

			// Output
			&p->VInitPreFillY[k],
			&p->MaxNumSwathY[k]);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, vm_bytes_l = %u (before hvm level)\n", __func__, k, s->vm_bytes_l);
		dml2_printf("DML::%s: k=%u, vm_bytes_c = %u (before hvm level)\n", __func__, k, s->vm_bytes_c);
		dml2_printf("DML::%s: k=%u, meta_row_bytes_per_row_ub_l = %u\n", __func__, k, s->meta_row_bytes_per_row_ub_l[k]);
		dml2_printf("DML::%s: k=%u, meta_row_bytes_per_row_ub_c = %u\n", __func__, k, s->meta_row_bytes_per_row_ub_c[k]);
#endif
		p->vm_bytes[k] = (s->vm_bytes_l + s->vm_bytes_c) * (1 + 8 * s->HostVMDynamicLevels);
		p->meta_row_bytes[k] = s->meta_row_bytes_per_row_ub_l[k] + s->meta_row_bytes_per_row_ub_c[k];
		p->meta_row_bytes_per_row_ub_l[k] = s->meta_row_bytes_per_row_ub_l[k];
		p->meta_row_bytes_per_row_ub_c[k] = s->meta_row_bytes_per_row_ub_c[k];

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, meta_row_bytes = %u\n", __func__, k, p->meta_row_bytes[k]);
		dml2_printf("DML::%s: k=%u, vm_bytes = %u (after hvm level)\n", __func__, k, p->vm_bytes[k]);
#endif
		if (s->PixelPTEBytesPerRowStorageY[k] <= 64 * s->PTEBufferSizeInRequestsForLuma[k] && s->PixelPTEBytesPerRowStorageC[k] <= 64 * s->PTEBufferSizeInRequestsForChroma[k]) {
			p->PTEBufferSizeNotExceeded[k] = true;
		} else {
			p->PTEBufferSizeNotExceeded[k] = false;
		}

		s->one_row_per_frame_fits_in_buffer[k] = (s->PixelPTEBytesPerRowY_one_row_per_frame[k] <= 64 * 2 * s->PTEBufferSizeInRequestsForLuma[k] &&
												s->PixelPTEBytesPerRowC_one_row_per_frame[k] <= 64 * 2 * s->PTEBufferSizeInRequestsForChroma[k]);
#ifdef __DML_VBA_DEBUG__
		if (p->PTEBufferSizeNotExceeded[k] == 0 || s->one_row_per_frame_fits_in_buffer[k] == 0) {
			dml2_printf("DML::%s: k=%u, PixelPTEBytesPerRowY = %u (before hvm level)\n", __func__, k, s->PixelPTEBytesPerRowY[k]);
			dml2_printf("DML::%s: k=%u, PixelPTEBytesPerRowC = %u (before hvm level)\n", __func__, k, s->PixelPTEBytesPerRowC[k]);
			dml2_printf("DML::%s: k=%u, PixelPTEBytesPerRowStorageY = %u\n", __func__, k, s->PixelPTEBytesPerRowStorageY[k]);
			dml2_printf("DML::%s: k=%u, PixelPTEBytesPerRowStorageC = %u\n", __func__, k, s->PixelPTEBytesPerRowStorageC[k]);
			dml2_printf("DML::%s: k=%u, PTEBufferSizeInRequestsForLuma = %u\n", __func__, k, s->PTEBufferSizeInRequestsForLuma[k]);
			dml2_printf("DML::%s: k=%u, PTEBufferSizeInRequestsForChroma = %u\n", __func__, k, s->PTEBufferSizeInRequestsForChroma[k]);
			dml2_printf("DML::%s: k=%u, PTEBufferSizeNotExceeded (not one_row_per_frame) = %u\n", __func__, k, p->PTEBufferSizeNotExceeded[k]);

			dml2_printf("DML::%s: k=%u, HostVMDynamicLevels = %u\n", __func__, k, s->HostVMDynamicLevels);
			dml2_printf("DML::%s: k=%u, PixelPTEBytesPerRowY_one_row_per_frame = %u\n", __func__, k, s->PixelPTEBytesPerRowY_one_row_per_frame[k]);
			dml2_printf("DML::%s: k=%u, PixelPTEBytesPerRowC_one_row_per_frame = %u\n", __func__, k, s->PixelPTEBytesPerRowC_one_row_per_frame[k]);
			dml2_printf("DML::%s: k=%u, one_row_per_frame_fits_in_buffer = %u\n", __func__, k, s->one_row_per_frame_fits_in_buffer[k]);
		}
#endif
	}

	CalculateMALLUseForStaticScreen(
		p->display_cfg,
		p->NumberOfActiveSurfaces,
		p->MALLAllocatedForDCN,
		p->SurfaceSizeInMALL,
		s->one_row_per_frame_fits_in_buffer,
		// Output
		p->is_using_mall_for_ss);

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->display_cfg->gpuvm_enable) {
			if (p->display_cfg->plane_descriptors[k].overrides.hw.force_pte_buffer_mode.enable == 1) {
				p->PTE_BUFFER_MODE[k] = p->display_cfg->plane_descriptors[k].overrides.hw.force_pte_buffer_mode.value;
			}
			p->PTE_BUFFER_MODE[k] = p->myPipe[k].FORCE_ONE_ROW_FOR_FRAME || p->is_using_mall_for_ss[k] || (p->display_cfg->plane_descriptors[k].overrides.legacy_svp_config == dml2_svp_mode_override_main_pipe) ||
				dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[k]) || (p->display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes > 64);
			p->BIGK_FRAGMENT_SIZE[k] = (unsigned int)(math_log((float)p->display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes * 1024, 2) - 12);
		} else {
			p->PTE_BUFFER_MODE[k] = 0;
			p->BIGK_FRAGMENT_SIZE[k] = 0;
		}
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		p->DCCMetaBufferSizeNotExceeded[k] = true;
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, SurfaceSizeInMALL = %u\n", __func__, k, p->SurfaceSizeInMALL[k]);
		dml2_printf("DML::%s: k=%u, is_using_mall_for_ss = %u\n", __func__, k, p->is_using_mall_for_ss[k]);
#endif
		p->use_one_row_for_frame[k] = p->myPipe[k].FORCE_ONE_ROW_FOR_FRAME || p->is_using_mall_for_ss[k] || (p->display_cfg->plane_descriptors[k].overrides.legacy_svp_config == dml2_svp_mode_override_main_pipe) ||
			(dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[k])) || (p->display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes > 64 && dml_is_vertical_rotation(p->myPipe[k].RotationAngle));

		p->use_one_row_for_frame_flip[k] = p->use_one_row_for_frame[k] && !(p->display_cfg->plane_descriptors[k].overrides.uclk_pstate_change_strategy == dml2_uclk_pstate_change_strategy_force_mall_full_frame);

		if (p->use_one_row_for_frame[k]) {
			p->dpte_row_height_luma[k] = s->dpte_row_height_luma_one_row_per_frame[k];
			p->dpte_row_width_luma_ub[k] = s->dpte_row_width_luma_ub_one_row_per_frame[k];
			s->PixelPTEBytesPerRowY[k] = s->PixelPTEBytesPerRowY_one_row_per_frame[k];
			p->dpte_row_height_chroma[k] = s->dpte_row_height_chroma_one_row_per_frame[k];
			p->dpte_row_width_chroma_ub[k] = s->dpte_row_width_chroma_ub_one_row_per_frame[k];
			s->PixelPTEBytesPerRowC[k] = s->PixelPTEBytesPerRowC_one_row_per_frame[k];
			p->PTEBufferSizeNotExceeded[k] = s->one_row_per_frame_fits_in_buffer[k];
		}

		if (p->meta_row_bytes[k] <= p->DCCMetaBufferSizeBytes) {
			p->DCCMetaBufferSizeNotExceeded[k] = true;
		} else {
			p->DCCMetaBufferSizeNotExceeded[k] = false;

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%d, meta_row_bytes = %d\n",  __func__, k, p->meta_row_bytes[k]);
			dml2_printf("DML::%s: k=%d, DCCMetaBufferSizeBytes = %d\n",  __func__, k, p->DCCMetaBufferSizeBytes);
			dml2_printf("DML::%s: k=%d, DCCMetaBufferSizeNotExceeded = %d\n",  __func__, k, p->DCCMetaBufferSizeNotExceeded[k]);
#endif
		}

		s->PixelPTEBytesPerRowY[k] = s->PixelPTEBytesPerRowY[k] * (1 + 8 * s->HostVMDynamicLevels);
		s->PixelPTEBytesPerRowC[k] = s->PixelPTEBytesPerRowC[k] * (1 + 8 * s->HostVMDynamicLevels);
		p->PixelPTEBytesPerRow[k] = s->PixelPTEBytesPerRowY[k] + s->PixelPTEBytesPerRowC[k];
		p->dpte_row_bytes_per_row_l[k] = s->PixelPTEBytesPerRowY[k];
		p->dpte_row_bytes_per_row_c[k] = s->PixelPTEBytesPerRowC[k];

		// if one row of dPTEs is meant to span the entire frame, then for these calculations, we will pretend like that one big row is fetched in two halfs
		if (p->use_one_row_for_frame[k])
			p->PixelPTEBytesPerRow[k] = p->PixelPTEBytesPerRow[k] / 2;

		CalculateRowBandwidth(
			p->display_cfg->gpuvm_enable,
			p->use_one_row_for_frame[k],
			p->myPipe[k].SourcePixelFormat,
			p->myPipe[k].VRatio,
			p->myPipe[k].VRatioChroma,
			p->myPipe[k].DCCEnable,
			p->myPipe[k].HTotal / p->myPipe[k].PixelClock,
			s->PixelPTEBytesPerRowY[k],
			s->PixelPTEBytesPerRowC[k],
			p->dpte_row_height_luma[k],
			p->dpte_row_height_chroma[k],

			p->mrq_present,
			p->meta_row_bytes_per_row_ub_l[k],
			p->meta_row_bytes_per_row_ub_c[k],
			p->meta_row_height_luma[k],
			p->meta_row_height_chroma[k],

			// Output
			&p->dpte_row_bw[k],
			&p->meta_row_bw[k]);
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, use_one_row_for_frame = %u\n", __func__, k, p->use_one_row_for_frame[k]);
		dml2_printf("DML::%s: k=%u, use_one_row_for_frame_flip = %u\n", __func__, k, p->use_one_row_for_frame_flip[k]);
		dml2_printf("DML::%s: k=%u, UseMALLForPStateChange = %u\n", __func__, k, p->display_cfg->plane_descriptors[k].overrides.legacy_svp_config);
		dml2_printf("DML::%s: k=%u, dpte_row_height_luma = %u\n", __func__, k, p->dpte_row_height_luma[k]);
		dml2_printf("DML::%s: k=%u, dpte_row_width_luma_ub = %u\n", __func__, k, p->dpte_row_width_luma_ub[k]);
		dml2_printf("DML::%s: k=%u, PixelPTEBytesPerRowY = %u (after hvm level)\n", __func__, k, s->PixelPTEBytesPerRowY[k]);
		dml2_printf("DML::%s: k=%u, dpte_row_height_chroma = %u\n", __func__, k, p->dpte_row_height_chroma[k]);
		dml2_printf("DML::%s: k=%u, dpte_row_width_chroma_ub = %u\n", __func__, k, p->dpte_row_width_chroma_ub[k]);
		dml2_printf("DML::%s: k=%u, PixelPTEBytesPerRowC = %u (after hvm level)\n", __func__, k, s->PixelPTEBytesPerRowC[k]);
		dml2_printf("DML::%s: k=%u, PixelPTEBytesPerRow = %u\n", __func__, k, p->PixelPTEBytesPerRow[k]);
		dml2_printf("DML::%s: k=%u, PTEBufferSizeNotExceeded = %u\n", __func__, k, p->PTEBufferSizeNotExceeded[k]);
		dml2_printf("DML::%s: k=%u, gpuvm_enable = %u\n", __func__, k, p->display_cfg->gpuvm_enable);
		dml2_printf("DML::%s: k=%u, PTE_BUFFER_MODE = %u\n", __func__, k, p->PTE_BUFFER_MODE[k]);
		dml2_printf("DML::%s: k=%u, BIGK_FRAGMENT_SIZE = %u\n", __func__, k, p->BIGK_FRAGMENT_SIZE[k]);
#endif
	}
}

static double CalculateUrgentLatency(
	double UrgentLatencyPixelDataOnly,
	double UrgentLatencyPixelMixedWithVMData,
	double UrgentLatencyVMDataOnly,
	bool DoUrgentLatencyAdjustment,
	double UrgentLatencyAdjustmentFabricClockComponent,
	double UrgentLatencyAdjustmentFabricClockReference,
	double FabricClock,
	double uclk_freq_mhz,
	enum dml2_qos_param_type qos_type,
	unsigned int urgent_ramp_uclk_cycles,
	unsigned int df_qos_response_time_fclk_cycles,
	unsigned int max_round_trip_to_furthest_cs_fclk_cycles,
	unsigned int mall_overhead_fclk_cycles,
	double umc_urgent_ramp_latency_margin,
	double fabric_max_transport_latency_margin)
{
	double urgent_latency = 0;
	if (qos_type == dml2_qos_param_type_dcn4x) {
		urgent_latency = (df_qos_response_time_fclk_cycles + mall_overhead_fclk_cycles) / FabricClock
			+ max_round_trip_to_furthest_cs_fclk_cycles / FabricClock * (1 + fabric_max_transport_latency_margin / 100.0)
			+ urgent_ramp_uclk_cycles / uclk_freq_mhz * (1 + umc_urgent_ramp_latency_margin / 100.0);
	} else {
		urgent_latency = math_max3(UrgentLatencyPixelDataOnly, UrgentLatencyPixelMixedWithVMData, UrgentLatencyVMDataOnly);
		if (DoUrgentLatencyAdjustment == true) {
			urgent_latency = urgent_latency + UrgentLatencyAdjustmentFabricClockComponent * (UrgentLatencyAdjustmentFabricClockReference / FabricClock - 1);
		}
	}
#ifdef __DML_VBA_DEBUG__
	if (qos_type == dml2_qos_param_type_dcn4x) {
		dml2_printf("DML::%s: qos_type = %d\n", __func__, qos_type);
		dml2_printf("DML::%s: urgent_ramp_uclk_cycles = %d\n", __func__, urgent_ramp_uclk_cycles);
		dml2_printf("DML::%s: uclk_freq_mhz = %f\n", __func__, uclk_freq_mhz);
		dml2_printf("DML::%s: umc_urgent_ramp_latency_margin = %f\n", __func__, umc_urgent_ramp_latency_margin);
	} else {
		dml2_printf("DML::%s: UrgentLatencyPixelDataOnly = %f\n", __func__, UrgentLatencyPixelDataOnly);
		dml2_printf("DML::%s: UrgentLatencyPixelMixedWithVMData = %f\n", __func__, UrgentLatencyPixelMixedWithVMData);
		dml2_printf("DML::%s: UrgentLatencyVMDataOnly = %f\n", __func__, UrgentLatencyVMDataOnly);
		dml2_printf("DML::%s: UrgentLatencyAdjustmentFabricClockComponent = %f\n", __func__, UrgentLatencyAdjustmentFabricClockComponent);
		dml2_printf("DML::%s: UrgentLatencyAdjustmentFabricClockReference = %f\n", __func__, UrgentLatencyAdjustmentFabricClockReference);
	}
	dml2_printf("DML::%s: FabricClock = %f\n", __func__, FabricClock);
	dml2_printf("DML::%s: UrgentLatency = %f\n", __func__, urgent_latency);
#endif
	return urgent_latency;
}

static double CalculateTripToMemory(
	double UrgLatency,
	double FabricClock,
	double uclk_freq_mhz,
	enum dml2_qos_param_type qos_type,
	unsigned int trip_to_memory_uclk_cycles,
	unsigned int max_round_trip_to_furthest_cs_fclk_cycles,
	unsigned int mall_overhead_fclk_cycles,
	double umc_max_latency_margin,
	double fabric_max_transport_latency_margin)
{
	double trip_to_memory_us;
	if (qos_type == dml2_qos_param_type_dcn4x) {
		trip_to_memory_us = mall_overhead_fclk_cycles / FabricClock
			+ max_round_trip_to_furthest_cs_fclk_cycles / FabricClock * (1.0 + fabric_max_transport_latency_margin / 100.0)
			+ trip_to_memory_uclk_cycles / uclk_freq_mhz * (1.0 + umc_max_latency_margin / 100.0);
	} else {
		trip_to_memory_us = UrgLatency;
	}

#ifdef __DML_VBA_DEBUG__
	if (qos_type == dml2_qos_param_type_dcn4x) {
		dml2_printf("DML::%s: qos_type = %d\n", __func__, qos_type);
		dml2_printf("DML::%s: max_round_trip_to_furthest_cs_fclk_cycles = %d\n", __func__, max_round_trip_to_furthest_cs_fclk_cycles);
		dml2_printf("DML::%s: mall_overhead_fclk_cycles = %d\n", __func__, mall_overhead_fclk_cycles);
		dml2_printf("DML::%s: trip_to_memory_uclk_cycles = %d\n", __func__, trip_to_memory_uclk_cycles);
		dml2_printf("DML::%s: uclk_freq_mhz = %f\n", __func__, uclk_freq_mhz);
		dml2_printf("DML::%s: FabricClock = %f\n", __func__, FabricClock);
		dml2_printf("DML::%s: fabric_max_transport_latency_margin = %f\n", __func__, fabric_max_transport_latency_margin);
		dml2_printf("DML::%s: umc_max_latency_margin = %f\n", __func__, umc_max_latency_margin);
	} else {
		dml2_printf("DML::%s: UrgLatency = %f\n", __func__, UrgLatency);
	}
	dml2_printf("DML::%s: trip_to_memory_us = %f\n", __func__, trip_to_memory_us);
#endif


	return trip_to_memory_us;
}

static double CalculateMetaTripToMemory(
	double UrgLatency,
	double FabricClock,
	double uclk_freq_mhz,
	enum dml2_qos_param_type qos_type,
	unsigned int meta_trip_to_memory_uclk_cycles,
	unsigned int meta_trip_to_memory_fclk_cycles,
	double umc_max_latency_margin,
	double fabric_max_transport_latency_margin)
{
	double meta_trip_to_memory_us;
	if (qos_type == dml2_qos_param_type_dcn4x) {
		meta_trip_to_memory_us = meta_trip_to_memory_fclk_cycles / FabricClock * (1.0 + fabric_max_transport_latency_margin / 100.0)
			+ meta_trip_to_memory_uclk_cycles / uclk_freq_mhz * (1.0 + umc_max_latency_margin / 100.0);
	} else {
		meta_trip_to_memory_us = UrgLatency;
	}

#ifdef __DML_VBA_DEBUG__
	if (qos_type == dml2_qos_param_type_dcn4x) {
		dml2_printf("DML::%s: qos_type = %d\n", __func__, qos_type);
		dml2_printf("DML::%s: meta_trip_to_memory_fclk_cycles = %d\n", __func__, meta_trip_to_memory_fclk_cycles);
		dml2_printf("DML::%s: meta_trip_to_memory_uclk_cycles = %d\n", __func__, meta_trip_to_memory_uclk_cycles);
		dml2_printf("DML::%s: uclk_freq_mhz = %f\n", __func__, uclk_freq_mhz);
	} else {
		dml2_printf("DML::%s: UrgLatency = %f\n", __func__, UrgLatency);
	}
	dml2_printf("DML::%s: meta_trip_to_memory_us = %f\n", __func__, meta_trip_to_memory_us);
#endif


	return meta_trip_to_memory_us;
}

static void calculate_cursor_req_attributes(
	unsigned int cursor_width,
	unsigned int cursor_bpp,

	// output
	unsigned int *cursor_lines_per_chunk,
	unsigned int *cursor_bytes_per_line,
	unsigned int *cursor_bytes_per_chunk,
	unsigned int *cursor_bytes)
{
	unsigned int cursor_pitch = 0;
	unsigned int cursor_bytes_per_req = 0;
	unsigned int cursor_width_bytes = 0;
	unsigned int cursor_height = 0;

	//SW determines the cursor pitch to support the maximum cursor_width that will be used but the following restrictions apply.
	//- For 2bpp, cursor_pitch = 256 pixels due to min cursor request size of 64B
	//- For 32 or 64 bpp, cursor_pitch = 64, 128 or 256 pixels depending on the cursor width
	if (cursor_bpp == 2)
		cursor_pitch = 256;
	else
		cursor_pitch = (unsigned int)1 << (unsigned int)math_ceil2(math_log((float)cursor_width, 2), 1);

	//The cursor requestor uses a cursor request size of 64B, 128B, or 256B depending on the cursor_width and cursor_bpp as follows.

	cursor_width_bytes = (unsigned int)math_ceil2((double)cursor_width * cursor_bpp / 8, 1);
	if (cursor_width_bytes <= 64)
		cursor_bytes_per_req = 64;
	else if (cursor_width_bytes <= 128)
		cursor_bytes_per_req = 128;
	else
		cursor_bytes_per_req = 256;

	//If cursor_width_bytes is greater than 256B, then multiple 256B requests are issued to fetch the entire cursor line.
	*cursor_bytes_per_line = (unsigned int)math_ceil2((double)cursor_width_bytes, cursor_bytes_per_req);

	//Nominally, the cursor chunk is 1KB or 2KB but it is restricted to a power of 2 number of lines with a maximum of 16 lines.
	if (cursor_bpp == 2) {
		*cursor_lines_per_chunk = 16;
	} else if (cursor_bpp == 32) {
		if (cursor_width <= 32)
			*cursor_lines_per_chunk = 16;
		else if (cursor_width <= 64)
			*cursor_lines_per_chunk = 8;
		else if (cursor_width <= 128)
			*cursor_lines_per_chunk = 4;
		else
			*cursor_lines_per_chunk = 2;
	} else if (cursor_bpp == 64) {
		if (cursor_width <= 16)
			*cursor_lines_per_chunk = 16;
		else if (cursor_width <= 32)
			*cursor_lines_per_chunk = 8;
		else if (cursor_width <= 64)
			*cursor_lines_per_chunk = 4;
		else if (cursor_width <= 128)
			*cursor_lines_per_chunk = 2;
		else
			*cursor_lines_per_chunk = 1;
	} else {
		if (cursor_width > 0) {
			dml2_printf("DML::%s: Invalid cursor_bpp = %d\n", __func__, cursor_bpp);
			dml2_assert(0);
		}
	}

	*cursor_bytes_per_chunk = *cursor_bytes_per_line * *cursor_lines_per_chunk;

	// For the cursor implementation, all requested data is stored in the return buffer. Given this fact, the cursor_bytes can be directly compared with the CursorBufferSize.
	// Only cursor_width is provided for worst case sizing so assume that the cursor is square
	cursor_height = cursor_width;
	*cursor_bytes = *cursor_bytes_per_line * cursor_height;
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: cursor_bpp = %d\n", __func__, cursor_bpp);
	dml2_printf("DML::%s: cursor_width = %d\n", __func__, cursor_width);
	dml2_printf("DML::%s: cursor_width_bytes = %d\n", __func__, cursor_width_bytes);
	dml2_printf("DML::%s: cursor_bytes_per_req = %d\n", __func__, cursor_bytes_per_req);
	dml2_printf("DML::%s: cursor_lines_per_chunk = %d\n", __func__, *cursor_lines_per_chunk);
	dml2_printf("DML::%s: cursor_bytes_per_line = %d\n", __func__, *cursor_bytes_per_line);
	dml2_printf("DML::%s: cursor_bytes_per_chunk = %d\n", __func__, *cursor_bytes_per_chunk);
	dml2_printf("DML::%s: cursor_bytes = %d\n", __func__, *cursor_bytes);
	dml2_printf("DML::%s: cursor_pitch = %d\n", __func__, cursor_pitch);
#endif
}

static void calculate_cursor_urgent_burst_factor(
	unsigned int CursorBufferSize,
	unsigned int CursorWidth,
	unsigned int cursor_bytes_per_chunk,
	unsigned int cursor_lines_per_chunk,
	double LineTime,
	double UrgentLatency,

	double *UrgentBurstFactorCursor,
	bool *NotEnoughUrgentLatencyHiding)
{
	unsigned int LinesInCursorBuffer = 0;
	double CursorBufferSizeInTime = 0;

	if (CursorWidth > 0) {
		LinesInCursorBuffer = (unsigned int)math_floor2(CursorBufferSize * 1024.0 / (double)cursor_bytes_per_chunk, 1) * cursor_lines_per_chunk;

		CursorBufferSizeInTime = LinesInCursorBuffer * LineTime;
		if (CursorBufferSizeInTime - UrgentLatency <= 0) {
			*NotEnoughUrgentLatencyHiding = 1;
			*UrgentBurstFactorCursor = 0;
		} else {
			*NotEnoughUrgentLatencyHiding = 0;
			*UrgentBurstFactorCursor = CursorBufferSizeInTime / (CursorBufferSizeInTime - UrgentLatency);
		}

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: LinesInCursorBuffer = %u\n", __func__, LinesInCursorBuffer);
		dml2_printf("DML::%s: CursorBufferSizeInTime = %f\n", __func__, CursorBufferSizeInTime);
		dml2_printf("DML::%s: CursorBufferSize = %u (kbytes)\n", __func__, CursorBufferSize);
		dml2_printf("DML::%s: cursor_bytes_per_chunk = %u\n", __func__, cursor_bytes_per_chunk);
		dml2_printf("DML::%s: cursor_lines_per_chunk = %u\n", __func__, cursor_lines_per_chunk);
		dml2_printf("DML::%s: UrgentBurstFactorCursor = %f\n", __func__, *UrgentBurstFactorCursor);
		dml2_printf("DML::%s: NotEnoughUrgentLatencyHiding = %d\n", __func__, *NotEnoughUrgentLatencyHiding);
#endif

	}
}

static void CalculateUrgentBurstFactor(
	const struct dml2_plane_parameters *plane_cfg,
	unsigned int swath_width_luma_ub,
	unsigned int swath_width_chroma_ub,
	unsigned int SwathHeightY,
	unsigned int SwathHeightC,
	double LineTime,
	double UrgentLatency,
	double VRatio,
	double VRatioC,
	double BytePerPixelInDETY,
	double BytePerPixelInDETC,
	unsigned int DETBufferSizeY,
	unsigned int DETBufferSizeC,
	// Output
	double *UrgentBurstFactorLuma,
	double *UrgentBurstFactorChroma,
	bool *NotEnoughUrgentLatencyHiding)
{
	double LinesInDETLuma;
	double LinesInDETChroma;
	double DETBufferSizeInTimeLuma;
	double DETBufferSizeInTimeChroma;

	*NotEnoughUrgentLatencyHiding = 0;
	*UrgentBurstFactorLuma = 0;
	*UrgentBurstFactorChroma = 0;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: VRatio = %f\n", __func__, VRatio);
	dml2_printf("DML::%s: VRatioC = %f\n", __func__, VRatioC);
	dml2_printf("DML::%s: DETBufferSizeY = %d\n", __func__, DETBufferSizeY);
	dml2_printf("DML::%s: DETBufferSizeC = %d\n", __func__, DETBufferSizeC);
	dml2_printf("DML::%s: BytePerPixelInDETY = %f\n", __func__, BytePerPixelInDETY);
	dml2_printf("DML::%s: swath_width_luma_ub = %d\n", __func__, swath_width_luma_ub);
	dml2_printf("DML::%s: LineTime = %f\n", __func__, LineTime);
#endif
	DML2_ASSERT(VRatio > 0);

	LinesInDETLuma = (dml_is_phantom_pipe(plane_cfg) ? 1024 * 1024 : DETBufferSizeY) / BytePerPixelInDETY / swath_width_luma_ub;

	DETBufferSizeInTimeLuma = math_floor2(LinesInDETLuma, SwathHeightY) * LineTime / VRatio;
	if (DETBufferSizeInTimeLuma - UrgentLatency <= 0) {
		*NotEnoughUrgentLatencyHiding = 1;
		*UrgentBurstFactorLuma = 0;
	} else {
		*UrgentBurstFactorLuma = DETBufferSizeInTimeLuma / (DETBufferSizeInTimeLuma - UrgentLatency);
	}

	if (BytePerPixelInDETC > 0) {
		LinesInDETChroma = (dml_is_phantom_pipe(plane_cfg) ? 1024 * 1024 : DETBufferSizeC) / BytePerPixelInDETC / swath_width_chroma_ub;

		DETBufferSizeInTimeChroma = math_floor2(LinesInDETChroma, SwathHeightC) * LineTime / VRatioC;
		if (DETBufferSizeInTimeChroma - UrgentLatency <= 0) {
			*NotEnoughUrgentLatencyHiding = 1;
			*UrgentBurstFactorChroma = 0;
		} else {
			*UrgentBurstFactorChroma = DETBufferSizeInTimeChroma / (DETBufferSizeInTimeChroma - UrgentLatency);
		}
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: LinesInDETLuma = %f\n", __func__, LinesInDETLuma);
	dml2_printf("DML::%s: UrgentLatency = %f\n", __func__, UrgentLatency);
	dml2_printf("DML::%s: DETBufferSizeInTimeLuma = %f\n", __func__, DETBufferSizeInTimeLuma);
	dml2_printf("DML::%s: UrgentBurstFactorLuma = %f\n", __func__, *UrgentBurstFactorLuma);
	dml2_printf("DML::%s: UrgentBurstFactorChroma = %f\n", __func__, *UrgentBurstFactorChroma);
	dml2_printf("DML::%s: NotEnoughUrgentLatencyHiding = %d\n", __func__, *NotEnoughUrgentLatencyHiding);
#endif

}

static void CalculateDCFCLKDeepSleep(
	const struct dml2_display_cfg *display_cfg,
	unsigned int NumberOfActiveSurfaces,
	unsigned int BytePerPixelY[],
	unsigned int BytePerPixelC[],
	unsigned int SwathWidthY[],
	unsigned int SwathWidthC[],
	unsigned int DPPPerSurface[],
	double PSCL_THROUGHPUT[],
	double PSCL_THROUGHPUT_CHROMA[],
	double Dppclk[],
	double ReadBandwidthLuma[],
	double ReadBandwidthChroma[],
	unsigned int ReturnBusWidth,

	// Output
	double *DCFClkDeepSleep)
{
	double DisplayPipeLineDeliveryTimeLuma;
	double DisplayPipeLineDeliveryTimeChroma;
	double DCFClkDeepSleepPerSurface[DML2_MAX_PLANES];
	double ReadBandwidth = 0.0;

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		double pixel_rate_mhz = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);

		if (display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio <= 1) {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] * DPPPerSurface[k] / display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio / pixel_rate_mhz;
		} else {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}
		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma = 0;
		} else {
			if (display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio <= 1) {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthC[k] * DPPPerSurface[k] / display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio / pixel_rate_mhz;
			} else {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthC[k] / PSCL_THROUGHPUT_CHROMA[k] / Dppclk[k];
			}
		}

		if (BytePerPixelC[k] > 0) {
			DCFClkDeepSleepPerSurface[k] = math_max2(__DML2_CALCS_DCFCLK_FACTOR__ * SwathWidthY[k] * BytePerPixelY[k] / 32.0 / DisplayPipeLineDeliveryTimeLuma,
				__DML2_CALCS_DCFCLK_FACTOR__ * SwathWidthC[k] * BytePerPixelC[k] / 32.0 / DisplayPipeLineDeliveryTimeChroma);
		} else {
			DCFClkDeepSleepPerSurface[k] = __DML2_CALCS_DCFCLK_FACTOR__ * SwathWidthY[k] * BytePerPixelY[k] / 64.0 / DisplayPipeLineDeliveryTimeLuma;
		}
		DCFClkDeepSleepPerSurface[k] = math_max2(DCFClkDeepSleepPerSurface[k], pixel_rate_mhz / 16);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, PixelClock = %f\n", __func__, k, pixel_rate_mhz);
		dml2_printf("DML::%s: k=%u, DCFClkDeepSleepPerSurface = %f\n", __func__, k, DCFClkDeepSleepPerSurface[k]);
#endif
	}

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		ReadBandwidth = ReadBandwidth + ReadBandwidthLuma[k] + ReadBandwidthChroma[k];
	}

	*DCFClkDeepSleep = math_max2(8.0, __DML2_CALCS_DCFCLK_FACTOR__ * ReadBandwidth / (double)ReturnBusWidth);

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: __DML2_CALCS_DCFCLK_FACTOR__ = %f\n", __func__, __DML2_CALCS_DCFCLK_FACTOR__);
	dml2_printf("DML::%s: ReadBandwidth = %f\n", __func__, ReadBandwidth);
	dml2_printf("DML::%s: ReturnBusWidth = %u\n", __func__, ReturnBusWidth);
	dml2_printf("DML::%s: DCFClkDeepSleep = %f\n", __func__, *DCFClkDeepSleep);
#endif

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		*DCFClkDeepSleep = math_max2(*DCFClkDeepSleep, DCFClkDeepSleepPerSurface[k]);
	}
	dml2_printf("DML::%s: DCFClkDeepSleep = %f (final)\n", __func__, *DCFClkDeepSleep);
}

static double CalculateWriteBackDelay(
	enum dml2_source_format_class WritebackPixelFormat,
	double WritebackHRatio,
	double WritebackVRatio,
	unsigned int WritebackVTaps,
	unsigned int WritebackDestinationWidth,
	unsigned int WritebackDestinationHeight,
	unsigned int WritebackSourceHeight,
	unsigned int HTotal)
{
	double CalculateWriteBackDelay;
	double Line_length;
	double Output_lines_last_notclamped;
	double WritebackVInit;

	WritebackVInit = (WritebackVRatio + WritebackVTaps + 1) / 2;
	Line_length = math_max2((double)WritebackDestinationWidth, math_ceil2((double)WritebackDestinationWidth / 6.0, 1.0) * WritebackVTaps);
	Output_lines_last_notclamped = WritebackDestinationHeight - 1 - math_ceil2(((double)WritebackSourceHeight - (double)WritebackVInit) / (double)WritebackVRatio, 1.0);
	if (Output_lines_last_notclamped < 0) {
		CalculateWriteBackDelay = 0;
	} else {
		CalculateWriteBackDelay = Output_lines_last_notclamped * Line_length + (HTotal - WritebackDestinationWidth) + 80;
	}
	return CalculateWriteBackDelay;
}

static unsigned int CalculateMaxVStartup(
	bool ptoi_supported,
	unsigned int vblank_nom_default_us,
	const struct dml2_timing_cfg *timing,
	double write_back_delay_us)
{
	unsigned int vblank_size = 0;
	unsigned int max_vstartup_lines = 0;

	double line_time_us = (double)timing->h_total / ((double)timing->pixel_clock_khz / 1000);
	unsigned int vblank_actual = timing->v_total - timing->v_active;
	unsigned int vblank_nom_default_in_line = (unsigned int)math_floor2((double)vblank_nom_default_us / line_time_us, 1.0);
	unsigned int vblank_nom_input = (unsigned int)math_min2(timing->vblank_nom, vblank_nom_default_in_line);
	unsigned int vblank_avail = (vblank_nom_input == 0) ? vblank_nom_default_in_line : vblank_nom_input;

	vblank_size = (unsigned int)math_min2(vblank_actual, vblank_avail);

	if (timing->interlaced && !ptoi_supported)
		max_vstartup_lines = (unsigned int)(math_floor2(vblank_size / 2.0, 1.0));
	else
		max_vstartup_lines = vblank_size - (unsigned int)math_max2(1.0, math_ceil2(write_back_delay_us / line_time_us, 1.0));
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: VBlankNom = %u\n", __func__, timing->vblank_nom);
	dml2_printf("DML::%s: vblank_nom_default_us = %u\n", __func__, vblank_nom_default_us);
	dml2_printf("DML::%s: line_time_us = %f\n", __func__, line_time_us);
	dml2_printf("DML::%s: vblank_actual = %u\n", __func__, vblank_actual);
	dml2_printf("DML::%s: vblank_avail = %u\n", __func__, vblank_avail);
	dml2_printf("DML::%s: max_vstartup_lines = %u\n", __func__, max_vstartup_lines);
#endif
	return max_vstartup_lines;
}

static void CalculateSwathAndDETConfiguration(struct dml2_core_internal_scratch *scratch,
	struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params *p)
{
	unsigned int MaximumSwathHeightY[DML2_MAX_PLANES] = { 0 };
	unsigned int MaximumSwathHeightC[DML2_MAX_PLANES] = { 0 };
	unsigned int RoundedUpSwathSizeBytesY[DML2_MAX_PLANES] = { 0 };
	unsigned int RoundedUpSwathSizeBytesC[DML2_MAX_PLANES] = { 0 };
	unsigned int SwathWidthSingleDPP[DML2_MAX_PLANES] = { 0 };
	unsigned int SwathWidthSingleDPPChroma[DML2_MAX_PLANES] = { 0 };

	unsigned int TotalActiveDPP = 0;
	bool NoChromaOrLinear = true;
	unsigned int SurfaceDoingUnboundedRequest = 0;
	unsigned int DETBufferSizeInKByteForSwathCalculation;

	const long TTUFIFODEPTH = 8;
	const long MAXIMUMCOMPRESSION = 4;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: ForceSingleDPP = %u\n", __func__, p->ForceSingleDPP);
	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		dml2_printf("DML::%s: DPPPerSurface[%u] = %u\n", __func__, k, p->DPPPerSurface[k]);
	}
#endif
	CalculateSwathWidth(
		p->display_cfg,
		p->ForceSingleDPP,
		p->NumberOfActiveSurfaces,
		p->ODMMode,
		p->BytePerPixY,
		p->BytePerPixC,
		p->Read256BytesBlockHeightY,
		p->Read256BytesBlockHeightC,
		p->Read256BytesBlockWidthY,
		p->Read256BytesBlockWidthC,
		p->surf_linear128_l,
		p->surf_linear128_c,
		p->DPPPerSurface,

		// Output
		p->req_per_swath_ub_l,
		p->req_per_swath_ub_c,
		SwathWidthSingleDPP,
		SwathWidthSingleDPPChroma,
		p->SwathWidth,
		p->SwathWidthChroma,
		MaximumSwathHeightY,
		MaximumSwathHeightC,
		p->swath_width_luma_ub,
		p->swath_width_chroma_ub);

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		p->full_swath_bytes_l[k] = (unsigned int)(p->swath_width_luma_ub[k] * p->BytePerPixDETY[k] * MaximumSwathHeightY[k]);
		p->full_swath_bytes_c[k] = (unsigned int)(p->swath_width_chroma_ub[k] * p->BytePerPixDETC[k] * MaximumSwathHeightC[k]);
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u DPPPerSurface = %u\n", __func__, k, p->DPPPerSurface[k]);
		dml2_printf("DML::%s: k=%u swath_width_luma_ub = %u\n", __func__, k, p->swath_width_luma_ub[k]);
		dml2_printf("DML::%s: k=%u BytePerPixDETY = %f\n", __func__, k, p->BytePerPixDETY[k]);
		dml2_printf("DML::%s: k=%u MaximumSwathHeightY = %u\n", __func__, k, MaximumSwathHeightY[k]);
		dml2_printf("DML::%s: k=%u full_swath_bytes_l = %u\n", __func__, k, p->full_swath_bytes_l[k]);
		dml2_printf("DML::%s: k=%u swath_width_chroma_ub = %u\n", __func__, k, p->swath_width_chroma_ub[k]);
		dml2_printf("DML::%s: k=%u BytePerPixDETC = %f\n", __func__, k, p->BytePerPixDETC[k]);
		dml2_printf("DML::%s: k=%u MaximumSwathHeightC = %u\n", __func__, k, MaximumSwathHeightC[k]);
		dml2_printf("DML::%s: k=%u full_swath_bytes_c = %u\n", __func__, k, p->full_swath_bytes_c[k]);
#endif
		if (p->display_cfg->plane_descriptors[k].pixel_format == dml2_420_10) {
			p->full_swath_bytes_l[k] = (unsigned int)(math_ceil2((double)p->full_swath_bytes_l[k], 256));
			p->full_swath_bytes_c[k] = (unsigned int)(math_ceil2((double)p->full_swath_bytes_c[k], 256));
		}
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		TotalActiveDPP = TotalActiveDPP + (p->ForceSingleDPP ? 1 : p->DPPPerSurface[k]);
		if (p->DPPPerSurface[k] > 0)
			SurfaceDoingUnboundedRequest = k;
		if (dml_is_420(p->display_cfg->plane_descriptors[k].pixel_format) || p->display_cfg->plane_descriptors[k].pixel_format == dml2_rgbe_alpha
			|| p->display_cfg->plane_descriptors[k].surface.tiling == dml2_sw_linear) {
			NoChromaOrLinear = false;
		}
	}

	*p->UnboundedRequestEnabled = UnboundedRequest(p->display_cfg->overrides.hw.force_unbounded_requesting.enable, p->display_cfg->overrides.hw.force_unbounded_requesting.value, TotalActiveDPP, NoChromaOrLinear);

	CalculateDETBufferSize(
		&scratch->CalculateDETBufferSize_locals,
		p->display_cfg,
		p->ForceSingleDPP,
		p->NumberOfActiveSurfaces,
		*p->UnboundedRequestEnabled,
		p->nomDETInKByte,
		p->MaxTotalDETInKByte,
		p->ConfigReturnBufferSizeInKByte,
		p->MinCompressedBufferSizeInKByte,
		p->ConfigReturnBufferSegmentSizeInkByte,
		p->CompressedBufferSegmentSizeInkByte,
		p->ReadBandwidthLuma,
		p->ReadBandwidthChroma,
		p->full_swath_bytes_l,
		p->full_swath_bytes_c,
		p->DPPPerSurface,

		// Output
		p->DETBufferSizeInKByte, // per hubp pipe
		p->CompressedBufferSizeInkByte);

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: TotalActiveDPP = %u\n", __func__, TotalActiveDPP);
	dml2_printf("DML::%s: nomDETInKByte = %u\n", __func__, p->nomDETInKByte);
	dml2_printf("DML::%s: ConfigReturnBufferSizeInKByte = %u\n", __func__, p->ConfigReturnBufferSizeInKByte);
	dml2_printf("DML::%s: UnboundedRequestEnabled = %u\n", __func__, *p->UnboundedRequestEnabled);
	dml2_printf("DML::%s: CompressedBufferSizeInkByte = %u\n", __func__, *p->CompressedBufferSizeInkByte);
#endif

	*p->ViewportSizeSupport = true;
	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {

		DETBufferSizeInKByteForSwathCalculation = (dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[k]) ? 1024 : p->DETBufferSizeInKByte[k]);
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u DETBufferSizeInKByteForSwathCalculation = %u\n", __func__, k, DETBufferSizeInKByteForSwathCalculation);
#endif
		if (p->display_cfg->plane_descriptors[k].surface.tiling == dml2_sw_linear) {
			p->SwathHeightY[k] = MaximumSwathHeightY[k];
			p->SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY[k] = p->full_swath_bytes_l[k];
			RoundedUpSwathSizeBytesC[k] = p->full_swath_bytes_c[k];

			if (p->surf_linear128_l[k])
				p->request_size_bytes_luma[k] = 128;
			else
				p->request_size_bytes_luma[k] = 256;

			if (p->surf_linear128_c[k])
				p->request_size_bytes_chroma[k] = 128;
			else
				p->request_size_bytes_chroma[k] = 256;

		} else if (p->full_swath_bytes_l[k] + p->full_swath_bytes_c[k] <= DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			p->SwathHeightY[k] = MaximumSwathHeightY[k];
			p->SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY[k] = p->full_swath_bytes_l[k];
			RoundedUpSwathSizeBytesC[k] = p->full_swath_bytes_c[k];
			p->request_size_bytes_luma[k] = 256;
			p->request_size_bytes_chroma[k] = 256;

		} else if (p->full_swath_bytes_l[k] >= 1.5 * p->full_swath_bytes_c[k] && p->full_swath_bytes_l[k] / 2 + p->full_swath_bytes_c[k] <= DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			p->SwathHeightY[k] = MaximumSwathHeightY[k] / 2;
			p->SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY[k] = p->full_swath_bytes_l[k] / 2;
			RoundedUpSwathSizeBytesC[k] = p->full_swath_bytes_c[k];
			p->request_size_bytes_luma[k] = ((p->BytePerPixY[k] == 2) == dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) ? 128 : 64;
			p->request_size_bytes_chroma[k] = 256;

		} else if (p->full_swath_bytes_l[k] < 1.5 * p->full_swath_bytes_c[k] && p->full_swath_bytes_l[k] + p->full_swath_bytes_c[k] / 2 <= DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			p->SwathHeightY[k] = MaximumSwathHeightY[k];
			p->SwathHeightC[k] = MaximumSwathHeightC[k] / 2;
			RoundedUpSwathSizeBytesY[k] = p->full_swath_bytes_l[k];
			RoundedUpSwathSizeBytesC[k] = p->full_swath_bytes_c[k] / 2;
			p->request_size_bytes_luma[k] = 256;
			p->request_size_bytes_chroma[k] = ((p->BytePerPixC[k] == 2) == dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) ? 128 : 64;

		} else {
			p->SwathHeightY[k] = MaximumSwathHeightY[k] / 2;
			p->SwathHeightC[k] = MaximumSwathHeightC[k] / 2;
			RoundedUpSwathSizeBytesY[k] = p->full_swath_bytes_l[k] / 2;
			RoundedUpSwathSizeBytesC[k] = p->full_swath_bytes_c[k] / 2;
			p->request_size_bytes_luma[k] = ((p->BytePerPixY[k] == 2) == dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) ? 128 : 64;;
			p->request_size_bytes_chroma[k] = ((p->BytePerPixC[k] == 2) == dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) ? 128 : 64;;
		}

		if (p->SwathHeightC[k] == 0)
			p->request_size_bytes_chroma[k] = 0;

		if ((p->full_swath_bytes_l[k] / 2 + p->full_swath_bytes_c[k] / 2 > DETBufferSizeInKByteForSwathCalculation * 1024 / 2) ||
			p->SwathWidth[k] > p->MaximumSwathWidthLuma[k] || (p->SwathHeightC[k] > 0 && p->SwathWidthChroma[k] > p->MaximumSwathWidthChroma[k])) {
			*p->ViewportSizeSupport = false;
			dml2_printf("DML::%s: k=%u full_swath_bytes_l=%u\n", __func__, k, p->full_swath_bytes_l[k]);
			dml2_printf("DML::%s: k=%u full_swath_bytes_c=%u\n", __func__, k, p->full_swath_bytes_c[k]);
			dml2_printf("DML::%s: k=%u DETBufferSizeInKByteForSwathCalculation=%u\n", __func__, k, DETBufferSizeInKByteForSwathCalculation);
			dml2_printf("DML::%s: k=%u SwathWidth=%u\n", __func__, k, p->SwathWidth[k]);
			dml2_printf("DML::%s: k=%u MaximumSwathWidthLuma=%f\n", __func__, k, p->MaximumSwathWidthLuma[k]);
			dml2_printf("DML::%s: k=%u SwathWidthChroma=%d\n", __func__, k, p->SwathWidthChroma[k]);
			dml2_printf("DML::%s: k=%u MaximumSwathWidthChroma=%f\n", __func__, k, p->MaximumSwathWidthChroma[k]);
			p->ViewportSizeSupportPerSurface[k] = false;
		} else {
			p->ViewportSizeSupportPerSurface[k] = true;
		}

		if (p->SwathHeightC[k] == 0) {
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u, All DET will be used for plane0\n", __func__, k);
#endif
			p->DETBufferSizeY[k] = p->DETBufferSizeInKByte[k] * 1024;
			p->DETBufferSizeC[k] = 0;
		} else if (RoundedUpSwathSizeBytesY[k] <= 1.5 * RoundedUpSwathSizeBytesC[k]) {
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u, Half DET will be used for plane0, and half for plane1\n", __func__, k);
#endif
			p->DETBufferSizeY[k] = p->DETBufferSizeInKByte[k] * 1024 / 2;
			p->DETBufferSizeC[k] = p->DETBufferSizeInKByte[k] * 1024 / 2;
		} else {
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u, 2/3 DET will be used for plane0, and 1/3 for plane1\n", __func__, k);
#endif
			p->DETBufferSizeY[k] = (unsigned int)(math_floor2(p->DETBufferSizeInKByte[k] * 1024 * 2 / 3, 1024));
			p->DETBufferSizeC[k] = p->DETBufferSizeInKByte[k] * 1024 - p->DETBufferSizeY[k];
		}

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u SwathHeightY = %u\n", __func__, k, p->SwathHeightY[k]);
		dml2_printf("DML::%s: k=%u SwathHeightC = %u\n", __func__, k, p->SwathHeightC[k]);
		dml2_printf("DML::%s: k=%u full_swath_bytes_l = %u\n", __func__, k, p->full_swath_bytes_l[k]);
		dml2_printf("DML::%s: k=%u full_swath_bytes_c = %u\n", __func__, k, p->full_swath_bytes_c[k]);
		dml2_printf("DML::%s: k=%u RoundedUpSwathSizeBytesY = %u\n", __func__, k, RoundedUpSwathSizeBytesY[k]);
		dml2_printf("DML::%s: k=%u RoundedUpSwathSizeBytesC = %u\n", __func__, k, RoundedUpSwathSizeBytesC[k]);
		dml2_printf("DML::%s: k=%u DETBufferSizeInKByte = %u\n", __func__, k, p->DETBufferSizeInKByte[k]);
		dml2_printf("DML::%s: k=%u DETBufferSizeY = %u\n", __func__, k, p->DETBufferSizeY[k]);
		dml2_printf("DML::%s: k=%u DETBufferSizeC = %u\n", __func__, k, p->DETBufferSizeC[k]);
		dml2_printf("DML::%s: k=%u ViewportSizeSupportPerSurface = %u\n", __func__, k, p->ViewportSizeSupportPerSurface[k]);
#endif

	}

	*p->compbuf_reserved_space_64b = 2 * p->pixel_chunk_size_kbytes * 1024 / 64;
	if (*p->UnboundedRequestEnabled) {
		*p->compbuf_reserved_space_64b = (unsigned int)math_ceil2(math_max2(*p->compbuf_reserved_space_64b,
			(double)(p->rob_buffer_size_kbytes * 1024 / 64) - (double)(RoundedUpSwathSizeBytesY[SurfaceDoingUnboundedRequest] * TTUFIFODEPTH / (p->mrq_present ? MAXIMUMCOMPRESSION : 1) / 64)), 1.0);
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: RoundedUpSwathSizeBytesY[%d] = %u\n", __func__, SurfaceDoingUnboundedRequest, RoundedUpSwathSizeBytesY[SurfaceDoingUnboundedRequest]);
		dml2_printf("DML::%s: rob_buffer_size_kbytes = %u\n", __func__, p->rob_buffer_size_kbytes);
#endif
	}
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: compbuf_reserved_space_64b = %u\n", __func__, *p->compbuf_reserved_space_64b);
#endif

	*p->hw_debug5 = false;
#ifdef ALLOW_SDPIF_RATE_LIMIT_PRE_CSTATE
	if (p->NumberOfActiveSurfaces > 1)
		*p->hw_debug5 = true;
#else
	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (!(p->mrq_present) && (!(*p->UnboundedRequestEnabled)) && (TotalActiveDPP == 1)
			&& p->display_cfg->plane_descriptors[k].surface.dcc.enable
			&& ((p->rob_buffer_size_kbytes * 1024 * (p->mrq_present ? MAXIMUMCOMPRESSION : 1)
				+ *p->CompressedBufferSizeInkByte * MAXIMUMCOMPRESSION * 1024) > TTUFIFODEPTH * (RoundedUpSwathSizeBytesY[k] + RoundedUpSwathSizeBytesC[k])))
			*p->hw_debug5 = true;
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u UnboundedRequestEnabled = %u\n", __func__, k, *p->UnboundedRequestEnabled);
		dml2_printf("DML::%s: k=%u MAXIMUMCOMPRESSION = %lu\n", __func__, k, MAXIMUMCOMPRESSION);
		dml2_printf("DML::%s: k=%u TTUFIFODEPTH = %lu\n", __func__, k, TTUFIFODEPTH);
		dml2_printf("DML::%s: k=%u CompressedBufferSizeInkByte = %u\n", __func__, k, *p->CompressedBufferSizeInkByte);
		dml2_printf("DML::%s: k=%u RoundedUpSwathSizeBytesC = %u\n", __func__, k, RoundedUpSwathSizeBytesC[k]);
		dml2_printf("DML::%s: k=%u hw_debug5 = %u\n", __func__, k, *p->hw_debug5);
#endif
	}
#endif
}

static enum dml2_odm_mode DecideODMMode(unsigned int HActive,
	double MaxDispclk,
	unsigned int MaximumPixelsPerLinePerDSCUnit,
	enum dml2_output_format_class OutFormat,
	bool UseDSC,
	unsigned int NumberOfDSCSlices,
	double SurfaceRequiredDISPCLKWithoutODMCombine,
	double SurfaceRequiredDISPCLKWithODMCombineTwoToOne,
	double SurfaceRequiredDISPCLKWithODMCombineThreeToOne,
	double SurfaceRequiredDISPCLKWithODMCombineFourToOne)
{
	enum dml2_odm_mode MinimumRequiredODMModeForMaxDispClock;
	enum dml2_odm_mode MinimumRequiredODMModeForMaxDSCHActive;
	enum dml2_odm_mode MinimumRequiredODMModeForMax420HActive;
	enum dml2_odm_mode ODMMode = dml2_odm_mode_bypass;

	MinimumRequiredODMModeForMaxDispClock =
			(SurfaceRequiredDISPCLKWithoutODMCombine <= MaxDispclk) ? dml2_odm_mode_bypass :
			(SurfaceRequiredDISPCLKWithODMCombineTwoToOne <= MaxDispclk) ? dml2_odm_mode_combine_2to1 :
			(SurfaceRequiredDISPCLKWithODMCombineThreeToOne <= MaxDispclk) ? dml2_odm_mode_combine_3to1 : dml2_odm_mode_combine_4to1;
	if (ODMMode < MinimumRequiredODMModeForMaxDispClock)
		ODMMode = MinimumRequiredODMModeForMaxDispClock;

	if (UseDSC) {
		MinimumRequiredODMModeForMaxDSCHActive =
				(HActive <= 1 * MaximumPixelsPerLinePerDSCUnit) ? dml2_odm_mode_bypass :
				(HActive <= 2 * MaximumPixelsPerLinePerDSCUnit) ? dml2_odm_mode_combine_2to1 :
				(HActive <= 3 * MaximumPixelsPerLinePerDSCUnit) ? dml2_odm_mode_combine_3to1 : dml2_odm_mode_combine_4to1;
		if (ODMMode < MinimumRequiredODMModeForMaxDSCHActive)
			ODMMode = MinimumRequiredODMModeForMaxDSCHActive;
	}

	if (OutFormat == dml2_420) {
		MinimumRequiredODMModeForMax420HActive =
				(HActive <= 1 * DML2_MAX_FMT_420_BUFFER_WIDTH) ? dml2_odm_mode_bypass :
				(HActive <= 2 * DML2_MAX_FMT_420_BUFFER_WIDTH) ? dml2_odm_mode_combine_2to1 :
				(HActive <= 3 * DML2_MAX_FMT_420_BUFFER_WIDTH) ? dml2_odm_mode_combine_3to1 : dml2_odm_mode_combine_4to1;
		if (ODMMode < MinimumRequiredODMModeForMax420HActive)
			ODMMode = MinimumRequiredODMModeForMax420HActive;
	}

	if (UseDSC) {
		if (ODMMode == dml2_odm_mode_bypass && NumberOfDSCSlices > 4)
			ODMMode = dml2_odm_mode_combine_2to1;
		if (ODMMode == dml2_odm_mode_combine_2to1 && NumberOfDSCSlices > 8)
			ODMMode = dml2_odm_mode_combine_3to1;
		if (ODMMode == dml2_odm_mode_combine_3to1 && NumberOfDSCSlices != 12)
			ODMMode = dml2_odm_mode_combine_4to1;
	}

	return ODMMode;
}

static void CalculateODMConstraints(
	enum dml2_odm_mode ODMUse,
	double SurfaceRequiredDISPCLKWithoutODMCombine,
	double SurfaceRequiredDISPCLKWithODMCombineTwoToOne,
	double SurfaceRequiredDISPCLKWithODMCombineThreeToOne,
	double SurfaceRequiredDISPCLKWithODMCombineFourToOne,
	unsigned int MaximumPixelsPerLinePerDSCUnit,
	/* Output */
	double *DISPCLKRequired,
	unsigned int *NumberOfDPPRequired,
	unsigned int *MaxHActiveForDSC,
	unsigned int *MaxDSCSlices,
	unsigned int *MaxHActiveFor420)
{
	switch (ODMUse) {
	case dml2_odm_mode_combine_2to1:
		*DISPCLKRequired = SurfaceRequiredDISPCLKWithODMCombineTwoToOne;
		*NumberOfDPPRequired = 2;
		break;
	case dml2_odm_mode_combine_3to1:
		*DISPCLKRequired = SurfaceRequiredDISPCLKWithODMCombineThreeToOne;
		*NumberOfDPPRequired = 3;
		break;
	case dml2_odm_mode_combine_4to1:
		*DISPCLKRequired = SurfaceRequiredDISPCLKWithODMCombineFourToOne;
		*NumberOfDPPRequired = 4;
		break;
	case dml2_odm_mode_auto:
	case dml2_odm_mode_split_1to2:
	case dml2_odm_mode_mso_1to2:
	case dml2_odm_mode_mso_1to4:
	case dml2_odm_mode_bypass:
	default:
		*DISPCLKRequired = SurfaceRequiredDISPCLKWithoutODMCombine;
		*NumberOfDPPRequired = 1;
		break;
	}
	*MaxHActiveForDSC = *NumberOfDPPRequired * MaximumPixelsPerLinePerDSCUnit;
	*MaxDSCSlices = *NumberOfDPPRequired * DML_MAX_NUM_OF_SLICES_PER_DSC;
	*MaxHActiveFor420 = *NumberOfDPPRequired * DML2_MAX_FMT_420_BUFFER_WIDTH;
}

static bool ValidateODMMode(enum dml2_odm_mode ODMMode,
	double MaxDispclk,
	unsigned int HActive,
	enum dml2_output_format_class OutFormat,
	bool UseDSC,
	unsigned int NumberOfDSCSlices,
	unsigned int TotalNumberOfActiveDPP,
	unsigned int MaxNumDPP,
	double DISPCLKRequired,
	unsigned int NumberOfDPPRequired,
	unsigned int MaxHActiveForDSC,
	unsigned int MaxDSCSlices,
	unsigned int MaxHActiveFor420)
{
	bool are_odm_segments_symmetrical = (ODMMode == dml2_odm_mode_combine_3to1) ? UseDSC : true;
	bool is_max_dsc_slice_required = (ODMMode == dml2_odm_mode_combine_3to1);
	unsigned int pixels_per_clock_cycle = (OutFormat == dml2_420 || OutFormat == dml2_n422) ? 2 : 1;
	unsigned int h_timing_div_mode =
			(ODMMode == dml2_odm_mode_combine_4to1 || ODMMode == dml2_odm_mode_combine_3to1) ? 4 :
			(ODMMode == dml2_odm_mode_combine_2to1) ? 2 : pixels_per_clock_cycle;

	if (DISPCLKRequired > MaxDispclk)
		return false;
	if ((TotalNumberOfActiveDPP + NumberOfDPPRequired) > MaxNumDPP)
		return false;
	if (are_odm_segments_symmetrical) {
		if (HActive % (NumberOfDPPRequired * pixels_per_clock_cycle))
			return false;
	}
	if (HActive % h_timing_div_mode)
		/*
		 * TODO - OTG_H_TOTAL, OTG_H_BLANK_START/END and
		 * OTG_H_SYNC_A_START/END all need to be visible by h timing div
		 * mode. This logic only checks H active.
		 */
		return false;

	if (UseDSC) {
		if (HActive > MaxHActiveForDSC)
			return false;
		if (NumberOfDSCSlices > MaxDSCSlices)
			return false;
		if (HActive % NumberOfDSCSlices)
			return false;
		if (NumberOfDSCSlices % NumberOfDPPRequired)
			return false;
		if (is_max_dsc_slice_required) {
			if (NumberOfDSCSlices != MaxDSCSlices)
				return false;
		}
	}

	if (OutFormat == dml2_420) {
		if (HActive > MaxHActiveFor420)
			return false;
	}

	return true;
}

static void CalculateODMMode(
	unsigned int MaximumPixelsPerLinePerDSCUnit,
	unsigned int HActive,
	enum dml2_output_format_class OutFormat,
	enum dml2_output_encoder_class Output,
	enum dml2_odm_mode ODMUse,
	double MaxDispclk,
	bool DSCEnable,
	unsigned int TotalNumberOfActiveDPP,
	unsigned int MaxNumDPP,
	double PixelClock,
	unsigned int NumberOfDSCSlices,

	// Output
	bool *TotalAvailablePipesSupport,
	unsigned int *NumberOfDPP,
	enum dml2_odm_mode *ODMMode,
	double *RequiredDISPCLKPerSurface)
{
	double SurfaceRequiredDISPCLKWithoutODMCombine;
	double SurfaceRequiredDISPCLKWithODMCombineTwoToOne;
	double SurfaceRequiredDISPCLKWithODMCombineThreeToOne;
	double SurfaceRequiredDISPCLKWithODMCombineFourToOne;
	double DISPCLKRequired;
	unsigned int NumberOfDPPRequired;
	unsigned int MaxHActiveForDSC;
	unsigned int MaxDSCSlices;
	unsigned int MaxHActiveFor420;
	bool success;
	bool UseDSC = DSCEnable && (NumberOfDSCSlices > 0);
	enum dml2_odm_mode DecidedODMMode;

	SurfaceRequiredDISPCLKWithoutODMCombine = CalculateRequiredDispclk(dml2_odm_mode_bypass, PixelClock);
	SurfaceRequiredDISPCLKWithODMCombineTwoToOne = CalculateRequiredDispclk(dml2_odm_mode_combine_2to1, PixelClock);
	SurfaceRequiredDISPCLKWithODMCombineThreeToOne = CalculateRequiredDispclk(dml2_odm_mode_combine_3to1, PixelClock);
	SurfaceRequiredDISPCLKWithODMCombineFourToOne = CalculateRequiredDispclk(dml2_odm_mode_combine_4to1, PixelClock);
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: ODMUse = %d\n", __func__, ODMUse);
	dml2_printf("DML::%s: Output = %d\n", __func__, Output);
	dml2_printf("DML::%s: DSCEnable = %d\n", __func__, DSCEnable);
	dml2_printf("DML::%s: MaxDispclk = %f\n", __func__, MaxDispclk);
	dml2_printf("DML::%s: MaximumPixelsPerLinePerDSCUnit = %d\n", __func__, MaximumPixelsPerLinePerDSCUnit);
	dml2_printf("DML::%s: SurfaceRequiredDISPCLKWithoutODMCombine = %f\n", __func__, SurfaceRequiredDISPCLKWithoutODMCombine);
	dml2_printf("DML::%s: SurfaceRequiredDISPCLKWithODMCombineTwoToOne = %f\n", __func__, SurfaceRequiredDISPCLKWithODMCombineTwoToOne);
	dml2_printf("DML::%s: SurfaceRequiredDISPCLKWithODMCombineThreeToOne = %f\n", __func__, SurfaceRequiredDISPCLKWithODMCombineThreeToOne);
	dml2_printf("DML::%s: SurfaceRequiredDISPCLKWithODMCombineFourToOne = %f\n", __func__, SurfaceRequiredDISPCLKWithODMCombineFourToOne);
#endif
	if (ODMUse == dml2_odm_mode_auto)
		DecidedODMMode = DecideODMMode(HActive,
				MaxDispclk,
				MaximumPixelsPerLinePerDSCUnit,
				OutFormat,
				UseDSC,
				NumberOfDSCSlices,
				SurfaceRequiredDISPCLKWithoutODMCombine,
				SurfaceRequiredDISPCLKWithODMCombineTwoToOne,
				SurfaceRequiredDISPCLKWithODMCombineThreeToOne,
				SurfaceRequiredDISPCLKWithODMCombineFourToOne);
	else
		DecidedODMMode = ODMUse;
	CalculateODMConstraints(DecidedODMMode,
			SurfaceRequiredDISPCLKWithoutODMCombine,
			SurfaceRequiredDISPCLKWithODMCombineTwoToOne,
			SurfaceRequiredDISPCLKWithODMCombineThreeToOne,
			SurfaceRequiredDISPCLKWithODMCombineFourToOne,
			MaximumPixelsPerLinePerDSCUnit,
			&DISPCLKRequired,
			&NumberOfDPPRequired,
			&MaxHActiveForDSC,
			&MaxDSCSlices,
			&MaxHActiveFor420);
	success = ValidateODMMode(DecidedODMMode,
			MaxDispclk,
			HActive,
			OutFormat,
			UseDSC,
			NumberOfDSCSlices,
			TotalNumberOfActiveDPP,
			MaxNumDPP,
			DISPCLKRequired,
			NumberOfDPPRequired,
			MaxHActiveForDSC,
			MaxDSCSlices,
			MaxHActiveFor420);

	*ODMMode = DecidedODMMode;
	*TotalAvailablePipesSupport = success;
	*NumberOfDPP = NumberOfDPPRequired;
	*RequiredDISPCLKPerSurface = success ? DISPCLKRequired : 0;
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: ODMMode = %d\n", __func__, *ODMMode);
	dml2_printf("DML::%s: NumberOfDPP = %d\n", __func__, *NumberOfDPP);
	dml2_printf("DML::%s: TotalAvailablePipesSupport = %d\n", __func__, *TotalAvailablePipesSupport);
	dml2_printf("DML::%s: RequiredDISPCLKPerSurface = %f\n", __func__, *RequiredDISPCLKPerSurface);
#endif
}

static void CalculateOutputLink(
	struct dml2_core_internal_scratch *s,
	double PHYCLK,
	double PHYCLKD18,
	double PHYCLKD32,
	double Downspreading,
	enum dml2_output_encoder_class Output,
	enum dml2_output_format_class OutputFormat,
	unsigned int HTotal,
	unsigned int HActive,
	double PixelClockBackEnd,
	double ForcedOutputLinkBPP,
	unsigned int DSCInputBitPerComponent,
	unsigned int NumberOfDSCSlices,
	double AudioSampleRate,
	unsigned int AudioSampleLayout,
	enum dml2_odm_mode ODMModeNoDSC,
	enum dml2_odm_mode ODMModeDSC,
	enum dml2_dsc_enable_option DSCEnable,
	unsigned int OutputLinkDPLanes,
	enum dml2_output_link_dp_rate OutputLinkDPRate,

	// Output
	bool *RequiresDSC,
	bool *RequiresFEC,
	double *OutBpp,
	enum dml2_core_internal_output_type *OutputType,
	enum dml2_core_internal_output_type_rate *OutputRate,
	unsigned int *RequiredSlots)
{
	bool LinkDSCEnable;
	unsigned int dummy;
	*RequiresDSC = false;
	*RequiresFEC = false;
	*OutBpp = 0;

	*OutputType = dml2_core_internal_output_type_unknown;
	*OutputRate = dml2_core_internal_output_rate_unknown;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: DSCEnable = %u (dis, en, en_if_necessary)\n", __func__, DSCEnable);
	dml2_printf("DML::%s: PHYCLK = %f\n", __func__, PHYCLK);
	dml2_printf("DML::%s: PixelClockBackEnd = %f\n", __func__, PixelClockBackEnd);
	dml2_printf("DML::%s: AudioSampleRate = %f\n", __func__, AudioSampleRate);
	dml2_printf("DML::%s: HActive = %u\n", __func__, HActive);
	dml2_printf("DML::%s: HTotal = %u\n", __func__, HTotal);
	dml2_printf("DML::%s: ODMModeNoDSC = %u\n", __func__, ODMModeNoDSC);
	dml2_printf("DML::%s: ODMModeDSC = %u\n", __func__, ODMModeDSC);
	dml2_printf("DML::%s: ForcedOutputLinkBPP = %f\n", __func__, ForcedOutputLinkBPP);
	dml2_printf("DML::%s: Output (encoder) = %u\n", __func__, Output);
	dml2_printf("DML::%s: OutputLinkDPRate = %u\n", __func__, OutputLinkDPRate);
#endif
	{
		if (Output == dml2_hdmi) {
			*RequiresDSC = false;
			*RequiresFEC = false;
			*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, math_min2(600, PHYCLK) * 10, 3, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, false, Output,
				OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
			//OutputTypeAndRate = "HDMI";
			*OutputType = dml2_core_internal_output_type_hdmi;
		} else if (Output == dml2_dp || Output == dml2_dp2p0 || Output == dml2_edp) {
			if (DSCEnable == dml2_dsc_enable) {
				*RequiresDSC = true;
				LinkDSCEnable = true;
				if (Output == dml2_dp || Output == dml2_dp2p0) {
					*RequiresFEC = true;
				} else {
					*RequiresFEC = false;
				}
			} else {
				*RequiresDSC = false;
				LinkDSCEnable = false;
				if (Output == dml2_dp2p0) {
					*RequiresFEC = true;
				} else {
					*RequiresFEC = false;
				}
			}
			if (Output == dml2_dp2p0) {
				*OutBpp = 0;
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_uhbr10) && PHYCLKD32 >= 10000.0 / 32) {
					*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 10000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
						OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && PHYCLKD32 < 13500.0 / 32 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 10000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR10";
					*OutputType = dml2_core_internal_output_type_dp2p0;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_uhbr10;
				}
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_uhbr13p5) && *OutBpp == 0 && PHYCLKD32 >= 13500.0 / 32) {
					*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 13500, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
						OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);

					if (*OutBpp == 0 && PHYCLKD32 < 20000.0 / 32 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 13500, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR13p5";
					*OutputType = dml2_core_internal_output_type_dp2p0;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_uhbr13p5;
				}
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_uhbr20) && *OutBpp == 0 && PHYCLKD32 >= 20000.0 / 32) {
					*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 20000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
						OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 20000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR20";
					*OutputType = dml2_core_internal_output_type_dp2p0;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_uhbr20;
				}
			} else { // output is dp or edp
				*OutBpp = 0;
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_hbr) && PHYCLK >= 270) {
					*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 2700, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
						OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && PHYCLK < 540 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dml2_dp) {
							*RequiresFEC = true;
						}
						*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 2700, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR";
					*OutputType = (Output == dml2_dp) ? dml2_core_internal_output_type_dp : dml2_core_internal_output_type_edp;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_hbr;
				}
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_hbr2) && *OutBpp == 0 && PHYCLK >= 540) {
					*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 5400, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
						OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);

					if (*OutBpp == 0 && PHYCLK < 810 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dml2_dp) {
							*RequiresFEC = true;
						}
						*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 5400, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR2";
					*OutputType = (Output == dml2_dp) ? dml2_core_internal_output_type_dp : dml2_core_internal_output_type_edp;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_hbr2;
				}
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_hbr3) && *OutBpp == 0 && PHYCLK >= 810) { // VBA_ERROR, vba code doesn't have hbr3 check
					*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 8100, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
						OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);

					if (*OutBpp == 0 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dml2_dp) {
							*RequiresFEC = true;
						}
						*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 8100, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR3";
					*OutputType = (Output == dml2_dp) ? dml2_core_internal_output_type_dp : dml2_core_internal_output_type_edp;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_hbr3;
				}
			}
		} else if (Output == dml2_hdmifrl) {
			if (DSCEnable == dml2_dsc_enable) {
				*RequiresDSC = true;
				LinkDSCEnable = true;
				*RequiresFEC = true;
			} else {
				*RequiresDSC = false;
				LinkDSCEnable = false;
				*RequiresFEC = false;
			}
			*OutBpp = 0;
			if (PHYCLKD18 >= 3000.0 / 18) {
				*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, 3000, 3, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				//OutputTypeAndRate = Output & "3x3";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_3x3;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 6000.0 / 18) {
				*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, 6000, 3, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				//OutputTypeAndRate = Output & "6x3";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_6x3;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 6000.0 / 18) {
				*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, 6000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				//OutputTypeAndRate = Output & "6x4";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_6x4;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 8000.0 / 18) {
				*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, 8000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				//OutputTypeAndRate = Output & "8x4";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_8x4;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 10000.0 / 18) {
				*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, 10000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				if (*OutBpp == 0 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0 && PHYCLKD18 < 12000.0 / 18) {
					*RequiresDSC = true;
					LinkDSCEnable = true;
					*RequiresFEC = true;
					*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, 10000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				}
				//OutputTypeAndRate = Output & "10x4";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_10x4;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 12000.0 / 18) {
				*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, 12000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				if (*OutBpp == 0 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
					*RequiresDSC = true;
					LinkDSCEnable = true;
					*RequiresFEC = true;
					*OutBpp = TruncToValidBPP(&s->TruncToValidBPP_locals, 12000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				}
				//OutputTypeAndRate = Output & "12x4";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_12x4;
			}
		}
	}
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: RequiresDSC = %u\n", __func__, *RequiresDSC);
	dml2_printf("DML::%s: RequiresFEC = %u\n", __func__, *RequiresFEC);
	dml2_printf("DML::%s: OutBpp = %f\n", __func__, *OutBpp);
#endif
}

static double CalculateWriteBackDISPCLK(
	enum dml2_source_format_class WritebackPixelFormat,
	double PixelClock,
	double WritebackHRatio,
	double WritebackVRatio,
	unsigned int WritebackHTaps,
	unsigned int WritebackVTaps,
	unsigned int WritebackSourceWidth,
	unsigned int WritebackDestinationWidth,
	unsigned int HTotal,
	unsigned int WritebackLineBufferSize)
{
	double DISPCLK_H, DISPCLK_V, DISPCLK_HB;

	DISPCLK_H = PixelClock * math_ceil2((double)WritebackHTaps / 8.0, 1) / WritebackHRatio;
	DISPCLK_V = PixelClock * (WritebackVTaps * math_ceil2((double)WritebackDestinationWidth / 6.0, 1) + 8.0) / (double)HTotal;
	DISPCLK_HB = PixelClock * WritebackVTaps * (WritebackDestinationWidth * WritebackVTaps - WritebackLineBufferSize / 57.0) / 6.0 / (double)WritebackSourceWidth;
	return math_max3(DISPCLK_H, DISPCLK_V, DISPCLK_HB);
}

static double RequiredDTBCLK(
	bool DSCEnable,
	double PixelClock,
	enum dml2_output_format_class OutputFormat,
	double OutputBpp,
	unsigned int DSCSlices,
	unsigned int HTotal,
	unsigned int HActive,
	unsigned int AudioRate,
	unsigned int AudioLayout)
{
	if (DSCEnable != true) {
		return math_max2(PixelClock / 4.0 * OutputBpp / 24.0, 25.0);
	} else {
		double PixelWordRate = PixelClock / (OutputFormat == dml2_444 ? 1 : 2);
		double HCActive = math_ceil2(DSCSlices * math_ceil2(OutputBpp * math_ceil2(HActive / DSCSlices, 1) / 8.0, 1) / 3.0, 1);
		double HCBlank = 64 + 32 * math_ceil2(AudioRate * (AudioLayout == 1 ? 1 : 0.25) * HTotal / (PixelClock * 1000), 1);
		double AverageTribyteRate = PixelWordRate * (HCActive + HCBlank) / HTotal;
		double HActiveTribyteRate = PixelWordRate * HCActive / HActive;
		return math_max4(PixelWordRate / 4.0, AverageTribyteRate / 4.0, HActiveTribyteRate / 4.0, 25.0) * 1.002;
	}
}

static unsigned int DSCDelayRequirement(
	bool DSCEnabled,
	enum dml2_odm_mode ODMMode,
	unsigned int DSCInputBitPerComponent,
	double OutputBpp,
	unsigned int HActive,
	unsigned int HTotal,
	unsigned int NumberOfDSCSlices,
	enum dml2_output_format_class OutputFormat,
	enum dml2_output_encoder_class Output,
	double PixelClock,
	double PixelClockBackEnd)
{
	unsigned int DSCDelayRequirement_val = 0;
	unsigned int NumberOfDSCSlicesFactor = 1;

	if (DSCEnabled == true && OutputBpp != 0) {

		if (ODMMode == dml2_odm_mode_combine_4to1)
			NumberOfDSCSlicesFactor = 4;
		else if (ODMMode == dml2_odm_mode_combine_3to1)
			NumberOfDSCSlicesFactor = 3;
		else if (ODMMode == dml2_odm_mode_combine_2to1)
			NumberOfDSCSlicesFactor = 2;

		DSCDelayRequirement_val = NumberOfDSCSlicesFactor * (dscceComputeDelay(DSCInputBitPerComponent, OutputBpp, (unsigned int)(math_ceil2((double)HActive / (double)NumberOfDSCSlices, 1.0)),
			(NumberOfDSCSlices / NumberOfDSCSlicesFactor), OutputFormat, Output) + dscComputeDelay(OutputFormat, Output));

		DSCDelayRequirement_val = (unsigned int)(DSCDelayRequirement_val + (HTotal - HActive) * math_ceil2((double)DSCDelayRequirement_val / (double)HActive, 1.0));
		DSCDelayRequirement_val = (unsigned int)(DSCDelayRequirement_val * PixelClock / PixelClockBackEnd);

	} else {
		DSCDelayRequirement_val = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: DSCEnabled= %u\n", __func__, DSCEnabled);
	dml2_printf("DML::%s: ODMMode = %u\n", __func__, ODMMode);
	dml2_printf("DML::%s: OutputBpp = %f\n", __func__, OutputBpp);
	dml2_printf("DML::%s: HActive = %u\n", __func__, HActive);
	dml2_printf("DML::%s: HTotal = %u\n", __func__, HTotal);
	dml2_printf("DML::%s: PixelClock = %f\n", __func__, PixelClock);
	dml2_printf("DML::%s: PixelClockBackEnd = %f\n", __func__, PixelClockBackEnd);
	dml2_printf("DML::%s: OutputFormat = %u\n", __func__, OutputFormat);
	dml2_printf("DML::%s: DSCInputBitPerComponent = %u\n", __func__, DSCInputBitPerComponent);
	dml2_printf("DML::%s: NumberOfDSCSlices = %u\n", __func__, NumberOfDSCSlices);
	dml2_printf("DML::%s: DSCDelayRequirement_val = %u\n", __func__, DSCDelayRequirement_val);
#endif

	return DSCDelayRequirement_val;
}

static void CalculateSurfaceSizeInMall(
	const struct dml2_display_cfg *display_cfg,
	unsigned int NumberOfActiveSurfaces,
	unsigned int MALLAllocatedForDCN,
	unsigned int BytesPerPixelY[],
	unsigned int BytesPerPixelC[],
	unsigned int Read256BytesBlockWidthY[],
	unsigned int Read256BytesBlockWidthC[],
	unsigned int Read256BytesBlockHeightY[],
	unsigned int Read256BytesBlockHeightC[],
	unsigned int ReadBlockWidthY[],
	unsigned int ReadBlockWidthC[],
	unsigned int ReadBlockHeightY[],
	unsigned int ReadBlockHeightC[],

	// Output
	unsigned int SurfaceSizeInMALL[],
	bool *ExceededMALLSize)
{
	unsigned int TotalSurfaceSizeInMALLForSS = 0;
	unsigned int TotalSurfaceSizeInMALLForSubVP = 0;
	unsigned int MALLAllocatedForDCNInBytes = MALLAllocatedForDCN * 1024 * 1024;

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		const struct dml2_composition_cfg *composition = &display_cfg->plane_descriptors[k].composition;
		const struct dml2_surface_cfg *surface = &display_cfg->plane_descriptors[k].surface;

		if (composition->viewport.stationary) {
			SurfaceSizeInMALL[k] = (unsigned int)(math_min2(math_ceil2((double)surface->plane0.width, ReadBlockWidthY[k]),
				math_floor2(composition->viewport.plane0.x_start + composition->viewport.plane0.width + ReadBlockWidthY[k] - 1, ReadBlockWidthY[k]) -
				math_floor2((double)composition->viewport.plane0.x_start, ReadBlockWidthY[k])) *
				math_min2(math_ceil2((double)surface->plane0.height, ReadBlockHeightY[k]),
					math_floor2((double)composition->viewport.plane0.y_start + composition->viewport.plane0.height + ReadBlockHeightY[k] - 1, ReadBlockHeightY[k]) -
					math_floor2((double)composition->viewport.plane0.y_start, ReadBlockHeightY[k])) * BytesPerPixelY[k]);

			if (ReadBlockWidthC[k] > 0) {
				SurfaceSizeInMALL[k] = (unsigned int)(SurfaceSizeInMALL[k] +
					math_min2(math_ceil2((double)surface->plane1.width, ReadBlockWidthC[k]),
						math_floor2((double)composition->viewport.plane1.y_start + composition->viewport.plane1.width + ReadBlockWidthC[k] - 1, ReadBlockWidthC[k]) -
						math_floor2((double)composition->viewport.plane1.y_start, ReadBlockWidthC[k])) *
					math_min2(math_ceil2((double)surface->plane1.height, ReadBlockHeightC[k]),
						math_floor2((double)composition->viewport.plane1.y_start + composition->viewport.plane1.height + ReadBlockHeightC[k] - 1, ReadBlockHeightC[k]) -
						math_floor2(composition->viewport.plane1.y_start, ReadBlockHeightC[k])) * BytesPerPixelC[k]);
			}
		} else {
			SurfaceSizeInMALL[k] = (unsigned int)(math_ceil2(math_min2(surface->plane0.width, composition->viewport.plane0.width + ReadBlockWidthY[k] - 1), ReadBlockWidthY[k]) *
				math_ceil2(math_min2(surface->plane0.height, composition->viewport.plane0.height + ReadBlockHeightY[k] - 1), ReadBlockHeightY[k]) * BytesPerPixelY[k]);
			if (ReadBlockWidthC[k] > 0) {
				SurfaceSizeInMALL[k] = (unsigned int)(SurfaceSizeInMALL[k] +
					math_ceil2(math_min2(surface->plane1.width, composition->viewport.plane1.width + ReadBlockWidthC[k] - 1), ReadBlockWidthC[k]) *
					math_ceil2(math_min2(surface->plane1.height, composition->viewport.plane1.height + ReadBlockHeightC[k] - 1), ReadBlockHeightC[k]) * BytesPerPixelC[k]);
			}
		}
	}

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		/* SS and Subvp counted separate as they are never used at the same time */
		if (dml_is_phantom_pipe(&display_cfg->plane_descriptors[k]))
			TotalSurfaceSizeInMALLForSubVP += SurfaceSizeInMALL[k];
		else if (display_cfg->plane_descriptors[k].overrides.refresh_from_mall == dml2_refresh_from_mall_mode_override_force_enable)
			TotalSurfaceSizeInMALLForSS += SurfaceSizeInMALL[k];
	}

	*ExceededMALLSize = (TotalSurfaceSizeInMALLForSS > MALLAllocatedForDCNInBytes) ||
		(TotalSurfaceSizeInMALLForSubVP > MALLAllocatedForDCNInBytes);

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: MALLAllocatedForDCN = %u\n", __func__, MALLAllocatedForDCN * 1024 * 1024);
	dml2_printf("DML::%s: TotalSurfaceSizeInMALLForSubVP = %u\n", __func__, TotalSurfaceSizeInMALLForSubVP);
	dml2_printf("DML::%s: TotalSurfaceSizeInMALLForSS = %u\n", __func__, TotalSurfaceSizeInMALLForSS);
	dml2_printf("DML::%s: ExceededMALLSize = %u\n", __func__, *ExceededMALLSize);
#endif
}

static void calculate_tdlut_setting(
		struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_calculate_tdlut_setting_params *p)
{
	// locals
	unsigned int tdlut_bpe = 8;
	unsigned int tdlut_width;
	unsigned int tdlut_pitch_bytes;
	unsigned int tdlut_footprint_bytes;
	unsigned int vmpg_bytes;
	unsigned int tdlut_vmpg_per_frame;
	unsigned int tdlut_pte_req_per_frame;
	unsigned int tdlut_bytes_per_line;
	unsigned int tdlut_delivery_cycles;
	double tdlut_drain_rate;
	unsigned int tdlut_mpc_width;
	unsigned int tdlut_bytes_per_group_simple;

	if (!p->setup_for_tdlut) {
		*p->tdlut_groups_per_2row_ub = 0;
		*p->tdlut_opt_time = 0;
		*p->tdlut_drain_time = 0;
		*p->tdlut_bytes_per_group = 0;
		*p->tdlut_pte_bytes_per_frame = 0;
		*p->tdlut_bytes_per_frame = 0;
		return;
	}

	if (p->tdlut_mpc_width_flag) {
		tdlut_mpc_width = 33;
		tdlut_bytes_per_group_simple = 39*256;
	} else {
		tdlut_mpc_width = 17;
		tdlut_bytes_per_group_simple = 10*256;
	}

	vmpg_bytes = p->gpuvm_page_size_kbytes * 1024;

	if (p->tdlut_addressing_mode == dml2_tdlut_simple_linear) {
		if (p->tdlut_width_mode == dml2_tdlut_width_17_cube)
			tdlut_width = 4916;
		else
			tdlut_width = 35940;
	} else {
		if (p->tdlut_width_mode == dml2_tdlut_width_17_cube)
			tdlut_width = 17;
		else // dml2_tdlut_width_33_cube
			tdlut_width = 33;
	}

	if (p->is_gfx11)
		tdlut_pitch_bytes = (unsigned int)math_ceil2(tdlut_width * tdlut_bpe, 256); //256B alignment
	else
		tdlut_pitch_bytes = (unsigned int)math_ceil2(tdlut_width * tdlut_bpe, 128); //128B alignment

	if (p->tdlut_addressing_mode == dml2_tdlut_sw_linear)
		tdlut_footprint_bytes = tdlut_pitch_bytes * tdlut_width * tdlut_width;
	else
		tdlut_footprint_bytes = tdlut_pitch_bytes;

	if (!p->gpuvm_enable) {
		tdlut_vmpg_per_frame = 0;
		tdlut_pte_req_per_frame = 0;
	} else {
		tdlut_vmpg_per_frame = (unsigned int)math_ceil2(tdlut_footprint_bytes - 1, vmpg_bytes) / vmpg_bytes + 1;
		tdlut_pte_req_per_frame = (unsigned int)math_ceil2(tdlut_vmpg_per_frame - 1, 8) / 8 + 1;
	}
	tdlut_bytes_per_line = (unsigned int)math_ceil2(tdlut_width * tdlut_bpe, 64); //64b request
	*p->tdlut_pte_bytes_per_frame = tdlut_pte_req_per_frame * 64;

	if (p->tdlut_addressing_mode == dml2_tdlut_sw_linear) {
		//the tdlut_width is either 17 or 33 but the 33x33x33 is subsampled every other line/slice
		*p->tdlut_bytes_per_frame = tdlut_bytes_per_line * tdlut_mpc_width * tdlut_mpc_width;
		*p->tdlut_bytes_per_group = tdlut_bytes_per_line * tdlut_mpc_width;
		//the delivery cycles is DispClk cycles per line * number of lines * number of slices
		tdlut_delivery_cycles = (unsigned int)math_ceil2(tdlut_mpc_width/2.0, 1) * tdlut_mpc_width * tdlut_mpc_width;
		tdlut_drain_rate = tdlut_bytes_per_line * p->dispclk_mhz / math_ceil2(tdlut_mpc_width/2.0, 1);
	} else {
		//tdlut_addressing_mode = tdlut_simple_linear, 3dlut width should be 4*1229=4916 elements
		*p->tdlut_bytes_per_frame = (unsigned int)math_ceil2(tdlut_width * tdlut_bpe, 256);
		*p->tdlut_bytes_per_group = tdlut_bytes_per_group_simple;
		tdlut_delivery_cycles = (unsigned int)math_ceil2(tdlut_width/2.0, 1);
		tdlut_drain_rate = 2 * tdlut_bpe * p->dispclk_mhz;
	}

	//the tdlut is fetched during the 2 row times of prefetch.
	if (p->setup_for_tdlut) {
		*p->tdlut_groups_per_2row_ub = (unsigned int)math_ceil2((double) *p->tdlut_bytes_per_frame / *p->tdlut_bytes_per_group, 1);
		*p->tdlut_opt_time = (*p->tdlut_bytes_per_frame - p->cursor_buffer_size * 1024) / tdlut_drain_rate;
		*p->tdlut_drain_time = p->cursor_buffer_size * 1024 / tdlut_drain_rate;
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: gpuvm_enable = %d\n", __func__, p->gpuvm_enable);
	dml2_printf("DML::%s: vmpg_bytes = %d\n", __func__, vmpg_bytes);
	dml2_printf("DML::%s: tdlut_vmpg_per_frame = %d\n", __func__, tdlut_vmpg_per_frame);
	dml2_printf("DML::%s: tdlut_pte_req_per_frame = %d\n", __func__, tdlut_pte_req_per_frame);

	dml2_printf("DML::%s: dispclk_mhz = %f\n", __func__, p->dispclk_mhz);
	dml2_printf("DML::%s: tdlut_width = %u\n", __func__, tdlut_width);
	dml2_printf("DML::%s: tdlut_addressing_mode = %s\n", __func__, (p->tdlut_addressing_mode == dml2_tdlut_sw_linear) ? "sw_linear" : "simple_linear");
	dml2_printf("DML::%s: tdlut_pitch_bytes = %u\n", __func__, tdlut_pitch_bytes);
	dml2_printf("DML::%s: tdlut_footprint_bytes = %u\n", __func__, tdlut_footprint_bytes);
	dml2_printf("DML::%s: tdlut_bytes_per_frame = %u\n", __func__, *p->tdlut_bytes_per_frame);
	dml2_printf("DML::%s: tdlut_bytes_per_line = %u\n", __func__, tdlut_bytes_per_line);
	dml2_printf("DML::%s: tdlut_bytes_per_group = %u\n", __func__, *p->tdlut_bytes_per_group);
	dml2_printf("DML::%s: tdlut_drain_rate = %f\n", __func__, tdlut_drain_rate);
	dml2_printf("DML::%s: tdlut_delivery_cycles = %u\n", __func__, tdlut_delivery_cycles);
	dml2_printf("DML::%s: tdlut_opt_time = %f\n", __func__, *p->tdlut_opt_time);
	dml2_printf("DML::%s: tdlut_drain_time = %f\n", __func__, *p->tdlut_drain_time);
	dml2_printf("DML::%s: tdlut_groups_per_2row_ub = %d\n", __func__, *p->tdlut_groups_per_2row_ub);
#endif
}

static void CalculateTarb(
	const struct dml2_display_cfg *display_cfg,
	unsigned int PixelChunkSizeInKByte,
	unsigned int NumberOfActiveSurfaces,
	unsigned int NumberOfDPP[],
	unsigned int dpte_group_bytes[],
	unsigned int tdlut_bytes_per_group[],
	double HostVMInefficiencyFactor,
	double HostVMInefficiencyFactorPrefetch,
	unsigned int HostVMMinPageSize,
	double ReturnBW,
	unsigned int MetaChunkSize,

	// output
	double *Tarb,
	double *Tarb_prefetch)
{
	double extra_bytes = 0;
	double extra_bytes_prefetch = 0;
	double HostVMDynamicLevels = CalculateHostVMDynamicLevels(display_cfg->gpuvm_enable, display_cfg->hostvm_enable, HostVMMinPageSize, display_cfg->hostvm_max_non_cached_page_table_levels);

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		extra_bytes = extra_bytes + (NumberOfDPP[k] * PixelChunkSizeInKByte * 1024);

		if (display_cfg->plane_descriptors[k].surface.dcc.enable)
			extra_bytes = extra_bytes + (MetaChunkSize * 1024);

		if (display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut)
			extra_bytes = extra_bytes + tdlut_bytes_per_group[k];
	}

	extra_bytes_prefetch = extra_bytes;

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (display_cfg->gpuvm_enable == true) {
			extra_bytes = extra_bytes + NumberOfDPP[k] * dpte_group_bytes[k] * (1 + 8 * HostVMDynamicLevels) * HostVMInefficiencyFactor;
			extra_bytes_prefetch = extra_bytes_prefetch + NumberOfDPP[k] * dpte_group_bytes[k] * (1 + 8 * HostVMDynamicLevels) * HostVMInefficiencyFactorPrefetch;
		}
	}
	*Tarb = extra_bytes / ReturnBW;
	*Tarb_prefetch = extra_bytes_prefetch / ReturnBW;
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: PixelChunkSizeInKByte = %d\n", __func__, PixelChunkSizeInKByte);
	dml2_printf("DML::%s: MetaChunkSize = %d\n", __func__, MetaChunkSize);
	dml2_printf("DML::%s: extra_bytes = %f\n", __func__, extra_bytes);
	dml2_printf("DML::%s: extra_bytes_prefetch = %f\n", __func__, extra_bytes_prefetch);
#endif
}

static double CalculateTWait(
	long reserved_vblank_time_ns,
	double UrgentLatency,
	double Ttrip,
	double g6_temp_read_blackout_us)
{
	double TWait;
	double t_urg_trip = math_max2(UrgentLatency, Ttrip);
	TWait = math_max2(reserved_vblank_time_ns/1000.0, g6_temp_read_blackout_us) + t_urg_trip;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: reserved_vblank_time_ns = %d\n", __func__, reserved_vblank_time_ns);
	dml2_printf("DML::%s: UrgentLatency = %f\n", __func__, UrgentLatency);
	dml2_printf("DML::%s: Ttrip = %f\n", __func__, Ttrip);
	dml2_printf("DML::%s: TWait = %f\n", __func__, TWait);
#endif
	return TWait;
}


static void CalculateVUpdateAndDynamicMetadataParameters(
	unsigned int MaxInterDCNTileRepeaters,
	double Dppclk,
	double Dispclk,
	double DCFClkDeepSleep,
	double PixelClock,
	unsigned int HTotal,
	unsigned int VBlank,
	unsigned int DynamicMetadataTransmittedBytes,
	unsigned int DynamicMetadataLinesBeforeActiveRequired,
	unsigned int InterlaceEnable,
	bool ProgressiveToInterlaceUnitInOPP,

	// Output
	double *TSetup,
	double *Tdmbf,
	double *Tdmec,
	double *Tdmsks,
	unsigned int *VUpdateOffsetPix,
	unsigned int *VUpdateWidthPix,
	unsigned int *VReadyOffsetPix)
{
	double TotalRepeaterDelayTime;
	TotalRepeaterDelayTime = MaxInterDCNTileRepeaters * (2 / Dppclk + 3 / Dispclk);
	*VUpdateWidthPix = (unsigned int)(math_ceil2((14.0 / DCFClkDeepSleep + 12.0 / Dppclk + TotalRepeaterDelayTime) * PixelClock, 1.0));
	*VReadyOffsetPix = (unsigned int)(math_ceil2(math_max2(150.0 / Dppclk, TotalRepeaterDelayTime + 20.0 / DCFClkDeepSleep + 10.0 / Dppclk) * PixelClock, 1.0));
	*VUpdateOffsetPix = (unsigned int)(math_ceil2(HTotal / 4.0, 1.0));
	*TSetup = (*VUpdateOffsetPix + *VUpdateWidthPix + *VReadyOffsetPix) / PixelClock;
	*Tdmbf = DynamicMetadataTransmittedBytes / 4.0 / Dispclk;
	*Tdmec = HTotal / PixelClock;

	if (DynamicMetadataLinesBeforeActiveRequired == 0) {
		*Tdmsks = VBlank * HTotal / PixelClock / 2.0;
	} else {
		*Tdmsks = DynamicMetadataLinesBeforeActiveRequired * HTotal / PixelClock;
	}
	if (InterlaceEnable == 1 && ProgressiveToInterlaceUnitInOPP == false) {
		*Tdmsks = *Tdmsks / 2;
	}
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: DynamicMetadataLinesBeforeActiveRequired = %u\n", __func__, DynamicMetadataLinesBeforeActiveRequired);
	dml2_printf("DML::%s: VBlank = %u\n", __func__, VBlank);
	dml2_printf("DML::%s: HTotal = %u\n", __func__, HTotal);
	dml2_printf("DML::%s: PixelClock = %f\n", __func__, PixelClock);
	dml2_printf("DML::%s: Dppclk = %f\n", __func__, Dppclk);
	dml2_printf("DML::%s: DCFClkDeepSleep = %f\n", __func__, DCFClkDeepSleep);
	dml2_printf("DML::%s: MaxInterDCNTileRepeaters = %u\n", __func__, MaxInterDCNTileRepeaters);
	dml2_printf("DML::%s: TotalRepeaterDelayTime = %f\n", __func__, TotalRepeaterDelayTime);

	dml2_printf("DML::%s: VUpdateWidthPix = %u\n", __func__, *VUpdateWidthPix);
	dml2_printf("DML::%s: VReadyOffsetPix = %u\n", __func__, *VReadyOffsetPix);
	dml2_printf("DML::%s: VUpdateOffsetPix = %u\n", __func__, *VUpdateOffsetPix);

	dml2_printf("DML::%s: Tdmsks = %f\n", __func__, *Tdmsks);
#endif
}

static double get_urgent_bandwidth_required(
	struct dml2_core_shared_get_urgent_bandwidth_required_locals *l,
	const struct dml2_display_cfg *display_cfg,
	enum dml2_core_internal_soc_state_type state_type,
	enum dml2_core_internal_bw_type bw_type,
	bool inc_flip_bw, // including flip bw
	bool use_qual_row_bw,
	unsigned int NumberOfActiveSurfaces,
	unsigned int NumberOfDPP[],
	double dcc_dram_bw_nom_overhead_factor_p0[],
	double dcc_dram_bw_nom_overhead_factor_p1[],
	double dcc_dram_bw_pref_overhead_factor_p0[],
	double dcc_dram_bw_pref_overhead_factor_p1[],
	double mall_prefetch_sdp_overhead_factor[],
	double mall_prefetch_dram_overhead_factor[],
	double ReadBandwidthLuma[],
	double ReadBandwidthChroma[],
	double PrefetchBandwidthLuma[],
	double PrefetchBandwidthChroma[],
	double excess_vactive_fill_bw_l[],
	double excess_vactive_fill_bw_c[],
	double cursor_bw[],
	double dpte_row_bw[],
	double meta_row_bw[],
	double prefetch_cursor_bw[],
	double prefetch_vmrow_bw[],
	double flip_bw[],
	double UrgentBurstFactorLuma[],
	double UrgentBurstFactorChroma[],
	double UrgentBurstFactorCursor[],
	double UrgentBurstFactorLumaPre[],
	double UrgentBurstFactorChromaPre[],
	double UrgentBurstFactorCursorPre[],
	/* outputs */
	double surface_required_bw[],
	double surface_peak_required_bw[])
{
	// set inc_flip_bw = 0 for total_dchub_urgent_read_bw_noflip calculation, 1 for total_dchub_urgent_read_bw as described in the MAS
	// set use_qual_row_bw = 1 to calculate using qualified row bandwidth, used for total_flip_bw calculation

	memset(l, 0, sizeof(struct dml2_core_shared_get_urgent_bandwidth_required_locals));

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		l->mall_svp_prefetch_factor = (state_type == dml2_core_internal_soc_state_svp_prefetch) ? (bw_type == dml2_core_internal_bw_dram ? mall_prefetch_dram_overhead_factor[k] : mall_prefetch_sdp_overhead_factor[k]) : 1.0;
		l->tmp_nom_adj_factor_p0 = (bw_type == dml2_core_internal_bw_dram ? dcc_dram_bw_nom_overhead_factor_p0[k] : 1.0) * l->mall_svp_prefetch_factor;
		l->tmp_nom_adj_factor_p1 = (bw_type == dml2_core_internal_bw_dram ? dcc_dram_bw_nom_overhead_factor_p1[k] : 1.0) * l->mall_svp_prefetch_factor;
		l->tmp_pref_adj_factor_p0 = (bw_type == dml2_core_internal_bw_dram ? dcc_dram_bw_pref_overhead_factor_p0[k] : 1.0) * l->mall_svp_prefetch_factor;
		l->tmp_pref_adj_factor_p1 = (bw_type == dml2_core_internal_bw_dram ? dcc_dram_bw_pref_overhead_factor_p1[k] : 1.0) * l->mall_svp_prefetch_factor;

		l->adj_factor_p0 = UrgentBurstFactorLuma[k] * l->tmp_nom_adj_factor_p0;
		l->adj_factor_p1 = UrgentBurstFactorChroma[k] * l->tmp_nom_adj_factor_p1;
		l->adj_factor_cur = UrgentBurstFactorCursor[k];
		l->adj_factor_p0_pre = UrgentBurstFactorLumaPre[k] * l->tmp_pref_adj_factor_p0;
		l->adj_factor_p1_pre = UrgentBurstFactorChromaPre[k] * l->tmp_pref_adj_factor_p1;
		l->adj_factor_cur_pre = UrgentBurstFactorCursorPre[k];

		bool is_phantom = dml_is_phantom_pipe(&display_cfg->plane_descriptors[k]);
		bool exclude_this_plane = 0;

		// Exclude phantom pipe in bw calculation for non svp prefetch state
		if (state_type != dml2_core_internal_soc_state_svp_prefetch && is_phantom)
			exclude_this_plane = 1;

		// The qualified row bandwidth, qual_row_bw, accounts for the regular non-flip row bandwidth when there is no possible immediate flip or HostVM invalidation flip.
		// The qual_row_bw is zero if HostVM is possible and only non-zero and equal to row_bw(i) if immediate flip is not allowed for that pipe.
		if (use_qual_row_bw) {
			if (display_cfg->hostvm_enable)
				l->per_plane_flip_bw[k] = 0; // qual_row_bw
			else if (!display_cfg->plane_descriptors[k].immediate_flip)
				l->per_plane_flip_bw[k] = NumberOfDPP[k] * (dpte_row_bw[k] + meta_row_bw[k]);
		} else {
			// the final_flip_bw includes the regular row_bw when immediate flip is disallowed (and no HostVM)
			if ((!display_cfg->plane_descriptors[k].immediate_flip && !display_cfg->hostvm_enable) || !inc_flip_bw)
				l->per_plane_flip_bw[k] = NumberOfDPP[k] * (dpte_row_bw[k] + meta_row_bw[k]);
			else
				l->per_plane_flip_bw[k] = NumberOfDPP[k] * flip_bw[k];
		}

		if (!exclude_this_plane) {
			l->vm_row_bw = NumberOfDPP[k] * prefetch_vmrow_bw[k];
			l->flip_and_active_bw = l->per_plane_flip_bw[k] + ReadBandwidthLuma[k] * l->adj_factor_p0 + ReadBandwidthChroma[k] * l->adj_factor_p1 + cursor_bw[k] * l->adj_factor_cur;
			l->flip_and_prefetch_bw = l->per_plane_flip_bw[k] + NumberOfDPP[k] * (PrefetchBandwidthLuma[k] * l->adj_factor_p0_pre + PrefetchBandwidthChroma[k] * l->adj_factor_p1_pre) + prefetch_cursor_bw[k] * l->adj_factor_cur_pre;
			l->active_and_excess_bw = (ReadBandwidthLuma[k] + excess_vactive_fill_bw_l[k]) * l->tmp_nom_adj_factor_p0 + (ReadBandwidthChroma[k] + excess_vactive_fill_bw_c[k]) * l->tmp_nom_adj_factor_p1 + dpte_row_bw[k] + meta_row_bw[k];
			surface_required_bw[k] = math_max4(l->vm_row_bw, l->flip_and_active_bw, l->flip_and_prefetch_bw, l->active_and_excess_bw);

			/* export peak required bandwidth for the surface */
			surface_peak_required_bw[k] = math_max2(surface_required_bw[k], surface_peak_required_bw[k]);

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%d, max1: vm_row_bw=%f\n", __func__, k, l->vm_row_bw);
			dml2_printf("DML::%s: k=%d, max2: flip_and_active_bw=%f\n", __func__, k, l->flip_and_active_bw);
			dml2_printf("DML::%s: k=%d, max3: flip_and_prefetch_bw=%f\n", __func__, k, l->flip_and_prefetch_bw);
			dml2_printf("DML::%s: k=%d, max4: active_and_excess_bw=%f\n", __func__, k, l->active_and_excess_bw);
			dml2_printf("DML::%s: k=%d, surface_required_bw=%f\n", __func__, k, surface_required_bw[k]);
			dml2_printf("DML::%s: k=%d, surface_peak_required_bw=%f\n", __func__, k, surface_peak_required_bw[k]);
#endif
		} else {
			surface_required_bw[k] = 0.0;
		}

		l->required_bandwidth_mbps += surface_required_bw[k];

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%d, NumberOfDPP=%d\n", __func__, k, NumberOfDPP[k]);
		dml2_printf("DML::%s: k=%d, use_qual_row_bw=%d\n", __func__, k, use_qual_row_bw);
		dml2_printf("DML::%s: k=%d, immediate_flip=%d\n", __func__, k, display_cfg->plane_descriptors[k].immediate_flip);
		dml2_printf("DML::%s: k=%d, mall_svp_prefetch_factor=%f\n", __func__, k, l->mall_svp_prefetch_factor);
		dml2_printf("DML::%s: k=%d, adj_factor_p0=%f\n", __func__, k, l->adj_factor_p0);
		dml2_printf("DML::%s: k=%d, adj_factor_p1=%f\n", __func__, k, l->adj_factor_p1);
		dml2_printf("DML::%s: k=%d, adj_factor_cur=%f\n", __func__, k, l->adj_factor_cur);

		dml2_printf("DML::%s: k=%d, adj_factor_p0_pre=%f\n", __func__, k, l->adj_factor_p0_pre);
		dml2_printf("DML::%s: k=%d, adj_factor_p1_pre=%f\n", __func__, k, l->adj_factor_p1_pre);
		dml2_printf("DML::%s: k=%d, adj_factor_cur_pre=%f\n", __func__, k, l->adj_factor_cur_pre);

		dml2_printf("DML::%s: k=%d, per_plane_flip_bw=%f\n", __func__, k, l->per_plane_flip_bw[k]);
		dml2_printf("DML::%s: k=%d, prefetch_vmrow_bw=%f\n", __func__, k, prefetch_vmrow_bw[k]);
		dml2_printf("DML::%s: k=%d, ReadBandwidthLuma=%f\n", __func__, k, ReadBandwidthLuma[k]);
		dml2_printf("DML::%s: k=%d, ReadBandwidthChroma=%f\n", __func__, k, ReadBandwidthChroma[k]);
		dml2_printf("DML::%s: k=%d, excess_vactive_fill_bw_l=%f\n", __func__, k, excess_vactive_fill_bw_l[k]);
		dml2_printf("DML::%s: k=%d, excess_vactive_fill_bw_c=%f\n", __func__, k, excess_vactive_fill_bw_c[k]);
		dml2_printf("DML::%s: k=%d, cursor_bw=%f\n", __func__, k, cursor_bw[k]);

		dml2_printf("DML::%s: k=%d, meta_row_bw=%f\n", __func__, k, meta_row_bw[k]);
		dml2_printf("DML::%s: k=%d, dpte_row_bw=%f\n", __func__, k, dpte_row_bw[k]);
		dml2_printf("DML::%s: k=%d, PrefetchBandwidthLuma=%f\n", __func__, k, PrefetchBandwidthLuma[k]);
		dml2_printf("DML::%s: k=%d, PrefetchBandwidthChroma=%f\n", __func__, k, PrefetchBandwidthChroma[k]);
		dml2_printf("DML::%s: k=%d, prefetch_cursor_bw=%f\n", __func__, k, prefetch_cursor_bw[k]);
		dml2_printf("DML::%s: k=%d, required_bandwidth_mbps=%f (total), inc_flip_bw=%d, is_phantom=%d exclude_this_plane=%d\n", __func__, k, l->required_bandwidth_mbps, inc_flip_bw, is_phantom, exclude_this_plane);
		dml2_printf("DML::%s: k=%d, required_bandwidth_mbps=%f (total), soc_state=%s, inc_flip_bw=%d, is_phantom=%d exclude_this_plane=%d\n", __func__, k, l->required_bandwidth_mbps, dml2_core_internal_soc_state_type_str(state_type), inc_flip_bw, is_phantom, exclude_this_plane);
		dml2_printf("DML::%s: k=%d, required_bandwidth_mbps=%f (total), inc_flip_bw=%d, is_phantom=%d exclude_this_plane=%d\n", __func__, k, l->required_bandwidth_mbps, inc_flip_bw, is_phantom, exclude_this_plane);
#endif
	}

	return l->required_bandwidth_mbps;
}

static void CalculateExtraLatency(
	const struct dml2_display_cfg *display_cfg,
	unsigned int ROBBufferSizeInKByte,
	unsigned int RoundTripPingLatencyCycles,
	unsigned int ReorderingBytes,
	double DCFCLK,
	double FabricClock,
	unsigned int PixelChunkSizeInKByte,
	double ReturnBW,
	unsigned int NumberOfActiveSurfaces,
	unsigned int NumberOfDPP[],
	unsigned int dpte_group_bytes[],
	unsigned int tdlut_bytes_per_group[],
	double HostVMInefficiencyFactor,
	double HostVMInefficiencyFactorPrefetch,
	unsigned int HostVMMinPageSize,
	enum dml2_qos_param_type qos_type,
	bool max_oustanding_when_urgent_expected,
	unsigned int max_outstanding_requests,
	unsigned int request_size_bytes_luma[],
	unsigned int request_size_bytes_chroma[],
	unsigned int MetaChunkSize,
	unsigned int dchub_arb_to_ret_delay,
	double Ttrip,
	unsigned int hostvm_mode,

	// output
	double *ExtraLatency, // Tex
	double *ExtraLatency_sr, // Tex_sr
	double *ExtraLatencyPrefetch)

{
	double Tarb;
	double Tarb_prefetch;
	double Tex_trips;
	unsigned int max_request_size_bytes = 0;

	CalculateTarb(
		display_cfg,
		PixelChunkSizeInKByte,
		NumberOfActiveSurfaces,
		NumberOfDPP,
		dpte_group_bytes,
		tdlut_bytes_per_group,
		HostVMInefficiencyFactor,
		HostVMInefficiencyFactorPrefetch,
		HostVMMinPageSize,
		ReturnBW,
		MetaChunkSize,
		// output
		&Tarb,
		&Tarb_prefetch);

	Tex_trips = (display_cfg->hostvm_enable && hostvm_mode == 1) ? (2.0 * Ttrip) : 0.0;

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (request_size_bytes_luma[k] > max_request_size_bytes)
			max_request_size_bytes = request_size_bytes_luma[k];
		if (request_size_bytes_chroma[k] > max_request_size_bytes)
			max_request_size_bytes = request_size_bytes_chroma[k];
	}

	if (qos_type == dml2_qos_param_type_dcn4x) {
		*ExtraLatency_sr = dchub_arb_to_ret_delay / DCFCLK;
		*ExtraLatency = *ExtraLatency_sr;
		if (max_oustanding_when_urgent_expected)
			*ExtraLatency = *ExtraLatency + (ROBBufferSizeInKByte * 1024 - max_outstanding_requests * max_request_size_bytes) / ReturnBW;
	} else {
		*ExtraLatency_sr = dchub_arb_to_ret_delay / DCFCLK + RoundTripPingLatencyCycles / FabricClock + ReorderingBytes / ReturnBW;
		*ExtraLatency = *ExtraLatency_sr;
	}
	*ExtraLatency = *ExtraLatency + Tex_trips;
	*ExtraLatencyPrefetch = *ExtraLatency + Tarb_prefetch;
	*ExtraLatency = *ExtraLatency + Tarb;
	*ExtraLatency_sr = *ExtraLatency_sr + Tarb;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: qos_type=%u\n", __func__, qos_type);
	dml2_printf("DML::%s: hostvm_mode=%u\n", __func__, hostvm_mode);
	dml2_printf("DML::%s: Tex_trips=%u\n", __func__, Tex_trips);
	dml2_printf("DML::%s: max_oustanding_when_urgent_expected=%u\n", __func__, max_oustanding_when_urgent_expected);
	dml2_printf("DML::%s: FabricClock=%f\n", __func__, FabricClock);
	dml2_printf("DML::%s: DCFCLK=%f\n", __func__, DCFCLK);
	dml2_printf("DML::%s: ReturnBW=%f\n", __func__, ReturnBW);
	dml2_printf("DML::%s: RoundTripPingLatencyCycles=%u\n", __func__, RoundTripPingLatencyCycles);
	dml2_printf("DML::%s: ReorderingBytes=%u\n", __func__, ReorderingBytes);
	dml2_printf("DML::%s: Tarb=%f\n", __func__, Tarb);
	dml2_printf("DML::%s: ExtraLatency=%f\n", __func__, *ExtraLatency);
	dml2_printf("DML::%s: ExtraLatency_sr=%f\n", __func__, *ExtraLatency_sr);
	dml2_printf("DML::%s: ExtraLatencyPrefetch=%f\n", __func__, *ExtraLatencyPrefetch);
#endif
}

static bool CalculatePrefetchSchedule(struct dml2_core_internal_scratch *scratch, struct dml2_core_calcs_CalculatePrefetchSchedule_params *p)
{
	struct dml2_core_calcs_CalculatePrefetchSchedule_locals *s = &scratch->CalculatePrefetchSchedule_locals;
	bool dcc_mrq_enable;

	unsigned int vm_bytes;
	unsigned int extra_tdpe_bytes;
	unsigned int tdlut_row_bytes;
	unsigned int Lo;

	s->NoTimeToPrefetch = false;
	s->DPPCycles = 0;
	s->DISPCLKCycles = 0;
	s->DSTTotalPixelsAfterScaler = 0.0;
	s->LineTime = 0.0;
	s->dst_y_prefetch_equ = 0.0;
	s->prefetch_bw_oto = 0.0;
	s->Tvm_oto = 0.0;
	s->Tr0_oto = 0.0;
	s->Tvm_oto_lines = 0.0;
	s->Tr0_oto_lines = 0.0;
	s->dst_y_prefetch_oto = 0.0;
	s->TimeForFetchingVM = 0.0;
	s->TimeForFetchingRowInVBlank = 0.0;
	s->LinesToRequestPrefetchPixelData = 0.0;
	s->HostVMDynamicLevelsTrips = 0;
	s->trip_to_mem = 0.0;
	*p->Tvm_trips = 0.0;
	*p->Tr0_trips = 0.0;
	s->Tvm_trips_rounded = 0.0;
	s->Tr0_trips_rounded = 0.0;
	s->max_Tsw = 0.0;
	s->Lsw_oto = 0.0;
	*p->Tpre_rounded = 0.0;
	s->prefetch_bw_equ = 0.0;
	s->Tvm_equ = 0.0;
	s->Tr0_equ = 0.0;
	s->Tdmbf = 0.0;
	s->Tdmec = 0.0;
	s->Tdmsks = 0.0;
	*p->prefetch_sw_bytes = 0.0;
	s->prefetch_bw_pr = 0.0;
	s->bytes_pp = 0.0;
	s->dep_bytes = 0.0;
	s->min_Lsw_oto = 0.0;
	s->min_Lsw_equ = 0.0;
	s->Tsw_est1 = 0.0;
	s->Tsw_est2 = 0.0;
	s->Tsw_est3 = 0.0;
	s->cursor_prefetch_bytes = 0;
	*p->prefetch_cursor_bw = 0;

	dcc_mrq_enable = (p->dcc_enable && p->mrq_present);

	s->TWait_p = p->TWait - p->Ttrip; // TWait includes max(Turg, Ttrip) and Ttrip here is already max(Turg, Ttrip)

	if (p->display_cfg->gpuvm_enable == true && p->display_cfg->hostvm_enable == true) {
		s->HostVMDynamicLevelsTrips = p->display_cfg->hostvm_max_non_cached_page_table_levels;
	} else {
		s->HostVMDynamicLevelsTrips = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: dcc_enable = %u\n", __func__, p->dcc_enable);
	dml2_printf("DML::%s: mrq_present = %u\n", __func__, p->mrq_present);
	dml2_printf("DML::%s: dcc_mrq_enable = %u\n", __func__, dcc_mrq_enable);
	dml2_printf("DML::%s: GPUVMEnable = %u\n", __func__, p->display_cfg->gpuvm_enable);
	dml2_printf("DML::%s: GPUVMPageTableLevels = %u\n", __func__, p->display_cfg->gpuvm_max_page_table_levels);
	dml2_printf("DML::%s: DCCEnable = %u\n", __func__, p->myPipe->DCCEnable);
	dml2_printf("DML::%s: VStartup = %u\n", __func__, p->VStartup);
	dml2_printf("DML::%s: HostVMEnable = %u\n", __func__, p->display_cfg->hostvm_enable);
	dml2_printf("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, p->HostVMInefficiencyFactor);
	dml2_printf("DML::%s: TWait = %f\n", __func__, p->TWait);
	dml2_printf("DML::%s: TWait_p = %f\n", __func__, s->TWait_p);
	dml2_printf("DML::%s: Ttrip = %f\n", __func__, p->Ttrip);
	dml2_printf("DML::%s: myPipe->Dppclk = %f\n", __func__, p->myPipe->Dppclk);
	dml2_printf("DML::%s: myPipe->Dispclk = %f\n", __func__, p->myPipe->Dispclk);
#endif
	CalculateVUpdateAndDynamicMetadataParameters(
		p->MaxInterDCNTileRepeaters,
		p->myPipe->Dppclk,
		p->myPipe->Dispclk,
		p->myPipe->DCFClkDeepSleep,
		p->myPipe->PixelClock,
		p->myPipe->HTotal,
		p->myPipe->VBlank,
		p->DynamicMetadataTransmittedBytes,
		p->DynamicMetadataLinesBeforeActiveRequired,
		p->myPipe->InterlaceEnable,
		p->myPipe->ProgressiveToInterlaceUnitInOPP,
		p->TSetup,

		// Output
		&s->Tdmbf,
		&s->Tdmec,
		&s->Tdmsks,
		p->VUpdateOffsetPix,
		p->VUpdateWidthPix,
		p->VReadyOffsetPix);

	s->LineTime = p->myPipe->HTotal / p->myPipe->PixelClock;
	s->trip_to_mem = p->Ttrip;
	*p->Tvm_trips = p->ExtraLatencyPrefetch + math_max2(s->trip_to_mem * (p->display_cfg->gpuvm_max_page_table_levels * (s->HostVMDynamicLevelsTrips + 1)), p->Turg);
	if (dcc_mrq_enable)
		*p->Tvm_trips_flip = *p->Tvm_trips;
	else
		*p->Tvm_trips_flip = *p->Tvm_trips - s->trip_to_mem;

	*p->Tr0_trips_flip = s->trip_to_mem * (s->HostVMDynamicLevelsTrips + 1);
	*p->Tr0_trips = math_max2(*p->Tr0_trips_flip, p->tdlut_opt_time / 2);

	if (p->DynamicMetadataVMEnabled == true) {
		*p->Tdmdl_vm = s->TWait_p + *p->Tvm_trips;
		*p->Tdmdl = *p->Tdmdl_vm + p->Ttrip;
	} else {
		*p->Tdmdl_vm = 0;
		*p->Tdmdl = s->TWait_p + p->ExtraLatencyPrefetch + p->Ttrip; // Tex
	}

	if (p->DynamicMetadataEnable == true) {
		if (p->VStartup * s->LineTime < *p->TSetup + *p->Tdmdl + s->Tdmbf + s->Tdmec + s->Tdmsks) {
			*p->NotEnoughTimeForDynamicMetadata = true;
			dml2_printf("DML::%s: Not Enough Time for Dynamic Meta!\n", __func__);
			dml2_printf("DML::%s: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n", __func__, s->Tdmbf);
			dml2_printf("DML::%s: Tdmec: %fus - time dio takes to transfer dmd\n", __func__, s->Tdmec);
			dml2_printf("DML::%s: Tdmsks: %fus - time before active dmd must complete transmission at dio\n", __func__, s->Tdmsks);
			dml2_printf("DML::%s: Tdmdl: %fus - time for fabric to become ready and fetch dmd \n", __func__, *p->Tdmdl);
		} else {
			*p->NotEnoughTimeForDynamicMetadata = false;
		}
	} else {
		*p->NotEnoughTimeForDynamicMetadata = false;
	}

	if (p->myPipe->ScalerEnabled)
		s->DPPCycles = (unsigned int)(p->DPPCLKDelaySubtotalPlusCNVCFormater + p->DPPCLKDelaySCL);
	else
		s->DPPCycles = (unsigned int)(p->DPPCLKDelaySubtotalPlusCNVCFormater + p->DPPCLKDelaySCLLBOnly);

	s->DPPCycles = (unsigned int)(s->DPPCycles + p->myPipe->NumberOfCursors * p->DPPCLKDelayCNVCCursor);

	s->DISPCLKCycles = (unsigned int)p->DISPCLKDelaySubtotal;

	if (p->myPipe->Dppclk == 0.0 || p->myPipe->Dispclk == 0.0)
		return true;

	*p->DSTXAfterScaler = (unsigned int)math_round(s->DPPCycles * p->myPipe->PixelClock / p->myPipe->Dppclk + s->DISPCLKCycles * p->myPipe->PixelClock / p->myPipe->Dispclk + p->DSCDelay);
	*p->DSTXAfterScaler = (unsigned int)math_round(*p->DSTXAfterScaler + (p->myPipe->ODMMode != dml2_odm_mode_bypass ? 18 : 0) + (p->myPipe->DPPPerSurface - 1) * p->DPP_RECOUT_WIDTH +
		((p->myPipe->ODMMode == dml2_odm_mode_split_1to2 || p->myPipe->ODMMode == dml2_odm_mode_mso_1to2) ? (double)p->myPipe->HActive / 2.0 : 0) +
		((p->myPipe->ODMMode == dml2_odm_mode_mso_1to4) ? (double)p->myPipe->HActive * 3.0 / 4.0 : 0));

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: DynamicMetadataVMEnabled = %u\n", __func__, p->DynamicMetadataVMEnabled);
	dml2_printf("DML::%s: DPPCycles = %u\n", __func__, s->DPPCycles);
	dml2_printf("DML::%s: PixelClock = %f\n", __func__, p->myPipe->PixelClock);
	dml2_printf("DML::%s: Dppclk = %f\n", __func__, p->myPipe->Dppclk);
	dml2_printf("DML::%s: DISPCLKCycles = %u\n", __func__, s->DISPCLKCycles);
	dml2_printf("DML::%s: DISPCLK = %f\n", __func__, p->myPipe->Dispclk);
	dml2_printf("DML::%s: DSCDelay = %u\n", __func__, p->DSCDelay);
	dml2_printf("DML::%s: ODMMode = %u\n", __func__, p->myPipe->ODMMode);
	dml2_printf("DML::%s: DPP_RECOUT_WIDTH = %u\n", __func__, p->DPP_RECOUT_WIDTH);
	dml2_printf("DML::%s: DSTXAfterScaler = %u\n", __func__, *p->DSTXAfterScaler);

	dml2_printf("DML::%s: setup_for_tdlut = %u\n", __func__, p->setup_for_tdlut);
	dml2_printf("DML::%s: tdlut_opt_time = %f\n", __func__, p->tdlut_opt_time);
	dml2_printf("DML::%s: tdlut_pte_bytes_per_frame = %u\n", __func__, p->tdlut_pte_bytes_per_frame);
	dml2_printf("DML::%s: tdlut_drain_time = %f\n", __func__, p->tdlut_drain_time);
#endif

	if (p->OutputFormat == dml2_420 || (p->myPipe->InterlaceEnable && p->myPipe->ProgressiveToInterlaceUnitInOPP))
		*p->DSTYAfterScaler = 1;
	else
		*p->DSTYAfterScaler = 0;

	s->DSTTotalPixelsAfterScaler = *p->DSTYAfterScaler * p->myPipe->HTotal + *p->DSTXAfterScaler;
	*p->DSTYAfterScaler = (unsigned int)(math_floor2(s->DSTTotalPixelsAfterScaler / p->myPipe->HTotal, 1));
	*p->DSTXAfterScaler = (unsigned int)(s->DSTTotalPixelsAfterScaler - ((double)(*p->DSTYAfterScaler * p->myPipe->HTotal)));
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: DSTXAfterScaler = %u (final)\n", __func__, *p->DSTXAfterScaler);
	dml2_printf("DML::%s: DSTYAfterScaler = %u (final)\n", __func__, *p->DSTYAfterScaler);
#endif

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: Tr0_trips = %f\n", __func__, *p->Tr0_trips);
	dml2_printf("DML::%s: Tvm_trips = %f\n", __func__, *p->Tvm_trips);
	dml2_printf("DML::%s: trip_to_mem = %f\n", __func__, s->trip_to_mem);
	dml2_printf("DML::%s: ExtraLatencyPrefetch = %f\n", __func__, p->ExtraLatencyPrefetch);
	dml2_printf("DML::%s: GPUVMPageTableLevels = %u\n", __func__, p->display_cfg->gpuvm_max_page_table_levels);
	dml2_printf("DML::%s: HostVMDynamicLevelsTrips = %u\n", __func__, s->HostVMDynamicLevelsTrips);
#endif
	if (p->display_cfg->gpuvm_enable) {
		s->Tvm_trips_rounded = math_ceil2(4.0 * *p->Tvm_trips / s->LineTime, 1.0) / 4.0 * s->LineTime;
		*p->Tvm_trips_flip_rounded = math_ceil2(4.0 * *p->Tvm_trips_flip / s->LineTime, 1.0) / 4.0 * s->LineTime;
	} else {
		if (p->DynamicMetadataEnable || dcc_mrq_enable || p->setup_for_tdlut)
			s->Tvm_trips_rounded = math_max2(s->LineTime * math_ceil2(4.0*math_max3(p->ExtraLatencyPrefetch, p->Turg, s->trip_to_mem)/s->LineTime, 1)/4, s->LineTime/4.0);
		else
			s->Tvm_trips_rounded = s->LineTime / 4.0;
		*p->Tvm_trips_flip_rounded = s->LineTime / 4.0;
	}

	s->Tvm_trips_rounded = math_max2(s->Tvm_trips_rounded, s->LineTime / 4.0);
	*p->Tvm_trips_flip_rounded = math_max2(*p->Tvm_trips_flip_rounded, s->LineTime / 4.0);

	if (p->display_cfg->gpuvm_enable == true || p->setup_for_tdlut || dcc_mrq_enable) {
		s->Tr0_trips_rounded = math_ceil2(4.0 * *p->Tr0_trips / s->LineTime, 1.0) / 4.0 * s->LineTime;
		*p->Tr0_trips_flip_rounded = math_ceil2(4.0 * *p->Tr0_trips_flip / s->LineTime, 1.0) / 4.0 * s->LineTime;
	} else {
		s->Tr0_trips_rounded = s->LineTime / 4.0;
		*p->Tr0_trips_flip_rounded = s->LineTime / 4.0;
	}
	s->Tr0_trips_rounded = math_max2(s->Tr0_trips_rounded, s->LineTime / 4.0);
	*p->Tr0_trips_flip_rounded = math_max2(*p->Tr0_trips_flip_rounded, s->LineTime / 4.0);

	if (p->display_cfg->gpuvm_enable == true) {
		if (p->display_cfg->gpuvm_max_page_table_levels >= 3) {
			*p->Tno_bw = p->ExtraLatencyPrefetch + s->trip_to_mem * (double)((p->display_cfg->gpuvm_max_page_table_levels - 2) * (s->HostVMDynamicLevelsTrips + 1));
		} else if (p->display_cfg->gpuvm_max_page_table_levels == 1 && !dcc_mrq_enable && !p->setup_for_tdlut) {
			*p->Tno_bw = p->ExtraLatencyPrefetch;
		} else {
			*p->Tno_bw = 0;
		}
	} else {
		*p->Tno_bw = 0;
	}

	if (p->mrq_present || p->display_cfg->gpuvm_max_page_table_levels >= 3)
		*p->Tno_bw_flip = *p->Tno_bw;
	else
		*p->Tno_bw_flip = 0; //because there is no 3DLUT for iFlip

	if (dml_is_420(p->myPipe->SourcePixelFormat)) {
		s->bytes_pp = p->myPipe->BytePerPixelY + p->myPipe->BytePerPixelC / 4.0;
	} else {
		s->bytes_pp = p->myPipe->BytePerPixelY + p->myPipe->BytePerPixelC;
	}

	*p->prefetch_sw_bytes = p->PrefetchSourceLinesY * p->swath_width_luma_ub * p->myPipe->BytePerPixelY + p->PrefetchSourceLinesC * p->swath_width_chroma_ub * p->myPipe->BytePerPixelC;
	*p->prefetch_sw_bytes = *p->prefetch_sw_bytes * p->mall_prefetch_sdp_overhead_factor;

	vm_bytes = p->vm_bytes; // vm_bytes is dpde0_bytes_per_frame_ub_l + dpde0_bytes_per_frame_ub_c + 2*extra_dpde_bytes;
	extra_tdpe_bytes = (unsigned int)math_max2(0, (p->display_cfg->gpuvm_max_page_table_levels - 1) * 128);

	if (p->setup_for_tdlut)
		vm_bytes = vm_bytes + p->tdlut_pte_bytes_per_frame + (p->display_cfg->gpuvm_enable ? extra_tdpe_bytes : 0);

	tdlut_row_bytes = (unsigned long) math_ceil2(p->tdlut_bytes_per_frame/2.0, 1.0);

	s->min_Lsw_oto = math_max2(p->PrefetchSourceLinesY, p->PrefetchSourceLinesC) / __DML2_CALCS_MAX_VRATIO_PRE_OTO__;
	s->min_Lsw_oto = math_max2(s->min_Lsw_oto, p->tdlut_drain_time / s->LineTime);
	s->min_Lsw_oto = math_max2(s->min_Lsw_oto, 2.0);

	// use vactive swath bw for prefetch oto and also cap prefetch_bw_oto to max_vratio_oto
	// Note: in prefetch calculation, acounting is done mostly per-pipe.
	// vactive swath bw represents the per-surface (aka per dml plane) bw to move vratio_l/c lines of bytes_l/c per line time
	s->per_pipe_vactive_sw_bw = p->vactive_sw_bw_l / (double)p->myPipe->DPPPerSurface;

	// one-to-one prefetch bw as one line of bytes per line time (as per vratio_pre_l/c = 1)
	s->prefetch_bw_oto = (p->swath_width_luma_ub * p->myPipe->BytePerPixelY) / s->LineTime;

	if (p->myPipe->BytePerPixelC > 0) {
		s->per_pipe_vactive_sw_bw += p->vactive_sw_bw_c / (double)p->myPipe->DPPPerSurface;
		s->prefetch_bw_oto += (p->swath_width_chroma_ub * p->myPipe->BytePerPixelC) / s->LineTime;
	}

	s->prefetch_bw_oto = math_max2(s->per_pipe_vactive_sw_bw, s->prefetch_bw_oto) * p->mall_prefetch_sdp_overhead_factor;

	s->prefetch_bw_oto = math_min2(s->prefetch_bw_oto, *p->prefetch_sw_bytes/(s->min_Lsw_oto*s->LineTime));

	s->Lsw_oto = math_ceil2(4.0 * *p->prefetch_sw_bytes / s->prefetch_bw_oto / s->LineTime, 1.0) / 4.0;

	s->prefetch_bw_oto = math_max3(s->prefetch_bw_oto,
					p->vm_bytes * p->HostVMInefficiencyFactor / (31 * s->LineTime) - *p->Tno_bw,
					(p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) / (15 * s->LineTime));

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: vactive_sw_bw_l = %f\n", __func__, p->vactive_sw_bw_l);
	dml2_printf("DML::%s: vactive_sw_bw_c = %f\n", __func__, p->vactive_sw_bw_c);
	dml2_printf("DML::%s: per_pipe_vactive_sw_bw = %f\n", __func__, s->per_pipe_vactive_sw_bw);
#endif

	if (p->display_cfg->gpuvm_enable == true) {
		s->Tvm_oto = math_max3(
			*p->Tvm_trips,
			*p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw_oto,
			s->LineTime / 4.0);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: Tvm_oto max0 = %f\n", __func__, *p->Tvm_trips);
		dml2_printf("DML::%s: Tvm_oto max1 = %f\n", __func__, *p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw_oto);
		dml2_printf("DML::%s: Tvm_oto max2 = %f\n", __func__, s->LineTime / 4.0);
#endif
	} else {
		s->Tvm_oto = s->Tvm_trips_rounded;
	}

	if ((p->display_cfg->gpuvm_enable == true || p->setup_for_tdlut || dcc_mrq_enable)) {
		s->Tr0_oto = math_max3(
			*p->Tr0_trips,
			(p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) / s->prefetch_bw_oto,
			s->LineTime / 4.0);
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: Tr0_oto max0 = %f\n", __func__, *p->Tr0_trips);
		dml2_printf("DML::%s: Tr0_oto max1 = %f\n", __func__, (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) / s->prefetch_bw_oto);
		dml2_printf("DML::%s: Tr0_oto max2 = %f\n", __func__, s->LineTime / 4);
#endif
	} else
		s->Tr0_oto = s->LineTime / 4.0;

	s->Tvm_oto_lines = math_ceil2(4.0 * s->Tvm_oto / s->LineTime, 1) / 4.0;
	s->Tr0_oto_lines = math_ceil2(4.0 * s->Tr0_oto / s->LineTime, 1) / 4.0;
	s->dst_y_prefetch_oto = s->Tvm_oto_lines + 2 * s->Tr0_oto_lines + s->Lsw_oto;

#ifdef DML_GLOBAL_PREFETCH_CHECK
	dml2_printf("DML::%s: impacted_Tpre = %f\n", __func__, p->impacted_dst_y_pre);
	if (p->impacted_dst_y_pre > 0) {
		dml2_printf("DML::%s: dst_y_prefetch_oto = %f\n", __func__, s->dst_y_prefetch_oto);
		s->dst_y_prefetch_oto = math_max2(s->dst_y_prefetch_oto, p->impacted_dst_y_pre);
		dml2_printf("DML::%s: dst_y_prefetch_oto = %f (impacted)\n", __func__, s->dst_y_prefetch_oto);
	}
#endif
	*p->Tpre_oto = s->dst_y_prefetch_oto * s->LineTime;

	//To (time for delay after scaler) in line time
	Lo = (unsigned int)(*p->DSTYAfterScaler + (double)*p->DSTXAfterScaler / (double)p->myPipe->HTotal);

	s->min_Lsw_equ = math_max2(p->PrefetchSourceLinesY, p->PrefetchSourceLinesC) / __DML2_CALCS_MAX_VRATIO_PRE_EQU__;
	s->min_Lsw_equ = math_max2(s->min_Lsw_equ, p->tdlut_drain_time / s->LineTime);
	s->min_Lsw_equ = math_max2(s->min_Lsw_equ, 2.0);
	//Tpre_equ in line time
	if (p->DynamicMetadataVMEnabled && p->DynamicMetadataEnable)
		s->dst_y_prefetch_equ = p->VStartup - (*p->TSetup + math_max2(p->TCalc, *p->Tvm_trips) + s->TWait_p) / s->LineTime - Lo;
	else
		s->dst_y_prefetch_equ = p->VStartup - (*p->TSetup + math_max2(p->TCalc, p->ExtraLatencyPrefetch) + s->TWait_p) / s->LineTime - Lo;

#ifdef DML_GLOBAL_PREFETCH_CHECK
	s->dst_y_prefetch_equ_impacted = math_max2(p->impacted_dst_y_pre, s->dst_y_prefetch_equ);

	s->dst_y_prefetch_equ_impacted = math_min2(s->dst_y_prefetch_equ_impacted, 63.75); // limit to the reg limit of U6.2 for DST_Y_PREFETCH

	if (s->dst_y_prefetch_equ_impacted > s->dst_y_prefetch_equ)
		s->dst_y_prefetch_equ -= s->dst_y_prefetch_equ_impacted - s->dst_y_prefetch_equ;
#endif

	s->dst_y_prefetch_equ = math_min2(s->dst_y_prefetch_equ, 63.75); // limit to the reg limit of U6.2 for DST_Y_PREFETCH

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: HTotal = %u\n", __func__, p->myPipe->HTotal);
	dml2_printf("DML::%s: min_Lsw_oto = %f\n", __func__, s->min_Lsw_oto);
	dml2_printf("DML::%s: min_Lsw_equ = %f\n", __func__, s->min_Lsw_equ);
	dml2_printf("DML::%s: Tno_bw = %f\n", __func__, *p->Tno_bw);
	dml2_printf("DML::%s: Tno_bw_flip = %f\n", __func__, *p->Tno_bw_flip);
	dml2_printf("DML::%s: ExtraLatencyPrefetch = %f\n", __func__, p->ExtraLatencyPrefetch);
	dml2_printf("DML::%s: trip_to_mem = %f\n", __func__, s->trip_to_mem);
	dml2_printf("DML::%s: mall_prefetch_sdp_overhead_factor = %f\n", __func__, p->mall_prefetch_sdp_overhead_factor);
	dml2_printf("DML::%s: BytePerPixelY = %u\n", __func__, p->myPipe->BytePerPixelY);
	dml2_printf("DML::%s: PrefetchSourceLinesY = %f\n", __func__, p->PrefetchSourceLinesY);
	dml2_printf("DML::%s: swath_width_luma_ub = %u\n", __func__, p->swath_width_luma_ub);
	dml2_printf("DML::%s: BytePerPixelC = %u\n", __func__, p->myPipe->BytePerPixelC);
	dml2_printf("DML::%s: PrefetchSourceLinesC = %f\n", __func__, p->PrefetchSourceLinesC);
	dml2_printf("DML::%s: swath_width_chroma_ub = %u\n", __func__, p->swath_width_chroma_ub);
	dml2_printf("DML::%s: prefetch_sw_bytes = %f\n", __func__, *p->prefetch_sw_bytes);
	dml2_printf("DML::%s: max_Tsw = %f\n", __func__, s->max_Tsw);
	dml2_printf("DML::%s: bytes_pp = %f\n", __func__, s->bytes_pp);
	dml2_printf("DML::%s: vm_bytes = %u\n", __func__, vm_bytes);
	dml2_printf("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, p->PixelPTEBytesPerRow);
	dml2_printf("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, p->HostVMInefficiencyFactor);
	dml2_printf("DML::%s: Tvm_trips = %f\n", __func__, *p->Tvm_trips);
	dml2_printf("DML::%s: Tr0_trips = %f\n", __func__, *p->Tr0_trips);
	dml2_printf("DML::%s: Tvm_trips_flip = %f\n", __func__, *p->Tvm_trips_flip);
	dml2_printf("DML::%s: Tr0_trips_flip = %f\n", __func__, *p->Tr0_trips_flip);
	dml2_printf("DML::%s: prefetch_bw_pr = %f\n", __func__, s->prefetch_bw_pr);
	dml2_printf("DML::%s: prefetch_bw_oto = %f\n", __func__, s->prefetch_bw_oto);
	dml2_printf("DML::%s: Tr0_oto = %f\n", __func__, s->Tr0_oto);
	dml2_printf("DML::%s: Tvm_oto = %f\n", __func__, s->Tvm_oto);
	dml2_printf("DML::%s: Tvm_oto_lines = %f\n", __func__, s->Tvm_oto_lines);
	dml2_printf("DML::%s: Tr0_oto_lines = %f\n", __func__, s->Tr0_oto_lines);
	dml2_printf("DML::%s: Lsw_oto = %f\n", __func__, s->Lsw_oto);
	dml2_printf("DML::%s: dst_y_prefetch_oto = %f\n", __func__, s->dst_y_prefetch_oto);
	dml2_printf("DML::%s: dst_y_prefetch_equ = %f\n", __func__, s->dst_y_prefetch_equ);
	dml2_printf("DML::%s: tdlut_row_bytes = %d\n", __func__, tdlut_row_bytes);
	dml2_printf("DML::%s: meta_row_bytes = %d\n", __func__, p->meta_row_bytes);
#endif
	double Tpre = s->dst_y_prefetch_equ * s->LineTime;
	s->dst_y_prefetch_equ = math_floor2(4.0 * (s->dst_y_prefetch_equ + 0.125), 1) / 4.0;
	*p->Tpre_rounded = s->dst_y_prefetch_equ * s->LineTime;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: dst_y_prefetch_equ: %f (after round)\n", __func__, s->dst_y_prefetch_equ);
	dml2_printf("DML::%s: LineTime: %f\n", __func__, s->LineTime);
	dml2_printf("DML::%s: VStartup: %u\n", __func__, p->VStartup);
	dml2_printf("DML::%s: Tvstartup: %fus - time between vstartup and first pixel of active\n", __func__, p->VStartup * s->LineTime);
	dml2_printf("DML::%s: TSetup: %fus - time from vstartup to vready\n", __func__, *p->TSetup);
	dml2_printf("DML::%s: TCalc: %fus - time for calculations in dchub starting at vready\n", __func__, p->TCalc);
	dml2_printf("DML::%s: TWait: %fus - time for fabric to become ready max(pstate exit,cstate enter/exit, urgent latency) after TCalc\n", __func__, p->TWait);
	dml2_printf("DML::%s: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n", __func__, s->Tdmbf);
	dml2_printf("DML::%s: Tdmec: %fus - time dio takes to transfer dmd\n", __func__, s->Tdmec);
	dml2_printf("DML::%s: Tdmsks: %fus - time before active dmd must complete transmission at dio\n", __func__, s->Tdmsks);
	dml2_printf("DML::%s: TWait = %f\n", __func__, p->TWait);
	dml2_printf("DML::%s: TWait_p = %f\n", __func__, s->TWait_p);
	dml2_printf("DML::%s: Ttrip = %f\n", __func__, p->Ttrip);
	dml2_printf("DML::%s: Tex = %f\n", __func__, p->ExtraLatencyPrefetch);
	dml2_printf("DML::%s: Tdmdl_vm: %fus - time for vm stages of dmd \n", __func__, *p->Tdmdl_vm);
	dml2_printf("DML::%s: Tdmdl: %fus - time for fabric to become ready and fetch dmd \n", __func__, *p->Tdmdl);
	dml2_printf("DML::%s: TWait_p: %fus\n", __func__, s->TWait_p);
	dml2_printf("DML::%s: Ttrip: %fus\n", __func__, p->Ttrip);
	dml2_printf("DML::%s: DSTXAfterScaler: %u pixels - number of pixel clocks pipeline and buffer delay after scaler \n", __func__, *p->DSTXAfterScaler);
	dml2_printf("DML::%s: DSTYAfterScaler: %u lines - number of lines of pipeline and buffer delay after scaler \n", __func__, *p->DSTYAfterScaler);
	dml2_printf("DML::%s: vm_bytes: %f (hvm inefficiency scaled)\n", __func__, vm_bytes*p->HostVMInefficiencyFactor);
	dml2_printf("DML::%s: row_bytes: %f (hvm inefficiency scaled, 1 row)\n", __func__, p->PixelPTEBytesPerRow*p->HostVMInefficiencyFactor+p->meta_row_bytes+tdlut_row_bytes);
	dml2_printf("DML::%s: Tno_bw: %f\n", __func__, *p->Tno_bw);
	dml2_printf("DML::%s: Tpre=%f Tpre_rounded: %f, delta=%f\n", __func__, Tpre, *p->Tpre_rounded, (*p->Tpre_rounded - Tpre));
	dml2_printf("DML::%s: Tvm_trips=%f Tvm_trips_rounded: %f, delta=%f\n", __func__, *p->Tvm_trips, s->Tvm_trips_rounded, (s->Tvm_trips_rounded - *p->Tvm_trips));
#endif

	*p->dst_y_per_vm_vblank = 0;
	*p->dst_y_per_row_vblank = 0;
	*p->VRatioPrefetchY = 0;
	*p->VRatioPrefetchC = 0;
	*p->RequiredPrefetchPixelDataBWLuma = 0;

	// Derive bandwidth by finding how much data to move within the time constraint
	// Tpre_rounded is Tpre rounding to 2-bit fraction
	// Tvm_trips_rounded is Tvm_trips ceiling to 1/4 line time
	// Tr0_trips_rounded is Tr0_trips ceiling to 1/4 line time
	// So that means prefetch bw calculated can be higher since the total time available for prefetch is less
	bool min_Lsw_equ_ok = *p->Tpre_rounded >= s->Tvm_trips_rounded + 2.0*s->Tr0_trips_rounded + s->min_Lsw_equ*s->LineTime;
	bool tpre_gt_req_latency = true;
#if 0
	// Check that Tpre_rounded is big enough if all of the stages of the prefetch are time constrained.
	// The terms Tvm_trips_rounded and Tr0_trips_rounded represent the min time constraints for the VM and row stages.
	// Normally, these terms cover the overall time constraint for Tpre >= (Tex + max{Ttrip, Turg}), but if these terms are at their minimum, an explicit check is necessary.
	tpre_gt_req_latency = *p->Tpre_rounded > (math_max2(p->Turg, s->trip_to_mem) + p->ExtraLatencyPrefetch);
#endif

	if (s->dst_y_prefetch_equ > 1 && min_Lsw_equ_ok && tpre_gt_req_latency) {
		s->prefetch_bw1 = 0.;
		s->prefetch_bw2 = 0.;
		s->prefetch_bw3 = 0.;
		s->prefetch_bw4 = 0.;

		// prefetch_bw1: VM + 2*R0 + SW
		if (*p->Tpre_rounded - *p->Tno_bw > 0) {
			s->prefetch_bw1 = (vm_bytes * p->HostVMInefficiencyFactor
				+ 2 * (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes)
				+ *p->prefetch_sw_bytes)
				/ (*p->Tpre_rounded - *p->Tno_bw);
			s->Tsw_est1 = *p->prefetch_sw_bytes / s->prefetch_bw1;
		} else
			s->prefetch_bw1 = 0;

		dml2_printf("DML::%s: prefetch_bw1: %f\n", __func__, s->prefetch_bw1);
		if ((s->Tsw_est1 < s->min_Lsw_equ * s->LineTime) && (*p->Tpre_rounded - s->min_Lsw_equ * s->LineTime - 0.75 * s->LineTime - *p->Tno_bw > 0)) {
			s->prefetch_bw1 = (vm_bytes * p->HostVMInefficiencyFactor + 2 * (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes)) /
				(*p->Tpre_rounded - s->min_Lsw_equ * s->LineTime - 0.75 * s->LineTime - *p->Tno_bw);
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: vm and 2 rows bytes = %f\n", __func__, (vm_bytes * p->HostVMInefficiencyFactor + 2 * (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes)));
			dml2_printf("DML::%s: Tpre_rounded = %f\n", __func__, *p->Tpre_rounded);
			dml2_printf("DML::%s: minus term = %f\n", __func__, s->min_Lsw_equ * s->LineTime + 0.75 * s->LineTime + *p->Tno_bw);
			dml2_printf("DML::%s: min_Lsw_equ = %f\n", __func__, s->min_Lsw_equ);
			dml2_printf("DML::%s: LineTime = %f\n", __func__, s->LineTime);
			dml2_printf("DML::%s: Tno_bw = %f\n", __func__, *p->Tno_bw);
			dml2_printf("DML::%s: Time to fetch vm and 2 rows = %f\n", __func__, (*p->Tpre_rounded - s->min_Lsw_equ * s->LineTime - 0.75 * s->LineTime - *p->Tno_bw));
			dml2_printf("DML::%s: prefetch_bw1: %f (updated)\n", __func__, s->prefetch_bw1);
#endif
		}

		// prefetch_bw2: VM + SW
		if (*p->Tpre_rounded - *p->Tno_bw - 2.0 * s->Tr0_trips_rounded > 0) {
			s->prefetch_bw2 = (vm_bytes * p->HostVMInefficiencyFactor + *p->prefetch_sw_bytes) /
			(*p->Tpre_rounded - *p->Tno_bw - 2.0 * s->Tr0_trips_rounded);
			s->Tsw_est2 = *p->prefetch_sw_bytes / s->prefetch_bw2;
		} else
			s->prefetch_bw2 = 0;

		dml2_printf("DML::%s: prefetch_bw2: %f\n", __func__, s->prefetch_bw2);
		if ((s->Tsw_est2 < s->min_Lsw_equ * s->LineTime) && ((*p->Tpre_rounded - *p->Tno_bw - 2.0 * s->Tr0_trips_rounded - s->min_Lsw_equ * s->LineTime - 0.25 * s->LineTime) > 0)) {
			s->prefetch_bw2 = vm_bytes * p->HostVMInefficiencyFactor / (*p->Tpre_rounded - *p->Tno_bw - 2.0 * s->Tr0_trips_rounded - s->min_Lsw_equ * s->LineTime - 0.25 * s->LineTime);
			dml2_printf("DML::%s: prefetch_bw2: %f (updated)\n", __func__, s->prefetch_bw2);
		}

		// prefetch_bw3: 2*R0 + SW
		if (*p->Tpre_rounded - s->Tvm_trips_rounded > 0) {
			s->prefetch_bw3 = (2 * (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) + *p->prefetch_sw_bytes) /
				(*p->Tpre_rounded - s->Tvm_trips_rounded);
			s->Tsw_est3 = *p->prefetch_sw_bytes / s->prefetch_bw3;
		} else
			s->prefetch_bw3 = 0;

		dml2_printf("DML::%s: prefetch_bw3: %f\n", __func__, s->prefetch_bw3);
		if ((s->Tsw_est3 < s->min_Lsw_equ * s->LineTime) && ((*p->Tpre_rounded - s->min_Lsw_equ * s->LineTime - 0.5 * s->LineTime - s->Tvm_trips_rounded) > 0)) {
			s->prefetch_bw3 = (2 * (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes)) / (*p->Tpre_rounded - s->min_Lsw_equ * s->LineTime - 0.5 * s->LineTime - s->Tvm_trips_rounded);
			dml2_printf("DML::%s: prefetch_bw3: %f (updated)\n", __func__, s->prefetch_bw3);
		}

		// prefetch_bw4: SW
		if (*p->Tpre_rounded - s->Tvm_trips_rounded - 2 * s->Tr0_trips_rounded > 0)
			s->prefetch_bw4 = *p->prefetch_sw_bytes / (*p->Tpre_rounded - s->Tvm_trips_rounded - 2 * s->Tr0_trips_rounded);
		else
			s->prefetch_bw4 = 0;

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: Tno_bw: %f\n", __func__, *p->Tno_bw);
		dml2_printf("DML::%s: Tpre=%f Tpre_rounded: %f, delta=%f\n", __func__, Tpre, *p->Tpre_rounded, (*p->Tpre_rounded - Tpre));
		dml2_printf("DML::%s: Tvm_trips=%f Tvm_trips_rounded: %f, delta=%f\n", __func__, *p->Tvm_trips, s->Tvm_trips_rounded, (s->Tvm_trips_rounded - *p->Tvm_trips));
		dml2_printf("DML::%s: Tr0_trips=%f Tr0_trips_rounded: %f, delta=%f\n", __func__, *p->Tr0_trips, s->Tr0_trips_rounded, (s->Tr0_trips_rounded - *p->Tr0_trips));
		dml2_printf("DML::%s: Tsw_est1: %f\n", __func__, s->Tsw_est1);
		dml2_printf("DML::%s: Tsw_est2: %f\n", __func__, s->Tsw_est2);
		dml2_printf("DML::%s: Tsw_est3: %f\n", __func__, s->Tsw_est3);
		dml2_printf("DML::%s: prefetch_bw1: %f (final)\n", __func__, s->prefetch_bw1);
		dml2_printf("DML::%s: prefetch_bw2: %f (final)\n", __func__, s->prefetch_bw2);
		dml2_printf("DML::%s: prefetch_bw3: %f (final)\n", __func__, s->prefetch_bw3);
		dml2_printf("DML::%s: prefetch_bw4: %f (final)\n", __func__, s->prefetch_bw4);
#endif
		{
			bool Case1OK = false;
			bool Case2OK = false;
			bool Case3OK = false;

			// get "equalized" bw among all stages (vm, r0, sw), so based is all 3 stages are just above the latency-based requirement
			// so it is not too dis-portionally favor a particular stage, next is either r0 more agressive and next is vm more agressive, the worst is all are agressive
			// vs the latency based number

			// prefetch_bw1: VM + 2*R0 + SW
			// so prefetch_bw1 will have enough bw to transfer the necessary data within Tpre_rounded - Tno_bw (Tpre is the the worst-case latency based time to fetch the data)
			// here is to make sure equ bw wont be more agressive than the latency-based requirement.
			// check vm time >= vm_trips
			// check r0 time >= r0_trips

			double total_row_bytes = (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes);

			dml2_printf("DML::%s: Tvm_trips_rounded = %f\n", __func__, s->Tvm_trips_rounded);
			dml2_printf("DML::%s: Tr0_trips_rounded = %f\n", __func__, s->Tr0_trips_rounded);

			if (s->prefetch_bw1 > 0) {
				double vm_transfer_time = *p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw1;
				double row_transfer_time = total_row_bytes / s->prefetch_bw1;
				dml2_printf("DML::%s: Case1: vm_transfer_time  = %f\n", __func__, vm_transfer_time);
				dml2_printf("DML::%s: Case1: row_transfer_time = %f\n", __func__, row_transfer_time);
				if (vm_transfer_time >= s->Tvm_trips_rounded && row_transfer_time >= s->Tr0_trips_rounded) {
					Case1OK = true;
				}
			}

			// prefetch_bw2: VM + SW
			// prefetch_bw2 will be enough bw to transfer VM and SW data within (Tpre_rounded - Tr0_trips_rounded - Tno_bw)
			// check vm time >= vm_trips
			// check r0 time < r0_trips
			if (s->prefetch_bw2 > 0) {
				double vm_transfer_time = *p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw2;
				double row_transfer_time = total_row_bytes / s->prefetch_bw2;
				dml2_printf("DML::%s: Case2: vm_transfer_time  = %f\n", __func__, vm_transfer_time);
				dml2_printf("DML::%s: Case2: row_transfer_time = %f\n", __func__, row_transfer_time);
				if (vm_transfer_time >= s->Tvm_trips_rounded && row_transfer_time < s->Tr0_trips_rounded) {
					Case2OK = true;
				}
			}

			// prefetch_bw3: VM + 2*R0
			// check vm time < vm_trips
			// check r0 time >= r0_trips
			if (s->prefetch_bw3 > 0) {
				double vm_transfer_time = *p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw3;
				double row_transfer_time = total_row_bytes / s->prefetch_bw3;
				dml2_printf("DML::%s: Case3: vm_transfer_time  = %f\n", __func__, vm_transfer_time);
				dml2_printf("DML::%s: Case3: row_transfer_time = %f\n", __func__, row_transfer_time);
				if (vm_transfer_time < s->Tvm_trips_rounded && row_transfer_time >= s->Tr0_trips_rounded) {
					Case3OK = true;
				}
			}

			if (Case1OK) {
				s->prefetch_bw_equ = s->prefetch_bw1;
			} else if (Case2OK) {
				s->prefetch_bw_equ = s->prefetch_bw2;
			} else if (Case3OK) {
				s->prefetch_bw_equ = s->prefetch_bw3;
			} else {
				s->prefetch_bw_equ = s->prefetch_bw4;
			}

			s->prefetch_bw_equ = math_max3(s->prefetch_bw_equ,
							p->vm_bytes * p->HostVMInefficiencyFactor / (31 * s->LineTime) - *p->Tno_bw,
							(p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) / (15 * s->LineTime));
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: Case1OK: %u\n", __func__, Case1OK);
			dml2_printf("DML::%s: Case2OK: %u\n", __func__, Case2OK);
			dml2_printf("DML::%s: Case3OK: %u\n", __func__, Case3OK);
			dml2_printf("DML::%s: prefetch_bw_equ: %f\n", __func__, s->prefetch_bw_equ);
#endif

			if (s->prefetch_bw_equ > 0) {
				if (p->display_cfg->gpuvm_enable == true) {
					s->Tvm_equ = math_max3(*p->Tno_bw + vm_bytes * p->HostVMInefficiencyFactor / s->prefetch_bw_equ, *p->Tvm_trips, s->LineTime / 4);
				} else {
					s->Tvm_equ = s->LineTime / 4;
				}

				if (p->display_cfg->gpuvm_enable == true || dcc_mrq_enable || p->setup_for_tdlut) {
					s->Tr0_equ = math_max3((p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + p->meta_row_bytes + tdlut_row_bytes) / s->prefetch_bw_equ, // PixelPTEBytesPerRow is dpte_row_bytes
						*p->Tr0_trips,
						s->LineTime / 4);
				} else {
					s->Tr0_equ = s->LineTime / 4;
				}
			} else {
				s->Tvm_equ = 0;
				s->Tr0_equ = 0;
				dml2_printf("DML::%s: prefetch_bw_equ equals 0!\n", __func__);
			}
		}
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: Tvm_equ = %f\n", __func__, s->Tvm_equ);
		dml2_printf("DML::%s: Tr0_equ = %f\n", __func__, s->Tr0_equ);
#endif
		// Use the more stressful prefetch schedule
		if (s->dst_y_prefetch_oto < s->dst_y_prefetch_equ) {
			*p->dst_y_prefetch = s->dst_y_prefetch_oto;
			s->TimeForFetchingVM = s->Tvm_oto;
			s->TimeForFetchingRowInVBlank = s->Tr0_oto;

			*p->dst_y_per_vm_vblank = math_ceil2(4.0 * s->TimeForFetchingVM / s->LineTime, 1.0) / 4.0;
			*p->dst_y_per_row_vblank = math_ceil2(4.0 * s->TimeForFetchingRowInVBlank / s->LineTime, 1.0) / 4.0;
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: Using oto scheduling for prefetch\n", __func__);
#endif

		} else {
			*p->dst_y_prefetch = s->dst_y_prefetch_equ;

			if (s->dst_y_prefetch_equ < s->dst_y_prefetch_equ_impacted)
				*p->dst_y_prefetch = s->dst_y_prefetch_equ_impacted;

			s->TimeForFetchingVM = s->Tvm_equ;
			s->TimeForFetchingRowInVBlank = s->Tr0_equ;

		*p->dst_y_per_vm_vblank = math_ceil2(4.0 * s->TimeForFetchingVM / s->LineTime, 1.0) / 4.0;
		*p->dst_y_per_row_vblank = math_ceil2(4.0 * s->TimeForFetchingRowInVBlank / s->LineTime, 1.0) / 4.0;

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: Using equ bw scheduling for prefetch\n", __func__);
#endif
		}

		// Lsw = dst_y_prefetch - (dst_y_per_vm_vblank + 2*dst_y_per_row_vblank)
		s->LinesToRequestPrefetchPixelData = *p->dst_y_prefetch - *p->dst_y_per_vm_vblank - 2 * *p->dst_y_per_row_vblank; // Lsw

		s->cursor_prefetch_bytes = (unsigned int)math_max2(p->cursor_bytes_per_chunk, 4 * p->cursor_bytes_per_line);
		*p->prefetch_cursor_bw = p->num_cursors * s->cursor_prefetch_bytes / (s->LinesToRequestPrefetchPixelData * s->LineTime);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: TimeForFetchingVM = %f\n", __func__, s->TimeForFetchingVM);
		dml2_printf("DML::%s: TimeForFetchingRowInVBlank = %f\n", __func__, s->TimeForFetchingRowInVBlank);
		dml2_printf("DML::%s: LineTime = %f\n", __func__, s->LineTime);
		dml2_printf("DML::%s: dst_y_prefetch = %f\n", __func__, *p->dst_y_prefetch);
		dml2_printf("DML::%s: dst_y_per_vm_vblank = %f\n", __func__, *p->dst_y_per_vm_vblank);
		dml2_printf("DML::%s: dst_y_per_row_vblank = %f\n", __func__, *p->dst_y_per_row_vblank);
		dml2_printf("DML::%s: LinesToRequestPrefetchPixelData = %f\n", __func__, s->LinesToRequestPrefetchPixelData);
		dml2_printf("DML::%s: PrefetchSourceLinesY = %f\n", __func__, p->PrefetchSourceLinesY);

		dml2_printf("DML::%s: cursor_bytes_per_chunk = %d\n", __func__, p->cursor_bytes_per_chunk);
		dml2_printf("DML::%s: cursor_bytes_per_line = %d\n", __func__, p->cursor_bytes_per_line);
		dml2_printf("DML::%s: cursor_prefetch_bytes = %d\n", __func__, s->cursor_prefetch_bytes);
		dml2_printf("DML::%s: prefetch_cursor_bw = %f\n", __func__, *p->prefetch_cursor_bw);
#endif
		dml2_assert(*p->dst_y_prefetch < 64);

		unsigned int min_lsw_required = (unsigned int)math_max2(2, p->tdlut_drain_time / s->LineTime);
		if (s->LinesToRequestPrefetchPixelData >= min_lsw_required && s->prefetch_bw_equ > 0) {
			*p->VRatioPrefetchY = (double)p->PrefetchSourceLinesY / s->LinesToRequestPrefetchPixelData;
			*p->VRatioPrefetchY = math_max2(*p->VRatioPrefetchY, 1.0);
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: VRatioPrefetchY = %f\n", __func__, *p->VRatioPrefetchY);
			dml2_printf("DML::%s: SwathHeightY = %u\n", __func__, p->SwathHeightY);
			dml2_printf("DML::%s: VInitPreFillY = %u\n", __func__, p->VInitPreFillY);
#endif
			if ((p->SwathHeightY > 4) && (p->VInitPreFillY > 3)) {
				if (s->LinesToRequestPrefetchPixelData > (p->VInitPreFillY - 3.0) / 2.0) {
					*p->VRatioPrefetchY = math_max2(*p->VRatioPrefetchY,
						(double)p->MaxNumSwathY * p->SwathHeightY / (s->LinesToRequestPrefetchPixelData - (p->VInitPreFillY - 3.0) / 2.0));
				} else {
					s->NoTimeToPrefetch = true;
					dml2_printf("DML::%s: No time to prefetch!. LinesToRequestPrefetchPixelData=%f VinitPreFillY=%u\n", __func__, s->LinesToRequestPrefetchPixelData, p->VInitPreFillY);
					*p->VRatioPrefetchY = 0;
				}
#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: VRatioPrefetchY = %f\n", __func__, *p->VRatioPrefetchY);
				dml2_printf("DML::%s: PrefetchSourceLinesY = %f\n", __func__, p->PrefetchSourceLinesY);
				dml2_printf("DML::%s: MaxNumSwathY = %u\n", __func__, p->MaxNumSwathY);
#endif
			}

			*p->VRatioPrefetchC = (double)p->PrefetchSourceLinesC / s->LinesToRequestPrefetchPixelData;
			*p->VRatioPrefetchC = math_max2(*p->VRatioPrefetchC, 1.0);

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: VRatioPrefetchC = %f\n", __func__, *p->VRatioPrefetchC);
			dml2_printf("DML::%s: SwathHeightC = %u\n", __func__, p->SwathHeightC);
			dml2_printf("DML::%s: VInitPreFillC = %u\n", __func__, p->VInitPreFillC);
#endif
			if ((p->SwathHeightC > 4) && (p->VInitPreFillC > 3)) {
				if (s->LinesToRequestPrefetchPixelData > (p->VInitPreFillC - 3.0) / 2.0) {
					*p->VRatioPrefetchC = math_max2(*p->VRatioPrefetchC, (double)p->MaxNumSwathC * p->SwathHeightC / (s->LinesToRequestPrefetchPixelData - (p->VInitPreFillC - 3.0) / 2.0));
				} else {
					s->NoTimeToPrefetch = true;
					dml2_printf("DML::%s: No time to prefetch!. LinesToRequestPrefetchPixelData=%f VInitPreFillC=%u\n", __func__, s->LinesToRequestPrefetchPixelData, p->VInitPreFillC);
					*p->VRatioPrefetchC = 0;
				}
#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: VRatioPrefetchC = %f\n", __func__, *p->VRatioPrefetchC);
				dml2_printf("DML::%s: PrefetchSourceLinesC = %f\n", __func__, p->PrefetchSourceLinesC);
				dml2_printf("DML::%s: MaxNumSwathC = %u\n", __func__, p->MaxNumSwathC);
#endif
			}

			*p->RequiredPrefetchPixelDataBWLuma = (double)p->PrefetchSourceLinesY / s->LinesToRequestPrefetchPixelData * p->myPipe->BytePerPixelY * p->swath_width_luma_ub / s->LineTime;
			*p->RequiredPrefetchPixelDataBWChroma = (double)p->PrefetchSourceLinesC / s->LinesToRequestPrefetchPixelData * p->myPipe->BytePerPixelC * p->swath_width_chroma_ub / s->LineTime;

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: BytePerPixelY = %u\n", __func__, p->myPipe->BytePerPixelY);
			dml2_printf("DML::%s: swath_width_luma_ub = %u\n", __func__, p->swath_width_luma_ub);
			dml2_printf("DML::%s: LineTime = %f\n", __func__, s->LineTime);
			dml2_printf("DML::%s: RequiredPrefetchPixelDataBWLuma = %f\n", __func__, *p->RequiredPrefetchPixelDataBWLuma);
			dml2_printf("DML::%s: RequiredPrefetchPixelDataBWChroma = %f\n", __func__, *p->RequiredPrefetchPixelDataBWChroma);
#endif
		} else {
			s->NoTimeToPrefetch = true;
			dml2_printf("DML::%s: No time to prefetch!, LinesToRequestPrefetchPixelData: %f, should be >= %d\n", __func__, s->LinesToRequestPrefetchPixelData, min_lsw_required);
			dml2_printf("DML::%s: No time to prefetch!, prefetch_bw_equ: %f, should be > 0\n", __func__, s->prefetch_bw_equ);
			*p->VRatioPrefetchY = 0;
			*p->VRatioPrefetchC = 0;
			*p->RequiredPrefetchPixelDataBWLuma = 0;
			*p->RequiredPrefetchPixelDataBWChroma = 0;
		}
		dml2_printf("DML: Tpre: %fus - sum of time to request 2 x data pte, swaths\n", (double)s->LinesToRequestPrefetchPixelData * s->LineTime + 2.0 * s->TimeForFetchingRowInVBlank + s->TimeForFetchingVM);
		dml2_printf("DML: Tvm: %fus - time to fetch vm\n", s->TimeForFetchingVM);
		dml2_printf("DML: Tr0: %fus - time to fetch first row of data pagetables\n", s->TimeForFetchingRowInVBlank);
		dml2_printf("DML: Tsw: %fus = time to fetch enough pixel data and cursor data to feed the scalers init position and detile\n", (double)s->LinesToRequestPrefetchPixelData * s->LineTime);
		dml2_printf("DML: To: %fus - time for propagation from scaler to optc\n", (*p->DSTYAfterScaler + ((double)(*p->DSTXAfterScaler) / (double)p->myPipe->HTotal)) * s->LineTime);
		dml2_printf("DML: Tvstartup - TSetup - Tcalc - TWait - Tpre - To > 0\n");
		dml2_printf("DML: Tslack(pre): %fus - time left over in schedule\n", p->VStartup * s->LineTime - s->TimeForFetchingVM - 2 * s->TimeForFetchingRowInVBlank - (*p->DSTYAfterScaler + ((double)(*p->DSTXAfterScaler) / (double)p->myPipe->HTotal)) * s->LineTime - p->TWait - p->TCalc - *p->TSetup);
		dml2_printf("DML: row_bytes = dpte_row_bytes (per_pipe) = PixelPTEBytesPerRow = : %u\n", p->PixelPTEBytesPerRow);

	} else {
		dml2_printf("DML::%s: No time to prefetch! dst_y_prefetch_equ = %f (should be > 1)\n", __func__, s->dst_y_prefetch_equ);
		dml2_printf("DML::%s: No time to prefetch! min_Lsw_equ_ok = %d, Tpre_rounded (%f) should be >= Tvm_trips_rounded (%f) + 2.0*Tr0_trips_rounded (%f) + min_Tsw_equ (%f)\n",
				__func__, min_Lsw_equ_ok, *p->Tpre_rounded, s->Tvm_trips_rounded, 2.0*s->Tr0_trips_rounded, s->min_Lsw_equ*s->LineTime);
		dml2_printf("DML::%s: No time to prefetch! min_Lsw_equ_ok = %d, Tpre_rounded+Tvm_trips_rounded+2.0*Tr0_trips_rounded+min_Tsw_equ (%f) should be > \n",
				__func__, tpre_gt_req_latency, (s->min_Lsw_equ*s->LineTime + s->Tvm_trips_rounded + 2.0*s->Tr0_trips_rounded), p->Turg, s->trip_to_mem, p->ExtraLatencyPrefetch);
		s->NoTimeToPrefetch = true;
		s->TimeForFetchingVM = 0;
		s->TimeForFetchingRowInVBlank = 0;
		*p->dst_y_per_vm_vblank = 0;
		*p->dst_y_per_row_vblank = 0;
		s->LinesToRequestPrefetchPixelData = 0;
		*p->VRatioPrefetchY = 0;
		*p->VRatioPrefetchC = 0;
		*p->RequiredPrefetchPixelDataBWLuma = 0;
		*p->RequiredPrefetchPixelDataBWChroma = 0;
	}

	{
		double prefetch_vm_bw;
		double prefetch_row_bw;

		if (vm_bytes == 0) {
			prefetch_vm_bw = 0;
		} else if (*p->dst_y_per_vm_vblank > 0) {
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, p->HostVMInefficiencyFactor);
			dml2_printf("DML::%s: dst_y_per_vm_vblank = %f\n", __func__, *p->dst_y_per_vm_vblank);
			dml2_printf("DML::%s: LineTime = %f\n", __func__, s->LineTime);
#endif
			prefetch_vm_bw = vm_bytes * p->HostVMInefficiencyFactor / (*p->dst_y_per_vm_vblank * s->LineTime);
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: prefetch_vm_bw = %f\n", __func__, prefetch_vm_bw);
#endif
		} else {
			prefetch_vm_bw = 0;
			s->NoTimeToPrefetch = true;
			dml2_printf("DML::%s: No time to prefetch!. dst_y_per_vm_vblank=%f (should be > 0)\n", __func__, *p->dst_y_per_vm_vblank);
		}

		if (p->PixelPTEBytesPerRow == 0 && tdlut_row_bytes == 0) {
			prefetch_row_bw = 0;
		} else if (*p->dst_y_per_row_vblank > 0) {
			prefetch_row_bw = (p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + tdlut_row_bytes) / (*p->dst_y_per_row_vblank * s->LineTime);

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, p->PixelPTEBytesPerRow);
			dml2_printf("DML::%s: dst_y_per_row_vblank = %f\n", __func__, *p->dst_y_per_row_vblank);
			dml2_printf("DML::%s: prefetch_row_bw = %f\n", __func__, prefetch_row_bw);
#endif
		} else {
			prefetch_row_bw = 0;
			s->NoTimeToPrefetch = true;
			dml2_printf("DML::%s: No time to prefetch!. dst_y_per_row_vblank=%f (should be > 0)\n", __func__, *p->dst_y_per_row_vblank);
		}

		*p->prefetch_vmrow_bw = math_max2(prefetch_vm_bw, prefetch_row_bw);
	}

	if (s->NoTimeToPrefetch) {
		s->TimeForFetchingVM = 0;
		s->TimeForFetchingRowInVBlank = 0;
		*p->dst_y_per_vm_vblank = 0;
		*p->dst_y_per_row_vblank = 0;
		*p->dst_y_prefetch = 0;
		s->LinesToRequestPrefetchPixelData = 0;
		*p->VRatioPrefetchY = 0;
		*p->VRatioPrefetchC = 0;
		*p->RequiredPrefetchPixelDataBWLuma = 0;
		*p->RequiredPrefetchPixelDataBWChroma = 0;
		*p->prefetch_vmrow_bw = 0;
	}

	dml2_printf("DML::%s: dst_y_per_vm_vblank = %f (final)\n", __func__, *p->dst_y_per_vm_vblank);
	dml2_printf("DML::%s: dst_y_per_row_vblank = %f (final)\n", __func__, *p->dst_y_per_row_vblank);
	dml2_printf("DML::%s: prefetch_vmrow_bw = %f (final)\n", __func__, *p->prefetch_vmrow_bw);
	dml2_printf("DML::%s: RequiredPrefetchPixelDataBWLuma = %f (final)\n", __func__, *p->RequiredPrefetchPixelDataBWLuma);
	dml2_printf("DML::%s: RequiredPrefetchPixelDataBWChroma = %f (final)\n", __func__, *p->RequiredPrefetchPixelDataBWChroma);
	dml2_printf("DML::%s: NoTimeToPrefetch=%d\n", __func__, s->NoTimeToPrefetch);

	return s->NoTimeToPrefetch;
}

static unsigned int get_num_lb_source_lines(unsigned int max_line_buffer_lines,
									unsigned int line_buffer_size_bits,
									unsigned int num_pipes,
									unsigned int vp_width,
									unsigned int vp_height,
									double h_ratio,
									enum dml2_rotation_angle rotation_angle)
{
	unsigned int num_lb_source_lines = 0;
	double lb_bit_per_pixel = 57.0;
	unsigned recin_width = vp_width/num_pipes;

	if (dml_is_vertical_rotation(rotation_angle))
		recin_width = vp_height/num_pipes;

	num_lb_source_lines = (unsigned int) math_min2((double) max_line_buffer_lines,
								math_floor2(line_buffer_size_bits / lb_bit_per_pixel / (recin_width / math_max2(h_ratio, 1.0)), 1.0));

	return num_lb_source_lines;
}

static unsigned int find_max_impact_plane(unsigned int this_plane_idx, unsigned int num_planes, unsigned int Trpd_dcfclk_cycles[])
{
	int max_value = -1;
	int max_idx = -1;
	for (unsigned int i = 0; i < num_planes; i++) {
		if (i != this_plane_idx && (int) Trpd_dcfclk_cycles[i] > max_value) {
			max_value = Trpd_dcfclk_cycles[i];
			max_idx = i;
		}
	}
	if (max_idx <= 0) {
		dml2_assert(max_idx >= 0);
		max_idx = this_plane_idx;
	}

	return max_idx;
}

static double calculate_impacted_Tsw(unsigned int exclude_plane_idx, unsigned int num_planes, double *prefetch_swath_bytes, double bw_mbps)
{
	double sum = 0.;
	for (unsigned int i = 0; i < num_planes; i++) {
		if (i != exclude_plane_idx) {
			sum += prefetch_swath_bytes[i];
		}
	}
	return sum / bw_mbps;
}

// a global check against the aggregate effect of the per plane prefetch schedule
static bool CheckGlobalPrefetchAdmissibility(struct dml2_core_internal_scratch *scratch,
											 struct dml2_core_calcs_CheckGlobalPrefetchAdmissibility_params *p)
{
	struct dml2_core_calcs_CheckGlobalPrefetchAdmissibility_locals *s = &scratch->CheckGlobalPrefetchAdmissibility_locals;
	unsigned int i, k;

	memset(s, 0, sizeof(struct dml2_core_calcs_CheckGlobalPrefetchAdmissibility_locals));

	*p->recalc_prefetch_schedule = 0;
	s->prefetch_global_check_passed = 1;
	// worst case if the rob and cdb is fully hogged
	s->max_Trpd_dcfclk_cycles = (unsigned int) math_ceil2((p->rob_buffer_size_kbytes*1024 + p->compressed_buffer_size_kbytes*DML_MAX_COMPRESSION_RATIO*1024)/64.0, 1.0);
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: num_active_planes = %d\n", __func__, p->num_active_planes);
	dml2_printf("DML::%s: rob_buffer_size_kbytes = %d\n", __func__, p->rob_buffer_size_kbytes);
	dml2_printf("DML::%s: compressed_buffer_size_kbytes = %d\n", __func__, p->compressed_buffer_size_kbytes);
	dml2_printf("DML::%s: estimated_urg_bandwidth_required_mbps = %f\n", __func__, p->estimated_urg_bandwidth_required_mbps);
	dml2_printf("DML::%s: estimated_dcfclk_mhz = %f\n", __func__, p->estimated_dcfclk_mhz);
	dml2_printf("DML::%s: max_Trpd_dcfclk_cycles = %u\n", __func__, s->max_Trpd_dcfclk_cycles);
#endif

	// calculate the return impact from each plane, request is 256B per dcfclk
	for (i = 0; i < p->num_active_planes; i++) {
		s->src_detile_buf_size_bytes_l[i] = p->detile_buffer_size_bytes_l[i];
		s->src_detile_buf_size_bytes_c[i] = p->detile_buffer_size_bytes_c[i];
		s->src_swath_bytes_l[i] = p->full_swath_bytes_l[i];
		s->src_swath_bytes_c[i] = p->full_swath_bytes_c[i];

		if (p->pixel_format[i] == dml2_420_10) {
			s->src_detile_buf_size_bytes_l[i] = (unsigned int) (s->src_detile_buf_size_bytes_l[i] * 1.5);
			s->src_detile_buf_size_bytes_c[i] = (unsigned int) (s->src_detile_buf_size_bytes_c[i] * 1.5);
			s->src_swath_bytes_l[i] = (unsigned int) (s->src_swath_bytes_l[i] * 1.5);
			s->src_swath_bytes_c[i] = (unsigned int) (s->src_swath_bytes_c[i] * 1.5);
		}

		s->burst_bytes_to_fill_det = (unsigned int) (math_floor2(s->src_detile_buf_size_bytes_l[i] / p->chunk_bytes_l, 1) * p->chunk_bytes_l);
		s->burst_bytes_to_fill_det += (unsigned int) (math_floor2(p->lb_source_lines_l[i] / p->swath_height_l[i], 1) * s->src_swath_bytes_l[i]);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: i=%u pixel_format = %d\n", __func__, i, p->pixel_format[i]);
		dml2_printf("DML::%s: i=%u chunk_bytes_l = %d\n", __func__, i, p->chunk_bytes_l);
		dml2_printf("DML::%s: i=%u lb_source_lines_l = %d\n", __func__, i, p->lb_source_lines_l[i]);
		dml2_printf("DML::%s: i=%u src_detile_buf_size_bytes_l=%d\n", __func__, i, s->src_detile_buf_size_bytes_l[i]);
		dml2_printf("DML::%s: i=%u src_swath_bytes_l=%d\n", __func__, i, s->src_swath_bytes_l[i]);
		dml2_printf("DML::%s: i=%u burst_bytes_to_fill_det=%d (luma)\n", __func__, i, s->burst_bytes_to_fill_det);
#endif

		if (s->src_swath_bytes_c[i] > 0) { // dual_plane
			s->burst_bytes_to_fill_det += (unsigned int) (math_floor2(s->src_detile_buf_size_bytes_c[i] / p->chunk_bytes_c, 1) * p->chunk_bytes_c);

			if (p->pixel_format[i] == dml2_422_planar_8 || p->pixel_format[i] == dml2_422_planar_10 || p->pixel_format[i] == dml2_422_planar_12) {
				s->burst_bytes_to_fill_det += (unsigned int) (math_floor2(p->lb_source_lines_c[i] / p->swath_height_c[i], 1) * s->src_swath_bytes_c[i]);
			}

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: i=%u chunk_bytes_c = %d\n", __func__, i, p->chunk_bytes_c);
			dml2_printf("DML::%s: i=%u lb_source_lines_c = %d\n", __func__, i, p->lb_source_lines_c[i]);
			dml2_printf("DML::%s: i=%u src_detile_buf_size_bytes_c=%d\n", __func__, i, s->src_detile_buf_size_bytes_c[i]);
			dml2_printf("DML::%s: i=%u src_swath_bytes_c=%d\n", __func__, i, s->src_swath_bytes_c[i]);
#endif
		}

		s->time_to_fill_det_us = (double) s->burst_bytes_to_fill_det / (256 * p->estimated_dcfclk_mhz); // fill time assume full burst at request rate
		s->accumulated_return_path_dcfclk_cycles[i] = (unsigned int) math_ceil2(((DML_MAX_COMPRESSION_RATIO-1) * 64 * p->estimated_dcfclk_mhz) * s->time_to_fill_det_us / 64.0, 1.0); //for 64B per DCFClk

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: i=%u burst_bytes_to_fill_det=%d\n", __func__, i, s->burst_bytes_to_fill_det);
		dml2_printf("DML::%s: i=%u time_to_fill_det_us=%f\n", __func__, i, s->time_to_fill_det_us);
		dml2_printf("DML::%s: i=%u accumulated_return_path_dcfclk_cycles=%u\n", __func__, i, s->accumulated_return_path_dcfclk_cycles[i]);
#endif
		// clamping to worst case delay which is one which occupy the full rob+cdb
		if (s->accumulated_return_path_dcfclk_cycles[i] > s->max_Trpd_dcfclk_cycles)
			s->accumulated_return_path_dcfclk_cycles[i] = s->max_Trpd_dcfclk_cycles;
	}

	// Figure out the impacted prefetch time for each plane
	// if impacted_Tre is > equ bw Tpre, we need to fail the prefetch schedule as we need a higher state to support the bw
	for (i = 0; i < p->num_active_planes; i++) {
		k = find_max_impact_plane(i, p->num_active_planes, s->accumulated_return_path_dcfclk_cycles); // plane k causes most impact to plane i
		// the rest of planes (except for k) complete for bw
		p->impacted_dst_y_pre[i] = s->accumulated_return_path_dcfclk_cycles[k]/p->estimated_dcfclk_mhz;
		p->impacted_dst_y_pre[i] += calculate_impacted_Tsw(k, p->num_active_planes, p->prefetch_sw_bytes, p->estimated_urg_bandwidth_required_mbps);
		p->impacted_dst_y_pre[i] = math_ceil2(p->impacted_dst_y_pre[i] / p->line_time[i], 0.25);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: i=%u impacted_Tpre=%f (k=%u)\n", __func__, i, p->impacted_dst_y_pre[i], k);
#endif
	}

	if (p->Tpre_rounded != NULL && p->Tpre_oto != NULL) {
		for (i = 0; i < p->num_active_planes; i++) {
			if (p->impacted_dst_y_pre[i] > p->dst_y_prefetch[i]) {
				s->prefetch_global_check_passed = 0;
				*p->recalc_prefetch_schedule = 1;
			}
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: i=%u Tpre_rounded=%f\n", __func__, i, p->Tpre_rounded[i]);
			dml2_printf("DML::%s: i=%u Tpre_oto=%f\n", __func__, i, p->Tpre_oto[i]);
#endif
		}
	} else {
		// likely a mode programming calls, assume support, and no recalc - not used anyways
		s->prefetch_global_check_passed = 1;
		*p->recalc_prefetch_schedule = 0;
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: prefetch_global_check_passed=%u\n", __func__, s->prefetch_global_check_passed);
	dml2_printf("DML::%s: recalc_prefetch_schedule=%u\n", __func__, *p->recalc_prefetch_schedule);
#endif

	return s->prefetch_global_check_passed;
}

static void calculate_peak_bandwidth_required(
	struct dml2_core_internal_scratch *s,
	struct dml2_core_calcs_calculate_peak_bandwidth_required_params *p)
{
	unsigned int n;
	unsigned int m;

	struct dml2_core_shared_calculate_peak_bandwidth_required_locals *l = &s->calculate_peak_bandwidth_required_locals;

	memset(l, 0, sizeof(struct dml2_core_shared_calculate_peak_bandwidth_required_locals));

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: inc_flip_bw = %d\n", __func__, p->inc_flip_bw);
	dml2_printf("DML::%s: NumberOfActiveSurfaces = %d\n", __func__, p->num_active_planes);
#endif

	for (unsigned int k = 0; k < p->num_active_planes; ++k) {
		l->unity_array[k] = 1.0;
		l->zero_array[k] = 0.0;
	}

	for (m = 0; m < dml2_core_internal_soc_state_max; m++) {
		for (n = 0; n < dml2_core_internal_bw_max; n++) {
			get_urgent_bandwidth_required(
				&s->get_urgent_bandwidth_required_locals,
				p->display_cfg,
				m,
				n,
				0, //inc_flip_bw,
				0, //use_qual_row_bw
				p->num_active_planes,
				p->num_of_dpp,
				p->dcc_dram_bw_nom_overhead_factor_p0,
				p->dcc_dram_bw_nom_overhead_factor_p1,
				p->dcc_dram_bw_pref_overhead_factor_p0,
				p->dcc_dram_bw_pref_overhead_factor_p1,
				p->mall_prefetch_sdp_overhead_factor,
				p->mall_prefetch_dram_overhead_factor,
				p->surface_read_bandwidth_l,
				p->surface_read_bandwidth_c,
				l->zero_array, //PrefetchBandwidthLuma,
				l->zero_array, //PrefetchBandwidthChroma,
				l->zero_array,
				l->zero_array,
				l->zero_array,
				p->dpte_row_bw,
				p->meta_row_bw,
				l->zero_array, //prefetch_cursor_bw,
				l->zero_array, //prefetch_vmrow_bw,
				l->zero_array, //flip_bw,
				l->zero_array,
				l->zero_array,
				l->zero_array,
				l->zero_array,
				l->zero_array,
				l->zero_array,
				p->surface_avg_vactive_required_bw[m][n],
				p->surface_peak_required_bw[m][n]);

			p->urg_vactive_bandwidth_required[m][n] = get_urgent_bandwidth_required(
				&s->get_urgent_bandwidth_required_locals,
				p->display_cfg,
				m,
				n,
				0, //inc_flip_bw,
				0, //use_qual_row_bw
				p->num_active_planes,
				p->num_of_dpp,
				p->dcc_dram_bw_nom_overhead_factor_p0,
				p->dcc_dram_bw_nom_overhead_factor_p1,
				p->dcc_dram_bw_pref_overhead_factor_p0,
				p->dcc_dram_bw_pref_overhead_factor_p1,
				p->mall_prefetch_sdp_overhead_factor,
				p->mall_prefetch_dram_overhead_factor,
				p->surface_read_bandwidth_l,
				p->surface_read_bandwidth_c,
				l->zero_array, //PrefetchBandwidthLuma,
				l->zero_array, //PrefetchBandwidthChroma,
				p->excess_vactive_fill_bw_l,
				p->excess_vactive_fill_bw_c,
				p->cursor_bw,
				p->dpte_row_bw,
				p->meta_row_bw,
				l->zero_array, //prefetch_cursor_bw,
				l->zero_array, //prefetch_vmrow_bw,
				l->zero_array, //flip_bw,
				p->urgent_burst_factor_l,
				p->urgent_burst_factor_c,
				p->urgent_burst_factor_cursor,
				p->urgent_burst_factor_prefetch_l,
				p->urgent_burst_factor_prefetch_c,
				p->urgent_burst_factor_prefetch_cursor,
				l->surface_dummy_bw,
				p->surface_peak_required_bw[m][n]);

			p->urg_bandwidth_required[m][n] = get_urgent_bandwidth_required(
				&s->get_urgent_bandwidth_required_locals,
				p->display_cfg,
				m,
				n,
				p->inc_flip_bw,
				0, //use_qual_row_bw
				p->num_active_planes,
				p->num_of_dpp,
				p->dcc_dram_bw_nom_overhead_factor_p0,
				p->dcc_dram_bw_nom_overhead_factor_p1,
				p->dcc_dram_bw_pref_overhead_factor_p0,
				p->dcc_dram_bw_pref_overhead_factor_p1,
				p->mall_prefetch_sdp_overhead_factor,
				p->mall_prefetch_dram_overhead_factor,
				p->surface_read_bandwidth_l,
				p->surface_read_bandwidth_c,
				p->prefetch_bandwidth_l,
				p->prefetch_bandwidth_c,
				p->excess_vactive_fill_bw_l,
				p->excess_vactive_fill_bw_c,
				p->cursor_bw,
				p->dpte_row_bw,
				p->meta_row_bw,
				p->prefetch_cursor_bw,
				p->prefetch_vmrow_bw,
				p->flip_bw,
				p->urgent_burst_factor_l,
				p->urgent_burst_factor_c,
				p->urgent_burst_factor_cursor,
				p->urgent_burst_factor_prefetch_l,
				p->urgent_burst_factor_prefetch_c,
				p->urgent_burst_factor_prefetch_cursor,
				l->surface_dummy_bw,
				p->surface_peak_required_bw[m][n]);

			p->urg_bandwidth_required_qual[m][n] = get_urgent_bandwidth_required(
				&s->get_urgent_bandwidth_required_locals,
				p->display_cfg,
				m,
				n,
				0, //inc_flip_bw
				1, //use_qual_row_bw
				p->num_active_planes,
				p->num_of_dpp,
				p->dcc_dram_bw_nom_overhead_factor_p0,
				p->dcc_dram_bw_nom_overhead_factor_p1,
				p->dcc_dram_bw_pref_overhead_factor_p0,
				p->dcc_dram_bw_pref_overhead_factor_p1,
				p->mall_prefetch_sdp_overhead_factor,
				p->mall_prefetch_dram_overhead_factor,
				p->surface_read_bandwidth_l,
				p->surface_read_bandwidth_c,
				p->prefetch_bandwidth_l,
				p->prefetch_bandwidth_c,
				p->excess_vactive_fill_bw_l,
				p->excess_vactive_fill_bw_c,
				p->cursor_bw,
				p->dpte_row_bw,
				p->meta_row_bw,
				p->prefetch_cursor_bw,
				p->prefetch_vmrow_bw,
				p->flip_bw,
				p->urgent_burst_factor_l,
				p->urgent_burst_factor_c,
				p->urgent_burst_factor_cursor,
				p->urgent_burst_factor_prefetch_l,
				p->urgent_burst_factor_prefetch_c,
				p->urgent_burst_factor_prefetch_cursor,
				l->surface_dummy_bw,
				p->surface_peak_required_bw[m][n]);

			p->non_urg_bandwidth_required[m][n] = get_urgent_bandwidth_required(
				&s->get_urgent_bandwidth_required_locals,
				p->display_cfg,
				m,
				n,
				p->inc_flip_bw,
				0, //use_qual_row_bw
				p->num_active_planes,
				p->num_of_dpp,
				p->dcc_dram_bw_nom_overhead_factor_p0,
				p->dcc_dram_bw_nom_overhead_factor_p1,
				p->dcc_dram_bw_pref_overhead_factor_p0,
				p->dcc_dram_bw_pref_overhead_factor_p1,
				p->mall_prefetch_sdp_overhead_factor,
				p->mall_prefetch_dram_overhead_factor,
				p->surface_read_bandwidth_l,
				p->surface_read_bandwidth_c,
				p->prefetch_bandwidth_l,
				p->prefetch_bandwidth_c,
				p->excess_vactive_fill_bw_l,
				p->excess_vactive_fill_bw_c,
				p->cursor_bw,
				p->dpte_row_bw,
				p->meta_row_bw,
				p->prefetch_cursor_bw,
				p->prefetch_vmrow_bw,
				p->flip_bw,
				l->unity_array,
				l->unity_array,
				l->unity_array,
				l->unity_array,
				l->unity_array,
				l->unity_array,
				l->surface_dummy_bw,
				p->surface_peak_required_bw[m][n]);

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: urg_vactive_bandwidth_required%s[%s][%s]=%f\n", __func__, (p->inc_flip_bw ? "_flip" : ""), dml2_core_internal_soc_state_type_str(m), dml2_core_internal_bw_type_str(n), p->urg_vactive_bandwidth_required[m][n]);
			dml2_printf("DML::%s: urg_bandwidth_required%s[%s][%s]=%f\n", __func__, (p->inc_flip_bw ? "_flip" : ""), dml2_core_internal_soc_state_type_str(m), dml2_core_internal_bw_type_str(n), p->urg_bandwidth_required[m][n]);
			dml2_printf("DML::%s: urg_bandwidth_required_qual[%s][%s]=%f\n", __func__, dml2_core_internal_soc_state_type_str(m), dml2_core_internal_bw_type_str(n), p->urg_bandwidth_required[m][n]);
			dml2_printf("DML::%s: non_urg_bandwidth_required%s[%s][%s]=%f\n", __func__, (p->inc_flip_bw ? "_flip" : ""), dml2_core_internal_soc_state_type_str(m), dml2_core_internal_bw_type_str(n), p->non_urg_bandwidth_required[m][n]);
#endif
			dml2_assert(p->urg_bandwidth_required[m][n] >= p->non_urg_bandwidth_required[m][n]);
		}
	}
}

static void check_urgent_bandwidth_support(
	double *frac_urg_bandwidth_nom,
	double *frac_urg_bandwidth_mall,
	bool *vactive_bandwidth_support_ok, // vactive ok
	bool *bandwidth_support_ok,// max of vm, prefetch, vactive all ok

	unsigned int mall_allocated_for_dcn_mbytes,
	double non_urg_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max],
	double urg_vactive_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max],
	double urg_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max],
	double urg_bandwidth_available[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max])
{
	double frac_urg_bandwidth_nom_sdp = non_urg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp] / urg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp];
	double frac_urg_bandwidth_nom_dram = non_urg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram] / urg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram];
	double frac_urg_bandwidth_mall_sdp;
	double frac_urg_bandwidth_mall_dram;
	if (urg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp] > 0)
		frac_urg_bandwidth_mall_sdp = non_urg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp] / urg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp];
	else
		frac_urg_bandwidth_mall_sdp = 0.0;
	if (urg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram] > 0)
		frac_urg_bandwidth_mall_dram = non_urg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram] / urg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram];
	else
		frac_urg_bandwidth_mall_dram = 0.0;

	*bandwidth_support_ok = 1;
	*vactive_bandwidth_support_ok = 1;

	// Check urgent bandwidth required at sdp vs urgent bandwidth avail at sdp -> FractionOfUrgentBandwidth
	// Check urgent bandwidth required at dram vs urgent bandwidth avail at dram
	// Check urgent bandwidth required at sdp vs urgent bandwidth avail at sdp, svp_prefetch -> FractionOfUrgentBandwidthMALL
	// Check urgent bandwidth required at dram vs urgent bandwidth avail at dram, svp_prefetch

	*bandwidth_support_ok &= urg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp] <= urg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp];
	*bandwidth_support_ok &= urg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram] <= urg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram];

	if (mall_allocated_for_dcn_mbytes > 0) {
		*bandwidth_support_ok &= urg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp] <= urg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp];
		*bandwidth_support_ok &= urg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram] <= urg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram];
	}

	*frac_urg_bandwidth_nom = math_max2(frac_urg_bandwidth_nom_sdp, frac_urg_bandwidth_nom_dram);
	*frac_urg_bandwidth_mall = math_max2(frac_urg_bandwidth_mall_sdp, frac_urg_bandwidth_mall_dram);

	*bandwidth_support_ok &= (*frac_urg_bandwidth_nom <= 1.0);

	if (mall_allocated_for_dcn_mbytes > 0)
		*bandwidth_support_ok &= (*frac_urg_bandwidth_mall <= 1.0);

	*vactive_bandwidth_support_ok &= urg_vactive_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp] <= urg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp];
	*vactive_bandwidth_support_ok &= urg_vactive_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram] <= urg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram];
	if (mall_allocated_for_dcn_mbytes > 0) {
		*vactive_bandwidth_support_ok &= urg_vactive_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp] <= urg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_sdp];
		*vactive_bandwidth_support_ok &= urg_vactive_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram] <= urg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][dml2_core_internal_bw_dram];
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: frac_urg_bandwidth_nom_sdp = %f\n", __func__, frac_urg_bandwidth_nom_sdp);
	dml2_printf("DML::%s: frac_urg_bandwidth_nom_dram = %f\n", __func__, frac_urg_bandwidth_nom_dram);
	dml2_printf("DML::%s: frac_urg_bandwidth_nom = %f\n", __func__, *frac_urg_bandwidth_nom);

	dml2_printf("DML::%s: frac_urg_bandwidth_mall_sdp = %f\n", __func__, frac_urg_bandwidth_mall_sdp);
	dml2_printf("DML::%s: frac_urg_bandwidth_mall_dram = %f\n", __func__, frac_urg_bandwidth_mall_dram);
	dml2_printf("DML::%s: frac_urg_bandwidth_mall = %f\n", __func__, *frac_urg_bandwidth_mall);
	dml2_printf("DML::%s: bandwidth_support_ok = %d\n", __func__, *bandwidth_support_ok);

	for (unsigned int m = 0; m < dml2_core_internal_soc_state_max; m++) {
		for (unsigned int n = 0; n < dml2_core_internal_bw_max; n++) {
			dml2_printf("DML::%s: state:%s bw_type:%s urg_bandwidth_available=%f %s urg_bandwidth_required=%f\n",
			__func__, dml2_core_internal_soc_state_type_str(m), dml2_core_internal_bw_type_str(n),
			urg_bandwidth_available[m][n], (urg_bandwidth_available[m][n] < urg_bandwidth_required[m][n]) ? "<" : ">=", urg_bandwidth_required[m][n]);
		}
	}
#endif
}

static double get_bandwidth_available_for_immediate_flip(enum dml2_core_internal_soc_state_type eval_state,
	double urg_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max], // no flip
	double urg_bandwidth_available[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max])
{
	double flip_bw_available_mbps;
	double flip_bw_available_sdp_mbps;
	double flip_bw_available_dram_mbps;

	flip_bw_available_sdp_mbps = urg_bandwidth_available[eval_state][dml2_core_internal_bw_sdp] - urg_bandwidth_required[eval_state][dml2_core_internal_bw_sdp];
	flip_bw_available_dram_mbps = urg_bandwidth_available[eval_state][dml2_core_internal_bw_dram] - urg_bandwidth_required[eval_state][dml2_core_internal_bw_dram];
	flip_bw_available_mbps = flip_bw_available_sdp_mbps < flip_bw_available_dram_mbps ? flip_bw_available_sdp_mbps : flip_bw_available_dram_mbps;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: eval_state = %s\n", __func__, dml2_core_internal_soc_state_type_str(eval_state));
	dml2_printf("DML::%s: urg_bandwidth_available_sdp_mbps = %f\n", __func__, urg_bandwidth_available[eval_state][dml2_core_internal_bw_sdp]);
	dml2_printf("DML::%s: urg_bandwidth_available_dram_mbps = %f\n", __func__, urg_bandwidth_available[eval_state][dml2_core_internal_bw_dram]);
	dml2_printf("DML::%s: urg_bandwidth_required_sdp_mbps = %f\n", __func__, urg_bandwidth_required[eval_state][dml2_core_internal_bw_sdp]);
	dml2_printf("DML::%s: urg_bandwidth_required_dram_mbps = %f\n", __func__, urg_bandwidth_required[eval_state][dml2_core_internal_bw_dram]);
	dml2_printf("DML::%s: flip_bw_available_sdp_mbps = %f\n", __func__, flip_bw_available_sdp_mbps);
	dml2_printf("DML::%s: flip_bw_available_dram_mbps = %f\n", __func__, flip_bw_available_dram_mbps);
	dml2_printf("DML::%s: flip_bw_available_mbps = %f\n", __func__, flip_bw_available_mbps);
#endif

	return flip_bw_available_mbps;
}

static void calculate_immediate_flip_bandwidth_support(
	// Output
	double *frac_urg_bandwidth_flip,
	bool *flip_bandwidth_support_ok,

	// Input
	enum dml2_core_internal_soc_state_type eval_state,
	double urg_bandwidth_required_flip[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max],
	double non_urg_bandwidth_required_flip[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max],
	double urg_bandwidth_available[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max])
{
	double frac_urg_bw_flip_sdp = non_urg_bandwidth_required_flip[eval_state][dml2_core_internal_bw_sdp] / urg_bandwidth_available[eval_state][dml2_core_internal_bw_sdp];
	double frac_urg_bw_flip_dram = non_urg_bandwidth_required_flip[eval_state][dml2_core_internal_bw_dram] / urg_bandwidth_available[eval_state][dml2_core_internal_bw_dram];

	*flip_bandwidth_support_ok = true;
	for (unsigned int n = 0; n < dml2_core_internal_bw_max; n++) { // check sdp and dram
		*flip_bandwidth_support_ok &= urg_bandwidth_available[eval_state][n] >= urg_bandwidth_required_flip[eval_state][n];

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: n = %s\n", __func__, dml2_core_internal_bw_type_str(n));
		dml2_printf("DML::%s: urg_bandwidth_available = %f\n", __func__, urg_bandwidth_available[eval_state][n]);
		dml2_printf("DML::%s: non_urg_bandwidth_required_flip = %f\n", __func__, non_urg_bandwidth_required_flip[eval_state][n]);
		dml2_printf("DML::%s: urg_bandwidth_required_flip = %f\n", __func__, urg_bandwidth_required_flip[eval_state][n]);
		dml2_printf("DML::%s: flip_bandwidth_support_ok = %d\n", __func__, *flip_bandwidth_support_ok);
#endif
		dml2_assert(urg_bandwidth_required_flip[eval_state][n] >= non_urg_bandwidth_required_flip[eval_state][n]);
	}

	*frac_urg_bandwidth_flip = (frac_urg_bw_flip_sdp > frac_urg_bw_flip_dram) ? frac_urg_bw_flip_sdp : frac_urg_bw_flip_dram;
	*flip_bandwidth_support_ok &= (*frac_urg_bandwidth_flip <= 1.0);

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: eval_state = %s\n", __func__, dml2_core_internal_soc_state_type_str(eval_state));
	dml2_printf("DML::%s: frac_urg_bw_flip_sdp = %f\n", __func__, frac_urg_bw_flip_sdp);
	dml2_printf("DML::%s: frac_urg_bw_flip_dram = %f\n", __func__, frac_urg_bw_flip_dram);
	dml2_printf("DML::%s: frac_urg_bandwidth_flip = %f\n", __func__, *frac_urg_bandwidth_flip);
	dml2_printf("DML::%s: flip_bandwidth_support_ok = %d\n", __func__, *flip_bandwidth_support_ok);

	for (unsigned int m = 0; m < dml2_core_internal_soc_state_max; m++) {
		for (unsigned int n = 0; n < dml2_core_internal_bw_max; n++) {
			dml2_printf("DML::%s: state:%s bw_type:%s, urg_bandwidth_available=%f %s urg_bandwidth_required=%f\n",
			__func__, dml2_core_internal_soc_state_type_str(m), dml2_core_internal_bw_type_str(n),
			urg_bandwidth_available[m][n], (urg_bandwidth_available[m][n] < urg_bandwidth_required_flip[m][n]) ? "<" : ">=", urg_bandwidth_required_flip[m][n]);
		}
	}
#endif
}

static void CalculateFlipSchedule(
	struct dml2_core_internal_scratch *s,
	bool iflip_enable,
	bool use_lb_flip_bw,
	double HostVMInefficiencyFactor,
	double Tvm_trips_flip,
	double Tr0_trips_flip,
	double Tvm_trips_flip_rounded,
	double Tr0_trips_flip_rounded,
	bool GPUVMEnable,
	double vm_bytes, // vm_bytes
	double DPTEBytesPerRow, // dpte_row_bytes
	double BandwidthAvailableForImmediateFlip,
	unsigned int TotImmediateFlipBytes,
	enum dml2_source_format_class SourcePixelFormat,
	double LineTime,
	double VRatio,
	double VRatioChroma,
	double Tno_bw_flip,
	unsigned int dpte_row_height,
	unsigned int dpte_row_height_chroma,
	bool use_one_row_for_frame_flip,
	unsigned int max_flip_time_us,
	unsigned int max_flip_time_lines,
	unsigned int per_pipe_flip_bytes,
	unsigned int meta_row_bytes,
	unsigned int meta_row_height,
	unsigned int meta_row_height_chroma,
	bool dcc_mrq_enable,

	// Output
	double *dst_y_per_vm_flip,
	double *dst_y_per_row_flip,
	double *final_flip_bw,
	bool *ImmediateFlipSupportedForPipe)
{
	struct dml2_core_shared_CalculateFlipSchedule_locals *l = &s->CalculateFlipSchedule_locals;

	l->dual_plane = dml_is_420(SourcePixelFormat) || SourcePixelFormat == dml2_rgbe_alpha;
	l->dpte_row_bytes = DPTEBytesPerRow;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: GPUVMEnable = %u\n", __func__, GPUVMEnable);
	dml2_printf("DML::%s: ip.max_flip_time_us = %d\n", __func__, max_flip_time_us);
	dml2_printf("DML::%s: ip.max_flip_time_lines = %d\n", __func__, max_flip_time_lines);
	dml2_printf("DML::%s: BandwidthAvailableForImmediateFlip = %f\n", __func__, BandwidthAvailableForImmediateFlip);
	dml2_printf("DML::%s: TotImmediateFlipBytes = %u\n", __func__, TotImmediateFlipBytes);
	dml2_printf("DML::%s: use_lb_flip_bw = %u\n", __func__, use_lb_flip_bw);
	dml2_printf("DML::%s: iflip_enable = %u\n", __func__, iflip_enable);
	dml2_printf("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, HostVMInefficiencyFactor);
	dml2_printf("DML::%s: LineTime = %f\n", __func__, LineTime);
	dml2_printf("DML::%s: Tno_bw_flip = %f\n", __func__, Tno_bw_flip);
	dml2_printf("DML::%s: Tvm_trips_flip = %f\n", __func__, Tvm_trips_flip);
	dml2_printf("DML::%s: Tr0_trips_flip = %f\n", __func__, Tr0_trips_flip);
	dml2_printf("DML::%s: Tvm_trips_flip_rounded = %f\n", __func__, Tvm_trips_flip_rounded);
	dml2_printf("DML::%s: Tr0_trips_flip_rounded = %f\n", __func__, Tr0_trips_flip_rounded);
	dml2_printf("DML::%s: vm_bytes = %f\n", __func__, vm_bytes);
	dml2_printf("DML::%s: DPTEBytesPerRow = %f\n", __func__, DPTEBytesPerRow);
	dml2_printf("DML::%s: meta_row_bytes = %d\n", __func__, meta_row_bytes);
	dml2_printf("DML::%s: dpte_row_bytes = %f\n", __func__, l->dpte_row_bytes);
	dml2_printf("DML::%s: dpte_row_height = %d\n", __func__, dpte_row_height);
	dml2_printf("DML::%s: meta_row_height = %d\n", __func__, meta_row_height);
	dml2_printf("DML::%s: VRatio = %f\n", __func__, VRatio);
#endif

	if (TotImmediateFlipBytes > 0 && (GPUVMEnable || dcc_mrq_enable)) {
		if (l->dual_plane) {
			if (dcc_mrq_enable & GPUVMEnable) {
				l->min_row_height = math_min2(dpte_row_height, meta_row_height);
				l->min_row_height_chroma = math_min2(dpte_row_height_chroma, meta_row_height_chroma);
			} else if (GPUVMEnable) {
				l->min_row_height = dpte_row_height;
				l->min_row_height_chroma = dpte_row_height_chroma;
			} else {
				l->min_row_height = meta_row_height;
				l->min_row_height_chroma = meta_row_height_chroma;
			}
			l->min_row_time = math_min2(l->min_row_height * LineTime / VRatio, l->min_row_height_chroma * LineTime / VRatioChroma);
		} else {
			if (dcc_mrq_enable & GPUVMEnable)
				l->min_row_height = math_min2(dpte_row_height, meta_row_height);
			else if (GPUVMEnable)
				l->min_row_height = dpte_row_height;
			else
				l->min_row_height = meta_row_height;

			l->min_row_time = l->min_row_height * LineTime / VRatio;
		}
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: min_row_time = %f\n", __func__, l->min_row_time);
#endif
		dml2_assert(l->min_row_time > 0);

		if (use_lb_flip_bw) {
			// For mode check, calculation the flip bw requirement with worst case flip time
			l->max_flip_time = math_min2(math_min2(l->min_row_time, (double)max_flip_time_lines * LineTime / VRatio),
				math_max2(Tvm_trips_flip_rounded + 2 * Tr0_trips_flip_rounded, (double)max_flip_time_us));

			//The lower bound on flip bandwidth
			// Note: The get_urgent_bandwidth_required already consider dpte_row_bw and meta_row_bw in bandwidth calculation, so leave final_flip_bw = 0 if iflip not required
			l->lb_flip_bw = 0;

			if (iflip_enable) {
				l->hvm_scaled_vm_bytes = vm_bytes * HostVMInefficiencyFactor;
				l->num_rows = 2;
				l->hvm_scaled_row_bytes = (l->num_rows * l->dpte_row_bytes * HostVMInefficiencyFactor + l->num_rows * meta_row_bytes);
				l->hvm_scaled_vm_row_bytes = l->hvm_scaled_vm_bytes + l->hvm_scaled_row_bytes;
				l->lb_flip_bw = math_max3(
					l->hvm_scaled_vm_row_bytes / (l->max_flip_time - Tno_bw_flip),
					l->hvm_scaled_vm_bytes / (l->max_flip_time - Tno_bw_flip - 2 * Tr0_trips_flip_rounded),
					l->hvm_scaled_row_bytes / (l->max_flip_time - Tvm_trips_flip_rounded));
#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: max_flip_time = %f\n", __func__, l->max_flip_time);
				dml2_printf("DML::%s: total vm bytes (hvm ineff scaled) = %f\n", __func__, l->hvm_scaled_vm_bytes);
				dml2_printf("DML::%s: total row bytes (%d row, hvm ineff scaled) = %f\n", __func__, l->num_rows, l->hvm_scaled_row_bytes);
				dml2_printf("DML::%s: total vm+row bytes (hvm ineff scaled) = %f\n", __func__, l->hvm_scaled_vm_row_bytes);
				dml2_printf("DML::%s: lb_flip_bw for vm and row = %f\n", __func__, l->hvm_scaled_vm_row_bytes / (l->max_flip_time - Tno_bw_flip));
				dml2_printf("DML::%s: lb_flip_bw for vm = %f\n", __func__, l->hvm_scaled_vm_bytes / (l->max_flip_time - Tno_bw_flip - 2 * Tr0_trips_flip_rounded));
				dml2_printf("DML::%s: lb_flip_bw for row = %f\n", __func__, l->hvm_scaled_row_bytes / (l->max_flip_time - Tvm_trips_flip_rounded));

				if (l->lb_flip_bw > 0) {
					dml2_printf("DML::%s: mode_support est Tvm_flip = %f (bw-based)\n", __func__, Tno_bw_flip + l->hvm_scaled_vm_bytes / l->lb_flip_bw);
					dml2_printf("DML::%s: mode_support est Tr0_flip = %f (bw-based)\n", __func__, l->hvm_scaled_row_bytes / l->lb_flip_bw / l->num_rows);
					dml2_printf("DML::%s: mode_support est dst_y_per_vm_flip = %f (bw-based)\n", __func__, Tno_bw_flip + l->hvm_scaled_vm_bytes / l->lb_flip_bw / LineTime);
					dml2_printf("DML::%s: mode_support est dst_y_per_row_flip = %f (bw-based)\n", __func__, l->hvm_scaled_row_bytes / l->lb_flip_bw / LineTime / l->num_rows);
					dml2_printf("DML::%s: Tvm_trips_flip_rounded + 2*Tr0_trips_flip_rounded = %f\n", __func__, (Tvm_trips_flip_rounded + 2 * Tr0_trips_flip_rounded));
				}
#endif
				l->lb_flip_bw = math_max3(l->lb_flip_bw,
						l->hvm_scaled_vm_bytes / (31 * LineTime) - Tno_bw_flip,
						(l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes) / (15 * LineTime));

#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: lb_flip_bw for vm reg limit = %f\n", __func__, l->hvm_scaled_vm_bytes / (31 * LineTime) - Tno_bw_flip);
				dml2_printf("DML::%s: lb_flip_bw for row reg limit = %f\n", __func__, (l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes) / (15 * LineTime));
#endif
			}

			*final_flip_bw = l->lb_flip_bw;

			*dst_y_per_vm_flip = 1; // not used
			*dst_y_per_row_flip = 1; // not used
			*ImmediateFlipSupportedForPipe = l->min_row_time >= (Tvm_trips_flip_rounded + 2 * Tr0_trips_flip_rounded);
		} else {
			if (iflip_enable) {
				l->ImmediateFlipBW = (double)per_pipe_flip_bytes * BandwidthAvailableForImmediateFlip / (double)TotImmediateFlipBytes; // flip_bw(i)
				double portion = (double)per_pipe_flip_bytes / (double)TotImmediateFlipBytes;

#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: per_pipe_flip_bytes = %d\n", __func__, per_pipe_flip_bytes);
				dml2_printf("DML::%s: BandwidthAvailableForImmediateFlip = %f\n", __func__, BandwidthAvailableForImmediateFlip);
				dml2_printf("DML::%s: ImmediateFlipBW = %f\n", __func__, l->ImmediateFlipBW);
				dml2_printf("DML::%s: portion of flip bw = %f\n", __func__, portion);
#endif
				if (l->ImmediateFlipBW == 0) {
					l->Tvm_flip = 0;
					l->Tr0_flip = 0;
				} else {
					l->Tvm_flip = math_max3(Tvm_trips_flip,
						Tno_bw_flip + vm_bytes * HostVMInefficiencyFactor / l->ImmediateFlipBW,
						LineTime / 4.0);

					l->Tr0_flip = math_max3(Tr0_trips_flip,
						(l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes) / l->ImmediateFlipBW,
						LineTime / 4.0);
				}
#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: total vm bytes (hvm ineff scaled) = %f\n", __func__, vm_bytes * HostVMInefficiencyFactor);
				dml2_printf("DML::%s: total row bytes (hvm ineff scaled, one row) = %f\n", __func__, (l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes));

				dml2_printf("DML::%s: Tvm_flip = %f (bw-based), Tvm_trips_flip = %f (latency-based)\n", __func__, Tno_bw_flip + vm_bytes * HostVMInefficiencyFactor / l->ImmediateFlipBW, Tvm_trips_flip);
				dml2_printf("DML::%s: Tr0_flip = %f (bw-based), Tr0_trips_flip = %f (latency-based)\n", __func__, (l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes) / l->ImmediateFlipBW, Tr0_trips_flip);
#endif
				*dst_y_per_vm_flip = math_ceil2(4.0 * (l->Tvm_flip / LineTime), 1.0) / 4.0;
				*dst_y_per_row_flip = math_ceil2(4.0 * (l->Tr0_flip / LineTime), 1.0) / 4.0;

				*final_flip_bw = math_max2(vm_bytes * HostVMInefficiencyFactor / (*dst_y_per_vm_flip * LineTime),
					(l->dpte_row_bytes * HostVMInefficiencyFactor + meta_row_bytes) / (*dst_y_per_row_flip * LineTime));

				if (*dst_y_per_vm_flip >= 32 || *dst_y_per_row_flip >= 16 || l->Tvm_flip + 2 * l->Tr0_flip > l->min_row_time) {
					*ImmediateFlipSupportedForPipe = false;
				} else {
					*ImmediateFlipSupportedForPipe = iflip_enable;
				}
			} else {
				l->Tvm_flip = 0;
				l->Tr0_flip = 0;
				*dst_y_per_vm_flip = 0;
				*dst_y_per_row_flip = 0;
				*final_flip_bw = 0;
				*ImmediateFlipSupportedForPipe = iflip_enable;
			}
		}
	} else {
		l->Tvm_flip = 0;
		l->Tr0_flip = 0;
		*dst_y_per_vm_flip = 0;
		*dst_y_per_row_flip = 0;
		*final_flip_bw = 0;
		*ImmediateFlipSupportedForPipe = iflip_enable;
	}

#ifdef __DML_VBA_DEBUG__
	if (!use_lb_flip_bw) {
		dml2_printf("DML::%s: dst_y_per_vm_flip = %f (should be < 32)\n", __func__, *dst_y_per_vm_flip);
		dml2_printf("DML::%s: dst_y_per_row_flip = %f (should be < 16)\n", __func__, *dst_y_per_row_flip);
		dml2_printf("DML::%s: Tvm_flip = %f (final)\n", __func__, l->Tvm_flip);
		dml2_printf("DML::%s: Tr0_flip = %f (final)\n", __func__, l->Tr0_flip);
		dml2_printf("DML::%s: Tvm_flip + 2*Tr0_flip = %f (should be <= min_row_time=%f)\n", __func__, l->Tvm_flip + 2 * l->Tr0_flip, l->min_row_time);
	}
	dml2_printf("DML::%s: final_flip_bw = %f\n", __func__, *final_flip_bw);
	dml2_printf("DML::%s: ImmediateFlipSupportedForPipe = %u\n", __func__, *ImmediateFlipSupportedForPipe);
#endif
}

static void CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport(
	struct dml2_core_internal_scratch *scratch,
	struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params *p)
{
	struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals *s = &scratch->CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals;

	enum dml2_uclk_pstate_change_strategy uclk_pstate_change_strategy;
	double reserved_vblank_time_us;
	bool FoundCriticalSurface = false;

	s->TotalActiveWriteback = 0;
	p->Watermark->UrgentWatermark = p->mmSOCParameters.UrgentLatency + p->mmSOCParameters.ExtraLatency;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: UrgentWatermark = %f\n", __func__, p->Watermark->UrgentWatermark);
#endif

	p->Watermark->USRRetrainingWatermark = p->mmSOCParameters.UrgentLatency + p->mmSOCParameters.ExtraLatency + p->mmSOCParameters.USRRetrainingLatency + p->mmSOCParameters.SMNLatency;
	p->Watermark->DRAMClockChangeWatermark = p->mmSOCParameters.DRAMClockChangeLatency + p->Watermark->UrgentWatermark;
	p->Watermark->FCLKChangeWatermark = p->mmSOCParameters.FCLKChangeLatency + p->Watermark->UrgentWatermark;
	p->Watermark->StutterExitWatermark = p->mmSOCParameters.SRExitTime + p->mmSOCParameters.ExtraLatency_sr + 10 / p->DCFClkDeepSleep;
	p->Watermark->StutterEnterPlusExitWatermark = p->mmSOCParameters.SREnterPlusExitTime + p->mmSOCParameters.ExtraLatency_sr + 10 / p->DCFClkDeepSleep;
	p->Watermark->Z8StutterExitWatermark = p->mmSOCParameters.SRExitZ8Time + p->mmSOCParameters.ExtraLatency_sr + 10 / p->DCFClkDeepSleep;
	p->Watermark->Z8StutterEnterPlusExitWatermark = p->mmSOCParameters.SREnterPlusExitZ8Time + p->mmSOCParameters.ExtraLatency_sr + 10 / p->DCFClkDeepSleep;
	if (p->mmSOCParameters.qos_type == dml2_qos_param_type_dcn4x) {
		p->Watermark->StutterExitWatermark += p->mmSOCParameters.max_urgent_latency_us + p->mmSOCParameters.df_response_time_us;
		p->Watermark->StutterEnterPlusExitWatermark += p->mmSOCParameters.max_urgent_latency_us + p->mmSOCParameters.df_response_time_us;
		p->Watermark->Z8StutterExitWatermark += p->mmSOCParameters.max_urgent_latency_us + p->mmSOCParameters.df_response_time_us;
		p->Watermark->Z8StutterEnterPlusExitWatermark += p->mmSOCParameters.max_urgent_latency_us + p->mmSOCParameters.df_response_time_us;
	}
	p->Watermark->temp_read_or_ppt_watermark_us = p->mmSOCParameters.g6_temp_read_blackout_us + p->Watermark->UrgentWatermark;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: UrgentLatency = %f\n", __func__, p->mmSOCParameters.UrgentLatency);
	dml2_printf("DML::%s: ExtraLatency = %f\n", __func__, p->mmSOCParameters.ExtraLatency);
	dml2_printf("DML::%s: DRAMClockChangeLatency = %f\n", __func__, p->mmSOCParameters.DRAMClockChangeLatency);
	dml2_printf("DML::%s: SREnterPlusExitZ8Time = %f\n", __func__, p->mmSOCParameters.SREnterPlusExitZ8Time);
	dml2_printf("DML::%s: SREnterPlusExitTime = %f\n", __func__, p->mmSOCParameters.SREnterPlusExitTime);
	dml2_printf("DML::%s: UrgentWatermark = %f\n", __func__, p->Watermark->UrgentWatermark);
	dml2_printf("DML::%s: USRRetrainingWatermark = %f\n", __func__, p->Watermark->USRRetrainingWatermark);
	dml2_printf("DML::%s: DRAMClockChangeWatermark = %f\n", __func__, p->Watermark->DRAMClockChangeWatermark);
	dml2_printf("DML::%s: FCLKChangeWatermark = %f\n", __func__, p->Watermark->FCLKChangeWatermark);
	dml2_printf("DML::%s: StutterExitWatermark = %f\n", __func__, p->Watermark->StutterExitWatermark);
	dml2_printf("DML::%s: StutterEnterPlusExitWatermark = %f\n", __func__, p->Watermark->StutterEnterPlusExitWatermark);
	dml2_printf("DML::%s: Z8StutterExitWatermark = %f\n", __func__, p->Watermark->Z8StutterExitWatermark);
	dml2_printf("DML::%s: Z8StutterEnterPlusExitWatermark = %f\n", __func__, p->Watermark->Z8StutterEnterPlusExitWatermark);
	dml2_printf("DML::%s: temp_read_or_ppt_watermark_us = %f\n", __func__, p->Watermark->temp_read_or_ppt_watermark_us);
#endif

	s->TotalActiveWriteback = 0;
	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0) {
			s->TotalActiveWriteback = s->TotalActiveWriteback + 1;
		}
	}

	if (s->TotalActiveWriteback <= 1) {
		p->Watermark->WritebackUrgentWatermark = p->mmSOCParameters.WritebackLatency;
	} else {
		p->Watermark->WritebackUrgentWatermark = p->mmSOCParameters.WritebackLatency + p->WritebackChunkSize * 1024.0 / 32.0 / p->SOCCLK;
	}
	if (p->USRRetrainingRequired)
		p->Watermark->WritebackUrgentWatermark = p->Watermark->WritebackUrgentWatermark + p->mmSOCParameters.USRRetrainingLatency;

	if (s->TotalActiveWriteback <= 1) {
		p->Watermark->WritebackDRAMClockChangeWatermark = p->mmSOCParameters.DRAMClockChangeLatency + p->mmSOCParameters.WritebackLatency;
		p->Watermark->WritebackFCLKChangeWatermark = p->mmSOCParameters.FCLKChangeLatency + p->mmSOCParameters.WritebackLatency;
	} else {
		p->Watermark->WritebackDRAMClockChangeWatermark = p->mmSOCParameters.DRAMClockChangeLatency + p->mmSOCParameters.WritebackLatency + p->WritebackChunkSize * 1024.0 / 32.0 / p->SOCCLK;
		p->Watermark->WritebackFCLKChangeWatermark = p->mmSOCParameters.FCLKChangeLatency + p->mmSOCParameters.WritebackLatency + p->WritebackChunkSize * 1024 / 32 / p->SOCCLK;
	}

	if (p->USRRetrainingRequired)
		p->Watermark->WritebackDRAMClockChangeWatermark = p->Watermark->WritebackDRAMClockChangeWatermark + p->mmSOCParameters.USRRetrainingLatency;

	if (p->USRRetrainingRequired)
		p->Watermark->WritebackFCLKChangeWatermark = p->Watermark->WritebackFCLKChangeWatermark + p->mmSOCParameters.USRRetrainingLatency;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: WritebackDRAMClockChangeWatermark = %f\n", __func__, p->Watermark->WritebackDRAMClockChangeWatermark);
	dml2_printf("DML::%s: WritebackFCLKChangeWatermark = %f\n", __func__, p->Watermark->WritebackFCLKChangeWatermark);
	dml2_printf("DML::%s: WritebackUrgentWatermark = %f\n", __func__, p->Watermark->WritebackUrgentWatermark);
	dml2_printf("DML::%s: USRRetrainingRequired = %u\n", __func__, p->USRRetrainingRequired);
	dml2_printf("DML::%s: USRRetrainingLatency = %f\n", __func__, p->mmSOCParameters.USRRetrainingLatency);
#endif

	s->TotalPixelBW = 0.0;
	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		double h_total = (double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total;
		double pixel_clock_mhz = p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000.0;
		double v_ratio = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		double v_ratio_c = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		s->TotalPixelBW = s->TotalPixelBW + p->DPPPerSurface[k]
			* (p->SwathWidthY[k] * p->BytePerPixelDETY[k] * v_ratio + p->SwathWidthC[k] * p->BytePerPixelDETC[k] * v_ratio_c) / (h_total / pixel_clock_mhz);
	}

	*p->global_fclk_change_supported = true;
	*p->global_dram_clock_change_supported = true;

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		double h_total = (double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total;
		double pixel_clock_mhz = p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000.0;
		double v_ratio = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		double v_ratio_c = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		double v_taps = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps;
		double v_taps_c = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps;
		double h_ratio = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio;
		double h_ratio_c = p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio;
		double LBBitPerPixel = 57;

		s->LBLatencyHidingSourceLinesY[k] = (unsigned int)(math_min2((double)p->MaxLineBufferLines, math_floor2((double)p->LineBufferSize / LBBitPerPixel / ((double)p->SwathWidthY[k] / math_max2(h_ratio, 1.0)), 1)) - (v_taps - 1));
		s->LBLatencyHidingSourceLinesC[k] = (unsigned int)(math_min2((double)p->MaxLineBufferLines, math_floor2((double)p->LineBufferSize / LBBitPerPixel / ((double)p->SwathWidthC[k] / math_max2(h_ratio_c, 1.0)), 1)) - (v_taps_c - 1));

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, MaxLineBufferLines = %u\n", __func__, k, p->MaxLineBufferLines);
		dml2_printf("DML::%s: k=%u, LineBufferSize = %u\n", __func__, k, p->LineBufferSize);
		dml2_printf("DML::%s: k=%u, LBBitPerPixel = %u\n", __func__, k, LBBitPerPixel);
		dml2_printf("DML::%s: k=%u, HRatio = %f\n", __func__, k, h_ratio);
		dml2_printf("DML::%s: k=%u, VTaps = %f\n", __func__, k, v_taps);
#endif

		s->EffectiveLBLatencyHidingY = s->LBLatencyHidingSourceLinesY[k] / v_ratio * (h_total / pixel_clock_mhz);
		s->EffectiveLBLatencyHidingC = s->LBLatencyHidingSourceLinesC[k] / v_ratio_c * (h_total / pixel_clock_mhz);

		s->EffectiveDETBufferSizeY = p->DETBufferSizeY[k];
		if (p->UnboundedRequestEnabled) {
			s->EffectiveDETBufferSizeY = s->EffectiveDETBufferSizeY + p->CompressedBufferSizeInkByte * 1024 * (p->SwathWidthY[k] * p->BytePerPixelDETY[k] * v_ratio) / (h_total / pixel_clock_mhz) / s->TotalPixelBW;
		}

		s->LinesInDETY[k] = (double)s->EffectiveDETBufferSizeY / p->BytePerPixelDETY[k] / p->SwathWidthY[k];
		s->LinesInDETYRoundedDownToSwath[k] = (unsigned int)(math_floor2(s->LinesInDETY[k], p->SwathHeightY[k]));
		s->FullDETBufferingTimeY = s->LinesInDETYRoundedDownToSwath[k] * (h_total / pixel_clock_mhz) / v_ratio;

		s->ActiveClockChangeLatencyHidingY = s->EffectiveLBLatencyHidingY + s->FullDETBufferingTimeY - ((double)p->DSTXAfterScaler[k] / h_total + (double)p->DSTYAfterScaler[k]) * h_total / pixel_clock_mhz;

		if (p->NumberOfActiveSurfaces > 1) {
			s->ActiveClockChangeLatencyHidingY = s->ActiveClockChangeLatencyHidingY - (1.0 - 1.0 / (double)p->NumberOfActiveSurfaces) * (double)p->SwathHeightY[k] * (double)h_total / pixel_clock_mhz / v_ratio;
		}

		if (p->BytePerPixelDETC[k] > 0) {
			s->LinesInDETC[k] = p->DETBufferSizeC[k] / p->BytePerPixelDETC[k] / p->SwathWidthC[k];
			s->LinesInDETCRoundedDownToSwath[k] = (unsigned int)(math_floor2(s->LinesInDETC[k], p->SwathHeightC[k]));
			s->FullDETBufferingTimeC = s->LinesInDETCRoundedDownToSwath[k] * (h_total / pixel_clock_mhz) / v_ratio_c;
			s->ActiveClockChangeLatencyHidingC = s->EffectiveLBLatencyHidingC + s->FullDETBufferingTimeC - ((double)p->DSTXAfterScaler[k] / (double)h_total + (double)p->DSTYAfterScaler[k]) * (double)h_total / pixel_clock_mhz;
			if (p->NumberOfActiveSurfaces > 1) {
				s->ActiveClockChangeLatencyHidingC = s->ActiveClockChangeLatencyHidingC - (1.0 - 1.0 / (double)p->NumberOfActiveSurfaces) * (double)p->SwathHeightC[k] * (double)h_total / pixel_clock_mhz / v_ratio_c;
			}
			s->ActiveClockChangeLatencyHiding = math_min2(s->ActiveClockChangeLatencyHidingY, s->ActiveClockChangeLatencyHidingC);
		} else {
			s->ActiveClockChangeLatencyHiding = s->ActiveClockChangeLatencyHidingY;
		}

		s->ActiveDRAMClockChangeLatencyMargin[k] = s->ActiveClockChangeLatencyHiding - p->Watermark->DRAMClockChangeWatermark;
		s->ActiveFCLKChangeLatencyMargin[k] = s->ActiveClockChangeLatencyHiding - p->Watermark->FCLKChangeWatermark;
		s->USRRetrainingLatencyMargin[k] = s->ActiveClockChangeLatencyHiding - p->Watermark->USRRetrainingWatermark;
		s->g6_temp_read_latency_margin[k] = s->ActiveClockChangeLatencyHiding - p->Watermark->temp_read_or_ppt_watermark_us;

		if (p->VActiveLatencyHidingMargin)
			p->VActiveLatencyHidingMargin[k] = s->ActiveDRAMClockChangeLatencyMargin[k];

		if (p->VActiveLatencyHidingUs)
			p->VActiveLatencyHidingUs[k] = s->ActiveClockChangeLatencyHiding;

		if (p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0) {
			s->WritebackLatencyHiding = (double)p->WritebackInterfaceBufferSize * 1024.0
				/ ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_height
					* (double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_width
					/ ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].input_height * (double)h_total / pixel_clock_mhz) * 4.0);
			if (p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].pixel_format == dml2_444_64) {
				s->WritebackLatencyHiding = s->WritebackLatencyHiding / 2;
			}
			s->WritebackDRAMClockChangeLatencyMargin = s->WritebackLatencyHiding - p->Watermark->WritebackDRAMClockChangeWatermark;

			s->WritebackFCLKChangeLatencyMargin = s->WritebackLatencyHiding - p->Watermark->WritebackFCLKChangeWatermark;

			s->ActiveDRAMClockChangeLatencyMargin[k] = math_min2(s->ActiveDRAMClockChangeLatencyMargin[k], s->WritebackDRAMClockChangeLatencyMargin);
			s->ActiveFCLKChangeLatencyMargin[k] = math_min2(s->ActiveFCLKChangeLatencyMargin[k], s->WritebackFCLKChangeLatencyMargin);
		}
		p->MaxActiveDRAMClockChangeLatencySupported[k] = dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[k]) ? 0 : (s->ActiveDRAMClockChangeLatencyMargin[k] + p->mmSOCParameters.DRAMClockChangeLatency);

		uclk_pstate_change_strategy = p->display_cfg->plane_descriptors[k].overrides.uclk_pstate_change_strategy;
		reserved_vblank_time_us = (double)p->display_cfg->plane_descriptors[k].overrides.reserved_vblank_time_ns / 1000;

		p->FCLKChangeSupport[k] = dml2_pstate_change_unsupported;
		if (s->ActiveFCLKChangeLatencyMargin[k] > 0)
			p->FCLKChangeSupport[k] = dml2_pstate_change_vactive;
		else if (reserved_vblank_time_us >= p->mmSOCParameters.FCLKChangeLatency)
			p->FCLKChangeSupport[k] = dml2_pstate_change_vblank;

		if (p->FCLKChangeSupport[k] == dml2_pstate_change_unsupported)
			*p->global_fclk_change_supported = false;

		p->DRAMClockChangeSupport[k] = dml2_pstate_change_unsupported;
		if (uclk_pstate_change_strategy == dml2_uclk_pstate_change_strategy_auto) {
			if (p->display_cfg->overrides.all_streams_blanked ||
					(s->ActiveDRAMClockChangeLatencyMargin[k] > 0 && reserved_vblank_time_us >= p->mmSOCParameters.DRAMClockChangeLatency))
				p->DRAMClockChangeSupport[k] = dml2_pstate_change_vblank_and_vactive;
			else if (s->ActiveDRAMClockChangeLatencyMargin[k] > 0)
				p->DRAMClockChangeSupport[k] = dml2_pstate_change_vactive;
			else if (reserved_vblank_time_us >= p->mmSOCParameters.DRAMClockChangeLatency)
				p->DRAMClockChangeSupport[k] = dml2_pstate_change_vblank;
		} else if (uclk_pstate_change_strategy == dml2_uclk_pstate_change_strategy_force_vactive && s->ActiveDRAMClockChangeLatencyMargin[k] > 0)
			p->DRAMClockChangeSupport[k] = dml2_pstate_change_vactive;
		else if (uclk_pstate_change_strategy == dml2_uclk_pstate_change_strategy_force_vblank && reserved_vblank_time_us >= p->mmSOCParameters.DRAMClockChangeLatency)
			p->DRAMClockChangeSupport[k] = dml2_pstate_change_vblank;
		else if (uclk_pstate_change_strategy == dml2_uclk_pstate_change_strategy_force_drr)
			p->DRAMClockChangeSupport[k] = dml2_pstate_change_drr;
		else if (uclk_pstate_change_strategy == dml2_uclk_pstate_change_strategy_force_mall_svp)
			p->DRAMClockChangeSupport[k] = dml2_pstate_change_mall_svp;
		else if (uclk_pstate_change_strategy == dml2_uclk_pstate_change_strategy_force_mall_full_frame)
			p->DRAMClockChangeSupport[k] = dml2_pstate_change_mall_full_frame;

		if (p->DRAMClockChangeSupport[k] == dml2_pstate_change_unsupported)
			*p->global_dram_clock_change_supported = false;

		s->dst_y_pstate = (unsigned int)(math_ceil2((p->mmSOCParameters.DRAMClockChangeLatency + p->mmSOCParameters.UrgentLatency) / (h_total / pixel_clock_mhz), 1));
		s->src_y_pstate_l = (unsigned int)(math_ceil2(s->dst_y_pstate * v_ratio, p->SwathHeightY[k]));
		s->src_y_ahead_l = (unsigned int)(math_floor2(p->DETBufferSizeY[k] / p->BytePerPixelDETY[k] / p->SwathWidthY[k], p->SwathHeightY[k]) + s->LBLatencyHidingSourceLinesY[k]);
		s->sub_vp_lines_l = s->src_y_pstate_l + s->src_y_ahead_l + p->meta_row_height_l[k];

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, DETBufferSizeY = %u\n", __func__, k, p->DETBufferSizeY[k]);
		dml2_printf("DML::%s: k=%u, BytePerPixelDETY = %f\n", __func__, k, p->BytePerPixelDETY[k]);
		dml2_printf("DML::%s: k=%u, SwathWidthY = %u\n", __func__, k, p->SwathWidthY[k]);
		dml2_printf("DML::%s: k=%u, SwathHeightY = %u\n", __func__, k, p->SwathHeightY[k]);
		dml2_printf("DML::%s: k=%u, LBLatencyHidingSourceLinesY = %u\n", __func__, k, s->LBLatencyHidingSourceLinesY[k]);
		dml2_printf("DML::%s: k=%u, dst_y_pstate = %u\n", __func__, k, s->dst_y_pstate);
		dml2_printf("DML::%s: k=%u, src_y_pstate_l = %u\n", __func__, k, s->src_y_pstate_l);
		dml2_printf("DML::%s: k=%u, src_y_ahead_l = %u\n", __func__, k, s->src_y_ahead_l);
		dml2_printf("DML::%s: k=%u, meta_row_height_l = %u\n", __func__, k, p->meta_row_height_l[k]);
		dml2_printf("DML::%s: k=%u, sub_vp_lines_l = %u\n", __func__, k, s->sub_vp_lines_l);
#endif
		p->SubViewportLinesNeededInMALL[k] = s->sub_vp_lines_l;

		if (p->BytePerPixelDETC[k] > 0) {
			s->src_y_pstate_c = (unsigned int)(math_ceil2(s->dst_y_pstate * v_ratio_c, p->SwathHeightC[k]));
			s->src_y_ahead_c = (unsigned int)(math_floor2(p->DETBufferSizeC[k] / p->BytePerPixelDETC[k] / p->SwathWidthC[k], p->SwathHeightC[k]) + s->LBLatencyHidingSourceLinesC[k]);
			s->sub_vp_lines_c = s->src_y_pstate_c + s->src_y_ahead_c + p->meta_row_height_c[k];

			if (dml_is_420(p->display_cfg->plane_descriptors[k].pixel_format))
				p->SubViewportLinesNeededInMALL[k] = (unsigned int)(math_max2(s->sub_vp_lines_l, 2 * s->sub_vp_lines_c));
			else
				p->SubViewportLinesNeededInMALL[k] = (unsigned int)(math_max2(s->sub_vp_lines_l, s->sub_vp_lines_c));

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u, meta_row_height_c = %u\n", __func__, k, p->meta_row_height_c[k]);
			dml2_printf("DML::%s: k=%u, src_y_pstate_c = %u\n", __func__, k, s->src_y_pstate_c);
			dml2_printf("DML::%s: k=%u, src_y_ahead_c = %u\n", __func__, k, s->src_y_ahead_c);
			dml2_printf("DML::%s: k=%u, sub_vp_lines_c = %u\n", __func__, k, s->sub_vp_lines_c);
#endif
		}
	}

	*p->g6_temp_read_support = true;
	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if ((!dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[k])) &&
			(s->g6_temp_read_latency_margin[k] < 0)) {
			*p->g6_temp_read_support = false;
		}
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if ((!dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[k])) && ((!FoundCriticalSurface)
			|| ((s->ActiveFCLKChangeLatencyMargin[k] + p->mmSOCParameters.FCLKChangeLatency) < *p->MaxActiveFCLKChangeLatencySupported))) {
			FoundCriticalSurface = true;
			*p->MaxActiveFCLKChangeLatencySupported = s->ActiveFCLKChangeLatencyMargin[k] + p->mmSOCParameters.FCLKChangeLatency;
		}
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: DRAMClockChangeSupport = %u\n", __func__, *p->global_dram_clock_change_supported);
	dml2_printf("DML::%s: FCLKChangeSupport = %u\n", __func__, *p->global_fclk_change_supported);
	dml2_printf("DML::%s: MaxActiveFCLKChangeLatencySupported = %f\n", __func__, *p->MaxActiveFCLKChangeLatencySupported);
	dml2_printf("DML::%s: USRRetrainingSupport = %u\n", __func__, *p->USRRetrainingSupport);
#endif
}

static void calculate_bytes_to_fetch_required_to_hide_latency(
		struct dml2_core_calcs_calculate_bytes_to_fetch_required_to_hide_latency_params *p)
{
	unsigned int dst_lines_to_hide;
	unsigned int src_lines_to_hide_l;
	unsigned int src_lines_to_hide_c;
	unsigned int plane_index;
	unsigned int stream_index;

	for (plane_index = 0; plane_index < p->num_active_planes; plane_index++) {
		if (dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[plane_index]))
			continue;

		stream_index = p->display_cfg->plane_descriptors[plane_index].stream_index;

		dst_lines_to_hide = (unsigned int)math_ceil(p->latency_to_hide_us /
			((double)p->display_cfg->stream_descriptors[stream_index].timing.h_total /
				(double)p->display_cfg->stream_descriptors[stream_index].timing.pixel_clock_khz * 1000.0));

		src_lines_to_hide_l = (unsigned int)math_ceil2(p->display_cfg->plane_descriptors[plane_index].composition.scaler_info.plane0.v_ratio * dst_lines_to_hide,
			p->swath_height_l[plane_index]);
		p->bytes_required_l[plane_index] = src_lines_to_hide_l * p->num_of_dpp[plane_index] * p->swath_width_l[plane_index] * p->byte_per_pix_l[plane_index];

		src_lines_to_hide_c = (unsigned int)math_ceil2(p->display_cfg->plane_descriptors[plane_index].composition.scaler_info.plane1.v_ratio * dst_lines_to_hide,
			p->swath_height_c[plane_index]);
		p->bytes_required_c[plane_index] = src_lines_to_hide_c * p->num_of_dpp[plane_index] * p->swath_width_c[plane_index] * p->byte_per_pix_c[plane_index];

		if (p->display_cfg->plane_descriptors[plane_index].surface.dcc.enable && p->mrq_present) {
			p->bytes_required_l[plane_index] += (unsigned int)math_ceil((double)src_lines_to_hide_l / p->meta_row_height_l[plane_index]) * p->meta_row_bytes_per_row_ub_l[plane_index];
			if (p->meta_row_height_c[plane_index]) {
				p->bytes_required_c[plane_index] += (unsigned int)math_ceil((double)src_lines_to_hide_c / p->meta_row_height_c[plane_index]) * p->meta_row_bytes_per_row_ub_c[plane_index];
			}
		}

		if (p->display_cfg->gpuvm_enable == true) {
			p->bytes_required_l[plane_index] += (unsigned int)math_ceil((double)src_lines_to_hide_l / p->dpte_row_height_l[plane_index]) * p->dpte_bytes_per_row_l[plane_index];
			if (p->dpte_row_height_c[plane_index]) {
				p->bytes_required_c[plane_index] += (unsigned int)math_ceil((double)src_lines_to_hide_c / p->dpte_row_height_c[plane_index]) * p->dpte_bytes_per_row_c[plane_index];
			}
		}
	}
}

static void calculate_vactive_det_fill_latency(
		const struct dml2_display_cfg *display_cfg,
		unsigned int num_active_planes,
		unsigned int bytes_required_l[],
		unsigned int bytes_required_c[],
		double dcc_dram_bw_nom_overhead_factor_p0[],
		double dcc_dram_bw_nom_overhead_factor_p1[],
		double surface_read_bw_l[],
		double surface_read_bw_c[],
		double (*surface_avg_vactive_required_bw)[dml2_core_internal_bw_max][DML2_MAX_PLANES],
		double (*surface_peak_required_bw)[dml2_core_internal_bw_max][DML2_MAX_PLANES],
		/* output */
		double vactive_det_fill_delay_us[])
{
	double effective_excess_bandwidth;
	double effective_excess_bandwidth_l;
	double effective_excess_bandwidth_c;
	double adj_factor;
	unsigned int plane_index;
	unsigned int soc_state;
	unsigned int bw_type;

	for (plane_index = 0; plane_index < num_active_planes; plane_index++) {
		if (dml_is_phantom_pipe(&display_cfg->plane_descriptors[plane_index]))
			continue;

		vactive_det_fill_delay_us[plane_index] = 0.0;
		for (soc_state = 0; soc_state < dml2_core_internal_soc_state_max; soc_state++) {
			for (bw_type = 0; bw_type < dml2_core_internal_bw_max; bw_type++) {
				effective_excess_bandwidth = (surface_peak_required_bw[soc_state][bw_type][plane_index] - surface_avg_vactive_required_bw[soc_state][bw_type][plane_index]);

				/* luma */
				adj_factor = bw_type == dml2_core_internal_bw_dram ? dcc_dram_bw_nom_overhead_factor_p0[plane_index] : 1.0;

				effective_excess_bandwidth_l = effective_excess_bandwidth * surface_read_bw_l[plane_index] / (surface_read_bw_l[plane_index] + surface_read_bw_c[plane_index]) / adj_factor;
				if (effective_excess_bandwidth_l > 0.0) {
					vactive_det_fill_delay_us[plane_index] = math_max2(vactive_det_fill_delay_us[plane_index], bytes_required_l[plane_index] / effective_excess_bandwidth_l);
				}

				/* chroma */
				adj_factor = bw_type == dml2_core_internal_bw_dram ? dcc_dram_bw_nom_overhead_factor_p1[plane_index] : 1.0;

				effective_excess_bandwidth_c = effective_excess_bandwidth * surface_read_bw_c[plane_index] / (surface_read_bw_l[plane_index] + surface_read_bw_c[plane_index]) / adj_factor;
				if (effective_excess_bandwidth_c > 0.0) {
					vactive_det_fill_delay_us[plane_index] = math_max2(vactive_det_fill_delay_us[plane_index], bytes_required_c[plane_index] / effective_excess_bandwidth_c);
				}
			}
		}
	}
}

static void calculate_excess_vactive_bandwidth_required(
	const struct dml2_display_cfg *display_cfg,
	unsigned int num_active_planes,
	unsigned int bytes_required_l[],
	unsigned int bytes_required_c[],
	/* outputs */
	double excess_vactive_fill_bw_l[],
	double excess_vactive_fill_bw_c[])
{
	unsigned int plane_index;

	for (plane_index = 0; plane_index < num_active_planes; plane_index++) {
		if (dml_is_phantom_pipe(&display_cfg->plane_descriptors[plane_index]))
			continue;

		excess_vactive_fill_bw_l[plane_index] = 0.0;
		excess_vactive_fill_bw_c[plane_index] = 0.0;

		if (display_cfg->plane_descriptors[plane_index].overrides.max_vactive_det_fill_delay_us > 0) {
			excess_vactive_fill_bw_l[plane_index] = (double)bytes_required_l[plane_index] / (double)display_cfg->plane_descriptors[plane_index].overrides.max_vactive_det_fill_delay_us;
			excess_vactive_fill_bw_c[plane_index] = (double)bytes_required_c[plane_index] / (double)display_cfg->plane_descriptors[plane_index].overrides.max_vactive_det_fill_delay_us;
		}
	}
}

static double uclk_khz_to_dram_bw_mbps(unsigned long uclk_khz, const struct dml2_dram_params *dram_config)
{
	double bw_mbps = 0;
	bw_mbps = ((double)uclk_khz * dram_config->channel_count * dram_config->channel_width_bytes * dram_config->transactions_per_clock) / 1000.0;

	return bw_mbps;
}

static double dram_bw_kbps_to_uclk_mhz(unsigned long long bw_kbps, const struct dml2_dram_params *dram_config)
{
	double uclk_mhz = 0;

	uclk_mhz = (double)bw_kbps / (dram_config->channel_count * dram_config->channel_width_bytes * dram_config->transactions_per_clock) / 1000.0;

	return uclk_mhz;
}

static unsigned int get_qos_param_index(unsigned long uclk_freq_khz, const struct dml2_dcn4_uclk_dpm_dependent_qos_params *per_uclk_dpm_params)
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

static unsigned int get_active_min_uclk_dpm_index(unsigned long uclk_freq_khz, const struct dml2_soc_state_table *clk_table)
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

static unsigned int get_pipe_flip_bytes(
		double hostvm_inefficiency_factor,
		unsigned int vm_bytes,
		unsigned int dpte_row_bytes,
		unsigned int meta_row_bytes)
{
	unsigned int flip_bytes = 0;

	flip_bytes += (unsigned int) ((vm_bytes * hostvm_inefficiency_factor) + 2*meta_row_bytes);
	flip_bytes += (unsigned int) (2*dpte_row_bytes * hostvm_inefficiency_factor);

	return flip_bytes;
}

static void calculate_hostvm_inefficiency_factor(
		double *HostVMInefficiencyFactor,
		double *HostVMInefficiencyFactorPrefetch,

		bool gpuvm_enable,
		bool hostvm_enable,
		unsigned int remote_iommu_outstanding_translations,
		unsigned int max_outstanding_reqs,
		double urg_bandwidth_avail_active_pixel_and_vm,
		double urg_bandwidth_avail_active_vm_only)
{
		*HostVMInefficiencyFactor = 1;
		*HostVMInefficiencyFactorPrefetch = 1;

		if (gpuvm_enable && hostvm_enable) {
			*HostVMInefficiencyFactor = urg_bandwidth_avail_active_pixel_and_vm / urg_bandwidth_avail_active_vm_only;
			*HostVMInefficiencyFactorPrefetch = *HostVMInefficiencyFactor;

			if ((*HostVMInefficiencyFactorPrefetch < 4) && (remote_iommu_outstanding_translations < max_outstanding_reqs))
				*HostVMInefficiencyFactorPrefetch = 4;
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: urg_bandwidth_avail_active_pixel_and_vm = %f\n", __func__, urg_bandwidth_avail_active_pixel_and_vm);
			dml2_printf("DML::%s: urg_bandwidth_avail_active_vm_only = %f\n", __func__, urg_bandwidth_avail_active_vm_only);
			dml2_printf("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, *HostVMInefficiencyFactor);
			dml2_printf("DML::%s: HostVMInefficiencyFactorPrefetch = %f\n", __func__, *HostVMInefficiencyFactorPrefetch);
#endif
		}
}

struct dml2_core_internal_g6_temp_read_blackouts_table {
	struct {
		unsigned int uclk_khz;
		unsigned int blackout_us;
	} entries[DML_MAX_CLK_TABLE_SIZE];
};

struct dml2_core_internal_g6_temp_read_blackouts_table core_dcn4_g6_temp_read_blackout_table = {
	.entries = {
		{
			.uclk_khz = 96000,
			.blackout_us = 23,
		},
		{
			.uclk_khz = 435000,
			.blackout_us = 10,
		},
		{
			.uclk_khz = 521000,
			.blackout_us = 10,
		},
		{
			.uclk_khz = 731000,
			.blackout_us = 8,
		},
		{
			.uclk_khz = 822000,
			.blackout_us = 8,
		},
		{
			.uclk_khz = 962000,
			.blackout_us = 5,
		},
		{
			.uclk_khz = 1069000,
			.blackout_us = 5,
		},
		{
			.uclk_khz = 1187000,
			.blackout_us = 5,
		},
	},
};

static double get_g6_temp_read_blackout_us(
	struct dml2_soc_bb *soc,
	unsigned int uclk_freq_khz,
	unsigned int min_clk_index)
{
	unsigned int i;
	unsigned int blackout_us = core_dcn4_g6_temp_read_blackout_table.entries[0].blackout_us;

	if (soc->power_management_parameters.g6_temp_read_blackout_us[0] > 0.0) {
		/* overrides are present in the SoC BB */
		return soc->power_management_parameters.g6_temp_read_blackout_us[min_clk_index];
	}

	/* use internal table */
	blackout_us = core_dcn4_g6_temp_read_blackout_table.entries[0].blackout_us;

	for (i = 0; i < DML_MAX_CLK_TABLE_SIZE; i++) {
		if (uclk_freq_khz < core_dcn4_g6_temp_read_blackout_table.entries[i].uclk_khz ||
			core_dcn4_g6_temp_read_blackout_table.entries[i].uclk_khz == 0) {
			break;
		}

		blackout_us = core_dcn4_g6_temp_read_blackout_table.entries[i].blackout_us;
	}

	return (double)blackout_us;
}

static double get_max_urgent_latency_us(
	struct dml2_dcn4x_soc_qos_params *dcn4x,
	double uclk_freq_mhz,
	double FabricClock,
	unsigned int min_clk_index)
{
	double latency;
	latency = dcn4x->per_uclk_dpm_params[min_clk_index].maximum_latency_when_urgent_uclk_cycles / uclk_freq_mhz
		* (1 + dcn4x->umc_max_latency_margin / 100.0)
		+ dcn4x->mall_overhead_fclk_cycles / FabricClock
		+ dcn4x->max_round_trip_to_furthest_cs_fclk_cycles / FabricClock
		* (1 + dcn4x->fabric_max_transport_latency_margin / 100.0);
	return latency;
}

static void calculate_pstate_keepout_dst_lines(
		const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_watermarks *watermarks,
		unsigned int pstate_keepout_dst_lines[])
{
	const struct dml2_stream_parameters *stream_descriptor;
	unsigned int i;

	for (i = 0; i < display_cfg->num_planes; i++) {
		if (!dml_is_phantom_pipe(&display_cfg->plane_descriptors[i])) {
			stream_descriptor = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[i].stream_index];

			pstate_keepout_dst_lines[i] =
					(unsigned int)math_ceil(watermarks->DRAMClockChangeWatermark / ((double)stream_descriptor->timing.h_total * 1000.0 / (double)stream_descriptor->timing.pixel_clock_khz));

			if (pstate_keepout_dst_lines[i] > stream_descriptor->timing.v_total - 1) {
				pstate_keepout_dst_lines[i] = stream_descriptor->timing.v_total - 1;
			}
		}
	}
}

static bool dml_core_mode_support(struct dml2_core_calcs_mode_support_ex *in_out_params)
{
	struct dml2_core_internal_display_mode_lib *mode_lib = in_out_params->mode_lib;
	const struct dml2_display_cfg *display_cfg = in_out_params->in_display_cfg;
	const struct dml2_mcg_min_clock_table *min_clk_table = in_out_params->min_clk_table;

#if defined(__DML_VBA_DEBUG__)
	double old_ReadBandwidthLuma;
	double old_ReadBandwidthChroma;
#endif
	double outstanding_latency_us = 0;
	double min_return_bw_for_latency;

	struct dml2_core_calcs_mode_support_locals *s = &mode_lib->scratch.dml_core_mode_support_locals;
	struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params *CalculateWatermarks_params = &mode_lib->scratch.CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params;
	struct dml2_core_calcs_CalculateVMRowAndSwath_params *CalculateVMRowAndSwath_params = &mode_lib->scratch.CalculateVMRowAndSwath_params;
	struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params *CalculateSwathAndDETConfiguration_params = &mode_lib->scratch.CalculateSwathAndDETConfiguration_params;
	struct dml2_core_calcs_CalculatePrefetchSchedule_params *CalculatePrefetchSchedule_params = &mode_lib->scratch.CalculatePrefetchSchedule_params;
#ifdef DML_GLOBAL_PREFETCH_CHECK
	struct dml2_core_calcs_CheckGlobalPrefetchAdmissibility_params *CheckGlobalPrefetchAdmissibility_params = &mode_lib->scratch.CheckGlobalPrefetchAdmissibility_params;
#endif
	struct dml2_core_calcs_calculate_tdlut_setting_params *calculate_tdlut_setting_params = &mode_lib->scratch.calculate_tdlut_setting_params;
	struct dml2_core_calcs_calculate_mcache_setting_params *calculate_mcache_setting_params = &mode_lib->scratch.calculate_mcache_setting_params;
	struct dml2_core_calcs_calculate_peak_bandwidth_required_params *calculate_peak_bandwidth_params = &mode_lib->scratch.calculate_peak_bandwidth_params;
	struct dml2_core_calcs_calculate_bytes_to_fetch_required_to_hide_latency_params *calculate_bytes_to_fetch_required_to_hide_latency_params = &mode_lib->scratch.calculate_bytes_to_fetch_required_to_hide_latency_params;
	unsigned int k, m, n;

	memset(&mode_lib->scratch, 0, sizeof(struct dml2_core_internal_scratch));
	memset(&mode_lib->ms, 0, sizeof(struct dml2_core_internal_mode_support));

	mode_lib->ms.num_active_planes = display_cfg->num_planes;
	get_stream_output_bpp(s->OutputBpp, display_cfg);

	mode_lib->ms.state_idx = in_out_params->min_clk_index;
	mode_lib->ms.SOCCLK = ((double)mode_lib->soc.clk_table.socclk.clk_values_khz[0] / 1000);
	mode_lib->ms.DCFCLK = ((double)min_clk_table->dram_bw_table.entries[in_out_params->min_clk_index].min_dcfclk_khz / 1000);
	mode_lib->ms.FabricClock = ((double)min_clk_table->dram_bw_table.entries[in_out_params->min_clk_index].min_fclk_khz / 1000);
	mode_lib->ms.MaxDCFCLK = (double)min_clk_table->max_clocks_khz.dcfclk / 1000;
	mode_lib->ms.MaxFabricClock = (double)min_clk_table->max_clocks_khz.fclk / 1000;
	mode_lib->ms.max_dispclk_freq_mhz = (double)min_clk_table->max_clocks_khz.dispclk / 1000;
	mode_lib->ms.max_dscclk_freq_mhz = (double)min_clk_table->max_clocks_khz.dscclk / 1000;
	mode_lib->ms.max_dppclk_freq_mhz = (double)min_clk_table->max_clocks_khz.dppclk / 1000;
	mode_lib->ms.uclk_freq_mhz = dram_bw_kbps_to_uclk_mhz(min_clk_table->dram_bw_table.entries[in_out_params->min_clk_index].pre_derate_dram_bw_kbps, &mode_lib->soc.clk_table.dram_config);
	mode_lib->ms.dram_bw_mbps = ((double)min_clk_table->dram_bw_table.entries[in_out_params->min_clk_index].pre_derate_dram_bw_kbps / 1000);
	mode_lib->ms.max_dram_bw_mbps = ((double)min_clk_table->dram_bw_table.entries[min_clk_table->dram_bw_table.num_entries - 1].pre_derate_dram_bw_kbps / 1000);
	mode_lib->ms.qos_param_index = get_qos_param_index((unsigned int) (mode_lib->ms.uclk_freq_mhz * 1000.0), mode_lib->soc.qos_parameters.qos_params.dcn4x.per_uclk_dpm_params);
	mode_lib->ms.active_min_uclk_dpm_index = get_active_min_uclk_dpm_index((unsigned int) (mode_lib->ms.uclk_freq_mhz * 1000.0), &mode_lib->soc.clk_table);

#if defined(__DML_VBA_DEBUG__)
	dml2_printf("DML::%s: --- START --- \n", __func__);
	dml2_printf("DML::%s: num_active_planes = %u\n", __func__, mode_lib->ms.num_active_planes);
	dml2_printf("DML::%s: min_clk_index = %0d\n", __func__, in_out_params->min_clk_index);
	dml2_printf("DML::%s: qos_param_index = %0d\n", __func__, mode_lib->ms.qos_param_index);
	dml2_printf("DML::%s: SOCCLK = %f\n", __func__, mode_lib->ms.SOCCLK);
	dml2_printf("DML::%s: dram_bw_mbps = %f\n", __func__, mode_lib->ms.dram_bw_mbps);
	dml2_printf("DML::%s: uclk_freq_mhz = %f\n", __func__, mode_lib->ms.uclk_freq_mhz);
	dml2_printf("DML::%s: DCFCLK = %f\n", __func__, mode_lib->ms.DCFCLK);
	dml2_printf("DML::%s: FabricClock = %f\n", __func__, mode_lib->ms.FabricClock);
	dml2_printf("DML::%s: MaxDCFCLK = %f\n", __func__, mode_lib->ms.MaxDCFCLK);
	dml2_printf("DML::%s: max_dispclk_freq_mhz = %f\n", __func__, mode_lib->ms.max_dispclk_freq_mhz);
	dml2_printf("DML::%s: max_dscclk_freq_mhz = %f\n", __func__, mode_lib->ms.max_dscclk_freq_mhz);
	dml2_printf("DML::%s: max_dppclk_freq_mhz = %f\n", __func__, mode_lib->ms.max_dppclk_freq_mhz);
	dml2_printf("DML::%s: MaxFabricClock = %f\n", __func__, mode_lib->ms.MaxFabricClock);
	dml2_printf("DML::%s: ip.compressed_buffer_segment_size_in_kbytes = %u\n", __func__, mode_lib->ip.compressed_buffer_segment_size_in_kbytes);
	dml2_printf("DML::%s: ip.dcn_mrq_present = %u\n", __func__, mode_lib->ip.dcn_mrq_present);

	for (k = 0; k < mode_lib->ms.num_active_planes; k++)
		dml2_printf("DML::%s: plane_%d: reserved_vblank_time_ns = %u\n", __func__, k, display_cfg->plane_descriptors[k].overrides.reserved_vblank_time_ns);
#endif

	CalculateMaxDETAndMinCompressedBufferSize(
		mode_lib->ip.config_return_buffer_size_in_kbytes,
		mode_lib->ip.config_return_buffer_segment_size_in_kbytes,
		mode_lib->ip.rob_buffer_size_kbytes,
		mode_lib->ip.max_num_dpp,
		display_cfg->overrides.hw.force_nom_det_size_kbytes.enable,
		display_cfg->overrides.hw.force_nom_det_size_kbytes.value,
		mode_lib->ip.dcn_mrq_present,

		/* Output */
		&mode_lib->ms.MaxTotalDETInKByte,
		&mode_lib->ms.NomDETInKByte,
		&mode_lib->ms.MinCompressedBufferSizeInKByte);

	PixelClockAdjustmentForProgressiveToInterlaceUnit(display_cfg, mode_lib->ip.ptoi_supported, s->PixelClockBackEnd);

	/*MODE SUPPORT, VOLTAGE STATE AND SOC CONFIGURATION*/

	/*Scale Ratio, taps Support Check*/
	mode_lib->ms.support.ScaleRatioAndTapsSupport = true;
	// Many core tests are still setting scaling parameters "incorrectly"
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (display_cfg->plane_descriptors[k].composition.scaler_info.enabled == false
			&& (dml_is_420(display_cfg->plane_descriptors[k].pixel_format)
				|| display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio != 1.0
				|| display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_taps != 1.0
				|| display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio != 1.0
				|| display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps != 1.0)) {
			mode_lib->ms.support.ScaleRatioAndTapsSupport = false;
		} else if (display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps < 1.0 || display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps > 8.0
			|| display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_taps < 1.0 || display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_taps > 8.0
			|| (display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_taps > 1.0 && (display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_taps % 2) == 1)
			|| display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio > mode_lib->ip.max_hscl_ratio
			|| display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio > mode_lib->ip.max_vscl_ratio
			|| display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio > display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_taps
			|| display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio > display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps
			|| (dml_is_420(display_cfg->plane_descriptors[k].pixel_format)
				&& (display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps < 1 || display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps > 8 ||
					display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_taps < 1 || display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_taps > 8 ||
					(display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_taps > 1 && display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_taps % 2 == 1) ||
					display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio > mode_lib->ip.max_hscl_ratio ||
					display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio > mode_lib->ip.max_vscl_ratio ||
					display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio > display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_taps ||
					display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio > display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps))) {
			mode_lib->ms.support.ScaleRatioAndTapsSupport = false;
		}
	}

	/*Source Format, Pixel Format and Scan Support Check*/
	mode_lib->ms.support.SourceFormatPixelAndScanSupport = true;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (display_cfg->plane_descriptors[k].surface.tiling == dml2_sw_linear && dml_is_vertical_rotation(display_cfg->plane_descriptors[k].composition.rotation_angle)) {
			mode_lib->ms.support.SourceFormatPixelAndScanSupport = false;
		}
	}

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		CalculateBytePerPixelAndBlockSizes(
			display_cfg->plane_descriptors[k].pixel_format,
			display_cfg->plane_descriptors[k].surface.tiling,
			display_cfg->plane_descriptors[k].surface.plane0.pitch,
			display_cfg->plane_descriptors[k].surface.plane1.pitch,

			/* Output */
			&mode_lib->ms.BytePerPixelY[k],
			&mode_lib->ms.BytePerPixelC[k],
			&mode_lib->ms.BytePerPixelInDETY[k],
			&mode_lib->ms.BytePerPixelInDETC[k],
			&mode_lib->ms.Read256BlockHeightY[k],
			&mode_lib->ms.Read256BlockHeightC[k],
			&mode_lib->ms.Read256BlockWidthY[k],
			&mode_lib->ms.Read256BlockWidthC[k],
			&mode_lib->ms.MacroTileHeightY[k],
			&mode_lib->ms.MacroTileHeightC[k],
			&mode_lib->ms.MacroTileWidthY[k],
			&mode_lib->ms.MacroTileWidthC[k],
			&mode_lib->ms.surf_linear128_l[k],
			&mode_lib->ms.surf_linear128_c[k]);
	}

	/*Bandwidth Support Check*/
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (!dml_is_vertical_rotation(display_cfg->plane_descriptors[k].composition.rotation_angle)) {
			mode_lib->ms.SwathWidthYSingleDPP[k] = display_cfg->plane_descriptors[k].composition.viewport.plane0.width;
			mode_lib->ms.SwathWidthCSingleDPP[k] = display_cfg->plane_descriptors[k].composition.viewport.plane1.width;
		} else {
			mode_lib->ms.SwathWidthYSingleDPP[k] = display_cfg->plane_descriptors[k].composition.viewport.plane0.height;
			mode_lib->ms.SwathWidthCSingleDPP[k] = display_cfg->plane_descriptors[k].composition.viewport.plane1.height;
		}
	}

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		mode_lib->ms.vactive_sw_bw_l[k] = mode_lib->ms.SwathWidthYSingleDPP[k] * math_ceil2(mode_lib->ms.BytePerPixelY[k], 1.0) / (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		mode_lib->ms.vactive_sw_bw_c[k] = mode_lib->ms.SwathWidthCSingleDPP[k] * math_ceil2(mode_lib->ms.BytePerPixelC[k], 2.0) / (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;

		mode_lib->ms.cursor_bw[k] = display_cfg->plane_descriptors[k].cursor.num_cursors * display_cfg->plane_descriptors[k].cursor.cursor_width *
			display_cfg->plane_descriptors[k].cursor.cursor_bpp / 8.0 / (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000));

#ifdef __DML_VBA_DEBUG__
		old_ReadBandwidthLuma = mode_lib->ms.SwathWidthYSingleDPP[k] * math_ceil2(mode_lib->ms.BytePerPixelInDETY[k], 1.0) / (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		old_ReadBandwidthChroma = mode_lib->ms.SwathWidthYSingleDPP[k] / 2 * math_ceil2(mode_lib->ms.BytePerPixelInDETC[k], 2.0) / (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio / 2.0;
		dml2_printf("DML::%s: k=%u, old_ReadBandwidthLuma = %f\n", __func__, k, old_ReadBandwidthLuma);
		dml2_printf("DML::%s: k=%u, old_ReadBandwidthChroma = %f\n", __func__, k, old_ReadBandwidthChroma);
		dml2_printf("DML::%s: k=%u, vactive_sw_bw_l = %f\n", __func__, k, mode_lib->ms.vactive_sw_bw_l[k]);
		dml2_printf("DML::%s: k=%u, vactive_sw_bw_c = %f\n", __func__, k, mode_lib->ms.vactive_sw_bw_c[k]);
#endif
	}

	// Writeback bandwidth
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0 && display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].pixel_format == dml2_444_64) {
			mode_lib->ms.WriteBandwidth[k][0] = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_height
				* display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_width
				/ (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].input_height
					* display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total
					/ ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * 8.0;
		} else if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0) {
			mode_lib->ms.WriteBandwidth[k][0] = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_height
				* display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_width
				/ (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].input_height
					* display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total
					/ ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * 4.0;
		} else {
			mode_lib->ms.WriteBandwidth[k][0] = 0.0;
		}
	}

	/*Writeback Latency support check*/
	mode_lib->ms.support.WritebackLatencySupport = true;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0 &&
			(mode_lib->ms.WriteBandwidth[k][0] > mode_lib->ip.writeback_interface_buffer_size_kbytes * 1024 / ((double)mode_lib->soc.qos_parameters.writeback.base_latency_us))) {
			mode_lib->ms.support.WritebackLatencySupport = false;
		}
	}


	/* Writeback Scale Ratio and Taps Support Check */
	mode_lib->ms.support.WritebackScaleRatioAndTapsSupport = true;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0) {
			if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].h_ratio > mode_lib->ip.writeback_max_hscl_ratio
				|| display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_ratio > mode_lib->ip.writeback_max_vscl_ratio
				|| display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].h_ratio < mode_lib->ip.writeback_min_hscl_ratio
				|| display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_ratio < mode_lib->ip.writeback_min_vscl_ratio
				|| display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].h_taps > (unsigned int) mode_lib->ip.writeback_max_hscl_taps
				|| display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_taps > (unsigned int) mode_lib->ip.writeback_max_vscl_taps
				|| display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].h_ratio > (unsigned int)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].h_taps
				|| display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_ratio > (unsigned int)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_taps
				|| (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].h_taps > 2.0 && ((display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].h_taps % 2) == 1))) {
				mode_lib->ms.support.WritebackScaleRatioAndTapsSupport = false;
			}
			if (2.0 * display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_height * (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_taps - 1) * 57 > mode_lib->ip.writeback_line_buffer_buffer_size) {
				mode_lib->ms.support.WritebackScaleRatioAndTapsSupport = false;
			}
		}
	}

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		CalculateSinglePipeDPPCLKAndSCLThroughput(
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio,
			mode_lib->ip.max_dchub_pscl_bw_pix_per_clk,
			mode_lib->ip.max_pscl_lb_bw_pix_per_clk,
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000),
			display_cfg->plane_descriptors[k].pixel_format,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_taps,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_taps,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps,
			/* Output */
			&mode_lib->ms.PSCL_FACTOR[k],
			&mode_lib->ms.PSCL_FACTOR_CHROMA[k],
			&mode_lib->ms.MinDPPCLKUsingSingleDPP[k]);
	}

	// Max Viewport Size support
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		if (display_cfg->plane_descriptors[k].surface.tiling == dml2_sw_linear) {
			s->MaximumSwathWidthSupportLuma = 15360;
		} else if (!dml_is_vertical_rotation(display_cfg->plane_descriptors[k].composition.rotation_angle) && mode_lib->ms.BytePerPixelC[k] > 0 && display_cfg->plane_descriptors[k].pixel_format != dml2_rgbe_alpha) { // horz video
			s->MaximumSwathWidthSupportLuma = 7680 + 16;
		} else if (dml_is_vertical_rotation(display_cfg->plane_descriptors[k].composition.rotation_angle) && mode_lib->ms.BytePerPixelC[k] > 0 && display_cfg->plane_descriptors[k].pixel_format != dml2_rgbe_alpha) { // vert video
			s->MaximumSwathWidthSupportLuma = 4320 + 16;
		} else if (display_cfg->plane_descriptors[k].pixel_format == dml2_rgbe_alpha) { // rgbe + alpha
			s->MaximumSwathWidthSupportLuma = 5120 + 16;
		} else if (dml_is_vertical_rotation(display_cfg->plane_descriptors[k].composition.rotation_angle) && mode_lib->ms.BytePerPixelY[k] == 8 && display_cfg->plane_descriptors[k].surface.dcc.enable == true) { // vert 64bpp
			s->MaximumSwathWidthSupportLuma = 3072 + 16;
		} else {
			s->MaximumSwathWidthSupportLuma = 6144 + 16;
		}

		if (dml_is_420(display_cfg->plane_descriptors[k].pixel_format)) {
			s->MaximumSwathWidthSupportChroma = (unsigned int)(s->MaximumSwathWidthSupportLuma / 2.0);
		} else {
			s->MaximumSwathWidthSupportChroma = s->MaximumSwathWidthSupportLuma;
		}

		unsigned lb_buffer_size_bits_luma = mode_lib->ip.line_buffer_size_bits;
		unsigned lb_buffer_size_bits_chroma = mode_lib->ip.line_buffer_size_bits;

/*
#if defined(DV_BUILD)
		// Assume a memory config setting of 3 in 420 mode or get a new ip parameter that reflects the programming.
		if (mode_lib->ms.BytePerPixelC[k] != 0.0 && display_cfg->plane_descriptors[k].pixel_format != dml2_rgbe_alpha) {
			lb_buffer_size_bits_luma = 34620 * 57;
			lb_buffer_size_bits_chroma = 13560 * 57;
		}
#endif
*/
		mode_lib->ms.MaximumSwathWidthInLineBufferLuma = lb_buffer_size_bits_luma * math_max2(display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio, 1.0) / 57 /
			(display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps + math_max2(math_ceil2(display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio, 1.0) - 2, 0.0));
		if (mode_lib->ms.BytePerPixelC[k] == 0.0) {
			mode_lib->ms.MaximumSwathWidthInLineBufferChroma = 0;
		} else {
			mode_lib->ms.MaximumSwathWidthInLineBufferChroma = lb_buffer_size_bits_chroma * math_max2(display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio, 1.0) / 57 /
				(display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps + math_max2(math_ceil2(display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio, 1.0) - 2, 0.0));
		}

		mode_lib->ms.MaximumSwathWidthLuma[k] = math_min2(s->MaximumSwathWidthSupportLuma, mode_lib->ms.MaximumSwathWidthInLineBufferLuma);
		mode_lib->ms.MaximumSwathWidthChroma[k] = math_min2(s->MaximumSwathWidthSupportChroma, mode_lib->ms.MaximumSwathWidthInLineBufferChroma);

		dml2_printf("DML::%s: k=%u MaximumSwathWidthLuma=%f\n", __func__, k, mode_lib->ms.MaximumSwathWidthLuma[k]);
		dml2_printf("DML::%s: k=%u MaximumSwathWidthSupportLuma=%u\n", __func__, k, s->MaximumSwathWidthSupportLuma);
		dml2_printf("DML::%s: k=%u MaximumSwathWidthInLineBufferLuma=%f\n", __func__, k, mode_lib->ms.MaximumSwathWidthInLineBufferLuma);

		dml2_printf("DML::%s: k=%u MaximumSwathWidthChroma=%f\n", __func__, k, mode_lib->ms.MaximumSwathWidthChroma[k]);
		dml2_printf("DML::%s: k=%u MaximumSwathWidthSupportChroma=%u\n", __func__, k, s->MaximumSwathWidthSupportChroma);
		dml2_printf("DML::%s: k=%u MaximumSwathWidthInLineBufferChroma=%f\n", __func__, k, mode_lib->ms.MaximumSwathWidthInLineBufferChroma);
	}

	/* Cursor Support Check */
	mode_lib->ms.support.CursorSupport = true;
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		if (display_cfg->plane_descriptors[k].cursor.num_cursors > 0) {
			if (display_cfg->plane_descriptors[k].cursor.cursor_bpp == 64 && mode_lib->ip.cursor_64bpp_support == false)
				mode_lib->ms.support.CursorSupport = false;
		}
	}

	/* Valid Pitch Check */
	mode_lib->ms.support.PitchSupport = true;
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {

		// data pitch
		unsigned int alignment_l = mode_lib->ms.MacroTileWidthY[k];

		if (mode_lib->ms.surf_linear128_l[k])
			alignment_l = alignment_l / 2;

		mode_lib->ms.support.AlignedYPitch[k] = (unsigned int)math_ceil2(math_max2(display_cfg->plane_descriptors[k].surface.plane0.pitch, display_cfg->plane_descriptors[k].surface.plane0.width), alignment_l);
		if (dml_is_420(display_cfg->plane_descriptors[k].pixel_format) || display_cfg->plane_descriptors[k].pixel_format == dml2_rgbe_alpha) {
			unsigned int alignment_c = mode_lib->ms.MacroTileWidthC[k];

			if (mode_lib->ms.surf_linear128_c[k])
				alignment_c = alignment_c / 2;
			mode_lib->ms.support.AlignedCPitch[k] = (unsigned int)math_ceil2(math_max2(display_cfg->plane_descriptors[k].surface.plane1.pitch, display_cfg->plane_descriptors[k].surface.plane1.width), alignment_c);
		} else {
			mode_lib->ms.support.AlignedCPitch[k] = display_cfg->plane_descriptors[k].surface.plane1.pitch;
		}

		if (mode_lib->ms.support.AlignedYPitch[k] > display_cfg->plane_descriptors[k].surface.plane0.pitch ||
			mode_lib->ms.support.AlignedCPitch[k] > display_cfg->plane_descriptors[k].surface.plane1.pitch) {
			mode_lib->ms.support.PitchSupport = false;
#if defined(__DML_VBA_DEBUG__)
			dml2_printf("DML::%s: k=%u AlignedYPitch = %d\n", __func__, k, mode_lib->ms.support.AlignedYPitch[k]);
			dml2_printf("DML::%s: k=%u PitchY = %d\n", __func__, k, display_cfg->plane_descriptors[k].surface.plane0.pitch);
			dml2_printf("DML::%s: k=%u AlignedCPitch = %d\n", __func__, k, mode_lib->ms.support.AlignedCPitch[k]);
			dml2_printf("DML::%s: k=%u PitchC = %d\n", __func__, k, display_cfg->plane_descriptors[k].surface.plane1.pitch);
			dml2_printf("DML::%s: k=%u PitchSupport = %d\n", __func__, k, mode_lib->ms.support.PitchSupport);
#endif
		}

		// meta pitch
		if (mode_lib->ip.dcn_mrq_present && display_cfg->plane_descriptors[k].surface.dcc.enable) {
			mode_lib->ms.support.AlignedDCCMetaPitchY[k] = (unsigned int)math_ceil2(math_max2(display_cfg->plane_descriptors[k].surface.dcc.plane0.pitch,
															display_cfg->plane_descriptors[k].surface.plane0.width), 64.0 * mode_lib->ms.Read256BlockWidthY[k]);

			if (mode_lib->ms.support.AlignedDCCMetaPitchY[k] > display_cfg->plane_descriptors[k].surface.dcc.plane0.pitch)
				mode_lib->ms.support.PitchSupport = false;

			if (dml_is_420(display_cfg->plane_descriptors[k].pixel_format) || display_cfg->plane_descriptors[k].pixel_format == dml2_rgbe_alpha) {
				mode_lib->ms.support.AlignedDCCMetaPitchC[k] = (unsigned int)math_ceil2(math_max2(display_cfg->plane_descriptors[k].surface.dcc.plane1.pitch,
																display_cfg->plane_descriptors[k].surface.plane1.width), 64.0 * mode_lib->ms.Read256BlockWidthC[k]);

				if (mode_lib->ms.support.AlignedDCCMetaPitchC[k] > display_cfg->plane_descriptors[k].surface.dcc.plane1.pitch)
					mode_lib->ms.support.PitchSupport = false;
			}
		} else {
			mode_lib->ms.support.AlignedDCCMetaPitchY[k] = 0;
			mode_lib->ms.support.AlignedDCCMetaPitchC[k] = 0;
		}
	}

	mode_lib->ms.support.ViewportExceedsSurface = false;
	if (!display_cfg->overrides.hw.surface_viewport_size_check_disable) {
		for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
			if (display_cfg->plane_descriptors[k].composition.viewport.plane0.width > display_cfg->plane_descriptors[k].surface.plane0.width ||
				display_cfg->plane_descriptors[k].composition.viewport.plane0.height > display_cfg->plane_descriptors[k].surface.plane0.height) {
				mode_lib->ms.support.ViewportExceedsSurface = true;
#if defined(__DML_VBA_DEBUG__)
				dml2_printf("DML::%s: k=%u ViewportWidth = %d\n", __func__, k, display_cfg->plane_descriptors[k].composition.viewport.plane0.width);
				dml2_printf("DML::%s: k=%u SurfaceWidthY = %d\n", __func__, k, display_cfg->plane_descriptors[k].surface.plane0.width);
				dml2_printf("DML::%s: k=%u ViewportHeight = %d\n", __func__, k, display_cfg->plane_descriptors[k].composition.viewport.plane0.height);
				dml2_printf("DML::%s: k=%u SurfaceHeightY = %d\n", __func__, k, display_cfg->plane_descriptors[k].surface.plane0.height);
				dml2_printf("DML::%s: k=%u ViewportExceedsSurface = %d\n", __func__, k, mode_lib->ms.support.ViewportExceedsSurface);
#endif
			}
			if (dml_is_420(display_cfg->plane_descriptors[k].pixel_format) || display_cfg->plane_descriptors[k].pixel_format == dml2_rgbe_alpha) {
				if (display_cfg->plane_descriptors[k].composition.viewport.plane1.width > display_cfg->plane_descriptors[k].surface.plane1.width ||
					display_cfg->plane_descriptors[k].composition.viewport.plane1.height > display_cfg->plane_descriptors[k].surface.plane1.height) {
					mode_lib->ms.support.ViewportExceedsSurface = true;
				}
			}
		}
	}

	CalculateSwathAndDETConfiguration_params->display_cfg = display_cfg;
	CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSizeInKByte = mode_lib->ip.config_return_buffer_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->MaxTotalDETInKByte = mode_lib->ms.MaxTotalDETInKByte;
	CalculateSwathAndDETConfiguration_params->MinCompressedBufferSizeInKByte = mode_lib->ms.MinCompressedBufferSizeInKByte;
	CalculateSwathAndDETConfiguration_params->rob_buffer_size_kbytes = mode_lib->ip.rob_buffer_size_kbytes;
	CalculateSwathAndDETConfiguration_params->pixel_chunk_size_kbytes = mode_lib->ip.pixel_chunk_size_kbytes;
	CalculateSwathAndDETConfiguration_params->rob_buffer_size_kbytes = mode_lib->ip.rob_buffer_size_kbytes;
	CalculateSwathAndDETConfiguration_params->pixel_chunk_size_kbytes = mode_lib->ip.pixel_chunk_size_kbytes;
	CalculateSwathAndDETConfiguration_params->ForceSingleDPP = 1;
	CalculateSwathAndDETConfiguration_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
	CalculateSwathAndDETConfiguration_params->nomDETInKByte = mode_lib->ms.NomDETInKByte;
	CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSegmentSizeInkByte = mode_lib->ip.config_return_buffer_segment_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->CompressedBufferSegmentSizeInkByte = mode_lib->ip.compressed_buffer_segment_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->ReadBandwidthLuma = mode_lib->ms.vactive_sw_bw_l;
	CalculateSwathAndDETConfiguration_params->ReadBandwidthChroma = mode_lib->ms.vactive_sw_bw_c;
	CalculateSwathAndDETConfiguration_params->MaximumSwathWidthLuma = mode_lib->ms.MaximumSwathWidthLuma;
	CalculateSwathAndDETConfiguration_params->MaximumSwathWidthChroma = mode_lib->ms.MaximumSwathWidthChroma;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightY = mode_lib->ms.Read256BlockHeightY;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightC = mode_lib->ms.Read256BlockHeightC;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthY = mode_lib->ms.Read256BlockWidthY;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthC = mode_lib->ms.Read256BlockWidthC;
	CalculateSwathAndDETConfiguration_params->surf_linear128_l = mode_lib->ms.surf_linear128_l;
	CalculateSwathAndDETConfiguration_params->surf_linear128_c = mode_lib->ms.surf_linear128_c;
	CalculateSwathAndDETConfiguration_params->ODMMode = s->dummy_odm_mode;
	CalculateSwathAndDETConfiguration_params->BytePerPixY = mode_lib->ms.BytePerPixelY;
	CalculateSwathAndDETConfiguration_params->BytePerPixC = mode_lib->ms.BytePerPixelC;
	CalculateSwathAndDETConfiguration_params->BytePerPixDETY = mode_lib->ms.BytePerPixelInDETY;
	CalculateSwathAndDETConfiguration_params->BytePerPixDETC = mode_lib->ms.BytePerPixelInDETC;
	CalculateSwathAndDETConfiguration_params->DPPPerSurface = s->dummy_integer_array[2];
	CalculateSwathAndDETConfiguration_params->mrq_present = mode_lib->ip.dcn_mrq_present;

	// output
	CalculateSwathAndDETConfiguration_params->req_per_swath_ub_l = s->dummy_integer_array[0];
	CalculateSwathAndDETConfiguration_params->req_per_swath_ub_c = s->dummy_integer_array[1];
	CalculateSwathAndDETConfiguration_params->swath_width_luma_ub = s->dummy_integer_array[3];
	CalculateSwathAndDETConfiguration_params->swath_width_chroma_ub = s->dummy_integer_array[4];
	CalculateSwathAndDETConfiguration_params->SwathWidth = s->dummy_integer_array[5];
	CalculateSwathAndDETConfiguration_params->SwathWidthChroma = s->dummy_integer_array[6];
	CalculateSwathAndDETConfiguration_params->SwathHeightY = s->dummy_integer_array[7];
	CalculateSwathAndDETConfiguration_params->SwathHeightC = s->dummy_integer_array[8];
	CalculateSwathAndDETConfiguration_params->request_size_bytes_luma = s->dummy_integer_array[26];
	CalculateSwathAndDETConfiguration_params->request_size_bytes_chroma = s->dummy_integer_array[27];
	CalculateSwathAndDETConfiguration_params->DETBufferSizeInKByte = s->dummy_integer_array[9];
	CalculateSwathAndDETConfiguration_params->DETBufferSizeY = s->dummy_integer_array[10];
	CalculateSwathAndDETConfiguration_params->DETBufferSizeC = s->dummy_integer_array[11];
	CalculateSwathAndDETConfiguration_params->full_swath_bytes_l = s->full_swath_bytes_l;
	CalculateSwathAndDETConfiguration_params->full_swath_bytes_c = s->full_swath_bytes_c;
	CalculateSwathAndDETConfiguration_params->UnboundedRequestEnabled = &s->dummy_boolean[0];
	CalculateSwathAndDETConfiguration_params->compbuf_reserved_space_64b = &s->dummy_integer[1];
	CalculateSwathAndDETConfiguration_params->hw_debug5 = &s->dummy_boolean[2];
	CalculateSwathAndDETConfiguration_params->CompressedBufferSizeInkByte = &s->dummy_integer[0];
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupportPerSurface = mode_lib->ms.SingleDPPViewportSizeSupportPerSurface;
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupport = &s->dummy_boolean[1];

	// This calls is just to find out if there is enough DET space to support full vp in 1 pipe.
	CalculateSwathAndDETConfiguration(&mode_lib->scratch, CalculateSwathAndDETConfiguration_params);

	mode_lib->ms.TotalNumberOfActiveDPP = 0;
	mode_lib->ms.support.TotalAvailablePipesSupport = true;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		/*Number Of DSC Slices*/
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.enable == dml2_dsc_enable ||
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.enable == dml2_dsc_enable_if_necessary) {

			if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.overrides.num_slices != 0)
				mode_lib->ms.support.NumberOfDSCSlices[k] = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.overrides.num_slices;
			else {
				if (s->PixelClockBackEnd[k] > 4800) {
					mode_lib->ms.support.NumberOfDSCSlices[k] = (unsigned int)(math_ceil2(s->PixelClockBackEnd[k] / 600, 4));
				} else if (s->PixelClockBackEnd[k] > 2400) {
					mode_lib->ms.support.NumberOfDSCSlices[k] = 8;
				} else if (s->PixelClockBackEnd[k] > 1200) {
					mode_lib->ms.support.NumberOfDSCSlices[k] = 4;
				} else if (s->PixelClockBackEnd[k] > 340) {
					mode_lib->ms.support.NumberOfDSCSlices[k] = 2;
				} else {
					mode_lib->ms.support.NumberOfDSCSlices[k] = 1;
				}
			}
		} else {
			mode_lib->ms.support.NumberOfDSCSlices[k] = 0;
		}

		CalculateODMMode(
			mode_lib->ip.maximum_pixels_per_line_per_dsc_unit,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].overrides.odm_mode,
			mode_lib->ms.max_dispclk_freq_mhz,
			false, // DSCEnable
			mode_lib->ms.TotalNumberOfActiveDPP,
			mode_lib->ip.max_num_dpp,
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000),
			mode_lib->ms.support.NumberOfDSCSlices[k],

			/* Output */
			&s->TotalAvailablePipesSupportNoDSC,
			&s->NumberOfDPPNoDSC,
			&s->ODMModeNoDSC,
			&s->RequiredDISPCLKPerSurfaceNoDSC);

		CalculateODMMode(
			mode_lib->ip.maximum_pixels_per_line_per_dsc_unit,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].overrides.odm_mode,
			mode_lib->ms.max_dispclk_freq_mhz,
			true, // DSCEnable
			mode_lib->ms.TotalNumberOfActiveDPP,
			mode_lib->ip.max_num_dpp,
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000),
			mode_lib->ms.support.NumberOfDSCSlices[k],

			/* Output */
			&s->TotalAvailablePipesSupportDSC,
			&s->NumberOfDPPDSC,
			&s->ODMModeDSC,
			&s->RequiredDISPCLKPerSurfaceDSC);

		CalculateOutputLink(
			&mode_lib->scratch,
			((double)mode_lib->soc.clk_table.phyclk.clk_values_khz[0] / 1000),
			((double)mode_lib->soc.clk_table.phyclk_d18.clk_values_khz[0] / 1000),
			((double)mode_lib->soc.clk_table.phyclk_d32.clk_values_khz[0] / 1000),
			mode_lib->soc.phy_downspread_percent,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active,
			s->PixelClockBackEnd[k],
			s->OutputBpp[k],
			mode_lib->ip.maximum_dsc_bits_per_component,
			mode_lib->ms.support.NumberOfDSCSlices[k],
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.audio_sample_rate,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.audio_sample_layout,
			s->ODMModeNoDSC,
			s->ODMModeDSC,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.enable,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_dp_lane_count,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_dp_link_rate,

			/* Output */
			&mode_lib->ms.RequiresDSC[k],
			&mode_lib->ms.RequiresFEC[k],
			&mode_lib->ms.OutputBpp[k],
			&mode_lib->ms.OutputType[k],
			&mode_lib->ms.OutputRate[k],
			&mode_lib->ms.RequiredSlots[k]);

		if (s->OutputBpp[k] == 0.0) {
			s->OutputBpp[k] = mode_lib->ms.OutputBpp[k];
		}

		if (mode_lib->ms.RequiresDSC[k] == false) {
			mode_lib->ms.ODMMode[k] = s->ODMModeNoDSC;
			mode_lib->ms.RequiredDISPCLKPerSurface[k] = s->RequiredDISPCLKPerSurfaceNoDSC;
			if (!s->TotalAvailablePipesSupportNoDSC)
				mode_lib->ms.support.TotalAvailablePipesSupport = false;
			mode_lib->ms.TotalNumberOfActiveDPP = mode_lib->ms.TotalNumberOfActiveDPP + s->NumberOfDPPNoDSC;
		} else {
			mode_lib->ms.ODMMode[k] = s->ODMModeDSC;
			mode_lib->ms.RequiredDISPCLKPerSurface[k] = s->RequiredDISPCLKPerSurfaceDSC;
			if (!s->TotalAvailablePipesSupportDSC)
				mode_lib->ms.support.TotalAvailablePipesSupport = false;
			mode_lib->ms.TotalNumberOfActiveDPP = mode_lib->ms.TotalNumberOfActiveDPP + s->NumberOfDPPDSC;
		}
#if defined(__DML_VBA_DEBUG__)
		dml2_printf("DML::%s: k=%d RequiresDSC = %d\n", __func__, k, mode_lib->ms.RequiresDSC[k]);
		dml2_printf("DML::%s: k=%d ODMMode = %d\n", __func__, k, mode_lib->ms.ODMMode[k]);
#endif

		// ensure the number dsc slices is integer multiple based on ODM mode
		mode_lib->ms.support.DSCSlicesODMModeSupported = true;
		if (mode_lib->ms.RequiresDSC[k]) {
			// fail a ms check if the override num_slices doesn't align with odm mode setting
			if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.overrides.num_slices != 0) {
				if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_2to1)
					mode_lib->ms.support.DSCSlicesODMModeSupported = ((mode_lib->ms.support.NumberOfDSCSlices[k] % 2) == 0);
				else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_3to1)
					mode_lib->ms.support.DSCSlicesODMModeSupported = (mode_lib->ms.support.NumberOfDSCSlices[k] == 12);
				else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_4to1)
					mode_lib->ms.support.DSCSlicesODMModeSupported = ((mode_lib->ms.support.NumberOfDSCSlices[k] % 4) == 0);
#if defined(__DML_VBA_DEBUG__)
				if (!mode_lib->ms.support.DSCSlicesODMModeSupported) {
					dml2_printf("DML::%s: k=%d Invalid dsc num_slices and ODM mode setting\n", __func__, k);
					dml2_printf("DML::%s: k=%d num_slices = %d\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.overrides.num_slices);
					dml2_printf("DML::%s: k=%d ODMMode = %d\n", __func__, k, mode_lib->ms.ODMMode[k]);
				}
#endif
			} else {
				// safe guard to ensure the dml derived dsc slices and odm setting are compatible
				if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_2to1)
					mode_lib->ms.support.NumberOfDSCSlices[k] = 2 * (unsigned int)math_ceil2(mode_lib->ms.support.NumberOfDSCSlices[k] / 2.0, 1.0);
				else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_3to1)
					mode_lib->ms.support.NumberOfDSCSlices[k] = 12;
				else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_4to1)
					mode_lib->ms.support.NumberOfDSCSlices[k] = 4 * (unsigned int)math_ceil2(mode_lib->ms.support.NumberOfDSCSlices[k] / 4.0, 1.0);
			}

		} else {
			mode_lib->ms.support.NumberOfDSCSlices[k] = 0;
		}
	}

	mode_lib->ms.support.incorrect_imall_usage = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ip.imall_supported && display_cfg->plane_descriptors[k].overrides.legacy_svp_config == dml2_svp_mode_override_imall)
			mode_lib->ms.support.incorrect_imall_usage = 1;
	}

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.MPCCombine[k] = false;
		mode_lib->ms.NoOfDPP[k] = 1;

		if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_4to1) {
			mode_lib->ms.MPCCombine[k] = false;
			mode_lib->ms.NoOfDPP[k] = 4;
		} else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_3to1) {
			mode_lib->ms.MPCCombine[k] = false;
			mode_lib->ms.NoOfDPP[k] = 3;
		} else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_2to1) {
			mode_lib->ms.MPCCombine[k] = false;
			mode_lib->ms.NoOfDPP[k] = 2;
		} else if (display_cfg->plane_descriptors[k].overrides.mpcc_combine_factor == 2) {
			mode_lib->ms.MPCCombine[k] = true;
			mode_lib->ms.NoOfDPP[k] = 2;
			mode_lib->ms.TotalNumberOfActiveDPP++;
		} else if (display_cfg->plane_descriptors[k].overrides.mpcc_combine_factor == 1) {
			mode_lib->ms.MPCCombine[k] = false;
			mode_lib->ms.NoOfDPP[k] = 1;
			if (!mode_lib->ms.SingleDPPViewportSizeSupportPerSurface[k]) {
				dml2_printf("WARNING: DML::%s: MPCC is override to disable but viewport is too large to be supported with single pipe!\n", __func__);
			}
		} else {
			if ((mode_lib->ms.MinDPPCLKUsingSingleDPP[k] > mode_lib->ms.max_dppclk_freq_mhz) || !mode_lib->ms.SingleDPPViewportSizeSupportPerSurface[k]) {
				mode_lib->ms.MPCCombine[k] = true;
				mode_lib->ms.NoOfDPP[k] = 2;
				mode_lib->ms.TotalNumberOfActiveDPP++;
			}
		}
#if defined(__DML_VBA_DEBUG__)
		dml2_printf("DML::%s: k=%d, NoOfDPP = %d\n", __func__, k, mode_lib->ms.NoOfDPP[k]);
#endif
	}

	if (mode_lib->ms.TotalNumberOfActiveDPP > (unsigned int)mode_lib->ip.max_num_dpp)
		mode_lib->ms.support.TotalAvailablePipesSupport = false;


	mode_lib->ms.TotalNumberOfSingleDPPSurfaces = 0;
	for (k = 0; k < (unsigned int)mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.NoOfDPP[k] == 1)
			mode_lib->ms.TotalNumberOfSingleDPPSurfaces = mode_lib->ms.TotalNumberOfSingleDPPSurfaces + 1;
	}

	//DISPCLK/DPPCLK
	mode_lib->ms.WritebackRequiredDISPCLK = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0) {
			mode_lib->ms.WritebackRequiredDISPCLK = math_max2(mode_lib->ms.WritebackRequiredDISPCLK,
				CalculateWriteBackDISPCLK(display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].pixel_format,
					((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000),
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].h_ratio,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_ratio,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].h_taps,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_taps,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].input_width,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_width,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total,
					mode_lib->ip.writeback_line_buffer_buffer_size));
		}
	}

	mode_lib->ms.RequiredDISPCLK = mode_lib->ms.WritebackRequiredDISPCLK;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.RequiredDISPCLK = math_max2(mode_lib->ms.RequiredDISPCLK, mode_lib->ms.RequiredDISPCLKPerSurface[k]);
	}

	mode_lib->ms.GlobalDPPCLK = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.RequiredDPPCLK[k] = mode_lib->ms.MinDPPCLKUsingSingleDPP[k] / mode_lib->ms.NoOfDPP[k];
		mode_lib->ms.GlobalDPPCLK = math_max2(mode_lib->ms.GlobalDPPCLK, mode_lib->ms.RequiredDPPCLK[k]);
	}

	mode_lib->ms.support.DISPCLK_DPPCLK_Support = !((mode_lib->ms.RequiredDISPCLK > mode_lib->ms.max_dispclk_freq_mhz) || (mode_lib->ms.GlobalDPPCLK > mode_lib->ms.max_dppclk_freq_mhz));

	/* Total Available OTG, Writeback, HDMIFRL, DP Support Check */
	s->TotalNumberOfActiveOTG = 0;
	s->TotalNumberOfActiveHDMIFRL = 0;
	s->TotalNumberOfActiveDP2p0 = 0;
	s->TotalNumberOfActiveDP2p0Outputs = 0;
	s->TotalNumberOfActiveWriteback = 0;
	memset(s->stream_visited, 0, DML2_MAX_PLANES * sizeof(bool));

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (!dml_is_phantom_pipe(&display_cfg->plane_descriptors[k])) {
			if (!s->stream_visited[display_cfg->plane_descriptors[k].stream_index]) {
				s->stream_visited[display_cfg->plane_descriptors[k].stream_index] = 1;

				if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0)
					s->TotalNumberOfActiveWriteback = s->TotalNumberOfActiveWriteback + 1;

				s->TotalNumberOfActiveOTG = s->TotalNumberOfActiveOTG + 1;
				if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmifrl)
					s->TotalNumberOfActiveHDMIFRL = s->TotalNumberOfActiveHDMIFRL + 1;
				if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_dp2p0) {
					s->TotalNumberOfActiveDP2p0 = s->TotalNumberOfActiveDP2p0 + 1;
					// FIXME_STAGE2: SW not using backend related stuff, need mapping for mst setup
					//if (display_cfg->output.OutputMultistreamId[k] == k || display_cfg->output.OutputMultistreamEn[k] == false) {
					s->TotalNumberOfActiveDP2p0Outputs = s->TotalNumberOfActiveDP2p0Outputs + 1;
					//}
				}
			}
		}
	}

	/* Writeback Mode Support Check */
	mode_lib->ms.support.EnoughWritebackUnits = 1;
	if (s->TotalNumberOfActiveWriteback > (unsigned int)mode_lib->ip.max_num_wb) {
		mode_lib->ms.support.EnoughWritebackUnits = false;
	}
	mode_lib->ms.support.NumberOfOTGSupport = (s->TotalNumberOfActiveOTG <= (unsigned int)mode_lib->ip.max_num_otg);
	mode_lib->ms.support.NumberOfHDMIFRLSupport = (s->TotalNumberOfActiveHDMIFRL <= (unsigned int)mode_lib->ip.max_num_hdmi_frl_outputs);
	mode_lib->ms.support.NumberOfDP2p0Support = (s->TotalNumberOfActiveDP2p0 <= (unsigned int)mode_lib->ip.max_num_dp2p0_streams && s->TotalNumberOfActiveDP2p0Outputs <= (unsigned int)mode_lib->ip.max_num_dp2p0_outputs);


	mode_lib->ms.support.ExceededMultistreamSlots = false;
	mode_lib->ms.support.LinkCapacitySupport = true;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_disabled == false &&
				(display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_dp || display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_dp2p0 || display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_edp ||
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmi || display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmifrl) && mode_lib->ms.OutputBpp[k] == 0) {
			mode_lib->ms.support.LinkCapacitySupport = false;
		}
	}

	mode_lib->ms.support.P2IWith420 = false;
	mode_lib->ms.support.DSCOnlyIfNecessaryWithBPP = false;
	mode_lib->ms.support.DSC422NativeNotSupported = false;
	mode_lib->ms.support.LinkRateDoesNotMatchDPVersion = false;
	mode_lib->ms.support.LinkRateForMultistreamNotIndicated = false;
	mode_lib->ms.support.BPPForMultistreamNotIndicated = false;
	mode_lib->ms.support.MultistreamWithHDMIOreDP = false;
	mode_lib->ms.support.MSOOrODMSplitWithNonDPLink = false;
	mode_lib->ms.support.NotEnoughLanesForMSO = false;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_dp || display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_dp2p0 || display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_edp ||
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmi || display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmifrl) {
			if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format == dml2_420 && display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced == 1 && mode_lib->ip.ptoi_supported == true)
				mode_lib->ms.support.P2IWith420 = true;

			if ((display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.enable == dml2_dsc_enable || display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.dsc.enable == dml2_dsc_enable_if_necessary) && display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format == dml2_n422 && !mode_lib->ip.dsc422_native_support)
				mode_lib->ms.support.DSC422NativeNotSupported = true;

			if (((display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_dp_link_rate == dml2_dp_rate_hbr || display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_dp_link_rate == dml2_dp_rate_hbr2 ||
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_dp_link_rate == dml2_dp_rate_hbr3) &&
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder != dml2_dp && display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder != dml2_edp) ||
				((display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_dp_link_rate == dml2_dp_rate_uhbr10 || display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_dp_link_rate == dml2_dp_rate_uhbr13p5 ||
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_dp_link_rate == dml2_dp_rate_uhbr20) &&
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder != dml2_dp2p0))
				mode_lib->ms.support.LinkRateDoesNotMatchDPVersion = true;

			// FIXME_STAGE2
			//if (display_cfg->output.OutputMultistreamEn[k] == 1) {
			// if (display_cfg->output.OutputMultistreamId[k] == k && display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_dp_link_rate == dml2_dp_rate_na)
			// mode_lib->ms.support.LinkRateForMultistreamNotIndicated = true;
			// if (display_cfg->output.OutputMultistreamId[k] == k && s->OutputBpp[k] == 0)
			// mode_lib->ms.support.BPPForMultistreamNotIndicated = true;
			// for (n = 0; n < mode_lib->ms.num_active_planes; ++n) {
			// if (display_cfg->output.OutputMultistreamId[k] == n && s->OutputBpp[k] == 0)
			// mode_lib->ms.support.BPPForMultistreamNotIndicated = true;
			// }
			//}

			if ((display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_edp ||
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmi ||
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmifrl)) {
				// FIXME_STAGE2
				//if (display_cfg->output.OutputMultistreamEn[k] == 1 && display_cfg->output.OutputMultistreamId[k] == k)
				// mode_lib->ms.support.MultistreamWithHDMIOreDP = true;
				//for (n = 0; n < mode_lib->ms.num_active_planes; ++n) {
				// if (display_cfg->output.OutputMultistreamEn[k] == 1 && display_cfg->output.OutputMultistreamId[k] == n)
				// mode_lib->ms.support.MultistreamWithHDMIOreDP = true;
				//}
			}
			if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder != dml2_dp && (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].overrides.odm_mode == dml2_odm_mode_split_1to2 ||
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].overrides.odm_mode == dml2_odm_mode_mso_1to2 || display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].overrides.odm_mode == dml2_odm_mode_mso_1to4))
				mode_lib->ms.support.MSOOrODMSplitWithNonDPLink = true;

			if ((display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].overrides.odm_mode == dml2_odm_mode_mso_1to2 && display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_dp_lane_count < 2) ||
				(display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].overrides.odm_mode == dml2_odm_mode_mso_1to4 && display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_dp_lane_count < 4))
				mode_lib->ms.support.NotEnoughLanesForMSO = true;
		}
	}

	mode_lib->ms.support.DTBCLKRequiredMoreThanSupported = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmifrl &&
				!dml_is_phantom_pipe(&display_cfg->plane_descriptors[k])) {
			mode_lib->ms.RequiredDTBCLK[k] = RequiredDTBCLK(
				mode_lib->ms.RequiresDSC[k],
				s->PixelClockBackEnd[k],
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format,
				mode_lib->ms.OutputBpp[k],
				mode_lib->ms.support.NumberOfDSCSlices[k],
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total,
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active,
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.audio_sample_rate,
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.audio_sample_layout);

			if (mode_lib->ms.RequiredDTBCLK[k] > ((double)min_clk_table->max_clocks_khz.dtbclk / 1000)) {
				mode_lib->ms.support.DTBCLKRequiredMoreThanSupported = true;
			}
		} else {
			/* Phantom DTBCLK can be calculated different from main because phantom has no DSC and thus
			 * will have a different output BPP. Ignore phantom DTBCLK requirement and only consider
			 * non-phantom DTBCLK requirements. In map_mode_to_soc_dpm we choose the highest DTBCLK
			 * required - by setting phantom dtbclk to 0 we ignore it.
			 */
			mode_lib->ms.RequiredDTBCLK[k] = 0;
		}
	}

	mode_lib->ms.support.DSCCLKRequiredMoreThanSupported = false;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_dp ||
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_dp2p0 ||
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_edp ||
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmifrl) {
			if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format == dml2_420) {
				s->DSCFormatFactor = 2;
			} else if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format == dml2_444) {
				s->DSCFormatFactor = 1;
			} else if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format == dml2_n422 || display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmifrl) {
				s->DSCFormatFactor = 2;
			} else {
				s->DSCFormatFactor = 1;
			}
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u, RequiresDSC = %u\n", __func__, k, mode_lib->ms.RequiresDSC[k]);
#endif
			if (mode_lib->ms.RequiresDSC[k] == true) {
				s->PixelClockBackEndFactor = 3.0;

				if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_4to1)
					s->PixelClockBackEndFactor = 12.0;
				else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_3to1)
					s->PixelClockBackEndFactor = 9.0;
				else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_2to1)
					s->PixelClockBackEndFactor = 6.0;

				mode_lib->ms.required_dscclk_freq_mhz[k] = s->PixelClockBackEnd[k] / s->PixelClockBackEndFactor / (double)s->DSCFormatFactor;
				if (mode_lib->ms.required_dscclk_freq_mhz[k] > mode_lib->ms.max_dscclk_freq_mhz) {
					mode_lib->ms.support.DSCCLKRequiredMoreThanSupported = true;
				}

#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: k=%u, PixelClockBackEnd = %f\n", __func__, k, s->PixelClockBackEnd[k]);
				dml2_printf("DML::%s: k=%u, required_dscclk_freq_mhz = %f\n", __func__, k, mode_lib->ms.required_dscclk_freq_mhz[k]);
				dml2_printf("DML::%s: k=%u, DSCFormatFactor = %u\n", __func__, k, s->DSCFormatFactor);
				dml2_printf("DML::%s: k=%u, DSCCLKRequiredMoreThanSupported = %u\n", __func__, k, mode_lib->ms.support.DSCCLKRequiredMoreThanSupported);
#endif
			}
		}
	}

	/* Check DSC Unit and Slices Support */
	mode_lib->ms.support.NotEnoughDSCSlices = false;
	s->TotalDSCUnitsRequired = 0;
	mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport = true;
	memset(s->stream_visited, 0, DML2_MAX_PLANES * sizeof(bool));

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.RequiresDSC[k] == true && !s->stream_visited[display_cfg->plane_descriptors[k].stream_index]) {
			s->NumDSCUnitRequired = 1;

			if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_4to1)
				s->NumDSCUnitRequired = 4;
			else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_3to1)
				s->NumDSCUnitRequired = 3;
			else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_2to1)
				s->NumDSCUnitRequired = 2;

			if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active > s->NumDSCUnitRequired * (unsigned int)mode_lib->ip.maximum_pixels_per_line_per_dsc_unit)
				mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport = false;
			s->TotalDSCUnitsRequired = s->TotalDSCUnitsRequired + s->NumDSCUnitRequired;

			if (mode_lib->ms.support.NumberOfDSCSlices[k] > 4 * s->NumDSCUnitRequired)
				mode_lib->ms.support.NotEnoughDSCSlices = true;
		}
		s->stream_visited[display_cfg->plane_descriptors[k].stream_index] = 1;
	}

	mode_lib->ms.support.NotEnoughDSCUnits = false;
	if (s->TotalDSCUnitsRequired > (unsigned int)mode_lib->ip.num_dsc) {
		mode_lib->ms.support.NotEnoughDSCUnits = true;
	}

	/*DSC Delay per state*/
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.DSCDelay[k] = DSCDelayRequirement(mode_lib->ms.RequiresDSC[k],
			mode_lib->ms.ODMMode[k],
			mode_lib->ip.maximum_dsc_bits_per_component,
			s->OutputBpp[k],
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total,
			mode_lib->ms.support.NumberOfDSCSlices[k],
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder,
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000),
			s->PixelClockBackEnd[k]);
	}

	// Figure out the swath and DET configuration after the num dpp per plane is figured out
	CalculateSwathAndDETConfiguration_params->ForceSingleDPP = false;
	CalculateSwathAndDETConfiguration_params->ODMMode = mode_lib->ms.ODMMode;
	CalculateSwathAndDETConfiguration_params->DPPPerSurface = mode_lib->ms.NoOfDPP;

	// output
	CalculateSwathAndDETConfiguration_params->req_per_swath_ub_l = s->dummy_integer_array[0];
	CalculateSwathAndDETConfiguration_params->req_per_swath_ub_c = s->dummy_integer_array[1];
	CalculateSwathAndDETConfiguration_params->swath_width_luma_ub = mode_lib->ms.swath_width_luma_ub;
	CalculateSwathAndDETConfiguration_params->swath_width_chroma_ub = mode_lib->ms.swath_width_chroma_ub;
	CalculateSwathAndDETConfiguration_params->SwathWidth = mode_lib->ms.SwathWidthY;
	CalculateSwathAndDETConfiguration_params->SwathWidthChroma = mode_lib->ms.SwathWidthC;
	CalculateSwathAndDETConfiguration_params->SwathHeightY = mode_lib->ms.SwathHeightY;
	CalculateSwathAndDETConfiguration_params->SwathHeightC = mode_lib->ms.SwathHeightC;
	CalculateSwathAndDETConfiguration_params->request_size_bytes_luma = mode_lib->ms.support.request_size_bytes_luma;
	CalculateSwathAndDETConfiguration_params->request_size_bytes_chroma = mode_lib->ms.support.request_size_bytes_chroma;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeInKByte = mode_lib->ms.DETBufferSizeInKByte; // FIXME: This is per pipe but the pipes in plane will use that
	CalculateSwathAndDETConfiguration_params->DETBufferSizeY = mode_lib->ms.DETBufferSizeY;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeC = mode_lib->ms.DETBufferSizeC;
	CalculateSwathAndDETConfiguration_params->UnboundedRequestEnabled = &mode_lib->ms.UnboundedRequestEnabled;
	CalculateSwathAndDETConfiguration_params->compbuf_reserved_space_64b = s->dummy_integer_array[3];
	CalculateSwathAndDETConfiguration_params->hw_debug5 = s->dummy_boolean_array[1];
	CalculateSwathAndDETConfiguration_params->CompressedBufferSizeInkByte = &mode_lib->ms.CompressedBufferSizeInkByte;
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupportPerSurface = s->dummy_boolean_array[0];
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupport = &mode_lib->ms.support.ViewportSizeSupport;

	CalculateSwathAndDETConfiguration(&mode_lib->scratch, CalculateSwathAndDETConfiguration_params);

	if (mode_lib->soc.mall_allocated_for_dcn_mbytes == 0) {
		for (k = 0; k < mode_lib->ms.num_active_planes; k++)
			mode_lib->ms.SurfaceSizeInMALL[k] = 0;
		mode_lib->ms.support.ExceededMALLSize = 0;
	} else {
		CalculateSurfaceSizeInMall(
			display_cfg,
			mode_lib->ms.num_active_planes,
			mode_lib->soc.mall_allocated_for_dcn_mbytes,

			mode_lib->ms.BytePerPixelY,
			mode_lib->ms.BytePerPixelC,
			mode_lib->ms.Read256BlockWidthY,
			mode_lib->ms.Read256BlockWidthC,
			mode_lib->ms.Read256BlockHeightY,
			mode_lib->ms.Read256BlockHeightC,
			mode_lib->ms.MacroTileWidthY,
			mode_lib->ms.MacroTileWidthC,
			mode_lib->ms.MacroTileHeightY,
			mode_lib->ms.MacroTileHeightC,

			/* Output */
			mode_lib->ms.SurfaceSizeInMALL,
			&mode_lib->ms.support.ExceededMALLSize);
	}

	mode_lib->ms.TotalNumberOfDCCActiveDPP = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (display_cfg->plane_descriptors[k].surface.dcc.enable == true) {
			mode_lib->ms.TotalNumberOfDCCActiveDPP = mode_lib->ms.TotalNumberOfDCCActiveDPP + mode_lib->ms.NoOfDPP[k];
		}
	}

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		s->SurfParameters[k].PixelClock = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
		s->SurfParameters[k].DPPPerSurface = mode_lib->ms.NoOfDPP[k];
		s->SurfParameters[k].RotationAngle = display_cfg->plane_descriptors[k].composition.rotation_angle;
		s->SurfParameters[k].ViewportHeight = display_cfg->plane_descriptors[k].composition.viewport.plane0.height;
		s->SurfParameters[k].ViewportHeightC = display_cfg->plane_descriptors[k].composition.viewport.plane1.height;
		s->SurfParameters[k].BlockWidth256BytesY = mode_lib->ms.Read256BlockWidthY[k];
		s->SurfParameters[k].BlockHeight256BytesY = mode_lib->ms.Read256BlockHeightY[k];
		s->SurfParameters[k].BlockWidth256BytesC = mode_lib->ms.Read256BlockWidthC[k];
		s->SurfParameters[k].BlockHeight256BytesC = mode_lib->ms.Read256BlockHeightC[k];
		s->SurfParameters[k].BlockWidthY = mode_lib->ms.MacroTileWidthY[k];
		s->SurfParameters[k].BlockHeightY = mode_lib->ms.MacroTileHeightY[k];
		s->SurfParameters[k].BlockWidthC = mode_lib->ms.MacroTileWidthC[k];
		s->SurfParameters[k].BlockHeightC = mode_lib->ms.MacroTileHeightC[k];
		s->SurfParameters[k].InterlaceEnable = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced;
		s->SurfParameters[k].HTotal = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total;
		s->SurfParameters[k].DCCEnable = display_cfg->plane_descriptors[k].surface.dcc.enable;
		s->SurfParameters[k].SourcePixelFormat = display_cfg->plane_descriptors[k].pixel_format;
		s->SurfParameters[k].SurfaceTiling = display_cfg->plane_descriptors[k].surface.tiling;
		s->SurfParameters[k].BytePerPixelY = mode_lib->ms.BytePerPixelY[k];
		s->SurfParameters[k].BytePerPixelC = mode_lib->ms.BytePerPixelC[k];
		s->SurfParameters[k].ProgressiveToInterlaceUnitInOPP = mode_lib->ip.ptoi_supported;
		s->SurfParameters[k].VRatio = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		s->SurfParameters[k].VRatioChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		s->SurfParameters[k].VTaps = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps;
		s->SurfParameters[k].VTapsChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps;
		s->SurfParameters[k].PitchY = display_cfg->plane_descriptors[k].surface.plane0.pitch;
		s->SurfParameters[k].PitchC = display_cfg->plane_descriptors[k].surface.plane1.pitch;
		s->SurfParameters[k].ViewportStationary = display_cfg->plane_descriptors[k].composition.viewport.stationary;
		s->SurfParameters[k].ViewportXStart = display_cfg->plane_descriptors[k].composition.viewport.plane0.x_start;
		s->SurfParameters[k].ViewportYStart = display_cfg->plane_descriptors[k].composition.viewport.plane0.y_start;
		s->SurfParameters[k].ViewportXStartC = display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start;
		s->SurfParameters[k].ViewportYStartC = display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start;
		s->SurfParameters[k].FORCE_ONE_ROW_FOR_FRAME = display_cfg->plane_descriptors[k].overrides.hw.force_one_row_for_frame;
		s->SurfParameters[k].SwathHeightY = mode_lib->ms.SwathHeightY[k];
		s->SurfParameters[k].SwathHeightC = mode_lib->ms.SwathHeightC[k];

		s->SurfParameters[k].DCCMetaPitchY = display_cfg->plane_descriptors[k].surface.dcc.plane0.pitch;
		s->SurfParameters[k].DCCMetaPitchC = display_cfg->plane_descriptors[k].surface.dcc.plane1.pitch;
	}

	CalculateVMRowAndSwath_params->display_cfg = display_cfg;
	CalculateVMRowAndSwath_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
	CalculateVMRowAndSwath_params->myPipe = s->SurfParameters;
	CalculateVMRowAndSwath_params->SurfaceSizeInMALL = mode_lib->ms.SurfaceSizeInMALL;
	CalculateVMRowAndSwath_params->PTEBufferSizeInRequestsLuma = mode_lib->ip.dpte_buffer_size_in_pte_reqs_luma;
	CalculateVMRowAndSwath_params->PTEBufferSizeInRequestsChroma = mode_lib->ip.dpte_buffer_size_in_pte_reqs_chroma;
	CalculateVMRowAndSwath_params->MALLAllocatedForDCN = mode_lib->soc.mall_allocated_for_dcn_mbytes;
	CalculateVMRowAndSwath_params->SwathWidthY = mode_lib->ms.SwathWidthY;
	CalculateVMRowAndSwath_params->SwathWidthC = mode_lib->ms.SwathWidthC;
	CalculateVMRowAndSwath_params->HostVMMinPageSize = mode_lib->soc.hostvm_min_page_size_kbytes;
	CalculateVMRowAndSwath_params->DCCMetaBufferSizeBytes = mode_lib->ip.dcc_meta_buffer_size_bytes;
	CalculateVMRowAndSwath_params->mrq_present = mode_lib->ip.dcn_mrq_present;

	// output
	CalculateVMRowAndSwath_params->PTEBufferSizeNotExceeded = mode_lib->ms.PTEBufferSizeNotExceeded;
	CalculateVMRowAndSwath_params->dpte_row_width_luma_ub = s->dummy_integer_array[12];
	CalculateVMRowAndSwath_params->dpte_row_width_chroma_ub = s->dummy_integer_array[13];
	CalculateVMRowAndSwath_params->dpte_row_height_luma = mode_lib->ms.dpte_row_height;
	CalculateVMRowAndSwath_params->dpte_row_height_chroma = mode_lib->ms.dpte_row_height_chroma;
	CalculateVMRowAndSwath_params->dpte_row_height_linear_luma = s->dummy_integer_array[14]; // VBA_DELTA
	CalculateVMRowAndSwath_params->dpte_row_height_linear_chroma = s->dummy_integer_array[15]; // VBA_DELTA
	CalculateVMRowAndSwath_params->vm_group_bytes = s->dummy_integer_array[16];
	CalculateVMRowAndSwath_params->dpte_group_bytes = mode_lib->ms.dpte_group_bytes;
	CalculateVMRowAndSwath_params->PixelPTEReqWidthY = s->dummy_integer_array[17];
	CalculateVMRowAndSwath_params->PixelPTEReqHeightY = s->dummy_integer_array[18];
	CalculateVMRowAndSwath_params->PTERequestSizeY = s->dummy_integer_array[19];
	CalculateVMRowAndSwath_params->PixelPTEReqWidthC = s->dummy_integer_array[20];
	CalculateVMRowAndSwath_params->PixelPTEReqHeightC = s->dummy_integer_array[21];
	CalculateVMRowAndSwath_params->PTERequestSizeC = s->dummy_integer_array[22];
	CalculateVMRowAndSwath_params->vmpg_width_y = s->vmpg_width_y;
	CalculateVMRowAndSwath_params->vmpg_height_y = s->vmpg_height_y;
	CalculateVMRowAndSwath_params->vmpg_width_c = s->vmpg_width_c;
	CalculateVMRowAndSwath_params->vmpg_height_c = s->vmpg_height_c;
	CalculateVMRowAndSwath_params->dpde0_bytes_per_frame_ub_l = s->dummy_integer_array[23];
	CalculateVMRowAndSwath_params->dpde0_bytes_per_frame_ub_c = s->dummy_integer_array[24];
	CalculateVMRowAndSwath_params->PrefetchSourceLinesY = mode_lib->ms.PrefetchLinesY;
	CalculateVMRowAndSwath_params->PrefetchSourceLinesC = mode_lib->ms.PrefetchLinesC;
	CalculateVMRowAndSwath_params->VInitPreFillY = mode_lib->ms.PrefillY;
	CalculateVMRowAndSwath_params->VInitPreFillC = mode_lib->ms.PrefillC;
	CalculateVMRowAndSwath_params->MaxNumSwathY = mode_lib->ms.MaxNumSwathY;
	CalculateVMRowAndSwath_params->MaxNumSwathC = mode_lib->ms.MaxNumSwathC;
	CalculateVMRowAndSwath_params->dpte_row_bw = mode_lib->ms.dpte_row_bw;
	CalculateVMRowAndSwath_params->PixelPTEBytesPerRow = mode_lib->ms.DPTEBytesPerRow;
	CalculateVMRowAndSwath_params->dpte_row_bytes_per_row_l = s->dpte_row_bytes_per_row_l;
	CalculateVMRowAndSwath_params->dpte_row_bytes_per_row_c = s->dpte_row_bytes_per_row_c;
	CalculateVMRowAndSwath_params->vm_bytes = mode_lib->ms.vm_bytes;
	CalculateVMRowAndSwath_params->use_one_row_for_frame = mode_lib->ms.use_one_row_for_frame;
	CalculateVMRowAndSwath_params->use_one_row_for_frame_flip = mode_lib->ms.use_one_row_for_frame_flip;
	CalculateVMRowAndSwath_params->is_using_mall_for_ss = s->dummy_boolean_array[0];
	CalculateVMRowAndSwath_params->PTE_BUFFER_MODE = s->dummy_boolean_array[1];
	CalculateVMRowAndSwath_params->BIGK_FRAGMENT_SIZE = s->dummy_integer_array[25];
	CalculateVMRowAndSwath_params->DCCMetaBufferSizeNotExceeded = mode_lib->ms.DCCMetaBufferSizeNotExceeded;
	CalculateVMRowAndSwath_params->meta_row_bw = mode_lib->ms.meta_row_bw;
	CalculateVMRowAndSwath_params->meta_row_bytes = mode_lib->ms.meta_row_bytes;
	CalculateVMRowAndSwath_params->meta_row_bytes_per_row_ub_l = s->meta_row_bytes_per_row_ub_l;
	CalculateVMRowAndSwath_params->meta_row_bytes_per_row_ub_c = s->meta_row_bytes_per_row_ub_c;
	CalculateVMRowAndSwath_params->meta_req_width_luma = s->dummy_integer_array[26];
	CalculateVMRowAndSwath_params->meta_req_height_luma = s->dummy_integer_array[27];
	CalculateVMRowAndSwath_params->meta_row_width_luma = s->dummy_integer_array[28];
	CalculateVMRowAndSwath_params->meta_row_height_luma = s->meta_row_height_luma;
	CalculateVMRowAndSwath_params->meta_pte_bytes_per_frame_ub_l = s->dummy_integer_array[29];
	CalculateVMRowAndSwath_params->meta_req_width_chroma = s->dummy_integer_array[30];
	CalculateVMRowAndSwath_params->meta_req_height_chroma = s->dummy_integer_array[31];
	CalculateVMRowAndSwath_params->meta_row_width_chroma = s->dummy_integer_array[32];
	CalculateVMRowAndSwath_params->meta_row_height_chroma = s->meta_row_height_chroma;
	CalculateVMRowAndSwath_params->meta_pte_bytes_per_frame_ub_c = s->dummy_integer_array[33];

	CalculateVMRowAndSwath(&mode_lib->scratch, CalculateVMRowAndSwath_params);

	mode_lib->ms.support.PTEBufferSizeNotExceeded = true;
	mode_lib->ms.support.DCCMetaBufferSizeNotExceeded = true;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.PTEBufferSizeNotExceeded[k] == false)
			mode_lib->ms.support.PTEBufferSizeNotExceeded = false;

		if (mode_lib->ms.DCCMetaBufferSizeNotExceeded[k] == false)
			mode_lib->ms.support.DCCMetaBufferSizeNotExceeded = false;

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, PTEBufferSizeNotExceeded = %u\n", __func__, k, mode_lib->ms.PTEBufferSizeNotExceeded[k]);
		dml2_printf("DML::%s: k=%u, DCCMetaBufferSizeNotExceeded = %u\n", __func__, k, mode_lib->ms.DCCMetaBufferSizeNotExceeded[k]);
#endif
	}
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: PTEBufferSizeNotExceeded = %u\n", __func__, mode_lib->ms.support.PTEBufferSizeNotExceeded);
	dml2_printf("DML::%s: DCCMetaBufferSizeNotExceeded = %u\n", __func__, mode_lib->ms.support.DCCMetaBufferSizeNotExceeded);
#endif

	/* VActive bytes to fetch for UCLK P-State */
	calculate_bytes_to_fetch_required_to_hide_latency_params->display_cfg = display_cfg;
	calculate_bytes_to_fetch_required_to_hide_latency_params->mrq_present = mode_lib->ip.dcn_mrq_present;

	calculate_bytes_to_fetch_required_to_hide_latency_params->num_active_planes = mode_lib->ms.num_active_planes;
	calculate_bytes_to_fetch_required_to_hide_latency_params->num_of_dpp = mode_lib->ms.NoOfDPP;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_height_l = s->meta_row_height_luma;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_height_c = s->meta_row_height_chroma;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_bytes_per_row_ub_l = s->meta_row_bytes_per_row_ub_l;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_bytes_per_row_ub_c = s->meta_row_bytes_per_row_ub_c;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_row_height_l = mode_lib->ms.dpte_row_height;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_row_height_c = mode_lib->ms.dpte_row_height_chroma;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_bytes_per_row_l = s->dpte_row_bytes_per_row_l;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_bytes_per_row_c = s->dpte_row_bytes_per_row_c;
	calculate_bytes_to_fetch_required_to_hide_latency_params->byte_per_pix_l = mode_lib->ms.BytePerPixelY;
	calculate_bytes_to_fetch_required_to_hide_latency_params->byte_per_pix_c = mode_lib->ms.BytePerPixelC;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_width_l = mode_lib->ms.SwathWidthY;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_width_c = mode_lib->ms.SwathWidthC;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_height_l = mode_lib->ms.SwathHeightY;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_height_c = mode_lib->ms.SwathHeightC;
	calculate_bytes_to_fetch_required_to_hide_latency_params->latency_to_hide_us = mode_lib->soc.power_management_parameters.dram_clk_change_blackout_us;

	/* outputs */
	calculate_bytes_to_fetch_required_to_hide_latency_params->bytes_required_l = s->pstate_bytes_required_l;
	calculate_bytes_to_fetch_required_to_hide_latency_params->bytes_required_c = s->pstate_bytes_required_c;

	calculate_bytes_to_fetch_required_to_hide_latency(calculate_bytes_to_fetch_required_to_hide_latency_params);

	/* Excess VActive bandwidth required to fill DET */
	calculate_excess_vactive_bandwidth_required(
			display_cfg,
			mode_lib->ms.num_active_planes,
			s->pstate_bytes_required_l,
			s->pstate_bytes_required_c,
			/* outputs */
			mode_lib->ms.excess_vactive_fill_bw_l,
			mode_lib->ms.excess_vactive_fill_bw_c);

	mode_lib->ms.UrgLatency = CalculateUrgentLatency(
		mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_latency_us.base_latency_us,
		mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_latency_us.base_latency_pixel_vm_us,
		mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_latency_us.base_latency_vm_us,
		mode_lib->soc.do_urgent_latency_adjustment,
		mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_latency_us.scaling_factor_fclk_us,
		mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_latency_us.scaling_factor_mhz,
		mode_lib->ms.FabricClock,
		mode_lib->ms.uclk_freq_mhz,
		mode_lib->soc.qos_parameters.qos_type,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.per_uclk_dpm_params[mode_lib->ms.qos_param_index].urgent_ramp_uclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.df_qos_response_time_fclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.max_round_trip_to_furthest_cs_fclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.mall_overhead_fclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.umc_urgent_ramp_latency_margin,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.fabric_max_transport_latency_margin);

	mode_lib->ms.TripToMemory = CalculateTripToMemory(
		mode_lib->ms.UrgLatency,
		mode_lib->ms.FabricClock,
		mode_lib->ms.uclk_freq_mhz,
		mode_lib->soc.qos_parameters.qos_type,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.per_uclk_dpm_params[mode_lib->ms.qos_param_index].trip_to_memory_uclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.max_round_trip_to_furthest_cs_fclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.mall_overhead_fclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.umc_max_latency_margin,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.fabric_max_transport_latency_margin);

	mode_lib->ms.TripToMemory = math_max2(mode_lib->ms.UrgLatency, mode_lib->ms.TripToMemory);

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		double line_time_us = (double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
		bool cursor_not_enough_urgent_latency_hiding = 0;

		if (display_cfg->plane_descriptors[k].cursor.num_cursors > 0) {
			calculate_cursor_req_attributes(
				display_cfg->plane_descriptors[k].cursor.cursor_width,
				display_cfg->plane_descriptors[k].cursor.cursor_bpp,

				// output
				&s->cursor_lines_per_chunk[k],
				&s->cursor_bytes_per_line[k],
				&s->cursor_bytes_per_chunk[k],
				&s->cursor_bytes[k]);

			calculate_cursor_urgent_burst_factor(
				mode_lib->ip.cursor_buffer_size,
				display_cfg->plane_descriptors[k].cursor.cursor_width,
				s->cursor_bytes_per_chunk[k],
				s->cursor_lines_per_chunk[k],
				line_time_us,
				mode_lib->ms.UrgLatency,

				// output
				&mode_lib->ms.UrgentBurstFactorCursor[k],
				&cursor_not_enough_urgent_latency_hiding);
		}

		mode_lib->ms.UrgentBurstFactorCursorPre[k] = mode_lib->ms.UrgentBurstFactorCursor[k];

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%d, Calling CalculateUrgentBurstFactor\n", __func__, k);
		dml2_printf("DML::%s: k=%d, VRatio=%f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio);
		dml2_printf("DML::%s: k=%d, VRatioChroma=%f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio);
#endif

		CalculateUrgentBurstFactor(
			&display_cfg->plane_descriptors[k],
			mode_lib->ms.swath_width_luma_ub[k],
			mode_lib->ms.swath_width_chroma_ub[k],
			mode_lib->ms.SwathHeightY[k],
			mode_lib->ms.SwathHeightC[k],
			line_time_us,
			mode_lib->ms.UrgLatency,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio,
			mode_lib->ms.BytePerPixelInDETY[k],
			mode_lib->ms.BytePerPixelInDETC[k],
			mode_lib->ms.DETBufferSizeY[k],
			mode_lib->ms.DETBufferSizeC[k],

			// Output
			&mode_lib->ms.UrgentBurstFactorLuma[k],
			&mode_lib->ms.UrgentBurstFactorChroma[k],
			&mode_lib->ms.NotEnoughUrgentLatencyHiding[k]);

		mode_lib->ms.NotEnoughUrgentLatencyHiding[k] = mode_lib->ms.NotEnoughUrgentLatencyHiding[k] || cursor_not_enough_urgent_latency_hiding;
	}

	CalculateDCFCLKDeepSleep(
		display_cfg,
		mode_lib->ms.num_active_planes,
		mode_lib->ms.BytePerPixelY,
		mode_lib->ms.BytePerPixelC,
		mode_lib->ms.SwathWidthY,
		mode_lib->ms.SwathWidthC,
		mode_lib->ms.NoOfDPP,
		mode_lib->ms.PSCL_FACTOR,
		mode_lib->ms.PSCL_FACTOR_CHROMA,
		mode_lib->ms.RequiredDPPCLK,
		mode_lib->ms.vactive_sw_bw_l,
		mode_lib->ms.vactive_sw_bw_c,
		mode_lib->soc.return_bus_width_bytes,

		/* Output */
		&mode_lib->ms.dcfclk_deepsleep);

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0) {
			mode_lib->ms.WritebackDelayTime[k] = mode_lib->soc.qos_parameters.writeback.base_latency_us + CalculateWriteBackDelay(
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].pixel_format,
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].h_ratio,
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_ratio,
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_taps,
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_width,
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_height,
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].input_height,
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total) / mode_lib->ms.RequiredDISPCLK;
		} else {
			mode_lib->ms.WritebackDelayTime[k] = 0.0;
		}
	}

	// MaximumVStartup is actually Tvstartup_min in DCN4 programming guide
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		bool isInterlaceTiming = (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced && !mode_lib->ip.ptoi_supported);
		s->MaximumVStartup[k] = CalculateMaxVStartup(
			mode_lib->ip.ptoi_supported,
			mode_lib->ip.vblank_nom_default_us,
			&display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing,
			mode_lib->ms.WritebackDelayTime[k]);
		mode_lib->ms.MaxVStartupLines[k] = (isInterlaceTiming ? (2 * s->MaximumVStartup[k]) : s->MaximumVStartup[k]);
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: k=%u, MaximumVStartup = %u\n", __func__, k, s->MaximumVStartup[k]);
#endif

	/* Immediate Flip and MALL parameters */
	s->ImmediateFlipRequired = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		s->ImmediateFlipRequired = s->ImmediateFlipRequired || display_cfg->plane_descriptors[k].immediate_flip;
	}

	mode_lib->ms.support.ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.support.ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe =
			mode_lib->ms.support.ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe ||
			((display_cfg->hostvm_enable == true || display_cfg->plane_descriptors[k].immediate_flip == true) &&
				(display_cfg->plane_descriptors[k].overrides.uclk_pstate_change_strategy == dml2_uclk_pstate_change_strategy_force_mall_full_frame || dml_is_phantom_pipe(&display_cfg->plane_descriptors[k])));
	}

	mode_lib->ms.support.InvalidCombinationOfMALLUseForPStateAndStaticScreen = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.support.InvalidCombinationOfMALLUseForPStateAndStaticScreen = mode_lib->ms.support.InvalidCombinationOfMALLUseForPStateAndStaticScreen ||
			((display_cfg->plane_descriptors[k].overrides.refresh_from_mall == dml2_refresh_from_mall_mode_override_force_enable || display_cfg->plane_descriptors[k].overrides.refresh_from_mall == dml2_refresh_from_mall_mode_override_auto) && (dml_is_phantom_pipe(&display_cfg->plane_descriptors[k]))) ||
			((display_cfg->plane_descriptors[k].overrides.refresh_from_mall == dml2_refresh_from_mall_mode_override_force_disable || display_cfg->plane_descriptors[k].overrides.refresh_from_mall == dml2_refresh_from_mall_mode_override_auto) && (display_cfg->plane_descriptors[k].overrides.uclk_pstate_change_strategy == dml2_uclk_pstate_change_strategy_force_mall_full_frame));
	}

	s->FullFrameMALLPStateMethod = false;
	s->SubViewportMALLPStateMethod = false;
	s->PhantomPipeMALLPStateMethod = false;
	s->SubViewportMALLRefreshGreaterThan120Hz = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (display_cfg->plane_descriptors[k].overrides.uclk_pstate_change_strategy == dml2_uclk_pstate_change_strategy_force_mall_full_frame)
			s->FullFrameMALLPStateMethod = true;
		if (display_cfg->plane_descriptors[k].overrides.legacy_svp_config == dml2_svp_mode_override_main_pipe) {
			s->SubViewportMALLPStateMethod = true;
			if (!display_cfg->overrides.enable_subvp_implicit_pmo) {
				// For dv, small frame tests will have very high refresh rate
				unsigned long long refresh_rate = (unsigned long long) ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz * 1000 /
					(double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total /
					(double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total);
				if (refresh_rate > 120)
					s->SubViewportMALLRefreshGreaterThan120Hz = true;
			}
		}
		if (dml_is_phantom_pipe(&display_cfg->plane_descriptors[k]))
			s->PhantomPipeMALLPStateMethod = true;
	}
	mode_lib->ms.support.InvalidCombinationOfMALLUseForPState = (s->SubViewportMALLPStateMethod != s->PhantomPipeMALLPStateMethod) ||
		(s->SubViewportMALLPStateMethod && s->FullFrameMALLPStateMethod) || s->SubViewportMALLRefreshGreaterThan120Hz;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: SubViewportMALLPStateMethod = %u\n", __func__, s->SubViewportMALLPStateMethod);
	dml2_printf("DML::%s: PhantomPipeMALLPStateMethod = %u\n", __func__, s->PhantomPipeMALLPStateMethod);
	dml2_printf("DML::%s: FullFrameMALLPStateMethod = %u\n", __func__, s->FullFrameMALLPStateMethod);
	dml2_printf("DML::%s: SubViewportMALLRefreshGreaterThan120Hz = %u\n", __func__, s->SubViewportMALLRefreshGreaterThan120Hz);
	dml2_printf("DML::%s: InvalidCombinationOfMALLUseForPState = %u\n", __func__, mode_lib->ms.support.InvalidCombinationOfMALLUseForPState);
	dml2_printf("DML::%s: in_out_params->min_clk_index = %u\n", __func__, in_out_params->min_clk_index);
	dml2_printf("DML::%s: mode_lib->ms.DCFCLK = %f\n", __func__, mode_lib->ms.DCFCLK);
	dml2_printf("DML::%s: mode_lib->ms.FabricClock = %f\n", __func__, mode_lib->ms.FabricClock);
	dml2_printf("DML::%s: mode_lib->ms.uclk_freq_mhz = %f\n", __func__, mode_lib->ms.uclk_freq_mhz);
	dml2_printf("DML::%s: urgent latency tolarance = %f\n", __func__, ((mode_lib->ip.rob_buffer_size_kbytes - mode_lib->ip.pixel_chunk_size_kbytes) * 1024 / (mode_lib->ms.DCFCLK * mode_lib->soc.return_bus_width_bytes)));
#endif

	mode_lib->ms.support.OutstandingRequestsSupport = true;
	mode_lib->ms.support.OutstandingRequestsUrgencyAvoidance = true;

	mode_lib->ms.support.avg_urgent_latency_us
		= (mode_lib->soc.qos_parameters.qos_params.dcn4x.per_uclk_dpm_params[mode_lib->ms.qos_param_index].average_latency_when_urgent_uclk_cycles / mode_lib->ms.uclk_freq_mhz
			* (1 + mode_lib->soc.qos_parameters.qos_params.dcn4x.umc_average_latency_margin / 100.0)
			+ mode_lib->soc.qos_parameters.qos_params.dcn4x.average_transport_distance_fclk_cycles / mode_lib->ms.FabricClock)
		* (1 + mode_lib->soc.qos_parameters.qos_params.dcn4x.fabric_average_transport_latency_margin / 100.0);

	mode_lib->ms.support.avg_non_urgent_latency_us
		= (mode_lib->soc.qos_parameters.qos_params.dcn4x.per_uclk_dpm_params[mode_lib->ms.qos_param_index].average_latency_when_non_urgent_uclk_cycles / mode_lib->ms.uclk_freq_mhz
			* (1 + mode_lib->soc.qos_parameters.qos_params.dcn4x.umc_average_latency_margin / 100.0)
			+ mode_lib->soc.qos_parameters.qos_params.dcn4x.average_transport_distance_fclk_cycles / mode_lib->ms.FabricClock)
		* (1 + mode_lib->soc.qos_parameters.qos_params.dcn4x.fabric_average_transport_latency_margin / 100.0);

	mode_lib->ms.support.max_non_urgent_latency_us
		= mode_lib->soc.qos_parameters.qos_params.dcn4x.per_uclk_dpm_params[mode_lib->ms.qos_param_index].maximum_latency_when_non_urgent_uclk_cycles
		/ mode_lib->ms.uclk_freq_mhz * (1 + mode_lib->soc.qos_parameters.qos_params.dcn4x.umc_max_latency_margin / 100.0)
		+ mode_lib->soc.qos_parameters.qos_params.dcn4x.mall_overhead_fclk_cycles / mode_lib->ms.FabricClock
		+ mode_lib->soc.qos_parameters.qos_params.dcn4x.max_round_trip_to_furthest_cs_fclk_cycles / mode_lib->ms.FabricClock
		* (1 + mode_lib->soc.qos_parameters.qos_params.dcn4x.fabric_max_transport_latency_margin / 100.0);

	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {

		if (mode_lib->soc.qos_parameters.qos_type == dml2_qos_param_type_dcn4x) {
			outstanding_latency_us = (mode_lib->soc.max_outstanding_reqs * mode_lib->ms.support.request_size_bytes_luma[k]
				/ (mode_lib->ms.DCFCLK * mode_lib->soc.return_bus_width_bytes));

			if (outstanding_latency_us < mode_lib->ms.support.avg_urgent_latency_us) {
				mode_lib->ms.support.OutstandingRequestsSupport = false;
			}

			if (outstanding_latency_us < mode_lib->ms.support.avg_non_urgent_latency_us) {
				mode_lib->ms.support.OutstandingRequestsUrgencyAvoidance = false;
			}

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: avg_urgent_latency_us = %f\n", __func__, mode_lib->ms.support.avg_urgent_latency_us);
			dml2_printf("DML::%s: avg_non_urgent_latency_us = %f\n", __func__, mode_lib->ms.support.avg_non_urgent_latency_us);
			dml2_printf("DML::%s: k=%d, request_size_bytes_luma = %d\n", __func__, k, mode_lib->ms.support.request_size_bytes_luma[k]);
			dml2_printf("DML::%s: k=%d, outstanding_latency_us = %f (luma)\n", __func__, k, outstanding_latency_us);
#endif
		}

		if (mode_lib->soc.qos_parameters.qos_type == dml2_qos_param_type_dcn4x && mode_lib->ms.BytePerPixelC[k] > 0) {
			outstanding_latency_us = (mode_lib->soc.max_outstanding_reqs * mode_lib->ms.support.request_size_bytes_chroma[k]
				/ (mode_lib->ms.DCFCLK * mode_lib->soc.return_bus_width_bytes));

			if (outstanding_latency_us < mode_lib->ms.support.avg_urgent_latency_us) {
				mode_lib->ms.support.OutstandingRequestsSupport = false;
			}

			if (outstanding_latency_us < mode_lib->ms.support.avg_non_urgent_latency_us) {
				mode_lib->ms.support.OutstandingRequestsUrgencyAvoidance = false;
			}
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%d, request_size_bytes_chroma = %d\n", __func__, k, mode_lib->ms.support.request_size_bytes_chroma[k]);
			dml2_printf("DML::%s: k=%d, outstanding_latency_us = %f (chroma)\n", __func__, k, outstanding_latency_us);
#endif
		}
	}

	memset(calculate_mcache_setting_params, 0, sizeof(struct dml2_core_calcs_calculate_mcache_setting_params));
	if (mode_lib->soc.mcache_size_bytes == 0 || mode_lib->ip.dcn_mrq_present) {
		for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
			mode_lib->ms.mall_prefetch_sdp_overhead_factor[k] = 1.0;
			mode_lib->ms.mall_prefetch_dram_overhead_factor[k] = 1.0;
			mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0[k] = 1.0;
			mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p0[k] = 1.0;
			mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1[k] = 1.0;
			mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p1[k] = 1.0;
		}
	} else {
		for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
			calculate_mcache_setting_params->dcc_enable = display_cfg->plane_descriptors[k].surface.dcc.enable;
			calculate_mcache_setting_params->num_chans = mode_lib->soc.clk_table.dram_config.channel_count;
			calculate_mcache_setting_params->mem_word_bytes = mode_lib->soc.mem_word_bytes;
			calculate_mcache_setting_params->mcache_size_bytes = mode_lib->soc.mcache_size_bytes;
			calculate_mcache_setting_params->mcache_line_size_bytes = mode_lib->soc.mcache_line_size_bytes;
			calculate_mcache_setting_params->gpuvm_enable = display_cfg->gpuvm_enable;
			calculate_mcache_setting_params->gpuvm_page_size_kbytes = display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes;

			calculate_mcache_setting_params->source_format = display_cfg->plane_descriptors[k].pixel_format;
			calculate_mcache_setting_params->surf_vert = dml_is_vertical_rotation(display_cfg->plane_descriptors[k].composition.rotation_angle);
			calculate_mcache_setting_params->vp_stationary = display_cfg->plane_descriptors[k].composition.viewport.stationary;
			calculate_mcache_setting_params->tiling_mode = display_cfg->plane_descriptors[k].surface.tiling;
			calculate_mcache_setting_params->imall_enable = mode_lib->ip.imall_supported && display_cfg->plane_descriptors[k].overrides.legacy_svp_config == dml2_svp_mode_override_imall;

			calculate_mcache_setting_params->vp_start_x_l = display_cfg->plane_descriptors[k].composition.viewport.plane0.x_start;
			calculate_mcache_setting_params->vp_start_y_l = display_cfg->plane_descriptors[k].composition.viewport.plane0.y_start;
			calculate_mcache_setting_params->full_vp_width_l = display_cfg->plane_descriptors[k].composition.viewport.plane0.width;
			calculate_mcache_setting_params->full_vp_height_l = display_cfg->plane_descriptors[k].composition.viewport.plane0.height;
			calculate_mcache_setting_params->blk_width_l = mode_lib->ms.MacroTileWidthY[k];
			calculate_mcache_setting_params->blk_height_l = mode_lib->ms.MacroTileHeightY[k];
			calculate_mcache_setting_params->vmpg_width_l = s->vmpg_width_y[k];
			calculate_mcache_setting_params->vmpg_height_l = s->vmpg_height_y[k];
			calculate_mcache_setting_params->full_swath_bytes_l = s->full_swath_bytes_l[k];
			calculate_mcache_setting_params->bytes_per_pixel_l = mode_lib->ms.BytePerPixelY[k];

			calculate_mcache_setting_params->vp_start_x_c = display_cfg->plane_descriptors[k].composition.viewport.plane1.x_start;
			calculate_mcache_setting_params->vp_start_y_c = display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start;
			calculate_mcache_setting_params->full_vp_width_c = display_cfg->plane_descriptors[k].composition.viewport.plane1.width;
			calculate_mcache_setting_params->full_vp_height_c = display_cfg->plane_descriptors[k].composition.viewport.plane1.height;
			calculate_mcache_setting_params->blk_width_c = mode_lib->ms.MacroTileWidthC[k];
			calculate_mcache_setting_params->blk_height_c = mode_lib->ms.MacroTileHeightC[k];
			calculate_mcache_setting_params->vmpg_width_c = s->vmpg_width_c[k];
			calculate_mcache_setting_params->vmpg_height_c = s->vmpg_height_c[k];
			calculate_mcache_setting_params->full_swath_bytes_c = s->full_swath_bytes_c[k];
			calculate_mcache_setting_params->bytes_per_pixel_c = mode_lib->ms.BytePerPixelC[k];

			// output
			calculate_mcache_setting_params->dcc_dram_bw_nom_overhead_factor_l = &mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0[k];
			calculate_mcache_setting_params->dcc_dram_bw_pref_overhead_factor_l = &mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p0[k];
			calculate_mcache_setting_params->dcc_dram_bw_nom_overhead_factor_c = &mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1[k];
			calculate_mcache_setting_params->dcc_dram_bw_pref_overhead_factor_c = &mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p1[k];

			calculate_mcache_setting_params->num_mcaches_l = &mode_lib->ms.num_mcaches_l[k];
			calculate_mcache_setting_params->mcache_row_bytes_l = &mode_lib->ms.mcache_row_bytes_l[k];
			calculate_mcache_setting_params->mcache_offsets_l = mode_lib->ms.mcache_offsets_l[k];
			calculate_mcache_setting_params->mcache_shift_granularity_l = &mode_lib->ms.mcache_shift_granularity_l[k];

			calculate_mcache_setting_params->num_mcaches_c = &mode_lib->ms.num_mcaches_c[k];
			calculate_mcache_setting_params->mcache_row_bytes_c = &mode_lib->ms.mcache_row_bytes_c[k];
			calculate_mcache_setting_params->mcache_offsets_c = mode_lib->ms.mcache_offsets_c[k];
			calculate_mcache_setting_params->mcache_shift_granularity_c = &mode_lib->ms.mcache_shift_granularity_c[k];

			calculate_mcache_setting_params->mall_comb_mcache_l = &mode_lib->ms.mall_comb_mcache_l[k];
			calculate_mcache_setting_params->mall_comb_mcache_c = &mode_lib->ms.mall_comb_mcache_c[k];
			calculate_mcache_setting_params->lc_comb_mcache = &mode_lib->ms.lc_comb_mcache[k];

			calculate_mcache_setting(&mode_lib->scratch, calculate_mcache_setting_params);
		}

		calculate_mall_bw_overhead_factor(
				mode_lib->ms.mall_prefetch_sdp_overhead_factor,
				mode_lib->ms.mall_prefetch_dram_overhead_factor,

				// input
				display_cfg,
				mode_lib->ms.num_active_planes);
	}

	// Calculate all the bandwidth available
	// Need anothe bw for latency evaluation
	calculate_bandwidth_available(
		mode_lib->ms.support.avg_bandwidth_available_min, // not used
		mode_lib->ms.support.avg_bandwidth_available, // not used
		mode_lib->ms.support.urg_bandwidth_available_min_latency,
		mode_lib->ms.support.urg_bandwidth_available, // not used
		mode_lib->ms.support.urg_bandwidth_available_vm_only, // not used
		mode_lib->ms.support.urg_bandwidth_available_pixel_and_vm, // not used

		&mode_lib->soc,
		display_cfg->hostvm_enable,
		mode_lib->ms.DCFCLK,
		mode_lib->ms.FabricClock,
		mode_lib->ms.dram_bw_mbps);

	calculate_bandwidth_available(
		mode_lib->ms.support.avg_bandwidth_available_min,
		mode_lib->ms.support.avg_bandwidth_available,
		mode_lib->ms.support.urg_bandwidth_available_min,
		mode_lib->ms.support.urg_bandwidth_available,
		mode_lib->ms.support.urg_bandwidth_available_vm_only,
		mode_lib->ms.support.urg_bandwidth_available_pixel_and_vm,

		&mode_lib->soc,
		display_cfg->hostvm_enable,
		mode_lib->ms.MaxDCFCLK,
		mode_lib->ms.MaxFabricClock,
#ifdef DML_MODE_SUPPORT_USE_DPM_DRAM_BW
		mode_lib->ms.dram_bw_mbps);
#else
		mode_lib->ms.max_dram_bw_mbps);
#endif

	// Average BW support check
	calculate_avg_bandwidth_required(
		mode_lib->ms.support.avg_bandwidth_required,
		// input
		display_cfg,
		mode_lib->ms.num_active_planes,
		mode_lib->ms.vactive_sw_bw_l,
		mode_lib->ms.vactive_sw_bw_c,
		mode_lib->ms.cursor_bw,
		mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0,
		mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1,
		mode_lib->ms.mall_prefetch_dram_overhead_factor,
		mode_lib->ms.mall_prefetch_sdp_overhead_factor);

	for (m = 0; m < dml2_core_internal_bw_max; m++) { // check sdp and dram
		mode_lib->ms.support.avg_bandwidth_support_ok[dml2_core_internal_soc_state_sys_idle][m] = 1;
		mode_lib->ms.support.avg_bandwidth_support_ok[dml2_core_internal_soc_state_sys_active][m] = (mode_lib->ms.support.avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][m] <= mode_lib->ms.support.avg_bandwidth_available[dml2_core_internal_soc_state_sys_active][m]);
		mode_lib->ms.support.avg_bandwidth_support_ok[dml2_core_internal_soc_state_svp_prefetch][m] = (mode_lib->ms.support.avg_bandwidth_required[dml2_core_internal_soc_state_svp_prefetch][m] <= mode_lib->ms.support.avg_bandwidth_available[dml2_core_internal_soc_state_svp_prefetch][m]);
	}

	mode_lib->ms.support.AvgBandwidthSupport = true;
	mode_lib->ms.support.EnoughUrgentLatencyHidingSupport = true;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.NotEnoughUrgentLatencyHiding[k]) {
			mode_lib->ms.support.EnoughUrgentLatencyHidingSupport = false;
			dml2_printf("DML::%s: k=%u NotEnoughUrgentLatencyHiding set\n", __func__, k);

		}
	}
	for (m = 0; m < dml2_core_internal_soc_state_max; m++) {
		for (n = 0; n < dml2_core_internal_bw_max; n++) { // check sdp and dram
			if (!mode_lib->ms.support.avg_bandwidth_support_ok[m][n] && (m == dml2_core_internal_soc_state_sys_active || mode_lib->soc.mall_allocated_for_dcn_mbytes > 0)) {
				mode_lib->ms.support.AvgBandwidthSupport = false;
#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: avg_bandwidth_support_ok[%s][%s] not ok\n", __func__, dml2_core_internal_soc_state_type_str(m), dml2_core_internal_bw_type_str(n));
#endif
			}
		}
	}

	/* Prefetch Check */
	{
		mode_lib->ms.TimeCalc = 24 / mode_lib->ms.dcfclk_deepsleep;

		calculate_hostvm_inefficiency_factor(
				&s->HostVMInefficiencyFactor,
				&s->HostVMInefficiencyFactorPrefetch,

				display_cfg->gpuvm_enable,
				display_cfg->hostvm_enable,
				mode_lib->ip.remote_iommu_outstanding_translations,
				mode_lib->soc.max_outstanding_reqs,
				mode_lib->ms.support.urg_bandwidth_available_pixel_and_vm[dml2_core_internal_soc_state_sys_active],
				mode_lib->ms.support.urg_bandwidth_available_vm_only[dml2_core_internal_soc_state_sys_active]);

		mode_lib->ms.Total3dlutActive = 0;
		for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
			if (display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut)
				mode_lib->ms.Total3dlutActive = mode_lib->ms.Total3dlutActive + 1;

			// Calculate tdlut schedule related terms
			calculate_tdlut_setting_params->dispclk_mhz = mode_lib->ms.RequiredDISPCLK;
			calculate_tdlut_setting_params->setup_for_tdlut = display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut;
			calculate_tdlut_setting_params->tdlut_width_mode = display_cfg->plane_descriptors[k].tdlut.tdlut_width_mode;
			calculate_tdlut_setting_params->tdlut_addressing_mode = display_cfg->plane_descriptors[k].tdlut.tdlut_addressing_mode;
			calculate_tdlut_setting_params->cursor_buffer_size = mode_lib->ip.cursor_buffer_size;
			calculate_tdlut_setting_params->gpuvm_enable = display_cfg->gpuvm_enable;
			calculate_tdlut_setting_params->gpuvm_page_size_kbytes = display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes;
			calculate_tdlut_setting_params->tdlut_mpc_width_flag = display_cfg->plane_descriptors[k].tdlut.tdlut_mpc_width_flag;
			calculate_tdlut_setting_params->is_gfx11 = dml_get_gfx_version(display_cfg->plane_descriptors[k].surface.tiling);

			// output
			calculate_tdlut_setting_params->tdlut_pte_bytes_per_frame = &s->tdlut_pte_bytes_per_frame[k];
			calculate_tdlut_setting_params->tdlut_bytes_per_frame = &s->tdlut_bytes_per_frame[k];
			calculate_tdlut_setting_params->tdlut_groups_per_2row_ub = &s->tdlut_groups_per_2row_ub[k];
			calculate_tdlut_setting_params->tdlut_opt_time = &s->tdlut_opt_time[k];
			calculate_tdlut_setting_params->tdlut_drain_time = &s->tdlut_drain_time[k];
			calculate_tdlut_setting_params->tdlut_bytes_per_group = &s->tdlut_bytes_per_group[k];

			calculate_tdlut_setting(&mode_lib->scratch, calculate_tdlut_setting_params);
		}

		min_return_bw_for_latency = mode_lib->ms.support.urg_bandwidth_available_min_latency[dml2_core_internal_soc_state_sys_active];

		if (mode_lib->soc.qos_parameters.qos_type == dml2_qos_param_type_dcn3)
			s->ReorderingBytes = (unsigned int)(mode_lib->soc.clk_table.dram_config.channel_count * math_max3(mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_out_of_order_return_per_channel_pixel_only_bytes,
											mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes,
											mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_out_of_order_return_per_channel_vm_only_bytes));

		CalculateExtraLatency(
			display_cfg,
			mode_lib->ip.rob_buffer_size_kbytes,
			mode_lib->soc.qos_parameters.qos_params.dcn32x.loaded_round_trip_latency_fclk_cycles,
			s->ReorderingBytes,
			mode_lib->ms.DCFCLK,
			mode_lib->ms.FabricClock,
			mode_lib->ip.pixel_chunk_size_kbytes,
			min_return_bw_for_latency,
			mode_lib->ms.num_active_planes,
			mode_lib->ms.NoOfDPP,
			mode_lib->ms.dpte_group_bytes,
			s->tdlut_bytes_per_group,
			s->HostVMInefficiencyFactor,
			s->HostVMInefficiencyFactorPrefetch,
			mode_lib->soc.hostvm_min_page_size_kbytes,
			mode_lib->soc.qos_parameters.qos_type,
			!(display_cfg->overrides.max_outstanding_when_urgent_expected_disable),
			mode_lib->soc.max_outstanding_reqs,
			mode_lib->ms.support.request_size_bytes_luma,
			mode_lib->ms.support.request_size_bytes_chroma,
			mode_lib->ip.meta_chunk_size_kbytes,
			mode_lib->ip.dchub_arb_to_ret_delay,
			mode_lib->ms.TripToMemory,
			mode_lib->ip.hostvm_mode,

			// output
			&mode_lib->ms.ExtraLatency,
			&mode_lib->ms.ExtraLatency_sr,
			&mode_lib->ms.ExtraLatencyPrefetch);

		for (k = 0; k < mode_lib->ms.num_active_planes; k++)
			s->impacted_dst_y_pre[k] = 0;

		s->recalc_prefetch_schedule = 0;
		s->recalc_prefetch_done = 0;
		do {
			mode_lib->ms.support.PrefetchSupported = true;

			for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
				s->line_times[k] = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
				s->pixel_format[k] = display_cfg->plane_descriptors[k].pixel_format;

				s->lb_source_lines_l[k] = get_num_lb_source_lines(mode_lib->ip.max_line_buffer_lines, mode_lib->ip.line_buffer_size_bits,
																	mode_lib->ms.NoOfDPP[k],
																	display_cfg->plane_descriptors[k].composition.viewport.plane0.width,
																	display_cfg->plane_descriptors[k].composition.viewport.plane0.height,
																	display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio,
																	display_cfg->plane_descriptors[k].composition.rotation_angle);

				s->lb_source_lines_c[k] = get_num_lb_source_lines(mode_lib->ip.max_line_buffer_lines, mode_lib->ip.line_buffer_size_bits,
																	mode_lib->ms.NoOfDPP[k],
																	display_cfg->plane_descriptors[k].composition.viewport.plane1.width,
																	display_cfg->plane_descriptors[k].composition.viewport.plane1.height,
																	display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio,
																	display_cfg->plane_descriptors[k].composition.rotation_angle);

				struct dml2_core_internal_DmlPipe *myPipe = &s->myPipe;

				mode_lib->ms.TWait[k] = CalculateTWait(
					display_cfg->plane_descriptors[k].overrides.reserved_vblank_time_ns,
					mode_lib->ms.UrgLatency,
					mode_lib->ms.TripToMemory,
					!dml_is_phantom_pipe(&display_cfg->plane_descriptors[k]) && display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.drr_config.enabled ?
					get_g6_temp_read_blackout_us(&mode_lib->soc, (unsigned int)(mode_lib->ms.uclk_freq_mhz * 1000), in_out_params->min_clk_index) : 0.0);

				myPipe->Dppclk = mode_lib->ms.RequiredDPPCLK[k];
				myPipe->Dispclk = mode_lib->ms.RequiredDISPCLK;
				myPipe->PixelClock = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
				myPipe->DCFClkDeepSleep = mode_lib->ms.dcfclk_deepsleep;
				myPipe->DPPPerSurface = mode_lib->ms.NoOfDPP[k];
				myPipe->ScalerEnabled = display_cfg->plane_descriptors[k].composition.scaler_info.enabled;
				myPipe->VRatio = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
				myPipe->VRatioChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
				myPipe->VTaps = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps;
				myPipe->VTapsChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps;
				myPipe->RotationAngle = display_cfg->plane_descriptors[k].composition.rotation_angle;
				myPipe->mirrored = display_cfg->plane_descriptors[k].composition.mirrored;
				myPipe->BlockWidth256BytesY = mode_lib->ms.Read256BlockWidthY[k];
				myPipe->BlockHeight256BytesY = mode_lib->ms.Read256BlockHeightY[k];
				myPipe->BlockWidth256BytesC = mode_lib->ms.Read256BlockWidthC[k];
				myPipe->BlockHeight256BytesC = mode_lib->ms.Read256BlockHeightC[k];
				myPipe->InterlaceEnable = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced;
				myPipe->NumberOfCursors = display_cfg->plane_descriptors[k].cursor.num_cursors;
				myPipe->VBlank = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_active;
				myPipe->HTotal = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total;
				myPipe->HActive = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active;
				myPipe->DCCEnable = display_cfg->plane_descriptors[k].surface.dcc.enable;
				myPipe->ODMMode = mode_lib->ms.ODMMode[k];
				myPipe->SourcePixelFormat = display_cfg->plane_descriptors[k].pixel_format;
				myPipe->BytePerPixelY = mode_lib->ms.BytePerPixelY[k];
				myPipe->BytePerPixelC = mode_lib->ms.BytePerPixelC[k];
				myPipe->ProgressiveToInterlaceUnitInOPP = mode_lib->ip.ptoi_supported;

#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: Calling CalculatePrefetchSchedule for k=%u\n", __func__, k);
				dml2_printf("DML::%s: MaximumVStartup = %u\n", __func__, s->MaximumVStartup[k]);
#endif
				CalculatePrefetchSchedule_params->display_cfg = display_cfg;
				CalculatePrefetchSchedule_params->HostVMInefficiencyFactor = s->HostVMInefficiencyFactorPrefetch;
				CalculatePrefetchSchedule_params->myPipe = myPipe;
				CalculatePrefetchSchedule_params->DSCDelay = mode_lib->ms.DSCDelay[k];
				CalculatePrefetchSchedule_params->DPPCLKDelaySubtotalPlusCNVCFormater = mode_lib->ip.dppclk_delay_subtotal + mode_lib->ip.dppclk_delay_cnvc_formatter;
				CalculatePrefetchSchedule_params->DPPCLKDelaySCL = mode_lib->ip.dppclk_delay_scl;
				CalculatePrefetchSchedule_params->DPPCLKDelaySCLLBOnly = mode_lib->ip.dppclk_delay_scl_lb_only;
				CalculatePrefetchSchedule_params->DPPCLKDelayCNVCCursor = mode_lib->ip.dppclk_delay_cnvc_cursor;
				CalculatePrefetchSchedule_params->DISPCLKDelaySubtotal = mode_lib->ip.dispclk_delay_subtotal;
				CalculatePrefetchSchedule_params->DPP_RECOUT_WIDTH = (unsigned int)(mode_lib->ms.SwathWidthY[k] / display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio);
				CalculatePrefetchSchedule_params->OutputFormat = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format;
				CalculatePrefetchSchedule_params->MaxInterDCNTileRepeaters = mode_lib->ip.max_inter_dcn_tile_repeaters;
				CalculatePrefetchSchedule_params->VStartup = s->MaximumVStartup[k];
				CalculatePrefetchSchedule_params->HostVMMinPageSize = mode_lib->soc.hostvm_min_page_size_kbytes;
				CalculatePrefetchSchedule_params->DynamicMetadataEnable = display_cfg->plane_descriptors[k].dynamic_meta_data.enable;
				CalculatePrefetchSchedule_params->DynamicMetadataVMEnabled = mode_lib->ip.dynamic_metadata_vm_enabled;
				CalculatePrefetchSchedule_params->DynamicMetadataLinesBeforeActiveRequired = display_cfg->plane_descriptors[k].dynamic_meta_data.lines_before_active_required;
				CalculatePrefetchSchedule_params->DynamicMetadataTransmittedBytes = display_cfg->plane_descriptors[k].dynamic_meta_data.transmitted_bytes;
				CalculatePrefetchSchedule_params->UrgentLatency = mode_lib->ms.UrgLatency;
				CalculatePrefetchSchedule_params->ExtraLatencyPrefetch = mode_lib->ms.ExtraLatencyPrefetch;
				CalculatePrefetchSchedule_params->TCalc = mode_lib->ms.TimeCalc;
				CalculatePrefetchSchedule_params->vm_bytes = mode_lib->ms.vm_bytes[k];
				CalculatePrefetchSchedule_params->PixelPTEBytesPerRow = mode_lib->ms.DPTEBytesPerRow[k];
				CalculatePrefetchSchedule_params->PrefetchSourceLinesY = mode_lib->ms.PrefetchLinesY[k];
				CalculatePrefetchSchedule_params->VInitPreFillY = mode_lib->ms.PrefillY[k];
				CalculatePrefetchSchedule_params->MaxNumSwathY = mode_lib->ms.MaxNumSwathY[k];
				CalculatePrefetchSchedule_params->PrefetchSourceLinesC = mode_lib->ms.PrefetchLinesC[k];
				CalculatePrefetchSchedule_params->VInitPreFillC = mode_lib->ms.PrefillC[k];
				CalculatePrefetchSchedule_params->MaxNumSwathC = mode_lib->ms.MaxNumSwathC[k];
				CalculatePrefetchSchedule_params->swath_width_luma_ub = mode_lib->ms.swath_width_luma_ub[k];
				CalculatePrefetchSchedule_params->swath_width_chroma_ub = mode_lib->ms.swath_width_chroma_ub[k];
				CalculatePrefetchSchedule_params->SwathHeightY = mode_lib->ms.SwathHeightY[k];
				CalculatePrefetchSchedule_params->SwathHeightC = mode_lib->ms.SwathHeightC[k];
				CalculatePrefetchSchedule_params->TWait = mode_lib->ms.TWait[k];
				CalculatePrefetchSchedule_params->Ttrip = mode_lib->ms.TripToMemory;
				CalculatePrefetchSchedule_params->Turg = mode_lib->ms.UrgLatency;
				CalculatePrefetchSchedule_params->setup_for_tdlut = display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut;
				CalculatePrefetchSchedule_params->tdlut_pte_bytes_per_frame = s->tdlut_pte_bytes_per_frame[k];
				CalculatePrefetchSchedule_params->tdlut_bytes_per_frame = s->tdlut_bytes_per_frame[k];
				CalculatePrefetchSchedule_params->tdlut_opt_time = s->tdlut_opt_time[k];
				CalculatePrefetchSchedule_params->tdlut_drain_time = s->tdlut_drain_time[k];
				CalculatePrefetchSchedule_params->num_cursors = (display_cfg->plane_descriptors[k].cursor.cursor_width > 0);
				CalculatePrefetchSchedule_params->cursor_bytes_per_chunk = s->cursor_bytes_per_chunk[k];
				CalculatePrefetchSchedule_params->cursor_bytes_per_line = s->cursor_bytes_per_line[k];
				CalculatePrefetchSchedule_params->dcc_enable = display_cfg->plane_descriptors[k].surface.dcc.enable;
				CalculatePrefetchSchedule_params->mrq_present = mode_lib->ip.dcn_mrq_present;
				CalculatePrefetchSchedule_params->meta_row_bytes = mode_lib->ms.meta_row_bytes[k];
				CalculatePrefetchSchedule_params->mall_prefetch_sdp_overhead_factor = mode_lib->ms.mall_prefetch_sdp_overhead_factor[k];
				CalculatePrefetchSchedule_params->impacted_dst_y_pre = s->impacted_dst_y_pre[k];
				CalculatePrefetchSchedule_params->vactive_sw_bw_l = mode_lib->ms.vactive_sw_bw_l[k];
				CalculatePrefetchSchedule_params->vactive_sw_bw_c = mode_lib->ms.vactive_sw_bw_c[k];

				// output
				CalculatePrefetchSchedule_params->DSTXAfterScaler = &s->DSTXAfterScaler[k];
				CalculatePrefetchSchedule_params->DSTYAfterScaler = &s->DSTYAfterScaler[k];
				CalculatePrefetchSchedule_params->dst_y_prefetch = &mode_lib->ms.dst_y_prefetch[k];
				CalculatePrefetchSchedule_params->dst_y_per_vm_vblank = &mode_lib->ms.LinesForVM[k];
				CalculatePrefetchSchedule_params->dst_y_per_row_vblank = &mode_lib->ms.LinesForDPTERow[k];
				CalculatePrefetchSchedule_params->VRatioPrefetchY = &mode_lib->ms.VRatioPreY[k];
				CalculatePrefetchSchedule_params->VRatioPrefetchC = &mode_lib->ms.VRatioPreC[k];
				CalculatePrefetchSchedule_params->RequiredPrefetchPixelDataBWLuma = &mode_lib->ms.RequiredPrefetchPixelDataBWLuma[k]; // prefetch_sw_bw_l
				CalculatePrefetchSchedule_params->RequiredPrefetchPixelDataBWChroma = &mode_lib->ms.RequiredPrefetchPixelDataBWChroma[k]; // prefetch_sw_bw_c
				CalculatePrefetchSchedule_params->NotEnoughTimeForDynamicMetadata = &mode_lib->ms.NoTimeForDynamicMetadata[k];
				CalculatePrefetchSchedule_params->Tno_bw = &mode_lib->ms.Tno_bw[k];
				CalculatePrefetchSchedule_params->Tno_bw_flip = &mode_lib->ms.Tno_bw_flip[k];
				CalculatePrefetchSchedule_params->prefetch_vmrow_bw = &mode_lib->ms.prefetch_vmrow_bw[k];
				CalculatePrefetchSchedule_params->Tdmdl_vm = &s->dummy_single[0];
				CalculatePrefetchSchedule_params->Tdmdl = &s->dummy_single[1];
				CalculatePrefetchSchedule_params->TSetup = &s->dummy_single[2];
				CalculatePrefetchSchedule_params->Tvm_trips = &s->Tvm_trips[k];
				CalculatePrefetchSchedule_params->Tr0_trips = &s->Tr0_trips[k];
				CalculatePrefetchSchedule_params->Tvm_trips_flip = &s->Tvm_trips_flip[k];
				CalculatePrefetchSchedule_params->Tr0_trips_flip = &s->Tr0_trips_flip[k];
				CalculatePrefetchSchedule_params->Tvm_trips_flip_rounded = &s->Tvm_trips_flip_rounded[k];
				CalculatePrefetchSchedule_params->Tr0_trips_flip_rounded = &s->Tr0_trips_flip_rounded[k];
				CalculatePrefetchSchedule_params->VUpdateOffsetPix = &s->dummy_integer[0];
				CalculatePrefetchSchedule_params->VUpdateWidthPix = &s->dummy_integer[1];
				CalculatePrefetchSchedule_params->VReadyOffsetPix = &s->dummy_integer[2];
				CalculatePrefetchSchedule_params->prefetch_cursor_bw = &mode_lib->ms.prefetch_cursor_bw[k];
				CalculatePrefetchSchedule_params->prefetch_sw_bytes = &s->prefetch_sw_bytes[k];
				CalculatePrefetchSchedule_params->Tpre_rounded = &s->Tpre_rounded[k];
				CalculatePrefetchSchedule_params->Tpre_oto = &s->Tpre_oto[k];

				mode_lib->ms.NoTimeForPrefetch[k] = CalculatePrefetchSchedule(&mode_lib->scratch, CalculatePrefetchSchedule_params);

				mode_lib->ms.support.PrefetchSupported &= !mode_lib->ms.NoTimeForPrefetch[k];
				dml2_printf("DML::%s: k=%d, dst_y_per_vm_vblank = %f\n", __func__, k, *CalculatePrefetchSchedule_params->dst_y_per_vm_vblank);
				dml2_printf("DML::%s: k=%d, dst_y_per_row_vblank = %f\n", __func__, k, *CalculatePrefetchSchedule_params->dst_y_per_row_vblank);
			} // for k num_planes

			for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
				if (mode_lib->ms.dst_y_prefetch[k] < 2.0
					|| mode_lib->ms.LinesForVM[k] >= 32.0
					|| mode_lib->ms.LinesForDPTERow[k] >= 16.0
					|| mode_lib->ms.NoTimeForPrefetch[k] == true
					|| s->DSTYAfterScaler[k] > 8) {
					mode_lib->ms.support.PrefetchSupported = false;
					dml2_printf("DML::%s: k=%d, dst_y_prefetch=%f (should not be < 2)\n", __func__, k, mode_lib->ms.dst_y_prefetch[k]);
					dml2_printf("DML::%s: k=%d, LinesForVM=%f (should not be >= 32)\n", __func__, k, mode_lib->ms.LinesForVM[k]);
					dml2_printf("DML::%s: k=%d, LinesForDPTERow=%f (should not be >= 16)\n", __func__, k, mode_lib->ms.LinesForDPTERow[k]);
					dml2_printf("DML::%s: k=%d, DSTYAfterScaler=%d (should be <= 8)\n", __func__, k, s->DSTYAfterScaler[k]);
					dml2_printf("DML::%s: k=%d, NoTimeForPrefetch=%d\n", __func__, k, mode_lib->ms.NoTimeForPrefetch[k]);
				}
			}

			mode_lib->ms.support.DynamicMetadataSupported = true;
			for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
				if (mode_lib->ms.NoTimeForDynamicMetadata[k] == true) {
					mode_lib->ms.support.DynamicMetadataSupported = false;
				}
			}

			mode_lib->ms.support.VRatioInPrefetchSupported = true;
			for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
				if (mode_lib->ms.VRatioPreY[k] > __DML2_CALCS_MAX_VRATIO_PRE__ ||
					mode_lib->ms.VRatioPreC[k] > __DML2_CALCS_MAX_VRATIO_PRE__) {
					mode_lib->ms.support.VRatioInPrefetchSupported = false;
					dml2_printf("DML::%s: k=%d VRatioPreY = %f (should be <= %f)\n", __func__, k, mode_lib->ms.VRatioPreY[k], __DML2_CALCS_MAX_VRATIO_PRE__);
					dml2_printf("DML::%s: k=%d VRatioPreC = %f (should be <= %f)\n", __func__, k, mode_lib->ms.VRatioPreC[k], __DML2_CALCS_MAX_VRATIO_PRE__);
					dml2_printf("DML::%s: VRatioInPrefetchSupported = %u\n", __func__, mode_lib->ms.support.VRatioInPrefetchSupported);
				}
			}

			mode_lib->ms.support.PrefetchSupported &= mode_lib->ms.support.VRatioInPrefetchSupported;

			// By default, do not recalc prefetch schedule
			s->recalc_prefetch_schedule = 0;

			// Only do urg vs prefetch bandwidth check, flip schedule check, power saving feature support check IF the Prefetch Schedule Check is ok
			if (mode_lib->ms.support.PrefetchSupported) {
				for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
					// Calculate Urgent burst factor for prefetch
#ifdef __DML_VBA_DEBUG__
					dml2_printf("DML::%s: k=%d, Calling CalculateUrgentBurstFactor (for prefetch)\n", __func__, k);
					dml2_printf("DML::%s: k=%d, VRatioPreY=%f\n", __func__, k, mode_lib->ms.VRatioPreY[k]);
					dml2_printf("DML::%s: k=%d, VRatioPreC=%f\n", __func__, k, mode_lib->ms.VRatioPreC[k]);
#endif
					CalculateUrgentBurstFactor(
						&display_cfg->plane_descriptors[k],
						mode_lib->ms.swath_width_luma_ub[k],
						mode_lib->ms.swath_width_chroma_ub[k],
						mode_lib->ms.SwathHeightY[k],
						mode_lib->ms.SwathHeightC[k],
						s->line_times[k],
						mode_lib->ms.UrgLatency,
						mode_lib->ms.VRatioPreY[k],
						mode_lib->ms.VRatioPreC[k],
						mode_lib->ms.BytePerPixelInDETY[k],
						mode_lib->ms.BytePerPixelInDETC[k],
						mode_lib->ms.DETBufferSizeY[k],
						mode_lib->ms.DETBufferSizeC[k],
						/* Output */
						&mode_lib->ms.UrgentBurstFactorLumaPre[k],
						&mode_lib->ms.UrgentBurstFactorChromaPre[k],
						&mode_lib->ms.NotEnoughUrgentLatencyHidingPre[k]);
				}

				// Calculate urgent bandwidth required, both urg and non urg peak bandwidth
				// assume flip bw is 0 at this point
				for (k = 0; k < mode_lib->ms.num_active_planes; k++)
					mode_lib->ms.final_flip_bw[k] = 0;

				calculate_peak_bandwidth_params->urg_vactive_bandwidth_required = mode_lib->ms.support.urg_vactive_bandwidth_required;
				calculate_peak_bandwidth_params->urg_bandwidth_required = mode_lib->ms.support.urg_bandwidth_required;
				calculate_peak_bandwidth_params->urg_bandwidth_required_qual = mode_lib->ms.support.urg_bandwidth_required_qual;
				calculate_peak_bandwidth_params->non_urg_bandwidth_required = mode_lib->ms.support.non_urg_bandwidth_required;
				calculate_peak_bandwidth_params->surface_avg_vactive_required_bw = mode_lib->ms.surface_avg_vactive_required_bw;
				calculate_peak_bandwidth_params->surface_peak_required_bw = mode_lib->ms.surface_peak_required_bw;

				calculate_peak_bandwidth_params->display_cfg = display_cfg;
				calculate_peak_bandwidth_params->inc_flip_bw = 0;
				calculate_peak_bandwidth_params->num_active_planes =  mode_lib->ms.num_active_planes;
				calculate_peak_bandwidth_params->num_of_dpp = mode_lib->ms.NoOfDPP;
				calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p0 = mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0;
				calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p1 = mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1;
				calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p0 = mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p0;
				calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p1 = mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p1;
				calculate_peak_bandwidth_params->mall_prefetch_sdp_overhead_factor = mode_lib->ms.mall_prefetch_sdp_overhead_factor;
				calculate_peak_bandwidth_params->mall_prefetch_dram_overhead_factor = mode_lib->ms.mall_prefetch_dram_overhead_factor;

				calculate_peak_bandwidth_params->surface_read_bandwidth_l = mode_lib->ms.vactive_sw_bw_l;
				calculate_peak_bandwidth_params->surface_read_bandwidth_c = mode_lib->ms.vactive_sw_bw_c;
				calculate_peak_bandwidth_params->prefetch_bandwidth_l = mode_lib->ms.RequiredPrefetchPixelDataBWLuma;
				calculate_peak_bandwidth_params->prefetch_bandwidth_c = mode_lib->ms.RequiredPrefetchPixelDataBWChroma;
				calculate_peak_bandwidth_params->excess_vactive_fill_bw_l = mode_lib->ms.excess_vactive_fill_bw_l;
				calculate_peak_bandwidth_params->excess_vactive_fill_bw_c = mode_lib->ms.excess_vactive_fill_bw_c;
				calculate_peak_bandwidth_params->cursor_bw = mode_lib->ms.cursor_bw;
				calculate_peak_bandwidth_params->dpte_row_bw = mode_lib->ms.dpte_row_bw;
				calculate_peak_bandwidth_params->meta_row_bw = mode_lib->ms.meta_row_bw;
				calculate_peak_bandwidth_params->prefetch_cursor_bw = mode_lib->ms.prefetch_cursor_bw;
				calculate_peak_bandwidth_params->prefetch_vmrow_bw = mode_lib->ms.prefetch_vmrow_bw;
				calculate_peak_bandwidth_params->flip_bw = mode_lib->ms.final_flip_bw;
				calculate_peak_bandwidth_params->urgent_burst_factor_l = mode_lib->ms.UrgentBurstFactorLuma;
				calculate_peak_bandwidth_params->urgent_burst_factor_c = mode_lib->ms.UrgentBurstFactorChroma;
				calculate_peak_bandwidth_params->urgent_burst_factor_cursor = mode_lib->ms.UrgentBurstFactorCursor;
				calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_l = mode_lib->ms.UrgentBurstFactorLumaPre;
				calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_c = mode_lib->ms.UrgentBurstFactorChromaPre;
				calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_cursor = mode_lib->ms.UrgentBurstFactorCursorPre;

				calculate_peak_bandwidth_required(
						&mode_lib->scratch,
						calculate_peak_bandwidth_params);

				// Check urg peak bandwidth against available urg bw
				// check at SDP and DRAM, for all soc states (SVP prefetch an Sys Active)
				check_urgent_bandwidth_support(
					&s->dummy_single[0], // double* frac_urg_bandwidth
					&s->dummy_single[1], // double* frac_urg_bandwidth_mall
					&mode_lib->ms.support.UrgVactiveBandwidthSupport,
					&mode_lib->ms.support.PrefetchBandwidthSupported,

					mode_lib->soc.mall_allocated_for_dcn_mbytes,
					mode_lib->ms.support.non_urg_bandwidth_required,
					mode_lib->ms.support.urg_vactive_bandwidth_required,
					mode_lib->ms.support.urg_bandwidth_required,
					mode_lib->ms.support.urg_bandwidth_available);

				mode_lib->ms.support.PrefetchSupported &= mode_lib->ms.support.PrefetchBandwidthSupported;
				dml2_printf("DML::%s: PrefetchBandwidthSupported=%0d\n", __func__, mode_lib->ms.support.PrefetchBandwidthSupported);

				for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
					if (mode_lib->ms.NotEnoughUrgentLatencyHidingPre[k]) {
						mode_lib->ms.support.PrefetchSupported = false;
						dml2_printf("DML::%s: k=%d, NotEnoughUrgentLatencyHidingPre=%d\n", __func__, k, mode_lib->ms.NotEnoughUrgentLatencyHidingPre[k]);
					}
				}

#ifdef DML_GLOBAL_PREFETCH_CHECK
				if (mode_lib->ms.support.PrefetchSupported && mode_lib->ms.num_active_planes > 1 && s->recalc_prefetch_done == 0) {
					CheckGlobalPrefetchAdmissibility_params->num_active_planes =  mode_lib->ms.num_active_planes;
					CheckGlobalPrefetchAdmissibility_params->pixel_format = s->pixel_format;
					CheckGlobalPrefetchAdmissibility_params->chunk_bytes_l = mode_lib->ip.pixel_chunk_size_kbytes * 1024;
					CheckGlobalPrefetchAdmissibility_params->chunk_bytes_c = mode_lib->ip.pixel_chunk_size_kbytes * 1024;
					CheckGlobalPrefetchAdmissibility_params->lb_source_lines_l = s->lb_source_lines_l;
					CheckGlobalPrefetchAdmissibility_params->lb_source_lines_c = s->lb_source_lines_c;
					CheckGlobalPrefetchAdmissibility_params->swath_height_l =  mode_lib->ms.SwathHeightY;
					CheckGlobalPrefetchAdmissibility_params->swath_height_c =  mode_lib->ms.SwathHeightC;
					CheckGlobalPrefetchAdmissibility_params->rob_buffer_size_kbytes = mode_lib->ip.rob_buffer_size_kbytes;
					CheckGlobalPrefetchAdmissibility_params->compressed_buffer_size_kbytes = mode_lib->ms.CompressedBufferSizeInkByte;
					CheckGlobalPrefetchAdmissibility_params->detile_buffer_size_bytes_l = mode_lib->ms.DETBufferSizeY;
					CheckGlobalPrefetchAdmissibility_params->detile_buffer_size_bytes_c = mode_lib->ms.DETBufferSizeC;
					CheckGlobalPrefetchAdmissibility_params->full_swath_bytes_l = s->full_swath_bytes_l;
					CheckGlobalPrefetchAdmissibility_params->full_swath_bytes_c = s->full_swath_bytes_c;
					CheckGlobalPrefetchAdmissibility_params->prefetch_sw_bytes = s->prefetch_sw_bytes;
					CheckGlobalPrefetchAdmissibility_params->Tpre_rounded = s->Tpre_rounded;
					CheckGlobalPrefetchAdmissibility_params->Tpre_oto = s->Tpre_oto;
					CheckGlobalPrefetchAdmissibility_params->estimated_urg_bandwidth_required_mbps = mode_lib->ms.support.urg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp];
					CheckGlobalPrefetchAdmissibility_params->line_time = s->line_times;
					CheckGlobalPrefetchAdmissibility_params->dst_y_prefetch = mode_lib->ms.dst_y_prefetch;
					if (CheckGlobalPrefetchAdmissibility_params->estimated_urg_bandwidth_required_mbps < 10 * 1024)
						CheckGlobalPrefetchAdmissibility_params->estimated_urg_bandwidth_required_mbps = 10 * 1024;

					CheckGlobalPrefetchAdmissibility_params->estimated_dcfclk_mhz = (CheckGlobalPrefetchAdmissibility_params->estimated_urg_bandwidth_required_mbps / (double) mode_lib->soc.return_bus_width_bytes) /
																					((double)mode_lib->soc.qos_parameters.derate_table.system_active_urgent.dcfclk_derate_percent / 100.0);

					// if recalc_prefetch_schedule is set, recalculate the prefetch schedule with the new impacted_Tpre, prefetch should be possible
					CheckGlobalPrefetchAdmissibility_params->recalc_prefetch_schedule = &s->recalc_prefetch_schedule;
					CheckGlobalPrefetchAdmissibility_params->impacted_dst_y_pre = s->impacted_dst_y_pre;
					mode_lib->ms.support.PrefetchSupported = CheckGlobalPrefetchAdmissibility(&mode_lib->scratch, CheckGlobalPrefetchAdmissibility_params);
					s->recalc_prefetch_done = 1;
					s->recalc_prefetch_schedule = 1;
				}
#endif
			} // prefetch schedule ok, do urg bw and flip schedule
		} while (s->recalc_prefetch_schedule);

		// Flip Schedule
		// Both prefetch schedule and BW okay
		if (mode_lib->ms.support.PrefetchSupported == true) {
			mode_lib->ms.BandwidthAvailableForImmediateFlip =
				get_bandwidth_available_for_immediate_flip(
					dml2_core_internal_soc_state_sys_active,
					mode_lib->ms.support.urg_bandwidth_required_qual, // no flip
					mode_lib->ms.support.urg_bandwidth_available);

			mode_lib->ms.TotImmediateFlipBytes = 0;
			for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
				if (display_cfg->plane_descriptors[k].immediate_flip) {
					s->per_pipe_flip_bytes[k] = get_pipe_flip_bytes(
									s->HostVMInefficiencyFactor,
									mode_lib->ms.vm_bytes[k],
									mode_lib->ms.DPTEBytesPerRow[k],
									mode_lib->ms.meta_row_bytes[k]);
				} else {
					s->per_pipe_flip_bytes[k] = 0;
				}
				mode_lib->ms.TotImmediateFlipBytes += s->per_pipe_flip_bytes[k] * mode_lib->ms.NoOfDPP[k];

			}

			for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
				CalculateFlipSchedule(
					&mode_lib->scratch,
					display_cfg->plane_descriptors[k].immediate_flip,
					1, // use_lb_flip_bw
					s->HostVMInefficiencyFactor,
					s->Tvm_trips_flip[k],
					s->Tr0_trips_flip[k],
					s->Tvm_trips_flip_rounded[k],
					s->Tr0_trips_flip_rounded[k],
					display_cfg->gpuvm_enable,
					mode_lib->ms.vm_bytes[k],
					mode_lib->ms.DPTEBytesPerRow[k],
					mode_lib->ms.BandwidthAvailableForImmediateFlip,
					mode_lib->ms.TotImmediateFlipBytes,
					display_cfg->plane_descriptors[k].pixel_format,
					(display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)),
					display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio,
					display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio,
					mode_lib->ms.Tno_bw_flip[k],
					mode_lib->ms.dpte_row_height[k],
					mode_lib->ms.dpte_row_height_chroma[k],
					mode_lib->ms.use_one_row_for_frame_flip[k],
					mode_lib->ip.max_flip_time_us,
					mode_lib->ip.max_flip_time_lines,
					s->per_pipe_flip_bytes[k],
					mode_lib->ms.meta_row_bytes[k],
					s->meta_row_height_luma[k],
					s->meta_row_height_chroma[k],
					mode_lib->ip.dcn_mrq_present && display_cfg->plane_descriptors[k].surface.dcc.enable,

					/* Output */
					&mode_lib->ms.dst_y_per_vm_flip[k],
					&mode_lib->ms.dst_y_per_row_flip[k],
					&mode_lib->ms.final_flip_bw[k],
					&mode_lib->ms.ImmediateFlipSupportedForPipe[k]);
			}

			calculate_peak_bandwidth_params->urg_vactive_bandwidth_required = s->dummy_bw;
			calculate_peak_bandwidth_params->urg_bandwidth_required = mode_lib->ms.support.urg_bandwidth_required_flip;
			calculate_peak_bandwidth_params->urg_bandwidth_required_qual = s->dummy_bw;
			calculate_peak_bandwidth_params->non_urg_bandwidth_required = mode_lib->ms.support.non_urg_bandwidth_required_flip;
			calculate_peak_bandwidth_params->surface_avg_vactive_required_bw = s->surface_dummy_bw;
			calculate_peak_bandwidth_params->surface_peak_required_bw = mode_lib->ms.surface_peak_required_bw;

			calculate_peak_bandwidth_params->display_cfg = display_cfg;
			calculate_peak_bandwidth_params->inc_flip_bw = 1;
			calculate_peak_bandwidth_params->num_active_planes = mode_lib->ms.num_active_planes;
			calculate_peak_bandwidth_params->num_of_dpp = mode_lib->ms.NoOfDPP;
			calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p0 = mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0;
			calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p1 = mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1;
			calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p0 = mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p0;
			calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p1 = mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p1;
			calculate_peak_bandwidth_params->mall_prefetch_sdp_overhead_factor = mode_lib->ms.mall_prefetch_sdp_overhead_factor;
			calculate_peak_bandwidth_params->mall_prefetch_dram_overhead_factor = mode_lib->ms.mall_prefetch_dram_overhead_factor;

			calculate_peak_bandwidth_params->surface_read_bandwidth_l = mode_lib->ms.vactive_sw_bw_l;
			calculate_peak_bandwidth_params->surface_read_bandwidth_c = mode_lib->ms.vactive_sw_bw_c;
			calculate_peak_bandwidth_params->prefetch_bandwidth_l = mode_lib->ms.RequiredPrefetchPixelDataBWLuma;
			calculate_peak_bandwidth_params->prefetch_bandwidth_c = mode_lib->ms.RequiredPrefetchPixelDataBWChroma;
			calculate_peak_bandwidth_params->excess_vactive_fill_bw_l = mode_lib->ms.excess_vactive_fill_bw_l;
			calculate_peak_bandwidth_params->excess_vactive_fill_bw_c = mode_lib->ms.excess_vactive_fill_bw_c;
			calculate_peak_bandwidth_params->cursor_bw = mode_lib->ms.cursor_bw;
			calculate_peak_bandwidth_params->dpte_row_bw = mode_lib->ms.dpte_row_bw;
			calculate_peak_bandwidth_params->meta_row_bw = mode_lib->ms.meta_row_bw;
			calculate_peak_bandwidth_params->prefetch_cursor_bw = mode_lib->ms.prefetch_cursor_bw;
			calculate_peak_bandwidth_params->prefetch_vmrow_bw = mode_lib->ms.prefetch_vmrow_bw;
			calculate_peak_bandwidth_params->flip_bw = mode_lib->ms.final_flip_bw;
			calculate_peak_bandwidth_params->urgent_burst_factor_l = mode_lib->ms.UrgentBurstFactorLuma;
			calculate_peak_bandwidth_params->urgent_burst_factor_c = mode_lib->ms.UrgentBurstFactorChroma;
			calculate_peak_bandwidth_params->urgent_burst_factor_cursor = mode_lib->ms.UrgentBurstFactorCursor;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_l = mode_lib->ms.UrgentBurstFactorLumaPre;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_c = mode_lib->ms.UrgentBurstFactorChromaPre;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_cursor = mode_lib->ms.UrgentBurstFactorCursorPre;

			calculate_peak_bandwidth_required(
					&mode_lib->scratch,
					calculate_peak_bandwidth_params);

			calculate_immediate_flip_bandwidth_support(
				&s->dummy_single[0], // double* frac_urg_bandwidth_flip
				&mode_lib->ms.support.ImmediateFlipSupport,

				dml2_core_internal_soc_state_sys_active,
				mode_lib->ms.support.urg_bandwidth_required_flip,
				mode_lib->ms.support.non_urg_bandwidth_required_flip,
				mode_lib->ms.support.urg_bandwidth_available);

			for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
				if (display_cfg->plane_descriptors[k].immediate_flip == true && mode_lib->ms.ImmediateFlipSupportedForPipe[k] == false)
					mode_lib->ms.support.ImmediateFlipSupport = false;
			}

		} else { // if prefetch not support, assume iflip is not supported too
			mode_lib->ms.support.ImmediateFlipSupport = false;
		}

		s->mSOCParameters.UrgentLatency = mode_lib->ms.UrgLatency;
		s->mSOCParameters.ExtraLatency = mode_lib->ms.ExtraLatency;
		s->mSOCParameters.ExtraLatency_sr = mode_lib->ms.ExtraLatency_sr;
		s->mSOCParameters.WritebackLatency = mode_lib->soc.qos_parameters.writeback.base_latency_us;
		s->mSOCParameters.DRAMClockChangeLatency = mode_lib->soc.power_management_parameters.dram_clk_change_blackout_us;
		s->mSOCParameters.FCLKChangeLatency = mode_lib->soc.power_management_parameters.fclk_change_blackout_us;
		s->mSOCParameters.SRExitTime = mode_lib->soc.power_management_parameters.stutter_exit_latency_us;
		s->mSOCParameters.SREnterPlusExitTime = mode_lib->soc.power_management_parameters.stutter_enter_plus_exit_latency_us;
		s->mSOCParameters.SRExitZ8Time = mode_lib->soc.power_management_parameters.z8_stutter_exit_latency_us;
		s->mSOCParameters.SREnterPlusExitZ8Time = mode_lib->soc.power_management_parameters.z8_stutter_enter_plus_exit_latency_us;
		s->mSOCParameters.USRRetrainingLatency = 0;
		s->mSOCParameters.SMNLatency = 0;
		s->mSOCParameters.g6_temp_read_blackout_us = get_g6_temp_read_blackout_us(&mode_lib->soc, (unsigned int)(mode_lib->ms.uclk_freq_mhz * 1000), in_out_params->min_clk_index);
		s->mSOCParameters.max_urgent_latency_us = get_max_urgent_latency_us(&mode_lib->soc.qos_parameters.qos_params.dcn4x, mode_lib->ms.uclk_freq_mhz, mode_lib->ms.FabricClock, in_out_params->min_clk_index);
		s->mSOCParameters.df_response_time_us = mode_lib->soc.qos_parameters.qos_params.dcn4x.df_qos_response_time_fclk_cycles / mode_lib->ms.FabricClock;
		s->mSOCParameters.qos_type = mode_lib->soc.qos_parameters.qos_type;

		CalculateWatermarks_params->display_cfg = display_cfg;
		CalculateWatermarks_params->USRRetrainingRequired = false;
		CalculateWatermarks_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
		CalculateWatermarks_params->MaxLineBufferLines = mode_lib->ip.max_line_buffer_lines;
		CalculateWatermarks_params->LineBufferSize = mode_lib->ip.line_buffer_size_bits;
		CalculateWatermarks_params->WritebackInterfaceBufferSize = mode_lib->ip.writeback_interface_buffer_size_kbytes;
		CalculateWatermarks_params->DCFCLK = mode_lib->ms.DCFCLK;
		CalculateWatermarks_params->SynchronizeTimings = display_cfg->overrides.synchronize_timings;
		CalculateWatermarks_params->SynchronizeDRRDisplaysForUCLKPStateChange = display_cfg->overrides.synchronize_ddr_displays_for_uclk_pstate_change;
		CalculateWatermarks_params->dpte_group_bytes = mode_lib->ms.dpte_group_bytes;
		CalculateWatermarks_params->mmSOCParameters = s->mSOCParameters;
		CalculateWatermarks_params->WritebackChunkSize = mode_lib->ip.writeback_chunk_size_kbytes;
		CalculateWatermarks_params->SOCCLK = mode_lib->ms.SOCCLK;
		CalculateWatermarks_params->DCFClkDeepSleep = mode_lib->ms.dcfclk_deepsleep;
		CalculateWatermarks_params->DETBufferSizeY = mode_lib->ms.DETBufferSizeY;
		CalculateWatermarks_params->DETBufferSizeC = mode_lib->ms.DETBufferSizeC;
		CalculateWatermarks_params->SwathHeightY = mode_lib->ms.SwathHeightY;
		CalculateWatermarks_params->SwathHeightC = mode_lib->ms.SwathHeightC;
		CalculateWatermarks_params->SwathWidthY = mode_lib->ms.SwathWidthY;
		CalculateWatermarks_params->SwathWidthC = mode_lib->ms.SwathWidthC;
		CalculateWatermarks_params->DPPPerSurface = mode_lib->ms.NoOfDPP;
		CalculateWatermarks_params->BytePerPixelDETY = mode_lib->ms.BytePerPixelInDETY;
		CalculateWatermarks_params->BytePerPixelDETC = mode_lib->ms.BytePerPixelInDETC;
		CalculateWatermarks_params->DSTXAfterScaler = s->DSTXAfterScaler;
		CalculateWatermarks_params->DSTYAfterScaler = s->DSTYAfterScaler;
		CalculateWatermarks_params->UnboundedRequestEnabled = mode_lib->ms.UnboundedRequestEnabled;
		CalculateWatermarks_params->CompressedBufferSizeInkByte = mode_lib->ms.CompressedBufferSizeInkByte;
		CalculateWatermarks_params->meta_row_height_l = s->meta_row_height_luma;
		CalculateWatermarks_params->meta_row_height_c = s->meta_row_height_chroma;

		// Output
		CalculateWatermarks_params->Watermark = &mode_lib->ms.support.watermarks; // Watermarks *Watermark
		CalculateWatermarks_params->DRAMClockChangeSupport = mode_lib->ms.support.DRAMClockChangeSupport;
		CalculateWatermarks_params->global_dram_clock_change_supported = &mode_lib->ms.support.global_dram_clock_change_supported;
		CalculateWatermarks_params->MaxActiveDRAMClockChangeLatencySupported = &s->dummy_single_array[0]; // double *MaxActiveDRAMClockChangeLatencySupported[]
		CalculateWatermarks_params->SubViewportLinesNeededInMALL = mode_lib->ms.SubViewportLinesNeededInMALL; // unsigned int SubViewportLinesNeededInMALL[]
		CalculateWatermarks_params->FCLKChangeSupport = mode_lib->ms.support.FCLKChangeSupport;
		CalculateWatermarks_params->global_fclk_change_supported = &mode_lib->ms.support.global_fclk_change_supported;
		CalculateWatermarks_params->MaxActiveFCLKChangeLatencySupported = &s->dummy_single[0]; // double *MaxActiveFCLKChangeLatencySupported
		CalculateWatermarks_params->USRRetrainingSupport = &mode_lib->ms.support.USRRetrainingSupport;
		CalculateWatermarks_params->g6_temp_read_support = &mode_lib->ms.support.g6_temp_read_support;
		CalculateWatermarks_params->VActiveLatencyHidingMargin = mode_lib->ms.VActiveLatencyHidingMargin;
		CalculateWatermarks_params->VActiveLatencyHidingUs = mode_lib->ms.VActiveLatencyHidingUs;

		CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport(&mode_lib->scratch, CalculateWatermarks_params);

		calculate_pstate_keepout_dst_lines(display_cfg, &mode_lib->ms.support.watermarks, s->dummy_integer_array[0]);
	}
	dml2_printf("DML::%s: Done prefetch calculation\n", __func__);
	// End of Prefetch Check

	mode_lib->ms.support.max_urgent_latency_us = s->mSOCParameters.max_urgent_latency_us;

	//Re-ordering Buffer Support Check
	if (mode_lib->soc.qos_parameters.qos_type == dml2_qos_param_type_dcn4x) {
		if (((mode_lib->ip.rob_buffer_size_kbytes - mode_lib->ip.pixel_chunk_size_kbytes) * 1024
			/ mode_lib->ms.support.non_urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]) >= s->mSOCParameters.max_urgent_latency_us) {
			mode_lib->ms.support.ROBSupport = true;
		} else {
			mode_lib->ms.support.ROBSupport = false;
		}
	} else {
		if (mode_lib->ip.rob_buffer_size_kbytes * 1024 >= mode_lib->soc.qos_parameters.qos_params.dcn32x.loaded_round_trip_latency_fclk_cycles * mode_lib->soc.fabric_datapath_to_dcn_data_return_bytes) {
			mode_lib->ms.support.ROBSupport = true;
		} else {
			mode_lib->ms.support.ROBSupport = false;
		}
	}

	/* VActive fill time calculations (informative) */
	calculate_vactive_det_fill_latency(
			display_cfg,
			mode_lib->ms.num_active_planes,
			s->pstate_bytes_required_l,
			s->pstate_bytes_required_c,
			mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0,
			mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1,
			mode_lib->ms.vactive_sw_bw_l,
			mode_lib->ms.vactive_sw_bw_c,
			mode_lib->ms.surface_avg_vactive_required_bw,
			mode_lib->ms.surface_peak_required_bw,
			/* outputs */
			mode_lib->ms.dram_change_vactive_det_fill_delay_us);

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: max_urgent_latency_us = %f\n", __func__, s->mSOCParameters.max_urgent_latency_us);
	dml2_printf("DML::%s: ROBSupport = %u\n", __func__, mode_lib->ms.support.ROBSupport);
#endif

	/*Mode Support, Voltage State and SOC Configuration*/
	{
		if (mode_lib->ms.support.ScaleRatioAndTapsSupport
			&& mode_lib->ms.support.SourceFormatPixelAndScanSupport
			&& mode_lib->ms.support.ViewportSizeSupport
			&& !mode_lib->ms.support.LinkRateDoesNotMatchDPVersion
			&& !mode_lib->ms.support.LinkRateForMultistreamNotIndicated
			&& !mode_lib->ms.support.BPPForMultistreamNotIndicated
			&& !mode_lib->ms.support.MultistreamWithHDMIOreDP
			&& !mode_lib->ms.support.ExceededMultistreamSlots
			&& !mode_lib->ms.support.MSOOrODMSplitWithNonDPLink
			&& !mode_lib->ms.support.NotEnoughLanesForMSO
			&& !mode_lib->ms.support.P2IWith420
			&& !mode_lib->ms.support.DSC422NativeNotSupported
			&& mode_lib->ms.support.DSCSlicesODMModeSupported
			&& !mode_lib->ms.support.NotEnoughDSCUnits
			&& !mode_lib->ms.support.NotEnoughDSCSlices
			&& !mode_lib->ms.support.ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe
			&& !mode_lib->ms.support.InvalidCombinationOfMALLUseForPStateAndStaticScreen
			&& !mode_lib->ms.support.DSCCLKRequiredMoreThanSupported
			&& mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport
			&& !mode_lib->ms.support.DTBCLKRequiredMoreThanSupported
			&& !mode_lib->ms.support.InvalidCombinationOfMALLUseForPState
			&& mode_lib->ms.support.ROBSupport
			&& mode_lib->ms.support.OutstandingRequestsSupport
			&& mode_lib->ms.support.OutstandingRequestsUrgencyAvoidance
			&& mode_lib->ms.support.DISPCLK_DPPCLK_Support
			&& mode_lib->ms.support.TotalAvailablePipesSupport
			&& mode_lib->ms.support.NumberOfOTGSupport
			&& mode_lib->ms.support.NumberOfHDMIFRLSupport
			&& mode_lib->ms.support.NumberOfDP2p0Support
			&& mode_lib->ms.support.EnoughWritebackUnits
			&& mode_lib->ms.support.WritebackLatencySupport
			&& mode_lib->ms.support.WritebackScaleRatioAndTapsSupport
			&& mode_lib->ms.support.CursorSupport
			&& mode_lib->ms.support.PitchSupport
			&& !mode_lib->ms.support.ViewportExceedsSurface
			&& mode_lib->ms.support.PrefetchSupported
			&& mode_lib->ms.support.EnoughUrgentLatencyHidingSupport
			&& mode_lib->ms.support.AvgBandwidthSupport
			&& mode_lib->ms.support.DynamicMetadataSupported
			&& mode_lib->ms.support.VRatioInPrefetchSupported
			&& mode_lib->ms.support.PTEBufferSizeNotExceeded
			&& mode_lib->ms.support.DCCMetaBufferSizeNotExceeded
			&& !mode_lib->ms.support.ExceededMALLSize
			&& mode_lib->ms.support.g6_temp_read_support
			&& ((!display_cfg->hostvm_enable && !s->ImmediateFlipRequired) || mode_lib->ms.support.ImmediateFlipSupport)) {
			dml2_printf("DML::%s: mode is supported\n", __func__);
			mode_lib->ms.support.ModeSupport = true;
		} else {
			dml2_printf("DML::%s: mode is NOT supported\n", __func__);
			mode_lib->ms.support.ModeSupport = false;
		}
	}

	// Since now the mode_support work on 1 particular power state, so there is only 1 state idx (index 0).
	dml2_printf("DML::%s: ModeSupport = %u\n", __func__, mode_lib->ms.support.ModeSupport);
	dml2_printf("DML::%s: ImmediateFlipSupport = %u\n", __func__, mode_lib->ms.support.ImmediateFlipSupport);

	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		mode_lib->ms.support.MPCCombineEnable[k] = mode_lib->ms.MPCCombine[k];
		mode_lib->ms.support.DPPPerSurface[k] = mode_lib->ms.NoOfDPP[k];
	}

	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		mode_lib->ms.support.ODMMode[k] = mode_lib->ms.ODMMode[k];
		mode_lib->ms.support.DSCEnabled[k] = mode_lib->ms.RequiresDSC[k];
		mode_lib->ms.support.FECEnabled[k] = mode_lib->ms.RequiresFEC[k];
		mode_lib->ms.support.OutputBpp[k] = mode_lib->ms.OutputBpp[k];
		mode_lib->ms.support.OutputType[k] = mode_lib->ms.OutputType[k];
		mode_lib->ms.support.OutputRate[k] = mode_lib->ms.OutputRate[k];

#if defined(__DML_VBA_DEBUG__)
		dml2_printf("DML::%s: k=%d, ODMMode = %u\n", __func__, k, mode_lib->ms.support.ODMMode[k]);
		dml2_printf("DML::%s: k=%d, DSCEnabled = %u\n", __func__, k, mode_lib->ms.support.DSCEnabled[k]);
#endif
	}

#if defined(__DML_VBA_DEBUG__)
	if (!mode_lib->ms.support.ModeSupport)
		dml2_print_mode_support_info(&mode_lib->ms.support, true);

	dml2_printf("DML::%s: --- DONE --- \n", __func__);
#endif

	return mode_lib->ms.support.ModeSupport;
}

unsigned int dml2_core_calcs_mode_support_ex(struct dml2_core_calcs_mode_support_ex *in_out_params)
{
	unsigned int result;

	dml2_printf("DML::%s: ------------- START ----------\n", __func__);
	result = dml_core_mode_support(in_out_params);

	if (result)
		*in_out_params->out_evaluation_info = in_out_params->mode_lib->ms.support;

	dml2_printf("DML::%s: is_mode_support = %u (min_clk_index=%d)\n", __func__, result, in_out_params->min_clk_index);

	for (unsigned int k = 0; k < in_out_params->in_display_cfg->num_planes; k++)
		dml2_printf("DML::%s: plane_%d: reserved_vblank_time_ns = %u\n", __func__, k, in_out_params->in_display_cfg->plane_descriptors[k].overrides.reserved_vblank_time_ns);

	dml2_printf("DML::%s: ------------- DONE ----------\n", __func__);

	return result;
}

static void CalculatePixelDeliveryTimes(
	const struct dml2_display_cfg *display_cfg,
	const struct core_display_cfg_support_info *cfg_support_info,
	unsigned int NumberOfActiveSurfaces,
	double VRatioPrefetchY[],
	double VRatioPrefetchC[],
	unsigned int swath_width_luma_ub[],
	unsigned int swath_width_chroma_ub[],
	double PSCL_THROUGHPUT[],
	double PSCL_THROUGHPUT_CHROMA[],
	double Dppclk[],
	unsigned int BytePerPixelC[],
	unsigned int req_per_swath_ub_l[],
	unsigned int req_per_swath_ub_c[],

	// Output
	double DisplayPipeLineDeliveryTimeLuma[],
	double DisplayPipeLineDeliveryTimeChroma[],
	double DisplayPipeLineDeliveryTimeLumaPrefetch[],
	double DisplayPipeLineDeliveryTimeChromaPrefetch[],
	double DisplayPipeRequestDeliveryTimeLuma[],
	double DisplayPipeRequestDeliveryTimeChroma[],
	double DisplayPipeRequestDeliveryTimeLumaPrefetch[],
	double DisplayPipeRequestDeliveryTimeChromaPrefetch[])
{
	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		double pixel_clock_mhz = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u : HRatio = %f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio);
		dml2_printf("DML::%s: k=%u : VRatio = %f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio);
		dml2_printf("DML::%s: k=%u : HRatioChroma = %f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio);
		dml2_printf("DML::%s: k=%u : VRatioChroma = %f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio);
		dml2_printf("DML::%s: k=%u : VRatioPrefetchY = %f\n", __func__, k, VRatioPrefetchY[k]);
		dml2_printf("DML::%s: k=%u : VRatioPrefetchC = %f\n", __func__, k, VRatioPrefetchC[k]);
		dml2_printf("DML::%s: k=%u : swath_width_luma_ub = %u\n", __func__, k, swath_width_luma_ub[k]);
		dml2_printf("DML::%s: k=%u : swath_width_chroma_ub = %u\n", __func__, k, swath_width_chroma_ub[k]);
		dml2_printf("DML::%s: k=%u : PSCL_THROUGHPUT = %f\n", __func__, k, PSCL_THROUGHPUT[k]);
		dml2_printf("DML::%s: k=%u : PSCL_THROUGHPUT_CHROMA = %f\n", __func__, k, PSCL_THROUGHPUT_CHROMA[k]);
		dml2_printf("DML::%s: k=%u : DPPPerSurface = %u\n", __func__, k, cfg_support_info->plane_support_info[k].dpps_used);
		dml2_printf("DML::%s: k=%u : pixel_clock_mhz = %f\n", __func__, k, pixel_clock_mhz);
		dml2_printf("DML::%s: k=%u : Dppclk = %f\n", __func__, k, Dppclk[k]);
#endif
		if (display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio <= 1) {
			DisplayPipeLineDeliveryTimeLuma[k] = swath_width_luma_ub[k] * cfg_support_info->plane_support_info[k].dpps_used / display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio / pixel_clock_mhz;
		} else {
			DisplayPipeLineDeliveryTimeLuma[k] = swath_width_luma_ub[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}

		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma[k] = 0;
		} else {
			if (display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio <= 1) {
				DisplayPipeLineDeliveryTimeChroma[k] = swath_width_chroma_ub[k] * cfg_support_info->plane_support_info[k].dpps_used / display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio / pixel_clock_mhz;
			} else {
				DisplayPipeLineDeliveryTimeChroma[k] = swath_width_chroma_ub[k] / PSCL_THROUGHPUT_CHROMA[k] / Dppclk[k];
			}
		}

		if (VRatioPrefetchY[k] <= 1) {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = swath_width_luma_ub[k] * cfg_support_info->plane_support_info[k].dpps_used / display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio / pixel_clock_mhz;
		} else {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = swath_width_luma_ub[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}

		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			if (VRatioPrefetchC[k] <= 1) {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] = swath_width_chroma_ub[k] * cfg_support_info->plane_support_info[k].dpps_used / display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio / pixel_clock_mhz;
			} else {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] = swath_width_chroma_ub[k] / PSCL_THROUGHPUT_CHROMA[k] / Dppclk[k];
			}
		}
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u : DisplayPipeLineDeliveryTimeLuma = %f\n", __func__, k, DisplayPipeLineDeliveryTimeLuma[k]);
		dml2_printf("DML::%s: k=%u : DisplayPipeLineDeliveryTimeLumaPrefetch = %f\n", __func__, k, DisplayPipeLineDeliveryTimeLumaPrefetch[k]);
		dml2_printf("DML::%s: k=%u : DisplayPipeLineDeliveryTimeChroma = %f\n", __func__, k, DisplayPipeLineDeliveryTimeChroma[k]);
		dml2_printf("DML::%s: k=%u : DisplayPipeLineDeliveryTimeChromaPrefetch = %f\n", __func__, k, DisplayPipeLineDeliveryTimeChromaPrefetch[k]);
#endif
	}

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {

		DisplayPipeRequestDeliveryTimeLuma[k] = DisplayPipeLineDeliveryTimeLuma[k] / req_per_swath_ub_l[k];
		DisplayPipeRequestDeliveryTimeLumaPrefetch[k] = DisplayPipeLineDeliveryTimeLumaPrefetch[k] / req_per_swath_ub_l[k];
		if (BytePerPixelC[k] == 0) {
			DisplayPipeRequestDeliveryTimeChroma[k] = 0;
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			DisplayPipeRequestDeliveryTimeChroma[k] = DisplayPipeLineDeliveryTimeChroma[k] / req_per_swath_ub_c[k];
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] = DisplayPipeLineDeliveryTimeChromaPrefetch[k] / req_per_swath_ub_c[k];
		}
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeLuma = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeLuma[k]);
		dml2_printf("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeLumaPrefetch = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeLumaPrefetch[k]);
		dml2_printf("DML::%s: k=%u : req_per_swath_ub_l = %d\n", __func__, k, req_per_swath_ub_l[k]);
		dml2_printf("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeChroma = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeChroma[k]);
		dml2_printf("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeChromaPrefetch = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeChromaPrefetch[k]);
		dml2_printf("DML::%s: k=%u : req_per_swath_ub_c = %d\n", __func__, k, req_per_swath_ub_c[k]);
#endif
	}
}

static void CalculateMetaAndPTETimes(struct dml2_core_shared_CalculateMetaAndPTETimes_params *p)
{
	unsigned int meta_chunk_width;
	unsigned int min_meta_chunk_width;
	unsigned int meta_chunk_per_row_int;
	unsigned int meta_row_remainder;
	unsigned int meta_chunk_threshold;
	unsigned int meta_chunks_per_row_ub;
	unsigned int meta_chunk_width_chroma;
	unsigned int min_meta_chunk_width_chroma;
	unsigned int meta_chunk_per_row_int_chroma;
	unsigned int meta_row_remainder_chroma;
	unsigned int meta_chunk_threshold_chroma;
	unsigned int meta_chunks_per_row_ub_chroma;
	unsigned int dpte_group_width_luma;
	unsigned int dpte_groups_per_row_luma_ub;
	unsigned int dpte_group_width_chroma;
	unsigned int dpte_groups_per_row_chroma_ub;
	double pixel_clock_mhz;

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		p->DST_Y_PER_PTE_ROW_NOM_L[k] = p->dpte_row_height[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		if (p->BytePerPixelC[k] == 0) {
			p->DST_Y_PER_PTE_ROW_NOM_C[k] = 0;
		} else {
			p->DST_Y_PER_PTE_ROW_NOM_C[k] = p->dpte_row_height_chroma[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		}
		p->DST_Y_PER_META_ROW_NOM_L[k] = p->meta_row_height[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		if (p->BytePerPixelC[k] == 0) {
			p->DST_Y_PER_META_ROW_NOM_C[k] = 0;
		} else {
			p->DST_Y_PER_META_ROW_NOM_C[k] = p->meta_row_height_chroma[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		}
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->display_cfg->plane_descriptors[k].surface.dcc.enable == true && p->mrq_present) {
			meta_chunk_width = p->MetaChunkSize * 1024 * 256 / p->BytePerPixelY[k] / p->meta_row_height[k];
			min_meta_chunk_width = p->MinMetaChunkSizeBytes * 256 / p->BytePerPixelY[k] / p->meta_row_height[k];
			meta_chunk_per_row_int = p->meta_row_width[k] / meta_chunk_width;
			meta_row_remainder = p->meta_row_width[k] % meta_chunk_width;
			if (!dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) {
				meta_chunk_threshold = 2 * min_meta_chunk_width - p->meta_req_width[k];
			} else {
				meta_chunk_threshold = 2 * min_meta_chunk_width - p->meta_req_height[k];
			}
			if (meta_row_remainder <= meta_chunk_threshold) {
				meta_chunks_per_row_ub = meta_chunk_per_row_int + 1;
			} else {
				meta_chunks_per_row_ub = meta_chunk_per_row_int + 2;
			}
			p->TimePerMetaChunkNominal[k] = p->meta_row_height[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio *
				p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total /
				(p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub;
			p->TimePerMetaChunkVBlank[k] = p->dst_y_per_row_vblank[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total /
				(p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub;
			p->TimePerMetaChunkFlip[k] = p->dst_y_per_row_flip[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total /
				(p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub;
			if (p->BytePerPixelC[k] == 0) {
				p->TimePerChromaMetaChunkNominal[k] = 0;
				p->TimePerChromaMetaChunkVBlank[k] = 0;
				p->TimePerChromaMetaChunkFlip[k] = 0;
			} else {
				meta_chunk_width_chroma = p->MetaChunkSize * 1024 * 256 / p->BytePerPixelC[k] / p->meta_row_height_chroma[k];
				min_meta_chunk_width_chroma = p->MinMetaChunkSizeBytes * 256 / p->BytePerPixelC[k] / p->meta_row_height_chroma[k];
				meta_chunk_per_row_int_chroma = (unsigned int)((double)p->meta_row_width_chroma[k] / meta_chunk_width_chroma);
				meta_row_remainder_chroma = p->meta_row_width_chroma[k] % meta_chunk_width_chroma;
				if (!dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) {
					meta_chunk_threshold_chroma = 2 * min_meta_chunk_width_chroma - p->meta_req_width_chroma[k];
				} else {
					meta_chunk_threshold_chroma = 2 * min_meta_chunk_width_chroma - p->meta_req_height_chroma[k];
				}
				if (meta_row_remainder_chroma <= meta_chunk_threshold_chroma) {
					meta_chunks_per_row_ub_chroma = meta_chunk_per_row_int_chroma + 1;
				} else {
					meta_chunks_per_row_ub_chroma = meta_chunk_per_row_int_chroma + 2;
				}
				p->TimePerChromaMetaChunkNominal[k] = p->meta_row_height_chroma[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / (p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub_chroma;
				p->TimePerChromaMetaChunkVBlank[k] = p->dst_y_per_row_vblank[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / (p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub_chroma;
				p->TimePerChromaMetaChunkFlip[k] = p->dst_y_per_row_flip[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / (p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) / meta_chunks_per_row_ub_chroma;
			}
		} else {
			p->TimePerMetaChunkNominal[k] = 0;
			p->TimePerMetaChunkVBlank[k] = 0;
			p->TimePerMetaChunkFlip[k] = 0;
			p->TimePerChromaMetaChunkNominal[k] = 0;
			p->TimePerChromaMetaChunkVBlank[k] = 0;
			p->TimePerChromaMetaChunkFlip[k] = 0;
		}

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%d, DST_Y_PER_META_ROW_NOM_L = %f\n", __func__, k, p->DST_Y_PER_META_ROW_NOM_L[k]);
		dml2_printf("DML::%s: k=%d, DST_Y_PER_META_ROW_NOM_C = %f\n", __func__, k, p->DST_Y_PER_META_ROW_NOM_C[k]);
		dml2_printf("DML::%s: k=%d, TimePerMetaChunkNominal		  = %f\n", __func__, k, p->TimePerMetaChunkNominal[k]);
		dml2_printf("DML::%s: k=%d, TimePerMetaChunkVBlank		   = %f\n", __func__, k, p->TimePerMetaChunkVBlank[k]);
		dml2_printf("DML::%s: k=%d, TimePerMetaChunkFlip			 = %f\n", __func__, k, p->TimePerMetaChunkFlip[k]);
		dml2_printf("DML::%s: k=%d, TimePerChromaMetaChunkNominal	= %f\n", __func__, k, p->TimePerChromaMetaChunkNominal[k]);
		dml2_printf("DML::%s: k=%d, TimePerChromaMetaChunkVBlank	 = %f\n", __func__, k, p->TimePerChromaMetaChunkVBlank[k]);
		dml2_printf("DML::%s: k=%d, TimePerChromaMetaChunkFlip	   = %f\n", __func__, k, p->TimePerChromaMetaChunkFlip[k]);
#endif
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		p->DST_Y_PER_PTE_ROW_NOM_L[k] = p->dpte_row_height[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		if (p->BytePerPixelC[k] == 0) {
			p->DST_Y_PER_PTE_ROW_NOM_C[k] = 0;
		} else {
			p->DST_Y_PER_PTE_ROW_NOM_C[k] = p->dpte_row_height_chroma[k] / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		}
	}

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		pixel_clock_mhz = ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);

		if (p->display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut)
			p->time_per_tdlut_group[k] = 2 * p->dst_y_per_row_vblank[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / p->tdlut_groups_per_2row_ub[k];
		else
			p->time_per_tdlut_group[k] = 0;

		dml2_printf("DML::%s: k=%u, time_per_tdlut_group = %f\n", __func__, k, p->time_per_tdlut_group[k]);

		if (p->display_cfg->gpuvm_enable == true) {
			if (!dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) {
				dpte_group_width_luma = (unsigned int)((double)p->dpte_group_bytes[k] / (double)p->PTERequestSizeY[k] * p->PixelPTEReqWidthY[k]);
			} else {
				dpte_group_width_luma = (unsigned int)((double)p->dpte_group_bytes[k] / (double)p->PTERequestSizeY[k] * p->PixelPTEReqHeightY[k]);
			}
			if (p->use_one_row_for_frame[k]) {
				dpte_groups_per_row_luma_ub = (unsigned int)(math_ceil2((double)p->dpte_row_width_luma_ub[k] / (double)dpte_group_width_luma / 2.0, 1.0));
			} else {
				dpte_groups_per_row_luma_ub = (unsigned int)(math_ceil2((double)p->dpte_row_width_luma_ub[k] / (double)dpte_group_width_luma, 1.0));
			}
			if (dpte_groups_per_row_luma_ub <= 2) {
				dpte_groups_per_row_luma_ub = dpte_groups_per_row_luma_ub + 1;
			}
			dml2_printf("DML::%s: k=%u, use_one_row_for_frame = %u\n", __func__, k, p->use_one_row_for_frame[k]);
			dml2_printf("DML::%s: k=%u, dpte_group_bytes = %u\n", __func__, k, p->dpte_group_bytes[k]);
			dml2_printf("DML::%s: k=%u, PTERequestSizeY = %u\n", __func__, k, p->PTERequestSizeY[k]);
			dml2_printf("DML::%s: k=%u, PixelPTEReqWidthY = %u\n", __func__, k, p->PixelPTEReqWidthY[k]);
			dml2_printf("DML::%s: k=%u, PixelPTEReqHeightY = %u\n", __func__, k, p->PixelPTEReqHeightY[k]);
			dml2_printf("DML::%s: k=%u, dpte_row_width_luma_ub = %u\n", __func__, k, p->dpte_row_width_luma_ub[k]);
			dml2_printf("DML::%s: k=%u, dpte_group_width_luma = %u\n", __func__, k, dpte_group_width_luma);
			dml2_printf("DML::%s: k=%u, dpte_groups_per_row_luma_ub = %u\n", __func__, k, dpte_groups_per_row_luma_ub);

			p->time_per_pte_group_nom_luma[k] = p->DST_Y_PER_PTE_ROW_NOM_L[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_luma_ub;
			p->time_per_pte_group_vblank_luma[k] = p->dst_y_per_row_vblank[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_luma_ub;
			p->time_per_pte_group_flip_luma[k] = p->dst_y_per_row_flip[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_luma_ub;
			if (p->BytePerPixelC[k] == 0) {
				p->time_per_pte_group_nom_chroma[k] = 0;
				p->time_per_pte_group_vblank_chroma[k] = 0;
				p->time_per_pte_group_flip_chroma[k] = 0;
			} else {
				if (!dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle)) {
					dpte_group_width_chroma = (unsigned int)((double)p->dpte_group_bytes[k] / (double)p->PTERequestSizeC[k] * p->PixelPTEReqWidthC[k]);
				} else {
					dpte_group_width_chroma = (unsigned int)((double)p->dpte_group_bytes[k] / (double)p->PTERequestSizeC[k] * p->PixelPTEReqHeightC[k]);
				}

				if (p->use_one_row_for_frame[k]) {
					dpte_groups_per_row_chroma_ub = (unsigned int)(math_ceil2((double)p->dpte_row_width_chroma_ub[k] / (double)dpte_group_width_chroma / 2.0, 1.0));
				} else {
					dpte_groups_per_row_chroma_ub = (unsigned int)(math_ceil2((double)p->dpte_row_width_chroma_ub[k] / (double)dpte_group_width_chroma, 1.0));
				}
				if (dpte_groups_per_row_chroma_ub <= 2) {
					dpte_groups_per_row_chroma_ub = dpte_groups_per_row_chroma_ub + 1;
				}
				dml2_printf("DML::%s: k=%u, dpte_row_width_chroma_ub = %u\n", __func__, k, p->dpte_row_width_chroma_ub[k]);
				dml2_printf("DML::%s: k=%u, dpte_group_width_chroma = %u\n", __func__, k, dpte_group_width_chroma);
				dml2_printf("DML::%s: k=%u, dpte_groups_per_row_chroma_ub = %u\n", __func__, k, dpte_groups_per_row_chroma_ub);

				p->time_per_pte_group_nom_chroma[k] = p->DST_Y_PER_PTE_ROW_NOM_C[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_chroma_ub;
				p->time_per_pte_group_vblank_chroma[k] = p->dst_y_per_row_vblank[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_chroma_ub;
				p->time_per_pte_group_flip_chroma[k] = p->dst_y_per_row_flip[k] * p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz / dpte_groups_per_row_chroma_ub;
			}
		} else {
			p->time_per_pte_group_nom_luma[k] = 0;
			p->time_per_pte_group_vblank_luma[k] = 0;
			p->time_per_pte_group_flip_luma[k] = 0;
			p->time_per_pte_group_nom_chroma[k] = 0;
			p->time_per_pte_group_vblank_chroma[k] = 0;
			p->time_per_pte_group_flip_chroma[k] = 0;
		}
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, dst_y_per_row_vblank = %f\n", __func__, k, p->dst_y_per_row_vblank[k]);
		dml2_printf("DML::%s: k=%u, dst_y_per_row_flip = %f\n", __func__, k, p->dst_y_per_row_flip[k]);

		dml2_printf("DML::%s: k=%u, DST_Y_PER_PTE_ROW_NOM_L = %f\n", __func__, k, p->DST_Y_PER_PTE_ROW_NOM_L[k]);
		dml2_printf("DML::%s: k=%u, DST_Y_PER_PTE_ROW_NOM_C = %f\n", __func__, k, p->DST_Y_PER_PTE_ROW_NOM_C[k]);
		dml2_printf("DML::%s: k=%u, time_per_pte_group_nom_luma = %f\n", __func__, k, p->time_per_pte_group_nom_luma[k]);
		dml2_printf("DML::%s: k=%u, time_per_pte_group_vblank_luma = %f\n", __func__, k, p->time_per_pte_group_vblank_luma[k]);
		dml2_printf("DML::%s: k=%u, time_per_pte_group_flip_luma = %f\n", __func__, k, p->time_per_pte_group_flip_luma[k]);
		dml2_printf("DML::%s: k=%u, time_per_pte_group_nom_chroma = %f\n", __func__, k, p->time_per_pte_group_nom_chroma[k]);
		dml2_printf("DML::%s: k=%u, time_per_pte_group_vblank_chroma = %f\n", __func__, k, p->time_per_pte_group_vblank_chroma[k]);
		dml2_printf("DML::%s: k=%u, time_per_pte_group_flip_chroma = %f\n", __func__, k, p->time_per_pte_group_flip_chroma[k]);
#endif
	}
} // CalculateMetaAndPTETimes

static void CalculateVMGroupAndRequestTimes(
	const struct dml2_display_cfg *display_cfg,
	unsigned int NumberOfActiveSurfaces,
	unsigned int BytePerPixelC[],
	double dst_y_per_vm_vblank[],
	double dst_y_per_vm_flip[],
	unsigned int dpte_row_width_luma_ub[],
	unsigned int dpte_row_width_chroma_ub[],
	unsigned int vm_group_bytes[],
	unsigned int dpde0_bytes_per_frame_ub_l[],
	unsigned int dpde0_bytes_per_frame_ub_c[],
	unsigned int tdlut_pte_bytes_per_frame[],
	unsigned int meta_pte_bytes_per_frame_ub_l[],
	unsigned int meta_pte_bytes_per_frame_ub_c[],
	bool mrq_present,

	// Output
	double TimePerVMGroupVBlank[],
	double TimePerVMGroupFlip[],
	double TimePerVMRequestVBlank[],
	double TimePerVMRequestFlip[])
{
	unsigned int num_group_per_lower_vm_stage = 0;
	unsigned int num_req_per_lower_vm_stage = 0;
	unsigned int num_group_per_lower_vm_stage_flip;
	unsigned int num_group_per_lower_vm_stage_pref;
	unsigned int num_req_per_lower_vm_stage_flip;
	unsigned int num_req_per_lower_vm_stage_pref;
	double line_time;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: NumberOfActiveSurfaces = %u\n", __func__, NumberOfActiveSurfaces);
#endif
	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		double pixel_clock_mhz = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
		bool dcc_mrq_enable = display_cfg->plane_descriptors[k].surface.dcc.enable && mrq_present;
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, dcc_mrq_enable = %u\n", __func__, k, dcc_mrq_enable);
		dml2_printf("DML::%s: k=%u, vm_group_bytes = %u\n", __func__, k, vm_group_bytes[k]);
		dml2_printf("DML::%s: k=%u, dpde0_bytes_per_frame_ub_l = %u\n", __func__, k, dpde0_bytes_per_frame_ub_l[k]);
		dml2_printf("DML::%s: k=%u, dpde0_bytes_per_frame_ub_c = %u\n", __func__, k, dpde0_bytes_per_frame_ub_c[k]);
		dml2_printf("DML::%s: k=%d, meta_pte_bytes_per_frame_ub_l = %d\n", __func__, k, meta_pte_bytes_per_frame_ub_l[k]);
		dml2_printf("DML::%s: k=%d, meta_pte_bytes_per_frame_ub_c = %d\n", __func__, k, meta_pte_bytes_per_frame_ub_c[k]);
#endif

		if (display_cfg->gpuvm_enable) {
			if (display_cfg->gpuvm_max_page_table_levels >= 2) {
				num_group_per_lower_vm_stage += (unsigned int) math_ceil2((double) (dpde0_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1);

				if (BytePerPixelC[k] > 0)
					num_group_per_lower_vm_stage += (unsigned int) math_ceil2((double) (dpde0_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1);
			}

			if (dcc_mrq_enable) {
				if (BytePerPixelC[k] > 0) {
					num_group_per_lower_vm_stage += (unsigned int)(2.0 /*for each mpde0 group*/ + math_ceil2((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1) +
																math_ceil2((double) (meta_pte_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1));
				} else {
					num_group_per_lower_vm_stage += (unsigned int)(1.0 + math_ceil2((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1));
				}
			}

			num_group_per_lower_vm_stage_flip = num_group_per_lower_vm_stage;
			num_group_per_lower_vm_stage_pref = num_group_per_lower_vm_stage;

			if (display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut && display_cfg->gpuvm_enable) {
				num_group_per_lower_vm_stage_pref += (unsigned int) math_ceil2(tdlut_pte_bytes_per_frame[k] / vm_group_bytes[k], 1);
				if (display_cfg->gpuvm_max_page_table_levels >= 2)
					num_group_per_lower_vm_stage_pref += 1; // tdpe0 group
			}

			if (display_cfg->gpuvm_max_page_table_levels >= 2) {
				num_req_per_lower_vm_stage += dpde0_bytes_per_frame_ub_l[k] / 64;
				if (BytePerPixelC[k] > 0)
					num_req_per_lower_vm_stage += dpde0_bytes_per_frame_ub_c[k];
			}

			if (dcc_mrq_enable) {
				num_req_per_lower_vm_stage += meta_pte_bytes_per_frame_ub_l[k] / 64;
				if (BytePerPixelC[k] > 0)
					num_req_per_lower_vm_stage += meta_pte_bytes_per_frame_ub_c[k] / 64;
			}

			num_req_per_lower_vm_stage_flip = num_req_per_lower_vm_stage;
			num_req_per_lower_vm_stage_pref = num_req_per_lower_vm_stage;

			if (display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut && display_cfg->gpuvm_enable) {
				num_req_per_lower_vm_stage_pref += tdlut_pte_bytes_per_frame[k] / 64;
			}

			line_time = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / pixel_clock_mhz;

			if (num_group_per_lower_vm_stage_pref > 0)
				TimePerVMGroupVBlank[k] = dst_y_per_vm_vblank[k] * line_time / num_group_per_lower_vm_stage_pref;
			else
				TimePerVMGroupVBlank[k] = 0;

			if (num_group_per_lower_vm_stage_flip > 0)
				TimePerVMGroupFlip[k] = dst_y_per_vm_flip[k] * line_time / num_group_per_lower_vm_stage_flip;
			else
				TimePerVMGroupFlip[k] = 0;

			if (num_req_per_lower_vm_stage_pref > 0)
				TimePerVMRequestVBlank[k] = dst_y_per_vm_vblank[k] * line_time / num_req_per_lower_vm_stage_pref;
			else
				TimePerVMRequestVBlank[k] = 0.0;
			if (num_req_per_lower_vm_stage_flip > 0)
				TimePerVMRequestFlip[k] = dst_y_per_vm_flip[k] * line_time / num_req_per_lower_vm_stage_flip;
			else
				TimePerVMRequestFlip[k] = 0.0;

			dml2_printf("DML::%s: k=%u, dst_y_per_vm_vblank = %f\n", __func__, k, dst_y_per_vm_vblank[k]);
			dml2_printf("DML::%s: k=%u, dst_y_per_vm_flip = %f\n", __func__, k, dst_y_per_vm_flip[k]);
			dml2_printf("DML::%s: k=%u, line_time = %f\n", __func__, k, line_time);
			dml2_printf("DML::%s: k=%u, num_group_per_lower_vm_stage_pref = %f\n", __func__, k, num_group_per_lower_vm_stage_pref);
			dml2_printf("DML::%s: k=%u, num_group_per_lower_vm_stage_flip = %f\n", __func__, k, num_group_per_lower_vm_stage_flip);
			dml2_printf("DML::%s: k=%u, num_req_per_lower_vm_stage_pref = %f\n", __func__, k, num_req_per_lower_vm_stage_pref);
			dml2_printf("DML::%s: k=%u, num_req_per_lower_vm_stage_flip = %f\n", __func__, k, num_req_per_lower_vm_stage_flip);

			if (display_cfg->gpuvm_max_page_table_levels > 2) {
				TimePerVMGroupVBlank[k] = TimePerVMGroupVBlank[k] / 2;
				TimePerVMGroupFlip[k] = TimePerVMGroupFlip[k] / 2;
				TimePerVMRequestVBlank[k] = TimePerVMRequestVBlank[k] / 2;
				TimePerVMRequestFlip[k] = TimePerVMRequestFlip[k] / 2;
			}

		} else {
			TimePerVMGroupVBlank[k] = 0;
			TimePerVMGroupFlip[k] = 0;
			TimePerVMRequestVBlank[k] = 0;
			TimePerVMRequestFlip[k] = 0;
		}

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u, TimePerVMGroupVBlank = %f\n", __func__, k, TimePerVMGroupVBlank[k]);
		dml2_printf("DML::%s: k=%u, TimePerVMGroupFlip = %f\n", __func__, k, TimePerVMGroupFlip[k]);
		dml2_printf("DML::%s: k=%u, TimePerVMRequestVBlank = %f\n", __func__, k, TimePerVMRequestVBlank[k]);
		dml2_printf("DML::%s: k=%u, TimePerVMRequestFlip = %f\n", __func__, k, TimePerVMRequestFlip[k]);
#endif
	}
}

static void CalculateStutterEfficiency(struct dml2_core_internal_scratch *scratch,
	struct dml2_core_calcs_CalculateStutterEfficiency_params *p)
{
	struct dml2_core_calcs_CalculateStutterEfficiency_locals *l = &scratch->CalculateStutterEfficiency_locals;

	unsigned int TotalNumberOfActiveOTG = 0;
	double SinglePixelClock = 0;
	unsigned int SingleHTotal = 0;
	unsigned int SingleVTotal = 0;
	bool SameTiming = true;
	bool FoundCriticalSurface = false;
	double LastZ8StutterPeriod = 0;

	memset(l, 0, sizeof(struct dml2_core_calcs_CalculateStutterEfficiency_locals));

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (!dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[k])) {
			if (p->display_cfg->plane_descriptors[k].surface.dcc.enable == true) {
				if ((dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) && p->BlockWidth256BytesY[k] > p->SwathHeightY[k]) || (!dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) && p->BlockHeight256BytesY[k] > p->SwathHeightY[k]) || p->DCCYMaxUncompressedBlock[k] < 256) {
					l->MaximumEffectiveCompressionLuma = 2;
				} else {
					l->MaximumEffectiveCompressionLuma = 4;
				}
				l->TotalCompressedReadBandwidth = l->TotalCompressedReadBandwidth + p->ReadBandwidthSurfaceLuma[k] / math_min2(p->display_cfg->plane_descriptors[k].surface.dcc.informative.dcc_rate_plane0, l->MaximumEffectiveCompressionLuma);
#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: k=%u, ReadBandwidthSurfaceLuma = %f\n", __func__, k, p->ReadBandwidthSurfaceLuma[k]);
				dml2_printf("DML::%s: k=%u, NetDCCRateLuma = %f\n", __func__, k, p->display_cfg->plane_descriptors[k].surface.dcc.informative.dcc_rate_plane0);
				dml2_printf("DML::%s: k=%u, MaximumEffectiveCompressionLuma = %f\n", __func__, k, l->MaximumEffectiveCompressionLuma);
#endif
				l->TotalZeroSizeRequestReadBandwidth = l->TotalZeroSizeRequestReadBandwidth + p->ReadBandwidthSurfaceLuma[k] * p->display_cfg->plane_descriptors[k].surface.dcc.informative.fraction_of_zero_size_request_plane0;
				l->TotalZeroSizeCompressedReadBandwidth = l->TotalZeroSizeCompressedReadBandwidth + p->ReadBandwidthSurfaceLuma[k] * p->display_cfg->plane_descriptors[k].surface.dcc.informative.fraction_of_zero_size_request_plane0 / l->MaximumEffectiveCompressionLuma;

				if (p->ReadBandwidthSurfaceChroma[k] > 0) {
					if ((dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) && p->BlockWidth256BytesC[k] > p->SwathHeightC[k]) || (!dml_is_vertical_rotation(p->display_cfg->plane_descriptors[k].composition.rotation_angle) && p->BlockHeight256BytesC[k] > p->SwathHeightC[k]) || p->DCCCMaxUncompressedBlock[k] < 256) {
						l->MaximumEffectiveCompressionChroma = 2;
					} else {
						l->MaximumEffectiveCompressionChroma = 4;
					}
					l->TotalCompressedReadBandwidth = l->TotalCompressedReadBandwidth + p->ReadBandwidthSurfaceChroma[k] / math_min2(p->display_cfg->plane_descriptors[k].surface.dcc.informative.dcc_rate_plane1, l->MaximumEffectiveCompressionChroma);
#ifdef __DML_VBA_DEBUG__
					dml2_printf("DML::%s: k=%u, ReadBandwidthSurfaceChroma = %f\n", __func__, k, p->ReadBandwidthSurfaceChroma[k]);
					dml2_printf("DML::%s: k=%u, NetDCCRateChroma = %f\n", __func__, k, p->display_cfg->plane_descriptors[k].surface.dcc.informative.dcc_rate_plane1);
					dml2_printf("DML::%s: k=%u, MaximumEffectiveCompressionChroma = %f\n", __func__, k, l->MaximumEffectiveCompressionChroma);
#endif
					l->TotalZeroSizeRequestReadBandwidth = l->TotalZeroSizeRequestReadBandwidth + p->ReadBandwidthSurfaceChroma[k] * p->display_cfg->plane_descriptors[k].surface.dcc.informative.fraction_of_zero_size_request_plane1;
					l->TotalZeroSizeCompressedReadBandwidth = l->TotalZeroSizeCompressedReadBandwidth + p->ReadBandwidthSurfaceChroma[k] * p->display_cfg->plane_descriptors[k].surface.dcc.informative.fraction_of_zero_size_request_plane1 / l->MaximumEffectiveCompressionChroma;
				}
			} else {
				l->TotalCompressedReadBandwidth = l->TotalCompressedReadBandwidth + p->ReadBandwidthSurfaceLuma[k] + p->ReadBandwidthSurfaceChroma[k];
			}
			l->TotalRowReadBandwidth = l->TotalRowReadBandwidth + p->DPPPerSurface[k] * (p->meta_row_bw[k] + p->dpte_row_bw[k]);
		}
	}

	l->AverageDCCCompressionRate = p->TotalDataReadBandwidth / l->TotalCompressedReadBandwidth;
	l->AverageDCCZeroSizeFraction = l->TotalZeroSizeRequestReadBandwidth / p->TotalDataReadBandwidth;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: UnboundedRequestEnabled = %u\n", __func__, p->UnboundedRequestEnabled);
	dml2_printf("DML::%s: TotalCompressedReadBandwidth = %f\n", __func__, l->TotalCompressedReadBandwidth);
	dml2_printf("DML::%s: TotalZeroSizeRequestReadBandwidth = %f\n", __func__, l->TotalZeroSizeRequestReadBandwidth);
	dml2_printf("DML::%s: TotalZeroSizeCompressedReadBandwidth = %f\n", __func__, l->TotalZeroSizeCompressedReadBandwidth);
	dml2_printf("DML::%s: MaximumEffectiveCompressionLuma = %f\n", __func__, l->MaximumEffectiveCompressionLuma);
	dml2_printf("DML::%s: MaximumEffectiveCompressionChroma = %f\n", __func__, l->MaximumEffectiveCompressionChroma);
	dml2_printf("DML::%s: AverageDCCCompressionRate = %f\n", __func__, l->AverageDCCCompressionRate);
	dml2_printf("DML::%s: AverageDCCZeroSizeFraction = %f\n", __func__, l->AverageDCCZeroSizeFraction);

	dml2_printf("DML::%s: CompbufReservedSpace64B = %u (%f kbytes)\n", __func__, p->CompbufReservedSpace64B, p->CompbufReservedSpace64B * 64 / 1024.0);
	dml2_printf("DML::%s: CompbufReservedSpaceZs = %u\n", __func__, p->CompbufReservedSpaceZs);
	dml2_printf("DML::%s: CompressedBufferSizeInkByte = %u kbytes\n", __func__, p->CompressedBufferSizeInkByte);
	dml2_printf("DML::%s: ROBBufferSizeInKByte = %u kbytes\n", __func__, p->ROBBufferSizeInKByte);
#endif
	if (l->AverageDCCZeroSizeFraction == 1) {
		l->AverageZeroSizeCompressionRate = l->TotalZeroSizeRequestReadBandwidth / l->TotalZeroSizeCompressedReadBandwidth;
		l->EffectiveCompressedBufferSize = (double)p->MetaFIFOSizeInKEntries * 1024 * 64 * l->AverageZeroSizeCompressionRate + ((double)p->ZeroSizeBufferEntries - p->CompbufReservedSpaceZs) * 64 * l->AverageZeroSizeCompressionRate;


	} else if (l->AverageDCCZeroSizeFraction > 0) {
		l->AverageZeroSizeCompressionRate = l->TotalZeroSizeRequestReadBandwidth / l->TotalZeroSizeCompressedReadBandwidth;
		l->EffectiveCompressedBufferSize = math_min2((double)p->CompressedBufferSizeInkByte * 1024 * l->AverageDCCCompressionRate,
			(double)p->MetaFIFOSizeInKEntries * 1024 * 64 / (l->AverageDCCZeroSizeFraction / l->AverageZeroSizeCompressionRate + 1 / l->AverageDCCCompressionRate)) +
			(p->rob_alloc_compressed ? math_min2(((double)p->ROBBufferSizeInKByte * 1024 - p->CompbufReservedSpace64B * 64) * l->AverageDCCCompressionRate,
				((double)p->ZeroSizeBufferEntries - p->CompbufReservedSpaceZs) * 64 / (l->AverageDCCZeroSizeFraction / l->AverageZeroSizeCompressionRate))
				: ((double)p->ROBBufferSizeInKByte * 1024 - p->CompbufReservedSpace64B * 64));


#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: min 1 = %f\n", __func__, p->CompressedBufferSizeInkByte * 1024 * l->AverageDCCCompressionRate);
		dml2_printf("DML::%s: min 2 = %f\n", __func__, p->MetaFIFOSizeInKEntries * 1024 * 64 / (l->AverageDCCZeroSizeFraction / l->AverageZeroSizeCompressionRate + 1 / l->AverageDCCCompressionRate));
		dml2_printf("DML::%s: min 3 = %d\n", __func__, (p->ROBBufferSizeInKByte * 1024 - p->CompbufReservedSpace64B * 64));
		dml2_printf("DML::%s: min 4 = %f\n", __func__, (p->ZeroSizeBufferEntries - p->CompbufReservedSpaceZs) * 64 / (l->AverageDCCZeroSizeFraction / l->AverageZeroSizeCompressionRate));
#endif
	} else {
		l->EffectiveCompressedBufferSize = math_min2((double)p->CompressedBufferSizeInkByte * 1024 * l->AverageDCCCompressionRate,
			(double)p->MetaFIFOSizeInKEntries * 1024 * 64 * l->AverageDCCCompressionRate) +
			((double)p->ROBBufferSizeInKByte * 1024 - p->CompbufReservedSpace64B * 64) * (p->rob_alloc_compressed ? l->AverageDCCCompressionRate : 1.0);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: min 1 = %f\n", __func__, p->CompressedBufferSizeInkByte * 1024 * l->AverageDCCCompressionRate);
		dml2_printf("DML::%s: min 2 = %f\n", __func__, p->MetaFIFOSizeInKEntries * 1024 * 64 * l->AverageDCCCompressionRate);
#endif
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: MetaFIFOSizeInKEntries = %u\n", __func__, p->MetaFIFOSizeInKEntries);
	dml2_printf("DML::%s: ZeroSizeBufferEntries = %u\n", __func__, p->ZeroSizeBufferEntries);
	dml2_printf("DML::%s: AverageZeroSizeCompressionRate = %f\n", __func__, l->AverageZeroSizeCompressionRate);
	dml2_printf("DML::%s: EffectiveCompressedBufferSize = %f (%f kbytes)\n", __func__, l->EffectiveCompressedBufferSize, l->EffectiveCompressedBufferSize / 1024.0);
#endif

	*p->StutterPeriod = 0;

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (!dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[k])) {
			l->LinesInDETY = ((double)p->DETBufferSizeY[k] + (p->UnboundedRequestEnabled == true ? l->EffectiveCompressedBufferSize : 0) * p->ReadBandwidthSurfaceLuma[k] / p->TotalDataReadBandwidth) / p->BytePerPixelDETY[k] / p->SwathWidthY[k];
			l->LinesInDETYRoundedDownToSwath = math_floor2(l->LinesInDETY, p->SwathHeightY[k]);
			l->DETBufferingTimeY = l->LinesInDETYRoundedDownToSwath * ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) / p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u, DETBufferSizeY = %u (%u kbytes)\n", __func__, k, p->DETBufferSizeY[k], p->DETBufferSizeY[k] / 1024);
			dml2_printf("DML::%s: k=%u, BytePerPixelDETY = %f\n", __func__, k, p->BytePerPixelDETY[k]);
			dml2_printf("DML::%s: k=%u, SwathWidthY = %u\n", __func__, k, p->SwathWidthY[k]);
			dml2_printf("DML::%s: k=%u, ReadBandwidthSurfaceLuma = %f\n", __func__, k, p->ReadBandwidthSurfaceLuma[k]);
			dml2_printf("DML::%s: k=%u, TotalDataReadBandwidth = %f\n", __func__, k, p->TotalDataReadBandwidth);
			dml2_printf("DML::%s: k=%u, LinesInDETY = %f\n", __func__, k, l->LinesInDETY);
			dml2_printf("DML::%s: k=%u, LinesInDETYRoundedDownToSwath = %f\n", __func__, k, l->LinesInDETYRoundedDownToSwath);
			dml2_printf("DML::%s: k=%u, VRatio = %f\n", __func__, k, p->display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio);
			dml2_printf("DML::%s: k=%u, DETBufferingTimeY = %f\n", __func__, k, l->DETBufferingTimeY);
#endif

			if (!FoundCriticalSurface || l->DETBufferingTimeY < *p->StutterPeriod) {
				bool isInterlaceTiming = p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.interlaced && !p->ProgressiveToInterlaceUnitInOPP;

				FoundCriticalSurface = true;
				*p->StutterPeriod = l->DETBufferingTimeY;
				l->FrameTimeCriticalSurface = (isInterlaceTiming ? math_floor2((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_total / 2.0, 1.0) : p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_total) * (double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
				l->VActiveTimeCriticalSurface = (isInterlaceTiming ? math_floor2((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_active / 2.0, 1.0) : p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_active) * (double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
				l->BytePerPixelYCriticalSurface = p->BytePerPixelY[k];
				l->SwathWidthYCriticalSurface = p->SwathWidthY[k];
				l->SwathHeightYCriticalSurface = p->SwathHeightY[k];
				l->BlockWidth256BytesYCriticalSurface = p->BlockWidth256BytesY[k];
				l->DETBufferSizeYCriticalSurface = p->DETBufferSizeY[k];
				l->MinTTUVBlankCriticalSurface = p->MinTTUVBlank[k];
				l->SinglePlaneCriticalSurface = (p->ReadBandwidthSurfaceChroma[k] == 0);
				l->SinglePipeCriticalSurface = (p->DPPPerSurface[k] == 1);

#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: k=%u, FoundCriticalSurface = %u\n", __func__, k, FoundCriticalSurface);
				dml2_printf("DML::%s: k=%u, StutterPeriod = %f\n", __func__, k, *p->StutterPeriod);
				dml2_printf("DML::%s: k=%u, MinTTUVBlankCriticalSurface = %f\n", __func__, k, l->MinTTUVBlankCriticalSurface);
				dml2_printf("DML::%s: k=%u, FrameTimeCriticalSurface= %f\n", __func__, k, l->FrameTimeCriticalSurface);
				dml2_printf("DML::%s: k=%u, VActiveTimeCriticalSurface = %f\n", __func__, k, l->VActiveTimeCriticalSurface);
				dml2_printf("DML::%s: k=%u, BytePerPixelYCriticalSurface = %u\n", __func__, k, l->BytePerPixelYCriticalSurface);
				dml2_printf("DML::%s: k=%u, SwathWidthYCriticalSurface = %f\n", __func__, k, l->SwathWidthYCriticalSurface);
				dml2_printf("DML::%s: k=%u, SwathHeightYCriticalSurface = %f\n", __func__, k, l->SwathHeightYCriticalSurface);
				dml2_printf("DML::%s: k=%u, BlockWidth256BytesYCriticalSurface = %u\n", __func__, k, l->BlockWidth256BytesYCriticalSurface);
				dml2_printf("DML::%s: k=%u, SinglePlaneCriticalSurface = %u\n", __func__, k, l->SinglePlaneCriticalSurface);
				dml2_printf("DML::%s: k=%u, SinglePipeCriticalSurface = %u\n", __func__, k, l->SinglePipeCriticalSurface);
#endif
			}
		}
	}

	// for bounded req, the stutter period is calculated only based on DET size, but during burst there can be some return inside ROB/compressed buffer
	// stutter period is calculated only on the det sizing
	// if (cdb + rob >= det) the stutter burst will be absorbed by the cdb + rob which is before decompress
	// else
	// the cdb + rob part will be in compressed rate with urg bw (idea bw)
	// the det part will be return at uncompressed rate with 64B/dcfclk
	//
	// for unbounded req, the stutter period should be calculated as total of CDB+ROB+DET, so the term "PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer"
	// should be == EffectiveCompressedBufferSize which will returned a compressed rate, the rest of stutter period is from the DET will be returned at uncompressed rate with 64B/dcfclk

	l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer = math_min2(*p->StutterPeriod * p->TotalDataReadBandwidth, l->EffectiveCompressedBufferSize);
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: AverageDCCCompressionRate = %f\n", __func__, l->AverageDCCCompressionRate);
	dml2_printf("DML::%s: StutterPeriod*TotalDataReadBandwidth = %f (%f kbytes)\n", __func__, *p->StutterPeriod * p->TotalDataReadBandwidth, (*p->StutterPeriod * p->TotalDataReadBandwidth) / 1024.0);
	dml2_printf("DML::%s: EffectiveCompressedBufferSize = %f (%f kbytes)\n", __func__, l->EffectiveCompressedBufferSize, l->EffectiveCompressedBufferSize / 1024.0);
	dml2_printf("DML::%s: PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer = %f (%f kbytes)\n", __func__, l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer, l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer / 1024);
	dml2_printf("DML::%s: ReturnBW = %f\n", __func__, p->ReturnBW);
	dml2_printf("DML::%s: TotalDataReadBandwidth = %f\n", __func__, p->TotalDataReadBandwidth);
	dml2_printf("DML::%s: TotalRowReadBandwidth = %f\n", __func__, l->TotalRowReadBandwidth);
	dml2_printf("DML::%s: DCFCLK = %f\n", __func__, p->DCFCLK);
#endif

	l->StutterBurstTime = l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer
		/ (p->ReturnBW * (p->hw_debug5 ? 1 : l->AverageDCCCompressionRate)) +
		(*p->StutterPeriod * p->TotalDataReadBandwidth - l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer)
		/ math_min2(p->DCFCLK * 64, p->ReturnBW * (p->hw_debug5 ? 1 : l->AverageDCCCompressionRate)) +
		*p->StutterPeriod * l->TotalRowReadBandwidth / p->ReturnBW;
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: Part 1 = %f\n", __func__, l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer / p->ReturnBW / (p->hw_debug5 ? 1 : l->AverageDCCCompressionRate));
	dml2_printf("DML::%s: Part 2 = %f\n", __func__, (*p->StutterPeriod * p->TotalDataReadBandwidth - l->PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer) / (p->DCFCLK * 64));
	dml2_printf("DML::%s: Part 3 = %f\n", __func__, *p->StutterPeriod * l->TotalRowReadBandwidth / p->ReturnBW);
	dml2_printf("DML::%s: StutterBurstTime = %f\n", __func__, l->StutterBurstTime);
#endif
	l->TotalActiveWriteback = 0;
	memset(l->stream_visited, 0, DML2_MAX_PLANES * sizeof(bool));

	for (unsigned int k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (!dml_is_phantom_pipe(&p->display_cfg->plane_descriptors[k])) {
			if (!l->stream_visited[p->display_cfg->plane_descriptors[k].stream_index]) {

				if (p->display_cfg->stream_descriptors[k].writeback.active_writebacks_per_stream > 0)
					l->TotalActiveWriteback = l->TotalActiveWriteback + 1;

				if (TotalNumberOfActiveOTG == 0) { // first otg
					SinglePixelClock = ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
					SingleHTotal = p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total;
					SingleVTotal = p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_total;
				} else if (SinglePixelClock != ((double)p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) ||
							SingleHTotal != p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.h_total ||
							SingleVTotal != p->display_cfg->stream_descriptors[p->display_cfg->plane_descriptors[k].stream_index].timing.v_total) {
					SameTiming = false;
				}
				TotalNumberOfActiveOTG = TotalNumberOfActiveOTG + 1;
				l->stream_visited[p->display_cfg->plane_descriptors[k].stream_index] = 1;
			}
		}
	}

	if (l->TotalActiveWriteback == 0) {
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: SRExitTime = %f\n", __func__, p->SRExitTime);
		dml2_printf("DML::%s: SRExitZ8Time = %f\n", __func__, p->SRExitZ8Time);
		dml2_printf("DML::%s: StutterPeriod = %f\n", __func__, *p->StutterPeriod);
#endif
		*p->StutterEfficiencyNotIncludingVBlank = math_max2(0., 1 - (p->SRExitTime + l->StutterBurstTime) / *p->StutterPeriod) * 100;
		*p->Z8StutterEfficiencyNotIncludingVBlank = math_max2(0., 1 - (p->SRExitZ8Time + l->StutterBurstTime) / *p->StutterPeriod) * 100;
		*p->NumberOfStutterBurstsPerFrame = (*p->StutterEfficiencyNotIncludingVBlank > 0 ? (unsigned int)(math_ceil2(l->VActiveTimeCriticalSurface / *p->StutterPeriod, 1)) : 0);
		*p->Z8NumberOfStutterBurstsPerFrame = (*p->Z8StutterEfficiencyNotIncludingVBlank > 0 ? (unsigned int)(math_ceil2(l->VActiveTimeCriticalSurface / *p->StutterPeriod, 1)) : 0);
	} else {
		*p->StutterEfficiencyNotIncludingVBlank = 0.;
		*p->Z8StutterEfficiencyNotIncludingVBlank = 0.;
		*p->NumberOfStutterBurstsPerFrame = 0;
		*p->Z8NumberOfStutterBurstsPerFrame = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: VActiveTimeCriticalSurface = %f\n", __func__, l->VActiveTimeCriticalSurface);
	dml2_printf("DML::%s: StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *p->StutterEfficiencyNotIncludingVBlank);
	dml2_printf("DML::%s: Z8StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *p->Z8StutterEfficiencyNotIncludingVBlank);
	dml2_printf("DML::%s: NumberOfStutterBurstsPerFrame = %u\n", __func__, *p->NumberOfStutterBurstsPerFrame);
	dml2_printf("DML::%s: Z8NumberOfStutterBurstsPerFrame = %u\n", __func__, *p->Z8NumberOfStutterBurstsPerFrame);
#endif

	if (*p->StutterEfficiencyNotIncludingVBlank > 0) {
		if (!((p->SynchronizeTimings || TotalNumberOfActiveOTG == 1) && SameTiming)) {
			*p->StutterEfficiency = *p->StutterEfficiencyNotIncludingVBlank;
		} else {
			*p->StutterEfficiency = (1 - (*p->NumberOfStutterBurstsPerFrame * p->SRExitTime + l->StutterBurstTime * l->VActiveTimeCriticalSurface / *p->StutterPeriod) / l->FrameTimeCriticalSurface) * 100;
		}
	} else {
		*p->StutterEfficiency = 0;
		*p->NumberOfStutterBurstsPerFrame = 0;
	}

	if (*p->Z8StutterEfficiencyNotIncludingVBlank > 0) {
		LastZ8StutterPeriod = l->VActiveTimeCriticalSurface - (*p->Z8NumberOfStutterBurstsPerFrame - 1) * *p->StutterPeriod;
		if (!((p->SynchronizeTimings || TotalNumberOfActiveOTG == 1) && SameTiming)) {
			*p->Z8StutterEfficiency = *p->Z8StutterEfficiencyNotIncludingVBlank;
		} else {
			*p->Z8StutterEfficiency = (1 - (*p->Z8NumberOfStutterBurstsPerFrame * p->SRExitZ8Time + l->StutterBurstTime * l->VActiveTimeCriticalSurface / *p->StutterPeriod) / l->FrameTimeCriticalSurface) * 100;
		}
	} else {
		*p->Z8StutterEfficiency = 0.;
		*p->Z8NumberOfStutterBurstsPerFrame = 0;
	}

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: TotalNumberOfActiveOTG = %u\n", __func__, TotalNumberOfActiveOTG);
	dml2_printf("DML::%s: SameTiming = %u\n", __func__, SameTiming);
	dml2_printf("DML::%s: SynchronizeTimings = %u\n", __func__, p->SynchronizeTimings);
	dml2_printf("DML::%s: LastZ8StutterPeriod = %f\n", __func__, LastZ8StutterPeriod);
	dml2_printf("DML::%s: Z8StutterEnterPlusExitWatermark = %f\n", __func__, p->Z8StutterEnterPlusExitWatermark);
	dml2_printf("DML::%s: StutterBurstTime = %f\n", __func__, l->StutterBurstTime);
	dml2_printf("DML::%s: StutterPeriod = %f\n", __func__, *p->StutterPeriod);
	dml2_printf("DML::%s: StutterEfficiency = %f\n", __func__, *p->StutterEfficiency);
	dml2_printf("DML::%s: Z8StutterEfficiency = %f\n", __func__, *p->Z8StutterEfficiency);
	dml2_printf("DML::%s: StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *p->StutterEfficiencyNotIncludingVBlank);
	dml2_printf("DML::%s: Z8NumberOfStutterBurstsPerFrame = %u\n", __func__, *p->Z8NumberOfStutterBurstsPerFrame);
#endif

	*p->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = !(!p->UnboundedRequestEnabled && (p->NumberOfActiveSurfaces == 1) && l->SinglePlaneCriticalSurface && l->SinglePipeCriticalSurface);

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: DETBufferSizeYCriticalSurface = %u\n", __func__, l->DETBufferSizeYCriticalSurface);
	dml2_printf("DML::%s: PixelChunkSizeInKByte = %u\n", __func__, p->PixelChunkSizeInKByte);
	dml2_printf("DML::%s: DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = %u\n", __func__, *p->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE);
#endif
}

static bool dml_core_mode_programming(struct dml2_core_calcs_mode_programming_ex *in_out_params)
{
	const struct dml2_display_cfg *display_cfg = in_out_params->in_display_cfg;
	const struct dml2_mcg_min_clock_table *min_clk_table = in_out_params->min_clk_table;
	const struct core_display_cfg_support_info *cfg_support_info = in_out_params->cfg_support_info;
	struct dml2_core_internal_display_mode_lib *mode_lib = in_out_params->mode_lib;
	struct dml2_display_cfg_programming *programming = in_out_params->programming;

	struct dml2_core_calcs_mode_programming_locals *s = &mode_lib->scratch.dml_core_mode_programming_locals;
	struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params *CalculateWatermarks_params = &mode_lib->scratch.CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params;
	struct dml2_core_calcs_CalculateVMRowAndSwath_params *CalculateVMRowAndSwath_params = &mode_lib->scratch.CalculateVMRowAndSwath_params;
	struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params *CalculateSwathAndDETConfiguration_params = &mode_lib->scratch.CalculateSwathAndDETConfiguration_params;
	struct dml2_core_calcs_CalculateStutterEfficiency_params *CalculateStutterEfficiency_params = &mode_lib->scratch.CalculateStutterEfficiency_params;
	struct dml2_core_calcs_CalculatePrefetchSchedule_params *CalculatePrefetchSchedule_params = &mode_lib->scratch.CalculatePrefetchSchedule_params;
	struct dml2_core_calcs_CheckGlobalPrefetchAdmissibility_params *CheckGlobalPrefetchAdmissibility_params = &mode_lib->scratch.CheckGlobalPrefetchAdmissibility_params;
	struct dml2_core_calcs_calculate_mcache_setting_params *calculate_mcache_setting_params = &mode_lib->scratch.calculate_mcache_setting_params;
	struct dml2_core_calcs_calculate_tdlut_setting_params *calculate_tdlut_setting_params = &mode_lib->scratch.calculate_tdlut_setting_params;
	struct dml2_core_shared_CalculateMetaAndPTETimes_params *CalculateMetaAndPTETimes_params = &mode_lib->scratch.CalculateMetaAndPTETimes_params;
	struct dml2_core_calcs_calculate_peak_bandwidth_required_params *calculate_peak_bandwidth_params = &mode_lib->scratch.calculate_peak_bandwidth_params;
	struct dml2_core_calcs_calculate_bytes_to_fetch_required_to_hide_latency_params *calculate_bytes_to_fetch_required_to_hide_latency_params = &mode_lib->scratch.calculate_bytes_to_fetch_required_to_hide_latency_params;

	unsigned int k;
	bool must_support_iflip;
	const long min_return_uclk_cycles = 83;
	const long min_return_fclk_cycles = 75;
	const double max_fclk_mhz = min_clk_table->max_clocks_khz.fclk / 1000.0;
	double hard_minimum_dcfclk_mhz = (double)min_clk_table->dram_bw_table.entries[0].min_dcfclk_khz / 1000.0;
	double max_uclk_mhz = 0;
	double min_return_latency_in_DCFCLK_cycles = 0;

	dml2_printf("DML::%s: --- START --- \n", __func__);

	memset(&mode_lib->scratch, 0, sizeof(struct dml2_core_internal_scratch));
	memset(&mode_lib->mp, 0, sizeof(struct dml2_core_internal_mode_program));

	s->num_active_planes = display_cfg->num_planes;
	get_stream_output_bpp(s->OutputBpp, display_cfg);

	mode_lib->mp.num_active_pipes = dml_get_num_active_pipes(display_cfg->num_planes, cfg_support_info);
	dml_calc_pipe_plane_mapping(cfg_support_info, mode_lib->mp.pipe_plane);

	mode_lib->mp.Dcfclk = programming->min_clocks.dcn4x.active.dcfclk_khz / 1000.0;
	mode_lib->mp.FabricClock = programming->min_clocks.dcn4x.active.fclk_khz / 1000.0;
	mode_lib->mp.dram_bw_mbps = uclk_khz_to_dram_bw_mbps(programming->min_clocks.dcn4x.active.uclk_khz, &mode_lib->soc.clk_table.dram_config);
	mode_lib->mp.uclk_freq_mhz = programming->min_clocks.dcn4x.active.uclk_khz / 1000.0;
	mode_lib->mp.GlobalDPPCLK = programming->min_clocks.dcn4x.dpprefclk_khz / 1000.0;
	s->SOCCLK = (double)programming->min_clocks.dcn4x.socclk_khz / 1000;
	mode_lib->mp.qos_param_index = get_qos_param_index(programming->min_clocks.dcn4x.active.uclk_khz, mode_lib->soc.qos_parameters.qos_params.dcn4x.per_uclk_dpm_params);
	mode_lib->mp.active_min_uclk_dpm_index = get_active_min_uclk_dpm_index(programming->min_clocks.dcn4x.active.uclk_khz, &mode_lib->soc.clk_table);

	for (k = 0; k < s->num_active_planes; ++k) {
		unsigned int stream_index = display_cfg->plane_descriptors[k].stream_index;
		dml2_assert(cfg_support_info->stream_support_info[stream_index].odms_used <= 4);
		dml2_assert(cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 4 ||
					cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 2 ||
					cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 1);

		if (cfg_support_info->stream_support_info[stream_index].odms_used > 1)
			dml2_assert(cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 1);

		switch (cfg_support_info->stream_support_info[stream_index].odms_used) {
		case (4):
			mode_lib->mp.ODMMode[k] = dml2_odm_mode_combine_4to1;
			break;
		case (3):
			mode_lib->mp.ODMMode[k] = dml2_odm_mode_combine_3to1;
			break;
		case (2):
			mode_lib->mp.ODMMode[k] = dml2_odm_mode_combine_2to1;
			break;
		default:
			if (cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 4)
				mode_lib->mp.ODMMode[k] = dml2_odm_mode_mso_1to4;
			else if (cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 2)
				mode_lib->mp.ODMMode[k] = dml2_odm_mode_mso_1to2;
			else
				mode_lib->mp.ODMMode[k] = dml2_odm_mode_bypass;
			break;
		}
	}

	for (k = 0; k < s->num_active_planes; ++k) {
		mode_lib->mp.NoOfDPP[k] = cfg_support_info->plane_support_info[k].dpps_used;
		mode_lib->mp.Dppclk[k] = programming->plane_programming[k].min_clocks.dcn4x.dppclk_khz / 1000.0;
		dml2_assert(mode_lib->mp.Dppclk[k] > 0);
	}

	for (k = 0; k < s->num_active_planes; ++k) {
		unsigned int stream_index = display_cfg->plane_descriptors[k].stream_index;
		mode_lib->mp.DSCCLK[k] = programming->stream_programming[stream_index].min_clocks.dcn4x.dscclk_khz / 1000.0;
		dml2_printf("DML::%s: k=%d stream_index=%d, mode_lib->mp.DSCCLK = %f\n", __func__, k, stream_index, mode_lib->mp.DSCCLK[k]);
	}

	mode_lib->mp.Dispclk = programming->min_clocks.dcn4x.dispclk_khz / 1000.0;
	mode_lib->mp.DCFCLKDeepSleep = programming->min_clocks.dcn4x.deepsleep_dcfclk_khz / 1000.0;

	dml2_assert(mode_lib->mp.Dcfclk > 0);
	dml2_assert(mode_lib->mp.FabricClock > 0);
	dml2_assert(mode_lib->mp.dram_bw_mbps > 0);
	dml2_assert(mode_lib->mp.uclk_freq_mhz > 0);
	dml2_assert(mode_lib->mp.GlobalDPPCLK > 0);
	dml2_assert(mode_lib->mp.Dispclk > 0);
	dml2_assert(mode_lib->mp.DCFCLKDeepSleep > 0);
	dml2_assert(s->SOCCLK > 0);

#ifdef __DML_VBA_DEBUG__
	// dml2_printf_dml_display_cfg_timing(&display_cfg->timing, s->num_active_planes);
	// dml2_printf_dml_display_cfg_plane(&display_cfg->plane, s->num_active_planes);
	// dml2_printf_dml_display_cfg_surface(&display_cfg->surface, s->num_active_planes);
	// dml2_printf_dml_display_cfg_output(&display_cfg->output, s->num_active_planes);
	// dml2_printf_dml_display_cfg_hw_resource(&display_cfg->hw, s->num_active_planes);

	dml2_printf("DML::%s: num_active_planes = %u\n", __func__, s->num_active_planes);
	dml2_printf("DML::%s: num_active_pipes = %u\n", __func__, mode_lib->mp.num_active_pipes);
	dml2_printf("DML::%s: Dcfclk = %f\n", __func__, mode_lib->mp.Dcfclk);
	dml2_printf("DML::%s: FabricClock = %f\n", __func__, mode_lib->mp.FabricClock);
	dml2_printf("DML::%s: dram_bw_mbps = %f\n", __func__, mode_lib->mp.dram_bw_mbps);
	dml2_printf("DML::%s: uclk_freq_mhz = %f\n", __func__, mode_lib->mp.uclk_freq_mhz);
	dml2_printf("DML::%s: Dispclk = %f\n", __func__, mode_lib->mp.Dispclk);
	for (k = 0; k < s->num_active_planes; ++k) {
		dml2_printf("DML::%s: Dppclk[%0d] = %f\n", __func__, k, mode_lib->mp.Dppclk[k]);
	}
	dml2_printf("DML::%s: GlobalDPPCLK = %f\n", __func__, mode_lib->mp.GlobalDPPCLK);
	dml2_printf("DML::%s: DCFCLKDeepSleep = %f\n", __func__, mode_lib->mp.DCFCLKDeepSleep);
	dml2_printf("DML::%s: SOCCLK = %f\n", __func__, s->SOCCLK);
	dml2_printf("DML::%s: min_clk_index = %0d\n", __func__, in_out_params->min_clk_index);
	dml2_printf("DML::%s: min_clk_table min_fclk_khz = %d\n", __func__, min_clk_table->dram_bw_table.entries[in_out_params->min_clk_index].min_fclk_khz);
	dml2_printf("DML::%s: min_clk_table uclk_mhz = %f\n", __func__, dram_bw_kbps_to_uclk_mhz(min_clk_table->dram_bw_table.entries[in_out_params->min_clk_index].pre_derate_dram_bw_kbps, &mode_lib->soc.clk_table.dram_config));
	for (k = 0; k < mode_lib->mp.num_active_pipes; ++k) {
		dml2_printf("DML::%s: pipe=%d is in plane=%d\n", __func__, k, mode_lib->mp.pipe_plane[k]);
		dml2_printf("DML::%s: Per-plane DPPPerSurface[%0d] = %d\n", __func__, k, mode_lib->mp.NoOfDPP[k]);
	}

	for (k = 0; k < s->num_active_planes; k++)
		dml2_printf("DML::%s: plane_%d: reserved_vblank_time_ns = %u\n", __func__, k, display_cfg->plane_descriptors[k].overrides.reserved_vblank_time_ns);
#endif

	CalculateMaxDETAndMinCompressedBufferSize(
		mode_lib->ip.config_return_buffer_size_in_kbytes,
		mode_lib->ip.config_return_buffer_segment_size_in_kbytes,
		mode_lib->ip.rob_buffer_size_kbytes,
		mode_lib->ip.max_num_dpp,
		display_cfg->overrides.hw.force_nom_det_size_kbytes.enable,
		display_cfg->overrides.hw.force_nom_det_size_kbytes.value,
		mode_lib->ip.dcn_mrq_present,

		/* Output */
		&s->MaxTotalDETInKByte,
		&s->NomDETInKByte,
		&s->MinCompressedBufferSizeInKByte);


	PixelClockAdjustmentForProgressiveToInterlaceUnit(display_cfg, mode_lib->ip.ptoi_supported, s->PixelClockBackEnd);

	for (k = 0; k < s->num_active_planes; ++k) {
		CalculateSinglePipeDPPCLKAndSCLThroughput(
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio,
			mode_lib->ip.max_dchub_pscl_bw_pix_per_clk,
			mode_lib->ip.max_pscl_lb_bw_pix_per_clk,
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000),
			display_cfg->plane_descriptors[k].pixel_format,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_taps,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_taps,

			/* Output */
			&mode_lib->mp.PSCL_THROUGHPUT[k],
			&mode_lib->mp.PSCL_THROUGHPUT_CHROMA[k],
			&mode_lib->mp.DPPCLKUsingSingleDPP[k]);
	}

	for (k = 0; k < s->num_active_planes; ++k) {
		CalculateBytePerPixelAndBlockSizes(
			display_cfg->plane_descriptors[k].pixel_format,
			display_cfg->plane_descriptors[k].surface.tiling,
			display_cfg->plane_descriptors[k].surface.plane0.pitch,
			display_cfg->plane_descriptors[k].surface.plane1.pitch,

			// Output
			&mode_lib->mp.BytePerPixelY[k],
			&mode_lib->mp.BytePerPixelC[k],
			&mode_lib->mp.BytePerPixelInDETY[k],
			&mode_lib->mp.BytePerPixelInDETC[k],
			&mode_lib->mp.Read256BlockHeightY[k],
			&mode_lib->mp.Read256BlockHeightC[k],
			&mode_lib->mp.Read256BlockWidthY[k],
			&mode_lib->mp.Read256BlockWidthC[k],
			&mode_lib->mp.MacroTileHeightY[k],
			&mode_lib->mp.MacroTileHeightC[k],
			&mode_lib->mp.MacroTileWidthY[k],
			&mode_lib->mp.MacroTileWidthC[k],
			&mode_lib->mp.surf_linear128_l[k],
			&mode_lib->mp.surf_linear128_c[k]);
	}

	CalculateSwathWidth(
		display_cfg,
		false, // ForceSingleDPP
		s->num_active_planes,
		mode_lib->mp.ODMMode,
		mode_lib->mp.BytePerPixelY,
		mode_lib->mp.BytePerPixelC,
		mode_lib->mp.Read256BlockHeightY,
		mode_lib->mp.Read256BlockHeightC,
		mode_lib->mp.Read256BlockWidthY,
		mode_lib->mp.Read256BlockWidthC,
		mode_lib->mp.surf_linear128_l,
		mode_lib->mp.surf_linear128_c,
		mode_lib->mp.NoOfDPP,

		/* Output */
		mode_lib->mp.req_per_swath_ub_l,
		mode_lib->mp.req_per_swath_ub_c,
		mode_lib->mp.SwathWidthSingleDPPY,
		mode_lib->mp.SwathWidthSingleDPPC,
		mode_lib->mp.SwathWidthY,
		mode_lib->mp.SwathWidthC,
		s->dummy_integer_array[0], // unsigned int MaximumSwathHeightY[]
		s->dummy_integer_array[1], // unsigned int MaximumSwathHeightC[]
		mode_lib->mp.swath_width_luma_ub,
		mode_lib->mp.swath_width_chroma_ub);

	for (k = 0; k < s->num_active_planes; ++k) {
		mode_lib->mp.cursor_bw[k] = display_cfg->plane_descriptors[k].cursor.num_cursors * display_cfg->plane_descriptors[k].cursor.cursor_width * display_cfg->plane_descriptors[k].cursor.cursor_bpp / 8.0 /
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000));
		mode_lib->mp.vactive_sw_bw_l[k] = mode_lib->mp.SwathWidthSingleDPPY[k] * mode_lib->mp.BytePerPixelY[k] / (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		mode_lib->mp.vactive_sw_bw_c[k] = mode_lib->mp.SwathWidthSingleDPPC[k] * mode_lib->mp.BytePerPixelC[k] / (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		dml2_printf("DML::%s: vactive_sw_bw_l[%i] = %fBps\n", __func__, k, mode_lib->mp.vactive_sw_bw_l[k]);
		dml2_printf("DML::%s: vactive_sw_bw_c[%i] = %fBps\n", __func__, k, mode_lib->mp.vactive_sw_bw_c[k]);
	}

	CalculateSwathAndDETConfiguration_params->display_cfg = display_cfg;
	CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSizeInKByte = mode_lib->ip.config_return_buffer_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->MaxTotalDETInKByte = s->MaxTotalDETInKByte;
	CalculateSwathAndDETConfiguration_params->MinCompressedBufferSizeInKByte = s->MinCompressedBufferSizeInKByte;
	CalculateSwathAndDETConfiguration_params->rob_buffer_size_kbytes = mode_lib->ip.rob_buffer_size_kbytes;
	CalculateSwathAndDETConfiguration_params->pixel_chunk_size_kbytes = mode_lib->ip.pixel_chunk_size_kbytes;
	CalculateSwathAndDETConfiguration_params->rob_buffer_size_kbytes = mode_lib->ip.rob_buffer_size_kbytes;
	CalculateSwathAndDETConfiguration_params->pixel_chunk_size_kbytes = mode_lib->ip.pixel_chunk_size_kbytes;
	CalculateSwathAndDETConfiguration_params->ForceSingleDPP = false;
	CalculateSwathAndDETConfiguration_params->NumberOfActiveSurfaces = s->num_active_planes;
	CalculateSwathAndDETConfiguration_params->nomDETInKByte = s->NomDETInKByte;
	CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSegmentSizeInkByte = mode_lib->ip.config_return_buffer_segment_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->CompressedBufferSegmentSizeInkByte = mode_lib->ip.compressed_buffer_segment_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->ReadBandwidthLuma = mode_lib->mp.vactive_sw_bw_l;
	CalculateSwathAndDETConfiguration_params->ReadBandwidthChroma = mode_lib->mp.vactive_sw_bw_c;
	CalculateSwathAndDETConfiguration_params->MaximumSwathWidthLuma = s->dummy_single_array[0];
	CalculateSwathAndDETConfiguration_params->MaximumSwathWidthChroma = s->dummy_single_array[1];
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightY = mode_lib->mp.Read256BlockHeightY;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightC = mode_lib->mp.Read256BlockHeightC;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthY = mode_lib->mp.Read256BlockWidthY;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthC = mode_lib->mp.Read256BlockWidthC;
	CalculateSwathAndDETConfiguration_params->surf_linear128_l = mode_lib->mp.surf_linear128_l;
	CalculateSwathAndDETConfiguration_params->surf_linear128_c = mode_lib->mp.surf_linear128_c;
	CalculateSwathAndDETConfiguration_params->ODMMode = mode_lib->mp.ODMMode;
	CalculateSwathAndDETConfiguration_params->DPPPerSurface = mode_lib->mp.NoOfDPP;
	CalculateSwathAndDETConfiguration_params->BytePerPixY = mode_lib->mp.BytePerPixelY;
	CalculateSwathAndDETConfiguration_params->BytePerPixC = mode_lib->mp.BytePerPixelC;
	CalculateSwathAndDETConfiguration_params->BytePerPixDETY = mode_lib->mp.BytePerPixelInDETY;
	CalculateSwathAndDETConfiguration_params->BytePerPixDETC = mode_lib->mp.BytePerPixelInDETC;
	CalculateSwathAndDETConfiguration_params->mrq_present = mode_lib->ip.dcn_mrq_present;

	// output
	CalculateSwathAndDETConfiguration_params->req_per_swath_ub_l = mode_lib->mp.req_per_swath_ub_l;
	CalculateSwathAndDETConfiguration_params->req_per_swath_ub_c = mode_lib->mp.req_per_swath_ub_c;
	CalculateSwathAndDETConfiguration_params->swath_width_luma_ub = s->dummy_long_array[0];
	CalculateSwathAndDETConfiguration_params->swath_width_chroma_ub = s->dummy_long_array[1];
	CalculateSwathAndDETConfiguration_params->SwathWidth = s->dummy_long_array[2];
	CalculateSwathAndDETConfiguration_params->SwathWidthChroma = s->dummy_long_array[3];
	CalculateSwathAndDETConfiguration_params->SwathHeightY = mode_lib->mp.SwathHeightY;
	CalculateSwathAndDETConfiguration_params->SwathHeightC = mode_lib->mp.SwathHeightC;
	CalculateSwathAndDETConfiguration_params->request_size_bytes_luma = mode_lib->mp.request_size_bytes_luma;
	CalculateSwathAndDETConfiguration_params->request_size_bytes_chroma = mode_lib->mp.request_size_bytes_chroma;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeInKByte = mode_lib->mp.DETBufferSizeInKByte;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeY = mode_lib->mp.DETBufferSizeY;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeC = mode_lib->mp.DETBufferSizeC;
	CalculateSwathAndDETConfiguration_params->full_swath_bytes_l = s->full_swath_bytes_l;
	CalculateSwathAndDETConfiguration_params->full_swath_bytes_c = s->full_swath_bytes_c;
	CalculateSwathAndDETConfiguration_params->UnboundedRequestEnabled = &mode_lib->mp.UnboundedRequestEnabled;
	CalculateSwathAndDETConfiguration_params->compbuf_reserved_space_64b = &mode_lib->mp.compbuf_reserved_space_64b;
	CalculateSwathAndDETConfiguration_params->hw_debug5 = &mode_lib->mp.hw_debug5;
	CalculateSwathAndDETConfiguration_params->CompressedBufferSizeInkByte = &mode_lib->mp.CompressedBufferSizeInkByte;
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupportPerSurface = &s->dummy_boolean_array[0][0];
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupport = &s->dummy_boolean[0];

	// Calculate DET size, swath height here.
	CalculateSwathAndDETConfiguration(&mode_lib->scratch, CalculateSwathAndDETConfiguration_params);

	// DSC Delay
	for (k = 0; k < s->num_active_planes; ++k) {
		mode_lib->mp.DSCDelay[k] = DSCDelayRequirement(cfg_support_info->stream_support_info[display_cfg->plane_descriptors[k].stream_index].dsc_enable,
			mode_lib->mp.ODMMode[k],
			mode_lib->ip.maximum_dsc_bits_per_component,
			s->OutputBpp[k],
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total,
			cfg_support_info->stream_support_info[display_cfg->plane_descriptors[k].stream_index].num_dsc_slices,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder,
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000),
			s->PixelClockBackEnd[k]);
	}

	// Prefetch
	if (mode_lib->soc.mall_allocated_for_dcn_mbytes == 0) {
		for (k = 0; k < s->num_active_planes; ++k)
			mode_lib->mp.SurfaceSizeInTheMALL[k] = 0;
	} else {
		CalculateSurfaceSizeInMall(
			display_cfg,
			s->num_active_planes,
			mode_lib->soc.mall_allocated_for_dcn_mbytes,
			mode_lib->mp.BytePerPixelY,
			mode_lib->mp.BytePerPixelC,
			mode_lib->mp.Read256BlockWidthY,
			mode_lib->mp.Read256BlockWidthC,
			mode_lib->mp.Read256BlockHeightY,
			mode_lib->mp.Read256BlockHeightC,
			mode_lib->mp.MacroTileWidthY,
			mode_lib->mp.MacroTileWidthC,
			mode_lib->mp.MacroTileHeightY,
			mode_lib->mp.MacroTileHeightC,

			/* Output */
			mode_lib->mp.SurfaceSizeInTheMALL,
			&s->dummy_boolean[0]); /* bool *ExceededMALLSize */
	}

	for (k = 0; k < s->num_active_planes; ++k) {
		s->SurfaceParameters[k].PixelClock = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
		s->SurfaceParameters[k].DPPPerSurface = mode_lib->mp.NoOfDPP[k];
		s->SurfaceParameters[k].RotationAngle = display_cfg->plane_descriptors[k].composition.rotation_angle;
		s->SurfaceParameters[k].ViewportHeight = display_cfg->plane_descriptors[k].composition.viewport.plane0.height;
		s->SurfaceParameters[k].ViewportHeightC = display_cfg->plane_descriptors[k].composition.viewport.plane1.height;
		s->SurfaceParameters[k].BlockWidth256BytesY = mode_lib->mp.Read256BlockWidthY[k];
		s->SurfaceParameters[k].BlockHeight256BytesY = mode_lib->mp.Read256BlockHeightY[k];
		s->SurfaceParameters[k].BlockWidth256BytesC = mode_lib->mp.Read256BlockWidthC[k];
		s->SurfaceParameters[k].BlockHeight256BytesC = mode_lib->mp.Read256BlockHeightC[k];
		s->SurfaceParameters[k].BlockWidthY = mode_lib->mp.MacroTileWidthY[k];
		s->SurfaceParameters[k].BlockHeightY = mode_lib->mp.MacroTileHeightY[k];
		s->SurfaceParameters[k].BlockWidthC = mode_lib->mp.MacroTileWidthC[k];
		s->SurfaceParameters[k].BlockHeightC = mode_lib->mp.MacroTileHeightC[k];
		s->SurfaceParameters[k].InterlaceEnable = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced;
		s->SurfaceParameters[k].HTotal = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total;
		s->SurfaceParameters[k].DCCEnable = display_cfg->plane_descriptors[k].surface.dcc.enable;
		s->SurfaceParameters[k].SourcePixelFormat = display_cfg->plane_descriptors[k].pixel_format;
		s->SurfaceParameters[k].SurfaceTiling = display_cfg->plane_descriptors[k].surface.tiling;
		s->SurfaceParameters[k].BytePerPixelY = mode_lib->mp.BytePerPixelY[k];
		s->SurfaceParameters[k].BytePerPixelC = mode_lib->mp.BytePerPixelC[k];
		s->SurfaceParameters[k].ProgressiveToInterlaceUnitInOPP = mode_lib->ip.ptoi_supported;
		s->SurfaceParameters[k].VRatio = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		s->SurfaceParameters[k].VRatioChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		s->SurfaceParameters[k].VTaps = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps;
		s->SurfaceParameters[k].VTapsChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps;
		s->SurfaceParameters[k].PitchY = display_cfg->plane_descriptors[k].surface.plane0.pitch;
		s->SurfaceParameters[k].PitchC = display_cfg->plane_descriptors[k].surface.plane1.pitch;
		s->SurfaceParameters[k].ViewportStationary = display_cfg->plane_descriptors[k].composition.viewport.stationary;
		s->SurfaceParameters[k].ViewportXStart = display_cfg->plane_descriptors[k].composition.viewport.plane0.x_start;
		s->SurfaceParameters[k].ViewportYStart = display_cfg->plane_descriptors[k].composition.viewport.plane0.y_start;
		s->SurfaceParameters[k].ViewportXStartC = display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start;
		s->SurfaceParameters[k].ViewportYStartC = display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start;
		s->SurfaceParameters[k].FORCE_ONE_ROW_FOR_FRAME = display_cfg->plane_descriptors[k].overrides.hw.force_one_row_for_frame;
		s->SurfaceParameters[k].SwathHeightY = mode_lib->mp.SwathHeightY[k];
		s->SurfaceParameters[k].SwathHeightC = mode_lib->mp.SwathHeightC[k];
		s->SurfaceParameters[k].DCCMetaPitchY = display_cfg->plane_descriptors[k].surface.dcc.plane0.pitch;
		s->SurfaceParameters[k].DCCMetaPitchC = display_cfg->plane_descriptors[k].surface.dcc.plane1.pitch;
	}

	CalculateVMRowAndSwath_params->display_cfg = display_cfg;
	CalculateVMRowAndSwath_params->NumberOfActiveSurfaces = s->num_active_planes;
	CalculateVMRowAndSwath_params->myPipe = s->SurfaceParameters;
	CalculateVMRowAndSwath_params->SurfaceSizeInMALL = mode_lib->mp.SurfaceSizeInTheMALL;
	CalculateVMRowAndSwath_params->PTEBufferSizeInRequestsLuma = mode_lib->ip.dpte_buffer_size_in_pte_reqs_luma;
	CalculateVMRowAndSwath_params->PTEBufferSizeInRequestsChroma = mode_lib->ip.dpte_buffer_size_in_pte_reqs_chroma;
	CalculateVMRowAndSwath_params->MALLAllocatedForDCN = mode_lib->soc.mall_allocated_for_dcn_mbytes;
	CalculateVMRowAndSwath_params->SwathWidthY = mode_lib->mp.SwathWidthY;
	CalculateVMRowAndSwath_params->SwathWidthC = mode_lib->mp.SwathWidthC;
	CalculateVMRowAndSwath_params->HostVMMinPageSize = mode_lib->soc.hostvm_min_page_size_kbytes;
	CalculateVMRowAndSwath_params->DCCMetaBufferSizeBytes = mode_lib->ip.dcc_meta_buffer_size_bytes;
	CalculateVMRowAndSwath_params->mrq_present = mode_lib->ip.dcn_mrq_present;

	// output
	CalculateVMRowAndSwath_params->PTEBufferSizeNotExceeded = s->dummy_boolean_array[0];
	CalculateVMRowAndSwath_params->dpte_row_width_luma_ub = mode_lib->mp.dpte_row_width_luma_ub;
	CalculateVMRowAndSwath_params->dpte_row_width_chroma_ub = mode_lib->mp.dpte_row_width_chroma_ub;
	CalculateVMRowAndSwath_params->dpte_row_height_luma = mode_lib->mp.dpte_row_height;
	CalculateVMRowAndSwath_params->dpte_row_height_chroma = mode_lib->mp.dpte_row_height_chroma;
	CalculateVMRowAndSwath_params->dpte_row_height_linear_luma = mode_lib->mp.dpte_row_height_linear;
	CalculateVMRowAndSwath_params->dpte_row_height_linear_chroma = mode_lib->mp.dpte_row_height_linear_chroma;
	CalculateVMRowAndSwath_params->vm_group_bytes = mode_lib->mp.vm_group_bytes;
	CalculateVMRowAndSwath_params->dpte_group_bytes = mode_lib->mp.dpte_group_bytes;
	CalculateVMRowAndSwath_params->PixelPTEReqWidthY = mode_lib->mp.PixelPTEReqWidthY;
	CalculateVMRowAndSwath_params->PixelPTEReqHeightY = mode_lib->mp.PixelPTEReqHeightY;
	CalculateVMRowAndSwath_params->PTERequestSizeY = mode_lib->mp.PTERequestSizeY;
	CalculateVMRowAndSwath_params->PixelPTEReqWidthC = mode_lib->mp.PixelPTEReqWidthC;
	CalculateVMRowAndSwath_params->PixelPTEReqHeightC = mode_lib->mp.PixelPTEReqHeightC;
	CalculateVMRowAndSwath_params->PTERequestSizeC = mode_lib->mp.PTERequestSizeC;
	CalculateVMRowAndSwath_params->vmpg_width_y = s->vmpg_width_y;
	CalculateVMRowAndSwath_params->vmpg_height_y = s->vmpg_height_y;
	CalculateVMRowAndSwath_params->vmpg_width_c = s->vmpg_width_c;
	CalculateVMRowAndSwath_params->vmpg_height_c = s->vmpg_height_c;
	CalculateVMRowAndSwath_params->dpde0_bytes_per_frame_ub_l = mode_lib->mp.dpde0_bytes_per_frame_ub_l;
	CalculateVMRowAndSwath_params->dpde0_bytes_per_frame_ub_c = mode_lib->mp.dpde0_bytes_per_frame_ub_c;
	CalculateVMRowAndSwath_params->PrefetchSourceLinesY = mode_lib->mp.PrefetchSourceLinesY;
	CalculateVMRowAndSwath_params->PrefetchSourceLinesC = mode_lib->mp.PrefetchSourceLinesC;
	CalculateVMRowAndSwath_params->VInitPreFillY = mode_lib->mp.VInitPreFillY;
	CalculateVMRowAndSwath_params->VInitPreFillC = mode_lib->mp.VInitPreFillC;
	CalculateVMRowAndSwath_params->MaxNumSwathY = mode_lib->mp.MaxNumSwathY;
	CalculateVMRowAndSwath_params->MaxNumSwathC = mode_lib->mp.MaxNumSwathC;
	CalculateVMRowAndSwath_params->dpte_row_bw = mode_lib->mp.dpte_row_bw;
	CalculateVMRowAndSwath_params->PixelPTEBytesPerRow = mode_lib->mp.PixelPTEBytesPerRow;
	CalculateVMRowAndSwath_params->dpte_row_bytes_per_row_l = s->dpte_row_bytes_per_row_l;
	CalculateVMRowAndSwath_params->dpte_row_bytes_per_row_c = s->dpte_row_bytes_per_row_c;
	CalculateVMRowAndSwath_params->vm_bytes = mode_lib->mp.vm_bytes;
	CalculateVMRowAndSwath_params->use_one_row_for_frame = mode_lib->mp.use_one_row_for_frame;
	CalculateVMRowAndSwath_params->use_one_row_for_frame_flip = mode_lib->mp.use_one_row_for_frame_flip;
	CalculateVMRowAndSwath_params->is_using_mall_for_ss = mode_lib->mp.is_using_mall_for_ss;
	CalculateVMRowAndSwath_params->PTE_BUFFER_MODE = mode_lib->mp.PTE_BUFFER_MODE;
	CalculateVMRowAndSwath_params->BIGK_FRAGMENT_SIZE = mode_lib->mp.BIGK_FRAGMENT_SIZE;
	CalculateVMRowAndSwath_params->DCCMetaBufferSizeNotExceeded = s->dummy_boolean_array[1];
	CalculateVMRowAndSwath_params->meta_row_bw = mode_lib->mp.meta_row_bw;
	CalculateVMRowAndSwath_params->meta_row_bytes = mode_lib->mp.meta_row_bytes;
	CalculateVMRowAndSwath_params->meta_row_bytes_per_row_ub_l = s->meta_row_bytes_per_row_ub_l;
	CalculateVMRowAndSwath_params->meta_row_bytes_per_row_ub_c = s->meta_row_bytes_per_row_ub_c;
	CalculateVMRowAndSwath_params->meta_req_width_luma = mode_lib->mp.meta_req_width;
	CalculateVMRowAndSwath_params->meta_req_height_luma = mode_lib->mp.meta_req_height;
	CalculateVMRowAndSwath_params->meta_row_width_luma = mode_lib->mp.meta_row_width;
	CalculateVMRowAndSwath_params->meta_row_height_luma = mode_lib->mp.meta_row_height;
	CalculateVMRowAndSwath_params->meta_pte_bytes_per_frame_ub_l = mode_lib->mp.meta_pte_bytes_per_frame_ub_l;
	CalculateVMRowAndSwath_params->meta_req_width_chroma = mode_lib->mp.meta_req_width_chroma;
	CalculateVMRowAndSwath_params->meta_row_height_chroma = mode_lib->mp.meta_row_height_chroma;
	CalculateVMRowAndSwath_params->meta_row_width_chroma = mode_lib->mp.meta_row_width_chroma;
	CalculateVMRowAndSwath_params->meta_req_height_chroma = mode_lib->mp.meta_req_height_chroma;
	CalculateVMRowAndSwath_params->meta_pte_bytes_per_frame_ub_c = mode_lib->mp.meta_pte_bytes_per_frame_ub_c;

	CalculateVMRowAndSwath(&mode_lib->scratch, CalculateVMRowAndSwath_params);

	memset(calculate_mcache_setting_params, 0, sizeof(struct dml2_core_calcs_calculate_mcache_setting_params));
	if (mode_lib->soc.mall_allocated_for_dcn_mbytes == 0 || mode_lib->ip.dcn_mrq_present) {
		for (k = 0; k < s->num_active_planes; k++) {
			mode_lib->mp.mall_prefetch_sdp_overhead_factor[k] = 1.0;
			mode_lib->mp.mall_prefetch_dram_overhead_factor[k] = 1.0;
			mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p0[k] = 1.0;
			mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p0[k] = 1.0;
			mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p1[k] = 1.0;
			mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p1[k] = 1.0;
		}
	} else {
		for (k = 0; k < s->num_active_planes; k++) {
			calculate_mcache_setting_params->dcc_enable = display_cfg->plane_descriptors[k].surface.dcc.enable;
			calculate_mcache_setting_params->num_chans = mode_lib->soc.clk_table.dram_config.channel_count;
			calculate_mcache_setting_params->mem_word_bytes = mode_lib->soc.mem_word_bytes;
			calculate_mcache_setting_params->mcache_size_bytes = mode_lib->soc.mcache_size_bytes;
			calculate_mcache_setting_params->mcache_line_size_bytes = mode_lib->soc.mcache_line_size_bytes;
			calculate_mcache_setting_params->gpuvm_enable = display_cfg->gpuvm_enable;
			calculate_mcache_setting_params->gpuvm_page_size_kbytes = display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes;

			calculate_mcache_setting_params->source_format = display_cfg->plane_descriptors[k].pixel_format;
			calculate_mcache_setting_params->surf_vert = dml_is_vertical_rotation(display_cfg->plane_descriptors[k].composition.rotation_angle);
			calculate_mcache_setting_params->vp_stationary = display_cfg->plane_descriptors[k].composition.viewport.stationary;
			calculate_mcache_setting_params->tiling_mode = display_cfg->plane_descriptors[k].surface.tiling;
			calculate_mcache_setting_params->imall_enable = mode_lib->ip.imall_supported && display_cfg->plane_descriptors[k].overrides.legacy_svp_config == dml2_svp_mode_override_imall;

			calculate_mcache_setting_params->vp_start_x_l = display_cfg->plane_descriptors[k].composition.viewport.plane0.x_start;
			calculate_mcache_setting_params->vp_start_y_l = display_cfg->plane_descriptors[k].composition.viewport.plane0.y_start;
			calculate_mcache_setting_params->full_vp_width_l = display_cfg->plane_descriptors[k].composition.viewport.plane0.width;
			calculate_mcache_setting_params->full_vp_height_l = display_cfg->plane_descriptors[k].composition.viewport.plane0.height;
			calculate_mcache_setting_params->blk_width_l = mode_lib->mp.MacroTileWidthY[k];
			calculate_mcache_setting_params->blk_height_l = mode_lib->mp.MacroTileHeightY[k];
			calculate_mcache_setting_params->vmpg_width_l = s->vmpg_width_y[k];
			calculate_mcache_setting_params->vmpg_height_l = s->vmpg_height_y[k];
			calculate_mcache_setting_params->full_swath_bytes_l = s->full_swath_bytes_l[k];
			calculate_mcache_setting_params->bytes_per_pixel_l = mode_lib->mp.BytePerPixelY[k];

			calculate_mcache_setting_params->vp_start_x_c = display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start;
			calculate_mcache_setting_params->vp_start_y_c = display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start;
			calculate_mcache_setting_params->full_vp_width_c = display_cfg->plane_descriptors[k].composition.viewport.plane1.width;
			calculate_mcache_setting_params->full_vp_height_c = display_cfg->plane_descriptors[k].composition.viewport.plane1.height;
			calculate_mcache_setting_params->blk_width_c = mode_lib->mp.MacroTileWidthC[k];
			calculate_mcache_setting_params->blk_height_c = mode_lib->mp.MacroTileHeightC[k];
			calculate_mcache_setting_params->vmpg_width_c = s->vmpg_width_c[k];
			calculate_mcache_setting_params->vmpg_height_c = s->vmpg_height_c[k];
			calculate_mcache_setting_params->full_swath_bytes_c = s->full_swath_bytes_c[k];
			calculate_mcache_setting_params->bytes_per_pixel_c = mode_lib->mp.BytePerPixelC[k];

			// output
			calculate_mcache_setting_params->dcc_dram_bw_nom_overhead_factor_l = &mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p0[k];
			calculate_mcache_setting_params->dcc_dram_bw_pref_overhead_factor_l = &mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p0[k];
			calculate_mcache_setting_params->dcc_dram_bw_nom_overhead_factor_c = &mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p1[k];
			calculate_mcache_setting_params->dcc_dram_bw_pref_overhead_factor_c = &mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p1[k];

			calculate_mcache_setting_params->num_mcaches_l = &mode_lib->mp.num_mcaches_l[k];
			calculate_mcache_setting_params->mcache_row_bytes_l = &mode_lib->mp.mcache_row_bytes_l[k];
			calculate_mcache_setting_params->mcache_offsets_l = mode_lib->mp.mcache_offsets_l[k];
			calculate_mcache_setting_params->mcache_shift_granularity_l = &mode_lib->mp.mcache_shift_granularity_l[k];

			calculate_mcache_setting_params->num_mcaches_c = &mode_lib->mp.num_mcaches_c[k];
			calculate_mcache_setting_params->mcache_row_bytes_c = &mode_lib->mp.mcache_row_bytes_c[k];
			calculate_mcache_setting_params->mcache_offsets_c = mode_lib->mp.mcache_offsets_c[k];
			calculate_mcache_setting_params->mcache_shift_granularity_c = &mode_lib->mp.mcache_shift_granularity_c[k];

			calculate_mcache_setting_params->mall_comb_mcache_l = &mode_lib->mp.mall_comb_mcache_l[k];
			calculate_mcache_setting_params->mall_comb_mcache_c = &mode_lib->mp.mall_comb_mcache_c[k];
			calculate_mcache_setting_params->lc_comb_mcache = &mode_lib->mp.lc_comb_mcache[k];
			calculate_mcache_setting(&mode_lib->scratch, calculate_mcache_setting_params);
		}

		calculate_mall_bw_overhead_factor(
			mode_lib->mp.mall_prefetch_sdp_overhead_factor,
			mode_lib->mp.mall_prefetch_dram_overhead_factor,

			// input
			display_cfg,
			s->num_active_planes);
	}

	// Calculate all the bandwidth availabe
	calculate_bandwidth_available(
		mode_lib->mp.avg_bandwidth_available_min,
		mode_lib->mp.avg_bandwidth_available,
		mode_lib->mp.urg_bandwidth_available_min,
		mode_lib->mp.urg_bandwidth_available,
		mode_lib->mp.urg_bandwidth_available_vm_only,
		mode_lib->mp.urg_bandwidth_available_pixel_and_vm,

		&mode_lib->soc,
		display_cfg->hostvm_enable,
		mode_lib->mp.Dcfclk,
		mode_lib->mp.FabricClock,
		mode_lib->mp.dram_bw_mbps);


	calculate_hostvm_inefficiency_factor(
		&s->HostVMInefficiencyFactor,
		&s->HostVMInefficiencyFactorPrefetch,

		display_cfg->gpuvm_enable,
		display_cfg->hostvm_enable,
		mode_lib->ip.remote_iommu_outstanding_translations,
		mode_lib->soc.max_outstanding_reqs,
		mode_lib->mp.urg_bandwidth_available_pixel_and_vm[dml2_core_internal_soc_state_sys_active],
		mode_lib->mp.urg_bandwidth_available_vm_only[dml2_core_internal_soc_state_sys_active]);

	s->TotalDCCActiveDPP = 0;
	s->TotalActiveDPP = 0;
	for (k = 0; k < s->num_active_planes; ++k) {
		s->TotalActiveDPP = s->TotalActiveDPP + mode_lib->mp.NoOfDPP[k];
		if (display_cfg->plane_descriptors[k].surface.dcc.enable)
			s->TotalDCCActiveDPP = s->TotalDCCActiveDPP + mode_lib->mp.NoOfDPP[k];
	}
	// Calculate tdlut schedule related terms
	for (k = 0; k <= s->num_active_planes - 1; k++) {
		calculate_tdlut_setting_params->dispclk_mhz = mode_lib->mp.Dispclk;
		calculate_tdlut_setting_params->setup_for_tdlut = display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut;
		calculate_tdlut_setting_params->tdlut_width_mode = display_cfg->plane_descriptors[k].tdlut.tdlut_width_mode;
		calculate_tdlut_setting_params->tdlut_addressing_mode = display_cfg->plane_descriptors[k].tdlut.tdlut_addressing_mode;
		calculate_tdlut_setting_params->cursor_buffer_size = mode_lib->ip.cursor_buffer_size;
		calculate_tdlut_setting_params->gpuvm_enable = display_cfg->gpuvm_enable;
		calculate_tdlut_setting_params->gpuvm_page_size_kbytes = display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes;

		// output
		calculate_tdlut_setting_params->tdlut_pte_bytes_per_frame = &s->tdlut_pte_bytes_per_frame[k];
		calculate_tdlut_setting_params->tdlut_bytes_per_frame = &s->tdlut_bytes_per_frame[k];
		calculate_tdlut_setting_params->tdlut_groups_per_2row_ub = &s->tdlut_groups_per_2row_ub[k];
		calculate_tdlut_setting_params->tdlut_opt_time = &s->tdlut_opt_time[k];
		calculate_tdlut_setting_params->tdlut_drain_time = &s->tdlut_drain_time[k];
		calculate_tdlut_setting_params->tdlut_bytes_per_group = &s->tdlut_bytes_per_group[k];

		calculate_tdlut_setting(&mode_lib->scratch, calculate_tdlut_setting_params);
	}

	if (mode_lib->soc.qos_parameters.qos_type == dml2_qos_param_type_dcn3)
		s->ReorderingBytes = (unsigned int)(mode_lib->soc.clk_table.dram_config.channel_count * math_max3(mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_out_of_order_return_per_channel_pixel_only_bytes,
										mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes,
										mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_out_of_order_return_per_channel_vm_only_bytes));

	CalculateExtraLatency(
		display_cfg,
		mode_lib->ip.rob_buffer_size_kbytes,
		mode_lib->soc.qos_parameters.qos_params.dcn32x.loaded_round_trip_latency_fclk_cycles,
		s->ReorderingBytes,
		mode_lib->mp.Dcfclk,
		mode_lib->mp.FabricClock,
		mode_lib->ip.pixel_chunk_size_kbytes,
		mode_lib->mp.urg_bandwidth_available_min[dml2_core_internal_soc_state_sys_active],
		s->num_active_planes,
		mode_lib->mp.NoOfDPP,
		mode_lib->mp.dpte_group_bytes,
		s->tdlut_bytes_per_group,
		s->HostVMInefficiencyFactor,
		s->HostVMInefficiencyFactorPrefetch,
		mode_lib->soc.hostvm_min_page_size_kbytes,
		mode_lib->soc.qos_parameters.qos_type,
		!(display_cfg->overrides.max_outstanding_when_urgent_expected_disable),
		mode_lib->soc.max_outstanding_reqs,
		mode_lib->mp.request_size_bytes_luma,
		mode_lib->mp.request_size_bytes_chroma,
		mode_lib->ip.meta_chunk_size_kbytes,
		mode_lib->ip.dchub_arb_to_ret_delay,
		mode_lib->mp.TripToMemory,
		mode_lib->ip.hostvm_mode,

		// output
		&mode_lib->mp.ExtraLatency,
		&mode_lib->mp.ExtraLatency_sr,
		&mode_lib->mp.ExtraLatencyPrefetch);

	mode_lib->mp.TCalc = 24.0 / mode_lib->mp.DCFCLKDeepSleep;

	for (k = 0; k < s->num_active_planes; ++k) {
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0) {
			mode_lib->mp.WritebackDelay[k] =
				mode_lib->soc.qos_parameters.writeback.base_latency_us
				+ CalculateWriteBackDelay(
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].pixel_format,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].h_ratio,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_ratio,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].v_taps,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_width,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].output_height,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[0].input_height,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total) / mode_lib->mp.Dispclk;
		} else
			mode_lib->mp.WritebackDelay[k] = 0;
	}

	/* VActive bytes to fetch for UCLK P-State */
	calculate_bytes_to_fetch_required_to_hide_latency_params->display_cfg = display_cfg;
	calculate_bytes_to_fetch_required_to_hide_latency_params->mrq_present = mode_lib->ip.dcn_mrq_present;

	calculate_bytes_to_fetch_required_to_hide_latency_params->num_active_planes = s->num_active_planes;
	calculate_bytes_to_fetch_required_to_hide_latency_params->num_of_dpp = mode_lib->mp.NoOfDPP;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_height_l = mode_lib->mp.meta_row_height;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_height_c = mode_lib->mp.meta_row_height_chroma;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_bytes_per_row_ub_l = s->meta_row_bytes_per_row_ub_l;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_bytes_per_row_ub_c = s->meta_row_bytes_per_row_ub_c;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_row_height_l = mode_lib->mp.dpte_row_height;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_row_height_c = mode_lib->mp.dpte_row_height_chroma;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_bytes_per_row_l = s->dpte_row_bytes_per_row_l;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_bytes_per_row_c = s->dpte_row_bytes_per_row_c;
	calculate_bytes_to_fetch_required_to_hide_latency_params->byte_per_pix_l = mode_lib->mp.BytePerPixelY;
	calculate_bytes_to_fetch_required_to_hide_latency_params->byte_per_pix_c = mode_lib->mp.BytePerPixelC;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_width_l = mode_lib->mp.SwathWidthY;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_width_c = mode_lib->mp.SwathWidthC;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_height_l = mode_lib->mp.SwathHeightY;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_height_c = mode_lib->mp.SwathHeightC;
	calculate_bytes_to_fetch_required_to_hide_latency_params->latency_to_hide_us = mode_lib->soc.power_management_parameters.dram_clk_change_blackout_us;

	/* outputs */
	calculate_bytes_to_fetch_required_to_hide_latency_params->bytes_required_l = s->pstate_bytes_required_l;
	calculate_bytes_to_fetch_required_to_hide_latency_params->bytes_required_c = s->pstate_bytes_required_c;

	calculate_bytes_to_fetch_required_to_hide_latency(calculate_bytes_to_fetch_required_to_hide_latency_params);

	/* Excess VActive bandwidth required to fill DET */
	calculate_excess_vactive_bandwidth_required(
			display_cfg,
			s->num_active_planes,
			s->pstate_bytes_required_l,
			s->pstate_bytes_required_c,
			/* outputs */
			mode_lib->mp.excess_vactive_fill_bw_l,
			mode_lib->mp.excess_vactive_fill_bw_c);

	mode_lib->mp.UrgentLatency = CalculateUrgentLatency(
		mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_latency_us.base_latency_us,
		mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_latency_us.base_latency_pixel_vm_us,
		mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_latency_us.base_latency_vm_us,
		mode_lib->soc.do_urgent_latency_adjustment,
		mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_latency_us.scaling_factor_fclk_us,
		mode_lib->soc.qos_parameters.qos_params.dcn32x.urgent_latency_us.scaling_factor_mhz,
		mode_lib->mp.FabricClock,
		mode_lib->mp.uclk_freq_mhz,
		mode_lib->soc.qos_parameters.qos_type,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.per_uclk_dpm_params[mode_lib->mp.qos_param_index].urgent_ramp_uclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.df_qos_response_time_fclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.max_round_trip_to_furthest_cs_fclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.mall_overhead_fclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.umc_urgent_ramp_latency_margin,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.fabric_max_transport_latency_margin);

	mode_lib->mp.TripToMemory = CalculateTripToMemory(
		mode_lib->mp.UrgentLatency,
		mode_lib->mp.FabricClock,
		mode_lib->mp.uclk_freq_mhz,
		mode_lib->soc.qos_parameters.qos_type,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.per_uclk_dpm_params[mode_lib->mp.qos_param_index].trip_to_memory_uclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.max_round_trip_to_furthest_cs_fclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.mall_overhead_fclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.umc_max_latency_margin,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.fabric_max_transport_latency_margin);

	mode_lib->mp.TripToMemory = math_max2(mode_lib->mp.UrgentLatency, mode_lib->mp.TripToMemory);

	mode_lib->mp.MetaTripToMemory = CalculateMetaTripToMemory(
		mode_lib->mp.UrgentLatency,
		mode_lib->mp.FabricClock,
		mode_lib->mp.uclk_freq_mhz,
		mode_lib->soc.qos_parameters.qos_type,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.per_uclk_dpm_params[mode_lib->mp.qos_param_index].meta_trip_to_memory_uclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.meta_trip_adder_fclk_cycles,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.umc_max_latency_margin,
		mode_lib->soc.qos_parameters.qos_params.dcn4x.fabric_max_transport_latency_margin);

	for (k = 0; k < s->num_active_planes; ++k) {
		bool cursor_not_enough_urgent_latency_hiding = 0;
		s->line_times[k] = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total /
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);

		s->pixel_format[k] = display_cfg->plane_descriptors[k].pixel_format;

		s->lb_source_lines_l[k] = get_num_lb_source_lines(mode_lib->ip.max_line_buffer_lines, mode_lib->ip.line_buffer_size_bits,
															mode_lib->mp.NoOfDPP[k],
															display_cfg->plane_descriptors[k].composition.viewport.plane0.width,
															display_cfg->plane_descriptors[k].composition.viewport.plane0.height,
															display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio,
															display_cfg->plane_descriptors[k].composition.rotation_angle);

		s->lb_source_lines_c[k] = get_num_lb_source_lines(mode_lib->ip.max_line_buffer_lines, mode_lib->ip.line_buffer_size_bits,
															mode_lib->mp.NoOfDPP[k],
															display_cfg->plane_descriptors[k].composition.viewport.plane1.width,
															display_cfg->plane_descriptors[k].composition.viewport.plane1.height,
															display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio,
															display_cfg->plane_descriptors[k].composition.rotation_angle);

		if (display_cfg->plane_descriptors[k].cursor.num_cursors > 0) {
			calculate_cursor_req_attributes(
				display_cfg->plane_descriptors[k].cursor.cursor_width,
				display_cfg->plane_descriptors[k].cursor.cursor_bpp,

				// output
				&s->cursor_lines_per_chunk[k],
				&s->cursor_bytes_per_line[k],
				&s->cursor_bytes_per_chunk[k],
				&s->cursor_bytes[k]);

			calculate_cursor_urgent_burst_factor(
				mode_lib->ip.cursor_buffer_size,
				display_cfg->plane_descriptors[k].cursor.cursor_width,
				s->cursor_bytes_per_chunk[k],
				s->cursor_lines_per_chunk[k],
				s->line_times[k],
				mode_lib->mp.UrgentLatency,

				// output
				&mode_lib->mp.UrgentBurstFactorCursor[k],
				&cursor_not_enough_urgent_latency_hiding);
		}
		mode_lib->mp.UrgentBurstFactorCursorPre[k] = mode_lib->mp.UrgentBurstFactorCursor[k];

		CalculateUrgentBurstFactor(
			&display_cfg->plane_descriptors[k],
			mode_lib->mp.swath_width_luma_ub[k],
			mode_lib->mp.swath_width_chroma_ub[k],
			mode_lib->mp.SwathHeightY[k],
			mode_lib->mp.SwathHeightC[k],
			s->line_times[k],
			mode_lib->mp.UrgentLatency,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio,
			mode_lib->mp.BytePerPixelInDETY[k],
			mode_lib->mp.BytePerPixelInDETC[k],
			mode_lib->mp.DETBufferSizeY[k],
			mode_lib->mp.DETBufferSizeC[k],

			/* output */
			&mode_lib->mp.UrgentBurstFactorLuma[k],
			&mode_lib->mp.UrgentBurstFactorChroma[k],
			&mode_lib->mp.NotEnoughUrgentLatencyHiding[k]);

		mode_lib->mp.NotEnoughUrgentLatencyHiding[k] = mode_lib->mp.NotEnoughUrgentLatencyHiding[k] || cursor_not_enough_urgent_latency_hiding;
	}

	for (k = 0; k < s->num_active_planes; ++k) {
		s->MaxVStartupLines[k] = CalculateMaxVStartup(
			mode_lib->ip.ptoi_supported,
			mode_lib->ip.vblank_nom_default_us,
			&display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing,
			mode_lib->mp.WritebackDelay[k]);

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: k=%u MaxVStartupLines = %u\n", __func__, k, s->MaxVStartupLines[k]);
		dml2_printf("DML::%s: k=%u WritebackDelay = %f\n", __func__, k, mode_lib->mp.WritebackDelay[k]);
#endif
	}

	s->immediate_flip_required = false;
	for (k = 0; k < s->num_active_planes; ++k) {
		s->immediate_flip_required = s->immediate_flip_required || display_cfg->plane_descriptors[k].immediate_flip;
	}
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: immediate_flip_required = %u\n", __func__, s->immediate_flip_required);
#endif

	if (s->num_active_planes > 1) {
		CheckGlobalPrefetchAdmissibility_params->num_active_planes =  s->num_active_planes;
		CheckGlobalPrefetchAdmissibility_params->pixel_format = s->pixel_format;
		CheckGlobalPrefetchAdmissibility_params->chunk_bytes_l = mode_lib->ip.pixel_chunk_size_kbytes * 1024;
		CheckGlobalPrefetchAdmissibility_params->chunk_bytes_c = mode_lib->ip.pixel_chunk_size_kbytes * 1024;
		CheckGlobalPrefetchAdmissibility_params->lb_source_lines_l = s->lb_source_lines_l;
		CheckGlobalPrefetchAdmissibility_params->lb_source_lines_c = s->lb_source_lines_c;
		CheckGlobalPrefetchAdmissibility_params->swath_height_l =  mode_lib->mp.SwathHeightY;
		CheckGlobalPrefetchAdmissibility_params->swath_height_c =  mode_lib->mp.SwathHeightC;
		CheckGlobalPrefetchAdmissibility_params->rob_buffer_size_kbytes = mode_lib->ip.rob_buffer_size_kbytes;
		CheckGlobalPrefetchAdmissibility_params->compressed_buffer_size_kbytes = mode_lib->mp.CompressedBufferSizeInkByte;
		CheckGlobalPrefetchAdmissibility_params->detile_buffer_size_bytes_l = mode_lib->mp.DETBufferSizeY;
		CheckGlobalPrefetchAdmissibility_params->detile_buffer_size_bytes_c = mode_lib->mp.DETBufferSizeC;
		CheckGlobalPrefetchAdmissibility_params->full_swath_bytes_l = s->full_swath_bytes_l;
		CheckGlobalPrefetchAdmissibility_params->full_swath_bytes_c = s->full_swath_bytes_c;
		CheckGlobalPrefetchAdmissibility_params->prefetch_sw_bytes = s->prefetch_sw_bytes;
		CheckGlobalPrefetchAdmissibility_params->Tpre_rounded = 0; // don't care
		CheckGlobalPrefetchAdmissibility_params->Tpre_oto = 0; // don't care
		CheckGlobalPrefetchAdmissibility_params->estimated_urg_bandwidth_required_mbps = mode_lib->mp.urg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp];
		CheckGlobalPrefetchAdmissibility_params->estimated_dcfclk_mhz = mode_lib->mp.Dcfclk;
		CheckGlobalPrefetchAdmissibility_params->line_time = s->line_times;
		CheckGlobalPrefetchAdmissibility_params->dst_y_prefetch = mode_lib->mp.dst_y_prefetch;

		// if recalc_prefetch_schedule is set, recalculate the prefetch schedule with the new impacted_Tpre, prefetch should be possible
		CheckGlobalPrefetchAdmissibility_params->recalc_prefetch_schedule = &s->dummy_boolean[0];
		CheckGlobalPrefetchAdmissibility_params->impacted_dst_y_pre = s->impacted_dst_y_pre;
		CheckGlobalPrefetchAdmissibility(&mode_lib->scratch, CheckGlobalPrefetchAdmissibility_params); // dont care about the check output for mode programming
	}

	{
		s->DestinationLineTimesForPrefetchLessThan2 = false;
		s->VRatioPrefetchMoreThanMax = false;

		dml2_printf("DML::%s: Start one iteration of prefetch schedule evaluation\n", __func__);

		for (k = 0; k < s->num_active_planes; ++k) {
			struct dml2_core_internal_DmlPipe *myPipe = &s->myPipe;

			dml2_printf("DML::%s: k=%d MaxVStartupLines = %u\n", __func__, k, s->MaxVStartupLines[k]);
			mode_lib->mp.TWait[k] = CalculateTWait(
					display_cfg->plane_descriptors[k].overrides.reserved_vblank_time_ns,
					mode_lib->mp.UrgentLatency,
					mode_lib->mp.TripToMemory,
					!dml_is_phantom_pipe(&display_cfg->plane_descriptors[k]) && display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.drr_config.enabled ?
					get_g6_temp_read_blackout_us(&mode_lib->soc, (unsigned int)(mode_lib->mp.uclk_freq_mhz * 1000), in_out_params->min_clk_index) : 0.0);

			myPipe->Dppclk = mode_lib->mp.Dppclk[k];
			myPipe->Dispclk = mode_lib->mp.Dispclk;
			myPipe->PixelClock = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
			myPipe->DCFClkDeepSleep = mode_lib->mp.DCFCLKDeepSleep;
			myPipe->DPPPerSurface = mode_lib->mp.NoOfDPP[k];
			myPipe->ScalerEnabled = display_cfg->plane_descriptors[k].composition.scaler_info.enabled;
			myPipe->VRatio = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
			myPipe->VRatioChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
			myPipe->VTaps = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps;
			myPipe->VTapsChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps;
			myPipe->RotationAngle = display_cfg->plane_descriptors[k].composition.rotation_angle;
			myPipe->mirrored = display_cfg->plane_descriptors[k].composition.mirrored;
			myPipe->BlockWidth256BytesY = mode_lib->mp.Read256BlockWidthY[k];
			myPipe->BlockHeight256BytesY = mode_lib->mp.Read256BlockHeightY[k];
			myPipe->BlockWidth256BytesC = mode_lib->mp.Read256BlockWidthC[k];
			myPipe->BlockHeight256BytesC = mode_lib->mp.Read256BlockHeightC[k];
			myPipe->InterlaceEnable = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced;
			myPipe->NumberOfCursors = display_cfg->plane_descriptors[k].cursor.num_cursors;
			myPipe->VBlank = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_active;
			myPipe->HTotal = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total;
			myPipe->HActive = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active;
			myPipe->DCCEnable = display_cfg->plane_descriptors[k].surface.dcc.enable;
			myPipe->ODMMode = mode_lib->mp.ODMMode[k];
			myPipe->SourcePixelFormat = display_cfg->plane_descriptors[k].pixel_format;
			myPipe->BytePerPixelY = mode_lib->mp.BytePerPixelY[k];
			myPipe->BytePerPixelC = mode_lib->mp.BytePerPixelC[k];
			myPipe->ProgressiveToInterlaceUnitInOPP = mode_lib->ip.ptoi_supported;

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: Calling CalculatePrefetchSchedule for k=%u\n", __func__, k);
#endif
			CalculatePrefetchSchedule_params->display_cfg = display_cfg;
			CalculatePrefetchSchedule_params->HostVMInefficiencyFactor = s->HostVMInefficiencyFactorPrefetch;
			CalculatePrefetchSchedule_params->myPipe = myPipe;
			CalculatePrefetchSchedule_params->DSCDelay = mode_lib->mp.DSCDelay[k];
			CalculatePrefetchSchedule_params->DPPCLKDelaySubtotalPlusCNVCFormater = mode_lib->ip.dppclk_delay_subtotal + mode_lib->ip.dppclk_delay_cnvc_formatter;
			CalculatePrefetchSchedule_params->DPPCLKDelaySCL = mode_lib->ip.dppclk_delay_scl;
			CalculatePrefetchSchedule_params->DPPCLKDelaySCLLBOnly = mode_lib->ip.dppclk_delay_scl_lb_only;
			CalculatePrefetchSchedule_params->DPPCLKDelayCNVCCursor = mode_lib->ip.dppclk_delay_cnvc_cursor;
			CalculatePrefetchSchedule_params->DISPCLKDelaySubtotal = mode_lib->ip.dispclk_delay_subtotal;
			CalculatePrefetchSchedule_params->DPP_RECOUT_WIDTH = (unsigned int)(mode_lib->mp.SwathWidthY[k] / display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio);
			CalculatePrefetchSchedule_params->OutputFormat = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format;
			CalculatePrefetchSchedule_params->MaxInterDCNTileRepeaters = mode_lib->ip.max_inter_dcn_tile_repeaters;
			CalculatePrefetchSchedule_params->VStartup = s->MaxVStartupLines[k];
			CalculatePrefetchSchedule_params->HostVMMinPageSize = mode_lib->soc.hostvm_min_page_size_kbytes;
			CalculatePrefetchSchedule_params->DynamicMetadataEnable = display_cfg->plane_descriptors[k].dynamic_meta_data.enable;
			CalculatePrefetchSchedule_params->DynamicMetadataVMEnabled = mode_lib->ip.dynamic_metadata_vm_enabled;
			CalculatePrefetchSchedule_params->DynamicMetadataLinesBeforeActiveRequired = display_cfg->plane_descriptors[k].dynamic_meta_data.lines_before_active_required;
			CalculatePrefetchSchedule_params->DynamicMetadataTransmittedBytes = display_cfg->plane_descriptors[k].dynamic_meta_data.transmitted_bytes;
			CalculatePrefetchSchedule_params->UrgentLatency = mode_lib->mp.UrgentLatency;
			CalculatePrefetchSchedule_params->ExtraLatencyPrefetch = mode_lib->mp.ExtraLatencyPrefetch;
			CalculatePrefetchSchedule_params->TCalc = mode_lib->mp.TCalc;
			CalculatePrefetchSchedule_params->vm_bytes = mode_lib->mp.vm_bytes[k];
			CalculatePrefetchSchedule_params->PixelPTEBytesPerRow = mode_lib->mp.PixelPTEBytesPerRow[k];
			CalculatePrefetchSchedule_params->PrefetchSourceLinesY = mode_lib->mp.PrefetchSourceLinesY[k];
			CalculatePrefetchSchedule_params->VInitPreFillY = mode_lib->mp.VInitPreFillY[k];
			CalculatePrefetchSchedule_params->MaxNumSwathY = mode_lib->mp.MaxNumSwathY[k];
			CalculatePrefetchSchedule_params->PrefetchSourceLinesC = mode_lib->mp.PrefetchSourceLinesC[k];
			CalculatePrefetchSchedule_params->VInitPreFillC = mode_lib->mp.VInitPreFillC[k];
			CalculatePrefetchSchedule_params->MaxNumSwathC = mode_lib->mp.MaxNumSwathC[k];
			CalculatePrefetchSchedule_params->swath_width_luma_ub = mode_lib->mp.swath_width_luma_ub[k];
			CalculatePrefetchSchedule_params->swath_width_chroma_ub = mode_lib->mp.swath_width_chroma_ub[k];
			CalculatePrefetchSchedule_params->SwathHeightY = mode_lib->mp.SwathHeightY[k];
			CalculatePrefetchSchedule_params->SwathHeightC = mode_lib->mp.SwathHeightC[k];
			CalculatePrefetchSchedule_params->TWait = mode_lib->mp.TWait[k];
			CalculatePrefetchSchedule_params->Ttrip = mode_lib->mp.TripToMemory;
			CalculatePrefetchSchedule_params->Turg = mode_lib->mp.UrgentLatency;
			CalculatePrefetchSchedule_params->setup_for_tdlut = display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut;
			CalculatePrefetchSchedule_params->tdlut_pte_bytes_per_frame = s->tdlut_pte_bytes_per_frame[k];
			CalculatePrefetchSchedule_params->tdlut_bytes_per_frame = s->tdlut_bytes_per_frame[k];
			CalculatePrefetchSchedule_params->tdlut_opt_time = s->tdlut_opt_time[k];
			CalculatePrefetchSchedule_params->tdlut_drain_time = s->tdlut_drain_time[k];
			CalculatePrefetchSchedule_params->num_cursors = (display_cfg->plane_descriptors[k].cursor.cursor_width > 0);
			CalculatePrefetchSchedule_params->cursor_bytes_per_chunk = s->cursor_bytes_per_chunk[k];
			CalculatePrefetchSchedule_params->cursor_bytes_per_line = s->cursor_bytes_per_line[k];
			CalculatePrefetchSchedule_params->dcc_enable = display_cfg->plane_descriptors[k].surface.dcc.enable;
			CalculatePrefetchSchedule_params->mrq_present = mode_lib->ip.dcn_mrq_present;
			CalculatePrefetchSchedule_params->meta_row_bytes = mode_lib->mp.meta_row_bytes[k];
			CalculatePrefetchSchedule_params->mall_prefetch_sdp_overhead_factor = mode_lib->mp.mall_prefetch_sdp_overhead_factor[k];
			CalculatePrefetchSchedule_params->impacted_dst_y_pre = s->impacted_dst_y_pre[k];
			CalculatePrefetchSchedule_params->vactive_sw_bw_l = mode_lib->mp.vactive_sw_bw_l[k];
			CalculatePrefetchSchedule_params->vactive_sw_bw_c = mode_lib->mp.vactive_sw_bw_c[k];

			// output
			CalculatePrefetchSchedule_params->DSTXAfterScaler = &mode_lib->mp.DSTXAfterScaler[k];
			CalculatePrefetchSchedule_params->DSTYAfterScaler = &mode_lib->mp.DSTYAfterScaler[k];
			CalculatePrefetchSchedule_params->dst_y_prefetch = &mode_lib->mp.dst_y_prefetch[k];
			CalculatePrefetchSchedule_params->dst_y_per_vm_vblank = &mode_lib->mp.dst_y_per_vm_vblank[k];
			CalculatePrefetchSchedule_params->dst_y_per_row_vblank = &mode_lib->mp.dst_y_per_row_vblank[k];
			CalculatePrefetchSchedule_params->VRatioPrefetchY = &mode_lib->mp.VRatioPrefetchY[k];
			CalculatePrefetchSchedule_params->VRatioPrefetchC = &mode_lib->mp.VRatioPrefetchC[k];
			CalculatePrefetchSchedule_params->RequiredPrefetchPixelDataBWLuma = &mode_lib->mp.RequiredPrefetchPixelDataBWLuma[k];
			CalculatePrefetchSchedule_params->RequiredPrefetchPixelDataBWChroma = &mode_lib->mp.RequiredPrefetchPixelDataBWChroma[k];
			CalculatePrefetchSchedule_params->NotEnoughTimeForDynamicMetadata = &mode_lib->mp.NotEnoughTimeForDynamicMetadata[k];
			CalculatePrefetchSchedule_params->Tno_bw = &mode_lib->mp.Tno_bw[k];
			CalculatePrefetchSchedule_params->Tno_bw_flip = &mode_lib->mp.Tno_bw_flip[k];
			CalculatePrefetchSchedule_params->prefetch_vmrow_bw = &mode_lib->mp.prefetch_vmrow_bw[k];
			CalculatePrefetchSchedule_params->Tdmdl_vm = &mode_lib->mp.Tdmdl_vm[k];
			CalculatePrefetchSchedule_params->Tdmdl = &mode_lib->mp.Tdmdl[k];
			CalculatePrefetchSchedule_params->TSetup = &mode_lib->mp.TSetup[k];
			CalculatePrefetchSchedule_params->Tvm_trips = &s->Tvm_trips[k];
			CalculatePrefetchSchedule_params->Tr0_trips = &s->Tr0_trips[k];
			CalculatePrefetchSchedule_params->Tvm_trips_flip = &s->Tvm_trips_flip[k];
			CalculatePrefetchSchedule_params->Tr0_trips_flip = &s->Tr0_trips_flip[k];
			CalculatePrefetchSchedule_params->Tvm_trips_flip_rounded = &s->Tvm_trips_flip_rounded[k];
			CalculatePrefetchSchedule_params->Tr0_trips_flip_rounded = &s->Tr0_trips_flip_rounded[k];
			CalculatePrefetchSchedule_params->VUpdateOffsetPix = &mode_lib->mp.VUpdateOffsetPix[k];
			CalculatePrefetchSchedule_params->VUpdateWidthPix = &mode_lib->mp.VUpdateWidthPix[k];
			CalculatePrefetchSchedule_params->VReadyOffsetPix = &mode_lib->mp.VReadyOffsetPix[k];
			CalculatePrefetchSchedule_params->prefetch_cursor_bw = &mode_lib->mp.prefetch_cursor_bw[k];
			CalculatePrefetchSchedule_params->prefetch_sw_bytes = &s->prefetch_sw_bytes[k];
			CalculatePrefetchSchedule_params->Tpre_rounded = &s->Tpre_rounded[k];
			CalculatePrefetchSchedule_params->Tpre_oto = &s->Tpre_oto[k];

			mode_lib->mp.NoTimeToPrefetch[k] = CalculatePrefetchSchedule(&mode_lib->scratch, CalculatePrefetchSchedule_params);

			if (s->impacted_dst_y_pre[k] > 0)
				mode_lib->mp.impacted_prefetch_margin_us[k] = (mode_lib->mp.dst_y_prefetch[k] - s->impacted_dst_y_pre[k]) * s->line_times[k];
			else
				mode_lib->mp.impacted_prefetch_margin_us[k] = 0;

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%0u NoTimeToPrefetch=%0d\n", __func__, k, mode_lib->mp.NoTimeToPrefetch[k]);
#endif
			mode_lib->mp.VStartupMin[k] = s->MaxVStartupLines[k];
		} // for k

		mode_lib->mp.PrefetchModeSupported = true;
		for (k = 0; k < s->num_active_planes; ++k) {
			if (mode_lib->mp.NoTimeToPrefetch[k] == true ||
				mode_lib->mp.NotEnoughTimeForDynamicMetadata[k] ||
				mode_lib->mp.DSTYAfterScaler[k] > 8) {
				dml2_printf("DML::%s: k=%u, NoTimeToPrefetch = %0d\n", __func__, k, mode_lib->mp.NoTimeToPrefetch[k]);
				dml2_printf("DML::%s: k=%u, NotEnoughTimeForDynamicMetadata=%u\n", __func__, k, mode_lib->mp.NotEnoughTimeForDynamicMetadata[k]);
				dml2_printf("DML::%s: k=%u, DSTYAfterScaler=%u (should be <= 0)\n", __func__, k, mode_lib->mp.DSTYAfterScaler[k]);
				mode_lib->mp.PrefetchModeSupported = false;
			}
			if (mode_lib->mp.dst_y_prefetch[k] < 2)
				s->DestinationLineTimesForPrefetchLessThan2 = true;

			if (mode_lib->mp.VRatioPrefetchY[k] > __DML2_CALCS_MAX_VRATIO_PRE__ ||
				mode_lib->mp.VRatioPrefetchC[k] > __DML2_CALCS_MAX_VRATIO_PRE__) {
				s->VRatioPrefetchMoreThanMax = true;
				dml2_printf("DML::%s: k=%d, VRatioPrefetchY=%f (should not be < %f)\n", __func__, k, mode_lib->mp.VRatioPrefetchY[k], __DML2_CALCS_MAX_VRATIO_PRE__);
				dml2_printf("DML::%s: k=%d, VRatioPrefetchC=%f (should not be < %f)\n", __func__, k, mode_lib->mp.VRatioPrefetchC[k], __DML2_CALCS_MAX_VRATIO_PRE__);
				dml2_printf("DML::%s: VRatioPrefetchMoreThanMax = %u\n", __func__, s->VRatioPrefetchMoreThanMax);
			}

			if (mode_lib->mp.NotEnoughUrgentLatencyHiding[k]) {
				dml2_printf("DML::%s: k=%u, NotEnoughUrgentLatencyHiding = %u\n", __func__, k, mode_lib->mp.NotEnoughUrgentLatencyHiding[k]);
				mode_lib->mp.PrefetchModeSupported = false;
			}
		}

		if (s->VRatioPrefetchMoreThanMax == true || s->DestinationLineTimesForPrefetchLessThan2 == true) {
			dml2_printf("DML::%s: VRatioPrefetchMoreThanMax = %u\n", __func__, s->VRatioPrefetchMoreThanMax);
			dml2_printf("DML::%s: DestinationLineTimesForPrefetchLessThan2 = %u\n", __func__, s->DestinationLineTimesForPrefetchLessThan2);
			mode_lib->mp.PrefetchModeSupported = false;
		}

		dml2_printf("DML::%s: Prefetch schedule is %sOK at vstartup = %u\n", __func__,
			mode_lib->mp.PrefetchModeSupported ? "" : "NOT ", CalculatePrefetchSchedule_params->VStartup);

		// Prefetch schedule OK, now check prefetch bw
		if (mode_lib->mp.PrefetchModeSupported == true) {
			for (k = 0; k < s->num_active_planes; ++k) {
				double line_time_us = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total /
					((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
				CalculateUrgentBurstFactor(
					&display_cfg->plane_descriptors[k],
					mode_lib->mp.swath_width_luma_ub[k],
					mode_lib->mp.swath_width_chroma_ub[k],
					mode_lib->mp.SwathHeightY[k],
					mode_lib->mp.SwathHeightC[k],
					line_time_us,
					mode_lib->mp.UrgentLatency,
					mode_lib->mp.VRatioPrefetchY[k],
					mode_lib->mp.VRatioPrefetchC[k],
					mode_lib->mp.BytePerPixelInDETY[k],
					mode_lib->mp.BytePerPixelInDETC[k],
					mode_lib->mp.DETBufferSizeY[k],
					mode_lib->mp.DETBufferSizeC[k],
					/* Output */
					&mode_lib->mp.UrgentBurstFactorLumaPre[k],
					&mode_lib->mp.UrgentBurstFactorChromaPre[k],
					&mode_lib->mp.NotEnoughUrgentLatencyHidingPre[k]);

#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: k=%0u DPPPerSurface=%u\n", __func__, k, mode_lib->mp.NoOfDPP[k]);
				dml2_printf("DML::%s: k=%0u UrgentBurstFactorLuma=%f\n", __func__, k, mode_lib->mp.UrgentBurstFactorLuma[k]);
				dml2_printf("DML::%s: k=%0u UrgentBurstFactorChroma=%f\n", __func__, k, mode_lib->mp.UrgentBurstFactorChroma[k]);
				dml2_printf("DML::%s: k=%0u UrgentBurstFactorLumaPre=%f\n", __func__, k, mode_lib->mp.UrgentBurstFactorLumaPre[k]);
				dml2_printf("DML::%s: k=%0u UrgentBurstFactorChromaPre=%f\n", __func__, k, mode_lib->mp.UrgentBurstFactorChromaPre[k]);

				dml2_printf("DML::%s: k=%0u VRatioPrefetchY=%f\n", __func__, k, mode_lib->mp.VRatioPrefetchY[k]);
				dml2_printf("DML::%s: k=%0u VRatioY=%f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio);

				dml2_printf("DML::%s: k=%0u prefetch_vmrow_bw=%f\n", __func__, k, mode_lib->mp.prefetch_vmrow_bw[k]);
				dml2_printf("DML::%s: k=%0u vactive_sw_bw_l=%f\n", __func__, k, mode_lib->mp.vactive_sw_bw_l[k]);
				dml2_printf("DML::%s: k=%0u vactive_sw_bw_c=%f\n", __func__, k, mode_lib->mp.vactive_sw_bw_c[k]);
				dml2_printf("DML::%s: k=%0u cursor_bw=%f\n", __func__, k, mode_lib->mp.cursor_bw[k]);
				dml2_printf("DML::%s: k=%0u dpte_row_bw=%f\n", __func__, k, mode_lib->mp.dpte_row_bw[k]);
				dml2_printf("DML::%s: k=%0u meta_row_bw=%f\n", __func__, k, mode_lib->mp.meta_row_bw[k]);
				dml2_printf("DML::%s: k=%0u RequiredPrefetchPixelDataBWLuma=%f\n", __func__, k, mode_lib->mp.RequiredPrefetchPixelDataBWLuma[k]);
				dml2_printf("DML::%s: k=%0u RequiredPrefetchPixelDataBWChroma=%f\n", __func__, k, mode_lib->mp.RequiredPrefetchPixelDataBWChroma[k]);
				dml2_printf("DML::%s: k=%0u prefetch_cursor_bw=%f\n", __func__, k, mode_lib->mp.prefetch_cursor_bw[k]);
#endif
			}

			for (k = 0; k <= s->num_active_planes - 1; k++)
				mode_lib->mp.final_flip_bw[k] = 0;

			calculate_peak_bandwidth_params->urg_vactive_bandwidth_required = mode_lib->mp.urg_vactive_bandwidth_required;
			calculate_peak_bandwidth_params->urg_bandwidth_required = mode_lib->mp.urg_bandwidth_required;
			calculate_peak_bandwidth_params->urg_bandwidth_required_qual = mode_lib->mp.urg_bandwidth_required_qual;
			calculate_peak_bandwidth_params->non_urg_bandwidth_required = mode_lib->mp.non_urg_bandwidth_required;
			calculate_peak_bandwidth_params->surface_avg_vactive_required_bw = s->surface_dummy_bw;
			calculate_peak_bandwidth_params->surface_peak_required_bw = s->surface_dummy_bw0;

			calculate_peak_bandwidth_params->display_cfg = display_cfg;
			calculate_peak_bandwidth_params->inc_flip_bw = 0;
			calculate_peak_bandwidth_params->num_active_planes = s->num_active_planes;
			calculate_peak_bandwidth_params->num_of_dpp = mode_lib->mp.NoOfDPP;
			calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p0 = mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p0;
			calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p1 = mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p1;
			calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p0 = mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p0;
			calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p1 = mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p1;
			calculate_peak_bandwidth_params->mall_prefetch_sdp_overhead_factor = mode_lib->mp.mall_prefetch_sdp_overhead_factor;
			calculate_peak_bandwidth_params->mall_prefetch_dram_overhead_factor = mode_lib->mp.mall_prefetch_dram_overhead_factor;

			calculate_peak_bandwidth_params->surface_read_bandwidth_l = mode_lib->mp.vactive_sw_bw_l;
			calculate_peak_bandwidth_params->surface_read_bandwidth_c = mode_lib->mp.vactive_sw_bw_c;
			calculate_peak_bandwidth_params->prefetch_bandwidth_l = mode_lib->mp.RequiredPrefetchPixelDataBWLuma;
			calculate_peak_bandwidth_params->prefetch_bandwidth_c = mode_lib->mp.RequiredPrefetchPixelDataBWChroma;
			calculate_peak_bandwidth_params->excess_vactive_fill_bw_l = mode_lib->mp.excess_vactive_fill_bw_l;
			calculate_peak_bandwidth_params->excess_vactive_fill_bw_c = mode_lib->mp.excess_vactive_fill_bw_c;
			calculate_peak_bandwidth_params->cursor_bw = mode_lib->mp.cursor_bw;
			calculate_peak_bandwidth_params->dpte_row_bw = mode_lib->mp.dpte_row_bw;
			calculate_peak_bandwidth_params->meta_row_bw = mode_lib->mp.meta_row_bw;
			calculate_peak_bandwidth_params->prefetch_cursor_bw = mode_lib->mp.prefetch_cursor_bw;
			calculate_peak_bandwidth_params->prefetch_vmrow_bw = mode_lib->mp.prefetch_vmrow_bw;
			calculate_peak_bandwidth_params->flip_bw = mode_lib->mp.final_flip_bw;
			calculate_peak_bandwidth_params->urgent_burst_factor_l = mode_lib->mp.UrgentBurstFactorLuma;
			calculate_peak_bandwidth_params->urgent_burst_factor_c = mode_lib->mp.UrgentBurstFactorChroma;
			calculate_peak_bandwidth_params->urgent_burst_factor_cursor = mode_lib->mp.UrgentBurstFactorCursor;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_l = mode_lib->mp.UrgentBurstFactorLumaPre;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_c = mode_lib->mp.UrgentBurstFactorChromaPre;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_cursor = mode_lib->mp.UrgentBurstFactorCursorPre;

			calculate_peak_bandwidth_required(
					&mode_lib->scratch,
					calculate_peak_bandwidth_params);

			// Check urg peak bandwidth against available urg bw
			// check at SDP and DRAM, for all soc states (SVP prefetch an Sys Active)
			check_urgent_bandwidth_support(
				&mode_lib->mp.FractionOfUrgentBandwidth, // double* frac_urg_bandwidth
				&mode_lib->mp.FractionOfUrgentBandwidthMALL, // double* frac_urg_bandwidth_mall
				&s->dummy_boolean[1], // vactive bw ok
				&mode_lib->mp.PrefetchModeSupported, // prefetch bw ok

				mode_lib->soc.mall_allocated_for_dcn_mbytes,
				mode_lib->mp.non_urg_bandwidth_required,
				mode_lib->mp.urg_vactive_bandwidth_required,
				mode_lib->mp.urg_bandwidth_required,
				mode_lib->mp.urg_bandwidth_available);

			if (!mode_lib->mp.PrefetchModeSupported)
				dml2_printf("DML::%s: Bandwidth not sufficient for prefetch!\n", __func__);

			for (k = 0; k < s->num_active_planes; ++k) {
				if (mode_lib->mp.NotEnoughUrgentLatencyHidingPre[k]) {
					dml2_printf("DML::%s: k=%u, NotEnoughUrgentLatencyHidingPre = %u\n", __func__, k, mode_lib->mp.NotEnoughUrgentLatencyHidingPre[k]);
					mode_lib->mp.PrefetchModeSupported = false;
				}
			}
		} // prefetch schedule ok

		// Prefetch schedule and prefetch bw ok, now check flip bw
		if (mode_lib->mp.PrefetchModeSupported == true) { // prefetch schedule and prefetch bw ok, now check flip bw

			mode_lib->mp.BandwidthAvailableForImmediateFlip =
				get_bandwidth_available_for_immediate_flip(
					dml2_core_internal_soc_state_sys_active,
					mode_lib->mp.urg_bandwidth_required_qual, // no flip
					mode_lib->mp.urg_bandwidth_available);
			mode_lib->mp.TotImmediateFlipBytes = 0;
			for (k = 0; k < s->num_active_planes; ++k) {
				if (display_cfg->plane_descriptors[k].immediate_flip) {
					s->per_pipe_flip_bytes[k] =  get_pipe_flip_bytes(s->HostVMInefficiencyFactor,
											mode_lib->mp.vm_bytes[k],
											mode_lib->mp.PixelPTEBytesPerRow[k],
											mode_lib->mp.meta_row_bytes[k]);
				} else {
					s->per_pipe_flip_bytes[k] = 0;
				}
				mode_lib->mp.TotImmediateFlipBytes += s->per_pipe_flip_bytes[k] * mode_lib->mp.NoOfDPP[k];
#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML::%s: k = %u\n", __func__, k);
				dml2_printf("DML::%s: DPPPerSurface = %u\n", __func__, mode_lib->mp.NoOfDPP[k]);
				dml2_printf("DML::%s: vm_bytes = %u\n", __func__, mode_lib->mp.vm_bytes[k]);
				dml2_printf("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, mode_lib->mp.PixelPTEBytesPerRow[k]);
				dml2_printf("DML::%s: meta_row_bytes = %u\n", __func__, mode_lib->mp.meta_row_bytes[k]);
				dml2_printf("DML::%s: TotImmediateFlipBytes = %u\n", __func__, mode_lib->mp.TotImmediateFlipBytes);
#endif
			}
			for (k = 0; k < s->num_active_planes; ++k) {
				CalculateFlipSchedule(
					&mode_lib->scratch,
					display_cfg->plane_descriptors[k].immediate_flip,
					0, // use_lb_flip_bw
					s->HostVMInefficiencyFactor,
					s->Tvm_trips_flip[k],
					s->Tr0_trips_flip[k],
					s->Tvm_trips_flip_rounded[k],
					s->Tr0_trips_flip_rounded[k],
					display_cfg->gpuvm_enable,
					mode_lib->mp.vm_bytes[k],
					mode_lib->mp.PixelPTEBytesPerRow[k],
					mode_lib->mp.BandwidthAvailableForImmediateFlip,
					mode_lib->mp.TotImmediateFlipBytes,
					display_cfg->plane_descriptors[k].pixel_format,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000),
					display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio,
					display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio,
					mode_lib->mp.Tno_bw[k],
					mode_lib->mp.dpte_row_height[k],
					mode_lib->mp.dpte_row_height_chroma[k],
					mode_lib->mp.use_one_row_for_frame_flip[k],
					mode_lib->ip.max_flip_time_us,
					mode_lib->ip.max_flip_time_lines,
					s->per_pipe_flip_bytes[k],
					mode_lib->mp.meta_row_bytes[k],
					mode_lib->mp.meta_row_height[k],
					mode_lib->mp.meta_row_height_chroma[k],
					mode_lib->ip.dcn_mrq_present && display_cfg->plane_descriptors[k].surface.dcc.enable,

					// Output
					&mode_lib->mp.dst_y_per_vm_flip[k],
					&mode_lib->mp.dst_y_per_row_flip[k],
					&mode_lib->mp.final_flip_bw[k],
					&mode_lib->mp.ImmediateFlipSupportedForPipe[k]);
			}

			calculate_peak_bandwidth_params->urg_vactive_bandwidth_required = s->dummy_bw;
			calculate_peak_bandwidth_params->urg_bandwidth_required = mode_lib->mp.urg_bandwidth_required_flip;
			calculate_peak_bandwidth_params->urg_bandwidth_required_qual = s->dummy_bw;
			calculate_peak_bandwidth_params->non_urg_bandwidth_required = mode_lib->mp.non_urg_bandwidth_required_flip;
			calculate_peak_bandwidth_params->surface_avg_vactive_required_bw = s->surface_dummy_bw;
			calculate_peak_bandwidth_params->surface_peak_required_bw = s->surface_dummy_bw0;

			calculate_peak_bandwidth_params->display_cfg = display_cfg;
			calculate_peak_bandwidth_params->inc_flip_bw = 1;
			calculate_peak_bandwidth_params->num_active_planes = s->num_active_planes;
			calculate_peak_bandwidth_params->num_of_dpp = mode_lib->mp.NoOfDPP;
			calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p0 = mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p0;
			calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p1 = mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p1;
			calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p0 = mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p0;
			calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p1 = mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p1;
			calculate_peak_bandwidth_params->mall_prefetch_sdp_overhead_factor = mode_lib->mp.mall_prefetch_sdp_overhead_factor;
			calculate_peak_bandwidth_params->mall_prefetch_dram_overhead_factor = mode_lib->mp.mall_prefetch_dram_overhead_factor;

			calculate_peak_bandwidth_params->surface_read_bandwidth_l = mode_lib->mp.vactive_sw_bw_l;
			calculate_peak_bandwidth_params->surface_read_bandwidth_c = mode_lib->mp.vactive_sw_bw_c;
			calculate_peak_bandwidth_params->prefetch_bandwidth_l = mode_lib->mp.RequiredPrefetchPixelDataBWLuma;
			calculate_peak_bandwidth_params->prefetch_bandwidth_c = mode_lib->mp.RequiredPrefetchPixelDataBWChroma;
			calculate_peak_bandwidth_params->excess_vactive_fill_bw_l = mode_lib->mp.excess_vactive_fill_bw_l;
			calculate_peak_bandwidth_params->excess_vactive_fill_bw_c = mode_lib->mp.excess_vactive_fill_bw_c;
			calculate_peak_bandwidth_params->cursor_bw = mode_lib->mp.cursor_bw;
			calculate_peak_bandwidth_params->dpte_row_bw = mode_lib->mp.dpte_row_bw;
			calculate_peak_bandwidth_params->meta_row_bw = mode_lib->mp.meta_row_bw;
			calculate_peak_bandwidth_params->prefetch_cursor_bw = mode_lib->mp.prefetch_cursor_bw;
			calculate_peak_bandwidth_params->prefetch_vmrow_bw = mode_lib->mp.prefetch_vmrow_bw;
			calculate_peak_bandwidth_params->flip_bw = mode_lib->mp.final_flip_bw;
			calculate_peak_bandwidth_params->urgent_burst_factor_l = mode_lib->mp.UrgentBurstFactorLuma;
			calculate_peak_bandwidth_params->urgent_burst_factor_c = mode_lib->mp.UrgentBurstFactorChroma;
			calculate_peak_bandwidth_params->urgent_burst_factor_cursor = mode_lib->mp.UrgentBurstFactorCursor;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_l = mode_lib->mp.UrgentBurstFactorLumaPre;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_c = mode_lib->mp.UrgentBurstFactorChromaPre;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_cursor = mode_lib->mp.UrgentBurstFactorCursorPre;

			calculate_peak_bandwidth_required(
					&mode_lib->scratch,
					calculate_peak_bandwidth_params);

			calculate_immediate_flip_bandwidth_support(
				&mode_lib->mp.FractionOfUrgentBandwidthImmediateFlip, // double* frac_urg_bandwidth_flip
				&mode_lib->mp.ImmediateFlipSupported, // bool* flip_bandwidth_support_ok

				dml2_core_internal_soc_state_sys_active,
				mode_lib->mp.urg_bandwidth_required_flip,
				mode_lib->mp.non_urg_bandwidth_required_flip,
				mode_lib->mp.urg_bandwidth_available);

			if (!mode_lib->mp.ImmediateFlipSupported)
				dml2_printf("DML::%s: Bandwidth not sufficient for flip!", __func__);

			for (k = 0; k < s->num_active_planes; ++k) {
				if (display_cfg->plane_descriptors[k].immediate_flip && mode_lib->mp.ImmediateFlipSupportedForPipe[k] == false) {
					mode_lib->mp.ImmediateFlipSupported = false;
#ifdef __DML_VBA_DEBUG__
					dml2_printf("DML::%s: Pipe %0d not supporting iflip!\n", __func__, k);
#endif
				}
			}
		} else { // flip or prefetch not support
			mode_lib->mp.ImmediateFlipSupported = false;
		}

		// consider flip support is okay if the flip bw is ok or (when user does't require a iflip and there is no host vm)
		must_support_iflip = display_cfg->hostvm_enable || s->immediate_flip_required;
		mode_lib->mp.PrefetchAndImmediateFlipSupported = (mode_lib->mp.PrefetchModeSupported == true && (!must_support_iflip || mode_lib->mp.ImmediateFlipSupported));

#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML::%s: PrefetchModeSupported = %u\n", __func__, mode_lib->mp.PrefetchModeSupported);
		for (k = 0; k < s->num_active_planes; ++k)
			dml2_printf("DML::%s: immediate_flip_required[%u] = %u\n", __func__, k, display_cfg->plane_descriptors[k].immediate_flip);
		dml2_printf("DML::%s: HostVMEnable = %u\n", __func__, display_cfg->hostvm_enable);
		dml2_printf("DML::%s: ImmediateFlipSupported = %u\n", __func__, mode_lib->mp.ImmediateFlipSupported);
		dml2_printf("DML::%s: PrefetchAndImmediateFlipSupported = %u\n", __func__, mode_lib->mp.PrefetchAndImmediateFlipSupported);
#endif
		dml2_printf("DML::%s: Done one iteration: k=%d, MaxVStartupLines=%u\n", __func__, k, s->MaxVStartupLines[k]);
	}

	for (k = 0; k < s->num_active_planes; ++k)
		dml2_printf("DML::%s: k=%d MaxVStartupLines = %u\n", __func__, k, s->MaxVStartupLines[k]);

	if (!mode_lib->mp.PrefetchAndImmediateFlipSupported) {
		dml2_printf("DML::%s: Bad, Prefetch and flip scheduling solution NOT found!\n", __func__);
	} else {
		dml2_printf("DML::%s: Good, Prefetch and flip scheduling solution found\n", __func__);

		// DCC Configuration
		for (k = 0; k < s->num_active_planes; ++k) {
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: Calculate DCC configuration for surface k=%u\n", __func__, k);
#endif
			CalculateDCCConfiguration(
				display_cfg->plane_descriptors[k].surface.dcc.enable,
				display_cfg->overrides.dcc_programming_assumes_scan_direction_unknown,
				display_cfg->plane_descriptors[k].pixel_format,
				display_cfg->plane_descriptors[k].surface.plane0.width,
				display_cfg->plane_descriptors[k].surface.plane1.width,
				display_cfg->plane_descriptors[k].surface.plane0.height,
				display_cfg->plane_descriptors[k].surface.plane1.height,
				s->NomDETInKByte,
				mode_lib->mp.Read256BlockHeightY[k],
				mode_lib->mp.Read256BlockHeightC[k],
				display_cfg->plane_descriptors[k].surface.tiling,
				mode_lib->mp.BytePerPixelY[k],
				mode_lib->mp.BytePerPixelC[k],
				mode_lib->mp.BytePerPixelInDETY[k],
				mode_lib->mp.BytePerPixelInDETC[k],
				display_cfg->plane_descriptors[k].composition.rotation_angle,

				/* Output */
				&mode_lib->mp.RequestLuma[k],
				&mode_lib->mp.RequestChroma[k],
				&mode_lib->mp.DCCYMaxUncompressedBlock[k],
				&mode_lib->mp.DCCCMaxUncompressedBlock[k],
				&mode_lib->mp.DCCYMaxCompressedBlock[k],
				&mode_lib->mp.DCCCMaxCompressedBlock[k],
				&mode_lib->mp.DCCYIndependentBlock[k],
				&mode_lib->mp.DCCCIndependentBlock[k]);
		}

		//Watermarks and NB P-State/DRAM Clock Change Support
		s->mmSOCParameters.UrgentLatency = mode_lib->mp.UrgentLatency;
		s->mmSOCParameters.ExtraLatency = mode_lib->mp.ExtraLatency;
		s->mmSOCParameters.ExtraLatency_sr = mode_lib->mp.ExtraLatency_sr;
		s->mmSOCParameters.WritebackLatency = mode_lib->soc.qos_parameters.writeback.base_latency_us;
		s->mmSOCParameters.DRAMClockChangeLatency = mode_lib->soc.power_management_parameters.dram_clk_change_blackout_us;
		s->mmSOCParameters.FCLKChangeLatency = mode_lib->soc.power_management_parameters.fclk_change_blackout_us;
		s->mmSOCParameters.SRExitTime = mode_lib->soc.power_management_parameters.stutter_exit_latency_us;
		s->mmSOCParameters.SREnterPlusExitTime = mode_lib->soc.power_management_parameters.stutter_enter_plus_exit_latency_us;
		s->mmSOCParameters.SRExitZ8Time = mode_lib->soc.power_management_parameters.z8_stutter_exit_latency_us;
		s->mmSOCParameters.SREnterPlusExitZ8Time = mode_lib->soc.power_management_parameters.z8_stutter_enter_plus_exit_latency_us;
		s->mmSOCParameters.USRRetrainingLatency = 0;
		s->mmSOCParameters.SMNLatency = 0;
		s->mmSOCParameters.g6_temp_read_blackout_us = get_g6_temp_read_blackout_us(&mode_lib->soc, (unsigned int)(mode_lib->mp.uclk_freq_mhz * 1000), in_out_params->min_clk_index);
		s->mmSOCParameters.max_urgent_latency_us = get_max_urgent_latency_us(&mode_lib->soc.qos_parameters.qos_params.dcn4x, mode_lib->mp.uclk_freq_mhz, mode_lib->mp.FabricClock, in_out_params->min_clk_index);
		s->mmSOCParameters.df_response_time_us = mode_lib->soc.qos_parameters.qos_params.dcn4x.df_qos_response_time_fclk_cycles / mode_lib->mp.FabricClock;
		s->mmSOCParameters.qos_type = mode_lib->soc.qos_parameters.qos_type;

		CalculateWatermarks_params->display_cfg = display_cfg;
		CalculateWatermarks_params->USRRetrainingRequired = false;
		CalculateWatermarks_params->NumberOfActiveSurfaces = s->num_active_planes;
		CalculateWatermarks_params->MaxLineBufferLines = mode_lib->ip.max_line_buffer_lines;
		CalculateWatermarks_params->LineBufferSize = mode_lib->ip.line_buffer_size_bits;
		CalculateWatermarks_params->WritebackInterfaceBufferSize = mode_lib->ip.writeback_interface_buffer_size_kbytes;
		CalculateWatermarks_params->DCFCLK = mode_lib->mp.Dcfclk;
		CalculateWatermarks_params->SynchronizeTimings = display_cfg->overrides.synchronize_timings;
		CalculateWatermarks_params->SynchronizeDRRDisplaysForUCLKPStateChange = display_cfg->overrides.synchronize_ddr_displays_for_uclk_pstate_change;
		CalculateWatermarks_params->dpte_group_bytes = mode_lib->mp.dpte_group_bytes;
		CalculateWatermarks_params->mmSOCParameters = s->mmSOCParameters;
		CalculateWatermarks_params->WritebackChunkSize = mode_lib->ip.writeback_chunk_size_kbytes;
		CalculateWatermarks_params->SOCCLK = s->SOCCLK;
		CalculateWatermarks_params->DCFClkDeepSleep = mode_lib->mp.DCFCLKDeepSleep;
		CalculateWatermarks_params->DETBufferSizeY = mode_lib->mp.DETBufferSizeY;
		CalculateWatermarks_params->DETBufferSizeC = mode_lib->mp.DETBufferSizeC;
		CalculateWatermarks_params->SwathHeightY = mode_lib->mp.SwathHeightY;
		CalculateWatermarks_params->SwathHeightC = mode_lib->mp.SwathHeightC;
		CalculateWatermarks_params->SwathWidthY = mode_lib->mp.SwathWidthY;
		CalculateWatermarks_params->SwathWidthC = mode_lib->mp.SwathWidthC;
		CalculateWatermarks_params->BytePerPixelDETY = mode_lib->mp.BytePerPixelInDETY;
		CalculateWatermarks_params->BytePerPixelDETC = mode_lib->mp.BytePerPixelInDETC;
		CalculateWatermarks_params->DSTXAfterScaler = mode_lib->mp.DSTXAfterScaler;
		CalculateWatermarks_params->DSTYAfterScaler = mode_lib->mp.DSTYAfterScaler;
		CalculateWatermarks_params->UnboundedRequestEnabled = mode_lib->mp.UnboundedRequestEnabled;
		CalculateWatermarks_params->CompressedBufferSizeInkByte = mode_lib->mp.CompressedBufferSizeInkByte;
		CalculateWatermarks_params->meta_row_height_l = mode_lib->mp.meta_row_height;
		CalculateWatermarks_params->meta_row_height_c = mode_lib->mp.meta_row_height_chroma;
		CalculateWatermarks_params->DPPPerSurface = mode_lib->mp.NoOfDPP;

		// Output
		CalculateWatermarks_params->Watermark = &mode_lib->mp.Watermark;
		CalculateWatermarks_params->DRAMClockChangeSupport = mode_lib->mp.DRAMClockChangeSupport;
		CalculateWatermarks_params->global_dram_clock_change_supported = &mode_lib->mp.global_dram_clock_change_supported;
		CalculateWatermarks_params->MaxActiveDRAMClockChangeLatencySupported = mode_lib->mp.MaxActiveDRAMClockChangeLatencySupported;
		CalculateWatermarks_params->SubViewportLinesNeededInMALL = mode_lib->mp.SubViewportLinesNeededInMALL;
		CalculateWatermarks_params->FCLKChangeSupport = mode_lib->mp.FCLKChangeSupport;
		CalculateWatermarks_params->global_fclk_change_supported = &mode_lib->mp.global_fclk_change_supported;
		CalculateWatermarks_params->MaxActiveFCLKChangeLatencySupported = &mode_lib->mp.MaxActiveFCLKChangeLatencySupported;
		CalculateWatermarks_params->USRRetrainingSupport = &mode_lib->mp.USRRetrainingSupport;
		CalculateWatermarks_params->g6_temp_read_support = &mode_lib->mp.g6_temp_read_support;
		CalculateWatermarks_params->VActiveLatencyHidingMargin = 0;
		CalculateWatermarks_params->VActiveLatencyHidingUs = 0;

		CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport(&mode_lib->scratch, CalculateWatermarks_params);

		for (k = 0; k < s->num_active_planes; ++k) {
			if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0) {
				mode_lib->mp.WritebackAllowDRAMClockChangeEndPosition[k] = math_max2(0, mode_lib->mp.VStartupMin[k] * display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total /
					((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) - mode_lib->mp.Watermark.WritebackDRAMClockChangeWatermark);
				mode_lib->mp.WritebackAllowFCLKChangeEndPosition[k] = math_max2(0, mode_lib->mp.VStartupMin[k] * display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total /
					((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) - mode_lib->mp.Watermark.WritebackFCLKChangeWatermark);
			} else {
				mode_lib->mp.WritebackAllowDRAMClockChangeEndPosition[k] = 0;
				mode_lib->mp.WritebackAllowFCLKChangeEndPosition[k] = 0;
			}
		}

		calculate_pstate_keepout_dst_lines(display_cfg, &mode_lib->mp.Watermark, mode_lib->mp.pstate_keepout_dst_lines);

		dml2_printf("DML::%s: DEBUG stream_index = %0d\n", __func__, display_cfg->plane_descriptors[0].stream_index);
		dml2_printf("DML::%s: DEBUG PixelClock = %d kHz\n", __func__, (display_cfg->stream_descriptors[display_cfg->plane_descriptors[0].stream_index].timing.pixel_clock_khz));

		//Display Pipeline Delivery Time in Prefetch, Groups
		CalculatePixelDeliveryTimes(
			display_cfg,
			cfg_support_info,
			s->num_active_planes,
			mode_lib->mp.VRatioPrefetchY,
			mode_lib->mp.VRatioPrefetchC,
			mode_lib->mp.swath_width_luma_ub,
			mode_lib->mp.swath_width_chroma_ub,
			mode_lib->mp.PSCL_THROUGHPUT,
			mode_lib->mp.PSCL_THROUGHPUT_CHROMA,
			mode_lib->mp.Dppclk,
			mode_lib->mp.BytePerPixelC,
			mode_lib->mp.req_per_swath_ub_l,
			mode_lib->mp.req_per_swath_ub_c,

			/* Output */
			mode_lib->mp.DisplayPipeLineDeliveryTimeLuma,
			mode_lib->mp.DisplayPipeLineDeliveryTimeChroma,
			mode_lib->mp.DisplayPipeLineDeliveryTimeLumaPrefetch,
			mode_lib->mp.DisplayPipeLineDeliveryTimeChromaPrefetch,
			mode_lib->mp.DisplayPipeRequestDeliveryTimeLuma,
			mode_lib->mp.DisplayPipeRequestDeliveryTimeChroma,
			mode_lib->mp.DisplayPipeRequestDeliveryTimeLumaPrefetch,
			mode_lib->mp.DisplayPipeRequestDeliveryTimeChromaPrefetch);

		CalculateMetaAndPTETimes_params->scratch = &mode_lib->scratch;
		CalculateMetaAndPTETimes_params->display_cfg = display_cfg;
		CalculateMetaAndPTETimes_params->NumberOfActiveSurfaces = s->num_active_planes;
		CalculateMetaAndPTETimes_params->use_one_row_for_frame = mode_lib->mp.use_one_row_for_frame;
		CalculateMetaAndPTETimes_params->dst_y_per_row_vblank = mode_lib->mp.dst_y_per_row_vblank;
		CalculateMetaAndPTETimes_params->dst_y_per_row_flip = mode_lib->mp.dst_y_per_row_flip;
		CalculateMetaAndPTETimes_params->BytePerPixelY = mode_lib->mp.BytePerPixelY;
		CalculateMetaAndPTETimes_params->BytePerPixelC = mode_lib->mp.BytePerPixelC;
		CalculateMetaAndPTETimes_params->dpte_row_height = mode_lib->mp.dpte_row_height;
		CalculateMetaAndPTETimes_params->dpte_row_height_chroma = mode_lib->mp.dpte_row_height_chroma;
		CalculateMetaAndPTETimes_params->dpte_group_bytes = mode_lib->mp.dpte_group_bytes;
		CalculateMetaAndPTETimes_params->PTERequestSizeY = mode_lib->mp.PTERequestSizeY;
		CalculateMetaAndPTETimes_params->PTERequestSizeC = mode_lib->mp.PTERequestSizeC;
		CalculateMetaAndPTETimes_params->PixelPTEReqWidthY = mode_lib->mp.PixelPTEReqWidthY;
		CalculateMetaAndPTETimes_params->PixelPTEReqHeightY = mode_lib->mp.PixelPTEReqHeightY;
		CalculateMetaAndPTETimes_params->PixelPTEReqWidthC = mode_lib->mp.PixelPTEReqWidthC;
		CalculateMetaAndPTETimes_params->PixelPTEReqHeightC = mode_lib->mp.PixelPTEReqHeightC;
		CalculateMetaAndPTETimes_params->dpte_row_width_luma_ub = mode_lib->mp.dpte_row_width_luma_ub;
		CalculateMetaAndPTETimes_params->dpte_row_width_chroma_ub = mode_lib->mp.dpte_row_width_chroma_ub;
		CalculateMetaAndPTETimes_params->tdlut_groups_per_2row_ub = s->tdlut_groups_per_2row_ub;
		CalculateMetaAndPTETimes_params->mrq_present = mode_lib->ip.dcn_mrq_present;

		CalculateMetaAndPTETimes_params->MetaChunkSize = mode_lib->ip.meta_chunk_size_kbytes;
		CalculateMetaAndPTETimes_params->MinMetaChunkSizeBytes = mode_lib->ip.min_meta_chunk_size_bytes;
		CalculateMetaAndPTETimes_params->meta_row_width = mode_lib->mp.meta_row_width;
		CalculateMetaAndPTETimes_params->meta_row_width_chroma = mode_lib->mp.meta_row_width_chroma;
		CalculateMetaAndPTETimes_params->meta_row_height = mode_lib->mp.meta_row_height;
		CalculateMetaAndPTETimes_params->meta_row_height_chroma = mode_lib->mp.meta_row_height_chroma;
		CalculateMetaAndPTETimes_params->meta_req_width = mode_lib->mp.meta_req_width;
		CalculateMetaAndPTETimes_params->meta_req_width_chroma = mode_lib->mp.meta_req_width_chroma;
		CalculateMetaAndPTETimes_params->meta_req_height = mode_lib->mp.meta_req_height;
		CalculateMetaAndPTETimes_params->meta_req_height_chroma = mode_lib->mp.meta_req_height_chroma;

		CalculateMetaAndPTETimes_params->time_per_tdlut_group = mode_lib->mp.time_per_tdlut_group;
		CalculateMetaAndPTETimes_params->DST_Y_PER_PTE_ROW_NOM_L = mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_L;
		CalculateMetaAndPTETimes_params->DST_Y_PER_PTE_ROW_NOM_C = mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_C;
		CalculateMetaAndPTETimes_params->time_per_pte_group_nom_luma = mode_lib->mp.time_per_pte_group_nom_luma;
		CalculateMetaAndPTETimes_params->time_per_pte_group_vblank_luma = mode_lib->mp.time_per_pte_group_vblank_luma;
		CalculateMetaAndPTETimes_params->time_per_pte_group_flip_luma = mode_lib->mp.time_per_pte_group_flip_luma;
		CalculateMetaAndPTETimes_params->time_per_pte_group_nom_chroma = mode_lib->mp.time_per_pte_group_nom_chroma;
		CalculateMetaAndPTETimes_params->time_per_pte_group_vblank_chroma = mode_lib->mp.time_per_pte_group_vblank_chroma;
		CalculateMetaAndPTETimes_params->time_per_pte_group_flip_chroma = mode_lib->mp.time_per_pte_group_flip_chroma;
		CalculateMetaAndPTETimes_params->DST_Y_PER_META_ROW_NOM_L = mode_lib->mp.DST_Y_PER_META_ROW_NOM_L;
		CalculateMetaAndPTETimes_params->DST_Y_PER_META_ROW_NOM_C = mode_lib->mp.DST_Y_PER_META_ROW_NOM_C;
		CalculateMetaAndPTETimes_params->TimePerMetaChunkNominal = mode_lib->mp.TimePerMetaChunkNominal;
		CalculateMetaAndPTETimes_params->TimePerChromaMetaChunkNominal = mode_lib->mp.TimePerChromaMetaChunkNominal;
		CalculateMetaAndPTETimes_params->TimePerMetaChunkVBlank = mode_lib->mp.TimePerMetaChunkVBlank;
		CalculateMetaAndPTETimes_params->TimePerChromaMetaChunkVBlank = mode_lib->mp.TimePerChromaMetaChunkVBlank;
		CalculateMetaAndPTETimes_params->TimePerMetaChunkFlip = mode_lib->mp.TimePerMetaChunkFlip;
		CalculateMetaAndPTETimes_params->TimePerChromaMetaChunkFlip = mode_lib->mp.TimePerChromaMetaChunkFlip;

		CalculateMetaAndPTETimes(CalculateMetaAndPTETimes_params);

		CalculateVMGroupAndRequestTimes(
			display_cfg,
			s->num_active_planes,
			mode_lib->mp.BytePerPixelC,
			mode_lib->mp.dst_y_per_vm_vblank,
			mode_lib->mp.dst_y_per_vm_flip,
			mode_lib->mp.dpte_row_width_luma_ub,
			mode_lib->mp.dpte_row_width_chroma_ub,
			mode_lib->mp.vm_group_bytes,
			mode_lib->mp.dpde0_bytes_per_frame_ub_l,
			mode_lib->mp.dpde0_bytes_per_frame_ub_c,
			s->tdlut_pte_bytes_per_frame,
			mode_lib->mp.meta_pte_bytes_per_frame_ub_l,
			mode_lib->mp.meta_pte_bytes_per_frame_ub_c,
			mode_lib->ip.dcn_mrq_present,

			/* Output */
			mode_lib->mp.TimePerVMGroupVBlank,
			mode_lib->mp.TimePerVMGroupFlip,
			mode_lib->mp.TimePerVMRequestVBlank,
			mode_lib->mp.TimePerVMRequestFlip);

		// VStartup Adjustment
		for (k = 0; k < s->num_active_planes; ++k) {
			bool isInterlaceTiming;

			mode_lib->mp.MinTTUVBlank[k] = mode_lib->mp.TWait[k] + mode_lib->mp.ExtraLatency;
			if (!display_cfg->plane_descriptors[k].dynamic_meta_data.enable)
				mode_lib->mp.MinTTUVBlank[k] = mode_lib->mp.TCalc + mode_lib->mp.MinTTUVBlank[k];

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u, MinTTUVBlank = %f (before vstartup margin)\n", __func__, k, mode_lib->mp.MinTTUVBlank[k]);
#endif
			s->Tvstartup_margin = (s->MaxVStartupLines[k] - mode_lib->mp.VStartupMin[k]) * display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
			mode_lib->mp.MinTTUVBlank[k] = mode_lib->mp.MinTTUVBlank[k] + s->Tvstartup_margin;

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u, Tvstartup_margin = %f\n", __func__, k, s->Tvstartup_margin);
			dml2_printf("DML::%s: k=%u, MaxVStartupLines = %u\n", __func__, k, s->MaxVStartupLines[k]);
			dml2_printf("DML::%s: k=%u, MinTTUVBlank = %f\n", __func__, k, mode_lib->mp.MinTTUVBlank[k]);
#endif

			mode_lib->mp.Tdmdl[k] = mode_lib->mp.Tdmdl[k] + s->Tvstartup_margin;
			if (display_cfg->plane_descriptors[k].dynamic_meta_data.enable && mode_lib->ip.dynamic_metadata_vm_enabled) {
				mode_lib->mp.Tdmdl_vm[k] = mode_lib->mp.Tdmdl_vm[k] + s->Tvstartup_margin;
			}

			isInterlaceTiming = (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced && !mode_lib->ip.ptoi_supported);

			// The actual positioning of the vstartup
			mode_lib->mp.VStartup[k] = (isInterlaceTiming ? (2 * s->MaxVStartupLines[k]) : s->MaxVStartupLines[k]);

			s->dlg_vblank_start = ((isInterlaceTiming ? math_floor2((display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_front_porch) / 2.0, 1.0) :
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total) - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_front_porch);
			s->LSetup = math_floor2(4.0 * mode_lib->mp.TSetup[k] / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)), 1.0) / 4.0;
			s->blank_lines_remaining = (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_active) - mode_lib->mp.VStartup[k];

			if (s->blank_lines_remaining < 0) {
				dml2_printf("ERROR: Vstartup is larger than vblank!?\n");
				s->blank_lines_remaining = 0;
				DML2_ASSERT(0);
			}
			mode_lib->mp.MIN_DST_Y_NEXT_START[k] = s->dlg_vblank_start + s->blank_lines_remaining + s->LSetup;

			// debug only
			if (((mode_lib->mp.VUpdateOffsetPix[k] + mode_lib->mp.VUpdateWidthPix[k] + mode_lib->mp.VReadyOffsetPix[k]) / (double) display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total) <=
				(isInterlaceTiming ?
					math_floor2((display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_active - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_front_porch - mode_lib->mp.VStartup[k]) / 2.0, 1.0) :
					(int)(display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_active - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_front_porch - mode_lib->mp.VStartup[k]))) {
				mode_lib->mp.VREADY_AT_OR_AFTER_VSYNC[k] = true;
			} else {
				mode_lib->mp.VREADY_AT_OR_AFTER_VSYNC[k] = false;
			}
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u, VStartup = %u (max)\n", __func__, k, mode_lib->mp.VStartup[k]);
			dml2_printf("DML::%s: k=%u, VStartupMin = %u (max)\n", __func__, k, mode_lib->mp.VStartupMin[k]);
			dml2_printf("DML::%s: k=%u, VUpdateOffsetPix = %u\n", __func__, k, mode_lib->mp.VUpdateOffsetPix[k]);
			dml2_printf("DML::%s: k=%u, VUpdateWidthPix = %u\n", __func__, k, mode_lib->mp.VUpdateWidthPix[k]);
			dml2_printf("DML::%s: k=%u, VReadyOffsetPix = %u\n", __func__, k, mode_lib->mp.VReadyOffsetPix[k]);
			dml2_printf("DML::%s: k=%u, HTotal = %u\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total);
			dml2_printf("DML::%s: k=%u, VTotal = %u\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total);
			dml2_printf("DML::%s: k=%u, VActive = %u\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_active);
			dml2_printf("DML::%s: k=%u, VFrontPorch = %u\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_front_porch);
			dml2_printf("DML::%s: k=%u, TSetup = %f\n", __func__, k, mode_lib->mp.TSetup[k]);
			dml2_printf("DML::%s: k=%u, MIN_DST_Y_NEXT_START = %f\n", __func__, k, mode_lib->mp.MIN_DST_Y_NEXT_START[k]);
			dml2_printf("DML::%s: k=%u, VREADY_AT_OR_AFTER_VSYNC = %u\n", __func__, k, mode_lib->mp.VREADY_AT_OR_AFTER_VSYNC[k]);
#endif
		}

		//Maximum Bandwidth Used
		s->TotalWRBandwidth = 0;
		for (k = 0; k < display_cfg->num_streams; ++k) {
			s->WRBandwidth = 0;
			if (display_cfg->stream_descriptors[k].writeback.active_writebacks_per_stream > 0) {
				s->WRBandwidth = display_cfg->stream_descriptors[k].writeback.writeback_stream[0].output_height
					* display_cfg->stream_descriptors[k].writeback.writeback_stream[0].output_width /
					(display_cfg->stream_descriptors[k].timing.h_total * display_cfg->stream_descriptors[k].writeback.writeback_stream[0].input_height
						/ ((double)display_cfg->stream_descriptors[k].timing.pixel_clock_khz / 1000))
					* (display_cfg->stream_descriptors[k].writeback.writeback_stream[0].pixel_format == dml2_444_32 ? 4.0 : 8.0);
				s->TotalWRBandwidth = s->TotalWRBandwidth + s->WRBandwidth;
			}
		}

		mode_lib->mp.TotalDataReadBandwidth = 0;
		for (k = 0; k < s->num_active_planes; ++k) {
			mode_lib->mp.TotalDataReadBandwidth = mode_lib->mp.TotalDataReadBandwidth + mode_lib->mp.vactive_sw_bw_l[k] + mode_lib->mp.vactive_sw_bw_c[k];
#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML::%s: k=%u, TotalDataReadBandwidth = %f\n", __func__, k, mode_lib->mp.TotalDataReadBandwidth);
			dml2_printf("DML::%s: k=%u, vactive_sw_bw_l = %f\n", __func__, k, mode_lib->mp.vactive_sw_bw_l[k]);
			dml2_printf("DML::%s: k=%u, vactive_sw_bw_c = %f\n", __func__, k, mode_lib->mp.vactive_sw_bw_c[k]);
#endif
		}

		CalculateStutterEfficiency_params->display_cfg = display_cfg;
		CalculateStutterEfficiency_params->CompressedBufferSizeInkByte = mode_lib->mp.CompressedBufferSizeInkByte;
		CalculateStutterEfficiency_params->UnboundedRequestEnabled = mode_lib->mp.UnboundedRequestEnabled;
		CalculateStutterEfficiency_params->MetaFIFOSizeInKEntries = mode_lib->ip.meta_fifo_size_in_kentries;
		CalculateStutterEfficiency_params->ZeroSizeBufferEntries = mode_lib->ip.zero_size_buffer_entries;
		CalculateStutterEfficiency_params->PixelChunkSizeInKByte = mode_lib->ip.pixel_chunk_size_kbytes;
		CalculateStutterEfficiency_params->NumberOfActiveSurfaces = s->num_active_planes;
		CalculateStutterEfficiency_params->ROBBufferSizeInKByte = mode_lib->ip.rob_buffer_size_kbytes;
		CalculateStutterEfficiency_params->TotalDataReadBandwidth = mode_lib->mp.TotalDataReadBandwidth;
		CalculateStutterEfficiency_params->DCFCLK = mode_lib->mp.Dcfclk;
		CalculateStutterEfficiency_params->ReturnBW = mode_lib->mp.urg_bandwidth_available_min[dml2_core_internal_soc_state_sys_active];
		CalculateStutterEfficiency_params->CompbufReservedSpace64B = mode_lib->mp.compbuf_reserved_space_64b;
		CalculateStutterEfficiency_params->CompbufReservedSpaceZs = mode_lib->ip.compbuf_reserved_space_zs;
		CalculateStutterEfficiency_params->SRExitTime = mode_lib->soc.power_management_parameters.stutter_exit_latency_us;
		CalculateStutterEfficiency_params->SRExitZ8Time = mode_lib->soc.power_management_parameters.z8_stutter_exit_latency_us;
		CalculateStutterEfficiency_params->SynchronizeTimings = display_cfg->overrides.synchronize_timings;
		CalculateStutterEfficiency_params->StutterEnterPlusExitWatermark = mode_lib->mp.Watermark.StutterEnterPlusExitWatermark;
		CalculateStutterEfficiency_params->Z8StutterEnterPlusExitWatermark = mode_lib->mp.Watermark.Z8StutterEnterPlusExitWatermark;
		CalculateStutterEfficiency_params->ProgressiveToInterlaceUnitInOPP = mode_lib->ip.ptoi_supported;
		CalculateStutterEfficiency_params->MinTTUVBlank = mode_lib->mp.MinTTUVBlank;
		CalculateStutterEfficiency_params->DPPPerSurface = mode_lib->mp.NoOfDPP;
		CalculateStutterEfficiency_params->DETBufferSizeY = mode_lib->mp.DETBufferSizeY;
		CalculateStutterEfficiency_params->BytePerPixelY = mode_lib->mp.BytePerPixelY;
		CalculateStutterEfficiency_params->BytePerPixelDETY = mode_lib->mp.BytePerPixelInDETY;
		CalculateStutterEfficiency_params->SwathWidthY = mode_lib->mp.SwathWidthY;
		CalculateStutterEfficiency_params->SwathHeightY = mode_lib->mp.SwathHeightY;
		CalculateStutterEfficiency_params->SwathHeightC = mode_lib->mp.SwathHeightC;
		CalculateStutterEfficiency_params->BlockHeight256BytesY = mode_lib->mp.Read256BlockHeightY;
		CalculateStutterEfficiency_params->BlockWidth256BytesY = mode_lib->mp.Read256BlockWidthY;
		CalculateStutterEfficiency_params->BlockHeight256BytesC = mode_lib->mp.Read256BlockHeightC;
		CalculateStutterEfficiency_params->BlockWidth256BytesC = mode_lib->mp.Read256BlockWidthC;
		CalculateStutterEfficiency_params->DCCYMaxUncompressedBlock = mode_lib->mp.DCCYMaxUncompressedBlock;
		CalculateStutterEfficiency_params->DCCCMaxUncompressedBlock = mode_lib->mp.DCCCMaxUncompressedBlock;
		CalculateStutterEfficiency_params->ReadBandwidthSurfaceLuma = mode_lib->mp.vactive_sw_bw_l;
		CalculateStutterEfficiency_params->ReadBandwidthSurfaceChroma = mode_lib->mp.vactive_sw_bw_c;
		CalculateStutterEfficiency_params->dpte_row_bw = mode_lib->mp.dpte_row_bw;
		CalculateStutterEfficiency_params->meta_row_bw = mode_lib->mp.meta_row_bw;
		CalculateStutterEfficiency_params->rob_alloc_compressed = mode_lib->ip.dcn_mrq_present;

		// output
		CalculateStutterEfficiency_params->StutterEfficiencyNotIncludingVBlank = &mode_lib->mp.StutterEfficiencyNotIncludingVBlank;
		CalculateStutterEfficiency_params->StutterEfficiency = &mode_lib->mp.StutterEfficiency;
		CalculateStutterEfficiency_params->NumberOfStutterBurstsPerFrame = &mode_lib->mp.NumberOfStutterBurstsPerFrame;
		CalculateStutterEfficiency_params->Z8StutterEfficiencyNotIncludingVBlank = &mode_lib->mp.Z8StutterEfficiencyNotIncludingVBlank;
		CalculateStutterEfficiency_params->Z8StutterEfficiency = &mode_lib->mp.Z8StutterEfficiency;
		CalculateStutterEfficiency_params->Z8NumberOfStutterBurstsPerFrame = &mode_lib->mp.Z8NumberOfStutterBurstsPerFrame;
		CalculateStutterEfficiency_params->StutterPeriod = &mode_lib->mp.StutterPeriod;
		CalculateStutterEfficiency_params->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = &mode_lib->mp.DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE;

		// Stutter Efficiency
		CalculateStutterEfficiency(&mode_lib->scratch, CalculateStutterEfficiency_params);

#ifdef __DML_VBA_ALLOW_DELTA__
		// Calculate z8 stutter eff assuming 0 reserved space
		CalculateStutterEfficiency_params->CompbufReservedSpace64B = 0;
		CalculateStutterEfficiency_params->CompbufReservedSpaceZs = 0;

		CalculateStutterEfficiency_params->Z8StutterEfficiencyNotIncludingVBlank = &mode_lib->mp.Z8StutterEfficiencyNotIncludingVBlankBestCase;
		CalculateStutterEfficiency_params->Z8StutterEfficiency = &mode_lib->mp.Z8StutterEfficiencyBestCase;
		CalculateStutterEfficiency_params->Z8NumberOfStutterBurstsPerFrame = &mode_lib->mp.Z8NumberOfStutterBurstsPerFrameBestCase;
		CalculateStutterEfficiency_params->StutterPeriod = &mode_lib->mp.StutterPeriodBestCase;

		// Stutter Efficiency
		CalculateStutterEfficiency(&mode_lib->scratch, CalculateStutterEfficiency_params);
#else
		mode_lib->mp.Z8StutterEfficiencyNotIncludingVBlankBestCase = mode_lib->mp.Z8StutterEfficiencyNotIncludingVBlank;
		mode_lib->mp.Z8StutterEfficiencyBestCase = mode_lib->mp.Z8StutterEfficiency;
		mode_lib->mp.Z8NumberOfStutterBurstsPerFrameBestCase = mode_lib->mp.Z8NumberOfStutterBurstsPerFrame;
		mode_lib->mp.StutterPeriodBestCase = mode_lib->mp.StutterPeriod;
#endif
	} // PrefetchAndImmediateFlipSupported

	max_uclk_mhz = mode_lib->soc.clk_table.uclk.clk_values_khz[mode_lib->soc.clk_table.uclk.num_clk_values - 1] / 1000.0;
	min_return_latency_in_DCFCLK_cycles = (min_return_uclk_cycles / max_uclk_mhz + min_return_fclk_cycles / max_fclk_mhz) * hard_minimum_dcfclk_mhz;
	mode_lib->mp.min_return_latency_in_dcfclk = (unsigned int)min_return_latency_in_DCFCLK_cycles;
	mode_lib->mp.dcfclk_deep_sleep_hysteresis = (unsigned int)math_max2(32, (double)mode_lib->ip.pixel_chunk_size_kbytes * 1024 * 3 / 4 / 64 - min_return_latency_in_DCFCLK_cycles);
	DML2_ASSERT(mode_lib->mp.dcfclk_deep_sleep_hysteresis < 256);

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: max_fclk_mhz = %f\n", __func__, max_fclk_mhz);
	dml2_printf("DML::%s: max_uclk_mhz = %f\n", __func__, max_uclk_mhz);
	dml2_printf("DML::%s: hard_minimum_dcfclk_mhz = %f\n", __func__, hard_minimum_dcfclk_mhz);
	dml2_printf("DML::%s: min_return_uclk_cycles = %d\n", __func__, min_return_uclk_cycles);
	dml2_printf("DML::%s: min_return_fclk_cycles = %d\n", __func__, min_return_fclk_cycles);
	dml2_printf("DML::%s: min_return_latency_in_DCFCLK_cycles = %f\n", __func__, min_return_latency_in_DCFCLK_cycles);
	dml2_printf("DML::%s: dcfclk_deep_sleep_hysteresis = %d \n", __func__, mode_lib->mp.dcfclk_deep_sleep_hysteresis);
	dml2_printf("DML::%s: --- END --- \n", __func__);
#endif
	return (in_out_params->mode_lib->mp.PrefetchAndImmediateFlipSupported);
}

bool dml2_core_calcs_mode_programming_ex(struct dml2_core_calcs_mode_programming_ex *in_out_params)
{
	dml2_printf("DML::%s: ------------- START ----------\n", __func__);
	bool result = dml_core_mode_programming(in_out_params);

	dml2_printf("DML::%s: result = %0d\n", __func__, result);
	dml2_printf("DML::%s: ------------- DONE ----------\n", __func__);
	return result;
}

void dml2_core_calcs_get_dpte_row_height(
						unsigned int							   *dpte_row_height,
						struct dml2_core_internal_display_mode_lib *mode_lib,
						bool										is_plane1,
						enum dml2_source_format_class				SourcePixelFormat,
						enum dml2_swizzle_mode						SurfaceTiling,
						enum dml2_rotation_angle					ScanDirection,
						unsigned int								pitch,
						unsigned int								GPUVMMinPageSizeKBytes)
{
	unsigned int BytePerPixelY;
	unsigned int BytePerPixelC;
	double BytePerPixelInDETY;
	double BytePerPixelInDETC;
	unsigned int BlockHeight256BytesY;
	unsigned int BlockHeight256BytesC;
	unsigned int BlockWidth256BytesY;
	unsigned int BlockWidth256BytesC;
	unsigned int MacroTileWidthY;
	unsigned int MacroTileWidthC;
	unsigned int MacroTileHeightY;
	unsigned int MacroTileHeightC;
	bool surf_linear_128_l = false;
	bool surf_linear_128_c = false;

	CalculateBytePerPixelAndBlockSizes(
		SourcePixelFormat,
		SurfaceTiling,
		pitch,
		pitch,

		/* Output */
		&BytePerPixelY,
		&BytePerPixelC,
		&BytePerPixelInDETY,
		&BytePerPixelInDETC,
		&BlockHeight256BytesY,
		&BlockHeight256BytesC,
		&BlockWidth256BytesY,
		&BlockWidth256BytesC,
		&MacroTileHeightY,
		&MacroTileHeightC,
		&MacroTileWidthY,
		&MacroTileWidthC,
		&surf_linear_128_l,
		&surf_linear_128_c);

	unsigned int BytePerPixel			= is_plane1 ? BytePerPixelC : BytePerPixelY;
	unsigned int BlockHeight256Bytes	= is_plane1 ? BlockHeight256BytesC : BlockHeight256BytesY;
	unsigned int BlockWidth256Bytes		= is_plane1 ? BlockWidth256BytesC  : BlockWidth256BytesY;
	unsigned int MacroTileWidth			= is_plane1 ? MacroTileWidthC  : MacroTileWidthY;
	unsigned int MacroTileHeight		= is_plane1 ? MacroTileHeightC : MacroTileHeightY;
	unsigned int PTEBufferSizeInRequests = is_plane1 ? mode_lib->ip.dpte_buffer_size_in_pte_reqs_chroma : mode_lib->ip.dpte_buffer_size_in_pte_reqs_luma;
#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML: %s: is_plane1 = %u\n", __func__, is_plane1);
	dml2_printf("DML: %s: BytePerPixel = %u\n", __func__, BytePerPixel);
	dml2_printf("DML: %s: BlockHeight256Bytes = %u\n", __func__, BlockHeight256Bytes);
	dml2_printf("DML: %s: BlockWidth256Bytes = %u\n", __func__, BlockWidth256Bytes);
	dml2_printf("DML: %s: MacroTileWidth = %u\n", __func__, MacroTileWidth);
	dml2_printf("DML: %s: MacroTileHeight = %u\n", __func__, MacroTileHeight);
	dml2_printf("DML: %s: PTEBufferSizeInRequests = %u\n", __func__, PTEBufferSizeInRequests);
	dml2_printf("DML: %s: dpte_buffer_size_in_pte_reqs_luma = %u\n", __func__, mode_lib->ip.dpte_buffer_size_in_pte_reqs_luma);
	dml2_printf("DML: %s: dpte_buffer_size_in_pte_reqs_chroma = %u\n", __func__, mode_lib->ip.dpte_buffer_size_in_pte_reqs_chroma);
	dml2_printf("DML: %s: GPUVMMinPageSizeKBytes = %u\n", __func__, GPUVMMinPageSizeKBytes);
#endif
	unsigned int dummy_integer[21];

	mode_lib->scratch.calculate_vm_and_row_bytes_params.ViewportStationary = 0;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.DCCEnable = 0;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.NumberOfDPPs = 1;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.BlockHeight256Bytes = BlockHeight256Bytes;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.BlockWidth256Bytes = BlockWidth256Bytes;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.SourcePixelFormat = SourcePixelFormat;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.SurfaceTiling = SurfaceTiling;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.BytePerPixel = BytePerPixel;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.RotationAngle = ScanDirection;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.SwathWidth = 0;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.ViewportHeight = 0;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.ViewportXStart = 0;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.ViewportYStart = 0;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.GPUVMEnable = 1;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.GPUVMMaxPageTableLevels = 4;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.GPUVMMinPageSizeKBytes = GPUVMMinPageSizeKBytes;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.PTEBufferSizeInRequests = PTEBufferSizeInRequests;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.Pitch = pitch;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.MacroTileWidth = MacroTileWidth;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.MacroTileHeight = MacroTileHeight;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.is_phantom = 0;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.DCCMetaPitch = 0;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.mrq_present = 0;

	mode_lib->scratch.calculate_vm_and_row_bytes_params.PixelPTEBytesPerRow = &dummy_integer[1];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.PixelPTEBytesPerRowStorage = &dummy_integer[2];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.dpte_row_width_ub = &dummy_integer[3];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.dpte_row_height = dpte_row_height;
	mode_lib->scratch.calculate_vm_and_row_bytes_params.dpte_row_height_linear = &dummy_integer[4];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.PixelPTEBytesPerRow_one_row_per_frame = &dummy_integer[5];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.dpte_row_width_ub_one_row_per_frame = &dummy_integer[6];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.dpte_row_height_one_row_per_frame = &dummy_integer[7];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.vmpg_width = &dummy_integer[8];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.vmpg_height = &dummy_integer[9];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.PixelPTEReqWidth = &dummy_integer[11];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.PixelPTEReqHeight = &dummy_integer[12];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.PTERequestSize = &dummy_integer[13];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.dpde0_bytes_per_frame_ub = &dummy_integer[14];

	mode_lib->scratch.calculate_vm_and_row_bytes_params.meta_row_bytes = &dummy_integer[15];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.MetaRequestWidth = &dummy_integer[16];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.MetaRequestHeight = &dummy_integer[17];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.meta_row_width = &dummy_integer[18];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.meta_row_height = &dummy_integer[19];
	mode_lib->scratch.calculate_vm_and_row_bytes_params.meta_pte_bytes_per_frame_ub = &dummy_integer[20];

	// just supply with enough parameters to calculate dpte
	CalculateVMAndRowBytes(&mode_lib->scratch.calculate_vm_and_row_bytes_params);

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML: %s: dpte_row_height = %u\n", __func__, *dpte_row_height);
#endif
}

static bool is_dual_plane(enum dml2_source_format_class source_format)
{
	bool ret_val = 0;

	if ((source_format == dml2_420_12) || (source_format == dml2_420_8) || (source_format == dml2_420_10) || (source_format == dml2_rgbe_alpha))
		ret_val = 1;

	return ret_val;
}

static unsigned int dml_get_plane_idx(const struct dml2_core_internal_display_mode_lib *mode_lib, unsigned int pipe_idx)
{
	unsigned int plane_idx = mode_lib->mp.pipe_plane[pipe_idx];
	return plane_idx;
}

static void rq_dlg_get_wm_regs(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_dchub_watermark_regs *wm_regs)
{
	double refclk_freq_in_mhz = (display_cfg->overrides.hw.dlg_ref_clk_mhz > 0) ? (double)display_cfg->overrides.hw.dlg_ref_clk_mhz : mode_lib->soc.dchub_refclk_mhz;

	wm_regs->fclk_pstate = (int unsigned)(mode_lib->mp.Watermark.FCLKChangeWatermark * refclk_freq_in_mhz);
	wm_regs->sr_enter = (int unsigned)(mode_lib->mp.Watermark.StutterEnterPlusExitWatermark * refclk_freq_in_mhz);
	wm_regs->sr_exit = (int unsigned)(mode_lib->mp.Watermark.StutterExitWatermark * refclk_freq_in_mhz);
	wm_regs->temp_read_or_ppt = (int unsigned)(mode_lib->mp.Watermark.temp_read_or_ppt_watermark_us * refclk_freq_in_mhz);
	wm_regs->uclk_pstate = (int unsigned)(mode_lib->mp.Watermark.DRAMClockChangeWatermark * refclk_freq_in_mhz);
	wm_regs->urgent = (int unsigned)(mode_lib->mp.Watermark.UrgentWatermark * refclk_freq_in_mhz);
	wm_regs->usr = (int unsigned)(mode_lib->mp.Watermark.USRRetrainingWatermark * refclk_freq_in_mhz);
	wm_regs->refcyc_per_trip_to_mem = (unsigned int)(mode_lib->mp.UrgentLatency * refclk_freq_in_mhz);
	wm_regs->refcyc_per_meta_trip_to_mem = (unsigned int)(mode_lib->mp.MetaTripToMemory * refclk_freq_in_mhz);
	wm_regs->frac_urg_bw_flip = (unsigned int)(mode_lib->mp.FractionOfUrgentBandwidthImmediateFlip * 1000);
	wm_regs->frac_urg_bw_nom = (unsigned int)(mode_lib->mp.FractionOfUrgentBandwidth * 1000);
	wm_regs->frac_urg_bw_mall = (unsigned int)(mode_lib->mp.FractionOfUrgentBandwidthMALL * 1000);
}

static unsigned int log_and_substract_if_non_zero(unsigned int a, unsigned int subtrahend)
{
	if (a == 0)
		return 0;

	return (math_log2_approx(a) - subtrahend);
}

void dml2_core_calcs_cursor_dlg_reg(struct dml2_cursor_dlg_regs *cursor_dlg_regs, const struct dml2_get_cursor_dlg_reg *p)
{
	int dst_x_offset = (int) ((p->cursor_x_position + (p->cursor_stereo_en == 0 ? 0 : math_max2(p->cursor_primary_offset, p->cursor_secondary_offset)) -
						(p->cursor_hotspot_x * (p->cursor_2x_magnify == 0 ? 1 : 2))) * p->dlg_refclk_mhz / p->pixel_rate_mhz / p->hratio);
	cursor_dlg_regs->dst_x_offset = (unsigned int) ((dst_x_offset > 0) ? dst_x_offset : 0);

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML_DLG::%s: cursor_x_position=%d\n", __func__, p->cursor_x_position);
	dml2_printf("DML_DLG::%s: dlg_refclk_mhz=%f\n", __func__, p->dlg_refclk_mhz);
	dml2_printf("DML_DLG::%s: pixel_rate_mhz=%f\n", __func__, p->pixel_rate_mhz);
	dml2_printf("DML_DLG::%s: dst_x_offset=%d\n", __func__, dst_x_offset);
	dml2_printf("DML_DLG::%s: dst_x_offset=%d (reg)\n", __func__, cursor_dlg_regs->dst_x_offset);
#endif

	cursor_dlg_regs->chunk_hdl_adjust = 3;
	cursor_dlg_regs->dst_y_offset	 = 0;

	cursor_dlg_regs->qos_level_fixed  = 8;
	cursor_dlg_regs->qos_ramp_disable = 0;
}

static void rq_dlg_get_rq_reg(struct dml2_display_rq_regs *rq_regs,
	const struct dml2_display_cfg *display_cfg,
	const struct dml2_core_internal_display_mode_lib *mode_lib,
	unsigned int pipe_idx)
{
	unsigned int plane_idx = dml_get_plane_idx(mode_lib, pipe_idx);
	enum dml2_source_format_class source_format = display_cfg->plane_descriptors[plane_idx].pixel_format;
	enum dml2_swizzle_mode sw_mode = display_cfg->plane_descriptors[plane_idx].surface.tiling;
	bool dual_plane = is_dual_plane((enum dml2_source_format_class)(source_format));

	unsigned int pixel_chunk_bytes = 0;
	unsigned int min_pixel_chunk_bytes = 0;
	unsigned int dpte_group_bytes = 0;
	unsigned int mpte_group_bytes = 0;

	unsigned int p1_pixel_chunk_bytes = 0;
	unsigned int p1_min_pixel_chunk_bytes = 0;
	unsigned int p1_dpte_group_bytes = 0;
	unsigned int p1_mpte_group_bytes = 0;

	unsigned int detile_buf_plane1_addr = 0;
	unsigned int detile_buf_size_in_bytes;
	double stored_swath_l_bytes;
	double stored_swath_c_bytes;
	bool is_phantom_pipe;

	dml2_printf("DML_DLG::%s: Calculation for pipe[%d] start\n", __func__, pipe_idx);

	pixel_chunk_bytes = (unsigned int)(mode_lib->ip.pixel_chunk_size_kbytes * 1024);
	min_pixel_chunk_bytes = (unsigned int)(mode_lib->ip.min_pixel_chunk_size_bytes);

	if (pixel_chunk_bytes == 64 * 1024)
		min_pixel_chunk_bytes = 0;

	dpte_group_bytes = (unsigned int)(dml_get_dpte_group_size_in_bytes(mode_lib, pipe_idx));
	mpte_group_bytes = (unsigned int)(dml_get_vm_group_size_in_bytes(mode_lib, pipe_idx));

	p1_pixel_chunk_bytes = pixel_chunk_bytes;
	p1_min_pixel_chunk_bytes = min_pixel_chunk_bytes;
	p1_dpte_group_bytes = dpte_group_bytes;
	p1_mpte_group_bytes = mpte_group_bytes;

	if (source_format == dml2_rgbe_alpha)
		p1_pixel_chunk_bytes = (unsigned int)(mode_lib->ip.alpha_pixel_chunk_size_kbytes * 1024);

	rq_regs->unbounded_request_enabled = dml_get_unbounded_request_enabled(mode_lib);
	rq_regs->rq_regs_l.chunk_size = log_and_substract_if_non_zero(pixel_chunk_bytes, 10);
	rq_regs->rq_regs_c.chunk_size = log_and_substract_if_non_zero(p1_pixel_chunk_bytes, 10);

	if (min_pixel_chunk_bytes == 0)
		rq_regs->rq_regs_l.min_chunk_size = 0;
	else
		rq_regs->rq_regs_l.min_chunk_size = log_and_substract_if_non_zero(min_pixel_chunk_bytes, 8 - 1);

	if (p1_min_pixel_chunk_bytes == 0)
		rq_regs->rq_regs_c.min_chunk_size = 0;
	else
		rq_regs->rq_regs_c.min_chunk_size = log_and_substract_if_non_zero(p1_min_pixel_chunk_bytes, 8 - 1);

	rq_regs->rq_regs_l.dpte_group_size = log_and_substract_if_non_zero(dpte_group_bytes, 6);
	rq_regs->rq_regs_l.mpte_group_size = log_and_substract_if_non_zero(mpte_group_bytes, 6);
	rq_regs->rq_regs_c.dpte_group_size = log_and_substract_if_non_zero(p1_dpte_group_bytes, 6);
	rq_regs->rq_regs_c.mpte_group_size = log_and_substract_if_non_zero(p1_mpte_group_bytes, 6);

	detile_buf_size_in_bytes = (unsigned int)(dml_get_det_buffer_size_kbytes(mode_lib, pipe_idx) * 1024);

	if (sw_mode == dml2_sw_linear && display_cfg->gpuvm_enable) {
		unsigned int p0_pte_row_height_linear = (unsigned int)(dml_get_dpte_row_height_linear_l(mode_lib, pipe_idx));
#ifdef __DML_VBA_DEBUG__
		dml2_printf("DML_DLG: %s: p0_pte_row_height_linear = %u\n", __func__, p0_pte_row_height_linear);
#endif
		DML2_ASSERT(p0_pte_row_height_linear >= 8);

		rq_regs->rq_regs_l.pte_row_height_linear = math_log2_approx(p0_pte_row_height_linear) - 3;
		if (dual_plane) {
			unsigned int p1_pte_row_height_linear = (unsigned int)(dml_get_dpte_row_height_linear_c(mode_lib, pipe_idx));

#ifdef __DML_VBA_DEBUG__
			dml2_printf("DML_DLG: %s: p1_pte_row_height_linear = %u\n", __func__, p1_pte_row_height_linear);
#endif
			if (sw_mode == dml2_sw_linear) {
				DML2_ASSERT(p1_pte_row_height_linear >= 8);
			}
			rq_regs->rq_regs_c.pte_row_height_linear = math_log2_approx(p1_pte_row_height_linear) - 3;
		}
	} else {
		rq_regs->rq_regs_l.pte_row_height_linear = 0;
		rq_regs->rq_regs_c.pte_row_height_linear = 0;
	}

	rq_regs->rq_regs_l.swath_height = log_and_substract_if_non_zero(dml_get_swath_height_l(mode_lib, pipe_idx), 0);
	rq_regs->rq_regs_c.swath_height = log_and_substract_if_non_zero(dml_get_swath_height_c(mode_lib, pipe_idx), 0);

	// FIXME_DCN4, programming guide has dGPU condition
	if (pixel_chunk_bytes >= 32 * 1024 || (dual_plane && p1_pixel_chunk_bytes >= 32 * 1024)) { //32kb
		rq_regs->drq_expansion_mode = 0;
	} else {
		rq_regs->drq_expansion_mode = 2;
	}
	rq_regs->prq_expansion_mode = 1;
	rq_regs->crq_expansion_mode = 1;
	rq_regs->mrq_expansion_mode = 1;

	stored_swath_l_bytes = dml_get_det_stored_buffer_size_l_bytes(mode_lib, pipe_idx);
	stored_swath_c_bytes = dml_get_det_stored_buffer_size_c_bytes(mode_lib, pipe_idx);
	is_phantom_pipe = dml_get_is_phantom_pipe(display_cfg, mode_lib, pipe_idx);

	// Note: detile_buf_plane1_addr is in unit of 1KB
	if (dual_plane) {
		if (is_phantom_pipe) {
			detile_buf_plane1_addr = (unsigned int)((1024.0 * 1024.0) / 2.0 / 1024.0); // half to chroma
		} else {
			if (stored_swath_l_bytes / stored_swath_c_bytes <= 1.5) {
				detile_buf_plane1_addr = (unsigned int)(detile_buf_size_in_bytes / 2.0 / 1024.0); // half to chroma
#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML_DLG: %s: detile_buf_plane1_addr = %d (1/2 to chroma)\n", __func__, detile_buf_plane1_addr);
#endif
			} else {
				detile_buf_plane1_addr = (unsigned int)(dml_round_to_multiple((unsigned int)((2.0 * detile_buf_size_in_bytes) / 3.0), 1024, 0) / 1024.0); // 2/3 to luma
#ifdef __DML_VBA_DEBUG__
				dml2_printf("DML_DLG: %s: detile_buf_plane1_addr = %d (1/3 chroma)\n", __func__, detile_buf_plane1_addr);
#endif
			}
		}
	}
	rq_regs->plane1_base_address = detile_buf_plane1_addr;

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML_DLG: %s: is_phantom_pipe = %d\n", __func__, is_phantom_pipe);
	dml2_printf("DML_DLG: %s: stored_swath_l_bytes = %f\n", __func__, stored_swath_l_bytes);
	dml2_printf("DML_DLG: %s: stored_swath_c_bytes = %f\n", __func__, stored_swath_c_bytes);
	dml2_printf("DML_DLG: %s: detile_buf_size_in_bytes = %d\n", __func__, detile_buf_size_in_bytes);
	dml2_printf("DML_DLG: %s: detile_buf_plane1_addr = %d\n", __func__, detile_buf_plane1_addr);
	dml2_printf("DML_DLG: %s: plane1_base_address = %d\n", __func__, rq_regs->plane1_base_address);
#endif
	//dml2_printf_rq_regs_st(rq_regs);
	dml2_printf("DML_DLG::%s: Calculation for pipe[%d] done\n", __func__, pipe_idx);
}

static void rq_dlg_get_dlg_reg(
	struct dml2_core_internal_scratch *s,
	struct dml2_display_dlg_regs *disp_dlg_regs,
	struct dml2_display_ttu_regs *disp_ttu_regs,
	const struct dml2_display_cfg *display_cfg,
	const struct dml2_core_internal_display_mode_lib *mode_lib,
	const unsigned int pipe_idx)
{
	struct dml2_core_shared_rq_dlg_get_dlg_reg_locals *l = &s->rq_dlg_get_dlg_reg_locals;

	memset(l, 0, sizeof(struct dml2_core_shared_rq_dlg_get_dlg_reg_locals));

	dml2_printf("DML_DLG::%s: Calculation for pipe_idx=%d\n", __func__, pipe_idx);

	l->plane_idx = dml_get_plane_idx(mode_lib, pipe_idx);
	dml2_assert(l->plane_idx < DML2_MAX_PLANES);

	l->source_format = dml2_444_8;
	l->odm_mode = dml2_odm_mode_bypass;
	l->dual_plane = false;
	l->htotal = 0;
	l->hactive = 0;
	l->hblank_end = 0;
	l->vblank_end = 0;
	l->interlaced = false;
	l->pclk_freq_in_mhz = 0.0;
	l->refclk_freq_in_mhz = (display_cfg->overrides.hw.dlg_ref_clk_mhz > 0) ? (double)display_cfg->overrides.hw.dlg_ref_clk_mhz : mode_lib->soc.dchub_refclk_mhz;
	l->ref_freq_to_pix_freq = 0.0;

	if (l->plane_idx < DML2_MAX_PLANES) {

		l->timing = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[l->plane_idx].stream_index].timing;
		l->source_format = display_cfg->plane_descriptors[l->plane_idx].pixel_format;
		l->odm_mode = mode_lib->mp.ODMMode[l->plane_idx];

		l->dual_plane = is_dual_plane(l->source_format);

		l->htotal = l->timing->h_total;
		l->hactive = l->timing->h_active;
		l->hblank_end = l->timing->h_blank_end;
		l->vblank_end = l->timing->v_blank_end;
		l->interlaced = l->timing->interlaced;
		l->pclk_freq_in_mhz = (double)l->timing->pixel_clock_khz / 1000;
		l->ref_freq_to_pix_freq = l->refclk_freq_in_mhz / l->pclk_freq_in_mhz;

		dml2_printf("DML_DLG::%s: plane_idx = %d\n", __func__, l->plane_idx);
		dml2_printf("DML_DLG: %s: htotal = %d\n", __func__, l->htotal);
		dml2_printf("DML_DLG: %s: refclk_freq_in_mhz = %3.2f\n", __func__, l->refclk_freq_in_mhz);
		dml2_printf("DML_DLG: %s: dlg_ref_clk_mhz = %3.2f\n", __func__, display_cfg->overrides.hw.dlg_ref_clk_mhz);
		dml2_printf("DML_DLG: %s: soc.refclk_mhz = %3.2f\n", __func__, mode_lib->soc.dchub_refclk_mhz);
		dml2_printf("DML_DLG: %s: pclk_freq_in_mhz = %3.2f\n", __func__, l->pclk_freq_in_mhz);
		dml2_printf("DML_DLG: %s: ref_freq_to_pix_freq = %3.2f\n", __func__, l->ref_freq_to_pix_freq);
		dml2_printf("DML_DLG: %s: interlaced = %d\n", __func__, l->interlaced);

		DML2_ASSERT(l->refclk_freq_in_mhz != 0);
		DML2_ASSERT(l->pclk_freq_in_mhz != 0);
		DML2_ASSERT(l->ref_freq_to_pix_freq < 4.0);

		// Need to figure out which side of odm combine we're in
		// Assume the pipe instance under the same plane is in order

		if (l->odm_mode == dml2_odm_mode_bypass) {
			disp_dlg_regs->refcyc_h_blank_end = (unsigned int)((double)l->hblank_end * l->ref_freq_to_pix_freq);
		} else if (l->odm_mode == dml2_odm_mode_combine_2to1 || l->odm_mode == dml2_odm_mode_combine_3to1 || l->odm_mode == dml2_odm_mode_combine_4to1) {
			// find out how many pipe are in this plane
			l->num_active_pipes = mode_lib->mp.num_active_pipes;
			l->first_pipe_idx_in_plane = DML2_MAX_PLANES;
			l->pipe_idx_in_combine = 0; // pipe index within the plane
			l->odm_combine_factor = 2;

			if (l->odm_mode == dml2_odm_mode_combine_3to1)
				l->odm_combine_factor = 3;
			else if (l->odm_mode == dml2_odm_mode_combine_4to1)
				l->odm_combine_factor = 4;

			for (unsigned int i = 0; i < l->num_active_pipes; i++) {
				if (dml_get_plane_idx(mode_lib, i) == l->plane_idx) {
					if (i < l->first_pipe_idx_in_plane) {
						l->first_pipe_idx_in_plane = i;
					}
				}
			}
			l->pipe_idx_in_combine = pipe_idx - l->first_pipe_idx_in_plane; // DML assumes the pipes in the same plane will have continuous indexing (i.e. plane 0 use pipe 0, 1, and plane 1 uses pipe 2, 3, etc.)

			disp_dlg_regs->refcyc_h_blank_end = (unsigned int)(((double)l->hblank_end + (double)l->pipe_idx_in_combine * (double)l->hactive / (double)l->odm_combine_factor) * l->ref_freq_to_pix_freq);
			dml2_printf("DML_DLG: %s: pipe_idx = %d\n", __func__, pipe_idx);
			dml2_printf("DML_DLG: %s: first_pipe_idx_in_plane = %d\n", __func__, l->first_pipe_idx_in_plane);
			dml2_printf("DML_DLG: %s: pipe_idx_in_combine = %d\n", __func__, l->pipe_idx_in_combine);
			dml2_printf("DML_DLG: %s: odm_combine_factor = %d\n", __func__, l->odm_combine_factor);
		}
		dml2_printf("DML_DLG: %s: refcyc_h_blank_end = %d\n", __func__, disp_dlg_regs->refcyc_h_blank_end);

		DML2_ASSERT(disp_dlg_regs->refcyc_h_blank_end < (unsigned int)math_pow(2, 13));

		disp_dlg_regs->ref_freq_to_pix_freq = (unsigned int)(l->ref_freq_to_pix_freq * math_pow(2, 19));
		disp_dlg_regs->refcyc_per_htotal = (unsigned int)(l->ref_freq_to_pix_freq * (double)l->htotal * math_pow(2, 8));
		disp_dlg_regs->dlg_vblank_end = l->interlaced ? (l->vblank_end / 2) : l->vblank_end; // 15 bits

		l->min_ttu_vblank = mode_lib->mp.MinTTUVBlank[mode_lib->mp.pipe_plane[pipe_idx]];
		l->min_dst_y_next_start = (unsigned int)(mode_lib->mp.MIN_DST_Y_NEXT_START[mode_lib->mp.pipe_plane[pipe_idx]]);

		dml2_printf("DML_DLG: %s: min_ttu_vblank (us) = %3.2f\n", __func__, l->min_ttu_vblank);
		dml2_printf("DML_DLG: %s: min_dst_y_next_start = %d\n", __func__, l->min_dst_y_next_start);
		dml2_printf("DML_DLG: %s: ref_freq_to_pix_freq = %3.2f\n", __func__, l->ref_freq_to_pix_freq);

		l->vready_after_vcount0 = (unsigned int)(mode_lib->mp.VREADY_AT_OR_AFTER_VSYNC[mode_lib->mp.pipe_plane[pipe_idx]]);
		disp_dlg_regs->vready_after_vcount0 = l->vready_after_vcount0;

		dml2_printf("DML_DLG: %s: vready_after_vcount0 = %d\n", __func__, disp_dlg_regs->vready_after_vcount0);

		l->dst_x_after_scaler = (unsigned int)(mode_lib->mp.DSTXAfterScaler[mode_lib->mp.pipe_plane[pipe_idx]]);
		l->dst_y_after_scaler = (unsigned int)(mode_lib->mp.DSTYAfterScaler[mode_lib->mp.pipe_plane[pipe_idx]]);

		dml2_printf("DML_DLG: %s: dst_x_after_scaler = %d\n", __func__, l->dst_x_after_scaler);
		dml2_printf("DML_DLG: %s: dst_y_after_scaler = %d\n", __func__, l->dst_y_after_scaler);

		l->dst_y_prefetch = mode_lib->mp.dst_y_prefetch[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_vm_vblank = mode_lib->mp.dst_y_per_vm_vblank[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_row_vblank = mode_lib->mp.dst_y_per_row_vblank[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_vm_flip = mode_lib->mp.dst_y_per_vm_flip[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_row_flip = mode_lib->mp.dst_y_per_row_flip[mode_lib->mp.pipe_plane[pipe_idx]];

		dml2_printf("DML_DLG: %s: dst_y_prefetch (after rnd) = %3.2f\n", __func__, l->dst_y_prefetch);
		dml2_printf("DML_DLG: %s: dst_y_per_vm_flip = %3.2f\n", __func__, l->dst_y_per_vm_flip);
		dml2_printf("DML_DLG: %s: dst_y_per_row_flip = %3.2f\n", __func__, l->dst_y_per_row_flip);
		dml2_printf("DML_DLG: %s: dst_y_per_vm_vblank = %3.2f\n", __func__, l->dst_y_per_vm_vblank);
		dml2_printf("DML_DLG: %s: dst_y_per_row_vblank = %3.2f\n", __func__, l->dst_y_per_row_vblank);

		if (l->dst_y_prefetch > 0 && l->dst_y_per_vm_vblank > 0 && l->dst_y_per_row_vblank > 0) {
			DML2_ASSERT(l->dst_y_prefetch > (l->dst_y_per_vm_vblank + l->dst_y_per_row_vblank));
		}

		l->vratio_pre_l = mode_lib->mp.VRatioPrefetchY[mode_lib->mp.pipe_plane[pipe_idx]];
		l->vratio_pre_c = mode_lib->mp.VRatioPrefetchC[mode_lib->mp.pipe_plane[pipe_idx]];

		dml2_printf("DML_DLG: %s: vratio_pre_l = %3.2f\n", __func__, l->vratio_pre_l);
		dml2_printf("DML_DLG: %s: vratio_pre_c = %3.2f\n", __func__, l->vratio_pre_c);

		// Active
		l->refcyc_per_line_delivery_pre_l = mode_lib->mp.DisplayPipeLineDeliveryTimeLumaPrefetch[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_line_delivery_l = mode_lib->mp.DisplayPipeLineDeliveryTimeLuma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

		dml2_printf("DML_DLG: %s: refcyc_per_line_delivery_pre_l = %3.2f\n", __func__, l->refcyc_per_line_delivery_pre_l);
		dml2_printf("DML_DLG: %s: refcyc_per_line_delivery_l = %3.2f\n", __func__, l->refcyc_per_line_delivery_l);

		l->refcyc_per_line_delivery_pre_c = 0.0;
		l->refcyc_per_line_delivery_c = 0.0;

		if (l->dual_plane) {
			l->refcyc_per_line_delivery_pre_c = mode_lib->mp.DisplayPipeLineDeliveryTimeChromaPrefetch[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
			l->refcyc_per_line_delivery_c = mode_lib->mp.DisplayPipeLineDeliveryTimeChroma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

			dml2_printf("DML_DLG: %s: refcyc_per_line_delivery_pre_c = %3.2f\n", __func__, l->refcyc_per_line_delivery_pre_c);
			dml2_printf("DML_DLG: %s: refcyc_per_line_delivery_c = %3.2f\n", __func__, l->refcyc_per_line_delivery_c);
		}

		disp_dlg_regs->refcyc_per_vm_dmdata = (unsigned int)(mode_lib->mp.Tdmdl_vm[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz);
		disp_dlg_regs->dmdata_dl_delta = (unsigned int)(mode_lib->mp.Tdmdl[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz);

		l->refcyc_per_req_delivery_pre_l = mode_lib->mp.DisplayPipeRequestDeliveryTimeLumaPrefetch[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_req_delivery_l = mode_lib->mp.DisplayPipeRequestDeliveryTimeLuma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

		dml2_printf("DML_DLG: %s: refcyc_per_req_delivery_pre_l = %3.2f\n", __func__, l->refcyc_per_req_delivery_pre_l);
		dml2_printf("DML_DLG: %s: refcyc_per_req_delivery_l = %3.2f\n", __func__, l->refcyc_per_req_delivery_l);

		l->refcyc_per_req_delivery_pre_c = 0.0;
		l->refcyc_per_req_delivery_c = 0.0;
		if (l->dual_plane) {
			l->refcyc_per_req_delivery_pre_c = mode_lib->mp.DisplayPipeRequestDeliveryTimeChromaPrefetch[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
			l->refcyc_per_req_delivery_c = mode_lib->mp.DisplayPipeRequestDeliveryTimeChroma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

			dml2_printf("DML_DLG: %s: refcyc_per_req_delivery_pre_c = %3.2f\n", __func__, l->refcyc_per_req_delivery_pre_c);
			dml2_printf("DML_DLG: %s: refcyc_per_req_delivery_c = %3.2f\n", __func__, l->refcyc_per_req_delivery_c);
		}

		// TTU - Cursor
		DML2_ASSERT(display_cfg->plane_descriptors[l->plane_idx].cursor.num_cursors <= 1);

		// Assign to register structures
		disp_dlg_regs->min_dst_y_next_start = (unsigned int)((double)l->min_dst_y_next_start * math_pow(2, 2));
		DML2_ASSERT(disp_dlg_regs->min_dst_y_next_start < (unsigned int)math_pow(2, 18));

		disp_dlg_regs->dst_y_after_scaler = l->dst_y_after_scaler; // in terms of line
		disp_dlg_regs->refcyc_x_after_scaler = (unsigned int)((double)l->dst_x_after_scaler * l->ref_freq_to_pix_freq); // in terms of refclk
		disp_dlg_regs->dst_y_prefetch = (unsigned int)(l->dst_y_prefetch * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_vm_vblank = (unsigned int)(l->dst_y_per_vm_vblank * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_row_vblank = (unsigned int)(l->dst_y_per_row_vblank * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_vm_flip = (unsigned int)(l->dst_y_per_vm_flip * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_row_flip = (unsigned int)(l->dst_y_per_row_flip * math_pow(2, 2));

		disp_dlg_regs->vratio_prefetch = (unsigned int)(l->vratio_pre_l * math_pow(2, 19));
		disp_dlg_regs->vratio_prefetch_c = (unsigned int)(l->vratio_pre_c * math_pow(2, 19));

		dml2_printf("DML_DLG: %s: disp_dlg_regs->dst_y_per_vm_vblank = 0x%x\n", __func__, disp_dlg_regs->dst_y_per_vm_vblank);
		dml2_printf("DML_DLG: %s: disp_dlg_regs->dst_y_per_row_vblank = 0x%x\n", __func__, disp_dlg_regs->dst_y_per_row_vblank);
		dml2_printf("DML_DLG: %s: disp_dlg_regs->dst_y_per_vm_flip = 0x%x\n", __func__, disp_dlg_regs->dst_y_per_vm_flip);
		dml2_printf("DML_DLG: %s: disp_dlg_regs->dst_y_per_row_flip = 0x%x\n", __func__, disp_dlg_regs->dst_y_per_row_flip);

		disp_dlg_regs->refcyc_per_vm_group_vblank = (unsigned int)(mode_lib->mp.TimePerVMGroupVBlank[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz);
		disp_dlg_regs->refcyc_per_vm_group_flip = (unsigned int)(mode_lib->mp.TimePerVMGroupFlip[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz);
		disp_dlg_regs->refcyc_per_vm_req_vblank = (unsigned int)(mode_lib->mp.TimePerVMRequestVBlank[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz * math_pow(2, 10));
		disp_dlg_regs->refcyc_per_vm_req_flip = (unsigned int)(mode_lib->mp.TimePerVMRequestFlip[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz * math_pow(2, 10));

		l->dst_y_per_pte_row_nom_l = mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_L[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_pte_row_nom_c = mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_C[mode_lib->mp.pipe_plane[pipe_idx]];
		l->refcyc_per_pte_group_nom_l = mode_lib->mp.time_per_pte_group_nom_luma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_pte_group_nom_c = mode_lib->mp.time_per_pte_group_nom_chroma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_pte_group_vblank_l = mode_lib->mp.time_per_pte_group_vblank_luma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_pte_group_vblank_c = mode_lib->mp.time_per_pte_group_vblank_chroma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_pte_group_flip_l = mode_lib->mp.time_per_pte_group_flip_luma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_pte_group_flip_c = mode_lib->mp.time_per_pte_group_flip_chroma[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_tdlut_group = mode_lib->mp.time_per_tdlut_group[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

		disp_dlg_regs->dst_y_per_pte_row_nom_l = (unsigned int)(l->dst_y_per_pte_row_nom_l * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_pte_row_nom_c = (unsigned int)(l->dst_y_per_pte_row_nom_c * math_pow(2, 2));

		disp_dlg_regs->refcyc_per_pte_group_nom_l = (unsigned int)(l->refcyc_per_pte_group_nom_l);
		disp_dlg_regs->refcyc_per_pte_group_nom_c = (unsigned int)(l->refcyc_per_pte_group_nom_c);
		disp_dlg_regs->refcyc_per_pte_group_vblank_l = (unsigned int)(l->refcyc_per_pte_group_vblank_l);
		disp_dlg_regs->refcyc_per_pte_group_vblank_c = (unsigned int)(l->refcyc_per_pte_group_vblank_c);
		disp_dlg_regs->refcyc_per_pte_group_flip_l = (unsigned int)(l->refcyc_per_pte_group_flip_l);
		disp_dlg_regs->refcyc_per_pte_group_flip_c = (unsigned int)(l->refcyc_per_pte_group_flip_c);
		disp_dlg_regs->refcyc_per_line_delivery_pre_l = (unsigned int)math_floor2(l->refcyc_per_line_delivery_pre_l, 1);
		disp_dlg_regs->refcyc_per_line_delivery_l = (unsigned int)math_floor2(l->refcyc_per_line_delivery_l, 1);
		disp_dlg_regs->refcyc_per_line_delivery_pre_c = (unsigned int)math_floor2(l->refcyc_per_line_delivery_pre_c, 1);
		disp_dlg_regs->refcyc_per_line_delivery_c = (unsigned int)math_floor2(l->refcyc_per_line_delivery_c, 1);

		l->dst_y_per_meta_row_nom_l = mode_lib->mp.DST_Y_PER_META_ROW_NOM_L[mode_lib->mp.pipe_plane[pipe_idx]];
		l->dst_y_per_meta_row_nom_c = mode_lib->mp.DST_Y_PER_META_ROW_NOM_C[mode_lib->mp.pipe_plane[pipe_idx]];
		l->refcyc_per_meta_chunk_nom_l = mode_lib->mp.TimePerMetaChunkNominal[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_meta_chunk_nom_c = mode_lib->mp.TimePerChromaMetaChunkNominal[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_meta_chunk_vblank_l = mode_lib->mp.TimePerMetaChunkVBlank[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_meta_chunk_vblank_c = mode_lib->mp.TimePerChromaMetaChunkVBlank[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_meta_chunk_flip_l = mode_lib->mp.TimePerMetaChunkFlip[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;
		l->refcyc_per_meta_chunk_flip_c = mode_lib->mp.TimePerChromaMetaChunkFlip[mode_lib->mp.pipe_plane[pipe_idx]] * l->refclk_freq_in_mhz;

		disp_dlg_regs->dst_y_per_meta_row_nom_l = (unsigned int)(l->dst_y_per_meta_row_nom_l * math_pow(2, 2));
		disp_dlg_regs->dst_y_per_meta_row_nom_c = (unsigned int)(l->dst_y_per_meta_row_nom_c * math_pow(2, 2));
		disp_dlg_regs->refcyc_per_meta_chunk_nom_l = (unsigned int)(l->refcyc_per_meta_chunk_nom_l);
		disp_dlg_regs->refcyc_per_meta_chunk_nom_c = (unsigned int)(l->refcyc_per_meta_chunk_nom_c);
		disp_dlg_regs->refcyc_per_meta_chunk_vblank_l = (unsigned int)(l->refcyc_per_meta_chunk_vblank_l);
		disp_dlg_regs->refcyc_per_meta_chunk_vblank_c = (unsigned int)(l->refcyc_per_meta_chunk_vblank_c);
		disp_dlg_regs->refcyc_per_meta_chunk_flip_l = (unsigned int)(l->refcyc_per_meta_chunk_flip_l);
		disp_dlg_regs->refcyc_per_meta_chunk_flip_c = (unsigned int)(l->refcyc_per_meta_chunk_flip_c);

		disp_dlg_regs->refcyc_per_tdlut_group = (unsigned int)(l->refcyc_per_tdlut_group);
		disp_dlg_regs->dst_y_delta_drq_limit = 0x7fff; // off

		disp_ttu_regs->refcyc_per_req_delivery_pre_l = (unsigned int)(l->refcyc_per_req_delivery_pre_l * math_pow(2, 10));
		disp_ttu_regs->refcyc_per_req_delivery_l = (unsigned int)(l->refcyc_per_req_delivery_l * math_pow(2, 10));
		disp_ttu_regs->refcyc_per_req_delivery_pre_c = (unsigned int)(l->refcyc_per_req_delivery_pre_c * math_pow(2, 10));
		disp_ttu_regs->refcyc_per_req_delivery_c = (unsigned int)(l->refcyc_per_req_delivery_c * math_pow(2, 10));
		disp_ttu_regs->qos_level_low_wm = 0;

		disp_ttu_regs->qos_level_high_wm = (unsigned int)(4.0 * (double)l->htotal * l->ref_freq_to_pix_freq);

		disp_ttu_regs->qos_level_flip = 14;
		disp_ttu_regs->qos_level_fixed_l = 8;
		disp_ttu_regs->qos_level_fixed_c = 8;
		disp_ttu_regs->qos_ramp_disable_l = 0;
		disp_ttu_regs->qos_ramp_disable_c = 0;
		disp_ttu_regs->min_ttu_vblank = (unsigned int)(l->min_ttu_vblank * l->refclk_freq_in_mhz);

		// CHECK for HW registers' range, DML2_ASSERT or clamp
		DML2_ASSERT(l->refcyc_per_req_delivery_pre_l < math_pow(2, 13));
		DML2_ASSERT(l->refcyc_per_req_delivery_l < math_pow(2, 13));
		DML2_ASSERT(l->refcyc_per_req_delivery_pre_c < math_pow(2, 13));
		DML2_ASSERT(l->refcyc_per_req_delivery_c < math_pow(2, 13));
		if (disp_dlg_regs->refcyc_per_vm_group_vblank >= (unsigned int)math_pow(2, 23))
			disp_dlg_regs->refcyc_per_vm_group_vblank = (unsigned int)(math_pow(2, 23) - 1);

		if (disp_dlg_regs->refcyc_per_vm_group_flip >= (unsigned int)math_pow(2, 23))
			disp_dlg_regs->refcyc_per_vm_group_flip = (unsigned int)(math_pow(2, 23) - 1);

		if (disp_dlg_regs->refcyc_per_vm_req_vblank >= (unsigned int)math_pow(2, 23))
			disp_dlg_regs->refcyc_per_vm_req_vblank = (unsigned int)(math_pow(2, 23) - 1);

		if (disp_dlg_regs->refcyc_per_vm_req_flip >= (unsigned int)math_pow(2, 23))
			disp_dlg_regs->refcyc_per_vm_req_flip = (unsigned int)(math_pow(2, 23) - 1);


		DML2_ASSERT(disp_dlg_regs->dst_y_after_scaler < (unsigned int)8);
		DML2_ASSERT(disp_dlg_regs->refcyc_x_after_scaler < (unsigned int)math_pow(2, 13));

		if (disp_dlg_regs->dst_y_per_pte_row_nom_l >= (unsigned int)math_pow(2, 17)) {
			dml2_printf("DML_DLG: %s: Warning DST_Y_PER_PTE_ROW_NOM_L %u > register max U15.2 %u, clamp to max\n", __func__, disp_dlg_regs->dst_y_per_pte_row_nom_l, (unsigned int)math_pow(2, 17) - 1);
			l->dst_y_per_pte_row_nom_l = (unsigned int)math_pow(2, 17) - 1;
		}
		if (l->dual_plane) {
			if (disp_dlg_regs->dst_y_per_pte_row_nom_c >= (unsigned int)math_pow(2, 17)) {
				dml2_printf("DML_DLG: %s: Warning DST_Y_PER_PTE_ROW_NOM_C %u > register max U15.2 %u, clamp to max\n", __func__, disp_dlg_regs->dst_y_per_pte_row_nom_c, (unsigned int)math_pow(2, 17) - 1);
				l->dst_y_per_pte_row_nom_c = (unsigned int)math_pow(2, 17) - 1;
			}
		}

		if (disp_dlg_regs->refcyc_per_pte_group_nom_l >= (unsigned int)math_pow(2, 23))
			disp_dlg_regs->refcyc_per_pte_group_nom_l = (unsigned int)(math_pow(2, 23) - 1);
		if (l->dual_plane) {
			if (disp_dlg_regs->refcyc_per_pte_group_nom_c >= (unsigned int)math_pow(2, 23))
				disp_dlg_regs->refcyc_per_pte_group_nom_c = (unsigned int)(math_pow(2, 23) - 1);
		}
		DML2_ASSERT(disp_dlg_regs->refcyc_per_pte_group_vblank_l < (unsigned int)math_pow(2, 13));
		if (l->dual_plane) {
			DML2_ASSERT(disp_dlg_regs->refcyc_per_pte_group_vblank_c < (unsigned int)math_pow(2, 13));
		}

		DML2_ASSERT(disp_dlg_regs->refcyc_per_line_delivery_pre_l < (unsigned int)math_pow(2, 13));
		DML2_ASSERT(disp_dlg_regs->refcyc_per_line_delivery_l < (unsigned int)math_pow(2, 13));
		DML2_ASSERT(disp_dlg_regs->refcyc_per_line_delivery_pre_c < (unsigned int)math_pow(2, 13));
		DML2_ASSERT(disp_dlg_regs->refcyc_per_line_delivery_c < (unsigned int)math_pow(2, 13));
		DML2_ASSERT(disp_ttu_regs->qos_level_low_wm < (unsigned int)math_pow(2, 14));
		DML2_ASSERT(disp_ttu_regs->qos_level_high_wm < (unsigned int)math_pow(2, 14));
		DML2_ASSERT(disp_ttu_regs->min_ttu_vblank < (unsigned int)math_pow(2, 24));

		dml2_printf("DML_DLG::%s: Calculation for pipe[%d] done\n", __func__, pipe_idx);

	}
}

static void rq_dlg_get_arb_params(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_display_arb_regs *arb_param)
{
	double refclk_freq_in_mhz = (display_cfg->overrides.hw.dlg_ref_clk_mhz > 0) ? (double)display_cfg->overrides.hw.dlg_ref_clk_mhz : mode_lib->soc.dchub_refclk_mhz;

	arb_param->max_req_outstanding = mode_lib->soc.max_outstanding_reqs;
	arb_param->min_req_outstanding = mode_lib->soc.max_outstanding_reqs; // turn off the sat level feature if this set to max
	arb_param->sdpif_request_rate_limit = (3 * mode_lib->ip.words_per_channel * mode_lib->soc.clk_table.dram_config.channel_count) / 4;
	arb_param->sdpif_request_rate_limit = arb_param->sdpif_request_rate_limit < 96 ? 96 : arb_param->sdpif_request_rate_limit;
	arb_param->sat_level_us = 60;
	arb_param->hvm_max_qos_commit_threshold = 0xf;
	arb_param->hvm_min_req_outstand_commit_threshold = 0xa;
	arb_param->compbuf_reserved_space_kbytes = dml_get_compbuf_reserved_space_64b(mode_lib) * 64 / 1024;
	arb_param->compbuf_size = mode_lib->mp.CompressedBufferSizeInkByte / mode_lib->ip.compressed_buffer_segment_size_in_kbytes;
	arb_param->allow_sdpif_rate_limit_when_cstate_req = dml_get_hw_debug5(mode_lib);
	arb_param->dcfclk_deep_sleep_hysteresis = dml_get_dcfclk_deep_sleep_hysteresis(mode_lib);
	arb_param->pstate_stall_threshold = (unsigned int)(mode_lib->ip_caps.fams2.max_allow_delay_us * refclk_freq_in_mhz);

#ifdef __DML_VBA_DEBUG__
	dml2_printf("DML::%s: max_req_outstanding = %d\n", __func__, arb_param->max_req_outstanding);
	dml2_printf("DML::%s: sdpif_request_rate_limit = %d\n", __func__, arb_param->sdpif_request_rate_limit);
	dml2_printf("DML::%s: compbuf_reserved_space_kbytes = %d\n", __func__, arb_param->compbuf_reserved_space_kbytes);
	dml2_printf("DML::%s: allow_sdpif_rate_limit_when_cstate_req = %d\n", __func__, arb_param->allow_sdpif_rate_limit_when_cstate_req);
	dml2_printf("DML::%s: dcfclk_deep_sleep_hysteresis = %d\n", __func__, arb_param->dcfclk_deep_sleep_hysteresis);
#endif

}

void dml2_core_calcs_get_watermarks(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_dchub_watermark_regs *out)
{
	rq_dlg_get_wm_regs(display_cfg, mode_lib, out);
}

void dml2_core_calcs_get_arb_params(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_display_arb_regs *out)
{
	rq_dlg_get_arb_params(display_cfg, mode_lib, out);
}

void dml2_core_calcs_get_pipe_regs(const struct dml2_display_cfg *display_cfg,
	struct dml2_core_internal_display_mode_lib *mode_lib,
	struct dml2_dchub_per_pipe_register_set *out, int pipe_index)
{
	rq_dlg_get_rq_reg(&out->rq_regs, display_cfg, mode_lib, pipe_index);
	rq_dlg_get_dlg_reg(&mode_lib->scratch, &out->dlg_regs, &out->ttu_regs, display_cfg, mode_lib, pipe_index);
	out->det_size = dml_get_det_buffer_size_kbytes(mode_lib, pipe_index) / mode_lib->ip.config_return_buffer_segment_size_in_kbytes;
}

void dml2_core_calcs_get_global_sync_programming(const struct dml2_core_internal_display_mode_lib *mode_lib, union dml2_global_sync_programming *out, int pipe_index)
{
	out->dcn4x.vready_offset_pixels = dml_get_vready_offset(mode_lib, pipe_index);
	out->dcn4x.vstartup_lines = dml_get_vstartup_calculated(mode_lib, pipe_index);
	out->dcn4x.vupdate_offset_pixels = dml_get_vupdate_offset(mode_lib, pipe_index);
	out->dcn4x.vupdate_vupdate_width_pixels = dml_get_vupdate_width(mode_lib, pipe_index);
	out->dcn4x.pstate_keepout_start_lines = dml_get_pstate_keepout_dst_lines(mode_lib, pipe_index);
}

void dml2_core_calcs_get_stream_programming(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_per_stream_programming *out, int pipe_index)
{
	dml2_core_calcs_get_global_sync_programming(mode_lib, &out->global_sync, pipe_index);
}

void dml2_core_calcs_get_global_fams2_programming(const struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct display_configuation_with_meta *display_cfg,
		struct dmub_cmd_fams2_global_config *fams2_global_config)
{
	fams2_global_config->features.bits.enable = display_cfg->stage3.fams2_required;

	if (fams2_global_config->features.bits.enable) {
		fams2_global_config->features.bits.enable_stall_recovery = true;
		fams2_global_config->features.bits.allow_delay_check_mode = FAMS2_ALLOW_DELAY_CHECK_FROM_START;

		fams2_global_config->max_allow_delay_us = mode_lib->ip_caps.fams2.max_allow_delay_us;
		fams2_global_config->lock_wait_time_us = mode_lib->ip_caps.fams2.lock_timeout_us;
		fams2_global_config->recovery_timeout_us = mode_lib->ip_caps.fams2.recovery_timeout_us;
		fams2_global_config->hwfq_flip_programming_delay_us = mode_lib->ip_caps.fams2.flip_programming_delay_us;

		fams2_global_config->num_streams = display_cfg->display_config.num_streams;
	}
}

void dml2_core_calcs_get_stream_fams2_programming(const struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct display_configuation_with_meta *display_cfg,
		union dmub_cmd_fams2_config *fams2_base_programming,
		union dmub_cmd_fams2_config *fams2_sub_programming,
		enum dml2_pstate_method pstate_method,
		int plane_index)
{
	const struct dml2_plane_parameters *plane_descriptor = &display_cfg->display_config.plane_descriptors[plane_index];
	const struct dml2_stream_parameters *stream_descriptor = &display_cfg->display_config.stream_descriptors[plane_descriptor->stream_index];
	const struct dml2_fams2_meta *stream_fams2_meta = &display_cfg->stage3.stream_fams2_meta[plane_descriptor->stream_index];

	struct dmub_fams2_cmd_stream_static_base_state *base_programming = &fams2_base_programming->stream_v1.base;
	union dmub_fams2_cmd_stream_static_sub_state *sub_programming = &fams2_sub_programming->stream_v1.sub_state;

	unsigned int i;

	if (display_cfg->display_config.overrides.all_streams_blanked) {
		/* stream is blanked, so do nothing */
		return;
	}

	/* from display configuration */
	base_programming->htotal = (uint16_t)stream_descriptor->timing.h_total;
	base_programming->vtotal = (uint16_t)stream_descriptor->timing.v_total;
	base_programming->vblank_start = (uint16_t)(stream_fams2_meta->nom_vtotal -
		stream_descriptor->timing.v_front_porch);
	base_programming->vblank_end = (uint16_t)(stream_fams2_meta->nom_vtotal -
		stream_descriptor->timing.v_front_porch -
		stream_descriptor->timing.v_active);
	base_programming->config.bits.is_drr = stream_descriptor->timing.drr_config.enabled;

	/* from meta */
	base_programming->otg_vline_time_ns =
		(unsigned int)(stream_fams2_meta->otg_vline_time_us * 1000.0);
	base_programming->scheduling_delay_otg_vlines = (uint8_t)stream_fams2_meta->scheduling_delay_otg_vlines;
	base_programming->contention_delay_otg_vlines = (uint8_t)stream_fams2_meta->contention_delay_otg_vlines;
	base_programming->vline_int_ack_delay_otg_vlines = (uint8_t)stream_fams2_meta->vertical_interrupt_ack_delay_otg_vlines;
	base_programming->drr_keepout_otg_vline = (uint16_t)(stream_fams2_meta->nom_vtotal -
		stream_descriptor->timing.v_front_porch -
		stream_fams2_meta->method_drr.programming_delay_otg_vlines);
	base_programming->allow_to_target_delay_otg_vlines = (uint8_t)stream_fams2_meta->allow_to_target_delay_otg_vlines;
	base_programming->max_vtotal = (uint16_t)stream_fams2_meta->max_vtotal;

	/* from core */
	base_programming->config.bits.min_ttu_vblank_usable = true;
	for (i = 0; i < display_cfg->display_config.num_planes; i++) {
		/* check if all planes support p-state in blank */
		if (display_cfg->display_config.plane_descriptors[i].stream_index == plane_descriptor->stream_index &&
				mode_lib->mp.MinTTUVBlank[i] <= mode_lib->mp.Watermark.DRAMClockChangeWatermark) {
			base_programming->config.bits.min_ttu_vblank_usable = false;
			break;
		}
	}

	switch (pstate_method) {
	case dml2_pstate_method_vactive:
	case dml2_pstate_method_fw_vactive_drr:
		/* legacy vactive */
		base_programming->type = FAMS2_STREAM_TYPE_VACTIVE;
		sub_programming->legacy.vactive_det_fill_delay_otg_vlines =
			(uint8_t)stream_fams2_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines;
		base_programming->allow_start_otg_vline =
			(uint16_t)stream_fams2_meta->method_vactive.common.allow_start_otg_vline;
		base_programming->allow_end_otg_vline =
			(uint16_t)stream_fams2_meta->method_vactive.common.allow_end_otg_vline;
		base_programming->config.bits.clamp_vtotal_min = true;
		break;
	case dml2_pstate_method_vblank:
	case dml2_pstate_method_fw_vblank_drr:
		/* legacy vblank */
		base_programming->type = FAMS2_STREAM_TYPE_VBLANK;
		base_programming->allow_start_otg_vline =
			(uint16_t)stream_fams2_meta->method_vblank.common.allow_start_otg_vline;
		base_programming->allow_end_otg_vline =
			(uint16_t)stream_fams2_meta->method_vblank.common.allow_end_otg_vline;
		base_programming->config.bits.clamp_vtotal_min = true;
		break;
	case dml2_pstate_method_fw_drr:
		/* drr */
		base_programming->type = FAMS2_STREAM_TYPE_DRR;
		sub_programming->drr.programming_delay_otg_vlines =
			(uint8_t)stream_fams2_meta->method_drr.programming_delay_otg_vlines;
		sub_programming->drr.nom_stretched_vtotal =
			(uint16_t)stream_fams2_meta->method_drr.stretched_vtotal;
		base_programming->allow_start_otg_vline =
			(uint16_t)stream_fams2_meta->method_drr.common.allow_start_otg_vline;
		base_programming->allow_end_otg_vline =
			(uint16_t)stream_fams2_meta->method_drr.common.allow_end_otg_vline;
		/* drr only clamps to vtotal min for single display */
		base_programming->config.bits.clamp_vtotal_min = display_cfg->display_config.num_streams == 1;
		sub_programming->drr.only_stretch_if_required = true;
		break;
	case dml2_pstate_method_fw_svp:
	case dml2_pstate_method_fw_svp_drr:
		/* subvp */
		base_programming->type = FAMS2_STREAM_TYPE_SUBVP;
		sub_programming->subvp.vratio_numerator =
			(uint16_t)(plane_descriptor->composition.scaler_info.plane0.v_ratio * 1000.0);
		sub_programming->subvp.vratio_denominator = 1000;
		sub_programming->subvp.programming_delay_otg_vlines =
			(uint8_t)stream_fams2_meta->method_subvp.programming_delay_otg_vlines;
		sub_programming->subvp.prefetch_to_mall_otg_vlines =
			(uint8_t)stream_fams2_meta->method_subvp.prefetch_to_mall_delay_otg_vlines;
		sub_programming->subvp.phantom_vtotal =
			(uint16_t)stream_fams2_meta->method_subvp.phantom_vtotal;
		sub_programming->subvp.phantom_vactive =
			(uint16_t)stream_fams2_meta->method_subvp.phantom_vactive;
		sub_programming->subvp.config.bits.is_multi_planar =
			plane_descriptor->surface.plane1.height > 0;
		sub_programming->subvp.config.bits.is_yuv420 =
			plane_descriptor->pixel_format == dml2_420_8 ||
			plane_descriptor->pixel_format == dml2_420_10 ||
			plane_descriptor->pixel_format == dml2_420_12;

		base_programming->allow_start_otg_vline =
			(uint16_t)stream_fams2_meta->method_subvp.common.allow_start_otg_vline;
		base_programming->allow_end_otg_vline =
			(uint16_t)stream_fams2_meta->method_subvp.common.allow_end_otg_vline;
		base_programming->config.bits.clamp_vtotal_min = true;
		break;
	case dml2_pstate_method_reserved_hw:
	case dml2_pstate_method_reserved_fw:
	case dml2_pstate_method_reserved_fw_drr_clamped:
	case dml2_pstate_method_reserved_fw_drr_var:
	case dml2_pstate_method_na:
	case dml2_pstate_method_count:
	default:
		/* this should never happen */
		break;
	}
}

void dml2_core_calcs_get_mcache_allocation(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_mcache_surface_allocation *out, int plane_idx)
{
	unsigned int n;

	out->num_mcaches_plane0 = dml_get_plane_num_mcaches_plane0(mode_lib, plane_idx);
	out->num_mcaches_plane1 = dml_get_plane_num_mcaches_plane1(mode_lib, plane_idx);
	out->shift_granularity.p0 = dml_get_plane_mcache_shift_granularity_plane0(mode_lib, plane_idx);
	out->shift_granularity.p1 = dml_get_plane_mcache_shift_granularity_plane1(mode_lib, plane_idx);

	for (n = 0; n < out->num_mcaches_plane0; n++)
		out->mcache_x_offsets_plane0[n] = dml_get_plane_array_mcache_offsets_plane0(mode_lib, plane_idx, n);

	for (n = 0; n < out->num_mcaches_plane1; n++)
		out->mcache_x_offsets_plane1[n] = dml_get_plane_array_mcache_offsets_plane1(mode_lib, plane_idx, n);

	out->last_slice_sharing.mall_comb_mcache_p0 = dml_get_plane_mall_comb_mcache_l(mode_lib, plane_idx);
	out->last_slice_sharing.mall_comb_mcache_p1 = dml_get_plane_mall_comb_mcache_c(mode_lib, plane_idx);
	out->last_slice_sharing.plane0_plane1 = dml_get_plane_lc_comb_mcache(mode_lib, plane_idx);
	out->informative.meta_row_bytes_plane0 = dml_get_plane_mcache_row_bytes_plane0(mode_lib, plane_idx);
	out->informative.meta_row_bytes_plane1 = dml_get_plane_mcache_row_bytes_plane1(mode_lib, plane_idx);

	out->valid = true;
}

void dml2_core_calcs_get_mall_allocation(struct dml2_core_internal_display_mode_lib *mode_lib, unsigned int *out, int pipe_index)
{
	*out = dml_get_surface_size_in_mall_bytes(mode_lib, pipe_index);
}

void dml2_core_calcs_get_plane_support_info(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct core_plane_support_info *out, int plane_idx)
{
	out->mall_svp_size_requirement_ways = 0;

	out->nominal_vblank_pstate_latency_hiding_us =
		(int)(display_cfg->stream_descriptors[display_cfg->plane_descriptors[plane_idx].stream_index].timing.h_total /
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[plane_idx].stream_index].timing.pixel_clock_khz / 1000) * mode_lib->ms.TWait[plane_idx]);

	out->dram_change_latency_hiding_margin_in_active = (int)mode_lib->ms.VActiveLatencyHidingMargin[plane_idx];

	out->active_latency_hiding_us = (int)mode_lib->ms.VActiveLatencyHidingUs[plane_idx];

	out->dram_change_vactive_det_fill_delay_us = (unsigned int)math_ceil(mode_lib->ms.dram_change_vactive_det_fill_delay_us[plane_idx]);
}

void dml2_core_calcs_get_stream_support_info(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct core_stream_support_info *out, int plane_index)
{
	double phantom_processing_delay_pix;
	unsigned int phantom_processing_delay_lines;
	unsigned int phantom_min_v_active_lines;
	unsigned int phantom_v_active_lines;
	unsigned int phantom_v_startup_lines;
	unsigned int phantom_v_blank_lines;
	unsigned int main_v_blank_lines;
	unsigned int rem;

	phantom_processing_delay_pix = (double)((mode_lib->ip.subvp_fw_processing_delay_us + mode_lib->ip.subvp_pstate_allow_width_us) *
		((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[plane_index].stream_index].timing.pixel_clock_khz / 1000));
	phantom_processing_delay_lines = (unsigned int)(phantom_processing_delay_pix / (double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[plane_index].stream_index].timing.h_total);
	dml2_core_div_rem(phantom_processing_delay_pix,
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[plane_index].stream_index].timing.h_total,
				&rem);
	if (rem)
		phantom_processing_delay_lines++;

	phantom_v_startup_lines = dml_get_plane_max_vstartup_lines(mode_lib, plane_index);
	phantom_min_v_active_lines = (unsigned int)math_ceil((double)dml_get_plane_subviewport_lines_needed_in_mall(mode_lib, plane_index) /
			display_cfg->plane_descriptors[plane_index].composition.scaler_info.plane0.v_ratio);
	phantom_v_active_lines = phantom_processing_delay_lines + phantom_min_v_active_lines + mode_lib->ip.subvp_swath_height_margin_lines;

	// phantom_vblank = max(vbp(vstartup) + vactive + vfp(always 1) + vsync(can be 1), main_vblank)
	phantom_v_blank_lines = phantom_v_startup_lines + 1 + 1;
	main_v_blank_lines = display_cfg->stream_descriptors[display_cfg->plane_descriptors[plane_index].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[plane_index].stream_index].timing.v_active;
	if (phantom_v_blank_lines > main_v_blank_lines)
		phantom_v_blank_lines = main_v_blank_lines;

	out->phantom_v_active = phantom_v_active_lines;
	// phantom_vtotal = vactive + vblank
	out->phantom_v_total = phantom_v_active_lines + phantom_v_blank_lines;

	out->phantom_min_v_active = phantom_min_v_active_lines;
	out->phantom_v_startup = phantom_v_startup_lines;

	out->vblank_reserved_time_us = display_cfg->plane_descriptors[plane_index].overrides.reserved_vblank_time_ns / 1000;
#if defined(__DML_VBA_DEBUG__)
	dml2_printf("DML::%s: subvp_fw_processing_delay_us = %d\n", __func__, mode_lib->ip.subvp_fw_processing_delay_us);
	dml2_printf("DML::%s: subvp_pstate_allow_width_us = %d\n", __func__, mode_lib->ip.subvp_pstate_allow_width_us);
	dml2_printf("DML::%s: subvp_swath_height_margin_lines = %d\n", __func__, mode_lib->ip.subvp_swath_height_margin_lines);
	dml2_printf("DML::%s: vblank_reserved_time_us = %f\n", __func__, out->vblank_reserved_time_us);
#endif
}

void dml2_core_calcs_get_informative(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_display_cfg_programming *out)
{
	unsigned int k, n;

	out->informative.mode_support_info.ModeIsSupported = mode_lib->ms.support.ModeSupport;
	out->informative.mode_support_info.ImmediateFlipSupport = mode_lib->ms.support.ImmediateFlipSupport;
	out->informative.mode_support_info.WritebackLatencySupport = mode_lib->ms.support.WritebackLatencySupport;
	out->informative.mode_support_info.ScaleRatioAndTapsSupport = mode_lib->ms.support.ScaleRatioAndTapsSupport;
	out->informative.mode_support_info.SourceFormatPixelAndScanSupport = mode_lib->ms.support.SourceFormatPixelAndScanSupport;
	out->informative.mode_support_info.P2IWith420 = mode_lib->ms.support.P2IWith420;
	out->informative.mode_support_info.DSCOnlyIfNecessaryWithBPP = false;
	out->informative.mode_support_info.DSC422NativeNotSupported = mode_lib->ms.support.DSC422NativeNotSupported;
	out->informative.mode_support_info.LinkRateDoesNotMatchDPVersion = mode_lib->ms.support.LinkRateDoesNotMatchDPVersion;
	out->informative.mode_support_info.LinkRateForMultistreamNotIndicated = mode_lib->ms.support.LinkRateForMultistreamNotIndicated;
	out->informative.mode_support_info.BPPForMultistreamNotIndicated = mode_lib->ms.support.BPPForMultistreamNotIndicated;
	out->informative.mode_support_info.MultistreamWithHDMIOreDP = mode_lib->ms.support.MultistreamWithHDMIOreDP;
	out->informative.mode_support_info.MSOOrODMSplitWithNonDPLink = mode_lib->ms.support.MSOOrODMSplitWithNonDPLink;
	out->informative.mode_support_info.NotEnoughLanesForMSO = mode_lib->ms.support.NotEnoughLanesForMSO;
	out->informative.mode_support_info.NumberOfOTGSupport = mode_lib->ms.support.NumberOfOTGSupport;
	out->informative.mode_support_info.NumberOfHDMIFRLSupport = mode_lib->ms.support.NumberOfHDMIFRLSupport;
	out->informative.mode_support_info.NumberOfDP2p0Support = mode_lib->ms.support.NumberOfDP2p0Support;
	out->informative.mode_support_info.WritebackScaleRatioAndTapsSupport = mode_lib->ms.support.WritebackScaleRatioAndTapsSupport;
	out->informative.mode_support_info.CursorSupport = mode_lib->ms.support.CursorSupport;
	out->informative.mode_support_info.PitchSupport = mode_lib->ms.support.PitchSupport;
	out->informative.mode_support_info.ViewportExceedsSurface = mode_lib->ms.support.ViewportExceedsSurface;
	out->informative.mode_support_info.ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified = false;
	out->informative.mode_support_info.ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe = mode_lib->ms.support.ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe;
	out->informative.mode_support_info.InvalidCombinationOfMALLUseForPStateAndStaticScreen = mode_lib->ms.support.InvalidCombinationOfMALLUseForPStateAndStaticScreen;
	out->informative.mode_support_info.InvalidCombinationOfMALLUseForPState = mode_lib->ms.support.InvalidCombinationOfMALLUseForPState;
	out->informative.mode_support_info.ExceededMALLSize = mode_lib->ms.support.ExceededMALLSize;
	out->informative.mode_support_info.EnoughWritebackUnits = mode_lib->ms.support.EnoughWritebackUnits;
	out->informative.mode_support_info.temp_read_or_ppt_support = mode_lib->ms.support.temp_read_or_ppt_support;
	out->informative.mode_support_info.g6_temp_read_support = mode_lib->ms.support.g6_temp_read_support;

	out->informative.mode_support_info.ExceededMultistreamSlots = mode_lib->ms.support.ExceededMultistreamSlots;
	out->informative.mode_support_info.NotEnoughDSCUnits = mode_lib->ms.support.NotEnoughDSCUnits;
	out->informative.mode_support_info.NotEnoughDSCSlices = mode_lib->ms.support.NotEnoughDSCSlices;
	out->informative.mode_support_info.PixelsPerLinePerDSCUnitSupport = mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport;
	out->informative.mode_support_info.DSCCLKRequiredMoreThanSupported = mode_lib->ms.support.DSCCLKRequiredMoreThanSupported;
	out->informative.mode_support_info.DTBCLKRequiredMoreThanSupported = mode_lib->ms.support.DTBCLKRequiredMoreThanSupported;
	out->informative.mode_support_info.LinkCapacitySupport = mode_lib->ms.support.LinkCapacitySupport;

	out->informative.mode_support_info.ROBSupport = mode_lib->ms.support.ROBSupport;
	out->informative.mode_support_info.OutstandingRequestsSupport = mode_lib->ms.support.OutstandingRequestsSupport;
	out->informative.mode_support_info.OutstandingRequestsUrgencyAvoidance = mode_lib->ms.support.OutstandingRequestsUrgencyAvoidance;
	out->informative.mode_support_info.PTEBufferSizeNotExceeded = mode_lib->ms.support.PTEBufferSizeNotExceeded;
	out->informative.mode_support_info.DCCMetaBufferSizeNotExceeded = mode_lib->ms.support.DCCMetaBufferSizeNotExceeded;

	out->informative.mode_support_info.TotalVerticalActiveBandwidthSupport = mode_lib->ms.support.AvgBandwidthSupport;
	out->informative.mode_support_info.VActiveBandwidthSupport = mode_lib->ms.support.UrgVactiveBandwidthSupport;
	out->informative.mode_support_info.USRRetrainingSupport = mode_lib->ms.support.USRRetrainingSupport;

	out->informative.mode_support_info.PrefetchSupported = mode_lib->ms.support.PrefetchSupported;
	out->informative.mode_support_info.DynamicMetadataSupported = mode_lib->ms.support.DynamicMetadataSupported;
	out->informative.mode_support_info.VRatioInPrefetchSupported = mode_lib->ms.support.VRatioInPrefetchSupported;
	out->informative.mode_support_info.DISPCLK_DPPCLK_Support = mode_lib->ms.support.DISPCLK_DPPCLK_Support;
	out->informative.mode_support_info.TotalAvailablePipesSupport = mode_lib->ms.support.TotalAvailablePipesSupport;
	out->informative.mode_support_info.ViewportSizeSupport = mode_lib->ms.support.ViewportSizeSupport;

	for (k = 0; k < out->display_config.num_planes; k++) {

		out->informative.mode_support_info.FCLKChangeSupport[k] = mode_lib->ms.support.FCLKChangeSupport[k];
		out->informative.mode_support_info.MPCCombineEnable[k] = mode_lib->ms.support.MPCCombineEnable[k];
		out->informative.mode_support_info.ODMMode[k] = mode_lib->ms.support.ODMMode[k];
		out->informative.mode_support_info.DPPPerSurface[k] = mode_lib->ms.support.DPPPerSurface[k];
		out->informative.mode_support_info.DSCEnabled[k] = mode_lib->ms.support.DSCEnabled[k];
		out->informative.mode_support_info.FECEnabled[k] = mode_lib->ms.support.FECEnabled[k];
		out->informative.mode_support_info.NumberOfDSCSlices[k] = mode_lib->ms.support.NumberOfDSCSlices[k];
		out->informative.mode_support_info.OutputBpp[k] = mode_lib->ms.support.OutputBpp[k];

		if (mode_lib->ms.support.OutputType[k] == dml2_core_internal_output_type_unknown)
			out->informative.mode_support_info.OutputType[k] = dml2_output_type_unknown;
		else if (mode_lib->ms.support.OutputType[k] == dml2_core_internal_output_type_dp)
			out->informative.mode_support_info.OutputType[k] = dml2_output_type_dp;
		else if (mode_lib->ms.support.OutputType[k] == dml2_core_internal_output_type_edp)
			out->informative.mode_support_info.OutputType[k] = dml2_output_type_edp;
		else if (mode_lib->ms.support.OutputType[k] == dml2_core_internal_output_type_dp2p0)
			out->informative.mode_support_info.OutputType[k] = dml2_output_type_dp2p0;
		else if (mode_lib->ms.support.OutputType[k] == dml2_core_internal_output_type_hdmi)
			out->informative.mode_support_info.OutputType[k] = dml2_output_type_hdmi;
		else if (mode_lib->ms.support.OutputType[k] == dml2_core_internal_output_type_hdmifrl)
			out->informative.mode_support_info.OutputType[k] = dml2_output_type_hdmifrl;

		if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_unknown)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_unknown;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_dp_rate_hbr)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_dp_rate_hbr;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_dp_rate_hbr2)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_dp_rate_hbr2;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_dp_rate_hbr3)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_dp_rate_hbr3;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_dp_rate_uhbr10)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_dp_rate_uhbr10;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_dp_rate_uhbr13p5)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_dp_rate_uhbr13p5;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_dp_rate_uhbr20)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_dp_rate_uhbr20;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_hdmi_rate_3x3)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_hdmi_rate_3x3;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_hdmi_rate_6x3)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_hdmi_rate_6x3;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_hdmi_rate_6x4)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_hdmi_rate_6x4;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_hdmi_rate_8x4)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_hdmi_rate_8x4;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_hdmi_rate_10x4)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_hdmi_rate_10x4;
		else if (mode_lib->ms.support.OutputRate[k] == dml2_core_internal_output_rate_hdmi_rate_12x4)
			out->informative.mode_support_info.OutputRate[k] = dml2_output_rate_hdmi_rate_12x4;

		out->informative.mode_support_info.AlignedYPitch[k] = mode_lib->ms.support.AlignedYPitch[k];
		out->informative.mode_support_info.AlignedCPitch[k] = mode_lib->ms.support.AlignedCPitch[k];
	}

	out->informative.watermarks.urgent_us = dml_get_wm_urgent(mode_lib);
	out->informative.watermarks.writeback_urgent_us = dml_get_wm_writeback_urgent(mode_lib);
	out->informative.watermarks.writeback_pstate_us = dml_get_wm_writeback_dram_clock_change(mode_lib);
	out->informative.watermarks.writeback_fclk_pstate_us = dml_get_wm_writeback_fclk_change(mode_lib);

	out->informative.watermarks.cstate_exit_us = dml_get_wm_stutter_exit(mode_lib);
	out->informative.watermarks.cstate_enter_plus_exit_us = dml_get_wm_stutter_enter_exit(mode_lib);
	out->informative.watermarks.z8_cstate_exit_us = dml_get_wm_z8_stutter_exit(mode_lib);
	out->informative.watermarks.z8_cstate_enter_plus_exit_us = dml_get_wm_z8_stutter_enter_exit(mode_lib);
	out->informative.watermarks.pstate_change_us = dml_get_wm_dram_clock_change(mode_lib);
	out->informative.watermarks.fclk_pstate_change_us = dml_get_wm_fclk_change(mode_lib);
	out->informative.watermarks.usr_retraining_us = dml_get_wm_usr_retraining(mode_lib);
	out->informative.watermarks.temp_read_or_ppt_watermark_us = dml_get_wm_temp_read_or_ppt(mode_lib);

	out->informative.mall.total_surface_size_in_mall_bytes = 0;
	for (k = 0; k < out->display_config.num_planes; ++k)
		out->informative.mall.total_surface_size_in_mall_bytes += mode_lib->mp.SurfaceSizeInTheMALL[k];

	out->informative.qos.min_return_latency_in_dcfclk = mode_lib->mp.min_return_latency_in_dcfclk;
	out->informative.qos.urgent_latency_us = dml_get_urgent_latency(mode_lib);

	out->informative.qos.max_urgent_latency_us = dml_get_max_urgent_latency_us(mode_lib);
	out->informative.qos.avg_non_urgent_latency_us = dml_get_avg_non_urgent_latency_us(mode_lib);
	out->informative.qos.avg_urgent_latency_us = dml_get_avg_urgent_latency_us(mode_lib);

	out->informative.qos.wm_memory_trip_us = dml_get_wm_memory_trip(mode_lib);
	out->informative.qos.meta_trip_memory_us = dml_get_meta_trip_memory_us(mode_lib);
	out->informative.qos.fraction_of_urgent_bandwidth = dml_get_fraction_of_urgent_bandwidth(mode_lib);
	out->informative.qos.fraction_of_urgent_bandwidth_immediate_flip = dml_get_fraction_of_urgent_bandwidth_imm_flip(mode_lib);
	out->informative.qos.fraction_of_urgent_bandwidth_mall = dml_get_fraction_of_urgent_bandwidth_mall(mode_lib);

	out->informative.qos.avg_bw_required.sys_active.sdp_bw_mbps = dml_get_sys_active_avg_bw_required_sdp(mode_lib);
	out->informative.qos.avg_bw_required.sys_active.dram_bw_mbps = dml_get_sys_active_avg_bw_required_dram(mode_lib);
	out->informative.qos.avg_bw_required.svp_prefetch.sdp_bw_mbps = dml_get_svp_prefetch_avg_bw_required_sdp(mode_lib);
	out->informative.qos.avg_bw_required.svp_prefetch.dram_bw_mbps = dml_get_svp_prefetch_avg_bw_required_dram(mode_lib);

	out->informative.qos.avg_bw_available.sys_active.sdp_bw_mbps = dml_get_sys_active_avg_bw_available_sdp(mode_lib);
	out->informative.qos.avg_bw_available.sys_active.dram_bw_mbps = dml_get_sys_active_avg_bw_available_dram(mode_lib);
	out->informative.qos.avg_bw_available.svp_prefetch.sdp_bw_mbps = dml_get_svp_prefetch_avg_bw_available_sdp(mode_lib);
	out->informative.qos.avg_bw_available.svp_prefetch.dram_bw_mbps = dml_get_svp_prefetch_avg_bw_available_dram(mode_lib);

	out->informative.qos.urg_bw_available.sys_active.sdp_bw_mbps = dml_get_sys_active_urg_bw_available_sdp(mode_lib);
	out->informative.qos.urg_bw_available.sys_active.dram_bw_mbps = dml_get_sys_active_urg_bw_available_dram(mode_lib);
	out->informative.qos.urg_bw_available.sys_active.dram_vm_only_bw_mbps = dml_get_sys_active_urg_bw_available_dram_vm_only(mode_lib);

	out->informative.qos.urg_bw_available.svp_prefetch.sdp_bw_mbps = dml_get_svp_prefetch_urg_bw_available_sdp(mode_lib);
	out->informative.qos.urg_bw_available.svp_prefetch.dram_bw_mbps = dml_get_svp_prefetch_urg_bw_available_dram(mode_lib);
	out->informative.qos.urg_bw_available.svp_prefetch.dram_vm_only_bw_mbps = dml_get_svp_prefetch_urg_bw_available_dram_vm_only(mode_lib);

	out->informative.qos.urg_bw_required.sys_active.sdp_bw_mbps = dml_get_sys_active_urg_bw_required_sdp(mode_lib);
	out->informative.qos.urg_bw_required.sys_active.dram_bw_mbps = dml_get_sys_active_urg_bw_required_dram(mode_lib);
	out->informative.qos.urg_bw_required.svp_prefetch.sdp_bw_mbps = dml_get_svp_prefetch_urg_bw_required_sdp(mode_lib);
	out->informative.qos.urg_bw_required.svp_prefetch.dram_bw_mbps = dml_get_svp_prefetch_urg_bw_required_dram(mode_lib);

	out->informative.qos.non_urg_bw_required.sys_active.sdp_bw_mbps = dml_get_sys_active_non_urg_required_sdp(mode_lib);
	out->informative.qos.non_urg_bw_required.sys_active.dram_bw_mbps = dml_get_sys_active_non_urg_required_dram(mode_lib);
	out->informative.qos.non_urg_bw_required.svp_prefetch.sdp_bw_mbps = dml_get_svp_prefetch_non_urg_bw_required_sdp(mode_lib);
	out->informative.qos.non_urg_bw_required.svp_prefetch.dram_bw_mbps = dml_get_svp_prefetch_non_urg_bw_required_dram(mode_lib);

	out->informative.qos.urg_bw_required_with_flip.sys_active.sdp_bw_mbps = dml_get_sys_active_urg_bw_required_sdp_flip(mode_lib);
	out->informative.qos.urg_bw_required_with_flip.sys_active.dram_bw_mbps = dml_get_sys_active_urg_bw_required_dram_flip(mode_lib);
	out->informative.qos.urg_bw_required_with_flip.svp_prefetch.sdp_bw_mbps = dml_get_svp_prefetch_urg_bw_required_sdp_flip(mode_lib);
	out->informative.qos.urg_bw_required_with_flip.svp_prefetch.dram_bw_mbps = dml_get_svp_prefetch_urg_bw_required_dram_flip(mode_lib);

	out->informative.qos.non_urg_bw_required_with_flip.sys_active.sdp_bw_mbps = dml_get_sys_active_non_urg_required_sdp_flip(mode_lib);
	out->informative.qos.non_urg_bw_required_with_flip.sys_active.dram_bw_mbps = dml_get_sys_active_non_urg_required_dram_flip(mode_lib);
	out->informative.qos.non_urg_bw_required_with_flip.svp_prefetch.sdp_bw_mbps = dml_get_svp_prefetch_non_urg_bw_required_sdp_flip(mode_lib);
	out->informative.qos.non_urg_bw_required_with_flip.svp_prefetch.dram_bw_mbps = dml_get_svp_prefetch_non_urg_bw_required_dram_flip(mode_lib);

	out->informative.crb.comp_buffer_size_kbytes = dml_get_comp_buffer_size_kbytes(mode_lib);
	out->informative.crb.UnboundedRequestEnabled = dml_get_unbounded_request_enabled(mode_lib);

	out->informative.crb.compbuf_reserved_space_64b = dml_get_compbuf_reserved_space_64b(mode_lib);
	out->informative.misc.hw_debug5 = dml_get_hw_debug5(mode_lib);
	out->informative.misc.dcfclk_deep_sleep_hysteresis = dml_get_dcfclk_deep_sleep_hysteresis(mode_lib);

	out->informative.power_management.stutter_efficiency = dml_get_stutter_efficiency_no_vblank(mode_lib);
	out->informative.power_management.stutter_efficiency_with_vblank = dml_get_stutter_efficiency(mode_lib);
	out->informative.power_management.stutter_num_bursts = dml_get_stutter_num_bursts(mode_lib);

	out->informative.power_management.z8.stutter_efficiency = dml_get_stutter_efficiency_z8(mode_lib);
	out->informative.power_management.z8.stutter_efficiency_with_vblank = dml_get_stutter_efficiency(mode_lib);
	out->informative.power_management.z8.stutter_num_bursts = dml_get_stutter_num_bursts_z8(mode_lib);
	out->informative.power_management.z8.stutter_period = dml_get_stutter_period(mode_lib);

	out->informative.power_management.z8.bestcase.stutter_efficiency = dml_get_stutter_efficiency_z8_bestcase(mode_lib);
	out->informative.power_management.z8.bestcase.stutter_num_bursts = dml_get_stutter_num_bursts_z8_bestcase(mode_lib);
	out->informative.power_management.z8.bestcase.stutter_period = dml_get_stutter_period_bestcase(mode_lib);

	out->informative.misc.cstate_max_cap_mode = dml_get_cstate_max_cap_mode(mode_lib);

	out->min_clocks.dcn4x.dpprefclk_khz = (int unsigned)dml_get_global_dppclk_khz(mode_lib);

	out->informative.qos.max_active_fclk_change_latency_supported = dml_get_fclk_change_latency(mode_lib);

	out->informative.misc.LowestPrefetchMargin = 10 * 1000 * 1000;

	for (k = 0; k < out->display_config.num_planes; k++) {

		if ((out->display_config.plane_descriptors->overrides.reserved_vblank_time_ns >= 1000.0 * mode_lib->soc.power_management_parameters.dram_clk_change_blackout_us)
			&& (out->display_config.plane_descriptors->overrides.reserved_vblank_time_ns >= 1000.0 * mode_lib->soc.power_management_parameters.fclk_change_blackout_us)
			&& (out->display_config.plane_descriptors->overrides.reserved_vblank_time_ns >= 1000.0 * mode_lib->soc.power_management_parameters.stutter_enter_plus_exit_latency_us))
			out->informative.misc.PrefetchMode[k] = 0;
		else if ((out->display_config.plane_descriptors->overrides.reserved_vblank_time_ns >= 1000.0 * mode_lib->soc.power_management_parameters.fclk_change_blackout_us)
			&& (out->display_config.plane_descriptors->overrides.reserved_vblank_time_ns >= 1000.0 * mode_lib->soc.power_management_parameters.stutter_enter_plus_exit_latency_us))
			out->informative.misc.PrefetchMode[k] = 1;
		else if (out->display_config.plane_descriptors->overrides.reserved_vblank_time_ns >= 1000.0 * mode_lib->soc.power_management_parameters.stutter_enter_plus_exit_latency_us)
			out->informative.misc.PrefetchMode[k] = 2;
		else
			out->informative.misc.PrefetchMode[k] = 3;

		out->informative.misc.min_ttu_vblank_us[k] = mode_lib->mp.MinTTUVBlank[k];
		out->informative.mall.subviewport_lines_needed_in_mall[k] = mode_lib->mp.SubViewportLinesNeededInMALL[k];
		out->informative.crb.det_size_in_kbytes[k] = mode_lib->mp.DETBufferSizeInKByte[k];
		out->informative.crb.DETBufferSizeY[k] = mode_lib->mp.DETBufferSizeY[k];
		out->informative.misc.ImmediateFlipSupportedForPipe[k] = mode_lib->mp.ImmediateFlipSupportedForPipe[k];
		out->informative.misc.UsesMALLForStaticScreen[k] = mode_lib->mp.is_using_mall_for_ss[k];
		out->informative.plane_info[k].dpte_row_height_plane0 = mode_lib->mp.dpte_row_height[k];
		out->informative.plane_info[k].dpte_row_height_plane1 = mode_lib->mp.dpte_row_height_chroma[k];
		out->informative.plane_info[k].meta_row_height_plane0 = mode_lib->mp.meta_row_height[k];
		out->informative.plane_info[k].meta_row_height_plane1 = mode_lib->mp.meta_row_height_chroma[k];
		out->informative.dcc_control[k].max_uncompressed_block_plane0 = mode_lib->mp.DCCYMaxUncompressedBlock[k];
		out->informative.dcc_control[k].max_compressed_block_plane0 = mode_lib->mp.DCCYMaxCompressedBlock[k];
		out->informative.dcc_control[k].independent_block_plane0 = mode_lib->mp.DCCYIndependentBlock[k];
		out->informative.dcc_control[k].max_uncompressed_block_plane1 = mode_lib->mp.DCCCMaxUncompressedBlock[k];
		out->informative.dcc_control[k].max_compressed_block_plane1 = mode_lib->mp.DCCCMaxCompressedBlock[k];
		out->informative.dcc_control[k].independent_block_plane1 = mode_lib->mp.DCCCIndependentBlock[k];
		out->informative.misc.dst_x_after_scaler[k] = mode_lib->mp.DSTXAfterScaler[k];
		out->informative.misc.dst_y_after_scaler[k] = mode_lib->mp.DSTYAfterScaler[k];
		out->informative.misc.prefetch_source_lines_plane0[k] = mode_lib->mp.PrefetchSourceLinesY[k];
		out->informative.misc.prefetch_source_lines_plane1[k] = mode_lib->mp.PrefetchSourceLinesC[k];
		out->informative.misc.vready_at_or_after_vsync[k] = mode_lib->mp.VREADY_AT_OR_AFTER_VSYNC[k];
		out->informative.misc.min_dst_y_next_start[k] = mode_lib->mp.MIN_DST_Y_NEXT_START[k];
		out->informative.plane_info[k].swath_width_plane0 = mode_lib->mp.SwathWidthY[k];
		out->informative.plane_info[k].swath_height_plane0 = mode_lib->mp.SwathHeightY[k];
		out->informative.plane_info[k].swath_height_plane1 = mode_lib->mp.SwathHeightC[k];
		out->informative.misc.CursorDstXOffset[k] = mode_lib->mp.CursorDstXOffset[k];
		out->informative.misc.CursorDstYOffset[k] = mode_lib->mp.CursorDstYOffset[k];
		out->informative.misc.CursorChunkHDLAdjust[k] = mode_lib->mp.CursorChunkHDLAdjust[k];
		out->informative.misc.dpte_group_bytes[k] = mode_lib->mp.dpte_group_bytes[k];
		out->informative.misc.vm_group_bytes[k] = mode_lib->mp.vm_group_bytes[k];
		out->informative.misc.DisplayPipeRequestDeliveryTimeLuma[k] = mode_lib->mp.DisplayPipeRequestDeliveryTimeLuma[k];
		out->informative.misc.DisplayPipeRequestDeliveryTimeChroma[k] = mode_lib->mp.DisplayPipeRequestDeliveryTimeChroma[k];
		out->informative.misc.DisplayPipeRequestDeliveryTimeLumaPrefetch[k] = mode_lib->mp.DisplayPipeRequestDeliveryTimeLumaPrefetch[k];
		out->informative.misc.DisplayPipeRequestDeliveryTimeChromaPrefetch[k] = mode_lib->mp.DisplayPipeRequestDeliveryTimeChromaPrefetch[k];
		out->informative.misc.TimePerVMGroupVBlank[k] = mode_lib->mp.TimePerVMGroupVBlank[k];
		out->informative.misc.TimePerVMGroupFlip[k] = mode_lib->mp.TimePerVMGroupFlip[k];
		out->informative.misc.TimePerVMRequestVBlank[k] = mode_lib->mp.TimePerVMRequestVBlank[k];
		out->informative.misc.TimePerVMRequestFlip[k] = mode_lib->mp.TimePerVMRequestFlip[k];
		out->informative.misc.Tdmdl_vm[k] = mode_lib->mp.Tdmdl_vm[k];
		out->informative.misc.Tdmdl[k] = mode_lib->mp.Tdmdl[k];
		out->informative.misc.VStartup[k] = mode_lib->mp.VStartup[k];
		out->informative.misc.VUpdateOffsetPix[k] = mode_lib->mp.VUpdateOffsetPix[k];
		out->informative.misc.VUpdateWidthPix[k] = mode_lib->mp.VUpdateWidthPix[k];
		out->informative.misc.VReadyOffsetPix[k] = mode_lib->mp.VReadyOffsetPix[k];

		out->informative.misc.DST_Y_PER_PTE_ROW_NOM_L[k] = mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_L[k];
		out->informative.misc.DST_Y_PER_PTE_ROW_NOM_C[k] = mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_C[k];
		out->informative.misc.time_per_pte_group_nom_luma[k] = mode_lib->mp.time_per_pte_group_nom_luma[k];
		out->informative.misc.time_per_pte_group_nom_chroma[k] = mode_lib->mp.time_per_pte_group_nom_chroma[k];
		out->informative.misc.time_per_pte_group_vblank_luma[k] = mode_lib->mp.time_per_pte_group_vblank_luma[k];
		out->informative.misc.time_per_pte_group_vblank_chroma[k] = mode_lib->mp.time_per_pte_group_vblank_chroma[k];
		out->informative.misc.time_per_pte_group_flip_luma[k] = mode_lib->mp.time_per_pte_group_flip_luma[k];
		out->informative.misc.time_per_pte_group_flip_chroma[k] = mode_lib->mp.time_per_pte_group_flip_chroma[k];
		out->informative.misc.VRatioPrefetchY[k] = mode_lib->mp.VRatioPrefetchY[k];
		out->informative.misc.VRatioPrefetchC[k] = mode_lib->mp.VRatioPrefetchC[k];
		out->informative.misc.DestinationLinesForPrefetch[k] = mode_lib->mp.dst_y_prefetch[k];
		out->informative.misc.DestinationLinesToRequestVMInVBlank[k] = mode_lib->mp.dst_y_per_vm_vblank[k];
		out->informative.misc.DestinationLinesToRequestRowInVBlank[k] = mode_lib->mp.dst_y_per_row_vblank[k];
		out->informative.misc.DestinationLinesToRequestVMInImmediateFlip[k] = mode_lib->mp.dst_y_per_vm_flip[k];
		out->informative.misc.DestinationLinesToRequestRowInImmediateFlip[k] = mode_lib->mp.dst_y_per_row_flip[k];
		out->informative.misc.DisplayPipeLineDeliveryTimeLuma[k] = mode_lib->mp.DisplayPipeLineDeliveryTimeLuma[k];
		out->informative.misc.DisplayPipeLineDeliveryTimeChroma[k] = mode_lib->mp.DisplayPipeLineDeliveryTimeChroma[k];
		out->informative.misc.DisplayPipeLineDeliveryTimeLumaPrefetch[k] = mode_lib->mp.DisplayPipeLineDeliveryTimeLumaPrefetch[k];
		out->informative.misc.DisplayPipeLineDeliveryTimeChromaPrefetch[k] = mode_lib->mp.DisplayPipeLineDeliveryTimeChromaPrefetch[k];

		out->informative.misc.WritebackRequiredBandwidth = mode_lib->scratch.dml_core_mode_programming_locals.TotalWRBandwidth / 1000.0;
		out->informative.misc.WritebackAllowDRAMClockChangeEndPosition[k] = mode_lib->mp.WritebackAllowDRAMClockChangeEndPosition[k];
		out->informative.misc.WritebackAllowFCLKChangeEndPosition[k] = mode_lib->mp.WritebackAllowFCLKChangeEndPosition[k];
		out->informative.misc.DSCCLK_calculated[k] = mode_lib->mp.DSCCLK[k];
		out->informative.misc.BIGK_FRAGMENT_SIZE[k] = mode_lib->mp.BIGK_FRAGMENT_SIZE[k];
		out->informative.misc.PTE_BUFFER_MODE[k] = mode_lib->mp.PTE_BUFFER_MODE[k];
		out->informative.misc.DSCDelay[k] = mode_lib->mp.DSCDelay[k];
		out->informative.misc.MaxActiveDRAMClockChangeLatencySupported[k] = mode_lib->mp.MaxActiveDRAMClockChangeLatencySupported[k];

		if (mode_lib->mp.impacted_prefetch_margin_us[k] < out->informative.misc.LowestPrefetchMargin)
			out->informative.misc.LowestPrefetchMargin = mode_lib->mp.impacted_prefetch_margin_us[k];
	}

	// For this DV informative layer, all pipes in the same planes will just use the same id
	// will have the optimization and helper layer later on
	// only work when we can have high "mcache" that fit everything without thrashing the cache
	for (k = 0; k < out->display_config.num_planes; k++) {
		out->informative.non_optimized_mcache_allocation[k].num_mcaches_plane0 = dml_get_plane_num_mcaches_plane0(mode_lib, k);
		out->informative.non_optimized_mcache_allocation[k].informative.meta_row_bytes_plane0 = dml_get_plane_mcache_row_bytes_plane0(mode_lib, k);

		for (n = 0; n < out->informative.non_optimized_mcache_allocation[k].num_mcaches_plane0; n++) {
			out->informative.non_optimized_mcache_allocation[k].mcache_x_offsets_plane0[n] = dml_get_plane_array_mcache_offsets_plane0(mode_lib, k, n);
			out->informative.non_optimized_mcache_allocation[k].global_mcache_ids_plane0[n] = k;
		}

		out->informative.non_optimized_mcache_allocation[k].num_mcaches_plane1 = dml_get_plane_num_mcaches_plane1(mode_lib, k);
		out->informative.non_optimized_mcache_allocation[k].informative.meta_row_bytes_plane1 = dml_get_plane_mcache_row_bytes_plane1(mode_lib, k);

		for (n = 0; n < out->informative.non_optimized_mcache_allocation[k].num_mcaches_plane1; n++) {
			out->informative.non_optimized_mcache_allocation[k].mcache_x_offsets_plane1[n] = dml_get_plane_array_mcache_offsets_plane1(mode_lib, k, n);
			out->informative.non_optimized_mcache_allocation[k].global_mcache_ids_plane1[n] = k;
		}
	}
	out->informative.qos.max_non_urgent_latency_us = dml_get_max_non_urgent_latency_us(mode_lib);

	if (mode_lib->soc.qos_parameters.qos_type == dml2_qos_param_type_dcn4x) {
		if (((mode_lib->ip.rob_buffer_size_kbytes - mode_lib->ip.pixel_chunk_size_kbytes) * 1024
			/ mode_lib->ms.support.non_urg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]) >= out->informative.qos.max_non_urgent_latency_us) {
			out->informative.misc.ROBUrgencyAvoidance = true;
		} else {
			out->informative.misc.ROBUrgencyAvoidance = false;
		}
	} else {
		out->informative.misc.ROBUrgencyAvoidance = true;
	}
}
