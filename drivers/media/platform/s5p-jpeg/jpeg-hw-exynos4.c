/* Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * Register interface file for JPEG driver on Exynos4x12.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/io.h>
#include <linux/delay.h>

#include "jpeg-core.h"
#include "jpeg-hw-exynos4.h"
#include "jpeg-regs.h"

void exynos4_jpeg_sw_reset(void __iomem *base)
{
	unsigned int reg;

	reg = readl(base + EXYNOS4_JPEG_CNTL_REG);
	writel(reg & ~EXYNOS4_SOFT_RESET_HI, base + EXYNOS4_JPEG_CNTL_REG);

	udelay(100);

	writel(reg | EXYNOS4_SOFT_RESET_HI, base + EXYNOS4_JPEG_CNTL_REG);
}

void exynos4_jpeg_set_enc_dec_mode(void __iomem *base, unsigned int mode)
{
	unsigned int reg;

	reg = readl(base + EXYNOS4_JPEG_CNTL_REG);
	/* set exynos4_jpeg mod register */
	if (mode == S5P_JPEG_DECODE) {
		writel((reg & EXYNOS4_ENC_DEC_MODE_MASK) |
					EXYNOS4_DEC_MODE,
			base + EXYNOS4_JPEG_CNTL_REG);
	} else {/* encode */
		writel((reg & EXYNOS4_ENC_DEC_MODE_MASK) |
					EXYNOS4_ENC_MODE,
			base + EXYNOS4_JPEG_CNTL_REG);
	}
}

void exynos4_jpeg_set_img_fmt(void __iomem *base, unsigned int img_fmt)
{
	unsigned int reg;

	reg = readl(base + EXYNOS4_IMG_FMT_REG) &
			EXYNOS4_ENC_IN_FMT_MASK; /* clear except enc format */

	switch (img_fmt) {
	case V4L2_PIX_FMT_GREY:
		reg = reg | EXYNOS4_ENC_GRAY_IMG | EXYNOS4_GRAY_IMG_IP;
		break;
	case V4L2_PIX_FMT_RGB32:
		reg = reg | EXYNOS4_ENC_RGB_IMG |
				EXYNOS4_RGB_IP_RGB_32BIT_IMG;
		break;
	case V4L2_PIX_FMT_RGB565:
		reg = reg | EXYNOS4_ENC_RGB_IMG |
				EXYNOS4_RGB_IP_RGB_16BIT_IMG;
		break;
	case V4L2_PIX_FMT_NV24:
		reg = reg | EXYNOS4_ENC_YUV_444_IMG |
				EXYNOS4_YUV_444_IP_YUV_444_2P_IMG |
				EXYNOS4_SWAP_CHROMA_CBCR;
		break;
	case V4L2_PIX_FMT_NV42:
		reg = reg | EXYNOS4_ENC_YUV_444_IMG |
				EXYNOS4_YUV_444_IP_YUV_444_2P_IMG |
				EXYNOS4_SWAP_CHROMA_CRCB;
		break;
	case V4L2_PIX_FMT_YUYV:
		reg = reg | EXYNOS4_DEC_YUV_422_IMG |
				EXYNOS4_YUV_422_IP_YUV_422_1P_IMG |
				EXYNOS4_SWAP_CHROMA_CBCR;
		break;

	case V4L2_PIX_FMT_YVYU:
		reg = reg | EXYNOS4_DEC_YUV_422_IMG |
				EXYNOS4_YUV_422_IP_YUV_422_1P_IMG |
				EXYNOS4_SWAP_CHROMA_CRCB;
		break;
	case V4L2_PIX_FMT_NV16:
		reg = reg | EXYNOS4_DEC_YUV_422_IMG |
				EXYNOS4_YUV_422_IP_YUV_422_2P_IMG |
				EXYNOS4_SWAP_CHROMA_CBCR;
		break;
	case V4L2_PIX_FMT_NV61:
		reg = reg | EXYNOS4_DEC_YUV_422_IMG |
				EXYNOS4_YUV_422_IP_YUV_422_2P_IMG |
				EXYNOS4_SWAP_CHROMA_CRCB;
		break;
	case V4L2_PIX_FMT_NV12:
		reg = reg | EXYNOS4_DEC_YUV_420_IMG |
				EXYNOS4_YUV_420_IP_YUV_420_2P_IMG |
				EXYNOS4_SWAP_CHROMA_CBCR;
		break;
	case V4L2_PIX_FMT_NV21:
		reg = reg | EXYNOS4_DEC_YUV_420_IMG |
				EXYNOS4_YUV_420_IP_YUV_420_2P_IMG |
				EXYNOS4_SWAP_CHROMA_CRCB;
		break;
	case V4L2_PIX_FMT_YUV420:
		reg = reg | EXYNOS4_DEC_YUV_420_IMG |
				EXYNOS4_YUV_420_IP_YUV_420_3P_IMG |
				EXYNOS4_SWAP_CHROMA_CBCR;
		break;
	default:
		break;

	}

	writel(reg, base + EXYNOS4_IMG_FMT_REG);
}

void exynos4_jpeg_set_enc_out_fmt(void __iomem *base, unsigned int out_fmt)
{
	unsigned int reg;

	reg = readl(base + EXYNOS4_IMG_FMT_REG) &
			~EXYNOS4_ENC_FMT_MASK; /* clear enc format */

	switch (out_fmt) {
	case V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY:
		reg = reg | EXYNOS4_ENC_FMT_GRAY;
		break;

	case V4L2_JPEG_CHROMA_SUBSAMPLING_444:
		reg = reg | EXYNOS4_ENC_FMT_YUV_444;
		break;

	case V4L2_JPEG_CHROMA_SUBSAMPLING_422:
		reg = reg | EXYNOS4_ENC_FMT_YUV_422;
		break;

	case V4L2_JPEG_CHROMA_SUBSAMPLING_420:
		reg = reg | EXYNOS4_ENC_FMT_YUV_420;
		break;

	default:
		break;
	}

	writel(reg, base + EXYNOS4_IMG_FMT_REG);
}

void exynos4_jpeg_set_interrupt(void __iomem *base)
{
	writel(EXYNOS4_INT_EN_ALL, base + EXYNOS4_INT_EN_REG);
}

unsigned int exynos4_jpeg_get_int_status(void __iomem *base)
{
	unsigned int	int_status;

	int_status = readl(base + EXYNOS4_INT_STATUS_REG);

	return int_status;
}

unsigned int exynos4_jpeg_get_fifo_status(void __iomem *base)
{
	unsigned int fifo_status;

	fifo_status = readl(base + EXYNOS4_FIFO_STATUS_REG);

	return fifo_status;
}

void exynos4_jpeg_set_huf_table_enable(void __iomem *base, int value)
{
	unsigned int	reg;

	reg = readl(base + EXYNOS4_JPEG_CNTL_REG) & ~EXYNOS4_HUF_TBL_EN;

	if (value == 1)
		writel(reg | EXYNOS4_HUF_TBL_EN,
					base + EXYNOS4_JPEG_CNTL_REG);
	else
		writel(reg & ~EXYNOS4_HUF_TBL_EN,
					base + EXYNOS4_JPEG_CNTL_REG);
}

void exynos4_jpeg_set_sys_int_enable(void __iomem *base, int value)
{
	unsigned int	reg;

	reg = readl(base + EXYNOS4_JPEG_CNTL_REG) & ~(EXYNOS4_SYS_INT_EN);

	if (value == 1)
		writel(reg | EXYNOS4_SYS_INT_EN, base + EXYNOS4_JPEG_CNTL_REG);
	else
		writel(reg & ~EXYNOS4_SYS_INT_EN, base + EXYNOS4_JPEG_CNTL_REG);
}

void exynos4_jpeg_set_stream_buf_address(void __iomem *base,
					 unsigned int address)
{
	writel(address, base + EXYNOS4_OUT_MEM_BASE_REG);
}

void exynos4_jpeg_set_stream_size(void __iomem *base,
		unsigned int x_value, unsigned int y_value)
{
	writel(0x0, base + EXYNOS4_JPEG_IMG_SIZE_REG); /* clear */
	writel(EXYNOS4_X_SIZE(x_value) | EXYNOS4_Y_SIZE(y_value),
			base + EXYNOS4_JPEG_IMG_SIZE_REG);
}

void exynos4_jpeg_set_frame_buf_address(void __iomem *base,
				struct s5p_jpeg_addr *exynos4_jpeg_addr)
{
	writel(exynos4_jpeg_addr->y, base + EXYNOS4_IMG_BA_PLANE_1_REG);
	writel(exynos4_jpeg_addr->cb, base + EXYNOS4_IMG_BA_PLANE_2_REG);
	writel(exynos4_jpeg_addr->cr, base + EXYNOS4_IMG_BA_PLANE_3_REG);
}

void exynos4_jpeg_set_encode_tbl_select(void __iomem *base,
		enum exynos4_jpeg_img_quality_level level)
{
	unsigned int	reg;

	reg = EXYNOS4_Q_TBL_COMP1_0 | EXYNOS4_Q_TBL_COMP2_1 |
		EXYNOS4_Q_TBL_COMP3_1 |
		EXYNOS4_HUFF_TBL_COMP1_AC_0_DC_1 |
		EXYNOS4_HUFF_TBL_COMP2_AC_0_DC_0 |
		EXYNOS4_HUFF_TBL_COMP3_AC_1_DC_1;

	writel(reg, base + EXYNOS4_TBL_SEL_REG);
}

void exynos4_jpeg_set_encode_hoff_cnt(void __iomem *base, unsigned int fmt)
{
	if (fmt == V4L2_PIX_FMT_GREY)
		writel(0xd2, base + EXYNOS4_HUFF_CNT_REG);
	else
		writel(0x1a2, base + EXYNOS4_HUFF_CNT_REG);
}

unsigned int exynos4_jpeg_get_stream_size(void __iomem *base)
{
	unsigned int size;

	size = readl(base + EXYNOS4_BITSTREAM_SIZE_REG);
	return size;
}

void exynos4_jpeg_set_dec_bitstream_size(void __iomem *base, unsigned int size)
{
	writel(size, base + EXYNOS4_BITSTREAM_SIZE_REG);
}

void exynos4_jpeg_get_frame_size(void __iomem *base,
			unsigned int *width, unsigned int *height)
{
	*width = (readl(base + EXYNOS4_DECODE_XY_SIZE_REG) &
				EXYNOS4_DECODED_SIZE_MASK);
	*height = (readl(base + EXYNOS4_DECODE_XY_SIZE_REG) >> 16) &
				EXYNOS4_DECODED_SIZE_MASK;
}

unsigned int exynos4_jpeg_get_frame_fmt(void __iomem *base)
{
	return readl(base + EXYNOS4_DECODE_IMG_FMT_REG) &
				EXYNOS4_JPEG_DECODED_IMG_FMT_MASK;
}

void exynos4_jpeg_set_timer_count(void __iomem *base, unsigned int size)
{
	writel(size, base + EXYNOS4_INT_TIMER_COUNT_REG);
}
