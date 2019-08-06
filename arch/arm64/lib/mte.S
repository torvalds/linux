/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 ARM Ltd.
 */
#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/page.h>
#include <asm/sysreg.h>

	.arch	armv8.5-a+memtag

/*
 * multitag_transfer_size - set \reg to the block size that is accessed by the
 * LDGM/STGM instructions.
 */
	.macro	multitag_transfer_size, reg, tmp
	mrs_s	\reg, SYS_GMID_EL1
	ubfx	\reg, \reg, #SYS_GMID_EL1_BS_SHIFT, #SYS_GMID_EL1_BS_SIZE
	mov	\tmp, #4
	lsl	\reg, \tmp, \reg
	.endm

/*
 * Clear the tags in a page
 *   x0 - address of the page to be cleared
 */
SYM_FUNC_START(mte_clear_page_tags)
	multitag_transfer_size x1, x2
1:	stgm	xzr, [x0]
	add	x0, x0, x1
	tst	x0, #(PAGE_SIZE - 1)
	b.ne	1b
	ret
SYM_FUNC_END(mte_clear_page_tags)

/*
 * Copy the tags from the source page to the destination one
 *   x0 - address of the destination page
 *   x1 - address of the source page
 */
SYM_FUNC_START(mte_copy_page_tags)
	mov	x2, x0
	mov	x3, x1
	multitag_transfer_size x5, x6
1:	ldgm	x4, [x3]
	stgm	x4, [x2]
	add	x2, x2, x5
	add	x3, x3, x5
	tst	x2, #(PAGE_SIZE - 1)
	b.ne	1b
	ret
SYM_FUNC_END(mte_copy_page_tags)
