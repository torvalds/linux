/*
 *  linux/arch/sh/boot/romimage/head.S
 *
 * Board specific setup code, executed before zImage loader
 */

.text
	#include <asm/page.h>

	.global	romstart
romstart:
	/* include board specific setup code */
#include <mach/romimage.h>

	/* copy the empty_zero_page contents to where vmlinux expects it */
	mova	empty_zero_page_src, r0
	mov.l	empty_zero_page_dst, r1
	mov	#(PAGE_SHIFT - 4), r4
	mov	#1, r3
	shld	r4, r3 /* r3 = PAGE_SIZE / 16 */

1:
	mov.l	@r0, r4
	mov.l	@(4, r0), r5
	mov.l	@(8, r0), r6
	mov.l	@(12, r0), r7
	add	#16,r0
	mov.l	r4, @r1
	mov.l	r5, @(4, r1)
	mov.l	r6, @(8, r1)
	mov.l	r7, @(12, r1)
	dt	r3
	add	#16,r1
	bf	1b

	/* jump to the zImage entry point located after the zero page data */
	mov	#PAGE_SHIFT, r4
	mov	#1, r1
	shld	r4, r1
	mova	empty_zero_page_src, r0
	add	r1, r0
	jmp	@r0
	 nop

	.align 2
empty_zero_page_dst:
	.long	_text
empty_zero_page_src:
