/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_MMU_H
#define __KVM_X86_MMU_H

#include <linux/kvm_host.h>
#include "kvm_cache_regs.h"
#include "cpuid.h"

#define PT64_PT_BITS 9
#define PT64_ENT_PER_PAGE (1 << PT64_PT_BITS)
#define PT32_PT_BITS 10
#define PT32_ENT_PER_PAGE (1 << PT32_PT_BITS)

#define PT_WRITABLE_SHIFT 1
#define PT_USER_SHIFT 2

#define PT_PRESENT_MASK (1ULL << 0)
#define PT_WRITABLE_MASK (1ULL << PT_WRITABLE_SHIFT)
#define PT_USER_MASK (1ULL << PT_USER_SHIFT)
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

#define PT64_ROOT_5LEVEL 5
#define PT64_ROOT_4LEVEL 4
#define PT32_ROOT_LEVEL 2
#define PT32E_ROOT_LEVEL 3

#define KVM_MMU_CR4_ROLE_BITS (X86_CR4_PSE | X86_CR4_PAE | X86_CR4_LA57 | \
			       X86_CR4_SMEP | X86_CR4_SMAP | X86_CR4_PKE)

#define KVM_MMU_CR0_ROLE_BITS (X86_CR0_PG | X86_CR0_WP)
#define KVM_MMU_EFER_ROLE_BITS (EFER_LME | EFER_NX)

static __always_inline u64 rsvd_bits(int s, int e)
{
	BUILD_BUG_ON(__builtin_constant_p(e) && __builtin_constant_p(s) && e < s);

	if (__builtin_constant_p(e))
		BUILD_BUG_ON(e > 63);
	else
		e &= 63;

	if (e < s)
		return 0;

	return ((2ULL << (e - s)) - 1) << s;
}

void kvm_mmu_set_mmio_spte_mask(u64 mmio_value, u64 mmio_mask, u64 access_mask);
void kvm_mmu_set_ept_masks(bool has_ad_bits, bool has_exec_only);

void kvm_init_mmu(struct kvm_vcpu *vcpu);
void kvm_init_shadow_npt_mmu(struct kvm_vcpu *vcpu, unsigned long cr0,
			     unsigned long cr4, u64 efer, gpa_t nested_cr3);
void kvm_init_shadow_ept_mmu(struct kvm_vcpu *vcpu, bool execonly,
			     int huge_page_level, bool accessed_dirty,
			     gpa_t new_eptp);
bool kvm_can_do_async_pf(struct kvm_vcpu *vcpu);
int kvm_handle_page_fault(struct kvm_vcpu *vcpu, u64 error_code,
				u64 fault_address, char *insn, int insn_len);

int kvm_mmu_load(struct kvm_vcpu *vcpu);
void kvm_mmu_unload(struct kvm_vcpu *vcpu);
void kvm_mmu_free_obsolete_roots(struct kvm_vcpu *vcpu);
void kvm_mmu_sync_roots(struct kvm_vcpu *vcpu);
void kvm_mmu_sync_prev_roots(struct kvm_vcpu *vcpu);

static inline int kvm_mmu_reload(struct kvm_vcpu *vcpu)
{
	if (likely(vcpu->arch.mmu->root.hpa != INVALID_PAGE))
		return 0;

	return kvm_mmu_load(vcpu);
}

static inline unsigned long kvm_get_pcid(struct kvm_vcpu *vcpu, gpa_t cr3)
{
	BUILD_BUG_ON((X86_CR3_PCID_MASK & PAGE_MASK) != 0);

	return kvm_read_cr4_bits(vcpu, X86_CR4_PCIDE)
	       ? cr3 & X86_CR3_PCID_MASK
	       : 0;
}

static inline unsigned long kvm_get_active_pcid(struct kvm_vcpu *vcpu)
{
	return kvm_get_pcid(vcpu, kvm_read_cr3(vcpu));
}

static inline void kvm_mmu_load_pgd(struct kvm_vcpu *vcpu)
{
	u64 root_hpa = vcpu->arch.mmu->root.hpa;

	if (!VALID_PAGE(root_hpa))
		return;

	static_call(kvm_x86_load_mmu_pgd)(vcpu, root_hpa,
					  vcpu->arch.mmu->shadow_root_level);
}

struct kvm_page_fault {
	/* arguments to kvm_mmu_do_page_fault.  */
	const gpa_t addr;
	const u32 error_code;
	const bool prefetch;

	/* Derived from error_code.  */
	const bool exec;
	const bool write;
	const bool present;
	const bool rsvd;
	const bool user;

	/* Derived from mmu and global state.  */
	const bool is_tdp;
	const bool nx_huge_page_workaround_enabled;

	/*
	 * Whether a >4KB mapping can be created or is forbidden due to NX
	 * hugepages.
	 */
	bool huge_page_disallowed;

	/*
	 * Maximum page size that can be created for this fault; input to
	 * FNAME(fetch), __direct_map and kvm_tdp_mmu_map.
	 */
	u8 max_level;

	/*
	 * Page size that can be created based on the max_level and the
	 * page size used by the host mapping.
	 */
	u8 req_level;

	/*
	 * Page size that will be created based on the req_level and
	 * huge_page_disallowed.
	 */
	u8 goal_level;

	/* Shifted addr, or result of guest page table walk if addr is a gva.  */
	gfn_t gfn;

	/* The memslot containing gfn. May be NULL. */
	struct kvm_memory_slot *slot;

	/* Outputs of kvm_faultin_pfn.  */
	kvm_pfn_t pfn;
	hva_t hva;
	bool map_writable;
};

int kvm_tdp_page_fault(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault);

extern int nx_huge_pages;
static inline bool is_nx_huge_page_enabled(void)
{
	return READ_ONCE(nx_huge_pages);
}

static inline int kvm_mmu_do_page_fault(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
					u32 err, bool prefetch)
{
	struct kvm_page_fault fault = {
		.addr = cr2_or_gpa,
		.error_code = err,
		.exec = err & PFERR_FETCH_MASK,
		.write = err & PFERR_WRITE_MASK,
		.present = err & PFERR_PRESENT_MASK,
		.rsvd = err & PFERR_RSVD_MASK,
		.user = err & PFERR_USER_MASK,
		.prefetch = prefetch,
		.is_tdp = likely(vcpu->arch.mmu->page_fault == kvm_tdp_page_fault),
		.nx_huge_page_workaround_enabled = is_nx_huge_page_enabled(),

		.max_level = KVM_MAX_HUGEPAGE_LEVEL,
		.req_level = PG_LEVEL_4K,
		.goal_level = PG_LEVEL_4K,
	};
#ifdef CONFIG_RETPOLINE
	if (fault.is_tdp)
		return kvm_tdp_page_fault(vcpu, &fault);
#endif
	return vcpu->arch.mmu->page_fault(vcpu, &fault);
}

/*
 * Check if a given access (described through the I/D, W/R and U/S bits of a
 * page fault error code pfec) causes a permission fault with the given PTE
 * access rights (in ACC_* format).
 *
 * Return zero if the access does not fault; return the page fault error code
 * if the access faults.
 */
static inline u8 permission_fault(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
				  unsigned pte_access, unsigned pte_pkey,
				  unsigned pfec)
{
	int cpl = static_call(kvm_x86_get_cpl)(vcpu);
	unsigned long rflags = static_call(kvm_x86_get_rflags)(vcpu);

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
	bool fault = (mmu->permissions[index] >> pte_access) & 1;
	u32 errcode = PFERR_PRESENT_MASK;

	WARN_ON(pfec & (PFERR_PK_MASK | PFERR_RSVD_MASK));
	if (unlikely(mmu->pkru_mask)) {
		u32 pkru_bits, offset;

		/*
		* PKRU defines 32 bits, there are 16 domains and 2
		* attribute bits per domain in pkru.  pte_pkey is the
		* index of the protection domain, so pte_pkey * 2 is
		* is the index of the first bit for the domain.
		*/
		pkru_bits = (vcpu->arch.pkru >> (pte_pkey * 2)) & 3;

		/* clear present bit, replace PFEC.RSVD with ACC_USER_MASK. */
		offset = (pfec & ~1) +
			((pte_access & PT_USER_MASK) << (PFERR_RSVD_BIT - PT_USER_SHIFT));

		pkru_bits &= mmu->pkru_mask >> offset;
		errcode |= -pkru_bits & PFERR_PK_MASK;
		fault |= (pkru_bits != 0);
	}

	return -(u32)fault & errcode;
}

void kvm_zap_gfn_range(struct kvm *kvm, gfn_t gfn_start, gfn_t gfn_end);

int kvm_arch_write_log_dirty(struct kvm_vcpu *vcpu);

int kvm_mmu_post_init_vm(struct kvm *kvm);
void kvm_mmu_pre_destroy_vm(struct kvm *kvm);

static inline bool kvm_shadow_root_allocated(struct kvm *kvm)
{
	/*
	 * Read shadow_root_allocated before related pointers. Hence, threads
	 * reading shadow_root_allocated in any lock context are guaranteed to
	 * see the pointers. Pairs with smp_store_release in
	 * mmu_first_shadow_root_alloc.
	 */
	return smp_load_acquire(&kvm->arch.shadow_root_allocated);
}

#ifdef CONFIG_X86_64
static inline bool is_tdp_mmu_enabled(struct kvm *kvm) { return kvm->arch.tdp_mmu_enabled; }
#else
static inline bool is_tdp_mmu_enabled(struct kvm *kvm) { return false; }
#endif

static inline bool kvm_memslots_have_rmaps(struct kvm *kvm)
{
	return !is_tdp_mmu_enabled(kvm) || kvm_shadow_root_allocated(kvm);
}

static inline gfn_t gfn_to_index(gfn_t gfn, gfn_t base_gfn, int level)
{
	/* KVM_HPAGE_GFN_SHIFT(PG_LEVEL_4K) must be 0. */
	return (gfn >> KVM_HPAGE_GFN_SHIFT(level)) -
		(base_gfn >> KVM_HPAGE_GFN_SHIFT(level));
}

static inline unsigned long
__kvm_mmu_slot_lpages(struct kvm_memory_slot *slot, unsigned long npages,
		      int level)
{
	return gfn_to_index(slot->base_gfn + npages - 1,
			    slot->base_gfn, level) + 1;
}

static inline unsigned long
kvm_mmu_slot_lpages(struct kvm_memory_slot *slot, int level)
{
	return __kvm_mmu_slot_lpages(slot, slot->npages, level);
}

static inline void kvm_update_page_stats(struct kvm *kvm, int level, int count)
{
	atomic64_add(count, &kvm->stat.pages[level - 1]);
}

gpa_t translate_nested_gpa(struct kvm_vcpu *vcpu, gpa_t gpa, u32 access,
			   struct x86_exception *exception);

static inline gpa_t kvm_translate_gpa(struct kvm_vcpu *vcpu,
				      struct kvm_mmu *mmu,
				      gpa_t gpa, u32 access,
				      struct x86_exception *exception)
{
	if (mmu != &vcpu->arch.nested_mmu)
		return gpa;
	return translate_nested_gpa(vcpu, gpa, access, exception);
}
#endif
