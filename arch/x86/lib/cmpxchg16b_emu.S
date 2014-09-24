/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; version 2
 *	of the License.
 *
 */
#include <linux/linkage.h>
#include <asm/dwarf2.h>
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
ENTRY(this_cpu_cmpxchg16b_emu)
CFI_STARTPROC

#
# Emulate 'cmpxchg16b %gs:(%rsi)' except we return the result in %al not
# via the ZF.  Caller will access %al to get result.
#
# Note that this is only useful for a cpuops operation.  Meaning that we
# do *not* have a fully atomic operation but just an operation that is
# *atomic* on a single cpu (as provided by the this_cpu_xx class of
# macros).
#
	pushfq_cfi
	cli

	cmpq PER_CPU_VAR((%rsi)), %rax
	jne .Lnot_same
	cmpq PER_CPU_VAR(8(%rsi)), %rdx
	jne .Lnot_same

	movq %rbx, PER_CPU_VAR((%rsi))
	movq %rcx, PER_CPU_VAR(8(%rsi))

	CFI_REMEMBER_STATE
	popfq_cfi
	mov $1, %al
	ret

	CFI_RESTORE_STATE
.Lnot_same:
	popfq_cfi
	xor %al,%al
	ret

CFI_ENDPROC

ENDPROC(this_cpu_cmpxchg16b_emu)
