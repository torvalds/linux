// SPDX-License-Identifier: GPL-2.0-only

#ifndef KVM_X86_MMU_SPTE_H
#define KVM_X86_MMU_SPTE_H

#include "mmu_internal.h"

#define PT_FIRST_AVAIL_BITS_SHIFT 10
#define PT64_SECOND_AVAIL_BITS_SHIFT 54

/*
 * The mask used to denote special SPTEs, which can be either MMIO SPTEs or
 * Access Tracking SPTEs.
 */
#define SPTE_SPECIAL_MASK (3ULL << 52)
#define SPTE_AD_ENABLED_MASK (0ULL << 52)
#define SPTE_AD_DISABLED_MASK (1ULL << 52)
#define SPTE_AD_WRPROT_ONLY_MASK (2ULL << 52)
#define SPTE_MMIO_MASK (3ULL << 52)

#ifdef CONFIG_DYNAMIC_PHYSICAL_MASK
#define PT64_BASE_ADDR_MASK (physical_mask & ~(u64)(PAGE_SIZE-1))
#else
#define PT64_BASE_ADDR_MASK (((1ULL << 52) - 1) & ~(u64)(PAGE_SIZE-1))
#endif
#define PT64_LVL_ADDR_MASK(level) \
	(PT64_BASE_ADDR_MASK & ~((1ULL << (PAGE_SHIFT + (((level) - 1) \
						* PT64_LEVEL_BITS))) - 1))
#define PT64_LVL_OFFSET_MASK(level) \
	(PT64_BASE_ADDR_MASK & ((1ULL << (PAGE_SHIFT + (((level) - 1) \
						* PT64_LEVEL_BITS))) - 1))

#define PT64_PERM_MASK (PT_PRESENT_MASK | PT_WRITABLE_MASK | shadow_user_mask \
			| shadow_x_mask | shadow_nx_mask | shadow_me_mask)

#define ACC_EXEC_MASK    1
#define ACC_WRITE_MASK   PT_WRITABLE_MASK
#define ACC_USER_MASK    PT_USER_MASK
#define ACC_ALL          (ACC_EXEC_MASK | ACC_WRITE_MASK | ACC_USER_MASK)

/* The mask for the R/X bits in EPT PTEs */
#define PT64_EPT_READABLE_MASK			0x1ull
#define PT64_EPT_EXECUTABLE_MASK		0x4ull

#define PT64_LEVEL_BITS 9

#define PT64_LEVEL_SHIFT(level) \
		(PAGE_SHIFT + (level - 1) * PT64_LEVEL_BITS)

#define PT64_INDEX(address, level)\
	(((address) >> PT64_LEVEL_SHIFT(level)) & ((1 << PT64_LEVEL_BITS) - 1))
#define SHADOW_PT_INDEX(addr, level) PT64_INDEX(addr, level)


#define SPTE_HOST_WRITEABLE	(1ULL << PT_FIRST_AVAIL_BITS_SHIFT)
#define SPTE_MMU_WRITEABLE	(1ULL << (PT_FIRST_AVAIL_BITS_SHIFT + 1))

/*
 * Due to limited space in PTEs, the MMIO generation is a 18 bit subset of
 * the memslots generation and is derived as follows:
 *
 * Bits 0-8 of the MMIO generation are propagated to spte bits 3-11
 * Bits 9-17 of the MMIO generation are propagated to spte bits 54-62
 *
 * The KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS flag is intentionally not included in
 * the MMIO generation number, as doing so would require stealing a bit from
 * the "real" generation number and thus effectively halve the maximum number
 * of MMIO generations that can be handled before encountering a wrap (which
 * requires a full MMU zap).  The flag is instead explicitly queried when
 * checking for MMIO spte cache hits.
 */

#define MMIO_SPTE_GEN_LOW_START		3
#define MMIO_SPTE_GEN_LOW_END		11

#define MMIO_SPTE_GEN_HIGH_START	PT64_SECOND_AVAIL_BITS_SHIFT
#define MMIO_SPTE_GEN_HIGH_END		62

#define MMIO_SPTE_GEN_LOW_MASK		GENMASK_ULL(MMIO_SPTE_GEN_LOW_END, \
						    MMIO_SPTE_GEN_LOW_START)
#define MMIO_SPTE_GEN_HIGH_MASK		GENMASK_ULL(MMIO_SPTE_GEN_HIGH_END, \
						    MMIO_SPTE_GEN_HIGH_START)

#define MMIO_SPTE_GEN_LOW_BITS		(MMIO_SPTE_GEN_LOW_END - MMIO_SPTE_GEN_LOW_START + 1)
#define MMIO_SPTE_GEN_HIGH_BITS		(MMIO_SPTE_GEN_HIGH_END - MMIO_SPTE_GEN_HIGH_START + 1)

/* remember to adjust the comment above as well if you change these */
static_assert(MMIO_SPTE_GEN_LOW_BITS == 9 && MMIO_SPTE_GEN_HIGH_BITS == 9);

#define MMIO_SPTE_GEN_LOW_SHIFT		(MMIO_SPTE_GEN_LOW_START - 0)
#define MMIO_SPTE_GEN_HIGH_SHIFT	(MMIO_SPTE_GEN_HIGH_START - MMIO_SPTE_GEN_LOW_BITS)

#define MMIO_SPTE_GEN_MASK		GENMASK_ULL(MMIO_SPTE_GEN_LOW_BITS + MMIO_SPTE_GEN_HIGH_BITS - 1, 0)

extern u64 __read_mostly shadow_nx_mask;
extern u64 __read_mostly shadow_x_mask; /* mutual exclusive with nx_mask */
extern u64 __read_mostly shadow_user_mask;
extern u64 __read_mostly shadow_accessed_mask;
extern u64 __read_mostly shadow_dirty_mask;
extern u64 __read_mostly shadow_mmio_value;
extern u64 __read_mostly shadow_mmio_access_mask;
extern u64 __read_mostly shadow_present_mask;
extern u64 __read_mostly shadow_me_mask;

/*
 * SPTEs used by MMUs without A/D bits are marked with SPTE_AD_DISABLED_MASK;
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
 * The mask/shift to use for saving the original R/X bits when marking the PTE
 * as not-present for access tracking purposes. We do not save the W bit as the
 * PTEs being access tracked also need to be dirty tracked, so the W bit will be
 * restored only when a write is attempted to the page.
 */
#define SHADOW_ACC_TRACK_SAVED_BITS_MASK (PT64_EPT_READABLE_MASK | \
					  PT64_EPT_EXECUTABLE_MASK)
#define SHADOW_ACC_TRACK_SAVED_BITS_SHIFT PT64_SECOND_AVAIL_BITS_SHIFT

/*
 * In some cases, we need to preserve the GFN of a non-present or reserved
 * SPTE when we usurp the upper five bits of the physical address space to
 * defend against L1TF, e.g. for MMIO SPTEs.  To preserve the GFN, we'll
 * shift bits of the GFN that overlap with shadow_nonpresent_or_rsvd_mask
 * left into the reserved bits, i.e. the GFN in the SPTE will be split into
 * high and low parts.  This mask covers the lower bits of the GFN.
 */
extern u64 __read_mostly shadow_nonpresent_or_rsvd_lower_gfn_mask;

/*
 * The number of non-reserved physical address bits irrespective of features
 * that repurpose legal bits, e.g. MKTME.
 */
extern u8 __read_mostly shadow_phys_bits;

static inline bool is_mmio_spte(u64 spte)
{
	return (spte & SPTE_SPECIAL_MASK) == SPTE_MMIO_MASK;
}

static inline bool sp_ad_disabled(struct kvm_mmu_page *sp)
{
	return sp->role.ad_disabled;
}

static inline bool spte_ad_enabled(u64 spte)
{
	MMU_WARN_ON(is_mmio_spte(spte));
	return (spte & SPTE_SPECIAL_MASK) != SPTE_AD_DISABLED_MASK;
}

static inline bool spte_ad_need_write_protect(u64 spte)
{
	MMU_WARN_ON(is_mmio_spte(spte));
	return (spte & SPTE_SPECIAL_MASK) != SPTE_AD_ENABLED_MASK;
}

static inline u64 spte_shadow_accessed_mask(u64 spte)
{
	MMU_WARN_ON(is_mmio_spte(spte));
	return spte_ad_enabled(spte) ? shadow_accessed_mask : 0;
}

static inline u64 spte_shadow_dirty_mask(u64 spte)
{
	MMU_WARN_ON(is_mmio_spte(spte));
	return spte_ad_enabled(spte) ? shadow_dirty_mask : 0;
}

static inline bool is_access_track_spte(u64 spte)
{
	return !spte_ad_enabled(spte) && (spte & shadow_acc_track_mask) == 0;
}

static inline int is_shadow_present_pte(u64 pte)
{
	return (pte != 0) && !is_mmio_spte(pte);
}

static inline int is_large_pte(u64 pte)
{
	return pte & PT_PAGE_SIZE_MASK;
}

static inline int is_last_spte(u64 pte, int level)
{
	if (level == PG_LEVEL_4K)
		return 1;
	if (is_large_pte(pte))
		return 1;
	return 0;
}

static inline bool is_executable_pte(u64 spte)
{
	return (spte & (shadow_x_mask | shadow_nx_mask)) == shadow_x_mask;
}

static inline kvm_pfn_t spte_to_pfn(u64 pte)
{
	return (pte & PT64_BASE_ADDR_MASK) >> PAGE_SHIFT;
}

static inline bool is_accessed_spte(u64 spte)
{
	u64 accessed_mask = spte_shadow_accessed_mask(spte);

	return accessed_mask ? spte & accessed_mask
			     : !is_access_track_spte(spte);
}

static inline bool is_dirty_spte(u64 spte)
{
	u64 dirty_mask = spte_shadow_dirty_mask(spte);

	return dirty_mask ? spte & dirty_mask : spte & PT_WRITABLE_MASK;
}

static inline bool spte_can_locklessly_be_made_writable(u64 spte)
{
	return (spte & (SPTE_HOST_WRITEABLE | SPTE_MMU_WRITEABLE)) ==
		(SPTE_HOST_WRITEABLE | SPTE_MMU_WRITEABLE);
}

static inline u64 get_mmio_spte_generation(u64 spte)
{
	u64 gen;

	gen = (spte & MMIO_SPTE_GEN_LOW_MASK) >> MMIO_SPTE_GEN_LOW_SHIFT;
	gen |= (spte & MMIO_SPTE_GEN_HIGH_MASK) >> MMIO_SPTE_GEN_HIGH_SHIFT;
	return gen;
}

/* Bits which may be returned by set_spte() */
#define SET_SPTE_WRITE_PROTECTED_PT    BIT(0)
#define SET_SPTE_NEED_REMOTE_TLB_FLUSH BIT(1)
#define SET_SPTE_SPURIOUS              BIT(2)

int make_spte(struct kvm_vcpu *vcpu, unsigned int pte_access, int level,
		     gfn_t gfn, kvm_pfn_t pfn, u64 old_spte, bool speculative,
		     bool can_unsync, bool host_writable, bool ad_disabled,
		     u64 *new_spte);
u64 make_nonleaf_spte(u64 *child_pt, bool ad_disabled);
u64 make_mmio_spte(struct kvm_vcpu *vcpu, u64 gfn, unsigned int access);
u64 mark_spte_for_access_track(u64 spte);
u64 kvm_mmu_changed_pte_notifier_make_spte(u64 old_spte, kvm_pfn_t new_pfn);

void kvm_mmu_reset_all_pte_masks(void);

#endif
