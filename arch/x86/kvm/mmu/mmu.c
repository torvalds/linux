// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * MMU support
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 */

#include "irq.h"
#include "ioapic.h"
#include "mmu.h"
#include "mmu_internal.h"
#include "x86.h"
#include "kvm_cache_regs.h"
#include "kvm_emulate.h"
#include "cpuid.h"

#include <linux/kvm_host.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/moduleparam.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/hugetlb.h>
#include <linux/compiler.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <linux/hash.h>
#include <linux/kern_levels.h>
#include <linux/kthread.h>

#include <asm/page.h>
#include <asm/memtype.h>
#include <asm/cmpxchg.h>
#include <asm/e820/api.h>
#include <asm/io.h>
#include <asm/vmx.h>
#include <asm/kvm_page_track.h>
#include "trace.h"

extern bool itlb_multihit_kvm_mitigation;

static int __read_mostly nx_huge_pages = -1;
#ifdef CONFIG_PREEMPT_RT
/* Recovery can cause latency spikes, disable it for PREEMPT_RT.  */
static uint __read_mostly nx_huge_pages_recovery_ratio = 0;
#else
static uint __read_mostly nx_huge_pages_recovery_ratio = 60;
#endif

static int set_nx_huge_pages(const char *val, const struct kernel_param *kp);
static int set_nx_huge_pages_recovery_ratio(const char *val, const struct kernel_param *kp);

static struct kernel_param_ops nx_huge_pages_ops = {
	.set = set_nx_huge_pages,
	.get = param_get_bool,
};

static struct kernel_param_ops nx_huge_pages_recovery_ratio_ops = {
	.set = set_nx_huge_pages_recovery_ratio,
	.get = param_get_uint,
};

module_param_cb(nx_huge_pages, &nx_huge_pages_ops, &nx_huge_pages, 0644);
__MODULE_PARM_TYPE(nx_huge_pages, "bool");
module_param_cb(nx_huge_pages_recovery_ratio, &nx_huge_pages_recovery_ratio_ops,
		&nx_huge_pages_recovery_ratio, 0644);
__MODULE_PARM_TYPE(nx_huge_pages_recovery_ratio, "uint");

static bool __read_mostly force_flush_and_sync_on_reuse;
module_param_named(flush_on_reuse, force_flush_and_sync_on_reuse, bool, 0644);

/*
 * When setting this variable to true it enables Two-Dimensional-Paging
 * where the hardware walks 2 page tables:
 * 1. the guest-virtual to guest-physical
 * 2. while doing 1. it walks guest-physical to host-physical
 * If the hardware supports that we don't need to do shadow paging.
 */
bool tdp_enabled = false;

static int max_huge_page_level __read_mostly;
static int max_tdp_level __read_mostly;

enum {
	AUDIT_PRE_PAGE_FAULT,
	AUDIT_POST_PAGE_FAULT,
	AUDIT_PRE_PTE_WRITE,
	AUDIT_POST_PTE_WRITE,
	AUDIT_PRE_SYNC,
	AUDIT_POST_SYNC
};

#undef MMU_DEBUG

#ifdef MMU_DEBUG
static bool dbg = 0;
module_param(dbg, bool, 0644);

#define pgprintk(x...) do { if (dbg) printk(x); } while (0)
#define rmap_printk(x...) do { if (dbg) printk(x); } while (0)
#define MMU_WARN_ON(x) WARN_ON(x)
#else
#define pgprintk(x...) do { } while (0)
#define rmap_printk(x...) do { } while (0)
#define MMU_WARN_ON(x) do { } while (0)
#endif

#define PTE_PREFETCH_NUM		8

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

#define PT64_LEVEL_BITS 9

#define PT64_LEVEL_SHIFT(level) \
		(PAGE_SHIFT + (level - 1) * PT64_LEVEL_BITS)

#define PT64_INDEX(address, level)\
	(((address) >> PT64_LEVEL_SHIFT(level)) & ((1 << PT64_LEVEL_BITS) - 1))


#define PT32_LEVEL_BITS 10

#define PT32_LEVEL_SHIFT(level) \
		(PAGE_SHIFT + (level - 1) * PT32_LEVEL_BITS)

#define PT32_LVL_OFFSET_MASK(level) \
	(PT32_BASE_ADDR_MASK & ((1ULL << (PAGE_SHIFT + (((level) - 1) \
						* PT32_LEVEL_BITS))) - 1))

#define PT32_INDEX(address, level)\
	(((address) >> PT32_LEVEL_SHIFT(level)) & ((1 << PT32_LEVEL_BITS) - 1))


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

#define PT32_BASE_ADDR_MASK PAGE_MASK
#define PT32_DIR_BASE_ADDR_MASK \
	(PAGE_MASK & ~((1ULL << (PAGE_SHIFT + PT32_LEVEL_BITS)) - 1))
#define PT32_LVL_ADDR_MASK(level) \
	(PAGE_MASK & ~((1ULL << (PAGE_SHIFT + (((level) - 1) \
					    * PT32_LEVEL_BITS))) - 1))

#define PT64_PERM_MASK (PT_PRESENT_MASK | PT_WRITABLE_MASK | shadow_user_mask \
			| shadow_x_mask | shadow_nx_mask | shadow_me_mask)

#define ACC_EXEC_MASK    1
#define ACC_WRITE_MASK   PT_WRITABLE_MASK
#define ACC_USER_MASK    PT_USER_MASK
#define ACC_ALL          (ACC_EXEC_MASK | ACC_WRITE_MASK | ACC_USER_MASK)

/* The mask for the R/X bits in EPT PTEs */
#define PT64_EPT_READABLE_MASK			0x1ull
#define PT64_EPT_EXECUTABLE_MASK		0x4ull

#include <trace/events/kvm.h>

#define SPTE_HOST_WRITEABLE	(1ULL << PT_FIRST_AVAIL_BITS_SHIFT)
#define SPTE_MMU_WRITEABLE	(1ULL << (PT_FIRST_AVAIL_BITS_SHIFT + 1))

#define SHADOW_PT_INDEX(addr, level) PT64_INDEX(addr, level)

/* make pte_list_desc fit well in cache line */
#define PTE_LIST_EXT 3

/*
 * Return values of handle_mmio_page_fault and mmu.page_fault:
 * RET_PF_RETRY: let CPU fault again on the address.
 * RET_PF_EMULATE: mmio page fault, emulate the instruction directly.
 *
 * For handle_mmio_page_fault only:
 * RET_PF_INVALID: the spte is invalid, let the real page fault path update it.
 */
enum {
	RET_PF_RETRY = 0,
	RET_PF_EMULATE = 1,
	RET_PF_INVALID = 2,
};

struct pte_list_desc {
	u64 *sptes[PTE_LIST_EXT];
	struct pte_list_desc *more;
};

struct kvm_shadow_walk_iterator {
	u64 addr;
	hpa_t shadow_addr;
	u64 *sptep;
	int level;
	unsigned index;
};

#define for_each_shadow_entry_using_root(_vcpu, _root, _addr, _walker)     \
	for (shadow_walk_init_using_root(&(_walker), (_vcpu),              \
					 (_root), (_addr));                \
	     shadow_walk_okay(&(_walker));			           \
	     shadow_walk_next(&(_walker)))

#define for_each_shadow_entry(_vcpu, _addr, _walker)            \
	for (shadow_walk_init(&(_walker), _vcpu, _addr);	\
	     shadow_walk_okay(&(_walker));			\
	     shadow_walk_next(&(_walker)))

#define for_each_shadow_entry_lockless(_vcpu, _addr, _walker, spte)	\
	for (shadow_walk_init(&(_walker), _vcpu, _addr);		\
	     shadow_walk_okay(&(_walker)) &&				\
		({ spte = mmu_spte_get_lockless(_walker.sptep); 1; });	\
	     __shadow_walk_next(&(_walker), spte))

static struct kmem_cache *pte_list_desc_cache;
static struct kmem_cache *mmu_page_header_cache;
static struct percpu_counter kvm_total_used_mmu_pages;

static u64 __read_mostly shadow_nx_mask;
static u64 __read_mostly shadow_x_mask;	/* mutual exclusive with nx_mask */
static u64 __read_mostly shadow_user_mask;
static u64 __read_mostly shadow_accessed_mask;
static u64 __read_mostly shadow_dirty_mask;
static u64 __read_mostly shadow_mmio_value;
static u64 __read_mostly shadow_mmio_access_mask;
static u64 __read_mostly shadow_present_mask;
static u64 __read_mostly shadow_me_mask;

/*
 * SPTEs used by MMUs without A/D bits are marked with SPTE_AD_DISABLED_MASK;
 * shadow_acc_track_mask is the set of bits to be cleared in non-accessed
 * pages.
 */
static u64 __read_mostly shadow_acc_track_mask;

/*
 * The mask/shift to use for saving the original R/X bits when marking the PTE
 * as not-present for access tracking purposes. We do not save the W bit as the
 * PTEs being access tracked also need to be dirty tracked, so the W bit will be
 * restored only when a write is attempted to the page.
 */
static const u64 shadow_acc_track_saved_bits_mask = PT64_EPT_READABLE_MASK |
						    PT64_EPT_EXECUTABLE_MASK;
static const u64 shadow_acc_track_saved_bits_shift = PT64_SECOND_AVAIL_BITS_SHIFT;

/*
 * This mask must be set on all non-zero Non-Present or Reserved SPTEs in order
 * to guard against L1TF attacks.
 */
static u64 __read_mostly shadow_nonpresent_or_rsvd_mask;

/*
 * The number of high-order 1 bits to use in the mask above.
 */
static const u64 shadow_nonpresent_or_rsvd_mask_len = 5;

/*
 * In some cases, we need to preserve the GFN of a non-present or reserved
 * SPTE when we usurp the upper five bits of the physical address space to
 * defend against L1TF, e.g. for MMIO SPTEs.  To preserve the GFN, we'll
 * shift bits of the GFN that overlap with shadow_nonpresent_or_rsvd_mask
 * left into the reserved bits, i.e. the GFN in the SPTE will be split into
 * high and low parts.  This mask covers the lower bits of the GFN.
 */
static u64 __read_mostly shadow_nonpresent_or_rsvd_lower_gfn_mask;

/*
 * The number of non-reserved physical address bits irrespective of features
 * that repurpose legal bits, e.g. MKTME.
 */
static u8 __read_mostly shadow_phys_bits;

static void mmu_spte_set(u64 *sptep, u64 spte);
static bool is_executable_pte(u64 spte);
static union kvm_mmu_page_role
kvm_mmu_calc_root_page_role(struct kvm_vcpu *vcpu);

#define CREATE_TRACE_POINTS
#include "mmutrace.h"


static inline bool kvm_available_flush_tlb_with_range(void)
{
	return kvm_x86_ops.tlb_remote_flush_with_range;
}

static void kvm_flush_remote_tlbs_with_range(struct kvm *kvm,
		struct kvm_tlb_range *range)
{
	int ret = -ENOTSUPP;

	if (range && kvm_x86_ops.tlb_remote_flush_with_range)
		ret = kvm_x86_ops.tlb_remote_flush_with_range(kvm, range);

	if (ret)
		kvm_flush_remote_tlbs(kvm);
}

static void kvm_flush_remote_tlbs_with_address(struct kvm *kvm,
		u64 start_gfn, u64 pages)
{
	struct kvm_tlb_range range;

	range.start_gfn = start_gfn;
	range.pages = pages;

	kvm_flush_remote_tlbs_with_range(kvm, &range);
}

void kvm_mmu_set_mmio_spte_mask(u64 mmio_value, u64 access_mask)
{
	BUG_ON((u64)(unsigned)access_mask != access_mask);
	WARN_ON(mmio_value & (shadow_nonpresent_or_rsvd_mask << shadow_nonpresent_or_rsvd_mask_len));
	WARN_ON(mmio_value & shadow_nonpresent_or_rsvd_lower_gfn_mask);
	shadow_mmio_value = mmio_value | SPTE_MMIO_MASK;
	shadow_mmio_access_mask = access_mask;
}
EXPORT_SYMBOL_GPL(kvm_mmu_set_mmio_spte_mask);

static bool is_mmio_spte(u64 spte)
{
	return (spte & SPTE_SPECIAL_MASK) == SPTE_MMIO_MASK;
}

static inline bool sp_ad_disabled(struct kvm_mmu_page *sp)
{
	return sp->role.ad_disabled;
}

static inline bool kvm_vcpu_ad_need_write_protect(struct kvm_vcpu *vcpu)
{
	/*
	 * When using the EPT page-modification log, the GPAs in the log
	 * would come from L2 rather than L1.  Therefore, we need to rely
	 * on write protection to record dirty pages.  This also bypasses
	 * PML, since writes now result in a vmexit.
	 */
	return vcpu->arch.mmu == &vcpu->arch.guest_mmu;
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

static bool is_nx_huge_page_enabled(void)
{
	return READ_ONCE(nx_huge_pages);
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

/*
 * Due to limited space in PTEs, the MMIO generation is a 19 bit subset of
 * the memslots generation and is derived as follows:
 *
 * Bits 0-8 of the MMIO generation are propagated to spte bits 3-11
 * Bits 9-18 of the MMIO generation are propagated to spte bits 52-61
 *
 * The KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS flag is intentionally not included in
 * the MMIO generation number, as doing so would require stealing a bit from
 * the "real" generation number and thus effectively halve the maximum number
 * of MMIO generations that can be handled before encountering a wrap (which
 * requires a full MMU zap).  The flag is instead explicitly queried when
 * checking for MMIO spte cache hits.
 */
#define MMIO_SPTE_GEN_MASK		GENMASK_ULL(17, 0)

#define MMIO_SPTE_GEN_LOW_START		3
#define MMIO_SPTE_GEN_LOW_END		11
#define MMIO_SPTE_GEN_LOW_MASK		GENMASK_ULL(MMIO_SPTE_GEN_LOW_END, \
						    MMIO_SPTE_GEN_LOW_START)

#define MMIO_SPTE_GEN_HIGH_START	PT64_SECOND_AVAIL_BITS_SHIFT
#define MMIO_SPTE_GEN_HIGH_END		62
#define MMIO_SPTE_GEN_HIGH_MASK		GENMASK_ULL(MMIO_SPTE_GEN_HIGH_END, \
						    MMIO_SPTE_GEN_HIGH_START)

static u64 generation_mmio_spte_mask(u64 gen)
{
	u64 mask;

	WARN_ON(gen & ~MMIO_SPTE_GEN_MASK);
	BUILD_BUG_ON((MMIO_SPTE_GEN_HIGH_MASK | MMIO_SPTE_GEN_LOW_MASK) & SPTE_SPECIAL_MASK);

	mask = (gen << MMIO_SPTE_GEN_LOW_START) & MMIO_SPTE_GEN_LOW_MASK;
	mask |= (gen << MMIO_SPTE_GEN_HIGH_START) & MMIO_SPTE_GEN_HIGH_MASK;
	return mask;
}

static u64 get_mmio_spte_generation(u64 spte)
{
	u64 gen;

	gen = (spte & MMIO_SPTE_GEN_LOW_MASK) >> MMIO_SPTE_GEN_LOW_START;
	gen |= (spte & MMIO_SPTE_GEN_HIGH_MASK) >> MMIO_SPTE_GEN_HIGH_START;
	return gen;
}

static u64 make_mmio_spte(struct kvm_vcpu *vcpu, u64 gfn, unsigned int access)
{

	u64 gen = kvm_vcpu_memslots(vcpu)->generation & MMIO_SPTE_GEN_MASK;
	u64 mask = generation_mmio_spte_mask(gen);
	u64 gpa = gfn << PAGE_SHIFT;

	access &= shadow_mmio_access_mask;
	mask |= shadow_mmio_value | access;
	mask |= gpa | shadow_nonpresent_or_rsvd_mask;
	mask |= (gpa & shadow_nonpresent_or_rsvd_mask)
		<< shadow_nonpresent_or_rsvd_mask_len;

	return mask;
}

static void mark_mmio_spte(struct kvm_vcpu *vcpu, u64 *sptep, u64 gfn,
			   unsigned int access)
{
	u64 mask = make_mmio_spte(vcpu, gfn, access);
	unsigned int gen = get_mmio_spte_generation(mask);

	access = mask & ACC_ALL;

	trace_mark_mmio_spte(sptep, gfn, access, gen);
	mmu_spte_set(sptep, mask);
}

static gfn_t get_mmio_spte_gfn(u64 spte)
{
	u64 gpa = spte & shadow_nonpresent_or_rsvd_lower_gfn_mask;

	gpa |= (spte >> shadow_nonpresent_or_rsvd_mask_len)
	       & shadow_nonpresent_or_rsvd_mask;

	return gpa >> PAGE_SHIFT;
}

static unsigned get_mmio_spte_access(u64 spte)
{
	return spte & shadow_mmio_access_mask;
}

static bool set_mmio_spte(struct kvm_vcpu *vcpu, u64 *sptep, gfn_t gfn,
			  kvm_pfn_t pfn, unsigned int access)
{
	if (unlikely(is_noslot_pfn(pfn))) {
		mark_mmio_spte(vcpu, sptep, gfn, access);
		return true;
	}

	return false;
}

static bool check_mmio_spte(struct kvm_vcpu *vcpu, u64 spte)
{
	u64 kvm_gen, spte_gen, gen;

	gen = kvm_vcpu_memslots(vcpu)->generation;
	if (unlikely(gen & KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS))
		return false;

	kvm_gen = gen & MMIO_SPTE_GEN_MASK;
	spte_gen = get_mmio_spte_generation(spte);

	trace_check_mmio_spte(spte, kvm_gen, spte_gen);
	return likely(kvm_gen == spte_gen);
}

static gpa_t translate_gpa(struct kvm_vcpu *vcpu, gpa_t gpa, u32 access,
                                  struct x86_exception *exception)
{
	/* Check if guest physical address doesn't exceed guest maximum */
	if (kvm_mmu_is_illegal_gpa(vcpu, gpa)) {
		exception->error_code |= PFERR_RSVD_MASK;
		return UNMAPPED_GVA;
	}

        return gpa;
}

/*
 * Sets the shadow PTE masks used by the MMU.
 *
 * Assumptions:
 *  - Setting either @accessed_mask or @dirty_mask requires setting both
 *  - At least one of @accessed_mask or @acc_track_mask must be set
 */
void kvm_mmu_set_mask_ptes(u64 user_mask, u64 accessed_mask,
		u64 dirty_mask, u64 nx_mask, u64 x_mask, u64 p_mask,
		u64 acc_track_mask, u64 me_mask)
{
	BUG_ON(!dirty_mask != !accessed_mask);
	BUG_ON(!accessed_mask && !acc_track_mask);
	BUG_ON(acc_track_mask & SPTE_SPECIAL_MASK);

	shadow_user_mask = user_mask;
	shadow_accessed_mask = accessed_mask;
	shadow_dirty_mask = dirty_mask;
	shadow_nx_mask = nx_mask;
	shadow_x_mask = x_mask;
	shadow_present_mask = p_mask;
	shadow_acc_track_mask = acc_track_mask;
	shadow_me_mask = me_mask;
}
EXPORT_SYMBOL_GPL(kvm_mmu_set_mask_ptes);

static u8 kvm_get_shadow_phys_bits(void)
{
	/*
	 * boot_cpu_data.x86_phys_bits is reduced when MKTME or SME are detected
	 * in CPU detection code, but the processor treats those reduced bits as
	 * 'keyID' thus they are not reserved bits. Therefore KVM needs to look at
	 * the physical address bits reported by CPUID.
	 */
	if (likely(boot_cpu_data.extended_cpuid_level >= 0x80000008))
		return cpuid_eax(0x80000008) & 0xff;

	/*
	 * Quite weird to have VMX or SVM but not MAXPHYADDR; probably a VM with
	 * custom CPUID.  Proceed with whatever the kernel found since these features
	 * aren't virtualizable (SME/SEV also require CPUIDs higher than 0x80000008).
	 */
	return boot_cpu_data.x86_phys_bits;
}

static void kvm_mmu_reset_all_pte_masks(void)
{
	u8 low_phys_bits;

	shadow_user_mask = 0;
	shadow_accessed_mask = 0;
	shadow_dirty_mask = 0;
	shadow_nx_mask = 0;
	shadow_x_mask = 0;
	shadow_present_mask = 0;
	shadow_acc_track_mask = 0;

	shadow_phys_bits = kvm_get_shadow_phys_bits();

	/*
	 * If the CPU has 46 or less physical address bits, then set an
	 * appropriate mask to guard against L1TF attacks. Otherwise, it is
	 * assumed that the CPU is not vulnerable to L1TF.
	 *
	 * Some Intel CPUs address the L1 cache using more PA bits than are
	 * reported by CPUID. Use the PA width of the L1 cache when possible
	 * to achieve more effective mitigation, e.g. if system RAM overlaps
	 * the most significant bits of legal physical address space.
	 */
	shadow_nonpresent_or_rsvd_mask = 0;
	low_phys_bits = boot_cpu_data.x86_phys_bits;
	if (boot_cpu_has_bug(X86_BUG_L1TF) &&
	    !WARN_ON_ONCE(boot_cpu_data.x86_cache_bits >=
			  52 - shadow_nonpresent_or_rsvd_mask_len)) {
		low_phys_bits = boot_cpu_data.x86_cache_bits
			- shadow_nonpresent_or_rsvd_mask_len;
		shadow_nonpresent_or_rsvd_mask =
			rsvd_bits(low_phys_bits, boot_cpu_data.x86_cache_bits - 1);
	}

	shadow_nonpresent_or_rsvd_lower_gfn_mask =
		GENMASK_ULL(low_phys_bits - 1, PAGE_SHIFT);
}

static int is_cpuid_PSE36(void)
{
	return 1;
}

static int is_nx(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.efer & EFER_NX;
}

static int is_shadow_present_pte(u64 pte)
{
	return (pte != 0) && !is_mmio_spte(pte);
}

static int is_large_pte(u64 pte)
{
	return pte & PT_PAGE_SIZE_MASK;
}

static int is_last_spte(u64 pte, int level)
{
	if (level == PG_LEVEL_4K)
		return 1;
	if (is_large_pte(pte))
		return 1;
	return 0;
}

static bool is_executable_pte(u64 spte)
{
	return (spte & (shadow_x_mask | shadow_nx_mask)) == shadow_x_mask;
}

static kvm_pfn_t spte_to_pfn(u64 pte)
{
	return (pte & PT64_BASE_ADDR_MASK) >> PAGE_SHIFT;
}

static gfn_t pse36_gfn_delta(u32 gpte)
{
	int shift = 32 - PT32_DIR_PSE36_SHIFT - PAGE_SHIFT;

	return (gpte & PT32_DIR_PSE36_MASK) << shift;
}

#ifdef CONFIG_X86_64
static void __set_spte(u64 *sptep, u64 spte)
{
	WRITE_ONCE(*sptep, spte);
}

static void __update_clear_spte_fast(u64 *sptep, u64 spte)
{
	WRITE_ONCE(*sptep, spte);
}

static u64 __update_clear_spte_slow(u64 *sptep, u64 spte)
{
	return xchg(sptep, spte);
}

static u64 __get_spte_lockless(u64 *sptep)
{
	return READ_ONCE(*sptep);
}
#else
union split_spte {
	struct {
		u32 spte_low;
		u32 spte_high;
	};
	u64 spte;
};

static void count_spte_clear(u64 *sptep, u64 spte)
{
	struct kvm_mmu_page *sp =  sptep_to_sp(sptep);

	if (is_shadow_present_pte(spte))
		return;

	/* Ensure the spte is completely set before we increase the count */
	smp_wmb();
	sp->clear_spte_count++;
}

static void __set_spte(u64 *sptep, u64 spte)
{
	union split_spte *ssptep, sspte;

	ssptep = (union split_spte *)sptep;
	sspte = (union split_spte)spte;

	ssptep->spte_high = sspte.spte_high;

	/*
	 * If we map the spte from nonpresent to present, We should store
	 * the high bits firstly, then set present bit, so cpu can not
	 * fetch this spte while we are setting the spte.
	 */
	smp_wmb();

	WRITE_ONCE(ssptep->spte_low, sspte.spte_low);
}

static void __update_clear_spte_fast(u64 *sptep, u64 spte)
{
	union split_spte *ssptep, sspte;

	ssptep = (union split_spte *)sptep;
	sspte = (union split_spte)spte;

	WRITE_ONCE(ssptep->spte_low, sspte.spte_low);

	/*
	 * If we map the spte from present to nonpresent, we should clear
	 * present bit firstly to avoid vcpu fetch the old high bits.
	 */
	smp_wmb();

	ssptep->spte_high = sspte.spte_high;
	count_spte_clear(sptep, spte);
}

static u64 __update_clear_spte_slow(u64 *sptep, u64 spte)
{
	union split_spte *ssptep, sspte, orig;

	ssptep = (union split_spte *)sptep;
	sspte = (union split_spte)spte;

	/* xchg acts as a barrier before the setting of the high bits */
	orig.spte_low = xchg(&ssptep->spte_low, sspte.spte_low);
	orig.spte_high = ssptep->spte_high;
	ssptep->spte_high = sspte.spte_high;
	count_spte_clear(sptep, spte);

	return orig.spte;
}

/*
 * The idea using the light way get the spte on x86_32 guest is from
 * gup_get_pte (mm/gup.c).
 *
 * An spte tlb flush may be pending, because kvm_set_pte_rmapp
 * coalesces them and we are running out of the MMU lock.  Therefore
 * we need to protect against in-progress updates of the spte.
 *
 * Reading the spte while an update is in progress may get the old value
 * for the high part of the spte.  The race is fine for a present->non-present
 * change (because the high part of the spte is ignored for non-present spte),
 * but for a present->present change we must reread the spte.
 *
 * All such changes are done in two steps (present->non-present and
 * non-present->present), hence it is enough to count the number of
 * present->non-present updates: if it changed while reading the spte,
 * we might have hit the race.  This is done using clear_spte_count.
 */
static u64 __get_spte_lockless(u64 *sptep)
{
	struct kvm_mmu_page *sp =  sptep_to_sp(sptep);
	union split_spte spte, *orig = (union split_spte *)sptep;
	int count;

retry:
	count = sp->clear_spte_count;
	smp_rmb();

	spte.spte_low = orig->spte_low;
	smp_rmb();

	spte.spte_high = orig->spte_high;
	smp_rmb();

	if (unlikely(spte.spte_low != orig->spte_low ||
	      count != sp->clear_spte_count))
		goto retry;

	return spte.spte;
}
#endif

static bool spte_can_locklessly_be_made_writable(u64 spte)
{
	return (spte & (SPTE_HOST_WRITEABLE | SPTE_MMU_WRITEABLE)) ==
		(SPTE_HOST_WRITEABLE | SPTE_MMU_WRITEABLE);
}

static bool spte_has_volatile_bits(u64 spte)
{
	if (!is_shadow_present_pte(spte))
		return false;

	/*
	 * Always atomically update spte if it can be updated
	 * out of mmu-lock, it can ensure dirty bit is not lost,
	 * also, it can help us to get a stable is_writable_pte()
	 * to ensure tlb flush is not missed.
	 */
	if (spte_can_locklessly_be_made_writable(spte) ||
	    is_access_track_spte(spte))
		return true;

	if (spte_ad_enabled(spte)) {
		if ((spte & shadow_accessed_mask) == 0 ||
	    	    (is_writable_pte(spte) && (spte & shadow_dirty_mask) == 0))
			return true;
	}

	return false;
}

static bool is_accessed_spte(u64 spte)
{
	u64 accessed_mask = spte_shadow_accessed_mask(spte);

	return accessed_mask ? spte & accessed_mask
			     : !is_access_track_spte(spte);
}

static bool is_dirty_spte(u64 spte)
{
	u64 dirty_mask = spte_shadow_dirty_mask(spte);

	return dirty_mask ? spte & dirty_mask : spte & PT_WRITABLE_MASK;
}

/* Rules for using mmu_spte_set:
 * Set the sptep from nonpresent to present.
 * Note: the sptep being assigned *must* be either not present
 * or in a state where the hardware will not attempt to update
 * the spte.
 */
static void mmu_spte_set(u64 *sptep, u64 new_spte)
{
	WARN_ON(is_shadow_present_pte(*sptep));
	__set_spte(sptep, new_spte);
}

/*
 * Update the SPTE (excluding the PFN), but do not track changes in its
 * accessed/dirty status.
 */
static u64 mmu_spte_update_no_track(u64 *sptep, u64 new_spte)
{
	u64 old_spte = *sptep;

	WARN_ON(!is_shadow_present_pte(new_spte));

	if (!is_shadow_present_pte(old_spte)) {
		mmu_spte_set(sptep, new_spte);
		return old_spte;
	}

	if (!spte_has_volatile_bits(old_spte))
		__update_clear_spte_fast(sptep, new_spte);
	else
		old_spte = __update_clear_spte_slow(sptep, new_spte);

	WARN_ON(spte_to_pfn(old_spte) != spte_to_pfn(new_spte));

	return old_spte;
}

/* Rules for using mmu_spte_update:
 * Update the state bits, it means the mapped pfn is not changed.
 *
 * Whenever we overwrite a writable spte with a read-only one we
 * should flush remote TLBs. Otherwise rmap_write_protect
 * will find a read-only spte, even though the writable spte
 * might be cached on a CPU's TLB, the return value indicates this
 * case.
 *
 * Returns true if the TLB needs to be flushed
 */
static bool mmu_spte_update(u64 *sptep, u64 new_spte)
{
	bool flush = false;
	u64 old_spte = mmu_spte_update_no_track(sptep, new_spte);

	if (!is_shadow_present_pte(old_spte))
		return false;

	/*
	 * For the spte updated out of mmu-lock is safe, since
	 * we always atomically update it, see the comments in
	 * spte_has_volatile_bits().
	 */
	if (spte_can_locklessly_be_made_writable(old_spte) &&
	      !is_writable_pte(new_spte))
		flush = true;

	/*
	 * Flush TLB when accessed/dirty states are changed in the page tables,
	 * to guarantee consistency between TLB and page tables.
	 */

	if (is_accessed_spte(old_spte) && !is_accessed_spte(new_spte)) {
		flush = true;
		kvm_set_pfn_accessed(spte_to_pfn(old_spte));
	}

	if (is_dirty_spte(old_spte) && !is_dirty_spte(new_spte)) {
		flush = true;
		kvm_set_pfn_dirty(spte_to_pfn(old_spte));
	}

	return flush;
}

/*
 * Rules for using mmu_spte_clear_track_bits:
 * It sets the sptep from present to nonpresent, and track the
 * state bits, it is used to clear the last level sptep.
 * Returns non-zero if the PTE was previously valid.
 */
static int mmu_spte_clear_track_bits(u64 *sptep)
{
	kvm_pfn_t pfn;
	u64 old_spte = *sptep;

	if (!spte_has_volatile_bits(old_spte))
		__update_clear_spte_fast(sptep, 0ull);
	else
		old_spte = __update_clear_spte_slow(sptep, 0ull);

	if (!is_shadow_present_pte(old_spte))
		return 0;

	pfn = spte_to_pfn(old_spte);

	/*
	 * KVM does not hold the refcount of the page used by
	 * kvm mmu, before reclaiming the page, we should
	 * unmap it from mmu first.
	 */
	WARN_ON(!kvm_is_reserved_pfn(pfn) && !page_count(pfn_to_page(pfn)));

	if (is_accessed_spte(old_spte))
		kvm_set_pfn_accessed(pfn);

	if (is_dirty_spte(old_spte))
		kvm_set_pfn_dirty(pfn);

	return 1;
}

/*
 * Rules for using mmu_spte_clear_no_track:
 * Directly clear spte without caring the state bits of sptep,
 * it is used to set the upper level spte.
 */
static void mmu_spte_clear_no_track(u64 *sptep)
{
	__update_clear_spte_fast(sptep, 0ull);
}

static u64 mmu_spte_get_lockless(u64 *sptep)
{
	return __get_spte_lockless(sptep);
}

static u64 mark_spte_for_access_track(u64 spte)
{
	if (spte_ad_enabled(spte))
		return spte & ~shadow_accessed_mask;

	if (is_access_track_spte(spte))
		return spte;

	/*
	 * Making an Access Tracking PTE will result in removal of write access
	 * from the PTE. So, verify that we will be able to restore the write
	 * access in the fast page fault path later on.
	 */
	WARN_ONCE((spte & PT_WRITABLE_MASK) &&
		  !spte_can_locklessly_be_made_writable(spte),
		  "kvm: Writable SPTE is not locklessly dirty-trackable\n");

	WARN_ONCE(spte & (shadow_acc_track_saved_bits_mask <<
			  shadow_acc_track_saved_bits_shift),
		  "kvm: Access Tracking saved bit locations are not zero\n");

	spte |= (spte & shadow_acc_track_saved_bits_mask) <<
		shadow_acc_track_saved_bits_shift;
	spte &= ~shadow_acc_track_mask;

	return spte;
}

/* Restore an acc-track PTE back to a regular PTE */
static u64 restore_acc_track_spte(u64 spte)
{
	u64 new_spte = spte;
	u64 saved_bits = (spte >> shadow_acc_track_saved_bits_shift)
			 & shadow_acc_track_saved_bits_mask;

	WARN_ON_ONCE(spte_ad_enabled(spte));
	WARN_ON_ONCE(!is_access_track_spte(spte));

	new_spte &= ~shadow_acc_track_mask;
	new_spte &= ~(shadow_acc_track_saved_bits_mask <<
		      shadow_acc_track_saved_bits_shift);
	new_spte |= saved_bits;

	return new_spte;
}

/* Returns the Accessed status of the PTE and resets it at the same time. */
static bool mmu_spte_age(u64 *sptep)
{
	u64 spte = mmu_spte_get_lockless(sptep);

	if (!is_accessed_spte(spte))
		return false;

	if (spte_ad_enabled(spte)) {
		clear_bit((ffs(shadow_accessed_mask) - 1),
			  (unsigned long *)sptep);
	} else {
		/*
		 * Capture the dirty status of the page, so that it doesn't get
		 * lost when the SPTE is marked for access tracking.
		 */
		if (is_writable_pte(spte))
			kvm_set_pfn_dirty(spte_to_pfn(spte));

		spte = mark_spte_for_access_track(spte);
		mmu_spte_update_no_track(sptep, spte);
	}

	return true;
}

static void walk_shadow_page_lockless_begin(struct kvm_vcpu *vcpu)
{
	/*
	 * Prevent page table teardown by making any free-er wait during
	 * kvm_flush_remote_tlbs() IPI to all active vcpus.
	 */
	local_irq_disable();

	/*
	 * Make sure a following spte read is not reordered ahead of the write
	 * to vcpu->mode.
	 */
	smp_store_mb(vcpu->mode, READING_SHADOW_PAGE_TABLES);
}

static void walk_shadow_page_lockless_end(struct kvm_vcpu *vcpu)
{
	/*
	 * Make sure the write to vcpu->mode is not reordered in front of
	 * reads to sptes.  If it does, kvm_mmu_commit_zap_page() can see us
	 * OUTSIDE_GUEST_MODE and proceed to free the shadow page table.
	 */
	smp_store_release(&vcpu->mode, OUTSIDE_GUEST_MODE);
	local_irq_enable();
}

static int mmu_topup_memory_caches(struct kvm_vcpu *vcpu, bool maybe_indirect)
{
	int r;

	/* 1 rmap, 1 parent PTE per level, and the prefetched rmaps. */
	r = kvm_mmu_topup_memory_cache(&vcpu->arch.mmu_pte_list_desc_cache,
				       1 + PT64_ROOT_MAX_LEVEL + PTE_PREFETCH_NUM);
	if (r)
		return r;
	r = kvm_mmu_topup_memory_cache(&vcpu->arch.mmu_shadow_page_cache,
				       PT64_ROOT_MAX_LEVEL);
	if (r)
		return r;
	if (maybe_indirect) {
		r = kvm_mmu_topup_memory_cache(&vcpu->arch.mmu_gfn_array_cache,
					       PT64_ROOT_MAX_LEVEL);
		if (r)
			return r;
	}
	return kvm_mmu_topup_memory_cache(&vcpu->arch.mmu_page_header_cache,
					  PT64_ROOT_MAX_LEVEL);
}

static void mmu_free_memory_caches(struct kvm_vcpu *vcpu)
{
	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_pte_list_desc_cache);
	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_shadow_page_cache);
	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_gfn_array_cache);
	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_page_header_cache);
}

static struct pte_list_desc *mmu_alloc_pte_list_desc(struct kvm_vcpu *vcpu)
{
	return kvm_mmu_memory_cache_alloc(&vcpu->arch.mmu_pte_list_desc_cache);
}

static void mmu_free_pte_list_desc(struct pte_list_desc *pte_list_desc)
{
	kmem_cache_free(pte_list_desc_cache, pte_list_desc);
}

static gfn_t kvm_mmu_page_get_gfn(struct kvm_mmu_page *sp, int index)
{
	if (!sp->role.direct)
		return sp->gfns[index];

	return sp->gfn + (index << ((sp->role.level - 1) * PT64_LEVEL_BITS));
}

static void kvm_mmu_page_set_gfn(struct kvm_mmu_page *sp, int index, gfn_t gfn)
{
	if (!sp->role.direct) {
		sp->gfns[index] = gfn;
		return;
	}

	if (WARN_ON(gfn != kvm_mmu_page_get_gfn(sp, index)))
		pr_err_ratelimited("gfn mismatch under direct page %llx "
				   "(expected %llx, got %llx)\n",
				   sp->gfn,
				   kvm_mmu_page_get_gfn(sp, index), gfn);
}

/*
 * Return the pointer to the large page information for a given gfn,
 * handling slots that are not large page aligned.
 */
static struct kvm_lpage_info *lpage_info_slot(gfn_t gfn,
					      struct kvm_memory_slot *slot,
					      int level)
{
	unsigned long idx;

	idx = gfn_to_index(gfn, slot->base_gfn, level);
	return &slot->arch.lpage_info[level - 2][idx];
}

static void update_gfn_disallow_lpage_count(struct kvm_memory_slot *slot,
					    gfn_t gfn, int count)
{
	struct kvm_lpage_info *linfo;
	int i;

	for (i = PG_LEVEL_2M; i <= KVM_MAX_HUGEPAGE_LEVEL; ++i) {
		linfo = lpage_info_slot(gfn, slot, i);
		linfo->disallow_lpage += count;
		WARN_ON(linfo->disallow_lpage < 0);
	}
}

void kvm_mmu_gfn_disallow_lpage(struct kvm_memory_slot *slot, gfn_t gfn)
{
	update_gfn_disallow_lpage_count(slot, gfn, 1);
}

void kvm_mmu_gfn_allow_lpage(struct kvm_memory_slot *slot, gfn_t gfn)
{
	update_gfn_disallow_lpage_count(slot, gfn, -1);
}

static void account_shadowed(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;
	gfn_t gfn;

	kvm->arch.indirect_shadow_pages++;
	gfn = sp->gfn;
	slots = kvm_memslots_for_spte_role(kvm, sp->role);
	slot = __gfn_to_memslot(slots, gfn);

	/* the non-leaf shadow pages are keeping readonly. */
	if (sp->role.level > PG_LEVEL_4K)
		return kvm_slot_page_track_add_page(kvm, slot, gfn,
						    KVM_PAGE_TRACK_WRITE);

	kvm_mmu_gfn_disallow_lpage(slot, gfn);
}

static void account_huge_nx_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	if (sp->lpage_disallowed)
		return;

	++kvm->stat.nx_lpage_splits;
	list_add_tail(&sp->lpage_disallowed_link,
		      &kvm->arch.lpage_disallowed_mmu_pages);
	sp->lpage_disallowed = true;
}

static void unaccount_shadowed(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;
	gfn_t gfn;

	kvm->arch.indirect_shadow_pages--;
	gfn = sp->gfn;
	slots = kvm_memslots_for_spte_role(kvm, sp->role);
	slot = __gfn_to_memslot(slots, gfn);
	if (sp->role.level > PG_LEVEL_4K)
		return kvm_slot_page_track_remove_page(kvm, slot, gfn,
						       KVM_PAGE_TRACK_WRITE);

	kvm_mmu_gfn_allow_lpage(slot, gfn);
}

static void unaccount_huge_nx_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	--kvm->stat.nx_lpage_splits;
	sp->lpage_disallowed = false;
	list_del(&sp->lpage_disallowed_link);
}

static struct kvm_memory_slot *
gfn_to_memslot_dirty_bitmap(struct kvm_vcpu *vcpu, gfn_t gfn,
			    bool no_dirty_log)
{
	struct kvm_memory_slot *slot;

	slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	if (!slot || slot->flags & KVM_MEMSLOT_INVALID)
		return NULL;
	if (no_dirty_log && slot->dirty_bitmap)
		return NULL;

	return slot;
}

/*
 * About rmap_head encoding:
 *
 * If the bit zero of rmap_head->val is clear, then it points to the only spte
 * in this rmap chain. Otherwise, (rmap_head->val & ~1) points to a struct
 * pte_list_desc containing more mappings.
 */

/*
 * Returns the number of pointers in the rmap chain, not counting the new one.
 */
static int pte_list_add(struct kvm_vcpu *vcpu, u64 *spte,
			struct kvm_rmap_head *rmap_head)
{
	struct pte_list_desc *desc;
	int i, count = 0;

	if (!rmap_head->val) {
		rmap_printk("pte_list_add: %p %llx 0->1\n", spte, *spte);
		rmap_head->val = (unsigned long)spte;
	} else if (!(rmap_head->val & 1)) {
		rmap_printk("pte_list_add: %p %llx 1->many\n", spte, *spte);
		desc = mmu_alloc_pte_list_desc(vcpu);
		desc->sptes[0] = (u64 *)rmap_head->val;
		desc->sptes[1] = spte;
		rmap_head->val = (unsigned long)desc | 1;
		++count;
	} else {
		rmap_printk("pte_list_add: %p %llx many->many\n", spte, *spte);
		desc = (struct pte_list_desc *)(rmap_head->val & ~1ul);
		while (desc->sptes[PTE_LIST_EXT-1] && desc->more) {
			desc = desc->more;
			count += PTE_LIST_EXT;
		}
		if (desc->sptes[PTE_LIST_EXT-1]) {
			desc->more = mmu_alloc_pte_list_desc(vcpu);
			desc = desc->more;
		}
		for (i = 0; desc->sptes[i]; ++i)
			++count;
		desc->sptes[i] = spte;
	}
	return count;
}

static void
pte_list_desc_remove_entry(struct kvm_rmap_head *rmap_head,
			   struct pte_list_desc *desc, int i,
			   struct pte_list_desc *prev_desc)
{
	int j;

	for (j = PTE_LIST_EXT - 1; !desc->sptes[j] && j > i; --j)
		;
	desc->sptes[i] = desc->sptes[j];
	desc->sptes[j] = NULL;
	if (j != 0)
		return;
	if (!prev_desc && !desc->more)
		rmap_head->val = 0;
	else
		if (prev_desc)
			prev_desc->more = desc->more;
		else
			rmap_head->val = (unsigned long)desc->more | 1;
	mmu_free_pte_list_desc(desc);
}

static void __pte_list_remove(u64 *spte, struct kvm_rmap_head *rmap_head)
{
	struct pte_list_desc *desc;
	struct pte_list_desc *prev_desc;
	int i;

	if (!rmap_head->val) {
		pr_err("%s: %p 0->BUG\n", __func__, spte);
		BUG();
	} else if (!(rmap_head->val & 1)) {
		rmap_printk("%s:  %p 1->0\n", __func__, spte);
		if ((u64 *)rmap_head->val != spte) {
			pr_err("%s:  %p 1->BUG\n", __func__, spte);
			BUG();
		}
		rmap_head->val = 0;
	} else {
		rmap_printk("%s:  %p many->many\n", __func__, spte);
		desc = (struct pte_list_desc *)(rmap_head->val & ~1ul);
		prev_desc = NULL;
		while (desc) {
			for (i = 0; i < PTE_LIST_EXT && desc->sptes[i]; ++i) {
				if (desc->sptes[i] == spte) {
					pte_list_desc_remove_entry(rmap_head,
							desc, i, prev_desc);
					return;
				}
			}
			prev_desc = desc;
			desc = desc->more;
		}
		pr_err("%s: %p many->many\n", __func__, spte);
		BUG();
	}
}

static void pte_list_remove(struct kvm_rmap_head *rmap_head, u64 *sptep)
{
	mmu_spte_clear_track_bits(sptep);
	__pte_list_remove(sptep, rmap_head);
}

static struct kvm_rmap_head *__gfn_to_rmap(gfn_t gfn, int level,
					   struct kvm_memory_slot *slot)
{
	unsigned long idx;

	idx = gfn_to_index(gfn, slot->base_gfn, level);
	return &slot->arch.rmap[level - PG_LEVEL_4K][idx];
}

static struct kvm_rmap_head *gfn_to_rmap(struct kvm *kvm, gfn_t gfn,
					 struct kvm_mmu_page *sp)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;

	slots = kvm_memslots_for_spte_role(kvm, sp->role);
	slot = __gfn_to_memslot(slots, gfn);
	return __gfn_to_rmap(gfn, sp->role.level, slot);
}

static bool rmap_can_add(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_memory_cache *mc;

	mc = &vcpu->arch.mmu_pte_list_desc_cache;
	return kvm_mmu_memory_cache_nr_free_objects(mc);
}

static int rmap_add(struct kvm_vcpu *vcpu, u64 *spte, gfn_t gfn)
{
	struct kvm_mmu_page *sp;
	struct kvm_rmap_head *rmap_head;

	sp = sptep_to_sp(spte);
	kvm_mmu_page_set_gfn(sp, spte - sp->spt, gfn);
	rmap_head = gfn_to_rmap(vcpu->kvm, gfn, sp);
	return pte_list_add(vcpu, spte, rmap_head);
}

static void rmap_remove(struct kvm *kvm, u64 *spte)
{
	struct kvm_mmu_page *sp;
	gfn_t gfn;
	struct kvm_rmap_head *rmap_head;

	sp = sptep_to_sp(spte);
	gfn = kvm_mmu_page_get_gfn(sp, spte - sp->spt);
	rmap_head = gfn_to_rmap(kvm, gfn, sp);
	__pte_list_remove(spte, rmap_head);
}

/*
 * Used by the following functions to iterate through the sptes linked by a
 * rmap.  All fields are private and not assumed to be used outside.
 */
struct rmap_iterator {
	/* private fields */
	struct pte_list_desc *desc;	/* holds the sptep if not NULL */
	int pos;			/* index of the sptep */
};

/*
 * Iteration must be started by this function.  This should also be used after
 * removing/dropping sptes from the rmap link because in such cases the
 * information in the iterator may not be valid.
 *
 * Returns sptep if found, NULL otherwise.
 */
static u64 *rmap_get_first(struct kvm_rmap_head *rmap_head,
			   struct rmap_iterator *iter)
{
	u64 *sptep;

	if (!rmap_head->val)
		return NULL;

	if (!(rmap_head->val & 1)) {
		iter->desc = NULL;
		sptep = (u64 *)rmap_head->val;
		goto out;
	}

	iter->desc = (struct pte_list_desc *)(rmap_head->val & ~1ul);
	iter->pos = 0;
	sptep = iter->desc->sptes[iter->pos];
out:
	BUG_ON(!is_shadow_present_pte(*sptep));
	return sptep;
}

/*
 * Must be used with a valid iterator: e.g. after rmap_get_first().
 *
 * Returns sptep if found, NULL otherwise.
 */
static u64 *rmap_get_next(struct rmap_iterator *iter)
{
	u64 *sptep;

	if (iter->desc) {
		if (iter->pos < PTE_LIST_EXT - 1) {
			++iter->pos;
			sptep = iter->desc->sptes[iter->pos];
			if (sptep)
				goto out;
		}

		iter->desc = iter->desc->more;

		if (iter->desc) {
			iter->pos = 0;
			/* desc->sptes[0] cannot be NULL */
			sptep = iter->desc->sptes[iter->pos];
			goto out;
		}
	}

	return NULL;
out:
	BUG_ON(!is_shadow_present_pte(*sptep));
	return sptep;
}

#define for_each_rmap_spte(_rmap_head_, _iter_, _spte_)			\
	for (_spte_ = rmap_get_first(_rmap_head_, _iter_);		\
	     _spte_; _spte_ = rmap_get_next(_iter_))

static void drop_spte(struct kvm *kvm, u64 *sptep)
{
	if (mmu_spte_clear_track_bits(sptep))
		rmap_remove(kvm, sptep);
}


static bool __drop_large_spte(struct kvm *kvm, u64 *sptep)
{
	if (is_large_pte(*sptep)) {
		WARN_ON(sptep_to_sp(sptep)->role.level == PG_LEVEL_4K);
		drop_spte(kvm, sptep);
		--kvm->stat.lpages;
		return true;
	}

	return false;
}

static void drop_large_spte(struct kvm_vcpu *vcpu, u64 *sptep)
{
	if (__drop_large_spte(vcpu->kvm, sptep)) {
		struct kvm_mmu_page *sp = sptep_to_sp(sptep);

		kvm_flush_remote_tlbs_with_address(vcpu->kvm, sp->gfn,
			KVM_PAGES_PER_HPAGE(sp->role.level));
	}
}

/*
 * Write-protect on the specified @sptep, @pt_protect indicates whether
 * spte write-protection is caused by protecting shadow page table.
 *
 * Note: write protection is difference between dirty logging and spte
 * protection:
 * - for dirty logging, the spte can be set to writable at anytime if
 *   its dirty bitmap is properly set.
 * - for spte protection, the spte can be writable only after unsync-ing
 *   shadow page.
 *
 * Return true if tlb need be flushed.
 */
static bool spte_write_protect(u64 *sptep, bool pt_protect)
{
	u64 spte = *sptep;

	if (!is_writable_pte(spte) &&
	      !(pt_protect && spte_can_locklessly_be_made_writable(spte)))
		return false;

	rmap_printk("rmap_write_protect: spte %p %llx\n", sptep, *sptep);

	if (pt_protect)
		spte &= ~SPTE_MMU_WRITEABLE;
	spte = spte & ~PT_WRITABLE_MASK;

	return mmu_spte_update(sptep, spte);
}

static bool __rmap_write_protect(struct kvm *kvm,
				 struct kvm_rmap_head *rmap_head,
				 bool pt_protect)
{
	u64 *sptep;
	struct rmap_iterator iter;
	bool flush = false;

	for_each_rmap_spte(rmap_head, &iter, sptep)
		flush |= spte_write_protect(sptep, pt_protect);

	return flush;
}

static bool spte_clear_dirty(u64 *sptep)
{
	u64 spte = *sptep;

	rmap_printk("rmap_clear_dirty: spte %p %llx\n", sptep, *sptep);

	MMU_WARN_ON(!spte_ad_enabled(spte));
	spte &= ~shadow_dirty_mask;
	return mmu_spte_update(sptep, spte);
}

static bool spte_wrprot_for_clear_dirty(u64 *sptep)
{
	bool was_writable = test_and_clear_bit(PT_WRITABLE_SHIFT,
					       (unsigned long *)sptep);
	if (was_writable && !spte_ad_enabled(*sptep))
		kvm_set_pfn_dirty(spte_to_pfn(*sptep));

	return was_writable;
}

/*
 * Gets the GFN ready for another round of dirty logging by clearing the
 *	- D bit on ad-enabled SPTEs, and
 *	- W bit on ad-disabled SPTEs.
 * Returns true iff any D or W bits were cleared.
 */
static bool __rmap_clear_dirty(struct kvm *kvm, struct kvm_rmap_head *rmap_head)
{
	u64 *sptep;
	struct rmap_iterator iter;
	bool flush = false;

	for_each_rmap_spte(rmap_head, &iter, sptep)
		if (spte_ad_need_write_protect(*sptep))
			flush |= spte_wrprot_for_clear_dirty(sptep);
		else
			flush |= spte_clear_dirty(sptep);

	return flush;
}

static bool spte_set_dirty(u64 *sptep)
{
	u64 spte = *sptep;

	rmap_printk("rmap_set_dirty: spte %p %llx\n", sptep, *sptep);

	/*
	 * Similar to the !kvm_x86_ops.slot_disable_log_dirty case,
	 * do not bother adding back write access to pages marked
	 * SPTE_AD_WRPROT_ONLY_MASK.
	 */
	spte |= shadow_dirty_mask;

	return mmu_spte_update(sptep, spte);
}

static bool __rmap_set_dirty(struct kvm *kvm, struct kvm_rmap_head *rmap_head)
{
	u64 *sptep;
	struct rmap_iterator iter;
	bool flush = false;

	for_each_rmap_spte(rmap_head, &iter, sptep)
		if (spte_ad_enabled(*sptep))
			flush |= spte_set_dirty(sptep);

	return flush;
}

/**
 * kvm_mmu_write_protect_pt_masked - write protect selected PT level pages
 * @kvm: kvm instance
 * @slot: slot to protect
 * @gfn_offset: start of the BITS_PER_LONG pages we care about
 * @mask: indicates which pages we should protect
 *
 * Used when we do not need to care about huge page mappings: e.g. during dirty
 * logging we do not have any such mappings.
 */
static void kvm_mmu_write_protect_pt_masked(struct kvm *kvm,
				     struct kvm_memory_slot *slot,
				     gfn_t gfn_offset, unsigned long mask)
{
	struct kvm_rmap_head *rmap_head;

	while (mask) {
		rmap_head = __gfn_to_rmap(slot->base_gfn + gfn_offset + __ffs(mask),
					  PG_LEVEL_4K, slot);
		__rmap_write_protect(kvm, rmap_head, false);

		/* clear the first set bit */
		mask &= mask - 1;
	}
}

/**
 * kvm_mmu_clear_dirty_pt_masked - clear MMU D-bit for PT level pages, or write
 * protect the page if the D-bit isn't supported.
 * @kvm: kvm instance
 * @slot: slot to clear D-bit
 * @gfn_offset: start of the BITS_PER_LONG pages we care about
 * @mask: indicates which pages we should clear D-bit
 *
 * Used for PML to re-log the dirty GPAs after userspace querying dirty_bitmap.
 */
void kvm_mmu_clear_dirty_pt_masked(struct kvm *kvm,
				     struct kvm_memory_slot *slot,
				     gfn_t gfn_offset, unsigned long mask)
{
	struct kvm_rmap_head *rmap_head;

	while (mask) {
		rmap_head = __gfn_to_rmap(slot->base_gfn + gfn_offset + __ffs(mask),
					  PG_LEVEL_4K, slot);
		__rmap_clear_dirty(kvm, rmap_head);

		/* clear the first set bit */
		mask &= mask - 1;
	}
}
EXPORT_SYMBOL_GPL(kvm_mmu_clear_dirty_pt_masked);

/**
 * kvm_arch_mmu_enable_log_dirty_pt_masked - enable dirty logging for selected
 * PT level pages.
 *
 * It calls kvm_mmu_write_protect_pt_masked to write protect selected pages to
 * enable dirty logging for them.
 *
 * Used when we do not need to care about huge page mappings: e.g. during dirty
 * logging we do not have any such mappings.
 */
void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *kvm,
				struct kvm_memory_slot *slot,
				gfn_t gfn_offset, unsigned long mask)
{
	if (kvm_x86_ops.enable_log_dirty_pt_masked)
		kvm_x86_ops.enable_log_dirty_pt_masked(kvm, slot, gfn_offset,
				mask);
	else
		kvm_mmu_write_protect_pt_masked(kvm, slot, gfn_offset, mask);
}

bool kvm_mmu_slot_gfn_write_protect(struct kvm *kvm,
				    struct kvm_memory_slot *slot, u64 gfn)
{
	struct kvm_rmap_head *rmap_head;
	int i;
	bool write_protected = false;

	for (i = PG_LEVEL_4K; i <= KVM_MAX_HUGEPAGE_LEVEL; ++i) {
		rmap_head = __gfn_to_rmap(gfn, i, slot);
		write_protected |= __rmap_write_protect(kvm, rmap_head, true);
	}

	return write_protected;
}

static bool rmap_write_protect(struct kvm_vcpu *vcpu, u64 gfn)
{
	struct kvm_memory_slot *slot;

	slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	return kvm_mmu_slot_gfn_write_protect(vcpu->kvm, slot, gfn);
}

static bool kvm_zap_rmapp(struct kvm *kvm, struct kvm_rmap_head *rmap_head)
{
	u64 *sptep;
	struct rmap_iterator iter;
	bool flush = false;

	while ((sptep = rmap_get_first(rmap_head, &iter))) {
		rmap_printk("%s: spte %p %llx.\n", __func__, sptep, *sptep);

		pte_list_remove(rmap_head, sptep);
		flush = true;
	}

	return flush;
}

static int kvm_unmap_rmapp(struct kvm *kvm, struct kvm_rmap_head *rmap_head,
			   struct kvm_memory_slot *slot, gfn_t gfn, int level,
			   unsigned long data)
{
	return kvm_zap_rmapp(kvm, rmap_head);
}

static int kvm_set_pte_rmapp(struct kvm *kvm, struct kvm_rmap_head *rmap_head,
			     struct kvm_memory_slot *slot, gfn_t gfn, int level,
			     unsigned long data)
{
	u64 *sptep;
	struct rmap_iterator iter;
	int need_flush = 0;
	u64 new_spte;
	pte_t *ptep = (pte_t *)data;
	kvm_pfn_t new_pfn;

	WARN_ON(pte_huge(*ptep));
	new_pfn = pte_pfn(*ptep);

restart:
	for_each_rmap_spte(rmap_head, &iter, sptep) {
		rmap_printk("kvm_set_pte_rmapp: spte %p %llx gfn %llx (%d)\n",
			    sptep, *sptep, gfn, level);

		need_flush = 1;

		if (pte_write(*ptep)) {
			pte_list_remove(rmap_head, sptep);
			goto restart;
		} else {
			new_spte = *sptep & ~PT64_BASE_ADDR_MASK;
			new_spte |= (u64)new_pfn << PAGE_SHIFT;

			new_spte &= ~PT_WRITABLE_MASK;
			new_spte &= ~SPTE_HOST_WRITEABLE;

			new_spte = mark_spte_for_access_track(new_spte);

			mmu_spte_clear_track_bits(sptep);
			mmu_spte_set(sptep, new_spte);
		}
	}

	if (need_flush && kvm_available_flush_tlb_with_range()) {
		kvm_flush_remote_tlbs_with_address(kvm, gfn, 1);
		return 0;
	}

	return need_flush;
}

struct slot_rmap_walk_iterator {
	/* input fields. */
	struct kvm_memory_slot *slot;
	gfn_t start_gfn;
	gfn_t end_gfn;
	int start_level;
	int end_level;

	/* output fields. */
	gfn_t gfn;
	struct kvm_rmap_head *rmap;
	int level;

	/* private field. */
	struct kvm_rmap_head *end_rmap;
};

static void
rmap_walk_init_level(struct slot_rmap_walk_iterator *iterator, int level)
{
	iterator->level = level;
	iterator->gfn = iterator->start_gfn;
	iterator->rmap = __gfn_to_rmap(iterator->gfn, level, iterator->slot);
	iterator->end_rmap = __gfn_to_rmap(iterator->end_gfn, level,
					   iterator->slot);
}

static void
slot_rmap_walk_init(struct slot_rmap_walk_iterator *iterator,
		    struct kvm_memory_slot *slot, int start_level,
		    int end_level, gfn_t start_gfn, gfn_t end_gfn)
{
	iterator->slot = slot;
	iterator->start_level = start_level;
	iterator->end_level = end_level;
	iterator->start_gfn = start_gfn;
	iterator->end_gfn = end_gfn;

	rmap_walk_init_level(iterator, iterator->start_level);
}

static bool slot_rmap_walk_okay(struct slot_rmap_walk_iterator *iterator)
{
	return !!iterator->rmap;
}

static void slot_rmap_walk_next(struct slot_rmap_walk_iterator *iterator)
{
	if (++iterator->rmap <= iterator->end_rmap) {
		iterator->gfn += (1UL << KVM_HPAGE_GFN_SHIFT(iterator->level));
		return;
	}

	if (++iterator->level > iterator->end_level) {
		iterator->rmap = NULL;
		return;
	}

	rmap_walk_init_level(iterator, iterator->level);
}

#define for_each_slot_rmap_range(_slot_, _start_level_, _end_level_,	\
	   _start_gfn, _end_gfn, _iter_)				\
	for (slot_rmap_walk_init(_iter_, _slot_, _start_level_,		\
				 _end_level_, _start_gfn, _end_gfn);	\
	     slot_rmap_walk_okay(_iter_);				\
	     slot_rmap_walk_next(_iter_))

static int kvm_handle_hva_range(struct kvm *kvm,
				unsigned long start,
				unsigned long end,
				unsigned long data,
				int (*handler)(struct kvm *kvm,
					       struct kvm_rmap_head *rmap_head,
					       struct kvm_memory_slot *slot,
					       gfn_t gfn,
					       int level,
					       unsigned long data))
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	struct slot_rmap_walk_iterator iterator;
	int ret = 0;
	int i;

	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++) {
		slots = __kvm_memslots(kvm, i);
		kvm_for_each_memslot(memslot, slots) {
			unsigned long hva_start, hva_end;
			gfn_t gfn_start, gfn_end;

			hva_start = max(start, memslot->userspace_addr);
			hva_end = min(end, memslot->userspace_addr +
				      (memslot->npages << PAGE_SHIFT));
			if (hva_start >= hva_end)
				continue;
			/*
			 * {gfn(page) | page intersects with [hva_start, hva_end)} =
			 * {gfn_start, gfn_start+1, ..., gfn_end-1}.
			 */
			gfn_start = hva_to_gfn_memslot(hva_start, memslot);
			gfn_end = hva_to_gfn_memslot(hva_end + PAGE_SIZE - 1, memslot);

			for_each_slot_rmap_range(memslot, PG_LEVEL_4K,
						 KVM_MAX_HUGEPAGE_LEVEL,
						 gfn_start, gfn_end - 1,
						 &iterator)
				ret |= handler(kvm, iterator.rmap, memslot,
					       iterator.gfn, iterator.level, data);
		}
	}

	return ret;
}

static int kvm_handle_hva(struct kvm *kvm, unsigned long hva,
			  unsigned long data,
			  int (*handler)(struct kvm *kvm,
					 struct kvm_rmap_head *rmap_head,
					 struct kvm_memory_slot *slot,
					 gfn_t gfn, int level,
					 unsigned long data))
{
	return kvm_handle_hva_range(kvm, hva, hva + 1, data, handler);
}

int kvm_unmap_hva_range(struct kvm *kvm, unsigned long start, unsigned long end,
			unsigned flags)
{
	return kvm_handle_hva_range(kvm, start, end, 0, kvm_unmap_rmapp);
}

int kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	return kvm_handle_hva(kvm, hva, (unsigned long)&pte, kvm_set_pte_rmapp);
}

static int kvm_age_rmapp(struct kvm *kvm, struct kvm_rmap_head *rmap_head,
			 struct kvm_memory_slot *slot, gfn_t gfn, int level,
			 unsigned long data)
{
	u64 *sptep;
	struct rmap_iterator iter;
	int young = 0;

	for_each_rmap_spte(rmap_head, &iter, sptep)
		young |= mmu_spte_age(sptep);

	trace_kvm_age_page(gfn, level, slot, young);
	return young;
}

static int kvm_test_age_rmapp(struct kvm *kvm, struct kvm_rmap_head *rmap_head,
			      struct kvm_memory_slot *slot, gfn_t gfn,
			      int level, unsigned long data)
{
	u64 *sptep;
	struct rmap_iterator iter;

	for_each_rmap_spte(rmap_head, &iter, sptep)
		if (is_accessed_spte(*sptep))
			return 1;
	return 0;
}

#define RMAP_RECYCLE_THRESHOLD 1000

static void rmap_recycle(struct kvm_vcpu *vcpu, u64 *spte, gfn_t gfn)
{
	struct kvm_rmap_head *rmap_head;
	struct kvm_mmu_page *sp;

	sp = sptep_to_sp(spte);

	rmap_head = gfn_to_rmap(vcpu->kvm, gfn, sp);

	kvm_unmap_rmapp(vcpu->kvm, rmap_head, NULL, gfn, sp->role.level, 0);
	kvm_flush_remote_tlbs_with_address(vcpu->kvm, sp->gfn,
			KVM_PAGES_PER_HPAGE(sp->role.level));
}

int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end)
{
	return kvm_handle_hva_range(kvm, start, end, 0, kvm_age_rmapp);
}

int kvm_test_age_hva(struct kvm *kvm, unsigned long hva)
{
	return kvm_handle_hva(kvm, hva, 0, kvm_test_age_rmapp);
}

#ifdef MMU_DEBUG
static int is_empty_shadow_page(u64 *spt)
{
	u64 *pos;
	u64 *end;

	for (pos = spt, end = pos + PAGE_SIZE / sizeof(u64); pos != end; pos++)
		if (is_shadow_present_pte(*pos)) {
			printk(KERN_ERR "%s: %p %llx\n", __func__,
			       pos, *pos);
			return 0;
		}
	return 1;
}
#endif

/*
 * This value is the sum of all of the kvm instances's
 * kvm->arch.n_used_mmu_pages values.  We need a global,
 * aggregate version in order to make the slab shrinker
 * faster
 */
static inline void kvm_mod_used_mmu_pages(struct kvm *kvm, unsigned long nr)
{
	kvm->arch.n_used_mmu_pages += nr;
	percpu_counter_add(&kvm_total_used_mmu_pages, nr);
}

static void kvm_mmu_free_page(struct kvm_mmu_page *sp)
{
	MMU_WARN_ON(!is_empty_shadow_page(sp->spt));
	hlist_del(&sp->hash_link);
	list_del(&sp->link);
	free_page((unsigned long)sp->spt);
	if (!sp->role.direct)
		free_page((unsigned long)sp->gfns);
	kmem_cache_free(mmu_page_header_cache, sp);
}

static unsigned kvm_page_table_hashfn(gfn_t gfn)
{
	return hash_64(gfn, KVM_MMU_HASH_SHIFT);
}

static void mmu_page_add_parent_pte(struct kvm_vcpu *vcpu,
				    struct kvm_mmu_page *sp, u64 *parent_pte)
{
	if (!parent_pte)
		return;

	pte_list_add(vcpu, parent_pte, &sp->parent_ptes);
}

static void mmu_page_remove_parent_pte(struct kvm_mmu_page *sp,
				       u64 *parent_pte)
{
	__pte_list_remove(parent_pte, &sp->parent_ptes);
}

static void drop_parent_pte(struct kvm_mmu_page *sp,
			    u64 *parent_pte)
{
	mmu_page_remove_parent_pte(sp, parent_pte);
	mmu_spte_clear_no_track(parent_pte);
}

static struct kvm_mmu_page *kvm_mmu_alloc_page(struct kvm_vcpu *vcpu, int direct)
{
	struct kvm_mmu_page *sp;

	sp = kvm_mmu_memory_cache_alloc(&vcpu->arch.mmu_page_header_cache);
	sp->spt = kvm_mmu_memory_cache_alloc(&vcpu->arch.mmu_shadow_page_cache);
	if (!direct)
		sp->gfns = kvm_mmu_memory_cache_alloc(&vcpu->arch.mmu_gfn_array_cache);
	set_page_private(virt_to_page(sp->spt), (unsigned long)sp);

	/*
	 * active_mmu_pages must be a FIFO list, as kvm_zap_obsolete_pages()
	 * depends on valid pages being added to the head of the list.  See
	 * comments in kvm_zap_obsolete_pages().
	 */
	sp->mmu_valid_gen = vcpu->kvm->arch.mmu_valid_gen;
	list_add(&sp->link, &vcpu->kvm->arch.active_mmu_pages);
	kvm_mod_used_mmu_pages(vcpu->kvm, +1);
	return sp;
}

static void mark_unsync(u64 *spte);
static void kvm_mmu_mark_parents_unsync(struct kvm_mmu_page *sp)
{
	u64 *sptep;
	struct rmap_iterator iter;

	for_each_rmap_spte(&sp->parent_ptes, &iter, sptep) {
		mark_unsync(sptep);
	}
}

static void mark_unsync(u64 *spte)
{
	struct kvm_mmu_page *sp;
	unsigned int index;

	sp = sptep_to_sp(spte);
	index = spte - sp->spt;
	if (__test_and_set_bit(index, sp->unsync_child_bitmap))
		return;
	if (sp->unsync_children++)
		return;
	kvm_mmu_mark_parents_unsync(sp);
}

static int nonpaging_sync_page(struct kvm_vcpu *vcpu,
			       struct kvm_mmu_page *sp)
{
	return 0;
}

static void nonpaging_update_pte(struct kvm_vcpu *vcpu,
				 struct kvm_mmu_page *sp, u64 *spte,
				 const void *pte)
{
	WARN_ON(1);
}

#define KVM_PAGE_ARRAY_NR 16

struct kvm_mmu_pages {
	struct mmu_page_and_offset {
		struct kvm_mmu_page *sp;
		unsigned int idx;
	} page[KVM_PAGE_ARRAY_NR];
	unsigned int nr;
};

static int mmu_pages_add(struct kvm_mmu_pages *pvec, struct kvm_mmu_page *sp,
			 int idx)
{
	int i;

	if (sp->unsync)
		for (i=0; i < pvec->nr; i++)
			if (pvec->page[i].sp == sp)
				return 0;

	pvec->page[pvec->nr].sp = sp;
	pvec->page[pvec->nr].idx = idx;
	pvec->nr++;
	return (pvec->nr == KVM_PAGE_ARRAY_NR);
}

static inline void clear_unsync_child_bit(struct kvm_mmu_page *sp, int idx)
{
	--sp->unsync_children;
	WARN_ON((int)sp->unsync_children < 0);
	__clear_bit(idx, sp->unsync_child_bitmap);
}

static int __mmu_unsync_walk(struct kvm_mmu_page *sp,
			   struct kvm_mmu_pages *pvec)
{
	int i, ret, nr_unsync_leaf = 0;

	for_each_set_bit(i, sp->unsync_child_bitmap, 512) {
		struct kvm_mmu_page *child;
		u64 ent = sp->spt[i];

		if (!is_shadow_present_pte(ent) || is_large_pte(ent)) {
			clear_unsync_child_bit(sp, i);
			continue;
		}

		child = to_shadow_page(ent & PT64_BASE_ADDR_MASK);

		if (child->unsync_children) {
			if (mmu_pages_add(pvec, child, i))
				return -ENOSPC;

			ret = __mmu_unsync_walk(child, pvec);
			if (!ret) {
				clear_unsync_child_bit(sp, i);
				continue;
			} else if (ret > 0) {
				nr_unsync_leaf += ret;
			} else
				return ret;
		} else if (child->unsync) {
			nr_unsync_leaf++;
			if (mmu_pages_add(pvec, child, i))
				return -ENOSPC;
		} else
			clear_unsync_child_bit(sp, i);
	}

	return nr_unsync_leaf;
}

#define INVALID_INDEX (-1)

static int mmu_unsync_walk(struct kvm_mmu_page *sp,
			   struct kvm_mmu_pages *pvec)
{
	pvec->nr = 0;
	if (!sp->unsync_children)
		return 0;

	mmu_pages_add(pvec, sp, INVALID_INDEX);
	return __mmu_unsync_walk(sp, pvec);
}

static void kvm_unlink_unsync_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	WARN_ON(!sp->unsync);
	trace_kvm_mmu_sync_page(sp);
	sp->unsync = 0;
	--kvm->stat.mmu_unsync;
}

static bool kvm_mmu_prepare_zap_page(struct kvm *kvm, struct kvm_mmu_page *sp,
				     struct list_head *invalid_list);
static void kvm_mmu_commit_zap_page(struct kvm *kvm,
				    struct list_head *invalid_list);

#define for_each_valid_sp(_kvm, _sp, _list)				\
	hlist_for_each_entry(_sp, _list, hash_link)			\
		if (is_obsolete_sp((_kvm), (_sp))) {			\
		} else

#define for_each_gfn_indirect_valid_sp(_kvm, _sp, _gfn)			\
	for_each_valid_sp(_kvm, _sp,					\
	  &(_kvm)->arch.mmu_page_hash[kvm_page_table_hashfn(_gfn)])	\
		if ((_sp)->gfn != (_gfn) || (_sp)->role.direct) {} else

static inline bool is_ept_sp(struct kvm_mmu_page *sp)
{
	return sp->role.cr0_wp && sp->role.smap_andnot_wp;
}

/* @sp->gfn should be write-protected at the call site */
static bool __kvm_sync_page(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			    struct list_head *invalid_list)
{
	if ((!is_ept_sp(sp) && sp->role.gpte_is_8_bytes != !!is_pae(vcpu)) ||
	    vcpu->arch.mmu->sync_page(vcpu, sp) == 0) {
		kvm_mmu_prepare_zap_page(vcpu->kvm, sp, invalid_list);
		return false;
	}

	return true;
}

static bool kvm_mmu_remote_flush_or_zap(struct kvm *kvm,
					struct list_head *invalid_list,
					bool remote_flush)
{
	if (!remote_flush && list_empty(invalid_list))
		return false;

	if (!list_empty(invalid_list))
		kvm_mmu_commit_zap_page(kvm, invalid_list);
	else
		kvm_flush_remote_tlbs(kvm);
	return true;
}

static void kvm_mmu_flush_or_zap(struct kvm_vcpu *vcpu,
				 struct list_head *invalid_list,
				 bool remote_flush, bool local_flush)
{
	if (kvm_mmu_remote_flush_or_zap(vcpu->kvm, invalid_list, remote_flush))
		return;

	if (local_flush)
		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
}

#ifdef CONFIG_KVM_MMU_AUDIT
#include "mmu_audit.c"
#else
static void kvm_mmu_audit(struct kvm_vcpu *vcpu, int point) { }
static void mmu_audit_disable(void) { }
#endif

static bool is_obsolete_sp(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	return sp->role.invalid ||
	       unlikely(sp->mmu_valid_gen != kvm->arch.mmu_valid_gen);
}

static bool kvm_sync_page(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			 struct list_head *invalid_list)
{
	kvm_unlink_unsync_page(vcpu->kvm, sp);
	return __kvm_sync_page(vcpu, sp, invalid_list);
}

/* @gfn should be write-protected at the call site */
static bool kvm_sync_pages(struct kvm_vcpu *vcpu, gfn_t gfn,
			   struct list_head *invalid_list)
{
	struct kvm_mmu_page *s;
	bool ret = false;

	for_each_gfn_indirect_valid_sp(vcpu->kvm, s, gfn) {
		if (!s->unsync)
			continue;

		WARN_ON(s->role.level != PG_LEVEL_4K);
		ret |= kvm_sync_page(vcpu, s, invalid_list);
	}

	return ret;
}

struct mmu_page_path {
	struct kvm_mmu_page *parent[PT64_ROOT_MAX_LEVEL];
	unsigned int idx[PT64_ROOT_MAX_LEVEL];
};

#define for_each_sp(pvec, sp, parents, i)			\
		for (i = mmu_pages_first(&pvec, &parents);	\
			i < pvec.nr && ({ sp = pvec.page[i].sp; 1;});	\
			i = mmu_pages_next(&pvec, &parents, i))

static int mmu_pages_next(struct kvm_mmu_pages *pvec,
			  struct mmu_page_path *parents,
			  int i)
{
	int n;

	for (n = i+1; n < pvec->nr; n++) {
		struct kvm_mmu_page *sp = pvec->page[n].sp;
		unsigned idx = pvec->page[n].idx;
		int level = sp->role.level;

		parents->idx[level-1] = idx;
		if (level == PG_LEVEL_4K)
			break;

		parents->parent[level-2] = sp;
	}

	return n;
}

static int mmu_pages_first(struct kvm_mmu_pages *pvec,
			   struct mmu_page_path *parents)
{
	struct kvm_mmu_page *sp;
	int level;

	if (pvec->nr == 0)
		return 0;

	WARN_ON(pvec->page[0].idx != INVALID_INDEX);

	sp = pvec->page[0].sp;
	level = sp->role.level;
	WARN_ON(level == PG_LEVEL_4K);

	parents->parent[level-2] = sp;

	/* Also set up a sentinel.  Further entries in pvec are all
	 * children of sp, so this element is never overwritten.
	 */
	parents->parent[level-1] = NULL;
	return mmu_pages_next(pvec, parents, 0);
}

static void mmu_pages_clear_parents(struct mmu_page_path *parents)
{
	struct kvm_mmu_page *sp;
	unsigned int level = 0;

	do {
		unsigned int idx = parents->idx[level];
		sp = parents->parent[level];
		if (!sp)
			return;

		WARN_ON(idx == INVALID_INDEX);
		clear_unsync_child_bit(sp, idx);
		level++;
	} while (!sp->unsync_children);
}

static void mmu_sync_children(struct kvm_vcpu *vcpu,
			      struct kvm_mmu_page *parent)
{
	int i;
	struct kvm_mmu_page *sp;
	struct mmu_page_path parents;
	struct kvm_mmu_pages pages;
	LIST_HEAD(invalid_list);
	bool flush = false;

	while (mmu_unsync_walk(parent, &pages)) {
		bool protected = false;

		for_each_sp(pages, sp, parents, i)
			protected |= rmap_write_protect(vcpu, sp->gfn);

		if (protected) {
			kvm_flush_remote_tlbs(vcpu->kvm);
			flush = false;
		}

		for_each_sp(pages, sp, parents, i) {
			flush |= kvm_sync_page(vcpu, sp, &invalid_list);
			mmu_pages_clear_parents(&parents);
		}
		if (need_resched() || spin_needbreak(&vcpu->kvm->mmu_lock)) {
			kvm_mmu_flush_or_zap(vcpu, &invalid_list, false, flush);
			cond_resched_lock(&vcpu->kvm->mmu_lock);
			flush = false;
		}
	}

	kvm_mmu_flush_or_zap(vcpu, &invalid_list, false, flush);
}

static void __clear_sp_write_flooding_count(struct kvm_mmu_page *sp)
{
	atomic_set(&sp->write_flooding_count,  0);
}

static void clear_sp_write_flooding_count(u64 *spte)
{
	__clear_sp_write_flooding_count(sptep_to_sp(spte));
}

static struct kvm_mmu_page *kvm_mmu_get_page(struct kvm_vcpu *vcpu,
					     gfn_t gfn,
					     gva_t gaddr,
					     unsigned level,
					     int direct,
					     unsigned int access)
{
	bool direct_mmu = vcpu->arch.mmu->direct_map;
	union kvm_mmu_page_role role;
	struct hlist_head *sp_list;
	unsigned quadrant;
	struct kvm_mmu_page *sp;
	bool need_sync = false;
	bool flush = false;
	int collisions = 0;
	LIST_HEAD(invalid_list);

	role = vcpu->arch.mmu->mmu_role.base;
	role.level = level;
	role.direct = direct;
	if (role.direct)
		role.gpte_is_8_bytes = true;
	role.access = access;
	if (!direct_mmu && vcpu->arch.mmu->root_level <= PT32_ROOT_LEVEL) {
		quadrant = gaddr >> (PAGE_SHIFT + (PT64_PT_BITS * level));
		quadrant &= (1 << ((PT32_PT_BITS - PT64_PT_BITS) * level)) - 1;
		role.quadrant = quadrant;
	}

	sp_list = &vcpu->kvm->arch.mmu_page_hash[kvm_page_table_hashfn(gfn)];
	for_each_valid_sp(vcpu->kvm, sp, sp_list) {
		if (sp->gfn != gfn) {
			collisions++;
			continue;
		}

		if (!need_sync && sp->unsync)
			need_sync = true;

		if (sp->role.word != role.word)
			continue;

		if (direct_mmu)
			goto trace_get_page;

		if (sp->unsync) {
			/* The page is good, but __kvm_sync_page might still end
			 * up zapping it.  If so, break in order to rebuild it.
			 */
			if (!__kvm_sync_page(vcpu, sp, &invalid_list))
				break;

			WARN_ON(!list_empty(&invalid_list));
			kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
		}

		if (sp->unsync_children)
			kvm_make_request(KVM_REQ_MMU_SYNC, vcpu);

		__clear_sp_write_flooding_count(sp);

trace_get_page:
		trace_kvm_mmu_get_page(sp, false);
		goto out;
	}

	++vcpu->kvm->stat.mmu_cache_miss;

	sp = kvm_mmu_alloc_page(vcpu, direct);

	sp->gfn = gfn;
	sp->role = role;
	hlist_add_head(&sp->hash_link, sp_list);
	if (!direct) {
		/*
		 * we should do write protection before syncing pages
		 * otherwise the content of the synced shadow page may
		 * be inconsistent with guest page table.
		 */
		account_shadowed(vcpu->kvm, sp);
		if (level == PG_LEVEL_4K && rmap_write_protect(vcpu, gfn))
			kvm_flush_remote_tlbs_with_address(vcpu->kvm, gfn, 1);

		if (level > PG_LEVEL_4K && need_sync)
			flush |= kvm_sync_pages(vcpu, gfn, &invalid_list);
	}
	trace_kvm_mmu_get_page(sp, true);

	kvm_mmu_flush_or_zap(vcpu, &invalid_list, false, flush);
out:
	if (collisions > vcpu->kvm->stat.max_mmu_page_hash_collisions)
		vcpu->kvm->stat.max_mmu_page_hash_collisions = collisions;
	return sp;
}

static void shadow_walk_init_using_root(struct kvm_shadow_walk_iterator *iterator,
					struct kvm_vcpu *vcpu, hpa_t root,
					u64 addr)
{
	iterator->addr = addr;
	iterator->shadow_addr = root;
	iterator->level = vcpu->arch.mmu->shadow_root_level;

	if (iterator->level == PT64_ROOT_4LEVEL &&
	    vcpu->arch.mmu->root_level < PT64_ROOT_4LEVEL &&
	    !vcpu->arch.mmu->direct_map)
		--iterator->level;

	if (iterator->level == PT32E_ROOT_LEVEL) {
		/*
		 * prev_root is currently only used for 64-bit hosts. So only
		 * the active root_hpa is valid here.
		 */
		BUG_ON(root != vcpu->arch.mmu->root_hpa);

		iterator->shadow_addr
			= vcpu->arch.mmu->pae_root[(addr >> 30) & 3];
		iterator->shadow_addr &= PT64_BASE_ADDR_MASK;
		--iterator->level;
		if (!iterator->shadow_addr)
			iterator->level = 0;
	}
}

static void shadow_walk_init(struct kvm_shadow_walk_iterator *iterator,
			     struct kvm_vcpu *vcpu, u64 addr)
{
	shadow_walk_init_using_root(iterator, vcpu, vcpu->arch.mmu->root_hpa,
				    addr);
}

static bool shadow_walk_okay(struct kvm_shadow_walk_iterator *iterator)
{
	if (iterator->level < PG_LEVEL_4K)
		return false;

	iterator->index = SHADOW_PT_INDEX(iterator->addr, iterator->level);
	iterator->sptep	= ((u64 *)__va(iterator->shadow_addr)) + iterator->index;
	return true;
}

static void __shadow_walk_next(struct kvm_shadow_walk_iterator *iterator,
			       u64 spte)
{
	if (is_last_spte(spte, iterator->level)) {
		iterator->level = 0;
		return;
	}

	iterator->shadow_addr = spte & PT64_BASE_ADDR_MASK;
	--iterator->level;
}

static void shadow_walk_next(struct kvm_shadow_walk_iterator *iterator)
{
	__shadow_walk_next(iterator, *iterator->sptep);
}

static void link_shadow_page(struct kvm_vcpu *vcpu, u64 *sptep,
			     struct kvm_mmu_page *sp)
{
	u64 spte;

	BUILD_BUG_ON(VMX_EPT_WRITABLE_MASK != PT_WRITABLE_MASK);

	spte = __pa(sp->spt) | shadow_present_mask | PT_WRITABLE_MASK |
	       shadow_user_mask | shadow_x_mask | shadow_me_mask;

	if (sp_ad_disabled(sp))
		spte |= SPTE_AD_DISABLED_MASK;
	else
		spte |= shadow_accessed_mask;

	mmu_spte_set(sptep, spte);

	mmu_page_add_parent_pte(vcpu, sp, sptep);

	if (sp->unsync_children || sp->unsync)
		mark_unsync(sptep);
}

static void validate_direct_spte(struct kvm_vcpu *vcpu, u64 *sptep,
				   unsigned direct_access)
{
	if (is_shadow_present_pte(*sptep) && !is_large_pte(*sptep)) {
		struct kvm_mmu_page *child;

		/*
		 * For the direct sp, if the guest pte's dirty bit
		 * changed form clean to dirty, it will corrupt the
		 * sp's access: allow writable in the read-only sp,
		 * so we should update the spte at this point to get
		 * a new sp with the correct access.
		 */
		child = to_shadow_page(*sptep & PT64_BASE_ADDR_MASK);
		if (child->role.access == direct_access)
			return;

		drop_parent_pte(child, sptep);
		kvm_flush_remote_tlbs_with_address(vcpu->kvm, child->gfn, 1);
	}
}

static bool mmu_page_zap_pte(struct kvm *kvm, struct kvm_mmu_page *sp,
			     u64 *spte)
{
	u64 pte;
	struct kvm_mmu_page *child;

	pte = *spte;
	if (is_shadow_present_pte(pte)) {
		if (is_last_spte(pte, sp->role.level)) {
			drop_spte(kvm, spte);
			if (is_large_pte(pte))
				--kvm->stat.lpages;
		} else {
			child = to_shadow_page(pte & PT64_BASE_ADDR_MASK);
			drop_parent_pte(child, spte);
		}
		return true;
	}

	if (is_mmio_spte(pte))
		mmu_spte_clear_no_track(spte);

	return false;
}

static void kvm_mmu_page_unlink_children(struct kvm *kvm,
					 struct kvm_mmu_page *sp)
{
	unsigned i;

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i)
		mmu_page_zap_pte(kvm, sp, sp->spt + i);
}

static void kvm_mmu_unlink_parents(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	u64 *sptep;
	struct rmap_iterator iter;

	while ((sptep = rmap_get_first(&sp->parent_ptes, &iter)))
		drop_parent_pte(sp, sptep);
}

static int mmu_zap_unsync_children(struct kvm *kvm,
				   struct kvm_mmu_page *parent,
				   struct list_head *invalid_list)
{
	int i, zapped = 0;
	struct mmu_page_path parents;
	struct kvm_mmu_pages pages;

	if (parent->role.level == PG_LEVEL_4K)
		return 0;

	while (mmu_unsync_walk(parent, &pages)) {
		struct kvm_mmu_page *sp;

		for_each_sp(pages, sp, parents, i) {
			kvm_mmu_prepare_zap_page(kvm, sp, invalid_list);
			mmu_pages_clear_parents(&parents);
			zapped++;
		}
	}

	return zapped;
}

static bool __kvm_mmu_prepare_zap_page(struct kvm *kvm,
				       struct kvm_mmu_page *sp,
				       struct list_head *invalid_list,
				       int *nr_zapped)
{
	bool list_unstable;

	trace_kvm_mmu_prepare_zap_page(sp);
	++kvm->stat.mmu_shadow_zapped;
	*nr_zapped = mmu_zap_unsync_children(kvm, sp, invalid_list);
	kvm_mmu_page_unlink_children(kvm, sp);
	kvm_mmu_unlink_parents(kvm, sp);

	/* Zapping children means active_mmu_pages has become unstable. */
	list_unstable = *nr_zapped;

	if (!sp->role.invalid && !sp->role.direct)
		unaccount_shadowed(kvm, sp);

	if (sp->unsync)
		kvm_unlink_unsync_page(kvm, sp);
	if (!sp->root_count) {
		/* Count self */
		(*nr_zapped)++;

		/*
		 * Already invalid pages (previously active roots) are not on
		 * the active page list.  See list_del() in the "else" case of
		 * !sp->root_count.
		 */
		if (sp->role.invalid)
			list_add(&sp->link, invalid_list);
		else
			list_move(&sp->link, invalid_list);
		kvm_mod_used_mmu_pages(kvm, -1);
	} else {
		/*
		 * Remove the active root from the active page list, the root
		 * will be explicitly freed when the root_count hits zero.
		 */
		list_del(&sp->link);

		/*
		 * Obsolete pages cannot be used on any vCPUs, see the comment
		 * in kvm_mmu_zap_all_fast().  Note, is_obsolete_sp() also
		 * treats invalid shadow pages as being obsolete.
		 */
		if (!is_obsolete_sp(kvm, sp))
			kvm_reload_remote_mmus(kvm);
	}

	if (sp->lpage_disallowed)
		unaccount_huge_nx_page(kvm, sp);

	sp->role.invalid = 1;
	return list_unstable;
}

static bool kvm_mmu_prepare_zap_page(struct kvm *kvm, struct kvm_mmu_page *sp,
				     struct list_head *invalid_list)
{
	int nr_zapped;

	__kvm_mmu_prepare_zap_page(kvm, sp, invalid_list, &nr_zapped);
	return nr_zapped;
}

static void kvm_mmu_commit_zap_page(struct kvm *kvm,
				    struct list_head *invalid_list)
{
	struct kvm_mmu_page *sp, *nsp;

	if (list_empty(invalid_list))
		return;

	/*
	 * We need to make sure everyone sees our modifications to
	 * the page tables and see changes to vcpu->mode here. The barrier
	 * in the kvm_flush_remote_tlbs() achieves this. This pairs
	 * with vcpu_enter_guest and walk_shadow_page_lockless_begin/end.
	 *
	 * In addition, kvm_flush_remote_tlbs waits for all vcpus to exit
	 * guest mode and/or lockless shadow page table walks.
	 */
	kvm_flush_remote_tlbs(kvm);

	list_for_each_entry_safe(sp, nsp, invalid_list, link) {
		WARN_ON(!sp->role.invalid || sp->root_count);
		kvm_mmu_free_page(sp);
	}
}

static unsigned long kvm_mmu_zap_oldest_mmu_pages(struct kvm *kvm,
						  unsigned long nr_to_zap)
{
	unsigned long total_zapped = 0;
	struct kvm_mmu_page *sp, *tmp;
	LIST_HEAD(invalid_list);
	bool unstable;
	int nr_zapped;

	if (list_empty(&kvm->arch.active_mmu_pages))
		return 0;

restart:
	list_for_each_entry_safe(sp, tmp, &kvm->arch.active_mmu_pages, link) {
		/*
		 * Don't zap active root pages, the page itself can't be freed
		 * and zapping it will just force vCPUs to realloc and reload.
		 */
		if (sp->root_count)
			continue;

		unstable = __kvm_mmu_prepare_zap_page(kvm, sp, &invalid_list,
						      &nr_zapped);
		total_zapped += nr_zapped;
		if (total_zapped >= nr_to_zap)
			break;

		if (unstable)
			goto restart;
	}

	kvm_mmu_commit_zap_page(kvm, &invalid_list);

	kvm->stat.mmu_recycled += total_zapped;
	return total_zapped;
}

static inline unsigned long kvm_mmu_available_pages(struct kvm *kvm)
{
	if (kvm->arch.n_max_mmu_pages > kvm->arch.n_used_mmu_pages)
		return kvm->arch.n_max_mmu_pages -
			kvm->arch.n_used_mmu_pages;

	return 0;
}

static int make_mmu_pages_available(struct kvm_vcpu *vcpu)
{
	unsigned long avail = kvm_mmu_available_pages(vcpu->kvm);

	if (likely(avail >= KVM_MIN_FREE_MMU_PAGES))
		return 0;

	kvm_mmu_zap_oldest_mmu_pages(vcpu->kvm, KVM_REFILL_PAGES - avail);

	if (!kvm_mmu_available_pages(vcpu->kvm))
		return -ENOSPC;
	return 0;
}

/*
 * Changing the number of mmu pages allocated to the vm
 * Note: if goal_nr_mmu_pages is too small, you will get dead lock
 */
void kvm_mmu_change_mmu_pages(struct kvm *kvm, unsigned long goal_nr_mmu_pages)
{
	spin_lock(&kvm->mmu_lock);

	if (kvm->arch.n_used_mmu_pages > goal_nr_mmu_pages) {
		kvm_mmu_zap_oldest_mmu_pages(kvm, kvm->arch.n_used_mmu_pages -
						  goal_nr_mmu_pages);

		goal_nr_mmu_pages = kvm->arch.n_used_mmu_pages;
	}

	kvm->arch.n_max_mmu_pages = goal_nr_mmu_pages;

	spin_unlock(&kvm->mmu_lock);
}

int kvm_mmu_unprotect_page(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_mmu_page *sp;
	LIST_HEAD(invalid_list);
	int r;

	pgprintk("%s: looking for gfn %llx\n", __func__, gfn);
	r = 0;
	spin_lock(&kvm->mmu_lock);
	for_each_gfn_indirect_valid_sp(kvm, sp, gfn) {
		pgprintk("%s: gfn %llx role %x\n", __func__, gfn,
			 sp->role.word);
		r = 1;
		kvm_mmu_prepare_zap_page(kvm, sp, &invalid_list);
	}
	kvm_mmu_commit_zap_page(kvm, &invalid_list);
	spin_unlock(&kvm->mmu_lock);

	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_unprotect_page);

static void kvm_unsync_page(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp)
{
	trace_kvm_mmu_unsync_page(sp);
	++vcpu->kvm->stat.mmu_unsync;
	sp->unsync = 1;

	kvm_mmu_mark_parents_unsync(sp);
}

static bool mmu_need_write_protect(struct kvm_vcpu *vcpu, gfn_t gfn,
				   bool can_unsync)
{
	struct kvm_mmu_page *sp;

	if (kvm_page_track_is_active(vcpu, gfn, KVM_PAGE_TRACK_WRITE))
		return true;

	for_each_gfn_indirect_valid_sp(vcpu->kvm, sp, gfn) {
		if (!can_unsync)
			return true;

		if (sp->unsync)
			continue;

		WARN_ON(sp->role.level != PG_LEVEL_4K);
		kvm_unsync_page(vcpu, sp);
	}

	/*
	 * We need to ensure that the marking of unsync pages is visible
	 * before the SPTE is updated to allow writes because
	 * kvm_mmu_sync_roots() checks the unsync flags without holding
	 * the MMU lock and so can race with this. If the SPTE was updated
	 * before the page had been marked as unsync-ed, something like the
	 * following could happen:
	 *
	 * CPU 1                    CPU 2
	 * ---------------------------------------------------------------------
	 * 1.2 Host updates SPTE
	 *     to be writable
	 *                      2.1 Guest writes a GPTE for GVA X.
	 *                          (GPTE being in the guest page table shadowed
	 *                           by the SP from CPU 1.)
	 *                          This reads SPTE during the page table walk.
	 *                          Since SPTE.W is read as 1, there is no
	 *                          fault.
	 *
	 *                      2.2 Guest issues TLB flush.
	 *                          That causes a VM Exit.
	 *
	 *                      2.3 kvm_mmu_sync_pages() reads sp->unsync.
	 *                          Since it is false, so it just returns.
	 *
	 *                      2.4 Guest accesses GVA X.
	 *                          Since the mapping in the SP was not updated,
	 *                          so the old mapping for GVA X incorrectly
	 *                          gets used.
	 * 1.1 Host marks SP
	 *     as unsync
	 *     (sp->unsync = true)
	 *
	 * The write barrier below ensures that 1.1 happens before 1.2 and thus
	 * the situation in 2.4 does not arise. The implicit barrier in 2.2
	 * pairs with this write barrier.
	 */
	smp_wmb();

	return false;
}

static bool kvm_is_mmio_pfn(kvm_pfn_t pfn)
{
	if (pfn_valid(pfn))
		return !is_zero_pfn(pfn) && PageReserved(pfn_to_page(pfn)) &&
			/*
			 * Some reserved pages, such as those from NVDIMM
			 * DAX devices, are not for MMIO, and can be mapped
			 * with cached memory type for better performance.
			 * However, the above check misconceives those pages
			 * as MMIO, and results in KVM mapping them with UC
			 * memory type, which would hurt the performance.
			 * Therefore, we check the host memory type in addition
			 * and only treat UC/UC-/WC pages as MMIO.
			 */
			(!pat_enabled() || pat_pfn_immune_to_uc_mtrr(pfn));

	return !e820__mapped_raw_any(pfn_to_hpa(pfn),
				     pfn_to_hpa(pfn + 1) - 1,
				     E820_TYPE_RAM);
}

/* Bits which may be returned by set_spte() */
#define SET_SPTE_WRITE_PROTECTED_PT	BIT(0)
#define SET_SPTE_NEED_REMOTE_TLB_FLUSH	BIT(1)

static int set_spte(struct kvm_vcpu *vcpu, u64 *sptep,
		    unsigned int pte_access, int level,
		    gfn_t gfn, kvm_pfn_t pfn, bool speculative,
		    bool can_unsync, bool host_writable)
{
	u64 spte = 0;
	int ret = 0;
	struct kvm_mmu_page *sp;

	if (set_mmio_spte(vcpu, sptep, gfn, pfn, pte_access))
		return 0;

	sp = sptep_to_sp(sptep);
	if (sp_ad_disabled(sp))
		spte |= SPTE_AD_DISABLED_MASK;
	else if (kvm_vcpu_ad_need_write_protect(vcpu))
		spte |= SPTE_AD_WRPROT_ONLY_MASK;

	/*
	 * For the EPT case, shadow_present_mask is 0 if hardware
	 * supports exec-only page table entries.  In that case,
	 * ACC_USER_MASK and shadow_user_mask are used to represent
	 * read access.  See FNAME(gpte_access) in paging_tmpl.h.
	 */
	spte |= shadow_present_mask;
	if (!speculative)
		spte |= spte_shadow_accessed_mask(spte);

	if (level > PG_LEVEL_4K && (pte_access & ACC_EXEC_MASK) &&
	    is_nx_huge_page_enabled()) {
		pte_access &= ~ACC_EXEC_MASK;
	}

	if (pte_access & ACC_EXEC_MASK)
		spte |= shadow_x_mask;
	else
		spte |= shadow_nx_mask;

	if (pte_access & ACC_USER_MASK)
		spte |= shadow_user_mask;

	if (level > PG_LEVEL_4K)
		spte |= PT_PAGE_SIZE_MASK;
	if (tdp_enabled)
		spte |= kvm_x86_ops.get_mt_mask(vcpu, gfn,
			kvm_is_mmio_pfn(pfn));

	if (host_writable)
		spte |= SPTE_HOST_WRITEABLE;
	else
		pte_access &= ~ACC_WRITE_MASK;

	if (!kvm_is_mmio_pfn(pfn))
		spte |= shadow_me_mask;

	spte |= (u64)pfn << PAGE_SHIFT;

	if (pte_access & ACC_WRITE_MASK) {
		spte |= PT_WRITABLE_MASK | SPTE_MMU_WRITEABLE;

		/*
		 * Optimization: for pte sync, if spte was writable the hash
		 * lookup is unnecessary (and expensive). Write protection
		 * is responsibility of mmu_get_page / kvm_sync_page.
		 * Same reasoning can be applied to dirty page accounting.
		 */
		if (!can_unsync && is_writable_pte(*sptep))
			goto set_pte;

		if (mmu_need_write_protect(vcpu, gfn, can_unsync)) {
			pgprintk("%s: found shadow page for %llx, marking ro\n",
				 __func__, gfn);
			ret |= SET_SPTE_WRITE_PROTECTED_PT;
			pte_access &= ~ACC_WRITE_MASK;
			spte &= ~(PT_WRITABLE_MASK | SPTE_MMU_WRITEABLE);
		}
	}

	if (pte_access & ACC_WRITE_MASK) {
		kvm_vcpu_mark_page_dirty(vcpu, gfn);
		spte |= spte_shadow_dirty_mask(spte);
	}

	if (speculative)
		spte = mark_spte_for_access_track(spte);

set_pte:
	if (mmu_spte_update(sptep, spte))
		ret |= SET_SPTE_NEED_REMOTE_TLB_FLUSH;
	return ret;
}

static int mmu_set_spte(struct kvm_vcpu *vcpu, u64 *sptep,
			unsigned int pte_access, int write_fault, int level,
			gfn_t gfn, kvm_pfn_t pfn, bool speculative,
			bool host_writable)
{
	int was_rmapped = 0;
	int rmap_count;
	int set_spte_ret;
	int ret = RET_PF_RETRY;
	bool flush = false;

	pgprintk("%s: spte %llx write_fault %d gfn %llx\n", __func__,
		 *sptep, write_fault, gfn);

	if (is_shadow_present_pte(*sptep)) {
		/*
		 * If we overwrite a PTE page pointer with a 2MB PMD, unlink
		 * the parent of the now unreachable PTE.
		 */
		if (level > PG_LEVEL_4K && !is_large_pte(*sptep)) {
			struct kvm_mmu_page *child;
			u64 pte = *sptep;

			child = to_shadow_page(pte & PT64_BASE_ADDR_MASK);
			drop_parent_pte(child, sptep);
			flush = true;
		} else if (pfn != spte_to_pfn(*sptep)) {
			pgprintk("hfn old %llx new %llx\n",
				 spte_to_pfn(*sptep), pfn);
			drop_spte(vcpu->kvm, sptep);
			flush = true;
		} else
			was_rmapped = 1;
	}

	set_spte_ret = set_spte(vcpu, sptep, pte_access, level, gfn, pfn,
				speculative, true, host_writable);
	if (set_spte_ret & SET_SPTE_WRITE_PROTECTED_PT) {
		if (write_fault)
			ret = RET_PF_EMULATE;
		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
	}

	if (set_spte_ret & SET_SPTE_NEED_REMOTE_TLB_FLUSH || flush)
		kvm_flush_remote_tlbs_with_address(vcpu->kvm, gfn,
				KVM_PAGES_PER_HPAGE(level));

	if (unlikely(is_mmio_spte(*sptep)))
		ret = RET_PF_EMULATE;

	pgprintk("%s: setting spte %llx\n", __func__, *sptep);
	trace_kvm_mmu_set_spte(level, gfn, sptep);
	if (!was_rmapped && is_large_pte(*sptep))
		++vcpu->kvm->stat.lpages;

	if (is_shadow_present_pte(*sptep)) {
		if (!was_rmapped) {
			rmap_count = rmap_add(vcpu, sptep, gfn);
			if (rmap_count > RMAP_RECYCLE_THRESHOLD)
				rmap_recycle(vcpu, sptep, gfn);
		}
	}

	return ret;
}

static kvm_pfn_t pte_prefetch_gfn_to_pfn(struct kvm_vcpu *vcpu, gfn_t gfn,
				     bool no_dirty_log)
{
	struct kvm_memory_slot *slot;

	slot = gfn_to_memslot_dirty_bitmap(vcpu, gfn, no_dirty_log);
	if (!slot)
		return KVM_PFN_ERR_FAULT;

	return gfn_to_pfn_memslot_atomic(slot, gfn);
}

static int direct_pte_prefetch_many(struct kvm_vcpu *vcpu,
				    struct kvm_mmu_page *sp,
				    u64 *start, u64 *end)
{
	struct page *pages[PTE_PREFETCH_NUM];
	struct kvm_memory_slot *slot;
	unsigned int access = sp->role.access;
	int i, ret;
	gfn_t gfn;

	gfn = kvm_mmu_page_get_gfn(sp, start - sp->spt);
	slot = gfn_to_memslot_dirty_bitmap(vcpu, gfn, access & ACC_WRITE_MASK);
	if (!slot)
		return -1;

	ret = gfn_to_page_many_atomic(slot, gfn, pages, end - start);
	if (ret <= 0)
		return -1;

	for (i = 0; i < ret; i++, gfn++, start++) {
		mmu_set_spte(vcpu, start, access, 0, sp->role.level, gfn,
			     page_to_pfn(pages[i]), true, true);
		put_page(pages[i]);
	}

	return 0;
}

static void __direct_pte_prefetch(struct kvm_vcpu *vcpu,
				  struct kvm_mmu_page *sp, u64 *sptep)
{
	u64 *spte, *start = NULL;
	int i;

	WARN_ON(!sp->role.direct);

	i = (sptep - sp->spt) & ~(PTE_PREFETCH_NUM - 1);
	spte = sp->spt + i;

	for (i = 0; i < PTE_PREFETCH_NUM; i++, spte++) {
		if (is_shadow_present_pte(*spte) || spte == sptep) {
			if (!start)
				continue;
			if (direct_pte_prefetch_many(vcpu, sp, start, spte) < 0)
				break;
			start = NULL;
		} else if (!start)
			start = spte;
	}
}

static void direct_pte_prefetch(struct kvm_vcpu *vcpu, u64 *sptep)
{
	struct kvm_mmu_page *sp;

	sp = sptep_to_sp(sptep);

	/*
	 * Without accessed bits, there's no way to distinguish between
	 * actually accessed translations and prefetched, so disable pte
	 * prefetch if accessed bits aren't available.
	 */
	if (sp_ad_disabled(sp))
		return;

	if (sp->role.level > PG_LEVEL_4K)
		return;

	__direct_pte_prefetch(vcpu, sp, sptep);
}

static int host_pfn_mapping_level(struct kvm_vcpu *vcpu, gfn_t gfn,
				  kvm_pfn_t pfn, struct kvm_memory_slot *slot)
{
	unsigned long hva;
	pte_t *pte;
	int level;

	if (!PageCompound(pfn_to_page(pfn)) && !kvm_is_zone_device_pfn(pfn))
		return PG_LEVEL_4K;

	/*
	 * Note, using the already-retrieved memslot and __gfn_to_hva_memslot()
	 * is not solely for performance, it's also necessary to avoid the
	 * "writable" check in __gfn_to_hva_many(), which will always fail on
	 * read-only memslots due to gfn_to_hva() assuming writes.  Earlier
	 * page fault steps have already verified the guest isn't writing a
	 * read-only memslot.
	 */
	hva = __gfn_to_hva_memslot(slot, gfn);

	pte = lookup_address_in_mm(vcpu->kvm->mm, hva, &level);
	if (unlikely(!pte))
		return PG_LEVEL_4K;

	return level;
}

static int kvm_mmu_hugepage_adjust(struct kvm_vcpu *vcpu, gfn_t gfn,
				   int max_level, kvm_pfn_t *pfnp)
{
	struct kvm_memory_slot *slot;
	struct kvm_lpage_info *linfo;
	kvm_pfn_t pfn = *pfnp;
	kvm_pfn_t mask;
	int level;

	if (unlikely(max_level == PG_LEVEL_4K))
		return PG_LEVEL_4K;

	if (is_error_noslot_pfn(pfn) || kvm_is_reserved_pfn(pfn))
		return PG_LEVEL_4K;

	slot = gfn_to_memslot_dirty_bitmap(vcpu, gfn, true);
	if (!slot)
		return PG_LEVEL_4K;

	max_level = min(max_level, max_huge_page_level);
	for ( ; max_level > PG_LEVEL_4K; max_level--) {
		linfo = lpage_info_slot(gfn, slot, max_level);
		if (!linfo->disallow_lpage)
			break;
	}

	if (max_level == PG_LEVEL_4K)
		return PG_LEVEL_4K;

	level = host_pfn_mapping_level(vcpu, gfn, pfn, slot);
	if (level == PG_LEVEL_4K)
		return level;

	level = min(level, max_level);

	/*
	 * mmu_notifier_retry() was successful and mmu_lock is held, so
	 * the pmd can't be split from under us.
	 */
	mask = KVM_PAGES_PER_HPAGE(level) - 1;
	VM_BUG_ON((gfn & mask) != (pfn & mask));
	*pfnp = pfn & ~mask;

	return level;
}

static void disallowed_hugepage_adjust(struct kvm_shadow_walk_iterator it,
				       gfn_t gfn, kvm_pfn_t *pfnp, int *levelp)
{
	int level = *levelp;
	u64 spte = *it.sptep;

	if (it.level == level && level > PG_LEVEL_4K &&
	    is_nx_huge_page_enabled() &&
	    is_shadow_present_pte(spte) &&
	    !is_large_pte(spte)) {
		/*
		 * A small SPTE exists for this pfn, but FNAME(fetch)
		 * and __direct_map would like to create a large PTE
		 * instead: just force them to go down another level,
		 * patching back for them into pfn the next 9 bits of
		 * the address.
		 */
		u64 page_mask = KVM_PAGES_PER_HPAGE(level) - KVM_PAGES_PER_HPAGE(level - 1);
		*pfnp |= gfn & page_mask;
		(*levelp)--;
	}
}

static int __direct_map(struct kvm_vcpu *vcpu, gpa_t gpa, int write,
			int map_writable, int max_level, kvm_pfn_t pfn,
			bool prefault, bool account_disallowed_nx_lpage)
{
	struct kvm_shadow_walk_iterator it;
	struct kvm_mmu_page *sp;
	int level, ret;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	gfn_t base_gfn = gfn;

	if (WARN_ON(!VALID_PAGE(vcpu->arch.mmu->root_hpa)))
		return RET_PF_RETRY;

	level = kvm_mmu_hugepage_adjust(vcpu, gfn, max_level, &pfn);

	trace_kvm_mmu_spte_requested(gpa, level, pfn);
	for_each_shadow_entry(vcpu, gpa, it) {
		/*
		 * We cannot overwrite existing page tables with an NX
		 * large page, as the leaf could be executable.
		 */
		disallowed_hugepage_adjust(it, gfn, &pfn, &level);

		base_gfn = gfn & ~(KVM_PAGES_PER_HPAGE(it.level) - 1);
		if (it.level == level)
			break;

		drop_large_spte(vcpu, it.sptep);
		if (!is_shadow_present_pte(*it.sptep)) {
			sp = kvm_mmu_get_page(vcpu, base_gfn, it.addr,
					      it.level - 1, true, ACC_ALL);

			link_shadow_page(vcpu, it.sptep, sp);
			if (account_disallowed_nx_lpage)
				account_huge_nx_page(vcpu->kvm, sp);
		}
	}

	ret = mmu_set_spte(vcpu, it.sptep, ACC_ALL,
			   write, level, base_gfn, pfn, prefault,
			   map_writable);
	direct_pte_prefetch(vcpu, it.sptep);
	++vcpu->stat.pf_fixed;
	return ret;
}

static void kvm_send_hwpoison_signal(unsigned long address, struct task_struct *tsk)
{
	send_sig_mceerr(BUS_MCEERR_AR, (void __user *)address, PAGE_SHIFT, tsk);
}

static int kvm_handle_bad_page(struct kvm_vcpu *vcpu, gfn_t gfn, kvm_pfn_t pfn)
{
	/*
	 * Do not cache the mmio info caused by writing the readonly gfn
	 * into the spte otherwise read access on readonly gfn also can
	 * caused mmio page fault and treat it as mmio access.
	 */
	if (pfn == KVM_PFN_ERR_RO_FAULT)
		return RET_PF_EMULATE;

	if (pfn == KVM_PFN_ERR_HWPOISON) {
		kvm_send_hwpoison_signal(kvm_vcpu_gfn_to_hva(vcpu, gfn), current);
		return RET_PF_RETRY;
	}

	return -EFAULT;
}

static bool handle_abnormal_pfn(struct kvm_vcpu *vcpu, gva_t gva, gfn_t gfn,
				kvm_pfn_t pfn, unsigned int access,
				int *ret_val)
{
	/* The pfn is invalid, report the error! */
	if (unlikely(is_error_pfn(pfn))) {
		*ret_val = kvm_handle_bad_page(vcpu, gfn, pfn);
		return true;
	}

	if (unlikely(is_noslot_pfn(pfn)))
		vcpu_cache_mmio_info(vcpu, gva, gfn,
				     access & shadow_mmio_access_mask);

	return false;
}

static bool page_fault_can_be_fast(u32 error_code)
{
	/*
	 * Do not fix the mmio spte with invalid generation number which
	 * need to be updated by slow page fault path.
	 */
	if (unlikely(error_code & PFERR_RSVD_MASK))
		return false;

	/* See if the page fault is due to an NX violation */
	if (unlikely(((error_code & (PFERR_FETCH_MASK | PFERR_PRESENT_MASK))
		      == (PFERR_FETCH_MASK | PFERR_PRESENT_MASK))))
		return false;

	/*
	 * #PF can be fast if:
	 * 1. The shadow page table entry is not present, which could mean that
	 *    the fault is potentially caused by access tracking (if enabled).
	 * 2. The shadow page table entry is present and the fault
	 *    is caused by write-protect, that means we just need change the W
	 *    bit of the spte which can be done out of mmu-lock.
	 *
	 * However, if access tracking is disabled we know that a non-present
	 * page must be a genuine page fault where we have to create a new SPTE.
	 * So, if access tracking is disabled, we return true only for write
	 * accesses to a present page.
	 */

	return shadow_acc_track_mask != 0 ||
	       ((error_code & (PFERR_WRITE_MASK | PFERR_PRESENT_MASK))
		== (PFERR_WRITE_MASK | PFERR_PRESENT_MASK));
}

/*
 * Returns true if the SPTE was fixed successfully. Otherwise,
 * someone else modified the SPTE from its original value.
 */
static bool
fast_pf_fix_direct_spte(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			u64 *sptep, u64 old_spte, u64 new_spte)
{
	gfn_t gfn;

	WARN_ON(!sp->role.direct);

	/*
	 * Theoretically we could also set dirty bit (and flush TLB) here in
	 * order to eliminate unnecessary PML logging. See comments in
	 * set_spte. But fast_page_fault is very unlikely to happen with PML
	 * enabled, so we do not do this. This might result in the same GPA
	 * to be logged in PML buffer again when the write really happens, and
	 * eventually to be called by mark_page_dirty twice. But it's also no
	 * harm. This also avoids the TLB flush needed after setting dirty bit
	 * so non-PML cases won't be impacted.
	 *
	 * Compare with set_spte where instead shadow_dirty_mask is set.
	 */
	if (cmpxchg64(sptep, old_spte, new_spte) != old_spte)
		return false;

	if (is_writable_pte(new_spte) && !is_writable_pte(old_spte)) {
		/*
		 * The gfn of direct spte is stable since it is
		 * calculated by sp->gfn.
		 */
		gfn = kvm_mmu_page_get_gfn(sp, sptep - sp->spt);
		kvm_vcpu_mark_page_dirty(vcpu, gfn);
	}

	return true;
}

static bool is_access_allowed(u32 fault_err_code, u64 spte)
{
	if (fault_err_code & PFERR_FETCH_MASK)
		return is_executable_pte(spte);

	if (fault_err_code & PFERR_WRITE_MASK)
		return is_writable_pte(spte);

	/* Fault was on Read access */
	return spte & PT_PRESENT_MASK;
}

/*
 * Return value:
 * - true: let the vcpu to access on the same address again.
 * - false: let the real page fault path to fix it.
 */
static bool fast_page_fault(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
			    u32 error_code)
{
	struct kvm_shadow_walk_iterator iterator;
	struct kvm_mmu_page *sp;
	bool fault_handled = false;
	u64 spte = 0ull;
	uint retry_count = 0;

	if (!page_fault_can_be_fast(error_code))
		return false;

	walk_shadow_page_lockless_begin(vcpu);

	do {
		u64 new_spte;

		for_each_shadow_entry_lockless(vcpu, cr2_or_gpa, iterator, spte)
			if (!is_shadow_present_pte(spte))
				break;

		sp = sptep_to_sp(iterator.sptep);
		if (!is_last_spte(spte, sp->role.level))
			break;

		/*
		 * Check whether the memory access that caused the fault would
		 * still cause it if it were to be performed right now. If not,
		 * then this is a spurious fault caused by TLB lazily flushed,
		 * or some other CPU has already fixed the PTE after the
		 * current CPU took the fault.
		 *
		 * Need not check the access of upper level table entries since
		 * they are always ACC_ALL.
		 */
		if (is_access_allowed(error_code, spte)) {
			fault_handled = true;
			break;
		}

		new_spte = spte;

		if (is_access_track_spte(spte))
			new_spte = restore_acc_track_spte(new_spte);

		/*
		 * Currently, to simplify the code, write-protection can
		 * be removed in the fast path only if the SPTE was
		 * write-protected for dirty-logging or access tracking.
		 */
		if ((error_code & PFERR_WRITE_MASK) &&
		    spte_can_locklessly_be_made_writable(spte)) {
			new_spte |= PT_WRITABLE_MASK;

			/*
			 * Do not fix write-permission on the large spte.  Since
			 * we only dirty the first page into the dirty-bitmap in
			 * fast_pf_fix_direct_spte(), other pages are missed
			 * if its slot has dirty logging enabled.
			 *
			 * Instead, we let the slow page fault path create a
			 * normal spte to fix the access.
			 *
			 * See the comments in kvm_arch_commit_memory_region().
			 */
			if (sp->role.level > PG_LEVEL_4K)
				break;
		}

		/* Verify that the fault can be handled in the fast path */
		if (new_spte == spte ||
		    !is_access_allowed(error_code, new_spte))
			break;

		/*
		 * Currently, fast page fault only works for direct mapping
		 * since the gfn is not stable for indirect shadow page. See
		 * Documentation/virt/kvm/locking.rst to get more detail.
		 */
		fault_handled = fast_pf_fix_direct_spte(vcpu, sp,
							iterator.sptep, spte,
							new_spte);
		if (fault_handled)
			break;

		if (++retry_count > 4) {
			printk_once(KERN_WARNING
				"kvm: Fast #PF retrying more than 4 times.\n");
			break;
		}

	} while (true);

	trace_fast_page_fault(vcpu, cr2_or_gpa, error_code, iterator.sptep,
			      spte, fault_handled);
	walk_shadow_page_lockless_end(vcpu);

	return fault_handled;
}

static void mmu_free_root_page(struct kvm *kvm, hpa_t *root_hpa,
			       struct list_head *invalid_list)
{
	struct kvm_mmu_page *sp;

	if (!VALID_PAGE(*root_hpa))
		return;

	sp = to_shadow_page(*root_hpa & PT64_BASE_ADDR_MASK);
	--sp->root_count;
	if (!sp->root_count && sp->role.invalid)
		kvm_mmu_prepare_zap_page(kvm, sp, invalid_list);

	*root_hpa = INVALID_PAGE;
}

/* roots_to_free must be some combination of the KVM_MMU_ROOT_* flags */
void kvm_mmu_free_roots(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			ulong roots_to_free)
{
	int i;
	LIST_HEAD(invalid_list);
	bool free_active_root = roots_to_free & KVM_MMU_ROOT_CURRENT;

	BUILD_BUG_ON(KVM_MMU_NUM_PREV_ROOTS >= BITS_PER_LONG);

	/* Before acquiring the MMU lock, see if we need to do any real work. */
	if (!(free_active_root && VALID_PAGE(mmu->root_hpa))) {
		for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
			if ((roots_to_free & KVM_MMU_ROOT_PREVIOUS(i)) &&
			    VALID_PAGE(mmu->prev_roots[i].hpa))
				break;

		if (i == KVM_MMU_NUM_PREV_ROOTS)
			return;
	}

	spin_lock(&vcpu->kvm->mmu_lock);

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
		if (roots_to_free & KVM_MMU_ROOT_PREVIOUS(i))
			mmu_free_root_page(vcpu->kvm, &mmu->prev_roots[i].hpa,
					   &invalid_list);

	if (free_active_root) {
		if (mmu->shadow_root_level >= PT64_ROOT_4LEVEL &&
		    (mmu->root_level >= PT64_ROOT_4LEVEL || mmu->direct_map)) {
			mmu_free_root_page(vcpu->kvm, &mmu->root_hpa,
					   &invalid_list);
		} else {
			for (i = 0; i < 4; ++i)
				if (mmu->pae_root[i] != 0)
					mmu_free_root_page(vcpu->kvm,
							   &mmu->pae_root[i],
							   &invalid_list);
			mmu->root_hpa = INVALID_PAGE;
		}
		mmu->root_pgd = 0;
	}

	kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
	spin_unlock(&vcpu->kvm->mmu_lock);
}
EXPORT_SYMBOL_GPL(kvm_mmu_free_roots);

static int mmu_check_root(struct kvm_vcpu *vcpu, gfn_t root_gfn)
{
	int ret = 0;

	if (!kvm_vcpu_is_visible_gfn(vcpu, root_gfn)) {
		kvm_make_request(KVM_REQ_TRIPLE_FAULT, vcpu);
		ret = 1;
	}

	return ret;
}

static hpa_t mmu_alloc_root(struct kvm_vcpu *vcpu, gfn_t gfn, gva_t gva,
			    u8 level, bool direct)
{
	struct kvm_mmu_page *sp;

	spin_lock(&vcpu->kvm->mmu_lock);

	if (make_mmu_pages_available(vcpu)) {
		spin_unlock(&vcpu->kvm->mmu_lock);
		return INVALID_PAGE;
	}
	sp = kvm_mmu_get_page(vcpu, gfn, gva, level, direct, ACC_ALL);
	++sp->root_count;

	spin_unlock(&vcpu->kvm->mmu_lock);
	return __pa(sp->spt);
}

static int mmu_alloc_direct_roots(struct kvm_vcpu *vcpu)
{
	u8 shadow_root_level = vcpu->arch.mmu->shadow_root_level;
	hpa_t root;
	unsigned i;

	if (shadow_root_level >= PT64_ROOT_4LEVEL) {
		root = mmu_alloc_root(vcpu, 0, 0, shadow_root_level, true);
		if (!VALID_PAGE(root))
			return -ENOSPC;
		vcpu->arch.mmu->root_hpa = root;
	} else if (shadow_root_level == PT32E_ROOT_LEVEL) {
		for (i = 0; i < 4; ++i) {
			MMU_WARN_ON(VALID_PAGE(vcpu->arch.mmu->pae_root[i]));

			root = mmu_alloc_root(vcpu, i << (30 - PAGE_SHIFT),
					      i << 30, PT32_ROOT_LEVEL, true);
			if (!VALID_PAGE(root))
				return -ENOSPC;
			vcpu->arch.mmu->pae_root[i] = root | PT_PRESENT_MASK;
		}
		vcpu->arch.mmu->root_hpa = __pa(vcpu->arch.mmu->pae_root);
	} else
		BUG();

	/* root_pgd is ignored for direct MMUs. */
	vcpu->arch.mmu->root_pgd = 0;

	return 0;
}

static int mmu_alloc_shadow_roots(struct kvm_vcpu *vcpu)
{
	u64 pdptr, pm_mask;
	gfn_t root_gfn, root_pgd;
	hpa_t root;
	int i;

	root_pgd = vcpu->arch.mmu->get_guest_pgd(vcpu);
	root_gfn = root_pgd >> PAGE_SHIFT;

	if (mmu_check_root(vcpu, root_gfn))
		return 1;

	/*
	 * Do we shadow a long mode page table? If so we need to
	 * write-protect the guests page table root.
	 */
	if (vcpu->arch.mmu->root_level >= PT64_ROOT_4LEVEL) {
		MMU_WARN_ON(VALID_PAGE(vcpu->arch.mmu->root_hpa));

		root = mmu_alloc_root(vcpu, root_gfn, 0,
				      vcpu->arch.mmu->shadow_root_level, false);
		if (!VALID_PAGE(root))
			return -ENOSPC;
		vcpu->arch.mmu->root_hpa = root;
		goto set_root_pgd;
	}

	/*
	 * We shadow a 32 bit page table. This may be a legacy 2-level
	 * or a PAE 3-level page table. In either case we need to be aware that
	 * the shadow page table may be a PAE or a long mode page table.
	 */
	pm_mask = PT_PRESENT_MASK;
	if (vcpu->arch.mmu->shadow_root_level == PT64_ROOT_4LEVEL)
		pm_mask |= PT_ACCESSED_MASK | PT_WRITABLE_MASK | PT_USER_MASK;

	for (i = 0; i < 4; ++i) {
		MMU_WARN_ON(VALID_PAGE(vcpu->arch.mmu->pae_root[i]));
		if (vcpu->arch.mmu->root_level == PT32E_ROOT_LEVEL) {
			pdptr = vcpu->arch.mmu->get_pdptr(vcpu, i);
			if (!(pdptr & PT_PRESENT_MASK)) {
				vcpu->arch.mmu->pae_root[i] = 0;
				continue;
			}
			root_gfn = pdptr >> PAGE_SHIFT;
			if (mmu_check_root(vcpu, root_gfn))
				return 1;
		}

		root = mmu_alloc_root(vcpu, root_gfn, i << 30,
				      PT32_ROOT_LEVEL, false);
		if (!VALID_PAGE(root))
			return -ENOSPC;
		vcpu->arch.mmu->pae_root[i] = root | pm_mask;
	}
	vcpu->arch.mmu->root_hpa = __pa(vcpu->arch.mmu->pae_root);

	/*
	 * If we shadow a 32 bit page table with a long mode page
	 * table we enter this path.
	 */
	if (vcpu->arch.mmu->shadow_root_level == PT64_ROOT_4LEVEL) {
		if (vcpu->arch.mmu->lm_root == NULL) {
			/*
			 * The additional page necessary for this is only
			 * allocated on demand.
			 */

			u64 *lm_root;

			lm_root = (void*)get_zeroed_page(GFP_KERNEL_ACCOUNT);
			if (lm_root == NULL)
				return 1;

			lm_root[0] = __pa(vcpu->arch.mmu->pae_root) | pm_mask;

			vcpu->arch.mmu->lm_root = lm_root;
		}

		vcpu->arch.mmu->root_hpa = __pa(vcpu->arch.mmu->lm_root);
	}

set_root_pgd:
	vcpu->arch.mmu->root_pgd = root_pgd;

	return 0;
}

static int mmu_alloc_roots(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.mmu->direct_map)
		return mmu_alloc_direct_roots(vcpu);
	else
		return mmu_alloc_shadow_roots(vcpu);
}

void kvm_mmu_sync_roots(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_mmu_page *sp;

	if (vcpu->arch.mmu->direct_map)
		return;

	if (!VALID_PAGE(vcpu->arch.mmu->root_hpa))
		return;

	vcpu_clear_mmio_info(vcpu, MMIO_GVA_ANY);

	if (vcpu->arch.mmu->root_level >= PT64_ROOT_4LEVEL) {
		hpa_t root = vcpu->arch.mmu->root_hpa;
		sp = to_shadow_page(root);

		/*
		 * Even if another CPU was marking the SP as unsync-ed
		 * simultaneously, any guest page table changes are not
		 * guaranteed to be visible anyway until this VCPU issues a TLB
		 * flush strictly after those changes are made. We only need to
		 * ensure that the other CPU sets these flags before any actual
		 * changes to the page tables are made. The comments in
		 * mmu_need_write_protect() describe what could go wrong if this
		 * requirement isn't satisfied.
		 */
		if (!smp_load_acquire(&sp->unsync) &&
		    !smp_load_acquire(&sp->unsync_children))
			return;

		spin_lock(&vcpu->kvm->mmu_lock);
		kvm_mmu_audit(vcpu, AUDIT_PRE_SYNC);

		mmu_sync_children(vcpu, sp);

		kvm_mmu_audit(vcpu, AUDIT_POST_SYNC);
		spin_unlock(&vcpu->kvm->mmu_lock);
		return;
	}

	spin_lock(&vcpu->kvm->mmu_lock);
	kvm_mmu_audit(vcpu, AUDIT_PRE_SYNC);

	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu->pae_root[i];

		if (root && VALID_PAGE(root)) {
			root &= PT64_BASE_ADDR_MASK;
			sp = to_shadow_page(root);
			mmu_sync_children(vcpu, sp);
		}
	}

	kvm_mmu_audit(vcpu, AUDIT_POST_SYNC);
	spin_unlock(&vcpu->kvm->mmu_lock);
}
EXPORT_SYMBOL_GPL(kvm_mmu_sync_roots);

static gpa_t nonpaging_gva_to_gpa(struct kvm_vcpu *vcpu, gpa_t vaddr,
				  u32 access, struct x86_exception *exception)
{
	if (exception)
		exception->error_code = 0;
	return vaddr;
}

static gpa_t nonpaging_gva_to_gpa_nested(struct kvm_vcpu *vcpu, gpa_t vaddr,
					 u32 access,
					 struct x86_exception *exception)
{
	if (exception)
		exception->error_code = 0;
	return vcpu->arch.nested_mmu.translate_gpa(vcpu, vaddr, access, exception);
}

static bool
__is_rsvd_bits_set(struct rsvd_bits_validate *rsvd_check, u64 pte, int level)
{
	int bit7 = (pte >> 7) & 1;

	return pte & rsvd_check->rsvd_bits_mask[bit7][level-1];
}

static bool __is_bad_mt_xwr(struct rsvd_bits_validate *rsvd_check, u64 pte)
{
	return rsvd_check->bad_mt_xwr & BIT_ULL(pte & 0x3f);
}

static bool mmio_info_in_cache(struct kvm_vcpu *vcpu, u64 addr, bool direct)
{
	/*
	 * A nested guest cannot use the MMIO cache if it is using nested
	 * page tables, because cr2 is a nGPA while the cache stores GPAs.
	 */
	if (mmu_is_nested(vcpu))
		return false;

	if (direct)
		return vcpu_match_mmio_gpa(vcpu, addr);

	return vcpu_match_mmio_gva(vcpu, addr);
}

/* return true if reserved bit is detected on spte. */
static bool
walk_shadow_page_get_mmio_spte(struct kvm_vcpu *vcpu, u64 addr, u64 *sptep)
{
	struct kvm_shadow_walk_iterator iterator;
	u64 sptes[PT64_ROOT_MAX_LEVEL], spte = 0ull;
	struct rsvd_bits_validate *rsvd_check;
	int root, leaf;
	bool reserved = false;

	rsvd_check = &vcpu->arch.mmu->shadow_zero_check;

	walk_shadow_page_lockless_begin(vcpu);

	for (shadow_walk_init(&iterator, vcpu, addr),
		 leaf = root = iterator.level;
	     shadow_walk_okay(&iterator);
	     __shadow_walk_next(&iterator, spte)) {
		spte = mmu_spte_get_lockless(iterator.sptep);

		sptes[leaf - 1] = spte;
		leaf--;

		if (!is_shadow_present_pte(spte))
			break;

		/*
		 * Use a bitwise-OR instead of a logical-OR to aggregate the
		 * reserved bit and EPT's invalid memtype/XWR checks to avoid
		 * adding a Jcc in the loop.
		 */
		reserved |= __is_bad_mt_xwr(rsvd_check, spte) |
			    __is_rsvd_bits_set(rsvd_check, spte, iterator.level);
	}

	walk_shadow_page_lockless_end(vcpu);

	if (reserved) {
		pr_err("%s: detect reserved bits on spte, addr 0x%llx, dump hierarchy:\n",
		       __func__, addr);
		while (root > leaf) {
			pr_err("------ spte 0x%llx level %d.\n",
			       sptes[root - 1], root);
			root--;
		}
	}

	*sptep = spte;
	return reserved;
}

static int handle_mmio_page_fault(struct kvm_vcpu *vcpu, u64 addr, bool direct)
{
	u64 spte;
	bool reserved;

	if (mmio_info_in_cache(vcpu, addr, direct))
		return RET_PF_EMULATE;

	reserved = walk_shadow_page_get_mmio_spte(vcpu, addr, &spte);
	if (WARN_ON(reserved))
		return -EINVAL;

	if (is_mmio_spte(spte)) {
		gfn_t gfn = get_mmio_spte_gfn(spte);
		unsigned int access = get_mmio_spte_access(spte);

		if (!check_mmio_spte(vcpu, spte))
			return RET_PF_INVALID;

		if (direct)
			addr = 0;

		trace_handle_mmio_page_fault(addr, gfn, access);
		vcpu_cache_mmio_info(vcpu, addr, gfn, access);
		return RET_PF_EMULATE;
	}

	/*
	 * If the page table is zapped by other cpus, let CPU fault again on
	 * the address.
	 */
	return RET_PF_RETRY;
}

static bool page_fault_handle_page_track(struct kvm_vcpu *vcpu,
					 u32 error_code, gfn_t gfn)
{
	if (unlikely(error_code & PFERR_RSVD_MASK))
		return false;

	if (!(error_code & PFERR_PRESENT_MASK) ||
	      !(error_code & PFERR_WRITE_MASK))
		return false;

	/*
	 * guest is writing the page which is write tracked which can
	 * not be fixed by page fault handler.
	 */
	if (kvm_page_track_is_active(vcpu, gfn, KVM_PAGE_TRACK_WRITE))
		return true;

	return false;
}

static void shadow_page_table_clear_flood(struct kvm_vcpu *vcpu, gva_t addr)
{
	struct kvm_shadow_walk_iterator iterator;
	u64 spte;

	walk_shadow_page_lockless_begin(vcpu);
	for_each_shadow_entry_lockless(vcpu, addr, iterator, spte) {
		clear_sp_write_flooding_count(iterator.sptep);
		if (!is_shadow_present_pte(spte))
			break;
	}
	walk_shadow_page_lockless_end(vcpu);
}

static bool kvm_arch_setup_async_pf(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
				    gfn_t gfn)
{
	struct kvm_arch_async_pf arch;

	arch.token = (vcpu->arch.apf.id++ << 12) | vcpu->vcpu_id;
	arch.gfn = gfn;
	arch.direct_map = vcpu->arch.mmu->direct_map;
	arch.cr3 = vcpu->arch.mmu->get_guest_pgd(vcpu);

	return kvm_setup_async_pf(vcpu, cr2_or_gpa,
				  kvm_vcpu_gfn_to_hva(vcpu, gfn), &arch);
}

static bool try_async_pf(struct kvm_vcpu *vcpu, bool prefault, gfn_t gfn,
			 gpa_t cr2_or_gpa, kvm_pfn_t *pfn, bool write,
			 bool *writable)
{
	struct kvm_memory_slot *slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	bool async;

	/* Don't expose private memslots to L2. */
	if (is_guest_mode(vcpu) && !kvm_is_visible_memslot(slot)) {
		*pfn = KVM_PFN_NOSLOT;
		*writable = false;
		return false;
	}

	async = false;
	*pfn = __gfn_to_pfn_memslot(slot, gfn, false, &async, write, writable);
	if (!async)
		return false; /* *pfn has correct page already */

	if (!prefault && kvm_can_do_async_pf(vcpu)) {
		trace_kvm_try_async_get_page(cr2_or_gpa, gfn);
		if (kvm_find_async_pf_gfn(vcpu, gfn)) {
			trace_kvm_async_pf_doublefault(cr2_or_gpa, gfn);
			kvm_make_request(KVM_REQ_APF_HALT, vcpu);
			return true;
		} else if (kvm_arch_setup_async_pf(vcpu, cr2_or_gpa, gfn))
			return true;
	}

	*pfn = __gfn_to_pfn_memslot(slot, gfn, false, NULL, write, writable);
	return false;
}

static int direct_page_fault(struct kvm_vcpu *vcpu, gpa_t gpa, u32 error_code,
			     bool prefault, int max_level, bool is_tdp)
{
	bool write = error_code & PFERR_WRITE_MASK;
	bool exec = error_code & PFERR_FETCH_MASK;
	bool lpage_disallowed = exec && is_nx_huge_page_enabled();
	bool map_writable;

	gfn_t gfn = gpa >> PAGE_SHIFT;
	unsigned long mmu_seq;
	kvm_pfn_t pfn;
	int r;

	if (page_fault_handle_page_track(vcpu, error_code, gfn))
		return RET_PF_EMULATE;

	if (fast_page_fault(vcpu, gpa, error_code))
		return RET_PF_RETRY;

	r = mmu_topup_memory_caches(vcpu, false);
	if (r)
		return r;

	if (lpage_disallowed)
		max_level = PG_LEVEL_4K;

	mmu_seq = vcpu->kvm->mmu_notifier_seq;
	smp_rmb();

	if (try_async_pf(vcpu, prefault, gfn, gpa, &pfn, write, &map_writable))
		return RET_PF_RETRY;

	if (handle_abnormal_pfn(vcpu, is_tdp ? 0 : gpa, gfn, pfn, ACC_ALL, &r))
		return r;

	r = RET_PF_RETRY;
	spin_lock(&vcpu->kvm->mmu_lock);
	if (mmu_notifier_retry(vcpu->kvm, mmu_seq))
		goto out_unlock;
	r = make_mmu_pages_available(vcpu);
	if (r)
		goto out_unlock;
	r = __direct_map(vcpu, gpa, write, map_writable, max_level, pfn,
			 prefault, is_tdp && lpage_disallowed);

out_unlock:
	spin_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(pfn);
	return r;
}

static int nonpaging_page_fault(struct kvm_vcpu *vcpu, gpa_t gpa,
				u32 error_code, bool prefault)
{
	pgprintk("%s: gva %lx error %x\n", __func__, gpa, error_code);

	/* This path builds a PAE pagetable, we can map 2mb pages at maximum. */
	return direct_page_fault(vcpu, gpa & PAGE_MASK, error_code, prefault,
				 PG_LEVEL_2M, false);
}

int kvm_handle_page_fault(struct kvm_vcpu *vcpu, u64 error_code,
				u64 fault_address, char *insn, int insn_len)
{
	int r = 1;
	u32 flags = vcpu->arch.apf.host_apf_flags;

#ifndef CONFIG_X86_64
	/* A 64-bit CR2 should be impossible on 32-bit KVM. */
	if (WARN_ON_ONCE(fault_address >> 32))
		return -EFAULT;
#endif

	vcpu->arch.l1tf_flush_l1d = true;
	if (!flags) {
		trace_kvm_page_fault(fault_address, error_code);

		if (kvm_event_needs_reinjection(vcpu))
			kvm_mmu_unprotect_page_virt(vcpu, fault_address);
		r = kvm_mmu_page_fault(vcpu, fault_address, error_code, insn,
				insn_len);
	} else if (flags & KVM_PV_REASON_PAGE_NOT_PRESENT) {
		vcpu->arch.apf.host_apf_flags = 0;
		local_irq_disable();
		kvm_async_pf_task_wait_schedule(fault_address);
		local_irq_enable();
	} else {
		WARN_ONCE(1, "Unexpected host async PF flags: %x\n", flags);
	}

	return r;
}
EXPORT_SYMBOL_GPL(kvm_handle_page_fault);

int kvm_tdp_page_fault(struct kvm_vcpu *vcpu, gpa_t gpa, u32 error_code,
		       bool prefault)
{
	int max_level;

	for (max_level = KVM_MAX_HUGEPAGE_LEVEL;
	     max_level > PG_LEVEL_4K;
	     max_level--) {
		int page_num = KVM_PAGES_PER_HPAGE(max_level);
		gfn_t base = (gpa >> PAGE_SHIFT) & ~(page_num - 1);

		if (kvm_mtrr_check_gfn_range_consistency(vcpu, base, page_num))
			break;
	}

	return direct_page_fault(vcpu, gpa, error_code, prefault,
				 max_level, true);
}

static void nonpaging_init_context(struct kvm_vcpu *vcpu,
				   struct kvm_mmu *context)
{
	context->page_fault = nonpaging_page_fault;
	context->gva_to_gpa = nonpaging_gva_to_gpa;
	context->sync_page = nonpaging_sync_page;
	context->invlpg = NULL;
	context->update_pte = nonpaging_update_pte;
	context->root_level = 0;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->direct_map = true;
	context->nx = false;
}

static inline bool is_root_usable(struct kvm_mmu_root_info *root, gpa_t pgd,
				  union kvm_mmu_page_role role)
{
	return (role.direct || pgd == root->pgd) &&
	       VALID_PAGE(root->hpa) && to_shadow_page(root->hpa) &&
	       role.word == to_shadow_page(root->hpa)->role.word;
}

/*
 * Find out if a previously cached root matching the new pgd/role is available.
 * The current root is also inserted into the cache.
 * If a matching root was found, it is assigned to kvm_mmu->root_hpa and true is
 * returned.
 * Otherwise, the LRU root from the cache is assigned to kvm_mmu->root_hpa and
 * false is returned. This root should now be freed by the caller.
 */
static bool cached_root_available(struct kvm_vcpu *vcpu, gpa_t new_pgd,
				  union kvm_mmu_page_role new_role)
{
	uint i;
	struct kvm_mmu_root_info root;
	struct kvm_mmu *mmu = vcpu->arch.mmu;

	root.pgd = mmu->root_pgd;
	root.hpa = mmu->root_hpa;

	if (is_root_usable(&root, new_pgd, new_role))
		return true;

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++) {
		swap(root, mmu->prev_roots[i]);

		if (is_root_usable(&root, new_pgd, new_role))
			break;
	}

	mmu->root_hpa = root.hpa;
	mmu->root_pgd = root.pgd;

	return i < KVM_MMU_NUM_PREV_ROOTS;
}

static bool fast_pgd_switch(struct kvm_vcpu *vcpu, gpa_t new_pgd,
			    union kvm_mmu_page_role new_role)
{
	struct kvm_mmu *mmu = vcpu->arch.mmu;

	/*
	 * For now, limit the fast switch to 64-bit hosts+VMs in order to avoid
	 * having to deal with PDPTEs. We may add support for 32-bit hosts/VMs
	 * later if necessary.
	 */
	if (mmu->shadow_root_level >= PT64_ROOT_4LEVEL &&
	    mmu->root_level >= PT64_ROOT_4LEVEL)
		return cached_root_available(vcpu, new_pgd, new_role);

	return false;
}

static void __kvm_mmu_new_pgd(struct kvm_vcpu *vcpu, gpa_t new_pgd,
			      union kvm_mmu_page_role new_role,
			      bool skip_tlb_flush, bool skip_mmu_sync)
{
	if (!fast_pgd_switch(vcpu, new_pgd, new_role)) {
		kvm_mmu_free_roots(vcpu, vcpu->arch.mmu, KVM_MMU_ROOT_CURRENT);
		return;
	}

	/*
	 * It's possible that the cached previous root page is obsolete because
	 * of a change in the MMU generation number. However, changing the
	 * generation number is accompanied by KVM_REQ_MMU_RELOAD, which will
	 * free the root set here and allocate a new one.
	 */
	kvm_make_request(KVM_REQ_LOAD_MMU_PGD, vcpu);

	if (!skip_mmu_sync || force_flush_and_sync_on_reuse)
		kvm_make_request(KVM_REQ_MMU_SYNC, vcpu);
	if (!skip_tlb_flush || force_flush_and_sync_on_reuse)
		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);

	/*
	 * The last MMIO access's GVA and GPA are cached in the VCPU. When
	 * switching to a new CR3, that GVA->GPA mapping may no longer be
	 * valid. So clear any cached MMIO info even when we don't need to sync
	 * the shadow page tables.
	 */
	vcpu_clear_mmio_info(vcpu, MMIO_GVA_ANY);

	__clear_sp_write_flooding_count(to_shadow_page(vcpu->arch.mmu->root_hpa));
}

void kvm_mmu_new_pgd(struct kvm_vcpu *vcpu, gpa_t new_pgd, bool skip_tlb_flush,
		     bool skip_mmu_sync)
{
	__kvm_mmu_new_pgd(vcpu, new_pgd, kvm_mmu_calc_root_page_role(vcpu),
			  skip_tlb_flush, skip_mmu_sync);
}
EXPORT_SYMBOL_GPL(kvm_mmu_new_pgd);

static unsigned long get_cr3(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr3(vcpu);
}

static bool sync_mmio_spte(struct kvm_vcpu *vcpu, u64 *sptep, gfn_t gfn,
			   unsigned int access, int *nr_present)
{
	if (unlikely(is_mmio_spte(*sptep))) {
		if (gfn != get_mmio_spte_gfn(*sptep)) {
			mmu_spte_clear_no_track(sptep);
			return true;
		}

		(*nr_present)++;
		mark_mmio_spte(vcpu, sptep, gfn, access);
		return true;
	}

	return false;
}

static inline bool is_last_gpte(struct kvm_mmu *mmu,
				unsigned level, unsigned gpte)
{
	/*
	 * The RHS has bit 7 set iff level < mmu->last_nonleaf_level.
	 * If it is clear, there are no large pages at this level, so clear
	 * PT_PAGE_SIZE_MASK in gpte if that is the case.
	 */
	gpte &= level - mmu->last_nonleaf_level;

	/*
	 * PG_LEVEL_4K always terminates.  The RHS has bit 7 set
	 * iff level <= PG_LEVEL_4K, which for our purpose means
	 * level == PG_LEVEL_4K; set PT_PAGE_SIZE_MASK in gpte then.
	 */
	gpte |= level - PG_LEVEL_4K - 1;

	return gpte & PT_PAGE_SIZE_MASK;
}

#define PTTYPE_EPT 18 /* arbitrary */
#define PTTYPE PTTYPE_EPT
#include "paging_tmpl.h"
#undef PTTYPE

#define PTTYPE 64
#include "paging_tmpl.h"
#undef PTTYPE

#define PTTYPE 32
#include "paging_tmpl.h"
#undef PTTYPE

static void
__reset_rsvds_bits_mask(struct kvm_vcpu *vcpu,
			struct rsvd_bits_validate *rsvd_check,
			int maxphyaddr, int level, bool nx, bool gbpages,
			bool pse, bool amd)
{
	u64 exb_bit_rsvd = 0;
	u64 gbpages_bit_rsvd = 0;
	u64 nonleaf_bit8_rsvd = 0;

	rsvd_check->bad_mt_xwr = 0;

	if (!nx)
		exb_bit_rsvd = rsvd_bits(63, 63);
	if (!gbpages)
		gbpages_bit_rsvd = rsvd_bits(7, 7);

	/*
	 * Non-leaf PML4Es and PDPEs reserve bit 8 (which would be the G bit for
	 * leaf entries) on AMD CPUs only.
	 */
	if (amd)
		nonleaf_bit8_rsvd = rsvd_bits(8, 8);

	switch (level) {
	case PT32_ROOT_LEVEL:
		/* no rsvd bits for 2 level 4K page table entries */
		rsvd_check->rsvd_bits_mask[0][1] = 0;
		rsvd_check->rsvd_bits_mask[0][0] = 0;
		rsvd_check->rsvd_bits_mask[1][0] =
			rsvd_check->rsvd_bits_mask[0][0];

		if (!pse) {
			rsvd_check->rsvd_bits_mask[1][1] = 0;
			break;
		}

		if (is_cpuid_PSE36())
			/* 36bits PSE 4MB page */
			rsvd_check->rsvd_bits_mask[1][1] = rsvd_bits(17, 21);
		else
			/* 32 bits PSE 4MB page */
			rsvd_check->rsvd_bits_mask[1][1] = rsvd_bits(13, 21);
		break;
	case PT32E_ROOT_LEVEL:
		rsvd_check->rsvd_bits_mask[0][2] =
			rsvd_bits(maxphyaddr, 63) |
			rsvd_bits(5, 8) | rsvd_bits(1, 2);	/* PDPTE */
		rsvd_check->rsvd_bits_mask[0][1] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 62);	/* PDE */
		rsvd_check->rsvd_bits_mask[0][0] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 62); 	/* PTE */
		rsvd_check->rsvd_bits_mask[1][1] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 62) |
			rsvd_bits(13, 20);		/* large page */
		rsvd_check->rsvd_bits_mask[1][0] =
			rsvd_check->rsvd_bits_mask[0][0];
		break;
	case PT64_ROOT_5LEVEL:
		rsvd_check->rsvd_bits_mask[0][4] = exb_bit_rsvd |
			nonleaf_bit8_rsvd | rsvd_bits(7, 7) |
			rsvd_bits(maxphyaddr, 51);
		rsvd_check->rsvd_bits_mask[1][4] =
			rsvd_check->rsvd_bits_mask[0][4];
		fallthrough;
	case PT64_ROOT_4LEVEL:
		rsvd_check->rsvd_bits_mask[0][3] = exb_bit_rsvd |
			nonleaf_bit8_rsvd | rsvd_bits(7, 7) |
			rsvd_bits(maxphyaddr, 51);
		rsvd_check->rsvd_bits_mask[0][2] = exb_bit_rsvd |
			gbpages_bit_rsvd |
			rsvd_bits(maxphyaddr, 51);
		rsvd_check->rsvd_bits_mask[0][1] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 51);
		rsvd_check->rsvd_bits_mask[0][0] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 51);
		rsvd_check->rsvd_bits_mask[1][3] =
			rsvd_check->rsvd_bits_mask[0][3];
		rsvd_check->rsvd_bits_mask[1][2] = exb_bit_rsvd |
			gbpages_bit_rsvd | rsvd_bits(maxphyaddr, 51) |
			rsvd_bits(13, 29);
		rsvd_check->rsvd_bits_mask[1][1] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 51) |
			rsvd_bits(13, 20);		/* large page */
		rsvd_check->rsvd_bits_mask[1][0] =
			rsvd_check->rsvd_bits_mask[0][0];
		break;
	}
}

static void reset_rsvds_bits_mask(struct kvm_vcpu *vcpu,
				  struct kvm_mmu *context)
{
	__reset_rsvds_bits_mask(vcpu, &context->guest_rsvd_check,
				cpuid_maxphyaddr(vcpu), context->root_level,
				context->nx,
				guest_cpuid_has(vcpu, X86_FEATURE_GBPAGES),
				is_pse(vcpu),
				guest_cpuid_is_amd_or_hygon(vcpu));
}

static void
__reset_rsvds_bits_mask_ept(struct rsvd_bits_validate *rsvd_check,
			    int maxphyaddr, bool execonly)
{
	u64 bad_mt_xwr;

	rsvd_check->rsvd_bits_mask[0][4] =
		rsvd_bits(maxphyaddr, 51) | rsvd_bits(3, 7);
	rsvd_check->rsvd_bits_mask[0][3] =
		rsvd_bits(maxphyaddr, 51) | rsvd_bits(3, 7);
	rsvd_check->rsvd_bits_mask[0][2] =
		rsvd_bits(maxphyaddr, 51) | rsvd_bits(3, 6);
	rsvd_check->rsvd_bits_mask[0][1] =
		rsvd_bits(maxphyaddr, 51) | rsvd_bits(3, 6);
	rsvd_check->rsvd_bits_mask[0][0] = rsvd_bits(maxphyaddr, 51);

	/* large page */
	rsvd_check->rsvd_bits_mask[1][4] = rsvd_check->rsvd_bits_mask[0][4];
	rsvd_check->rsvd_bits_mask[1][3] = rsvd_check->rsvd_bits_mask[0][3];
	rsvd_check->rsvd_bits_mask[1][2] =
		rsvd_bits(maxphyaddr, 51) | rsvd_bits(12, 29);
	rsvd_check->rsvd_bits_mask[1][1] =
		rsvd_bits(maxphyaddr, 51) | rsvd_bits(12, 20);
	rsvd_check->rsvd_bits_mask[1][0] = rsvd_check->rsvd_bits_mask[0][0];

	bad_mt_xwr = 0xFFull << (2 * 8);	/* bits 3..5 must not be 2 */
	bad_mt_xwr |= 0xFFull << (3 * 8);	/* bits 3..5 must not be 3 */
	bad_mt_xwr |= 0xFFull << (7 * 8);	/* bits 3..5 must not be 7 */
	bad_mt_xwr |= REPEAT_BYTE(1ull << 2);	/* bits 0..2 must not be 010 */
	bad_mt_xwr |= REPEAT_BYTE(1ull << 6);	/* bits 0..2 must not be 110 */
	if (!execonly) {
		/* bits 0..2 must not be 100 unless VMX capabilities allow it */
		bad_mt_xwr |= REPEAT_BYTE(1ull << 4);
	}
	rsvd_check->bad_mt_xwr = bad_mt_xwr;
}

static void reset_rsvds_bits_mask_ept(struct kvm_vcpu *vcpu,
		struct kvm_mmu *context, bool execonly)
{
	__reset_rsvds_bits_mask_ept(&context->guest_rsvd_check,
				    cpuid_maxphyaddr(vcpu), execonly);
}

/*
 * the page table on host is the shadow page table for the page
 * table in guest or amd nested guest, its mmu features completely
 * follow the features in guest.
 */
void
reset_shadow_zero_bits_mask(struct kvm_vcpu *vcpu, struct kvm_mmu *context)
{
	bool uses_nx = context->nx ||
		context->mmu_role.base.smep_andnot_wp;
	struct rsvd_bits_validate *shadow_zero_check;
	int i;

	/*
	 * Passing "true" to the last argument is okay; it adds a check
	 * on bit 8 of the SPTEs which KVM doesn't use anyway.
	 */
	shadow_zero_check = &context->shadow_zero_check;
	__reset_rsvds_bits_mask(vcpu, shadow_zero_check,
				shadow_phys_bits,
				context->shadow_root_level, uses_nx,
				guest_cpuid_has(vcpu, X86_FEATURE_GBPAGES),
				is_pse(vcpu), true);

	if (!shadow_me_mask)
		return;

	for (i = context->shadow_root_level; --i >= 0;) {
		shadow_zero_check->rsvd_bits_mask[0][i] &= ~shadow_me_mask;
		shadow_zero_check->rsvd_bits_mask[1][i] &= ~shadow_me_mask;
	}

}
EXPORT_SYMBOL_GPL(reset_shadow_zero_bits_mask);

static inline bool boot_cpu_is_amd(void)
{
	WARN_ON_ONCE(!tdp_enabled);
	return shadow_x_mask == 0;
}

/*
 * the direct page table on host, use as much mmu features as
 * possible, however, kvm currently does not do execution-protection.
 */
static void
reset_tdp_shadow_zero_bits_mask(struct kvm_vcpu *vcpu,
				struct kvm_mmu *context)
{
	struct rsvd_bits_validate *shadow_zero_check;
	int i;

	shadow_zero_check = &context->shadow_zero_check;

	if (boot_cpu_is_amd())
		__reset_rsvds_bits_mask(vcpu, shadow_zero_check,
					shadow_phys_bits,
					context->shadow_root_level, false,
					boot_cpu_has(X86_FEATURE_GBPAGES),
					true, true);
	else
		__reset_rsvds_bits_mask_ept(shadow_zero_check,
					    shadow_phys_bits,
					    false);

	if (!shadow_me_mask)
		return;

	for (i = context->shadow_root_level; --i >= 0;) {
		shadow_zero_check->rsvd_bits_mask[0][i] &= ~shadow_me_mask;
		shadow_zero_check->rsvd_bits_mask[1][i] &= ~shadow_me_mask;
	}
}

/*
 * as the comments in reset_shadow_zero_bits_mask() except it
 * is the shadow page table for intel nested guest.
 */
static void
reset_ept_shadow_zero_bits_mask(struct kvm_vcpu *vcpu,
				struct kvm_mmu *context, bool execonly)
{
	__reset_rsvds_bits_mask_ept(&context->shadow_zero_check,
				    shadow_phys_bits, execonly);
}

#define BYTE_MASK(access) \
	((1 & (access) ? 2 : 0) | \
	 (2 & (access) ? 4 : 0) | \
	 (3 & (access) ? 8 : 0) | \
	 (4 & (access) ? 16 : 0) | \
	 (5 & (access) ? 32 : 0) | \
	 (6 & (access) ? 64 : 0) | \
	 (7 & (access) ? 128 : 0))


static void update_permission_bitmask(struct kvm_vcpu *vcpu,
				      struct kvm_mmu *mmu, bool ept)
{
	unsigned byte;

	const u8 x = BYTE_MASK(ACC_EXEC_MASK);
	const u8 w = BYTE_MASK(ACC_WRITE_MASK);
	const u8 u = BYTE_MASK(ACC_USER_MASK);

	bool cr4_smep = kvm_read_cr4_bits(vcpu, X86_CR4_SMEP) != 0;
	bool cr4_smap = kvm_read_cr4_bits(vcpu, X86_CR4_SMAP) != 0;
	bool cr0_wp = is_write_protection(vcpu);

	for (byte = 0; byte < ARRAY_SIZE(mmu->permissions); ++byte) {
		unsigned pfec = byte << 1;

		/*
		 * Each "*f" variable has a 1 bit for each UWX value
		 * that causes a fault with the given PFEC.
		 */

		/* Faults from writes to non-writable pages */
		u8 wf = (pfec & PFERR_WRITE_MASK) ? (u8)~w : 0;
		/* Faults from user mode accesses to supervisor pages */
		u8 uf = (pfec & PFERR_USER_MASK) ? (u8)~u : 0;
		/* Faults from fetches of non-executable pages*/
		u8 ff = (pfec & PFERR_FETCH_MASK) ? (u8)~x : 0;
		/* Faults from kernel mode fetches of user pages */
		u8 smepf = 0;
		/* Faults from kernel mode accesses of user pages */
		u8 smapf = 0;

		if (!ept) {
			/* Faults from kernel mode accesses to user pages */
			u8 kf = (pfec & PFERR_USER_MASK) ? 0 : u;

			/* Not really needed: !nx will cause pte.nx to fault */
			if (!mmu->nx)
				ff = 0;

			/* Allow supervisor writes if !cr0.wp */
			if (!cr0_wp)
				wf = (pfec & PFERR_USER_MASK) ? wf : 0;

			/* Disallow supervisor fetches of user code if cr4.smep */
			if (cr4_smep)
				smepf = (pfec & PFERR_FETCH_MASK) ? kf : 0;

			/*
			 * SMAP:kernel-mode data accesses from user-mode
			 * mappings should fault. A fault is considered
			 * as a SMAP violation if all of the following
			 * conditions are true:
			 *   - X86_CR4_SMAP is set in CR4
			 *   - A user page is accessed
			 *   - The access is not a fetch
			 *   - Page fault in kernel mode
			 *   - if CPL = 3 or X86_EFLAGS_AC is clear
			 *
			 * Here, we cover the first three conditions.
			 * The fourth is computed dynamically in permission_fault();
			 * PFERR_RSVD_MASK bit will be set in PFEC if the access is
			 * *not* subject to SMAP restrictions.
			 */
			if (cr4_smap)
				smapf = (pfec & (PFERR_RSVD_MASK|PFERR_FETCH_MASK)) ? 0 : kf;
		}

		mmu->permissions[byte] = ff | uf | wf | smepf | smapf;
	}
}

/*
* PKU is an additional mechanism by which the paging controls access to
* user-mode addresses based on the value in the PKRU register.  Protection
* key violations are reported through a bit in the page fault error code.
* Unlike other bits of the error code, the PK bit is not known at the
* call site of e.g. gva_to_gpa; it must be computed directly in
* permission_fault based on two bits of PKRU, on some machine state (CR4,
* CR0, EFER, CPL), and on other bits of the error code and the page tables.
*
* In particular the following conditions come from the error code, the
* page tables and the machine state:
* - PK is always zero unless CR4.PKE=1 and EFER.LMA=1
* - PK is always zero if RSVD=1 (reserved bit set) or F=1 (instruction fetch)
* - PK is always zero if U=0 in the page tables
* - PKRU.WD is ignored if CR0.WP=0 and the access is a supervisor access.
*
* The PKRU bitmask caches the result of these four conditions.  The error
* code (minus the P bit) and the page table's U bit form an index into the
* PKRU bitmask.  Two bits of the PKRU bitmask are then extracted and ANDed
* with the two bits of the PKRU register corresponding to the protection key.
* For the first three conditions above the bits will be 00, thus masking
* away both AD and WD.  For all reads or if the last condition holds, WD
* only will be masked away.
*/
static void update_pkru_bitmask(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
				bool ept)
{
	unsigned bit;
	bool wp;

	if (ept) {
		mmu->pkru_mask = 0;
		return;
	}

	/* PKEY is enabled only if CR4.PKE and EFER.LMA are both set. */
	if (!kvm_read_cr4_bits(vcpu, X86_CR4_PKE) || !is_long_mode(vcpu)) {
		mmu->pkru_mask = 0;
		return;
	}

	wp = is_write_protection(vcpu);

	for (bit = 0; bit < ARRAY_SIZE(mmu->permissions); ++bit) {
		unsigned pfec, pkey_bits;
		bool check_pkey, check_write, ff, uf, wf, pte_user;

		pfec = bit << 1;
		ff = pfec & PFERR_FETCH_MASK;
		uf = pfec & PFERR_USER_MASK;
		wf = pfec & PFERR_WRITE_MASK;

		/* PFEC.RSVD is replaced by ACC_USER_MASK. */
		pte_user = pfec & PFERR_RSVD_MASK;

		/*
		 * Only need to check the access which is not an
		 * instruction fetch and is to a user page.
		 */
		check_pkey = (!ff && pte_user);
		/*
		 * write access is controlled by PKRU if it is a
		 * user access or CR0.WP = 1.
		 */
		check_write = check_pkey && wf && (uf || wp);

		/* PKRU.AD stops both read and write access. */
		pkey_bits = !!check_pkey;
		/* PKRU.WD stops write access. */
		pkey_bits |= (!!check_write) << 1;

		mmu->pkru_mask |= (pkey_bits & 3) << pfec;
	}
}

static void update_last_nonleaf_level(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu)
{
	unsigned root_level = mmu->root_level;

	mmu->last_nonleaf_level = root_level;
	if (root_level == PT32_ROOT_LEVEL && is_pse(vcpu))
		mmu->last_nonleaf_level++;
}

static void paging64_init_context_common(struct kvm_vcpu *vcpu,
					 struct kvm_mmu *context,
					 int level)
{
	context->nx = is_nx(vcpu);
	context->root_level = level;

	reset_rsvds_bits_mask(vcpu, context);
	update_permission_bitmask(vcpu, context, false);
	update_pkru_bitmask(vcpu, context, false);
	update_last_nonleaf_level(vcpu, context);

	MMU_WARN_ON(!is_pae(vcpu));
	context->page_fault = paging64_page_fault;
	context->gva_to_gpa = paging64_gva_to_gpa;
	context->sync_page = paging64_sync_page;
	context->invlpg = paging64_invlpg;
	context->update_pte = paging64_update_pte;
	context->shadow_root_level = level;
	context->direct_map = false;
}

static void paging64_init_context(struct kvm_vcpu *vcpu,
				  struct kvm_mmu *context)
{
	int root_level = is_la57_mode(vcpu) ?
			 PT64_ROOT_5LEVEL : PT64_ROOT_4LEVEL;

	paging64_init_context_common(vcpu, context, root_level);
}

static void paging32_init_context(struct kvm_vcpu *vcpu,
				  struct kvm_mmu *context)
{
	context->nx = false;
	context->root_level = PT32_ROOT_LEVEL;

	reset_rsvds_bits_mask(vcpu, context);
	update_permission_bitmask(vcpu, context, false);
	update_pkru_bitmask(vcpu, context, false);
	update_last_nonleaf_level(vcpu, context);

	context->page_fault = paging32_page_fault;
	context->gva_to_gpa = paging32_gva_to_gpa;
	context->sync_page = paging32_sync_page;
	context->invlpg = paging32_invlpg;
	context->update_pte = paging32_update_pte;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->direct_map = false;
}

static void paging32E_init_context(struct kvm_vcpu *vcpu,
				   struct kvm_mmu *context)
{
	paging64_init_context_common(vcpu, context, PT32E_ROOT_LEVEL);
}

static union kvm_mmu_extended_role kvm_calc_mmu_role_ext(struct kvm_vcpu *vcpu)
{
	union kvm_mmu_extended_role ext = {0};

	ext.cr0_pg = !!is_paging(vcpu);
	ext.cr4_pae = !!is_pae(vcpu);
	ext.cr4_smep = !!kvm_read_cr4_bits(vcpu, X86_CR4_SMEP);
	ext.cr4_smap = !!kvm_read_cr4_bits(vcpu, X86_CR4_SMAP);
	ext.cr4_pse = !!is_pse(vcpu);
	ext.cr4_pke = !!kvm_read_cr4_bits(vcpu, X86_CR4_PKE);
	ext.maxphyaddr = cpuid_maxphyaddr(vcpu);

	ext.valid = 1;

	return ext;
}

static union kvm_mmu_role kvm_calc_mmu_role_common(struct kvm_vcpu *vcpu,
						   bool base_only)
{
	union kvm_mmu_role role = {0};

	role.base.access = ACC_ALL;
	role.base.nxe = !!is_nx(vcpu);
	role.base.cr0_wp = is_write_protection(vcpu);
	role.base.smm = is_smm(vcpu);
	role.base.guest_mode = is_guest_mode(vcpu);

	if (base_only)
		return role;

	role.ext = kvm_calc_mmu_role_ext(vcpu);

	return role;
}

static inline int kvm_mmu_get_tdp_level(struct kvm_vcpu *vcpu)
{
	/* Use 5-level TDP if and only if it's useful/necessary. */
	if (max_tdp_level == 5 && cpuid_maxphyaddr(vcpu) <= 48)
		return 4;

	return max_tdp_level;
}

static union kvm_mmu_role
kvm_calc_tdp_mmu_root_page_role(struct kvm_vcpu *vcpu, bool base_only)
{
	union kvm_mmu_role role = kvm_calc_mmu_role_common(vcpu, base_only);

	role.base.ad_disabled = (shadow_accessed_mask == 0);
	role.base.level = kvm_mmu_get_tdp_level(vcpu);
	role.base.direct = true;
	role.base.gpte_is_8_bytes = true;

	return role;
}

static void init_kvm_tdp_mmu(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->arch.root_mmu;
	union kvm_mmu_role new_role =
		kvm_calc_tdp_mmu_root_page_role(vcpu, false);

	if (new_role.as_u64 == context->mmu_role.as_u64)
		return;

	context->mmu_role.as_u64 = new_role.as_u64;
	context->page_fault = kvm_tdp_page_fault;
	context->sync_page = nonpaging_sync_page;
	context->invlpg = NULL;
	context->update_pte = nonpaging_update_pte;
	context->shadow_root_level = kvm_mmu_get_tdp_level(vcpu);
	context->direct_map = true;
	context->get_guest_pgd = get_cr3;
	context->get_pdptr = kvm_pdptr_read;
	context->inject_page_fault = kvm_inject_page_fault;

	if (!is_paging(vcpu)) {
		context->nx = false;
		context->gva_to_gpa = nonpaging_gva_to_gpa;
		context->root_level = 0;
	} else if (is_long_mode(vcpu)) {
		context->nx = is_nx(vcpu);
		context->root_level = is_la57_mode(vcpu) ?
				PT64_ROOT_5LEVEL : PT64_ROOT_4LEVEL;
		reset_rsvds_bits_mask(vcpu, context);
		context->gva_to_gpa = paging64_gva_to_gpa;
	} else if (is_pae(vcpu)) {
		context->nx = is_nx(vcpu);
		context->root_level = PT32E_ROOT_LEVEL;
		reset_rsvds_bits_mask(vcpu, context);
		context->gva_to_gpa = paging64_gva_to_gpa;
	} else {
		context->nx = false;
		context->root_level = PT32_ROOT_LEVEL;
		reset_rsvds_bits_mask(vcpu, context);
		context->gva_to_gpa = paging32_gva_to_gpa;
	}

	update_permission_bitmask(vcpu, context, false);
	update_pkru_bitmask(vcpu, context, false);
	update_last_nonleaf_level(vcpu, context);
	reset_tdp_shadow_zero_bits_mask(vcpu, context);
}

static union kvm_mmu_role
kvm_calc_shadow_root_page_role_common(struct kvm_vcpu *vcpu, bool base_only)
{
	union kvm_mmu_role role = kvm_calc_mmu_role_common(vcpu, base_only);

	role.base.smep_andnot_wp = role.ext.cr4_smep &&
		!is_write_protection(vcpu);
	role.base.smap_andnot_wp = role.ext.cr4_smap &&
		!is_write_protection(vcpu);
	role.base.gpte_is_8_bytes = !!is_pae(vcpu);

	return role;
}

static union kvm_mmu_role
kvm_calc_shadow_mmu_root_page_role(struct kvm_vcpu *vcpu, bool base_only)
{
	union kvm_mmu_role role =
		kvm_calc_shadow_root_page_role_common(vcpu, base_only);

	role.base.direct = !is_paging(vcpu);

	if (!is_long_mode(vcpu))
		role.base.level = PT32E_ROOT_LEVEL;
	else if (is_la57_mode(vcpu))
		role.base.level = PT64_ROOT_5LEVEL;
	else
		role.base.level = PT64_ROOT_4LEVEL;

	return role;
}

static void shadow_mmu_init_context(struct kvm_vcpu *vcpu, struct kvm_mmu *context,
				    u32 cr0, u32 cr4, u32 efer,
				    union kvm_mmu_role new_role)
{
	if (!(cr0 & X86_CR0_PG))
		nonpaging_init_context(vcpu, context);
	else if (efer & EFER_LMA)
		paging64_init_context(vcpu, context);
	else if (cr4 & X86_CR4_PAE)
		paging32E_init_context(vcpu, context);
	else
		paging32_init_context(vcpu, context);

	context->mmu_role.as_u64 = new_role.as_u64;
	reset_shadow_zero_bits_mask(vcpu, context);
}

static void kvm_init_shadow_mmu(struct kvm_vcpu *vcpu, u32 cr0, u32 cr4, u32 efer)
{
	struct kvm_mmu *context = &vcpu->arch.root_mmu;
	union kvm_mmu_role new_role =
		kvm_calc_shadow_mmu_root_page_role(vcpu, false);

	if (new_role.as_u64 != context->mmu_role.as_u64)
		shadow_mmu_init_context(vcpu, context, cr0, cr4, efer, new_role);
}

static union kvm_mmu_role
kvm_calc_shadow_npt_root_page_role(struct kvm_vcpu *vcpu)
{
	union kvm_mmu_role role =
		kvm_calc_shadow_root_page_role_common(vcpu, false);

	role.base.direct = false;
	role.base.level = kvm_mmu_get_tdp_level(vcpu);

	return role;
}

void kvm_init_shadow_npt_mmu(struct kvm_vcpu *vcpu, u32 cr0, u32 cr4, u32 efer,
			     gpa_t nested_cr3)
{
	struct kvm_mmu *context = &vcpu->arch.guest_mmu;
	union kvm_mmu_role new_role = kvm_calc_shadow_npt_root_page_role(vcpu);

	context->shadow_root_level = new_role.base.level;

	__kvm_mmu_new_pgd(vcpu, nested_cr3, new_role.base, false, false);

	if (new_role.as_u64 != context->mmu_role.as_u64)
		shadow_mmu_init_context(vcpu, context, cr0, cr4, efer, new_role);
}
EXPORT_SYMBOL_GPL(kvm_init_shadow_npt_mmu);

static union kvm_mmu_role
kvm_calc_shadow_ept_root_page_role(struct kvm_vcpu *vcpu, bool accessed_dirty,
				   bool execonly, u8 level)
{
	union kvm_mmu_role role = {0};

	/* SMM flag is inherited from root_mmu */
	role.base.smm = vcpu->arch.root_mmu.mmu_role.base.smm;

	role.base.level = level;
	role.base.gpte_is_8_bytes = true;
	role.base.direct = false;
	role.base.ad_disabled = !accessed_dirty;
	role.base.guest_mode = true;
	role.base.access = ACC_ALL;

	/*
	 * WP=1 and NOT_WP=1 is an impossible combination, use WP and the
	 * SMAP variation to denote shadow EPT entries.
	 */
	role.base.cr0_wp = true;
	role.base.smap_andnot_wp = true;

	role.ext = kvm_calc_mmu_role_ext(vcpu);
	role.ext.execonly = execonly;

	return role;
}

void kvm_init_shadow_ept_mmu(struct kvm_vcpu *vcpu, bool execonly,
			     bool accessed_dirty, gpa_t new_eptp)
{
	struct kvm_mmu *context = &vcpu->arch.guest_mmu;
	u8 level = vmx_eptp_page_walk_level(new_eptp);
	union kvm_mmu_role new_role =
		kvm_calc_shadow_ept_root_page_role(vcpu, accessed_dirty,
						   execonly, level);

	__kvm_mmu_new_pgd(vcpu, new_eptp, new_role.base, true, true);

	if (new_role.as_u64 == context->mmu_role.as_u64)
		return;

	context->shadow_root_level = level;

	context->nx = true;
	context->ept_ad = accessed_dirty;
	context->page_fault = ept_page_fault;
	context->gva_to_gpa = ept_gva_to_gpa;
	context->sync_page = ept_sync_page;
	context->invlpg = ept_invlpg;
	context->update_pte = ept_update_pte;
	context->root_level = level;
	context->direct_map = false;
	context->mmu_role.as_u64 = new_role.as_u64;

	update_permission_bitmask(vcpu, context, true);
	update_pkru_bitmask(vcpu, context, true);
	update_last_nonleaf_level(vcpu, context);
	reset_rsvds_bits_mask_ept(vcpu, context, execonly);
	reset_ept_shadow_zero_bits_mask(vcpu, context, execonly);
}
EXPORT_SYMBOL_GPL(kvm_init_shadow_ept_mmu);

static void init_kvm_softmmu(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->arch.root_mmu;

	kvm_init_shadow_mmu(vcpu,
			    kvm_read_cr0_bits(vcpu, X86_CR0_PG),
			    kvm_read_cr4_bits(vcpu, X86_CR4_PAE),
			    vcpu->arch.efer);

	context->get_guest_pgd     = get_cr3;
	context->get_pdptr         = kvm_pdptr_read;
	context->inject_page_fault = kvm_inject_page_fault;
}

static void init_kvm_nested_mmu(struct kvm_vcpu *vcpu)
{
	union kvm_mmu_role new_role = kvm_calc_mmu_role_common(vcpu, false);
	struct kvm_mmu *g_context = &vcpu->arch.nested_mmu;

	if (new_role.as_u64 == g_context->mmu_role.as_u64)
		return;

	g_context->mmu_role.as_u64 = new_role.as_u64;
	g_context->get_guest_pgd     = get_cr3;
	g_context->get_pdptr         = kvm_pdptr_read;
	g_context->inject_page_fault = kvm_inject_page_fault;

	/*
	 * L2 page tables are never shadowed, so there is no need to sync
	 * SPTEs.
	 */
	g_context->invlpg            = NULL;

	/*
	 * Note that arch.mmu->gva_to_gpa translates l2_gpa to l1_gpa using
	 * L1's nested page tables (e.g. EPT12). The nested translation
	 * of l2_gva to l1_gpa is done by arch.nested_mmu.gva_to_gpa using
	 * L2's page tables as the first level of translation and L1's
	 * nested page tables as the second level of translation. Basically
	 * the gva_to_gpa functions between mmu and nested_mmu are swapped.
	 */
	if (!is_paging(vcpu)) {
		g_context->nx = false;
		g_context->root_level = 0;
		g_context->gva_to_gpa = nonpaging_gva_to_gpa_nested;
	} else if (is_long_mode(vcpu)) {
		g_context->nx = is_nx(vcpu);
		g_context->root_level = is_la57_mode(vcpu) ?
					PT64_ROOT_5LEVEL : PT64_ROOT_4LEVEL;
		reset_rsvds_bits_mask(vcpu, g_context);
		g_context->gva_to_gpa = paging64_gva_to_gpa_nested;
	} else if (is_pae(vcpu)) {
		g_context->nx = is_nx(vcpu);
		g_context->root_level = PT32E_ROOT_LEVEL;
		reset_rsvds_bits_mask(vcpu, g_context);
		g_context->gva_to_gpa = paging64_gva_to_gpa_nested;
	} else {
		g_context->nx = false;
		g_context->root_level = PT32_ROOT_LEVEL;
		reset_rsvds_bits_mask(vcpu, g_context);
		g_context->gva_to_gpa = paging32_gva_to_gpa_nested;
	}

	update_permission_bitmask(vcpu, g_context, false);
	update_pkru_bitmask(vcpu, g_context, false);
	update_last_nonleaf_level(vcpu, g_context);
}

void kvm_init_mmu(struct kvm_vcpu *vcpu, bool reset_roots)
{
	if (reset_roots) {
		uint i;

		vcpu->arch.mmu->root_hpa = INVALID_PAGE;

		for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
			vcpu->arch.mmu->prev_roots[i] = KVM_MMU_ROOT_INFO_INVALID;
	}

	if (mmu_is_nested(vcpu))
		init_kvm_nested_mmu(vcpu);
	else if (tdp_enabled)
		init_kvm_tdp_mmu(vcpu);
	else
		init_kvm_softmmu(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_init_mmu);

static union kvm_mmu_page_role
kvm_mmu_calc_root_page_role(struct kvm_vcpu *vcpu)
{
	union kvm_mmu_role role;

	if (tdp_enabled)
		role = kvm_calc_tdp_mmu_root_page_role(vcpu, true);
	else
		role = kvm_calc_shadow_mmu_root_page_role(vcpu, true);

	return role.base;
}

void kvm_mmu_reset_context(struct kvm_vcpu *vcpu)
{
	kvm_mmu_unload(vcpu);
	kvm_init_mmu(vcpu, true);
}
EXPORT_SYMBOL_GPL(kvm_mmu_reset_context);

int kvm_mmu_load(struct kvm_vcpu *vcpu)
{
	int r;

	r = mmu_topup_memory_caches(vcpu, !vcpu->arch.mmu->direct_map);
	if (r)
		goto out;
	r = mmu_alloc_roots(vcpu);
	kvm_mmu_sync_roots(vcpu);
	if (r)
		goto out;
	kvm_mmu_load_pgd(vcpu);
	kvm_x86_ops.tlb_flush_current(vcpu);
out:
	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_load);

void kvm_mmu_unload(struct kvm_vcpu *vcpu)
{
	kvm_mmu_free_roots(vcpu, &vcpu->arch.root_mmu, KVM_MMU_ROOTS_ALL);
	WARN_ON(VALID_PAGE(vcpu->arch.root_mmu.root_hpa));
	kvm_mmu_free_roots(vcpu, &vcpu->arch.guest_mmu, KVM_MMU_ROOTS_ALL);
	WARN_ON(VALID_PAGE(vcpu->arch.guest_mmu.root_hpa));
}
EXPORT_SYMBOL_GPL(kvm_mmu_unload);

static void mmu_pte_write_new_pte(struct kvm_vcpu *vcpu,
				  struct kvm_mmu_page *sp, u64 *spte,
				  const void *new)
{
	if (sp->role.level != PG_LEVEL_4K) {
		++vcpu->kvm->stat.mmu_pde_zapped;
		return;
        }

	++vcpu->kvm->stat.mmu_pte_updated;
	vcpu->arch.mmu->update_pte(vcpu, sp, spte, new);
}

static bool need_remote_flush(u64 old, u64 new)
{
	if (!is_shadow_present_pte(old))
		return false;
	if (!is_shadow_present_pte(new))
		return true;
	if ((old ^ new) & PT64_BASE_ADDR_MASK)
		return true;
	old ^= shadow_nx_mask;
	new ^= shadow_nx_mask;
	return (old & ~new & PT64_PERM_MASK) != 0;
}

static u64 mmu_pte_write_fetch_gpte(struct kvm_vcpu *vcpu, gpa_t *gpa,
				    int *bytes)
{
	u64 gentry = 0;
	int r;

	/*
	 * Assume that the pte write on a page table of the same type
	 * as the current vcpu paging mode since we update the sptes only
	 * when they have the same mode.
	 */
	if (is_pae(vcpu) && *bytes == 4) {
		/* Handle a 32-bit guest writing two halves of a 64-bit gpte */
		*gpa &= ~(gpa_t)7;
		*bytes = 8;
	}

	if (*bytes == 4 || *bytes == 8) {
		r = kvm_vcpu_read_guest_atomic(vcpu, *gpa, &gentry, *bytes);
		if (r)
			gentry = 0;
	}

	return gentry;
}

/*
 * If we're seeing too many writes to a page, it may no longer be a page table,
 * or we may be forking, in which case it is better to unmap the page.
 */
static bool detect_write_flooding(struct kvm_mmu_page *sp)
{
	/*
	 * Skip write-flooding detected for the sp whose level is 1, because
	 * it can become unsync, then the guest page is not write-protected.
	 */
	if (sp->role.level == PG_LEVEL_4K)
		return false;

	atomic_inc(&sp->write_flooding_count);
	return atomic_read(&sp->write_flooding_count) >= 3;
}

/*
 * Misaligned accesses are too much trouble to fix up; also, they usually
 * indicate a page is not used as a page table.
 */
static bool detect_write_misaligned(struct kvm_mmu_page *sp, gpa_t gpa,
				    int bytes)
{
	unsigned offset, pte_size, misaligned;

	pgprintk("misaligned: gpa %llx bytes %d role %x\n",
		 gpa, bytes, sp->role.word);

	offset = offset_in_page(gpa);
	pte_size = sp->role.gpte_is_8_bytes ? 8 : 4;

	/*
	 * Sometimes, the OS only writes the last one bytes to update status
	 * bits, for example, in linux, andb instruction is used in clear_bit().
	 */
	if (!(offset & (pte_size - 1)) && bytes == 1)
		return false;

	misaligned = (offset ^ (offset + bytes - 1)) & ~(pte_size - 1);
	misaligned |= bytes < 4;

	return misaligned;
}

static u64 *get_written_sptes(struct kvm_mmu_page *sp, gpa_t gpa, int *nspte)
{
	unsigned page_offset, quadrant;
	u64 *spte;
	int level;

	page_offset = offset_in_page(gpa);
	level = sp->role.level;
	*nspte = 1;
	if (!sp->role.gpte_is_8_bytes) {
		page_offset <<= 1;	/* 32->64 */
		/*
		 * A 32-bit pde maps 4MB while the shadow pdes map
		 * only 2MB.  So we need to double the offset again
		 * and zap two pdes instead of one.
		 */
		if (level == PT32_ROOT_LEVEL) {
			page_offset &= ~7; /* kill rounding error */
			page_offset <<= 1;
			*nspte = 2;
		}
		quadrant = page_offset >> PAGE_SHIFT;
		page_offset &= ~PAGE_MASK;
		if (quadrant != sp->role.quadrant)
			return NULL;
	}

	spte = &sp->spt[page_offset / sizeof(*spte)];
	return spte;
}

/*
 * Ignore various flags when determining if a SPTE can be immediately
 * overwritten for the current MMU.
 *  - level: explicitly checked in mmu_pte_write_new_pte(), and will never
 *    match the current MMU role, as MMU's level tracks the root level.
 *  - access: updated based on the new guest PTE
 *  - quadrant: handled by get_written_sptes()
 *  - invalid: always false (loop only walks valid shadow pages)
 */
static const union kvm_mmu_page_role role_ign = {
	.level = 0xf,
	.access = 0x7,
	.quadrant = 0x3,
	.invalid = 0x1,
};

static void kvm_mmu_pte_write(struct kvm_vcpu *vcpu, gpa_t gpa,
			      const u8 *new, int bytes,
			      struct kvm_page_track_notifier_node *node)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct kvm_mmu_page *sp;
	LIST_HEAD(invalid_list);
	u64 entry, gentry, *spte;
	int npte;
	bool remote_flush, local_flush;

	/*
	 * If we don't have indirect shadow pages, it means no page is
	 * write-protected, so we can exit simply.
	 */
	if (!READ_ONCE(vcpu->kvm->arch.indirect_shadow_pages))
		return;

	remote_flush = local_flush = false;

	pgprintk("%s: gpa %llx bytes %d\n", __func__, gpa, bytes);

	/*
	 * No need to care whether allocation memory is successful
	 * or not since pte prefetch is skiped if it does not have
	 * enough objects in the cache.
	 */
	mmu_topup_memory_caches(vcpu, true);

	spin_lock(&vcpu->kvm->mmu_lock);

	gentry = mmu_pte_write_fetch_gpte(vcpu, &gpa, &bytes);

	++vcpu->kvm->stat.mmu_pte_write;
	kvm_mmu_audit(vcpu, AUDIT_PRE_PTE_WRITE);

	for_each_gfn_indirect_valid_sp(vcpu->kvm, sp, gfn) {
		if (detect_write_misaligned(sp, gpa, bytes) ||
		      detect_write_flooding(sp)) {
			kvm_mmu_prepare_zap_page(vcpu->kvm, sp, &invalid_list);
			++vcpu->kvm->stat.mmu_flooded;
			continue;
		}

		spte = get_written_sptes(sp, gpa, &npte);
		if (!spte)
			continue;

		local_flush = true;
		while (npte--) {
			u32 base_role = vcpu->arch.mmu->mmu_role.base.word;

			entry = *spte;
			mmu_page_zap_pte(vcpu->kvm, sp, spte);
			if (gentry &&
			    !((sp->role.word ^ base_role) & ~role_ign.word) &&
			    rmap_can_add(vcpu))
				mmu_pte_write_new_pte(vcpu, sp, spte, &gentry);
			if (need_remote_flush(entry, *spte))
				remote_flush = true;
			++spte;
		}
	}
	kvm_mmu_flush_or_zap(vcpu, &invalid_list, remote_flush, local_flush);
	kvm_mmu_audit(vcpu, AUDIT_POST_PTE_WRITE);
	spin_unlock(&vcpu->kvm->mmu_lock);
}

int kvm_mmu_unprotect_page_virt(struct kvm_vcpu *vcpu, gva_t gva)
{
	gpa_t gpa;
	int r;

	if (vcpu->arch.mmu->direct_map)
		return 0;

	gpa = kvm_mmu_gva_to_gpa_read(vcpu, gva, NULL);

	r = kvm_mmu_unprotect_page(vcpu->kvm, gpa >> PAGE_SHIFT);

	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_unprotect_page_virt);

int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa, u64 error_code,
		       void *insn, int insn_len)
{
	int r, emulation_type = EMULTYPE_PF;
	bool direct = vcpu->arch.mmu->direct_map;

	if (WARN_ON(!VALID_PAGE(vcpu->arch.mmu->root_hpa)))
		return RET_PF_RETRY;

	r = RET_PF_INVALID;
	if (unlikely(error_code & PFERR_RSVD_MASK)) {
		r = handle_mmio_page_fault(vcpu, cr2_or_gpa, direct);
		if (r == RET_PF_EMULATE)
			goto emulate;
	}

	if (r == RET_PF_INVALID) {
		r = kvm_mmu_do_page_fault(vcpu, cr2_or_gpa,
					  lower_32_bits(error_code), false);
		WARN_ON(r == RET_PF_INVALID);
	}

	if (r == RET_PF_RETRY)
		return 1;
	if (r < 0)
		return r;

	/*
	 * Before emulating the instruction, check if the error code
	 * was due to a RO violation while translating the guest page.
	 * This can occur when using nested virtualization with nested
	 * paging in both guests. If true, we simply unprotect the page
	 * and resume the guest.
	 */
	if (vcpu->arch.mmu->direct_map &&
	    (error_code & PFERR_NESTED_GUEST_PAGE) == PFERR_NESTED_GUEST_PAGE) {
		kvm_mmu_unprotect_page(vcpu->kvm, gpa_to_gfn(cr2_or_gpa));
		return 1;
	}

	/*
	 * vcpu->arch.mmu.page_fault returned RET_PF_EMULATE, but we can still
	 * optimistically try to just unprotect the page and let the processor
	 * re-execute the instruction that caused the page fault.  Do not allow
	 * retrying MMIO emulation, as it's not only pointless but could also
	 * cause us to enter an infinite loop because the processor will keep
	 * faulting on the non-existent MMIO address.  Retrying an instruction
	 * from a nested guest is also pointless and dangerous as we are only
	 * explicitly shadowing L1's page tables, i.e. unprotecting something
	 * for L1 isn't going to magically fix whatever issue cause L2 to fail.
	 */
	if (!mmio_info_in_cache(vcpu, cr2_or_gpa, direct) && !is_guest_mode(vcpu))
		emulation_type |= EMULTYPE_ALLOW_RETRY_PF;
emulate:
	/*
	 * On AMD platforms, under certain conditions insn_len may be zero on #NPF.
	 * This can happen if a guest gets a page-fault on data access but the HW
	 * table walker is not able to read the instruction page (e.g instruction
	 * page is not present in memory). In those cases we simply restart the
	 * guest, with the exception of AMD Erratum 1096 which is unrecoverable.
	 */
	if (unlikely(insn && !insn_len)) {
		if (!kvm_x86_ops.need_emulation_on_page_fault(vcpu))
			return 1;
	}

	return x86_emulate_instruction(vcpu, cr2_or_gpa, emulation_type, insn,
				       insn_len);
}
EXPORT_SYMBOL_GPL(kvm_mmu_page_fault);

void kvm_mmu_invalidate_gva(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			    gva_t gva, hpa_t root_hpa)
{
	int i;

	/* It's actually a GPA for vcpu->arch.guest_mmu.  */
	if (mmu != &vcpu->arch.guest_mmu) {
		/* INVLPG on a non-canonical address is a NOP according to the SDM.  */
		if (is_noncanonical_address(gva, vcpu))
			return;

		kvm_x86_ops.tlb_flush_gva(vcpu, gva);
	}

	if (!mmu->invlpg)
		return;

	if (root_hpa == INVALID_PAGE) {
		mmu->invlpg(vcpu, gva, mmu->root_hpa);

		/*
		 * INVLPG is required to invalidate any global mappings for the VA,
		 * irrespective of PCID. Since it would take us roughly similar amount
		 * of work to determine whether any of the prev_root mappings of the VA
		 * is marked global, or to just sync it blindly, so we might as well
		 * just always sync it.
		 *
		 * Mappings not reachable via the current cr3 or the prev_roots will be
		 * synced when switching to that cr3, so nothing needs to be done here
		 * for them.
		 */
		for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
			if (VALID_PAGE(mmu->prev_roots[i].hpa))
				mmu->invlpg(vcpu, gva, mmu->prev_roots[i].hpa);
	} else {
		mmu->invlpg(vcpu, gva, root_hpa);
	}
}
EXPORT_SYMBOL_GPL(kvm_mmu_invalidate_gva);

void kvm_mmu_invlpg(struct kvm_vcpu *vcpu, gva_t gva)
{
	kvm_mmu_invalidate_gva(vcpu, vcpu->arch.mmu, gva, INVALID_PAGE);
	++vcpu->stat.invlpg;
}
EXPORT_SYMBOL_GPL(kvm_mmu_invlpg);


void kvm_mmu_invpcid_gva(struct kvm_vcpu *vcpu, gva_t gva, unsigned long pcid)
{
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	bool tlb_flush = false;
	uint i;

	if (pcid == kvm_get_active_pcid(vcpu)) {
		mmu->invlpg(vcpu, gva, mmu->root_hpa);
		tlb_flush = true;
	}

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++) {
		if (VALID_PAGE(mmu->prev_roots[i].hpa) &&
		    pcid == kvm_get_pcid(vcpu, mmu->prev_roots[i].pgd)) {
			mmu->invlpg(vcpu, gva, mmu->prev_roots[i].hpa);
			tlb_flush = true;
		}
	}

	if (tlb_flush)
		kvm_x86_ops.tlb_flush_gva(vcpu, gva);

	++vcpu->stat.invlpg;

	/*
	 * Mappings not reachable via the current cr3 or the prev_roots will be
	 * synced when switching to that cr3, so nothing needs to be done here
	 * for them.
	 */
}
EXPORT_SYMBOL_GPL(kvm_mmu_invpcid_gva);

void kvm_configure_mmu(bool enable_tdp, int tdp_max_root_level,
		       int tdp_huge_page_level)
{
	tdp_enabled = enable_tdp;
	max_tdp_level = tdp_max_root_level;

	/*
	 * max_huge_page_level reflects KVM's MMU capabilities irrespective
	 * of kernel support, e.g. KVM may be capable of using 1GB pages when
	 * the kernel is not.  But, KVM never creates a page size greater than
	 * what is used by the kernel for any given HVA, i.e. the kernel's
	 * capabilities are ultimately consulted by kvm_mmu_hugepage_adjust().
	 */
	if (tdp_enabled)
		max_huge_page_level = tdp_huge_page_level;
	else if (boot_cpu_has(X86_FEATURE_GBPAGES))
		max_huge_page_level = PG_LEVEL_1G;
	else
		max_huge_page_level = PG_LEVEL_2M;
}
EXPORT_SYMBOL_GPL(kvm_configure_mmu);

/* The return value indicates if tlb flush on all vcpus is needed. */
typedef bool (*slot_level_handler) (struct kvm *kvm, struct kvm_rmap_head *rmap_head);

/* The caller should hold mmu-lock before calling this function. */
static __always_inline bool
slot_handle_level_range(struct kvm *kvm, struct kvm_memory_slot *memslot,
			slot_level_handler fn, int start_level, int end_level,
			gfn_t start_gfn, gfn_t end_gfn, bool lock_flush_tlb)
{
	struct slot_rmap_walk_iterator iterator;
	bool flush = false;

	for_each_slot_rmap_range(memslot, start_level, end_level, start_gfn,
			end_gfn, &iterator) {
		if (iterator.rmap)
			flush |= fn(kvm, iterator.rmap);

		if (need_resched() || spin_needbreak(&kvm->mmu_lock)) {
			if (flush && lock_flush_tlb) {
				kvm_flush_remote_tlbs_with_address(kvm,
						start_gfn,
						iterator.gfn - start_gfn + 1);
				flush = false;
			}
			cond_resched_lock(&kvm->mmu_lock);
		}
	}

	if (flush && lock_flush_tlb) {
		kvm_flush_remote_tlbs_with_address(kvm, start_gfn,
						   end_gfn - start_gfn + 1);
		flush = false;
	}

	return flush;
}

static __always_inline bool
slot_handle_level(struct kvm *kvm, struct kvm_memory_slot *memslot,
		  slot_level_handler fn, int start_level, int end_level,
		  bool lock_flush_tlb)
{
	return slot_handle_level_range(kvm, memslot, fn, start_level,
			end_level, memslot->base_gfn,
			memslot->base_gfn + memslot->npages - 1,
			lock_flush_tlb);
}

static __always_inline bool
slot_handle_all_level(struct kvm *kvm, struct kvm_memory_slot *memslot,
		      slot_level_handler fn, bool lock_flush_tlb)
{
	return slot_handle_level(kvm, memslot, fn, PG_LEVEL_4K,
				 KVM_MAX_HUGEPAGE_LEVEL, lock_flush_tlb);
}

static __always_inline bool
slot_handle_large_level(struct kvm *kvm, struct kvm_memory_slot *memslot,
			slot_level_handler fn, bool lock_flush_tlb)
{
	return slot_handle_level(kvm, memslot, fn, PG_LEVEL_4K + 1,
				 KVM_MAX_HUGEPAGE_LEVEL, lock_flush_tlb);
}

static __always_inline bool
slot_handle_leaf(struct kvm *kvm, struct kvm_memory_slot *memslot,
		 slot_level_handler fn, bool lock_flush_tlb)
{
	return slot_handle_level(kvm, memslot, fn, PG_LEVEL_4K,
				 PG_LEVEL_4K, lock_flush_tlb);
}

static void free_mmu_pages(struct kvm_mmu *mmu)
{
	free_page((unsigned long)mmu->pae_root);
	free_page((unsigned long)mmu->lm_root);
}

static int alloc_mmu_pages(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu)
{
	struct page *page;
	int i;

	/*
	 * When using PAE paging, the four PDPTEs are treated as 'root' pages,
	 * while the PDP table is a per-vCPU construct that's allocated at MMU
	 * creation.  When emulating 32-bit mode, cr3 is only 32 bits even on
	 * x86_64.  Therefore we need to allocate the PDP table in the first
	 * 4GB of memory, which happens to fit the DMA32 zone.  Except for
	 * SVM's 32-bit NPT support, TDP paging doesn't use PAE paging and can
	 * skip allocating the PDP table.
	 */
	if (tdp_enabled && kvm_mmu_get_tdp_level(vcpu) > PT32E_ROOT_LEVEL)
		return 0;

	page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_DMA32);
	if (!page)
		return -ENOMEM;

	mmu->pae_root = page_address(page);
	for (i = 0; i < 4; ++i)
		mmu->pae_root[i] = INVALID_PAGE;

	return 0;
}

int kvm_mmu_create(struct kvm_vcpu *vcpu)
{
	uint i;
	int ret;

	vcpu->arch.mmu_pte_list_desc_cache.kmem_cache = pte_list_desc_cache;
	vcpu->arch.mmu_pte_list_desc_cache.gfp_zero = __GFP_ZERO;

	vcpu->arch.mmu_page_header_cache.kmem_cache = mmu_page_header_cache;
	vcpu->arch.mmu_page_header_cache.gfp_zero = __GFP_ZERO;

	vcpu->arch.mmu_shadow_page_cache.gfp_zero = __GFP_ZERO;

	vcpu->arch.mmu = &vcpu->arch.root_mmu;
	vcpu->arch.walk_mmu = &vcpu->arch.root_mmu;

	vcpu->arch.root_mmu.root_hpa = INVALID_PAGE;
	vcpu->arch.root_mmu.root_pgd = 0;
	vcpu->arch.root_mmu.translate_gpa = translate_gpa;
	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
		vcpu->arch.root_mmu.prev_roots[i] = KVM_MMU_ROOT_INFO_INVALID;

	vcpu->arch.guest_mmu.root_hpa = INVALID_PAGE;
	vcpu->arch.guest_mmu.root_pgd = 0;
	vcpu->arch.guest_mmu.translate_gpa = translate_gpa;
	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
		vcpu->arch.guest_mmu.prev_roots[i] = KVM_MMU_ROOT_INFO_INVALID;

	vcpu->arch.nested_mmu.translate_gpa = translate_nested_gpa;

	ret = alloc_mmu_pages(vcpu, &vcpu->arch.guest_mmu);
	if (ret)
		return ret;

	ret = alloc_mmu_pages(vcpu, &vcpu->arch.root_mmu);
	if (ret)
		goto fail_allocate_root;

	return ret;
 fail_allocate_root:
	free_mmu_pages(&vcpu->arch.guest_mmu);
	return ret;
}

#define BATCH_ZAP_PAGES	10
static void kvm_zap_obsolete_pages(struct kvm *kvm)
{
	struct kvm_mmu_page *sp, *node;
	int nr_zapped, batch = 0;

restart:
	list_for_each_entry_safe_reverse(sp, node,
	      &kvm->arch.active_mmu_pages, link) {
		/*
		 * No obsolete valid page exists before a newly created page
		 * since active_mmu_pages is a FIFO list.
		 */
		if (!is_obsolete_sp(kvm, sp))
			break;

		/*
		 * Invalid pages should never land back on the list of active
		 * pages.  Skip the bogus page, otherwise we'll get stuck in an
		 * infinite loop if the page gets put back on the list (again).
		 */
		if (WARN_ON(sp->role.invalid))
			continue;

		/*
		 * No need to flush the TLB since we're only zapping shadow
		 * pages with an obsolete generation number and all vCPUS have
		 * loaded a new root, i.e. the shadow pages being zapped cannot
		 * be in active use by the guest.
		 */
		if (batch >= BATCH_ZAP_PAGES &&
		    cond_resched_lock(&kvm->mmu_lock)) {
			batch = 0;
			goto restart;
		}

		if (__kvm_mmu_prepare_zap_page(kvm, sp,
				&kvm->arch.zapped_obsolete_pages, &nr_zapped)) {
			batch += nr_zapped;
			goto restart;
		}
	}

	/*
	 * Trigger a remote TLB flush before freeing the page tables to ensure
	 * KVM is not in the middle of a lockless shadow page table walk, which
	 * may reference the pages.
	 */
	kvm_mmu_commit_zap_page(kvm, &kvm->arch.zapped_obsolete_pages);
}

/*
 * Fast invalidate all shadow pages and use lock-break technique
 * to zap obsolete pages.
 *
 * It's required when memslot is being deleted or VM is being
 * destroyed, in these cases, we should ensure that KVM MMU does
 * not use any resource of the being-deleted slot or all slots
 * after calling the function.
 */
static void kvm_mmu_zap_all_fast(struct kvm *kvm)
{
	lockdep_assert_held(&kvm->slots_lock);

	spin_lock(&kvm->mmu_lock);
	trace_kvm_mmu_zap_all_fast(kvm);

	/*
	 * Toggle mmu_valid_gen between '0' and '1'.  Because slots_lock is
	 * held for the entire duration of zapping obsolete pages, it's
	 * impossible for there to be multiple invalid generations associated
	 * with *valid* shadow pages at any given time, i.e. there is exactly
	 * one valid generation and (at most) one invalid generation.
	 */
	kvm->arch.mmu_valid_gen = kvm->arch.mmu_valid_gen ? 0 : 1;

	/*
	 * Notify all vcpus to reload its shadow page table and flush TLB.
	 * Then all vcpus will switch to new shadow page table with the new
	 * mmu_valid_gen.
	 *
	 * Note: we need to do this under the protection of mmu_lock,
	 * otherwise, vcpu would purge shadow page but miss tlb flush.
	 */
	kvm_reload_remote_mmus(kvm);

	kvm_zap_obsolete_pages(kvm);
	spin_unlock(&kvm->mmu_lock);
}

static bool kvm_has_zapped_obsolete_pages(struct kvm *kvm)
{
	return unlikely(!list_empty_careful(&kvm->arch.zapped_obsolete_pages));
}

static void kvm_mmu_invalidate_zap_pages_in_memslot(struct kvm *kvm,
			struct kvm_memory_slot *slot,
			struct kvm_page_track_notifier_node *node)
{
	kvm_mmu_zap_all_fast(kvm);
}

void kvm_mmu_init_vm(struct kvm *kvm)
{
	struct kvm_page_track_notifier_node *node = &kvm->arch.mmu_sp_tracker;

	node->track_write = kvm_mmu_pte_write;
	node->track_flush_slot = kvm_mmu_invalidate_zap_pages_in_memslot;
	kvm_page_track_register_notifier(kvm, node);
}

void kvm_mmu_uninit_vm(struct kvm *kvm)
{
	struct kvm_page_track_notifier_node *node = &kvm->arch.mmu_sp_tracker;

	kvm_page_track_unregister_notifier(kvm, node);
}

void kvm_zap_gfn_range(struct kvm *kvm, gfn_t gfn_start, gfn_t gfn_end)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int i;

	spin_lock(&kvm->mmu_lock);
	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++) {
		slots = __kvm_memslots(kvm, i);
		kvm_for_each_memslot(memslot, slots) {
			gfn_t start, end;

			start = max(gfn_start, memslot->base_gfn);
			end = min(gfn_end, memslot->base_gfn + memslot->npages);
			if (start >= end)
				continue;

			slot_handle_level_range(kvm, memslot, kvm_zap_rmapp,
						PG_LEVEL_4K,
						KVM_MAX_HUGEPAGE_LEVEL,
						start, end - 1, true);
		}
	}

	spin_unlock(&kvm->mmu_lock);
}

static bool slot_rmap_write_protect(struct kvm *kvm,
				    struct kvm_rmap_head *rmap_head)
{
	return __rmap_write_protect(kvm, rmap_head, false);
}

void kvm_mmu_slot_remove_write_access(struct kvm *kvm,
				      struct kvm_memory_slot *memslot,
				      int start_level)
{
	bool flush;

	spin_lock(&kvm->mmu_lock);
	flush = slot_handle_level(kvm, memslot, slot_rmap_write_protect,
				start_level, KVM_MAX_HUGEPAGE_LEVEL, false);
	spin_unlock(&kvm->mmu_lock);

	/*
	 * We can flush all the TLBs out of the mmu lock without TLB
	 * corruption since we just change the spte from writable to
	 * readonly so that we only need to care the case of changing
	 * spte from present to present (changing the spte from present
	 * to nonpresent will flush all the TLBs immediately), in other
	 * words, the only case we care is mmu_spte_update() where we
	 * have checked SPTE_HOST_WRITEABLE | SPTE_MMU_WRITEABLE
	 * instead of PT_WRITABLE_MASK, that means it does not depend
	 * on PT_WRITABLE_MASK anymore.
	 */
	if (flush)
		kvm_arch_flush_remote_tlbs_memslot(kvm, memslot);
}

static bool kvm_mmu_zap_collapsible_spte(struct kvm *kvm,
					 struct kvm_rmap_head *rmap_head)
{
	u64 *sptep;
	struct rmap_iterator iter;
	int need_tlb_flush = 0;
	kvm_pfn_t pfn;
	struct kvm_mmu_page *sp;

restart:
	for_each_rmap_spte(rmap_head, &iter, sptep) {
		sp = sptep_to_sp(sptep);
		pfn = spte_to_pfn(*sptep);

		/*
		 * We cannot do huge page mapping for indirect shadow pages,
		 * which are found on the last rmap (level = 1) when not using
		 * tdp; such shadow pages are synced with the page table in
		 * the guest, and the guest page table is using 4K page size
		 * mapping if the indirect sp has level = 1.
		 */
		if (sp->role.direct && !kvm_is_reserved_pfn(pfn) &&
		    (kvm_is_zone_device_pfn(pfn) ||
		     PageCompound(pfn_to_page(pfn)))) {
			pte_list_remove(rmap_head, sptep);

			if (kvm_available_flush_tlb_with_range())
				kvm_flush_remote_tlbs_with_address(kvm, sp->gfn,
					KVM_PAGES_PER_HPAGE(sp->role.level));
			else
				need_tlb_flush = 1;

			goto restart;
		}
	}

	return need_tlb_flush;
}

void kvm_mmu_zap_collapsible_sptes(struct kvm *kvm,
				   const struct kvm_memory_slot *memslot)
{
	/* FIXME: const-ify all uses of struct kvm_memory_slot.  */
	spin_lock(&kvm->mmu_lock);
	slot_handle_leaf(kvm, (struct kvm_memory_slot *)memslot,
			 kvm_mmu_zap_collapsible_spte, true);
	spin_unlock(&kvm->mmu_lock);
}

void kvm_arch_flush_remote_tlbs_memslot(struct kvm *kvm,
					struct kvm_memory_slot *memslot)
{
	/*
	 * All current use cases for flushing the TLBs for a specific memslot
	 * are related to dirty logging, and do the TLB flush out of mmu_lock.
	 * The interaction between the various operations on memslot must be
	 * serialized by slots_locks to ensure the TLB flush from one operation
	 * is observed by any other operation on the same memslot.
	 */
	lockdep_assert_held(&kvm->slots_lock);
	kvm_flush_remote_tlbs_with_address(kvm, memslot->base_gfn,
					   memslot->npages);
}

void kvm_mmu_slot_leaf_clear_dirty(struct kvm *kvm,
				   struct kvm_memory_slot *memslot)
{
	bool flush;

	spin_lock(&kvm->mmu_lock);
	flush = slot_handle_leaf(kvm, memslot, __rmap_clear_dirty, false);
	spin_unlock(&kvm->mmu_lock);

	/*
	 * It's also safe to flush TLBs out of mmu lock here as currently this
	 * function is only used for dirty logging, in which case flushing TLB
	 * out of mmu lock also guarantees no dirty pages will be lost in
	 * dirty_bitmap.
	 */
	if (flush)
		kvm_arch_flush_remote_tlbs_memslot(kvm, memslot);
}
EXPORT_SYMBOL_GPL(kvm_mmu_slot_leaf_clear_dirty);

void kvm_mmu_slot_largepage_remove_write_access(struct kvm *kvm,
					struct kvm_memory_slot *memslot)
{
	bool flush;

	spin_lock(&kvm->mmu_lock);
	flush = slot_handle_large_level(kvm, memslot, slot_rmap_write_protect,
					false);
	spin_unlock(&kvm->mmu_lock);

	if (flush)
		kvm_arch_flush_remote_tlbs_memslot(kvm, memslot);
}
EXPORT_SYMBOL_GPL(kvm_mmu_slot_largepage_remove_write_access);

void kvm_mmu_slot_set_dirty(struct kvm *kvm,
			    struct kvm_memory_slot *memslot)
{
	bool flush;

	spin_lock(&kvm->mmu_lock);
	flush = slot_handle_all_level(kvm, memslot, __rmap_set_dirty, false);
	spin_unlock(&kvm->mmu_lock);

	if (flush)
		kvm_arch_flush_remote_tlbs_memslot(kvm, memslot);
}
EXPORT_SYMBOL_GPL(kvm_mmu_slot_set_dirty);

void kvm_mmu_zap_all(struct kvm *kvm)
{
	struct kvm_mmu_page *sp, *node;
	LIST_HEAD(invalid_list);
	int ign;

	spin_lock(&kvm->mmu_lock);
restart:
	list_for_each_entry_safe(sp, node, &kvm->arch.active_mmu_pages, link) {
		if (WARN_ON(sp->role.invalid))
			continue;
		if (__kvm_mmu_prepare_zap_page(kvm, sp, &invalid_list, &ign))
			goto restart;
		if (cond_resched_lock(&kvm->mmu_lock))
			goto restart;
	}

	kvm_mmu_commit_zap_page(kvm, &invalid_list);
	spin_unlock(&kvm->mmu_lock);
}

void kvm_mmu_invalidate_mmio_sptes(struct kvm *kvm, u64 gen)
{
	WARN_ON(gen & KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS);

	gen &= MMIO_SPTE_GEN_MASK;

	/*
	 * Generation numbers are incremented in multiples of the number of
	 * address spaces in order to provide unique generations across all
	 * address spaces.  Strip what is effectively the address space
	 * modifier prior to checking for a wrap of the MMIO generation so
	 * that a wrap in any address space is detected.
	 */
	gen &= ~((u64)KVM_ADDRESS_SPACE_NUM - 1);

	/*
	 * The very rare case: if the MMIO generation number has wrapped,
	 * zap all shadow pages.
	 */
	if (unlikely(gen == 0)) {
		kvm_debug_ratelimited("kvm: zapping shadow pages for mmio generation wraparound\n");
		kvm_mmu_zap_all_fast(kvm);
	}
}

static unsigned long
mmu_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	struct kvm *kvm;
	int nr_to_scan = sc->nr_to_scan;
	unsigned long freed = 0;

	mutex_lock(&kvm_lock);

	list_for_each_entry(kvm, &vm_list, vm_list) {
		int idx;
		LIST_HEAD(invalid_list);

		/*
		 * Never scan more than sc->nr_to_scan VM instances.
		 * Will not hit this condition practically since we do not try
		 * to shrink more than one VM and it is very unlikely to see
		 * !n_used_mmu_pages so many times.
		 */
		if (!nr_to_scan--)
			break;
		/*
		 * n_used_mmu_pages is accessed without holding kvm->mmu_lock
		 * here. We may skip a VM instance errorneosly, but we do not
		 * want to shrink a VM that only started to populate its MMU
		 * anyway.
		 */
		if (!kvm->arch.n_used_mmu_pages &&
		    !kvm_has_zapped_obsolete_pages(kvm))
			continue;

		idx = srcu_read_lock(&kvm->srcu);
		spin_lock(&kvm->mmu_lock);

		if (kvm_has_zapped_obsolete_pages(kvm)) {
			kvm_mmu_commit_zap_page(kvm,
			      &kvm->arch.zapped_obsolete_pages);
			goto unlock;
		}

		freed = kvm_mmu_zap_oldest_mmu_pages(kvm, sc->nr_to_scan);

unlock:
		spin_unlock(&kvm->mmu_lock);
		srcu_read_unlock(&kvm->srcu, idx);

		/*
		 * unfair on small ones
		 * per-vm shrinkers cry out
		 * sadness comes quickly
		 */
		list_move_tail(&kvm->vm_list, &vm_list);
		break;
	}

	mutex_unlock(&kvm_lock);
	return freed;
}

static unsigned long
mmu_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	return percpu_counter_read_positive(&kvm_total_used_mmu_pages);
}

static struct shrinker mmu_shrinker = {
	.count_objects = mmu_shrink_count,
	.scan_objects = mmu_shrink_scan,
	.seeks = DEFAULT_SEEKS * 10,
};

static void mmu_destroy_caches(void)
{
	kmem_cache_destroy(pte_list_desc_cache);
	kmem_cache_destroy(mmu_page_header_cache);
}

static void kvm_set_mmio_spte_mask(void)
{
	u64 mask;

	/*
	 * Set a reserved PA bit in MMIO SPTEs to generate page faults with
	 * PFEC.RSVD=1 on MMIO accesses.  64-bit PTEs (PAE, x86-64, and EPT
	 * paging) support a maximum of 52 bits of PA, i.e. if the CPU supports
	 * 52-bit physical addresses then there are no reserved PA bits in the
	 * PTEs and so the reserved PA approach must be disabled.
	 */
	if (shadow_phys_bits < 52)
		mask = BIT_ULL(51) | PT_PRESENT_MASK;
	else
		mask = 0;

	kvm_mmu_set_mmio_spte_mask(mask, ACC_WRITE_MASK | ACC_USER_MASK);
}

static bool get_nx_auto_mode(void)
{
	/* Return true when CPU has the bug, and mitigations are ON */
	return boot_cpu_has_bug(X86_BUG_ITLB_MULTIHIT) && !cpu_mitigations_off();
}

static void __set_nx_huge_pages(bool val)
{
	nx_huge_pages = itlb_multihit_kvm_mitigation = val;
}

static int set_nx_huge_pages(const char *val, const struct kernel_param *kp)
{
	bool old_val = nx_huge_pages;
	bool new_val;

	/* In "auto" mode deploy workaround only if CPU has the bug. */
	if (sysfs_streq(val, "off"))
		new_val = 0;
	else if (sysfs_streq(val, "force"))
		new_val = 1;
	else if (sysfs_streq(val, "auto"))
		new_val = get_nx_auto_mode();
	else if (strtobool(val, &new_val) < 0)
		return -EINVAL;

	__set_nx_huge_pages(new_val);

	if (new_val != old_val) {
		struct kvm *kvm;

		mutex_lock(&kvm_lock);

		list_for_each_entry(kvm, &vm_list, vm_list) {
			mutex_lock(&kvm->slots_lock);
			kvm_mmu_zap_all_fast(kvm);
			mutex_unlock(&kvm->slots_lock);

			wake_up_process(kvm->arch.nx_lpage_recovery_thread);
		}
		mutex_unlock(&kvm_lock);
	}

	return 0;
}

int kvm_mmu_module_init(void)
{
	int ret = -ENOMEM;

	if (nx_huge_pages == -1)
		__set_nx_huge_pages(get_nx_auto_mode());

	/*
	 * MMU roles use union aliasing which is, generally speaking, an
	 * undefined behavior. However, we supposedly know how compilers behave
	 * and the current status quo is unlikely to change. Guardians below are
	 * supposed to let us know if the assumption becomes false.
	 */
	BUILD_BUG_ON(sizeof(union kvm_mmu_page_role) != sizeof(u32));
	BUILD_BUG_ON(sizeof(union kvm_mmu_extended_role) != sizeof(u32));
	BUILD_BUG_ON(sizeof(union kvm_mmu_role) != sizeof(u64));

	kvm_mmu_reset_all_pte_masks();

	kvm_set_mmio_spte_mask();

	pte_list_desc_cache = kmem_cache_create("pte_list_desc",
					    sizeof(struct pte_list_desc),
					    0, SLAB_ACCOUNT, NULL);
	if (!pte_list_desc_cache)
		goto out;

	mmu_page_header_cache = kmem_cache_create("kvm_mmu_page_header",
						  sizeof(struct kvm_mmu_page),
						  0, SLAB_ACCOUNT, NULL);
	if (!mmu_page_header_cache)
		goto out;

	if (percpu_counter_init(&kvm_total_used_mmu_pages, 0, GFP_KERNEL))
		goto out;

	ret = register_shrinker(&mmu_shrinker);
	if (ret)
		goto out;

	return 0;

out:
	mmu_destroy_caches();
	return ret;
}

/*
 * Calculate mmu pages needed for kvm.
 */
unsigned long kvm_mmu_calculate_default_mmu_pages(struct kvm *kvm)
{
	unsigned long nr_mmu_pages;
	unsigned long nr_pages = 0;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int i;

	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++) {
		slots = __kvm_memslots(kvm, i);

		kvm_for_each_memslot(memslot, slots)
			nr_pages += memslot->npages;
	}

	nr_mmu_pages = nr_pages * KVM_PERMILLE_MMU_PAGES / 1000;
	nr_mmu_pages = max(nr_mmu_pages, KVM_MIN_ALLOC_MMU_PAGES);

	return nr_mmu_pages;
}

void kvm_mmu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_mmu_unload(vcpu);
	free_mmu_pages(&vcpu->arch.root_mmu);
	free_mmu_pages(&vcpu->arch.guest_mmu);
	mmu_free_memory_caches(vcpu);
}

void kvm_mmu_module_exit(void)
{
	mmu_destroy_caches();
	percpu_counter_destroy(&kvm_total_used_mmu_pages);
	unregister_shrinker(&mmu_shrinker);
	mmu_audit_disable();
}

static int set_nx_huge_pages_recovery_ratio(const char *val, const struct kernel_param *kp)
{
	unsigned int old_val;
	int err;

	old_val = nx_huge_pages_recovery_ratio;
	err = param_set_uint(val, kp);
	if (err)
		return err;

	if (READ_ONCE(nx_huge_pages) &&
	    !old_val && nx_huge_pages_recovery_ratio) {
		struct kvm *kvm;

		mutex_lock(&kvm_lock);

		list_for_each_entry(kvm, &vm_list, vm_list)
			wake_up_process(kvm->arch.nx_lpage_recovery_thread);

		mutex_unlock(&kvm_lock);
	}

	return err;
}

static void kvm_recover_nx_lpages(struct kvm *kvm)
{
	int rcu_idx;
	struct kvm_mmu_page *sp;
	unsigned int ratio;
	LIST_HEAD(invalid_list);
	ulong to_zap;

	rcu_idx = srcu_read_lock(&kvm->srcu);
	spin_lock(&kvm->mmu_lock);

	ratio = READ_ONCE(nx_huge_pages_recovery_ratio);
	to_zap = ratio ? DIV_ROUND_UP(kvm->stat.nx_lpage_splits, ratio) : 0;
	while (to_zap && !list_empty(&kvm->arch.lpage_disallowed_mmu_pages)) {
		/*
		 * We use a separate list instead of just using active_mmu_pages
		 * because the number of lpage_disallowed pages is expected to
		 * be relatively small compared to the total.
		 */
		sp = list_first_entry(&kvm->arch.lpage_disallowed_mmu_pages,
				      struct kvm_mmu_page,
				      lpage_disallowed_link);
		WARN_ON_ONCE(!sp->lpage_disallowed);
		kvm_mmu_prepare_zap_page(kvm, sp, &invalid_list);
		WARN_ON_ONCE(sp->lpage_disallowed);

		if (!--to_zap || need_resched() || spin_needbreak(&kvm->mmu_lock)) {
			kvm_mmu_commit_zap_page(kvm, &invalid_list);
			if (to_zap)
				cond_resched_lock(&kvm->mmu_lock);
		}
	}

	spin_unlock(&kvm->mmu_lock);
	srcu_read_unlock(&kvm->srcu, rcu_idx);
}

static long get_nx_lpage_recovery_timeout(u64 start_time)
{
	return READ_ONCE(nx_huge_pages) && READ_ONCE(nx_huge_pages_recovery_ratio)
		? start_time + 60 * HZ - get_jiffies_64()
		: MAX_SCHEDULE_TIMEOUT;
}

static int kvm_nx_lpage_recovery_worker(struct kvm *kvm, uintptr_t data)
{
	u64 start_time;
	long remaining_time;

	while (true) {
		start_time = get_jiffies_64();
		remaining_time = get_nx_lpage_recovery_timeout(start_time);

		set_current_state(TASK_INTERRUPTIBLE);
		while (!kthread_should_stop() && remaining_time > 0) {
			schedule_timeout(remaining_time);
			remaining_time = get_nx_lpage_recovery_timeout(start_time);
			set_current_state(TASK_INTERRUPTIBLE);
		}

		set_current_state(TASK_RUNNING);

		if (kthread_should_stop())
			return 0;

		kvm_recover_nx_lpages(kvm);
	}
}

int kvm_mmu_post_init_vm(struct kvm *kvm)
{
	int err;

	err = kvm_vm_create_worker_thread(kvm, kvm_nx_lpage_recovery_worker, 0,
					  "kvm-nx-lpage-recovery",
					  &kvm->arch.nx_lpage_recovery_thread);
	if (!err)
		kthread_unpark(kvm->arch.nx_lpage_recovery_thread);

	return err;
}

void kvm_mmu_pre_destroy_vm(struct kvm *kvm)
{
	if (kvm->arch.nx_lpage_recovery_thread)
		kthread_stop(kvm->arch.nx_lpage_recovery_thread);
}
