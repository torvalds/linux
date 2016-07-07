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
#ifdef CONFIG_ISA_ARCV2
#define IPI_IRQ		19
#define SOFTIRQ_IRQ	21
#endif

#include <linux/interrupt.h>
#include <asm-generic/irq.h>

extern void arc_init_IRQ(void);

#endif
