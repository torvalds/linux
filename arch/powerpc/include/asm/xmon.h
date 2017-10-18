#ifndef __ASM_POWERPC_XMON_H
#define __ASM_POWERPC_XMON_H

/*
 * Copyrignt (C) 2006 IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifdef __KERNEL__

#include <linux/irqreturn.h>

#ifdef CONFIG_XMON
extern void xmon_setup(void);
extern void xmon_register_spus(struct list_head *list);
struct pt_regs;
extern int xmon(struct pt_regs *excp);
extern irqreturn_t xmon_irq(int, void *);
#else
static inline void xmon_setup(void) { };
static inline void xmon_register_spus(struct list_head *list) { };
#endif

#if defined(CONFIG_XMON) && defined(CONFIG_SMP)
extern int cpus_are_in_xmon(void);
#endif

extern void xmon_printf(const char *format, ...);

#endif /* __KERNEL __ */
#endif /* __ASM_POWERPC_XMON_H */
