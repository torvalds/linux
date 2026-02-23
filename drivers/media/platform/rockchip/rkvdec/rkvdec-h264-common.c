// SPDX-License-Identifier: GPL-2.0
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

#include <linux/v4l2-common.h>
#include <media/v4l2-h264.h>
#include <media/v4l2-mem2mem.h>

#include "rkvdec.h"
#include "rkvdec-h264-common.h"

#define RKVDEC_NUM_REFLIST		3

static void set_dpb_info(struct rkvdec_rps_entry *entries,
			 u8 reflist,
			 u8 refnum,
			 u8 info,
			 bool bottom)
{
	struct rkvdec_rps_entry *entry = &entries[(reflist * 4) + refnum / 8];
	u8 idx = refnum % 8;

	switch (idx) {
	case 0:
		entry->dpb_info0 = info;
		entry->bottom_flag0 = bottom;
		break;
	case 1:
		entry->dpb_info1 = info;
		entry->bottom_flag1 = bottom;
		break;
	case 2:
		entry->dpb_info2 = info;
		entry->bottom_flag2 = bottom;
		break;
	case 3:
		entry->dpb_info3 = info;
		entry->bottom_flag3 = bottom;
		break;
	case 4:
		entry->dpb_info4 = info;
		entry->bottom_flag4 = bottom;
		break;
	case 5:
		entry->dpb_info5 = info;
		entry->bottom_flag5 = bottom;
		break;
	case 6:
		entry->dpb_info6 = info;
		entry->bottom_flag6 = bottom;
		break;
	case 7:
		entry->dpb_info7 = info;
		entry->bottom_flag7 = bottom;
		break;
	}
}

void lookup_ref_buf_idx(struct rkvdec_ctx *ctx,
			struct rkvdec_h264_run *run)
{
	const struct v4l2_ctrl_h264_decode_params *dec_params = run->decode_params;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(dec_params->dpb); i++) {
		struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
		const struct v4l2_h264_dpb_entry *dpb = run->decode_params->dpb;
		struct vb2_queue *cap_q = &m2m_ctx->cap_q_ctx.q;
		struct vb2_buffer *buf = NULL;

		if (dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE) {
			buf = vb2_find_buffer(cap_q, dpb[i].reference_ts);
			if (!buf)
				pr_debug("No buffer for reference_ts %llu",
					 dpb[i].reference_ts);
		}

		run->ref_buf[i] = buf;
	}
}

void assemble_hw_rps(struct v4l2_h264_reflist_builder *builder,
		     struct rkvdec_h264_run *run,
		     struct rkvdec_h264_reflists *reflists,
		     struct rkvdec_rps *hw_rps)
{
	const struct v4l2_ctrl_h264_decode_params *dec_params = run->decode_params;
	const struct v4l2_h264_dpb_entry *dpb = dec_params->dpb;

	u32 i, j;

	memset(hw_rps, 0, sizeof(*hw_rps));

	/*
	 * Assign an invalid pic_num if DPB entry at that position is inactive.
	 * If we assign 0 in that position hardware will treat that as a real
	 * reference picture with pic_num 0, triggering output picture
	 * corruption.
	 */
	for (i = 0; i < ARRAY_SIZE(dec_params->dpb); i++) {
		if (!(dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
			continue;

		hw_rps->frame_num[i] = builder->refs[i].frame_num;
	}

	for (j = 0; j < RKVDEC_NUM_REFLIST; j++) {
		for (i = 0; i < builder->num_valid; i++) {
			struct v4l2_h264_reference *ref;
			bool dpb_valid;
			bool bottom;

			switch (j) {
			case 0:
				ref = &reflists->p[i];
				break;
			case 1:
				ref = &reflists->b0[i];
				break;
			case 2:
				ref = &reflists->b1[i];
				break;
			}

			if (WARN_ON(ref->index >= ARRAY_SIZE(dec_params->dpb)))
				continue;

			dpb_valid = !!(run->ref_buf[ref->index]);
			bottom = ref->fields == V4L2_H264_BOTTOM_FIELD_REF;

			set_dpb_info(hw_rps->entries, j, i, ref->index | (dpb_valid << 4), bottom);
		}
	}
}

void assemble_hw_scaling_list(struct rkvdec_h264_run *run,
			      struct rkvdec_h264_scaling_list *scaling_list)
{
	const struct v4l2_ctrl_h264_scaling_matrix *scaling = run->scaling_matrix;
	const struct v4l2_ctrl_h264_pps *pps = run->pps;

	if (!(pps->flags & V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT))
		return;

	BUILD_BUG_ON(sizeof(scaling_list->scaling_list_4x4) !=
		     sizeof(scaling->scaling_list_4x4));
	BUILD_BUG_ON(sizeof(scaling_list->scaling_list_8x8) !=
		     sizeof(scaling->scaling_list_8x8));

	memcpy(scaling_list->scaling_list_4x4,
	       scaling->scaling_list_4x4,
	       sizeof(scaling->scaling_list_4x4));

	memcpy(scaling_list->scaling_list_8x8,
	       scaling->scaling_list_8x8,
	       sizeof(scaling->scaling_list_8x8));
}

#define RKVDEC_H264_MAX_DEPTH_IN_BYTES		2

int rkvdec_h264_adjust_fmt(struct rkvdec_ctx *ctx,
			   struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *fmt = &f->fmt.pix_mp;

	fmt->num_planes = 1;
	if (!fmt->plane_fmt[0].sizeimage)
		fmt->plane_fmt[0].sizeimage = fmt->width * fmt->height *
					      RKVDEC_H264_MAX_DEPTH_IN_BYTES;
	return 0;
}

enum rkvdec_image_fmt rkvdec_h264_get_image_fmt(struct rkvdec_ctx *ctx,
						struct v4l2_ctrl *ctrl)
{
	const struct v4l2_ctrl_h264_sps *sps = ctrl->p_new.p_h264_sps;

	if (ctrl->id != V4L2_CID_STATELESS_H264_SPS)
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

int rkvdec_h264_validate_sps(struct rkvdec_ctx *ctx,
			     const struct v4l2_ctrl_h264_sps *sps)
{
	unsigned int width, height;

	if (sps->chroma_format_idc > 2)
		/* Only 4:0:0, 4:2:0 and 4:2:2 are supported */
		return -EINVAL;
	if (sps->bit_depth_luma_minus8 != sps->bit_depth_chroma_minus8)
		/* Luma and chroma bit depth mismatch */
		return -EINVAL;
	if (sps->bit_depth_luma_minus8 != 0 && sps->bit_depth_luma_minus8 != 2)
		/* Only 8-bit and 10-bit is supported */
		return -EINVAL;

	width = (sps->pic_width_in_mbs_minus1 + 1) * 16;
	height = (sps->pic_height_in_map_units_minus1 + 1) * 16;

	/*
	 * When frame_mbs_only_flag is not set, this is field height,
	 * which is half the final height (see (7-18) in the
	 * specification)
	 */
	if (!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY))
		height *= 2;

	if (width > ctx->coded_fmt.fmt.pix_mp.width ||
	    height > ctx->coded_fmt.fmt.pix_mp.height)
		return -EINVAL;

	return 0;
}

void rkvdec_h264_run_preamble(struct rkvdec_ctx *ctx,
			      struct rkvdec_h264_run *run)
{
	struct v4l2_ctrl *ctrl;

	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_H264_DECODE_PARAMS);
	run->decode_params = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_H264_SPS);
	run->sps = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_H264_PPS);
	run->pps = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_H264_SCALING_MATRIX);
	run->scaling_matrix = ctrl ? ctrl->p_cur.p : NULL;

	rkvdec_run_preamble(ctx, &run->base);
}
