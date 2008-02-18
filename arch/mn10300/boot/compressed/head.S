/* Boot entry point for a compressed MN10300 kernel
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
	.section	.text

#define DEBUG

#include <linux/linkage.h>
#include <asm/cpu-regs.h>

	.globl startup_32
startup_32:
	# first save off parameters from bootloader
	mov	param_save_area,a0
	mov	d0,(a0)
	mov	d1,(4,a0)
	mov	d2,(8,a0)

	mov	sp,a3
	mov	decomp_stack+0x2000-4,a0
	mov	a0,sp

	# invalidate and enable both of the caches
	mov	CHCTR,a0
	clr	d0
	movhu	d0,(a0)					# turn off first
	mov	CHCTR_ICINV|CHCTR_DCINV,d0
	movhu	d0,(a0)
	setlb
	mov	(a0),d0
	btst	CHCTR_ICBUSY|CHCTR_DCBUSY,d0		# wait till not busy
	lne
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD,d0	# writethru dcache
	movhu	d0,(a0)					# enable

	# clear the BSS area
	mov	__bss_start,a0
	mov	_end,a1
	clr	d0
bssclear:
	cmp	a1,a0
	bge	bssclear_end
	movbu	d0,(a0)
	inc	a0
	bra	bssclear
bssclear_end:

	# decompress the kernel
	call	decompress_kernel[],0

	# disable caches again
	mov	CHCTR,a0
	clr	d0
	movhu	d0,(a0)
	setlb
	mov	(a0),d0
	btst	CHCTR_ICBUSY|CHCTR_DCBUSY,d0		# wait till not busy
	lne

	mov	param_save_area,a0
	mov	(a0),d0
	mov	(4,a0),d1
	mov	(8,a0),d2

	mov	a3,sp
	mov	CONFIG_KERNEL_TEXT_ADDRESS,a0
	jmp	(a0)

	.data
	.align		4
param_save_area:
	.rept 3
	.word		0
	.endr

	.section	.bss
	.align		4
decomp_stack:
	.space		0x2000
