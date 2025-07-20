// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 - Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */

#include <linux/init.h>
#include <linux/interval_tree_generic.h>
#include <linux/kmemleak.h>
#include <linux/kvm_host.h>
#include <asm/kvm_mmu.h>
#include <linux/memblock.h>
#include <linux/mutex.h>

#include <asm/kvm_pkvm.h>

#include "hyp_constants.h"

DEFINE_STATIC_KEY_FALSE(kvm_protected_mode_initialized);

static struct memblock_region *hyp_memory = kvm_nvhe_sym(hyp_memory);
static unsigned int *hyp_memblock_nr_ptr = &kvm_nvhe_sym(hyp_memblock_nr);

phys_addr_t hyp_mem_base;
phys_addr_t hyp_mem_size;

static int __init register_memblock_regions(void)
{
	struct memblock_region *reg;

	for_each_mem_region(reg) {
		if (*hyp_memblock_nr_ptr >= HYP_MEMBLOCK_REGIONS)
			return -ENOMEM;

		hyp_memory[*hyp_memblock_nr_ptr] = *reg;
		(*hyp_memblock_nr_ptr)++;
	}

	return 0;
}

void __init kvm_hyp_reserve(void)
{
	u64 hyp_mem_pages = 0;
	int ret;

	if (!is_hyp_mode_available() || is_kernel_in_hyp_mode())
		return;

	if (kvm_get_mode() != KVM_MODE_PROTECTED)
		return;

	ret = register_memblock_regions();
	if (ret) {
		*hyp_memblock_nr_ptr = 0;
		kvm_err("Failed to register hyp memblocks: %d\n", ret);
		return;
	}

	hyp_mem_pages += hyp_s1_pgtable_pages();
	hyp_mem_pages += host_s2_pgtable_pages();
	hyp_mem_pages += hyp_vm_table_pages();
	hyp_mem_pages += hyp_vmemmap_pages(STRUCT_HYP_PAGE_SIZE);
	hyp_mem_pages += pkvm_selftest_pages();
	hyp_mem_pages += hyp_ffa_proxy_pages();

	/*
	 * Try to allocate a PMD-aligned region to reduce TLB pressure once
	 * this is unmapped from the host stage-2, and fallback to PAGE_SIZE.
	 */
	hyp_mem_size = hyp_mem_pages << PAGE_SHIFT;
	hyp_mem_base = memblock_phys_alloc(ALIGN(hyp_mem_size, PMD_SIZE),
					   PMD_SIZE);
	if (!hyp_mem_base)
		hyp_mem_base = memblock_phys_alloc(hyp_mem_size, PAGE_SIZE);
	else
		hyp_mem_size = ALIGN(hyp_mem_size, PMD_SIZE);

	if (!hyp_mem_base) {
		kvm_err("Failed to reserve hyp memory\n");
		return;
	}

	kvm_info("Reserved %lld MiB at 0x%llx\n", hyp_mem_size >> 20,
		 hyp_mem_base);
}

static void __pkvm_destroy_hyp_vm(struct kvm *host_kvm)
{
	if (host_kvm->arch.pkvm.handle) {
		WARN_ON(kvm_call_hyp_nvhe(__pkvm_teardown_vm,
					  host_kvm->arch.pkvm.handle));
	}

	host_kvm->arch.pkvm.handle = 0;
	free_hyp_memcache(&host_kvm->arch.pkvm.teardown_mc);
	free_hyp_memcache(&host_kvm->arch.pkvm.stage2_teardown_mc);
}

static int __pkvm_create_hyp_vcpu(struct kvm_vcpu *vcpu)
{
	size_t hyp_vcpu_sz = PAGE_ALIGN(PKVM_HYP_VCPU_SIZE);
	pkvm_handle_t handle = vcpu->kvm->arch.pkvm.handle;
	void *hyp_vcpu;
	int ret;

	vcpu->arch.pkvm_memcache.flags |= HYP_MEMCACHE_ACCOUNT_STAGE2;

	hyp_vcpu = alloc_pages_exact(hyp_vcpu_sz, GFP_KERNEL_ACCOUNT);
	if (!hyp_vcpu)
		return -ENOMEM;

	ret = kvm_call_hyp_nvhe(__pkvm_init_vcpu, handle, vcpu, hyp_vcpu);
	if (!ret)
		vcpu_set_flag(vcpu, VCPU_PKVM_FINALIZED);
	else
		free_pages_exact(hyp_vcpu, hyp_vcpu_sz);

	return ret;
}

/*
 * Allocates and donates memory for hypervisor VM structs at EL2.
 *
 * Allocates space for the VM state, which includes the hyp vm as well as
 * the hyp vcpus.
 *
 * Stores an opaque handler in the kvm struct for future reference.
 *
 * Return 0 on success, negative error code on failure.
 */
static int __pkvm_create_hyp_vm(struct kvm *host_kvm)
{
	size_t pgd_sz, hyp_vm_sz;
	void *pgd, *hyp_vm;
	int ret;

	if (host_kvm->created_vcpus < 1)
		return -EINVAL;

	pgd_sz = kvm_pgtable_stage2_pgd_size(host_kvm->arch.mmu.vtcr);

	/*
	 * The PGD pages will be reclaimed using a hyp_memcache which implies
	 * page granularity. So, use alloc_pages_exact() to get individual
	 * refcounts.
	 */
	pgd = alloc_pages_exact(pgd_sz, GFP_KERNEL_ACCOUNT);
	if (!pgd)
		return -ENOMEM;

	/* Allocate memory to donate to hyp for vm and vcpu pointers. */
	hyp_vm_sz = PAGE_ALIGN(size_add(PKVM_HYP_VM_SIZE,
					size_mul(sizeof(void *),
						 host_kvm->created_vcpus)));
	hyp_vm = alloc_pages_exact(hyp_vm_sz, GFP_KERNEL_ACCOUNT);
	if (!hyp_vm) {
		ret = -ENOMEM;
		goto free_pgd;
	}

	/* Donate the VM memory to hyp and let hyp initialize it. */
	ret = kvm_call_hyp_nvhe(__pkvm_init_vm, host_kvm, hyp_vm, pgd);
	if (ret < 0)
		goto free_vm;

	host_kvm->arch.pkvm.handle = ret;
	host_kvm->arch.pkvm.stage2_teardown_mc.flags |= HYP_MEMCACHE_ACCOUNT_STAGE2;
	kvm_account_pgtable_pages(pgd, pgd_sz / PAGE_SIZE);

	return 0;
free_vm:
	free_pages_exact(hyp_vm, hyp_vm_sz);
free_pgd:
	free_pages_exact(pgd, pgd_sz);
	return ret;
}

int pkvm_create_hyp_vm(struct kvm *host_kvm)
{
	int ret = 0;

	mutex_lock(&host_kvm->arch.config_lock);
	if (!host_kvm->arch.pkvm.handle)
		ret = __pkvm_create_hyp_vm(host_kvm);
	mutex_unlock(&host_kvm->arch.config_lock);

	return ret;
}

int pkvm_create_hyp_vcpu(struct kvm_vcpu *vcpu)
{
	int ret = 0;

	mutex_lock(&vcpu->kvm->arch.config_lock);
	if (!vcpu_get_flag(vcpu, VCPU_PKVM_FINALIZED))
		ret = __pkvm_create_hyp_vcpu(vcpu);
	mutex_unlock(&vcpu->kvm->arch.config_lock);

	return ret;
}

void pkvm_destroy_hyp_vm(struct kvm *host_kvm)
{
	mutex_lock(&host_kvm->arch.config_lock);
	__pkvm_destroy_hyp_vm(host_kvm);
	mutex_unlock(&host_kvm->arch.config_lock);
}

int pkvm_init_host_vm(struct kvm *host_kvm)
{
	return 0;
}

static void __init _kvm_host_prot_finalize(void *arg)
{
	int *err = arg;

	if (WARN_ON(kvm_call_hyp_nvhe(__pkvm_prot_finalize)))
		WRITE_ONCE(*err, -EINVAL);
}

static int __init pkvm_drop_host_privileges(void)
{
	int ret = 0;

	/*
	 * Flip the static key upfront as that may no longer be possible
	 * once the host stage 2 is installed.
	 */
	static_branch_enable(&kvm_protected_mode_initialized);
	on_each_cpu(_kvm_host_prot_finalize, &ret, 1);
	return ret;
}

static int __init finalize_pkvm(void)
{
	int ret;

	if (!is_protected_kvm_enabled() || !is_kvm_arm_initialised())
		return 0;

	/*
	 * Exclude HYP sections from kmemleak so that they don't get peeked
	 * at, which would end badly once inaccessible.
	 */
	kmemleak_free_part(__hyp_bss_start, __hyp_bss_end - __hyp_bss_start);
	kmemleak_free_part(__hyp_data_start, __hyp_data_end - __hyp_data_start);
	kmemleak_free_part(__hyp_rodata_start, __hyp_rodata_end - __hyp_rodata_start);
	kmemleak_free_part_phys(hyp_mem_base, hyp_mem_size);

	ret = pkvm_drop_host_privileges();
	if (ret)
		pr_err("Failed to finalize Hyp protection: %d\n", ret);

	return ret;
}
device_initcall_sync(finalize_pkvm);

static u64 __pkvm_mapping_start(struct pkvm_mapping *m)
{
	return m->gfn * PAGE_SIZE;
}

static u64 __pkvm_mapping_end(struct pkvm_mapping *m)
{
	return (m->gfn + m->nr_pages) * PAGE_SIZE - 1;
}

INTERVAL_TREE_DEFINE(struct pkvm_mapping, node, u64, __subtree_last,
		     __pkvm_mapping_start, __pkvm_mapping_end, static,
		     pkvm_mapping);

/*
 * __tmp is updated to iter_first(pkvm_mappings) *before* entering the body of the loop to allow
 * freeing of __map inline.
 */
#define for_each_mapping_in_range_safe(__pgt, __start, __end, __map)				\
	for (struct pkvm_mapping *__tmp = pkvm_mapping_iter_first(&(__pgt)->pkvm_mappings,	\
								  __start, __end - 1);		\
	     __tmp && ({									\
				__map = __tmp;							\
				__tmp = pkvm_mapping_iter_next(__map, __start, __end - 1);	\
				true;								\
		       });									\
	    )

int pkvm_pgtable_stage2_init(struct kvm_pgtable *pgt, struct kvm_s2_mmu *mmu,
			     struct kvm_pgtable_mm_ops *mm_ops)
{
	pgt->pkvm_mappings	= RB_ROOT_CACHED;
	pgt->mmu		= mmu;

	return 0;
}

static int __pkvm_pgtable_stage2_unmap(struct kvm_pgtable *pgt, u64 start, u64 end)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(pgt->mmu);
	pkvm_handle_t handle = kvm->arch.pkvm.handle;
	struct pkvm_mapping *mapping;
	int ret;

	if (!handle)
		return 0;

	for_each_mapping_in_range_safe(pgt, start, end, mapping) {
		ret = kvm_call_hyp_nvhe(__pkvm_host_unshare_guest, handle, mapping->gfn,
					mapping->nr_pages);
		if (WARN_ON(ret))
			return ret;
		pkvm_mapping_remove(mapping, &pgt->pkvm_mappings);
		kfree(mapping);
	}

	return 0;
}

void pkvm_pgtable_stage2_destroy(struct kvm_pgtable *pgt)
{
	__pkvm_pgtable_stage2_unmap(pgt, 0, ~(0ULL));
}

int pkvm_pgtable_stage2_map(struct kvm_pgtable *pgt, u64 addr, u64 size,
			   u64 phys, enum kvm_pgtable_prot prot,
			   void *mc, enum kvm_pgtable_walk_flags flags)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(pgt->mmu);
	struct pkvm_mapping *mapping = NULL;
	struct kvm_hyp_memcache *cache = mc;
	u64 gfn = addr >> PAGE_SHIFT;
	u64 pfn = phys >> PAGE_SHIFT;
	int ret;

	if (size != PAGE_SIZE && size != PMD_SIZE)
		return -EINVAL;

	lockdep_assert_held_write(&kvm->mmu_lock);

	/*
	 * Calling stage2_map() on top of existing mappings is either happening because of a race
	 * with another vCPU, or because we're changing between page and block mappings. As per
	 * user_mem_abort(), same-size permission faults are handled in the relax_perms() path.
	 */
	mapping = pkvm_mapping_iter_first(&pgt->pkvm_mappings, addr, addr + size - 1);
	if (mapping) {
		if (size == (mapping->nr_pages * PAGE_SIZE))
			return -EAGAIN;

		/* Remove _any_ pkvm_mapping overlapping with the range, bigger or smaller. */
		ret = __pkvm_pgtable_stage2_unmap(pgt, addr, addr + size);
		if (ret)
			return ret;
		mapping = NULL;
	}

	ret = kvm_call_hyp_nvhe(__pkvm_host_share_guest, pfn, gfn, size / PAGE_SIZE, prot);
	if (WARN_ON(ret))
		return ret;

	swap(mapping, cache->mapping);
	mapping->gfn = gfn;
	mapping->pfn = pfn;
	mapping->nr_pages = size / PAGE_SIZE;
	pkvm_mapping_insert(mapping, &pgt->pkvm_mappings);

	return ret;
}

int pkvm_pgtable_stage2_unmap(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	lockdep_assert_held_write(&kvm_s2_mmu_to_kvm(pgt->mmu)->mmu_lock);

	return __pkvm_pgtable_stage2_unmap(pgt, addr, addr + size);
}

int pkvm_pgtable_stage2_wrprotect(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(pgt->mmu);
	pkvm_handle_t handle = kvm->arch.pkvm.handle;
	struct pkvm_mapping *mapping;
	int ret = 0;

	lockdep_assert_held(&kvm->mmu_lock);
	for_each_mapping_in_range_safe(pgt, addr, addr + size, mapping) {
		ret = kvm_call_hyp_nvhe(__pkvm_host_wrprotect_guest, handle, mapping->gfn,
					mapping->nr_pages);
		if (WARN_ON(ret))
			break;
	}

	return ret;
}

int pkvm_pgtable_stage2_flush(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(pgt->mmu);
	struct pkvm_mapping *mapping;

	lockdep_assert_held(&kvm->mmu_lock);
	for_each_mapping_in_range_safe(pgt, addr, addr + size, mapping)
		__clean_dcache_guest_page(pfn_to_kaddr(mapping->pfn),
					  PAGE_SIZE * mapping->nr_pages);

	return 0;
}

bool pkvm_pgtable_stage2_test_clear_young(struct kvm_pgtable *pgt, u64 addr, u64 size, bool mkold)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(pgt->mmu);
	pkvm_handle_t handle = kvm->arch.pkvm.handle;
	struct pkvm_mapping *mapping;
	bool young = false;

	lockdep_assert_held(&kvm->mmu_lock);
	for_each_mapping_in_range_safe(pgt, addr, addr + size, mapping)
		young |= kvm_call_hyp_nvhe(__pkvm_host_test_clear_young_guest, handle, mapping->gfn,
					   mapping->nr_pages, mkold);

	return young;
}

int pkvm_pgtable_stage2_relax_perms(struct kvm_pgtable *pgt, u64 addr, enum kvm_pgtable_prot prot,
				    enum kvm_pgtable_walk_flags flags)
{
	return kvm_call_hyp_nvhe(__pkvm_host_relax_perms_guest, addr >> PAGE_SHIFT, prot);
}

void pkvm_pgtable_stage2_mkyoung(struct kvm_pgtable *pgt, u64 addr,
				 enum kvm_pgtable_walk_flags flags)
{
	WARN_ON(kvm_call_hyp_nvhe(__pkvm_host_mkyoung_guest, addr >> PAGE_SHIFT));
}

void pkvm_pgtable_stage2_free_unlinked(struct kvm_pgtable_mm_ops *mm_ops, void *pgtable, s8 level)
{
	WARN_ON_ONCE(1);
}

kvm_pte_t *pkvm_pgtable_stage2_create_unlinked(struct kvm_pgtable *pgt, u64 phys, s8 level,
					enum kvm_pgtable_prot prot, void *mc, bool force_pte)
{
	WARN_ON_ONCE(1);
	return NULL;
}

int pkvm_pgtable_stage2_split(struct kvm_pgtable *pgt, u64 addr, u64 size,
			      struct kvm_mmu_memory_cache *mc)
{
	WARN_ON_ONCE(1);
	return -EINVAL;
}
