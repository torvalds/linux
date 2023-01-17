/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * EFI entry point.
 *
 * Copyright (C) 2013, 2014 Red Hat, Inc.
 * Author: Mark Salter <msalter@redhat.com>
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

	/*
	 * The entrypoint of a arm64 bare metal image is at offset #0 of the
	 * image, so this is a reasonable default for primary_entry_offset.
	 * Only when the EFI stub is integrated into the core kernel, it is not
	 * guaranteed that the PE/COFF header has been copied to memory too, so
	 * in this case, primary_entry_offset should be overridden by the
	 * linker and point to primary_entry() directly.
	 */
	.weak	primary_entry_offset

SYM_CODE_START(efi_enter_kernel)
	/*
	 * efi_pe_entry() will have copied the kernel image if necessary and we
	 * end up here with device tree address in x1 and the kernel entry
	 * point stored in x0. Save those values in registers which are
	 * callee preserved.
	 */
	ldr	w2, =primary_entry_offset
	add	x19, x0, x2		// relocated Image entrypoint

	mov	x0, x1			// DTB address
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr

	/*
	 * Clean the remainder of this routine to the PoC
	 * so that we can safely disable the MMU and caches.
	 */
	adr	x4, 1f
	dc	civac, x4
	dsb	sy

	/* Turn off Dcache and MMU */
	mrs	x4, CurrentEL
	cmp	x4, #CurrentEL_EL2
	mrs	x4, sctlr_el1
	b.ne	0f
	mrs	x4, sctlr_el2
0:	bic	x4, x4, #SCTLR_ELx_M
	bic	x4, x4, #SCTLR_ELx_C
	b.eq	1f
	b	2f

	.balign	32
1:	pre_disable_mmu_workaround
	msr	sctlr_el2, x4
	isb
	br	x19		// jump to kernel entrypoint

2:	pre_disable_mmu_workaround
	msr	sctlr_el1, x4
	isb
	br	x19		// jump to kernel entrypoint

	.org	1b + 32
SYM_CODE_END(efi_enter_kernel)
