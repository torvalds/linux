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

static inline unsigned long iommu_num_pages(unsigned long vaddr,
					    unsigned long slen)
{
	unsigned long npages;

	npages = IOMMU_PAGE_ALIGN(vaddr + slen) - (vaddr & IOMMU_PAGE_MASK);
	npages >>= IOMMU_PAGE_SHIFT;

	return npages;
}

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

static unsigned long iommu_range_alloc(struct iommu_table *tbl,
                                       unsigned long npages,
                                       unsigned long *handle,
                                       unsigned long mask,
                                       unsigned int align_order)
{ 
	unsigned long n, end, i, start;
	unsigned long limit;
	int largealloc = npages > 15;
	int pass = 0;
	unsigned long align_mask;

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

	n = find_next_zero_bit(tbl->it_map, limit, start);

	/* Align allocation */
	n = (n + align_mask) & ~align_mask;

	end = n + npages;

	if (unlikely(end >= limit)) {
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

	for (i = n; i < end; i++)
		if (test_bit(i, tbl->it_map)) {
			start = i+1;
			goto again;
		}

	for (i = n; i < end; i++)
		__set_bit(i, tbl->it_map);

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

static dma_addr_t iommu_alloc(struct iommu_table *tbl, void *page,
		       unsigned int npages, enum dma_data_direction direction,
		       unsigned long mask, unsigned int align_order)
{
	unsigned long entry, flags;
	dma_addr_t ret = DMA_ERROR_CODE;

	spin_lock_irqsave(&(tbl->it_lock), flags);

	entry = iommu_range_alloc(tbl, npages, NULL, mask, align_order);

	if (unlikely(entry == DMA_ERROR_CODE)) {
		spin_unlock_irqrestore(&(tbl->it_lock), flags);
		return DMA_ERROR_CODE;
	}

	entry += tbl->it_offset;	/* Offset into real TCE table */
	ret = entry << IOMMU_PAGE_SHIFT;	/* Set the return dma address */

	/* Put the TCEs in the HW table */
	ppc_md.tce_build(tbl, entry, npages, (unsigned long)page & IOMMU_PAGE_MASK,
			 direction);


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
	unsigned long i;

	entry = dma_addr >> IOMMU_PAGE_SHIFT;
	free_entry = entry - tbl->it_offset;

	if (((free_entry + npages) > tbl->it_size) ||
	    (entry < tbl->it_offset)) {
		if (printk_ratelimit()) {
			printk(KERN_INFO "iommu_free: invalid entry\n");
			printk(KERN_INFO "\tentry     = 0x%lx\n", entry); 
			printk(KERN_INFO "\tdma_addr  = 0x%lx\n", (u64)dma_addr);
			printk(KERN_INFO "\tTable     = 0x%lx\n", (u64)tbl);
			printk(KERN_INFO "\tbus#      = 0x%lx\n", (u64)tbl->it_busno);
			printk(KERN_INFO "\tsize      = 0x%lx\n", (u64)tbl->it_size);
			printk(KERN_INFO "\tstartOff  = 0x%lx\n", (u64)tbl->it_offset);
			printk(KERN_INFO "\tindex     = 0x%lx\n", (u64)tbl->it_index);
			WARN_ON(1);
		}
		return;
	}

	ppc_md.tce_free(tbl, entry, npages);
	
	for (i = 0; i < npages; i++)
		__clear_bit(free_entry+i, tbl->it_map);
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

int iommu_map_sg(struct iommu_table *tbl, struct scatterlist *sglist,
		 int nelems, unsigned long mask,
		 enum dma_data_direction direction)
{
	dma_addr_t dma_next = 0, dma_addr;
	unsigned long flags;
	struct scatterlist *s, *outs, *segstart;
	int outcount, incount;
	unsigned long handle;

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

	for (s = outs; nelems; nelems--, s++) {
		unsigned long vaddr, npages, entry, slen;

		slen = s->length;
		/* Sanity check */
		if (slen == 0) {
			dma_next = 0;
			continue;
		}
		/* Allocate iommu entries for that segment */
		vaddr = (unsigned long)page_address(s->page) + s->offset;
		npages = iommu_num_pages(vaddr, slen);
		entry = iommu_range_alloc(tbl, npages, &handle, mask >> IOMMU_PAGE_SHIFT, 0);

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
		ppc_md.tce_build(tbl, entry, npages, vaddr & IOMMU_PAGE_MASK, direction);

		/* If we are in an open segment, try merging */
		if (segstart != s) {
			DBG("  - trying merge...\n");
			/* We cannot merge if:
			 * - allocated dma_addr isn't contiguous to previous allocation
			 */
			if (novmerge || (dma_addr != dma_next)) {
				/* Can't merge: create a new segment */
				segstart = s;
				outcount++; outs++;
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
		outs++;
		outs->dma_address = DMA_ERROR_CODE;
		outs->dma_length = 0;
	}

	/* Make sure updates are seen by hardware */
	mb();

	return outcount;

 failure:
	for (s = &sglist[0]; s <= outs; s++) {
		if (s->dma_length != 0) {
			unsigned long vaddr, npages;

			vaddr = s->dma_address & IOMMU_PAGE_MASK;
			npages = iommu_num_pages(s->dma_address, s->dma_length);
			__iommu_free(tbl, vaddr, npages);
			s->dma_address = DMA_ERROR_CODE;
			s->dma_length = 0;
		}
	}
	spin_unlock_irqrestore(&(tbl->it_lock), flags);
	return 0;
}


void iommu_unmap_sg(struct iommu_table *tbl, struct scatterlist *sglist,
		int nelems, enum dma_data_direction direction)
{
	unsigned long flags;

	BUG_ON(direction == DMA_NONE);

	if (!tbl)
		return;

	spin_lock_irqsave(&(tbl->it_lock), flags);

	while (nelems--) {
		unsigned int npages;
		dma_addr_t dma_handle = sglist->dma_address;

		if (sglist->dma_length == 0)
			break;
		npages = iommu_num_pages(dma_handle,sglist->dma_length);
		__iommu_free(tbl, dma_handle, npages);
		sglist++;
	}

	/* Flush/invalidate TLBs if necessary. As for iommu_free(), we
	 * do not do an mb() here, the affected platforms do not need it
	 * when freeing.
	 */
	if (ppc_md.tce_flush)
		ppc_md.tce_flush(tbl);

	spin_unlock_irqrestore(&(tbl->it_lock), flags);
}

/*
 * Build a iommu_table structure.  This contains a bit map which
 * is used to manage allocation of the tce space.
 */
struct iommu_table *iommu_init_table(struct iommu_table *tbl, int nid)
{
	unsigned long sz;
	unsigned long start_index, end_index;
	unsigned long entries_per_4g;
	unsigned long index;
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

#ifdef CONFIG_CRASH_DUMP
	if (ppc_md.tce_get) {
		unsigned long tceval;
		unsigned long tcecount = 0;

		/*
		 * Reserve the existing mappings left by the first kernel.
		 */
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
			printk(KERN_WARNING "TCE table is full; ");
			printk(KERN_WARNING "freeing %d entries for the kdump boot\n",
				KDUMP_MIN_TCE_ENTRIES);
			for (index = tbl->it_size - KDUMP_MIN_TCE_ENTRIES;
				index < tbl->it_size; index++)
				__clear_bit(index, tbl->it_map);
		}
	}
#else
	/* Clear the hardware table in case firmware left allocations in it */
	ppc_md.tce_free(tbl, tbl->it_offset, tbl->it_size);
#endif

	/*
	 * DMA cannot cross 4 GB boundary.  Mark last entry of each 4
	 * GB chunk as reserved.
	 */
	if (protect4gb) {
		entries_per_4g = 0x100000000l >> IOMMU_PAGE_SHIFT;

		/* Mark the last bit before a 4GB boundary as used */
		start_index = tbl->it_offset | (entries_per_4g - 1);
		start_index -= tbl->it_offset;

		end_index = tbl->it_size;

		for (index = start_index; index < end_index - 1; index += entries_per_4g)
			__set_bit(index, tbl->it_map);
	}

	if (!welcomed) {
		printk(KERN_INFO "IOMMU table initialized, virtual merging %s\n",
		       novmerge ? "disabled" : "enabled");
		welcomed = 1;
	}

	return tbl;
}

void iommu_free_table(struct device_node *dn)
{
	struct pci_dn *pdn = dn->data;
	struct iommu_table *tbl = pdn->iommu_table;
	unsigned long bitmap_sz, i;
	unsigned int order;

	if (!tbl || !tbl->it_map) {
		printk(KERN_ERR "%s: expected TCE map for %s\n", __FUNCTION__,
				dn->full_name);
		return;
	}

	/* verify that table contains no entries */
	/* it_size is in entries, and we're examining 64 at a time */
	for (i = 0; i < (tbl->it_size/64); i++) {
		if (tbl->it_map[i] != 0) {
			printk(KERN_WARNING "%s: Unexpected TCEs for %s\n",
				__FUNCTION__, dn->full_name);
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
 * contiguous real kernel storage (not vmalloc).  The address of the buffer
 * passed here is the kernel (virtual) address of the buffer.  The buffer
 * need not be page aligned, the dma_addr_t returned will point to the same
 * byte within the page as vaddr.
 */
dma_addr_t iommu_map_single(struct iommu_table *tbl, void *vaddr,
		size_t size, unsigned long mask,
		enum dma_data_direction direction)
{
	dma_addr_t dma_handle = DMA_ERROR_CODE;
	unsigned long uaddr;
	unsigned int npages;

	BUG_ON(direction == DMA_NONE);

	uaddr = (unsigned long)vaddr;
	npages = iommu_num_pages(uaddr, size);

	if (tbl) {
		dma_handle = iommu_alloc(tbl, vaddr, npages, direction,
					 mask >> IOMMU_PAGE_SHIFT, 0);
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

void iommu_unmap_single(struct iommu_table *tbl, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction direction)
{
	unsigned int npages;

	BUG_ON(direction == DMA_NONE);

	if (tbl) {
		npages = iommu_num_pages(dma_handle, size);
		iommu_free(tbl, dma_handle, npages);
	}
}

/* Allocates a contiguous real buffer and creates mappings over it.
 * Returns the virtual address of the buffer and sets dma_handle
 * to the dma address (mapping) of the first page.
 */
void *iommu_alloc_coherent(struct iommu_table *tbl, size_t size,
		dma_addr_t *dma_handle, unsigned long mask, gfp_t flag, int node)
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
	mapping = iommu_alloc(tbl, ret, nio_pages, DMA_BIDIRECTIONAL,
			      mask >> IOMMU_PAGE_SHIFT, io_order);
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
