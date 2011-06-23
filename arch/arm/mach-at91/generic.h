/*
 * linux/arch/arm/mach-at91/generic.h
 *
 *  Copyright (C) 2005 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clkdev.h>

 /* Map io */
extern void __init at91rm9200_map_io(void);
extern void __init at91sam9260_map_io(void);
extern void __init at91sam9261_map_io(void);
extern void __init at91sam9263_map_io(void);
extern void __init at91sam9rl_map_io(void);
extern void __init at91sam9g45_map_io(void);
extern void __init at91x40_map_io(void);
extern void __init at91cap9_map_io(void);

 /* Processors */
extern void __init at91rm9200_set_type(int type);
extern void __init at91rm9200_initialize(unsigned long main_clock);
extern void __init at91sam9260_initialize(unsigned long main_clock);
extern void __init at91sam9261_initialize(unsigned long main_clock);
extern void __init at91sam9263_initialize(unsigned long main_clock);
extern void __init at91sam9rl_initialize(unsigned long main_clock);
extern void __init at91sam9g45_initialize(unsigned long main_clock);
extern void __init at91x40_initialize(unsigned long main_clock);
extern void __init at91cap9_initialize(unsigned long main_clock);

 /* Interrupts */
extern void __init at91rm9200_init_interrupts(unsigned int priority[]);
extern void __init at91sam9260_init_interrupts(unsigned int priority[]);
extern void __init at91sam9261_init_interrupts(unsigned int priority[]);
extern void __init at91sam9263_init_interrupts(unsigned int priority[]);
extern void __init at91sam9rl_init_interrupts(unsigned int priority[]);
extern void __init at91sam9g45_init_interrupts(unsigned int priority[]);
extern void __init at91x40_init_interrupts(unsigned int priority[]);
extern void __init at91cap9_init_interrupts(unsigned int priority[]);
extern void __init at91_aic_init(unsigned int priority[]);

 /* Timer */
struct sys_timer;
extern struct sys_timer at91rm9200_timer;
extern struct sys_timer at91sam926x_timer;
extern struct sys_timer at91x40_timer;

 /* Clocks */
extern int __init at91_clock_init(unsigned long main_clock);
/*
 * function to specify the clock of the default console. As we do not
 * use the device/driver bus, the dev_name is not intialize. So we need
 * to link the clock to a specific con_id only "usart"
 */
extern void __init at91rm9200_set_console_clock(int id);
extern void __init at91sam9260_set_console_clock(int id);
extern void __init at91sam9261_set_console_clock(int id);
extern void __init at91sam9263_set_console_clock(int id);
extern void __init at91sam9rl_set_console_clock(int id);
extern void __init at91sam9g45_set_console_clock(int id);
extern void __init at91cap9_set_console_clock(int id);
struct device;

 /* Power Management */
extern void at91_irq_suspend(void);
extern void at91_irq_resume(void);

/* reset */
extern void at91sam9_alt_reset(void);

 /* GPIO */
#define AT91RM9200_PQFP		3	/* AT91RM9200 PQFP package has 3 banks */
#define AT91RM9200_BGA		4	/* AT91RM9200 BGA package has 4 banks */

struct at91_gpio_bank {
	unsigned short id;		/* peripheral ID */
	unsigned long offset;		/* offset from system peripheral base */
	struct clk *clock;		/* associated clock */
};
extern void __init at91_gpio_init(struct at91_gpio_bank *, int nr_banks);
extern void __init at91_gpio_irq_setup(void);

extern void (*at91_arch_reset)(void);
extern int at91_extern_irq;
