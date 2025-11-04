/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 *
 * "Templated C code" for implementing the iommu operations for page tables.
 * This is compiled multiple times, over all the page table formats to pick up
 * the per-format definitions.
 */
#ifndef __GENERIC_PT_IOMMU_PT_H
#define __GENERIC_PT_IOMMU_PT_H

#include "pt_iter.h"

#include <linux/export.h>
#include <linux/iommu.h>
#include "../iommu-pages.h"

#define DOMAIN_NS(op) CONCATENATE(CONCATENATE(pt_iommu_, PTPFX), op)

static int make_range_ul(struct pt_common *common, struct pt_range *range,
			 unsigned long iova, unsigned long len)
{
	unsigned long last;

	if (unlikely(len == 0))
		return -EINVAL;

	if (check_add_overflow(iova, len - 1, &last))
		return -EOVERFLOW;

	*range = pt_make_range(common, iova, last);
	if (sizeof(iova) > sizeof(range->va)) {
		if (unlikely(range->va != iova || range->last_va != last))
			return -EOVERFLOW;
	}
	return 0;
}

static __maybe_unused int make_range_u64(struct pt_common *common,
					 struct pt_range *range, u64 iova,
					 u64 len)
{
	if (unlikely(iova > ULONG_MAX || len > ULONG_MAX))
		return -EOVERFLOW;
	return make_range_ul(common, range, iova, len);
}

/*
 * Some APIs use unsigned long, while othersuse dma_addr_t as the type. Dispatch
 * to the correct validation based on the type.
 */
#define make_range_no_check(common, range, iova, len)                   \
	({                                                              \
		int ret;                                                \
		if (sizeof(iova) > sizeof(unsigned long) ||             \
		    sizeof(len) > sizeof(unsigned long))                \
			ret = make_range_u64(common, range, iova, len); \
		else                                                    \
			ret = make_range_ul(common, range, iova, len);  \
		ret;                                                    \
	})

#define make_range(common, range, iova, len)                             \
	({                                                               \
		int ret = make_range_no_check(common, range, iova, len); \
		if (!ret)                                                \
			ret = pt_check_range(range);                     \
		ret;                                                     \
	})

static __always_inline int __do_iova_to_phys(struct pt_range *range, void *arg,
					     unsigned int level,
					     struct pt_table_p *table,
					     pt_level_fn_t descend_fn)
{
	struct pt_state pts = pt_init(range, level, table);
	pt_oaddr_t *res = arg;

	switch (pt_load_single_entry(&pts)) {
	case PT_ENTRY_EMPTY:
		return -ENOENT;
	case PT_ENTRY_TABLE:
		return pt_descend(&pts, arg, descend_fn);
	case PT_ENTRY_OA:
		*res = pt_entry_oa_exact(&pts);
		return 0;
	}
	return -ENOENT;
}
PT_MAKE_LEVELS(__iova_to_phys, __do_iova_to_phys);

/**
 * iova_to_phys() - Return the output address for the given IOVA
 * @iommu_table: Table to query
 * @iova: IO virtual address to query
 *
 * Determine the output address from the given IOVA. @iova may have any
 * alignment, the returned physical will be adjusted with any sub page offset.
 *
 * Context: The caller must hold a read range lock that includes @iova.
 *
 * Return: 0 if there is no translation for the given iova.
 */
phys_addr_t DOMAIN_NS(iova_to_phys)(struct iommu_domain *domain,
				    dma_addr_t iova)
{
	struct pt_iommu *iommu_table =
		container_of(domain, struct pt_iommu, domain);
	struct pt_range range;
	pt_oaddr_t res;
	int ret;

	ret = make_range(common_from_iommu(iommu_table), &range, iova, 1);
	if (ret)
		return ret;

	ret = pt_walk_range(&range, __iova_to_phys, &res);
	/* PHYS_ADDR_MAX would be a better error code */
	if (ret)
		return 0;
	return res;
}
EXPORT_SYMBOL_NS_GPL(DOMAIN_NS(iova_to_phys), "GENERIC_PT_IOMMU");

struct pt_iommu_collect_args {
	struct iommu_pages_list free_list;
};

static int __collect_tables(struct pt_range *range, void *arg,
			    unsigned int level, struct pt_table_p *table)
{
	struct pt_state pts = pt_init(range, level, table);
	struct pt_iommu_collect_args *collect = arg;
	int ret;

	if (!pt_can_have_table(&pts))
		return 0;

	for_each_pt_level_entry(&pts) {
		if (pts.type == PT_ENTRY_TABLE) {
			iommu_pages_list_add(&collect->free_list, pts.table_lower);
			ret = pt_descend(&pts, arg, __collect_tables);
			if (ret)
				return ret;
			continue;
		}
	}
	return 0;
}

static inline struct pt_table_p *table_alloc_top(struct pt_common *common,
						 uintptr_t top_of_table,
						 gfp_t gfp)
{
	struct pt_iommu *iommu_table = iommu_from_common(common);

	/*
	 * Top doesn't need the free list or otherwise, so it technically
	 * doesn't need to use iommu pages. Use the API anyhow as the top is
	 * usually not smaller than PAGE_SIZE to keep things simple.
	 */
	return iommu_alloc_pages_node_sz(
		iommu_table->nid, gfp,
		log2_to_int(pt_top_memsize_lg2(common, top_of_table)));
}

static void NS(get_info)(struct pt_iommu *iommu_table,
			 struct pt_iommu_info *info)
{
	struct pt_common *common = common_from_iommu(iommu_table);
	struct pt_range range = pt_top_range(common);
	struct pt_state pts = pt_init_top(&range);
	pt_vaddr_t pgsize_bitmap = 0;

	if (pt_feature(common, PT_FEAT_DYNAMIC_TOP)) {
		for (pts.level = 0; pts.level <= PT_MAX_TOP_LEVEL;
		     pts.level++) {
			if (pt_table_item_lg2sz(&pts) >= common->max_vasz_lg2)
				break;
			pgsize_bitmap |= pt_possible_sizes(&pts);
		}
	} else {
		for (pts.level = 0; pts.level <= range.top_level; pts.level++)
			pgsize_bitmap |= pt_possible_sizes(&pts);
	}

	/* Hide page sizes larger than the maximum OA */
	info->pgsize_bitmap = oalog2_mod(pgsize_bitmap, common->max_oasz_lg2);
}

static void NS(deinit)(struct pt_iommu *iommu_table)
{
	struct pt_common *common = common_from_iommu(iommu_table);
	struct pt_range range = pt_all_range(common);
	struct pt_iommu_collect_args collect = {
		.free_list = IOMMU_PAGES_LIST_INIT(collect.free_list),
	};

	iommu_pages_list_add(&collect.free_list, range.top_table);
	pt_walk_range(&range, __collect_tables, &collect);

	/*
	 * The driver has to already have fenced the HW access to the page table
	 * and invalidated any caching referring to this memory.
	 */
	iommu_put_pages_list(&collect.free_list);
}

static const struct pt_iommu_ops NS(ops) = {
	.get_info = NS(get_info),
	.deinit = NS(deinit),
};

static int pt_init_common(struct pt_common *common)
{
	struct pt_range top_range = pt_top_range(common);

	if (PT_WARN_ON(top_range.top_level > PT_MAX_TOP_LEVEL))
		return -EINVAL;

	if (top_range.top_level == PT_MAX_TOP_LEVEL ||
	    common->max_vasz_lg2 == top_range.max_vasz_lg2)
		common->features &= ~BIT(PT_FEAT_DYNAMIC_TOP);

	if (top_range.max_vasz_lg2 == PT_VADDR_MAX_LG2)
		common->features |= BIT(PT_FEAT_FULL_VA);

	/* Requested features must match features compiled into this format */
	if ((common->features & ~(unsigned int)PT_SUPPORTED_FEATURES) ||
	    (!IS_ENABLED(CONFIG_DEBUG_GENERIC_PT) &&
	     (common->features & PT_FORCE_ENABLED_FEATURES) !=
		     PT_FORCE_ENABLED_FEATURES))
		return -EOPNOTSUPP;

	if (common->max_oasz_lg2 == 0)
		common->max_oasz_lg2 = pt_max_oa_lg2(common);
	else
		common->max_oasz_lg2 = min(common->max_oasz_lg2,
					   pt_max_oa_lg2(common));
	return 0;
}

static int pt_iommu_init_domain(struct pt_iommu *iommu_table,
				struct iommu_domain *domain)
{
	struct pt_common *common = common_from_iommu(iommu_table);
	struct pt_iommu_info info;
	struct pt_range range;

	NS(get_info)(iommu_table, &info);

	domain->type = __IOMMU_DOMAIN_PAGING;
	domain->pgsize_bitmap = info.pgsize_bitmap;

	if (pt_feature(common, PT_FEAT_DYNAMIC_TOP))
		range = _pt_top_range(common,
				      _pt_top_set(NULL, PT_MAX_TOP_LEVEL));
	else
		range = pt_top_range(common);

	/* A 64-bit high address space table on a 32-bit system cannot work. */
	domain->geometry.aperture_start = (unsigned long)range.va;
	if ((pt_vaddr_t)domain->geometry.aperture_start != range.va)
		return -EOVERFLOW;

	/*
	 * The aperture is limited to what the API can do after considering all
	 * the different types dma_addr_t/unsigned long/pt_vaddr_t that are used
	 * to store a VA. Set the aperture to something that is valid for all
	 * cases. Saturate instead of truncate the end if the types are smaller
	 * than the top range. aperture_end should be called aperture_last.
	 */
	domain->geometry.aperture_end = (unsigned long)range.last_va;
	if ((pt_vaddr_t)domain->geometry.aperture_end != range.last_va) {
		domain->geometry.aperture_end = ULONG_MAX;
		domain->pgsize_bitmap &= ULONG_MAX;
	}
	domain->geometry.force_aperture = true;

	return 0;
}

static void pt_iommu_zero(struct pt_iommu_table *fmt_table)
{
	struct pt_iommu *iommu_table = &fmt_table->iommu;
	struct pt_iommu cfg = *iommu_table;

	static_assert(offsetof(struct pt_iommu_table, iommu.domain) == 0);
	memset_after(fmt_table, 0, iommu.domain);

	/* The caller can initialize some of these values */
	iommu_table->nid = cfg.nid;
}

#define pt_iommu_table_cfg CONCATENATE(pt_iommu_table, _cfg)
#define pt_iommu_init CONCATENATE(CONCATENATE(pt_iommu_, PTPFX), init)

int pt_iommu_init(struct pt_iommu_table *fmt_table,
		  const struct pt_iommu_table_cfg *cfg, gfp_t gfp)
{
	struct pt_iommu *iommu_table = &fmt_table->iommu;
	struct pt_common *common = common_from_iommu(iommu_table);
	struct pt_table_p *table_mem;
	int ret;

	if (cfg->common.hw_max_vasz_lg2 > PT_MAX_VA_ADDRESS_LG2 ||
	    !cfg->common.hw_max_vasz_lg2 || !cfg->common.hw_max_oasz_lg2)
		return -EINVAL;

	pt_iommu_zero(fmt_table);
	common->features = cfg->common.features;
	common->max_vasz_lg2 = cfg->common.hw_max_vasz_lg2;
	common->max_oasz_lg2 = cfg->common.hw_max_oasz_lg2;
	ret = pt_iommu_fmt_init(fmt_table, cfg);
	if (ret)
		return ret;

	if (cfg->common.hw_max_oasz_lg2 > pt_max_oa_lg2(common))
		return -EINVAL;

	ret = pt_init_common(common);
	if (ret)
		return ret;

	if (pt_feature(common, PT_FEAT_SIGN_EXTEND) &&
	    (pt_feature(common, PT_FEAT_FULL_VA) ||
	     pt_feature(common, PT_FEAT_DYNAMIC_TOP)))
		return -EINVAL;

	ret = pt_iommu_init_domain(iommu_table, &iommu_table->domain);
	if (ret)
		return ret;

	table_mem = table_alloc_top(common, common->top_of_table, gfp);
	if (IS_ERR(table_mem))
		return PTR_ERR(table_mem);
	pt_top_set(common, table_mem, pt_top_get_level(common));

	/* Must be last, see pt_iommu_deinit() */
	iommu_table->ops = &NS(ops);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(pt_iommu_init, "GENERIC_PT_IOMMU");

#ifdef pt_iommu_fmt_hw_info
#define pt_iommu_table_hw_info CONCATENATE(pt_iommu_table, _hw_info)
#define pt_iommu_hw_info CONCATENATE(CONCATENATE(pt_iommu_, PTPFX), hw_info)
void pt_iommu_hw_info(struct pt_iommu_table *fmt_table,
		      struct pt_iommu_table_hw_info *info)
{
	struct pt_iommu *iommu_table = &fmt_table->iommu;
	struct pt_common *common = common_from_iommu(iommu_table);
	struct pt_range top_range = pt_top_range(common);

	pt_iommu_fmt_hw_info(fmt_table, &top_range, info);
}
EXPORT_SYMBOL_NS_GPL(pt_iommu_hw_info, "GENERIC_PT_IOMMU");
#endif

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IOMMU Page table implementation for " __stringify(PTPFX_RAW));
MODULE_IMPORT_NS("GENERIC_PT");

#endif  /* __GENERIC_PT_IOMMU_PT_H */
