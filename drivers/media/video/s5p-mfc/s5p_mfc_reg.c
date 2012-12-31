/*
 * linux/drivers/media/video/s5p-mfc/s5p_mfc_reg.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

 #include <linux/io.h>

static void __iomem *regs;

void s5p_mfc_init_reg(void __iomem *base)
{
	regs = base;
}

void s5p_mfc_write_reg(unsigned int data, unsigned int offset)
{
	writel(data, regs + offset);
	/* FIXME: */
	/* __raw_write() */
}

unsigned int s5p_mfc_read_reg(unsigned int offset)
{
	return readl(regs + offset);
	/* FIXME: */
	/* __raw_read() */
}
