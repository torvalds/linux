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
#include <asm/cache.h>
#ifdef CONFIG_SMP
#include <proc/smp-regs.h>
#endif

	.globl startup_32
startup_32:
#ifdef CONFIG_SMP
	#
	# Secondary CPUs jump directly to the kernel entry point
	#
	# Must save primary CPU's D0-D2 registers as they hold boot parameters
	#
	mov	(CPUID), d3
	and	CPUID_MASK,d3
	beq	startup_primary
	mov	CONFIG_KERNEL_TEXT_ADDRESS,a0
	jmp	(a0)

startup_primary:
#endif /* CONFIG_SMP */

	# first save parameters from bootloader
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

#ifdef CONFIG_MN10300_CACHE_ENABLED
#ifdef CONFIG_MN10300_CACHE_WBACK
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD_WRBACK,d0
#else
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD_WRTHROUGH,d0
#endif /* WBACK */
	movhu	d0,(a0)					# enable
#endif /* !ENABLED */

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
#ifdef CONFIG_MN10300_CACHE_WBACK
	call	mn10300_dcache_flush_inv[],0
#endif

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

	# jump to the kernel proper entry point
	mov	a3,sp
	mov	CONFIG_KERNEL_TEXT_ADDRESS,a0
	jmp	(a0)


###############################################################################
#
# Cache flush routines
#
###############################################################################
#ifdef CONFIG_MN10300_CACHE_WBACK
mn10300_dcache_flush_inv:
	movhu	(CHCTR),d0
	btst	CHCTR_DCEN,d0
	beq	mn10300_dcache_flush_inv_end

	mov	L1_CACHE_NENTRIES,d1
	clr	a1

mn10300_dcache_flush_inv_loop:
	mov	(DCACHE_PURGE_WAY0(0),a1),d0	# unconditional purge
	mov	(DCACHE_PURGE_WAY1(0),a1),d0	# unconditional purge
	mov	(DCACHE_PURGE_WAY2(0),a1),d0	# unconditional purge
	mov	(DCACHE_PURGE_WAY3(0),a1),d0	# unconditional purge

	add	L1_CACHE_BYTES,a1
	add	-1,d1
	bne	mn10300_dcache_flush_inv_loop

mn10300_dcache_flush_inv_end:
	ret	[],0
#endif /* CONFIG_MN10300_CACHE_WBACK */


###############################################################################
#
# Data areas
#
###############################################################################
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
