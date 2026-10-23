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

#define CREATE_TRACE_POINTS
#include "trace_pkvm.h"

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

static int pkvm_hyp_topup(enum pkvm_topup_id id, unsigned long nr_pages)
{
	struct kvm_hyp_memcache mc;
	struct arm_smccc_res res;
	int ret;

	init_hyp_memcache(&mc);
	ret = topup_hyp_memcache(&mc, nr_pages);
	if (ret)
		goto err;

	arm_smccc_1_1_hvc(KVM_HOST_SMCCC_FUNC(__pkvm_hyp_topup), id, mc.head,
			  mc.nr_pages, &res);
	if (WARN_ON_ONCE(res.a0 != SMCCC_RET_SUCCESS)) {
		ret = -EINVAL;
		goto err;
	}

	ret = res.a1;
	mc.head = res.a2;
	mc.nr_pages = res.a3;

err:
	free_hyp_memcache(&mc);
	return ret;
}

static unsigned long __pkvm_hyp_reclaim(enum pkvm_topup_id id, unsigned long target)
{
	struct kvm_hyp_memcache mc;
	struct arm_smccc_res res;
	unsigned long reclaimed;

	arm_smccc_1_1_hvc(KVM_HOST_SMCCC_FUNC(__pkvm_hyp_reclaim), id, target, &res);
	if (WARN_ON_ONCE(res.a0 != SMCCC_RET_SUCCESS) || WARN_ON_ONCE(res.a1))
		return 0;

	init_hyp_memcache(&mc);
	mc.head = res.a2;
	mc.nr_pages = reclaimed = res.a3;
	free_hyp_memcache(&mc);

	return reclaimed;
}

static unsigned long pkvm_hyp_reclaim(enum pkvm_topup_id id, unsigned long target)
{
	unsigned long reclaimed = 0;

	while (reclaimed < target) {
		/* Arbitrary limit to avoid blocking in EL2 for too long */
		unsigned long r = __pkvm_hyp_reclaim(id, min(target - reclaimed, 16));

		if (!r)
			break;

		reclaimed += r;
		if (reclaimed >= target)
			break;

		cond_resched();
	}

	return reclaimed;
}

static unsigned long pkvm_hyp_reclaimable(enum pkvm_topup_id id)
{
	return kvm_call_hyp_nvhe(__pkvm_hyp_reclaimable, id);
}

static void __pkvm_destroy_hyp_vm(struct kvm *kvm)
{
	if (pkvm_hyp_vm_is_created(kvm)) {
		WARN_ON(kvm_call_hyp_nvhe(__pkvm_finalize_teardown_vm,
					  kvm->arch.pkvm.handle));
	} else if (kvm->arch.pkvm.handle) {
		/*
		 * The VM could have been reserved but hyp initialization has
		 * failed. Make sure to unreserve it.
		 */
		kvm_call_hyp_nvhe(__pkvm_unreserve_vm, kvm->arch.pkvm.handle);
	}

	kvm->arch.pkvm.handle = 0;
	kvm->arch.pkvm.is_created = false;
	free_hyp_memcache(&kvm->arch.pkvm.stage2_teardown_mc);
}

static int __pkvm_create_hyp_vcpu(struct kvm_vcpu *vcpu)
{
	pkvm_handle_t handle = vcpu->kvm->arch.pkvm.handle;
	int ret;

	init_hyp_stage2_memcache(&vcpu->arch.stage2_mc);

	ret = pkvm_call_hyp_req(__pkvm_init_vcpu, handle, vcpu);
	if (ret)
		return ret;

	/*
	 * Mirror EL2's seeding of power_state from mp_state. The hyp vCPU is
	 * published, so take mp_state_lock against kvm_psci_vcpu_on().
	 */
	if (vcpu_is_protected(vcpu)) {
		spin_lock(&vcpu->arch.mp_state_lock);
		if (kvm_arm_vcpu_stopped(vcpu))
			WRITE_ONCE(vcpu->arch.mp_state.mp_state, KVM_MP_STATE_UNINITIALIZED);
		spin_unlock(&vcpu->arch.mp_state_lock);
	}

	vcpu_set_flag(vcpu, VCPU_PKVM_FINALIZED);

	return 0;
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
static int __pkvm_create_hyp_vm(struct kvm *kvm)
{
	size_t pgd_sz;
	void *pgd;
	int ret;

	if (kvm->created_vcpus < 1)
		return -EINVAL;

	pgd_sz = kvm_pgtable_stage2_pgd_size(kvm->arch.mmu.vtcr);

	/*
	 * The PGD pages will be reclaimed using a hyp_memcache which implies
	 * page granularity. So, use alloc_pages_exact() to get individual
	 * refcounts.
	 */
	pgd = alloc_pages_exact(pgd_sz, GFP_KERNEL_ACCOUNT);
	if (!pgd)
		return -ENOMEM;

	ret = pkvm_call_hyp_req(__pkvm_init_vm, kvm, pgd);
	if (ret)
		goto free_pgd;

	kvm->arch.pkvm.is_created = true;
	init_hyp_stage2_memcache(&kvm->arch.pkvm.stage2_teardown_mc);
	kvm_account_pgtable_pages(pgd, pgd_sz / PAGE_SIZE);

	return 0;
free_pgd:
	free_pages_exact(pgd, pgd_sz);
	return ret;
}

bool pkvm_hyp_vm_is_created(struct kvm *kvm)
{
	/*
	 * Serialised by config_lock/slots_lock, or by VM lifecycle at
	 * teardown, so a plain read suffices.
	 */
	return kvm->arch.pkvm.is_created;
}

int pkvm_create_hyp_vm(struct kvm *kvm)
{
	int ret = 0;

	/*
	 * Synchronise with kvm_arch_prepare_memory_region(), as we
	 * prevent memslot modifications on a pVM that has been run.
	 */
	mutex_lock(&kvm->slots_lock);
	mutex_lock(&kvm->arch.config_lock);
	if (!pkvm_hyp_vm_is_created(kvm))
		ret = __pkvm_create_hyp_vm(kvm);
	mutex_unlock(&kvm->arch.config_lock);
	mutex_unlock(&kvm->slots_lock);

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

void pkvm_destroy_hyp_vm(struct kvm *kvm)
{
	mutex_lock(&kvm->arch.config_lock);
	__pkvm_destroy_hyp_vm(kvm);
	mutex_unlock(&kvm->arch.config_lock);
}

int pkvm_init_host_vm(struct kvm *kvm)
{
	int ret;

	/* Reserve the VM in hyp and obtain a hyp handle for the VM. */
	ret = kvm_call_hyp_nvhe(__pkvm_reserve_vm);
	if (ret < 0)
		return ret;

	kvm->arch.pkvm.handle = ret;
	if (kvm_vm_is_protected(kvm)) {
		pr_warn_once("kvm: protected VMs are experimental and for development only, tainting kernel\n");
		add_taint(TAINT_USER, LOCKDEP_STILL_OK);
	}

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

void __init pkvm_selftests(void)
{
#ifdef CONFIG_NVHE_EL2_DEBUG
	int ret = pkvm_call_hyp_req(__pkvm_hyp_alloc_selftest);
	unsigned long reclaimed;

	reclaimed = pkvm_hyp_reclaim(PKVM_TOPUP_HYP_ALLOC_SELFTEST, ULONG_MAX);

	/* On failure, not all the pages may be reclaimable */
	if (!ret)
		WARN_ON(reclaimed != 6 /* SELFTEST_MAX_PAGES */);
	else
		kvm_err("pKVM hyp allocator selftest failed (%d)\n", ret);
#endif
}

static unsigned long pkvm_shrinker_count(struct shrinker *shrink, struct shrink_control *sc)
{
	unsigned long reclaimable = 0;
	int id;

	for (id = 0; id < NR_PKVM_TOPUP_HYP_IDS; id++)
		reclaimable += pkvm_hyp_reclaimable(id);

	return reclaimable ?: SHRINK_EMPTY;
}

static unsigned long pkvm_shrinker_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	unsigned long reclaimed = 0;
	int id;

	sc->nr_scanned = 0;

	for (id = 0; id < NR_PKVM_TOPUP_HYP_IDS; id++) {
		unsigned long r = pkvm_hyp_reclaim(id, sc->nr_to_scan - sc->nr_scanned);

		reclaimed += r;
		sc->nr_scanned += r;
		if (sc->nr_scanned >= sc->nr_to_scan)
			break;
	}

	return reclaimed ?: SHRINK_STOP;
}

static int __init finalize_pkvm(void)
{
	struct shrinker *pkvm_shrinker;
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
	if (ret) {
		pr_err("Failed to finalize Hyp protection: %d\n", ret);
		return ret;
	}

	pkvm_shrinker = shrinker_alloc(0, "pkvm");
	if (pkvm_shrinker) {
		pkvm_shrinker->count_objects = pkvm_shrinker_count;
		pkvm_shrinker->scan_objects = pkvm_shrinker_scan;
		shrinker_register(pkvm_shrinker);
	} else {
		kvm_err("Failed to register shrinker for pKVM\n");
	}

	return 0;
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

static int __pkvm_pgtable_stage2_reclaim(struct kvm_pgtable *pgt, u64 start, u64 end)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(pgt->mmu);
	pkvm_handle_t handle = kvm->arch.pkvm.handle;
	struct pkvm_mapping *mapping;
	int ret;

	for_each_mapping_in_range_safe(pgt, start, end, mapping) {
		struct page *page;

		ret = kvm_call_hyp_nvhe(__pkvm_reclaim_dying_guest_page,
					handle, mapping->gfn);
		if (WARN_ON(ret))
			continue;

		page = pfn_to_page(mapping->pfn);
		WARN_ON_ONCE(mapping->nr_pages != 1);
		unpin_user_pages_dirty_lock(&page, 1, true);
		account_locked_vm(kvm->mm, 1, false);
		pkvm_mapping_remove(mapping, &pgt->pkvm_mappings);
		kfree(mapping);
	}

	return 0;
}

static int __pkvm_pgtable_stage2_unshare(struct kvm_pgtable *pgt, u64 start, u64 end)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(pgt->mmu);
	pkvm_handle_t handle = kvm->arch.pkvm.handle;
	struct pkvm_mapping *mapping;
	int ret;

	for_each_mapping_in_range_safe(pgt, start, end, mapping) {
		ret = kvm_call_hyp_nvhe(__pkvm_host_unshare_guest, handle, mapping->gfn,
					(u64)mapping->nr_pages);
		if (WARN_ON(ret))
			return ret;
		pkvm_mapping_remove(mapping, &pgt->pkvm_mappings);
		kfree(mapping);
	}

	return 0;
}

void pkvm_pgtable_stage2_destroy_range(struct kvm_pgtable *pgt,
					u64 addr, u64 size)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(pgt->mmu);
	pkvm_handle_t handle = kvm->arch.pkvm.handle;

	if (!handle)
		return;

	if (pkvm_hyp_vm_is_created(kvm) && !kvm->arch.pkvm.is_dying) {
		WARN_ON(kvm_call_hyp_nvhe(__pkvm_start_teardown_vm, handle));
		kvm->arch.pkvm.is_dying = true;
	}

	if (kvm_vm_is_protected(kvm))
		__pkvm_pgtable_stage2_reclaim(pgt, addr, addr + size);
	else
		__pkvm_pgtable_stage2_unshare(pgt, addr, addr + size);
}

void pkvm_pgtable_stage2_destroy_pgd(struct kvm_pgtable *pgt)
{
	/* Expected to be called after all pKVM mappings have been released. */
	WARN_ON_ONCE(!RB_EMPTY_ROOT(&pgt->pkvm_mappings.rb_root));
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
	u64 end = addr + size;
	int ret;

	if (WARN_ON_ONCE(!PAGE_ALIGNED(addr | size)))
		return -EINVAL;

	lockdep_assert_held_write(&kvm->mmu_lock);
	mapping = pkvm_mapping_iter_first(&pgt->pkvm_mappings, addr, end - 1);

	if (kvm_vm_is_protected(kvm)) {
		/* Protected VMs are mapped using RWX page-granular mappings */
		if (WARN_ON_ONCE(size != PAGE_SIZE))
			return -EINVAL;

		if (WARN_ON_ONCE(prot != KVM_PGTABLE_PROT_RWX))
			return -EINVAL;

		/*
		 * We either raced with another vCPU or the guest PTE
		 * has been poisoned by an erroneous host access.
		 */
		if (mapping) {
			ret = kvm_call_hyp_nvhe(__pkvm_vcpu_in_poison_fault);
			return ret ? -EFAULT : -EAGAIN;
		}

		ret = kvm_call_hyp_nvhe(__pkvm_host_donate_guest, pfn, gfn);
	} else {
		if (WARN_ON_ONCE(size != PAGE_SIZE && size != PMD_SIZE))
			return -EINVAL;

		/*
		 * We either raced with another vCPU or we're changing between
		 * page and block mappings. As per user_mem_abort(), same-size
		 * permission faults are handled in the relax_perms() path.
		 */
		if (mapping) {
			if (size == (mapping->nr_pages * PAGE_SIZE))
				return -EAGAIN;

			/*
			 * Remove _any_ pkvm_mapping overlapping with the range,
			 * bigger or smaller.
			 */
			ret = __pkvm_pgtable_stage2_unshare(pgt, addr, end);
			if (ret)
				return ret;

			mapping = NULL;
		}

		ret = kvm_call_hyp_nvhe(__pkvm_host_share_guest, pfn, gfn,
					size / PAGE_SIZE, prot);
	}

	if (ret)
		return ret;

	swap(mapping, cache->mapping);
	mapping->gfn = gfn;
	mapping->pfn = pfn;
	mapping->nr_pages = size / PAGE_SIZE;
	mapping->nc = !!(prot & (KVM_PGTABLE_PROT_DEVICE | KVM_PGTABLE_PROT_NORMAL_NC));
	pkvm_mapping_insert(mapping, &pgt->pkvm_mappings);

	return ret;
}

int pkvm_pgtable_stage2_unmap(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(pgt->mmu);

	if (WARN_ON(kvm_vm_is_protected(kvm)))
		return -EPERM;

	lockdep_assert_held_write(&kvm->mmu_lock);

	return __pkvm_pgtable_stage2_unshare(pgt, addr, addr + size);
}

int pkvm_pgtable_stage2_wrprotect(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(pgt->mmu);
	pkvm_handle_t handle = kvm->arch.pkvm.handle;
	struct pkvm_mapping *mapping;
	int ret = 0;

	if (WARN_ON(kvm_vm_is_protected(kvm)))
		return -EPERM;

	lockdep_assert_held(&kvm->mmu_lock);
	for_each_mapping_in_range_safe(pgt, addr, addr + size, mapping) {
		ret = kvm_call_hyp_nvhe(__pkvm_host_wrprotect_guest, handle, mapping->gfn,
					(u64)mapping->nr_pages);
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

	if (cpus_have_final_cap(ARM64_HAS_STAGE2_FWB))
		return 0;

	for_each_mapping_in_range_safe(pgt, addr, addr + size, mapping) {
		if (!mapping->nc)
			__clean_dcache_guest_page(pfn_to_kaddr(mapping->pfn),
						  PAGE_SIZE * mapping->nr_pages);
	}

	return 0;
}

bool pkvm_pgtable_stage2_test_clear_young(struct kvm_pgtable *pgt, u64 addr, u64 size, bool mkold)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(pgt->mmu);
	pkvm_handle_t handle = kvm->arch.pkvm.handle;
	struct pkvm_mapping *mapping;
	bool young = false;

	if (WARN_ON(kvm_vm_is_protected(kvm)))
		return false;

	lockdep_assert_held(&kvm->mmu_lock);
	for_each_mapping_in_range_safe(pgt, addr, addr + size, mapping)
		young |= kvm_call_hyp_nvhe(__pkvm_host_test_clear_young_guest, handle, mapping->gfn,
					   (u64)mapping->nr_pages, mkold);

	return young;
}

int pkvm_pgtable_stage2_relax_perms(struct kvm_pgtable *pgt, u64 addr, enum kvm_pgtable_prot prot,
				    enum kvm_pgtable_walk_flags flags)
{
	if (WARN_ON(kvm_vm_is_protected(kvm_s2_mmu_to_kvm(pgt->mmu))))
		return -EPERM;

	return kvm_call_hyp_nvhe(__pkvm_host_relax_perms_guest, addr >> PAGE_SHIFT, prot);
}

void pkvm_pgtable_stage2_mkyoung(struct kvm_pgtable *pgt, u64 addr,
				 enum kvm_pgtable_walk_flags flags)
{
	if (WARN_ON(kvm_vm_is_protected(kvm_s2_mmu_to_kvm(pgt->mmu))))
		return;

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

/*
 * Forcefully reclaim a page from the guest, zeroing its contents and
 * poisoning the stage-2 pte so that pages can no longer be mapped at
 * the same IPA. The page remains pinned until the guest is destroyed.
 */
bool pkvm_force_reclaim_guest_page(phys_addr_t phys)
{
	int ret = kvm_call_hyp_nvhe(__pkvm_force_reclaim_guest_page, phys);

	return !ret || ret == -EAGAIN;
}

static int pkvm_handle_hyp_req(struct pkvm_hyp_req *req)
{
	int ret = -EINVAL;

	switch (req->type) {
	case PKVM_HYP_REQ_HYP_ALLOC:
		ret = pkvm_hyp_topup(PKVM_TOPUP_HYP_ALLOC, req->mem.nr_pages);
		break;
	case PKVM_HYP_REQ_HYP_ALLOC_SELFTEST:
		ret = pkvm_hyp_topup(PKVM_TOPUP_HYP_ALLOC_SELFTEST, req->mem.nr_pages);
		break;
	}

	trace_kvm_handle_pkvm_hyp_req(req, ret);

	return ret;
}

int __pkvm_handle_smccc_req(struct arm_smccc_res *res)
{
	struct pkvm_hyp_req req;

	if (smccc_to_pkvm_hyp_req(&req, res))
		return pkvm_handle_hyp_req(&req);

	return res->a1;
}
