/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/linkage.h>
#include <linux/const.h>
#include <asm/assembler.h>
#include <asm/page.h>

/*
 * Clear page @dest
 *
 * Parameters:
 *	x0 - dest
 */
SYM_FUNC_START(__pi_clear_page)
#ifdef CONFIG_AS_HAS_MOPS
	.arch_extension mops
alternative_if_not ARM64_HAS_MOPS
	b	.Lno_mops
alternative_else_nop_endif

	mov	x1, #PAGE_SIZE
	setpn	[x0]!, x1!, xzr
	setmn	[x0]!, x1!, xzr
	seten	[x0]!, x1!, xzr
	ret
.Lno_mops:
#endif
	mrs	x1, dczid_el0
	tbnz	x1, #4, 2f	/* Branch if DC ZVA is prohibited */
	and	w1, w1, #0xf
	mov	x2, #4
	lsl	x1, x2, x1

1:	dc	zva, x0
	add	x0, x0, x1
	tst	x0, #(PAGE_SIZE - 1)
	b.ne	1b
	ret

2:	stnp	xzr, xzr, [x0]
	stnp	xzr, xzr, [x0, #16]
	stnp	xzr, xzr, [x0, #32]
	stnp	xzr, xzr, [x0, #48]
	add	x0, x0, #64
	tst	x0, #(PAGE_SIZE - 1)
	b.ne	2b
	ret
SYM_FUNC_END(__pi_clear_page)
SYM_FUNC_ALIAS(clear_page, __pi_clear_page)
EXPORT_SYMBOL(clear_page)
