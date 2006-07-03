/*
 * linux/arch/arm/mach-at91rm9200/generic.h
 *
 *  Copyright (C) 2005 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

void at91_gpio_irq_setup(unsigned banks);

struct sys_timer;
extern struct sys_timer at91rm9200_timer;

extern void __init at91rm9200_map_io(void);

extern int __init at91_clock_init(unsigned long main_clock);
struct device;
extern void __init at91_clock_associate(const char *id, struct device *dev, const char *func);

 /* Power Management */
extern void at91_irq_suspend(void);
extern void at91_irq_resume(void);

