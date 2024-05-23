/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <asm/asm.h>
#include <asm/loongarch.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

#define INVTLB_ADDR_GFALSE_AND_ASID	5

#define PTRS_PER_PGD_BITS	(PAGE_SHIFT - 3)
#define PTRS_PER_PUD_BITS	(PAGE_SHIFT - 3)
#define PTRS_PER_PMD_BITS	(PAGE_SHIFT - 3)
#define PTRS_PER_PTE_BITS	(PAGE_SHIFT - 3)

	.macro tlb_do_page_fault, write
	SYM_CODE_START(tlb_do_page_fault_\write)
	UNWIND_HINT_UNDEFINED
	SAVE_ALL
	csrrd		a2, LOONGARCH_CSR_BADV
	move		a0, sp
	REG_S		a2, sp, PT_BVADDR
	li.w		a1, \write
	bl		do_page_fault
	RESTORE_ALL_AND_RET
	SYM_CODE_END(tlb_do_page_fault_\write)
	.endm

	tlb_do_page_fault 0
	tlb_do_page_fault 1

SYM_CODE_START(handle_tlb_protect)
	UNWIND_HINT_UNDEFINED
	BACKUP_T0T1
	SAVE_ALL
	move		a0, sp
	move		a1, zero
	csrrd		a2, LOONGARCH_CSR_BADV
	REG_S		a2, sp, PT_BVADDR
	la_abs		t0, do_page_fault
	jirl		ra, t0, 0
	RESTORE_ALL_AND_RET
SYM_CODE_END(handle_tlb_protect)

SYM_CODE_START(handle_tlb_load)
	UNWIND_HINT_UNDEFINED
	csrwr		t0, EXCEPTION_KS0
	csrwr		t1, EXCEPTION_KS1
	csrwr		ra, EXCEPTION_KS2

	/*
	 * The vmalloc handling is not in the hotpath.
	 */
	csrrd		t0, LOONGARCH_CSR_BADV
	bltz		t0, vmalloc_load
	csrrd		t1, LOONGARCH_CSR_PGDL

vmalloc_done_load:
	/* Get PGD offset in bytes */
	bstrpick.d	ra, t0, PTRS_PER_PGD_BITS + PGDIR_SHIFT - 1, PGDIR_SHIFT
	alsl.d		t1, ra, t1, 3
#if CONFIG_PGTABLE_LEVELS > 3
	ld.d		t1, t1, 0
	bstrpick.d	ra, t0, PTRS_PER_PUD_BITS + PUD_SHIFT - 1, PUD_SHIFT
	alsl.d		t1, ra, t1, 3
#endif
#if CONFIG_PGTABLE_LEVELS > 2
	ld.d		t1, t1, 0
	bstrpick.d	ra, t0, PTRS_PER_PMD_BITS + PMD_SHIFT - 1, PMD_SHIFT
	alsl.d		t1, ra, t1, 3
#endif
	ld.d		ra, t1, 0

	/*
	 * For huge tlb entries, pmde doesn't contain an address but
	 * instead contains the tlb pte. Check the PAGE_HUGE bit and
	 * see if we need to jump to huge tlb processing.
	 */
	rotri.d		ra, ra, _PAGE_HUGE_SHIFT + 1
	bltz		ra, tlb_huge_update_load

	rotri.d		ra, ra, 64 - (_PAGE_HUGE_SHIFT + 1)
	bstrpick.d	t0, t0, PTRS_PER_PTE_BITS + PAGE_SHIFT - 1, PAGE_SHIFT
	alsl.d		t1, t0, ra, _PTE_T_LOG2

#ifdef CONFIG_SMP
smp_pgtable_change_load:
	ll.d		t0, t1, 0
#else
	ld.d		t0, t1, 0
#endif
	andi		ra, t0, _PAGE_PRESENT
	beqz		ra, nopage_tlb_load

	ori		t0, t0, _PAGE_VALID
#ifdef CONFIG_SMP
	sc.d		t0, t1, 0
	beqz		t0, smp_pgtable_change_load
#else
	st.d		t0, t1, 0
#endif
	tlbsrch
	bstrins.d	t1, zero, 3, 3
	ld.d		t0, t1, 0
	ld.d		t1, t1, 8
	csrwr		t0, LOONGARCH_CSR_TLBELO0
	csrwr		t1, LOONGARCH_CSR_TLBELO1
	tlbwr

	csrrd		t0, EXCEPTION_KS0
	csrrd		t1, EXCEPTION_KS1
	csrrd		ra, EXCEPTION_KS2
	ertn

#ifdef CONFIG_64BIT
vmalloc_load:
	la_abs		t1, swapper_pg_dir
	b		vmalloc_done_load
#endif

	/* This is the entry point of a huge page. */
tlb_huge_update_load:
#ifdef CONFIG_SMP
	ll.d		ra, t1, 0
#else
	rotri.d		ra, ra, 64 - (_PAGE_HUGE_SHIFT + 1)
#endif
	andi		t0, ra, _PAGE_PRESENT
	beqz		t0, nopage_tlb_load

#ifdef CONFIG_SMP
	ori		t0, ra, _PAGE_VALID
	sc.d		t0, t1, 0
	beqz		t0, tlb_huge_update_load
	ori		t0, ra, _PAGE_VALID
#else
	ori		t0, ra, _PAGE_VALID
	st.d		t0, t1, 0
#endif
	csrrd		ra, LOONGARCH_CSR_ASID
	csrrd		t1, LOONGARCH_CSR_BADV
	andi		ra, ra, CSR_ASID_ASID
	invtlb		INVTLB_ADDR_GFALSE_AND_ASID, ra, t1

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
	xori		t0, t0, _PAGE_HUGE
	lu12i.w		t1, _PAGE_HGLOBAL >> 12
	and		t1, t0, t1
	srli.d		t1, t1, (_PAGE_HGLOBAL_SHIFT - _PAGE_GLOBAL_SHIFT)
	or		t0, t0, t1

	move		ra, t0
	csrwr		ra, LOONGARCH_CSR_TLBELO0

	/* Convert to entrylo1 */
	addi.d		t1, zero, 1
	slli.d		t1, t1, (HPAGE_SHIFT - 1)
	add.d		t0, t0, t1
	csrwr		t0, LOONGARCH_CSR_TLBELO1

	/* Set huge page tlb entry size */
	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_HUGE_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

	tlbfill

	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_DEFAULT_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

	csrrd		t0, EXCEPTION_KS0
	csrrd		t1, EXCEPTION_KS1
	csrrd		ra, EXCEPTION_KS2
	ertn

nopage_tlb_load:
	dbar		0x700
	csrrd		ra, EXCEPTION_KS2
	la_abs		t0, tlb_do_page_fault_0
	jr		t0
SYM_CODE_END(handle_tlb_load)

SYM_CODE_START(handle_tlb_load_ptw)
	UNWIND_HINT_UNDEFINED
	csrwr		t0, LOONGARCH_CSR_KS0
	csrwr		t1, LOONGARCH_CSR_KS1
	la_abs		t0, tlb_do_page_fault_0
	jr		t0
SYM_CODE_END(handle_tlb_load_ptw)

SYM_CODE_START(handle_tlb_store)
	UNWIND_HINT_UNDEFINED
	csrwr		t0, EXCEPTION_KS0
	csrwr		t1, EXCEPTION_KS1
	csrwr		ra, EXCEPTION_KS2

	/*
	 * The vmalloc handling is not in the hotpath.
	 */
	csrrd		t0, LOONGARCH_CSR_BADV
	bltz		t0, vmalloc_store
	csrrd		t1, LOONGARCH_CSR_PGDL

vmalloc_done_store:
	/* Get PGD offset in bytes */
	bstrpick.d	ra, t0, PTRS_PER_PGD_BITS + PGDIR_SHIFT - 1, PGDIR_SHIFT
	alsl.d		t1, ra, t1, 3
#if CONFIG_PGTABLE_LEVELS > 3
	ld.d		t1, t1, 0
	bstrpick.d	ra, t0, PTRS_PER_PUD_BITS + PUD_SHIFT - 1, PUD_SHIFT
	alsl.d		t1, ra, t1, 3
#endif
#if CONFIG_PGTABLE_LEVELS > 2
	ld.d		t1, t1, 0
	bstrpick.d	ra, t0, PTRS_PER_PMD_BITS + PMD_SHIFT - 1, PMD_SHIFT
	alsl.d		t1, ra, t1, 3
#endif
	ld.d		ra, t1, 0

	/*
	 * For huge tlb entries, pmde doesn't contain an address but
	 * instead contains the tlb pte. Check the PAGE_HUGE bit and
	 * see if we need to jump to huge tlb processing.
	 */
	rotri.d		ra, ra, _PAGE_HUGE_SHIFT + 1
	bltz		ra, tlb_huge_update_store

	rotri.d		ra, ra, 64 - (_PAGE_HUGE_SHIFT + 1)
	bstrpick.d	t0, t0, PTRS_PER_PTE_BITS + PAGE_SHIFT - 1, PAGE_SHIFT
	alsl.d		t1, t0, ra, _PTE_T_LOG2

#ifdef CONFIG_SMP
smp_pgtable_change_store:
	ll.d		t0, t1, 0
#else
	ld.d		t0, t1, 0
#endif
	andi		ra, t0, _PAGE_PRESENT | _PAGE_WRITE
	xori		ra, ra, _PAGE_PRESENT | _PAGE_WRITE
	bnez		ra, nopage_tlb_store

	ori		t0, t0, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)
#ifdef CONFIG_SMP
	sc.d		t0, t1, 0
	beqz		t0, smp_pgtable_change_store
#else
	st.d		t0, t1, 0
#endif
	tlbsrch
	bstrins.d	t1, zero, 3, 3
	ld.d		t0, t1, 0
	ld.d		t1, t1, 8
	csrwr		t0, LOONGARCH_CSR_TLBELO0
	csrwr		t1, LOONGARCH_CSR_TLBELO1
	tlbwr

	csrrd		t0, EXCEPTION_KS0
	csrrd		t1, EXCEPTION_KS1
	csrrd		ra, EXCEPTION_KS2
	ertn

#ifdef CONFIG_64BIT
vmalloc_store:
	la_abs		t1, swapper_pg_dir
	b		vmalloc_done_store
#endif

	/* This is the entry point of a huge page. */
tlb_huge_update_store:
#ifdef CONFIG_SMP
	ll.d		ra, t1, 0
#else
	rotri.d		ra, ra, 64 - (_PAGE_HUGE_SHIFT + 1)
#endif
	andi		t0, ra, _PAGE_PRESENT | _PAGE_WRITE
	xori		t0, t0, _PAGE_PRESENT | _PAGE_WRITE
	bnez		t0, nopage_tlb_store

#ifdef CONFIG_SMP
	ori		t0, ra, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)
	sc.d		t0, t1, 0
	beqz		t0, tlb_huge_update_store
	ori		t0, ra, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)
#else
	ori		t0, ra, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)
	st.d		t0, t1, 0
#endif
	csrrd		ra, LOONGARCH_CSR_ASID
	csrrd		t1, LOONGARCH_CSR_BADV
	andi		ra, ra, CSR_ASID_ASID
	invtlb		INVTLB_ADDR_GFALSE_AND_ASID, ra, t1

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
	xori		t0, t0, _PAGE_HUGE
	lu12i.w		t1, _PAGE_HGLOBAL >> 12
	and		t1, t0, t1
	srli.d		t1, t1, (_PAGE_HGLOBAL_SHIFT - _PAGE_GLOBAL_SHIFT)
	or		t0, t0, t1

	move		ra, t0
	csrwr		ra, LOONGARCH_CSR_TLBELO0

	/* Convert to entrylo1 */
	addi.d		t1, zero, 1
	slli.d		t1, t1, (HPAGE_SHIFT - 1)
	add.d		t0, t0, t1
	csrwr		t0, LOONGARCH_CSR_TLBELO1

	/* Set huge page tlb entry size */
	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_HUGE_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

	tlbfill

	/* Reset default page size */
	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_DEFAULT_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

	csrrd		t0, EXCEPTION_KS0
	csrrd		t1, EXCEPTION_KS1
	csrrd		ra, EXCEPTION_KS2
	ertn

nopage_tlb_store:
	dbar		0x700
	csrrd		ra, EXCEPTION_KS2
	la_abs		t0, tlb_do_page_fault_1
	jr		t0
SYM_CODE_END(handle_tlb_store)

SYM_CODE_START(handle_tlb_store_ptw)
	UNWIND_HINT_UNDEFINED
	csrwr		t0, LOONGARCH_CSR_KS0
	csrwr		t1, LOONGARCH_CSR_KS1
	la_abs		t0, tlb_do_page_fault_1
	jr		t0
SYM_CODE_END(handle_tlb_store_ptw)

SYM_CODE_START(handle_tlb_modify)
	UNWIND_HINT_UNDEFINED
	csrwr		t0, EXCEPTION_KS0
	csrwr		t1, EXCEPTION_KS1
	csrwr		ra, EXCEPTION_KS2

	/*
	 * The vmalloc handling is not in the hotpath.
	 */
	csrrd		t0, LOONGARCH_CSR_BADV
	bltz		t0, vmalloc_modify
	csrrd		t1, LOONGARCH_CSR_PGDL

vmalloc_done_modify:
	/* Get PGD offset in bytes */
	bstrpick.d	ra, t0, PTRS_PER_PGD_BITS + PGDIR_SHIFT - 1, PGDIR_SHIFT
	alsl.d		t1, ra, t1, 3
#if CONFIG_PGTABLE_LEVELS > 3
	ld.d		t1, t1, 0
	bstrpick.d	ra, t0, PTRS_PER_PUD_BITS + PUD_SHIFT - 1, PUD_SHIFT
	alsl.d		t1, ra, t1, 3
#endif
#if CONFIG_PGTABLE_LEVELS > 2
	ld.d		t1, t1, 0
	bstrpick.d	ra, t0, PTRS_PER_PMD_BITS + PMD_SHIFT - 1, PMD_SHIFT
	alsl.d		t1, ra, t1, 3
#endif
	ld.d		ra, t1, 0

	/*
	 * For huge tlb entries, pmde doesn't contain an address but
	 * instead contains the tlb pte. Check the PAGE_HUGE bit and
	 * see if we need to jump to huge tlb processing.
	 */
	rotri.d		ra, ra, _PAGE_HUGE_SHIFT + 1
	bltz		ra, tlb_huge_update_modify

	rotri.d		ra, ra, 64 - (_PAGE_HUGE_SHIFT + 1)
	bstrpick.d	t0, t0, PTRS_PER_PTE_BITS + PAGE_SHIFT - 1, PAGE_SHIFT
	alsl.d		t1, t0, ra, _PTE_T_LOG2

#ifdef CONFIG_SMP
smp_pgtable_change_modify:
	ll.d		t0, t1, 0
#else
	ld.d		t0, t1, 0
#endif
	andi		ra, t0, _PAGE_WRITE
	beqz		ra, nopage_tlb_modify

	ori		t0, t0, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)
#ifdef CONFIG_SMP
	sc.d		t0, t1, 0
	beqz		t0, smp_pgtable_change_modify
#else
	st.d		t0, t1, 0
#endif
	tlbsrch
	bstrins.d	t1, zero, 3, 3
	ld.d		t0, t1, 0
	ld.d		t1, t1, 8
	csrwr		t0, LOONGARCH_CSR_TLBELO0
	csrwr		t1, LOONGARCH_CSR_TLBELO1
	tlbwr

	csrrd		t0, EXCEPTION_KS0
	csrrd		t1, EXCEPTION_KS1
	csrrd		ra, EXCEPTION_KS2
	ertn

#ifdef CONFIG_64BIT
vmalloc_modify:
	la_abs		t1, swapper_pg_dir
	b		vmalloc_done_modify
#endif

	/* This is the entry point of a huge page. */
tlb_huge_update_modify:
#ifdef CONFIG_SMP
	ll.d		ra, t1, 0
#else
	rotri.d		ra, ra, 64 - (_PAGE_HUGE_SHIFT + 1)
#endif
	andi		t0, ra, _PAGE_WRITE
	beqz		t0, nopage_tlb_modify

#ifdef CONFIG_SMP
	ori		t0, ra, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)
	sc.d		t0, t1, 0
	beqz		t0, tlb_huge_update_modify
	ori		t0, ra, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)
#else
	ori		t0, ra, (_PAGE_VALID | _PAGE_DIRTY | _PAGE_MODIFIED)
	st.d		t0, t1, 0
#endif
	csrrd		ra, LOONGARCH_CSR_ASID
	csrrd		t1, LOONGARCH_CSR_BADV
	andi		ra, ra, CSR_ASID_ASID
	invtlb		INVTLB_ADDR_GFALSE_AND_ASID, ra, t1

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
	xori		t0, t0, _PAGE_HUGE
	lu12i.w		t1, _PAGE_HGLOBAL >> 12
	and		t1, t0, t1
	srli.d		t1, t1, (_PAGE_HGLOBAL_SHIFT - _PAGE_GLOBAL_SHIFT)
	or		t0, t0, t1

	move		ra, t0
	csrwr		ra, LOONGARCH_CSR_TLBELO0

	/* Convert to entrylo1 */
	addi.d		t1, zero, 1
	slli.d		t1, t1, (HPAGE_SHIFT - 1)
	add.d		t0, t0, t1
	csrwr		t0, LOONGARCH_CSR_TLBELO1

	/* Set huge page tlb entry size */
	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_HUGE_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

	tlbfill

	/* Reset default page size */
	addu16i.d	t0, zero, (CSR_TLBIDX_PS >> 16)
	addu16i.d	t1, zero, (PS_DEFAULT_SIZE << (CSR_TLBIDX_PS_SHIFT - 16))
	csrxchg		t1, t0, LOONGARCH_CSR_TLBIDX

	csrrd		t0, EXCEPTION_KS0
	csrrd		t1, EXCEPTION_KS1
	csrrd		ra, EXCEPTION_KS2
	ertn

nopage_tlb_modify:
	dbar		0x700
	csrrd		ra, EXCEPTION_KS2
	la_abs		t0, tlb_do_page_fault_1
	jr		t0
SYM_CODE_END(handle_tlb_modify)

SYM_CODE_START(handle_tlb_modify_ptw)
	UNWIND_HINT_UNDEFINED
	csrwr		t0, LOONGARCH_CSR_KS0
	csrwr		t1, LOONGARCH_CSR_KS1
	la_abs		t0, tlb_do_page_fault_1
	jr		t0
SYM_CODE_END(handle_tlb_modify_ptw)

SYM_CODE_START(handle_tlb_refill)
	UNWIND_HINT_UNDEFINED
	csrwr		t0, LOONGARCH_CSR_TLBRSAVE
	csrrd		t0, LOONGARCH_CSR_PGD
	lddir		t0, t0, 3
#if CONFIG_PGTABLE_LEVELS > 3
	lddir		t0, t0, 2
#endif
#if CONFIG_PGTABLE_LEVELS > 2
	lddir		t0, t0, 1
#endif
	ldpte		t0, 0
	ldpte		t0, 1
	tlbfill
	csrrd		t0, LOONGARCH_CSR_TLBRSAVE
	ertn
SYM_CODE_END(handle_tlb_refill)
