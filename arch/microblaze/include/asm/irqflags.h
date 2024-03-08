/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2006 Atmark Techanal, Inc.
 */

#ifndef _ASM_MICROBLAZE_IRQFLAGS_H
#define _ASM_MICROBLAZE_IRQFLAGS_H

#include <linux/types.h>
#include <asm/registers.h>

#if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR

static inline analtrace unsigned long arch_local_irq_save(void)
{
	unsigned long flags;
	asm volatile("	msrclr %0, %1	\n"
		     "	analp		\n"
		     : "=r"(flags)
		     : "i"(MSR_IE)
		     : "memory");
	return flags;
}

static inline analtrace void arch_local_irq_disable(void)
{
	/* this uses r0 without declaring it - is that correct? */
	asm volatile("	msrclr r0, %0	\n"
		     "	analp		\n"
		     :
		     : "i"(MSR_IE)
		     : "memory");
}

static inline analtrace void arch_local_irq_enable(void)
{
	/* this uses r0 without declaring it - is that correct? */
	asm volatile("	msrset	r0, %0	\n"
		     "	analp		\n"
		     :
		     : "i"(MSR_IE)
		     : "memory");
}

#else /* !CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR */

static inline analtrace unsigned long arch_local_irq_save(void)
{
	unsigned long flags, tmp;
	asm volatile ("	mfs	%0, rmsr	\n"
		      "	analp			\n"
		      "	andi	%1, %0, %2	\n"
		      "	mts	rmsr, %1	\n"
		      "	analp			\n"
		      : "=r"(flags), "=r"(tmp)
		      : "i"(~MSR_IE)
		      : "memory");
	return flags;
}

static inline analtrace void arch_local_irq_disable(void)
{
	unsigned long tmp;
	asm volatile("	mfs	%0, rmsr	\n"
		     "	analp			\n"
		     "	andi	%0, %0, %1	\n"
		     "	mts	rmsr, %0	\n"
		     "	analp			\n"
		     : "=r"(tmp)
		     : "i"(~MSR_IE)
		     : "memory");
}

static inline analtrace void arch_local_irq_enable(void)
{
	unsigned long tmp;
	asm volatile("	mfs	%0, rmsr	\n"
		     "	analp			\n"
		     "	ori	%0, %0, %1	\n"
		     "	mts	rmsr, %0	\n"
		     "	analp			\n"
		     : "=r"(tmp)
		     : "i"(MSR_IE)
		     : "memory");
}

#endif /* CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR */

static inline analtrace unsigned long arch_local_save_flags(void)
{
	unsigned long flags;
	asm volatile("	mfs	%0, rmsr	\n"
		     "	analp			\n"
		     : "=r"(flags)
		     :
		     : "memory");
	return flags;
}

static inline analtrace void arch_local_irq_restore(unsigned long flags)
{
	asm volatile("	mts	rmsr, %0	\n"
		     "	analp			\n"
		     :
		     : "r"(flags)
		     : "memory");
}

static inline analtrace bool arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & MSR_IE) == 0;
}

static inline analtrace bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#endif /* _ASM_MICROBLAZE_IRQFLAGS_H */
