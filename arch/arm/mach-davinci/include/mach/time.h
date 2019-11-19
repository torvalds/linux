/*
 * Local header file for DaVinci time code.
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ARCH_ARM_MACH_DAVINCI_TIME_H
#define __ARCH_ARM_MACH_DAVINCI_TIME_H

#define DAVINCI_TIMER1_BASE		(IO_PHYS + 0x21800)

enum {
	T0_BOT,
	T0_TOP,
	T1_BOT,
	T1_TOP,
	NUM_TIMERS
};

#define IS_TIMER1(id)		(id & 0x2)
#define IS_TIMER0(id)		(!IS_TIMER1(id))
#define IS_TIMER_TOP(id)	((id & 0x1))
#define IS_TIMER_BOT(id)	(!IS_TIMER_TOP(id))

#define ID_TO_TIMER(id)		(IS_TIMER1(id) != 0)

extern struct davinci_timer_instance davinci_timer_instance[];

#endif /* __ARCH_ARM_MACH_DAVINCI_TIME_H */
