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
#include "rkvdec-bitwriter.h"

#define SEQ_PARAMETER_SET_ID				BW_FIELD(0, 4)
#define PROFILE_IDC					BW_FIELD(4, 8)
#define CONSTRAINT_SET3_FLAG				BW_FIELD(12, 1)
#define CHROMA_FORMAT_IDC				BW_FIELD(13, 2)
#define BIT_DEPTH_LUMA					BW_FIELD(15, 3)
#define BIT_DEPTH_CHROMA				BW_FIELD(18, 3)
#define QPPRIME_Y_ZERO_TRANSFORM_BYPASS_FLAG		BW_FIELD(21, 1)
#define LOG2_MAX_FRAME_NUM_MINUS4			BW_FIELD(22, 4)
#define MAX_NUM_REF_FRAMES				BW_FIELD(26, 5)
#define PIC_ORDER_CNT_TYPE				BW_FIELD(31, 2)
#define LOG2_MAX_PIC_ORDER_CNT_LSB_MINUS4		BW_FIELD(33, 4)
#define DELTA_PIC_ORDER_ALWAYS_ZERO_FLAG		BW_FIELD(37, 1)
#define PIC_WIDTH_IN_MBS				BW_FIELD(38, 16)
#define PIC_HEIGHT_IN_MBS				BW_FIELD(54, 16)
#define FRAME_MBS_ONLY_FLAG				BW_FIELD(70, 1)
#define MB_ADAPTIVE_FRAME_FIELD_FLAG			BW_FIELD(71, 1)
#define DIRECT_8X8_INFERENCE_FLAG			BW_FIELD(72, 1)
#define MVC_EXTENSION_ENABLE				BW_FIELD(73, 1)
#define NUM_VIEWS					BW_FIELD(74, 2)
#define VIEW_ID(i)					BW_FIELD(76 + ((i) * 10), 10) // i: 0-1

#define PIC_PARAMETER_SET_ID				BW_FIELD(96, 8)
#define PPS_SEQ_PARAMETER_SET_ID			BW_FIELD(104, 5)
#define ENTROPY_CODING_MODE_FLAG			BW_FIELD(109, 1)
#define BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT_FLAG	BW_FIELD(110, 1)
#define NUM_REF_IDX_L_DEFAULT_ACTIVE_MINUS1(i)		BW_FIELD(111 + ((i) * 5), 5) // i: 0-1
#define WEIGHTED_PRED_FLAG				BW_FIELD(121, 1)
#define WEIGHTED_BIPRED_IDC				BW_FIELD(122, 2)
#define PIC_INIT_QP_MINUS26				BW_FIELD(124, 7)
#define PIC_INIT_QS_MINUS26				BW_FIELD(131, 6)
#define CHROMA_QP_INDEX_OFFSET				BW_FIELD(137, 5)
#define DEBLOCKING_FILTER_CONTROL_PRESENT_FLAG		BW_FIELD(142, 1)
#define CONSTRAINED_INTRA_PRED_FLAG			BW_FIELD(143, 1)
#define REDUNDANT_PIC_CNT_PRESENT			BW_FIELD(144, 1)
#define TRANSFORM_8X8_MODE_FLAG				BW_FIELD(145, 1)
#define SECOND_CHROMA_QP_INDEX_OFFSET			BW_FIELD(146, 5)
#define SCALING_LIST_ENABLE_FLAG			BW_FIELD(151, 1)
#define IS_LONG_TERM(i)					BW_FIELD(152 + (i), 1) // i: 0-15

#define PIC_FIELD_FLAG					BW_FIELD(184, 1)
#define PIC_ASSOCIATED_FLAG				BW_FIELD(185, 1)
#define CUR_TOP_FIELD					BW_FIELD(186, 32)
#define CUR_BOT_FIELD					BW_FIELD(218, 32)

#define TOP_FIELD_ORDER_CNT(i)				BW_FIELD(250 + (i) * 64, 32) // i: 0-15
#define BOT_FIELD_ORDER_CNT(i)				BW_FIELD(282 + (i) * 64, 32) // i: 0-15

#define REF_FIELD_FLAGS(i)				BW_FIELD(1274 + (i), 1) // i: 0-15
#define REF_TOPFIELD_USED(i)				BW_FIELD(1290 + (i), 1) // i: 0-15
#define REF_BOTFIELD_USED(i)				BW_FIELD(1306 + (i), 1) // i: 0-15
#define REF_COLMV_USE_FLAG(i)				BW_FIELD(1322 + (i), 1) // i: 0-15

#define SPS_SIZE					ALIGN(1322 + 16, 128)

struct rkvdec_sps_pps {
	u32 info[SPS_SIZE / 8 / 4];
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
	int i;

	/*
	 * HW read the SPS/PPS information from PPS packet index by PPS id.
	 * offset from the base can be calculated by PPS_id * 32 (size per PPS
	 * packet unit). so the driver copy SPS/PPS information to the exact PPS
	 * packet unit for HW accessing.
	 */
	hw_ps = &priv_tbl->param_set[pps->pic_parameter_set_id];
	memset(hw_ps, 0, sizeof(*hw_ps));

	/* write sps */
	rkvdec_set_bw_field(hw_ps->info, SEQ_PARAMETER_SET_ID, sps->seq_parameter_set_id);
	rkvdec_set_bw_field(hw_ps->info, PROFILE_IDC, sps->profile_idc);
	rkvdec_set_bw_field(hw_ps->info, CONSTRAINT_SET3_FLAG,
			    !!(sps->constraint_set_flags & (1 << 3)));
	rkvdec_set_bw_field(hw_ps->info, CHROMA_FORMAT_IDC, sps->chroma_format_idc);
	rkvdec_set_bw_field(hw_ps->info, BIT_DEPTH_LUMA, sps->bit_depth_luma_minus8);
	rkvdec_set_bw_field(hw_ps->info, BIT_DEPTH_CHROMA, sps->bit_depth_chroma_minus8);
	rkvdec_set_bw_field(hw_ps->info, QPPRIME_Y_ZERO_TRANSFORM_BYPASS_FLAG,
			    !!(sps->flags & V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS));
	rkvdec_set_bw_field(hw_ps->info, LOG2_MAX_FRAME_NUM_MINUS4,
			    sps->log2_max_frame_num_minus4);
	rkvdec_set_bw_field(hw_ps->info, MAX_NUM_REF_FRAMES, sps->max_num_ref_frames);
	rkvdec_set_bw_field(hw_ps->info, PIC_ORDER_CNT_TYPE, sps->pic_order_cnt_type);
	rkvdec_set_bw_field(hw_ps->info, LOG2_MAX_PIC_ORDER_CNT_LSB_MINUS4,
			    sps->log2_max_pic_order_cnt_lsb_minus4);
	rkvdec_set_bw_field(hw_ps->info, DELTA_PIC_ORDER_ALWAYS_ZERO_FLAG,
			    !!(sps->flags & V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO));
	rkvdec_set_bw_field(hw_ps->info, MVC_EXTENSION_ENABLE, 0);
	rkvdec_set_bw_field(hw_ps->info, NUM_VIEWS, 0);

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

	rkvdec_set_bw_field(hw_ps->info, PIC_WIDTH_IN_MBS, pic_width);
	rkvdec_set_bw_field(hw_ps->info, PIC_HEIGHT_IN_MBS, pic_height);

	rkvdec_set_bw_field(hw_ps->info, FRAME_MBS_ONLY_FLAG,
			    !!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY));
	rkvdec_set_bw_field(hw_ps->info, MB_ADAPTIVE_FRAME_FIELD_FLAG,
			    !!(sps->flags & V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD));
	rkvdec_set_bw_field(hw_ps->info, DIRECT_8X8_INFERENCE_FLAG,
			    !!(sps->flags & V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE));

	/* write pps */
	rkvdec_set_bw_field(hw_ps->info, PIC_PARAMETER_SET_ID, pps->pic_parameter_set_id);
	rkvdec_set_bw_field(hw_ps->info, PPS_SEQ_PARAMETER_SET_ID, pps->seq_parameter_set_id);
	rkvdec_set_bw_field(hw_ps->info, ENTROPY_CODING_MODE_FLAG,
			    !!(pps->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE));
	rkvdec_set_bw_field(hw_ps->info, BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT_FLAG,
			    !!(pps->flags &
			       V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT));
	rkvdec_set_bw_field(hw_ps->info, NUM_REF_IDX_L_DEFAULT_ACTIVE_MINUS1(0),
			    pps->num_ref_idx_l0_default_active_minus1);
	rkvdec_set_bw_field(hw_ps->info, NUM_REF_IDX_L_DEFAULT_ACTIVE_MINUS1(1),
			    pps->num_ref_idx_l1_default_active_minus1);
	rkvdec_set_bw_field(hw_ps->info, WEIGHTED_PRED_FLAG,
			    !!(pps->flags & V4L2_H264_PPS_FLAG_WEIGHTED_PRED));
	rkvdec_set_bw_field(hw_ps->info, WEIGHTED_BIPRED_IDC, pps->weighted_bipred_idc);
	rkvdec_set_bw_field(hw_ps->info, PIC_INIT_QP_MINUS26, pps->pic_init_qp_minus26);
	rkvdec_set_bw_field(hw_ps->info, PIC_INIT_QS_MINUS26, pps->pic_init_qs_minus26);
	rkvdec_set_bw_field(hw_ps->info, CHROMA_QP_INDEX_OFFSET, pps->chroma_qp_index_offset);
	rkvdec_set_bw_field(hw_ps->info, DEBLOCKING_FILTER_CONTROL_PRESENT_FLAG,
			    !!(pps->flags & V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT));
	rkvdec_set_bw_field(hw_ps->info, CONSTRAINED_INTRA_PRED_FLAG,
			    !!(pps->flags & V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED));
	rkvdec_set_bw_field(hw_ps->info, REDUNDANT_PIC_CNT_PRESENT,
			    !!(pps->flags & V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT));
	rkvdec_set_bw_field(hw_ps->info, TRANSFORM_8X8_MODE_FLAG,
			    !!(pps->flags & V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE));
	rkvdec_set_bw_field(hw_ps->info, SECOND_CHROMA_QP_INDEX_OFFSET,
			    pps->second_chroma_qp_index_offset);
	rkvdec_set_bw_field(hw_ps->info, SCALING_LIST_ENABLE_FLAG,
			    !!(pps->flags & V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT));

	for (i = 0; i < ARRAY_SIZE(dec_params->dpb); i++) {
		rkvdec_set_bw_field(hw_ps->info, TOP_FIELD_ORDER_CNT(i),
				    dpb[i].top_field_order_cnt);
		rkvdec_set_bw_field(hw_ps->info, BOT_FIELD_ORDER_CNT(i),
				    dpb[i].bottom_field_order_cnt);

		rkvdec_set_bw_field(hw_ps->info, IS_LONG_TERM(i),
				    !!(dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM));
		rkvdec_set_bw_field(hw_ps->info, REF_FIELD_FLAGS(i),
				    !!(dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_FIELD));
		rkvdec_set_bw_field(hw_ps->info, REF_COLMV_USE_FLAG(i),
				    !!(dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE));
		rkvdec_set_bw_field(hw_ps->info, REF_TOPFIELD_USED(i),
				    !!(dpb[i].fields & V4L2_H264_TOP_FIELD_REF));
		rkvdec_set_bw_field(hw_ps->info, REF_BOTFIELD_USED(i),
				    !!(dpb[i].fields & V4L2_H264_BOTTOM_FIELD_REF));
	}

	rkvdec_set_bw_field(hw_ps->info, PIC_FIELD_FLAG,
			    !!(dec_params->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC));
	rkvdec_set_bw_field(hw_ps->info, PIC_ASSOCIATED_FLAG,
			    !!(dec_params->flags & V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD));

	rkvdec_set_bw_field(hw_ps->info, CUR_TOP_FIELD, dec_params->top_field_order_cnt);
	rkvdec_set_bw_field(hw_ps->info, CUR_BOT_FIELD, dec_params->bottom_field_order_cnt);
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

	h264_ctx = kzalloc_obj(*h264_ctx);
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
