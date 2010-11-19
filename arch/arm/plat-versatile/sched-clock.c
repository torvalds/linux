/*
 *  linux/arch/arm/plat-versatile/sched-clock.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/cnt32_to_63.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <asm/div64.h>

#include <mach/hardware.h>
#include <mach/platform.h>

#ifdef VERSATILE_SYS_BASE
#define REFCOUNTER	(__io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_24MHz_OFFSET)
#endif

#ifdef REALVIEW_SYS_BASE
#define REFCOUNTER	(__io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_24MHz_OFFSET)
#endif

/*
 * This is the Realview and Versatile sched_clock implementation.  This
 * has a resolution of 41.7ns, and a maximum value of about 35583 days.
 *
 * The return value is guaranteed to be monotonic in that range as
 * long as there is always less than 89 seconds between successive
 * calls to this function.
 */
unsigned long long sched_clock(void)
{
	unsigned long long v = cnt32_to_63(readl(REFCOUNTER));

	/* the <<1 gets rid of the cnt_32_to_63 top bit saving on a bic insn */
	v *= 125<<1;
	do_div(v, 3<<1);

	return v;
}
