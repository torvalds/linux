/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; version 2
 *	of the License.
 *
 */

#include <linux/linkage.h>

.text

/*
 * Inputs:
 * %esi : memory location to compare
 * %eax : low 32 bits of old value
 * %edx : high 32 bits of old value
 * %ebx : low 32 bits of new value
 * %ecx : high 32 bits of new value
 */
ENTRY(cmpxchg8b_emu)

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
	ret

.Lnot_same:
	movl  (%esi), %eax
.Lhalf_same:
	movl 4(%esi), %edx

	popfl
	ret

ENDPROC(cmpxchg8b_emu)
