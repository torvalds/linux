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
#include <asm/assembler.h>

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

#ifndef __ARMEB__
#  define rpgdl	r0
#  define rpgdh	r1
#else
#  define rpgdl	r1
#  define rpgdh	r0
#endif

/*
 * cpu_v7_switch_mm(pgd_phys, tsk)
 *
 * Set the translation table base pointer to be pgd_phys (physical address of
 * the new TTB).
 */
ENTRY(cpu_v7_switch_mm)
#ifdef CONFIG_MMU
	mmid	r2, r2
	asid	r2, r2
	orr	rpgdh, rpgdh, r2, lsl #(48 - 32)	@ upper 32-bits of pgd
	mcrr	p15, 0, rpgdl, rpgdh, c2		@ set TTB 0
	isb
#endif
	ret	lr
ENDPROC(cpu_v7_switch_mm)

#ifdef __ARMEB__
#define rl r3
#define rh r2
#else
#define rl r2
#define rh r3
#endif

/*
 * cpu_v7_set_pte_ext(ptep, pte)
 *
 * Set a level 2 translation table entry.
 * - ptep - pointer to level 3 translation table entry
 * - pte - PTE value to store (64-bit in r2 and r3)
 */
ENTRY(cpu_v7_set_pte_ext)
#ifdef CONFIG_MMU
	tst	rl, #L_PTE_VALID
	beq	1f
	tst	rh, #1 << (57 - 32)		@ L_PTE_NONE
	bicne	rl, #L_PTE_VALID
	bne	1f

	eor	ip, rh, #1 << (55 - 32)	@ toggle L_PTE_DIRTY in temp reg to
					@ test for !L_PTE_DIRTY || L_PTE_RDONLY
	tst	ip, #1 << (55 - 32) | 1 << (58 - 32)
	orrne	rl, #PTE_AP2
	biceq	rl, #PTE_AP2

1:	strd	r2, r3, [r0]
	ALT_SMP(W(nop))
	ALT_UP (mcr	p15, 0, r0, c7, c10, 1)		@ flush_pte
#endif
	ret	lr
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
	mov	\tmp, \tmp, lsr #ARCH_PGD_SHIFT
	cmp	\ttbr1, \tmp			@ PHYS_OFFSET > PAGE_OFFSET?
	mrc	p15, 0, \tmp, c2, c0, 2		@ TTB control register
	orr	\tmp, \tmp, #TTB_EAE
	ALT_SMP(orr	\tmp, \tmp, #TTB_FLAGS_SMP)
	ALT_UP(orr	\tmp, \tmp, #TTB_FLAGS_UP)
	ALT_SMP(orr	\tmp, \tmp, #TTB_FLAGS_SMP << 16)
	ALT_UP(orr	\tmp, \tmp, #TTB_FLAGS_UP << 16)
	/*
	 * Only use split TTBRs if PHYS_OFFSET <= PAGE_OFFSET (cmp above),
	 * otherwise booting secondary CPUs would end up using TTBR1 for the
	 * identity mapping set up in TTBR0.
	 */
	orrls	\tmp, \tmp, #TTBR1_SIZE				@ TTBCR.T1SZ
	mcr	p15, 0, \tmp, c2, c0, 2				@ TTBCR
	mov	\tmp, \ttbr1, lsr #(32 - ARCH_PGD_SHIFT)	@ upper bits
	mov	\ttbr1, \ttbr1, lsl #ARCH_PGD_SHIFT		@ lower bits
	addls	\ttbr1, \ttbr1, #TTBR1_OFFSET
	mcrr	p15, 1, \ttbr1, \tmp, c2			@ load TTBR1
	mov	\tmp, \ttbr0, lsr #(32 - ARCH_PGD_SHIFT)	@ upper bits
	mov	\ttbr0, \ttbr0, lsl #ARCH_PGD_SHIFT		@ lower bits
	mcrr	p15, 0, \ttbr0, \tmp, c2			@ load TTBR0
	.endm

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
