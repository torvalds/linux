/*
 * Register interface file for Samsung Camera Interface (FIMC) driver
 *
 * Copyright (c) 2010 Samsung Electronics
 *
 * Sylwester Nawrocki, s.nawrocki@samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>
#include <linux/delay.h>
#include <mach/map.h>

#include "fimc-core.h"


void fimc_hw_reset(struct fimc_dev *dev)
{
	u32 cfg;

	cfg = readl(dev->regs + S5P_CISRCFMT);
	cfg |= S5P_CISRCFMT_ITU601_8BIT;
	writel(cfg, dev->regs + S5P_CISRCFMT);

	/* Software reset. */
	cfg = readl(dev->regs + S5P_CIGCTRL);
	cfg |= (S5P_CIGCTRL_SWRST | S5P_CIGCTRL_IRQ_LEVEL);
	writel(cfg, dev->regs + S5P_CIGCTRL);
	msleep(1);

	cfg = readl(dev->regs + S5P_CIGCTRL);
	cfg &= ~S5P_CIGCTRL_SWRST;
	writel(cfg, dev->regs + S5P_CIGCTRL);

}

void fimc_hw_set_rotation(struct fimc_ctx *ctx)
{
	u32 cfg, flip;
	struct fimc_dev *dev = ctx->fimc_dev;

	cfg = readl(dev->regs + S5P_CITRGFMT);
	cfg &= ~(S5P_CITRGFMT_INROT90 | S5P_CITRGFMT_OUTROT90);

	flip = readl(dev->regs + S5P_MSCTRL);
	flip &= ~S5P_MSCTRL_FLIP_MASK;

	/*
	 * The input and output rotator cannot work simultaneously.
	 * Use the output rotator in output DMA mode or the input rotator
	 * in direct fifo output mode.
	 */
	if (ctx->rotation == 90 || ctx->rotation == 270) {
		if (ctx->out_path == FIMC_LCDFIFO) {
			cfg |= S5P_CITRGFMT_INROT90;
			if (ctx->rotation == 270)
				flip |= S5P_MSCTRL_FLIP_180;
		} else {
			cfg |= S5P_CITRGFMT_OUTROT90;
			if (ctx->rotation == 270)
				cfg |= S5P_CITRGFMT_FLIP_180;
		}
	} else if (ctx->rotation == 180) {
		if (ctx->out_path == FIMC_LCDFIFO)
			flip |= S5P_MSCTRL_FLIP_180;
		else
			cfg |= S5P_CITRGFMT_FLIP_180;
	}
	if (ctx->rotation == 180 || ctx->rotation == 270)
		writel(flip, dev->regs + S5P_MSCTRL);
	writel(cfg, dev->regs + S5P_CITRGFMT);
}

static u32 fimc_hw_get_in_flip(u32 ctx_flip)
{
	u32 flip = S5P_MSCTRL_FLIP_NORMAL;

	switch (ctx_flip) {
	case FLIP_X_AXIS:
		flip = S5P_MSCTRL_FLIP_X_MIRROR;
		break;
	case FLIP_Y_AXIS:
		flip = S5P_MSCTRL_FLIP_Y_MIRROR;
		break;
	case FLIP_XY_AXIS:
		flip = S5P_MSCTRL_FLIP_180;
		break;
	}

	return flip;
}

static u32 fimc_hw_get_target_flip(u32 ctx_flip)
{
	u32 flip = S5P_CITRGFMT_FLIP_NORMAL;

	switch (ctx_flip) {
	case FLIP_X_AXIS:
		flip = S5P_CITRGFMT_FLIP_X_MIRROR;
		break;
	case FLIP_Y_AXIS:
		flip = S5P_CITRGFMT_FLIP_Y_MIRROR;
		break;
	case FLIP_XY_AXIS:
		flip = S5P_CITRGFMT_FLIP_180;
		break;
	case FLIP_NONE:
		break;

	}
	return flip;
}

void fimc_hw_set_target_format(struct fimc_ctx *ctx)
{
	u32 cfg;
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->d_frame;

	dbg("w= %d, h= %d color: %d", frame->width,
		frame->height, frame->fmt->color);

	cfg = readl(dev->regs + S5P_CITRGFMT);
	cfg &= ~(S5P_CITRGFMT_FMT_MASK | S5P_CITRGFMT_HSIZE_MASK |
		  S5P_CITRGFMT_VSIZE_MASK);

	switch (frame->fmt->color) {
	case S5P_FIMC_RGB565:
	case S5P_FIMC_RGB666:
	case S5P_FIMC_RGB888:
		cfg |= S5P_CITRGFMT_RGB;
		break;
	case S5P_FIMC_YCBCR420:
		cfg |= S5P_CITRGFMT_YCBCR420;
		break;
	case S5P_FIMC_YCBYCR422:
	case S5P_FIMC_YCRYCB422:
	case S5P_FIMC_CBYCRY422:
	case S5P_FIMC_CRYCBY422:
		if (frame->fmt->planes_cnt == 1)
			cfg |= S5P_CITRGFMT_YCBCR422_1P;
		else
			cfg |= S5P_CITRGFMT_YCBCR422;
		break;
	default:
		break;
	}

	cfg |= S5P_CITRGFMT_HSIZE(frame->width);
	cfg |= S5P_CITRGFMT_VSIZE(frame->height);

	if (ctx->rotation == 0) {
		cfg &= ~S5P_CITRGFMT_FLIP_MASK;
		cfg |= fimc_hw_get_target_flip(ctx->flip);
	}
	writel(cfg, dev->regs + S5P_CITRGFMT);

	cfg = readl(dev->regs + S5P_CITAREA) & ~S5P_CITAREA_MASK;
	cfg |= (frame->width * frame->height);
	writel(cfg, dev->regs + S5P_CITAREA);
}

static void fimc_hw_set_out_dma_size(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->d_frame;
	u32 cfg = 0;

	if (ctx->rotation == 90 || ctx->rotation == 270) {
		cfg |= S5P_ORIG_SIZE_HOR(frame->f_height);
		cfg |= S5P_ORIG_SIZE_VER(frame->f_width);
	} else {
		cfg |= S5P_ORIG_SIZE_HOR(frame->f_width);
		cfg |= S5P_ORIG_SIZE_VER(frame->f_height);
	}
	writel(cfg, dev->regs + S5P_ORGOSIZE);
}

void fimc_hw_set_out_dma(struct fimc_ctx *ctx)
{
	u32 cfg;
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->d_frame;
	struct fimc_dma_offset *offset = &frame->dma_offset;

	/* Set the input dma offsets. */
	cfg = 0;
	cfg |= S5P_CIO_OFFS_HOR(offset->y_h);
	cfg |= S5P_CIO_OFFS_VER(offset->y_v);
	writel(cfg, dev->regs + S5P_CIOYOFF);

	cfg = 0;
	cfg |= S5P_CIO_OFFS_HOR(offset->cb_h);
	cfg |= S5P_CIO_OFFS_VER(offset->cb_v);
	writel(cfg, dev->regs + S5P_CIOCBOFF);

	cfg = 0;
	cfg |= S5P_CIO_OFFS_HOR(offset->cr_h);
	cfg |= S5P_CIO_OFFS_VER(offset->cr_v);
	writel(cfg, dev->regs + S5P_CIOCROFF);

	fimc_hw_set_out_dma_size(ctx);

	/* Configure chroma components order. */
	cfg = readl(dev->regs + S5P_CIOCTRL);

	cfg &= ~(S5P_CIOCTRL_ORDER2P_MASK | S5P_CIOCTRL_ORDER422_MASK |
		 S5P_CIOCTRL_YCBCR_PLANE_MASK);

	if (frame->fmt->planes_cnt == 1)
		cfg |= ctx->out_order_1p;
	else if (frame->fmt->planes_cnt == 2)
		cfg |= ctx->out_order_2p | S5P_CIOCTRL_YCBCR_2PLANE;
	else if (frame->fmt->planes_cnt == 3)
		cfg |= S5P_CIOCTRL_YCBCR_3PLANE;

	writel(cfg, dev->regs + S5P_CIOCTRL);
}

static void fimc_hw_en_autoload(struct fimc_dev *dev, int enable)
{
	u32 cfg = readl(dev->regs + S5P_ORGISIZE);
	if (enable)
		cfg |= S5P_CIREAL_ISIZE_AUTOLOAD_EN;
	else
		cfg &= ~S5P_CIREAL_ISIZE_AUTOLOAD_EN;
	writel(cfg, dev->regs + S5P_ORGISIZE);
}

void fimc_hw_en_lastirq(struct fimc_dev *dev, int enable)
{
	unsigned long flags;
	u32 cfg;

	spin_lock_irqsave(&dev->slock, flags);

	cfg = readl(dev->regs + S5P_CIOCTRL);
	if (enable)
		cfg |= S5P_CIOCTRL_LASTIRQ_ENABLE;
	else
		cfg &= ~S5P_CIOCTRL_LASTIRQ_ENABLE;
	writel(cfg, dev->regs + S5P_CIOCTRL);

	spin_unlock_irqrestore(&dev->slock, flags);
}

void fimc_hw_set_prescaler(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev =  ctx->fimc_dev;
	struct fimc_scaler *sc = &ctx->scaler;
	u32 cfg = 0, shfactor;

	shfactor = 10 - (sc->hfactor + sc->vfactor);

	cfg |= S5P_CISCPRERATIO_SHFACTOR(shfactor);
	cfg |= S5P_CISCPRERATIO_HOR(sc->pre_hratio);
	cfg |= S5P_CISCPRERATIO_VER(sc->pre_vratio);
	writel(cfg, dev->regs + S5P_CISCPRERATIO);

	cfg = 0;
	cfg |= S5P_CISCPREDST_WIDTH(sc->pre_dst_width);
	cfg |= S5P_CISCPREDST_HEIGHT(sc->pre_dst_height);
	writel(cfg, dev->regs + S5P_CISCPREDST);
}

void fimc_hw_set_scaler(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_scaler *sc = &ctx->scaler;
	struct fimc_frame *src_frame = &ctx->s_frame;
	struct fimc_frame *dst_frame = &ctx->d_frame;
	u32 cfg = 0;

	if (!(ctx->flags & FIMC_COLOR_RANGE_NARROW))
		cfg |= (S5P_CISCCTRL_CSCR2Y_WIDE | S5P_CISCCTRL_CSCY2R_WIDE);

	if (!sc->enabled)
		cfg |= S5P_CISCCTRL_SCALERBYPASS;

	if (sc->scaleup_h)
		cfg |= S5P_CISCCTRL_SCALEUP_H;

	if (sc->scaleup_v)
		cfg |= S5P_CISCCTRL_SCALEUP_V;

	if (sc->copy_mode)
		cfg |= S5P_CISCCTRL_ONE2ONE;


	if (ctx->in_path == FIMC_DMA) {
		if (src_frame->fmt->color == S5P_FIMC_RGB565)
			cfg |= S5P_CISCCTRL_INRGB_FMT_RGB565;
		else if (src_frame->fmt->color == S5P_FIMC_RGB666)
			cfg |= S5P_CISCCTRL_INRGB_FMT_RGB666;
		else if (src_frame->fmt->color == S5P_FIMC_RGB888)
			cfg |= S5P_CISCCTRL_INRGB_FMT_RGB888;
	}

	if (ctx->out_path == FIMC_DMA) {
		if (dst_frame->fmt->color == S5P_FIMC_RGB565)
			cfg |= S5P_CISCCTRL_OUTRGB_FMT_RGB565;
		else if (dst_frame->fmt->color == S5P_FIMC_RGB666)
			cfg |= S5P_CISCCTRL_OUTRGB_FMT_RGB666;
		else if (dst_frame->fmt->color == S5P_FIMC_RGB888)
			cfg |= S5P_CISCCTRL_OUTRGB_FMT_RGB888;
	} else {
		cfg |= S5P_CISCCTRL_OUTRGB_FMT_RGB888;

		if (ctx->flags & FIMC_SCAN_MODE_INTERLACED)
			cfg |= S5P_CISCCTRL_INTERLACE;
	}

	dbg("main_hratio= 0x%X  main_vratio= 0x%X",
		sc->main_hratio, sc->main_vratio);

	cfg |= S5P_CISCCTRL_SC_HORRATIO(sc->main_hratio);
	cfg |= S5P_CISCCTRL_SC_VERRATIO(sc->main_vratio);

	writel(cfg, dev->regs + S5P_CISCCTRL);
}

void fimc_hw_en_capture(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	u32 cfg;

	cfg = readl(dev->regs + S5P_CIIMGCPT);
	/* One shot mode for output DMA or freerun for FIFO. */
	if (ctx->out_path == FIMC_DMA)
		cfg |= S5P_CIIMGCPT_CPT_FREN_ENABLE;
	else
		cfg &= ~S5P_CIIMGCPT_CPT_FREN_ENABLE;

	if (ctx->scaler.enabled)
		cfg |= S5P_CIIMGCPT_IMGCPTEN_SC;

	writel(cfg | S5P_CIIMGCPT_IMGCPTEN, dev->regs + S5P_CIIMGCPT);
}

void fimc_hw_set_effect(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_effect *effect = &ctx->effect;
	u32 cfg = (S5P_CIIMGEFF_IE_ENABLE | S5P_CIIMGEFF_IE_SC_AFTER);

	cfg |= effect->type;

	if (effect->type == S5P_FIMC_EFFECT_ARBITRARY) {
		cfg |= S5P_CIIMGEFF_PAT_CB(effect->pat_cb);
		cfg |= S5P_CIIMGEFF_PAT_CR(effect->pat_cr);
	}

	writel(cfg, dev->regs + S5P_CIIMGEFF);
}

static void fimc_hw_set_in_dma_size(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->s_frame;
	u32 cfg_o = 0;
	u32 cfg_r = 0;

	if (FIMC_LCDFIFO == ctx->out_path)
		cfg_r |=  S5P_CIREAL_ISIZE_AUTOLOAD_EN;

	cfg_o |= S5P_ORIG_SIZE_HOR(frame->f_width);
	cfg_o |= S5P_ORIG_SIZE_VER(frame->f_height);
	cfg_r |= S5P_CIREAL_ISIZE_WIDTH(frame->width);
	cfg_r |= S5P_CIREAL_ISIZE_HEIGHT(frame->height);

	writel(cfg_o, dev->regs + S5P_ORGISIZE);
	writel(cfg_r, dev->regs + S5P_CIREAL_ISIZE);
}

void fimc_hw_set_in_dma(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->s_frame;
	struct fimc_dma_offset *offset = &frame->dma_offset;
	u32 cfg = 0;

	/* Set the pixel offsets. */
	cfg |= S5P_CIO_OFFS_HOR(offset->y_h);
	cfg |= S5P_CIO_OFFS_VER(offset->y_v);
	writel(cfg, dev->regs + S5P_CIIYOFF);

	cfg = 0;
	cfg |= S5P_CIO_OFFS_HOR(offset->cb_h);
	cfg |= S5P_CIO_OFFS_VER(offset->cb_v);
	writel(cfg, dev->regs + S5P_CIICBOFF);

	cfg = 0;
	cfg |= S5P_CIO_OFFS_HOR(offset->cr_h);
	cfg |= S5P_CIO_OFFS_VER(offset->cr_v);
	writel(cfg, dev->regs + S5P_CIICROFF);

	/* Input original and real size. */
	fimc_hw_set_in_dma_size(ctx);

	/* Autoload is used currently only in FIFO mode. */
	fimc_hw_en_autoload(dev, ctx->out_path == FIMC_LCDFIFO);

	/* Set the input DMA to process single frame only. */
	cfg = readl(dev->regs + S5P_MSCTRL);
	cfg &= ~(S5P_MSCTRL_FLIP_MASK
		| S5P_MSCTRL_INFORMAT_MASK
		| S5P_MSCTRL_IN_BURST_COUNT_MASK
		| S5P_MSCTRL_INPUT_MASK
		| S5P_MSCTRL_C_INT_IN_MASK
		| S5P_MSCTRL_2P_IN_ORDER_MASK);

	cfg |= (S5P_MSCTRL_FRAME_COUNT(1) | S5P_MSCTRL_INPUT_MEMORY);

	switch (frame->fmt->color) {
	case S5P_FIMC_RGB565:
	case S5P_FIMC_RGB666:
	case S5P_FIMC_RGB888:
		cfg |= S5P_MSCTRL_INFORMAT_RGB;
		break;
	case S5P_FIMC_YCBCR420:
		cfg |= S5P_MSCTRL_INFORMAT_YCBCR420;

		if (frame->fmt->planes_cnt == 2)
			cfg |= ctx->in_order_2p | S5P_MSCTRL_C_INT_IN_2PLANE;
		else
			cfg |= S5P_MSCTRL_C_INT_IN_3PLANE;

		break;
	case S5P_FIMC_YCBYCR422:
	case S5P_FIMC_YCRYCB422:
	case S5P_FIMC_CBYCRY422:
	case S5P_FIMC_CRYCBY422:
		if (frame->fmt->planes_cnt == 1) {
			cfg |= ctx->in_order_1p
				| S5P_MSCTRL_INFORMAT_YCBCR422_1P;
		} else {
			cfg |= S5P_MSCTRL_INFORMAT_YCBCR422;

			if (frame->fmt->planes_cnt == 2)
				cfg |= ctx->in_order_2p
					| S5P_MSCTRL_C_INT_IN_2PLANE;
			else
				cfg |= S5P_MSCTRL_C_INT_IN_3PLANE;
		}
		break;
	default:
		break;
	}

	/*
	 * Input DMA flip mode (and rotation).
	 * Do not allow simultaneous rotation and flipping.
	 */
	if (!ctx->rotation && ctx->out_path == FIMC_LCDFIFO)
		cfg |= fimc_hw_get_in_flip(ctx->flip);

	writel(cfg, dev->regs + S5P_MSCTRL);

	/* Input/output DMA linear/tiled mode. */
	cfg = readl(dev->regs + S5P_CIDMAPARAM);
	cfg &= ~S5P_CIDMAPARAM_TILE_MASK;

	if (tiled_fmt(ctx->s_frame.fmt))
		cfg |= S5P_CIDMAPARAM_R_64X32;

	if (tiled_fmt(ctx->d_frame.fmt))
		cfg |= S5P_CIDMAPARAM_W_64X32;

	writel(cfg, dev->regs + S5P_CIDMAPARAM);
}


void fimc_hw_set_input_path(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;

	u32 cfg = readl(dev->regs + S5P_MSCTRL);
	cfg &= ~S5P_MSCTRL_INPUT_MASK;

	if (ctx->in_path == FIMC_DMA)
		cfg |= S5P_MSCTRL_INPUT_MEMORY;
	else
		cfg |= S5P_MSCTRL_INPUT_EXTCAM;

	writel(cfg, dev->regs + S5P_MSCTRL);
}

void fimc_hw_set_output_path(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;

	u32 cfg = readl(dev->regs + S5P_CISCCTRL);
	cfg &= ~S5P_CISCCTRL_LCDPATHEN_FIFO;
	if (ctx->out_path == FIMC_LCDFIFO)
		cfg |= S5P_CISCCTRL_LCDPATHEN_FIFO;
	writel(cfg, dev->regs + S5P_CISCCTRL);
}

void fimc_hw_set_input_addr(struct fimc_dev *dev, struct fimc_addr *paddr)
{
	u32 cfg = 0;

	cfg = readl(dev->regs + S5P_CIREAL_ISIZE);
	cfg |= S5P_CIREAL_ISIZE_ADDR_CH_DIS;
	writel(cfg, dev->regs + S5P_CIREAL_ISIZE);

	writel(paddr->y, dev->regs + S5P_CIIYSA0);
	writel(paddr->cb, dev->regs + S5P_CIICBSA0);
	writel(paddr->cr, dev->regs + S5P_CIICRSA0);

	cfg &= ~S5P_CIREAL_ISIZE_ADDR_CH_DIS;
	writel(cfg, dev->regs + S5P_CIREAL_ISIZE);
}

void fimc_hw_set_output_addr(struct fimc_dev *dev, struct fimc_addr *paddr)
{
	int i;
	/* Set all the output register sets to point to single video buffer. */
	for (i = 0; i < FIMC_MAX_OUT_BUFS; i++) {
		writel(paddr->y, dev->regs + S5P_CIOYSA(i));
		writel(paddr->cb, dev->regs + S5P_CIOCBSA(i));
		writel(paddr->cr, dev->regs + S5P_CIOCRSA(i));
	}
}
