/*
 * Register interface file for Samsung Camera Interface (FIMC) driver
 *
 * Copyright (C) 2010 - 2013 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/regmap.h>

#include <media/exynos-fimc.h>
#include "media-dev.h"

#include "fimc-reg.h"
#include "fimc-core.h"

void fimc_hw_reset(struct fimc_dev *dev)
{
	u32 cfg;

	cfg = readl(dev->regs + FIMC_REG_CISRCFMT);
	cfg |= FIMC_REG_CISRCFMT_ITU601_8BIT;
	writel(cfg, dev->regs + FIMC_REG_CISRCFMT);

	/* Software reset. */
	cfg = readl(dev->regs + FIMC_REG_CIGCTRL);
	cfg |= (FIMC_REG_CIGCTRL_SWRST | FIMC_REG_CIGCTRL_IRQ_LEVEL);
	writel(cfg, dev->regs + FIMC_REG_CIGCTRL);
	udelay(10);

	cfg = readl(dev->regs + FIMC_REG_CIGCTRL);
	cfg &= ~FIMC_REG_CIGCTRL_SWRST;
	writel(cfg, dev->regs + FIMC_REG_CIGCTRL);

	if (dev->drv_data->out_buf_count > 4)
		fimc_hw_set_dma_seq(dev, 0xF);
}

static u32 fimc_hw_get_in_flip(struct fimc_ctx *ctx)
{
	u32 flip = FIMC_REG_MSCTRL_FLIP_NORMAL;

	if (ctx->hflip)
		flip = FIMC_REG_MSCTRL_FLIP_Y_MIRROR;
	if (ctx->vflip)
		flip = FIMC_REG_MSCTRL_FLIP_X_MIRROR;

	if (ctx->rotation <= 90)
		return flip;

	return (flip ^ FIMC_REG_MSCTRL_FLIP_180) & FIMC_REG_MSCTRL_FLIP_180;
}

static u32 fimc_hw_get_target_flip(struct fimc_ctx *ctx)
{
	u32 flip = FIMC_REG_CITRGFMT_FLIP_NORMAL;

	if (ctx->hflip)
		flip |= FIMC_REG_CITRGFMT_FLIP_Y_MIRROR;
	if (ctx->vflip)
		flip |= FIMC_REG_CITRGFMT_FLIP_X_MIRROR;

	if (ctx->rotation <= 90)
		return flip;

	return (flip ^ FIMC_REG_CITRGFMT_FLIP_180) & FIMC_REG_CITRGFMT_FLIP_180;
}

void fimc_hw_set_rotation(struct fimc_ctx *ctx)
{
	u32 cfg, flip;
	struct fimc_dev *dev = ctx->fimc_dev;

	cfg = readl(dev->regs + FIMC_REG_CITRGFMT);
	cfg &= ~(FIMC_REG_CITRGFMT_INROT90 | FIMC_REG_CITRGFMT_OUTROT90 |
		 FIMC_REG_CITRGFMT_FLIP_180);

	/*
	 * The input and output rotator cannot work simultaneously.
	 * Use the output rotator in output DMA mode or the input rotator
	 * in direct fifo output mode.
	 */
	if (ctx->rotation == 90 || ctx->rotation == 270) {
		if (ctx->out_path == FIMC_IO_LCDFIFO)
			cfg |= FIMC_REG_CITRGFMT_INROT90;
		else
			cfg |= FIMC_REG_CITRGFMT_OUTROT90;
	}

	if (ctx->out_path == FIMC_IO_DMA) {
		cfg |= fimc_hw_get_target_flip(ctx);
		writel(cfg, dev->regs + FIMC_REG_CITRGFMT);
	} else {
		/* LCD FIFO path */
		flip = readl(dev->regs + FIMC_REG_MSCTRL);
		flip &= ~FIMC_REG_MSCTRL_FLIP_MASK;
		flip |= fimc_hw_get_in_flip(ctx);
		writel(flip, dev->regs + FIMC_REG_MSCTRL);
	}
}

void fimc_hw_set_target_format(struct fimc_ctx *ctx)
{
	u32 cfg;
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->d_frame;

	dbg("w= %d, h= %d color: %d", frame->width,
	    frame->height, frame->fmt->color);

	cfg = readl(dev->regs + FIMC_REG_CITRGFMT);
	cfg &= ~(FIMC_REG_CITRGFMT_FMT_MASK | FIMC_REG_CITRGFMT_HSIZE_MASK |
		 FIMC_REG_CITRGFMT_VSIZE_MASK);

	switch (frame->fmt->color) {
	case FIMC_FMT_RGB444...FIMC_FMT_RGB888:
		cfg |= FIMC_REG_CITRGFMT_RGB;
		break;
	case FIMC_FMT_YCBCR420:
		cfg |= FIMC_REG_CITRGFMT_YCBCR420;
		break;
	case FIMC_FMT_YCBYCR422...FIMC_FMT_CRYCBY422:
		if (frame->fmt->colplanes == 1)
			cfg |= FIMC_REG_CITRGFMT_YCBCR422_1P;
		else
			cfg |= FIMC_REG_CITRGFMT_YCBCR422;
		break;
	default:
		break;
	}

	if (ctx->rotation == 90 || ctx->rotation == 270)
		cfg |= (frame->height << 16) | frame->width;
	else
		cfg |= (frame->width << 16) | frame->height;

	writel(cfg, dev->regs + FIMC_REG_CITRGFMT);

	cfg = readl(dev->regs + FIMC_REG_CITAREA);
	cfg &= ~FIMC_REG_CITAREA_MASK;
	cfg |= (frame->width * frame->height);
	writel(cfg, dev->regs + FIMC_REG_CITAREA);
}

static void fimc_hw_set_out_dma_size(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->d_frame;
	u32 cfg;

	cfg = (frame->f_height << 16) | frame->f_width;
	writel(cfg, dev->regs + FIMC_REG_ORGOSIZE);

	/* Select color space conversion equation (HD/SD size).*/
	cfg = readl(dev->regs + FIMC_REG_CIGCTRL);
	if (frame->f_width >= 1280) /* HD */
		cfg |= FIMC_REG_CIGCTRL_CSC_ITU601_709;
	else	/* SD */
		cfg &= ~FIMC_REG_CIGCTRL_CSC_ITU601_709;
	writel(cfg, dev->regs + FIMC_REG_CIGCTRL);

}

void fimc_hw_set_out_dma(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->d_frame;
	struct fimc_dma_offset *offset = &frame->dma_offset;
	struct fimc_fmt *fmt = frame->fmt;
	u32 cfg;

	/* Set the input dma offsets. */
	cfg = (offset->y_v << 16) | offset->y_h;
	writel(cfg, dev->regs + FIMC_REG_CIOYOFF);

	cfg = (offset->cb_v << 16) | offset->cb_h;
	writel(cfg, dev->regs + FIMC_REG_CIOCBOFF);

	cfg = (offset->cr_v << 16) | offset->cr_h;
	writel(cfg, dev->regs + FIMC_REG_CIOCROFF);

	fimc_hw_set_out_dma_size(ctx);

	/* Configure chroma components order. */
	cfg = readl(dev->regs + FIMC_REG_CIOCTRL);

	cfg &= ~(FIMC_REG_CIOCTRL_ORDER2P_MASK |
		 FIMC_REG_CIOCTRL_ORDER422_MASK |
		 FIMC_REG_CIOCTRL_YCBCR_PLANE_MASK |
		 FIMC_REG_CIOCTRL_RGB16FMT_MASK);

	if (fmt->colplanes == 1)
		cfg |= ctx->out_order_1p;
	else if (fmt->colplanes == 2)
		cfg |= ctx->out_order_2p | FIMC_REG_CIOCTRL_YCBCR_2PLANE;
	else if (fmt->colplanes == 3)
		cfg |= FIMC_REG_CIOCTRL_YCBCR_3PLANE;

	if (fmt->color == FIMC_FMT_RGB565)
		cfg |= FIMC_REG_CIOCTRL_RGB565;
	else if (fmt->color == FIMC_FMT_RGB555)
		cfg |= FIMC_REG_CIOCTRL_ARGB1555;
	else if (fmt->color == FIMC_FMT_RGB444)
		cfg |= FIMC_REG_CIOCTRL_ARGB4444;

	writel(cfg, dev->regs + FIMC_REG_CIOCTRL);
}

static void fimc_hw_en_autoload(struct fimc_dev *dev, int enable)
{
	u32 cfg = readl(dev->regs + FIMC_REG_ORGISIZE);
	if (enable)
		cfg |= FIMC_REG_CIREAL_ISIZE_AUTOLOAD_EN;
	else
		cfg &= ~FIMC_REG_CIREAL_ISIZE_AUTOLOAD_EN;
	writel(cfg, dev->regs + FIMC_REG_ORGISIZE);
}

void fimc_hw_en_lastirq(struct fimc_dev *dev, int enable)
{
	u32 cfg = readl(dev->regs + FIMC_REG_CIOCTRL);
	if (enable)
		cfg |= FIMC_REG_CIOCTRL_LASTIRQ_ENABLE;
	else
		cfg &= ~FIMC_REG_CIOCTRL_LASTIRQ_ENABLE;
	writel(cfg, dev->regs + FIMC_REG_CIOCTRL);
}

void fimc_hw_set_prescaler(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev =  ctx->fimc_dev;
	struct fimc_scaler *sc = &ctx->scaler;
	u32 cfg, shfactor;

	shfactor = 10 - (sc->hfactor + sc->vfactor);
	cfg = shfactor << 28;

	cfg |= (sc->pre_hratio << 16) | sc->pre_vratio;
	writel(cfg, dev->regs + FIMC_REG_CISCPRERATIO);

	cfg = (sc->pre_dst_width << 16) | sc->pre_dst_height;
	writel(cfg, dev->regs + FIMC_REG_CISCPREDST);
}

static void fimc_hw_set_scaler(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_scaler *sc = &ctx->scaler;
	struct fimc_frame *src_frame = &ctx->s_frame;
	struct fimc_frame *dst_frame = &ctx->d_frame;

	u32 cfg = readl(dev->regs + FIMC_REG_CISCCTRL);

	cfg &= ~(FIMC_REG_CISCCTRL_CSCR2Y_WIDE | FIMC_REG_CISCCTRL_CSCY2R_WIDE |
		 FIMC_REG_CISCCTRL_SCALEUP_H | FIMC_REG_CISCCTRL_SCALEUP_V |
		 FIMC_REG_CISCCTRL_SCALERBYPASS | FIMC_REG_CISCCTRL_ONE2ONE |
		 FIMC_REG_CISCCTRL_INRGB_FMT_MASK | FIMC_REG_CISCCTRL_OUTRGB_FMT_MASK |
		 FIMC_REG_CISCCTRL_INTERLACE | FIMC_REG_CISCCTRL_RGB_EXT);

	if (!(ctx->flags & FIMC_COLOR_RANGE_NARROW))
		cfg |= (FIMC_REG_CISCCTRL_CSCR2Y_WIDE |
			FIMC_REG_CISCCTRL_CSCY2R_WIDE);

	if (!sc->enabled)
		cfg |= FIMC_REG_CISCCTRL_SCALERBYPASS;

	if (sc->scaleup_h)
		cfg |= FIMC_REG_CISCCTRL_SCALEUP_H;

	if (sc->scaleup_v)
		cfg |= FIMC_REG_CISCCTRL_SCALEUP_V;

	if (sc->copy_mode)
		cfg |= FIMC_REG_CISCCTRL_ONE2ONE;

	if (ctx->in_path == FIMC_IO_DMA) {
		switch (src_frame->fmt->color) {
		case FIMC_FMT_RGB565:
			cfg |= FIMC_REG_CISCCTRL_INRGB_FMT_RGB565;
			break;
		case FIMC_FMT_RGB666:
			cfg |= FIMC_REG_CISCCTRL_INRGB_FMT_RGB666;
			break;
		case FIMC_FMT_RGB888:
			cfg |= FIMC_REG_CISCCTRL_INRGB_FMT_RGB888;
			break;
		}
	}

	if (ctx->out_path == FIMC_IO_DMA) {
		u32 color = dst_frame->fmt->color;

		if (color >= FIMC_FMT_RGB444 && color <= FIMC_FMT_RGB565)
			cfg |= FIMC_REG_CISCCTRL_OUTRGB_FMT_RGB565;
		else if (color == FIMC_FMT_RGB666)
			cfg |= FIMC_REG_CISCCTRL_OUTRGB_FMT_RGB666;
		else if (color == FIMC_FMT_RGB888)
			cfg |= FIMC_REG_CISCCTRL_OUTRGB_FMT_RGB888;
	} else {
		cfg |= FIMC_REG_CISCCTRL_OUTRGB_FMT_RGB888;

		if (ctx->flags & FIMC_SCAN_MODE_INTERLACED)
			cfg |= FIMC_REG_CISCCTRL_INTERLACE;
	}

	writel(cfg, dev->regs + FIMC_REG_CISCCTRL);
}

void fimc_hw_set_mainscaler(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	const struct fimc_variant *variant = dev->variant;
	struct fimc_scaler *sc = &ctx->scaler;
	u32 cfg;

	dbg("main_hratio= 0x%X  main_vratio= 0x%X",
	    sc->main_hratio, sc->main_vratio);

	fimc_hw_set_scaler(ctx);

	cfg = readl(dev->regs + FIMC_REG_CISCCTRL);
	cfg &= ~(FIMC_REG_CISCCTRL_MHRATIO_MASK |
		 FIMC_REG_CISCCTRL_MVRATIO_MASK);

	if (variant->has_mainscaler_ext) {
		cfg |= FIMC_REG_CISCCTRL_MHRATIO_EXT(sc->main_hratio);
		cfg |= FIMC_REG_CISCCTRL_MVRATIO_EXT(sc->main_vratio);
		writel(cfg, dev->regs + FIMC_REG_CISCCTRL);

		cfg = readl(dev->regs + FIMC_REG_CIEXTEN);

		cfg &= ~(FIMC_REG_CIEXTEN_MVRATIO_EXT_MASK |
			 FIMC_REG_CIEXTEN_MHRATIO_EXT_MASK);
		cfg |= FIMC_REG_CIEXTEN_MHRATIO_EXT(sc->main_hratio);
		cfg |= FIMC_REG_CIEXTEN_MVRATIO_EXT(sc->main_vratio);
		writel(cfg, dev->regs + FIMC_REG_CIEXTEN);
	} else {
		cfg |= FIMC_REG_CISCCTRL_MHRATIO(sc->main_hratio);
		cfg |= FIMC_REG_CISCCTRL_MVRATIO(sc->main_vratio);
		writel(cfg, dev->regs + FIMC_REG_CISCCTRL);
	}
}

void fimc_hw_enable_capture(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	u32 cfg;

	cfg = readl(dev->regs + FIMC_REG_CIIMGCPT);
	cfg |= FIMC_REG_CIIMGCPT_CPT_FREN_ENABLE;

	if (ctx->scaler.enabled)
		cfg |= FIMC_REG_CIIMGCPT_IMGCPTEN_SC;
	else
		cfg &= FIMC_REG_CIIMGCPT_IMGCPTEN_SC;

	cfg |= FIMC_REG_CIIMGCPT_IMGCPTEN;
	writel(cfg, dev->regs + FIMC_REG_CIIMGCPT);
}

void fimc_hw_disable_capture(struct fimc_dev *dev)
{
	u32 cfg = readl(dev->regs + FIMC_REG_CIIMGCPT);
	cfg &= ~(FIMC_REG_CIIMGCPT_IMGCPTEN |
		 FIMC_REG_CIIMGCPT_IMGCPTEN_SC);
	writel(cfg, dev->regs + FIMC_REG_CIIMGCPT);
}

void fimc_hw_set_effect(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_effect *effect = &ctx->effect;
	u32 cfg = 0;

	if (effect->type != FIMC_REG_CIIMGEFF_FIN_BYPASS) {
		cfg |= FIMC_REG_CIIMGEFF_IE_SC_AFTER |
			FIMC_REG_CIIMGEFF_IE_ENABLE;
		cfg |= effect->type;
		if (effect->type == FIMC_REG_CIIMGEFF_FIN_ARBITRARY)
			cfg |= (effect->pat_cb << 13) | effect->pat_cr;
	}

	writel(cfg, dev->regs + FIMC_REG_CIIMGEFF);
}

void fimc_hw_set_rgb_alpha(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->d_frame;
	u32 cfg;

	if (!(frame->fmt->flags & FMT_HAS_ALPHA))
		return;

	cfg = readl(dev->regs + FIMC_REG_CIOCTRL);
	cfg &= ~FIMC_REG_CIOCTRL_ALPHA_OUT_MASK;
	cfg |= (frame->alpha << 4);
	writel(cfg, dev->regs + FIMC_REG_CIOCTRL);
}

static void fimc_hw_set_in_dma_size(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->s_frame;
	u32 cfg_o = 0;
	u32 cfg_r = 0;

	if (FIMC_IO_LCDFIFO == ctx->out_path)
		cfg_r |= FIMC_REG_CIREAL_ISIZE_AUTOLOAD_EN;

	cfg_o |= (frame->f_height << 16) | frame->f_width;
	cfg_r |= (frame->height << 16) | frame->width;

	writel(cfg_o, dev->regs + FIMC_REG_ORGISIZE);
	writel(cfg_r, dev->regs + FIMC_REG_CIREAL_ISIZE);
}

void fimc_hw_set_in_dma(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->s_frame;
	struct fimc_dma_offset *offset = &frame->dma_offset;
	u32 cfg;

	/* Set the pixel offsets. */
	cfg = (offset->y_v << 16) | offset->y_h;
	writel(cfg, dev->regs + FIMC_REG_CIIYOFF);

	cfg = (offset->cb_v << 16) | offset->cb_h;
	writel(cfg, dev->regs + FIMC_REG_CIICBOFF);

	cfg = (offset->cr_v << 16) | offset->cr_h;
	writel(cfg, dev->regs + FIMC_REG_CIICROFF);

	/* Input original and real size. */
	fimc_hw_set_in_dma_size(ctx);

	/* Use DMA autoload only in FIFO mode. */
	fimc_hw_en_autoload(dev, ctx->out_path == FIMC_IO_LCDFIFO);

	/* Set the input DMA to process single frame only. */
	cfg = readl(dev->regs + FIMC_REG_MSCTRL);
	cfg &= ~(FIMC_REG_MSCTRL_INFORMAT_MASK
		 | FIMC_REG_MSCTRL_IN_BURST_COUNT_MASK
		 | FIMC_REG_MSCTRL_INPUT_MASK
		 | FIMC_REG_MSCTRL_C_INT_IN_MASK
		 | FIMC_REG_MSCTRL_2P_IN_ORDER_MASK
		 | FIMC_REG_MSCTRL_ORDER422_MASK);

	cfg |= (FIMC_REG_MSCTRL_IN_BURST_COUNT(4)
		| FIMC_REG_MSCTRL_INPUT_MEMORY
		| FIMC_REG_MSCTRL_FIFO_CTRL_FULL);

	switch (frame->fmt->color) {
	case FIMC_FMT_RGB565...FIMC_FMT_RGB888:
		cfg |= FIMC_REG_MSCTRL_INFORMAT_RGB;
		break;
	case FIMC_FMT_YCBCR420:
		cfg |= FIMC_REG_MSCTRL_INFORMAT_YCBCR420;

		if (frame->fmt->colplanes == 2)
			cfg |= ctx->in_order_2p | FIMC_REG_MSCTRL_C_INT_IN_2PLANE;
		else
			cfg |= FIMC_REG_MSCTRL_C_INT_IN_3PLANE;

		break;
	case FIMC_FMT_YCBYCR422...FIMC_FMT_CRYCBY422:
		if (frame->fmt->colplanes == 1) {
			cfg |= ctx->in_order_1p
				| FIMC_REG_MSCTRL_INFORMAT_YCBCR422_1P;
		} else {
			cfg |= FIMC_REG_MSCTRL_INFORMAT_YCBCR422;

			if (frame->fmt->colplanes == 2)
				cfg |= ctx->in_order_2p
					| FIMC_REG_MSCTRL_C_INT_IN_2PLANE;
			else
				cfg |= FIMC_REG_MSCTRL_C_INT_IN_3PLANE;
		}
		break;
	default:
		break;
	}

	writel(cfg, dev->regs + FIMC_REG_MSCTRL);

	/* Input/output DMA linear/tiled mode. */
	cfg = readl(dev->regs + FIMC_REG_CIDMAPARAM);
	cfg &= ~FIMC_REG_CIDMAPARAM_TILE_MASK;

	if (tiled_fmt(ctx->s_frame.fmt))
		cfg |= FIMC_REG_CIDMAPARAM_R_64X32;

	if (tiled_fmt(ctx->d_frame.fmt))
		cfg |= FIMC_REG_CIDMAPARAM_W_64X32;

	writel(cfg, dev->regs + FIMC_REG_CIDMAPARAM);
}


void fimc_hw_set_input_path(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;

	u32 cfg = readl(dev->regs + FIMC_REG_MSCTRL);
	cfg &= ~FIMC_REG_MSCTRL_INPUT_MASK;

	if (ctx->in_path == FIMC_IO_DMA)
		cfg |= FIMC_REG_MSCTRL_INPUT_MEMORY;
	else
		cfg |= FIMC_REG_MSCTRL_INPUT_EXTCAM;

	writel(cfg, dev->regs + FIMC_REG_MSCTRL);
}

void fimc_hw_set_output_path(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;

	u32 cfg = readl(dev->regs + FIMC_REG_CISCCTRL);
	cfg &= ~FIMC_REG_CISCCTRL_LCDPATHEN_FIFO;
	if (ctx->out_path == FIMC_IO_LCDFIFO)
		cfg |= FIMC_REG_CISCCTRL_LCDPATHEN_FIFO;
	writel(cfg, dev->regs + FIMC_REG_CISCCTRL);
}

void fimc_hw_set_input_addr(struct fimc_dev *dev, struct fimc_addr *paddr)
{
	u32 cfg = readl(dev->regs + FIMC_REG_CIREAL_ISIZE);
	cfg |= FIMC_REG_CIREAL_ISIZE_ADDR_CH_DIS;
	writel(cfg, dev->regs + FIMC_REG_CIREAL_ISIZE);

	writel(paddr->y, dev->regs + FIMC_REG_CIIYSA(0));
	writel(paddr->cb, dev->regs + FIMC_REG_CIICBSA(0));
	writel(paddr->cr, dev->regs + FIMC_REG_CIICRSA(0));

	cfg &= ~FIMC_REG_CIREAL_ISIZE_ADDR_CH_DIS;
	writel(cfg, dev->regs + FIMC_REG_CIREAL_ISIZE);
}

void fimc_hw_set_output_addr(struct fimc_dev *dev,
			     struct fimc_addr *paddr, int index)
{
	int i = (index == -1) ? 0 : index;
	do {
		writel(paddr->y, dev->regs + FIMC_REG_CIOYSA(i));
		writel(paddr->cb, dev->regs + FIMC_REG_CIOCBSA(i));
		writel(paddr->cr, dev->regs + FIMC_REG_CIOCRSA(i));
		dbg("dst_buf[%d]: 0x%X, cb: 0x%X, cr: 0x%X",
		    i, paddr->y, paddr->cb, paddr->cr);
	} while (index == -1 && ++i < FIMC_MAX_OUT_BUFS);
}

int fimc_hw_set_camera_polarity(struct fimc_dev *fimc,
				struct fimc_source_info *cam)
{
	u32 cfg = readl(fimc->regs + FIMC_REG_CIGCTRL);

	cfg &= ~(FIMC_REG_CIGCTRL_INVPOLPCLK | FIMC_REG_CIGCTRL_INVPOLVSYNC |
		 FIMC_REG_CIGCTRL_INVPOLHREF | FIMC_REG_CIGCTRL_INVPOLHSYNC |
		 FIMC_REG_CIGCTRL_INVPOLFIELD);

	if (cam->flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
		cfg |= FIMC_REG_CIGCTRL_INVPOLPCLK;

	if (cam->flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
		cfg |= FIMC_REG_CIGCTRL_INVPOLVSYNC;

	if (cam->flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
		cfg |= FIMC_REG_CIGCTRL_INVPOLHREF;

	if (cam->flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
		cfg |= FIMC_REG_CIGCTRL_INVPOLHSYNC;

	if (cam->flags & V4L2_MBUS_FIELD_EVEN_LOW)
		cfg |= FIMC_REG_CIGCTRL_INVPOLFIELD;

	writel(cfg, fimc->regs + FIMC_REG_CIGCTRL);

	return 0;
}

struct mbus_pixfmt_desc {
	u32 pixelcode;
	u32 cisrcfmt;
	u16 bus_width;
};

static const struct mbus_pixfmt_desc pix_desc[] = {
	{ MEDIA_BUS_FMT_YUYV8_2X8, FIMC_REG_CISRCFMT_ORDER422_YCBYCR, 8 },
	{ MEDIA_BUS_FMT_YVYU8_2X8, FIMC_REG_CISRCFMT_ORDER422_YCRYCB, 8 },
	{ MEDIA_BUS_FMT_VYUY8_2X8, FIMC_REG_CISRCFMT_ORDER422_CRYCBY, 8 },
	{ MEDIA_BUS_FMT_UYVY8_2X8, FIMC_REG_CISRCFMT_ORDER422_CBYCRY, 8 },
};

int fimc_hw_set_camera_source(struct fimc_dev *fimc,
			      struct fimc_source_info *source)
{
	struct fimc_vid_cap *vc = &fimc->vid_cap;
	struct fimc_frame *f = &vc->ctx->s_frame;
	u32 bus_width, cfg = 0;
	int i;

	switch (source->fimc_bus_type) {
	case FIMC_BUS_TYPE_ITU_601:
	case FIMC_BUS_TYPE_ITU_656:
		for (i = 0; i < ARRAY_SIZE(pix_desc); i++) {
			if (vc->ci_fmt.code == pix_desc[i].pixelcode) {
				cfg = pix_desc[i].cisrcfmt;
				bus_width = pix_desc[i].bus_width;
				break;
			}
		}

		if (i == ARRAY_SIZE(pix_desc)) {
			v4l2_err(&vc->ve.vdev,
				 "Camera color format not supported: %d\n",
				 vc->ci_fmt.code);
			return -EINVAL;
		}

		if (source->fimc_bus_type == FIMC_BUS_TYPE_ITU_601) {
			if (bus_width == 8)
				cfg |= FIMC_REG_CISRCFMT_ITU601_8BIT;
			else if (bus_width == 16)
				cfg |= FIMC_REG_CISRCFMT_ITU601_16BIT;
		} /* else defaults to ITU-R BT.656 8-bit */
		break;
	case FIMC_BUS_TYPE_MIPI_CSI2:
		if (fimc_fmt_is_user_defined(f->fmt->color))
			cfg |= FIMC_REG_CISRCFMT_ITU601_8BIT;
		break;
	default:
	case FIMC_BUS_TYPE_ISP_WRITEBACK:
		/* Anything to do here ? */
		break;
	}

	cfg |= (f->o_width << 16) | f->o_height;
	writel(cfg, fimc->regs + FIMC_REG_CISRCFMT);
	return 0;
}

void fimc_hw_set_camera_offset(struct fimc_dev *fimc, struct fimc_frame *f)
{
	u32 hoff2, voff2;

	u32 cfg = readl(fimc->regs + FIMC_REG_CIWDOFST);

	cfg &= ~(FIMC_REG_CIWDOFST_HOROFF_MASK | FIMC_REG_CIWDOFST_VEROFF_MASK);
	cfg |=  FIMC_REG_CIWDOFST_OFF_EN |
		(f->offs_h << 16) | f->offs_v;

	writel(cfg, fimc->regs + FIMC_REG_CIWDOFST);

	/* See CIWDOFSTn register description in the datasheet for details. */
	hoff2 = f->o_width - f->width - f->offs_h;
	voff2 = f->o_height - f->height - f->offs_v;
	cfg = (hoff2 << 16) | voff2;
	writel(cfg, fimc->regs + FIMC_REG_CIWDOFST2);
}

int fimc_hw_set_camera_type(struct fimc_dev *fimc,
			    struct fimc_source_info *source)
{
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;
	u32 csis_data_alignment = 32;
	u32 cfg, tmp;

	cfg = readl(fimc->regs + FIMC_REG_CIGCTRL);

	/* Select ITU B interface, disable Writeback path and test pattern. */
	cfg &= ~(FIMC_REG_CIGCTRL_TESTPAT_MASK | FIMC_REG_CIGCTRL_SELCAM_ITU_A |
		FIMC_REG_CIGCTRL_SELCAM_MIPI | FIMC_REG_CIGCTRL_CAMIF_SELWB |
		FIMC_REG_CIGCTRL_SELCAM_MIPI_A | FIMC_REG_CIGCTRL_CAM_JPEG |
		FIMC_REG_CIGCTRL_SELWB_A);

	switch (source->fimc_bus_type) {
	case FIMC_BUS_TYPE_MIPI_CSI2:
		cfg |= FIMC_REG_CIGCTRL_SELCAM_MIPI;

		if (source->mux_id == 0)
			cfg |= FIMC_REG_CIGCTRL_SELCAM_MIPI_A;

		/* TODO: add remaining supported formats. */
		switch (vid_cap->ci_fmt.code) {
		case MEDIA_BUS_FMT_VYUY8_2X8:
			tmp = FIMC_REG_CSIIMGFMT_YCBCR422_8BIT;
			break;
		case MEDIA_BUS_FMT_JPEG_1X8:
		case MEDIA_BUS_FMT_S5C_UYVY_JPEG_1X8:
			tmp = FIMC_REG_CSIIMGFMT_USER(1);
			cfg |= FIMC_REG_CIGCTRL_CAM_JPEG;
			break;
		default:
			v4l2_err(&vid_cap->ve.vdev,
				 "Not supported camera pixel format: %#x\n",
				 vid_cap->ci_fmt.code);
			return -EINVAL;
		}
		tmp |= (csis_data_alignment == 32) << 8;

		writel(tmp, fimc->regs + FIMC_REG_CSIIMGFMT);
		break;
	case FIMC_BUS_TYPE_ITU_601...FIMC_BUS_TYPE_ITU_656:
		if (source->mux_id == 0) /* ITU-A, ITU-B: 0, 1 */
			cfg |= FIMC_REG_CIGCTRL_SELCAM_ITU_A;
		break;
	case FIMC_BUS_TYPE_LCD_WRITEBACK_A:
		cfg |= FIMC_REG_CIGCTRL_CAMIF_SELWB;
		/* fall through */
	case FIMC_BUS_TYPE_ISP_WRITEBACK:
		if (fimc->variant->has_isp_wb)
			cfg |= FIMC_REG_CIGCTRL_CAMIF_SELWB;
		else
			WARN_ONCE(1, "ISP Writeback input is not supported\n");
		break;
	default:
		v4l2_err(&vid_cap->ve.vdev,
			 "Invalid FIMC bus type selected: %d\n",
			 source->fimc_bus_type);
		return -EINVAL;
	}
	writel(cfg, fimc->regs + FIMC_REG_CIGCTRL);

	return 0;
}

void fimc_hw_clear_irq(struct fimc_dev *dev)
{
	u32 cfg = readl(dev->regs + FIMC_REG_CIGCTRL);
	cfg |= FIMC_REG_CIGCTRL_IRQ_CLR;
	writel(cfg, dev->regs + FIMC_REG_CIGCTRL);
}

void fimc_hw_enable_scaler(struct fimc_dev *dev, bool on)
{
	u32 cfg = readl(dev->regs + FIMC_REG_CISCCTRL);
	if (on)
		cfg |= FIMC_REG_CISCCTRL_SCALERSTART;
	else
		cfg &= ~FIMC_REG_CISCCTRL_SCALERSTART;
	writel(cfg, dev->regs + FIMC_REG_CISCCTRL);
}

void fimc_hw_activate_input_dma(struct fimc_dev *dev, bool on)
{
	u32 cfg = readl(dev->regs + FIMC_REG_MSCTRL);
	if (on)
		cfg |= FIMC_REG_MSCTRL_ENVID;
	else
		cfg &= ~FIMC_REG_MSCTRL_ENVID;
	writel(cfg, dev->regs + FIMC_REG_MSCTRL);
}

/* Return an index to the buffer actually being written. */
s32 fimc_hw_get_frame_index(struct fimc_dev *dev)
{
	s32 reg;

	if (dev->drv_data->cistatus2) {
		reg = readl(dev->regs + FIMC_REG_CISTATUS2) & 0x3f;
		return reg - 1;
	}

	reg = readl(dev->regs + FIMC_REG_CISTATUS);

	return (reg & FIMC_REG_CISTATUS_FRAMECNT_MASK) >>
		FIMC_REG_CISTATUS_FRAMECNT_SHIFT;
}

/* Return an index to the buffer being written previously. */
s32 fimc_hw_get_prev_frame_index(struct fimc_dev *dev)
{
	s32 reg;

	if (!dev->drv_data->cistatus2)
		return -1;

	reg = readl(dev->regs + FIMC_REG_CISTATUS2);
	return ((reg >> 7) & 0x3f) - 1;
}

/* Locking: the caller holds fimc->slock */
void fimc_activate_capture(struct fimc_ctx *ctx)
{
	fimc_hw_enable_scaler(ctx->fimc_dev, ctx->scaler.enabled);
	fimc_hw_enable_capture(ctx);
}

void fimc_deactivate_capture(struct fimc_dev *fimc)
{
	fimc_hw_en_lastirq(fimc, true);
	fimc_hw_disable_capture(fimc);
	fimc_hw_enable_scaler(fimc, false);
	fimc_hw_en_lastirq(fimc, false);
}

int fimc_hw_camblk_cfg_writeback(struct fimc_dev *fimc)
{
	struct regmap *map = fimc->sysreg;
	unsigned int mask, val, camblk_cfg;
	int ret;

	if (map == NULL)
		return 0;

	ret = regmap_read(map, SYSREG_CAMBLK, &camblk_cfg);
	if (ret < 0 || ((camblk_cfg & 0x00700000) >> 20 != 0x3))
		return ret;

	if (!WARN(fimc->id >= 3, "not supported id: %d\n", fimc->id))
		val = 0x1 << (fimc->id + 20);
	else
		val = 0;

	mask = SYSREG_CAMBLK_FIFORST_ISP | SYSREG_CAMBLK_ISPWB_FULL_EN;
	ret = regmap_update_bits(map, SYSREG_CAMBLK, mask, val);
	if (ret < 0)
		return ret;

	usleep_range(1000, 2000);

	val |= SYSREG_CAMBLK_FIFORST_ISP;
	ret = regmap_update_bits(map, SYSREG_CAMBLK, mask, val);
	if (ret < 0)
		return ret;

	mask = SYSREG_ISPBLK_FIFORST_CAM_BLK;
	ret = regmap_update_bits(map, SYSREG_ISPBLK, mask, ~mask);
	if (ret < 0)
		return ret;

	usleep_range(1000, 2000);

	return regmap_update_bits(map, SYSREG_ISPBLK, mask, mask);
}
