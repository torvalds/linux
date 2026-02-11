// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Video Decoder VDPU383 H264 backend
 *
 * Copyright (C) 2024 Collabora, Ltd.
 *  Detlev Casanova <detlev.casanova@collabora.com>
 */

#include <media/v4l2-h264.h>
#include <media/v4l2-mem2mem.h>

#include <linux/iopoll.h>

#include "rkvdec-rcb.h"
#include "rkvdec-cabac.h"
#include "rkvdec-vdpu383-regs.h"
#include "rkvdec-h264-common.h"

struct rkvdec_sps {
	u16 seq_parameter_set_id:			4;
	u16 profile_idc:				8;
	u16 constraint_set3_flag:			1;
	u16 chroma_format_idc:				2;
	u16 bit_depth_luma:				3;
	u16 bit_depth_chroma:				3;
	u16 qpprime_y_zero_transform_bypass_flag:	1;
	u16 log2_max_frame_num_minus4:			4;
	u16 max_num_ref_frames:				5;
	u16 pic_order_cnt_type:				2;
	u16 log2_max_pic_order_cnt_lsb_minus4:		4;
	u16 delta_pic_order_always_zero_flag:		1;

	u16 pic_width_in_mbs:				16;
	u16 pic_height_in_mbs:				16;

	u16 frame_mbs_only_flag:			1;
	u16 mb_adaptive_frame_field_flag:		1;
	u16 direct_8x8_inference_flag:			1;
	u16 mvc_extension_enable:			1;
	u16 num_views:					2;
	u16 view_id0:                                   10;
	u16 view_id1:                                   10;
} __packed;

struct rkvdec_pps {
	u32 pic_parameter_set_id:				8;
	u32 pps_seq_parameter_set_id:				5;
	u32 entropy_coding_mode_flag:				1;
	u32 bottom_field_pic_order_in_frame_present_flag:	1;
	u32 num_ref_idx_l0_default_active_minus1:		5;
	u32 num_ref_idx_l1_default_active_minus1:		5;
	u32 weighted_pred_flag:					1;
	u32 weighted_bipred_idc:				2;
	u32 pic_init_qp_minus26:				7;
	u32 pic_init_qs_minus26:				6;
	u32 chroma_qp_index_offset:				5;
	u32 deblocking_filter_control_present_flag:		1;
	u32 constrained_intra_pred_flag:			1;
	u32 redundant_pic_cnt_present:				1;
	u32 transform_8x8_mode_flag:				1;
	u32 second_chroma_qp_index_offset:			5;
	u32 scaling_list_enable_flag:				1;
	u32 is_longterm:					16;
	u32 voidx:						16;

	// dpb
	u32 pic_field_flag:                                     1;
	u32 pic_associated_flag:                                1;
	u32 cur_top_field:					32;
	u32 cur_bot_field:					32;

	u32 top_field_order_cnt0:				32;
	u32 bot_field_order_cnt0:				32;
	u32 top_field_order_cnt1:				32;
	u32 bot_field_order_cnt1:				32;
	u32 top_field_order_cnt2:				32;
	u32 bot_field_order_cnt2:				32;
	u32 top_field_order_cnt3:				32;
	u32 bot_field_order_cnt3:				32;
	u32 top_field_order_cnt4:				32;
	u32 bot_field_order_cnt4:				32;
	u32 top_field_order_cnt5:				32;
	u32 bot_field_order_cnt5:				32;
	u32 top_field_order_cnt6:				32;
	u32 bot_field_order_cnt6:				32;
	u32 top_field_order_cnt7:				32;
	u32 bot_field_order_cnt7:				32;
	u32 top_field_order_cnt8:				32;
	u32 bot_field_order_cnt8:				32;
	u32 top_field_order_cnt9:				32;
	u32 bot_field_order_cnt9:				32;
	u32 top_field_order_cnt10:				32;
	u32 bot_field_order_cnt10:				32;
	u32 top_field_order_cnt11:				32;
	u32 bot_field_order_cnt11:				32;
	u32 top_field_order_cnt12:				32;
	u32 bot_field_order_cnt12:				32;
	u32 top_field_order_cnt13:				32;
	u32 bot_field_order_cnt13:				32;
	u32 top_field_order_cnt14:				32;
	u32 bot_field_order_cnt14:				32;
	u32 top_field_order_cnt15:				32;
	u32 bot_field_order_cnt15:				32;

	u32 ref_field_flags:					16;
	u32 ref_topfield_used:					16;
	u32 ref_botfield_used:					16;
	u32 ref_colmv_use_flag:					16;

	u32 reserved0:						30;
	u32 reserved[3];
} __packed;

struct rkvdec_sps_pps {
	struct rkvdec_sps sps;
	struct rkvdec_pps pps;
} __packed;

/* Data structure describing auxiliary buffer format. */
struct rkvdec_h264_priv_tbl {
	s8 cabac_table[4][464][2];
	struct rkvdec_h264_scaling_list scaling_list;
	struct rkvdec_sps_pps param_set[256];
	struct rkvdec_rps rps;
} __packed;

struct rkvdec_h264_ctx {
	struct rkvdec_aux_buf priv_tbl;
	struct rkvdec_h264_reflists reflists;
	struct vdpu383_regs_h26x regs;
};

static void set_field_order_cnt(struct rkvdec_pps *pps, const struct v4l2_h264_dpb_entry *dpb)
{
	pps->top_field_order_cnt0 = dpb[0].top_field_order_cnt;
	pps->bot_field_order_cnt0 = dpb[0].bottom_field_order_cnt;
	pps->top_field_order_cnt1 = dpb[1].top_field_order_cnt;
	pps->bot_field_order_cnt1 = dpb[1].bottom_field_order_cnt;
	pps->top_field_order_cnt2 = dpb[2].top_field_order_cnt;
	pps->bot_field_order_cnt2 = dpb[2].bottom_field_order_cnt;
	pps->top_field_order_cnt3 = dpb[3].top_field_order_cnt;
	pps->bot_field_order_cnt3 = dpb[3].bottom_field_order_cnt;
	pps->top_field_order_cnt4 = dpb[4].top_field_order_cnt;
	pps->bot_field_order_cnt4 = dpb[4].bottom_field_order_cnt;
	pps->top_field_order_cnt5 = dpb[5].top_field_order_cnt;
	pps->bot_field_order_cnt5 = dpb[5].bottom_field_order_cnt;
	pps->top_field_order_cnt6 = dpb[6].top_field_order_cnt;
	pps->bot_field_order_cnt6 = dpb[6].bottom_field_order_cnt;
	pps->top_field_order_cnt7 = dpb[7].top_field_order_cnt;
	pps->bot_field_order_cnt7 = dpb[7].bottom_field_order_cnt;
	pps->top_field_order_cnt8 = dpb[8].top_field_order_cnt;
	pps->bot_field_order_cnt8 = dpb[8].bottom_field_order_cnt;
	pps->top_field_order_cnt9 = dpb[9].top_field_order_cnt;
	pps->bot_field_order_cnt9 = dpb[9].bottom_field_order_cnt;
	pps->top_field_order_cnt10 = dpb[10].top_field_order_cnt;
	pps->bot_field_order_cnt10 = dpb[10].bottom_field_order_cnt;
	pps->top_field_order_cnt11 = dpb[11].top_field_order_cnt;
	pps->bot_field_order_cnt11 = dpb[11].bottom_field_order_cnt;
	pps->top_field_order_cnt12 = dpb[12].top_field_order_cnt;
	pps->bot_field_order_cnt12 = dpb[12].bottom_field_order_cnt;
	pps->top_field_order_cnt13 = dpb[13].top_field_order_cnt;
	pps->bot_field_order_cnt13 = dpb[13].bottom_field_order_cnt;
	pps->top_field_order_cnt14 = dpb[14].top_field_order_cnt;
	pps->bot_field_order_cnt14 = dpb[14].bottom_field_order_cnt;
	pps->top_field_order_cnt15 = dpb[15].top_field_order_cnt;
	pps->bot_field_order_cnt15 = dpb[15].bottom_field_order_cnt;
}

static void assemble_hw_pps(struct rkvdec_ctx *ctx,
			    struct rkvdec_h264_run *run)
{
	struct rkvdec_h264_ctx *h264_ctx = ctx->priv;
	const struct v4l2_ctrl_h264_sps *sps = run->sps;
	const struct v4l2_ctrl_h264_pps *pps = run->pps;
	const struct v4l2_ctrl_h264_decode_params *dec_params = run->decode_params;
	const struct v4l2_h264_dpb_entry *dpb = dec_params->dpb;
	struct rkvdec_h264_priv_tbl *priv_tbl = h264_ctx->priv_tbl.cpu;
	struct rkvdec_sps_pps *hw_ps;
	u32 pic_width, pic_height;
	u32 i;

	/*
	 * HW read the SPS/PPS information from PPS packet index by PPS id.
	 * offset from the base can be calculated by PPS_id * 32 (size per PPS
	 * packet unit). so the driver copy SPS/PPS information to the exact PPS
	 * packet unit for HW accessing.
	 */
	hw_ps = &priv_tbl->param_set[pps->pic_parameter_set_id];
	memset(hw_ps, 0, sizeof(*hw_ps));

	/* write sps */
	hw_ps->sps.seq_parameter_set_id = sps->seq_parameter_set_id;
	hw_ps->sps.profile_idc = sps->profile_idc;
	hw_ps->sps.constraint_set3_flag = !!(sps->constraint_set_flags & (1 << 3));
	hw_ps->sps.chroma_format_idc = sps->chroma_format_idc;
	hw_ps->sps.bit_depth_luma = sps->bit_depth_luma_minus8;
	hw_ps->sps.bit_depth_chroma = sps->bit_depth_chroma_minus8;
	hw_ps->sps.qpprime_y_zero_transform_bypass_flag =
		!!(sps->flags & V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS);
	hw_ps->sps.log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4;
	hw_ps->sps.max_num_ref_frames = sps->max_num_ref_frames;
	hw_ps->sps.pic_order_cnt_type = sps->pic_order_cnt_type;
	hw_ps->sps.log2_max_pic_order_cnt_lsb_minus4 =
		sps->log2_max_pic_order_cnt_lsb_minus4;
	hw_ps->sps.delta_pic_order_always_zero_flag =
		!!(sps->flags & V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO);
	hw_ps->sps.mvc_extension_enable = 0;
	hw_ps->sps.num_views = 0;

	/*
	 * Use the SPS values since they are already in macroblocks
	 * dimensions, height can be field height (halved) if
	 * V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY is not set and also it allows
	 * decoding smaller images into larger allocation which can be used
	 * to implementing SVC spatial layer support.
	 */
	pic_width = 16 * (sps->pic_width_in_mbs_minus1 + 1);
	pic_height = 16 * (sps->pic_height_in_map_units_minus1 + 1);
	if (!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY))
		pic_height *= 2;
	if (!!(dec_params->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC))
		pic_height /= 2;

	hw_ps->sps.pic_width_in_mbs = pic_width;
	hw_ps->sps.pic_height_in_mbs = pic_height;

	hw_ps->sps.frame_mbs_only_flag =
		!!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY);
	hw_ps->sps.mb_adaptive_frame_field_flag =
		!!(sps->flags & V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD);
	hw_ps->sps.direct_8x8_inference_flag =
		!!(sps->flags & V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE);

	/* write pps */
	hw_ps->pps.pic_parameter_set_id = pps->pic_parameter_set_id;
	hw_ps->pps.pps_seq_parameter_set_id = pps->seq_parameter_set_id;
	hw_ps->pps.entropy_coding_mode_flag =
		!!(pps->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE);
	hw_ps->pps.bottom_field_pic_order_in_frame_present_flag =
		!!(pps->flags & V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT);
	hw_ps->pps.num_ref_idx_l0_default_active_minus1 =
		pps->num_ref_idx_l0_default_active_minus1;
	hw_ps->pps.num_ref_idx_l1_default_active_minus1 =
		pps->num_ref_idx_l1_default_active_minus1;
	hw_ps->pps.weighted_pred_flag =
		!!(pps->flags & V4L2_H264_PPS_FLAG_WEIGHTED_PRED);
	hw_ps->pps.weighted_bipred_idc = pps->weighted_bipred_idc;
	hw_ps->pps.pic_init_qp_minus26 = pps->pic_init_qp_minus26;
	hw_ps->pps.pic_init_qs_minus26 = pps->pic_init_qs_minus26;
	hw_ps->pps.chroma_qp_index_offset = pps->chroma_qp_index_offset;
	hw_ps->pps.deblocking_filter_control_present_flag =
		!!(pps->flags & V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT);
	hw_ps->pps.constrained_intra_pred_flag =
		!!(pps->flags & V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED);
	hw_ps->pps.redundant_pic_cnt_present =
		!!(pps->flags & V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT);
	hw_ps->pps.transform_8x8_mode_flag =
		!!(pps->flags & V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE);
	hw_ps->pps.second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset;
	hw_ps->pps.scaling_list_enable_flag =
		!!(pps->flags & V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT);

	set_field_order_cnt(&hw_ps->pps, dpb);

	for (i = 0; i < ARRAY_SIZE(dec_params->dpb); i++) {
		if (dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM)
			hw_ps->pps.is_longterm |= (1 << i);

		hw_ps->pps.ref_field_flags |=
			(!!(dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_FIELD)) << i;
		hw_ps->pps.ref_colmv_use_flag |=
			(!!(dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE)) << i;
		hw_ps->pps.ref_topfield_used |=
			(!!(dpb[i].fields & V4L2_H264_TOP_FIELD_REF)) << i;
		hw_ps->pps.ref_botfield_used |=
			(!!(dpb[i].fields & V4L2_H264_BOTTOM_FIELD_REF)) << i;
	}

	hw_ps->pps.pic_field_flag =
		!!(dec_params->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC);
	hw_ps->pps.pic_associated_flag =
		!!(dec_params->flags & V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD);

	hw_ps->pps.cur_top_field = dec_params->top_field_order_cnt;
	hw_ps->pps.cur_bot_field = dec_params->bottom_field_order_cnt;
}

static void rkvdec_write_regs(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_h264_ctx *h264_ctx = ctx->priv;

	rkvdec_memcpy_toio(rkvdec->regs + VDPU383_OFFSET_COMMON_REGS,
			   &h264_ctx->regs.common,
			   sizeof(h264_ctx->regs.common));
	rkvdec_memcpy_toio(rkvdec->regs + VDPU383_OFFSET_COMMON_ADDR_REGS,
			   &h264_ctx->regs.common_addr,
			   sizeof(h264_ctx->regs.common_addr));
	rkvdec_memcpy_toio(rkvdec->regs + VDPU383_OFFSET_CODEC_PARAMS_REGS,
			   &h264_ctx->regs.h26x_params,
			   sizeof(h264_ctx->regs.h26x_params));
	rkvdec_memcpy_toio(rkvdec->regs + VDPU383_OFFSET_CODEC_ADDR_REGS,
			   &h264_ctx->regs.h26x_addr,
			   sizeof(h264_ctx->regs.h26x_addr));
}

static void config_registers(struct rkvdec_ctx *ctx,
			     struct rkvdec_h264_run *run)
{
	const struct v4l2_ctrl_h264_decode_params *dec_params = run->decode_params;
	struct rkvdec_h264_ctx *h264_ctx = ctx->priv;
	dma_addr_t priv_start_addr = h264_ctx->priv_tbl.dma;
	const struct v4l2_pix_format_mplane *dst_fmt;
	struct vb2_v4l2_buffer *src_buf = run->base.bufs.src;
	struct vb2_v4l2_buffer *dst_buf = run->base.bufs.dst;
	struct vdpu383_regs_h26x *regs = &h264_ctx->regs;
	const struct v4l2_format *f;
	dma_addr_t rlc_addr;
	dma_addr_t dst_addr;
	u32 hor_virstride;
	u32 ver_virstride;
	u32 y_virstride;
	u32 offset;
	u32 pixels;
	u32 i;

	memset(regs, 0, sizeof(*regs));

	/* Set H264 mode */
	regs->common.reg008_dec_mode = VDPU383_MODE_H264;

	/* Set input stream length */
	regs->h26x_params.reg066_stream_len = vb2_get_plane_payload(&src_buf->vb2_buf, 0);

	/* Set strides */
	f = &ctx->decoded_fmt;
	dst_fmt = &f->fmt.pix_mp;
	hor_virstride = dst_fmt->plane_fmt[0].bytesperline;
	ver_virstride = dst_fmt->height;
	y_virstride = hor_virstride * ver_virstride;

	pixels = dst_fmt->height * dst_fmt->width;

	regs->h26x_params.reg068_hor_virstride = hor_virstride / 16;
	regs->h26x_params.reg069_raster_uv_hor_virstride = hor_virstride / 16;
	regs->h26x_params.reg070_y_virstride = y_virstride / 16;

	/* Activate block gating */
	regs->common.reg010_block_gating_en.strmd_auto_gating_e      = 1;
	regs->common.reg010_block_gating_en.inter_auto_gating_e      = 1;
	regs->common.reg010_block_gating_en.intra_auto_gating_e      = 1;
	regs->common.reg010_block_gating_en.transd_auto_gating_e     = 1;
	regs->common.reg010_block_gating_en.recon_auto_gating_e      = 1;
	regs->common.reg010_block_gating_en.filterd_auto_gating_e    = 1;
	regs->common.reg010_block_gating_en.bus_auto_gating_e        = 1;
	regs->common.reg010_block_gating_en.ctrl_auto_gating_e       = 1;
	regs->common.reg010_block_gating_en.rcb_auto_gating_e        = 1;
	regs->common.reg010_block_gating_en.err_prc_auto_gating_e    = 1;

	/* Set timeout threshold */
	if (pixels < RKVDEC_1080P_PIXELS)
		regs->common.reg013_core_timeout_threshold = VDPU383_TIMEOUT_1080p;
	else if (pixels < RKVDEC_4K_PIXELS)
		regs->common.reg013_core_timeout_threshold = VDPU383_TIMEOUT_4K;
	else if (pixels < RKVDEC_8K_PIXELS)
		regs->common.reg013_core_timeout_threshold = VDPU383_TIMEOUT_8K;
	else
		regs->common.reg013_core_timeout_threshold = VDPU383_TIMEOUT_MAX;

	regs->common.reg016_error_ctrl_set.error_proc_disable = 1;

	/* Set ref pic address & poc */
	for (i = 0; i < ARRAY_SIZE(dec_params->dpb); i++) {
		struct vb2_buffer *vb_buf = run->ref_buf[i];
		dma_addr_t buf_dma;

		/*
		 * If a DPB entry is unused or invalid, address of current destination
		 * buffer is returned.
		 */
		if (!vb_buf)
			vb_buf = &dst_buf->vb2_buf;

		buf_dma = vb2_dma_contig_plane_dma_addr(vb_buf, 0);

		/* Set reference addresses */
		regs->h26x_addr.reg170_185_ref_base[i] = buf_dma;
		regs->h26x_addr.reg195_210_payload_st_ref_base[i] = buf_dma;

		/* Set COLMV addresses */
		regs->h26x_addr.reg217_232_colmv_ref_base[i] = buf_dma + ctx->colmv_offset;
	}

	/* Set rlc base address (input stream) */
	rlc_addr = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	regs->common_addr.reg128_strm_base = rlc_addr;

	/* Set output base address */
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	regs->h26x_addr.reg168_decout_base = dst_addr;
	regs->h26x_addr.reg169_error_ref_base = dst_addr;
	regs->h26x_addr.reg192_payload_st_cur_base = dst_addr;

	/* Set colmv address */
	regs->h26x_addr.reg216_colmv_cur_base = dst_addr + ctx->colmv_offset;

	/* Set RCB addresses */
	for (i = 0; i < rkvdec_rcb_buf_count(ctx); i++) {
		regs->common_addr.reg140_162_rcb_info[i].offset = rkvdec_rcb_buf_dma_addr(ctx, i);
		regs->common_addr.reg140_162_rcb_info[i].size = rkvdec_rcb_buf_size(ctx, i);
	}

	/* Set hw pps address */
	offset = offsetof(struct rkvdec_h264_priv_tbl, param_set);
	regs->common_addr.reg131_gbl_base = priv_start_addr + offset;
	regs->h26x_params.reg067_global_len = sizeof(struct rkvdec_sps_pps) / 16;

	/* Set hw rps address */
	offset = offsetof(struct rkvdec_h264_priv_tbl, rps);
	regs->common_addr.reg129_rps_base = priv_start_addr + offset;

	/* Set cabac table */
	offset = offsetof(struct rkvdec_h264_priv_tbl, cabac_table);
	regs->common_addr.reg130_cabactbl_base = priv_start_addr + offset;

	/* Set scaling list address */
	offset = offsetof(struct rkvdec_h264_priv_tbl, scaling_list);
	regs->common_addr.reg132_scanlist_addr = priv_start_addr + offset;

	rkvdec_write_regs(ctx);
}

static int rkvdec_h264_start(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_h264_priv_tbl *priv_tbl;
	struct rkvdec_h264_ctx *h264_ctx;
	struct v4l2_ctrl *ctrl;
	int ret;

	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_H264_SPS);
	if (!ctrl)
		return -EINVAL;

	ret = rkvdec_h264_validate_sps(ctx, ctrl->p_new.p_h264_sps);
	if (ret)
		return ret;

	h264_ctx = kzalloc(sizeof(*h264_ctx), GFP_KERNEL);
	if (!h264_ctx)
		return -ENOMEM;

	priv_tbl = dma_alloc_coherent(rkvdec->dev, sizeof(*priv_tbl),
				      &h264_ctx->priv_tbl.dma, GFP_KERNEL);
	if (!priv_tbl) {
		ret = -ENOMEM;
		goto err_free_ctx;
	}

	h264_ctx->priv_tbl.size = sizeof(*priv_tbl);
	h264_ctx->priv_tbl.cpu = priv_tbl;
	memcpy(priv_tbl->cabac_table, rkvdec_h264_cabac_table,
	       sizeof(rkvdec_h264_cabac_table));

	ctx->priv = h264_ctx;

	return 0;

err_free_ctx:
	kfree(h264_ctx);
	return ret;
}

static void rkvdec_h264_stop(struct rkvdec_ctx *ctx)
{
	struct rkvdec_h264_ctx *h264_ctx = ctx->priv;
	struct rkvdec_dev *rkvdec = ctx->dev;

	dma_free_coherent(rkvdec->dev, h264_ctx->priv_tbl.size,
			  h264_ctx->priv_tbl.cpu, h264_ctx->priv_tbl.dma);
	kfree(h264_ctx);
}

static int rkvdec_h264_run(struct rkvdec_ctx *ctx)
{
	struct v4l2_h264_reflist_builder reflist_builder;
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_h264_ctx *h264_ctx = ctx->priv;
	struct rkvdec_h264_run run;
	struct rkvdec_h264_priv_tbl *tbl = h264_ctx->priv_tbl.cpu;
	u32 timeout_threshold;

	rkvdec_h264_run_preamble(ctx, &run);

	/* Build the P/B{0,1} ref lists. */
	v4l2_h264_init_reflist_builder(&reflist_builder, run.decode_params,
				       run.sps, run.decode_params->dpb);
	v4l2_h264_build_p_ref_list(&reflist_builder, h264_ctx->reflists.p);
	v4l2_h264_build_b_ref_lists(&reflist_builder, h264_ctx->reflists.b0,
				    h264_ctx->reflists.b1);

	assemble_hw_scaling_list(&run, &tbl->scaling_list);
	assemble_hw_pps(ctx, &run);
	lookup_ref_buf_idx(ctx, &run);
	assemble_hw_rps(&reflist_builder, &run, &h264_ctx->reflists, &tbl->rps);

	config_registers(ctx, &run);

	rkvdec_run_postamble(ctx, &run.base);

	timeout_threshold = h264_ctx->regs.common.reg013_core_timeout_threshold;
	rkvdec_schedule_watchdog(rkvdec, timeout_threshold);

	/* Start decoding! */
	writel(timeout_threshold, rkvdec->link + VDPU383_LINK_TIMEOUT_THRESHOLD);
	writel(0, rkvdec->link + VDPU383_LINK_IP_ENABLE);
	writel(VDPU383_DEC_E_BIT, rkvdec->link + VDPU383_LINK_DEC_ENABLE);

	return 0;
}

static int rkvdec_h264_try_ctrl(struct rkvdec_ctx *ctx, struct v4l2_ctrl *ctrl)
{
	if (ctrl->id == V4L2_CID_STATELESS_H264_SPS)
		return rkvdec_h264_validate_sps(ctx, ctrl->p_new.p_h264_sps);

	return 0;
}

const struct rkvdec_coded_fmt_ops rkvdec_vdpu383_h264_fmt_ops = {
	.adjust_fmt = rkvdec_h264_adjust_fmt,
	.get_image_fmt = rkvdec_h264_get_image_fmt,
	.start = rkvdec_h264_start,
	.stop = rkvdec_h264_stop,
	.run = rkvdec_h264_run,
	.try_ctrl = rkvdec_h264_try_ctrl,
};
