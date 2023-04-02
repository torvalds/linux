// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */

#include <linux/kvm_host.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pgtable.h>
#include <asm/kvm_pkvm.h>

#include <nvhe/early_alloc.h>
#include <nvhe/ffa.h>
#include <nvhe/gfp.h>
#include <nvhe/iommu.h>
#include <nvhe/memory.h>
#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>
#include <nvhe/pkvm.h>
#include <nvhe/serial.h>
#include <nvhe/trap_handler.h>

unsigned long hyp_nr_cpus;

phys_addr_t pvmfw_base;
phys_addr_t pvmfw_size;

#define hyp_percpu_size ((unsigned long)__per_cpu_end - \
			 (unsigned long)__per_cpu_start)

static void *vmemmap_base;
static void *vm_table_base;
static void *hyp_pgt_base;
static void *host_s2_pgt_base;
static void *ffa_proxy_pages;
static void *hyp_host_fp_base;
static struct kvm_pgtable_mm_ops pkvm_pgtable_mm_ops;
static struct hyp_pool hpool;

static int divide_memory_pool(void *virt, unsigned long size)
{
	unsigned long nr_pages;

	hyp_early_alloc_init(virt, size);

	nr_pages = hyp_vmemmap_pages(sizeof(struct hyp_page));
	vmemmap_base = hyp_early_alloc_contig(nr_pages);
	if (!vmemmap_base)
		return -ENOMEM;

	nr_pages = hyp_vm_table_pages();
	vm_table_base = hyp_early_alloc_contig(nr_pages);
	if (!vm_table_base)
		return -ENOMEM;

	nr_pages = hyp_s1_pgtable_pages();
	hyp_pgt_base = hyp_early_alloc_contig(nr_pages);
	if (!hyp_pgt_base)
		return -ENOMEM;

	nr_pages = host_s2_pgtable_pages();
	host_s2_pgt_base = hyp_early_alloc_contig(nr_pages);
	if (!host_s2_pgt_base)
		return -ENOMEM;

	nr_pages = hyp_ffa_proxy_pages();
	ffa_proxy_pages = hyp_early_alloc_contig(nr_pages);
	if (!ffa_proxy_pages)
		return -ENOMEM;

	nr_pages = hyp_host_fp_pages(hyp_nr_cpus);
	hyp_host_fp_base = hyp_early_alloc_contig(nr_pages);
	if (!hyp_host_fp_base)
		return -ENOMEM;

	return 0;
}

static int recreate_hyp_mappings(phys_addr_t phys, unsigned long size,
				 unsigned long *per_cpu_base,
				 u32 hyp_va_bits)
{
	void *start, *end, *virt = hyp_phys_to_virt(phys);
	unsigned long pgt_size = hyp_s1_pgtable_pages() << PAGE_SHIFT;
	enum kvm_pgtable_prot prot;
	int ret, i;

	/* Recreate the hyp page-table using the early page allocator */
	hyp_early_alloc_init(hyp_pgt_base, pgt_size);
	ret = kvm_pgtable_hyp_init(&pkvm_pgtable, hyp_va_bits,
				   &hyp_early_alloc_mm_ops);
	if (ret)
		return ret;

	ret = hyp_create_idmap(hyp_va_bits);
	if (ret)
		return ret;

	ret = hyp_map_vectors();
	if (ret)
		return ret;

	ret = hyp_back_vmemmap(hyp_virt_to_phys(vmemmap_base));
	if (ret)
		return ret;

	ret = pkvm_create_mappings(__hyp_text_start, __hyp_text_end, PAGE_HYP_EXEC);
	if (ret)
		return ret;

	ret = pkvm_create_mappings(__hyp_data_start, __hyp_data_end, PAGE_HYP);
	if (ret)
		return ret;

	ret = pkvm_create_mappings(__hyp_rodata_start, __hyp_rodata_end, PAGE_HYP_RO);
	if (ret)
		return ret;

	ret = pkvm_create_mappings(__hyp_bss_start, __hyp_bss_end, PAGE_HYP);
	if (ret)
		return ret;

	ret = pkvm_create_mappings(virt, virt + size, PAGE_HYP);
	if (ret)
		return ret;

	for (i = 0; i < hyp_nr_cpus; i++) {
		struct kvm_nvhe_init_params *params = per_cpu_ptr(&kvm_init_params, i);
		unsigned long hyp_addr;

		start = (void *)kern_hyp_va(per_cpu_base[i]);
		end = start + PAGE_ALIGN(hyp_percpu_size);
		ret = pkvm_create_mappings(start, end, PAGE_HYP);
		if (ret)
			return ret;

		/*
		 * Allocate a contiguous HYP private VA range for the stack
		 * and guard page. The allocation is also aligned based on
		 * the order of its size.
		 */
		ret = pkvm_alloc_private_va_range(PAGE_SIZE * 2, &hyp_addr);
		if (ret)
			return ret;

		/*
		 * Since the stack grows downwards, map the stack to the page
		 * at the higher address and leave the lower guard page
		 * unbacked.
		 *
		 * Any valid stack address now has the PAGE_SHIFT bit as 1
		 * and addresses corresponding to the guard page have the
		 * PAGE_SHIFT bit as 0 - this is used for overflow detection.
		 */
		hyp_spin_lock(&pkvm_pgd_lock);
		ret = kvm_pgtable_hyp_map(&pkvm_pgtable, hyp_addr + PAGE_SIZE,
					PAGE_SIZE, params->stack_pa, PAGE_HYP);
		hyp_spin_unlock(&pkvm_pgd_lock);
		if (ret)
			return ret;

		/* Update stack_hyp_va to end of the stack's private VA range */
		params->stack_hyp_va = hyp_addr + (2 * PAGE_SIZE);
	}

	/*
	 * Map the host sections RO in the hypervisor, but transfer the
	 * ownership from the host to the hypervisor itself to make sure they
	 * can't be donated or shared with another entity.
	 *
	 * The ownership transition requires matching changes in the host
	 * stage-2. This will be done later (see finalize_host_mappings()) once
	 * the hyp_vmemmap is addressable.
	 */
	prot = pkvm_mkstate(PAGE_HYP_RO, PKVM_PAGE_SHARED_OWNED);
	ret = pkvm_create_mappings(&kvm_vgic_global_state,
				   &kvm_vgic_global_state + 1, prot);
	if (ret)
		return ret;

	start = hyp_phys_to_virt(pvmfw_base);
	end = start + pvmfw_size;
	prot = pkvm_mkstate(PAGE_HYP_RO, PKVM_PAGE_OWNED);
	ret = pkvm_create_mappings(start, end, prot);
	if (ret)
		return ret;

	return 0;
}

static void update_nvhe_init_params(void)
{
	struct kvm_nvhe_init_params *params;
	unsigned long i;

	for (i = 0; i < hyp_nr_cpus; i++) {
		params = per_cpu_ptr(&kvm_init_params, i);
		params->pgd_pa = __hyp_pa(pkvm_pgtable.pgd);
		dcache_clean_inval_poc((unsigned long)params,
				    (unsigned long)params + sizeof(*params));
	}
}

static void *hyp_zalloc_hyp_page(void *arg)
{
	return hyp_alloc_pages(&hpool, 0);
}

static void hpool_get_page(void *addr)
{
	hyp_get_page(&hpool, addr);
}

static void hpool_put_page(void *addr)
{
	hyp_put_page(&hpool, addr);
}

static int fix_host_ownership_walker(u64 addr, u64 end, u32 level,
				     kvm_pte_t *ptep,
				     enum kvm_pgtable_walk_flags flag,
				     void * const arg)
{
	enum kvm_pgtable_prot prot;
	enum pkvm_page_state state;
	kvm_pte_t pte = *ptep;
	phys_addr_t phys;

	if (!kvm_pte_valid(pte))
		return 0;

	if (level != (KVM_PGTABLE_MAX_LEVELS - 1))
		return -EINVAL;

	phys = kvm_pte_to_phys(pte);
	if (!addr_is_memory(phys))
		return -EINVAL;

	/*
	 * Adjust the host stage-2 mappings to match the ownership attributes
	 * configured in the hypervisor stage-1.
	 */
	state = pkvm_getstate(kvm_pgtable_hyp_pte_prot(pte));
	switch (state) {
	case PKVM_PAGE_OWNED:
		return host_stage2_set_owner_locked(phys, PAGE_SIZE, PKVM_ID_HYP);
	case PKVM_PAGE_SHARED_OWNED:
		prot = pkvm_mkstate(PKVM_HOST_MEM_PROT, PKVM_PAGE_SHARED_BORROWED);
		break;
	case PKVM_PAGE_SHARED_BORROWED:
		prot = pkvm_mkstate(PKVM_HOST_MEM_PROT, PKVM_PAGE_SHARED_OWNED);
		break;
	default:
		return -EINVAL;
	}

	return host_stage2_idmap_locked(phys, PAGE_SIZE, prot, false);
}

static int fix_hyp_pgtable_refcnt_walker(u64 addr, u64 end, u32 level,
					 kvm_pte_t *ptep,
					 enum kvm_pgtable_walk_flags flag,
					 void * const arg)
{
	struct kvm_pgtable_mm_ops *mm_ops = arg;
	kvm_pte_t pte = *ptep;

	/*
	 * Fix-up the refcount for the page-table pages as the early allocator
	 * was unable to access the hyp_vmemmap and so the buddy allocator has
	 * initialised the refcount to '1'.
	 */
	if (kvm_pte_valid(pte))
		mm_ops->get_page(ptep);

	return 0;
}

static int fix_host_ownership(void)
{
	struct kvm_pgtable_walker walker = {
		.cb	= fix_host_ownership_walker,
		.flags	= KVM_PGTABLE_WALK_LEAF,
	};
	int i, ret;

	for (i = 0; i < hyp_memblock_nr; i++) {
		struct memblock_region *reg = &hyp_memory[i];
		u64 start = (u64)hyp_phys_to_virt(reg->base);

		ret = kvm_pgtable_walk(&pkvm_pgtable, start, reg->size, &walker);
		if (ret)
			return ret;
	}

	return 0;
}

static int fix_hyp_pgtable_refcnt(void)
{
	struct kvm_pgtable_walker walker = {
		.cb	= fix_hyp_pgtable_refcnt_walker,
		.flags	= KVM_PGTABLE_WALK_LEAF | KVM_PGTABLE_WALK_TABLE_POST,
		.arg	= pkvm_pgtable.mm_ops,
	};

	return kvm_pgtable_walk(&pkvm_pgtable, 0, BIT(pkvm_pgtable.ia_bits),
				&walker);
}

static int unmap_protected_regions(void)
{
	struct pkvm_moveable_reg *reg;
	int i, ret;

	for (i = 0; i < pkvm_moveable_regs_nr; i++) {
		reg = &pkvm_moveable_regs[i];
		if (reg->type != PKVM_MREG_PROTECTED_RANGE)
			continue;
		ret = host_stage2_protect_pages_locked(reg->start, reg->size);
		if (ret)
			return ret;
	}

	return 0;
}

void __noreturn __pkvm_init_finalise(void)
{
	struct kvm_host_data *host_data = this_cpu_ptr(&kvm_host_data);
	struct kvm_cpu_context *host_ctxt = &host_data->host_ctxt;
	unsigned long nr_pages, reserved_pages, pfn;
	int ret;

	/* Now that the vmemmap is backed, install the full-fledged allocator */
	pfn = hyp_virt_to_pfn(hyp_pgt_base);
	nr_pages = hyp_s1_pgtable_pages();
	reserved_pages = hyp_early_alloc_nr_used_pages();
	ret = hyp_pool_init(&hpool, pfn, nr_pages, reserved_pages);
	if (ret)
		goto out;

	ret = kvm_host_prepare_stage2(host_s2_pgt_base);
	if (ret)
		goto out;

	pkvm_pgtable_mm_ops = (struct kvm_pgtable_mm_ops) {
		.zalloc_page = hyp_zalloc_hyp_page,
		.phys_to_virt = hyp_phys_to_virt,
		.virt_to_phys = hyp_virt_to_phys,
		.get_page = hpool_get_page,
		.put_page = hpool_put_page,
		.page_count = hyp_page_count,
	};
	pkvm_pgtable.mm_ops = &pkvm_pgtable_mm_ops;

	ret = fix_host_ownership();
	if (ret)
		goto out;

	ret = fix_hyp_pgtable_refcnt();
	if (ret)
		goto out;

	ret = hyp_create_pcpu_fixmap();
	if (ret)
		goto out;

	ret = unmap_protected_regions();
	if (ret)
		goto out;

	ret = hyp_ffa_init(ffa_proxy_pages);
	if (ret)
		goto out;

	pkvm_hyp_vm_table_init(vm_table_base);
	pkvm_hyp_host_fp_init(hyp_host_fp_base);
out:
	/*
	 * We tail-called to here from handle___pkvm_init() and will not return,
	 * so make sure to propagate the return value to the host.
	 */
	cpu_reg(host_ctxt, 1) = ret;

	__host_enter(host_ctxt);
}

int __pkvm_init(phys_addr_t phys, unsigned long size, unsigned long nr_cpus,
		unsigned long *per_cpu_base, u32 hyp_va_bits)
{
	struct kvm_nvhe_init_params *params;
	void *virt = hyp_phys_to_virt(phys);
	void (*fn)(phys_addr_t params_pa, void *finalize_fn_va);
	int ret;

	BUG_ON(kvm_check_pvm_sysreg_table());

	if (!PAGE_ALIGNED(phys) || !PAGE_ALIGNED(size))
		return -EINVAL;

	hyp_spin_lock_init(&pkvm_pgd_lock);
	hyp_nr_cpus = nr_cpus;

	ret = divide_memory_pool(virt, size);
	if (ret)
		return ret;

	ret = recreate_hyp_mappings(phys, size, per_cpu_base, hyp_va_bits);
	if (ret)
		return ret;

	update_nvhe_init_params();

	/* Jump in the idmap page to switch to the new page-tables */
	params = this_cpu_ptr(&kvm_init_params);
	fn = (typeof(fn))__hyp_pa(__pkvm_init_switch_pgd);
	fn(__hyp_pa(params), __pkvm_init_finalise);

	unreachable();
}
