/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_CODEC_H
#define _AMPHION_VPU_CODEC_H

struct vpu_encode_params {
	u32 input_format;
	u32 codec_format;
	u32 profile;
	u32 tier;
	u32 level;
	struct v4l2_fract frame_rate;
	u32 src_stride;
	u32 src_width;
	u32 src_height;
	struct v4l2_rect crop;
	u32 out_width;
	u32 out_height;

	u32 gop_length;
	u32 bframes;

	u32 rc_enable;
	u32 rc_mode;
	u32 bitrate;
	u32 bitrate_min;
	u32 bitrate_max;

	u32 i_frame_qp;
	u32 p_frame_qp;
	u32 b_frame_qp;
	u32 qp_min;
	u32 qp_max;
	u32 qp_min_i;
	u32 qp_max_i;

	struct {
		u32 enable;
		u32 idc;
		u32 width;
		u32 height;
	} sar;

	struct {
		u32 primaries;
		u32 transfer;
		u32 matrix;
		u32 full_range;
	} color;
};

struct vpu_decode_params {
	u32 codec_format;
	u32 output_format;
	u32 b_dis_reorder;
	u32 b_non_frame;
	u32 frame_count;
	u32 end_flag;
	struct {
		u32 base;
		u32 size;
	} udata;
};

#endif
