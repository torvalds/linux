/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/linkage.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>

.text

/*
 * Emulate 'cmpxchg16b %gs:(%rsi)'
 *
 * Inputs:
 * %rsi : memory location to compare
 * %rax : low 64 bits of old value
 * %rdx : high 64 bits of old value
 * %rbx : low 64 bits of new value
 * %rcx : high 64 bits of new value
 *
 * Notably this is not LOCK prefixed and is not safe against NMIs
 */
SYM_FUNC_START(this_cpu_cmpxchg16b_emu)

	pushfq
	cli

	/* if (*ptr == old) */
	cmpq	PER_CPU_VAR(0(%rsi)), %rax
	jne	.Lnot_same
	cmpq	PER_CPU_VAR(8(%rsi)), %rdx
	jne	.Lnot_same

	/* *ptr = new */
	movq	%rbx, PER_CPU_VAR(0(%rsi))
	movq	%rcx, PER_CPU_VAR(8(%rsi))

	/* set ZF in EFLAGS to indicate success */
	orl	$X86_EFLAGS_ZF, (%rsp)

	popfq
	RET

.Lnot_same:
	/* *ptr != old */

	/* old = *ptr */
	movq	PER_CPU_VAR(0(%rsi)), %rax
	movq	PER_CPU_VAR(8(%rsi)), %rdx

	/* clear ZF in EFLAGS to indicate failure */
	andl	$(~X86_EFLAGS_ZF), (%rsp)

	popfq
	RET

SYM_FUNC_END(this_cpu_cmpxchg16b_emu)
