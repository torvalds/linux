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
.align 4;
_tlbia:
	addik	r12, r0, 63 /* flush all entries (63 - 3) */
	/* isync */
_tlbia_1:
	mts	rtlbx, r12
	nop
	mts	rtlbhi, r0 /* flush: ensure V is clear */
	nop
	addik	r11, r12, -2
	bneid	r11, _tlbia_1 /* loop for all entries */
	addik	r12, r12, -1
	/* sync */
	rtsd	r15, 8
	nop

/*
 * Flush MMU TLB for a particular address (in r5)
 */
.globl _tlbie;
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

/*
 * Allocate TLB entry for early console
 */
.globl early_console_reg_tlb_alloc;
.align 4;
early_console_reg_tlb_alloc:
	/*
	 * Load a TLB entry for the UART, so that microblaze_progress() can use
	 * the UARTs nice and early.  We use a 4k real==virtual mapping.
	 */
	ori	r4, r0, 63
	mts	rtlbx, r4 /* TLB slot 2 */

	or	r4,r5,r0
	andi	r4,r4,0xfffff000
	ori	r4,r4,(TLB_WR|TLB_I|TLB_M|TLB_G)

	andi	r5,r5,0xfffff000
	ori	r5,r5,(TLB_VALID | TLB_PAGESZ(PAGESZ_4K))

	mts	rtlblo,r4 /* Load the data portion of the entry */
	nop
	mts	rtlbhi,r5 /* Load the tag portion of the entry */
	nop
	rtsd	r15, 8
	nop

/*
 * Copy a whole page (4096 bytes).
 */
#define COPY_16_BYTES		\
	lwi	r7, r6, 0;	\
	lwi	r8, r6, 4;	\
	lwi	r9, r6, 8;	\
	lwi	r10, r6, 12;	\
	swi	r7, r5, 0;	\
	swi	r8, r5, 4;	\
	swi	r9, r5, 8;	\
	swi	r10, r5, 12


/* FIXME DCACHE_LINE_BYTES (CONFIG_XILINX_MICROBLAZE0_DCACHE_LINE_LEN * 4)*/
#define DCACHE_LINE_BYTES (4 * 4)

.globl copy_page;
.align 4;
copy_page:
	ori	r11, r0, (PAGE_SIZE/DCACHE_LINE_BYTES) - 1
_copy_page_loop:
	COPY_16_BYTES
#if DCACHE_LINE_BYTES >= 32
	COPY_16_BYTES
#endif
	addik	r6, r6, DCACHE_LINE_BYTES
	addik	r5, r5, DCACHE_LINE_BYTES
	bneid	r11, _copy_page_loop
	addik	r11, r11, -1
	rtsd	r15, 8
	nop
