/*
 * Copyright (C) 2014 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_ROCKCHIP_SRAM_DELAY_H
#define __MACH_ROCKCHIP_SRAM_DELAY_H

#include <asm/arch_timer.h>

/*
 * Arch timer freq is present in dts.  We could get it from there but
 * really nobody is going to put anything but 24MHz here.  If they do then
 * we'll have to add this to the suspend data.
 */
#define ARCH_TIMER_FREQ			24000000
#define ARCH_TIMER_TICKS_PER_US		(ARCH_TIMER_FREQ / 1000000)

static inline void sram_udelay(u32 us)
{
	u32 orig;
	u32 to_wait = ARCH_TIMER_TICKS_PER_US * us;

	/* Note: u32 math is way more than enough for our small delays */
	orig = (u32) arch_counter_get_cntpct();
	while ((u32) arch_counter_get_cntpct() - orig <= to_wait)
		;
}

#endif /* MACH_ROCKCHIP_SRAM_DELAY_H */
