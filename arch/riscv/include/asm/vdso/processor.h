/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_VDSO_PROCESSOR_H
#define __ASM_VDSO_PROCESSOR_H

#ifndef __ASSEMBLY__

#include <asm/barrier.h>

static inline void cpu_relax(void)
{
#ifdef __riscv_muldiv
	int dummy;
	/* In lieu of a halt instruction, induce a long-latency stall. */
	__asm__ __volatile__ ("div %0, %0, zero" : "=r" (dummy));
#endif

#ifdef CONFIG_TOOLCHAIN_HAS_ZIHINTPAUSE
	/*
	 * Reduce instruction retirement.
	 * This assumes the PC changes.
	 */
	__asm__ __volatile__ ("pause");
#else
	/* Encoding of the pause instruction */
	__asm__ __volatile__ (".4byte 0x100000F");
#endif
	barrier();
}

#endif /* __ASSEMBLY__ */

#endif /* __ASM_VDSO_PROCESSOR_H */
