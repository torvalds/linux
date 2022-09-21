/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <asm/asm.h>
#include <asm/export.h>
#include <asm/loongarch.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.macro tlb_do_page_fault, write
	SYM_FUNC_START(tlb_do_page_fault_\write)
	SAVE_ALL
	csrrd	a2, LOONGARCH_CSR_BADV
	move	a0, sp
	REG_S	a2, sp, PT_BVADDR
	li.w	a1, \write
	la.abs	t0, do_page_fault
	jirl	ra, t0, 0
	RESTORE_ALL_AND_RET
	SYM_FUNC_END(tlb_do_page_fault_\write)
	.endm

	tlb_do_page_fault 0
	tlb_do_page_fault 1

SYM_FUNC_START(handle_tlb_protect)
	BACKUP_T0T1
	SAVE_ALL
	move	a0, sp
	move	a1, zero
	csrrd	a2, LOONGARCH_CSR_BADV
	REG_S	a2, sp, PT_BVADDR
	la.abs	t0, do_page_fault
	jirl	ra, t0, 0
	RESTORE_ALL_AND_RET
SYM_FUNC_END(handle_tlb_protect)

SYM_FUNC_START(handle_tlb_load)
	csrwr	t0, EXCEPTION_KS0
	csrwr	t1, EXCEPTION_KS1
	csrwr	ra, EXCEPTION_KS2

	/*
	 * The vmalloc handling is not in the hotpath.
	 */
	csrrd	t0, LOONGARCH_CSR_BADV
	bltz	t0, vmalloc_load
	csrrd	t1, LOONGARCH_CSR_PGDL

vmalloc_done_load:
	/* Get PGD offset in bytes */
	srli.d	t0, t0, PGDIR_SHIFT
	andi	t0, t0, (PTRS_PER_PGD - 1)
	slli.d	t0, t0, 3
	add.d	t1, t1, t0
#if CONFIG_PGTABLE_LEVELS > 3
	csrrd	t0, LOONGARCH_CSR_BADV
	ld.d	t1, t1, 0
	srli.d	t0, t0, PUD_SHIFT
	andi	t0, t0, (PTRS_PER_PUD - 1)
	slli.d	t0, t0, 3
	add.d	t1, t1, t0
#endif
#if CONFIG_PGTABLE_LEVELS > 2
	csrrd	t0, LOONGARCH_CSR_BADV
	ld.d	t1, t1, 0
	srli.d	t0, t0, PMD_SHIFT
	andi	t0, t0, (PTRS_PER_PMD - 1)
	slli.d	t0, t0, 3
	add.d	t1, t1, t0
#endif
	ld.d	ra, t1, 0

	/*
	 * For huge tlb entries, pmde doesn't contain an address but
	 * instead contains the tlb pte. Check the PAGE_HUGE bit and
	 * see if we need to jump to huge tlb processing.
	 */
	andi	t0, ra, _PAGE_HUGE
	bnez	t0, tlb_huge_update_load

	csrrd	t0, LOONGARCH_CSR_BADV
	srli.d	t0, t0, PAGE_SHIFT
	andi	t0, t0, (PTRS_PER_PTE - 1)
	slli.d	t0, t0, _PTE_T_LOG2
	add.d	t1, ra, t0

#ifdef CONFIG_SMP
smp_pgtable_change_load:
#endif
#ifdef CONFIG_SMP
	ll.d	t0, t1, 0
#else
	ld.d	t0, t1, 0
#endif
	tlbsrch

	srli.d	ra, t0, _PAGE_PRESENT_SHIFT
	andi	ra, ra, 1
	beqz	ra, nopage_tlb_load

	ori	t0, t0, _PAGE_VALID
#ifdef CONFIG_SMP
	sc.d	t0, t1, 0
	beqz	t0, smp_pgtable_change_load
#else
	st.d	t0, t1, 0
#endif
	ori	t1, t1, 8
	xori	t1, t1, 8
	ld.d	t0, t1, 0
	ld.d	t1, t1, 8
	csrwr	t0, LOONGARCH_CSR_TLBELO0
	csrwr	t1, LOONGARCH_CSR_TLBELO1
	tlbwr
leave_load:
	csrrd	t0, EXCEPTION_KS0
	csrrd	t1, EXCEPTION_KS1
	csrrd	ra, EXCEPTION_KS2
	ertn
#ifdef CONFIG_64BIT
vmalloc_load:
	la.abs	t1, swapper_pg_dir
	b	vmalloc_done_load
#endif

	/*
	 * This is the entry point when build_tlbchange_handler_head
	 * spots a huge page.
	 */
tlb_huge_update_load:
#ifdef CONFIG_SMP
	ll.d	t0, t1, 0
#else
	ld.d	t0, t1, 0
#endif
	srli.d	ra, t0, _PAGE_PRESENT_SHIFT
	andi	ra, ra, 1
	beqz	ra, nopage_tlb_load
	tlbsrch

	ori	t0, t0, _PAGE_VALID
#ifdef CONFIG_SMP
	sc.d	t0, t1, 0
	beqz	t0, tlb_huge_update_load
	ld.d	t0, t1, 0
#else
	st.d	t0, t1, 0
#endif
	addu16i.d	t1, zero, -(CSR_TLBIDX_EHINV >> 16)
	addi.d		ra, t1, 0
	csrxchg		ra, t1, LOONGARCH_CSR_TLBIDX
	tlbwr

	csrxchg	zero, t1, LOONGARCH_CSR_TLBIDX

	/*
	 * A huge PTE describes an area the size of the
	 * configured huge page size. This is twice the
	 * of the large TLB entry size we intend to use.
	 * A TLB entry half the size of the configured
	 * huge page size is configured into entrylo0
	 * and entrylo1 to cover the contiguous huge PTE
	 * address space.
	 */
	/* Huge page: Move Global bit */
	xori	t0, t0, _PAGE_HUGE
	lu12i.w	t1, _PAGE_HGLOBAL >> 12
	and	t1, t0, t1
	srli.d	t1, t1, (_PAGE_HGLOBAL_SHIFT - _PAGE_GLOBAL_SHIFT)
	or	t0, t0, t1

	addi.d	ra, t0, 0
	csrwr	t0, LOONGARCH_CSR_TLBELO0
	addi.d	t0, ra, 0

	/* Convert to entrylo1 */
	addi.d	t1, zero, 1
	slli.d	t1, t1, (HPAGE_SHIFT - 1)
	add.d	t0, t0, t1
	csrwr	t0, LOONGARCH_CSR_TLBELO1

	/* Set huge page tlb entry size */
	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_HUGE_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

	tlbfill

	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_DEFAULT_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

nopage_tlb_load:
	dbar	0
	csrrd	ra, EXCEPTION_KS2
	la.abs	t0, tlb_do_page_fault_0
	jr	t0
SYM_FUNC_END(handle_tlb_load)

SYM_FUNC_START(handle_tlb_store)
	csrwr	t0, EXCEPTION_KS0
	csrwr	t1, EXCEPTION_KS1
	csrwr	ra, EXCEPTION_KS2

	/*
	 * The vmalloc handling is not in the hotpath.
	 */
	csrrd	t0, LOONGARCH_CSR_BADV
	bltz	t0, vmalloc_store
	csrrd	t1, LOONGARCH_CSR_PGDL

vmalloc_done_store:
	/* Get PGD offset in bytes */
	srli.d	t0, t0, PGDIR_SHIFT
	andi	t0, t0, (PTRS_PER_PGD - 1)
	slli.d	t0, t0, 3
	add.d	t1, t1, t0

#if CONFIG_PGTABLE_LEVELS > 3
	csrrd	t0, LOONGARCH_CSR_BADV
	ld.d	t1, t1, 0
	srli.d	t0, t0, PUD_SHIFT
	andi	t0, t0, (PTRS_PER_PUD - 1)
	slli.d	t0, t0, 3
	add.d	t1, t1, t0
#endif
#if CONFIG_PGTABLE_LEVELS > 2
	csrrd	t0, LOONGARCH_CSR_BADV
	ld.d	t1, t1, 0
	srli.d	t0, t0, PMD_SHIFT
	andi	t0, t0, (PTRS_PER_PMD - 1)
	slli.d	t0, t0, 3
	add.d	t1, t1, t0
#endif
	ld.d	ra, t1, 0

	/*
	 * For huge tlb entries, pmde doesn't contain an address but
	 * instead contains the tlb pte. Check the PAGE_HUGE bit and
	 * see if we need to jump to huge tlb processing.
	 */
	andi	t0, ra, _PAGE_HUGE
	bnez	t0, tlb_huge_update_store

	csrrd	t0, LOONGARCH_CSR_BADV
	srli.d	t0, t0, PAGE_SHIFT
	andi	t0, t0, (PTRS_PER_PTE - 1)
	slli.d	t0, t0, _PTE_T_LOG2
	add.d	t1, ra, t0

#ifdef CONFIG_SMP
smp_pgtable_change_store:
#endif
#ifdef CONFIG_SMP
	ll.d	t0, t1, 0
#else
	ld.d	t0, t1, 0
#endif
	tlbsrch

	srli.d	ra, t0, _PAGE_PRESENT_SHIFT
	andi	ra, ra, ((_PAGE_PRESENT | _PAGE_WRITE) >> _PAGE_PRESENT_SHIFT)
	xori	ra, ra, ((_PAGE_PRESENT | _PAGE_WRITE) >> _PAGE_PRESENT_SHIFT)
	bnez	ra, nopage_tlb_store

	ori	t0, t0, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)
#ifdef CONFIG_SMP
	sc.d	t0, t1, 0
	beqz	t0, smp_pgtable_change_store
#else
	st.d	t0, t1, 0
#endif

	ori	t1, t1, 8
	xori	t1, t1, 8
	ld.d	t0, t1, 0
	ld.d	t1, t1, 8
	csrwr	t0, LOONGARCH_CSR_TLBELO0
	csrwr	t1, LOONGARCH_CSR_TLBELO1
	tlbwr
leave_store:
	csrrd	t0, EXCEPTION_KS0
	csrrd	t1, EXCEPTION_KS1
	csrrd	ra, EXCEPTION_KS2
	ertn
#ifdef CONFIG_64BIT
vmalloc_store:
	la.abs	t1, swapper_pg_dir
	b	vmalloc_done_store
#endif

	/*
	 * This is the entry point when build_tlbchange_handler_head
	 * spots a huge page.
	 */
tlb_huge_update_store:
#ifdef CONFIG_SMP
	ll.d	t0, t1, 0
#else
	ld.d	t0, t1, 0
#endif
	srli.d	ra, t0, _PAGE_PRESENT_SHIFT
	andi	ra, ra, ((_PAGE_PRESENT | _PAGE_WRITE) >> _PAGE_PRESENT_SHIFT)
	xori	ra, ra, ((_PAGE_PRESENT | _PAGE_WRITE) >> _PAGE_PRESENT_SHIFT)
	bnez	ra, nopage_tlb_store

	tlbsrch
	ori	t0, t0, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)

#ifdef CONFIG_SMP
	sc.d	t0, t1, 0
	beqz	t0, tlb_huge_update_store
	ld.d	t0, t1, 0
#else
	st.d	t0, t1, 0
#endif
	addu16i.d	t1, zero, -(CSR_TLBIDX_EHINV >> 16)
	addi.d		ra, t1, 0
	csrxchg		ra, t1, LOONGARCH_CSR_TLBIDX
	tlbwr

	csrxchg	zero, t1, LOONGARCH_CSR_TLBIDX
	/*
	 * A huge PTE describes an area the size of the
	 * configured huge page size. This is twice the
	 * of the large TLB entry size we intend to use.
	 * A TLB entry half the size of the configured
	 * huge page size is configured into entrylo0
	 * and entrylo1 to cover the contiguous huge PTE
	 * address space.
	 */
	/* Huge page: Move Global bit */
	xori	t0, t0, _PAGE_HUGE
	lu12i.w	t1, _PAGE_HGLOBAL >> 12
	and	t1, t0, t1
	srli.d	t1, t1, (_PAGE_HGLOBAL_SHIFT - _PAGE_GLOBAL_SHIFT)
	or	t0, t0, t1

	addi.d	ra, t0, 0
	csrwr	t0, LOONGARCH_CSR_TLBELO0
	addi.d	t0, ra, 0

	/* Convert to entrylo1 */
	addi.d	t1, zero, 1
	slli.d	t1, t1, (HPAGE_SHIFT - 1)
	add.d	t0, t0, t1
	csrwr	t0, LOONGARCH_CSR_TLBELO1

	/* Set huge page tlb entry size */
	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_HUGE_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

	tlbfill

	/* Reset default page size */
	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_DEFAULT_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

nopage_tlb_store:
	dbar	0
	csrrd	ra, EXCEPTION_KS2
	la.abs	t0, tlb_do_page_fault_1
	jr	t0
SYM_FUNC_END(handle_tlb_store)

SYM_FUNC_START(handle_tlb_modify)
	csrwr	t0, EXCEPTION_KS0
	csrwr	t1, EXCEPTION_KS1
	csrwr	ra, EXCEPTION_KS2

	/*
	 * The vmalloc handling is not in the hotpath.
	 */
	csrrd	t0, LOONGARCH_CSR_BADV
	bltz	t0, vmalloc_modify
	csrrd	t1, LOONGARCH_CSR_PGDL

vmalloc_done_modify:
	/* Get PGD offset in bytes */
	srli.d	t0, t0, PGDIR_SHIFT
	andi	t0, t0, (PTRS_PER_PGD - 1)
	slli.d	t0, t0, 3
	add.d	t1, t1, t0
#if CONFIG_PGTABLE_LEVELS > 3
	csrrd	t0, LOONGARCH_CSR_BADV
	ld.d	t1, t1, 0
	srli.d	t0, t0, PUD_SHIFT
	andi	t0, t0, (PTRS_PER_PUD - 1)
	slli.d	t0, t0, 3
	add.d	t1, t1, t0
#endif
#if CONFIG_PGTABLE_LEVELS > 2
	csrrd	t0, LOONGARCH_CSR_BADV
	ld.d	t1, t1, 0
	srli.d	t0, t0, PMD_SHIFT
	andi	t0, t0, (PTRS_PER_PMD - 1)
	slli.d	t0, t0, 3
	add.d	t1, t1, t0
#endif
	ld.d	ra, t1, 0

	/*
	 * For huge tlb entries, pmde doesn't contain an address but
	 * instead contains the tlb pte. Check the PAGE_HUGE bit and
	 * see if we need to jump to huge tlb processing.
	 */
	andi	t0, ra, _PAGE_HUGE
	bnez	t0, tlb_huge_update_modify

	csrrd	t0, LOONGARCH_CSR_BADV
	srli.d	t0, t0, PAGE_SHIFT
	andi	t0, t0, (PTRS_PER_PTE - 1)
	slli.d	t0, t0, _PTE_T_LOG2
	add.d	t1, ra, t0

#ifdef CONFIG_SMP
smp_pgtable_change_modify:
#endif
#ifdef CONFIG_SMP
	ll.d	t0, t1, 0
#else
	ld.d	t0, t1, 0
#endif
	tlbsrch

	srli.d	ra, t0, _PAGE_WRITE_SHIFT
	andi	ra, ra, 1
	beqz	ra, nopage_tlb_modify

	ori	t0, t0, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)
#ifdef CONFIG_SMP
	sc.d	t0, t1, 0
	beqz	t0, smp_pgtable_change_modify
#else
	st.d	t0, t1, 0
#endif
	ori	t1, t1, 8
	xori	t1, t1, 8
	ld.d	t0, t1, 0
	ld.d	t1, t1, 8
	csrwr	t0, LOONGARCH_CSR_TLBELO0
	csrwr	t1, LOONGARCH_CSR_TLBELO1
	tlbwr
leave_modify:
	csrrd	t0, EXCEPTION_KS0
	csrrd	t1, EXCEPTION_KS1
	csrrd	ra, EXCEPTION_KS2
	ertn
#ifdef CONFIG_64BIT
vmalloc_modify:
	la.abs	t1, swapper_pg_dir
	b	vmalloc_done_modify
#endif

	/*
	 * This is the entry point when
	 * build_tlbchange_handler_head spots a huge page.
	 */
tlb_huge_update_modify:
#ifdef CONFIG_SMP
	ll.d	t0, t1, 0
#else
	ld.d	t0, t1, 0
#endif

	srli.d	ra, t0, _PAGE_WRITE_SHIFT
	andi	ra, ra, 1
	beqz	ra, nopage_tlb_modify

	tlbsrch
	ori	t0, t0, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)

#ifdef CONFIG_SMP
	sc.d	t0, t1, 0
	beqz	t0, tlb_huge_update_modify
	ld.d	t0, t1, 0
#else
	st.d	t0, t1, 0
#endif
	/*
	 * A huge PTE describes an area the size of the
	 * configured huge page size. This is twice the
	 * of the large TLB entry size we intend to use.
	 * A TLB entry half the size of the configured
	 * huge page size is configured into entrylo0
	 * and entrylo1 to cover the contiguous huge PTE
	 * address space.
	 */
	/* Huge page: Move Global bit */
	xori	t0, t0, _PAGE_HUGE
	lu12i.w	t1, _PAGE_HGLOBAL >> 12
	and	t1, t0, t1
	srli.d	t1, t1, (_PAGE_HGLOBAL_SHIFT - _PAGE_GLOBAL_SHIFT)
	or	t0, t0, t1

	addi.d	ra, t0, 0
	csrwr	t0, LOONGARCH_CSR_TLBELO0
	addi.d	t0, ra, 0

	/* Convert to entrylo1 */
	addi.d	t1, zero, 1
	slli.d	t1, t1, (HPAGE_SHIFT - 1)
	add.d	t0, t0, t1
	csrwr	t0, LOONGARCH_CSR_TLBELO1

	/* Set huge page tlb entry size */
	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_HUGE_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

	tlbwr

	/* Reset default page size */
	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_DEFAULT_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

nopage_tlb_modify:
	dbar	0
	csrrd	ra, EXCEPTION_KS2
	la.abs	t0, tlb_do_page_fault_1
	jr	t0
SYM_FUNC_END(handle_tlb_modify)

SYM_FUNC_START(handle_tlb_refill)
	csrwr	t0, LOONGARCH_CSR_TLBRSAVE
	csrrd	t0, LOONGARCH_CSR_PGD
	lddir	t0, t0, 3
#if CONFIG_PGTABLE_LEVELS > 3
	lddir	t0, t0, 2
#endif
#if CONFIG_PGTABLE_LEVELS > 2
	lddir	t0, t0, 1
#endif
	ldpte	t0, 0
	ldpte	t0, 1
	tlbfill
	csrrd	t0, LOONGARCH_CSR_TLBRSAVE
	ertn
SYM_FUNC_END(handle_tlb_refill)
