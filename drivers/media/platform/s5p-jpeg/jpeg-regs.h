/* linux/drivers/media/platform/s5p-jpeg/jpeg-regs.h
 *
 * Register definition file for Samsung JPEG codec driver
 *
 * Copyright (c) 2011-2013 Samsung Electronics Co., Ltd.
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
#define EXYNOS4_DEC_MODE			(1 << 0)
#define EXYNOS4_ENC_MODE			(1 << 1)
#define EXYNOS4_AUTO_RST_MARKER		(1 << 2)
#define EXYNOS4_RST_INTERVAL_SHIFT	3
#define EXYNOS4_RST_INTERVAL(x)		(((x) & 0xffff) \
						<< EXYNOS4_RST_INTERVAL_SHIFT)
#define EXYNOS4_HUF_TBL_EN		(1 << 19)
#define EXYNOS4_HOR_SCALING_SHIFT	20
#define EXYNOS4_HOR_SCALING_MASK		(3 << EXYNOS4_HOR_SCALING_SHIFT)
#define EXYNOS4_HOR_SCALING(x)		(((x) & 0x3) \
						<< EXYNOS4_HOR_SCALING_SHIFT)
#define EXYNOS4_VER_SCALING_SHIFT	22
#define EXYNOS4_VER_SCALING_MASK		(3 << EXYNOS4_VER_SCALING_SHIFT)
#define EXYNOS4_VER_SCALING(x)		(((x) & 0x3) \
						<< EXYNOS4_VER_SCALING_SHIFT)
#define EXYNOS4_PADDING			(1 << 27)
#define EXYNOS4_SYS_INT_EN		(1 << 28)
#define EXYNOS4_SOFT_RESET_HI		(1 << 29)

/* JPEG INT Register bit */
#define EXYNOS4_INT_EN_MASK		(0x1f << 0)
#define EXYNOS4_PROT_ERR_INT_EN		(1 << 0)
#define EXYNOS4_IMG_COMPLETION_INT_EN	(1 << 1)
#define EXYNOS4_DEC_INVALID_FORMAT_EN	(1 << 2)
#define EXYNOS4_MULTI_SCAN_ERROR_EN	(1 << 3)
#define EXYNOS4_FRAME_ERR_EN		(1 << 4)
#define EXYNOS4_INT_EN_ALL		(0x1f << 0)

#define EXYNOS4_MOD_REG_PROC_ENC		(0 << 3)
#define EXYNOS4_MOD_REG_PROC_DEC		(1 << 3)

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
#define EXYNOS4_GRAY_IMG_IP_MASK		(7 << EXYNOS4_GRAY_IMG_IP_SHIFT)
#define EXYNOS4_GRAY_IMG_IP		(4 << EXYNOS4_GRAY_IMG_IP_SHIFT)

#define EXYNOS4_RGB_IP_SHIFT		6
#define EXYNOS4_RGB_IP_MASK		(7 << EXYNOS4_RGB_IP_SHIFT)
#define EXYNOS4_RGB_IP_RGB_16BIT_IMG	(4 << EXYNOS4_RGB_IP_SHIFT)
#define EXYNOS4_RGB_IP_RGB_32BIT_IMG	(5 << EXYNOS4_RGB_IP_SHIFT)

#define EXYNOS4_YUV_444_IP_SHIFT			9
#define EXYNOS4_YUV_444_IP_MASK			(7 << EXYNOS4_YUV_444_IP_SHIFT)
#define EXYNOS4_YUV_444_IP_YUV_444_2P_IMG	(4 << EXYNOS4_YUV_444_IP_SHIFT)
#define EXYNOS4_YUV_444_IP_YUV_444_3P_IMG	(5 << EXYNOS4_YUV_444_IP_SHIFT)

#define EXYNOS4_YUV_422_IP_SHIFT			12
#define EXYNOS4_YUV_422_IP_MASK			(7 << EXYNOS4_YUV_422_IP_SHIFT)
#define EXYNOS4_YUV_422_IP_YUV_422_1P_IMG	(4 << EXYNOS4_YUV_422_IP_SHIFT)
#define EXYNOS4_YUV_422_IP_YUV_422_2P_IMG	(5 << EXYNOS4_YUV_422_IP_SHIFT)
#define EXYNOS4_YUV_422_IP_YUV_422_3P_IMG	(6 << EXYNOS4_YUV_422_IP_SHIFT)

#define EXYNOS4_YUV_420_IP_SHIFT			15
#define EXYNOS4_YUV_420_IP_MASK			(7 << EXYNOS4_YUV_420_IP_SHIFT)
#define EXYNOS4_YUV_420_IP_YUV_420_2P_IMG	(4 << EXYNOS4_YUV_420_IP_SHIFT)
#define EXYNOS4_YUV_420_IP_YUV_420_3P_IMG	(5 << EXYNOS4_YUV_420_IP_SHIFT)

#define EXYNOS4_ENC_FMT_SHIFT			24
#define EXYNOS4_ENC_FMT_MASK			(3 << EXYNOS4_ENC_FMT_SHIFT)
#define EXYNOS4_ENC_FMT_GRAY			(0 << EXYNOS4_ENC_FMT_SHIFT)
#define EXYNOS4_ENC_FMT_YUV_444			(1 << EXYNOS4_ENC_FMT_SHIFT)
#define EXYNOS4_ENC_FMT_YUV_422			(2 << EXYNOS4_ENC_FMT_SHIFT)
#define EXYNOS4_ENC_FMT_YUV_420			(3 << EXYNOS4_ENC_FMT_SHIFT)

#define EXYNOS4_JPEG_DECODED_IMG_FMT_MASK	0x03

#define EXYNOS4_SWAP_CHROMA_CRCB			(1 << 26)
#define EXYNOS4_SWAP_CHROMA_CBCR			(0 << 26)

/* JPEG HUFF count Register bit */
#define EXYNOS4_HUFF_COUNT_MASK			0xffff

/* JPEG Decoded_img_x_y_size Register bit */
#define EXYNOS4_DECODED_SIZE_MASK		0x0000ffff

/* JPEG Decoded image format Register bit */
#define EXYNOS4_DECODED_IMG_FMT_MASK		0x3

/* JPEG TBL SEL Register bit */
#define EXYNOS4_Q_TBL_COMP1_0		(0 << 0)
#define EXYNOS4_Q_TBL_COMP1_1		(1 << 0)
#define EXYNOS4_Q_TBL_COMP1_2		(2 << 0)
#define EXYNOS4_Q_TBL_COMP1_3		(3 << 0)

#define EXYNOS4_Q_TBL_COMP2_0		(0 << 2)
#define EXYNOS4_Q_TBL_COMP2_1		(1 << 2)
#define EXYNOS4_Q_TBL_COMP2_2		(2 << 2)
#define EXYNOS4_Q_TBL_COMP2_3		(3 << 2)

#define EXYNOS4_Q_TBL_COMP3_0		(0 << 4)
#define EXYNOS4_Q_TBL_COMP3_1		(1 << 4)
#define EXYNOS4_Q_TBL_COMP3_2		(2 << 4)
#define EXYNOS4_Q_TBL_COMP3_3		(3 << 4)

#define EXYNOS4_HUFF_TBL_COMP1_AC_0_DC_0	(0 << 6)
#define EXYNOS4_HUFF_TBL_COMP1_AC_0_DC_1	(1 << 6)
#define EXYNOS4_HUFF_TBL_COMP1_AC_1_DC_0	(2 << 6)
#define EXYNOS4_HUFF_TBL_COMP1_AC_1_DC_1	(3 << 6)

#define EXYNOS4_HUFF_TBL_COMP2_AC_0_DC_0	(0 << 8)
#define EXYNOS4_HUFF_TBL_COMP2_AC_0_DC_1	(1 << 8)
#define EXYNOS4_HUFF_TBL_COMP2_AC_1_DC_0	(2 << 8)
#define EXYNOS4_HUFF_TBL_COMP2_AC_1_DC_1	(3 << 8)

#define EXYNOS4_HUFF_TBL_COMP3_AC_0_DC_0	(0 << 10)
#define EXYNOS4_HUFF_TBL_COMP3_AC_0_DC_1	(1 << 10)
#define EXYNOS4_HUFF_TBL_COMP3_AC_1_DC_0	(2 << 10)
#define EXYNOS4_HUFF_TBL_COMP3_AC_1_DC_1	(3 << 10)

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

#endif /* JPEG_REGS_H_ */

