/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>

.text

#ifndef CONFIG_X86_CMPXCHG64

/*
 * Emulate 'cmpxchg8b (%esi)' on UP
 *
 * Inputs:
 * %esi : memory location to compare
 * %eax : low 32 bits of old value
 * %edx : high 32 bits of old value
 * %ebx : low 32 bits of new value
 * %ecx : high 32 bits of new value
 */
SYM_FUNC_START(cmpxchg8b_emu)

	pushfl
	cli

	cmpl	0(%esi), %eax
	jne	.Lnot_same
	cmpl	4(%esi), %edx
	jne	.Lnot_same

	movl	%ebx, 0(%esi)
	movl	%ecx, 4(%esi)

	orl	$X86_EFLAGS_ZF, (%esp)

	popfl
	RET

.Lnot_same:
	movl	0(%esi), %eax
	movl	4(%esi), %edx

	andl	$(~X86_EFLAGS_ZF), (%esp)

	popfl
	RET

SYM_FUNC_END(cmpxchg8b_emu)
EXPORT_SYMBOL(cmpxchg8b_emu)

#endif

#ifndef CONFIG_UML

SYM_FUNC_START(this_cpu_cmpxchg8b_emu)

	pushfl
	cli

	cmpl	PER_CPU_VAR(0(%esi)), %eax
	jne	.Lnot_same2
	cmpl	PER_CPU_VAR(4(%esi)), %edx
	jne	.Lnot_same2

	movl	%ebx, PER_CPU_VAR(0(%esi))
	movl	%ecx, PER_CPU_VAR(4(%esi))

	orl	$X86_EFLAGS_ZF, (%esp)

	popfl
	RET

.Lnot_same2:
	movl	PER_CPU_VAR(0(%esi)), %eax
	movl	PER_CPU_VAR(4(%esi)), %edx

	andl	$(~X86_EFLAGS_ZF), (%esp)

	popfl
	RET

SYM_FUNC_END(this_cpu_cmpxchg8b_emu)

#endif
