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
#include <asm/stage2_pgtable.h>


#define KVM_PTE_LEAF_ATTR_S2_PERMS	(KVM_PTE_LEAF_ATTR_LO_S2_S2AP_R | \
					 KVM_PTE_LEAF_ATTR_LO_S2_S2AP_W | \
					 KVM_PTE_LEAF_ATTR_HI_S2_XN)

struct kvm_pgtable_walk_data {
	struct kvm_pgtable		*pgt;
	struct kvm_pgtable_walker	*walker;

	u64				addr;
	u64				end;
};

static bool kvm_phys_is_valid(u64 phys)
{
	return phys < BIT(id_aa64mmfr0_parange_to_phys_shift(ID_AA64MMFR0_EL1_PARANGE_MAX));
}

static bool kvm_block_mapping_supported(u64 addr, u64 end, u64 phys, u32 level)
{
	u64 granule = kvm_granule_size(level);

	if (!kvm_level_supports_block_mapping(level))
		return false;

	if (granule > (end - addr))
		return false;

	if (kvm_phys_is_valid(phys) && !IS_ALIGNED(phys, granule))
		return false;

	return IS_ALIGNED(addr, granule);
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

static void kvm_clear_pte(kvm_pte_t *ptep)
{
	WRITE_ONCE(*ptep, 0);
}

static void kvm_set_table_pte(kvm_pte_t *ptep, kvm_pte_t *childp,
			      struct kvm_pgtable_mm_ops *mm_ops)
{
	kvm_pte_t old = *ptep, pte = kvm_phys_to_pte(mm_ops->virt_to_phys(childp));

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
		data->addr = ALIGN_DOWN(data->addr, kvm_granule_size(level));
		data->addr += kvm_granule_size(level);
		goto out;
	}

	childp = kvm_pte_follow(pte, data->pgt->mm_ops);
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

struct leaf_walk_data {
	kvm_pte_t	pte;
	u32		level;
};

static int leaf_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
		       enum kvm_pgtable_walk_flags flag, void * const arg)
{
	struct leaf_walk_data *data = arg;

	data->pte   = *ptep;
	data->level = level;

	return 0;
}

int kvm_pgtable_get_leaf(struct kvm_pgtable *pgt, u64 addr,
			 kvm_pte_t *ptep, u32 *level)
{
	struct leaf_walk_data data;
	struct kvm_pgtable_walker walker = {
		.cb	= leaf_walker,
		.flags	= KVM_PGTABLE_WALK_LEAF,
		.arg	= &data,
	};
	int ret;

	ret = kvm_pgtable_walk(pgt, ALIGN_DOWN(addr, PAGE_SIZE),
			       PAGE_SIZE, &walker);
	if (!ret) {
		if (ptep)
			*ptep  = data.pte;
		if (level)
			*level = data.level;
	}

	return ret;
}

struct hyp_map_data {
	u64				phys;
	kvm_pte_t			attr;
	struct kvm_pgtable_mm_ops	*mm_ops;
};

static int hyp_set_prot_attr(enum kvm_pgtable_prot prot, kvm_pte_t *ptep)
{
	u32 ap = (prot & KVM_PGTABLE_PROT_W) ? KVM_PTE_LEAF_ATTR_LO_S1_AP_RW :
					       KVM_PTE_LEAF_ATTR_LO_S1_AP_RO;
	bool device = prot & KVM_PGTABLE_PROT_DEVICE;
	u32 sh = KVM_PTE_LEAF_ATTR_LO_S1_SH_IS;
	bool nc = prot & KVM_PGTABLE_PROT_NC;
	kvm_pte_t attr;
	u32 mtype;

	if (!(prot & KVM_PGTABLE_PROT_R) || (device && nc) ||
			(prot & (KVM_PGTABLE_PROT_PXN | KVM_PGTABLE_PROT_UXN)))
		return -EINVAL;

	if (device)
		mtype = MT_DEVICE_nGnRnE;
	else if (nc)
		mtype = MT_NORMAL_NC;
	else
		mtype = MT_NORMAL;

	attr = FIELD_PREP(KVM_PTE_LEAF_ATTR_LO_S1_ATTRIDX, mtype);

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
	attr |= prot & KVM_PTE_LEAF_ATTR_HI_SW;
	*ptep = attr;

	return 0;
}

enum kvm_pgtable_prot kvm_pgtable_hyp_pte_prot(kvm_pte_t pte)
{
	enum kvm_pgtable_prot prot = pte & KVM_PTE_LEAF_ATTR_HI_SW;
	u32 ap;

	if (!kvm_pte_valid(pte))
		return prot;

	if (!(pte & KVM_PTE_LEAF_ATTR_HI_S1_XN))
		prot |= KVM_PGTABLE_PROT_X;

	ap = FIELD_GET(KVM_PTE_LEAF_ATTR_LO_S1_AP, pte);
	if (ap == KVM_PTE_LEAF_ATTR_LO_S1_AP_RO)
		prot |= KVM_PGTABLE_PROT_R;
	else if (ap == KVM_PTE_LEAF_ATTR_LO_S1_AP_RW)
		prot |= KVM_PGTABLE_PROT_RW;

	return prot;
}

static bool hyp_map_walker_try_leaf(u64 addr, u64 end, u32 level,
				    kvm_pte_t *ptep, struct hyp_map_data *data)
{
	kvm_pte_t new, old = *ptep;
	u64 granule = kvm_granule_size(level), phys = data->phys;

	if (!kvm_block_mapping_supported(addr, end, phys, level))
		return false;

	data->phys += granule;
	new = kvm_init_valid_leaf_pte(phys, data->attr, level);
	if (old == new)
		return true;
	if (!kvm_pte_valid(old))
		data->mm_ops->get_page(ptep);
	else if (WARN_ON((old ^ new) & ~KVM_PTE_LEAF_ATTR_HI_SW))
		return false;

	smp_store_release(ptep, new);
	return true;
}

static int hyp_map_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			  enum kvm_pgtable_walk_flags flag, void * const arg)
{
	kvm_pte_t *childp;
	struct hyp_map_data *data = arg;
	struct kvm_pgtable_mm_ops *mm_ops = data->mm_ops;

	if (hyp_map_walker_try_leaf(addr, end, level, ptep, arg))
		return 0;

	if (WARN_ON(level == KVM_PGTABLE_MAX_LEVELS - 1))
		return -EINVAL;

	childp = (kvm_pte_t *)mm_ops->zalloc_page(NULL);
	if (!childp)
		return -ENOMEM;

	kvm_set_table_pte(ptep, childp, mm_ops);
	mm_ops->get_page(ptep);
	return 0;
}

int kvm_pgtable_hyp_map(struct kvm_pgtable *pgt, u64 addr, u64 size, u64 phys,
			enum kvm_pgtable_prot prot)
{
	int ret;
	struct hyp_map_data map_data = {
		.phys	= ALIGN_DOWN(phys, PAGE_SIZE),
		.mm_ops	= pgt->mm_ops,
	};
	struct kvm_pgtable_walker walker = {
		.cb	= hyp_map_walker,
		.flags	= KVM_PGTABLE_WALK_LEAF,
		.arg	= &map_data,
	};

	ret = hyp_set_prot_attr(prot, &map_data.attr);
	if (ret)
		return ret;

	ret = kvm_pgtable_walk(pgt, addr, size, &walker);
	dsb(ishst);
	isb();
	return ret;
}

struct hyp_unmap_data {
	u64				unmapped;
	struct kvm_pgtable_mm_ops	*mm_ops;
};

static int hyp_unmap_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			    enum kvm_pgtable_walk_flags flag, void * const arg)
{
	kvm_pte_t pte = *ptep, *childp = NULL;
	u64 granule = kvm_granule_size(level);
	struct hyp_unmap_data *data = arg;
	struct kvm_pgtable_mm_ops *mm_ops = data->mm_ops;

	if (!kvm_pte_valid(pte))
		return -EINVAL;

	if (kvm_pte_table(pte, level)) {
		childp = kvm_pte_follow(pte, mm_ops);

		if (mm_ops->page_count(childp) != 1)
			return 0;

		kvm_clear_pte(ptep);
		dsb(ishst);
		__tlbi_level(vae2is, __TLBI_VADDR(addr, 0), level);
	} else {
		if (end - addr < granule)
			return -EINVAL;

		kvm_clear_pte(ptep);
		dsb(ishst);
		__tlbi_level(vale2is, __TLBI_VADDR(addr, 0), level);
		data->unmapped += granule;
	}

	dsb(ish);
	isb();
	mm_ops->put_page(ptep);

	if (childp)
		mm_ops->put_page(childp);

	return 0;
}

u64 kvm_pgtable_hyp_unmap(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	struct hyp_unmap_data unmap_data = {
		.mm_ops	= pgt->mm_ops,
	};
	struct kvm_pgtable_walker walker = {
		.cb	= hyp_unmap_walker,
		.arg	= &unmap_data,
		.flags	= KVM_PGTABLE_WALK_LEAF | KVM_PGTABLE_WALK_TABLE_POST,
	};

	if (!pgt->mm_ops->page_count)
		return 0;

	kvm_pgtable_walk(pgt, addr, size, &walker);
	return unmap_data.unmapped;
}

int kvm_pgtable_hyp_init(struct kvm_pgtable *pgt, u32 va_bits,
			 struct kvm_pgtable_mm_ops *mm_ops)
{
	u64 levels = ARM64_HW_PGTABLE_LEVELS(va_bits);

	pgt->pgd = (kvm_pte_t *)mm_ops->zalloc_page(NULL);
	if (!pgt->pgd)
		return -ENOMEM;

	pgt->ia_bits		= va_bits;
	pgt->start_level	= KVM_PGTABLE_MAX_LEVELS - levels;
	pgt->mm_ops		= mm_ops;
	pgt->mmu		= NULL;
	pgt->pte_ops		= NULL;

	return 0;
}

static int hyp_free_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			   enum kvm_pgtable_walk_flags flag, void * const arg)
{
	struct kvm_pgtable_mm_ops *mm_ops = arg;
	kvm_pte_t pte = *ptep;

	if (!kvm_pte_valid(pte))
		return 0;

	mm_ops->put_page(ptep);

	if (kvm_pte_table(pte, level))
		mm_ops->put_page(kvm_pte_follow(pte, mm_ops));

	return 0;
}

void kvm_pgtable_hyp_destroy(struct kvm_pgtable *pgt)
{
	struct kvm_pgtable_walker walker = {
		.cb	= hyp_free_walker,
		.flags	= KVM_PGTABLE_WALK_LEAF | KVM_PGTABLE_WALK_TABLE_POST,
		.arg	= pgt->mm_ops,
	};

	WARN_ON(kvm_pgtable_walk(pgt, 0, BIT(pgt->ia_bits), &walker));
	pgt->mm_ops->put_page(pgt->pgd);
	pgt->pgd = NULL;
}

struct stage2_map_data {
	u64				phys;
	kvm_pte_t			attr;
	u64				annotation;

	kvm_pte_t			*anchor;
	kvm_pte_t			*childp;

	struct kvm_s2_mmu		*mmu;
	void				*memcache;

	struct kvm_pgtable_mm_ops	*mm_ops;

	/* Force mappings to page granularity */
	bool				force_pte;
};

u64 kvm_get_vtcr(u64 mmfr0, u64 mmfr1, u32 phys_shift)
{
	u64 vtcr = VTCR_EL2_FLAGS;
	u8 lvls;

	vtcr |= kvm_get_parange(mmfr0) << VTCR_EL2_PS_SHIFT;
	vtcr |= VTCR_EL2_T0SZ(phys_shift);
	/*
	 * Use a minimum 2 level page table to prevent splitting
	 * host PMD huge pages at stage2.
	 */
	lvls = stage2_pgtable_levels(phys_shift);
	if (lvls < 2)
		lvls = 2;
	vtcr |= VTCR_EL2_LVLS_TO_SL0(lvls);

	/*
	 * Enable the Hardware Access Flag management, unconditionally
	 * on all CPUs. The features is RES0 on CPUs without the support
	 * and must be ignored by the CPUs.
	 */
	vtcr |= VTCR_EL2_HA;

	/* Set the vmid bits */
	vtcr |= (get_vmid_bits(mmfr1) == 16) ?
		VTCR_EL2_VS_16BIT :
		VTCR_EL2_VS_8BIT;

	return vtcr;
}

static bool stage2_has_fwb(struct kvm_pgtable *pgt)
{
	if (!cpus_have_const_cap(ARM64_HAS_STAGE2_FWB))
		return false;

	return !(pgt->flags & KVM_PGTABLE_S2_NOFWB);
}

#define KVM_S2_MEMATTR(pgt, attr) PAGE_S2_MEMATTR(attr, stage2_has_fwb(pgt))

static int stage2_set_prot_attr(struct kvm_pgtable *pgt, enum kvm_pgtable_prot prot,
		kvm_pte_t *ptep)
{
	u64 exec_type = KVM_PTE_LEAF_ATTR_HI_S2_XN_XN;
	bool device = prot & KVM_PGTABLE_PROT_DEVICE;
	u32 sh = KVM_PTE_LEAF_ATTR_LO_S2_SH_IS;
	bool nc = prot & KVM_PGTABLE_PROT_NC;
	enum kvm_pgtable_prot exec_prot;
	kvm_pte_t attr;

	if (device)
		attr = KVM_S2_MEMATTR(pgt, DEVICE_nGnRE);
	else if (nc)
		attr = KVM_S2_MEMATTR(pgt, NORMAL_NC);
	else
		attr = KVM_S2_MEMATTR(pgt, NORMAL);

	exec_prot = prot & (KVM_PGTABLE_PROT_X | KVM_PGTABLE_PROT_PXN | KVM_PGTABLE_PROT_UXN);
	switch(exec_prot) {
	case KVM_PGTABLE_PROT_X:
		goto set_ap;
	case KVM_PGTABLE_PROT_PXN:
		exec_type = KVM_PTE_LEAF_ATTR_HI_S2_XN_PXN;
		break;
	case KVM_PGTABLE_PROT_UXN:
		exec_type = KVM_PTE_LEAF_ATTR_HI_S2_XN_UXN;
		break;
	default:
		if (exec_prot)
			return -EINVAL;
	}
	attr |= FIELD_PREP(KVM_PTE_LEAF_ATTR_HI_S2_XN, exec_type);

set_ap:
	if (prot & KVM_PGTABLE_PROT_R)
		attr |= KVM_PTE_LEAF_ATTR_LO_S2_S2AP_R;

	if (prot & KVM_PGTABLE_PROT_W)
		attr |= KVM_PTE_LEAF_ATTR_LO_S2_S2AP_W;

	attr |= FIELD_PREP(KVM_PTE_LEAF_ATTR_LO_S2_SH, sh);
	attr |= KVM_PTE_LEAF_ATTR_LO_S2_AF;
	attr |= prot & KVM_PTE_LEAF_ATTR_HI_SW;
	*ptep = attr;

	return 0;
}

enum kvm_pgtable_prot kvm_pgtable_stage2_pte_prot(kvm_pte_t pte)
{
	enum kvm_pgtable_prot prot = pte & KVM_PTE_LEAF_ATTR_HI_SW;

	if (!kvm_pte_valid(pte))
		return prot;

	if (pte & KVM_PTE_LEAF_ATTR_LO_S2_S2AP_R)
		prot |= KVM_PGTABLE_PROT_R;
	if (pte & KVM_PTE_LEAF_ATTR_LO_S2_S2AP_W)
		prot |= KVM_PGTABLE_PROT_W;
	switch(FIELD_GET(KVM_PTE_LEAF_ATTR_HI_S2_XN, pte)) {
	case 0:
		prot |= KVM_PGTABLE_PROT_X;
		break;
	case KVM_PTE_LEAF_ATTR_HI_S2_XN_PXN:
		prot |= KVM_PGTABLE_PROT_PXN;
		break;
	case KVM_PTE_LEAF_ATTR_HI_S2_XN_UXN:
		prot |= KVM_PGTABLE_PROT_UXN;
		break;
	case KVM_PTE_LEAF_ATTR_HI_S2_XN_XN:
		break;
	default:
		WARN_ON(1);
	}

	return prot;
}

static bool stage2_pte_needs_update(kvm_pte_t old, kvm_pte_t new)
{
	if (!kvm_pte_valid(old) || !kvm_pte_valid(new))
		return true;

	return ((old ^ new) & (~KVM_PTE_LEAF_ATTR_S2_PERMS));
}

static void stage2_clear_pte(kvm_pte_t *ptep, struct kvm_s2_mmu *mmu, u64 addr,
			     u32 level)
{
	if (!kvm_pte_valid(*ptep))
		return;

	kvm_clear_pte(ptep);
	kvm_call_hyp(__kvm_tlb_flush_vmid_ipa, mmu, addr, level);
}

static void stage2_put_pte(kvm_pte_t *ptep, struct kvm_s2_mmu *mmu, u64 addr,
			   u32 level, struct kvm_pgtable_mm_ops *mm_ops)
{
	/*
	 * Clear the existing PTE, and perform break-before-make with
	 * TLB maintenance if it was valid.
	 */
	stage2_clear_pte(ptep, mmu, addr, level);
	mm_ops->put_page(ptep);
}

static bool stage2_pte_cacheable(struct kvm_pgtable *pgt, kvm_pte_t pte)
{
	u64 memattr = pte & KVM_PTE_LEAF_ATTR_LO_S2_MEMATTR;
	return kvm_pte_valid(pte) && memattr == KVM_S2_MEMATTR(pgt, NORMAL);
}

static bool stage2_pte_executable(kvm_pte_t pte)
{
	kvm_pte_t xn = FIELD_GET(KVM_PTE_LEAF_ATTR_HI_S2_XN, pte);

	return kvm_pte_valid(pte) && xn != KVM_PTE_LEAF_ATTR_HI_S2_XN_XN;
}

static bool stage2_leaf_mapping_allowed(u64 addr, u64 end, u32 level,
					struct stage2_map_data *data)
{
	if (data->force_pte && (level < (KVM_PGTABLE_MAX_LEVELS - 1)))
		return false;

	return kvm_block_mapping_supported(addr, end, data->phys, level);
}

static int stage2_map_walker_try_leaf(u64 addr, u64 end, u32 level,
				      kvm_pte_t *ptep,
				      struct stage2_map_data *data)
{
	kvm_pte_t new, old = *ptep;
	u64 granule = kvm_granule_size(level), phys = data->phys;
	struct kvm_pgtable *pgt = data->mmu->pgt;
	struct kvm_pgtable_pte_ops *pte_ops = pgt->pte_ops;
	struct kvm_pgtable_mm_ops *mm_ops = data->mm_ops;

	if (!stage2_leaf_mapping_allowed(addr, end, level, data))
		return -E2BIG;

	if (kvm_phys_is_valid(phys))
		new = kvm_init_valid_leaf_pte(phys, data->attr, level);
	else
		new = data->annotation;

	/*
	 * Skip updating the PTE if we are trying to recreate the exact
	 * same mapping or only change the access permissions. Instead,
	 * the vCPU will exit one more time from guest if still needed
	 * and then go through the path of relaxing permissions.
	 */
	if (!stage2_pte_needs_update(old, new))
		return -EAGAIN;

	if (pte_ops->pte_is_counted_cb(old, level))
		mm_ops->put_page(ptep);

	/*
	 * If we're only changing software bits, then we don't need to
	 * do anything else.
	 */
	if (!((old ^ new) & ~KVM_PTE_LEAF_ATTR_HI_SW))
		goto out_set_pte;

	stage2_clear_pte(ptep, data->mmu, addr, level);

	/* Perform CMOs before installation of the guest stage-2 PTE */
	if (mm_ops->dcache_clean_inval_poc && stage2_pte_cacheable(pgt, new))
		mm_ops->dcache_clean_inval_poc(kvm_pte_follow(new, mm_ops),
					       granule);
	if (mm_ops->icache_inval_pou && stage2_pte_executable(new))
		mm_ops->icache_inval_pou(kvm_pte_follow(new, mm_ops), granule);

out_set_pte:
	if (pte_ops->pte_is_counted_cb(new, level))
		mm_ops->get_page(ptep);

	smp_store_release(ptep, new);
	if (kvm_phys_is_valid(phys))
		data->phys += granule;
	return 0;
}

static int stage2_map_walk_table_pre(u64 addr, u64 end, u32 level,
				     kvm_pte_t *ptep,
				     struct stage2_map_data *data)
{
	if (data->anchor)
		return 0;

	if (!stage2_leaf_mapping_allowed(addr, end, level, data))
		return 0;

	data->childp = kvm_pte_follow(*ptep, data->mm_ops);
	kvm_clear_pte(ptep);

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
	struct kvm_pgtable_mm_ops *mm_ops = data->mm_ops;
	struct kvm_pgtable *pgt = data->mmu->pgt;
	struct kvm_pgtable_pte_ops *pte_ops = pgt->pte_ops;
	kvm_pte_t *childp, pte = *ptep;
	int ret;

	if (data->anchor) {
		if (pte_ops->pte_is_counted_cb(pte, level))
			mm_ops->put_page(ptep);

		return 0;
	}

	ret = stage2_map_walker_try_leaf(addr, end, level, ptep, data);
	if (ret != -E2BIG)
		return ret;

	if (WARN_ON(level == KVM_PGTABLE_MAX_LEVELS - 1))
		return -EINVAL;

	if (!data->memcache)
		return -ENOMEM;

	childp = mm_ops->zalloc_page(data->memcache);
	if (!childp)
		return -ENOMEM;

	/*
	 * If we've run into an existing block mapping then replace it with
	 * a table. Accesses beyond 'end' that fall within the new table
	 * will be mapped lazily.
	 */
	if (pte_ops->pte_is_counted_cb(pte, level)) {
		stage2_put_pte(ptep, data->mmu, addr, level, mm_ops);
	} else {
		/*
		 * On non-refcounted PTEs we just clear them out without
		 * dropping the refcount.
		 */
		stage2_clear_pte(ptep, data->mmu, addr, level);
	}

	kvm_set_table_pte(ptep, childp, mm_ops);
	mm_ops->get_page(ptep);

	return 0;
}

static void stage2_coalesce_walk_table_post(u64 addr, u64 end, u32 level,
					    kvm_pte_t *ptep,
					    struct stage2_map_data *data)
{
	struct kvm_pgtable_mm_ops *mm_ops = data->mm_ops;
	kvm_pte_t *childp = kvm_pte_follow(*ptep, mm_ops);

	/*
	 * Decrement the refcount only on the set ownership path to avoid a
	 * loop situation when the following happens:
	 *  1. We take a host stage2 fault and we create a small mapping which
	 *  has default attributes (is not refcounted).
	 *  2. On the way back we execute the post handler and we zap the
	 *  table that holds our mapping.
	 */
	if (kvm_phys_is_valid(data->phys) ||
	    !kvm_level_supports_block_mapping(level))
		return;

	/*
	 * Free a page that is not referenced anymore and drop the reference
	 * of the page table page.
	 */
	if (mm_ops->page_count(childp) == 1) {
		stage2_put_pte(ptep, data->mmu, addr, level, mm_ops);
		mm_ops->put_page(childp);
	}
}

static int stage2_map_walk_table_post(u64 addr, u64 end, u32 level,
				      kvm_pte_t *ptep,
				      struct stage2_map_data *data)
{
	struct kvm_pgtable_mm_ops *mm_ops = data->mm_ops;
	kvm_pte_t *childp;
	int ret = 0;

	if (!data->anchor) {
		stage2_coalesce_walk_table_post(addr, end, level, ptep,
						data);
		return 0;
	}

	if (data->anchor == ptep) {
		childp = data->childp;
		data->anchor = NULL;
		data->childp = NULL;
		ret = stage2_map_walk_leaf(addr, end, level, ptep, data);
	} else {
		childp = kvm_pte_follow(*ptep, mm_ops);
	}

	mm_ops->put_page(childp);
	mm_ops->put_page(ptep);

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
			   void *mc)
{
	int ret;
	struct kvm_pgtable_pte_ops *pte_ops = pgt->pte_ops;
	struct stage2_map_data map_data = {
		.phys		= ALIGN_DOWN(phys, PAGE_SIZE),
		.mmu		= pgt->mmu,
		.memcache	= mc,
		.mm_ops		= pgt->mm_ops,
	};
	struct kvm_pgtable_walker walker = {
		.cb		= stage2_map_walker,
		.flags		= KVM_PGTABLE_WALK_TABLE_PRE |
				  KVM_PGTABLE_WALK_LEAF |
				  KVM_PGTABLE_WALK_TABLE_POST,
		.arg		= &map_data,
	};

	if (pte_ops->force_pte_cb)
		map_data.force_pte = pte_ops->force_pte_cb(addr, addr + size, prot);

	if (WARN_ON((pgt->flags & KVM_PGTABLE_S2_IDMAP) && (addr != phys)))
		return -EINVAL;

	ret = stage2_set_prot_attr(pgt, prot, &map_data.attr);
	if (ret)
		return ret;

	ret = kvm_pgtable_walk(pgt, addr, size, &walker);
	dsb(ishst);
	return ret;
}

int kvm_pgtable_stage2_annotate(struct kvm_pgtable *pgt, u64 addr, u64 size,
				void *mc, kvm_pte_t annotation)
{
	int ret;
	struct stage2_map_data map_data = {
		.phys		= KVM_PHYS_INVALID,
		.mmu		= pgt->mmu,
		.memcache	= mc,
		.mm_ops		= pgt->mm_ops,
		.force_pte	= true,
		.annotation	= annotation,
	};
	struct kvm_pgtable_walker walker = {
		.cb		= stage2_map_walker,
		.flags		= KVM_PGTABLE_WALK_TABLE_PRE |
				  KVM_PGTABLE_WALK_LEAF |
				  KVM_PGTABLE_WALK_TABLE_POST,
		.arg		= &map_data,
	};

	if (annotation & PTE_VALID)
		return -EINVAL;

	ret = kvm_pgtable_walk(pgt, addr, size, &walker);
	return ret;
}

static int stage2_unmap_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			       enum kvm_pgtable_walk_flags flag,
			       void * const arg)
{
	struct kvm_pgtable *pgt = arg;
	struct kvm_s2_mmu *mmu = pgt->mmu;
	struct kvm_pgtable_mm_ops *mm_ops = pgt->mm_ops;
	struct kvm_pgtable_pte_ops *pte_ops = pgt->pte_ops;
	kvm_pte_t pte = *ptep, *childp = NULL;
	bool need_flush = false;

	if (!kvm_pte_valid(pte)) {
		if (pte_ops->pte_is_counted_cb(pte, level)) {
			kvm_clear_pte(ptep);
			mm_ops->put_page(ptep);
		}
		return 0;
	}

	if (kvm_pte_table(pte, level)) {
		childp = kvm_pte_follow(pte, mm_ops);

		if (mm_ops->page_count(childp) != 1)
			return 0;
	} else if (stage2_pte_cacheable(pgt, pte)) {
		need_flush = !stage2_has_fwb(pgt);
	}

	/*
	 * This is similar to the map() path in that we unmap the entire
	 * block entry and rely on the remaining portions being faulted
	 * back lazily.
	 */
	if (pte_ops->pte_is_counted_cb(pte, level))
		stage2_put_pte(ptep, mmu, addr, level, mm_ops);
	else
		stage2_clear_pte(ptep, mmu, addr, level);

	if (need_flush && mm_ops->dcache_clean_inval_poc)
		mm_ops->dcache_clean_inval_poc(kvm_pte_follow(pte, mm_ops),
					       kvm_granule_size(level));

	if (childp)
		mm_ops->put_page(childp);

	return 0;
}

int kvm_pgtable_stage2_unmap(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	struct kvm_pgtable_walker walker = {
		.cb	= stage2_unmap_walker,
		.arg	= pgt,
		.flags	= KVM_PGTABLE_WALK_LEAF | KVM_PGTABLE_WALK_TABLE_POST,
	};

	return kvm_pgtable_walk(pgt, addr, size, &walker);
}

static int stage2_reclaim_leaf_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
				      enum kvm_pgtable_walk_flags flag, void * const arg)
{
	stage2_coalesce_walk_table_post(addr, end, level, ptep, arg);

	return 0;
}

int kvm_pgtable_stage2_reclaim_leaves(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	struct stage2_map_data map_data = {
		.phys		= KVM_PHYS_INVALID,
		.mmu		= pgt->mmu,
		.mm_ops		= pgt->mm_ops,
	};
	struct kvm_pgtable_walker walker = {
		.cb	= stage2_reclaim_leaf_walker,
		.arg	= &map_data,
		.flags	= KVM_PGTABLE_WALK_TABLE_POST,
	};

	return kvm_pgtable_walk(pgt, addr, size, &walker);
}

struct stage2_attr_data {
	kvm_pte_t			attr_set;
	kvm_pte_t			attr_clr;
	kvm_pte_t			pte;
	u32				level;
	struct kvm_pgtable_mm_ops	*mm_ops;
};

static int stage2_attr_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			      enum kvm_pgtable_walk_flags flag,
			      void * const arg)
{
	kvm_pte_t pte = *ptep;
	struct stage2_attr_data *data = arg;
	struct kvm_pgtable_mm_ops *mm_ops = data->mm_ops;

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
	if (data->pte != pte) {
		/*
		 * Invalidate instruction cache before updating the guest
		 * stage-2 PTE if we are going to add executable permission.
		 */
		if (mm_ops->icache_inval_pou &&
		    stage2_pte_executable(pte) && !stage2_pte_executable(*ptep))
			mm_ops->icache_inval_pou(kvm_pte_follow(pte, mm_ops),
						  kvm_granule_size(level));
		WRITE_ONCE(*ptep, pte);
	}

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
		.mm_ops		= pgt->mm_ops,
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

	if (prot & !KVM_PGTABLE_PROT_RWX)
		return -EINVAL;

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
	struct kvm_pgtable *pgt = arg;
	struct kvm_pgtable_mm_ops *mm_ops = pgt->mm_ops;
	kvm_pte_t pte = *ptep;

	if (!stage2_pte_cacheable(pgt, pte))
		return 0;

	if (mm_ops->dcache_clean_inval_poc)
		mm_ops->dcache_clean_inval_poc(kvm_pte_follow(pte, mm_ops),
					       kvm_granule_size(level));
	return 0;
}

int kvm_pgtable_stage2_flush(struct kvm_pgtable *pgt, u64 addr, u64 size)
{
	struct kvm_pgtable_walker walker = {
		.cb	= stage2_flush_walker,
		.flags	= KVM_PGTABLE_WALK_LEAF,
		.arg	= pgt,
	};

	if (stage2_has_fwb(pgt))
		return 0;

	return kvm_pgtable_walk(pgt, addr, size, &walker);
}


int __kvm_pgtable_stage2_init(struct kvm_pgtable *pgt, struct kvm_s2_mmu *mmu,
			      struct kvm_pgtable_mm_ops *mm_ops,
			      enum kvm_pgtable_stage2_flags flags,
			      struct kvm_pgtable_pte_ops *pte_ops)
{
	size_t pgd_sz;
	u64 vtcr = mmu->arch->vtcr;
	u32 ia_bits = VTCR_EL2_IPA(vtcr);
	u32 sl0 = FIELD_GET(VTCR_EL2_SL0_MASK, vtcr);
	u32 start_level = VTCR_EL2_TGRAN_SL0_BASE - sl0;

	pgd_sz = kvm_pgd_pages(ia_bits, start_level) * PAGE_SIZE;
	pgt->pgd = mm_ops->zalloc_pages_exact(pgd_sz);
	if (!pgt->pgd)
		return -ENOMEM;

	pgt->ia_bits		= ia_bits;
	pgt->start_level	= start_level;
	pgt->mm_ops		= mm_ops;
	pgt->mmu		= mmu;
	pgt->flags		= flags;
	pgt->pte_ops		= pte_ops;

	/* Ensure zeroed PGD pages are visible to the hardware walker */
	dsb(ishst);
	return 0;
}

size_t kvm_pgtable_stage2_pgd_size(u64 vtcr)
{
	u32 ia_bits = VTCR_EL2_IPA(vtcr);
	u32 sl0 = FIELD_GET(VTCR_EL2_SL0_MASK, vtcr);
	u32 start_level = VTCR_EL2_TGRAN_SL0_BASE - sl0;

	return kvm_pgd_pages(ia_bits, start_level) * PAGE_SIZE;
}

static int stage2_free_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			      enum kvm_pgtable_walk_flags flag,
			      void * const arg)
{
	struct kvm_pgtable *pgt = arg;
	struct kvm_pgtable_mm_ops *mm_ops = pgt->mm_ops;
	struct kvm_pgtable_pte_ops *pte_ops = pgt->pte_ops;
	kvm_pte_t pte = *ptep;

	if (!pte_ops->pte_is_counted_cb(pte, level))
		return 0;

	mm_ops->put_page(ptep);

	if (kvm_pte_table(pte, level))
		mm_ops->put_page(kvm_pte_follow(pte, mm_ops));

	return 0;
}

void kvm_pgtable_stage2_destroy(struct kvm_pgtable *pgt)
{
	size_t pgd_sz;
	struct kvm_pgtable_walker walker = {
		.cb	= stage2_free_walker,
		.flags	= KVM_PGTABLE_WALK_LEAF |
			  KVM_PGTABLE_WALK_TABLE_POST,
		.arg	= pgt,
	};

	WARN_ON(kvm_pgtable_walk(pgt, 0, BIT(pgt->ia_bits), &walker));
	pgd_sz = kvm_pgd_pages(pgt->ia_bits, pgt->start_level) * PAGE_SIZE;
	pgt->mm_ops->free_pages_exact(pgt->pgd, pgd_sz);
	pgt->pgd = NULL;
}
