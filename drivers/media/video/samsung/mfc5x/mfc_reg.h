/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_regs.h
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

#ifndef __MFC_REGS_H
#define __MFC_REGS_H __FILE__

void init_reg(void __iomem *base);
void write_reg(unsigned int data, unsigned int offset);
unsigned int read_reg(unsigned int offset);

#endif /* __MFC_REGS_H */
