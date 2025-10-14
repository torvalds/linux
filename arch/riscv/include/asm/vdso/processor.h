/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_VDSO_PROCESSOR_H
#define __ASM_VDSO_PROCESSOR_H

#ifndef __ASSEMBLER__

#include <asm/barrier.h>
#include <asm/errata_list.h>
#include <asm/insn-def.h>

static inline void cpu_relax(void)
{
#ifdef __riscv_muldiv
	int dummy;
	/* In lieu of a halt instruction, induce a long-latency stall. */
	__asm__ __volatile__ ("div %0, %0, zero" : "=r" (dummy));
#endif

	/*
	 * Reduce instruction retirement.
	 * This assumes the PC changes.
	 */
	ALT_RISCV_PAUSE();
	barrier();
}

#endif /* __ASSEMBLER__ */

#endif /* __ASM_VDSO_PROCESSOR_H */
