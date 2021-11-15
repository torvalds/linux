/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Hibernate low-level support
 *
 * Copyright (C) 2016 ARM Ltd.
 * Author:	James Morse <james.morse@arm.com>
 */
#include <linux/linkage.h>
#include <linux/errno.h>

#include <asm/asm-offsets.h>
#include <asm/assembler.h>
#include <asm/cputype.h>
#include <asm/memory.h>
#include <asm/page.h>
#include <asm/virt.h>

/*
 * Resume from hibernate
 *
 * Loads temporary page tables then restores the memory image.
 * Finally branches to cpu_resume() to restore the state saved by
 * swsusp_arch_suspend().
 *
 * Because this code has to be copied to a 'safe' page, it can't call out to
 * other functions by PC-relative address. Also remember that it may be
 * mid-way through over-writing other functions. For this reason it contains
 * code from caches_clean_inval_pou() and uses the copy_page() macro.
 *
 * This 'safe' page is mapped via ttbr0, and executed from there. This function
 * switches to a copy of the linear map in ttbr1, performs the restore, then
 * switches ttbr1 to the original kernel's swapper_pg_dir.
 *
 * All of memory gets written to, including code. We need to clean the kernel
 * text to the Point of Coherence (PoC) before secondary cores can be booted.
 * Because the kernel modules and executable pages mapped to user space are
 * also written as data, we clean all pages we touch to the Point of
 * Unification (PoU).
 *
 * x0: physical address of temporary page tables
 * x1: physical address of swapper page tables
 * x2: address of cpu_resume
 * x3: linear map address of restore_pblist in the current kernel
 * x4: physical address of __hyp_stub_vectors, or 0
 * x5: physical address of a  zero page that remains zero after resume
 */
.pushsection    ".hibernate_exit.text", "ax"
SYM_CODE_START(swsusp_arch_suspend_exit)
	/*
	 * We execute from ttbr0, change ttbr1 to our copied linear map tables
	 * with a break-before-make via the zero page
	 */
	break_before_make_ttbr_switch	x5, x0, x6, x8

	mov	x21, x1
	mov	x30, x2
	mov	x24, x4
	mov	x25, x5

	/* walk the restore_pblist and use copy_page() to over-write memory */
	mov	x19, x3

1:	ldr	x10, [x19, #HIBERN_PBE_ORIG]
	mov	x0, x10
	ldr	x1, [x19, #HIBERN_PBE_ADDR]

	copy_page	x0, x1, x2, x3, x4, x5, x6, x7, x8, x9

	add	x1, x10, #PAGE_SIZE
	/* Clean the copied page to PoU - based on caches_clean_inval_pou() */
	raw_dcache_line_size x2, x3
	sub	x3, x2, #1
	bic	x4, x10, x3
2:	/* clean D line / unified line */
alternative_insn "dc cvau, x4",  "dc civac, x4",  ARM64_WORKAROUND_CLEAN_CACHE
	add	x4, x4, x2
	cmp	x4, x1
	b.lo	2b

	ldr	x19, [x19, #HIBERN_PBE_NEXT]
	cbnz	x19, 1b
	dsb	ish		/* wait for PoU cleaning to finish */

	/* switch to the restored kernels page tables */
	break_before_make_ttbr_switch	x25, x21, x6, x8

	ic	ialluis
	dsb	ish
	isb

	cbz	x24, 3f		/* Do we need to re-initialise EL2? */
	hvc	#0
3:	ret
SYM_CODE_END(swsusp_arch_suspend_exit)
.popsection
