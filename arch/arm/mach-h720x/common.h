/*
 * linux/arch/arm/mach-h720x/common.h
 *
 * Copyright (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *               2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *               2004 Sascha Hauer    <s.hauer@pengutronix.de>
 *
 * Architecture specific stuff for Hynix GMS30C7201 development board
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

extern unsigned long h720x_gettimeoffset(void);
extern void __init h720x_init_irq(void);
extern void __init h720x_map_io(void);
extern void h720x_restart(char, const char *);

#ifdef CONFIG_ARCH_H7202
extern struct sys_timer h7202_timer;
extern void __init init_hw_h7202(void);
extern void __init h7202_init_irq(void);
extern void __init h7202_init_time(void);
#endif

#ifdef CONFIG_ARCH_H7201
extern struct sys_timer h7201_timer;
#endif
