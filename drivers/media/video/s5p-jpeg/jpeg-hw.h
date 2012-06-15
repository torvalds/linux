/* linux/drivers/media/video/s5p-jpeg/jpeg-hw.h
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
#ifndef JPEG_HW_H_
#define JPEG_HW_H_

#include <linux/io.h>
#include <linux/videodev2.h>

#include "jpeg-hw.h"
#include "jpeg-regs.h"

#define S5P_JPEG_MIN_WIDTH		32
#define S5P_JPEG_MIN_HEIGHT		32
#define S5P_JPEG_MAX_WIDTH		8192
#define S5P_JPEG_MAX_HEIGHT		8192
#define S5P_JPEG_ENCODE			0
#define S5P_JPEG_DECODE			1
#define S5P_JPEG_RAW_IN_565		0
#define S5P_JPEG_RAW_IN_422		1
#define S5P_JPEG_RAW_OUT_422		0
#define S5P_JPEG_RAW_OUT_420		1

static inline void jpeg_reset(void __iomem *regs)
{
	unsigned long reg;

	writel(1, regs + S5P_JPG_SW_RESET);
	reg = readl(regs + S5P_JPG_SW_RESET);
	/* no other way but polling for when JPEG IP becomes operational */
	while (reg != 0) {
		cpu_relax();
		reg = readl(regs + S5P_JPG_SW_RESET);
	}
}

static inline void jpeg_poweron(void __iomem *regs)
{
	writel(S5P_POWER_ON, regs + S5P_JPGCLKCON);
}

static inline void jpeg_input_raw_mode(void __iomem *regs, unsigned long mode)
{
	unsigned long reg, m;

	m = S5P_MOD_SEL_565;
	if (mode == S5P_JPEG_RAW_IN_565)
		m = S5P_MOD_SEL_565;
	else if (mode == S5P_JPEG_RAW_IN_422)
		m = S5P_MOD_SEL_422;

	reg = readl(regs + S5P_JPGCMOD);
	reg &= ~S5P_MOD_SEL_MASK;
	reg |= m;
	writel(reg, regs + S5P_JPGCMOD);
}

static inline void jpeg_input_raw_y16(void __iomem *regs, bool y16)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPGCMOD);
	if (y16)
		reg |= S5P_MODE_Y16;
	else
		reg &= ~S5P_MODE_Y16_MASK;
	writel(reg, regs + S5P_JPGCMOD);
}

static inline void jpeg_proc_mode(void __iomem *regs, unsigned long mode)
{
	unsigned long reg, m;

	m = S5P_PROC_MODE_DECOMPR;
	if (mode == S5P_JPEG_ENCODE)
		m = S5P_PROC_MODE_COMPR;
	else
		m = S5P_PROC_MODE_DECOMPR;
	reg = readl(regs + S5P_JPGMOD);
	reg &= ~S5P_PROC_MODE_MASK;
	reg |= m;
	writel(reg, regs + S5P_JPGMOD);
}

static inline void jpeg_subsampling_mode(void __iomem *regs, unsigned int mode)
{
	unsigned long reg, m;

	if (mode == V4L2_JPEG_CHROMA_SUBSAMPLING_420)
		m = S5P_SUBSAMPLING_MODE_420;
	else
		m = S5P_SUBSAMPLING_MODE_422;

	reg = readl(regs + S5P_JPGMOD);
	reg &= ~S5P_SUBSAMPLING_MODE_MASK;
	reg |= m;
	writel(reg, regs + S5P_JPGMOD);
}

static inline unsigned int jpeg_get_subsampling_mode(void __iomem *regs)
{
	return readl(regs + S5P_JPGMOD) & S5P_SUBSAMPLING_MODE_MASK;
}

static inline void jpeg_dri(void __iomem *regs, unsigned int dri)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPGDRI_U);
	reg &= ~0xff;
	reg |= (dri >> 8) & 0xff;
	writel(reg, regs + S5P_JPGDRI_U);

	reg = readl(regs + S5P_JPGDRI_L);
	reg &= ~0xff;
	reg |= dri & 0xff;
	writel(reg, regs + S5P_JPGDRI_L);
}

static inline void jpeg_qtbl(void __iomem *regs, unsigned int t, unsigned int n)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_QTBL);
	reg &= ~S5P_QT_NUMt_MASK(t);
	reg |= (n << S5P_QT_NUMt_SHIFT(t)) & S5P_QT_NUMt_MASK(t);
	writel(reg, regs + S5P_JPG_QTBL);
}

static inline void jpeg_htbl_ac(void __iomem *regs, unsigned int t)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_HTBL);
	reg &= ~S5P_HT_NUMt_AC_MASK(t);
	/* this driver uses table 0 for all color components */
	reg |= (0 << S5P_HT_NUMt_AC_SHIFT(t)) & S5P_HT_NUMt_AC_MASK(t);
	writel(reg, regs + S5P_JPG_HTBL);
}

static inline void jpeg_htbl_dc(void __iomem *regs, unsigned int t)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_HTBL);
	reg &= ~S5P_HT_NUMt_DC_MASK(t);
	/* this driver uses table 0 for all color components */
	reg |= (0 << S5P_HT_NUMt_DC_SHIFT(t)) & S5P_HT_NUMt_DC_MASK(t);
	writel(reg, regs + S5P_JPG_HTBL);
}

static inline void jpeg_y(void __iomem *regs, unsigned int y)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPGY_U);
	reg &= ~0xff;
	reg |= (y >> 8) & 0xff;
	writel(reg, regs + S5P_JPGY_U);

	reg = readl(regs + S5P_JPGY_L);
	reg &= ~0xff;
	reg |= y & 0xff;
	writel(reg, regs + S5P_JPGY_L);
}

static inline void jpeg_x(void __iomem *regs, unsigned int x)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPGX_U);
	reg &= ~0xff;
	reg |= (x >> 8) & 0xff;
	writel(reg, regs + S5P_JPGX_U);

	reg = readl(regs + S5P_JPGX_L);
	reg &= ~0xff;
	reg |= x & 0xff;
	writel(reg, regs + S5P_JPGX_L);
}

static inline void jpeg_rst_int_enable(void __iomem *regs, bool enable)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPGINTSE);
	reg &= ~S5P_RSTm_INT_EN_MASK;
	if (enable)
		reg |= S5P_RSTm_INT_EN;
	writel(reg, regs + S5P_JPGINTSE);
}

static inline void jpeg_data_num_int_enable(void __iomem *regs, bool enable)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPGINTSE);
	reg &= ~S5P_DATA_NUM_INT_EN_MASK;
	if (enable)
		reg |= S5P_DATA_NUM_INT_EN;
	writel(reg, regs + S5P_JPGINTSE);
}

static inline void jpeg_final_mcu_num_int_enable(void __iomem *regs, bool enbl)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPGINTSE);
	reg &= ~S5P_FINAL_MCU_NUM_INT_EN_MASK;
	if (enbl)
		reg |= S5P_FINAL_MCU_NUM_INT_EN;
	writel(reg, regs + S5P_JPGINTSE);
}

static inline void jpeg_timer_enable(void __iomem *regs, unsigned long val)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_TIMER_SE);
	reg |= S5P_TIMER_INT_EN;
	reg &= ~S5P_TIMER_INIT_MASK;
	reg |= val & S5P_TIMER_INIT_MASK;
	writel(reg, regs + S5P_JPG_TIMER_SE);
}

static inline void jpeg_timer_disable(void __iomem *regs)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_TIMER_SE);
	reg &= ~S5P_TIMER_INT_EN_MASK;
	writel(reg, regs + S5P_JPG_TIMER_SE);
}

static inline int jpeg_timer_stat(void __iomem *regs)
{
	return (int)((readl(regs + S5P_JPG_TIMER_ST) & S5P_TIMER_INT_STAT_MASK)
		     >> S5P_TIMER_INT_STAT_SHIFT);
}

static inline void jpeg_clear_timer_stat(void __iomem *regs)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_TIMER_SE);
	reg &= ~S5P_TIMER_INT_STAT_MASK;
	writel(reg, regs + S5P_JPG_TIMER_SE);
}

static inline void jpeg_enc_stream_int(void __iomem *regs, unsigned long size)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_ENC_STREAM_INTSE);
	reg &= ~S5P_ENC_STREAM_BOUND_MASK;
	reg |= S5P_ENC_STREAM_INT_EN;
	reg |= size & S5P_ENC_STREAM_BOUND_MASK;
	writel(reg, regs + S5P_JPG_ENC_STREAM_INTSE);
}

static inline int jpeg_enc_stream_stat(void __iomem *regs)
{
	return (int)(readl(regs + S5P_JPG_ENC_STREAM_INTST) &
		     S5P_ENC_STREAM_INT_STAT_MASK);
}

static inline void jpeg_clear_enc_stream_stat(void __iomem *regs)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_ENC_STREAM_INTSE);
	reg &= ~S5P_ENC_STREAM_INT_MASK;
	writel(reg, regs + S5P_JPG_ENC_STREAM_INTSE);
}

static inline void jpeg_outform_raw(void __iomem *regs, unsigned long format)
{
	unsigned long reg, f;

	f = S5P_DEC_OUT_FORMAT_422;
	if (format == S5P_JPEG_RAW_OUT_422)
		f = S5P_DEC_OUT_FORMAT_422;
	else if (format == S5P_JPEG_RAW_OUT_420)
		f = S5P_DEC_OUT_FORMAT_420;
	reg = readl(regs + S5P_JPG_OUTFORM);
	reg &= ~S5P_DEC_OUT_FORMAT_MASK;
	reg |= f;
	writel(reg, regs + S5P_JPG_OUTFORM);
}

static inline void jpeg_jpgadr(void __iomem *regs, unsigned long addr)
{
	writel(addr, regs + S5P_JPG_JPGADR);
}

static inline void jpeg_imgadr(void __iomem *regs, unsigned long addr)
{
	writel(addr, regs + S5P_JPG_IMGADR);
}

static inline void jpeg_coef(void __iomem *regs, unsigned int i,
			     unsigned int j, unsigned int coef)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_COEF(i));
	reg &= ~S5P_COEFn_MASK(j);
	reg |= (coef << S5P_COEFn_SHIFT(j)) & S5P_COEFn_MASK(j);
	writel(reg, regs + S5P_JPG_COEF(i));
}

static inline void jpeg_start(void __iomem *regs)
{
	writel(1, regs + S5P_JSTART);
}

static inline int jpeg_result_stat_ok(void __iomem *regs)
{
	return (int)((readl(regs + S5P_JPGINTST) & S5P_RESULT_STAT_MASK)
		     >> S5P_RESULT_STAT_SHIFT);
}

static inline int jpeg_stream_stat_ok(void __iomem *regs)
{
	return !(int)((readl(regs + S5P_JPGINTST) & S5P_STREAM_STAT_MASK)
		      >> S5P_STREAM_STAT_SHIFT);
}

static inline void jpeg_clear_int(void __iomem *regs)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPGINTST);
	writel(S5P_INT_RELEASE, regs + S5P_JPGCOM);
	reg = readl(regs + S5P_JPGOPR);
}

static inline unsigned int jpeg_compressed_size(void __iomem *regs)
{
	unsigned long jpeg_size = 0;

	jpeg_size |= (readl(regs + S5P_JPGCNT_U) & 0xff) << 16;
	jpeg_size |= (readl(regs + S5P_JPGCNT_M) & 0xff) << 8;
	jpeg_size |= (readl(regs + S5P_JPGCNT_L) & 0xff);

	return (unsigned int)jpeg_size;
}

#endif /* JPEG_HW_H_ */
