// SPDX-License-Identifier: GPL-2.0

#include "mmu.h"
#include "mmu_internal.h"
#include "tdp_mmu.h"
#include "spte.h"

static bool __read_mostly tdp_mmu_enabled = false;

static bool is_tdp_mmu_enabled(void)
{
#ifdef CONFIG_X86_64
	return tdp_enabled && READ_ONCE(tdp_mmu_enabled);
#else
	return false;
#endif /* CONFIG_X86_64 */
}

/* Initializes the TDP MMU for the VM, if enabled. */
void kvm_mmu_init_tdp_mmu(struct kvm *kvm)
{
	if (!is_tdp_mmu_enabled())
		return;

	/* This should not be changed for the lifetime of the VM. */
	kvm->arch.tdp_mmu_enabled = true;

	INIT_LIST_HEAD(&kvm->arch.tdp_mmu_roots);
}

void kvm_mmu_uninit_tdp_mmu(struct kvm *kvm)
{
	if (!kvm->arch.tdp_mmu_enabled)
		return;

	WARN_ON(!list_empty(&kvm->arch.tdp_mmu_roots));
}

#define for_each_tdp_mmu_root(_kvm, _root)			    \
	list_for_each_entry(_root, &_kvm->arch.tdp_mmu_roots, link)

bool is_tdp_mmu_root(struct kvm *kvm, hpa_t hpa)
{
	struct kvm_mmu_page *sp;

	sp = to_shadow_page(hpa);

	return sp->tdp_mmu_page && sp->root_count;
}

void kvm_tdp_mmu_free_root(struct kvm *kvm, struct kvm_mmu_page *root)
{
	lockdep_assert_held(&kvm->mmu_lock);

	WARN_ON(root->root_count);
	WARN_ON(!root->tdp_mmu_page);

	list_del(&root->link);

	free_page((unsigned long)root->spt);
	kmem_cache_free(mmu_page_header_cache, root);
}

static union kvm_mmu_page_role page_role_for_level(struct kvm_vcpu *vcpu,
						   int level)
{
	union kvm_mmu_page_role role;

	role = vcpu->arch.mmu->mmu_role.base;
	role.level = level;
	role.direct = true;
	role.gpte_is_8_bytes = true;
	role.access = ACC_ALL;

	return role;
}

static struct kvm_mmu_page *alloc_tdp_mmu_page(struct kvm_vcpu *vcpu, gfn_t gfn,
					       int level)
{
	struct kvm_mmu_page *sp;

	sp = kvm_mmu_memory_cache_alloc(&vcpu->arch.mmu_page_header_cache);
	sp->spt = kvm_mmu_memory_cache_alloc(&vcpu->arch.mmu_shadow_page_cache);
	set_page_private(virt_to_page(sp->spt), (unsigned long)sp);

	sp->role.word = page_role_for_level(vcpu, level).word;
	sp->gfn = gfn;
	sp->tdp_mmu_page = true;

	return sp;
}

static struct kvm_mmu_page *get_tdp_mmu_vcpu_root(struct kvm_vcpu *vcpu)
{
	union kvm_mmu_page_role role;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_page *root;

	role = page_role_for_level(vcpu, vcpu->arch.mmu->shadow_root_level);

	spin_lock(&kvm->mmu_lock);

	/* Check for an existing root before allocating a new one. */
	for_each_tdp_mmu_root(kvm, root) {
		if (root->role.word == role.word) {
			kvm_mmu_get_root(kvm, root);
			spin_unlock(&kvm->mmu_lock);
			return root;
		}
	}

	root = alloc_tdp_mmu_page(vcpu, 0, vcpu->arch.mmu->shadow_root_level);
	root->root_count = 1;

	list_add(&root->link, &kvm->arch.tdp_mmu_roots);

	spin_unlock(&kvm->mmu_lock);

	return root;
}

hpa_t kvm_tdp_mmu_get_vcpu_root_hpa(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *root;

	root = get_tdp_mmu_vcpu_root(vcpu);
	if (!root)
		return INVALID_PAGE;

	return __pa(root->spt);
}
