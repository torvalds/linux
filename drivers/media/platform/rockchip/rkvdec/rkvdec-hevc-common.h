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
#include "rkvdec-bitwriter.h"

#define RPS_LT_REF_PIC_POC_LSB(i)	BW_FIELD(0 + (i) * 32, 16) // i: 0-31
#define RPS_LT_REF_USED_BY_CURR_PIC(i)	BW_FIELD(16 + (i) * 32, 1) // i: 0-31

#define RPS_ST_REF_SET_NUM_NEGATIVE(i)	BW_FIELD(1024 + ((i) * 384), 4) // i: 0-63
#define RPS_ST_REF_SET_NUM_POSITIVE(i)	BW_FIELD(1028 + ((i) * 384), 4) // i: 0-63

// i: 0-63, j: 0-14
#define RPS_ST_REF_SET_DELTA_POC(i, j)	BW_FIELD(1032 + ((i) * 384) + ((j) * 17), 16)

// i: 0-63, j: 0-14
#define RPS_ST_REF_SET_USED(i, j)	BW_FIELD(1048 + ((i) * 384) + ((j) * 17), 1)

#define RKVDEC_RPS_HEVC_SIZE		ALIGN(1032 + 64 * 384, 128)

struct rkvdec_rps {
	u32 info[RKVDEC_RPS_HEVC_SIZE / 8 / 4];
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
