/*
 * DaVinci timer defines
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_ARCH_TIMEX_H
#define __ASM_ARCH_TIMEX_H

/*
 * Alert: Not all timers of the DaVinci family run at a frequency of 27MHz,
 * but we should be fine as long as CLOCK_TICK_RATE or LATCH (see include/
 * linux/jiffies.h) are not used directly in code. Currently none of the
 * code relevant to DaVinci platform depends on these values directly.
 */
#define CLOCK_TICK_RATE 27000000

#endif /* __ASM_ARCH_TIMEX_H__ */
