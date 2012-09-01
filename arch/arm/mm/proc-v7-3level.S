/*
 * arch/arm/mm/proc-v7-3level.S
 *
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
 * Copyright (C) 2011 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 *   based on arch/arm/mm/proc-v7-2level.S
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#define TTB_IRGN_NC	(0 << 8)
#define TTB_IRGN_WBWA	(1 << 8)
#define TTB_IRGN_WT	(2 << 8)
#define TTB_IRGN_WB	(3 << 8)
#define TTB_RGN_NC	(0 << 10)
#define TTB_RGN_OC_WBWA	(1 << 10)
#define TTB_RGN_OC_WT	(2 << 10)
#define TTB_RGN_OC_WB	(3 << 10)
#define TTB_S		(3 << 12)
#define TTB_EAE		(1 << 31)

/* PTWs cacheable, inner WB not shareable, outer WB not shareable */
#define TTB_FLAGS_UP	(TTB_IRGN_WB|TTB_RGN_OC_WB)
#define PMD_FLAGS_UP	(PMD_SECT_WB)

/* PTWs cacheable, inner WBWA shareable, outer WBWA not shareable */
#define TTB_FLAGS_SMP	(TTB_IRGN_WBWA|TTB_S|TTB_RGN_OC_WBWA)
#define PMD_FLAGS_SMP	(PMD_SECT_WBWA|PMD_SECT_S)

/*
 * cpu_v7_switch_mm(pgd_phys, tsk)
 *
 * Set the translation table base pointer to be pgd_phys (physical address of
 * the new TTB).
 */
ENTRY(cpu_v7_switch_mm)
#ifdef CONFIG_MMU
	ldr	r1, [r1, #MM_CONTEXT_ID]	@ get mm->context.id
	and	r3, r1, #0xff
	mov	r3, r3, lsl #(48 - 32)		@ ASID
	mcrr	p15, 0, r0, r3, c2		@ set TTB 0
	isb
#endif
	mov	pc, lr
ENDPROC(cpu_v7_switch_mm)

/*
 * cpu_v7_set_pte_ext(ptep, pte)
 *
 * Set a level 2 translation table entry.
 * - ptep - pointer to level 3 translation table entry
 * - pte - PTE value to store (64-bit in r2 and r3)
 */
ENTRY(cpu_v7_set_pte_ext)
#ifdef CONFIG_MMU
	tst	r2, #L_PTE_VALID
	beq	1f
	tst	r3, #1 << (57 - 32)		@ L_PTE_NONE
	bicne	r2, #L_PTE_VALID
	bne	1f
	tst	r3, #1 << (55 - 32)		@ L_PTE_DIRTY
	orreq	r2, #L_PTE_RDONLY
1:	strd	r2, r3, [r0]
	mcr	p15, 0, r0, c7, c10, 1		@ flush_pte
#endif
	mov	pc, lr
ENDPROC(cpu_v7_set_pte_ext)

	/*
	 * Memory region attributes for LPAE (defined in pgtable-3level.h):
	 *
	 *   n = AttrIndx[2:0]
	 *
	 *			n	MAIR
	 *   UNCACHED		000	00000000
	 *   BUFFERABLE		001	01000100
	 *   DEV_WC		001	01000100
	 *   WRITETHROUGH	010	10101010
	 *   WRITEBACK		011	11101110
	 *   DEV_CACHED		011	11101110
	 *   DEV_SHARED		100	00000100
	 *   DEV_NONSHARED	100	00000100
	 *   unused		101
	 *   unused		110
	 *   WRITEALLOC		111	11111111
	 */
.equ	PRRR,	0xeeaa4400			@ MAIR0
.equ	NMRR,	0xff000004			@ MAIR1

	/*
	 * Macro for setting up the TTBRx and TTBCR registers.
	 * - \ttbr1 updated.
	 */
	.macro	v7_ttb_setup, zero, ttbr0, ttbr1, tmp
	ldr	\tmp, =swapper_pg_dir		@ swapper_pg_dir virtual address
	cmp	\ttbr1, \tmp			@ PHYS_OFFSET > PAGE_OFFSET? (branch below)
	mrc	p15, 0, \tmp, c2, c0, 2		@ TTB control register
	orr	\tmp, \tmp, #TTB_EAE
	ALT_SMP(orr	\tmp, \tmp, #TTB_FLAGS_SMP)
	ALT_UP(orr	\tmp, \tmp, #TTB_FLAGS_UP)
	ALT_SMP(orr	\tmp, \tmp, #TTB_FLAGS_SMP << 16)
	ALT_UP(orr	\tmp, \tmp, #TTB_FLAGS_UP << 16)
	/*
	 * TTBR0/TTBR1 split (PAGE_OFFSET):
	 *   0x40000000: T0SZ = 2, T1SZ = 0 (not used)
	 *   0x80000000: T0SZ = 0, T1SZ = 1
	 *   0xc0000000: T0SZ = 0, T1SZ = 2
	 *
	 * Only use this feature if PHYS_OFFSET <= PAGE_OFFSET, otherwise
	 * booting secondary CPUs would end up using TTBR1 for the identity
	 * mapping set up in TTBR0.
	 */
	bhi	9001f				@ PHYS_OFFSET > PAGE_OFFSET?
	orr	\tmp, \tmp, #(((PAGE_OFFSET >> 30) - 1) << 16) @ TTBCR.T1SZ
#if defined CONFIG_VMSPLIT_2G
	/* PAGE_OFFSET == 0x80000000, T1SZ == 1 */
	add	\ttbr1, \ttbr1, #1 << 4		@ skip two L1 entries
#elif defined CONFIG_VMSPLIT_3G
	/* PAGE_OFFSET == 0xc0000000, T1SZ == 2 */
	add	\ttbr1, \ttbr1, #4096 * (1 + 3)	@ only L2 used, skip pgd+3*pmd
#endif
	/* CONFIG_VMSPLIT_1G does not need TTBR1 adjustment */
9001:	mcr	p15, 0, \tmp, c2, c0, 2		@ TTB control register
	mcrr	p15, 1, \ttbr1, \zero, c2	@ load TTBR1
	.endm

	__CPUINIT

	/*
	 *   AT
	 *  TFR   EV X F   IHD LR    S
	 * .EEE ..EE PUI. .TAT 4RVI ZWRS BLDP WCAM
	 * rxxx rrxx xxx0 0101 xxxx xxxx x111 xxxx < forced
	 *   11    0 110    1  0011 1100 .111 1101 < we want
	 */
	.align	2
	.type	v7_crval, #object
v7_crval:
	crval	clear=0x0120c302, mmuset=0x30c23c7d, ucset=0x00c01c7c

	.previous
