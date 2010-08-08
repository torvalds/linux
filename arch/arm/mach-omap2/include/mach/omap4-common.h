/*
 * omap4-common.h: OMAP4 specific common header file
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 *
 * Author:
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef OMAP_ARCH_OMAP4_COMMON_H
#define OMAP_ARCH_OMAP4_COMMON_H

/*
 * wfi used in low power code. Directly opcode is used instead
 * of instruction to avoid mulit-omap build break
 */
#define do_wfi()			\
		__asm__ __volatile__ (".word	0xe320f003" : : : "memory")

#ifdef CONFIG_CACHE_L2X0
extern void __iomem *l2cache_base;
#endif

extern void __iomem *gic_cpu_base_addr;
extern void __iomem *gic_dist_base_addr;

extern void __init gic_init_irq(void);
extern void omap_smc1(u32 fn, u32 arg);

#endif
