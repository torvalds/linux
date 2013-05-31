#ifndef __KVM_X86_MMU_H
#define __KVM_X86_MMU_H

#include <linux/kvm_host.h>
#include "kvm_cache_regs.h"

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
#define PT_DIRTY_SHIFT 6
#define PT_DIRTY_MASK (1ULL << PT_DIRTY_SHIFT)
#define PT_PAGE_SIZE_SHIFT 7
#define PT_PAGE_SIZE_MASK (1ULL << PT_PAGE_SIZE_SHIFT)
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

#define PT_PDPE_LEVEL 3
#define PT_DIRECTORY_LEVEL 2
#define PT_PAGE_TABLE_LEVEL 1

#define PFERR_PRESENT_MASK (1U << 0)
#define PFERR_WRITE_MASK (1U << 1)
#define PFERR_USER_MASK (1U << 2)
#define PFERR_RSVD_MASK (1U << 3)
#define PFERR_FETCH_MASK (1U << 4)

int kvm_mmu_get_spte_hierarchy(struct kvm_vcpu *vcpu, u64 addr, u64 sptes[4]);
void kvm_mmu_set_mmio_spte_mask(u64 mmio_mask);
int handle_mmio_page_fault_common(struct kvm_vcpu *vcpu, u64 addr, bool direct);
int kvm_init_shadow_mmu(struct kvm_vcpu *vcpu, struct kvm_mmu *context);

static inline unsigned int kvm_mmu_available_pages(struct kvm *kvm)
{
	if (kvm->arch.n_max_mmu_pages > kvm->arch.n_used_mmu_pages)
		return kvm->arch.n_max_mmu_pages -
			kvm->arch.n_used_mmu_pages;

	return 0;
}

static inline int kvm_mmu_reload(struct kvm_vcpu *vcpu)
{
	if (likely(vcpu->arch.mmu.root_hpa != INVALID_PAGE))
		return 0;

	return kvm_mmu_load(vcpu);
}

static inline int is_present_gpte(unsigned long pte)
{
	return pte & PT_PRESENT_MASK;
}

static inline int is_writable_pte(unsigned long pte)
{
	return pte & PT_WRITABLE_MASK;
}

static inline bool is_write_protection(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr0_bits(vcpu, X86_CR0_WP);
}

/*
 * Will a fault with a given page-fault error code (pfec) cause a permission
 * fault with the given access (in ACC_* format)?
 */
static inline bool permission_fault(struct kvm_mmu *mmu, unsigned pte_access,
				    unsigned pfec)
{
	return (mmu->permissions[pfec >> 1] >> pte_access) & 1;
}

void kvm_mmu_invalidate_zap_all_pages(struct kvm *kvm);
#endif
