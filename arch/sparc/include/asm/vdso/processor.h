/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_SPARC_VDSO_PROCESSOR_H
#define _ASM_SPARC_VDSO_PROCESSOR_H

#include <linux/compiler.h>

#if defined(__arch64__)

/* Please see the commentary in asm/backoff.h for a description of
 * what these instructions are doing and how they have been chosen.
 * To make a long story short, we are trying to yield the current cpu
 * strand during busy loops.
 */
#ifdef	BUILD_VDSO
#define	cpu_relax()	asm volatile("\n99:\n\t"			\
				     "rd	%%ccr, %%g0\n\t"	\
				     "rd	%%ccr, %%g0\n\t"	\
				     "rd	%%ccr, %%g0\n\t"	\
				     ::: "memory")
#else /* ! BUILD_VDSO */
#define cpu_relax()	asm volatile("\n99:\n\t"			\
				     "rd	%%ccr, %%g0\n\t"	\
				     "rd	%%ccr, %%g0\n\t"	\
				     "rd	%%ccr, %%g0\n\t"	\
				     ".section	.pause_3insn_patch,\"ax\"\n\t"\
				     ".word	99b\n\t"		\
				     "wr	%%g0, 128, %%asr27\n\t"	\
				     "nop\n\t"				\
				     "nop\n\t"				\
				     ".previous"			\
				     ::: "memory")
#endif /* BUILD_VDSO */

#else /* ! __arch64__ */

#define cpu_relax()	barrier()

#endif /* __arch64__ */

#endif /* _ASM_SPARC_VDSO_PROCESSOR_H */
