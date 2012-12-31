/* linux/drivers/media/video/samsung/jpeg/jpeg_regs.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Register interface file for jpeg driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/io.h>
#include <plat/regs_jpeg.h>

#include "jpeg_regs.h"
#include "jpeg_conf.h"

void jpeg_sw_reset(void __iomem *base)
{
	writel(S5P_JPEG_SW_RESET_REG_ENABLE,
			base + S5P_JPEG_SW_RESET_REG);

	do {
		writel(S5P_JPEG_SW_RESET_REG_ENABLE,
				base + S5P_JPEG_SW_RESET_REG);
	} while (((readl(base + S5P_JPEG_SW_RESET_REG))
		& S5P_JPEG_SW_RESET_REG_ENABLE)
		== S5P_JPEG_SW_RESET_REG_ENABLE);

}

void jpeg_set_clk_power_on(void __iomem *base)
{
	/* set jpeg clock register : power on */
	writel(readl(base + S5P_JPEG_CLKCON_REG) |
			(S5P_JPEG_CLKCON_REG_POWER_ON_ACTIVATE),
			base + S5P_JPEG_CLKCON_REG);
}

void jpeg_set_mode(void __iomem *base, int mode)
{
	/* set jpeg mod register */
	if (mode) {/* decode */
		writel(readl(base + S5P_JPEG_MOD_REG) |
			(S5P_JPEG_MOD_REG_PROC_DEC),
			base + S5P_JPEG_MOD_REG);
	} else {/* encode */
		writel(readl(base + S5P_JPEG_MOD_REG) |
			(S5P_JPEG_MOD_REG_PROC_ENC),
			base + S5P_JPEG_MOD_REG);
	}

}

void jpeg_set_dec_out_fmt(void __iomem *base,
					enum jpeg_frame_format out_fmt)
{
	/* set jpeg deocde ouput format register */
	writel(readl(base + S5P_JPEG_OUTFORM_REG) &
			~(S5P_JPEG_OUTFORM_REG_YCBCY420),
			base + S5P_JPEG_OUTFORM_REG);
	if (out_fmt == YUV_422) {
		writel(readl(base + S5P_JPEG_OUTFORM_REG) |
				(S5P_JPEG_OUTFORM_REG_YCBCY422),
				base + S5P_JPEG_OUTFORM_REG);
	} else { /* default YUV420 */
		writel(readl(base + S5P_JPEG_OUTFORM_REG) |
				(S5P_JPEG_OUTFORM_REG_YCBCY420),
				base + S5P_JPEG_OUTFORM_REG);
	}

}

void jpeg_set_enc_in_fmt(void __iomem *base,
					enum jpeg_frame_format in_fmt)
{
	if (in_fmt == YUV_422) {
		writel(readl(base + S5P_JPEG_CMOD_REG) |
			(S5P_JPEG_CMOD_REG_MOD_SEL_YCBCR422),
			base + S5P_JPEG_CMOD_REG);
	} else {
		writel(readl(base + S5P_JPEG_CMOD_REG) |
			(S5P_JPEG_CMOD_REG_MOD_SEL_RGB),
			base + S5P_JPEG_CMOD_REG);
	}

}

void jpeg_set_enc_out_fmt(void __iomem *base,
					enum jpeg_stream_format out_fmt)
{
	if (out_fmt == JPEG_422) {
		writel(readl(base + S5P_JPEG_MOD_REG) |
			(S5P_JPEG_MOD_REG_SUBSAMPLE_422),
			base + S5P_JPEG_MOD_REG);
	} else {
		writel(readl(base + S5P_JPEG_MOD_REG) |
			(S5P_JPEG_MOD_REG_SUBSAMPLE_420),
			base + S5P_JPEG_MOD_REG);
	}
}

void jpeg_set_enc_dri(void __iomem *base, unsigned int value)
{
	/* set JPEG Restart Interval */
	writel(value, base + S5P_JPEG_DRI_L_REG);
	writel((value >> 8), base + S5P_JPEG_DRI_U_REG);
}

void jpeg_set_enc_qtbl(void __iomem *base,
				enum  jpeg_img_quality_level level)
{
	/* set quantization table index for jpeg encode */
	unsigned int val;
	int i;

	switch (level) {
	case QUALITY_LEVEL_1:
		val = S5P_JPEG_QHTBL_REG_QT_NUM1;
		break;
	case QUALITY_LEVEL_2:
		val = S5P_JPEG_QHTBL_REG_QT_NUM2;
		break;
	case QUALITY_LEVEL_3:
		val = S5P_JPEG_QHTBL_REG_QT_NUM3;
		break;
	case QUALITY_LEVEL_4:
		val = S5P_JPEG_QHTBL_REG_QT_NUM4;
		break;
	default:
		val = S5P_JPEG_QHTBL_REG_QT_NUM1;
		break;
	}
	writel(val, base + S5P_JPEG_QTBL_REG);

	for (i = 0; i < 64; i++) {
		writel((unsigned int)qtbl_luminance[level][i],
			base + S5P_JPEG_QTBL0_REG + (i*0x04));
	}
	for (i = 0; i < 64; i++) {
		writel((unsigned int)qtbl_chrominance[level][i],
			base + S5P_JPEG_QTBL1_REG + (i*0x04));
	}

}

void jpeg_set_enc_htbl(void __iomem *base)
{
	int i;

	/* set huffman table index for jpeg encode */
	writel(0x00, base + S5P_JPEG_HTBL_REG);

	for (i = 0; i < 16; i++) {
		writel((unsigned int)hdctbl0[i],
			base + S5P_JPEG_HDCTBL0_REG + (i*0x04));
	}
	for (i = 0; i < 12; i++) {
		writel((unsigned int)hdctblg0[i],
			base + S5P_JPEG_HDCTBLG0_REG + (i*0x04));
	}
	for (i = 0; i < 16; i++) {
		writel((unsigned int)hactbl0[i],
			base + S5P_JPEG_HACTBL0_REG + (i*0x04));
	}
	for (i = 0; i < 162; i++) {
		writel((unsigned int)hactblg0[i],
			base + S5P_JPEG_HACTBLG0_REG + (i*0x04));
	}
}

void jpeg_set_enc_coef(void __iomem *base)
{
	/* set coefficient value for RGB-to-YCbCr */
	writel(COEF1_RGB_2_YUV, base + S5P_JPEG_COEF1_REG);
	writel(COEF2_RGB_2_YUV, base + S5P_JPEG_COEF2_REG);
	writel(COEF3_RGB_2_YUV, base + S5P_JPEG_COEF3_REG);
}

void jpeg_set_frame_addr(void __iomem *base, unsigned int fra_addr)
{
	/* set the address of compressed input data */
	writel(fra_addr, base + S5P_JPEG_IMGADR_REG);
}

void jpeg_set_stream_addr(void __iomem *base, unsigned int str_addr)
{
	/* set the address of compressed input data */
	writel(str_addr, base + S5P_JPEG_JPGADR_REG);
}

void jpeg_get_frame_size(void __iomem *base,
			unsigned int *width, unsigned int *height)
{
	*width = (readl(base + S5P_JPEG_X_U_REG)<<8)|
		readl(base + S5P_JPEG_X_L_REG);
	*height = (readl(base + S5P_JPEG_Y_U_REG)<<8)|
		readl(base + S5P_JPEG_Y_L_REG);
}

void jpeg_set_frame_size(void __iomem *base,
			unsigned int width, unsigned int height)
{
	/* Horizontal resolution */
	writel((width >> 8), base + S5P_JPEG_X_U_REG);
	writel(width, base + S5P_JPEG_X_L_REG);

	/* Vertical resolution */
	writel((height >> 8), base + S5P_JPEG_Y_U_REG);
	writel(height, base + S5P_JPEG_Y_L_REG);
}

enum jpeg_stream_format jpeg_get_stream_fmt(void __iomem *base)
{
	enum jpeg_stream_format		out_fmt;
	unsigned long			jpeg_mode;

	jpeg_mode = readl(base + S5P_JPEG_MOD_REG);

	out_fmt =
		((jpeg_mode & 0x07) == 0x00) ? JPEG_444 :
		((jpeg_mode & 0x07) == 0x01) ? JPEG_422 :
		((jpeg_mode & 0x07) == 0x02) ? JPEG_420 :
		((jpeg_mode & 0x07) == 0x03) ? JPEG_GRAY : JPEG_RESERVED;

	return out_fmt;

}

unsigned int jpeg_get_stream_size(void __iomem *base)
{
	unsigned int size;

	size = readl(base + S5P_JPEG_CNT_U_REG) << 16;
	size |= readl(base + S5P_JPEG_CNT_M_REG) << 8;
	size |= readl(base + S5P_JPEG_CNT_L_REG);

	return size;
}

void jpeg_start_decode(void __iomem *base)
{
	/* set jpeg interrupt */
	writel(readl(base  + S5P_JPEG_INTSE_REG) |
			(S5P_JPEG_INTSE_REG_RSTM_INT_EN	|
			S5P_JPEG_INTSE_REG_DATA_NUM_INT_EN |
			S5P_JPEG_INTSE_REG_FINAL_MCU_NUM_INT_EN),
			base  + S5P_JPEG_INTSE_REG);

	/* start decoding */
	writel(readl(base + S5P_JPEG_JRSTART_REG) |
			S5P_JPEG_JRSTART_REG_ENABLE,
			base + S5P_JPEG_JSTART_REG);
}

void jpeg_start_encode(void __iomem *base)
{
	/* set jpeg interrupt */
	writel(readl(base + S5P_JPEG_INTSE_REG) |
			(S5P_JPEG_INTSE_REG_RSTM_INT_EN	|
			S5P_JPEG_INTSE_REG_DATA_NUM_INT_EN |
			S5P_JPEG_INTSE_REG_FINAL_MCU_NUM_INT_EN),
			base + S5P_JPEG_INTSE_REG);

	/* start encoding */
	writel(readl(base + S5P_JPEG_JSTART_REG) |
			S5P_JPEG_JSTART_REG_ENABLE,
			base + S5P_JPEG_JSTART_REG);
}

unsigned int jpeg_get_int_status(void __iomem *base)
{
	unsigned int	int_status;
	unsigned int	status;

	int_status = readl(base + S5P_JPEG_INTST_REG);

	do {
		status = readl(base + S5P_JPEG_OPR_REG);
	} while (status);

	return int_status;
}

void jpeg_clear_int(void __iomem *base)
{
	writel(S5P_JPEG_COM_INT_RELEASE, base + S5P_JPEG_COM_REG);
}

