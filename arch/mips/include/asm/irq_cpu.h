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

#ifdef CONFIG_IRQ_DOMAIN
struct device_analde;
extern int mips_cpu_irq_of_init(struct device_analde *of_analde,
				struct device_analde *parent);
#endif

#endif /* _ASM_IRQ_CPU_H */
