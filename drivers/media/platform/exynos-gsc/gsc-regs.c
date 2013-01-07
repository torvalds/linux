/*
 * Copyright (c) 2011 - 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung EXYNOS5 SoC series G-Scaler driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <mach/map.h>

#include "gsc-core.h"

void gsc_hw_set_sw_reset(struct gsc_dev *dev)
{
	writel(GSC_SW_RESET_SRESET, dev->regs + GSC_SW_RESET);
}

int gsc_wait_reset(struct gsc_dev *dev)
{
	unsigned long end = jiffies + msecs_to_jiffies(50);
	u32 cfg;

	while (time_before(jiffies, end)) {
		cfg = readl(dev->regs + GSC_SW_RESET);
		if (!cfg)
			return 0;
		usleep_range(10, 20);
	}

	return -EBUSY;
}

void gsc_hw_set_frm_done_irq_mask(struct gsc_dev *dev, bool mask)
{
	u32 cfg;

	cfg = readl(dev->regs + GSC_IRQ);
	if (mask)
		cfg |= GSC_IRQ_FRMDONE_MASK;
	else
		cfg &= ~GSC_IRQ_FRMDONE_MASK;
	writel(cfg, dev->regs + GSC_IRQ);
}

void gsc_hw_set_gsc_irq_enable(struct gsc_dev *dev, bool mask)
{
	u32 cfg;

	cfg = readl(dev->regs + GSC_IRQ);
	if (mask)
		cfg |= GSC_IRQ_ENABLE;
	else
		cfg &= ~GSC_IRQ_ENABLE;
	writel(cfg, dev->regs + GSC_IRQ);
}

void gsc_hw_set_input_buf_masking(struct gsc_dev *dev, u32 shift,
				bool enable)
{
	u32 cfg = readl(dev->regs + GSC_IN_BASE_ADDR_Y_MASK);
	u32 mask = 1 << shift;

	cfg &= ~mask;
	cfg |= enable << shift;

	writel(cfg, dev->regs + GSC_IN_BASE_ADDR_Y_MASK);
	writel(cfg, dev->regs + GSC_IN_BASE_ADDR_CB_MASK);
	writel(cfg, dev->regs + GSC_IN_BASE_ADDR_CR_MASK);
}

void gsc_hw_set_output_buf_masking(struct gsc_dev *dev, u32 shift,
				bool enable)
{
	u32 cfg = readl(dev->regs + GSC_OUT_BASE_ADDR_Y_MASK);
	u32 mask = 1 << shift;

	cfg &= ~mask;
	cfg |= enable << shift;

	writel(cfg, dev->regs + GSC_OUT_BASE_ADDR_Y_MASK);
	writel(cfg, dev->regs + GSC_OUT_BASE_ADDR_CB_MASK);
	writel(cfg, dev->regs + GSC_OUT_BASE_ADDR_CR_MASK);
}

void gsc_hw_set_input_addr(struct gsc_dev *dev, struct gsc_addr *addr,
				int index)
{
	pr_debug("src_buf[%d]: 0x%X, cb: 0x%X, cr: 0x%X", index,
			addr->y, addr->cb, addr->cr);
	writel(addr->y, dev->regs + GSC_IN_BASE_ADDR_Y(index));
	writel(addr->cb, dev->regs + GSC_IN_BASE_ADDR_CB(index));
	writel(addr->cr, dev->regs + GSC_IN_BASE_ADDR_CR(index));

}

void gsc_hw_set_output_addr(struct gsc_dev *dev,
			     struct gsc_addr *addr, int index)
{
	pr_debug("dst_buf[%d]: 0x%X, cb: 0x%X, cr: 0x%X",
			index, addr->y, addr->cb, addr->cr);
	writel(addr->y, dev->regs + GSC_OUT_BASE_ADDR_Y(index));
	writel(addr->cb, dev->regs + GSC_OUT_BASE_ADDR_CB(index));
	writel(addr->cr, dev->regs + GSC_OUT_BASE_ADDR_CR(index));
}

void gsc_hw_set_input_path(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;

	u32 cfg = readl(dev->regs + GSC_IN_CON);
	cfg &= ~(GSC_IN_PATH_MASK | GSC_IN_LOCAL_SEL_MASK);

	if (ctx->in_path == GSC_DMA)
		cfg |= GSC_IN_PATH_MEMORY;

	writel(cfg, dev->regs + GSC_IN_CON);
}

void gsc_hw_set_in_size(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;
	struct gsc_frame *frame = &ctx->s_frame;
	u32 cfg;

	/* Set input pixel offset */
	cfg = GSC_SRCIMG_OFFSET_X(frame->crop.left);
	cfg |= GSC_SRCIMG_OFFSET_Y(frame->crop.top);
	writel(cfg, dev->regs + GSC_SRCIMG_OFFSET);

	/* Set input original size */
	cfg = GSC_SRCIMG_WIDTH(frame->f_width);
	cfg |= GSC_SRCIMG_HEIGHT(frame->f_height);
	writel(cfg, dev->regs + GSC_SRCIMG_SIZE);

	/* Set input cropped size */
	cfg = GSC_CROPPED_WIDTH(frame->crop.width);
	cfg |= GSC_CROPPED_HEIGHT(frame->crop.height);
	writel(cfg, dev->regs + GSC_CROPPED_SIZE);
}

void gsc_hw_set_in_image_rgb(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;
	struct gsc_frame *frame = &ctx->s_frame;
	u32 cfg;

	cfg = readl(dev->regs + GSC_IN_CON);
	if (frame->colorspace == V4L2_COLORSPACE_REC709)
		cfg |= GSC_IN_RGB_HD_WIDE;
	else
		cfg |= GSC_IN_RGB_SD_WIDE;

	if (frame->fmt->pixelformat == V4L2_PIX_FMT_RGB565X)
		cfg |= GSC_IN_RGB565;
	else if (frame->fmt->pixelformat == V4L2_PIX_FMT_RGB32)
		cfg |= GSC_IN_XRGB8888;

	writel(cfg, dev->regs + GSC_IN_CON);
}

void gsc_hw_set_in_image_format(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;
	struct gsc_frame *frame = &ctx->s_frame;
	u32 i, depth = 0;
	u32 cfg;

	cfg = readl(dev->regs + GSC_IN_CON);
	cfg &= ~(GSC_IN_RGB_TYPE_MASK | GSC_IN_YUV422_1P_ORDER_MASK |
		 GSC_IN_CHROMA_ORDER_MASK | GSC_IN_FORMAT_MASK |
		 GSC_IN_TILE_TYPE_MASK | GSC_IN_TILE_MODE);
	writel(cfg, dev->regs + GSC_IN_CON);

	if (is_rgb(frame->fmt->color)) {
		gsc_hw_set_in_image_rgb(ctx);
		return;
	}
	for (i = 0; i < frame->fmt->num_planes; i++)
		depth += frame->fmt->depth[i];

	switch (frame->fmt->num_comp) {
	case 1:
		cfg |= GSC_IN_YUV422_1P;
		if (frame->fmt->yorder == GSC_LSB_Y)
			cfg |= GSC_IN_YUV422_1P_ORDER_LSB_Y;
		else
			cfg |= GSC_IN_YUV422_1P_OEDER_LSB_C;
		if (frame->fmt->corder == GSC_CBCR)
			cfg |= GSC_IN_CHROMA_ORDER_CBCR;
		else
			cfg |= GSC_IN_CHROMA_ORDER_CRCB;
		break;
	case 2:
		if (depth == 12)
			cfg |= GSC_IN_YUV420_2P;
		else
			cfg |= GSC_IN_YUV422_2P;
		if (frame->fmt->corder == GSC_CBCR)
			cfg |= GSC_IN_CHROMA_ORDER_CBCR;
		else
			cfg |= GSC_IN_CHROMA_ORDER_CRCB;
		break;
	case 3:
		if (depth == 12)
			cfg |= GSC_IN_YUV420_3P;
		else
			cfg |= GSC_IN_YUV422_3P;
		break;
	}

	writel(cfg, dev->regs + GSC_IN_CON);
}

void gsc_hw_set_output_path(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;

	u32 cfg = readl(dev->regs + GSC_OUT_CON);
	cfg &= ~GSC_OUT_PATH_MASK;

	if (ctx->out_path == GSC_DMA)
		cfg |= GSC_OUT_PATH_MEMORY;
	else
		cfg |= GSC_OUT_PATH_LOCAL;

	writel(cfg, dev->regs + GSC_OUT_CON);
}

void gsc_hw_set_out_size(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;
	struct gsc_frame *frame = &ctx->d_frame;
	u32 cfg;

	/* Set output original size */
	if (ctx->out_path == GSC_DMA) {
		cfg = GSC_DSTIMG_OFFSET_X(frame->crop.left);
		cfg |= GSC_DSTIMG_OFFSET_Y(frame->crop.top);
		writel(cfg, dev->regs + GSC_DSTIMG_OFFSET);

		cfg = GSC_DSTIMG_WIDTH(frame->f_width);
		cfg |= GSC_DSTIMG_HEIGHT(frame->f_height);
		writel(cfg, dev->regs + GSC_DSTIMG_SIZE);
	}

	/* Set output scaled size */
	if (ctx->gsc_ctrls.rotate->val == 90 ||
	    ctx->gsc_ctrls.rotate->val == 270) {
		cfg = GSC_SCALED_WIDTH(frame->crop.height);
		cfg |= GSC_SCALED_HEIGHT(frame->crop.width);
	} else {
		cfg = GSC_SCALED_WIDTH(frame->crop.width);
		cfg |= GSC_SCALED_HEIGHT(frame->crop.height);
	}
	writel(cfg, dev->regs + GSC_SCALED_SIZE);
}

void gsc_hw_set_out_image_rgb(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;
	struct gsc_frame *frame = &ctx->d_frame;
	u32 cfg;

	cfg = readl(dev->regs + GSC_OUT_CON);
	if (frame->colorspace == V4L2_COLORSPACE_REC709)
		cfg |= GSC_OUT_RGB_HD_WIDE;
	else
		cfg |= GSC_OUT_RGB_SD_WIDE;

	if (frame->fmt->pixelformat == V4L2_PIX_FMT_RGB565X)
		cfg |= GSC_OUT_RGB565;
	else if (frame->fmt->pixelformat == V4L2_PIX_FMT_RGB32)
		cfg |= GSC_OUT_XRGB8888;

	writel(cfg, dev->regs + GSC_OUT_CON);
}

void gsc_hw_set_out_image_format(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;
	struct gsc_frame *frame = &ctx->d_frame;
	u32 i, depth = 0;
	u32 cfg;

	cfg = readl(dev->regs + GSC_OUT_CON);
	cfg &= ~(GSC_OUT_RGB_TYPE_MASK | GSC_OUT_YUV422_1P_ORDER_MASK |
		 GSC_OUT_CHROMA_ORDER_MASK | GSC_OUT_FORMAT_MASK |
		 GSC_OUT_TILE_TYPE_MASK | GSC_OUT_TILE_MODE);
	writel(cfg, dev->regs + GSC_OUT_CON);

	if (is_rgb(frame->fmt->color)) {
		gsc_hw_set_out_image_rgb(ctx);
		return;
	}

	if (ctx->out_path != GSC_DMA) {
		cfg |= GSC_OUT_YUV444;
		goto end_set;
	}

	for (i = 0; i < frame->fmt->num_planes; i++)
		depth += frame->fmt->depth[i];

	switch (frame->fmt->num_comp) {
	case 1:
		cfg |= GSC_OUT_YUV422_1P;
		if (frame->fmt->yorder == GSC_LSB_Y)
			cfg |= GSC_OUT_YUV422_1P_ORDER_LSB_Y;
		else
			cfg |= GSC_OUT_YUV422_1P_OEDER_LSB_C;
		if (frame->fmt->corder == GSC_CBCR)
			cfg |= GSC_OUT_CHROMA_ORDER_CBCR;
		else
			cfg |= GSC_OUT_CHROMA_ORDER_CRCB;
		break;
	case 2:
		if (depth == 12)
			cfg |= GSC_OUT_YUV420_2P;
		else
			cfg |= GSC_OUT_YUV422_2P;
		if (frame->fmt->corder == GSC_CBCR)
			cfg |= GSC_OUT_CHROMA_ORDER_CBCR;
		else
			cfg |= GSC_OUT_CHROMA_ORDER_CRCB;
		break;
	case 3:
		cfg |= GSC_OUT_YUV420_3P;
		break;
	}

end_set:
	writel(cfg, dev->regs + GSC_OUT_CON);
}

void gsc_hw_set_prescaler(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;
	struct gsc_scaler *sc = &ctx->scaler;
	u32 cfg;

	cfg = GSC_PRESC_SHFACTOR(sc->pre_shfactor);
	cfg |= GSC_PRESC_H_RATIO(sc->pre_hratio);
	cfg |= GSC_PRESC_V_RATIO(sc->pre_vratio);
	writel(cfg, dev->regs + GSC_PRE_SCALE_RATIO);
}

void gsc_hw_set_mainscaler(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;
	struct gsc_scaler *sc = &ctx->scaler;
	u32 cfg;

	cfg = GSC_MAIN_H_RATIO_VALUE(sc->main_hratio);
	writel(cfg, dev->regs + GSC_MAIN_H_RATIO);

	cfg = GSC_MAIN_V_RATIO_VALUE(sc->main_vratio);
	writel(cfg, dev->regs + GSC_MAIN_V_RATIO);
}

void gsc_hw_set_rotation(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;
	u32 cfg;

	cfg = readl(dev->regs + GSC_IN_CON);
	cfg &= ~GSC_IN_ROT_MASK;

	switch (ctx->gsc_ctrls.rotate->val) {
	case 270:
		cfg |= GSC_IN_ROT_270;
		break;
	case 180:
		cfg |= GSC_IN_ROT_180;
		break;
	case 90:
		if (ctx->gsc_ctrls.hflip->val)
			cfg |= GSC_IN_ROT_90_XFLIP;
		else if (ctx->gsc_ctrls.vflip->val)
			cfg |= GSC_IN_ROT_90_YFLIP;
		else
			cfg |= GSC_IN_ROT_90;
		break;
	case 0:
		if (ctx->gsc_ctrls.hflip->val)
			cfg |= GSC_IN_ROT_XFLIP;
		else if (ctx->gsc_ctrls.vflip->val)
			cfg |= GSC_IN_ROT_YFLIP;
	}

	writel(cfg, dev->regs + GSC_IN_CON);
}

void gsc_hw_set_global_alpha(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;
	struct gsc_frame *frame = &ctx->d_frame;
	u32 cfg;

	if (!is_rgb(frame->fmt->color)) {
		pr_debug("Not a RGB format");
		return;
	}

	cfg = readl(dev->regs + GSC_OUT_CON);
	cfg &= ~GSC_OUT_GLOBAL_ALPHA_MASK;

	cfg |= GSC_OUT_GLOBAL_ALPHA(ctx->gsc_ctrls.global_alpha->val);
	writel(cfg, dev->regs + GSC_OUT_CON);
}

void gsc_hw_set_sfr_update(struct gsc_ctx *ctx)
{
	struct gsc_dev *dev = ctx->gsc_dev;
	u32 cfg;

	cfg = readl(dev->regs + GSC_ENABLE);
	cfg |= GSC_ENABLE_SFR_UPDATE;
	writel(cfg, dev->regs + GSC_ENABLE);
}
