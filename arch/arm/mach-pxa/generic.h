/*
 *  linux/arch/arm/mach-pxa/generic.h
 *
 * Author:	Nicolas Pitre
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct sys_timer;

extern struct sys_timer pxa_timer;
extern void __init pxa_init_irq_low(void);
extern void __init pxa_init_irq_high(void);
extern void __init pxa_init_irq_gpio(int gpio_nr);
extern void __init pxa_init_irq_set_wake(int (*set_wake)(unsigned int, unsigned int));
extern void __init pxa_init_gpio(int gpio_nr);
extern void __init pxa25x_init_irq(void);
extern void __init pxa27x_init_irq(void);
extern void __init pxa3xx_init_irq(void);
extern void __init pxa_map_io(void);

extern unsigned int get_clk_frequency_khz(int info);
extern int pxa_last_gpio;

#define SET_BANK(__nr,__start,__size) \
	mi->bank[__nr].start = (__start), \
	mi->bank[__nr].size = (__size), \
	mi->bank[__nr].node = (((unsigned)(__start) - PHYS_OFFSET) >> 27)

#ifdef CONFIG_PXA25x
extern unsigned pxa25x_get_clk_frequency_khz(int);
extern unsigned pxa25x_get_memclk_frequency_10khz(void);
#else
#define pxa25x_get_clk_frequency_khz(x)		(0)
#define pxa25x_get_memclk_frequency_10khz()	(0)
#endif

#ifdef CONFIG_PXA27x
extern unsigned pxa27x_get_clk_frequency_khz(int);
extern unsigned pxa27x_get_memclk_frequency_10khz(void);
#else
#define pxa27x_get_clk_frequency_khz(x)		(0)
#define pxa27x_get_memclk_frequency_10khz()	(0)
#endif

#ifdef CONFIG_PXA3xx
extern unsigned pxa3xx_get_clk_frequency_khz(int);
extern unsigned pxa3xx_get_memclk_frequency_10khz(void);
#else
#define pxa3xx_get_clk_frequency_khz(x)		(0)
#define pxa3xx_get_memclk_frequency_10khz()	(0)
#endif

extern struct sysdev_class pxa_irq_sysclass;
extern struct sysdev_class pxa_gpio_sysclass;
