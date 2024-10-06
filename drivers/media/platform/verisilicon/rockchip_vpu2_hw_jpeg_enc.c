// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 *
 * JPEG encoder
 * ------------
 * The VPU JPEG encoder produces JPEG baseline sequential format.
 * The quantization coefficients are 8-bit values, complying with
 * the baseline specification. Therefore, it requires
 * luma and chroma quantization tables. The hardware does entropy
 * encoding using internal Huffman tables, as specified in the JPEG
 * specification.
 *
 * In other words, only the luma and chroma quantization tables are
 * required for the encoding operation.
 *
 * Quantization luma table values are written to registers
 * VEPU_swreg_0-VEPU_swreg_15, and chroma table values to
 * VEPU_swreg_16-VEPU_swreg_31. A special order is needed, neither
 * zigzag, nor linear.
 */

#include <linux/unaligned.h>
#include <media/v4l2-mem2mem.h>
#include "hantro_jpeg.h"
#include "hantro.h"
#include "hantro_v4l2.h"
#include "hantro_hw.h"
#include "rockchip_vpu2_regs.h"

#define VEPU_JPEG_QUANT_TABLE_COUNT 16

static void rockchip_vpu2_set_src_img_ctrl(struct hantro_dev *vpu,
					   struct hantro_ctx *ctx)
{
	u32 overfill_r, overfill_b;
	u32 reg;

	/*
	 * The format width and height are already macroblock aligned
	 * by .vidioc_s_fmt_vid_cap_mplane() callback. Destination
	 * format width and height can be further modified by
	 * .vidioc_s_selection(), and the width is 4-aligned.
	 */
	overfill_r = ctx->src_fmt.width - ctx->dst_fmt.width;
	overfill_b = ctx->src_fmt.height - ctx->dst_fmt.height;

	reg = VEPU_REG_IN_IMG_CTRL_ROW_LEN(ctx->src_fmt.width);
	vepu_write_relaxed(vpu, reg, VEPU_REG_INPUT_LUMA_INFO);

	reg = VEPU_REG_IN_IMG_CTRL_OVRFLR_D4(overfill_r / 4) |
	      VEPU_REG_IN_IMG_CTRL_OVRFLB(overfill_b);
	/*
	 * This register controls the input crop, as the offset
	 * from the right/bottom within the last macroblock. The offset from the
	 * right must be divided by 4 and so the crop must be aligned to 4 pixels
	 * horizontally.
	 */
	vepu_write_relaxed(vpu, reg, VEPU_REG_ENC_OVER_FILL_STRM_OFFSET);

	reg = VEPU_REG_IN_IMG_CTRL_FMT(ctx->vpu_src_fmt->enc_fmt);
	vepu_write_relaxed(vpu, reg, VEPU_REG_ENC_CTRL1);
}

static void rockchip_vpu2_jpeg_enc_set_buffers(struct hantro_dev *vpu,
					       struct hantro_ctx *ctx,
					       struct vb2_buffer *src_buf,
					       struct vb2_buffer *dst_buf)
{
	struct v4l2_pix_format_mplane *pix_fmt = &ctx->src_fmt;
	dma_addr_t src[3];
	u32 size_left;

	size_left = vb2_plane_size(dst_buf, 0) - ctx->vpu_dst_fmt->header_size;
	if (WARN_ON(vb2_plane_size(dst_buf, 0) < ctx->vpu_dst_fmt->header_size))
		size_left = 0;

	WARN_ON(pix_fmt->num_planes > 3);

	vepu_write_relaxed(vpu, vb2_dma_contig_plane_dma_addr(dst_buf, 0) +
				ctx->vpu_dst_fmt->header_size,
			   VEPU_REG_ADDR_OUTPUT_STREAM);
	vepu_write_relaxed(vpu, size_left, VEPU_REG_STR_BUF_LIMIT);

	if (pix_fmt->num_planes == 1) {
		src[0] = vb2_dma_contig_plane_dma_addr(src_buf, 0);
		vepu_write_relaxed(vpu, src[0], VEPU_REG_ADDR_IN_PLANE_0);
	} else if (pix_fmt->num_planes == 2) {
		src[0] = vb2_dma_contig_plane_dma_addr(src_buf, 0);
		src[1] = vb2_dma_contig_plane_dma_addr(src_buf, 1);
		vepu_write_relaxed(vpu, src[0], VEPU_REG_ADDR_IN_PLANE_0);
		vepu_write_relaxed(vpu, src[1], VEPU_REG_ADDR_IN_PLANE_1);
	} else {
		src[0] = vb2_dma_contig_plane_dma_addr(src_buf, 0);
		src[1] = vb2_dma_contig_plane_dma_addr(src_buf, 1);
		src[2] = vb2_dma_contig_plane_dma_addr(src_buf, 2);
		vepu_write_relaxed(vpu, src[0], VEPU_REG_ADDR_IN_PLANE_0);
		vepu_write_relaxed(vpu, src[1], VEPU_REG_ADDR_IN_PLANE_1);
		vepu_write_relaxed(vpu, src[2], VEPU_REG_ADDR_IN_PLANE_2);
	}
}

static void
rockchip_vpu2_jpeg_enc_set_qtable(struct hantro_dev *vpu,
				  unsigned char *luma_qtable,
				  unsigned char *chroma_qtable)
{
	u32 reg, i;
	__be32 *luma_qtable_p;
	__be32 *chroma_qtable_p;

	luma_qtable_p = (__be32 *)luma_qtable;
	chroma_qtable_p = (__be32 *)chroma_qtable;

	/*
	 * Quantization table registers must be written in contiguous blocks.
	 * DO NOT collapse the below two "for" loops into one.
	 */
	for (i = 0; i < VEPU_JPEG_QUANT_TABLE_COUNT; i++) {
		reg = get_unaligned_be32(&luma_qtable_p[i]);
		vepu_write_relaxed(vpu, reg, VEPU_REG_JPEG_LUMA_QUAT(i));
	}

	for (i = 0; i < VEPU_JPEG_QUANT_TABLE_COUNT; i++) {
		reg = get_unaligned_be32(&chroma_qtable_p[i]);
		vepu_write_relaxed(vpu, reg, VEPU_REG_JPEG_CHROMA_QUAT(i));
	}
}

int rockchip_vpu2_jpeg_enc_run(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct hantro_jpeg_ctx jpeg_ctx;
	u32 reg;

	src_buf = hantro_get_src_buf(ctx);
	dst_buf = hantro_get_dst_buf(ctx);

	hantro_start_prepare_run(ctx);

	memset(&jpeg_ctx, 0, sizeof(jpeg_ctx));
	jpeg_ctx.buffer = vb2_plane_vaddr(&dst_buf->vb2_buf, 0);
	if (!jpeg_ctx.buffer)
		return -ENOMEM;

	jpeg_ctx.width = ctx->dst_fmt.width;
	jpeg_ctx.height = ctx->dst_fmt.height;
	jpeg_ctx.quality = ctx->jpeg_quality;
	hantro_jpeg_header_assemble(&jpeg_ctx);

	/* Switch to JPEG encoder mode before writing registers */
	vepu_write_relaxed(vpu, VEPU_REG_ENCODE_FORMAT_JPEG,
			   VEPU_REG_ENCODE_START);

	rockchip_vpu2_set_src_img_ctrl(vpu, ctx);
	rockchip_vpu2_jpeg_enc_set_buffers(vpu, ctx, &src_buf->vb2_buf,
					   &dst_buf->vb2_buf);
	rockchip_vpu2_jpeg_enc_set_qtable(vpu, jpeg_ctx.hw_luma_qtable,
					  jpeg_ctx.hw_chroma_qtable);

	reg = VEPU_REG_OUTPUT_SWAP32
		| VEPU_REG_OUTPUT_SWAP16
		| VEPU_REG_OUTPUT_SWAP8
		| VEPU_REG_INPUT_SWAP8
		| VEPU_REG_INPUT_SWAP16
		| VEPU_REG_INPUT_SWAP32;
	/* Make sure that all registers are written at this point. */
	vepu_write(vpu, reg, VEPU_REG_DATA_ENDIAN);

	reg = VEPU_REG_AXI_CTRL_BURST_LEN(16);
	vepu_write_relaxed(vpu, reg, VEPU_REG_AXI_CTRL);

	reg = VEPU_REG_MB_WIDTH(MB_WIDTH(ctx->src_fmt.width))
		| VEPU_REG_MB_HEIGHT(MB_HEIGHT(ctx->src_fmt.height))
		| VEPU_REG_FRAME_TYPE_INTRA
		| VEPU_REG_ENCODE_FORMAT_JPEG
		| VEPU_REG_ENCODE_ENABLE;

	/* Kick the watchdog and start encoding */
	hantro_end_prepare_run(ctx);
	vepu_write(vpu, reg, VEPU_REG_ENCODE_START);

	return 0;
}

void rockchip_vpu2_jpeg_enc_done(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	u32 bytesused = vepu_read(vpu, VEPU_REG_STR_BUF_LIMIT) / 8;
	struct vb2_v4l2_buffer *dst_buf = hantro_get_dst_buf(ctx);

	vb2_set_plane_payload(&dst_buf->vb2_buf, 0,
			      ctx->vpu_dst_fmt->header_size + bytesused);
}
