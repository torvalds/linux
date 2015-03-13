/* linux/drivers/media/platform/exynos3250-jpeg/jpeg-hw.h
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/videodev2.h>
#include <linux/delay.h>

#include "jpeg-core.h"
#include "jpeg-regs.h"
#include "jpeg-hw-exynos3250.h"

void exynos3250_jpeg_reset(void __iomem *regs)
{
	u32 reg = 0;
	int count = 1000;

	writel(1, regs + EXYNOS3250_SW_RESET);
	/* no other way but polling for when JPEG IP becomes operational */
	while (reg != 0 && --count > 0) {
		udelay(1);
		cpu_relax();
		reg = readl(regs + EXYNOS3250_SW_RESET);
	}

	reg = 0;
	count = 1000;

	while (reg != 1 && --count > 0) {
		writel(1, regs + EXYNOS3250_JPGDRI);
		udelay(1);
		cpu_relax();
		reg = readl(regs + EXYNOS3250_JPGDRI);
	}

	writel(0, regs + EXYNOS3250_JPGDRI);
}

void exynos3250_jpeg_poweron(void __iomem *regs)
{
	writel(EXYNOS3250_POWER_ON, regs + EXYNOS3250_JPGCLKCON);
}

void exynos3250_jpeg_set_dma_num(void __iomem *regs)
{
	writel(((EXYNOS3250_DMA_MO_COUNT << EXYNOS3250_WDMA_ISSUE_NUM_SHIFT) &
			EXYNOS3250_WDMA_ISSUE_NUM_MASK) |
	       ((EXYNOS3250_DMA_MO_COUNT << EXYNOS3250_RDMA_ISSUE_NUM_SHIFT) &
			EXYNOS3250_RDMA_ISSUE_NUM_MASK) |
	       ((EXYNOS3250_DMA_MO_COUNT << EXYNOS3250_ISSUE_GATHER_NUM_SHIFT) &
			EXYNOS3250_ISSUE_GATHER_NUM_MASK),
		regs + EXYNOS3250_DMA_ISSUE_NUM);
}

void exynos3250_jpeg_clk_set(void __iomem *base)
{
	u32 reg;

	reg = readl(base + EXYNOS3250_JPGCMOD) & ~EXYNOS3250_HALF_EN_MASK;

	writel(reg | EXYNOS3250_HALF_EN, base + EXYNOS3250_JPGCMOD);
}

void exynos3250_jpeg_input_raw_fmt(void __iomem *regs, unsigned int fmt)
{
	u32 reg;

	reg = readl(regs + EXYNOS3250_JPGCMOD) &
			EXYNOS3250_MODE_Y16_MASK;

	switch (fmt) {
	case V4L2_PIX_FMT_RGB32:
		reg |= EXYNOS3250_MODE_SEL_ARGB8888;
		break;
	case V4L2_PIX_FMT_BGR32:
		reg |= EXYNOS3250_MODE_SEL_ARGB8888 | EXYNOS3250_SRC_SWAP_RGB;
		break;
	case V4L2_PIX_FMT_RGB565:
		reg |= EXYNOS3250_MODE_SEL_RGB565;
		break;
	case V4L2_PIX_FMT_RGB565X:
		reg |= EXYNOS3250_MODE_SEL_RGB565 | EXYNOS3250_SRC_SWAP_RGB;
		break;
	case V4L2_PIX_FMT_YUYV:
		reg |= EXYNOS3250_MODE_SEL_422_1P_LUM_CHR;
		break;
	case V4L2_PIX_FMT_YVYU:
		reg |= EXYNOS3250_MODE_SEL_422_1P_LUM_CHR |
			EXYNOS3250_SRC_SWAP_UV;
		break;
	case V4L2_PIX_FMT_UYVY:
		reg |= EXYNOS3250_MODE_SEL_422_1P_CHR_LUM;
		break;
	case V4L2_PIX_FMT_VYUY:
		reg |= EXYNOS3250_MODE_SEL_422_1P_CHR_LUM |
			EXYNOS3250_SRC_SWAP_UV;
		break;
	case V4L2_PIX_FMT_NV12:
		reg |= EXYNOS3250_MODE_SEL_420_2P | EXYNOS3250_SRC_NV12;
		break;
	case V4L2_PIX_FMT_NV21:
		reg |= EXYNOS3250_MODE_SEL_420_2P | EXYNOS3250_SRC_NV21;
		break;
	case V4L2_PIX_FMT_YUV420:
		reg |= EXYNOS3250_MODE_SEL_420_3P;
		break;
	default:
		break;

	}

	writel(reg, regs + EXYNOS3250_JPGCMOD);
}

void exynos3250_jpeg_set_y16(void __iomem *regs, bool y16)
{
	u32 reg;

	reg = readl(regs + EXYNOS3250_JPGCMOD);
	if (y16)
		reg |= EXYNOS3250_MODE_Y16;
	else
		reg &= ~EXYNOS3250_MODE_Y16_MASK;
	writel(reg, regs + EXYNOS3250_JPGCMOD);
}

void exynos3250_jpeg_proc_mode(void __iomem *regs, unsigned int mode)
{
	u32 reg, m;

	if (mode == S5P_JPEG_ENCODE)
		m = EXYNOS3250_PROC_MODE_COMPR;
	else
		m = EXYNOS3250_PROC_MODE_DECOMPR;
	reg = readl(regs + EXYNOS3250_JPGMOD);
	reg &= ~EXYNOS3250_PROC_MODE_MASK;
	reg |= m;
	writel(reg, regs + EXYNOS3250_JPGMOD);
}

void exynos3250_jpeg_subsampling_mode(void __iomem *regs, unsigned int mode)
{
	u32 reg, m = 0;

	switch (mode) {
	case V4L2_JPEG_CHROMA_SUBSAMPLING_444:
		m = EXYNOS3250_SUBSAMPLING_MODE_444;
		break;
	case V4L2_JPEG_CHROMA_SUBSAMPLING_422:
		m = EXYNOS3250_SUBSAMPLING_MODE_422;
		break;
	case V4L2_JPEG_CHROMA_SUBSAMPLING_420:
		m = EXYNOS3250_SUBSAMPLING_MODE_420;
		break;
	}

	reg = readl(regs + EXYNOS3250_JPGMOD);
	reg &= ~EXYNOS3250_SUBSAMPLING_MODE_MASK;
	reg |= m;
	writel(reg, regs + EXYNOS3250_JPGMOD);
}

unsigned int exynos3250_jpeg_get_subsampling_mode(void __iomem *regs)
{
	return readl(regs + EXYNOS3250_JPGMOD) &
				EXYNOS3250_SUBSAMPLING_MODE_MASK;
}

void exynos3250_jpeg_dri(void __iomem *regs, unsigned int dri)
{
	u32 reg;

	reg = dri & EXYNOS3250_JPGDRI_MASK;
	writel(reg, regs + EXYNOS3250_JPGDRI);
}

void exynos3250_jpeg_qtbl(void __iomem *regs, unsigned int t, unsigned int n)
{
	unsigned long reg;

	reg = readl(regs + EXYNOS3250_QHTBL);
	reg &= ~EXYNOS3250_QT_NUM_MASK(t);
	reg |= (n << EXYNOS3250_QT_NUM_SHIFT(t)) &
					EXYNOS3250_QT_NUM_MASK(t);
	writel(reg, regs + EXYNOS3250_QHTBL);
}

void exynos3250_jpeg_htbl_ac(void __iomem *regs, unsigned int t)
{
	unsigned long reg;

	reg = readl(regs + EXYNOS3250_QHTBL);
	reg &= ~EXYNOS3250_HT_NUM_AC_MASK(t);
	/* this driver uses table 0 for all color components */
	reg |= (0 << EXYNOS3250_HT_NUM_AC_SHIFT(t)) &
					EXYNOS3250_HT_NUM_AC_MASK(t);
	writel(reg, regs + EXYNOS3250_QHTBL);
}

void exynos3250_jpeg_htbl_dc(void __iomem *regs, unsigned int t)
{
	unsigned long reg;

	reg = readl(regs + EXYNOS3250_QHTBL);
	reg &= ~EXYNOS3250_HT_NUM_DC_MASK(t);
	/* this driver uses table 0 for all color components */
	reg |= (0 << EXYNOS3250_HT_NUM_DC_SHIFT(t)) &
					EXYNOS3250_HT_NUM_DC_MASK(t);
	writel(reg, regs + EXYNOS3250_QHTBL);
}

void exynos3250_jpeg_set_y(void __iomem *regs, unsigned int y)
{
	u32 reg;

	reg = y & EXYNOS3250_JPGY_MASK;
	writel(reg, regs + EXYNOS3250_JPGY);
}

void exynos3250_jpeg_set_x(void __iomem *regs, unsigned int x)
{
	u32 reg;

	reg = x & EXYNOS3250_JPGX_MASK;
	writel(reg, regs + EXYNOS3250_JPGX);
}

#if 0	/* Currently unused */
unsigned int exynos3250_jpeg_get_y(void __iomem *regs)
{
	return readl(regs + EXYNOS3250_JPGY);
}

unsigned int exynos3250_jpeg_get_x(void __iomem *regs)
{
	return readl(regs + EXYNOS3250_JPGX);
}
#endif

void exynos3250_jpeg_interrupts_enable(void __iomem *regs)
{
	u32 reg;

	reg = readl(regs + EXYNOS3250_JPGINTSE);
	reg |= (EXYNOS3250_JPEG_DONE_EN |
		EXYNOS3250_WDMA_DONE_EN |
		EXYNOS3250_RDMA_DONE_EN |
		EXYNOS3250_ENC_STREAM_INT_EN |
		EXYNOS3250_CORE_DONE_EN |
		EXYNOS3250_ERR_INT_EN |
		EXYNOS3250_HEAD_INT_EN);
	writel(reg, regs + EXYNOS3250_JPGINTSE);
}

void exynos3250_jpeg_enc_stream_bound(void __iomem *regs, unsigned int size)
{
	u32 reg;

	reg = size & EXYNOS3250_ENC_STREAM_BOUND_MASK;
	writel(reg, regs + EXYNOS3250_ENC_STREAM_BOUND);
}

void exynos3250_jpeg_output_raw_fmt(void __iomem *regs, unsigned int fmt)
{
	u32 reg;

	switch (fmt) {
	case V4L2_PIX_FMT_RGB32:
		reg = EXYNOS3250_OUT_FMT_ARGB8888;
		break;
	case V4L2_PIX_FMT_BGR32:
		reg = EXYNOS3250_OUT_FMT_ARGB8888 | EXYNOS3250_OUT_SWAP_RGB;
		break;
	case V4L2_PIX_FMT_RGB565:
		reg = EXYNOS3250_OUT_FMT_RGB565;
		break;
	case V4L2_PIX_FMT_RGB565X:
		reg = EXYNOS3250_OUT_FMT_RGB565 | EXYNOS3250_OUT_SWAP_RGB;
		break;
	case V4L2_PIX_FMT_YUYV:
		reg = EXYNOS3250_OUT_FMT_422_1P_LUM_CHR;
		break;
	case V4L2_PIX_FMT_YVYU:
		reg = EXYNOS3250_OUT_FMT_422_1P_LUM_CHR |
			EXYNOS3250_OUT_SWAP_UV;
		break;
	case V4L2_PIX_FMT_UYVY:
		reg = EXYNOS3250_OUT_FMT_422_1P_CHR_LUM;
		break;
	case V4L2_PIX_FMT_VYUY:
		reg = EXYNOS3250_OUT_FMT_422_1P_CHR_LUM |
			EXYNOS3250_OUT_SWAP_UV;
		break;
	case V4L2_PIX_FMT_NV12:
		reg = EXYNOS3250_OUT_FMT_420_2P | EXYNOS3250_OUT_NV12;
		break;
	case V4L2_PIX_FMT_NV21:
		reg = EXYNOS3250_OUT_FMT_420_2P | EXYNOS3250_OUT_NV21;
		break;
	case V4L2_PIX_FMT_YUV420:
		reg = EXYNOS3250_OUT_FMT_420_3P;
		break;
	default:
		reg = 0;
		break;
	}

	writel(reg, regs + EXYNOS3250_OUTFORM);
}

void exynos3250_jpeg_jpgadr(void __iomem *regs, unsigned int addr)
{
	writel(addr, regs + EXYNOS3250_JPG_JPGADR);
}

void exynos3250_jpeg_imgadr(void __iomem *regs, struct s5p_jpeg_addr *img_addr)
{
	writel(img_addr->y, regs + EXYNOS3250_LUMA_BASE);
	writel(img_addr->cb, regs + EXYNOS3250_CHROMA_BASE);
	writel(img_addr->cr, regs + EXYNOS3250_CHROMA_CR_BASE);
}

void exynos3250_jpeg_stride(void __iomem *regs, unsigned int img_fmt,
			    unsigned int width)
{
	u32 reg_luma = 0, reg_cr = 0, reg_cb = 0;

	switch (img_fmt) {
	case V4L2_PIX_FMT_RGB32:
		reg_luma = 4 * width;
		break;
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB565X:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
		reg_luma = 2 * width;
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		reg_luma = width;
		reg_cb = reg_luma;
		break;
	case V4L2_PIX_FMT_YUV420:
		reg_luma = width;
		reg_cb = reg_cr = reg_luma / 2;
		break;
	default:
		break;
	}

	writel(reg_luma, regs + EXYNOS3250_LUMA_STRIDE);
	writel(reg_cb, regs + EXYNOS3250_CHROMA_STRIDE);
	writel(reg_cr, regs + EXYNOS3250_CHROMA_CR_STRIDE);
}

void exynos3250_jpeg_offset(void __iomem *regs, unsigned int x_offset,
				unsigned int y_offset)
{
	u32 reg;

	reg = (y_offset << EXYNOS3250_LUMA_YY_OFFSET_SHIFT) &
			EXYNOS3250_LUMA_YY_OFFSET_MASK;
	reg |= (x_offset << EXYNOS3250_LUMA_YX_OFFSET_SHIFT) &
			EXYNOS3250_LUMA_YX_OFFSET_MASK;

	writel(reg, regs + EXYNOS3250_LUMA_XY_OFFSET);

	reg = (y_offset << EXYNOS3250_CHROMA_YY_OFFSET_SHIFT) &
			EXYNOS3250_CHROMA_YY_OFFSET_MASK;
	reg |= (x_offset << EXYNOS3250_CHROMA_YX_OFFSET_SHIFT) &
			EXYNOS3250_CHROMA_YX_OFFSET_MASK;

	writel(reg, regs + EXYNOS3250_CHROMA_XY_OFFSET);

	reg = (y_offset << EXYNOS3250_CHROMA_CR_YY_OFFSET_SHIFT) &
			EXYNOS3250_CHROMA_CR_YY_OFFSET_MASK;
	reg |= (x_offset << EXYNOS3250_CHROMA_CR_YX_OFFSET_SHIFT) &
			EXYNOS3250_CHROMA_CR_YX_OFFSET_MASK;

	writel(reg, regs + EXYNOS3250_CHROMA_CR_XY_OFFSET);
}

void exynos3250_jpeg_coef(void __iomem *base, unsigned int mode)
{
	if (mode == S5P_JPEG_ENCODE) {
		writel(EXYNOS3250_JPEG_ENC_COEF1,
					base + EXYNOS3250_JPG_COEF(1));
		writel(EXYNOS3250_JPEG_ENC_COEF2,
					base + EXYNOS3250_JPG_COEF(2));
		writel(EXYNOS3250_JPEG_ENC_COEF3,
					base + EXYNOS3250_JPG_COEF(3));
	} else {
		writel(EXYNOS3250_JPEG_DEC_COEF1,
					base + EXYNOS3250_JPG_COEF(1));
		writel(EXYNOS3250_JPEG_DEC_COEF2,
					base + EXYNOS3250_JPG_COEF(2));
		writel(EXYNOS3250_JPEG_DEC_COEF3,
					base + EXYNOS3250_JPG_COEF(3));
	}
}

void exynos3250_jpeg_start(void __iomem *regs)
{
	writel(1, regs + EXYNOS3250_JSTART);
}

void exynos3250_jpeg_rstart(void __iomem *regs)
{
	writel(1, regs + EXYNOS3250_JRSTART);
}

unsigned int exynos3250_jpeg_get_int_status(void __iomem *regs)
{
	return readl(regs + EXYNOS3250_JPGINTST);
}

void exynos3250_jpeg_clear_int_status(void __iomem *regs,
						unsigned int value)
{
	return writel(value, regs + EXYNOS3250_JPGINTST);
}

unsigned int exynos3250_jpeg_operating(void __iomem *regs)
{
	return readl(regs + S5P_JPGOPR) & EXYNOS3250_JPGOPR_MASK;
}

unsigned int exynos3250_jpeg_compressed_size(void __iomem *regs)
{
	return readl(regs + EXYNOS3250_JPGCNT) & EXYNOS3250_JPGCNT_MASK;
}

void exynos3250_jpeg_dec_stream_size(void __iomem *regs,
						unsigned int size)
{
	writel(size & EXYNOS3250_DEC_STREAM_MASK,
				regs + EXYNOS3250_DEC_STREAM_SIZE);
}

void exynos3250_jpeg_dec_scaling_ratio(void __iomem *regs,
						unsigned int sratio)
{
	switch (sratio) {
	case 1:
	default:
		sratio = EXYNOS3250_DEC_SCALE_FACTOR_8_8;
		break;
	case 2:
		sratio = EXYNOS3250_DEC_SCALE_FACTOR_4_8;
		break;
	case 4:
		sratio = EXYNOS3250_DEC_SCALE_FACTOR_2_8;
		break;
	case 8:
		sratio = EXYNOS3250_DEC_SCALE_FACTOR_1_8;
		break;
	}

	writel(sratio & EXYNOS3250_DEC_SCALE_FACTOR_MASK,
				regs + EXYNOS3250_DEC_SCALING_RATIO);
}

void exynos3250_jpeg_set_timer(void __iomem *regs, unsigned int time_value)
{
	time_value &= EXYNOS3250_TIMER_INIT_MASK;

	writel(EXYNOS3250_TIMER_INT_STAT | time_value,
					regs + EXYNOS3250_TIMER_SE);
}

unsigned int exynos3250_jpeg_get_timer_status(void __iomem *regs)
{
	return readl(regs + EXYNOS3250_TIMER_ST);
}

void exynos3250_jpeg_clear_timer_status(void __iomem *regs)
{
	writel(EXYNOS3250_TIMER_INT_STAT, regs + EXYNOS3250_TIMER_ST);
}
