/*
 * include/mach/clkdev.h
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * core header file for Lichee Linux BSP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __SW_CLKDEV_H
#define __SW_CLKDEV_H

struct clk {
        unsigned long           rate;
        const struct clk_ops    *ops;
        const struct icst_params *params;
        void __iomem            *vcoreg;
};

/* FIXME: lock? */
#define __clk_get(clk) ({ 1; })
#define __clk_put(clk) do { } while (0)

#endif


