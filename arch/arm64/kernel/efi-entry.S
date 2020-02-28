/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * EFI entry point.
 *
 * Copyright (C) 2013, 2014 Red Hat, Inc.
 * Author: Mark Salter <msalter@redhat.com>
 */
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>

	__INIT

SYM_CODE_START(efi_enter_kernel)
	/*
	 * efi_entry() will have copied the kernel image if necessary and we
	 * end up here with device tree address in x1 and the kernel entry
	 * point stored in x0. Save those values in registers which are
	 * callee preserved.
	 */
	ldr	w2, =stext_offset
	add	x19, x0, x2		// relocated Image entrypoint
	mov	x20, x1			// DTB address

	/*
	 * Clean the copied Image to the PoC, and ensure it is not shadowed by
	 * stale icache entries from before relocation.
	 */
	ldr	w1, =kernel_size
	bl	__clean_dcache_area_poc
	ic	ialluis

	/*
	 * Clean the remainder of this routine to the PoC
	 * so that we can safely disable the MMU and caches.
	 */
	adr	x0, 0f
	ldr	w1, 3f
	bl	__clean_dcache_area_poc
0:
	/* Turn off Dcache and MMU */
	mrs	x0, CurrentEL
	cmp	x0, #CurrentEL_EL2
	b.ne	1f
	mrs	x0, sctlr_el2
	bic	x0, x0, #1 << 0	// clear SCTLR.M
	bic	x0, x0, #1 << 2	// clear SCTLR.C
	pre_disable_mmu_workaround
	msr	sctlr_el2, x0
	isb
	b	2f
1:
	mrs	x0, sctlr_el1
	bic	x0, x0, #1 << 0	// clear SCTLR.M
	bic	x0, x0, #1 << 2	// clear SCTLR.C
	pre_disable_mmu_workaround
	msr	sctlr_el1, x0
	isb
2:
	/* Jump to kernel entry point */
	mov	x0, x20
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	br	x19
SYM_CODE_END(efi_enter_kernel)
3:	.long	. - 0b
