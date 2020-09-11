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

#define KVM_PTE_LEAF_ATTR_HI		GENMASK(63, 51)

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

static bool kvm_set_valid_leaf_pte(kvm_pte_t *ptep, u64 pa, kvm_pte_t attr,
				   u32 level)
{
	kvm_pte_t old = *ptep, pte = kvm_phys_to_pte(pa);
	u64 type = (level == KVM_PGTABLE_MAX_LEVELS - 1) ? KVM_PTE_TYPE_PAGE :
							   KVM_PTE_TYPE_BLOCK;

	pte |= attr & (KVM_PTE_LEAF_ATTR_LO | KVM_PTE_LEAF_ATTR_HI);
	pte |= FIELD_PREP(KVM_PTE_TYPE, type);
	pte |= KVM_PTE_VALID;

	/* Tolerate KVM recreating the exact same mapping. */
	if (kvm_pte_valid(old))
		return old == pte;

	smp_store_release(ptep, pte);
	return true;
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
