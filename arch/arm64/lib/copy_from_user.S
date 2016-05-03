/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/linkage.h>

#include <asm/alternative.h>
#include <asm/assembler.h>
#include <asm/cache.h>
#include <asm/cpufeature.h>
#include <asm/sysreg.h>

/*
 * Copy from user space to a kernel buffer (alignment handled by the hardware)
 *
 * Parameters:
 *	x0 - to
 *	x1 - from
 *	x2 - n
 * Returns:
 *	x0 - bytes not copied
 */

	.macro ldrb1 ptr, regB, val
	uao_user_alternative 9998f, ldrb, ldtrb, \ptr, \regB, \val
	.endm

	.macro strb1 ptr, regB, val
	strb \ptr, [\regB], \val
	.endm

	.macro ldrh1 ptr, regB, val
	uao_user_alternative 9998f, ldrh, ldtrh, \ptr, \regB, \val
	.endm

	.macro strh1 ptr, regB, val
	strh \ptr, [\regB], \val
	.endm

	.macro ldr1 ptr, regB, val
	uao_user_alternative 9998f, ldr, ldtr, \ptr, \regB, \val
	.endm

	.macro str1 ptr, regB, val
	str \ptr, [\regB], \val
	.endm

	.macro ldp1 ptr, regB, regC, val
	uao_ldp 9998f, \ptr, \regB, \regC, \val
	.endm

	.macro stp1 ptr, regB, regC, val
	stp \ptr, \regB, [\regC], \val
	.endm

end	.req	x5
ENTRY(__copy_from_user)
ALTERNATIVE("nop", __stringify(SET_PSTATE_PAN(0)), ARM64_ALT_PAN_NOT_UAO, \
	    CONFIG_ARM64_PAN)
	add	end, x0, x2
#include "copy_template.S"
ALTERNATIVE("nop", __stringify(SET_PSTATE_PAN(1)), ARM64_ALT_PAN_NOT_UAO, \
	    CONFIG_ARM64_PAN)
	mov	x0, #0				// Nothing to copy
	ret
ENDPROC(__copy_from_user)

	.section .fixup,"ax"
	.align	2
9998:
	sub	x0, end, dst
9999:
	strb	wzr, [dst], #1			// zero remaining buffer space
	cmp	dst, end
	b.lo	9999b
	ret
	.previous
