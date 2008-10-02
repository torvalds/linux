#ifndef __KVM_X86_MMU_H
#define __KVM_X86_MMU_H

#include <linux/kvm_host.h>

#define PT64_PT_BITS 9
#define PT64_ENT_PER_PAGE (1 << PT64_PT_BITS)
#define PT32_PT_BITS 10
#define PT32_ENT_PER_PAGE (1 << PT32_PT_BITS)

#define PT_WRITABLE_SHIFT 1

#define PT_PRESENT_MASK (1ULL << 0)
#define PT_WRITABLE_MASK (1ULL << PT_WRITABLE_SHIFT)
#define PT_USER_MASK (1ULL << 2)
#define PT_PWT_MASK (1ULL << 3)
#define PT_PCD_MASK (1ULL << 4)
#define PT_ACCESSED_SHIFT 5
#define PT_ACCESSED_MASK (1ULL << PT_ACCESSED_SHIFT)
#define PT_DIRTY_MASK (1ULL << 6)
#define PT_PAGE_SIZE_MASK (1ULL << 7)
#define PT_PAT_MASK (1ULL << 7)
#define PT_GLOBAL_MASK (1ULL << 8)
#define PT64_NX_SHIFT 63
#define PT64_NX_MASK (1ULL << PT64_NX_SHIFT)

#define PT_PAT_SHIFT 7
#define PT_DIR_PAT_SHIFT 12
#define PT_DIR_PAT_MASK (1ULL << PT_DIR_PAT_SHIFT)

#define PT32_DIR_PSE36_SIZE 4
#define PT32_DIR_PSE36_SHIFT 13
#define PT32_DIR_PSE36_MASK \
	(((1ULL << PT32_DIR_PSE36_SIZE) - 1) << PT32_DIR_PSE36_SHIFT)

#define PT64_ROOT_LEVEL 4
#define PT32_ROOT_LEVEL 2
#define PT32E_ROOT_LEVEL 3

static inline void kvm_mmu_free_some_pages(struct kvm_vcpu *vcpu)
{
	if (unlikely(vcpu->kvm->arch.n_free_mmu_pages < KVM_MIN_FREE_MMU_PAGES))
		__kvm_mmu_free_some_pages(vcpu);
}

static inline int kvm_mmu_reload(struct kvm_vcpu *vcpu)
{
	if (likely(vcpu->arch.mmu.root_hpa != INVALID_PAGE))
		return 0;

	return kvm_mmu_load(vcpu);
}

static inline int is_long_mode(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_X86_64
	return vcpu->arch.shadow_efer & EFER_LME;
#else
	return 0;
#endif
}

static inline int is_pae(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.cr4 & X86_CR4_PAE;
}

static inline int is_pse(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.cr4 & X86_CR4_PSE;
}

static inline int is_paging(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.cr0 & X86_CR0_PG;
}

#endif
