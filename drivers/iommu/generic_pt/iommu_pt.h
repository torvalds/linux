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
#include <linux/cleanup.h>
#include <linux/dma-mapping.h>

enum {
	SW_BIT_CACHE_FLUSH_DONE = 0,
};

static void flush_writes_range(const struct pt_state *pts,
			       unsigned int start_index, unsigned int end_index)
{
	if (pts_feature(pts, PT_FEAT_DMA_INCOHERENT))
		iommu_pages_flush_incoherent(
			iommu_from_common(pts->range->common)->iommu_device,
			pts->table, start_index * PT_ITEM_WORD_SIZE,
			(end_index - start_index) * PT_ITEM_WORD_SIZE);
}

static void flush_writes_item(const struct pt_state *pts)
{
	if (pts_feature(pts, PT_FEAT_DMA_INCOHERENT))
		iommu_pages_flush_incoherent(
			iommu_from_common(pts->range->common)->iommu_device,
			pts->table, pts->index * PT_ITEM_WORD_SIZE,
			PT_ITEM_WORD_SIZE);
}

static void gather_range_pages(struct iommu_iotlb_gather *iotlb_gather,
			       struct pt_iommu *iommu_table, pt_vaddr_t iova,
			       pt_vaddr_t len,
			       struct iommu_pages_list *free_list)
{
	struct pt_common *common = common_from_iommu(iommu_table);

	if (pt_feature(common, PT_FEAT_DMA_INCOHERENT))
		iommu_pages_stop_incoherent_list(free_list,
						 iommu_table->iommu_device);

	if (pt_feature(common, PT_FEAT_FLUSH_RANGE_NO_GAPS) &&
	    iommu_iotlb_gather_is_disjoint(iotlb_gather, iova, len)) {
		iommu_iotlb_sync(&iommu_table->domain, iotlb_gather);
		/*
		 * Note that the sync frees the gather's free list, so we must
		 * not have any pages on that list that are covered by iova/len
		 */
	}

	iommu_iotlb_gather_add_range(iotlb_gather, iova, len);
	iommu_pages_list_splice(free_list, &iotlb_gather->freelist);
}

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

static inline unsigned int compute_best_pgsize(struct pt_state *pts,
					       pt_oaddr_t oa)
{
	struct pt_iommu *iommu_table = iommu_from_common(pts->range->common);

	if (!pt_can_have_leaf(pts))
		return 0;

	/*
	 * The page size is limited by the domain's bitmap. This allows the core
	 * code to reduce the supported page sizes by changing the bitmap.
	 */
	return pt_compute_best_pgsize(pt_possible_sizes(pts) &
					      iommu_table->domain.pgsize_bitmap,
				      pts->range->va, pts->range->last_va, oa);
}

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
 * @domain: Table to query
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

struct pt_iommu_dirty_args {
	struct iommu_dirty_bitmap *dirty;
	unsigned int flags;
};

static void record_dirty(struct pt_state *pts,
			 struct pt_iommu_dirty_args *dirty,
			 unsigned int num_contig_lg2)
{
	pt_vaddr_t dirty_len;

	if (num_contig_lg2 != ilog2(1)) {
		unsigned int index = pts->index;
		unsigned int end_index = log2_set_mod_max_t(
			unsigned int, pts->index, num_contig_lg2);

		/* Adjust for being contained inside a contiguous page */
		end_index = min(end_index, pts->end_index);
		dirty_len = (end_index - index) *
				log2_to_int(pt_table_item_lg2sz(pts));
	} else {
		dirty_len = log2_to_int(pt_table_item_lg2sz(pts));
	}

	if (dirty->dirty->bitmap)
		iova_bitmap_set(dirty->dirty->bitmap, pts->range->va,
				dirty_len);

	if (!(dirty->flags & IOMMU_DIRTY_NO_CLEAR)) {
		/*
		 * No write log required because DMA incoherence and atomic
		 * dirty tracking bits can't work together
		 */
		pt_entry_make_write_clean(pts);
		iommu_iotlb_gather_add_range(dirty->dirty->gather,
					     pts->range->va, dirty_len);
	}
}

static inline int __read_and_clear_dirty(struct pt_range *range, void *arg,
					 unsigned int level,
					 struct pt_table_p *table)
{
	struct pt_state pts = pt_init(range, level, table);
	struct pt_iommu_dirty_args *dirty = arg;
	int ret;

	for_each_pt_level_entry(&pts) {
		if (pts.type == PT_ENTRY_TABLE) {
			ret = pt_descend(&pts, arg, __read_and_clear_dirty);
			if (ret)
				return ret;
			continue;
		}
		if (pts.type == PT_ENTRY_OA && pt_entry_is_write_dirty(&pts))
			record_dirty(&pts, dirty,
				     pt_entry_num_contig_lg2(&pts));
	}
	return 0;
}

/**
 * read_and_clear_dirty() - Manipulate the HW set write dirty state
 * @domain: Domain to manipulate
 * @iova: IO virtual address to start
 * @size: Length of the IOVA
 * @flags: A bitmap of IOMMU_DIRTY_NO_CLEAR
 * @dirty: Place to store the dirty bits
 *
 * Iterate over all the entries in the mapped range and record their write dirty
 * status in iommu_dirty_bitmap. If IOMMU_DIRTY_NO_CLEAR is not specified then
 * the entries will be left dirty, otherwise they are returned to being not
 * write dirty.
 *
 * Context: The caller must hold a read range lock that includes @iova.
 *
 * Returns: -ERRNO on failure, 0 on success.
 */
int DOMAIN_NS(read_and_clear_dirty)(struct iommu_domain *domain,
				    unsigned long iova, size_t size,
				    unsigned long flags,
				    struct iommu_dirty_bitmap *dirty)
{
	struct pt_iommu *iommu_table =
		container_of(domain, struct pt_iommu, domain);
	struct pt_iommu_dirty_args dirty_args = {
		.dirty = dirty,
		.flags = flags,
	};
	struct pt_range range;
	int ret;

#if !IS_ENABLED(CONFIG_IOMMUFD_DRIVER) || !defined(pt_entry_is_write_dirty)
	return -EOPNOTSUPP;
#endif

	ret = make_range(common_from_iommu(iommu_table), &range, iova, size);
	if (ret)
		return ret;

	ret = pt_walk_range(&range, __read_and_clear_dirty, &dirty_args);
	PT_WARN_ON(ret);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(DOMAIN_NS(read_and_clear_dirty), "GENERIC_PT_IOMMU");

static inline int __set_dirty(struct pt_range *range, void *arg,
			      unsigned int level, struct pt_table_p *table)
{
	struct pt_state pts = pt_init(range, level, table);

	switch (pt_load_single_entry(&pts)) {
	case PT_ENTRY_EMPTY:
		return -ENOENT;
	case PT_ENTRY_TABLE:
		return pt_descend(&pts, arg, __set_dirty);
	case PT_ENTRY_OA:
		if (!pt_entry_make_write_dirty(&pts))
			return -EAGAIN;
		return 0;
	}
	return -ENOENT;
}

static int __maybe_unused NS(set_dirty)(struct pt_iommu *iommu_table,
					dma_addr_t iova)
{
	struct pt_range range;
	int ret;

	ret = make_range(common_from_iommu(iommu_table), &range, iova, 1);
	if (ret)
		return ret;

	/*
	 * Note: There is no locking here yet, if the test suite races this it
	 * can crash. It should use RCU locking eventually.
	 */
	return pt_walk_range(&range, __set_dirty, NULL);
}

struct pt_iommu_collect_args {
	struct iommu_pages_list free_list;
	/* Fail if any OAs are within the range */
	u8 check_mapped : 1;
};

static int __collect_tables(struct pt_range *range, void *arg,
			    unsigned int level, struct pt_table_p *table)
{
	struct pt_state pts = pt_init(range, level, table);
	struct pt_iommu_collect_args *collect = arg;
	int ret;

	if (!collect->check_mapped && !pt_can_have_table(&pts))
		return 0;

	for_each_pt_level_entry(&pts) {
		if (pts.type == PT_ENTRY_TABLE) {
			iommu_pages_list_add(&collect->free_list, pts.table_lower);
			ret = pt_descend(&pts, arg, __collect_tables);
			if (ret)
				return ret;
			continue;
		}
		if (pts.type == PT_ENTRY_OA && collect->check_mapped)
			return -EADDRINUSE;
	}
	return 0;
}

enum alloc_mode {ALLOC_NORMAL, ALLOC_DEFER_COHERENT_FLUSH};

/* Allocate a table, the empty table will be ready to be installed. */
static inline struct pt_table_p *_table_alloc(struct pt_common *common,
					      size_t lg2sz, gfp_t gfp,
					      enum alloc_mode mode)
{
	struct pt_iommu *iommu_table = iommu_from_common(common);
	struct pt_table_p *table_mem;

	table_mem = iommu_alloc_pages_node_sz(iommu_table->nid, gfp,
					      log2_to_int(lg2sz));
	if (!table_mem)
		return ERR_PTR(-ENOMEM);

	if (pt_feature(common, PT_FEAT_DMA_INCOHERENT) &&
	    mode == ALLOC_NORMAL) {
		int ret = iommu_pages_start_incoherent(
			table_mem, iommu_table->iommu_device);
		if (ret) {
			iommu_free_pages(table_mem);
			return ERR_PTR(ret);
		}
	}
	return table_mem;
}

static inline struct pt_table_p *table_alloc_top(struct pt_common *common,
						 uintptr_t top_of_table,
						 gfp_t gfp,
						 enum alloc_mode mode)
{
	/*
	 * Top doesn't need the free list or otherwise, so it technically
	 * doesn't need to use iommu pages. Use the API anyhow as the top is
	 * usually not smaller than PAGE_SIZE to keep things simple.
	 */
	return _table_alloc(common, pt_top_memsize_lg2(common, top_of_table),
			    gfp, mode);
}

/* Allocate an interior table */
static inline struct pt_table_p *table_alloc(const struct pt_state *parent_pts,
					     gfp_t gfp, enum alloc_mode mode)
{
	struct pt_state child_pts =
		pt_init(parent_pts->range, parent_pts->level - 1, NULL);

	return _table_alloc(parent_pts->range->common,
			    pt_num_items_lg2(&child_pts) +
				    ilog2(PT_ITEM_WORD_SIZE),
			    gfp, mode);
}

static inline int pt_iommu_new_table(struct pt_state *pts,
				     struct pt_write_attrs *attrs)
{
	struct pt_table_p *table_mem;
	phys_addr_t phys;

	/* Given PA/VA/length can't be represented */
	if (PT_WARN_ON(!pt_can_have_table(pts)))
		return -ENXIO;

	table_mem = table_alloc(pts, attrs->gfp, ALLOC_NORMAL);
	if (IS_ERR(table_mem))
		return PTR_ERR(table_mem);

	phys = virt_to_phys(table_mem);
	if (!pt_install_table(pts, phys, attrs)) {
		iommu_pages_free_incoherent(
			table_mem,
			iommu_from_common(pts->range->common)->iommu_device);
		return -EAGAIN;
	}

	if (pts_feature(pts, PT_FEAT_DMA_INCOHERENT)) {
		flush_writes_item(pts);
		pt_set_sw_bit_release(pts, SW_BIT_CACHE_FLUSH_DONE);
	}

	if (IS_ENABLED(CONFIG_DEBUG_GENERIC_PT)) {
		/*
		 * The underlying table can't store the physical table address.
		 * This happens when kunit testing tables outside their normal
		 * environment where a CPU might be limited.
		 */
		pt_load_single_entry(pts);
		if (PT_WARN_ON(pt_table_pa(pts) != phys)) {
			pt_clear_entries(pts, ilog2(1));
			iommu_pages_free_incoherent(
				table_mem, iommu_from_common(pts->range->common)
						   ->iommu_device);
			return -EINVAL;
		}
	}

	pts->table_lower = table_mem;
	return 0;
}

struct pt_iommu_map_args {
	struct iommu_iotlb_gather *iotlb_gather;
	struct pt_write_attrs attrs;
	pt_oaddr_t oa;
	unsigned int leaf_pgsize_lg2;
	unsigned int leaf_level;
};

/*
 * This will recursively check any tables in the block to validate they are
 * empty and then free them through the gather.
 */
static int clear_contig(const struct pt_state *start_pts,
			struct iommu_iotlb_gather *iotlb_gather,
			unsigned int step, unsigned int pgsize_lg2)
{
	struct pt_iommu *iommu_table =
		iommu_from_common(start_pts->range->common);
	struct pt_range range = *start_pts->range;
	struct pt_state pts =
		pt_init(&range, start_pts->level, start_pts->table);
	struct pt_iommu_collect_args collect = { .check_mapped = true };
	int ret;

	pts.index = start_pts->index;
	pts.end_index = start_pts->index + step;
	for (; _pt_iter_load(&pts); pt_next_entry(&pts)) {
		if (pts.type == PT_ENTRY_TABLE) {
			collect.free_list =
				IOMMU_PAGES_LIST_INIT(collect.free_list);
			ret = pt_walk_descend_all(&pts, __collect_tables,
						  &collect);
			if (ret)
				return ret;

			/*
			 * The table item must be cleared before we can update
			 * the gather
			 */
			pt_clear_entries(&pts, ilog2(1));
			flush_writes_item(&pts);

			iommu_pages_list_add(&collect.free_list,
					     pt_table_ptr(&pts));
			gather_range_pages(
				iotlb_gather, iommu_table, range.va,
				log2_to_int(pt_table_item_lg2sz(&pts)),
				&collect.free_list);
		} else if (pts.type != PT_ENTRY_EMPTY) {
			return -EADDRINUSE;
		}
	}
	return 0;
}

static int __map_range_leaf(struct pt_range *range, void *arg,
			    unsigned int level, struct pt_table_p *table)
{
	struct pt_state pts = pt_init(range, level, table);
	struct pt_iommu_map_args *map = arg;
	unsigned int leaf_pgsize_lg2 = map->leaf_pgsize_lg2;
	unsigned int start_index;
	pt_oaddr_t oa = map->oa;
	unsigned int step;
	bool need_contig;
	int ret = 0;

	PT_WARN_ON(map->leaf_level != level);
	PT_WARN_ON(!pt_can_have_leaf(&pts));

	step = log2_to_int_t(unsigned int,
			     leaf_pgsize_lg2 - pt_table_item_lg2sz(&pts));
	need_contig = leaf_pgsize_lg2 != pt_table_item_lg2sz(&pts);

	_pt_iter_first(&pts);
	start_index = pts.index;
	do {
		pts.type = pt_load_entry_raw(&pts);
		if (pts.type != PT_ENTRY_EMPTY || need_contig) {
			if (pts.index != start_index)
				pt_index_to_va(&pts);
			ret = clear_contig(&pts, map->iotlb_gather, step,
					   leaf_pgsize_lg2);
			if (ret)
				break;
		}

		if (IS_ENABLED(CONFIG_DEBUG_GENERIC_PT)) {
			pt_index_to_va(&pts);
			PT_WARN_ON(compute_best_pgsize(&pts, oa) !=
				   leaf_pgsize_lg2);
		}
		pt_install_leaf_entry(&pts, oa, leaf_pgsize_lg2, &map->attrs);

		oa += log2_to_int(leaf_pgsize_lg2);
		pts.index += step;
	} while (pts.index < pts.end_index);

	flush_writes_range(&pts, start_index, pts.index);

	map->oa = oa;
	return ret;
}

static int __map_range(struct pt_range *range, void *arg, unsigned int level,
		       struct pt_table_p *table)
{
	struct pt_state pts = pt_init(range, level, table);
	struct pt_iommu_map_args *map = arg;
	int ret;

	PT_WARN_ON(map->leaf_level == level);
	PT_WARN_ON(!pt_can_have_table(&pts));

	_pt_iter_first(&pts);

	/* Descend to a child table */
	do {
		pts.type = pt_load_entry_raw(&pts);

		if (pts.type != PT_ENTRY_TABLE) {
			if (pts.type != PT_ENTRY_EMPTY)
				return -EADDRINUSE;
			ret = pt_iommu_new_table(&pts, &map->attrs);
			if (ret) {
				/*
				 * Racing with another thread installing a table
				 */
				if (ret == -EAGAIN)
					continue;
				return ret;
			}
		} else {
			pts.table_lower = pt_table_ptr(&pts);
			/*
			 * Racing with a shared pt_iommu_new_table()? The other
			 * thread is still flushing the cache, so we have to
			 * also flush it to ensure that when our thread's map
			 * completes all the table items leading to our mapping
			 * are visible.
			 *
			 * This requires the pt_set_bit_release() to be a
			 * release of the cache flush so that this can acquire
			 * visibility at the iommu.
			 */
			if (pts_feature(&pts, PT_FEAT_DMA_INCOHERENT) &&
			    !pt_test_sw_bit_acquire(&pts,
						    SW_BIT_CACHE_FLUSH_DONE))
				flush_writes_item(&pts);
		}

		/*
		 * The already present table can possibly be shared with another
		 * concurrent map.
		 */
		if (map->leaf_level == level - 1)
			ret = pt_descend(&pts, arg, __map_range_leaf);
		else
			ret = pt_descend(&pts, arg, __map_range);
		if (ret)
			return ret;

		pts.index++;
		pt_index_to_va(&pts);
		if (pts.index >= pts.end_index)
			break;
	} while (true);
	return 0;
}

/*
 * Fast path for the easy case of mapping a 4k page to an already allocated
 * table. This is a common workload. If it returns EAGAIN run the full algorithm
 * instead.
 */
static __always_inline int __do_map_single_page(struct pt_range *range,
						void *arg, unsigned int level,
						struct pt_table_p *table,
						pt_level_fn_t descend_fn)
{
	struct pt_state pts = pt_init(range, level, table);
	struct pt_iommu_map_args *map = arg;

	pts.type = pt_load_single_entry(&pts);
	if (pts.level == 0) {
		if (pts.type != PT_ENTRY_EMPTY)
			return -EADDRINUSE;
		pt_install_leaf_entry(&pts, map->oa, PAGE_SHIFT,
				      &map->attrs);
		/* No flush, not used when incoherent */
		map->oa += PAGE_SIZE;
		return 0;
	}
	if (pts.type == PT_ENTRY_TABLE)
		return pt_descend(&pts, arg, descend_fn);
	/* Something else, use the slow path */
	return -EAGAIN;
}
PT_MAKE_LEVELS(__map_single_page, __do_map_single_page);

/*
 * Add a table to the top, increasing the top level as much as necessary to
 * encompass range.
 */
static int increase_top(struct pt_iommu *iommu_table, struct pt_range *range,
			struct pt_iommu_map_args *map)
{
	struct iommu_pages_list free_list = IOMMU_PAGES_LIST_INIT(free_list);
	struct pt_common *common = common_from_iommu(iommu_table);
	uintptr_t top_of_table = READ_ONCE(common->top_of_table);
	uintptr_t new_top_of_table = top_of_table;
	struct pt_table_p *table_mem;
	unsigned int new_level;
	spinlock_t *domain_lock;
	unsigned long flags;
	int ret;

	while (true) {
		struct pt_range top_range =
			_pt_top_range(common, new_top_of_table);
		struct pt_state pts = pt_init_top(&top_range);

		top_range.va = range->va;
		top_range.last_va = range->last_va;

		if (!pt_check_range(&top_range) &&
		    map->leaf_level <= pts.level) {
			new_level = pts.level;
			break;
		}

		pts.level++;
		if (pts.level > PT_MAX_TOP_LEVEL ||
		    pt_table_item_lg2sz(&pts) >= common->max_vasz_lg2) {
			ret = -ERANGE;
			goto err_free;
		}

		table_mem =
			table_alloc_top(common, _pt_top_set(NULL, pts.level),
					map->attrs.gfp, ALLOC_DEFER_COHERENT_FLUSH);
		if (IS_ERR(table_mem)) {
			ret = PTR_ERR(table_mem);
			goto err_free;
		}
		iommu_pages_list_add(&free_list, table_mem);

		/* The new table links to the lower table always at index 0 */
		top_range.va = 0;
		top_range.top_level = pts.level;
		pts.table_lower = pts.table;
		pts.table = table_mem;
		pt_load_single_entry(&pts);
		PT_WARN_ON(pts.index != 0);
		pt_install_table(&pts, virt_to_phys(pts.table_lower),
				 &map->attrs);
		new_top_of_table = _pt_top_set(pts.table, pts.level);
	}

	/*
	 * Avoid double flushing, flush it once after all pt_install_table()
	 */
	if (pt_feature(common, PT_FEAT_DMA_INCOHERENT)) {
		ret = iommu_pages_start_incoherent_list(
			&free_list, iommu_table->iommu_device);
		if (ret)
			goto err_free;
	}

	/*
	 * top_of_table is write locked by the spinlock, but readers can use
	 * READ_ONCE() to get the value. Since we encode both the level and the
	 * pointer in one quanta the lockless reader will always see something
	 * valid. The HW must be updated to the new level under the spinlock
	 * before top_of_table is updated so that concurrent readers don't map
	 * into the new level until it is fully functional. If another thread
	 * already updated it while we were working then throw everything away
	 * and try again.
	 */
	domain_lock = iommu_table->driver_ops->get_top_lock(iommu_table);
	spin_lock_irqsave(domain_lock, flags);
	if (common->top_of_table != top_of_table ||
	    top_of_table == new_top_of_table) {
		spin_unlock_irqrestore(domain_lock, flags);
		ret = -EAGAIN;
		goto err_free;
	}

	/*
	 * We do not issue any flushes for change_top on the expectation that
	 * any walk cache will not become a problem by adding another layer to
	 * the tree. Misses will rewalk from the updated top pointer, hits
	 * continue to be correct. Negative caching is fine too since all the
	 * new IOVA added by the new top is non-present.
	 */
	iommu_table->driver_ops->change_top(
		iommu_table, virt_to_phys(table_mem), new_level);
	WRITE_ONCE(common->top_of_table, new_top_of_table);
	spin_unlock_irqrestore(domain_lock, flags);
	return 0;

err_free:
	if (pt_feature(common, PT_FEAT_DMA_INCOHERENT))
		iommu_pages_stop_incoherent_list(&free_list,
						 iommu_table->iommu_device);
	iommu_put_pages_list(&free_list);
	return ret;
}

static int check_map_range(struct pt_iommu *iommu_table, struct pt_range *range,
			   struct pt_iommu_map_args *map)
{
	struct pt_common *common = common_from_iommu(iommu_table);
	int ret;

	do {
		ret = pt_check_range(range);
		if (!pt_feature(common, PT_FEAT_DYNAMIC_TOP))
			return ret;

		if (!ret && map->leaf_level <= range->top_level)
			break;

		ret = increase_top(iommu_table, range, map);
		if (ret && ret != -EAGAIN)
			return ret;

		/* Reload the new top */
		*range = pt_make_range(common, range->va, range->last_va);
	} while (ret);
	PT_WARN_ON(pt_check_range(range));
	return 0;
}

static int do_map(struct pt_range *range, struct pt_common *common,
		  bool single_page, struct pt_iommu_map_args *map)
{
	/*
	 * The __map_single_page() fast path does not support DMA_INCOHERENT
	 * flushing to keep its .text small.
	 */
	if (single_page && !pt_feature(common, PT_FEAT_DMA_INCOHERENT)) {
		int ret;

		ret = pt_walk_range(range, __map_single_page, map);
		if (ret != -EAGAIN)
			return ret;
		/* EAGAIN falls through to the full path */
	}

	if (map->leaf_level == range->top_level)
		return pt_walk_range(range, __map_range_leaf, map);
	return pt_walk_range(range, __map_range, map);
}

/**
 * map_pages() - Install translation for an IOVA range
 * @domain: Domain to manipulate
 * @iova: IO virtual address to start
 * @paddr: Physical/Output address to start
 * @pgsize: Length of each page
 * @pgcount: Length of the range in pgsize units starting from @iova
 * @prot: A bitmap of IOMMU_READ/WRITE/CACHE/NOEXEC/MMIO
 * @gfp: GFP flags for any memory allocations
 * @mapped: Total bytes successfully mapped
 *
 * The range starting at IOVA will have paddr installed into it. The caller
 * must specify a valid pgsize and pgcount to segment the range into compatible
 * blocks.
 *
 * On error the caller will probably want to invoke unmap on the range from iova
 * up to the amount indicated by @mapped to return the table back to an
 * unchanged state.
 *
 * Context: The caller must hold a write range lock that includes the whole
 * range.
 *
 * Returns: -ERRNO on failure, 0 on success. The number of bytes of VA that were
 * mapped are added to @mapped, @mapped is not zerod first.
 */
int DOMAIN_NS(map_pages)(struct iommu_domain *domain, unsigned long iova,
			 phys_addr_t paddr, size_t pgsize, size_t pgcount,
			 int prot, gfp_t gfp, size_t *mapped)
{
	struct pt_iommu *iommu_table =
		container_of(domain, struct pt_iommu, domain);
	pt_vaddr_t pgsize_bitmap = iommu_table->domain.pgsize_bitmap;
	struct pt_common *common = common_from_iommu(iommu_table);
	struct iommu_iotlb_gather iotlb_gather;
	pt_vaddr_t len = pgsize * pgcount;
	struct pt_iommu_map_args map = {
		.iotlb_gather = &iotlb_gather,
		.oa = paddr,
		.leaf_pgsize_lg2 = vaffs(pgsize),
	};
	bool single_page = false;
	struct pt_range range;
	int ret;

	iommu_iotlb_gather_init(&iotlb_gather);

	if (WARN_ON(!(prot & (IOMMU_READ | IOMMU_WRITE))))
		return -EINVAL;

	/* Check the paddr doesn't exceed what the table can store */
	if ((sizeof(pt_oaddr_t) < sizeof(paddr) &&
	     (pt_vaddr_t)paddr > PT_VADDR_MAX) ||
	    (common->max_oasz_lg2 != PT_VADDR_MAX_LG2 &&
	     oalog2_div(paddr, common->max_oasz_lg2)))
		return -ERANGE;

	ret = pt_iommu_set_prot(common, &map.attrs, prot);
	if (ret)
		return ret;
	map.attrs.gfp = gfp;

	ret = make_range_no_check(common, &range, iova, len);
	if (ret)
		return ret;

	/* Calculate target page size and level for the leaves */
	if (pt_has_system_page_size(common) && pgsize == PAGE_SIZE &&
	    pgcount == 1) {
		PT_WARN_ON(!(pgsize_bitmap & PAGE_SIZE));
		if (log2_mod(iova | paddr, PAGE_SHIFT))
			return -ENXIO;
		map.leaf_pgsize_lg2 = PAGE_SHIFT;
		map.leaf_level = 0;
		single_page = true;
	} else {
		map.leaf_pgsize_lg2 = pt_compute_best_pgsize(
			pgsize_bitmap, range.va, range.last_va, paddr);
		if (!map.leaf_pgsize_lg2)
			return -ENXIO;
		map.leaf_level =
			pt_pgsz_lg2_to_level(common, map.leaf_pgsize_lg2);
	}

	ret = check_map_range(iommu_table, &range, &map);
	if (ret)
		return ret;

	PT_WARN_ON(map.leaf_level > range.top_level);

	ret = do_map(&range, common, single_page, &map);

	/*
	 * Table levels were freed and replaced with large items, flush any walk
	 * cache that may refer to the freed levels.
	 */
	if (!iommu_pages_list_empty(&iotlb_gather.freelist))
		iommu_iotlb_sync(&iommu_table->domain, &iotlb_gather);

	/* Bytes successfully mapped */
	PT_WARN_ON(!ret && map.oa - paddr != len);
	*mapped += map.oa - paddr;
	return ret;
}
EXPORT_SYMBOL_NS_GPL(DOMAIN_NS(map_pages), "GENERIC_PT_IOMMU");

struct pt_unmap_args {
	struct iommu_pages_list free_list;
	pt_vaddr_t unmapped;
};

static __maybe_unused int __unmap_range(struct pt_range *range, void *arg,
					unsigned int level,
					struct pt_table_p *table)
{
	struct pt_state pts = pt_init(range, level, table);
	unsigned int flush_start_index = UINT_MAX;
	unsigned int flush_end_index = UINT_MAX;
	struct pt_unmap_args *unmap = arg;
	unsigned int num_oas = 0;
	unsigned int start_index;
	int ret = 0;

	_pt_iter_first(&pts);
	start_index = pts.index;
	pts.type = pt_load_entry_raw(&pts);
	/*
	 * A starting index is in the middle of a contiguous entry
	 *
	 * The IOMMU API does not require drivers to support unmapping parts of
	 * large pages. Long ago VFIO would try to split maps but the current
	 * version never does.
	 *
	 * Instead when unmap reaches a partial unmap of the start of a large
	 * IOPTE it should remove the entire IOPTE and return that size to the
	 * caller.
	 */
	if (pts.type == PT_ENTRY_OA) {
		if (log2_mod(range->va, pt_entry_oa_lg2sz(&pts)))
			return -EINVAL;
		/* Micro optimization */
		goto start_oa;
	}

	do {
		if (pts.type != PT_ENTRY_OA) {
			bool fully_covered;

			if (pts.type != PT_ENTRY_TABLE) {
				ret = -EINVAL;
				break;
			}

			if (pts.index != start_index)
				pt_index_to_va(&pts);
			pts.table_lower = pt_table_ptr(&pts);

			fully_covered = pt_entry_fully_covered(
				&pts, pt_table_item_lg2sz(&pts));

			ret = pt_descend(&pts, arg, __unmap_range);
			if (ret)
				break;

			/*
			 * If the unmapping range fully covers the table then we
			 * can free it as well. The clear is delayed until we
			 * succeed in clearing the lower table levels.
			 */
			if (fully_covered) {
				iommu_pages_list_add(&unmap->free_list,
						     pts.table_lower);
				pt_clear_entries(&pts, ilog2(1));
				if (pts.index < flush_start_index)
					flush_start_index = pts.index;
				flush_end_index = pts.index + 1;
			}
			pts.index++;
		} else {
			unsigned int num_contig_lg2;
start_oa:
			/*
			 * If the caller requested an last that falls within a
			 * single entry then the entire entry is unmapped and
			 * the length returned will be larger than requested.
			 */
			num_contig_lg2 = pt_entry_num_contig_lg2(&pts);
			pt_clear_entries(&pts, num_contig_lg2);
			num_oas += log2_to_int(num_contig_lg2);
			if (pts.index < flush_start_index)
				flush_start_index = pts.index;
			pts.index += log2_to_int(num_contig_lg2);
			flush_end_index = pts.index;
		}
		if (pts.index >= pts.end_index)
			break;
		pts.type = pt_load_entry_raw(&pts);
	} while (true);

	unmap->unmapped += log2_mul(num_oas, pt_table_item_lg2sz(&pts));
	if (flush_start_index != flush_end_index)
		flush_writes_range(&pts, flush_start_index, flush_end_index);

	return ret;
}

/**
 * unmap_pages() - Make a range of IOVA empty/not present
 * @domain: Domain to manipulate
 * @iova: IO virtual address to start
 * @pgsize: Length of each page
 * @pgcount: Length of the range in pgsize units starting from @iova
 * @iotlb_gather: Gather struct that must be flushed on return
 *
 * unmap_pages() will remove a translation created by map_pages(). It cannot
 * subdivide a mapping created by map_pages(), so it should be called with IOVA
 * ranges that match those passed to map_pages(). The IOVA range can aggregate
 * contiguous map_pages() calls so long as no individual range is split.
 *
 * Context: The caller must hold a write range lock that includes
 * the whole range.
 *
 * Returns: Number of bytes of VA unmapped. iova + res will be the point
 * unmapping stopped.
 */
size_t DOMAIN_NS(unmap_pages)(struct iommu_domain *domain, unsigned long iova,
			      size_t pgsize, size_t pgcount,
			      struct iommu_iotlb_gather *iotlb_gather)
{
	struct pt_iommu *iommu_table =
		container_of(domain, struct pt_iommu, domain);
	struct pt_unmap_args unmap = { .free_list = IOMMU_PAGES_LIST_INIT(
					       unmap.free_list) };
	pt_vaddr_t len = pgsize * pgcount;
	struct pt_range range;
	int ret;

	ret = make_range(common_from_iommu(iommu_table), &range, iova, len);
	if (ret)
		return 0;

	pt_walk_range(&range, __unmap_range, &unmap);

	gather_range_pages(iotlb_gather, iommu_table, iova, len,
			   &unmap.free_list);

	return unmap.unmapped;
}
EXPORT_SYMBOL_NS_GPL(DOMAIN_NS(unmap_pages), "GENERIC_PT_IOMMU");

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
	if (pt_feature(common, PT_FEAT_DMA_INCOHERENT))
		iommu_pages_stop_incoherent_list(&collect.free_list,
						 iommu_table->iommu_device);
	iommu_put_pages_list(&collect.free_list);
}

static const struct pt_iommu_ops NS(ops) = {
#if IS_ENABLED(CONFIG_IOMMUFD_DRIVER) && defined(pt_entry_is_write_dirty) && \
	IS_ENABLED(CONFIG_IOMMUFD_TEST) && defined(pt_entry_make_write_dirty)
	.set_dirty = NS(set_dirty),
#endif
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

	/*
	 * Check if the top level of the page table is too small to hold the
	 * specified maxvasz.
	 */
	if (!pt_feature(common, PT_FEAT_DYNAMIC_TOP) &&
	    top_range.top_level != PT_MAX_TOP_LEVEL) {
		struct pt_state pts = { .range = &top_range,
					.level = top_range.top_level };

		if (common->max_vasz_lg2 >
		    pt_num_items_lg2(&pts) + pt_table_item_lg2sz(&pts))
			return -EOPNOTSUPP;
	}

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
	iommu_table->iommu_device = cfg.iommu_device;
	iommu_table->driver_ops = cfg.driver_ops;
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

	if (pt_feature(common, PT_FEAT_DYNAMIC_TOP) &&
	    WARN_ON(!iommu_table->driver_ops ||
		    !iommu_table->driver_ops->change_top ||
		    !iommu_table->driver_ops->get_top_lock))
		return -EINVAL;

	if (pt_feature(common, PT_FEAT_SIGN_EXTEND) &&
	    (pt_feature(common, PT_FEAT_FULL_VA) ||
	     pt_feature(common, PT_FEAT_DYNAMIC_TOP)))
		return -EINVAL;

	if (pt_feature(common, PT_FEAT_DMA_INCOHERENT) &&
	    WARN_ON(!iommu_table->iommu_device))
		return -EINVAL;

	ret = pt_iommu_init_domain(iommu_table, &iommu_table->domain);
	if (ret)
		return ret;

	table_mem = table_alloc_top(common, common->top_of_table, gfp,
				    ALLOC_NORMAL);
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
/* For iommu_dirty_bitmap_record() */
MODULE_IMPORT_NS("IOMMUFD");

#endif  /* __GENERIC_PT_IOMMU_PT_H */
