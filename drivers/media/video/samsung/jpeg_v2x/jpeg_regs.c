/* linux/drivers/media/video/samsung/jpeg_v2x/jpeg_regs.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Register interface file for jpeg v2.x driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/io.h>
#include <linux/delay.h>
#include <plat/regs_jpeg_v2_x.h>
#include <plat/cpu.h>

#include "jpeg_regs.h"
#include "jpeg_conf.h"
#include "jpeg_core.h"

void jpeg_sw_reset(void __iomem *base)
{
	unsigned int reg;

#ifdef CONFIG_JPEG_V2_2
	reg = readl(base + S5P_JPEG_CNTL_REG);
	writel((reg & S5P_JPEG_ENC_DEC_MODE_MASK),
			base + S5P_JPEG_CNTL_REG);
#endif
	reg = readl(base + S5P_JPEG_CNTL_REG);
	writel(reg & ~S5P_JPEG_SOFT_RESET_HI,
			base + S5P_JPEG_CNTL_REG);

	ndelay(100000);

	writel(reg | S5P_JPEG_SOFT_RESET_HI,
			base + S5P_JPEG_CNTL_REG);
}

void jpeg_set_enc_dec_mode(void __iomem *base, enum jpeg_mode mode)
{
	unsigned int reg;

	reg = readl(base + S5P_JPEG_CNTL_REG);
	/* set jpeg mod register */
	if (mode == DECODING) {
		writel((reg & S5P_JPEG_ENC_DEC_MODE_MASK) | S5P_JPEG_DEC_MODE,
			base + S5P_JPEG_CNTL_REG);
	} else {/* encode */
		writel((reg & S5P_JPEG_ENC_DEC_MODE_MASK) | S5P_JPEG_ENC_MODE,
			base + S5P_JPEG_CNTL_REG);
	}
}

void jpeg_set_dec_out_fmt(void __iomem *base,
					enum jpeg_frame_format out_fmt)
{
	unsigned int reg = 0;

	writel(0, base + S5P_JPEG_IMG_FMT_REG); /* clear */

	/* set jpeg deocde ouput format register */
	switch (out_fmt) {
	case GRAY:
		reg = S5P_JPEG_DEC_GRAY_IMG  |
				S5P_JPEG_GRAY_IMG_IP;
		break;

	case RGB_565:
		reg = S5P_JPEG_DEC_RGB_IMG |
				S5P_JPEG_RGB_IP_RGB_16BIT_IMG;
		break;

	case YCRCB_444_2P:
		reg = S5P_JPEG_DEC_YUV_444_IMG |
				S5P_JPEG_YUV_444_IP_YUV_444_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CrCb;
		break;

	case YCBCR_444_2P:
		reg = S5P_JPEG_DEC_YUV_444_IMG |
				S5P_JPEG_YUV_444_IP_YUV_444_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CbCr;
		break;

	case YCBCR_444_3P:
		reg = S5P_JPEG_DEC_YUV_444_IMG |
			S5P_JPEG_YUV_444_IP_YUV_444_3P_IMG;
		break;
#if defined (CONFIG_JPEG_V2_2)
	case RGB_888:
		reg = S5P_JPEG_DEC_RGB_IMG |
			S5P_JPEG_RGB_IP_RGB_32BIT_IMG
				|S5P_JPEG_ENC_FMT_RGB;
		break;
	case BGR_888:
		reg = S5P_JPEG_DEC_RGB_IMG |
			S5P_JPEG_RGB_IP_RGB_32BIT_IMG
				|S5P_JPEG_ENC_FMT_BGR;
			break;

	case CRYCBY_422_1P:
		reg = S5P_JPEG_DEC_YUV_422_IMG |
			S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
				S5P_JPEG_ENC_FMT_VYUY;
			break;

	case CBYCRY_422_1P:
		reg = S5P_JPEG_DEC_YUV_422_IMG |
			S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
				S5P_JPEG_ENC_FMT_UYVY;
			break;
	case YCRYCB_422_1P:
			reg = S5P_JPEG_DEC_YUV_422_IMG |
				S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
				S5P_JPEG_ENC_FMT_YVYU;
		break;
	case YCBYCR_422_1P:
		reg = S5P_JPEG_DEC_YUV_422_IMG |
			S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
			S5P_JPEG_ENC_FMT_YUYV;
		break;

#elif defined (CONFIG_JPEG_V2_1)
	case RGB_888:
		reg = S5P_JPEG_DEC_RGB_IMG |
			S5P_JPEG_RGB_IP_RGB_32BIT_IMG;
		break;
	case YCRYCB_422_1P:
		reg = S5P_JPEG_DEC_YUV_422_IMG |
			S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
			S5P_JPEG_SWAP_CHROMA_CrCb;
		break;
	case YCBYCR_422_1P:
		reg = S5P_JPEG_DEC_YUV_422_IMG |
			S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
			S5P_JPEG_SWAP_CHROMA_CbCr;
		break;
#endif

	case YCRCB_422_2P:
		reg = S5P_JPEG_DEC_YUV_422_IMG |
				S5P_JPEG_YUV_422_IP_YUV_422_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CrCb;
		break;

	case YCBCR_422_2P:
		reg = S5P_JPEG_DEC_YUV_422_IMG |
				S5P_JPEG_YUV_422_IP_YUV_422_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CbCr;
		break;

	case YCBYCR_422_3P:
		reg = S5P_JPEG_DEC_YUV_422_IMG |
				S5P_JPEG_YUV_422_IP_YUV_422_3P_IMG;
		break;

	case YCRCB_420_2P:
		reg = S5P_JPEG_DEC_YUV_420_IMG |
				S5P_JPEG_YUV_420_IP_YUV_420_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CrCb;
		break;

	case YCBCR_420_2P:
		reg = S5P_JPEG_DEC_YUV_420_IMG |
				S5P_JPEG_YUV_420_IP_YUV_420_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CbCr;
		break;

	case YCBCR_420_3P:
	case YCRCB_420_3P:
		reg = S5P_JPEG_DEC_YUV_420_IMG |
				S5P_JPEG_YUV_420_IP_YUV_420_3P_IMG;
		break;

	default:
		break;
	}

	writel(reg, base + S5P_JPEG_IMG_FMT_REG);
}

void jpeg_set_enc_in_fmt(void __iomem *base,
					enum jpeg_frame_format in_fmt)
{
	unsigned int reg;

	reg = readl(base + S5P_JPEG_IMG_FMT_REG) &
			S5P_JPEG_ENC_IN_FMT_MASK; /* clear except enc format */

	switch (in_fmt) {
	case GRAY:
		reg = reg | S5P_JPEG_ENC_GRAY_IMG | S5P_JPEG_GRAY_IMG_IP;
		break;

	case RGB_565:
		reg = reg | S5P_JPEG_ENC_RGB_IMG |
				S5P_JPEG_RGB_IP_RGB_16BIT_IMG;
		break;

	case YCRCB_444_2P:
		reg = reg | S5P_JPEG_ENC_YUV_444_IMG |
				S5P_JPEG_YUV_444_IP_YUV_444_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CrCb;
		break;

	case YCBCR_444_2P:
		reg = reg | S5P_JPEG_ENC_YUV_444_IMG |
				S5P_JPEG_YUV_444_IP_YUV_444_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CbCr;
		break;

	case YCBCR_444_3P:
		reg = reg | S5P_JPEG_ENC_YUV_444_IMG |
				S5P_JPEG_YUV_444_IP_YUV_444_3P_IMG;
		break;

#if defined (CONFIG_JPEG_V2_2)
	case RGB_888:
		reg = reg | S5P_JPEG_DEC_RGB_IMG |
				S5P_JPEG_RGB_IP_RGB_32BIT_IMG
				|S5P_JPEG_ENC_FMT_RGB;
		break;
	case BGR_888:
		reg = reg | S5P_JPEG_DEC_RGB_IMG |
				S5P_JPEG_RGB_IP_RGB_32BIT_IMG
				|S5P_JPEG_ENC_FMT_BGR;
		break;
	case CRYCBY_422_1P:
		reg = reg | S5P_JPEG_DEC_YUV_422_IMG |
				S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
				S5P_JPEG_ENC_FMT_VYUY;
		break;
	case CBYCRY_422_1P:
		reg = reg | S5P_JPEG_DEC_YUV_422_IMG |
				S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
				S5P_JPEG_ENC_FMT_UYVY;
		break;

	case YCRYCB_422_1P:
		reg = reg | S5P_JPEG_DEC_YUV_422_IMG |
			S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
			S5P_JPEG_ENC_FMT_YVYU;
		break;
	case YCBYCR_422_1P:
		reg = reg | S5P_JPEG_DEC_YUV_422_IMG |
			S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
			S5P_JPEG_ENC_FMT_YUYV;
		break;

#elif defined (CONFIG_JPEG_V2_1)
	case RGB_888:
		reg = reg | S5P_JPEG_ENC_RGB_IMG |
				S5P_JPEG_RGB_IP_RGB_32BIT_IMG;
		break;
	case YCRYCB_422_1P:
		reg = reg | S5P_JPEG_DEC_YUV_422_IMG |
			S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
			S5P_JPEG_SWAP_CHROMA_CrCb;
		break;
	case YCBYCR_422_1P:
		reg = reg | S5P_JPEG_DEC_YUV_422_IMG |
			S5P_JPEG_YUV_422_IP_YUV_422_1P_IMG |
			S5P_JPEG_SWAP_CHROMA_CbCr;
		break;
#endif

	case YCRCB_422_2P:
		reg = reg | S5P_JPEG_DEC_YUV_422_IMG |
				S5P_JPEG_YUV_422_IP_YUV_422_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CrCb;
		break;

	case YCBCR_422_2P:
		reg = reg | S5P_JPEG_DEC_YUV_422_IMG |
				S5P_JPEG_YUV_422_IP_YUV_422_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CbCr;
		break;

	case YCBYCR_422_3P:
		reg = reg | S5P_JPEG_DEC_YUV_422_IMG |
				S5P_JPEG_YUV_422_IP_YUV_422_3P_IMG;
		break;

	case YCRCB_420_2P:
		reg = reg | S5P_JPEG_DEC_YUV_420_IMG |
				S5P_JPEG_YUV_420_IP_YUV_420_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CrCb;
		break;

	case YCBCR_420_2P:
		reg = reg | S5P_JPEG_DEC_YUV_420_IMG |
				S5P_JPEG_YUV_420_IP_YUV_420_2P_IMG |
				S5P_JPEG_SWAP_CHROMA_CbCr;
		break;

	case YCBCR_420_3P:
	case YCRCB_420_3P:
		reg = reg | S5P_JPEG_DEC_YUV_420_IMG |
				S5P_JPEG_YUV_420_IP_YUV_420_3P_IMG;
		break;

	default:
		break;

	}

	writel(reg, base + S5P_JPEG_IMG_FMT_REG);

}

void jpeg_set_enc_out_fmt(void __iomem *base,
					enum jpeg_stream_format out_fmt)
{
	unsigned int reg;

	reg = readl(base + S5P_JPEG_IMG_FMT_REG) &
			~S5P_JPEG_ENC_FMT_MASK; /* clear enc format */

	switch (out_fmt) {
	case JPEG_GRAY:
		reg = reg | S5P_JPEG_ENC_FMT_GRAY;
		break;

	case JPEG_444:
		reg = reg | S5P_JPEG_ENC_FMT_YUV_444;
		break;

	case JPEG_422:
		reg = reg | S5P_JPEG_ENC_FMT_YUV_422;
		break;

	case JPEG_420:
		reg = reg | S5P_JPEG_ENC_FMT_YUV_420;
		break;

	default:
		break;
	}

	writel(reg, base + S5P_JPEG_IMG_FMT_REG);
}

void jpeg_set_enc_tbl(void __iomem *base,
		enum jpeg_img_quality_level level)
{
	int i;

	switch (level) {
	case QUALITY_LEVEL_1:
		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[0][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[1][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x40 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[0][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x80 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[1][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0xc0 + (i*0x04));
		}
		break;

	case QUALITY_LEVEL_2:
		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[2][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[3][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x40 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[2][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x80 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[3][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0xc0 + (i*0x04));
		}
		break;

	case QUALITY_LEVEL_3:
		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[4][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[5][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x40 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[4][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x80 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[5][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0xc0 + (i*0x04));
		}
		break;

	case QUALITY_LEVEL_FRONT_1:
		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[6][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[7][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x40 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[6][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x80 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[7][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0xc0 + (i*0x04));
		}
		break;

	case QUALITY_LEVEL_FRONT_2:
		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[8][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[9][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x40 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[8][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x80 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[9][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0xc0 + (i*0x04));
		}
		break;

	case QUALITY_LEVEL_FRONT_3:
		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[10][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[11][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x40 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[10][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x80 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[11][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0xc0 + (i*0x04));
		}
		break;

	default:
		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[0][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[1][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x40 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[0][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0x80 + (i*0x04));
		}

		for (i = 0; i < 16; i++) {
			writel((unsigned int)ITU_Q_tbl[1][i],
				base + S5P_JPEG_QUAN_TBL_ENTRY_REG + 0xc0 + (i*0x04));
		}
		break;
	}

	for (i = 0; i < 4; i++) {
		writel((unsigned int)ITU_H_tbl_len_DC_luminance[i],
			base + S5P_JPEG_HUFF_TBL_ENTRY_REG + (i*0x04));
	}

	for (i = 0; i < 3; i++) {
		writel((unsigned int)ITU_H_tbl_val_DC_luminance[i],
			base + S5P_JPEG_HUFF_TBL_ENTRY_REG + 0x10 + (i*0x04));
	}

	for (i = 0; i < 4; i++) {
		writel((unsigned int)ITU_H_tbl_len_DC_chrominance[i],
			base + S5P_JPEG_HUFF_TBL_ENTRY_REG + 0x20 + (i*0x04));
	}

	for (i = 0; i < 3; i++) {
		writel((unsigned int)ITU_H_tbl_val_DC_chrominance[i],
			base + S5P_JPEG_HUFF_TBL_ENTRY_REG + 0x30 + (i*0x04));
	}

	for (i = 0; i < 4; i++) {
		writel((unsigned int)ITU_H_tbl_len_AC_luminance[i],
			base + S5P_JPEG_HUFF_TBL_ENTRY_REG + 0x40 + (i*0x04));
	}

	for (i = 0; i < 41; i++) {
		writel((unsigned int)ITU_H_tbl_val_AC_luminance[i],
			base + S5P_JPEG_HUFF_TBL_ENTRY_REG + 0x50 + (i*0x04));
	}

	for (i = 0; i < 4; i++) {
		writel((unsigned int)ITU_H_tbl_len_AC_chrominance[i],
			base + S5P_JPEG_HUFF_TBL_ENTRY_REG + 0x100 + (i*0x04));
	}

	for (i = 0; i < 41; i++) {
		writel((unsigned int)ITU_H_tbl_val_AC_chrominance[i],
			base + S5P_JPEG_HUFF_TBL_ENTRY_REG + 0x110 + (i*0x04));
	}

}

void jpeg_set_interrupt(void __iomem *base)
{
	unsigned int reg;
	reg = readl(base + S5P_JPEG_INT_EN_REG) & ~S5P_JPEG_INT_EN_MASK;
	writel(S5P_JPEG_INT_EN_ALL, base + S5P_JPEG_INT_EN_REG);
}

void jpeg_clean_interrupt(void __iomem *base)
{
	writel(0, base + S5P_JPEG_INT_EN_REG);
}

unsigned int jpeg_get_int_status(void __iomem *base)
{
	unsigned int	int_status;

	int_status = readl(base + S5P_JPEG_INT_STATUS_REG);

	return int_status;
}

void jpeg_set_huf_table_enable(void __iomem *base, int value)
{
	unsigned int	reg;

	reg = readl(base + S5P_JPEG_CNTL_REG) & ~S5P_JPEG_HUF_TBL_EN;

	if (value == 1)
		writel(reg | S5P_JPEG_HUF_TBL_EN, base + S5P_JPEG_CNTL_REG);
	else
		writel(reg | ~S5P_JPEG_HUF_TBL_EN, base + S5P_JPEG_CNTL_REG);
}

void jpeg_set_dec_scaling(void __iomem *base,
		enum jpeg_scale_value x_value, enum jpeg_scale_value y_value)
{
	unsigned int	reg;

	reg = readl(base + S5P_JPEG_CNTL_REG) &
			~(S5P_JPEG_HOR_SCALING_MASK |
				S5P_JPEG_VER_SCALING_MASK);

	writel(reg | S5P_JPEG_HOR_SCALING(x_value) |
			S5P_JPEG_VER_SCALING(y_value),
				base + S5P_JPEG_CNTL_REG);
}

void jpeg_set_sys_int_enable(void __iomem *base, int value)
{
	unsigned int	reg;

	reg = readl(base + S5P_JPEG_CNTL_REG) & ~(S5P_JPEG_SYS_INT_EN);

	if (value == 1)
		writel(S5P_JPEG_SYS_INT_EN, base + S5P_JPEG_CNTL_REG);
	else
		writel(~S5P_JPEG_SYS_INT_EN, base + S5P_JPEG_CNTL_REG);
}

void jpeg_set_stream_buf_address(void __iomem *base, unsigned int address)
{
	writel(address, base + S5P_JPEG_OUT_MEM_BASE_REG);
}

void jpeg_set_stream_size(void __iomem *base,
		unsigned int x_value, unsigned int y_value)
{
	writel(0x0, base + S5P_JPEG_IMG_SIZE_REG); /* clear */
	writel(S5P_JPEG_X_SIZE(x_value) | S5P_JPEG_Y_SIZE(y_value),
			base + S5P_JPEG_IMG_SIZE_REG);
}

void jpeg_set_frame_buf_address(void __iomem *base,
		enum jpeg_frame_format fmt, unsigned int address_1p,
		unsigned int address_2p, unsigned int address_3p)
{
	switch (fmt) {
	case GRAY:
	case RGB_565:
	case RGB_888:
	case YCRYCB_422_1P:
	case YCBYCR_422_1P:
#if defined (CONFIG_JPEG_V2_2)
	case BGR_888:
	case CBYCRY_422_1P:
	case CRYCBY_422_1P:
#endif
		writel(address_1p, base + S5P_JPEG_IMG_BA_PLANE_1_REG);
		writel(0, base + S5P_JPEG_IMG_BA_PLANE_2_REG);
		writel(0, base + S5P_JPEG_IMG_BA_PLANE_3_REG);
		break;
	case YCBCR_444_2P:
	case YCRCB_444_2P:
	case YCRCB_422_2P:
	case YCBCR_422_2P:
	case YCBCR_420_2P:
	case YCRCB_420_2P:
		writel(address_1p, base + S5P_JPEG_IMG_BA_PLANE_1_REG);
		writel(address_2p, base + S5P_JPEG_IMG_BA_PLANE_2_REG);
		writel(0, base + S5P_JPEG_IMG_BA_PLANE_3_REG);
		break;
	case YCBCR_444_3P:
		writel(address_1p, base + S5P_JPEG_IMG_BA_PLANE_1_REG);
		writel(address_2p, base + S5P_JPEG_IMG_BA_PLANE_2_REG);
		writel(address_3p, base + S5P_JPEG_IMG_BA_PLANE_3_REG);
		break;
	case YCBYCR_422_3P:
		writel(address_1p, base + S5P_JPEG_IMG_BA_PLANE_1_REG);
		writel(address_2p, base + S5P_JPEG_IMG_BA_PLANE_2_REG);
		writel(address_3p, base + S5P_JPEG_IMG_BA_PLANE_3_REG);
		break;
	case YCBCR_420_3P:
		writel(address_1p, base + S5P_JPEG_IMG_BA_PLANE_1_REG);
		writel(address_2p, base + S5P_JPEG_IMG_BA_PLANE_2_REG);
		writel(address_3p, base + S5P_JPEG_IMG_BA_PLANE_3_REG);
		break;
	case YCRCB_420_3P:
		writel(address_1p, base + S5P_JPEG_IMG_BA_PLANE_1_REG);
		writel(address_3p, base + S5P_JPEG_IMG_BA_PLANE_2_REG);
		writel(address_2p, base + S5P_JPEG_IMG_BA_PLANE_3_REG);
		break;
	default:
		break;
	}
}
void jpeg_set_encode_tbl_select(void __iomem *base,
		enum jpeg_img_quality_level level)
{
	unsigned int	reg;

	switch (level) {
	case QUALITY_LEVEL_1:
	case QUALITY_LEVEL_FRONT_1:
		reg = S5P_JPEG_Q_TBL_COMP1_0 | S5P_JPEG_Q_TBL_COMP2_1 |
			S5P_JPEG_Q_TBL_COMP3_1 |
			S5P_JPEG_HUFF_TBL_COMP1_AC_0_DC_0 |
			S5P_JPEG_HUFF_TBL_COMP2_AC_1_DC_1 |
			S5P_JPEG_HUFF_TBL_COMP3_AC_1_DC_1;
		break;
	case QUALITY_LEVEL_2:
	case QUALITY_LEVEL_FRONT_2:
		reg = S5P_JPEG_Q_TBL_COMP1_0 | S5P_JPEG_Q_TBL_COMP2_3 |
			S5P_JPEG_Q_TBL_COMP3_3 |
			S5P_JPEG_HUFF_TBL_COMP1_AC_0_DC_0 |
			S5P_JPEG_HUFF_TBL_COMP2_AC_1_DC_1 |
			S5P_JPEG_HUFF_TBL_COMP3_AC_1_DC_1;
		break;
	case QUALITY_LEVEL_3:
	case QUALITY_LEVEL_FRONT_3:
		reg = S5P_JPEG_Q_TBL_COMP1_2 | S5P_JPEG_Q_TBL_COMP2_1 |
			S5P_JPEG_Q_TBL_COMP3_1 |
			S5P_JPEG_HUFF_TBL_COMP1_AC_0_DC_0 |
			S5P_JPEG_HUFF_TBL_COMP2_AC_1_DC_1 |
			S5P_JPEG_HUFF_TBL_COMP3_AC_1_DC_1;
		break;
	case QUALITY_LEVEL_4:
		reg = S5P_JPEG_Q_TBL_COMP1_2 | S5P_JPEG_Q_TBL_COMP2_3 |
			S5P_JPEG_Q_TBL_COMP3_3 |
			S5P_JPEG_HUFF_TBL_COMP1_AC_0_DC_0 |
			S5P_JPEG_HUFF_TBL_COMP2_AC_1_DC_1 |
			S5P_JPEG_HUFF_TBL_COMP3_AC_1_DC_1;
		break;
	default:
		reg = S5P_JPEG_Q_TBL_COMP1_0 | S5P_JPEG_Q_TBL_COMP2_1 |
			S5P_JPEG_Q_TBL_COMP3_1 |
			S5P_JPEG_HUFF_TBL_COMP1_AC_0_DC_0 |
			S5P_JPEG_HUFF_TBL_COMP2_AC_1_DC_1 |
			S5P_JPEG_HUFF_TBL_COMP3_AC_1_DC_1;
		break;
	}
	writel(reg, base + S5P_JPEG_TBL_SEL_REG);
}

void jpeg_set_encode_hoff_cnt(void __iomem *base, enum jpeg_stream_format fmt)
{
	if (fmt == JPEG_GRAY)
		writel(0xd2, base + S5P_JPEG_HUFF_CNT_REG);
	else
		writel(0x1a2, base + S5P_JPEG_HUFF_CNT_REG);
}

unsigned int jpeg_get_stream_size(void __iomem *base)
{
	unsigned int size;

	size = readl(base + S5P_JPEG_BITSTREAM_SIZE_REG);
	return size;
}

void jpeg_set_dec_bitstream_size(void __iomem *base, unsigned int size)
{
	writel(size, base + S5P_JPEG_BITSTREAM_SIZE_REG);
}

void jpeg_set_timer_count(void __iomem *base, unsigned int size)
{
	writel(size, base + S5P_JPEG_INT_TIMER_COUNT_REG);
}

void jpeg_get_frame_size(void __iomem *base,
			unsigned int *width, unsigned int *height)
{
	*width = (readl(base + S5P_JPEG_DECODE_XY_SIZE_REG) &
				S5P_JPEG_DECODED_SIZE_MASK);
	*height = (readl(base + S5P_JPEG_DECODE_XY_SIZE_REG) >> 16) &
				S5P_JPEG_DECODED_SIZE_MASK ;
}

enum jpeg_stream_format jpeg_get_frame_fmt(void __iomem *base)
{
	unsigned int	reg;
	enum jpeg_stream_format out_format;

	reg = readl(base + S5P_JPEG_DECODE_IMG_FMT_REG);

	out_format =
		((reg & 0x03) == 0x01) ? JPEG_444 :
		((reg & 0x03) == 0x02) ? JPEG_422 :
		((reg & 0x03) == 0x03) ? JPEG_420 :
		((reg & 0x03) == 0x00) ? JPEG_GRAY : JPEG_RESERVED;

	return out_format;
}
