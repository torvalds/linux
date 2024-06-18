/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
 */

#include "dml_display_rq_dlg_calc.h"
#include "display_mode_core.h"
#include "display_mode_util.h"

static dml_bool_t is_dual_plane(enum dml_source_format_class source_format)
{
	dml_bool_t ret_val = 0;

	if ((source_format == dml_420_12) || (source_format == dml_420_8) || (source_format == dml_420_10) || (source_format == dml_rgbe_alpha))
		ret_val = 1;

	return ret_val;
}

void dml_rq_dlg_get_rq_reg(dml_display_rq_regs_st			*rq_regs,
							struct display_mode_lib_st	 *mode_lib,
							const dml_uint_t			  pipe_idx)
{
	dml_uint_t plane_idx					= dml_get_plane_idx(mode_lib, pipe_idx);
	enum dml_source_format_class source_format	= mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[plane_idx];
	enum dml_swizzle_mode	  sw_mode		 = mode_lib->ms.cache_display_cfg.surface.SurfaceTiling[plane_idx];
	dml_bool_t dual_plane					= is_dual_plane((enum dml_source_format_class)(source_format));

	uint32 pixel_chunk_bytes = 0;
	uint32 min_pixel_chunk_bytes = 0;
	uint32 meta_chunk_bytes = 0;
	uint32 min_meta_chunk_bytes = 0;
	uint32 dpte_group_bytes = 0;
	uint32 mpte_group_bytes = 0;

	uint32 p1_pixel_chunk_bytes = 0;
	uint32 p1_min_pixel_chunk_bytes = 0;
	uint32 p1_meta_chunk_bytes = 0;
	uint32 p1_min_meta_chunk_bytes = 0;
	uint32 p1_dpte_group_bytes = 0;
	uint32 p1_mpte_group_bytes = 0;

	dml_uint_t detile_buf_size_in_bytes;
	dml_uint_t detile_buf_plane1_addr = 0;

	dml_float_t stored_swath_l_bytes;
	dml_float_t stored_swath_c_bytes;
	dml_bool_t	is_phantom_pipe;

	dml_uint_t pte_row_height_linear;

	dml_print("DML_DLG::%s: Calculation for pipe[%d] start\n", __func__, pipe_idx);

	memset(rq_regs, 0, sizeof(*rq_regs));

	pixel_chunk_bytes		= (dml_uint_t)(dml_get_pixel_chunk_size_in_kbyte(mode_lib) * 1024);
	min_pixel_chunk_bytes	= (dml_uint_t)(dml_get_min_pixel_chunk_size_in_byte(mode_lib));

	if (pixel_chunk_bytes == 64 * 1024)
		min_pixel_chunk_bytes = 0;

	meta_chunk_bytes		= (dml_uint_t)(dml_get_meta_chunk_size_in_kbyte(mode_lib) * 1024);
	min_meta_chunk_bytes	= (dml_uint_t)(dml_get_min_meta_chunk_size_in_byte(mode_lib));

	dpte_group_bytes = (dml_uint_t)(dml_get_dpte_group_size_in_bytes(mode_lib, pipe_idx));
	mpte_group_bytes = (dml_uint_t)(dml_get_vm_group_size_in_bytes(mode_lib, pipe_idx));

	p1_pixel_chunk_bytes		=  pixel_chunk_bytes;
	p1_min_pixel_chunk_bytes	=  min_pixel_chunk_bytes;
	p1_meta_chunk_bytes			=  meta_chunk_bytes;
	p1_min_meta_chunk_bytes		=  min_meta_chunk_bytes;
	p1_dpte_group_bytes			=  dpte_group_bytes;
	p1_mpte_group_bytes			=  mpte_group_bytes;

	if (source_format == dml_rgbe_alpha)
		p1_pixel_chunk_bytes = (dml_uint_t)(dml_get_alpha_pixel_chunk_size_in_kbyte(mode_lib) * 1024);

	rq_regs->rq_regs_l.chunk_size = (dml_uint_t)(dml_log2((dml_float_t) pixel_chunk_bytes) - 10);
	rq_regs->rq_regs_c.chunk_size = (dml_uint_t)(dml_log2((dml_float_t) p1_pixel_chunk_bytes) - 10);

	if (min_pixel_chunk_bytes == 0)
		rq_regs->rq_regs_l.min_chunk_size = 0;
	else
		rq_regs->rq_regs_l.min_chunk_size = (dml_uint_t)(dml_log2((dml_float_t) min_pixel_chunk_bytes) - 8 + 1);

	if (p1_min_pixel_chunk_bytes == 0)
		rq_regs->rq_regs_c.min_chunk_size = 0;
	else
		rq_regs->rq_regs_c.min_chunk_size = (dml_uint_t)(dml_log2((dml_float_t) p1_min_pixel_chunk_bytes) - 8 + 1);

	rq_regs->rq_regs_l.meta_chunk_size = (dml_uint_t)(dml_log2((dml_float_t) meta_chunk_bytes) - 10);
	rq_regs->rq_regs_c.meta_chunk_size = (dml_uint_t)(dml_log2((dml_float_t) p1_meta_chunk_bytes) - 10);

	if (min_meta_chunk_bytes == 0)
		rq_regs->rq_regs_l.min_meta_chunk_size = 0;
	else
		rq_regs->rq_regs_l.min_meta_chunk_size = (dml_uint_t)(dml_log2((dml_float_t) min_meta_chunk_bytes) - 6 + 1);

	if (min_meta_chunk_bytes == 0)
		rq_regs->rq_regs_c.min_meta_chunk_size = 0;
	else
		rq_regs->rq_regs_c.min_meta_chunk_size = (dml_uint_t)(dml_log2((dml_float_t) p1_min_meta_chunk_bytes) - 6 + 1);

	rq_regs->rq_regs_l.dpte_group_size = (dml_uint_t)(dml_log2((dml_float_t) dpte_group_bytes) - 6);
	rq_regs->rq_regs_l.mpte_group_size = (dml_uint_t)(dml_log2((dml_float_t) mpte_group_bytes) - 6);
	rq_regs->rq_regs_c.dpte_group_size = (dml_uint_t)(dml_log2((dml_float_t) p1_dpte_group_bytes) - 6);
	rq_regs->rq_regs_c.mpte_group_size = (dml_uint_t)(dml_log2((dml_float_t) p1_mpte_group_bytes) - 6);

	detile_buf_size_in_bytes = (dml_uint_t)(dml_get_det_buffer_size_kbytes(mode_lib, pipe_idx) * 1024);

	pte_row_height_linear = (dml_uint_t)(dml_get_dpte_row_height_linear_l(mode_lib, pipe_idx));

	if (sw_mode == dml_sw_linear)
		ASSERT(pte_row_height_linear >= 8);

	rq_regs->rq_regs_l.pte_row_height_linear = (dml_uint_t)(dml_floor(dml_log2((dml_float_t) pte_row_height_linear), 1) - 3);

	if (dual_plane) {
		dml_uint_t p1_pte_row_height_linear = (dml_uint_t)(dml_get_dpte_row_height_linear_c(mode_lib, pipe_idx));
		if (sw_mode == dml_sw_linear)
			ASSERT(p1_pte_row_height_linear >= 8);

		rq_regs->rq_regs_c.pte_row_height_linear = (dml_uint_t)(dml_floor(dml_log2((dml_float_t) p1_pte_row_height_linear), 1) - 3);
	}

	rq_regs->rq_regs_l.swath_height = (dml_uint_t)(dml_log2((dml_float_t) dml_get_swath_height_l(mode_lib, pipe_idx)));
	rq_regs->rq_regs_c.swath_height = (dml_uint_t)(dml_log2((dml_float_t) dml_get_swath_height_c(mode_lib, pipe_idx)));

	if (pixel_chunk_bytes >= 32 * 1024 || (dual_plane && p1_pixel_chunk_bytes >= 32 * 1024)) { //32kb
		rq_regs->drq_expansion_mode = 0;
	} else {
		rq_regs->drq_expansion_mode = 2;
	}
	rq_regs->prq_expansion_mode = 1;
	rq_regs->mrq_expansion_mode = 1;
	rq_regs->crq_expansion_mode = 1;

	stored_swath_l_bytes = dml_get_det_stored_buffer_size_l_bytes(mode_lib, pipe_idx);
	stored_swath_c_bytes = dml_get_det_stored_buffer_size_c_bytes(mode_lib, pipe_idx);
	is_phantom_pipe		 = dml_get_is_phantom_pipe(mode_lib, pipe_idx);

	// Note: detile_buf_plane1_addr is in unit of 1KB
	if (dual_plane) {
		if (is_phantom_pipe) {
			detile_buf_plane1_addr = (dml_uint_t)((1024.0*1024.0) / 2.0 / 1024.0); // half to chroma
		} else {
			if (stored_swath_l_bytes / stored_swath_c_bytes <= 1.5) {
				detile_buf_plane1_addr = (dml_uint_t)(detile_buf_size_in_bytes / 2.0 / 1024.0); // half to chroma
#ifdef __DML_VBA_DEBUG__
				dml_print("DML_DLG: %s: detile_buf_plane1_addr = %d (1/2 to chroma)\n", __func__, detile_buf_plane1_addr);
#endif
			} else {
				detile_buf_plane1_addr = (dml_uint_t)(dml_round_to_multiple((dml_uint_t)((2.0 * detile_buf_size_in_bytes) / 3.0), 1024, 0) / 1024.0); // 2/3 to luma
#ifdef __DML_VBA_DEBUG__
				dml_print("DML_DLG: %s: detile_buf_plane1_addr = %d (1/3 chroma)\n", __func__, detile_buf_plane1_addr);
#endif
			}
		}
	}
	rq_regs->plane1_base_address = detile_buf_plane1_addr;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML_DLG: %s: is_phantom_pipe = %d\n", __func__, is_phantom_pipe);
	dml_print("DML_DLG: %s: stored_swath_l_bytes = %f\n", __func__, stored_swath_l_bytes);
	dml_print("DML_DLG: %s: stored_swath_c_bytes = %f\n", __func__, stored_swath_c_bytes);
	dml_print("DML_DLG: %s: detile_buf_size_in_bytes = %d\n", __func__, detile_buf_size_in_bytes);
	dml_print("DML_DLG: %s: detile_buf_plane1_addr = %d\n", __func__, detile_buf_plane1_addr);
	dml_print("DML_DLG: %s: plane1_base_address = %d\n", __func__, rq_regs->plane1_base_address);
#endif
	dml_print_rq_regs_st(rq_regs);
	dml_print("DML_DLG::%s: Calculation for pipe[%d] done\n", __func__, pipe_idx);
}

// Note: currently taken in as is.
// Nice to decouple code from hw register implement and extract code that are repeated for luma and chroma.


void dml_rq_dlg_get_dlg_reg(dml_display_dlg_regs_st		   *disp_dlg_regs,
	dml_display_ttu_regs_st		   *disp_ttu_regs,
							struct display_mode_lib_st *mode_lib,
							const dml_uint_t			pipe_idx)
{
	dml_uint_t					plane_idx		= dml_get_plane_idx(mode_lib, pipe_idx);
	enum dml_source_format_class	source_format	= mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[plane_idx];
	struct dml_timing_cfg_st		   *timing		   = &mode_lib->ms.cache_display_cfg.timing;
	struct dml_plane_cfg_st			   *plane		   = &mode_lib->ms.cache_display_cfg.plane;
	struct dml_hw_resource_st		   *hw			   = &mode_lib->ms.cache_display_cfg.hw;
	dml_bool_t dual_plane						= is_dual_plane(source_format);
	dml_uint_t num_cursors						= plane->NumberOfCursors[plane_idx];
	enum dml_odm_mode				odm_mode		= hw->ODMMode[plane_idx];

	dml_uint_t	  htotal				= timing->HTotal[plane_idx];
	dml_uint_t	  hactive				= timing->HActive[plane_idx];
	dml_uint_t	  hblank_end			= timing->HBlankEnd[plane_idx];
	dml_uint_t	  vblank_end			= timing->VBlankEnd[plane_idx];
	dml_bool_t	  interlaced			= timing->Interlace[plane_idx];
	dml_float_t   pclk_freq_in_mhz		= (dml_float_t) timing->PixelClock[plane_idx];
	dml_float_t   refclk_freq_in_mhz	= (hw->DLGRefClkFreqMHz > 0) ? (dml_float_t) hw->DLGRefClkFreqMHz : mode_lib->soc.refclk_mhz;
	dml_float_t   ref_freq_to_pix_freq	= refclk_freq_in_mhz / pclk_freq_in_mhz;

	dml_uint_t vready_after_vcount0;

	dml_uint_t dst_x_after_scaler;
	dml_uint_t dst_y_after_scaler;

	dml_float_t dst_y_prefetch;
	dml_float_t dst_y_per_vm_vblank;
	dml_float_t dst_y_per_row_vblank;
	dml_float_t dst_y_per_vm_flip;
	dml_float_t dst_y_per_row_flip;

	dml_float_t max_dst_y_per_vm_vblank = 32.0;		//U5.2
	dml_float_t max_dst_y_per_row_vblank = 16.0;	//U4.2

	dml_float_t vratio_pre_l;
	dml_float_t vratio_pre_c;

	dml_float_t refcyc_per_line_delivery_pre_l;
	dml_float_t refcyc_per_line_delivery_l;
	dml_float_t refcyc_per_line_delivery_pre_c = 0.;
	dml_float_t refcyc_per_line_delivery_c = 0.;
	dml_float_t refcyc_per_req_delivery_pre_l;
	dml_float_t refcyc_per_req_delivery_l;
	dml_float_t refcyc_per_req_delivery_pre_c = 0.;
	dml_float_t refcyc_per_req_delivery_c	  = 0.;
	dml_float_t refcyc_per_req_delivery_pre_cur0 = 0.;
	dml_float_t refcyc_per_req_delivery_cur0 = 0.;

	dml_float_t dst_y_per_pte_row_nom_l;
	dml_float_t dst_y_per_pte_row_nom_c;
	dml_float_t dst_y_per_meta_row_nom_l;
	dml_float_t dst_y_per_meta_row_nom_c;
	dml_float_t refcyc_per_pte_group_nom_l;
	dml_float_t refcyc_per_pte_group_nom_c;
	dml_float_t refcyc_per_pte_group_vblank_l;
	dml_float_t refcyc_per_pte_group_vblank_c;
	dml_float_t refcyc_per_pte_group_flip_l;
	dml_float_t refcyc_per_pte_group_flip_c;
	dml_float_t refcyc_per_meta_chunk_nom_l;
	dml_float_t refcyc_per_meta_chunk_nom_c;
	dml_float_t refcyc_per_meta_chunk_vblank_l;
	dml_float_t refcyc_per_meta_chunk_vblank_c;
	dml_float_t refcyc_per_meta_chunk_flip_l;
	dml_float_t refcyc_per_meta_chunk_flip_c;

	dml_float_t temp;
	dml_float_t min_ttu_vblank;
	dml_uint_t min_dst_y_next_start;

	dml_print("DML_DLG::%s: Calculation for pipe_idx=%d\n", __func__, pipe_idx);
	dml_print("DML_DLG::%s: plane_idx				= %d\n", __func__, plane_idx);
	dml_print("DML_DLG: %s: htotal					= %d\n", __func__, htotal);
	dml_print("DML_DLG: %s: refclk_freq_in_mhz		= %3.2f\n", __func__, refclk_freq_in_mhz);
	dml_print("DML_DLG: %s: hw->DLGRefClkFreqMHz	= %3.2f\n", __func__, hw->DLGRefClkFreqMHz);
	dml_print("DML_DLG: %s: soc.refclk_mhz			= %3.2f\n", __func__, mode_lib->soc.refclk_mhz);
	dml_print("DML_DLG: %s: pclk_freq_in_mhz		= %3.2f\n", __func__, pclk_freq_in_mhz);
	dml_print("DML_DLG: %s: ref_freq_to_pix_freq	= %3.2f\n", __func__, ref_freq_to_pix_freq);
	dml_print("DML_DLG: %s: interlaced				= %d\n", __func__, interlaced);

	memset(disp_dlg_regs, 0, sizeof(*disp_dlg_regs));
	memset(disp_ttu_regs, 0, sizeof(*disp_ttu_regs));

	ASSERT(refclk_freq_in_mhz != 0);
	ASSERT(pclk_freq_in_mhz != 0);
	ASSERT(ref_freq_to_pix_freq < 4.0);

	// Need to figure out which side of odm combine we're in
	// Assume the pipe instance under the same plane is in order

	if (odm_mode == dml_odm_mode_bypass) {
		disp_dlg_regs->refcyc_h_blank_end = (dml_uint_t)((dml_float_t) hblank_end * ref_freq_to_pix_freq);
	} else if (odm_mode == dml_odm_mode_combine_2to1 || odm_mode == dml_odm_mode_combine_4to1) {
		// find out how many pipe are in this plane
		dml_uint_t num_active_pipes			= dml_get_num_active_pipes(&mode_lib->ms.cache_display_cfg);
		dml_uint_t first_pipe_idx_in_plane	= __DML_NUM_PLANES__;
		dml_uint_t pipe_idx_in_combine		= 0; // pipe index within the plane
		dml_uint_t odm_combine_factor		= (odm_mode == dml_odm_mode_combine_2to1 ? 2 : 4);

		for (dml_uint_t i = 0; i < num_active_pipes; i++) {
			if (dml_get_plane_idx(mode_lib, i) == plane_idx) {
				if (i < first_pipe_idx_in_plane) {
					first_pipe_idx_in_plane = i;
				}
			}
		}
		pipe_idx_in_combine = pipe_idx - first_pipe_idx_in_plane; // DML assumes the pipes in the same plane will have continuous indexing (i.e. plane 0 use pipe 0, 1, and plane 1 uses pipe 2, 3, etc.)

		disp_dlg_regs->refcyc_h_blank_end = (dml_uint_t)(((dml_float_t) hblank_end + (dml_float_t) pipe_idx_in_combine * (dml_float_t) hactive / (dml_float_t) odm_combine_factor) * ref_freq_to_pix_freq);
		dml_print("DML_DLG: %s: pipe_idx = %d\n", __func__, pipe_idx);
		dml_print("DML_DLG: %s: first_pipe_idx_in_plane = %d\n", __func__, first_pipe_idx_in_plane);
		dml_print("DML_DLG: %s: pipe_idx_in_combine = %d\n", __func__, pipe_idx_in_combine);
		dml_print("DML_DLG: %s: odm_combine_factor = %d\n", __func__, odm_combine_factor);
	}
	dml_print("DML_DLG: %s: refcyc_h_blank_end = %d\n", __func__, disp_dlg_regs->refcyc_h_blank_end);

	ASSERT(disp_dlg_regs->refcyc_h_blank_end < (dml_uint_t)dml_pow(2, 13));

	disp_dlg_regs->ref_freq_to_pix_freq = (dml_uint_t)(ref_freq_to_pix_freq * dml_pow(2, 19));
	temp = dml_pow(2, 8);
	disp_dlg_regs->refcyc_per_htotal = (dml_uint_t)(ref_freq_to_pix_freq * (dml_float_t)htotal * temp);
	disp_dlg_regs->dlg_vblank_end = interlaced ? (vblank_end / 2) : vblank_end; // 15 bits

	min_ttu_vblank		= dml_get_min_ttu_vblank_in_us(mode_lib, pipe_idx);
	min_dst_y_next_start = (dml_uint_t)(dml_get_min_dst_y_next_start(mode_lib, pipe_idx));

	dml_print("DML_DLG: %s: min_ttu_vblank (us)    = %3.2f\n", __func__, min_ttu_vblank);
	dml_print("DML_DLG: %s: min_dst_y_next_start   = %d\n", __func__, min_dst_y_next_start);
	dml_print("DML_DLG: %s: ref_freq_to_pix_freq   = %3.2f\n", __func__, ref_freq_to_pix_freq);

	vready_after_vcount0		= (dml_uint_t)(dml_get_vready_at_or_after_vsync(mode_lib, pipe_idx));
	disp_dlg_regs->vready_after_vcount0 = vready_after_vcount0;

	dml_print("DML_DLG: %s: vready_after_vcount0 = %d\n", __func__, disp_dlg_regs->vready_after_vcount0);

	dst_x_after_scaler = (dml_uint_t)(dml_get_dst_x_after_scaler(mode_lib, pipe_idx));
	dst_y_after_scaler = (dml_uint_t)(dml_get_dst_y_after_scaler(mode_lib, pipe_idx));

	dml_print("DML_DLG: %s: dst_x_after_scaler	   = %d\n", __func__, dst_x_after_scaler);
	dml_print("DML_DLG: %s: dst_y_after_scaler	   = %d\n", __func__, dst_y_after_scaler);

	dst_y_prefetch			= dml_get_dst_y_prefetch(mode_lib, pipe_idx);
	dst_y_per_vm_vblank		= dml_get_dst_y_per_vm_vblank(mode_lib, pipe_idx);
	dst_y_per_row_vblank	= dml_get_dst_y_per_row_vblank(mode_lib, pipe_idx);
	dst_y_per_vm_flip		= dml_get_dst_y_per_vm_flip(mode_lib, pipe_idx);
	dst_y_per_row_flip		= dml_get_dst_y_per_row_flip(mode_lib, pipe_idx);

	// magic!
	if (htotal <= 75) {
		max_dst_y_per_vm_vblank = 100.0;
		max_dst_y_per_row_vblank = 100.0;
	}

	dml_print("DML_DLG: %s: dst_y_prefetch (after rnd) = %3.2f\n", __func__, dst_y_prefetch);
	dml_print("DML_DLG: %s: dst_y_per_vm_flip	 = %3.2f\n", __func__, dst_y_per_vm_flip);
	dml_print("DML_DLG: %s: dst_y_per_row_flip	 = %3.2f\n", __func__, dst_y_per_row_flip);
	dml_print("DML_DLG: %s: dst_y_per_vm_vblank  = %3.2f\n", __func__, dst_y_per_vm_vblank);
	dml_print("DML_DLG: %s: dst_y_per_row_vblank = %3.2f\n", __func__, dst_y_per_row_vblank);

	ASSERT(dst_y_per_vm_vblank < max_dst_y_per_vm_vblank);
	ASSERT(dst_y_per_row_vblank < max_dst_y_per_row_vblank);
	ASSERT(dst_y_prefetch > (dst_y_per_vm_vblank + dst_y_per_row_vblank));

	vratio_pre_l = dml_get_vratio_prefetch_l(mode_lib, pipe_idx);
	vratio_pre_c = dml_get_vratio_prefetch_c(mode_lib, pipe_idx);

	dml_print("DML_DLG: %s: vratio_pre_l = %3.2f\n", __func__, vratio_pre_l);
	dml_print("DML_DLG: %s: vratio_pre_c = %3.2f\n", __func__, vratio_pre_c);

	// Active
	refcyc_per_line_delivery_pre_l = dml_get_refcyc_per_line_delivery_pre_l_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_line_delivery_l	   = dml_get_refcyc_per_line_delivery_l_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;

	dml_print("DML_DLG: %s: refcyc_per_line_delivery_pre_l = %3.2f\n", __func__, refcyc_per_line_delivery_pre_l);
	dml_print("DML_DLG: %s: refcyc_per_line_delivery_l	   = %3.2f\n", __func__, refcyc_per_line_delivery_l);

	if (dual_plane) {
		refcyc_per_line_delivery_pre_c = dml_get_refcyc_per_line_delivery_pre_c_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
		refcyc_per_line_delivery_c	   = dml_get_refcyc_per_line_delivery_c_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;

		dml_print("DML_DLG: %s: refcyc_per_line_delivery_pre_c = %3.2f\n", __func__, refcyc_per_line_delivery_pre_c);
		dml_print("DML_DLG: %s: refcyc_per_line_delivery_c	   = %3.2f\n", __func__, refcyc_per_line_delivery_c);
	}

	disp_dlg_regs->refcyc_per_vm_dmdata = (dml_uint_t)(dml_get_refcyc_per_vm_dmdata_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz);
	disp_dlg_regs->dmdata_dl_delta = (dml_uint_t)(dml_get_dmdata_dl_delta_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz);

	refcyc_per_req_delivery_pre_l = dml_get_refcyc_per_req_delivery_pre_l_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_req_delivery_l	  = dml_get_refcyc_per_req_delivery_l_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;

	dml_print("DML_DLG: %s: refcyc_per_req_delivery_pre_l = %3.2f\n", __func__, refcyc_per_req_delivery_pre_l);
	dml_print("DML_DLG: %s: refcyc_per_req_delivery_l	  = %3.2f\n", __func__, refcyc_per_req_delivery_l);

	if (dual_plane) {
		refcyc_per_req_delivery_pre_c = dml_get_refcyc_per_req_delivery_pre_c_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
		refcyc_per_req_delivery_c	  = dml_get_refcyc_per_req_delivery_c_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;

		dml_print("DML_DLG: %s: refcyc_per_req_delivery_pre_c = %3.2f\n", __func__, refcyc_per_req_delivery_pre_c);
		dml_print("DML_DLG: %s: refcyc_per_req_delivery_c	  = %3.2f\n", __func__, refcyc_per_req_delivery_c);
	}

	// TTU - Cursor
	ASSERT(num_cursors <= 1);
	if (num_cursors > 0) {
		refcyc_per_req_delivery_pre_cur0 = dml_get_refcyc_per_cursor_req_delivery_pre_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
		refcyc_per_req_delivery_cur0	 = dml_get_refcyc_per_cursor_req_delivery_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;

		dml_print("DML_DLG: %s: refcyc_per_req_delivery_pre_cur0 = %3.2f\n", __func__, refcyc_per_req_delivery_pre_cur0);
		dml_print("DML_DLG: %s: refcyc_per_req_delivery_cur0	 = %3.2f\n", __func__, refcyc_per_req_delivery_cur0);
	}

	// Assign to register structures
	disp_dlg_regs->min_dst_y_next_start = (dml_uint_t)((dml_float_t) min_dst_y_next_start * dml_pow(2, 2));
	ASSERT(disp_dlg_regs->min_dst_y_next_start < (dml_uint_t)dml_pow(2, 18));

	disp_dlg_regs->dst_y_after_scaler = dst_y_after_scaler; // in terms of line
	disp_dlg_regs->refcyc_x_after_scaler = (dml_uint_t)((dml_float_t) dst_x_after_scaler * ref_freq_to_pix_freq); // in terms of refclk
	disp_dlg_regs->dst_y_prefetch			= (dml_uint_t)(dst_y_prefetch * dml_pow(2, 2));
	disp_dlg_regs->dst_y_per_vm_vblank		= (dml_uint_t)(dst_y_per_vm_vblank * dml_pow(2, 2));
	disp_dlg_regs->dst_y_per_row_vblank		= (dml_uint_t)(dst_y_per_row_vblank * dml_pow(2, 2));
	disp_dlg_regs->dst_y_per_vm_flip		= (dml_uint_t)(dst_y_per_vm_flip * dml_pow(2, 2));
	disp_dlg_regs->dst_y_per_row_flip		= (dml_uint_t)(dst_y_per_row_flip * dml_pow(2, 2));

	disp_dlg_regs->vratio_prefetch = (dml_uint_t)(vratio_pre_l * dml_pow(2, 19));
	disp_dlg_regs->vratio_prefetch_c = (dml_uint_t)(vratio_pre_c * dml_pow(2, 19));

	dml_print("DML_DLG: %s: disp_dlg_regs->dst_y_per_vm_vblank	= 0x%x\n", __func__, disp_dlg_regs->dst_y_per_vm_vblank);
	dml_print("DML_DLG: %s: disp_dlg_regs->dst_y_per_row_vblank = 0x%x\n", __func__, disp_dlg_regs->dst_y_per_row_vblank);
	dml_print("DML_DLG: %s: disp_dlg_regs->dst_y_per_vm_flip	= 0x%x\n", __func__, disp_dlg_regs->dst_y_per_vm_flip);
	dml_print("DML_DLG: %s: disp_dlg_regs->dst_y_per_row_flip	= 0x%x\n", __func__, disp_dlg_regs->dst_y_per_row_flip);

	// hack for FPGA
	/* NOTE: We dont have getenv defined in driver and it does not make any sense in the driver */
	/*char* fpga_env = getenv("FPGA_FPDIV");
	if(fpga_env !=NULL)
	{
		if(disp_dlg_regs->vratio_prefetch >= (dml_uint_t)dml_pow(2, 22))
		{
			disp_dlg_regs->vratio_prefetch = (dml_uint_t)dml_pow(2, 22)-1;
			dml_print("FPGA msg: vratio_prefetch exceed the max value, the register field is [21:0]\n");
		}
	}*/

	disp_dlg_regs->refcyc_per_vm_group_vblank		= (dml_uint_t)(dml_get_refcyc_per_vm_group_vblank_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz);
	disp_dlg_regs->refcyc_per_vm_group_flip			= (dml_uint_t)(dml_get_refcyc_per_vm_group_flip_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz);
	disp_dlg_regs->refcyc_per_vm_req_vblank			= (dml_uint_t)(dml_get_refcyc_per_vm_req_vblank_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz * dml_pow(2, 10));
	disp_dlg_regs->refcyc_per_vm_req_flip			= (dml_uint_t)(dml_get_refcyc_per_vm_req_flip_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz * dml_pow(2, 10));

	dst_y_per_pte_row_nom_l = dml_get_dst_y_per_pte_row_nom_l(mode_lib, pipe_idx);
	dst_y_per_pte_row_nom_c = dml_get_dst_y_per_pte_row_nom_c(mode_lib, pipe_idx);
	dst_y_per_meta_row_nom_l = dml_get_dst_y_per_meta_row_nom_l(mode_lib, pipe_idx);
	dst_y_per_meta_row_nom_c = dml_get_dst_y_per_meta_row_nom_c(mode_lib, pipe_idx);

	refcyc_per_pte_group_nom_l		= dml_get_refcyc_per_pte_group_nom_l_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_pte_group_nom_c		= dml_get_refcyc_per_pte_group_nom_c_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_pte_group_vblank_l	= dml_get_refcyc_per_pte_group_vblank_l_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_pte_group_vblank_c	= dml_get_refcyc_per_pte_group_vblank_c_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_pte_group_flip_l		= dml_get_refcyc_per_pte_group_flip_l_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_pte_group_flip_c		= dml_get_refcyc_per_pte_group_flip_c_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;

	refcyc_per_meta_chunk_nom_l		= dml_get_refcyc_per_meta_chunk_nom_l_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_meta_chunk_nom_c		= dml_get_refcyc_per_meta_chunk_nom_c_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_meta_chunk_vblank_l	= dml_get_refcyc_per_meta_chunk_vblank_l_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_meta_chunk_vblank_c	= dml_get_refcyc_per_meta_chunk_vblank_c_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_meta_chunk_flip_l	= dml_get_refcyc_per_meta_chunk_flip_l_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;
	refcyc_per_meta_chunk_flip_c	= dml_get_refcyc_per_meta_chunk_flip_c_in_us(mode_lib, pipe_idx) * refclk_freq_in_mhz;

	disp_dlg_regs->dst_y_per_pte_row_nom_l			= (dml_uint_t)(dst_y_per_pte_row_nom_l * dml_pow(2, 2));
	disp_dlg_regs->dst_y_per_pte_row_nom_c			= (dml_uint_t)(dst_y_per_pte_row_nom_c * dml_pow(2, 2));
	disp_dlg_regs->dst_y_per_meta_row_nom_l			= (dml_uint_t)(dst_y_per_meta_row_nom_l * dml_pow(2, 2));
	disp_dlg_regs->dst_y_per_meta_row_nom_c			= (dml_uint_t)(dst_y_per_meta_row_nom_c * dml_pow(2, 2));
	disp_dlg_regs->refcyc_per_pte_group_nom_l		= (dml_uint_t)(refcyc_per_pte_group_nom_l);
	disp_dlg_regs->refcyc_per_pte_group_nom_c		= (dml_uint_t)(refcyc_per_pte_group_nom_c);
	disp_dlg_regs->refcyc_per_pte_group_vblank_l	= (dml_uint_t)(refcyc_per_pte_group_vblank_l);
	disp_dlg_regs->refcyc_per_pte_group_vblank_c	= (dml_uint_t)(refcyc_per_pte_group_vblank_c);
	disp_dlg_regs->refcyc_per_pte_group_flip_l		= (dml_uint_t)(refcyc_per_pte_group_flip_l);
	disp_dlg_regs->refcyc_per_pte_group_flip_c		= (dml_uint_t)(refcyc_per_pte_group_flip_c);
	disp_dlg_regs->refcyc_per_meta_chunk_nom_l		= (dml_uint_t)(refcyc_per_meta_chunk_nom_l);
	disp_dlg_regs->refcyc_per_meta_chunk_nom_c		= (dml_uint_t)(refcyc_per_meta_chunk_nom_c);
	disp_dlg_regs->refcyc_per_meta_chunk_vblank_l	= (dml_uint_t)(refcyc_per_meta_chunk_vblank_l);
	disp_dlg_regs->refcyc_per_meta_chunk_vblank_c	= (dml_uint_t)(refcyc_per_meta_chunk_vblank_c);
	disp_dlg_regs->refcyc_per_meta_chunk_flip_l		= (dml_uint_t)(refcyc_per_meta_chunk_flip_l);
	disp_dlg_regs->refcyc_per_meta_chunk_flip_c		= (dml_uint_t)(refcyc_per_meta_chunk_flip_c);
	disp_dlg_regs->refcyc_per_line_delivery_pre_l	= (dml_uint_t)dml_floor(refcyc_per_line_delivery_pre_l, 1);
	disp_dlg_regs->refcyc_per_line_delivery_l		= (dml_uint_t)dml_floor(refcyc_per_line_delivery_l, 1);
	disp_dlg_regs->refcyc_per_line_delivery_pre_c	= (dml_uint_t)dml_floor(refcyc_per_line_delivery_pre_c, 1);
	disp_dlg_regs->refcyc_per_line_delivery_c		= (dml_uint_t)dml_floor(refcyc_per_line_delivery_c, 1);

	disp_dlg_regs->chunk_hdl_adjust_cur0	= 3;
	disp_dlg_regs->dst_y_offset_cur0		= 0;
	disp_dlg_regs->chunk_hdl_adjust_cur1	= 3;
	disp_dlg_regs->dst_y_offset_cur1		= 0;

	disp_dlg_regs->dst_y_delta_drq_limit = 0x7fff; // off

	disp_ttu_regs->refcyc_per_req_delivery_pre_l	= (dml_uint_t)(refcyc_per_req_delivery_pre_l	  * dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_l		= (dml_uint_t)(refcyc_per_req_delivery_l		  * dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_pre_c	= (dml_uint_t)(refcyc_per_req_delivery_pre_c	  * dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_c		= (dml_uint_t)(refcyc_per_req_delivery_c		  * dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_pre_cur0 = (dml_uint_t)(refcyc_per_req_delivery_pre_cur0   * dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_cur0		= (dml_uint_t)(refcyc_per_req_delivery_cur0		  * dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_pre_cur1 = 0;
	disp_ttu_regs->refcyc_per_req_delivery_cur1		= 0;
	disp_ttu_regs->qos_level_low_wm = 0;

	disp_ttu_regs->qos_level_high_wm = (dml_uint_t)(4.0 * (dml_float_t)htotal * ref_freq_to_pix_freq);

	disp_ttu_regs->qos_level_flip = 14;
	disp_ttu_regs->qos_level_fixed_l = 8;
	disp_ttu_regs->qos_level_fixed_c = 8;
	disp_ttu_regs->qos_level_fixed_cur0 = 8;
	disp_ttu_regs->qos_ramp_disable_l = 0;
	disp_ttu_regs->qos_ramp_disable_c = 0;
	disp_ttu_regs->qos_ramp_disable_cur0 = 0;
	disp_ttu_regs->min_ttu_vblank = (dml_uint_t)(min_ttu_vblank * refclk_freq_in_mhz);

	// CHECK for HW registers' range, assert or clamp
	ASSERT(refcyc_per_req_delivery_pre_l < dml_pow(2, 13));
	ASSERT(refcyc_per_req_delivery_l < dml_pow(2, 13));
	ASSERT(refcyc_per_req_delivery_pre_c < dml_pow(2, 13));
	ASSERT(refcyc_per_req_delivery_c < dml_pow(2, 13));
	if (disp_dlg_regs->refcyc_per_vm_group_vblank >= (dml_uint_t)dml_pow(2, 23))
		disp_dlg_regs->refcyc_per_vm_group_vblank = (dml_uint_t)(dml_pow(2, 23) - 1);

	if (disp_dlg_regs->refcyc_per_vm_group_flip >= (dml_uint_t)dml_pow(2, 23))
		disp_dlg_regs->refcyc_per_vm_group_flip = (dml_uint_t)(dml_pow(2, 23) - 1);

	if (disp_dlg_regs->refcyc_per_vm_req_vblank >= (dml_uint_t)dml_pow(2, 23))
		disp_dlg_regs->refcyc_per_vm_req_vblank = (dml_uint_t)(dml_pow(2, 23) - 1);

	if (disp_dlg_regs->refcyc_per_vm_req_flip >= (dml_uint_t)dml_pow(2, 23))
		disp_dlg_regs->refcyc_per_vm_req_flip = (dml_uint_t)(dml_pow(2, 23) - 1);


	ASSERT(disp_dlg_regs->dst_y_after_scaler < (dml_uint_t)8);
	ASSERT(disp_dlg_regs->refcyc_x_after_scaler < (dml_uint_t)dml_pow(2, 13));
	ASSERT(disp_dlg_regs->dst_y_per_pte_row_nom_l < (dml_uint_t)dml_pow(2, 17));
	if (dual_plane) {
		if (disp_dlg_regs->dst_y_per_pte_row_nom_c >= (dml_uint_t)dml_pow(2, 17)) { // FIXME what so special about chroma, can we just assert?
			dml_print("DML_DLG: %s: Warning dst_y_per_pte_row_nom_c %u > register max U15.2 %u\n", __func__, disp_dlg_regs->dst_y_per_pte_row_nom_c, (dml_uint_t)dml_pow(2, 17) - 1);
		}
	}
	ASSERT(disp_dlg_regs->dst_y_per_meta_row_nom_l < (dml_uint_t)dml_pow(2, 17));
	ASSERT(disp_dlg_regs->dst_y_per_meta_row_nom_c < (dml_uint_t)dml_pow(2, 17));

	if (disp_dlg_regs->refcyc_per_pte_group_nom_l >= (dml_uint_t)dml_pow(2, 23))
		disp_dlg_regs->refcyc_per_pte_group_nom_l = (dml_uint_t)(dml_pow(2, 23) - 1);
	if (dual_plane) {
		if (disp_dlg_regs->refcyc_per_pte_group_nom_c >= (dml_uint_t)dml_pow(2, 23))
			disp_dlg_regs->refcyc_per_pte_group_nom_c = (dml_uint_t)(dml_pow(2, 23) - 1);
	}
	ASSERT(disp_dlg_regs->refcyc_per_pte_group_vblank_l < (dml_uint_t)dml_pow(2, 13));
	if (dual_plane) {
		ASSERT(disp_dlg_regs->refcyc_per_pte_group_vblank_c < (dml_uint_t)dml_pow(2, 13));
	}

	if (disp_dlg_regs->refcyc_per_meta_chunk_nom_l >= (dml_uint_t)dml_pow(2, 23))
		disp_dlg_regs->refcyc_per_meta_chunk_nom_l = (dml_uint_t)(dml_pow(2, 23) - 1);
	if (dual_plane) {
		if (disp_dlg_regs->refcyc_per_meta_chunk_nom_c >= (dml_uint_t)dml_pow(2, 23))
			disp_dlg_regs->refcyc_per_meta_chunk_nom_c = (dml_uint_t)(dml_pow(2, 23) - 1);
	}
	ASSERT(disp_dlg_regs->refcyc_per_meta_chunk_vblank_l	< (dml_uint_t)dml_pow(2, 13));
	ASSERT(disp_dlg_regs->refcyc_per_meta_chunk_vblank_c	< (dml_uint_t)dml_pow(2, 13));
	ASSERT(disp_dlg_regs->refcyc_per_line_delivery_pre_l	< (dml_uint_t)dml_pow(2, 13));
	ASSERT(disp_dlg_regs->refcyc_per_line_delivery_l		< (dml_uint_t)dml_pow(2, 13));
	ASSERT(disp_dlg_regs->refcyc_per_line_delivery_pre_c	< (dml_uint_t)dml_pow(2, 13));
	ASSERT(disp_dlg_regs->refcyc_per_line_delivery_c		< (dml_uint_t)dml_pow(2, 13));
	ASSERT(disp_ttu_regs->qos_level_low_wm					< (dml_uint_t) dml_pow(2, 14));
	ASSERT(disp_ttu_regs->qos_level_high_wm					< (dml_uint_t) dml_pow(2, 14));
	ASSERT(disp_ttu_regs->min_ttu_vblank					< (dml_uint_t) dml_pow(2, 24));

	dml_print_ttu_regs_st(disp_ttu_regs);
	dml_print_dlg_regs_st(disp_dlg_regs);
	dml_print("DML_DLG::%s: Calculation for pipe[%d] done\n", __func__, pipe_idx);
}

void dml_rq_dlg_get_arb_params(struct display_mode_lib_st *mode_lib, dml_display_arb_params_st *arb_param)
{
	memset(arb_param, 0, sizeof(*arb_param));
	arb_param->max_req_outstanding = 256;
	arb_param->min_req_outstanding = 256; // turn off the sat level feature if this set to max
	arb_param->sat_level_us = 60;
	arb_param->hvm_max_qos_commit_threshold = 0xf;
	arb_param->hvm_min_req_outstand_commit_threshold = 0xa;
	arb_param->compbuf_reserved_space_kbytes = 2 * 8;  // assume max data chunk size of 8K
}
