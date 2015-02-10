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

#include <linux/reboot.h>

struct irq_data;

extern unsigned int get_clk_frequency_khz(int info);
extern void __init pxa_dt_irq_init(int (*fn)(struct irq_data *,
					     unsigned int));
extern void __init pxa_map_io(void);
extern void pxa_timer_init(void);

#define SET_BANK(__nr,__start,__size) \
	mi->bank[__nr].start = (__start), \
	mi->bank[__nr].size = (__size)

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

#define pxa25x_handle_irq icip_handle_irq
extern void __init pxa25x_init_irq(void);
extern void __init pxa25x_map_io(void);
extern void __init pxa26x_init_irq(void);

#define pxa27x_handle_irq ichp_handle_irq
extern void __init pxa27x_dt_init_irq(void);
extern unsigned	pxa27x_get_clk_frequency_khz(int);
extern void __init pxa27x_init_irq(void);
extern void __init pxa27x_map_io(void);

#define pxa3xx_handle_irq ichp_handle_irq
extern void __init pxa3xx_dt_init_irq(void);
extern void __init pxa3xx_init_irq(void);
extern void __init pxa3xx_map_io(void);

extern struct syscore_ops pxa_irq_syscore_ops;
extern struct syscore_ops pxa2xx_mfp_syscore_ops;
extern struct syscore_ops pxa3xx_mfp_syscore_ops;

void __init pxa_set_ffuart_info(void *info);
void __init pxa_set_btuart_info(void *info);
void __init pxa_set_stuart_info(void *info);
void __init pxa_set_hwuart_info(void *info);

void pxa_restart(enum reboot_mode, const char *);

#if defined(CONFIG_PXA25x) || defined(CONFIG_PXA27x)
extern void pxa2xx_clear_reset_status(unsigned int);
#else
static inline void pxa2xx_clear_reset_status(unsigned int mask) {}
#endif

/*
 * Once fully converted to the clock framework, all these functions should be
 * removed, and replaced with a clk_get(NULL, "core").
 */
#ifdef CONFIG_PXA25x
extern unsigned pxa25x_get_clk_frequency_khz(int);
#else
#define pxa25x_get_clk_frequency_khz(x)		(0)
#endif

#ifdef CONFIG_PXA27x
#else
#define pxa27x_get_clk_frequency_khz(x)		(0)
#endif

#ifdef CONFIG_PXA3xx
extern unsigned	pxa3xx_get_clk_frequency_khz(int);
#else
#define pxa3xx_get_clk_frequency_khz(x)		(0)
#endif
