/*
 * arch/arm/mach-ns9xxx/generic.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/time.h>
#include <asm/mach/time.h>
#include <linux/init.h>

void __init ns9xxx_init_irq(void);
void __init ns9xxx_map_io(void);
void __init ns9xxx_init_machine(void);

extern struct sys_timer ns9xxx_timer;
