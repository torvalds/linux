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
#include <trace/events/kvm.h>
#include <asm/idmap.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_mmio.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/mach/map.h>
#include <trace/events/kvm.h>

#include "trace.h"

extern char  __hyp_idmap_text_start[], __hyp_idmap_text_end[];

static DEFINE_MUTEX(kvm_hyp_pgd_mutex);

static void kvm_tlb_flush_vmid(struct kvm *kvm)
{
	kvm_call_hyp(__kvm_tlb_flush_vmid, kvm);
}

static void kvm_set_pte(pte_t *pte, pte_t new_pte)
{
	pte_val(*pte) = new_pte;
	/*
	 * flush_pmd_entry just takes a void pointer and cleans the necessary
	 * cache entries, so we can reuse the function for ptes.
	 */
	flush_pmd_entry(pte);
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

static void free_ptes(pmd_t *pmd, unsigned long addr)
{
	pte_t *pte;
	unsigned int i;

	for (i = 0; i < PTRS_PER_PMD; i++, addr += PMD_SIZE) {
		if (!pmd_none(*pmd) && pmd_table(*pmd)) {
			pte = pte_offset_kernel(pmd, addr);
			pte_free_kernel(NULL, pte);
		}
		pmd++;
	}
}

/**
 * free_hyp_pmds - free a Hyp-mode level-2 tables and child level-3 tables
 *
 * Assumes this is a page table used strictly in Hyp-mode and therefore contains
 * only mappings in the kernel memory area, which is above PAGE_OFFSET.
 */
void free_hyp_pmds(void)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	unsigned long addr;

	mutex_lock(&kvm_hyp_pgd_mutex);
	for (addr = PAGE_OFFSET; addr != 0; addr += PGDIR_SIZE) {
		pgd = hyp_pgd + pgd_index(addr);
		pud = pud_offset(pgd, addr);

		if (pud_none(*pud))
			continue;
		BUG_ON(pud_bad(*pud));

		pmd = pmd_offset(pud, addr);
		free_ptes(pmd, addr);
		pmd_free(NULL, pmd);
		pud_clear(pud);
	}
	mutex_unlock(&kvm_hyp_pgd_mutex);
}

static void create_hyp_pte_mappings(pmd_t *pmd, unsigned long start,
				    unsigned long end)
{
	pte_t *pte;
	unsigned long addr;
	struct page *page;

	for (addr = start & PAGE_MASK; addr < end; addr += PAGE_SIZE) {
		pte = pte_offset_kernel(pmd, addr);
		BUG_ON(!virt_addr_valid(addr));
		page = virt_to_page(addr);
		kvm_set_pte(pte, mk_pte(page, PAGE_HYP));
	}
}

static void create_hyp_io_pte_mappings(pmd_t *pmd, unsigned long start,
				       unsigned long end,
				       unsigned long *pfn_base)
{
	pte_t *pte;
	unsigned long addr;

	for (addr = start & PAGE_MASK; addr < end; addr += PAGE_SIZE) {
		pte = pte_offset_kernel(pmd, addr);
		BUG_ON(pfn_valid(*pfn_base));
		kvm_set_pte(pte, pfn_pte(*pfn_base, PAGE_HYP_DEVICE));
		(*pfn_base)++;
	}
}

static int create_hyp_pmd_mappings(pud_t *pud, unsigned long start,
				   unsigned long end, unsigned long *pfn_base)
{
	pmd_t *pmd;
	pte_t *pte;
	unsigned long addr, next;

	for (addr = start; addr < end; addr = next) {
		pmd = pmd_offset(pud, addr);

		BUG_ON(pmd_sect(*pmd));

		if (pmd_none(*pmd)) {
			pte = pte_alloc_one_kernel(NULL, addr);
			if (!pte) {
				kvm_err("Cannot allocate Hyp pte\n");
				return -ENOMEM;
			}
			pmd_populate_kernel(NULL, pmd, pte);
		}

		next = pmd_addr_end(addr, end);

		/*
		 * If pfn_base is NULL, we map kernel pages into HYP with the
		 * virtual address. Otherwise, this is considered an I/O
		 * mapping and we map the physical region starting at
		 * *pfn_base to [start, end[.
		 */
		if (!pfn_base)
			create_hyp_pte_mappings(pmd, addr, next);
		else
			create_hyp_io_pte_mappings(pmd, addr, next, pfn_base);
	}

	return 0;
}

static int __create_hyp_mappings(void *from, void *to, unsigned long *pfn_base)
{
	unsigned long start = (unsigned long)from;
	unsigned long end = (unsigned long)to;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	unsigned long addr, next;
	int err = 0;

	BUG_ON(start > end);
	if (start < PAGE_OFFSET)
		return -EINVAL;

	mutex_lock(&kvm_hyp_pgd_mutex);
	for (addr = start; addr < end; addr = next) {
		pgd = hyp_pgd + pgd_index(addr);
		pud = pud_offset(pgd, addr);

		if (pud_none_or_clear_bad(pud)) {
			pmd = pmd_alloc_one(NULL, addr);
			if (!pmd) {
				kvm_err("Cannot allocate Hyp pmd\n");
				err = -ENOMEM;
				goto out;
			}
			pud_populate(NULL, pud, pmd);
		}

		next = pgd_addr_end(addr, end);
		err = create_hyp_pmd_mappings(pud, addr, next, pfn_base);
		if (err)
			goto out;
	}
out:
	mutex_unlock(&kvm_hyp_pgd_mutex);
	return err;
}

/**
 * create_hyp_mappings - map a kernel virtual address range in Hyp mode
 * @from:	The virtual kernel start address of the range
 * @to:		The virtual kernel end address of the range (exclusive)
 *
 * The same virtual address as the kernel virtual address is also used in
 * Hyp-mode mapping to the same underlying physical pages.
 *
 * Note: Wrapping around zero in the "to" address is not supported.
 */
int create_hyp_mappings(void *from, void *to)
{
	return __create_hyp_mappings(from, to, NULL);
}

/**
 * create_hyp_io_mappings - map a physical IO range in Hyp mode
 * @from:	The virtual HYP start address of the range
 * @to:		The virtual HYP end address of the range (exclusive)
 * @addr:	The physical start address which gets mapped
 */
int create_hyp_io_mappings(void *from, void *to, phys_addr_t addr)
{
	unsigned long pfn = __phys_to_pfn(addr);
	return __create_hyp_mappings(from, to, &pfn);
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

	/* stage-2 pgd must be aligned to its size */
	VM_BUG_ON((unsigned long)pgd & (S2_PGD_SIZE - 1));

	memset(pgd, 0, PTRS_PER_S2_PGD * sizeof(pgd_t));
	clean_dcache_area(pgd, PTRS_PER_S2_PGD * sizeof(pgd_t));
	kvm->arch.pgd = pgd;

	return 0;
}

static void clear_pud_entry(pud_t *pud)
{
	pmd_t *pmd_table = pmd_offset(pud, 0);
	pud_clear(pud);
	pmd_free(NULL, pmd_table);
	put_page(virt_to_page(pud));
}

static void clear_pmd_entry(pmd_t *pmd)
{
	pte_t *pte_table = pte_offset_kernel(pmd, 0);
	pmd_clear(pmd);
	pte_free_kernel(NULL, pte_table);
	put_page(virt_to_page(pmd));
}

static bool pmd_empty(pmd_t *pmd)
{
	struct page *pmd_page = virt_to_page(pmd);
	return page_count(pmd_page) == 1;
}

static void clear_pte_entry(pte_t *pte)
{
	if (pte_present(*pte)) {
		kvm_set_pte(pte, __pte(0));
		put_page(virt_to_page(pte));
	}
}

static bool pte_empty(pte_t *pte)
{
	struct page *pte_page = virt_to_page(pte);
	return page_count(pte_page) == 1;
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
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	phys_addr_t addr = start, end = start + size;
	u64 range;

	while (addr < end) {
		pgd = kvm->arch.pgd + pgd_index(addr);
		pud = pud_offset(pgd, addr);
		if (pud_none(*pud)) {
			addr += PUD_SIZE;
			continue;
		}

		pmd = pmd_offset(pud, addr);
		if (pmd_none(*pmd)) {
			addr += PMD_SIZE;
			continue;
		}

		pte = pte_offset_kernel(pmd, addr);
		clear_pte_entry(pte);
		range = PAGE_SIZE;

		/* If we emptied the pte, walk back up the ladder */
		if (pte_empty(pte)) {
			clear_pmd_entry(pmd);
			range = PMD_SIZE;
			if (pmd_empty(pmd)) {
				clear_pud_entry(pud);
				range = PUD_SIZE;
			}
		}

		addr += range;
	}
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


static int stage2_set_pte(struct kvm *kvm, struct kvm_mmu_memory_cache *cache,
			  phys_addr_t addr, const pte_t *new_pte, bool iomap)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte, old_pte;

	/* Create 2nd stage page table mapping - Level 1 */
	pgd = kvm->arch.pgd + pgd_index(addr);
	pud = pud_offset(pgd, addr);
	if (pud_none(*pud)) {
		if (!cache)
			return 0; /* ignore calls from kvm_set_spte_hva */
		pmd = mmu_memory_cache_alloc(cache);
		pud_populate(NULL, pud, pmd);
		pmd += pmd_index(addr);
		get_page(virt_to_page(pud));
	} else
		pmd = pmd_offset(pud, addr);

	/* Create 2nd stage page table mapping - Level 2 */
	if (pmd_none(*pmd)) {
		if (!cache)
			return 0; /* ignore calls from kvm_set_spte_hva */
		pte = mmu_memory_cache_alloc(cache);
		clean_pte_table(pte);
		pmd_populate_kernel(NULL, pmd, pte);
		pte += pte_index(addr);
		get_page(virt_to_page(pmd));
	} else
		pte = pte_offset_kernel(pmd, addr);

	if (iomap && pte_present(*pte))
		return -EFAULT;

	/* Create 2nd stage page table mapping - Level 3 */
	old_pte = *pte;
	kvm_set_pte(pte, *new_pte);
	if (pte_present(old_pte))
		kvm_tlb_flush_vmid(kvm);
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
		pte_t pte = pfn_pte(pfn, PAGE_S2_DEVICE | L_PTE_S2_RDWR);

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

static void coherent_icache_guest_page(struct kvm *kvm, gfn_t gfn)
{
	/*
	 * If we are going to insert an instruction page and the icache is
	 * either VIPT or PIPT, there is a potential problem where the host
	 * (or another VM) may have used the same page as this guest, and we
	 * read incorrect data from the icache.  If we're using a PIPT cache,
	 * we can invalidate just that page, but if we are using a VIPT cache
	 * we need to invalidate the entire icache - damn shame - as written
	 * in the ARM ARM (DDI 0406C.b - Page B3-1393).
	 *
	 * VIVT caches are tagged using both the ASID and the VMID and doesn't
	 * need any kind of flushing (DDI 0406C.b - Page B3-1392).
	 */
	if (icache_is_pipt()) {
		unsigned long hva = gfn_to_hva(kvm, gfn);
		__cpuc_coherent_user_range(hva, hva + PAGE_SIZE);
	} else if (!icache_is_vivt_asid_tagged()) {
		/* any kind of VIPT cache */
		__flush_icache_all();
	}
}

static int user_mem_abort(struct kvm_vcpu *vcpu, phys_addr_t fault_ipa,
			  gfn_t gfn, struct kvm_memory_slot *memslot,
			  unsigned long fault_status)
{
	pte_t new_pte;
	pfn_t pfn;
	int ret;
	bool write_fault, writable;
	unsigned long mmu_seq;
	struct kvm_mmu_memory_cache *memcache = &vcpu->arch.mmu_page_cache;

	write_fault = kvm_is_write_fault(kvm_vcpu_get_hsr(vcpu));
	if (fault_status == FSC_PERM && !write_fault) {
		kvm_err("Unexpected L2 read permission error\n");
		return -EFAULT;
	}

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

	pfn = gfn_to_pfn_prot(vcpu->kvm, gfn, write_fault, &writable);
	if (is_error_pfn(pfn))
		return -EFAULT;

	new_pte = pfn_pte(pfn, PAGE_S2);
	coherent_icache_guest_page(vcpu->kvm, gfn);

	spin_lock(&vcpu->kvm->mmu_lock);
	if (mmu_notifier_retry(vcpu->kvm, mmu_seq))
		goto out_unlock;
	if (writable) {
		pte_val(new_pte) |= L_PTE_S2_RDWR;
		kvm_set_pfn_dirty(pfn);
	}
	stage2_set_pte(vcpu->kvm, memcache, fault_ipa, &new_pte, false);

out_unlock:
	spin_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(pfn);
	return 0;
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
	unsigned long hsr_ec;
	unsigned long fault_status;
	phys_addr_t fault_ipa;
	struct kvm_memory_slot *memslot;
	bool is_iabt;
	gfn_t gfn;
	int ret, idx;

	hsr_ec = kvm_vcpu_get_hsr(vcpu) >> HSR_EC_SHIFT;
	is_iabt = (hsr_ec == HSR_EC_IABT);
	fault_ipa = kvm_vcpu_get_fault_ipa(vcpu);

	trace_kvm_guest_fault(*vcpu_pc(vcpu), kvm_vcpu_get_hsr(vcpu),
			      kvm_vcpu_get_hfar(vcpu), fault_ipa);

	/* Check the stage-2 fault is trans. fault or write fault */
	fault_status = (kvm_vcpu_get_hsr(vcpu) & HSR_FSC_TYPE);
	if (fault_status != FSC_FAULT && fault_status != FSC_PERM) {
		kvm_err("Unsupported fault status: EC=%#lx DFCS=%#lx\n",
			hsr_ec, fault_status);
		return -EFAULT;
	}

	idx = srcu_read_lock(&vcpu->kvm->srcu);

	gfn = fault_ipa >> PAGE_SHIFT;
	if (!kvm_is_visible_gfn(vcpu->kvm, gfn)) {
		if (is_iabt) {
			/* Prefetch Abort on I/O address */
			kvm_inject_pabt(vcpu, kvm_vcpu_get_hfar(vcpu));
			ret = 1;
			goto out_unlock;
		}

		if (fault_status != FSC_FAULT) {
			kvm_err("Unsupported fault status on io memory: %#lx\n",
				fault_status);
			ret = -EFAULT;
			goto out_unlock;
		}

		/* Adjust page offset */
		fault_ipa |= kvm_vcpu_get_hfar(vcpu) & ~PAGE_MASK;
		ret = io_mem_abort(vcpu, run, fault_ipa);
		goto out_unlock;
	}

	memslot = gfn_to_memslot(vcpu->kvm, gfn);

	ret = user_mem_abort(vcpu, fault_ipa, gfn, memslot, fault_status);
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
	kvm_tlb_flush_vmid(kvm);
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
	VM_BUG_ON(!virt_addr_valid(hyp_pgd));
	return virt_to_phys(hyp_pgd);
}

int kvm_mmu_init(void)
{
	if (!hyp_pgd) {
		kvm_err("Hyp mode PGD not allocated\n");
		return -ENOMEM;
	}

	return 0;
}

/**
 * kvm_clear_idmap - remove all idmaps from the hyp pgd
 *
 * Free the underlying pmds for all pgds in range and clear the pgds (but
 * don't free them) afterwards.
 */
void kvm_clear_hyp_idmap(void)
{
	unsigned long addr, end;
	unsigned long next;
	pgd_t *pgd = hyp_pgd;
	pud_t *pud;
	pmd_t *pmd;

	addr = virt_to_phys(__hyp_idmap_text_start);
	end = virt_to_phys(__hyp_idmap_text_end);

	pgd += pgd_index(addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		pud = pud_offset(pgd, addr);
		pmd = pmd_offset(pud, addr);

		pud_clear(pud);
		clean_pmd_entry(pmd);
		pmd_free(NULL, (pmd_t *)((unsigned long)pmd & PAGE_MASK));
	} while (pgd++, addr = next, addr < end);
}
