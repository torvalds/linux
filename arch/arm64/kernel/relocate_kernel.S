/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * kexec for arm64
 *
 * Copyright (C) Linaro.
 * Copyright (C) Huawei Futurewei Technologies.
 * Copyright (C) 2021, Microsoft Corporation.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#include <linux/kexec.h>
#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/kexec.h>
#include <asm/page.h>
#include <asm/sysreg.h>
#include <asm/virt.h>

.macro turn_off_mmu tmp1, tmp2
	mov_q   \tmp1, INIT_SCTLR_EL1_MMU_OFF
	pre_disable_mmu_workaround
	msr	sctlr_el1, \tmp1
	isb
.endm

.section    ".kexec_relocate.text", "ax"
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
	/*
	 * The kimage structure isn't allocated specially and may be clobbered
	 * during relocation. We must load any values we need from it prior to
	 * any relocation occurring.
	 */
	ldr	x28, [x0, #KIMAGE_START]
	ldr	x27, [x0, #KIMAGE_ARCH_EL2_VECTORS]
	ldr	x26, [x0, #KIMAGE_ARCH_DTB_MEM]

	/* Setup the list loop variables. */
	ldr	x18, [x0, #KIMAGE_ARCH_ZERO_PAGE] /* x18 = zero page for BBM */
	ldr	x17, [x0, #KIMAGE_ARCH_TTBR1]	/* x17 = linear map copy */
	ldr	x16, [x0, #KIMAGE_HEAD]		/* x16 = kimage_head */
	ldr	x22, [x0, #KIMAGE_ARCH_PHYS_OFFSET]	/* x22 phys_offset */
	raw_dcache_line_size x15, x1		/* x15 = dcache line size */
	break_before_make_ttbr_switch	x18, x17, x1, x2 /* set linear map */
.Lloop:
	and	x12, x16, PAGE_MASK		/* x12 = addr */
	sub	x12, x12, x22			/* Convert x12 to virt */
	/* Test the entry flags. */
.Ltest_source:
	tbz	x16, IND_SOURCE_BIT, .Ltest_indirection

	/* Invalidate dest page to PoC. */
	mov	x19, x13
	copy_page x13, x12, x1, x2, x3, x4, x5, x6, x7, x8
	add	x1, x19, #PAGE_SIZE
	dcache_by_myline_op civac, sy, x19, x1, x15, x20
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
	/* wait for writes from copy_page to finish */
	dsb	nsh
	ic	iallu
	dsb	nsh
	isb
	turn_off_mmu x12, x13

	/* Start new image. */
	cbz	x27, .Lel1
	mov	x1, x28				/* kernel entry point */
	mov	x2, x26				/* dtb address */
	mov	x3, xzr
	mov	x4, xzr
	mov     x0, #HVC_SOFT_RESTART
	hvc	#0				/* Jumps from el2 */
.Lel1:
	mov	x0, x26				/* dtb address */
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	br	x28				/* Jumps from el1 */
SYM_CODE_END(arm64_relocate_new_kernel)
