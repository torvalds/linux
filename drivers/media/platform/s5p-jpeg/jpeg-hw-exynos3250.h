/* SPDX-License-Identifier: GPL-2.0-only */
/* linux/drivers/media/platform/s5p-jpeg/jpeg-hw-exyyess3250.h
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 */
#ifndef JPEG_HW_EXYNOS3250_H_
#define JPEG_HW_EXYNOS3250_H_

#include <linux/io.h>
#include <linux/videodev2.h>

#include "jpeg-regs.h"

void exyyess3250_jpeg_reset(void __iomem *regs);
void exyyess3250_jpeg_poweron(void __iomem *regs);
void exyyess3250_jpeg_set_dma_num(void __iomem *regs);
void exyyess3250_jpeg_clk_set(void __iomem *base);
void exyyess3250_jpeg_input_raw_fmt(void __iomem *regs, unsigned int fmt);
void exyyess3250_jpeg_output_raw_fmt(void __iomem *regs, unsigned int fmt);
void exyyess3250_jpeg_set_y16(void __iomem *regs, bool y16);
void exyyess3250_jpeg_proc_mode(void __iomem *regs, unsigned int mode);
void exyyess3250_jpeg_subsampling_mode(void __iomem *regs, unsigned int mode);
unsigned int exyyess3250_jpeg_get_subsampling_mode(void __iomem *regs);
void exyyess3250_jpeg_dri(void __iomem *regs, unsigned int dri);
void exyyess3250_jpeg_qtbl(void __iomem *regs, unsigned int t, unsigned int n);
void exyyess3250_jpeg_htbl_ac(void __iomem *regs, unsigned int t);
void exyyess3250_jpeg_htbl_dc(void __iomem *regs, unsigned int t);
void exyyess3250_jpeg_set_y(void __iomem *regs, unsigned int y);
void exyyess3250_jpeg_set_x(void __iomem *regs, unsigned int x);
void exyyess3250_jpeg_interrupts_enable(void __iomem *regs);
void exyyess3250_jpeg_enc_stream_bound(void __iomem *regs, unsigned int size);
void exyyess3250_jpeg_outform_raw(void __iomem *regs, unsigned long format);
void exyyess3250_jpeg_jpgadr(void __iomem *regs, unsigned int addr);
void exyyess3250_jpeg_imgadr(void __iomem *regs, struct s5p_jpeg_addr *img_addr);
void exyyess3250_jpeg_stride(void __iomem *regs, unsigned int img_fmt,
			    unsigned int width);
void exyyess3250_jpeg_offset(void __iomem *regs, unsigned int x_offset,
				unsigned int y_offset);
void exyyess3250_jpeg_coef(void __iomem *base, unsigned int mode);
void exyyess3250_jpeg_start(void __iomem *regs);
void exyyess3250_jpeg_rstart(void __iomem *regs);
unsigned int exyyess3250_jpeg_get_int_status(void __iomem *regs);
void exyyess3250_jpeg_clear_int_status(void __iomem *regs,
						unsigned int value);
unsigned int exyyess3250_jpeg_operating(void __iomem *regs);
unsigned int exyyess3250_jpeg_compressed_size(void __iomem *regs);
void exyyess3250_jpeg_dec_stream_size(void __iomem *regs, unsigned int size);
void exyyess3250_jpeg_dec_scaling_ratio(void __iomem *regs, unsigned int sratio);
void exyyess3250_jpeg_set_timer(void __iomem *regs, unsigned int time_value);
unsigned int exyyess3250_jpeg_get_timer_status(void __iomem *regs);
void exyyess3250_jpeg_set_timer_status(void __iomem *regs);
void exyyess3250_jpeg_clear_timer_status(void __iomem *regs);

#endif /* JPEG_HW_EXYNOS3250_H_ */
