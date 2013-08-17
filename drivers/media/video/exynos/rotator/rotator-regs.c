/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Register interface file for Exynos Rotator driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include "rotator.h"

void rot_hwset_irq_frame_done(struct rot_dev *rot, u32 enable)
{
	unsigned long cfg = readl(rot->regs + ROTATOR_CONFIG);

	if (enable)
		cfg |= ROTATOR_CONFIG_IRQ_DONE;
	else
		cfg &= ~ROTATOR_CONFIG_IRQ_DONE;

	writel(cfg, rot->regs + ROTATOR_CONFIG);
}

void rot_hwset_irq_illegal_config(struct rot_dev *rot, u32 enable)
{
	unsigned long cfg = readl(rot->regs + ROTATOR_CONFIG);

	if (enable)
		cfg |= ROTATOR_CONFIG_IRQ_ILLEGAL;
	else
		cfg &= ~ROTATOR_CONFIG_IRQ_ILLEGAL;

	writel(cfg, rot->regs + ROTATOR_CONFIG);
}

int rot_hwset_image_format(struct rot_dev *rot, u32 pixelformat)
{
	unsigned long cfg = readl(rot->regs + ROTATOR_CONTROL);
	cfg &= ~ROTATOR_CONTROL_FMT_MASK;

	switch (pixelformat) {
	case V4L2_PIX_FMT_YUV420M:
		cfg |= ROTATOR_CONTROL_FMT_YCBCR420_3P;
		break;
	case V4L2_PIX_FMT_NV12M:
		cfg |= ROTATOR_CONTROL_FMT_YCBCR420_2P;
		break;
	case V4L2_PIX_FMT_YUYV:
		cfg |= ROTATOR_CONTROL_FMT_YCBCR422;
		break;
	case V4L2_PIX_FMT_RGB565:
		cfg |= ROTATOR_CONTROL_FMT_RGB565;
		break;
	case V4L2_PIX_FMT_RGB32:
		cfg |= ROTATOR_CONTROL_FMT_RGB888;
		break;
	default:
		dev_err(rot->dev, "invalid pixelformat type\n");
		return -EINVAL;
	}
	writel(cfg, rot->regs + ROTATOR_CONTROL);
	return 0;
}

void rot_hwset_flip(struct rot_dev *rot, u32 direction)
{
	unsigned long cfg = readl(rot->regs + ROTATOR_CONTROL);
	cfg &= ~ROTATOR_CONTROL_FLIP_MASK;

	if (direction == ROT_VFLIP)
		cfg |= ROTATOR_CONTROL_FLIP_V;
	else if (direction == ROT_HFLIP)
		cfg |= ROTATOR_CONTROL_FLIP_H;

	writel(cfg, rot->regs + ROTATOR_CONTROL);
}

void rot_hwset_rotation(struct rot_dev *rot, int degree)
{
	unsigned long cfg = readl(rot->regs + ROTATOR_CONTROL);
	cfg &= ~ROTATOR_CONTROL_ROT_MASK;

	if (degree == 90)
		cfg |= ROTATOR_CONTROL_ROT_90;
	else if (degree == 180)
		cfg |= ROTATOR_CONTROL_ROT_180;
	else if (degree == 270)
		cfg |= ROTATOR_CONTROL_ROT_270;

	writel(cfg, rot->regs + ROTATOR_CONTROL);
}

void rot_hwset_start(struct rot_dev *rot)
{
	unsigned long cfg = readl(rot->regs + ROTATOR_CONTROL);

	cfg |= ROTATOR_CONTROL_START;

	writel(cfg, rot->regs + ROTATOR_CONTROL);
}

void rot_hwset_src_addr(struct rot_dev *rot, dma_addr_t addr, u32 comp)
{
	writel(addr, rot->regs + ROTATOR_SRC_IMG_ADDR(comp));
}

void rot_hwset_dst_addr(struct rot_dev *rot, dma_addr_t addr, u32 comp)
{
	writel(addr, rot->regs + ROTATOR_DST_IMG_ADDR(comp));
}

void rot_hwset_src_imgsize(struct rot_dev *rot, struct rot_frame *frame)
{
	unsigned long cfg;

	cfg = ROTATOR_SRCIMG_YSIZE(frame->pix_mp.height) |
		ROTATOR_SRCIMG_XSIZE(frame->pix_mp.width);

	writel(cfg, rot->regs + ROTATOR_SRCIMG);

	cfg = ROTATOR_SRCROT_YSIZE(frame->pix_mp.height) |
		ROTATOR_SRCROT_XSIZE(frame->pix_mp.width);

	writel(cfg, rot->regs + ROTATOR_SRCROT);
}

void rot_hwset_src_crop(struct rot_dev *rot, struct v4l2_rect *rect)
{
	unsigned long cfg;

	cfg = ROTATOR_SRC_Y(rect->top) |
		ROTATOR_SRC_X(rect->left);

	writel(cfg, rot->regs + ROTATOR_SRC);

	cfg = ROTATOR_SRCROT_YSIZE(rect->height) |
		ROTATOR_SRCROT_XSIZE(rect->width);

	writel(cfg, rot->regs + ROTATOR_SRCROT);
}

void rot_hwset_dst_imgsize(struct rot_dev *rot, struct rot_frame *frame)
{
	unsigned long cfg;

	cfg = ROTATOR_DSTIMG_YSIZE(frame->pix_mp.height) |
		ROTATOR_DSTIMG_XSIZE(frame->pix_mp.width);

	writel(cfg, rot->regs + ROTATOR_DSTIMG);
}

void rot_hwset_dst_crop(struct rot_dev *rot, struct v4l2_rect *rect)
{
	unsigned long cfg;

	cfg =  ROTATOR_DST_Y(rect->top) |
		ROTATOR_DST_X(rect->left);

	writel(cfg, rot->regs + ROTATOR_DST);
}

void rot_hwget_irq_src(struct rot_dev *rot, enum rot_irq_src *irq)
{
	unsigned long cfg = readl(rot->regs + ROTATOR_STATUS);
	cfg = ROTATOR_STATUS_IRQ(cfg);

	if (cfg == 1)
		*irq = ISR_PEND_DONE;
	else if (cfg == 2)
		*irq = ISR_PEND_ILLEGAL;
}

void rot_hwset_irq_clear(struct rot_dev *rot, enum rot_irq_src *irq)
{
	unsigned long cfg = readl(rot->regs + ROTATOR_STATUS);
	cfg |= ROTATOR_STATUS_IRQ_PENDING((u32)irq);

	writel(cfg, rot->regs + ROTATOR_STATUS);
}

void rot_hwget_status(struct rot_dev *rot, enum rot_status *state)
{
	unsigned long cfg;

	cfg = readl(rot->regs + ROTATOR_STATUS);
	cfg &= ROTATOR_STATUS_MASK;

	switch (cfg) {
	case 0:
		*state = ROT_IDLE;
		break;
	case 1:
		*state = ROT_RESERVED;
		break;
	case 2:
		*state = ROT_RUNNING;
		break;
	case 3:
		*state = ROT_RUNNING_REMAIN;
		break;
	};
}

void rot_dump_registers(struct rot_dev *rot)
{
	unsigned int tmp, i;

	rot_dbg("dump rotator registers\n");
	for (i = 0; i <= ROTATOR_DST; i += 0x4) {
		tmp = readl(rot->regs + i);
		rot_dbg("0x%08x: 0x%08x", i, tmp);
	}
}
