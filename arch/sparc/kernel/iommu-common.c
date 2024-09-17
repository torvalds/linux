// SPDX-License-Identifier: GPL-2.0
/*
 * IOMMU mmap management and range allocation functions.
 * Based almost entirely upon the powerpc iommu allocator.
 */

#include <linux/export.h>
#include <linux/bitmap.h>
#include <linux/bug.h>
#include <linux/iommu-helper.h>
#include <linux/dma-mapping.h>
#include <linux/hash.h>
#include <asm/iommu-common.h>

static unsigned long iommu_large_alloc = 15;

static	DEFINE_PER_CPU(unsigned int, iommu_hash_common);

static inline bool need_flush(struct iommu_map_table *iommu)
{
	return ((iommu->flags & IOMMU_NEED_FLUSH) != 0);
}

static inline void set_flush(struct iommu_map_table *iommu)
{
	iommu->flags |= IOMMU_NEED_FLUSH;
}

static inline void clear_flush(struct iommu_map_table *iommu)
{
	iommu->flags &= ~IOMMU_NEED_FLUSH;
}

static void setup_iommu_pool_hash(void)
{
	unsigned int i;
	static bool do_once;

	if (do_once)
		return;
	do_once = true;
	for_each_possible_cpu(i)
		per_cpu(iommu_hash_common, i) = hash_32(i, IOMMU_POOL_HASHBITS);
}

/*
 * Initialize iommu_pool entries for the iommu_map_table. `num_entries'
 * is the number of table entries. If `large_pool' is set to true,
 * the top 1/4 of the table will be set aside for pool allocations
 * of more than iommu_large_alloc pages.
 */
void iommu_tbl_pool_init(struct iommu_map_table *iommu,
			 unsigned long num_entries,
			 u32 table_shift,
			 void (*lazy_flush)(struct iommu_map_table *),
			 bool large_pool, u32 npools,
			 bool skip_span_boundary_check)
{
	unsigned int start, i;
	struct iommu_pool *p = &(iommu->large_pool);

	setup_iommu_pool_hash();
	if (npools == 0)
		iommu->nr_pools = IOMMU_NR_POOLS;
	else
		iommu->nr_pools = npools;
	BUG_ON(npools > IOMMU_NR_POOLS);

	iommu->table_shift = table_shift;
	iommu->lazy_flush = lazy_flush;
	start = 0;
	if (skip_span_boundary_check)
		iommu->flags |= IOMMU_NO_SPAN_BOUND;
	if (large_pool)
		iommu->flags |= IOMMU_HAS_LARGE_POOL;

	if (!large_pool)
		iommu->poolsize = num_entries/iommu->nr_pools;
	else
		iommu->poolsize = (num_entries * 3 / 4)/iommu->nr_pools;
	for (i = 0; i < iommu->nr_pools; i++) {
		spin_lock_init(&(iommu->pools[i].lock));
		iommu->pools[i].start = start;
		iommu->pools[i].hint = start;
		start += iommu->poolsize; /* start for next pool */
		iommu->pools[i].end = start - 1;
	}
	if (!large_pool)
		return;
	/* initialize large_pool */
	spin_lock_init(&(p->lock));
	p->start = start;
	p->hint = p->start;
	p->end = num_entries;
}

unsigned long iommu_tbl_range_alloc(struct device *dev,
				struct iommu_map_table *iommu,
				unsigned long npages,
				unsigned long *handle,
				unsigned long mask,
				unsigned int align_order)
{
	unsigned int pool_hash = __this_cpu_read(iommu_hash_common);
	unsigned long n, end, start, limit, boundary_size;
	struct iommu_pool *pool;
	int pass = 0;
	unsigned int pool_nr;
	unsigned int npools = iommu->nr_pools;
	unsigned long flags;
	bool large_pool = ((iommu->flags & IOMMU_HAS_LARGE_POOL) != 0);
	bool largealloc = (large_pool && npages > iommu_large_alloc);
	unsigned long shift;
	unsigned long align_mask = 0;

	if (align_order > 0)
		align_mask = ~0ul >> (BITS_PER_LONG - align_order);

	/* Sanity check */
	if (unlikely(npages == 0)) {
		WARN_ON_ONCE(1);
		return IOMMU_ERROR_CODE;
	}

	if (largealloc) {
		pool = &(iommu->large_pool);
		pool_nr = 0; /* to keep compiler happy */
	} else {
		/* pick out pool_nr */
		pool_nr =  pool_hash & (npools - 1);
		pool = &(iommu->pools[pool_nr]);
	}
	spin_lock_irqsave(&pool->lock, flags);

 again:
	if (pass == 0 && handle && *handle &&
	    (*handle >= pool->start) && (*handle < pool->end))
		start = *handle;
	else
		start = pool->hint;

	limit = pool->end;

	/* The case below can happen if we have a small segment appended
	 * to a large, or when the previous alloc was at the very end of
	 * the available space. If so, go back to the beginning. If a
	 * flush is needed, it will get done based on the return value
	 * from iommu_area_alloc() below.
	 */
	if (start >= limit)
		start = pool->start;
	shift = iommu->table_map_base >> iommu->table_shift;
	if (limit + shift > mask) {
		limit = mask - shift + 1;
		/* If we're constrained on address range, first try
		 * at the masked hint to avoid O(n) search complexity,
		 * but on second pass, start at 0 in pool 0.
		 */
		if ((start & mask) >= limit || pass > 0) {
			spin_unlock(&(pool->lock));
			pool = &(iommu->pools[0]);
			spin_lock(&(pool->lock));
			start = pool->start;
		} else {
			start &= mask;
		}
	}

	/*
	 * if the skip_span_boundary_check had been set during init, we set
	 * things up so that iommu_is_span_boundary() merely checks if the
	 * (index + npages) < num_tsb_entries
	 */
	if ((iommu->flags & IOMMU_NO_SPAN_BOUND) != 0) {
		shift = 0;
		boundary_size = iommu->poolsize * iommu->nr_pools;
	} else {
		boundary_size = dma_get_seg_boundary_nr_pages(dev,
					iommu->table_shift);
	}
	n = iommu_area_alloc(iommu->map, limit, start, npages, shift,
			     boundary_size, align_mask);
	if (n == -1) {
		if (likely(pass == 0)) {
			/* First failure, rescan from the beginning.  */
			pool->hint = pool->start;
			set_flush(iommu);
			pass++;
			goto again;
		} else if (!largealloc && pass <= iommu->nr_pools) {
			spin_unlock(&(pool->lock));
			pool_nr = (pool_nr + 1) & (iommu->nr_pools - 1);
			pool = &(iommu->pools[pool_nr]);
			spin_lock(&(pool->lock));
			pool->hint = pool->start;
			set_flush(iommu);
			pass++;
			goto again;
		} else {
			/* give up */
			n = IOMMU_ERROR_CODE;
			goto bail;
		}
	}
	if (iommu->lazy_flush &&
	    (n < pool->hint || need_flush(iommu))) {
		clear_flush(iommu);
		iommu->lazy_flush(iommu);
	}

	end = n + npages;
	pool->hint = end;

	/* Update handle for SG allocations */
	if (handle)
		*handle = end;
bail:
	spin_unlock_irqrestore(&(pool->lock), flags);

	return n;
}

static struct iommu_pool *get_pool(struct iommu_map_table *tbl,
				   unsigned long entry)
{
	struct iommu_pool *p;
	unsigned long largepool_start = tbl->large_pool.start;
	bool large_pool = ((tbl->flags & IOMMU_HAS_LARGE_POOL) != 0);

	/* The large pool is the last pool at the top of the table */
	if (large_pool && entry >= largepool_start) {
		p = &tbl->large_pool;
	} else {
		unsigned int pool_nr = entry / tbl->poolsize;

		BUG_ON(pool_nr >= tbl->nr_pools);
		p = &tbl->pools[pool_nr];
	}
	return p;
}

/* Caller supplies the index of the entry into the iommu map table
 * itself when the mapping from dma_addr to the entry is not the
 * default addr->entry mapping below.
 */
void iommu_tbl_range_free(struct iommu_map_table *iommu, u64 dma_addr,
			  unsigned long npages, unsigned long entry)
{
	struct iommu_pool *pool;
	unsigned long flags;
	unsigned long shift = iommu->table_shift;

	if (entry == IOMMU_ERROR_CODE) /* use default addr->entry mapping */
		entry = (dma_addr - iommu->table_map_base) >> shift;
	pool = get_pool(iommu, entry);

	spin_lock_irqsave(&(pool->lock), flags);
	bitmap_clear(iommu->map, entry, npages);
	spin_unlock_irqrestore(&(pool->lock), flags);
}
