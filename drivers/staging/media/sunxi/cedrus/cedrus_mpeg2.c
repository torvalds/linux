// SPDX-License-Identifier: GPL-2.0
/*
 * Cedrus VPU driver
 *
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2018 Bootlin
 */

#include <media/videobuf2-dma-contig.h>

#include "cedrus.h"
#include "cedrus_hw.h"
#include "cedrus_regs.h"

/* Default MPEG-2 quantization coefficients, from the specification. */

static const u8 intra_quantization_matrix_default[64] = {
	8,  16, 16, 19, 16, 19, 22, 22,
	22, 22, 22, 22, 26, 24, 26, 27,
	27, 27, 26, 26, 26, 26, 27, 27,
	27, 29, 29, 29, 34, 34, 34, 29,
	29, 29, 27, 27, 29, 29, 32, 32,
	34, 34, 37, 38, 37, 35, 35, 34,
	35, 38, 38, 40, 40, 40, 48, 48,
	46, 46, 56, 56, 58, 69, 69, 83
};

static const u8 non_intra_quantization_matrix_default[64] = {
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16
};

static enum cedrus_irq_status cedrus_mpeg2_irq_status(struct cedrus_ctx *ctx)
{
	struct cedrus_dev *dev = ctx->dev;
	u32 reg;

	reg = cedrus_read(dev, VE_DEC_MPEG_STATUS);
	reg &= VE_DEC_MPEG_STATUS_CHECK_MASK;

	if (!reg)
		return CEDRUS_IRQ_NONE;

	if (reg & VE_DEC_MPEG_STATUS_CHECK_ERROR ||
	    !(reg & VE_DEC_MPEG_STATUS_SUCCESS))
		return CEDRUS_IRQ_ERROR;

	return CEDRUS_IRQ_OK;
}

static void cedrus_mpeg2_irq_clear(struct cedrus_ctx *ctx)
{
	struct cedrus_dev *dev = ctx->dev;

	cedrus_write(dev, VE_DEC_MPEG_STATUS, VE_DEC_MPEG_STATUS_CHECK_MASK);
}

static void cedrus_mpeg2_irq_disable(struct cedrus_ctx *ctx)
{
	struct cedrus_dev *dev = ctx->dev;
	u32 reg = cedrus_read(dev, VE_DEC_MPEG_CTRL);

	reg &= ~VE_DEC_MPEG_CTRL_IRQ_MASK;

	cedrus_write(dev, VE_DEC_MPEG_CTRL, reg);
}

static void cedrus_mpeg2_setup(struct cedrus_ctx *ctx, struct cedrus_run *run)
{
	const struct v4l2_ctrl_mpeg2_slice_params *slice_params;
	const struct v4l2_mpeg2_sequence *sequence;
	const struct v4l2_mpeg2_picture *picture;
	const struct v4l2_ctrl_mpeg2_quantization *quantization;
	dma_addr_t src_buf_addr, dst_luma_addr, dst_chroma_addr;
	dma_addr_t fwd_luma_addr, fwd_chroma_addr;
	dma_addr_t bwd_luma_addr, bwd_chroma_addr;
	struct cedrus_dev *dev = ctx->dev;
	const u8 *matrix;
	unsigned int i;
	u32 reg;

	slice_params = run->mpeg2.slice_params;
	sequence = &slice_params->sequence;
	picture = &slice_params->picture;

	quantization = run->mpeg2.quantization;

	/* Activate MPEG engine. */
	cedrus_engine_enable(dev, CEDRUS_CODEC_MPEG2);

	/* Set intra quantization matrix. */

	if (quantization && quantization->load_intra_quantiser_matrix)
		matrix = quantization->intra_quantiser_matrix;
	else
		matrix = intra_quantization_matrix_default;

	for (i = 0; i < 64; i++) {
		reg = VE_DEC_MPEG_IQMINPUT_WEIGHT(i, matrix[i]);
		reg |= VE_DEC_MPEG_IQMINPUT_FLAG_INTRA;

		cedrus_write(dev, VE_DEC_MPEG_IQMINPUT, reg);
	}

	/* Set non-intra quantization matrix. */

	if (quantization && quantization->load_non_intra_quantiser_matrix)
		matrix = quantization->non_intra_quantiser_matrix;
	else
		matrix = non_intra_quantization_matrix_default;

	for (i = 0; i < 64; i++) {
		reg = VE_DEC_MPEG_IQMINPUT_WEIGHT(i, matrix[i]);
		reg |= VE_DEC_MPEG_IQMINPUT_FLAG_NON_INTRA;

		cedrus_write(dev, VE_DEC_MPEG_IQMINPUT, reg);
	}

	/* Set MPEG picture header. */

	reg = VE_DEC_MPEG_MP12HDR_SLICE_TYPE(picture->picture_coding_type);
	reg |= VE_DEC_MPEG_MP12HDR_F_CODE(0, 0, picture->f_code[0][0]);
	reg |= VE_DEC_MPEG_MP12HDR_F_CODE(0, 1, picture->f_code[0][1]);
	reg |= VE_DEC_MPEG_MP12HDR_F_CODE(1, 0, picture->f_code[1][0]);
	reg |= VE_DEC_MPEG_MP12HDR_F_CODE(1, 1, picture->f_code[1][1]);
	reg |= VE_DEC_MPEG_MP12HDR_INTRA_DC_PRECISION(picture->intra_dc_precision);
	reg |= VE_DEC_MPEG_MP12HDR_INTRA_PICTURE_STRUCTURE(picture->picture_structure);
	reg |= VE_DEC_MPEG_MP12HDR_TOP_FIELD_FIRST(picture->top_field_first);
	reg |= VE_DEC_MPEG_MP12HDR_FRAME_PRED_FRAME_DCT(picture->frame_pred_frame_dct);
	reg |= VE_DEC_MPEG_MP12HDR_CONCEALMENT_MOTION_VECTORS(picture->concealment_motion_vectors);
	reg |= VE_DEC_MPEG_MP12HDR_Q_SCALE_TYPE(picture->q_scale_type);
	reg |= VE_DEC_MPEG_MP12HDR_INTRA_VLC_FORMAT(picture->intra_vlc_format);
	reg |= VE_DEC_MPEG_MP12HDR_ALTERNATE_SCAN(picture->alternate_scan);
	reg |= VE_DEC_MPEG_MP12HDR_FULL_PEL_FORWARD_VECTOR(0);
	reg |= VE_DEC_MPEG_MP12HDR_FULL_PEL_BACKWARD_VECTOR(0);

	cedrus_write(dev, VE_DEC_MPEG_MP12HDR, reg);

	/* Set frame dimensions. */

	reg = VE_DEC_MPEG_PICCODEDSIZE_WIDTH(sequence->horizontal_size);
	reg |= VE_DEC_MPEG_PICCODEDSIZE_HEIGHT(sequence->vertical_size);

	cedrus_write(dev, VE_DEC_MPEG_PICCODEDSIZE, reg);

	reg = VE_DEC_MPEG_PICBOUNDSIZE_WIDTH(ctx->src_fmt.width);
	reg |= VE_DEC_MPEG_PICBOUNDSIZE_HEIGHT(ctx->src_fmt.height);

	cedrus_write(dev, VE_DEC_MPEG_PICBOUNDSIZE, reg);

	/* Forward and backward prediction reference buffers. */

	fwd_luma_addr = cedrus_dst_buf_addr(ctx,
					    slice_params->forward_ref_index,
					    0);
	fwd_chroma_addr = cedrus_dst_buf_addr(ctx,
					      slice_params->forward_ref_index,
					      1);

	cedrus_write(dev, VE_DEC_MPEG_FWD_REF_LUMA_ADDR, fwd_luma_addr);
	cedrus_write(dev, VE_DEC_MPEG_FWD_REF_CHROMA_ADDR, fwd_chroma_addr);

	bwd_luma_addr = cedrus_dst_buf_addr(ctx,
					    slice_params->backward_ref_index,
					    0);
	bwd_chroma_addr = cedrus_dst_buf_addr(ctx,
					      slice_params->backward_ref_index,
					      1);

	cedrus_write(dev, VE_DEC_MPEG_BWD_REF_LUMA_ADDR, bwd_luma_addr);
	cedrus_write(dev, VE_DEC_MPEG_BWD_REF_CHROMA_ADDR, bwd_chroma_addr);

	/* Destination luma and chroma buffers. */

	dst_luma_addr = cedrus_dst_buf_addr(ctx, run->dst->vb2_buf.index, 0);
	dst_chroma_addr = cedrus_dst_buf_addr(ctx, run->dst->vb2_buf.index, 1);

	cedrus_write(dev, VE_DEC_MPEG_REC_LUMA, dst_luma_addr);
	cedrus_write(dev, VE_DEC_MPEG_REC_CHROMA, dst_chroma_addr);

	/* Source offset and length in bits. */

	cedrus_write(dev, VE_DEC_MPEG_VLD_OFFSET,
		     slice_params->data_bit_offset);

	reg = slice_params->bit_size - slice_params->data_bit_offset;
	cedrus_write(dev, VE_DEC_MPEG_VLD_LEN, reg);

	/* Source beginning and end addresses. */

	src_buf_addr = vb2_dma_contig_plane_dma_addr(&run->src->vb2_buf, 0);

	reg = VE_DEC_MPEG_VLD_ADDR_BASE(src_buf_addr);
	reg |= VE_DEC_MPEG_VLD_ADDR_VALID_PIC_DATA;
	reg |= VE_DEC_MPEG_VLD_ADDR_LAST_PIC_DATA;
	reg |= VE_DEC_MPEG_VLD_ADDR_FIRST_PIC_DATA;

	cedrus_write(dev, VE_DEC_MPEG_VLD_ADDR, reg);

	reg = src_buf_addr + DIV_ROUND_UP(slice_params->bit_size, 8);
	cedrus_write(dev, VE_DEC_MPEG_VLD_END_ADDR, reg);

	/* Macroblock address: start at the beginning. */
	reg = VE_DEC_MPEG_MBADDR_Y(0) | VE_DEC_MPEG_MBADDR_X(0);
	cedrus_write(dev, VE_DEC_MPEG_MBADDR, reg);

	/* Clear previous errors. */
	cedrus_write(dev, VE_DEC_MPEG_ERROR, 0);

	/* Clear correct macroblocks register. */
	cedrus_write(dev, VE_DEC_MPEG_CRTMBADDR, 0);

	/* Enable appropriate interruptions and components. */

	reg = VE_DEC_MPEG_CTRL_IRQ_MASK | VE_DEC_MPEG_CTRL_MC_NO_WRITEBACK |
	      VE_DEC_MPEG_CTRL_MC_CACHE_EN;

	cedrus_write(dev, VE_DEC_MPEG_CTRL, reg);
}

static void cedrus_mpeg2_trigger(struct cedrus_ctx *ctx)
{
	struct cedrus_dev *dev = ctx->dev;
	u32 reg;

	/* Trigger MPEG engine. */
	reg = VE_DEC_MPEG_TRIGGER_HW_MPEG_VLD | VE_DEC_MPEG_TRIGGER_MPEG2 |
	      VE_DEC_MPEG_TRIGGER_MB_BOUNDARY;

	cedrus_write(dev, VE_DEC_MPEG_TRIGGER, reg);
}

struct cedrus_dec_ops cedrus_dec_ops_mpeg2 = {
	.irq_clear	= cedrus_mpeg2_irq_clear,
	.irq_disable	= cedrus_mpeg2_irq_disable,
	.irq_status	= cedrus_mpeg2_irq_status,
	.setup		= cedrus_mpeg2_setup,
	.trigger	= cedrus_mpeg2_trigger,
};
