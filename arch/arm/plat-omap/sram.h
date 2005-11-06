/*
 * linux/arch/arm/plat-omap/sram.h
 *
 * Interface for functions that need to be run in internal SRAM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_OMAP_SRAM_H
#define __ARCH_ARM_OMAP_SRAM_H

extern void * omap_sram_push(void * start, unsigned long size);
extern void omap_sram_reprogram_clock(u32 dpllctl, u32 ckctl);

/* Do not use these */
extern void sram_reprogram_clock(u32 ckctl, u32 dpllctl);
extern unsigned long sram_reprogram_clock_sz;

#endif
