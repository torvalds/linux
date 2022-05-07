// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "ia_css_types.h"
#include "sh_css_defs.h"
#ifndef IA_CSS_NO_DEBUG
#include "ia_css_debug.h"
#endif
#include "sh_css_frac.h"
#include "assert_support.h"

#include "bh/bh_2/ia_css_bh.host.h"
#include "ia_css_s3a.host.h"

const struct ia_css_3a_config default_3a_config = {
	25559,
	32768,
	7209,
	65535,
	0,
	65535,
	{-3344, -6104, -19143, 19143, 6104, 3344, 0},
	{1027, 0, -9219, 16384, -9219, 1027, 0}
};

static unsigned int s3a_raw_bit_depth;

void
ia_css_s3a_configure(unsigned int raw_bit_depth)
{
	s3a_raw_bit_depth = raw_bit_depth;
}

static void
ia_css_ae_encode(
    struct sh_css_isp_ae_params *to,
    const struct ia_css_3a_config *from,
    unsigned int size)
{
	(void)size;
	/* coefficients to calculate Y */
	to->y_coef_r =
	    uDIGIT_FITTING(from->ae_y_coef_r, 16, SH_CSS_AE_YCOEF_SHIFT);
	to->y_coef_g =
	    uDIGIT_FITTING(from->ae_y_coef_g, 16, SH_CSS_AE_YCOEF_SHIFT);
	to->y_coef_b =
	    uDIGIT_FITTING(from->ae_y_coef_b, 16, SH_CSS_AE_YCOEF_SHIFT);
}

static void
ia_css_awb_encode(
    struct sh_css_isp_awb_params *to,
    const struct ia_css_3a_config *from,
    unsigned int size)
{
	(void)size;
	/* AWB level gate */
	to->lg_high_raw =
	    uDIGIT_FITTING(from->awb_lg_high_raw, 16, s3a_raw_bit_depth);
	to->lg_low =
	    uDIGIT_FITTING(from->awb_lg_low, 16, SH_CSS_BAYER_BITS);
	to->lg_high =
	    uDIGIT_FITTING(from->awb_lg_high, 16, SH_CSS_BAYER_BITS);
}

static void
ia_css_af_encode(
    struct sh_css_isp_af_params *to,
    const struct ia_css_3a_config *from,
    unsigned int size)
{
	unsigned int i;
	(void)size;

	/* af fir coefficients */
	for (i = 0; i < 7; ++i) {
		to->fir1[i] =
		    sDIGIT_FITTING(from->af_fir1_coef[i], 15,
				   SH_CSS_AF_FIR_SHIFT);
		to->fir2[i] =
		    sDIGIT_FITTING(from->af_fir2_coef[i], 15,
				   SH_CSS_AF_FIR_SHIFT);
	}
}

void
ia_css_s3a_encode(
    struct sh_css_isp_s3a_params *to,
    const struct ia_css_3a_config *from,
    unsigned int size)
{
	(void)size;

	ia_css_ae_encode(&to->ae,   from, sizeof(to->ae));
	ia_css_awb_encode(&to->awb, from, sizeof(to->awb));
	ia_css_af_encode(&to->af,   from, sizeof(to->af));
}

#if 0
void
ia_css_process_s3a(
    unsigned int pipe_id,
    const struct ia_css_pipeline_stage *stage,
    struct ia_css_isp_parameters *params)
{
	short dmem_offset = stage->binary->info->mem_offsets->dmem.s3a;

	assert(params);

	if (dmem_offset >= 0) {
		ia_css_s3a_encode((struct sh_css_isp_s3a_params *)
				  &stage->isp_mem_params[IA_CSS_ISP_DMEM0].address[dmem_offset],
				  &params->s3a_config);
		ia_css_bh_encode((struct sh_css_isp_bh_params *)
				 &stage->isp_mem_params[IA_CSS_ISP_DMEM0].address[dmem_offset],
				 &params->s3a_config);
		params->isp_params_changed = true;
		params->isp_mem_params_changed[pipe_id][stage->stage_num][IA_CSS_ISP_DMEM0] =
		    true;
	}

	params->isp_params_changed = true;
}
#endif

#ifndef IA_CSS_NO_DEBUG
void
ia_css_ae_dump(
    const struct sh_css_isp_ae_params *ae,
    unsigned int level)
{
	if (!ae) return;
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ae_y_coef_r", ae->y_coef_r);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ae_y_coef_g", ae->y_coef_g);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ae_y_coef_b", ae->y_coef_b);
}

void
ia_css_awb_dump(
    const struct sh_css_isp_awb_params *awb,
    unsigned int level)
{
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "awb_lg_high_raw", awb->lg_high_raw);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "awb_lg_low", awb->lg_low);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "awb_lg_high", awb->lg_high);
}

void
ia_css_af_dump(
    const struct sh_css_isp_af_params *af,
    unsigned int level)
{
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir1[0]", af->fir1[0]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir1[1]", af->fir1[1]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir1[2]", af->fir1[2]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir1[3]", af->fir1[3]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir1[4]", af->fir1[4]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir1[5]", af->fir1[5]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir1[6]", af->fir1[6]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir2[0]", af->fir2[0]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir2[1]", af->fir2[1]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir2[2]", af->fir2[2]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir2[3]", af->fir2[3]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir2[4]", af->fir2[4]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir2[5]", af->fir2[5]);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "af_fir2[6]", af->fir2[6]);
}

void
ia_css_s3a_dump(
    const struct sh_css_isp_s3a_params *s3a,
    unsigned int level)
{
	ia_css_debug_dtrace(level, "S3A Support:\n");
	ia_css_ae_dump(&s3a->ae, level);
	ia_css_awb_dump(&s3a->awb, level);
	ia_css_af_dump(&s3a->af, level);
}

void
ia_css_s3a_debug_dtrace(
    const struct ia_css_3a_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.ae_y_coef_r=%d, config.ae_y_coef_g=%d, config.ae_y_coef_b=%d, config.awb_lg_high_raw=%d, config.awb_lg_low=%d, config.awb_lg_high=%d\n",
			    config->ae_y_coef_r, config->ae_y_coef_g,
			    config->ae_y_coef_b, config->awb_lg_high_raw,
			    config->awb_lg_low, config->awb_lg_high);
}
#endif

void
ia_css_s3a_hmem_decode(
    struct ia_css_3a_statistics *host_stats,
    const struct ia_css_bh_table *hmem_buf)
{
#if defined(HAS_NO_HMEM)
	(void)host_stats;
	(void)hmem_buf;
#else
	struct ia_css_3a_rgby_output	*out_ptr;
	int			i;

	/* pixel counts(BQ) for 3A area */
	int count_for_3a;
	int sum_r, diff;

	assert(host_stats);
	assert(host_stats->rgby_data);
	assert(hmem_buf);

	count_for_3a = host_stats->grid.width * host_stats->grid.height
		       * host_stats->grid.bqs_per_grid_cell
		       * host_stats->grid.bqs_per_grid_cell;

	out_ptr = host_stats->rgby_data;

	ia_css_bh_hmem_decode(out_ptr, hmem_buf);

	/* Calculate sum of histogram of R,
	   which should not be less than count_for_3a */
	sum_r = 0;
	for (i = 0; i < HMEM_UNIT_SIZE; i++) {
		sum_r += out_ptr[i].r;
	}
	if (sum_r < count_for_3a) {
		/* histogram is invalid */
		return;
	}

	/* Verify for sum of histogram of R/G/B/Y */
#if 0
	{
		int sum_g = 0;
		int sum_b = 0;
		int sum_y = 0;

		for (i = 0; i < HMEM_UNIT_SIZE; i++) {
			sum_g += out_ptr[i].g;
			sum_b += out_ptr[i].b;
			sum_y += out_ptr[i].y;
		}
		if (sum_g != sum_r || sum_b != sum_r || sum_y != sum_r) {
			/* histogram is invalid */
			return;
		}
	}
#endif

	/*
	 * Limit the histogram area only to 3A area.
	 * In DSP, the histogram of 0 is incremented for pixels
	 * which are outside of 3A area. That amount should be subtracted here.
	 *   hist[0] = hist[0] - ((sum of all hist[]) - (pixel count for 3A area))
	 */
	diff = sum_r - count_for_3a;
	out_ptr[0].r -= diff;
	out_ptr[0].g -= diff;
	out_ptr[0].b -= diff;
	out_ptr[0].y -= diff;
#endif
}

void
ia_css_s3a_dmem_decode(
    struct ia_css_3a_statistics *host_stats,
    const struct ia_css_3a_output *isp_stats)
{
	int isp_width, host_width, height, i;
	struct ia_css_3a_output *host_ptr;

	assert(host_stats);
	assert(host_stats->data);
	assert(isp_stats);

	isp_width  = host_stats->grid.aligned_width;
	host_width = host_stats->grid.width;
	height     = host_stats->grid.height;
	host_ptr   = host_stats->data;

	/* Getting 3A statistics from DMEM does not involve any
	 * transformation (like the VMEM version), we just copy the data
	 * using a different output width. */
	for (i = 0; i < height; i++) {
		memcpy(host_ptr, isp_stats, host_width * sizeof(*host_ptr));
		isp_stats += isp_width;
		host_ptr += host_width;
	}
}

/* MW: this is an ISP function */
static inline int
merge_hi_lo_14(unsigned short hi, unsigned short lo)
{
	int val = (int)((((unsigned int)hi << 14) & 0xfffc000) |
			((unsigned int)lo & 0x3fff));
	return val;
}

void
ia_css_s3a_vmem_decode(
    struct ia_css_3a_statistics *host_stats,
    const u16 *isp_stats_hi,
    const uint16_t *isp_stats_lo)
{
	int out_width, out_height, chunk, rest, kmax, y, x, k, elm_start, elm, ofs;
	const u16 *hi, *lo;
	struct ia_css_3a_output *output;

	assert(host_stats);
	assert(host_stats->data);
	assert(isp_stats_hi);
	assert(isp_stats_lo);

	output = host_stats->data;
	out_width  = host_stats->grid.width;
	out_height = host_stats->grid.height;
	hi = isp_stats_hi;
	lo = isp_stats_lo;

	chunk = ISP_VEC_NELEMS >> host_stats->grid.deci_factor_log2;
	chunk = max(chunk, 1);

	for (y = 0; y < out_height; y++) {
		elm_start = y * ISP_S3ATBL_HI_LO_STRIDE;
		rest = out_width;
		x = 0;
		while (x < out_width) {
			kmax = (rest > chunk) ? chunk : rest;
			ofs = y * out_width + x;
			elm = elm_start + x * sizeof(*output) / sizeof(int32_t);
			for (k = 0; k < kmax; k++, elm++) {
				output[ofs + k].ae_y    = merge_hi_lo_14(
							      hi[elm + chunk * 0], lo[elm + chunk * 0]);
				output[ofs + k].awb_cnt = merge_hi_lo_14(
							      hi[elm + chunk * 1], lo[elm + chunk * 1]);
				output[ofs + k].awb_gr  = merge_hi_lo_14(
							      hi[elm + chunk * 2], lo[elm + chunk * 2]);
				output[ofs + k].awb_r   = merge_hi_lo_14(
							      hi[elm + chunk * 3], lo[elm + chunk * 3]);
				output[ofs + k].awb_b   = merge_hi_lo_14(
							      hi[elm + chunk * 4], lo[elm + chunk * 4]);
				output[ofs + k].awb_gb  = merge_hi_lo_14(
							      hi[elm + chunk * 5], lo[elm + chunk * 5]);
				output[ofs + k].af_hpf1 = merge_hi_lo_14(
							      hi[elm + chunk * 6], lo[elm + chunk * 6]);
				output[ofs + k].af_hpf2 = merge_hi_lo_14(
							      hi[elm + chunk * 7], lo[elm + chunk * 7]);
			}
			x += chunk;
			rest -= chunk;
		}
	}
}
