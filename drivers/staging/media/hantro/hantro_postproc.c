// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro G1 post-processor support
 *
 * Copyright (C) 2019 Collabora, Ltd.
 */

#include <linux/dma-mapping.h>
#include <linux/types.h>

#include "hantro.h"
#include "hantro_hw.h"
#include "hantro_g1_regs.h"

#define HANTRO_PP_REG_WRITE(vpu, reg_name, val) \
{ \
	hantro_reg_write(vpu, \
			 &(vpu)->variant->postproc_regs->reg_name, \
			 val); \
}

#define HANTRO_PP_REG_WRITE_S(vpu, reg_name, val) \
{ \
	hantro_reg_write_s(vpu, \
			   &(vpu)->variant->postproc_regs->reg_name, \
			   val); \
}

#define VPU_PP_IN_YUYV			0x0
#define VPU_PP_IN_NV12			0x1
#define VPU_PP_IN_YUV420		0x2
#define VPU_PP_IN_YUV240_TILED		0x5
#define VPU_PP_OUT_RGB			0x0
#define VPU_PP_OUT_YUYV			0x3

const struct hantro_postproc_regs hantro_g1_postproc_regs = {
	.pipeline_en = {G1_REG_PP_INTERRUPT, 1, 0x1},
	.max_burst = {G1_REG_PP_DEV_CONFIG, 0, 0x1f},
	.clk_gate = {G1_REG_PP_DEV_CONFIG, 1, 0x1},
	.out_swap32 = {G1_REG_PP_DEV_CONFIG, 5, 0x1},
	.out_endian = {G1_REG_PP_DEV_CONFIG, 6, 0x1},
	.out_luma_base = {G1_REG_PP_OUT_LUMA_BASE, 0, 0xffffffff},
	.input_width = {G1_REG_PP_INPUT_SIZE, 0, 0x1ff},
	.input_height = {G1_REG_PP_INPUT_SIZE, 9, 0x1ff},
	.output_width = {G1_REG_PP_CONTROL, 4, 0x7ff},
	.output_height = {G1_REG_PP_CONTROL, 15, 0x7ff},
	.input_fmt = {G1_REG_PP_CONTROL, 29, 0x7},
	.output_fmt = {G1_REG_PP_CONTROL, 26, 0x7},
	.orig_width = {G1_REG_PP_MASK1_ORIG_WIDTH, 23, 0x1ff},
	.display_width = {G1_REG_PP_DISPLAY_WIDTH, 0, 0xfff},
};

void hantro_postproc_enable(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct vb2_v4l2_buffer *dst_buf;
	u32 src_pp_fmt, dst_pp_fmt;
	dma_addr_t dst_dma;

	if (!vpu->variant->postproc_regs)
		return;

	/* Turn on pipeline mode. Must be done first. */
	HANTRO_PP_REG_WRITE_S(vpu, pipeline_en, 0x1);

	src_pp_fmt = VPU_PP_IN_NV12;

	switch (ctx->vpu_dst_fmt->fourcc) {
	case V4L2_PIX_FMT_YUYV:
		dst_pp_fmt = VPU_PP_OUT_YUYV;
		break;
	default:
		WARN(1, "output format %d not supported by the post-processor, this wasn't expected.",
		     ctx->vpu_dst_fmt->fourcc);
		dst_pp_fmt = 0;
		break;
	}

	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	dst_dma = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);

	HANTRO_PP_REG_WRITE(vpu, clk_gate, 0x1);
	HANTRO_PP_REG_WRITE(vpu, out_endian, 0x1);
	HANTRO_PP_REG_WRITE(vpu, out_swap32, 0x1);
	HANTRO_PP_REG_WRITE(vpu, max_burst, 16);
	HANTRO_PP_REG_WRITE(vpu, out_luma_base, dst_dma);
	HANTRO_PP_REG_WRITE(vpu, input_width, MB_WIDTH(ctx->dst_fmt.width));
	HANTRO_PP_REG_WRITE(vpu, input_height, MB_HEIGHT(ctx->dst_fmt.height));
	HANTRO_PP_REG_WRITE(vpu, input_fmt, src_pp_fmt);
	HANTRO_PP_REG_WRITE(vpu, output_fmt, dst_pp_fmt);
	HANTRO_PP_REG_WRITE(vpu, output_width, ctx->dst_fmt.width);
	HANTRO_PP_REG_WRITE(vpu, output_height, ctx->dst_fmt.height);
	HANTRO_PP_REG_WRITE(vpu, orig_width, MB_WIDTH(ctx->dst_fmt.width));
	HANTRO_PP_REG_WRITE(vpu, display_width, ctx->dst_fmt.width);
}

void hantro_postproc_free(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	unsigned int i;

	for (i = 0; i < VB2_MAX_FRAME; ++i) {
		struct hantro_aux_buf *priv = &ctx->postproc.dec_q[i];

		if (priv->cpu) {
			dma_free_attrs(vpu->dev, priv->size, priv->cpu,
				       priv->dma, priv->attrs);
			priv->cpu = NULL;
		}
	}
}

int hantro_postproc_alloc(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	struct vb2_queue *cap_queue = &m2m_ctx->cap_q_ctx.q;
	unsigned int num_buffers = cap_queue->num_buffers;
	unsigned int i, buf_size;

	buf_size = ctx->dst_fmt.plane_fmt[0].sizeimage +
		   hantro_h264_mv_size(ctx->dst_fmt.width,
				       ctx->dst_fmt.height);

	for (i = 0; i < num_buffers; ++i) {
		struct hantro_aux_buf *priv = &ctx->postproc.dec_q[i];

		/*
		 * The buffers on this queue are meant as intermediate
		 * buffers for the decoder, so no mapping is needed.
		 */
		priv->attrs = DMA_ATTR_NO_KERNEL_MAPPING;
		priv->cpu = dma_alloc_attrs(vpu->dev, buf_size, &priv->dma,
					    GFP_KERNEL, priv->attrs);
		if (!priv->cpu)
			return -ENOMEM;
		priv->size = buf_size;
	}
	return 0;
}

void hantro_postproc_disable(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	if (!vpu->variant->postproc_regs)
		return;

	HANTRO_PP_REG_WRITE_S(vpu, pipeline_en, 0x0);
}
