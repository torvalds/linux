/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_IRQ_H
#define _ASM_MICROBLAZE_IRQ_H

#define NR_IRQS		(32 + 1)
#include <asm-generic/irq.h>

struct pt_regs;
extern void do_IRQ(struct pt_regs *regs);

/* should be defined in each interrupt controller driver */
extern unsigned int xintc_get_irq(void);

#endif /* _ASM_MICROBLAZE_IRQ_H */
