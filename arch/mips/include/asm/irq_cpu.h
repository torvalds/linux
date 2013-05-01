/*
 *	include/asm-mips/irq_cpu.h
 *
 *	MIPS CPU interrupt definitions.
 *
 *	Copyright (C) 2002  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_IRQ_CPU_H
#define _ASM_IRQ_CPU_H

extern void mips_cpu_irq_init(void);
extern void rm7k_cpu_irq_init(void);
extern void rm9k_cpu_irq_init(void);

#ifdef CONFIG_IRQ_DOMAIN
struct device_node;
extern int mips_cpu_intc_init(struct device_node *of_node,
			      struct device_node *parent);
#endif

#endif /* _ASM_IRQ_CPU_H */
