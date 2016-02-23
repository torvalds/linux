/*
 * linux/arch/unicore32/boot/compressed/head.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <mach/memory.h>

#define csub	cmpsub
#define cand	cmpand
#define nop8	nop; nop; nop; nop; nop; nop; nop; nop

		.section ".start", #alloc, #execinstr
		.text
start:
		.type	start,#function

		/* Initialize ASR, PRIV mode and INTR off */
		mov	r0, #0xD3
		mov.a	asr, r0

		adr	r0, LC0
		ldm	(r1, r2, r3, r5, r6, r7, r8), [r0]+
		ldw	sp, [r0+], #28
		sub.a	r0, r0, r1		@ calculate the delta offset

		/*
		 * if delta is zero, we are running at the address
		 * we were linked at.
		 */
		beq	not_relocated

		/*
		 * We're running at a different address.  We need to fix
		 * up various pointers:
		 *   r5 - zImage base address (_start)
		 *   r7 - GOT start
		 *   r8 - GOT end
		 */
		add	r5, r5, r0
		add	r7, r7, r0
		add	r8, r8, r0

		/*
		 * we need to fix up pointers into the BSS region.
		 *   r2 - BSS start
		 *   r3 - BSS end
		 *   sp - stack pointer
		 */
		add	r2, r2, r0
		add	r3, r3, r0
		add	sp, sp, r0

		/*
		 * Relocate all entries in the GOT table.
		 * This fixes up the C references.
		 *   r7 - GOT start
		 *   r8 - GOT end
		 */
1001:		ldw	r1, [r7+], #0
		add	r1, r1, r0
		stw.w	r1, [r7]+, #4
		csub.a	r7, r8
		bub	1001b

not_relocated:
		/*
		 * Clear BSS region.
		 *   r2 - BSS start
		 *   r3 - BSS end
		 */
		mov	r0, #0
1002:		stw.w	r0, [r2]+, #4
		csub.a	r2, r3
		bub	1002b

		/*
		 * Turn on the cache.
		 */
                mov     r0, #0
                movc    p0.c5, r0, #28		@ cache invalidate all
                nop8
                movc    p0.c6, r0, #6		@ tlb invalidate all
                nop8

                mov     r0, #0x1c		@ en icache and wb dcache
                movc    p0.c1, r0, #0
                nop8

		/*
		 * Set up some pointers, for starting decompressing.
		 */

		mov	r1, sp			@ malloc space above stack
		add	r2, sp, #0x10000	@ 64k max

		/*
		 * Check to see if we will overwrite ourselves.
		 *   r4 = final kernel address
		 *   r5 = start of this image
		 *   r6 = size of decompressed image
		 *   r2 = end of malloc space (and therefore this image)
		 * We basically want:
		 *   r4 >= r2 -> OK
		 *   r4 + image length <= r5 -> OK
		 */
		ldw	r4, =KERNEL_IMAGE_START
		csub.a	r4, r2
		bea	wont_overwrite
		add	r0, r4, r6
		csub.a	r0, r5
		beb	wont_overwrite

		/*
		 * If overwrite, just print error message
		 */
		b	__error_overwrite

		/*
		 * We're not in danger of overwriting ourselves.
		 * Do this the simple way.
		 */
wont_overwrite:
		/*
		 * decompress_kernel:
		 *   r0: output_start
		 *   r1: free_mem_ptr_p
		 *   r2: free_mem_ptr_end_p
		 */
		mov	r0, r4
		b.l	decompress_kernel	@ C functions

		/*
		 * Clean and flush the cache to maintain consistency.
		 */
		mov	r0, #0
                movc    p0.c5, r0, #14		@ flush dcache
		nop8
                movc    p0.c5, r0, #20		@ icache invalidate all
                nop8

		/*
		 * Turn off the Cache and MMU.
		 */
		mov	r0, #0			@ disable i/d cache and MMU
		movc	p0.c1, r0, #0
                nop8

		mov	r0, #0			@ must be zero
		ldw	r4, =KERNEL_IMAGE_START
		mov	pc, r4			@ call kernel


		.align	2
		.type	LC0, #object
LC0:		.word	LC0			@ r1
		.word	__bss_start		@ r2
		.word	_end			@ r3
		.word	_start			@ r5
		.word	_image_size		@ r6
		.word	_got_start		@ r7
		.word	_got_end		@ r8
		.word	decompress_stack_end	@ sp
		.size	LC0, . - LC0

print_string:
#ifdef CONFIG_DEBUG_OCD
2001:		ldb.w	r1, [r0]+, #1
		csub.a	r1, #0
		bne	2002f
		mov	pc, lr
2002:
		movc	r2, p1.c0, #0
		cand.a	r2, #2
		bne	2002b
		movc	p1.c1, r1, #1
		csub.a	r1, #'\n'
		cmoveq	r1, #'\r'
		beq	2002b
		b	2001b
#else
		mov	pc, lr
#endif

__error_overwrite:
		adr	r0, str_error
		b.l	print_string
2001:		nop8
		b	2001b
str_error:	.asciz	"\nError: Kernel address OVERWRITE\n"
		.align

		.ltorg

		.align	4
		.section ".stack", "aw", %nobits
decompress_stack:	.space	4096
decompress_stack_end:
