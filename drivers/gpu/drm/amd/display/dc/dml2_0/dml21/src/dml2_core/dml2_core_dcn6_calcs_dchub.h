// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_DCN6_CALCS_DCHUB_H__
#define __DML2_CORE_DCN6_CALCS_DCHUB_H__
#include "dml2_internal_shared_types.h"

unsigned int dcn6_calculate_max_vstartup(
	bool ptoi_supported,
	unsigned int vblank_nom_default_us,
	const struct dml2_timing_cfg *timing,
	enum dml2_uclk_pstate_change_strategy pstate_strategy,
	double write_back_delay_us,
	unsigned int svp_lines);
void dcn6_calculate_alternate_params(struct dml2_core_calcs_calculate_alternate_params *p);
void dcn6_calculate_alternate_svp_lines(struct dml2_core_calcs_calculate_alternate_svp_lines *p);
void dcn6_calculate_alternate_lead_lines(struct dml2_core_calcs_calculate_alternate_lead_lines *p);

void dcn6_calculate_flip_schedule(
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
	double vm_bytes, // vm_bytes
	double DPTEBytesPerRow, // dpte_row_bytes
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

void dcn6_get_pipe_regs(const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		struct dml2_dchub_per_pipe_register_set *out, int pipe_index, const struct dml2_utm_soc_bb *utm_soc_bb,
		struct dml2_core_internal_scratch *s);

void dcn6_calculate_watermarks_and_dram_speed_change_support(
		struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params *p);

void dcn6_calculate_stutter_efficiency(struct dml2_core_internal_scratch *scratch,
		struct dml2_core_calcs_CalculateStutterEfficiency_params *p);

void dcn6_get_watermarks(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, const struct dml2_utm_soc_bb *utm_soc_bb, struct dml2_dchub_watermark_regs *out);

void dcn6_calculate_excess_vactive_bandwidth_required(
	const struct dml2_display_cfg *display_cfg,
	unsigned int bytes_required_l[dml2_pstate_type_count][DML2_MAX_PLANES],
	unsigned int bytes_required_c[dml2_pstate_type_count][DML2_MAX_PLANES],
	/* outputs */
	double excess_vactive_fill_bw_l[],
	double excess_vactive_fill_bw_c[]);

void dcn6_calculate_pstate_schedule_windows(
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

void dcn6_calculate_pstate_schedule_admissibility(
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

#endif /* __DML2_CORE_DCN6_CALCS_DCHUB_H__ */
