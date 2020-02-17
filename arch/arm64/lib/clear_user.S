/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/lib/clear_user.S
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#include <linux/linkage.h>

#include <asm/asm-uaccess.h>
#include <asm/assembler.h>

	.text

/* Prototype: int __arch_clear_user(void *addr, size_t sz)
 * Purpose  : clear some user memory
 * Params   : addr - user memory address to clear
 *          : sz   - number of bytes to clear
 * Returns  : number of bytes NOT cleared
 *
 * Alignment fixed up by hardware.
 */
SYM_FUNC_START(__arch_clear_user)
	mov	x2, x1			// save the size for fixup return
	subs	x1, x1, #8
	b.mi	2f
1:
uao_user_alternative 9f, str, sttr, xzr, x0, 8
	subs	x1, x1, #8
	b.pl	1b
2:	adds	x1, x1, #4
	b.mi	3f
uao_user_alternative 9f, str, sttr, wzr, x0, 4
	sub	x1, x1, #4
3:	adds	x1, x1, #2
	b.mi	4f
uao_user_alternative 9f, strh, sttrh, wzr, x0, 2
	sub	x1, x1, #2
4:	adds	x1, x1, #1
	b.mi	5f
uao_user_alternative 9f, strb, sttrb, wzr, x0, 0
5:	mov	x0, #0
	ret
SYM_FUNC_END(__arch_clear_user)
EXPORT_SYMBOL(__arch_clear_user)

	.section .fixup,"ax"
	.align	2
9:	mov	x0, x2			// return the original size
	ret
	.previous
