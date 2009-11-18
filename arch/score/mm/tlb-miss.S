/*
 * arch/score/mm/tlbex.S
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <asm/asmmacro.h>
#include <asm/pgtable-bits.h>
#include <asm/scoreregs.h>

/*
* After this macro runs, the pte faulted on is
* in register PTE, a ptr into the table in which
* the pte belongs is in PTR.
*/
	.macro	load_pte, pte, ptr
	la	\ptr, pgd_current
	lw	\ptr, [\ptr, 0]
	mfcr	\pte, cr6
	srli	\pte, \pte, 22
	slli	\pte, \pte, 2
	add	\ptr, \ptr, \pte
	lw	\ptr, [\ptr, 0]
	mfcr	\pte, cr6
	srli	\pte, \pte, 10
	andi	\pte, 0xffc
	add	\ptr, \ptr, \pte
	lw	\pte, [\ptr, 0]
	.endm

	.macro	pte_reload, ptr
	lw	\ptr, [\ptr, 0]
	mtcr	\ptr, cr12
	nop
	nop
	nop
	nop
	nop
	.endm

	.macro do_fault, write
	SAVE_ALL
	mfcr	r6, cr6
	mv	r4, r0
	ldi	r5, \write
	la	r8, do_page_fault
	brl	r8
	j	ret_from_exception
	.endm

	.macro	pte_writable, pte, ptr, label
	andi	\pte, 0x280
	cmpi.c	\pte, 0x280
	bne	\label
	lw	\pte, [\ptr, 0]		/*reload PTE*/
	.endm

/*
 * Make PTE writable, update software status bits as well,
 * then store at PTR.
 */
	.macro	pte_makewrite, pte, ptr
	ori	\pte, 0x426
	sw	\pte, [\ptr, 0]
	.endm

	.text
ENTRY(score7_FTLB_refill_Handler)
	la	r31, pgd_current	/* get pgd pointer */
	lw	r31, [r31, 0]		/* get the address of PGD */
	mfcr	r30, cr6
	srli	r30, r30, 22		/* PGDIR_SHIFT = 22*/
	slli	r30, r30, 2
	add	r31, r31, r30
	lw	r31, [r31, 0]		/* get the address of the start address of PTE table */

	mfcr	r30, cr9
	andi	r30, 0xfff 		/* equivalent to get PET index and right shift 2 bits */
	add	r31, r31, r30
	lw	r30, [r31, 0]		/* load pte entry */
	mtcr	r30, cr12
	nop
	nop
	nop
	nop
	nop
	mtrtlb
	nop
	nop
	nop
	nop
	nop
	rte				/* 6 cycles to make sure tlb entry works */

ENTRY(score7_KSEG_refill_Handler)
	la	r31, pgd_current	/* get pgd pointer */
	lw	r31, [r31, 0]		/* get the address of PGD */
	mfcr	r30, cr6
	srli	r30, r30, 22		/* PGDIR_SHIFT = 22 */
	slli	r30, r30, 2
	add	r31, r31, r30
	lw	r31, [r31, 0]		/* get the address of the start address of PTE table */

	mfcr	r30, cr6		/* get Bad VPN */
	srli	r30, r30, 10
	andi	r30, 0xffc		/* PTE VPN mask (bit 11~2) */

	add	r31, r31, r30
	lw	r30, [r31, 0]		/* load pte entry */
	mtcr	r30, cr12
	nop
	nop
	nop
	nop
	nop
	mtrtlb
	nop
	nop
	nop
	nop
	nop
	rte				/* 6 cycles to make sure tlb entry works */

nopage_tlbl:
	do_fault	0		/* Read */

ENTRY(handle_tlb_refill)
	load_pte	r30, r31
	pte_writable	r30, r31, handle_tlb_refill_nopage
	pte_makewrite	r30, r31	/* Access|Modify|Dirty|Valid */
	pte_reload	r31
	mtrtlb
	nop
	nop
	nop
	nop
	nop
	rte
handle_tlb_refill_nopage:
	do_fault	0		/* Read */

ENTRY(handle_tlb_invaild)
	load_pte	r30, r31
	stlb				/* find faulting entry */
	pte_writable	r30, r31, handle_tlb_invaild_nopage
	pte_makewrite	r30, r31	/* Access|Modify|Dirty|Valid */
	pte_reload	r31
	mtptlb
	nop
	nop
	nop
	nop
	nop
	rte
handle_tlb_invaild_nopage:
	do_fault	0		/* Read */

ENTRY(handle_mod)
	load_pte	r30, r31
	stlb				/* find faulting entry */
	andi	r30, _PAGE_WRITE	/* Writable? */
	cmpz.c	r30
	beq	nowrite_mod
	lw	r30, [r31, 0]		/* reload into r30 */

	/* Present and writable bits set, set accessed and dirty bits. */
	pte_makewrite	r30, r31

	/* Now reload the entry into the tlb. */
	pte_reload	r31
	mtptlb
	nop
	nop
	nop
	nop
	nop
	rte

nowrite_mod:
	do_fault	1	/* Write */
