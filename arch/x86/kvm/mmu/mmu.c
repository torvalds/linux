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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "irq.h"
#include "ioapic.h"
#include "mmu.h"
#include "mmu_internal.h"
#include "tdp_mmu.h"
#include "x86.h"
#include "kvm_cache_regs.h"
#include "smm.h"
#include "kvm_emulate.h"
#include "page_track.h"
#include "cpuid.h"
#include "spte.h"

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
#include <linux/kstrtox.h>
#include <linux/kthread.h>
#include <linux/wordpart.h>

#include <asm/page.h>
#include <asm/memtype.h>
#include <asm/cmpxchg.h>
#include <asm/io.h>
#include <asm/set_memory.h>
#include <asm/spec-ctrl.h>
#include <asm/vmx.h>

#include "trace.h"

static bool nx_hugepage_mitigation_hard_disabled;

int __read_mostly nx_huge_pages = -1;
static uint __read_mostly nx_huge_pages_recovery_period_ms;
#ifdef CONFIG_PREEMPT_RT
/* Recovery can cause latency spikes, disable it for PREEMPT_RT.  */
static uint __read_mostly nx_huge_pages_recovery_ratio = 0;
#else
static uint __read_mostly nx_huge_pages_recovery_ratio = 60;
#endif

static int get_nx_huge_pages(char *buffer, const struct kernel_param *kp);
static int set_nx_huge_pages(const char *val, const struct kernel_param *kp);
static int set_nx_huge_pages_recovery_param(const char *val, const struct kernel_param *kp);

static const struct kernel_param_ops nx_huge_pages_ops = {
	.set = set_nx_huge_pages,
	.get = get_nx_huge_pages,
};

static const struct kernel_param_ops nx_huge_pages_recovery_param_ops = {
	.set = set_nx_huge_pages_recovery_param,
	.get = param_get_uint,
};

module_param_cb(nx_huge_pages, &nx_huge_pages_ops, &nx_huge_pages, 0644);
__MODULE_PARM_TYPE(nx_huge_pages, "bool");
module_param_cb(nx_huge_pages_recovery_ratio, &nx_huge_pages_recovery_param_ops,
		&nx_huge_pages_recovery_ratio, 0644);
__MODULE_PARM_TYPE(nx_huge_pages_recovery_ratio, "uint");
module_param_cb(nx_huge_pages_recovery_period_ms, &nx_huge_pages_recovery_param_ops,
		&nx_huge_pages_recovery_period_ms, 0644);
__MODULE_PARM_TYPE(nx_huge_pages_recovery_period_ms, "uint");

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

static bool __ro_after_init tdp_mmu_allowed;

#ifdef CONFIG_X86_64
bool __read_mostly tdp_mmu_enabled = true;
module_param_named(tdp_mmu, tdp_mmu_enabled, bool, 0444);
#endif

static int max_huge_page_level __read_mostly;
static int tdp_root_level __read_mostly;
static int max_tdp_level __read_mostly;

#define PTE_PREFETCH_NUM		8

#include <trace/events/kvm.h>

/* make pte_list_desc fit well in cache lines */
#define PTE_LIST_EXT 14

/*
 * struct pte_list_desc is the core data structure used to implement a custom
 * list for tracking a set of related SPTEs, e.g. all the SPTEs that map a
 * given GFN when used in the context of rmaps.  Using a custom list allows KVM
 * to optimize for the common case where many GFNs will have at most a handful
 * of SPTEs pointing at them, i.e. allows packing multiple SPTEs into a small
 * memory footprint, which in turn improves runtime performance by exploiting
 * cache locality.
 *
 * A list is comprised of one or more pte_list_desc objects (descriptors).
 * Each individual descriptor stores up to PTE_LIST_EXT SPTEs.  If a descriptor
 * is full and a new SPTEs needs to be added, a new descriptor is allocated and
 * becomes the head of the list.  This means that by definitions, all tail
 * descriptors are full.
 *
 * Note, the meta data fields are deliberately placed at the start of the
 * structure to optimize the cacheline layout; accessing the descriptor will
 * touch only a single cacheline so long as @spte_count<=6 (or if only the
 * descriptors metadata is accessed).
 */
struct pte_list_desc {
	struct pte_list_desc *more;
	/* The number of PTEs stored in _this_ descriptor. */
	u32 spte_count;
	/* The number of PTEs stored in all tails of this descriptor. */
	u32 tail_count;
	u64 *sptes[PTE_LIST_EXT];
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
struct kmem_cache *mmu_page_header_cache;
static struct percpu_counter kvm_total_used_mmu_pages;

static void mmu_spte_set(u64 *sptep, u64 spte);

struct kvm_mmu_role_regs {
	const unsigned long cr0;
	const unsigned long cr4;
	const u64 efer;
};

#define CREATE_TRACE_POINTS
#include "mmutrace.h"

/*
 * Yes, lot's of underscores.  They're a hint that you probably shouldn't be
 * reading from the role_regs.  Once the root_role is constructed, it becomes
 * the single source of truth for the MMU's state.
 */
#define BUILD_MMU_ROLE_REGS_ACCESSOR(reg, name, flag)			\
static inline bool __maybe_unused					\
____is_##reg##_##name(const struct kvm_mmu_role_regs *regs)		\
{									\
	return !!(regs->reg & flag);					\
}
BUILD_MMU_ROLE_REGS_ACCESSOR(cr0, pg, X86_CR0_PG);
BUILD_MMU_ROLE_REGS_ACCESSOR(cr0, wp, X86_CR0_WP);
BUILD_MMU_ROLE_REGS_ACCESSOR(cr4, pse, X86_CR4_PSE);
BUILD_MMU_ROLE_REGS_ACCESSOR(cr4, pae, X86_CR4_PAE);
BUILD_MMU_ROLE_REGS_ACCESSOR(cr4, smep, X86_CR4_SMEP);
BUILD_MMU_ROLE_REGS_ACCESSOR(cr4, smap, X86_CR4_SMAP);
BUILD_MMU_ROLE_REGS_ACCESSOR(cr4, pke, X86_CR4_PKE);
BUILD_MMU_ROLE_REGS_ACCESSOR(cr4, la57, X86_CR4_LA57);
BUILD_MMU_ROLE_REGS_ACCESSOR(efer, nx, EFER_NX);
BUILD_MMU_ROLE_REGS_ACCESSOR(efer, lma, EFER_LMA);

/*
 * The MMU itself (with a valid role) is the single source of truth for the
 * MMU.  Do not use the regs used to build the MMU/role, nor the vCPU.  The
 * regs don't account for dependencies, e.g. clearing CR4 bits if CR0.PG=1,
 * and the vCPU may be incorrect/irrelevant.
 */
#define BUILD_MMU_ROLE_ACCESSOR(base_or_ext, reg, name)		\
static inline bool __maybe_unused is_##reg##_##name(struct kvm_mmu *mmu)	\
{								\
	return !!(mmu->cpu_role. base_or_ext . reg##_##name);	\
}
BUILD_MMU_ROLE_ACCESSOR(base, cr0, wp);
BUILD_MMU_ROLE_ACCESSOR(ext,  cr4, pse);
BUILD_MMU_ROLE_ACCESSOR(ext,  cr4, smep);
BUILD_MMU_ROLE_ACCESSOR(ext,  cr4, smap);
BUILD_MMU_ROLE_ACCESSOR(ext,  cr4, pke);
BUILD_MMU_ROLE_ACCESSOR(ext,  cr4, la57);
BUILD_MMU_ROLE_ACCESSOR(base, efer, nx);
BUILD_MMU_ROLE_ACCESSOR(ext,  efer, lma);

static inline bool is_cr0_pg(struct kvm_mmu *mmu)
{
        return mmu->cpu_role.base.level > 0;
}

static inline bool is_cr4_pae(struct kvm_mmu *mmu)
{
        return !mmu->cpu_role.base.has_4_byte_gpte;
}

static struct kvm_mmu_role_regs vcpu_to_role_regs(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_role_regs regs = {
		.cr0 = kvm_read_cr0_bits(vcpu, KVM_MMU_CR0_ROLE_BITS),
		.cr4 = kvm_read_cr4_bits(vcpu, KVM_MMU_CR4_ROLE_BITS),
		.efer = vcpu->arch.efer,
	};

	return regs;
}

static unsigned long get_guest_cr3(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr3(vcpu);
}

static inline unsigned long kvm_mmu_get_guest_pgd(struct kvm_vcpu *vcpu,
						  struct kvm_mmu *mmu)
{
	if (IS_ENABLED(CONFIG_MITIGATION_RETPOLINE) && mmu->get_guest_pgd == get_guest_cr3)
		return kvm_read_cr3(vcpu);

	return mmu->get_guest_pgd(vcpu);
}

static inline bool kvm_available_flush_remote_tlbs_range(void)
{
#if IS_ENABLED(CONFIG_HYPERV)
	return kvm_x86_ops.flush_remote_tlbs_range;
#else
	return false;
#endif
}

static gfn_t kvm_mmu_page_get_gfn(struct kvm_mmu_page *sp, int index);

/* Flush the range of guest memory mapped by the given SPTE. */
static void kvm_flush_remote_tlbs_sptep(struct kvm *kvm, u64 *sptep)
{
	struct kvm_mmu_page *sp = sptep_to_sp(sptep);
	gfn_t gfn = kvm_mmu_page_get_gfn(sp, spte_index(sptep));

	kvm_flush_remote_tlbs_gfn(kvm, gfn, sp->role.level);
}

static void mark_mmio_spte(struct kvm_vcpu *vcpu, u64 *sptep, u64 gfn,
			   unsigned int access)
{
	u64 spte = make_mmio_spte(vcpu, gfn, access);

	trace_mark_mmio_spte(sptep, gfn, spte);
	mmu_spte_set(sptep, spte);
}

static gfn_t get_mmio_spte_gfn(u64 spte)
{
	u64 gpa = spte & shadow_nonpresent_or_rsvd_lower_gfn_mask;

	gpa |= (spte >> SHADOW_NONPRESENT_OR_RSVD_MASK_LEN)
	       & shadow_nonpresent_or_rsvd_mask;

	return gpa >> PAGE_SHIFT;
}

static unsigned get_mmio_spte_access(u64 spte)
{
	return spte & shadow_mmio_access_mask;
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

static int is_cpuid_PSE36(void)
{
	return 1;
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
 * An spte tlb flush may be pending, because kvm_set_pte_rmap
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

/* Rules for using mmu_spte_set:
 * Set the sptep from nonpresent to present.
 * Note: the sptep being assigned *must* be either not present
 * or in a state where the hardware will not attempt to update
 * the spte.
 */
static void mmu_spte_set(u64 *sptep, u64 new_spte)
{
	WARN_ON_ONCE(is_shadow_present_pte(*sptep));
	__set_spte(sptep, new_spte);
}

/*
 * Update the SPTE (excluding the PFN), but do not track changes in its
 * accessed/dirty status.
 */
static u64 mmu_spte_update_no_track(u64 *sptep, u64 new_spte)
{
	u64 old_spte = *sptep;

	WARN_ON_ONCE(!is_shadow_present_pte(new_spte));
	check_spte_writable_invariants(new_spte);

	if (!is_shadow_present_pte(old_spte)) {
		mmu_spte_set(sptep, new_spte);
		return old_spte;
	}

	if (!spte_has_volatile_bits(old_spte))
		__update_clear_spte_fast(sptep, new_spte);
	else
		old_spte = __update_clear_spte_slow(sptep, new_spte);

	WARN_ON_ONCE(spte_to_pfn(old_spte) != spte_to_pfn(new_spte));

	return old_spte;
}

/* Rules for using mmu_spte_update:
 * Update the state bits, it means the mapped pfn is not changed.
 *
 * Whenever an MMU-writable SPTE is overwritten with a read-only SPTE, remote
 * TLBs must be flushed. Otherwise rmap_write_protect will find a read-only
 * spte, even though the writable spte might be cached on a CPU's TLB.
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
	if (is_mmu_writable_spte(old_spte) &&
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
 * Returns the old PTE.
 */
static u64 mmu_spte_clear_track_bits(struct kvm *kvm, u64 *sptep)
{
	kvm_pfn_t pfn;
	u64 old_spte = *sptep;
	int level = sptep_to_sp(sptep)->role.level;
	struct page *page;

	if (!is_shadow_present_pte(old_spte) ||
	    !spte_has_volatile_bits(old_spte))
		__update_clear_spte_fast(sptep, 0ull);
	else
		old_spte = __update_clear_spte_slow(sptep, 0ull);

	if (!is_shadow_present_pte(old_spte))
		return old_spte;

	kvm_update_page_stats(kvm, level, -1);

	pfn = spte_to_pfn(old_spte);

	/*
	 * KVM doesn't hold a reference to any pages mapped into the guest, and
	 * instead uses the mmu_notifier to ensure that KVM unmaps any pages
	 * before they are reclaimed.  Sanity check that, if the pfn is backed
	 * by a refcounted page, the refcount is elevated.
	 */
	page = kvm_pfn_to_refcounted_page(pfn);
	WARN_ON_ONCE(page && !page_count(page));

	if (is_accessed_spte(old_spte))
		kvm_set_pfn_accessed(pfn);

	if (is_dirty_spte(old_spte))
		kvm_set_pfn_dirty(pfn);

	return old_spte;
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

static inline bool is_tdp_mmu_active(struct kvm_vcpu *vcpu)
{
	return tdp_mmu_enabled && vcpu->arch.mmu->root_role.direct;
}

static void walk_shadow_page_lockless_begin(struct kvm_vcpu *vcpu)
{
	if (is_tdp_mmu_active(vcpu)) {
		kvm_tdp_mmu_walk_lockless_begin();
	} else {
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
}

static void walk_shadow_page_lockless_end(struct kvm_vcpu *vcpu)
{
	if (is_tdp_mmu_active(vcpu)) {
		kvm_tdp_mmu_walk_lockless_end();
	} else {
		/*
		 * Make sure the write to vcpu->mode is not reordered in front of
		 * reads to sptes.  If it does, kvm_mmu_commit_zap_page() can see us
		 * OUTSIDE_GUEST_MODE and proceed to free the shadow page table.
		 */
		smp_store_release(&vcpu->mode, OUTSIDE_GUEST_MODE);
		local_irq_enable();
	}
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
		r = kvm_mmu_topup_memory_cache(&vcpu->arch.mmu_shadowed_info_cache,
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
	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_shadowed_info_cache);
	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_page_header_cache);
}

static void mmu_free_pte_list_desc(struct pte_list_desc *pte_list_desc)
{
	kmem_cache_free(pte_list_desc_cache, pte_list_desc);
}

static bool sp_has_gptes(struct kvm_mmu_page *sp);

static gfn_t kvm_mmu_page_get_gfn(struct kvm_mmu_page *sp, int index)
{
	if (sp->role.passthrough)
		return sp->gfn;

	if (!sp->role.direct)
		return sp->shadowed_translation[index] >> PAGE_SHIFT;

	return sp->gfn + (index << ((sp->role.level - 1) * SPTE_LEVEL_BITS));
}

/*
 * For leaf SPTEs, fetch the *guest* access permissions being shadowed. Note
 * that the SPTE itself may have a more constrained access permissions that
 * what the guest enforces. For example, a guest may create an executable
 * huge PTE but KVM may disallow execution to mitigate iTLB multihit.
 */
static u32 kvm_mmu_page_get_access(struct kvm_mmu_page *sp, int index)
{
	if (sp_has_gptes(sp))
		return sp->shadowed_translation[index] & ACC_ALL;

	/*
	 * For direct MMUs (e.g. TDP or non-paging guests) or passthrough SPs,
	 * KVM is not shadowing any guest page tables, so the "guest access
	 * permissions" are just ACC_ALL.
	 *
	 * For direct SPs in indirect MMUs (shadow paging), i.e. when KVM
	 * is shadowing a guest huge page with small pages, the guest access
	 * permissions being shadowed are the access permissions of the huge
	 * page.
	 *
	 * In both cases, sp->role.access contains the correct access bits.
	 */
	return sp->role.access;
}

static void kvm_mmu_page_set_translation(struct kvm_mmu_page *sp, int index,
					 gfn_t gfn, unsigned int access)
{
	if (sp_has_gptes(sp)) {
		sp->shadowed_translation[index] = (gfn << PAGE_SHIFT) | access;
		return;
	}

	WARN_ONCE(access != kvm_mmu_page_get_access(sp, index),
	          "access mismatch under %s page %llx (expected %u, got %u)\n",
	          sp->role.passthrough ? "passthrough" : "direct",
	          sp->gfn, kvm_mmu_page_get_access(sp, index), access);

	WARN_ONCE(gfn != kvm_mmu_page_get_gfn(sp, index),
	          "gfn mismatch under %s page %llx (expected %llx, got %llx)\n",
	          sp->role.passthrough ? "passthrough" : "direct",
	          sp->gfn, kvm_mmu_page_get_gfn(sp, index), gfn);
}

static void kvm_mmu_page_set_access(struct kvm_mmu_page *sp, int index,
				    unsigned int access)
{
	gfn_t gfn = kvm_mmu_page_get_gfn(sp, index);

	kvm_mmu_page_set_translation(sp, index, gfn, access);
}

/*
 * Return the pointer to the large page information for a given gfn,
 * handling slots that are not large page aligned.
 */
static struct kvm_lpage_info *lpage_info_slot(gfn_t gfn,
		const struct kvm_memory_slot *slot, int level)
{
	unsigned long idx;

	idx = gfn_to_index(gfn, slot->base_gfn, level);
	return &slot->arch.lpage_info[level - 2][idx];
}

/*
 * The most significant bit in disallow_lpage tracks whether or not memory
 * attributes are mixed, i.e. not identical for all gfns at the current level.
 * The lower order bits are used to refcount other cases where a hugepage is
 * disallowed, e.g. if KVM has shadow a page table at the gfn.
 */
#define KVM_LPAGE_MIXED_FLAG	BIT(31)

static void update_gfn_disallow_lpage_count(const struct kvm_memory_slot *slot,
					    gfn_t gfn, int count)
{
	struct kvm_lpage_info *linfo;
	int old, i;

	for (i = PG_LEVEL_2M; i <= KVM_MAX_HUGEPAGE_LEVEL; ++i) {
		linfo = lpage_info_slot(gfn, slot, i);

		old = linfo->disallow_lpage;
		linfo->disallow_lpage += count;
		WARN_ON_ONCE((old ^ linfo->disallow_lpage) & KVM_LPAGE_MIXED_FLAG);
	}
}

void kvm_mmu_gfn_disallow_lpage(const struct kvm_memory_slot *slot, gfn_t gfn)
{
	update_gfn_disallow_lpage_count(slot, gfn, 1);
}

void kvm_mmu_gfn_allow_lpage(const struct kvm_memory_slot *slot, gfn_t gfn)
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
		return __kvm_write_track_add_gfn(kvm, slot, gfn);

	kvm_mmu_gfn_disallow_lpage(slot, gfn);

	if (kvm_mmu_slot_gfn_write_protect(kvm, slot, gfn, PG_LEVEL_4K))
		kvm_flush_remote_tlbs_gfn(kvm, gfn, PG_LEVEL_4K);
}

void track_possible_nx_huge_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	/*
	 * If it's possible to replace the shadow page with an NX huge page,
	 * i.e. if the shadow page is the only thing currently preventing KVM
	 * from using a huge page, add the shadow page to the list of "to be
	 * zapped for NX recovery" pages.  Note, the shadow page can already be
	 * on the list if KVM is reusing an existing shadow page, i.e. if KVM
	 * links a shadow page at multiple points.
	 */
	if (!list_empty(&sp->possible_nx_huge_page_link))
		return;

	++kvm->stat.nx_lpage_splits;
	list_add_tail(&sp->possible_nx_huge_page_link,
		      &kvm->arch.possible_nx_huge_pages);
}

static void account_nx_huge_page(struct kvm *kvm, struct kvm_mmu_page *sp,
				 bool nx_huge_page_possible)
{
	sp->nx_huge_page_disallowed = true;

	if (nx_huge_page_possible)
		track_possible_nx_huge_page(kvm, sp);
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
		return __kvm_write_track_remove_gfn(kvm, slot, gfn);

	kvm_mmu_gfn_allow_lpage(slot, gfn);
}

void untrack_possible_nx_huge_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	if (list_empty(&sp->possible_nx_huge_page_link))
		return;

	--kvm->stat.nx_lpage_splits;
	list_del_init(&sp->possible_nx_huge_page_link);
}

static void unaccount_nx_huge_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	sp->nx_huge_page_disallowed = false;

	untrack_possible_nx_huge_page(kvm, sp);
}

static struct kvm_memory_slot *gfn_to_memslot_dirty_bitmap(struct kvm_vcpu *vcpu,
							   gfn_t gfn,
							   bool no_dirty_log)
{
	struct kvm_memory_slot *slot;

	slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	if (!slot || slot->flags & KVM_MEMSLOT_INVALID)
		return NULL;
	if (no_dirty_log && kvm_slot_dirty_track_enabled(slot))
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
static int pte_list_add(struct kvm_mmu_memory_cache *cache, u64 *spte,
			struct kvm_rmap_head *rmap_head)
{
	struct pte_list_desc *desc;
	int count = 0;

	if (!rmap_head->val) {
		rmap_head->val = (unsigned long)spte;
	} else if (!(rmap_head->val & 1)) {
		desc = kvm_mmu_memory_cache_alloc(cache);
		desc->sptes[0] = (u64 *)rmap_head->val;
		desc->sptes[1] = spte;
		desc->spte_count = 2;
		desc->tail_count = 0;
		rmap_head->val = (unsigned long)desc | 1;
		++count;
	} else {
		desc = (struct pte_list_desc *)(rmap_head->val & ~1ul);
		count = desc->tail_count + desc->spte_count;

		/*
		 * If the previous head is full, allocate a new head descriptor
		 * as tail descriptors are always kept full.
		 */
		if (desc->spte_count == PTE_LIST_EXT) {
			desc = kvm_mmu_memory_cache_alloc(cache);
			desc->more = (struct pte_list_desc *)(rmap_head->val & ~1ul);
			desc->spte_count = 0;
			desc->tail_count = count;
			rmap_head->val = (unsigned long)desc | 1;
		}
		desc->sptes[desc->spte_count++] = spte;
	}
	return count;
}

static void pte_list_desc_remove_entry(struct kvm *kvm,
				       struct kvm_rmap_head *rmap_head,
				       struct pte_list_desc *desc, int i)
{
	struct pte_list_desc *head_desc = (struct pte_list_desc *)(rmap_head->val & ~1ul);
	int j = head_desc->spte_count - 1;

	/*
	 * The head descriptor should never be empty.  A new head is added only
	 * when adding an entry and the previous head is full, and heads are
	 * removed (this flow) when they become empty.
	 */
	KVM_BUG_ON_DATA_CORRUPTION(j < 0, kvm);

	/*
	 * Replace the to-be-freed SPTE with the last valid entry from the head
	 * descriptor to ensure that tail descriptors are full at all times.
	 * Note, this also means that tail_count is stable for each descriptor.
	 */
	desc->sptes[i] = head_desc->sptes[j];
	head_desc->sptes[j] = NULL;
	head_desc->spte_count--;
	if (head_desc->spte_count)
		return;

	/*
	 * The head descriptor is empty.  If there are no tail descriptors,
	 * nullify the rmap head to mark the list as empty, else point the rmap
	 * head at the next descriptor, i.e. the new head.
	 */
	if (!head_desc->more)
		rmap_head->val = 0;
	else
		rmap_head->val = (unsigned long)head_desc->more | 1;
	mmu_free_pte_list_desc(head_desc);
}

static void pte_list_remove(struct kvm *kvm, u64 *spte,
			    struct kvm_rmap_head *rmap_head)
{
	struct pte_list_desc *desc;
	int i;

	if (KVM_BUG_ON_DATA_CORRUPTION(!rmap_head->val, kvm))
		return;

	if (!(rmap_head->val & 1)) {
		if (KVM_BUG_ON_DATA_CORRUPTION((u64 *)rmap_head->val != spte, kvm))
			return;

		rmap_head->val = 0;
	} else {
		desc = (struct pte_list_desc *)(rmap_head->val & ~1ul);
		while (desc) {
			for (i = 0; i < desc->spte_count; ++i) {
				if (desc->sptes[i] == spte) {
					pte_list_desc_remove_entry(kvm, rmap_head,
								   desc, i);
					return;
				}
			}
			desc = desc->more;
		}

		KVM_BUG_ON_DATA_CORRUPTION(true, kvm);
	}
}

static void kvm_zap_one_rmap_spte(struct kvm *kvm,
				  struct kvm_rmap_head *rmap_head, u64 *sptep)
{
	mmu_spte_clear_track_bits(kvm, sptep);
	pte_list_remove(kvm, sptep, rmap_head);
}

/* Return true if at least one SPTE was zapped, false otherwise */
static bool kvm_zap_all_rmap_sptes(struct kvm *kvm,
				   struct kvm_rmap_head *rmap_head)
{
	struct pte_list_desc *desc, *next;
	int i;

	if (!rmap_head->val)
		return false;

	if (!(rmap_head->val & 1)) {
		mmu_spte_clear_track_bits(kvm, (u64 *)rmap_head->val);
		goto out;
	}

	desc = (struct pte_list_desc *)(rmap_head->val & ~1ul);

	for (; desc; desc = next) {
		for (i = 0; i < desc->spte_count; i++)
			mmu_spte_clear_track_bits(kvm, desc->sptes[i]);
		next = desc->more;
		mmu_free_pte_list_desc(desc);
	}
out:
	/* rmap_head is meaningless now, remember to reset it */
	rmap_head->val = 0;
	return true;
}

unsigned int pte_list_count(struct kvm_rmap_head *rmap_head)
{
	struct pte_list_desc *desc;

	if (!rmap_head->val)
		return 0;
	else if (!(rmap_head->val & 1))
		return 1;

	desc = (struct pte_list_desc *)(rmap_head->val & ~1ul);
	return desc->tail_count + desc->spte_count;
}

static struct kvm_rmap_head *gfn_to_rmap(gfn_t gfn, int level,
					 const struct kvm_memory_slot *slot)
{
	unsigned long idx;

	idx = gfn_to_index(gfn, slot->base_gfn, level);
	return &slot->arch.rmap[level - PG_LEVEL_4K][idx];
}

static void rmap_remove(struct kvm *kvm, u64 *spte)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;
	struct kvm_mmu_page *sp;
	gfn_t gfn;
	struct kvm_rmap_head *rmap_head;

	sp = sptep_to_sp(spte);
	gfn = kvm_mmu_page_get_gfn(sp, spte_index(spte));

	/*
	 * Unlike rmap_add, rmap_remove does not run in the context of a vCPU
	 * so we have to determine which memslots to use based on context
	 * information in sp->role.
	 */
	slots = kvm_memslots_for_spte_role(kvm, sp->role);

	slot = __gfn_to_memslot(slots, gfn);
	rmap_head = gfn_to_rmap(gfn, sp->role.level, slot);

	pte_list_remove(kvm, spte, rmap_head);
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
	u64 old_spte = mmu_spte_clear_track_bits(kvm, sptep);

	if (is_shadow_present_pte(old_spte))
		rmap_remove(kvm, sptep);
}

static void drop_large_spte(struct kvm *kvm, u64 *sptep, bool flush)
{
	struct kvm_mmu_page *sp;

	sp = sptep_to_sp(sptep);
	WARN_ON_ONCE(sp->role.level == PG_LEVEL_4K);

	drop_spte(kvm, sptep);

	if (flush)
		kvm_flush_remote_tlbs_sptep(kvm, sptep);
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
	    !(pt_protect && is_mmu_writable_spte(spte)))
		return false;

	if (pt_protect)
		spte &= ~shadow_mmu_writable_mask;
	spte = spte & ~PT_WRITABLE_MASK;

	return mmu_spte_update(sptep, spte);
}

static bool rmap_write_protect(struct kvm_rmap_head *rmap_head,
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

	KVM_MMU_WARN_ON(!spte_ad_enabled(spte));
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
static bool __rmap_clear_dirty(struct kvm *kvm, struct kvm_rmap_head *rmap_head,
			       const struct kvm_memory_slot *slot)
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

/**
 * kvm_mmu_write_protect_pt_masked - write protect selected PT level pages
 * @kvm: kvm instance
 * @slot: slot to protect
 * @gfn_offset: start of the BITS_PER_LONG pages we care about
 * @mask: indicates which pages we should protect
 *
 * Used when we do not need to care about huge page mappings.
 */
static void kvm_mmu_write_protect_pt_masked(struct kvm *kvm,
				     struct kvm_memory_slot *slot,
				     gfn_t gfn_offset, unsigned long mask)
{
	struct kvm_rmap_head *rmap_head;

	if (tdp_mmu_enabled)
		kvm_tdp_mmu_clear_dirty_pt_masked(kvm, slot,
				slot->base_gfn + gfn_offset, mask, true);

	if (!kvm_memslots_have_rmaps(kvm))
		return;

	while (mask) {
		rmap_head = gfn_to_rmap(slot->base_gfn + gfn_offset + __ffs(mask),
					PG_LEVEL_4K, slot);
		rmap_write_protect(rmap_head, false);

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
static void kvm_mmu_clear_dirty_pt_masked(struct kvm *kvm,
					 struct kvm_memory_slot *slot,
					 gfn_t gfn_offset, unsigned long mask)
{
	struct kvm_rmap_head *rmap_head;

	if (tdp_mmu_enabled)
		kvm_tdp_mmu_clear_dirty_pt_masked(kvm, slot,
				slot->base_gfn + gfn_offset, mask, false);

	if (!kvm_memslots_have_rmaps(kvm))
		return;

	while (mask) {
		rmap_head = gfn_to_rmap(slot->base_gfn + gfn_offset + __ffs(mask),
					PG_LEVEL_4K, slot);
		__rmap_clear_dirty(kvm, rmap_head, slot);

		/* clear the first set bit */
		mask &= mask - 1;
	}
}

/**
 * kvm_arch_mmu_enable_log_dirty_pt_masked - enable dirty logging for selected
 * PT level pages.
 *
 * It calls kvm_mmu_write_protect_pt_masked to write protect selected pages to
 * enable dirty logging for them.
 *
 * We need to care about huge page mappings: e.g. during dirty logging we may
 * have such mappings.
 */
void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *kvm,
				struct kvm_memory_slot *slot,
				gfn_t gfn_offset, unsigned long mask)
{
	/*
	 * Huge pages are NOT write protected when we start dirty logging in
	 * initially-all-set mode; must write protect them here so that they
	 * are split to 4K on the first write.
	 *
	 * The gfn_offset is guaranteed to be aligned to 64, but the base_gfn
	 * of memslot has no such restriction, so the range can cross two large
	 * pages.
	 */
	if (kvm_dirty_log_manual_protect_and_init_set(kvm)) {
		gfn_t start = slot->base_gfn + gfn_offset + __ffs(mask);
		gfn_t end = slot->base_gfn + gfn_offset + __fls(mask);

		if (READ_ONCE(eager_page_split))
			kvm_mmu_try_split_huge_pages(kvm, slot, start, end + 1, PG_LEVEL_4K);

		kvm_mmu_slot_gfn_write_protect(kvm, slot, start, PG_LEVEL_2M);

		/* Cross two large pages? */
		if (ALIGN(start << PAGE_SHIFT, PMD_SIZE) !=
		    ALIGN(end << PAGE_SHIFT, PMD_SIZE))
			kvm_mmu_slot_gfn_write_protect(kvm, slot, end,
						       PG_LEVEL_2M);
	}

	/* Now handle 4K PTEs.  */
	if (kvm_x86_ops.cpu_dirty_log_size)
		kvm_mmu_clear_dirty_pt_masked(kvm, slot, gfn_offset, mask);
	else
		kvm_mmu_write_protect_pt_masked(kvm, slot, gfn_offset, mask);
}

int kvm_cpu_dirty_log_size(void)
{
	return kvm_x86_ops.cpu_dirty_log_size;
}

bool kvm_mmu_slot_gfn_write_protect(struct kvm *kvm,
				    struct kvm_memory_slot *slot, u64 gfn,
				    int min_level)
{
	struct kvm_rmap_head *rmap_head;
	int i;
	bool write_protected = false;

	if (kvm_memslots_have_rmaps(kvm)) {
		for (i = min_level; i <= KVM_MAX_HUGEPAGE_LEVEL; ++i) {
			rmap_head = gfn_to_rmap(gfn, i, slot);
			write_protected |= rmap_write_protect(rmap_head, true);
		}
	}

	if (tdp_mmu_enabled)
		write_protected |=
			kvm_tdp_mmu_write_protect_gfn(kvm, slot, gfn, min_level);

	return write_protected;
}

static bool kvm_vcpu_write_protect_gfn(struct kvm_vcpu *vcpu, u64 gfn)
{
	struct kvm_memory_slot *slot;

	slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	return kvm_mmu_slot_gfn_write_protect(vcpu->kvm, slot, gfn, PG_LEVEL_4K);
}

static bool __kvm_zap_rmap(struct kvm *kvm, struct kvm_rmap_head *rmap_head,
			   const struct kvm_memory_slot *slot)
{
	return kvm_zap_all_rmap_sptes(kvm, rmap_head);
}

static bool kvm_zap_rmap(struct kvm *kvm, struct kvm_rmap_head *rmap_head,
			 struct kvm_memory_slot *slot, gfn_t gfn, int level,
			 pte_t unused)
{
	return __kvm_zap_rmap(kvm, rmap_head, slot);
}

static bool kvm_set_pte_rmap(struct kvm *kvm, struct kvm_rmap_head *rmap_head,
			     struct kvm_memory_slot *slot, gfn_t gfn, int level,
			     pte_t pte)
{
	u64 *sptep;
	struct rmap_iterator iter;
	bool need_flush = false;
	u64 new_spte;
	kvm_pfn_t new_pfn;

	WARN_ON_ONCE(pte_huge(pte));
	new_pfn = pte_pfn(pte);

restart:
	for_each_rmap_spte(rmap_head, &iter, sptep) {
		need_flush = true;

		if (pte_write(pte)) {
			kvm_zap_one_rmap_spte(kvm, rmap_head, sptep);
			goto restart;
		} else {
			new_spte = kvm_mmu_changed_pte_notifier_make_spte(
					*sptep, new_pfn);

			mmu_spte_clear_track_bits(kvm, sptep);
			mmu_spte_set(sptep, new_spte);
		}
	}

	if (need_flush && kvm_available_flush_remote_tlbs_range()) {
		kvm_flush_remote_tlbs_gfn(kvm, gfn, level);
		return false;
	}

	return need_flush;
}

struct slot_rmap_walk_iterator {
	/* input fields. */
	const struct kvm_memory_slot *slot;
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

static void rmap_walk_init_level(struct slot_rmap_walk_iterator *iterator,
				 int level)
{
	iterator->level = level;
	iterator->gfn = iterator->start_gfn;
	iterator->rmap = gfn_to_rmap(iterator->gfn, level, iterator->slot);
	iterator->end_rmap = gfn_to_rmap(iterator->end_gfn, level, iterator->slot);
}

static void slot_rmap_walk_init(struct slot_rmap_walk_iterator *iterator,
				const struct kvm_memory_slot *slot,
				int start_level, int end_level,
				gfn_t start_gfn, gfn_t end_gfn)
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
	while (++iterator->rmap <= iterator->end_rmap) {
		iterator->gfn += (1UL << KVM_HPAGE_GFN_SHIFT(iterator->level));

		if (iterator->rmap->val)
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

typedef bool (*rmap_handler_t)(struct kvm *kvm, struct kvm_rmap_head *rmap_head,
			       struct kvm_memory_slot *slot, gfn_t gfn,
			       int level, pte_t pte);

static __always_inline bool kvm_handle_gfn_range(struct kvm *kvm,
						 struct kvm_gfn_range *range,
						 rmap_handler_t handler)
{
	struct slot_rmap_walk_iterator iterator;
	bool ret = false;

	for_each_slot_rmap_range(range->slot, PG_LEVEL_4K, KVM_MAX_HUGEPAGE_LEVEL,
				 range->start, range->end - 1, &iterator)
		ret |= handler(kvm, iterator.rmap, range->slot, iterator.gfn,
			       iterator.level, range->arg.pte);

	return ret;
}

bool kvm_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range)
{
	bool flush = false;

	if (kvm_memslots_have_rmaps(kvm))
		flush = kvm_handle_gfn_range(kvm, range, kvm_zap_rmap);

	if (tdp_mmu_enabled)
		flush = kvm_tdp_mmu_unmap_gfn_range(kvm, range, flush);

	if (kvm_x86_ops.set_apic_access_page_addr &&
	    range->slot->id == APIC_ACCESS_PAGE_PRIVATE_MEMSLOT)
		kvm_make_all_cpus_request(kvm, KVM_REQ_APIC_PAGE_RELOAD);

	return flush;
}

bool kvm_set_spte_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	bool flush = false;

	if (kvm_memslots_have_rmaps(kvm))
		flush = kvm_handle_gfn_range(kvm, range, kvm_set_pte_rmap);

	if (tdp_mmu_enabled)
		flush |= kvm_tdp_mmu_set_spte_gfn(kvm, range);

	return flush;
}

static bool kvm_age_rmap(struct kvm *kvm, struct kvm_rmap_head *rmap_head,
			 struct kvm_memory_slot *slot, gfn_t gfn, int level,
			 pte_t unused)
{
	u64 *sptep;
	struct rmap_iterator iter;
	int young = 0;

	for_each_rmap_spte(rmap_head, &iter, sptep)
		young |= mmu_spte_age(sptep);

	return young;
}

static bool kvm_test_age_rmap(struct kvm *kvm, struct kvm_rmap_head *rmap_head,
			      struct kvm_memory_slot *slot, gfn_t gfn,
			      int level, pte_t unused)
{
	u64 *sptep;
	struct rmap_iterator iter;

	for_each_rmap_spte(rmap_head, &iter, sptep)
		if (is_accessed_spte(*sptep))
			return true;
	return false;
}

#define RMAP_RECYCLE_THRESHOLD 1000

static void __rmap_add(struct kvm *kvm,
		       struct kvm_mmu_memory_cache *cache,
		       const struct kvm_memory_slot *slot,
		       u64 *spte, gfn_t gfn, unsigned int access)
{
	struct kvm_mmu_page *sp;
	struct kvm_rmap_head *rmap_head;
	int rmap_count;

	sp = sptep_to_sp(spte);
	kvm_mmu_page_set_translation(sp, spte_index(spte), gfn, access);
	kvm_update_page_stats(kvm, sp->role.level, 1);

	rmap_head = gfn_to_rmap(gfn, sp->role.level, slot);
	rmap_count = pte_list_add(cache, spte, rmap_head);

	if (rmap_count > kvm->stat.max_mmu_rmap_size)
		kvm->stat.max_mmu_rmap_size = rmap_count;
	if (rmap_count > RMAP_RECYCLE_THRESHOLD) {
		kvm_zap_all_rmap_sptes(kvm, rmap_head);
		kvm_flush_remote_tlbs_gfn(kvm, gfn, sp->role.level);
	}
}

static void rmap_add(struct kvm_vcpu *vcpu, const struct kvm_memory_slot *slot,
		     u64 *spte, gfn_t gfn, unsigned int access)
{
	struct kvm_mmu_memory_cache *cache = &vcpu->arch.mmu_pte_list_desc_cache;

	__rmap_add(vcpu->kvm, cache, slot, spte, gfn, access);
}

bool kvm_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	bool young = false;

	if (kvm_memslots_have_rmaps(kvm))
		young = kvm_handle_gfn_range(kvm, range, kvm_age_rmap);

	if (tdp_mmu_enabled)
		young |= kvm_tdp_mmu_age_gfn_range(kvm, range);

	return young;
}

bool kvm_test_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	bool young = false;

	if (kvm_memslots_have_rmaps(kvm))
		young = kvm_handle_gfn_range(kvm, range, kvm_test_age_rmap);

	if (tdp_mmu_enabled)
		young |= kvm_tdp_mmu_test_age_gfn(kvm, range);

	return young;
}

static void kvm_mmu_check_sptes_at_free(struct kvm_mmu_page *sp)
{
#ifdef CONFIG_KVM_PROVE_MMU
	int i;

	for (i = 0; i < SPTE_ENT_PER_PAGE; i++) {
		if (KVM_MMU_WARN_ON(is_shadow_present_pte(sp->spt[i])))
			pr_err_ratelimited("SPTE %llx (@ %p) for gfn %llx shadow-present at free",
					   sp->spt[i], &sp->spt[i],
					   kvm_mmu_page_get_gfn(sp, i));
	}
#endif
}

/*
 * This value is the sum of all of the kvm instances's
 * kvm->arch.n_used_mmu_pages values.  We need a global,
 * aggregate version in order to make the slab shrinker
 * faster
 */
static inline void kvm_mod_used_mmu_pages(struct kvm *kvm, long nr)
{
	kvm->arch.n_used_mmu_pages += nr;
	percpu_counter_add(&kvm_total_used_mmu_pages, nr);
}

static void kvm_account_mmu_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	kvm_mod_used_mmu_pages(kvm, +1);
	kvm_account_pgtable_pages((void *)sp->spt, +1);
}

static void kvm_unaccount_mmu_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	kvm_mod_used_mmu_pages(kvm, -1);
	kvm_account_pgtable_pages((void *)sp->spt, -1);
}

static void kvm_mmu_free_shadow_page(struct kvm_mmu_page *sp)
{
	kvm_mmu_check_sptes_at_free(sp);

	hlist_del(&sp->hash_link);
	list_del(&sp->link);
	free_page((unsigned long)sp->spt);
	if (!sp->role.direct)
		free_page((unsigned long)sp->shadowed_translation);
	kmem_cache_free(mmu_page_header_cache, sp);
}

static unsigned kvm_page_table_hashfn(gfn_t gfn)
{
	return hash_64(gfn, KVM_MMU_HASH_SHIFT);
}

static void mmu_page_add_parent_pte(struct kvm_mmu_memory_cache *cache,
				    struct kvm_mmu_page *sp, u64 *parent_pte)
{
	if (!parent_pte)
		return;

	pte_list_add(cache, parent_pte, &sp->parent_ptes);
}

static void mmu_page_remove_parent_pte(struct kvm *kvm, struct kvm_mmu_page *sp,
				       u64 *parent_pte)
{
	pte_list_remove(kvm, parent_pte, &sp->parent_ptes);
}

static void drop_parent_pte(struct kvm *kvm, struct kvm_mmu_page *sp,
			    u64 *parent_pte)
{
	mmu_page_remove_parent_pte(kvm, sp, parent_pte);
	mmu_spte_clear_no_track(parent_pte);
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

	sp = sptep_to_sp(spte);
	if (__test_and_set_bit(spte_index(spte), sp->unsync_child_bitmap))
		return;
	if (sp->unsync_children++)
		return;
	kvm_mmu_mark_parents_unsync(sp);
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
	WARN_ON_ONCE((int)sp->unsync_children < 0);
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

		child = spte_to_child_sp(ent);

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
	WARN_ON_ONCE(!sp->unsync);
	trace_kvm_mmu_sync_page(sp);
	sp->unsync = 0;
	--kvm->stat.mmu_unsync;
}

static bool kvm_mmu_prepare_zap_page(struct kvm *kvm, struct kvm_mmu_page *sp,
				     struct list_head *invalid_list);
static void kvm_mmu_commit_zap_page(struct kvm *kvm,
				    struct list_head *invalid_list);

static bool sp_has_gptes(struct kvm_mmu_page *sp)
{
	if (sp->role.direct)
		return false;

	if (sp->role.passthrough)
		return false;

	return true;
}

#define for_each_valid_sp(_kvm, _sp, _list)				\
	hlist_for_each_entry(_sp, _list, hash_link)			\
		if (is_obsolete_sp((_kvm), (_sp))) {			\
		} else

#define for_each_gfn_valid_sp_with_gptes(_kvm, _sp, _gfn)		\
	for_each_valid_sp(_kvm, _sp,					\
	  &(_kvm)->arch.mmu_page_hash[kvm_page_table_hashfn(_gfn)])	\
		if ((_sp)->gfn != (_gfn) || !sp_has_gptes(_sp)) {} else

static bool kvm_sync_page_check(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp)
{
	union kvm_mmu_page_role root_role = vcpu->arch.mmu->root_role;

	/*
	 * Ignore various flags when verifying that it's safe to sync a shadow
	 * page using the current MMU context.
	 *
	 *  - level: not part of the overall MMU role and will never match as the MMU's
	 *           level tracks the root level
	 *  - access: updated based on the new guest PTE
	 *  - quadrant: not part of the overall MMU role (similar to level)
	 */
	const union kvm_mmu_page_role sync_role_ign = {
		.level = 0xf,
		.access = 0x7,
		.quadrant = 0x3,
		.passthrough = 0x1,
	};

	/*
	 * Direct pages can never be unsync, and KVM should never attempt to
	 * sync a shadow page for a different MMU context, e.g. if the role
	 * differs then the memslot lookup (SMM vs. non-SMM) will be bogus, the
	 * reserved bits checks will be wrong, etc...
	 */
	if (WARN_ON_ONCE(sp->role.direct || !vcpu->arch.mmu->sync_spte ||
			 (sp->role.word ^ root_role.word) & ~sync_role_ign.word))
		return false;

	return true;
}

static int kvm_sync_spte(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp, int i)
{
	if (!sp->spt[i])
		return 0;

	return vcpu->arch.mmu->sync_spte(vcpu, sp, i);
}

static int __kvm_sync_page(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp)
{
	int flush = 0;
	int i;

	if (!kvm_sync_page_check(vcpu, sp))
		return -1;

	for (i = 0; i < SPTE_ENT_PER_PAGE; i++) {
		int ret = kvm_sync_spte(vcpu, sp, i);

		if (ret < -1)
			return -1;
		flush |= ret;
	}

	/*
	 * Note, any flush is purely for KVM's correctness, e.g. when dropping
	 * an existing SPTE or clearing W/A/D bits to ensure an mmu_notifier
	 * unmap or dirty logging event doesn't fail to flush.  The guest is
	 * responsible for flushing the TLB to ensure any changes in protection
	 * bits are recognized, i.e. until the guest flushes or page faults on
	 * a relevant address, KVM is architecturally allowed to let vCPUs use
	 * cached translations with the old protection bits.
	 */
	return flush;
}

static int kvm_sync_page(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			 struct list_head *invalid_list)
{
	int ret = __kvm_sync_page(vcpu, sp);

	if (ret < 0)
		kvm_mmu_prepare_zap_page(vcpu->kvm, sp, invalid_list);
	return ret;
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

static bool is_obsolete_sp(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	if (sp->role.invalid)
		return true;

	/* TDP MMU pages do not use the MMU generation. */
	return !is_tdp_mmu_page(sp) &&
	       unlikely(sp->mmu_valid_gen != kvm->arch.mmu_valid_gen);
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

	WARN_ON_ONCE(pvec->page[0].idx != INVALID_INDEX);

	sp = pvec->page[0].sp;
	level = sp->role.level;
	WARN_ON_ONCE(level == PG_LEVEL_4K);

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

		WARN_ON_ONCE(idx == INVALID_INDEX);
		clear_unsync_child_bit(sp, idx);
		level++;
	} while (!sp->unsync_children);
}

static int mmu_sync_children(struct kvm_vcpu *vcpu,
			     struct kvm_mmu_page *parent, bool can_yield)
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
			protected |= kvm_vcpu_write_protect_gfn(vcpu, sp->gfn);

		if (protected) {
			kvm_mmu_remote_flush_or_zap(vcpu->kvm, &invalid_list, true);
			flush = false;
		}

		for_each_sp(pages, sp, parents, i) {
			kvm_unlink_unsync_page(vcpu->kvm, sp);
			flush |= kvm_sync_page(vcpu, sp, &invalid_list) > 0;
			mmu_pages_clear_parents(&parents);
		}
		if (need_resched() || rwlock_needbreak(&vcpu->kvm->mmu_lock)) {
			kvm_mmu_remote_flush_or_zap(vcpu->kvm, &invalid_list, flush);
			if (!can_yield) {
				kvm_make_request(KVM_REQ_MMU_SYNC, vcpu);
				return -EINTR;
			}

			cond_resched_rwlock_write(&vcpu->kvm->mmu_lock);
			flush = false;
		}
	}

	kvm_mmu_remote_flush_or_zap(vcpu->kvm, &invalid_list, flush);
	return 0;
}

static void __clear_sp_write_flooding_count(struct kvm_mmu_page *sp)
{
	atomic_set(&sp->write_flooding_count,  0);
}

static void clear_sp_write_flooding_count(u64 *spte)
{
	__clear_sp_write_flooding_count(sptep_to_sp(spte));
}

/*
 * The vCPU is required when finding indirect shadow pages; the shadow
 * page may already exist and syncing it needs the vCPU pointer in
 * order to read guest page tables.  Direct shadow pages are never
 * unsync, thus @vcpu can be NULL if @role.direct is true.
 */
static struct kvm_mmu_page *kvm_mmu_find_shadow_page(struct kvm *kvm,
						     struct kvm_vcpu *vcpu,
						     gfn_t gfn,
						     struct hlist_head *sp_list,
						     union kvm_mmu_page_role role)
{
	struct kvm_mmu_page *sp;
	int ret;
	int collisions = 0;
	LIST_HEAD(invalid_list);

	for_each_valid_sp(kvm, sp, sp_list) {
		if (sp->gfn != gfn) {
			collisions++;
			continue;
		}

		if (sp->role.word != role.word) {
			/*
			 * If the guest is creating an upper-level page, zap
			 * unsync pages for the same gfn.  While it's possible
			 * the guest is using recursive page tables, in all
			 * likelihood the guest has stopped using the unsync
			 * page and is installing a completely unrelated page.
			 * Unsync pages must not be left as is, because the new
			 * upper-level page will be write-protected.
			 */
			if (role.level > PG_LEVEL_4K && sp->unsync)
				kvm_mmu_prepare_zap_page(kvm, sp,
							 &invalid_list);
			continue;
		}

		/* unsync and write-flooding only apply to indirect SPs. */
		if (sp->role.direct)
			goto out;

		if (sp->unsync) {
			if (KVM_BUG_ON(!vcpu, kvm))
				break;

			/*
			 * The page is good, but is stale.  kvm_sync_page does
			 * get the latest guest state, but (unlike mmu_unsync_children)
			 * it doesn't write-protect the page or mark it synchronized!
			 * This way the validity of the mapping is ensured, but the
			 * overhead of write protection is not incurred until the
			 * guest invalidates the TLB mapping.  This allows multiple
			 * SPs for a single gfn to be unsync.
			 *
			 * If the sync fails, the page is zapped.  If so, break
			 * in order to rebuild it.
			 */
			ret = kvm_sync_page(vcpu, sp, &invalid_list);
			if (ret < 0)
				break;

			WARN_ON_ONCE(!list_empty(&invalid_list));
			if (ret > 0)
				kvm_flush_remote_tlbs(kvm);
		}

		__clear_sp_write_flooding_count(sp);

		goto out;
	}

	sp = NULL;
	++kvm->stat.mmu_cache_miss;

out:
	kvm_mmu_commit_zap_page(kvm, &invalid_list);

	if (collisions > kvm->stat.max_mmu_page_hash_collisions)
		kvm->stat.max_mmu_page_hash_collisions = collisions;
	return sp;
}

/* Caches used when allocating a new shadow page. */
struct shadow_page_caches {
	struct kvm_mmu_memory_cache *page_header_cache;
	struct kvm_mmu_memory_cache *shadow_page_cache;
	struct kvm_mmu_memory_cache *shadowed_info_cache;
};

static struct kvm_mmu_page *kvm_mmu_alloc_shadow_page(struct kvm *kvm,
						      struct shadow_page_caches *caches,
						      gfn_t gfn,
						      struct hlist_head *sp_list,
						      union kvm_mmu_page_role role)
{
	struct kvm_mmu_page *sp;

	sp = kvm_mmu_memory_cache_alloc(caches->page_header_cache);
	sp->spt = kvm_mmu_memory_cache_alloc(caches->shadow_page_cache);
	if (!role.direct)
		sp->shadowed_translation = kvm_mmu_memory_cache_alloc(caches->shadowed_info_cache);

	set_page_private(virt_to_page(sp->spt), (unsigned long)sp);

	INIT_LIST_HEAD(&sp->possible_nx_huge_page_link);

	/*
	 * active_mmu_pages must be a FIFO list, as kvm_zap_obsolete_pages()
	 * depends on valid pages being added to the head of the list.  See
	 * comments in kvm_zap_obsolete_pages().
	 */
	sp->mmu_valid_gen = kvm->arch.mmu_valid_gen;
	list_add(&sp->link, &kvm->arch.active_mmu_pages);
	kvm_account_mmu_page(kvm, sp);

	sp->gfn = gfn;
	sp->role = role;
	hlist_add_head(&sp->hash_link, sp_list);
	if (sp_has_gptes(sp))
		account_shadowed(kvm, sp);

	return sp;
}

/* Note, @vcpu may be NULL if @role.direct is true; see kvm_mmu_find_shadow_page. */
static struct kvm_mmu_page *__kvm_mmu_get_shadow_page(struct kvm *kvm,
						      struct kvm_vcpu *vcpu,
						      struct shadow_page_caches *caches,
						      gfn_t gfn,
						      union kvm_mmu_page_role role)
{
	struct hlist_head *sp_list;
	struct kvm_mmu_page *sp;
	bool created = false;

	sp_list = &kvm->arch.mmu_page_hash[kvm_page_table_hashfn(gfn)];

	sp = kvm_mmu_find_shadow_page(kvm, vcpu, gfn, sp_list, role);
	if (!sp) {
		created = true;
		sp = kvm_mmu_alloc_shadow_page(kvm, caches, gfn, sp_list, role);
	}

	trace_kvm_mmu_get_page(sp, created);
	return sp;
}

static struct kvm_mmu_page *kvm_mmu_get_shadow_page(struct kvm_vcpu *vcpu,
						    gfn_t gfn,
						    union kvm_mmu_page_role role)
{
	struct shadow_page_caches caches = {
		.page_header_cache = &vcpu->arch.mmu_page_header_cache,
		.shadow_page_cache = &vcpu->arch.mmu_shadow_page_cache,
		.shadowed_info_cache = &vcpu->arch.mmu_shadowed_info_cache,
	};

	return __kvm_mmu_get_shadow_page(vcpu->kvm, vcpu, &caches, gfn, role);
}

static union kvm_mmu_page_role kvm_mmu_child_role(u64 *sptep, bool direct,
						  unsigned int access)
{
	struct kvm_mmu_page *parent_sp = sptep_to_sp(sptep);
	union kvm_mmu_page_role role;

	role = parent_sp->role;
	role.level--;
	role.access = access;
	role.direct = direct;
	role.passthrough = 0;

	/*
	 * If the guest has 4-byte PTEs then that means it's using 32-bit,
	 * 2-level, non-PAE paging. KVM shadows such guests with PAE paging
	 * (i.e. 8-byte PTEs). The difference in PTE size means that KVM must
	 * shadow each guest page table with multiple shadow page tables, which
	 * requires extra bookkeeping in the role.
	 *
	 * Specifically, to shadow the guest's page directory (which covers a
	 * 4GiB address space), KVM uses 4 PAE page directories, each mapping
	 * 1GiB of the address space. @role.quadrant encodes which quarter of
	 * the address space each maps.
	 *
	 * To shadow the guest's page tables (which each map a 4MiB region), KVM
	 * uses 2 PAE page tables, each mapping a 2MiB region. For these,
	 * @role.quadrant encodes which half of the region they map.
	 *
	 * Concretely, a 4-byte PDE consumes bits 31:22, while an 8-byte PDE
	 * consumes bits 29:21.  To consume bits 31:30, KVM's uses 4 shadow
	 * PDPTEs; those 4 PAE page directories are pre-allocated and their
	 * quadrant is assigned in mmu_alloc_root().   A 4-byte PTE consumes
	 * bits 21:12, while an 8-byte PTE consumes bits 20:12.  To consume
	 * bit 21 in the PTE (the child here), KVM propagates that bit to the
	 * quadrant, i.e. sets quadrant to '0' or '1'.  The parent 8-byte PDE
	 * covers bit 21 (see above), thus the quadrant is calculated from the
	 * _least_ significant bit of the PDE index.
	 */
	if (role.has_4_byte_gpte) {
		WARN_ON_ONCE(role.level != PG_LEVEL_4K);
		role.quadrant = spte_index(sptep) & 1;
	}

	return role;
}

static struct kvm_mmu_page *kvm_mmu_get_child_sp(struct kvm_vcpu *vcpu,
						 u64 *sptep, gfn_t gfn,
						 bool direct, unsigned int access)
{
	union kvm_mmu_page_role role;

	if (is_shadow_present_pte(*sptep) && !is_large_pte(*sptep))
		return ERR_PTR(-EEXIST);

	role = kvm_mmu_child_role(sptep, direct, access);
	return kvm_mmu_get_shadow_page(vcpu, gfn, role);
}

static void shadow_walk_init_using_root(struct kvm_shadow_walk_iterator *iterator,
					struct kvm_vcpu *vcpu, hpa_t root,
					u64 addr)
{
	iterator->addr = addr;
	iterator->shadow_addr = root;
	iterator->level = vcpu->arch.mmu->root_role.level;

	if (iterator->level >= PT64_ROOT_4LEVEL &&
	    vcpu->arch.mmu->cpu_role.base.level < PT64_ROOT_4LEVEL &&
	    !vcpu->arch.mmu->root_role.direct)
		iterator->level = PT32E_ROOT_LEVEL;

	if (iterator->level == PT32E_ROOT_LEVEL) {
		/*
		 * prev_root is currently only used for 64-bit hosts. So only
		 * the active root_hpa is valid here.
		 */
		BUG_ON(root != vcpu->arch.mmu->root.hpa);

		iterator->shadow_addr
			= vcpu->arch.mmu->pae_root[(addr >> 30) & 3];
		iterator->shadow_addr &= SPTE_BASE_ADDR_MASK;
		--iterator->level;
		if (!iterator->shadow_addr)
			iterator->level = 0;
	}
}

static void shadow_walk_init(struct kvm_shadow_walk_iterator *iterator,
			     struct kvm_vcpu *vcpu, u64 addr)
{
	shadow_walk_init_using_root(iterator, vcpu, vcpu->arch.mmu->root.hpa,
				    addr);
}

static bool shadow_walk_okay(struct kvm_shadow_walk_iterator *iterator)
{
	if (iterator->level < PG_LEVEL_4K)
		return false;

	iterator->index = SPTE_INDEX(iterator->addr, iterator->level);
	iterator->sptep	= ((u64 *)__va(iterator->shadow_addr)) + iterator->index;
	return true;
}

static void __shadow_walk_next(struct kvm_shadow_walk_iterator *iterator,
			       u64 spte)
{
	if (!is_shadow_present_pte(spte) || is_last_spte(spte, iterator->level)) {
		iterator->level = 0;
		return;
	}

	iterator->shadow_addr = spte & SPTE_BASE_ADDR_MASK;
	--iterator->level;
}

static void shadow_walk_next(struct kvm_shadow_walk_iterator *iterator)
{
	__shadow_walk_next(iterator, *iterator->sptep);
}

static void __link_shadow_page(struct kvm *kvm,
			       struct kvm_mmu_memory_cache *cache, u64 *sptep,
			       struct kvm_mmu_page *sp, bool flush)
{
	u64 spte;

	BUILD_BUG_ON(VMX_EPT_WRITABLE_MASK != PT_WRITABLE_MASK);

	/*
	 * If an SPTE is present already, it must be a leaf and therefore
	 * a large one.  Drop it, and flush the TLB if needed, before
	 * installing sp.
	 */
	if (is_shadow_present_pte(*sptep))
		drop_large_spte(kvm, sptep, flush);

	spte = make_nonleaf_spte(sp->spt, sp_ad_disabled(sp));

	mmu_spte_set(sptep, spte);

	mmu_page_add_parent_pte(cache, sp, sptep);

	/*
	 * The non-direct sub-pagetable must be updated before linking.  For
	 * L1 sp, the pagetable is updated via kvm_sync_page() in
	 * kvm_mmu_find_shadow_page() without write-protecting the gfn,
	 * so sp->unsync can be true or false.  For higher level non-direct
	 * sp, the pagetable is updated/synced via mmu_sync_children() in
	 * FNAME(fetch)(), so sp->unsync_children can only be false.
	 * WARN_ON_ONCE() if anything happens unexpectedly.
	 */
	if (WARN_ON_ONCE(sp->unsync_children) || sp->unsync)
		mark_unsync(sptep);
}

static void link_shadow_page(struct kvm_vcpu *vcpu, u64 *sptep,
			     struct kvm_mmu_page *sp)
{
	__link_shadow_page(vcpu->kvm, &vcpu->arch.mmu_pte_list_desc_cache, sptep, sp, true);
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
		child = spte_to_child_sp(*sptep);
		if (child->role.access == direct_access)
			return;

		drop_parent_pte(vcpu->kvm, child, sptep);
		kvm_flush_remote_tlbs_sptep(vcpu->kvm, sptep);
	}
}

/* Returns the number of zapped non-leaf child shadow pages. */
static int mmu_page_zap_pte(struct kvm *kvm, struct kvm_mmu_page *sp,
			    u64 *spte, struct list_head *invalid_list)
{
	u64 pte;
	struct kvm_mmu_page *child;

	pte = *spte;
	if (is_shadow_present_pte(pte)) {
		if (is_last_spte(pte, sp->role.level)) {
			drop_spte(kvm, spte);
		} else {
			child = spte_to_child_sp(pte);
			drop_parent_pte(kvm, child, spte);

			/*
			 * Recursively zap nested TDP SPs, parentless SPs are
			 * unlikely to be used again in the near future.  This
			 * avoids retaining a large number of stale nested SPs.
			 */
			if (tdp_enabled && invalid_list &&
			    child->role.guest_mode && !child->parent_ptes.val)
				return kvm_mmu_prepare_zap_page(kvm, child,
								invalid_list);
		}
	} else if (is_mmio_spte(pte)) {
		mmu_spte_clear_no_track(spte);
	}
	return 0;
}

static int kvm_mmu_page_unlink_children(struct kvm *kvm,
					struct kvm_mmu_page *sp,
					struct list_head *invalid_list)
{
	int zapped = 0;
	unsigned i;

	for (i = 0; i < SPTE_ENT_PER_PAGE; ++i)
		zapped += mmu_page_zap_pte(kvm, sp, sp->spt + i, invalid_list);

	return zapped;
}

static void kvm_mmu_unlink_parents(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	u64 *sptep;
	struct rmap_iterator iter;

	while ((sptep = rmap_get_first(&sp->parent_ptes, &iter)))
		drop_parent_pte(kvm, sp, sptep);
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
	bool list_unstable, zapped_root = false;

	lockdep_assert_held_write(&kvm->mmu_lock);
	trace_kvm_mmu_prepare_zap_page(sp);
	++kvm->stat.mmu_shadow_zapped;
	*nr_zapped = mmu_zap_unsync_children(kvm, sp, invalid_list);
	*nr_zapped += kvm_mmu_page_unlink_children(kvm, sp, invalid_list);
	kvm_mmu_unlink_parents(kvm, sp);

	/* Zapping children means active_mmu_pages has become unstable. */
	list_unstable = *nr_zapped;

	if (!sp->role.invalid && sp_has_gptes(sp))
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
		kvm_unaccount_mmu_page(kvm, sp);
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
		zapped_root = !is_obsolete_sp(kvm, sp);
	}

	if (sp->nx_huge_page_disallowed)
		unaccount_nx_huge_page(kvm, sp);

	sp->role.invalid = 1;

	/*
	 * Make the request to free obsolete roots after marking the root
	 * invalid, otherwise other vCPUs may not see it as invalid.
	 */
	if (zapped_root)
		kvm_make_all_cpus_request(kvm, KVM_REQ_MMU_FREE_OBSOLETE_ROOTS);
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
		WARN_ON_ONCE(!sp->role.invalid || sp->root_count);
		kvm_mmu_free_shadow_page(sp);
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
	list_for_each_entry_safe_reverse(sp, tmp, &kvm->arch.active_mmu_pages, link) {
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

	/*
	 * Note, this check is intentionally soft, it only guarantees that one
	 * page is available, while the caller may end up allocating as many as
	 * four pages, e.g. for PAE roots or for 5-level paging.  Temporarily
	 * exceeding the (arbitrary by default) limit will not harm the host,
	 * being too aggressive may unnecessarily kill the guest, and getting an
	 * exact count is far more trouble than it's worth, especially in the
	 * page fault paths.
	 */
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
	write_lock(&kvm->mmu_lock);

	if (kvm->arch.n_used_mmu_pages > goal_nr_mmu_pages) {
		kvm_mmu_zap_oldest_mmu_pages(kvm, kvm->arch.n_used_mmu_pages -
						  goal_nr_mmu_pages);

		goal_nr_mmu_pages = kvm->arch.n_used_mmu_pages;
	}

	kvm->arch.n_max_mmu_pages = goal_nr_mmu_pages;

	write_unlock(&kvm->mmu_lock);
}

int kvm_mmu_unprotect_page(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_mmu_page *sp;
	LIST_HEAD(invalid_list);
	int r;

	r = 0;
	write_lock(&kvm->mmu_lock);
	for_each_gfn_valid_sp_with_gptes(kvm, sp, gfn) {
		r = 1;
		kvm_mmu_prepare_zap_page(kvm, sp, &invalid_list);
	}
	kvm_mmu_commit_zap_page(kvm, &invalid_list);
	write_unlock(&kvm->mmu_lock);

	return r;
}

static int kvm_mmu_unprotect_page_virt(struct kvm_vcpu *vcpu, gva_t gva)
{
	gpa_t gpa;
	int r;

	if (vcpu->arch.mmu->root_role.direct)
		return 0;

	gpa = kvm_mmu_gva_to_gpa_read(vcpu, gva, NULL);

	r = kvm_mmu_unprotect_page(vcpu->kvm, gpa >> PAGE_SHIFT);

	return r;
}

static void kvm_unsync_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	trace_kvm_mmu_unsync_page(sp);
	++kvm->stat.mmu_unsync;
	sp->unsync = 1;

	kvm_mmu_mark_parents_unsync(sp);
}

/*
 * Attempt to unsync any shadow pages that can be reached by the specified gfn,
 * KVM is creating a writable mapping for said gfn.  Returns 0 if all pages
 * were marked unsync (or if there is no shadow page), -EPERM if the SPTE must
 * be write-protected.
 */
int mmu_try_to_unsync_pages(struct kvm *kvm, const struct kvm_memory_slot *slot,
			    gfn_t gfn, bool can_unsync, bool prefetch)
{
	struct kvm_mmu_page *sp;
	bool locked = false;

	/*
	 * Force write-protection if the page is being tracked.  Note, the page
	 * track machinery is used to write-protect upper-level shadow pages,
	 * i.e. this guards the role.level == 4K assertion below!
	 */
	if (kvm_gfn_is_write_tracked(kvm, slot, gfn))
		return -EPERM;

	/*
	 * The page is not write-tracked, mark existing shadow pages unsync
	 * unless KVM is synchronizing an unsync SP (can_unsync = false).  In
	 * that case, KVM must complete emulation of the guest TLB flush before
	 * allowing shadow pages to become unsync (writable by the guest).
	 */
	for_each_gfn_valid_sp_with_gptes(kvm, sp, gfn) {
		if (!can_unsync)
			return -EPERM;

		if (sp->unsync)
			continue;

		if (prefetch)
			return -EEXIST;

		/*
		 * TDP MMU page faults require an additional spinlock as they
		 * run with mmu_lock held for read, not write, and the unsync
		 * logic is not thread safe.  Take the spinklock regardless of
		 * the MMU type to avoid extra conditionals/parameters, there's
		 * no meaningful penalty if mmu_lock is held for write.
		 */
		if (!locked) {
			locked = true;
			spin_lock(&kvm->arch.mmu_unsync_pages_lock);

			/*
			 * Recheck after taking the spinlock, a different vCPU
			 * may have since marked the page unsync.  A false
			 * negative on the unprotected check above is not
			 * possible as clearing sp->unsync _must_ hold mmu_lock
			 * for write, i.e. unsync cannot transition from 1->0
			 * while this CPU holds mmu_lock for read (or write).
			 */
			if (READ_ONCE(sp->unsync))
				continue;
		}

		WARN_ON_ONCE(sp->role.level != PG_LEVEL_4K);
		kvm_unsync_page(kvm, sp);
	}
	if (locked)
		spin_unlock(&kvm->arch.mmu_unsync_pages_lock);

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
	 *                      2.3 Walking of unsync pages sees sp->unsync is
	 *                          false and skips the page.
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
	 * the situation in 2.4 does not arise.  It pairs with the read barrier
	 * in is_unsync_root(), placed between 2.1's load of SPTE.W and 2.3.
	 */
	smp_wmb();

	return 0;
}

static int mmu_set_spte(struct kvm_vcpu *vcpu, struct kvm_memory_slot *slot,
			u64 *sptep, unsigned int pte_access, gfn_t gfn,
			kvm_pfn_t pfn, struct kvm_page_fault *fault)
{
	struct kvm_mmu_page *sp = sptep_to_sp(sptep);
	int level = sp->role.level;
	int was_rmapped = 0;
	int ret = RET_PF_FIXED;
	bool flush = false;
	bool wrprot;
	u64 spte;

	/* Prefetching always gets a writable pfn.  */
	bool host_writable = !fault || fault->map_writable;
	bool prefetch = !fault || fault->prefetch;
	bool write_fault = fault && fault->write;

	if (unlikely(is_noslot_pfn(pfn))) {
		vcpu->stat.pf_mmio_spte_created++;
		mark_mmio_spte(vcpu, sptep, gfn, pte_access);
		return RET_PF_EMULATE;
	}

	if (is_shadow_present_pte(*sptep)) {
		/*
		 * If we overwrite a PTE page pointer with a 2MB PMD, unlink
		 * the parent of the now unreachable PTE.
		 */
		if (level > PG_LEVEL_4K && !is_large_pte(*sptep)) {
			struct kvm_mmu_page *child;
			u64 pte = *sptep;

			child = spte_to_child_sp(pte);
			drop_parent_pte(vcpu->kvm, child, sptep);
			flush = true;
		} else if (pfn != spte_to_pfn(*sptep)) {
			drop_spte(vcpu->kvm, sptep);
			flush = true;
		} else
			was_rmapped = 1;
	}

	wrprot = make_spte(vcpu, sp, slot, pte_access, gfn, pfn, *sptep, prefetch,
			   true, host_writable, &spte);

	if (*sptep == spte) {
		ret = RET_PF_SPURIOUS;
	} else {
		flush |= mmu_spte_update(sptep, spte);
		trace_kvm_mmu_set_spte(level, gfn, sptep);
	}

	if (wrprot) {
		if (write_fault)
			ret = RET_PF_EMULATE;
	}

	if (flush)
		kvm_flush_remote_tlbs_gfn(vcpu->kvm, gfn, level);

	if (!was_rmapped) {
		WARN_ON_ONCE(ret == RET_PF_SPURIOUS);
		rmap_add(vcpu, slot, sptep, gfn, pte_access);
	} else {
		/* Already rmapped but the pte_access bits may have changed. */
		kvm_mmu_page_set_access(sp, spte_index(sptep), pte_access);
	}

	return ret;
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

	gfn = kvm_mmu_page_get_gfn(sp, spte_index(start));
	slot = gfn_to_memslot_dirty_bitmap(vcpu, gfn, access & ACC_WRITE_MASK);
	if (!slot)
		return -1;

	ret = gfn_to_page_many_atomic(slot, gfn, pages, end - start);
	if (ret <= 0)
		return -1;

	for (i = 0; i < ret; i++, gfn++, start++) {
		mmu_set_spte(vcpu, slot, start, access, gfn,
			     page_to_pfn(pages[i]), NULL);
		put_page(pages[i]);
	}

	return 0;
}

static void __direct_pte_prefetch(struct kvm_vcpu *vcpu,
				  struct kvm_mmu_page *sp, u64 *sptep)
{
	u64 *spte, *start = NULL;
	int i;

	WARN_ON_ONCE(!sp->role.direct);

	i = spte_index(sptep) & ~(PTE_PREFETCH_NUM - 1);
	spte = sp->spt + i;

	for (i = 0; i < PTE_PREFETCH_NUM; i++, spte++) {
		if (is_shadow_present_pte(*spte) || spte == sptep) {
			if (!start)
				continue;
			if (direct_pte_prefetch_many(vcpu, sp, start, spte) < 0)
				return;
			start = NULL;
		} else if (!start)
			start = spte;
	}
	if (start)
		direct_pte_prefetch_many(vcpu, sp, start, spte);
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

	/*
	 * If addresses are being invalidated, skip prefetching to avoid
	 * accidentally prefetching those addresses.
	 */
	if (unlikely(vcpu->kvm->mmu_invalidate_in_progress))
		return;

	__direct_pte_prefetch(vcpu, sp, sptep);
}

/*
 * Lookup the mapping level for @gfn in the current mm.
 *
 * WARNING!  Use of host_pfn_mapping_level() requires the caller and the end
 * consumer to be tied into KVM's handlers for MMU notifier events!
 *
 * There are several ways to safely use this helper:
 *
 * - Check mmu_invalidate_retry_gfn() after grabbing the mapping level, before
 *   consuming it.  In this case, mmu_lock doesn't need to be held during the
 *   lookup, but it does need to be held while checking the MMU notifier.
 *
 * - Hold mmu_lock AND ensure there is no in-progress MMU notifier invalidation
 *   event for the hva.  This can be done by explicit checking the MMU notifier
 *   or by ensuring that KVM already has a valid mapping that covers the hva.
 *
 * - Do not use the result to install new mappings, e.g. use the host mapping
 *   level only to decide whether or not to zap an entry.  In this case, it's
 *   not required to hold mmu_lock (though it's highly likely the caller will
 *   want to hold mmu_lock anyways, e.g. to modify SPTEs).
 *
 * Note!  The lookup can still race with modifications to host page tables, but
 * the above "rules" ensure KVM will not _consume_ the result of the walk if a
 * race with the primary MMU occurs.
 */
static int host_pfn_mapping_level(struct kvm *kvm, gfn_t gfn,
				  const struct kvm_memory_slot *slot)
{
	int level = PG_LEVEL_4K;
	unsigned long hva;
	unsigned long flags;
	pgd_t pgd;
	p4d_t p4d;
	pud_t pud;
	pmd_t pmd;

	/*
	 * Note, using the already-retrieved memslot and __gfn_to_hva_memslot()
	 * is not solely for performance, it's also necessary to avoid the
	 * "writable" check in __gfn_to_hva_many(), which will always fail on
	 * read-only memslots due to gfn_to_hva() assuming writes.  Earlier
	 * page fault steps have already verified the guest isn't writing a
	 * read-only memslot.
	 */
	hva = __gfn_to_hva_memslot(slot, gfn);

	/*
	 * Disable IRQs to prevent concurrent tear down of host page tables,
	 * e.g. if the primary MMU promotes a P*D to a huge page and then frees
	 * the original page table.
	 */
	local_irq_save(flags);

	/*
	 * Read each entry once.  As above, a non-leaf entry can be promoted to
	 * a huge page _during_ this walk.  Re-reading the entry could send the
	 * walk into the weeks, e.g. p*d_leaf() returns false (sees the old
	 * value) and then p*d_offset() walks into the target huge page instead
	 * of the old page table (sees the new value).
	 */
	pgd = READ_ONCE(*pgd_offset(kvm->mm, hva));
	if (pgd_none(pgd))
		goto out;

	p4d = READ_ONCE(*p4d_offset(&pgd, hva));
	if (p4d_none(p4d) || !p4d_present(p4d))
		goto out;

	pud = READ_ONCE(*pud_offset(&p4d, hva));
	if (pud_none(pud) || !pud_present(pud))
		goto out;

	if (pud_leaf(pud)) {
		level = PG_LEVEL_1G;
		goto out;
	}

	pmd = READ_ONCE(*pmd_offset(&pud, hva));
	if (pmd_none(pmd) || !pmd_present(pmd))
		goto out;

	if (pmd_leaf(pmd))
		level = PG_LEVEL_2M;

out:
	local_irq_restore(flags);
	return level;
}

static int __kvm_mmu_max_mapping_level(struct kvm *kvm,
				       const struct kvm_memory_slot *slot,
				       gfn_t gfn, int max_level, bool is_private)
{
	struct kvm_lpage_info *linfo;
	int host_level;

	max_level = min(max_level, max_huge_page_level);
	for ( ; max_level > PG_LEVEL_4K; max_level--) {
		linfo = lpage_info_slot(gfn, slot, max_level);
		if (!linfo->disallow_lpage)
			break;
	}

	if (is_private)
		return max_level;

	if (max_level == PG_LEVEL_4K)
		return PG_LEVEL_4K;

	host_level = host_pfn_mapping_level(kvm, gfn, slot);
	return min(host_level, max_level);
}

int kvm_mmu_max_mapping_level(struct kvm *kvm,
			      const struct kvm_memory_slot *slot, gfn_t gfn,
			      int max_level)
{
	bool is_private = kvm_slot_can_be_private(slot) &&
			  kvm_mem_is_private(kvm, gfn);

	return __kvm_mmu_max_mapping_level(kvm, slot, gfn, max_level, is_private);
}

void kvm_mmu_hugepage_adjust(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault)
{
	struct kvm_memory_slot *slot = fault->slot;
	kvm_pfn_t mask;

	fault->huge_page_disallowed = fault->exec && fault->nx_huge_page_workaround_enabled;

	if (unlikely(fault->max_level == PG_LEVEL_4K))
		return;

	if (is_error_noslot_pfn(fault->pfn))
		return;

	if (kvm_slot_dirty_track_enabled(slot))
		return;

	/*
	 * Enforce the iTLB multihit workaround after capturing the requested
	 * level, which will be used to do precise, accurate accounting.
	 */
	fault->req_level = __kvm_mmu_max_mapping_level(vcpu->kvm, slot,
						       fault->gfn, fault->max_level,
						       fault->is_private);
	if (fault->req_level == PG_LEVEL_4K || fault->huge_page_disallowed)
		return;

	/*
	 * mmu_invalidate_retry() was successful and mmu_lock is held, so
	 * the pmd can't be split from under us.
	 */
	fault->goal_level = fault->req_level;
	mask = KVM_PAGES_PER_HPAGE(fault->goal_level) - 1;
	VM_BUG_ON((fault->gfn & mask) != (fault->pfn & mask));
	fault->pfn &= ~mask;
}

void disallowed_hugepage_adjust(struct kvm_page_fault *fault, u64 spte, int cur_level)
{
	if (cur_level > PG_LEVEL_4K &&
	    cur_level == fault->goal_level &&
	    is_shadow_present_pte(spte) &&
	    !is_large_pte(spte) &&
	    spte_to_child_sp(spte)->nx_huge_page_disallowed) {
		/*
		 * A small SPTE exists for this pfn, but FNAME(fetch),
		 * direct_map(), or kvm_tdp_mmu_map() would like to create a
		 * large PTE instead: just force them to go down another level,
		 * patching back for them into pfn the next 9 bits of the
		 * address.
		 */
		u64 page_mask = KVM_PAGES_PER_HPAGE(cur_level) -
				KVM_PAGES_PER_HPAGE(cur_level - 1);
		fault->pfn |= fault->gfn & page_mask;
		fault->goal_level--;
	}
}

static int direct_map(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault)
{
	struct kvm_shadow_walk_iterator it;
	struct kvm_mmu_page *sp;
	int ret;
	gfn_t base_gfn = fault->gfn;

	kvm_mmu_hugepage_adjust(vcpu, fault);

	trace_kvm_mmu_spte_requested(fault);
	for_each_shadow_entry(vcpu, fault->addr, it) {
		/*
		 * We cannot overwrite existing page tables with an NX
		 * large page, as the leaf could be executable.
		 */
		if (fault->nx_huge_page_workaround_enabled)
			disallowed_hugepage_adjust(fault, *it.sptep, it.level);

		base_gfn = gfn_round_for_level(fault->gfn, it.level);
		if (it.level == fault->goal_level)
			break;

		sp = kvm_mmu_get_child_sp(vcpu, it.sptep, base_gfn, true, ACC_ALL);
		if (sp == ERR_PTR(-EEXIST))
			continue;

		link_shadow_page(vcpu, it.sptep, sp);
		if (fault->huge_page_disallowed)
			account_nx_huge_page(vcpu->kvm, sp,
					     fault->req_level >= it.level);
	}

	if (WARN_ON_ONCE(it.level != fault->goal_level))
		return -EFAULT;

	ret = mmu_set_spte(vcpu, fault->slot, it.sptep, ACC_ALL,
			   base_gfn, fault->pfn, fault);
	if (ret == RET_PF_SPURIOUS)
		return ret;

	direct_pte_prefetch(vcpu, it.sptep);
	return ret;
}

static void kvm_send_hwpoison_signal(struct kvm_memory_slot *slot, gfn_t gfn)
{
	unsigned long hva = gfn_to_hva_memslot(slot, gfn);

	send_sig_mceerr(BUS_MCEERR_AR, (void __user *)hva, PAGE_SHIFT, current);
}

static int kvm_handle_error_pfn(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault)
{
	if (is_sigpending_pfn(fault->pfn)) {
		kvm_handle_signal_exit(vcpu);
		return -EINTR;
	}

	/*
	 * Do not cache the mmio info caused by writing the readonly gfn
	 * into the spte otherwise read access on readonly gfn also can
	 * caused mmio page fault and treat it as mmio access.
	 */
	if (fault->pfn == KVM_PFN_ERR_RO_FAULT)
		return RET_PF_EMULATE;

	if (fault->pfn == KVM_PFN_ERR_HWPOISON) {
		kvm_send_hwpoison_signal(fault->slot, fault->gfn);
		return RET_PF_RETRY;
	}

	return -EFAULT;
}

static int kvm_handle_noslot_fault(struct kvm_vcpu *vcpu,
				   struct kvm_page_fault *fault,
				   unsigned int access)
{
	gva_t gva = fault->is_tdp ? 0 : fault->addr;

	vcpu_cache_mmio_info(vcpu, gva, fault->gfn,
			     access & shadow_mmio_access_mask);

	/*
	 * If MMIO caching is disabled, emulate immediately without
	 * touching the shadow page tables as attempting to install an
	 * MMIO SPTE will just be an expensive nop.
	 */
	if (unlikely(!enable_mmio_caching))
		return RET_PF_EMULATE;

	/*
	 * Do not create an MMIO SPTE for a gfn greater than host.MAXPHYADDR,
	 * any guest that generates such gfns is running nested and is being
	 * tricked by L0 userspace (you can observe gfn > L1.MAXPHYADDR if and
	 * only if L1's MAXPHYADDR is inaccurate with respect to the
	 * hardware's).
	 */
	if (unlikely(fault->gfn > kvm_mmu_max_gfn()))
		return RET_PF_EMULATE;

	return RET_PF_CONTINUE;
}

static bool page_fault_can_be_fast(struct kvm_page_fault *fault)
{
	/*
	 * Page faults with reserved bits set, i.e. faults on MMIO SPTEs, only
	 * reach the common page fault handler if the SPTE has an invalid MMIO
	 * generation number.  Refreshing the MMIO generation needs to go down
	 * the slow path.  Note, EPT Misconfigs do NOT set the PRESENT flag!
	 */
	if (fault->rsvd)
		return false;

	/*
	 * #PF can be fast if:
	 *
	 * 1. The shadow page table entry is not present and A/D bits are
	 *    disabled _by KVM_, which could mean that the fault is potentially
	 *    caused by access tracking (if enabled).  If A/D bits are enabled
	 *    by KVM, but disabled by L1 for L2, KVM is forced to disable A/D
	 *    bits for L2 and employ access tracking, but the fast page fault
	 *    mechanism only supports direct MMUs.
	 * 2. The shadow page table entry is present, the access is a write,
	 *    and no reserved bits are set (MMIO SPTEs cannot be "fixed"), i.e.
	 *    the fault was caused by a write-protection violation.  If the
	 *    SPTE is MMU-writable (determined later), the fault can be fixed
	 *    by setting the Writable bit, which can be done out of mmu_lock.
	 */
	if (!fault->present)
		return !kvm_ad_enabled();

	/*
	 * Note, instruction fetches and writes are mutually exclusive, ignore
	 * the "exec" flag.
	 */
	return fault->write;
}

/*
 * Returns true if the SPTE was fixed successfully. Otherwise,
 * someone else modified the SPTE from its original value.
 */
static bool fast_pf_fix_direct_spte(struct kvm_vcpu *vcpu,
				    struct kvm_page_fault *fault,
				    u64 *sptep, u64 old_spte, u64 new_spte)
{
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
	if (!try_cmpxchg64(sptep, &old_spte, new_spte))
		return false;

	if (is_writable_pte(new_spte) && !is_writable_pte(old_spte))
		mark_page_dirty_in_slot(vcpu->kvm, fault->slot, fault->gfn);

	return true;
}

static bool is_access_allowed(struct kvm_page_fault *fault, u64 spte)
{
	if (fault->exec)
		return is_executable_pte(spte);

	if (fault->write)
		return is_writable_pte(spte);

	/* Fault was on Read access */
	return spte & PT_PRESENT_MASK;
}

/*
 * Returns the last level spte pointer of the shadow page walk for the given
 * gpa, and sets *spte to the spte value. This spte may be non-preset. If no
 * walk could be performed, returns NULL and *spte does not contain valid data.
 *
 * Contract:
 *  - Must be called between walk_shadow_page_lockless_{begin,end}.
 *  - The returned sptep must not be used after walk_shadow_page_lockless_end.
 */
static u64 *fast_pf_get_last_sptep(struct kvm_vcpu *vcpu, gpa_t gpa, u64 *spte)
{
	struct kvm_shadow_walk_iterator iterator;
	u64 old_spte;
	u64 *sptep = NULL;

	for_each_shadow_entry_lockless(vcpu, gpa, iterator, old_spte) {
		sptep = iterator.sptep;
		*spte = old_spte;
	}

	return sptep;
}

/*
 * Returns one of RET_PF_INVALID, RET_PF_FIXED or RET_PF_SPURIOUS.
 */
static int fast_page_fault(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault)
{
	struct kvm_mmu_page *sp;
	int ret = RET_PF_INVALID;
	u64 spte;
	u64 *sptep;
	uint retry_count = 0;

	if (!page_fault_can_be_fast(fault))
		return ret;

	walk_shadow_page_lockless_begin(vcpu);

	do {
		u64 new_spte;

		if (tdp_mmu_enabled)
			sptep = kvm_tdp_mmu_fast_pf_get_last_sptep(vcpu, fault->addr, &spte);
		else
			sptep = fast_pf_get_last_sptep(vcpu, fault->addr, &spte);

		/*
		 * It's entirely possible for the mapping to have been zapped
		 * by a different task, but the root page should always be
		 * available as the vCPU holds a reference to its root(s).
		 */
		if (WARN_ON_ONCE(!sptep))
			spte = REMOVED_SPTE;

		if (!is_shadow_present_pte(spte))
			break;

		sp = sptep_to_sp(sptep);
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
		if (is_access_allowed(fault, spte)) {
			ret = RET_PF_SPURIOUS;
			break;
		}

		new_spte = spte;

		/*
		 * KVM only supports fixing page faults outside of MMU lock for
		 * direct MMUs, nested MMUs are always indirect, and KVM always
		 * uses A/D bits for non-nested MMUs.  Thus, if A/D bits are
		 * enabled, the SPTE can't be an access-tracked SPTE.
		 */
		if (unlikely(!kvm_ad_enabled()) && is_access_track_spte(spte))
			new_spte = restore_acc_track_spte(new_spte);

		/*
		 * To keep things simple, only SPTEs that are MMU-writable can
		 * be made fully writable outside of mmu_lock, e.g. only SPTEs
		 * that were write-protected for dirty-logging or access
		 * tracking are handled here.  Don't bother checking if the
		 * SPTE is writable to prioritize running with A/D bits enabled.
		 * The is_access_allowed() check above handles the common case
		 * of the fault being spurious, and the SPTE is known to be
		 * shadow-present, i.e. except for access tracking restoration
		 * making the new SPTE writable, the check is wasteful.
		 */
		if (fault->write && is_mmu_writable_spte(spte)) {
			new_spte |= PT_WRITABLE_MASK;

			/*
			 * Do not fix write-permission on the large spte when
			 * dirty logging is enabled. Since we only dirty the
			 * first page into the dirty-bitmap in
			 * fast_pf_fix_direct_spte(), other pages are missed
			 * if its slot has dirty logging enabled.
			 *
			 * Instead, we let the slow page fault path create a
			 * normal spte to fix the access.
			 */
			if (sp->role.level > PG_LEVEL_4K &&
			    kvm_slot_dirty_track_enabled(fault->slot))
				break;
		}

		/* Verify that the fault can be handled in the fast path */
		if (new_spte == spte ||
		    !is_access_allowed(fault, new_spte))
			break;

		/*
		 * Currently, fast page fault only works for direct mapping
		 * since the gfn is not stable for indirect shadow page. See
		 * Documentation/virt/kvm/locking.rst to get more detail.
		 */
		if (fast_pf_fix_direct_spte(vcpu, fault, sptep, spte, new_spte)) {
			ret = RET_PF_FIXED;
			break;
		}

		if (++retry_count > 4) {
			pr_warn_once("Fast #PF retrying more than 4 times.\n");
			break;
		}

	} while (true);

	trace_fast_page_fault(vcpu, fault, sptep, spte, ret);
	walk_shadow_page_lockless_end(vcpu);

	if (ret != RET_PF_INVALID)
		vcpu->stat.pf_fast++;

	return ret;
}

static void mmu_free_root_page(struct kvm *kvm, hpa_t *root_hpa,
			       struct list_head *invalid_list)
{
	struct kvm_mmu_page *sp;

	if (!VALID_PAGE(*root_hpa))
		return;

	sp = root_to_sp(*root_hpa);
	if (WARN_ON_ONCE(!sp))
		return;

	if (is_tdp_mmu_page(sp)) {
		lockdep_assert_held_read(&kvm->mmu_lock);
		kvm_tdp_mmu_put_root(kvm, sp);
	} else {
		lockdep_assert_held_write(&kvm->mmu_lock);
		if (!--sp->root_count && sp->role.invalid)
			kvm_mmu_prepare_zap_page(kvm, sp, invalid_list);
	}

	*root_hpa = INVALID_PAGE;
}

/* roots_to_free must be some combination of the KVM_MMU_ROOT_* flags */
void kvm_mmu_free_roots(struct kvm *kvm, struct kvm_mmu *mmu,
			ulong roots_to_free)
{
	bool is_tdp_mmu = tdp_mmu_enabled && mmu->root_role.direct;
	int i;
	LIST_HEAD(invalid_list);
	bool free_active_root;

	WARN_ON_ONCE(roots_to_free & ~KVM_MMU_ROOTS_ALL);

	BUILD_BUG_ON(KVM_MMU_NUM_PREV_ROOTS >= BITS_PER_LONG);

	/* Before acquiring the MMU lock, see if we need to do any real work. */
	free_active_root = (roots_to_free & KVM_MMU_ROOT_CURRENT)
		&& VALID_PAGE(mmu->root.hpa);

	if (!free_active_root) {
		for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
			if ((roots_to_free & KVM_MMU_ROOT_PREVIOUS(i)) &&
			    VALID_PAGE(mmu->prev_roots[i].hpa))
				break;

		if (i == KVM_MMU_NUM_PREV_ROOTS)
			return;
	}

	if (is_tdp_mmu)
		read_lock(&kvm->mmu_lock);
	else
		write_lock(&kvm->mmu_lock);

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
		if (roots_to_free & KVM_MMU_ROOT_PREVIOUS(i))
			mmu_free_root_page(kvm, &mmu->prev_roots[i].hpa,
					   &invalid_list);

	if (free_active_root) {
		if (kvm_mmu_is_dummy_root(mmu->root.hpa)) {
			/* Nothing to cleanup for dummy roots. */
		} else if (root_to_sp(mmu->root.hpa)) {
			mmu_free_root_page(kvm, &mmu->root.hpa, &invalid_list);
		} else if (mmu->pae_root) {
			for (i = 0; i < 4; ++i) {
				if (!IS_VALID_PAE_ROOT(mmu->pae_root[i]))
					continue;

				mmu_free_root_page(kvm, &mmu->pae_root[i],
						   &invalid_list);
				mmu->pae_root[i] = INVALID_PAE_ROOT;
			}
		}
		mmu->root.hpa = INVALID_PAGE;
		mmu->root.pgd = 0;
	}

	if (is_tdp_mmu) {
		read_unlock(&kvm->mmu_lock);
		WARN_ON_ONCE(!list_empty(&invalid_list));
	} else {
		kvm_mmu_commit_zap_page(kvm, &invalid_list);
		write_unlock(&kvm->mmu_lock);
	}
}
EXPORT_SYMBOL_GPL(kvm_mmu_free_roots);

void kvm_mmu_free_guest_mode_roots(struct kvm *kvm, struct kvm_mmu *mmu)
{
	unsigned long roots_to_free = 0;
	struct kvm_mmu_page *sp;
	hpa_t root_hpa;
	int i;

	/*
	 * This should not be called while L2 is active, L2 can't invalidate
	 * _only_ its own roots, e.g. INVVPID unconditionally exits.
	 */
	WARN_ON_ONCE(mmu->root_role.guest_mode);

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++) {
		root_hpa = mmu->prev_roots[i].hpa;
		if (!VALID_PAGE(root_hpa))
			continue;

		sp = root_to_sp(root_hpa);
		if (!sp || sp->role.guest_mode)
			roots_to_free |= KVM_MMU_ROOT_PREVIOUS(i);
	}

	kvm_mmu_free_roots(kvm, mmu, roots_to_free);
}
EXPORT_SYMBOL_GPL(kvm_mmu_free_guest_mode_roots);

static hpa_t mmu_alloc_root(struct kvm_vcpu *vcpu, gfn_t gfn, int quadrant,
			    u8 level)
{
	union kvm_mmu_page_role role = vcpu->arch.mmu->root_role;
	struct kvm_mmu_page *sp;

	role.level = level;
	role.quadrant = quadrant;

	WARN_ON_ONCE(quadrant && !role.has_4_byte_gpte);
	WARN_ON_ONCE(role.direct && role.has_4_byte_gpte);

	sp = kvm_mmu_get_shadow_page(vcpu, gfn, role);
	++sp->root_count;

	return __pa(sp->spt);
}

static int mmu_alloc_direct_roots(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	u8 shadow_root_level = mmu->root_role.level;
	hpa_t root;
	unsigned i;
	int r;

	if (tdp_mmu_enabled)
		return kvm_tdp_mmu_alloc_root(vcpu);

	write_lock(&vcpu->kvm->mmu_lock);
	r = make_mmu_pages_available(vcpu);
	if (r < 0)
		goto out_unlock;

	if (shadow_root_level >= PT64_ROOT_4LEVEL) {
		root = mmu_alloc_root(vcpu, 0, 0, shadow_root_level);
		mmu->root.hpa = root;
	} else if (shadow_root_level == PT32E_ROOT_LEVEL) {
		if (WARN_ON_ONCE(!mmu->pae_root)) {
			r = -EIO;
			goto out_unlock;
		}

		for (i = 0; i < 4; ++i) {
			WARN_ON_ONCE(IS_VALID_PAE_ROOT(mmu->pae_root[i]));

			root = mmu_alloc_root(vcpu, i << (30 - PAGE_SHIFT), 0,
					      PT32_ROOT_LEVEL);
			mmu->pae_root[i] = root | PT_PRESENT_MASK |
					   shadow_me_value;
		}
		mmu->root.hpa = __pa(mmu->pae_root);
	} else {
		WARN_ONCE(1, "Bad TDP root level = %d\n", shadow_root_level);
		r = -EIO;
		goto out_unlock;
	}

	/* root.pgd is ignored for direct MMUs. */
	mmu->root.pgd = 0;
out_unlock:
	write_unlock(&vcpu->kvm->mmu_lock);
	return r;
}

static int mmu_first_shadow_root_alloc(struct kvm *kvm)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;
	int r = 0, i, bkt;

	/*
	 * Check if this is the first shadow root being allocated before
	 * taking the lock.
	 */
	if (kvm_shadow_root_allocated(kvm))
		return 0;

	mutex_lock(&kvm->slots_arch_lock);

	/* Recheck, under the lock, whether this is the first shadow root. */
	if (kvm_shadow_root_allocated(kvm))
		goto out_unlock;

	/*
	 * Check if anything actually needs to be allocated, e.g. all metadata
	 * will be allocated upfront if TDP is disabled.
	 */
	if (kvm_memslots_have_rmaps(kvm) &&
	    kvm_page_track_write_tracking_enabled(kvm))
		goto out_success;

	for (i = 0; i < kvm_arch_nr_memslot_as_ids(kvm); i++) {
		slots = __kvm_memslots(kvm, i);
		kvm_for_each_memslot(slot, bkt, slots) {
			/*
			 * Both of these functions are no-ops if the target is
			 * already allocated, so unconditionally calling both
			 * is safe.  Intentionally do NOT free allocations on
			 * failure to avoid having to track which allocations
			 * were made now versus when the memslot was created.
			 * The metadata is guaranteed to be freed when the slot
			 * is freed, and will be kept/used if userspace retries
			 * KVM_RUN instead of killing the VM.
			 */
			r = memslot_rmap_alloc(slot, slot->npages);
			if (r)
				goto out_unlock;
			r = kvm_page_track_write_tracking_alloc(slot);
			if (r)
				goto out_unlock;
		}
	}

	/*
	 * Ensure that shadow_root_allocated becomes true strictly after
	 * all the related pointers are set.
	 */
out_success:
	smp_store_release(&kvm->arch.shadow_root_allocated, true);

out_unlock:
	mutex_unlock(&kvm->slots_arch_lock);
	return r;
}

static int mmu_alloc_shadow_roots(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	u64 pdptrs[4], pm_mask;
	gfn_t root_gfn, root_pgd;
	int quadrant, i, r;
	hpa_t root;

	root_pgd = kvm_mmu_get_guest_pgd(vcpu, mmu);
	root_gfn = (root_pgd & __PT_BASE_ADDR_MASK) >> PAGE_SHIFT;

	if (!kvm_vcpu_is_visible_gfn(vcpu, root_gfn)) {
		mmu->root.hpa = kvm_mmu_get_dummy_root();
		return 0;
	}

	/*
	 * On SVM, reading PDPTRs might access guest memory, which might fault
	 * and thus might sleep.  Grab the PDPTRs before acquiring mmu_lock.
	 */
	if (mmu->cpu_role.base.level == PT32E_ROOT_LEVEL) {
		for (i = 0; i < 4; ++i) {
			pdptrs[i] = mmu->get_pdptr(vcpu, i);
			if (!(pdptrs[i] & PT_PRESENT_MASK))
				continue;

			if (!kvm_vcpu_is_visible_gfn(vcpu, pdptrs[i] >> PAGE_SHIFT))
				pdptrs[i] = 0;
		}
	}

	r = mmu_first_shadow_root_alloc(vcpu->kvm);
	if (r)
		return r;

	write_lock(&vcpu->kvm->mmu_lock);
	r = make_mmu_pages_available(vcpu);
	if (r < 0)
		goto out_unlock;

	/*
	 * Do we shadow a long mode page table? If so we need to
	 * write-protect the guests page table root.
	 */
	if (mmu->cpu_role.base.level >= PT64_ROOT_4LEVEL) {
		root = mmu_alloc_root(vcpu, root_gfn, 0,
				      mmu->root_role.level);
		mmu->root.hpa = root;
		goto set_root_pgd;
	}

	if (WARN_ON_ONCE(!mmu->pae_root)) {
		r = -EIO;
		goto out_unlock;
	}

	/*
	 * We shadow a 32 bit page table. This may be a legacy 2-level
	 * or a PAE 3-level page table. In either case we need to be aware that
	 * the shadow page table may be a PAE or a long mode page table.
	 */
	pm_mask = PT_PRESENT_MASK | shadow_me_value;
	if (mmu->root_role.level >= PT64_ROOT_4LEVEL) {
		pm_mask |= PT_ACCESSED_MASK | PT_WRITABLE_MASK | PT_USER_MASK;

		if (WARN_ON_ONCE(!mmu->pml4_root)) {
			r = -EIO;
			goto out_unlock;
		}
		mmu->pml4_root[0] = __pa(mmu->pae_root) | pm_mask;

		if (mmu->root_role.level == PT64_ROOT_5LEVEL) {
			if (WARN_ON_ONCE(!mmu->pml5_root)) {
				r = -EIO;
				goto out_unlock;
			}
			mmu->pml5_root[0] = __pa(mmu->pml4_root) | pm_mask;
		}
	}

	for (i = 0; i < 4; ++i) {
		WARN_ON_ONCE(IS_VALID_PAE_ROOT(mmu->pae_root[i]));

		if (mmu->cpu_role.base.level == PT32E_ROOT_LEVEL) {
			if (!(pdptrs[i] & PT_PRESENT_MASK)) {
				mmu->pae_root[i] = INVALID_PAE_ROOT;
				continue;
			}
			root_gfn = pdptrs[i] >> PAGE_SHIFT;
		}

		/*
		 * If shadowing 32-bit non-PAE page tables, each PAE page
		 * directory maps one quarter of the guest's non-PAE page
		 * directory. Othwerise each PAE page direct shadows one guest
		 * PAE page directory so that quadrant should be 0.
		 */
		quadrant = (mmu->cpu_role.base.level == PT32_ROOT_LEVEL) ? i : 0;

		root = mmu_alloc_root(vcpu, root_gfn, quadrant, PT32_ROOT_LEVEL);
		mmu->pae_root[i] = root | pm_mask;
	}

	if (mmu->root_role.level == PT64_ROOT_5LEVEL)
		mmu->root.hpa = __pa(mmu->pml5_root);
	else if (mmu->root_role.level == PT64_ROOT_4LEVEL)
		mmu->root.hpa = __pa(mmu->pml4_root);
	else
		mmu->root.hpa = __pa(mmu->pae_root);

set_root_pgd:
	mmu->root.pgd = root_pgd;
out_unlock:
	write_unlock(&vcpu->kvm->mmu_lock);

	return r;
}

static int mmu_alloc_special_roots(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	bool need_pml5 = mmu->root_role.level > PT64_ROOT_4LEVEL;
	u64 *pml5_root = NULL;
	u64 *pml4_root = NULL;
	u64 *pae_root;

	/*
	 * When shadowing 32-bit or PAE NPT with 64-bit NPT, the PML4 and PDP
	 * tables are allocated and initialized at root creation as there is no
	 * equivalent level in the guest's NPT to shadow.  Allocate the tables
	 * on demand, as running a 32-bit L1 VMM on 64-bit KVM is very rare.
	 */
	if (mmu->root_role.direct ||
	    mmu->cpu_role.base.level >= PT64_ROOT_4LEVEL ||
	    mmu->root_role.level < PT64_ROOT_4LEVEL)
		return 0;

	/*
	 * NPT, the only paging mode that uses this horror, uses a fixed number
	 * of levels for the shadow page tables, e.g. all MMUs are 4-level or
	 * all MMus are 5-level.  Thus, this can safely require that pml5_root
	 * is allocated if the other roots are valid and pml5 is needed, as any
	 * prior MMU would also have required pml5.
	 */
	if (mmu->pae_root && mmu->pml4_root && (!need_pml5 || mmu->pml5_root))
		return 0;

	/*
	 * The special roots should always be allocated in concert.  Yell and
	 * bail if KVM ends up in a state where only one of the roots is valid.
	 */
	if (WARN_ON_ONCE(!tdp_enabled || mmu->pae_root || mmu->pml4_root ||
			 (need_pml5 && mmu->pml5_root)))
		return -EIO;

	/*
	 * Unlike 32-bit NPT, the PDP table doesn't need to be in low mem, and
	 * doesn't need to be decrypted.
	 */
	pae_root = (void *)get_zeroed_page(GFP_KERNEL_ACCOUNT);
	if (!pae_root)
		return -ENOMEM;

#ifdef CONFIG_X86_64
	pml4_root = (void *)get_zeroed_page(GFP_KERNEL_ACCOUNT);
	if (!pml4_root)
		goto err_pml4;

	if (need_pml5) {
		pml5_root = (void *)get_zeroed_page(GFP_KERNEL_ACCOUNT);
		if (!pml5_root)
			goto err_pml5;
	}
#endif

	mmu->pae_root = pae_root;
	mmu->pml4_root = pml4_root;
	mmu->pml5_root = pml5_root;

	return 0;

#ifdef CONFIG_X86_64
err_pml5:
	free_page((unsigned long)pml4_root);
err_pml4:
	free_page((unsigned long)pae_root);
	return -ENOMEM;
#endif
}

static bool is_unsync_root(hpa_t root)
{
	struct kvm_mmu_page *sp;

	if (!VALID_PAGE(root) || kvm_mmu_is_dummy_root(root))
		return false;

	/*
	 * The read barrier orders the CPU's read of SPTE.W during the page table
	 * walk before the reads of sp->unsync/sp->unsync_children here.
	 *
	 * Even if another CPU was marking the SP as unsync-ed simultaneously,
	 * any guest page table changes are not guaranteed to be visible anyway
	 * until this VCPU issues a TLB flush strictly after those changes are
	 * made.  We only need to ensure that the other CPU sets these flags
	 * before any actual changes to the page tables are made.  The comments
	 * in mmu_try_to_unsync_pages() describe what could go wrong if this
	 * requirement isn't satisfied.
	 */
	smp_rmb();
	sp = root_to_sp(root);

	/*
	 * PAE roots (somewhat arbitrarily) aren't backed by shadow pages, the
	 * PDPTEs for a given PAE root need to be synchronized individually.
	 */
	if (WARN_ON_ONCE(!sp))
		return false;

	if (sp->unsync || sp->unsync_children)
		return true;

	return false;
}

void kvm_mmu_sync_roots(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_mmu_page *sp;

	if (vcpu->arch.mmu->root_role.direct)
		return;

	if (!VALID_PAGE(vcpu->arch.mmu->root.hpa))
		return;

	vcpu_clear_mmio_info(vcpu, MMIO_GVA_ANY);

	if (vcpu->arch.mmu->cpu_role.base.level >= PT64_ROOT_4LEVEL) {
		hpa_t root = vcpu->arch.mmu->root.hpa;

		if (!is_unsync_root(root))
			return;

		sp = root_to_sp(root);

		write_lock(&vcpu->kvm->mmu_lock);
		mmu_sync_children(vcpu, sp, true);
		write_unlock(&vcpu->kvm->mmu_lock);
		return;
	}

	write_lock(&vcpu->kvm->mmu_lock);

	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu->pae_root[i];

		if (IS_VALID_PAE_ROOT(root)) {
			sp = spte_to_child_sp(root);
			mmu_sync_children(vcpu, sp, true);
		}
	}

	write_unlock(&vcpu->kvm->mmu_lock);
}

void kvm_mmu_sync_prev_roots(struct kvm_vcpu *vcpu)
{
	unsigned long roots_to_free = 0;
	int i;

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
		if (is_unsync_root(vcpu->arch.mmu->prev_roots[i].hpa))
			roots_to_free |= KVM_MMU_ROOT_PREVIOUS(i);

	/* sync prev_roots by simply freeing them */
	kvm_mmu_free_roots(vcpu->kvm, vcpu->arch.mmu, roots_to_free);
}

static gpa_t nonpaging_gva_to_gpa(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
				  gpa_t vaddr, u64 access,
				  struct x86_exception *exception)
{
	if (exception)
		exception->error_code = 0;
	return kvm_translate_gpa(vcpu, mmu, vaddr, access, exception);
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

/*
 * Return the level of the lowest level SPTE added to sptes.
 * That SPTE may be non-present.
 *
 * Must be called between walk_shadow_page_lockless_{begin,end}.
 */
static int get_walk(struct kvm_vcpu *vcpu, u64 addr, u64 *sptes, int *root_level)
{
	struct kvm_shadow_walk_iterator iterator;
	int leaf = -1;
	u64 spte;

	for (shadow_walk_init(&iterator, vcpu, addr),
	     *root_level = iterator.level;
	     shadow_walk_okay(&iterator);
	     __shadow_walk_next(&iterator, spte)) {
		leaf = iterator.level;
		spte = mmu_spte_get_lockless(iterator.sptep);

		sptes[leaf] = spte;
	}

	return leaf;
}

/* return true if reserved bit(s) are detected on a valid, non-MMIO SPTE. */
static bool get_mmio_spte(struct kvm_vcpu *vcpu, u64 addr, u64 *sptep)
{
	u64 sptes[PT64_ROOT_MAX_LEVEL + 1];
	struct rsvd_bits_validate *rsvd_check;
	int root, leaf, level;
	bool reserved = false;

	walk_shadow_page_lockless_begin(vcpu);

	if (is_tdp_mmu_active(vcpu))
		leaf = kvm_tdp_mmu_get_walk(vcpu, addr, sptes, &root);
	else
		leaf = get_walk(vcpu, addr, sptes, &root);

	walk_shadow_page_lockless_end(vcpu);

	if (unlikely(leaf < 0)) {
		*sptep = 0ull;
		return reserved;
	}

	*sptep = sptes[leaf];

	/*
	 * Skip reserved bits checks on the terminal leaf if it's not a valid
	 * SPTE.  Note, this also (intentionally) skips MMIO SPTEs, which, by
	 * design, always have reserved bits set.  The purpose of the checks is
	 * to detect reserved bits on non-MMIO SPTEs. i.e. buggy SPTEs.
	 */
	if (!is_shadow_present_pte(sptes[leaf]))
		leaf++;

	rsvd_check = &vcpu->arch.mmu->shadow_zero_check;

	for (level = root; level >= leaf; level--)
		reserved |= is_rsvd_spte(rsvd_check, sptes[level], level);

	if (reserved) {
		pr_err("%s: reserved bits set on MMU-present spte, addr 0x%llx, hierarchy:\n",
		       __func__, addr);
		for (level = root; level >= leaf; level--)
			pr_err("------ spte = 0x%llx level = %d, rsvd bits = 0x%llx",
			       sptes[level], level,
			       get_rsvd_bits(rsvd_check, sptes[level], level));
	}

	return reserved;
}

static int handle_mmio_page_fault(struct kvm_vcpu *vcpu, u64 addr, bool direct)
{
	u64 spte;
	bool reserved;

	if (mmio_info_in_cache(vcpu, addr, direct))
		return RET_PF_EMULATE;

	reserved = get_mmio_spte(vcpu, addr, &spte);
	if (WARN_ON_ONCE(reserved))
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
					 struct kvm_page_fault *fault)
{
	if (unlikely(fault->rsvd))
		return false;

	if (!fault->present || !fault->write)
		return false;

	/*
	 * guest is writing the page which is write tracked which can
	 * not be fixed by page fault handler.
	 */
	if (kvm_gfn_is_write_tracked(vcpu->kvm, fault->slot, fault->gfn))
		return true;

	return false;
}

static void shadow_page_table_clear_flood(struct kvm_vcpu *vcpu, gva_t addr)
{
	struct kvm_shadow_walk_iterator iterator;
	u64 spte;

	walk_shadow_page_lockless_begin(vcpu);
	for_each_shadow_entry_lockless(vcpu, addr, iterator, spte)
		clear_sp_write_flooding_count(iterator.sptep);
	walk_shadow_page_lockless_end(vcpu);
}

static u32 alloc_apf_token(struct kvm_vcpu *vcpu)
{
	/* make sure the token value is not 0 */
	u32 id = vcpu->arch.apf.id;

	if (id << 12 == 0)
		vcpu->arch.apf.id = 1;

	return (vcpu->arch.apf.id++ << 12) | vcpu->vcpu_id;
}

static bool kvm_arch_setup_async_pf(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
				    gfn_t gfn)
{
	struct kvm_arch_async_pf arch;

	arch.token = alloc_apf_token(vcpu);
	arch.gfn = gfn;
	arch.direct_map = vcpu->arch.mmu->root_role.direct;
	arch.cr3 = kvm_mmu_get_guest_pgd(vcpu, vcpu->arch.mmu);

	return kvm_setup_async_pf(vcpu, cr2_or_gpa,
				  kvm_vcpu_gfn_to_hva(vcpu, gfn), &arch);
}

void kvm_arch_async_page_ready(struct kvm_vcpu *vcpu, struct kvm_async_pf *work)
{
	int r;

	if ((vcpu->arch.mmu->root_role.direct != work->arch.direct_map) ||
	      work->wakeup_all)
		return;

	r = kvm_mmu_reload(vcpu);
	if (unlikely(r))
		return;

	if (!vcpu->arch.mmu->root_role.direct &&
	      work->arch.cr3 != kvm_mmu_get_guest_pgd(vcpu, vcpu->arch.mmu))
		return;

	kvm_mmu_do_page_fault(vcpu, work->cr2_or_gpa, 0, true, NULL);
}

static inline u8 kvm_max_level_for_order(int order)
{
	BUILD_BUG_ON(KVM_MAX_HUGEPAGE_LEVEL > PG_LEVEL_1G);

	KVM_MMU_WARN_ON(order != KVM_HPAGE_GFN_SHIFT(PG_LEVEL_1G) &&
			order != KVM_HPAGE_GFN_SHIFT(PG_LEVEL_2M) &&
			order != KVM_HPAGE_GFN_SHIFT(PG_LEVEL_4K));

	if (order >= KVM_HPAGE_GFN_SHIFT(PG_LEVEL_1G))
		return PG_LEVEL_1G;

	if (order >= KVM_HPAGE_GFN_SHIFT(PG_LEVEL_2M))
		return PG_LEVEL_2M;

	return PG_LEVEL_4K;
}

static void kvm_mmu_prepare_memory_fault_exit(struct kvm_vcpu *vcpu,
					      struct kvm_page_fault *fault)
{
	kvm_prepare_memory_fault_exit(vcpu, fault->gfn << PAGE_SHIFT,
				      PAGE_SIZE, fault->write, fault->exec,
				      fault->is_private);
}

static int kvm_faultin_pfn_private(struct kvm_vcpu *vcpu,
				   struct kvm_page_fault *fault)
{
	int max_order, r;

	if (!kvm_slot_can_be_private(fault->slot)) {
		kvm_mmu_prepare_memory_fault_exit(vcpu, fault);
		return -EFAULT;
	}

	r = kvm_gmem_get_pfn(vcpu->kvm, fault->slot, fault->gfn, &fault->pfn,
			     &max_order);
	if (r) {
		kvm_mmu_prepare_memory_fault_exit(vcpu, fault);
		return r;
	}

	fault->max_level = min(kvm_max_level_for_order(max_order),
			       fault->max_level);
	fault->map_writable = !(fault->slot->flags & KVM_MEM_READONLY);

	return RET_PF_CONTINUE;
}

static int __kvm_faultin_pfn(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault)
{
	struct kvm_memory_slot *slot = fault->slot;
	bool async;

	/*
	 * Retry the page fault if the gfn hit a memslot that is being deleted
	 * or moved.  This ensures any existing SPTEs for the old memslot will
	 * be zapped before KVM inserts a new MMIO SPTE for the gfn.
	 */
	if (slot && (slot->flags & KVM_MEMSLOT_INVALID))
		return RET_PF_RETRY;

	if (!kvm_is_visible_memslot(slot)) {
		/* Don't expose private memslots to L2. */
		if (is_guest_mode(vcpu)) {
			fault->slot = NULL;
			fault->pfn = KVM_PFN_NOSLOT;
			fault->map_writable = false;
			return RET_PF_CONTINUE;
		}
		/*
		 * If the APIC access page exists but is disabled, go directly
		 * to emulation without caching the MMIO access or creating a
		 * MMIO SPTE.  That way the cache doesn't need to be purged
		 * when the AVIC is re-enabled.
		 */
		if (slot && slot->id == APIC_ACCESS_PAGE_PRIVATE_MEMSLOT &&
		    !kvm_apicv_activated(vcpu->kvm))
			return RET_PF_EMULATE;
	}

	if (fault->is_private != kvm_mem_is_private(vcpu->kvm, fault->gfn)) {
		kvm_mmu_prepare_memory_fault_exit(vcpu, fault);
		return -EFAULT;
	}

	if (fault->is_private)
		return kvm_faultin_pfn_private(vcpu, fault);

	async = false;
	fault->pfn = __gfn_to_pfn_memslot(slot, fault->gfn, false, false, &async,
					  fault->write, &fault->map_writable,
					  &fault->hva);
	if (!async)
		return RET_PF_CONTINUE; /* *pfn has correct page already */

	if (!fault->prefetch && kvm_can_do_async_pf(vcpu)) {
		trace_kvm_try_async_get_page(fault->addr, fault->gfn);
		if (kvm_find_async_pf_gfn(vcpu, fault->gfn)) {
			trace_kvm_async_pf_repeated_fault(fault->addr, fault->gfn);
			kvm_make_request(KVM_REQ_APF_HALT, vcpu);
			return RET_PF_RETRY;
		} else if (kvm_arch_setup_async_pf(vcpu, fault->addr, fault->gfn)) {
			return RET_PF_RETRY;
		}
	}

	/*
	 * Allow gup to bail on pending non-fatal signals when it's also allowed
	 * to wait for IO.  Note, gup always bails if it is unable to quickly
	 * get a page and a fatal signal, i.e. SIGKILL, is pending.
	 */
	fault->pfn = __gfn_to_pfn_memslot(slot, fault->gfn, false, true, NULL,
					  fault->write, &fault->map_writable,
					  &fault->hva);
	return RET_PF_CONTINUE;
}

static int kvm_faultin_pfn(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault,
			   unsigned int access)
{
	int ret;

	fault->mmu_seq = vcpu->kvm->mmu_invalidate_seq;
	smp_rmb();

	/*
	 * Check for a relevant mmu_notifier invalidation event before getting
	 * the pfn from the primary MMU, and before acquiring mmu_lock.
	 *
	 * For mmu_lock, if there is an in-progress invalidation and the kernel
	 * allows preemption, the invalidation task may drop mmu_lock and yield
	 * in response to mmu_lock being contended, which is *very* counter-
	 * productive as this vCPU can't actually make forward progress until
	 * the invalidation completes.
	 *
	 * Retrying now can also avoid unnessary lock contention in the primary
	 * MMU, as the primary MMU doesn't necessarily hold a single lock for
	 * the duration of the invalidation, i.e. faulting in a conflicting pfn
	 * can cause the invalidation to take longer by holding locks that are
	 * needed to complete the invalidation.
	 *
	 * Do the pre-check even for non-preemtible kernels, i.e. even if KVM
	 * will never yield mmu_lock in response to contention, as this vCPU is
	 * *guaranteed* to need to retry, i.e. waiting until mmu_lock is held
	 * to detect retry guarantees the worst case latency for the vCPU.
	 */
	if (fault->slot &&
	    mmu_invalidate_retry_gfn_unsafe(vcpu->kvm, fault->mmu_seq, fault->gfn))
		return RET_PF_RETRY;

	ret = __kvm_faultin_pfn(vcpu, fault);
	if (ret != RET_PF_CONTINUE)
		return ret;

	if (unlikely(is_error_pfn(fault->pfn)))
		return kvm_handle_error_pfn(vcpu, fault);

	if (unlikely(!fault->slot))
		return kvm_handle_noslot_fault(vcpu, fault, access);

	/*
	 * Check again for a relevant mmu_notifier invalidation event purely to
	 * avoid contending mmu_lock.  Most invalidations will be detected by
	 * the previous check, but checking is extremely cheap relative to the
	 * overall cost of failing to detect the invalidation until after
	 * mmu_lock is acquired.
	 */
	if (mmu_invalidate_retry_gfn_unsafe(vcpu->kvm, fault->mmu_seq, fault->gfn)) {
		kvm_release_pfn_clean(fault->pfn);
		return RET_PF_RETRY;
	}

	return RET_PF_CONTINUE;
}

/*
 * Returns true if the page fault is stale and needs to be retried, i.e. if the
 * root was invalidated by a memslot update or a relevant mmu_notifier fired.
 */
static bool is_page_fault_stale(struct kvm_vcpu *vcpu,
				struct kvm_page_fault *fault)
{
	struct kvm_mmu_page *sp = root_to_sp(vcpu->arch.mmu->root.hpa);

	/* Special roots, e.g. pae_root, are not backed by shadow pages. */
	if (sp && is_obsolete_sp(vcpu->kvm, sp))
		return true;

	/*
	 * Roots without an associated shadow page are considered invalid if
	 * there is a pending request to free obsolete roots.  The request is
	 * only a hint that the current root _may_ be obsolete and needs to be
	 * reloaded, e.g. if the guest frees a PGD that KVM is tracking as a
	 * previous root, then __kvm_mmu_prepare_zap_page() signals all vCPUs
	 * to reload even if no vCPU is actively using the root.
	 */
	if (!sp && kvm_test_request(KVM_REQ_MMU_FREE_OBSOLETE_ROOTS, vcpu))
		return true;

	/*
	 * Check for a relevant mmu_notifier invalidation event one last time
	 * now that mmu_lock is held, as the "unsafe" checks performed without
	 * holding mmu_lock can get false negatives.
	 */
	return fault->slot &&
	       mmu_invalidate_retry_gfn(vcpu->kvm, fault->mmu_seq, fault->gfn);
}

static int direct_page_fault(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault)
{
	int r;

	/* Dummy roots are used only for shadowing bad guest roots. */
	if (WARN_ON_ONCE(kvm_mmu_is_dummy_root(vcpu->arch.mmu->root.hpa)))
		return RET_PF_RETRY;

	if (page_fault_handle_page_track(vcpu, fault))
		return RET_PF_EMULATE;

	r = fast_page_fault(vcpu, fault);
	if (r != RET_PF_INVALID)
		return r;

	r = mmu_topup_memory_caches(vcpu, false);
	if (r)
		return r;

	r = kvm_faultin_pfn(vcpu, fault, ACC_ALL);
	if (r != RET_PF_CONTINUE)
		return r;

	r = RET_PF_RETRY;
	write_lock(&vcpu->kvm->mmu_lock);

	if (is_page_fault_stale(vcpu, fault))
		goto out_unlock;

	r = make_mmu_pages_available(vcpu);
	if (r)
		goto out_unlock;

	r = direct_map(vcpu, fault);

out_unlock:
	write_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(fault->pfn);
	return r;
}

static int nonpaging_page_fault(struct kvm_vcpu *vcpu,
				struct kvm_page_fault *fault)
{
	/* This path builds a PAE pagetable, we can map 2mb pages at maximum. */
	fault->max_level = PG_LEVEL_2M;
	return direct_page_fault(vcpu, fault);
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
		trace_kvm_page_fault(vcpu, fault_address, error_code);

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

#ifdef CONFIG_X86_64
static int kvm_tdp_mmu_page_fault(struct kvm_vcpu *vcpu,
				  struct kvm_page_fault *fault)
{
	int r;

	if (page_fault_handle_page_track(vcpu, fault))
		return RET_PF_EMULATE;

	r = fast_page_fault(vcpu, fault);
	if (r != RET_PF_INVALID)
		return r;

	r = mmu_topup_memory_caches(vcpu, false);
	if (r)
		return r;

	r = kvm_faultin_pfn(vcpu, fault, ACC_ALL);
	if (r != RET_PF_CONTINUE)
		return r;

	r = RET_PF_RETRY;
	read_lock(&vcpu->kvm->mmu_lock);

	if (is_page_fault_stale(vcpu, fault))
		goto out_unlock;

	r = kvm_tdp_mmu_map(vcpu, fault);

out_unlock:
	read_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(fault->pfn);
	return r;
}
#endif

bool __kvm_mmu_honors_guest_mtrrs(bool vm_has_noncoherent_dma)
{
	/*
	 * If host MTRRs are ignored (shadow_memtype_mask is non-zero), and the
	 * VM has non-coherent DMA (DMA doesn't snoop CPU caches), KVM's ABI is
	 * to honor the memtype from the guest's MTRRs so that guest accesses
	 * to memory that is DMA'd aren't cached against the guest's wishes.
	 *
	 * Note, KVM may still ultimately ignore guest MTRRs for certain PFNs,
	 * e.g. KVM will force UC memtype for host MMIO.
	 */
	return vm_has_noncoherent_dma && shadow_memtype_mask;
}

int kvm_tdp_page_fault(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault)
{
	/*
	 * If the guest's MTRRs may be used to compute the "real" memtype,
	 * restrict the mapping level to ensure KVM uses a consistent memtype
	 * across the entire mapping.
	 */
	if (kvm_mmu_honors_guest_mtrrs(vcpu->kvm)) {
		for ( ; fault->max_level > PG_LEVEL_4K; --fault->max_level) {
			int page_num = KVM_PAGES_PER_HPAGE(fault->max_level);
			gfn_t base = gfn_round_for_level(fault->gfn,
							 fault->max_level);

			if (kvm_mtrr_check_gfn_range_consistency(vcpu, base, page_num))
				break;
		}
	}

#ifdef CONFIG_X86_64
	if (tdp_mmu_enabled)
		return kvm_tdp_mmu_page_fault(vcpu, fault);
#endif

	return direct_page_fault(vcpu, fault);
}

static void nonpaging_init_context(struct kvm_mmu *context)
{
	context->page_fault = nonpaging_page_fault;
	context->gva_to_gpa = nonpaging_gva_to_gpa;
	context->sync_spte = NULL;
}

static inline bool is_root_usable(struct kvm_mmu_root_info *root, gpa_t pgd,
				  union kvm_mmu_page_role role)
{
	struct kvm_mmu_page *sp;

	if (!VALID_PAGE(root->hpa))
		return false;

	if (!role.direct && pgd != root->pgd)
		return false;

	sp = root_to_sp(root->hpa);
	if (WARN_ON_ONCE(!sp))
		return false;

	return role.word == sp->role.word;
}

/*
 * Find out if a previously cached root matching the new pgd/role is available,
 * and insert the current root as the MRU in the cache.
 * If a matching root is found, it is assigned to kvm_mmu->root and
 * true is returned.
 * If no match is found, kvm_mmu->root is left invalid, the LRU root is
 * evicted to make room for the current root, and false is returned.
 */
static bool cached_root_find_and_keep_current(struct kvm *kvm, struct kvm_mmu *mmu,
					      gpa_t new_pgd,
					      union kvm_mmu_page_role new_role)
{
	uint i;

	if (is_root_usable(&mmu->root, new_pgd, new_role))
		return true;

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++) {
		/*
		 * The swaps end up rotating the cache like this:
		 *   C   0 1 2 3   (on entry to the function)
		 *   0   C 1 2 3
		 *   1   C 0 2 3
		 *   2   C 0 1 3
		 *   3   C 0 1 2   (on exit from the loop)
		 */
		swap(mmu->root, mmu->prev_roots[i]);
		if (is_root_usable(&mmu->root, new_pgd, new_role))
			return true;
	}

	kvm_mmu_free_roots(kvm, mmu, KVM_MMU_ROOT_CURRENT);
	return false;
}

/*
 * Find out if a previously cached root matching the new pgd/role is available.
 * On entry, mmu->root is invalid.
 * If a matching root is found, it is assigned to kvm_mmu->root, the LRU entry
 * of the cache becomes invalid, and true is returned.
 * If no match is found, kvm_mmu->root is left invalid and false is returned.
 */
static bool cached_root_find_without_current(struct kvm *kvm, struct kvm_mmu *mmu,
					     gpa_t new_pgd,
					     union kvm_mmu_page_role new_role)
{
	uint i;

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
		if (is_root_usable(&mmu->prev_roots[i], new_pgd, new_role))
			goto hit;

	return false;

hit:
	swap(mmu->root, mmu->prev_roots[i]);
	/* Bubble up the remaining roots.  */
	for (; i < KVM_MMU_NUM_PREV_ROOTS - 1; i++)
		mmu->prev_roots[i] = mmu->prev_roots[i + 1];
	mmu->prev_roots[i].hpa = INVALID_PAGE;
	return true;
}

static bool fast_pgd_switch(struct kvm *kvm, struct kvm_mmu *mmu,
			    gpa_t new_pgd, union kvm_mmu_page_role new_role)
{
	/*
	 * Limit reuse to 64-bit hosts+VMs without "special" roots in order to
	 * avoid having to deal with PDPTEs and other complexities.
	 */
	if (VALID_PAGE(mmu->root.hpa) && !root_to_sp(mmu->root.hpa))
		kvm_mmu_free_roots(kvm, mmu, KVM_MMU_ROOT_CURRENT);

	if (VALID_PAGE(mmu->root.hpa))
		return cached_root_find_and_keep_current(kvm, mmu, new_pgd, new_role);
	else
		return cached_root_find_without_current(kvm, mmu, new_pgd, new_role);
}

void kvm_mmu_new_pgd(struct kvm_vcpu *vcpu, gpa_t new_pgd)
{
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	union kvm_mmu_page_role new_role = mmu->root_role;

	/*
	 * Return immediately if no usable root was found, kvm_mmu_reload()
	 * will establish a valid root prior to the next VM-Enter.
	 */
	if (!fast_pgd_switch(vcpu->kvm, mmu, new_pgd, new_role))
		return;

	/*
	 * It's possible that the cached previous root page is obsolete because
	 * of a change in the MMU generation number. However, changing the
	 * generation number is accompanied by KVM_REQ_MMU_FREE_OBSOLETE_ROOTS,
	 * which will free the root set here and allocate a new one.
	 */
	kvm_make_request(KVM_REQ_LOAD_MMU_PGD, vcpu);

	if (force_flush_and_sync_on_reuse) {
		kvm_make_request(KVM_REQ_MMU_SYNC, vcpu);
		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
	}

	/*
	 * The last MMIO access's GVA and GPA are cached in the VCPU. When
	 * switching to a new CR3, that GVA->GPA mapping may no longer be
	 * valid. So clear any cached MMIO info even when we don't need to sync
	 * the shadow page tables.
	 */
	vcpu_clear_mmio_info(vcpu, MMIO_GVA_ANY);

	/*
	 * If this is a direct root page, it doesn't have a write flooding
	 * count. Otherwise, clear the write flooding count.
	 */
	if (!new_role.direct) {
		struct kvm_mmu_page *sp = root_to_sp(vcpu->arch.mmu->root.hpa);

		if (!WARN_ON_ONCE(!sp))
			__clear_sp_write_flooding_count(sp);
	}
}
EXPORT_SYMBOL_GPL(kvm_mmu_new_pgd);

static bool sync_mmio_spte(struct kvm_vcpu *vcpu, u64 *sptep, gfn_t gfn,
			   unsigned int access)
{
	if (unlikely(is_mmio_spte(*sptep))) {
		if (gfn != get_mmio_spte_gfn(*sptep)) {
			mmu_spte_clear_no_track(sptep);
			return true;
		}

		mark_mmio_spte(vcpu, sptep, gfn, access);
		return true;
	}

	return false;
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

static void __reset_rsvds_bits_mask(struct rsvd_bits_validate *rsvd_check,
				    u64 pa_bits_rsvd, int level, bool nx,
				    bool gbpages, bool pse, bool amd)
{
	u64 gbpages_bit_rsvd = 0;
	u64 nonleaf_bit8_rsvd = 0;
	u64 high_bits_rsvd;

	rsvd_check->bad_mt_xwr = 0;

	if (!gbpages)
		gbpages_bit_rsvd = rsvd_bits(7, 7);

	if (level == PT32E_ROOT_LEVEL)
		high_bits_rsvd = pa_bits_rsvd & rsvd_bits(0, 62);
	else
		high_bits_rsvd = pa_bits_rsvd & rsvd_bits(0, 51);

	/* Note, NX doesn't exist in PDPTEs, this is handled below. */
	if (!nx)
		high_bits_rsvd |= rsvd_bits(63, 63);

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
		rsvd_check->rsvd_bits_mask[0][2] = rsvd_bits(63, 63) |
						   high_bits_rsvd |
						   rsvd_bits(5, 8) |
						   rsvd_bits(1, 2);	/* PDPTE */
		rsvd_check->rsvd_bits_mask[0][1] = high_bits_rsvd;	/* PDE */
		rsvd_check->rsvd_bits_mask[0][0] = high_bits_rsvd;	/* PTE */
		rsvd_check->rsvd_bits_mask[1][1] = high_bits_rsvd |
						   rsvd_bits(13, 20);	/* large page */
		rsvd_check->rsvd_bits_mask[1][0] =
			rsvd_check->rsvd_bits_mask[0][0];
		break;
	case PT64_ROOT_5LEVEL:
		rsvd_check->rsvd_bits_mask[0][4] = high_bits_rsvd |
						   nonleaf_bit8_rsvd |
						   rsvd_bits(7, 7);
		rsvd_check->rsvd_bits_mask[1][4] =
			rsvd_check->rsvd_bits_mask[0][4];
		fallthrough;
	case PT64_ROOT_4LEVEL:
		rsvd_check->rsvd_bits_mask[0][3] = high_bits_rsvd |
						   nonleaf_bit8_rsvd |
						   rsvd_bits(7, 7);
		rsvd_check->rsvd_bits_mask[0][2] = high_bits_rsvd |
						   gbpages_bit_rsvd;
		rsvd_check->rsvd_bits_mask[0][1] = high_bits_rsvd;
		rsvd_check->rsvd_bits_mask[0][0] = high_bits_rsvd;
		rsvd_check->rsvd_bits_mask[1][3] =
			rsvd_check->rsvd_bits_mask[0][3];
		rsvd_check->rsvd_bits_mask[1][2] = high_bits_rsvd |
						   gbpages_bit_rsvd |
						   rsvd_bits(13, 29);
		rsvd_check->rsvd_bits_mask[1][1] = high_bits_rsvd |
						   rsvd_bits(13, 20); /* large page */
		rsvd_check->rsvd_bits_mask[1][0] =
			rsvd_check->rsvd_bits_mask[0][0];
		break;
	}
}

static void reset_guest_rsvds_bits_mask(struct kvm_vcpu *vcpu,
					struct kvm_mmu *context)
{
	__reset_rsvds_bits_mask(&context->guest_rsvd_check,
				vcpu->arch.reserved_gpa_bits,
				context->cpu_role.base.level, is_efer_nx(context),
				guest_can_use(vcpu, X86_FEATURE_GBPAGES),
				is_cr4_pse(context),
				guest_cpuid_is_amd_or_hygon(vcpu));
}

static void __reset_rsvds_bits_mask_ept(struct rsvd_bits_validate *rsvd_check,
					u64 pa_bits_rsvd, bool execonly,
					int huge_page_level)
{
	u64 high_bits_rsvd = pa_bits_rsvd & rsvd_bits(0, 51);
	u64 large_1g_rsvd = 0, large_2m_rsvd = 0;
	u64 bad_mt_xwr;

	if (huge_page_level < PG_LEVEL_1G)
		large_1g_rsvd = rsvd_bits(7, 7);
	if (huge_page_level < PG_LEVEL_2M)
		large_2m_rsvd = rsvd_bits(7, 7);

	rsvd_check->rsvd_bits_mask[0][4] = high_bits_rsvd | rsvd_bits(3, 7);
	rsvd_check->rsvd_bits_mask[0][3] = high_bits_rsvd | rsvd_bits(3, 7);
	rsvd_check->rsvd_bits_mask[0][2] = high_bits_rsvd | rsvd_bits(3, 6) | large_1g_rsvd;
	rsvd_check->rsvd_bits_mask[0][1] = high_bits_rsvd | rsvd_bits(3, 6) | large_2m_rsvd;
	rsvd_check->rsvd_bits_mask[0][0] = high_bits_rsvd;

	/* large page */
	rsvd_check->rsvd_bits_mask[1][4] = rsvd_check->rsvd_bits_mask[0][4];
	rsvd_check->rsvd_bits_mask[1][3] = rsvd_check->rsvd_bits_mask[0][3];
	rsvd_check->rsvd_bits_mask[1][2] = high_bits_rsvd | rsvd_bits(12, 29) | large_1g_rsvd;
	rsvd_check->rsvd_bits_mask[1][1] = high_bits_rsvd | rsvd_bits(12, 20) | large_2m_rsvd;
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
		struct kvm_mmu *context, bool execonly, int huge_page_level)
{
	__reset_rsvds_bits_mask_ept(&context->guest_rsvd_check,
				    vcpu->arch.reserved_gpa_bits, execonly,
				    huge_page_level);
}

static inline u64 reserved_hpa_bits(void)
{
	return rsvd_bits(shadow_phys_bits, 63);
}

/*
 * the page table on host is the shadow page table for the page
 * table in guest or amd nested guest, its mmu features completely
 * follow the features in guest.
 */
static void reset_shadow_zero_bits_mask(struct kvm_vcpu *vcpu,
					struct kvm_mmu *context)
{
	/* @amd adds a check on bit of SPTEs, which KVM shouldn't use anyways. */
	bool is_amd = true;
	/* KVM doesn't use 2-level page tables for the shadow MMU. */
	bool is_pse = false;
	struct rsvd_bits_validate *shadow_zero_check;
	int i;

	WARN_ON_ONCE(context->root_role.level < PT32E_ROOT_LEVEL);

	shadow_zero_check = &context->shadow_zero_check;
	__reset_rsvds_bits_mask(shadow_zero_check, reserved_hpa_bits(),
				context->root_role.level,
				context->root_role.efer_nx,
				guest_can_use(vcpu, X86_FEATURE_GBPAGES),
				is_pse, is_amd);

	if (!shadow_me_mask)
		return;

	for (i = context->root_role.level; --i >= 0;) {
		/*
		 * So far shadow_me_value is a constant during KVM's life
		 * time.  Bits in shadow_me_value are allowed to be set.
		 * Bits in shadow_me_mask but not in shadow_me_value are
		 * not allowed to be set.
		 */
		shadow_zero_check->rsvd_bits_mask[0][i] |= shadow_me_mask;
		shadow_zero_check->rsvd_bits_mask[1][i] |= shadow_me_mask;
		shadow_zero_check->rsvd_bits_mask[0][i] &= ~shadow_me_value;
		shadow_zero_check->rsvd_bits_mask[1][i] &= ~shadow_me_value;
	}

}

static inline bool boot_cpu_is_amd(void)
{
	WARN_ON_ONCE(!tdp_enabled);
	return shadow_x_mask == 0;
}

/*
 * the direct page table on host, use as much mmu features as
 * possible, however, kvm currently does not do execution-protection.
 */
static void reset_tdp_shadow_zero_bits_mask(struct kvm_mmu *context)
{
	struct rsvd_bits_validate *shadow_zero_check;
	int i;

	shadow_zero_check = &context->shadow_zero_check;

	if (boot_cpu_is_amd())
		__reset_rsvds_bits_mask(shadow_zero_check, reserved_hpa_bits(),
					context->root_role.level, true,
					boot_cpu_has(X86_FEATURE_GBPAGES),
					false, true);
	else
		__reset_rsvds_bits_mask_ept(shadow_zero_check,
					    reserved_hpa_bits(), false,
					    max_huge_page_level);

	if (!shadow_me_mask)
		return;

	for (i = context->root_role.level; --i >= 0;) {
		shadow_zero_check->rsvd_bits_mask[0][i] &= ~shadow_me_mask;
		shadow_zero_check->rsvd_bits_mask[1][i] &= ~shadow_me_mask;
	}
}

/*
 * as the comments in reset_shadow_zero_bits_mask() except it
 * is the shadow page table for intel nested guest.
 */
static void
reset_ept_shadow_zero_bits_mask(struct kvm_mmu *context, bool execonly)
{
	__reset_rsvds_bits_mask_ept(&context->shadow_zero_check,
				    reserved_hpa_bits(), execonly,
				    max_huge_page_level);
}

#define BYTE_MASK(access) \
	((1 & (access) ? 2 : 0) | \
	 (2 & (access) ? 4 : 0) | \
	 (3 & (access) ? 8 : 0) | \
	 (4 & (access) ? 16 : 0) | \
	 (5 & (access) ? 32 : 0) | \
	 (6 & (access) ? 64 : 0) | \
	 (7 & (access) ? 128 : 0))


static void update_permission_bitmask(struct kvm_mmu *mmu, bool ept)
{
	unsigned byte;

	const u8 x = BYTE_MASK(ACC_EXEC_MASK);
	const u8 w = BYTE_MASK(ACC_WRITE_MASK);
	const u8 u = BYTE_MASK(ACC_USER_MASK);

	bool cr4_smep = is_cr4_smep(mmu);
	bool cr4_smap = is_cr4_smap(mmu);
	bool cr0_wp = is_cr0_wp(mmu);
	bool efer_nx = is_efer_nx(mmu);

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
			if (!efer_nx)
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
			 *   - The access is supervisor mode
			 *   - If implicit supervisor access or X86_EFLAGS_AC is clear
			 *
			 * Here, we cover the first four conditions.
			 * The fifth is computed dynamically in permission_fault();
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
static void update_pkru_bitmask(struct kvm_mmu *mmu)
{
	unsigned bit;
	bool wp;

	mmu->pkru_mask = 0;

	if (!is_cr4_pke(mmu))
		return;

	wp = is_cr0_wp(mmu);

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

static void reset_guest_paging_metadata(struct kvm_vcpu *vcpu,
					struct kvm_mmu *mmu)
{
	if (!is_cr0_pg(mmu))
		return;

	reset_guest_rsvds_bits_mask(vcpu, mmu);
	update_permission_bitmask(mmu, false);
	update_pkru_bitmask(mmu);
}

static void paging64_init_context(struct kvm_mmu *context)
{
	context->page_fault = paging64_page_fault;
	context->gva_to_gpa = paging64_gva_to_gpa;
	context->sync_spte = paging64_sync_spte;
}

static void paging32_init_context(struct kvm_mmu *context)
{
	context->page_fault = paging32_page_fault;
	context->gva_to_gpa = paging32_gva_to_gpa;
	context->sync_spte = paging32_sync_spte;
}

static union kvm_cpu_role kvm_calc_cpu_role(struct kvm_vcpu *vcpu,
					    const struct kvm_mmu_role_regs *regs)
{
	union kvm_cpu_role role = {0};

	role.base.access = ACC_ALL;
	role.base.smm = is_smm(vcpu);
	role.base.guest_mode = is_guest_mode(vcpu);
	role.ext.valid = 1;

	if (!____is_cr0_pg(regs)) {
		role.base.direct = 1;
		return role;
	}

	role.base.efer_nx = ____is_efer_nx(regs);
	role.base.cr0_wp = ____is_cr0_wp(regs);
	role.base.smep_andnot_wp = ____is_cr4_smep(regs) && !____is_cr0_wp(regs);
	role.base.smap_andnot_wp = ____is_cr4_smap(regs) && !____is_cr0_wp(regs);
	role.base.has_4_byte_gpte = !____is_cr4_pae(regs);

	if (____is_efer_lma(regs))
		role.base.level = ____is_cr4_la57(regs) ? PT64_ROOT_5LEVEL
							: PT64_ROOT_4LEVEL;
	else if (____is_cr4_pae(regs))
		role.base.level = PT32E_ROOT_LEVEL;
	else
		role.base.level = PT32_ROOT_LEVEL;

	role.ext.cr4_smep = ____is_cr4_smep(regs);
	role.ext.cr4_smap = ____is_cr4_smap(regs);
	role.ext.cr4_pse = ____is_cr4_pse(regs);

	/* PKEY and LA57 are active iff long mode is active. */
	role.ext.cr4_pke = ____is_efer_lma(regs) && ____is_cr4_pke(regs);
	role.ext.cr4_la57 = ____is_efer_lma(regs) && ____is_cr4_la57(regs);
	role.ext.efer_lma = ____is_efer_lma(regs);
	return role;
}

void __kvm_mmu_refresh_passthrough_bits(struct kvm_vcpu *vcpu,
					struct kvm_mmu *mmu)
{
	const bool cr0_wp = kvm_is_cr0_bit_set(vcpu, X86_CR0_WP);

	BUILD_BUG_ON((KVM_MMU_CR0_ROLE_BITS & KVM_POSSIBLE_CR0_GUEST_BITS) != X86_CR0_WP);
	BUILD_BUG_ON((KVM_MMU_CR4_ROLE_BITS & KVM_POSSIBLE_CR4_GUEST_BITS));

	if (is_cr0_wp(mmu) == cr0_wp)
		return;

	mmu->cpu_role.base.cr0_wp = cr0_wp;
	reset_guest_paging_metadata(vcpu, mmu);
}

static inline int kvm_mmu_get_tdp_level(struct kvm_vcpu *vcpu)
{
	/* tdp_root_level is architecture forced level, use it if nonzero */
	if (tdp_root_level)
		return tdp_root_level;

	/* Use 5-level TDP if and only if it's useful/necessary. */
	if (max_tdp_level == 5 && cpuid_maxphyaddr(vcpu) <= 48)
		return 4;

	return max_tdp_level;
}

static union kvm_mmu_page_role
kvm_calc_tdp_mmu_root_page_role(struct kvm_vcpu *vcpu,
				union kvm_cpu_role cpu_role)
{
	union kvm_mmu_page_role role = {0};

	role.access = ACC_ALL;
	role.cr0_wp = true;
	role.efer_nx = true;
	role.smm = cpu_role.base.smm;
	role.guest_mode = cpu_role.base.guest_mode;
	role.ad_disabled = !kvm_ad_enabled();
	role.level = kvm_mmu_get_tdp_level(vcpu);
	role.direct = true;
	role.has_4_byte_gpte = false;

	return role;
}

static void init_kvm_tdp_mmu(struct kvm_vcpu *vcpu,
			     union kvm_cpu_role cpu_role)
{
	struct kvm_mmu *context = &vcpu->arch.root_mmu;
	union kvm_mmu_page_role root_role = kvm_calc_tdp_mmu_root_page_role(vcpu, cpu_role);

	if (cpu_role.as_u64 == context->cpu_role.as_u64 &&
	    root_role.word == context->root_role.word)
		return;

	context->cpu_role.as_u64 = cpu_role.as_u64;
	context->root_role.word = root_role.word;
	context->page_fault = kvm_tdp_page_fault;
	context->sync_spte = NULL;
	context->get_guest_pgd = get_guest_cr3;
	context->get_pdptr = kvm_pdptr_read;
	context->inject_page_fault = kvm_inject_page_fault;

	if (!is_cr0_pg(context))
		context->gva_to_gpa = nonpaging_gva_to_gpa;
	else if (is_cr4_pae(context))
		context->gva_to_gpa = paging64_gva_to_gpa;
	else
		context->gva_to_gpa = paging32_gva_to_gpa;

	reset_guest_paging_metadata(vcpu, context);
	reset_tdp_shadow_zero_bits_mask(context);
}

static void shadow_mmu_init_context(struct kvm_vcpu *vcpu, struct kvm_mmu *context,
				    union kvm_cpu_role cpu_role,
				    union kvm_mmu_page_role root_role)
{
	if (cpu_role.as_u64 == context->cpu_role.as_u64 &&
	    root_role.word == context->root_role.word)
		return;

	context->cpu_role.as_u64 = cpu_role.as_u64;
	context->root_role.word = root_role.word;

	if (!is_cr0_pg(context))
		nonpaging_init_context(context);
	else if (is_cr4_pae(context))
		paging64_init_context(context);
	else
		paging32_init_context(context);

	reset_guest_paging_metadata(vcpu, context);
	reset_shadow_zero_bits_mask(vcpu, context);
}

static void kvm_init_shadow_mmu(struct kvm_vcpu *vcpu,
				union kvm_cpu_role cpu_role)
{
	struct kvm_mmu *context = &vcpu->arch.root_mmu;
	union kvm_mmu_page_role root_role;

	root_role = cpu_role.base;

	/* KVM uses PAE paging whenever the guest isn't using 64-bit paging. */
	root_role.level = max_t(u32, root_role.level, PT32E_ROOT_LEVEL);

	/*
	 * KVM forces EFER.NX=1 when TDP is disabled, reflect it in the MMU role.
	 * KVM uses NX when TDP is disabled to handle a variety of scenarios,
	 * notably for huge SPTEs if iTLB multi-hit mitigation is enabled and
	 * to generate correct permissions for CR0.WP=0/CR4.SMEP=1/EFER.NX=0.
	 * The iTLB multi-hit workaround can be toggled at any time, so assume
	 * NX can be used by any non-nested shadow MMU to avoid having to reset
	 * MMU contexts.
	 */
	root_role.efer_nx = true;

	shadow_mmu_init_context(vcpu, context, cpu_role, root_role);
}

void kvm_init_shadow_npt_mmu(struct kvm_vcpu *vcpu, unsigned long cr0,
			     unsigned long cr4, u64 efer, gpa_t nested_cr3)
{
	struct kvm_mmu *context = &vcpu->arch.guest_mmu;
	struct kvm_mmu_role_regs regs = {
		.cr0 = cr0,
		.cr4 = cr4 & ~X86_CR4_PKE,
		.efer = efer,
	};
	union kvm_cpu_role cpu_role = kvm_calc_cpu_role(vcpu, &regs);
	union kvm_mmu_page_role root_role;

	/* NPT requires CR0.PG=1. */
	WARN_ON_ONCE(cpu_role.base.direct);

	root_role = cpu_role.base;
	root_role.level = kvm_mmu_get_tdp_level(vcpu);
	if (root_role.level == PT64_ROOT_5LEVEL &&
	    cpu_role.base.level == PT64_ROOT_4LEVEL)
		root_role.passthrough = 1;

	shadow_mmu_init_context(vcpu, context, cpu_role, root_role);
	kvm_mmu_new_pgd(vcpu, nested_cr3);
}
EXPORT_SYMBOL_GPL(kvm_init_shadow_npt_mmu);

static union kvm_cpu_role
kvm_calc_shadow_ept_root_page_role(struct kvm_vcpu *vcpu, bool accessed_dirty,
				   bool execonly, u8 level)
{
	union kvm_cpu_role role = {0};

	/*
	 * KVM does not support SMM transfer monitors, and consequently does not
	 * support the "entry to SMM" control either.  role.base.smm is always 0.
	 */
	WARN_ON_ONCE(is_smm(vcpu));
	role.base.level = level;
	role.base.has_4_byte_gpte = false;
	role.base.direct = false;
	role.base.ad_disabled = !accessed_dirty;
	role.base.guest_mode = true;
	role.base.access = ACC_ALL;

	role.ext.word = 0;
	role.ext.execonly = execonly;
	role.ext.valid = 1;

	return role;
}

void kvm_init_shadow_ept_mmu(struct kvm_vcpu *vcpu, bool execonly,
			     int huge_page_level, bool accessed_dirty,
			     gpa_t new_eptp)
{
	struct kvm_mmu *context = &vcpu->arch.guest_mmu;
	u8 level = vmx_eptp_page_walk_level(new_eptp);
	union kvm_cpu_role new_mode =
		kvm_calc_shadow_ept_root_page_role(vcpu, accessed_dirty,
						   execonly, level);

	if (new_mode.as_u64 != context->cpu_role.as_u64) {
		/* EPT, and thus nested EPT, does not consume CR0, CR4, nor EFER. */
		context->cpu_role.as_u64 = new_mode.as_u64;
		context->root_role.word = new_mode.base.word;

		context->page_fault = ept_page_fault;
		context->gva_to_gpa = ept_gva_to_gpa;
		context->sync_spte = ept_sync_spte;

		update_permission_bitmask(context, true);
		context->pkru_mask = 0;
		reset_rsvds_bits_mask_ept(vcpu, context, execonly, huge_page_level);
		reset_ept_shadow_zero_bits_mask(context, execonly);
	}

	kvm_mmu_new_pgd(vcpu, new_eptp);
}
EXPORT_SYMBOL_GPL(kvm_init_shadow_ept_mmu);

static void init_kvm_softmmu(struct kvm_vcpu *vcpu,
			     union kvm_cpu_role cpu_role)
{
	struct kvm_mmu *context = &vcpu->arch.root_mmu;

	kvm_init_shadow_mmu(vcpu, cpu_role);

	context->get_guest_pgd     = get_guest_cr3;
	context->get_pdptr         = kvm_pdptr_read;
	context->inject_page_fault = kvm_inject_page_fault;
}

static void init_kvm_nested_mmu(struct kvm_vcpu *vcpu,
				union kvm_cpu_role new_mode)
{
	struct kvm_mmu *g_context = &vcpu->arch.nested_mmu;

	if (new_mode.as_u64 == g_context->cpu_role.as_u64)
		return;

	g_context->cpu_role.as_u64   = new_mode.as_u64;
	g_context->get_guest_pgd     = get_guest_cr3;
	g_context->get_pdptr         = kvm_pdptr_read;
	g_context->inject_page_fault = kvm_inject_page_fault;

	/*
	 * L2 page tables are never shadowed, so there is no need to sync
	 * SPTEs.
	 */
	g_context->sync_spte         = NULL;

	/*
	 * Note that arch.mmu->gva_to_gpa translates l2_gpa to l1_gpa using
	 * L1's nested page tables (e.g. EPT12). The nested translation
	 * of l2_gva to l1_gpa is done by arch.nested_mmu.gva_to_gpa using
	 * L2's page tables as the first level of translation and L1's
	 * nested page tables as the second level of translation. Basically
	 * the gva_to_gpa functions between mmu and nested_mmu are swapped.
	 */
	if (!is_paging(vcpu))
		g_context->gva_to_gpa = nonpaging_gva_to_gpa;
	else if (is_long_mode(vcpu))
		g_context->gva_to_gpa = paging64_gva_to_gpa;
	else if (is_pae(vcpu))
		g_context->gva_to_gpa = paging64_gva_to_gpa;
	else
		g_context->gva_to_gpa = paging32_gva_to_gpa;

	reset_guest_paging_metadata(vcpu, g_context);
}

void kvm_init_mmu(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_role_regs regs = vcpu_to_role_regs(vcpu);
	union kvm_cpu_role cpu_role = kvm_calc_cpu_role(vcpu, &regs);

	if (mmu_is_nested(vcpu))
		init_kvm_nested_mmu(vcpu, cpu_role);
	else if (tdp_enabled)
		init_kvm_tdp_mmu(vcpu, cpu_role);
	else
		init_kvm_softmmu(vcpu, cpu_role);
}
EXPORT_SYMBOL_GPL(kvm_init_mmu);

void kvm_mmu_after_set_cpuid(struct kvm_vcpu *vcpu)
{
	/*
	 * Invalidate all MMU roles to force them to reinitialize as CPUID
	 * information is factored into reserved bit calculations.
	 *
	 * Correctly handling multiple vCPU models with respect to paging and
	 * physical address properties) in a single VM would require tracking
	 * all relevant CPUID information in kvm_mmu_page_role. That is very
	 * undesirable as it would increase the memory requirements for
	 * gfn_write_track (see struct kvm_mmu_page_role comments).  For now
	 * that problem is swept under the rug; KVM's CPUID API is horrific and
	 * it's all but impossible to solve it without introducing a new API.
	 */
	vcpu->arch.root_mmu.root_role.word = 0;
	vcpu->arch.guest_mmu.root_role.word = 0;
	vcpu->arch.nested_mmu.root_role.word = 0;
	vcpu->arch.root_mmu.cpu_role.ext.valid = 0;
	vcpu->arch.guest_mmu.cpu_role.ext.valid = 0;
	vcpu->arch.nested_mmu.cpu_role.ext.valid = 0;
	kvm_mmu_reset_context(vcpu);

	/*
	 * Changing guest CPUID after KVM_RUN is forbidden, see the comment in
	 * kvm_arch_vcpu_ioctl().
	 */
	KVM_BUG_ON(kvm_vcpu_has_run(vcpu), vcpu->kvm);
}

void kvm_mmu_reset_context(struct kvm_vcpu *vcpu)
{
	kvm_mmu_unload(vcpu);
	kvm_init_mmu(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_mmu_reset_context);

int kvm_mmu_load(struct kvm_vcpu *vcpu)
{
	int r;

	r = mmu_topup_memory_caches(vcpu, !vcpu->arch.mmu->root_role.direct);
	if (r)
		goto out;
	r = mmu_alloc_special_roots(vcpu);
	if (r)
		goto out;
	if (vcpu->arch.mmu->root_role.direct)
		r = mmu_alloc_direct_roots(vcpu);
	else
		r = mmu_alloc_shadow_roots(vcpu);
	if (r)
		goto out;

	kvm_mmu_sync_roots(vcpu);

	kvm_mmu_load_pgd(vcpu);

	/*
	 * Flush any TLB entries for the new root, the provenance of the root
	 * is unknown.  Even if KVM ensures there are no stale TLB entries
	 * for a freed root, in theory another hypervisor could have left
	 * stale entries.  Flushing on alloc also allows KVM to skip the TLB
	 * flush when freeing a root (see kvm_tdp_mmu_put_root()).
	 */
	static_call(kvm_x86_flush_tlb_current)(vcpu);
out:
	return r;
}

void kvm_mmu_unload(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	kvm_mmu_free_roots(kvm, &vcpu->arch.root_mmu, KVM_MMU_ROOTS_ALL);
	WARN_ON_ONCE(VALID_PAGE(vcpu->arch.root_mmu.root.hpa));
	kvm_mmu_free_roots(kvm, &vcpu->arch.guest_mmu, KVM_MMU_ROOTS_ALL);
	WARN_ON_ONCE(VALID_PAGE(vcpu->arch.guest_mmu.root.hpa));
	vcpu_clear_mmio_info(vcpu, MMIO_GVA_ANY);
}

static bool is_obsolete_root(struct kvm *kvm, hpa_t root_hpa)
{
	struct kvm_mmu_page *sp;

	if (!VALID_PAGE(root_hpa))
		return false;

	/*
	 * When freeing obsolete roots, treat roots as obsolete if they don't
	 * have an associated shadow page, as it's impossible to determine if
	 * such roots are fresh or stale.  This does mean KVM will get false
	 * positives and free roots that don't strictly need to be freed, but
	 * such false positives are relatively rare:
	 *
	 *  (a) only PAE paging and nested NPT have roots without shadow pages
	 *      (or any shadow paging flavor with a dummy root, see note below)
	 *  (b) remote reloads due to a memslot update obsoletes _all_ roots
	 *  (c) KVM doesn't track previous roots for PAE paging, and the guest
	 *      is unlikely to zap an in-use PGD.
	 *
	 * Note!  Dummy roots are unique in that they are obsoleted by memslot
	 * _creation_!  See also FNAME(fetch).
	 */
	sp = root_to_sp(root_hpa);
	return !sp || is_obsolete_sp(kvm, sp);
}

static void __kvm_mmu_free_obsolete_roots(struct kvm *kvm, struct kvm_mmu *mmu)
{
	unsigned long roots_to_free = 0;
	int i;

	if (is_obsolete_root(kvm, mmu->root.hpa))
		roots_to_free |= KVM_MMU_ROOT_CURRENT;

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++) {
		if (is_obsolete_root(kvm, mmu->prev_roots[i].hpa))
			roots_to_free |= KVM_MMU_ROOT_PREVIOUS(i);
	}

	if (roots_to_free)
		kvm_mmu_free_roots(kvm, mmu, roots_to_free);
}

void kvm_mmu_free_obsolete_roots(struct kvm_vcpu *vcpu)
{
	__kvm_mmu_free_obsolete_roots(vcpu->kvm, &vcpu->arch.root_mmu);
	__kvm_mmu_free_obsolete_roots(vcpu->kvm, &vcpu->arch.guest_mmu);
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

	offset = offset_in_page(gpa);
	pte_size = sp->role.has_4_byte_gpte ? 4 : 8;

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
	if (sp->role.has_4_byte_gpte) {
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

void kvm_mmu_track_write(struct kvm_vcpu *vcpu, gpa_t gpa, const u8 *new,
			 int bytes)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct kvm_mmu_page *sp;
	LIST_HEAD(invalid_list);
	u64 entry, gentry, *spte;
	int npte;
	bool flush = false;

	/*
	 * If we don't have indirect shadow pages, it means no page is
	 * write-protected, so we can exit simply.
	 */
	if (!READ_ONCE(vcpu->kvm->arch.indirect_shadow_pages))
		return;

	write_lock(&vcpu->kvm->mmu_lock);

	gentry = mmu_pte_write_fetch_gpte(vcpu, &gpa, &bytes);

	++vcpu->kvm->stat.mmu_pte_write;

	for_each_gfn_valid_sp_with_gptes(vcpu->kvm, sp, gfn) {
		if (detect_write_misaligned(sp, gpa, bytes) ||
		      detect_write_flooding(sp)) {
			kvm_mmu_prepare_zap_page(vcpu->kvm, sp, &invalid_list);
			++vcpu->kvm->stat.mmu_flooded;
			continue;
		}

		spte = get_written_sptes(sp, gpa, &npte);
		if (!spte)
			continue;

		while (npte--) {
			entry = *spte;
			mmu_page_zap_pte(vcpu->kvm, sp, spte, NULL);
			if (gentry && sp->role.level != PG_LEVEL_4K)
				++vcpu->kvm->stat.mmu_pde_zapped;
			if (is_shadow_present_pte(entry))
				flush = true;
			++spte;
		}
	}
	kvm_mmu_remote_flush_or_zap(vcpu->kvm, &invalid_list, flush);
	write_unlock(&vcpu->kvm->mmu_lock);
}

int noinline kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa, u64 error_code,
		       void *insn, int insn_len)
{
	int r, emulation_type = EMULTYPE_PF;
	bool direct = vcpu->arch.mmu->root_role.direct;

	/*
	 * IMPLICIT_ACCESS is a KVM-defined flag used to correctly perform SMAP
	 * checks when emulating instructions that triggers implicit access.
	 * WARN if hardware generates a fault with an error code that collides
	 * with the KVM-defined value.  Clear the flag and continue on, i.e.
	 * don't terminate the VM, as KVM can't possibly be relying on a flag
	 * that KVM doesn't know about.
	 */
	if (WARN_ON_ONCE(error_code & PFERR_IMPLICIT_ACCESS))
		error_code &= ~PFERR_IMPLICIT_ACCESS;

	if (WARN_ON_ONCE(!VALID_PAGE(vcpu->arch.mmu->root.hpa)))
		return RET_PF_RETRY;

	r = RET_PF_INVALID;
	if (unlikely(error_code & PFERR_RSVD_MASK)) {
		r = handle_mmio_page_fault(vcpu, cr2_or_gpa, direct);
		if (r == RET_PF_EMULATE)
			goto emulate;
	}

	if (r == RET_PF_INVALID) {
		r = kvm_mmu_do_page_fault(vcpu, cr2_or_gpa,
					  lower_32_bits(error_code), false,
					  &emulation_type);
		if (KVM_BUG_ON(r == RET_PF_INVALID, vcpu->kvm))
			return -EIO;
	}

	if (r < 0)
		return r;
	if (r != RET_PF_EMULATE)
		return 1;

	/*
	 * Before emulating the instruction, check if the error code
	 * was due to a RO violation while translating the guest page.
	 * This can occur when using nested virtualization with nested
	 * paging in both guests. If true, we simply unprotect the page
	 * and resume the guest.
	 */
	if (vcpu->arch.mmu->root_role.direct &&
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
	return x86_emulate_instruction(vcpu, cr2_or_gpa, emulation_type, insn,
				       insn_len);
}
EXPORT_SYMBOL_GPL(kvm_mmu_page_fault);

static void __kvm_mmu_invalidate_addr(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
				      u64 addr, hpa_t root_hpa)
{
	struct kvm_shadow_walk_iterator iterator;

	vcpu_clear_mmio_info(vcpu, addr);

	/*
	 * Walking and synchronizing SPTEs both assume they are operating in
	 * the context of the current MMU, and would need to be reworked if
	 * this is ever used to sync the guest_mmu, e.g. to emulate INVEPT.
	 */
	if (WARN_ON_ONCE(mmu != vcpu->arch.mmu))
		return;

	if (!VALID_PAGE(root_hpa))
		return;

	write_lock(&vcpu->kvm->mmu_lock);
	for_each_shadow_entry_using_root(vcpu, root_hpa, addr, iterator) {
		struct kvm_mmu_page *sp = sptep_to_sp(iterator.sptep);

		if (sp->unsync) {
			int ret = kvm_sync_spte(vcpu, sp, iterator.index);

			if (ret < 0)
				mmu_page_zap_pte(vcpu->kvm, sp, iterator.sptep, NULL);
			if (ret)
				kvm_flush_remote_tlbs_sptep(vcpu->kvm, iterator.sptep);
		}

		if (!sp->unsync_children)
			break;
	}
	write_unlock(&vcpu->kvm->mmu_lock);
}

void kvm_mmu_invalidate_addr(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			     u64 addr, unsigned long roots)
{
	int i;

	WARN_ON_ONCE(roots & ~KVM_MMU_ROOTS_ALL);

	/* It's actually a GPA for vcpu->arch.guest_mmu.  */
	if (mmu != &vcpu->arch.guest_mmu) {
		/* INVLPG on a non-canonical address is a NOP according to the SDM.  */
		if (is_noncanonical_address(addr, vcpu))
			return;

		static_call(kvm_x86_flush_tlb_gva)(vcpu, addr);
	}

	if (!mmu->sync_spte)
		return;

	if (roots & KVM_MMU_ROOT_CURRENT)
		__kvm_mmu_invalidate_addr(vcpu, mmu, addr, mmu->root.hpa);

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++) {
		if (roots & KVM_MMU_ROOT_PREVIOUS(i))
			__kvm_mmu_invalidate_addr(vcpu, mmu, addr, mmu->prev_roots[i].hpa);
	}
}
EXPORT_SYMBOL_GPL(kvm_mmu_invalidate_addr);

void kvm_mmu_invlpg(struct kvm_vcpu *vcpu, gva_t gva)
{
	/*
	 * INVLPG is required to invalidate any global mappings for the VA,
	 * irrespective of PCID.  Blindly sync all roots as it would take
	 * roughly the same amount of work/time to determine whether any of the
	 * previous roots have a global mapping.
	 *
	 * Mappings not reachable via the current or previous cached roots will
	 * be synced when switching to that new cr3, so nothing needs to be
	 * done here for them.
	 */
	kvm_mmu_invalidate_addr(vcpu, vcpu->arch.walk_mmu, gva, KVM_MMU_ROOTS_ALL);
	++vcpu->stat.invlpg;
}
EXPORT_SYMBOL_GPL(kvm_mmu_invlpg);


void kvm_mmu_invpcid_gva(struct kvm_vcpu *vcpu, gva_t gva, unsigned long pcid)
{
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	unsigned long roots = 0;
	uint i;

	if (pcid == kvm_get_active_pcid(vcpu))
		roots |= KVM_MMU_ROOT_CURRENT;

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++) {
		if (VALID_PAGE(mmu->prev_roots[i].hpa) &&
		    pcid == kvm_get_pcid(vcpu, mmu->prev_roots[i].pgd))
			roots |= KVM_MMU_ROOT_PREVIOUS(i);
	}

	if (roots)
		kvm_mmu_invalidate_addr(vcpu, mmu, gva, roots);
	++vcpu->stat.invlpg;

	/*
	 * Mappings not reachable via the current cr3 or the prev_roots will be
	 * synced when switching to that cr3, so nothing needs to be done here
	 * for them.
	 */
}

void kvm_configure_mmu(bool enable_tdp, int tdp_forced_root_level,
		       int tdp_max_root_level, int tdp_huge_page_level)
{
	tdp_enabled = enable_tdp;
	tdp_root_level = tdp_forced_root_level;
	max_tdp_level = tdp_max_root_level;

#ifdef CONFIG_X86_64
	tdp_mmu_enabled = tdp_mmu_allowed && tdp_enabled;
#endif
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
typedef bool (*slot_rmaps_handler) (struct kvm *kvm,
				    struct kvm_rmap_head *rmap_head,
				    const struct kvm_memory_slot *slot);

static __always_inline bool __walk_slot_rmaps(struct kvm *kvm,
					      const struct kvm_memory_slot *slot,
					      slot_rmaps_handler fn,
					      int start_level, int end_level,
					      gfn_t start_gfn, gfn_t end_gfn,
					      bool flush_on_yield, bool flush)
{
	struct slot_rmap_walk_iterator iterator;

	lockdep_assert_held_write(&kvm->mmu_lock);

	for_each_slot_rmap_range(slot, start_level, end_level, start_gfn,
			end_gfn, &iterator) {
		if (iterator.rmap)
			flush |= fn(kvm, iterator.rmap, slot);

		if (need_resched() || rwlock_needbreak(&kvm->mmu_lock)) {
			if (flush && flush_on_yield) {
				kvm_flush_remote_tlbs_range(kvm, start_gfn,
							    iterator.gfn - start_gfn + 1);
				flush = false;
			}
			cond_resched_rwlock_write(&kvm->mmu_lock);
		}
	}

	return flush;
}

static __always_inline bool walk_slot_rmaps(struct kvm *kvm,
					    const struct kvm_memory_slot *slot,
					    slot_rmaps_handler fn,
					    int start_level, int end_level,
					    bool flush_on_yield)
{
	return __walk_slot_rmaps(kvm, slot, fn, start_level, end_level,
				 slot->base_gfn, slot->base_gfn + slot->npages - 1,
				 flush_on_yield, false);
}

static __always_inline bool walk_slot_rmaps_4k(struct kvm *kvm,
					       const struct kvm_memory_slot *slot,
					       slot_rmaps_handler fn,
					       bool flush_on_yield)
{
	return walk_slot_rmaps(kvm, slot, fn, PG_LEVEL_4K, PG_LEVEL_4K, flush_on_yield);
}

static void free_mmu_pages(struct kvm_mmu *mmu)
{
	if (!tdp_enabled && mmu->pae_root)
		set_memory_encrypted((unsigned long)mmu->pae_root, 1);
	free_page((unsigned long)mmu->pae_root);
	free_page((unsigned long)mmu->pml4_root);
	free_page((unsigned long)mmu->pml5_root);
}

static int __kvm_mmu_create(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu)
{
	struct page *page;
	int i;

	mmu->root.hpa = INVALID_PAGE;
	mmu->root.pgd = 0;
	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
		mmu->prev_roots[i] = KVM_MMU_ROOT_INFO_INVALID;

	/* vcpu->arch.guest_mmu isn't used when !tdp_enabled. */
	if (!tdp_enabled && mmu == &vcpu->arch.guest_mmu)
		return 0;

	/*
	 * When using PAE paging, the four PDPTEs are treated as 'root' pages,
	 * while the PDP table is a per-vCPU construct that's allocated at MMU
	 * creation.  When emulating 32-bit mode, cr3 is only 32 bits even on
	 * x86_64.  Therefore we need to allocate the PDP table in the first
	 * 4GB of memory, which happens to fit the DMA32 zone.  TDP paging
	 * generally doesn't use PAE paging and can skip allocating the PDP
	 * table.  The main exception, handled here, is SVM's 32-bit NPT.  The
	 * other exception is for shadowing L1's 32-bit or PAE NPT on 64-bit
	 * KVM; that horror is handled on-demand by mmu_alloc_special_roots().
	 */
	if (tdp_enabled && kvm_mmu_get_tdp_level(vcpu) > PT32E_ROOT_LEVEL)
		return 0;

	page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_DMA32);
	if (!page)
		return -ENOMEM;

	mmu->pae_root = page_address(page);

	/*
	 * CR3 is only 32 bits when PAE paging is used, thus it's impossible to
	 * get the CPU to treat the PDPTEs as encrypted.  Decrypt the page so
	 * that KVM's writes and the CPU's reads get along.  Note, this is
	 * only necessary when using shadow paging, as 64-bit NPT can get at
	 * the C-bit even when shadowing 32-bit NPT, and SME isn't supported
	 * by 32-bit kernels (when KVM itself uses 32-bit NPT).
	 */
	if (!tdp_enabled)
		set_memory_decrypted((unsigned long)mmu->pae_root, 1);
	else
		WARN_ON_ONCE(shadow_me_value);

	for (i = 0; i < 4; ++i)
		mmu->pae_root[i] = INVALID_PAE_ROOT;

	return 0;
}

int kvm_mmu_create(struct kvm_vcpu *vcpu)
{
	int ret;

	vcpu->arch.mmu_pte_list_desc_cache.kmem_cache = pte_list_desc_cache;
	vcpu->arch.mmu_pte_list_desc_cache.gfp_zero = __GFP_ZERO;

	vcpu->arch.mmu_page_header_cache.kmem_cache = mmu_page_header_cache;
	vcpu->arch.mmu_page_header_cache.gfp_zero = __GFP_ZERO;

	vcpu->arch.mmu_shadow_page_cache.gfp_zero = __GFP_ZERO;

	vcpu->arch.mmu = &vcpu->arch.root_mmu;
	vcpu->arch.walk_mmu = &vcpu->arch.root_mmu;

	ret = __kvm_mmu_create(vcpu, &vcpu->arch.guest_mmu);
	if (ret)
		return ret;

	ret = __kvm_mmu_create(vcpu, &vcpu->arch.root_mmu);
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
	bool unstable;

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
		if (WARN_ON_ONCE(sp->role.invalid))
			continue;

		/*
		 * No need to flush the TLB since we're only zapping shadow
		 * pages with an obsolete generation number and all vCPUS have
		 * loaded a new root, i.e. the shadow pages being zapped cannot
		 * be in active use by the guest.
		 */
		if (batch >= BATCH_ZAP_PAGES &&
		    cond_resched_rwlock_write(&kvm->mmu_lock)) {
			batch = 0;
			goto restart;
		}

		unstable = __kvm_mmu_prepare_zap_page(kvm, sp,
				&kvm->arch.zapped_obsolete_pages, &nr_zapped);
		batch += nr_zapped;

		if (unstable)
			goto restart;
	}

	/*
	 * Kick all vCPUs (via remote TLB flush) before freeing the page tables
	 * to ensure KVM is not in the middle of a lockless shadow page table
	 * walk, which may reference the pages.  The remote TLB flush itself is
	 * not required and is simply a convenient way to kick vCPUs as needed.
	 * KVM performs a local TLB flush when allocating a new root (see
	 * kvm_mmu_load()), and the reload in the caller ensure no vCPUs are
	 * running with an obsolete MMU.
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

	write_lock(&kvm->mmu_lock);
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
	 * In order to ensure all vCPUs drop their soon-to-be invalid roots,
	 * invalidating TDP MMU roots must be done while holding mmu_lock for
	 * write and in the same critical section as making the reload request,
	 * e.g. before kvm_zap_obsolete_pages() could drop mmu_lock and yield.
	 */
	if (tdp_mmu_enabled)
		kvm_tdp_mmu_invalidate_all_roots(kvm);

	/*
	 * Notify all vcpus to reload its shadow page table and flush TLB.
	 * Then all vcpus will switch to new shadow page table with the new
	 * mmu_valid_gen.
	 *
	 * Note: we need to do this under the protection of mmu_lock,
	 * otherwise, vcpu would purge shadow page but miss tlb flush.
	 */
	kvm_make_all_cpus_request(kvm, KVM_REQ_MMU_FREE_OBSOLETE_ROOTS);

	kvm_zap_obsolete_pages(kvm);

	write_unlock(&kvm->mmu_lock);

	/*
	 * Zap the invalidated TDP MMU roots, all SPTEs must be dropped before
	 * returning to the caller, e.g. if the zap is in response to a memslot
	 * deletion, mmu_notifier callbacks will be unable to reach the SPTEs
	 * associated with the deleted memslot once the update completes, and
	 * Deferring the zap until the final reference to the root is put would
	 * lead to use-after-free.
	 */
	if (tdp_mmu_enabled)
		kvm_tdp_mmu_zap_invalidated_roots(kvm);
}

static bool kvm_has_zapped_obsolete_pages(struct kvm *kvm)
{
	return unlikely(!list_empty_careful(&kvm->arch.zapped_obsolete_pages));
}

void kvm_mmu_init_vm(struct kvm *kvm)
{
	INIT_LIST_HEAD(&kvm->arch.active_mmu_pages);
	INIT_LIST_HEAD(&kvm->arch.zapped_obsolete_pages);
	INIT_LIST_HEAD(&kvm->arch.possible_nx_huge_pages);
	spin_lock_init(&kvm->arch.mmu_unsync_pages_lock);

	if (tdp_mmu_enabled)
		kvm_mmu_init_tdp_mmu(kvm);

	kvm->arch.split_page_header_cache.kmem_cache = mmu_page_header_cache;
	kvm->arch.split_page_header_cache.gfp_zero = __GFP_ZERO;

	kvm->arch.split_shadow_page_cache.gfp_zero = __GFP_ZERO;

	kvm->arch.split_desc_cache.kmem_cache = pte_list_desc_cache;
	kvm->arch.split_desc_cache.gfp_zero = __GFP_ZERO;
}

static void mmu_free_vm_memory_caches(struct kvm *kvm)
{
	kvm_mmu_free_memory_cache(&kvm->arch.split_desc_cache);
	kvm_mmu_free_memory_cache(&kvm->arch.split_page_header_cache);
	kvm_mmu_free_memory_cache(&kvm->arch.split_shadow_page_cache);
}

void kvm_mmu_uninit_vm(struct kvm *kvm)
{
	if (tdp_mmu_enabled)
		kvm_mmu_uninit_tdp_mmu(kvm);

	mmu_free_vm_memory_caches(kvm);
}

static bool kvm_rmap_zap_gfn_range(struct kvm *kvm, gfn_t gfn_start, gfn_t gfn_end)
{
	const struct kvm_memory_slot *memslot;
	struct kvm_memslots *slots;
	struct kvm_memslot_iter iter;
	bool flush = false;
	gfn_t start, end;
	int i;

	if (!kvm_memslots_have_rmaps(kvm))
		return flush;

	for (i = 0; i < kvm_arch_nr_memslot_as_ids(kvm); i++) {
		slots = __kvm_memslots(kvm, i);

		kvm_for_each_memslot_in_gfn_range(&iter, slots, gfn_start, gfn_end) {
			memslot = iter.slot;
			start = max(gfn_start, memslot->base_gfn);
			end = min(gfn_end, memslot->base_gfn + memslot->npages);
			if (WARN_ON_ONCE(start >= end))
				continue;

			flush = __walk_slot_rmaps(kvm, memslot, __kvm_zap_rmap,
						  PG_LEVEL_4K, KVM_MAX_HUGEPAGE_LEVEL,
						  start, end - 1, true, flush);
		}
	}

	return flush;
}

/*
 * Invalidate (zap) SPTEs that cover GFNs from gfn_start and up to gfn_end
 * (not including it)
 */
void kvm_zap_gfn_range(struct kvm *kvm, gfn_t gfn_start, gfn_t gfn_end)
{
	bool flush;

	if (WARN_ON_ONCE(gfn_end <= gfn_start))
		return;

	write_lock(&kvm->mmu_lock);

	kvm_mmu_invalidate_begin(kvm);

	kvm_mmu_invalidate_range_add(kvm, gfn_start, gfn_end);

	flush = kvm_rmap_zap_gfn_range(kvm, gfn_start, gfn_end);

	if (tdp_mmu_enabled)
		flush = kvm_tdp_mmu_zap_leafs(kvm, gfn_start, gfn_end, flush);

	if (flush)
		kvm_flush_remote_tlbs_range(kvm, gfn_start, gfn_end - gfn_start);

	kvm_mmu_invalidate_end(kvm);

	write_unlock(&kvm->mmu_lock);
}

static bool slot_rmap_write_protect(struct kvm *kvm,
				    struct kvm_rmap_head *rmap_head,
				    const struct kvm_memory_slot *slot)
{
	return rmap_write_protect(rmap_head, false);
}

void kvm_mmu_slot_remove_write_access(struct kvm *kvm,
				      const struct kvm_memory_slot *memslot,
				      int start_level)
{
	if (kvm_memslots_have_rmaps(kvm)) {
		write_lock(&kvm->mmu_lock);
		walk_slot_rmaps(kvm, memslot, slot_rmap_write_protect,
				start_level, KVM_MAX_HUGEPAGE_LEVEL, false);
		write_unlock(&kvm->mmu_lock);
	}

	if (tdp_mmu_enabled) {
		read_lock(&kvm->mmu_lock);
		kvm_tdp_mmu_wrprot_slot(kvm, memslot, start_level);
		read_unlock(&kvm->mmu_lock);
	}
}

static inline bool need_topup(struct kvm_mmu_memory_cache *cache, int min)
{
	return kvm_mmu_memory_cache_nr_free_objects(cache) < min;
}

static bool need_topup_split_caches_or_resched(struct kvm *kvm)
{
	if (need_resched() || rwlock_needbreak(&kvm->mmu_lock))
		return true;

	/*
	 * In the worst case, SPLIT_DESC_CACHE_MIN_NR_OBJECTS descriptors are needed
	 * to split a single huge page. Calculating how many are actually needed
	 * is possible but not worth the complexity.
	 */
	return need_topup(&kvm->arch.split_desc_cache, SPLIT_DESC_CACHE_MIN_NR_OBJECTS) ||
	       need_topup(&kvm->arch.split_page_header_cache, 1) ||
	       need_topup(&kvm->arch.split_shadow_page_cache, 1);
}

static int topup_split_caches(struct kvm *kvm)
{
	/*
	 * Allocating rmap list entries when splitting huge pages for nested
	 * MMUs is uncommon as KVM needs to use a list if and only if there is
	 * more than one rmap entry for a gfn, i.e. requires an L1 gfn to be
	 * aliased by multiple L2 gfns and/or from multiple nested roots with
	 * different roles.  Aliasing gfns when using TDP is atypical for VMMs;
	 * a few gfns are often aliased during boot, e.g. when remapping BIOS,
	 * but aliasing rarely occurs post-boot or for many gfns.  If there is
	 * only one rmap entry, rmap->val points directly at that one entry and
	 * doesn't need to allocate a list.  Buffer the cache by the default
	 * capacity so that KVM doesn't have to drop mmu_lock to topup if KVM
	 * encounters an aliased gfn or two.
	 */
	const int capacity = SPLIT_DESC_CACHE_MIN_NR_OBJECTS +
			     KVM_ARCH_NR_OBJS_PER_MEMORY_CACHE;
	int r;

	lockdep_assert_held(&kvm->slots_lock);

	r = __kvm_mmu_topup_memory_cache(&kvm->arch.split_desc_cache, capacity,
					 SPLIT_DESC_CACHE_MIN_NR_OBJECTS);
	if (r)
		return r;

	r = kvm_mmu_topup_memory_cache(&kvm->arch.split_page_header_cache, 1);
	if (r)
		return r;

	return kvm_mmu_topup_memory_cache(&kvm->arch.split_shadow_page_cache, 1);
}

static struct kvm_mmu_page *shadow_mmu_get_sp_for_split(struct kvm *kvm, u64 *huge_sptep)
{
	struct kvm_mmu_page *huge_sp = sptep_to_sp(huge_sptep);
	struct shadow_page_caches caches = {};
	union kvm_mmu_page_role role;
	unsigned int access;
	gfn_t gfn;

	gfn = kvm_mmu_page_get_gfn(huge_sp, spte_index(huge_sptep));
	access = kvm_mmu_page_get_access(huge_sp, spte_index(huge_sptep));

	/*
	 * Note, huge page splitting always uses direct shadow pages, regardless
	 * of whether the huge page itself is mapped by a direct or indirect
	 * shadow page, since the huge page region itself is being directly
	 * mapped with smaller pages.
	 */
	role = kvm_mmu_child_role(huge_sptep, /*direct=*/true, access);

	/* Direct SPs do not require a shadowed_info_cache. */
	caches.page_header_cache = &kvm->arch.split_page_header_cache;
	caches.shadow_page_cache = &kvm->arch.split_shadow_page_cache;

	/* Safe to pass NULL for vCPU since requesting a direct SP. */
	return __kvm_mmu_get_shadow_page(kvm, NULL, &caches, gfn, role);
}

static void shadow_mmu_split_huge_page(struct kvm *kvm,
				       const struct kvm_memory_slot *slot,
				       u64 *huge_sptep)

{
	struct kvm_mmu_memory_cache *cache = &kvm->arch.split_desc_cache;
	u64 huge_spte = READ_ONCE(*huge_sptep);
	struct kvm_mmu_page *sp;
	bool flush = false;
	u64 *sptep, spte;
	gfn_t gfn;
	int index;

	sp = shadow_mmu_get_sp_for_split(kvm, huge_sptep);

	for (index = 0; index < SPTE_ENT_PER_PAGE; index++) {
		sptep = &sp->spt[index];
		gfn = kvm_mmu_page_get_gfn(sp, index);

		/*
		 * The SP may already have populated SPTEs, e.g. if this huge
		 * page is aliased by multiple sptes with the same access
		 * permissions. These entries are guaranteed to map the same
		 * gfn-to-pfn translation since the SP is direct, so no need to
		 * modify them.
		 *
		 * However, if a given SPTE points to a lower level page table,
		 * that lower level page table may only be partially populated.
		 * Installing such SPTEs would effectively unmap a potion of the
		 * huge page. Unmapping guest memory always requires a TLB flush
		 * since a subsequent operation on the unmapped regions would
		 * fail to detect the need to flush.
		 */
		if (is_shadow_present_pte(*sptep)) {
			flush |= !is_last_spte(*sptep, sp->role.level);
			continue;
		}

		spte = make_huge_page_split_spte(kvm, huge_spte, sp->role, index);
		mmu_spte_set(sptep, spte);
		__rmap_add(kvm, cache, slot, sptep, gfn, sp->role.access);
	}

	__link_shadow_page(kvm, cache, huge_sptep, sp, flush);
}

static int shadow_mmu_try_split_huge_page(struct kvm *kvm,
					  const struct kvm_memory_slot *slot,
					  u64 *huge_sptep)
{
	struct kvm_mmu_page *huge_sp = sptep_to_sp(huge_sptep);
	int level, r = 0;
	gfn_t gfn;
	u64 spte;

	/* Grab information for the tracepoint before dropping the MMU lock. */
	gfn = kvm_mmu_page_get_gfn(huge_sp, spte_index(huge_sptep));
	level = huge_sp->role.level;
	spte = *huge_sptep;

	if (kvm_mmu_available_pages(kvm) <= KVM_MIN_FREE_MMU_PAGES) {
		r = -ENOSPC;
		goto out;
	}

	if (need_topup_split_caches_or_resched(kvm)) {
		write_unlock(&kvm->mmu_lock);
		cond_resched();
		/*
		 * If the topup succeeds, return -EAGAIN to indicate that the
		 * rmap iterator should be restarted because the MMU lock was
		 * dropped.
		 */
		r = topup_split_caches(kvm) ?: -EAGAIN;
		write_lock(&kvm->mmu_lock);
		goto out;
	}

	shadow_mmu_split_huge_page(kvm, slot, huge_sptep);

out:
	trace_kvm_mmu_split_huge_page(gfn, spte, level, r);
	return r;
}

static bool shadow_mmu_try_split_huge_pages(struct kvm *kvm,
					    struct kvm_rmap_head *rmap_head,
					    const struct kvm_memory_slot *slot)
{
	struct rmap_iterator iter;
	struct kvm_mmu_page *sp;
	u64 *huge_sptep;
	int r;

restart:
	for_each_rmap_spte(rmap_head, &iter, huge_sptep) {
		sp = sptep_to_sp(huge_sptep);

		/* TDP MMU is enabled, so rmap only contains nested MMU SPs. */
		if (WARN_ON_ONCE(!sp->role.guest_mode))
			continue;

		/* The rmaps should never contain non-leaf SPTEs. */
		if (WARN_ON_ONCE(!is_large_pte(*huge_sptep)))
			continue;

		/* SPs with level >PG_LEVEL_4K should never by unsync. */
		if (WARN_ON_ONCE(sp->unsync))
			continue;

		/* Don't bother splitting huge pages on invalid SPs. */
		if (sp->role.invalid)
			continue;

		r = shadow_mmu_try_split_huge_page(kvm, slot, huge_sptep);

		/*
		 * The split succeeded or needs to be retried because the MMU
		 * lock was dropped. Either way, restart the iterator to get it
		 * back into a consistent state.
		 */
		if (!r || r == -EAGAIN)
			goto restart;

		/* The split failed and shouldn't be retried (e.g. -ENOMEM). */
		break;
	}

	return false;
}

static void kvm_shadow_mmu_try_split_huge_pages(struct kvm *kvm,
						const struct kvm_memory_slot *slot,
						gfn_t start, gfn_t end,
						int target_level)
{
	int level;

	/*
	 * Split huge pages starting with KVM_MAX_HUGEPAGE_LEVEL and working
	 * down to the target level. This ensures pages are recursively split
	 * all the way to the target level. There's no need to split pages
	 * already at the target level.
	 */
	for (level = KVM_MAX_HUGEPAGE_LEVEL; level > target_level; level--)
		__walk_slot_rmaps(kvm, slot, shadow_mmu_try_split_huge_pages,
				  level, level, start, end - 1, true, false);
}

/* Must be called with the mmu_lock held in write-mode. */
void kvm_mmu_try_split_huge_pages(struct kvm *kvm,
				   const struct kvm_memory_slot *memslot,
				   u64 start, u64 end,
				   int target_level)
{
	if (!tdp_mmu_enabled)
		return;

	if (kvm_memslots_have_rmaps(kvm))
		kvm_shadow_mmu_try_split_huge_pages(kvm, memslot, start, end, target_level);

	kvm_tdp_mmu_try_split_huge_pages(kvm, memslot, start, end, target_level, false);

	/*
	 * A TLB flush is unnecessary at this point for the same reasons as in
	 * kvm_mmu_slot_try_split_huge_pages().
	 */
}

void kvm_mmu_slot_try_split_huge_pages(struct kvm *kvm,
					const struct kvm_memory_slot *memslot,
					int target_level)
{
	u64 start = memslot->base_gfn;
	u64 end = start + memslot->npages;

	if (!tdp_mmu_enabled)
		return;

	if (kvm_memslots_have_rmaps(kvm)) {
		write_lock(&kvm->mmu_lock);
		kvm_shadow_mmu_try_split_huge_pages(kvm, memslot, start, end, target_level);
		write_unlock(&kvm->mmu_lock);
	}

	read_lock(&kvm->mmu_lock);
	kvm_tdp_mmu_try_split_huge_pages(kvm, memslot, start, end, target_level, true);
	read_unlock(&kvm->mmu_lock);

	/*
	 * No TLB flush is necessary here. KVM will flush TLBs after
	 * write-protecting and/or clearing dirty on the newly split SPTEs to
	 * ensure that guest writes are reflected in the dirty log before the
	 * ioctl to enable dirty logging on this memslot completes. Since the
	 * split SPTEs retain the write and dirty bits of the huge SPTE, it is
	 * safe for KVM to decide if a TLB flush is necessary based on the split
	 * SPTEs.
	 */
}

static bool kvm_mmu_zap_collapsible_spte(struct kvm *kvm,
					 struct kvm_rmap_head *rmap_head,
					 const struct kvm_memory_slot *slot)
{
	u64 *sptep;
	struct rmap_iterator iter;
	int need_tlb_flush = 0;
	struct kvm_mmu_page *sp;

restart:
	for_each_rmap_spte(rmap_head, &iter, sptep) {
		sp = sptep_to_sp(sptep);

		/*
		 * We cannot do huge page mapping for indirect shadow pages,
		 * which are found on the last rmap (level = 1) when not using
		 * tdp; such shadow pages are synced with the page table in
		 * the guest, and the guest page table is using 4K page size
		 * mapping if the indirect sp has level = 1.
		 */
		if (sp->role.direct &&
		    sp->role.level < kvm_mmu_max_mapping_level(kvm, slot, sp->gfn,
							       PG_LEVEL_NUM)) {
			kvm_zap_one_rmap_spte(kvm, rmap_head, sptep);

			if (kvm_available_flush_remote_tlbs_range())
				kvm_flush_remote_tlbs_sptep(kvm, sptep);
			else
				need_tlb_flush = 1;

			goto restart;
		}
	}

	return need_tlb_flush;
}

static void kvm_rmap_zap_collapsible_sptes(struct kvm *kvm,
					   const struct kvm_memory_slot *slot)
{
	/*
	 * Note, use KVM_MAX_HUGEPAGE_LEVEL - 1 since there's no need to zap
	 * pages that are already mapped at the maximum hugepage level.
	 */
	if (walk_slot_rmaps(kvm, slot, kvm_mmu_zap_collapsible_spte,
			    PG_LEVEL_4K, KVM_MAX_HUGEPAGE_LEVEL - 1, true))
		kvm_flush_remote_tlbs_memslot(kvm, slot);
}

void kvm_mmu_zap_collapsible_sptes(struct kvm *kvm,
				   const struct kvm_memory_slot *slot)
{
	if (kvm_memslots_have_rmaps(kvm)) {
		write_lock(&kvm->mmu_lock);
		kvm_rmap_zap_collapsible_sptes(kvm, slot);
		write_unlock(&kvm->mmu_lock);
	}

	if (tdp_mmu_enabled) {
		read_lock(&kvm->mmu_lock);
		kvm_tdp_mmu_zap_collapsible_sptes(kvm, slot);
		read_unlock(&kvm->mmu_lock);
	}
}

void kvm_mmu_slot_leaf_clear_dirty(struct kvm *kvm,
				   const struct kvm_memory_slot *memslot)
{
	if (kvm_memslots_have_rmaps(kvm)) {
		write_lock(&kvm->mmu_lock);
		/*
		 * Clear dirty bits only on 4k SPTEs since the legacy MMU only
		 * support dirty logging at a 4k granularity.
		 */
		walk_slot_rmaps_4k(kvm, memslot, __rmap_clear_dirty, false);
		write_unlock(&kvm->mmu_lock);
	}

	if (tdp_mmu_enabled) {
		read_lock(&kvm->mmu_lock);
		kvm_tdp_mmu_clear_dirty_slot(kvm, memslot);
		read_unlock(&kvm->mmu_lock);
	}

	/*
	 * The caller will flush the TLBs after this function returns.
	 *
	 * It's also safe to flush TLBs out of mmu lock here as currently this
	 * function is only used for dirty logging, in which case flushing TLB
	 * out of mmu lock also guarantees no dirty pages will be lost in
	 * dirty_bitmap.
	 */
}

static void kvm_mmu_zap_all(struct kvm *kvm)
{
	struct kvm_mmu_page *sp, *node;
	LIST_HEAD(invalid_list);
	int ign;

	write_lock(&kvm->mmu_lock);
restart:
	list_for_each_entry_safe(sp, node, &kvm->arch.active_mmu_pages, link) {
		if (WARN_ON_ONCE(sp->role.invalid))
			continue;
		if (__kvm_mmu_prepare_zap_page(kvm, sp, &invalid_list, &ign))
			goto restart;
		if (cond_resched_rwlock_write(&kvm->mmu_lock))
			goto restart;
	}

	kvm_mmu_commit_zap_page(kvm, &invalid_list);

	if (tdp_mmu_enabled)
		kvm_tdp_mmu_zap_all(kvm);

	write_unlock(&kvm->mmu_lock);
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	kvm_mmu_zap_all(kvm);
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
	kvm_mmu_zap_all_fast(kvm);
}

void kvm_mmu_invalidate_mmio_sptes(struct kvm *kvm, u64 gen)
{
	WARN_ON_ONCE(gen & KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS);

	gen &= MMIO_SPTE_GEN_MASK;

	/*
	 * Generation numbers are incremented in multiples of the number of
	 * address spaces in order to provide unique generations across all
	 * address spaces.  Strip what is effectively the address space
	 * modifier prior to checking for a wrap of the MMIO generation so
	 * that a wrap in any address space is detected.
	 */
	gen &= ~((u64)kvm_arch_nr_memslot_as_ids(kvm) - 1);

	/*
	 * The very rare case: if the MMIO generation number has wrapped,
	 * zap all shadow pages.
	 */
	if (unlikely(gen == 0)) {
		kvm_debug_ratelimited("zapping shadow pages for mmio generation wraparound\n");
		kvm_mmu_zap_all_fast(kvm);
	}
}

static unsigned long mmu_shrink_scan(struct shrinker *shrink,
				     struct shrink_control *sc)
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
		write_lock(&kvm->mmu_lock);

		if (kvm_has_zapped_obsolete_pages(kvm)) {
			kvm_mmu_commit_zap_page(kvm,
			      &kvm->arch.zapped_obsolete_pages);
			goto unlock;
		}

		freed = kvm_mmu_zap_oldest_mmu_pages(kvm, sc->nr_to_scan);

unlock:
		write_unlock(&kvm->mmu_lock);
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

static unsigned long mmu_shrink_count(struct shrinker *shrink,
				      struct shrink_control *sc)
{
	return percpu_counter_read_positive(&kvm_total_used_mmu_pages);
}

static struct shrinker *mmu_shrinker;

static void mmu_destroy_caches(void)
{
	kmem_cache_destroy(pte_list_desc_cache);
	kmem_cache_destroy(mmu_page_header_cache);
}

static int get_nx_huge_pages(char *buffer, const struct kernel_param *kp)
{
	if (nx_hugepage_mitigation_hard_disabled)
		return sysfs_emit(buffer, "never\n");

	return param_get_bool(buffer, kp);
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

	if (nx_hugepage_mitigation_hard_disabled)
		return -EPERM;

	/* In "auto" mode deploy workaround only if CPU has the bug. */
	if (sysfs_streq(val, "off")) {
		new_val = 0;
	} else if (sysfs_streq(val, "force")) {
		new_val = 1;
	} else if (sysfs_streq(val, "auto")) {
		new_val = get_nx_auto_mode();
	} else if (sysfs_streq(val, "never")) {
		new_val = 0;

		mutex_lock(&kvm_lock);
		if (!list_empty(&vm_list)) {
			mutex_unlock(&kvm_lock);
			return -EBUSY;
		}
		nx_hugepage_mitigation_hard_disabled = true;
		mutex_unlock(&kvm_lock);
	} else if (kstrtobool(val, &new_val) < 0) {
		return -EINVAL;
	}

	__set_nx_huge_pages(new_val);

	if (new_val != old_val) {
		struct kvm *kvm;

		mutex_lock(&kvm_lock);

		list_for_each_entry(kvm, &vm_list, vm_list) {
			mutex_lock(&kvm->slots_lock);
			kvm_mmu_zap_all_fast(kvm);
			mutex_unlock(&kvm->slots_lock);

			wake_up_process(kvm->arch.nx_huge_page_recovery_thread);
		}
		mutex_unlock(&kvm_lock);
	}

	return 0;
}

/*
 * nx_huge_pages needs to be resolved to true/false when kvm.ko is loaded, as
 * its default value of -1 is technically undefined behavior for a boolean.
 * Forward the module init call to SPTE code so that it too can handle module
 * params that need to be resolved/snapshot.
 */
void __init kvm_mmu_x86_module_init(void)
{
	if (nx_huge_pages == -1)
		__set_nx_huge_pages(get_nx_auto_mode());

	/*
	 * Snapshot userspace's desire to enable the TDP MMU. Whether or not the
	 * TDP MMU is actually enabled is determined in kvm_configure_mmu()
	 * when the vendor module is loaded.
	 */
	tdp_mmu_allowed = tdp_mmu_enabled;

	kvm_mmu_spte_module_init();
}

/*
 * The bulk of the MMU initialization is deferred until the vendor module is
 * loaded as many of the masks/values may be modified by VMX or SVM, i.e. need
 * to be reset when a potentially different vendor module is loaded.
 */
int kvm_mmu_vendor_module_init(void)
{
	int ret = -ENOMEM;

	/*
	 * MMU roles use union aliasing which is, generally speaking, an
	 * undefined behavior. However, we supposedly know how compilers behave
	 * and the current status quo is unlikely to change. Guardians below are
	 * supposed to let us know if the assumption becomes false.
	 */
	BUILD_BUG_ON(sizeof(union kvm_mmu_page_role) != sizeof(u32));
	BUILD_BUG_ON(sizeof(union kvm_mmu_extended_role) != sizeof(u32));
	BUILD_BUG_ON(sizeof(union kvm_cpu_role) != sizeof(u64));

	kvm_mmu_reset_all_pte_masks();

	pte_list_desc_cache = KMEM_CACHE(pte_list_desc, SLAB_ACCOUNT);
	if (!pte_list_desc_cache)
		goto out;

	mmu_page_header_cache = kmem_cache_create("kvm_mmu_page_header",
						  sizeof(struct kvm_mmu_page),
						  0, SLAB_ACCOUNT, NULL);
	if (!mmu_page_header_cache)
		goto out;

	if (percpu_counter_init(&kvm_total_used_mmu_pages, 0, GFP_KERNEL))
		goto out;

	mmu_shrinker = shrinker_alloc(0, "x86-mmu");
	if (!mmu_shrinker)
		goto out_shrinker;

	mmu_shrinker->count_objects = mmu_shrink_count;
	mmu_shrinker->scan_objects = mmu_shrink_scan;
	mmu_shrinker->seeks = DEFAULT_SEEKS * 10;

	shrinker_register(mmu_shrinker);

	return 0;

out_shrinker:
	percpu_counter_destroy(&kvm_total_used_mmu_pages);
out:
	mmu_destroy_caches();
	return ret;
}

void kvm_mmu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_mmu_unload(vcpu);
	free_mmu_pages(&vcpu->arch.root_mmu);
	free_mmu_pages(&vcpu->arch.guest_mmu);
	mmu_free_memory_caches(vcpu);
}

void kvm_mmu_vendor_module_exit(void)
{
	mmu_destroy_caches();
	percpu_counter_destroy(&kvm_total_used_mmu_pages);
	shrinker_free(mmu_shrinker);
}

/*
 * Calculate the effective recovery period, accounting for '0' meaning "let KVM
 * select a halving time of 1 hour".  Returns true if recovery is enabled.
 */
static bool calc_nx_huge_pages_recovery_period(uint *period)
{
	/*
	 * Use READ_ONCE to get the params, this may be called outside of the
	 * param setters, e.g. by the kthread to compute its next timeout.
	 */
	bool enabled = READ_ONCE(nx_huge_pages);
	uint ratio = READ_ONCE(nx_huge_pages_recovery_ratio);

	if (!enabled || !ratio)
		return false;

	*period = READ_ONCE(nx_huge_pages_recovery_period_ms);
	if (!*period) {
		/* Make sure the period is not less than one second.  */
		ratio = min(ratio, 3600u);
		*period = 60 * 60 * 1000 / ratio;
	}
	return true;
}

static int set_nx_huge_pages_recovery_param(const char *val, const struct kernel_param *kp)
{
	bool was_recovery_enabled, is_recovery_enabled;
	uint old_period, new_period;
	int err;

	if (nx_hugepage_mitigation_hard_disabled)
		return -EPERM;

	was_recovery_enabled = calc_nx_huge_pages_recovery_period(&old_period);

	err = param_set_uint(val, kp);
	if (err)
		return err;

	is_recovery_enabled = calc_nx_huge_pages_recovery_period(&new_period);

	if (is_recovery_enabled &&
	    (!was_recovery_enabled || old_period > new_period)) {
		struct kvm *kvm;

		mutex_lock(&kvm_lock);

		list_for_each_entry(kvm, &vm_list, vm_list)
			wake_up_process(kvm->arch.nx_huge_page_recovery_thread);

		mutex_unlock(&kvm_lock);
	}

	return err;
}

static void kvm_recover_nx_huge_pages(struct kvm *kvm)
{
	unsigned long nx_lpage_splits = kvm->stat.nx_lpage_splits;
	struct kvm_memory_slot *slot;
	int rcu_idx;
	struct kvm_mmu_page *sp;
	unsigned int ratio;
	LIST_HEAD(invalid_list);
	bool flush = false;
	ulong to_zap;

	rcu_idx = srcu_read_lock(&kvm->srcu);
	write_lock(&kvm->mmu_lock);

	/*
	 * Zapping TDP MMU shadow pages, including the remote TLB flush, must
	 * be done under RCU protection, because the pages are freed via RCU
	 * callback.
	 */
	rcu_read_lock();

	ratio = READ_ONCE(nx_huge_pages_recovery_ratio);
	to_zap = ratio ? DIV_ROUND_UP(nx_lpage_splits, ratio) : 0;
	for ( ; to_zap; --to_zap) {
		if (list_empty(&kvm->arch.possible_nx_huge_pages))
			break;

		/*
		 * We use a separate list instead of just using active_mmu_pages
		 * because the number of shadow pages that be replaced with an
		 * NX huge page is expected to be relatively small compared to
		 * the total number of shadow pages.  And because the TDP MMU
		 * doesn't use active_mmu_pages.
		 */
		sp = list_first_entry(&kvm->arch.possible_nx_huge_pages,
				      struct kvm_mmu_page,
				      possible_nx_huge_page_link);
		WARN_ON_ONCE(!sp->nx_huge_page_disallowed);
		WARN_ON_ONCE(!sp->role.direct);

		/*
		 * Unaccount and do not attempt to recover any NX Huge Pages
		 * that are being dirty tracked, as they would just be faulted
		 * back in as 4KiB pages. The NX Huge Pages in this slot will be
		 * recovered, along with all the other huge pages in the slot,
		 * when dirty logging is disabled.
		 *
		 * Since gfn_to_memslot() is relatively expensive, it helps to
		 * skip it if it the test cannot possibly return true.  On the
		 * other hand, if any memslot has logging enabled, chances are
		 * good that all of them do, in which case unaccount_nx_huge_page()
		 * is much cheaper than zapping the page.
		 *
		 * If a memslot update is in progress, reading an incorrect value
		 * of kvm->nr_memslots_dirty_logging is not a problem: if it is
		 * becoming zero, gfn_to_memslot() will be done unnecessarily; if
		 * it is becoming nonzero, the page will be zapped unnecessarily.
		 * Either way, this only affects efficiency in racy situations,
		 * and not correctness.
		 */
		slot = NULL;
		if (atomic_read(&kvm->nr_memslots_dirty_logging)) {
			struct kvm_memslots *slots;

			slots = kvm_memslots_for_spte_role(kvm, sp->role);
			slot = __gfn_to_memslot(slots, sp->gfn);
			WARN_ON_ONCE(!slot);
		}

		if (slot && kvm_slot_dirty_track_enabled(slot))
			unaccount_nx_huge_page(kvm, sp);
		else if (is_tdp_mmu_page(sp))
			flush |= kvm_tdp_mmu_zap_sp(kvm, sp);
		else
			kvm_mmu_prepare_zap_page(kvm, sp, &invalid_list);
		WARN_ON_ONCE(sp->nx_huge_page_disallowed);

		if (need_resched() || rwlock_needbreak(&kvm->mmu_lock)) {
			kvm_mmu_remote_flush_or_zap(kvm, &invalid_list, flush);
			rcu_read_unlock();

			cond_resched_rwlock_write(&kvm->mmu_lock);
			flush = false;

			rcu_read_lock();
		}
	}
	kvm_mmu_remote_flush_or_zap(kvm, &invalid_list, flush);

	rcu_read_unlock();

	write_unlock(&kvm->mmu_lock);
	srcu_read_unlock(&kvm->srcu, rcu_idx);
}

static long get_nx_huge_page_recovery_timeout(u64 start_time)
{
	bool enabled;
	uint period;

	enabled = calc_nx_huge_pages_recovery_period(&period);

	return enabled ? start_time + msecs_to_jiffies(period) - get_jiffies_64()
		       : MAX_SCHEDULE_TIMEOUT;
}

static int kvm_nx_huge_page_recovery_worker(struct kvm *kvm, uintptr_t data)
{
	u64 start_time;
	long remaining_time;

	while (true) {
		start_time = get_jiffies_64();
		remaining_time = get_nx_huge_page_recovery_timeout(start_time);

		set_current_state(TASK_INTERRUPTIBLE);
		while (!kthread_should_stop() && remaining_time > 0) {
			schedule_timeout(remaining_time);
			remaining_time = get_nx_huge_page_recovery_timeout(start_time);
			set_current_state(TASK_INTERRUPTIBLE);
		}

		set_current_state(TASK_RUNNING);

		if (kthread_should_stop())
			return 0;

		kvm_recover_nx_huge_pages(kvm);
	}
}

int kvm_mmu_post_init_vm(struct kvm *kvm)
{
	int err;

	if (nx_hugepage_mitigation_hard_disabled)
		return 0;

	err = kvm_vm_create_worker_thread(kvm, kvm_nx_huge_page_recovery_worker, 0,
					  "kvm-nx-lpage-recovery",
					  &kvm->arch.nx_huge_page_recovery_thread);
	if (!err)
		kthread_unpark(kvm->arch.nx_huge_page_recovery_thread);

	return err;
}

void kvm_mmu_pre_destroy_vm(struct kvm *kvm)
{
	if (kvm->arch.nx_huge_page_recovery_thread)
		kthread_stop(kvm->arch.nx_huge_page_recovery_thread);
}

#ifdef CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES
bool kvm_arch_pre_set_memory_attributes(struct kvm *kvm,
					struct kvm_gfn_range *range)
{
	/*
	 * Zap SPTEs even if the slot can't be mapped PRIVATE.  KVM x86 only
	 * supports KVM_MEMORY_ATTRIBUTE_PRIVATE, and so it *seems* like KVM
	 * can simply ignore such slots.  But if userspace is making memory
	 * PRIVATE, then KVM must prevent the guest from accessing the memory
	 * as shared.  And if userspace is making memory SHARED and this point
	 * is reached, then at least one page within the range was previously
	 * PRIVATE, i.e. the slot's possible hugepage ranges are changing.
	 * Zapping SPTEs in this case ensures KVM will reassess whether or not
	 * a hugepage can be used for affected ranges.
	 */
	if (WARN_ON_ONCE(!kvm_arch_has_private_mem(kvm)))
		return false;

	return kvm_unmap_gfn_range(kvm, range);
}

static bool hugepage_test_mixed(struct kvm_memory_slot *slot, gfn_t gfn,
				int level)
{
	return lpage_info_slot(gfn, slot, level)->disallow_lpage & KVM_LPAGE_MIXED_FLAG;
}

static void hugepage_clear_mixed(struct kvm_memory_slot *slot, gfn_t gfn,
				 int level)
{
	lpage_info_slot(gfn, slot, level)->disallow_lpage &= ~KVM_LPAGE_MIXED_FLAG;
}

static void hugepage_set_mixed(struct kvm_memory_slot *slot, gfn_t gfn,
			       int level)
{
	lpage_info_slot(gfn, slot, level)->disallow_lpage |= KVM_LPAGE_MIXED_FLAG;
}

static bool hugepage_has_attrs(struct kvm *kvm, struct kvm_memory_slot *slot,
			       gfn_t gfn, int level, unsigned long attrs)
{
	const unsigned long start = gfn;
	const unsigned long end = start + KVM_PAGES_PER_HPAGE(level);

	if (level == PG_LEVEL_2M)
		return kvm_range_has_memory_attributes(kvm, start, end, attrs);

	for (gfn = start; gfn < end; gfn += KVM_PAGES_PER_HPAGE(level - 1)) {
		if (hugepage_test_mixed(slot, gfn, level - 1) ||
		    attrs != kvm_get_memory_attributes(kvm, gfn))
			return false;
	}
	return true;
}

bool kvm_arch_post_set_memory_attributes(struct kvm *kvm,
					 struct kvm_gfn_range *range)
{
	unsigned long attrs = range->arg.attributes;
	struct kvm_memory_slot *slot = range->slot;
	int level;

	lockdep_assert_held_write(&kvm->mmu_lock);
	lockdep_assert_held(&kvm->slots_lock);

	/*
	 * Calculate which ranges can be mapped with hugepages even if the slot
	 * can't map memory PRIVATE.  KVM mustn't create a SHARED hugepage over
	 * a range that has PRIVATE GFNs, and conversely converting a range to
	 * SHARED may now allow hugepages.
	 */
	if (WARN_ON_ONCE(!kvm_arch_has_private_mem(kvm)))
		return false;

	/*
	 * The sequence matters here: upper levels consume the result of lower
	 * level's scanning.
	 */
	for (level = PG_LEVEL_2M; level <= KVM_MAX_HUGEPAGE_LEVEL; level++) {
		gfn_t nr_pages = KVM_PAGES_PER_HPAGE(level);
		gfn_t gfn = gfn_round_for_level(range->start, level);

		/* Process the head page if it straddles the range. */
		if (gfn != range->start || gfn + nr_pages > range->end) {
			/*
			 * Skip mixed tracking if the aligned gfn isn't covered
			 * by the memslot, KVM can't use a hugepage due to the
			 * misaligned address regardless of memory attributes.
			 */
			if (gfn >= slot->base_gfn) {
				if (hugepage_has_attrs(kvm, slot, gfn, level, attrs))
					hugepage_clear_mixed(slot, gfn, level);
				else
					hugepage_set_mixed(slot, gfn, level);
			}
			gfn += nr_pages;
		}

		/*
		 * Pages entirely covered by the range are guaranteed to have
		 * only the attributes which were just set.
		 */
		for ( ; gfn + nr_pages <= range->end; gfn += nr_pages)
			hugepage_clear_mixed(slot, gfn, level);

		/*
		 * Process the last tail page if it straddles the range and is
		 * contained by the memslot.  Like the head page, KVM can't
		 * create a hugepage if the slot size is misaligned.
		 */
		if (gfn < range->end &&
		    (gfn + nr_pages) <= (slot->base_gfn + slot->npages)) {
			if (hugepage_has_attrs(kvm, slot, gfn, level, attrs))
				hugepage_clear_mixed(slot, gfn, level);
			else
				hugepage_set_mixed(slot, gfn, level);
		}
	}
	return false;
}

void kvm_mmu_init_memslot_memory_attributes(struct kvm *kvm,
					    struct kvm_memory_slot *slot)
{
	int level;

	if (!kvm_arch_has_private_mem(kvm))
		return;

	for (level = PG_LEVEL_2M; level <= KVM_MAX_HUGEPAGE_LEVEL; level++) {
		/*
		 * Don't bother tracking mixed attributes for pages that can't
		 * be huge due to alignment, i.e. process only pages that are
		 * entirely contained by the memslot.
		 */
		gfn_t end = gfn_round_for_level(slot->base_gfn + slot->npages, level);
		gfn_t start = gfn_round_for_level(slot->base_gfn, level);
		gfn_t nr_pages = KVM_PAGES_PER_HPAGE(level);
		gfn_t gfn;

		if (start < slot->base_gfn)
			start += nr_pages;

		/*
		 * Unlike setting attributes, every potential hugepage needs to
		 * be manually checked as the attributes may already be mixed.
		 */
		for (gfn = start; gfn < end; gfn += nr_pages) {
			unsigned long attrs = kvm_get_memory_attributes(kvm, gfn);

			if (hugepage_has_attrs(kvm, slot, gfn, level, attrs))
				hugepage_clear_mixed(slot, gfn, level);
			else
				hugepage_set_mixed(slot, gfn, level);
		}
	}
}
#endif
