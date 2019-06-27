/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include "../display_mode_lib.h"
#include "../display_mode_vba.h"
#include "display_rq_dlg_calc_20.h"

// Function: dml20_rq_dlg_get_rq_params
//  Calculate requestor related parameters that register definition agnostic
//  (i.e. this layer does try to separate real values from register definition)
// Input:
//  pipe_src_param - pipe source configuration (e.g. vp, pitch, etc.)
// Output:
//  rq_param - values that can be used to setup RQ (e.g. swath_height, plane1_addr, etc.)
//
static void dml20_rq_dlg_get_rq_params(
		struct display_mode_lib *mode_lib,
		display_rq_params_st * rq_param,
		const display_pipe_source_params_st pipe_src_param);

// Function: dml20_rq_dlg_get_dlg_params
//  Calculate deadline related parameters
//
static void dml20_rq_dlg_get_dlg_params(struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *e2e_pipe_param,
		const unsigned int num_pipes,
		const unsigned int pipe_idx,
		display_dlg_regs_st *disp_dlg_regs,
		display_ttu_regs_st *disp_ttu_regs,
		const display_rq_dlg_params_st rq_dlg_param,
		const display_dlg_sys_params_st dlg_sys_param,
		const bool cstate_en,
		const bool pstate_en);
/*
 * NOTE:
 *   This file is gcc-parseable HW gospel, coming straight from HW engineers.
 *
 * It doesn't adhere to Linux kernel style and sometimes will do things in odd
 * ways. Unless there is something clearly wrong with it the code should
 * remain as-is as it provides us with a guarantee from HW that it is correct.
 */

static void calculate_ttu_cursor(struct display_mode_lib *mode_lib,
		double *refcyc_per_req_delivery_pre_cur,
		double *refcyc_per_req_delivery_cur,
		double refclk_freq_in_mhz,
		double ref_freq_to_pix_freq,
		double hscale_pixel_rate_l,
		double hscl_ratio,
		double vratio_pre_l,
		double vratio_l,
		unsigned int cur_width,
		enum cursor_bpp cur_bpp);

#include "../dml_inline_defs.h"

static unsigned int get_bytes_per_element(enum source_format_class source_format, bool is_chroma)
{
	unsigned int ret_val = 0;

	if (source_format == dm_444_16) {
		if (!is_chroma)
			ret_val = 2;
	} else if (source_format == dm_444_32) {
		if (!is_chroma)
			ret_val = 4;
	} else if (source_format == dm_444_64) {
		if (!is_chroma)
			ret_val = 8;
	} else if (source_format == dm_420_8) {
		if (is_chroma)
			ret_val = 2;
		else
			ret_val = 1;
	} else if (source_format == dm_420_10) {
		if (is_chroma)
			ret_val = 4;
		else
			ret_val = 2;
	} else if (source_format == dm_444_8) {
		ret_val = 1;
	}
	return ret_val;
}

static bool is_dual_plane(enum source_format_class source_format)
{
	bool ret_val = 0;

	if ((source_format == dm_420_8) || (source_format == dm_420_10))
		ret_val = 1;

	return ret_val;
}

static double get_refcyc_per_delivery(struct display_mode_lib *mode_lib,
		double refclk_freq_in_mhz,
		double pclk_freq_in_mhz,
		bool odm_combine,
		unsigned int recout_width,
		unsigned int hactive,
		double vratio,
		double hscale_pixel_rate,
		unsigned int delivery_width,
		unsigned int req_per_swath_ub)
{
	double refcyc_per_delivery = 0.0;

	if (vratio <= 1.0) {
		if (odm_combine)
			refcyc_per_delivery = (double) refclk_freq_in_mhz
					* dml_min((double) recout_width, (double) hactive / 2.0)
					/ pclk_freq_in_mhz / (double) req_per_swath_ub;
		else
			refcyc_per_delivery = (double) refclk_freq_in_mhz * (double) recout_width
					/ pclk_freq_in_mhz / (double) req_per_swath_ub;
	} else {
		refcyc_per_delivery = (double) refclk_freq_in_mhz * (double) delivery_width
				/ (double) hscale_pixel_rate / (double) req_per_swath_ub;
	}

	dml_print("DML_DLG: %s: refclk_freq_in_mhz = %3.2f\n", __func__, refclk_freq_in_mhz);
	dml_print("DML_DLG: %s: pclk_freq_in_mhz   = %3.2f\n", __func__, pclk_freq_in_mhz);
	dml_print("DML_DLG: %s: recout_width       = %d\n", __func__, recout_width);
	dml_print("DML_DLG: %s: vratio             = %3.2f\n", __func__, vratio);
	dml_print("DML_DLG: %s: req_per_swath_ub   = %d\n", __func__, req_per_swath_ub);
	dml_print("DML_DLG: %s: refcyc_per_delivery= %3.2f\n", __func__, refcyc_per_delivery);

	return refcyc_per_delivery;

}

static unsigned int get_blk_size_bytes(const enum source_macro_tile_size tile_size)
{
	if (tile_size == dm_256k_tile)
		return (256 * 1024);
	else if (tile_size == dm_64k_tile)
		return (64 * 1024);
	else
		return (4 * 1024);
}

static void extract_rq_sizing_regs(struct display_mode_lib *mode_lib,
		display_data_rq_regs_st *rq_regs,
		const display_data_rq_sizing_params_st rq_sizing)
{
	dml_print("DML_DLG: %s: rq_sizing param\n", __func__);
	print__data_rq_sizing_params_st(mode_lib, rq_sizing);

	rq_regs->chunk_size = dml_log2(rq_sizing.chunk_bytes) - 10;

	if (rq_sizing.min_chunk_bytes == 0)
		rq_regs->min_chunk_size = 0;
	else
		rq_regs->min_chunk_size = dml_log2(rq_sizing.min_chunk_bytes) - 8 + 1;

	rq_regs->meta_chunk_size = dml_log2(rq_sizing.meta_chunk_bytes) - 10;
	if (rq_sizing.min_meta_chunk_bytes == 0)
		rq_regs->min_meta_chunk_size = 0;
	else
		rq_regs->min_meta_chunk_size = dml_log2(rq_sizing.min_meta_chunk_bytes) - 6 + 1;

	rq_regs->dpte_group_size = dml_log2(rq_sizing.dpte_group_bytes) - 6;
	rq_regs->mpte_group_size = dml_log2(rq_sizing.mpte_group_bytes) - 6;
}

static void extract_rq_regs(struct display_mode_lib *mode_lib,
		display_rq_regs_st *rq_regs,
		const display_rq_params_st rq_param)
{
	unsigned int detile_buf_size_in_bytes = mode_lib->ip.det_buffer_size_kbytes * 1024;
	unsigned int detile_buf_plane1_addr = 0;

	extract_rq_sizing_regs(mode_lib, &(rq_regs->rq_regs_l), rq_param.sizing.rq_l);

	rq_regs->rq_regs_l.pte_row_height_linear = dml_floor(dml_log2(rq_param.dlg.rq_l.dpte_row_height),
			1) - 3;

	if (rq_param.yuv420) {
		extract_rq_sizing_regs(mode_lib, &(rq_regs->rq_regs_c), rq_param.sizing.rq_c);
		rq_regs->rq_regs_c.pte_row_height_linear = dml_floor(dml_log2(rq_param.dlg.rq_c.dpte_row_height),
				1) - 3;
	}

	rq_regs->rq_regs_l.swath_height = dml_log2(rq_param.dlg.rq_l.swath_height);
	rq_regs->rq_regs_c.swath_height = dml_log2(rq_param.dlg.rq_c.swath_height);

	// FIXME: take the max between luma, chroma chunk size?
	// okay for now, as we are setting chunk_bytes to 8kb anyways
	if (rq_param.sizing.rq_l.chunk_bytes >= 32 * 1024) { //32kb
		rq_regs->drq_expansion_mode = 0;
	} else {
		rq_regs->drq_expansion_mode = 2;
	}
	rq_regs->prq_expansion_mode = 1;
	rq_regs->mrq_expansion_mode = 1;
	rq_regs->crq_expansion_mode = 1;

	if (rq_param.yuv420) {
		if ((double) rq_param.misc.rq_l.stored_swath_bytes
				/ (double) rq_param.misc.rq_c.stored_swath_bytes <= 1.5) {
			detile_buf_plane1_addr = (detile_buf_size_in_bytes / 2.0 / 64.0); // half to chroma
		} else {
			detile_buf_plane1_addr = dml_round_to_multiple((unsigned int) ((2.0 * detile_buf_size_in_bytes) / 3.0),
					256,
					0) / 64.0; // 2/3 to chroma
		}
	}
	rq_regs->plane1_base_address = detile_buf_plane1_addr;
}

static void handle_det_buf_split(struct display_mode_lib *mode_lib,
		display_rq_params_st *rq_param,
		const display_pipe_source_params_st pipe_src_param)
{
	unsigned int total_swath_bytes = 0;
	unsigned int swath_bytes_l = 0;
	unsigned int swath_bytes_c = 0;
	unsigned int full_swath_bytes_packed_l = 0;
	unsigned int full_swath_bytes_packed_c = 0;
	bool req128_l = 0;
	bool req128_c = 0;
	bool surf_linear = (pipe_src_param.sw_mode == dm_sw_linear);
	bool surf_vert = (pipe_src_param.source_scan == dm_vert);
	unsigned int log2_swath_height_l = 0;
	unsigned int log2_swath_height_c = 0;
	unsigned int detile_buf_size_in_bytes = mode_lib->ip.det_buffer_size_kbytes * 1024;

	full_swath_bytes_packed_l = rq_param->misc.rq_l.full_swath_bytes;
	full_swath_bytes_packed_c = rq_param->misc.rq_c.full_swath_bytes;

	if (rq_param->yuv420_10bpc) {
		full_swath_bytes_packed_l = dml_round_to_multiple(rq_param->misc.rq_l.full_swath_bytes * 2 / 3,
				256,
				1) + 256;
		full_swath_bytes_packed_c = dml_round_to_multiple(rq_param->misc.rq_c.full_swath_bytes * 2 / 3,
				256,
				1) + 256;
	}

	if (rq_param->yuv420) {
		total_swath_bytes = 2 * full_swath_bytes_packed_l + 2 * full_swath_bytes_packed_c;

		if (total_swath_bytes <= detile_buf_size_in_bytes) { //full 256b request
			req128_l = 0;
			req128_c = 0;
			swath_bytes_l = full_swath_bytes_packed_l;
			swath_bytes_c = full_swath_bytes_packed_c;
		} else { //128b request (for luma only for yuv420 8bpc)
			req128_l = 1;
			req128_c = 0;
			swath_bytes_l = full_swath_bytes_packed_l / 2;
			swath_bytes_c = full_swath_bytes_packed_c;
		}
		// Note: assumption, the config that pass in will fit into
		//       the detiled buffer.
	} else {
		total_swath_bytes = 2 * full_swath_bytes_packed_l;

		if (total_swath_bytes <= detile_buf_size_in_bytes)
			req128_l = 0;
		else
			req128_l = 1;

		swath_bytes_l = total_swath_bytes;
		swath_bytes_c = 0;
	}
	rq_param->misc.rq_l.stored_swath_bytes = swath_bytes_l;
	rq_param->misc.rq_c.stored_swath_bytes = swath_bytes_c;

	if (surf_linear) {
		log2_swath_height_l = 0;
		log2_swath_height_c = 0;
	} else if (!surf_vert) {
		log2_swath_height_l = dml_log2(rq_param->misc.rq_l.blk256_height) - req128_l;
		log2_swath_height_c = dml_log2(rq_param->misc.rq_c.blk256_height) - req128_c;
	} else {
		log2_swath_height_l = dml_log2(rq_param->misc.rq_l.blk256_width) - req128_l;
		log2_swath_height_c = dml_log2(rq_param->misc.rq_c.blk256_width) - req128_c;
	}
	rq_param->dlg.rq_l.swath_height = 1 << log2_swath_height_l;
	rq_param->dlg.rq_c.swath_height = 1 << log2_swath_height_c;

	dml_print("DML_DLG: %s: req128_l = %0d\n", __func__, req128_l);
	dml_print("DML_DLG: %s: req128_c = %0d\n", __func__, req128_c);
	dml_print("DML_DLG: %s: full_swath_bytes_packed_l = %0d\n",
			__func__,
			full_swath_bytes_packed_l);
	dml_print("DML_DLG: %s: full_swath_bytes_packed_c = %0d\n",
			__func__,
			full_swath_bytes_packed_c);
}

static void get_meta_and_pte_attr(struct display_mode_lib *mode_lib,
		display_data_rq_dlg_params_st *rq_dlg_param,
		display_data_rq_misc_params_st *rq_misc_param,
		display_data_rq_sizing_params_st *rq_sizing_param,
		unsigned int vp_width,
		unsigned int vp_height,
		unsigned int data_pitch,
		unsigned int meta_pitch,
		unsigned int source_format,
		unsigned int tiling,
		unsigned int macro_tile_size,
		unsigned int source_scan,
		unsigned int is_chroma)
{
	bool surf_linear = (tiling == dm_sw_linear);
	bool surf_vert = (source_scan == dm_vert);

	unsigned int bytes_per_element;
	unsigned int bytes_per_element_y = get_bytes_per_element((enum source_format_class)(source_format),
			false);
	unsigned int bytes_per_element_c = get_bytes_per_element((enum source_format_class)(source_format),
			true);

	unsigned int blk256_width = 0;
	unsigned int blk256_height = 0;

	unsigned int blk256_width_y = 0;
	unsigned int blk256_height_y = 0;
	unsigned int blk256_width_c = 0;
	unsigned int blk256_height_c = 0;
	unsigned int log2_bytes_per_element;
	unsigned int log2_blk256_width;
	unsigned int log2_blk256_height;
	unsigned int blk_bytes;
	unsigned int log2_blk_bytes;
	unsigned int log2_blk_height;
	unsigned int log2_blk_width;
	unsigned int log2_meta_req_bytes;
	unsigned int log2_meta_req_height;
	unsigned int log2_meta_req_width;
	unsigned int meta_req_width;
	unsigned int meta_req_height;
	unsigned int log2_meta_row_height;
	unsigned int meta_row_width_ub;
	unsigned int log2_meta_chunk_bytes;
	unsigned int log2_meta_chunk_height;

	//full sized meta chunk width in unit of data elements
	unsigned int log2_meta_chunk_width;
	unsigned int log2_min_meta_chunk_bytes;
	unsigned int min_meta_chunk_width;
	unsigned int meta_chunk_width;
	unsigned int meta_chunk_per_row_int;
	unsigned int meta_row_remainder;
	unsigned int meta_chunk_threshold;
	unsigned int meta_blk_bytes;
	unsigned int meta_blk_height;
	unsigned int meta_blk_width;
	unsigned int meta_surface_bytes;
	unsigned int vmpg_bytes;
	unsigned int meta_pte_req_per_frame_ub;
	unsigned int meta_pte_bytes_per_frame_ub;
	const unsigned int log2_vmpg_bytes = dml_log2(mode_lib->soc.vmm_page_size_bytes);
	const unsigned int dpte_buf_in_pte_reqs = mode_lib->ip.dpte_buffer_size_in_pte_reqs_luma;
	const unsigned int pde_proc_buffer_size_64k_reqs =
			mode_lib->ip.pde_proc_buffer_size_64k_reqs;

	unsigned int log2_vmpg_height = 0;
	unsigned int log2_vmpg_width = 0;
	unsigned int log2_dpte_req_height_ptes = 0;
	unsigned int log2_dpte_req_height = 0;
	unsigned int log2_dpte_req_width = 0;
	unsigned int log2_dpte_row_height_linear = 0;
	unsigned int log2_dpte_row_height = 0;
	unsigned int log2_dpte_group_width = 0;
	unsigned int dpte_row_width_ub = 0;
	unsigned int dpte_req_height = 0;
	unsigned int dpte_req_width = 0;
	unsigned int dpte_group_width = 0;
	unsigned int log2_dpte_group_bytes = 0;
	unsigned int log2_dpte_group_length = 0;
	unsigned int pde_buf_entries;
	bool yuv420 = (source_format == dm_420_8 || source_format == dm_420_10);

	Calculate256BBlockSizes((enum source_format_class)(source_format),
			(enum dm_swizzle_mode)(tiling),
			bytes_per_element_y,
			bytes_per_element_c,
			&blk256_height_y,
			&blk256_height_c,
			&blk256_width_y,
			&blk256_width_c);

	if (!is_chroma) {
		blk256_width = blk256_width_y;
		blk256_height = blk256_height_y;
		bytes_per_element = bytes_per_element_y;
	} else {
		blk256_width = blk256_width_c;
		blk256_height = blk256_height_c;
		bytes_per_element = bytes_per_element_c;
	}

	log2_bytes_per_element = dml_log2(bytes_per_element);

	dml_print("DML_DLG: %s: surf_linear        = %d\n", __func__, surf_linear);
	dml_print("DML_DLG: %s: surf_vert          = %d\n", __func__, surf_vert);
	dml_print("DML_DLG: %s: blk256_width       = %d\n", __func__, blk256_width);
	dml_print("DML_DLG: %s: blk256_height      = %d\n", __func__, blk256_height);

	log2_blk256_width = dml_log2((double) blk256_width);
	log2_blk256_height = dml_log2((double) blk256_height);
	blk_bytes = surf_linear ?
			256 : get_blk_size_bytes((enum source_macro_tile_size) macro_tile_size);
	log2_blk_bytes = dml_log2((double) blk_bytes);
	log2_blk_height = 0;
	log2_blk_width = 0;

	// remember log rule
	// "+" in log is multiply
	// "-" in log is divide
	// "/2" is like square root
	// blk is vertical biased
	if (tiling != dm_sw_linear)
		log2_blk_height = log2_blk256_height
				+ dml_ceil((double) (log2_blk_bytes - 8) / 2.0, 1);
	else
		log2_blk_height = 0;  // blk height of 1

	log2_blk_width = log2_blk_bytes - log2_bytes_per_element - log2_blk_height;

	if (!surf_vert) {
		rq_dlg_param->swath_width_ub = dml_round_to_multiple(vp_width - 1, blk256_width, 1)
				+ blk256_width;
		rq_dlg_param->req_per_swath_ub = rq_dlg_param->swath_width_ub >> log2_blk256_width;
	} else {
		rq_dlg_param->swath_width_ub = dml_round_to_multiple(vp_height - 1, blk256_height, 1)
				+ blk256_height;
		rq_dlg_param->req_per_swath_ub = rq_dlg_param->swath_width_ub >> log2_blk256_height;
	}

	if (!surf_vert)
		rq_misc_param->full_swath_bytes = rq_dlg_param->swath_width_ub * blk256_height
				* bytes_per_element;
	else
		rq_misc_param->full_swath_bytes = rq_dlg_param->swath_width_ub * blk256_width
				* bytes_per_element;

	rq_misc_param->blk256_height = blk256_height;
	rq_misc_param->blk256_width = blk256_width;

	// -------
	// meta
	// -------
	log2_meta_req_bytes = 6; // meta request is 64b and is 8x8byte meta element

	// each 64b meta request for dcn is 8x8 meta elements and
	// a meta element covers one 256b block of the the data surface.
	log2_meta_req_height = log2_blk256_height + 3; // meta req is 8x8 byte, each byte represent 1 blk256
	log2_meta_req_width = log2_meta_req_bytes + 8 - log2_bytes_per_element
			- log2_meta_req_height;
	meta_req_width = 1 << log2_meta_req_width;
	meta_req_height = 1 << log2_meta_req_height;
	log2_meta_row_height = 0;
	meta_row_width_ub = 0;

	// the dimensions of a meta row are meta_row_width x meta_row_height in elements.
	// calculate upper bound of the meta_row_width
	if (!surf_vert) {
		log2_meta_row_height = log2_meta_req_height;
		meta_row_width_ub = dml_round_to_multiple(vp_width - 1, meta_req_width, 1)
				+ meta_req_width;
		rq_dlg_param->meta_req_per_row_ub = meta_row_width_ub / meta_req_width;
	} else {
		log2_meta_row_height = log2_meta_req_width;
		meta_row_width_ub = dml_round_to_multiple(vp_height - 1, meta_req_height, 1)
				+ meta_req_height;
		rq_dlg_param->meta_req_per_row_ub = meta_row_width_ub / meta_req_height;
	}
	rq_dlg_param->meta_bytes_per_row_ub = rq_dlg_param->meta_req_per_row_ub * 64;

	rq_dlg_param->meta_row_height = 1 << log2_meta_row_height;

	log2_meta_chunk_bytes = dml_log2(rq_sizing_param->meta_chunk_bytes);
	log2_meta_chunk_height = log2_meta_row_height;

	//full sized meta chunk width in unit of data elements
	log2_meta_chunk_width = log2_meta_chunk_bytes + 8 - log2_bytes_per_element
			- log2_meta_chunk_height;
	log2_min_meta_chunk_bytes = dml_log2(rq_sizing_param->min_meta_chunk_bytes);
	min_meta_chunk_width = 1
			<< (log2_min_meta_chunk_bytes + 8 - log2_bytes_per_element
					- log2_meta_chunk_height);
	meta_chunk_width = 1 << log2_meta_chunk_width;
	meta_chunk_per_row_int = (unsigned int) (meta_row_width_ub / meta_chunk_width);
	meta_row_remainder = meta_row_width_ub % meta_chunk_width;
	meta_chunk_threshold = 0;
	meta_blk_bytes = 4096;
	meta_blk_height = blk256_height * 64;
	meta_blk_width = meta_blk_bytes * 256 / bytes_per_element / meta_blk_height;
	meta_surface_bytes = meta_pitch
			* (dml_round_to_multiple(vp_height - 1, meta_blk_height, 1) + meta_blk_height)
			* bytes_per_element / 256;
	vmpg_bytes = mode_lib->soc.vmm_page_size_bytes;
	meta_pte_req_per_frame_ub = (dml_round_to_multiple(meta_surface_bytes - vmpg_bytes,
			8 * vmpg_bytes,
			1) + 8 * vmpg_bytes) / (8 * vmpg_bytes);
	meta_pte_bytes_per_frame_ub = meta_pte_req_per_frame_ub * 64; //64B mpte request
	rq_dlg_param->meta_pte_bytes_per_frame_ub = meta_pte_bytes_per_frame_ub;

	dml_print("DML_DLG: %s: meta_blk_height             = %d\n", __func__, meta_blk_height);
	dml_print("DML_DLG: %s: meta_blk_width              = %d\n", __func__, meta_blk_width);
	dml_print("DML_DLG: %s: meta_surface_bytes          = %d\n", __func__, meta_surface_bytes);
	dml_print("DML_DLG: %s: meta_pte_req_per_frame_ub   = %d\n",
			__func__,
			meta_pte_req_per_frame_ub);
	dml_print("DML_DLG: %s: meta_pte_bytes_per_frame_ub = %d\n",
			__func__,
			meta_pte_bytes_per_frame_ub);

	if (!surf_vert)
		meta_chunk_threshold = 2 * min_meta_chunk_width - meta_req_width;
	else
		meta_chunk_threshold = 2 * min_meta_chunk_width - meta_req_height;

	if (meta_row_remainder <= meta_chunk_threshold)
		rq_dlg_param->meta_chunks_per_row_ub = meta_chunk_per_row_int + 1;
	else
		rq_dlg_param->meta_chunks_per_row_ub = meta_chunk_per_row_int + 2;

	// ------
	// dpte
	// ------
	if (surf_linear) {
		log2_vmpg_height = 0;   // one line high
	} else {
		log2_vmpg_height = (log2_vmpg_bytes - 8) / 2 + log2_blk256_height;
	}
	log2_vmpg_width = log2_vmpg_bytes - log2_bytes_per_element - log2_vmpg_height;

	// only 3 possible shapes for dpte request in dimensions of ptes: 8x1, 4x2, 2x4.
	if (surf_linear) { //one 64B PTE request returns 8 PTEs
		log2_dpte_req_height_ptes = 0;
		log2_dpte_req_width = log2_vmpg_width + 3;
		log2_dpte_req_height = 0;
	} else if (log2_blk_bytes == 12) { //4KB tile means 4kB page size
		//one 64B req gives 8x1 PTEs for 4KB tile
		log2_dpte_req_height_ptes = 0;
		log2_dpte_req_width = log2_blk_width + 3;
		log2_dpte_req_height = log2_blk_height + 0;
	} else if ((log2_blk_bytes >= 16) && (log2_vmpg_bytes == 12)) { // tile block >= 64KB
		//two 64B reqs of 2x4 PTEs give 16 PTEs to cover 64KB
		log2_dpte_req_height_ptes = 4;
		log2_dpte_req_width = log2_blk256_width + 4; // log2_64KB_width
		log2_dpte_req_height = log2_blk256_height + 4; // log2_64KB_height
	} else { //64KB page size and must 64KB tile block
		 //one 64B req gives 8x1 PTEs for 64KB tile
		log2_dpte_req_height_ptes = 0;
		log2_dpte_req_width = log2_blk_width + 3;
		log2_dpte_req_height = log2_blk_height + 0;
	}

	// The dpte request dimensions in data elements is dpte_req_width x dpte_req_height
	// log2_vmpg_width is how much 1 pte represent, now calculating how much a 64b pte req represent
	// That depends on the pte shape (i.e. 8x1, 4x2, 2x4)
	//log2_dpte_req_height    = log2_vmpg_height + log2_dpte_req_height_ptes;
	//log2_dpte_req_width     = log2_vmpg_width + log2_dpte_req_width_ptes;
	dpte_req_height = 1 << log2_dpte_req_height;
	dpte_req_width = 1 << log2_dpte_req_width;

	// calculate pitch dpte row buffer can hold
	// round the result down to a power of two.
	pde_buf_entries = yuv420 ? (pde_proc_buffer_size_64k_reqs >> 1) : pde_proc_buffer_size_64k_reqs;
	if (surf_linear) {
		unsigned int dpte_row_height;

		log2_dpte_row_height_linear = dml_floor(dml_log2(dml_min(64 * 1024 * pde_buf_entries
										/ bytes_per_element,
								dpte_buf_in_pte_reqs
										* dpte_req_width)
								/ data_pitch),
				1);

		ASSERT(log2_dpte_row_height_linear >= 3);

		if (log2_dpte_row_height_linear > 7)
			log2_dpte_row_height_linear = 7;

		log2_dpte_row_height = log2_dpte_row_height_linear;
		// For linear, the dpte row is pitch dependent and the pte requests wrap at the pitch boundary.
		// the dpte_row_width_ub is the upper bound of data_pitch*dpte_row_height in elements with this unique buffering.
		dpte_row_height = 1 << log2_dpte_row_height;
		dpte_row_width_ub = dml_round_to_multiple(data_pitch * dpte_row_height - 1,
				dpte_req_width,
				1) + dpte_req_width;
		rq_dlg_param->dpte_req_per_row_ub = dpte_row_width_ub / dpte_req_width;
	} else {
		// the upper bound of the dpte_row_width without dependency on viewport position follows.
		// for tiled mode, row height is the same as req height and row store up to vp size upper bound
		if (!surf_vert) {
			log2_dpte_row_height = log2_dpte_req_height;
			dpte_row_width_ub = dml_round_to_multiple(vp_width - 1, dpte_req_width, 1)
					+ dpte_req_width;
			rq_dlg_param->dpte_req_per_row_ub = dpte_row_width_ub / dpte_req_width;
		} else {
			log2_dpte_row_height =
					(log2_blk_width < log2_dpte_req_width) ?
							log2_blk_width : log2_dpte_req_width;
			dpte_row_width_ub = dml_round_to_multiple(vp_height - 1, dpte_req_height, 1)
					+ dpte_req_height;
			rq_dlg_param->dpte_req_per_row_ub = dpte_row_width_ub / dpte_req_height;
		}
	}
	if (log2_blk_bytes >= 16 && log2_vmpg_bytes == 12) // tile block >= 64KB
		rq_dlg_param->dpte_bytes_per_row_ub = rq_dlg_param->dpte_req_per_row_ub * 128; //2*64B dpte request
	else
		rq_dlg_param->dpte_bytes_per_row_ub = rq_dlg_param->dpte_req_per_row_ub * 64; //64B dpte request

	rq_dlg_param->dpte_row_height = 1 << log2_dpte_row_height;

	// the dpte_group_bytes is reduced for the specific case of vertical
	// access of a tile surface that has dpte request of 8x1 ptes.
	if (!surf_linear & (log2_dpte_req_height_ptes == 0) & surf_vert) //reduced, in this case, will have page fault within a group
		rq_sizing_param->dpte_group_bytes = 512;
	else
		//full size
		rq_sizing_param->dpte_group_bytes = 2048;

	//since pte request size is 64byte, the number of data pte requests per full sized group is as follows.
	log2_dpte_group_bytes = dml_log2(rq_sizing_param->dpte_group_bytes);
	log2_dpte_group_length = log2_dpte_group_bytes - 6; //length in 64b requests

	// full sized data pte group width in elements
	if (!surf_vert)
		log2_dpte_group_width = log2_dpte_group_length + log2_dpte_req_width;
	else
		log2_dpte_group_width = log2_dpte_group_length + log2_dpte_req_height;

	//But if the tile block >=64KB and the page size is 4KB, then each dPTE request is 2*64B
	if ((log2_blk_bytes >= 16) && (log2_vmpg_bytes == 12)) // tile block >= 64KB
		log2_dpte_group_width = log2_dpte_group_width - 1;

	dpte_group_width = 1 << log2_dpte_group_width;

	// since dpte groups are only aligned to dpte_req_width and not dpte_group_width,
	// the upper bound for the dpte groups per row is as follows.
	rq_dlg_param->dpte_groups_per_row_ub = dml_ceil((double) dpte_row_width_ub / dpte_group_width,
			1);
}

static void get_surf_rq_param(struct display_mode_lib *mode_lib,
		display_data_rq_sizing_params_st *rq_sizing_param,
		display_data_rq_dlg_params_st *rq_dlg_param,
		display_data_rq_misc_params_st *rq_misc_param,
		const display_pipe_source_params_st pipe_src_param,
		bool is_chroma)
{
	bool mode_422 = 0;
	unsigned int vp_width = 0;
	unsigned int vp_height = 0;
	unsigned int data_pitch = 0;
	unsigned int meta_pitch = 0;
	unsigned int ppe = mode_422 ? 2 : 1;

	// FIXME check if ppe apply for both luma and chroma in 422 case
	if (is_chroma) {
		vp_width = pipe_src_param.viewport_width_c / ppe;
		vp_height = pipe_src_param.viewport_height_c;
		data_pitch = pipe_src_param.data_pitch_c;
		meta_pitch = pipe_src_param.meta_pitch_c;
	} else {
		vp_width = pipe_src_param.viewport_width / ppe;
		vp_height = pipe_src_param.viewport_height;
		data_pitch = pipe_src_param.data_pitch;
		meta_pitch = pipe_src_param.meta_pitch;
	}

	rq_sizing_param->chunk_bytes = 8192;

	if (rq_sizing_param->chunk_bytes == 64 * 1024)
		rq_sizing_param->min_chunk_bytes = 0;
	else
		rq_sizing_param->min_chunk_bytes = 1024;

	rq_sizing_param->meta_chunk_bytes = 2048;
	rq_sizing_param->min_meta_chunk_bytes = 256;

	rq_sizing_param->mpte_group_bytes = 2048;

	get_meta_and_pte_attr(mode_lib,
			rq_dlg_param,
			rq_misc_param,
			rq_sizing_param,
			vp_width,
			vp_height,
			data_pitch,
			meta_pitch,
			pipe_src_param.source_format,
			pipe_src_param.sw_mode,
			pipe_src_param.macro_tile_size,
			pipe_src_param.source_scan,
			is_chroma);
}

static void dml20_rq_dlg_get_rq_params(struct display_mode_lib *mode_lib,
		display_rq_params_st *rq_param,
		const display_pipe_source_params_st pipe_src_param)
{
	// get param for luma surface
	rq_param->yuv420 = pipe_src_param.source_format == dm_420_8
			|| pipe_src_param.source_format == dm_420_10;
	rq_param->yuv420_10bpc = pipe_src_param.source_format == dm_420_10;

	get_surf_rq_param(mode_lib,
			&(rq_param->sizing.rq_l),
			&(rq_param->dlg.rq_l),
			&(rq_param->misc.rq_l),
			pipe_src_param,
			0);

	if (is_dual_plane((enum source_format_class)(pipe_src_param.source_format))) {
		// get param for chroma surface
		get_surf_rq_param(mode_lib,
				&(rq_param->sizing.rq_c),
				&(rq_param->dlg.rq_c),
				&(rq_param->misc.rq_c),
				pipe_src_param,
				1);
	}

	// calculate how to split the det buffer space between luma and chroma
	handle_det_buf_split(mode_lib, rq_param, pipe_src_param);
	print__rq_params_st(mode_lib, *rq_param);
}

void dml20_rq_dlg_get_rq_reg(struct display_mode_lib *mode_lib,
		display_rq_regs_st *rq_regs,
		const display_pipe_params_st pipe_param)
{
	display_rq_params_st rq_param = {0};

	memset(rq_regs, 0, sizeof(*rq_regs));
	dml20_rq_dlg_get_rq_params(mode_lib, &rq_param, pipe_param.src);
	extract_rq_regs(mode_lib, rq_regs, rq_param);

	print__rq_regs_st(mode_lib, *rq_regs);
}

// Note: currently taken in as is.
// Nice to decouple code from hw register implement and extract code that are repeated for luma and chroma.
static void dml20_rq_dlg_get_dlg_params(struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *e2e_pipe_param,
		const unsigned int num_pipes,
		const unsigned int pipe_idx,
		display_dlg_regs_st *disp_dlg_regs,
		display_ttu_regs_st *disp_ttu_regs,
		const display_rq_dlg_params_st rq_dlg_param,
		const display_dlg_sys_params_st dlg_sys_param,
		const bool cstate_en,
		const bool pstate_en)
{
	const display_pipe_source_params_st *src = &e2e_pipe_param[pipe_idx].pipe.src;
	const display_pipe_dest_params_st *dst = &e2e_pipe_param[pipe_idx].pipe.dest;
	const display_output_params_st *dout = &e2e_pipe_param[pipe_idx].dout;
	const display_clocks_and_cfg_st *clks = &e2e_pipe_param[pipe_idx].clks_cfg;
	const scaler_ratio_depth_st *scl = &e2e_pipe_param[pipe_idx].pipe.scale_ratio_depth;
	const scaler_taps_st *taps = &e2e_pipe_param[pipe_idx].pipe.scale_taps;

	// -------------------------
	// Section 1.15.2.1: OTG dependent Params
	// -------------------------
	// Timing
	unsigned int htotal = dst->htotal;
//    unsigned int hblank_start = dst.hblank_start; // TODO: Remove
	unsigned int hblank_end = dst->hblank_end;
	unsigned int vblank_start = dst->vblank_start;
	unsigned int vblank_end = dst->vblank_end;
	unsigned int min_vblank = mode_lib->ip.min_vblank_lines;

	double dppclk_freq_in_mhz = clks->dppclk_mhz;
	double dispclk_freq_in_mhz = clks->dispclk_mhz;
	double refclk_freq_in_mhz = clks->refclk_mhz;
	double pclk_freq_in_mhz = dst->pixel_rate_mhz;
	bool interlaced = dst->interlaced;

	double ref_freq_to_pix_freq = refclk_freq_in_mhz / pclk_freq_in_mhz;

	double min_dcfclk_mhz;
	double t_calc_us;
	double min_ttu_vblank;

	double min_dst_y_ttu_vblank;
	unsigned int dlg_vblank_start;
	bool dual_plane;
	bool mode_422;
	unsigned int access_dir;
	unsigned int vp_height_l;
	unsigned int vp_width_l;
	unsigned int vp_height_c;
	unsigned int vp_width_c;

	// Scaling
	unsigned int htaps_l;
	unsigned int htaps_c;
	double hratio_l;
	double hratio_c;
	double vratio_l;
	double vratio_c;
	bool scl_enable;

	double line_time_in_us;
	//    double vinit_l;
	//    double vinit_c;
	//    double vinit_bot_l;
	//    double vinit_bot_c;

	//    unsigned int swath_height_l;
	unsigned int swath_width_ub_l;
	//    unsigned int dpte_bytes_per_row_ub_l;
	unsigned int dpte_groups_per_row_ub_l;
	//    unsigned int meta_pte_bytes_per_frame_ub_l;
	//    unsigned int meta_bytes_per_row_ub_l;

	//    unsigned int swath_height_c;
	unsigned int swath_width_ub_c;
	//   unsigned int dpte_bytes_per_row_ub_c;
	unsigned int dpte_groups_per_row_ub_c;

	unsigned int meta_chunks_per_row_ub_l;
	unsigned int meta_chunks_per_row_ub_c;
	unsigned int vupdate_offset;
	unsigned int vupdate_width;
	unsigned int vready_offset;

	unsigned int dppclk_delay_subtotal;
	unsigned int dispclk_delay_subtotal;
	unsigned int pixel_rate_delay_subtotal;

	unsigned int vstartup_start;
	unsigned int dst_x_after_scaler;
	unsigned int dst_y_after_scaler;
	double line_wait;
	double dst_y_prefetch;
	double dst_y_per_vm_vblank;
	double dst_y_per_row_vblank;
	double dst_y_per_vm_flip;
	double dst_y_per_row_flip;
	double min_dst_y_per_vm_vblank;
	double min_dst_y_per_row_vblank;
	double lsw;
	double vratio_pre_l;
	double vratio_pre_c;
	unsigned int req_per_swath_ub_l;
	unsigned int req_per_swath_ub_c;
	unsigned int meta_row_height_l;
	unsigned int meta_row_height_c;
	unsigned int swath_width_pixels_ub_l;
	unsigned int swath_width_pixels_ub_c;
	unsigned int scaler_rec_in_width_l;
	unsigned int scaler_rec_in_width_c;
	unsigned int dpte_row_height_l;
	unsigned int dpte_row_height_c;
	double hscale_pixel_rate_l;
	double hscale_pixel_rate_c;
	double min_hratio_fact_l;
	double min_hratio_fact_c;
	double refcyc_per_line_delivery_pre_l;
	double refcyc_per_line_delivery_pre_c;
	double refcyc_per_line_delivery_l;
	double refcyc_per_line_delivery_c;

	double refcyc_per_req_delivery_pre_l;
	double refcyc_per_req_delivery_pre_c;
	double refcyc_per_req_delivery_l;
	double refcyc_per_req_delivery_c;

	unsigned int full_recout_width;
	double xfc_transfer_delay;
	double xfc_precharge_delay;
	double xfc_remote_surface_flip_latency;
	double xfc_dst_y_delta_drq_limit;
	double xfc_prefetch_margin;
	double refcyc_per_req_delivery_pre_cur0;
	double refcyc_per_req_delivery_cur0;
	double refcyc_per_req_delivery_pre_cur1;
	double refcyc_per_req_delivery_cur1;

	memset(disp_dlg_regs, 0, sizeof(*disp_dlg_regs));
	memset(disp_ttu_regs, 0, sizeof(*disp_ttu_regs));

	dml_print("DML_DLG: %s:  cstate_en = %d\n", __func__, cstate_en);
	dml_print("DML_DLG: %s:  pstate_en = %d\n", __func__, pstate_en);

	dml_print("DML_DLG: %s: dppclk_freq_in_mhz     = %3.2f\n", __func__, dppclk_freq_in_mhz);
	dml_print("DML_DLG: %s: dispclk_freq_in_mhz    = %3.2f\n", __func__, dispclk_freq_in_mhz);
	dml_print("DML_DLG: %s: refclk_freq_in_mhz     = %3.2f\n", __func__, refclk_freq_in_mhz);
	dml_print("DML_DLG: %s: pclk_freq_in_mhz       = %3.2f\n", __func__, pclk_freq_in_mhz);
	dml_print("DML_DLG: %s: interlaced             = %d\n", __func__, interlaced);
	ASSERT(ref_freq_to_pix_freq < 4.0);

	disp_dlg_regs->ref_freq_to_pix_freq =
			(unsigned int) (ref_freq_to_pix_freq * dml_pow(2, 19));
	disp_dlg_regs->refcyc_per_htotal = (unsigned int) (ref_freq_to_pix_freq * (double) htotal
			* dml_pow(2, 8));
	disp_dlg_regs->dlg_vblank_end = interlaced ? (vblank_end / 2) : vblank_end; // 15 bits
	disp_dlg_regs->refcyc_h_blank_end = (unsigned int) ((double) hblank_end
			* (double) ref_freq_to_pix_freq);
	ASSERT(disp_dlg_regs->refcyc_h_blank_end < (unsigned int) dml_pow(2, 13));

	min_dcfclk_mhz = dlg_sys_param.deepsleep_dcfclk_mhz;
	t_calc_us = get_tcalc(mode_lib, e2e_pipe_param, num_pipes);
	min_ttu_vblank = get_min_ttu_vblank(mode_lib, e2e_pipe_param, num_pipes, pipe_idx);

	min_dst_y_ttu_vblank = min_ttu_vblank * pclk_freq_in_mhz / (double) htotal;
	dlg_vblank_start = interlaced ? (vblank_start / 2) : vblank_start;

	disp_dlg_regs->min_dst_y_next_start = (unsigned int) (((double) dlg_vblank_start
			+ min_dst_y_ttu_vblank) * dml_pow(2, 2));
	ASSERT(disp_dlg_regs->min_dst_y_next_start < (unsigned int) dml_pow(2, 18));

	dml_print("DML_DLG: %s: min_dcfclk_mhz                         = %3.2f\n",
			__func__,
			min_dcfclk_mhz);
	dml_print("DML_DLG: %s: min_ttu_vblank                         = %3.2f\n",
			__func__,
			min_ttu_vblank);
	dml_print("DML_DLG: %s: min_dst_y_ttu_vblank                   = %3.2f\n",
			__func__,
			min_dst_y_ttu_vblank);
	dml_print("DML_DLG: %s: t_calc_us                              = %3.2f\n",
			__func__,
			t_calc_us);
	dml_print("DML_DLG: %s: disp_dlg_regs->min_dst_y_next_start    = 0x%0x\n",
			__func__,
			disp_dlg_regs->min_dst_y_next_start);
	dml_print("DML_DLG: %s: ref_freq_to_pix_freq                   = %3.2f\n",
			__func__,
			ref_freq_to_pix_freq);

	// -------------------------
	// Section 1.15.2.2: Prefetch, Active and TTU
	// -------------------------
	// Prefetch Calc
	// Source
//             dcc_en              = src.dcc;
	dual_plane = is_dual_plane((enum source_format_class)(src->source_format));
	mode_422 = 0; // FIXME
	access_dir = (src->source_scan == dm_vert); // vp access direction: horizontal or vertical accessed
//      bytes_per_element_l = get_bytes_per_element(source_format_class(src.source_format), 0);
//      bytes_per_element_c = get_bytes_per_element(source_format_class(src.source_format), 1);
	vp_height_l = src->viewport_height;
	vp_width_l = src->viewport_width;
	vp_height_c = src->viewport_height_c;
	vp_width_c = src->viewport_width_c;

	// Scaling
	htaps_l = taps->htaps;
	htaps_c = taps->htaps_c;
	hratio_l = scl->hscl_ratio;
	hratio_c = scl->hscl_ratio_c;
	vratio_l = scl->vscl_ratio;
	vratio_c = scl->vscl_ratio_c;
	scl_enable = scl->scl_enable;

	line_time_in_us = (htotal / pclk_freq_in_mhz);
//     vinit_l         = scl.vinit;
//     vinit_c         = scl.vinit_c;
//     vinit_bot_l     = scl.vinit_bot;
//     vinit_bot_c     = scl.vinit_bot_c;

//    unsigned int swath_height_l                 = rq_dlg_param.rq_l.swath_height;
	swath_width_ub_l = rq_dlg_param.rq_l.swath_width_ub;
//    unsigned int dpte_bytes_per_row_ub_l        = rq_dlg_param.rq_l.dpte_bytes_per_row_ub;
	dpte_groups_per_row_ub_l = rq_dlg_param.rq_l.dpte_groups_per_row_ub;
//    unsigned int meta_pte_bytes_per_frame_ub_l  = rq_dlg_param.rq_l.meta_pte_bytes_per_frame_ub;
//    unsigned int meta_bytes_per_row_ub_l        = rq_dlg_param.rq_l.meta_bytes_per_row_ub;

//    unsigned int swath_height_c                 = rq_dlg_param.rq_c.swath_height;
	swath_width_ub_c = rq_dlg_param.rq_c.swath_width_ub;
	//   dpte_bytes_per_row_ub_c        = rq_dlg_param.rq_c.dpte_bytes_per_row_ub;
	dpte_groups_per_row_ub_c = rq_dlg_param.rq_c.dpte_groups_per_row_ub;

	meta_chunks_per_row_ub_l = rq_dlg_param.rq_l.meta_chunks_per_row_ub;
	meta_chunks_per_row_ub_c = rq_dlg_param.rq_c.meta_chunks_per_row_ub;
	vupdate_offset = dst->vupdate_offset;
	vupdate_width = dst->vupdate_width;
	vready_offset = dst->vready_offset;

	dppclk_delay_subtotal = mode_lib->ip.dppclk_delay_subtotal;
	dispclk_delay_subtotal = mode_lib->ip.dispclk_delay_subtotal;

	if (scl_enable)
		dppclk_delay_subtotal += mode_lib->ip.dppclk_delay_scl;
	else
		dppclk_delay_subtotal += mode_lib->ip.dppclk_delay_scl_lb_only;

	dppclk_delay_subtotal += mode_lib->ip.dppclk_delay_cnvc_formatter
			+ src->num_cursors * mode_lib->ip.dppclk_delay_cnvc_cursor;

	if (dout->dsc_enable) {
		double dsc_delay = get_dsc_delay(mode_lib, e2e_pipe_param, num_pipes, pipe_idx);

		dispclk_delay_subtotal += dsc_delay;
	}

	pixel_rate_delay_subtotal = dppclk_delay_subtotal * pclk_freq_in_mhz / dppclk_freq_in_mhz
			+ dispclk_delay_subtotal * pclk_freq_in_mhz / dispclk_freq_in_mhz;

	vstartup_start = dst->vstartup_start;
	if (interlaced) {
		if (vstartup_start / 2.0
				- (double) (vready_offset + vupdate_width + vupdate_offset) / htotal
				<= vblank_end / 2.0)
			disp_dlg_regs->vready_after_vcount0 = 1;
		else
			disp_dlg_regs->vready_after_vcount0 = 0;
	} else {
		if (vstartup_start
				- (double) (vready_offset + vupdate_width + vupdate_offset) / htotal
				<= vblank_end)
			disp_dlg_regs->vready_after_vcount0 = 1;
		else
			disp_dlg_regs->vready_after_vcount0 = 0;
	}

	// TODO: Where is this coming from?
	if (interlaced)
		vstartup_start = vstartup_start / 2;

	// TODO: What if this min_vblank doesn't match the value in the dml_config_settings.cpp?
	if (vstartup_start >= min_vblank) {
		dml_print("WARNING: DML_DLG: %s: vblank_start=%d vblank_end=%d\n",
				__func__,
				vblank_start,
				vblank_end);
		dml_print("WARNING: DML_DLG: %s: vstartup_start=%d should be less than min_vblank=%d\n",
				__func__,
				vstartup_start,
				min_vblank);
		min_vblank = vstartup_start + 1;
		dml_print("WARNING: DML_DLG: %s: vstartup_start=%d should be less than min_vblank=%d\n",
				__func__,
				vstartup_start,
				min_vblank);
	}

	dst_x_after_scaler = get_dst_x_after_scaler(mode_lib, e2e_pipe_param, num_pipes, pipe_idx);
	dst_y_after_scaler = get_dst_y_after_scaler(mode_lib, e2e_pipe_param, num_pipes, pipe_idx);

	dml_print("DML_DLG: %s: htotal                                 = %d\n", __func__, htotal);
	dml_print("DML_DLG: %s: pixel_rate_delay_subtotal              = %d\n",
			__func__,
			pixel_rate_delay_subtotal);
	dml_print("DML_DLG: %s: dst_x_after_scaler                     = %d\n",
			__func__,
			dst_x_after_scaler);
	dml_print("DML_DLG: %s: dst_y_after_scaler                     = %d\n",
			__func__,
			dst_y_after_scaler);

	// Lwait
	line_wait = mode_lib->soc.urgent_latency_us;
	if (cstate_en)
		line_wait = dml_max(mode_lib->soc.sr_enter_plus_exit_time_us, line_wait);
	if (pstate_en)
		line_wait = dml_max(mode_lib->soc.dram_clock_change_latency_us
						+ mode_lib->soc.urgent_latency_us,
				line_wait);
	line_wait = line_wait / line_time_in_us;

	dst_y_prefetch = get_dst_y_prefetch(mode_lib, e2e_pipe_param, num_pipes, pipe_idx);
	dml_print("DML_DLG: %s: dst_y_prefetch (after rnd) = %3.2f\n", __func__, dst_y_prefetch);

	dst_y_per_vm_vblank = get_dst_y_per_vm_vblank(mode_lib,
			e2e_pipe_param,
			num_pipes,
			pipe_idx);
	dst_y_per_row_vblank = get_dst_y_per_row_vblank(mode_lib,
			e2e_pipe_param,
			num_pipes,
			pipe_idx);
	dst_y_per_vm_flip = get_dst_y_per_vm_flip(mode_lib, e2e_pipe_param, num_pipes, pipe_idx);
	dst_y_per_row_flip = get_dst_y_per_row_flip(mode_lib, e2e_pipe_param, num_pipes, pipe_idx);

	min_dst_y_per_vm_vblank = 8.0;
	min_dst_y_per_row_vblank = 16.0;

	// magic!
	if (htotal <= 75) {
		min_vblank = 300;
		min_dst_y_per_vm_vblank = 100.0;
		min_dst_y_per_row_vblank = 100.0;
	}

	dml_print("DML_DLG: %s: dst_y_per_vm_vblank    = %3.2f\n", __func__, dst_y_per_vm_vblank);
	dml_print("DML_DLG: %s: dst_y_per_row_vblank   = %3.2f\n", __func__, dst_y_per_row_vblank);

	ASSERT(dst_y_per_vm_vblank < min_dst_y_per_vm_vblank);
	ASSERT(dst_y_per_row_vblank < min_dst_y_per_row_vblank);

	ASSERT(dst_y_prefetch > (dst_y_per_vm_vblank + dst_y_per_row_vblank));
	lsw = dst_y_prefetch - (dst_y_per_vm_vblank + dst_y_per_row_vblank);

	dml_print("DML_DLG: %s: lsw = %3.2f\n", __func__, lsw);

	vratio_pre_l = get_vratio_prefetch_l(mode_lib, e2e_pipe_param, num_pipes, pipe_idx);
	vratio_pre_c = get_vratio_prefetch_c(mode_lib, e2e_pipe_param, num_pipes, pipe_idx);

	dml_print("DML_DLG: %s: vratio_pre_l=%3.2f\n", __func__, vratio_pre_l);
	dml_print("DML_DLG: %s: vratio_pre_c=%3.2f\n", __func__, vratio_pre_c);

	// Active
	req_per_swath_ub_l = rq_dlg_param.rq_l.req_per_swath_ub;
	req_per_swath_ub_c = rq_dlg_param.rq_c.req_per_swath_ub;
	meta_row_height_l = rq_dlg_param.rq_l.meta_row_height;
	meta_row_height_c = rq_dlg_param.rq_c.meta_row_height;
	swath_width_pixels_ub_l = 0;
	swath_width_pixels_ub_c = 0;
	scaler_rec_in_width_l = 0;
	scaler_rec_in_width_c = 0;
	dpte_row_height_l = rq_dlg_param.rq_l.dpte_row_height;
	dpte_row_height_c = rq_dlg_param.rq_c.dpte_row_height;

	if (mode_422) {
		swath_width_pixels_ub_l = swath_width_ub_l * 2;  // *2 for 2 pixel per element
		swath_width_pixels_ub_c = swath_width_ub_c * 2;
	} else {
		swath_width_pixels_ub_l = swath_width_ub_l * 1;
		swath_width_pixels_ub_c = swath_width_ub_c * 1;
	}

	hscale_pixel_rate_l = 0.;
	hscale_pixel_rate_c = 0.;
	min_hratio_fact_l = 1.0;
	min_hratio_fact_c = 1.0;

	if (htaps_l <= 1)
		min_hratio_fact_l = 2.0;
	else if (htaps_l <= 6) {
		if ((hratio_l * 2.0) > 4.0)
			min_hratio_fact_l = 4.0;
		else
			min_hratio_fact_l = hratio_l * 2.0;
	} else {
		if (hratio_l > 4.0)
			min_hratio_fact_l = 4.0;
		else
			min_hratio_fact_l = hratio_l;
	}

	hscale_pixel_rate_l = min_hratio_fact_l * dppclk_freq_in_mhz;

	if (htaps_c <= 1)
		min_hratio_fact_c = 2.0;
	else if (htaps_c <= 6) {
		if ((hratio_c * 2.0) > 4.0)
			min_hratio_fact_c = 4.0;
		else
			min_hratio_fact_c = hratio_c * 2.0;
	} else {
		if (hratio_c > 4.0)
			min_hratio_fact_c = 4.0;
		else
			min_hratio_fact_c = hratio_c;
	}

	hscale_pixel_rate_c = min_hratio_fact_c * dppclk_freq_in_mhz;

	refcyc_per_line_delivery_pre_l = 0.;
	refcyc_per_line_delivery_pre_c = 0.;
	refcyc_per_line_delivery_l = 0.;
	refcyc_per_line_delivery_c = 0.;

	refcyc_per_req_delivery_pre_l = 0.;
	refcyc_per_req_delivery_pre_c = 0.;
	refcyc_per_req_delivery_l = 0.;
	refcyc_per_req_delivery_c = 0.;

	full_recout_width = 0;
	// In ODM
	if (src->is_hsplit) {
		// This "hack"  is only allowed (and valid) for MPC combine. In ODM
		// combine, you MUST specify the full_recout_width...according to Oswin
		if (dst->full_recout_width == 0 && !dst->odm_combine) {
			dml_print("DML_DLG: %s: Warning: full_recout_width not set in hsplit mode\n",
					__func__);
			full_recout_width = dst->recout_width * 2; // assume half split for dcn1
		} else
			full_recout_width = dst->full_recout_width;
	} else
		full_recout_width = dst->recout_width;

	// As of DCN2, mpc_combine and odm_combine are mutually exclusive
	refcyc_per_line_delivery_pre_l = get_refcyc_per_delivery(mode_lib,
			refclk_freq_in_mhz,
			pclk_freq_in_mhz,
			dst->odm_combine,
			full_recout_width,
			dst->hactive,
			vratio_pre_l,
			hscale_pixel_rate_l,
			swath_width_pixels_ub_l,
			1); // per line

	refcyc_per_line_delivery_l = get_refcyc_per_delivery(mode_lib,
			refclk_freq_in_mhz,
			pclk_freq_in_mhz,
			dst->odm_combine,
			full_recout_width,
			dst->hactive,
			vratio_l,
			hscale_pixel_rate_l,
			swath_width_pixels_ub_l,
			1); // per line

	dml_print("DML_DLG: %s: full_recout_width              = %d\n",
			__func__,
			full_recout_width);
	dml_print("DML_DLG: %s: hscale_pixel_rate_l            = %3.2f\n",
			__func__,
			hscale_pixel_rate_l);
	dml_print("DML_DLG: %s: refcyc_per_line_delivery_pre_l = %3.2f\n",
			__func__,
			refcyc_per_line_delivery_pre_l);
	dml_print("DML_DLG: %s: refcyc_per_line_delivery_l     = %3.2f\n",
			__func__,
			refcyc_per_line_delivery_l);

	if (dual_plane) {
		refcyc_per_line_delivery_pre_c = get_refcyc_per_delivery(mode_lib,
				refclk_freq_in_mhz,
				pclk_freq_in_mhz,
				dst->odm_combine,
				full_recout_width,
				dst->hactive,
				vratio_pre_c,
				hscale_pixel_rate_c,
				swath_width_pixels_ub_c,
				1); // per line

		refcyc_per_line_delivery_c = get_refcyc_per_delivery(mode_lib,
				refclk_freq_in_mhz,
				pclk_freq_in_mhz,
				dst->odm_combine,
				full_recout_width,
				dst->hactive,
				vratio_c,
				hscale_pixel_rate_c,
				swath_width_pixels_ub_c,
				1);  // per line

		dml_print("DML_DLG: %s: refcyc_per_line_delivery_pre_c = %3.2f\n",
				__func__,
				refcyc_per_line_delivery_pre_c);
		dml_print("DML_DLG: %s: refcyc_per_line_delivery_c     = %3.2f\n",
				__func__,
				refcyc_per_line_delivery_c);
	}

	// TTU - Luma / Chroma
	if (access_dir) {  // vertical access
		scaler_rec_in_width_l = vp_height_l;
		scaler_rec_in_width_c = vp_height_c;
	} else {
		scaler_rec_in_width_l = vp_width_l;
		scaler_rec_in_width_c = vp_width_c;
	}

	refcyc_per_req_delivery_pre_l = get_refcyc_per_delivery(mode_lib,
			refclk_freq_in_mhz,
			pclk_freq_in_mhz,
			dst->odm_combine,
			full_recout_width,
			dst->hactive,
			vratio_pre_l,
			hscale_pixel_rate_l,
			scaler_rec_in_width_l,
			req_per_swath_ub_l);  // per req
	refcyc_per_req_delivery_l = get_refcyc_per_delivery(mode_lib,
			refclk_freq_in_mhz,
			pclk_freq_in_mhz,
			dst->odm_combine,
			full_recout_width,
			dst->hactive,
			vratio_l,
			hscale_pixel_rate_l,
			scaler_rec_in_width_l,
			req_per_swath_ub_l);  // per req

	dml_print("DML_DLG: %s: refcyc_per_req_delivery_pre_l = %3.2f\n",
			__func__,
			refcyc_per_req_delivery_pre_l);
	dml_print("DML_DLG: %s: refcyc_per_req_delivery_l     = %3.2f\n",
			__func__,
			refcyc_per_req_delivery_l);

	ASSERT(refcyc_per_req_delivery_pre_l < dml_pow(2, 13));
	ASSERT(refcyc_per_req_delivery_l < dml_pow(2, 13));

	if (dual_plane) {
		refcyc_per_req_delivery_pre_c = get_refcyc_per_delivery(mode_lib,
				refclk_freq_in_mhz,
				pclk_freq_in_mhz,
				dst->odm_combine,
				full_recout_width,
				dst->hactive,
				vratio_pre_c,
				hscale_pixel_rate_c,
				scaler_rec_in_width_c,
				req_per_swath_ub_c);  // per req
		refcyc_per_req_delivery_c = get_refcyc_per_delivery(mode_lib,
				refclk_freq_in_mhz,
				pclk_freq_in_mhz,
				dst->odm_combine,
				full_recout_width,
				dst->hactive,
				vratio_c,
				hscale_pixel_rate_c,
				scaler_rec_in_width_c,
				req_per_swath_ub_c);  // per req

		dml_print("DML_DLG: %s: refcyc_per_req_delivery_pre_c = %3.2f\n",
				__func__,
				refcyc_per_req_delivery_pre_c);
		dml_print("DML_DLG: %s: refcyc_per_req_delivery_c     = %3.2f\n",
				__func__,
				refcyc_per_req_delivery_c);

		ASSERT(refcyc_per_req_delivery_pre_c < dml_pow(2, 13));
		ASSERT(refcyc_per_req_delivery_c < dml_pow(2, 13));
	}

	// XFC
	xfc_transfer_delay = get_xfc_transfer_delay(mode_lib, e2e_pipe_param, num_pipes, pipe_idx);
	xfc_precharge_delay = get_xfc_precharge_delay(mode_lib,
			e2e_pipe_param,
			num_pipes,
			pipe_idx);
	xfc_remote_surface_flip_latency = get_xfc_remote_surface_flip_latency(mode_lib,
			e2e_pipe_param,
			num_pipes,
			pipe_idx);
	xfc_dst_y_delta_drq_limit = xfc_remote_surface_flip_latency;
	xfc_prefetch_margin = get_xfc_prefetch_margin(mode_lib,
			e2e_pipe_param,
			num_pipes,
			pipe_idx);

	// TTU - Cursor
	refcyc_per_req_delivery_pre_cur0 = 0.0;
	refcyc_per_req_delivery_cur0 = 0.0;
	if (src->num_cursors > 0) {
		calculate_ttu_cursor(mode_lib,
				&refcyc_per_req_delivery_pre_cur0,
				&refcyc_per_req_delivery_cur0,
				refclk_freq_in_mhz,
				ref_freq_to_pix_freq,
				hscale_pixel_rate_l,
				scl->hscl_ratio,
				vratio_pre_l,
				vratio_l,
				src->cur0_src_width,
				(enum cursor_bpp)(src->cur0_bpp));
	}

	refcyc_per_req_delivery_pre_cur1 = 0.0;
	refcyc_per_req_delivery_cur1 = 0.0;
	if (src->num_cursors > 1) {
		calculate_ttu_cursor(mode_lib,
				&refcyc_per_req_delivery_pre_cur1,
				&refcyc_per_req_delivery_cur1,
				refclk_freq_in_mhz,
				ref_freq_to_pix_freq,
				hscale_pixel_rate_l,
				scl->hscl_ratio,
				vratio_pre_l,
				vratio_l,
				src->cur1_src_width,
				(enum cursor_bpp)(src->cur1_bpp));
	}

	// TTU - Misc
	// all hard-coded

	// Assignment to register structures
	disp_dlg_regs->dst_y_after_scaler = dst_y_after_scaler; // in terms of line
	disp_dlg_regs->refcyc_x_after_scaler = dst_x_after_scaler * ref_freq_to_pix_freq; // in terms of refclk
	ASSERT(disp_dlg_regs->refcyc_x_after_scaler < (unsigned int) dml_pow(2, 13));
	disp_dlg_regs->dst_y_prefetch = (unsigned int) (dst_y_prefetch * dml_pow(2, 2));
	disp_dlg_regs->dst_y_per_vm_vblank = (unsigned int) (dst_y_per_vm_vblank * dml_pow(2, 2));
	disp_dlg_regs->dst_y_per_row_vblank = (unsigned int) (dst_y_per_row_vblank * dml_pow(2, 2));
	disp_dlg_regs->dst_y_per_vm_flip = (unsigned int) (dst_y_per_vm_flip * dml_pow(2, 2));
	disp_dlg_regs->dst_y_per_row_flip = (unsigned int) (dst_y_per_row_flip * dml_pow(2, 2));

	disp_dlg_regs->vratio_prefetch = (unsigned int) (vratio_pre_l * dml_pow(2, 19));
	disp_dlg_regs->vratio_prefetch_c = (unsigned int) (vratio_pre_c * dml_pow(2, 19));

	disp_dlg_regs->refcyc_per_pte_group_vblank_l =
			(unsigned int) (dst_y_per_row_vblank * (double) htotal
					* ref_freq_to_pix_freq / (double) dpte_groups_per_row_ub_l);
	ASSERT(disp_dlg_regs->refcyc_per_pte_group_vblank_l < (unsigned int) dml_pow(2, 13));

	if (dual_plane) {
		disp_dlg_regs->refcyc_per_pte_group_vblank_c = (unsigned int) (dst_y_per_row_vblank
				* (double) htotal * ref_freq_to_pix_freq
				/ (double) dpte_groups_per_row_ub_c);
		ASSERT(disp_dlg_regs->refcyc_per_pte_group_vblank_c
						< (unsigned int) dml_pow(2, 13));
	}

	disp_dlg_regs->refcyc_per_meta_chunk_vblank_l =
			(unsigned int) (dst_y_per_row_vblank * (double) htotal
					* ref_freq_to_pix_freq / (double) meta_chunks_per_row_ub_l);
	ASSERT(disp_dlg_regs->refcyc_per_meta_chunk_vblank_l < (unsigned int) dml_pow(2, 13));

	disp_dlg_regs->refcyc_per_meta_chunk_vblank_c =
			disp_dlg_regs->refcyc_per_meta_chunk_vblank_l; // dcc for 4:2:0 is not supported in dcn1.0.  assigned to be the same as _l for now

	disp_dlg_regs->refcyc_per_pte_group_flip_l = (unsigned int) (dst_y_per_row_flip * htotal
			* ref_freq_to_pix_freq) / dpte_groups_per_row_ub_l;
	disp_dlg_regs->refcyc_per_meta_chunk_flip_l = (unsigned int) (dst_y_per_row_flip * htotal
			* ref_freq_to_pix_freq) / meta_chunks_per_row_ub_l;

	if (dual_plane) {
		disp_dlg_regs->refcyc_per_pte_group_flip_c = (unsigned int) (dst_y_per_row_flip
				* htotal * ref_freq_to_pix_freq) / dpte_groups_per_row_ub_c;
		disp_dlg_regs->refcyc_per_meta_chunk_flip_c = (unsigned int) (dst_y_per_row_flip
				* htotal * ref_freq_to_pix_freq) / meta_chunks_per_row_ub_c;
	}

	disp_dlg_regs->dst_y_per_pte_row_nom_l = (unsigned int) ((double) dpte_row_height_l
			/ (double) vratio_l * dml_pow(2, 2));
	ASSERT(disp_dlg_regs->dst_y_per_pte_row_nom_l < (unsigned int) dml_pow(2, 17));

	if (dual_plane) {
		disp_dlg_regs->dst_y_per_pte_row_nom_c = (unsigned int) ((double) dpte_row_height_c
				/ (double) vratio_c * dml_pow(2, 2));
		if (disp_dlg_regs->dst_y_per_pte_row_nom_c >= (unsigned int) dml_pow(2, 17)) {
			dml_print("DML_DLG: %s: Warning dst_y_per_pte_row_nom_c %u larger than supported by register format U15.2 %u\n",
					__func__,
					disp_dlg_regs->dst_y_per_pte_row_nom_c,
					(unsigned int) dml_pow(2, 17) - 1);
		}
	}

	disp_dlg_regs->dst_y_per_meta_row_nom_l = (unsigned int) ((double) meta_row_height_l
			/ (double) vratio_l * dml_pow(2, 2));
	ASSERT(disp_dlg_regs->dst_y_per_meta_row_nom_l < (unsigned int) dml_pow(2, 17));

	disp_dlg_regs->dst_y_per_meta_row_nom_c = disp_dlg_regs->dst_y_per_meta_row_nom_l; // TODO: dcc for 4:2:0 is not supported in dcn1.0.  assigned to be the same as _l for now

	disp_dlg_regs->refcyc_per_pte_group_nom_l = (unsigned int) ((double) dpte_row_height_l
			/ (double) vratio_l * (double) htotal * ref_freq_to_pix_freq
			/ (double) dpte_groups_per_row_ub_l);
	if (disp_dlg_regs->refcyc_per_pte_group_nom_l >= (unsigned int) dml_pow(2, 23))
		disp_dlg_regs->refcyc_per_pte_group_nom_l = dml_pow(2, 23) - 1;
	disp_dlg_regs->refcyc_per_meta_chunk_nom_l = (unsigned int) ((double) meta_row_height_l
			/ (double) vratio_l * (double) htotal * ref_freq_to_pix_freq
			/ (double) meta_chunks_per_row_ub_l);
	if (disp_dlg_regs->refcyc_per_meta_chunk_nom_l >= (unsigned int) dml_pow(2, 23))
		disp_dlg_regs->refcyc_per_meta_chunk_nom_l = dml_pow(2, 23) - 1;

	if (dual_plane) {
		disp_dlg_regs->refcyc_per_pte_group_nom_c =
				(unsigned int) ((double) dpte_row_height_c / (double) vratio_c
						* (double) htotal * ref_freq_to_pix_freq
						/ (double) dpte_groups_per_row_ub_c);
		if (disp_dlg_regs->refcyc_per_pte_group_nom_c >= (unsigned int) dml_pow(2, 23))
			disp_dlg_regs->refcyc_per_pte_group_nom_c = dml_pow(2, 23) - 1;

		// TODO: Is this the right calculation? Does htotal need to be halved?
		disp_dlg_regs->refcyc_per_meta_chunk_nom_c =
				(unsigned int) ((double) meta_row_height_c / (double) vratio_c
						* (double) htotal * ref_freq_to_pix_freq
						/ (double) meta_chunks_per_row_ub_c);
		if (disp_dlg_regs->refcyc_per_meta_chunk_nom_c >= (unsigned int) dml_pow(2, 23))
			disp_dlg_regs->refcyc_per_meta_chunk_nom_c = dml_pow(2, 23) - 1;
	}

	disp_dlg_regs->refcyc_per_line_delivery_pre_l = (unsigned int) dml_floor(refcyc_per_line_delivery_pre_l,
			1);
	disp_dlg_regs->refcyc_per_line_delivery_l = (unsigned int) dml_floor(refcyc_per_line_delivery_l,
			1);
	ASSERT(disp_dlg_regs->refcyc_per_line_delivery_pre_l < (unsigned int) dml_pow(2, 13));
	ASSERT(disp_dlg_regs->refcyc_per_line_delivery_l < (unsigned int) dml_pow(2, 13));

	disp_dlg_regs->refcyc_per_line_delivery_pre_c = (unsigned int) dml_floor(refcyc_per_line_delivery_pre_c,
			1);
	disp_dlg_regs->refcyc_per_line_delivery_c = (unsigned int) dml_floor(refcyc_per_line_delivery_c,
			1);
	ASSERT(disp_dlg_regs->refcyc_per_line_delivery_pre_c < (unsigned int) dml_pow(2, 13));
	ASSERT(disp_dlg_regs->refcyc_per_line_delivery_c < (unsigned int) dml_pow(2, 13));

	disp_dlg_regs->chunk_hdl_adjust_cur0 = 3;
	disp_dlg_regs->dst_y_offset_cur0 = 0;
	disp_dlg_regs->chunk_hdl_adjust_cur1 = 3;
	disp_dlg_regs->dst_y_offset_cur1 = 0;

	disp_dlg_regs->xfc_reg_transfer_delay = xfc_transfer_delay;
	disp_dlg_regs->xfc_reg_precharge_delay = xfc_precharge_delay;
	disp_dlg_regs->xfc_reg_remote_surface_flip_latency = xfc_remote_surface_flip_latency;
	disp_dlg_regs->xfc_reg_prefetch_margin = dml_ceil(xfc_prefetch_margin * refclk_freq_in_mhz,
			1);

	// slave has to have this value also set to off
	if (src->xfc_enable && !src->xfc_slave)
		disp_dlg_regs->dst_y_delta_drq_limit = dml_ceil(xfc_dst_y_delta_drq_limit, 1);
	else
		disp_dlg_regs->dst_y_delta_drq_limit = 0x7fff; // off

	disp_ttu_regs->refcyc_per_req_delivery_pre_l = (unsigned int) (refcyc_per_req_delivery_pre_l
			* dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_l = (unsigned int) (refcyc_per_req_delivery_l
			* dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_pre_c = (unsigned int) (refcyc_per_req_delivery_pre_c
			* dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_c = (unsigned int) (refcyc_per_req_delivery_c
			* dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_pre_cur0 =
			(unsigned int) (refcyc_per_req_delivery_pre_cur0 * dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_cur0 = (unsigned int) (refcyc_per_req_delivery_cur0
			* dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_pre_cur1 =
			(unsigned int) (refcyc_per_req_delivery_pre_cur1 * dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_cur1 = (unsigned int) (refcyc_per_req_delivery_cur1
			* dml_pow(2, 10));
	disp_ttu_regs->qos_level_low_wm = 0;
	ASSERT(disp_ttu_regs->qos_level_low_wm < dml_pow(2, 14));
	disp_ttu_regs->qos_level_high_wm = (unsigned int) (4.0 * (double) htotal
			* ref_freq_to_pix_freq);
	/*ASSERT(disp_ttu_regs->qos_level_high_wm < dml_pow(2, 14));*/

	disp_ttu_regs->qos_level_flip = 14;
	disp_ttu_regs->qos_level_fixed_l = 8;
	disp_ttu_regs->qos_level_fixed_c = 8;
	disp_ttu_regs->qos_level_fixed_cur0 = 8;
	disp_ttu_regs->qos_ramp_disable_l = 0;
	disp_ttu_regs->qos_ramp_disable_c = 0;
	disp_ttu_regs->qos_ramp_disable_cur0 = 0;

	disp_ttu_regs->min_ttu_vblank = min_ttu_vblank * refclk_freq_in_mhz;
	ASSERT(disp_ttu_regs->min_ttu_vblank < dml_pow(2, 24));

	print__ttu_regs_st(mode_lib, *disp_ttu_regs);
	print__dlg_regs_st(mode_lib, *disp_dlg_regs);
}

void dml20_rq_dlg_get_dlg_reg(struct display_mode_lib *mode_lib,
		display_dlg_regs_st *dlg_regs,
		display_ttu_regs_st *ttu_regs,
		display_e2e_pipe_params_st *e2e_pipe_param,
		const unsigned int num_pipes,
		const unsigned int pipe_idx,
		const bool cstate_en,
		const bool pstate_en,
		const bool vm_en,
		const bool ignore_viewport_pos,
		const bool immediate_flip_support)
{
	display_rq_params_st rq_param = {0};
	display_dlg_sys_params_st dlg_sys_param = {0};

	// Get watermark and Tex.
	dlg_sys_param.t_urg_wm_us = get_wm_urgent(mode_lib, e2e_pipe_param, num_pipes);
	dlg_sys_param.deepsleep_dcfclk_mhz = get_clk_dcf_deepsleep(mode_lib,
			e2e_pipe_param,
			num_pipes);
	dlg_sys_param.t_extra_us = get_urgent_extra_latency(mode_lib, e2e_pipe_param, num_pipes);
	dlg_sys_param.mem_trip_us = get_wm_memory_trip(mode_lib, e2e_pipe_param, num_pipes);
	dlg_sys_param.t_mclk_wm_us = get_wm_dram_clock_change(mode_lib, e2e_pipe_param, num_pipes);
	dlg_sys_param.t_sr_wm_us = get_wm_stutter_enter_exit(mode_lib, e2e_pipe_param, num_pipes);
	dlg_sys_param.total_flip_bw = get_total_immediate_flip_bw(mode_lib,
			e2e_pipe_param,
			num_pipes);
	dlg_sys_param.total_flip_bytes = get_total_immediate_flip_bytes(mode_lib,
			e2e_pipe_param,
			num_pipes);
	dlg_sys_param.t_srx_delay_us = mode_lib->ip.dcfclk_cstate_latency
			/ dlg_sys_param.deepsleep_dcfclk_mhz; // TODO: Deprecated

	print__dlg_sys_params_st(mode_lib, dlg_sys_param);

	// system parameter calculation done

	dml_print("DML_DLG: Calculation for pipe[%d] start\n\n", pipe_idx);
	dml20_rq_dlg_get_rq_params(mode_lib, &rq_param, e2e_pipe_param[pipe_idx].pipe.src);
	dml20_rq_dlg_get_dlg_params(mode_lib,
			e2e_pipe_param,
			num_pipes,
			pipe_idx,
			dlg_regs,
			ttu_regs,
			rq_param.dlg,
			dlg_sys_param,
			cstate_en,
			pstate_en);
	dml_print("DML_DLG: Calculation for pipe[%d] end\n", pipe_idx);
}

static void calculate_ttu_cursor(struct display_mode_lib *mode_lib,
		double *refcyc_per_req_delivery_pre_cur,
		double *refcyc_per_req_delivery_cur,
		double refclk_freq_in_mhz,
		double ref_freq_to_pix_freq,
		double hscale_pixel_rate_l,
		double hscl_ratio,
		double vratio_pre_l,
		double vratio_l,
		unsigned int cur_width,
		enum cursor_bpp cur_bpp)
{
	unsigned int cur_src_width = cur_width;
	unsigned int cur_req_size = 0;
	unsigned int cur_req_width = 0;
	double cur_width_ub = 0.0;
	double cur_req_per_width = 0.0;
	double hactive_cur = 0.0;

	ASSERT(cur_src_width <= 256);

	*refcyc_per_req_delivery_pre_cur = 0.0;
	*refcyc_per_req_delivery_cur = 0.0;
	if (cur_src_width > 0) {
		unsigned int cur_bit_per_pixel = 0;

		if (cur_bpp == dm_cur_2bit) {
			cur_req_size = 64; // byte
			cur_bit_per_pixel = 2;
		} else { // 32bit
			cur_bit_per_pixel = 32;
			if (cur_src_width >= 1 && cur_src_width <= 16)
				cur_req_size = 64;
			else if (cur_src_width >= 17 && cur_src_width <= 31)
				cur_req_size = 128;
			else
				cur_req_size = 256;
		}

		cur_req_width = (double) cur_req_size / ((double) cur_bit_per_pixel / 8.0);
		cur_width_ub = dml_ceil((double) cur_src_width / (double) cur_req_width, 1)
				* (double) cur_req_width;
		cur_req_per_width = cur_width_ub / (double) cur_req_width;
		hactive_cur = (double) cur_src_width / hscl_ratio; // FIXME: oswin to think about what to do for cursor

		if (vratio_pre_l <= 1.0) {
			*refcyc_per_req_delivery_pre_cur = hactive_cur * ref_freq_to_pix_freq
					/ (double) cur_req_per_width;
		} else {
			*refcyc_per_req_delivery_pre_cur = (double) refclk_freq_in_mhz
					* (double) cur_src_width / hscale_pixel_rate_l
					/ (double) cur_req_per_width;
		}

		ASSERT(*refcyc_per_req_delivery_pre_cur < dml_pow(2, 13));

		if (vratio_l <= 1.0) {
			*refcyc_per_req_delivery_cur = hactive_cur * ref_freq_to_pix_freq
					/ (double) cur_req_per_width;
		} else {
			*refcyc_per_req_delivery_cur = (double) refclk_freq_in_mhz
					* (double) cur_src_width / hscale_pixel_rate_l
					/ (double) cur_req_per_width;
		}

		dml_print("DML_DLG: %s: cur_req_width                     = %d\n",
				__func__,
				cur_req_width);
		dml_print("DML_DLG: %s: cur_width_ub                      = %3.2f\n",
				__func__,
				cur_width_ub);
		dml_print("DML_DLG: %s: cur_req_per_width                 = %3.2f\n",
				__func__,
				cur_req_per_width);
		dml_print("DML_DLG: %s: hactive_cur                       = %3.2f\n",
				__func__,
				hactive_cur);
		dml_print("DML_DLG: %s: refcyc_per_req_delivery_pre_cur   = %3.2f\n",
				__func__,
				*refcyc_per_req_delivery_pre_cur);
		dml_print("DML_DLG: %s: refcyc_per_req_delivery_cur       = %3.2f\n",
				__func__,
				*refcyc_per_req_delivery_cur);

		ASSERT(*refcyc_per_req_delivery_cur < dml_pow(2, 13));
	}
}
