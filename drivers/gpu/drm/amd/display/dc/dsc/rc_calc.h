
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

#ifndef __RC_CALC_H__
#define __RC_CALC_H__


#define QP_SET_SIZE 15

typedef int qp_set[QP_SET_SIZE];

struct rc_params {
	int      rc_quant_incr_limit0;
	int      rc_quant_incr_limit1;
	int      initial_fullness_offset;
	int      initial_xmit_delay;
	int      first_line_bpg_offset;
	int      second_line_bpg_offset;
	int      flatness_min_qp;
	int      flatness_max_qp;
	int      flatness_det_thresh;
	qp_set   qp_min;
	qp_set   qp_max;
	qp_set   ofs;
	int      rc_model_size;
	int      rc_edge_factor;
	int      rc_tgt_offset_hi;
	int      rc_tgt_offset_lo;
	int      rc_buf_thresh[QP_SET_SIZE - 1];
};

enum colour_mode {
	CM_RGB,   /* 444 RGB */
	CM_444,   /* 444 YUV or simple 422 */
	CM_422,   /* native 422 */
	CM_420    /* native 420 */
};

enum bits_per_comp {
	BPC_8  =  8,
	BPC_10 = 10,
	BPC_12 = 12
};

enum max_min {
	DAL_MM_MIN = 0,
	DAL_MM_MAX = 1
};

struct qp_entry {
	float         bpp;
	const qp_set  qps;
};

typedef struct qp_entry qp_table[];

void calc_rc_params(struct rc_params *rc, const struct drm_dsc_config *pps);
u32 calc_dsc_bytes_per_pixel(const struct drm_dsc_config *pps);
u32 calc_dsc_bpp_x16(u32 stream_bandwidth_kbps, u32 pix_clk_100hz,
		     u32 bpp_increment_div);

#endif

