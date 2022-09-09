/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AArch32 user helpers.
 * Based on the kuser helpers in arch/arm/kernel/entry-armv.S.
 *
 * Copyright (C) 2005-2011 Nicolas Pitre <nico@fluxnic.net>
 * Copyright (C) 2012-2018 ARM Ltd.
 *
 * The kuser helpers below are mapped at a fixed address by
 * aarch32_setup_additional_pages() and are provided for compatibility
 * reasons with 32 bit (aarch32) applications that need them.
 *
 * See Documentation/arm/kernel_user_helpers.rst for formal definitions.
 */

#include <asm/unistd.h>

	.section .rodata
	.align	5
	.globl	__kuser_helper_start
__kuser_helper_start:

__kuser_cmpxchg64:			// 0xffff0f60
	.inst	0xe92d00f0		//	push		{r4, r5, r6, r7}
	.inst	0xe1c040d0		//	ldrd		r4, r5, [r0]
	.inst	0xe1c160d0		//	ldrd		r6, r7, [r1]
	.inst	0xe1b20f9f		// 1:	ldrexd		r0, r1, [r2]
	.inst	0xe0303004		//	eors		r3, r0, r4
	.inst	0x00313005		//	eoreqs		r3, r1, r5
	.inst	0x01a23e96		//	stlexdeq	r3, r6, [r2]
	.inst	0x03330001		//	teqeq		r3, #1
	.inst	0x0afffff9		//	beq		1b
	.inst	0xf57ff05b		//	dmb		ish
	.inst	0xe2730000		//	rsbs		r0, r3, #0
	.inst	0xe8bd00f0		//	pop		{r4, r5, r6, r7}
	.inst	0xe12fff1e		//	bx		lr

	.align	5
__kuser_memory_barrier:			// 0xffff0fa0
	.inst	0xf57ff05b		//	dmb		ish
	.inst	0xe12fff1e		//	bx		lr

	.align	5
__kuser_cmpxchg:			// 0xffff0fc0
	.inst	0xe1923f9f		// 1:	ldrex		r3, [r2]
	.inst	0xe0533000		//	subs		r3, r3, r0
	.inst	0x01823e91		//	stlexeq		r3, r1, [r2]
	.inst	0x03330001		//	teqeq		r3, #1
	.inst	0x0afffffa		//	beq		1b
	.inst	0xf57ff05b		//	dmb		ish
	.inst	0xe2730000		//	rsbs		r0, r3, #0
	.inst	0xe12fff1e		//	bx		lr

	.align	5
__kuser_get_tls:			// 0xffff0fe0
	.inst	0xee1d0f70		//	mrc		p15, 0, r0, c13, c0, 3
	.inst	0xe12fff1e		//	bx		lr
	.rep	5
	.word	0
	.endr

__kuser_helper_version:			// 0xffff0ffc
	.word	((__kuser_helper_end - __kuser_helper_start) >> 5)
	.globl	__kuser_helper_end
__kuser_helper_end:
