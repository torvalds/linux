/*
 * Based on arch/arm/mm/proc.S
 *
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <asm/pgtable.h>
#include <asm/pgtable-hwdef.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>

#ifdef CONFIG_ARM64_64K_PAGES
#define TCR_TG_FLAGS	TCR_TG0_64K | TCR_TG1_64K
#elif defined(CONFIG_ARM64_16K_PAGES)
#define TCR_TG_FLAGS	TCR_TG0_16K | TCR_TG1_16K
#else /* CONFIG_ARM64_4K_PAGES */
#define TCR_TG_FLAGS	TCR_TG0_4K | TCR_TG1_4K
#endif

#define TCR_SMP_FLAGS	TCR_SHARED

/* PTWs cacheable, inner/outer WBWA */
#define TCR_CACHE_FLAGS	TCR_IRGN_WBWA | TCR_ORGN_WBWA

#define MAIR(attr, mt)	((attr) << ((mt) * 8))

/*
 *	cpu_do_idle()
 *
 *	Idle the processor (wait for interrupt).
 */
ENTRY(cpu_do_idle)
	dsb	sy				// WFI may enter a low-power mode
	wfi
	ret
ENDPROC(cpu_do_idle)

#ifdef CONFIG_CPU_PM
/**
 * cpu_do_suspend - save CPU registers context
 *
 * x0: virtual address of context pointer
 */
ENTRY(cpu_do_suspend)
	mrs	x2, tpidr_el0
	mrs	x3, tpidrro_el0
	mrs	x4, contextidr_el1
	mrs	x5, cpacr_el1
	mrs	x6, tcr_el1
	mrs	x7, vbar_el1
	mrs	x8, mdscr_el1
	mrs	x9, oslsr_el1
	mrs	x10, sctlr_el1
	mrs	x11, tpidr_el1
	mrs	x12, sp_el0
	stp	x2, x3, [x0]
	stp	x4, xzr, [x0, #16]
	stp	x5, x6, [x0, #32]
	stp	x7, x8, [x0, #48]
	stp	x9, x10, [x0, #64]
	stp	x11, x12, [x0, #80]
	ret
ENDPROC(cpu_do_suspend)

/**
 * cpu_do_resume - restore CPU register context
 *
 * x0: Address of context pointer
 */
	.pushsection ".idmap.text", "awx"
ENTRY(cpu_do_resume)
	ldp	x2, x3, [x0]
	ldp	x4, x5, [x0, #16]
	ldp	x6, x8, [x0, #32]
	ldp	x9, x10, [x0, #48]
	ldp	x11, x12, [x0, #64]
	ldp	x13, x14, [x0, #80]
	msr	tpidr_el0, x2
	msr	tpidrro_el0, x3
	msr	contextidr_el1, x4
	msr	cpacr_el1, x6

	/* Don't change t0sz here, mask those bits when restoring */
	mrs	x5, tcr_el1
	bfi	x8, x5, TCR_T0SZ_OFFSET, TCR_TxSZ_WIDTH

	msr	tcr_el1, x8
	msr	vbar_el1, x9

	/*
	 * __cpu_setup() cleared MDSCR_EL1.MDE and friends, before unmasking
	 * debug exceptions. By restoring MDSCR_EL1 here, we may take a debug
	 * exception. Mask them until local_dbg_restore() in cpu_suspend()
	 * resets them.
	 */
	disable_dbg
	msr	mdscr_el1, x10

	msr	sctlr_el1, x12
	msr	tpidr_el1, x13
	msr	sp_el0, x14
	/*
	 * Restore oslsr_el1 by writing oslar_el1
	 */
	ubfx	x11, x11, #1, #1
	msr	oslar_el1, x11
	reset_pmuserenr_el0 x0			// Disable PMU access from EL0
	isb
	ret
ENDPROC(cpu_do_resume)
	.popsection
#endif

/*
 *	cpu_do_switch_mm(pgd_phys, tsk)
 *
 *	Set the translation table base pointer to be pgd_phys.
 *
 *	- pgd_phys - physical address of new TTB
 */
ENTRY(cpu_do_switch_mm)
	mrs	x2, ttbr1_el1
	mmid	x1, x1				// get mm->context.id
#ifdef CONFIG_ARM64_SW_TTBR0_PAN
	bfi	x0, x1, #48, #16		// set the ASID field in TTBR0
#endif
	bfi	x2, x1, #48, #16		// set the ASID
	msr	ttbr1_el1, x2			// in TTBR1 (since TCR.A1 is set)
	isb
	msr	ttbr0_el1, x0			// now update TTBR0
	isb
	b	post_ttbr_update_workaround	// Back to C code...
ENDPROC(cpu_do_switch_mm)

	.pushsection ".idmap.text", "awx"

.macro	__idmap_cpu_set_reserved_ttbr1, tmp1, tmp2
	adrp	\tmp1, empty_zero_page
	msr	ttbr1_el1, \tmp1
	isb
	tlbi	vmalle1
	dsb	nsh
	isb
.endm

/*
 * void idmap_cpu_replace_ttbr1(phys_addr_t new_pgd)
 *
 * This is the low-level counterpart to cpu_replace_ttbr1, and should not be
 * called by anything else. It can only be executed from a TTBR0 mapping.
 */
ENTRY(idmap_cpu_replace_ttbr1)
	mrs	x2, daif
	msr	daifset, #0xf

	__idmap_cpu_set_reserved_ttbr1 x1, x3

	msr	ttbr1_el1, x0
	isb

	msr	daif, x2

	ret
ENDPROC(idmap_cpu_replace_ttbr1)
	.popsection

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
	.pushsection ".idmap.text", "awx"

	.macro	__idmap_kpti_get_pgtable_ent, type
	dc	cvac, cur_\()\type\()p		// Ensure any existing dirty
	dmb	sy				// lines are written back before
	ldr	\type, [cur_\()\type\()p]	// loading the entry
	tbz	\type, #0, skip_\()\type	// Skip invalid and
	tbnz	\type, #11, skip_\()\type	// non-global entries
	.endm

	.macro __idmap_kpti_put_pgtable_ent_ng, type
	orr	\type, \type, #PTE_NG		// Same bit for blocks and pages
	str	\type, [cur_\()\type\()p]	// Update the entry and ensure it
	dc	civac, cur_\()\type\()p		// is visible to all CPUs.
	.endm

/*
 * void __kpti_install_ng_mappings(int cpu, int num_cpus, phys_addr_t swapper)
 *
 * Called exactly once from stop_machine context by each CPU found during boot.
 */
__idmap_kpti_flag:
	.long	1
ENTRY(idmap_kpti_install_ng_mappings)
	cpu		.req	w0
	num_cpus	.req	w1
	swapper_pa	.req	x2
	swapper_ttb	.req	x3
	flag_ptr	.req	x4
	cur_pgdp	.req	x5
	end_pgdp	.req	x6
	pgd		.req	x7
	cur_pudp	.req	x8
	end_pudp	.req	x9
	pud		.req	x10
	cur_pmdp	.req	x11
	end_pmdp	.req	x12
	pmd		.req	x13
	cur_ptep	.req	x14
	end_ptep	.req	x15
	pte		.req	x16

	mrs	swapper_ttb, ttbr1_el1
	adr	flag_ptr, __idmap_kpti_flag

	cbnz	cpu, __idmap_kpti_secondary

	/* We're the boot CPU. Wait for the others to catch up */
	sevl
1:	wfe
	ldaxr	w18, [flag_ptr]
	eor	w18, w18, num_cpus
	cbnz	w18, 1b

	/* We need to walk swapper, so turn off the MMU. */
	mrs	x18, sctlr_el1
	bic	x18, x18, #SCTLR_ELx_M
	msr	sctlr_el1, x18
	isb

	/* Everybody is enjoying the idmap, so we can rewrite swapper. */
	/* PGD */
	mov	cur_pgdp, swapper_pa
	add	end_pgdp, cur_pgdp, #(PTRS_PER_PGD * 8)
do_pgd:	__idmap_kpti_get_pgtable_ent	pgd
	tbnz	pgd, #1, walk_puds
next_pgd:
	__idmap_kpti_put_pgtable_ent_ng	pgd
skip_pgd:
	add	cur_pgdp, cur_pgdp, #8
	cmp	cur_pgdp, end_pgdp
	b.ne	do_pgd

	/* Publish the updated tables and nuke all the TLBs */
	dsb	sy
	tlbi	vmalle1is
	dsb	ish
	isb

	/* We're done: fire up the MMU again */
	mrs	x18, sctlr_el1
	orr	x18, x18, #SCTLR_ELx_M
	msr	sctlr_el1, x18
	isb

	/* Set the flag to zero to indicate that we're all done */
	str	wzr, [flag_ptr]
	ret

	/* PUD */
walk_puds:
	.if CONFIG_PGTABLE_LEVELS > 3
	pte_to_phys	cur_pudp, pgd
	add	end_pudp, cur_pudp, #(PTRS_PER_PUD * 8)
do_pud:	__idmap_kpti_get_pgtable_ent	pud
	tbnz	pud, #1, walk_pmds
next_pud:
	__idmap_kpti_put_pgtable_ent_ng	pud
skip_pud:
	add	cur_pudp, cur_pudp, 8
	cmp	cur_pudp, end_pudp
	b.ne	do_pud
	b	next_pgd
	.else /* CONFIG_PGTABLE_LEVELS <= 3 */
	mov	pud, pgd
	b	walk_pmds
next_pud:
	b	next_pgd
	.endif

	/* PMD */
walk_pmds:
	.if CONFIG_PGTABLE_LEVELS > 2
	pte_to_phys	cur_pmdp, pud
	add	end_pmdp, cur_pmdp, #(PTRS_PER_PMD * 8)
do_pmd:	__idmap_kpti_get_pgtable_ent	pmd
	tbnz	pmd, #1, walk_ptes
next_pmd:
	__idmap_kpti_put_pgtable_ent_ng	pmd
skip_pmd:
	add	cur_pmdp, cur_pmdp, #8
	cmp	cur_pmdp, end_pmdp
	b.ne	do_pmd
	b	next_pud
	.else /* CONFIG_PGTABLE_LEVELS <= 2 */
	mov	pmd, pud
	b	walk_ptes
next_pmd:
	b	next_pud
	.endif

	/* PTE */
walk_ptes:
	pte_to_phys	cur_ptep, pmd
	add	end_ptep, cur_ptep, #(PTRS_PER_PTE * 8)
do_pte:	__idmap_kpti_get_pgtable_ent	pte
	__idmap_kpti_put_pgtable_ent_ng	pte
skip_pte:
	add	cur_ptep, cur_ptep, #8
	cmp	cur_ptep, end_ptep
	b.ne	do_pte
	b	next_pmd

	/* Secondary CPUs end up here */
__idmap_kpti_secondary:
	/* Uninstall swapper before surgery begins */
	__idmap_cpu_set_reserved_ttbr1 x18, x17

	/* Increment the flag to let the boot CPU we're ready */
1:	ldxr	w18, [flag_ptr]
	add	w18, w18, #1
	stxr	w17, w18, [flag_ptr]
	cbnz	w17, 1b

	/* Wait for the boot CPU to finish messing around with swapper */
	sevl
1:	wfe
	ldxr	w18, [flag_ptr]
	cbnz	w18, 1b

	/* All done, act like nothing happened */
	msr	ttbr1_el1, swapper_ttb
	isb
	ret

	.unreq	cpu
	.unreq	num_cpus
	.unreq	swapper_pa
	.unreq	swapper_ttb
	.unreq	flag_ptr
	.unreq	cur_pgdp
	.unreq	end_pgdp
	.unreq	pgd
	.unreq	cur_pudp
	.unreq	end_pudp
	.unreq	pud
	.unreq	cur_pmdp
	.unreq	end_pmdp
	.unreq	pmd
	.unreq	cur_ptep
	.unreq	end_ptep
	.unreq	pte
ENDPROC(idmap_kpti_install_ng_mappings)
	.popsection
#endif

/*
 *	__cpu_setup
 *
 *	Initialise the processor for turning the MMU on.  Return in x0 the
 *	value of the SCTLR_EL1 register.
 */
	.pushsection ".idmap.text", "awx"
ENTRY(__cpu_setup)
	tlbi	vmalle1				// Invalidate local TLB
	dsb	nsh

	mov	x0, #3 << 20
	msr	cpacr_el1, x0			// Enable FP/ASIMD
	mov	x0, #1 << 12			// Reset mdscr_el1 and disable
	msr	mdscr_el1, x0			// access to the DCC from EL0
	isb					// Unmask debug exceptions now,
	enable_dbg				// since this is per-cpu
	reset_pmuserenr_el0 x0			// Disable PMU access from EL0
	/*
	 * Memory region attributes for LPAE:
	 *
	 *   n = AttrIndx[2:0]
	 *			n	MAIR
	 *   DEVICE_nGnRnE	000	00000000
	 *   DEVICE_nGnRE	001	00000100
	 *   DEVICE_GRE		010	00001100
	 *   NORMAL_NC		011	01000100
	 *   NORMAL		100	11111111
	 *   NORMAL_WT		101	10111011
	 */
	ldr	x5, =MAIR(0x00, MT_DEVICE_nGnRnE) | \
		     MAIR(0x04, MT_DEVICE_nGnRE) | \
		     MAIR(0x0c, MT_DEVICE_GRE) | \
		     MAIR(0x44, MT_NORMAL_NC) | \
		     MAIR(0xff, MT_NORMAL) | \
		     MAIR(0xbb, MT_NORMAL_WT)
	msr	mair_el1, x5
	/*
	 * Prepare SCTLR
	 */
	adr	x5, crval
	ldp	w5, w6, [x5]
	mrs	x0, sctlr_el1
	bic	x0, x0, x5			// clear bits
	orr	x0, x0, x6			// set bits
	/*
	 * Set/prepare TCR and TTBR. We use 512GB (39-bit) address range for
	 * both user and kernel.
	 */
	ldr	x10, =TCR_TxSZ(VA_BITS) | TCR_CACHE_FLAGS | TCR_SMP_FLAGS | \
			TCR_TG_FLAGS | TCR_ASID16 | TCR_TBI0 | TCR_A1
	tcr_set_idmap_t0sz	x10, x9

	/*
	 * Read the PARange bits from ID_AA64MMFR0_EL1 and set the IPS bits in
	 * TCR_EL1.
	 */
	mrs	x9, ID_AA64MMFR0_EL1
	bfi	x10, x9, #32, #3
#ifdef CONFIG_ARM64_HW_AFDBM
	/*
	 * Hardware update of the Access and Dirty bits.
	 */
	mrs	x9, ID_AA64MMFR1_EL1
	and	x9, x9, #0xf
	cbz	x9, 2f
	cmp	x9, #2
	b.lt	1f
#ifdef CONFIG_ARM64_ERRATUM_1024718
	/* Disable hardware DBM on Cortex-A55 r0p0, r0p1 & r1p0 */
	cpu_midr_match MIDR_CORTEX_A55, MIDR_CPU_VAR_REV(0, 0), MIDR_CPU_VAR_REV(1, 0), x1, x2, x3, x4
	cbnz	x1, 1f
#endif
	orr	x10, x10, #TCR_HD		// hardware Dirty flag update
1:	orr	x10, x10, #TCR_HA		// hardware Access flag update
2:
#endif	/* CONFIG_ARM64_HW_AFDBM */
	msr	tcr_el1, x10
	ret					// return to head.S
ENDPROC(__cpu_setup)

	/*
	 * We set the desired value explicitly, including those of the
	 * reserved bits. The values of bits EE & E0E were set early in
	 * el2_setup, which are left untouched below.
	 *
	 *                 n n            T
	 *       U E      WT T UD     US IHBS
	 *       CE0      XWHW CZ     ME TEEA S
	 * .... .IEE .... NEAI TE.I ..AD DEN0 ACAM
	 * 0011 0... 1101 ..0. ..0. 10.. .0.. .... < hardware reserved
	 * .... .1.. .... 01.1 11.1 ..01 0.01 1101 < software settings
	 */
	.type	crval, #object
crval:
	.word	0xfcffffff			// clear
	.word	0x34d5d91d			// set
	.popsection
