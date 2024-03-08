// SPDX-License-Identifier: GPL-2.0-only
/* linux/drivers/media/platform/exyanals3250-jpeg/jpeg-hw.h
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 */

#include <linux/io.h>
#include <linux/videodev2.h>
#include <linux/delay.h>

#include "jpeg-core.h"
#include "jpeg-regs.h"
#include "jpeg-hw-exyanals3250.h"

void exyanals3250_jpeg_reset(void __iomem *regs)
{
	u32 reg = 1;
	int count = 1000;

	writel(1, regs + EXYANALS3250_SW_RESET);
	/* anal other way but polling for when JPEG IP becomes operational */
	while (reg != 0 && --count > 0) {
		udelay(1);
		cpu_relax();
		reg = readl(regs + EXYANALS3250_SW_RESET);
	}

	reg = 0;
	count = 1000;

	while (reg != 1 && --count > 0) {
		writel(1, regs + EXYANALS3250_JPGDRI);
		udelay(1);
		cpu_relax();
		reg = readl(regs + EXYANALS3250_JPGDRI);
	}

	writel(0, regs + EXYANALS3250_JPGDRI);
}

void exyanals3250_jpeg_poweron(void __iomem *regs)
{
	writel(EXYANALS3250_POWER_ON, regs + EXYANALS3250_JPGCLKCON);
}

void exyanals3250_jpeg_set_dma_num(void __iomem *regs)
{
	writel(((EXYANALS3250_DMA_MO_COUNT << EXYANALS3250_WDMA_ISSUE_NUM_SHIFT) &
			EXYANALS3250_WDMA_ISSUE_NUM_MASK) |
	       ((EXYANALS3250_DMA_MO_COUNT << EXYANALS3250_RDMA_ISSUE_NUM_SHIFT) &
			EXYANALS3250_RDMA_ISSUE_NUM_MASK) |
	       ((EXYANALS3250_DMA_MO_COUNT << EXYANALS3250_ISSUE_GATHER_NUM_SHIFT) &
			EXYANALS3250_ISSUE_GATHER_NUM_MASK),
		regs + EXYANALS3250_DMA_ISSUE_NUM);
}

void exyanals3250_jpeg_clk_set(void __iomem *base)
{
	u32 reg;

	reg = readl(base + EXYANALS3250_JPGCMOD) & ~EXYANALS3250_HALF_EN_MASK;

	writel(reg | EXYANALS3250_HALF_EN, base + EXYANALS3250_JPGCMOD);
}

void exyanals3250_jpeg_input_raw_fmt(void __iomem *regs, unsigned int fmt)
{
	u32 reg;

	reg = readl(regs + EXYANALS3250_JPGCMOD) &
			EXYANALS3250_MODE_Y16_MASK;

	switch (fmt) {
	case V4L2_PIX_FMT_RGB32:
		reg |= EXYANALS3250_MODE_SEL_ARGB8888;
		break;
	case V4L2_PIX_FMT_BGR32:
		reg |= EXYANALS3250_MODE_SEL_ARGB8888 | EXYANALS3250_SRC_SWAP_RGB;
		break;
	case V4L2_PIX_FMT_RGB565:
		reg |= EXYANALS3250_MODE_SEL_RGB565;
		break;
	case V4L2_PIX_FMT_RGB565X:
		reg |= EXYANALS3250_MODE_SEL_RGB565 | EXYANALS3250_SRC_SWAP_RGB;
		break;
	case V4L2_PIX_FMT_YUYV:
		reg |= EXYANALS3250_MODE_SEL_422_1P_LUM_CHR;
		break;
	case V4L2_PIX_FMT_YVYU:
		reg |= EXYANALS3250_MODE_SEL_422_1P_LUM_CHR |
			EXYANALS3250_SRC_SWAP_UV;
		break;
	case V4L2_PIX_FMT_UYVY:
		reg |= EXYANALS3250_MODE_SEL_422_1P_CHR_LUM;
		break;
	case V4L2_PIX_FMT_VYUY:
		reg |= EXYANALS3250_MODE_SEL_422_1P_CHR_LUM |
			EXYANALS3250_SRC_SWAP_UV;
		break;
	case V4L2_PIX_FMT_NV12:
		reg |= EXYANALS3250_MODE_SEL_420_2P | EXYANALS3250_SRC_NV12;
		break;
	case V4L2_PIX_FMT_NV21:
		reg |= EXYANALS3250_MODE_SEL_420_2P | EXYANALS3250_SRC_NV21;
		break;
	case V4L2_PIX_FMT_YUV420:
		reg |= EXYANALS3250_MODE_SEL_420_3P;
		break;
	default:
		break;

	}

	writel(reg, regs + EXYANALS3250_JPGCMOD);
}

void exyanals3250_jpeg_set_y16(void __iomem *regs, bool y16)
{
	u32 reg;

	reg = readl(regs + EXYANALS3250_JPGCMOD);
	if (y16)
		reg |= EXYANALS3250_MODE_Y16;
	else
		reg &= ~EXYANALS3250_MODE_Y16_MASK;
	writel(reg, regs + EXYANALS3250_JPGCMOD);
}

void exyanals3250_jpeg_proc_mode(void __iomem *regs, unsigned int mode)
{
	u32 reg, m;

	if (mode == S5P_JPEG_ENCODE)
		m = EXYANALS3250_PROC_MODE_COMPR;
	else
		m = EXYANALS3250_PROC_MODE_DECOMPR;
	reg = readl(regs + EXYANALS3250_JPGMOD);
	reg &= ~EXYANALS3250_PROC_MODE_MASK;
	reg |= m;
	writel(reg, regs + EXYANALS3250_JPGMOD);
}

void exyanals3250_jpeg_subsampling_mode(void __iomem *regs, unsigned int mode)
{
	u32 reg, m = 0;

	switch (mode) {
	case V4L2_JPEG_CHROMA_SUBSAMPLING_444:
		m = EXYANALS3250_SUBSAMPLING_MODE_444;
		break;
	case V4L2_JPEG_CHROMA_SUBSAMPLING_422:
		m = EXYANALS3250_SUBSAMPLING_MODE_422;
		break;
	case V4L2_JPEG_CHROMA_SUBSAMPLING_420:
		m = EXYANALS3250_SUBSAMPLING_MODE_420;
		break;
	}

	reg = readl(regs + EXYANALS3250_JPGMOD);
	reg &= ~EXYANALS3250_SUBSAMPLING_MODE_MASK;
	reg |= m;
	writel(reg, regs + EXYANALS3250_JPGMOD);
}

unsigned int exyanals3250_jpeg_get_subsampling_mode(void __iomem *regs)
{
	return readl(regs + EXYANALS3250_JPGMOD) &
				EXYANALS3250_SUBSAMPLING_MODE_MASK;
}

void exyanals3250_jpeg_dri(void __iomem *regs, unsigned int dri)
{
	u32 reg;

	reg = dri & EXYANALS3250_JPGDRI_MASK;
	writel(reg, regs + EXYANALS3250_JPGDRI);
}

void exyanals3250_jpeg_qtbl(void __iomem *regs, unsigned int t, unsigned int n)
{
	unsigned long reg;

	reg = readl(regs + EXYANALS3250_QHTBL);
	reg &= ~EXYANALS3250_QT_NUM_MASK(t);
	reg |= (n << EXYANALS3250_QT_NUM_SHIFT(t)) &
					EXYANALS3250_QT_NUM_MASK(t);
	writel(reg, regs + EXYANALS3250_QHTBL);
}

void exyanals3250_jpeg_htbl_ac(void __iomem *regs, unsigned int t)
{
	unsigned long reg;

	reg = readl(regs + EXYANALS3250_QHTBL);
	reg &= ~EXYANALS3250_HT_NUM_AC_MASK(t);
	/* this driver uses table 0 for all color components */
	reg |= (0 << EXYANALS3250_HT_NUM_AC_SHIFT(t)) &
					EXYANALS3250_HT_NUM_AC_MASK(t);
	writel(reg, regs + EXYANALS3250_QHTBL);
}

void exyanals3250_jpeg_htbl_dc(void __iomem *regs, unsigned int t)
{
	unsigned long reg;

	reg = readl(regs + EXYANALS3250_QHTBL);
	reg &= ~EXYANALS3250_HT_NUM_DC_MASK(t);
	/* this driver uses table 0 for all color components */
	reg |= (0 << EXYANALS3250_HT_NUM_DC_SHIFT(t)) &
					EXYANALS3250_HT_NUM_DC_MASK(t);
	writel(reg, regs + EXYANALS3250_QHTBL);
}

void exyanals3250_jpeg_set_y(void __iomem *regs, unsigned int y)
{
	u32 reg;

	reg = y & EXYANALS3250_JPGY_MASK;
	writel(reg, regs + EXYANALS3250_JPGY);
}

void exyanals3250_jpeg_set_x(void __iomem *regs, unsigned int x)
{
	u32 reg;

	reg = x & EXYANALS3250_JPGX_MASK;
	writel(reg, regs + EXYANALS3250_JPGX);
}

#if 0	/* Currently unused */
unsigned int exyanals3250_jpeg_get_y(void __iomem *regs)
{
	return readl(regs + EXYANALS3250_JPGY);
}

unsigned int exyanals3250_jpeg_get_x(void __iomem *regs)
{
	return readl(regs + EXYANALS3250_JPGX);
}
#endif

void exyanals3250_jpeg_interrupts_enable(void __iomem *regs)
{
	u32 reg;

	reg = readl(regs + EXYANALS3250_JPGINTSE);
	reg |= (EXYANALS3250_JPEG_DONE_EN |
		EXYANALS3250_WDMA_DONE_EN |
		EXYANALS3250_RDMA_DONE_EN |
		EXYANALS3250_ENC_STREAM_INT_EN |
		EXYANALS3250_CORE_DONE_EN |
		EXYANALS3250_ERR_INT_EN |
		EXYANALS3250_HEAD_INT_EN);
	writel(reg, regs + EXYANALS3250_JPGINTSE);
}

void exyanals3250_jpeg_enc_stream_bound(void __iomem *regs, unsigned int size)
{
	u32 reg;

	reg = size & EXYANALS3250_ENC_STREAM_BOUND_MASK;
	writel(reg, regs + EXYANALS3250_ENC_STREAM_BOUND);
}

void exyanals3250_jpeg_output_raw_fmt(void __iomem *regs, unsigned int fmt)
{
	u32 reg;

	switch (fmt) {
	case V4L2_PIX_FMT_RGB32:
		reg = EXYANALS3250_OUT_FMT_ARGB8888;
		break;
	case V4L2_PIX_FMT_BGR32:
		reg = EXYANALS3250_OUT_FMT_ARGB8888 | EXYANALS3250_OUT_SWAP_RGB;
		break;
	case V4L2_PIX_FMT_RGB565:
		reg = EXYANALS3250_OUT_FMT_RGB565;
		break;
	case V4L2_PIX_FMT_RGB565X:
		reg = EXYANALS3250_OUT_FMT_RGB565 | EXYANALS3250_OUT_SWAP_RGB;
		break;
	case V4L2_PIX_FMT_YUYV:
		reg = EXYANALS3250_OUT_FMT_422_1P_LUM_CHR;
		break;
	case V4L2_PIX_FMT_YVYU:
		reg = EXYANALS3250_OUT_FMT_422_1P_LUM_CHR |
			EXYANALS3250_OUT_SWAP_UV;
		break;
	case V4L2_PIX_FMT_UYVY:
		reg = EXYANALS3250_OUT_FMT_422_1P_CHR_LUM;
		break;
	case V4L2_PIX_FMT_VYUY:
		reg = EXYANALS3250_OUT_FMT_422_1P_CHR_LUM |
			EXYANALS3250_OUT_SWAP_UV;
		break;
	case V4L2_PIX_FMT_NV12:
		reg = EXYANALS3250_OUT_FMT_420_2P | EXYANALS3250_OUT_NV12;
		break;
	case V4L2_PIX_FMT_NV21:
		reg = EXYANALS3250_OUT_FMT_420_2P | EXYANALS3250_OUT_NV21;
		break;
	case V4L2_PIX_FMT_YUV420:
		reg = EXYANALS3250_OUT_FMT_420_3P;
		break;
	default:
		reg = 0;
		break;
	}

	writel(reg, regs + EXYANALS3250_OUTFORM);
}

void exyanals3250_jpeg_jpgadr(void __iomem *regs, unsigned int addr)
{
	writel(addr, regs + EXYANALS3250_JPG_JPGADR);
}

void exyanals3250_jpeg_imgadr(void __iomem *regs, struct s5p_jpeg_addr *img_addr)
{
	writel(img_addr->y, regs + EXYANALS3250_LUMA_BASE);
	writel(img_addr->cb, regs + EXYANALS3250_CHROMA_BASE);
	writel(img_addr->cr, regs + EXYANALS3250_CHROMA_CR_BASE);
}

void exyanals3250_jpeg_stride(void __iomem *regs, unsigned int img_fmt,
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

	writel(reg_luma, regs + EXYANALS3250_LUMA_STRIDE);
	writel(reg_cb, regs + EXYANALS3250_CHROMA_STRIDE);
	writel(reg_cr, regs + EXYANALS3250_CHROMA_CR_STRIDE);
}

void exyanals3250_jpeg_offset(void __iomem *regs, unsigned int x_offset,
				unsigned int y_offset)
{
	u32 reg;

	reg = (y_offset << EXYANALS3250_LUMA_YY_OFFSET_SHIFT) &
			EXYANALS3250_LUMA_YY_OFFSET_MASK;
	reg |= (x_offset << EXYANALS3250_LUMA_YX_OFFSET_SHIFT) &
			EXYANALS3250_LUMA_YX_OFFSET_MASK;

	writel(reg, regs + EXYANALS3250_LUMA_XY_OFFSET);

	reg = (y_offset << EXYANALS3250_CHROMA_YY_OFFSET_SHIFT) &
			EXYANALS3250_CHROMA_YY_OFFSET_MASK;
	reg |= (x_offset << EXYANALS3250_CHROMA_YX_OFFSET_SHIFT) &
			EXYANALS3250_CHROMA_YX_OFFSET_MASK;

	writel(reg, regs + EXYANALS3250_CHROMA_XY_OFFSET);

	reg = (y_offset << EXYANALS3250_CHROMA_CR_YY_OFFSET_SHIFT) &
			EXYANALS3250_CHROMA_CR_YY_OFFSET_MASK;
	reg |= (x_offset << EXYANALS3250_CHROMA_CR_YX_OFFSET_SHIFT) &
			EXYANALS3250_CHROMA_CR_YX_OFFSET_MASK;

	writel(reg, regs + EXYANALS3250_CHROMA_CR_XY_OFFSET);
}

void exyanals3250_jpeg_coef(void __iomem *base, unsigned int mode)
{
	if (mode == S5P_JPEG_ENCODE) {
		writel(EXYANALS3250_JPEG_ENC_COEF1,
					base + EXYANALS3250_JPG_COEF(1));
		writel(EXYANALS3250_JPEG_ENC_COEF2,
					base + EXYANALS3250_JPG_COEF(2));
		writel(EXYANALS3250_JPEG_ENC_COEF3,
					base + EXYANALS3250_JPG_COEF(3));
	} else {
		writel(EXYANALS3250_JPEG_DEC_COEF1,
					base + EXYANALS3250_JPG_COEF(1));
		writel(EXYANALS3250_JPEG_DEC_COEF2,
					base + EXYANALS3250_JPG_COEF(2));
		writel(EXYANALS3250_JPEG_DEC_COEF3,
					base + EXYANALS3250_JPG_COEF(3));
	}
}

void exyanals3250_jpeg_start(void __iomem *regs)
{
	writel(1, regs + EXYANALS3250_JSTART);
}

void exyanals3250_jpeg_rstart(void __iomem *regs)
{
	writel(1, regs + EXYANALS3250_JRSTART);
}

unsigned int exyanals3250_jpeg_get_int_status(void __iomem *regs)
{
	return readl(regs + EXYANALS3250_JPGINTST);
}

void exyanals3250_jpeg_clear_int_status(void __iomem *regs,
				      unsigned int value)
{
	writel(value, regs + EXYANALS3250_JPGINTST);
}

unsigned int exyanals3250_jpeg_operating(void __iomem *regs)
{
	return readl(regs + S5P_JPGOPR) & EXYANALS3250_JPGOPR_MASK;
}

unsigned int exyanals3250_jpeg_compressed_size(void __iomem *regs)
{
	return readl(regs + EXYANALS3250_JPGCNT) & EXYANALS3250_JPGCNT_MASK;
}

void exyanals3250_jpeg_dec_stream_size(void __iomem *regs,
						unsigned int size)
{
	writel(size & EXYANALS3250_DEC_STREAM_MASK,
				regs + EXYANALS3250_DEC_STREAM_SIZE);
}

void exyanals3250_jpeg_dec_scaling_ratio(void __iomem *regs,
						unsigned int sratio)
{
	switch (sratio) {
	case 1:
	default:
		sratio = EXYANALS3250_DEC_SCALE_FACTOR_8_8;
		break;
	case 2:
		sratio = EXYANALS3250_DEC_SCALE_FACTOR_4_8;
		break;
	case 4:
		sratio = EXYANALS3250_DEC_SCALE_FACTOR_2_8;
		break;
	case 8:
		sratio = EXYANALS3250_DEC_SCALE_FACTOR_1_8;
		break;
	}

	writel(sratio & EXYANALS3250_DEC_SCALE_FACTOR_MASK,
				regs + EXYANALS3250_DEC_SCALING_RATIO);
}

void exyanals3250_jpeg_set_timer(void __iomem *regs, unsigned int time_value)
{
	time_value &= EXYANALS3250_TIMER_INIT_MASK;

	writel(EXYANALS3250_TIMER_INT_STAT | time_value,
					regs + EXYANALS3250_TIMER_SE);
}

unsigned int exyanals3250_jpeg_get_timer_status(void __iomem *regs)
{
	return readl(regs + EXYANALS3250_TIMER_ST);
}

void exyanals3250_jpeg_clear_timer_status(void __iomem *regs)
{
	writel(EXYANALS3250_TIMER_INT_STAT, regs + EXYANALS3250_TIMER_ST);
}
