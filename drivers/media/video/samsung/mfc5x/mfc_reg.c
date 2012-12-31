/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_regs.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Register interface for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>

static void __iomem *regs;

void init_reg(void __iomem *base)
{
	regs = base;
}

void write_reg(unsigned int data, unsigned int offset)
{
	writel(data, regs + offset);
}

unsigned int read_reg(unsigned int offset)
{
	return readl(regs + offset);
}

