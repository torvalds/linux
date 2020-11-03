/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/mm/proc.S
 *
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/pgtable.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/asm_pointer_auth.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>
#include <asm/smp.h>
#include <asm/sysreg.h>

#ifdef CONFIG_ARM64_64K_PAGES
#define TCR_TG_FLAGS	TCR_TG0_64K | TCR_TG1_64K
#elif defined(CONFIG_ARM64_16K_PAGES)
#define TCR_TG_FLAGS	TCR_TG0_16K | TCR_TG1_16K
#else /* CONFIG_ARM64_4K_PAGES */
#define TCR_TG_FLAGS	TCR_TG0_4K | TCR_TG1_4K
#endif

#ifdef CONFIG_RANDOMIZE_BASE
#define TCR_KASLR_FLAGS	TCR_NFD1
#else
#define TCR_KASLR_FLAGS	0
#endif

#define TCR_SMP_FLAGS	TCR_SHARED

/* PTWs cacheable, inner/outer WBWA */
#define TCR_CACHE_FLAGS	TCR_IRGN_WBWA | TCR_ORGN_WBWA

#ifdef CONFIG_KASAN_SW_TAGS
#define TCR_KASAN_FLAGS TCR_TBI1
#else
#define TCR_KASAN_FLAGS 0
#endif

/*
 * Default MAIR_EL1. MT_NORMAL_TAGGED is initially mapped as Normal memory and
 * changed during __cpu_setup to Normal Tagged if the system supports MTE.
 */
#define MAIR_EL1_SET							\
	(MAIR_ATTRIDX(MAIR_ATTR_DEVICE_nGnRnE, MT_DEVICE_nGnRnE) |	\
	 MAIR_ATTRIDX(MAIR_ATTR_DEVICE_nGnRE, MT_DEVICE_nGnRE) |	\
	 MAIR_ATTRIDX(MAIR_ATTR_DEVICE_GRE, MT_DEVICE_GRE) |		\
	 MAIR_ATTRIDX(MAIR_ATTR_NORMAL_NC, MT_NORMAL_NC) |		\
	 MAIR_ATTRIDX(MAIR_ATTR_NORMAL, MT_NORMAL) |			\
	 MAIR_ATTRIDX(MAIR_ATTR_NORMAL_WT, MT_NORMAL_WT) |		\
	 MAIR_ATTRIDX(MAIR_ATTR_NORMAL, MT_NORMAL_TAGGED))

#ifdef CONFIG_CPU_PM
/**
 * cpu_do_suspend - save CPU registers context
 *
 * x0: virtual address of context pointer
 *
 * This must be kept in sync with struct cpu_suspend_ctx in <asm/suspend.h>.
 */
SYM_FUNC_START(cpu_do_suspend)
	mrs	x2, tpidr_el0
	mrs	x3, tpidrro_el0
	mrs	x4, contextidr_el1
	mrs	x5, osdlr_el1
	mrs	x6, cpacr_el1
	mrs	x7, tcr_el1
	mrs	x8, vbar_el1
	mrs	x9, mdscr_el1
	mrs	x10, oslsr_el1
	mrs	x11, sctlr_el1
alternative_if_not ARM64_HAS_VIRT_HOST_EXTN
	mrs	x12, tpidr_el1
alternative_else
	mrs	x12, tpidr_el2
alternative_endif
	mrs	x13, sp_el0
	stp	x2, x3, [x0]
	stp	x4, x5, [x0, #16]
	stp	x6, x7, [x0, #32]
	stp	x8, x9, [x0, #48]
	stp	x10, x11, [x0, #64]
	stp	x12, x13, [x0, #80]
	/*
	 * Save x18 as it may be used as a platform register, e.g. by shadow
	 * call stack.
	 */
	str	x18, [x0, #96]
	ret
SYM_FUNC_END(cpu_do_suspend)

/**
 * cpu_do_resume - restore CPU register context
 *
 * x0: Address of context pointer
 */
	.pushsection ".idmap.text", "awx"
SYM_FUNC_START(cpu_do_resume)
	ldp	x2, x3, [x0]
	ldp	x4, x5, [x0, #16]
	ldp	x6, x8, [x0, #32]
	ldp	x9, x10, [x0, #48]
	ldp	x11, x12, [x0, #64]
	ldp	x13, x14, [x0, #80]
	/*
	 * Restore x18, as it may be used as a platform register, and clear
	 * the buffer to minimize the risk of exposure when used for shadow
	 * call stack.
	 */
	ldr	x18, [x0, #96]
	str	xzr, [x0, #96]
	msr	tpidr_el0, x2
	msr	tpidrro_el0, x3
	msr	contextidr_el1, x4
	msr	cpacr_el1, x6

	/* Don't change t0sz here, mask those bits when restoring */
	mrs	x7, tcr_el1
	bfi	x8, x7, TCR_T0SZ_OFFSET, TCR_TxSZ_WIDTH

	msr	tcr_el1, x8
	msr	vbar_el1, x9

	/*
	 * __cpu_setup() cleared MDSCR_EL1.MDE and friends, before unmasking
	 * debug exceptions. By restoring MDSCR_EL1 here, we may take a debug
	 * exception. Mask them until local_daif_restore() in cpu_suspend()
	 * resets them.
	 */
	disable_daif
	msr	mdscr_el1, x10

	msr	sctlr_el1, x12
alternative_if_not ARM64_HAS_VIRT_HOST_EXTN
	msr	tpidr_el1, x13
alternative_else
	msr	tpidr_el2, x13
alternative_endif
	msr	sp_el0, x14
	/*
	 * Restore oslsr_el1 by writing oslar_el1
	 */
	msr	osdlr_el1, x5
	ubfx	x11, x11, #1, #1
	msr	oslar_el1, x11
	reset_pmuserenr_el0 x0			// Disable PMU access from EL0
	reset_amuserenr_el0 x0			// Disable AMU access from EL0

alternative_if ARM64_HAS_RAS_EXTN
	msr_s	SYS_DISR_EL1, xzr
alternative_else_nop_endif

	ptrauth_keys_install_kernel_nosync x14, x1, x2, x3
	isb
	ret
SYM_FUNC_END(cpu_do_resume)
	.popsection
#endif

	.pushsection ".idmap.text", "awx"

.macro	__idmap_cpu_set_reserved_ttbr1, tmp1, tmp2
	adrp	\tmp1, reserved_pg_dir
	phys_to_ttbr \tmp2, \tmp1
	offset_ttbr1 \tmp2, \tmp1
	msr	ttbr1_el1, \tmp2
	isb
	tlbi	vmalle1
	dsb	nsh
	isb
.endm

/*
 * void idmap_cpu_replace_ttbr1(phys_addr_t ttbr1)
 *
 * This is the low-level counterpart to cpu_replace_ttbr1, and should not be
 * called by anything else. It can only be executed from a TTBR0 mapping.
 */
SYM_FUNC_START(idmap_cpu_replace_ttbr1)
	save_and_disable_daif flags=x2

	__idmap_cpu_set_reserved_ttbr1 x1, x3

	offset_ttbr1 x0, x3
	msr	ttbr1_el1, x0
	isb

	restore_daif x2

	ret
SYM_FUNC_END(idmap_cpu_replace_ttbr1)
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
	str	\type, [cur_\()\type\()p]	// Update the entry and ensure
	dmb	sy				// that it is visible to all
	dc	civac, cur_\()\type\()p		// CPUs.
	.endm

/*
 * void __kpti_install_ng_mappings(int cpu, int num_cpus, phys_addr_t swapper)
 *
 * Called exactly once from stop_machine context by each CPU found during boot.
 */
__idmap_kpti_flag:
	.long	1
SYM_FUNC_START(idmap_kpti_install_ng_mappings)
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
	restore_ttbr1	swapper_ttb
	adr	flag_ptr, __idmap_kpti_flag

	cbnz	cpu, __idmap_kpti_secondary

	/* We're the boot CPU. Wait for the others to catch up */
	sevl
1:	wfe
	ldaxr	w17, [flag_ptr]
	eor	w17, w17, num_cpus
	cbnz	w17, 1b

	/* We need to walk swapper, so turn off the MMU. */
	pre_disable_mmu_workaround
	mrs	x17, sctlr_el1
	bic	x17, x17, #SCTLR_ELx_M
	msr	sctlr_el1, x17
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
	mrs	x17, sctlr_el1
	orr	x17, x17, #SCTLR_ELx_M
	msr	sctlr_el1, x17
	isb

	/*
	 * Invalidate the local I-cache so that any instructions fetched
	 * speculatively from the PoC are discarded, since they may have
	 * been dynamically patched at the PoU.
	 */
	ic	iallu
	dsb	nsh
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

	.unreq	cpu
	.unreq	num_cpus
	.unreq	swapper_pa
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

	/* Secondary CPUs end up here */
__idmap_kpti_secondary:
	/* Uninstall swapper before surgery begins */
	__idmap_cpu_set_reserved_ttbr1 x16, x17

	/* Increment the flag to let the boot CPU we're ready */
1:	ldxr	w16, [flag_ptr]
	add	w16, w16, #1
	stxr	w17, w16, [flag_ptr]
	cbnz	w17, 1b

	/* Wait for the boot CPU to finish messing around with swapper */
	sevl
1:	wfe
	ldxr	w16, [flag_ptr]
	cbnz	w16, 1b

	/* All done, act like nothing happened */
	offset_ttbr1 swapper_ttb, x16
	msr	ttbr1_el1, swapper_ttb
	isb
	ret

	.unreq	swapper_ttb
	.unreq	flag_ptr
SYM_FUNC_END(idmap_kpti_install_ng_mappings)
	.popsection
#endif

/*
 *	__cpu_setup
 *
 *	Initialise the processor for turning the MMU on.
 *
 * Output:
 *	Return in x0 the value of the SCTLR_EL1 register.
 */
	.pushsection ".idmap.text", "awx"
SYM_FUNC_START(__cpu_setup)
	tlbi	vmalle1				// Invalidate local TLB
	dsb	nsh

	mov	x1, #3 << 20
	msr	cpacr_el1, x1			// Enable FP/ASIMD
	mov	x1, #1 << 12			// Reset mdscr_el1 and disable
	msr	mdscr_el1, x1			// access to the DCC from EL0
	isb					// Unmask debug exceptions now,
	enable_dbg				// since this is per-cpu
	reset_pmuserenr_el0 x1			// Disable PMU access from EL0
	reset_amuserenr_el0 x1			// Disable AMU access from EL0

	/*
	 * Memory region attributes
	 */
	mov_q	x5, MAIR_EL1_SET
#ifdef CONFIG_ARM64_MTE
	/*
	 * Update MAIR_EL1, GCR_EL1 and TFSR*_EL1 if MTE is supported
	 * (ID_AA64PFR1_EL1[11:8] > 1).
	 */
	mrs	x10, ID_AA64PFR1_EL1
	ubfx	x10, x10, #ID_AA64PFR1_MTE_SHIFT, #4
	cmp	x10, #ID_AA64PFR1_MTE
	b.lt	1f

	/* Normal Tagged memory type at the corresponding MAIR index */
	mov	x10, #MAIR_ATTR_NORMAL_TAGGED
	bfi	x5, x10, #(8 *  MT_NORMAL_TAGGED), #8

	/* initialize GCR_EL1: all non-zero tags excluded by default */
	mov	x10, #(SYS_GCR_EL1_RRND | SYS_GCR_EL1_EXCL_MASK)
	msr_s	SYS_GCR_EL1, x10

	/*
	 * If GCR_EL1.RRND=1 is implemented the same way as RRND=0, then
	 * RGSR_EL1.SEED must be non-zero for IRG to produce
	 * pseudorandom numbers. As RGSR_EL1 is UNKNOWN out of reset, we
	 * must initialize it.
	 */
	mrs	x10, CNTVCT_EL0
	ands	x10, x10, #SYS_RGSR_EL1_SEED_MASK
	csinc	x10, x10, xzr, ne
	lsl	x10, x10, #SYS_RGSR_EL1_SEED_SHIFT
	msr_s	SYS_RGSR_EL1, x10

	/* clear any pending tag check faults in TFSR*_EL1 */
	msr_s	SYS_TFSR_EL1, xzr
	msr_s	SYS_TFSRE0_EL1, xzr
1:
#endif
	msr	mair_el1, x5
	/*
	 * Set/prepare TCR and TTBR. We use 512GB (39-bit) address range for
	 * both user and kernel.
	 */
	mov_q	x10, TCR_TxSZ(VA_BITS) | TCR_CACHE_FLAGS | TCR_SMP_FLAGS | \
			TCR_TG_FLAGS | TCR_KASLR_FLAGS | TCR_ASID16 | \
			TCR_TBI0 | TCR_A1 | TCR_KASAN_FLAGS
	tcr_clear_errata_bits x10, x9, x5

#ifdef CONFIG_ARM64_VA_BITS_52
	ldr_l		x9, vabits_actual
	sub		x9, xzr, x9
	add		x9, x9, #64
	tcr_set_t1sz	x10, x9
#else
	ldr_l		x9, idmap_t0sz
#endif
	tcr_set_t0sz	x10, x9

	/*
	 * Set the IPS bits in TCR_EL1.
	 */
	tcr_compute_pa_size x10, #TCR_IPS_SHIFT, x5, x6
#ifdef CONFIG_ARM64_HW_AFDBM
	/*
	 * Enable hardware update of the Access Flags bit.
	 * Hardware dirty bit management is enabled later,
	 * via capabilities.
	 */
	mrs	x9, ID_AA64MMFR1_EL1
	and	x9, x9, #0xf
	cbz	x9, 1f
	orr	x10, x10, #TCR_HA		// hardware Access flag update
1:
#endif	/* CONFIG_ARM64_HW_AFDBM */
	msr	tcr_el1, x10
	/*
	 * Prepare SCTLR
	 */
	mov_q	x0, SCTLR_EL1_SET
	ret					// return to head.S
SYM_FUNC_END(__cpu_setup)
