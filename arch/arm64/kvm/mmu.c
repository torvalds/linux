// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <linux/mman.h>
#include <linux/kvm_host.h>
#include <linux/io.h>
#include <linux/hugetlb.h>
#include <linux/sched/signal.h>
#include <trace/events/kvm.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_ras.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/virt.h>

#include "trace.h"

static pgd_t *boot_hyp_pgd;
static pgd_t *hyp_pgd;
static pgd_t *merged_hyp_pgd;
static DEFINE_MUTEX(kvm_hyp_pgd_mutex);

static unsigned long hyp_idmap_start;
static unsigned long hyp_idmap_end;
static phys_addr_t hyp_idmap_vector;

static unsigned long io_map_base;

#define hyp_pgd_order get_order(PTRS_PER_PGD * sizeof(pgd_t))

#define KVM_S2PTE_FLAG_IS_IOMAP		(1UL << 0)
#define KVM_S2_FLAG_LOGGING_ACTIVE	(1UL << 1)

static bool is_iomap(unsigned long flags)
{
	return flags & KVM_S2PTE_FLAG_IS_IOMAP;
}

static bool memslot_is_logging(struct kvm_memory_slot *memslot)
{
	return memslot->dirty_bitmap && !(memslot->flags & KVM_MEM_READONLY);
}

/**
 * kvm_flush_remote_tlbs() - flush all VM TLB entries for v7/8
 * @kvm:	pointer to kvm structure.
 *
 * Interface to HYP function to flush all VM TLB entries
 */
void kvm_flush_remote_tlbs(struct kvm *kvm)
{
	kvm_call_hyp(__kvm_tlb_flush_vmid, &kvm->arch.mmu);
}

static void kvm_tlb_flush_vmid_ipa(struct kvm_s2_mmu *mmu, phys_addr_t ipa,
				   int level)
{
	kvm_call_hyp(__kvm_tlb_flush_vmid_ipa, mmu, ipa, level);
}

/*
 * D-Cache management functions. They take the page table entries by
 * value, as they are flushing the cache using the kernel mapping (or
 * kmap on 32bit).
 */
static void kvm_flush_dcache_pte(pte_t pte)
{
	__kvm_flush_dcache_pte(pte);
}

static void kvm_flush_dcache_pmd(pmd_t pmd)
{
	__kvm_flush_dcache_pmd(pmd);
}

static void kvm_flush_dcache_pud(pud_t pud)
{
	__kvm_flush_dcache_pud(pud);
}

static bool kvm_is_device_pfn(unsigned long pfn)
{
	return !pfn_valid(pfn);
}

/**
 * stage2_dissolve_pmd() - clear and flush huge PMD entry
 * @mmu:	pointer to mmu structure to operate on
 * @addr:	IPA
 * @pmd:	pmd pointer for IPA
 *
 * Function clears a PMD entry, flushes addr 1st and 2nd stage TLBs.
 */
static void stage2_dissolve_pmd(struct kvm_s2_mmu *mmu, phys_addr_t addr, pmd_t *pmd)
{
	if (!pmd_thp_or_huge(*pmd))
		return;

	pmd_clear(pmd);
	kvm_tlb_flush_vmid_ipa(mmu, addr, S2_PMD_LEVEL);
	put_page(virt_to_page(pmd));
}

/**
 * stage2_dissolve_pud() - clear and flush huge PUD entry
 * @mmu:	pointer to mmu structure to operate on
 * @addr:	IPA
 * @pud:	pud pointer for IPA
 *
 * Function clears a PUD entry, flushes addr 1st and 2nd stage TLBs.
 */
static void stage2_dissolve_pud(struct kvm_s2_mmu *mmu, phys_addr_t addr, pud_t *pudp)
{
	struct kvm *kvm = mmu->kvm;

	if (!stage2_pud_huge(kvm, *pudp))
		return;

	stage2_pud_clear(kvm, pudp);
	kvm_tlb_flush_vmid_ipa(mmu, addr, S2_PUD_LEVEL);
	put_page(virt_to_page(pudp));
}

static void clear_stage2_pgd_entry(struct kvm_s2_mmu *mmu, pgd_t *pgd, phys_addr_t addr)
{
	struct kvm *kvm = mmu->kvm;
	p4d_t *p4d_table __maybe_unused = stage2_p4d_offset(kvm, pgd, 0UL);
	stage2_pgd_clear(kvm, pgd);
	kvm_tlb_flush_vmid_ipa(mmu, addr, S2_NO_LEVEL_HINT);
	stage2_p4d_free(kvm, p4d_table);
	put_page(virt_to_page(pgd));
}

static void clear_stage2_p4d_entry(struct kvm_s2_mmu *mmu, p4d_t *p4d, phys_addr_t addr)
{
	struct kvm *kvm = mmu->kvm;
	pud_t *pud_table __maybe_unused = stage2_pud_offset(kvm, p4d, 0);
	stage2_p4d_clear(kvm, p4d);
	kvm_tlb_flush_vmid_ipa(mmu, addr, S2_NO_LEVEL_HINT);
	stage2_pud_free(kvm, pud_table);
	put_page(virt_to_page(p4d));
}

static void clear_stage2_pud_entry(struct kvm_s2_mmu *mmu, pud_t *pud, phys_addr_t addr)
{
	struct kvm *kvm = mmu->kvm;
	pmd_t *pmd_table __maybe_unused = stage2_pmd_offset(kvm, pud, 0);

	VM_BUG_ON(stage2_pud_huge(kvm, *pud));
	stage2_pud_clear(kvm, pud);
	kvm_tlb_flush_vmid_ipa(mmu, addr, S2_NO_LEVEL_HINT);
	stage2_pmd_free(kvm, pmd_table);
	put_page(virt_to_page(pud));
}

static void clear_stage2_pmd_entry(struct kvm_s2_mmu *mmu, pmd_t *pmd, phys_addr_t addr)
{
	pte_t *pte_table = pte_offset_kernel(pmd, 0);
	VM_BUG_ON(pmd_thp_or_huge(*pmd));
	pmd_clear(pmd);
	kvm_tlb_flush_vmid_ipa(mmu, addr, S2_NO_LEVEL_HINT);
	free_page((unsigned long)pte_table);
	put_page(virt_to_page(pmd));
}

static inline void kvm_set_pte(pte_t *ptep, pte_t new_pte)
{
	WRITE_ONCE(*ptep, new_pte);
	dsb(ishst);
}

static inline void kvm_set_pmd(pmd_t *pmdp, pmd_t new_pmd)
{
	WRITE_ONCE(*pmdp, new_pmd);
	dsb(ishst);
}

static inline void kvm_pmd_populate(pmd_t *pmdp, pte_t *ptep)
{
	kvm_set_pmd(pmdp, kvm_mk_pmd(ptep));
}

static inline void kvm_pud_populate(pud_t *pudp, pmd_t *pmdp)
{
	WRITE_ONCE(*pudp, kvm_mk_pud(pmdp));
	dsb(ishst);
}

static inline void kvm_p4d_populate(p4d_t *p4dp, pud_t *pudp)
{
	WRITE_ONCE(*p4dp, kvm_mk_p4d(pudp));
	dsb(ishst);
}

static inline void kvm_pgd_populate(pgd_t *pgdp, p4d_t *p4dp)
{
#ifndef __PAGETABLE_P4D_FOLDED
	WRITE_ONCE(*pgdp, kvm_mk_pgd(p4dp));
	dsb(ishst);
#endif
}

/*
 * Unmapping vs dcache management:
 *
 * If a guest maps certain memory pages as uncached, all writes will
 * bypass the data cache and go directly to RAM.  However, the CPUs
 * can still speculate reads (not writes) and fill cache lines with
 * data.
 *
 * Those cache lines will be *clean* cache lines though, so a
 * clean+invalidate operation is equivalent to an invalidate
 * operation, because no cache lines are marked dirty.
 *
 * Those clean cache lines could be filled prior to an uncached write
 * by the guest, and the cache coherent IO subsystem would therefore
 * end up writing old data to disk.
 *
 * This is why right after unmapping a page/section and invalidating
 * the corresponding TLBs, we call kvm_flush_dcache_p*() to make sure
 * the IO subsystem will never hit in the cache.
 *
 * This is all avoided on systems that have ARM64_HAS_STAGE2_FWB, as
 * we then fully enforce cacheability of RAM, no matter what the guest
 * does.
 */
static void unmap_stage2_ptes(struct kvm_s2_mmu *mmu, pmd_t *pmd,
		       phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t start_addr = addr;
	pte_t *pte, *start_pte;

	start_pte = pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_none(*pte)) {
			pte_t old_pte = *pte;

			kvm_set_pte(pte, __pte(0));
			kvm_tlb_flush_vmid_ipa(mmu, addr, S2_PTE_LEVEL);

			/* No need to invalidate the cache for device mappings */
			if (!kvm_is_device_pfn(pte_pfn(old_pte)))
				kvm_flush_dcache_pte(old_pte);

			put_page(virt_to_page(pte));
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);

	if (stage2_pte_table_empty(mmu->kvm, start_pte))
		clear_stage2_pmd_entry(mmu, pmd, start_addr);
}

static void unmap_stage2_pmds(struct kvm_s2_mmu *mmu, pud_t *pud,
		       phys_addr_t addr, phys_addr_t end)
{
	struct kvm *kvm = mmu->kvm;
	phys_addr_t next, start_addr = addr;
	pmd_t *pmd, *start_pmd;

	start_pmd = pmd = stage2_pmd_offset(kvm, pud, addr);
	do {
		next = stage2_pmd_addr_end(kvm, addr, end);
		if (!pmd_none(*pmd)) {
			if (pmd_thp_or_huge(*pmd)) {
				pmd_t old_pmd = *pmd;

				pmd_clear(pmd);
				kvm_tlb_flush_vmid_ipa(mmu, addr, S2_PMD_LEVEL);

				kvm_flush_dcache_pmd(old_pmd);

				put_page(virt_to_page(pmd));
			} else {
				unmap_stage2_ptes(mmu, pmd, addr, next);
			}
		}
	} while (pmd++, addr = next, addr != end);

	if (stage2_pmd_table_empty(kvm, start_pmd))
		clear_stage2_pud_entry(mmu, pud, start_addr);
}

static void unmap_stage2_puds(struct kvm_s2_mmu *mmu, p4d_t *p4d,
		       phys_addr_t addr, phys_addr_t end)
{
	struct kvm *kvm = mmu->kvm;
	phys_addr_t next, start_addr = addr;
	pud_t *pud, *start_pud;

	start_pud = pud = stage2_pud_offset(kvm, p4d, addr);
	do {
		next = stage2_pud_addr_end(kvm, addr, end);
		if (!stage2_pud_none(kvm, *pud)) {
			if (stage2_pud_huge(kvm, *pud)) {
				pud_t old_pud = *pud;

				stage2_pud_clear(kvm, pud);
				kvm_tlb_flush_vmid_ipa(mmu, addr, S2_PUD_LEVEL);
				kvm_flush_dcache_pud(old_pud);
				put_page(virt_to_page(pud));
			} else {
				unmap_stage2_pmds(mmu, pud, addr, next);
			}
		}
	} while (pud++, addr = next, addr != end);

	if (stage2_pud_table_empty(kvm, start_pud))
		clear_stage2_p4d_entry(mmu, p4d, start_addr);
}

static void unmap_stage2_p4ds(struct kvm_s2_mmu *mmu, pgd_t *pgd,
		       phys_addr_t addr, phys_addr_t end)
{
	struct kvm *kvm = mmu->kvm;
	phys_addr_t next, start_addr = addr;
	p4d_t *p4d, *start_p4d;

	start_p4d = p4d = stage2_p4d_offset(kvm, pgd, addr);
	do {
		next = stage2_p4d_addr_end(kvm, addr, end);
		if (!stage2_p4d_none(kvm, *p4d))
			unmap_stage2_puds(mmu, p4d, addr, next);
	} while (p4d++, addr = next, addr != end);

	if (stage2_p4d_table_empty(kvm, start_p4d))
		clear_stage2_pgd_entry(mmu, pgd, start_addr);
}

/**
 * unmap_stage2_range -- Clear stage2 page table entries to unmap a range
 * @kvm:   The VM pointer
 * @start: The intermediate physical base address of the range to unmap
 * @size:  The size of the area to unmap
 *
 * Clear a range of stage-2 mappings, lowering the various ref-counts.  Must
 * be called while holding mmu_lock (unless for freeing the stage2 pgd before
 * destroying the VM), otherwise another faulting VCPU may come in and mess
 * with things behind our backs.
 */
static void __unmap_stage2_range(struct kvm_s2_mmu *mmu, phys_addr_t start, u64 size,
				 bool may_block)
{
	struct kvm *kvm = mmu->kvm;
	pgd_t *pgd;
	phys_addr_t addr = start, end = start + size;
	phys_addr_t next;

	assert_spin_locked(&kvm->mmu_lock);
	WARN_ON(size & ~PAGE_MASK);

	pgd = mmu->pgd + stage2_pgd_index(kvm, addr);
	do {
		/*
		 * Make sure the page table is still active, as another thread
		 * could have possibly freed the page table, while we released
		 * the lock.
		 */
		if (!READ_ONCE(mmu->pgd))
			break;
		next = stage2_pgd_addr_end(kvm, addr, end);
		if (!stage2_pgd_none(kvm, *pgd))
			unmap_stage2_p4ds(mmu, pgd, addr, next);
		/*
		 * If the range is too large, release the kvm->mmu_lock
		 * to prevent starvation and lockup detector warnings.
		 */
		if (may_block && next != end)
			cond_resched_lock(&kvm->mmu_lock);
	} while (pgd++, addr = next, addr != end);
}

static void unmap_stage2_range(struct kvm_s2_mmu *mmu, phys_addr_t start, u64 size)
{
	__unmap_stage2_range(mmu, start, size, true);
}

static void stage2_flush_ptes(struct kvm_s2_mmu *mmu, pmd_t *pmd,
			      phys_addr_t addr, phys_addr_t end)
{
	pte_t *pte;

	pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_none(*pte) && !kvm_is_device_pfn(pte_pfn(*pte)))
			kvm_flush_dcache_pte(*pte);
	} while (pte++, addr += PAGE_SIZE, addr != end);
}

static void stage2_flush_pmds(struct kvm_s2_mmu *mmu, pud_t *pud,
			      phys_addr_t addr, phys_addr_t end)
{
	struct kvm *kvm = mmu->kvm;
	pmd_t *pmd;
	phys_addr_t next;

	pmd = stage2_pmd_offset(kvm, pud, addr);
	do {
		next = stage2_pmd_addr_end(kvm, addr, end);
		if (!pmd_none(*pmd)) {
			if (pmd_thp_or_huge(*pmd))
				kvm_flush_dcache_pmd(*pmd);
			else
				stage2_flush_ptes(mmu, pmd, addr, next);
		}
	} while (pmd++, addr = next, addr != end);
}

static void stage2_flush_puds(struct kvm_s2_mmu *mmu, p4d_t *p4d,
			      phys_addr_t addr, phys_addr_t end)
{
	struct kvm *kvm = mmu->kvm;
	pud_t *pud;
	phys_addr_t next;

	pud = stage2_pud_offset(kvm, p4d, addr);
	do {
		next = stage2_pud_addr_end(kvm, addr, end);
		if (!stage2_pud_none(kvm, *pud)) {
			if (stage2_pud_huge(kvm, *pud))
				kvm_flush_dcache_pud(*pud);
			else
				stage2_flush_pmds(mmu, pud, addr, next);
		}
	} while (pud++, addr = next, addr != end);
}

static void stage2_flush_p4ds(struct kvm_s2_mmu *mmu, pgd_t *pgd,
			      phys_addr_t addr, phys_addr_t end)
{
	struct kvm *kvm = mmu->kvm;
	p4d_t *p4d;
	phys_addr_t next;

	p4d = stage2_p4d_offset(kvm, pgd, addr);
	do {
		next = stage2_p4d_addr_end(kvm, addr, end);
		if (!stage2_p4d_none(kvm, *p4d))
			stage2_flush_puds(mmu, p4d, addr, next);
	} while (p4d++, addr = next, addr != end);
}

static void stage2_flush_memslot(struct kvm *kvm,
				 struct kvm_memory_slot *memslot)
{
	struct kvm_s2_mmu *mmu = &kvm->arch.mmu;
	phys_addr_t addr = memslot->base_gfn << PAGE_SHIFT;
	phys_addr_t end = addr + PAGE_SIZE * memslot->npages;
	phys_addr_t next;
	pgd_t *pgd;

	pgd = mmu->pgd + stage2_pgd_index(kvm, addr);
	do {
		next = stage2_pgd_addr_end(kvm, addr, end);
		if (!stage2_pgd_none(kvm, *pgd))
			stage2_flush_p4ds(mmu, pgd, addr, next);

		if (next != end)
			cond_resched_lock(&kvm->mmu_lock);
	} while (pgd++, addr = next, addr != end);
}

/**
 * stage2_flush_vm - Invalidate cache for pages mapped in stage 2
 * @kvm: The struct kvm pointer
 *
 * Go through the stage 2 page tables and invalidate any cache lines
 * backing memory already mapped to the VM.
 */
static void stage2_flush_vm(struct kvm *kvm)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int idx;

	idx = srcu_read_lock(&kvm->srcu);
	spin_lock(&kvm->mmu_lock);

	slots = kvm_memslots(kvm);
	kvm_for_each_memslot(memslot, slots)
		stage2_flush_memslot(kvm, memslot);

	spin_unlock(&kvm->mmu_lock);
	srcu_read_unlock(&kvm->srcu, idx);
}

static void clear_hyp_pgd_entry(pgd_t *pgd)
{
	p4d_t *p4d_table __maybe_unused = p4d_offset(pgd, 0UL);
	pgd_clear(pgd);
	p4d_free(NULL, p4d_table);
	put_page(virt_to_page(pgd));
}

static void clear_hyp_p4d_entry(p4d_t *p4d)
{
	pud_t *pud_table __maybe_unused = pud_offset(p4d, 0UL);
	VM_BUG_ON(p4d_huge(*p4d));
	p4d_clear(p4d);
	pud_free(NULL, pud_table);
	put_page(virt_to_page(p4d));
}

static void clear_hyp_pud_entry(pud_t *pud)
{
	pmd_t *pmd_table __maybe_unused = pmd_offset(pud, 0);
	VM_BUG_ON(pud_huge(*pud));
	pud_clear(pud);
	pmd_free(NULL, pmd_table);
	put_page(virt_to_page(pud));
}

static void clear_hyp_pmd_entry(pmd_t *pmd)
{
	pte_t *pte_table = pte_offset_kernel(pmd, 0);
	VM_BUG_ON(pmd_thp_or_huge(*pmd));
	pmd_clear(pmd);
	pte_free_kernel(NULL, pte_table);
	put_page(virt_to_page(pmd));
}

static void unmap_hyp_ptes(pmd_t *pmd, phys_addr_t addr, phys_addr_t end)
{
	pte_t *pte, *start_pte;

	start_pte = pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_none(*pte)) {
			kvm_set_pte(pte, __pte(0));
			put_page(virt_to_page(pte));
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);

	if (hyp_pte_table_empty(start_pte))
		clear_hyp_pmd_entry(pmd);
}

static void unmap_hyp_pmds(pud_t *pud, phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t next;
	pmd_t *pmd, *start_pmd;

	start_pmd = pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		/* Hyp doesn't use huge pmds */
		if (!pmd_none(*pmd))
			unmap_hyp_ptes(pmd, addr, next);
	} while (pmd++, addr = next, addr != end);

	if (hyp_pmd_table_empty(start_pmd))
		clear_hyp_pud_entry(pud);
}

static void unmap_hyp_puds(p4d_t *p4d, phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t next;
	pud_t *pud, *start_pud;

	start_pud = pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		/* Hyp doesn't use huge puds */
		if (!pud_none(*pud))
			unmap_hyp_pmds(pud, addr, next);
	} while (pud++, addr = next, addr != end);

	if (hyp_pud_table_empty(start_pud))
		clear_hyp_p4d_entry(p4d);
}

static void unmap_hyp_p4ds(pgd_t *pgd, phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t next;
	p4d_t *p4d, *start_p4d;

	start_p4d = p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		/* Hyp doesn't use huge p4ds */
		if (!p4d_none(*p4d))
			unmap_hyp_puds(p4d, addr, next);
	} while (p4d++, addr = next, addr != end);

	if (hyp_p4d_table_empty(start_p4d))
		clear_hyp_pgd_entry(pgd);
}

static unsigned int kvm_pgd_index(unsigned long addr, unsigned int ptrs_per_pgd)
{
	return (addr >> PGDIR_SHIFT) & (ptrs_per_pgd - 1);
}

static void __unmap_hyp_range(pgd_t *pgdp, unsigned long ptrs_per_pgd,
			      phys_addr_t start, u64 size)
{
	pgd_t *pgd;
	phys_addr_t addr = start, end = start + size;
	phys_addr_t next;

	/*
	 * We don't unmap anything from HYP, except at the hyp tear down.
	 * Hence, we don't have to invalidate the TLBs here.
	 */
	pgd = pgdp + kvm_pgd_index(addr, ptrs_per_pgd);
	do {
		next = pgd_addr_end(addr, end);
		if (!pgd_none(*pgd))
			unmap_hyp_p4ds(pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}

static void unmap_hyp_range(pgd_t *pgdp, phys_addr_t start, u64 size)
{
	__unmap_hyp_range(pgdp, PTRS_PER_PGD, start, size);
}

static void unmap_hyp_idmap_range(pgd_t *pgdp, phys_addr_t start, u64 size)
{
	__unmap_hyp_range(pgdp, __kvm_idmap_ptrs_per_pgd(), start, size);
}

/**
 * free_hyp_pgds - free Hyp-mode page tables
 *
 * Assumes hyp_pgd is a page table used strictly in Hyp-mode and
 * therefore contains either mappings in the kernel memory area (above
 * PAGE_OFFSET), or device mappings in the idmap range.
 *
 * boot_hyp_pgd should only map the idmap range, and is only used in
 * the extended idmap case.
 */
void free_hyp_pgds(void)
{
	pgd_t *id_pgd;

	mutex_lock(&kvm_hyp_pgd_mutex);

	id_pgd = boot_hyp_pgd ? boot_hyp_pgd : hyp_pgd;

	if (id_pgd) {
		/* In case we never called hyp_mmu_init() */
		if (!io_map_base)
			io_map_base = hyp_idmap_start;
		unmap_hyp_idmap_range(id_pgd, io_map_base,
				      hyp_idmap_start + PAGE_SIZE - io_map_base);
	}

	if (boot_hyp_pgd) {
		free_pages((unsigned long)boot_hyp_pgd, hyp_pgd_order);
		boot_hyp_pgd = NULL;
	}

	if (hyp_pgd) {
		unmap_hyp_range(hyp_pgd, kern_hyp_va(PAGE_OFFSET),
				(uintptr_t)high_memory - PAGE_OFFSET);

		free_pages((unsigned long)hyp_pgd, hyp_pgd_order);
		hyp_pgd = NULL;
	}
	if (merged_hyp_pgd) {
		clear_page(merged_hyp_pgd);
		free_page((unsigned long)merged_hyp_pgd);
		merged_hyp_pgd = NULL;
	}

	mutex_unlock(&kvm_hyp_pgd_mutex);
}

static void create_hyp_pte_mappings(pmd_t *pmd, unsigned long start,
				    unsigned long end, unsigned long pfn,
				    pgprot_t prot)
{
	pte_t *pte;
	unsigned long addr;

	addr = start;
	do {
		pte = pte_offset_kernel(pmd, addr);
		kvm_set_pte(pte, kvm_pfn_pte(pfn, prot));
		get_page(virt_to_page(pte));
		pfn++;
	} while (addr += PAGE_SIZE, addr != end);
}

static int create_hyp_pmd_mappings(pud_t *pud, unsigned long start,
				   unsigned long end, unsigned long pfn,
				   pgprot_t prot)
{
	pmd_t *pmd;
	pte_t *pte;
	unsigned long addr, next;

	addr = start;
	do {
		pmd = pmd_offset(pud, addr);

		BUG_ON(pmd_sect(*pmd));

		if (pmd_none(*pmd)) {
			pte = pte_alloc_one_kernel(NULL);
			if (!pte) {
				kvm_err("Cannot allocate Hyp pte\n");
				return -ENOMEM;
			}
			kvm_pmd_populate(pmd, pte);
			get_page(virt_to_page(pmd));
		}

		next = pmd_addr_end(addr, end);

		create_hyp_pte_mappings(pmd, addr, next, pfn, prot);
		pfn += (next - addr) >> PAGE_SHIFT;
	} while (addr = next, addr != end);

	return 0;
}

static int create_hyp_pud_mappings(p4d_t *p4d, unsigned long start,
				   unsigned long end, unsigned long pfn,
				   pgprot_t prot)
{
	pud_t *pud;
	pmd_t *pmd;
	unsigned long addr, next;
	int ret;

	addr = start;
	do {
		pud = pud_offset(p4d, addr);

		if (pud_none_or_clear_bad(pud)) {
			pmd = pmd_alloc_one(NULL, addr);
			if (!pmd) {
				kvm_err("Cannot allocate Hyp pmd\n");
				return -ENOMEM;
			}
			kvm_pud_populate(pud, pmd);
			get_page(virt_to_page(pud));
		}

		next = pud_addr_end(addr, end);
		ret = create_hyp_pmd_mappings(pud, addr, next, pfn, prot);
		if (ret)
			return ret;
		pfn += (next - addr) >> PAGE_SHIFT;
	} while (addr = next, addr != end);

	return 0;
}

static int create_hyp_p4d_mappings(pgd_t *pgd, unsigned long start,
				   unsigned long end, unsigned long pfn,
				   pgprot_t prot)
{
	p4d_t *p4d;
	pud_t *pud;
	unsigned long addr, next;
	int ret;

	addr = start;
	do {
		p4d = p4d_offset(pgd, addr);

		if (p4d_none(*p4d)) {
			pud = pud_alloc_one(NULL, addr);
			if (!pud) {
				kvm_err("Cannot allocate Hyp pud\n");
				return -ENOMEM;
			}
			kvm_p4d_populate(p4d, pud);
			get_page(virt_to_page(p4d));
		}

		next = p4d_addr_end(addr, end);
		ret = create_hyp_pud_mappings(p4d, addr, next, pfn, prot);
		if (ret)
			return ret;
		pfn += (next - addr) >> PAGE_SHIFT;
	} while (addr = next, addr != end);

	return 0;
}

static int __create_hyp_mappings(pgd_t *pgdp, unsigned long ptrs_per_pgd,
				 unsigned long start, unsigned long end,
				 unsigned long pfn, pgprot_t prot)
{
	pgd_t *pgd;
	p4d_t *p4d;
	unsigned long addr, next;
	int err = 0;

	mutex_lock(&kvm_hyp_pgd_mutex);
	addr = start & PAGE_MASK;
	end = PAGE_ALIGN(end);
	do {
		pgd = pgdp + kvm_pgd_index(addr, ptrs_per_pgd);

		if (pgd_none(*pgd)) {
			p4d = p4d_alloc_one(NULL, addr);
			if (!p4d) {
				kvm_err("Cannot allocate Hyp p4d\n");
				err = -ENOMEM;
				goto out;
			}
			kvm_pgd_populate(pgd, p4d);
			get_page(virt_to_page(pgd));
		}

		next = pgd_addr_end(addr, end);
		err = create_hyp_p4d_mappings(pgd, addr, next, pfn, prot);
		if (err)
			goto out;
		pfn += (next - addr) >> PAGE_SHIFT;
	} while (addr = next, addr != end);
out:
	mutex_unlock(&kvm_hyp_pgd_mutex);
	return err;
}

static phys_addr_t kvm_kaddr_to_phys(void *kaddr)
{
	if (!is_vmalloc_addr(kaddr)) {
		BUG_ON(!virt_addr_valid(kaddr));
		return __pa(kaddr);
	} else {
		return page_to_phys(vmalloc_to_page(kaddr)) +
		       offset_in_page(kaddr);
	}
}

/**
 * create_hyp_mappings - duplicate a kernel virtual address range in Hyp mode
 * @from:	The virtual kernel start address of the range
 * @to:		The virtual kernel end address of the range (exclusive)
 * @prot:	The protection to be applied to this range
 *
 * The same virtual address as the kernel virtual address is also used
 * in Hyp-mode mapping (modulo HYP_PAGE_OFFSET) to the same underlying
 * physical pages.
 */
int create_hyp_mappings(void *from, void *to, pgprot_t prot)
{
	phys_addr_t phys_addr;
	unsigned long virt_addr;
	unsigned long start = kern_hyp_va((unsigned long)from);
	unsigned long end = kern_hyp_va((unsigned long)to);

	if (is_kernel_in_hyp_mode())
		return 0;

	start = start & PAGE_MASK;
	end = PAGE_ALIGN(end);

	for (virt_addr = start; virt_addr < end; virt_addr += PAGE_SIZE) {
		int err;

		phys_addr = kvm_kaddr_to_phys(from + virt_addr - start);
		err = __create_hyp_mappings(hyp_pgd, PTRS_PER_PGD,
					    virt_addr, virt_addr + PAGE_SIZE,
					    __phys_to_pfn(phys_addr),
					    prot);
		if (err)
			return err;
	}

	return 0;
}

static int __create_hyp_private_mapping(phys_addr_t phys_addr, size_t size,
					unsigned long *haddr, pgprot_t prot)
{
	pgd_t *pgd = hyp_pgd;
	unsigned long base;
	int ret = 0;

	mutex_lock(&kvm_hyp_pgd_mutex);

	/*
	 * This assumes that we have enough space below the idmap
	 * page to allocate our VAs. If not, the check below will
	 * kick. A potential alternative would be to detect that
	 * overflow and switch to an allocation above the idmap.
	 *
	 * The allocated size is always a multiple of PAGE_SIZE.
	 */
	size = PAGE_ALIGN(size + offset_in_page(phys_addr));
	base = io_map_base - size;

	/*
	 * Verify that BIT(VA_BITS - 1) hasn't been flipped by
	 * allocating the new area, as it would indicate we've
	 * overflowed the idmap/IO address range.
	 */
	if ((base ^ io_map_base) & BIT(VA_BITS - 1))
		ret = -ENOMEM;
	else
		io_map_base = base;

	mutex_unlock(&kvm_hyp_pgd_mutex);

	if (ret)
		goto out;

	if (__kvm_cpu_uses_extended_idmap())
		pgd = boot_hyp_pgd;

	ret = __create_hyp_mappings(pgd, __kvm_idmap_ptrs_per_pgd(),
				    base, base + size,
				    __phys_to_pfn(phys_addr), prot);
	if (ret)
		goto out;

	*haddr = base + offset_in_page(phys_addr);

out:
	return ret;
}

/**
 * create_hyp_io_mappings - Map IO into both kernel and HYP
 * @phys_addr:	The physical start address which gets mapped
 * @size:	Size of the region being mapped
 * @kaddr:	Kernel VA for this mapping
 * @haddr:	HYP VA for this mapping
 */
int create_hyp_io_mappings(phys_addr_t phys_addr, size_t size,
			   void __iomem **kaddr,
			   void __iomem **haddr)
{
	unsigned long addr;
	int ret;

	*kaddr = ioremap(phys_addr, size);
	if (!*kaddr)
		return -ENOMEM;

	if (is_kernel_in_hyp_mode()) {
		*haddr = *kaddr;
		return 0;
	}

	ret = __create_hyp_private_mapping(phys_addr, size,
					   &addr, PAGE_HYP_DEVICE);
	if (ret) {
		iounmap(*kaddr);
		*kaddr = NULL;
		*haddr = NULL;
		return ret;
	}

	*haddr = (void __iomem *)addr;
	return 0;
}

/**
 * create_hyp_exec_mappings - Map an executable range into HYP
 * @phys_addr:	The physical start address which gets mapped
 * @size:	Size of the region being mapped
 * @haddr:	HYP VA for this mapping
 */
int create_hyp_exec_mappings(phys_addr_t phys_addr, size_t size,
			     void **haddr)
{
	unsigned long addr;
	int ret;

	BUG_ON(is_kernel_in_hyp_mode());

	ret = __create_hyp_private_mapping(phys_addr, size,
					   &addr, PAGE_HYP_EXEC);
	if (ret) {
		*haddr = NULL;
		return ret;
	}

	*haddr = (void *)addr;
	return 0;
}

/**
 * kvm_init_stage2_mmu - Initialise a S2 MMU strucrure
 * @kvm:	The pointer to the KVM structure
 * @mmu:	The pointer to the s2 MMU structure
 *
 * Allocates only the stage-2 HW PGD level table(s) of size defined by
 * stage2_pgd_size(mmu->kvm).
 *
 * Note we don't need locking here as this is only called when the VM is
 * created, which can only be done once.
 */
int kvm_init_stage2_mmu(struct kvm *kvm, struct kvm_s2_mmu *mmu)
{
	phys_addr_t pgd_phys;
	pgd_t *pgd;
	int cpu;

	if (mmu->pgd != NULL) {
		kvm_err("kvm_arch already initialized?\n");
		return -EINVAL;
	}

	/* Allocate the HW PGD, making sure that each page gets its own refcount */
	pgd = alloc_pages_exact(stage2_pgd_size(kvm), GFP_KERNEL | __GFP_ZERO);
	if (!pgd)
		return -ENOMEM;

	pgd_phys = virt_to_phys(pgd);
	if (WARN_ON(pgd_phys & ~kvm_vttbr_baddr_mask(kvm)))
		return -EINVAL;

	mmu->last_vcpu_ran = alloc_percpu(typeof(*mmu->last_vcpu_ran));
	if (!mmu->last_vcpu_ran) {
		free_pages_exact(pgd, stage2_pgd_size(kvm));
		return -ENOMEM;
	}

	for_each_possible_cpu(cpu)
		*per_cpu_ptr(mmu->last_vcpu_ran, cpu) = -1;

	mmu->kvm = kvm;
	mmu->pgd = pgd;
	mmu->pgd_phys = pgd_phys;
	mmu->vmid.vmid_gen = 0;

	return 0;
}

static void stage2_unmap_memslot(struct kvm *kvm,
				 struct kvm_memory_slot *memslot)
{
	hva_t hva = memslot->userspace_addr;
	phys_addr_t addr = memslot->base_gfn << PAGE_SHIFT;
	phys_addr_t size = PAGE_SIZE * memslot->npages;
	hva_t reg_end = hva + size;

	/*
	 * A memory region could potentially cover multiple VMAs, and any holes
	 * between them, so iterate over all of them to find out if we should
	 * unmap any of them.
	 *
	 *     +--------------------------------------------+
	 * +---------------+----------------+   +----------------+
	 * |   : VMA 1     |      VMA 2     |   |    VMA 3  :    |
	 * +---------------+----------------+   +----------------+
	 *     |               memory region                |
	 *     +--------------------------------------------+
	 */
	do {
		struct vm_area_struct *vma = find_vma(current->mm, hva);
		hva_t vm_start, vm_end;

		if (!vma || vma->vm_start >= reg_end)
			break;

		/*
		 * Take the intersection of this VMA with the memory region
		 */
		vm_start = max(hva, vma->vm_start);
		vm_end = min(reg_end, vma->vm_end);

		if (!(vma->vm_flags & VM_PFNMAP)) {
			gpa_t gpa = addr + (vm_start - memslot->userspace_addr);
			unmap_stage2_range(&kvm->arch.mmu, gpa, vm_end - vm_start);
		}
		hva = vm_end;
	} while (hva < reg_end);
}

/**
 * stage2_unmap_vm - Unmap Stage-2 RAM mappings
 * @kvm: The struct kvm pointer
 *
 * Go through the memregions and unmap any regular RAM
 * backing memory already mapped to the VM.
 */
void stage2_unmap_vm(struct kvm *kvm)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int idx;

	idx = srcu_read_lock(&kvm->srcu);
	mmap_read_lock(current->mm);
	spin_lock(&kvm->mmu_lock);

	slots = kvm_memslots(kvm);
	kvm_for_each_memslot(memslot, slots)
		stage2_unmap_memslot(kvm, memslot);

	spin_unlock(&kvm->mmu_lock);
	mmap_read_unlock(current->mm);
	srcu_read_unlock(&kvm->srcu, idx);
}

void kvm_free_stage2_pgd(struct kvm_s2_mmu *mmu)
{
	struct kvm *kvm = mmu->kvm;
	void *pgd = NULL;

	spin_lock(&kvm->mmu_lock);
	if (mmu->pgd) {
		unmap_stage2_range(mmu, 0, kvm_phys_size(kvm));
		pgd = READ_ONCE(mmu->pgd);
		mmu->pgd = NULL;
	}
	spin_unlock(&kvm->mmu_lock);

	/* Free the HW pgd, one page at a time */
	if (pgd) {
		free_pages_exact(pgd, stage2_pgd_size(kvm));
		free_percpu(mmu->last_vcpu_ran);
	}
}

static p4d_t *stage2_get_p4d(struct kvm_s2_mmu *mmu, struct kvm_mmu_memory_cache *cache,
			     phys_addr_t addr)
{
	struct kvm *kvm = mmu->kvm;
	pgd_t *pgd;
	p4d_t *p4d;

	pgd = mmu->pgd + stage2_pgd_index(kvm, addr);
	if (stage2_pgd_none(kvm, *pgd)) {
		if (!cache)
			return NULL;
		p4d = kvm_mmu_memory_cache_alloc(cache);
		stage2_pgd_populate(kvm, pgd, p4d);
		get_page(virt_to_page(pgd));
	}

	return stage2_p4d_offset(kvm, pgd, addr);
}

static pud_t *stage2_get_pud(struct kvm_s2_mmu *mmu, struct kvm_mmu_memory_cache *cache,
			     phys_addr_t addr)
{
	struct kvm *kvm = mmu->kvm;
	p4d_t *p4d;
	pud_t *pud;

	p4d = stage2_get_p4d(mmu, cache, addr);
	if (stage2_p4d_none(kvm, *p4d)) {
		if (!cache)
			return NULL;
		pud = kvm_mmu_memory_cache_alloc(cache);
		stage2_p4d_populate(kvm, p4d, pud);
		get_page(virt_to_page(p4d));
	}

	return stage2_pud_offset(kvm, p4d, addr);
}

static pmd_t *stage2_get_pmd(struct kvm_s2_mmu *mmu, struct kvm_mmu_memory_cache *cache,
			     phys_addr_t addr)
{
	struct kvm *kvm = mmu->kvm;
	pud_t *pud;
	pmd_t *pmd;

	pud = stage2_get_pud(mmu, cache, addr);
	if (!pud || stage2_pud_huge(kvm, *pud))
		return NULL;

	if (stage2_pud_none(kvm, *pud)) {
		if (!cache)
			return NULL;
		pmd = kvm_mmu_memory_cache_alloc(cache);
		stage2_pud_populate(kvm, pud, pmd);
		get_page(virt_to_page(pud));
	}

	return stage2_pmd_offset(kvm, pud, addr);
}

static int stage2_set_pmd_huge(struct kvm_s2_mmu *mmu,
			       struct kvm_mmu_memory_cache *cache,
			       phys_addr_t addr, const pmd_t *new_pmd)
{
	pmd_t *pmd, old_pmd;

retry:
	pmd = stage2_get_pmd(mmu, cache, addr);
	VM_BUG_ON(!pmd);

	old_pmd = *pmd;
	/*
	 * Multiple vcpus faulting on the same PMD entry, can
	 * lead to them sequentially updating the PMD with the
	 * same value. Following the break-before-make
	 * (pmd_clear() followed by tlb_flush()) process can
	 * hinder forward progress due to refaults generated
	 * on missing translations.
	 *
	 * Skip updating the page table if the entry is
	 * unchanged.
	 */
	if (pmd_val(old_pmd) == pmd_val(*new_pmd))
		return 0;

	if (pmd_present(old_pmd)) {
		/*
		 * If we already have PTE level mapping for this block,
		 * we must unmap it to avoid inconsistent TLB state and
		 * leaking the table page. We could end up in this situation
		 * if the memory slot was marked for dirty logging and was
		 * reverted, leaving PTE level mappings for the pages accessed
		 * during the period. So, unmap the PTE level mapping for this
		 * block and retry, as we could have released the upper level
		 * table in the process.
		 *
		 * Normal THP split/merge follows mmu_notifier callbacks and do
		 * get handled accordingly.
		 */
		if (!pmd_thp_or_huge(old_pmd)) {
			unmap_stage2_range(mmu, addr & S2_PMD_MASK, S2_PMD_SIZE);
			goto retry;
		}
		/*
		 * Mapping in huge pages should only happen through a
		 * fault.  If a page is merged into a transparent huge
		 * page, the individual subpages of that huge page
		 * should be unmapped through MMU notifiers before we
		 * get here.
		 *
		 * Merging of CompoundPages is not supported; they
		 * should become splitting first, unmapped, merged,
		 * and mapped back in on-demand.
		 */
		WARN_ON_ONCE(pmd_pfn(old_pmd) != pmd_pfn(*new_pmd));
		pmd_clear(pmd);
		kvm_tlb_flush_vmid_ipa(mmu, addr, S2_PMD_LEVEL);
	} else {
		get_page(virt_to_page(pmd));
	}

	kvm_set_pmd(pmd, *new_pmd);
	return 0;
}

static int stage2_set_pud_huge(struct kvm_s2_mmu *mmu,
			       struct kvm_mmu_memory_cache *cache,
			       phys_addr_t addr, const pud_t *new_pudp)
{
	struct kvm *kvm = mmu->kvm;
	pud_t *pudp, old_pud;

retry:
	pudp = stage2_get_pud(mmu, cache, addr);
	VM_BUG_ON(!pudp);

	old_pud = *pudp;

	/*
	 * A large number of vcpus faulting on the same stage 2 entry,
	 * can lead to a refault due to the stage2_pud_clear()/tlb_flush().
	 * Skip updating the page tables if there is no change.
	 */
	if (pud_val(old_pud) == pud_val(*new_pudp))
		return 0;

	if (stage2_pud_present(kvm, old_pud)) {
		/*
		 * If we already have table level mapping for this block, unmap
		 * the range for this block and retry.
		 */
		if (!stage2_pud_huge(kvm, old_pud)) {
			unmap_stage2_range(mmu, addr & S2_PUD_MASK, S2_PUD_SIZE);
			goto retry;
		}

		WARN_ON_ONCE(kvm_pud_pfn(old_pud) != kvm_pud_pfn(*new_pudp));
		stage2_pud_clear(kvm, pudp);
		kvm_tlb_flush_vmid_ipa(mmu, addr, S2_PUD_LEVEL);
	} else {
		get_page(virt_to_page(pudp));
	}

	kvm_set_pud(pudp, *new_pudp);
	return 0;
}

/*
 * stage2_get_leaf_entry - walk the stage2 VM page tables and return
 * true if a valid and present leaf-entry is found. A pointer to the
 * leaf-entry is returned in the appropriate level variable - pudpp,
 * pmdpp, ptepp.
 */
static bool stage2_get_leaf_entry(struct kvm_s2_mmu *mmu, phys_addr_t addr,
				  pud_t **pudpp, pmd_t **pmdpp, pte_t **ptepp)
{
	struct kvm *kvm = mmu->kvm;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	*pudpp = NULL;
	*pmdpp = NULL;
	*ptepp = NULL;

	pudp = stage2_get_pud(mmu, NULL, addr);
	if (!pudp || stage2_pud_none(kvm, *pudp) || !stage2_pud_present(kvm, *pudp))
		return false;

	if (stage2_pud_huge(kvm, *pudp)) {
		*pudpp = pudp;
		return true;
	}

	pmdp = stage2_pmd_offset(kvm, pudp, addr);
	if (!pmdp || pmd_none(*pmdp) || !pmd_present(*pmdp))
		return false;

	if (pmd_thp_or_huge(*pmdp)) {
		*pmdpp = pmdp;
		return true;
	}

	ptep = pte_offset_kernel(pmdp, addr);
	if (!ptep || pte_none(*ptep) || !pte_present(*ptep))
		return false;

	*ptepp = ptep;
	return true;
}

static bool stage2_is_exec(struct kvm_s2_mmu *mmu, phys_addr_t addr, unsigned long sz)
{
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	bool found;

	found = stage2_get_leaf_entry(mmu, addr, &pudp, &pmdp, &ptep);
	if (!found)
		return false;

	if (pudp)
		return sz <= PUD_SIZE && kvm_s2pud_exec(pudp);
	else if (pmdp)
		return sz <= PMD_SIZE && kvm_s2pmd_exec(pmdp);
	else
		return sz == PAGE_SIZE && kvm_s2pte_exec(ptep);
}

static int stage2_set_pte(struct kvm_s2_mmu *mmu,
			  struct kvm_mmu_memory_cache *cache,
			  phys_addr_t addr, const pte_t *new_pte,
			  unsigned long flags)
{
	struct kvm *kvm = mmu->kvm;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte, old_pte;
	bool iomap = flags & KVM_S2PTE_FLAG_IS_IOMAP;
	bool logging_active = flags & KVM_S2_FLAG_LOGGING_ACTIVE;

	VM_BUG_ON(logging_active && !cache);

	/* Create stage-2 page table mapping - Levels 0 and 1 */
	pud = stage2_get_pud(mmu, cache, addr);
	if (!pud) {
		/*
		 * Ignore calls from kvm_set_spte_hva for unallocated
		 * address ranges.
		 */
		return 0;
	}

	/*
	 * While dirty page logging - dissolve huge PUD, then continue
	 * on to allocate page.
	 */
	if (logging_active)
		stage2_dissolve_pud(mmu, addr, pud);

	if (stage2_pud_none(kvm, *pud)) {
		if (!cache)
			return 0; /* ignore calls from kvm_set_spte_hva */
		pmd = kvm_mmu_memory_cache_alloc(cache);
		stage2_pud_populate(kvm, pud, pmd);
		get_page(virt_to_page(pud));
	}

	pmd = stage2_pmd_offset(kvm, pud, addr);
	if (!pmd) {
		/*
		 * Ignore calls from kvm_set_spte_hva for unallocated
		 * address ranges.
		 */
		return 0;
	}

	/*
	 * While dirty page logging - dissolve huge PMD, then continue on to
	 * allocate page.
	 */
	if (logging_active)
		stage2_dissolve_pmd(mmu, addr, pmd);

	/* Create stage-2 page mappings - Level 2 */
	if (pmd_none(*pmd)) {
		if (!cache)
			return 0; /* ignore calls from kvm_set_spte_hva */
		pte = kvm_mmu_memory_cache_alloc(cache);
		kvm_pmd_populate(pmd, pte);
		get_page(virt_to_page(pmd));
	}

	pte = pte_offset_kernel(pmd, addr);

	if (iomap && pte_present(*pte))
		return -EFAULT;

	/* Create 2nd stage page table mapping - Level 3 */
	old_pte = *pte;
	if (pte_present(old_pte)) {
		/* Skip page table update if there is no change */
		if (pte_val(old_pte) == pte_val(*new_pte))
			return 0;

		kvm_set_pte(pte, __pte(0));
		kvm_tlb_flush_vmid_ipa(mmu, addr, S2_PTE_LEVEL);
	} else {
		get_page(virt_to_page(pte));
	}

	kvm_set_pte(pte, *new_pte);
	return 0;
}

#ifndef __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static int stage2_ptep_test_and_clear_young(pte_t *pte)
{
	if (pte_young(*pte)) {
		*pte = pte_mkold(*pte);
		return 1;
	}
	return 0;
}
#else
static int stage2_ptep_test_and_clear_young(pte_t *pte)
{
	return __ptep_test_and_clear_young(pte);
}
#endif

static int stage2_pmdp_test_and_clear_young(pmd_t *pmd)
{
	return stage2_ptep_test_and_clear_young((pte_t *)pmd);
}

static int stage2_pudp_test_and_clear_young(pud_t *pud)
{
	return stage2_ptep_test_and_clear_young((pte_t *)pud);
}

/**
 * kvm_phys_addr_ioremap - map a device range to guest IPA
 *
 * @kvm:	The KVM pointer
 * @guest_ipa:	The IPA at which to insert the mapping
 * @pa:		The physical address of the device
 * @size:	The size of the mapping
 */
int kvm_phys_addr_ioremap(struct kvm *kvm, phys_addr_t guest_ipa,
			  phys_addr_t pa, unsigned long size, bool writable)
{
	phys_addr_t addr, end;
	int ret = 0;
	unsigned long pfn;
	struct kvm_mmu_memory_cache cache = { 0, __GFP_ZERO, NULL, };

	end = (guest_ipa + size + PAGE_SIZE - 1) & PAGE_MASK;
	pfn = __phys_to_pfn(pa);

	for (addr = guest_ipa; addr < end; addr += PAGE_SIZE) {
		pte_t pte = kvm_pfn_pte(pfn, PAGE_S2_DEVICE);

		if (writable)
			pte = kvm_s2pte_mkwrite(pte);

		ret = kvm_mmu_topup_memory_cache(&cache,
						 kvm_mmu_cache_min_pages(kvm));
		if (ret)
			goto out;
		spin_lock(&kvm->mmu_lock);
		ret = stage2_set_pte(&kvm->arch.mmu, &cache, addr, &pte,
				     KVM_S2PTE_FLAG_IS_IOMAP);
		spin_unlock(&kvm->mmu_lock);
		if (ret)
			goto out;

		pfn++;
	}

out:
	kvm_mmu_free_memory_cache(&cache);
	return ret;
}

/**
 * stage2_wp_ptes - write protect PMD range
 * @pmd:	pointer to pmd entry
 * @addr:	range start address
 * @end:	range end address
 */
static void stage2_wp_ptes(pmd_t *pmd, phys_addr_t addr, phys_addr_t end)
{
	pte_t *pte;

	pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_none(*pte)) {
			if (!kvm_s2pte_readonly(pte))
				kvm_set_s2pte_readonly(pte);
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);
}

/**
 * stage2_wp_pmds - write protect PUD range
 * kvm:		kvm instance for the VM
 * @pud:	pointer to pud entry
 * @addr:	range start address
 * @end:	range end address
 */
static void stage2_wp_pmds(struct kvm_s2_mmu *mmu, pud_t *pud,
			   phys_addr_t addr, phys_addr_t end)
{
	struct kvm *kvm = mmu->kvm;
	pmd_t *pmd;
	phys_addr_t next;

	pmd = stage2_pmd_offset(kvm, pud, addr);

	do {
		next = stage2_pmd_addr_end(kvm, addr, end);
		if (!pmd_none(*pmd)) {
			if (pmd_thp_or_huge(*pmd)) {
				if (!kvm_s2pmd_readonly(pmd))
					kvm_set_s2pmd_readonly(pmd);
			} else {
				stage2_wp_ptes(pmd, addr, next);
			}
		}
	} while (pmd++, addr = next, addr != end);
}

/**
 * stage2_wp_puds - write protect P4D range
 * @p4d:	pointer to p4d entry
 * @addr:	range start address
 * @end:	range end address
 */
static void  stage2_wp_puds(struct kvm_s2_mmu *mmu, p4d_t *p4d,
			    phys_addr_t addr, phys_addr_t end)
{
	struct kvm *kvm = mmu->kvm;
	pud_t *pud;
	phys_addr_t next;

	pud = stage2_pud_offset(kvm, p4d, addr);
	do {
		next = stage2_pud_addr_end(kvm, addr, end);
		if (!stage2_pud_none(kvm, *pud)) {
			if (stage2_pud_huge(kvm, *pud)) {
				if (!kvm_s2pud_readonly(pud))
					kvm_set_s2pud_readonly(pud);
			} else {
				stage2_wp_pmds(mmu, pud, addr, next);
			}
		}
	} while (pud++, addr = next, addr != end);
}

/**
 * stage2_wp_p4ds - write protect PGD range
 * @pgd:	pointer to pgd entry
 * @addr:	range start address
 * @end:	range end address
 */
static void  stage2_wp_p4ds(struct kvm_s2_mmu *mmu, pgd_t *pgd,
			    phys_addr_t addr, phys_addr_t end)
{
	struct kvm *kvm = mmu->kvm;
	p4d_t *p4d;
	phys_addr_t next;

	p4d = stage2_p4d_offset(kvm, pgd, addr);
	do {
		next = stage2_p4d_addr_end(kvm, addr, end);
		if (!stage2_p4d_none(kvm, *p4d))
			stage2_wp_puds(mmu, p4d, addr, next);
	} while (p4d++, addr = next, addr != end);
}

/**
 * stage2_wp_range() - write protect stage2 memory region range
 * @kvm:	The KVM pointer
 * @addr:	Start address of range
 * @end:	End address of range
 */
static void stage2_wp_range(struct kvm_s2_mmu *mmu, phys_addr_t addr, phys_addr_t end)
{
	struct kvm *kvm = mmu->kvm;
	pgd_t *pgd;
	phys_addr_t next;

	pgd = mmu->pgd + stage2_pgd_index(kvm, addr);
	do {
		/*
		 * Release kvm_mmu_lock periodically if the memory region is
		 * large. Otherwise, we may see kernel panics with
		 * CONFIG_DETECT_HUNG_TASK, CONFIG_LOCKUP_DETECTOR,
		 * CONFIG_LOCKDEP. Additionally, holding the lock too long
		 * will also starve other vCPUs. We have to also make sure
		 * that the page tables are not freed while we released
		 * the lock.
		 */
		cond_resched_lock(&kvm->mmu_lock);
		if (!READ_ONCE(mmu->pgd))
			break;
		next = stage2_pgd_addr_end(kvm, addr, end);
		if (stage2_pgd_present(kvm, *pgd))
			stage2_wp_p4ds(mmu, pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}

/**
 * kvm_mmu_wp_memory_region() - write protect stage 2 entries for memory slot
 * @kvm:	The KVM pointer
 * @slot:	The memory slot to write protect
 *
 * Called to start logging dirty pages after memory region
 * KVM_MEM_LOG_DIRTY_PAGES operation is called. After this function returns
 * all present PUD, PMD and PTEs are write protected in the memory region.
 * Afterwards read of dirty page log can be called.
 *
 * Acquires kvm_mmu_lock. Called with kvm->slots_lock mutex acquired,
 * serializing operations for VM memory regions.
 */
void kvm_mmu_wp_memory_region(struct kvm *kvm, int slot)
{
	struct kvm_memslots *slots = kvm_memslots(kvm);
	struct kvm_memory_slot *memslot = id_to_memslot(slots, slot);
	phys_addr_t start, end;

	if (WARN_ON_ONCE(!memslot))
		return;

	start = memslot->base_gfn << PAGE_SHIFT;
	end = (memslot->base_gfn + memslot->npages) << PAGE_SHIFT;

	spin_lock(&kvm->mmu_lock);
	stage2_wp_range(&kvm->arch.mmu, start, end);
	spin_unlock(&kvm->mmu_lock);
	kvm_flush_remote_tlbs(kvm);
}

/**
 * kvm_mmu_write_protect_pt_masked() - write protect dirty pages
 * @kvm:	The KVM pointer
 * @slot:	The memory slot associated with mask
 * @gfn_offset:	The gfn offset in memory slot
 * @mask:	The mask of dirty pages at offset 'gfn_offset' in this memory
 *		slot to be write protected
 *
 * Walks bits set in mask write protects the associated pte's. Caller must
 * acquire kvm_mmu_lock.
 */
static void kvm_mmu_write_protect_pt_masked(struct kvm *kvm,
		struct kvm_memory_slot *slot,
		gfn_t gfn_offset, unsigned long mask)
{
	phys_addr_t base_gfn = slot->base_gfn + gfn_offset;
	phys_addr_t start = (base_gfn +  __ffs(mask)) << PAGE_SHIFT;
	phys_addr_t end = (base_gfn + __fls(mask) + 1) << PAGE_SHIFT;

	stage2_wp_range(&kvm->arch.mmu, start, end);
}

/*
 * kvm_arch_mmu_enable_log_dirty_pt_masked - enable dirty logging for selected
 * dirty pages.
 *
 * It calls kvm_mmu_write_protect_pt_masked to write protect selected pages to
 * enable dirty logging for them.
 */
void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *kvm,
		struct kvm_memory_slot *slot,
		gfn_t gfn_offset, unsigned long mask)
{
	kvm_mmu_write_protect_pt_masked(kvm, slot, gfn_offset, mask);
}

static void clean_dcache_guest_page(kvm_pfn_t pfn, unsigned long size)
{
	__clean_dcache_guest_page(pfn, size);
}

static void invalidate_icache_guest_page(kvm_pfn_t pfn, unsigned long size)
{
	__invalidate_icache_guest_page(pfn, size);
}

static void kvm_send_hwpoison_signal(unsigned long address, short lsb)
{
	send_sig_mceerr(BUS_MCEERR_AR, (void __user *)address, lsb, current);
}

static bool fault_supports_stage2_huge_mapping(struct kvm_memory_slot *memslot,
					       unsigned long hva,
					       unsigned long map_size)
{
	gpa_t gpa_start;
	hva_t uaddr_start, uaddr_end;
	size_t size;

	/* The memslot and the VMA are guaranteed to be aligned to PAGE_SIZE */
	if (map_size == PAGE_SIZE)
		return true;

	size = memslot->npages * PAGE_SIZE;

	gpa_start = memslot->base_gfn << PAGE_SHIFT;

	uaddr_start = memslot->userspace_addr;
	uaddr_end = uaddr_start + size;

	/*
	 * Pages belonging to memslots that don't have the same alignment
	 * within a PMD/PUD for userspace and IPA cannot be mapped with stage-2
	 * PMD/PUD entries, because we'll end up mapping the wrong pages.
	 *
	 * Consider a layout like the following:
	 *
	 *    memslot->userspace_addr:
	 *    +-----+--------------------+--------------------+---+
	 *    |abcde|fgh  Stage-1 block  |    Stage-1 block tv|xyz|
	 *    +-----+--------------------+--------------------+---+
	 *
	 *    memslot->base_gfn << PAGE_SHIFT:
	 *      +---+--------------------+--------------------+-----+
	 *      |abc|def  Stage-2 block  |    Stage-2 block   |tvxyz|
	 *      +---+--------------------+--------------------+-----+
	 *
	 * If we create those stage-2 blocks, we'll end up with this incorrect
	 * mapping:
	 *   d -> f
	 *   e -> g
	 *   f -> h
	 */
	if ((gpa_start & (map_size - 1)) != (uaddr_start & (map_size - 1)))
		return false;

	/*
	 * Next, let's make sure we're not trying to map anything not covered
	 * by the memslot. This means we have to prohibit block size mappings
	 * for the beginning and end of a non-block aligned and non-block sized
	 * memory slot (illustrated by the head and tail parts of the
	 * userspace view above containing pages 'abcde' and 'xyz',
	 * respectively).
	 *
	 * Note that it doesn't matter if we do the check using the
	 * userspace_addr or the base_gfn, as both are equally aligned (per
	 * the check above) and equally sized.
	 */
	return (hva & ~(map_size - 1)) >= uaddr_start &&
	       (hva & ~(map_size - 1)) + map_size <= uaddr_end;
}

/*
 * Check if the given hva is backed by a transparent huge page (THP) and
 * whether it can be mapped using block mapping in stage2. If so, adjust
 * the stage2 PFN and IPA accordingly. Only PMD_SIZE THPs are currently
 * supported. This will need to be updated to support other THP sizes.
 *
 * Returns the size of the mapping.
 */
static unsigned long
transparent_hugepage_adjust(struct kvm_memory_slot *memslot,
			    unsigned long hva, kvm_pfn_t *pfnp,
			    phys_addr_t *ipap)
{
	kvm_pfn_t pfn = *pfnp;

	/*
	 * Make sure the adjustment is done only for THP pages. Also make
	 * sure that the HVA and IPA are sufficiently aligned and that the
	 * block map is contained within the memslot.
	 */
	if (kvm_is_transparent_hugepage(pfn) &&
	    fault_supports_stage2_huge_mapping(memslot, hva, PMD_SIZE)) {
		/*
		 * The address we faulted on is backed by a transparent huge
		 * page.  However, because we map the compound huge page and
		 * not the individual tail page, we need to transfer the
		 * refcount to the head page.  We have to be careful that the
		 * THP doesn't start to split while we are adjusting the
		 * refcounts.
		 *
		 * We are sure this doesn't happen, because mmu_notifier_retry
		 * was successful and we are holding the mmu_lock, so if this
		 * THP is trying to split, it will be blocked in the mmu
		 * notifier before touching any of the pages, specifically
		 * before being able to call __split_huge_page_refcount().
		 *
		 * We can therefore safely transfer the refcount from PG_tail
		 * to PG_head and switch the pfn from a tail page to the head
		 * page accordingly.
		 */
		*ipap &= PMD_MASK;
		kvm_release_pfn_clean(pfn);
		pfn &= ~(PTRS_PER_PMD - 1);
		kvm_get_pfn(pfn);
		*pfnp = pfn;

		return PMD_SIZE;
	}

	/* Use page mapping if we cannot use block mapping. */
	return PAGE_SIZE;
}

static int user_mem_abort(struct kvm_vcpu *vcpu, phys_addr_t fault_ipa,
			  struct kvm_memory_slot *memslot, unsigned long hva,
			  unsigned long fault_status)
{
	int ret;
	bool write_fault, writable, force_pte = false;
	bool exec_fault, needs_exec;
	unsigned long mmu_seq;
	gfn_t gfn = fault_ipa >> PAGE_SHIFT;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_memory_cache *memcache = &vcpu->arch.mmu_page_cache;
	struct vm_area_struct *vma;
	short vma_shift;
	kvm_pfn_t pfn;
	pgprot_t mem_type = PAGE_S2;
	bool logging_active = memslot_is_logging(memslot);
	unsigned long vma_pagesize, flags = 0;
	struct kvm_s2_mmu *mmu = vcpu->arch.hw_mmu;

	write_fault = kvm_is_write_fault(vcpu);
	exec_fault = kvm_vcpu_trap_is_iabt(vcpu);
	VM_BUG_ON(write_fault && exec_fault);

	if (fault_status == FSC_PERM && !write_fault && !exec_fault) {
		kvm_err("Unexpected L2 read permission error\n");
		return -EFAULT;
	}

	/* Let's check if we will get back a huge page backed by hugetlbfs */
	mmap_read_lock(current->mm);
	vma = find_vma_intersection(current->mm, hva, hva + 1);
	if (unlikely(!vma)) {
		kvm_err("Failed to find VMA for hva 0x%lx\n", hva);
		mmap_read_unlock(current->mm);
		return -EFAULT;
	}

	if (is_vm_hugetlb_page(vma))
		vma_shift = huge_page_shift(hstate_vma(vma));
	else
		vma_shift = PAGE_SHIFT;

	vma_pagesize = 1ULL << vma_shift;
	if (logging_active ||
	    (vma->vm_flags & VM_PFNMAP) ||
	    !fault_supports_stage2_huge_mapping(memslot, hva, vma_pagesize)) {
		force_pte = true;
		vma_pagesize = PAGE_SIZE;
	}

	/*
	 * The stage2 has a minimum of 2 level table (For arm64 see
	 * kvm_arm_setup_stage2()). Hence, we are guaranteed that we can
	 * use PMD_SIZE huge mappings (even when the PMD is folded into PGD).
	 * As for PUD huge maps, we must make sure that we have at least
	 * 3 levels, i.e, PMD is not folded.
	 */
	if (vma_pagesize == PMD_SIZE ||
	    (vma_pagesize == PUD_SIZE && kvm_stage2_has_pmd(kvm)))
		gfn = (fault_ipa & huge_page_mask(hstate_vma(vma))) >> PAGE_SHIFT;
	mmap_read_unlock(current->mm);

	/* We need minimum second+third level pages */
	ret = kvm_mmu_topup_memory_cache(memcache, kvm_mmu_cache_min_pages(kvm));
	if (ret)
		return ret;

	mmu_seq = vcpu->kvm->mmu_notifier_seq;
	/*
	 * Ensure the read of mmu_notifier_seq happens before we call
	 * gfn_to_pfn_prot (which calls get_user_pages), so that we don't risk
	 * the page we just got a reference to gets unmapped before we have a
	 * chance to grab the mmu_lock, which ensure that if the page gets
	 * unmapped afterwards, the call to kvm_unmap_hva will take it away
	 * from us again properly. This smp_rmb() interacts with the smp_wmb()
	 * in kvm_mmu_notifier_invalidate_<page|range_end>.
	 */
	smp_rmb();

	pfn = gfn_to_pfn_prot(kvm, gfn, write_fault, &writable);
	if (pfn == KVM_PFN_ERR_HWPOISON) {
		kvm_send_hwpoison_signal(hva, vma_shift);
		return 0;
	}
	if (is_error_noslot_pfn(pfn))
		return -EFAULT;

	if (kvm_is_device_pfn(pfn)) {
		mem_type = PAGE_S2_DEVICE;
		flags |= KVM_S2PTE_FLAG_IS_IOMAP;
	} else if (logging_active) {
		/*
		 * Faults on pages in a memslot with logging enabled
		 * should not be mapped with huge pages (it introduces churn
		 * and performance degradation), so force a pte mapping.
		 */
		flags |= KVM_S2_FLAG_LOGGING_ACTIVE;

		/*
		 * Only actually map the page as writable if this was a write
		 * fault.
		 */
		if (!write_fault)
			writable = false;
	}

	if (exec_fault && is_iomap(flags))
		return -ENOEXEC;

	spin_lock(&kvm->mmu_lock);
	if (mmu_notifier_retry(kvm, mmu_seq))
		goto out_unlock;

	/*
	 * If we are not forced to use page mapping, check if we are
	 * backed by a THP and thus use block mapping if possible.
	 */
	if (vma_pagesize == PAGE_SIZE && !force_pte)
		vma_pagesize = transparent_hugepage_adjust(memslot, hva,
							   &pfn, &fault_ipa);
	if (writable)
		kvm_set_pfn_dirty(pfn);

	if (fault_status != FSC_PERM && !is_iomap(flags))
		clean_dcache_guest_page(pfn, vma_pagesize);

	if (exec_fault)
		invalidate_icache_guest_page(pfn, vma_pagesize);

	/*
	 * If we took an execution fault we have made the
	 * icache/dcache coherent above and should now let the s2
	 * mapping be executable.
	 *
	 * Write faults (!exec_fault && FSC_PERM) are orthogonal to
	 * execute permissions, and we preserve whatever we have.
	 */
	needs_exec = exec_fault ||
		(fault_status == FSC_PERM &&
		 stage2_is_exec(mmu, fault_ipa, vma_pagesize));

	if (vma_pagesize == PUD_SIZE) {
		pud_t new_pud = kvm_pfn_pud(pfn, mem_type);

		new_pud = kvm_pud_mkhuge(new_pud);
		if (writable)
			new_pud = kvm_s2pud_mkwrite(new_pud);

		if (needs_exec)
			new_pud = kvm_s2pud_mkexec(new_pud);

		ret = stage2_set_pud_huge(mmu, memcache, fault_ipa, &new_pud);
	} else if (vma_pagesize == PMD_SIZE) {
		pmd_t new_pmd = kvm_pfn_pmd(pfn, mem_type);

		new_pmd = kvm_pmd_mkhuge(new_pmd);

		if (writable)
			new_pmd = kvm_s2pmd_mkwrite(new_pmd);

		if (needs_exec)
			new_pmd = kvm_s2pmd_mkexec(new_pmd);

		ret = stage2_set_pmd_huge(mmu, memcache, fault_ipa, &new_pmd);
	} else {
		pte_t new_pte = kvm_pfn_pte(pfn, mem_type);

		if (writable) {
			new_pte = kvm_s2pte_mkwrite(new_pte);
			mark_page_dirty(kvm, gfn);
		}

		if (needs_exec)
			new_pte = kvm_s2pte_mkexec(new_pte);

		ret = stage2_set_pte(mmu, memcache, fault_ipa, &new_pte, flags);
	}

out_unlock:
	spin_unlock(&kvm->mmu_lock);
	kvm_set_pfn_accessed(pfn);
	kvm_release_pfn_clean(pfn);
	return ret;
}

/*
 * Resolve the access fault by making the page young again.
 * Note that because the faulting entry is guaranteed not to be
 * cached in the TLB, we don't need to invalidate anything.
 * Only the HW Access Flag updates are supported for Stage 2 (no DBM),
 * so there is no need for atomic (pte|pmd)_mkyoung operations.
 */
static void handle_access_fault(struct kvm_vcpu *vcpu, phys_addr_t fault_ipa)
{
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	kvm_pfn_t pfn;
	bool pfn_valid = false;

	trace_kvm_access_fault(fault_ipa);

	spin_lock(&vcpu->kvm->mmu_lock);

	if (!stage2_get_leaf_entry(vcpu->arch.hw_mmu, fault_ipa, &pud, &pmd, &pte))
		goto out;

	if (pud) {		/* HugeTLB */
		*pud = kvm_s2pud_mkyoung(*pud);
		pfn = kvm_pud_pfn(*pud);
		pfn_valid = true;
	} else	if (pmd) {	/* THP, HugeTLB */
		*pmd = pmd_mkyoung(*pmd);
		pfn = pmd_pfn(*pmd);
		pfn_valid = true;
	} else {
		*pte = pte_mkyoung(*pte);	/* Just a page... */
		pfn = pte_pfn(*pte);
		pfn_valid = true;
	}

out:
	spin_unlock(&vcpu->kvm->mmu_lock);
	if (pfn_valid)
		kvm_set_pfn_accessed(pfn);
}

/**
 * kvm_handle_guest_abort - handles all 2nd stage aborts
 * @vcpu:	the VCPU pointer
 *
 * Any abort that gets to the host is almost guaranteed to be caused by a
 * missing second stage translation table entry, which can mean that either the
 * guest simply needs more memory and we must allocate an appropriate page or it
 * can mean that the guest tried to access I/O memory, which is emulated by user
 * space. The distinction is based on the IPA causing the fault and whether this
 * memory region has been registered as standard RAM by user space.
 */
int kvm_handle_guest_abort(struct kvm_vcpu *vcpu)
{
	unsigned long fault_status;
	phys_addr_t fault_ipa;
	struct kvm_memory_slot *memslot;
	unsigned long hva;
	bool is_iabt, write_fault, writable;
	gfn_t gfn;
	int ret, idx;

	fault_status = kvm_vcpu_trap_get_fault_type(vcpu);

	fault_ipa = kvm_vcpu_get_fault_ipa(vcpu);
	is_iabt = kvm_vcpu_trap_is_iabt(vcpu);

	/* Synchronous External Abort? */
	if (kvm_vcpu_abt_issea(vcpu)) {
		/*
		 * For RAS the host kernel may handle this abort.
		 * There is no need to pass the error into the guest.
		 */
		if (kvm_handle_guest_sea(fault_ipa, kvm_vcpu_get_esr(vcpu)))
			kvm_inject_vabt(vcpu);

		return 1;
	}

	trace_kvm_guest_fault(*vcpu_pc(vcpu), kvm_vcpu_get_esr(vcpu),
			      kvm_vcpu_get_hfar(vcpu), fault_ipa);

	/* Check the stage-2 fault is trans. fault or write fault */
	if (fault_status != FSC_FAULT && fault_status != FSC_PERM &&
	    fault_status != FSC_ACCESS) {
		kvm_err("Unsupported FSC: EC=%#x xFSC=%#lx ESR_EL2=%#lx\n",
			kvm_vcpu_trap_get_class(vcpu),
			(unsigned long)kvm_vcpu_trap_get_fault(vcpu),
			(unsigned long)kvm_vcpu_get_esr(vcpu));
		return -EFAULT;
	}

	idx = srcu_read_lock(&vcpu->kvm->srcu);

	gfn = fault_ipa >> PAGE_SHIFT;
	memslot = gfn_to_memslot(vcpu->kvm, gfn);
	hva = gfn_to_hva_memslot_prot(memslot, gfn, &writable);
	write_fault = kvm_is_write_fault(vcpu);
	if (kvm_is_error_hva(hva) || (write_fault && !writable)) {
		/*
		 * The guest has put either its instructions or its page-tables
		 * somewhere it shouldn't have. Userspace won't be able to do
		 * anything about this (there's no syndrome for a start), so
		 * re-inject the abort back into the guest.
		 */
		if (is_iabt) {
			ret = -ENOEXEC;
			goto out;
		}

		if (kvm_vcpu_dabt_iss1tw(vcpu)) {
			kvm_inject_dabt(vcpu, kvm_vcpu_get_hfar(vcpu));
			ret = 1;
			goto out_unlock;
		}

		/*
		 * Check for a cache maintenance operation. Since we
		 * ended-up here, we know it is outside of any memory
		 * slot. But we can't find out if that is for a device,
		 * or if the guest is just being stupid. The only thing
		 * we know for sure is that this range cannot be cached.
		 *
		 * So let's assume that the guest is just being
		 * cautious, and skip the instruction.
		 */
		if (kvm_is_error_hva(hva) && kvm_vcpu_dabt_is_cm(vcpu)) {
			kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
			ret = 1;
			goto out_unlock;
		}

		/*
		 * The IPA is reported as [MAX:12], so we need to
		 * complement it with the bottom 12 bits from the
		 * faulting VA. This is always 12 bits, irrespective
		 * of the page size.
		 */
		fault_ipa |= kvm_vcpu_get_hfar(vcpu) & ((1 << 12) - 1);
		ret = io_mem_abort(vcpu, fault_ipa);
		goto out_unlock;
	}

	/* Userspace should not be able to register out-of-bounds IPAs */
	VM_BUG_ON(fault_ipa >= kvm_phys_size(vcpu->kvm));

	if (fault_status == FSC_ACCESS) {
		handle_access_fault(vcpu, fault_ipa);
		ret = 1;
		goto out_unlock;
	}

	ret = user_mem_abort(vcpu, fault_ipa, memslot, hva, fault_status);
	if (ret == 0)
		ret = 1;
out:
	if (ret == -ENOEXEC) {
		kvm_inject_pabt(vcpu, kvm_vcpu_get_hfar(vcpu));
		ret = 1;
	}
out_unlock:
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	return ret;
}

static int handle_hva_to_gpa(struct kvm *kvm,
			     unsigned long start,
			     unsigned long end,
			     int (*handler)(struct kvm *kvm,
					    gpa_t gpa, u64 size,
					    void *data),
			     void *data)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int ret = 0;

	slots = kvm_memslots(kvm);

	/* we only care about the pages that the guest sees */
	kvm_for_each_memslot(memslot, slots) {
		unsigned long hva_start, hva_end;
		gfn_t gpa;

		hva_start = max(start, memslot->userspace_addr);
		hva_end = min(end, memslot->userspace_addr +
					(memslot->npages << PAGE_SHIFT));
		if (hva_start >= hva_end)
			continue;

		gpa = hva_to_gfn_memslot(hva_start, memslot) << PAGE_SHIFT;
		ret |= handler(kvm, gpa, (u64)(hva_end - hva_start), data);
	}

	return ret;
}

static int kvm_unmap_hva_handler(struct kvm *kvm, gpa_t gpa, u64 size, void *data)
{
	unsigned flags = *(unsigned *)data;
	bool may_block = flags & MMU_NOTIFIER_RANGE_BLOCKABLE;

	__unmap_stage2_range(&kvm->arch.mmu, gpa, size, may_block);
	return 0;
}

int kvm_unmap_hva_range(struct kvm *kvm,
			unsigned long start, unsigned long end, unsigned flags)
{
	if (!kvm->arch.mmu.pgd)
		return 0;

	trace_kvm_unmap_hva_range(start, end);
	handle_hva_to_gpa(kvm, start, end, &kvm_unmap_hva_handler, &flags);
	return 0;
}

static int kvm_set_spte_handler(struct kvm *kvm, gpa_t gpa, u64 size, void *data)
{
	pte_t *pte = (pte_t *)data;

	WARN_ON(size != PAGE_SIZE);
	/*
	 * We can always call stage2_set_pte with KVM_S2PTE_FLAG_LOGGING_ACTIVE
	 * flag clear because MMU notifiers will have unmapped a huge PMD before
	 * calling ->change_pte() (which in turn calls kvm_set_spte_hva()) and
	 * therefore stage2_set_pte() never needs to clear out a huge PMD
	 * through this calling path.
	 */
	stage2_set_pte(&kvm->arch.mmu, NULL, gpa, pte, 0);
	return 0;
}


int kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	unsigned long end = hva + PAGE_SIZE;
	kvm_pfn_t pfn = pte_pfn(pte);
	pte_t stage2_pte;

	if (!kvm->arch.mmu.pgd)
		return 0;

	trace_kvm_set_spte_hva(hva);

	/*
	 * We've moved a page around, probably through CoW, so let's treat it
	 * just like a translation fault and clean the cache to the PoC.
	 */
	clean_dcache_guest_page(pfn, PAGE_SIZE);
	stage2_pte = kvm_pfn_pte(pfn, PAGE_S2);
	handle_hva_to_gpa(kvm, hva, end, &kvm_set_spte_handler, &stage2_pte);

	return 0;
}

static int kvm_age_hva_handler(struct kvm *kvm, gpa_t gpa, u64 size, void *data)
{
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	WARN_ON(size != PAGE_SIZE && size != PMD_SIZE && size != PUD_SIZE);
	if (!stage2_get_leaf_entry(&kvm->arch.mmu, gpa, &pud, &pmd, &pte))
		return 0;

	if (pud)
		return stage2_pudp_test_and_clear_young(pud);
	else if (pmd)
		return stage2_pmdp_test_and_clear_young(pmd);
	else
		return stage2_ptep_test_and_clear_young(pte);
}

static int kvm_test_age_hva_handler(struct kvm *kvm, gpa_t gpa, u64 size, void *data)
{
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	WARN_ON(size != PAGE_SIZE && size != PMD_SIZE && size != PUD_SIZE);
	if (!stage2_get_leaf_entry(&kvm->arch.mmu, gpa, &pud, &pmd, &pte))
		return 0;

	if (pud)
		return kvm_s2pud_young(*pud);
	else if (pmd)
		return pmd_young(*pmd);
	else
		return pte_young(*pte);
}

int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end)
{
	if (!kvm->arch.mmu.pgd)
		return 0;
	trace_kvm_age_hva(start, end);
	return handle_hva_to_gpa(kvm, start, end, kvm_age_hva_handler, NULL);
}

int kvm_test_age_hva(struct kvm *kvm, unsigned long hva)
{
	if (!kvm->arch.mmu.pgd)
		return 0;
	trace_kvm_test_age_hva(hva);
	return handle_hva_to_gpa(kvm, hva, hva + PAGE_SIZE,
				 kvm_test_age_hva_handler, NULL);
}

void kvm_mmu_free_memory_caches(struct kvm_vcpu *vcpu)
{
	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_page_cache);
}

phys_addr_t kvm_mmu_get_httbr(void)
{
	if (__kvm_cpu_uses_extended_idmap())
		return virt_to_phys(merged_hyp_pgd);
	else
		return virt_to_phys(hyp_pgd);
}

phys_addr_t kvm_get_idmap_vector(void)
{
	return hyp_idmap_vector;
}

static int kvm_map_idmap_text(pgd_t *pgd)
{
	int err;

	/* Create the idmap in the boot page tables */
	err = 	__create_hyp_mappings(pgd, __kvm_idmap_ptrs_per_pgd(),
				      hyp_idmap_start, hyp_idmap_end,
				      __phys_to_pfn(hyp_idmap_start),
				      PAGE_HYP_EXEC);
	if (err)
		kvm_err("Failed to idmap %lx-%lx\n",
			hyp_idmap_start, hyp_idmap_end);

	return err;
}

int kvm_mmu_init(void)
{
	int err;

	hyp_idmap_start = __pa_symbol(__hyp_idmap_text_start);
	hyp_idmap_start = ALIGN_DOWN(hyp_idmap_start, PAGE_SIZE);
	hyp_idmap_end = __pa_symbol(__hyp_idmap_text_end);
	hyp_idmap_end = ALIGN(hyp_idmap_end, PAGE_SIZE);
	hyp_idmap_vector = __pa_symbol(__kvm_hyp_init);

	/*
	 * We rely on the linker script to ensure at build time that the HYP
	 * init code does not cross a page boundary.
	 */
	BUG_ON((hyp_idmap_start ^ (hyp_idmap_end - 1)) & PAGE_MASK);

	kvm_debug("IDMAP page: %lx\n", hyp_idmap_start);
	kvm_debug("HYP VA range: %lx:%lx\n",
		  kern_hyp_va(PAGE_OFFSET),
		  kern_hyp_va((unsigned long)high_memory - 1));

	if (hyp_idmap_start >= kern_hyp_va(PAGE_OFFSET) &&
	    hyp_idmap_start <  kern_hyp_va((unsigned long)high_memory - 1) &&
	    hyp_idmap_start != (unsigned long)__hyp_idmap_text_start) {
		/*
		 * The idmap page is intersecting with the VA space,
		 * it is not safe to continue further.
		 */
		kvm_err("IDMAP intersecting with HYP VA, unable to continue\n");
		err = -EINVAL;
		goto out;
	}

	hyp_pgd = (pgd_t *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, hyp_pgd_order);
	if (!hyp_pgd) {
		kvm_err("Hyp mode PGD not allocated\n");
		err = -ENOMEM;
		goto out;
	}

	if (__kvm_cpu_uses_extended_idmap()) {
		boot_hyp_pgd = (pgd_t *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
							 hyp_pgd_order);
		if (!boot_hyp_pgd) {
			kvm_err("Hyp boot PGD not allocated\n");
			err = -ENOMEM;
			goto out;
		}

		err = kvm_map_idmap_text(boot_hyp_pgd);
		if (err)
			goto out;

		merged_hyp_pgd = (pgd_t *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
		if (!merged_hyp_pgd) {
			kvm_err("Failed to allocate extra HYP pgd\n");
			goto out;
		}
		__kvm_extend_hypmap(boot_hyp_pgd, hyp_pgd, merged_hyp_pgd,
				    hyp_idmap_start);
	} else {
		err = kvm_map_idmap_text(hyp_pgd);
		if (err)
			goto out;
	}

	io_map_base = hyp_idmap_start;
	return 0;
out:
	free_hyp_pgds();
	return err;
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				   const struct kvm_userspace_memory_region *mem,
				   struct kvm_memory_slot *old,
				   const struct kvm_memory_slot *new,
				   enum kvm_mr_change change)
{
	/*
	 * At this point memslot has been committed and there is an
	 * allocated dirty_bitmap[], dirty pages will be tracked while the
	 * memory slot is write protected.
	 */
	if (change != KVM_MR_DELETE && mem->flags & KVM_MEM_LOG_DIRTY_PAGES) {
		/*
		 * If we're with initial-all-set, we don't need to write
		 * protect any pages because they're all reported as dirty.
		 * Huge pages and normal pages will be write protect gradually.
		 */
		if (!kvm_dirty_log_manual_protect_and_init_set(kvm)) {
			kvm_mmu_wp_memory_region(kvm, mem->slot);
		}
	}
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
				   struct kvm_memory_slot *memslot,
				   const struct kvm_userspace_memory_region *mem,
				   enum kvm_mr_change change)
{
	hva_t hva = mem->userspace_addr;
	hva_t reg_end = hva + mem->memory_size;
	bool writable = !(mem->flags & KVM_MEM_READONLY);
	int ret = 0;

	if (change != KVM_MR_CREATE && change != KVM_MR_MOVE &&
			change != KVM_MR_FLAGS_ONLY)
		return 0;

	/*
	 * Prevent userspace from creating a memory region outside of the IPA
	 * space addressable by the KVM guest IPA space.
	 */
	if (memslot->base_gfn + memslot->npages >=
	    (kvm_phys_size(kvm) >> PAGE_SHIFT))
		return -EFAULT;

	mmap_read_lock(current->mm);
	/*
	 * A memory region could potentially cover multiple VMAs, and any holes
	 * between them, so iterate over all of them to find out if we can map
	 * any of them right now.
	 *
	 *     +--------------------------------------------+
	 * +---------------+----------------+   +----------------+
	 * |   : VMA 1     |      VMA 2     |   |    VMA 3  :    |
	 * +---------------+----------------+   +----------------+
	 *     |               memory region                |
	 *     +--------------------------------------------+
	 */
	do {
		struct vm_area_struct *vma = find_vma(current->mm, hva);
		hva_t vm_start, vm_end;

		if (!vma || vma->vm_start >= reg_end)
			break;

		/*
		 * Take the intersection of this VMA with the memory region
		 */
		vm_start = max(hva, vma->vm_start);
		vm_end = min(reg_end, vma->vm_end);

		if (vma->vm_flags & VM_PFNMAP) {
			gpa_t gpa = mem->guest_phys_addr +
				    (vm_start - mem->userspace_addr);
			phys_addr_t pa;

			pa = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;
			pa += vm_start - vma->vm_start;

			/* IO region dirty page logging not allowed */
			if (memslot->flags & KVM_MEM_LOG_DIRTY_PAGES) {
				ret = -EINVAL;
				goto out;
			}

			ret = kvm_phys_addr_ioremap(kvm, gpa, pa,
						    vm_end - vm_start,
						    writable);
			if (ret)
				break;
		}
		hva = vm_end;
	} while (hva < reg_end);

	if (change == KVM_MR_FLAGS_ONLY)
		goto out;

	spin_lock(&kvm->mmu_lock);
	if (ret)
		unmap_stage2_range(&kvm->arch.mmu, mem->guest_phys_addr, mem->memory_size);
	else
		stage2_flush_memslot(kvm, memslot);
	spin_unlock(&kvm->mmu_lock);
out:
	mmap_read_unlock(current->mm);
	return ret;
}

void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *slot)
{
}

void kvm_arch_memslots_updated(struct kvm *kvm, u64 gen)
{
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	kvm_free_stage2_pgd(&kvm->arch.mmu);
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
	gpa_t gpa = slot->base_gfn << PAGE_SHIFT;
	phys_addr_t size = slot->npages << PAGE_SHIFT;

	spin_lock(&kvm->mmu_lock);
	unmap_stage2_range(&kvm->arch.mmu, gpa, size);
	spin_unlock(&kvm->mmu_lock);
}

/*
 * See note at ARMv7 ARM B1.14.4 (TL;DR: S/W ops are not easily virtualized).
 *
 * Main problems:
 * - S/W ops are local to a CPU (not broadcast)
 * - We have line migration behind our back (speculation)
 * - System caches don't support S/W at all (damn!)
 *
 * In the face of the above, the best we can do is to try and convert
 * S/W ops to VA ops. Because the guest is not allowed to infer the
 * S/W to PA mapping, it can only use S/W to nuke the whole cache,
 * which is a rather good thing for us.
 *
 * Also, it is only used when turning caches on/off ("The expected
 * usage of the cache maintenance instructions that operate by set/way
 * is associated with the cache maintenance instructions associated
 * with the powerdown and powerup of caches, if this is required by
 * the implementation.").
 *
 * We use the following policy:
 *
 * - If we trap a S/W operation, we enable VM trapping to detect
 *   caches being turned on/off, and do a full clean.
 *
 * - We flush the caches on both caches being turned on and off.
 *
 * - Once the caches are enabled, we stop trapping VM ops.
 */
void kvm_set_way_flush(struct kvm_vcpu *vcpu)
{
	unsigned long hcr = *vcpu_hcr(vcpu);

	/*
	 * If this is the first time we do a S/W operation
	 * (i.e. HCR_TVM not set) flush the whole memory, and set the
	 * VM trapping.
	 *
	 * Otherwise, rely on the VM trapping to wait for the MMU +
	 * Caches to be turned off. At that point, we'll be able to
	 * clean the caches again.
	 */
	if (!(hcr & HCR_TVM)) {
		trace_kvm_set_way_flush(*vcpu_pc(vcpu),
					vcpu_has_cache_enabled(vcpu));
		stage2_flush_vm(vcpu->kvm);
		*vcpu_hcr(vcpu) = hcr | HCR_TVM;
	}
}

void kvm_toggle_cache(struct kvm_vcpu *vcpu, bool was_enabled)
{
	bool now_enabled = vcpu_has_cache_enabled(vcpu);

	/*
	 * If switching the MMU+caches on, need to invalidate the caches.
	 * If switching it off, need to clean the caches.
	 * Clean + invalidate does the trick always.
	 */
	if (now_enabled != was_enabled)
		stage2_flush_vm(vcpu->kvm);

	/* Caches are now on, stop trapping VM ops (until a S/W op) */
	if (now_enabled)
		*vcpu_hcr(vcpu) &= ~HCR_TVM;

	trace_kvm_toggle_cache(*vcpu_pc(vcpu), was_enabled, now_enabled);
}
