/* iommu.c: Generic sparc64 IOMMU support.
 *
 * Copyright (C) 1999, 2007, 2008 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1999, 2000 Jakub Jelinek (jakub@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/iommu-helper.h>

#ifdef CONFIG_PCI
#include <linux/pci.h>
#endif

#include <asm/iommu.h>

#include "iommu_common.h"

#define STC_CTXMATCH_ADDR(STC, CTX)	\
	((STC)->strbuf_ctxmatch_base + ((CTX) << 3))
#define STC_FLUSHFLAG_INIT(STC) \
	(*((STC)->strbuf_flushflag) = 0UL)
#define STC_FLUSHFLAG_SET(STC) \
	(*((STC)->strbuf_flushflag) != 0UL)

#define iommu_read(__reg) \
({	u64 __ret; \
	__asm__ __volatile__("ldxa [%1] %2, %0" \
			     : "=r" (__ret) \
			     : "r" (__reg), "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory"); \
	__ret; \
})
#define iommu_write(__reg, __val) \
	__asm__ __volatile__("stxa %0, [%1] %2" \
			     : /* no outputs */ \
			     : "r" (__val), "r" (__reg), \
			       "i" (ASI_PHYS_BYPASS_EC_E))

/* Must be invoked under the IOMMU lock. */
static void iommu_flushall(struct iommu *iommu)
{
	if (iommu->iommu_flushinv) {
		iommu_write(iommu->iommu_flushinv, ~(u64)0);
	} else {
		unsigned long tag;
		int entry;

		tag = iommu->iommu_tags;
		for (entry = 0; entry < 16; entry++) {
			iommu_write(tag, 0);
			tag += 8;
		}

		/* Ensure completion of previous PIO writes. */
		(void) iommu_read(iommu->write_complete_reg);
	}
}

#define IOPTE_CONSISTENT(CTX) \
	(IOPTE_VALID | IOPTE_CACHE | \
	 (((CTX) << 47) & IOPTE_CONTEXT))

#define IOPTE_STREAMING(CTX) \
	(IOPTE_CONSISTENT(CTX) | IOPTE_STBUF)

/* Existing mappings are never marked invalid, instead they
 * are pointed to a dummy page.
 */
#define IOPTE_IS_DUMMY(iommu, iopte)	\
	((iopte_val(*iopte) & IOPTE_PAGE) == (iommu)->dummy_page_pa)

static inline void iopte_make_dummy(struct iommu *iommu, iopte_t *iopte)
{
	unsigned long val = iopte_val(*iopte);

	val &= ~IOPTE_PAGE;
	val |= iommu->dummy_page_pa;

	iopte_val(*iopte) = val;
}

/* Based almost entirely upon the ppc64 iommu allocator.  If you use the 'handle'
 * facility it must all be done in one pass while under the iommu lock.
 *
 * On sun4u platforms, we only flush the IOMMU once every time we've passed
 * over the entire page table doing allocations.  Therefore we only ever advance
 * the hint and cannot backtrack it.
 */
unsigned long iommu_range_alloc(struct device *dev,
				struct iommu *iommu,
				unsigned long npages,
				unsigned long *handle)
{
	unsigned long n, end, start, limit, boundary_size;
	struct iommu_arena *arena = &iommu->arena;
	int pass = 0;

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
		start = arena->hint;

	limit = arena->limit;

	/* The case below can happen if we have a small segment appended
	 * to a large, or when the previous alloc was at the very end of
	 * the available space. If so, go back to the beginning and flush.
	 */
	if (start >= limit) {
		start = 0;
		if (iommu->flush_all)
			iommu->flush_all(iommu);
	}

 again:

	if (dev)
		boundary_size = ALIGN(dma_get_seg_boundary(dev) + 1,
				      1 << IO_PAGE_SHIFT);
	else
		boundary_size = ALIGN(1UL << 32, 1 << IO_PAGE_SHIFT);

	n = iommu_area_alloc(arena->map, limit, start, npages,
			     iommu->page_table_map_base >> IO_PAGE_SHIFT,
			     boundary_size >> IO_PAGE_SHIFT, 0);
	if (n == -1) {
		if (likely(pass < 1)) {
			/* First failure, rescan from the beginning.  */
			start = 0;
			if (iommu->flush_all)
				iommu->flush_all(iommu);
			pass++;
			goto again;
		} else {
			/* Second failure, give up */
			return DMA_ERROR_CODE;
		}
	}

	end = n + npages;

	arena->hint = end;

	/* Update handle for SG allocations */
	if (handle)
		*handle = end;

	return n;
}

void iommu_range_free(struct iommu *iommu, dma_addr_t dma_addr, unsigned long npages)
{
	struct iommu_arena *arena = &iommu->arena;
	unsigned long entry;

	entry = (dma_addr - iommu->page_table_map_base) >> IO_PAGE_SHIFT;

	iommu_area_free(arena->map, entry, npages);
}

int iommu_table_init(struct iommu *iommu, int tsbsize,
		     u32 dma_offset, u32 dma_addr_mask,
		     int numa_node)
{
	unsigned long i, order, sz, num_tsb_entries;
	struct page *page;

	num_tsb_entries = tsbsize / sizeof(iopte_t);

	/* Setup initial software IOMMU state. */
	spin_lock_init(&iommu->lock);
	iommu->ctx_lowest_free = 1;
	iommu->page_table_map_base = dma_offset;
	iommu->dma_addr_mask = dma_addr_mask;

	/* Allocate and initialize the free area map.  */
	sz = num_tsb_entries / 8;
	sz = (sz + 7UL) & ~7UL;
	iommu->arena.map = kmalloc_node(sz, GFP_KERNEL, numa_node);
	if (!iommu->arena.map) {
		printk(KERN_ERR "IOMMU: Error, kmalloc(arena.map) failed.\n");
		return -ENOMEM;
	}
	memset(iommu->arena.map, 0, sz);
	iommu->arena.limit = num_tsb_entries;

	if (tlb_type != hypervisor)
		iommu->flush_all = iommu_flushall;

	/* Allocate and initialize the dummy page which we
	 * set inactive IO PTEs to point to.
	 */
	page = alloc_pages_node(numa_node, GFP_KERNEL, 0);
	if (!page) {
		printk(KERN_ERR "IOMMU: Error, gfp(dummy_page) failed.\n");
		goto out_free_map;
	}
	iommu->dummy_page = (unsigned long) page_address(page);
	memset((void *)iommu->dummy_page, 0, PAGE_SIZE);
	iommu->dummy_page_pa = (unsigned long) __pa(iommu->dummy_page);

	/* Now allocate and setup the IOMMU page table itself.  */
	order = get_order(tsbsize);
	page = alloc_pages_node(numa_node, GFP_KERNEL, order);
	if (!page) {
		printk(KERN_ERR "IOMMU: Error, gfp(tsb) failed.\n");
		goto out_free_dummy_page;
	}
	iommu->page_table = (iopte_t *)page_address(page);

	for (i = 0; i < num_tsb_entries; i++)
		iopte_make_dummy(iommu, &iommu->page_table[i]);

	return 0;

out_free_dummy_page:
	free_page(iommu->dummy_page);
	iommu->dummy_page = 0UL;

out_free_map:
	kfree(iommu->arena.map);
	iommu->arena.map = NULL;

	return -ENOMEM;
}

static inline iopte_t *alloc_npages(struct device *dev, struct iommu *iommu,
				    unsigned long npages)
{
	unsigned long entry;

	entry = iommu_range_alloc(dev, iommu, npages, NULL);
	if (unlikely(entry == DMA_ERROR_CODE))
		return NULL;

	return iommu->page_table + entry;
}

static int iommu_alloc_ctx(struct iommu *iommu)
{
	int lowest = iommu->ctx_lowest_free;
	int sz = IOMMU_NUM_CTXS - lowest;
	int n = find_next_zero_bit(iommu->ctx_bitmap, sz, lowest);

	if (unlikely(n == sz)) {
		n = find_next_zero_bit(iommu->ctx_bitmap, lowest, 1);
		if (unlikely(n == lowest)) {
			printk(KERN_WARNING "IOMMU: Ran out of contexts.\n");
			n = 0;
		}
	}
	if (n)
		__set_bit(n, iommu->ctx_bitmap);

	return n;
}

static inline void iommu_free_ctx(struct iommu *iommu, int ctx)
{
	if (likely(ctx)) {
		__clear_bit(ctx, iommu->ctx_bitmap);
		if (ctx < iommu->ctx_lowest_free)
			iommu->ctx_lowest_free = ctx;
	}
}

static void *dma_4u_alloc_coherent(struct device *dev, size_t size,
				   dma_addr_t *dma_addrp, gfp_t gfp)
{
	unsigned long flags, order, first_page;
	struct iommu *iommu;
	struct page *page;
	int npages, nid;
	iopte_t *iopte;
	void *ret;

	size = IO_PAGE_ALIGN(size);
	order = get_order(size);
	if (order >= 10)
		return NULL;

	nid = dev->archdata.numa_node;
	page = alloc_pages_node(nid, gfp, order);
	if (unlikely(!page))
		return NULL;

	first_page = (unsigned long) page_address(page);
	memset((char *)first_page, 0, PAGE_SIZE << order);

	iommu = dev->archdata.iommu;

	spin_lock_irqsave(&iommu->lock, flags);
	iopte = alloc_npages(dev, iommu, size >> IO_PAGE_SHIFT);
	spin_unlock_irqrestore(&iommu->lock, flags);

	if (unlikely(iopte == NULL)) {
		free_pages(first_page, order);
		return NULL;
	}

	*dma_addrp = (iommu->page_table_map_base +
		      ((iopte - iommu->page_table) << IO_PAGE_SHIFT));
	ret = (void *) first_page;
	npages = size >> IO_PAGE_SHIFT;
	first_page = __pa(first_page);
	while (npages--) {
		iopte_val(*iopte) = (IOPTE_CONSISTENT(0UL) |
				     IOPTE_WRITE |
				     (first_page & IOPTE_PAGE));
		iopte++;
		first_page += IO_PAGE_SIZE;
	}

	return ret;
}

static void dma_4u_free_coherent(struct device *dev, size_t size,
				 void *cpu, dma_addr_t dvma)
{
	struct iommu *iommu;
	iopte_t *iopte;
	unsigned long flags, order, npages;

	npages = IO_PAGE_ALIGN(size) >> IO_PAGE_SHIFT;
	iommu = dev->archdata.iommu;
	iopte = iommu->page_table +
		((dvma - iommu->page_table_map_base) >> IO_PAGE_SHIFT);

	spin_lock_irqsave(&iommu->lock, flags);

	iommu_range_free(iommu, dvma, npages);

	spin_unlock_irqrestore(&iommu->lock, flags);

	order = get_order(size);
	if (order < 10)
		free_pages((unsigned long)cpu, order);
}

static dma_addr_t dma_4u_map_single(struct device *dev, void *ptr, size_t sz,
				    enum dma_data_direction direction)
{
	struct iommu *iommu;
	struct strbuf *strbuf;
	iopte_t *base;
	unsigned long flags, npages, oaddr;
	unsigned long i, base_paddr, ctx;
	u32 bus_addr, ret;
	unsigned long iopte_protection;

	iommu = dev->archdata.iommu;
	strbuf = dev->archdata.stc;

	if (unlikely(direction == DMA_NONE))
		goto bad_no_ctx;

	oaddr = (unsigned long)ptr;
	npages = IO_PAGE_ALIGN(oaddr + sz) - (oaddr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;

	spin_lock_irqsave(&iommu->lock, flags);
	base = alloc_npages(dev, iommu, npages);
	ctx = 0;
	if (iommu->iommu_ctxflush)
		ctx = iommu_alloc_ctx(iommu);
	spin_unlock_irqrestore(&iommu->lock, flags);

	if (unlikely(!base))
		goto bad;

	bus_addr = (iommu->page_table_map_base +
		    ((base - iommu->page_table) << IO_PAGE_SHIFT));
	ret = bus_addr | (oaddr & ~IO_PAGE_MASK);
	base_paddr = __pa(oaddr & IO_PAGE_MASK);
	if (strbuf->strbuf_enabled)
		iopte_protection = IOPTE_STREAMING(ctx);
	else
		iopte_protection = IOPTE_CONSISTENT(ctx);
	if (direction != DMA_TO_DEVICE)
		iopte_protection |= IOPTE_WRITE;

	for (i = 0; i < npages; i++, base++, base_paddr += IO_PAGE_SIZE)
		iopte_val(*base) = iopte_protection | base_paddr;

	return ret;

bad:
	iommu_free_ctx(iommu, ctx);
bad_no_ctx:
	if (printk_ratelimit())
		WARN_ON(1);
	return DMA_ERROR_CODE;
}

static void strbuf_flush(struct strbuf *strbuf, struct iommu *iommu,
			 u32 vaddr, unsigned long ctx, unsigned long npages,
			 enum dma_data_direction direction)
{
	int limit;

	if (strbuf->strbuf_ctxflush &&
	    iommu->iommu_ctxflush) {
		unsigned long matchreg, flushreg;
		u64 val;

		flushreg = strbuf->strbuf_ctxflush;
		matchreg = STC_CTXMATCH_ADDR(strbuf, ctx);

		iommu_write(flushreg, ctx);
		val = iommu_read(matchreg);
		val &= 0xffff;
		if (!val)
			goto do_flush_sync;

		while (val) {
			if (val & 0x1)
				iommu_write(flushreg, ctx);
			val >>= 1;
		}
		val = iommu_read(matchreg);
		if (unlikely(val)) {
			printk(KERN_WARNING "strbuf_flush: ctx flush "
			       "timeout matchreg[%llx] ctx[%lx]\n",
			       val, ctx);
			goto do_page_flush;
		}
	} else {
		unsigned long i;

	do_page_flush:
		for (i = 0; i < npages; i++, vaddr += IO_PAGE_SIZE)
			iommu_write(strbuf->strbuf_pflush, vaddr);
	}

do_flush_sync:
	/* If the device could not have possibly put dirty data into
	 * the streaming cache, no flush-flag synchronization needs
	 * to be performed.
	 */
	if (direction == DMA_TO_DEVICE)
		return;

	STC_FLUSHFLAG_INIT(strbuf);
	iommu_write(strbuf->strbuf_fsync, strbuf->strbuf_flushflag_pa);
	(void) iommu_read(iommu->write_complete_reg);

	limit = 100000;
	while (!STC_FLUSHFLAG_SET(strbuf)) {
		limit--;
		if (!limit)
			break;
		udelay(1);
		rmb();
	}
	if (!limit)
		printk(KERN_WARNING "strbuf_flush: flushflag timeout "
		       "vaddr[%08x] ctx[%lx] npages[%ld]\n",
		       vaddr, ctx, npages);
}

static void dma_4u_unmap_single(struct device *dev, dma_addr_t bus_addr,
				size_t sz, enum dma_data_direction direction)
{
	struct iommu *iommu;
	struct strbuf *strbuf;
	iopte_t *base;
	unsigned long flags, npages, ctx, i;

	if (unlikely(direction == DMA_NONE)) {
		if (printk_ratelimit())
			WARN_ON(1);
		return;
	}

	iommu = dev->archdata.iommu;
	strbuf = dev->archdata.stc;

	npages = IO_PAGE_ALIGN(bus_addr + sz) - (bus_addr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;
	base = iommu->page_table +
		((bus_addr - iommu->page_table_map_base) >> IO_PAGE_SHIFT);
	bus_addr &= IO_PAGE_MASK;

	spin_lock_irqsave(&iommu->lock, flags);

	/* Record the context, if any. */
	ctx = 0;
	if (iommu->iommu_ctxflush)
		ctx = (iopte_val(*base) & IOPTE_CONTEXT) >> 47UL;

	/* Step 1: Kick data out of streaming buffers if necessary. */
	if (strbuf->strbuf_enabled)
		strbuf_flush(strbuf, iommu, bus_addr, ctx,
			     npages, direction);

	/* Step 2: Clear out TSB entries. */
	for (i = 0; i < npages; i++)
		iopte_make_dummy(iommu, base + i);

	iommu_range_free(iommu, bus_addr, npages);

	iommu_free_ctx(iommu, ctx);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

static int dma_4u_map_sg(struct device *dev, struct scatterlist *sglist,
			 int nelems, enum dma_data_direction direction)
{
	struct scatterlist *s, *outs, *segstart;
	unsigned long flags, handle, prot, ctx;
	dma_addr_t dma_next = 0, dma_addr;
	unsigned int max_seg_size;
	unsigned long seg_boundary_size;
	int outcount, incount, i;
	struct strbuf *strbuf;
	struct iommu *iommu;
	unsigned long base_shift;

	BUG_ON(direction == DMA_NONE);

	iommu = dev->archdata.iommu;
	strbuf = dev->archdata.stc;
	if (nelems == 0 || !iommu)
		return 0;

	spin_lock_irqsave(&iommu->lock, flags);

	ctx = 0;
	if (iommu->iommu_ctxflush)
		ctx = iommu_alloc_ctx(iommu);

	if (strbuf->strbuf_enabled)
		prot = IOPTE_STREAMING(ctx);
	else
		prot = IOPTE_CONSISTENT(ctx);
	if (direction != DMA_TO_DEVICE)
		prot |= IOPTE_WRITE;

	outs = s = segstart = &sglist[0];
	outcount = 1;
	incount = nelems;
	handle = 0;

	/* Init first segment length for backout at failure */
	outs->dma_length = 0;

	max_seg_size = dma_get_max_seg_size(dev);
	seg_boundary_size = ALIGN(dma_get_seg_boundary(dev) + 1,
				  IO_PAGE_SIZE) >> IO_PAGE_SHIFT;
	base_shift = iommu->page_table_map_base >> IO_PAGE_SHIFT;
	for_each_sg(sglist, s, nelems, i) {
		unsigned long paddr, npages, entry, out_entry = 0, slen;
		iopte_t *base;

		slen = s->length;
		/* Sanity check */
		if (slen == 0) {
			dma_next = 0;
			continue;
		}
		/* Allocate iommu entries for that segment */
		paddr = (unsigned long) SG_ENT_PHYS_ADDRESS(s);
		npages = iommu_num_pages(paddr, slen, IO_PAGE_SIZE);
		entry = iommu_range_alloc(dev, iommu, npages, &handle);

		/* Handle failure */
		if (unlikely(entry == DMA_ERROR_CODE)) {
			if (printk_ratelimit())
				printk(KERN_INFO "iommu_alloc failed, iommu %p paddr %lx"
				       " npages %lx\n", iommu, paddr, npages);
			goto iommu_map_failed;
		}

		base = iommu->page_table + entry;

		/* Convert entry to a dma_addr_t */
		dma_addr = iommu->page_table_map_base +
			(entry << IO_PAGE_SHIFT);
		dma_addr |= (s->offset & ~IO_PAGE_MASK);

		/* Insert into HW table */
		paddr &= IO_PAGE_MASK;
		while (npages--) {
			iopte_val(*base) = prot | paddr;
			base++;
			paddr += IO_PAGE_SIZE;
		}

		/* If we are in an open segment, try merging */
		if (segstart != s) {
			/* We cannot merge if:
			 * - allocated dma_addr isn't contiguous to previous allocation
			 */
			if ((dma_addr != dma_next) ||
			    (outs->dma_length + s->length > max_seg_size) ||
			    (is_span_boundary(out_entry, base_shift,
					      seg_boundary_size, outs, s))) {
				/* Can't merge: create a new segment */
				segstart = s;
				outcount++;
				outs = sg_next(outs);
			} else {
				outs->dma_length += s->length;
			}
		}

		if (segstart == s) {
			/* This is a new segment, fill entries */
			outs->dma_address = dma_addr;
			outs->dma_length = slen;
			out_entry = entry;
		}

		/* Calculate next page pointer for contiguous check */
		dma_next = dma_addr + slen;
	}

	spin_unlock_irqrestore(&iommu->lock, flags);

	if (outcount < incount) {
		outs = sg_next(outs);
		outs->dma_address = DMA_ERROR_CODE;
		outs->dma_length = 0;
	}

	return outcount;

iommu_map_failed:
	for_each_sg(sglist, s, nelems, i) {
		if (s->dma_length != 0) {
			unsigned long vaddr, npages, entry, j;
			iopte_t *base;

			vaddr = s->dma_address & IO_PAGE_MASK;
			npages = iommu_num_pages(s->dma_address, s->dma_length,
						 IO_PAGE_SIZE);
			iommu_range_free(iommu, vaddr, npages);

			entry = (vaddr - iommu->page_table_map_base)
				>> IO_PAGE_SHIFT;
			base = iommu->page_table + entry;

			for (j = 0; j < npages; j++)
				iopte_make_dummy(iommu, base + j);

			s->dma_address = DMA_ERROR_CODE;
			s->dma_length = 0;
		}
		if (s == outs)
			break;
	}
	spin_unlock_irqrestore(&iommu->lock, flags);

	return 0;
}

/* If contexts are being used, they are the same in all of the mappings
 * we make for a particular SG.
 */
static unsigned long fetch_sg_ctx(struct iommu *iommu, struct scatterlist *sg)
{
	unsigned long ctx = 0;

	if (iommu->iommu_ctxflush) {
		iopte_t *base;
		u32 bus_addr;

		bus_addr = sg->dma_address & IO_PAGE_MASK;
		base = iommu->page_table +
			((bus_addr - iommu->page_table_map_base) >> IO_PAGE_SHIFT);

		ctx = (iopte_val(*base) & IOPTE_CONTEXT) >> 47UL;
	}
	return ctx;
}

static void dma_4u_unmap_sg(struct device *dev, struct scatterlist *sglist,
			    int nelems, enum dma_data_direction direction)
{
	unsigned long flags, ctx;
	struct scatterlist *sg;
	struct strbuf *strbuf;
	struct iommu *iommu;

	BUG_ON(direction == DMA_NONE);

	iommu = dev->archdata.iommu;
	strbuf = dev->archdata.stc;

	ctx = fetch_sg_ctx(iommu, sglist);

	spin_lock_irqsave(&iommu->lock, flags);

	sg = sglist;
	while (nelems--) {
		dma_addr_t dma_handle = sg->dma_address;
		unsigned int len = sg->dma_length;
		unsigned long npages, entry;
		iopte_t *base;
		int i;

		if (!len)
			break;
		npages = iommu_num_pages(dma_handle, len, IO_PAGE_SIZE);
		iommu_range_free(iommu, dma_handle, npages);

		entry = ((dma_handle - iommu->page_table_map_base)
			 >> IO_PAGE_SHIFT);
		base = iommu->page_table + entry;

		dma_handle &= IO_PAGE_MASK;
		if (strbuf->strbuf_enabled)
			strbuf_flush(strbuf, iommu, dma_handle, ctx,
				     npages, direction);

		for (i = 0; i < npages; i++)
			iopte_make_dummy(iommu, base + i);

		sg = sg_next(sg);
	}

	iommu_free_ctx(iommu, ctx);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

static void dma_4u_sync_single_for_cpu(struct device *dev,
				       dma_addr_t bus_addr, size_t sz,
				       enum dma_data_direction direction)
{
	struct iommu *iommu;
	struct strbuf *strbuf;
	unsigned long flags, ctx, npages;

	iommu = dev->archdata.iommu;
	strbuf = dev->archdata.stc;

	if (!strbuf->strbuf_enabled)
		return;

	spin_lock_irqsave(&iommu->lock, flags);

	npages = IO_PAGE_ALIGN(bus_addr + sz) - (bus_addr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;
	bus_addr &= IO_PAGE_MASK;

	/* Step 1: Record the context, if any. */
	ctx = 0;
	if (iommu->iommu_ctxflush &&
	    strbuf->strbuf_ctxflush) {
		iopte_t *iopte;

		iopte = iommu->page_table +
			((bus_addr - iommu->page_table_map_base)>>IO_PAGE_SHIFT);
		ctx = (iopte_val(*iopte) & IOPTE_CONTEXT) >> 47UL;
	}

	/* Step 2: Kick data out of streaming buffers. */
	strbuf_flush(strbuf, iommu, bus_addr, ctx, npages, direction);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

static void dma_4u_sync_sg_for_cpu(struct device *dev,
				   struct scatterlist *sglist, int nelems,
				   enum dma_data_direction direction)
{
	struct iommu *iommu;
	struct strbuf *strbuf;
	unsigned long flags, ctx, npages, i;
	struct scatterlist *sg, *sgprv;
	u32 bus_addr;

	iommu = dev->archdata.iommu;
	strbuf = dev->archdata.stc;

	if (!strbuf->strbuf_enabled)
		return;

	spin_lock_irqsave(&iommu->lock, flags);

	/* Step 1: Record the context, if any. */
	ctx = 0;
	if (iommu->iommu_ctxflush &&
	    strbuf->strbuf_ctxflush) {
		iopte_t *iopte;

		iopte = iommu->page_table +
			((sglist[0].dma_address - iommu->page_table_map_base) >> IO_PAGE_SHIFT);
		ctx = (iopte_val(*iopte) & IOPTE_CONTEXT) >> 47UL;
	}

	/* Step 2: Kick data out of streaming buffers. */
	bus_addr = sglist[0].dma_address & IO_PAGE_MASK;
	sgprv = NULL;
	for_each_sg(sglist, sg, nelems, i) {
		if (sg->dma_length == 0)
			break;
		sgprv = sg;
	}

	npages = (IO_PAGE_ALIGN(sgprv->dma_address + sgprv->dma_length)
		  - bus_addr) >> IO_PAGE_SHIFT;
	strbuf_flush(strbuf, iommu, bus_addr, ctx, npages, direction);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

static const struct dma_ops sun4u_dma_ops = {
	.alloc_coherent		= dma_4u_alloc_coherent,
	.free_coherent		= dma_4u_free_coherent,
	.map_single		= dma_4u_map_single,
	.unmap_single		= dma_4u_unmap_single,
	.map_sg			= dma_4u_map_sg,
	.unmap_sg		= dma_4u_unmap_sg,
	.sync_single_for_cpu	= dma_4u_sync_single_for_cpu,
	.sync_sg_for_cpu	= dma_4u_sync_sg_for_cpu,
};

const struct dma_ops *dma_ops = &sun4u_dma_ops;
EXPORT_SYMBOL(dma_ops);

int dma_supported(struct device *dev, u64 device_mask)
{
	struct iommu *iommu = dev->archdata.iommu;
	u64 dma_addr_mask = iommu->dma_addr_mask;

	if (device_mask >= (1UL << 32UL))
		return 0;

	if ((device_mask & dma_addr_mask) == dma_addr_mask)
		return 1;

#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type)
		return pci_dma_supported(to_pci_dev(dev), device_mask);
#endif

	return 0;
}
EXPORT_SYMBOL(dma_supported);

int dma_set_mask(struct device *dev, u64 dma_mask)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type)
		return pci_set_dma_mask(to_pci_dev(dev), dma_mask);
#endif
	return -EINVAL;
}
EXPORT_SYMBOL(dma_set_mask);
