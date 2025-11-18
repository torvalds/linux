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

	gstage.kvm = kvm;
	gstage.flags = 0;
	gstage.vmid = READ_ONCE(kvm->arch.vmid.vmid);
	gstage.pgd = kvm->arch.pgd;

	spin_lock(&kvm->mmu_lock);
	kvm_riscv_gstage_wp_range(&gstage, start, end);
	spin_unlock(&kvm->mmu_lock);
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

	gstage.kvm = kvm;
	gstage.flags = 0;
	gstage.vmid = READ_ONCE(kvm->arch.vmid.vmid);
	gstage.pgd = kvm->arch.pgd;

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

		ret = kvm_mmu_topup_memory_cache(&pcache, kvm_riscv_gstage_pgd_levels);
		if (ret)
			goto out;

		spin_lock(&kvm->mmu_lock);
		ret = kvm_riscv_gstage_set_pte(&gstage, &pcache, &map);
		spin_unlock(&kvm->mmu_lock);
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

	gstage.kvm = kvm;
	gstage.flags = 0;
	gstage.vmid = READ_ONCE(kvm->arch.vmid.vmid);
	gstage.pgd = kvm->arch.pgd;

	spin_lock(&kvm->mmu_lock);
	kvm_riscv_gstage_unmap_range(&gstage, gpa, size, false);
	spin_unlock(&kvm->mmu_lock);
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

	gstage.kvm = kvm;
	gstage.flags = 0;
	gstage.vmid = READ_ONCE(kvm->arch.vmid.vmid);
	gstage.pgd = kvm->arch.pgd;

	kvm_riscv_gstage_wp_range(&gstage, start, end);
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

	gstage.kvm = kvm;
	gstage.flags = 0;
	gstage.vmid = READ_ONCE(kvm->arch.vmid.vmid);
	gstage.pgd = kvm->arch.pgd;

	spin_lock(&kvm->mmu_lock);
	kvm_riscv_gstage_unmap_range(&gstage, gpa, size, false);
	spin_unlock(&kvm->mmu_lock);
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	/*
	 * At this point memslot has been committed and there is an
	 * allocated dirty_bitmap[], dirty pages will be tracked while
	 * the memory slot is write protected.
	 */
	if (change != KVM_MR_DELETE && new->flags & KVM_MEM_LOG_DIRTY_PAGES)
		mmu_wp_memory_region(kvm, new->id);
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
	    (kvm_riscv_gstage_gpa_size >> PAGE_SHIFT))
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

	if (!kvm->arch.pgd)
		return false;

	gstage.kvm = kvm;
	gstage.flags = 0;
	gstage.vmid = READ_ONCE(kvm->arch.vmid.vmid);
	gstage.pgd = kvm->arch.pgd;
	kvm_riscv_gstage_unmap_range(&gstage, range->start << PAGE_SHIFT,
				     (range->end - range->start) << PAGE_SHIFT,
				     range->may_block);
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

	gstage.kvm = kvm;
	gstage.flags = 0;
	gstage.vmid = READ_ONCE(kvm->arch.vmid.vmid);
	gstage.pgd = kvm->arch.pgd;
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

	gstage.kvm = kvm;
	gstage.flags = 0;
	gstage.vmid = READ_ONCE(kvm->arch.vmid.vmid);
	gstage.pgd = kvm->arch.pgd;
	if (!kvm_riscv_gstage_get_leaf(&gstage, range->start << PAGE_SHIFT,
				       &ptep, &ptep_level))
		return false;

	return pte_young(ptep_get(ptep));
}

int kvm_riscv_mmu_map(struct kvm_vcpu *vcpu, struct kvm_memory_slot *memslot,
		      gpa_t gpa, unsigned long hva, bool is_write,
		      struct kvm_gstage_mapping *out_map)
{
	int ret;
	kvm_pfn_t hfn;
	bool writable;
	short vma_pageshift;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct vm_area_struct *vma;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_memory_cache *pcache = &vcpu->arch.mmu_page_cache;
	bool logging = (memslot->dirty_bitmap &&
			!(memslot->flags & KVM_MEM_READONLY)) ? true : false;
	unsigned long vma_pagesize, mmu_seq;
	struct kvm_gstage gstage;
	struct page *page;

	gstage.kvm = kvm;
	gstage.flags = 0;
	gstage.vmid = READ_ONCE(kvm->arch.vmid.vmid);
	gstage.pgd = kvm->arch.pgd;

	/* Setup initial state of output mapping */
	memset(out_map, 0, sizeof(*out_map));

	/* We need minimum second+third level pages */
	ret = kvm_mmu_topup_memory_cache(pcache, kvm_riscv_gstage_pgd_levels);
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

	if (is_vm_hugetlb_page(vma))
		vma_pageshift = huge_page_shift(hstate_vma(vma));
	else
		vma_pageshift = PAGE_SHIFT;
	vma_pagesize = 1ULL << vma_pageshift;
	if (logging || (vma->vm_flags & VM_PFNMAP))
		vma_pagesize = PAGE_SIZE;

	if (vma_pagesize == PMD_SIZE || vma_pagesize == PUD_SIZE)
		gfn = (gpa & huge_page_mask(hstate_vma(vma))) >> PAGE_SHIFT;

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

	spin_lock(&kvm->mmu_lock);

	if (mmu_invalidate_retry(kvm, mmu_seq))
		goto out_unlock;

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
	spin_unlock(&kvm->mmu_lock);
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

	return 0;
}

void kvm_riscv_mmu_free_pgd(struct kvm *kvm)
{
	struct kvm_gstage gstage;
	void *pgd = NULL;

	spin_lock(&kvm->mmu_lock);
	if (kvm->arch.pgd) {
		gstage.kvm = kvm;
		gstage.flags = 0;
		gstage.vmid = READ_ONCE(kvm->arch.vmid.vmid);
		gstage.pgd = kvm->arch.pgd;
		kvm_riscv_gstage_unmap_range(&gstage, 0UL, kvm_riscv_gstage_gpa_size, false);
		pgd = READ_ONCE(kvm->arch.pgd);
		kvm->arch.pgd = NULL;
		kvm->arch.pgd_phys = 0;
	}
	spin_unlock(&kvm->mmu_lock);

	if (pgd)
		free_pages((unsigned long)pgd, get_order(kvm_riscv_gstage_pgd_size));
}

void kvm_riscv_mmu_update_hgatp(struct kvm_vcpu *vcpu)
{
	unsigned long hgatp = kvm_riscv_gstage_mode << HGATP_MODE_SHIFT;
	struct kvm_arch *k = &vcpu->kvm->arch;

	hgatp |= (READ_ONCE(k->vmid.vmid) << HGATP_VMID_SHIFT) & HGATP_VMID;
	hgatp |= (k->pgd_phys >> PAGE_SHIFT) & HGATP_PPN;

	ncsr_write(CSR_HGATP, hgatp);

	if (!kvm_riscv_gstage_vmid_bits())
		kvm_riscv_local_hfence_gvma_all();
}
