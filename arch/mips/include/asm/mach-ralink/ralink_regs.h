/*
 *  Ralink SoC register definitions
 *
 *  Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#ifndef _RALINK_REGS_H_
#define _RALINK_REGS_H_

extern __iomem void *rt_sysc_membase;
extern __iomem void *rt_memc_membase;

static inline void rt_sysc_w32(u32 val, unsigned reg)
{
	__raw_writel(val, rt_sysc_membase + reg);
}

static inline u32 rt_sysc_r32(unsigned reg)
{
	return __raw_readl(rt_sysc_membase + reg);
}

static inline void rt_sysc_m32(u32 clr, u32 set, unsigned reg)
{
	u32 val = rt_sysc_r32(reg) & ~clr;

	__raw_writel(val | set, rt_sysc_membase + reg);
}

static inline void rt_memc_w32(u32 val, unsigned reg)
{
	__raw_writel(val, rt_memc_membase + reg);
}

static inline u32 rt_memc_r32(unsigned reg)
{
	return __raw_readl(rt_memc_membase + reg);
}

#endif /* _RALINK_REGS_H_ */
