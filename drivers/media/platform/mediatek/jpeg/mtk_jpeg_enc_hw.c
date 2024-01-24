// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: Xia Jiang <xia.jiang@mediatek.com>
 *
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "mtk_jpeg_enc_hw.h"

static const struct mtk_jpeg_enc_qlt mtk_jpeg_enc_quality[] = {
	{.quality_param = 34, .hardware_value = JPEG_ENC_QUALITY_Q34},
	{.quality_param = 39, .hardware_value = JPEG_ENC_QUALITY_Q39},
	{.quality_param = 48, .hardware_value = JPEG_ENC_QUALITY_Q48},
	{.quality_param = 60, .hardware_value = JPEG_ENC_QUALITY_Q60},
	{.quality_param = 64, .hardware_value = JPEG_ENC_QUALITY_Q64},
	{.quality_param = 68, .hardware_value = JPEG_ENC_QUALITY_Q68},
	{.quality_param = 74, .hardware_value = JPEG_ENC_QUALITY_Q74},
	{.quality_param = 80, .hardware_value = JPEG_ENC_QUALITY_Q80},
	{.quality_param = 82, .hardware_value = JPEG_ENC_QUALITY_Q82},
	{.quality_param = 84, .hardware_value = JPEG_ENC_QUALITY_Q84},
	{.quality_param = 87, .hardware_value = JPEG_ENC_QUALITY_Q87},
	{.quality_param = 90, .hardware_value = JPEG_ENC_QUALITY_Q90},
	{.quality_param = 92, .hardware_value = JPEG_ENC_QUALITY_Q92},
	{.quality_param = 95, .hardware_value = JPEG_ENC_QUALITY_Q95},
	{.quality_param = 97, .hardware_value = JPEG_ENC_QUALITY_Q97},
};

void mtk_jpeg_enc_reset(void __iomem *base)
{
	writel(0, base + JPEG_ENC_RSTB);
	writel(JPEG_ENC_RESET_BIT, base + JPEG_ENC_RSTB);
	writel(0, base + JPEG_ENC_CODEC_SEL);
}

u32 mtk_jpeg_enc_get_file_size(void __iomem *base)
{
	return readl(base + JPEG_ENC_DMA_ADDR0) -
	       readl(base + JPEG_ENC_DST_ADDR0);
}

void mtk_jpeg_enc_start(void __iomem *base)
{
	u32 value;

	value = readl(base + JPEG_ENC_CTRL);
	value |= JPEG_ENC_CTRL_INT_EN_BIT | JPEG_ENC_CTRL_ENABLE_BIT;
	writel(value, base + JPEG_ENC_CTRL);
}

void mtk_jpeg_set_enc_src(struct mtk_jpeg_ctx *ctx,  void __iomem *base,
			  struct vb2_buffer *src_buf)
{
	int i;
	dma_addr_t dma_addr;

	for (i = 0; i < src_buf->num_planes; i++) {
		dma_addr = vb2_dma_contig_plane_dma_addr(src_buf, i) +
			   src_buf->planes[i].data_offset;
		if (!i)
			writel(dma_addr, base + JPEG_ENC_SRC_LUMA_ADDR);
		else
			writel(dma_addr, base + JPEG_ENC_SRC_CHROMA_ADDR);
	}
}

void mtk_jpeg_set_enc_dst(struct mtk_jpeg_ctx *ctx, void __iomem *base,
			  struct vb2_buffer *dst_buf)
{
	dma_addr_t dma_addr;
	size_t size;
	u32 dma_addr_offset;
	u32 dma_addr_offsetmask;

	dma_addr = vb2_dma_contig_plane_dma_addr(dst_buf, 0);
	dma_addr_offset = ctx->enable_exif ? MTK_JPEG_MAX_EXIF_SIZE : 0;
	dma_addr_offsetmask = dma_addr & JPEG_ENC_DST_ADDR_OFFSET_MASK;
	size = vb2_plane_size(dst_buf, 0);

	writel(dma_addr_offset & ~0xf, base + JPEG_ENC_OFFSET_ADDR);
	writel(dma_addr_offsetmask & 0xf, base + JPEG_ENC_BYTE_OFFSET_MASK);
	writel(dma_addr & ~0xf, base + JPEG_ENC_DST_ADDR0);
	writel((dma_addr + size) & ~0xf, base + JPEG_ENC_STALL_ADDR0);
}

void mtk_jpeg_set_enc_params(struct mtk_jpeg_ctx *ctx,  void __iomem *base)
{
	u32 value;
	u32 width = ctx->out_q.enc_crop_rect.width;
	u32 height = ctx->out_q.enc_crop_rect.height;
	u32 enc_format = ctx->out_q.fmt->fourcc;
	u32 bytesperline = ctx->out_q.pix_mp.plane_fmt[0].bytesperline;
	u32 blk_num;
	u32 img_stride;
	u32 mem_stride;
	u32 i, enc_quality;
	u32 nr_enc_quality = ARRAY_SIZE(mtk_jpeg_enc_quality);

	value = width << 16 | height;
	writel(value, base + JPEG_ENC_IMG_SIZE);

	if (enc_format == V4L2_PIX_FMT_NV12M ||
	    enc_format == V4L2_PIX_FMT_NV21M)
	    /*
	     * Total 8 x 8 block number of luma and chroma.
	     * The number of blocks is counted from 0.
	     */
		blk_num = DIV_ROUND_UP(width, 16) *
			  DIV_ROUND_UP(height, 16) * 6 - 1;
	else
		blk_num = DIV_ROUND_UP(width, 16) *
			  DIV_ROUND_UP(height, 8) * 4 - 1;
	writel(blk_num, base + JPEG_ENC_BLK_NUM);

	if (enc_format == V4L2_PIX_FMT_NV12M ||
	    enc_format == V4L2_PIX_FMT_NV21M) {
		/* 4:2:0 */
		img_stride = round_up(width, 16);
		mem_stride = bytesperline;
	} else {
		/* 4:2:2 */
		img_stride = round_up(width * 2, 32);
		mem_stride = img_stride;
	}
	writel(img_stride, base + JPEG_ENC_IMG_STRIDE);
	writel(mem_stride, base + JPEG_ENC_STRIDE);

	enc_quality = mtk_jpeg_enc_quality[nr_enc_quality - 1].hardware_value;
	for (i = 0; i < nr_enc_quality; i++) {
		if (ctx->enc_quality <= mtk_jpeg_enc_quality[i].quality_param) {
			enc_quality = mtk_jpeg_enc_quality[i].hardware_value;
			break;
		}
	}
	writel(enc_quality, base + JPEG_ENC_QUALITY);

	value = readl(base + JPEG_ENC_CTRL);
	value &= ~JPEG_ENC_CTRL_YUV_FORMAT_MASK;
	value |= (ctx->out_q.fmt->hw_format & 3) << 3;
	if (ctx->enable_exif)
		value |= JPEG_ENC_CTRL_FILE_FORMAT_BIT;
	else
		value &= ~JPEG_ENC_CTRL_FILE_FORMAT_BIT;
	if (ctx->restart_interval)
		value |= JPEG_ENC_CTRL_RESTART_EN_BIT;
	else
		value &= ~JPEG_ENC_CTRL_RESTART_EN_BIT;
	writel(value, base + JPEG_ENC_CTRL);

	writel(ctx->restart_interval, base + JPEG_ENC_RST_MCU_NUM);
}
