// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_DCN6_CALCS_H__
#define __DML2_CORE_DCN6_CALCS_H__
#include "dml2_internal_shared_types.h"

/*
 * Flat table of every calcs function reachable from the DCN6 funcs layer
 * (both DCN6-specific calcs and DCN5 calcs reused unmodified by DCN6).
 * Slot names carry no generation prefix, only the assigned symbols do.
 */
struct dml2_core_dcn6_calcs {
	unsigned int (*calculate_max_vstartup)(
			bool ptoi_supported,
			unsigned int vblank_nom_default_us,
			const struct dml2_timing_cfg *timing,
			enum dml2_uclk_pstate_change_strategy pstate_strategy,
			double write_back_delay_us,
			unsigned int svp_lines);
	void (*calculate_alternate_params)(struct dml2_core_calcs_calculate_alternate_params *p);
	void (*calculate_alternate_svp_lines)(struct dml2_core_calcs_calculate_alternate_svp_lines *p);
	void (*calculate_flip_schedule)(
			struct dml2_core_internal_scratch *s,
			bool iflip_enable,
			bool ihostvm_enable,
			bool iffbm_enable,
			double HostVMInefficiencyFactor,
			double Tvm_trips_flip,
			double Tr0_trips_flip,
			double Tvm_trips_flip_rounded,
			double Tr0_trips_flip_rounded,
			bool GPUVMEnable,
			double vm_bytes,
			double DPTEBytesPerRow,
			enum dml2_source_format_class SourcePixelFormat,
			double LineTime,
			double VRatio,
			double VRatioChroma,
			double Tno_bw_flip,
			unsigned int dpte_row_height,
			unsigned int dpte_row_height_chroma,
			unsigned int max_flip_time_us,
			unsigned int max_flip_time_lines,
			unsigned int meta_row_height,
			unsigned int meta_row_height_chroma,

			// Output
			double *dst_y_per_vm_flip,
			double *dst_y_per_row_flip,
			double *final_flip_bw,
			bool *ImmediateFlipSupportedForPipe);
	void (*get_pipe_regs)(const struct dml2_display_cfg *display_cfg,
			const struct dml2_core_internal_display_mode_lib *mode_lib,
			struct dml2_dchub_per_pipe_register_set *out, int pipe_index, const struct dml2_utm_soc_bb *utm_soc_bb,
			struct dml2_core_internal_scratch *s);
	void (*calculate_watermarks_and_dram_speed_change_support)(
			struct dml2_core_internal_scratch *scratch,
			struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params *p);
	void (*calculate_stutter_efficiency)(struct dml2_core_internal_scratch *scratch,
			struct dml2_core_calcs_CalculateStutterEfficiency_params *p);
	void (*get_watermarks)(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib,
			const struct dml2_utm_soc_bb *utm_soc_bb, struct dml2_dchub_watermark_regs *out);
	void (*calculate_excess_vactive_bandwidth_required)(
			const struct dml2_display_cfg *display_cfg,
			unsigned int bytes_required_l[dml2_pstate_type_count][DML2_MAX_PLANES],
			unsigned int bytes_required_c[dml2_pstate_type_count][DML2_MAX_PLANES],
			/* outputs */
			double excess_vactive_fill_bw_l[],
			double excess_vactive_fill_bw_c[]);
	void (*calculate_pstate_schedule_windows)(
			int num_active_planes,
			const unsigned int v_blank_start[DML2_MAX_PLANES],
			const unsigned int v_blank_end[DML2_MAX_PLANES],
			const double otg_vline_time_us[DML2_MAX_PLANES],
			const double det_fill_delay_us[DML2_MAX_PLANES],
			const double reserved_vblank_us[DML2_MAX_PLANES],
			const double blackout_us,
			// Outputs
			double allow_start_us[DML2_MAX_PLANES],
			double allow_end_us[DML2_MAX_PLANES]);
	void (*calculate_pstate_schedule_admissibility)(
			uint32_t num_active_planes,
			double max_allow_delay_us,
			double min_allow_width_us,
			const uint32_t timing_group_id[DML2_MAX_PLANES],
			uint32_t timing_group_count,
			const double frame_time_us[DML2_MAX_PLANES],
			const double allow_start_us[DML2_MAX_PLANES],
			const double allow_end_us[DML2_MAX_PLANES],
			const enum dml2_pstate_method pstate_method[DML2_MAX_PLANES],
			const bool drr_enabled[DML2_MAX_DCN_PIPES],
			// Output
			double allow_window_us[DML2_MAX_DCN_PIPES],
			double disallow_window_us[DML2_MAX_DCN_PIPES],
			bool *pstate_admissible);

	/* DCN5 calcs reused unmodified by DCN6 */
	void (*calculate_max_det_and_min_compressed_buffer_size)(
			unsigned int ConfigReturnBufferSizeInKByte,
			unsigned int ConfigReturnBufferSegmentSizeInKByte,
			unsigned int ROBBufferSizeInKByte,
			unsigned int MaxNumDPP,
			unsigned int nomDETInKByteOverrideEnable,
			unsigned int nomDETInKByteOverrideValue,
			bool is_mrq_present,

			// Output
			unsigned int *MaxTotalDETInKByte,
			unsigned int *nomDETInKByte,
			unsigned int *MinCompressedBufferSizeInKByte);
	void (*adjust_pixel_clock_for_progressive_to_interlace_unit)(const struct dml2_display_cfg *display_cfg,
			bool ptoi_supported, double *PixelClockBackEnd);
	void (*calculate_byte_per_pixel_and_block_sizes)(
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
			bool *surf_linear128_c);
	void (*calculate_single_pipe_dppclk_and_scl_throughput)(
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
			double *DPPCLKUsingSingleDPP);
	void (*calculate_swath_and_det_configuration)(struct dml2_core_internal_scratch *scratch,
			struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params *p);
	void (*calculate_output_link)(
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
			unsigned int *RequiredSlots);
	void (*calculate_odm_mode)(
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
			unsigned int MaximumSlicesPerDSCUnit,
			unsigned int NumberOfDSCSlices,
			unsigned int odm_combine_support_mask,

			// Output
			bool *TotalAvailablePipesSupport,
			unsigned int *NumberOfDPP,
			enum dml2_odm_mode *ODMMode,
			double *RequiredDISPCLKPerSurface);
	double (*calculate_write_back_dispclk)(
			enum dml2_source_format_class WritebackPixelFormat,
			double PixelClock,
			enum dml2_odm_mode ODMMode,
			double WritebackHRatio,
			double WritebackVRatio,
			unsigned int WritebackHTaps,
			unsigned int WritebackVTaps,
			unsigned int WritebackHTapsChroma,
			unsigned int WritebackVTapsChroma,
			unsigned int WritebackSourceWidth,
			unsigned int WritebackDestinationWidth,
			unsigned int HTotal,
			unsigned int WritebackLineBufferSize);
	double (*calculate_required_dtbclk)(
			bool DSCEnable,
			double PixelClock,
			enum dml2_output_format_class OutputFormat,
			double OutputBpp,
			unsigned int DSCSlices,
			unsigned int HTotal,
			unsigned int HActive,
			unsigned int AudioRate,
			unsigned int AudioLayout);
	unsigned int (*calculate_dsc_delay_requirement)(
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
			double PixelClockBackEnd,
			bool use_legacy_dsc_delay_formula);
	void (*calculate_vm_row_and_swath)(struct dml2_core_internal_scratch *scratch,
			struct dml2_core_calcs_CalculateVMRowAndSwath_params *p);
	void (*calculate_bytes_to_fetch_required_to_hide_latency)(
			struct dml2_core_calcs_calculate_bytes_to_fetch_required_to_hide_latency_params *p);
	void (*calculate_cursor_req_attributes)(
			unsigned int cursor_width,
			unsigned int cursor_bpp,

			// output
			unsigned int *cursor_lines_per_chunk,
			unsigned int *cursor_bytes_per_line,
			unsigned int *cursor_bytes_per_chunk,
			unsigned int *cursor_bytes);
	void (*calculate_cursor_urgent_burst_factor)(
			unsigned int CursorBufferSize,
			unsigned int CursorWidth,
			unsigned int cursor_bytes_per_chunk,
			unsigned int cursor_lines_per_chunk,
			double LineTime,
			double UrgentLatency,

			double *UrgentBurstFactorCursor,
			bool *NotEnoughUrgentLatencyHiding);
	void (*calculate_urgent_burst_factor)(
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
			bool UnboundedRequestEnabled,
			unsigned int CompressedBufferSizeInkByte,
			unsigned int DETBufferSizeY,
			unsigned int DETBufferSizeC,
			// Output
			double *UrgentBurstFactorLuma,
			double *UrgentBurstFactorChroma,
			bool *NotEnoughUrgentLatencyHiding);
	void (*calculate_dcfclk_deep_sleep)(
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
			double *DCFClkDeepSleep);
	double (*calculate_write_back_delay)(
			enum dml2_source_format_class WritebackPixelFormat,
			double WritebackHRatio,
			double WritebackVRatio,
			unsigned int WritebackVTaps,
			unsigned int WritebackVTapsChroma,
			unsigned int WritebackDestinationWidth,
			unsigned int WritebackDestinationHeight,
			unsigned int WritebackSourceWidth,
			unsigned int WritebackSourceHeight,
			unsigned int HTotal);
	void (*calculate_mcache_setting)(
			struct dml2_core_internal_scratch *scratch,
			struct dml2_core_calcs_calculate_mcache_setting_params *p);
	void (*calculate_avg_bandwidth_required)(
			double *avg_bandwidth_required,

			// input
			unsigned int num_active_planes,
			double ReadBandwidthLuma[],
			double ReadBandwidthChroma[],
			double cursor_bw[],
			double dcc_dram_bw_nom_overhead_factor_p0[],
			double dcc_dram_bw_nom_overhead_factor_p1[]);
	void (*calculate_hostvm_inefficiency_factor)(
			double *HostVMInefficiencyFactor,
			double *HostVMInefficiencyFactorPrefetch,

			bool gpuvm_enable,
			bool hostvm_enable,
			unsigned int remote_iommu_outstanding_translations,
			unsigned int max_outstanding_reqs,
			double urg_bandwidth_avail_active_pixel_and_vm,
			double urg_bandwidth_avail_active_vm_only);
	void (*calculate_tdlut_setting)(
			struct dml2_core_internal_scratch *scratch,
			struct dml2_core_calcs_calculate_tdlut_setting_params *p);
	void (*calculate_extra_latency)(
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
			enum dml2_qos_param_type qos_type,
			bool max_outstanding_when_urgent_expected,
			unsigned int max_outstanding_requests,
			unsigned int request_size_bytes_luma[],
			unsigned int request_size_bytes_chroma[],
			unsigned int MetaChunkSize,
			unsigned int dchub_arb_to_ret_delay,
			double Ttrip,
			unsigned int hostvm_mode,

			// output
			double *ExtraLatency,
			double *ExtraLatency_sr,
			double *ExtraLatencyPrefetch);
	double (*calculate_t_wait)(
			long reserved_vblank_time_ns,
			double UrgentLatency,
			double Ttrip,
			double temp_read_or_ppt_blackout_us,
			bool drr_enabled);
	bool (*calculate_prefetch_schedule)(struct dml2_core_internal_scratch *scratch, struct dml2_core_calcs_CalculatePrefetchSchedule_params *p);
	void (*calculate_peak_bandwidth_required)(
			struct dml2_core_internal_scratch *s,
			struct dml2_core_calcs_calculate_peak_bandwidth_required_params *p);
	void (*calculate_vactive_det_fill_latency)(
			const struct dml2_display_cfg *display_cfg,
			unsigned int num_active_planes,
			unsigned int bytes_required_l[],
			unsigned int bytes_required_c[],
			double dcc_dram_bw_nom_overhead_factor_p0[],
			double dcc_dram_bw_nom_overhead_factor_p1[],
			double surface_read_bw_l[],
			double surface_read_bw_c[],
			double surface_avg_vactive_required_bw[],
			double surface_peak_required_bw[],
			/* output */
			double vactive_det_fill_delay_us[]);
	void (*calculate_dcc_configuration)(
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
			unsigned int *IndependentBlockChroma);
	void (*calculate_pixel_delivery_times)(
			const struct dml2_display_cfg *display_cfg,
			unsigned int NoOfDPP[DML2_MAX_PLANES],
			unsigned int NumberOfActiveSurfaces,
			double VRatioPrefetchY[],
			double VRatioPrefetchC[],
			unsigned int swath_width_luma_ub[],
			unsigned int swath_width_chroma_ub[],
			double PSCL_THROUGHPUT[],
			double PSCL_THROUGHPUT_CHROMA[],
			double Dppclk[],
			double DCFCLKDeepSleep,
			unsigned int BytePerPixelY[],
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
			double DisplayPipeRequestDeliveryTimeChromaPrefetch[]);
	void (*calculate_meta_and_pte_times)(struct dml2_core_shared_CalculateMetaAndPTETimes_params *p);
	void (*calculate_vm_group_and_request_times)(
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
			double TimePerVMRequestFlip[]);
	void (*calculate_pstate_keepout_dst_lines)(
			const struct dml2_display_cfg *display_cfg,
			const struct dml2_core_internal_watermarks *watermarks,
			unsigned int pstate_keepout_dst_lines[]);
	void (*get_arb_params)(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib,
			const struct dml2_utm_soc_bb *utm_soc_bb, struct dml2_display_arb_regs *out);
};

void dml2_core_dcn6_calcs_init(union dml2_core_calcs *calcs);

#endif /* __DML2_CORE_DCN6_CALCS_H__ */
