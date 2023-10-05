// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * 
 * Rewrite, cleanup, new allocation schemes, virtual merging: 
 * Copyright (C) 2004 Olof Johansson, IBM Corporation
 *               and  Ben. Herrenschmidt, IBM Corporation
 *
 * Dynamic DMA mapping support, bus-independent parts.
 */


#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/bitmap.h>
#include <linux/iommu-helper.h>
#include <linux/crash_dump.h>
#include <linux/hash.h>
#include <linux/fault-inject.h>
#include <linux/pci.h>
#include <linux/iommu.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <asm/io.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/kdump.h>
#include <asm/fadump.h>
#include <asm/vio.h>
#include <asm/tce.h>
#include <asm/mmu_context.h>
#include <asm/ppc-pci.h>

#define DBG(...)

#ifdef CONFIG_IOMMU_DEBUGFS
static int iommu_debugfs_weight_get(void *data, u64 *val)
{
	struct iommu_table *tbl = data;
	*val = bitmap_weight(tbl->it_map, tbl->it_size);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(iommu_debugfs_fops_weight, iommu_debugfs_weight_get, NULL, "%llu\n");

static void iommu_debugfs_add(struct iommu_table *tbl)
{
	char name[10];
	struct dentry *liobn_entry;

	sprintf(name, "%08lx", tbl->it_index);
	liobn_entry = debugfs_create_dir(name, iommu_debugfs_dir);

	debugfs_create_file_unsafe("weight", 0400, liobn_entry, tbl, &iommu_debugfs_fops_weight);
	debugfs_create_ulong("it_size", 0400, liobn_entry, &tbl->it_size);
	debugfs_create_ulong("it_page_shift", 0400, liobn_entry, &tbl->it_page_shift);
	debugfs_create_ulong("it_reserved_start", 0400, liobn_entry, &tbl->it_reserved_start);
	debugfs_create_ulong("it_reserved_end", 0400, liobn_entry, &tbl->it_reserved_end);
	debugfs_create_ulong("it_indirect_levels", 0400, liobn_entry, &tbl->it_indirect_levels);
	debugfs_create_ulong("it_level_size", 0400, liobn_entry, &tbl->it_level_size);
}

static void iommu_debugfs_del(struct iommu_table *tbl)
{
	char name[10];

	sprintf(name, "%08lx", tbl->it_index);
	debugfs_lookup_and_remove(name, iommu_debugfs_dir);
}
#else
static void iommu_debugfs_add(struct iommu_table *tbl){}
static void iommu_debugfs_del(struct iommu_table *tbl){}
#endif

static int novmerge;

static void __iommu_free(struct iommu_table *, dma_addr_t, unsigned int);

static int __init setup_iommu(char *str)
{
	if (!strcmp(str, "novmerge"))
		novmerge = 1;
	else if (!strcmp(str, "vmerge"))
		novmerge = 0;
	return 1;
}

__setup("iommu=", setup_iommu);

static DEFINE_PER_CPU(unsigned int, iommu_pool_hash);

/*
 * We precalculate the hash to avoid doing it on every allocation.
 *
 * The hash is important to spread CPUs across all the pools. For example,
 * on a POWER7 with 4 way SMT we want interrupts on the primary threads and
 * with 4 pools all primary threads would map to the same pool.
 */
static int __init setup_iommu_pool_hash(void)
{
	unsigned int i;

	for_each_possible_cpu(i)
		per_cpu(iommu_pool_hash, i) = hash_32(i, IOMMU_POOL_HASHBITS);

	return 0;
}
subsys_initcall(setup_iommu_pool_hash);

#ifdef CONFIG_FAIL_IOMMU

static DECLARE_FAULT_ATTR(fail_iommu);

static int __init setup_fail_iommu(char *str)
{
	return setup_fault_attr(&fail_iommu, str);
}
__setup("fail_iommu=", setup_fail_iommu);

static bool should_fail_iommu(struct device *dev)
{
	return dev->archdata.fail_iommu && should_fail(&fail_iommu, 1);
}

static int __init fail_iommu_debugfs(void)
{
	struct dentry *dir = fault_create_debugfs_attr("fail_iommu",
						       NULL, &fail_iommu);

	return PTR_ERR_OR_ZERO(dir);
}
late_initcall(fail_iommu_debugfs);

static ssize_t fail_iommu_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dev->archdata.fail_iommu);
}

static ssize_t fail_iommu_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int i;

	if (count > 0 && sscanf(buf, "%d", &i) > 0)
		dev->archdata.fail_iommu = (i == 0) ? 0 : 1;

	return count;
}

static DEVICE_ATTR_RW(fail_iommu);

static int fail_iommu_bus_notify(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct device *dev = data;

	if (action == BUS_NOTIFY_ADD_DEVICE) {
		if (device_create_file(dev, &dev_attr_fail_iommu))
			pr_warn("Unable to create IOMMU fault injection sysfs "
				"entries\n");
	} else if (action == BUS_NOTIFY_DEL_DEVICE) {
		device_remove_file(dev, &dev_attr_fail_iommu);
	}

	return 0;
}

/*
 * PCI and VIO buses need separate notifier_block structs, since they're linked
 * list nodes.  Sharing a notifier_block would mean that any notifiers later
 * registered for PCI buses would also get called by VIO buses and vice versa.
 */
static struct notifier_block fail_iommu_pci_bus_notifier = {
	.notifier_call = fail_iommu_bus_notify
};

#ifdef CONFIG_IBMVIO
static struct notifier_block fail_iommu_vio_bus_notifier = {
	.notifier_call = fail_iommu_bus_notify
};
#endif

static int __init fail_iommu_setup(void)
{
#ifdef CONFIG_PCI
	bus_register_notifier(&pci_bus_type, &fail_iommu_pci_bus_notifier);
#endif
#ifdef CONFIG_IBMVIO
	bus_register_notifier(&vio_bus_type, &fail_iommu_vio_bus_notifier);
#endif

	return 0;
}
/*
 * Must execute after PCI and VIO subsystem have initialised but before
 * devices are probed.
 */
arch_initcall(fail_iommu_setup);
#else
static inline bool should_fail_iommu(struct device *dev)
{
	return false;
}
#endif

static unsigned long iommu_range_alloc(struct device *dev,
				       struct iommu_table *tbl,
                                       unsigned long npages,
                                       unsigned long *handle,
                                       unsigned long mask,
                                       unsigned int align_order)
{ 
	unsigned long n, end, start;
	unsigned long limit;
	int largealloc = npages > 15;
	int pass = 0;
	unsigned long align_mask;
	unsigned long flags;
	unsigned int pool_nr;
	struct iommu_pool *pool;

	align_mask = (1ull << align_order) - 1;

	/* This allocator was derived from x86_64's bit string search */

	/* Sanity check */
	if (unlikely(npages == 0)) {
		if (printk_ratelimit())
			WARN_ON(1);
		return DMA_MAPPING_ERROR;
	}

	if (should_fail_iommu(dev))
		return DMA_MAPPING_ERROR;

	/*
	 * We don't need to disable preemption here because any CPU can
	 * safely use any IOMMU pool.
	 */
	pool_nr = raw_cpu_read(iommu_pool_hash) & (tbl->nr_pools - 1);

	if (largealloc)
		pool = &(tbl->large_pool);
	else
		pool = &(tbl->pools[pool_nr]);

	spin_lock_irqsave(&(pool->lock), flags);

again:
	if ((pass == 0) && handle && *handle &&
	    (*handle >= pool->start) && (*handle < pool->end))
		start = *handle;
	else
		start = pool->hint;

	limit = pool->end;

	/* The case below can happen if we have a small segment appended
	 * to a large, or when the previous alloc was at the very end of
	 * the available space. If so, go back to the initial start.
	 */
	if (start >= limit)
		start = pool->start;

	if (limit + tbl->it_offset > mask) {
		limit = mask - tbl->it_offset + 1;
		/* If we're constrained on address range, first try
		 * at the masked hint to avoid O(n) search complexity,
		 * but on second pass, start at 0 in pool 0.
		 */
		if ((start & mask) >= limit || pass > 0) {
			spin_unlock(&(pool->lock));
			pool = &(tbl->pools[0]);
			spin_lock(&(pool->lock));
			start = pool->start;
		} else {
			start &= mask;
		}
	}

	n = iommu_area_alloc(tbl->it_map, limit, start, npages, tbl->it_offset,
			dma_get_seg_boundary_nr_pages(dev, tbl->it_page_shift),
			align_mask);
	if (n == -1) {
		if (likely(pass == 0)) {
			/* First try the pool from the start */
			pool->hint = pool->start;
			pass++;
			goto again;

		} else if (pass <= tbl->nr_pools) {
			/* Now try scanning all the other pools */
			spin_unlock(&(pool->lock));
			pool_nr = (pool_nr + 1) & (tbl->nr_pools - 1);
			pool = &tbl->pools[pool_nr];
			spin_lock(&(pool->lock));
			pool->hint = pool->start;
			pass++;
			goto again;

		} else if (pass == tbl->nr_pools + 1) {
			/* Last resort: try largepool */
			spin_unlock(&pool->lock);
			pool = &tbl->large_pool;
			spin_lock(&pool->lock);
			pool->hint = pool->start;
			pass++;
			goto again;

		} else {
			/* Give up */
			spin_unlock_irqrestore(&(pool->lock), flags);
			return DMA_MAPPING_ERROR;
		}
	}

	end = n + npages;

	/* Bump the hint to a new block for small allocs. */
	if (largealloc) {
		/* Don't bump to new block to avoid fragmentation */
		pool->hint = end;
	} else {
		/* Overflow will be taken care of at the next allocation */
		pool->hint = (end + tbl->it_blocksize - 1) &
		                ~(tbl->it_blocksize - 1);
	}

	/* Update handle for SG allocations */
	if (handle)
		*handle = end;

	spin_unlock_irqrestore(&(pool->lock), flags);

	return n;
}

static dma_addr_t iommu_alloc(struct device *dev, struct iommu_table *tbl,
			      void *page, unsigned int npages,
			      enum dma_data_direction direction,
			      unsigned long mask, unsigned int align_order,
			      unsigned long attrs)
{
	unsigned long entry;
	dma_addr_t ret = DMA_MAPPING_ERROR;
	int build_fail;

	entry = iommu_range_alloc(dev, tbl, npages, NULL, mask, align_order);

	if (unlikely(entry == DMA_MAPPING_ERROR))
		return DMA_MAPPING_ERROR;

	entry += tbl->it_offset;	/* Offset into real TCE table */
	ret = entry << tbl->it_page_shift;	/* Set the return dma address */

	/* Put the TCEs in the HW table */
	build_fail = tbl->it_ops->set(tbl, entry, npages,
				      (unsigned long)page &
				      IOMMU_PAGE_MASK(tbl), direction, attrs);

	/* tbl->it_ops->set() only returns non-zero for transient errors.
	 * Clean up the table bitmap in this case and return
	 * DMA_MAPPING_ERROR. For all other errors the functionality is
	 * not altered.
	 */
	if (unlikely(build_fail)) {
		__iommu_free(tbl, ret, npages);
		return DMA_MAPPING_ERROR;
	}

	/* Flush/invalidate TLB caches if necessary */
	if (tbl->it_ops->flush)
		tbl->it_ops->flush(tbl);

	/* Make sure updates are seen by hardware */
	mb();

	return ret;
}

static bool iommu_free_check(struct iommu_table *tbl, dma_addr_t dma_addr,
			     unsigned int npages)
{
	unsigned long entry, free_entry;

	entry = dma_addr >> tbl->it_page_shift;
	free_entry = entry - tbl->it_offset;

	if (((free_entry + npages) > tbl->it_size) ||
	    (entry < tbl->it_offset)) {
		if (printk_ratelimit()) {
			printk(KERN_INFO "iommu_free: invalid entry\n");
			printk(KERN_INFO "\tentry     = 0x%lx\n", entry); 
			printk(KERN_INFO "\tdma_addr  = 0x%llx\n", (u64)dma_addr);
			printk(KERN_INFO "\tTable     = 0x%llx\n", (u64)tbl);
			printk(KERN_INFO "\tbus#      = 0x%llx\n", (u64)tbl->it_busno);
			printk(KERN_INFO "\tsize      = 0x%llx\n", (u64)tbl->it_size);
			printk(KERN_INFO "\tstartOff  = 0x%llx\n", (u64)tbl->it_offset);
			printk(KERN_INFO "\tindex     = 0x%llx\n", (u64)tbl->it_index);
			WARN_ON(1);
		}

		return false;
	}

	return true;
}

static struct iommu_pool *get_pool(struct iommu_table *tbl,
				   unsigned long entry)
{
	struct iommu_pool *p;
	unsigned long largepool_start = tbl->large_pool.start;

	/* The large pool is the last pool at the top of the table */
	if (entry >= largepool_start) {
		p = &tbl->large_pool;
	} else {
		unsigned int pool_nr = entry / tbl->poolsize;

		BUG_ON(pool_nr > tbl->nr_pools);
		p = &tbl->pools[pool_nr];
	}

	return p;
}

static void __iommu_free(struct iommu_table *tbl, dma_addr_t dma_addr,
			 unsigned int npages)
{
	unsigned long entry, free_entry;
	unsigned long flags;
	struct iommu_pool *pool;

	entry = dma_addr >> tbl->it_page_shift;
	free_entry = entry - tbl->it_offset;

	pool = get_pool(tbl, free_entry);

	if (!iommu_free_check(tbl, dma_addr, npages))
		return;

	tbl->it_ops->clear(tbl, entry, npages);

	spin_lock_irqsave(&(pool->lock), flags);
	bitmap_clear(tbl->it_map, free_entry, npages);
	spin_unlock_irqrestore(&(pool->lock), flags);
}

static void iommu_free(struct iommu_table *tbl, dma_addr_t dma_addr,
		unsigned int npages)
{
	__iommu_free(tbl, dma_addr, npages);

	/* Make sure TLB cache is flushed if the HW needs it. We do
	 * not do an mb() here on purpose, it is not needed on any of
	 * the current platforms.
	 */
	if (tbl->it_ops->flush)
		tbl->it_ops->flush(tbl);
}

int ppc_iommu_map_sg(struct device *dev, struct iommu_table *tbl,
		     struct scatterlist *sglist, int nelems,
		     unsigned long mask, enum dma_data_direction direction,
		     unsigned long attrs)
{
	dma_addr_t dma_next = 0, dma_addr;
	struct scatterlist *s, *outs, *segstart;
	int outcount, incount, i, build_fail = 0;
	unsigned int align;
	unsigned long handle;
	unsigned int max_seg_size;

	BUG_ON(direction == DMA_NONE);

	if ((nelems == 0) || !tbl)
		return -EINVAL;

	outs = s = segstart = &sglist[0];
	outcount = 1;
	incount = nelems;
	handle = 0;

	/* Init first segment length for backout at failure */
	outs->dma_length = 0;

	DBG("sg mapping %d elements:\n", nelems);

	max_seg_size = dma_get_max_seg_size(dev);
	for_each_sg(sglist, s, nelems, i) {
		unsigned long vaddr, npages, entry, slen;

		slen = s->length;
		/* Sanity check */
		if (slen == 0) {
			dma_next = 0;
			continue;
		}
		/* Allocate iommu entries for that segment */
		vaddr = (unsigned long) sg_virt(s);
		npages = iommu_num_pages(vaddr, slen, IOMMU_PAGE_SIZE(tbl));
		align = 0;
		if (tbl->it_page_shift < PAGE_SHIFT && slen >= PAGE_SIZE &&
		    (vaddr & ~PAGE_MASK) == 0)
			align = PAGE_SHIFT - tbl->it_page_shift;
		entry = iommu_range_alloc(dev, tbl, npages, &handle,
					  mask >> tbl->it_page_shift, align);

		DBG("  - vaddr: %lx, size: %lx\n", vaddr, slen);

		/* Handle failure */
		if (unlikely(entry == DMA_MAPPING_ERROR)) {
			if (!(attrs & DMA_ATTR_NO_WARN) &&
			    printk_ratelimit())
				dev_info(dev, "iommu_alloc failed, tbl %p "
					 "vaddr %lx npages %lu\n", tbl, vaddr,
					 npages);
			goto failure;
		}

		/* Convert entry to a dma_addr_t */
		entry += tbl->it_offset;
		dma_addr = entry << tbl->it_page_shift;
		dma_addr |= (vaddr & ~IOMMU_PAGE_MASK(tbl));

		DBG("  - %lu pages, entry: %lx, dma_addr: %lx\n",
			    npages, entry, dma_addr);

		/* Insert into HW table */
		build_fail = tbl->it_ops->set(tbl, entry, npages,
					      vaddr & IOMMU_PAGE_MASK(tbl),
					      direction, attrs);
		if(unlikely(build_fail))
			goto failure;

		/* If we are in an open segment, try merging */
		if (segstart != s) {
			DBG("  - trying merge...\n");
			/* We cannot merge if:
			 * - allocated dma_addr isn't contiguous to previous allocation
			 */
			if (novmerge || (dma_addr != dma_next) ||
			    (outs->dma_length + s->length > max_seg_size)) {
				/* Can't merge: create a new segment */
				segstart = s;
				outcount++;
				outs = sg_next(outs);
				DBG("    can't merge, new segment.\n");
			} else {
				outs->dma_length += s->length;
				DBG("    merged, new len: %ux\n", outs->dma_length);
			}
		}

		if (segstart == s) {
			/* This is a new segment, fill entries */
			DBG("  - filling new segment.\n");
			outs->dma_address = dma_addr;
			outs->dma_length = slen;
		}

		/* Calculate next page pointer for contiguous check */
		dma_next = dma_addr + slen;

		DBG("  - dma next is: %lx\n", dma_next);
	}

	/* Flush/invalidate TLB caches if necessary */
	if (tbl->it_ops->flush)
		tbl->it_ops->flush(tbl);

	DBG("mapped %d elements:\n", outcount);

	/* For the sake of ppc_iommu_unmap_sg, we clear out the length in the
	 * next entry of the sglist if we didn't fill the list completely
	 */
	if (outcount < incount) {
		outs = sg_next(outs);
		outs->dma_length = 0;
	}

	/* Make sure updates are seen by hardware */
	mb();

	return outcount;

 failure:
	for_each_sg(sglist, s, nelems, i) {
		if (s->dma_length != 0) {
			unsigned long vaddr, npages;

			vaddr = s->dma_address & IOMMU_PAGE_MASK(tbl);
			npages = iommu_num_pages(s->dma_address, s->dma_length,
						 IOMMU_PAGE_SIZE(tbl));
			__iommu_free(tbl, vaddr, npages);
			s->dma_length = 0;
		}
		if (s == outs)
			break;
	}
	return -EIO;
}


void ppc_iommu_unmap_sg(struct iommu_table *tbl, struct scatterlist *sglist,
			int nelems, enum dma_data_direction direction,
			unsigned long attrs)
{
	struct scatterlist *sg;

	BUG_ON(direction == DMA_NONE);

	if (!tbl)
		return;

	sg = sglist;
	while (nelems--) {
		unsigned int npages;
		dma_addr_t dma_handle = sg->dma_address;

		if (sg->dma_length == 0)
			break;
		npages = iommu_num_pages(dma_handle, sg->dma_length,
					 IOMMU_PAGE_SIZE(tbl));
		__iommu_free(tbl, dma_handle, npages);
		sg = sg_next(sg);
	}

	/* Flush/invalidate TLBs if necessary. As for iommu_free(), we
	 * do not do an mb() here, the affected platforms do not need it
	 * when freeing.
	 */
	if (tbl->it_ops->flush)
		tbl->it_ops->flush(tbl);
}

static void iommu_table_clear(struct iommu_table *tbl)
{
	/*
	 * In case of firmware assisted dump system goes through clean
	 * reboot process at the time of system crash. Hence it's safe to
	 * clear the TCE entries if firmware assisted dump is active.
	 */
	if (!is_kdump_kernel() || is_fadump_active()) {
		/* Clear the table in case firmware left allocations in it */
		tbl->it_ops->clear(tbl, tbl->it_offset, tbl->it_size);
		return;
	}

#ifdef CONFIG_CRASH_DUMP
	if (tbl->it_ops->get) {
		unsigned long index, tceval, tcecount = 0;

		/* Reserve the existing mappings left by the first kernel. */
		for (index = 0; index < tbl->it_size; index++) {
			tceval = tbl->it_ops->get(tbl, index + tbl->it_offset);
			/*
			 * Freed TCE entry contains 0x7fffffffffffffff on JS20
			 */
			if (tceval && (tceval != 0x7fffffffffffffffUL)) {
				__set_bit(index, tbl->it_map);
				tcecount++;
			}
		}

		if ((tbl->it_size - tcecount) < KDUMP_MIN_TCE_ENTRIES) {
			printk(KERN_WARNING "TCE table is full; freeing ");
			printk(KERN_WARNING "%d entries for the kdump boot\n",
				KDUMP_MIN_TCE_ENTRIES);
			for (index = tbl->it_size - KDUMP_MIN_TCE_ENTRIES;
				index < tbl->it_size; index++)
				__clear_bit(index, tbl->it_map);
		}
	}
#endif
}

static void iommu_table_reserve_pages(struct iommu_table *tbl,
		unsigned long res_start, unsigned long res_end)
{
	int i;

	WARN_ON_ONCE(res_end < res_start);
	/*
	 * Reserve page 0 so it will not be used for any mappings.
	 * This avoids buggy drivers that consider page 0 to be invalid
	 * to crash the machine or even lose data.
	 */
	if (tbl->it_offset == 0)
		set_bit(0, tbl->it_map);

	if (res_start < tbl->it_offset)
		res_start = tbl->it_offset;

	if (res_end > (tbl->it_offset + tbl->it_size))
		res_end = tbl->it_offset + tbl->it_size;

	/* Check if res_start..res_end is a valid range in the table */
	if (res_start >= res_end) {
		tbl->it_reserved_start = tbl->it_offset;
		tbl->it_reserved_end = tbl->it_offset;
		return;
	}

	tbl->it_reserved_start = res_start;
	tbl->it_reserved_end = res_end;

	for (i = tbl->it_reserved_start; i < tbl->it_reserved_end; ++i)
		set_bit(i - tbl->it_offset, tbl->it_map);
}

/*
 * Build a iommu_table structure.  This contains a bit map which
 * is used to manage allocation of the tce space.
 */
struct iommu_table *iommu_init_table(struct iommu_table *tbl, int nid,
		unsigned long res_start, unsigned long res_end)
{
	unsigned long sz;
	static int welcomed = 0;
	unsigned int i;
	struct iommu_pool *p;

	BUG_ON(!tbl->it_ops);

	/* number of bytes needed for the bitmap */
	sz = BITS_TO_LONGS(tbl->it_size) * sizeof(unsigned long);

	tbl->it_map = vzalloc_node(sz, nid);
	if (!tbl->it_map) {
		pr_err("%s: Can't allocate %ld bytes\n", __func__, sz);
		return NULL;
	}

	iommu_table_reserve_pages(tbl, res_start, res_end);

	/* We only split the IOMMU table if we have 1GB or more of space */
	if ((tbl->it_size << tbl->it_page_shift) >= (1UL * 1024 * 1024 * 1024))
		tbl->nr_pools = IOMMU_NR_POOLS;
	else
		tbl->nr_pools = 1;

	/* We reserve the top 1/4 of the table for large allocations */
	tbl->poolsize = (tbl->it_size * 3 / 4) / tbl->nr_pools;

	for (i = 0; i < tbl->nr_pools; i++) {
		p = &tbl->pools[i];
		spin_lock_init(&(p->lock));
		p->start = tbl->poolsize * i;
		p->hint = p->start;
		p->end = p->start + tbl->poolsize;
	}

	p = &tbl->large_pool;
	spin_lock_init(&(p->lock));
	p->start = tbl->poolsize * i;
	p->hint = p->start;
	p->end = tbl->it_size;

	iommu_table_clear(tbl);

	if (!welcomed) {
		printk(KERN_INFO "IOMMU table initialized, virtual merging %s\n",
		       novmerge ? "disabled" : "enabled");
		welcomed = 1;
	}

	iommu_debugfs_add(tbl);

	return tbl;
}

bool iommu_table_in_use(struct iommu_table *tbl)
{
	unsigned long start = 0, end;

	/* ignore reserved bit0 */
	if (tbl->it_offset == 0)
		start = 1;

	/* Simple case with no reserved MMIO32 region */
	if (!tbl->it_reserved_start && !tbl->it_reserved_end)
		return find_next_bit(tbl->it_map, tbl->it_size, start) != tbl->it_size;

	end = tbl->it_reserved_start - tbl->it_offset;
	if (find_next_bit(tbl->it_map, end, start) != end)
		return true;

	start = tbl->it_reserved_end - tbl->it_offset;
	end = tbl->it_size;
	return find_next_bit(tbl->it_map, end, start) != end;
}

static void iommu_table_free(struct kref *kref)
{
	struct iommu_table *tbl;

	tbl = container_of(kref, struct iommu_table, it_kref);

	if (tbl->it_ops->free)
		tbl->it_ops->free(tbl);

	if (!tbl->it_map) {
		kfree(tbl);
		return;
	}

	iommu_debugfs_del(tbl);

	/* verify that table contains no entries */
	if (iommu_table_in_use(tbl))
		pr_warn("%s: Unexpected TCEs\n", __func__);

	/* free bitmap */
	vfree(tbl->it_map);

	/* free table */
	kfree(tbl);
}

struct iommu_table *iommu_tce_table_get(struct iommu_table *tbl)
{
	if (kref_get_unless_zero(&tbl->it_kref))
		return tbl;

	return NULL;
}
EXPORT_SYMBOL_GPL(iommu_tce_table_get);

int iommu_tce_table_put(struct iommu_table *tbl)
{
	if (WARN_ON(!tbl))
		return 0;

	return kref_put(&tbl->it_kref, iommu_table_free);
}
EXPORT_SYMBOL_GPL(iommu_tce_table_put);

/* Creates TCEs for a user provided buffer.  The user buffer must be
 * contiguous real kernel storage (not vmalloc).  The address passed here
 * comprises a page address and offset into that page. The dma_addr_t
 * returned will point to the same byte within the page as was passed in.
 */
dma_addr_t iommu_map_page(struct device *dev, struct iommu_table *tbl,
			  struct page *page, unsigned long offset, size_t size,
			  unsigned long mask, enum dma_data_direction direction,
			  unsigned long attrs)
{
	dma_addr_t dma_handle = DMA_MAPPING_ERROR;
	void *vaddr;
	unsigned long uaddr;
	unsigned int npages, align;

	BUG_ON(direction == DMA_NONE);

	vaddr = page_address(page) + offset;
	uaddr = (unsigned long)vaddr;

	if (tbl) {
		npages = iommu_num_pages(uaddr, size, IOMMU_PAGE_SIZE(tbl));
		align = 0;
		if (tbl->it_page_shift < PAGE_SHIFT && size >= PAGE_SIZE &&
		    ((unsigned long)vaddr & ~PAGE_MASK) == 0)
			align = PAGE_SHIFT - tbl->it_page_shift;

		dma_handle = iommu_alloc(dev, tbl, vaddr, npages, direction,
					 mask >> tbl->it_page_shift, align,
					 attrs);
		if (dma_handle == DMA_MAPPING_ERROR) {
			if (!(attrs & DMA_ATTR_NO_WARN) &&
			    printk_ratelimit())  {
				dev_info(dev, "iommu_alloc failed, tbl %p "
					 "vaddr %p npages %d\n", tbl, vaddr,
					 npages);
			}
		} else
			dma_handle |= (uaddr & ~IOMMU_PAGE_MASK(tbl));
	}

	return dma_handle;
}

void iommu_unmap_page(struct iommu_table *tbl, dma_addr_t dma_handle,
		      size_t size, enum dma_data_direction direction,
		      unsigned long attrs)
{
	unsigned int npages;

	BUG_ON(direction == DMA_NONE);

	if (tbl) {
		npages = iommu_num_pages(dma_handle, size,
					 IOMMU_PAGE_SIZE(tbl));
		iommu_free(tbl, dma_handle, npages);
	}
}

/* Allocates a contiguous real buffer and creates mappings over it.
 * Returns the virtual address of the buffer and sets dma_handle
 * to the dma address (mapping) of the first page.
 */
void *iommu_alloc_coherent(struct device *dev, struct iommu_table *tbl,
			   size_t size,	dma_addr_t *dma_handle,
			   unsigned long mask, gfp_t flag, int node)
{
	void *ret = NULL;
	dma_addr_t mapping;
	unsigned int order;
	unsigned int nio_pages, io_order;
	struct page *page;
	int tcesize = (1 << tbl->it_page_shift);

	size = PAGE_ALIGN(size);
	order = get_order(size);

 	/*
	 * Client asked for way too much space.  This is checked later
	 * anyway.  It is easier to debug here for the drivers than in
	 * the tce tables.
	 */
	if (order >= IOMAP_MAX_ORDER) {
		dev_info(dev, "iommu_alloc_consistent size too large: 0x%lx\n",
			 size);
		return NULL;
	}

	if (!tbl)
		return NULL;

	/* Alloc enough pages (and possibly more) */
	page = alloc_pages_node(node, flag, order);
	if (!page)
		return NULL;
	ret = page_address(page);
	memset(ret, 0, size);

	/* Set up tces to cover the allocated range */
	nio_pages = IOMMU_PAGE_ALIGN(size, tbl) >> tbl->it_page_shift;

	io_order = get_iommu_order(size, tbl);
	mapping = iommu_alloc(dev, tbl, ret, nio_pages, DMA_BIDIRECTIONAL,
			      mask >> tbl->it_page_shift, io_order, 0);
	if (mapping == DMA_MAPPING_ERROR) {
		free_pages((unsigned long)ret, order);
		return NULL;
	}

	*dma_handle = mapping | ((u64)ret & (tcesize - 1));
	return ret;
}

void iommu_free_coherent(struct iommu_table *tbl, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	if (tbl) {
		unsigned int nio_pages;

		size = PAGE_ALIGN(size);
		nio_pages = IOMMU_PAGE_ALIGN(size, tbl) >> tbl->it_page_shift;
		iommu_free(tbl, dma_handle, nio_pages);
		size = PAGE_ALIGN(size);
		free_pages((unsigned long)vaddr, get_order(size));
	}
}

unsigned long iommu_direction_to_tce_perm(enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_BIDIRECTIONAL:
		return TCE_PCI_READ | TCE_PCI_WRITE;
	case DMA_FROM_DEVICE:
		return TCE_PCI_WRITE;
	case DMA_TO_DEVICE:
		return TCE_PCI_READ;
	default:
		return 0;
	}
}
EXPORT_SYMBOL_GPL(iommu_direction_to_tce_perm);

#ifdef CONFIG_IOMMU_API
/*
 * SPAPR TCE API
 */
static void group_release(void *iommu_data)
{
	struct iommu_table_group *table_group = iommu_data;

	table_group->group = NULL;
}

void iommu_register_group(struct iommu_table_group *table_group,
		int pci_domain_number, unsigned long pe_num)
{
	struct iommu_group *grp;
	char *name;

	grp = iommu_group_alloc();
	if (IS_ERR(grp)) {
		pr_warn("powerpc iommu api: cannot create new group, err=%ld\n",
				PTR_ERR(grp));
		return;
	}
	table_group->group = grp;
	iommu_group_set_iommudata(grp, table_group, group_release);
	name = kasprintf(GFP_KERNEL, "domain%d-pe%lx",
			pci_domain_number, pe_num);
	if (!name)
		return;
	iommu_group_set_name(grp, name);
	kfree(name);
}

enum dma_data_direction iommu_tce_direction(unsigned long tce)
{
	if ((tce & TCE_PCI_READ) && (tce & TCE_PCI_WRITE))
		return DMA_BIDIRECTIONAL;
	else if (tce & TCE_PCI_READ)
		return DMA_TO_DEVICE;
	else if (tce & TCE_PCI_WRITE)
		return DMA_FROM_DEVICE;
	else
		return DMA_NONE;
}
EXPORT_SYMBOL_GPL(iommu_tce_direction);

void iommu_flush_tce(struct iommu_table *tbl)
{
	/* Flush/invalidate TLB caches if necessary */
	if (tbl->it_ops->flush)
		tbl->it_ops->flush(tbl);

	/* Make sure updates are seen by hardware */
	mb();
}
EXPORT_SYMBOL_GPL(iommu_flush_tce);

int iommu_tce_check_ioba(unsigned long page_shift,
		unsigned long offset, unsigned long size,
		unsigned long ioba, unsigned long npages)
{
	unsigned long mask = (1UL << page_shift) - 1;

	if (ioba & mask)
		return -EINVAL;

	ioba >>= page_shift;
	if (ioba < offset)
		return -EINVAL;

	if ((ioba + 1) > (offset + size))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(iommu_tce_check_ioba);

int iommu_tce_check_gpa(unsigned long page_shift, unsigned long gpa)
{
	unsigned long mask = (1UL << page_shift) - 1;

	if (gpa & mask)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(iommu_tce_check_gpa);

extern long iommu_tce_xchg_no_kill(struct mm_struct *mm,
		struct iommu_table *tbl,
		unsigned long entry, unsigned long *hpa,
		enum dma_data_direction *direction)
{
	long ret;
	unsigned long size = 0;

	ret = tbl->it_ops->xchg_no_kill(tbl, entry, hpa, direction);
	if (!ret && ((*direction == DMA_FROM_DEVICE) ||
			(*direction == DMA_BIDIRECTIONAL)) &&
			!mm_iommu_is_devmem(mm, *hpa, tbl->it_page_shift,
					&size))
		SetPageDirty(pfn_to_page(*hpa >> PAGE_SHIFT));

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_tce_xchg_no_kill);

void iommu_tce_kill(struct iommu_table *tbl,
		unsigned long entry, unsigned long pages)
{
	if (tbl->it_ops->tce_kill)
		tbl->it_ops->tce_kill(tbl, entry, pages);
}
EXPORT_SYMBOL_GPL(iommu_tce_kill);

#if defined(CONFIG_PPC_PSERIES) || defined(CONFIG_PPC_POWERNV)
static int iommu_take_ownership(struct iommu_table *tbl)
{
	unsigned long flags, i, sz = (tbl->it_size + 7) >> 3;
	int ret = 0;

	/*
	 * VFIO does not control TCE entries allocation and the guest
	 * can write new TCEs on top of existing ones so iommu_tce_build()
	 * must be able to release old pages. This functionality
	 * requires exchange() callback defined so if it is not
	 * implemented, we disallow taking ownership over the table.
	 */
	if (!tbl->it_ops->xchg_no_kill)
		return -EINVAL;

	spin_lock_irqsave(&tbl->large_pool.lock, flags);
	for (i = 0; i < tbl->nr_pools; i++)
		spin_lock_nest_lock(&tbl->pools[i].lock, &tbl->large_pool.lock);

	if (iommu_table_in_use(tbl)) {
		pr_err("iommu_tce: it_map is not empty");
		ret = -EBUSY;
	} else {
		memset(tbl->it_map, 0xff, sz);
	}

	for (i = 0; i < tbl->nr_pools; i++)
		spin_unlock(&tbl->pools[i].lock);
	spin_unlock_irqrestore(&tbl->large_pool.lock, flags);

	return ret;
}

static void iommu_release_ownership(struct iommu_table *tbl)
{
	unsigned long flags, i, sz = (tbl->it_size + 7) >> 3;

	spin_lock_irqsave(&tbl->large_pool.lock, flags);
	for (i = 0; i < tbl->nr_pools; i++)
		spin_lock_nest_lock(&tbl->pools[i].lock, &tbl->large_pool.lock);

	memset(tbl->it_map, 0, sz);

	iommu_table_reserve_pages(tbl, tbl->it_reserved_start,
			tbl->it_reserved_end);

	for (i = 0; i < tbl->nr_pools; i++)
		spin_unlock(&tbl->pools[i].lock);
	spin_unlock_irqrestore(&tbl->large_pool.lock, flags);
}
#endif

int iommu_add_device(struct iommu_table_group *table_group, struct device *dev)
{
	/*
	 * The sysfs entries should be populated before
	 * binding IOMMU group. If sysfs entries isn't
	 * ready, we simply bail.
	 */
	if (!device_is_registered(dev))
		return -ENOENT;

	if (device_iommu_mapped(dev)) {
		pr_debug("%s: Skipping device %s with iommu group %d\n",
			 __func__, dev_name(dev),
			 iommu_group_id(dev->iommu_group));
		return -EBUSY;
	}

	pr_debug("%s: Adding %s to iommu group %d\n",
		 __func__, dev_name(dev),  iommu_group_id(table_group->group));
	/*
	 * This is still not adding devices via the IOMMU bus notifier because
	 * of pcibios_init() from arch/powerpc/kernel/pci_64.c which calls
	 * pcibios_scan_phb() first (and this guy adds devices and triggers
	 * the notifier) and only then it calls pci_bus_add_devices() which
	 * configures DMA for buses which also creates PEs and IOMMU groups.
	 */
	return iommu_probe_device(dev);
}
EXPORT_SYMBOL_GPL(iommu_add_device);

#if defined(CONFIG_PPC_PSERIES) || defined(CONFIG_PPC_POWERNV)
/*
 * A simple iommu_table_group_ops which only allows reusing the existing
 * iommu_table. This handles VFIO for POWER7 or the nested KVM.
 * The ops does not allow creating windows and only allows reusing the existing
 * one if it matches table_group->tce32_start/tce32_size/page_shift.
 */
static unsigned long spapr_tce_get_table_size(__u32 page_shift,
					      __u64 window_size, __u32 levels)
{
	unsigned long size;

	if (levels > 1)
		return ~0U;
	size = window_size >> (page_shift - 3);
	return size;
}

static long spapr_tce_create_table(struct iommu_table_group *table_group, int num,
				   __u32 page_shift, __u64 window_size, __u32 levels,
				   struct iommu_table **ptbl)
{
	struct iommu_table *tbl = table_group->tables[0];

	if (num > 0)
		return -EPERM;

	if (tbl->it_page_shift != page_shift ||
	    tbl->it_size != (window_size >> page_shift) ||
	    tbl->it_indirect_levels != levels - 1)
		return -EINVAL;

	*ptbl = iommu_tce_table_get(tbl);
	return 0;
}

static long spapr_tce_set_window(struct iommu_table_group *table_group,
				 int num, struct iommu_table *tbl)
{
	return tbl == table_group->tables[num] ? 0 : -EPERM;
}

static long spapr_tce_unset_window(struct iommu_table_group *table_group, int num)
{
	return 0;
}

static long spapr_tce_take_ownership(struct iommu_table_group *table_group)
{
	int i, j, rc = 0;

	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i) {
		struct iommu_table *tbl = table_group->tables[i];

		if (!tbl || !tbl->it_map)
			continue;

		rc = iommu_take_ownership(tbl);
		if (!rc)
			continue;

		for (j = 0; j < i; ++j)
			iommu_release_ownership(table_group->tables[j]);
		return rc;
	}
	return 0;
}

static void spapr_tce_release_ownership(struct iommu_table_group *table_group)
{
	int i;

	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i) {
		struct iommu_table *tbl = table_group->tables[i];

		if (!tbl)
			continue;

		iommu_table_clear(tbl);
		if (tbl->it_map)
			iommu_release_ownership(tbl);
	}
}

struct iommu_table_group_ops spapr_tce_table_group_ops = {
	.get_table_size = spapr_tce_get_table_size,
	.create_table = spapr_tce_create_table,
	.set_window = spapr_tce_set_window,
	.unset_window = spapr_tce_unset_window,
	.take_ownership = spapr_tce_take_ownership,
	.release_ownership = spapr_tce_release_ownership,
};

/*
 * A simple iommu_ops to allow less cruft in generic VFIO code.
 */
static int
spapr_tce_platform_iommu_attach_dev(struct iommu_domain *platform_domain,
				    struct device *dev)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	struct iommu_group *grp = iommu_group_get(dev);
	struct iommu_table_group *table_group;
	int ret = -EINVAL;

	/* At first attach the ownership is already set */
	if (!domain)
		return 0;

	if (!grp)
		return -ENODEV;

	table_group = iommu_group_get_iommudata(grp);
	ret = table_group->ops->take_ownership(table_group);
	iommu_group_put(grp);

	return ret;
}

static const struct iommu_domain_ops spapr_tce_platform_domain_ops = {
	.attach_dev = spapr_tce_platform_iommu_attach_dev,
};

static struct iommu_domain spapr_tce_platform_domain = {
	.type = IOMMU_DOMAIN_PLATFORM,
	.ops = &spapr_tce_platform_domain_ops,
};

static struct iommu_domain spapr_tce_blocked_domain = {
	.type = IOMMU_DOMAIN_BLOCKED,
	/*
	 * FIXME: SPAPR mixes blocked and platform behaviors, the blocked domain
	 * also sets the dma_api ops
	 */
	.ops = &spapr_tce_platform_domain_ops,
};

static bool spapr_tce_iommu_capable(struct device *dev, enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return true;
	default:
		break;
	}

	return false;
}

static struct iommu_domain *spapr_tce_iommu_domain_alloc(unsigned int type)
{
	if (type != IOMMU_DOMAIN_BLOCKED)
		return NULL;
	return &spapr_tce_blocked_domain;
}

static struct iommu_device *spapr_tce_iommu_probe_device(struct device *dev)
{
	struct pci_dev *pdev;
	struct pci_controller *hose;

	if (!dev_is_pci(dev))
		return ERR_PTR(-EPERM);

	pdev = to_pci_dev(dev);
	hose = pdev->bus->sysdata;

	return &hose->iommu;
}

static void spapr_tce_iommu_release_device(struct device *dev)
{
}

static struct iommu_group *spapr_tce_iommu_device_group(struct device *dev)
{
	struct pci_controller *hose;
	struct pci_dev *pdev;

	pdev = to_pci_dev(dev);
	hose = pdev->bus->sysdata;

	if (!hose->controller_ops.device_group)
		return ERR_PTR(-ENOENT);

	return hose->controller_ops.device_group(hose, pdev);
}

static const struct iommu_ops spapr_tce_iommu_ops = {
	.default_domain = &spapr_tce_platform_domain,
	.capable = spapr_tce_iommu_capable,
	.domain_alloc = spapr_tce_iommu_domain_alloc,
	.probe_device = spapr_tce_iommu_probe_device,
	.release_device = spapr_tce_iommu_release_device,
	.device_group = spapr_tce_iommu_device_group,
};

static struct attribute *spapr_tce_iommu_attrs[] = {
	NULL,
};

static struct attribute_group spapr_tce_iommu_group = {
	.name = "spapr-tce-iommu",
	.attrs = spapr_tce_iommu_attrs,
};

static const struct attribute_group *spapr_tce_iommu_groups[] = {
	&spapr_tce_iommu_group,
	NULL,
};

/*
 * This registers IOMMU devices of PHBs. This needs to happen
 * after core_initcall(iommu_init) + postcore_initcall(pci_driver_init) and
 * before subsys_initcall(iommu_subsys_init).
 */
static int __init spapr_tce_setup_phb_iommus_initcall(void)
{
	struct pci_controller *hose;

	list_for_each_entry(hose, &hose_list, list_node) {
		iommu_device_sysfs_add(&hose->iommu, hose->parent,
				       spapr_tce_iommu_groups, "iommu-phb%04x",
				       hose->global_number);
		iommu_device_register(&hose->iommu, &spapr_tce_iommu_ops,
				      hose->parent);
	}
	return 0;
}
postcore_initcall_sync(spapr_tce_setup_phb_iommus_initcall);
#endif

#endif /* CONFIG_IOMMU_API */
