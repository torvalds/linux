/*
 *
 * arch/arm/mach-u300/include/mach/platform.h
 *
 *
 * Copyright (C) 2006-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Basic platform init and mapping functions.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */

#ifndef __ASSEMBLY__

void u300_map_io(void);
void u300_init_irq(void);
void u300_init_devices(void);
void u300_restart(char, const char *);
extern struct sys_timer u300_timer;

#endif
