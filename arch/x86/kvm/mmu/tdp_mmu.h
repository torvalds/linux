// SPDX-License-Identifier: GPL-2.0

#ifndef __KVM_X86_MMU_TDP_MMU_H
#define __KVM_X86_MMU_TDP_MMU_H

#include <linux/kvm_host.h>

hpa_t kvm_tdp_mmu_get_vcpu_root_hpa(struct kvm_vcpu *vcpu);
void kvm_tdp_mmu_free_root(struct kvm *kvm, struct kvm_mmu_page *root);

bool kvm_tdp_mmu_zap_gfn_range(struct kvm *kvm, gfn_t start, gfn_t end);
void kvm_tdp_mmu_zap_all(struct kvm *kvm);

int kvm_tdp_mmu_map(struct kvm_vcpu *vcpu, gpa_t gpa, u32 error_code,
		    int map_writable, int max_level, kvm_pfn_t pfn,
		    bool prefault);

int kvm_tdp_mmu_zap_hva_range(struct kvm *kvm, unsigned long start,
			      unsigned long end);

int kvm_tdp_mmu_age_hva_range(struct kvm *kvm, unsigned long start,
			      unsigned long end);
int kvm_tdp_mmu_test_age_hva(struct kvm *kvm, unsigned long hva);

int kvm_tdp_mmu_set_spte_hva(struct kvm *kvm, unsigned long address,
			     pte_t *host_ptep);

bool kvm_tdp_mmu_wrprot_slot(struct kvm *kvm, struct kvm_memory_slot *slot,
			     int min_level);
bool kvm_tdp_mmu_clear_dirty_slot(struct kvm *kvm,
				  struct kvm_memory_slot *slot);
void kvm_tdp_mmu_clear_dirty_pt_masked(struct kvm *kvm,
				       struct kvm_memory_slot *slot,
				       gfn_t gfn, unsigned long mask,
				       bool wrprot);
void kvm_tdp_mmu_zap_collapsible_sptes(struct kvm *kvm,
				       struct kvm_memory_slot *slot);

bool kvm_tdp_mmu_write_protect_gfn(struct kvm *kvm,
				   struct kvm_memory_slot *slot, gfn_t gfn);

int kvm_tdp_mmu_get_walk(struct kvm_vcpu *vcpu, u64 addr, u64 *sptes,
			 int *root_level);

#ifdef CONFIG_X86_64
void kvm_mmu_init_tdp_mmu(struct kvm *kvm);
void kvm_mmu_uninit_tdp_mmu(struct kvm *kvm);
static inline bool is_tdp_mmu_enabled(struct kvm *kvm) { return kvm->arch.tdp_mmu_enabled; }
static inline bool is_tdp_mmu_page(struct kvm_mmu_page *sp) { return sp->tdp_mmu_page; }
#else
static inline void kvm_mmu_init_tdp_mmu(struct kvm *kvm) {}
static inline void kvm_mmu_uninit_tdp_mmu(struct kvm *kvm) {}
static inline bool is_tdp_mmu_enabled(struct kvm *kvm) { return false; }
static inline bool is_tdp_mmu_page(struct kvm_mmu_page *sp) { return false; }
#endif

static inline bool is_tdp_mmu_root(struct kvm *kvm, hpa_t hpa)
{
	struct kvm_mmu_page *sp;

	if (!is_tdp_mmu_enabled(kvm))
		return false;
	if (WARN_ON(!VALID_PAGE(hpa)))
		return false;

	sp = to_shadow_page(hpa);
	if (WARN_ON(!sp))
		return false;

	return is_tdp_mmu_page(sp) && sp->root_count;
}

#endif /* __KVM_X86_MMU_TDP_MMU_H */
