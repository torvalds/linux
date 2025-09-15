// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __dml2_TOP_DCHUB_REGISTERS_H__
#define __dml2_TOP_DCHUB_REGISTERS_H__

#include "dml2_external_lib_deps.h"
// These types are uint32_t as they represent actual calculated register values for HW

struct dml2_display_dlg_regs {
	uint32_t refcyc_h_blank_end;
	uint32_t dlg_vblank_end;
	uint32_t min_dst_y_next_start;
	uint32_t refcyc_per_htotal;
	uint32_t refcyc_x_after_scaler;
	uint32_t dst_y_after_scaler;
	uint32_t dst_y_prefetch;
	uint32_t dst_y_per_vm_vblank;
	uint32_t dst_y_per_row_vblank;
	uint32_t dst_y_per_vm_flip;
	uint32_t dst_y_per_row_flip;
	uint32_t ref_freq_to_pix_freq;
	uint32_t vratio_prefetch;
	uint32_t vratio_prefetch_c;
	uint32_t refcyc_per_tdlut_group;
	uint32_t refcyc_per_pte_group_vblank_l;
	uint32_t refcyc_per_pte_group_vblank_c;
	uint32_t refcyc_per_pte_group_flip_l;
	uint32_t refcyc_per_pte_group_flip_c;
	uint32_t dst_y_per_pte_row_nom_l;
	uint32_t dst_y_per_pte_row_nom_c;
	uint32_t refcyc_per_pte_group_nom_l;
	uint32_t refcyc_per_pte_group_nom_c;
	uint32_t refcyc_per_line_delivery_pre_l;
	uint32_t refcyc_per_line_delivery_pre_c;
	uint32_t refcyc_per_line_delivery_l;
	uint32_t refcyc_per_line_delivery_c;
	uint32_t refcyc_per_vm_group_vblank;
	uint32_t refcyc_per_vm_group_flip;
	uint32_t refcyc_per_vm_req_vblank;
	uint32_t refcyc_per_vm_req_flip;
	uint32_t dst_y_offset_cur0;
	uint32_t chunk_hdl_adjust_cur0;
	uint32_t vready_after_vcount0;
	uint32_t dst_y_delta_drq_limit;
	uint32_t refcyc_per_vm_dmdata;
	uint32_t dmdata_dl_delta;
	uint32_t dst_y_svp_drq_limit;

	// MRQ
	uint32_t refcyc_per_meta_chunk_vblank_l;
	uint32_t refcyc_per_meta_chunk_vblank_c;
	uint32_t refcyc_per_meta_chunk_flip_l;
	uint32_t refcyc_per_meta_chunk_flip_c;
	uint32_t dst_y_per_meta_row_nom_l;
	uint32_t dst_y_per_meta_row_nom_c;
	uint32_t refcyc_per_meta_chunk_nom_l;
	uint32_t refcyc_per_meta_chunk_nom_c;
};

struct dml2_display_ttu_regs {
	uint32_t qos_level_low_wm;
	uint32_t qos_level_high_wm;
	uint32_t min_ttu_vblank;
	uint32_t qos_level_flip;
	uint32_t refcyc_per_req_delivery_l;
	uint32_t refcyc_per_req_delivery_c;
	uint32_t refcyc_per_req_delivery_cur0;
	uint32_t refcyc_per_req_delivery_pre_l;
	uint32_t refcyc_per_req_delivery_pre_c;
	uint32_t refcyc_per_req_delivery_pre_cur0;
	uint32_t qos_level_fixed_l;
	uint32_t qos_level_fixed_c;
	uint32_t qos_level_fixed_cur0;
	uint32_t qos_ramp_disable_l;
	uint32_t qos_ramp_disable_c;
	uint32_t qos_ramp_disable_cur0;
};

struct dml2_display_arb_regs {
	uint32_t max_req_outstanding;
	uint32_t min_req_outstanding;
	uint32_t sat_level_us;
	uint32_t hvm_max_qos_commit_threshold;
	uint32_t hvm_min_req_outstand_commit_threshold;
	uint32_t compbuf_reserved_space_kbytes;
	uint32_t compbuf_size;
	uint32_t sdpif_request_rate_limit;
	uint32_t allow_sdpif_rate_limit_when_cstate_req;
	uint32_t dcfclk_deep_sleep_hysteresis;
	uint32_t pstate_stall_threshold;
};

struct dml2_cursor_dlg_regs{
	uint32_t dst_x_offset;			   // CURSOR0_DST_X_OFFSET
	uint32_t dst_y_offset;			   // CURSOR0_DST_Y_OFFSET
	uint32_t chunk_hdl_adjust;		   // CURSOR0_CHUNK_HDL_ADJUST

	uint32_t qos_level_fixed;
	uint32_t qos_ramp_disable;
};

struct dml2_display_plane_rq_regs {
	uint32_t chunk_size;
	uint32_t min_chunk_size;
	uint32_t dpte_group_size;
	uint32_t mpte_group_size;
	uint32_t swath_height;
	uint32_t pte_row_height_linear;

	// MRQ
	uint32_t meta_chunk_size;
	uint32_t min_meta_chunk_size;
};

struct dml2_display_rq_regs {
	struct dml2_display_plane_rq_regs rq_regs_l;
	struct dml2_display_plane_rq_regs rq_regs_c;
	uint32_t drq_expansion_mode;
	uint32_t prq_expansion_mode;
	uint32_t crq_expansion_mode;
	uint32_t plane1_base_address;
	uint32_t unbounded_request_enabled;

	// MRQ
	uint32_t mrq_expansion_mode;
};

struct dml2_display_mcache_regs {
	uint32_t mcache_id_first;
	uint32_t mcache_id_second;
	uint32_t split_location;
};

struct dml2_hubp_pipe_mcache_regs {
	struct {
		struct dml2_display_mcache_regs p0;
		struct dml2_display_mcache_regs p1;
	} main;
	struct {
		struct dml2_display_mcache_regs p0;
		struct dml2_display_mcache_regs p1;
	} mall;
};

struct dml2_dchub_per_pipe_register_set {
	struct dml2_display_rq_regs rq_regs;
	struct dml2_display_ttu_regs ttu_regs;
	struct dml2_display_dlg_regs dlg_regs;

	uint32_t det_size;
};

struct dml2_dchub_watermark_regs {
	/* watermarks */
	uint32_t urgent;
	uint32_t sr_enter;
	uint32_t sr_exit;
	uint32_t sr_enter_z8;
	uint32_t sr_exit_z8;
	uint32_t sr_enter_low_power;
	uint32_t sr_exit_low_power;
	uint32_t uclk_pstate;
	uint32_t fclk_pstate;
	uint32_t temp_read_or_ppt;
	uint32_t usr;
	/* qos */
	uint32_t refcyc_per_trip_to_mem;
	uint32_t refcyc_per_meta_trip_to_mem;
	uint32_t frac_urg_bw_flip;
	uint32_t frac_urg_bw_nom;
	uint32_t frac_urg_bw_mall;
};

enum dml2_dchub_watermark_reg_set_index {
	DML2_DCHUB_WATERMARK_SET_A = 0,
	DML2_DCHUB_WATERMARK_SET_B = 1,
	DML2_DCHUB_WATERMARK_SET_C = 2,
	DML2_DCHUB_WATERMARK_SET_D = 3,
	DML2_DCHUB_WATERMARK_SET_NUM = 4,
};

struct dml2_dchub_global_register_set {
	struct dml2_display_arb_regs arb_regs;
	struct dml2_dchub_watermark_regs wm_regs[DML2_DCHUB_WATERMARK_SET_NUM];
	unsigned int num_watermark_sets;
};

#endif
