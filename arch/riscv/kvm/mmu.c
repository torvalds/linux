// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/hugetlb.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/kvm_host.h>
#include <linux/sched/signal.h>
#include <asm/csr.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sbi.h>

#ifdef CONFIG_64BIT
static unsigned long stage2_mode = (HGATP_MODE_SV39X4 << HGATP_MODE_SHIFT);
static unsigned long stage2_pgd_levels = 3;
#define stage2_index_bits	9
#else
static unsigned long stage2_mode = (HGATP_MODE_SV32X4 << HGATP_MODE_SHIFT);
static unsigned long stage2_pgd_levels = 2;
#define stage2_index_bits	10
#endif

#define stage2_pgd_xbits	2
#define stage2_pgd_size	(1UL << (HGATP_PAGE_SHIFT + stage2_pgd_xbits))
#define stage2_gpa_bits	(HGATP_PAGE_SHIFT + \
			 (stage2_pgd_levels * stage2_index_bits) + \
			 stage2_pgd_xbits)
#define stage2_gpa_size	((gpa_t)(1ULL << stage2_gpa_bits))

#define stage2_pte_leaf(__ptep)	\
	(pte_val(*(__ptep)) & (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC))

static inline unsigned long stage2_pte_index(gpa_t addr, u32 level)
{
	unsigned long mask;
	unsigned long shift = HGATP_PAGE_SHIFT + (stage2_index_bits * level);

	if (level == (stage2_pgd_levels - 1))
		mask = (PTRS_PER_PTE * (1UL << stage2_pgd_xbits)) - 1;
	else
		mask = PTRS_PER_PTE - 1;

	return (addr >> shift) & mask;
}

static inline unsigned long stage2_pte_page_vaddr(pte_t pte)
{
	return (unsigned long)pfn_to_virt(pte_val(pte) >> _PAGE_PFN_SHIFT);
}

static int stage2_page_size_to_level(unsigned long page_size, u32 *out_level)
{
	u32 i;
	unsigned long psz = 1UL << 12;

	for (i = 0; i < stage2_pgd_levels; i++) {
		if (page_size == (psz << (i * stage2_index_bits))) {
			*out_level = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int stage2_level_to_page_size(u32 level, unsigned long *out_pgsize)
{
	if (stage2_pgd_levels < level)
		return -EINVAL;

	*out_pgsize = 1UL << (12 + (level * stage2_index_bits));

	return 0;
}

static int stage2_cache_topup(struct kvm_mmu_page_cache *pcache,
			      int min, int max)
{
	void *page;

	BUG_ON(max > KVM_MMU_PAGE_CACHE_NR_OBJS);
	if (pcache->nobjs >= min)
		return 0;
	while (pcache->nobjs < max) {
		page = (void *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
		if (!page)
			return -ENOMEM;
		pcache->objects[pcache->nobjs++] = page;
	}

	return 0;
}

static void stage2_cache_flush(struct kvm_mmu_page_cache *pcache)
{
	while (pcache && pcache->nobjs)
		free_page((unsigned long)pcache->objects[--pcache->nobjs]);
}

static void *stage2_cache_alloc(struct kvm_mmu_page_cache *pcache)
{
	void *p;

	if (!pcache)
		return NULL;

	BUG_ON(!pcache->nobjs);
	p = pcache->objects[--pcache->nobjs];

	return p;
}

static bool stage2_get_leaf_entry(struct kvm *kvm, gpa_t addr,
				  pte_t **ptepp, u32 *ptep_level)
{
	pte_t *ptep;
	u32 current_level = stage2_pgd_levels - 1;

	*ptep_level = current_level;
	ptep = (pte_t *)kvm->arch.pgd;
	ptep = &ptep[stage2_pte_index(addr, current_level)];
	while (ptep && pte_val(*ptep)) {
		if (stage2_pte_leaf(ptep)) {
			*ptep_level = current_level;
			*ptepp = ptep;
			return true;
		}

		if (current_level) {
			current_level--;
			*ptep_level = current_level;
			ptep = (pte_t *)stage2_pte_page_vaddr(*ptep);
			ptep = &ptep[stage2_pte_index(addr, current_level)];
		} else {
			ptep = NULL;
		}
	}

	return false;
}

static void stage2_remote_tlb_flush(struct kvm *kvm, u32 level, gpa_t addr)
{
	struct cpumask hmask;
	unsigned long size = PAGE_SIZE;
	struct kvm_vmid *vmid = &kvm->arch.vmid;

	if (stage2_level_to_page_size(level, &size))
		return;
	addr &= ~(size - 1);

	/*
	 * TODO: Instead of cpu_online_mask, we should only target CPUs
	 * where the Guest/VM is running.
	 */
	preempt_disable();
	riscv_cpuid_to_hartid_mask(cpu_online_mask, &hmask);
	sbi_remote_hfence_gvma_vmid(cpumask_bits(&hmask), addr, size,
				    READ_ONCE(vmid->vmid));
	preempt_enable();
}

static int stage2_set_pte(struct kvm *kvm, u32 level,
			   struct kvm_mmu_page_cache *pcache,
			   gpa_t addr, const pte_t *new_pte)
{
	u32 current_level = stage2_pgd_levels - 1;
	pte_t *next_ptep = (pte_t *)kvm->arch.pgd;
	pte_t *ptep = &next_ptep[stage2_pte_index(addr, current_level)];

	if (current_level < level)
		return -EINVAL;

	while (current_level != level) {
		if (stage2_pte_leaf(ptep))
			return -EEXIST;

		if (!pte_val(*ptep)) {
			next_ptep = stage2_cache_alloc(pcache);
			if (!next_ptep)
				return -ENOMEM;
			*ptep = pfn_pte(PFN_DOWN(__pa(next_ptep)),
					__pgprot(_PAGE_TABLE));
		} else {
			if (stage2_pte_leaf(ptep))
				return -EEXIST;
			next_ptep = (pte_t *)stage2_pte_page_vaddr(*ptep);
		}

		current_level--;
		ptep = &next_ptep[stage2_pte_index(addr, current_level)];
	}

	*ptep = *new_pte;
	if (stage2_pte_leaf(ptep))
		stage2_remote_tlb_flush(kvm, current_level, addr);

	return 0;
}

static int stage2_map_page(struct kvm *kvm,
			   struct kvm_mmu_page_cache *pcache,
			   gpa_t gpa, phys_addr_t hpa,
			   unsigned long page_size,
			   bool page_rdonly, bool page_exec)
{
	int ret;
	u32 level = 0;
	pte_t new_pte;
	pgprot_t prot;

	ret = stage2_page_size_to_level(page_size, &level);
	if (ret)
		return ret;

	/*
	 * A RISC-V implementation can choose to either:
	 * 1) Update 'A' and 'D' PTE bits in hardware
	 * 2) Generate page fault when 'A' and/or 'D' bits are not set
	 *    PTE so that software can update these bits.
	 *
	 * We support both options mentioned above. To achieve this, we
	 * always set 'A' and 'D' PTE bits at time of creating stage2
	 * mapping. To support KVM dirty page logging with both options
	 * mentioned above, we will write-protect stage2 PTEs to track
	 * dirty pages.
	 */

	if (page_exec) {
		if (page_rdonly)
			prot = PAGE_READ_EXEC;
		else
			prot = PAGE_WRITE_EXEC;
	} else {
		if (page_rdonly)
			prot = PAGE_READ;
		else
			prot = PAGE_WRITE;
	}
	new_pte = pfn_pte(PFN_DOWN(hpa), prot);
	new_pte = pte_mkdirty(new_pte);

	return stage2_set_pte(kvm, level, pcache, gpa, &new_pte);
}

enum stage2_op {
	STAGE2_OP_NOP = 0,	/* Nothing */
	STAGE2_OP_CLEAR,	/* Clear/Unmap */
	STAGE2_OP_WP,		/* Write-protect */
};

static void stage2_op_pte(struct kvm *kvm, gpa_t addr,
			  pte_t *ptep, u32 ptep_level, enum stage2_op op)
{
	int i, ret;
	pte_t *next_ptep;
	u32 next_ptep_level;
	unsigned long next_page_size, page_size;

	ret = stage2_level_to_page_size(ptep_level, &page_size);
	if (ret)
		return;

	BUG_ON(addr & (page_size - 1));

	if (!pte_val(*ptep))
		return;

	if (ptep_level && !stage2_pte_leaf(ptep)) {
		next_ptep = (pte_t *)stage2_pte_page_vaddr(*ptep);
		next_ptep_level = ptep_level - 1;
		ret = stage2_level_to_page_size(next_ptep_level,
						&next_page_size);
		if (ret)
			return;

		if (op == STAGE2_OP_CLEAR)
			set_pte(ptep, __pte(0));
		for (i = 0; i < PTRS_PER_PTE; i++)
			stage2_op_pte(kvm, addr + i * next_page_size,
					&next_ptep[i], next_ptep_level, op);
		if (op == STAGE2_OP_CLEAR)
			put_page(virt_to_page(next_ptep));
	} else {
		if (op == STAGE2_OP_CLEAR)
			set_pte(ptep, __pte(0));
		else if (op == STAGE2_OP_WP)
			set_pte(ptep, __pte(pte_val(*ptep) & ~_PAGE_WRITE));
		stage2_remote_tlb_flush(kvm, ptep_level, addr);
	}
}

static void stage2_unmap_range(struct kvm *kvm, gpa_t start,
			       gpa_t size, bool may_block)
{
	int ret;
	pte_t *ptep;
	u32 ptep_level;
	bool found_leaf;
	unsigned long page_size;
	gpa_t addr = start, end = start + size;

	while (addr < end) {
		found_leaf = stage2_get_leaf_entry(kvm, addr,
						   &ptep, &ptep_level);
		ret = stage2_level_to_page_size(ptep_level, &page_size);
		if (ret)
			break;

		if (!found_leaf)
			goto next;

		if (!(addr & (page_size - 1)) && ((end - addr) >= page_size))
			stage2_op_pte(kvm, addr, ptep,
				      ptep_level, STAGE2_OP_CLEAR);

next:
		addr += page_size;

		/*
		 * If the range is too large, release the kvm->mmu_lock
		 * to prevent starvation and lockup detector warnings.
		 */
		if (may_block && addr < end)
			cond_resched_lock(&kvm->mmu_lock);
	}
}

static void stage2_wp_range(struct kvm *kvm, gpa_t start, gpa_t end)
{
	int ret;
	pte_t *ptep;
	u32 ptep_level;
	bool found_leaf;
	gpa_t addr = start;
	unsigned long page_size;

	while (addr < end) {
		found_leaf = stage2_get_leaf_entry(kvm, addr,
						   &ptep, &ptep_level);
		ret = stage2_level_to_page_size(ptep_level, &page_size);
		if (ret)
			break;

		if (!found_leaf)
			goto next;

		if (!(addr & (page_size - 1)) && ((end - addr) >= page_size))
			stage2_op_pte(kvm, addr, ptep,
				      ptep_level, STAGE2_OP_WP);

next:
		addr += page_size;
	}
}

static void stage2_wp_memory_region(struct kvm *kvm, int slot)
{
	struct kvm_memslots *slots = kvm_memslots(kvm);
	struct kvm_memory_slot *memslot = id_to_memslot(slots, slot);
	phys_addr_t start = memslot->base_gfn << PAGE_SHIFT;
	phys_addr_t end = (memslot->base_gfn + memslot->npages) << PAGE_SHIFT;

	spin_lock(&kvm->mmu_lock);
	stage2_wp_range(kvm, start, end);
	spin_unlock(&kvm->mmu_lock);
	kvm_flush_remote_tlbs(kvm);
}

static int stage2_ioremap(struct kvm *kvm, gpa_t gpa, phys_addr_t hpa,
			  unsigned long size, bool writable)
{
	pte_t pte;
	int ret = 0;
	unsigned long pfn;
	phys_addr_t addr, end;
	struct kvm_mmu_page_cache pcache = { 0, };

	end = (gpa + size + PAGE_SIZE - 1) & PAGE_MASK;
	pfn = __phys_to_pfn(hpa);

	for (addr = gpa; addr < end; addr += PAGE_SIZE) {
		pte = pfn_pte(pfn, PAGE_KERNEL);

		if (!writable)
			pte = pte_wrprotect(pte);

		ret = stage2_cache_topup(&pcache,
					 stage2_pgd_levels,
					 KVM_MMU_PAGE_CACHE_NR_OBJS);
		if (ret)
			goto out;

		spin_lock(&kvm->mmu_lock);
		ret = stage2_set_pte(kvm, 0, &pcache, addr, &pte);
		spin_unlock(&kvm->mmu_lock);
		if (ret)
			goto out;

		pfn++;
	}

out:
	stage2_cache_flush(&pcache);
	return ret;
}

void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *kvm,
					     struct kvm_memory_slot *slot,
					     gfn_t gfn_offset,
					     unsigned long mask)
{
	phys_addr_t base_gfn = slot->base_gfn + gfn_offset;
	phys_addr_t start = (base_gfn +  __ffs(mask)) << PAGE_SHIFT;
	phys_addr_t end = (base_gfn + __fls(mask) + 1) << PAGE_SHIFT;

	stage2_wp_range(kvm, start, end);
}

void kvm_arch_sync_dirty_log(struct kvm *kvm, struct kvm_memory_slot *memslot)
{
}

void kvm_arch_flush_remote_tlbs_memslot(struct kvm *kvm,
					const struct kvm_memory_slot *memslot)
{
	kvm_flush_remote_tlbs(kvm);
}

void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *free)
{
}

void kvm_arch_memslots_updated(struct kvm *kvm, u64 gen)
{
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	kvm_riscv_stage2_free_pgd(kvm);
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
	gpa_t gpa = slot->base_gfn << PAGE_SHIFT;
	phys_addr_t size = slot->npages << PAGE_SHIFT;

	spin_lock(&kvm->mmu_lock);
	stage2_unmap_range(kvm, gpa, size, false);
	spin_unlock(&kvm->mmu_lock);
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				const struct kvm_userspace_memory_region *mem,
				struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	/*
	 * At this point memslot has been committed and there is an
	 * allocated dirty_bitmap[], dirty pages will be tracked while
	 * the memory slot is write protected.
	 */
	if (change != KVM_MR_DELETE && mem->flags & KVM_MEM_LOG_DIRTY_PAGES)
		stage2_wp_memory_region(kvm, mem->slot);
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
	 * Prevent userspace from creating a memory region outside of the GPA
	 * space addressable by the KVM guest GPA space.
	 */
	if ((memslot->base_gfn + memslot->npages) >=
	    (stage2_gpa_size >> PAGE_SHIFT))
		return -EFAULT;

	mmap_read_lock(current->mm);

	/*
	 * A memory region could potentially cover multiple VMAs, and
	 * any holes between them, so iterate over all of them to find
	 * out if we can map any of them right now.
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
		 * Mapping a read-only VMA is only allowed if the
		 * memory region is configured as read-only.
		 */
		if (writable && !(vma->vm_flags & VM_WRITE)) {
			ret = -EPERM;
			break;
		}

		/* Take the intersection of this VMA with the memory region */
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

			ret = stage2_ioremap(kvm, gpa, pa,
					     vm_end - vm_start, writable);
			if (ret)
				break;
		}
		hva = vm_end;
	} while (hva < reg_end);

	if (change == KVM_MR_FLAGS_ONLY)
		goto out;

	spin_lock(&kvm->mmu_lock);
	if (ret)
		stage2_unmap_range(kvm, mem->guest_phys_addr,
				   mem->memory_size, false);
	spin_unlock(&kvm->mmu_lock);

out:
	mmap_read_unlock(current->mm);
	return ret;
}

bool kvm_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range)
{
	if (!kvm->arch.pgd)
		return false;

	stage2_unmap_range(kvm, range->start << PAGE_SHIFT,
			   (range->end - range->start) << PAGE_SHIFT,
			   range->may_block);
	return false;
}

bool kvm_set_spte_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	int ret;
	kvm_pfn_t pfn = pte_pfn(range->pte);

	if (!kvm->arch.pgd)
		return false;

	WARN_ON(range->end - range->start != 1);

	ret = stage2_map_page(kvm, NULL, range->start << PAGE_SHIFT,
			      __pfn_to_phys(pfn), PAGE_SIZE, true, true);
	if (ret) {
		kvm_debug("Failed to map stage2 page (error %d)\n", ret);
		return true;
	}

	return false;
}

bool kvm_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	pte_t *ptep;
	u32 ptep_level = 0;
	u64 size = (range->end - range->start) << PAGE_SHIFT;

	if (!kvm->arch.pgd)
		return false;

	WARN_ON(size != PAGE_SIZE && size != PMD_SIZE && size != PGDIR_SIZE);

	if (!stage2_get_leaf_entry(kvm, range->start << PAGE_SHIFT,
				   &ptep, &ptep_level))
		return false;

	return ptep_test_and_clear_young(NULL, 0, ptep);
}

bool kvm_test_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	pte_t *ptep;
	u32 ptep_level = 0;
	u64 size = (range->end - range->start) << PAGE_SHIFT;

	if (!kvm->arch.pgd)
		return false;

	WARN_ON(size != PAGE_SIZE && size != PMD_SIZE && size != PGDIR_SIZE);

	if (!stage2_get_leaf_entry(kvm, range->start << PAGE_SHIFT,
				   &ptep, &ptep_level))
		return false;

	return pte_young(*ptep);
}

int kvm_riscv_stage2_map(struct kvm_vcpu *vcpu,
			 struct kvm_memory_slot *memslot,
			 gpa_t gpa, unsigned long hva, bool is_write)
{
	int ret;
	kvm_pfn_t hfn;
	bool writeable;
	short vma_pageshift;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct vm_area_struct *vma;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_page_cache *pcache = &vcpu->arch.mmu_page_cache;
	bool logging = (memslot->dirty_bitmap &&
			!(memslot->flags & KVM_MEM_READONLY)) ? true : false;
	unsigned long vma_pagesize, mmu_seq;

	mmap_read_lock(current->mm);

	vma = find_vma_intersection(current->mm, hva, hva + 1);
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

	if (vma_pagesize == PMD_SIZE || vma_pagesize == PGDIR_SIZE)
		gfn = (gpa & huge_page_mask(hstate_vma(vma))) >> PAGE_SHIFT;

	mmap_read_unlock(current->mm);

	if (vma_pagesize != PGDIR_SIZE &&
	    vma_pagesize != PMD_SIZE &&
	    vma_pagesize != PAGE_SIZE) {
		kvm_err("Invalid VMA page size 0x%lx\n", vma_pagesize);
		return -EFAULT;
	}

	/* We need minimum second+third level pages */
	ret = stage2_cache_topup(pcache, stage2_pgd_levels,
				 KVM_MMU_PAGE_CACHE_NR_OBJS);
	if (ret) {
		kvm_err("Failed to topup stage2 cache\n");
		return ret;
	}

	mmu_seq = kvm->mmu_notifier_seq;

	hfn = gfn_to_pfn_prot(kvm, gfn, is_write, &writeable);
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
		writeable = false;

	spin_lock(&kvm->mmu_lock);

	if (mmu_notifier_retry(kvm, mmu_seq))
		goto out_unlock;

	if (writeable) {
		kvm_set_pfn_dirty(hfn);
		mark_page_dirty(kvm, gfn);
		ret = stage2_map_page(kvm, pcache, gpa, hfn << PAGE_SHIFT,
				      vma_pagesize, false, true);
	} else {
		ret = stage2_map_page(kvm, pcache, gpa, hfn << PAGE_SHIFT,
				      vma_pagesize, true, true);
	}

	if (ret)
		kvm_err("Failed to map in stage2\n");

out_unlock:
	spin_unlock(&kvm->mmu_lock);
	kvm_set_pfn_accessed(hfn);
	kvm_release_pfn_clean(hfn);
	return ret;
}

void kvm_riscv_stage2_flush_cache(struct kvm_vcpu *vcpu)
{
	stage2_cache_flush(&vcpu->arch.mmu_page_cache);
}

int kvm_riscv_stage2_alloc_pgd(struct kvm *kvm)
{
	struct page *pgd_page;

	if (kvm->arch.pgd != NULL) {
		kvm_err("kvm_arch already initialized?\n");
		return -EINVAL;
	}

	pgd_page = alloc_pages(GFP_KERNEL | __GFP_ZERO,
				get_order(stage2_pgd_size));
	if (!pgd_page)
		return -ENOMEM;
	kvm->arch.pgd = page_to_virt(pgd_page);
	kvm->arch.pgd_phys = page_to_phys(pgd_page);

	return 0;
}

void kvm_riscv_stage2_free_pgd(struct kvm *kvm)
{
	void *pgd = NULL;

	spin_lock(&kvm->mmu_lock);
	if (kvm->arch.pgd) {
		stage2_unmap_range(kvm, 0UL, stage2_gpa_size, false);
		pgd = READ_ONCE(kvm->arch.pgd);
		kvm->arch.pgd = NULL;
		kvm->arch.pgd_phys = 0;
	}
	spin_unlock(&kvm->mmu_lock);

	if (pgd)
		free_pages((unsigned long)pgd, get_order(stage2_pgd_size));
}

void kvm_riscv_stage2_update_hgatp(struct kvm_vcpu *vcpu)
{
	unsigned long hgatp = stage2_mode;
	struct kvm_arch *k = &vcpu->kvm->arch;

	hgatp |= (READ_ONCE(k->vmid.vmid) << HGATP_VMID_SHIFT) &
		 HGATP_VMID_MASK;
	hgatp |= (k->pgd_phys >> PAGE_SHIFT) & HGATP_PPN;

	csr_write(CSR_HGATP, hgatp);

	if (!kvm_riscv_stage2_vmid_bits())
		__kvm_riscv_hfence_gvma_all();
}

void kvm_riscv_stage2_mode_detect(void)
{
#ifdef CONFIG_64BIT
	/* Try Sv48x4 stage2 mode */
	csr_write(CSR_HGATP, HGATP_MODE_SV48X4 << HGATP_MODE_SHIFT);
	if ((csr_read(CSR_HGATP) >> HGATP_MODE_SHIFT) == HGATP_MODE_SV48X4) {
		stage2_mode = (HGATP_MODE_SV48X4 << HGATP_MODE_SHIFT);
		stage2_pgd_levels = 4;
	}
	csr_write(CSR_HGATP, 0);

	__kvm_riscv_hfence_gvma_all();
#endif
}

unsigned long kvm_riscv_stage2_mode(void)
{
	return stage2_mode >> HGATP_MODE_SHIFT;
}
