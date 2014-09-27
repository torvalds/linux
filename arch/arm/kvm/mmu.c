/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/mman.h>
#include <linux/kvm_host.h>
#include <linux/io.h>
#include <linux/hugetlb.h>
#include <trace/events/kvm.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_mmio.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>

#include "trace.h"

extern char  __hyp_idmap_text_start[], __hyp_idmap_text_end[];

static pgd_t *boot_hyp_pgd;
static pgd_t *hyp_pgd;
static DEFINE_MUTEX(kvm_hyp_pgd_mutex);

static void *init_bounce_page;
static unsigned long hyp_idmap_start;
static unsigned long hyp_idmap_end;
static phys_addr_t hyp_idmap_vector;

#define pgd_order get_order(PTRS_PER_PGD * sizeof(pgd_t))

#define kvm_pmd_huge(_x)	(pmd_huge(_x) || pmd_trans_huge(_x))

static void kvm_tlb_flush_vmid_ipa(struct kvm *kvm, phys_addr_t ipa)
{
	/*
	 * This function also gets called when dealing with HYP page
	 * tables. As HYP doesn't have an associated struct kvm (and
	 * the HYP page tables are fairly static), we don't do
	 * anything there.
	 */
	if (kvm)
		kvm_call_hyp(__kvm_tlb_flush_vmid_ipa, kvm, ipa);
}

static int mmu_topup_memory_cache(struct kvm_mmu_memory_cache *cache,
				  int min, int max)
{
	void *page;

	BUG_ON(max > KVM_NR_MEM_OBJS);
	if (cache->nobjs >= min)
		return 0;
	while (cache->nobjs < max) {
		page = (void *)__get_free_page(PGALLOC_GFP);
		if (!page)
			return -ENOMEM;
		cache->objects[cache->nobjs++] = page;
	}
	return 0;
}

static void mmu_free_memory_cache(struct kvm_mmu_memory_cache *mc)
{
	while (mc->nobjs)
		free_page((unsigned long)mc->objects[--mc->nobjs]);
}

static void *mmu_memory_cache_alloc(struct kvm_mmu_memory_cache *mc)
{
	void *p;

	BUG_ON(!mc || !mc->nobjs);
	p = mc->objects[--mc->nobjs];
	return p;
}

static void clear_pgd_entry(struct kvm *kvm, pgd_t *pgd, phys_addr_t addr)
{
	pud_t *pud_table __maybe_unused = pud_offset(pgd, 0);
	pgd_clear(pgd);
	kvm_tlb_flush_vmid_ipa(kvm, addr);
	pud_free(NULL, pud_table);
	put_page(virt_to_page(pgd));
}

static void clear_pud_entry(struct kvm *kvm, pud_t *pud, phys_addr_t addr)
{
	pmd_t *pmd_table = pmd_offset(pud, 0);
	VM_BUG_ON(pud_huge(*pud));
	pud_clear(pud);
	kvm_tlb_flush_vmid_ipa(kvm, addr);
	pmd_free(NULL, pmd_table);
	put_page(virt_to_page(pud));
}

static void clear_pmd_entry(struct kvm *kvm, pmd_t *pmd, phys_addr_t addr)
{
	pte_t *pte_table = pte_offset_kernel(pmd, 0);
	VM_BUG_ON(kvm_pmd_huge(*pmd));
	pmd_clear(pmd);
	kvm_tlb_flush_vmid_ipa(kvm, addr);
	pte_free_kernel(NULL, pte_table);
	put_page(virt_to_page(pmd));
}

static void unmap_ptes(struct kvm *kvm, pmd_t *pmd,
		       phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t start_addr = addr;
	pte_t *pte, *start_pte;

	start_pte = pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_none(*pte)) {
			kvm_set_pte(pte, __pte(0));
			put_page(virt_to_page(pte));
			kvm_tlb_flush_vmid_ipa(kvm, addr);
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);

	if (kvm_pte_table_empty(start_pte))
		clear_pmd_entry(kvm, pmd, start_addr);
}

static void unmap_pmds(struct kvm *kvm, pud_t *pud,
		       phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t next, start_addr = addr;
	pmd_t *pmd, *start_pmd;

	start_pmd = pmd = pmd_offset(pud, addr);
	do {
		next = kvm_pmd_addr_end(addr, end);
		if (!pmd_none(*pmd)) {
			if (kvm_pmd_huge(*pmd)) {
				pmd_clear(pmd);
				kvm_tlb_flush_vmid_ipa(kvm, addr);
				put_page(virt_to_page(pmd));
			} else {
				unmap_ptes(kvm, pmd, addr, next);
			}
		}
	} while (pmd++, addr = next, addr != end);

	if (kvm_pmd_table_empty(start_pmd))
		clear_pud_entry(kvm, pud, start_addr);
}

static void unmap_puds(struct kvm *kvm, pgd_t *pgd,
		       phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t next, start_addr = addr;
	pud_t *pud, *start_pud;

	start_pud = pud = pud_offset(pgd, addr);
	do {
		next = kvm_pud_addr_end(addr, end);
		if (!pud_none(*pud)) {
			if (pud_huge(*pud)) {
				pud_clear(pud);
				kvm_tlb_flush_vmid_ipa(kvm, addr);
				put_page(virt_to_page(pud));
			} else {
				unmap_pmds(kvm, pud, addr, next);
			}
		}
	} while (pud++, addr = next, addr != end);

	if (kvm_pud_table_empty(start_pud))
		clear_pgd_entry(kvm, pgd, start_addr);
}


static void unmap_range(struct kvm *kvm, pgd_t *pgdp,
			phys_addr_t start, u64 size)
{
	pgd_t *pgd;
	phys_addr_t addr = start, end = start + size;
	phys_addr_t next;

	pgd = pgdp + pgd_index(addr);
	do {
		next = kvm_pgd_addr_end(addr, end);
		unmap_puds(kvm, pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}

static void stage2_flush_ptes(struct kvm *kvm, pmd_t *pmd,
			      phys_addr_t addr, phys_addr_t end)
{
	pte_t *pte;

	pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_none(*pte)) {
			hva_t hva = gfn_to_hva(kvm, addr >> PAGE_SHIFT);
			kvm_flush_dcache_to_poc((void*)hva, PAGE_SIZE);
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);
}

static void stage2_flush_pmds(struct kvm *kvm, pud_t *pud,
			      phys_addr_t addr, phys_addr_t end)
{
	pmd_t *pmd;
	phys_addr_t next;

	pmd = pmd_offset(pud, addr);
	do {
		next = kvm_pmd_addr_end(addr, end);
		if (!pmd_none(*pmd)) {
			if (kvm_pmd_huge(*pmd)) {
				hva_t hva = gfn_to_hva(kvm, addr >> PAGE_SHIFT);
				kvm_flush_dcache_to_poc((void*)hva, PMD_SIZE);
			} else {
				stage2_flush_ptes(kvm, pmd, addr, next);
			}
		}
	} while (pmd++, addr = next, addr != end);
}

static void stage2_flush_puds(struct kvm *kvm, pgd_t *pgd,
			      phys_addr_t addr, phys_addr_t end)
{
	pud_t *pud;
	phys_addr_t next;

	pud = pud_offset(pgd, addr);
	do {
		next = kvm_pud_addr_end(addr, end);
		if (!pud_none(*pud)) {
			if (pud_huge(*pud)) {
				hva_t hva = gfn_to_hva(kvm, addr >> PAGE_SHIFT);
				kvm_flush_dcache_to_poc((void*)hva, PUD_SIZE);
			} else {
				stage2_flush_pmds(kvm, pud, addr, next);
			}
		}
	} while (pud++, addr = next, addr != end);
}

static void stage2_flush_memslot(struct kvm *kvm,
				 struct kvm_memory_slot *memslot)
{
	phys_addr_t addr = memslot->base_gfn << PAGE_SHIFT;
	phys_addr_t end = addr + PAGE_SIZE * memslot->npages;
	phys_addr_t next;
	pgd_t *pgd;

	pgd = kvm->arch.pgd + pgd_index(addr);
	do {
		next = kvm_pgd_addr_end(addr, end);
		stage2_flush_puds(kvm, pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}

/**
 * stage2_flush_vm - Invalidate cache for pages mapped in stage 2
 * @kvm: The struct kvm pointer
 *
 * Go through the stage 2 page tables and invalidate any cache lines
 * backing memory already mapped to the VM.
 */
void stage2_flush_vm(struct kvm *kvm)
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

/**
 * free_boot_hyp_pgd - free HYP boot page tables
 *
 * Free the HYP boot page tables. The bounce page is also freed.
 */
void free_boot_hyp_pgd(void)
{
	mutex_lock(&kvm_hyp_pgd_mutex);

	if (boot_hyp_pgd) {
		unmap_range(NULL, boot_hyp_pgd, hyp_idmap_start, PAGE_SIZE);
		unmap_range(NULL, boot_hyp_pgd, TRAMPOLINE_VA, PAGE_SIZE);
		free_pages((unsigned long)boot_hyp_pgd, pgd_order);
		boot_hyp_pgd = NULL;
	}

	if (hyp_pgd)
		unmap_range(NULL, hyp_pgd, TRAMPOLINE_VA, PAGE_SIZE);

	free_page((unsigned long)init_bounce_page);
	init_bounce_page = NULL;

	mutex_unlock(&kvm_hyp_pgd_mutex);
}

/**
 * free_hyp_pgds - free Hyp-mode page tables
 *
 * Assumes hyp_pgd is a page table used strictly in Hyp-mode and
 * therefore contains either mappings in the kernel memory area (above
 * PAGE_OFFSET), or device mappings in the vmalloc range (from
 * VMALLOC_START to VMALLOC_END).
 *
 * boot_hyp_pgd should only map two pages for the init code.
 */
void free_hyp_pgds(void)
{
	unsigned long addr;

	free_boot_hyp_pgd();

	mutex_lock(&kvm_hyp_pgd_mutex);

	if (hyp_pgd) {
		for (addr = PAGE_OFFSET; virt_addr_valid(addr); addr += PGDIR_SIZE)
			unmap_range(NULL, hyp_pgd, KERN_TO_HYP(addr), PGDIR_SIZE);
		for (addr = VMALLOC_START; is_vmalloc_addr((void*)addr); addr += PGDIR_SIZE)
			unmap_range(NULL, hyp_pgd, KERN_TO_HYP(addr), PGDIR_SIZE);

		free_pages((unsigned long)hyp_pgd, pgd_order);
		hyp_pgd = NULL;
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
		kvm_set_pte(pte, pfn_pte(pfn, prot));
		get_page(virt_to_page(pte));
		kvm_flush_dcache_to_poc(pte, sizeof(*pte));
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
			pte = pte_alloc_one_kernel(NULL, addr);
			if (!pte) {
				kvm_err("Cannot allocate Hyp pte\n");
				return -ENOMEM;
			}
			pmd_populate_kernel(NULL, pmd, pte);
			get_page(virt_to_page(pmd));
			kvm_flush_dcache_to_poc(pmd, sizeof(*pmd));
		}

		next = pmd_addr_end(addr, end);

		create_hyp_pte_mappings(pmd, addr, next, pfn, prot);
		pfn += (next - addr) >> PAGE_SHIFT;
	} while (addr = next, addr != end);

	return 0;
}

static int __create_hyp_mappings(pgd_t *pgdp,
				 unsigned long start, unsigned long end,
				 unsigned long pfn, pgprot_t prot)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	unsigned long addr, next;
	int err = 0;

	mutex_lock(&kvm_hyp_pgd_mutex);
	addr = start & PAGE_MASK;
	end = PAGE_ALIGN(end);
	do {
		pgd = pgdp + pgd_index(addr);
		pud = pud_offset(pgd, addr);

		if (pud_none_or_clear_bad(pud)) {
			pmd = pmd_alloc_one(NULL, addr);
			if (!pmd) {
				kvm_err("Cannot allocate Hyp pmd\n");
				err = -ENOMEM;
				goto out;
			}
			pud_populate(NULL, pud, pmd);
			get_page(virt_to_page(pud));
			kvm_flush_dcache_to_poc(pud, sizeof(*pud));
		}

		next = pgd_addr_end(addr, end);
		err = create_hyp_pmd_mappings(pud, addr, next, pfn, prot);
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
 *
 * The same virtual address as the kernel virtual address is also used
 * in Hyp-mode mapping (modulo HYP_PAGE_OFFSET) to the same underlying
 * physical pages.
 */
int create_hyp_mappings(void *from, void *to)
{
	phys_addr_t phys_addr;
	unsigned long virt_addr;
	unsigned long start = KERN_TO_HYP((unsigned long)from);
	unsigned long end = KERN_TO_HYP((unsigned long)to);

	start = start & PAGE_MASK;
	end = PAGE_ALIGN(end);

	for (virt_addr = start; virt_addr < end; virt_addr += PAGE_SIZE) {
		int err;

		phys_addr = kvm_kaddr_to_phys(from + virt_addr - start);
		err = __create_hyp_mappings(hyp_pgd, virt_addr,
					    virt_addr + PAGE_SIZE,
					    __phys_to_pfn(phys_addr),
					    PAGE_HYP);
		if (err)
			return err;
	}

	return 0;
}

/**
 * create_hyp_io_mappings - duplicate a kernel IO mapping into Hyp mode
 * @from:	The kernel start VA of the range
 * @to:		The kernel end VA of the range (exclusive)
 * @phys_addr:	The physical start address which gets mapped
 *
 * The resulting HYP VA is the same as the kernel VA, modulo
 * HYP_PAGE_OFFSET.
 */
int create_hyp_io_mappings(void *from, void *to, phys_addr_t phys_addr)
{
	unsigned long start = KERN_TO_HYP((unsigned long)from);
	unsigned long end = KERN_TO_HYP((unsigned long)to);

	/* Check for a valid kernel IO mapping */
	if (!is_vmalloc_addr(from) || !is_vmalloc_addr(to - 1))
		return -EINVAL;

	return __create_hyp_mappings(hyp_pgd, start, end,
				     __phys_to_pfn(phys_addr), PAGE_HYP_DEVICE);
}

/**
 * kvm_alloc_stage2_pgd - allocate level-1 table for stage-2 translation.
 * @kvm:	The KVM struct pointer for the VM.
 *
 * Allocates the 1st level table only of size defined by S2_PGD_ORDER (can
 * support either full 40-bit input addresses or limited to 32-bit input
 * addresses). Clears the allocated pages.
 *
 * Note we don't need locking here as this is only called when the VM is
 * created, which can only be done once.
 */
int kvm_alloc_stage2_pgd(struct kvm *kvm)
{
	pgd_t *pgd;

	if (kvm->arch.pgd != NULL) {
		kvm_err("kvm_arch already initialized?\n");
		return -EINVAL;
	}

	pgd = (pgd_t *)__get_free_pages(GFP_KERNEL, S2_PGD_ORDER);
	if (!pgd)
		return -ENOMEM;

	memset(pgd, 0, PTRS_PER_S2_PGD * sizeof(pgd_t));
	kvm_clean_pgd(pgd);
	kvm->arch.pgd = pgd;

	return 0;
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
static void unmap_stage2_range(struct kvm *kvm, phys_addr_t start, u64 size)
{
	unmap_range(kvm, kvm->arch.pgd, start, size);
}

/**
 * kvm_free_stage2_pgd - free all stage-2 tables
 * @kvm:	The KVM struct pointer for the VM.
 *
 * Walks the level-1 page table pointed to by kvm->arch.pgd and frees all
 * underlying level-2 and level-3 tables before freeing the actual level-1 table
 * and setting the struct pointer to NULL.
 *
 * Note we don't need locking here as this is only called when the VM is
 * destroyed, which can only be done once.
 */
void kvm_free_stage2_pgd(struct kvm *kvm)
{
	if (kvm->arch.pgd == NULL)
		return;

	unmap_stage2_range(kvm, 0, KVM_PHYS_SIZE);
	free_pages((unsigned long)kvm->arch.pgd, S2_PGD_ORDER);
	kvm->arch.pgd = NULL;
}

static pmd_t *stage2_get_pmd(struct kvm *kvm, struct kvm_mmu_memory_cache *cache,
			     phys_addr_t addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	pgd = kvm->arch.pgd + pgd_index(addr);
	pud = pud_offset(pgd, addr);
	if (pud_none(*pud)) {
		if (!cache)
			return NULL;
		pmd = mmu_memory_cache_alloc(cache);
		pud_populate(NULL, pud, pmd);
		get_page(virt_to_page(pud));
	}

	return pmd_offset(pud, addr);
}

static int stage2_set_pmd_huge(struct kvm *kvm, struct kvm_mmu_memory_cache
			       *cache, phys_addr_t addr, const pmd_t *new_pmd)
{
	pmd_t *pmd, old_pmd;

	pmd = stage2_get_pmd(kvm, cache, addr);
	VM_BUG_ON(!pmd);

	/*
	 * Mapping in huge pages should only happen through a fault.  If a
	 * page is merged into a transparent huge page, the individual
	 * subpages of that huge page should be unmapped through MMU
	 * notifiers before we get here.
	 *
	 * Merging of CompoundPages is not supported; they should become
	 * splitting first, unmapped, merged, and mapped back in on-demand.
	 */
	VM_BUG_ON(pmd_present(*pmd) && pmd_pfn(*pmd) != pmd_pfn(*new_pmd));

	old_pmd = *pmd;
	kvm_set_pmd(pmd, *new_pmd);
	if (pmd_present(old_pmd))
		kvm_tlb_flush_vmid_ipa(kvm, addr);
	else
		get_page(virt_to_page(pmd));
	return 0;
}

static int stage2_set_pte(struct kvm *kvm, struct kvm_mmu_memory_cache *cache,
			  phys_addr_t addr, const pte_t *new_pte, bool iomap)
{
	pmd_t *pmd;
	pte_t *pte, old_pte;

	/* Create stage-2 page table mapping - Level 1 */
	pmd = stage2_get_pmd(kvm, cache, addr);
	if (!pmd) {
		/*
		 * Ignore calls from kvm_set_spte_hva for unallocated
		 * address ranges.
		 */
		return 0;
	}

	/* Create stage-2 page mappings - Level 2 */
	if (pmd_none(*pmd)) {
		if (!cache)
			return 0; /* ignore calls from kvm_set_spte_hva */
		pte = mmu_memory_cache_alloc(cache);
		kvm_clean_pte(pte);
		pmd_populate_kernel(NULL, pmd, pte);
		get_page(virt_to_page(pmd));
	}

	pte = pte_offset_kernel(pmd, addr);

	if (iomap && pte_present(*pte))
		return -EFAULT;

	/* Create 2nd stage page table mapping - Level 3 */
	old_pte = *pte;
	kvm_set_pte(pte, *new_pte);
	if (pte_present(old_pte))
		kvm_tlb_flush_vmid_ipa(kvm, addr);
	else
		get_page(virt_to_page(pte));

	return 0;
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
			  phys_addr_t pa, unsigned long size)
{
	phys_addr_t addr, end;
	int ret = 0;
	unsigned long pfn;
	struct kvm_mmu_memory_cache cache = { 0, };

	end = (guest_ipa + size + PAGE_SIZE - 1) & PAGE_MASK;
	pfn = __phys_to_pfn(pa);

	for (addr = guest_ipa; addr < end; addr += PAGE_SIZE) {
		pte_t pte = pfn_pte(pfn, PAGE_S2_DEVICE);

		ret = mmu_topup_memory_cache(&cache, 2, 2);
		if (ret)
			goto out;
		spin_lock(&kvm->mmu_lock);
		ret = stage2_set_pte(kvm, &cache, addr, &pte, true);
		spin_unlock(&kvm->mmu_lock);
		if (ret)
			goto out;

		pfn++;
	}

out:
	mmu_free_memory_cache(&cache);
	return ret;
}

static bool transparent_hugepage_adjust(pfn_t *pfnp, phys_addr_t *ipap)
{
	pfn_t pfn = *pfnp;
	gfn_t gfn = *ipap >> PAGE_SHIFT;

	if (PageTransCompound(pfn_to_page(pfn))) {
		unsigned long mask;
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
		mask = PTRS_PER_PMD - 1;
		VM_BUG_ON((gfn & mask) != (pfn & mask));
		if (pfn & mask) {
			*ipap &= PMD_MASK;
			kvm_release_pfn_clean(pfn);
			pfn &= ~mask;
			kvm_get_pfn(pfn);
			*pfnp = pfn;
		}

		return true;
	}

	return false;
}

static bool kvm_is_write_fault(struct kvm_vcpu *vcpu)
{
	if (kvm_vcpu_trap_is_iabt(vcpu))
		return false;

	return kvm_vcpu_dabt_iswrite(vcpu);
}

static int user_mem_abort(struct kvm_vcpu *vcpu, phys_addr_t fault_ipa,
			  struct kvm_memory_slot *memslot, unsigned long hva,
			  unsigned long fault_status)
{
	int ret;
	bool write_fault, writable, hugetlb = false, force_pte = false;
	unsigned long mmu_seq;
	gfn_t gfn = fault_ipa >> PAGE_SHIFT;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_memory_cache *memcache = &vcpu->arch.mmu_page_cache;
	struct vm_area_struct *vma;
	pfn_t pfn;
	pgprot_t mem_type = PAGE_S2;

	write_fault = kvm_is_write_fault(vcpu);
	if (fault_status == FSC_PERM && !write_fault) {
		kvm_err("Unexpected L2 read permission error\n");
		return -EFAULT;
	}

	/* Let's check if we will get back a huge page backed by hugetlbfs */
	down_read(&current->mm->mmap_sem);
	vma = find_vma_intersection(current->mm, hva, hva + 1);
	if (is_vm_hugetlb_page(vma)) {
		hugetlb = true;
		gfn = (fault_ipa & PMD_MASK) >> PAGE_SHIFT;
	} else {
		/*
		 * Pages belonging to memslots that don't have the same
		 * alignment for userspace and IPA cannot be mapped using
		 * block descriptors even if the pages belong to a THP for
		 * the process, because the stage-2 block descriptor will
		 * cover more than a single THP and we loose atomicity for
		 * unmapping, updates, and splits of the THP or other pages
		 * in the stage-2 block range.
		 */
		if ((memslot->userspace_addr & ~PMD_MASK) !=
		    ((memslot->base_gfn << PAGE_SHIFT) & ~PMD_MASK))
			force_pte = true;
	}
	up_read(&current->mm->mmap_sem);

	/* We need minimum second+third level pages */
	ret = mmu_topup_memory_cache(memcache, 2, KVM_NR_MEM_OBJS);
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
	if (is_error_pfn(pfn))
		return -EFAULT;

	if (kvm_is_mmio_pfn(pfn))
		mem_type = PAGE_S2_DEVICE;

	spin_lock(&kvm->mmu_lock);
	if (mmu_notifier_retry(kvm, mmu_seq))
		goto out_unlock;
	if (!hugetlb && !force_pte)
		hugetlb = transparent_hugepage_adjust(&pfn, &fault_ipa);

	if (hugetlb) {
		pmd_t new_pmd = pfn_pmd(pfn, mem_type);
		new_pmd = pmd_mkhuge(new_pmd);
		if (writable) {
			kvm_set_s2pmd_writable(&new_pmd);
			kvm_set_pfn_dirty(pfn);
		}
		coherent_cache_guest_page(vcpu, hva & PMD_MASK, PMD_SIZE);
		ret = stage2_set_pmd_huge(kvm, memcache, fault_ipa, &new_pmd);
	} else {
		pte_t new_pte = pfn_pte(pfn, mem_type);
		if (writable) {
			kvm_set_s2pte_writable(&new_pte);
			kvm_set_pfn_dirty(pfn);
		}
		coherent_cache_guest_page(vcpu, hva, PAGE_SIZE);
		ret = stage2_set_pte(kvm, memcache, fault_ipa, &new_pte,
				     mem_type == PAGE_S2_DEVICE);
	}


out_unlock:
	spin_unlock(&kvm->mmu_lock);
	kvm_release_pfn_clean(pfn);
	return ret;
}

/**
 * kvm_handle_guest_abort - handles all 2nd stage aborts
 * @vcpu:	the VCPU pointer
 * @run:	the kvm_run structure
 *
 * Any abort that gets to the host is almost guaranteed to be caused by a
 * missing second stage translation table entry, which can mean that either the
 * guest simply needs more memory and we must allocate an appropriate page or it
 * can mean that the guest tried to access I/O memory, which is emulated by user
 * space. The distinction is based on the IPA causing the fault and whether this
 * memory region has been registered as standard RAM by user space.
 */
int kvm_handle_guest_abort(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	unsigned long fault_status;
	phys_addr_t fault_ipa;
	struct kvm_memory_slot *memslot;
	unsigned long hva;
	bool is_iabt, write_fault, writable;
	gfn_t gfn;
	int ret, idx;

	is_iabt = kvm_vcpu_trap_is_iabt(vcpu);
	fault_ipa = kvm_vcpu_get_fault_ipa(vcpu);

	trace_kvm_guest_fault(*vcpu_pc(vcpu), kvm_vcpu_get_hsr(vcpu),
			      kvm_vcpu_get_hfar(vcpu), fault_ipa);

	/* Check the stage-2 fault is trans. fault or write fault */
	fault_status = kvm_vcpu_trap_get_fault_type(vcpu);
	if (fault_status != FSC_FAULT && fault_status != FSC_PERM) {
		kvm_err("Unsupported FSC: EC=%#x xFSC=%#lx ESR_EL2=%#lx\n",
			kvm_vcpu_trap_get_class(vcpu),
			(unsigned long)kvm_vcpu_trap_get_fault(vcpu),
			(unsigned long)kvm_vcpu_get_hsr(vcpu));
		return -EFAULT;
	}

	idx = srcu_read_lock(&vcpu->kvm->srcu);

	gfn = fault_ipa >> PAGE_SHIFT;
	memslot = gfn_to_memslot(vcpu->kvm, gfn);
	hva = gfn_to_hva_memslot_prot(memslot, gfn, &writable);
	write_fault = kvm_is_write_fault(vcpu);
	if (kvm_is_error_hva(hva) || (write_fault && !writable)) {
		if (is_iabt) {
			/* Prefetch Abort on I/O address */
			kvm_inject_pabt(vcpu, kvm_vcpu_get_hfar(vcpu));
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
		ret = io_mem_abort(vcpu, run, fault_ipa);
		goto out_unlock;
	}

	ret = user_mem_abort(vcpu, fault_ipa, memslot, hva, fault_status);
	if (ret == 0)
		ret = 1;
out_unlock:
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	return ret;
}

static void handle_hva_to_gpa(struct kvm *kvm,
			      unsigned long start,
			      unsigned long end,
			      void (*handler)(struct kvm *kvm,
					      gpa_t gpa, void *data),
			      void *data)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;

	slots = kvm_memslots(kvm);

	/* we only care about the pages that the guest sees */
	kvm_for_each_memslot(memslot, slots) {
		unsigned long hva_start, hva_end;
		gfn_t gfn, gfn_end;

		hva_start = max(start, memslot->userspace_addr);
		hva_end = min(end, memslot->userspace_addr +
					(memslot->npages << PAGE_SHIFT));
		if (hva_start >= hva_end)
			continue;

		/*
		 * {gfn(page) | page intersects with [hva_start, hva_end)} =
		 * {gfn_start, gfn_start+1, ..., gfn_end-1}.
		 */
		gfn = hva_to_gfn_memslot(hva_start, memslot);
		gfn_end = hva_to_gfn_memslot(hva_end + PAGE_SIZE - 1, memslot);

		for (; gfn < gfn_end; ++gfn) {
			gpa_t gpa = gfn << PAGE_SHIFT;
			handler(kvm, gpa, data);
		}
	}
}

static void kvm_unmap_hva_handler(struct kvm *kvm, gpa_t gpa, void *data)
{
	unmap_stage2_range(kvm, gpa, PAGE_SIZE);
}

int kvm_unmap_hva(struct kvm *kvm, unsigned long hva)
{
	unsigned long end = hva + PAGE_SIZE;

	if (!kvm->arch.pgd)
		return 0;

	trace_kvm_unmap_hva(hva);
	handle_hva_to_gpa(kvm, hva, end, &kvm_unmap_hva_handler, NULL);
	return 0;
}

int kvm_unmap_hva_range(struct kvm *kvm,
			unsigned long start, unsigned long end)
{
	if (!kvm->arch.pgd)
		return 0;

	trace_kvm_unmap_hva_range(start, end);
	handle_hva_to_gpa(kvm, start, end, &kvm_unmap_hva_handler, NULL);
	return 0;
}

static void kvm_set_spte_handler(struct kvm *kvm, gpa_t gpa, void *data)
{
	pte_t *pte = (pte_t *)data;

	stage2_set_pte(kvm, NULL, gpa, pte, false);
}


void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	unsigned long end = hva + PAGE_SIZE;
	pte_t stage2_pte;

	if (!kvm->arch.pgd)
		return;

	trace_kvm_set_spte_hva(hva);
	stage2_pte = pfn_pte(pte_pfn(pte), PAGE_S2);
	handle_hva_to_gpa(kvm, hva, end, &kvm_set_spte_handler, &stage2_pte);
}

void kvm_mmu_free_memory_caches(struct kvm_vcpu *vcpu)
{
	mmu_free_memory_cache(&vcpu->arch.mmu_page_cache);
}

phys_addr_t kvm_mmu_get_httbr(void)
{
	return virt_to_phys(hyp_pgd);
}

phys_addr_t kvm_mmu_get_boot_httbr(void)
{
	return virt_to_phys(boot_hyp_pgd);
}

phys_addr_t kvm_get_idmap_vector(void)
{
	return hyp_idmap_vector;
}

int kvm_mmu_init(void)
{
	int err;

	hyp_idmap_start = kvm_virt_to_phys(__hyp_idmap_text_start);
	hyp_idmap_end = kvm_virt_to_phys(__hyp_idmap_text_end);
	hyp_idmap_vector = kvm_virt_to_phys(__kvm_hyp_init);

	if ((hyp_idmap_start ^ hyp_idmap_end) & PAGE_MASK) {
		/*
		 * Our init code is crossing a page boundary. Allocate
		 * a bounce page, copy the code over and use that.
		 */
		size_t len = __hyp_idmap_text_end - __hyp_idmap_text_start;
		phys_addr_t phys_base;

		init_bounce_page = (void *)__get_free_page(GFP_KERNEL);
		if (!init_bounce_page) {
			kvm_err("Couldn't allocate HYP init bounce page\n");
			err = -ENOMEM;
			goto out;
		}

		memcpy(init_bounce_page, __hyp_idmap_text_start, len);
		/*
		 * Warning: the code we just copied to the bounce page
		 * must be flushed to the point of coherency.
		 * Otherwise, the data may be sitting in L2, and HYP
		 * mode won't be able to observe it as it runs with
		 * caches off at that point.
		 */
		kvm_flush_dcache_to_poc(init_bounce_page, len);

		phys_base = kvm_virt_to_phys(init_bounce_page);
		hyp_idmap_vector += phys_base - hyp_idmap_start;
		hyp_idmap_start = phys_base;
		hyp_idmap_end = phys_base + len;

		kvm_info("Using HYP init bounce page @%lx\n",
			 (unsigned long)phys_base);
	}

	hyp_pgd = (pgd_t *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, pgd_order);
	boot_hyp_pgd = (pgd_t *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, pgd_order);

	if (!hyp_pgd || !boot_hyp_pgd) {
		kvm_err("Hyp mode PGD not allocated\n");
		err = -ENOMEM;
		goto out;
	}

	/* Create the idmap in the boot page tables */
	err = 	__create_hyp_mappings(boot_hyp_pgd,
				      hyp_idmap_start, hyp_idmap_end,
				      __phys_to_pfn(hyp_idmap_start),
				      PAGE_HYP);

	if (err) {
		kvm_err("Failed to idmap %lx-%lx\n",
			hyp_idmap_start, hyp_idmap_end);
		goto out;
	}

	/* Map the very same page at the trampoline VA */
	err = 	__create_hyp_mappings(boot_hyp_pgd,
				      TRAMPOLINE_VA, TRAMPOLINE_VA + PAGE_SIZE,
				      __phys_to_pfn(hyp_idmap_start),
				      PAGE_HYP);
	if (err) {
		kvm_err("Failed to map trampoline @%lx into boot HYP pgd\n",
			TRAMPOLINE_VA);
		goto out;
	}

	/* Map the same page again into the runtime page tables */
	err = 	__create_hyp_mappings(hyp_pgd,
				      TRAMPOLINE_VA, TRAMPOLINE_VA + PAGE_SIZE,
				      __phys_to_pfn(hyp_idmap_start),
				      PAGE_HYP);
	if (err) {
		kvm_err("Failed to map trampoline @%lx into runtime HYP pgd\n",
			TRAMPOLINE_VA);
		goto out;
	}

	return 0;
out:
	free_hyp_pgds();
	return err;
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				   struct kvm_userspace_memory_region *mem,
				   const struct kvm_memory_slot *old,
				   enum kvm_mr_change change)
{
	gpa_t gpa = old->base_gfn << PAGE_SHIFT;
	phys_addr_t size = old->npages << PAGE_SHIFT;
	if (change == KVM_MR_DELETE || change == KVM_MR_MOVE) {
		spin_lock(&kvm->mmu_lock);
		unmap_stage2_range(kvm, gpa, size);
		spin_unlock(&kvm->mmu_lock);
	}
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
				   struct kvm_memory_slot *memslot,
				   struct kvm_userspace_memory_region *mem,
				   enum kvm_mr_change change)
{
	return 0;
}

void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *free,
			   struct kvm_memory_slot *dont)
{
}

int kvm_arch_create_memslot(struct kvm *kvm, struct kvm_memory_slot *slot,
			    unsigned long npages)
{
	return 0;
}

void kvm_arch_memslots_updated(struct kvm *kvm)
{
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
}
