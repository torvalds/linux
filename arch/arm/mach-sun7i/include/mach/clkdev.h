/*
 * arch/arm/mach-sun7i/include/mach/clkdev.h
 * (c) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * James Deng <csjamesdeng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_MACH_CLKDEV_H
#define __ASM_MACH_CLKDEV_H

#include <linux/clk.h>

int __clk_get(struct clk *hclk);
void __clk_put(struct clk *clk);

#endif
