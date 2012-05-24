/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2006, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  Large parts taken directly from powerpc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_IRQ_H
#define _ASM_C6X_IRQ_H

#include <linux/irqdomain.h>
#include <linux/threads.h>
#include <linux/list.h>
#include <linux/radix-tree.h>
#include <asm/percpu.h>

#define irq_canonicalize(irq)  (irq)

/*
 * The C64X+ core has 16 IRQ vectors. One each is used by Reset and NMI. Two
 * are reserved. The remaining 12 vectors are used to route SoC interrupts.
 * These interrupt vectors are prioritized with IRQ 4 having the highest
 * priority and IRQ 15 having the lowest.
 *
 * The C64x+ megamodule provides a PIC which combines SoC IRQ sources into a
 * single core IRQ vector. There are four combined sources, each of which
 * feed into one of the 12 general interrupt vectors. The remaining 8 vectors
 * can each route a single SoC interrupt directly.
 */
#define NR_PRIORITY_IRQS 16

#define NR_IRQS_LEGACY	NR_PRIORITY_IRQS

/* Total number of virq in the platform */
#define NR_IRQS		256

/* This number is used when no interrupt has been assigned */
#define NO_IRQ		0

extern void __init init_pic_c64xplus(void);

extern void init_IRQ(void);

struct pt_regs;

extern asmlinkage void c6x_do_IRQ(unsigned int prio, struct pt_regs *regs);

extern unsigned long irq_err_count;

#endif /* _ASM_C6X_IRQ_H */
