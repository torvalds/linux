/*
 * Miscellaneous low-level MMU functions.
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2007 Xilinx, Inc.  All rights reserved.
 *
 * Derived from arch/ppc/kernel/misc.S
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <asm/unistd.h>
#include <linux/errno.h>
#include <asm/mmu.h>
#include <asm/page.h>

	.text
/*
 * Flush MMU TLB
 *
 * We avoid flushing the pinned 0, 1 and possibly 2 entries.
 */
.globl _tlbia;
.type  _tlbia, @function
.align 4;
_tlbia:
	lwi	r12, r0, tlb_skip;
	/* isync */
_tlbia_1:
	mts	rtlbx, r12
	nop
	mts	rtlbhi, r0 /* flush: ensure V is clear */
	nop
	rsubi	r11, r12, MICROBLAZE_TLB_SIZE - 1
	bneid	r11, _tlbia_1 /* loop for all entries */
	addik	r12, r12, 1
	/* sync */
	rtsd	r15, 8
	nop
	.size  _tlbia, . - _tlbia

/*
 * Flush MMU TLB for a particular address (in r5)
 */
.globl _tlbie;
.type  _tlbie, @function
.align 4;
_tlbie:
	mts	rtlbsx, r5 /* look up the address in TLB */
	nop
	mfs	r12, rtlbx /* Retrieve index */
	nop
	blti	r12, _tlbie_1 /* Check if found */
	mts	rtlbhi, r0 /* flush: ensure V is clear */
	nop
_tlbie_1:
	rtsd	r15, 8
	nop

	.size  _tlbie, . - _tlbie
