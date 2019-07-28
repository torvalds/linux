// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

#include <asm/unaligned.h>
#include <linux/bitfield.h>
#include <media/v4l2-mem2mem.h>
#include "hantro.h"
#include "hantro_hw.h"

#define G1_SWREG(nr)			((nr) * 4)

#define G1_REG_RLC_VLC_BASE		G1_SWREG(12)
#define G1_REG_DEC_OUT_BASE		G1_SWREG(13)
#define G1_REG_REFER0_BASE		G1_SWREG(14)
#define G1_REG_REFER1_BASE		G1_SWREG(15)
#define G1_REG_REFER2_BASE		G1_SWREG(16)
#define G1_REG_REFER3_BASE		G1_SWREG(17)
#define G1_REG_QTABLE_BASE		G1_SWREG(40)
#define G1_REG_DEC_E(v)			((v) ? BIT(0) : 0)

#define G1_REG_DEC_AXI_RD_ID(v)		(((v) << 24) & GENMASK(31, 24))
#define G1_REG_DEC_TIMEOUT_E(v)		((v) ? BIT(23) : 0)
#define G1_REG_DEC_STRSWAP32_E(v)	((v) ? BIT(22) : 0)
#define G1_REG_DEC_STRENDIAN_E(v)	((v) ? BIT(21) : 0)
#define G1_REG_DEC_INSWAP32_E(v)	((v) ? BIT(20) : 0)
#define G1_REG_DEC_OUTSWAP32_E(v)	((v) ? BIT(19) : 0)
#define G1_REG_DEC_DATA_DISC_E(v)	((v) ? BIT(18) : 0)
#define G1_REG_DEC_LATENCY(v)		(((v) << 11) & GENMASK(16, 11))
#define G1_REG_DEC_CLK_GATE_E(v)	((v) ? BIT(10) : 0)
#define G1_REG_DEC_IN_ENDIAN(v)		((v) ? BIT(9) : 0)
#define G1_REG_DEC_OUT_ENDIAN(v)	((v) ? BIT(8) : 0)
#define G1_REG_DEC_ADV_PRE_DIS(v)	((v) ? BIT(6) : 0)
#define G1_REG_DEC_SCMD_DIS(v)		((v) ? BIT(5) : 0)
#define G1_REG_DEC_MAX_BURST(v)		(((v) << 0) & GENMASK(4, 0))

#define G1_REG_DEC_MODE(v)		(((v) << 28) & GENMASK(31, 28))
#define G1_REG_RLC_MODE_E(v)		((v) ? BIT(27) : 0)
#define G1_REG_PIC_INTERLACE_E(v)	((v) ? BIT(23) : 0)
#define G1_REG_PIC_FIELDMODE_E(v)	((v) ? BIT(22) : 0)
#define G1_REG_PIC_B_E(v)		((v) ? BIT(21) : 0)
#define G1_REG_PIC_INTER_E(v)		((v) ? BIT(20) : 0)
#define G1_REG_PIC_TOPFIELD_E(v)	((v) ? BIT(19) : 0)
#define G1_REG_FWD_INTERLACE_E(v)	((v) ? BIT(18) : 0)
#define G1_REG_FILTERING_DIS(v)		((v) ? BIT(14) : 0)
#define G1_REG_WRITE_MVS_E(v)		((v) ? BIT(12) : 0)
#define G1_REG_DEC_AXI_WR_ID(v)		(((v) << 0) & GENMASK(7, 0))

#define G1_REG_PIC_MB_WIDTH(v)		(((v) << 23) & GENMASK(31, 23))
#define G1_REG_PIC_MB_HEIGHT_P(v)	(((v) << 11) & GENMASK(18, 11))
#define G1_REG_ALT_SCAN_E(v)		((v) ? BIT(6) : 0)
#define G1_REG_TOPFIELDFIRST_E(v)	((v) ? BIT(5) : 0)

#define G1_REG_STRM_START_BIT(v)	(((v) << 26) & GENMASK(31, 26))
#define G1_REG_QSCALE_TYPE(v)		((v) ? BIT(24) : 0)
#define G1_REG_CON_MV_E(v)		((v) ? BIT(4) : 0)
#define G1_REG_INTRA_DC_PREC(v)		(((v) << 2) & GENMASK(3, 2))
#define G1_REG_INTRA_VLC_TAB(v)		((v) ? BIT(1) : 0)
#define G1_REG_FRAME_PRED_DCT(v)	((v) ? BIT(0) : 0)

#define G1_REG_INIT_QP(v)		(((v) << 25) & GENMASK(30, 25))
#define G1_REG_STREAM_LEN(v)		(((v) << 0) & GENMASK(23, 0))

#define G1_REG_ALT_SCAN_FLAG_E(v)	((v) ? BIT(19) : 0)
#define G1_REG_FCODE_FWD_HOR(v)		(((v) << 15) & GENMASK(18, 15))
#define G1_REG_FCODE_FWD_VER(v)		(((v) << 11) & GENMASK(14, 11))
#define G1_REG_FCODE_BWD_HOR(v)		(((v) << 7) & GENMASK(10, 7))
#define G1_REG_FCODE_BWD_VER(v)		(((v) << 3) & GENMASK(6, 3))
#define G1_REG_MV_ACCURACY_FWD(v)	((v) ? BIT(2) : 0)
#define G1_REG_MV_ACCURACY_BWD(v)	((v) ? BIT(1) : 0)

#define G1_REG_STARTMB_X(v)		(((v) << 23) & GENMASK(31, 23))
#define G1_REG_STARTMB_Y(v)		(((v) << 15) & GENMASK(22, 15))

#define G1_REG_APF_THRESHOLD(v)		(((v) << 0) & GENMASK(13, 0))

#define PICT_TOP_FIELD     1
#define PICT_BOTTOM_FIELD  2
#define PICT_FRAME         3

static void
hantro_g1_mpeg2_dec_set_quantization(struct hantro_dev *vpu,
				     struct hantro_ctx *ctx)
{
	struct v4l2_ctrl_mpeg2_quantization *quantization;

	quantization = hantro_get_ctrl(ctx,
				       V4L2_CID_MPEG_VIDEO_MPEG2_QUANTIZATION);
	hantro_mpeg2_dec_copy_qtable(ctx->mpeg2_dec.qtable.cpu,
				     quantization);
	vdpu_write_relaxed(vpu, ctx->mpeg2_dec.qtable.dma,
			   G1_REG_QTABLE_BASE);
}

static void
hantro_g1_mpeg2_dec_set_buffers(struct hantro_dev *vpu, struct hantro_ctx *ctx,
				struct vb2_buffer *src_buf,
				struct vb2_buffer *dst_buf,
				const struct v4l2_mpeg2_sequence *sequence,
				const struct v4l2_mpeg2_picture *picture,
				const struct v4l2_ctrl_mpeg2_slice_params *slice_params)
{
	dma_addr_t forward_addr = 0, backward_addr = 0;
	dma_addr_t current_addr, addr;
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx);

	switch (picture->picture_coding_type) {
	case V4L2_MPEG2_PICTURE_CODING_TYPE_B:
		backward_addr = hantro_get_ref(vq,
					       slice_params->backward_ref_ts);
		/* fall-through */
	case V4L2_MPEG2_PICTURE_CODING_TYPE_P:
		forward_addr = hantro_get_ref(vq,
					      slice_params->forward_ref_ts);
	}

	/* Source bitstream buffer */
	addr = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	vdpu_write_relaxed(vpu, addr, G1_REG_RLC_VLC_BASE);

	/* Destination frame buffer */
	addr = vb2_dma_contig_plane_dma_addr(dst_buf, 0);
	current_addr = addr;

	if (picture->picture_structure == PICT_BOTTOM_FIELD)
		addr += ALIGN(ctx->dst_fmt.width, 16);
	vdpu_write_relaxed(vpu, addr, G1_REG_DEC_OUT_BASE);

	if (!forward_addr)
		forward_addr = current_addr;
	if (!backward_addr)
		backward_addr = current_addr;

	/* Set forward ref frame (top/bottom field) */
	if (picture->picture_structure == PICT_FRAME ||
	    picture->picture_coding_type == V4L2_MPEG2_PICTURE_CODING_TYPE_B ||
	    (picture->picture_structure == PICT_TOP_FIELD &&
	     picture->top_field_first) ||
	    (picture->picture_structure == PICT_BOTTOM_FIELD &&
	     !picture->top_field_first)) {
		vdpu_write_relaxed(vpu, forward_addr, G1_REG_REFER0_BASE);
		vdpu_write_relaxed(vpu, forward_addr, G1_REG_REFER1_BASE);
	} else if (picture->picture_structure == PICT_TOP_FIELD) {
		vdpu_write_relaxed(vpu, forward_addr, G1_REG_REFER0_BASE);
		vdpu_write_relaxed(vpu, current_addr, G1_REG_REFER1_BASE);
	} else if (picture->picture_structure == PICT_BOTTOM_FIELD) {
		vdpu_write_relaxed(vpu, current_addr, G1_REG_REFER0_BASE);
		vdpu_write_relaxed(vpu, forward_addr, G1_REG_REFER1_BASE);
	}

	/* Set backward ref frame (top/bottom field) */
	vdpu_write_relaxed(vpu, backward_addr, G1_REG_REFER2_BASE);
	vdpu_write_relaxed(vpu, backward_addr, G1_REG_REFER3_BASE);
}

void hantro_g1_mpeg2_dec_run(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	const struct v4l2_ctrl_mpeg2_slice_params *slice_params;
	const struct v4l2_mpeg2_sequence *sequence;
	const struct v4l2_mpeg2_picture *picture;
	u32 reg;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* Apply request controls if any */
	v4l2_ctrl_request_setup(src_buf->vb2_buf.req_obj.req,
				&ctx->ctrl_handler);

	slice_params = hantro_get_ctrl(ctx,
				       V4L2_CID_MPEG_VIDEO_MPEG2_SLICE_PARAMS);
	sequence = &slice_params->sequence;
	picture = &slice_params->picture;

	reg = G1_REG_DEC_AXI_RD_ID(0) |
	      G1_REG_DEC_TIMEOUT_E(1) |
	      G1_REG_DEC_STRSWAP32_E(1) |
	      G1_REG_DEC_STRENDIAN_E(1) |
	      G1_REG_DEC_INSWAP32_E(1) |
	      G1_REG_DEC_OUTSWAP32_E(1) |
	      G1_REG_DEC_DATA_DISC_E(0) |
	      G1_REG_DEC_LATENCY(0) |
	      G1_REG_DEC_CLK_GATE_E(1) |
	      G1_REG_DEC_IN_ENDIAN(1) |
	      G1_REG_DEC_OUT_ENDIAN(1) |
	      G1_REG_DEC_ADV_PRE_DIS(0) |
	      G1_REG_DEC_SCMD_DIS(0) |
	      G1_REG_DEC_MAX_BURST(16);
	vdpu_write_relaxed(vpu, reg, G1_SWREG(2));

	reg = G1_REG_DEC_MODE(5) |
	      G1_REG_RLC_MODE_E(0) |
	      G1_REG_PIC_INTERLACE_E(!sequence->progressive_sequence) |
	      G1_REG_PIC_FIELDMODE_E(picture->picture_structure != PICT_FRAME) |
	      G1_REG_PIC_B_E(picture->picture_coding_type == V4L2_MPEG2_PICTURE_CODING_TYPE_B) |
	      G1_REG_PIC_INTER_E(picture->picture_coding_type != V4L2_MPEG2_PICTURE_CODING_TYPE_I) |
	      G1_REG_PIC_TOPFIELD_E(picture->picture_structure == PICT_TOP_FIELD) |
	      G1_REG_FWD_INTERLACE_E(0) |
	      G1_REG_FILTERING_DIS(1) |
	      G1_REG_WRITE_MVS_E(0) |
	      G1_REG_DEC_AXI_WR_ID(0);
	vdpu_write_relaxed(vpu, reg, G1_SWREG(3));

	reg = G1_REG_PIC_MB_WIDTH(MPEG2_MB_WIDTH(ctx->dst_fmt.width)) |
	      G1_REG_PIC_MB_HEIGHT_P(MPEG2_MB_HEIGHT(ctx->dst_fmt.height)) |
	      G1_REG_ALT_SCAN_E(picture->alternate_scan) |
	      G1_REG_TOPFIELDFIRST_E(picture->top_field_first);
	vdpu_write_relaxed(vpu, reg, G1_SWREG(4));

	reg = G1_REG_STRM_START_BIT(slice_params->data_bit_offset) |
	      G1_REG_QSCALE_TYPE(picture->q_scale_type) |
	      G1_REG_CON_MV_E(picture->concealment_motion_vectors) |
	      G1_REG_INTRA_DC_PREC(picture->intra_dc_precision) |
	      G1_REG_INTRA_VLC_TAB(picture->intra_vlc_format) |
	      G1_REG_FRAME_PRED_DCT(picture->frame_pred_frame_dct);
	vdpu_write_relaxed(vpu, reg, G1_SWREG(5));

	reg = G1_REG_INIT_QP(1) |
	      G1_REG_STREAM_LEN(slice_params->bit_size >> 3);
	vdpu_write_relaxed(vpu, reg, G1_SWREG(6));

	reg = G1_REG_ALT_SCAN_FLAG_E(picture->alternate_scan) |
	      G1_REG_FCODE_FWD_HOR(picture->f_code[0][0]) |
	      G1_REG_FCODE_FWD_VER(picture->f_code[0][1]) |
	      G1_REG_FCODE_BWD_HOR(picture->f_code[1][0]) |
	      G1_REG_FCODE_BWD_VER(picture->f_code[1][1]) |
	      G1_REG_MV_ACCURACY_FWD(1) |
	      G1_REG_MV_ACCURACY_BWD(1);
	vdpu_write_relaxed(vpu, reg, G1_SWREG(18));

	reg = G1_REG_STARTMB_X(0) |
	      G1_REG_STARTMB_Y(0);
	vdpu_write_relaxed(vpu, reg, G1_SWREG(48));

	reg = G1_REG_APF_THRESHOLD(8);
	vdpu_write_relaxed(vpu, reg, G1_SWREG(55));

	hantro_g1_mpeg2_dec_set_quantization(vpu, ctx);

	hantro_g1_mpeg2_dec_set_buffers(vpu, ctx, &src_buf->vb2_buf,
					&dst_buf->vb2_buf,
					sequence, picture, slice_params);

	/* Controls no longer in-use, we can complete them */
	v4l2_ctrl_request_complete(src_buf->vb2_buf.req_obj.req,
				   &ctx->ctrl_handler);

	/* Kick the watchdog and start decoding */
	schedule_delayed_work(&vpu->watchdog_work, msecs_to_jiffies(2000));

	reg = G1_REG_DEC_E(1);
	vdpu_write(vpu, reg, G1_SWREG(1));
}
