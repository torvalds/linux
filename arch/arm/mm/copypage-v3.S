/*
 *  linux/arch/arm/lib/copypage.S
 *
 *  Copyright (C) 1995-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ASM optimised string functions
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>

		.text
		.align	5
/*
 * ARMv3 optimised copy_user_page
 *
 * FIXME: do we need to handle cache stuff...
 */
ENTRY(v3_copy_user_page)
	stmfd	sp!, {r4, lr}			@	2
	mov	r2, #PAGE_SZ/64			@	1
	ldmia	r1!, {r3, r4, ip, lr}		@	4+1
1:	stmia	r0!, {r3, r4, ip, lr}		@	4
	ldmia	r1!, {r3, r4, ip, lr}		@	4+1
	stmia	r0!, {r3, r4, ip, lr}		@	4
	ldmia	r1!, {r3, r4, ip, lr}		@	4+1
	stmia	r0!, {r3, r4, ip, lr}		@	4
	ldmia	r1!, {r3, r4, ip, lr}		@	4
	subs	r2, r2, #1			@	1
	stmia	r0!, {r3, r4, ip, lr}		@	4
	ldmneia	r1!, {r3, r4, ip, lr}		@	4
	bne	1b				@	1
	LOADREGS(fd, sp!, {r4, pc})		@	3

	.align	5
/*
 * ARMv3 optimised clear_user_page
 *
 * FIXME: do we need to handle cache stuff...
 */
ENTRY(v3_clear_user_page)
	str	lr, [sp, #-4]!
	mov	r1, #PAGE_SZ/64			@ 1
	mov	r2, #0				@ 1
	mov	r3, #0				@ 1
	mov	ip, #0				@ 1
	mov	lr, #0				@ 1
1:	stmia	r0!, {r2, r3, ip, lr}		@ 4
	stmia	r0!, {r2, r3, ip, lr}		@ 4
	stmia	r0!, {r2, r3, ip, lr}		@ 4
	stmia	r0!, {r2, r3, ip, lr}		@ 4
	subs	r1, r1, #1			@ 1
	bne	1b				@ 1
	ldr	pc, [sp], #4

	__INITDATA

	.type	v3_user_fns, #object
ENTRY(v3_user_fns)
	.long	v3_clear_user_page
	.long	v3_copy_user_page
	.size	v3_user_fns, . - v3_user_fns
