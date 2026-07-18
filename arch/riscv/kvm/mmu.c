// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/errno.h>
#include <linux/hugetlb.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/kvm_host.h>
#include <linux/sched/signal.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_nacl.h>

static void mmu_wp_memory_region(struct kvm *kvm, int slot)
{
	struct kvm_memslots *slots = kvm_memslots(kvm);
	struct kvm_memory_slot *memslot = id_to_memslot(slots, slot);
	phys_addr_t start = memslot->base_gfn << PAGE_SHIFT;
	phys_addr_t end = (memslot->base_gfn + memslot->npages) << PAGE_SHIFT;
	struct kvm_gstage gstage;
	bool flush;

	kvm_riscv_gstage_init(&gstage, kvm);

	write_lock(&kvm->mmu_lock);
	flush = kvm_riscv_gstage_wp_range(&gstage, start, end);
	write_unlock(&kvm->mmu_lock);
	if (flush)
		kvm_flush_remote_tlbs_memslot(kvm, memslot);
}

int kvm_riscv_mmu_ioremap(struct kvm *kvm, gpa_t gpa, phys_addr_t hpa,
			  unsigned long size, bool writable, bool in_atomic)
{
	int ret = 0;
	pgprot_t prot;
	unsigned long pfn;
	phys_addr_t addr, end;
	struct kvm_mmu_memory_cache pcache = {
		.gfp_custom = (in_atomic) ? GFP_ATOMIC | __GFP_ACCOUNT : 0,
		.gfp_zero = __GFP_ZERO,
	};
	struct kvm_gstage_mapping map;
	struct kvm_gstage gstage;

	kvm_riscv_gstage_init(&gstage, kvm);

	end = (gpa + size + PAGE_SIZE - 1) & PAGE_MASK;
	pfn = __phys_to_pfn(hpa);
	prot = pgprot_noncached(PAGE_WRITE);

	for (addr = gpa; addr < end; addr += PAGE_SIZE) {
		map.addr = addr;
		map.pte = pfn_pte(pfn, prot);
		map.pte = pte_mkdirty(map.pte);
		map.level = 0;

		if (!writable)
			map.pte = pte_wrprotect(map.pte);

		ret = kvm_mmu_topup_memory_cache(&pcache, kvm->arch.pgd_levels);
		if (ret)
			goto out;

		write_lock(&kvm->mmu_lock);
		ret = kvm_riscv_gstage_set_pte(&gstage, &pcache, &map);
		write_unlock(&kvm->mmu_lock);
		if (ret)
			goto out;

		pfn++;
	}

out:
	kvm_mmu_free_memory_cache(&pcache);
	return ret;
}

void kvm_riscv_mmu_iounmap(struct kvm *kvm, gpa_t gpa, unsigned long size)
{
	struct kvm_gstage gstage;
	bool flush;

	kvm_riscv_gstage_init(&gstage, kvm);

	write_lock(&kvm->mmu_lock);
	flush = kvm_riscv_gstage_unmap_range(&gstage, gpa, size, false);
	write_unlock(&kvm->mmu_lock);

	if (flush)
		kvm_flush_remote_tlbs_range(kvm, gpa >> PAGE_SHIFT,
					    size >> PAGE_SHIFT);
}

void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *kvm,
					     struct kvm_memory_slot *slot,
					     gfn_t gfn_offset,
					     unsigned long mask)
{
	phys_addr_t base_gfn = slot->base_gfn + gfn_offset;
	phys_addr_t start = (base_gfn +  __ffs(mask)) << PAGE_SHIFT;
	phys_addr_t end = (base_gfn + __fls(mask) + 1) << PAGE_SHIFT;
	struct kvm_gstage gstage;
	bool flush;

	kvm_riscv_gstage_init(&gstage, kvm);

	flush = kvm_riscv_gstage_wp_range(&gstage, start, end);
	if (flush)
		kvm_flush_remote_tlbs_range(kvm, start >> PAGE_SHIFT,
					    (end - start) >> PAGE_SHIFT);
}

void kvm_arch_sync_dirty_log(struct kvm *kvm, struct kvm_memory_slot *memslot)
{
}

void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *free)
{
}

void kvm_arch_memslots_updated(struct kvm *kvm, u64 gen)
{
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	kvm_riscv_mmu_free_pgd(kvm);
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
	gpa_t gpa = slot->base_gfn << PAGE_SHIFT;
	phys_addr_t size = slot->npages << PAGE_SHIFT;
	struct kvm_gstage gstage;
	bool flush;

	kvm_riscv_gstage_init(&gstage, kvm);

	write_lock(&kvm->mmu_lock);
	flush = kvm_riscv_gstage_unmap_range(&gstage, gpa, size, false);
	write_unlock(&kvm->mmu_lock);
	if (flush)
		kvm_flush_remote_tlbs_range(kvm, gpa >> PAGE_SHIFT,
					    size >> PAGE_SHIFT);
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	/*
	 * At this point memslot has been committed and dirty pages will be
	 * tracked while the memory slot is write protected.
	 */
	if (change != KVM_MR_DELETE && new->flags & KVM_MEM_LOG_DIRTY_PAGES) {
		if (kvm_dirty_log_manual_protect_and_init_set(kvm))
			return;
		mmu_wp_memory_region(kvm, new->id);
	}
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
				const struct kvm_memory_slot *old,
				struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	hva_t hva, reg_end, size;
	bool writable;
	int ret = 0;

	if (change != KVM_MR_CREATE && change != KVM_MR_MOVE &&
			change != KVM_MR_FLAGS_ONLY)
		return 0;

	/*
	 * Prevent userspace from creating a memory region outside of the GPA
	 * space addressable by the KVM guest GPA space.
	 */
	if ((new->base_gfn + new->npages) >=
	     kvm_riscv_gstage_gpa_size(kvm->arch.pgd_levels) >> PAGE_SHIFT)
		return -EFAULT;

	hva = new->userspace_addr;
	size = new->npages << PAGE_SHIFT;
	reg_end = hva + size;
	writable = !(new->flags & KVM_MEM_READONLY);

	mmap_read_lock(current->mm);

	/*
	 * A memory region could potentially cover multiple VMAs, and
	 * any holes between them, so iterate over all of them.
	 *
	 *     +--------------------------------------------+
	 * +---------------+----------------+   +----------------+
	 * |   : VMA 1     |      VMA 2     |   |    VMA 3  :    |
	 * +---------------+----------------+   +----------------+
	 *     |               memory region                |
	 *     +--------------------------------------------+
	 */
	do {
		struct vm_area_struct *vma;
		hva_t vm_end;

		vma = find_vma_intersection(current->mm, hva, reg_end);
		if (!vma)
			break;

		/*
		 * Mapping a read-only VMA is only allowed if the
		 * memory region is configured as read-only.
		 */
		if (writable && !(vma->vm_flags & VM_WRITE)) {
			ret = -EPERM;
			break;
		}

		/* Take the intersection of this VMA with the memory region */
		vm_end = min(reg_end, vma->vm_end);

		if (vma->vm_flags & VM_PFNMAP) {
			/* IO region dirty page logging not allowed */
			if (new->flags & KVM_MEM_LOG_DIRTY_PAGES) {
				ret = -EINVAL;
				goto out;
			}
		}
		hva = vm_end;
	} while (hva < reg_end);

out:
	mmap_read_unlock(current->mm);
	return ret;
}

bool kvm_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range)
{
	struct kvm_gstage gstage;
	bool flush;

	if (!kvm->arch.pgd)
		return false;

	lockdep_assert_held_write(&kvm->mmu_lock);

	kvm_riscv_gstage_init(&gstage, kvm);
	flush = kvm_riscv_gstage_unmap_range(&gstage, range->start << PAGE_SHIFT,
					     (range->end - range->start) << PAGE_SHIFT,
					     range->may_block);
	if (flush)
		kvm_flush_remote_tlbs_range(kvm, range->start,
					    range->end - range->start);
	return false;
}

bool kvm_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	pte_t *ptep;
	u32 ptep_level = 0;
	u64 size = (range->end - range->start) << PAGE_SHIFT;
	struct kvm_gstage gstage;

	if (!kvm->arch.pgd)
		return false;

	WARN_ON(size != PAGE_SIZE && size != PMD_SIZE && size != PUD_SIZE);

	kvm_riscv_gstage_init(&gstage, kvm);
	if (!kvm_riscv_gstage_get_leaf(&gstage, range->start << PAGE_SHIFT,
				       &ptep, &ptep_level))
		return false;

	return ptep_test_and_clear_young(NULL, 0, ptep);
}

bool kvm_test_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	pte_t *ptep;
	u32 ptep_level = 0;
	u64 size = (range->end - range->start) << PAGE_SHIFT;
	struct kvm_gstage gstage;

	if (!kvm->arch.pgd)
		return false;

	WARN_ON(size != PAGE_SIZE && size != PMD_SIZE && size != PUD_SIZE);

	kvm_riscv_gstage_init(&gstage, kvm);
	if (!kvm_riscv_gstage_get_leaf(&gstage, range->start << PAGE_SHIFT,
				       &ptep, &ptep_level))
		return false;

	return pte_young(ptep_get(ptep));
}

static bool fault_supports_gstage_huge_mapping(struct kvm_memory_slot *memslot,
					       unsigned long hva,
					       unsigned long map_size)
{
	hva_t uaddr_start, uaddr_end;
	gpa_t gpa_start;
	size_t size;

	size = memslot->npages * PAGE_SIZE;
	uaddr_start = memslot->userspace_addr;
	uaddr_end = uaddr_start + size;

	gpa_start = memslot->base_gfn << PAGE_SHIFT;

	/*
	 * Pages belonging to memslots that don't have the same alignment
	 * within a huge page for userspace and GPA cannot be mapped with
	 * g-stage block entries, because we'll end up mapping the wrong pages.
	 *
	 * Consider a layout like the following:
	 *
	 *    memslot->userspace_addr:
	 *    +-----+--------------------+--------------------+---+
	 *    |abcde|fgh  vs-stage block  |    vs-stage block tv|xyz|
	 *    +-----+--------------------+--------------------+---+
	 *
	 *    memslot->base_gfn << PAGE_SHIFT:
	 *      +---+--------------------+--------------------+-----+
	 *      |abc|def  g-stage block  |    g-stage block   |tvxyz|
	 *      +---+--------------------+--------------------+-----+
	 *
	 * If we create those g-stage blocks, we'll end up with this incorrect
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
	return (hva >= ALIGN(uaddr_start, map_size)) &&
	       (hva < ALIGN_DOWN(uaddr_end, map_size));
}

static int get_hva_mapping_size(struct kvm *kvm,
				unsigned long hva)
{
	int size = PAGE_SIZE;
	unsigned long flags;
	pgd_t pgd;
	p4d_t p4d;
	pud_t pud;
	pmd_t pmd;

	/*
	 * Disable IRQs to prevent concurrent tear down of host page tables,
	 * e.g. if the primary MMU promotes a P*D to a huge page and then frees
	 * the original page table.
	 */
	local_irq_save(flags);

	/*
	 * Read each entry once.  As above, a non-leaf entry can be promoted to
	 * a huge page _during_ this walk.  Re-reading the entry could send the
	 * walk into the weeks, e.g. p*d_leaf() returns false (sees the old
	 * value) and then p*d_offset() walks into the target huge page instead
	 * of the old page table (sees the new value).
	 */
	pgd = pgdp_get(pgd_offset(kvm->mm, hva));
	if (pgd_none(pgd))
		goto out;

	p4d = p4dp_get(p4d_offset(&pgd, hva));
	if (p4d_none(p4d) || !p4d_present(p4d))
		goto out;

	pud = pudp_get(pud_offset(&p4d, hva));
	if (pud_none(pud) || !pud_present(pud))
		goto out;

	if (pud_leaf(pud)) {
		size = PUD_SIZE;
		goto out;
	}

	pmd = pmdp_get(pmd_offset(&pud, hva));
	if (pmd_none(pmd) || !pmd_present(pmd))
		goto out;

	if (pmd_leaf(pmd))
		size = PMD_SIZE;

out:
	local_irq_restore(flags);
	return size;
}

static unsigned long transparent_hugepage_adjust(struct kvm *kvm,
						 struct kvm_memory_slot *memslot,
						 unsigned long hva,
						 kvm_pfn_t *hfnp, gpa_t *gpa)
{
	kvm_pfn_t hfn = *hfnp;

	/*
	 * Make sure the adjustment is done only for THP pages. Also make
	 * sure that the HVA and GPA are sufficiently aligned and that the
	 * block map is contained within the memslot.
	 */
	if (fault_supports_gstage_huge_mapping(memslot, hva, PMD_SIZE)) {
		int sz;

		sz = get_hva_mapping_size(kvm, hva);
		if (sz < PMD_SIZE)
			return sz;

		*gpa &= PMD_MASK;
		hfn &= ~(PTRS_PER_PMD - 1);
		*hfnp = hfn;

		return PMD_SIZE;
	}

	return PAGE_SIZE;
}

static unsigned long hugetlb_mapping_size(struct kvm_memory_slot *memslot,
					  unsigned long hva,
					  unsigned long map_size)
{
	switch (map_size) {
#ifndef CONFIG_32BIT
	case PUD_SIZE:
		if (fault_supports_gstage_huge_mapping(memslot, hva, PUD_SIZE))
			return PUD_SIZE;
		fallthrough;
#endif
	case PMD_SIZE:
		if (fault_supports_gstage_huge_mapping(memslot, hva, PMD_SIZE))
			return PMD_SIZE;
		fallthrough;
	case PAGE_SIZE:
		return PAGE_SIZE;
	default:
		return map_size;
	}
}

static bool kvm_riscv_mmu_dirty_log_write_fault_fast(struct kvm *kvm,
						     struct kvm_memory_slot *memslot,
						     gpa_t gpa,
						     struct kvm_gstage_mapping *out_map)
{
	struct kvm_gstage gstage;
	unsigned long mmu_seq;
	pte_t old_pte, new_pte;
	pte_t *ptep;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	u32 ptep_level;
	bool dirty_marked = false;
	bool ret;

	kvm_riscv_gstage_init(&gstage, kvm);
	mmu_seq = kvm->mmu_invalidate_seq;

	read_lock(&kvm->mmu_lock);

	if (mmu_invalidate_retry_gfn(kvm, mmu_seq, gfn)) {
		ret = false;
		goto out_unlock;
	}

	if (!kvm_riscv_gstage_get_leaf(&gstage, gpa, &ptep, &ptep_level) ||
	    ptep_level) {
		ret = false;
		goto out_unlock;
	}

	for (;;) {
		old_pte = ptep_get(ptep);
		if (!(pte_val(old_pte) & _PAGE_LEAF)) {
			ret = false;
			break;
		}

		if (!dirty_marked) {
			mark_page_dirty_in_slot(kvm, memslot, gfn);
			dirty_marked = true;
		}

		if ((pte_val(old_pte) & (_PAGE_WRITE | _PAGE_DIRTY)) ==
		    (_PAGE_WRITE | _PAGE_DIRTY)) {
			new_pte = old_pte;
			ret = true;
			break;
		}

		new_pte = pte_mkdirty(pte_mkwrite_novma(old_pte));

		if (kvm_riscv_gstage_try_update_pte(&gstage, ptep_level, gpa,
						    ptep, old_pte, new_pte)) {
			ret = true;
			break;
		}
		cpu_relax();
	}

out_unlock:
	read_unlock(&kvm->mmu_lock);

	if (ret) {
		out_map->addr = gpa & PAGE_MASK;
		out_map->level = 0;
		out_map->pte = new_pte;
	}

	return ret;
}

int kvm_riscv_mmu_map(struct kvm_vcpu *vcpu, struct kvm_memory_slot *memslot,
		      gpa_t gpa, unsigned long hva, bool is_write,
		      struct kvm_gstage_mapping *out_map)
{
	int ret;
	kvm_pfn_t hfn;
	bool is_hugetlb;
	bool writable;
	short vma_pageshift;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct vm_area_struct *vma;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_memory_cache *pcache = &vcpu->arch.mmu_page_cache;
	bool logging = kvm_slot_dirty_track_enabled(memslot) &&
		       !(memslot->flags & KVM_MEM_READONLY);
	unsigned long vma_pagesize, mmu_seq;
	struct kvm_gstage gstage;
	struct page *page;

	kvm_riscv_gstage_init(&gstage, kvm);

	/* Setup initial state of output mapping */
	memset(out_map, 0, sizeof(*out_map));

	if (is_write && logging &&
	    kvm_riscv_mmu_dirty_log_write_fault_fast(kvm, memslot, gpa, out_map))
		return 0;

	/* We need minimum second+third level pages */
	ret = kvm_mmu_topup_memory_cache(pcache, kvm->arch.pgd_levels);
	if (ret) {
		kvm_err("Failed to topup G-stage cache\n");
		return ret;
	}

	mmap_read_lock(current->mm);

	vma = vma_lookup(current->mm, hva);
	if (unlikely(!vma)) {
		kvm_err("Failed to find VMA for hva 0x%lx\n", hva);
		mmap_read_unlock(current->mm);
		return -EFAULT;
	}

	is_hugetlb = is_vm_hugetlb_page(vma);
	if (is_hugetlb)
		vma_pageshift = huge_page_shift(hstate_vma(vma));
	else
		vma_pageshift = PAGE_SHIFT;
	vma_pagesize = 1ULL << vma_pageshift;
	if (logging || (vma->vm_flags & VM_PFNMAP))
		vma_pagesize = PAGE_SIZE;
	else if (is_hugetlb)
		vma_pagesize = hugetlb_mapping_size(memslot, hva, vma_pagesize);

	/*
	 * For hugetlb mappings, vma_pagesize might have been reduced from the
	 * VMA size to a smaller safe mapping size.
	 */
	if (vma_pagesize == PMD_SIZE || vma_pagesize == PUD_SIZE)
		gfn = ALIGN_DOWN(gpa, vma_pagesize) >> PAGE_SHIFT;

	/*
	 * Read mmu_invalidate_seq so that KVM can detect if the results of
	 * vma_lookup() or __kvm_faultin_pfn() become stale prior to acquiring
	 * kvm->mmu_lock.
	 *
	 * Rely on mmap_read_unlock() for an implicit smp_rmb(), which pairs
	 * with the smp_wmb() in kvm_mmu_invalidate_end().
	 */
	mmu_seq = kvm->mmu_invalidate_seq;
	mmap_read_unlock(current->mm);

	if (vma_pagesize != PUD_SIZE &&
	    vma_pagesize != PMD_SIZE &&
	    vma_pagesize != PAGE_SIZE) {
		kvm_err("Invalid VMA page size 0x%lx\n", vma_pagesize);
		return -EFAULT;
	}

	hfn = __kvm_faultin_pfn(memslot, gfn, is_write ? FOLL_WRITE : 0,
				&writable, &page);
	if (hfn == KVM_PFN_ERR_HWPOISON) {
		send_sig_mceerr(BUS_MCEERR_AR, (void __user *)hva,
				vma_pageshift, current);
		return 0;
	}
	if (is_error_noslot_pfn(hfn))
		return -EFAULT;

	/*
	 * If logging is active then we allow writable pages only
	 * for write faults.
	 */
	if (logging && !is_write)
		writable = false;

	write_lock(&kvm->mmu_lock);

	if (mmu_invalidate_retry(kvm, mmu_seq))
		goto out_unlock;

	/*
	 * Check if we are backed by a THP and thus use block mapping if
	 * possible. Hugetlb mappings already selected their target size above,
	 * so do not promote them through the THP helper.
	 */
	if (!logging && !is_hugetlb && vma_pagesize == PAGE_SIZE)
		vma_pagesize = transparent_hugepage_adjust(kvm, memslot, hva, &hfn, &gpa);

	if (writable) {
		mark_page_dirty_in_slot(kvm, memslot, gfn);
		ret = kvm_riscv_gstage_map_page(&gstage, pcache, gpa, hfn << PAGE_SHIFT,
						vma_pagesize, false, true, out_map);
	} else {
		ret = kvm_riscv_gstage_map_page(&gstage, pcache, gpa, hfn << PAGE_SHIFT,
						vma_pagesize, true, true, out_map);
	}

	if (ret)
		kvm_err("Failed to map in G-stage\n");

out_unlock:
	kvm_release_faultin_page(kvm, page, ret && ret != -EEXIST, writable);
	write_unlock(&kvm->mmu_lock);
	return ret;
}

int kvm_riscv_mmu_alloc_pgd(struct kvm *kvm)
{
	struct page *pgd_page;

	if (kvm->arch.pgd != NULL) {
		kvm_err("kvm_arch already initialized?\n");
		return -EINVAL;
	}

	pgd_page = alloc_pages(GFP_KERNEL | __GFP_ZERO,
				get_order(kvm_riscv_gstage_pgd_size));
	if (!pgd_page)
		return -ENOMEM;
	kvm->arch.pgd = page_to_virt(pgd_page);
	kvm->arch.pgd_phys = page_to_phys(pgd_page);
	kvm->arch.pgd_levels = kvm_riscv_gstage_max_pgd_levels;

	return 0;
}

void kvm_riscv_mmu_free_pgd(struct kvm *kvm)
{
	struct kvm_gstage gstage;
	void *pgd = NULL;
	bool flush = false;

	write_lock(&kvm->mmu_lock);
	if (kvm->arch.pgd) {
		kvm_riscv_gstage_init(&gstage, kvm);
		flush = kvm_riscv_gstage_unmap_range(&gstage, 0UL,
			kvm_riscv_gstage_gpa_size(kvm->arch.pgd_levels), false);
		pgd = READ_ONCE(kvm->arch.pgd);
		kvm->arch.pgd = NULL;
		kvm->arch.pgd_phys = 0;
		kvm->arch.pgd_levels = 0;
	}
	write_unlock(&kvm->mmu_lock);

	if (flush)
		kvm_flush_remote_tlbs(kvm);

	if (pgd)
		free_pages((unsigned long)pgd, get_order(kvm_riscv_gstage_pgd_size));
}

void kvm_riscv_mmu_update_hgatp(struct kvm_vcpu *vcpu)
{
	struct kvm_arch *ka = &vcpu->kvm->arch;
	unsigned long hgatp = kvm_riscv_gstage_mode(ka->pgd_levels)
			      << HGATP_MODE_SHIFT;

	hgatp |= (READ_ONCE(ka->vmid.vmid) << HGATP_VMID_SHIFT) & HGATP_VMID;
	hgatp |= (ka->pgd_phys >> PAGE_SHIFT) & HGATP_PPN;

	ncsr_write(CSR_HGATP, hgatp);

	if (!kvm_riscv_gstage_vmid_bits())
		kvm_riscv_local_hfence_gvma_all();
}
