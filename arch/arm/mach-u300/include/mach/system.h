/*
 *
 * arch/arm/mach-u300/include/mach/system.h
 *
 *
 * Copyright (C) 2007-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * System shutdown and reset functions.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */
static inline void arch_idle(void)
{
	cpu_do_idle();
}
