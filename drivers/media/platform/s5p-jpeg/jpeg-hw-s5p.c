/* linux/drivers/media/platform/s5p-jpeg/jpeg-hw.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/videodev2.h>

#include "jpeg-core.h"
#include "jpeg-regs.h"
#include "jpeg-hw-s5p.h"

void s5p_jpeg_reset(void __iomem *regs)
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

void s5p_jpeg_poweron(void __iomem *regs)
{
	writel(S5P_POWER_ON, regs + S5P_JPGCLKCON);
}

void s5p_jpeg_input_raw_mode(void __iomem *regs, unsigned long mode)
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

void s5p_jpeg_proc_mode(void __iomem *regs, unsigned long mode)
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

void s5p_jpeg_subsampling_mode(void __iomem *regs, unsigned int mode)
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

unsigned int s5p_jpeg_get_subsampling_mode(void __iomem *regs)
{
	return readl(regs + S5P_JPGMOD) & S5P_SUBSAMPLING_MODE_MASK;
}

void s5p_jpeg_dri(void __iomem *regs, unsigned int dri)
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

void s5p_jpeg_qtbl(void __iomem *regs, unsigned int t, unsigned int n)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_QTBL);
	reg &= ~S5P_QT_NUMt_MASK(t);
	reg |= (n << S5P_QT_NUMt_SHIFT(t)) & S5P_QT_NUMt_MASK(t);
	writel(reg, regs + S5P_JPG_QTBL);
}

void s5p_jpeg_htbl_ac(void __iomem *regs, unsigned int t)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_HTBL);
	reg &= ~S5P_HT_NUMt_AC_MASK(t);
	/* this driver uses table 0 for all color components */
	reg |= (0 << S5P_HT_NUMt_AC_SHIFT(t)) & S5P_HT_NUMt_AC_MASK(t);
	writel(reg, regs + S5P_JPG_HTBL);
}

void s5p_jpeg_htbl_dc(void __iomem *regs, unsigned int t)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_HTBL);
	reg &= ~S5P_HT_NUMt_DC_MASK(t);
	/* this driver uses table 0 for all color components */
	reg |= (0 << S5P_HT_NUMt_DC_SHIFT(t)) & S5P_HT_NUMt_DC_MASK(t);
	writel(reg, regs + S5P_JPG_HTBL);
}

void s5p_jpeg_y(void __iomem *regs, unsigned int y)
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

void s5p_jpeg_x(void __iomem *regs, unsigned int x)
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

void s5p_jpeg_rst_int_enable(void __iomem *regs, bool enable)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPGINTSE);
	reg &= ~S5P_RSTm_INT_EN_MASK;
	if (enable)
		reg |= S5P_RSTm_INT_EN;
	writel(reg, regs + S5P_JPGINTSE);
}

void s5p_jpeg_data_num_int_enable(void __iomem *regs, bool enable)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPGINTSE);
	reg &= ~S5P_DATA_NUM_INT_EN_MASK;
	if (enable)
		reg |= S5P_DATA_NUM_INT_EN;
	writel(reg, regs + S5P_JPGINTSE);
}

void s5p_jpeg_final_mcu_num_int_enable(void __iomem *regs, bool enbl)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPGINTSE);
	reg &= ~S5P_FINAL_MCU_NUM_INT_EN_MASK;
	if (enbl)
		reg |= S5P_FINAL_MCU_NUM_INT_EN;
	writel(reg, regs + S5P_JPGINTSE);
}

int s5p_jpeg_timer_stat(void __iomem *regs)
{
	return (int)((readl(regs + S5P_JPG_TIMER_ST) & S5P_TIMER_INT_STAT_MASK)
		     >> S5P_TIMER_INT_STAT_SHIFT);
}

void s5p_jpeg_clear_timer_stat(void __iomem *regs)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_TIMER_SE);
	reg &= ~S5P_TIMER_INT_STAT_MASK;
	writel(reg, regs + S5P_JPG_TIMER_SE);
}

void s5p_jpeg_enc_stream_int(void __iomem *regs, unsigned long size)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_ENC_STREAM_INTSE);
	reg &= ~S5P_ENC_STREAM_BOUND_MASK;
	reg |= S5P_ENC_STREAM_INT_EN;
	reg |= size & S5P_ENC_STREAM_BOUND_MASK;
	writel(reg, regs + S5P_JPG_ENC_STREAM_INTSE);
}

int s5p_jpeg_enc_stream_stat(void __iomem *regs)
{
	return (int)(readl(regs + S5P_JPG_ENC_STREAM_INTST) &
		     S5P_ENC_STREAM_INT_STAT_MASK);
}

void s5p_jpeg_clear_enc_stream_stat(void __iomem *regs)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_ENC_STREAM_INTSE);
	reg &= ~S5P_ENC_STREAM_INT_MASK;
	writel(reg, regs + S5P_JPG_ENC_STREAM_INTSE);
}

void s5p_jpeg_outform_raw(void __iomem *regs, unsigned long format)
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

void s5p_jpeg_jpgadr(void __iomem *regs, unsigned long addr)
{
	writel(addr, regs + S5P_JPG_JPGADR);
}

void s5p_jpeg_imgadr(void __iomem *regs, unsigned long addr)
{
	writel(addr, regs + S5P_JPG_IMGADR);
}

void s5p_jpeg_coef(void __iomem *regs, unsigned int i,
			     unsigned int j, unsigned int coef)
{
	unsigned long reg;

	reg = readl(regs + S5P_JPG_COEF(i));
	reg &= ~S5P_COEFn_MASK(j);
	reg |= (coef << S5P_COEFn_SHIFT(j)) & S5P_COEFn_MASK(j);
	writel(reg, regs + S5P_JPG_COEF(i));
}

void s5p_jpeg_start(void __iomem *regs)
{
	writel(1, regs + S5P_JSTART);
}

int s5p_jpeg_result_stat_ok(void __iomem *regs)
{
	return (int)((readl(regs + S5P_JPGINTST) & S5P_RESULT_STAT_MASK)
		     >> S5P_RESULT_STAT_SHIFT);
}

int s5p_jpeg_stream_stat_ok(void __iomem *regs)
{
	return !(int)((readl(regs + S5P_JPGINTST) & S5P_STREAM_STAT_MASK)
		      >> S5P_STREAM_STAT_SHIFT);
}

void s5p_jpeg_clear_int(void __iomem *regs)
{
	readl(regs + S5P_JPGINTST);
	writel(S5P_INT_RELEASE, regs + S5P_JPGCOM);
	readl(regs + S5P_JPGOPR);
}

unsigned int s5p_jpeg_compressed_size(void __iomem *regs)
{
	unsigned long jpeg_size = 0;

	jpeg_size |= (readl(regs + S5P_JPGCNT_U) & 0xff) << 16;
	jpeg_size |= (readl(regs + S5P_JPGCNT_M) & 0xff) << 8;
	jpeg_size |= (readl(regs + S5P_JPGCNT_L) & 0xff);

	return (unsigned int)jpeg_size;
}
