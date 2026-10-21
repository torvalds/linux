// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_DCN5_CALCS_DCHUB_H__
#define __DML2_CORE_DCN5_CALCS_DCHUB_H__
#include "dml2_internal_shared_types.h"

void dcn5_calculate_max_det_and_min_compressed_buffer_size(
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
		unsigned int *MinCompressedBufferSizeInKByte);

// ???
void dcn5_adjust_pixel_clock_for_progressive_to_interlace_unit(const struct dml2_display_cfg *display_cfg, bool ptoi_supported, double *PixelClockBackEnd);

void dcn5_calculate_swath_width(
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
	unsigned int SwathWidthSingleDPPY[],
	unsigned int SwathWidthSingleDPPC[],
	unsigned int SwathWidthY[], // per-pipe
	unsigned int SwathWidthC[], // per-pipe
	unsigned int MaximumSwathHeightY[],
	unsigned int MaximumSwathHeightC[],
	unsigned int swath_width_luma_ub[], // per-pipe
	unsigned int swath_width_chroma_ub[], // per-pipe
	unsigned int swath_width_luma_ub_single_dpp[],
	unsigned int swath_width_chroma_ub_single_dpp[]);

void dcn5_calculate_swath_and_det_configuration(struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params *p);

void dcn5_calculate_vm_row_and_swath(struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_CalculateVMRowAndSwath_params *p);

void dcn5_calculate_bytes_to_fetch_required_to_hide_latency(
		struct dml2_core_calcs_calculate_bytes_to_fetch_required_to_hide_latency_params *p);

void dcn5_calculate_excess_vactive_bandwidth_required(
		const struct dml2_display_cfg *display_cfg,
		unsigned int num_active_planes,
		unsigned int bytes_required_l[],
		unsigned int bytes_required_c[],
		/* outputs */
		double excess_vactive_fill_bw_l[],
		double excess_vactive_fill_bw_c[]);

void dcn5_calculate_cursor_req_attributes(
		unsigned int cursor_width,
		unsigned int cursor_bpp,

		// output
		unsigned int *cursor_lines_per_chunk,
		unsigned int *cursor_bytes_per_line,
		unsigned int *cursor_bytes_per_chunk,
		unsigned int *cursor_bytes);

void dcn5_calculate_cursor_urgent_burst_factor(
		unsigned int CursorBufferSize,
		unsigned int CursorWidth,
		unsigned int cursor_bytes_per_chunk,
		unsigned int cursor_lines_per_chunk,
		double LineTime,
		double UrgentLatency,

		double *UrgentBurstFactorCursor,
		bool *NotEnoughUrgentLatencyHiding);

void dcn5_calculate_urgent_burst_factor(
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
		bool *NotEnoughUrgentLatencyHiding);

void dcn5_calculate_dcfclk_deep_sleep(
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

unsigned int dcn5_calculate_max_vstartup(
		bool ptoi_supported,
		unsigned int vblank_nom_default_us,
		const struct dml2_timing_cfg *timing,
		double write_back_delay_us);

void dcn5_calculate_mcache_setting(
		struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_calculate_mcache_setting_params *p);

void dcn5_calculate_avg_bandwidth_required(
		double *avg_bandwidth_required,

		// input
		unsigned int num_active_planes,
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		double cursor_bw[],
		double dcc_dram_bw_nom_overhead_factor_p0[],
		double dcc_dram_bw_nom_overhead_factor_p1[]);

void dcn5_calculate_hostvm_inefficiency_factor(
		double *HostVMInefficiencyFactor,
		double *HostVMInefficiencyFactorPrefetch,

		bool gpuvm_enable,
		bool hostvm_enable,
		unsigned int remote_iommu_outstanding_translations,
		unsigned int max_outstanding_reqs,
		double urg_bandwidth_avail_active_pixel_and_vm,
		double urg_bandwidth_avail_active_vm_only);

void dcn5_calculate_tdlut_setting(
		struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_calculate_tdlut_setting_params *p);

void dcn5_calculate_extra_latency(
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
		double *ExtraLatency, // Tex
		double *ExtraLatency_sr, // Tex_sr
		double *ExtraLatencyPrefetch);

double dcn5_calculate_t_wait(
		long reserved_vblank_time_ns,
		double UrgentLatency,
		double Ttrip,
		double temp_read_or_ppt_blackout_us,
		bool drr_enabled);

bool dcn5_calculate_prefetch_schedule(struct dml2_core_internal_scratch *scratch, struct dml2_core_calcs_CalculatePrefetchSchedule_params *p);

void dcn5_calculate_peak_bandwidth_required(
		struct dml2_core_internal_scratch *s,
		struct dml2_core_calcs_calculate_peak_bandwidth_required_params *p);

// ???
void dcn5_check_urgent_bandwidth_support(
		double *frac_urg_bandwidth_nom,
		bool *bandwidth_support_ok,   // max of vm, prefetch, vactive all ok

		double non_urg_bandwidth_required,
		double urg_bandwidth_required,
		double urg_bandwidth_available);

// ???
double dcn5_get_bandwidth_available_for_immediate_flip(
		double urg_bandwidth_required, // no flip
		double urg_bandwidth_available);

// ???
unsigned int dcn5_get_pipe_flip_bytes(
		double hostvm_inefficiency_factor,
		unsigned int vm_bytes,
		unsigned int dpte_row_bytes,
		unsigned int meta_row_bytes);

// ???
void dcn5_check_immediate_flip_bandwidth_support(
		// Output
		double *frac_urg_bandwidth_flip,
		bool *flip_bandwidth_support_ok,

		// Input
		double urg_bandwidth_required_flip,
		double non_urg_bandwidth_required_flip,
		double urg_bandwidth_available);

void dcn5_calculate_dcc_configuration(
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

void dcn5_calculate_flip_schedule(
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
		bool *ImmediateFlipSupportedForPipe);

void dcn5_calculate_watermarks_and_dram_speed_change_support(
		struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params *p);

bool dcn5_calculate_pstate_support_method(
		enum dml2_pstate_method method,
		double vactive_margin_us,
		double reserved_vblank_us,
		double blackout_us,
		bool all_streams_blanked,
		/* output */
		enum dml2_pstate_change_support *surface_pstate_change_support);

void dcn5_calculate_pstate_keepout_dst_lines(
		const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_watermarks *watermarks,
		unsigned int pstate_keepout_dst_lines[]);

void dcn5_calculate_vactive_det_fill_latency(
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

double dcn5_calculate_write_back_delay(
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

void dcn5_calculate_meta_and_pte_times(struct dml2_core_shared_CalculateMetaAndPTETimes_params *p);

void dcn5_calculate_vm_group_and_request_times(
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

void dcn5_calculate_stutter_efficiency(struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_CalculateStutterEfficiency_params *p);

void dcn5_calculate_byte_per_pixel_and_block_sizes(
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

void dml2_core_dcn5_calcs_cursor_dlg_reg(struct dml2_cursor_dlg_regs *cursor_dlg_regs, const struct dml2_get_cursor_dlg_reg *p);

unsigned int dcn5_calculate_vm_and_row_bytes(struct dml2_core_shared_calculate_vm_and_row_bytes_params *p);

void dcn5_get_pipe_regs(const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		struct dml2_dchub_per_pipe_register_set *out, int pipe_index, const struct dml2_utm_soc_bb *utm_soc_bb,
		struct dml2_core_internal_scratch *s);

void dcn5_get_arb_params(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, const struct dml2_utm_soc_bb *utm_soc_bb, struct dml2_display_arb_regs *out);

void dcn5_get_watermarks(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, const struct dml2_utm_soc_bb *utm_soc_bb, struct dml2_dchub_watermark_regs *out);

void dcn5_rq_dlg_get_rq_reg(struct dml2_display_rq_regs *rq_regs,
		const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		unsigned int pipe_idx);

void dcn5_get_mcif_arb_params(const struct dml2_core_internal_display_mode_lib *mode_lib,
		struct dml2_mcif_global_register_set *out);

void dcn5_get_per_dwb_params(const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		struct dml2_mcif_per_pipe_register_set *out,
		int stream_index,
		int dwb_index);

#endif /* __DML2_CORE_DCN5_CALCS_DCHUB_H__ */
