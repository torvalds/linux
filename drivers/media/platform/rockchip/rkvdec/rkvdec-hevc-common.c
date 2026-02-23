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

/* Store the Short term ref pic set calculated values */
struct calculated_rps_st_set {
	u8 num_delta_pocs;
	u8 num_negative_pics;
	u8 num_positive_pics;
	u8 used_by_curr_pic_s0[16];
	u8 used_by_curr_pic_s1[16];
	s32 delta_poc_s0[16];
	s32 delta_poc_s1[16];
};

void compute_tiles_uniform(struct rkvdec_hevc_run *run, u16 log2_min_cb_size,
			   u16 width, u16 height, s32 pic_in_cts_width,
			   s32 pic_in_cts_height, u16 *column_width, u16 *row_height)
{
	const struct v4l2_ctrl_hevc_pps *pps = run->pps;
	int i;

	for (i = 0; i < pps->num_tile_columns_minus1 + 1; i++)
		column_width[i] = ((i + 1) * pic_in_cts_width) /
				  (pps->num_tile_columns_minus1 + 1) -
				  (i * pic_in_cts_width) /
				  (pps->num_tile_columns_minus1 + 1);

	for (i = 0; i < pps->num_tile_rows_minus1 + 1; i++)
		row_height[i] = ((i + 1) * pic_in_cts_height) /
				(pps->num_tile_rows_minus1 + 1) -
				(i * pic_in_cts_height) /
				(pps->num_tile_rows_minus1 + 1);
}

void compute_tiles_non_uniform(struct rkvdec_hevc_run *run, u16 log2_min_cb_size,
			       u16 width, u16 height, s32 pic_in_cts_width,
			       s32 pic_in_cts_height, u16 *column_width, u16 *row_height)
{
	const struct v4l2_ctrl_hevc_pps *pps = run->pps;
	s32 sum = 0;
	int i;

	for (i = 0; i < pps->num_tile_columns_minus1; i++) {
		column_width[i] = pps->column_width_minus1[i] + 1;
		sum += column_width[i];
	}
	column_width[i] = pic_in_cts_width - sum;

	sum = 0;
	for (i = 0; i < pps->num_tile_rows_minus1; i++) {
		row_height[i] = pps->row_height_minus1[i] + 1;
		sum += row_height[i];
	}
	row_height[i] = pic_in_cts_height - sum;
}

static void set_ref_poc(struct rkvdec_rps_short_term_ref_set *set, int poc, int value, int flag)
{
	switch (poc) {
	case 0:
		set->delta_poc0 = value;
		set->used_flag0 = flag;
		break;
	case 1:
		set->delta_poc1 = value;
		set->used_flag1 = flag;
		break;
	case 2:
		set->delta_poc2 = value;
		set->used_flag2 = flag;
		break;
	case 3:
		set->delta_poc3 = value;
		set->used_flag3 = flag;
		break;
	case 4:
		set->delta_poc4 = value;
		set->used_flag4 = flag;
		break;
	case 5:
		set->delta_poc5 = value;
		set->used_flag5 = flag;
		break;
	case 6:
		set->delta_poc6 = value;
		set->used_flag6 = flag;
		break;
	case 7:
		set->delta_poc7 = value;
		set->used_flag7 = flag;
		break;
	case 8:
		set->delta_poc8 = value;
		set->used_flag8 = flag;
		break;
	case 9:
		set->delta_poc9 = value;
		set->used_flag9 = flag;
		break;
	case 10:
		set->delta_poc10 = value;
		set->used_flag10 = flag;
		break;
	case 11:
		set->delta_poc11 = value;
		set->used_flag11 = flag;
		break;
	case 12:
		set->delta_poc12 = value;
		set->used_flag12 = flag;
		break;
	case 13:
		set->delta_poc13 = value;
		set->used_flag13 = flag;
		break;
	case 14:
		set->delta_poc14 = value;
		set->used_flag14 = flag;
		break;
	}
}

static void assemble_scalingfactor0(struct rkvdec_ctx *ctx, u8 *output,
				    const struct v4l2_ctrl_hevc_scaling_matrix *input)
{
	const struct rkvdec_variant *variant = ctx->dev->variant;
	int offset = 0;

	variant->ops->flatten_matrices(output, (const u8 *)input->scaling_list_4x4, 6, 4);
	offset = 6 * 16 * sizeof(u8);
	variant->ops->flatten_matrices(output + offset, (const u8 *)input->scaling_list_8x8, 6, 8);
	offset += 6 * 64 * sizeof(u8);
	variant->ops->flatten_matrices(output + offset, (const u8 *)input->scaling_list_16x16,
				       6, 8);
	offset += 6 * 64 * sizeof(u8);
	/* Add a 128 byte padding with 0s between the two 32x32 matrices */
	variant->ops->flatten_matrices(output + offset, (const u8 *)input->scaling_list_32x32,
				       1, 8);
	offset += 64 * sizeof(u8);
	memset(output + offset, 0, 128);
	offset += 128 * sizeof(u8);
	variant->ops->flatten_matrices(output + offset,
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

static void translate_scaling_list(struct rkvdec_ctx *ctx, struct scaling_factor *output,
				   const struct v4l2_ctrl_hevc_scaling_matrix *input)
{
	assemble_scalingfactor0(ctx, output->scalingfactor0, input);
	memcpy(output->scalingfactor1, (const u8 *)input->scaling_list_4x4, 96);
	assemble_scalingdc(output->scalingdc, input);
	memset(output->reserved, 0, 4 * sizeof(u8));
}

void rkvdec_hevc_assemble_hw_scaling_list(struct rkvdec_ctx *ctx,
					  struct rkvdec_hevc_run *run,
					  struct scaling_factor *scaling_factor,
					  struct v4l2_ctrl_hevc_scaling_matrix *cache)
{
	const struct v4l2_ctrl_hevc_scaling_matrix *scaling = run->scaling_matrix;

	if (!memcmp(cache, scaling,
		    sizeof(struct v4l2_ctrl_hevc_scaling_matrix)))
		return;

	translate_scaling_list(ctx, scaling_factor, scaling);

	memcpy(cache, scaling,
	       sizeof(struct v4l2_ctrl_hevc_scaling_matrix));
}

static void rkvdec_hevc_assemble_hw_lt_rps(struct rkvdec_hevc_run *run, struct rkvdec_rps *rps)
{
	const struct v4l2_ctrl_hevc_sps *sps = run->sps;

	if (!run->ext_sps_lt_rps)
		return;

	for (int i = 0; i < sps->num_long_term_ref_pics_sps; i++) {
		rps->refs[i].lt_ref_pic_poc_lsb =
			run->ext_sps_lt_rps[i].lt_ref_pic_poc_lsb_sps;
		rps->refs[i].used_by_curr_pic_lt_flag =
			!!(run->ext_sps_lt_rps[i].flags & V4L2_HEVC_EXT_SPS_LT_RPS_FLAG_USED_LT);
	}
}

static void rkvdec_hevc_assemble_hw_st_rps(struct rkvdec_hevc_run *run, struct rkvdec_rps *rps,
					   struct calculated_rps_st_set *calculated_rps_st_sets)
{
	const struct v4l2_ctrl_hevc_sps *sps = run->sps;

	for (int i = 0; i < sps->num_short_term_ref_pic_sets; i++) {
		int poc = 0;
		int j = 0;
		const struct calculated_rps_st_set *set = &calculated_rps_st_sets[i];

		rps->short_term_ref_sets[i].num_negative = set->num_negative_pics;
		rps->short_term_ref_sets[i].num_positive = set->num_positive_pics;

		for (; j < set->num_negative_pics; j++) {
			set_ref_poc(&rps->short_term_ref_sets[i], j,
				    set->delta_poc_s0[j], set->used_by_curr_pic_s0[j]);
		}
		poc = j;

		for (j = 0; j < set->num_positive_pics; j++) {
			set_ref_poc(&rps->short_term_ref_sets[i], poc + j,
				    set->delta_poc_s1[j], set->used_by_curr_pic_s1[j]);
		}
	}
}

/*
 * Compute the short term ref pic set parameters based on its reference short term ref pic
 */
static void st_ref_pic_set_prediction(struct rkvdec_hevc_run *run, int idx,
				      struct calculated_rps_st_set *calculated_rps_st_sets)
{
	const struct v4l2_ctrl_hevc_ext_sps_st_rps *rps_data = &run->ext_sps_st_rps[idx];
	struct calculated_rps_st_set *st_rps = &calculated_rps_st_sets[idx];
	struct calculated_rps_st_set *ref_rps;
	u8 st_rps_idx = idx;
	u8 ref_rps_idx = 0;
	s16 delta_rps = 0;
	u8 use_delta_flag[16] = { 0 };
	u8 used_by_curr_pic_flag[16] = { 0 };
	int i, j;
	int dPoc;

	ref_rps_idx = st_rps_idx - (rps_data->delta_idx_minus1 + 1); /* 7-59 */
	delta_rps = (1 - 2 * rps_data->delta_rps_sign) *
		   (rps_data->abs_delta_rps_minus1 + 1); /* 7-60 */

	ref_rps = &calculated_rps_st_sets[ref_rps_idx];

	for (j = 0; j <= ref_rps->num_delta_pocs; j++) {
		used_by_curr_pic_flag[j] = !!(rps_data->used_by_curr_pic & (1 << j));
		use_delta_flag[j] = !!(rps_data->use_delta_flag & (1 << j));
	}

	/* 7-61: calculate num_negative_pics, delta_poc_s0 and used_by_curr_pic_s0 */
	i = 0;
	for (j = (ref_rps->num_positive_pics - 1); j >= 0; j--) {
		dPoc = ref_rps->delta_poc_s1[j] + delta_rps;
		if (dPoc < 0 && use_delta_flag[ref_rps->num_negative_pics + j]) {
			st_rps->delta_poc_s0[i] = dPoc;
			st_rps->used_by_curr_pic_s0[i++] =
				used_by_curr_pic_flag[ref_rps->num_negative_pics + j];
		}
	}
	if (delta_rps < 0 && use_delta_flag[ref_rps->num_delta_pocs]) {
		st_rps->delta_poc_s0[i] = delta_rps;
		st_rps->used_by_curr_pic_s0[i++] = used_by_curr_pic_flag[ref_rps->num_delta_pocs];
	}
	for (j = 0; j < ref_rps->num_negative_pics; j++) {
		dPoc = ref_rps->delta_poc_s0[j] + delta_rps;
		if (dPoc < 0 && use_delta_flag[j]) {
			st_rps->delta_poc_s0[i] = dPoc;
			st_rps->used_by_curr_pic_s0[i++] = used_by_curr_pic_flag[j];
		}
	}
	st_rps->num_negative_pics = i;

	/* 7-62: calculate num_positive_pics, delta_poc_s1 and used_by_curr_pic_s1 */
	i = 0;
	for (j = (ref_rps->num_negative_pics - 1); j >= 0; j--) {
		dPoc = ref_rps->delta_poc_s0[j] + delta_rps;
		if (dPoc > 0 && use_delta_flag[j]) {
			st_rps->delta_poc_s1[i] = dPoc;
			st_rps->used_by_curr_pic_s1[i++] = used_by_curr_pic_flag[j];
		}
	}
	if (delta_rps > 0 && use_delta_flag[ref_rps->num_delta_pocs]) {
		st_rps->delta_poc_s1[i] = delta_rps;
		st_rps->used_by_curr_pic_s1[i++] = used_by_curr_pic_flag[ref_rps->num_delta_pocs];
	}
	for (j = 0; j < ref_rps->num_positive_pics; j++) {
		dPoc = ref_rps->delta_poc_s1[j] + delta_rps;
		if (dPoc > 0 && use_delta_flag[ref_rps->num_negative_pics + j]) {
			st_rps->delta_poc_s1[i] = dPoc;
			st_rps->used_by_curr_pic_s1[i++] =
				used_by_curr_pic_flag[ref_rps->num_negative_pics + j];
		}
	}
	st_rps->num_positive_pics = i;

	st_rps->num_delta_pocs = st_rps->num_positive_pics + st_rps->num_negative_pics;
}

/*
 * Compute the short term ref pic set parameters based on the control's data.
 */
static void st_ref_pic_set_calculate(struct rkvdec_hevc_run *run, int idx,
				     struct calculated_rps_st_set *calculated_rps_st_sets)
{
	const struct v4l2_ctrl_hevc_ext_sps_st_rps *rps_data = &run->ext_sps_st_rps[idx];
	struct calculated_rps_st_set *st_rps = &calculated_rps_st_sets[idx];
	int j, i = 0;

	/* 7-63 */
	st_rps->num_negative_pics = rps_data->num_negative_pics;
	/* 7-64 */
	st_rps->num_positive_pics = rps_data->num_positive_pics;

	for (i = 0; i < st_rps->num_negative_pics; i++) {
		/* 7-65 */
		st_rps->used_by_curr_pic_s0[i] = !!(rps_data->used_by_curr_pic & (1 << i));

		if (i == 0) {
			/* 7-67 */
			st_rps->delta_poc_s0[i] = -(rps_data->delta_poc_s0_minus1[i] + 1);
		} else {
			/* 7-69 */
			st_rps->delta_poc_s0[i] =
				st_rps->delta_poc_s0[i - 1] -
				(rps_data->delta_poc_s0_minus1[i] + 1);
		}
	}

	for (j = 0; j < st_rps->num_positive_pics; j++) {
		/* 7-66 */
		st_rps->used_by_curr_pic_s1[j] = !!(rps_data->used_by_curr_pic & (1 << (i + j)));

		if (j == 0) {
			/* 7-68 */
			st_rps->delta_poc_s1[j] = rps_data->delta_poc_s1_minus1[j] + 1;
		} else {
			/* 7-70 */
			st_rps->delta_poc_s1[j] =
				st_rps->delta_poc_s1[j - 1] +
				(rps_data->delta_poc_s1_minus1[j] + 1);
		}
	}

	/* 7-71 */
	st_rps->num_delta_pocs = st_rps->num_positive_pics + st_rps->num_negative_pics;
}

static void rkvdec_hevc_prepare_hw_st_rps(struct rkvdec_hevc_run *run, struct rkvdec_rps *rps,
					  struct v4l2_ctrl_hevc_ext_sps_st_rps *cache)
{
	int idx;

	if (!run->ext_sps_st_rps)
		return;

	if (!memcmp(cache, run->ext_sps_st_rps, sizeof(struct v4l2_ctrl_hevc_ext_sps_st_rps)))
		return;

	struct calculated_rps_st_set *calculated_rps_st_sets =
		kzalloc(sizeof(struct calculated_rps_st_set) *
			run->sps->num_short_term_ref_pic_sets, GFP_KERNEL);

	for (idx = 0; idx < run->sps->num_short_term_ref_pic_sets; idx++) {
		const struct v4l2_ctrl_hevc_ext_sps_st_rps *rps_data = &run->ext_sps_st_rps[idx];

		if (rps_data->flags & V4L2_HEVC_EXT_SPS_ST_RPS_FLAG_INTER_REF_PIC_SET_PRED)
			st_ref_pic_set_prediction(run, idx, calculated_rps_st_sets);
		else
			st_ref_pic_set_calculate(run, idx, calculated_rps_st_sets);
	}

	rkvdec_hevc_assemble_hw_st_rps(run, rps, calculated_rps_st_sets);

	kfree(calculated_rps_st_sets);

	memcpy(cache, run->ext_sps_st_rps, sizeof(struct v4l2_ctrl_hevc_ext_sps_st_rps));
}

void rkvdec_hevc_assemble_hw_rps(struct rkvdec_hevc_run *run, struct rkvdec_rps *rps,
				 struct v4l2_ctrl_hevc_ext_sps_st_rps *st_cache)
{
	rkvdec_hevc_prepare_hw_st_rps(run, rps, st_cache);
	rkvdec_hevc_assemble_hw_lt_rps(run, rps);
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

	if (ctx->has_sps_st_rps) {
		ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
				      V4L2_CID_STATELESS_HEVC_EXT_SPS_ST_RPS);
		run->ext_sps_st_rps = ctrl ? ctrl->p_cur.p : NULL;
	}
	if (ctx->has_sps_lt_rps) {
		ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
				      V4L2_CID_STATELESS_HEVC_EXT_SPS_LT_RPS);
		run->ext_sps_lt_rps = ctrl ? ctrl->p_cur.p : NULL;
	}

	rkvdec_run_preamble(ctx, &run->base);
}
