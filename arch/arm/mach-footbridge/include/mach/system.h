/*
 *  arch/arm/mach-footbridge/include/mach/system.h
 *
 *  Copyright (C) 1996-1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/io.h>
#include <asm/hardware/dec21285.h>
#include <mach/hardware.h>
#include <asm/leds.h>
#include <asm/mach-types.h>

static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void arch_reset(char mode, const char *cmd)
{
	if (mode == 's') {
		/*
		 * Jump into the ROM
		 */
		cpu_reset(0x41000000);
	} else {
		if (machine_is_netwinder()) {
			/* open up the SuperIO chip
			 */
			outb(0x87, 0x370);
			outb(0x87, 0x370);

			/* aux function group 1 (logical device 7)
			 */
			outb(0x07, 0x370);
			outb(0x07, 0x371);

			/* set GP16 for WD-TIMER output
			 */
			outb(0xe6, 0x370);
			outb(0x00, 0x371);

			/* set a RED LED and toggle WD_TIMER for rebooting
			 */
			outb(0xc4, 0x338);
		} else {
			/* 
			 * Force the watchdog to do a CPU reset.
			 *
			 * After making sure that the watchdog is disabled
			 * (so we can change the timer registers) we first
			 * enable the timer to autoreload itself.  Next, the
			 * timer interval is set really short and any
			 * current interrupt request is cleared (so we can
			 * see an edge transition).  Finally, TIMER4 is
			 * enabled as the watchdog.
			 */
			*CSR_SA110_CNTL &= ~(1 << 13);
			*CSR_TIMER4_CNTL = TIMER_CNTL_ENABLE |
					   TIMER_CNTL_AUTORELOAD |
					   TIMER_CNTL_DIV16;
			*CSR_TIMER4_LOAD = 0x2;
			*CSR_TIMER4_CLR  = 0;
			*CSR_SA110_CNTL |= (1 << 13);
		}
	}
}
