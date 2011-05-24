/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; version 2
 *	of the License.
 *
 */
#include <linux/linkage.h>
#include <asm/alternative-asm.h>
#include <asm/frame.h>
#include <asm/dwarf2.h>

#ifdef CONFIG_SMP
#define SEG_PREFIX %gs:
#else
#define SEG_PREFIX
#endif

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
this_cpu_cmpxchg16b_emu:
	pushf
	cli

	cmpq SEG_PREFIX(%rsi), %rax
	jne not_same
	cmpq SEG_PREFIX 8(%rsi), %rdx
	jne not_same

	movq %rbx, SEG_PREFIX(%rsi)
	movq %rcx, SEG_PREFIX 8(%rsi)

	popf
	mov $1, %al
	ret

 not_same:
	popf
	xor %al,%al
	ret

CFI_ENDPROC

ENDPROC(this_cpu_cmpxchg16b_emu)
