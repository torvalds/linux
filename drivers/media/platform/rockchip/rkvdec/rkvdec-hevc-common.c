// SPDX-License-Identifier: GPL-2.0
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

#include <linux/v4l2-common.h>
#include <media/v4l2-mem2mem.h>

#include "rkvdec.h"
#include "rkvdec-hevc-common.h"

/*
 * Flip one or more matrices along their main diagonal and flatten them
 * before writing it to the memory.
 * Convert:
 * ABCD         AEIM
 * EFGH     =>  BFJN     =>     AEIMBFJNCGKODHLP
 * IJKL         CGKO
 * MNOP         DHLP
 */
static void transpose_and_flatten_matrices(u8 *output, const u8 *input,
					   int matrices, int row_length)
{
	int i, j, row, x_offset, matrix_offset, rot_index, y_offset, matrix_size, new_value;

	matrix_size = row_length * row_length;
	for (i = 0; i < matrices; i++) {
		row = 0;
		x_offset = 0;
		matrix_offset = i * matrix_size;
		for (j = 0; j < matrix_size; j++) {
			y_offset = j - (row * row_length);
			rot_index = y_offset * row_length + x_offset;
			new_value = *(input + i * matrix_size + j);
			output[matrix_offset + rot_index] = new_value;
			if ((j + 1) % row_length == 0) {
				row += 1;
				x_offset += 1;
			}
		}
	}
}

static void assemble_scalingfactor0(u8 *output, const struct v4l2_ctrl_hevc_scaling_matrix *input)
{
	int offset = 0;

	transpose_and_flatten_matrices(output, (const u8 *)input->scaling_list_4x4, 6, 4);
	offset = 6 * 16 * sizeof(u8);
	transpose_and_flatten_matrices(output + offset, (const u8 *)input->scaling_list_8x8, 6, 8);
	offset += 6 * 64 * sizeof(u8);
	transpose_and_flatten_matrices(output + offset,
				       (const u8 *)input->scaling_list_16x16, 6, 8);
	offset += 6 * 64 * sizeof(u8);
	/* Add a 128 byte padding with 0s between the two 32x32 matrices */
	transpose_and_flatten_matrices(output + offset,
				       (const u8 *)input->scaling_list_32x32, 1, 8);
	offset += 64 * sizeof(u8);
	memset(output + offset, 0, 128);
	offset += 128 * sizeof(u8);
	transpose_and_flatten_matrices(output + offset,
				       (const u8 *)input->scaling_list_32x32 + (64 * sizeof(u8)),
				       1, 8);
	offset += 64 * sizeof(u8);
	memset(output + offset, 0, 128);
}

/*
 * Required layout:
 * A = scaling_list_dc_coef_16x16
 * B = scaling_list_dc_coef_32x32
 * 0 = Padding
 *
 * A, A, A, A, A, A, B, 0, 0, B, 0, 0
 */
static void assemble_scalingdc(u8 *output, const struct v4l2_ctrl_hevc_scaling_matrix *input)
{
	u8 list_32x32[6] = {0};

	memcpy(output, input->scaling_list_dc_coef_16x16, 6 * sizeof(u8));
	list_32x32[0] = input->scaling_list_dc_coef_32x32[0];
	list_32x32[3] = input->scaling_list_dc_coef_32x32[1];
	memcpy(output + 6 * sizeof(u8), list_32x32, 6 * sizeof(u8));
}

static void translate_scaling_list(struct scaling_factor *output,
				   const struct v4l2_ctrl_hevc_scaling_matrix *input)
{
	assemble_scalingfactor0(output->scalingfactor0, input);
	memcpy(output->scalingfactor1, (const u8 *)input->scaling_list_4x4, 96);
	assemble_scalingdc(output->scalingdc, input);
	memset(output->reserved, 0, 4 * sizeof(u8));
}

void rkvdec_hevc_assemble_hw_scaling_list(struct rkvdec_hevc_run *run,
					  struct scaling_factor *scaling_factor,
					  struct v4l2_ctrl_hevc_scaling_matrix *cache)
{
	const struct v4l2_ctrl_hevc_scaling_matrix *scaling = run->scaling_matrix;

	if (!memcmp(cache, scaling,
		    sizeof(struct v4l2_ctrl_hevc_scaling_matrix)))
		return;

	translate_scaling_list(scaling_factor, scaling);

	memcpy(cache, scaling,
	       sizeof(struct v4l2_ctrl_hevc_scaling_matrix));
}

struct vb2_buffer *
get_ref_buf(struct rkvdec_ctx *ctx, struct rkvdec_hevc_run *run,
	    unsigned int dpb_idx)
{
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	const struct v4l2_ctrl_hevc_decode_params *decode_params = run->decode_params;
	const struct v4l2_hevc_dpb_entry *dpb = decode_params->dpb;
	struct vb2_queue *cap_q = &m2m_ctx->cap_q_ctx.q;
	struct vb2_buffer *buf = NULL;

	if (dpb_idx < decode_params->num_active_dpb_entries)
		buf = vb2_find_buffer(cap_q, dpb[dpb_idx].timestamp);

	/*
	 * If a DPB entry is unused or invalid, the address of current destination
	 * buffer is returned.
	 */
	if (!buf)
		return &run->base.bufs.dst->vb2_buf;

	return buf;
}

#define RKVDEC_HEVC_MAX_DEPTH_IN_BYTES		2

int rkvdec_hevc_adjust_fmt(struct rkvdec_ctx *ctx, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *fmt = &f->fmt.pix_mp;

	fmt->num_planes = 1;
	if (!fmt->plane_fmt[0].sizeimage)
		fmt->plane_fmt[0].sizeimage = fmt->width * fmt->height *
					      RKVDEC_HEVC_MAX_DEPTH_IN_BYTES;
	return 0;
}

enum rkvdec_image_fmt rkvdec_hevc_get_image_fmt(struct rkvdec_ctx *ctx,
						struct v4l2_ctrl *ctrl)
{
	const struct v4l2_ctrl_hevc_sps *sps = ctrl->p_new.p_hevc_sps;

	if (ctrl->id != V4L2_CID_STATELESS_HEVC_SPS)
		return RKVDEC_IMG_FMT_ANY;

	if (sps->bit_depth_luma_minus8 == 0) {
		if (sps->chroma_format_idc == 2)
			return RKVDEC_IMG_FMT_422_8BIT;
		else
			return RKVDEC_IMG_FMT_420_8BIT;
	} else if (sps->bit_depth_luma_minus8 == 2) {
		if (sps->chroma_format_idc == 2)
			return RKVDEC_IMG_FMT_422_10BIT;
		else
			return RKVDEC_IMG_FMT_420_10BIT;
	}

	return RKVDEC_IMG_FMT_ANY;
}

void rkvdec_hevc_run_preamble(struct rkvdec_ctx *ctx,
			      struct rkvdec_hevc_run *run)
{
	struct v4l2_ctrl *ctrl;

	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_DECODE_PARAMS);
	run->decode_params = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_SLICE_PARAMS);
	run->slices_params = ctrl ? ctrl->p_cur.p : NULL;
	run->num_slices = ctrl ? ctrl->new_elems : 0;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_SPS);
	run->sps = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_PPS);
	run->pps = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_SCALING_MATRIX);
	run->scaling_matrix = ctrl ? ctrl->p_cur.p : NULL;

	rkvdec_run_preamble(ctx, &run->base);
}
