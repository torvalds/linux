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
#include <linux/bitops.h>
#include <linux/iommu-helper.h>
#include <linux/crash_dump.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/kdump.h>

#define DBG(...)

#ifdef CONFIG_IOMMU_VMERGE
static int novmerge = 0;
#else
static int novmerge = 1;
#endif

static int protect4gb = 1;

static void __iommu_free(struct iommu_table *, dma_addr_t, unsigned int);

static int __init setup_protect4gb(char *str)
{
	if (strcmp(str, "on") == 0)
		protect4gb = 1;
	else if (strcmp(str, "off") == 0)
		protect4gb = 0;

	return 1;
}

static int __init setup_iommu(char *str)
{
	if (!strcmp(str, "novmerge"))
		novmerge = 1;
	else if (!strcmp(str, "vmerge"))
		novmerge = 0;
	return 1;
}

__setup("protect4gb=", setup_protect4gb);
__setup("iommu=", setup_iommu);

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

	align_mask = 0xffffffffffffffffl >> (64 - align_order);

	/* This allocator was derived from x86_64's bit string search */

	/* Sanity check */
	if (unlikely(npages == 0)) {
		if (printk_ratelimit())
			WARN_ON(1);
		return DMA_ERROR_CODE;
	}

	if (handle && *handle)
		start = *handle;
	else
		start = largealloc ? tbl->it_largehint : tbl->it_hint;

	/* Use only half of the table for small allocs (15 pages or less) */
	limit = largealloc ? tbl->it_size : tbl->it_halfpoint;

	if (largealloc && start < tbl->it_halfpoint)
		start = tbl->it_halfpoint;

	/* The case below can happen if we have a small segment appended
	 * to a large, or when the previous alloc was at the very end of
	 * the available space. If so, go back to the initial start.
	 */
	if (start >= limit)
		start = largealloc ? tbl->it_largehint : tbl->it_hint;

 again:

	if (limit + tbl->it_offset > mask) {
		limit = mask - tbl->it_offset + 1;
		/* If we're constrained on address range, first try
		 * at the masked hint to avoid O(n) search complexity,
		 * but on second pass, start at 0.
		 */
		if ((start & mask) >= limit || pass > 0)
			start = 0;
		else
			start &= mask;
	}

	if (dev)
		boundary_size = ALIGN(dma_get_seg_boundary(dev) + 1,
				      1 << IOMMU_PAGE_SHIFT);
	else
		boundary_size = ALIGN(1UL << 32, 1 << IOMMU_PAGE_SHIFT);
	/* 4GB boundary for iseries_hv_alloc and iseries_hv_map */

	n = iommu_area_alloc(tbl->it_map, limit, start, npages,
			     tbl->it_offset, boundary_size >> IOMMU_PAGE_SHIFT,
			     align_mask);
	if (n == -1) {
		if (likely(pass < 2)) {
			/* First failure, just rescan the half of the table.
			 * Second failure, rescan the other half of the table.
			 */
			start = (largealloc ^ pass) ? tbl->it_halfpoint : 0;
			limit = pass ? tbl->it_size : limit;
			pass++;
			goto again;
		} else {
			/* Third failure, give up */
			return DMA_ERROR_CODE;
		}
	}

	end = n + npages;

	/* Bump the hint to a new block for small allocs. */
	if (largealloc) {
		/* Don't bump to new block to avoid fragmentation */
		tbl->it_largehint = end;
	} else {
		/* Overflow will be taken care of at the next allocation */
		tbl->it_hint = (end + tbl->it_blocksize - 1) &
		                ~(tbl->it_blocksize - 1);
	}

	/* Update handle for SG allocations */
	if (handle)
		*handle = end;

	return n;
}

static dma_addr_t iommu_alloc(struct device *dev, struct iommu_table *tbl,
			      void *page, unsigned int npages,
			      enum dma_data_direction direction,
			      unsigned long mask, unsigned int align_order,
			      struct dma_attrs *attrs)
{
	unsigned long entry, flags;
	dma_addr_t ret = DMA_ERROR_CODE;
	int build_fail;

	spin_lock_irqsave(&(tbl->it_lock), flags);

	entry = iommu_range_alloc(dev, tbl, npages, NULL, mask, align_order);

	if (unlikely(entry == DMA_ERROR_CODE)) {
		spin_unlock_irqrestore(&(tbl->it_lock), flags);
		return DMA_ERROR_CODE;
	}

	entry += tbl->it_offset;	/* Offset into real TCE table */
	ret = entry << IOMMU_PAGE_SHIFT;	/* Set the return dma address */

	/* Put the TCEs in the HW table */
	build_fail = ppc_md.tce_build(tbl, entry, npages,
	                              (unsigned long)page & IOMMU_PAGE_MASK,
	                              direction, attrs);

	/* ppc_md.tce_build() only returns non-zero for transient errors.
	 * Clean up the table bitmap in this case and return
	 * DMA_ERROR_CODE. For all other errors the functionality is
	 * not altered.
	 */
	if (unlikely(build_fail)) {
		__iommu_free(tbl, ret, npages);

		spin_unlock_irqrestore(&(tbl->it_lock), flags);
		return DMA_ERROR_CODE;
	}

	/* Flush/invalidate TLB caches if necessary */
	if (ppc_md.tce_flush)
		ppc_md.tce_flush(tbl);

	spin_unlock_irqrestore(&(tbl->it_lock), flags);

	/* Make sure updates are seen by hardware */
	mb();

	return ret;
}

static void __iommu_free(struct iommu_table *tbl, dma_addr_t dma_addr, 
			 unsigned int npages)
{
	unsigned long entry, free_entry;

	entry = dma_addr >> IOMMU_PAGE_SHIFT;
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
		return;
	}

	ppc_md.tce_free(tbl, entry, npages);
	iommu_area_free(tbl->it_map, free_entry, npages);
}

static void iommu_free(struct iommu_table *tbl, dma_addr_t dma_addr,
		unsigned int npages)
{
	unsigned long flags;

	spin_lock_irqsave(&(tbl->it_lock), flags);

	__iommu_free(tbl, dma_addr, npages);

	/* Make sure TLB cache is flushed if the HW needs it. We do
	 * not do an mb() here on purpose, it is not needed on any of
	 * the current platforms.
	 */
	if (ppc_md.tce_flush)
		ppc_md.tce_flush(tbl);

	spin_unlock_irqrestore(&(tbl->it_lock), flags);
}

int iommu_map_sg(struct device *dev, struct iommu_table *tbl,
		 struct scatterlist *sglist, int nelems,
		 unsigned long mask, enum dma_data_direction direction,
		 struct dma_attrs *attrs)
{
	dma_addr_t dma_next = 0, dma_addr;
	unsigned long flags;
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

	spin_lock_irqsave(&(tbl->it_lock), flags);

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
		npages = iommu_num_pages(vaddr, slen, IOMMU_PAGE_SIZE);
		align = 0;
		if (IOMMU_PAGE_SHIFT < PAGE_SHIFT && slen >= PAGE_SIZE &&
		    (vaddr & ~PAGE_MASK) == 0)
			align = PAGE_SHIFT - IOMMU_PAGE_SHIFT;
		entry = iommu_range_alloc(dev, tbl, npages, &handle,
					  mask >> IOMMU_PAGE_SHIFT, align);

		DBG("  - vaddr: %lx, size: %lx\n", vaddr, slen);

		/* Handle failure */
		if (unlikely(entry == DMA_ERROR_CODE)) {
			if (printk_ratelimit())
				printk(KERN_INFO "iommu_alloc failed, tbl %p vaddr %lx"
				       " npages %lx\n", tbl, vaddr, npages);
			goto failure;
		}

		/* Convert entry to a dma_addr_t */
		entry += tbl->it_offset;
		dma_addr = entry << IOMMU_PAGE_SHIFT;
		dma_addr |= (s->offset & ~IOMMU_PAGE_MASK);

		DBG("  - %lu pages, entry: %lx, dma_addr: %lx\n",
			    npages, entry, dma_addr);

		/* Insert into HW table */
		build_fail = ppc_md.tce_build(tbl, entry, npages,
		                              vaddr & IOMMU_PAGE_MASK,
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
	if (ppc_md.tce_flush)
		ppc_md.tce_flush(tbl);

	spin_unlock_irqrestore(&(tbl->it_lock), flags);

	DBG("mapped %d elements:\n", outcount);

	/* For the sake of iommu_unmap_sg, we clear out the length in the
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

			vaddr = s->dma_address & IOMMU_PAGE_MASK;
			npages = iommu_num_pages(s->dma_address, s->dma_length,
						 IOMMU_PAGE_SIZE);
			__iommu_free(tbl, vaddr, npages);
			s->dma_address = DMA_ERROR_CODE;
			s->dma_length = 0;
		}
		if (s == outs)
			break;
	}
	spin_unlock_irqrestore(&(tbl->it_lock), flags);
	return 0;
}


void iommu_unmap_sg(struct iommu_table *tbl, struct scatterlist *sglist,
		int nelems, enum dma_data_direction direction,
		struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	unsigned long flags;

	BUG_ON(direction == DMA_NONE);

	if (!tbl)
		return;

	spin_lock_irqsave(&(tbl->it_lock), flags);

	sg = sglist;
	while (nelems--) {
		unsigned int npages;
		dma_addr_t dma_handle = sg->dma_address;

		if (sg->dma_length == 0)
			break;
		npages = iommu_num_pages(dma_handle, sg->dma_length,
					 IOMMU_PAGE_SIZE);
		__iommu_free(tbl, dma_handle, npages);
		sg = sg_next(sg);
	}

	/* Flush/invalidate TLBs if necessary. As for iommu_free(), we
	 * do not do an mb() here, the affected platforms do not need it
	 * when freeing.
	 */
	if (ppc_md.tce_flush)
		ppc_md.tce_flush(tbl);

	spin_unlock_irqrestore(&(tbl->it_lock), flags);
}

static void iommu_table_clear(struct iommu_table *tbl)
{
	if (!is_kdump_kernel()) {
		/* Clear the table in case firmware left allocations in it */
		ppc_md.tce_free(tbl, tbl->it_offset, tbl->it_size);
		return;
	}

#ifdef CONFIG_CRASH_DUMP
	if (ppc_md.tce_get) {
		unsigned long index, tceval, tcecount = 0;

		/* Reserve the existing mappings left by the first kernel. */
		for (index = 0; index < tbl->it_size; index++) {
			tceval = ppc_md.tce_get(tbl, index + tbl->it_offset);
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

	/* Set aside 1/4 of the table for large allocations. */
	tbl->it_halfpoint = tbl->it_size * 3 / 4;

	/* number of bytes needed for the bitmap */
	sz = (tbl->it_size + 7) >> 3;

	page = alloc_pages_node(nid, GFP_ATOMIC, get_order(sz));
	if (!page)
		panic("iommu_init_table: Can't allocate %ld bytes\n", sz);
	tbl->it_map = page_address(page);
	memset(tbl->it_map, 0, sz);

	tbl->it_hint = 0;
	tbl->it_largehint = tbl->it_halfpoint;
	spin_lock_init(&tbl->it_lock);

	iommu_table_clear(tbl);

	if (!welcomed) {
		printk(KERN_INFO "IOMMU table initialized, virtual merging %s\n",
		       novmerge ? "disabled" : "enabled");
		welcomed = 1;
	}

	return tbl;
}

void iommu_free_table(struct iommu_table *tbl, const char *node_name)
{
	unsigned long bitmap_sz, i;
	unsigned int order;

	if (!tbl || !tbl->it_map) {
		printk(KERN_ERR "%s: expected TCE map for %s\n", __func__,
				node_name);
		return;
	}

	/* verify that table contains no entries */
	/* it_size is in entries, and we're examining 64 at a time */
	for (i = 0; i < (tbl->it_size/64); i++) {
		if (tbl->it_map[i] != 0) {
			printk(KERN_WARNING "%s: Unexpected TCEs for %s\n",
				__func__, node_name);
			break;
		}
	}

	/* calculate bitmap size in bytes */
	bitmap_sz = (tbl->it_size + 7) / 8;

	/* free bitmap */
	order = get_order(bitmap_sz);
	free_pages((unsigned long) tbl->it_map, order);

	/* free table */
	kfree(tbl);
}

/* Creates TCEs for a user provided buffer.  The user buffer must be
 * contiguous real kernel storage (not vmalloc).  The address passed here
 * comprises a page address and offset into that page. The dma_addr_t
 * returned will point to the same byte within the page as was passed in.
 */
dma_addr_t iommu_map_page(struct device *dev, struct iommu_table *tbl,
			  struct page *page, unsigned long offset, size_t size,
			  unsigned long mask, enum dma_data_direction direction,
			  struct dma_attrs *attrs)
{
	dma_addr_t dma_handle = DMA_ERROR_CODE;
	void *vaddr;
	unsigned long uaddr;
	unsigned int npages, align;

	BUG_ON(direction == DMA_NONE);

	vaddr = page_address(page) + offset;
	uaddr = (unsigned long)vaddr;
	npages = iommu_num_pages(uaddr, size, IOMMU_PAGE_SIZE);

	if (tbl) {
		align = 0;
		if (IOMMU_PAGE_SHIFT < PAGE_SHIFT && size >= PAGE_SIZE &&
		    ((unsigned long)vaddr & ~PAGE_MASK) == 0)
			align = PAGE_SHIFT - IOMMU_PAGE_SHIFT;

		dma_handle = iommu_alloc(dev, tbl, vaddr, npages, direction,
					 mask >> IOMMU_PAGE_SHIFT, align,
					 attrs);
		if (dma_handle == DMA_ERROR_CODE) {
			if (printk_ratelimit())  {
				printk(KERN_INFO "iommu_alloc failed, "
						"tbl %p vaddr %p npages %d\n",
						tbl, vaddr, npages);
			}
		} else
			dma_handle |= (uaddr & ~IOMMU_PAGE_MASK);
	}

	return dma_handle;
}

void iommu_unmap_page(struct iommu_table *tbl, dma_addr_t dma_handle,
		      size_t size, enum dma_data_direction direction,
		      struct dma_attrs *attrs)
{
	unsigned int npages;

	BUG_ON(direction == DMA_NONE);

	if (tbl) {
		npages = iommu_num_pages(dma_handle, size, IOMMU_PAGE_SIZE);
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
		printk("iommu_alloc_consistent size too large: 0x%lx\n", size);
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
	nio_pages = size >> IOMMU_PAGE_SHIFT;
	io_order = get_iommu_order(size);
	mapping = iommu_alloc(dev, tbl, ret, nio_pages, DMA_BIDIRECTIONAL,
			      mask >> IOMMU_PAGE_SHIFT, io_order, NULL);
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
		nio_pages = size >> IOMMU_PAGE_SHIFT;
		iommu_free(tbl, dma_handle, nio_pages);
		size = PAGE_ALIGN(size);
		free_pages((unsigned long)vaddr, get_order(size));
	}
}
