// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip VDPU381 Video Decoder H264 backend
 *
 * Copyright (C) 2024 Collabora, Ltd.
 *  Detlev Casanova <detlev.casanova@collabora.com>
 */

#include <media/v4l2-h264.h>
#include <media/v4l2-mem2mem.h>

#include "rkvdec.h"
#include "rkvdec-cabac.h"
#include "rkvdec-rcb.h"
#include "rkvdec-h264-common.h"
#include "rkvdec-vdpu381-regs.h"

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
	u16 pic_width_in_mbs:				12;
	u16 pic_height_in_mbs:				12;
	u16 frame_mbs_only_flag:			1;
	u16 mb_adaptive_frame_field_flag:		1;
	u16 direct_8x8_inference_flag:			1;
	u16 mvc_extension_enable:			1;
	u16 num_views:					2;

	u16 reserved_bits:				12;
	u16 reserved[11];
} __packed;

struct rkvdec_pps {
	u16 pic_parameter_set_id:				8;
	u16 pps_seq_parameter_set_id:				5;
	u16 entropy_coding_mode_flag:				1;
	u16 bottom_field_pic_order_in_frame_present_flag:	1;
	u16 num_ref_idx_l0_default_active_minus1:		5;
	u16 num_ref_idx_l1_default_active_minus1:		5;
	u16 weighted_pred_flag:					1;
	u16 weighted_bipred_idc:				2;
	u16 pic_init_qp_minus26:				7;
	u16 pic_init_qs_minus26:				6;
	u16 chroma_qp_index_offset:				5;
	u16 deblocking_filter_control_present_flag:		1;
	u16 constrained_intra_pred_flag:			1;
	u16 redundant_pic_cnt_present:				1;
	u16 transform_8x8_mode_flag:				1;
	u16 second_chroma_qp_index_offset:			5;
	u16 scaling_list_enable_flag:				1;
	u32 scaling_list_address;
	u16 is_longterm;

	u8 reserved[3];
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
};

struct rkvdec_h264_ctx {
	struct rkvdec_aux_buf priv_tbl;
	struct rkvdec_h264_reflists reflists;
	struct rkvdec_vdpu381_regs_h264 regs;
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
	dma_addr_t scaling_list_address;
	u32 scaling_distance;
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
	hw_ps->sps.mvc_extension_enable = 1;
	hw_ps->sps.num_views = 1;

	/*
	 * Use the SPS values since they are already in macroblocks
	 * dimensions, height can be field height (halved) if
	 * V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY is not set and also it allows
	 * decoding smaller images into larger allocation which can be used
	 * to implementing SVC spatial layer support.
	 */
	hw_ps->sps.pic_width_in_mbs = sps->pic_width_in_mbs_minus1 + 1;
	hw_ps->sps.pic_height_in_mbs = sps->pic_height_in_map_units_minus1 + 1;
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

	/*
	 * To be on the safe side, program the scaling matrix address
	 */
	scaling_distance = offsetof(struct rkvdec_h264_priv_tbl, scaling_list);
	scaling_list_address = h264_ctx->priv_tbl.dma + scaling_distance;
	hw_ps->pps.scaling_list_address = scaling_list_address;

	for (i = 0; i < ARRAY_SIZE(dec_params->dpb); i++) {
		if (dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM)
			hw_ps->pps.is_longterm |= (1 << i);
	}
}

static void rkvdec_write_regs(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_h264_ctx *h264_ctx = ctx->priv;

	rkvdec_memcpy_toio(rkvdec->regs + OFFSET_COMMON_REGS,
			   &h264_ctx->regs.common,
			   sizeof(h264_ctx->regs.common));
	rkvdec_memcpy_toio(rkvdec->regs + OFFSET_CODEC_PARAMS_REGS,
			   &h264_ctx->regs.h264_param,
			   sizeof(h264_ctx->regs.h264_param));
	rkvdec_memcpy_toio(rkvdec->regs + OFFSET_COMMON_ADDR_REGS,
			   &h264_ctx->regs.common_addr,
			   sizeof(h264_ctx->regs.common_addr));
	rkvdec_memcpy_toio(rkvdec->regs + OFFSET_CODEC_ADDR_REGS,
			   &h264_ctx->regs.h264_addr,
			   sizeof(h264_ctx->regs.h264_addr));
	rkvdec_memcpy_toio(rkvdec->regs + OFFSET_POC_HIGHBIT_REGS,
			   &h264_ctx->regs.h264_highpoc,
			   sizeof(h264_ctx->regs.h264_highpoc));
}

static void config_registers(struct rkvdec_ctx *ctx,
			     struct rkvdec_h264_run *run)
{
	const struct v4l2_ctrl_h264_decode_params *dec_params = run->decode_params;
	const struct v4l2_h264_dpb_entry *dpb = dec_params->dpb;
	struct rkvdec_h264_ctx *h264_ctx = ctx->priv;
	dma_addr_t priv_start_addr = h264_ctx->priv_tbl.dma;
	const struct v4l2_pix_format_mplane *dst_fmt;
	struct vb2_v4l2_buffer *src_buf = run->base.bufs.src;
	struct vb2_v4l2_buffer *dst_buf = run->base.bufs.dst;
	struct rkvdec_vdpu381_regs_h264 *regs = &h264_ctx->regs;
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
	regs->common.reg009_dec_mode.dec_mode = VDPU381_MODE_H264;

	/* Set config */
	regs->common.reg011_important_en.buf_empty_en = 1;
	regs->common.reg011_important_en.dec_clkgate_e = 1;
	regs->common.reg011_important_en.dec_timeout_e = 1;
	regs->common.reg011_important_en.pix_range_det_e = 1;

	/*
	 * Even though the scan list address can be set in RPS,
	 * with some frames, it will try to use the address set in the register.
	 */
	regs->common.reg012_secondary_en.scanlist_addr_valid_en = 1;

	/* Set IDR flag */
	regs->common.reg013_en_mode_set.cur_pic_is_idr =
		!!(dec_params->flags & V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC);

	/* Set input stream length */
	regs->common.reg016_stream_len = vb2_get_plane_payload(&src_buf->vb2_buf, 0);

	/* Set max slice number */
	regs->common.reg017_slice_number.slice_num = MAX_SLICE_NUMBER;

	/* Set strides */
	f = &ctx->decoded_fmt;
	dst_fmt = &f->fmt.pix_mp;
	hor_virstride = dst_fmt->plane_fmt[0].bytesperline;
	ver_virstride = dst_fmt->height;
	y_virstride = hor_virstride * ver_virstride;

	regs->common.reg018_y_hor_stride.y_hor_virstride = hor_virstride / 16;
	regs->common.reg019_uv_hor_stride.uv_hor_virstride = hor_virstride / 16;
	regs->common.reg020_y_stride.y_virstride = y_virstride / 16;

	/* Activate block gating */
	regs->common.reg026_block_gating_en.inter_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.filterd_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.strmd_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.mcp_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.busifd_auto_gating_e = 0;
	regs->common.reg026_block_gating_en.dec_ctrl_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.intra_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.mc_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.transd_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.sram_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.cru_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.reg_cfg_gating_en = 1;

	/* Set timeout threshold */
	pixels = dst_fmt->height * dst_fmt->width;
	if (pixels < RKVDEC_1080P_PIXELS)
		regs->common.reg032_timeout_threshold = RKVDEC_TIMEOUT_1080p;
	else if (pixels < RKVDEC_4K_PIXELS)
		regs->common.reg032_timeout_threshold = RKVDEC_TIMEOUT_4K;
	else if (pixels < RKVDEC_8K_PIXELS)
		regs->common.reg032_timeout_threshold = RKVDEC_TIMEOUT_8K;
	else
		regs->common.reg032_timeout_threshold = RKVDEC_TIMEOUT_MAX;

	/* Set TOP and BOTTOM POCs */
	regs->h264_param.reg065_cur_top_poc = dec_params->top_field_order_cnt;
	regs->h264_param.reg066_cur_bot_poc = dec_params->bottom_field_order_cnt;

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
		regs->h264_addr.reg164_180_ref_base[i] = buf_dma;

		/* Set COLMV addresses */
		regs->h264_addr.reg182_198_colmv_base[i] = buf_dma + ctx->colmv_offset;

		struct rkvdec_vdpu381_h264_ref_info *ref_info =
			&regs->h264_param.reg099_102_ref_info_regs[i / 4].ref_info[i % 4];

		ref_info->ref_field =
			!!(dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_FIELD);
		ref_info->ref_colmv_use_flag =
			!!(dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE);
		ref_info->ref_topfield_used =
			!!(dpb[i].fields & V4L2_H264_TOP_FIELD_REF);
		ref_info->ref_botfield_used =
			!!(dpb[i].fields & V4L2_H264_BOTTOM_FIELD_REF);

		regs->h264_param.reg067_098_ref_poc[i * 2] =
			dpb[i].top_field_order_cnt;
		regs->h264_param.reg067_098_ref_poc[i * 2 + 1] =
			dpb[i].bottom_field_order_cnt;
	}

	/* Set rlc base address (input stream) */
	rlc_addr = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	regs->common_addr.rlc_base = rlc_addr;
	regs->common_addr.rlcwrite_base = rlc_addr;

	/* Set output base address */
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	regs->common_addr.decout_base = dst_addr;
	regs->common_addr.error_ref_base = dst_addr;

	/* Set colmv address */
	regs->common_addr.colmv_cur_base = dst_addr + ctx->colmv_offset;

	/* Set RCB addresses */
	for (i = 0; i < rkvdec_rcb_buf_count(ctx); i++)
		regs->common_addr.rcb_base[i] = rkvdec_rcb_buf_dma_addr(ctx, i);

	/* Set hw pps address */
	offset = offsetof(struct rkvdec_h264_priv_tbl, param_set);
	regs->h264_addr.reg161_pps_base = priv_start_addr + offset;

	/* Set hw rps address */
	offset = offsetof(struct rkvdec_h264_priv_tbl, rps);
	regs->h264_addr.reg163_rps_base = priv_start_addr + offset;

	/* Set cabac table */
	offset = offsetof(struct rkvdec_h264_priv_tbl, cabac_table);
	regs->h264_addr.reg199_cabactbl_base = priv_start_addr + offset;

	offset = offsetof(struct rkvdec_h264_priv_tbl, scaling_list);
	regs->h264_addr.reg181_scanlist_addr = priv_start_addr + offset;

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
	struct rkvdec_h264_priv_tbl *tbl = h264_ctx->priv_tbl.cpu;
	struct rkvdec_h264_run run;

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

	rkvdec_schedule_watchdog(rkvdec, h264_ctx->regs.common.reg032_timeout_threshold);

	/* Start decoding! */
	writel(VDPU381_DEC_E_BIT, rkvdec->regs + VDPU381_REG_DEC_E);

	return 0;
}

static int rkvdec_h264_try_ctrl(struct rkvdec_ctx *ctx, struct v4l2_ctrl *ctrl)
{
	if (ctrl->id == V4L2_CID_STATELESS_H264_SPS)
		return rkvdec_h264_validate_sps(ctx, ctrl->p_new.p_h264_sps);

	return 0;
}

const struct rkvdec_coded_fmt_ops rkvdec_vdpu381_h264_fmt_ops = {
	.adjust_fmt = rkvdec_h264_adjust_fmt,
	.get_image_fmt = rkvdec_h264_get_image_fmt,
	.start = rkvdec_h264_start,
	.stop = rkvdec_h264_stop,
	.run = rkvdec_h264_run,
	.try_ctrl = rkvdec_h264_try_ctrl,
};
