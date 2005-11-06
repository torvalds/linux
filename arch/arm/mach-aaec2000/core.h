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

#include <asm/hardware/amba_clcd.h>

struct sys_timer;

extern struct sys_timer aaec2000_timer;
extern void __init aaec2000_map_io(void);
extern void __init aaec2000_init_irq(void);

struct aaec2000_clcd_info {
	struct clcd_panel panel;
	void (*disable)(struct clcd_fb *);
	void (*enable)(struct clcd_fb *);
};

extern void __init aaec2000_set_clcd_plat_data(struct aaec2000_clcd_info *);

