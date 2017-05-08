/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "display_rq_dlg_helpers.h"

void print__rq_params_st(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_rq_params_st rq_param)
{
	DTRACE("RQ_DLG_CALC: *************************** ");
	DTRACE("RQ_DLG_CALC: DISPLAY_RQ_PARAM_ST");
	DTRACE("RQ_DLG_CALC:  <LUMA>");
	print__data_rq_sizing_params_st(mode_lib, rq_param.sizing.rq_l);
	DTRACE("RQ_DLG_CALC:  <CHROMA> === ");
	print__data_rq_sizing_params_st(mode_lib, rq_param.sizing.rq_c);

	DTRACE("RQ_DLG_CALC: <LUMA>");
	print__data_rq_dlg_params_st(mode_lib, rq_param.dlg.rq_l);
	DTRACE("RQ_DLG_CALC: <CHROMA>");
	print__data_rq_dlg_params_st(mode_lib, rq_param.dlg.rq_c);

	DTRACE("RQ_DLG_CALC: <LUMA>");
	print__data_rq_misc_params_st(mode_lib, rq_param.misc.rq_l);
	DTRACE("RQ_DLG_CALC: <CHROMA>");
	print__data_rq_misc_params_st(mode_lib, rq_param.misc.rq_c);
	DTRACE("RQ_DLG_CALC: *************************** ");
}

void print__data_rq_sizing_params_st(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_data_rq_sizing_params_st rq_sizing)
{
	DTRACE("RQ_DLG_CALC: ===================================== ");
	DTRACE("RQ_DLG_CALC: DISPLAY_DATA_RQ_SIZING_PARAM_ST");
	DTRACE("RQ_DLG_CALC:    chunk_bytes           = %0d", rq_sizing.chunk_bytes);
	DTRACE("RQ_DLG_CALC:    min_chunk_bytes       = %0d", rq_sizing.min_chunk_bytes);
	DTRACE("RQ_DLG_CALC:    meta_chunk_bytes      = %0d", rq_sizing.meta_chunk_bytes);
	DTRACE("RQ_DLG_CALC:    min_meta_chunk_bytes  = %0d", rq_sizing.min_meta_chunk_bytes);
	DTRACE("RQ_DLG_CALC:    mpte_group_bytes      = %0d", rq_sizing.mpte_group_bytes);
	DTRACE("RQ_DLG_CALC:    dpte_group_bytes      = %0d", rq_sizing.dpte_group_bytes);
	DTRACE("RQ_DLG_CALC: ===================================== ");
}

void print__data_rq_dlg_params_st(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_data_rq_dlg_params_st rq_dlg_param)
{
	DTRACE("RQ_DLG_CALC: ===================================== ");
	DTRACE("RQ_DLG_CALC: DISPLAY_DATA_RQ_DLG_PARAM_ST");
	DTRACE("RQ_DLG_CALC:    swath_width_ub              = %0d", rq_dlg_param.swath_width_ub);
	DTRACE("RQ_DLG_CALC:    swath_height                = %0d", rq_dlg_param.swath_height);
	DTRACE("RQ_DLG_CALC:    req_per_swath_ub            = %0d", rq_dlg_param.req_per_swath_ub);
	DTRACE(
			"RQ_DLG_CALC:    meta_pte_bytes_per_frame_ub = %0d",
			rq_dlg_param.meta_pte_bytes_per_frame_ub);
	DTRACE(
			"RQ_DLG_CALC:    dpte_req_per_row_ub         = %0d",
			rq_dlg_param.dpte_req_per_row_ub);
	DTRACE(
			"RQ_DLG_CALC:    dpte_groups_per_row_ub      = %0d",
			rq_dlg_param.dpte_groups_per_row_ub);
	DTRACE("RQ_DLG_CALC:    dpte_row_height             = %0d", rq_dlg_param.dpte_row_height);
	DTRACE(
			"RQ_DLG_CALC:    dpte_bytes_per_row_ub       = %0d",
			rq_dlg_param.dpte_bytes_per_row_ub);
	DTRACE(
			"RQ_DLG_CALC:    meta_chunks_per_row_ub      = %0d",
			rq_dlg_param.meta_chunks_per_row_ub);
	DTRACE(
			"RQ_DLG_CALC:    meta_req_per_row_ub         = %0d",
			rq_dlg_param.meta_req_per_row_ub);
	DTRACE("RQ_DLG_CALC:    meta_row_height             = %0d", rq_dlg_param.meta_row_height);
	DTRACE(
			"RQ_DLG_CALC:    meta_bytes_per_row_ub       = %0d",
			rq_dlg_param.meta_bytes_per_row_ub);
	DTRACE("RQ_DLG_CALC: ===================================== ");
}

void print__data_rq_misc_params_st(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_data_rq_misc_params_st rq_misc_param)
{
	DTRACE("RQ_DLG_CALC: ===================================== ");
	DTRACE("RQ_DLG_CALC: DISPLAY_DATA_RQ_MISC_PARAM_ST");
	DTRACE("RQ_DLG_CALC:     full_swath_bytes   = %0d", rq_misc_param.full_swath_bytes);
	DTRACE("RQ_DLG_CALC:     stored_swath_bytes = %0d", rq_misc_param.stored_swath_bytes);
	DTRACE("RQ_DLG_CALC:     blk256_width       = %0d", rq_misc_param.blk256_width);
	DTRACE("RQ_DLG_CALC:     blk256_height      = %0d", rq_misc_param.blk256_height);
	DTRACE("RQ_DLG_CALC:     req_width          = %0d", rq_misc_param.req_width);
	DTRACE("RQ_DLG_CALC:     req_height         = %0d", rq_misc_param.req_height);
	DTRACE("RQ_DLG_CALC: ===================================== ");
}

void print__rq_dlg_params_st(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_rq_dlg_params_st rq_dlg_param)
{
	DTRACE("RQ_DLG_CALC: ===================================== ");
	DTRACE("RQ_DLG_CALC: DISPLAY_RQ_DLG_PARAM_ST");
	DTRACE("RQ_DLG_CALC:  <LUMA> ");
	print__data_rq_dlg_params_st(mode_lib, rq_dlg_param.rq_l);
	DTRACE("RQ_DLG_CALC:  <CHROMA> ");
	print__data_rq_dlg_params_st(mode_lib, rq_dlg_param.rq_c);
	DTRACE("RQ_DLG_CALC: ===================================== ");
}

void print__dlg_sys_params_st(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_dlg_sys_params_st dlg_sys_param)
{
	DTRACE("RQ_DLG_CALC: ===================================== ");
	DTRACE("RQ_DLG_CALC: DISPLAY_RQ_DLG_PARAM_ST");
	DTRACE("RQ_DLG_CALC:    t_mclk_wm_us         = %3.2f", dlg_sys_param.t_mclk_wm_us);
	DTRACE("RQ_DLG_CALC:    t_urg_wm_us          = %3.2f", dlg_sys_param.t_urg_wm_us);
	DTRACE("RQ_DLG_CALC:    t_sr_wm_us           = %3.2f", dlg_sys_param.t_sr_wm_us);
	DTRACE("RQ_DLG_CALC:    t_extra_us           = %3.2f", dlg_sys_param.t_extra_us);
	DTRACE("RQ_DLG_CALC:    t_srx_delay_us       = %3.2f", dlg_sys_param.t_srx_delay_us);
	DTRACE("RQ_DLG_CALC:    deepsleep_dcfclk_mhz = %3.2f", dlg_sys_param.deepsleep_dcfclk_mhz);
	DTRACE("RQ_DLG_CALC: ===================================== ");
}

void print__data_rq_regs_st(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_data_rq_regs_st rq_regs)
{
	DTRACE("RQ_DLG_CALC: ===================================== ");
	DTRACE("RQ_DLG_CALC: DISPLAY_DATA_RQ_REGS_ST");
	DTRACE("RQ_DLG_CALC:    chunk_size              = 0x%0x", rq_regs.chunk_size);
	DTRACE("RQ_DLG_CALC:    min_chunk_size          = 0x%0x", rq_regs.min_chunk_size);
	DTRACE("RQ_DLG_CALC:    meta_chunk_size         = 0x%0x", rq_regs.meta_chunk_size);
	DTRACE("RQ_DLG_CALC:    min_meta_chunk_size     = 0x%0x", rq_regs.min_meta_chunk_size);
	DTRACE("RQ_DLG_CALC:    dpte_group_size         = 0x%0x", rq_regs.dpte_group_size);
	DTRACE("RQ_DLG_CALC:    mpte_group_size         = 0x%0x", rq_regs.mpte_group_size);
	DTRACE("RQ_DLG_CALC:    swath_height            = 0x%0x", rq_regs.swath_height);
	DTRACE("RQ_DLG_CALC:    pte_row_height_linear   = 0x%0x", rq_regs.pte_row_height_linear);
	DTRACE("RQ_DLG_CALC: ===================================== ");
}

void print__rq_regs_st(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_rq_regs_st rq_regs)
{
	DTRACE("RQ_DLG_CALC: ===================================== ");
	DTRACE("RQ_DLG_CALC: DISPLAY_RQ_REGS_ST");
	DTRACE("RQ_DLG_CALC:  <LUMA> ");
	print__data_rq_regs_st(mode_lib, rq_regs.rq_regs_l);
	DTRACE("RQ_DLG_CALC:  <CHROMA> ");
	print__data_rq_regs_st(mode_lib, rq_regs.rq_regs_c);
	DTRACE("RQ_DLG_CALC:    drq_expansion_mode  = 0x%0x", rq_regs.drq_expansion_mode);
	DTRACE("RQ_DLG_CALC:    prq_expansion_mode  = 0x%0x", rq_regs.prq_expansion_mode);
	DTRACE("RQ_DLG_CALC:    mrq_expansion_mode  = 0x%0x", rq_regs.mrq_expansion_mode);
	DTRACE("RQ_DLG_CALC:    crq_expansion_mode  = 0x%0x", rq_regs.crq_expansion_mode);
	DTRACE("RQ_DLG_CALC:    plane1_base_address = 0x%0x", rq_regs.plane1_base_address);
	DTRACE("RQ_DLG_CALC: ===================================== ");
}

void print__dlg_regs_st(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_dlg_regs_st dlg_regs)
{
	DTRACE("RQ_DLG_CALC: ===================================== ");
	DTRACE("RQ_DLG_CALC: DISPLAY_DLG_REGS_ST ");
	DTRACE(
			"RQ_DLG_CALC:    refcyc_h_blank_end              = 0x%0x",
			dlg_regs.refcyc_h_blank_end);
	DTRACE("RQ_DLG_CALC:    dlg_vblank_end                  = 0x%0x", dlg_regs.dlg_vblank_end);
	DTRACE(
			"RQ_DLG_CALC:    min_dst_y_next_start            = 0x%0x",
			dlg_regs.min_dst_y_next_start);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_htotal               = 0x%0x",
			dlg_regs.refcyc_per_htotal);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_x_after_scaler           = 0x%0x",
			dlg_regs.refcyc_x_after_scaler);
	DTRACE(
			"RQ_DLG_CALC:    dst_y_after_scaler              = 0x%0x",
			dlg_regs.dst_y_after_scaler);
	DTRACE("RQ_DLG_CALC:    dst_y_prefetch                  = 0x%0x", dlg_regs.dst_y_prefetch);
	DTRACE(
			"RQ_DLG_CALC:    dst_y_per_vm_vblank             = 0x%0x",
			dlg_regs.dst_y_per_vm_vblank);
	DTRACE(
			"RQ_DLG_CALC:    dst_y_per_row_vblank            = 0x%0x",
			dlg_regs.dst_y_per_row_vblank);
	DTRACE(
			"RQ_DLG_CALC:    ref_freq_to_pix_freq            = 0x%0x",
			dlg_regs.ref_freq_to_pix_freq);
	DTRACE("RQ_DLG_CALC:    vratio_prefetch                 = 0x%0x", dlg_regs.vratio_prefetch);
	DTRACE(
			"RQ_DLG_CALC:    vratio_prefetch_c               = 0x%0x",
			dlg_regs.vratio_prefetch_c);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_pte_group_vblank_l   = 0x%0x",
			dlg_regs.refcyc_per_pte_group_vblank_l);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_pte_group_vblank_c   = 0x%0x",
			dlg_regs.refcyc_per_pte_group_vblank_c);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_meta_chunk_vblank_l  = 0x%0x",
			dlg_regs.refcyc_per_meta_chunk_vblank_l);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_meta_chunk_vblank_c  = 0x%0x",
			dlg_regs.refcyc_per_meta_chunk_vblank_c);
	DTRACE(
			"RQ_DLG_CALC:    dst_y_per_pte_row_nom_l         = 0x%0x",
			dlg_regs.dst_y_per_pte_row_nom_l);
	DTRACE(
			"RQ_DLG_CALC:    dst_y_per_pte_row_nom_c         = 0x%0x",
			dlg_regs.dst_y_per_pte_row_nom_c);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_pte_group_nom_l      = 0x%0x",
			dlg_regs.refcyc_per_pte_group_nom_l);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_pte_group_nom_c      = 0x%0x",
			dlg_regs.refcyc_per_pte_group_nom_c);
	DTRACE(
			"RQ_DLG_CALC:    dst_y_per_meta_row_nom_l        = 0x%0x",
			dlg_regs.dst_y_per_meta_row_nom_l);
	DTRACE(
			"RQ_DLG_CALC:    dst_y_per_meta_row_nom_c        = 0x%0x",
			dlg_regs.dst_y_per_meta_row_nom_c);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_meta_chunk_nom_l     = 0x%0x",
			dlg_regs.refcyc_per_meta_chunk_nom_l);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_meta_chunk_nom_c     = 0x%0x",
			dlg_regs.refcyc_per_meta_chunk_nom_c);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_line_delivery_pre_l  = 0x%0x",
			dlg_regs.refcyc_per_line_delivery_pre_l);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_line_delivery_pre_c  = 0x%0x",
			dlg_regs.refcyc_per_line_delivery_pre_c);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_line_delivery_l      = 0x%0x",
			dlg_regs.refcyc_per_line_delivery_l);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_line_delivery_c      = 0x%0x",
			dlg_regs.refcyc_per_line_delivery_c);
	DTRACE(
			"RQ_DLG_CALC:    chunk_hdl_adjust_cur0           = 0x%0x",
			dlg_regs.chunk_hdl_adjust_cur0);
	DTRACE("RQ_DLG_CALC: ===================================== ");
}

void print__ttu_regs_st(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_ttu_regs_st ttu_regs)
{
	DTRACE("RQ_DLG_CALC: ===================================== ");
	DTRACE("RQ_DLG_CALC: DISPLAY_TTU_REGS_ST ");
	DTRACE(
			"RQ_DLG_CALC:    qos_level_low_wm                  = 0x%0x",
			ttu_regs.qos_level_low_wm);
	DTRACE(
			"RQ_DLG_CALC:    qos_level_high_wm                 = 0x%0x",
			ttu_regs.qos_level_high_wm);
	DTRACE("RQ_DLG_CALC:    min_ttu_vblank                    = 0x%0x", ttu_regs.min_ttu_vblank);
	DTRACE("RQ_DLG_CALC:    qos_level_flip                    = 0x%0x", ttu_regs.qos_level_flip);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_req_delivery_pre_l     = 0x%0x",
			ttu_regs.refcyc_per_req_delivery_pre_l);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_req_delivery_l         = 0x%0x",
			ttu_regs.refcyc_per_req_delivery_l);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_req_delivery_pre_c     = 0x%0x",
			ttu_regs.refcyc_per_req_delivery_pre_c);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_req_delivery_c         = 0x%0x",
			ttu_regs.refcyc_per_req_delivery_c);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_req_delivery_cur0      = 0x%0x",
			ttu_regs.refcyc_per_req_delivery_cur0);
	DTRACE(
			"RQ_DLG_CALC:    refcyc_per_req_delivery_pre_cur0  = 0x%0x",
			ttu_regs.refcyc_per_req_delivery_pre_cur0);
	DTRACE(
			"RQ_DLG_CALC:    qos_level_fixed_l                 = 0x%0x",
			ttu_regs.qos_level_fixed_l);
	DTRACE(
			"RQ_DLG_CALC:    qos_ramp_disable_l                = 0x%0x",
			ttu_regs.qos_ramp_disable_l);
	DTRACE(
			"RQ_DLG_CALC:    qos_level_fixed_c                 = 0x%0x",
			ttu_regs.qos_level_fixed_c);
	DTRACE(
			"RQ_DLG_CALC:    qos_ramp_disable_c                = 0x%0x",
			ttu_regs.qos_ramp_disable_c);
	DTRACE(
			"RQ_DLG_CALC:    qos_level_fixed_cur0              = 0x%0x",
			ttu_regs.qos_level_fixed_cur0);
	DTRACE(
			"RQ_DLG_CALC:    qos_ramp_disable_cur0             = 0x%0x",
			ttu_regs.qos_ramp_disable_cur0);
	DTRACE("RQ_DLG_CALC: ===================================== ");
}
