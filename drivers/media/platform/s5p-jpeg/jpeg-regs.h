/* linux/drivers/media/platform/s5p-jpeg/jpeg-regs.h
 *
 * Register definition file for Samsung JPEG codec driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef JPEG_REGS_H_
#define JPEG_REGS_H_

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

#endif /* JPEG_REGS_H_ */

