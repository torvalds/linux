/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * MMU support
 *
 * Copyright (C) 2006 Qumranet, Inc.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "vmx.h"
#include "mmu.h"

#include <linux/kvm_host.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/hugetlb.h>
#include <linux/compiler.h>

#include <asm/page.h>
#include <asm/cmpxchg.h>
#include <asm/io.h>

/*
 * When setting this variable to true it enables Two-Dimensional-Paging
 * where the hardware walks 2 page tables:
 * 1. the guest-virtual to guest-physical
 * 2. while doing 1. it walks guest-physical to host-physical
 * If the hardware supports that we don't need to do shadow paging.
 */
bool tdp_enabled = false;

#undef MMU_DEBUG

#undef AUDIT

#ifdef AUDIT
static void kvm_mmu_audit(struct kvm_vcpu *vcpu, const char *msg);
#else
static void kvm_mmu_audit(struct kvm_vcpu *vcpu, const char *msg) {}
#endif

#ifdef MMU_DEBUG

#define pgprintk(x...) do { if (dbg) printk(x); } while (0)
#define rmap_printk(x...) do { if (dbg) printk(x); } while (0)

#else

#define pgprintk(x...) do { } while (0)
#define rmap_printk(x...) do { } while (0)

#endif

#if defined(MMU_DEBUG) || defined(AUDIT)
static int dbg = 1;
#endif

#ifndef MMU_DEBUG
#define ASSERT(x) do { } while (0)
#else
#define ASSERT(x)							\
	if (!(x)) {							\
		printk(KERN_WARNING "assertion failed %s:%d: %s\n",	\
		       __FILE__, __LINE__, #x);				\
	}
#endif

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
#define PT_ACCESSED_MASK (1ULL << 5)
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


#define PT_FIRST_AVAIL_BITS_SHIFT 9
#define PT64_SECOND_AVAIL_BITS_SHIFT 52

#define VALID_PAGE(x) ((x) != INVALID_PAGE)

#define PT64_LEVEL_BITS 9

#define PT64_LEVEL_SHIFT(level) \
		(PAGE_SHIFT + (level - 1) * PT64_LEVEL_BITS)

#define PT64_LEVEL_MASK(level) \
		(((1ULL << PT64_LEVEL_BITS) - 1) << PT64_LEVEL_SHIFT(level))

#define PT64_INDEX(address, level)\
	(((address) >> PT64_LEVEL_SHIFT(level)) & ((1 << PT64_LEVEL_BITS) - 1))


#define PT32_LEVEL_BITS 10

#define PT32_LEVEL_SHIFT(level) \
		(PAGE_SHIFT + (level - 1) * PT32_LEVEL_BITS)

#define PT32_LEVEL_MASK(level) \
		(((1ULL << PT32_LEVEL_BITS) - 1) << PT32_LEVEL_SHIFT(level))

#define PT32_INDEX(address, level)\
	(((address) >> PT32_LEVEL_SHIFT(level)) & ((1 << PT32_LEVEL_BITS) - 1))


#define PT64_BASE_ADDR_MASK (((1ULL << 52) - 1) & ~(u64)(PAGE_SIZE-1))
#define PT64_DIR_BASE_ADDR_MASK \
	(PT64_BASE_ADDR_MASK & ~((1ULL << (PAGE_SHIFT + PT64_LEVEL_BITS)) - 1))

#define PT32_BASE_ADDR_MASK PAGE_MASK
#define PT32_DIR_BASE_ADDR_MASK \
	(PAGE_MASK & ~((1ULL << (PAGE_SHIFT + PT32_LEVEL_BITS)) - 1))

#define PT64_PERM_MASK (PT_PRESENT_MASK | PT_WRITABLE_MASK | PT_USER_MASK \
			| PT64_NX_MASK)

#define PFERR_PRESENT_MASK (1U << 0)
#define PFERR_WRITE_MASK (1U << 1)
#define PFERR_USER_MASK (1U << 2)
#define PFERR_FETCH_MASK (1U << 4)

#define PT64_ROOT_LEVEL 4
#define PT32_ROOT_LEVEL 2
#define PT32E_ROOT_LEVEL 3

#define PT_DIRECTORY_LEVEL 2
#define PT_PAGE_TABLE_LEVEL 1

#define RMAP_EXT 4

#define ACC_EXEC_MASK    1
#define ACC_WRITE_MASK   PT_WRITABLE_MASK
#define ACC_USER_MASK    PT_USER_MASK
#define ACC_ALL          (ACC_EXEC_MASK | ACC_WRITE_MASK | ACC_USER_MASK)

struct kvm_pv_mmu_op_buffer {
	void *ptr;
	unsigned len;
	unsigned processed;
	char buf[512] __aligned(sizeof(long));
};

struct kvm_rmap_desc {
	u64 *shadow_ptes[RMAP_EXT];
	struct kvm_rmap_desc *more;
};

static struct kmem_cache *pte_chain_cache;
static struct kmem_cache *rmap_desc_cache;
static struct kmem_cache *mmu_page_header_cache;

static u64 __read_mostly shadow_trap_nonpresent_pte;
static u64 __read_mostly shadow_notrap_nonpresent_pte;

void kvm_mmu_set_nonpresent_ptes(u64 trap_pte, u64 notrap_pte)
{
	shadow_trap_nonpresent_pte = trap_pte;
	shadow_notrap_nonpresent_pte = notrap_pte;
}
EXPORT_SYMBOL_GPL(kvm_mmu_set_nonpresent_ptes);

static int is_write_protection(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.cr0 & X86_CR0_WP;
}

static int is_cpuid_PSE36(void)
{
	return 1;
}

static int is_nx(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.shadow_efer & EFER_NX;
}

static int is_present_pte(unsigned long pte)
{
	return pte & PT_PRESENT_MASK;
}

static int is_shadow_present_pte(u64 pte)
{
	return pte != shadow_trap_nonpresent_pte
		&& pte != shadow_notrap_nonpresent_pte;
}

static int is_large_pte(u64 pte)
{
	return pte & PT_PAGE_SIZE_MASK;
}

static int is_writeble_pte(unsigned long pte)
{
	return pte & PT_WRITABLE_MASK;
}

static int is_dirty_pte(unsigned long pte)
{
	return pte & PT_DIRTY_MASK;
}

static int is_rmap_pte(u64 pte)
{
	return is_shadow_present_pte(pte);
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

static void set_shadow_pte(u64 *sptep, u64 spte)
{
#ifdef CONFIG_X86_64
	set_64bit((unsigned long *)sptep, spte);
#else
	set_64bit((unsigned long long *)sptep, spte);
#endif
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

static void mmu_free_memory_cache(struct kvm_mmu_memory_cache *mc)
{
	while (mc->nobjs)
		kfree(mc->objects[--mc->nobjs]);
}

static int mmu_topup_memory_cache_page(struct kvm_mmu_memory_cache *cache,
				       int min)
{
	struct page *page;

	if (cache->nobjs >= min)
		return 0;
	while (cache->nobjs < ARRAY_SIZE(cache->objects)) {
		page = alloc_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		set_page_private(page, 0);
		cache->objects[cache->nobjs++] = page_address(page);
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

	r = mmu_topup_memory_cache(&vcpu->arch.mmu_pte_chain_cache,
				   pte_chain_cache, 4);
	if (r)
		goto out;
	r = mmu_topup_memory_cache(&vcpu->arch.mmu_rmap_desc_cache,
				   rmap_desc_cache, 1);
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
	mmu_free_memory_cache(&vcpu->arch.mmu_pte_chain_cache);
	mmu_free_memory_cache(&vcpu->arch.mmu_rmap_desc_cache);
	mmu_free_memory_cache_page(&vcpu->arch.mmu_page_cache);
	mmu_free_memory_cache(&vcpu->arch.mmu_page_header_cache);
}

static void *mmu_memory_cache_alloc(struct kvm_mmu_memory_cache *mc,
				    size_t size)
{
	void *p;

	BUG_ON(!mc->nobjs);
	p = mc->objects[--mc->nobjs];
	memset(p, 0, size);
	return p;
}

static struct kvm_pte_chain *mmu_alloc_pte_chain(struct kvm_vcpu *vcpu)
{
	return mmu_memory_cache_alloc(&vcpu->arch.mmu_pte_chain_cache,
				      sizeof(struct kvm_pte_chain));
}

static void mmu_free_pte_chain(struct kvm_pte_chain *pc)
{
	kfree(pc);
}

static struct kvm_rmap_desc *mmu_alloc_rmap_desc(struct kvm_vcpu *vcpu)
{
	return mmu_memory_cache_alloc(&vcpu->arch.mmu_rmap_desc_cache,
				      sizeof(struct kvm_rmap_desc));
}

static void mmu_free_rmap_desc(struct kvm_rmap_desc *rd)
{
	kfree(rd);
}

/*
 * Return the pointer to the largepage write count for a given
 * gfn, handling slots that are not large page aligned.
 */
static int *slot_largepage_idx(gfn_t gfn, struct kvm_memory_slot *slot)
{
	unsigned long idx;

	idx = (gfn / KVM_PAGES_PER_HPAGE) -
	      (slot->base_gfn / KVM_PAGES_PER_HPAGE);
	return &slot->lpage_info[idx].write_count;
}

static void account_shadowed(struct kvm *kvm, gfn_t gfn)
{
	int *write_count;

	write_count = slot_largepage_idx(gfn, gfn_to_memslot(kvm, gfn));
	*write_count += 1;
	WARN_ON(*write_count > KVM_PAGES_PER_HPAGE);
}

static void unaccount_shadowed(struct kvm *kvm, gfn_t gfn)
{
	int *write_count;

	write_count = slot_largepage_idx(gfn, gfn_to_memslot(kvm, gfn));
	*write_count -= 1;
	WARN_ON(*write_count < 0);
}

static int has_wrprotected_page(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *slot = gfn_to_memslot(kvm, gfn);
	int *largepage_idx;

	if (slot) {
		largepage_idx = slot_largepage_idx(gfn, slot);
		return *largepage_idx;
	}

	return 1;
}

static int host_largepage_backed(struct kvm *kvm, gfn_t gfn)
{
	struct vm_area_struct *vma;
	unsigned long addr;

	addr = gfn_to_hva(kvm, gfn);
	if (kvm_is_error_hva(addr))
		return 0;

	vma = find_vma(current->mm, addr);
	if (vma && is_vm_hugetlb_page(vma))
		return 1;

	return 0;
}

static int is_largepage_backed(struct kvm_vcpu *vcpu, gfn_t large_gfn)
{
	struct kvm_memory_slot *slot;

	if (has_wrprotected_page(vcpu->kvm, large_gfn))
		return 0;

	if (!host_largepage_backed(vcpu->kvm, large_gfn))
		return 0;

	slot = gfn_to_memslot(vcpu->kvm, large_gfn);
	if (slot && slot->dirty_bitmap)
		return 0;

	return 1;
}

/*
 * Take gfn and return the reverse mapping to it.
 * Note: gfn must be unaliased before this function get called
 */

static unsigned long *gfn_to_rmap(struct kvm *kvm, gfn_t gfn, int lpage)
{
	struct kvm_memory_slot *slot;
	unsigned long idx;

	slot = gfn_to_memslot(kvm, gfn);
	if (!lpage)
		return &slot->rmap[gfn - slot->base_gfn];

	idx = (gfn / KVM_PAGES_PER_HPAGE) -
	      (slot->base_gfn / KVM_PAGES_PER_HPAGE);

	return &slot->lpage_info[idx].rmap_pde;
}

/*
 * Reverse mapping data structures:
 *
 * If rmapp bit zero is zero, then rmapp point to the shadw page table entry
 * that points to page_address(page).
 *
 * If rmapp bit zero is one, (then rmap & ~1) points to a struct kvm_rmap_desc
 * containing more mappings.
 */
static void rmap_add(struct kvm_vcpu *vcpu, u64 *spte, gfn_t gfn, int lpage)
{
	struct kvm_mmu_page *sp;
	struct kvm_rmap_desc *desc;
	unsigned long *rmapp;
	int i;

	if (!is_rmap_pte(*spte))
		return;
	gfn = unalias_gfn(vcpu->kvm, gfn);
	sp = page_header(__pa(spte));
	sp->gfns[spte - sp->spt] = gfn;
	rmapp = gfn_to_rmap(vcpu->kvm, gfn, lpage);
	if (!*rmapp) {
		rmap_printk("rmap_add: %p %llx 0->1\n", spte, *spte);
		*rmapp = (unsigned long)spte;
	} else if (!(*rmapp & 1)) {
		rmap_printk("rmap_add: %p %llx 1->many\n", spte, *spte);
		desc = mmu_alloc_rmap_desc(vcpu);
		desc->shadow_ptes[0] = (u64 *)*rmapp;
		desc->shadow_ptes[1] = spte;
		*rmapp = (unsigned long)desc | 1;
	} else {
		rmap_printk("rmap_add: %p %llx many->many\n", spte, *spte);
		desc = (struct kvm_rmap_desc *)(*rmapp & ~1ul);
		while (desc->shadow_ptes[RMAP_EXT-1] && desc->more)
			desc = desc->more;
		if (desc->shadow_ptes[RMAP_EXT-1]) {
			desc->more = mmu_alloc_rmap_desc(vcpu);
			desc = desc->more;
		}
		for (i = 0; desc->shadow_ptes[i]; ++i)
			;
		desc->shadow_ptes[i] = spte;
	}
}

static void rmap_desc_remove_entry(unsigned long *rmapp,
				   struct kvm_rmap_desc *desc,
				   int i,
				   struct kvm_rmap_desc *prev_desc)
{
	int j;

	for (j = RMAP_EXT - 1; !desc->shadow_ptes[j] && j > i; --j)
		;
	desc->shadow_ptes[i] = desc->shadow_ptes[j];
	desc->shadow_ptes[j] = NULL;
	if (j != 0)
		return;
	if (!prev_desc && !desc->more)
		*rmapp = (unsigned long)desc->shadow_ptes[0];
	else
		if (prev_desc)
			prev_desc->more = desc->more;
		else
			*rmapp = (unsigned long)desc->more | 1;
	mmu_free_rmap_desc(desc);
}

static void rmap_remove(struct kvm *kvm, u64 *spte)
{
	struct kvm_rmap_desc *desc;
	struct kvm_rmap_desc *prev_desc;
	struct kvm_mmu_page *sp;
	pfn_t pfn;
	unsigned long *rmapp;
	int i;

	if (!is_rmap_pte(*spte))
		return;
	sp = page_header(__pa(spte));
	pfn = spte_to_pfn(*spte);
	if (*spte & PT_ACCESSED_MASK)
		kvm_set_pfn_accessed(pfn);
	if (is_writeble_pte(*spte))
		kvm_release_pfn_dirty(pfn);
	else
		kvm_release_pfn_clean(pfn);
	rmapp = gfn_to_rmap(kvm, sp->gfns[spte - sp->spt], is_large_pte(*spte));
	if (!*rmapp) {
		printk(KERN_ERR "rmap_remove: %p %llx 0->BUG\n", spte, *spte);
		BUG();
	} else if (!(*rmapp & 1)) {
		rmap_printk("rmap_remove:  %p %llx 1->0\n", spte, *spte);
		if ((u64 *)*rmapp != spte) {
			printk(KERN_ERR "rmap_remove:  %p %llx 1->BUG\n",
			       spte, *spte);
			BUG();
		}
		*rmapp = 0;
	} else {
		rmap_printk("rmap_remove:  %p %llx many->many\n", spte, *spte);
		desc = (struct kvm_rmap_desc *)(*rmapp & ~1ul);
		prev_desc = NULL;
		while (desc) {
			for (i = 0; i < RMAP_EXT && desc->shadow_ptes[i]; ++i)
				if (desc->shadow_ptes[i] == spte) {
					rmap_desc_remove_entry(rmapp,
							       desc, i,
							       prev_desc);
					return;
				}
			prev_desc = desc;
			desc = desc->more;
		}
		BUG();
	}
}

static u64 *rmap_next(struct kvm *kvm, unsigned long *rmapp, u64 *spte)
{
	struct kvm_rmap_desc *desc;
	struct kvm_rmap_desc *prev_desc;
	u64 *prev_spte;
	int i;

	if (!*rmapp)
		return NULL;
	else if (!(*rmapp & 1)) {
		if (!spte)
			return (u64 *)*rmapp;
		return NULL;
	}
	desc = (struct kvm_rmap_desc *)(*rmapp & ~1ul);
	prev_desc = NULL;
	prev_spte = NULL;
	while (desc) {
		for (i = 0; i < RMAP_EXT && desc->shadow_ptes[i]; ++i) {
			if (prev_spte == spte)
				return desc->shadow_ptes[i];
			prev_spte = desc->shadow_ptes[i];
		}
		desc = desc->more;
	}
	return NULL;
}

static void rmap_write_protect(struct kvm *kvm, u64 gfn)
{
	unsigned long *rmapp;
	u64 *spte;
	int write_protected = 0;

	gfn = unalias_gfn(kvm, gfn);
	rmapp = gfn_to_rmap(kvm, gfn, 0);

	spte = rmap_next(kvm, rmapp, NULL);
	while (spte) {
		BUG_ON(!spte);
		BUG_ON(!(*spte & PT_PRESENT_MASK));
		rmap_printk("rmap_write_protect: spte %p %llx\n", spte, *spte);
		if (is_writeble_pte(*spte)) {
			set_shadow_pte(spte, *spte & ~PT_WRITABLE_MASK);
			write_protected = 1;
		}
		spte = rmap_next(kvm, rmapp, spte);
	}
	if (write_protected) {
		pfn_t pfn;

		spte = rmap_next(kvm, rmapp, NULL);
		pfn = spte_to_pfn(*spte);
		kvm_set_pfn_dirty(pfn);
	}

	/* check for huge page mappings */
	rmapp = gfn_to_rmap(kvm, gfn, 1);
	spte = rmap_next(kvm, rmapp, NULL);
	while (spte) {
		BUG_ON(!spte);
		BUG_ON(!(*spte & PT_PRESENT_MASK));
		BUG_ON((*spte & (PT_PAGE_SIZE_MASK|PT_PRESENT_MASK)) != (PT_PAGE_SIZE_MASK|PT_PRESENT_MASK));
		pgprintk("rmap_write_protect(large): spte %p %llx %lld\n", spte, *spte, gfn);
		if (is_writeble_pte(*spte)) {
			rmap_remove(kvm, spte);
			--kvm->stat.lpages;
			set_shadow_pte(spte, shadow_trap_nonpresent_pte);
			write_protected = 1;
		}
		spte = rmap_next(kvm, rmapp, spte);
	}

	if (write_protected)
		kvm_flush_remote_tlbs(kvm);

	account_shadowed(kvm, gfn);
}

#ifdef MMU_DEBUG
static int is_empty_shadow_page(u64 *spt)
{
	u64 *pos;
	u64 *end;

	for (pos = spt, end = pos + PAGE_SIZE / sizeof(u64); pos != end; pos++)
		if (*pos != shadow_trap_nonpresent_pte) {
			printk(KERN_ERR "%s: %p %llx\n", __func__,
			       pos, *pos);
			return 0;
		}
	return 1;
}
#endif

static void kvm_mmu_free_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	ASSERT(is_empty_shadow_page(sp->spt));
	list_del(&sp->link);
	__free_page(virt_to_page(sp->spt));
	__free_page(virt_to_page(sp->gfns));
	kfree(sp);
	++kvm->arch.n_free_mmu_pages;
}

static unsigned kvm_page_table_hashfn(gfn_t gfn)
{
	return gfn & ((1 << KVM_MMU_HASH_SHIFT) - 1);
}

static struct kvm_mmu_page *kvm_mmu_alloc_page(struct kvm_vcpu *vcpu,
					       u64 *parent_pte)
{
	struct kvm_mmu_page *sp;

	sp = mmu_memory_cache_alloc(&vcpu->arch.mmu_page_header_cache, sizeof *sp);
	sp->spt = mmu_memory_cache_alloc(&vcpu->arch.mmu_page_cache, PAGE_SIZE);
	sp->gfns = mmu_memory_cache_alloc(&vcpu->arch.mmu_page_cache, PAGE_SIZE);
	set_page_private(virt_to_page(sp->spt), (unsigned long)sp);
	list_add(&sp->link, &vcpu->kvm->arch.active_mmu_pages);
	ASSERT(is_empty_shadow_page(sp->spt));
	sp->slot_bitmap = 0;
	sp->multimapped = 0;
	sp->parent_pte = parent_pte;
	--vcpu->kvm->arch.n_free_mmu_pages;
	return sp;
}

static void mmu_page_add_parent_pte(struct kvm_vcpu *vcpu,
				    struct kvm_mmu_page *sp, u64 *parent_pte)
{
	struct kvm_pte_chain *pte_chain;
	struct hlist_node *node;
	int i;

	if (!parent_pte)
		return;
	if (!sp->multimapped) {
		u64 *old = sp->parent_pte;

		if (!old) {
			sp->parent_pte = parent_pte;
			return;
		}
		sp->multimapped = 1;
		pte_chain = mmu_alloc_pte_chain(vcpu);
		INIT_HLIST_HEAD(&sp->parent_ptes);
		hlist_add_head(&pte_chain->link, &sp->parent_ptes);
		pte_chain->parent_ptes[0] = old;
	}
	hlist_for_each_entry(pte_chain, node, &sp->parent_ptes, link) {
		if (pte_chain->parent_ptes[NR_PTE_CHAIN_ENTRIES-1])
			continue;
		for (i = 0; i < NR_PTE_CHAIN_ENTRIES; ++i)
			if (!pte_chain->parent_ptes[i]) {
				pte_chain->parent_ptes[i] = parent_pte;
				return;
			}
	}
	pte_chain = mmu_alloc_pte_chain(vcpu);
	BUG_ON(!pte_chain);
	hlist_add_head(&pte_chain->link, &sp->parent_ptes);
	pte_chain->parent_ptes[0] = parent_pte;
}

static void mmu_page_remove_parent_pte(struct kvm_mmu_page *sp,
				       u64 *parent_pte)
{
	struct kvm_pte_chain *pte_chain;
	struct hlist_node *node;
	int i;

	if (!sp->multimapped) {
		BUG_ON(sp->parent_pte != parent_pte);
		sp->parent_pte = NULL;
		return;
	}
	hlist_for_each_entry(pte_chain, node, &sp->parent_ptes, link)
		for (i = 0; i < NR_PTE_CHAIN_ENTRIES; ++i) {
			if (!pte_chain->parent_ptes[i])
				break;
			if (pte_chain->parent_ptes[i] != parent_pte)
				continue;
			while (i + 1 < NR_PTE_CHAIN_ENTRIES
				&& pte_chain->parent_ptes[i + 1]) {
				pte_chain->parent_ptes[i]
					= pte_chain->parent_ptes[i + 1];
				++i;
			}
			pte_chain->parent_ptes[i] = NULL;
			if (i == 0) {
				hlist_del(&pte_chain->link);
				mmu_free_pte_chain(pte_chain);
				if (hlist_empty(&sp->parent_ptes)) {
					sp->multimapped = 0;
					sp->parent_pte = NULL;
				}
			}
			return;
		}
	BUG();
}

static struct kvm_mmu_page *kvm_mmu_lookup_page(struct kvm *kvm, gfn_t gfn)
{
	unsigned index;
	struct hlist_head *bucket;
	struct kvm_mmu_page *sp;
	struct hlist_node *node;

	pgprintk("%s: looking for gfn %lx\n", __func__, gfn);
	index = kvm_page_table_hashfn(gfn);
	bucket = &kvm->arch.mmu_page_hash[index];
	hlist_for_each_entry(sp, node, bucket, hash_link)
		if (sp->gfn == gfn && !sp->role.metaphysical
		    && !sp->role.invalid) {
			pgprintk("%s: found role %x\n",
				 __func__, sp->role.word);
			return sp;
		}
	return NULL;
}

static struct kvm_mmu_page *kvm_mmu_get_page(struct kvm_vcpu *vcpu,
					     gfn_t gfn,
					     gva_t gaddr,
					     unsigned level,
					     int metaphysical,
					     unsigned access,
					     u64 *parent_pte)
{
	union kvm_mmu_page_role role;
	unsigned index;
	unsigned quadrant;
	struct hlist_head *bucket;
	struct kvm_mmu_page *sp;
	struct hlist_node *node;

	role.word = 0;
	role.glevels = vcpu->arch.mmu.root_level;
	role.level = level;
	role.metaphysical = metaphysical;
	role.access = access;
	if (vcpu->arch.mmu.root_level <= PT32_ROOT_LEVEL) {
		quadrant = gaddr >> (PAGE_SHIFT + (PT64_PT_BITS * level));
		quadrant &= (1 << ((PT32_PT_BITS - PT64_PT_BITS) * level)) - 1;
		role.quadrant = quadrant;
	}
	pgprintk("%s: looking gfn %lx role %x\n", __func__,
		 gfn, role.word);
	index = kvm_page_table_hashfn(gfn);
	bucket = &vcpu->kvm->arch.mmu_page_hash[index];
	hlist_for_each_entry(sp, node, bucket, hash_link)
		if (sp->gfn == gfn && sp->role.word == role.word) {
			mmu_page_add_parent_pte(vcpu, sp, parent_pte);
			pgprintk("%s: found\n", __func__);
			return sp;
		}
	++vcpu->kvm->stat.mmu_cache_miss;
	sp = kvm_mmu_alloc_page(vcpu, parent_pte);
	if (!sp)
		return sp;
	pgprintk("%s: adding gfn %lx role %x\n", __func__, gfn, role.word);
	sp->gfn = gfn;
	sp->role = role;
	hlist_add_head(&sp->hash_link, bucket);
	if (!metaphysical)
		rmap_write_protect(vcpu->kvm, gfn);
	vcpu->arch.mmu.prefetch_page(vcpu, sp);
	return sp;
}

static void kvm_mmu_page_unlink_children(struct kvm *kvm,
					 struct kvm_mmu_page *sp)
{
	unsigned i;
	u64 *pt;
	u64 ent;

	pt = sp->spt;

	if (sp->role.level == PT_PAGE_TABLE_LEVEL) {
		for (i = 0; i < PT64_ENT_PER_PAGE; ++i) {
			if (is_shadow_present_pte(pt[i]))
				rmap_remove(kvm, &pt[i]);
			pt[i] = shadow_trap_nonpresent_pte;
		}
		kvm_flush_remote_tlbs(kvm);
		return;
	}

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i) {
		ent = pt[i];

		if (is_shadow_present_pte(ent)) {
			if (!is_large_pte(ent)) {
				ent &= PT64_BASE_ADDR_MASK;
				mmu_page_remove_parent_pte(page_header(ent),
							   &pt[i]);
			} else {
				--kvm->stat.lpages;
				rmap_remove(kvm, &pt[i]);
			}
		}
		pt[i] = shadow_trap_nonpresent_pte;
	}
	kvm_flush_remote_tlbs(kvm);
}

static void kvm_mmu_put_page(struct kvm_mmu_page *sp, u64 *parent_pte)
{
	mmu_page_remove_parent_pte(sp, parent_pte);
}

static void kvm_mmu_reset_last_pte_updated(struct kvm *kvm)
{
	int i;

	for (i = 0; i < KVM_MAX_VCPUS; ++i)
		if (kvm->vcpus[i])
			kvm->vcpus[i]->arch.last_pte_updated = NULL;
}

static void kvm_mmu_zap_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	u64 *parent_pte;

	++kvm->stat.mmu_shadow_zapped;
	while (sp->multimapped || sp->parent_pte) {
		if (!sp->multimapped)
			parent_pte = sp->parent_pte;
		else {
			struct kvm_pte_chain *chain;

			chain = container_of(sp->parent_ptes.first,
					     struct kvm_pte_chain, link);
			parent_pte = chain->parent_ptes[0];
		}
		BUG_ON(!parent_pte);
		kvm_mmu_put_page(sp, parent_pte);
		set_shadow_pte(parent_pte, shadow_trap_nonpresent_pte);
	}
	kvm_mmu_page_unlink_children(kvm, sp);
	if (!sp->root_count) {
		if (!sp->role.metaphysical)
			unaccount_shadowed(kvm, sp->gfn);
		hlist_del(&sp->hash_link);
		kvm_mmu_free_page(kvm, sp);
	} else {
		list_move(&sp->link, &kvm->arch.active_mmu_pages);
		sp->role.invalid = 1;
		kvm_reload_remote_mmus(kvm);
	}
	kvm_mmu_reset_last_pte_updated(kvm);
}

/*
 * Changing the number of mmu pages allocated to the vm
 * Note: if kvm_nr_mmu_pages is too small, you will get dead lock
 */
void kvm_mmu_change_mmu_pages(struct kvm *kvm, unsigned int kvm_nr_mmu_pages)
{
	/*
	 * If we set the number of mmu pages to be smaller be than the
	 * number of actived pages , we must to free some mmu pages before we
	 * change the value
	 */

	if ((kvm->arch.n_alloc_mmu_pages - kvm->arch.n_free_mmu_pages) >
	    kvm_nr_mmu_pages) {
		int n_used_mmu_pages = kvm->arch.n_alloc_mmu_pages
				       - kvm->arch.n_free_mmu_pages;

		while (n_used_mmu_pages > kvm_nr_mmu_pages) {
			struct kvm_mmu_page *page;

			page = container_of(kvm->arch.active_mmu_pages.prev,
					    struct kvm_mmu_page, link);
			kvm_mmu_zap_page(kvm, page);
			n_used_mmu_pages--;
		}
		kvm->arch.n_free_mmu_pages = 0;
	}
	else
		kvm->arch.n_free_mmu_pages += kvm_nr_mmu_pages
					 - kvm->arch.n_alloc_mmu_pages;

	kvm->arch.n_alloc_mmu_pages = kvm_nr_mmu_pages;
}

static int kvm_mmu_unprotect_page(struct kvm *kvm, gfn_t gfn)
{
	unsigned index;
	struct hlist_head *bucket;
	struct kvm_mmu_page *sp;
	struct hlist_node *node, *n;
	int r;

	pgprintk("%s: looking for gfn %lx\n", __func__, gfn);
	r = 0;
	index = kvm_page_table_hashfn(gfn);
	bucket = &kvm->arch.mmu_page_hash[index];
	hlist_for_each_entry_safe(sp, node, n, bucket, hash_link)
		if (sp->gfn == gfn && !sp->role.metaphysical) {
			pgprintk("%s: gfn %lx role %x\n", __func__, gfn,
				 sp->role.word);
			kvm_mmu_zap_page(kvm, sp);
			r = 1;
		}
	return r;
}

static void mmu_unshadow(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_mmu_page *sp;

	while ((sp = kvm_mmu_lookup_page(kvm, gfn)) != NULL) {
		pgprintk("%s: zap %lx %x\n", __func__, gfn, sp->role.word);
		kvm_mmu_zap_page(kvm, sp);
	}
}

static void page_header_update_slot(struct kvm *kvm, void *pte, gfn_t gfn)
{
	int slot = memslot_id(kvm, gfn_to_memslot(kvm, gfn));
	struct kvm_mmu_page *sp = page_header(__pa(pte));

	__set_bit(slot, &sp->slot_bitmap);
}

struct page *gva_to_page(struct kvm_vcpu *vcpu, gva_t gva)
{
	struct page *page;

	gpa_t gpa = vcpu->arch.mmu.gva_to_gpa(vcpu, gva);

	if (gpa == UNMAPPED_GVA)
		return NULL;

	down_read(&current->mm->mmap_sem);
	page = gfn_to_page(vcpu->kvm, gpa >> PAGE_SHIFT);
	up_read(&current->mm->mmap_sem);

	return page;
}

static void mmu_set_spte(struct kvm_vcpu *vcpu, u64 *shadow_pte,
			 unsigned pt_access, unsigned pte_access,
			 int user_fault, int write_fault, int dirty,
			 int *ptwrite, int largepage, gfn_t gfn,
			 pfn_t pfn, bool speculative)
{
	u64 spte;
	int was_rmapped = 0;
	int was_writeble = is_writeble_pte(*shadow_pte);

	pgprintk("%s: spte %llx access %x write_fault %d"
		 " user_fault %d gfn %lx\n",
		 __func__, *shadow_pte, pt_access,
		 write_fault, user_fault, gfn);

	if (is_rmap_pte(*shadow_pte)) {
		/*
		 * If we overwrite a PTE page pointer with a 2MB PMD, unlink
		 * the parent of the now unreachable PTE.
		 */
		if (largepage && !is_large_pte(*shadow_pte)) {
			struct kvm_mmu_page *child;
			u64 pte = *shadow_pte;

			child = page_header(pte & PT64_BASE_ADDR_MASK);
			mmu_page_remove_parent_pte(child, shadow_pte);
		} else if (pfn != spte_to_pfn(*shadow_pte)) {
			pgprintk("hfn old %lx new %lx\n",
				 spte_to_pfn(*shadow_pte), pfn);
			rmap_remove(vcpu->kvm, shadow_pte);
		} else {
			if (largepage)
				was_rmapped = is_large_pte(*shadow_pte);
			else
				was_rmapped = 1;
		}
	}

	/*
	 * We don't set the accessed bit, since we sometimes want to see
	 * whether the guest actually used the pte (in order to detect
	 * demand paging).
	 */
	spte = PT_PRESENT_MASK | PT_DIRTY_MASK;
	if (!speculative)
		pte_access |= PT_ACCESSED_MASK;
	if (!dirty)
		pte_access &= ~ACC_WRITE_MASK;
	if (!(pte_access & ACC_EXEC_MASK))
		spte |= PT64_NX_MASK;

	spte |= PT_PRESENT_MASK;
	if (pte_access & ACC_USER_MASK)
		spte |= PT_USER_MASK;
	if (largepage)
		spte |= PT_PAGE_SIZE_MASK;

	spte |= (u64)pfn << PAGE_SHIFT;

	if ((pte_access & ACC_WRITE_MASK)
	    || (write_fault && !is_write_protection(vcpu) && !user_fault)) {
		struct kvm_mmu_page *shadow;

		spte |= PT_WRITABLE_MASK;
		if (user_fault) {
			mmu_unshadow(vcpu->kvm, gfn);
			goto unshadowed;
		}

		shadow = kvm_mmu_lookup_page(vcpu->kvm, gfn);
		if (shadow ||
		   (largepage && has_wrprotected_page(vcpu->kvm, gfn))) {
			pgprintk("%s: found shadow page for %lx, marking ro\n",
				 __func__, gfn);
			pte_access &= ~ACC_WRITE_MASK;
			if (is_writeble_pte(spte)) {
				spte &= ~PT_WRITABLE_MASK;
				kvm_x86_ops->tlb_flush(vcpu);
			}
			if (write_fault)
				*ptwrite = 1;
		}
	}

unshadowed:

	if (pte_access & ACC_WRITE_MASK)
		mark_page_dirty(vcpu->kvm, gfn);

	pgprintk("%s: setting spte %llx\n", __func__, spte);
	pgprintk("instantiating %s PTE (%s) at %d (%llx) addr %llx\n",
		 (spte&PT_PAGE_SIZE_MASK)? "2MB" : "4kB",
		 (spte&PT_WRITABLE_MASK)?"RW":"R", gfn, spte, shadow_pte);
	set_shadow_pte(shadow_pte, spte);
	if (!was_rmapped && (spte & PT_PAGE_SIZE_MASK)
	    && (spte & PT_PRESENT_MASK))
		++vcpu->kvm->stat.lpages;

	page_header_update_slot(vcpu->kvm, shadow_pte, gfn);
	if (!was_rmapped) {
		rmap_add(vcpu, shadow_pte, gfn, largepage);
		if (!is_rmap_pte(*shadow_pte))
			kvm_release_pfn_clean(pfn);
	} else {
		if (was_writeble)
			kvm_release_pfn_dirty(pfn);
		else
			kvm_release_pfn_clean(pfn);
	}
	if (!ptwrite || !*ptwrite)
		vcpu->arch.last_pte_updated = shadow_pte;
}

static void nonpaging_new_cr3(struct kvm_vcpu *vcpu)
{
}

static int __direct_map(struct kvm_vcpu *vcpu, gpa_t v, int write,
			   int largepage, gfn_t gfn, pfn_t pfn,
			   int level)
{
	hpa_t table_addr = vcpu->arch.mmu.root_hpa;
	int pt_write = 0;

	for (; ; level--) {
		u32 index = PT64_INDEX(v, level);
		u64 *table;

		ASSERT(VALID_PAGE(table_addr));
		table = __va(table_addr);

		if (level == 1) {
			mmu_set_spte(vcpu, &table[index], ACC_ALL, ACC_ALL,
				     0, write, 1, &pt_write, 0, gfn, pfn, false);
			return pt_write;
		}

		if (largepage && level == 2) {
			mmu_set_spte(vcpu, &table[index], ACC_ALL, ACC_ALL,
				     0, write, 1, &pt_write, 1, gfn, pfn, false);
			return pt_write;
		}

		if (table[index] == shadow_trap_nonpresent_pte) {
			struct kvm_mmu_page *new_table;
			gfn_t pseudo_gfn;

			pseudo_gfn = (v & PT64_DIR_BASE_ADDR_MASK)
				>> PAGE_SHIFT;
			new_table = kvm_mmu_get_page(vcpu, pseudo_gfn,
						     v, level - 1,
						     1, ACC_ALL, &table[index]);
			if (!new_table) {
				pgprintk("nonpaging_map: ENOMEM\n");
				kvm_release_pfn_clean(pfn);
				return -ENOMEM;
			}

			table[index] = __pa(new_table->spt) | PT_PRESENT_MASK
				| PT_WRITABLE_MASK | PT_USER_MASK;
		}
		table_addr = table[index] & PT64_BASE_ADDR_MASK;
	}
}

static int nonpaging_map(struct kvm_vcpu *vcpu, gva_t v, int write, gfn_t gfn)
{
	int r;
	int largepage = 0;
	pfn_t pfn;

	down_read(&current->mm->mmap_sem);
	if (is_largepage_backed(vcpu, gfn & ~(KVM_PAGES_PER_HPAGE-1))) {
		gfn &= ~(KVM_PAGES_PER_HPAGE-1);
		largepage = 1;
	}

	pfn = gfn_to_pfn(vcpu->kvm, gfn);
	up_read(&current->mm->mmap_sem);

	/* mmio */
	if (is_error_pfn(pfn)) {
		kvm_release_pfn_clean(pfn);
		return 1;
	}

	spin_lock(&vcpu->kvm->mmu_lock);
	kvm_mmu_free_some_pages(vcpu);
	r = __direct_map(vcpu, v, write, largepage, gfn, pfn,
			 PT32E_ROOT_LEVEL);
	spin_unlock(&vcpu->kvm->mmu_lock);


	return r;
}


static void nonpaging_prefetch_page(struct kvm_vcpu *vcpu,
				    struct kvm_mmu_page *sp)
{
	int i;

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i)
		sp->spt[i] = shadow_trap_nonpresent_pte;
}

static void mmu_free_roots(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_mmu_page *sp;

	if (!VALID_PAGE(vcpu->arch.mmu.root_hpa))
		return;
	spin_lock(&vcpu->kvm->mmu_lock);
#ifdef CONFIG_X86_64
	if (vcpu->arch.mmu.shadow_root_level == PT64_ROOT_LEVEL) {
		hpa_t root = vcpu->arch.mmu.root_hpa;

		sp = page_header(root);
		--sp->root_count;
		if (!sp->root_count && sp->role.invalid)
			kvm_mmu_zap_page(vcpu->kvm, sp);
		vcpu->arch.mmu.root_hpa = INVALID_PAGE;
		spin_unlock(&vcpu->kvm->mmu_lock);
		return;
	}
#endif
	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu.pae_root[i];

		if (root) {
			root &= PT64_BASE_ADDR_MASK;
			sp = page_header(root);
			--sp->root_count;
			if (!sp->root_count && sp->role.invalid)
				kvm_mmu_zap_page(vcpu->kvm, sp);
		}
		vcpu->arch.mmu.pae_root[i] = INVALID_PAGE;
	}
	spin_unlock(&vcpu->kvm->mmu_lock);
	vcpu->arch.mmu.root_hpa = INVALID_PAGE;
}

static void mmu_alloc_roots(struct kvm_vcpu *vcpu)
{
	int i;
	gfn_t root_gfn;
	struct kvm_mmu_page *sp;
	int metaphysical = 0;

	root_gfn = vcpu->arch.cr3 >> PAGE_SHIFT;

#ifdef CONFIG_X86_64
	if (vcpu->arch.mmu.shadow_root_level == PT64_ROOT_LEVEL) {
		hpa_t root = vcpu->arch.mmu.root_hpa;

		ASSERT(!VALID_PAGE(root));
		if (tdp_enabled)
			metaphysical = 1;
		sp = kvm_mmu_get_page(vcpu, root_gfn, 0,
				      PT64_ROOT_LEVEL, metaphysical,
				      ACC_ALL, NULL);
		root = __pa(sp->spt);
		++sp->root_count;
		vcpu->arch.mmu.root_hpa = root;
		return;
	}
#endif
	metaphysical = !is_paging(vcpu);
	if (tdp_enabled)
		metaphysical = 1;
	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu.pae_root[i];

		ASSERT(!VALID_PAGE(root));
		if (vcpu->arch.mmu.root_level == PT32E_ROOT_LEVEL) {
			if (!is_present_pte(vcpu->arch.pdptrs[i])) {
				vcpu->arch.mmu.pae_root[i] = 0;
				continue;
			}
			root_gfn = vcpu->arch.pdptrs[i] >> PAGE_SHIFT;
		} else if (vcpu->arch.mmu.root_level == 0)
			root_gfn = 0;
		sp = kvm_mmu_get_page(vcpu, root_gfn, i << 30,
				      PT32_ROOT_LEVEL, metaphysical,
				      ACC_ALL, NULL);
		root = __pa(sp->spt);
		++sp->root_count;
		vcpu->arch.mmu.pae_root[i] = root | PT_PRESENT_MASK;
	}
	vcpu->arch.mmu.root_hpa = __pa(vcpu->arch.mmu.pae_root);
}

static gpa_t nonpaging_gva_to_gpa(struct kvm_vcpu *vcpu, gva_t vaddr)
{
	return vaddr;
}

static int nonpaging_page_fault(struct kvm_vcpu *vcpu, gva_t gva,
				u32 error_code)
{
	gfn_t gfn;
	int r;

	pgprintk("%s: gva %lx error %x\n", __func__, gva, error_code);
	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	ASSERT(vcpu);
	ASSERT(VALID_PAGE(vcpu->arch.mmu.root_hpa));

	gfn = gva >> PAGE_SHIFT;

	return nonpaging_map(vcpu, gva & PAGE_MASK,
			     error_code & PFERR_WRITE_MASK, gfn);
}

static int tdp_page_fault(struct kvm_vcpu *vcpu, gva_t gpa,
				u32 error_code)
{
	pfn_t pfn;
	int r;
	int largepage = 0;
	gfn_t gfn = gpa >> PAGE_SHIFT;

	ASSERT(vcpu);
	ASSERT(VALID_PAGE(vcpu->arch.mmu.root_hpa));

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	down_read(&current->mm->mmap_sem);
	if (is_largepage_backed(vcpu, gfn & ~(KVM_PAGES_PER_HPAGE-1))) {
		gfn &= ~(KVM_PAGES_PER_HPAGE-1);
		largepage = 1;
	}
	pfn = gfn_to_pfn(vcpu->kvm, gfn);
	up_read(&current->mm->mmap_sem);
	if (is_error_pfn(pfn)) {
		kvm_release_pfn_clean(pfn);
		return 1;
	}
	spin_lock(&vcpu->kvm->mmu_lock);
	kvm_mmu_free_some_pages(vcpu);
	r = __direct_map(vcpu, gpa, error_code & PFERR_WRITE_MASK,
			 largepage, gfn, pfn, TDP_ROOT_LEVEL);
	spin_unlock(&vcpu->kvm->mmu_lock);

	return r;
}

static void nonpaging_free(struct kvm_vcpu *vcpu)
{
	mmu_free_roots(vcpu);
}

static int nonpaging_init_context(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->arch.mmu;

	context->new_cr3 = nonpaging_new_cr3;
	context->page_fault = nonpaging_page_fault;
	context->gva_to_gpa = nonpaging_gva_to_gpa;
	context->free = nonpaging_free;
	context->prefetch_page = nonpaging_prefetch_page;
	context->root_level = 0;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->root_hpa = INVALID_PAGE;
	return 0;
}

void kvm_mmu_flush_tlb(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.tlb_flush;
	kvm_x86_ops->tlb_flush(vcpu);
}

static void paging_new_cr3(struct kvm_vcpu *vcpu)
{
	pgprintk("%s: cr3 %lx\n", __func__, vcpu->arch.cr3);
	mmu_free_roots(vcpu);
}

static void inject_page_fault(struct kvm_vcpu *vcpu,
			      u64 addr,
			      u32 err_code)
{
	kvm_inject_page_fault(vcpu, addr, err_code);
}

static void paging_free(struct kvm_vcpu *vcpu)
{
	nonpaging_free(vcpu);
}

#define PTTYPE 64
#include "paging_tmpl.h"
#undef PTTYPE

#define PTTYPE 32
#include "paging_tmpl.h"
#undef PTTYPE

static int paging64_init_context_common(struct kvm_vcpu *vcpu, int level)
{
	struct kvm_mmu *context = &vcpu->arch.mmu;

	ASSERT(is_pae(vcpu));
	context->new_cr3 = paging_new_cr3;
	context->page_fault = paging64_page_fault;
	context->gva_to_gpa = paging64_gva_to_gpa;
	context->prefetch_page = paging64_prefetch_page;
	context->free = paging_free;
	context->root_level = level;
	context->shadow_root_level = level;
	context->root_hpa = INVALID_PAGE;
	return 0;
}

static int paging64_init_context(struct kvm_vcpu *vcpu)
{
	return paging64_init_context_common(vcpu, PT64_ROOT_LEVEL);
}

static int paging32_init_context(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->arch.mmu;

	context->new_cr3 = paging_new_cr3;
	context->page_fault = paging32_page_fault;
	context->gva_to_gpa = paging32_gva_to_gpa;
	context->free = paging_free;
	context->prefetch_page = paging32_prefetch_page;
	context->root_level = PT32_ROOT_LEVEL;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->root_hpa = INVALID_PAGE;
	return 0;
}

static int paging32E_init_context(struct kvm_vcpu *vcpu)
{
	return paging64_init_context_common(vcpu, PT32E_ROOT_LEVEL);
}

static int init_kvm_tdp_mmu(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->arch.mmu;

	context->new_cr3 = nonpaging_new_cr3;
	context->page_fault = tdp_page_fault;
	context->free = nonpaging_free;
	context->prefetch_page = nonpaging_prefetch_page;
	context->shadow_root_level = TDP_ROOT_LEVEL;
	context->root_hpa = INVALID_PAGE;

	if (!is_paging(vcpu)) {
		context->gva_to_gpa = nonpaging_gva_to_gpa;
		context->root_level = 0;
	} else if (is_long_mode(vcpu)) {
		context->gva_to_gpa = paging64_gva_to_gpa;
		context->root_level = PT64_ROOT_LEVEL;
	} else if (is_pae(vcpu)) {
		context->gva_to_gpa = paging64_gva_to_gpa;
		context->root_level = PT32E_ROOT_LEVEL;
	} else {
		context->gva_to_gpa = paging32_gva_to_gpa;
		context->root_level = PT32_ROOT_LEVEL;
	}

	return 0;
}

static int init_kvm_softmmu(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->arch.mmu.root_hpa));

	if (!is_paging(vcpu))
		return nonpaging_init_context(vcpu);
	else if (is_long_mode(vcpu))
		return paging64_init_context(vcpu);
	else if (is_pae(vcpu))
		return paging32E_init_context(vcpu);
	else
		return paging32_init_context(vcpu);
}

static int init_kvm_mmu(struct kvm_vcpu *vcpu)
{
	vcpu->arch.update_pte.pfn = bad_pfn;

	if (tdp_enabled)
		return init_kvm_tdp_mmu(vcpu);
	else
		return init_kvm_softmmu(vcpu);
}

static void destroy_kvm_mmu(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	if (VALID_PAGE(vcpu->arch.mmu.root_hpa)) {
		vcpu->arch.mmu.free(vcpu);
		vcpu->arch.mmu.root_hpa = INVALID_PAGE;
	}
}

int kvm_mmu_reset_context(struct kvm_vcpu *vcpu)
{
	destroy_kvm_mmu(vcpu);
	return init_kvm_mmu(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_mmu_reset_context);

int kvm_mmu_load(struct kvm_vcpu *vcpu)
{
	int r;

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		goto out;
	spin_lock(&vcpu->kvm->mmu_lock);
	kvm_mmu_free_some_pages(vcpu);
	mmu_alloc_roots(vcpu);
	spin_unlock(&vcpu->kvm->mmu_lock);
	kvm_x86_ops->set_cr3(vcpu, vcpu->arch.mmu.root_hpa);
	kvm_mmu_flush_tlb(vcpu);
out:
	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_load);

void kvm_mmu_unload(struct kvm_vcpu *vcpu)
{
	mmu_free_roots(vcpu);
}

static void mmu_pte_write_zap_pte(struct kvm_vcpu *vcpu,
				  struct kvm_mmu_page *sp,
				  u64 *spte)
{
	u64 pte;
	struct kvm_mmu_page *child;

	pte = *spte;
	if (is_shadow_present_pte(pte)) {
		if (sp->role.level == PT_PAGE_TABLE_LEVEL ||
		    is_large_pte(pte))
			rmap_remove(vcpu->kvm, spte);
		else {
			child = page_header(pte & PT64_BASE_ADDR_MASK);
			mmu_page_remove_parent_pte(child, spte);
		}
	}
	set_shadow_pte(spte, shadow_trap_nonpresent_pte);
	if (is_large_pte(pte))
		--vcpu->kvm->stat.lpages;
}

static void mmu_pte_write_new_pte(struct kvm_vcpu *vcpu,
				  struct kvm_mmu_page *sp,
				  u64 *spte,
				  const void *new)
{
	if ((sp->role.level != PT_PAGE_TABLE_LEVEL)
	    && !vcpu->arch.update_pte.largepage) {
		++vcpu->kvm->stat.mmu_pde_zapped;
		return;
	}

	++vcpu->kvm->stat.mmu_pte_updated;
	if (sp->role.glevels == PT32_ROOT_LEVEL)
		paging32_update_pte(vcpu, sp, spte, new);
	else
		paging64_update_pte(vcpu, sp, spte, new);
}

static bool need_remote_flush(u64 old, u64 new)
{
	if (!is_shadow_present_pte(old))
		return false;
	if (!is_shadow_present_pte(new))
		return true;
	if ((old ^ new) & PT64_BASE_ADDR_MASK)
		return true;
	old ^= PT64_NX_MASK;
	new ^= PT64_NX_MASK;
	return (old & ~new & PT64_PERM_MASK) != 0;
}

static void mmu_pte_write_flush_tlb(struct kvm_vcpu *vcpu, u64 old, u64 new)
{
	if (need_remote_flush(old, new))
		kvm_flush_remote_tlbs(vcpu->kvm);
	else
		kvm_mmu_flush_tlb(vcpu);
}

static bool last_updated_pte_accessed(struct kvm_vcpu *vcpu)
{
	u64 *spte = vcpu->arch.last_pte_updated;

	return !!(spte && (*spte & PT_ACCESSED_MASK));
}

static void mmu_guess_page_from_pte_write(struct kvm_vcpu *vcpu, gpa_t gpa,
					  const u8 *new, int bytes)
{
	gfn_t gfn;
	int r;
	u64 gpte = 0;
	pfn_t pfn;

	vcpu->arch.update_pte.largepage = 0;

	if (bytes != 4 && bytes != 8)
		return;

	/*
	 * Assume that the pte write on a page table of the same type
	 * as the current vcpu paging mode.  This is nearly always true
	 * (might be false while changing modes).  Note it is verified later
	 * by update_pte().
	 */
	if (is_pae(vcpu)) {
		/* Handle a 32-bit guest writing two halves of a 64-bit gpte */
		if ((bytes == 4) && (gpa % 4 == 0)) {
			r = kvm_read_guest(vcpu->kvm, gpa & ~(u64)7, &gpte, 8);
			if (r)
				return;
			memcpy((void *)&gpte + (gpa % 8), new, 4);
		} else if ((bytes == 8) && (gpa % 8 == 0)) {
			memcpy((void *)&gpte, new, 8);
		}
	} else {
		if ((bytes == 4) && (gpa % 4 == 0))
			memcpy((void *)&gpte, new, 4);
	}
	if (!is_present_pte(gpte))
		return;
	gfn = (gpte & PT64_BASE_ADDR_MASK) >> PAGE_SHIFT;

	down_read(&current->mm->mmap_sem);
	if (is_large_pte(gpte) && is_largepage_backed(vcpu, gfn)) {
		gfn &= ~(KVM_PAGES_PER_HPAGE-1);
		vcpu->arch.update_pte.largepage = 1;
	}
	pfn = gfn_to_pfn(vcpu->kvm, gfn);
	up_read(&current->mm->mmap_sem);

	if (is_error_pfn(pfn)) {
		kvm_release_pfn_clean(pfn);
		return;
	}
	vcpu->arch.update_pte.gfn = gfn;
	vcpu->arch.update_pte.pfn = pfn;
}

void kvm_mmu_pte_write(struct kvm_vcpu *vcpu, gpa_t gpa,
		       const u8 *new, int bytes)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct kvm_mmu_page *sp;
	struct hlist_node *node, *n;
	struct hlist_head *bucket;
	unsigned index;
	u64 entry, gentry;
	u64 *spte;
	unsigned offset = offset_in_page(gpa);
	unsigned pte_size;
	unsigned page_offset;
	unsigned misaligned;
	unsigned quadrant;
	int level;
	int flooded = 0;
	int npte;
	int r;

	pgprintk("%s: gpa %llx bytes %d\n", __func__, gpa, bytes);
	mmu_guess_page_from_pte_write(vcpu, gpa, new, bytes);
	spin_lock(&vcpu->kvm->mmu_lock);
	kvm_mmu_free_some_pages(vcpu);
	++vcpu->kvm->stat.mmu_pte_write;
	kvm_mmu_audit(vcpu, "pre pte write");
	if (gfn == vcpu->arch.last_pt_write_gfn
	    && !last_updated_pte_accessed(vcpu)) {
		++vcpu->arch.last_pt_write_count;
		if (vcpu->arch.last_pt_write_count >= 3)
			flooded = 1;
	} else {
		vcpu->arch.last_pt_write_gfn = gfn;
		vcpu->arch.last_pt_write_count = 1;
		vcpu->arch.last_pte_updated = NULL;
	}
	index = kvm_page_table_hashfn(gfn);
	bucket = &vcpu->kvm->arch.mmu_page_hash[index];
	hlist_for_each_entry_safe(sp, node, n, bucket, hash_link) {
		if (sp->gfn != gfn || sp->role.metaphysical)
			continue;
		pte_size = sp->role.glevels == PT32_ROOT_LEVEL ? 4 : 8;
		misaligned = (offset ^ (offset + bytes - 1)) & ~(pte_size - 1);
		misaligned |= bytes < 4;
		if (misaligned || flooded) {
			/*
			 * Misaligned accesses are too much trouble to fix
			 * up; also, they usually indicate a page is not used
			 * as a page table.
			 *
			 * If we're seeing too many writes to a page,
			 * it may no longer be a page table, or we may be
			 * forking, in which case it is better to unmap the
			 * page.
			 */
			pgprintk("misaligned: gpa %llx bytes %d role %x\n",
				 gpa, bytes, sp->role.word);
			kvm_mmu_zap_page(vcpu->kvm, sp);
			++vcpu->kvm->stat.mmu_flooded;
			continue;
		}
		page_offset = offset;
		level = sp->role.level;
		npte = 1;
		if (sp->role.glevels == PT32_ROOT_LEVEL) {
			page_offset <<= 1;	/* 32->64 */
			/*
			 * A 32-bit pde maps 4MB while the shadow pdes map
			 * only 2MB.  So we need to double the offset again
			 * and zap two pdes instead of one.
			 */
			if (level == PT32_ROOT_LEVEL) {
				page_offset &= ~7; /* kill rounding error */
				page_offset <<= 1;
				npte = 2;
			}
			quadrant = page_offset >> PAGE_SHIFT;
			page_offset &= ~PAGE_MASK;
			if (quadrant != sp->role.quadrant)
				continue;
		}
		spte = &sp->spt[page_offset / sizeof(*spte)];
		if ((gpa & (pte_size - 1)) || (bytes < pte_size)) {
			gentry = 0;
			r = kvm_read_guest_atomic(vcpu->kvm,
						  gpa & ~(u64)(pte_size - 1),
						  &gentry, pte_size);
			new = (const void *)&gentry;
			if (r < 0)
				new = NULL;
		}
		while (npte--) {
			entry = *spte;
			mmu_pte_write_zap_pte(vcpu, sp, spte);
			if (new)
				mmu_pte_write_new_pte(vcpu, sp, spte, new);
			mmu_pte_write_flush_tlb(vcpu, entry, *spte);
			++spte;
		}
	}
	kvm_mmu_audit(vcpu, "post pte write");
	spin_unlock(&vcpu->kvm->mmu_lock);
	if (!is_error_pfn(vcpu->arch.update_pte.pfn)) {
		kvm_release_pfn_clean(vcpu->arch.update_pte.pfn);
		vcpu->arch.update_pte.pfn = bad_pfn;
	}
}

int kvm_mmu_unprotect_page_virt(struct kvm_vcpu *vcpu, gva_t gva)
{
	gpa_t gpa;
	int r;

	gpa = vcpu->arch.mmu.gva_to_gpa(vcpu, gva);

	spin_lock(&vcpu->kvm->mmu_lock);
	r = kvm_mmu_unprotect_page(vcpu->kvm, gpa >> PAGE_SHIFT);
	spin_unlock(&vcpu->kvm->mmu_lock);
	return r;
}

void __kvm_mmu_free_some_pages(struct kvm_vcpu *vcpu)
{
	while (vcpu->kvm->arch.n_free_mmu_pages < KVM_REFILL_PAGES) {
		struct kvm_mmu_page *sp;

		sp = container_of(vcpu->kvm->arch.active_mmu_pages.prev,
				  struct kvm_mmu_page, link);
		kvm_mmu_zap_page(vcpu->kvm, sp);
		++vcpu->kvm->stat.mmu_recycled;
	}
}

int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gva_t cr2, u32 error_code)
{
	int r;
	enum emulation_result er;

	r = vcpu->arch.mmu.page_fault(vcpu, cr2, error_code);
	if (r < 0)
		goto out;

	if (!r) {
		r = 1;
		goto out;
	}

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		goto out;

	er = emulate_instruction(vcpu, vcpu->run, cr2, error_code, 0);

	switch (er) {
	case EMULATE_DONE:
		return 1;
	case EMULATE_DO_MMIO:
		++vcpu->stat.mmio_exits;
		return 0;
	case EMULATE_FAIL:
		kvm_report_emulation_failure(vcpu, "pagetable");
		return 1;
	default:
		BUG();
	}
out:
	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_page_fault);

void kvm_enable_tdp(void)
{
	tdp_enabled = true;
}
EXPORT_SYMBOL_GPL(kvm_enable_tdp);

static void free_mmu_pages(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *sp;

	while (!list_empty(&vcpu->kvm->arch.active_mmu_pages)) {
		sp = container_of(vcpu->kvm->arch.active_mmu_pages.next,
				  struct kvm_mmu_page, link);
		kvm_mmu_zap_page(vcpu->kvm, sp);
	}
	free_page((unsigned long)vcpu->arch.mmu.pae_root);
}

static int alloc_mmu_pages(struct kvm_vcpu *vcpu)
{
	struct page *page;
	int i;

	ASSERT(vcpu);

	if (vcpu->kvm->arch.n_requested_mmu_pages)
		vcpu->kvm->arch.n_free_mmu_pages =
					vcpu->kvm->arch.n_requested_mmu_pages;
	else
		vcpu->kvm->arch.n_free_mmu_pages =
					vcpu->kvm->arch.n_alloc_mmu_pages;
	/*
	 * When emulating 32-bit mode, cr3 is only 32 bits even on x86_64.
	 * Therefore we need to allocate shadow page tables in the first
	 * 4GB of memory, which happens to fit the DMA32 zone.
	 */
	page = alloc_page(GFP_KERNEL | __GFP_DMA32);
	if (!page)
		goto error_1;
	vcpu->arch.mmu.pae_root = page_address(page);
	for (i = 0; i < 4; ++i)
		vcpu->arch.mmu.pae_root[i] = INVALID_PAGE;

	return 0;

error_1:
	free_mmu_pages(vcpu);
	return -ENOMEM;
}

int kvm_mmu_create(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->arch.mmu.root_hpa));

	return alloc_mmu_pages(vcpu);
}

int kvm_mmu_setup(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->arch.mmu.root_hpa));

	return init_kvm_mmu(vcpu);
}

void kvm_mmu_destroy(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);

	destroy_kvm_mmu(vcpu);
	free_mmu_pages(vcpu);
	mmu_free_memory_caches(vcpu);
}

void kvm_mmu_slot_remove_write_access(struct kvm *kvm, int slot)
{
	struct kvm_mmu_page *sp;

	list_for_each_entry(sp, &kvm->arch.active_mmu_pages, link) {
		int i;
		u64 *pt;

		if (!test_bit(slot, &sp->slot_bitmap))
			continue;

		pt = sp->spt;
		for (i = 0; i < PT64_ENT_PER_PAGE; ++i)
			/* avoid RMW */
			if (pt[i] & PT_WRITABLE_MASK)
				pt[i] &= ~PT_WRITABLE_MASK;
	}
}

void kvm_mmu_zap_all(struct kvm *kvm)
{
	struct kvm_mmu_page *sp, *node;

	spin_lock(&kvm->mmu_lock);
	list_for_each_entry_safe(sp, node, &kvm->arch.active_mmu_pages, link)
		kvm_mmu_zap_page(kvm, sp);
	spin_unlock(&kvm->mmu_lock);

	kvm_flush_remote_tlbs(kvm);
}

void kvm_mmu_remove_one_alloc_mmu_page(struct kvm *kvm)
{
	struct kvm_mmu_page *page;

	page = container_of(kvm->arch.active_mmu_pages.prev,
			    struct kvm_mmu_page, link);
	kvm_mmu_zap_page(kvm, page);
}

static int mmu_shrink(int nr_to_scan, gfp_t gfp_mask)
{
	struct kvm *kvm;
	struct kvm *kvm_freed = NULL;
	int cache_count = 0;

	spin_lock(&kvm_lock);

	list_for_each_entry(kvm, &vm_list, vm_list) {
		int npages;

		spin_lock(&kvm->mmu_lock);
		npages = kvm->arch.n_alloc_mmu_pages -
			 kvm->arch.n_free_mmu_pages;
		cache_count += npages;
		if (!kvm_freed && nr_to_scan > 0 && npages > 0) {
			kvm_mmu_remove_one_alloc_mmu_page(kvm);
			cache_count--;
			kvm_freed = kvm;
		}
		nr_to_scan--;

		spin_unlock(&kvm->mmu_lock);
	}
	if (kvm_freed)
		list_move_tail(&kvm_freed->vm_list, &vm_list);

	spin_unlock(&kvm_lock);

	return cache_count;
}

static struct shrinker mmu_shrinker = {
	.shrink = mmu_shrink,
	.seeks = DEFAULT_SEEKS * 10,
};

void mmu_destroy_caches(void)
{
	if (pte_chain_cache)
		kmem_cache_destroy(pte_chain_cache);
	if (rmap_desc_cache)
		kmem_cache_destroy(rmap_desc_cache);
	if (mmu_page_header_cache)
		kmem_cache_destroy(mmu_page_header_cache);
}

void kvm_mmu_module_exit(void)
{
	mmu_destroy_caches();
	unregister_shrinker(&mmu_shrinker);
}

int kvm_mmu_module_init(void)
{
	pte_chain_cache = kmem_cache_create("kvm_pte_chain",
					    sizeof(struct kvm_pte_chain),
					    0, 0, NULL);
	if (!pte_chain_cache)
		goto nomem;
	rmap_desc_cache = kmem_cache_create("kvm_rmap_desc",
					    sizeof(struct kvm_rmap_desc),
					    0, 0, NULL);
	if (!rmap_desc_cache)
		goto nomem;

	mmu_page_header_cache = kmem_cache_create("kvm_mmu_page_header",
						  sizeof(struct kvm_mmu_page),
						  0, 0, NULL);
	if (!mmu_page_header_cache)
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
	int i;
	unsigned int nr_mmu_pages;
	unsigned int  nr_pages = 0;

	for (i = 0; i < kvm->nmemslots; i++)
		nr_pages += kvm->memslots[i].npages;

	nr_mmu_pages = nr_pages * KVM_PERMILLE_MMU_PAGES / 1000;
	nr_mmu_pages = max(nr_mmu_pages,
			(unsigned int) KVM_MIN_ALLOC_MMU_PAGES);

	return nr_mmu_pages;
}

static void *pv_mmu_peek_buffer(struct kvm_pv_mmu_op_buffer *buffer,
				unsigned len)
{
	if (len > buffer->len)
		return NULL;
	return buffer->ptr;
}

static void *pv_mmu_read_buffer(struct kvm_pv_mmu_op_buffer *buffer,
				unsigned len)
{
	void *ret;

	ret = pv_mmu_peek_buffer(buffer, len);
	if (!ret)
		return ret;
	buffer->ptr += len;
	buffer->len -= len;
	buffer->processed += len;
	return ret;
}

static int kvm_pv_mmu_write(struct kvm_vcpu *vcpu,
			     gpa_t addr, gpa_t value)
{
	int bytes = 8;
	int r;

	if (!is_long_mode(vcpu) && !is_pae(vcpu))
		bytes = 4;

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	if (!emulator_write_phys(vcpu, addr, &value, bytes))
		return -EFAULT;

	return 1;
}

static int kvm_pv_mmu_flush_tlb(struct kvm_vcpu *vcpu)
{
	kvm_x86_ops->tlb_flush(vcpu);
	return 1;
}

static int kvm_pv_mmu_release_pt(struct kvm_vcpu *vcpu, gpa_t addr)
{
	spin_lock(&vcpu->kvm->mmu_lock);
	mmu_unshadow(vcpu->kvm, addr >> PAGE_SHIFT);
	spin_unlock(&vcpu->kvm->mmu_lock);
	return 1;
}

static int kvm_pv_mmu_op_one(struct kvm_vcpu *vcpu,
			     struct kvm_pv_mmu_op_buffer *buffer)
{
	struct kvm_mmu_op_header *header;

	header = pv_mmu_peek_buffer(buffer, sizeof *header);
	if (!header)
		return 0;
	switch (header->op) {
	case KVM_MMU_OP_WRITE_PTE: {
		struct kvm_mmu_op_write_pte *wpte;

		wpte = pv_mmu_read_buffer(buffer, sizeof *wpte);
		if (!wpte)
			return 0;
		return kvm_pv_mmu_write(vcpu, wpte->pte_phys,
					wpte->pte_val);
	}
	case KVM_MMU_OP_FLUSH_TLB: {
		struct kvm_mmu_op_flush_tlb *ftlb;

		ftlb = pv_mmu_read_buffer(buffer, sizeof *ftlb);
		if (!ftlb)
			return 0;
		return kvm_pv_mmu_flush_tlb(vcpu);
	}
	case KVM_MMU_OP_RELEASE_PT: {
		struct kvm_mmu_op_release_pt *rpt;

		rpt = pv_mmu_read_buffer(buffer, sizeof *rpt);
		if (!rpt)
			return 0;
		return kvm_pv_mmu_release_pt(vcpu, rpt->pt_phys);
	}
	default: return 0;
	}
}

int kvm_pv_mmu_op(struct kvm_vcpu *vcpu, unsigned long bytes,
		  gpa_t addr, unsigned long *ret)
{
	int r;
	struct kvm_pv_mmu_op_buffer buffer;

	buffer.ptr = buffer.buf;
	buffer.len = min_t(unsigned long, bytes, sizeof buffer.buf);
	buffer.processed = 0;

	r = kvm_read_guest(vcpu->kvm, addr, buffer.buf, buffer.len);
	if (r)
		goto out;

	while (buffer.len) {
		r = kvm_pv_mmu_op_one(vcpu, &buffer);
		if (r < 0)
			goto out;
		if (r == 0)
			break;
	}

	r = 1;
out:
	*ret = buffer.processed;
	return r;
}

#ifdef AUDIT

static const char *audit_msg;

static gva_t canonicalize(gva_t gva)
{
#ifdef CONFIG_X86_64
	gva = (long long)(gva << 16) >> 16;
#endif
	return gva;
}

static void audit_mappings_page(struct kvm_vcpu *vcpu, u64 page_pte,
				gva_t va, int level)
{
	u64 *pt = __va(page_pte & PT64_BASE_ADDR_MASK);
	int i;
	gva_t va_delta = 1ul << (PAGE_SHIFT + 9 * (level - 1));

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i, va += va_delta) {
		u64 ent = pt[i];

		if (ent == shadow_trap_nonpresent_pte)
			continue;

		va = canonicalize(va);
		if (level > 1) {
			if (ent == shadow_notrap_nonpresent_pte)
				printk(KERN_ERR "audit: (%s) nontrapping pte"
				       " in nonleaf level: levels %d gva %lx"
				       " level %d pte %llx\n", audit_msg,
				       vcpu->arch.mmu.root_level, va, level, ent);

			audit_mappings_page(vcpu, ent, va, level - 1);
		} else {
			gpa_t gpa = vcpu->arch.mmu.gva_to_gpa(vcpu, va);
			hpa_t hpa = (hpa_t)gpa_to_pfn(vcpu, gpa) << PAGE_SHIFT;

			if (is_shadow_present_pte(ent)
			    && (ent & PT64_BASE_ADDR_MASK) != hpa)
				printk(KERN_ERR "xx audit error: (%s) levels %d"
				       " gva %lx gpa %llx hpa %llx ent %llx %d\n",
				       audit_msg, vcpu->arch.mmu.root_level,
				       va, gpa, hpa, ent,
				       is_shadow_present_pte(ent));
			else if (ent == shadow_notrap_nonpresent_pte
				 && !is_error_hpa(hpa))
				printk(KERN_ERR "audit: (%s) notrap shadow,"
				       " valid guest gva %lx\n", audit_msg, va);
			kvm_release_pfn_clean(pfn);

		}
	}
}

static void audit_mappings(struct kvm_vcpu *vcpu)
{
	unsigned i;

	if (vcpu->arch.mmu.root_level == 4)
		audit_mappings_page(vcpu, vcpu->arch.mmu.root_hpa, 0, 4);
	else
		for (i = 0; i < 4; ++i)
			if (vcpu->arch.mmu.pae_root[i] & PT_PRESENT_MASK)
				audit_mappings_page(vcpu,
						    vcpu->arch.mmu.pae_root[i],
						    i << 30,
						    2);
}

static int count_rmaps(struct kvm_vcpu *vcpu)
{
	int nmaps = 0;
	int i, j, k;

	for (i = 0; i < KVM_MEMORY_SLOTS; ++i) {
		struct kvm_memory_slot *m = &vcpu->kvm->memslots[i];
		struct kvm_rmap_desc *d;

		for (j = 0; j < m->npages; ++j) {
			unsigned long *rmapp = &m->rmap[j];

			if (!*rmapp)
				continue;
			if (!(*rmapp & 1)) {
				++nmaps;
				continue;
			}
			d = (struct kvm_rmap_desc *)(*rmapp & ~1ul);
			while (d) {
				for (k = 0; k < RMAP_EXT; ++k)
					if (d->shadow_ptes[k])
						++nmaps;
					else
						break;
				d = d->more;
			}
		}
	}
	return nmaps;
}

static int count_writable_mappings(struct kvm_vcpu *vcpu)
{
	int nmaps = 0;
	struct kvm_mmu_page *sp;
	int i;

	list_for_each_entry(sp, &vcpu->kvm->arch.active_mmu_pages, link) {
		u64 *pt = sp->spt;

		if (sp->role.level != PT_PAGE_TABLE_LEVEL)
			continue;

		for (i = 0; i < PT64_ENT_PER_PAGE; ++i) {
			u64 ent = pt[i];

			if (!(ent & PT_PRESENT_MASK))
				continue;
			if (!(ent & PT_WRITABLE_MASK))
				continue;
			++nmaps;
		}
	}
	return nmaps;
}

static void audit_rmap(struct kvm_vcpu *vcpu)
{
	int n_rmap = count_rmaps(vcpu);
	int n_actual = count_writable_mappings(vcpu);

	if (n_rmap != n_actual)
		printk(KERN_ERR "%s: (%s) rmap %d actual %d\n",
		       __func__, audit_msg, n_rmap, n_actual);
}

static void audit_write_protection(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *sp;
	struct kvm_memory_slot *slot;
	unsigned long *rmapp;
	gfn_t gfn;

	list_for_each_entry(sp, &vcpu->kvm->arch.active_mmu_pages, link) {
		if (sp->role.metaphysical)
			continue;

		slot = gfn_to_memslot(vcpu->kvm, sp->gfn);
		gfn = unalias_gfn(vcpu->kvm, sp->gfn);
		rmapp = &slot->rmap[gfn - slot->base_gfn];
		if (*rmapp)
			printk(KERN_ERR "%s: (%s) shadow page has writable"
			       " mappings: gfn %lx role %x\n",
			       __func__, audit_msg, sp->gfn,
			       sp->role.word);
	}
}

static void kvm_mmu_audit(struct kvm_vcpu *vcpu, const char *msg)
{
	int olddbg = dbg;

	dbg = 0;
	audit_msg = msg;
	audit_rmap(vcpu);
	audit_write_protection(vcpu);
	audit_mappings(vcpu);
	dbg = olddbg;
}

#endif
