/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	include/asm-mips/irq_cpu.h
 *
 *	MIPS CPU interrupt definitions.
 *
 *	Copyright (C) 2002  Maciej W. Rozycki
 */
#ifndef _ASM_IRQ_CPU_H
#define _ASM_IRQ_CPU_H

extern void mips_cpu_irq_init(void);
extern void rm7k_cpu_irq_init(void);
extern void rm9k_cpu_irq_init(void);

#ifdef CONFIG_IRQ_DOMAIN
struct device_node;
extern int mips_cpu_irq_of_init(struct device_node *of_node,
				struct device_node *parent);
#endif

#endif /* _ASM_IRQ_CPU_H */
