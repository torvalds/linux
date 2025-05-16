/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Arm Ltd.
 */

#include <linux/linkage.h>
#include <asm/asm-uaccess.h>

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
	add	x2, x0, x1

#ifdef CONFIG_AS_HAS_MOPS
	.arch_extension mops
alternative_if_not ARM64_HAS_MOPS
	b	.Lno_mops
alternative_else_nop_endif

USER(9f, setpt	[x0]!, x1!, xzr)
USER(6f, setmt	[x0]!, x1!, xzr)
USER(6f, setet	[x0]!, x1!, xzr)
	mov	x0, #0
	ret
.Lno_mops:
#endif

	subs	x1, x1, #8
	b.mi	2f

1:	.p2align 4
USER(9f, sttr	xzr, [x0])
	add	x0, x0, #8
	subs	x1, x1, #8
	b.hi	1b
USER(9f, sttr	xzr, [x2, #-8])
	mov	x0, #0
	ret

2:	tbz	x1, #2, 3f
USER(9f, sttr	wzr, [x0])
USER(8f, sttr	wzr, [x2, #-4])
	mov	x0, #0
	ret

3:	tbz	x1, #1, 4f
USER(9f, sttrh	wzr, [x0])
4:	tbz	x1, #0, 5f
USER(7f, sttrb	wzr, [x2, #-1])
5:	mov	x0, #0
	ret

	// Exception fixups
6:	b.cs	9f
	// Registers are in Option A format
	add	x0, x0, x1
	b	9f
7:	sub	x0, x2, #5	// Adjust for faulting on the final byte...
8:	add	x0, x0, #4	// ...or the second word of the 4-7 byte case
9:	sub	x0, x2, x0
	ret
SYM_FUNC_END(__arch_clear_user)
EXPORT_SYMBOL(__arch_clear_user)
