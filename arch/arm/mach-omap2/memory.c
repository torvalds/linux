/*
 * linux/arch/arm/mach-omap2/memory.c
 *
 * Memory timing related functions for OMAP24XX
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <asm/io.h>

#include <asm/arch/clock.h>
#include <asm/arch/sram.h>

#include "prcm-regs.h"
#include "memory.h"

static struct memory_timings mem_timings;

u32 omap2_memory_get_slow_dll_ctrl(void)
{
	return mem_timings.slow_dll_ctrl;
}

u32 omap2_memory_get_fast_dll_ctrl(void)
{
	return mem_timings.fast_dll_ctrl;
}

u32 omap2_memory_get_type(void)
{
	return mem_timings.m_type;
}

void omap2_init_memory_params(u32 force_lock_to_unlock_mode)
{
	unsigned long dll_cnt;
	u32 fast_dll = 0;

	mem_timings.m_type = !((SDRC_MR_0 & 0x3) == 0x1); /* DDR = 1, SDR = 0 */

	/* 2422 es2.05 and beyond has a single SIP DDR instead of 2 like others.
	 * In the case of 2422, its ok to use CS1 instead of CS0.
	 */
	if (cpu_is_omap2422())
		mem_timings.base_cs = 1;
	else
		mem_timings.base_cs = 0;

	if (mem_timings.m_type != M_DDR)
		return;

	/* With DDR we need to determine the low frequency DLL value */
	if (((mem_timings.fast_dll_ctrl & (1 << 2)) == M_LOCK_CTRL))
		mem_timings.dll_mode = M_UNLOCK;
	else
		mem_timings.dll_mode = M_LOCK;

	if (mem_timings.base_cs == 0) {
		fast_dll = SDRC_DLLA_CTRL;
		dll_cnt = SDRC_DLLA_STATUS & 0xff00;
	} else {
		fast_dll = SDRC_DLLB_CTRL;
		dll_cnt = SDRC_DLLB_STATUS & 0xff00;
	}
	if (force_lock_to_unlock_mode) {
		fast_dll &= ~0xff00;
		fast_dll |= dll_cnt;		/* Current lock mode */
	}
	/* set fast timings with DLL filter disabled */
	mem_timings.fast_dll_ctrl = (fast_dll | (3 << 8));

	/* No disruptions, DDR will be offline & C-ABI not followed */
	omap2_sram_ddr_init(&mem_timings.slow_dll_ctrl,
			    mem_timings.fast_dll_ctrl,
			    mem_timings.base_cs,
			    force_lock_to_unlock_mode);
	mem_timings.slow_dll_ctrl &= 0xff00;	/* Keep lock value */

	/* Turn status into unlock ctrl */
	mem_timings.slow_dll_ctrl |=
		((mem_timings.fast_dll_ctrl & 0xF) | (1 << 2));

	/* 90 degree phase for anything below 133Mhz + disable DLL filter */
	mem_timings.slow_dll_ctrl |= ((1 << 1) | (3 << 8));
}
