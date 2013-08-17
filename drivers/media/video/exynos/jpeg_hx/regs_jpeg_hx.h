/* linux/driver/media/exynos/jpeg_hx/regs-jpeg_hx.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Register definition file for Samsung JPEG hx Encoder/Decoder
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARM_REGS_JPEG_HX_H
#define __ASM_ARM_REGS_JPEG_HX_H

#define MO_COUNT		0x07

/* JPEG Registers part */

/* JPEG Codec Control Registers */
#define JPEG_MOD_REG		0x00 /* Sub-sampling mode register */
#define JPEG_OPR_REG		0x04 /* Operation status register */
#define JPEG_QHTBL_REG		0x08 /* Quantization table number register and Huffman table number register */

#define JPEG_QHTBL			0x08

#define JPEG_DRI_REG		0x0c /* MCU, which inserts RST marker */
#define JPEG_Y_SIZE_REG		0x10 /* Vertical resolution */
#define JPEG_X_SIZE_REG		0x14 /* Horizontal resolution */
#define JPEG_CNT_REG		0x18 /* The amount of the compressed data in bytes */
#define JPEG_INT_SET_REG		0x1c /* Interrupt setting register */
#define JPEG_INT_STATUS_REG		0x20 /* Interrupt status register */

#define JPEG_LUMA_BASE_REG		0x100 /* Base address of source or destination luma raw image buffer */
#define JPEG_LUMA_STRIDE_REG		0x104 /* Stride of source or destination luma raw image buffer */
#define JPEG_LUMA_XY_OFFSET_REG		0x108 /* Horizontal/vertical offset of active region in luma raw image buffer */

#define JPEG_CHROMA_BASE_REG		0x10c /* Base address of source or destination chroma raw image buffer */
#define JPEG_CHROMA_STRIDE_REG		0x110 /* Stride of source or destination chroma raw image buffer */
#define JPEG_CHROMA_XY_OFFSET_REG		0x114 /* Horizontal/vertical offset of active region in chroma raw image buffer */

#define JPEG_IMG_ADDRESS_REG		0x118 /* Source or destination JPEG file address */

#define JPEG_COEF1_REG		0x11c /* Coefficient values for RGB   YCbCr converter */
#define JPEG_COEF2_REG		0x120 /* Coefficient values for RGB   YCbCr converter */
#define JPEG_COEF3_REG		0x124 /* Coefficient values for RGB   YCbCr converter */

#define JPEG_CMOD_REG		0x128 /* Mode selection and core clock setting */

#define JPEG_CLK_CON_REG		0x12c /* Power on/off and clock down control */

#define JPEG_START_REG		0x130 /* Start compression or decompression */
#define JPEG_RE_START_REG		0x134 /* Restart decompression after header analysis */
#define JPEG_SW_RESET_REG		0x138 /* S/W reset */

#define JPEG_TIMER_SET_REG		0x13c /* Internal timer setting register */
#define JPEG_TIMER_STATUS_REG		0x140 /* Internal timer status register */
#define JPEG_COMMAND_STATUS_REG		0x144 /* Command status register */

#define JPEG_OUT_FORMAT_REG		0x148 /* Output color format of decompression */

#define JPEG_DEC_STREAM_SIZE_REG		0x14c /* Input jpeg stream byte size for decompression */
#define JPEG_ENC_STREAM_BOUND_REG		0x150 /* Compressed stream size interrupt setting register */

#define JPEG_DEC_SCALE_RATIO_REG		0x154 /* Scale-down ratio when decoding */
#define JPEG_CRC_RESULT_REG		0x158 /* Error check */

#define JPEG_DMA_OPER_STATUS_REG		0x15c /* Check operation of the RDMA and WDMA */
#define JPEG_DMA_ISSUE_NUM_REG		0x160 /* Set issue gathering number and issue number of DMA */

/* JPEG quantizer table register */
#define JPEG_QTBL_CONTENT(n)		(0x400 + (n) * 0x100)

/* JPEG DC Huffman table register */
#define JPEG_HDCTBL(n)		(0x800 + (n) * 0x400)

/* JPEG DC Huffman table register */
#define JPEG_HDCTBLG(n)		(0x840 + (n) * 0x400)

/* JPEG AC Huffman table register */
#define JPEG_HACTBL(n)		(0x880 + (n) * 0x400)

/* JPEG AC Huffman table register */
#define JPEG_HACTBLG(n)		(0x8c0 + (n) * 0x400)

#define JPEG_QTBL_0_REG			0x400
#define JPEG_QTBL_1_REG			0x500
#define JPEG_QTBL_2_REG			0x600
#define JPEG_QTBL_3_REG			0x700

#define JPEG_HDCTBL_0_REG			0x800
#define JPEG_HDCTBLG_0_REG			0x840
#define JPEG_HACTBL_0_REG			0x880
#define JPEG_HACTBLG_0_REG			0x8c0
#define JPEG_HDCTBL_1_REG			0xc00
#define JPEG_HDCTBLG_1_REG			0xc40
#define JPEG_HACTBL_1_REG			0xc80
#define JPEG_HACTBLG_1_REG			0xcc0

/****************************************************************/
/* Bit definition part												*/
/****************************************************************/

/* JPEG_MOD Register bit */
#define JPEG_SUBSAMPLE_MODE_SHIFT			0
#define JPEG_SUBSAMPLE_MODE_MASK			(7 << JPEG_SUBSAMPLE_MODE_SHIFT)
#define JPEG_SUBSAMPLE_444			(0 << JPEG_SUBSAMPLE_MODE_SHIFT)
#define JPEG_SUBSAMPLE_422			(1 << JPEG_SUBSAMPLE_MODE_SHIFT)
#define JPEG_SUBSAMPLE_420			(2 << JPEG_SUBSAMPLE_MODE_SHIFT)
#define JPEG_SUBSAMPLE_411			(6 << JPEG_SUBSAMPLE_MODE_SHIFT)
#define JPEG_SUBSAMPLE_GRAY		(3 << JPEG_SUBSAMPLE_MODE_SHIFT)

#define JPEG_PROC_MODE_SHIFT		3
#define JPEG_PROC_MODE_MASK		(1 << JPEG_PROC_MODE_SHIFT)
#define JPEG_PROC_ENC					(0 << JPEG_PROC_MODE_SHIFT)	/* encoding mode */
#define JPEG_PROC_DEC					(1 << JPEG_PROC_MODE_SHIFT)	/* decoding mode */
/* JPEG_CMOD Register bit */
#define JPEG_HALF_EN_MASK			(1 << 0)

/* JPEG_QHTBL Register bit */
#define JPEG_QT_NUM1_SHIFT		8
#define JPEG_QT_NUM1_MASK		(0x3 << JPEG_QT_NUM1_SHIFT)
#define JPEG_QT_NUM2_SHIFT		10
#define JPEG_QT_NUM2_MASK		(0x3 << JPEG_QT_NUM2_SHIFT)
#define JPEG_QT_NUM3_SHIFT		12
#define JPEG_QT_NUM3_MASK		(0x3 << JPEG_QT_NUM3_SHIFT)

#define JPEG_HT_NUM3_AC_SHIFT		5
#define JPEG_HT_NUM3_AC_MASK		(0x1 << JPEG_HT_NUM3_AC_SHIFT)
#define JPEG_HT_NUM3_DC_SHIFT		4
#define JPEG_HT_NUM3_DC_MASK		(0x1 << JPEG_HT_NUM3_DC_SHIFT)
#define JPEG_HT_NUM2_AC_SHIFT		3
#define JPEG_HT_NUM2_AC_MASK		(0x1 << JPEG_HT_NUM2_AC_SHIFT)
#define JPEG_HT_NUM2_DC_SHIFT		2
#define JPEG_HT_NUM2_DC_MASK		(0x1 << JPEG_HT_NUM2_DC_SHIFT)
#define JPEG_HT_NUM1_AC_SHIFT		1
#define JPEG_HT_NUM1_AC_MASK		(0x1 << JPEG_HT_NUM1_AC_SHIFT)
#define JPEG_HT_NUM1_DC_SHIFT		0
#define JPEG_HT_NUM1_DC_MASK		(0x1 << JPEG_HT_NUM1_DC_SHIFT)

#define JPEG_Q_TBL_Y_0		(0 << JPEG_QT_NUM1_SHIFT)
#define JPEG_Q_TBL_Y_1		(1 << JPEG_QT_NUM1_SHIFT)
#define JPEG_Q_TBL_Cb_0		(0 << JPEG_QT_NUM2_SHIFT)
#define JPEG_Q_TBL_Cb_1		(1 << JPEG_QT_NUM2_SHIFT)
#define JPEG_Q_TBL_Cr_0		(0 << JPEG_QT_NUM3_SHIFT)
#define JPEG_Q_TBL_Cr_1		(1 << JPEG_QT_NUM3_SHIFT)

#define JPEG_HUFF_TBL_Y_AC_0		(0 << JPEG_HT_NUM1_AC_SHIFT)
#define JPEG_HUFF_TBL_Y_AC_1		(1 << JPEG_HT_NUM1_AC_SHIFT)
#define JPEG_HUFF_TBL_Y_DC_0		(0 << JPEG_HT_NUM1_DC_SHIFT)
#define JPEG_HUFF_TBL_Y_DC_1		(1 << JPEG_HT_NUM1_DC_SHIFT)

#define JPEG_HUFF_TBL_Cb_AC_0		(0 << JPEG_HT_NUM2_AC_SHIFT)
#define JPEG_HUFF_TBL_Cb_AC_1		(1 << JPEG_HT_NUM2_AC_SHIFT)
#define JPEG_HUFF_TBL_Cb_DC_0		(0 << JPEG_HT_NUM2_DC_SHIFT)
#define JPEG_HUFF_TBL_Cb_DC_1		(1 << JPEG_HT_NUM2_DC_SHIFT)

#define JPEG_HUFF_TBL_Cr_AC_0		(0 << JPEG_HT_NUM3_AC_SHIFT)
#define JPEG_HUFF_TBL_Cr_AC_1		(1 << JPEG_HT_NUM3_AC_SHIFT)
#define JPEG_HUFF_TBL_Cr_DC_0		(0 << JPEG_HT_NUM3_DC_SHIFT)
#define JPEG_HUFF_TBL_Cr_DC_1		(1 << JPEG_HT_NUM3_DC_SHIFT)

/* JPEG_CLK_CON Register bit */
#define JPEG_CLK_ON			(1 << 0)
#define JPEG_CLK_OFF			(0 << 0)
#define JPEG_MOD_SEL_SHIFT			5
#define JPEG_MOD_SEL_MASK			(7 << JPEG_MOD_SEL_SHIFT)
#define JPEG_MOD_YUV_420		(0 << JPEG_MOD_SEL_SHIFT)
#define JPEG_MOD_YUV_422		(1 << JPEG_MOD_SEL_SHIFT)
#define JPEG_MOD_YUV_422_2YUV		(3 << JPEG_MOD_SEL_SHIFT)

/* JPEG_SW_RESET Register bit */
#define JPEG_SW_RESET_ENABLE			(1 << 0)

/* JPEG_LUMA_OFFSET_RESET Register bit */
#define JPEG_LUMA_X_OFFSET_SHIFT			2
#define JPEG_LUMA_Y_OFFSET_SHIFT			18

/* JPEG_CHROMA_OFFSET_RESET Register bit */
#define JPEG_CHROMA_X_OFFSET_SHIFT			2
#define JPEG_CHROMA_Y_OFFSET_SHIFT			18

/* JPEG_CMOD Register bit */
#define JPEG_SRC_NV_SHIFT		9
#define JPEG_SRC_NV_MASK		(1 << JPEG_SRC_NV_SHIFT)
#define JPEG_SRC_NV_21		(1 << JPEG_SRC_NV_SHIFT)
#define JPEG_SRC_NV_12		(0 << JPEG_SRC_NV_SHIFT)

#define JPEG_SRC_TILE_EN_SHIFT		10
#define JPEG_SRC_TILE_EN_MASK		(1 << JPEG_SRC_TILE_EN_SHIFT)
#define JPEG_SRC_TILE_EN			(1 << JPEG_SRC_TILE_EN_SHIFT)	/* tile mode */
#define JPEG_SRC_LINEAR_EN			(0 << JPEG_SRC_TILE_EN_SHIFT)	/* tile mode */

#define JPEG_SRC_MOD_SEL_SHIFT		5
#define JPEG_SRC_MOD_SEL_MASK		(7 << JPEG_SRC_MOD_SEL_SHIFT)
#define JPEG_SRC_YUV_420_2P		(0 << JPEG_SRC_MOD_SEL_SHIFT)
#define JPEG_SRC_YUYV		(1 << JPEG_SRC_MOD_SEL_SHIFT)
#define JPEG_SRC_RGB565		(2 << JPEG_SRC_MOD_SEL_SHIFT)
#define JPEG_SRC_UYUV		(3 << JPEG_SRC_MOD_SEL_SHIFT)

#define JPEG_SRC_MODE_Y16_SHIFT		1
#define JPEG_SRC_MODE_Y16_MASK		(1 << JPEG_SRC_MODE_Y16_SHIFT)
#define JPEG_SRC_C1_0			(0 << JPEG_SRC_MODE_Y16_SHIFT)
#define JPEG_SRC_C1_16			(1 << JPEG_SRC_MODE_Y16_SHIFT)

/* JPEG_OUT_FORMAT Register bit */
#define JPEG_DEC_OUT_FORMAT_SHIFT		0
#define JPEG_DEC_OUT_FORMAT_MASK			(3 << JPEG_DEC_OUT_FORMAT_SHIFT)
#define JPEG_DEC_YUV_420		(0 << JPEG_DEC_OUT_FORMAT_SHIFT)
#define JPEG_DEC_YUYV		(1 << JPEG_DEC_OUT_FORMAT_SHIFT)
#define JPEG_DEC_UYVY		(3 << JPEG_DEC_OUT_FORMAT_SHIFT)

#define JPEG_OUT_BIG_ENDIAN_SHIFT		8
#define JPEG_OUT_BIG_ENDIAN_MASK		(1 << JPEG_OUT_BIG_ENDIAN_SHIFT)
#define JPEG_DEC_BIG_ENDIAN		(1 << JPEG_OUT_BIG_ENDIAN_SHIFT)	/* big endian source format */
#define JPEG_DEC_LINEAR_ENDIAN	(0 << JPEG_OUT_BIG_ENDIAN_SHIFT)	/* big endian source format */

/* JPEG_OUT_FORMAT Register bit */
#define JPEG_OUT_NV_SHIFT		9
#define JPEG_OUT_NV_MASK		(1 << JPEG_OUT_NV_SHIFT)
#define JPEG_OUT_NV_21		(1 << JPEG_OUT_NV_SHIFT)
#define JPEG_OUT_NV_12		(0 << JPEG_OUT_NV_SHIFT)

#define JPEG_OUT_TILE_EN_SHIFT		10
#define JPEG_OUT_TILE_EN_MASK		(1 << JPEG_OUT_TILE_EN_SHIFT)
#define JPEG_DEC_TILE_EN			(1 << JPEG_OUT_TILE_EN_SHIFT)	/* tile mode */
#define JPEG_DEC_LINEAR_EN			(0 << JPEG_OUT_TILE_EN_SHIFT)	/* tile mode */

/* JPEG_DEC_STREAM_SIZE Register bit */

#define JPEG_ALL_INT_EN				((0xf << 8) | (0x7<<3))

/* JPEG_TIMER_SET Register bit */
#define JPEG_TIMER_INT_SHIFT				31
#define JPEG_TIMER_INT_EN				(1 << JPEG_TIMER_INT_SHIFT)

#endif /* __ASM_ARM_REGS_JPEG_HX_H */

