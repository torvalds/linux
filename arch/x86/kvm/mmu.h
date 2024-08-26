/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_MMU_H
#define __KVM_X86_MMU_H

#include <linux/kvm_host.h>
#include "kvm_cache_regs.h"
#include "cpuid.h"

extern bool __read_mostly enable_mmio_caching;

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

static inline gfn_t kvm_mmu_max_gfn(void)
{
	/*
	 * Note that this uses the host MAXPHYADDR, not the guest's.
	 * EPT/NPT cannot support GPAs that would exceed host.MAXPHYADDR;
	 * assuming KVM is running on bare metal, guest accesses beyond
	 * host.MAXPHYADDR will hit a #PF(RSVD) and never cause a vmexit
	 * (either EPT Violation/Misconfig or #NPF), and so KVM will never
	 * install a SPTE for such addresses.  If KVM is running as a VM
	 * itself, on the other hand, it might see a MAXPHYADDR that is less
	 * than hardware's real MAXPHYADDR.  Using the host MAXPHYADDR
	 * disallows such SPTEs entirely and simplifies the TDP MMU.
	 */
	int max_gpa_bits = likely(tdp_enabled) ? kvm_host.maxphyaddr : 52;

	return (1ULL << (max_gpa_bits - PAGE_SHIFT)) - 1;
}

u8 kvm_mmu_get_max_tdp_level(void);

void kvm_mmu_set_mmio_spte_mask(u64 mmio_value, u64 mmio_mask, u64 access_mask);
void kvm_mmu_set_me_spte_mask(u64 me_value, u64 me_mask);
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
void __kvm_mmu_refresh_passthrough_bits(struct kvm_vcpu *vcpu,
					struct kvm_mmu *mmu);

int kvm_mmu_load(struct kvm_vcpu *vcpu);
void kvm_mmu_unload(struct kvm_vcpu *vcpu);
void kvm_mmu_free_obsolete_roots(struct kvm_vcpu *vcpu);
void kvm_mmu_sync_roots(struct kvm_vcpu *vcpu);
void kvm_mmu_sync_prev_roots(struct kvm_vcpu *vcpu);
void kvm_mmu_track_write(struct kvm_vcpu *vcpu, gpa_t gpa, const u8 *new,
			 int bytes);

static inline int kvm_mmu_reload(struct kvm_vcpu *vcpu)
{
	if (likely(vcpu->arch.mmu->root.hpa != INVALID_PAGE))
		return 0;

	return kvm_mmu_load(vcpu);
}

static inline unsigned long kvm_get_pcid(struct kvm_vcpu *vcpu, gpa_t cr3)
{
	BUILD_BUG_ON((X86_CR3_PCID_MASK & PAGE_MASK) != 0);

	return kvm_is_cr4_bit_set(vcpu, X86_CR4_PCIDE)
	       ? cr3 & X86_CR3_PCID_MASK
	       : 0;
}

static inline unsigned long kvm_get_active_pcid(struct kvm_vcpu *vcpu)
{
	return kvm_get_pcid(vcpu, kvm_read_cr3(vcpu));
}

static inline unsigned long kvm_get_active_cr3_lam_bits(struct kvm_vcpu *vcpu)
{
	if (!guest_can_use(vcpu, X86_FEATURE_LAM))
		return 0;

	return kvm_read_cr3(vcpu) & (X86_CR3_LAM_U48 | X86_CR3_LAM_U57);
}

static inline void kvm_mmu_load_pgd(struct kvm_vcpu *vcpu)
{
	u64 root_hpa = vcpu->arch.mmu->root.hpa;

	if (!VALID_PAGE(root_hpa))
		return;

	kvm_x86_call(load_mmu_pgd)(vcpu, root_hpa,
				   vcpu->arch.mmu->root_role.level);
}

static inline void kvm_mmu_refresh_passthrough_bits(struct kvm_vcpu *vcpu,
						    struct kvm_mmu *mmu)
{
	/*
	 * When EPT is enabled, KVM may passthrough CR0.WP to the guest, i.e.
	 * @mmu's snapshot of CR0.WP and thus all related paging metadata may
	 * be stale.  Refresh CR0.WP and the metadata on-demand when checking
	 * for permission faults.  Exempt nested MMUs, i.e. MMUs for shadowing
	 * nEPT and nNPT, as CR0.WP is ignored in both cases.  Note, KVM does
	 * need to refresh nested_mmu, a.k.a. the walker used to translate L2
	 * GVAs to GPAs, as that "MMU" needs to honor L2's CR0.WP.
	 */
	if (!tdp_enabled || mmu == &vcpu->arch.guest_mmu)
		return;

	__kvm_mmu_refresh_passthrough_bits(vcpu, mmu);
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
				  u64 access)
{
	/* strip nested paging fault error codes */
	unsigned int pfec = access;
	unsigned long rflags = kvm_x86_call(get_rflags)(vcpu);

	/*
	 * For explicit supervisor accesses, SMAP is disabled if EFLAGS.AC = 1.
	 * For implicit supervisor accesses, SMAP cannot be overridden.
	 *
	 * SMAP works on supervisor accesses only, and not_smap can
	 * be set or not set when user access with neither has any bearing
	 * on the result.
	 *
	 * We put the SMAP checking bit in place of the PFERR_RSVD_MASK bit;
	 * this bit will always be zero in pfec, but it will be one in index
	 * if SMAP checks are being disabled.
	 */
	u64 implicit_access = access & PFERR_IMPLICIT_ACCESS;
	bool not_smap = ((rflags & X86_EFLAGS_AC) | implicit_access) == X86_EFLAGS_AC;
	int index = (pfec | (not_smap ? PFERR_RSVD_MASK : 0)) >> 1;
	u32 errcode = PFERR_PRESENT_MASK;
	bool fault;

	kvm_mmu_refresh_passthrough_bits(vcpu, mmu);

	fault = (mmu->permissions[index] >> pte_access) & 1;

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
		offset = (pfec & ~1) | ((pte_access & PT_USER_MASK) ? PFERR_RSVD_MASK : 0);

		pkru_bits &= mmu->pkru_mask >> offset;
		errcode |= -pkru_bits & PFERR_PK_MASK;
		fault |= (pkru_bits != 0);
	}

	return -(u32)fault & errcode;
}

bool kvm_mmu_may_ignore_guest_pat(void);

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
extern bool tdp_mmu_enabled;
#else
#define tdp_mmu_enabled false
#endif

static inline bool kvm_memslots_have_rmaps(struct kvm *kvm)
{
	return !tdp_mmu_enabled || kvm_shadow_root_allocated(kvm);
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

gpa_t translate_nested_gpa(struct kvm_vcpu *vcpu, gpa_t gpa, u64 access,
			   struct x86_exception *exception);

static inline gpa_t kvm_translate_gpa(struct kvm_vcpu *vcpu,
				      struct kvm_mmu *mmu,
				      gpa_t gpa, u64 access,
				      struct x86_exception *exception)
{
	if (mmu != &vcpu->arch.nested_mmu)
		return gpa;
	return translate_nested_gpa(vcpu, gpa, access, exception);
}
#endif
