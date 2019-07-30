/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */


#ifndef _ASM_RISCV_IRQFLAGS_H
#define _ASM_RISCV_IRQFLAGS_H

#include <asm/processor.h>
#include <asm/csr.h>

/* read interrupt enabled status */
static inline unsigned long arch_local_save_flags(void)
{
	return csr_read(CSR_SSTATUS);
}

/* unconditionally enable interrupts */
static inline void arch_local_irq_enable(void)
{
	csr_set(CSR_SSTATUS, SR_SIE);
}

/* unconditionally disable interrupts */
static inline void arch_local_irq_disable(void)
{
	csr_clear(CSR_SSTATUS, SR_SIE);
}

/* get status and disable interrupts */
static inline unsigned long arch_local_irq_save(void)
{
	return csr_read_clear(CSR_SSTATUS, SR_SIE);
}

/* test flags */
static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & SR_SIE);
}

/* test hardware interrupt enable bit */
static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

/* set interrupt enabled status */
static inline void arch_local_irq_restore(unsigned long flags)
{
	csr_set(CSR_SSTATUS, flags & SR_SIE);
}

#endif /* _ASM_RISCV_IRQFLAGS_H */
