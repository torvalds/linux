/*
 * Copyright 2012-17 Advanced Micro Devices, Inc.
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
#include "os_types.h"
#include <drm/drm_dsc.h>
#include "dscc_types.h"
#include "rc_calc.h"

double dsc_ceil(double num);

static void copy_pps_fields(struct drm_dsc_config *to, const struct drm_dsc_config *from)
{
	to->line_buf_depth           = from->line_buf_depth;
	to->bits_per_component       = from->bits_per_component;
	to->convert_rgb              = from->convert_rgb;
	to->slice_width              = from->slice_width;
	to->slice_height             = from->slice_height;
	to->simple_422               = from->simple_422;
	to->native_422               = from->native_422;
	to->native_420               = from->native_420;
	to->pic_width                = from->pic_width;
	to->pic_height               = from->pic_height;
	to->rc_tgt_offset_high       = from->rc_tgt_offset_high;
	to->rc_tgt_offset_low        = from->rc_tgt_offset_low;
	to->bits_per_pixel           = from->bits_per_pixel;
	to->rc_edge_factor           = from->rc_edge_factor;
	to->rc_quant_incr_limit1     = from->rc_quant_incr_limit1;
	to->rc_quant_incr_limit0     = from->rc_quant_incr_limit0;
	to->initial_xmit_delay       = from->initial_xmit_delay;
	to->initial_dec_delay        = from->initial_dec_delay;
	to->block_pred_enable        = from->block_pred_enable;
	to->first_line_bpg_offset    = from->first_line_bpg_offset;
	to->second_line_bpg_offset   = from->second_line_bpg_offset;
	to->initial_offset           = from->initial_offset;
	memcpy(&to->rc_buf_thresh, &from->rc_buf_thresh, sizeof(from->rc_buf_thresh));
	memcpy(&to->rc_range_params, &from->rc_range_params, sizeof(from->rc_range_params));
	to->rc_model_size            = from->rc_model_size;
	to->flatness_min_qp          = from->flatness_min_qp;
	to->flatness_max_qp          = from->flatness_max_qp;
	to->initial_scale_value      = from->initial_scale_value;
	to->scale_decrement_interval = from->scale_decrement_interval;
	to->scale_increment_interval = from->scale_increment_interval;
	to->nfl_bpg_offset           = from->nfl_bpg_offset;
	to->nsl_bpg_offset           = from->nsl_bpg_offset;
	to->slice_bpg_offset         = from->slice_bpg_offset;
	to->final_offset             = from->final_offset;
	to->vbr_enable               = from->vbr_enable;
	to->slice_chunk_size         = from->slice_chunk_size;
	to->second_line_offset_adj   = from->second_line_offset_adj;
	to->dsc_version_minor        = from->dsc_version_minor;
}

static void copy_rc_to_cfg(struct drm_dsc_config *dsc_cfg, const struct rc_params *rc)
{
	int i;

	dsc_cfg->rc_quant_incr_limit0   = rc->rc_quant_incr_limit0;
	dsc_cfg->rc_quant_incr_limit1   = rc->rc_quant_incr_limit1;
	dsc_cfg->initial_offset         = rc->initial_fullness_offset;
	dsc_cfg->initial_xmit_delay     = rc->initial_xmit_delay;
	dsc_cfg->first_line_bpg_offset  = rc->first_line_bpg_offset;
	dsc_cfg->second_line_bpg_offset = rc->second_line_bpg_offset;
	dsc_cfg->flatness_min_qp        = rc->flatness_min_qp;
	dsc_cfg->flatness_max_qp        = rc->flatness_max_qp;
	for (i = 0; i < QP_SET_SIZE; ++i) {
		dsc_cfg->rc_range_params[i].range_min_qp     = rc->qp_min[i];
		dsc_cfg->rc_range_params[i].range_max_qp     = rc->qp_max[i];
		/* Truncate 8-bit signed value to 6-bit signed value */
		dsc_cfg->rc_range_params[i].range_bpg_offset = 0x3f & rc->ofs[i];
	}
	dsc_cfg->rc_model_size    = rc->rc_model_size;
	dsc_cfg->rc_edge_factor   = rc->rc_edge_factor;
	dsc_cfg->rc_tgt_offset_high = rc->rc_tgt_offset_hi;
	dsc_cfg->rc_tgt_offset_low = rc->rc_tgt_offset_lo;

	for (i = 0; i < QP_SET_SIZE - 1; ++i)
		dsc_cfg->rc_buf_thresh[i] = rc->rc_buf_thresh[i];
}

int dscc_compute_dsc_parameters(const struct drm_dsc_config *pps, struct dsc_parameters *dsc_params)
{
	enum colour_mode  mode = pps->convert_rgb ? CM_RGB :
							(pps->simple_422  ? CM_444 :
							(pps->native_422  ? CM_422 :
							pps->native_420  ? CM_420 : CM_444));
	enum bits_per_comp bpc = (pps->bits_per_component == 8) ? BPC_8 :
							(pps->bits_per_component == 10) ? BPC_10 : BPC_12;
	float            bpp = ((float) pps->bits_per_pixel / 16.0);
	int              slice_width  = pps->slice_width;
	int              slice_height = pps->slice_height;
	int              ret;
	struct rc_params rc;
	struct drm_dsc_config   dsc_cfg;

	double d_bytes_per_pixel = dsc_ceil(bpp * slice_width / 8.0) / slice_width;

	// TODO: Make sure the formula for calculating this is precise (ceiling vs. floor, and at what point they should be applied)
	if (pps->native_422 || pps->native_420)
		d_bytes_per_pixel /= 2;

	dsc_params->bytes_per_pixel = (uint32_t)dsc_ceil(d_bytes_per_pixel * 0x10000000);

	/* in native_422 or native_420 modes, the bits_per_pixel is double the target bpp
	 * (the latter is what calc_rc_params expects)
	 */
	if (pps->native_422 || pps->native_420)
		bpp /= 2.0;

	calc_rc_params(&rc, mode, bpc, bpp, slice_width, slice_height, pps->dsc_version_minor);
	dsc_params->pps = *pps;
	dsc_params->pps.initial_scale_value = 8 * rc.rc_model_size / (rc.rc_model_size - rc.initial_fullness_offset);

	copy_pps_fields(&dsc_cfg, &dsc_params->pps);
	copy_rc_to_cfg(&dsc_cfg, &rc);

	dsc_cfg.mux_word_size = dsc_params->pps.bits_per_component <= 10 ? 48 : 64;

	ret = drm_dsc_compute_rc_parameters(&dsc_cfg);

	copy_pps_fields(&dsc_params->pps, &dsc_cfg);
	dsc_params->rc_buffer_model_size = dsc_cfg.rc_bits;
	return ret;
}

