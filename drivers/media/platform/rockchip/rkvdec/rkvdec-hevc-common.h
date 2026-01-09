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

#include "rkvdec.h"

struct rkvdec_hevc_run {
	struct rkvdec_run base;
	const struct v4l2_ctrl_hevc_slice_params *slices_params;
	const struct v4l2_ctrl_hevc_decode_params *decode_params;
	const struct v4l2_ctrl_hevc_sps *sps;
	const struct v4l2_ctrl_hevc_pps *pps;
	const struct v4l2_ctrl_hevc_scaling_matrix *scaling_matrix;
	int num_slices;
};

struct scaling_factor {
	u8 scalingfactor0[1248];
	u8 scalingfactor1[96];	/*4X4 TU Rotate, total 16X4*/
	u8 scalingdc[12];	/*N1005 Vienna Meeting*/
	u8 reserved[4];		/*16Bytes align*/
};

void rkvdec_hevc_assemble_hw_scaling_list(struct rkvdec_hevc_run *run,
					  struct scaling_factor *scaling_factor,
					  struct v4l2_ctrl_hevc_scaling_matrix *cache);
struct vb2_buffer *get_ref_buf(struct rkvdec_ctx *ctx,
			       struct rkvdec_hevc_run *run,
			       unsigned int dpb_idx);
int rkvdec_hevc_adjust_fmt(struct rkvdec_ctx *ctx, struct v4l2_format *f);
enum rkvdec_image_fmt rkvdec_hevc_get_image_fmt(struct rkvdec_ctx *ctx, struct v4l2_ctrl *ctrl);
void rkvdec_hevc_run_preamble(struct rkvdec_ctx *ctx, struct rkvdec_hevc_run *run);
