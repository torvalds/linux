/*
 * linux/arch/arm/mach-omap2/memory.h
 *
 * Interface for memory timing related functions for OMAP24XX
 *
 * Copyright (C) 2005 Texas Instruments Inc.
 * Richard Woodruff <r-woodruff2@ti.com>
 *
 * Copyright (C) 2005 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARCH_ARM_MACH_OMAP2_MEMORY_H
#define ARCH_ARM_MACH_OMAP2_MEMORY_H

/* Memory timings */
#define M_DDR		1
#define M_LOCK_CTRL	(1 << 2)
#define M_UNLOCK	0
#define M_LOCK		1

struct memory_timings {
	u32 m_type;		/* ddr = 1, sdr = 0 */
	u32 dll_mode;		/* use lock mode = 1, unlock mode = 0 */
	u32 slow_dll_ctrl;	/* unlock mode, dll value for slow speed */
	u32 fast_dll_ctrl;	/* unlock mode, dll value for fast speed */
	u32 base_cs;		/* base chip select to use for calculations */
};

extern void omap2_init_memory_params(u32 force_lock_to_unlock_mode);
extern u32 omap2_memory_get_slow_dll_ctrl(void);
extern u32 omap2_memory_get_fast_dll_ctrl(void);
extern u32 omap2_memory_get_type(void);
u32 omap2_dll_force_needed(void);
u32 omap2_reprogram_sdrc(u32 level, u32 force);
void __init omap2_init_memory(void);
void __init gpmc_init(void);

#endif
