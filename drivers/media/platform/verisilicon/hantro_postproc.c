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
#include "hantro_g2_regs.h"
#include "hantro_v4l2.h"

#define HANTRO_PP_REG_WRITE(vpu, reg_name, val) \
{ \
	hantro_reg_write(vpu, \
			 &hantro_g1_postproc_regs.reg_name, \
			 val); \
}

#define HANTRO_PP_REG_WRITE_RELAXED(vpu, reg_name, val) \
{ \
	hantro_reg_write_relaxed(vpu, \
				 &hantro_g1_postproc_regs.reg_name, \
				 val); \
}

#define VPU_PP_IN_YUYV			0x0
#define VPU_PP_IN_NV12			0x1
#define VPU_PP_IN_YUV420		0x2
#define VPU_PP_IN_YUV240_TILED		0x5
#define VPU_PP_OUT_RGB			0x0
#define VPU_PP_OUT_YUYV			0x3

static const struct hantro_postproc_regs hantro_g1_postproc_regs = {
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

bool hantro_needs_postproc(const struct hantro_ctx *ctx,
			   const struct hantro_fmt *fmt)
{
	if (ctx->is_encoder)
		return false;

	if (ctx->need_postproc)
		return true;

	return fmt->postprocessed;
}

static void hantro_postproc_g1_enable(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct vb2_v4l2_buffer *dst_buf;
	u32 src_pp_fmt, dst_pp_fmt;
	dma_addr_t dst_dma;

	/* Turn on pipeline mode. Must be done first. */
	HANTRO_PP_REG_WRITE(vpu, pipeline_en, 0x1);

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

static int down_scale_factor(struct hantro_ctx *ctx)
{
	if (ctx->src_fmt.width == ctx->dst_fmt.width)
		return 0;

	return DIV_ROUND_CLOSEST(ctx->src_fmt.width, ctx->dst_fmt.width);
}

static void hantro_postproc_g2_enable(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct vb2_v4l2_buffer *dst_buf;
	int down_scale = down_scale_factor(ctx);
	int out_depth;
	size_t chroma_offset;
	dma_addr_t dst_dma;

	dst_buf = hantro_get_dst_buf(ctx);
	dst_dma = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	chroma_offset = ctx->dst_fmt.plane_fmt[0].bytesperline *
			ctx->dst_fmt.height;

	if (down_scale) {
		hantro_reg_write(vpu, &g2_down_scale_e, 1);
		hantro_reg_write(vpu, &g2_down_scale_y, down_scale >> 2);
		hantro_reg_write(vpu, &g2_down_scale_x, down_scale >> 2);
		hantro_write_addr(vpu, G2_DS_DST, dst_dma);
		hantro_write_addr(vpu, G2_DS_DST_CHR, dst_dma + (chroma_offset >> down_scale));
	} else {
		hantro_write_addr(vpu, G2_RS_OUT_LUMA_ADDR, dst_dma);
		hantro_write_addr(vpu, G2_RS_OUT_CHROMA_ADDR, dst_dma + chroma_offset);
	}

	out_depth = hantro_get_format_depth(ctx->dst_fmt.pixelformat);
	if (ctx->dev->variant->legacy_regs) {
		u8 pp_shift = 0;

		if (out_depth > 8)
			pp_shift = 16 - out_depth;

		hantro_reg_write(ctx->dev, &g2_rs_out_bit_depth, out_depth);
		hantro_reg_write(ctx->dev, &g2_pp_pix_shift, pp_shift);
	} else {
		hantro_reg_write(vpu, &g2_output_8_bits, out_depth > 8 ? 0 : 1);
		hantro_reg_write(vpu, &g2_output_format, out_depth > 8 ? 1 : 0);
	}
	hantro_reg_write(vpu, &g2_out_rs_e, 1);
}

static int hantro_postproc_g2_enum_framesizes(struct hantro_ctx *ctx,
					      struct v4l2_frmsizeenum *fsize)
{
	/**
	 * G2 scaler can scale down by 0, 2, 4 or 8
	 * use fsize->index has power of 2 diviser
	 **/
	if (fsize->index > 3)
		return -EINVAL;

	if (!ctx->src_fmt.width || !ctx->src_fmt.height)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = ctx->src_fmt.width >> fsize->index;
	fsize->discrete.height = ctx->src_fmt.height >> fsize->index;

	return 0;
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
	struct v4l2_pix_format_mplane pix_mp;
	const struct hantro_fmt *fmt;
	unsigned int i, buf_size;

	/* this should always pick native format */
	fmt = hantro_get_default_fmt(ctx, false, ctx->bit_depth, HANTRO_AUTO_POSTPROC);
	if (!fmt)
		return -EINVAL;
	v4l2_fill_pixfmt_mp(&pix_mp, fmt->fourcc, ctx->src_fmt.width,
			    ctx->src_fmt.height);

	buf_size = pix_mp.plane_fmt[0].sizeimage;
	if (ctx->vpu_src_fmt->fourcc == V4L2_PIX_FMT_H264_SLICE)
		buf_size += hantro_h264_mv_size(pix_mp.width,
						pix_mp.height);
	else if (ctx->vpu_src_fmt->fourcc == V4L2_PIX_FMT_VP9_FRAME)
		buf_size += hantro_vp9_mv_size(pix_mp.width,
					       pix_mp.height);
	else if (ctx->vpu_src_fmt->fourcc == V4L2_PIX_FMT_HEVC_SLICE)
		buf_size += hantro_hevc_mv_size(pix_mp.width,
						pix_mp.height);
	else if (ctx->vpu_src_fmt->fourcc == V4L2_PIX_FMT_AV1_FRAME)
		buf_size += hantro_av1_mv_size(pix_mp.width,
					       pix_mp.height);

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

static void hantro_postproc_g1_disable(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	HANTRO_PP_REG_WRITE(vpu, pipeline_en, 0x0);
}

static void hantro_postproc_g2_disable(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	hantro_reg_write(vpu, &g2_out_rs_e, 0);
}

void hantro_postproc_disable(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	if (vpu->variant->postproc_ops && vpu->variant->postproc_ops->disable)
		vpu->variant->postproc_ops->disable(ctx);
}

void hantro_postproc_enable(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	if (vpu->variant->postproc_ops && vpu->variant->postproc_ops->enable)
		vpu->variant->postproc_ops->enable(ctx);
}

int hanto_postproc_enum_framesizes(struct hantro_ctx *ctx,
				   struct v4l2_frmsizeenum *fsize)
{
	struct hantro_dev *vpu = ctx->dev;

	if (vpu->variant->postproc_ops && vpu->variant->postproc_ops->enum_framesizes)
		return vpu->variant->postproc_ops->enum_framesizes(ctx, fsize);

	return -EINVAL;
}

const struct hantro_postproc_ops hantro_g1_postproc_ops = {
	.enable = hantro_postproc_g1_enable,
	.disable = hantro_postproc_g1_disable,
};

const struct hantro_postproc_ops hantro_g2_postproc_ops = {
	.enable = hantro_postproc_g2_enable,
	.disable = hantro_postproc_g2_disable,
	.enum_framesizes = hantro_postproc_g2_enum_framesizes,
};
