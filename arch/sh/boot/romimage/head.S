/* SPDX-License-Identifier: GPL-2.0 */
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

#ifdef CONFIG_ROMIMAGE_MMCIF
	/* load the romImage to above the empty zero page */
	mov.l	empty_zero_page_dst, r4
	mov.l	empty_zero_page_dst_adj, r5
	add	r5, r4
	mov.l	bytes_to_load, r5
	mov.l	loader_function, r7
	jsr	@r7
	 mov	r4, r15

	mov.l	empty_zero_page_dst, r4
	mov.l	empty_zero_page_dst_adj, r5
	add	r5, r4
	mov.l	loaded_code_offs, r5
	add	r5, r4
	jmp	@r4
	 nop

	.balign 4
empty_zero_page_dst_adj:
	.long	PAGE_SIZE
bytes_to_load:
	.long	end_data - romstart
loader_function:
	.long	mmcif_loader
loaded_code_offs:
	.long	loaded_code - romstart
loaded_code:
#endif /* CONFIG_ROMIMAGE_MMCIF */

	/* copy the empty_zero_page contents to where vmlinux expects it */
	mova	extra_data_pos, r0
	mov.l	extra_data_size, r1
	add	r1, r0
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
	mova	extra_data_pos, r0
	add	r1, r0
	mov.l	extra_data_size, r1
	add	r1, r0
	jmp	@r0
	 nop

	.align 2
empty_zero_page_dst:
	.long	_text
extra_data_pos:
extra_data_size:
	.long	zero_page_pos - extra_data_pos
