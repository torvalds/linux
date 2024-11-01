/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <asm/export.h>

.text

/*
 * Inputs:
 * %esi : memory location to compare
 * %eax : low 32 bits of old value
 * %edx : high 32 bits of old value
 * %ebx : low 32 bits of new value
 * %ecx : high 32 bits of new value
 */
SYM_FUNC_START(cmpxchg8b_emu)

#
# Emulate 'cmpxchg8b (%esi)' on UP except we don't
# set the whole ZF thing (caller will just compare
# eax:edx with the expected value)
#
	pushfl
	cli

	cmpl  (%esi), %eax
	jne .Lnot_same
	cmpl 4(%esi), %edx
	jne .Lhalf_same

	movl %ebx,  (%esi)
	movl %ecx, 4(%esi)

	popfl
	RET

.Lnot_same:
	movl  (%esi), %eax
.Lhalf_same:
	movl 4(%esi), %edx

	popfl
	RET

SYM_FUNC_END(cmpxchg8b_emu)
EXPORT_SYMBOL(cmpxchg8b_emu)
