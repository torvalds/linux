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
#define PT_MAX_HUGEPAGE_LEVEL (PT_PAGE_TABLE_LEVEL + KVM_NR_PAGE_SIZES - 1)

static inline u64 rsvd_bits(int s, int e)
{
	return ((1ULL << (e - s + 1)) - 1) << s;
}

int kvm_mmu_get_spte_hierarchy(struct kvm_vcpu *vcpu, u64 addr, u64 sptes[4]);
void kvm_mmu_set_mmio_spte_mask(u64 mmio_mask);

/*
 * Return values of handle_mmio_page_fault_common:
 * RET_MMIO_PF_EMULATE: it is a real mmio page fault, emulate the instruction
 *			directly.
 * RET_MMIO_PF_INVALID: invalid spte is detected then let the real page
 *			fault path update the mmio spte.
 * RET_MMIO_PF_RETRY: let CPU fault again on the address.
 * RET_MMIO_PF_BUG: bug is detected.
 */
enum {
	RET_MMIO_PF_EMULATE = 1,
	RET_MMIO_PF_INVALID = 2,
	RET_MMIO_PF_RETRY = 0,
	RET_MMIO_PF_BUG = -1
};

int handle_mmio_page_fault_common(struct kvm_vcpu *vcpu, u64 addr, bool direct);
void kvm_init_shadow_mmu(struct kvm_vcpu *vcpu);
void kvm_init_shadow_ept_mmu(struct kvm_vcpu *vcpu, bool execonly);

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

/*
 * Currently, we have two sorts of write-protection, a) the first one
 * write-protects guest page to sync the guest modification, b) another one is
 * used to sync dirty bitmap when we do KVM_GET_DIRTY_LOG. The differences
 * between these two sorts are:
 * 1) the first case clears SPTE_MMU_WRITEABLE bit.
 * 2) the first case requires flushing tlb immediately avoiding corrupting
 *    shadow page table between all vcpus so it should be in the protection of
 *    mmu-lock. And the another case does not need to flush tlb until returning
 *    the dirty bitmap to userspace since it only write-protects the page
 *    logged in the bitmap, that means the page in the dirty bitmap is not
 *    missed, so it can flush tlb out of mmu-lock.
 *
 * So, there is the problem: the first case can meet the corrupted tlb caused
 * by another case which write-protects pages but without flush tlb
 * immediately. In order to making the first case be aware this problem we let
 * it flush tlb if we try to write-protect a spte whose SPTE_MMU_WRITEABLE bit
 * is set, it works since another case never touches SPTE_MMU_WRITEABLE bit.
 *
 * Anyway, whenever a spte is updated (only permission and status bits are
 * changed) we need to check whether the spte with SPTE_MMU_WRITEABLE becomes
 * readonly, if that happens, we need to flush tlb. Fortunately,
 * mmu_spte_update() has already handled it perfectly.
 *
 * The rules to use SPTE_MMU_WRITEABLE and PT_WRITABLE_MASK:
 * - if we want to see if it has writable tlb entry or if the spte can be
 *   writable on the mmu mapping, check SPTE_MMU_WRITEABLE, this is the most
 *   case, otherwise
 * - if we fix page fault on the spte or do write-protection by dirty logging,
 *   check PT_WRITABLE_MASK.
 *
 * TODO: introduce APIs to split these two cases.
 */
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
static inline bool permission_fault(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
				    unsigned pte_access, unsigned pfec)
{
	int cpl = kvm_x86_ops->get_cpl(vcpu);
	unsigned long rflags = kvm_x86_ops->get_rflags(vcpu);

	/*
	 * If CPL < 3, SMAP prevention are disabled if EFLAGS.AC = 1.
	 *
	 * If CPL = 3, SMAP applies to all supervisor-mode data accesses
	 * (these are implicit supervisor accesses) regardless of the value
	 * of EFLAGS.AC.
	 *
	 * This computes (cpl < 3) && (rflags & X86_EFLAGS_AC), leaving
	 * the result in X86_EFLAGS_AC. We then insert it in place of
	 * the PFERR_RSVD_MASK bit; this bit will always be zero in pfec,
	 * but it will be one in index if SMAP checks are being overridden.
	 * It is important to keep this branchless.
	 */
	unsigned long smap = (cpl - 3) & (rflags & X86_EFLAGS_AC);
	int index = (pfec >> 1) +
		    (smap >> (X86_EFLAGS_AC_BIT - PFERR_RSVD_BIT + 1));

	WARN_ON(pfec & PFERR_RSVD_MASK);

	return (mmu->permissions[index] >> pte_access) & 1;
}

void kvm_mmu_invalidate_zap_all_pages(struct kvm *kvm);
void kvm_zap_gfn_range(struct kvm *kvm, gfn_t gfn_start, gfn_t gfn_end);
#endif
