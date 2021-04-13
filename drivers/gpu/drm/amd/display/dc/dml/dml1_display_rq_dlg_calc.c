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

#include "dml1_display_rq_dlg_calc.h"
#include "display_mode_lib.h"

#include "dml_inline_defs.h"

/*
 * NOTE:
 *   This file is gcc-parseable HW gospel, coming straight from HW engineers.
 *
 * It doesn't adhere to Linux kernel style and sometimes will do things in odd
 * ways. Unless there is something clearly wrong with it the code should
 * remain as-is as it provides us with a guarantee from HW that it is correct.
 */

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

static void get_blk256_size(
		unsigned int *blk256_width,
		unsigned int *blk256_height,
		unsigned int bytes_per_element)
{
	if (bytes_per_element == 1) {
		*blk256_width = 16;
		*blk256_height = 16;
	} else if (bytes_per_element == 2) {
		*blk256_width = 16;
		*blk256_height = 8;
	} else if (bytes_per_element == 4) {
		*blk256_width = 8;
		*blk256_height = 8;
	} else if (bytes_per_element == 8) {
		*blk256_width = 8;
		*blk256_height = 4;
	}
}

static double get_refcyc_per_delivery(
		struct display_mode_lib *mode_lib,
		double refclk_freq_in_mhz,
		double pclk_freq_in_mhz,
		unsigned int recout_width,
		double vratio,
		double hscale_pixel_rate,
		unsigned int delivery_width,
		unsigned int req_per_swath_ub)
{
	double refcyc_per_delivery = 0.0;

	if (vratio <= 1.0) {
		refcyc_per_delivery = (double) refclk_freq_in_mhz * (double) recout_width
				/ pclk_freq_in_mhz / (double) req_per_swath_ub;
	} else {
		refcyc_per_delivery = (double) refclk_freq_in_mhz * (double) delivery_width
				/ (double) hscale_pixel_rate / (double) req_per_swath_ub;
	}

	DTRACE("DLG: %s: refclk_freq_in_mhz = %3.2f", __func__, refclk_freq_in_mhz);
	DTRACE("DLG: %s: pclk_freq_in_mhz   = %3.2f", __func__, pclk_freq_in_mhz);
	DTRACE("DLG: %s: recout_width       = %d", __func__, recout_width);
	DTRACE("DLG: %s: vratio             = %3.2f", __func__, vratio);
	DTRACE("DLG: %s: req_per_swath_ub   = %d", __func__, req_per_swath_ub);
	DTRACE("DLG: %s: refcyc_per_delivery= %3.2f", __func__, refcyc_per_delivery);

	return refcyc_per_delivery;

}

static double get_vratio_pre(
		struct display_mode_lib *mode_lib,
		unsigned int max_num_sw,
		unsigned int max_partial_sw,
		unsigned int swath_height,
		double vinit,
		double l_sw)
{
	double prefill = dml_floor(vinit, 1);
	double vratio_pre = 1.0;

	vratio_pre = (max_num_sw * swath_height + max_partial_sw) / l_sw;

	if (swath_height > 4) {
		double tmp0 = (max_num_sw * swath_height) / (l_sw - (prefill - 3.0) / 2.0);

		if (tmp0 > vratio_pre)
			vratio_pre = tmp0;
	}

	DTRACE("DLG: %s: max_num_sw        = %0d", __func__, max_num_sw);
	DTRACE("DLG: %s: max_partial_sw    = %0d", __func__, max_partial_sw);
	DTRACE("DLG: %s: swath_height      = %0d", __func__, swath_height);
	DTRACE("DLG: %s: vinit             = %3.2f", __func__, vinit);
	DTRACE("DLG: %s: vratio_pre        = %3.2f", __func__, vratio_pre);

	if (vratio_pre < 1.0) {
		DTRACE("WARNING_DLG: %s:  vratio_pre=%3.2f < 1.0, set to 1.0", __func__, vratio_pre);
		vratio_pre = 1.0;
	}

	if (vratio_pre > 4.0) {
		DTRACE(
				"WARNING_DLG: %s:  vratio_pre=%3.2f > 4.0 (max scaling ratio). set to 4.0",
				__func__,
				vratio_pre);
		vratio_pre = 4.0;
	}

	return vratio_pre;
}

static void get_swath_need(
		struct display_mode_lib *mode_lib,
		unsigned int *max_num_sw,
		unsigned int *max_partial_sw,
		unsigned int swath_height,
		double vinit)
{
	double prefill = dml_floor(vinit, 1);
	unsigned int max_partial_sw_int;

	DTRACE("DLG: %s: swath_height      = %0d", __func__, swath_height);
	DTRACE("DLG: %s: vinit             = %3.2f", __func__, vinit);

	ASSERT(prefill > 0.0 && prefill <= 8.0);

	*max_num_sw = (unsigned int) (dml_ceil((prefill - 1.0) / (double) swath_height, 1) + 1.0); /* prefill has to be >= 1 */
	max_partial_sw_int =
			(prefill == 1) ?
					(swath_height - 1) :
					((unsigned int) (prefill - 2.0) % swath_height);
	*max_partial_sw = (max_partial_sw_int < 1) ? 1 : max_partial_sw_int; /* ensure minimum of 1 is used */

	DTRACE("DLG: %s: max_num_sw        = %0d", __func__, *max_num_sw);
	DTRACE("DLG: %s: max_partial_sw    = %0d", __func__, *max_partial_sw);
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

static void extract_rq_sizing_regs(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_data_rq_regs_st *rq_regs,
		const struct _vcs_dpi_display_data_rq_sizing_params_st rq_sizing)
{
	DTRACE("DLG: %s: rq_sizing param", __func__);
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

void dml1_extract_rq_regs(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_rq_regs_st *rq_regs,
		const struct _vcs_dpi_display_rq_params_st rq_param)
{
	unsigned int detile_buf_size_in_bytes = mode_lib->ip.det_buffer_size_kbytes * 1024;
	unsigned int detile_buf_plane1_addr = 0;

	extract_rq_sizing_regs(mode_lib, &(rq_regs->rq_regs_l), rq_param.sizing.rq_l);
	if (rq_param.yuv420)
		extract_rq_sizing_regs(mode_lib, &(rq_regs->rq_regs_c), rq_param.sizing.rq_c);

	rq_regs->rq_regs_l.swath_height = dml_log2(rq_param.dlg.rq_l.swath_height);
	rq_regs->rq_regs_c.swath_height = dml_log2(rq_param.dlg.rq_c.swath_height);

	/* TODO: take the max between luma, chroma chunk size?
	 * okay for now, as we are setting chunk_bytes to 8kb anyways
	 */
	if (rq_param.sizing.rq_l.chunk_bytes >= 32 * 1024) { /*32kb */
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
			detile_buf_plane1_addr = (detile_buf_size_in_bytes / 2.0 / 64.0); /* half to chroma */
		} else {
			detile_buf_plane1_addr = dml_round_to_multiple(
					(unsigned int) ((2.0 * detile_buf_size_in_bytes) / 3.0),
					256,
					0) / 64.0; /* 2/3 to chroma */
		}
	}
	rq_regs->plane1_base_address = detile_buf_plane1_addr;
}

static void handle_det_buf_split(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_rq_params_st *rq_param,
		const struct _vcs_dpi_display_pipe_source_params_st pipe_src_param)
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
		full_swath_bytes_packed_l = dml_round_to_multiple(
				rq_param->misc.rq_l.full_swath_bytes * 2 / 3,
				256,
				1) + 256;
		full_swath_bytes_packed_c = dml_round_to_multiple(
				rq_param->misc.rq_c.full_swath_bytes * 2 / 3,
				256,
				1) + 256;
	}

	if (rq_param->yuv420) {
		total_swath_bytes = 2 * full_swath_bytes_packed_l + 2 * full_swath_bytes_packed_c;

		if (total_swath_bytes <= detile_buf_size_in_bytes) { /*full 256b request */
			req128_l = 0;
			req128_c = 0;
			swath_bytes_l = full_swath_bytes_packed_l;
			swath_bytes_c = full_swath_bytes_packed_c;
		} else { /*128b request (for luma only for yuv420 8bpc) */
			req128_l = 1;
			req128_c = 0;
			swath_bytes_l = full_swath_bytes_packed_l / 2;
			swath_bytes_c = full_swath_bytes_packed_c;
		}

		/* Bug workaround, luma and chroma req size needs to be the same. (see: DEGVIDCN10-137)
		 * TODO: Remove after rtl fix
		 */
		if (req128_l == 1) {
			req128_c = 1;
			DTRACE("DLG: %s: bug workaround DEGVIDCN10-137", __func__);
		}

		/* Note: assumption, the config that pass in will fit into
		 *       the detiled buffer.
		 */
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
	} else {
		unsigned int swath_height_l;
		unsigned int swath_height_c;

		if (!surf_vert) {
			swath_height_l = rq_param->misc.rq_l.blk256_height;
			swath_height_c = rq_param->misc.rq_c.blk256_height;
		} else {
			swath_height_l = rq_param->misc.rq_l.blk256_width;
			swath_height_c = rq_param->misc.rq_c.blk256_width;
		}

		if (swath_height_l > 0)
			log2_swath_height_l = dml_log2(swath_height_l);

		if (req128_l && log2_swath_height_l > 0)
			log2_swath_height_l -= 1;

		if (swath_height_c > 0)
			log2_swath_height_c = dml_log2(swath_height_c);

		if (req128_c && log2_swath_height_c > 0)
			log2_swath_height_c -= 1;
	}

	rq_param->dlg.rq_l.swath_height = 1 << log2_swath_height_l;
	rq_param->dlg.rq_c.swath_height = 1 << log2_swath_height_c;

	DTRACE("DLG: %s: req128_l = %0d", __func__, req128_l);
	DTRACE("DLG: %s: req128_c = %0d", __func__, req128_c);
	DTRACE("DLG: %s: full_swath_bytes_packed_l = %0d", __func__, full_swath_bytes_packed_l);
	DTRACE("DLG: %s: full_swath_bytes_packed_c = %0d", __func__, full_swath_bytes_packed_c);
}

/* Need refactor. */
static void dml1_rq_dlg_get_row_heights(
		struct display_mode_lib *mode_lib,
		unsigned int *o_dpte_row_height,
		unsigned int *o_meta_row_height,
		unsigned int vp_width,
		unsigned int data_pitch,
		int source_format,
		int tiling,
		int macro_tile_size,
		int source_scan,
		int is_chroma)
{
	bool surf_linear = (tiling == dm_sw_linear);
	bool surf_vert = (source_scan == dm_vert);

	unsigned int bytes_per_element = get_bytes_per_element(
			(enum source_format_class) source_format,
			is_chroma);
	unsigned int log2_bytes_per_element = dml_log2(bytes_per_element);
	unsigned int blk256_width = 0;
	unsigned int blk256_height = 0;

	unsigned int log2_blk256_height;
	unsigned int blk_bytes;
	unsigned int log2_blk_bytes;
	unsigned int log2_blk_height;
	unsigned int log2_blk_width;
	unsigned int log2_meta_req_bytes;
	unsigned int log2_meta_req_height;
	unsigned int log2_meta_req_width;
	unsigned int log2_meta_row_height;
	unsigned int log2_vmpg_bytes;
	unsigned int dpte_buf_in_pte_reqs;
	unsigned int log2_vmpg_height;
	unsigned int log2_vmpg_width;
	unsigned int log2_dpte_req_height_ptes;
	unsigned int log2_dpte_req_width_ptes;
	unsigned int log2_dpte_req_height;
	unsigned int log2_dpte_req_width;
	unsigned int log2_dpte_row_height_linear;
	unsigned int log2_dpte_row_height;
	unsigned int dpte_req_width;

	if (surf_linear) {
		blk256_width = 256;
		blk256_height = 1;
	} else {
		get_blk256_size(&blk256_width, &blk256_height, bytes_per_element);
	}

	log2_blk256_height = dml_log2((double) blk256_height);
	blk_bytes = surf_linear ?
			256 : get_blk_size_bytes((enum source_macro_tile_size) macro_tile_size);
	log2_blk_bytes = dml_log2((double) blk_bytes);
	log2_blk_height = 0;
	log2_blk_width = 0;

	/* remember log rule
	 * "+" in log is multiply
	 * "-" in log is divide
	 * "/2" is like square root
	 * blk is vertical biased
	 */
	if (tiling != dm_sw_linear)
		log2_blk_height = log2_blk256_height
				+ dml_ceil((double) (log2_blk_bytes - 8) / 2.0, 1);
	else
		log2_blk_height = 0; /* blk height of 1 */

	log2_blk_width = log2_blk_bytes - log2_bytes_per_element - log2_blk_height;

	/* ------- */
	/* meta    */
	/* ------- */
	log2_meta_req_bytes = 6; /* meta request is 64b and is 8x8byte meta element */

	/* each 64b meta request for dcn is 8x8 meta elements and
	 * a meta element covers one 256b block of the the data surface.
	 */
	log2_meta_req_height = log2_blk256_height + 3; /* meta req is 8x8 */
	log2_meta_req_width = log2_meta_req_bytes + 8 - log2_bytes_per_element
			- log2_meta_req_height;
	log2_meta_row_height = 0;

	/* the dimensions of a meta row are meta_row_width x meta_row_height in elements.
	 * calculate upper bound of the meta_row_width
	 */
	if (!surf_vert)
		log2_meta_row_height = log2_meta_req_height;
	else
		log2_meta_row_height = log2_meta_req_width;

	*o_meta_row_height = 1 << log2_meta_row_height;

	/* ------ */
	/* dpte   */
	/* ------ */
	log2_vmpg_bytes = dml_log2(mode_lib->soc.vmm_page_size_bytes);
	dpte_buf_in_pte_reqs = mode_lib->ip.dpte_buffer_size_in_pte_reqs_luma;

	log2_vmpg_height = 0;
	log2_vmpg_width = 0;
	log2_dpte_req_height_ptes = 0;
	log2_dpte_req_width_ptes = 0;
	log2_dpte_req_height = 0;
	log2_dpte_req_width = 0;
	log2_dpte_row_height_linear = 0;
	log2_dpte_row_height = 0;
	dpte_req_width = 0; /* 64b dpte req width in data element */

	if (surf_linear)
		log2_vmpg_height = 0; /* one line high */
	else
		log2_vmpg_height = (log2_vmpg_bytes - 8) / 2 + log2_blk256_height;
	log2_vmpg_width = log2_vmpg_bytes - log2_bytes_per_element - log2_vmpg_height;

	/* only 3 possible shapes for dpte request in dimensions of ptes: 8x1, 4x2, 2x4. */
	if (log2_blk_bytes <= log2_vmpg_bytes)
		log2_dpte_req_height_ptes = 0;
	else if (log2_blk_height - log2_vmpg_height >= 2)
		log2_dpte_req_height_ptes = 2;
	else
		log2_dpte_req_height_ptes = log2_blk_height - log2_vmpg_height;
	log2_dpte_req_width_ptes = 3 - log2_dpte_req_height_ptes;

	ASSERT((log2_dpte_req_width_ptes == 3 && log2_dpte_req_height_ptes == 0) || /* 8x1 */
			(log2_dpte_req_width_ptes == 2 && log2_dpte_req_height_ptes == 1) || /* 4x2 */
			(log2_dpte_req_width_ptes == 1 && log2_dpte_req_height_ptes == 2)); /* 2x4 */

	/* the dpte request dimensions in data elements is dpte_req_width x dpte_req_height
	 * log2_wmpg_width is how much 1 pte represent, now trying to calculate how much 64b pte req represent
	 */
	log2_dpte_req_height = log2_vmpg_height + log2_dpte_req_height_ptes;
	log2_dpte_req_width = log2_vmpg_width + log2_dpte_req_width_ptes;
	dpte_req_width = 1 << log2_dpte_req_width;

	/* calculate pitch dpte row buffer can hold
	 * round the result down to a power of two.
	 */
	if (surf_linear) {
		log2_dpte_row_height_linear = dml_floor(
				dml_log2(dpte_buf_in_pte_reqs * dpte_req_width / data_pitch),
				1);

		ASSERT(log2_dpte_row_height_linear >= 3);

		if (log2_dpte_row_height_linear > 7)
			log2_dpte_row_height_linear = 7;

		log2_dpte_row_height = log2_dpte_row_height_linear;
	} else {
		/* the upper bound of the dpte_row_width without dependency on viewport position follows.  */
		if (!surf_vert)
			log2_dpte_row_height = log2_dpte_req_height;
		else
			log2_dpte_row_height =
					(log2_blk_width < log2_dpte_req_width) ?
							log2_blk_width : log2_dpte_req_width;
	}

	/* From programming guide:
	 * There is a special case of saving only half of ptes returned due to buffer space limits.
	 * this case applies to 4 and 8bpe in horizontal access of a vp_width greater than 2560+16
	 * when the pte request is 2x4 ptes (which happens when vmpg_bytes =4kb and tile blk_bytes >=64kb).
	 */
	if (!surf_vert && vp_width > (2560 + 16) && bytes_per_element >= 4 && log2_vmpg_bytes == 12
			&& log2_blk_bytes >= 16)
		log2_dpte_row_height = log2_dpte_row_height - 1; /*half of the full height */

	*o_dpte_row_height = 1 << log2_dpte_row_height;
}

static void get_surf_rq_param(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_data_rq_sizing_params_st *rq_sizing_param,
		struct _vcs_dpi_display_data_rq_dlg_params_st *rq_dlg_param,
		struct _vcs_dpi_display_data_rq_misc_params_st *rq_misc_param,
		const struct _vcs_dpi_display_pipe_source_params_st pipe_src_param,
		bool is_chroma)
{
	bool mode_422 = 0;
	unsigned int vp_width = 0;
	unsigned int vp_height = 0;
	unsigned int data_pitch = 0;
	unsigned int meta_pitch = 0;
	unsigned int ppe = mode_422 ? 2 : 1;
	bool surf_linear;
	bool surf_vert;
	unsigned int bytes_per_element;
	unsigned int log2_bytes_per_element;
	unsigned int blk256_width;
	unsigned int blk256_height;
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
	unsigned int log2_vmpg_bytes;
	unsigned int dpte_buf_in_pte_reqs;
	unsigned int log2_vmpg_height;
	unsigned int log2_vmpg_width;
	unsigned int log2_dpte_req_height_ptes;
	unsigned int log2_dpte_req_width_ptes;
	unsigned int log2_dpte_req_height;
	unsigned int log2_dpte_req_width;
	unsigned int log2_dpte_row_height_linear;
	unsigned int log2_dpte_row_height;
	unsigned int log2_dpte_group_width;
	unsigned int dpte_row_width_ub;
	unsigned int dpte_row_height;
	unsigned int dpte_req_height;
	unsigned int dpte_req_width;
	unsigned int dpte_group_width;
	unsigned int log2_dpte_group_bytes;
	unsigned int log2_dpte_group_length;
	unsigned int func_meta_row_height, func_dpte_row_height;

	/* TODO check if ppe apply for both luma and chroma in 422 case */
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

	surf_linear = (pipe_src_param.sw_mode == dm_sw_linear);
	surf_vert = (pipe_src_param.source_scan == dm_vert);

	bytes_per_element = get_bytes_per_element(
			(enum source_format_class) pipe_src_param.source_format,
			is_chroma);
	log2_bytes_per_element = dml_log2(bytes_per_element);
	blk256_width = 0;
	blk256_height = 0;

	if (surf_linear) {
		blk256_width = 256 / bytes_per_element;
		blk256_height = 1;
	} else {
		get_blk256_size(&blk256_width, &blk256_height, bytes_per_element);
	}

	DTRACE("DLG: %s: surf_linear        = %d", __func__, surf_linear);
	DTRACE("DLG: %s: surf_vert          = %d", __func__, surf_vert);
	DTRACE("DLG: %s: blk256_width       = %d", __func__, blk256_width);
	DTRACE("DLG: %s: blk256_height      = %d", __func__, blk256_height);

	log2_blk256_width = dml_log2((double) blk256_width);
	log2_blk256_height = dml_log2((double) blk256_height);
	blk_bytes =
			surf_linear ? 256 : get_blk_size_bytes(
							(enum source_macro_tile_size) pipe_src_param.macro_tile_size);
	log2_blk_bytes = dml_log2((double) blk_bytes);
	log2_blk_height = 0;
	log2_blk_width = 0;

	/* remember log rule
	 * "+" in log is multiply
	 * "-" in log is divide
	 * "/2" is like square root
	 * blk is vertical biased
	 */
	if (pipe_src_param.sw_mode != dm_sw_linear)
		log2_blk_height = log2_blk256_height
				+ dml_ceil((double) (log2_blk_bytes - 8) / 2.0, 1);
	else
		log2_blk_height = 0; /* blk height of 1 */

	log2_blk_width = log2_blk_bytes - log2_bytes_per_element - log2_blk_height;

	if (!surf_vert) {
		rq_dlg_param->swath_width_ub = dml_round_to_multiple(vp_width - 1, blk256_width, 1)
				+ blk256_width;
		rq_dlg_param->req_per_swath_ub = rq_dlg_param->swath_width_ub >> log2_blk256_width;
	} else {
		rq_dlg_param->swath_width_ub = dml_round_to_multiple(
				vp_height - 1,
				blk256_height,
				1) + blk256_height;
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

	/* -------  */
	/* meta     */
	/* -------  */
	log2_meta_req_bytes = 6; /* meta request is 64b and is 8x8byte meta element */

	/* each 64b meta request for dcn is 8x8 meta elements and
	 * a meta element covers one 256b block of the the data surface.
	 */
	log2_meta_req_height = log2_blk256_height + 3; /* meta req is 8x8 byte, each byte represent 1 blk256 */
	log2_meta_req_width = log2_meta_req_bytes + 8 - log2_bytes_per_element
			- log2_meta_req_height;
	meta_req_width = 1 << log2_meta_req_width;
	meta_req_height = 1 << log2_meta_req_height;
	log2_meta_row_height = 0;
	meta_row_width_ub = 0;

	/* the dimensions of a meta row are meta_row_width x meta_row_height in elements.
	 * calculate upper bound of the meta_row_width
	 */
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

	log2_meta_chunk_bytes = dml_log2(rq_sizing_param->meta_chunk_bytes);
	log2_meta_chunk_height = log2_meta_row_height;

	/*full sized meta chunk width in unit of data elements */
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
			* (dml_round_to_multiple(vp_height - 1, meta_blk_height, 1)
					+ meta_blk_height) * bytes_per_element / 256;
	vmpg_bytes = mode_lib->soc.vmm_page_size_bytes;
	meta_pte_req_per_frame_ub = (dml_round_to_multiple(
			meta_surface_bytes - vmpg_bytes,
			8 * vmpg_bytes,
			1) + 8 * vmpg_bytes) / (8 * vmpg_bytes);
	meta_pte_bytes_per_frame_ub = meta_pte_req_per_frame_ub * 64; /*64B mpte request */
	rq_dlg_param->meta_pte_bytes_per_frame_ub = meta_pte_bytes_per_frame_ub;

	DTRACE("DLG: %s: meta_blk_height             = %d", __func__, meta_blk_height);
	DTRACE("DLG: %s: meta_blk_width              = %d", __func__, meta_blk_width);
	DTRACE("DLG: %s: meta_surface_bytes          = %d", __func__, meta_surface_bytes);
	DTRACE("DLG: %s: meta_pte_req_per_frame_ub   = %d", __func__, meta_pte_req_per_frame_ub);
	DTRACE("DLG: %s: meta_pte_bytes_per_frame_ub = %d", __func__, meta_pte_bytes_per_frame_ub);

	if (!surf_vert)
		meta_chunk_threshold = 2 * min_meta_chunk_width - meta_req_width;
	else
		meta_chunk_threshold = 2 * min_meta_chunk_width - meta_req_height;

	if (meta_row_remainder <= meta_chunk_threshold)
		rq_dlg_param->meta_chunks_per_row_ub = meta_chunk_per_row_int + 1;
	else
		rq_dlg_param->meta_chunks_per_row_ub = meta_chunk_per_row_int + 2;

	rq_dlg_param->meta_row_height = 1 << log2_meta_row_height;

	/* ------ */
	/* dpte   */
	/* ------ */
	log2_vmpg_bytes = dml_log2(mode_lib->soc.vmm_page_size_bytes);
	dpte_buf_in_pte_reqs = mode_lib->ip.dpte_buffer_size_in_pte_reqs_luma;

	log2_vmpg_height = 0;
	log2_vmpg_width = 0;
	log2_dpte_req_height_ptes = 0;
	log2_dpte_req_width_ptes = 0;
	log2_dpte_req_height = 0;
	log2_dpte_req_width = 0;
	log2_dpte_row_height_linear = 0;
	log2_dpte_row_height = 0;
	log2_dpte_group_width = 0;
	dpte_row_width_ub = 0;
	dpte_row_height = 0;
	dpte_req_height = 0; /* 64b dpte req height in data element */
	dpte_req_width = 0; /* 64b dpte req width in data element */
	dpte_group_width = 0;
	log2_dpte_group_bytes = 0;
	log2_dpte_group_length = 0;

	if (surf_linear)
		log2_vmpg_height = 0; /* one line high */
	else
		log2_vmpg_height = (log2_vmpg_bytes - 8) / 2 + log2_blk256_height;
	log2_vmpg_width = log2_vmpg_bytes - log2_bytes_per_element - log2_vmpg_height;

	/* only 3 possible shapes for dpte request in dimensions of ptes: 8x1, 4x2, 2x4. */
	if (log2_blk_bytes <= log2_vmpg_bytes)
		log2_dpte_req_height_ptes = 0;
	else if (log2_blk_height - log2_vmpg_height >= 2)
		log2_dpte_req_height_ptes = 2;
	else
		log2_dpte_req_height_ptes = log2_blk_height - log2_vmpg_height;
	log2_dpte_req_width_ptes = 3 - log2_dpte_req_height_ptes;

	/* Ensure we only have the 3 shapes */
	ASSERT((log2_dpte_req_width_ptes == 3 && log2_dpte_req_height_ptes == 0) || /* 8x1 */
			(log2_dpte_req_width_ptes == 2 && log2_dpte_req_height_ptes == 1) || /* 4x2 */
			(log2_dpte_req_width_ptes == 1 && log2_dpte_req_height_ptes == 2)); /* 2x4 */

	/* The dpte request dimensions in data elements is dpte_req_width x dpte_req_height
	 * log2_vmpg_width is how much 1 pte represent, now calculating how much a 64b pte req represent
	 * That depends on the pte shape (i.e. 8x1, 4x2, 2x4)
	 */
	log2_dpte_req_height = log2_vmpg_height + log2_dpte_req_height_ptes;
	log2_dpte_req_width = log2_vmpg_width + log2_dpte_req_width_ptes;
	dpte_req_height = 1 << log2_dpte_req_height;
	dpte_req_width = 1 << log2_dpte_req_width;

	/* calculate pitch dpte row buffer can hold
	 * round the result down to a power of two.
	 */
	if (surf_linear) {
		log2_dpte_row_height_linear = dml_floor(
				dml_log2(dpte_buf_in_pte_reqs * dpte_req_width / data_pitch),
				1);

		ASSERT(log2_dpte_row_height_linear >= 3);

		if (log2_dpte_row_height_linear > 7)
			log2_dpte_row_height_linear = 7;

		log2_dpte_row_height = log2_dpte_row_height_linear;
		rq_dlg_param->dpte_row_height = 1 << log2_dpte_row_height;

		/* For linear, the dpte row is pitch dependent and the pte requests wrap at the pitch boundary.
		 * the dpte_row_width_ub is the upper bound of data_pitch*dpte_row_height in elements with this unique buffering.
		 */
		dpte_row_width_ub = dml_round_to_multiple(
				data_pitch * dpte_row_height - 1,
				dpte_req_width,
				1) + dpte_req_width;
		rq_dlg_param->dpte_req_per_row_ub = dpte_row_width_ub / dpte_req_width;
	} else {
		/* for tiled mode, row height is the same as req height and row store up to vp size upper bound */
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
		rq_dlg_param->dpte_row_height = 1 << log2_dpte_row_height;
	}
	rq_dlg_param->dpte_bytes_per_row_ub = rq_dlg_param->dpte_req_per_row_ub * 64;

	/* From programming guide:
	 * There is a special case of saving only half of ptes returned due to buffer space limits.
	 * this case applies to 4 and 8bpe in horizontal access of a vp_width greater than 2560+16
	 * when the pte request is 2x4 ptes (which happens when vmpg_bytes =4kb and tile blk_bytes >=64kb).
	 */
	if (!surf_vert && vp_width > (2560 + 16) && bytes_per_element >= 4 && log2_vmpg_bytes == 12
			&& log2_blk_bytes >= 16) {
		log2_dpte_row_height = log2_dpte_row_height - 1; /*half of the full height */
		rq_dlg_param->dpte_row_height = 1 << log2_dpte_row_height;
	}

	/* the dpte_group_bytes is reduced for the specific case of vertical
	 * access of a tile surface that has dpte request of 8x1 ptes.
	 */
	if (!surf_linear && (log2_dpte_req_height_ptes == 0) && surf_vert) /*reduced, in this case, will have page fault within a group */
		rq_sizing_param->dpte_group_bytes = 512;
	else
		/*full size */
		rq_sizing_param->dpte_group_bytes = 2048;

	/*since pte request size is 64byte, the number of data pte requests per full sized group is as follows.  */
	log2_dpte_group_bytes = dml_log2(rq_sizing_param->dpte_group_bytes);
	log2_dpte_group_length = log2_dpte_group_bytes - 6; /*length in 64b requests  */

	/* full sized data pte group width in elements */
	if (!surf_vert)
		log2_dpte_group_width = log2_dpte_group_length + log2_dpte_req_width;
	else
		log2_dpte_group_width = log2_dpte_group_length + log2_dpte_req_height;

	dpte_group_width = 1 << log2_dpte_group_width;

	/* since dpte groups are only aligned to dpte_req_width and not dpte_group_width,
	 * the upper bound for the dpte groups per row is as follows.
	 */
	rq_dlg_param->dpte_groups_per_row_ub = dml_ceil(
			(double) dpte_row_width_ub / dpte_group_width,
			1);

	dml1_rq_dlg_get_row_heights(
			mode_lib,
			&func_dpte_row_height,
			&func_meta_row_height,
			vp_width,
			data_pitch,
			pipe_src_param.source_format,
			pipe_src_param.sw_mode,
			pipe_src_param.macro_tile_size,
			pipe_src_param.source_scan,
			is_chroma);

	/* Just a check to make sure this function and the new one give the same
	 * result. The standalone get_row_heights() function is based off of the
	 * code in this function so the same changes need to be made to both.
	 */
	if (rq_dlg_param->meta_row_height != func_meta_row_height) {
		DTRACE(
				"MISMATCH: rq_dlg_param->meta_row_height = %d",
				rq_dlg_param->meta_row_height);
		DTRACE("MISMATCH: func_meta_row_height = %d", func_meta_row_height);
		ASSERT(0);
	}

	if (rq_dlg_param->dpte_row_height != func_dpte_row_height) {
		DTRACE(
				"MISMATCH: rq_dlg_param->dpte_row_height = %d",
				rq_dlg_param->dpte_row_height);
		DTRACE("MISMATCH: func_dpte_row_height = %d", func_dpte_row_height);
		ASSERT(0);
	}
}

void dml1_rq_dlg_get_rq_params(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_rq_params_st *rq_param,
		const struct _vcs_dpi_display_pipe_source_params_st pipe_src_param)
{
	/* get param for luma surface */
	rq_param->yuv420 = pipe_src_param.source_format == dm_420_8
			|| pipe_src_param.source_format == dm_420_10;
	rq_param->yuv420_10bpc = pipe_src_param.source_format == dm_420_10;

	get_surf_rq_param(
			mode_lib,
			&(rq_param->sizing.rq_l),
			&(rq_param->dlg.rq_l),
			&(rq_param->misc.rq_l),
			pipe_src_param,
			0);

	if (is_dual_plane((enum source_format_class) pipe_src_param.source_format)) {
		/* get param for chroma surface */
		get_surf_rq_param(
				mode_lib,
				&(rq_param->sizing.rq_c),
				&(rq_param->dlg.rq_c),
				&(rq_param->misc.rq_c),
				pipe_src_param,
				1);
	}

	/* calculate how to split the det buffer space between luma and chroma */
	handle_det_buf_split(mode_lib, rq_param, pipe_src_param);
	print__rq_params_st(mode_lib, *rq_param);
}

/* Note: currently taken in as is.
 * Nice to decouple code from hw register implement and extract code that are repeated for luma and chroma.
 */
void dml1_rq_dlg_get_dlg_params(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_dlg_regs_st *disp_dlg_regs,
		struct _vcs_dpi_display_ttu_regs_st *disp_ttu_regs,
		const struct _vcs_dpi_display_rq_dlg_params_st rq_dlg_param,
		const struct _vcs_dpi_display_dlg_sys_params_st dlg_sys_param,
		const struct _vcs_dpi_display_e2e_pipe_params_st e2e_pipe_param,
		const bool cstate_en,
		const bool pstate_en,
		const bool vm_en,
		const bool iflip_en)
{
	/* Timing */
	unsigned int htotal = e2e_pipe_param.pipe.dest.htotal;
	unsigned int hblank_end = e2e_pipe_param.pipe.dest.hblank_end;
	unsigned int vblank_start = e2e_pipe_param.pipe.dest.vblank_start;
	unsigned int vblank_end = e2e_pipe_param.pipe.dest.vblank_end;
	bool interlaced = e2e_pipe_param.pipe.dest.interlaced;
	unsigned int min_vblank = mode_lib->ip.min_vblank_lines;

	double pclk_freq_in_mhz = e2e_pipe_param.pipe.dest.pixel_rate_mhz;
	double refclk_freq_in_mhz = e2e_pipe_param.clks_cfg.refclk_mhz;
	double dppclk_freq_in_mhz = e2e_pipe_param.clks_cfg.dppclk_mhz;
	double dispclk_freq_in_mhz = e2e_pipe_param.clks_cfg.dispclk_mhz;

	double ref_freq_to_pix_freq;
	double prefetch_xy_calc_in_dcfclk;
	double min_dcfclk_mhz;
	double t_calc_us;
	double min_ttu_vblank;
	double min_dst_y_ttu_vblank;
	unsigned int dlg_vblank_start;
	bool dcc_en;
	bool dual_plane;
	bool mode_422;
	unsigned int access_dir;
	unsigned int bytes_per_element_l;
	unsigned int bytes_per_element_c;
	unsigned int vp_height_l;
	unsigned int vp_width_l;
	unsigned int vp_height_c;
	unsigned int vp_width_c;
	unsigned int htaps_l;
	unsigned int htaps_c;
	double hratios_l;
	double hratios_c;
	double vratio_l;
	double vratio_c;
	double line_time_in_us;
	double vinit_l;
	double vinit_c;
	double vinit_bot_l;
	double vinit_bot_c;
	unsigned int swath_height_l;
	unsigned int swath_width_ub_l;
	unsigned int dpte_bytes_per_row_ub_l;
	unsigned int dpte_groups_per_row_ub_l;
	unsigned int meta_pte_bytes_per_frame_ub_l;
	unsigned int meta_bytes_per_row_ub_l;
	unsigned int swath_height_c;
	unsigned int swath_width_ub_c;
	unsigned int dpte_bytes_per_row_ub_c;
	unsigned int dpte_groups_per_row_ub_c;
	unsigned int meta_chunks_per_row_ub_l;
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
	double line_o;
	double line_setup;
	double line_calc;
	double dst_y_prefetch;
	double t_pre_us;
	unsigned int vm_bytes;
	unsigned int meta_row_bytes;
	unsigned int max_num_sw_l;
	unsigned int max_num_sw_c;
	unsigned int max_partial_sw_l;
	unsigned int max_partial_sw_c;
	double max_vinit_l;
	double max_vinit_c;
	unsigned int lsw_l;
	unsigned int lsw_c;
	unsigned int sw_bytes_ub_l;
	unsigned int sw_bytes_ub_c;
	unsigned int sw_bytes;
	unsigned int dpte_row_bytes;
	double prefetch_bw;
	double flip_bw;
	double t_vm_us;
	double t_r0_us;
	double dst_y_per_vm_vblank;
	double dst_y_per_row_vblank;
	double min_dst_y_per_vm_vblank;
	double min_dst_y_per_row_vblank;
	double lsw;
	double vratio_pre_l;
	double vratio_pre_c;
	unsigned int req_per_swath_ub_l;
	unsigned int req_per_swath_ub_c;
	unsigned int meta_row_height_l;
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
	double refcyc_per_req_delivery_pre_cur0;
	double refcyc_per_req_delivery_cur0;
	unsigned int full_recout_width;
	double hratios_cur0;
	unsigned int cur0_src_width;
	enum cursor_bpp cur0_bpp;
	unsigned int cur0_req_size;
	unsigned int cur0_req_width;
	double cur0_width_ub;
	double cur0_req_per_width;
	double hactive_cur0;

	memset(disp_dlg_regs, 0, sizeof(*disp_dlg_regs));
	memset(disp_ttu_regs, 0, sizeof(*disp_ttu_regs));

	DTRACE("DLG: %s: cstate_en = %d", __func__, cstate_en);
	DTRACE("DLG: %s: pstate_en = %d", __func__, pstate_en);
	DTRACE("DLG: %s: vm_en     = %d", __func__, vm_en);
	DTRACE("DLG: %s: iflip_en  = %d", __func__, iflip_en);

	/* ------------------------- */
	/* Section 1.5.2.1: OTG dependent Params */
	/* ------------------------- */
	DTRACE("DLG: %s: dppclk_freq_in_mhz     = %3.2f", __func__, dppclk_freq_in_mhz);
	DTRACE("DLG: %s: dispclk_freq_in_mhz    = %3.2f", __func__, dispclk_freq_in_mhz);
	DTRACE("DLG: %s: refclk_freq_in_mhz     = %3.2f", __func__, refclk_freq_in_mhz);
	DTRACE("DLG: %s: pclk_freq_in_mhz       = %3.2f", __func__, pclk_freq_in_mhz);
	DTRACE("DLG: %s: interlaced             = %d", __func__, interlaced);

	ref_freq_to_pix_freq = refclk_freq_in_mhz / pclk_freq_in_mhz;
	ASSERT(ref_freq_to_pix_freq < 4.0);
	disp_dlg_regs->ref_freq_to_pix_freq =
			(unsigned int) (ref_freq_to_pix_freq * dml_pow(2, 19));
	disp_dlg_regs->refcyc_per_htotal = (unsigned int) (ref_freq_to_pix_freq * (double) htotal
			* dml_pow(2, 8));
	disp_dlg_regs->refcyc_h_blank_end = (unsigned int) ((double) hblank_end
			* (double) ref_freq_to_pix_freq);
	ASSERT(disp_dlg_regs->refcyc_h_blank_end < (unsigned int) dml_pow(2, 13));
	disp_dlg_regs->dlg_vblank_end = interlaced ? (vblank_end / 2) : vblank_end; /* 15 bits */

	prefetch_xy_calc_in_dcfclk = 24.0; /* TODO: ip_param */
	min_dcfclk_mhz = dlg_sys_param.deepsleep_dcfclk_mhz;
	t_calc_us = prefetch_xy_calc_in_dcfclk / min_dcfclk_mhz;
	min_ttu_vblank = dlg_sys_param.t_urg_wm_us;
	if (cstate_en)
		min_ttu_vblank = dml_max(dlg_sys_param.t_sr_wm_us, min_ttu_vblank);
	if (pstate_en)
		min_ttu_vblank = dml_max(dlg_sys_param.t_mclk_wm_us, min_ttu_vblank);
	min_ttu_vblank = min_ttu_vblank + t_calc_us;

	min_dst_y_ttu_vblank = min_ttu_vblank * pclk_freq_in_mhz / (double) htotal;
	dlg_vblank_start = interlaced ? (vblank_start / 2) : vblank_start;

	disp_dlg_regs->min_dst_y_next_start = (unsigned int) (((double) dlg_vblank_start
			+ min_dst_y_ttu_vblank) * dml_pow(2, 2));
	ASSERT(disp_dlg_regs->min_dst_y_next_start < (unsigned int) dml_pow(2, 18));

	DTRACE("DLG: %s: min_dcfclk_mhz                         = %3.2f", __func__, min_dcfclk_mhz);
	DTRACE("DLG: %s: min_ttu_vblank                         = %3.2f", __func__, min_ttu_vblank);
	DTRACE(
			"DLG: %s: min_dst_y_ttu_vblank                   = %3.2f",
			__func__,
			min_dst_y_ttu_vblank);
	DTRACE("DLG: %s: t_calc_us                              = %3.2f", __func__, t_calc_us);
	DTRACE(
			"DLG: %s: disp_dlg_regs->min_dst_y_next_start    = 0x%0x",
			__func__,
			disp_dlg_regs->min_dst_y_next_start);
	DTRACE(
			"DLG: %s: ref_freq_to_pix_freq                   = %3.2f",
			__func__,
			ref_freq_to_pix_freq);

	/* ------------------------- */
	/* Section 1.5.2.2: Prefetch, Active and TTU  */
	/* ------------------------- */
	/* Prefetch Calc */
	/* Source */
	dcc_en = e2e_pipe_param.pipe.src.dcc;
	dual_plane = is_dual_plane(
			(enum source_format_class) e2e_pipe_param.pipe.src.source_format);
	mode_422 = 0; /* TODO */
	access_dir = (e2e_pipe_param.pipe.src.source_scan == dm_vert); /* vp access direction: horizontal or vertical accessed */
	bytes_per_element_l = get_bytes_per_element(
			(enum source_format_class) e2e_pipe_param.pipe.src.source_format,
			0);
	bytes_per_element_c = get_bytes_per_element(
			(enum source_format_class) e2e_pipe_param.pipe.src.source_format,
			1);
	vp_height_l = e2e_pipe_param.pipe.src.viewport_height;
	vp_width_l = e2e_pipe_param.pipe.src.viewport_width;
	vp_height_c = e2e_pipe_param.pipe.src.viewport_height_c;
	vp_width_c = e2e_pipe_param.pipe.src.viewport_width_c;

	/* Scaling */
	htaps_l = e2e_pipe_param.pipe.scale_taps.htaps;
	htaps_c = e2e_pipe_param.pipe.scale_taps.htaps_c;
	hratios_l = e2e_pipe_param.pipe.scale_ratio_depth.hscl_ratio;
	hratios_c = e2e_pipe_param.pipe.scale_ratio_depth.hscl_ratio_c;
	vratio_l = e2e_pipe_param.pipe.scale_ratio_depth.vscl_ratio;
	vratio_c = e2e_pipe_param.pipe.scale_ratio_depth.vscl_ratio_c;

	line_time_in_us = (htotal / pclk_freq_in_mhz);
	vinit_l = e2e_pipe_param.pipe.scale_ratio_depth.vinit;
	vinit_c = e2e_pipe_param.pipe.scale_ratio_depth.vinit_c;
	vinit_bot_l = e2e_pipe_param.pipe.scale_ratio_depth.vinit_bot;
	vinit_bot_c = e2e_pipe_param.pipe.scale_ratio_depth.vinit_bot_c;

	swath_height_l = rq_dlg_param.rq_l.swath_height;
	swath_width_ub_l = rq_dlg_param.rq_l.swath_width_ub;
	dpte_bytes_per_row_ub_l = rq_dlg_param.rq_l.dpte_bytes_per_row_ub;
	dpte_groups_per_row_ub_l = rq_dlg_param.rq_l.dpte_groups_per_row_ub;
	meta_pte_bytes_per_frame_ub_l = rq_dlg_param.rq_l.meta_pte_bytes_per_frame_ub;
	meta_bytes_per_row_ub_l = rq_dlg_param.rq_l.meta_bytes_per_row_ub;

	swath_height_c = rq_dlg_param.rq_c.swath_height;
	swath_width_ub_c = rq_dlg_param.rq_c.swath_width_ub;
	dpte_bytes_per_row_ub_c = rq_dlg_param.rq_c.dpte_bytes_per_row_ub;
	dpte_groups_per_row_ub_c = rq_dlg_param.rq_c.dpte_groups_per_row_ub;

	meta_chunks_per_row_ub_l = rq_dlg_param.rq_l.meta_chunks_per_row_ub;
	vupdate_offset = e2e_pipe_param.pipe.dest.vupdate_offset;
	vupdate_width = e2e_pipe_param.pipe.dest.vupdate_width;
	vready_offset = e2e_pipe_param.pipe.dest.vready_offset;

	dppclk_delay_subtotal = mode_lib->ip.dppclk_delay_subtotal;
	dispclk_delay_subtotal = mode_lib->ip.dispclk_delay_subtotal;
	pixel_rate_delay_subtotal = dppclk_delay_subtotal * pclk_freq_in_mhz / dppclk_freq_in_mhz
			+ dispclk_delay_subtotal * pclk_freq_in_mhz / dispclk_freq_in_mhz;

	vstartup_start = e2e_pipe_param.pipe.dest.vstartup_start;

	if (interlaced)
		vstartup_start = vstartup_start / 2;

	if (vstartup_start >= min_vblank) {
		DTRACE(
				"WARNING_DLG: %s:  vblank_start=%d vblank_end=%d",
				__func__,
				vblank_start,
				vblank_end);
		DTRACE(
				"WARNING_DLG: %s:  vstartup_start=%d should be less than min_vblank=%d",
				__func__,
				vstartup_start,
				min_vblank);
		min_vblank = vstartup_start + 1;
		DTRACE(
				"WARNING_DLG: %s:  vstartup_start=%d should be less than min_vblank=%d",
				__func__,
				vstartup_start,
				min_vblank);
	}

	dst_x_after_scaler = 0;
	dst_y_after_scaler = 0;

	if (e2e_pipe_param.pipe.src.is_hsplit)
		dst_x_after_scaler = pixel_rate_delay_subtotal
				+ e2e_pipe_param.pipe.dest.recout_width;
	else
		dst_x_after_scaler = pixel_rate_delay_subtotal;

	if (e2e_pipe_param.dout.output_format == dm_420)
		dst_y_after_scaler = 1;
	else
		dst_y_after_scaler = 0;

	if (dst_x_after_scaler >= htotal) {
		dst_x_after_scaler = dst_x_after_scaler - htotal;
		dst_y_after_scaler = dst_y_after_scaler + 1;
	}

	DTRACE("DLG: %s: htotal                                 = %d", __func__, htotal);
	DTRACE(
			"DLG: %s: pixel_rate_delay_subtotal              = %d",
			__func__,
			pixel_rate_delay_subtotal);
	DTRACE("DLG: %s: dst_x_after_scaler                     = %d", __func__, dst_x_after_scaler);
	DTRACE("DLG: %s: dst_y_after_scaler                     = %d", __func__, dst_y_after_scaler);

	line_wait = mode_lib->soc.urgent_latency_us;
	if (cstate_en)
		line_wait = dml_max(mode_lib->soc.sr_enter_plus_exit_time_us, line_wait);
	if (pstate_en)
		line_wait = dml_max(
				mode_lib->soc.dram_clock_change_latency_us
						+ mode_lib->soc.urgent_latency_us,
				line_wait);
	line_wait = line_wait / line_time_in_us;

	line_o = (double) dst_y_after_scaler + dst_x_after_scaler / (double) htotal;
	line_setup = (double) (vupdate_offset + vupdate_width + vready_offset) / (double) htotal;
	line_calc = t_calc_us / line_time_in_us;

	DTRACE(
			"DLG: %s: soc.sr_enter_plus_exit_time_us     = %3.2f",
			__func__,
			(double) mode_lib->soc.sr_enter_plus_exit_time_us);
	DTRACE(
			"DLG: %s: soc.dram_clock_change_latency_us   = %3.2f",
			__func__,
			(double) mode_lib->soc.dram_clock_change_latency_us);
	DTRACE(
			"DLG: %s: soc.urgent_latency_us              = %3.2f",
			__func__,
			mode_lib->soc.urgent_latency_us);

	DTRACE("DLG: %s: swath_height_l     = %d", __func__, swath_height_l);
	if (dual_plane)
		DTRACE("DLG: %s: swath_height_c     = %d", __func__, swath_height_c);

	DTRACE(
			"DLG: %s: t_srx_delay_us     = %3.2f",
			__func__,
			(double) dlg_sys_param.t_srx_delay_us);
	DTRACE("DLG: %s: line_time_in_us    = %3.2f", __func__, (double) line_time_in_us);
	DTRACE("DLG: %s: vupdate_offset     = %d", __func__, vupdate_offset);
	DTRACE("DLG: %s: vupdate_width      = %d", __func__, vupdate_width);
	DTRACE("DLG: %s: vready_offset      = %d", __func__, vready_offset);
	DTRACE("DLG: %s: line_time_in_us    = %3.2f", __func__, line_time_in_us);
	DTRACE("DLG: %s: line_wait          = %3.2f", __func__, line_wait);
	DTRACE("DLG: %s: line_o             = %3.2f", __func__, line_o);
	DTRACE("DLG: %s: line_setup         = %3.2f", __func__, line_setup);
	DTRACE("DLG: %s: line_calc          = %3.2f", __func__, line_calc);

	dst_y_prefetch = ((double) min_vblank - 1.0)
			- (line_setup + line_calc + line_wait + line_o);
	DTRACE("DLG: %s: dst_y_prefetch (before rnd) = %3.2f", __func__, dst_y_prefetch);
	ASSERT(dst_y_prefetch >= 2.0);

	dst_y_prefetch = dml_floor(4.0 * (dst_y_prefetch + 0.125), 1) / 4;
	DTRACE("DLG: %s: dst_y_prefetch (after rnd) = %3.2f", __func__, dst_y_prefetch);

	t_pre_us = dst_y_prefetch * line_time_in_us;
	vm_bytes = 0;
	meta_row_bytes = 0;

	if (dcc_en && vm_en)
		vm_bytes = meta_pte_bytes_per_frame_ub_l;
	if (dcc_en)
		meta_row_bytes = meta_bytes_per_row_ub_l;

	max_num_sw_l = 0;
	max_num_sw_c = 0;
	max_partial_sw_l = 0;
	max_partial_sw_c = 0;

	max_vinit_l = interlaced ? dml_max(vinit_l, vinit_bot_l) : vinit_l;
	max_vinit_c = interlaced ? dml_max(vinit_c, vinit_bot_c) : vinit_c;

	get_swath_need(mode_lib, &max_num_sw_l, &max_partial_sw_l, swath_height_l, max_vinit_l);
	if (dual_plane)
		get_swath_need(
				mode_lib,
				&max_num_sw_c,
				&max_partial_sw_c,
				swath_height_c,
				max_vinit_c);

	lsw_l = max_num_sw_l * swath_height_l + max_partial_sw_l;
	lsw_c = max_num_sw_c * swath_height_c + max_partial_sw_c;
	sw_bytes_ub_l = lsw_l * swath_width_ub_l * bytes_per_element_l;
	sw_bytes_ub_c = lsw_c * swath_width_ub_c * bytes_per_element_c;
	sw_bytes = 0;
	dpte_row_bytes = 0;

	if (vm_en) {
		if (dual_plane)
			dpte_row_bytes = dpte_bytes_per_row_ub_l + dpte_bytes_per_row_ub_c;
		else
			dpte_row_bytes = dpte_bytes_per_row_ub_l;
	} else {
		dpte_row_bytes = 0;
	}

	if (dual_plane)
		sw_bytes = sw_bytes_ub_l + sw_bytes_ub_c;
	else
		sw_bytes = sw_bytes_ub_l;

	DTRACE("DLG: %s: sw_bytes_ub_l           = %d", __func__, sw_bytes_ub_l);
	DTRACE("DLG: %s: sw_bytes_ub_c           = %d", __func__, sw_bytes_ub_c);
	DTRACE("DLG: %s: sw_bytes                = %d", __func__, sw_bytes);
	DTRACE("DLG: %s: vm_bytes                = %d", __func__, vm_bytes);
	DTRACE("DLG: %s: meta_row_bytes          = %d", __func__, meta_row_bytes);
	DTRACE("DLG: %s: dpte_row_bytes          = %d", __func__, dpte_row_bytes);

	prefetch_bw = (vm_bytes + 2 * dpte_row_bytes + 2 * meta_row_bytes + sw_bytes) / t_pre_us;
	flip_bw = ((vm_bytes + dpte_row_bytes + meta_row_bytes) * dlg_sys_param.total_flip_bw)
			/ (double) dlg_sys_param.total_flip_bytes;
	t_vm_us = line_time_in_us / 4.0;
	if (vm_en && dcc_en) {
		t_vm_us = dml_max(
				dlg_sys_param.t_extra_us,
				dml_max((double) vm_bytes / prefetch_bw, t_vm_us));

		if (iflip_en && !dual_plane) {
			t_vm_us = dml_max(mode_lib->soc.urgent_latency_us, t_vm_us);
			if (flip_bw > 0.)
				t_vm_us = dml_max(vm_bytes / flip_bw, t_vm_us);
		}
	}

	t_r0_us = dml_max(dlg_sys_param.t_extra_us - t_vm_us, line_time_in_us - t_vm_us);

	if (vm_en || dcc_en) {
		t_r0_us = dml_max(
				(double) (dpte_row_bytes + meta_row_bytes) / prefetch_bw,
				dlg_sys_param.t_extra_us);
		t_r0_us = dml_max((double) (line_time_in_us - t_vm_us), t_r0_us);

		if (iflip_en && !dual_plane) {
			t_r0_us = dml_max(mode_lib->soc.urgent_latency_us * 2.0, t_r0_us);
			if (flip_bw > 0.)
				t_r0_us = dml_max(
						(dpte_row_bytes + meta_row_bytes) / flip_bw,
						t_r0_us);
		}
	}

	disp_dlg_regs->dst_y_after_scaler = dst_y_after_scaler; /* in terms of line */
	disp_dlg_regs->refcyc_x_after_scaler = dst_x_after_scaler * ref_freq_to_pix_freq; /* in terms of refclk */
	ASSERT(disp_dlg_regs->refcyc_x_after_scaler < (unsigned int) dml_pow(2, 13));
	DTRACE(
			"DLG: %s: disp_dlg_regs->dst_y_after_scaler      = 0x%0x",
			__func__,
			disp_dlg_regs->dst_y_after_scaler);
	DTRACE(
			"DLG: %s: disp_dlg_regs->refcyc_x_after_scaler   = 0x%0x",
			__func__,
			disp_dlg_regs->refcyc_x_after_scaler);

	disp_dlg_regs->dst_y_prefetch = (unsigned int) (dst_y_prefetch * dml_pow(2, 2));
	DTRACE(
			"DLG: %s: disp_dlg_regs->dst_y_prefetch  = %d",
			__func__,
			disp_dlg_regs->dst_y_prefetch);

	dst_y_per_vm_vblank = 0.0;
	dst_y_per_row_vblank = 0.0;

	dst_y_per_vm_vblank = t_vm_us / line_time_in_us;
	dst_y_per_vm_vblank = dml_floor(4.0 * (dst_y_per_vm_vblank + 0.125), 1) / 4.0;
	disp_dlg_regs->dst_y_per_vm_vblank = (unsigned int) (dst_y_per_vm_vblank * dml_pow(2, 2));

	dst_y_per_row_vblank = t_r0_us / line_time_in_us;
	dst_y_per_row_vblank = dml_floor(4.0 * (dst_y_per_row_vblank + 0.125), 1) / 4.0;
	disp_dlg_regs->dst_y_per_row_vblank = (unsigned int) (dst_y_per_row_vblank * dml_pow(2, 2));

	DTRACE("DLG: %s: lsw_l                   = %d", __func__, lsw_l);
	DTRACE("DLG: %s: lsw_c                   = %d", __func__, lsw_c);
	DTRACE("DLG: %s: dpte_bytes_per_row_ub_l = %d", __func__, dpte_bytes_per_row_ub_l);
	DTRACE("DLG: %s: dpte_bytes_per_row_ub_c = %d", __func__, dpte_bytes_per_row_ub_c);

	DTRACE("DLG: %s: prefetch_bw            = %3.2f", __func__, prefetch_bw);
	DTRACE("DLG: %s: flip_bw                = %3.2f", __func__, flip_bw);
	DTRACE("DLG: %s: t_pre_us               = %3.2f", __func__, t_pre_us);
	DTRACE("DLG: %s: t_vm_us                = %3.2f", __func__, t_vm_us);
	DTRACE("DLG: %s: t_r0_us                = %3.2f", __func__, t_r0_us);
	DTRACE("DLG: %s: dst_y_per_vm_vblank    = %3.2f", __func__, dst_y_per_vm_vblank);
	DTRACE("DLG: %s: dst_y_per_row_vblank   = %3.2f", __func__, dst_y_per_row_vblank);
	DTRACE("DLG: %s: dst_y_prefetch         = %3.2f", __func__, dst_y_prefetch);

	min_dst_y_per_vm_vblank = 8.0;
	min_dst_y_per_row_vblank = 16.0;
	if (htotal <= 75) {
		min_vblank = 300;
		min_dst_y_per_vm_vblank = 100.0;
		min_dst_y_per_row_vblank = 100.0;
	}

	ASSERT(dst_y_per_vm_vblank < min_dst_y_per_vm_vblank);
	ASSERT(dst_y_per_row_vblank < min_dst_y_per_row_vblank);

	ASSERT(dst_y_prefetch > (dst_y_per_vm_vblank + dst_y_per_row_vblank));
	lsw = dst_y_prefetch - (dst_y_per_vm_vblank + dst_y_per_row_vblank);

	DTRACE("DLG: %s: lsw = %3.2f", __func__, lsw);

	vratio_pre_l = get_vratio_pre(
			mode_lib,
			max_num_sw_l,
			max_partial_sw_l,
			swath_height_l,
			max_vinit_l,
			lsw);
	vratio_pre_c = 1.0;
	if (dual_plane)
		vratio_pre_c = get_vratio_pre(
				mode_lib,
				max_num_sw_c,
				max_partial_sw_c,
				swath_height_c,
				max_vinit_c,
				lsw);

	DTRACE("DLG: %s: vratio_pre_l=%3.2f", __func__, vratio_pre_l);
	DTRACE("DLG: %s: vratio_pre_c=%3.2f", __func__, vratio_pre_c);

	ASSERT(vratio_pre_l <= 4.0);
	if (vratio_pre_l >= 4.0)
		disp_dlg_regs->vratio_prefetch = (unsigned int) dml_pow(2, 21) - 1;
	else
		disp_dlg_regs->vratio_prefetch = (unsigned int) (vratio_pre_l * dml_pow(2, 19));

	ASSERT(vratio_pre_c <= 4.0);
	if (vratio_pre_c >= 4.0)
		disp_dlg_regs->vratio_prefetch_c = (unsigned int) dml_pow(2, 21) - 1;
	else
		disp_dlg_regs->vratio_prefetch_c = (unsigned int) (vratio_pre_c * dml_pow(2, 19));

	disp_dlg_regs->refcyc_per_pte_group_vblank_l =
			(unsigned int) (dst_y_per_row_vblank * (double) htotal
					* ref_freq_to_pix_freq / (double) dpte_groups_per_row_ub_l);
	ASSERT(disp_dlg_regs->refcyc_per_pte_group_vblank_l < (unsigned int) dml_pow(2, 13));

	disp_dlg_regs->refcyc_per_pte_group_vblank_c =
			(unsigned int) (dst_y_per_row_vblank * (double) htotal
					* ref_freq_to_pix_freq / (double) dpte_groups_per_row_ub_c);
	ASSERT(disp_dlg_regs->refcyc_per_pte_group_vblank_c < (unsigned int) dml_pow(2, 13));

	disp_dlg_regs->refcyc_per_meta_chunk_vblank_l =
			(unsigned int) (dst_y_per_row_vblank * (double) htotal
					* ref_freq_to_pix_freq / (double) meta_chunks_per_row_ub_l);
	ASSERT(disp_dlg_regs->refcyc_per_meta_chunk_vblank_l < (unsigned int) dml_pow(2, 13));

	disp_dlg_regs->refcyc_per_meta_chunk_vblank_c =
			disp_dlg_regs->refcyc_per_meta_chunk_vblank_l;/* dcc for 4:2:0 is not supported in dcn1.0.  assigned to be the same as _l for now */

	/* Active */
	req_per_swath_ub_l = rq_dlg_param.rq_l.req_per_swath_ub;
	req_per_swath_ub_c = rq_dlg_param.rq_c.req_per_swath_ub;
	meta_row_height_l = rq_dlg_param.rq_l.meta_row_height;
	swath_width_pixels_ub_l = 0;
	swath_width_pixels_ub_c = 0;
	scaler_rec_in_width_l = 0;
	scaler_rec_in_width_c = 0;
	dpte_row_height_l = rq_dlg_param.rq_l.dpte_row_height;
	dpte_row_height_c = rq_dlg_param.rq_c.dpte_row_height;

	disp_dlg_regs->dst_y_per_pte_row_nom_l = (unsigned int) ((double) dpte_row_height_l
			/ (double) vratio_l * dml_pow(2, 2));
	ASSERT(disp_dlg_regs->dst_y_per_pte_row_nom_l < (unsigned int) dml_pow(2, 17));

	disp_dlg_regs->dst_y_per_pte_row_nom_c = (unsigned int) ((double) dpte_row_height_c
			/ (double) vratio_c * dml_pow(2, 2));
	ASSERT(disp_dlg_regs->dst_y_per_pte_row_nom_c < (unsigned int) dml_pow(2, 17));

	disp_dlg_regs->dst_y_per_meta_row_nom_l = (unsigned int) ((double) meta_row_height_l
			/ (double) vratio_l * dml_pow(2, 2));
	ASSERT(disp_dlg_regs->dst_y_per_meta_row_nom_l < (unsigned int) dml_pow(2, 17));

	disp_dlg_regs->dst_y_per_meta_row_nom_c = disp_dlg_regs->dst_y_per_meta_row_nom_l; /* dcc for 4:2:0 is not supported in dcn1.0.  assigned to be the same as _l for now */

	disp_dlg_regs->refcyc_per_pte_group_nom_l = (unsigned int) ((double) dpte_row_height_l
			/ (double) vratio_l * (double) htotal * ref_freq_to_pix_freq
			/ (double) dpte_groups_per_row_ub_l);
	if (disp_dlg_regs->refcyc_per_pte_group_nom_l >= (unsigned int) dml_pow(2, 23))
		disp_dlg_regs->refcyc_per_pte_group_nom_l = dml_pow(2, 23) - 1;

	disp_dlg_regs->refcyc_per_pte_group_nom_c = (unsigned int) ((double) dpte_row_height_c
			/ (double) vratio_c * (double) htotal * ref_freq_to_pix_freq
			/ (double) dpte_groups_per_row_ub_c);
	if (disp_dlg_regs->refcyc_per_pte_group_nom_c >= (unsigned int) dml_pow(2, 23))
		disp_dlg_regs->refcyc_per_pte_group_nom_c = dml_pow(2, 23) - 1;

	disp_dlg_regs->refcyc_per_meta_chunk_nom_l = (unsigned int) ((double) meta_row_height_l
			/ (double) vratio_l * (double) htotal * ref_freq_to_pix_freq
			/ (double) meta_chunks_per_row_ub_l);
	if (disp_dlg_regs->refcyc_per_meta_chunk_nom_l >= (unsigned int) dml_pow(2, 23))
		disp_dlg_regs->refcyc_per_meta_chunk_nom_l = dml_pow(2, 23) - 1;

	if (mode_422) {
		swath_width_pixels_ub_l = swath_width_ub_l * 2; /* *2 for 2 pixel per element */
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
		if ((hratios_l * 2.0) > 4.0)
			min_hratio_fact_l = 4.0;
		else
			min_hratio_fact_l = hratios_l * 2.0;
	} else {
		if (hratios_l > 4.0)
			min_hratio_fact_l = 4.0;
		else
			min_hratio_fact_l = hratios_l;
	}

	hscale_pixel_rate_l = min_hratio_fact_l * dppclk_freq_in_mhz;

	if (htaps_c <= 1)
		min_hratio_fact_c = 2.0;
	else if (htaps_c <= 6) {
		if ((hratios_c * 2.0) > 4.0)
			min_hratio_fact_c = 4.0;
		else
			min_hratio_fact_c = hratios_c * 2.0;
	} else {
		if (hratios_c > 4.0)
			min_hratio_fact_c = 4.0;
		else
			min_hratio_fact_c = hratios_c;
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
	refcyc_per_req_delivery_pre_cur0 = 0.;
	refcyc_per_req_delivery_cur0 = 0.;

	full_recout_width = 0;
	if (e2e_pipe_param.pipe.src.is_hsplit) {
		if (e2e_pipe_param.pipe.dest.full_recout_width == 0) {
			DTRACE("DLG: %s: Warningfull_recout_width not set in hsplit mode", __func__);
			full_recout_width = e2e_pipe_param.pipe.dest.recout_width * 2; /* assume half split for dcn1 */
		} else
			full_recout_width = e2e_pipe_param.pipe.dest.full_recout_width;
	} else
		full_recout_width = e2e_pipe_param.pipe.dest.recout_width;

	refcyc_per_line_delivery_pre_l = get_refcyc_per_delivery(
			mode_lib,
			refclk_freq_in_mhz,
			pclk_freq_in_mhz,
			full_recout_width,
			vratio_pre_l,
			hscale_pixel_rate_l,
			swath_width_pixels_ub_l,
			1); /* per line */

	refcyc_per_line_delivery_l = get_refcyc_per_delivery(
			mode_lib,
			refclk_freq_in_mhz,
			pclk_freq_in_mhz,
			full_recout_width,
			vratio_l,
			hscale_pixel_rate_l,
			swath_width_pixels_ub_l,
			1); /* per line */

	DTRACE("DLG: %s: full_recout_width              = %d", __func__, full_recout_width);
	DTRACE("DLG: %s: hscale_pixel_rate_l            = %3.2f", __func__, hscale_pixel_rate_l);
	DTRACE(
			"DLG: %s: refcyc_per_line_delivery_pre_l = %3.2f",
			__func__,
			refcyc_per_line_delivery_pre_l);
	DTRACE(
			"DLG: %s: refcyc_per_line_delivery_l     = %3.2f",
			__func__,
			refcyc_per_line_delivery_l);

	disp_dlg_regs->refcyc_per_line_delivery_pre_l = (unsigned int) dml_floor(
			refcyc_per_line_delivery_pre_l,
			1);
	disp_dlg_regs->refcyc_per_line_delivery_l = (unsigned int) dml_floor(
			refcyc_per_line_delivery_l,
			1);
	ASSERT(disp_dlg_regs->refcyc_per_line_delivery_pre_l < (unsigned int) dml_pow(2, 13));
	ASSERT(disp_dlg_regs->refcyc_per_line_delivery_l < (unsigned int) dml_pow(2, 13));

	if (dual_plane) {
		refcyc_per_line_delivery_pre_c = get_refcyc_per_delivery(
				mode_lib,
				refclk_freq_in_mhz,
				pclk_freq_in_mhz,
				full_recout_width,
				vratio_pre_c,
				hscale_pixel_rate_c,
				swath_width_pixels_ub_c,
				1); /* per line */

		refcyc_per_line_delivery_c = get_refcyc_per_delivery(
				mode_lib,
				refclk_freq_in_mhz,
				pclk_freq_in_mhz,
				full_recout_width,
				vratio_c,
				hscale_pixel_rate_c,
				swath_width_pixels_ub_c,
				1); /* per line */

		DTRACE(
				"DLG: %s: refcyc_per_line_delivery_pre_c = %3.2f",
				__func__,
				refcyc_per_line_delivery_pre_c);
		DTRACE(
				"DLG: %s: refcyc_per_line_delivery_c     = %3.2f",
				__func__,
				refcyc_per_line_delivery_c);

		disp_dlg_regs->refcyc_per_line_delivery_pre_c = (unsigned int) dml_floor(
				refcyc_per_line_delivery_pre_c,
				1);
		disp_dlg_regs->refcyc_per_line_delivery_c = (unsigned int) dml_floor(
				refcyc_per_line_delivery_c,
				1);
		ASSERT(disp_dlg_regs->refcyc_per_line_delivery_pre_c < (unsigned int) dml_pow(2, 13));
		ASSERT(disp_dlg_regs->refcyc_per_line_delivery_c < (unsigned int) dml_pow(2, 13));
	}
	disp_dlg_regs->chunk_hdl_adjust_cur0 = 3;

	/* TTU - Luma / Chroma */
	if (access_dir) { /* vertical access */
		scaler_rec_in_width_l = vp_height_l;
		scaler_rec_in_width_c = vp_height_c;
	} else {
		scaler_rec_in_width_l = vp_width_l;
		scaler_rec_in_width_c = vp_width_c;
	}

	refcyc_per_req_delivery_pre_l = get_refcyc_per_delivery(
			mode_lib,
			refclk_freq_in_mhz,
			pclk_freq_in_mhz,
			full_recout_width,
			vratio_pre_l,
			hscale_pixel_rate_l,
			scaler_rec_in_width_l,
			req_per_swath_ub_l); /* per req */
	refcyc_per_req_delivery_l = get_refcyc_per_delivery(
			mode_lib,
			refclk_freq_in_mhz,
			pclk_freq_in_mhz,
			full_recout_width,
			vratio_l,
			hscale_pixel_rate_l,
			scaler_rec_in_width_l,
			req_per_swath_ub_l); /* per req */

	DTRACE(
			"DLG: %s: refcyc_per_req_delivery_pre_l = %3.2f",
			__func__,
			refcyc_per_req_delivery_pre_l);
	DTRACE(
			"DLG: %s: refcyc_per_req_delivery_l     = %3.2f",
			__func__,
			refcyc_per_req_delivery_l);

	disp_ttu_regs->refcyc_per_req_delivery_pre_l = (unsigned int) (refcyc_per_req_delivery_pre_l
			* dml_pow(2, 10));
	disp_ttu_regs->refcyc_per_req_delivery_l = (unsigned int) (refcyc_per_req_delivery_l
			* dml_pow(2, 10));

	ASSERT(refcyc_per_req_delivery_pre_l < dml_pow(2, 13));
	ASSERT(refcyc_per_req_delivery_l < dml_pow(2, 13));

	if (dual_plane) {
		refcyc_per_req_delivery_pre_c = get_refcyc_per_delivery(
				mode_lib,
				refclk_freq_in_mhz,
				pclk_freq_in_mhz,
				full_recout_width,
				vratio_pre_c,
				hscale_pixel_rate_c,
				scaler_rec_in_width_c,
				req_per_swath_ub_c); /* per req  */
		refcyc_per_req_delivery_c = get_refcyc_per_delivery(
				mode_lib,
				refclk_freq_in_mhz,
				pclk_freq_in_mhz,
				full_recout_width,
				vratio_c,
				hscale_pixel_rate_c,
				scaler_rec_in_width_c,
				req_per_swath_ub_c); /* per req */

		DTRACE(
				"DLG: %s: refcyc_per_req_delivery_pre_c = %3.2f",
				__func__,
				refcyc_per_req_delivery_pre_c);
		DTRACE(
				"DLG: %s: refcyc_per_req_delivery_c     = %3.2f",
				__func__,
				refcyc_per_req_delivery_c);

		disp_ttu_regs->refcyc_per_req_delivery_pre_c =
				(unsigned int) (refcyc_per_req_delivery_pre_c * dml_pow(2, 10));
		disp_ttu_regs->refcyc_per_req_delivery_c = (unsigned int) (refcyc_per_req_delivery_c
				* dml_pow(2, 10));

		ASSERT(refcyc_per_req_delivery_pre_c < dml_pow(2, 13));
		ASSERT(refcyc_per_req_delivery_c < dml_pow(2, 13));
	}

	/* TTU - Cursor */
	hratios_cur0 = e2e_pipe_param.pipe.scale_ratio_depth.hscl_ratio;
	cur0_src_width = e2e_pipe_param.pipe.src.cur0_src_width; /* cursor source width */
	cur0_bpp = (enum cursor_bpp) e2e_pipe_param.pipe.src.cur0_bpp;
	cur0_req_size = 0;
	cur0_req_width = 0;
	cur0_width_ub = 0.0;
	cur0_req_per_width = 0.0;
	hactive_cur0 = 0.0;

	ASSERT(cur0_src_width <= 256);

	if (cur0_src_width > 0) {
		unsigned int cur0_bit_per_pixel = 0;

		if (cur0_bpp == dm_cur_2bit) {
			cur0_req_size = 64; /* byte */
			cur0_bit_per_pixel = 2;
		} else { /* 32bit */
			cur0_bit_per_pixel = 32;
			if (cur0_src_width >= 1 && cur0_src_width <= 16)
				cur0_req_size = 64;
			else if (cur0_src_width >= 17 && cur0_src_width <= 31)
				cur0_req_size = 128;
			else
				cur0_req_size = 256;
		}

		cur0_req_width = (double) cur0_req_size / ((double) cur0_bit_per_pixel / 8.0);
		cur0_width_ub = dml_ceil((double) cur0_src_width / (double) cur0_req_width, 1)
				* (double) cur0_req_width;
		cur0_req_per_width = cur0_width_ub / (double) cur0_req_width;
		hactive_cur0 = (double) cur0_src_width / hratios_cur0; /* TODO: oswin to think about what to do for cursor */

		if (vratio_pre_l <= 1.0) {
			refcyc_per_req_delivery_pre_cur0 = hactive_cur0 * ref_freq_to_pix_freq
					/ (double) cur0_req_per_width;
		} else {
			refcyc_per_req_delivery_pre_cur0 = (double) refclk_freq_in_mhz
					* (double) cur0_src_width / hscale_pixel_rate_l
					/ (double) cur0_req_per_width;
		}

		disp_ttu_regs->refcyc_per_req_delivery_pre_cur0 =
				(unsigned int) (refcyc_per_req_delivery_pre_cur0 * dml_pow(2, 10));
		ASSERT(refcyc_per_req_delivery_pre_cur0 < dml_pow(2, 13));

		if (vratio_l <= 1.0) {
			refcyc_per_req_delivery_cur0 = hactive_cur0 * ref_freq_to_pix_freq
					/ (double) cur0_req_per_width;
		} else {
			refcyc_per_req_delivery_cur0 = (double) refclk_freq_in_mhz
					* (double) cur0_src_width / hscale_pixel_rate_l
					/ (double) cur0_req_per_width;
		}

		DTRACE("DLG: %s: cur0_req_width                     = %d", __func__, cur0_req_width);
		DTRACE(
				"DLG: %s: cur0_width_ub                      = %3.2f",
				__func__,
				cur0_width_ub);
		DTRACE(
				"DLG: %s: cur0_req_per_width                 = %3.2f",
				__func__,
				cur0_req_per_width);
		DTRACE(
				"DLG: %s: hactive_cur0                       = %3.2f",
				__func__,
				hactive_cur0);
		DTRACE(
				"DLG: %s: refcyc_per_req_delivery_pre_cur0   = %3.2f",
				__func__,
				refcyc_per_req_delivery_pre_cur0);
		DTRACE(
				"DLG: %s: refcyc_per_req_delivery_cur0       = %3.2f",
				__func__,
				refcyc_per_req_delivery_cur0);

		disp_ttu_regs->refcyc_per_req_delivery_cur0 =
				(unsigned int) (refcyc_per_req_delivery_cur0 * dml_pow(2, 10));
		ASSERT(refcyc_per_req_delivery_cur0 < dml_pow(2, 13));
	} else {
		disp_ttu_regs->refcyc_per_req_delivery_pre_cur0 = 0;
		disp_ttu_regs->refcyc_per_req_delivery_cur0 = 0;
	}

	/* TTU - Misc */
	disp_ttu_regs->qos_level_low_wm = 0;
	ASSERT(disp_ttu_regs->qos_level_low_wm < dml_pow(2, 14));
	disp_ttu_regs->qos_level_high_wm = (unsigned int) (4.0 * (double) htotal
			* ref_freq_to_pix_freq);
	ASSERT(disp_ttu_regs->qos_level_high_wm < dml_pow(2, 14));

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
