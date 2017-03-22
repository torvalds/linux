/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * 
 * Rewrite, cleanup, new allocation schemes, virtual merging: 
 * Copyright (C) 2004 Olof Johansson, IBM Corporation
 *               and  Ben. Herrenschmidt, IBM Corporation
 *
 * Dynamic DMA mapping support, bus-independent parts.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
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
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/kdump.h>
#include <asm/fadump.h>
#include <asm/vio.h>
#include <asm/tce.h>

#define DBG(...)

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

static DEVICE_ATTR(fail_iommu, S_IRUGO|S_IWUSR, fail_iommu_show,
		   fail_iommu_store);

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

static struct notifier_block fail_iommu_bus_notifier = {
	.notifier_call = fail_iommu_bus_notify
};

static int __init fail_iommu_setup(void)
{
#ifdef CONFIG_PCI
	bus_register_notifier(&pci_bus_type, &fail_iommu_bus_notifier);
#endif
#ifdef CONFIG_IBMVIO
	bus_register_notifier(&vio_bus_type, &fail_iommu_bus_notifier);
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
	unsigned long boundary_size;
	unsigned long flags;
	unsigned int pool_nr;
	struct iommu_pool *pool;

	align_mask = 0xffffffffffffffffl >> (64 - align_order);

	/* This allocator was derived from x86_64's bit string search */

	/* Sanity check */
	if (unlikely(npages == 0)) {
		if (printk_ratelimit())
			WARN_ON(1);
		return DMA_ERROR_CODE;
	}

	if (should_fail_iommu(dev))
		return DMA_ERROR_CODE;

	/*
	 * We don't need to disable preemption here because any CPU can
	 * safely use any IOMMU pool.
	 */
	pool_nr = __this_cpu_read(iommu_pool_hash) & (tbl->nr_pools - 1);

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

	if (dev)
		boundary_size = ALIGN(dma_get_seg_boundary(dev) + 1,
				      1 << tbl->it_page_shift);
	else
		boundary_size = ALIGN(1UL << 32, 1 << tbl->it_page_shift);
	/* 4GB boundary for iseries_hv_alloc and iseries_hv_map */

	n = iommu_area_alloc(tbl->it_map, limit, start, npages, tbl->it_offset,
			     boundary_size >> tbl->it_page_shift, align_mask);
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

		} else {
			/* Give up */
			spin_unlock_irqrestore(&(pool->lock), flags);
			return DMA_ERROR_CODE;
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
	dma_addr_t ret = DMA_ERROR_CODE;
	int build_fail;

	entry = iommu_range_alloc(dev, tbl, npages, NULL, mask, align_order);

	if (unlikely(entry == DMA_ERROR_CODE))
		return DMA_ERROR_CODE;

	entry += tbl->it_offset;	/* Offset into real TCE table */
	ret = entry << tbl->it_page_shift;	/* Set the return dma address */

	/* Put the TCEs in the HW table */
	build_fail = tbl->it_ops->set(tbl, entry, npages,
				      (unsigned long)page &
				      IOMMU_PAGE_MASK(tbl), direction, attrs);

	/* tbl->it_ops->set() only returns non-zero for transient errors.
	 * Clean up the table bitmap in this case and return
	 * DMA_ERROR_CODE. For all other errors the functionality is
	 * not altered.
	 */
	if (unlikely(build_fail)) {
		__iommu_free(tbl, ret, npages);
		return DMA_ERROR_CODE;
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
		return 0;

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
		if (unlikely(entry == DMA_ERROR_CODE)) {
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
		dma_addr |= (s->offset & ~IOMMU_PAGE_MASK(tbl));

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
		outs->dma_address = DMA_ERROR_CODE;
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
			s->dma_address = DMA_ERROR_CODE;
			s->dma_length = 0;
		}
		if (s == outs)
			break;
	}
	return 0;
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

/*
 * Build a iommu_table structure.  This contains a bit map which
 * is used to manage allocation of the tce space.
 */
struct iommu_table *iommu_init_table(struct iommu_table *tbl, int nid)
{
	unsigned long sz;
	static int welcomed = 0;
	struct page *page;
	unsigned int i;
	struct iommu_pool *p;

	BUG_ON(!tbl->it_ops);

	/* number of bytes needed for the bitmap */
	sz = BITS_TO_LONGS(tbl->it_size) * sizeof(unsigned long);

	page = alloc_pages_node(nid, GFP_KERNEL, get_order(sz));
	if (!page)
		panic("iommu_init_table: Can't allocate %ld bytes\n", sz);
	tbl->it_map = page_address(page);
	memset(tbl->it_map, 0, sz);

	/*
	 * Reserve page 0 so it will not be used for any mappings.
	 * This avoids buggy drivers that consider page 0 to be invalid
	 * to crash the machine or even lose data.
	 */
	if (tbl->it_offset == 0)
		set_bit(0, tbl->it_map);

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

	return tbl;
}

static void iommu_table_free(struct kref *kref)
{
	unsigned long bitmap_sz;
	unsigned int order;
	struct iommu_table *tbl;

	tbl = container_of(kref, struct iommu_table, it_kref);

	if (tbl->it_ops->free)
		tbl->it_ops->free(tbl);

	if (!tbl->it_map) {
		kfree(tbl);
		return;
	}

	/*
	 * In case we have reserved the first bit, we should not emit
	 * the warning below.
	 */
	if (tbl->it_offset == 0)
		clear_bit(0, tbl->it_map);

	/* verify that table contains no entries */
	if (!bitmap_empty(tbl->it_map, tbl->it_size))
		pr_warn("%s: Unexpected TCEs\n", __func__);

	/* calculate bitmap size in bytes */
	bitmap_sz = BITS_TO_LONGS(tbl->it_size) * sizeof(unsigned long);

	/* free bitmap */
	order = get_order(bitmap_sz);
	free_pages((unsigned long) tbl->it_map, order);

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
	dma_addr_t dma_handle = DMA_ERROR_CODE;
	void *vaddr;
	unsigned long uaddr;
	unsigned int npages, align;

	BUG_ON(direction == DMA_NONE);

	vaddr = page_address(page) + offset;
	uaddr = (unsigned long)vaddr;
	npages = iommu_num_pages(uaddr, size, IOMMU_PAGE_SIZE(tbl));

	if (tbl) {
		align = 0;
		if (tbl->it_page_shift < PAGE_SHIFT && size >= PAGE_SIZE &&
		    ((unsigned long)vaddr & ~PAGE_MASK) == 0)
			align = PAGE_SHIFT - tbl->it_page_shift;

		dma_handle = iommu_alloc(dev, tbl, vaddr, npages, direction,
					 mask >> tbl->it_page_shift, align,
					 attrs);
		if (dma_handle == DMA_ERROR_CODE) {
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
	nio_pages = size >> tbl->it_page_shift;
	io_order = get_iommu_order(size, tbl);
	mapping = iommu_alloc(dev, tbl, ret, nio_pages, DMA_BIDIRECTIONAL,
			      mask >> tbl->it_page_shift, io_order, 0);
	if (mapping == DMA_ERROR_CODE) {
		free_pages((unsigned long)ret, order);
		return NULL;
	}
	*dma_handle = mapping;
	return ret;
}

void iommu_free_coherent(struct iommu_table *tbl, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	if (tbl) {
		unsigned int nio_pages;

		size = PAGE_ALIGN(size);
		nio_pages = size >> tbl->it_page_shift;
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

int iommu_tce_clear_param_check(struct iommu_table *tbl,
		unsigned long ioba, unsigned long tce_value,
		unsigned long npages)
{
	/* tbl->it_ops->clear() does not support any value but 0 */
	if (tce_value)
		return -EINVAL;

	if (ioba & ~IOMMU_PAGE_MASK(tbl))
		return -EINVAL;

	ioba >>= tbl->it_page_shift;
	if (ioba < tbl->it_offset)
		return -EINVAL;

	if ((ioba + npages) > (tbl->it_offset + tbl->it_size))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(iommu_tce_clear_param_check);

int iommu_tce_put_param_check(struct iommu_table *tbl,
		unsigned long ioba, unsigned long tce)
{
	if (tce & ~IOMMU_PAGE_MASK(tbl))
		return -EINVAL;

	if (ioba & ~IOMMU_PAGE_MASK(tbl))
		return -EINVAL;

	ioba >>= tbl->it_page_shift;
	if (ioba < tbl->it_offset)
		return -EINVAL;

	if ((ioba + 1) > (tbl->it_offset + tbl->it_size))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(iommu_tce_put_param_check);

long iommu_tce_xchg(struct iommu_table *tbl, unsigned long entry,
		unsigned long *hpa, enum dma_data_direction *direction)
{
	long ret;

	ret = tbl->it_ops->exchange(tbl, entry, hpa, direction);

	if (!ret && ((*direction == DMA_FROM_DEVICE) ||
			(*direction == DMA_BIDIRECTIONAL)))
		SetPageDirty(pfn_to_page(*hpa >> PAGE_SHIFT));

	/* if (unlikely(ret))
		pr_err("iommu_tce: %s failed on hwaddr=%lx ioba=%lx kva=%lx ret=%d\n",
			__func__, hwaddr, entry << tbl->it_page_shift,
				hwaddr, ret); */

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_tce_xchg);

#ifdef CONFIG_PPC_BOOK3S_64
long iommu_tce_xchg_rm(struct iommu_table *tbl, unsigned long entry,
		unsigned long *hpa, enum dma_data_direction *direction)
{
	long ret;

	ret = tbl->it_ops->exchange_rm(tbl, entry, hpa, direction);

	if (!ret && ((*direction == DMA_FROM_DEVICE) ||
			(*direction == DMA_BIDIRECTIONAL))) {
		struct page *pg = realmode_pfn_to_page(*hpa >> PAGE_SHIFT);

		if (likely(pg)) {
			SetPageDirty(pg);
		} else {
			tbl->it_ops->exchange_rm(tbl, entry, hpa, direction);
			ret = -EFAULT;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_tce_xchg_rm);
#endif

int iommu_take_ownership(struct iommu_table *tbl)
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
	if (!tbl->it_ops->exchange)
		return -EINVAL;

	spin_lock_irqsave(&tbl->large_pool.lock, flags);
	for (i = 0; i < tbl->nr_pools; i++)
		spin_lock(&tbl->pools[i].lock);

	if (tbl->it_offset == 0)
		clear_bit(0, tbl->it_map);

	if (!bitmap_empty(tbl->it_map, tbl->it_size)) {
		pr_err("iommu_tce: it_map is not empty");
		ret = -EBUSY;
		/* Restore bit#0 set by iommu_init_table() */
		if (tbl->it_offset == 0)
			set_bit(0, tbl->it_map);
	} else {
		memset(tbl->it_map, 0xff, sz);
	}

	for (i = 0; i < tbl->nr_pools; i++)
		spin_unlock(&tbl->pools[i].lock);
	spin_unlock_irqrestore(&tbl->large_pool.lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_take_ownership);

void iommu_release_ownership(struct iommu_table *tbl)
{
	unsigned long flags, i, sz = (tbl->it_size + 7) >> 3;

	spin_lock_irqsave(&tbl->large_pool.lock, flags);
	for (i = 0; i < tbl->nr_pools; i++)
		spin_lock(&tbl->pools[i].lock);

	memset(tbl->it_map, 0, sz);

	/* Restore bit#0 set by iommu_init_table() */
	if (tbl->it_offset == 0)
		set_bit(0, tbl->it_map);

	for (i = 0; i < tbl->nr_pools; i++)
		spin_unlock(&tbl->pools[i].lock);
	spin_unlock_irqrestore(&tbl->large_pool.lock, flags);
}
EXPORT_SYMBOL_GPL(iommu_release_ownership);

int iommu_add_device(struct device *dev)
{
	struct iommu_table *tbl;
	struct iommu_table_group_link *tgl;

	/*
	 * The sysfs entries should be populated before
	 * binding IOMMU group. If sysfs entries isn't
	 * ready, we simply bail.
	 */
	if (!device_is_registered(dev))
		return -ENOENT;

	if (dev->iommu_group) {
		pr_debug("%s: Skipping device %s with iommu group %d\n",
			 __func__, dev_name(dev),
			 iommu_group_id(dev->iommu_group));
		return -EBUSY;
	}

	tbl = get_iommu_table_base(dev);
	if (!tbl) {
		pr_debug("%s: Skipping device %s with no tbl\n",
			 __func__, dev_name(dev));
		return 0;
	}

	tgl = list_first_entry_or_null(&tbl->it_group_list,
			struct iommu_table_group_link, next);
	if (!tgl) {
		pr_debug("%s: Skipping device %s with no group\n",
			 __func__, dev_name(dev));
		return 0;
	}
	pr_debug("%s: Adding %s to iommu group %d\n",
		 __func__, dev_name(dev),
		 iommu_group_id(tgl->table_group->group));

	if (PAGE_SIZE < IOMMU_PAGE_SIZE(tbl)) {
		pr_err("%s: Invalid IOMMU page size %lx (%lx) on %s\n",
		       __func__, IOMMU_PAGE_SIZE(tbl),
		       PAGE_SIZE, dev_name(dev));
		return -EINVAL;
	}

	return iommu_group_add_device(tgl->table_group->group, dev);
}
EXPORT_SYMBOL_GPL(iommu_add_device);

void iommu_del_device(struct device *dev)
{
	/*
	 * Some devices might not have IOMMU table and group
	 * and we needn't detach them from the associated
	 * IOMMU groups
	 */
	if (!dev->iommu_group) {
		pr_debug("iommu_tce: skipping device %s with no tbl\n",
			 dev_name(dev));
		return;
	}

	iommu_group_remove_device(dev);
}
EXPORT_SYMBOL_GPL(iommu_del_device);

static int tce_iommu_bus_notifier(struct notifier_block *nb,
                unsigned long action, void *data)
{
        struct device *dev = data;

        switch (action) {
        case BUS_NOTIFY_ADD_DEVICE:
                return iommu_add_device(dev);
        case BUS_NOTIFY_DEL_DEVICE:
                if (dev->iommu_group)
                        iommu_del_device(dev);
                return 0;
        default:
                return 0;
        }
}

static struct notifier_block tce_iommu_bus_nb = {
        .notifier_call = tce_iommu_bus_notifier,
};

int __init tce_iommu_bus_notifier_init(void)
{
        bus_register_notifier(&pci_bus_type, &tce_iommu_bus_nb);
        return 0;
}
#endif /* CONFIG_IOMMU_API */
