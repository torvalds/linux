// SPDX-License-Identifier: GPL-2.0-only

#ifndef KVM_X86_MMU_SPTE_H
#define KVM_X86_MMU_SPTE_H

#include <asm/vmx.h>

#include "mmu.h"
#include "mmu_internal.h"

/*
 * A MMU present SPTE is backed by actual memory and may or may not be present
 * in hardware.  E.g. MMIO SPTEs are not considered present.  Use bit 11, as it
 * is ignored by all flavors of SPTEs and checking a low bit often generates
 * better code than for a high bit, e.g. 56+.  MMU present checks are pervasive
 * enough that the improved code generation is noticeable in KVM's footprint.
 */
#define SPTE_MMU_PRESENT_MASK		BIT_ULL(11)

/*
 * TDP SPTES (more specifically, EPT SPTEs) may not have A/D bits, and may also
 * be restricted to using write-protection (for L2 when CPU dirty logging, i.e.
 * PML, is enabled).  Use bits 52 and 53 to hold the type of A/D tracking that
 * is must be employed for a given TDP SPTE.
 *
 * Note, the "enabled" mask must be '0', as bits 62:52 are _reserved_ for PAE
 * paging, including NPT PAE.  This scheme works because legacy shadow paging
 * is guaranteed to have A/D bits and write-protection is forced only for
 * TDP with CPU dirty logging (PML).  If NPT ever gains PML-like support, it
 * must be restricted to 64-bit KVM.
 */
#define SPTE_TDP_AD_SHIFT		52
#define SPTE_TDP_AD_MASK		(3ULL << SPTE_TDP_AD_SHIFT)
#define SPTE_TDP_AD_ENABLED		(0ULL << SPTE_TDP_AD_SHIFT)
#define SPTE_TDP_AD_DISABLED		(1ULL << SPTE_TDP_AD_SHIFT)
#define SPTE_TDP_AD_WRPROT_ONLY		(2ULL << SPTE_TDP_AD_SHIFT)
static_assert(SPTE_TDP_AD_ENABLED == 0);

#ifdef CONFIG_DYNAMIC_PHYSICAL_MASK
#define SPTE_BASE_ADDR_MASK (physical_mask & ~(u64)(PAGE_SIZE-1))
#else
#define SPTE_BASE_ADDR_MASK (((1ULL << 52) - 1) & ~(u64)(PAGE_SIZE-1))
#endif

#define SPTE_PERM_MASK (PT_PRESENT_MASK | PT_WRITABLE_MASK | shadow_user_mask \
			| shadow_x_mask | shadow_nx_mask | shadow_me_mask)

#define ACC_EXEC_MASK    1
#define ACC_WRITE_MASK   PT_WRITABLE_MASK
#define ACC_USER_MASK    PT_USER_MASK
#define ACC_ALL          (ACC_EXEC_MASK | ACC_WRITE_MASK | ACC_USER_MASK)

/* The mask for the R/X bits in EPT PTEs */
#define SPTE_EPT_READABLE_MASK			0x1ull
#define SPTE_EPT_EXECUTABLE_MASK		0x4ull

#define SPTE_LEVEL_BITS			9
#define SPTE_LEVEL_SHIFT(level)		__PT_LEVEL_SHIFT(level, SPTE_LEVEL_BITS)
#define SPTE_INDEX(address, level)	__PT_INDEX(address, level, SPTE_LEVEL_BITS)
#define SPTE_ENT_PER_PAGE		__PT_ENT_PER_PAGE(SPTE_LEVEL_BITS)

/*
 * The mask/shift to use for saving the original R/X bits when marking the PTE
 * as not-present for access tracking purposes. We do not save the W bit as the
 * PTEs being access tracked also need to be dirty tracked, so the W bit will be
 * restored only when a write is attempted to the page.  This mask obviously
 * must not overlap the A/D type mask.
 */
#define SHADOW_ACC_TRACK_SAVED_BITS_MASK (SPTE_EPT_READABLE_MASK | \
					  SPTE_EPT_EXECUTABLE_MASK)
#define SHADOW_ACC_TRACK_SAVED_BITS_SHIFT 54
#define SHADOW_ACC_TRACK_SAVED_MASK	(SHADOW_ACC_TRACK_SAVED_BITS_MASK << \
					 SHADOW_ACC_TRACK_SAVED_BITS_SHIFT)
static_assert(!(SPTE_TDP_AD_MASK & SHADOW_ACC_TRACK_SAVED_MASK));

/*
 * {DEFAULT,EPT}_SPTE_{HOST,MMU}_WRITABLE are used to keep track of why a given
 * SPTE is write-protected. See is_writable_pte() for details.
 */

/* Bits 9 and 10 are ignored by all non-EPT PTEs. */
#define DEFAULT_SPTE_HOST_WRITABLE	BIT_ULL(9)
#define DEFAULT_SPTE_MMU_WRITABLE	BIT_ULL(10)

/*
 * Low ignored bits are at a premium for EPT, use high ignored bits, taking care
 * to not overlap the A/D type mask or the saved access bits of access-tracked
 * SPTEs when A/D bits are disabled.
 */
#define EPT_SPTE_HOST_WRITABLE		BIT_ULL(57)
#define EPT_SPTE_MMU_WRITABLE		BIT_ULL(58)

static_assert(!(EPT_SPTE_HOST_WRITABLE & SPTE_TDP_AD_MASK));
static_assert(!(EPT_SPTE_MMU_WRITABLE & SPTE_TDP_AD_MASK));
static_assert(!(EPT_SPTE_HOST_WRITABLE & SHADOW_ACC_TRACK_SAVED_MASK));
static_assert(!(EPT_SPTE_MMU_WRITABLE & SHADOW_ACC_TRACK_SAVED_MASK));

/* Defined only to keep the above static asserts readable. */
#undef SHADOW_ACC_TRACK_SAVED_MASK

/*
 * Due to limited space in PTEs, the MMIO generation is a 19 bit subset of
 * the memslots generation and is derived as follows:
 *
 * Bits 0-7 of the MMIO generation are propagated to spte bits 3-10
 * Bits 8-18 of the MMIO generation are propagated to spte bits 52-62
 *
 * The KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS flag is intentionally not included in
 * the MMIO generation number, as doing so would require stealing a bit from
 * the "real" generation number and thus effectively halve the maximum number
 * of MMIO generations that can be handled before encountering a wrap (which
 * requires a full MMU zap).  The flag is instead explicitly queried when
 * checking for MMIO spte cache hits.
 */

#define MMIO_SPTE_GEN_LOW_START		3
#define MMIO_SPTE_GEN_LOW_END		10

#define MMIO_SPTE_GEN_HIGH_START	52
#define MMIO_SPTE_GEN_HIGH_END		62

#define MMIO_SPTE_GEN_LOW_MASK		GENMASK_ULL(MMIO_SPTE_GEN_LOW_END, \
						    MMIO_SPTE_GEN_LOW_START)
#define MMIO_SPTE_GEN_HIGH_MASK		GENMASK_ULL(MMIO_SPTE_GEN_HIGH_END, \
						    MMIO_SPTE_GEN_HIGH_START)
static_assert(!(SPTE_MMU_PRESENT_MASK &
		(MMIO_SPTE_GEN_LOW_MASK | MMIO_SPTE_GEN_HIGH_MASK)));

/*
 * The SPTE MMIO mask must NOT overlap the MMIO generation bits or the
 * MMU-present bit.  The generation obviously co-exists with the magic MMIO
 * mask/value, and MMIO SPTEs are considered !MMU-present.
 *
 * The SPTE MMIO mask is allowed to use hardware "present" bits (i.e. all EPT
 * RWX bits), all physical address bits (legal PA bits are used for "fast" MMIO
 * and so they're off-limits for generation; additional checks ensure the mask
 * doesn't overlap legal PA bits), and bit 63 (carved out for future usage).
 */
#define SPTE_MMIO_ALLOWED_MASK (BIT_ULL(63) | GENMASK_ULL(51, 12) | GENMASK_ULL(2, 0))
static_assert(!(SPTE_MMIO_ALLOWED_MASK &
		(SPTE_MMU_PRESENT_MASK | MMIO_SPTE_GEN_LOW_MASK | MMIO_SPTE_GEN_HIGH_MASK)));

#define MMIO_SPTE_GEN_LOW_BITS		(MMIO_SPTE_GEN_LOW_END - MMIO_SPTE_GEN_LOW_START + 1)
#define MMIO_SPTE_GEN_HIGH_BITS		(MMIO_SPTE_GEN_HIGH_END - MMIO_SPTE_GEN_HIGH_START + 1)

/* remember to adjust the comment above as well if you change these */
static_assert(MMIO_SPTE_GEN_LOW_BITS == 8 && MMIO_SPTE_GEN_HIGH_BITS == 11);

#define MMIO_SPTE_GEN_LOW_SHIFT		(MMIO_SPTE_GEN_LOW_START - 0)
#define MMIO_SPTE_GEN_HIGH_SHIFT	(MMIO_SPTE_GEN_HIGH_START - MMIO_SPTE_GEN_LOW_BITS)

#define MMIO_SPTE_GEN_MASK		GENMASK_ULL(MMIO_SPTE_GEN_LOW_BITS + MMIO_SPTE_GEN_HIGH_BITS - 1, 0)

/*
 * Non-present SPTE value needs to set bit 63 for TDX, in order to suppress
 * #VE and get EPT violations on non-present PTEs.  We can use the
 * same value also without TDX for both VMX and SVM:
 *
 * For SVM NPT, for non-present spte (bit 0 = 0), other bits are ignored.
 * For VMX EPT, bit 63 is ignored if #VE is disabled. (EPT_VIOLATION_VE=0)
 *              bit 63 is #VE suppress if #VE is enabled. (EPT_VIOLATION_VE=1)
 */
#ifdef CONFIG_X86_64
#define SHADOW_NONPRESENT_VALUE	BIT_ULL(63)
static_assert(!(SHADOW_NONPRESENT_VALUE & SPTE_MMU_PRESENT_MASK));
#else
#define SHADOW_NONPRESENT_VALUE	0ULL
#endif


/*
 * True if A/D bits are supported in hardware and are enabled by KVM.  When
 * enabled, KVM uses A/D bits for all non-nested MMUs.  Because L1 can disable
 * A/D bits in EPTP12, SP and SPTE variants are needed to handle the scenario
 * where KVM is using A/D bits for L1, but not L2.
 */
extern bool __read_mostly kvm_ad_enabled;

extern u64 __read_mostly shadow_host_writable_mask;
extern u64 __read_mostly shadow_mmu_writable_mask;
extern u64 __read_mostly shadow_nx_mask;
extern u64 __read_mostly shadow_x_mask; /* mutual exclusive with nx_mask */
extern u64 __read_mostly shadow_user_mask;
extern u64 __read_mostly shadow_accessed_mask;
extern u64 __read_mostly shadow_dirty_mask;
extern u64 __read_mostly shadow_mmio_value;
extern u64 __read_mostly shadow_mmio_mask;
extern u64 __read_mostly shadow_mmio_access_mask;
extern u64 __read_mostly shadow_present_mask;
extern u64 __read_mostly shadow_memtype_mask;
extern u64 __read_mostly shadow_me_value;
extern u64 __read_mostly shadow_me_mask;

/*
 * SPTEs in MMUs without A/D bits are marked with SPTE_TDP_AD_DISABLED;
 * shadow_acc_track_mask is the set of bits to be cleared in non-accessed
 * pages.
 */
extern u64 __read_mostly shadow_acc_track_mask;

/*
 * This mask must be set on all non-zero Non-Present or Reserved SPTEs in order
 * to guard against L1TF attacks.
 */
extern u64 __read_mostly shadow_nonpresent_or_rsvd_mask;

/*
 * The number of high-order 1 bits to use in the mask above.
 */
#define SHADOW_NONPRESENT_OR_RSVD_MASK_LEN 5

/*
 * If a thread running without exclusive control of the MMU lock must perform a
 * multi-part operation on an SPTE, it can set the SPTE to FROZEN_SPTE as a
 * non-present intermediate value. Other threads which encounter this value
 * should not modify the SPTE.
 *
 * Use a semi-arbitrary value that doesn't set RWX bits, i.e. is not-present on
 * both AMD and Intel CPUs, and doesn't set PFN bits, i.e. doesn't create a L1TF
 * vulnerability.
 *
 * Only used by the TDP MMU.
 */
#define FROZEN_SPTE	(SHADOW_NONPRESENT_VALUE | 0x5a0ULL)

/* Frozen SPTEs must not be misconstrued as shadow present PTEs. */
static_assert(!(FROZEN_SPTE & SPTE_MMU_PRESENT_MASK));

static inline bool is_frozen_spte(u64 spte)
{
	return spte == FROZEN_SPTE;
}

/* Get an SPTE's index into its parent's page table (and the spt array). */
static inline int spte_index(u64 *sptep)
{
	return ((unsigned long)sptep / sizeof(*sptep)) & (SPTE_ENT_PER_PAGE - 1);
}

/*
 * In some cases, we need to preserve the GFN of a non-present or reserved
 * SPTE when we usurp the upper five bits of the physical address space to
 * defend against L1TF, e.g. for MMIO SPTEs.  To preserve the GFN, we'll
 * shift bits of the GFN that overlap with shadow_nonpresent_or_rsvd_mask
 * left into the reserved bits, i.e. the GFN in the SPTE will be split into
 * high and low parts.  This mask covers the lower bits of the GFN.
 */
extern u64 __read_mostly shadow_nonpresent_or_rsvd_lower_gfn_mask;

static inline struct kvm_mmu_page *to_shadow_page(hpa_t shadow_page)
{
	struct page *page = pfn_to_page((shadow_page) >> PAGE_SHIFT);

	return (struct kvm_mmu_page *)page_private(page);
}

static inline struct kvm_mmu_page *spte_to_child_sp(u64 spte)
{
	return to_shadow_page(spte & SPTE_BASE_ADDR_MASK);
}

static inline struct kvm_mmu_page *sptep_to_sp(u64 *sptep)
{
	return to_shadow_page(__pa(sptep));
}

static inline struct kvm_mmu_page *root_to_sp(hpa_t root)
{
	if (kvm_mmu_is_dummy_root(root))
		return NULL;

	/*
	 * The "root" may be a special root, e.g. a PAE entry, treat it as a
	 * SPTE to ensure any non-PA bits are dropped.
	 */
	return spte_to_child_sp(root);
}

static inline bool is_mmio_spte(struct kvm *kvm, u64 spte)
{
	return (spte & shadow_mmio_mask) == kvm->arch.shadow_mmio_value &&
	       likely(enable_mmio_caching);
}

static inline bool is_shadow_present_pte(u64 pte)
{
	return !!(pte & SPTE_MMU_PRESENT_MASK);
}

static inline bool is_ept_ve_possible(u64 spte)
{
	return (shadow_present_mask & VMX_EPT_SUPPRESS_VE_BIT) &&
	       !(spte & VMX_EPT_SUPPRESS_VE_BIT) &&
	       (spte & VMX_EPT_RWX_MASK) != VMX_EPT_MISCONFIG_WX_VALUE;
}

static inline bool sp_ad_disabled(struct kvm_mmu_page *sp)
{
	return sp->role.ad_disabled;
}

static inline bool spte_ad_enabled(u64 spte)
{
	KVM_MMU_WARN_ON(!is_shadow_present_pte(spte));
	return (spte & SPTE_TDP_AD_MASK) != SPTE_TDP_AD_DISABLED;
}

static inline bool spte_ad_need_write_protect(u64 spte)
{
	KVM_MMU_WARN_ON(!is_shadow_present_pte(spte));
	/*
	 * This is benign for non-TDP SPTEs as SPTE_TDP_AD_ENABLED is '0',
	 * and non-TDP SPTEs will never set these bits.  Optimize for 64-bit
	 * TDP and do the A/D type check unconditionally.
	 */
	return (spte & SPTE_TDP_AD_MASK) != SPTE_TDP_AD_ENABLED;
}

static inline bool is_access_track_spte(u64 spte)
{
	return !spte_ad_enabled(spte) && (spte & shadow_acc_track_mask) == 0;
}

static inline bool is_large_pte(u64 pte)
{
	return pte & PT_PAGE_SIZE_MASK;
}

static inline bool is_last_spte(u64 pte, int level)
{
	return (level == PG_LEVEL_4K) || is_large_pte(pte);
}

static inline bool is_executable_pte(u64 spte)
{
	return (spte & (shadow_x_mask | shadow_nx_mask)) == shadow_x_mask;
}

static inline kvm_pfn_t spte_to_pfn(u64 pte)
{
	return (pte & SPTE_BASE_ADDR_MASK) >> PAGE_SHIFT;
}

static inline bool is_accessed_spte(u64 spte)
{
	return spte & shadow_accessed_mask;
}

static inline u64 get_rsvd_bits(struct rsvd_bits_validate *rsvd_check, u64 pte,
				int level)
{
	int bit7 = (pte >> 7) & 1;

	return rsvd_check->rsvd_bits_mask[bit7][level-1];
}

static inline bool __is_rsvd_bits_set(struct rsvd_bits_validate *rsvd_check,
				      u64 pte, int level)
{
	return pte & get_rsvd_bits(rsvd_check, pte, level);
}

static inline bool __is_bad_mt_xwr(struct rsvd_bits_validate *rsvd_check,
				   u64 pte)
{
	return rsvd_check->bad_mt_xwr & BIT_ULL(pte & 0x3f);
}

static __always_inline bool is_rsvd_spte(struct rsvd_bits_validate *rsvd_check,
					 u64 spte, int level)
{
	return __is_bad_mt_xwr(rsvd_check, spte) ||
	       __is_rsvd_bits_set(rsvd_check, spte, level);
}

/*
 * A shadow-present leaf SPTE may be non-writable for 4 possible reasons:
 *
 *  1. To intercept writes for dirty logging. KVM write-protects huge pages
 *     so that they can be split down into the dirty logging
 *     granularity (4KiB) whenever the guest writes to them. KVM also
 *     write-protects 4KiB pages so that writes can be recorded in the dirty log
 *     (e.g. if not using PML). SPTEs are write-protected for dirty logging
 *     during the VM-iotcls that enable dirty logging.
 *
 *  2. To intercept writes to guest page tables that KVM is shadowing. When a
 *     guest writes to its page table the corresponding shadow page table will
 *     be marked "unsync". That way KVM knows which shadow page tables need to
 *     be updated on the next TLB flush, INVLPG, etc. and which do not.
 *
 *  3. To prevent guest writes to read-only memory, such as for memory in a
 *     read-only memslot or guest memory backed by a read-only VMA. Writes to
 *     such pages are disallowed entirely.
 *
 *  4. To emulate the Accessed bit for SPTEs without A/D bits.  Note, in this
 *     case, the SPTE is access-protected, not just write-protected!
 *
 * For cases #1 and #4, KVM can safely make such SPTEs writable without taking
 * mmu_lock as capturing the Accessed/Dirty state doesn't require taking it.
 * To differentiate #1 and #4 from #2 and #3, KVM uses two software-only bits
 * in the SPTE:
 *
 *  shadow_mmu_writable_mask, aka MMU-writable -
 *    Cleared on SPTEs that KVM is currently write-protecting for shadow paging
 *    purposes (case 2 above).
 *
 *  shadow_host_writable_mask, aka Host-writable -
 *    Cleared on SPTEs that are not host-writable (case 3 above)
 *
 * Note, not all possible combinations of PT_WRITABLE_MASK,
 * shadow_mmu_writable_mask, and shadow_host_writable_mask are valid. A given
 * SPTE can be in only one of the following states, which map to the
 * aforementioned 3 cases:
 *
 *   shadow_host_writable_mask | shadow_mmu_writable_mask | PT_WRITABLE_MASK
 *   ------------------------- | ------------------------ | ----------------
 *   1                         | 1                        | 1       (writable)
 *   1                         | 1                        | 0       (case 1)
 *   1                         | 0                        | 0       (case 2)
 *   0                         | 0                        | 0       (case 3)
 *
 * The valid combinations of these bits are checked by
 * check_spte_writable_invariants() whenever an SPTE is modified.
 *
 * Clearing the MMU-writable bit is always done under the MMU lock and always
 * accompanied by a TLB flush before dropping the lock to avoid corrupting the
 * shadow page tables between vCPUs. Write-protecting an SPTE for dirty logging
 * (which does not clear the MMU-writable bit), does not flush TLBs before
 * dropping the lock, as it only needs to synchronize guest writes with the
 * dirty bitmap. Similarly, making the SPTE inaccessible (and non-writable) for
 * access-tracking via the clear_young() MMU notifier also does not flush TLBs.
 *
 * So, there is the problem: clearing the MMU-writable bit can encounter a
 * write-protected SPTE while CPUs still have writable mappings for that SPTE
 * cached in their TLB. To address this, KVM always flushes TLBs when
 * write-protecting SPTEs if the MMU-writable bit is set on the old SPTE.
 *
 * The Host-writable bit is not modified on present SPTEs, it is only set or
 * cleared when an SPTE is first faulted in from non-present and then remains
 * immutable.
 */
static inline bool is_writable_pte(unsigned long pte)
{
	return pte & PT_WRITABLE_MASK;
}

/* Note: spte must be a shadow-present leaf SPTE. */
static inline void check_spte_writable_invariants(u64 spte)
{
	if (spte & shadow_mmu_writable_mask)
		WARN_ONCE(!(spte & shadow_host_writable_mask),
			  KBUILD_MODNAME ": MMU-writable SPTE is not Host-writable: %llx",
			  spte);
	else
		WARN_ONCE(is_writable_pte(spte),
			  KBUILD_MODNAME ": Writable SPTE is not MMU-writable: %llx", spte);
}

static inline bool is_mmu_writable_spte(u64 spte)
{
	return spte & shadow_mmu_writable_mask;
}

/*
 * If the MMU-writable flag is cleared, i.e. the SPTE is write-protected for
 * write-tracking, remote TLBs must be flushed, even if the SPTE was read-only,
 * as KVM allows stale Writable TLB entries to exist.  When dirty logging, KVM
 * flushes TLBs based on whether or not dirty bitmap/ring entries were reaped,
 * not whether or not SPTEs were modified, i.e. only the write-tracking case
 * needs to flush at the time the SPTEs is modified, before dropping mmu_lock.
 *
 * Don't flush if the Accessed bit is cleared, as access tracking tolerates
 * false negatives, e.g. KVM x86 omits TLB flushes even when aging SPTEs for a
 * mmu_notifier.clear_flush_young() event.
 *
 * Lastly, don't flush if the Dirty bit is cleared, as KVM unconditionally
 * flushes when enabling dirty logging (see kvm_mmu_slot_apply_flags()), and
 * when clearing dirty logs, KVM flushes based on whether or not dirty entries
 * were reaped from the bitmap/ring, not whether or not dirty SPTEs were found.
 *
 * Note, this logic only applies to shadow-present leaf SPTEs.  The caller is
 * responsible for checking that the old SPTE is shadow-present, and is also
 * responsible for determining whether or not a TLB flush is required when
 * modifying a shadow-present non-leaf SPTE.
 */
static inline bool leaf_spte_change_needs_tlb_flush(u64 old_spte, u64 new_spte)
{
	return is_mmu_writable_spte(old_spte) && !is_mmu_writable_spte(new_spte);
}

static inline u64 get_mmio_spte_generation(u64 spte)
{
	u64 gen;

	gen = (spte & MMIO_SPTE_GEN_LOW_MASK) >> MMIO_SPTE_GEN_LOW_SHIFT;
	gen |= (spte & MMIO_SPTE_GEN_HIGH_MASK) >> MMIO_SPTE_GEN_HIGH_SHIFT;
	return gen;
}

bool spte_has_volatile_bits(u64 spte);

bool make_spte(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
	       const struct kvm_memory_slot *slot,
	       unsigned int pte_access, gfn_t gfn, kvm_pfn_t pfn,
	       u64 old_spte, bool prefetch, bool synchronizing,
	       bool host_writable, u64 *new_spte);
u64 make_small_spte(struct kvm *kvm, u64 huge_spte,
		    union kvm_mmu_page_role role, int index);
u64 make_huge_spte(struct kvm *kvm, u64 small_spte, int level);
u64 make_nonleaf_spte(u64 *child_pt, bool ad_disabled);
u64 make_mmio_spte(struct kvm_vcpu *vcpu, u64 gfn, unsigned int access);
u64 mark_spte_for_access_track(u64 spte);

/* Restore an acc-track PTE back to a regular PTE */
static inline u64 restore_acc_track_spte(u64 spte)
{
	u64 saved_bits = (spte >> SHADOW_ACC_TRACK_SAVED_BITS_SHIFT)
			 & SHADOW_ACC_TRACK_SAVED_BITS_MASK;

	spte &= ~shadow_acc_track_mask;
	spte &= ~(SHADOW_ACC_TRACK_SAVED_BITS_MASK <<
		  SHADOW_ACC_TRACK_SAVED_BITS_SHIFT);
	spte |= saved_bits;

	return spte;
}

void __init kvm_mmu_spte_module_init(void);
void kvm_mmu_reset_all_pte_masks(void);

#endif
