/*
 *  linux/arch/arm/mach-aaec2000/core.h
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

struct sys_timer;

extern struct sys_timer aaec2000_timer;
extern void __init aaec2000_map_io(void);
extern void __init aaec2000_init_irq(void);
