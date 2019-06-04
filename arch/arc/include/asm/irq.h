/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __ASM_ARC_IRQ_H
#define __ASM_ARC_IRQ_H

/*
 * ARCv2 can support 240 interrupts in the core interrupts controllers and
 * 128 interrupts in IDU. Thus 512 virtual IRQs must be enough for most
 * configurations of boards.
 * This doesnt affect ARCompact, but we change it to same value
 */
#define NR_IRQS		512

/* Platform Independent IRQs */
#ifdef CONFIG_ISA_ARCV2
#define IPI_IRQ		19
#define SOFTIRQ_IRQ	21
#define FIRST_EXT_IRQ	24
#endif

#include <linux/interrupt.h>
#include <asm-generic/irq.h>

extern void arc_init_IRQ(void);

#endif
