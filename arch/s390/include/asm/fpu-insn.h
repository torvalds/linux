/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Floating Point and Vector Instructions
 *
 */

#ifndef __ASM_S390_FPU_INSN_H
#define __ASM_S390_FPU_INSN_H

#include <asm/fpu-insn-asm.h>

#ifndef __ASSEMBLY__

#include <linux/instrumented.h>
#include <asm/asm-extable.h>

asm(".include \"asm/fpu-insn-asm.h\"\n");

/*
 * Various small helper functions, which can and should be used within
 * kernel fpu code sections. Each function represents only one floating
 * point or vector instruction (except for helper functions which require
 * exception handling).
 *
 * This allows to use floating point and vector instructions like C
 * functions, which has the advantage that all supporting code, like
 * e.g. loops, can be written in easy to read C code.
 *
 * Each of the helper functions provides support for code instrumentation,
 * like e.g. KASAN. Therefore instrumentation is also covered automatically
 * when using these functions.
 *
 * In order to ensure that code generated with the helper functions stays
 * within kernel fpu sections, which are guarded with kernel_fpu_begin()
 * and kernel_fpu_end() calls, each function has a mandatory "memory"
 * barrier.
 */

/**
 * fpu_lfpc_safe - Load floating point control register safely.
 * @fpc: new value for floating point control register
 *
 * Load floating point control register. This may lead to an exception,
 * since a saved value may have been modified by user space (ptrace,
 * signal return, kvm registers) to an invalid value. In such a case
 * set the floating point control register to zero.
 */
static inline void fpu_lfpc_safe(unsigned int *fpc)
{
	u32 tmp;

	instrument_read(fpc, sizeof(*fpc));
	asm volatile("\n"
		"0:	lfpc	%[fpc]\n"
		"1:	nopr	%%r7\n"
		".pushsection .fixup, \"ax\"\n"
		"2:	lghi	%[tmp],0\n"
		"	sfpc	%[tmp]\n"
		"	jg	1b\n"
		".popsection\n"
		EX_TABLE(1b, 2b)
		: [tmp] "=d" (tmp)
		: [fpc] "Q" (*fpc)
		: "memory");
}

#endif /* __ASSEMBLY__ */
#endif	/* __ASM_S390_FPU_INSN_H */
