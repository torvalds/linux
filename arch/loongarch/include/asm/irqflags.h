/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#ifndef __ASSEMBLER__

#include <linux/compiler.h>
#include <linux/stringify.h>
#include <asm/loongarch.h>

static inline void arch_local_irq_enable(void)
{
	u32 flags = CSR_CRMD_IE;
	register u32 mask asm("t0") = CSR_CRMD_IE;

	__asm__ __volatile__(
		"csrxchg %[val], %[mask], %[reg]\n\t"
		: [val] "+r" (flags)
		: [mask] "r" (mask), [reg] "i" (LOONGARCH_CSR_CRMD)
		: "memory");
}

static inline void arch_local_irq_disable(void)
{
	u32 flags = 0;
	register u32 mask asm("t0") = CSR_CRMD_IE;

	__asm__ __volatile__(
		"csrxchg %[val], %[mask], %[reg]\n\t"
		: [val] "+r" (flags)
		: [mask] "r" (mask), [reg] "i" (LOONGARCH_CSR_CRMD)
		: "memory");
}

static inline unsigned long arch_local_irq_save(void)
{
	u32 flags = 0;
	register u32 mask asm("t0") = CSR_CRMD_IE;

	__asm__ __volatile__(
		"csrxchg %[val], %[mask], %[reg]\n\t"
		: [val] "+r" (flags)
		: [mask] "r" (mask), [reg] "i" (LOONGARCH_CSR_CRMD)
		: "memory");
	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	register u32 mask asm("t0") = CSR_CRMD_IE;

	__asm__ __volatile__(
		"csrxchg %[val], %[mask], %[reg]\n\t"
		: [val] "+r" (flags)
		: [mask] "r" (mask), [reg] "i" (LOONGARCH_CSR_CRMD)
		: "memory");
}

static inline unsigned long arch_local_save_flags(void)
{
	u32 flags;
	__asm__ __volatile__(
		"csrrd %[val], %[reg]\n\t"
		: [val] "=r" (flags)
		: [reg] "i" (LOONGARCH_CSR_CRMD)
		: "memory");
	return flags;
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & CSR_CRMD_IE);
}

static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#endif /* #ifndef __ASSEMBLER__ */

#endif /* _ASM_IRQFLAGS_H */
