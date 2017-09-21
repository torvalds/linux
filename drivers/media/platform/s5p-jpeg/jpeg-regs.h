/* linux/drivers/media/platform/s5p-jpeg/jpeg-regs.h
 *
 * Register definition file for Samsung JPEG codec driver
 *
 * Copyright (c) 2011-2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef JPEG_REGS_H_
#define JPEG_REGS_H_

/* Register and bit definitions for S5PC210 */

/* JPEG mode register */
#define S5P_JPGMOD			0x00
#define S5P_PROC_MODE_MASK		(0x1 << 3)
#define S5P_PROC_MODE_DECOMPR		(0x1 << 3)
#define S5P_PROC_MODE_COMPR		(0x0 << 3)
#define S5P_SUBSAMPLING_MODE_MASK	0x7
#define S5P_SUBSAMPLING_MODE_444	(0x0 << 0)
#define S5P_SUBSAMPLING_MODE_422	(0x1 << 0)
#define S5P_SUBSAMPLING_MODE_420	(0x2 << 0)
#define S5P_SUBSAMPLING_MODE_GRAY	(0x3 << 0)

/* JPEG operation status register */
#define S5P_JPGOPR			0x04

/* Quantization tables*/
#define S5P_JPG_QTBL			0x08
#define S5P_QT_NUMt_SHIFT(t)		(((t) - 1) << 1)
#define S5P_QT_NUMt_MASK(t)		(0x3 << S5P_QT_NUMt_SHIFT(t))

/* Huffman tables */
#define S5P_JPG_HTBL			0x0c
#define S5P_HT_NUMt_AC_SHIFT(t)		(((t) << 1) - 1)
#define S5P_HT_NUMt_AC_MASK(t)		(0x1 << S5P_HT_NUMt_AC_SHIFT(t))

#define S5P_HT_NUMt_DC_SHIFT(t)		(((t) - 1) << 1)
#define S5P_HT_NUMt_DC_MASK(t)		(0x1 << S5P_HT_NUMt_DC_SHIFT(t))

/* JPEG restart interval register upper byte */
#define S5P_JPGDRI_U			0x10

/* JPEG restart interval register lower byte */
#define S5P_JPGDRI_L			0x14

/* JPEG vertical resolution register upper byte */
#define S5P_JPGY_U			0x18

/* JPEG vertical resolution register lower byte */
#define S5P_JPGY_L			0x1c

/* JPEG horizontal resolution register upper byte */
#define S5P_JPGX_U			0x20

/* JPEG horizontal resolution register lower byte */
#define S5P_JPGX_L			0x24

/* JPEG byte count register upper byte */
#define S5P_JPGCNT_U			0x28

/* JPEG byte count register middle byte */
#define S5P_JPGCNT_M			0x2c

/* JPEG byte count register lower byte */
#define S5P_JPGCNT_L			0x30

/* JPEG interrupt setting register */
#define S5P_JPGINTSE			0x34
#define S5P_RSTm_INT_EN_MASK		(0x1 << 7)
#define S5P_RSTm_INT_EN			(0x1 << 7)
#define S5P_DATA_NUM_INT_EN_MASK	(0x1 << 6)
#define S5P_DATA_NUM_INT_EN		(0x1 << 6)
#define S5P_FINAL_MCU_NUM_INT_EN_MASK	(0x1 << 5)
#define S5P_FINAL_MCU_NUM_INT_EN	(0x1 << 5)

/* JPEG interrupt status register */
#define S5P_JPGINTST			0x38
#define S5P_RESULT_STAT_SHIFT		6
#define S5P_RESULT_STAT_MASK		(0x1 << S5P_RESULT_STAT_SHIFT)
#define S5P_STREAM_STAT_SHIFT		5
#define S5P_STREAM_STAT_MASK		(0x1 << S5P_STREAM_STAT_SHIFT)

/* JPEG command register */
#define S5P_JPGCOM			0x4c
#define S5P_INT_RELEASE			(0x1 << 2)

/* Raw image data r/w address register */
#define S5P_JPG_IMGADR			0x50

/* JPEG file r/w address register */
#define S5P_JPG_JPGADR			0x58

/* Coefficient for RGB-to-YCbCr converter register */
#define S5P_JPG_COEF(n)			(0x5c + (((n) - 1) << 2))
#define S5P_COEFn_SHIFT(j)		((3 - (j)) << 3)
#define S5P_COEFn_MASK(j)		(0xff << S5P_COEFn_SHIFT(j))

/* JPEG color mode register */
#define S5P_JPGCMOD			0x68
#define S5P_MOD_SEL_MASK		(0x7 << 5)
#define S5P_MOD_SEL_422			(0x1 << 5)
#define S5P_MOD_SEL_565			(0x2 << 5)
#define S5P_MODE_Y16_MASK		(0x1 << 1)
#define S5P_MODE_Y16			(0x1 << 1)

/* JPEG clock control register */
#define S5P_JPGCLKCON			0x6c
#define S5P_CLK_DOWN_READY		(0x1 << 1)
#define S5P_POWER_ON			(0x1 << 0)

/* JPEG start register */
#define S5P_JSTART			0x70

/* JPEG SW reset register */
#define S5P_JPG_SW_RESET		0x78

/* JPEG timer setting register */
#define S5P_JPG_TIMER_SE		0x7c
#define S5P_TIMER_INT_EN_MASK		(0x1 << 31)
#define S5P_TIMER_INT_EN		(0x1 << 31)
#define S5P_TIMER_INIT_MASK		0x7fffffff

/* JPEG timer status register */
#define S5P_JPG_TIMER_ST		0x80
#define S5P_TIMER_INT_STAT_SHIFT	31
#define S5P_TIMER_INT_STAT_MASK		(0x1 << S5P_TIMER_INT_STAT_SHIFT)
#define S5P_TIMER_CNT_SHIFT		0
#define S5P_TIMER_CNT_MASK		0x7fffffff

/* JPEG decompression output format register */
#define S5P_JPG_OUTFORM			0x88
#define S5P_DEC_OUT_FORMAT_MASK		(0x1 << 0)
#define S5P_DEC_OUT_FORMAT_422		(0x0 << 0)
#define S5P_DEC_OUT_FORMAT_420		(0x1 << 0)

/* JPEG version register */
#define S5P_JPG_VERSION			0x8c

/* JPEG compressed stream size interrupt setting register */
#define S5P_JPG_ENC_STREAM_INTSE	0x98
#define S5P_ENC_STREAM_INT_MASK		(0x1 << 24)
#define S5P_ENC_STREAM_INT_EN		(0x1 << 24)
#define S5P_ENC_STREAM_BOUND_MASK	0xffffff

/* JPEG compressed stream size interrupt status register */
#define S5P_JPG_ENC_STREAM_INTST	0x9c
#define S5P_ENC_STREAM_INT_STAT_MASK	0x1

/* JPEG quantizer table register */
#define S5P_JPG_QTBL_CONTENT(n)		(0x400 + (n) * 0x100)

/* JPEG DC Huffman table register */
#define S5P_JPG_HDCTBL(n)		(0x800 + (n) * 0x400)

/* JPEG DC Huffman table register */
#define S5P_JPG_HDCTBLG(n)		(0x840 + (n) * 0x400)

/* JPEG AC Huffman table register */
#define S5P_JPG_HACTBL(n)		(0x880 + (n) * 0x400)

/* JPEG AC Huffman table register */
#define S5P_JPG_HACTBLG(n)		(0x8c0 + (n) * 0x400)


/* Register and bit definitions for Exynos 4x12 */

/* JPEG Codec Control Registers */
#define EXYNOS4_JPEG_CNTL_REG		0x00
#define EXYNOS4_INT_EN_REG		0x04
#define EXYNOS4_INT_TIMER_COUNT_REG	0x08
#define EXYNOS4_INT_STATUS_REG		0x0c
#define EXYNOS4_OUT_MEM_BASE_REG		0x10
#define EXYNOS4_JPEG_IMG_SIZE_REG	0x14
#define EXYNOS4_IMG_BA_PLANE_1_REG	0x18
#define EXYNOS4_IMG_SO_PLANE_1_REG	0x1c
#define EXYNOS4_IMG_PO_PLANE_1_REG	0x20
#define EXYNOS4_IMG_BA_PLANE_2_REG	0x24
#define EXYNOS4_IMG_SO_PLANE_2_REG	0x28
#define EXYNOS4_IMG_PO_PLANE_2_REG	0x2c
#define EXYNOS4_IMG_BA_PLANE_3_REG	0x30
#define EXYNOS4_IMG_SO_PLANE_3_REG	0x34
#define EXYNOS4_IMG_PO_PLANE_3_REG	0x38

#define EXYNOS4_TBL_SEL_REG		0x3c

#define EXYNOS4_IMG_FMT_REG		0x40

#define EXYNOS4_BITSTREAM_SIZE_REG	0x44
#define EXYNOS4_PADDING_REG		0x48
#define EXYNOS4_HUFF_CNT_REG		0x4c
#define EXYNOS4_FIFO_STATUS_REG	0x50
#define EXYNOS4_DECODE_XY_SIZE_REG	0x54
#define EXYNOS4_DECODE_IMG_FMT_REG	0x58

#define EXYNOS4_QUAN_TBL_ENTRY_REG	0x100
#define EXYNOS4_HUFF_TBL_ENTRY_REG	0x200


/****************************************************************/
/* Bit definition part						*/
/****************************************************************/

/* JPEG CNTL Register bit */
#define EXYNOS4_ENC_DEC_MODE_MASK	(0xfffffffc << 0)
#define EXYNOS4_DEC_MODE		(1 << 0)
#define EXYNOS4_ENC_MODE		(1 << 1)
#define EXYNOS4_AUTO_RST_MARKER		(1 << 2)
#define EXYNOS4_RST_INTERVAL_SHIFT	3
#define EXYNOS4_RST_INTERVAL(x)		(((x) & 0xffff) \
						<< EXYNOS4_RST_INTERVAL_SHIFT)
#define EXYNOS4_HUF_TBL_EN		(1 << 19)
#define EXYNOS4_HOR_SCALING_SHIFT	20
#define EXYNOS4_HOR_SCALING_MASK	(3 << EXYNOS4_HOR_SCALING_SHIFT)
#define EXYNOS4_HOR_SCALING(x)		(((x) & 0x3) \
						<< EXYNOS4_HOR_SCALING_SHIFT)
#define EXYNOS4_VER_SCALING_SHIFT	22
#define EXYNOS4_VER_SCALING_MASK	(3 << EXYNOS4_VER_SCALING_SHIFT)
#define EXYNOS4_VER_SCALING(x)		(((x) & 0x3) \
						<< EXYNOS4_VER_SCALING_SHIFT)
#define EXYNOS4_PADDING			(1 << 27)
#define EXYNOS4_SYS_INT_EN		(1 << 28)
#define EXYNOS4_SOFT_RESET_HI		(1 << 29)

/* JPEG INT Register bit */
#define EXYNOS4_INT_EN_MASK		(0x1f << 0)
#define EXYNOS5433_INT_EN_MASK		(0x1ff << 0)
#define EXYNOS4_PROT_ERR_INT_EN		(1 << 0)
#define EXYNOS4_IMG_COMPLETION_INT_EN	(1 << 1)
#define EXYNOS4_DEC_INVALID_FORMAT_EN	(1 << 2)
#define EXYNOS4_MULTI_SCAN_ERROR_EN	(1 << 3)
#define EXYNOS4_FRAME_ERR_EN		(1 << 4)
#define EXYNOS4_INT_EN_ALL		(0x1f << 0)
#define EXYNOS5433_INT_EN_ALL		(0x1b6 << 0)

#define EXYNOS4_MOD_REG_PROC_ENC	(0 << 3)
#define EXYNOS4_MOD_REG_PROC_DEC	(1 << 3)

#define EXYNOS4_MOD_REG_SUBSAMPLE_444	(0 << 0)
#define EXYNOS4_MOD_REG_SUBSAMPLE_422	(1 << 0)
#define EXYNOS4_MOD_REG_SUBSAMPLE_420	(2 << 0)
#define EXYNOS4_MOD_REG_SUBSAMPLE_GRAY	(3 << 0)


/* JPEG IMAGE SIZE Register bit */
#define EXYNOS4_X_SIZE_SHIFT		0
#define EXYNOS4_X_SIZE_MASK		(0xffff << EXYNOS4_X_SIZE_SHIFT)
#define EXYNOS4_X_SIZE(x)		(((x) & 0xffff) << EXYNOS4_X_SIZE_SHIFT)
#define EXYNOS4_Y_SIZE_SHIFT		16
#define EXYNOS4_Y_SIZE_MASK		(0xffff << EXYNOS4_Y_SIZE_SHIFT)
#define EXYNOS4_Y_SIZE(x)		(((x) & 0xffff) << EXYNOS4_Y_SIZE_SHIFT)

/* JPEG IMAGE FORMAT Register bit */
#define EXYNOS4_ENC_IN_FMT_MASK		0xffff0000
#define EXYNOS4_ENC_GRAY_IMG		(0 << 0)
#define EXYNOS4_ENC_RGB_IMG		(1 << 0)
#define EXYNOS4_ENC_YUV_444_IMG		(2 << 0)
#define EXYNOS4_ENC_YUV_422_IMG		(3 << 0)
#define EXYNOS4_ENC_YUV_440_IMG		(4 << 0)

#define EXYNOS4_DEC_GRAY_IMG		(0 << 0)
#define EXYNOS4_DEC_RGB_IMG		(1 << 0)
#define EXYNOS4_DEC_YUV_444_IMG		(2 << 0)
#define EXYNOS4_DEC_YUV_422_IMG		(3 << 0)
#define EXYNOS4_DEC_YUV_420_IMG		(4 << 0)

#define EXYNOS4_GRAY_IMG_IP_SHIFT	3
#define EXYNOS4_GRAY_IMG_IP_MASK	(7 << EXYNOS4_GRAY_IMG_IP_SHIFT)
#define EXYNOS4_GRAY_IMG_IP		(4 << EXYNOS4_GRAY_IMG_IP_SHIFT)

#define EXYNOS4_RGB_IP_SHIFT		6
#define EXYNOS4_RGB_IP_MASK		(7 << EXYNOS4_RGB_IP_SHIFT)
#define EXYNOS4_RGB_IP_RGB_16BIT_IMG	(4 << EXYNOS4_RGB_IP_SHIFT)
#define EXYNOS4_RGB_IP_RGB_32BIT_IMG	(5 << EXYNOS4_RGB_IP_SHIFT)

#define EXYNOS4_YUV_444_IP_SHIFT		9
#define EXYNOS4_YUV_444_IP_MASK			(7 << EXYNOS4_YUV_444_IP_SHIFT)
#define EXYNOS4_YUV_444_IP_YUV_444_2P_IMG	(4 << EXYNOS4_YUV_444_IP_SHIFT)
#define EXYNOS4_YUV_444_IP_YUV_444_3P_IMG	(5 << EXYNOS4_YUV_444_IP_SHIFT)

#define EXYNOS4_YUV_422_IP_SHIFT		12
#define EXYNOS4_YUV_422_IP_MASK			(7 << EXYNOS4_YUV_422_IP_SHIFT)
#define EXYNOS4_YUV_422_IP_YUV_422_1P_IMG	(4 << EXYNOS4_YUV_422_IP_SHIFT)
#define EXYNOS4_YUV_422_IP_YUV_422_2P_IMG	(5 << EXYNOS4_YUV_422_IP_SHIFT)
#define EXYNOS4_YUV_422_IP_YUV_422_3P_IMG	(6 << EXYNOS4_YUV_422_IP_SHIFT)

#define EXYNOS4_YUV_420_IP_SHIFT		15
#define EXYNOS4_YUV_420_IP_MASK			(7 << EXYNOS4_YUV_420_IP_SHIFT)
#define EXYNOS4_YUV_420_IP_YUV_420_2P_IMG	(4 << EXYNOS4_YUV_420_IP_SHIFT)
#define EXYNOS4_YUV_420_IP_YUV_420_3P_IMG	(5 << EXYNOS4_YUV_420_IP_SHIFT)

#define EXYNOS4_ENC_FMT_SHIFT			24
#define EXYNOS4_ENC_FMT_MASK			(3 << EXYNOS4_ENC_FMT_SHIFT)
#define EXYNOS5433_ENC_FMT_MASK			(7 << EXYNOS4_ENC_FMT_SHIFT)

#define EXYNOS4_ENC_FMT_GRAY			(0 << EXYNOS4_ENC_FMT_SHIFT)
#define EXYNOS4_ENC_FMT_YUV_444			(1 << EXYNOS4_ENC_FMT_SHIFT)
#define EXYNOS4_ENC_FMT_YUV_422			(2 << EXYNOS4_ENC_FMT_SHIFT)
#define EXYNOS4_ENC_FMT_YUV_420			(3 << EXYNOS4_ENC_FMT_SHIFT)

#define EXYNOS4_JPEG_DECODED_IMG_FMT_MASK	0x03

#define EXYNOS4_SWAP_CHROMA_CRCB		(1 << 26)
#define EXYNOS4_SWAP_CHROMA_CBCR		(0 << 26)
#define EXYNOS5433_SWAP_CHROMA_CRCB		(1 << 27)
#define EXYNOS5433_SWAP_CHROMA_CBCR		(0 << 27)

/* JPEG HUFF count Register bit */
#define EXYNOS4_HUFF_COUNT_MASK			0xffff

/* JPEG Decoded_img_x_y_size Register bit */
#define EXYNOS4_DECODED_SIZE_MASK		0x0000ffff

/* JPEG Decoded image format Register bit */
#define EXYNOS4_DECODED_IMG_FMT_MASK		0x3

/* JPEG TBL SEL Register bit */
#define EXYNOS4_Q_TBL_COMP(c, n)	((n) << (((c) - 1) << 1))

#define EXYNOS4_Q_TBL_COMP1_0		EXYNOS4_Q_TBL_COMP(1, 0)
#define EXYNOS4_Q_TBL_COMP1_1		EXYNOS4_Q_TBL_COMP(1, 1)
#define EXYNOS4_Q_TBL_COMP1_2		EXYNOS4_Q_TBL_COMP(1, 2)
#define EXYNOS4_Q_TBL_COMP1_3		EXYNOS4_Q_TBL_COMP(1, 3)

#define EXYNOS4_Q_TBL_COMP2_0		EXYNOS4_Q_TBL_COMP(2, 0)
#define EXYNOS4_Q_TBL_COMP2_1		EXYNOS4_Q_TBL_COMP(2, 1)
#define EXYNOS4_Q_TBL_COMP2_2		EXYNOS4_Q_TBL_COMP(2, 2)
#define EXYNOS4_Q_TBL_COMP2_3		EXYNOS4_Q_TBL_COMP(2, 3)

#define EXYNOS4_Q_TBL_COMP3_0		EXYNOS4_Q_TBL_COMP(3, 0)
#define EXYNOS4_Q_TBL_COMP3_1		EXYNOS4_Q_TBL_COMP(3, 1)
#define EXYNOS4_Q_TBL_COMP3_2		EXYNOS4_Q_TBL_COMP(3, 2)
#define EXYNOS4_Q_TBL_COMP3_3		EXYNOS4_Q_TBL_COMP(3, 3)

#define EXYNOS4_HUFF_TBL_COMP(c, n)	((n) << ((((c) - 1) << 1) + 6))

#define EXYNOS4_HUFF_TBL_COMP1_AC_0_DC_0	\
	EXYNOS4_HUFF_TBL_COMP(1, 0)
#define EXYNOS4_HUFF_TBL_COMP1_AC_0_DC_1	\
	EXYNOS4_HUFF_TBL_COMP(1, 1)
#define EXYNOS4_HUFF_TBL_COMP1_AC_1_DC_0	\
	EXYNOS4_HUFF_TBL_COMP(1, 2)
#define EXYNOS4_HUFF_TBL_COMP1_AC_1_DC_1	\
	EXYNOS4_HUFF_TBL_COMP(1, 3)

#define EXYNOS4_HUFF_TBL_COMP2_AC_0_DC_0	\
	EXYNOS4_HUFF_TBL_COMP(2, 0)
#define EXYNOS4_HUFF_TBL_COMP2_AC_0_DC_1	\
	EXYNOS4_HUFF_TBL_COMP(2, 1)
#define EXYNOS4_HUFF_TBL_COMP2_AC_1_DC_0	\
	EXYNOS4_HUFF_TBL_COMP(2, 2)
#define EXYNOS4_HUFF_TBL_COMP2_AC_1_DC_1	\
	EXYNOS4_HUFF_TBL_COMP(2, 3)

#define EXYNOS4_HUFF_TBL_COMP3_AC_0_DC_0	\
	EXYNOS4_HUFF_TBL_COMP(3, 0)
#define EXYNOS4_HUFF_TBL_COMP3_AC_0_DC_1	\
	EXYNOS4_HUFF_TBL_COMP(3, 1)
#define EXYNOS4_HUFF_TBL_COMP3_AC_1_DC_0	\
	EXYNOS4_HUFF_TBL_COMP(3, 2)
#define EXYNOS4_HUFF_TBL_COMP3_AC_1_DC_1	\
	EXYNOS4_HUFF_TBL_COMP(3, 3)

#define EXYNOS4_NF_SHIFT			16
#define EXYNOS4_NF_MASK				0xff
#define EXYNOS4_NF(x)				\
	(((x) & EXYNOS4_NF_MASK) << EXYNOS4_NF_SHIFT)

/* JPEG quantizer table register */
#define EXYNOS4_QTBL_CONTENT(n)	(0x100 + (n) * 0x40)

/* JPEG DC luminance (code length) Huffman table register */
#define EXYNOS4_HUFF_TBL_HDCLL	0x200

/* JPEG DC luminance (values) Huffman table register */
#define EXYNOS4_HUFF_TBL_HDCLV	0x210

/* JPEG DC chrominance (code length) Huffman table register */
#define EXYNOS4_HUFF_TBL_HDCCL	0x220

/* JPEG DC chrominance (values) Huffman table register */
#define EXYNOS4_HUFF_TBL_HDCCV	0x230

/* JPEG AC luminance (code length) Huffman table register */
#define EXYNOS4_HUFF_TBL_HACLL	0x240

/* JPEG AC luminance (values) Huffman table register */
#define EXYNOS4_HUFF_TBL_HACLV	0x250

/* JPEG AC chrominance (code length) Huffman table register */
#define EXYNOS4_HUFF_TBL_HACCL	0x300

/* JPEG AC chrominance (values) Huffman table register */
#define EXYNOS4_HUFF_TBL_HACCV	0x310

/* Register and bit definitions for Exynos 3250 */

/* JPEG mode register */
#define EXYNOS3250_JPGMOD			0x00
#define EXYNOS3250_PROC_MODE_MASK		(0x1 << 3)
#define EXYNOS3250_PROC_MODE_DECOMPR		(0x1 << 3)
#define EXYNOS3250_PROC_MODE_COMPR		(0x0 << 3)
#define EXYNOS3250_SUBSAMPLING_MODE_MASK	(0x7 << 0)
#define EXYNOS3250_SUBSAMPLING_MODE_444		(0x0 << 0)
#define EXYNOS3250_SUBSAMPLING_MODE_422		(0x1 << 0)
#define EXYNOS3250_SUBSAMPLING_MODE_420		(0x2 << 0)
#define EXYNOS3250_SUBSAMPLING_MODE_411		(0x6 << 0)
#define EXYNOS3250_SUBSAMPLING_MODE_GRAY	(0x3 << 0)

/* JPEG operation status register */
#define EXYNOS3250_JPGOPR			0x04
#define EXYNOS3250_JPGOPR_MASK			0x01

/* Quantization and Huffman tables register */
#define EXYNOS3250_QHTBL			0x08
#define EXYNOS3250_QT_NUM_SHIFT(t)		((((t) - 1) << 1) + 8)
#define EXYNOS3250_QT_NUM_MASK(t)		(0x3 << EXYNOS3250_QT_NUM_SHIFT(t))

/* Huffman tables */
#define EXYNOS3250_HT_NUM_AC_SHIFT(t)		(((t) << 1) - 1)
#define EXYNOS3250_HT_NUM_AC_MASK(t)		(0x1 << EXYNOS3250_HT_NUM_AC_SHIFT(t))

#define EXYNOS3250_HT_NUM_DC_SHIFT(t)		(((t) - 1) << 1)
#define EXYNOS3250_HT_NUM_DC_MASK(t)		(0x1 << EXYNOS3250_HT_NUM_DC_SHIFT(t))

/* JPEG restart interval register */
#define EXYNOS3250_JPGDRI			0x0c
#define EXYNOS3250_JPGDRI_MASK			0xffff

/* JPEG vertical resolution register */
#define EXYNOS3250_JPGY				0x10
#define EXYNOS3250_JPGY_MASK			0xffff

/* JPEG horizontal resolution register */
#define EXYNOS3250_JPGX				0x14
#define EXYNOS3250_JPGX_MASK			0xffff

/* JPEG byte count register */
#define EXYNOS3250_JPGCNT			0x18
#define EXYNOS3250_JPGCNT_MASK			0xffffff

/* JPEG interrupt mask register */
#define EXYNOS3250_JPGINTSE			0x1c
#define EXYNOS3250_JPEG_DONE_EN			(1 << 11)
#define EXYNOS3250_WDMA_DONE_EN			(1 << 10)
#define EXYNOS3250_RDMA_DONE_EN			(1 << 9)
#define EXYNOS3250_ENC_STREAM_INT_EN		(1 << 8)
#define EXYNOS3250_CORE_DONE_EN			(1 << 5)
#define EXYNOS3250_ERR_INT_EN			(1 << 4)
#define EXYNOS3250_HEAD_INT_EN			(1 << 3)

/* JPEG interrupt status register */
#define EXYNOS3250_JPGINTST			0x20
#define EXYNOS3250_JPEG_DONE			(1 << 11)
#define EXYNOS3250_WDMA_DONE			(1 << 10)
#define EXYNOS3250_RDMA_DONE			(1 << 9)
#define EXYNOS3250_ENC_STREAM_STAT		(1 << 8)
#define EXYNOS3250_RESULT_STAT			(1 << 5)
#define EXYNOS3250_STREAM_STAT			(1 << 4)
#define EXYNOS3250_HEADER_STAT			(1 << 3)

/*
 * Base address of the luma component DMA buffer
 * of the raw input or output image.
 */
#define EXYNOS3250_LUMA_BASE			0x100
#define EXYNOS3250_SRC_TILE_EN_MASK		0x100

/* Stride of source or destination luma raw image buffer */
#define EXYNOS3250_LUMA_STRIDE			0x104

/* Horizontal/vertical offset of active region in luma raw image buffer */
#define EXYNOS3250_LUMA_XY_OFFSET		0x108
#define EXYNOS3250_LUMA_YY_OFFSET_SHIFT		18
#define EXYNOS3250_LUMA_YY_OFFSET_MASK		(0x1fff << EXYNOS3250_LUMA_YY_OFFSET_SHIFT)
#define EXYNOS3250_LUMA_YX_OFFSET_SHIFT		2
#define EXYNOS3250_LUMA_YX_OFFSET_MASK		(0x1fff << EXYNOS3250_LUMA_YX_OFFSET_SHIFT)

/*
 * Base address of the chroma(Cb) component DMA buffer
 * of the raw input or output image.
 */
#define EXYNOS3250_CHROMA_BASE			0x10c

/* Stride of source or destination chroma(Cb) raw image buffer */
#define EXYNOS3250_CHROMA_STRIDE		0x110

/* Horizontal/vertical offset of active region in chroma(Cb) raw image buffer */
#define EXYNOS3250_CHROMA_XY_OFFSET		0x114
#define EXYNOS3250_CHROMA_YY_OFFSET_SHIFT	18
#define EXYNOS3250_CHROMA_YY_OFFSET_MASK	(0x1fff << EXYNOS3250_CHROMA_YY_OFFSET_SHIFT)
#define EXYNOS3250_CHROMA_YX_OFFSET_SHIFT	2
#define EXYNOS3250_CHROMA_YX_OFFSET_MASK	(0x1fff << EXYNOS3250_CHROMA_YX_OFFSET_SHIFT)

/*
 * Base address of the chroma(Cr) component DMA buffer
 * of the raw input or output image.
 */
#define EXYNOS3250_CHROMA_CR_BASE		0x118

/* Stride of source or destination chroma(Cr) raw image buffer */
#define EXYNOS3250_CHROMA_CR_STRIDE		0x11c

/* Horizontal/vertical offset of active region in chroma(Cb) raw image buffer */
#define EXYNOS3250_CHROMA_CR_XY_OFFSET		0x120
#define EXYNOS3250_CHROMA_CR_YY_OFFSET_SHIFT	18
#define EXYNOS3250_CHROMA_CR_YY_OFFSET_MASK	(0x1fff << EXYNOS3250_CHROMA_CR_YY_OFFSET_SHIFT)
#define EXYNOS3250_CHROMA_CR_YX_OFFSET_SHIFT	2
#define EXYNOS3250_CHROMA_CR_YX_OFFSET_MASK	(0x1fff << EXYNOS3250_CHROMA_CR_YX_OFFSET_SHIFT)

/* Raw image data r/w address register */
#define EXYNOS3250_JPG_IMGADR			0x50

/* Source or destination JPEG file DMA buffer address */
#define EXYNOS3250_JPG_JPGADR			0x124

/* Coefficients for RGB-to-YCbCr converter register */
#define EXYNOS3250_JPG_COEF(n)			(0x128 + (((n) - 1) << 2))
#define EXYNOS3250_COEF_SHIFT(j)		((3 - (j)) << 3)
#define EXYNOS3250_COEF_MASK(j)			(0xff << EXYNOS3250_COEF_SHIFT(j))

/* Raw input format setting */
#define EXYNOS3250_JPGCMOD			0x134
#define EXYNOS3250_SRC_TILE_EN			(0x1 << 10)
#define EXYNOS3250_SRC_NV_MASK			(0x1 << 9)
#define EXYNOS3250_SRC_NV12			(0x0 << 9)
#define EXYNOS3250_SRC_NV21			(0x1 << 9)
#define EXYNOS3250_SRC_BIG_ENDIAN_MASK		(0x1 << 8)
#define EXYNOS3250_SRC_BIG_ENDIAN		(0x1 << 8)
#define EXYNOS3250_MODE_SEL_MASK		(0x7 << 5)
#define EXYNOS3250_MODE_SEL_420_2P		(0x0 << 5)
#define EXYNOS3250_MODE_SEL_422_1P_LUM_CHR	(0x1 << 5)
#define EXYNOS3250_MODE_SEL_RGB565		(0x2 << 5)
#define EXYNOS3250_MODE_SEL_422_1P_CHR_LUM	(0x3 << 5)
#define EXYNOS3250_MODE_SEL_ARGB8888		(0x4 << 5)
#define EXYNOS3250_MODE_SEL_420_3P		(0x5 << 5)
#define EXYNOS3250_SRC_SWAP_RGB			(0x1 << 3)
#define EXYNOS3250_SRC_SWAP_UV			(0x1 << 2)
#define EXYNOS3250_MODE_Y16_MASK		(0x1 << 1)
#define EXYNOS3250_MODE_Y16			(0x1 << 1)
#define EXYNOS3250_HALF_EN_MASK			(0x1 << 0)
#define EXYNOS3250_HALF_EN			(0x1 << 0)

/* Power on/off and clock down control */
#define EXYNOS3250_JPGCLKCON			0x138
#define EXYNOS3250_CLK_DOWN_READY		(0x1 << 1)
#define EXYNOS3250_POWER_ON			(0x1 << 0)

/* Start compression or decompression */
#define EXYNOS3250_JSTART			0x13c

/* Restart decompression after header analysis */
#define EXYNOS3250_JRSTART			0x140

/* JPEG SW reset register */
#define EXYNOS3250_SW_RESET			0x144

/* JPEG timer setting register */
#define EXYNOS3250_TIMER_SE			0x148
#define EXYNOS3250_TIMER_INT_EN_SHIFT		31
#define EXYNOS3250_TIMER_INT_EN			(1 << EXYNOS3250_TIMER_INT_EN_SHIFT)
#define EXYNOS3250_TIMER_INIT_MASK		0x7fffffff

/* JPEG timer status register */
#define EXYNOS3250_TIMER_ST			0x14c
#define EXYNOS3250_TIMER_INT_STAT_SHIFT		31
#define EXYNOS3250_TIMER_INT_STAT		(1 << EXYNOS3250_TIMER_INT_STAT_SHIFT)
#define EXYNOS3250_TIMER_CNT_SHIFT		0
#define EXYNOS3250_TIMER_CNT_MASK		0x7fffffff

/* Command status register */
#define EXYNOS3250_COMSTAT			0x150
#define EXYNOS3250_CUR_PROC_MODE		(0x1 << 1)
#define EXYNOS3250_CUR_COM_MODE			(0x1 << 0)

/* JPEG decompression output format register */
#define EXYNOS3250_OUTFORM			0x154
#define EXYNOS3250_OUT_ALPHA_MASK		(0xff << 24)
#define EXYNOS3250_OUT_TILE_EN			(0x1 << 10)
#define EXYNOS3250_OUT_NV_MASK			(0x1 << 9)
#define EXYNOS3250_OUT_NV12			(0x0 << 9)
#define EXYNOS3250_OUT_NV21			(0x1 << 9)
#define EXYNOS3250_OUT_BIG_ENDIAN_MASK		(0x1 << 8)
#define EXYNOS3250_OUT_BIG_ENDIAN		(0x1 << 8)
#define EXYNOS3250_OUT_SWAP_RGB			(0x1 << 7)
#define EXYNOS3250_OUT_SWAP_UV			(0x1 << 6)
#define EXYNOS3250_OUT_FMT_MASK			(0x7 << 0)
#define EXYNOS3250_OUT_FMT_420_2P		(0x0 << 0)
#define EXYNOS3250_OUT_FMT_422_1P_LUM_CHR	(0x1 << 0)
#define EXYNOS3250_OUT_FMT_422_1P_CHR_LUM	(0x3 << 0)
#define EXYNOS3250_OUT_FMT_420_3P		(0x4 << 0)
#define EXYNOS3250_OUT_FMT_RGB565		(0x5 << 0)
#define EXYNOS3250_OUT_FMT_ARGB8888		(0x6 << 0)

/* Input JPEG stream byte size for decompression */
#define EXYNOS3250_DEC_STREAM_SIZE		0x158
#define EXYNOS3250_DEC_STREAM_MASK		0x1fffffff

/* The upper bound of the byte size of output compressed stream */
#define EXYNOS3250_ENC_STREAM_BOUND		0x15c
#define EXYNOS3250_ENC_STREAM_BOUND_MASK	0xffffc0

/* Scale-down ratio when decoding */
#define EXYNOS3250_DEC_SCALING_RATIO		0x160
#define EXYNOS3250_DEC_SCALE_FACTOR_MASK	0x3
#define EXYNOS3250_DEC_SCALE_FACTOR_8_8		0x0
#define EXYNOS3250_DEC_SCALE_FACTOR_4_8		0x1
#define EXYNOS3250_DEC_SCALE_FACTOR_2_8		0x2
#define EXYNOS3250_DEC_SCALE_FACTOR_1_8		0x3

/* Error check */
#define EXYNOS3250_CRC_RESULT			0x164

/* RDMA and WDMA operation status register */
#define EXYNOS3250_DMA_OPER_STATUS		0x168
#define EXYNOS3250_WDMA_OPER_STATUS		(0x1 << 1)
#define EXYNOS3250_RDMA_OPER_STATUS		(0x1 << 0)

/* DMA issue gathering number and issue number settings */
#define EXYNOS3250_DMA_ISSUE_NUM		0x16c
#define EXYNOS3250_WDMA_ISSUE_NUM_SHIFT		16
#define EXYNOS3250_WDMA_ISSUE_NUM_MASK		(0x7 << EXYNOS3250_WDMA_ISSUE_NUM_SHIFT)
#define EXYNOS3250_RDMA_ISSUE_NUM_SHIFT		8
#define EXYNOS3250_RDMA_ISSUE_NUM_MASK		(0x7 << EXYNOS3250_RDMA_ISSUE_NUM_SHIFT)
#define EXYNOS3250_ISSUE_GATHER_NUM_SHIFT	0
#define EXYNOS3250_ISSUE_GATHER_NUM_MASK	(0x7 << EXYNOS3250_ISSUE_GATHER_NUM_SHIFT)
#define EXYNOS3250_DMA_MO_COUNT			0x7

/* Version register */
#define EXYNOS3250_VERSION			0x1fc

/* RGB <-> YUV conversion coefficients */
#define EXYNOS3250_JPEG_ENC_COEF1		0x01352e1e
#define EXYNOS3250_JPEG_ENC_COEF2		0x00b0ae83
#define EXYNOS3250_JPEG_ENC_COEF3		0x020cdc13

#define EXYNOS3250_JPEG_DEC_COEF1		0x04a80199
#define EXYNOS3250_JPEG_DEC_COEF2		0x04a9a064
#define EXYNOS3250_JPEG_DEC_COEF3		0x04a80102

#endif /* JPEG_REGS_H_ */

