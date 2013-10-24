/*
 * Startup code for use with the u-boot bootloader.
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/setup.h>
#include <asm/thread_info.h>
#include <asm/sysreg.h>

	/*
	 * The kernel is loaded where we want it to be and all caches
	 * have just been flushed. We get two parameters from u-boot:
	 *
	 * r12 contains a magic number (ATAG_MAGIC)
	 * r11 points to a tag table providing information about
	 *     the system.
	 */
	.section .init.text,"ax"
	.global _start
_start:
	/* Initialize .bss */
	lddpc	r2, bss_start_addr
	lddpc   r3, end_addr
	mov	r0, 0
	mov	r1, 0
1:      st.d    r2++, r0
	cp      r2, r3
	brlo    1b

	/* Initialize status register */
	lddpc	r0, init_sr
	mtsr	SYSREG_SR, r0

	/* Set initial stack pointer */
	lddpc	sp, stack_addr
	sub	sp, -THREAD_SIZE

#ifdef CONFIG_FRAME_POINTER
	/* Mark last stack frame */
	mov	lr, 0
	mov	r7, 0
#endif

	/* Check if the boot loader actually provided a tag table */
	lddpc	r0, magic_number
	cp.w	r12, r0
	brne	no_tag_table

	/*
	 * Save the tag table address for later use. This must be done
	 * _after_ .bss has been initialized...
	 */
	lddpc	r0, tag_table_addr
	st.w	r0[0], r11

	/* Jump to loader-independent setup code */
	rjmp	kernel_entry

	.align	2
magic_number:
	.long	ATAG_MAGIC
tag_table_addr:
	.long	bootloader_tags
bss_start_addr:
	.long   __bss_start
end_addr:
	.long   _end
init_sr:
	.long	0x007f0000	/* Supervisor mode, everything masked */
stack_addr:
	.long	init_thread_union

no_tag_table:
	sub	r12, pc, (. - 2f)
	bral	panic
2:	.asciz	"Boot loader didn't provide correct magic number\n"
