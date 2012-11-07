/*
 * arch/arm/include/asm/mach/time.h
 *
 * Copyright (C) 2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_MACH_TIME_H
#define __ASM_ARM_MACH_TIME_H

/*
 * This is our kernel timer structure.
 *
 * - init
 *   Initialise the kernels jiffy timer source, claim interrupt
 *   using setup_irq.  This is called early on during initialisation
 *   while interrupts are still disabled on the local CPU.
 * - offset
 *   Return the timer offset in microseconds since the last timer
 *   interrupt.  Note: this must take account of any unprocessed
 *   timer interrupt which may be pending.
 */
struct sys_timer {
	void			(*init)(void);
};

extern void timer_tick(void);

struct timespec;
typedef void (*clock_access_fn)(struct timespec *);
extern int register_persistent_clock(clock_access_fn read_boot,
				     clock_access_fn read_persistent);

#endif
