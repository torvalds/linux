/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Floating Point and Vector Instructions
 *
 */

#ifndef __ASM_S390_FPU_INSN_H
#define __ASM_S390_FPU_INSN_H

#include <asm/fpu-insn-asm.h>

#ifndef __ASSEMBLY__

#include <asm/asm-extable.h>

asm(".include \"asm/fpu-insn-asm.h\"\n");

/**
 * sfpc_safe - Set floating point control register safely.
 * @fpc: new value for floating point control register
 *
 * Set floating point control register. This may lead to an exception,
 * since a saved value may have been modified by user space (ptrace,
 * signal return, kvm registers) to an invalid value. In such a case
 * set the floating point control register to zero.
 */
static inline void sfpc_safe(u32 fpc)
{
	asm volatile("\n"
		"0:	sfpc	%[fpc]\n"
		"1:	nopr	%%r7\n"
		".pushsection .fixup, \"ax\"\n"
		"2:	lghi	%[fpc],0\n"
		"	jg	0b\n"
		".popsection\n"
		EX_TABLE(1b, 2b)
		: [fpc] "+d" (fpc)
		: : "memory");
}

#endif /* __ASSEMBLY__ */
#endif	/* __ASM_S390_FPU_INSN_H */
