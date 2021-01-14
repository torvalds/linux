// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stand-alone page-table allocator for hyp stage-1 and guest stage-2.
 * No bombay mix was harmed in the writing of this file.
 *
 * Copyright (C) 2020 Google LLC
 * Author: Will Deacon <will@kernel.org>
 */

#include <linux/bitfield.h>
#include <asm/kvm_pgtable.h>

#define KVM_PGTABLE_MAX_LEVELS		4U

#define KVM_PTE_VALID			BIT(0)

#define KVM_PTE_TYPE			BIT(1)
#define KVM_PTE_TYPE_BLOCK		0
#define KVM_PTE_TYPE_PAGE		1
#define KVM_PTE_TYPE_TABLE		1

#define KVM_PTE_ADDR_MASK		GENMASK(47, PAGE_SHIFT)
#define KVM_PTE_ADDR_51_48		GENMASK(15, 12)

#define KVM_PTE_LEAF_ATTR_LO		GENMASK(11, 2)

#define KVM_PTE_LEAF_ATTR_LO_S1_ATTRIDX	GENMASK(4, 2)
#define KVM_PTE_LEAF_ATTR_LO_S1_AP	GENMASK(7, 6)
#define KVM_PTE_LEAF_ATTR_LO_S1_AP_RO	3
#define KVM_PTE_LEAF_ATTR_LO_S1_AP_RW	1
#define KVM_PTE_LEAF_ATTR_LO_S1_SH	GENMASK(9, 8)
#define KVM_PTE_LEAF_ATTR_LO_S1_SH_IS	3
#define KVM_PTE_LEAF_ATTR_LO_S1_AF	BIT(10)

#define KVM_PTE_LEAF_ATTR_LO_S2_MEMATTR	GENMASK(5, 2)
#define KVM_PTE_LEAF_ATTR_LO_S2_S2AP_R	BIT(6)
#define KVM_PTE_LEAF_ATTR_LO_S2_S2AP_W	BIT(7)
#define KVM_PTE_LEAF_ATTR_LO_S2_SH	GENMASK(9, 8)
#define KVM_PTE_LEAF_ATTR_LO_S2_SH_IS	3
#define KVM_PTE_LEAF_ATTR_LO_S2_AF	BIT(10)

#define KVM_PTE_LEAF_ATTR_HI		GENMASK(63, 51)

#define KVM_PTE_LEAF_ATTR_HI_S1_XN	BIT(54)

#define KVM_PTE_LEAF_ATTR_HI_S2_XN	BIT(54)

#define KVM_PTE_LEAF_ATTR_S2_PERMS	(KVM_PTE_LEAF_ATTR_LO_S2_S2AP_R | \
					 KVM_PTE_LEAF_ATTR_LO_S2_S2AP_W | \
					 KVM_PTE_LEAF_ATTR_HI_S2_XN)

struct kvm_pgtable_walk_data {
	struct kvm_pgtable		*pgt;
	struct kvm_pgtable_walker	*walker;

	u64				addr;
	u64				end;
};

static u64 kvm_granule_shift(u32 level)
{
	/* Assumes KVM_PGTABLE_MAX_LEVELS is 4 */
	return ARM64_HW_PGTABLE_LEVEL_SHIFT(level);
}

static u64 kvm_granule_size(u32 level)
{
	return BIT(kvm_granule_shift(level));
}

static bool kvm_block_mapping_supported(u64 addr, u64 end, u64 phys, u32 level)
{
	u64 granule = kvm_granule_size(level);

	/*
	 * Reject invalid block mappings and don't bother with 4TB mappings for
	 * 52-bit PAs.
	 */
	if (level == 0 || (PAGE_SIZE != SZ_4K && level == 1))
		return false;

	if (granule > (end - addr))
		return false;

	return IS_ALIGNED(addr, granule) && IS_ALIGNED(phys, granule);
}

static u32 kvm_pgtable_idx(struct kvm_pgtable_walk_data *data, u32 level)
{
	u64 shift = kvm_granule_shift(level);
	u64 mask = BIT(PAGE_SHIFT - 3) - 1;

	return (data->addr >> shift) & mask;
}

static u32 __kvm_pgd_page_idx(struct kvm_pgtable *pgt, u64 addr)
{
	u64 shift = kvm_granule_shift(pgt->start_level - 1); /* May underflow */
	u64 mask = BIT(pgt->ia_bits) - 1;

	return (addr & mask) >> shift;
}

static u32 kvm_pgd_page_idx(struct kvm_pgtable_walk_data *data)
{
	return __kvm_pgd_page_idx(data->pgt, data->addr);
}

static u32 kvm_pgd_pages(u32 ia_bits, u32 start_level)
{
	struct kvm_pgtable pgt = {
		.ia_bits	= ia_bits,
		.start_level	= start_level,
	};

	return __kvm_pgd_page_idx(&pgt, -1ULL) + 1;
}

static bool kvm_pte_valid(kvm_pte_t pte)
{
	return pte & KVM_PTE_VALID;
}

static bool kvm_pte_table(kvm_pte_t pte, u32 level)
{
	if (level == KVM_PGTABLE_MAX_LEVELS - 1)
		return false;

	if (!kvm_pte_valid(pte))
		return false;

	return FIELD_GET(KVM_PTE_TYPE, pte) == KVM_PTE_TYPE_TABLE;
}

static u64 kvm_pte_to_phys(kvm_pte_t pte)
{
	u64 pa = pte & KVM_PTE_ADDR_MASK;

	if (PAGE_SHIFT == 16)
		pa |= FIELD_GET(KVM_PTE_ADDR_51_48, pte) << 48;

	return pa;
}

static kvm_pte_t kvm_phys_to_pte(u64 pa)
{
	kvm_pte_t pte = pa & KVM_PTE_ADDR_MASK;

	if (PAGE_SHIFT == 16)
		pte |= FIELD_PREP(KVM_PTE_ADDR_51_48, pa >> 48);

	return pte;
}

static kvm_pte_t *kvm_pte_follow(kvm_pte_t pte)
{
	return __va(kvm_pte_to_phys(pte));
}

static void kvm_set_invalid_pte(kvm_pte_t *ptep)
{
	kvm_pte_t pte = *ptep;
	WRITE_ONCE(*ptep, pte & ~KVM_PTE_VALID);
}

static void kvm_set_table_pte(kvm_pte_t *ptep, kvm_pte_t *childp)
{
	kvm_pte_t old = *ptep, pte = kvm_phys_to_pte(__pa(childp));

	pte |= FIELD_PREP(KVM_PTE_TYPE, KVM_PTE_TYPE_TABLE);
	pte |= KVM_PTE_VALID;

	WARN_ON(kvm_pte_valid(old));
	smp_store_release(ptep, pte);
}

static kvm_pte_t kvm_init_valid_leaf_pte(u64 pa, kvm_pte_t attr, u32 level)
{
	kvm_pte_t pte = kvm_phys_to_pte(pa);
	u64 type = (level == KVM_PGTABLE_MAX_LEVELS - 1) ? KVM_PTE_TYPE_PAGE :
							   KVM_PTE_TYPE_BLOCK;

	pte |= attr & (KVM_PTE_LEAF_ATTR_LO | KVM_PTE_LEAF_ATTR_HI);
	pte |= FIELD_PREP(KVM_PTE_TYPE, type);
	pte |= KVM_PTE_VALID;

	return pte;
}

static int kvm_pgtable_visitor_cb(struct kvm_pgtable_walk_data *data, u64 addr,
				  u32 level, kvm_pte_t *ptep,
				  enum kvm_pgtable_walk_flags flag)
{
	struct kvm_pgtable_walker *walker = data->walker;
	return walker->cb(addr, data->end, level, ptep, flag, walker->arg);
}

static int __kvm_pgtable_walk(struct kvm_pgtable_walk_data *data,
			      kvm_pte_t *pgtable, u32 level);

static inline int __kvm_pgtable_visit(struct kvm_pgtable_walk_data *data,
				      kvm_pte_t *ptep, u32 level)
{
	int ret = 0;
	u64 addr = data->addr;
	kvm_pte_t *childp, pte = *ptep;
	bool table = kvm_pte_table(pte, level);
	enum kvm_pgtable_walk_flags flags = data->walker->flags;

	if (table && (flags & KVM_PGTABLE_WALK_TABLE_PRE)) {
		ret = kvm_pgtable_visitor_cb(data, addr, level, ptep,
					     KVM_PGTABLE_WALK_TABLE_PRE);
	}

	if (!table && (flags & KVM_PGTABLE_WALK_LEAF)) {
		ret = kvm_pgtable_visitor_cb(data, addr, level, ptep,
					     KVM_PGTABLE_WALK_LEAF);
		pte = *ptep;
		table = kvm_pte_table(pte, level);
	}

	if (ret)
		goto out;

	if (!table) {
		data->addr += kvm_granule_size(level);
		goto out;
	}

	childp = kvm_pte_follow(pte);
	ret = __kvm_pgtable_walk(data, childp, level + 1);
	if (ret)
		goto out;

	if (flags & KVM_PGTABLE_WALK_TABLE_POST) {
		ret = kvm_pgtable_visitor_cb(data, addr, level, ptep,
					     KVM_PGTABLE_WALK_TABLE_POST);
	}

out:
	return ret;
}

static int __kvm_pgtable_walk(struct kvm_pgtable_walk_data *data,
			      kvm_pte_t *pgtable, u32 level)
{
	u32 idx;
	int ret = 0;

	if (WARN_ON_ONCE(level >= KVM_PGTABLE_MAX_LEVELS))
		return -EINVAL;

	for (idx = kvm_pgtable_idx(data, level); idx < PTRS_PER_PTE; ++idx) {
		kvm_pte_t *ptep = &pgtable[idx];

		if (data->addr >= data->end)
			break;

		ret = __kvm_pgtable_visit(data, ptep, level);
		if (ret)
			break;
	}

	return ret;
}

static int _kvm_pgtable_walk(struct kvm_pgtable_walk_data *data)
{
	u32 idx;
	int ret = 0;
	struct kvm_pgtable *pgt = data->pgt;
	u64 limit = BIT(pgt->ia_bits);

	if (data->addr > limit || data->end > limit)
		return -ERANGE;

	if (!pgt->pgd)
		return -EINVAL;

	for (idx = kvm_pgd_page_idx(data); data->addr < data->end; ++idx) {
		kvm_pte_t *ptep = &pgt->pgd[idx * PTRS_PER_PTE];

		ret = __kvm_pgtable_walk(data, ptep, pgt->start_level);
		if (ret)
			break;
	}

	return ret;
}

int kvm_pgtable_walk(struct kvm_pgtable *pgt, u64 addr, u64 size,
		     struct kvm_pgtable_walker *walker)
{
	struct kvm_pgtable_walk_data walk_data = {
		.pgt	= pgt,
		.addr	= ALIGN_DOWN(addr, PAGE_SIZE),
		.end	= PAGE_ALIGN(walk_data.addr + size),
		.walker	= walker,
	};

	return _kvm_pgtable_walk(&walk_data);
}

struct hyp_map_data {
	u64		phys;
	kvm_pte_t	attr;
};

static int hyp_map_set_prot_attr(enum kvm_pgtable_prot prot,
				 struct hyp_map_data *data)
{
	bool device = prot & KVM_PGTABLE_PROT_DEVICE;
	u32 mtype = device ? MT_DEVICE_nGnRE : MT_NORMAL;
	kvm_pte_t attr = FIELD_PREP(KVM_PTE_LEAF_ATTR_LO_S1_ATTRIDX, mtype);
	u32 sh = KVM_PTE_LEAF_ATTR_LO_S1_SH_IS;
	u32 ap = (prot & KVM_PGTABLE_PROT_W) ? KVM_PTE_LEAF_ATTR_LO_S1_AP_RW :
					       KVM_PTE_LEAF_ATTR_LO_S1_AP_RO;

	if (!(prot & KVM_PGTABLE_PROT_R))
		return -EINVAL;

	if (prot & KVM_PGTABLE_PROT_X) {
		if (prot & KVM_PGTABLE_PROT_W)
			return -EINVAL;

		if (device)
			return -EINVAL;
	} else {
		attr |= KVM_PTE_LEAF_ATTR_HI_S1_XN;
	}

	attr |= FIELD_PREP(KVM_PTE_LEAF_ATTR_LO_S1_AP, ap);
	attr |= FIELD_PREP(KVM_PTE_LEAF_ATTR_LO_S1_SH, sh);
	attr |= KVM_PTE_LEAF_ATTR_LO_S1_AF;
	data->attr = attr;
	return 0;
}

static bool hyp_map_walker_try_leaf(u64 addr, u64 end, u32 level,
				    kvm_pte_t *ptep, struct hyp_map_data *data)
{
	kvm_pte_t new, old = *ptep;
	u64 granule = kvm_granule_size(level), phys = data->phys;

	if (!kvm_block_mapping_supported(addr, end, phys, level))
		return false;

	/* Tolerate KVM recreating the exact same mapping */
	new = kvm_init_valid_leaf_pte(phys, data->attr, level);
	if (old != new && !WARN_ON(kvm_pte_valid(old)))
		smp_store_release(ptep, new);

	data->phys += granule;
	return true;
}

static int hyp_map_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			  enum kvm_pgtable_walk_flags flag, void * const arg)
{
	kvm_pte_t *childp;

	if (hyp_map_walker_try_leaf(addr, end, level, ptep, arg))
		return 0;

	if (WARN_ON(level == KVM_PGTABLE_MAX_LEVELS - 1))
		return -EINVAL;

	childp = (kvm_pte_t *)get_zeroed_page(GFP_KERNEL);
	if (!childp)
		return -ENOMEM;

	kvm_set_table_pte(ptep, childp);
	return 0;
}

int kvm_pgtable_hyp_map(struct kvm_pgtable *pgt, u64 addr, u64 size, u64 phys,
			enum kvm_pgtable_prot prot)
{
	int ret;
	struct hyp_map_data map_data = {
		.phys	= ALIGN_DOWN(phys, PAGE_SIZE),
	};
	struct kvm_pgtable_walker walker = {
		.cb	= hyp_map_walker,
		.flags	= KVM_PGTABLE_WALK_LEAF,
		.arg	= &map_data,
	};

	ret = hyp_map_set_prot_attr(prot, &map_data);
	if (ret)
		return ret;

	ret = kvm_pgtable_walk(pgt, addr, size, &walker);
	dsb(ishst);
	isb();
	return ret;
}

int kvm_pgtable_hyp_init(struct kvm_pgtable *pgt, u32 va_bits)
{
	u64 levels = ARM64_HW_PGTABLE_LEVELS(va_bits);

	pgt->pgd = (kvm_pte_t *)get_zeroed_page(GFP_KERNEL);
	if (!pgt->pgd)
		return -ENOMEM;

	pgt->ia_bits		= va_bits;
	pgt->start_level	= KVM_PGTABLE_MAX_LEVELS - levels;
	pgt->mmu		= NULL;
	return 0;
}

static int hyp_free_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			   enum kvm_pgtable_walk_flags flag, void * const arg)
{
	free_page((unsigned long)kvm_pte_follow(*ptep));
	return 0;
}

void kvm_pgtable_hyp_destroy(struct kvm_pgtable *pgt)
{
	struct kvm_pgtable_walker walker = {
		.cb	= hyp_free_walker,
		.flags	= KVM_PGTABLE_WALK_TABLE_POST,
	};

	WARN_ON(kvm_pgtable_walk(pgt, 0, BIT(pgt->ia_bits), &walker));
	free_page((unsigned long)pgt->pgd);
	pgt->pgd = NULL;
}

struct stage2_map_data {
	u64				phys;
	kvm_pte_t			attr;

	kvm_pte_t			*anchor;

	struct kvm_s2_mmu		*mmu;
	struct kvm_mmu_memory_cache	*memcache;
};

static int stage2_map_set_prot_attr(enum kvm_pgtable_prot prot,
				    struct stage2_map_data *data)
{
	bool device = prot & KVM_PGTABLE_PROT_DEVICE;
	kvm_pte_t attr = device ? PAGE_S2_MEMATTR(DEVICE_nGnRE) :
			    PAGE_S2_MEMATTR(NORMAL);
	u32 sh = KVM_PTE_LEAF_ATTR_LO_S2_SH_IS;

	if (!(prot & KVM_PGTABLE_PROT_X))
		attr |= KVM_PTE_LEAF_ATTR_HI_S2_XN;
	else if (device)
		return -EINVAL;

	if (prot & KVM_PGTABLE_PROT_R)
		attr |= KVM_PTE_LEAF_ATTR_LO_S2_S2AP_R;

	if (prot & KVM_PGTABLE_PROT_W)
		attr |= KVM_PTE_LEAF_ATTR_LO_S2_S2AP_W;

	attr |= FIELD_PREP(KVM_PTE_LEAF_ATTR_LO_S2_SH, sh);
	attr |= KVM_PTE_LEAF_ATTR_LO_S2_AF;
	data->attr = attr;
	return 0;
}

static int stage2_map_walker_try_leaf(u64 addr, u64 end, u32 level,
				      kvm_pte_t *ptep,
				      struct stage2_map_data *data)
{
	kvm_pte_t new, old = *ptep;
	u64 granule = kvm_granule_size(level), phys = data->phys;
	struct page *page = virt_to_page(ptep);

	if (!kvm_block_mapping_supported(addr, end, phys, level))
		return -E2BIG;

	new = kvm_init_valid_leaf_pte(phys, data->attr, level);
	if (kvm_pte_valid(old)) {
		/*
		 * Skip updating the PTE if we are trying to recreate the exact
		 * same mapping or only change the access permissions. Instead,
		 * the vCPU will exit one more time from guest if still needed
		 * and then go through the path of relaxing permissions.
		 */
		if (!((old ^ new) & (~KVM_PTE_LEAF_ATTR_S2_PERMS)))
			return -EAGAIN;

		/*
		 * There's an existing different valid leaf entry, so perform
		 * break-before-make.
		 */
		kvm_set_invalid_pte(ptep);
		kvm_call_hyp(__kvm_tlb_flush_vmid_ipa, data->mmu, addr, level);
		put_page(page);
	}

	smp_store_release(ptep, new);
	get_page(page);
	data->phys += granule;
	return 0;
}

static int stage2_map_walk_table_pre(u64 addr, u64 end, u32 level,
				     kvm_pte_t *ptep,
				     struct stage2_map_data *data)
{
	if (data->anchor)
		return 0;

	if (!kvm_block_mapping_supported(addr, end, data->phys, level))
		return 0;

	kvm_set_invalid_pte(ptep);

	/*
	 * Invalidate the whole stage-2, as we may have numerous leaf
	 * entries below us which would otherwise need invalidating
	 * individually.
	 */
	kvm_call_hyp(__kvm_tlb_flush_vmid, data->mmu);
	data->anchor = ptep;
	return 0;
}

static int stage2_map_walk_leaf(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
				struct stage2_map_data *data)
{
	int ret;
	kvm_pte_t *childp, pte = *ptep;
	struct page *page = virt_to_page(ptep);

	if (data->anchor) {
		if (kvm_pte_valid(pte))
			put_page(page);

		return 0;
	}

	ret = stage2_map_walker_try_leaf(addr, end, level, ptep, data);
	if (ret != -E2BIG)
		return ret;

	if (WARN_ON(level == KVM_PGTABLE_MAX_LEVELS - 1))
		return -EINVAL;

	if (!data->memcache)
		return -ENOMEM;

	childp = kvm_mmu_memory_cache_alloc(data->memcache);
	if (!childp)
		return -ENOMEM;

	/*
	 * If we've run into an existing block mapping then replace it with
	 * a table. Accesses beyond 'end' that fall within the new table
	 * will be mapped lazily.
	 */
	if (kvm_pte_valid(pte)) {
		kvm_set_invalid_pte(ptep);
		kvm_call_hyp(__kvm_tlb_flush_vmid_ipa, data->mmu, addr, level);
		put_page(page);
	}

	kvm_set_table_pte(ptep, childp);
	get_page(page);

	return 0;
}

static int stage2_map_walk_table_post(u64 addr, u64 end, u32 level,
				      kvm_pte_t *ptep,
				      struct stage2_map_data *data)
{
	int ret = 0;

	if (!data->anchor)
		return 0;

	free_page((unsigned long)kvm_pte_follow(*ptep));
	put_page(virt_to_page(ptep));

	if (data->anchor == ptep) {
		data->anchor = NULL;
		ret = stage2_map_walk_leaf(addr, end, level, ptep, data);
	}

	return ret;
}

/*
 * This is a little fiddly, as we use all three of the walk flags. The idea
 * is that the TABLE_PRE callback runs for table entries on the way down,
 * looking for table entries which we could conceivably replace with a
 * block entry for this mapping. If it finds one, then it sets the 'anchor'
 * field in 'struct stage2_map_data' to point at the table entry, before
 * clearing the entry to zero and descending into the now detached table.
 *
 * The behaviour of the LEAF callback then depends on whether or not the
 * anchor has been set. If not, then we're not using a block mapping higher
 * up the table and we perform the mapping at the existing leaves instead.
 * If, on the other hand, the anchor _is_ set, then we drop references to
 * all valid leaves so that the pages beneath the anchor can be freed.
 *
 * Finally, the TABLE_POST callback does nothing if the anchor has not
 * been set, but otherwise frees the page-table pages while walking back up
 * the page-table, installing the block entry when it revisits the anchor
 * pointer and clearing the anchor to NULL.
 */
static int stage2_map_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			     enum kvm_pgtable_walk_flags flag, void * const arg)
{
	struct stage2_map_data *data = arg;

	switch (flag) {
	case KVM_PGTABLE_WALK_TABLE_PRE:
		return stage2_map_walk_table_pre(addr, end, level, ptep, data);
	case KVM_PGTABLE_WALK_LEAF:
		return stage2_map_walk_leaf(addr, end, level, ptep, data);
	case KVM_PGTABLE_WALK_TABLE_POST:
		return stage2_map_walk_table_post(addr, end, level, ptep, data);
	}

	return -EINVAL;
}

int kvm_pgtable_stage2_map(struct kvm_pgtable *pgt, u64 addr, u64 size,
			   u64 phys, enum kvm_pgtable_prot prot,
			   struct kvm_mmu_memory_cache *mc)
{
	int ret;
	struct stage2_map_data map_data = {
		.phys		= ALIGN_DOWN(phys, PAGE_SIZE),
		.mmu		= pgt->mmu,
		.memcache	= mc,
	};
	struct kvm_pgtable_walker walker = {
		.cb		= stage2_map_walker,
		.flags		= KVM_PGTABLE_WALK_TABLE_PRE |
				  KVM_PGTABLE_WALK_LEAF |
				  KVM_PGTABLE_WALK_TABLE_POST,
		.arg		= &map_data,
	};

	ret = stage2_map_set_prot_attr(prot, &map_data);
	if (ret)
		return ret;

	ret = kvm_pgtable_walk(pgt, addr, size, &walker);
	dsb(ishst);
	return ret;
}

static void stage2_flush_dcache(void *addr, u64 size)
{
	if (cpus_have_const_cap(ARM64_HAS_STAGE2_FWB))
		return;

	__flush_dcache_area(addr, size);
}

static bool stage2_pte_cacheable(kvm_pte_t pte)
{
	u64 memattr = pte & KVM_PTE_LEAF_ATTR_LO_S2_MEMATTR;
	return memattr == PAGE_S2_MEMATTR(NORMAL);
}

static int stage2_unmap_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			       enum kvm_pgtable_walk_flags flag,
			       void * const arg)
{
	struct kvm_s2_mmu *mmu = arg;
	kvm_pte_t pte = *ptep, *childp = NULL;
	bool need_flush = false;

	if (!kvm_pte_valid(pte))
		return 0;

	if (kvm_pte_table(pte, level)) {
		childp = kvm_pte_follow(pte);

		if (page_count(virt_to_page(childp)) != 1)
			return 0;
	} else if (stage2_pte_cacheable(pte)) {
		need_flush = true;
	}

	/*
	 * This is similar to the map() path in that we unmap the entire
	 * block entry and rely on the remaining portions being faulted
	 * back lazily.
	 */
	kvm_set_invalid_pte(ptep);
	kvm_call_hyp(__kvm_tlb_flush_vmid_ipa, mmu, addr, level);
	put_page(virt_to_page(ptep));

	if (need_flush) {
		stage2_flush_dcache(kvm_pte_follow(pte),
				    kvm_granule_size(level));
	}

	if (childp)
		free_page((unsigned long)childp);

	return 0;
}

int kvm_pgtable_stage2_unmap(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	struct kvm_pgtable_walker walker = {
		.cb	= stage2_unmap_walker,
		.arg	= pgt->mmu,
		.flags	= KVM_PGTABLE_WALK_LEAF | KVM_PGTABLE_WALK_TABLE_POST,
	};

	return kvm_pgtable_walk(pgt, addr, size, &walker);
}

struct stage2_attr_data {
	kvm_pte_t	attr_set;
	kvm_pte_t	attr_clr;
	kvm_pte_t	pte;
	u32		level;
};

static int stage2_attr_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			      enum kvm_pgtable_walk_flags flag,
			      void * const arg)
{
	kvm_pte_t pte = *ptep;
	struct stage2_attr_data *data = arg;

	if (!kvm_pte_valid(pte))
		return 0;

	data->level = level;
	data->pte = pte;
	pte &= ~data->attr_clr;
	pte |= data->attr_set;

	/*
	 * We may race with the CPU trying to set the access flag here,
	 * but worst-case the access flag update gets lost and will be
	 * set on the next access instead.
	 */
	if (data->pte != pte)
		WRITE_ONCE(*ptep, pte);

	return 0;
}

static int stage2_update_leaf_attrs(struct kvm_pgtable *pgt, u64 addr,
				    u64 size, kvm_pte_t attr_set,
				    kvm_pte_t attr_clr, kvm_pte_t *orig_pte,
				    u32 *level)
{
	int ret;
	kvm_pte_t attr_mask = KVM_PTE_LEAF_ATTR_LO | KVM_PTE_LEAF_ATTR_HI;
	struct stage2_attr_data data = {
		.attr_set	= attr_set & attr_mask,
		.attr_clr	= attr_clr & attr_mask,
	};
	struct kvm_pgtable_walker walker = {
		.cb		= stage2_attr_walker,
		.arg		= &data,
		.flags		= KVM_PGTABLE_WALK_LEAF,
	};

	ret = kvm_pgtable_walk(pgt, addr, size, &walker);
	if (ret)
		return ret;

	if (orig_pte)
		*orig_pte = data.pte;

	if (level)
		*level = data.level;
	return 0;
}

int kvm_pgtable_stage2_wrprotect(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	return stage2_update_leaf_attrs(pgt, addr, size, 0,
					KVM_PTE_LEAF_ATTR_LO_S2_S2AP_W,
					NULL, NULL);
}

kvm_pte_t kvm_pgtable_stage2_mkyoung(struct kvm_pgtable *pgt, u64 addr)
{
	kvm_pte_t pte = 0;
	stage2_update_leaf_attrs(pgt, addr, 1, KVM_PTE_LEAF_ATTR_LO_S2_AF, 0,
				 &pte, NULL);
	dsb(ishst);
	return pte;
}

kvm_pte_t kvm_pgtable_stage2_mkold(struct kvm_pgtable *pgt, u64 addr)
{
	kvm_pte_t pte = 0;
	stage2_update_leaf_attrs(pgt, addr, 1, 0, KVM_PTE_LEAF_ATTR_LO_S2_AF,
				 &pte, NULL);
	/*
	 * "But where's the TLBI?!", you scream.
	 * "Over in the core code", I sigh.
	 *
	 * See the '->clear_flush_young()' callback on the KVM mmu notifier.
	 */
	return pte;
}

bool kvm_pgtable_stage2_is_young(struct kvm_pgtable *pgt, u64 addr)
{
	kvm_pte_t pte = 0;
	stage2_update_leaf_attrs(pgt, addr, 1, 0, 0, &pte, NULL);
	return pte & KVM_PTE_LEAF_ATTR_LO_S2_AF;
}

int kvm_pgtable_stage2_relax_perms(struct kvm_pgtable *pgt, u64 addr,
				   enum kvm_pgtable_prot prot)
{
	int ret;
	u32 level;
	kvm_pte_t set = 0, clr = 0;

	if (prot & KVM_PGTABLE_PROT_R)
		set |= KVM_PTE_LEAF_ATTR_LO_S2_S2AP_R;

	if (prot & KVM_PGTABLE_PROT_W)
		set |= KVM_PTE_LEAF_ATTR_LO_S2_S2AP_W;

	if (prot & KVM_PGTABLE_PROT_X)
		clr |= KVM_PTE_LEAF_ATTR_HI_S2_XN;

	ret = stage2_update_leaf_attrs(pgt, addr, 1, set, clr, NULL, &level);
	if (!ret)
		kvm_call_hyp(__kvm_tlb_flush_vmid_ipa, pgt->mmu, addr, level);
	return ret;
}

static int stage2_flush_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			       enum kvm_pgtable_walk_flags flag,
			       void * const arg)
{
	kvm_pte_t pte = *ptep;

	if (!kvm_pte_valid(pte) || !stage2_pte_cacheable(pte))
		return 0;

	stage2_flush_dcache(kvm_pte_follow(pte), kvm_granule_size(level));
	return 0;
}

int kvm_pgtable_stage2_flush(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	struct kvm_pgtable_walker walker = {
		.cb	= stage2_flush_walker,
		.flags	= KVM_PGTABLE_WALK_LEAF,
	};

	if (cpus_have_const_cap(ARM64_HAS_STAGE2_FWB))
		return 0;

	return kvm_pgtable_walk(pgt, addr, size, &walker);
}

int kvm_pgtable_stage2_init(struct kvm_pgtable *pgt, struct kvm *kvm)
{
	size_t pgd_sz;
	u64 vtcr = kvm->arch.vtcr;
	u32 ia_bits = VTCR_EL2_IPA(vtcr);
	u32 sl0 = FIELD_GET(VTCR_EL2_SL0_MASK, vtcr);
	u32 start_level = VTCR_EL2_TGRAN_SL0_BASE - sl0;

	pgd_sz = kvm_pgd_pages(ia_bits, start_level) * PAGE_SIZE;
	pgt->pgd = alloc_pages_exact(pgd_sz, GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!pgt->pgd)
		return -ENOMEM;

	pgt->ia_bits		= ia_bits;
	pgt->start_level	= start_level;
	pgt->mmu		= &kvm->arch.mmu;

	/* Ensure zeroed PGD pages are visible to the hardware walker */
	dsb(ishst);
	return 0;
}

static int stage2_free_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			      enum kvm_pgtable_walk_flags flag,
			      void * const arg)
{
	kvm_pte_t pte = *ptep;

	if (!kvm_pte_valid(pte))
		return 0;

	put_page(virt_to_page(ptep));

	if (kvm_pte_table(pte, level))
		free_page((unsigned long)kvm_pte_follow(pte));

	return 0;
}

void kvm_pgtable_stage2_destroy(struct kvm_pgtable *pgt)
{
	size_t pgd_sz;
	struct kvm_pgtable_walker walker = {
		.cb	= stage2_free_walker,
		.flags	= KVM_PGTABLE_WALK_LEAF |
			  KVM_PGTABLE_WALK_TABLE_POST,
	};

	WARN_ON(kvm_pgtable_walk(pgt, 0, BIT(pgt->ia_bits), &walker));
	pgd_sz = kvm_pgd_pages(pgt->ia_bits, pgt->start_level) * PAGE_SIZE;
	free_pages_exact(pgt->pgd, pgd_sz);
	pgt->pgd = NULL;
}
