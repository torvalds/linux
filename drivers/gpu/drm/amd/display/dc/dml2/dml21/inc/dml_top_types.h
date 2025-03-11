// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML_TOP_TYPES_H__
#define __DML_TOP_TYPES_H__

#include "dml_top_display_cfg_types.h"
#include "dml_top_soc_parameter_types.h"
#include "dml_top_policy_types.h"
#include "dml_top_dchub_registers.h"

#include "dmub_cmd.h"

struct dml2_instance;

enum dml2_status {
	dml2_success = 0,
	dml2_error_generic = 1
};

enum dml2_project_id {
	dml2_project_invalid = 0,
	dml2_project_dcn4x_stage1 = 1,
	dml2_project_dcn4x_stage2 = 2,
	dml2_project_dcn4x_stage2_auto_drr_svp = 3,
};

enum dml2_pstate_change_support {
	dml2_pstate_change_vactive = 0,
	dml2_pstate_change_vblank = 1,
	dml2_pstate_change_vblank_and_vactive = 2,
	dml2_pstate_change_drr = 3,
	dml2_pstate_change_mall_svp = 4,
	dml2_pstate_change_mall_full_frame = 6,
	dml2_pstate_change_unsupported = 7
};

enum dml2_output_type_and_rate__type {
	dml2_output_type_unknown = 0,
	dml2_output_type_dp = 1,
	dml2_output_type_edp = 2,
	dml2_output_type_dp2p0 = 3,
	dml2_output_type_hdmi = 4,
	dml2_output_type_hdmifrl = 5
};

enum dml2_output_type_and_rate__rate {
	dml2_output_rate_unknown = 0,
	dml2_output_rate_dp_rate_hbr = 1,
	dml2_output_rate_dp_rate_hbr2 = 2,
	dml2_output_rate_dp_rate_hbr3 = 3,
	dml2_output_rate_dp_rate_uhbr10 = 4,
	dml2_output_rate_dp_rate_uhbr13p5 = 5,
	dml2_output_rate_dp_rate_uhbr20 = 6,
	dml2_output_rate_hdmi_rate_3x3 = 7,
	dml2_output_rate_hdmi_rate_6x3 = 8,
	dml2_output_rate_hdmi_rate_6x4 = 9,
	dml2_output_rate_hdmi_rate_8x4 = 10,
	dml2_output_rate_hdmi_rate_10x4 = 11,
	dml2_output_rate_hdmi_rate_12x4 = 12
};

struct dml2_pmo_options {
	bool disable_vblank;
	bool disable_svp;
	bool disable_drr_var;
	bool disable_drr_clamped;
	bool disable_drr_var_when_var_active;
	bool disable_drr_clamped_when_var_active;
	bool disable_fams2;
	bool disable_vactive_det_fill_bw_pad; /* dml2_project_dcn4x_stage2_auto_drr_svp and above only */
	bool disable_dyn_odm;
	bool disable_dyn_odm_for_multi_stream;
	bool disable_dyn_odm_for_stream_with_svp;
};

struct dml2_options {
	enum dml2_project_id project_id;
	struct dml2_pmo_options pmo_options;
};

struct dml2_initialize_instance_in_out {
	struct dml2_instance *dml2_instance;
	struct dml2_options options;
	struct dml2_soc_bb soc_bb;
	struct dml2_ip_capabilities ip_caps;

	struct {
		void *explicit_ip_bb;
		unsigned int explicit_ip_bb_size;
	} overrides;
};

struct dml2_reset_instance_in_out {
	struct dml2_instance *dml2_instance;
};

struct dml2_check_mode_supported_in_out {
	/*
	* Inputs
	*/
	struct dml2_instance *dml2_instance;
	const struct dml2_display_cfg *display_config;

	/*
	* Outputs
	*/
	bool is_supported;
};

struct dml2_mcache_surface_allocation {
	bool valid;
	/*
	* For iMALL, dedicated mall mcaches are required (sharing of last
	* slice possible), for legacy phantom or phantom without return
	* the only mall mcaches need to be valid.
	*/
	bool requires_dedicated_mall_mcache;

	unsigned int num_mcaches_plane0;
	unsigned int num_mcaches_plane1;
	/*
	* A plane is divided into vertical slices of mcaches,
	* which wrap on the surface width.
	*
	* For example, if the surface width is 7680, and split into
	* three slices of equal width, the boundary array would contain
	* [2560, 5120, 7680]
	*
	* The assignments are
	* 0 = [0 .. 2559]
	* 1 = [2560 .. 5119]
	* 2 = [5120 .. 7679]
	* 0 = [7680 .. INF]
	* The final element implicitly is the same as the first, and
	* at first seems invalid since it is never referenced (since)
	* it is outside the surface. However, its useful when shifting
	* (see below).
	*
	* For any given valid mcache assignment, a shifted version, wrapped
	* on the surface width boundary is also assumed to be valid.
	*
	* For example, shifting [2560, 5120, 7680] by -50 results in
	* [2510, 5170, 7630].
	*
	* The assignments are now:
	* 0 = [0 .. 2509]
	* 1 = [2510 .. 5169]
	* 2 = [5170 .. 7629]
	* 0 = [7630 .. INF]
	*/
	int mcache_x_offsets_plane0[DML2_MAX_MCACHES + 1];
	int mcache_x_offsets_plane1[DML2_MAX_MCACHES + 1];

	/*
	* Shift grainularity is not necessarily 1
	*/
	struct {
		int p0;
		int p1;
	} shift_granularity;

	/*
	* MCacheIDs have global scope in the SoC, and they are stored here.
	* These IDs are generally not valid until all planes in a display
	* configuration have had their mcache requirements calculated.
	*/
	int global_mcache_ids_plane0[DML2_MAX_MCACHES + 1];
	int global_mcache_ids_plane1[DML2_MAX_MCACHES + 1];
	int global_mcache_ids_mall_plane0[DML2_MAX_MCACHES + 1];
	int global_mcache_ids_mall_plane1[DML2_MAX_MCACHES + 1];

	/*
	* Generally, plane0/1 slices must use a disjoint set of caches
	* but in some cases the final segement of the two planes can
	* use the same cache. If plane0_plane1 is set, then this is
	* allowed.
	*
	* Similarly, the caches allocated to MALL prefetcher are generally
	* disjoint, but if mall_prefetch is set, then the final segment
	* between the main and the mall pixel requestor can use the same
	* cache.
	*
	* Note that both bits may be set at the same time.
	*/
	struct {
		bool mall_comb_mcache_p0;
		bool mall_comb_mcache_p1;
		bool plane0_plane1;
	} last_slice_sharing;

	struct {
		int meta_row_bytes_plane0;
		int meta_row_bytes_plane1;
	} informative;
};

enum dml2_pstate_method {
	dml2_pstate_method_na = 0,
	/* hw exclusive modes */
	dml2_pstate_method_vactive = 1,
	dml2_pstate_method_vblank = 2,
	dml2_pstate_method_reserved_hw = 5,
	/* fw assisted exclusive modes */
	dml2_pstate_method_fw_svp = 6,
	dml2_pstate_method_reserved_fw = 10,
	/* fw assisted modes requiring drr modulation */
	dml2_pstate_method_fw_vactive_drr = 11,
	dml2_pstate_method_fw_vblank_drr = 12,
	dml2_pstate_method_fw_svp_drr = 13,
	dml2_pstate_method_reserved_fw_drr_clamped = 20,
	dml2_pstate_method_fw_drr = 21,
	dml2_pstate_method_reserved_fw_drr_var = 22,
	dml2_pstate_method_count
};

struct dml2_per_plane_programming {
	const struct dml2_plane_parameters *plane_descriptor;

	union {
		struct {
			unsigned long dppclk_khz;
		} dcn4x;
	} min_clocks;

	struct dml2_mcache_surface_allocation mcache_allocation;

	// If a stream is using automatic or forced odm combine
	// and the stream for this plane has num_odms_required > 1
	// num_dpps_required is always equal to num_odms_required for
	// ALL planes of the stream

	// If a stream is using odm split, then this value is always 1
	unsigned int num_dpps_required;

	enum dml2_pstate_method uclk_pstate_support_method;

	// MALL size requirements for MALL SS and SubVP
	unsigned int surface_size_mall_bytes;
	unsigned int svp_size_mall_bytes;

	struct dml2_dchub_per_pipe_register_set *pipe_regs[DML2_MAX_PLANES];

	struct {
		bool valid;
		struct dml2_plane_parameters descriptor;
		struct dml2_dchub_per_pipe_register_set *pipe_regs[DML2_MAX_PLANES];
	} phantom_plane;
};

union dml2_global_sync_programming {
	struct {
		unsigned int vstartup_lines;
		unsigned int vupdate_offset_pixels;
		unsigned int vupdate_vupdate_width_pixels;
		unsigned int vready_offset_pixels;
		unsigned int pstate_keepout_start_lines;
	} dcn4x;
};

struct dml2_per_stream_programming {
	const struct dml2_stream_parameters *stream_descriptor;

	union {
		struct {
			unsigned long dscclk_khz;
			unsigned long dtbclk_khz;
			unsigned long phyclk_khz;
		} dcn4x;
	} min_clocks;

	union dml2_global_sync_programming global_sync;

	unsigned int num_odms_required;

	enum dml2_pstate_method uclk_pstate_method;

	struct {
		bool enabled;
		struct dml2_stream_parameters descriptor;
		union dml2_global_sync_programming global_sync;
	} phantom_stream;

	union dmub_cmd_fams2_config fams2_base_params;
	union dmub_cmd_fams2_config fams2_sub_params;
};

//-----------------
// Mode Support Information
//-----------------

struct dml2_mode_support_info {
	bool ModeIsSupported; //<brief Is the mode support any voltage and combine setting
	bool ImmediateFlipSupport; //<brief Means mode support immediate flip at the max combine setting; determine in mode support and used in mode programming
	// Mode Support Reason
	bool WritebackLatencySupport;
	bool ScaleRatioAndTapsSupport;
	bool SourceFormatPixelAndScanSupport;
	bool P2IWith420;
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
	bool ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified;
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
	bool TotalVerticalActiveBandwidthSupport;
	bool VActiveBandwidthSupport;
	enum dml2_pstate_change_support FCLKChangeSupport[DML2_MAX_PLANES];
	bool USRRetrainingSupport;
	bool PrefetchSupported;
	bool DynamicMetadataSupported;
	bool VRatioInPrefetchSupported;
	bool DISPCLK_DPPCLK_Support;
	bool TotalAvailablePipesSupport;
	bool ViewportSizeSupport;
	bool ImmediateFlipSupportedForState;
	double MaxTotalVerticalActiveAvailableBandwidth;
	bool MPCCombineEnable[DML2_MAX_PLANES]; /// <brief Indicate if the MPC Combine enable in the given state and optimize mpc combine setting
	enum dml2_odm_mode ODMMode[DML2_MAX_PLANES]; /// <brief ODM mode that is chosen in the mode check stage and will be used in mode programming stage
	unsigned int DPPPerSurface[DML2_MAX_PLANES]; /// <brief How many DPPs are needed drive the surface to output. If MPCC or ODMC could be 2 or 4.
	bool DSCEnabled[DML2_MAX_PLANES]; /// <brief Indicate if the DSC is actually required; used in mode_programming
	bool FECEnabled[DML2_MAX_PLANES]; /// <brief Indicate if the FEC is actually required
	unsigned int NumberOfDSCSlices[DML2_MAX_PLANES]; /// <brief Indicate how many slices needed to support the given mode
	double OutputBpp[DML2_MAX_PLANES];
	enum dml2_output_type_and_rate__type OutputType[DML2_MAX_PLANES];
	enum dml2_output_type_and_rate__rate OutputRate[DML2_MAX_PLANES];
	unsigned int AlignedYPitch[DML2_MAX_PLANES];
	unsigned int AlignedCPitch[DML2_MAX_PLANES];
	bool g6_temp_read_support;
	bool temp_read_or_ppt_support;
}; // dml2_mode_support_info

struct dml2_display_cfg_programming {
	struct dml2_display_cfg display_config;

	union {
		struct {
			unsigned long dcfclk_khz;
			unsigned long fclk_khz;
			unsigned long uclk_khz;
			unsigned long socclk_khz;
			unsigned long dispclk_khz;
			unsigned long dcfclk_deepsleep_khz;
			unsigned long dpp_ref_khz;
		} dcn32x;
		struct {
			struct {
				unsigned long uclk_khz;
				unsigned long fclk_khz;
				unsigned long dcfclk_khz;
			} active;
			struct {
				unsigned long uclk_khz;
				unsigned long fclk_khz;
				unsigned long dcfclk_khz;
			} idle;
			struct {
				unsigned long uclk_khz;
				unsigned long fclk_khz;
				unsigned long dcfclk_khz;
			} svp_prefetch;
			struct {
				unsigned long uclk_khz;
				unsigned long fclk_khz;
				unsigned long dcfclk_khz;
			} svp_prefetch_no_throttle;

			unsigned long deepsleep_dcfclk_khz;
			unsigned long dispclk_khz;
			unsigned long dpprefclk_khz;
			unsigned long dtbrefclk_khz;
			unsigned long socclk_khz;

			struct {
				uint32_t dispclk_did;
				uint32_t dpprefclk_did;
				uint32_t dtbrefclk_did;
			} divider_ids;
		} dcn4x;
	} min_clocks;

	bool uclk_pstate_supported;
	bool fclk_pstate_supported;

	/* indicates this configuration requires FW to support */
	bool fams2_required;
	struct dmub_cmd_fams2_global_config fams2_global_config;

	struct {
		bool supported_in_blank; // Changing to configurations where this is false requires stutter to be disabled during the transition
	} stutter;

	struct {
		bool meets_eco; // Stutter cycles will meet Z8 ECO criteria
		bool supported_in_blank; // Changing to configurations where this is false requires Z8 to be disabled during the transition
	} z8_stutter;

	struct dml2_dchub_global_register_set global_regs;

	struct dml2_per_plane_programming plane_programming[DML2_MAX_PLANES];
	struct dml2_per_stream_programming stream_programming[DML2_MAX_PLANES];

	// Don't access this structure directly, access it through plane_programming.pipe_regs
	struct dml2_dchub_per_pipe_register_set pipe_regs[DML2_MAX_PLANES];

	struct {
		struct {
			double urgent_us;
			double writeback_urgent_us;
			double writeback_pstate_us;
			double writeback_fclk_pstate_us;
			double cstate_exit_us;
			double cstate_enter_plus_exit_us;
			double z8_cstate_exit_us;
			double z8_cstate_enter_plus_exit_us;
			double pstate_change_us;
			double fclk_pstate_change_us;
			double usr_retraining_us;
			double temp_read_or_ppt_watermark_us;
		} watermarks;

		struct {
			unsigned int swath_width_plane0;
			unsigned int swath_height_plane0;
			unsigned int swath_height_plane1;
			unsigned int dpte_row_height_plane0;
			unsigned int dpte_row_height_plane1;
			unsigned int meta_row_height_plane0;
			unsigned int meta_row_height_plane1;
		} plane_info[DML2_MAX_PLANES];

		struct {
			unsigned long long total_surface_size_in_mall_bytes;
			unsigned int subviewport_lines_needed_in_mall[DML2_MAX_PLANES];
		} mall;

		struct {
			double urgent_latency_us; // urgent ramp latency
			double max_non_urgent_latency_us;
			double max_urgent_latency_us;
			double avg_non_urgent_latency_us;
			double avg_urgent_latency_us;
			double wm_memory_trip_us;
			double meta_trip_memory_us;
			double fraction_of_urgent_bandwidth; // nom
			double fraction_of_urgent_bandwidth_immediate_flip;
			double fraction_of_urgent_bandwidth_mall;
			double max_active_fclk_change_latency_supported;
			unsigned int min_return_latency_in_dcfclk;

			struct {
				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
					double dram_vm_only_bw_mbps;
				} svp_prefetch;

				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
					double dram_vm_only_bw_mbps;
				} sys_active;
			} urg_bw_available;

			struct {
				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} svp_prefetch;

				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} sys_active;
			} avg_bw_available;

			struct {
				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} svp_prefetch;

				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} sys_active;
			} non_urg_bw_required;

			struct {
				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} svp_prefetch;

				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} sys_active;
			} non_urg_bw_required_with_flip;

			struct {
				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} svp_prefetch;

				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} sys_active;

			} urg_bw_required;

			struct {
				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} svp_prefetch;

				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} sys_active;
			} urg_bw_required_with_flip;

			struct {
				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} svp_prefetch;

				struct {
					double sdp_bw_mbps;
					double dram_bw_mbps;
				} sys_active;
			} avg_bw_required;
		} qos;

		struct {
			unsigned long long det_size_in_kbytes[DML2_MAX_PLANES];
			unsigned long long DETBufferSizeY[DML2_MAX_PLANES];
			unsigned long long comp_buffer_size_kbytes;
			bool UnboundedRequestEnabled;
			unsigned int compbuf_reserved_space_64b;
		} crb;

		struct {
			unsigned int max_uncompressed_block_plane0;
			unsigned int max_compressed_block_plane0;
			unsigned int independent_block_plane0;
			unsigned int max_uncompressed_block_plane1;
			unsigned int max_compressed_block_plane1;
			unsigned int independent_block_plane1;
		} dcc_control[DML2_MAX_PLANES];

		struct {
			double stutter_efficiency;
			double stutter_efficiency_with_vblank;
			double stutter_num_bursts;

			struct {
				double stutter_efficiency;
				double stutter_efficiency_with_vblank;
				double stutter_num_bursts;
				double stutter_period;

				struct {
					double stutter_efficiency;
					double stutter_num_bursts;
					double stutter_period;
				} bestcase;
			} z8;
		} power_management;

		struct {
			double min_ttu_vblank_us[DML2_MAX_PLANES];
			bool vready_at_or_after_vsync[DML2_MAX_PLANES];
			double min_dst_y_next_start[DML2_MAX_PLANES];
			bool cstate_max_cap_mode;
			bool hw_debug5;
			unsigned int dcfclk_deep_sleep_hysteresis;
			unsigned int dst_x_after_scaler[DML2_MAX_PLANES];
			unsigned int dst_y_after_scaler[DML2_MAX_PLANES];
			unsigned int prefetch_source_lines_plane0[DML2_MAX_PLANES];
			unsigned int prefetch_source_lines_plane1[DML2_MAX_PLANES];
			bool ImmediateFlipSupportedForPipe[DML2_MAX_PLANES];
			bool UsesMALLForStaticScreen[DML2_MAX_PLANES];
			unsigned int CursorDstXOffset[DML2_MAX_PLANES];
			unsigned int CursorDstYOffset[DML2_MAX_PLANES];
			unsigned int CursorChunkHDLAdjust[DML2_MAX_PLANES];
			unsigned int dpte_group_bytes[DML2_MAX_PLANES];
			unsigned int vm_group_bytes[DML2_MAX_PLANES];
			double DisplayPipeRequestDeliveryTimeLuma[DML2_MAX_PLANES];
			double DisplayPipeRequestDeliveryTimeChroma[DML2_MAX_PLANES];
			double DisplayPipeRequestDeliveryTimeLumaPrefetch[DML2_MAX_PLANES];
			double DisplayPipeRequestDeliveryTimeChromaPrefetch[DML2_MAX_PLANES];
			double TimePerVMGroupVBlank[DML2_MAX_PLANES];
			double TimePerVMGroupFlip[DML2_MAX_PLANES];
			double TimePerVMRequestVBlank[DML2_MAX_PLANES];
			double TimePerVMRequestFlip[DML2_MAX_PLANES];
			double Tdmdl_vm[DML2_MAX_PLANES];
			double Tdmdl[DML2_MAX_PLANES];
			unsigned int VStartup[DML2_MAX_PLANES];
			unsigned int VUpdateOffsetPix[DML2_MAX_PLANES];
			unsigned int VUpdateWidthPix[DML2_MAX_PLANES];
			unsigned int VReadyOffsetPix[DML2_MAX_PLANES];

			double DST_Y_PER_PTE_ROW_NOM_L[DML2_MAX_PLANES];
			double DST_Y_PER_PTE_ROW_NOM_C[DML2_MAX_PLANES];
			double time_per_pte_group_nom_luma[DML2_MAX_PLANES];
			double time_per_pte_group_nom_chroma[DML2_MAX_PLANES];
			double time_per_pte_group_vblank_luma[DML2_MAX_PLANES];
			double time_per_pte_group_vblank_chroma[DML2_MAX_PLANES];
			double time_per_pte_group_flip_luma[DML2_MAX_PLANES];
			double time_per_pte_group_flip_chroma[DML2_MAX_PLANES];
			double VRatioPrefetchY[DML2_MAX_PLANES];
			double VRatioPrefetchC[DML2_MAX_PLANES];
			double DestinationLinesForPrefetch[DML2_MAX_PLANES];
			double DestinationLinesToRequestVMInVBlank[DML2_MAX_PLANES];
			double DestinationLinesToRequestRowInVBlank[DML2_MAX_PLANES];
			double DestinationLinesToRequestVMInImmediateFlip[DML2_MAX_PLANES];
			double DestinationLinesToRequestRowInImmediateFlip[DML2_MAX_PLANES];
			double DisplayPipeLineDeliveryTimeLuma[DML2_MAX_PLANES];
			double DisplayPipeLineDeliveryTimeChroma[DML2_MAX_PLANES];
			double DisplayPipeLineDeliveryTimeLumaPrefetch[DML2_MAX_PLANES];
			double DisplayPipeLineDeliveryTimeChromaPrefetch[DML2_MAX_PLANES];

			double WritebackRequiredBandwidth;
			double WritebackAllowDRAMClockChangeEndPosition[DML2_MAX_PLANES];
			double WritebackAllowFCLKChangeEndPosition[DML2_MAX_PLANES];
			double DSCCLK_calculated[DML2_MAX_PLANES];
			unsigned int BIGK_FRAGMENT_SIZE[DML2_MAX_PLANES];
			bool PTE_BUFFER_MODE[DML2_MAX_PLANES];
			double DSCDelay[DML2_MAX_PLANES];
			double MaxActiveDRAMClockChangeLatencySupported[DML2_MAX_PLANES];
			unsigned int PrefetchMode[DML2_MAX_PLANES]; // LEGACY_ONLY
			bool ROBUrgencyAvoidance;
			double LowestPrefetchMargin;
		} misc;

		struct dml2_mode_support_info mode_support_info;
		unsigned int voltage_level; // LEGACY_ONLY

		// For DV only
		// This is what dml core calculated, only on the full_vp width and assume we have
		// unlimited # of mcache
		struct dml2_mcache_surface_allocation non_optimized_mcache_allocation[DML2_MAX_PLANES];

		bool failed_mcache_validation;
		bool failed_dpmm;
		bool failed_mode_programming;
		bool failed_map_watermarks;
	} informative;
};

struct dml2_build_mode_programming_in_out {
	/*
	* Inputs
	*/
	struct dml2_instance *dml2_instance;
	const struct dml2_display_cfg *display_config;

	/*
	* Outputs
	*/
	struct dml2_display_cfg_programming *programming;
};

struct dml2_build_mcache_programming_in_out {
	/*
	* Inputs
	*/
	struct dml2_instance *dml2_instance;

	struct dml2_plane_mcache_configuration_descriptor mcache_configurations[DML2_MAX_PLANES];
	char num_configurations;

	/*
	* Outputs
	*/
	// per_plane_pipe_mcache_regs[i][j] refers to the proper programming for the j-th pipe of the
	// i-th plane (from mcache_configurations)
	struct dml2_hubp_pipe_mcache_regs *per_plane_pipe_mcache_regs[DML2_MAX_PLANES][DML2_MAX_DCN_PIPES];

	// It's not a good idea to reference this directly, better to use the pointer structure above instead
	struct dml2_hubp_pipe_mcache_regs mcache_regs_set[DML2_MAX_DCN_PIPES];
};

struct dml2_unit_test_in_out {
	/*
	* Inputs
	*/
	struct dml2_instance *dml2_instance;
};


#endif
