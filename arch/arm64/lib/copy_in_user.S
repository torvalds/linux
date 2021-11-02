/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copy from user space to user space
 *
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/linkage.h>

#include <asm/asm-uaccess.h>
#include <asm/assembler.h>
#include <asm/cache.h>

/*
 * Copy from user space to user space (alignment handled by the hardware)
 *
 * Parameters:
 *	x0 - to
 *	x1 - from
 *	x2 - n
 * Returns:
 *	x0 - bytes not copied
 */
	.macro ldrb1 reg, ptr, val
	uao_user_alternative 9998f, ldrb, ldtrb, \reg, \ptr, \val
	.endm

	.macro strb1 reg, ptr, val
	uao_user_alternative 9998f, strb, sttrb, \reg, \ptr, \val
	.endm

	.macro ldrh1 reg, ptr, val
	uao_user_alternative 9997f, ldrh, ldtrh, \reg, \ptr, \val
	.endm

	.macro strh1 reg, ptr, val
	uao_user_alternative 9997f, strh, sttrh, \reg, \ptr, \val
	.endm

	.macro ldr1 reg, ptr, val
	uao_user_alternative 9997f, ldr, ldtr, \reg, \ptr, \val
	.endm

	.macro str1 reg, ptr, val
	uao_user_alternative 9997f, str, sttr, \reg, \ptr, \val
	.endm

	.macro ldp1 reg1, reg2, ptr, val
	uao_ldp 9997f, \reg1, \reg2, \ptr, \val
	.endm

	.macro stp1 reg1, reg2, ptr, val
	uao_stp 9997f, \reg1, \reg2, \ptr, \val
	.endm

end	.req	x5
srcin	.req	x15
SYM_FUNC_START(__arch_copy_in_user)
	add	end, x0, x2
	mov	srcin, x1
#include "copy_template.S"
	mov	x0, #0
	ret
SYM_FUNC_END(__arch_copy_in_user)
EXPORT_SYMBOL(__arch_copy_in_user)

	.section .fixup,"ax"
	.align	2
9997:	cmp	dst, dstin
	b.ne	9998f
	// Before being absolutely sure we couldn't copy anything, try harder
USER(9998f, ldtrb tmp1w, [srcin])
USER(9998f, sttrb tmp1w, [dst])
	add	dst, dst, #1
9998:	sub	x0, end, dst			// bytes not copied
	ret
	.previous
