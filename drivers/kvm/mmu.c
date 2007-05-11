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
#include <linux/types.h>
#include <linux/string.h>
#include <asm/page.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/module.h>

#include "vmx.h"
#include "kvm.h"

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
#define PT64_NX_MASK (1ULL << 63)

#define PT_PAT_SHIFT 7
#define PT_DIR_PAT_SHIFT 12
#define PT_DIR_PAT_MASK (1ULL << PT_DIR_PAT_SHIFT)

#define PT32_DIR_PSE36_SIZE 4
#define PT32_DIR_PSE36_SHIFT 13
#define PT32_DIR_PSE36_MASK (((1ULL << PT32_DIR_PSE36_SIZE) - 1) << PT32_DIR_PSE36_SHIFT)


#define PT32_PTE_COPY_MASK \
	(PT_PRESENT_MASK | PT_ACCESSED_MASK | PT_DIRTY_MASK | PT_GLOBAL_MASK)

#define PT64_PTE_COPY_MASK (PT64_NX_MASK | PT32_PTE_COPY_MASK)

#define PT_FIRST_AVAIL_BITS_SHIFT 9
#define PT64_SECOND_AVAIL_BITS_SHIFT 52

#define PT_SHADOW_PS_MARK (1ULL << PT_FIRST_AVAIL_BITS_SHIFT)
#define PT_SHADOW_IO_MARK (1ULL << PT_FIRST_AVAIL_BITS_SHIFT)

#define PT_SHADOW_WRITABLE_SHIFT (PT_FIRST_AVAIL_BITS_SHIFT + 1)
#define PT_SHADOW_WRITABLE_MASK (1ULL << PT_SHADOW_WRITABLE_SHIFT)

#define PT_SHADOW_USER_SHIFT (PT_SHADOW_WRITABLE_SHIFT + 1)
#define PT_SHADOW_USER_MASK (1ULL << (PT_SHADOW_USER_SHIFT))

#define PT_SHADOW_BITS_OFFSET (PT_SHADOW_WRITABLE_SHIFT - PT_WRITABLE_SHIFT)

#define VALID_PAGE(x) ((x) != INVALID_PAGE)

#define PT64_LEVEL_BITS 9

#define PT64_LEVEL_SHIFT(level) \
		( PAGE_SHIFT + (level - 1) * PT64_LEVEL_BITS )

#define PT64_LEVEL_MASK(level) \
		(((1ULL << PT64_LEVEL_BITS) - 1) << PT64_LEVEL_SHIFT(level))

#define PT64_INDEX(address, level)\
	(((address) >> PT64_LEVEL_SHIFT(level)) & ((1 << PT64_LEVEL_BITS) - 1))


#define PT32_LEVEL_BITS 10

#define PT32_LEVEL_SHIFT(level) \
		( PAGE_SHIFT + (level - 1) * PT32_LEVEL_BITS )

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

struct kvm_rmap_desc {
	u64 *shadow_ptes[RMAP_EXT];
	struct kvm_rmap_desc *more;
};

static struct kmem_cache *pte_chain_cache;
static struct kmem_cache *rmap_desc_cache;

static int is_write_protection(struct kvm_vcpu *vcpu)
{
	return vcpu->cr0 & CR0_WP_MASK;
}

static int is_cpuid_PSE36(void)
{
	return 1;
}

static int is_nx(struct kvm_vcpu *vcpu)
{
	return vcpu->shadow_efer & EFER_NX;
}

static int is_present_pte(unsigned long pte)
{
	return pte & PT_PRESENT_MASK;
}

static int is_writeble_pte(unsigned long pte)
{
	return pte & PT_WRITABLE_MASK;
}

static int is_io_pte(unsigned long pte)
{
	return pte & PT_SHADOW_IO_MARK;
}

static int is_rmap_pte(u64 pte)
{
	return (pte & (PT_WRITABLE_MASK | PT_PRESENT_MASK))
		== (PT_WRITABLE_MASK | PT_PRESENT_MASK);
}

static int mmu_topup_memory_cache(struct kvm_mmu_memory_cache *cache,
				  struct kmem_cache *base_cache, int min,
				  gfp_t gfp_flags)
{
	void *obj;

	if (cache->nobjs >= min)
		return 0;
	while (cache->nobjs < ARRAY_SIZE(cache->objects)) {
		obj = kmem_cache_zalloc(base_cache, gfp_flags);
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

static int __mmu_topup_memory_caches(struct kvm_vcpu *vcpu, gfp_t gfp_flags)
{
	int r;

	r = mmu_topup_memory_cache(&vcpu->mmu_pte_chain_cache,
				   pte_chain_cache, 4, gfp_flags);
	if (r)
		goto out;
	r = mmu_topup_memory_cache(&vcpu->mmu_rmap_desc_cache,
				   rmap_desc_cache, 1, gfp_flags);
out:
	return r;
}

static int mmu_topup_memory_caches(struct kvm_vcpu *vcpu)
{
	int r;

	r = __mmu_topup_memory_caches(vcpu, GFP_NOWAIT);
	if (r < 0) {
		spin_unlock(&vcpu->kvm->lock);
		kvm_arch_ops->vcpu_put(vcpu);
		r = __mmu_topup_memory_caches(vcpu, GFP_KERNEL);
		kvm_arch_ops->vcpu_load(vcpu);
		spin_lock(&vcpu->kvm->lock);
	}
	return r;
}

static void mmu_free_memory_caches(struct kvm_vcpu *vcpu)
{
	mmu_free_memory_cache(&vcpu->mmu_pte_chain_cache);
	mmu_free_memory_cache(&vcpu->mmu_rmap_desc_cache);
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

static void mmu_memory_cache_free(struct kvm_mmu_memory_cache *mc, void *obj)
{
	if (mc->nobjs < KVM_NR_MEM_OBJS)
		mc->objects[mc->nobjs++] = obj;
	else
		kfree(obj);
}

static struct kvm_pte_chain *mmu_alloc_pte_chain(struct kvm_vcpu *vcpu)
{
	return mmu_memory_cache_alloc(&vcpu->mmu_pte_chain_cache,
				      sizeof(struct kvm_pte_chain));
}

static void mmu_free_pte_chain(struct kvm_vcpu *vcpu,
			       struct kvm_pte_chain *pc)
{
	mmu_memory_cache_free(&vcpu->mmu_pte_chain_cache, pc);
}

static struct kvm_rmap_desc *mmu_alloc_rmap_desc(struct kvm_vcpu *vcpu)
{
	return mmu_memory_cache_alloc(&vcpu->mmu_rmap_desc_cache,
				      sizeof(struct kvm_rmap_desc));
}

static void mmu_free_rmap_desc(struct kvm_vcpu *vcpu,
			       struct kvm_rmap_desc *rd)
{
	mmu_memory_cache_free(&vcpu->mmu_rmap_desc_cache, rd);
}

/*
 * Reverse mapping data structures:
 *
 * If page->private bit zero is zero, then page->private points to the
 * shadow page table entry that points to page_address(page).
 *
 * If page->private bit zero is one, (then page->private & ~1) points
 * to a struct kvm_rmap_desc containing more mappings.
 */
static void rmap_add(struct kvm_vcpu *vcpu, u64 *spte)
{
	struct page *page;
	struct kvm_rmap_desc *desc;
	int i;

	if (!is_rmap_pte(*spte))
		return;
	page = pfn_to_page((*spte & PT64_BASE_ADDR_MASK) >> PAGE_SHIFT);
	if (!page_private(page)) {
		rmap_printk("rmap_add: %p %llx 0->1\n", spte, *spte);
		set_page_private(page,(unsigned long)spte);
	} else if (!(page_private(page) & 1)) {
		rmap_printk("rmap_add: %p %llx 1->many\n", spte, *spte);
		desc = mmu_alloc_rmap_desc(vcpu);
		desc->shadow_ptes[0] = (u64 *)page_private(page);
		desc->shadow_ptes[1] = spte;
		set_page_private(page,(unsigned long)desc | 1);
	} else {
		rmap_printk("rmap_add: %p %llx many->many\n", spte, *spte);
		desc = (struct kvm_rmap_desc *)(page_private(page) & ~1ul);
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

static void rmap_desc_remove_entry(struct kvm_vcpu *vcpu,
				   struct page *page,
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
		set_page_private(page,(unsigned long)desc->shadow_ptes[0]);
	else
		if (prev_desc)
			prev_desc->more = desc->more;
		else
			set_page_private(page,(unsigned long)desc->more | 1);
	mmu_free_rmap_desc(vcpu, desc);
}

static void rmap_remove(struct kvm_vcpu *vcpu, u64 *spte)
{
	struct page *page;
	struct kvm_rmap_desc *desc;
	struct kvm_rmap_desc *prev_desc;
	int i;

	if (!is_rmap_pte(*spte))
		return;
	page = pfn_to_page((*spte & PT64_BASE_ADDR_MASK) >> PAGE_SHIFT);
	if (!page_private(page)) {
		printk(KERN_ERR "rmap_remove: %p %llx 0->BUG\n", spte, *spte);
		BUG();
	} else if (!(page_private(page) & 1)) {
		rmap_printk("rmap_remove:  %p %llx 1->0\n", spte, *spte);
		if ((u64 *)page_private(page) != spte) {
			printk(KERN_ERR "rmap_remove:  %p %llx 1->BUG\n",
			       spte, *spte);
			BUG();
		}
		set_page_private(page,0);
	} else {
		rmap_printk("rmap_remove:  %p %llx many->many\n", spte, *spte);
		desc = (struct kvm_rmap_desc *)(page_private(page) & ~1ul);
		prev_desc = NULL;
		while (desc) {
			for (i = 0; i < RMAP_EXT && desc->shadow_ptes[i]; ++i)
				if (desc->shadow_ptes[i] == spte) {
					rmap_desc_remove_entry(vcpu, page,
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

static void rmap_write_protect(struct kvm_vcpu *vcpu, u64 gfn)
{
	struct kvm *kvm = vcpu->kvm;
	struct page *page;
	struct kvm_rmap_desc *desc;
	u64 *spte;

	page = gfn_to_page(kvm, gfn);
	BUG_ON(!page);

	while (page_private(page)) {
		if (!(page_private(page) & 1))
			spte = (u64 *)page_private(page);
		else {
			desc = (struct kvm_rmap_desc *)(page_private(page) & ~1ul);
			spte = desc->shadow_ptes[0];
		}
		BUG_ON(!spte);
		BUG_ON((*spte & PT64_BASE_ADDR_MASK) >> PAGE_SHIFT
		       != page_to_pfn(page));
		BUG_ON(!(*spte & PT_PRESENT_MASK));
		BUG_ON(!(*spte & PT_WRITABLE_MASK));
		rmap_printk("rmap_write_protect: spte %p %llx\n", spte, *spte);
		rmap_remove(vcpu, spte);
		kvm_arch_ops->tlb_flush(vcpu);
		*spte &= ~(u64)PT_WRITABLE_MASK;
	}
}

#ifdef MMU_DEBUG
static int is_empty_shadow_page(hpa_t page_hpa)
{
	u64 *pos;
	u64 *end;

	for (pos = __va(page_hpa), end = pos + PAGE_SIZE / sizeof(u64);
		      pos != end; pos++)
		if (*pos != 0) {
			printk(KERN_ERR "%s: %p %llx\n", __FUNCTION__,
			       pos, *pos);
			return 0;
		}
	return 1;
}
#endif

static void kvm_mmu_free_page(struct kvm_vcpu *vcpu, hpa_t page_hpa)
{
	struct kvm_mmu_page *page_head = page_header(page_hpa);

	ASSERT(is_empty_shadow_page(page_hpa));
	page_head->page_hpa = page_hpa;
	list_move(&page_head->link, &vcpu->free_pages);
	++vcpu->kvm->n_free_mmu_pages;
}

static unsigned kvm_page_table_hashfn(gfn_t gfn)
{
	return gfn;
}

static struct kvm_mmu_page *kvm_mmu_alloc_page(struct kvm_vcpu *vcpu,
					       u64 *parent_pte)
{
	struct kvm_mmu_page *page;

	if (list_empty(&vcpu->free_pages))
		return NULL;

	page = list_entry(vcpu->free_pages.next, struct kvm_mmu_page, link);
	list_move(&page->link, &vcpu->kvm->active_mmu_pages);
	ASSERT(is_empty_shadow_page(page->page_hpa));
	page->slot_bitmap = 0;
	page->multimapped = 0;
	page->parent_pte = parent_pte;
	--vcpu->kvm->n_free_mmu_pages;
	return page;
}

static void mmu_page_add_parent_pte(struct kvm_vcpu *vcpu,
				    struct kvm_mmu_page *page, u64 *parent_pte)
{
	struct kvm_pte_chain *pte_chain;
	struct hlist_node *node;
	int i;

	if (!parent_pte)
		return;
	if (!page->multimapped) {
		u64 *old = page->parent_pte;

		if (!old) {
			page->parent_pte = parent_pte;
			return;
		}
		page->multimapped = 1;
		pte_chain = mmu_alloc_pte_chain(vcpu);
		INIT_HLIST_HEAD(&page->parent_ptes);
		hlist_add_head(&pte_chain->link, &page->parent_ptes);
		pte_chain->parent_ptes[0] = old;
	}
	hlist_for_each_entry(pte_chain, node, &page->parent_ptes, link) {
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
	hlist_add_head(&pte_chain->link, &page->parent_ptes);
	pte_chain->parent_ptes[0] = parent_pte;
}

static void mmu_page_remove_parent_pte(struct kvm_vcpu *vcpu,
				       struct kvm_mmu_page *page,
				       u64 *parent_pte)
{
	struct kvm_pte_chain *pte_chain;
	struct hlist_node *node;
	int i;

	if (!page->multimapped) {
		BUG_ON(page->parent_pte != parent_pte);
		page->parent_pte = NULL;
		return;
	}
	hlist_for_each_entry(pte_chain, node, &page->parent_ptes, link)
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
				mmu_free_pte_chain(vcpu, pte_chain);
				if (hlist_empty(&page->parent_ptes)) {
					page->multimapped = 0;
					page->parent_pte = NULL;
				}
			}
			return;
		}
	BUG();
}

static struct kvm_mmu_page *kvm_mmu_lookup_page(struct kvm_vcpu *vcpu,
						gfn_t gfn)
{
	unsigned index;
	struct hlist_head *bucket;
	struct kvm_mmu_page *page;
	struct hlist_node *node;

	pgprintk("%s: looking for gfn %lx\n", __FUNCTION__, gfn);
	index = kvm_page_table_hashfn(gfn) % KVM_NUM_MMU_PAGES;
	bucket = &vcpu->kvm->mmu_page_hash[index];
	hlist_for_each_entry(page, node, bucket, hash_link)
		if (page->gfn == gfn && !page->role.metaphysical) {
			pgprintk("%s: found role %x\n",
				 __FUNCTION__, page->role.word);
			return page;
		}
	return NULL;
}

static struct kvm_mmu_page *kvm_mmu_get_page(struct kvm_vcpu *vcpu,
					     gfn_t gfn,
					     gva_t gaddr,
					     unsigned level,
					     int metaphysical,
					     unsigned hugepage_access,
					     u64 *parent_pte)
{
	union kvm_mmu_page_role role;
	unsigned index;
	unsigned quadrant;
	struct hlist_head *bucket;
	struct kvm_mmu_page *page;
	struct hlist_node *node;

	role.word = 0;
	role.glevels = vcpu->mmu.root_level;
	role.level = level;
	role.metaphysical = metaphysical;
	role.hugepage_access = hugepage_access;
	if (vcpu->mmu.root_level <= PT32_ROOT_LEVEL) {
		quadrant = gaddr >> (PAGE_SHIFT + (PT64_PT_BITS * level));
		quadrant &= (1 << ((PT32_PT_BITS - PT64_PT_BITS) * level)) - 1;
		role.quadrant = quadrant;
	}
	pgprintk("%s: looking gfn %lx role %x\n", __FUNCTION__,
		 gfn, role.word);
	index = kvm_page_table_hashfn(gfn) % KVM_NUM_MMU_PAGES;
	bucket = &vcpu->kvm->mmu_page_hash[index];
	hlist_for_each_entry(page, node, bucket, hash_link)
		if (page->gfn == gfn && page->role.word == role.word) {
			mmu_page_add_parent_pte(vcpu, page, parent_pte);
			pgprintk("%s: found\n", __FUNCTION__);
			return page;
		}
	page = kvm_mmu_alloc_page(vcpu, parent_pte);
	if (!page)
		return page;
	pgprintk("%s: adding gfn %lx role %x\n", __FUNCTION__, gfn, role.word);
	page->gfn = gfn;
	page->role = role;
	hlist_add_head(&page->hash_link, bucket);
	if (!metaphysical)
		rmap_write_protect(vcpu, gfn);
	return page;
}

static void kvm_mmu_page_unlink_children(struct kvm_vcpu *vcpu,
					 struct kvm_mmu_page *page)
{
	unsigned i;
	u64 *pt;
	u64 ent;

	pt = __va(page->page_hpa);

	if (page->role.level == PT_PAGE_TABLE_LEVEL) {
		for (i = 0; i < PT64_ENT_PER_PAGE; ++i) {
			if (pt[i] & PT_PRESENT_MASK)
				rmap_remove(vcpu, &pt[i]);
			pt[i] = 0;
		}
		kvm_arch_ops->tlb_flush(vcpu);
		return;
	}

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i) {
		ent = pt[i];

		pt[i] = 0;
		if (!(ent & PT_PRESENT_MASK))
			continue;
		ent &= PT64_BASE_ADDR_MASK;
		mmu_page_remove_parent_pte(vcpu, page_header(ent), &pt[i]);
	}
}

static void kvm_mmu_put_page(struct kvm_vcpu *vcpu,
			     struct kvm_mmu_page *page,
			     u64 *parent_pte)
{
	mmu_page_remove_parent_pte(vcpu, page, parent_pte);
}

static void kvm_mmu_zap_page(struct kvm_vcpu *vcpu,
			     struct kvm_mmu_page *page)
{
	u64 *parent_pte;

	while (page->multimapped || page->parent_pte) {
		if (!page->multimapped)
			parent_pte = page->parent_pte;
		else {
			struct kvm_pte_chain *chain;

			chain = container_of(page->parent_ptes.first,
					     struct kvm_pte_chain, link);
			parent_pte = chain->parent_ptes[0];
		}
		BUG_ON(!parent_pte);
		kvm_mmu_put_page(vcpu, page, parent_pte);
		*parent_pte = 0;
	}
	kvm_mmu_page_unlink_children(vcpu, page);
	if (!page->root_count) {
		hlist_del(&page->hash_link);
		kvm_mmu_free_page(vcpu, page->page_hpa);
	} else
		list_move(&page->link, &vcpu->kvm->active_mmu_pages);
}

static int kvm_mmu_unprotect_page(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	unsigned index;
	struct hlist_head *bucket;
	struct kvm_mmu_page *page;
	struct hlist_node *node, *n;
	int r;

	pgprintk("%s: looking for gfn %lx\n", __FUNCTION__, gfn);
	r = 0;
	index = kvm_page_table_hashfn(gfn) % KVM_NUM_MMU_PAGES;
	bucket = &vcpu->kvm->mmu_page_hash[index];
	hlist_for_each_entry_safe(page, node, n, bucket, hash_link)
		if (page->gfn == gfn && !page->role.metaphysical) {
			pgprintk("%s: gfn %lx role %x\n", __FUNCTION__, gfn,
				 page->role.word);
			kvm_mmu_zap_page(vcpu, page);
			r = 1;
		}
	return r;
}

static void page_header_update_slot(struct kvm *kvm, void *pte, gpa_t gpa)
{
	int slot = memslot_id(kvm, gfn_to_memslot(kvm, gpa >> PAGE_SHIFT));
	struct kvm_mmu_page *page_head = page_header(__pa(pte));

	__set_bit(slot, &page_head->slot_bitmap);
}

hpa_t safe_gpa_to_hpa(struct kvm_vcpu *vcpu, gpa_t gpa)
{
	hpa_t hpa = gpa_to_hpa(vcpu, gpa);

	return is_error_hpa(hpa) ? bad_page_address | (gpa & ~PAGE_MASK): hpa;
}

hpa_t gpa_to_hpa(struct kvm_vcpu *vcpu, gpa_t gpa)
{
	struct page *page;

	ASSERT((gpa & HPA_ERR_MASK) == 0);
	page = gfn_to_page(vcpu->kvm, gpa >> PAGE_SHIFT);
	if (!page)
		return gpa | HPA_ERR_MASK;
	return ((hpa_t)page_to_pfn(page) << PAGE_SHIFT)
		| (gpa & (PAGE_SIZE-1));
}

hpa_t gva_to_hpa(struct kvm_vcpu *vcpu, gva_t gva)
{
	gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, gva);

	if (gpa == UNMAPPED_GVA)
		return UNMAPPED_GVA;
	return gpa_to_hpa(vcpu, gpa);
}

struct page *gva_to_page(struct kvm_vcpu *vcpu, gva_t gva)
{
	gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, gva);

	if (gpa == UNMAPPED_GVA)
		return NULL;
	return pfn_to_page(gpa_to_hpa(vcpu, gpa) >> PAGE_SHIFT);
}

static void nonpaging_new_cr3(struct kvm_vcpu *vcpu)
{
}

static int nonpaging_map(struct kvm_vcpu *vcpu, gva_t v, hpa_t p)
{
	int level = PT32E_ROOT_LEVEL;
	hpa_t table_addr = vcpu->mmu.root_hpa;

	for (; ; level--) {
		u32 index = PT64_INDEX(v, level);
		u64 *table;
		u64 pte;

		ASSERT(VALID_PAGE(table_addr));
		table = __va(table_addr);

		if (level == 1) {
			pte = table[index];
			if (is_present_pte(pte) && is_writeble_pte(pte))
				return 0;
			mark_page_dirty(vcpu->kvm, v >> PAGE_SHIFT);
			page_header_update_slot(vcpu->kvm, table, v);
			table[index] = p | PT_PRESENT_MASK | PT_WRITABLE_MASK |
								PT_USER_MASK;
			rmap_add(vcpu, &table[index]);
			return 0;
		}

		if (table[index] == 0) {
			struct kvm_mmu_page *new_table;
			gfn_t pseudo_gfn;

			pseudo_gfn = (v & PT64_DIR_BASE_ADDR_MASK)
				>> PAGE_SHIFT;
			new_table = kvm_mmu_get_page(vcpu, pseudo_gfn,
						     v, level - 1,
						     1, 0, &table[index]);
			if (!new_table) {
				pgprintk("nonpaging_map: ENOMEM\n");
				return -ENOMEM;
			}

			table[index] = new_table->page_hpa | PT_PRESENT_MASK
				| PT_WRITABLE_MASK | PT_USER_MASK;
		}
		table_addr = table[index] & PT64_BASE_ADDR_MASK;
	}
}

static void mmu_free_roots(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_mmu_page *page;

#ifdef CONFIG_X86_64
	if (vcpu->mmu.shadow_root_level == PT64_ROOT_LEVEL) {
		hpa_t root = vcpu->mmu.root_hpa;

		ASSERT(VALID_PAGE(root));
		page = page_header(root);
		--page->root_count;
		vcpu->mmu.root_hpa = INVALID_PAGE;
		return;
	}
#endif
	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->mmu.pae_root[i];

		if (root) {
			ASSERT(VALID_PAGE(root));
			root &= PT64_BASE_ADDR_MASK;
			page = page_header(root);
			--page->root_count;
		}
		vcpu->mmu.pae_root[i] = INVALID_PAGE;
	}
	vcpu->mmu.root_hpa = INVALID_PAGE;
}

static void mmu_alloc_roots(struct kvm_vcpu *vcpu)
{
	int i;
	gfn_t root_gfn;
	struct kvm_mmu_page *page;

	root_gfn = vcpu->cr3 >> PAGE_SHIFT;

#ifdef CONFIG_X86_64
	if (vcpu->mmu.shadow_root_level == PT64_ROOT_LEVEL) {
		hpa_t root = vcpu->mmu.root_hpa;

		ASSERT(!VALID_PAGE(root));
		page = kvm_mmu_get_page(vcpu, root_gfn, 0,
					PT64_ROOT_LEVEL, 0, 0, NULL);
		root = page->page_hpa;
		++page->root_count;
		vcpu->mmu.root_hpa = root;
		return;
	}
#endif
	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->mmu.pae_root[i];

		ASSERT(!VALID_PAGE(root));
		if (vcpu->mmu.root_level == PT32E_ROOT_LEVEL) {
			if (!is_present_pte(vcpu->pdptrs[i])) {
				vcpu->mmu.pae_root[i] = 0;
				continue;
			}
			root_gfn = vcpu->pdptrs[i] >> PAGE_SHIFT;
		} else if (vcpu->mmu.root_level == 0)
			root_gfn = 0;
		page = kvm_mmu_get_page(vcpu, root_gfn, i << 30,
					PT32_ROOT_LEVEL, !is_paging(vcpu),
					0, NULL);
		root = page->page_hpa;
		++page->root_count;
		vcpu->mmu.pae_root[i] = root | PT_PRESENT_MASK;
	}
	vcpu->mmu.root_hpa = __pa(vcpu->mmu.pae_root);
}

static gpa_t nonpaging_gva_to_gpa(struct kvm_vcpu *vcpu, gva_t vaddr)
{
	return vaddr;
}

static int nonpaging_page_fault(struct kvm_vcpu *vcpu, gva_t gva,
			       u32 error_code)
{
	gpa_t addr = gva;
	hpa_t paddr;
	int r;

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	ASSERT(vcpu);
	ASSERT(VALID_PAGE(vcpu->mmu.root_hpa));


	paddr = gpa_to_hpa(vcpu , addr & PT64_BASE_ADDR_MASK);

	if (is_error_hpa(paddr))
		return 1;

	return nonpaging_map(vcpu, addr & PAGE_MASK, paddr);
}

static void nonpaging_free(struct kvm_vcpu *vcpu)
{
	mmu_free_roots(vcpu);
}

static int nonpaging_init_context(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->mmu;

	context->new_cr3 = nonpaging_new_cr3;
	context->page_fault = nonpaging_page_fault;
	context->gva_to_gpa = nonpaging_gva_to_gpa;
	context->free = nonpaging_free;
	context->root_level = 0;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	mmu_alloc_roots(vcpu);
	ASSERT(VALID_PAGE(context->root_hpa));
	kvm_arch_ops->set_cr3(vcpu, context->root_hpa);
	return 0;
}

static void kvm_mmu_flush_tlb(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.tlb_flush;
	kvm_arch_ops->tlb_flush(vcpu);
}

static void paging_new_cr3(struct kvm_vcpu *vcpu)
{
	pgprintk("%s: cr3 %lx\n", __FUNCTION__, vcpu->cr3);
	mmu_free_roots(vcpu);
	if (unlikely(vcpu->kvm->n_free_mmu_pages < KVM_MIN_FREE_MMU_PAGES))
		kvm_mmu_free_some_pages(vcpu);
	mmu_alloc_roots(vcpu);
	kvm_mmu_flush_tlb(vcpu);
	kvm_arch_ops->set_cr3(vcpu, vcpu->mmu.root_hpa);
}

static inline void set_pte_common(struct kvm_vcpu *vcpu,
			     u64 *shadow_pte,
			     gpa_t gaddr,
			     int dirty,
			     u64 access_bits,
			     gfn_t gfn)
{
	hpa_t paddr;

	*shadow_pte |= access_bits << PT_SHADOW_BITS_OFFSET;
	if (!dirty)
		access_bits &= ~PT_WRITABLE_MASK;

	paddr = gpa_to_hpa(vcpu, gaddr & PT64_BASE_ADDR_MASK);

	*shadow_pte |= access_bits;

	if (is_error_hpa(paddr)) {
		*shadow_pte |= gaddr;
		*shadow_pte |= PT_SHADOW_IO_MARK;
		*shadow_pte &= ~PT_PRESENT_MASK;
		return;
	}

	*shadow_pte |= paddr;

	if (access_bits & PT_WRITABLE_MASK) {
		struct kvm_mmu_page *shadow;

		shadow = kvm_mmu_lookup_page(vcpu, gfn);
		if (shadow) {
			pgprintk("%s: found shadow page for %lx, marking ro\n",
				 __FUNCTION__, gfn);
			access_bits &= ~PT_WRITABLE_MASK;
			if (is_writeble_pte(*shadow_pte)) {
				    *shadow_pte &= ~PT_WRITABLE_MASK;
				    kvm_arch_ops->tlb_flush(vcpu);
			}
		}
	}

	if (access_bits & PT_WRITABLE_MASK)
		mark_page_dirty(vcpu->kvm, gaddr >> PAGE_SHIFT);

	page_header_update_slot(vcpu->kvm, shadow_pte, gaddr);
	rmap_add(vcpu, shadow_pte);
}

static void inject_page_fault(struct kvm_vcpu *vcpu,
			      u64 addr,
			      u32 err_code)
{
	kvm_arch_ops->inject_page_fault(vcpu, addr, err_code);
}

static inline int fix_read_pf(u64 *shadow_ent)
{
	if ((*shadow_ent & PT_SHADOW_USER_MASK) &&
	    !(*shadow_ent & PT_USER_MASK)) {
		/*
		 * If supervisor write protect is disabled, we shadow kernel
		 * pages as user pages so we can trap the write access.
		 */
		*shadow_ent |= PT_USER_MASK;
		*shadow_ent &= ~PT_WRITABLE_MASK;

		return 1;

	}
	return 0;
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
	struct kvm_mmu *context = &vcpu->mmu;

	ASSERT(is_pae(vcpu));
	context->new_cr3 = paging_new_cr3;
	context->page_fault = paging64_page_fault;
	context->gva_to_gpa = paging64_gva_to_gpa;
	context->free = paging_free;
	context->root_level = level;
	context->shadow_root_level = level;
	mmu_alloc_roots(vcpu);
	ASSERT(VALID_PAGE(context->root_hpa));
	kvm_arch_ops->set_cr3(vcpu, context->root_hpa |
		    (vcpu->cr3 & (CR3_PCD_MASK | CR3_WPT_MASK)));
	return 0;
}

static int paging64_init_context(struct kvm_vcpu *vcpu)
{
	return paging64_init_context_common(vcpu, PT64_ROOT_LEVEL);
}

static int paging32_init_context(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->mmu;

	context->new_cr3 = paging_new_cr3;
	context->page_fault = paging32_page_fault;
	context->gva_to_gpa = paging32_gva_to_gpa;
	context->free = paging_free;
	context->root_level = PT32_ROOT_LEVEL;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	mmu_alloc_roots(vcpu);
	ASSERT(VALID_PAGE(context->root_hpa));
	kvm_arch_ops->set_cr3(vcpu, context->root_hpa |
		    (vcpu->cr3 & (CR3_PCD_MASK | CR3_WPT_MASK)));
	return 0;
}

static int paging32E_init_context(struct kvm_vcpu *vcpu)
{
	return paging64_init_context_common(vcpu, PT32E_ROOT_LEVEL);
}

static int init_kvm_mmu(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->mmu.root_hpa));

	if (!is_paging(vcpu))
		return nonpaging_init_context(vcpu);
	else if (is_long_mode(vcpu))
		return paging64_init_context(vcpu);
	else if (is_pae(vcpu))
		return paging32E_init_context(vcpu);
	else
		return paging32_init_context(vcpu);
}

static void destroy_kvm_mmu(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	if (VALID_PAGE(vcpu->mmu.root_hpa)) {
		vcpu->mmu.free(vcpu);
		vcpu->mmu.root_hpa = INVALID_PAGE;
	}
}

int kvm_mmu_reset_context(struct kvm_vcpu *vcpu)
{
	int r;

	destroy_kvm_mmu(vcpu);
	r = init_kvm_mmu(vcpu);
	if (r < 0)
		goto out;
	r = mmu_topup_memory_caches(vcpu);
out:
	return r;
}

static void mmu_pre_write_zap_pte(struct kvm_vcpu *vcpu,
				  struct kvm_mmu_page *page,
				  u64 *spte)
{
	u64 pte;
	struct kvm_mmu_page *child;

	pte = *spte;
	if (is_present_pte(pte)) {
		if (page->role.level == PT_PAGE_TABLE_LEVEL)
			rmap_remove(vcpu, spte);
		else {
			child = page_header(pte & PT64_BASE_ADDR_MASK);
			mmu_page_remove_parent_pte(vcpu, child, spte);
		}
	}
	*spte = 0;
}

void kvm_mmu_pre_write(struct kvm_vcpu *vcpu, gpa_t gpa, int bytes)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct kvm_mmu_page *page;
	struct hlist_node *node, *n;
	struct hlist_head *bucket;
	unsigned index;
	u64 *spte;
	unsigned offset = offset_in_page(gpa);
	unsigned pte_size;
	unsigned page_offset;
	unsigned misaligned;
	int level;
	int flooded = 0;
	int npte;

	pgprintk("%s: gpa %llx bytes %d\n", __FUNCTION__, gpa, bytes);
	if (gfn == vcpu->last_pt_write_gfn) {
		++vcpu->last_pt_write_count;
		if (vcpu->last_pt_write_count >= 3)
			flooded = 1;
	} else {
		vcpu->last_pt_write_gfn = gfn;
		vcpu->last_pt_write_count = 1;
	}
	index = kvm_page_table_hashfn(gfn) % KVM_NUM_MMU_PAGES;
	bucket = &vcpu->kvm->mmu_page_hash[index];
	hlist_for_each_entry_safe(page, node, n, bucket, hash_link) {
		if (page->gfn != gfn || page->role.metaphysical)
			continue;
		pte_size = page->role.glevels == PT32_ROOT_LEVEL ? 4 : 8;
		misaligned = (offset ^ (offset + bytes - 1)) & ~(pte_size - 1);
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
				 gpa, bytes, page->role.word);
			kvm_mmu_zap_page(vcpu, page);
			continue;
		}
		page_offset = offset;
		level = page->role.level;
		npte = 1;
		if (page->role.glevels == PT32_ROOT_LEVEL) {
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
			page_offset &= ~PAGE_MASK;
		}
		spte = __va(page->page_hpa);
		spte += page_offset / sizeof(*spte);
		while (npte--) {
			mmu_pre_write_zap_pte(vcpu, page, spte);
			++spte;
		}
	}
}

void kvm_mmu_post_write(struct kvm_vcpu *vcpu, gpa_t gpa, int bytes)
{
}

int kvm_mmu_unprotect_page_virt(struct kvm_vcpu *vcpu, gva_t gva)
{
	gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, gva);

	return kvm_mmu_unprotect_page(vcpu, gpa >> PAGE_SHIFT);
}

void kvm_mmu_free_some_pages(struct kvm_vcpu *vcpu)
{
	while (vcpu->kvm->n_free_mmu_pages < KVM_REFILL_PAGES) {
		struct kvm_mmu_page *page;

		page = container_of(vcpu->kvm->active_mmu_pages.prev,
				    struct kvm_mmu_page, link);
		kvm_mmu_zap_page(vcpu, page);
	}
}
EXPORT_SYMBOL_GPL(kvm_mmu_free_some_pages);

static void free_mmu_pages(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *page;

	while (!list_empty(&vcpu->kvm->active_mmu_pages)) {
		page = container_of(vcpu->kvm->active_mmu_pages.next,
				    struct kvm_mmu_page, link);
		kvm_mmu_zap_page(vcpu, page);
	}
	while (!list_empty(&vcpu->free_pages)) {
		page = list_entry(vcpu->free_pages.next,
				  struct kvm_mmu_page, link);
		list_del(&page->link);
		__free_page(pfn_to_page(page->page_hpa >> PAGE_SHIFT));
		page->page_hpa = INVALID_PAGE;
	}
	free_page((unsigned long)vcpu->mmu.pae_root);
}

static int alloc_mmu_pages(struct kvm_vcpu *vcpu)
{
	struct page *page;
	int i;

	ASSERT(vcpu);

	for (i = 0; i < KVM_NUM_MMU_PAGES; i++) {
		struct kvm_mmu_page *page_header = &vcpu->page_header_buf[i];

		INIT_LIST_HEAD(&page_header->link);
		if ((page = alloc_page(GFP_KERNEL)) == NULL)
			goto error_1;
		set_page_private(page, (unsigned long)page_header);
		page_header->page_hpa = (hpa_t)page_to_pfn(page) << PAGE_SHIFT;
		memset(__va(page_header->page_hpa), 0, PAGE_SIZE);
		list_add(&page_header->link, &vcpu->free_pages);
		++vcpu->kvm->n_free_mmu_pages;
	}

	/*
	 * When emulating 32-bit mode, cr3 is only 32 bits even on x86_64.
	 * Therefore we need to allocate shadow page tables in the first
	 * 4GB of memory, which happens to fit the DMA32 zone.
	 */
	page = alloc_page(GFP_KERNEL | __GFP_DMA32);
	if (!page)
		goto error_1;
	vcpu->mmu.pae_root = page_address(page);
	for (i = 0; i < 4; ++i)
		vcpu->mmu.pae_root[i] = INVALID_PAGE;

	return 0;

error_1:
	free_mmu_pages(vcpu);
	return -ENOMEM;
}

int kvm_mmu_create(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->mmu.root_hpa));
	ASSERT(list_empty(&vcpu->free_pages));

	return alloc_mmu_pages(vcpu);
}

int kvm_mmu_setup(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->mmu.root_hpa));
	ASSERT(!list_empty(&vcpu->free_pages));

	return init_kvm_mmu(vcpu);
}

void kvm_mmu_destroy(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);

	destroy_kvm_mmu(vcpu);
	free_mmu_pages(vcpu);
	mmu_free_memory_caches(vcpu);
}

void kvm_mmu_slot_remove_write_access(struct kvm_vcpu *vcpu, int slot)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_page *page;

	list_for_each_entry(page, &kvm->active_mmu_pages, link) {
		int i;
		u64 *pt;

		if (!test_bit(slot, &page->slot_bitmap))
			continue;

		pt = __va(page->page_hpa);
		for (i = 0; i < PT64_ENT_PER_PAGE; ++i)
			/* avoid RMW */
			if (pt[i] & PT_WRITABLE_MASK) {
				rmap_remove(vcpu, &pt[i]);
				pt[i] &= ~PT_WRITABLE_MASK;
			}
	}
}

void kvm_mmu_zap_all(struct kvm_vcpu *vcpu)
{
	destroy_kvm_mmu(vcpu);

	while (!list_empty(&vcpu->kvm->active_mmu_pages)) {
		struct kvm_mmu_page *page;

		page = container_of(vcpu->kvm->active_mmu_pages.next,
				    struct kvm_mmu_page, link);
		kvm_mmu_zap_page(vcpu, page);
	}

	mmu_free_memory_caches(vcpu);
	kvm_arch_ops->tlb_flush(vcpu);
	init_kvm_mmu(vcpu);
}

void kvm_mmu_module_exit(void)
{
	if (pte_chain_cache)
		kmem_cache_destroy(pte_chain_cache);
	if (rmap_desc_cache)
		kmem_cache_destroy(rmap_desc_cache);
}

int kvm_mmu_module_init(void)
{
	pte_chain_cache = kmem_cache_create("kvm_pte_chain",
					    sizeof(struct kvm_pte_chain),
					    0, 0, NULL, NULL);
	if (!pte_chain_cache)
		goto nomem;
	rmap_desc_cache = kmem_cache_create("kvm_rmap_desc",
					    sizeof(struct kvm_rmap_desc),
					    0, 0, NULL, NULL);
	if (!rmap_desc_cache)
		goto nomem;

	return 0;

nomem:
	kvm_mmu_module_exit();
	return -ENOMEM;
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

		if (!(ent & PT_PRESENT_MASK))
			continue;

		va = canonicalize(va);
		if (level > 1)
			audit_mappings_page(vcpu, ent, va, level - 1);
		else {
			gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, va);
			hpa_t hpa = gpa_to_hpa(vcpu, gpa);

			if ((ent & PT_PRESENT_MASK)
			    && (ent & PT64_BASE_ADDR_MASK) != hpa)
				printk(KERN_ERR "audit error: (%s) levels %d"
				       " gva %lx gpa %llx hpa %llx ent %llx\n",
				       audit_msg, vcpu->mmu.root_level,
				       va, gpa, hpa, ent);
		}
	}
}

static void audit_mappings(struct kvm_vcpu *vcpu)
{
	unsigned i;

	if (vcpu->mmu.root_level == 4)
		audit_mappings_page(vcpu, vcpu->mmu.root_hpa, 0, 4);
	else
		for (i = 0; i < 4; ++i)
			if (vcpu->mmu.pae_root[i] & PT_PRESENT_MASK)
				audit_mappings_page(vcpu,
						    vcpu->mmu.pae_root[i],
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
			struct page *page = m->phys_mem[j];

			if (!page->private)
				continue;
			if (!(page->private & 1)) {
				++nmaps;
				continue;
			}
			d = (struct kvm_rmap_desc *)(page->private & ~1ul);
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
	struct kvm_mmu_page *page;
	int i;

	list_for_each_entry(page, &vcpu->kvm->active_mmu_pages, link) {
		u64 *pt = __va(page->page_hpa);

		if (page->role.level != PT_PAGE_TABLE_LEVEL)
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
		       __FUNCTION__, audit_msg, n_rmap, n_actual);
}

static void audit_write_protection(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *page;

	list_for_each_entry(page, &vcpu->kvm->active_mmu_pages, link) {
		hfn_t hfn;
		struct page *pg;

		if (page->role.metaphysical)
			continue;

		hfn = gpa_to_hpa(vcpu, (gpa_t)page->gfn << PAGE_SHIFT)
			>> PAGE_SHIFT;
		pg = pfn_to_page(hfn);
		if (pg->private)
			printk(KERN_ERR "%s: (%s) shadow page has writable"
			       " mappings: gfn %lx role %x\n",
			       __FUNCTION__, audit_msg, page->gfn,
			       page->role.word);
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
