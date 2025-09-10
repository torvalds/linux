// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_SHARED_TYPES_H__
#define __DML2_CORE_SHARED_TYPES_H__

#include "dml2_external_lib_deps.h"
#include "dml_top_display_cfg_types.h"
#include "dml_top_types.h"

#define __DML_VBA_DEBUG__
#define __DML2_CALCS_MAX_VRATIO_PRE_OTO__ 4.0 //<brief max vratio for one-to-one prefetch bw scheduling
#define __DML2_CALCS_MAX_VRATIO_PRE_EQU__ 6.0 //<brief max vratio for equalized prefetch bw scheduling
#define __DML2_CALCS_MAX_VRATIO_PRE__ 8.0 //<brief max prefetch vratio register limit

#define __DML2_CALCS_DPP_INVALID__ 0
#define __DML2_CALCS_DCFCLK_FACTOR__ 1.15 //<brief fudge factor for min dcfclk calclation
#define __DML2_CALCS_PIPE_NO_PLANE__ 99

struct dml2_core_ip_params {
	unsigned int vblank_nom_default_us;
	unsigned int remote_iommu_outstanding_translations;
	unsigned int rob_buffer_size_kbytes;
	unsigned int config_return_buffer_size_in_kbytes;
	unsigned int config_return_buffer_segment_size_in_kbytes;
	unsigned int compressed_buffer_segment_size_in_kbytes;
	unsigned int meta_fifo_size_in_kentries;
	unsigned int dpte_buffer_size_in_pte_reqs_luma;
	unsigned int dpte_buffer_size_in_pte_reqs_chroma;
	unsigned int pixel_chunk_size_kbytes;
	unsigned int alpha_pixel_chunk_size_kbytes;
	unsigned int min_pixel_chunk_size_bytes;
	unsigned int writeback_chunk_size_kbytes;
	unsigned int line_buffer_size_bits;
	unsigned int max_line_buffer_lines;
	unsigned int writeback_interface_buffer_size_kbytes;
	unsigned int max_num_dpp;
	unsigned int max_num_otg;
	unsigned int max_num_wb;
	unsigned int max_dchub_pscl_bw_pix_per_clk;
	unsigned int max_pscl_lb_bw_pix_per_clk;
	unsigned int max_lb_vscl_bw_pix_per_clk;
	unsigned int max_vscl_hscl_bw_pix_per_clk;
	double max_hscl_ratio;
	double max_vscl_ratio;
	unsigned int max_hscl_taps;
	unsigned int max_vscl_taps;
	unsigned int num_dsc;
	unsigned int maximum_dsc_bits_per_component;
	unsigned int maximum_pixels_per_line_per_dsc_unit;
	bool dsc422_native_support;
	bool cursor_64bpp_support;
	double dispclk_ramp_margin_percent;
	unsigned int dppclk_delay_subtotal;
	unsigned int dppclk_delay_scl;
	unsigned int dppclk_delay_scl_lb_only;
	unsigned int dppclk_delay_cnvc_formatter;
	unsigned int dppclk_delay_cnvc_cursor;
	unsigned int cursor_buffer_size;
	unsigned int cursor_chunk_size;
	unsigned int dispclk_delay_subtotal;
	bool dynamic_metadata_vm_enabled;
	unsigned int max_inter_dcn_tile_repeaters;
	unsigned int max_num_hdmi_frl_outputs;
	unsigned int max_num_dp2p0_outputs;
	unsigned int max_num_dp2p0_streams;
	bool dcc_supported;
	bool ptoi_supported;
	double writeback_max_hscl_ratio;
	double writeback_max_vscl_ratio;
	double writeback_min_hscl_ratio;
	double writeback_min_vscl_ratio;
	unsigned int writeback_max_hscl_taps;
	unsigned int writeback_max_vscl_taps;
	unsigned int writeback_line_buffer_buffer_size;

	unsigned int words_per_channel;
	bool imall_supported;
	unsigned int max_flip_time_us;
	unsigned int max_flip_time_lines;
	unsigned int subvp_swath_height_margin_lines;
	unsigned int subvp_fw_processing_delay_us;
	unsigned int subvp_pstate_allow_width_us;

	// MRQ
	bool dcn_mrq_present;
	unsigned int zero_size_buffer_entries;
	unsigned int compbuf_reserved_space_zs;
	unsigned int dcc_meta_buffer_size_bytes;
	unsigned int meta_chunk_size_kbytes;
	unsigned int min_meta_chunk_size_bytes;

	unsigned int dchub_arb_to_ret_delay; // num of dcfclk
	unsigned int hostvm_mode;
};

struct dml2_core_internal_DmlPipe {
	double Dppclk;
	double Dispclk;
	double PixelClock;
	double DCFClkDeepSleep;
	unsigned int DPPPerSurface;
	bool ScalerEnabled;
	bool UPSPEnabled;
	enum dml2_rotation_angle RotationAngle;
	bool mirrored;
	unsigned int ViewportHeight;
	unsigned int ViewportHeightC;
	unsigned int BlockWidth256BytesY;
	unsigned int BlockHeight256BytesY;
	unsigned int BlockWidth256BytesC;
	unsigned int BlockHeight256BytesC;
	unsigned int BlockWidthY;
	unsigned int BlockHeightY;
	unsigned int BlockWidthC;
	unsigned int BlockHeightC;
	unsigned int InterlaceEnable;
	unsigned int NumberOfCursors;
	unsigned int VBlank;
	unsigned int HTotal;
	unsigned int HActive;
	bool DCCEnable;
	enum dml2_odm_mode ODMMode;
	enum dml2_source_format_class SourcePixelFormat;
	enum dml2_swizzle_mode SurfaceTiling;
	unsigned int BytePerPixelY;
	unsigned int BytePerPixelC;
	bool ProgressiveToInterlaceUnitInOPP;
	double VRatio;
	double VRatioChroma;
	unsigned int VTaps;
	unsigned int VTapsChroma;
	unsigned int PitchY;
	unsigned int PitchC;
	bool ViewportStationary;
	unsigned int ViewportXStart;
	unsigned int ViewportYStart;
	unsigned int ViewportXStartC;
	unsigned int ViewportYStartC;
	bool FORCE_ONE_ROW_FOR_FRAME;
	unsigned int SwathHeightY;
	unsigned int SwathHeightC;

	unsigned int DCCMetaPitchY;
	unsigned int DCCMetaPitchC;
};

enum dml2_core_internal_request_type {
	dml2_core_internal_request_type_256_bytes = 0,
	dml2_core_internal_request_type_128_bytes_non_contiguous = 1,
	dml2_core_internal_request_type_128_bytes_contiguous = 2,
	dml2_core_internal_request_type_na = 3
};
enum dml2_core_internal_bw_type {
	dml2_core_internal_bw_sdp = 0,
	dml2_core_internal_bw_dram = 1,
	dml2_core_internal_bw_max
};

enum dml2_core_internal_soc_state_type {
	dml2_core_internal_soc_state_sys_active = 0,
	dml2_core_internal_soc_state_svp_prefetch = 1,
	dml2_core_internal_soc_state_sys_idle = 2,
	dml2_core_internal_soc_state_max
};

enum dml2_core_internal_output_type {
	dml2_core_internal_output_type_unknown = 0,
	dml2_core_internal_output_type_dp = 1,
	dml2_core_internal_output_type_edp = 2,
	dml2_core_internal_output_type_dp2p0 = 3,
	dml2_core_internal_output_type_hdmi = 4,
	dml2_core_internal_output_type_hdmifrl = 5
};

enum dml2_core_internal_output_type_rate {
	dml2_core_internal_output_rate_unknown = 0,
	dml2_core_internal_output_rate_dp_rate_hbr = 1,
	dml2_core_internal_output_rate_dp_rate_hbr2 = 2,
	dml2_core_internal_output_rate_dp_rate_hbr3 = 3,
	dml2_core_internal_output_rate_dp_rate_uhbr10 = 4,
	dml2_core_internal_output_rate_dp_rate_uhbr13p5 = 5,
	dml2_core_internal_output_rate_dp_rate_uhbr20 = 6,
	dml2_core_internal_output_rate_hdmi_rate_3x3 = 7,
	dml2_core_internal_output_rate_hdmi_rate_6x3 = 8,
	dml2_core_internal_output_rate_hdmi_rate_6x4 = 9,
	dml2_core_internal_output_rate_hdmi_rate_8x4 = 10,
	dml2_core_internal_output_rate_hdmi_rate_10x4 = 11,
	dml2_core_internal_output_rate_hdmi_rate_12x4 = 12,
	dml2_core_internal_output_rate_hdmi_rate_16x4 = 13,
	dml2_core_internal_output_rate_hdmi_rate_20x4 = 14
};

struct dml2_core_internal_watermarks {
	double UrgentWatermark;
	double WritebackUrgentWatermark;
	double DRAMClockChangeWatermark;
	double FCLKChangeWatermark;
	double WritebackDRAMClockChangeWatermark;
	double WritebackFCLKChangeWatermark;
	double StutterExitWatermark;
	double StutterEnterPlusExitWatermark;
	double LowPowerStutterExitWatermark;
	double LowPowerStutterEnterPlusExitWatermark;
	double Z8StutterExitWatermark;
	double Z8StutterEnterPlusExitWatermark;
	double USRRetrainingWatermark;
	double temp_read_or_ppt_watermark_us;
};

struct dml2_core_internal_mode_support_info {
	//-----------------
	// Mode Support Information
	//-----------------
	bool ImmediateFlipSupport; //<brief Means mode support immediate flip at the max combine setting; determine in mode support and used in mode programming

	// Mode Support Reason/
	bool WritebackLatencySupport;
	bool ScaleRatioAndTapsSupport;
	bool SourceFormatPixelAndScanSupport;
	bool P2IWith420;
	bool DSCSlicesODMModeSupported;
	bool DSCOnlyIfNecessaryWithBPP;
	bool DSC422NativeNotSupported;
	bool LinkRateDoesNotMatchDPVersion;
	bool LinkRateForMultistreamNotIndicated;
	bool BPPForMultistreamNotIndicated;
	bool MultistreamWithHDMIOreDP;
	bool MSOOrODMSplitWithNonDPLink;
	bool NotEnoughLanesForMSO;
	bool NumberOfOTGSupport;
	bool NumberOfHDMIFRLSupport;
	bool NumberOfDP2p0Support;
	bool WritebackScaleRatioAndTapsSupport;
	bool CursorSupport;
	bool PitchSupport;
	bool ViewportExceedsSurface;
	//bool ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified;
	bool ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe;
	bool InvalidCombinationOfMALLUseForPStateAndStaticScreen;
	bool InvalidCombinationOfMALLUseForPState;
	bool ExceededMALLSize;
	bool EnoughWritebackUnits;

	bool ExceededMultistreamSlots;
	bool NotEnoughDSCUnits;
	bool NotEnoughDSCSlices;
	bool PixelsPerLinePerDSCUnitSupport;
	bool DSCCLKRequiredMoreThanSupported;
	bool DTBCLKRequiredMoreThanSupported;
	bool LinkCapacitySupport;

	bool ROBSupport;
	bool OutstandingRequestsSupport;
	bool OutstandingRequestsUrgencyAvoidance;

	bool PTEBufferSizeNotExceeded;
	bool DCCMetaBufferSizeNotExceeded;
	enum dml2_pstate_change_support DRAMClockChangeSupport[DML2_MAX_PLANES];
	enum dml2_pstate_change_support FCLKChangeSupport[DML2_MAX_PLANES];
	bool global_dram_clock_change_supported;
	bool global_fclk_change_supported;
	bool USRRetrainingSupport;
	bool AvgBandwidthSupport;
	bool UrgVactiveBandwidthSupport;
	bool EnoughUrgentLatencyHidingSupport;
	bool PrefetchScheduleSupported;
	bool PrefetchSupported;
	bool PrefetchBandwidthSupported;
	bool DynamicMetadataSupported;
	bool VRatioInPrefetchSupported;
	bool DISPCLK_DPPCLK_Support;
	bool TotalAvailablePipesSupport;
	bool ODMSupport;
	bool ModeSupport;
	bool ViewportSizeSupport;

	bool MPCCombineEnable[DML2_MAX_PLANES]; /// <brief Indicate if the MPC Combine enable in the given state and optimize mpc combine setting
	enum dml2_odm_mode ODMMode[DML2_MAX_PLANES]; /// <brief ODM mode that is chosen in the mode check stage and will be used in mode programming stage
	unsigned int DPPPerSurface[DML2_MAX_PLANES]; /// <brief How many DPPs are needed drive the surface to output. If MPCC or ODMC could be 2 or 4.
	bool DSCEnabled[DML2_MAX_PLANES]; /// <brief Indicate if the DSC is actually required; used in mode_programming
	bool FECEnabled[DML2_MAX_PLANES]; /// <brief Indicate if the FEC is actually required
	unsigned int NumberOfDSCSlices[DML2_MAX_PLANES]; /// <brief Indicate how many slices needed to support the given mode

	double OutputBpp[DML2_MAX_PLANES];
	enum dml2_core_internal_output_type OutputType[DML2_MAX_PLANES];
	enum dml2_core_internal_output_type_rate OutputRate[DML2_MAX_PLANES];

	unsigned int AlignedYPitch[DML2_MAX_PLANES];
	unsigned int AlignedCPitch[DML2_MAX_PLANES];

	unsigned int AlignedDCCMetaPitchY[DML2_MAX_PLANES];
	unsigned int AlignedDCCMetaPitchC[DML2_MAX_PLANES];

	unsigned int request_size_bytes_luma[DML2_MAX_PLANES];
	unsigned int request_size_bytes_chroma[DML2_MAX_PLANES];
	enum dml2_core_internal_request_type RequestLuma[DML2_MAX_PLANES];
	enum dml2_core_internal_request_type RequestChroma[DML2_MAX_PLANES];

	unsigned int DCCYMaxUncompressedBlock[DML2_MAX_PLANES];
	unsigned int DCCYMaxCompressedBlock[DML2_MAX_PLANES];
	unsigned int DCCYIndependentBlock[DML2_MAX_PLANES];
	unsigned int DCCCMaxUncompressedBlock[DML2_MAX_PLANES];
	unsigned int DCCCMaxCompressedBlock[DML2_MAX_PLANES];
	unsigned int DCCCIndependentBlock[DML2_MAX_PLANES];

	double avg_bandwidth_available_min[dml2_core_internal_soc_state_max];
	double avg_bandwidth_available[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max];
	double urg_bandwidth_available_min_latency[dml2_core_internal_soc_state_max]; // min between SDP and DRAM, for latency evaluation
	double urg_bandwidth_available_min[dml2_core_internal_soc_state_max]; // min between SDP and DRAM
	double urg_bandwidth_available[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max];
	double urg_bandwidth_available_vm_only[dml2_core_internal_soc_state_max]; // the min of sdp bw and dram_vm_only bw, sdp has no different derate for vm/non-vm etc.
	double urg_bandwidth_available_pixel_and_vm[dml2_core_internal_soc_state_max]; // the min of sdp bw and dram_pixel_and_vm bw, sdp has no different derate for vm/non-vm etc.

	double avg_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max];
	double urg_vactive_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max]; // active bandwidth, scaled by urg burst factor
	double urg_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max]; // include vm, prefetch, active bandwidth, scaled by urg burst factor
	double urg_bandwidth_required_qual[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max]; // include vm, prefetch, active bandwidth, scaled by urg burst factor, use qual_row_bw
	double urg_bandwidth_required_flip[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max]; // include vm, prefetch, active bandwidth + flip

	double non_urg_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max]; // same as urg_bandwidth, except not scaled by urg burst factor
	double non_urg_bandwidth_required_flip[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max];
	bool avg_bandwidth_support_ok[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max];
	double max_urgent_latency_us;
	double max_non_urgent_latency_us;
	double avg_non_urgent_latency_us;
	double avg_urgent_latency_us;
	double df_response_time_us;

	bool incorrect_imall_usage;

	bool g6_temp_read_support;
	bool temp_read_or_ppt_support;

	struct dml2_core_internal_watermarks watermarks;
	bool dcfclk_support;
	bool qos_bandwidth_support;
};

struct dml2_core_internal_mode_support {
	// Physical info; only using for programming
	unsigned int state_idx; // <brief min clk state table index for mode support call
	unsigned int qos_param_index; // to access the uclk dependent qos_parameters table
	unsigned int active_min_uclk_dpm_index; // to access the min_clk table
	unsigned int num_active_planes; // <brief As determined by either e2e_pipe_param or display_cfg

	// Calculated Clocks
	double RequiredDISPCLK; /// <brief Required DISPCLK; depends on pixel rate; odm mode etc.
	double RequiredDPPCLK[DML2_MAX_PLANES];
	double RequiredDISPCLKPerSurface[DML2_MAX_PLANES];
	double RequiredDTBCLK[DML2_MAX_PLANES];

	double required_dscclk_freq_mhz[DML2_MAX_PLANES];

	double FabricClock; /// <brief Basically just the clock freq at the min (or given) state
	double SOCCLK; /// <brief Basically just the clock freq at the min (or given) state
	double DCFCLK; /// <brief Basically just the clock freq at the min (or given) state and max combine setting
	double GlobalDPPCLK; /// <brief the Max DPPCLK freq out of all pipes
	double GlobalDTBCLK; /// <brief the Max DTBCLK freq out of all pipes
	double uclk_freq_mhz;
	double dram_bw_mbps;
	double max_dram_bw_mbps;
	double min_available_urgent_bandwidth_MBps; /// <brief Minimum guaranteed available urgent return bandwidth in MBps

	double MaxFabricClock; /// <brief Basically just the clock freq at the min (or given) state
	double MaxDCFCLK; /// <brief Basically just the clock freq at the min (or given) state and max combine setting
	double max_dispclk_freq_mhz;
	double max_dppclk_freq_mhz;
	double max_dscclk_freq_mhz;

	bool NoTimeForPrefetch[DML2_MAX_PLANES];
	bool NoTimeForDynamicMetadata[DML2_MAX_PLANES];

	// ----------------------------------
	// Mode Support Info and fail reason
	// ----------------------------------
	struct dml2_core_internal_mode_support_info support;

	// These are calculated before the ModeSupport and ModeProgram step
	// They represent the bound for the return buffer sizing
	unsigned int MaxTotalDETInKByte;
	unsigned int NomDETInKByte;
	unsigned int MinCompressedBufferSizeInKByte;

	// Info obtained at the end of mode support calculations
	// The reported info is at the "optimal" state and combine setting
	unsigned int DETBufferSizeInKByte[DML2_MAX_PLANES]; // <brief Recommended DET size configuration for this plane. All pipes under this plane should program the DET buffer size to the calculated value.
	unsigned int DETBufferSizeY[DML2_MAX_PLANES];
	unsigned int DETBufferSizeC[DML2_MAX_PLANES];
	unsigned int SwathHeightY[DML2_MAX_PLANES];
	unsigned int SwathHeightC[DML2_MAX_PLANES];
	unsigned int SwathWidthY[DML2_MAX_PLANES]; // per-pipe
	unsigned int SwathWidthC[DML2_MAX_PLANES]; // per-pipe

	// ----------------------------------
	// Intermediates/Informational
	// ----------------------------------
	unsigned int TotImmediateFlipBytes;
	bool DCCEnabledInAnySurface;
	double WritebackRequiredDISPCLK;
	double TimeCalc;
	double TWait[DML2_MAX_PLANES];

	bool UnboundedRequestEnabled;
	unsigned int compbuf_reserved_space_64b;
	bool hw_debug5;
	unsigned int CompressedBufferSizeInkByte;
	double VRatioPreY[DML2_MAX_PLANES];
	double VRatioPreC[DML2_MAX_PLANES];
	unsigned int req_per_swath_ub_l[DML2_MAX_PLANES];
	unsigned int req_per_swath_ub_c[DML2_MAX_PLANES];
	unsigned int swath_width_luma_ub[DML2_MAX_PLANES];
	unsigned int swath_width_chroma_ub[DML2_MAX_PLANES];
	unsigned int RequiredSlots[DML2_MAX_PLANES];
	unsigned int vm_bytes[DML2_MAX_PLANES];
	unsigned int DPTEBytesPerRow[DML2_MAX_PLANES];
	unsigned int PrefetchLinesY[DML2_MAX_PLANES];
	unsigned int PrefetchLinesC[DML2_MAX_PLANES];
	unsigned int MaxNumSwathY[DML2_MAX_PLANES]; /// <brief Max number of swath for prefetch
	unsigned int MaxNumSwathC[DML2_MAX_PLANES]; /// <brief Max number of swath for prefetch
	unsigned int PrefillY[DML2_MAX_PLANES];
	unsigned int PrefillC[DML2_MAX_PLANES];
	unsigned int full_swath_bytes_l[DML2_MAX_PLANES];
	unsigned int full_swath_bytes_c[DML2_MAX_PLANES];

	bool use_one_row_for_frame[DML2_MAX_PLANES];
	bool use_one_row_for_frame_flip[DML2_MAX_PLANES];

	double dst_y_prefetch[DML2_MAX_PLANES];
	double LinesForVM[DML2_MAX_PLANES];
	double LinesForDPTERow[DML2_MAX_PLANES];
	unsigned int SwathWidthYSingleDPP[DML2_MAX_PLANES];
	unsigned int SwathWidthCSingleDPP[DML2_MAX_PLANES];
	unsigned int BytePerPixelY[DML2_MAX_PLANES];
	unsigned int BytePerPixelC[DML2_MAX_PLANES];
	double BytePerPixelInDETY[DML2_MAX_PLANES];
	double BytePerPixelInDETC[DML2_MAX_PLANES];

	unsigned int Read256BlockHeightY[DML2_MAX_PLANES];
	unsigned int Read256BlockWidthY[DML2_MAX_PLANES];
	unsigned int Read256BlockHeightC[DML2_MAX_PLANES];
	unsigned int Read256BlockWidthC[DML2_MAX_PLANES];
	unsigned int MacroTileHeightY[DML2_MAX_PLANES];
	unsigned int MacroTileHeightC[DML2_MAX_PLANES];
	unsigned int MacroTileWidthY[DML2_MAX_PLANES];
	unsigned int MacroTileWidthC[DML2_MAX_PLANES];

	bool surf_linear128_l[DML2_MAX_PLANES];
	bool surf_linear128_c[DML2_MAX_PLANES];

	double PSCL_FACTOR[DML2_MAX_PLANES];
	double PSCL_FACTOR_CHROMA[DML2_MAX_PLANES];
	double MaximumSwathWidthLuma[DML2_MAX_PLANES];
	double MaximumSwathWidthChroma[DML2_MAX_PLANES];
	double Tno_bw[DML2_MAX_PLANES];
	double Tno_bw_flip[DML2_MAX_PLANES];
	double dst_y_per_vm_flip[DML2_MAX_PLANES];
	double dst_y_per_row_flip[DML2_MAX_PLANES];
	double WritebackDelayTime[DML2_MAX_PLANES];
	unsigned int dpte_group_bytes[DML2_MAX_PLANES];
	unsigned int dpte_row_height[DML2_MAX_PLANES];
	unsigned int dpte_row_height_chroma[DML2_MAX_PLANES];
	double UrgLatency;
	double TripToMemory;
	double UrgentBurstFactorCursor[DML2_MAX_PLANES];
	double UrgentBurstFactorCursorPre[DML2_MAX_PLANES];
	double UrgentBurstFactorLuma[DML2_MAX_PLANES];
	double UrgentBurstFactorLumaPre[DML2_MAX_PLANES];
	double UrgentBurstFactorChroma[DML2_MAX_PLANES];
	double UrgentBurstFactorChromaPre[DML2_MAX_PLANES];
	double MaximumSwathWidthInLineBufferLuma;
	double MaximumSwathWidthInLineBufferChroma;
	double ExtraLatency;
	double ExtraLatency_sr;
	double ExtraLatencyPrefetch;

	double dcc_dram_bw_nom_overhead_factor_p0[DML2_MAX_PLANES]; // overhead to request meta
	double dcc_dram_bw_nom_overhead_factor_p1[DML2_MAX_PLANES];
	double dcc_dram_bw_pref_overhead_factor_p0[DML2_MAX_PLANES]; // overhead to request meta
	double dcc_dram_bw_pref_overhead_factor_p1[DML2_MAX_PLANES];
	double mall_prefetch_sdp_overhead_factor[DML2_MAX_PLANES]; // overhead to the imall or phantom pipe
	double mall_prefetch_dram_overhead_factor[DML2_MAX_PLANES];

	bool is_using_mall_for_ss[DML2_MAX_PLANES];
	unsigned int meta_row_width_chroma[DML2_MAX_PLANES];
	unsigned int PixelPTEReqHeightC[DML2_MAX_PLANES];
	bool PTE_BUFFER_MODE[DML2_MAX_PLANES];
	unsigned int meta_req_height_chroma[DML2_MAX_PLANES];
	unsigned int meta_pte_bytes_per_frame_ub_c[DML2_MAX_PLANES];
	unsigned int dpde0_bytes_per_frame_ub_c[DML2_MAX_PLANES];
	unsigned int dpte_row_width_luma_ub[DML2_MAX_PLANES];
	unsigned int meta_req_width[DML2_MAX_PLANES];
	unsigned int meta_row_width[DML2_MAX_PLANES];
	unsigned int PixelPTEReqWidthY[DML2_MAX_PLANES];
	unsigned int dpte_row_height_linear[DML2_MAX_PLANES];
	unsigned int PTERequestSizeY[DML2_MAX_PLANES];
	unsigned int dpte_row_width_chroma_ub[DML2_MAX_PLANES];
	unsigned int PixelPTEReqWidthC[DML2_MAX_PLANES];
	unsigned int meta_pte_bytes_per_frame_ub_l[DML2_MAX_PLANES];
	unsigned int dpte_row_height_linear_chroma[DML2_MAX_PLANES];
	unsigned int PTERequestSizeC[DML2_MAX_PLANES];
	unsigned int meta_req_height[DML2_MAX_PLANES];
	unsigned int dpde0_bytes_per_frame_ub_l[DML2_MAX_PLANES];
	unsigned int meta_req_width_chroma[DML2_MAX_PLANES];
	unsigned int PixelPTEReqHeightY[DML2_MAX_PLANES];
	unsigned int BIGK_FRAGMENT_SIZE[DML2_MAX_PLANES];
	unsigned int vm_group_bytes[DML2_MAX_PLANES];
	unsigned int VReadyOffsetPix[DML2_MAX_PLANES];
	unsigned int VUpdateOffsetPix[DML2_MAX_PLANES];
	unsigned int VUpdateWidthPix[DML2_MAX_PLANES];
	double TSetup[DML2_MAX_PLANES];
	double Tdmdl_vm_raw[DML2_MAX_PLANES];
	double Tdmdl_raw[DML2_MAX_PLANES];
	unsigned int VStartupMin[DML2_MAX_PLANES]; /// <brief Minimum vstartup to meet the prefetch schedule (i.e. the prefetch solution can be found at this vstartup time); not the actual global sync vstartup pos.
	double MaxActiveDRAMClockChangeLatencySupported[DML2_MAX_PLANES];
	double MaxActiveFCLKChangeLatencySupported;

	// Backend
	bool RequiresDSC[DML2_MAX_PLANES];
	bool RequiresFEC[DML2_MAX_PLANES];
	double OutputBpp[DML2_MAX_PLANES];
	double DesiredOutputBpp[DML2_MAX_PLANES];
	double PixelClockBackEnd[DML2_MAX_PLANES];
	unsigned int DSCDelay[DML2_MAX_PLANES];
	enum dml2_core_internal_output_type OutputType[DML2_MAX_PLANES];
	enum dml2_core_internal_output_type_rate OutputRate[DML2_MAX_PLANES];
	bool TotalAvailablePipesSupportNoDSC;
	bool TotalAvailablePipesSupportDSC;
	unsigned int NumberOfDPPNoDSC;
	unsigned int NumberOfDPPDSC;
	enum dml2_odm_mode ODMModeNoDSC;
	enum dml2_odm_mode ODMModeDSC;
	double RequiredDISPCLKPerSurfaceNoDSC;
	double RequiredDISPCLKPerSurfaceDSC;
	unsigned int EstimatedNumberOfDSCSlices[DML2_MAX_PLANES];

	// Bandwidth Related Info
	double BandwidthAvailableForImmediateFlip;
	double vactive_sw_bw_l[DML2_MAX_PLANES]; // no dcc overhead, for the plane
	double vactive_sw_bw_c[DML2_MAX_PLANES];
	double WriteBandwidth[DML2_MAX_PLANES][DML2_MAX_WRITEBACK];
	double RequiredPrefetchPixelDataBWLuma[DML2_MAX_PLANES];
	double RequiredPrefetchPixelDataBWChroma[DML2_MAX_PLANES];
	/* Max bandwidth calculated from prefetch schedule should be considered in addition to the pixel data bw to avoid ms/mp mismatches.
	 * 1. oto bw should also be considered when calculating peak urgent bw to avoid situations oto/equ mismatches between ms and mp
	 *
	 * 2. equ bandwidth needs to be considered for calculating peak urgent bw when equ schedule is used in mode support.
	 *    Some slight difference in variables may cause the pixel data bandwidth to be higher
	 *    even though overall equ prefetch bandwidths can be lower going from ms to mp
	 */
	double RequiredPrefetchBWMax[DML2_MAX_PLANES];
	double cursor_bw[DML2_MAX_PLANES];
	double prefetch_cursor_bw[DML2_MAX_PLANES];
	double prefetch_vmrow_bw[DML2_MAX_PLANES];
	double final_flip_bw[DML2_MAX_PLANES];
	double meta_row_bw[DML2_MAX_PLANES];
	unsigned int meta_row_bytes[DML2_MAX_PLANES];
	double dpte_row_bw[DML2_MAX_PLANES];
	double excess_vactive_fill_bw_l[DML2_MAX_PLANES];
	double excess_vactive_fill_bw_c[DML2_MAX_PLANES];
	double surface_avg_vactive_required_bw[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max][DML2_MAX_PLANES];
	double surface_peak_required_bw[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max][DML2_MAX_PLANES];

	// Something that should be feedback to caller
	enum dml2_odm_mode ODMMode[DML2_MAX_PLANES];
	unsigned int SurfaceSizeInMALL[DML2_MAX_PLANES];
	unsigned int NoOfDPP[DML2_MAX_PLANES];
	bool MPCCombine[DML2_MAX_PLANES];
	double dcfclk_deepsleep;
	double MinDPPCLKUsingSingleDPP[DML2_MAX_PLANES];
	bool SingleDPPViewportSizeSupportPerSurface[DML2_MAX_PLANES];
	bool ImmediateFlipSupportedForPipe[DML2_MAX_PLANES];
	bool NotEnoughUrgentLatencyHiding[DML2_MAX_PLANES];
	bool NotEnoughUrgentLatencyHidingPre[DML2_MAX_PLANES];
	bool PTEBufferSizeNotExceeded[DML2_MAX_PLANES];
	bool DCCMetaBufferSizeNotExceeded[DML2_MAX_PLANES];
	unsigned int TotalNumberOfActiveDPP;
	unsigned int TotalNumberOfSingleDPPSurfaces;
	unsigned int TotalNumberOfDCCActiveDPP;
	unsigned int Total3dlutActive;

	unsigned int SubViewportLinesNeededInMALL[DML2_MAX_PLANES];
	double VActiveLatencyHidingMargin[DML2_MAX_PLANES];
	double VActiveLatencyHidingUs[DML2_MAX_PLANES];
	unsigned int MaxVStartupLines[DML2_MAX_PLANES];
	double dram_change_vactive_det_fill_delay_us[DML2_MAX_PLANES];

	unsigned int num_mcaches_l[DML2_MAX_PLANES];
	unsigned int mcache_row_bytes_l[DML2_MAX_PLANES];
	unsigned int mcache_row_bytes_per_channel_l[DML2_MAX_PLANES];
	unsigned int mcache_offsets_l[DML2_MAX_PLANES][DML2_MAX_MCACHES + 1];
	unsigned int mcache_shift_granularity_l[DML2_MAX_PLANES];

	unsigned int num_mcaches_c[DML2_MAX_PLANES];
	unsigned int mcache_row_bytes_c[DML2_MAX_PLANES];
	unsigned int mcache_row_bytes_per_channel_c[DML2_MAX_PLANES];
	unsigned int mcache_offsets_c[DML2_MAX_PLANES][DML2_MAX_MCACHES + 1];
	unsigned int mcache_shift_granularity_c[DML2_MAX_PLANES];

	bool mall_comb_mcache_l[DML2_MAX_PLANES];
	bool mall_comb_mcache_c[DML2_MAX_PLANES];
	bool lc_comb_mcache[DML2_MAX_PLANES];

	unsigned int vmpg_width_y[DML2_MAX_PLANES];
	unsigned int vmpg_height_y[DML2_MAX_PLANES];
	unsigned int vmpg_width_c[DML2_MAX_PLANES];
	unsigned int vmpg_height_c[DML2_MAX_PLANES];

	unsigned int meta_row_height_luma[DML2_MAX_PLANES];
	unsigned int meta_row_height_chroma[DML2_MAX_PLANES];
	unsigned int meta_row_bytes_per_row_ub_l[DML2_MAX_PLANES];
	unsigned int meta_row_bytes_per_row_ub_c[DML2_MAX_PLANES];
	unsigned int dpte_row_bytes_per_row_l[DML2_MAX_PLANES];
	unsigned int dpte_row_bytes_per_row_c[DML2_MAX_PLANES];

	unsigned int pstate_bytes_required_l[DML2_MAX_PLANES];
	unsigned int pstate_bytes_required_c[DML2_MAX_PLANES];
	unsigned int cursor_bytes_per_chunk[DML2_MAX_PLANES];
	unsigned int cursor_bytes_per_line[DML2_MAX_PLANES];

	unsigned int MaximumVStartup[DML2_MAX_PLANES];

	double HostVMInefficiencyFactor;
	double HostVMInefficiencyFactorPrefetch;

	unsigned int tdlut_pte_bytes_per_frame[DML2_MAX_PLANES];
	unsigned int tdlut_bytes_per_frame[DML2_MAX_PLANES];
	unsigned int tdlut_groups_per_2row_ub[DML2_MAX_PLANES];
	double tdlut_opt_time[DML2_MAX_PLANES];
	double tdlut_drain_time[DML2_MAX_PLANES];
	unsigned int tdlut_bytes_per_group[DML2_MAX_PLANES];

	double Tvm_trips_flip[DML2_MAX_PLANES];
	double Tr0_trips_flip[DML2_MAX_PLANES];
	double Tvm_trips_flip_rounded[DML2_MAX_PLANES];
	double Tr0_trips_flip_rounded[DML2_MAX_PLANES];

	unsigned int DSTYAfterScaler[DML2_MAX_PLANES];
	unsigned int DSTXAfterScaler[DML2_MAX_PLANES];

	enum dml2_pstate_method pstate_switch_modes[DML2_MAX_PLANES];
};

/// @brief A mega structure that houses various info for model programming step.
struct dml2_core_internal_mode_program {
	unsigned int qos_param_index; // to access the uclk dependent dpm table
	unsigned int active_min_uclk_dpm_index; // to access the min_clk table
	double FabricClock; /// <brief Basically just the clock freq at the min (or given) state
	//double DCFCLK; /// <brief Basically just the clock freq at the min (or given) state and max combine setting
	double dram_bw_mbps;
	double min_available_urgent_bandwidth_MBps; /// <brief Minimum guaranteed available urgent return bandwidth in MBps
	double uclk_freq_mhz;
	unsigned int NoOfDPP[DML2_MAX_PLANES];
	enum dml2_odm_mode ODMMode[DML2_MAX_PLANES];

	//-------------
	// Intermediate/Informational
	//-------------
	double UrgentLatency;
	double TripToMemory;
	double MetaTripToMemory;
	unsigned int VInitPreFillY[DML2_MAX_PLANES];
	unsigned int VInitPreFillC[DML2_MAX_PLANES];
	unsigned int MaxNumSwathY[DML2_MAX_PLANES];
	unsigned int MaxNumSwathC[DML2_MAX_PLANES];
	unsigned int full_swath_bytes_l[DML2_MAX_PLANES];
	unsigned int full_swath_bytes_c[DML2_MAX_PLANES];

	double BytePerPixelInDETY[DML2_MAX_PLANES];
	double BytePerPixelInDETC[DML2_MAX_PLANES];
	unsigned int BytePerPixelY[DML2_MAX_PLANES];
	unsigned int BytePerPixelC[DML2_MAX_PLANES];
	unsigned int SwathWidthY[DML2_MAX_PLANES]; // per-pipe
	unsigned int SwathWidthC[DML2_MAX_PLANES]; // per-pipe
	unsigned int req_per_swath_ub_l[DML2_MAX_PLANES];
	unsigned int req_per_swath_ub_c[DML2_MAX_PLANES];
	unsigned int SwathWidthSingleDPPY[DML2_MAX_PLANES];
	unsigned int SwathWidthSingleDPPC[DML2_MAX_PLANES];
	double vactive_sw_bw_l[DML2_MAX_PLANES];
	double vactive_sw_bw_c[DML2_MAX_PLANES];
	double excess_vactive_fill_bw_l[DML2_MAX_PLANES];
	double excess_vactive_fill_bw_c[DML2_MAX_PLANES];

	unsigned int PixelPTEBytesPerRow[DML2_MAX_PLANES];
	unsigned int vm_bytes[DML2_MAX_PLANES];
	unsigned int PrefetchSourceLinesY[DML2_MAX_PLANES];
	double RequiredPrefetchPixelDataBWLuma[DML2_MAX_PLANES];
	double RequiredPrefetchPixelDataBWChroma[DML2_MAX_PLANES];
	unsigned int PrefetchSourceLinesC[DML2_MAX_PLANES];
	double PSCL_THROUGHPUT[DML2_MAX_PLANES];
	double PSCL_THROUGHPUT_CHROMA[DML2_MAX_PLANES];
	unsigned int DSCDelay[DML2_MAX_PLANES];
	double DPPCLKUsingSingleDPP[DML2_MAX_PLANES];

	unsigned int Read256BlockHeightY[DML2_MAX_PLANES];
	unsigned int Read256BlockWidthY[DML2_MAX_PLANES];
	unsigned int Read256BlockHeightC[DML2_MAX_PLANES];
	unsigned int Read256BlockWidthC[DML2_MAX_PLANES];
	unsigned int MacroTileHeightY[DML2_MAX_PLANES];
	unsigned int MacroTileHeightC[DML2_MAX_PLANES];
	unsigned int MacroTileWidthY[DML2_MAX_PLANES];
	unsigned int MacroTileWidthC[DML2_MAX_PLANES];
	double MaximumSwathWidthLuma[DML2_MAX_PLANES];
	double MaximumSwathWidthChroma[DML2_MAX_PLANES];

	bool surf_linear128_l[DML2_MAX_PLANES];
	bool surf_linear128_c[DML2_MAX_PLANES];

	unsigned int SurfaceSizeInTheMALL[DML2_MAX_PLANES];
	double VRatioPrefetchY[DML2_MAX_PLANES];
	double VRatioPrefetchC[DML2_MAX_PLANES];
	double Tno_bw[DML2_MAX_PLANES];
	double Tno_bw_flip[DML2_MAX_PLANES];
	double final_flip_bw[DML2_MAX_PLANES];
	double prefetch_vmrow_bw[DML2_MAX_PLANES];
	double cursor_bw[DML2_MAX_PLANES];
	double prefetch_cursor_bw[DML2_MAX_PLANES];
	double WritebackDelay[DML2_MAX_PLANES];
	unsigned int dpte_row_height[DML2_MAX_PLANES];
	unsigned int dpte_row_height_linear[DML2_MAX_PLANES];
	unsigned int dpte_row_width_luma_ub[DML2_MAX_PLANES];
	unsigned int dpte_row_width_chroma_ub[DML2_MAX_PLANES];
	unsigned int dpte_row_height_chroma[DML2_MAX_PLANES];
	unsigned int dpte_row_height_linear_chroma[DML2_MAX_PLANES];
	unsigned int vm_group_bytes[DML2_MAX_PLANES];
	unsigned int dpte_group_bytes[DML2_MAX_PLANES];

	double dpte_row_bw[DML2_MAX_PLANES];
	double time_per_tdlut_group[DML2_MAX_PLANES];
	double UrgentBurstFactorCursor[DML2_MAX_PLANES];
	double UrgentBurstFactorCursorPre[DML2_MAX_PLANES];
	double UrgentBurstFactorLuma[DML2_MAX_PLANES];
	double UrgentBurstFactorLumaPre[DML2_MAX_PLANES];
	double UrgentBurstFactorChroma[DML2_MAX_PLANES];
	double UrgentBurstFactorChromaPre[DML2_MAX_PLANES];

	double MaximumSwathWidthInLineBufferLuma;
	double MaximumSwathWidthInLineBufferChroma;

	unsigned int vmpg_width_y[DML2_MAX_PLANES];
	unsigned int vmpg_height_y[DML2_MAX_PLANES];
	unsigned int vmpg_width_c[DML2_MAX_PLANES];
	unsigned int vmpg_height_c[DML2_MAX_PLANES];

	double meta_row_bw[DML2_MAX_PLANES];
	unsigned int meta_row_bytes[DML2_MAX_PLANES];
	unsigned int meta_req_width[DML2_MAX_PLANES];
	unsigned int meta_req_height[DML2_MAX_PLANES];
	unsigned int meta_row_width[DML2_MAX_PLANES];
	unsigned int meta_row_height[DML2_MAX_PLANES];
	unsigned int meta_req_width_chroma[DML2_MAX_PLANES];
	unsigned int meta_row_height_chroma[DML2_MAX_PLANES];
	unsigned int meta_row_width_chroma[DML2_MAX_PLANES];
	unsigned int meta_req_height_chroma[DML2_MAX_PLANES];

	unsigned int swath_width_luma_ub[DML2_MAX_PLANES];
	unsigned int swath_width_chroma_ub[DML2_MAX_PLANES];
	unsigned int PixelPTEReqWidthY[DML2_MAX_PLANES];
	unsigned int PixelPTEReqHeightY[DML2_MAX_PLANES];
	unsigned int PTERequestSizeY[DML2_MAX_PLANES];
	unsigned int PixelPTEReqWidthC[DML2_MAX_PLANES];
	unsigned int PixelPTEReqHeightC[DML2_MAX_PLANES];
	unsigned int PTERequestSizeC[DML2_MAX_PLANES];

	double TWait[DML2_MAX_PLANES];
	double Tdmdl_vm_raw[DML2_MAX_PLANES];
	double Tdmdl_vm[DML2_MAX_PLANES];
	double Tdmdl_raw[DML2_MAX_PLANES];
	double Tdmdl[DML2_MAX_PLANES];
	double TSetup[DML2_MAX_PLANES];
	unsigned int dpde0_bytes_per_frame_ub_l[DML2_MAX_PLANES];
	unsigned int dpde0_bytes_per_frame_ub_c[DML2_MAX_PLANES];

	unsigned int meta_pte_bytes_per_frame_ub_l[DML2_MAX_PLANES];
	unsigned int meta_pte_bytes_per_frame_ub_c[DML2_MAX_PLANES];

	bool UnboundedRequestEnabled;
	unsigned int CompressedBufferSizeInkByte;
	unsigned int compbuf_reserved_space_64b;
	bool hw_debug5;
	unsigned int dcfclk_deep_sleep_hysteresis;
	unsigned int min_return_latency_in_dcfclk;

	bool NotEnoughUrgentLatencyHiding[DML2_MAX_PLANES];
	bool NotEnoughUrgentLatencyHidingPre[DML2_MAX_PLANES];
	double ExtraLatency;
	double ExtraLatency_sr;
	double ExtraLatencyPrefetch;
	bool PrefetchAndImmediateFlipSupported;
	double TotalDataReadBandwidth;
	double BandwidthAvailableForImmediateFlip;
	bool NotEnoughTimeForDynamicMetadata[DML2_MAX_PLANES];

	bool use_one_row_for_frame[DML2_MAX_PLANES];
	bool use_one_row_for_frame_flip[DML2_MAX_PLANES];

	double TCalc;
	unsigned int TotImmediateFlipBytes;

	unsigned int MaxTotalDETInKByte;
	unsigned int NomDETInKByte;
	unsigned int MinCompressedBufferSizeInKByte;
	double PixelClockBackEnd[DML2_MAX_PLANES];
	double OutputBpp[DML2_MAX_PLANES];
	bool dsc_enable[DML2_MAX_PLANES];
	unsigned int num_dsc_slices[DML2_MAX_PLANES];
	unsigned int meta_row_bytes_per_row_ub_l[DML2_MAX_PLANES];
	unsigned int meta_row_bytes_per_row_ub_c[DML2_MAX_PLANES];
	unsigned int dpte_row_bytes_per_row_l[DML2_MAX_PLANES];
	unsigned int dpte_row_bytes_per_row_c[DML2_MAX_PLANES];
	unsigned int cursor_bytes_per_chunk[DML2_MAX_PLANES];
	unsigned int cursor_bytes_per_line[DML2_MAX_PLANES];
	unsigned int MaxVStartupLines[DML2_MAX_PLANES]; /// <brief more like vblank for the plane's OTG
	double HostVMInefficiencyFactor;
	double HostVMInefficiencyFactorPrefetch;
	unsigned int tdlut_pte_bytes_per_frame[DML2_MAX_PLANES];
	unsigned int tdlut_bytes_per_frame[DML2_MAX_PLANES];
	unsigned int tdlut_groups_per_2row_ub[DML2_MAX_PLANES];
	double tdlut_opt_time[DML2_MAX_PLANES];
	double tdlut_drain_time[DML2_MAX_PLANES];
	unsigned int tdlut_bytes_per_group[DML2_MAX_PLANES];
	double Tvm_trips_flip[DML2_MAX_PLANES];
	double Tr0_trips_flip[DML2_MAX_PLANES];
	double Tvm_trips_flip_rounded[DML2_MAX_PLANES];
	double Tr0_trips_flip_rounded[DML2_MAX_PLANES];
	bool immediate_flip_required; // any pipes need immediate flip
	double SOCCLK; /// <brief Basically just the clock freq at the min (or given) state
	double TotalWRBandwidth;
	double max_urgent_latency_us;
	double df_response_time_us;

	// -------------------
	// Output
	// -------------------
	unsigned int pipe_plane[DML2_MAX_PLANES]; // <brief used mainly by dv to map the pipe inst to plane index within DML core; the plane idx of a pipe
	unsigned int num_active_pipes;

	bool NoTimeToPrefetch[DML2_MAX_PLANES]; // <brief Prefetch schedule calculation result

	// Support
	bool UrgVactiveBandwidthSupport;
	bool PrefetchScheduleSupported;
	bool UrgentBandwidthSupport;
	bool PrefetchModeSupported; // <brief Is the prefetch mode (bandwidth and latency) supported
	bool ImmediateFlipSupported;
	bool ImmediateFlipSupportedForPipe[DML2_MAX_PLANES];
	bool dcfclk_support;

	// Clock
	double Dcfclk;
	double Dispclk; // <brief dispclk being used in mode programming
	double Dppclk[DML2_MAX_PLANES]; // <brief dppclk being used in mode programming
	double GlobalDPPCLK;

	double DSCCLK[DML2_MAX_PLANES]; //< brief Required DSCCLK freq. Backend; not used in any subsequent calculations for now
	double DCFCLKDeepSleep;

	// ARB reg
	bool DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE;
	struct dml2_core_internal_watermarks Watermark;

	// DCC compression control
	unsigned int request_size_bytes_luma[DML2_MAX_PLANES];
	unsigned int request_size_bytes_chroma[DML2_MAX_PLANES];
	enum dml2_core_internal_request_type RequestLuma[DML2_MAX_PLANES];
	enum dml2_core_internal_request_type RequestChroma[DML2_MAX_PLANES];
	unsigned int DCCYMaxUncompressedBlock[DML2_MAX_PLANES];
	unsigned int DCCYMaxCompressedBlock[DML2_MAX_PLANES];
	unsigned int DCCYIndependentBlock[DML2_MAX_PLANES];
	unsigned int DCCCMaxUncompressedBlock[DML2_MAX_PLANES];
	unsigned int DCCCMaxCompressedBlock[DML2_MAX_PLANES];
	unsigned int DCCCIndependentBlock[DML2_MAX_PLANES];

	// Stutter Efficiency
	double StutterEfficiency;
	double StutterEfficiencyNotIncludingVBlank;
	unsigned int NumberOfStutterBurstsPerFrame;
	double Z8StutterEfficiency;
	unsigned int Z8NumberOfStutterBurstsPerFrame;
	double Z8StutterEfficiencyNotIncludingVBlank;
	double LowPowerStutterEfficiency;
	double LowPowerStutterEfficiencyNotIncludingVBlank;
	unsigned int LowPowerNumberOfStutterBurstsPerFrame;
	double StutterPeriod;
	double Z8StutterEfficiencyBestCase;
	unsigned int Z8NumberOfStutterBurstsPerFrameBestCase;
	double Z8StutterEfficiencyNotIncludingVBlankBestCase;
	double StutterPeriodBestCase;

	// DLG TTU reg
	double MIN_DST_Y_NEXT_START[DML2_MAX_PLANES];
	bool VREADY_AT_OR_AFTER_VSYNC[DML2_MAX_PLANES];
	unsigned int DSTYAfterScaler[DML2_MAX_PLANES];
	unsigned int DSTXAfterScaler[DML2_MAX_PLANES];
	double dst_y_prefetch[DML2_MAX_PLANES];
	double dst_y_per_vm_vblank[DML2_MAX_PLANES];
	double dst_y_per_row_vblank[DML2_MAX_PLANES];
	double dst_y_per_vm_flip[DML2_MAX_PLANES];
	double dst_y_per_row_flip[DML2_MAX_PLANES];
	double MinTTUVBlank[DML2_MAX_PLANES];
	double DisplayPipeLineDeliveryTimeLuma[DML2_MAX_PLANES];
	double DisplayPipeLineDeliveryTimeChroma[DML2_MAX_PLANES];
	double DisplayPipeLineDeliveryTimeLumaPrefetch[DML2_MAX_PLANES];
	double DisplayPipeLineDeliveryTimeChromaPrefetch[DML2_MAX_PLANES];
	double DisplayPipeRequestDeliveryTimeLuma[DML2_MAX_PLANES];
	double DisplayPipeRequestDeliveryTimeChroma[DML2_MAX_PLANES];
	double DisplayPipeRequestDeliveryTimeLumaPrefetch[DML2_MAX_PLANES];
	double DisplayPipeRequestDeliveryTimeChromaPrefetch[DML2_MAX_PLANES];
	unsigned int CursorDstXOffset[DML2_MAX_PLANES];
	unsigned int CursorDstYOffset[DML2_MAX_PLANES];
	unsigned int CursorChunkHDLAdjust[DML2_MAX_PLANES];

	double DST_Y_PER_PTE_ROW_NOM_L[DML2_MAX_PLANES];
	double DST_Y_PER_PTE_ROW_NOM_C[DML2_MAX_PLANES];
	double time_per_pte_group_nom_luma[DML2_MAX_PLANES];
	double time_per_pte_group_nom_chroma[DML2_MAX_PLANES];
	double time_per_pte_group_vblank_luma[DML2_MAX_PLANES];
	double time_per_pte_group_vblank_chroma[DML2_MAX_PLANES];
	double time_per_pte_group_flip_luma[DML2_MAX_PLANES];
	double time_per_pte_group_flip_chroma[DML2_MAX_PLANES];
	double TimePerVMGroupVBlank[DML2_MAX_PLANES];
	double TimePerVMGroupFlip[DML2_MAX_PLANES];
	double TimePerVMRequestVBlank[DML2_MAX_PLANES];
	double TimePerVMRequestFlip[DML2_MAX_PLANES];

	double DST_Y_PER_META_ROW_NOM_L[DML2_MAX_PLANES];
	double DST_Y_PER_META_ROW_NOM_C[DML2_MAX_PLANES];
	double TimePerMetaChunkNominal[DML2_MAX_PLANES];
	double TimePerChromaMetaChunkNominal[DML2_MAX_PLANES];
	double TimePerMetaChunkVBlank[DML2_MAX_PLANES];
	double TimePerChromaMetaChunkVBlank[DML2_MAX_PLANES];
	double TimePerMetaChunkFlip[DML2_MAX_PLANES];
	double TimePerChromaMetaChunkFlip[DML2_MAX_PLANES];

	double FractionOfUrgentBandwidth;
	double FractionOfUrgentBandwidthImmediateFlip;
	double FractionOfUrgentBandwidthMALL;

	// RQ registers
	bool PTE_BUFFER_MODE[DML2_MAX_PLANES];
	unsigned int BIGK_FRAGMENT_SIZE[DML2_MAX_PLANES];
	double VActiveLatencyHidingUs[DML2_MAX_PLANES];
	unsigned int SubViewportLinesNeededInMALL[DML2_MAX_PLANES];
	bool is_using_mall_for_ss[DML2_MAX_PLANES];

	// OTG
	unsigned int VStartupMin[DML2_MAX_PLANES]; /// <brief Minimum vstartup to meet the prefetch schedule (i.e. the prefetch solution can be found at this vstartup time); not the actual global sync vstartup pos.
	unsigned int VStartup[DML2_MAX_PLANES]; /// <brief The vstartup value for OTG programming (will set to max vstartup; but now bounded by min(vblank_nom. actual vblank))
	unsigned int VUpdateOffsetPix[DML2_MAX_PLANES];
	unsigned int VUpdateWidthPix[DML2_MAX_PLANES];
	unsigned int VReadyOffsetPix[DML2_MAX_PLANES];
	unsigned int pstate_keepout_dst_lines[DML2_MAX_PLANES];

	// Latency and Support
	double MaxActiveFCLKChangeLatencySupported;
	bool USRRetrainingSupport;
	bool g6_temp_read_support;
	bool temp_read_or_ppt_support;
	enum dml2_pstate_change_support FCLKChangeSupport[DML2_MAX_PLANES];
	enum dml2_pstate_change_support DRAMClockChangeSupport[DML2_MAX_PLANES];
	bool global_dram_clock_change_supported;
	bool global_fclk_change_supported;
	double MaxActiveDRAMClockChangeLatencySupported[DML2_MAX_PLANES];
	double WritebackAllowFCLKChangeEndPosition[DML2_MAX_PLANES];
	double WritebackAllowDRAMClockChangeEndPosition[DML2_MAX_PLANES];

	// buffer sizing
	unsigned int DETBufferSizeInKByte[DML2_MAX_PLANES]; // <brief Recommended DET size configuration for this plane. All pipes under this plane should program the DET buffer size to the calculated value.
	unsigned int DETBufferSizeY[DML2_MAX_PLANES];
	unsigned int DETBufferSizeC[DML2_MAX_PLANES];
	unsigned int SwathHeightY[DML2_MAX_PLANES];
	unsigned int SwathHeightC[DML2_MAX_PLANES];

	double urg_vactive_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max]; // active bandwidth, scaled by urg burst factor
	double urg_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max]; // include vm, prefetch, active bandwidth, scaled by urg burst factor
	double urg_bandwidth_required_qual[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max]; // include vm, prefetch, active bandwidth, scaled by urg burst factor, use qual_row_bw
	double urg_bandwidth_required_flip[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max]; // include vm, prefetch, active bandwidth + flip
	double non_urg_bandwidth_required[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max]; // same as urg_bandwidth, except not scaled by urg burst factor
	double non_urg_bandwidth_required_flip[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max];

	double avg_bandwidth_available_min[dml2_core_internal_soc_state_max];
	double avg_bandwidth_available[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max];
	double urg_bandwidth_available_min[dml2_core_internal_soc_state_max]; // min between SDP and DRAM
	double urg_bandwidth_available[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max];
	double urg_bandwidth_available_vm_only[dml2_core_internal_soc_state_max]; // the min of sdp bw and dram_vm_only bw, sdp has no different derate for vm/non-vm traffic etc.
	double urg_bandwidth_available_pixel_and_vm[dml2_core_internal_soc_state_max]; // the min of sdp bw and dram_pixel_and_vm bw, sdp has no different derate for vm/non-vm etc.

	double dcc_dram_bw_nom_overhead_factor_p0[DML2_MAX_PLANES];
	double dcc_dram_bw_nom_overhead_factor_p1[DML2_MAX_PLANES];
	double dcc_dram_bw_pref_overhead_factor_p0[DML2_MAX_PLANES];
	double dcc_dram_bw_pref_overhead_factor_p1[DML2_MAX_PLANES];
	double mall_prefetch_sdp_overhead_factor[DML2_MAX_PLANES];
	double mall_prefetch_dram_overhead_factor[DML2_MAX_PLANES];

	unsigned int num_mcaches_l[DML2_MAX_PLANES];
	unsigned int mcache_row_bytes_l[DML2_MAX_PLANES];
	unsigned int mcache_row_bytes_per_channel_l[DML2_MAX_PLANES];
	unsigned int mcache_offsets_l[DML2_MAX_PLANES][DML2_MAX_MCACHES + 1];
	unsigned int mcache_shift_granularity_l[DML2_MAX_PLANES];

	unsigned int num_mcaches_c[DML2_MAX_PLANES];
	unsigned int mcache_row_bytes_c[DML2_MAX_PLANES];
	unsigned int mcache_row_bytes_per_channel_c[DML2_MAX_PLANES];
	unsigned int mcache_offsets_c[DML2_MAX_PLANES][DML2_MAX_MCACHES + 1];
	unsigned int mcache_shift_granularity_c[DML2_MAX_PLANES];

	bool mall_comb_mcache_l[DML2_MAX_PLANES];
	bool mall_comb_mcache_c[DML2_MAX_PLANES];
	bool lc_comb_mcache[DML2_MAX_PLANES];

	double impacted_prefetch_margin_us[DML2_MAX_PLANES];
};

struct dml2_core_internal_SOCParametersList {
	double UrgentLatency;
	double ExtraLatency_sr;
	double ExtraLatency;
	double WritebackLatency;
	double DRAMClockChangeLatency;
	double FCLKChangeLatency;
	double SRExitTime;
	double SREnterPlusExitTime;
	double SRExitTimeLowPower;
	double SREnterPlusExitTimeLowPower;
	double SRExitZ8Time;
	double SREnterPlusExitZ8Time;
	double USRRetrainingLatency;
	double SMNLatency;
	double g6_temp_read_blackout_us;
	double temp_read_or_ppt_blackout_us;
	double max_urgent_latency_us;
	double df_response_time_us;
	enum dml2_qos_param_type qos_type;
};

struct dml2_core_calcs_mode_support_locals {
	double PixelClockBackEnd[DML2_MAX_PLANES];
	double OutputBpp[DML2_MAX_PLANES];

	unsigned int meta_row_height_luma[DML2_MAX_PLANES];
	unsigned int meta_row_height_chroma[DML2_MAX_PLANES];
	unsigned int meta_row_bytes_per_row_ub_l[DML2_MAX_PLANES];
	unsigned int meta_row_bytes_per_row_ub_c[DML2_MAX_PLANES];
	unsigned int dpte_row_bytes_per_row_l[DML2_MAX_PLANES];
	unsigned int dpte_row_bytes_per_row_c[DML2_MAX_PLANES];

	bool dummy_boolean[3];
	unsigned int dummy_integer[3];
	unsigned int dummy_integer_array[36][DML2_MAX_PLANES];
	enum dml2_odm_mode dummy_odm_mode[DML2_MAX_PLANES];
	bool dummy_boolean_array[2][DML2_MAX_PLANES];
	double dummy_single[3];
	double dummy_single_array[DML2_MAX_PLANES];
	struct dml2_core_internal_watermarks dummy_watermark;
	double dummy_bw[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max];
	double surface_dummy_bw[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max][DML2_MAX_PLANES];

	unsigned int MaximumVStartup[DML2_MAX_PLANES];
	unsigned int DSTYAfterScaler[DML2_MAX_PLANES];
	unsigned int DSTXAfterScaler[DML2_MAX_PLANES];
	struct dml2_core_internal_SOCParametersList mSOCParameters;
	struct dml2_core_internal_DmlPipe myPipe;
	struct dml2_core_internal_DmlPipe SurfParameters[DML2_MAX_PLANES];
	unsigned int TotalNumberOfActiveWriteback;
	unsigned int MaximumSwathWidthSupportLuma;
	unsigned int MaximumSwathWidthSupportChroma;
	bool MPCCombineMethodAsNeededForPStateChangeAndVoltage;
	bool MPCCombineMethodAsPossible;
	bool TotalAvailablePipesSupportNoDSC;
	unsigned int NumberOfDPPNoDSC;
	enum dml2_odm_mode ODMModeNoDSC;
	double RequiredDISPCLKPerSurfaceNoDSC;
	bool TotalAvailablePipesSupportDSC;
	unsigned int NumberOfDPPDSC;
	enum dml2_odm_mode ODMModeDSC;
	double RequiredDISPCLKPerSurfaceDSC;
	double BWOfNonCombinedSurfaceOfMaximumBandwidth;
	unsigned int NumberOfNonCombinedSurfaceOfMaximumBandwidth;
	unsigned int TotalNumberOfActiveOTG;
	unsigned int TotalNumberOfActiveHDMIFRL;
	unsigned int TotalNumberOfActiveDP2p0;
	unsigned int TotalNumberOfActiveDP2p0Outputs;
	unsigned int TotalSlots;
	unsigned int DSCFormatFactor;
	unsigned int TotalDSCUnitsRequired;
	unsigned int ReorderingBytes;
	bool ImmediateFlipRequired;
	bool FullFrameMALLPStateMethod;
	bool SubViewportMALLPStateMethod;
	bool PhantomPipeMALLPStateMethod;
	bool SubViewportMALLRefreshGreaterThan120Hz;

	double HostVMInefficiencyFactor;
	double HostVMInefficiencyFactorPrefetch;
	unsigned int MaxVStartup;
	double PixelClockBackEndFactor;
	unsigned int NumDSCUnitRequired;

	double Tvm_trips[DML2_MAX_PLANES];
	double Tr0_trips[DML2_MAX_PLANES];
	double Tvm_trips_flip[DML2_MAX_PLANES];
	double Tr0_trips_flip[DML2_MAX_PLANES];
	double Tvm_trips_flip_rounded[DML2_MAX_PLANES];
	double Tr0_trips_flip_rounded[DML2_MAX_PLANES];
	unsigned int per_pipe_flip_bytes[DML2_MAX_PLANES];

	unsigned int vmpg_width_y[DML2_MAX_PLANES];
	unsigned int vmpg_height_y[DML2_MAX_PLANES];
	unsigned int vmpg_width_c[DML2_MAX_PLANES];
	unsigned int vmpg_height_c[DML2_MAX_PLANES];
	unsigned int full_swath_bytes_l[DML2_MAX_PLANES];
	unsigned int full_swath_bytes_c[DML2_MAX_PLANES];

	unsigned int tdlut_pte_bytes_per_frame[DML2_MAX_PLANES];
	unsigned int tdlut_bytes_per_frame[DML2_MAX_PLANES];
	unsigned int tdlut_row_bytes[DML2_MAX_PLANES];
	unsigned int tdlut_groups_per_2row_ub[DML2_MAX_PLANES];
	double tdlut_opt_time[DML2_MAX_PLANES];
	double tdlut_drain_time[DML2_MAX_PLANES];
	unsigned int tdlut_bytes_to_deliver[DML2_MAX_PLANES];
	unsigned int tdlut_bytes_per_group[DML2_MAX_PLANES];

	unsigned int cursor_bytes_per_chunk[DML2_MAX_PLANES];
	unsigned int cursor_bytes_per_line[DML2_MAX_PLANES];
	unsigned int cursor_lines_per_chunk[DML2_MAX_PLANES];
	unsigned int cursor_bytes[DML2_MAX_PLANES];
	bool stream_visited[DML2_MAX_PLANES];

	unsigned int pstate_bytes_required_l[DML2_MAX_PLANES];
	unsigned int pstate_bytes_required_c[DML2_MAX_PLANES];

	double prefetch_sw_bytes[DML2_MAX_PLANES];
	double Tpre_rounded[DML2_MAX_PLANES];
	double Tpre_oto[DML2_MAX_PLANES];
	bool recalc_prefetch_schedule;
	bool recalc_prefetch_done;
	double impacted_dst_y_pre[DML2_MAX_PLANES];
	double line_times[DML2_MAX_PLANES];
	enum dml2_source_format_class pixel_format[DML2_MAX_PLANES];
	unsigned int lb_source_lines_l[DML2_MAX_PLANES];
	unsigned int lb_source_lines_c[DML2_MAX_PLANES];
	double prefetch_swath_time_us[DML2_MAX_PLANES];
};

struct dml2_core_calcs_mode_programming_locals {
	double PixelClockBackEnd[DML2_MAX_PLANES];
	double OutputBpp[DML2_MAX_PLANES];
	unsigned int num_active_planes; // <brief As determined by either e2e_pipe_param or display_cfg
	unsigned int MaxTotalDETInKByte;
	unsigned int NomDETInKByte;
	unsigned int MinCompressedBufferSizeInKByte;
	double SOCCLK; /// <brief Basically just the clock freq at the min (or given) state

	double dummy_bw[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max];
	double surface_dummy_bw[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max][DML2_MAX_PLANES];
	double surface_dummy_bw0[dml2_core_internal_soc_state_max][dml2_core_internal_bw_max][DML2_MAX_PLANES];
	unsigned int dummy_integer_array[4][DML2_MAX_PLANES];
	enum dml2_output_encoder_class dummy_output_encoder_array[DML2_MAX_PLANES];
	double dummy_single_array[2][DML2_MAX_PLANES];
	unsigned int dummy_long_array[8][DML2_MAX_PLANES];
	bool dummy_boolean_array[2][DML2_MAX_PLANES];
	bool dummy_boolean[2];
	double dummy_single[2];
	struct dml2_core_internal_watermarks dummy_watermark;

	unsigned int DSCFormatFactor;
	struct dml2_core_internal_DmlPipe SurfaceParameters[DML2_MAX_PLANES];
	unsigned int ReorderingBytes;
	double HostVMInefficiencyFactor;
	double HostVMInefficiencyFactorPrefetch;
	unsigned int TotalDCCActiveDPP;
	unsigned int TotalActiveDPP;
	unsigned int Total3dlutActive;
	unsigned int MaxVStartupLines[DML2_MAX_PLANES]; /// <brief more like vblank for the plane's OTG
	bool immediate_flip_required; // any pipes need immediate flip
	bool DestinationLineTimesForPrefetchLessThan2;
	bool VRatioPrefetchMoreThanMax;
	double MaxTotalRDBandwidthNotIncludingMALLPrefetch;
	struct dml2_core_internal_SOCParametersList mmSOCParameters;
	double Tvstartup_margin;
	double dlg_vblank_start;
	double LSetup;
	double blank_lines_remaining;
	double WRBandwidth;
	struct dml2_core_internal_DmlPipe myPipe;
	double PixelClockBackEndFactor;
	unsigned int vmpg_width_y[DML2_MAX_PLANES];
	unsigned int vmpg_height_y[DML2_MAX_PLANES];
	unsigned int vmpg_width_c[DML2_MAX_PLANES];
	unsigned int vmpg_height_c[DML2_MAX_PLANES];
	unsigned int full_swath_bytes_l[DML2_MAX_PLANES];
	unsigned int full_swath_bytes_c[DML2_MAX_PLANES];

	unsigned int meta_row_bytes_per_row_ub_l[DML2_MAX_PLANES];
	unsigned int meta_row_bytes_per_row_ub_c[DML2_MAX_PLANES];
	unsigned int dpte_row_bytes_per_row_l[DML2_MAX_PLANES];
	unsigned int dpte_row_bytes_per_row_c[DML2_MAX_PLANES];

	unsigned int tdlut_pte_bytes_per_frame[DML2_MAX_PLANES];
	unsigned int tdlut_bytes_per_frame[DML2_MAX_PLANES];
	unsigned int tdlut_row_bytes[DML2_MAX_PLANES];
	unsigned int tdlut_groups_per_2row_ub[DML2_MAX_PLANES];
	double tdlut_opt_time[DML2_MAX_PLANES];
	double tdlut_drain_time[DML2_MAX_PLANES];
	unsigned int tdlut_bytes_to_deliver[DML2_MAX_PLANES];
	unsigned int tdlut_bytes_per_group[DML2_MAX_PLANES];

	unsigned int cursor_bytes_per_chunk[DML2_MAX_PLANES];
	unsigned int cursor_bytes_per_line[DML2_MAX_PLANES];
	unsigned int cursor_lines_per_chunk[DML2_MAX_PLANES];
	unsigned int cursor_bytes[DML2_MAX_PLANES];

	double Tvm_trips[DML2_MAX_PLANES];
	double Tr0_trips[DML2_MAX_PLANES];
	double Tvm_trips_flip[DML2_MAX_PLANES];
	double Tr0_trips_flip[DML2_MAX_PLANES];
	double Tvm_trips_flip_rounded[DML2_MAX_PLANES];
	double Tr0_trips_flip_rounded[DML2_MAX_PLANES];
	unsigned int per_pipe_flip_bytes[DML2_MAX_PLANES];

	unsigned int pstate_bytes_required_l[DML2_MAX_PLANES];
	unsigned int pstate_bytes_required_c[DML2_MAX_PLANES];

	double prefetch_sw_bytes[DML2_MAX_PLANES];
	double Tpre_rounded[DML2_MAX_PLANES];
	double Tpre_oto[DML2_MAX_PLANES];
	bool recalc_prefetch_schedule;
	double impacted_dst_y_pre[DML2_MAX_PLANES];
	double line_times[DML2_MAX_PLANES];
	enum dml2_source_format_class pixel_format[DML2_MAX_PLANES];
	unsigned int lb_source_lines_l[DML2_MAX_PLANES];
	unsigned int lb_source_lines_c[DML2_MAX_PLANES];
	unsigned int num_dsc_slices[DML2_MAX_PLANES];
	bool dsc_enable[DML2_MAX_PLANES];
};

struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals {
	double ActiveDRAMClockChangeLatencyMargin[DML2_MAX_PLANES];
	double ActiveFCLKChangeLatencyMargin[DML2_MAX_PLANES];
	double USRRetrainingLatencyMargin[DML2_MAX_PLANES];
	double g6_temp_read_latency_margin[DML2_MAX_PLANES];
	double temp_read_or_ppt_latency_margin[DML2_MAX_PLANES];

	double EffectiveLBLatencyHidingY;
	double EffectiveLBLatencyHidingC;
	double LinesInDETY[DML2_MAX_PLANES];
	double LinesInDETC[DML2_MAX_PLANES];
	unsigned int LinesInDETYRoundedDownToSwath[DML2_MAX_PLANES];
	unsigned int LinesInDETCRoundedDownToSwath[DML2_MAX_PLANES];
	double FullDETBufferingTimeY;
	double FullDETBufferingTimeC;
	double WritebackDRAMClockChangeLatencyMargin;
	double WritebackFCLKChangeLatencyMargin;
	double WritebackLatencyHiding;

	unsigned int TotalActiveWriteback;
	unsigned int LBLatencyHidingSourceLinesY[DML2_MAX_PLANES];
	unsigned int LBLatencyHidingSourceLinesC[DML2_MAX_PLANES];
	double TotalPixelBW;
	double EffectiveDETBufferSizeY;
	double ActiveClockChangeLatencyHidingY;
	double ActiveClockChangeLatencyHidingC;
	double ActiveClockChangeLatencyHiding;
	unsigned int dst_y_pstate;
	unsigned int src_y_pstate_l;
	unsigned int src_y_pstate_c;
	unsigned int src_y_ahead_l;
	unsigned int src_y_ahead_c;
	unsigned int sub_vp_lines_l;
	unsigned int sub_vp_lines_c;

};

struct dml2_core_calcs_CalculateVMRowAndSwath_locals {
	unsigned int PTEBufferSizeInRequestsForLuma[DML2_MAX_PLANES];
	unsigned int PTEBufferSizeInRequestsForChroma[DML2_MAX_PLANES];
	unsigned int vm_bytes_l;
	unsigned int vm_bytes_c;
	unsigned int PixelPTEBytesPerRowY[DML2_MAX_PLANES];
	unsigned int PixelPTEBytesPerRowC[DML2_MAX_PLANES];
	unsigned int PixelPTEBytesPerRowStorageY[DML2_MAX_PLANES];
	unsigned int PixelPTEBytesPerRowStorageC[DML2_MAX_PLANES];
	unsigned int PixelPTEBytesPerRowY_one_row_per_frame[DML2_MAX_PLANES];
	unsigned int PixelPTEBytesPerRowC_one_row_per_frame[DML2_MAX_PLANES];
	unsigned int dpte_row_width_luma_ub_one_row_per_frame[DML2_MAX_PLANES];
	unsigned int dpte_row_height_luma_one_row_per_frame[DML2_MAX_PLANES];
	unsigned int dpte_row_width_chroma_ub_one_row_per_frame[DML2_MAX_PLANES];
	unsigned int dpte_row_height_chroma_one_row_per_frame[DML2_MAX_PLANES];
	bool one_row_per_frame_fits_in_buffer[DML2_MAX_PLANES];
	unsigned int HostVMDynamicLevels;
	unsigned int meta_row_bytes_per_row_ub_l[DML2_MAX_PLANES];
	unsigned int meta_row_bytes_per_row_ub_c[DML2_MAX_PLANES];
};

struct dml2_core_calcs_CalculateVMRowAndSwath_params {
	const struct dml2_display_cfg *display_cfg;
	unsigned int NumberOfActiveSurfaces;
	struct dml2_core_internal_DmlPipe *myPipe;
	unsigned int *SurfaceSizeInMALL;
	unsigned int PTEBufferSizeInRequestsLuma;
	unsigned int PTEBufferSizeInRequestsChroma;
	unsigned int MALLAllocatedForDCN;
	unsigned int *SwathWidthY;
	unsigned int *SwathWidthC;
	unsigned int HostVMMinPageSize;
	unsigned int DCCMetaBufferSizeBytes;
	bool mrq_present;
	enum dml2_pstate_method pstate_switch_modes[DML2_MAX_PLANES];

	// Output
	bool *PTEBufferSizeNotExceeded;
	bool *DCCMetaBufferSizeNotExceeded;

	unsigned int *dpte_row_width_luma_ub;
	unsigned int *dpte_row_width_chroma_ub;
	unsigned int *dpte_row_height_luma;
	unsigned int *dpte_row_height_chroma;
	unsigned int *dpte_row_height_linear_luma; // VBA_DELTA
	unsigned int *dpte_row_height_linear_chroma; // VBA_DELTA

	unsigned int *vm_group_bytes;
	unsigned int *dpte_group_bytes;
	unsigned int *PixelPTEReqWidthY;
	unsigned int *PixelPTEReqHeightY;
	unsigned int *PTERequestSizeY;
	unsigned int *vmpg_width_y;
	unsigned int *vmpg_height_y;

	unsigned int *PixelPTEReqWidthC;
	unsigned int *PixelPTEReqHeightC;
	unsigned int *PTERequestSizeC;
	unsigned int *vmpg_width_c;
	unsigned int *vmpg_height_c;

	unsigned int *dpde0_bytes_per_frame_ub_l;
	unsigned int *dpde0_bytes_per_frame_ub_c;

	unsigned int *PrefetchSourceLinesY;
	unsigned int *PrefetchSourceLinesC;
	unsigned int *VInitPreFillY;
	unsigned int *VInitPreFillC;
	unsigned int *MaxNumSwathY;
	unsigned int *MaxNumSwathC;
	double *dpte_row_bw;
	unsigned int *PixelPTEBytesPerRow;
	unsigned int *dpte_row_bytes_per_row_l;
	unsigned int *dpte_row_bytes_per_row_c;
	unsigned int *vm_bytes;
	bool *use_one_row_for_frame;
	bool *use_one_row_for_frame_flip;
	bool *is_using_mall_for_ss;
	bool *PTE_BUFFER_MODE;
	unsigned int *BIGK_FRAGMENT_SIZE;

	// MRQ
	unsigned int *meta_req_width_luma;
	unsigned int *meta_req_height_luma;
	unsigned int *meta_row_width_luma;
	unsigned int *meta_row_height_luma;
	unsigned int *meta_pte_bytes_per_frame_ub_l;

	unsigned int *meta_req_width_chroma;
	unsigned int *meta_req_height_chroma;
	unsigned int *meta_row_width_chroma;
	unsigned int *meta_row_height_chroma;
	unsigned int *meta_pte_bytes_per_frame_ub_c;
	double *meta_row_bw;
	unsigned int *meta_row_bytes;
	unsigned int *meta_row_bytes_per_row_ub_l;
	unsigned int *meta_row_bytes_per_row_ub_c;
};

struct dml2_core_calcs_CalculatePrefetchSchedule_locals {
	bool NoTimeToPrefetch;
	unsigned int DPPCycles;
	unsigned int DISPCLKCycles;
	double DSTTotalPixelsAfterScaler;
	double LineTime;
	double dst_y_prefetch_equ;
	double prefetch_bw_oto;
	double per_pipe_vactive_sw_bw;
	double Tvm_oto;
	double Tr0_oto;
	double Tvm_oto_lines;
	double Tr0_oto_lines;
	double dst_y_prefetch_oto;
	double TimeForFetchingVM;
	double TimeForFetchingRowInVBlank;
	double LinesToRequestPrefetchPixelData;
	unsigned int HostVMDynamicLevelsTrips;
	double trip_to_mem;
	double Tvm_trips_rounded;
	double Tr0_trips_rounded;
	double max_Tsw;
	double Lsw_oto;
	double prefetch_bw_equ;
	double Tvm_equ;
	double Tr0_equ;
	double Tdmbf;
	double Tdmec;
	double Tdmsks;
	double total_row_bytes;
	double prefetch_bw_pr;
	double bytes_pp;
	double dep_bytes;
	double min_Lsw_oto;
	double min_Lsw_equ;
	double Tsw_est1;
	double Tsw_est2;
	double Tsw_est3;
	double prefetch_bw1;
	double prefetch_bw2;
	double prefetch_bw3;
	double prefetch_bw4;
	double dst_y_prefetch_equ_impacted;

	double TWait_p;
	unsigned int cursor_prefetch_bytes;
};

struct dml2_core_shared_calculate_det_buffer_size_params {
	const struct dml2_display_cfg *display_cfg;
	bool ForceSingleDPP;
	unsigned int NumberOfActiveSurfaces;
	bool UnboundedRequestEnabled;
	unsigned int nomDETInKByte;
	unsigned int MaxTotalDETInKByte;
	unsigned int ConfigReturnBufferSizeInKByte;
	unsigned int MinCompressedBufferSizeInKByte;
	unsigned int ConfigReturnBufferSegmentSizeInkByte;
	unsigned int CompressedBufferSegmentSizeInkByte;
	double *ReadBandwidthLuma;
	double *ReadBandwidthChroma;
	unsigned int *full_swath_bytes_l;
	unsigned int *full_swath_bytes_c;
	unsigned int *swath_time_value_us;
	unsigned int *DPPPerSurface;
	bool TryToAllocateForWriteLatency;
	unsigned int bestEffortMinActiveLatencyHidingUs;

	// Output
	unsigned int *DETBufferSizeInKByte;
	unsigned int *CompressedBufferSizeInkByte;
};

struct dml2_core_shared_calculate_vm_and_row_bytes_params {
	bool ViewportStationary;
	bool DCCEnable;
	unsigned int NumberOfDPPs;
	unsigned int BlockHeight256Bytes;
	unsigned int BlockWidth256Bytes;
	enum dml2_source_format_class SourcePixelFormat;
	unsigned int SurfaceTiling;
	unsigned int BytePerPixel;
	enum dml2_rotation_angle RotationAngle;
	unsigned int SwathWidth; // per pipe
	unsigned int ViewportHeight;
	unsigned int ViewportXStart;
	unsigned int ViewportYStart;
	bool GPUVMEnable;
	unsigned int GPUVMMaxPageTableLevels;
	unsigned int GPUVMMinPageSizeKBytes;
	unsigned int PTEBufferSizeInRequests;
	unsigned int Pitch;
	unsigned int MacroTileWidth;
	unsigned int MacroTileHeight;
	bool is_phantom;
	unsigned int DCCMetaPitch;
	bool mrq_present;

	// Output
	unsigned int *PixelPTEBytesPerRow; // for bandwidth calculation
	unsigned int *PixelPTEBytesPerRowStorage; // for PTE buffer size check
	unsigned int *dpte_row_width_ub;
	unsigned int *dpte_row_height;
	unsigned int *dpte_row_height_linear;
	unsigned int *PixelPTEBytesPerRow_one_row_per_frame;
	unsigned int *dpte_row_width_ub_one_row_per_frame;
	unsigned int *dpte_row_height_one_row_per_frame;
	unsigned int *vmpg_width;
	unsigned int *vmpg_height;
	unsigned int *PixelPTEReqWidth;
	unsigned int *PixelPTEReqHeight;
	unsigned int *PTERequestSize;
	unsigned int *dpde0_bytes_per_frame_ub;

	unsigned int *meta_row_bytes;
	unsigned int *MetaRequestWidth;
	unsigned int *MetaRequestHeight;
	unsigned int *meta_row_width;
	unsigned int *meta_row_height;
	unsigned int *meta_pte_bytes_per_frame_ub;
};

struct dml2_core_shared_CalculateSwathAndDETConfiguration_locals {
	unsigned int MaximumSwathHeightY[DML2_MAX_PLANES];
	unsigned int MaximumSwathHeightC[DML2_MAX_PLANES];
	unsigned int RoundedUpSwathSizeBytesY[DML2_MAX_PLANES];
	unsigned int RoundedUpSwathSizeBytesC[DML2_MAX_PLANES];
	unsigned int SwathWidthSingleDPP[DML2_MAX_PLANES];
	unsigned int SwathWidthSingleDPPChroma[DML2_MAX_PLANES];
	unsigned int SwathTimeValueUs[DML2_MAX_PLANES];

	struct dml2_core_shared_calculate_det_buffer_size_params calculate_det_buffer_size_params;
};

struct dml2_core_shared_TruncToValidBPP_locals {
};

struct dml2_core_shared_CalculateDETBufferSize_locals {
	unsigned int DETBufferSizePoolInKByte;
	unsigned int NextDETBufferPieceInKByte;
	unsigned int NextSurfaceToAssignDETPiece;
	double TotalBandwidth;
	double BandwidthOfSurfacesNotAssignedDETPiece;
	unsigned int max_minDET;
	unsigned int minDET;
	unsigned int minDET_pipe;
	unsigned int TotalBandwidthPerStream[DML2_MAX_PLANES];
	unsigned int TotalPixelRate;
	unsigned int DETBudgetPerStream[DML2_MAX_PLANES];
	unsigned int RemainingDETBudgetPerStream[DML2_MAX_PLANES];
	unsigned int IdealDETBudget, DeltaDETBudget;
	unsigned int ResidualDETAfterRounding;
};

struct dml2_core_shared_get_urgent_bandwidth_required_locals {
	double required_bandwidth_mbps;
	double required_bandwidth_mbps_this_surface;
	double adj_factor_p0;
	double adj_factor_p1;
	double adj_factor_cur;
	double adj_factor_p0_pre;
	double adj_factor_p1_pre;
	double adj_factor_cur_pre;
	double per_plane_flip_bw[DML2_MAX_PLANES];
	double mall_svp_prefetch_factor;
	double tmp_nom_adj_factor_p0;
	double tmp_nom_adj_factor_p1;
	double tmp_pref_adj_factor_p0;
	double tmp_pref_adj_factor_p1;
	double vm_row_bw;
	double flip_and_active_bw;
	double flip_and_prefetch_bw;
	double flip_and_prefetch_bw_max;
	double active_and_excess_bw;
};

struct dml2_core_shared_calculate_peak_bandwidth_required_locals {
	double unity_array[DML2_MAX_PLANES];
	double zero_array[DML2_MAX_PLANES];
	double surface_dummy_bw[DML2_MAX_PLANES];
};

struct dml2_core_shared_CalculateFlipSchedule_locals {
	double min_row_time;
	double Tvm_flip;
	double Tr0_flip;
	double ImmediateFlipBW;
	double dpte_row_bytes;
	double min_row_height;
	double min_row_height_chroma;
	double max_flip_time;
	double lb_flip_bw;
	double hvm_scaled_vm_bytes;
	double num_rows;
	double hvm_scaled_row_bytes;
	double hvm_scaled_vm_row_bytes;
	bool dual_plane;
};

struct dml2_core_shared_rq_dlg_get_dlg_reg_locals {
	unsigned int plane_idx;
	unsigned int stream_idx;
	enum dml2_source_format_class source_format;
	const struct dml2_timing_cfg *timing;
	bool dual_plane;
	enum dml2_odm_mode odm_mode;

	unsigned int htotal;
	unsigned int hactive;
	unsigned int hblank_end;
	unsigned int vblank_end;
	bool interlaced;
	double pclk_freq_in_mhz;
	double refclk_freq_in_mhz;
	double ref_freq_to_pix_freq;

	unsigned int num_active_pipes;
	unsigned int first_pipe_idx_in_plane;
	unsigned int pipe_idx_in_combine;
	unsigned int odm_combine_factor;

	double min_ttu_vblank;
	unsigned int min_dst_y_next_start;

	unsigned int vready_after_vcount0;

	unsigned int dst_x_after_scaler;
	unsigned int dst_y_after_scaler;

	double dst_y_prefetch;
	double dst_y_per_vm_vblank;
	double dst_y_per_row_vblank;
	double dst_y_per_vm_flip;
	double dst_y_per_row_flip;

	double max_dst_y_per_vm_vblank;
	double max_dst_y_per_row_vblank;

	double vratio_pre_l;
	double vratio_pre_c;

	double refcyc_per_line_delivery_pre_l;
	double refcyc_per_line_delivery_l;

	double refcyc_per_line_delivery_pre_c;
	double refcyc_per_line_delivery_c;

	double refcyc_per_req_delivery_pre_l;
	double refcyc_per_req_delivery_l;

	double refcyc_per_req_delivery_pre_c;
	double refcyc_per_req_delivery_c;

	double dst_y_per_pte_row_nom_l;
	double dst_y_per_pte_row_nom_c;
	double refcyc_per_pte_group_nom_l;
	double refcyc_per_pte_group_nom_c;
	double refcyc_per_pte_group_vblank_l;
	double refcyc_per_pte_group_vblank_c;
	double refcyc_per_pte_group_flip_l;
	double refcyc_per_pte_group_flip_c;
	double refcyc_per_tdlut_group;

	double dst_y_per_meta_row_nom_l;
	double dst_y_per_meta_row_nom_c;
	double refcyc_per_meta_chunk_nom_l;
	double refcyc_per_meta_chunk_nom_c;
	double refcyc_per_meta_chunk_vblank_l;
	double refcyc_per_meta_chunk_vblank_c;
	double refcyc_per_meta_chunk_flip_l;
	double refcyc_per_meta_chunk_flip_c;
};

struct dml2_core_shared_CalculateMetaAndPTETimes_params {
	struct dml2_core_internal_scratch *scratch;
	const struct dml2_display_cfg *display_cfg;
	unsigned int NumberOfActiveSurfaces;
	bool *use_one_row_for_frame;
	double *dst_y_per_row_vblank;
	double *dst_y_per_row_flip;
	unsigned int *BytePerPixelY;
	unsigned int *BytePerPixelC;
	unsigned int *dpte_row_height;
	unsigned int *dpte_row_height_chroma;
	unsigned int *dpte_group_bytes;
	unsigned int *PTERequestSizeY;
	unsigned int *PTERequestSizeC;
	unsigned int *PixelPTEReqWidthY;
	unsigned int *PixelPTEReqHeightY;
	unsigned int *PixelPTEReqWidthC;
	unsigned int *PixelPTEReqHeightC;
	unsigned int *dpte_row_width_luma_ub;
	unsigned int *dpte_row_width_chroma_ub;
	unsigned int *tdlut_groups_per_2row_ub;
	bool mrq_present;
	unsigned int MetaChunkSize;
	unsigned int MinMetaChunkSizeBytes;
	unsigned int *meta_row_width;
	unsigned int *meta_row_width_chroma;
	unsigned int *meta_row_height;
	unsigned int *meta_row_height_chroma;
	unsigned int *meta_req_width;
	unsigned int *meta_req_width_chroma;
	unsigned int *meta_req_height;
	unsigned int *meta_req_height_chroma;

	// Output
	double *time_per_tdlut_group;
	double *DST_Y_PER_PTE_ROW_NOM_L;
	double *DST_Y_PER_PTE_ROW_NOM_C;
	double *time_per_pte_group_nom_luma;
	double *time_per_pte_group_vblank_luma;
	double *time_per_pte_group_flip_luma;
	double *time_per_pte_group_nom_chroma;
	double *time_per_pte_group_vblank_chroma;
	double *time_per_pte_group_flip_chroma;

	double *DST_Y_PER_META_ROW_NOM_L;
	double *DST_Y_PER_META_ROW_NOM_C;

	double *TimePerMetaChunkNominal;
	double *TimePerChromaMetaChunkNominal;
	double *TimePerMetaChunkVBlank;
	double *TimePerChromaMetaChunkVBlank;
	double *TimePerMetaChunkFlip;
	double *TimePerChromaMetaChunkFlip;
};

struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params {
	const struct dml2_display_cfg *display_cfg;
	bool USRRetrainingRequired;
	unsigned int NumberOfActiveSurfaces;
	unsigned int MaxLineBufferLines;
	unsigned int LineBufferSize;
	unsigned int WritebackInterfaceBufferSize;
	double DCFCLK;
	double ReturnBW;
	bool SynchronizeTimings;
	bool SynchronizeDRRDisplaysForUCLKPStateChange;
	unsigned int *dpte_group_bytes;
	struct dml2_core_internal_SOCParametersList mmSOCParameters;
	unsigned int WritebackChunkSize;
	double SOCCLK;
	double DCFClkDeepSleep;
	unsigned int *DETBufferSizeY;
	unsigned int *DETBufferSizeC;
	unsigned int *SwathHeightY;
	unsigned int *SwathHeightC;
	unsigned int *SwathWidthY;
	unsigned int *SwathWidthC;
	unsigned int *DPPPerSurface;
	double *BytePerPixelDETY;
	double *BytePerPixelDETC;
	unsigned int *DSTXAfterScaler;
	unsigned int *DSTYAfterScaler;
	bool UnboundedRequestEnabled;
	unsigned int CompressedBufferSizeInkByte;
	bool max_outstanding_when_urgent_expected;
	unsigned int max_outstanding_requests;
	unsigned int max_request_size_bytes;
	unsigned int *meta_row_height_l;
	unsigned int *meta_row_height_c;

	// Output
	struct dml2_core_internal_watermarks *Watermark;
	enum dml2_pstate_change_support *DRAMClockChangeSupport;
	bool *global_dram_clock_change_supported;
	double *MaxActiveDRAMClockChangeLatencySupported;
	unsigned int *SubViewportLinesNeededInMALL;
	enum dml2_pstate_change_support *FCLKChangeSupport;
	bool *global_fclk_change_supported;
	double *MaxActiveFCLKChangeLatencySupported;
	bool *USRRetrainingSupport;
	double *VActiveLatencyHidingMargin;
	double *VActiveLatencyHidingUs;
	bool *g6_temp_read_support;
	bool *temp_read_or_ppt_support;
};


struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params {
	const struct dml2_display_cfg *display_cfg;
	unsigned int ConfigReturnBufferSizeInKByte;
	unsigned int MaxTotalDETInKByte;
	unsigned int MinCompressedBufferSizeInKByte;
	unsigned int rob_buffer_size_kbytes;
	unsigned int pixel_chunk_size_kbytes;
	bool ForceSingleDPP;
	unsigned int NumberOfActiveSurfaces;
	unsigned int nomDETInKByte;
	unsigned int ConfigReturnBufferSegmentSizeInkByte;
	unsigned int CompressedBufferSegmentSizeInkByte;
	double *ReadBandwidthLuma;
	double *ReadBandwidthChroma;
	double *MaximumSwathWidthLuma;
	double *MaximumSwathWidthChroma;
	unsigned int *Read256BytesBlockHeightY;
	unsigned int *Read256BytesBlockHeightC;
	unsigned int *Read256BytesBlockWidthY;
	unsigned int *Read256BytesBlockWidthC;
	bool *surf_linear128_l;
	bool *surf_linear128_c;
	enum dml2_odm_mode *ODMMode;
	unsigned int *BytePerPixY;
	unsigned int *BytePerPixC;
	double *BytePerPixDETY;
	double *BytePerPixDETC;
	unsigned int *DPPPerSurface;
	bool mrq_present;
	unsigned int dummy[2][DML2_MAX_PLANES];
	unsigned int swath_width_luma_ub_single_dpp[DML2_MAX_PLANES];
	unsigned int swath_width_chroma_ub_single_dpp[DML2_MAX_PLANES];

	// output
	unsigned int *req_per_swath_ub_l;
	unsigned int *req_per_swath_ub_c;
	unsigned int *swath_width_luma_ub;
	unsigned int *swath_width_chroma_ub;
	unsigned int *SwathWidth;
	unsigned int *SwathWidthChroma;
	unsigned int *SwathHeightY;
	unsigned int *SwathHeightC;
	unsigned int *request_size_bytes_luma;
	unsigned int *request_size_bytes_chroma;
	unsigned int *DETBufferSizeInKByte;
	unsigned int *DETBufferSizeY;
	unsigned int *DETBufferSizeC;
	unsigned int *full_swath_bytes_l;
	unsigned int *full_swath_bytes_c;
	unsigned int *full_swath_bytes_single_dpp_l;
	unsigned int *full_swath_bytes_single_dpp_c;
	bool *UnboundedRequestEnabled;
	unsigned int *compbuf_reserved_space_64b;
	unsigned int *CompressedBufferSizeInkByte;
	bool *ViewportSizeSupportPerSurface;
	bool *ViewportSizeSupport;
	bool *hw_debug5;

	struct dml2_core_shared_calculation_funcs *funcs;
};

struct dml2_core_calcs_CalculateStutterEfficiency_locals {
	double DETBufferingTimeY;
	double SwathWidthYCriticalSurface;
	double SwathHeightYCriticalSurface;
	double VActiveTimeCriticalSurface;
	double FrameTimeCriticalSurface;
	unsigned int BytePerPixelYCriticalSurface;
	unsigned int DETBufferSizeYCriticalSurface;
	double MinTTUVBlankCriticalSurface;
	unsigned int BlockWidth256BytesYCriticalSurface;
	bool SinglePlaneCriticalSurface;
	bool SinglePipeCriticalSurface;
	double TotalCompressedReadBandwidth;
	double TotalRowReadBandwidth;
	double AverageDCCCompressionRate;
	double EffectiveCompressedBufferSize;
	double PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer;
	double StutterBurstTime;
	unsigned int TotalActiveWriteback;
	double LinesInDETY;
	double LinesInDETYRoundedDownToSwath;
	double MaximumEffectiveCompressionLuma;
	double MaximumEffectiveCompressionChroma;
	double TotalZeroSizeRequestReadBandwidth;
	double TotalZeroSizeCompressedReadBandwidth;
	double AverageDCCZeroSizeFraction;
	double AverageZeroSizeCompressionRate;
	bool stream_visited[DML2_MAX_PLANES];
};

struct dml2_core_calcs_CalculateStutterEfficiency_params {
	const struct dml2_display_cfg *display_cfg;
	unsigned int CompressedBufferSizeInkByte;
	bool UnboundedRequestEnabled;
	unsigned int MetaFIFOSizeInKEntries;
	unsigned int ZeroSizeBufferEntries;
	unsigned int PixelChunkSizeInKByte;
	unsigned int NumberOfActiveSurfaces;
	unsigned int ROBBufferSizeInKByte;
	double TotalDataReadBandwidth;
	double DCFCLK;
	double ReturnBW;
	unsigned int CompbufReservedSpace64B;
	unsigned int CompbufReservedSpaceZs;
	bool hw_debug5;
	double SRExitTime;
	double SRExitTimeLowPower;
	double SRExitZ8Time;
	bool SynchronizeTimings;
	double StutterEnterPlusExitWatermark;
	double LowPowerStutterEnterPlusExitWatermark;
	double Z8StutterEnterPlusExitWatermark;
	bool ProgressiveToInterlaceUnitInOPP;
	double *MinTTUVBlank;
	unsigned int *DPPPerSurface;
	unsigned int *DETBufferSizeY;
	unsigned int *BytePerPixelY;
	double *BytePerPixelDETY;
	unsigned int *SwathWidthY;
	unsigned int *SwathHeightY;
	unsigned int *SwathHeightC;
	unsigned int *BlockHeight256BytesY;
	unsigned int *BlockWidth256BytesY;
	unsigned int *BlockHeight256BytesC;
	unsigned int *BlockWidth256BytesC;
	unsigned int *DCCYMaxUncompressedBlock;
	unsigned int *DCCCMaxUncompressedBlock;
	double *ReadBandwidthSurfaceLuma;
	double *ReadBandwidthSurfaceChroma;
	double *meta_row_bw;
	double *dpte_row_bw;
	bool rob_alloc_compressed;

	// output
	double *StutterEfficiencyNotIncludingVBlank;
	double *StutterEfficiency;
	double *LowPowerStutterEfficiencyNotIncludingVBlank;
	double *LowPowerStutterEfficiency;
	unsigned int *NumberOfStutterBurstsPerFrame;
	unsigned int *LowPowerNumberOfStutterBurstsPerFrame;
	double *Z8StutterEfficiencyNotIncludingVBlank;
	double *Z8StutterEfficiency;
	unsigned int *Z8NumberOfStutterBurstsPerFrame;
	double *StutterPeriod;
	bool *DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE;
};

struct dml2_core_calcs_CalculatePrefetchSchedule_params {
	const struct dml2_display_cfg *display_cfg;
	double HostVMInefficiencyFactor;
	struct dml2_core_internal_DmlPipe *myPipe;
	unsigned int DSCDelay;
	double DPPCLKDelaySubtotalPlusCNVCFormater;
	double DPPCLKDelaySCL;
	double DPPCLKDelaySCLLBOnly;
	double DPPCLKDelayCNVCCursor;
	double DISPCLKDelaySubtotal;
	unsigned int DPP_RECOUT_WIDTH;
	enum dml2_output_format_class OutputFormat;
	unsigned int MaxInterDCNTileRepeaters;
	unsigned int VStartup;
	unsigned int HostVMMinPageSize;
	bool DynamicMetadataEnable;
	bool DynamicMetadataVMEnabled;
	unsigned int DynamicMetadataLinesBeforeActiveRequired;
	unsigned int DynamicMetadataTransmittedBytes;
	double UrgentLatency;
	double ExtraLatencyPrefetch;
	double TCalc;
	unsigned int vm_bytes;
	unsigned int PixelPTEBytesPerRow;
	double PrefetchSourceLinesY;
	unsigned int VInitPreFillY;
	unsigned int MaxNumSwathY;
	double PrefetchSourceLinesC;
	unsigned int VInitPreFillC;
	unsigned int MaxNumSwathC;
	unsigned int swath_width_luma_ub;  // per-pipe
	unsigned int swath_width_chroma_ub; // per-pipe
	unsigned int SwathHeightY;
	unsigned int SwathHeightC;
	double TWait;
	double Ttrip;
	double Turg;
	bool setup_for_tdlut;
	unsigned int tdlut_pte_bytes_per_frame;
	unsigned int tdlut_bytes_per_frame;
	double tdlut_opt_time;
	double tdlut_drain_time;

	unsigned int num_cursors;
	unsigned int cursor_bytes_per_chunk;
	unsigned int cursor_bytes_per_line;

	// MRQ
	bool dcc_enable;
	bool mrq_present;
	unsigned int meta_row_bytes;
	double mall_prefetch_sdp_overhead_factor;

	double impacted_dst_y_pre;
	double vactive_sw_bw_l; // per surface bw
	double vactive_sw_bw_c; // per surface bw

	// output
	unsigned int *DSTXAfterScaler;
	unsigned int *DSTYAfterScaler;
	double *dst_y_prefetch;
	double *dst_y_per_vm_vblank;
	double *dst_y_per_row_vblank;
	double *VRatioPrefetchY;
	double *VRatioPrefetchC;
	double *RequiredPrefetchPixelDataBWLuma;
	double *RequiredPrefetchPixelDataBWChroma;
	double *RequiredPrefetchBWMax;
	bool *NotEnoughTimeForDynamicMetadata;
	double *Tno_bw;
	double *Tno_bw_flip;
	double *prefetch_vmrow_bw;
	double *Tdmdl_vm;
	double *Tdmdl;
	double *TSetup;
	double *Tpre_rounded;
	double *Tpre_oto;
	double *Tvm_trips;
	double *Tr0_trips;
	double *Tvm_trips_flip;
	double *Tr0_trips_flip;
	double *Tvm_trips_flip_rounded;
	double *Tr0_trips_flip_rounded;
	unsigned int *VUpdateOffsetPix;
	unsigned int *VUpdateWidthPix;
	unsigned int *VReadyOffsetPix;
	double *prefetch_cursor_bw;
	double *prefetch_sw_bytes;
	double *prefetch_swath_time_us;
};

struct dml2_core_calcs_CheckGlobalPrefetchAdmissibility_params {
	unsigned int num_active_planes;
	enum dml2_source_format_class *pixel_format;
	unsigned int rob_buffer_size_kbytes;
	unsigned int compressed_buffer_size_kbytes;
	unsigned int chunk_bytes_l; // same for all planes
	unsigned int chunk_bytes_c;
	unsigned int *detile_buffer_size_bytes_l;
	unsigned int *detile_buffer_size_bytes_c;
	unsigned int *full_swath_bytes_l;
	unsigned int *full_swath_bytes_c;
	unsigned int *lb_source_lines_l;
	unsigned int *lb_source_lines_c;
	unsigned int *swath_height_l;
	unsigned int *swath_height_c;
	double *prefetch_sw_bytes;
	double *Tpre_rounded;
	double *Tpre_oto;
	double estimated_dcfclk_mhz;
	double estimated_urg_bandwidth_required_mbps;
	double *line_time;
	double *dst_y_prefetch;

	// output
	bool *recalc_prefetch_schedule;
	double *impacted_dst_y_pre;
};

struct dml2_core_calcs_CheckGlobalPrefetchAdmissibility_locals {
	unsigned int max_Trpd_dcfclk_cycles;
	unsigned int burst_bytes_to_fill_det;
	double time_to_fill_det_us;
	unsigned int accumulated_return_path_dcfclk_cycles[DML2_MAX_PLANES];
	bool prefetch_global_check_passed;
	unsigned int src_swath_bytes_l[DML2_MAX_PLANES];
	unsigned int src_swath_bytes_c[DML2_MAX_PLANES];
	unsigned int src_detile_buf_size_bytes_l[DML2_MAX_PLANES];
	unsigned int src_detile_buf_size_bytes_c[DML2_MAX_PLANES];
};

struct dml2_core_calcs_calculate_mcache_row_bytes_params {
	unsigned int num_chans;
	unsigned int mem_word_bytes;
	unsigned int mcache_size_bytes;
	unsigned int mcache_line_size_bytes;
	unsigned int gpuvm_enable;
	unsigned int gpuvm_page_size_kbytes;

	//enum dml_rotation_angle rotation_angle;
	bool surf_vert;
	unsigned int vp_stationary;
	unsigned int tiling_mode;
	bool imall_enable;

	unsigned int vp_start_x;
	unsigned int vp_start_y;
	unsigned int full_vp_width;
	unsigned int full_vp_height;
	unsigned int blk_width;
	unsigned int blk_height;
	unsigned int vmpg_width;
	unsigned int vmpg_height;
	unsigned int full_swath_bytes;
	unsigned int bytes_per_pixel;

	// output
	unsigned int *num_mcaches;
	unsigned int *mcache_row_bytes;
	unsigned int *mcache_row_bytes_per_channel;
	unsigned int *meta_row_width_ub;
	double *dcc_dram_bw_nom_overhead_factor;
	double *dcc_dram_bw_pref_overhead_factor;
	unsigned int *mvmpg_width;
	unsigned int *mvmpg_height;
	unsigned int *full_vp_access_width_mvmpg_aligned;
	unsigned int *mvmpg_per_mcache_lb;
};

struct dml2_core_shared_calculate_mcache_setting_locals {
	struct dml2_core_calcs_calculate_mcache_row_bytes_params l_p;
	struct dml2_core_calcs_calculate_mcache_row_bytes_params c_p;

	bool is_dual_plane;
	unsigned int mvmpg_width_l;
	unsigned int mvmpg_height_l;
	unsigned int full_vp_access_width_mvmpg_aligned_l;
	unsigned int mvmpg_per_mcache_lb_l;
	unsigned int meta_row_width_l;

	unsigned int mvmpg_width_c;
	unsigned int mvmpg_height_c;
	unsigned int full_vp_access_width_mvmpg_aligned_c;
	unsigned int mvmpg_per_mcache_lb_c;
	unsigned int meta_row_width_c;

	unsigned int lc_comb_last_mcache_size;
	double luma_time_factor;
	double mcache_remainder_l;
	double mcache_remainder_c;
	unsigned int mvmpg_access_width_l;
	unsigned int mvmpg_access_width_c;
	unsigned int avg_mcache_element_size_l;
	unsigned int avg_mcache_element_size_c;

	unsigned int full_vp_access_width_l;
	unsigned int full_vp_access_width_c;
};

struct dml2_core_calcs_calculate_mcache_setting_params {
	bool dcc_enable;
	unsigned int num_chans;
	unsigned int mem_word_bytes;
	unsigned int mcache_size_bytes;
	unsigned int mcache_line_size_bytes;
	unsigned int gpuvm_enable;
	unsigned int gpuvm_page_size_kbytes;

	enum dml2_source_format_class source_format;
	bool surf_vert;
	unsigned int vp_stationary;
	unsigned int tiling_mode;
	bool imall_enable;

	unsigned int vp_start_x_l;
	unsigned int vp_start_y_l;
	unsigned int full_vp_width_l;
	unsigned int full_vp_height_l;
	unsigned int blk_width_l;
	unsigned int blk_height_l;
	unsigned int vmpg_width_l;
	unsigned int vmpg_height_l;
	unsigned int full_swath_bytes_l;
	unsigned int bytes_per_pixel_l;

	unsigned int vp_start_x_c;
	unsigned int vp_start_y_c;
	unsigned int full_vp_width_c;
	unsigned int full_vp_height_c;
	unsigned int blk_width_c;
	unsigned int blk_height_c;
	unsigned int vmpg_width_c;
	unsigned int vmpg_height_c;
	unsigned int full_swath_bytes_c;
	unsigned int bytes_per_pixel_c;

	// output
	unsigned int *num_mcaches_l;
	unsigned int *mcache_row_bytes_l;
	unsigned int *mcache_row_bytes_per_channel_l;
	unsigned int *mcache_offsets_l;
	unsigned int *mcache_shift_granularity_l;
	double *dcc_dram_bw_nom_overhead_factor_l;
	double *dcc_dram_bw_pref_overhead_factor_l;

	unsigned int *num_mcaches_c;
	unsigned int *mcache_row_bytes_c;
	unsigned int *mcache_row_bytes_per_channel_c;
	unsigned int *mcache_offsets_c;
	unsigned int *mcache_shift_granularity_c;
	double *dcc_dram_bw_nom_overhead_factor_c;
	double *dcc_dram_bw_pref_overhead_factor_c;

	bool *mall_comb_mcache_l;
	bool *mall_comb_mcache_c;
	bool *lc_comb_mcache;
};

struct dml2_core_calcs_calculate_tdlut_setting_params {
	// input params
	double dispclk_mhz;
	bool setup_for_tdlut;
	enum dml2_tdlut_width_mode tdlut_width_mode;
	enum dml2_tdlut_addressing_mode tdlut_addressing_mode;
	unsigned int cursor_buffer_size;
	bool gpuvm_enable;
	unsigned int gpuvm_page_size_kbytes;
	bool is_gfx11;
	bool tdlut_mpc_width_flag;

	// output param
	unsigned int *tdlut_pte_bytes_per_frame;
	unsigned int *tdlut_bytes_per_frame;
	unsigned int *tdlut_groups_per_2row_ub;
	double *tdlut_opt_time;
	double *tdlut_drain_time;
	unsigned int *tdlut_bytes_to_deliver;
	unsigned int *tdlut_bytes_per_group;
};

struct dml2_core_calcs_calculate_peak_bandwidth_required_params {
	// output
	double (*urg_vactive_bandwidth_required)[dml2_core_internal_bw_max];
	double (*urg_bandwidth_required)[dml2_core_internal_bw_max];
	double (*urg_bandwidth_required_qual)[dml2_core_internal_bw_max];
	double (*non_urg_bandwidth_required)[dml2_core_internal_bw_max];
	double (*surface_avg_vactive_required_bw)[dml2_core_internal_bw_max][DML2_MAX_PLANES];
	double (*surface_peak_required_bw)[dml2_core_internal_bw_max][DML2_MAX_PLANES];

	// input
	const struct dml2_display_cfg *display_cfg;
	bool inc_flip_bw;
	unsigned int num_active_planes;
	unsigned int *num_of_dpp;
	double *dcc_dram_bw_nom_overhead_factor_p0;
	double *dcc_dram_bw_nom_overhead_factor_p1;
	double *dcc_dram_bw_pref_overhead_factor_p0;
	double *dcc_dram_bw_pref_overhead_factor_p1;
	double *mall_prefetch_sdp_overhead_factor;
	double *mall_prefetch_dram_overhead_factor;
	double *surface_read_bandwidth_l;
	double *surface_read_bandwidth_c;
	double *prefetch_bandwidth_l;
	double *prefetch_bandwidth_c;
	double *prefetch_bandwidth_max;
	double *excess_vactive_fill_bw_l;
	double *excess_vactive_fill_bw_c;
	double *cursor_bw;
	double *dpte_row_bw;
	double *meta_row_bw;
	double *prefetch_cursor_bw;
	double *prefetch_vmrow_bw;
	double *flip_bw;
	double *urgent_burst_factor_l;
	double *urgent_burst_factor_c;
	double *urgent_burst_factor_cursor;
	double *urgent_burst_factor_prefetch_l;
	double *urgent_burst_factor_prefetch_c;
	double *urgent_burst_factor_prefetch_cursor;
};

struct dml2_core_calcs_calculate_bytes_to_fetch_required_to_hide_latency_params {
	/* inputs */
	const struct dml2_display_cfg *display_cfg;
	bool mrq_present;
	unsigned int num_active_planes;
	unsigned int *num_of_dpp;
	unsigned int *meta_row_height_l;
	unsigned int *meta_row_height_c;
	unsigned int *meta_row_bytes_per_row_ub_l;
	unsigned int *meta_row_bytes_per_row_ub_c;
	unsigned int *dpte_row_height_l;
	unsigned int *dpte_row_height_c;
	unsigned int *dpte_bytes_per_row_l;
	unsigned int *dpte_bytes_per_row_c;
	unsigned int *byte_per_pix_l;
	unsigned int *byte_per_pix_c;
	unsigned int *swath_width_l;
	unsigned int *swath_width_c;
	unsigned int *swath_height_l;
	unsigned int *swath_height_c;
	double latency_to_hide_us;

	/* outputs */
	unsigned int *bytes_required_l;
	unsigned int *bytes_required_c;
};

// A list of overridable function pointers in the core
// shared calculation library.
struct dml2_core_shared_calculation_funcs {
	void (*calculate_det_buffer_size)(struct dml2_core_shared_calculate_det_buffer_size_params *p);
};

struct dml2_core_internal_scratch {
	// Scratch space for function locals
	struct dml2_core_calcs_mode_support_locals dml_core_mode_support_locals;
	struct dml2_core_calcs_mode_programming_locals dml_core_mode_programming_locals;
	struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals;
	struct dml2_core_calcs_CalculateVMRowAndSwath_locals CalculateVMRowAndSwath_locals;
	struct dml2_core_calcs_CalculatePrefetchSchedule_locals CalculatePrefetchSchedule_locals;
	struct dml2_core_calcs_CheckGlobalPrefetchAdmissibility_locals CheckGlobalPrefetchAdmissibility_locals;
	struct dml2_core_shared_CalculateSwathAndDETConfiguration_locals CalculateSwathAndDETConfiguration_locals;
	struct dml2_core_shared_TruncToValidBPP_locals TruncToValidBPP_locals;
	struct dml2_core_shared_CalculateDETBufferSize_locals CalculateDETBufferSize_locals;
	struct dml2_core_shared_get_urgent_bandwidth_required_locals get_urgent_bandwidth_required_locals;
	struct dml2_core_shared_calculate_peak_bandwidth_required_locals calculate_peak_bandwidth_required_locals;
	struct dml2_core_shared_CalculateFlipSchedule_locals CalculateFlipSchedule_locals;
	struct dml2_core_shared_rq_dlg_get_dlg_reg_locals rq_dlg_get_dlg_reg_locals;
	struct dml2_core_calcs_CalculateStutterEfficiency_locals CalculateStutterEfficiency_locals;

	// Scratch space for function params
	struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params;
	struct dml2_core_calcs_CalculateVMRowAndSwath_params CalculateVMRowAndSwath_params;
	struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params CalculateSwathAndDETConfiguration_params;
	struct dml2_core_calcs_CalculateStutterEfficiency_params CalculateStutterEfficiency_params;
	struct dml2_core_calcs_CalculatePrefetchSchedule_params CalculatePrefetchSchedule_params;
	struct dml2_core_calcs_CheckGlobalPrefetchAdmissibility_params CheckGlobalPrefetchAdmissibility_params;
	struct dml2_core_calcs_calculate_mcache_setting_params calculate_mcache_setting_params;
	struct dml2_core_calcs_calculate_tdlut_setting_params calculate_tdlut_setting_params;
	struct dml2_core_shared_calculate_vm_and_row_bytes_params calculate_vm_and_row_bytes_params;
	struct dml2_core_shared_calculate_mcache_setting_locals calculate_mcache_setting_locals;
	struct dml2_core_shared_CalculateMetaAndPTETimes_params CalculateMetaAndPTETimes_params;
	struct dml2_core_calcs_calculate_peak_bandwidth_required_params calculate_peak_bandwidth_params;
	struct dml2_core_calcs_calculate_bytes_to_fetch_required_to_hide_latency_params calculate_bytes_to_fetch_required_to_hide_latency_params;
};

//struct dml2_svp_mode_override;
struct dml2_core_internal_display_mode_lib {
	struct dml2_core_ip_params ip;
	struct dml2_soc_bb soc;
	struct dml2_ip_capabilities ip_caps;

	//@brief Mode Support and Mode programming struct
	// Used to hold input; intermediate and output of the calculations
	struct dml2_core_internal_mode_support ms; // struct for mode support
	struct dml2_core_internal_mode_program mp; // struct for mode programming
	// Available overridable calculators for core_shared.
	// if null, core_shared will use default calculators.
	struct dml2_core_shared_calculation_funcs funcs;

	struct dml2_core_internal_scratch scratch;
};

struct dml2_core_calcs_mode_support_ex {
	struct dml2_core_internal_display_mode_lib *mode_lib;
	const struct dml2_display_cfg *in_display_cfg;
	const struct dml2_mcg_min_clock_table *min_clk_table;
	int min_clk_index;
	//unsigned int in_state_index;
	struct dml2_core_internal_mode_support_info *out_evaluation_info;
};

struct core_display_cfg_support_info;

struct dml2_core_calcs_mode_programming_ex {
	struct dml2_core_internal_display_mode_lib *mode_lib;
	const struct dml2_display_cfg *in_display_cfg;
	const struct dml2_mcg_min_clock_table *min_clk_table;
	const struct core_display_cfg_support_info *cfg_support_info;
	int min_clk_index;
	struct dml2_display_cfg_programming *programming;
};

#endif
