// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

#include <linux/unaligned.h>
#include <media/v4l2-mem2mem.h>
#include "hantro_jpeg.h"
#include "hantro.h"
#include "hantro_v4l2.h"
#include "hantro_hw.h"
#include "hantro_h1_regs.h"

#define H1_JPEG_QUANT_TABLE_COUNT 16

static void hantro_h1_set_src_img_ctrl(struct hantro_dev *vpu,
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

	reg = H1_REG_IN_IMG_CTRL_ROW_LEN(ctx->src_fmt.width)
		| H1_REG_IN_IMG_CTRL_OVRFLR_D4(overfill_r / 4)
		| H1_REG_IN_IMG_CTRL_OVRFLB(overfill_b)
		| H1_REG_IN_IMG_CTRL_FMT(ctx->vpu_src_fmt->enc_fmt);
	vepu_write_relaxed(vpu, reg, H1_REG_IN_IMG_CTRL);
}

static void hantro_h1_jpeg_enc_set_buffers(struct hantro_dev *vpu,
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
			   H1_REG_ADDR_OUTPUT_STREAM);
	vepu_write_relaxed(vpu, size_left, H1_REG_STR_BUF_LIMIT);

	if (pix_fmt->num_planes == 1) {
		src[0] = vb2_dma_contig_plane_dma_addr(src_buf, 0);
		/* single plane formats we supported are all interlaced */
		vepu_write_relaxed(vpu, src[0], H1_REG_ADDR_IN_PLANE_0);
	} else if (pix_fmt->num_planes == 2) {
		src[0] = vb2_dma_contig_plane_dma_addr(src_buf, 0);
		src[1] = vb2_dma_contig_plane_dma_addr(src_buf, 1);
		vepu_write_relaxed(vpu, src[0], H1_REG_ADDR_IN_PLANE_0);
		vepu_write_relaxed(vpu, src[1], H1_REG_ADDR_IN_PLANE_1);
	} else {
		src[0] = vb2_dma_contig_plane_dma_addr(src_buf, 0);
		src[1] = vb2_dma_contig_plane_dma_addr(src_buf, 1);
		src[2] = vb2_dma_contig_plane_dma_addr(src_buf, 2);
		vepu_write_relaxed(vpu, src[0], H1_REG_ADDR_IN_PLANE_0);
		vepu_write_relaxed(vpu, src[1], H1_REG_ADDR_IN_PLANE_1);
		vepu_write_relaxed(vpu, src[2], H1_REG_ADDR_IN_PLANE_2);
	}
}

static void
hantro_h1_jpeg_enc_set_qtable(struct hantro_dev *vpu,
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
	for (i = 0; i < H1_JPEG_QUANT_TABLE_COUNT; i++) {
		reg = get_unaligned_be32(&luma_qtable_p[i]);
		vepu_write_relaxed(vpu, reg, H1_REG_JPEG_LUMA_QUAT(i));
	}

	for (i = 0; i < H1_JPEG_QUANT_TABLE_COUNT; i++) {
		reg = get_unaligned_be32(&chroma_qtable_p[i]);
		vepu_write_relaxed(vpu, reg, H1_REG_JPEG_CHROMA_QUAT(i));
	}
}

int hantro_h1_jpeg_enc_run(struct hantro_ctx *ctx)
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
	jpeg_ctx.width = ctx->dst_fmt.width;
	jpeg_ctx.height = ctx->dst_fmt.height;
	jpeg_ctx.quality = ctx->jpeg_quality;
	hantro_jpeg_header_assemble(&jpeg_ctx);

	/* Switch to JPEG encoder mode before writing registers */
	vepu_write_relaxed(vpu, H1_REG_ENC_CTRL_ENC_MODE_JPEG,
			   H1_REG_ENC_CTRL);

	hantro_h1_set_src_img_ctrl(vpu, ctx);
	hantro_h1_jpeg_enc_set_buffers(vpu, ctx, &src_buf->vb2_buf,
				       &dst_buf->vb2_buf);
	hantro_h1_jpeg_enc_set_qtable(vpu, jpeg_ctx.hw_luma_qtable,
				      jpeg_ctx.hw_chroma_qtable);

	reg = H1_REG_AXI_CTRL_OUTPUT_SWAP16
		| H1_REG_AXI_CTRL_INPUT_SWAP16
		| H1_REG_AXI_CTRL_BURST_LEN(16)
		| H1_REG_AXI_CTRL_OUTPUT_SWAP32
		| H1_REG_AXI_CTRL_INPUT_SWAP32
		| H1_REG_AXI_CTRL_OUTPUT_SWAP8
		| H1_REG_AXI_CTRL_INPUT_SWAP8;
	/* Make sure that all registers are written at this point. */
	vepu_write(vpu, reg, H1_REG_AXI_CTRL);

	reg = H1_REG_ENC_CTRL_WIDTH(MB_WIDTH(ctx->src_fmt.width))
		| H1_REG_ENC_CTRL_HEIGHT(MB_HEIGHT(ctx->src_fmt.height))
		| H1_REG_ENC_CTRL_ENC_MODE_JPEG
		| H1_REG_ENC_PIC_INTRA
		| H1_REG_ENC_CTRL_EN_BIT;

	hantro_end_prepare_run(ctx);

	vepu_write(vpu, reg, H1_REG_ENC_CTRL);

	return 0;
}

void hantro_h1_jpeg_enc_done(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	u32 bytesused = vepu_read(vpu, H1_REG_STR_BUF_LIMIT) / 8;
	struct vb2_v4l2_buffer *dst_buf = hantro_get_dst_buf(ctx);

	vb2_set_plane_payload(&dst_buf->vb2_buf, 0,
			      ctx->vpu_dst_fmt->header_size + bytesused);
}
