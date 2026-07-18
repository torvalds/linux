/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip video decoder h264 common functions
 *
 * Copyright (C) 2025 Collabora, Ltd.
 *	Detlev Casanova <detlev.casanova@collabora.com>
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *	Boris Brezillon <boris.brezillon@collabora.com>
 *
 * Copyright (C) 2016 Rockchip Electronics Co., Ltd.
 *	Jeffy Chen <jeffy.chen@rock-chips.com>
 */

#include <media/v4l2-h264.h>
#include <media/v4l2-mem2mem.h>

#include "rkvdec.h"
#include "rkvdec-bitwriter.h"

struct rkvdec_h264_scaling_list {
	u8 scaling_list_4x4[6][16];
	u8 scaling_list_8x8[6][64];
	u8 padding[128];
};

struct rkvdec_h264_reflists {
	struct v4l2_h264_reference p[V4L2_H264_REF_LIST_LEN];
	struct v4l2_h264_reference b0[V4L2_H264_REF_LIST_LEN];
	struct v4l2_h264_reference b1[V4L2_H264_REF_LIST_LEN];
};

struct rkvdec_h264_run {
	struct rkvdec_run base;
	const struct v4l2_ctrl_h264_decode_params *decode_params;
	const struct v4l2_ctrl_h264_sps *sps;
	const struct v4l2_ctrl_h264_pps *pps;
	const struct v4l2_ctrl_h264_scaling_matrix *scaling_matrix;
	struct vb2_buffer *ref_buf[V4L2_H264_NUM_DPB_ENTRIES];
};

#define RPS_FRAME_NUM(i)		BW_FIELD((i) * 16, 16)
#define RPS_ENTRY_DPB_INFO(l, e)	BW_FIELD(288 + (l) * 7 * 32 + (e) * 7, 5) //l: 0-2, e: 0-31
#define RPS_ENTRY_BOTTOM_FLAG(l, e)	BW_FIELD(293 + (l) * 7 * 32 + (e) * 7, 1) //l: 0-2, e: 0-31
#define RPS_ENTRY_VIEW_INDEX_OFF(l, e)	BW_FIELD(294 + (l) * 7 * 32 + (e) * 7, 1) //l: 0-2, e: 0-31

#define RKVDEC_H264_RPS_SIZE		ALIGN(288 + 3 * 7 * 32, 128)

struct rkvdec_rps {
	u32 info[RKVDEC_H264_RPS_SIZE / 8 / 4];
};

void lookup_ref_buf_idx(struct rkvdec_ctx *ctx, struct rkvdec_h264_run *run);
void assemble_hw_rps(struct v4l2_h264_reflist_builder *builder,
		     struct rkvdec_h264_run *run,
		     struct rkvdec_h264_reflists *reflists,
		     struct rkvdec_rps *hw_rps);
void assemble_hw_scaling_list(struct rkvdec_h264_run *run,
			      struct rkvdec_h264_scaling_list *scaling_list);
int rkvdec_h264_adjust_fmt(struct rkvdec_ctx *ctx, struct v4l2_format *f);
enum rkvdec_image_fmt rkvdec_h264_get_image_fmt(struct rkvdec_ctx *ctx, struct v4l2_ctrl *ctrl);
int rkvdec_h264_validate_sps(struct rkvdec_ctx *ctx, const struct v4l2_ctrl_h264_sps *sps);
void rkvdec_h264_run_preamble(struct rkvdec_ctx *ctx, struct rkvdec_h264_run *run);
