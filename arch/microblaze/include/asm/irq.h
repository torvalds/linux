/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_IRQ_H
#define _ASM_MICROBLAZE_IRQ_H

#include <asm-generic/irq.h>

struct pt_regs;
extern void do_IRQ(struct pt_regs *regs);

/* should be defined in each interrupt controller driver */
extern unsigned int xintc_get_irq(void);

#endif /* _ASM_MICROBLAZE_IRQ_H */
