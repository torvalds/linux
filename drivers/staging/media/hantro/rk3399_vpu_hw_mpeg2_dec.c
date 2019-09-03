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

#define VDPU_SWREG(nr)			((nr) * 4)

#define VDPU_REG_DEC_OUT_BASE		VDPU_SWREG(63)
#define VDPU_REG_RLC_VLC_BASE		VDPU_SWREG(64)
#define VDPU_REG_QTABLE_BASE		VDPU_SWREG(61)
#define VDPU_REG_REFER0_BASE		VDPU_SWREG(131)
#define VDPU_REG_REFER2_BASE		VDPU_SWREG(134)
#define VDPU_REG_REFER3_BASE		VDPU_SWREG(135)
#define VDPU_REG_REFER1_BASE		VDPU_SWREG(148)
#define VDPU_REG_DEC_E(v)		((v) ? BIT(0) : 0)

#define VDPU_REG_DEC_ADV_PRE_DIS(v)	((v) ? BIT(11) : 0)
#define VDPU_REG_DEC_SCMD_DIS(v)	((v) ? BIT(10) : 0)
#define VDPU_REG_FILTERING_DIS(v)	((v) ? BIT(8) : 0)
#define VDPU_REG_DEC_LATENCY(v)		(((v) << 1) & GENMASK(6, 1))

#define VDPU_REG_INIT_QP(v)		(((v) << 25) & GENMASK(30, 25))
#define VDPU_REG_STREAM_LEN(v)		(((v) << 0) & GENMASK(23, 0))

#define VDPU_REG_APF_THRESHOLD(v)	(((v) << 17) & GENMASK(30, 17))
#define VDPU_REG_STARTMB_X(v)		(((v) << 8) & GENMASK(16, 8))
#define VDPU_REG_STARTMB_Y(v)		(((v) << 0) & GENMASK(7, 0))

#define VDPU_REG_DEC_MODE(v)		(((v) << 0) & GENMASK(3, 0))

#define VDPU_REG_DEC_STRENDIAN_E(v)	((v) ? BIT(5) : 0)
#define VDPU_REG_DEC_STRSWAP32_E(v)	((v) ? BIT(4) : 0)
#define VDPU_REG_DEC_OUTSWAP32_E(v)	((v) ? BIT(3) : 0)
#define VDPU_REG_DEC_INSWAP32_E(v)	((v) ? BIT(2) : 0)
#define VDPU_REG_DEC_OUT_ENDIAN(v)	((v) ? BIT(1) : 0)
#define VDPU_REG_DEC_IN_ENDIAN(v)	((v) ? BIT(0) : 0)

#define VDPU_REG_DEC_DATA_DISC_E(v)	((v) ? BIT(22) : 0)
#define VDPU_REG_DEC_MAX_BURST(v)	(((v) << 16) & GENMASK(20, 16))
#define VDPU_REG_DEC_AXI_WR_ID(v)	(((v) << 8) & GENMASK(15, 8))
#define VDPU_REG_DEC_AXI_RD_ID(v)	(((v) << 0) & GENMASK(7, 0))

#define VDPU_REG_RLC_MODE_E(v)		((v) ? BIT(20) : 0)
#define VDPU_REG_PIC_INTERLACE_E(v)	((v) ? BIT(17) : 0)
#define VDPU_REG_PIC_FIELDMODE_E(v)	((v) ? BIT(16) : 0)
#define VDPU_REG_PIC_B_E(v)		((v) ? BIT(15) : 0)
#define VDPU_REG_PIC_INTER_E(v)		((v) ? BIT(14) : 0)
#define VDPU_REG_PIC_TOPFIELD_E(v)	((v) ? BIT(13) : 0)
#define VDPU_REG_FWD_INTERLACE_E(v)	((v) ? BIT(12) : 0)
#define VDPU_REG_WRITE_MVS_E(v)		((v) ? BIT(10) : 0)
#define VDPU_REG_DEC_TIMEOUT_E(v)	((v) ? BIT(5) : 0)
#define VDPU_REG_DEC_CLK_GATE_E(v)	((v) ? BIT(4) : 0)

#define VDPU_REG_PIC_MB_WIDTH(v)	(((v) << 23) & GENMASK(31, 23))
#define VDPU_REG_PIC_MB_HEIGHT_P(v)	(((v) << 11) & GENMASK(18, 11))
#define VDPU_REG_ALT_SCAN_E(v)		((v) ? BIT(6) : 0)
#define VDPU_REG_TOPFIELDFIRST_E(v)	((v) ? BIT(5) : 0)

#define VDPU_REG_STRM_START_BIT(v)	(((v) << 26) & GENMASK(31, 26))
#define VDPU_REG_QSCALE_TYPE(v)		((v) ? BIT(24) : 0)
#define VDPU_REG_CON_MV_E(v)		((v) ? BIT(4) : 0)
#define VDPU_REG_INTRA_DC_PREC(v)	(((v) << 2) & GENMASK(3, 2))
#define VDPU_REG_INTRA_VLC_TAB(v)	((v) ? BIT(1) : 0)
#define VDPU_REG_FRAME_PRED_DCT(v)	((v) ? BIT(0) : 0)

#define VDPU_REG_ALT_SCAN_FLAG_E(v)	((v) ? BIT(19) : 0)
#define VDPU_REG_FCODE_FWD_HOR(v)	(((v) << 15) & GENMASK(18, 15))
#define VDPU_REG_FCODE_FWD_VER(v)	(((v) << 11) & GENMASK(14, 11))
#define VDPU_REG_FCODE_BWD_HOR(v)	(((v) << 7) & GENMASK(10, 7))
#define VDPU_REG_FCODE_BWD_VER(v)	(((v) << 3) & GENMASK(6, 3))
#define VDPU_REG_MV_ACCURACY_FWD(v)	((v) ? BIT(2) : 0)
#define VDPU_REG_MV_ACCURACY_BWD(v)	((v) ? BIT(1) : 0)

#define PICT_TOP_FIELD     1
#define PICT_BOTTOM_FIELD  2
#define PICT_FRAME         3

static void
rk3399_vpu_mpeg2_dec_set_quantization(struct hantro_dev *vpu,
				      struct hantro_ctx *ctx)
{
	struct v4l2_ctrl_mpeg2_quantization *quantization;

	quantization = hantro_get_ctrl(ctx,
				       V4L2_CID_MPEG_VIDEO_MPEG2_QUANTIZATION);
	hantro_mpeg2_dec_copy_qtable(ctx->mpeg2_dec.qtable.cpu, quantization);
	vdpu_write_relaxed(vpu, ctx->mpeg2_dec.qtable.dma,
			   VDPU_REG_QTABLE_BASE);
}

static void
rk3399_vpu_mpeg2_dec_set_buffers(struct hantro_dev *vpu,
				 struct hantro_ctx *ctx,
				 struct vb2_buffer *src_buf,
				 struct vb2_buffer *dst_buf,
				 const struct v4l2_mpeg2_sequence *sequence,
				 const struct v4l2_mpeg2_picture *picture,
				 const struct v4l2_ctrl_mpeg2_slice_params *slice_params)
{
	dma_addr_t forward_addr = 0, backward_addr = 0;
	dma_addr_t current_addr, addr;

	switch (picture->picture_coding_type) {
	case V4L2_MPEG2_PICTURE_CODING_TYPE_B:
		backward_addr = hantro_get_ref(ctx,
					       slice_params->backward_ref_ts);
		/* fall-through */
	case V4L2_MPEG2_PICTURE_CODING_TYPE_P:
		forward_addr = hantro_get_ref(ctx,
					      slice_params->forward_ref_ts);
	}

	/* Source bitstream buffer */
	addr = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	vdpu_write_relaxed(vpu, addr, VDPU_REG_RLC_VLC_BASE);

	/* Destination frame buffer */
	addr = vb2_dma_contig_plane_dma_addr(dst_buf, 0);
	current_addr = addr;

	if (picture->picture_structure == PICT_BOTTOM_FIELD)
		addr += ALIGN(ctx->dst_fmt.width, 16);
	vdpu_write_relaxed(vpu, addr, VDPU_REG_DEC_OUT_BASE);

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
		vdpu_write_relaxed(vpu, forward_addr, VDPU_REG_REFER0_BASE);
		vdpu_write_relaxed(vpu, forward_addr, VDPU_REG_REFER1_BASE);
	} else if (picture->picture_structure == PICT_TOP_FIELD) {
		vdpu_write_relaxed(vpu, forward_addr, VDPU_REG_REFER0_BASE);
		vdpu_write_relaxed(vpu, current_addr, VDPU_REG_REFER1_BASE);
	} else if (picture->picture_structure == PICT_BOTTOM_FIELD) {
		vdpu_write_relaxed(vpu, current_addr, VDPU_REG_REFER0_BASE);
		vdpu_write_relaxed(vpu, forward_addr, VDPU_REG_REFER1_BASE);
	}

	/* Set backward ref frame (top/bottom field) */
	vdpu_write_relaxed(vpu, backward_addr, VDPU_REG_REFER2_BASE);
	vdpu_write_relaxed(vpu, backward_addr, VDPU_REG_REFER3_BASE);
}

void rk3399_vpu_mpeg2_dec_run(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	const struct v4l2_ctrl_mpeg2_slice_params *slice_params;
	const struct v4l2_mpeg2_sequence *sequence;
	const struct v4l2_mpeg2_picture *picture;
	u32 reg;

	src_buf = hantro_get_src_buf(ctx);
	dst_buf = hantro_get_dst_buf(ctx);

	hantro_prepare_run(ctx);

	slice_params = hantro_get_ctrl(ctx,
				       V4L2_CID_MPEG_VIDEO_MPEG2_SLICE_PARAMS);
	sequence = &slice_params->sequence;
	picture = &slice_params->picture;

	reg = VDPU_REG_DEC_ADV_PRE_DIS(0) |
	      VDPU_REG_DEC_SCMD_DIS(0) |
	      VDPU_REG_FILTERING_DIS(1) |
	      VDPU_REG_DEC_LATENCY(0);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(50));

	reg = VDPU_REG_INIT_QP(1) |
	      VDPU_REG_STREAM_LEN(slice_params->bit_size >> 3);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(51));

	reg = VDPU_REG_APF_THRESHOLD(8) |
	      VDPU_REG_STARTMB_X(0) |
	      VDPU_REG_STARTMB_Y(0);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(52));

	reg = VDPU_REG_DEC_MODE(5);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(53));

	reg = VDPU_REG_DEC_STRENDIAN_E(1) |
	      VDPU_REG_DEC_STRSWAP32_E(1) |
	      VDPU_REG_DEC_OUTSWAP32_E(1) |
	      VDPU_REG_DEC_INSWAP32_E(1) |
	      VDPU_REG_DEC_OUT_ENDIAN(1) |
	      VDPU_REG_DEC_IN_ENDIAN(1);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(54));

	reg = VDPU_REG_DEC_DATA_DISC_E(0) |
	      VDPU_REG_DEC_MAX_BURST(16) |
	      VDPU_REG_DEC_AXI_WR_ID(0) |
	      VDPU_REG_DEC_AXI_RD_ID(0);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(56));

	reg = VDPU_REG_RLC_MODE_E(0) |
	      VDPU_REG_PIC_INTERLACE_E(!sequence->progressive_sequence) |
	      VDPU_REG_PIC_FIELDMODE_E(picture->picture_structure != PICT_FRAME) |
	      VDPU_REG_PIC_B_E(picture->picture_coding_type == V4L2_MPEG2_PICTURE_CODING_TYPE_B) |
	      VDPU_REG_PIC_INTER_E(picture->picture_coding_type != V4L2_MPEG2_PICTURE_CODING_TYPE_I) |
	      VDPU_REG_PIC_TOPFIELD_E(picture->picture_structure == PICT_TOP_FIELD) |
	      VDPU_REG_FWD_INTERLACE_E(0) |
	      VDPU_REG_WRITE_MVS_E(0) |
	      VDPU_REG_DEC_TIMEOUT_E(1) |
	      VDPU_REG_DEC_CLK_GATE_E(1);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(57));

	reg = VDPU_REG_PIC_MB_WIDTH(MB_WIDTH(ctx->dst_fmt.width)) |
	      VDPU_REG_PIC_MB_HEIGHT_P(MB_HEIGHT(ctx->dst_fmt.height)) |
	      VDPU_REG_ALT_SCAN_E(picture->alternate_scan) |
	      VDPU_REG_TOPFIELDFIRST_E(picture->top_field_first);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(120));

	reg = VDPU_REG_STRM_START_BIT(slice_params->data_bit_offset) |
	      VDPU_REG_QSCALE_TYPE(picture->q_scale_type) |
	      VDPU_REG_CON_MV_E(picture->concealment_motion_vectors) |
	      VDPU_REG_INTRA_DC_PREC(picture->intra_dc_precision) |
	      VDPU_REG_INTRA_VLC_TAB(picture->intra_vlc_format) |
	      VDPU_REG_FRAME_PRED_DCT(picture->frame_pred_frame_dct);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(122));

	reg = VDPU_REG_ALT_SCAN_FLAG_E(picture->alternate_scan) |
	      VDPU_REG_FCODE_FWD_HOR(picture->f_code[0][0]) |
	      VDPU_REG_FCODE_FWD_VER(picture->f_code[0][1]) |
	      VDPU_REG_FCODE_BWD_HOR(picture->f_code[1][0]) |
	      VDPU_REG_FCODE_BWD_VER(picture->f_code[1][1]) |
	      VDPU_REG_MV_ACCURACY_FWD(1) |
	      VDPU_REG_MV_ACCURACY_BWD(1);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(136));

	rk3399_vpu_mpeg2_dec_set_quantization(vpu, ctx);

	rk3399_vpu_mpeg2_dec_set_buffers(vpu, ctx, &src_buf->vb2_buf,
					 &dst_buf->vb2_buf,
					 sequence, picture, slice_params);

	/* Kick the watchdog and start decoding */
	hantro_finish_run(ctx);

	reg = vdpu_read(vpu, VDPU_SWREG(57)) | VDPU_REG_DEC_E(1);
	vdpu_write(vpu, reg, VDPU_SWREG(57));
}
