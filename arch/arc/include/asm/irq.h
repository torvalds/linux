/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARC_IRQ_H
#define __ASM_ARC_IRQ_H

#define NR_CPU_IRQS	32  /* number of interrupt lines of ARC770 CPU */
#define NR_IRQS		128 /* allow some CPU external IRQ handling */

/* Platform Independent IRQs */
#ifdef CONFIG_ISA_ARCOMPACT
#define TIMER0_IRQ      3
#define TIMER1_IRQ      4
#define IPI_IRQ		(NR_CPU_IRQS-1) /* dummy to enable SMP build for up hardware */
#else
#define TIMER0_IRQ      16
#define TIMER1_IRQ      17
#define IPI_IRQ         19
#endif

#include <linux/interrupt.h>
#include <asm-generic/irq.h>

extern void arc_init_IRQ(void);
void arc_local_timer_setup(void);
void arc_request_percpu_irq(int irq, int cpu,
                            irqreturn_t (*isr)(int irq, void *dev),
                            const char *irq_nm, void *percpu_dev);

#endif
