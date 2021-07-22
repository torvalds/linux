/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * kexec for arm64
 *
 * Copyright (C) Linaro.
 * Copyright (C) Huawei Futurewei Technologies.
 */

#include <linux/kexec.h>
#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/kexec.h>
#include <asm/page.h>
#include <asm/sysreg.h>

/*
 * arm64_relocate_new_kernel - Put a 2nd stage image in place and boot it.
 *
 * The memory that the old kernel occupies may be overwritten when copying the
 * new image to its final location.  To assure that the
 * arm64_relocate_new_kernel routine which does that copy is not overwritten,
 * all code and data needed by arm64_relocate_new_kernel must be between the
 * symbols arm64_relocate_new_kernel and arm64_relocate_new_kernel_end.  The
 * machine_kexec() routine will copy arm64_relocate_new_kernel to the kexec
 * safe memory that has been set up to be preserved during the copy operation.
 */
SYM_CODE_START(arm64_relocate_new_kernel)
	/* Setup the list loop variables. */
	mov	x18, x2				/* x18 = dtb address */
	mov	x17, x1				/* x17 = kimage_start */
	mov	x16, x0				/* x16 = kimage_head */
	mov	x14, xzr			/* x14 = entry ptr */
	mov	x13, xzr			/* x13 = copy dest */
	/* Check if the new image needs relocation. */
	tbnz	x16, IND_DONE_BIT, .Ldone
	raw_dcache_line_size x15, x1		/* x15 = dcache line size */
.Lloop:
	and	x12, x16, PAGE_MASK		/* x12 = addr */

	/* Test the entry flags. */
.Ltest_source:
	tbz	x16, IND_SOURCE_BIT, .Ltest_indirection

	/* Invalidate dest page to PoC. */
	mov     x2, x13
	add     x20, x2, #PAGE_SIZE
	sub     x1, x15, #1
	bic     x2, x2, x1
2:	dc      ivac, x2
	add     x2, x2, x15
	cmp     x2, x20
	b.lo    2b
	dsb     sy

	copy_page x13, x12, x1, x2, x3, x4, x5, x6, x7, x8
	b	.Lnext
.Ltest_indirection:
	tbz	x16, IND_INDIRECTION_BIT, .Ltest_destination
	mov	x14, x12			/* ptr = addr */
	b	.Lnext
.Ltest_destination:
	tbz	x16, IND_DESTINATION_BIT, .Lnext
	mov	x13, x12			/* dest = addr */
.Lnext:
	ldr	x16, [x14], #8			/* entry = *ptr++ */
	tbz	x16, IND_DONE_BIT, .Lloop	/* while (!(entry & DONE)) */
.Ldone:
	/* wait for writes from copy_page to finish */
	dsb	nsh
	ic	iallu
	dsb	nsh
	isb

	/* Start new image. */
	mov	x0, x18
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	br	x17

SYM_CODE_END(arm64_relocate_new_kernel)

.align 3	/* To keep the 64-bit values below naturally aligned. */

.Lcopy_end:
.org	KEXEC_CONTROL_PAGE_SIZE

/*
 * arm64_relocate_new_kernel_size - Number of bytes to copy to the
 * control_code_page.
 */
.globl arm64_relocate_new_kernel_size
arm64_relocate_new_kernel_size:
	.quad	.Lcopy_end - arm64_relocate_new_kernel
