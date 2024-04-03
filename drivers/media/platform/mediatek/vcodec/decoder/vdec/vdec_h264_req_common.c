// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#include "vdec_h264_req_common.h"

/* get used parameters for sps/pps */
#define GET_MTK_VDEC_FLAG(cond, flag) \
	{ dst_param->cond = ((src_param->flags & flag) ? (1) : (0)); }
#define GET_MTK_VDEC_PARAM(param) \
	{ dst_param->param = src_param->param; }

void mtk_vdec_h264_get_ref_list(u8 *ref_list,
				const struct v4l2_h264_reference *v4l2_ref_list,
				int num_valid)
{
	u32 i;

	/*
	 * TODO The firmware does not support field decoding. Future
	 * implementation must use v4l2_ref_list[i].fields to obtain
	 * the reference field parity.
	 */

	for (i = 0; i < num_valid; i++)
		ref_list[i] = v4l2_ref_list[i].index;

	/*
	 * The firmware expects unused reflist entries to have the value 0x20.
	 */
	memset(&ref_list[num_valid], 0x20, 32 - num_valid);
}

void *mtk_vdec_h264_get_ctrl_ptr(struct mtk_vcodec_dec_ctx *ctx, int id)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl, id);

	if (!ctrl)
		return ERR_PTR(-EINVAL);

	return ctrl->p_cur.p;
}

void mtk_vdec_h264_fill_dpb_info(struct mtk_vcodec_dec_ctx *ctx,
				 struct slice_api_h264_decode_param *decode_params,
				 struct mtk_h264_dpb_info *h264_dpb_info)
{
	const struct slice_h264_dpb_entry *dpb;
	struct vb2_queue *vq;
	struct vb2_buffer *vb;
	struct vb2_v4l2_buffer *vb2_v4l2;
	int index;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	for (index = 0; index < V4L2_H264_NUM_DPB_ENTRIES; index++) {
		dpb = &decode_params->dpb[index];
		if (!(dpb->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE)) {
			h264_dpb_info[index].reference_flag = 0;
			continue;
		}

		vb = vb2_find_buffer(vq, dpb->reference_ts);
		if (!vb) {
			dev_err(&ctx->dev->plat_dev->dev,
				"Reference invalid: dpb_index(%d) reference_ts(%lld)",
				index, dpb->reference_ts);
			continue;
		}

		/* 1 for short term reference, 2 for long term reference */
		if (!(dpb->flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM))
			h264_dpb_info[index].reference_flag = 1;
		else
			h264_dpb_info[index].reference_flag = 2;

		vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
		h264_dpb_info[index].field = vb2_v4l2->field;

		h264_dpb_info[index].y_dma_addr =
			vb2_dma_contig_plane_dma_addr(vb, 0);
		if (ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes == 2)
			h264_dpb_info[index].c_dma_addr =
				vb2_dma_contig_plane_dma_addr(vb, 1);
		else
			h264_dpb_info[index].c_dma_addr =
				h264_dpb_info[index].y_dma_addr +
				ctx->picinfo.fb_sz[0];
	}
}

void mtk_vdec_h264_copy_sps_params(struct mtk_h264_sps_param *dst_param,
				   const struct v4l2_ctrl_h264_sps *src_param)
{
	GET_MTK_VDEC_PARAM(chroma_format_idc);
	GET_MTK_VDEC_PARAM(bit_depth_luma_minus8);
	GET_MTK_VDEC_PARAM(bit_depth_chroma_minus8);
	GET_MTK_VDEC_PARAM(log2_max_frame_num_minus4);
	GET_MTK_VDEC_PARAM(pic_order_cnt_type);
	GET_MTK_VDEC_PARAM(log2_max_pic_order_cnt_lsb_minus4);
	GET_MTK_VDEC_PARAM(max_num_ref_frames);
	GET_MTK_VDEC_PARAM(pic_width_in_mbs_minus1);
	GET_MTK_VDEC_PARAM(pic_height_in_map_units_minus1);

	GET_MTK_VDEC_FLAG(separate_colour_plane_flag,
			  V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE);
	GET_MTK_VDEC_FLAG(qpprime_y_zero_transform_bypass_flag,
			  V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS);
	GET_MTK_VDEC_FLAG(delta_pic_order_always_zero_flag,
			  V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO);
	GET_MTK_VDEC_FLAG(frame_mbs_only_flag,
			  V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY);
	GET_MTK_VDEC_FLAG(mb_adaptive_frame_field_flag,
			  V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD);
	GET_MTK_VDEC_FLAG(direct_8x8_inference_flag,
			  V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE);
}

void mtk_vdec_h264_copy_pps_params(struct mtk_h264_pps_param *dst_param,
				   const struct v4l2_ctrl_h264_pps *src_param)
{
	GET_MTK_VDEC_PARAM(num_ref_idx_l0_default_active_minus1);
	GET_MTK_VDEC_PARAM(num_ref_idx_l1_default_active_minus1);
	GET_MTK_VDEC_PARAM(weighted_bipred_idc);
	GET_MTK_VDEC_PARAM(pic_init_qp_minus26);
	GET_MTK_VDEC_PARAM(chroma_qp_index_offset);
	GET_MTK_VDEC_PARAM(second_chroma_qp_index_offset);

	GET_MTK_VDEC_FLAG(entropy_coding_mode_flag,
			  V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE);
	GET_MTK_VDEC_FLAG(pic_order_present_flag,
			  V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT);
	GET_MTK_VDEC_FLAG(weighted_pred_flag,
			  V4L2_H264_PPS_FLAG_WEIGHTED_PRED);
	GET_MTK_VDEC_FLAG(deblocking_filter_control_present_flag,
			  V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT);
	GET_MTK_VDEC_FLAG(constrained_intra_pred_flag,
			  V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED);
	GET_MTK_VDEC_FLAG(redundant_pic_cnt_present_flag,
			  V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT);
	GET_MTK_VDEC_FLAG(transform_8x8_mode_flag,
			  V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE);
	GET_MTK_VDEC_FLAG(scaling_matrix_present_flag,
			  V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT);
}

void mtk_vdec_h264_copy_slice_hd_params(struct mtk_h264_slice_hd_param *dst_param,
					const struct v4l2_ctrl_h264_slice_params *src_param,
					const struct v4l2_ctrl_h264_decode_params *dec_param)
{
	int temp;

	GET_MTK_VDEC_PARAM(first_mb_in_slice);
	GET_MTK_VDEC_PARAM(slice_type);
	GET_MTK_VDEC_PARAM(cabac_init_idc);
	GET_MTK_VDEC_PARAM(slice_qp_delta);
	GET_MTK_VDEC_PARAM(disable_deblocking_filter_idc);
	GET_MTK_VDEC_PARAM(slice_alpha_c0_offset_div2);
	GET_MTK_VDEC_PARAM(slice_beta_offset_div2);
	GET_MTK_VDEC_PARAM(num_ref_idx_l0_active_minus1);
	GET_MTK_VDEC_PARAM(num_ref_idx_l1_active_minus1);

	dst_param->frame_num = dec_param->frame_num;
	dst_param->pic_order_cnt_lsb = dec_param->pic_order_cnt_lsb;

	dst_param->delta_pic_order_cnt_bottom =
		dec_param->delta_pic_order_cnt_bottom;
	dst_param->delta_pic_order_cnt0 =
		dec_param->delta_pic_order_cnt0;
	dst_param->delta_pic_order_cnt1 =
		dec_param->delta_pic_order_cnt1;

	temp = dec_param->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC;
	dst_param->field_pic_flag = temp ? 1 : 0;

	temp = dec_param->flags & V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD;
	dst_param->bottom_field_flag = temp ? 1 : 0;

	GET_MTK_VDEC_FLAG(direct_spatial_mv_pred_flag,
			  V4L2_H264_SLICE_FLAG_DIRECT_SPATIAL_MV_PRED);
}

void mtk_vdec_h264_copy_scaling_matrix(struct slice_api_h264_scaling_matrix *dst_matrix,
				       const struct v4l2_ctrl_h264_scaling_matrix *src_matrix)
{
	memcpy(dst_matrix->scaling_list_4x4, src_matrix->scaling_list_4x4,
	       sizeof(dst_matrix->scaling_list_4x4));

	memcpy(dst_matrix->scaling_list_8x8, src_matrix->scaling_list_8x8,
	       sizeof(dst_matrix->scaling_list_8x8));
}

void
mtk_vdec_h264_copy_decode_params(struct slice_api_h264_decode_param *dst_params,
				 const struct v4l2_ctrl_h264_decode_params *src_params,
				 const struct v4l2_h264_dpb_entry dpb[V4L2_H264_NUM_DPB_ENTRIES])
{
	struct slice_h264_dpb_entry *dst_entry;
	const struct v4l2_h264_dpb_entry *src_entry;
	int i;

	for (i = 0; i < ARRAY_SIZE(dst_params->dpb); i++) {
		dst_entry = &dst_params->dpb[i];
		src_entry = &dpb[i];

		dst_entry->reference_ts = src_entry->reference_ts;
		dst_entry->frame_num = src_entry->frame_num;
		dst_entry->pic_num = src_entry->pic_num;
		dst_entry->top_field_order_cnt = src_entry->top_field_order_cnt;
		dst_entry->bottom_field_order_cnt =
			src_entry->bottom_field_order_cnt;
		dst_entry->flags = src_entry->flags;
	}

	/* num_slices is a leftover from the old H.264 support and is ignored
	 * by the firmware.
	 */
	dst_params->num_slices = 0;
	dst_params->nal_ref_idc = src_params->nal_ref_idc;
	dst_params->top_field_order_cnt = src_params->top_field_order_cnt;
	dst_params->bottom_field_order_cnt = src_params->bottom_field_order_cnt;
	dst_params->flags = src_params->flags;
}

static bool mtk_vdec_h264_dpb_entry_match(const struct v4l2_h264_dpb_entry *a,
					  const struct v4l2_h264_dpb_entry *b)
{
	return a->top_field_order_cnt == b->top_field_order_cnt &&
	       a->bottom_field_order_cnt == b->bottom_field_order_cnt;
}

/*
 * Move DPB entries of dec_param that refer to a frame already existing in dpb
 * into the already existing slot in dpb, and move other entries into new slots.
 *
 * This function is an adaptation of the similarly-named function in
 * hantro_h264.c.
 */
void mtk_vdec_h264_update_dpb(const struct v4l2_ctrl_h264_decode_params *dec_param,
			      struct v4l2_h264_dpb_entry *dpb)
{
	DECLARE_BITMAP(new, ARRAY_SIZE(dec_param->dpb)) = { 0, };
	DECLARE_BITMAP(in_use, ARRAY_SIZE(dec_param->dpb)) = { 0, };
	DECLARE_BITMAP(used, ARRAY_SIZE(dec_param->dpb)) = { 0, };
	unsigned int i, j;

	/* Disable all entries by default, and mark the ones in use. */
	for (i = 0; i < ARRAY_SIZE(dec_param->dpb); i++) {
		if (dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE)
			set_bit(i, in_use);
		dpb[i].flags &= ~V4L2_H264_DPB_ENTRY_FLAG_ACTIVE;
	}

	/* Try to match new DPB entries with existing ones by their POCs. */
	for (i = 0; i < ARRAY_SIZE(dec_param->dpb); i++) {
		const struct v4l2_h264_dpb_entry *ndpb = &dec_param->dpb[i];

		if (!(ndpb->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
			continue;

		/*
		 * To cut off some comparisons, iterate only on target DPB
		 * entries were already used.
		 */
		for_each_set_bit(j, in_use, ARRAY_SIZE(dec_param->dpb)) {
			struct v4l2_h264_dpb_entry *cdpb;

			cdpb = &dpb[j];
			if (!mtk_vdec_h264_dpb_entry_match(cdpb, ndpb))
				continue;

			*cdpb = *ndpb;
			set_bit(j, used);
			/* Don't reiterate on this one. */
			clear_bit(j, in_use);
			break;
		}

		if (j == ARRAY_SIZE(dec_param->dpb))
			set_bit(i, new);
	}

	/* For entries that could not be matched, use remaining free slots. */
	for_each_set_bit(i, new, ARRAY_SIZE(dec_param->dpb)) {
		const struct v4l2_h264_dpb_entry *ndpb = &dec_param->dpb[i];
		struct v4l2_h264_dpb_entry *cdpb;

		/*
		 * Both arrays are of the same sizes, so there is no way
		 * we can end up with no space in target array, unless
		 * something is buggy.
		 */
		j = find_first_zero_bit(used, ARRAY_SIZE(dec_param->dpb));
		if (WARN_ON(j >= ARRAY_SIZE(dec_param->dpb)))
			return;

		cdpb = &dpb[j];
		*cdpb = *ndpb;
		set_bit(j, used);
	}
}

unsigned int mtk_vdec_h264_get_mv_buf_size(unsigned int width, unsigned int height)
{
	int unit_size = (width / MB_UNIT_LEN) * (height / MB_UNIT_LEN) + 8;

	return HW_MB_STORE_SZ * unit_size;
}

int mtk_vdec_h264_find_start_code(unsigned char *data, unsigned int data_sz)
{
	if (data_sz > 3 && data[0] == 0 && data[1] == 0 && data[2] == 1)
		return 3;

	if (data_sz > 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 &&
	    data[3] == 1)
		return 4;

	return -1;
}
