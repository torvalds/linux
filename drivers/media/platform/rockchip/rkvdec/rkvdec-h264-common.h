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

struct rkvdec_rps_entry {
	u32 dpb_info0:          5;
	u32 bottom_flag0:       1;
	u32 view_index_off0:    1;
	u32 dpb_info1:          5;
	u32 bottom_flag1:       1;
	u32 view_index_off1:    1;
	u32 dpb_info2:          5;
	u32 bottom_flag2:       1;
	u32 view_index_off2:    1;
	u32 dpb_info3:          5;
	u32 bottom_flag3:       1;
	u32 view_index_off3:    1;
	u32 dpb_info4:          5;
	u32 bottom_flag4:       1;
	u32 view_index_off4:    1;
	u32 dpb_info5:          5;
	u32 bottom_flag5:       1;
	u32 view_index_off5:    1;
	u32 dpb_info6:          5;
	u32 bottom_flag6:       1;
	u32 view_index_off6:    1;
	u32 dpb_info7:          5;
	u32 bottom_flag7:       1;
	u32 view_index_off7:    1;
} __packed;

struct rkvdec_rps {
	u16 frame_num[16];
	u32 reserved0;
	struct rkvdec_rps_entry entries[12];
	u32 reserved1[66];
} __packed;

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
