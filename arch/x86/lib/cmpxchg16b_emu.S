/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/linkage.h>
#include <asm/percpu.h>

.text

/*
 * Inputs:
 * %rsi : memory location to compare
 * %rax : low 64 bits of old value
 * %rdx : high 64 bits of old value
 * %rbx : low 64 bits of new value
 * %rcx : high 64 bits of new value
 * %al  : Operation successful
 */
SYM_FUNC_START(this_cpu_cmpxchg16b_emu)

#
# Emulate 'cmpxchg16b %gs:(%rsi)' except we return the result in %al not
# via the ZF.  Caller will access %al to get result.
#
# Note that this is only useful for a cpuops operation.  Meaning that we
# do *not* have a fully atomic operation but just an operation that is
# *atomic* on a single cpu (as provided by the this_cpu_xx class of
# macros).
#
	pushfq
	cli

	cmpq PER_CPU_VAR((%rsi)), %rax
	jne .Lnot_same
	cmpq PER_CPU_VAR(8(%rsi)), %rdx
	jne .Lnot_same

	movq %rbx, PER_CPU_VAR((%rsi))
	movq %rcx, PER_CPU_VAR(8(%rsi))

	popfq
	mov $1, %al
	ret

.Lnot_same:
	popfq
	xor %al,%al
	ret

SYM_FUNC_END(this_cpu_cmpxchg16b_emu)
