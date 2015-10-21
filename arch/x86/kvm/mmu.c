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
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "irq.h"
#include "mmu.h"
#include "x86.h"
#include "kvm_cache_regs.h"
#include "cpuid.h"

#include <linux/kvm_host.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/hugetlb.h>
#include <linux/compiler.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <asm/page.h>
#include <asm/cmpxchg.h>
#include <asm/io.h>
#include <asm/vmx.h>

/*
 * When setting this variable to true it enables Two-Dimensional-Paging
 * where the hardware walks 2 page tables:
 * 1. the guest-virtual to guest-physical
 * 2. while doing 1. it walks guest-physical to host-physical
 * If the hardware supports that we don't need to do shadow paging.
 */
bool tdp_enabled = false;

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
#define PT64_SECOND_AVAIL_BITS_SHIFT 52

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


#define PT64_BASE_ADDR_MASK (((1ULL << 52) - 1) & ~(u64)(PAGE_SIZE-1))
#define PT64_DIR_BASE_ADDR_MASK \
	(PT64_BASE_ADDR_MASK & ~((1ULL << (PAGE_SHIFT + PT64_LEVEL_BITS)) - 1))
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
			| shadow_x_mask | shadow_nx_mask)

#define ACC_EXEC_MASK    1
#define ACC_WRITE_MASK   PT_WRITABLE_MASK
#define ACC_USER_MASK    PT_USER_MASK
#define ACC_ALL          (ACC_EXEC_MASK | ACC_WRITE_MASK | ACC_USER_MASK)

#include <trace/events/kvm.h>

#define CREATE_TRACE_POINTS
#include "mmutrace.h"

#define SPTE_HOST_WRITEABLE	(1ULL << PT_FIRST_AVAIL_BITS_SHIFT)
#define SPTE_MMU_WRITEABLE	(1ULL << (PT_FIRST_AVAIL_BITS_SHIFT + 1))

#define SHADOW_PT_INDEX(addr, level) PT64_INDEX(addr, level)

/* make pte_list_desc fit well in cache line */
#define PTE_LIST_EXT 3

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

#define for_each_shadow_entry(_vcpu, _addr, _walker)    \
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
static u64 __read_mostly shadow_mmio_mask;

static void mmu_spte_set(u64 *sptep, u64 spte);
static void mmu_free_roots(struct kvm_vcpu *vcpu);

void kvm_mmu_set_mmio_spte_mask(u64 mmio_mask)
{
	shadow_mmio_mask = mmio_mask;
}
EXPORT_SYMBOL_GPL(kvm_mmu_set_mmio_spte_mask);

/*
 * the low bit of the generation number is always presumed to be zero.
 * This disables mmio caching during memslot updates.  The concept is
 * similar to a seqcount but instead of retrying the access we just punt
 * and ignore the cache.
 *
 * spte bits 3-11 are used as bits 1-9 of the generation number,
 * the bits 52-61 are used as bits 10-19 of the generation number.
 */
#define MMIO_SPTE_GEN_LOW_SHIFT		2
#define MMIO_SPTE_GEN_HIGH_SHIFT	52

#define MMIO_GEN_SHIFT			20
#define MMIO_GEN_LOW_SHIFT		10
#define MMIO_GEN_LOW_MASK		((1 << MMIO_GEN_LOW_SHIFT) - 2)
#define MMIO_GEN_MASK			((1 << MMIO_GEN_SHIFT) - 1)

static u64 generation_mmio_spte_mask(unsigned int gen)
{
	u64 mask;

	WARN_ON(gen & ~MMIO_GEN_MASK);

	mask = (gen & MMIO_GEN_LOW_MASK) << MMIO_SPTE_GEN_LOW_SHIFT;
	mask |= ((u64)gen >> MMIO_GEN_LOW_SHIFT) << MMIO_SPTE_GEN_HIGH_SHIFT;
	return mask;
}

static unsigned int get_mmio_spte_generation(u64 spte)
{
	unsigned int gen;

	spte &= ~shadow_mmio_mask;

	gen = (spte >> MMIO_SPTE_GEN_LOW_SHIFT) & MMIO_GEN_LOW_MASK;
	gen |= (spte >> MMIO_SPTE_GEN_HIGH_SHIFT) << MMIO_GEN_LOW_SHIFT;
	return gen;
}

static unsigned int kvm_current_mmio_generation(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_memslots(vcpu)->generation & MMIO_GEN_MASK;
}

static void mark_mmio_spte(struct kvm_vcpu *vcpu, u64 *sptep, u64 gfn,
			   unsigned access)
{
	unsigned int gen = kvm_current_mmio_generation(vcpu);
	u64 mask = generation_mmio_spte_mask(gen);

	access &= ACC_WRITE_MASK | ACC_USER_MASK;
	mask |= shadow_mmio_mask | access | gfn << PAGE_SHIFT;

	trace_mark_mmio_spte(sptep, gfn, access, gen);
	mmu_spte_set(sptep, mask);
}

static bool is_mmio_spte(u64 spte)
{
	return (spte & shadow_mmio_mask) == shadow_mmio_mask;
}

static gfn_t get_mmio_spte_gfn(u64 spte)
{
	u64 mask = generation_mmio_spte_mask(MMIO_GEN_MASK) | shadow_mmio_mask;
	return (spte & ~mask) >> PAGE_SHIFT;
}

static unsigned get_mmio_spte_access(u64 spte)
{
	u64 mask = generation_mmio_spte_mask(MMIO_GEN_MASK) | shadow_mmio_mask;
	return (spte & ~mask) & ~PAGE_MASK;
}

static bool set_mmio_spte(struct kvm_vcpu *vcpu, u64 *sptep, gfn_t gfn,
			  pfn_t pfn, unsigned access)
{
	if (unlikely(is_noslot_pfn(pfn))) {
		mark_mmio_spte(vcpu, sptep, gfn, access);
		return true;
	}

	return false;
}

static bool check_mmio_spte(struct kvm_vcpu *vcpu, u64 spte)
{
	unsigned int kvm_gen, spte_gen;

	kvm_gen = kvm_current_mmio_generation(vcpu);
	spte_gen = get_mmio_spte_generation(spte);

	trace_check_mmio_spte(spte, kvm_gen, spte_gen);
	return likely(kvm_gen == spte_gen);
}

void kvm_mmu_set_mask_ptes(u64 user_mask, u64 accessed_mask,
		u64 dirty_mask, u64 nx_mask, u64 x_mask)
{
	shadow_user_mask = user_mask;
	shadow_accessed_mask = accessed_mask;
	shadow_dirty_mask = dirty_mask;
	shadow_nx_mask = nx_mask;
	shadow_x_mask = x_mask;
}
EXPORT_SYMBOL_GPL(kvm_mmu_set_mask_ptes);

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
	return pte & PT_PRESENT_MASK && !is_mmio_spte(pte);
}

static int is_large_pte(u64 pte)
{
	return pte & PT_PAGE_SIZE_MASK;
}

static int is_rmap_spte(u64 pte)
{
	return is_shadow_present_pte(pte);
}

static int is_last_spte(u64 pte, int level)
{
	if (level == PT_PAGE_TABLE_LEVEL)
		return 1;
	if (is_large_pte(pte))
		return 1;
	return 0;
}

static pfn_t spte_to_pfn(u64 pte)
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
	*sptep = spte;
}

static void __update_clear_spte_fast(u64 *sptep, u64 spte)
{
	*sptep = spte;
}

static u64 __update_clear_spte_slow(u64 *sptep, u64 spte)
{
	return xchg(sptep, spte);
}

static u64 __get_spte_lockless(u64 *sptep)
{
	return ACCESS_ONCE(*sptep);
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
	struct kvm_mmu_page *sp =  page_header(__pa(sptep));

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

	ssptep->spte_low = sspte.spte_low;
}

static void __update_clear_spte_fast(u64 *sptep, u64 spte)
{
	union split_spte *ssptep, sspte;

	ssptep = (union split_spte *)sptep;
	sspte = (union split_spte)spte;

	ssptep->spte_low = sspte.spte_low;

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
 * gup_get_pte(arch/x86/mm/gup.c).
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
	struct kvm_mmu_page *sp =  page_header(__pa(sptep));
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

static bool spte_is_locklessly_modifiable(u64 spte)
{
	return (spte & (SPTE_HOST_WRITEABLE | SPTE_MMU_WRITEABLE)) ==
		(SPTE_HOST_WRITEABLE | SPTE_MMU_WRITEABLE);
}

static bool spte_has_volatile_bits(u64 spte)
{
	/*
	 * Always atomicly update spte if it can be updated
	 * out of mmu-lock, it can ensure dirty bit is not lost,
	 * also, it can help us to get a stable is_writable_pte()
	 * to ensure tlb flush is not missed.
	 */
	if (spte_is_locklessly_modifiable(spte))
		return true;

	if (!shadow_accessed_mask)
		return false;

	if (!is_shadow_present_pte(spte))
		return false;

	if ((spte & shadow_accessed_mask) &&
	      (!is_writable_pte(spte) || (spte & shadow_dirty_mask)))
		return false;

	return true;
}

static bool spte_is_bit_cleared(u64 old_spte, u64 new_spte, u64 bit_mask)
{
	return (old_spte & bit_mask) && !(new_spte & bit_mask);
}

static bool spte_is_bit_changed(u64 old_spte, u64 new_spte, u64 bit_mask)
{
	return (old_spte & bit_mask) != (new_spte & bit_mask);
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

/* Rules for using mmu_spte_update:
 * Update the state bits, it means the mapped pfn is not changged.
 *
 * Whenever we overwrite a writable spte with a read-only one we
 * should flush remote TLBs. Otherwise rmap_write_protect
 * will find a read-only spte, even though the writable spte
 * might be cached on a CPU's TLB, the return value indicates this
 * case.
 */
static bool mmu_spte_update(u64 *sptep, u64 new_spte)
{
	u64 old_spte = *sptep;
	bool ret = false;

	WARN_ON(!is_rmap_spte(new_spte));

	if (!is_shadow_present_pte(old_spte)) {
		mmu_spte_set(sptep, new_spte);
		return ret;
	}

	if (!spte_has_volatile_bits(old_spte))
		__update_clear_spte_fast(sptep, new_spte);
	else
		old_spte = __update_clear_spte_slow(sptep, new_spte);

	/*
	 * For the spte updated out of mmu-lock is safe, since
	 * we always atomicly update it, see the comments in
	 * spte_has_volatile_bits().
	 */
	if (spte_is_locklessly_modifiable(old_spte) &&
	      !is_writable_pte(new_spte))
		ret = true;

	if (!shadow_accessed_mask)
		return ret;

	/*
	 * Flush TLB when accessed/dirty bits are changed in the page tables,
	 * to guarantee consistency between TLB and page tables.
	 */
	if (spte_is_bit_changed(old_spte, new_spte,
                                shadow_accessed_mask | shadow_dirty_mask))
		ret = true;

	if (spte_is_bit_cleared(old_spte, new_spte, shadow_accessed_mask))
		kvm_set_pfn_accessed(spte_to_pfn(old_spte));
	if (spte_is_bit_cleared(old_spte, new_spte, shadow_dirty_mask))
		kvm_set_pfn_dirty(spte_to_pfn(old_spte));

	return ret;
}

/*
 * Rules for using mmu_spte_clear_track_bits:
 * It sets the sptep from present to nonpresent, and track the
 * state bits, it is used to clear the last level sptep.
 */
static int mmu_spte_clear_track_bits(u64 *sptep)
{
	pfn_t pfn;
	u64 old_spte = *sptep;

	if (!spte_has_volatile_bits(old_spte))
		__update_clear_spte_fast(sptep, 0ull);
	else
		old_spte = __update_clear_spte_slow(sptep, 0ull);

	if (!is_rmap_spte(old_spte))
		return 0;

	pfn = spte_to_pfn(old_spte);

	/*
	 * KVM does not hold the refcount of the page used by
	 * kvm mmu, before reclaiming the page, we should
	 * unmap it from mmu first.
	 */
	WARN_ON(!kvm_is_reserved_pfn(pfn) && !page_count(pfn_to_page(pfn)));

	if (!shadow_accessed_mask || old_spte & shadow_accessed_mask)
		kvm_set_pfn_accessed(pfn);
	if (!shadow_dirty_mask || (old_spte & shadow_dirty_mask))
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

static void walk_shadow_page_lockless_begin(struct kvm_vcpu *vcpu)
{
	/*
	 * Prevent page table teardown by making any free-er wait during
	 * kvm_flush_remote_tlbs() IPI to all active vcpus.
	 */
	local_irq_disable();
	vcpu->mode = READING_SHADOW_PAGE_TABLES;
	/*
	 * Make sure a following spte read is not reordered ahead of the write
	 * to vcpu->mode.
	 */
	smp_mb();
}

static void walk_shadow_page_lockless_end(struct kvm_vcpu *vcpu)
{
	/*
	 * Make sure the write to vcpu->mode is not reordered in front of
	 * reads to sptes.  If it does, kvm_commit_zap_page() can see us
	 * OUTSIDE_GUEST_MODE and proceed to free the shadow page table.
	 */
	smp_mb();
	vcpu->mode = OUTSIDE_GUEST_MODE;
	local_irq_enable();
}

static int mmu_topup_memory_cache(struct kvm_mmu_memory_cache *cache,
				  struct kmem_cache *base_cache, int min)
{
	void *obj;

	if (cache->nobjs >= min)
		return 0;
	while (cache->nobjs < ARRAY_SIZE(cache->objects)) {
		obj = kmem_cache_zalloc(base_cache, GFP_KERNEL);
		if (!obj)
			return -ENOMEM;
		cache->objects[cache->nobjs++] = obj;
	}
	return 0;
}

static int mmu_memory_cache_free_objects(struct kvm_mmu_memory_cache *cache)
{
	return cache->nobjs;
}

static void mmu_free_memory_cache(struct kvm_mmu_memory_cache *mc,
				  struct kmem_cache *cache)
{
	while (mc->nobjs)
		kmem_cache_free(cache, mc->objects[--mc->nobjs]);
}

static int mmu_topup_memory_cache_page(struct kvm_mmu_memory_cache *cache,
				       int min)
{
	void *page;

	if (cache->nobjs >= min)
		return 0;
	while (cache->nobjs < ARRAY_SIZE(cache->objects)) {
		page = (void *)__get_free_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		cache->objects[cache->nobjs++] = page;
	}
	return 0;
}

static void mmu_free_memory_cache_page(struct kvm_mmu_memory_cache *mc)
{
	while (mc->nobjs)
		free_page((unsigned long)mc->objects[--mc->nobjs]);
}

static int mmu_topup_memory_caches(struct kvm_vcpu *vcpu)
{
	int r;

	r = mmu_topup_memory_cache(&vcpu->arch.mmu_pte_list_desc_cache,
				   pte_list_desc_cache, 8 + PTE_PREFETCH_NUM);
	if (r)
		goto out;
	r = mmu_topup_memory_cache_page(&vcpu->arch.mmu_page_cache, 8);
	if (r)
		goto out;
	r = mmu_topup_memory_cache(&vcpu->arch.mmu_page_header_cache,
				   mmu_page_header_cache, 4);
out:
	return r;
}

static void mmu_free_memory_caches(struct kvm_vcpu *vcpu)
{
	mmu_free_memory_cache(&vcpu->arch.mmu_pte_list_desc_cache,
				pte_list_desc_cache);
	mmu_free_memory_cache_page(&vcpu->arch.mmu_page_cache);
	mmu_free_memory_cache(&vcpu->arch.mmu_page_header_cache,
				mmu_page_header_cache);
}

static void *mmu_memory_cache_alloc(struct kvm_mmu_memory_cache *mc)
{
	void *p;

	BUG_ON(!mc->nobjs);
	p = mc->objects[--mc->nobjs];
	return p;
}

static struct pte_list_desc *mmu_alloc_pte_list_desc(struct kvm_vcpu *vcpu)
{
	return mmu_memory_cache_alloc(&vcpu->arch.mmu_pte_list_desc_cache);
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
	if (sp->role.direct)
		BUG_ON(gfn != kvm_mmu_page_get_gfn(sp, index));
	else
		sp->gfns[index] = gfn;
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

static void account_shadowed(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;
	struct kvm_lpage_info *linfo;
	gfn_t gfn;
	int i;

	gfn = sp->gfn;
	slots = kvm_memslots_for_spte_role(kvm, sp->role);
	slot = __gfn_to_memslot(slots, gfn);
	for (i = PT_DIRECTORY_LEVEL; i <= PT_MAX_HUGEPAGE_LEVEL; ++i) {
		linfo = lpage_info_slot(gfn, slot, i);
		linfo->write_count += 1;
	}
	kvm->arch.indirect_shadow_pages++;
}

static void unaccount_shadowed(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;
	struct kvm_lpage_info *linfo;
	gfn_t gfn;
	int i;

	gfn = sp->gfn;
	slots = kvm_memslots_for_spte_role(kvm, sp->role);
	slot = __gfn_to_memslot(slots, gfn);
	for (i = PT_DIRECTORY_LEVEL; i <= PT_MAX_HUGEPAGE_LEVEL; ++i) {
		linfo = lpage_info_slot(gfn, slot, i);
		linfo->write_count -= 1;
		WARN_ON(linfo->write_count < 0);
	}
	kvm->arch.indirect_shadow_pages--;
}

static int has_wrprotected_page(struct kvm_vcpu *vcpu,
				gfn_t gfn,
				int level)
{
	struct kvm_memory_slot *slot;
	struct kvm_lpage_info *linfo;

	slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	if (slot) {
		linfo = lpage_info_slot(gfn, slot, level);
		return linfo->write_count;
	}

	return 1;
}

static int host_mapping_level(struct kvm *kvm, gfn_t gfn)
{
	unsigned long page_size;
	int i, ret = 0;

	page_size = kvm_host_page_size(kvm, gfn);

	for (i = PT_PAGE_TABLE_LEVEL; i <= PT_MAX_HUGEPAGE_LEVEL; ++i) {
		if (page_size >= KVM_HPAGE_SIZE(i))
			ret = i;
		else
			break;
	}

	return ret;
}

static struct kvm_memory_slot *
gfn_to_memslot_dirty_bitmap(struct kvm_vcpu *vcpu, gfn_t gfn,
			    bool no_dirty_log)
{
	struct kvm_memory_slot *slot;

	slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	if (!slot || slot->flags & KVM_MEMSLOT_INVALID ||
	      (no_dirty_log && slot->dirty_bitmap))
		slot = NULL;

	return slot;
}

static bool mapping_level_dirty_bitmap(struct kvm_vcpu *vcpu, gfn_t large_gfn)
{
	return !gfn_to_memslot_dirty_bitmap(vcpu, large_gfn, true);
}

static int mapping_level(struct kvm_vcpu *vcpu, gfn_t large_gfn)
{
	int host_level, level, max_level;

	host_level = host_mapping_level(vcpu->kvm, large_gfn);

	if (host_level == PT_PAGE_TABLE_LEVEL)
		return host_level;

	max_level = min(kvm_x86_ops->get_lpage_level(), host_level);

	for (level = PT_DIRECTORY_LEVEL; level <= max_level; ++level)
		if (has_wrprotected_page(vcpu, large_gfn, level))
			break;

	return level - 1;
}

/*
 * Pte mapping structures:
 *
 * If pte_list bit zero is zero, then pte_list point to the spte.
 *
 * If pte_list bit zero is one, (then pte_list & ~1) points to a struct
 * pte_list_desc containing more mappings.
 *
 * Returns the number of pte entries before the spte was added or zero if
 * the spte was not added.
 *
 */
static int pte_list_add(struct kvm_vcpu *vcpu, u64 *spte,
			unsigned long *pte_list)
{
	struct pte_list_desc *desc;
	int i, count = 0;

	if (!*pte_list) {
		rmap_printk("pte_list_add: %p %llx 0->1\n", spte, *spte);
		*pte_list = (unsigned long)spte;
	} else if (!(*pte_list & 1)) {
		rmap_printk("pte_list_add: %p %llx 1->many\n", spte, *spte);
		desc = mmu_alloc_pte_list_desc(vcpu);
		desc->sptes[0] = (u64 *)*pte_list;
		desc->sptes[1] = spte;
		*pte_list = (unsigned long)desc | 1;
		++count;
	} else {
		rmap_printk("pte_list_add: %p %llx many->many\n", spte, *spte);
		desc = (struct pte_list_desc *)(*pte_list & ~1ul);
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
pte_list_desc_remove_entry(unsigned long *pte_list, struct pte_list_desc *desc,
			   int i, struct pte_list_desc *prev_desc)
{
	int j;

	for (j = PTE_LIST_EXT - 1; !desc->sptes[j] && j > i; --j)
		;
	desc->sptes[i] = desc->sptes[j];
	desc->sptes[j] = NULL;
	if (j != 0)
		return;
	if (!prev_desc && !desc->more)
		*pte_list = (unsigned long)desc->sptes[0];
	else
		if (prev_desc)
			prev_desc->more = desc->more;
		else
			*pte_list = (unsigned long)desc->more | 1;
	mmu_free_pte_list_desc(desc);
}

static void pte_list_remove(u64 *spte, unsigned long *pte_list)
{
	struct pte_list_desc *desc;
	struct pte_list_desc *prev_desc;
	int i;

	if (!*pte_list) {
		printk(KERN_ERR "pte_list_remove: %p 0->BUG\n", spte);
		BUG();
	} else if (!(*pte_list & 1)) {
		rmap_printk("pte_list_remove:  %p 1->0\n", spte);
		if ((u64 *)*pte_list != spte) {
			printk(KERN_ERR "pte_list_remove:  %p 1->BUG\n", spte);
			BUG();
		}
		*pte_list = 0;
	} else {
		rmap_printk("pte_list_remove:  %p many->many\n", spte);
		desc = (struct pte_list_desc *)(*pte_list & ~1ul);
		prev_desc = NULL;
		while (desc) {
			for (i = 0; i < PTE_LIST_EXT && desc->sptes[i]; ++i)
				if (desc->sptes[i] == spte) {
					pte_list_desc_remove_entry(pte_list,
							       desc, i,
							       prev_desc);
					return;
				}
			prev_desc = desc;
			desc = desc->more;
		}
		pr_err("pte_list_remove: %p many->many\n", spte);
		BUG();
	}
}

typedef void (*pte_list_walk_fn) (u64 *spte);
static void pte_list_walk(unsigned long *pte_list, pte_list_walk_fn fn)
{
	struct pte_list_desc *desc;
	int i;

	if (!*pte_list)
		return;

	if (!(*pte_list & 1))
		return fn((u64 *)*pte_list);

	desc = (struct pte_list_desc *)(*pte_list & ~1ul);
	while (desc) {
		for (i = 0; i < PTE_LIST_EXT && desc->sptes[i]; ++i)
			fn(desc->sptes[i]);
		desc = desc->more;
	}
}

static unsigned long *__gfn_to_rmap(gfn_t gfn, int level,
				    struct kvm_memory_slot *slot)
{
	unsigned long idx;

	idx = gfn_to_index(gfn, slot->base_gfn, level);
	return &slot->arch.rmap[level - PT_PAGE_TABLE_LEVEL][idx];
}

/*
 * Take gfn and return the reverse mapping to it.
 */
static unsigned long *gfn_to_rmap(struct kvm *kvm, gfn_t gfn, struct kvm_mmu_page *sp)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;

	slots = kvm_memslots_for_spte_role(kvm, sp->role);
	slot = __gfn_to_memslot(slots, gfn);
	return __gfn_to_rmap(gfn, sp->role.level, slot);
}

static bool rmap_can_add(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_memory_cache *cache;

	cache = &vcpu->arch.mmu_pte_list_desc_cache;
	return mmu_memory_cache_free_objects(cache);
}

static int rmap_add(struct kvm_vcpu *vcpu, u64 *spte, gfn_t gfn)
{
	struct kvm_mmu_page *sp;
	unsigned long *rmapp;

	sp = page_header(__pa(spte));
	kvm_mmu_page_set_gfn(sp, spte - sp->spt, gfn);
	rmapp = gfn_to_rmap(vcpu->kvm, gfn, sp);
	return pte_list_add(vcpu, spte, rmapp);
}

static void rmap_remove(struct kvm *kvm, u64 *spte)
{
	struct kvm_mmu_page *sp;
	gfn_t gfn;
	unsigned long *rmapp;

	sp = page_header(__pa(spte));
	gfn = kvm_mmu_page_get_gfn(sp, spte - sp->spt);
	rmapp = gfn_to_rmap(kvm, gfn, sp);
	pte_list_remove(spte, rmapp);
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
 * information in the itererator may not be valid.
 *
 * Returns sptep if found, NULL otherwise.
 */
static u64 *rmap_get_first(unsigned long rmap, struct rmap_iterator *iter)
{
	if (!rmap)
		return NULL;

	if (!(rmap & 1)) {
		iter->desc = NULL;
		return (u64 *)rmap;
	}

	iter->desc = (struct pte_list_desc *)(rmap & ~1ul);
	iter->pos = 0;
	return iter->desc->sptes[iter->pos];
}

/*
 * Must be used with a valid iterator: e.g. after rmap_get_first().
 *
 * Returns sptep if found, NULL otherwise.
 */
static u64 *rmap_get_next(struct rmap_iterator *iter)
{
	if (iter->desc) {
		if (iter->pos < PTE_LIST_EXT - 1) {
			u64 *sptep;

			++iter->pos;
			sptep = iter->desc->sptes[iter->pos];
			if (sptep)
				return sptep;
		}

		iter->desc = iter->desc->more;

		if (iter->desc) {
			iter->pos = 0;
			/* desc->sptes[0] cannot be NULL */
			return iter->desc->sptes[iter->pos];
		}
	}

	return NULL;
}

#define for_each_rmap_spte(_rmap_, _iter_, _spte_)			    \
	   for (_spte_ = rmap_get_first(*_rmap_, _iter_);		    \
		_spte_ && ({BUG_ON(!is_shadow_present_pte(*_spte_)); 1;});  \
			_spte_ = rmap_get_next(_iter_))

static void drop_spte(struct kvm *kvm, u64 *sptep)
{
	if (mmu_spte_clear_track_bits(sptep))
		rmap_remove(kvm, sptep);
}


static bool __drop_large_spte(struct kvm *kvm, u64 *sptep)
{
	if (is_large_pte(*sptep)) {
		WARN_ON(page_header(__pa(sptep))->role.level ==
			PT_PAGE_TABLE_LEVEL);
		drop_spte(kvm, sptep);
		--kvm->stat.lpages;
		return true;
	}

	return false;
}

static void drop_large_spte(struct kvm_vcpu *vcpu, u64 *sptep)
{
	if (__drop_large_spte(vcpu->kvm, sptep))
		kvm_flush_remote_tlbs(vcpu->kvm);
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
static bool spte_write_protect(struct kvm *kvm, u64 *sptep, bool pt_protect)
{
	u64 spte = *sptep;

	if (!is_writable_pte(spte) &&
	      !(pt_protect && spte_is_locklessly_modifiable(spte)))
		return false;

	rmap_printk("rmap_write_protect: spte %p %llx\n", sptep, *sptep);

	if (pt_protect)
		spte &= ~SPTE_MMU_WRITEABLE;
	spte = spte & ~PT_WRITABLE_MASK;

	return mmu_spte_update(sptep, spte);
}

static bool __rmap_write_protect(struct kvm *kvm, unsigned long *rmapp,
				 bool pt_protect)
{
	u64 *sptep;
	struct rmap_iterator iter;
	bool flush = false;

	for_each_rmap_spte(rmapp, &iter, sptep)
		flush |= spte_write_protect(kvm, sptep, pt_protect);

	return flush;
}

static bool spte_clear_dirty(struct kvm *kvm, u64 *sptep)
{
	u64 spte = *sptep;

	rmap_printk("rmap_clear_dirty: spte %p %llx\n", sptep, *sptep);

	spte &= ~shadow_dirty_mask;

	return mmu_spte_update(sptep, spte);
}

static bool __rmap_clear_dirty(struct kvm *kvm, unsigned long *rmapp)
{
	u64 *sptep;
	struct rmap_iterator iter;
	bool flush = false;

	for_each_rmap_spte(rmapp, &iter, sptep)
		flush |= spte_clear_dirty(kvm, sptep);

	return flush;
}

static bool spte_set_dirty(struct kvm *kvm, u64 *sptep)
{
	u64 spte = *sptep;

	rmap_printk("rmap_set_dirty: spte %p %llx\n", sptep, *sptep);

	spte |= shadow_dirty_mask;

	return mmu_spte_update(sptep, spte);
}

static bool __rmap_set_dirty(struct kvm *kvm, unsigned long *rmapp)
{
	u64 *sptep;
	struct rmap_iterator iter;
	bool flush = false;

	for_each_rmap_spte(rmapp, &iter, sptep)
		flush |= spte_set_dirty(kvm, sptep);

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
	unsigned long *rmapp;

	while (mask) {
		rmapp = __gfn_to_rmap(slot->base_gfn + gfn_offset + __ffs(mask),
				      PT_PAGE_TABLE_LEVEL, slot);
		__rmap_write_protect(kvm, rmapp, false);

		/* clear the first set bit */
		mask &= mask - 1;
	}
}

/**
 * kvm_mmu_clear_dirty_pt_masked - clear MMU D-bit for PT level pages
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
	unsigned long *rmapp;

	while (mask) {
		rmapp = __gfn_to_rmap(slot->base_gfn + gfn_offset + __ffs(mask),
				      PT_PAGE_TABLE_LEVEL, slot);
		__rmap_clear_dirty(kvm, rmapp);

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
	if (kvm_x86_ops->enable_log_dirty_pt_masked)
		kvm_x86_ops->enable_log_dirty_pt_masked(kvm, slot, gfn_offset,
				mask);
	else
		kvm_mmu_write_protect_pt_masked(kvm, slot, gfn_offset, mask);
}

static bool rmap_write_protect(struct kvm_vcpu *vcpu, u64 gfn)
{
	struct kvm_memory_slot *slot;
	unsigned long *rmapp;
	int i;
	bool write_protected = false;

	slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);

	for (i = PT_PAGE_TABLE_LEVEL; i <= PT_MAX_HUGEPAGE_LEVEL; ++i) {
		rmapp = __gfn_to_rmap(gfn, i, slot);
		write_protected |= __rmap_write_protect(vcpu->kvm, rmapp, true);
	}

	return write_protected;
}

static bool kvm_zap_rmapp(struct kvm *kvm, unsigned long *rmapp)
{
	u64 *sptep;
	struct rmap_iterator iter;
	bool flush = false;

	while ((sptep = rmap_get_first(*rmapp, &iter))) {
		BUG_ON(!(*sptep & PT_PRESENT_MASK));
		rmap_printk("%s: spte %p %llx.\n", __func__, sptep, *sptep);

		drop_spte(kvm, sptep);
		flush = true;
	}

	return flush;
}

static int kvm_unmap_rmapp(struct kvm *kvm, unsigned long *rmapp,
			   struct kvm_memory_slot *slot, gfn_t gfn, int level,
			   unsigned long data)
{
	return kvm_zap_rmapp(kvm, rmapp);
}

static int kvm_set_pte_rmapp(struct kvm *kvm, unsigned long *rmapp,
			     struct kvm_memory_slot *slot, gfn_t gfn, int level,
			     unsigned long data)
{
	u64 *sptep;
	struct rmap_iterator iter;
	int need_flush = 0;
	u64 new_spte;
	pte_t *ptep = (pte_t *)data;
	pfn_t new_pfn;

	WARN_ON(pte_huge(*ptep));
	new_pfn = pte_pfn(*ptep);

restart:
	for_each_rmap_spte(rmapp, &iter, sptep) {
		rmap_printk("kvm_set_pte_rmapp: spte %p %llx gfn %llx (%d)\n",
			     sptep, *sptep, gfn, level);

		need_flush = 1;

		if (pte_write(*ptep)) {
			drop_spte(kvm, sptep);
			goto restart;
		} else {
			new_spte = *sptep & ~PT64_BASE_ADDR_MASK;
			new_spte |= (u64)new_pfn << PAGE_SHIFT;

			new_spte &= ~PT_WRITABLE_MASK;
			new_spte &= ~SPTE_HOST_WRITEABLE;
			new_spte &= ~shadow_accessed_mask;

			mmu_spte_clear_track_bits(sptep);
			mmu_spte_set(sptep, new_spte);
		}
	}

	if (need_flush)
		kvm_flush_remote_tlbs(kvm);

	return 0;
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
	unsigned long *rmap;
	int level;

	/* private field. */
	unsigned long *end_rmap;
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
					       unsigned long *rmapp,
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

			for_each_slot_rmap_range(memslot, PT_PAGE_TABLE_LEVEL,
						 PT_MAX_HUGEPAGE_LEVEL,
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
			  int (*handler)(struct kvm *kvm, unsigned long *rmapp,
					 struct kvm_memory_slot *slot,
					 gfn_t gfn, int level,
					 unsigned long data))
{
	return kvm_handle_hva_range(kvm, hva, hva + 1, data, handler);
}

int kvm_unmap_hva(struct kvm *kvm, unsigned long hva)
{
	return kvm_handle_hva(kvm, hva, 0, kvm_unmap_rmapp);
}

int kvm_unmap_hva_range(struct kvm *kvm, unsigned long start, unsigned long end)
{
	return kvm_handle_hva_range(kvm, start, end, 0, kvm_unmap_rmapp);
}

void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	kvm_handle_hva(kvm, hva, (unsigned long)&pte, kvm_set_pte_rmapp);
}

static int kvm_age_rmapp(struct kvm *kvm, unsigned long *rmapp,
			 struct kvm_memory_slot *slot, gfn_t gfn, int level,
			 unsigned long data)
{
	u64 *sptep;
	struct rmap_iterator uninitialized_var(iter);
	int young = 0;

	BUG_ON(!shadow_accessed_mask);

	for_each_rmap_spte(rmapp, &iter, sptep)
		if (*sptep & shadow_accessed_mask) {
			young = 1;
			clear_bit((ffs(shadow_accessed_mask) - 1),
				 (unsigned long *)sptep);
		}

	trace_kvm_age_page(gfn, level, slot, young);
	return young;
}

static int kvm_test_age_rmapp(struct kvm *kvm, unsigned long *rmapp,
			      struct kvm_memory_slot *slot, gfn_t gfn,
			      int level, unsigned long data)
{
	u64 *sptep;
	struct rmap_iterator iter;
	int young = 0;

	/*
	 * If there's no access bit in the secondary pte set by the
	 * hardware it's up to gup-fast/gup to set the access bit in
	 * the primary pte or in the page structure.
	 */
	if (!shadow_accessed_mask)
		goto out;

	for_each_rmap_spte(rmapp, &iter, sptep)
		if (*sptep & shadow_accessed_mask) {
			young = 1;
			break;
		}
out:
	return young;
}

#define RMAP_RECYCLE_THRESHOLD 1000

static void rmap_recycle(struct kvm_vcpu *vcpu, u64 *spte, gfn_t gfn)
{
	unsigned long *rmapp;
	struct kvm_mmu_page *sp;

	sp = page_header(__pa(spte));

	rmapp = gfn_to_rmap(vcpu->kvm, gfn, sp);

	kvm_unmap_rmapp(vcpu->kvm, rmapp, NULL, gfn, sp->role.level, 0);
	kvm_flush_remote_tlbs(vcpu->kvm);
}

int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end)
{
	/*
	 * In case of absence of EPT Access and Dirty Bits supports,
	 * emulate the accessed bit for EPT, by checking if this page has
	 * an EPT mapping, and clearing it if it does. On the next access,
	 * a new EPT mapping will be established.
	 * This has some overhead, but not as much as the cost of swapping
	 * out actively used pages or breaking up actively used hugepages.
	 */
	if (!shadow_accessed_mask) {
		/*
		 * We are holding the kvm->mmu_lock, and we are blowing up
		 * shadow PTEs. MMU notifier consumers need to be kept at bay.
		 * This is correct as long as we don't decouple the mmu_lock
		 * protected regions (like invalidate_range_start|end does).
		 */
		kvm->mmu_notifier_seq++;
		return kvm_handle_hva_range(kvm, start, end, 0,
					    kvm_unmap_rmapp);
	}

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
static inline void kvm_mod_used_mmu_pages(struct kvm *kvm, int nr)
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
	return gfn & ((1 << KVM_MMU_HASH_SHIFT) - 1);
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
	pte_list_remove(parent_pte, &sp->parent_ptes);
}

static void drop_parent_pte(struct kvm_mmu_page *sp,
			    u64 *parent_pte)
{
	mmu_page_remove_parent_pte(sp, parent_pte);
	mmu_spte_clear_no_track(parent_pte);
}

static struct kvm_mmu_page *kvm_mmu_alloc_page(struct kvm_vcpu *vcpu,
					       u64 *parent_pte, int direct)
{
	struct kvm_mmu_page *sp;

	sp = mmu_memory_cache_alloc(&vcpu->arch.mmu_page_header_cache);
	sp->spt = mmu_memory_cache_alloc(&vcpu->arch.mmu_page_cache);
	if (!direct)
		sp->gfns = mmu_memory_cache_alloc(&vcpu->arch.mmu_page_cache);
	set_page_private(virt_to_page(sp->spt), (unsigned long)sp);

	/*
	 * The active_mmu_pages list is the FIFO list, do not move the
	 * page until it is zapped. kvm_zap_obsolete_pages depends on
	 * this feature. See the comments in kvm_zap_obsolete_pages().
	 */
	list_add(&sp->link, &vcpu->kvm->arch.active_mmu_pages);
	sp->parent_ptes = 0;
	mmu_page_add_parent_pte(vcpu, sp, parent_pte);
	kvm_mod_used_mmu_pages(vcpu->kvm, +1);
	return sp;
}

static void mark_unsync(u64 *spte);
static void kvm_mmu_mark_parents_unsync(struct kvm_mmu_page *sp)
{
	pte_list_walk(&sp->parent_ptes, mark_unsync);
}

static void mark_unsync(u64 *spte)
{
	struct kvm_mmu_page *sp;
	unsigned int index;

	sp = page_header(__pa(spte));
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
	return 1;
}

static void nonpaging_invlpg(struct kvm_vcpu *vcpu, gva_t gva)
{
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

static int __mmu_unsync_walk(struct kvm_mmu_page *sp,
			   struct kvm_mmu_pages *pvec)
{
	int i, ret, nr_unsync_leaf = 0;

	for_each_set_bit(i, sp->unsync_child_bitmap, 512) {
		struct kvm_mmu_page *child;
		u64 ent = sp->spt[i];

		if (!is_shadow_present_pte(ent) || is_large_pte(ent))
			goto clear_child_bitmap;

		child = page_header(ent & PT64_BASE_ADDR_MASK);

		if (child->unsync_children) {
			if (mmu_pages_add(pvec, child, i))
				return -ENOSPC;

			ret = __mmu_unsync_walk(child, pvec);
			if (!ret)
				goto clear_child_bitmap;
			else if (ret > 0)
				nr_unsync_leaf += ret;
			else
				return ret;
		} else if (child->unsync) {
			nr_unsync_leaf++;
			if (mmu_pages_add(pvec, child, i))
				return -ENOSPC;
		} else
			 goto clear_child_bitmap;

		continue;

clear_child_bitmap:
		__clear_bit(i, sp->unsync_child_bitmap);
		sp->unsync_children--;
		WARN_ON((int)sp->unsync_children < 0);
	}


	return nr_unsync_leaf;
}

static int mmu_unsync_walk(struct kvm_mmu_page *sp,
			   struct kvm_mmu_pages *pvec)
{
	if (!sp->unsync_children)
		return 0;

	mmu_pages_add(pvec, sp, 0);
	return __mmu_unsync_walk(sp, pvec);
}

static void kvm_unlink_unsync_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	WARN_ON(!sp->unsync);
	trace_kvm_mmu_sync_page(sp);
	sp->unsync = 0;
	--kvm->stat.mmu_unsync;
}

static int kvm_mmu_prepare_zap_page(struct kvm *kvm, struct kvm_mmu_page *sp,
				    struct list_head *invalid_list);
static void kvm_mmu_commit_zap_page(struct kvm *kvm,
				    struct list_head *invalid_list);

/*
 * NOTE: we should pay more attention on the zapped-obsolete page
 * (is_obsolete_sp(sp) && sp->role.invalid) when you do hash list walk
 * since it has been deleted from active_mmu_pages but still can be found
 * at hast list.
 *
 * for_each_gfn_indirect_valid_sp has skipped that kind of page and
 * kvm_mmu_get_page(), the only user of for_each_gfn_sp(), has skipped
 * all the obsolete pages.
 */
#define for_each_gfn_sp(_kvm, _sp, _gfn)				\
	hlist_for_each_entry(_sp,					\
	  &(_kvm)->arch.mmu_page_hash[kvm_page_table_hashfn(_gfn)], hash_link) \
		if ((_sp)->gfn != (_gfn)) {} else

#define for_each_gfn_indirect_valid_sp(_kvm, _sp, _gfn)			\
	for_each_gfn_sp(_kvm, _sp, _gfn)				\
		if ((_sp)->role.direct || (_sp)->role.invalid) {} else

/* @sp->gfn should be write-protected at the call site */
static int __kvm_sync_page(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			   struct list_head *invalid_list, bool clear_unsync)
{
	if (sp->role.cr4_pae != !!is_pae(vcpu)) {
		kvm_mmu_prepare_zap_page(vcpu->kvm, sp, invalid_list);
		return 1;
	}

	if (clear_unsync)
		kvm_unlink_unsync_page(vcpu->kvm, sp);

	if (vcpu->arch.mmu.sync_page(vcpu, sp)) {
		kvm_mmu_prepare_zap_page(vcpu->kvm, sp, invalid_list);
		return 1;
	}

	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	return 0;
}

static int kvm_sync_page_transient(struct kvm_vcpu *vcpu,
				   struct kvm_mmu_page *sp)
{
	LIST_HEAD(invalid_list);
	int ret;

	ret = __kvm_sync_page(vcpu, sp, &invalid_list, false);
	if (ret)
		kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);

	return ret;
}

#ifdef CONFIG_KVM_MMU_AUDIT
#include "mmu_audit.c"
#else
static void kvm_mmu_audit(struct kvm_vcpu *vcpu, int point) { }
static void mmu_audit_disable(void) { }
#endif

static int kvm_sync_page(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			 struct list_head *invalid_list)
{
	return __kvm_sync_page(vcpu, sp, invalid_list, true);
}

/* @gfn should be write-protected at the call site */
static void kvm_sync_pages(struct kvm_vcpu *vcpu,  gfn_t gfn)
{
	struct kvm_mmu_page *s;
	LIST_HEAD(invalid_list);
	bool flush = false;

	for_each_gfn_indirect_valid_sp(vcpu->kvm, s, gfn) {
		if (!s->unsync)
			continue;

		WARN_ON(s->role.level != PT_PAGE_TABLE_LEVEL);
		kvm_unlink_unsync_page(vcpu->kvm, s);
		if ((s->role.cr4_pae != !!is_pae(vcpu)) ||
			(vcpu->arch.mmu.sync_page(vcpu, s))) {
			kvm_mmu_prepare_zap_page(vcpu->kvm, s, &invalid_list);
			continue;
		}
		flush = true;
	}

	kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
	if (flush)
		kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
}

struct mmu_page_path {
	struct kvm_mmu_page *parent[PT64_ROOT_LEVEL-1];
	unsigned int idx[PT64_ROOT_LEVEL-1];
};

#define for_each_sp(pvec, sp, parents, i)			\
		for (i = mmu_pages_next(&pvec, &parents, -1),	\
			sp = pvec.page[i].sp;			\
			i < pvec.nr && ({ sp = pvec.page[i].sp; 1;});	\
			i = mmu_pages_next(&pvec, &parents, i))

static int mmu_pages_next(struct kvm_mmu_pages *pvec,
			  struct mmu_page_path *parents,
			  int i)
{
	int n;

	for (n = i+1; n < pvec->nr; n++) {
		struct kvm_mmu_page *sp = pvec->page[n].sp;

		if (sp->role.level == PT_PAGE_TABLE_LEVEL) {
			parents->idx[0] = pvec->page[n].idx;
			return n;
		}

		parents->parent[sp->role.level-2] = sp;
		parents->idx[sp->role.level-1] = pvec->page[n].idx;
	}

	return n;
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

		--sp->unsync_children;
		WARN_ON((int)sp->unsync_children < 0);
		__clear_bit(idx, sp->unsync_child_bitmap);
		level++;
	} while (level < PT64_ROOT_LEVEL-1 && !sp->unsync_children);
}

static void kvm_mmu_pages_init(struct kvm_mmu_page *parent,
			       struct mmu_page_path *parents,
			       struct kvm_mmu_pages *pvec)
{
	parents->parent[parent->role.level-1] = NULL;
	pvec->nr = 0;
}

static void mmu_sync_children(struct kvm_vcpu *vcpu,
			      struct kvm_mmu_page *parent)
{
	int i;
	struct kvm_mmu_page *sp;
	struct mmu_page_path parents;
	struct kvm_mmu_pages pages;
	LIST_HEAD(invalid_list);

	kvm_mmu_pages_init(parent, &parents, &pages);
	while (mmu_unsync_walk(parent, &pages)) {
		bool protected = false;

		for_each_sp(pages, sp, parents, i)
			protected |= rmap_write_protect(vcpu, sp->gfn);

		if (protected)
			kvm_flush_remote_tlbs(vcpu->kvm);

		for_each_sp(pages, sp, parents, i) {
			kvm_sync_page(vcpu, sp, &invalid_list);
			mmu_pages_clear_parents(&parents);
		}
		kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
		cond_resched_lock(&vcpu->kvm->mmu_lock);
		kvm_mmu_pages_init(parent, &parents, &pages);
	}
}

static void init_shadow_page_table(struct kvm_mmu_page *sp)
{
	int i;

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i)
		sp->spt[i] = 0ull;
}

static void __clear_sp_write_flooding_count(struct kvm_mmu_page *sp)
{
	sp->write_flooding_count = 0;
}

static void clear_sp_write_flooding_count(u64 *spte)
{
	struct kvm_mmu_page *sp =  page_header(__pa(spte));

	__clear_sp_write_flooding_count(sp);
}

static bool is_obsolete_sp(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	return unlikely(sp->mmu_valid_gen != kvm->arch.mmu_valid_gen);
}

static struct kvm_mmu_page *kvm_mmu_get_page(struct kvm_vcpu *vcpu,
					     gfn_t gfn,
					     gva_t gaddr,
					     unsigned level,
					     int direct,
					     unsigned access,
					     u64 *parent_pte)
{
	union kvm_mmu_page_role role;
	unsigned quadrant;
	struct kvm_mmu_page *sp;
	bool need_sync = false;

	role = vcpu->arch.mmu.base_role;
	role.level = level;
	role.direct = direct;
	if (role.direct)
		role.cr4_pae = 0;
	role.access = access;
	if (!vcpu->arch.mmu.direct_map
	    && vcpu->arch.mmu.root_level <= PT32_ROOT_LEVEL) {
		quadrant = gaddr >> (PAGE_SHIFT + (PT64_PT_BITS * level));
		quadrant &= (1 << ((PT32_PT_BITS - PT64_PT_BITS) * level)) - 1;
		role.quadrant = quadrant;
	}
	for_each_gfn_sp(vcpu->kvm, sp, gfn) {
		if (is_obsolete_sp(vcpu->kvm, sp))
			continue;

		if (!need_sync && sp->unsync)
			need_sync = true;

		if (sp->role.word != role.word)
			continue;

		if (sp->unsync && kvm_sync_page_transient(vcpu, sp))
			break;

		mmu_page_add_parent_pte(vcpu, sp, parent_pte);
		if (sp->unsync_children) {
			kvm_make_request(KVM_REQ_MMU_SYNC, vcpu);
			kvm_mmu_mark_parents_unsync(sp);
		} else if (sp->unsync)
			kvm_mmu_mark_parents_unsync(sp);

		__clear_sp_write_flooding_count(sp);
		trace_kvm_mmu_get_page(sp, false);
		return sp;
	}
	++vcpu->kvm->stat.mmu_cache_miss;
	sp = kvm_mmu_alloc_page(vcpu, parent_pte, direct);
	if (!sp)
		return sp;
	sp->gfn = gfn;
	sp->role = role;
	hlist_add_head(&sp->hash_link,
		&vcpu->kvm->arch.mmu_page_hash[kvm_page_table_hashfn(gfn)]);
	if (!direct) {
		if (rmap_write_protect(vcpu, gfn))
			kvm_flush_remote_tlbs(vcpu->kvm);
		if (level > PT_PAGE_TABLE_LEVEL && need_sync)
			kvm_sync_pages(vcpu, gfn);

		account_shadowed(vcpu->kvm, sp);
	}
	sp->mmu_valid_gen = vcpu->kvm->arch.mmu_valid_gen;
	init_shadow_page_table(sp);
	trace_kvm_mmu_get_page(sp, true);
	return sp;
}

static void shadow_walk_init(struct kvm_shadow_walk_iterator *iterator,
			     struct kvm_vcpu *vcpu, u64 addr)
{
	iterator->addr = addr;
	iterator->shadow_addr = vcpu->arch.mmu.root_hpa;
	iterator->level = vcpu->arch.mmu.shadow_root_level;

	if (iterator->level == PT64_ROOT_LEVEL &&
	    vcpu->arch.mmu.root_level < PT64_ROOT_LEVEL &&
	    !vcpu->arch.mmu.direct_map)
		--iterator->level;

	if (iterator->level == PT32E_ROOT_LEVEL) {
		iterator->shadow_addr
			= vcpu->arch.mmu.pae_root[(addr >> 30) & 3];
		iterator->shadow_addr &= PT64_BASE_ADDR_MASK;
		--iterator->level;
		if (!iterator->shadow_addr)
			iterator->level = 0;
	}
}

static bool shadow_walk_okay(struct kvm_shadow_walk_iterator *iterator)
{
	if (iterator->level < PT_PAGE_TABLE_LEVEL)
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
	return __shadow_walk_next(iterator, *iterator->sptep);
}

static void link_shadow_page(u64 *sptep, struct kvm_mmu_page *sp, bool accessed)
{
	u64 spte;

	BUILD_BUG_ON(VMX_EPT_READABLE_MASK != PT_PRESENT_MASK ||
			VMX_EPT_WRITABLE_MASK != PT_WRITABLE_MASK);

	spte = __pa(sp->spt) | PT_PRESENT_MASK | PT_WRITABLE_MASK |
	       shadow_user_mask | shadow_x_mask;

	if (accessed)
		spte |= shadow_accessed_mask;

	mmu_spte_set(sptep, spte);
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
		child = page_header(*sptep & PT64_BASE_ADDR_MASK);
		if (child->role.access == direct_access)
			return;

		drop_parent_pte(child, sptep);
		kvm_flush_remote_tlbs(vcpu->kvm);
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
			child = page_header(pte & PT64_BASE_ADDR_MASK);
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

static void kvm_mmu_put_page(struct kvm_mmu_page *sp, u64 *parent_pte)
{
	mmu_page_remove_parent_pte(sp, parent_pte);
}

static void kvm_mmu_unlink_parents(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	u64 *sptep;
	struct rmap_iterator iter;

	while ((sptep = rmap_get_first(sp->parent_ptes, &iter)))
		drop_parent_pte(sp, sptep);
}

static int mmu_zap_unsync_children(struct kvm *kvm,
				   struct kvm_mmu_page *parent,
				   struct list_head *invalid_list)
{
	int i, zapped = 0;
	struct mmu_page_path parents;
	struct kvm_mmu_pages pages;

	if (parent->role.level == PT_PAGE_TABLE_LEVEL)
		return 0;

	kvm_mmu_pages_init(parent, &parents, &pages);
	while (mmu_unsync_walk(parent, &pages)) {
		struct kvm_mmu_page *sp;

		for_each_sp(pages, sp, parents, i) {
			kvm_mmu_prepare_zap_page(kvm, sp, invalid_list);
			mmu_pages_clear_parents(&parents);
			zapped++;
		}
		kvm_mmu_pages_init(parent, &parents, &pages);
	}

	return zapped;
}

static int kvm_mmu_prepare_zap_page(struct kvm *kvm, struct kvm_mmu_page *sp,
				    struct list_head *invalid_list)
{
	int ret;

	trace_kvm_mmu_prepare_zap_page(sp);
	++kvm->stat.mmu_shadow_zapped;
	ret = mmu_zap_unsync_children(kvm, sp, invalid_list);
	kvm_mmu_page_unlink_children(kvm, sp);
	kvm_mmu_unlink_parents(kvm, sp);

	if (!sp->role.invalid && !sp->role.direct)
		unaccount_shadowed(kvm, sp);

	if (sp->unsync)
		kvm_unlink_unsync_page(kvm, sp);
	if (!sp->root_count) {
		/* Count self */
		ret++;
		list_move(&sp->link, invalid_list);
		kvm_mod_used_mmu_pages(kvm, -1);
	} else {
		list_move(&sp->link, &kvm->arch.active_mmu_pages);

		/*
		 * The obsolete pages can not be used on any vcpus.
		 * See the comments in kvm_mmu_invalidate_zap_all_pages().
		 */
		if (!sp->role.invalid && !is_obsolete_sp(kvm, sp))
			kvm_reload_remote_mmus(kvm);
	}

	sp->role.invalid = 1;
	return ret;
}

static void kvm_mmu_commit_zap_page(struct kvm *kvm,
				    struct list_head *invalid_list)
{
	struct kvm_mmu_page *sp, *nsp;

	if (list_empty(invalid_list))
		return;

	/*
	 * wmb: make sure everyone sees our modifications to the page tables
	 * rmb: make sure we see changes to vcpu->mode
	 */
	smp_mb();

	/*
	 * Wait for all vcpus to exit guest mode and/or lockless shadow
	 * page table walks.
	 */
	kvm_flush_remote_tlbs(kvm);

	list_for_each_entry_safe(sp, nsp, invalid_list, link) {
		WARN_ON(!sp->role.invalid || sp->root_count);
		kvm_mmu_free_page(sp);
	}
}

static bool prepare_zap_oldest_mmu_page(struct kvm *kvm,
					struct list_head *invalid_list)
{
	struct kvm_mmu_page *sp;

	if (list_empty(&kvm->arch.active_mmu_pages))
		return false;

	sp = list_entry(kvm->arch.active_mmu_pages.prev,
			struct kvm_mmu_page, link);
	kvm_mmu_prepare_zap_page(kvm, sp, invalid_list);

	return true;
}

/*
 * Changing the number of mmu pages allocated to the vm
 * Note: if goal_nr_mmu_pages is too small, you will get dead lock
 */
void kvm_mmu_change_mmu_pages(struct kvm *kvm, unsigned int goal_nr_mmu_pages)
{
	LIST_HEAD(invalid_list);

	spin_lock(&kvm->mmu_lock);

	if (kvm->arch.n_used_mmu_pages > goal_nr_mmu_pages) {
		/* Need to free some mmu pages to achieve the goal. */
		while (kvm->arch.n_used_mmu_pages > goal_nr_mmu_pages)
			if (!prepare_zap_oldest_mmu_page(kvm, &invalid_list))
				break;

		kvm_mmu_commit_zap_page(kvm, &invalid_list);
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

static void __kvm_unsync_page(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp)
{
	trace_kvm_mmu_unsync_page(sp);
	++vcpu->kvm->stat.mmu_unsync;
	sp->unsync = 1;

	kvm_mmu_mark_parents_unsync(sp);
}

static void kvm_unsync_pages(struct kvm_vcpu *vcpu,  gfn_t gfn)
{
	struct kvm_mmu_page *s;

	for_each_gfn_indirect_valid_sp(vcpu->kvm, s, gfn) {
		if (s->unsync)
			continue;
		WARN_ON(s->role.level != PT_PAGE_TABLE_LEVEL);
		__kvm_unsync_page(vcpu, s);
	}
}

static int mmu_need_write_protect(struct kvm_vcpu *vcpu, gfn_t gfn,
				  bool can_unsync)
{
	struct kvm_mmu_page *s;
	bool need_unsync = false;

	for_each_gfn_indirect_valid_sp(vcpu->kvm, s, gfn) {
		if (!can_unsync)
			return 1;

		if (s->role.level != PT_PAGE_TABLE_LEVEL)
			return 1;

		if (!s->unsync)
			need_unsync = true;
	}
	if (need_unsync)
		kvm_unsync_pages(vcpu, gfn);
	return 0;
}

static bool kvm_is_mmio_pfn(pfn_t pfn)
{
	if (pfn_valid(pfn))
		return !is_zero_pfn(pfn) && PageReserved(pfn_to_page(pfn));

	return true;
}

static int set_spte(struct kvm_vcpu *vcpu, u64 *sptep,
		    unsigned pte_access, int level,
		    gfn_t gfn, pfn_t pfn, bool speculative,
		    bool can_unsync, bool host_writable)
{
	u64 spte;
	int ret = 0;

	if (set_mmio_spte(vcpu, sptep, gfn, pfn, pte_access))
		return 0;

	spte = PT_PRESENT_MASK;
	if (!speculative)
		spte |= shadow_accessed_mask;

	if (pte_access & ACC_EXEC_MASK)
		spte |= shadow_x_mask;
	else
		spte |= shadow_nx_mask;

	if (pte_access & ACC_USER_MASK)
		spte |= shadow_user_mask;

	if (level > PT_PAGE_TABLE_LEVEL)
		spte |= PT_PAGE_SIZE_MASK;
	if (tdp_enabled)
		spte |= kvm_x86_ops->get_mt_mask(vcpu, gfn,
			kvm_is_mmio_pfn(pfn));

	if (host_writable)
		spte |= SPTE_HOST_WRITEABLE;
	else
		pte_access &= ~ACC_WRITE_MASK;

	spte |= (u64)pfn << PAGE_SHIFT;

	if (pte_access & ACC_WRITE_MASK) {

		/*
		 * Other vcpu creates new sp in the window between
		 * mapping_level() and acquiring mmu-lock. We can
		 * allow guest to retry the access, the mapping can
		 * be fixed if guest refault.
		 */
		if (level > PT_PAGE_TABLE_LEVEL &&
		    has_wrprotected_page(vcpu, gfn, level))
			goto done;

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
			ret = 1;
			pte_access &= ~ACC_WRITE_MASK;
			spte &= ~(PT_WRITABLE_MASK | SPTE_MMU_WRITEABLE);
		}
	}

	if (pte_access & ACC_WRITE_MASK) {
		kvm_vcpu_mark_page_dirty(vcpu, gfn);
		spte |= shadow_dirty_mask;
	}

set_pte:
	if (mmu_spte_update(sptep, spte))
		kvm_flush_remote_tlbs(vcpu->kvm);
done:
	return ret;
}

static void mmu_set_spte(struct kvm_vcpu *vcpu, u64 *sptep,
			 unsigned pte_access, int write_fault, int *emulate,
			 int level, gfn_t gfn, pfn_t pfn, bool speculative,
			 bool host_writable)
{
	int was_rmapped = 0;
	int rmap_count;

	pgprintk("%s: spte %llx write_fault %d gfn %llx\n", __func__,
		 *sptep, write_fault, gfn);

	if (is_rmap_spte(*sptep)) {
		/*
		 * If we overwrite a PTE page pointer with a 2MB PMD, unlink
		 * the parent of the now unreachable PTE.
		 */
		if (level > PT_PAGE_TABLE_LEVEL &&
		    !is_large_pte(*sptep)) {
			struct kvm_mmu_page *child;
			u64 pte = *sptep;

			child = page_header(pte & PT64_BASE_ADDR_MASK);
			drop_parent_pte(child, sptep);
			kvm_flush_remote_tlbs(vcpu->kvm);
		} else if (pfn != spte_to_pfn(*sptep)) {
			pgprintk("hfn old %llx new %llx\n",
				 spte_to_pfn(*sptep), pfn);
			drop_spte(vcpu->kvm, sptep);
			kvm_flush_remote_tlbs(vcpu->kvm);
		} else
			was_rmapped = 1;
	}

	if (set_spte(vcpu, sptep, pte_access, level, gfn, pfn, speculative,
	      true, host_writable)) {
		if (write_fault)
			*emulate = 1;
		kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	}

	if (unlikely(is_mmio_spte(*sptep) && emulate))
		*emulate = 1;

	pgprintk("%s: setting spte %llx\n", __func__, *sptep);
	pgprintk("instantiating %s PTE (%s) at %llx (%llx) addr %p\n",
		 is_large_pte(*sptep)? "2MB" : "4kB",
		 *sptep & PT_PRESENT_MASK ?"RW":"R", gfn,
		 *sptep, sptep);
	if (!was_rmapped && is_large_pte(*sptep))
		++vcpu->kvm->stat.lpages;

	if (is_shadow_present_pte(*sptep)) {
		if (!was_rmapped) {
			rmap_count = rmap_add(vcpu, sptep, gfn);
			if (rmap_count > RMAP_RECYCLE_THRESHOLD)
				rmap_recycle(vcpu, sptep, gfn);
		}
	}

	kvm_release_pfn_clean(pfn);
}

static pfn_t pte_prefetch_gfn_to_pfn(struct kvm_vcpu *vcpu, gfn_t gfn,
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
	unsigned access = sp->role.access;
	int i, ret;
	gfn_t gfn;

	gfn = kvm_mmu_page_get_gfn(sp, start - sp->spt);
	slot = gfn_to_memslot_dirty_bitmap(vcpu, gfn, access & ACC_WRITE_MASK);
	if (!slot)
		return -1;

	ret = gfn_to_page_many_atomic(slot, gfn, pages, end - start);
	if (ret <= 0)
		return -1;

	for (i = 0; i < ret; i++, gfn++, start++)
		mmu_set_spte(vcpu, start, access, 0, NULL,
			     sp->role.level, gfn, page_to_pfn(pages[i]),
			     true, true);

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

	/*
	 * Since it's no accessed bit on EPT, it's no way to
	 * distinguish between actually accessed translations
	 * and prefetched, so disable pte prefetch if EPT is
	 * enabled.
	 */
	if (!shadow_accessed_mask)
		return;

	sp = page_header(__pa(sptep));
	if (sp->role.level > PT_PAGE_TABLE_LEVEL)
		return;

	__direct_pte_prefetch(vcpu, sp, sptep);
}

static int __direct_map(struct kvm_vcpu *vcpu, gpa_t v, int write,
			int map_writable, int level, gfn_t gfn, pfn_t pfn,
			bool prefault)
{
	struct kvm_shadow_walk_iterator iterator;
	struct kvm_mmu_page *sp;
	int emulate = 0;
	gfn_t pseudo_gfn;

	if (!VALID_PAGE(vcpu->arch.mmu.root_hpa))
		return 0;

	for_each_shadow_entry(vcpu, (u64)gfn << PAGE_SHIFT, iterator) {
		if (iterator.level == level) {
			mmu_set_spte(vcpu, iterator.sptep, ACC_ALL,
				     write, &emulate, level, gfn, pfn,
				     prefault, map_writable);
			direct_pte_prefetch(vcpu, iterator.sptep);
			++vcpu->stat.pf_fixed;
			break;
		}

		drop_large_spte(vcpu, iterator.sptep);
		if (!is_shadow_present_pte(*iterator.sptep)) {
			u64 base_addr = iterator.addr;

			base_addr &= PT64_LVL_ADDR_MASK(iterator.level);
			pseudo_gfn = base_addr >> PAGE_SHIFT;
			sp = kvm_mmu_get_page(vcpu, pseudo_gfn, iterator.addr,
					      iterator.level - 1,
					      1, ACC_ALL, iterator.sptep);

			link_shadow_page(iterator.sptep, sp, true);
		}
	}
	return emulate;
}

static void kvm_send_hwpoison_signal(unsigned long address, struct task_struct *tsk)
{
	siginfo_t info;

	info.si_signo	= SIGBUS;
	info.si_errno	= 0;
	info.si_code	= BUS_MCEERR_AR;
	info.si_addr	= (void __user *)address;
	info.si_addr_lsb = PAGE_SHIFT;

	send_sig_info(SIGBUS, &info, tsk);
}

static int kvm_handle_bad_page(struct kvm_vcpu *vcpu, gfn_t gfn, pfn_t pfn)
{
	/*
	 * Do not cache the mmio info caused by writing the readonly gfn
	 * into the spte otherwise read access on readonly gfn also can
	 * caused mmio page fault and treat it as mmio access.
	 * Return 1 to tell kvm to emulate it.
	 */
	if (pfn == KVM_PFN_ERR_RO_FAULT)
		return 1;

	if (pfn == KVM_PFN_ERR_HWPOISON) {
		kvm_send_hwpoison_signal(kvm_vcpu_gfn_to_hva(vcpu, gfn), current);
		return 0;
	}

	return -EFAULT;
}

static void transparent_hugepage_adjust(struct kvm_vcpu *vcpu,
					gfn_t *gfnp, pfn_t *pfnp, int *levelp)
{
	pfn_t pfn = *pfnp;
	gfn_t gfn = *gfnp;
	int level = *levelp;

	/*
	 * Check if it's a transparent hugepage. If this would be an
	 * hugetlbfs page, level wouldn't be set to
	 * PT_PAGE_TABLE_LEVEL and there would be no adjustment done
	 * here.
	 */
	if (!is_error_noslot_pfn(pfn) && !kvm_is_reserved_pfn(pfn) &&
	    level == PT_PAGE_TABLE_LEVEL &&
	    PageTransCompound(pfn_to_page(pfn)) &&
	    !has_wrprotected_page(vcpu, gfn, PT_DIRECTORY_LEVEL)) {
		unsigned long mask;
		/*
		 * mmu_notifier_retry was successful and we hold the
		 * mmu_lock here, so the pmd can't become splitting
		 * from under us, and in turn
		 * __split_huge_page_refcount() can't run from under
		 * us and we can safely transfer the refcount from
		 * PG_tail to PG_head as we switch the pfn to tail to
		 * head.
		 */
		*levelp = level = PT_DIRECTORY_LEVEL;
		mask = KVM_PAGES_PER_HPAGE(level) - 1;
		VM_BUG_ON((gfn & mask) != (pfn & mask));
		if (pfn & mask) {
			gfn &= ~mask;
			*gfnp = gfn;
			kvm_release_pfn_clean(pfn);
			pfn &= ~mask;
			kvm_get_pfn(pfn);
			*pfnp = pfn;
		}
	}
}

static bool handle_abnormal_pfn(struct kvm_vcpu *vcpu, gva_t gva, gfn_t gfn,
				pfn_t pfn, unsigned access, int *ret_val)
{
	bool ret = true;

	/* The pfn is invalid, report the error! */
	if (unlikely(is_error_pfn(pfn))) {
		*ret_val = kvm_handle_bad_page(vcpu, gfn, pfn);
		goto exit;
	}

	if (unlikely(is_noslot_pfn(pfn)))
		vcpu_cache_mmio_info(vcpu, gva, gfn, access);

	ret = false;
exit:
	return ret;
}

static bool page_fault_can_be_fast(u32 error_code)
{
	/*
	 * Do not fix the mmio spte with invalid generation number which
	 * need to be updated by slow page fault path.
	 */
	if (unlikely(error_code & PFERR_RSVD_MASK))
		return false;

	/*
	 * #PF can be fast only if the shadow page table is present and it
	 * is caused by write-protect, that means we just need change the
	 * W bit of the spte which can be done out of mmu-lock.
	 */
	if (!(error_code & PFERR_PRESENT_MASK) ||
	      !(error_code & PFERR_WRITE_MASK))
		return false;

	return true;
}

static bool
fast_pf_fix_direct_spte(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			u64 *sptep, u64 spte)
{
	gfn_t gfn;

	WARN_ON(!sp->role.direct);

	/*
	 * The gfn of direct spte is stable since it is calculated
	 * by sp->gfn.
	 */
	gfn = kvm_mmu_page_get_gfn(sp, sptep - sp->spt);

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
	if (cmpxchg64(sptep, spte, spte | PT_WRITABLE_MASK) == spte)
		kvm_vcpu_mark_page_dirty(vcpu, gfn);

	return true;
}

/*
 * Return value:
 * - true: let the vcpu to access on the same address again.
 * - false: let the real page fault path to fix it.
 */
static bool fast_page_fault(struct kvm_vcpu *vcpu, gva_t gva, int level,
			    u32 error_code)
{
	struct kvm_shadow_walk_iterator iterator;
	struct kvm_mmu_page *sp;
	bool ret = false;
	u64 spte = 0ull;

	if (!VALID_PAGE(vcpu->arch.mmu.root_hpa))
		return false;

	if (!page_fault_can_be_fast(error_code))
		return false;

	walk_shadow_page_lockless_begin(vcpu);
	for_each_shadow_entry_lockless(vcpu, gva, iterator, spte)
		if (!is_shadow_present_pte(spte) || iterator.level < level)
			break;

	/*
	 * If the mapping has been changed, let the vcpu fault on the
	 * same address again.
	 */
	if (!is_rmap_spte(spte)) {
		ret = true;
		goto exit;
	}

	sp = page_header(__pa(iterator.sptep));
	if (!is_last_spte(spte, sp->role.level))
		goto exit;

	/*
	 * Check if it is a spurious fault caused by TLB lazily flushed.
	 *
	 * Need not check the access of upper level table entries since
	 * they are always ACC_ALL.
	 */
	 if (is_writable_pte(spte)) {
		ret = true;
		goto exit;
	}

	/*
	 * Currently, to simplify the code, only the spte write-protected
	 * by dirty-log can be fast fixed.
	 */
	if (!spte_is_locklessly_modifiable(spte))
		goto exit;

	/*
	 * Do not fix write-permission on the large spte since we only dirty
	 * the first page into the dirty-bitmap in fast_pf_fix_direct_spte()
	 * that means other pages are missed if its slot is dirty-logged.
	 *
	 * Instead, we let the slow page fault path create a normal spte to
	 * fix the access.
	 *
	 * See the comments in kvm_arch_commit_memory_region().
	 */
	if (sp->role.level > PT_PAGE_TABLE_LEVEL)
		goto exit;

	/*
	 * Currently, fast page fault only works for direct mapping since
	 * the gfn is not stable for indirect shadow page.
	 * See Documentation/virtual/kvm/locking.txt to get more detail.
	 */
	ret = fast_pf_fix_direct_spte(vcpu, sp, iterator.sptep, spte);
exit:
	trace_fast_page_fault(vcpu, gva, error_code, iterator.sptep,
			      spte, ret);
	walk_shadow_page_lockless_end(vcpu);

	return ret;
}

static bool try_async_pf(struct kvm_vcpu *vcpu, bool prefault, gfn_t gfn,
			 gva_t gva, pfn_t *pfn, bool write, bool *writable);
static void make_mmu_pages_available(struct kvm_vcpu *vcpu);

static int nonpaging_map(struct kvm_vcpu *vcpu, gva_t v, u32 error_code,
			 gfn_t gfn, bool prefault)
{
	int r;
	int level;
	int force_pt_level;
	pfn_t pfn;
	unsigned long mmu_seq;
	bool map_writable, write = error_code & PFERR_WRITE_MASK;

	force_pt_level = mapping_level_dirty_bitmap(vcpu, gfn);
	if (likely(!force_pt_level)) {
		level = mapping_level(vcpu, gfn);
		/*
		 * This path builds a PAE pagetable - so we can map
		 * 2mb pages at maximum. Therefore check if the level
		 * is larger than that.
		 */
		if (level > PT_DIRECTORY_LEVEL)
			level = PT_DIRECTORY_LEVEL;

		gfn &= ~(KVM_PAGES_PER_HPAGE(level) - 1);
	} else
		level = PT_PAGE_TABLE_LEVEL;

	if (fast_page_fault(vcpu, v, level, error_code))
		return 0;

	mmu_seq = vcpu->kvm->mmu_notifier_seq;
	smp_rmb();

	if (try_async_pf(vcpu, prefault, gfn, v, &pfn, write, &map_writable))
		return 0;

	if (handle_abnormal_pfn(vcpu, v, gfn, pfn, ACC_ALL, &r))
		return r;

	spin_lock(&vcpu->kvm->mmu_lock);
	if (mmu_notifier_retry(vcpu->kvm, mmu_seq))
		goto out_unlock;
	make_mmu_pages_available(vcpu);
	if (likely(!force_pt_level))
		transparent_hugepage_adjust(vcpu, &gfn, &pfn, &level);
	r = __direct_map(vcpu, v, write, map_writable, level, gfn, pfn,
			 prefault);
	spin_unlock(&vcpu->kvm->mmu_lock);


	return r;

out_unlock:
	spin_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(pfn);
	return 0;
}


static void mmu_free_roots(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_mmu_page *sp;
	LIST_HEAD(invalid_list);

	if (!VALID_PAGE(vcpu->arch.mmu.root_hpa))
		return;

	if (vcpu->arch.mmu.shadow_root_level == PT64_ROOT_LEVEL &&
	    (vcpu->arch.mmu.root_level == PT64_ROOT_LEVEL ||
	     vcpu->arch.mmu.direct_map)) {
		hpa_t root = vcpu->arch.mmu.root_hpa;

		spin_lock(&vcpu->kvm->mmu_lock);
		sp = page_header(root);
		--sp->root_count;
		if (!sp->root_count && sp->role.invalid) {
			kvm_mmu_prepare_zap_page(vcpu->kvm, sp, &invalid_list);
			kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
		}
		spin_unlock(&vcpu->kvm->mmu_lock);
		vcpu->arch.mmu.root_hpa = INVALID_PAGE;
		return;
	}

	spin_lock(&vcpu->kvm->mmu_lock);
	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu.pae_root[i];

		if (root) {
			root &= PT64_BASE_ADDR_MASK;
			sp = page_header(root);
			--sp->root_count;
			if (!sp->root_count && sp->role.invalid)
				kvm_mmu_prepare_zap_page(vcpu->kvm, sp,
							 &invalid_list);
		}
		vcpu->arch.mmu.pae_root[i] = INVALID_PAGE;
	}
	kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
	spin_unlock(&vcpu->kvm->mmu_lock);
	vcpu->arch.mmu.root_hpa = INVALID_PAGE;
}

static int mmu_check_root(struct kvm_vcpu *vcpu, gfn_t root_gfn)
{
	int ret = 0;

	if (!kvm_is_visible_gfn(vcpu->kvm, root_gfn)) {
		kvm_make_request(KVM_REQ_TRIPLE_FAULT, vcpu);
		ret = 1;
	}

	return ret;
}

static int mmu_alloc_direct_roots(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *sp;
	unsigned i;

	if (vcpu->arch.mmu.shadow_root_level == PT64_ROOT_LEVEL) {
		spin_lock(&vcpu->kvm->mmu_lock);
		make_mmu_pages_available(vcpu);
		sp = kvm_mmu_get_page(vcpu, 0, 0, PT64_ROOT_LEVEL,
				      1, ACC_ALL, NULL);
		++sp->root_count;
		spin_unlock(&vcpu->kvm->mmu_lock);
		vcpu->arch.mmu.root_hpa = __pa(sp->spt);
	} else if (vcpu->arch.mmu.shadow_root_level == PT32E_ROOT_LEVEL) {
		for (i = 0; i < 4; ++i) {
			hpa_t root = vcpu->arch.mmu.pae_root[i];

			MMU_WARN_ON(VALID_PAGE(root));
			spin_lock(&vcpu->kvm->mmu_lock);
			make_mmu_pages_available(vcpu);
			sp = kvm_mmu_get_page(vcpu, i << (30 - PAGE_SHIFT),
					      i << 30,
					      PT32_ROOT_LEVEL, 1, ACC_ALL,
					      NULL);
			root = __pa(sp->spt);
			++sp->root_count;
			spin_unlock(&vcpu->kvm->mmu_lock);
			vcpu->arch.mmu.pae_root[i] = root | PT_PRESENT_MASK;
		}
		vcpu->arch.mmu.root_hpa = __pa(vcpu->arch.mmu.pae_root);
	} else
		BUG();

	return 0;
}

static int mmu_alloc_shadow_roots(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *sp;
	u64 pdptr, pm_mask;
	gfn_t root_gfn;
	int i;

	root_gfn = vcpu->arch.mmu.get_cr3(vcpu) >> PAGE_SHIFT;

	if (mmu_check_root(vcpu, root_gfn))
		return 1;

	/*
	 * Do we shadow a long mode page table? If so we need to
	 * write-protect the guests page table root.
	 */
	if (vcpu->arch.mmu.root_level == PT64_ROOT_LEVEL) {
		hpa_t root = vcpu->arch.mmu.root_hpa;

		MMU_WARN_ON(VALID_PAGE(root));

		spin_lock(&vcpu->kvm->mmu_lock);
		make_mmu_pages_available(vcpu);
		sp = kvm_mmu_get_page(vcpu, root_gfn, 0, PT64_ROOT_LEVEL,
				      0, ACC_ALL, NULL);
		root = __pa(sp->spt);
		++sp->root_count;
		spin_unlock(&vcpu->kvm->mmu_lock);
		vcpu->arch.mmu.root_hpa = root;
		return 0;
	}

	/*
	 * We shadow a 32 bit page table. This may be a legacy 2-level
	 * or a PAE 3-level page table. In either case we need to be aware that
	 * the shadow page table may be a PAE or a long mode page table.
	 */
	pm_mask = PT_PRESENT_MASK;
	if (vcpu->arch.mmu.shadow_root_level == PT64_ROOT_LEVEL)
		pm_mask |= PT_ACCESSED_MASK | PT_WRITABLE_MASK | PT_USER_MASK;

	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu.pae_root[i];

		MMU_WARN_ON(VALID_PAGE(root));
		if (vcpu->arch.mmu.root_level == PT32E_ROOT_LEVEL) {
			pdptr = vcpu->arch.mmu.get_pdptr(vcpu, i);
			if (!is_present_gpte(pdptr)) {
				vcpu->arch.mmu.pae_root[i] = 0;
				continue;
			}
			root_gfn = pdptr >> PAGE_SHIFT;
			if (mmu_check_root(vcpu, root_gfn))
				return 1;
		}
		spin_lock(&vcpu->kvm->mmu_lock);
		make_mmu_pages_available(vcpu);
		sp = kvm_mmu_get_page(vcpu, root_gfn, i << 30,
				      PT32_ROOT_LEVEL, 0,
				      ACC_ALL, NULL);
		root = __pa(sp->spt);
		++sp->root_count;
		spin_unlock(&vcpu->kvm->mmu_lock);

		vcpu->arch.mmu.pae_root[i] = root | pm_mask;
	}
	vcpu->arch.mmu.root_hpa = __pa(vcpu->arch.mmu.pae_root);

	/*
	 * If we shadow a 32 bit page table with a long mode page
	 * table we enter this path.
	 */
	if (vcpu->arch.mmu.shadow_root_level == PT64_ROOT_LEVEL) {
		if (vcpu->arch.mmu.lm_root == NULL) {
			/*
			 * The additional page necessary for this is only
			 * allocated on demand.
			 */

			u64 *lm_root;

			lm_root = (void*)get_zeroed_page(GFP_KERNEL);
			if (lm_root == NULL)
				return 1;

			lm_root[0] = __pa(vcpu->arch.mmu.pae_root) | pm_mask;

			vcpu->arch.mmu.lm_root = lm_root;
		}

		vcpu->arch.mmu.root_hpa = __pa(vcpu->arch.mmu.lm_root);
	}

	return 0;
}

static int mmu_alloc_roots(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.mmu.direct_map)
		return mmu_alloc_direct_roots(vcpu);
	else
		return mmu_alloc_shadow_roots(vcpu);
}

static void mmu_sync_roots(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_mmu_page *sp;

	if (vcpu->arch.mmu.direct_map)
		return;

	if (!VALID_PAGE(vcpu->arch.mmu.root_hpa))
		return;

	vcpu_clear_mmio_info(vcpu, MMIO_GVA_ANY);
	kvm_mmu_audit(vcpu, AUDIT_PRE_SYNC);
	if (vcpu->arch.mmu.root_level == PT64_ROOT_LEVEL) {
		hpa_t root = vcpu->arch.mmu.root_hpa;
		sp = page_header(root);
		mmu_sync_children(vcpu, sp);
		kvm_mmu_audit(vcpu, AUDIT_POST_SYNC);
		return;
	}
	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu.pae_root[i];

		if (root && VALID_PAGE(root)) {
			root &= PT64_BASE_ADDR_MASK;
			sp = page_header(root);
			mmu_sync_children(vcpu, sp);
		}
	}
	kvm_mmu_audit(vcpu, AUDIT_POST_SYNC);
}

void kvm_mmu_sync_roots(struct kvm_vcpu *vcpu)
{
	spin_lock(&vcpu->kvm->mmu_lock);
	mmu_sync_roots(vcpu);
	spin_unlock(&vcpu->kvm->mmu_lock);
}
EXPORT_SYMBOL_GPL(kvm_mmu_sync_roots);

static gpa_t nonpaging_gva_to_gpa(struct kvm_vcpu *vcpu, gva_t vaddr,
				  u32 access, struct x86_exception *exception)
{
	if (exception)
		exception->error_code = 0;
	return vaddr;
}

static gpa_t nonpaging_gva_to_gpa_nested(struct kvm_vcpu *vcpu, gva_t vaddr,
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
	int bit7 = (pte >> 7) & 1, low6 = pte & 0x3f;

	return (pte & rsvd_check->rsvd_bits_mask[bit7][level-1]) |
		((rsvd_check->bad_mt_xwr & (1ull << low6)) != 0);
}

static bool is_rsvd_bits_set(struct kvm_mmu *mmu, u64 gpte, int level)
{
	return __is_rsvd_bits_set(&mmu->guest_rsvd_check, gpte, level);
}

static bool is_shadow_zero_bits_set(struct kvm_mmu *mmu, u64 spte, int level)
{
	return __is_rsvd_bits_set(&mmu->shadow_zero_check, spte, level);
}

static bool quickly_check_mmio_pf(struct kvm_vcpu *vcpu, u64 addr, bool direct)
{
	if (direct)
		return vcpu_match_mmio_gpa(vcpu, addr);

	return vcpu_match_mmio_gva(vcpu, addr);
}

/* return true if reserved bit is detected on spte. */
static bool
walk_shadow_page_get_mmio_spte(struct kvm_vcpu *vcpu, u64 addr, u64 *sptep)
{
	struct kvm_shadow_walk_iterator iterator;
	u64 sptes[PT64_ROOT_LEVEL], spte = 0ull;
	int root, leaf;
	bool reserved = false;

	if (!VALID_PAGE(vcpu->arch.mmu.root_hpa))
		goto exit;

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

		reserved |= is_shadow_zero_bits_set(&vcpu->arch.mmu, spte,
						    iterator.level);
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
exit:
	*sptep = spte;
	return reserved;
}

int handle_mmio_page_fault_common(struct kvm_vcpu *vcpu, u64 addr, bool direct)
{
	u64 spte;
	bool reserved;

	if (quickly_check_mmio_pf(vcpu, addr, direct))
		return RET_MMIO_PF_EMULATE;

	reserved = walk_shadow_page_get_mmio_spte(vcpu, addr, &spte);
	if (unlikely(reserved))
		return RET_MMIO_PF_BUG;

	if (is_mmio_spte(spte)) {
		gfn_t gfn = get_mmio_spte_gfn(spte);
		unsigned access = get_mmio_spte_access(spte);

		if (!check_mmio_spte(vcpu, spte))
			return RET_MMIO_PF_INVALID;

		if (direct)
			addr = 0;

		trace_handle_mmio_page_fault(addr, gfn, access);
		vcpu_cache_mmio_info(vcpu, addr, gfn, access);
		return RET_MMIO_PF_EMULATE;
	}

	/*
	 * If the page table is zapped by other cpus, let CPU fault again on
	 * the address.
	 */
	return RET_MMIO_PF_RETRY;
}
EXPORT_SYMBOL_GPL(handle_mmio_page_fault_common);

static int handle_mmio_page_fault(struct kvm_vcpu *vcpu, u64 addr,
				  u32 error_code, bool direct)
{
	int ret;

	ret = handle_mmio_page_fault_common(vcpu, addr, direct);
	WARN_ON(ret == RET_MMIO_PF_BUG);
	return ret;
}

static int nonpaging_page_fault(struct kvm_vcpu *vcpu, gva_t gva,
				u32 error_code, bool prefault)
{
	gfn_t gfn;
	int r;

	pgprintk("%s: gva %lx error %x\n", __func__, gva, error_code);

	if (unlikely(error_code & PFERR_RSVD_MASK)) {
		r = handle_mmio_page_fault(vcpu, gva, error_code, true);

		if (likely(r != RET_MMIO_PF_INVALID))
			return r;
	}

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	MMU_WARN_ON(!VALID_PAGE(vcpu->arch.mmu.root_hpa));

	gfn = gva >> PAGE_SHIFT;

	return nonpaging_map(vcpu, gva & PAGE_MASK,
			     error_code, gfn, prefault);
}

static int kvm_arch_setup_async_pf(struct kvm_vcpu *vcpu, gva_t gva, gfn_t gfn)
{
	struct kvm_arch_async_pf arch;

	arch.token = (vcpu->arch.apf.id++ << 12) | vcpu->vcpu_id;
	arch.gfn = gfn;
	arch.direct_map = vcpu->arch.mmu.direct_map;
	arch.cr3 = vcpu->arch.mmu.get_cr3(vcpu);

	return kvm_setup_async_pf(vcpu, gva, kvm_vcpu_gfn_to_hva(vcpu, gfn), &arch);
}

static bool can_do_async_pf(struct kvm_vcpu *vcpu)
{
	if (unlikely(!irqchip_in_kernel(vcpu->kvm) ||
		     kvm_event_needs_reinjection(vcpu)))
		return false;

	return kvm_x86_ops->interrupt_allowed(vcpu);
}

static bool try_async_pf(struct kvm_vcpu *vcpu, bool prefault, gfn_t gfn,
			 gva_t gva, pfn_t *pfn, bool write, bool *writable)
{
	struct kvm_memory_slot *slot;
	bool async;

	slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	async = false;
	*pfn = __gfn_to_pfn_memslot(slot, gfn, false, &async, write, writable);
	if (!async)
		return false; /* *pfn has correct page already */

	if (!prefault && can_do_async_pf(vcpu)) {
		trace_kvm_try_async_get_page(gva, gfn);
		if (kvm_find_async_pf_gfn(vcpu, gfn)) {
			trace_kvm_async_pf_doublefault(gva, gfn);
			kvm_make_request(KVM_REQ_APF_HALT, vcpu);
			return true;
		} else if (kvm_arch_setup_async_pf(vcpu, gva, gfn))
			return true;
	}

	*pfn = __gfn_to_pfn_memslot(slot, gfn, false, NULL, write, writable);
	return false;
}

static bool
check_hugepage_cache_consistency(struct kvm_vcpu *vcpu, gfn_t gfn, int level)
{
	int page_num = KVM_PAGES_PER_HPAGE(level);

	gfn &= ~(page_num - 1);

	return kvm_mtrr_check_gfn_range_consistency(vcpu, gfn, page_num);
}

static int tdp_page_fault(struct kvm_vcpu *vcpu, gva_t gpa, u32 error_code,
			  bool prefault)
{
	pfn_t pfn;
	int r;
	int level;
	int force_pt_level;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	unsigned long mmu_seq;
	int write = error_code & PFERR_WRITE_MASK;
	bool map_writable;

	MMU_WARN_ON(!VALID_PAGE(vcpu->arch.mmu.root_hpa));

	if (unlikely(error_code & PFERR_RSVD_MASK)) {
		r = handle_mmio_page_fault(vcpu, gpa, error_code, true);

		if (likely(r != RET_MMIO_PF_INVALID))
			return r;
	}

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	if (mapping_level_dirty_bitmap(vcpu, gfn) ||
	    !check_hugepage_cache_consistency(vcpu, gfn, PT_DIRECTORY_LEVEL))
		force_pt_level = 1;
	else
		force_pt_level = 0;

	if (likely(!force_pt_level)) {
		level = mapping_level(vcpu, gfn);
		if (level > PT_DIRECTORY_LEVEL &&
		    !check_hugepage_cache_consistency(vcpu, gfn, level))
			level = PT_DIRECTORY_LEVEL;
		gfn &= ~(KVM_PAGES_PER_HPAGE(level) - 1);
	} else
		level = PT_PAGE_TABLE_LEVEL;

	if (fast_page_fault(vcpu, gpa, level, error_code))
		return 0;

	mmu_seq = vcpu->kvm->mmu_notifier_seq;
	smp_rmb();

	if (try_async_pf(vcpu, prefault, gfn, gpa, &pfn, write, &map_writable))
		return 0;

	if (handle_abnormal_pfn(vcpu, 0, gfn, pfn, ACC_ALL, &r))
		return r;

	spin_lock(&vcpu->kvm->mmu_lock);
	if (mmu_notifier_retry(vcpu->kvm, mmu_seq))
		goto out_unlock;
	make_mmu_pages_available(vcpu);
	if (likely(!force_pt_level))
		transparent_hugepage_adjust(vcpu, &gfn, &pfn, &level);
	r = __direct_map(vcpu, gpa, write, map_writable,
			 level, gfn, pfn, prefault);
	spin_unlock(&vcpu->kvm->mmu_lock);

	return r;

out_unlock:
	spin_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(pfn);
	return 0;
}

static void nonpaging_init_context(struct kvm_vcpu *vcpu,
				   struct kvm_mmu *context)
{
	context->page_fault = nonpaging_page_fault;
	context->gva_to_gpa = nonpaging_gva_to_gpa;
	context->sync_page = nonpaging_sync_page;
	context->invlpg = nonpaging_invlpg;
	context->update_pte = nonpaging_update_pte;
	context->root_level = 0;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->root_hpa = INVALID_PAGE;
	context->direct_map = true;
	context->nx = false;
}

void kvm_mmu_new_cr3(struct kvm_vcpu *vcpu)
{
	mmu_free_roots(vcpu);
}

static unsigned long get_cr3(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr3(vcpu);
}

static void inject_page_fault(struct kvm_vcpu *vcpu,
			      struct x86_exception *fault)
{
	vcpu->arch.mmu.inject_page_fault(vcpu, fault);
}

static bool sync_mmio_spte(struct kvm_vcpu *vcpu, u64 *sptep, gfn_t gfn,
			   unsigned access, int *nr_present)
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

static inline bool is_last_gpte(struct kvm_mmu *mmu, unsigned level, unsigned gpte)
{
	unsigned index;

	index = level - 1;
	index |= (gpte & PT_PAGE_SIZE_MASK) >> (PT_PAGE_SIZE_SHIFT - 2);
	return mmu->last_pte_bitmap & (1 << index);
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
	case PT64_ROOT_LEVEL:
		rsvd_check->rsvd_bits_mask[0][3] = exb_bit_rsvd |
			nonleaf_bit8_rsvd | rsvd_bits(7, 7) |
			rsvd_bits(maxphyaddr, 51);
		rsvd_check->rsvd_bits_mask[0][2] = exb_bit_rsvd |
			nonleaf_bit8_rsvd | gbpages_bit_rsvd |
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
				context->nx, guest_cpuid_has_gbpages(vcpu),
				is_pse(vcpu), guest_cpuid_is_amd(vcpu));
}

static void
__reset_rsvds_bits_mask_ept(struct rsvd_bits_validate *rsvd_check,
			    int maxphyaddr, bool execonly)
{
	int pte;

	rsvd_check->rsvd_bits_mask[0][3] =
		rsvd_bits(maxphyaddr, 51) | rsvd_bits(3, 7);
	rsvd_check->rsvd_bits_mask[0][2] =
		rsvd_bits(maxphyaddr, 51) | rsvd_bits(3, 6);
	rsvd_check->rsvd_bits_mask[0][1] =
		rsvd_bits(maxphyaddr, 51) | rsvd_bits(3, 6);
	rsvd_check->rsvd_bits_mask[0][0] = rsvd_bits(maxphyaddr, 51);

	/* large page */
	rsvd_check->rsvd_bits_mask[1][3] = rsvd_check->rsvd_bits_mask[0][3];
	rsvd_check->rsvd_bits_mask[1][2] =
		rsvd_bits(maxphyaddr, 51) | rsvd_bits(12, 29);
	rsvd_check->rsvd_bits_mask[1][1] =
		rsvd_bits(maxphyaddr, 51) | rsvd_bits(12, 20);
	rsvd_check->rsvd_bits_mask[1][0] = rsvd_check->rsvd_bits_mask[0][0];

	for (pte = 0; pte < 64; pte++) {
		int rwx_bits = pte & 7;
		int mt = pte >> 3;
		if (mt == 0x2 || mt == 0x3 || mt == 0x7 ||
				rwx_bits == 0x2 || rwx_bits == 0x6 ||
				(rwx_bits == 0x4 && !execonly))
			rsvd_check->bad_mt_xwr |= (1ull << pte);
	}
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
	/*
	 * Passing "true" to the last argument is okay; it adds a check
	 * on bit 8 of the SPTEs which KVM doesn't use anyway.
	 */
	__reset_rsvds_bits_mask(vcpu, &context->shadow_zero_check,
				boot_cpu_data.x86_phys_bits,
				context->shadow_root_level, context->nx,
				guest_cpuid_has_gbpages(vcpu), is_pse(vcpu),
				true);
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
	if (boot_cpu_is_amd())
		__reset_rsvds_bits_mask(vcpu, &context->shadow_zero_check,
					boot_cpu_data.x86_phys_bits,
					context->shadow_root_level, false,
					cpu_has_gbpages, true, true);
	else
		__reset_rsvds_bits_mask_ept(&context->shadow_zero_check,
					    boot_cpu_data.x86_phys_bits,
					    false);

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
				    boot_cpu_data.x86_phys_bits, execonly);
}

static void update_permission_bitmask(struct kvm_vcpu *vcpu,
				      struct kvm_mmu *mmu, bool ept)
{
	unsigned bit, byte, pfec;
	u8 map;
	bool fault, x, w, u, wf, uf, ff, smapf, cr4_smap, cr4_smep, smap = 0;

	cr4_smep = kvm_read_cr4_bits(vcpu, X86_CR4_SMEP);
	cr4_smap = kvm_read_cr4_bits(vcpu, X86_CR4_SMAP);
	for (byte = 0; byte < ARRAY_SIZE(mmu->permissions); ++byte) {
		pfec = byte << 1;
		map = 0;
		wf = pfec & PFERR_WRITE_MASK;
		uf = pfec & PFERR_USER_MASK;
		ff = pfec & PFERR_FETCH_MASK;
		/*
		 * PFERR_RSVD_MASK bit is set in PFEC if the access is not
		 * subject to SMAP restrictions, and cleared otherwise. The
		 * bit is only meaningful if the SMAP bit is set in CR4.
		 */
		smapf = !(pfec & PFERR_RSVD_MASK);
		for (bit = 0; bit < 8; ++bit) {
			x = bit & ACC_EXEC_MASK;
			w = bit & ACC_WRITE_MASK;
			u = bit & ACC_USER_MASK;

			if (!ept) {
				/* Not really needed: !nx will cause pte.nx to fault */
				x |= !mmu->nx;
				/* Allow supervisor writes if !cr0.wp */
				w |= !is_write_protection(vcpu) && !uf;
				/* Disallow supervisor fetches of user code if cr4.smep */
				x &= !(cr4_smep && u && !uf);

				/*
				 * SMAP:kernel-mode data accesses from user-mode
				 * mappings should fault. A fault is considered
				 * as a SMAP violation if all of the following
				 * conditions are ture:
				 *   - X86_CR4_SMAP is set in CR4
				 *   - An user page is accessed
				 *   - Page fault in kernel mode
				 *   - if CPL = 3 or X86_EFLAGS_AC is clear
				 *
				 *   Here, we cover the first three conditions.
				 *   The fourth is computed dynamically in
				 *   permission_fault() and is in smapf.
				 *
				 *   Also, SMAP does not affect instruction
				 *   fetches, add the !ff check here to make it
				 *   clearer.
				 */
				smap = cr4_smap && u && !uf && !ff;
			} else
				/* Not really needed: no U/S accesses on ept  */
				u = 1;

			fault = (ff && !x) || (uf && !u) || (wf && !w) ||
				(smapf && smap);
			map |= fault << bit;
		}
		mmu->permissions[byte] = map;
	}
}

static void update_last_pte_bitmap(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu)
{
	u8 map;
	unsigned level, root_level = mmu->root_level;
	const unsigned ps_set_index = 1 << 2;  /* bit 2 of index: ps */

	if (root_level == PT32E_ROOT_LEVEL)
		--root_level;
	/* PT_PAGE_TABLE_LEVEL always terminates */
	map = 1 | (1 << ps_set_index);
	for (level = PT_DIRECTORY_LEVEL; level <= root_level; ++level) {
		if (level <= PT_PDPE_LEVEL
		    && (mmu->root_level >= PT32E_ROOT_LEVEL || is_pse(vcpu)))
			map |= 1 << (ps_set_index | (level - 1));
	}
	mmu->last_pte_bitmap = map;
}

static void paging64_init_context_common(struct kvm_vcpu *vcpu,
					 struct kvm_mmu *context,
					 int level)
{
	context->nx = is_nx(vcpu);
	context->root_level = level;

	reset_rsvds_bits_mask(vcpu, context);
	update_permission_bitmask(vcpu, context, false);
	update_last_pte_bitmap(vcpu, context);

	MMU_WARN_ON(!is_pae(vcpu));
	context->page_fault = paging64_page_fault;
	context->gva_to_gpa = paging64_gva_to_gpa;
	context->sync_page = paging64_sync_page;
	context->invlpg = paging64_invlpg;
	context->update_pte = paging64_update_pte;
	context->shadow_root_level = level;
	context->root_hpa = INVALID_PAGE;
	context->direct_map = false;
}

static void paging64_init_context(struct kvm_vcpu *vcpu,
				  struct kvm_mmu *context)
{
	paging64_init_context_common(vcpu, context, PT64_ROOT_LEVEL);
}

static void paging32_init_context(struct kvm_vcpu *vcpu,
				  struct kvm_mmu *context)
{
	context->nx = false;
	context->root_level = PT32_ROOT_LEVEL;

	reset_rsvds_bits_mask(vcpu, context);
	update_permission_bitmask(vcpu, context, false);
	update_last_pte_bitmap(vcpu, context);

	context->page_fault = paging32_page_fault;
	context->gva_to_gpa = paging32_gva_to_gpa;
	context->sync_page = paging32_sync_page;
	context->invlpg = paging32_invlpg;
	context->update_pte = paging32_update_pte;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->root_hpa = INVALID_PAGE;
	context->direct_map = false;
}

static void paging32E_init_context(struct kvm_vcpu *vcpu,
				   struct kvm_mmu *context)
{
	paging64_init_context_common(vcpu, context, PT32E_ROOT_LEVEL);
}

static void init_kvm_tdp_mmu(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->arch.mmu;

	context->base_role.word = 0;
	context->base_role.smm = is_smm(vcpu);
	context->page_fault = tdp_page_fault;
	context->sync_page = nonpaging_sync_page;
	context->invlpg = nonpaging_invlpg;
	context->update_pte = nonpaging_update_pte;
	context->shadow_root_level = kvm_x86_ops->get_tdp_level();
	context->root_hpa = INVALID_PAGE;
	context->direct_map = true;
	context->set_cr3 = kvm_x86_ops->set_tdp_cr3;
	context->get_cr3 = get_cr3;
	context->get_pdptr = kvm_pdptr_read;
	context->inject_page_fault = kvm_inject_page_fault;

	if (!is_paging(vcpu)) {
		context->nx = false;
		context->gva_to_gpa = nonpaging_gva_to_gpa;
		context->root_level = 0;
	} else if (is_long_mode(vcpu)) {
		context->nx = is_nx(vcpu);
		context->root_level = PT64_ROOT_LEVEL;
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
	update_last_pte_bitmap(vcpu, context);
	reset_tdp_shadow_zero_bits_mask(vcpu, context);
}

void kvm_init_shadow_mmu(struct kvm_vcpu *vcpu)
{
	bool smep = kvm_read_cr4_bits(vcpu, X86_CR4_SMEP);
	bool smap = kvm_read_cr4_bits(vcpu, X86_CR4_SMAP);
	struct kvm_mmu *context = &vcpu->arch.mmu;

	MMU_WARN_ON(VALID_PAGE(context->root_hpa));

	if (!is_paging(vcpu))
		nonpaging_init_context(vcpu, context);
	else if (is_long_mode(vcpu))
		paging64_init_context(vcpu, context);
	else if (is_pae(vcpu))
		paging32E_init_context(vcpu, context);
	else
		paging32_init_context(vcpu, context);

	context->base_role.nxe = is_nx(vcpu);
	context->base_role.cr4_pae = !!is_pae(vcpu);
	context->base_role.cr0_wp  = is_write_protection(vcpu);
	context->base_role.smep_andnot_wp
		= smep && !is_write_protection(vcpu);
	context->base_role.smap_andnot_wp
		= smap && !is_write_protection(vcpu);
	context->base_role.smm = is_smm(vcpu);
	reset_shadow_zero_bits_mask(vcpu, context);
}
EXPORT_SYMBOL_GPL(kvm_init_shadow_mmu);

void kvm_init_shadow_ept_mmu(struct kvm_vcpu *vcpu, bool execonly)
{
	struct kvm_mmu *context = &vcpu->arch.mmu;

	MMU_WARN_ON(VALID_PAGE(context->root_hpa));

	context->shadow_root_level = kvm_x86_ops->get_tdp_level();

	context->nx = true;
	context->page_fault = ept_page_fault;
	context->gva_to_gpa = ept_gva_to_gpa;
	context->sync_page = ept_sync_page;
	context->invlpg = ept_invlpg;
	context->update_pte = ept_update_pte;
	context->root_level = context->shadow_root_level;
	context->root_hpa = INVALID_PAGE;
	context->direct_map = false;

	update_permission_bitmask(vcpu, context, true);
	reset_rsvds_bits_mask_ept(vcpu, context, execonly);
	reset_ept_shadow_zero_bits_mask(vcpu, context, execonly);
}
EXPORT_SYMBOL_GPL(kvm_init_shadow_ept_mmu);

static void init_kvm_softmmu(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->arch.mmu;

	kvm_init_shadow_mmu(vcpu);
	context->set_cr3           = kvm_x86_ops->set_cr3;
	context->get_cr3           = get_cr3;
	context->get_pdptr         = kvm_pdptr_read;
	context->inject_page_fault = kvm_inject_page_fault;
}

static void init_kvm_nested_mmu(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *g_context = &vcpu->arch.nested_mmu;

	g_context->get_cr3           = get_cr3;
	g_context->get_pdptr         = kvm_pdptr_read;
	g_context->inject_page_fault = kvm_inject_page_fault;

	/*
	 * Note that arch.mmu.gva_to_gpa translates l2_gva to l1_gpa. The
	 * translation of l2_gpa to l1_gpa addresses is done using the
	 * arch.nested_mmu.gva_to_gpa function. Basically the gva_to_gpa
	 * functions between mmu and nested_mmu are swapped.
	 */
	if (!is_paging(vcpu)) {
		g_context->nx = false;
		g_context->root_level = 0;
		g_context->gva_to_gpa = nonpaging_gva_to_gpa_nested;
	} else if (is_long_mode(vcpu)) {
		g_context->nx = is_nx(vcpu);
		g_context->root_level = PT64_ROOT_LEVEL;
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
	update_last_pte_bitmap(vcpu, g_context);
}

static void init_kvm_mmu(struct kvm_vcpu *vcpu)
{
	if (mmu_is_nested(vcpu))
		init_kvm_nested_mmu(vcpu);
	else if (tdp_enabled)
		init_kvm_tdp_mmu(vcpu);
	else
		init_kvm_softmmu(vcpu);
}

void kvm_mmu_reset_context(struct kvm_vcpu *vcpu)
{
	kvm_mmu_unload(vcpu);
	init_kvm_mmu(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_mmu_reset_context);

int kvm_mmu_load(struct kvm_vcpu *vcpu)
{
	int r;

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		goto out;
	r = mmu_alloc_roots(vcpu);
	kvm_mmu_sync_roots(vcpu);
	if (r)
		goto out;
	/* set_cr3() should ensure TLB has been flushed */
	vcpu->arch.mmu.set_cr3(vcpu, vcpu->arch.mmu.root_hpa);
out:
	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_load);

void kvm_mmu_unload(struct kvm_vcpu *vcpu)
{
	mmu_free_roots(vcpu);
	WARN_ON(VALID_PAGE(vcpu->arch.mmu.root_hpa));
}
EXPORT_SYMBOL_GPL(kvm_mmu_unload);

static void mmu_pte_write_new_pte(struct kvm_vcpu *vcpu,
				  struct kvm_mmu_page *sp, u64 *spte,
				  const void *new)
{
	if (sp->role.level != PT_PAGE_TABLE_LEVEL) {
		++vcpu->kvm->stat.mmu_pde_zapped;
		return;
        }

	++vcpu->kvm->stat.mmu_pte_updated;
	vcpu->arch.mmu.update_pte(vcpu, sp, spte, new);
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

static void mmu_pte_write_flush_tlb(struct kvm_vcpu *vcpu, bool zap_page,
				    bool remote_flush, bool local_flush)
{
	if (zap_page)
		return;

	if (remote_flush)
		kvm_flush_remote_tlbs(vcpu->kvm);
	else if (local_flush)
		kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
}

static u64 mmu_pte_write_fetch_gpte(struct kvm_vcpu *vcpu, gpa_t *gpa,
				    const u8 *new, int *bytes)
{
	u64 gentry;
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
		r = kvm_vcpu_read_guest(vcpu, *gpa, &gentry, 8);
		if (r)
			gentry = 0;
		new = (const u8 *)&gentry;
	}

	switch (*bytes) {
	case 4:
		gentry = *(const u32 *)new;
		break;
	case 8:
		gentry = *(const u64 *)new;
		break;
	default:
		gentry = 0;
		break;
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
	if (sp->role.level == PT_PAGE_TABLE_LEVEL)
		return false;

	return ++sp->write_flooding_count >= 3;
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
	pte_size = sp->role.cr4_pae ? 8 : 4;

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
	if (!sp->role.cr4_pae) {
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

void kvm_mmu_pte_write(struct kvm_vcpu *vcpu, gpa_t gpa,
		       const u8 *new, int bytes)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct kvm_mmu_page *sp;
	LIST_HEAD(invalid_list);
	u64 entry, gentry, *spte;
	int npte;
	bool remote_flush, local_flush, zap_page;
	union kvm_mmu_page_role mask = { };

	mask.cr0_wp = 1;
	mask.cr4_pae = 1;
	mask.nxe = 1;
	mask.smep_andnot_wp = 1;
	mask.smap_andnot_wp = 1;
	mask.smm = 1;

	/*
	 * If we don't have indirect shadow pages, it means no page is
	 * write-protected, so we can exit simply.
	 */
	if (!ACCESS_ONCE(vcpu->kvm->arch.indirect_shadow_pages))
		return;

	zap_page = remote_flush = local_flush = false;

	pgprintk("%s: gpa %llx bytes %d\n", __func__, gpa, bytes);

	gentry = mmu_pte_write_fetch_gpte(vcpu, &gpa, new, &bytes);

	/*
	 * No need to care whether allocation memory is successful
	 * or not since pte prefetch is skiped if it does not have
	 * enough objects in the cache.
	 */
	mmu_topup_memory_caches(vcpu);

	spin_lock(&vcpu->kvm->mmu_lock);
	++vcpu->kvm->stat.mmu_pte_write;
	kvm_mmu_audit(vcpu, AUDIT_PRE_PTE_WRITE);

	for_each_gfn_indirect_valid_sp(vcpu->kvm, sp, gfn) {
		if (detect_write_misaligned(sp, gpa, bytes) ||
		      detect_write_flooding(sp)) {
			zap_page |= !!kvm_mmu_prepare_zap_page(vcpu->kvm, sp,
						     &invalid_list);
			++vcpu->kvm->stat.mmu_flooded;
			continue;
		}

		spte = get_written_sptes(sp, gpa, &npte);
		if (!spte)
			continue;

		local_flush = true;
		while (npte--) {
			entry = *spte;
			mmu_page_zap_pte(vcpu->kvm, sp, spte);
			if (gentry &&
			      !((sp->role.word ^ vcpu->arch.mmu.base_role.word)
			      & mask.word) && rmap_can_add(vcpu))
				mmu_pte_write_new_pte(vcpu, sp, spte, &gentry);
			if (need_remote_flush(entry, *spte))
				remote_flush = true;
			++spte;
		}
	}
	mmu_pte_write_flush_tlb(vcpu, zap_page, remote_flush, local_flush);
	kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
	kvm_mmu_audit(vcpu, AUDIT_POST_PTE_WRITE);
	spin_unlock(&vcpu->kvm->mmu_lock);
}

int kvm_mmu_unprotect_page_virt(struct kvm_vcpu *vcpu, gva_t gva)
{
	gpa_t gpa;
	int r;

	if (vcpu->arch.mmu.direct_map)
		return 0;

	gpa = kvm_mmu_gva_to_gpa_read(vcpu, gva, NULL);

	r = kvm_mmu_unprotect_page(vcpu->kvm, gpa >> PAGE_SHIFT);

	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_unprotect_page_virt);

static void make_mmu_pages_available(struct kvm_vcpu *vcpu)
{
	LIST_HEAD(invalid_list);

	if (likely(kvm_mmu_available_pages(vcpu->kvm) >= KVM_MIN_FREE_MMU_PAGES))
		return;

	while (kvm_mmu_available_pages(vcpu->kvm) < KVM_REFILL_PAGES) {
		if (!prepare_zap_oldest_mmu_page(vcpu->kvm, &invalid_list))
			break;

		++vcpu->kvm->stat.mmu_recycled;
	}
	kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
}

static bool is_mmio_page_fault(struct kvm_vcpu *vcpu, gva_t addr)
{
	if (vcpu->arch.mmu.direct_map || mmu_is_nested(vcpu))
		return vcpu_match_mmio_gpa(vcpu, addr);

	return vcpu_match_mmio_gva(vcpu, addr);
}

int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gva_t cr2, u32 error_code,
		       void *insn, int insn_len)
{
	int r, emulation_type = EMULTYPE_RETRY;
	enum emulation_result er;

	r = vcpu->arch.mmu.page_fault(vcpu, cr2, error_code, false);
	if (r < 0)
		goto out;

	if (!r) {
		r = 1;
		goto out;
	}

	if (is_mmio_page_fault(vcpu, cr2))
		emulation_type = 0;

	er = x86_emulate_instruction(vcpu, cr2, emulation_type, insn, insn_len);

	switch (er) {
	case EMULATE_DONE:
		return 1;
	case EMULATE_USER_EXIT:
		++vcpu->stat.mmio_exits;
		/* fall through */
	case EMULATE_FAIL:
		return 0;
	default:
		BUG();
	}
out:
	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_page_fault);

void kvm_mmu_invlpg(struct kvm_vcpu *vcpu, gva_t gva)
{
	vcpu->arch.mmu.invlpg(vcpu, gva);
	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	++vcpu->stat.invlpg;
}
EXPORT_SYMBOL_GPL(kvm_mmu_invlpg);

void kvm_enable_tdp(void)
{
	tdp_enabled = true;
}
EXPORT_SYMBOL_GPL(kvm_enable_tdp);

void kvm_disable_tdp(void)
{
	tdp_enabled = false;
}
EXPORT_SYMBOL_GPL(kvm_disable_tdp);

static void free_mmu_pages(struct kvm_vcpu *vcpu)
{
	free_page((unsigned long)vcpu->arch.mmu.pae_root);
	if (vcpu->arch.mmu.lm_root != NULL)
		free_page((unsigned long)vcpu->arch.mmu.lm_root);
}

static int alloc_mmu_pages(struct kvm_vcpu *vcpu)
{
	struct page *page;
	int i;

	/*
	 * When emulating 32-bit mode, cr3 is only 32 bits even on x86_64.
	 * Therefore we need to allocate shadow page tables in the first
	 * 4GB of memory, which happens to fit the DMA32 zone.
	 */
	page = alloc_page(GFP_KERNEL | __GFP_DMA32);
	if (!page)
		return -ENOMEM;

	vcpu->arch.mmu.pae_root = page_address(page);
	for (i = 0; i < 4; ++i)
		vcpu->arch.mmu.pae_root[i] = INVALID_PAGE;

	return 0;
}

int kvm_mmu_create(struct kvm_vcpu *vcpu)
{
	vcpu->arch.walk_mmu = &vcpu->arch.mmu;
	vcpu->arch.mmu.root_hpa = INVALID_PAGE;
	vcpu->arch.mmu.translate_gpa = translate_gpa;
	vcpu->arch.nested_mmu.translate_gpa = translate_nested_gpa;

	return alloc_mmu_pages(vcpu);
}

void kvm_mmu_setup(struct kvm_vcpu *vcpu)
{
	MMU_WARN_ON(VALID_PAGE(vcpu->arch.mmu.root_hpa));

	init_kvm_mmu(vcpu);
}

/* The return value indicates if tlb flush on all vcpus is needed. */
typedef bool (*slot_level_handler) (struct kvm *kvm, unsigned long *rmap);

/* The caller should hold mmu-lock before calling this function. */
static bool
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
				kvm_flush_remote_tlbs(kvm);
				flush = false;
			}
			cond_resched_lock(&kvm->mmu_lock);
		}
	}

	if (flush && lock_flush_tlb) {
		kvm_flush_remote_tlbs(kvm);
		flush = false;
	}

	return flush;
}

static bool
slot_handle_level(struct kvm *kvm, struct kvm_memory_slot *memslot,
		  slot_level_handler fn, int start_level, int end_level,
		  bool lock_flush_tlb)
{
	return slot_handle_level_range(kvm, memslot, fn, start_level,
			end_level, memslot->base_gfn,
			memslot->base_gfn + memslot->npages - 1,
			lock_flush_tlb);
}

static bool
slot_handle_all_level(struct kvm *kvm, struct kvm_memory_slot *memslot,
		      slot_level_handler fn, bool lock_flush_tlb)
{
	return slot_handle_level(kvm, memslot, fn, PT_PAGE_TABLE_LEVEL,
				 PT_MAX_HUGEPAGE_LEVEL, lock_flush_tlb);
}

static bool
slot_handle_large_level(struct kvm *kvm, struct kvm_memory_slot *memslot,
			slot_level_handler fn, bool lock_flush_tlb)
{
	return slot_handle_level(kvm, memslot, fn, PT_PAGE_TABLE_LEVEL + 1,
				 PT_MAX_HUGEPAGE_LEVEL, lock_flush_tlb);
}

static bool
slot_handle_leaf(struct kvm *kvm, struct kvm_memory_slot *memslot,
		 slot_level_handler fn, bool lock_flush_tlb)
{
	return slot_handle_level(kvm, memslot, fn, PT_PAGE_TABLE_LEVEL,
				 PT_PAGE_TABLE_LEVEL, lock_flush_tlb);
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
						PT_PAGE_TABLE_LEVEL, PT_MAX_HUGEPAGE_LEVEL,
						start, end - 1, true);
		}
	}

	spin_unlock(&kvm->mmu_lock);
}

static bool slot_rmap_write_protect(struct kvm *kvm, unsigned long *rmapp)
{
	return __rmap_write_protect(kvm, rmapp, false);
}

void kvm_mmu_slot_remove_write_access(struct kvm *kvm,
				      struct kvm_memory_slot *memslot)
{
	bool flush;

	spin_lock(&kvm->mmu_lock);
	flush = slot_handle_all_level(kvm, memslot, slot_rmap_write_protect,
				      false);
	spin_unlock(&kvm->mmu_lock);

	/*
	 * kvm_mmu_slot_remove_write_access() and kvm_vm_ioctl_get_dirty_log()
	 * which do tlb flush out of mmu-lock should be serialized by
	 * kvm->slots_lock otherwise tlb flush would be missed.
	 */
	lockdep_assert_held(&kvm->slots_lock);

	/*
	 * We can flush all the TLBs out of the mmu lock without TLB
	 * corruption since we just change the spte from writable to
	 * readonly so that we only need to care the case of changing
	 * spte from present to present (changing the spte from present
	 * to nonpresent will flush all the TLBs immediately), in other
	 * words, the only case we care is mmu_spte_update() where we
	 * haved checked SPTE_HOST_WRITEABLE | SPTE_MMU_WRITEABLE
	 * instead of PT_WRITABLE_MASK, that means it does not depend
	 * on PT_WRITABLE_MASK anymore.
	 */
	if (flush)
		kvm_flush_remote_tlbs(kvm);
}

static bool kvm_mmu_zap_collapsible_spte(struct kvm *kvm,
		unsigned long *rmapp)
{
	u64 *sptep;
	struct rmap_iterator iter;
	int need_tlb_flush = 0;
	pfn_t pfn;
	struct kvm_mmu_page *sp;

restart:
	for_each_rmap_spte(rmapp, &iter, sptep) {
		sp = page_header(__pa(sptep));
		pfn = spte_to_pfn(*sptep);

		/*
		 * We cannot do huge page mapping for indirect shadow pages,
		 * which are found on the last rmap (level = 1) when not using
		 * tdp; such shadow pages are synced with the page table in
		 * the guest, and the guest page table is using 4K page size
		 * mapping if the indirect sp has level = 1.
		 */
		if (sp->role.direct &&
			!kvm_is_reserved_pfn(pfn) &&
			PageTransCompound(pfn_to_page(pfn))) {
			drop_spte(kvm, sptep);
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

void kvm_mmu_slot_leaf_clear_dirty(struct kvm *kvm,
				   struct kvm_memory_slot *memslot)
{
	bool flush;

	spin_lock(&kvm->mmu_lock);
	flush = slot_handle_leaf(kvm, memslot, __rmap_clear_dirty, false);
	spin_unlock(&kvm->mmu_lock);

	lockdep_assert_held(&kvm->slots_lock);

	/*
	 * It's also safe to flush TLBs out of mmu lock here as currently this
	 * function is only used for dirty logging, in which case flushing TLB
	 * out of mmu lock also guarantees no dirty pages will be lost in
	 * dirty_bitmap.
	 */
	if (flush)
		kvm_flush_remote_tlbs(kvm);
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

	/* see kvm_mmu_slot_remove_write_access */
	lockdep_assert_held(&kvm->slots_lock);

	if (flush)
		kvm_flush_remote_tlbs(kvm);
}
EXPORT_SYMBOL_GPL(kvm_mmu_slot_largepage_remove_write_access);

void kvm_mmu_slot_set_dirty(struct kvm *kvm,
			    struct kvm_memory_slot *memslot)
{
	bool flush;

	spin_lock(&kvm->mmu_lock);
	flush = slot_handle_all_level(kvm, memslot, __rmap_set_dirty, false);
	spin_unlock(&kvm->mmu_lock);

	lockdep_assert_held(&kvm->slots_lock);

	/* see kvm_mmu_slot_leaf_clear_dirty */
	if (flush)
		kvm_flush_remote_tlbs(kvm);
}
EXPORT_SYMBOL_GPL(kvm_mmu_slot_set_dirty);

#define BATCH_ZAP_PAGES	10
static void kvm_zap_obsolete_pages(struct kvm *kvm)
{
	struct kvm_mmu_page *sp, *node;
	int batch = 0;

restart:
	list_for_each_entry_safe_reverse(sp, node,
	      &kvm->arch.active_mmu_pages, link) {
		int ret;

		/*
		 * No obsolete page exists before new created page since
		 * active_mmu_pages is the FIFO list.
		 */
		if (!is_obsolete_sp(kvm, sp))
			break;

		/*
		 * Since we are reversely walking the list and the invalid
		 * list will be moved to the head, skip the invalid page
		 * can help us to avoid the infinity list walking.
		 */
		if (sp->role.invalid)
			continue;

		/*
		 * Need not flush tlb since we only zap the sp with invalid
		 * generation number.
		 */
		if (batch >= BATCH_ZAP_PAGES &&
		      cond_resched_lock(&kvm->mmu_lock)) {
			batch = 0;
			goto restart;
		}

		ret = kvm_mmu_prepare_zap_page(kvm, sp,
				&kvm->arch.zapped_obsolete_pages);
		batch += ret;

		if (ret)
			goto restart;
	}

	/*
	 * Should flush tlb before free page tables since lockless-walking
	 * may use the pages.
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
void kvm_mmu_invalidate_zap_all_pages(struct kvm *kvm)
{
	spin_lock(&kvm->mmu_lock);
	trace_kvm_mmu_invalidate_zap_all_pages(kvm);
	kvm->arch.mmu_valid_gen++;

	/*
	 * Notify all vcpus to reload its shadow page table
	 * and flush TLB. Then all vcpus will switch to new
	 * shadow page table with the new mmu_valid_gen.
	 *
	 * Note: we should do this under the protection of
	 * mmu-lock, otherwise, vcpu would purge shadow page
	 * but miss tlb flush.
	 */
	kvm_reload_remote_mmus(kvm);

	kvm_zap_obsolete_pages(kvm);
	spin_unlock(&kvm->mmu_lock);
}

static bool kvm_has_zapped_obsolete_pages(struct kvm *kvm)
{
	return unlikely(!list_empty_careful(&kvm->arch.zapped_obsolete_pages));
}

void kvm_mmu_invalidate_mmio_sptes(struct kvm *kvm, struct kvm_memslots *slots)
{
	/*
	 * The very rare case: if the generation-number is round,
	 * zap all shadow pages.
	 */
	if (unlikely((slots->generation & MMIO_GEN_MASK) == 0)) {
		printk_ratelimited(KERN_DEBUG "kvm: zapping shadow pages for mmio generation wraparound\n");
		kvm_mmu_invalidate_zap_all_pages(kvm);
	}
}

static unsigned long
mmu_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	struct kvm *kvm;
	int nr_to_scan = sc->nr_to_scan;
	unsigned long freed = 0;

	spin_lock(&kvm_lock);

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

		if (prepare_zap_oldest_mmu_page(kvm, &invalid_list))
			freed++;
		kvm_mmu_commit_zap_page(kvm, &invalid_list);

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

	spin_unlock(&kvm_lock);
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
	if (pte_list_desc_cache)
		kmem_cache_destroy(pte_list_desc_cache);
	if (mmu_page_header_cache)
		kmem_cache_destroy(mmu_page_header_cache);
}

int kvm_mmu_module_init(void)
{
	pte_list_desc_cache = kmem_cache_create("pte_list_desc",
					    sizeof(struct pte_list_desc),
					    0, 0, NULL);
	if (!pte_list_desc_cache)
		goto nomem;

	mmu_page_header_cache = kmem_cache_create("kvm_mmu_page_header",
						  sizeof(struct kvm_mmu_page),
						  0, 0, NULL);
	if (!mmu_page_header_cache)
		goto nomem;

	if (percpu_counter_init(&kvm_total_used_mmu_pages, 0, GFP_KERNEL))
		goto nomem;

	register_shrinker(&mmu_shrinker);

	return 0;

nomem:
	mmu_destroy_caches();
	return -ENOMEM;
}

/*
 * Caculate mmu pages needed for kvm.
 */
unsigned int kvm_mmu_calculate_mmu_pages(struct kvm *kvm)
{
	unsigned int nr_mmu_pages;
	unsigned int  nr_pages = 0;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int i;

	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++) {
		slots = __kvm_memslots(kvm, i);

		kvm_for_each_memslot(memslot, slots)
			nr_pages += memslot->npages;
	}

	nr_mmu_pages = nr_pages * KVM_PERMILLE_MMU_PAGES / 1000;
	nr_mmu_pages = max(nr_mmu_pages,
			   (unsigned int) KVM_MIN_ALLOC_MMU_PAGES);

	return nr_mmu_pages;
}

void kvm_mmu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_mmu_unload(vcpu);
	free_mmu_pages(vcpu);
	mmu_free_memory_caches(vcpu);
}

void kvm_mmu_module_exit(void)
{
	mmu_destroy_caches();
	percpu_counter_destroy(&kvm_total_used_mmu_pages);
	unregister_shrinker(&mmu_shrinker);
	mmu_audit_disable();
}
