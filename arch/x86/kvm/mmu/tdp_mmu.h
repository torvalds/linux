// SPDX-License-Identifier: GPL-2.0

#ifndef __KVM_X86_MMU_TDP_MMU_H
#define __KVM_X86_MMU_TDP_MMU_H

#include <linux/kvm_host.h>

#include "spte.h"

void kvm_mmu_init_tdp_mmu(struct kvm *kvm);
void kvm_mmu_uninit_tdp_mmu(struct kvm *kvm);

void kvm_tdp_mmu_alloc_root(struct kvm_vcpu *vcpu, bool private);

__must_check static inline bool kvm_tdp_mmu_get_root(struct kvm_mmu_page *root)
{
	return refcount_inc_not_zero(&root->tdp_mmu_root_count);
}

void kvm_tdp_mmu_put_root(struct kvm *kvm, struct kvm_mmu_page *root);

enum kvm_tdp_mmu_root_types {
	KVM_INVALID_ROOTS = BIT(0),
	KVM_DIRECT_ROOTS = BIT(1),
	KVM_MIRROR_ROOTS = BIT(2),
	KVM_VALID_ROOTS = KVM_DIRECT_ROOTS | KVM_MIRROR_ROOTS,
	KVM_ALL_ROOTS = KVM_VALID_ROOTS | KVM_INVALID_ROOTS,
};

static inline enum kvm_tdp_mmu_root_types kvm_gfn_range_filter_to_root_types(struct kvm *kvm,
							     enum kvm_gfn_range_filter process)
{
	enum kvm_tdp_mmu_root_types ret = 0;

	if (!kvm_has_mirrored_tdp(kvm))
		return KVM_DIRECT_ROOTS;

	if (process & KVM_FILTER_PRIVATE)
		ret |= KVM_MIRROR_ROOTS;
	if (process & KVM_FILTER_SHARED)
		ret |= KVM_DIRECT_ROOTS;

	WARN_ON_ONCE(!ret);

	return ret;
}

static inline struct kvm_mmu_page *tdp_mmu_get_root_for_fault(struct kvm_vcpu *vcpu,
							      struct kvm_page_fault *fault)
{
	if (unlikely(!kvm_is_addr_direct(vcpu->kvm, fault->addr)))
		return root_to_sp(vcpu->arch.mmu->mirror_root_hpa);

	return root_to_sp(vcpu->arch.mmu->root.hpa);
}

static inline struct kvm_mmu_page *tdp_mmu_get_root(struct kvm_vcpu *vcpu,
						    enum kvm_tdp_mmu_root_types type)
{
	if (unlikely(type == KVM_MIRROR_ROOTS))
		return root_to_sp(vcpu->arch.mmu->mirror_root_hpa);

	return root_to_sp(vcpu->arch.mmu->root.hpa);
}

bool kvm_tdp_mmu_zap_leafs(struct kvm *kvm, gfn_t start, gfn_t end, bool flush);
bool kvm_tdp_mmu_zap_possible_nx_huge_page(struct kvm *kvm,
					   struct kvm_mmu_page *sp);
void kvm_tdp_mmu_zap_all(struct kvm *kvm);
void kvm_tdp_mmu_invalidate_roots(struct kvm *kvm,
				  enum kvm_tdp_mmu_root_types root_types);
void kvm_tdp_mmu_zap_invalidated_roots(struct kvm *kvm, bool shared);

int kvm_tdp_mmu_map(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault);

bool kvm_tdp_mmu_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range,
				 bool flush);
bool kvm_tdp_mmu_age_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range);
bool kvm_tdp_mmu_test_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range);

bool kvm_tdp_mmu_wrprot_slot(struct kvm *kvm,
			     const struct kvm_memory_slot *slot, int min_level);
void kvm_tdp_mmu_clear_dirty_slot(struct kvm *kvm,
				  const struct kvm_memory_slot *slot);
void kvm_tdp_mmu_clear_dirty_pt_masked(struct kvm *kvm,
				       struct kvm_memory_slot *slot,
				       gfn_t gfn, unsigned long mask,
				       bool wrprot);
void kvm_tdp_mmu_recover_huge_pages(struct kvm *kvm,
				    const struct kvm_memory_slot *slot);

bool kvm_tdp_mmu_write_protect_gfn(struct kvm *kvm,
				   struct kvm_memory_slot *slot, gfn_t gfn,
				   int min_level);

void kvm_tdp_mmu_try_split_huge_pages(struct kvm *kvm,
				      const struct kvm_memory_slot *slot,
				      gfn_t start, gfn_t end,
				      int target_level, bool shared);

static inline void kvm_tdp_mmu_walk_lockless_begin(void)
{
	rcu_read_lock();
}

static inline void kvm_tdp_mmu_walk_lockless_end(void)
{
	rcu_read_unlock();
}

int kvm_tdp_mmu_get_walk(struct kvm_vcpu *vcpu, u64 addr, u64 *sptes,
			 int *root_level);
u64 *kvm_tdp_mmu_fast_pf_get_last_sptep(struct kvm_vcpu *vcpu, gfn_t gfn,
					u64 *spte);

#ifdef CONFIG_X86_64
static inline bool is_tdp_mmu_page(struct kvm_mmu_page *sp) { return sp->tdp_mmu_page; }
#else
static inline bool is_tdp_mmu_page(struct kvm_mmu_page *sp) { return false; }
#endif

#endif /* __KVM_X86_MMU_TDP_MMU_H */
