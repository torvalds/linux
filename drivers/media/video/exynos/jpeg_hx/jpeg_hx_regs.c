/* linux/drivers/media/video/exynos/jpeg_hx/jpeg_hx_regs.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Register interface file for jpeg hx driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/io.h>
#include <linux/delay.h>

#include "jpeg_hx_regs.h"
#include "jpeg_hx_conf.h"
#include "jpeg_hx_core.h"
#include "regs_jpeg_hx.h"

#define GET_8BYTE_ALIGHNED_SIZE(size)	(((size) % 8 != 0) ? ((size) / 8 + 1) * 8 : (size))

void jpeg_hx_sw_reset(void __iomem *base)
{
	writel(JPEG_SW_RESET_ENABLE,
			base + JPEG_SW_RESET_REG);
	while (readl(base + JPEG_SW_RESET_REG)) {
		ndelay(100);
	}
}

void jpeg_hx_set_dma_num(void __iomem *base)
{
	writel(MO_COUNT << 16 | MO_COUNT << 8 | MO_COUNT << 0,
			base + JPEG_DMA_ISSUE_NUM_REG);
}

void jpeg_hx_clk_on(void __iomem *base)
{
	unsigned int reg;

	reg = readl(base + JPEG_CLK_CON_REG);

	writel(reg | JPEG_CLK_ON,
			base + JPEG_CLK_CON_REG);
}

void jpeg_hx_clk_off(void __iomem *base)
{
	unsigned int reg;

	reg = readl(base + JPEG_CLK_CON_REG);

	writel(reg & ~JPEG_CLK_ON,
			base + JPEG_CLK_CON_REG);
}

void jpeg_hx_clk_set(void __iomem *base, enum jpeg_clk_mode mode)
{
	unsigned int reg;

	reg = readl(base + JPEG_CMOD_REG) & ~JPEG_HALF_EN_MASK;

	writel(reg | mode,
			base + JPEG_CMOD_REG);
}

void jpeg_hx_set_dec_out_fmt(void __iomem *base,
					enum jpeg_frame_format out_fmt)
{
	unsigned int reg = 0;

	reg = readl(base + JPEG_OUT_FORMAT_REG) & ~(JPEG_DEC_OUT_FORMAT_MASK | JPEG_OUT_NV_MASK);

	/* set jpeg deocde ouput format register */
	switch (out_fmt) {
	case YCBYCR_422_1P:
		reg = reg | JPEG_DEC_YUYV;
		break;

	case CBYCRY_422_1P:
		reg = reg | JPEG_DEC_UYVY;
		break;

	case YCBCR_420_2P:
		reg = reg | JPEG_DEC_YUV_420 | JPEG_OUT_NV_12;
		break;

	case YCRCB_420_2P:
		reg = reg | JPEG_DEC_YUV_420 | JPEG_OUT_NV_21;
		break;

	default:
		break;
	}

	writel(reg, base + JPEG_OUT_FORMAT_REG);
}

void jpeg_hx_set_enc_dec_mode(void __iomem *base, enum jpeg_mode mode)
{
	unsigned int reg;

	reg = readl(base + JPEG_MOD_REG) & ~JPEG_PROC_MODE_MASK;

	/* set jpeg mod register */
	if (mode == DECODING) {
		writel(reg | JPEG_PROC_DEC,
			base + JPEG_MOD_REG);
	} else {/* encode */
		writel(reg | JPEG_PROC_ENC,
			base + JPEG_MOD_REG);
	}
}

void jpeg_hx_set_dec_bitstream_size(void __iomem *base, unsigned int size)
{
	writel(0, base + JPEG_DEC_STREAM_SIZE_REG);
	writel(size, base + JPEG_DEC_STREAM_SIZE_REG);
}

void jpeg_hx_color_mode_select(void __iomem *base, enum jpeg_frame_format out_fmt)
{
	unsigned int reg = 0;

	reg = readl(base + JPEG_CMOD_REG) & ~JPEG_MOD_SEL_MASK;

	/* set jpeg deocde ouput format register */
	switch (out_fmt) {
	case YCBYCR_422_1P:
		reg = reg | JPEG_MOD_YUV_422;
		break;

	case CBYCRY_422_1P:
		reg = reg | JPEG_MOD_YUV_422_2YUV;
		break;

	case YCBCR_420_2P:
	case YCRCB_420_2P:
		reg = reg | JPEG_MOD_YUV_420;
		break;

	default:
		break;
	}

	writel(reg, base + JPEG_CMOD_REG);
}

void jpeg_hx_set_interrupt(void __iomem *base)
{
	unsigned int reg;

	reg = readl(base + JPEG_INT_SET_REG);
	writel(reg | JPEG_ALL_INT_EN, base + JPEG_INT_SET_REG);
}

void jpeg_hx_set_stream_buf_address(void __iomem *base, unsigned int address)
{
	writel(address, base + JPEG_IMG_ADDRESS_REG);
}

void jpeg_hx_set_frame_buf_address(void __iomem *base, enum jpeg_frame_format fmt,
		unsigned int address, unsigned int width, unsigned int height)
{
	switch (fmt) {
	case RGB_565:
	case YCBYCR_422_1P:
	case CBYCRY_422_1P:
		writel(address, base + JPEG_LUMA_BASE_REG);
		writel(0, base + JPEG_CHROMA_BASE_REG);
		break;
	case YCBCR_420_2P:
	case YCRCB_420_2P:
		writel(address, base + JPEG_LUMA_BASE_REG);
		writel(address + (width * height), base + JPEG_CHROMA_BASE_REG);
		break;
	default:
		break;
	}
}

void jpeg_hx_set_enc_luma_stride(void __iomem *base, unsigned int w_stride, enum jpeg_frame_format fmt)
{
	unsigned int byteperpixel;
	unsigned int stride;

	switch (fmt) {
	case RGB_565:
	case YCRYCB_422_1P:
	case YCBYCR_422_1P:
	case YCRCB_422_2P:
	case YCBCR_422_2P:
	case YCBYCR_422_3P:
		byteperpixel = 2;
		break;
	default:
		byteperpixel = 1;
		break;
	}
	stride = GET_8BYTE_ALIGHNED_SIZE(w_stride * byteperpixel);
	writel(stride, base + JPEG_LUMA_STRIDE_REG);
}

void jpeg_hx_set_enc_cbcr_stride(void __iomem *base, unsigned int w_stride, enum jpeg_frame_format fmt)
{
	unsigned int byteperpixel;
	unsigned int stride;

	switch (fmt) {
	case YCRYCB_422_1P:
	case YCBYCR_422_1P:
	case YCRCB_422_2P:
	case YCBCR_422_2P:
	case YCBYCR_422_3P:
		byteperpixel = 2;
		break;
	default:
		byteperpixel = 1;
		break;
	}
	stride = GET_8BYTE_ALIGHNED_SIZE(w_stride * byteperpixel);
	writel(stride, base + JPEG_CHROMA_STRIDE_REG);
}

void jpeg_hx_set_dec_luma_stride(void __iomem *base, unsigned int w_stride, enum jpeg_frame_format fmt)
{
	unsigned int byteperpixel;

	switch (fmt) {
	case YCRYCB_422_1P:
	case YCBYCR_422_1P:
	case YCRCB_422_2P:
	case YCBCR_422_2P:
	case YCBYCR_422_3P:
		byteperpixel = 2;
		break;
	default:
		byteperpixel = 1;
		break;
	}
	writel(w_stride * byteperpixel, base + JPEG_LUMA_STRIDE_REG);
}

void jpeg_hx_set_dec_cbcr_stride(void __iomem *base, unsigned int w_stride, enum jpeg_frame_format fmt)
{
	unsigned int byteperpixel;

	switch (fmt) {
	case YCRYCB_422_1P:
	case YCBYCR_422_1P:
	case YCRCB_422_2P:
	case YCBCR_422_2P:
	case YCBYCR_422_3P:
		byteperpixel = 2;
		break;
	default:
		byteperpixel = 1;
		break;
	}
	writel(w_stride * byteperpixel, base + JPEG_CHROMA_STRIDE_REG);
}

unsigned int jpeg_hx_get_int_status(void __iomem *base)
{
	return readl(base + JPEG_INT_STATUS_REG);
}

void jpeg_hx_clear_int_status(void __iomem *base, int value)
{
	writel(value, base + JPEG_INT_STATUS_REG);
}

unsigned int jpeg_hx_get_stream_size(void __iomem *base)
{
	return readl(base + JPEG_CNT_REG);
}

void jpeg_hx_set_dec_scaling(void __iomem *base,
		enum jpeg_scale_value scale_value)
{
	writel(scale_value, base + JPEG_DEC_SCALE_RATIO_REG);
}

void jpeg_hx_get_frame_size(void __iomem *base,
			unsigned int *width, unsigned int *height)
{

}
void jpeg_hx_set_timer(void __iomem *base,
		unsigned int time_value)
{
	if (time_value > 0x7fffffff)
		time_value =  0x7fffffff;
	writel(JPEG_TIMER_INT_EN | time_value, base + JPEG_DEC_SCALE_RATIO_REG);
}

void jpeg_hx_start(void __iomem *base)
{
	writel(1, base + JPEG_START_REG);
}

void jpeg_hx_re_start(void __iomem *base)
{
	writel(1, base + JPEG_RE_START_REG);
}

void jpeg_hx_coef(void __iomem *base, unsigned int i)
{
	writel(JPEG_COEF1, base + JPEG_COEF1_REG);
	writel(JPEG_COEF2, base + JPEG_COEF2_REG);
	writel(JPEG_COEF3, base + JPEG_COEF3_REG);
}

void jpeg_hx_set_qtbl(void __iomem *base, const unsigned char *qtbl,
		   unsigned long tab, int len)
{
	int i;

	for (i = 0; i < len; i++)
		writel((unsigned int)qtbl[i], base + tab + (i * 0x04));
}

void jpeg_hx_set_htbl(void __iomem *base, const unsigned char *htbl,
		   unsigned long tab, int len)
{
	int i;
	for (i = 0; i < len; i++)
		writel((unsigned int)htbl[i], base + tab + (i * 0x04));
}

void jpeg_hx_set_enc_tbl(void __iomem *base,
	enum jpeg_img_quality_level level)
{
	unsigned int i, j;

	switch (level) {
	case QUALITY_LEVEL_1:
		for (i = 0; i < 4; i++) {
			j = i % 2;
			jpeg_hx_set_qtbl(base, qtbl[0][j], JPEG_QTBL_CONTENT(j),
					ARRAY_SIZE(qtbl[0][j]));
		}
		break;

	case QUALITY_LEVEL_2:
		for (i = 0; i < 4; i++) {
			j = i % 2;
			jpeg_hx_set_qtbl(base, qtbl[1][j], JPEG_QTBL_CONTENT(j),
					ARRAY_SIZE(qtbl[1][j]));
		}
		break;

	case QUALITY_LEVEL_3:
		for (i = 0; i < 4; i++) {
			j = i % 2;
			jpeg_hx_set_qtbl(base, qtbl[2][j], JPEG_QTBL_CONTENT(j),
					ARRAY_SIZE(qtbl[2][j]));
		}
		break;

	case QUALITY_LEVEL_4:
		for (i = 0; i < 4; i++) {
			j = i % 2;
			jpeg_hx_set_qtbl(base, qtbl[3][j], JPEG_QTBL_CONTENT(j),
					ARRAY_SIZE(qtbl[3][j]));
		}
		break;

	case QUALITY_LEVEL_5:
		for (i = 0; i < 4; i++) {
			j = i % 2;
			jpeg_hx_set_qtbl(base, qtbl[4][j], JPEG_QTBL_CONTENT(j),
					ARRAY_SIZE(qtbl[4][j]));
		}
		break;

	case QUALITY_LEVEL_6:
		for (i = 0; i < 4; i++) {
			j = i % 2;
			jpeg_hx_set_qtbl(base, qtbl[5][j], JPEG_QTBL_CONTENT(j),
					ARRAY_SIZE(qtbl[5][j]));
		}
		break;

	default:
		for (i = 0; i < 4; i++) {
			j = i % 2;
			jpeg_hx_set_qtbl(base, qtbl[0][j], JPEG_QTBL_CONTENT(j),
					ARRAY_SIZE(qtbl[0][j]));
		}
		break;
	}
	jpeg_hx_set_htbl(base, len_dc_luminance, JPEG_HDCTBL(0), ARRAY_SIZE(len_dc_luminance));
	jpeg_hx_set_htbl(base, val_dc_luminance, JPEG_HDCTBLG(0), ARRAY_SIZE(val_dc_luminance));
	jpeg_hx_set_htbl(base, len_ac_luminance, JPEG_HACTBL(0), ARRAY_SIZE(len_ac_luminance));
	jpeg_hx_set_htbl(base, val_ac_luminance, JPEG_HACTBLG(0), ARRAY_SIZE(val_ac_luminance));
	jpeg_hx_set_htbl(base, len_dc_chrominance, JPEG_HDCTBL(1), ARRAY_SIZE(len_dc_chrominance));
	jpeg_hx_set_htbl(base, val_dc_chrominance, JPEG_HDCTBLG(1), ARRAY_SIZE(val_dc_chrominance));
	jpeg_hx_set_htbl(base, len_ac_chrominance, JPEG_HACTBL(1), ARRAY_SIZE(len_ac_chrominance));
	jpeg_hx_set_htbl(base, val_ac_chrominance, JPEG_HACTBLG(1), ARRAY_SIZE(val_ac_chrominance));
}

void jpeg_hx_set_encode_tbl_select(void __iomem *base,
	enum jpeg_img_quality_level level)
{

	unsigned int	reg;
	switch (level) {
	case QUALITY_LEVEL_1:
		reg = JPEG_Q_TBL_Y_0 | JPEG_Q_TBL_Cb_1 |
		JPEG_Q_TBL_Cr_1 |
		JPEG_HUFF_TBL_Y_AC_0 | JPEG_HUFF_TBL_Y_DC_0 |
		JPEG_HUFF_TBL_Cb_AC_1 | JPEG_HUFF_TBL_Cb_DC_1 |
		JPEG_HUFF_TBL_Cr_AC_1 | JPEG_HUFF_TBL_Cr_DC_1;
		break;

	case QUALITY_LEVEL_2:
		reg = JPEG_Q_TBL_Y_0 | JPEG_Q_TBL_Cb_1 |
		JPEG_Q_TBL_Cr_1 |
		JPEG_HUFF_TBL_Y_AC_0 | JPEG_HUFF_TBL_Y_DC_0 |
		JPEG_HUFF_TBL_Cb_AC_1 | JPEG_HUFF_TBL_Cb_DC_1 |
		JPEG_HUFF_TBL_Cr_AC_1 | JPEG_HUFF_TBL_Cr_DC_1;
		break;

	case QUALITY_LEVEL_3:
		reg = JPEG_Q_TBL_Y_0 | JPEG_Q_TBL_Cb_1 |
		JPEG_Q_TBL_Cr_1 |
		JPEG_HUFF_TBL_Y_AC_0 | JPEG_HUFF_TBL_Y_DC_0 |
		JPEG_HUFF_TBL_Cb_AC_1 | JPEG_HUFF_TBL_Cb_DC_1 |
		JPEG_HUFF_TBL_Cr_AC_1 | JPEG_HUFF_TBL_Cr_DC_1;
		break;

	case QUALITY_LEVEL_4:
		reg = JPEG_Q_TBL_Y_0 | JPEG_Q_TBL_Cb_1 |
		JPEG_Q_TBL_Cr_1 |
		JPEG_HUFF_TBL_Y_AC_0 | JPEG_HUFF_TBL_Y_DC_0 |
		JPEG_HUFF_TBL_Cb_AC_1 | JPEG_HUFF_TBL_Cb_DC_1 |
		JPEG_HUFF_TBL_Cr_AC_1 | JPEG_HUFF_TBL_Cr_DC_1;
		break;

	default:
		reg = JPEG_Q_TBL_Y_0 | JPEG_Q_TBL_Cb_1 |
		JPEG_Q_TBL_Cr_1 |
		JPEG_HUFF_TBL_Y_AC_0 | JPEG_HUFF_TBL_Y_DC_0 |
		JPEG_HUFF_TBL_Cb_AC_1 | JPEG_HUFF_TBL_Cb_DC_1 |
		JPEG_HUFF_TBL_Cr_AC_1 | JPEG_HUFF_TBL_Cr_DC_1;
		break;
	}
	writel(reg, base + JPEG_QHTBL);
}

void jpeg_hx_set_stream_size(void __iomem *base,
		unsigned int x_value, unsigned int y_value)
{
	writel(0x0, base + JPEG_X_SIZE_REG); /* clear */
	writel(x_value, base + JPEG_X_SIZE_REG);
	writel(0x0, base + JPEG_Y_SIZE_REG); /* clear */
	writel(y_value, base + JPEG_Y_SIZE_REG);
}

void jpeg_hx_set_enc_out_fmt(void __iomem *base,
					enum jpeg_stream_format out_fmt)
{
	unsigned int reg;

	reg = readl(base + JPEG_MOD_REG) &
			~JPEG_SUBSAMPLE_MODE_MASK; /* clear except enc format */

	switch (out_fmt) {
	case JPEG_GRAY:
		reg = reg | JPEG_SUBSAMPLE_GRAY;
		break;

	case JPEG_444:
		reg = reg | JPEG_SUBSAMPLE_444;
		break;

	case JPEG_422:
		reg = reg | JPEG_SUBSAMPLE_422;
		break;

	case JPEG_420:
		reg = reg | JPEG_SUBSAMPLE_420;
		break;

	default:
		break;
	}

	writel(reg, base + JPEG_MOD_REG);

}

void jpeg_hx_set_enc_in_fmt(void __iomem *base,
					enum jpeg_frame_format in_fmt)
{
	unsigned int reg;

	reg = readl(base + JPEG_CMOD_REG) &
			~(JPEG_SRC_NV_MASK | JPEG_SRC_MOD_SEL_MASK); /* clear enc input format */

	switch (in_fmt) {
	case RGB_565:
		reg = reg | JPEG_SRC_RGB565;
		break;

	case YCBYCR_422_1P:
		reg = reg | JPEG_SRC_YUYV;
		break;

	case CBYCRY_422_1P:
		reg = reg | JPEG_SRC_UYUV;
		break;

	case YCRCB_420_2P:
		reg = reg | JPEG_SRC_NV_21 | JPEG_SRC_YUV_420_2P;
		break;

	case YCBCR_420_2P:
		reg = reg | JPEG_SRC_NV_12 | JPEG_SRC_YUV_420_2P;
		break;

	default:
		break;
	}

	writel(reg, base + JPEG_CMOD_REG);
}

void jpeg_hx_set_y16(void __iomem *base)
{
	unsigned int reg;

	reg = readl(base + JPEG_CMOD_REG) &
			~(JPEG_SRC_MODE_Y16_MASK); /* clear */

	writel(reg | JPEG_SRC_C1_16, base + JPEG_CMOD_REG);
}
