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
#include <media/s5p_fimc.h>
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
	udelay(1000);

	cfg = readl(dev->regs + S5P_CIGCTRL);
	cfg &= ~S5P_CIGCTRL_SWRST;
	writel(cfg, dev->regs + S5P_CIGCTRL);
	/* Clear LASTCAPT_END bit : This is for Exynos4210 EVT1 */
	cfg = readl(dev->regs + S5P_CISTATUS);
	cfg &= ~S5P_CISTATUS_LASTCAPT_END;
	writel(cfg, dev->regs + S5P_CISTATUS);
}

void fimc_hw_set_irq_level(struct fimc_dev *dev)
{
	u32 cfg;
	cfg = readl(dev->regs + S5P_CIGCTRL);
	cfg |= S5P_CIGCTRL_IRQ_LEVEL;

	writel(cfg, dev->regs + S5P_CIGCTRL);
}

static u32 fimc_hw_get_in_flip(struct fimc_ctx *ctx)
{
	u32 flip = S5P_MSCTRL_FLIP_NORMAL;

	switch (ctx->flip) {
	case FLIP_X_AXIS:
		flip = S5P_MSCTRL_FLIP_X_MIRROR;
		break;
	case FLIP_Y_AXIS:
		flip = S5P_MSCTRL_FLIP_Y_MIRROR;
		break;
	case FLIP_XY_AXIS:
		flip = S5P_MSCTRL_FLIP_180;
		break;
	default:
		break;
	}
	if (ctx->rotation <= 90)
		return flip;

	return (flip ^ S5P_MSCTRL_FLIP_180) & S5P_MSCTRL_FLIP_180;
}

static u32 fimc_hw_get_target_flip(struct fimc_ctx *ctx)
{
	u32 flip = S5P_CITRGFMT_FLIP_NORMAL;

	switch (ctx->flip) {
	case FLIP_X_AXIS:
		flip = S5P_CITRGFMT_FLIP_X_MIRROR;
		break;
	case FLIP_Y_AXIS:
		flip = S5P_CITRGFMT_FLIP_Y_MIRROR;
		break;
	case FLIP_XY_AXIS:
		flip = S5P_CITRGFMT_FLIP_180;
		break;
	default:
		break;
	}
	if (ctx->rotation <= 90)
		return flip;

	return (flip ^ S5P_CITRGFMT_FLIP_180) & S5P_CITRGFMT_FLIP_180;
}

void fimc_hw_set_rotation(struct fimc_ctx *ctx)
{
	u32 cfg, flip;
	struct fimc_dev *dev = ctx->fimc_dev;

	cfg = readl(dev->regs + S5P_CITRGFMT);
	cfg &= ~(S5P_CITRGFMT_INROT90 | S5P_CITRGFMT_OUTROT90 |
		 S5P_CITRGFMT_FLIP_180);

	/*
	 * The input and output rotator cannot work simultaneously.
	 * Use the output rotator in output DMA mode or the input rotator
	 * in direct fifo output mode.
	 */
	if (ctx->rotation == 90 || ctx->rotation == 270) {
		if (ctx->out_path == FIMC_LCDFIFO)
			cfg |= S5P_CITRGFMT_INROT90;
		else
			cfg |= S5P_CITRGFMT_OUTROT90;
	}

	if (ctx->out_path == FIMC_DMA) {
		cfg |= fimc_hw_get_target_flip(ctx);
		writel(cfg, dev->regs + S5P_CITRGFMT);
	} else {
		/* LCD FIFO path */
		flip = readl(dev->regs + S5P_MSCTRL);
		flip &= ~S5P_MSCTRL_FLIP_MASK;
		flip |= fimc_hw_get_in_flip(ctx);
		writel(flip, dev->regs + S5P_MSCTRL);
	}
}

void fimc_hw_set_target_format(struct fimc_ctx *ctx)
{
	u32 cfg, cfg_ext;
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->d_frame;

	dbg("w= %d, h= %d color: %d", frame->width,
		frame->height, frame->fmt->color);

	cfg_ext = readl(dev->regs + S5P_CIEXTEN);
	cfg_ext &= ~(S5P_CIEXTEN_TRGHSIZE_EXT_MASK |
		     S5P_CIEXTEN_TRGVSIZE_EXT_MASK);
	cfg = readl(dev->regs + S5P_CITRGFMT);
	cfg &= ~(S5P_CITRGFMT_FMT_MASK | S5P_CITRGFMT_HSIZE_MASK |
		  S5P_CITRGFMT_VSIZE_MASK);

	switch (frame->fmt->color) {
	case S5P_FIMC_RGB565...S5P_FIMC_RGB444:
		cfg |= S5P_CITRGFMT_RGB;
		break;
	case S5P_FIMC_YCBCR420: /* fall through */
	case S5P_FIMC_YCRCB420:
		cfg |= S5P_CITRGFMT_YCBCR420;
		break;
	case S5P_FIMC_YCBYCR422...S5P_FIMC_CRYCBY422:
		if (frame->fmt->colplanes == 1)
			cfg |= S5P_CITRGFMT_YCBCR422_1P;
		else
			cfg |= S5P_CITRGFMT_YCBCR422;
		break;
	default:
		break;
	}

	if (ctx->rotation == 90 || ctx->rotation == 270) {
		cfg |= S5P_CITRGFMT_HSIZE(frame->height);
		cfg |= S5P_CITRGFMT_VSIZE(frame->width);
		cfg_ext |= S5P_CIEXTEN_TRGHSIZE_EXT(frame->height);
		cfg_ext |= S5P_CIEXTEN_TRGVSIZE_EXT(frame->width);
	} else {

		cfg |= S5P_CITRGFMT_HSIZE(frame->width);
		cfg |= S5P_CITRGFMT_VSIZE(frame->height);
		cfg_ext |= S5P_CIEXTEN_TRGHSIZE_EXT(frame->width);
		cfg_ext |= S5P_CIEXTEN_TRGVSIZE_EXT(frame->height);
	}

	writel(cfg, dev->regs + S5P_CITRGFMT);
	writel(cfg_ext, dev->regs + S5P_CIEXTEN);

	cfg = readl(dev->regs + S5P_CITAREA) & ~S5P_CITAREA_MASK;

	if (frame->fmt->fourcc == V4L2_PIX_FMT_JPEG)
		cfg |= frame->payload[0];
	else
	cfg |= (frame->width * frame->height);

	writel(cfg, dev->regs + S5P_CITAREA);
}

static void fimc_hw_set_out_dma_size(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->d_frame;
	u32 cfg;

	cfg = S5P_ORIG_SIZE_HOR(frame->f_width);
	cfg |= S5P_ORIG_SIZE_VER(frame->f_height);
	writel(cfg, dev->regs + S5P_ORGOSIZE);

	/* Select color space conversion equation (HD/SD size).*/
	cfg = readl(dev->regs + S5P_CIGCTRL);
	if (frame->f_width >= 1280) /* HD */
		cfg |= S5P_CIGCTRL_CSC_ITU601_709;
	else	/* SD */
		cfg &= ~S5P_CIGCTRL_CSC_ITU601_709;
	writel(cfg, dev->regs + S5P_CIGCTRL);

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
		 S5P_CIOCTRL_YCBCR_PLANE_MASK | S5P_CIOCTRL_RGB16FMT_MASK);

	if (frame->fmt->colplanes == 1)
		cfg |= ctx->out_order_1p;
	else if (frame->fmt->colplanes == 2)
		cfg |= ctx->out_order_2p | S5P_CIOCTRL_YCBCR_2PLANE;
	else if (frame->fmt->colplanes == 3)
		cfg |= S5P_CIOCTRL_YCBCR_3PLANE;

	if (frame->fmt->color == S5P_FIMC_RGB565)
		cfg |= S5P_CIOCTRL_RGB565;
	else if (frame->fmt->color == S5P_FIMC_RGB555)
		cfg |= S5P_CIOCTRL_ARGB1555;
	else if (frame->fmt->color == S5P_FIMC_RGB444)
		cfg |= S5P_CIOCTRL_ARGB4444;
	else if (frame->fmt->color == S5P_FIMC_YCRCB420)
		cfg |= S5P_CIOCTRL_ORDER422_2P_LSB_CBCR;

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
	u32 cfg = readl(dev->regs + S5P_CIOCTRL);
	if (enable)
		cfg |= S5P_CIOCTRL_LASTIRQ_ENABLE;
	else
		cfg &= ~S5P_CIOCTRL_LASTIRQ_ENABLE;
	writel(cfg, dev->regs + S5P_CIOCTRL);
}

void fimc_hw_set_prescaler(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev =  ctx->fimc_dev;
	struct fimc_scaler *sc = &ctx->scaler;
	u32 cfg, shfactor;

	shfactor = 10 - (sc->hfactor + sc->vfactor);

	cfg = S5P_CISCPRERATIO_SHFACTOR(shfactor);
	cfg |= S5P_CISCPRERATIO_HOR(sc->pre_hratio);
	cfg |= S5P_CISCPRERATIO_VER(sc->pre_vratio);
	writel(cfg, dev->regs + S5P_CISCPRERATIO);

	cfg = S5P_CISCPREDST_WIDTH(sc->pre_dst_width);
	cfg |= S5P_CISCPREDST_HEIGHT(sc->pre_dst_height);
	writel(cfg, dev->regs + S5P_CISCPREDST);
}

static void fimc_hw_set_scaler(struct fimc_ctx *ctx)
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
		if ((dst_frame->fmt->color == S5P_FIMC_RGB565)
		    | (dst_frame->fmt->color == S5P_FIMC_RGB555)
		    | (dst_frame->fmt->color == S5P_FIMC_RGB444))
			cfg |= S5P_CISCCTRL_OUTRGB_FMT_RGB565;
		else if (dst_frame->fmt->color == S5P_FIMC_RGB666)
			cfg |= S5P_CISCCTRL_OUTRGB_FMT_RGB666;
		else if (dst_frame->fmt->color == S5P_FIMC_RGB888)
			cfg |= S5P_CISCCTRL_OUTRGB_FMT_RGB888;
		cfg &= ~S5P_CISCCTRL_LCDPATHEN_FIFO;
	} else {
		cfg |= (S5P_CISCCTRL_OUTRGB_FMT_RGB888
			| S5P_CISCCTRL_LCDPATHEN_FIFO);

		if (ctx->flags & FIMC_SCAN_MODE_INTERLACED)
			cfg |= S5P_CISCCTRL_INTERLACE;
	}

	writel(cfg, dev->regs + S5P_CISCCTRL);
}

void fimc_hw_set_mainscaler(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct samsung_fimc_variant *variant = dev->variant;
	struct fimc_scaler *sc = &ctx->scaler;
	u32 cfg;

	dbg("main_hratio= 0x%X  main_vratio= 0x%X",
		sc->main_hratio, sc->main_vratio);

	fimc_hw_set_scaler(ctx);

	cfg = readl(dev->regs + S5P_CISCCTRL);

	if (variant->has_mainscaler_ext) {
		cfg &= ~(S5P_CISCCTRL_MHRATIO_MASK | S5P_CISCCTRL_MVRATIO_MASK);
		cfg |= S5P_CISCCTRL_MHRATIO_EXT(sc->main_hratio);
		cfg |= S5P_CISCCTRL_MVRATIO_EXT(sc->main_vratio);
		writel(cfg, dev->regs + S5P_CISCCTRL);

		cfg = readl(dev->regs + S5P_CIEXTEN);

		cfg &= ~(S5P_CIEXTEN_MVRATIO_EXT_MASK |
			 S5P_CIEXTEN_MHRATIO_EXT_MASK);
		cfg |= S5P_CIEXTEN_MHRATIO_EXT(sc->main_hratio);
		cfg |= S5P_CIEXTEN_MVRATIO_EXT(sc->main_vratio);
		writel(cfg, dev->regs + S5P_CIEXTEN);
	} else {
		cfg &= ~(S5P_CISCCTRL_MHRATIO_MASK | S5P_CISCCTRL_MVRATIO_MASK);
		cfg |= S5P_CISCCTRL_MHRATIO(sc->main_hratio);
		cfg |= S5P_CISCCTRL_MVRATIO(sc->main_vratio);
		writel(cfg, dev->regs + S5P_CISCCTRL);
	}
}

void fimc_hw_en_capture(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;

	u32 cfg = readl(dev->regs + S5P_CIIMGCPT);

	if (ctx->out_path == FIMC_DMA) {
		/* one shot mode */
		cfg |= S5P_CIIMGCPT_CPT_FREN_ENABLE | S5P_CIIMGCPT_IMGCPTEN;
	} else {
		/* Continous frame capture mode (freerun). */
		cfg &= ~(S5P_CIIMGCPT_CPT_FREN_ENABLE |
			 S5P_CIIMGCPT_CPT_FRMOD_CNT);
		cfg |= S5P_CIIMGCPT_IMGCPTEN;
	}

	if (ctx->scaler.enabled)
		cfg |= S5P_CIIMGCPT_IMGCPTEN_SC;

	writel(cfg | S5P_CIIMGCPT_IMGCPTEN, dev->regs + S5P_CIIMGCPT);
}

void fimc_hw_set_effect(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_effect *effect = &ctx->effect;
	u32 cfg = S5P_CIIMGEFF_IE_SC_AFTER;

	cfg |= effect->type;

	if (effect->type == S5P_FIMC_EFFECT_ARBITRARY) {
		cfg |= S5P_CIIMGEFF_PAT_CB(effect->pat_cb);
		cfg |= S5P_CIIMGEFF_PAT_CR(effect->pat_cr);
	}

	writel(cfg, dev->regs + S5P_CIIMGEFF);
}

void fimc_hw_set_rgb_alpha(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->d_frame;
	u32 cfg;

	if (!((frame->fmt->color == S5P_FIMC_RGB555)
	    | (frame->fmt->color == S5P_FIMC_RGB444)
	    | (frame->fmt->color == S5P_FIMC_RGB888)))
		return;

	cfg = readl(dev->regs + S5P_CIOCTRL);
	cfg &= ~S5P_CIOCTRL_ALPHA_OUT_MASK;
	cfg |= (frame->alpha << 4);
	writel(cfg, dev->regs + S5P_CIOCTRL);
}

static void fimc_hw_set_in_dma_size(struct fimc_ctx *ctx)
{
	struct fimc_dev *dev = ctx->fimc_dev;
	struct fimc_frame *frame = &ctx->s_frame;
	u32 cfg_o = 0;
	u32 cfg_r = 0;

	if (FIMC_LCDFIFO == ctx->out_path)
		cfg_r |= S5P_CIREAL_ISIZE_AUTOLOAD_EN;

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
	u32 cfg;

	/* Set the pixel offsets. */
	cfg = S5P_CIO_OFFS_HOR(offset->y_h);
	cfg |= S5P_CIO_OFFS_VER(offset->y_v);
	writel(cfg, dev->regs + S5P_CIIYOFF);

	cfg = S5P_CIO_OFFS_HOR(offset->cb_h);
	cfg |= S5P_CIO_OFFS_VER(offset->cb_v);
	writel(cfg, dev->regs + S5P_CIICBOFF);

	cfg = S5P_CIO_OFFS_HOR(offset->cr_h);
	cfg |= S5P_CIO_OFFS_VER(offset->cr_v);
	writel(cfg, dev->regs + S5P_CIICROFF);

	/* Input original and real size. */
	fimc_hw_set_in_dma_size(ctx);

	/* Use DMA autoload only in FIFO mode. */
	fimc_hw_en_autoload(dev, ctx->out_path == FIMC_LCDFIFO);

	/* Set the input DMA to process single frame only. */
	cfg = readl(dev->regs + S5P_MSCTRL);
	cfg &= ~(S5P_MSCTRL_INFORMAT_MASK
		| S5P_MSCTRL_IN_BURST_COUNT_MASK
		| S5P_MSCTRL_INPUT_MASK
		| S5P_MSCTRL_C_INT_IN_MASK
		| S5P_MSCTRL_2P_IN_ORDER_MASK);

	cfg |= (S5P_MSCTRL_IN_BURST_COUNT(4)
		| S5P_MSCTRL_INPUT_MEMORY
		| S5P_MSCTRL_FIFO_CTRL_FULL);

	switch (frame->fmt->color) {
	case S5P_FIMC_RGB565...S5P_FIMC_RGB888:
		cfg |= S5P_MSCTRL_INFORMAT_RGB;
		break;
	case S5P_FIMC_YCBCR420: /* fall through */
	case S5P_FIMC_YCRCB420:
		cfg |= S5P_MSCTRL_INFORMAT_YCBCR420;

		if (frame->fmt->colplanes == 2)
			cfg |= ctx->in_order_2p | S5P_MSCTRL_C_INT_IN_2PLANE;
		else
			cfg |= S5P_MSCTRL_C_INT_IN_3PLANE;

		if (frame->fmt->color == S5P_FIMC_YCRCB420)
			cfg |= S5P_MSCTRL_2P_IN_YCRCB;
		break;
	case S5P_FIMC_YCBYCR422...S5P_FIMC_CRYCBY422:
		if (frame->fmt->colplanes == 1) {
			cfg |= ctx->in_order_1p
				| S5P_MSCTRL_INFORMAT_YCBCR422_1P;
		} else {
			cfg |= S5P_MSCTRL_INFORMAT_YCBCR422;

			if (frame->fmt->colplanes == 2)
				cfg |= ctx->in_order_2p
					| S5P_MSCTRL_C_INT_IN_2PLANE;
			else
				cfg |= S5P_MSCTRL_C_INT_IN_3PLANE;
		}
		break;
	default:
		break;
	}

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
	u32 cfg = readl(dev->regs + S5P_CIREAL_ISIZE);
	cfg |= S5P_CIREAL_ISIZE_ADDR_CH_DIS;
	writel(cfg, dev->regs + S5P_CIREAL_ISIZE);

	writel(paddr->y, dev->regs + S5P_CIIYSA(0));
	writel(paddr->cb, dev->regs + S5P_CIICBSA(0));
	writel(paddr->cr, dev->regs + S5P_CIICRSA(0));

	cfg &= ~S5P_CIREAL_ISIZE_ADDR_CH_DIS;
	writel(cfg, dev->regs + S5P_CIREAL_ISIZE);
}

void fimc_hw_set_output_addr(struct fimc_dev *dev,
			     struct fimc_addr *paddr, int index)
{
	int i = (index == -1) ? 0 : index;
	do {
		writel(paddr->y, dev->regs + S5P_CIOYSA(i));
		writel(paddr->cb, dev->regs + S5P_CIOCBSA(i));
		writel(paddr->cr, dev->regs + S5P_CIOCRSA(i));
		dbg("dst_buf[%d]: 0x%X, cb: 0x%X, cr: 0x%X",
		    i, paddr->y, paddr->cb, paddr->cr);
	} while (index == -1 && ++i < FIMC_MAX_OUT_BUFS);
}

int fimc_hw_save_output_addr(struct fimc_dev *fimc)
{
	int i;
	for (i = 0; i < FIMC_MAX_OUT_BUFS; i++) {
		fimc->paddr[i].y = readl(fimc->regs + S5P_CIOYSA(i));
		fimc->paddr[i].cb = readl(fimc->regs + S5P_CIOCBSA(i));
		fimc->paddr[i].cr = readl(fimc->regs + S5P_CIOCRSA(i));
	}

	return 0;
}

int fimc_hw_set_camera_polarity(struct fimc_dev *fimc,
				struct s5p_fimc_isp_info *cam)
{
	u32 cfg = readl(fimc->regs + S5P_CIGCTRL);

	cfg &= ~(S5P_CIGCTRL_INVPOLPCLK | S5P_CIGCTRL_INVPOLVSYNC |
		 S5P_CIGCTRL_INVPOLHREF | S5P_CIGCTRL_INVPOLHSYNC);

	if (cam->flags & FIMC_CLK_INV_PCLK)
		cfg |= S5P_CIGCTRL_INVPOLPCLK;

	if (cam->flags & FIMC_CLK_INV_VSYNC)
		cfg |= S5P_CIGCTRL_INVPOLVSYNC;

	if (cam->flags & FIMC_CLK_INV_HREF)
		cfg |= S5P_CIGCTRL_INVPOLHREF;

	if (cam->flags & FIMC_CLK_INV_HSYNC)
		cfg |= S5P_CIGCTRL_INVPOLHSYNC;

	writel(cfg, fimc->regs + S5P_CIGCTRL);

	return 0;
}

int fimc_hw_set_camera_source(struct fimc_dev *fimc,
			      struct s5p_fimc_isp_info *cam)
{
	struct fimc_frame *f = &fimc->vid_cap.ctx->s_frame;
	u32 cfg = 0;
	u32 bus_width;
	int i;
	static const struct {
		u32 pixelcode;
		u32 cisrcfmt;
		u16 bus_width;
	} pix_desc[] = {
		{ V4L2_MBUS_FMT_YUYV8_2X8, S5P_CISRCFMT_ORDER422_YCBYCR, 8 },
		{ V4L2_MBUS_FMT_YVYU8_2X8, S5P_CISRCFMT_ORDER422_YCRYCB, 8 },
		{ V4L2_MBUS_FMT_VYUY8_2X8, S5P_CISRCFMT_ORDER422_CRYCBY, 8 },
		{ V4L2_MBUS_FMT_UYVY8_2X8, S5P_CISRCFMT_ORDER422_CBYCRY, 8 },
		/* TODO: Add pixel codes for 16-bit bus width */
	};
	dbg("f->o_width : %d, f->o_height : %d\n", f->o_width, f->o_height);
	dbg("code : %d\n", fimc->vid_cap.fmt.code);

	if (cam->bus_type == FIMC_ITU_601 || cam->bus_type == FIMC_ITU_656) {
		for (i = 0; i < ARRAY_SIZE(pix_desc); i++) {
			if (fimc->vid_cap.fmt.code == pix_desc[i].pixelcode) {
				cfg = pix_desc[i].cisrcfmt;
				bus_width = pix_desc[i].bus_width;
				break;
			}
		}

		if (i == ARRAY_SIZE(pix_desc)) {
			v4l2_err(&fimc->vid_cap.v4l2_dev,
				 "Camera color format not supported: %d\n",
				 fimc->vid_cap.fmt.code);
			return -EINVAL;
		}

		if (cam->bus_type == FIMC_ITU_601) {
			if (bus_width == 8)
				cfg |= S5P_CISRCFMT_ITU601_8BIT;
			else if (bus_width == 16)
				cfg |= S5P_CISRCFMT_ITU601_16BIT;
		} /* else defaults to ITU-R BT.656 8-bit */
	} else if (cam->bus_type == FIMC_MIPI_CSI2) {
		if (fimc_fmt_is_jpeg(f->fmt->color) || fimc->vid_cap.is.sd
						|| fimc->vid_cap.is.camcording)
			cfg |= S5P_CISRCFMT_ITU601_8BIT;
	}

	if (fimc->vid_cap.is.sd || fimc->vid_cap.is.camcording)
		cfg |= S5P_CISRCFMT_HSIZE(fimc->vid_cap.is.fmt.width) |
			S5P_CISRCFMT_VSIZE(fimc->vid_cap.is.fmt.height);
	else
		cfg |= S5P_CISRCFMT_HSIZE(f->o_width) |
			S5P_CISRCFMT_VSIZE(f->o_height);
	writel(cfg, fimc->regs + S5P_CISRCFMT);
	return 0;
}


int fimc_hw_set_camera_offset(struct fimc_dev *fimc, struct fimc_frame *f)
{
	u32 hoff2, voff2;

	u32 cfg = readl(fimc->regs + S5P_CIWDOFST);

	cfg &= ~(S5P_CIWDOFST_HOROFF_MASK | S5P_CIWDOFST_VEROFF_MASK);
	cfg |=  S5P_CIWDOFST_OFF_EN |
		S5P_CIWDOFST_HOROFF(f->offs_h) |
		S5P_CIWDOFST_VEROFF(f->offs_v);

	writel(cfg, fimc->regs + S5P_CIWDOFST);

	/* See CIWDOFSTn register description in the datasheet for details. */
	hoff2 = f->o_width - f->width - f->offs_h;
	voff2 = f->o_height - f->height - f->offs_v;
	cfg = S5P_CIWDOFST2_HOROFF(hoff2) | S5P_CIWDOFST2_VEROFF(voff2);

	writel(cfg, fimc->regs + S5P_CIWDOFST2);
	return 0;
}

int fimc_hw_set_camera_type(struct fimc_dev *fimc,
			    struct s5p_fimc_isp_info *cam)
{
	u32 cfg = 0;
	u32 tmp = 0;
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;

	cfg = readl(fimc->regs + S5P_CIGCTRL);

	/* Select ITU B interface, disable Writeback path and test pattern. */
	cfg &= ~(S5P_CIGCTRL_TESTPAT_MASK | S5P_CIGCTRL_SELCAM_ITU_A |
		S5P_CIGCTRL_SELCAM_MIPI | S5P_CIGCTRL_CAMIF_SELWB |
		S5P_CIGCTRL_SELCAM_MIPI_A | S5P_CIGCTRL_SELWRITEBACK_A |
		S5P_CIGCTRL_CAM_JPEG);

	if (cam->bus_type == FIMC_MIPI_CSI2) {
		if (!vid_cap->is.sd && !vid_cap->is.camcording) {
			cfg |= S5P_CIGCTRL_SELCAM_MIPI;

			if (cam->mux_id == 0)
				cfg |= S5P_CIGCTRL_SELCAM_MIPI_A;

			/* TODO: add remaining supported formats. */
			switch (vid_cap->fmt.code) {
			case V4L2_MBUS_FMT_VYUY8_2X8:
			case V4L2_MBUS_FMT_UYVY8_2X8:
			case V4L2_MBUS_FMT_YUYV8_2X8:
				tmp = S5P_CSIIMGFMT_YCBCR422_8BIT;
				break;
			case V4L2_MBUS_FMT_JPEG_1X8:
				tmp = S5P_CSIIMGFMT_USER(1);
				cfg |= S5P_CIGCTRL_CAM_JPEG;
				break;
			default:
				v4l2_err(&fimc->vid_cap.v4l2_dev,
				 "Not supported camera pixel format: %d",
				 vid_cap->fmt.code);
			}
			tmp |= (cam->csi_data_align == 32) << 8;

			writel(tmp, fimc->regs + S5P_CSIIMGFMT);
		} else {
			cfg |= S5P_CIGCTRL_CAMIF_SELWB;
			cfg |= S5P_CIGCTRL_SELWRITEBACK_B;
		}
	} else if (cam->bus_type == FIMC_ITU_601 ||
		  cam->bus_type == FIMC_ITU_656) {
		if (cam->mux_id == 0) /* ITU-A, ITU-B: 0, 1 */
			cfg |= S5P_CIGCTRL_SELCAM_ITU_A;
	} else if (cam->bus_type == FIMC_LCD_WB) {
		cfg |= S5P_CIGCTRL_CAMIF_SELWB;
		if (cam->mux_id == 0)
			cfg |= S5P_CIGCTRL_SELWRITEBACK_A;
	} else {
		err("invalid camera bus type selected\n");
		return -EINVAL;
	}
	writel(cfg, fimc->regs + S5P_CIGCTRL);

	return 0;
}

int fimc_hwset_sysreg_camblk_fimd0_wb(struct fimc_dev *fimc)
{
	u32 cfg = readl(SYSREG_CAMERA_BLK);
	cfg = cfg & (~(0x3 << 14));
	if (fimc->id == 0)
		cfg = cfg | FIMD0_WB_DEST_FIMC0;
	else if (fimc->id == 1)
		cfg = cfg | FIMD0_WB_DEST_FIMC1;
	else if (fimc->id == 2)
		cfg = cfg | FIMD0_WB_DEST_FIMC2;
	else if (fimc->id == 3)
		cfg = cfg | FIMD0_WB_DEST_FIMC3;
	else
		err("%s: not supported id : %d\n", __func__, fimc->id);

	writel(cfg, SYSREG_CAMERA_BLK);

	return 0;
}
int fimc_hwset_sysreg_camblk_fimd1_wb(struct fimc_dev *fimc)
{
	u32 cfg = readl(SYSREG_CAMERA_BLK);
	cfg = cfg & (~(0x3 << 10));
	if (fimc->id == 0)
		cfg = cfg | FIMD1_WB_DEST_FIMC0;
	else if (fimc->id == 1)
		cfg = cfg | FIMD1_WB_DEST_FIMC1;
	else if (fimc->id == 2)
		cfg = cfg | FIMD1_WB_DEST_FIMC2;
	else if (fimc->id == 3)
		cfg = cfg | FIMD1_WB_DEST_FIMC3;
	else
		err("%s: not supported id : %d\n", __func__, fimc->id);

	writel(cfg, SYSREG_CAMERA_BLK);

	return 0;
}

int fimc_hwset_sysreg_camblk_isp_wb(struct fimc_dev *fimc)
{
	u32 camblk_cfg = readl(SYSREG_CAMERA_BLK);
	u32 ispblk_cfg = readl(SYSREG_ISP_BLK);
	camblk_cfg = camblk_cfg & (~(0x7 << 20));
	if (fimc->id == 0)
		camblk_cfg = camblk_cfg | (0x1 << 20);
	else if (fimc->id == 1)
		camblk_cfg = camblk_cfg | (0x2 << 20);
	else if (fimc->id == 2)
		camblk_cfg = camblk_cfg | (0x4 << 20);
	else if (fimc->id == 3)
		camblk_cfg = camblk_cfg | (0x7 << 20); /* FIXME*/
	else
		err("%s: not supported id : %d\n", __func__, fimc->id);

	camblk_cfg = camblk_cfg & (~(0x1 << 15));
	writel(camblk_cfg, SYSREG_CAMERA_BLK);
	udelay(1000);
	camblk_cfg = camblk_cfg | (0x1 << 15);
	writel(camblk_cfg, SYSREG_CAMERA_BLK);

	ispblk_cfg = ispblk_cfg & (~(0x1 << 7));
	writel(ispblk_cfg, SYSREG_ISP_BLK);
	udelay(1000);
	ispblk_cfg = ispblk_cfg | (0x1 << 7);
	writel(ispblk_cfg, SYSREG_ISP_BLK);

	return 0;
}

int fimc_wait_disable_capture(struct fimc_dev *fimc)
{
	unsigned long timeo = jiffies + 20; /* timeout of 100ms */
	u32 cfg;

	while (time_before(jiffies, timeo)) {
		cfg = readl(fimc->regs + S5P_CISTATUS);

		if (!(cfg & S5P_CISTATUS_IMGCPT_EN) &&
			!(cfg &	S5P_CISTATUS_IMGCPT_SCEN) &&
			!(cfg & S5P_CISTATUS_SCALER_START))
			return 0;
		msleep(5);
	}
	dbg("wait time : %d ms\n", jiffies_to_msecs(jiffies - timeo + 20));

	return -EBUSY;
}

int fimc_hwset_enable_lastend(struct fimc_dev *fimc)
{
	u32 cfg = readl(fimc->regs + S5P_CIOCTRL);

	cfg |= S5P_CIOCTRL_LASTENDEN;
	writel(cfg, fimc->regs + S5P_CIOCTRL);

	return 0;
}
