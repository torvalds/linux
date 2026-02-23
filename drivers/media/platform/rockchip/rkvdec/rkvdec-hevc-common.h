/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip video decoder hevc common functions
 *
 * Copyright (C) 2025 Collabora, Ltd.
 *      Detlev Casanova <detlev.casanova@collabora.com>
 *
 * Copyright (C) 2023 Collabora, Ltd.
 *      Sebastian Fricke <sebastian.fricke@collabora.com>
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *	Boris Brezillon <boris.brezillon@collabora.com>
 *
 * Copyright (C) 2016 Rockchip Electronics Co., Ltd.
 *	Jeffy Chen <jeffy.chen@rock-chips.com>
 */

#include <media/v4l2-mem2mem.h>
#include <linux/types.h>

#include "rkvdec.h"

struct rkvdec_rps_refs {
	u16 lt_ref_pic_poc_lsb;
	u16 used_by_curr_pic_lt_flag	: 1;
	u16 reserved			: 15;
} __packed;

struct rkvdec_rps_short_term_ref_set {
	u32 num_negative	: 4;
	u32 num_positive	: 4;
	u32 delta_poc0		: 16;
	u32 used_flag0		: 1;
	u32 delta_poc1		: 16;
	u32 used_flag1		: 1;
	u32 delta_poc2		: 16;
	u32 used_flag2		: 1;
	u32 delta_poc3		: 16;
	u32 used_flag3		: 1;
	u32 delta_poc4		: 16;
	u32 used_flag4		: 1;
	u32 delta_poc5		: 16;
	u32 used_flag5		: 1;
	u32 delta_poc6		: 16;
	u32 used_flag6		: 1;
	u32 delta_poc7		: 16;
	u32 used_flag7		: 1;
	u32 delta_poc8		: 16;
	u32 used_flag8		: 1;
	u32 delta_poc9		: 16;
	u32 used_flag9		: 1;
	u32 delta_poc10		: 16;
	u32 used_flag10		: 1;
	u32 delta_poc11		: 16;
	u32 used_flag11		: 1;
	u32 delta_poc12		: 16;
	u32 used_flag12		: 1;
	u32 delta_poc13		: 16;
	u32 used_flag13		: 1;
	u32 delta_poc14		: 16;
	u32 used_flag14		: 1;
	u32 reserved_bits	: 25;
	u32 reserved[3];
} __packed;

struct rkvdec_rps {
	struct rkvdec_rps_refs refs[32];
	struct rkvdec_rps_short_term_ref_set short_term_ref_sets[64];
} __packed;

struct rkvdec_hevc_run {
	struct rkvdec_run base;
	const struct v4l2_ctrl_hevc_slice_params *slices_params;
	const struct v4l2_ctrl_hevc_decode_params *decode_params;
	const struct v4l2_ctrl_hevc_sps *sps;
	const struct v4l2_ctrl_hevc_pps *pps;
	const struct v4l2_ctrl_hevc_scaling_matrix *scaling_matrix;
	const struct v4l2_ctrl_hevc_ext_sps_st_rps *ext_sps_st_rps;
	const struct v4l2_ctrl_hevc_ext_sps_lt_rps *ext_sps_lt_rps;
	int num_slices;
};

struct scaling_factor {
	u8 scalingfactor0[1248];
	u8 scalingfactor1[96];	/*4X4 TU Rotate, total 16X4*/
	u8 scalingdc[12];	/*N1005 Vienna Meeting*/
	u8 reserved[4];		/*16Bytes align*/
};

void compute_tiles_uniform(struct rkvdec_hevc_run *run, u16 log2_min_cb_size,
			   u16 width, u16 height, s32 pic_in_cts_width,
			   s32 pic_in_cts_height, u16 *column_width, u16 *row_height);
void compute_tiles_non_uniform(struct rkvdec_hevc_run *run, u16 log2_min_cb_size,
			       u16 width, u16 height, s32 pic_in_cts_width,
			       s32 pic_in_cts_height, u16 *column_width, u16 *row_height);
void rkvdec_hevc_assemble_hw_rps(struct rkvdec_hevc_run *run, struct rkvdec_rps *rps,
				 struct v4l2_ctrl_hevc_ext_sps_st_rps *st_cache);
void rkvdec_hevc_assemble_hw_scaling_list(struct rkvdec_ctx *ctx,
					  struct rkvdec_hevc_run *run,
					  struct scaling_factor *scaling_factor,
					  struct v4l2_ctrl_hevc_scaling_matrix *cache);
struct vb2_buffer *get_ref_buf(struct rkvdec_ctx *ctx,
			       struct rkvdec_hevc_run *run,
			       unsigned int dpb_idx);
int rkvdec_hevc_adjust_fmt(struct rkvdec_ctx *ctx, struct v4l2_format *f);
enum rkvdec_image_fmt rkvdec_hevc_get_image_fmt(struct rkvdec_ctx *ctx, struct v4l2_ctrl *ctrl);
void rkvdec_hevc_run_preamble(struct rkvdec_ctx *ctx, struct rkvdec_hevc_run *run);
