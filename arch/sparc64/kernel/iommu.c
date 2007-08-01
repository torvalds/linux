/* iommu.c: Generic sparc64 IOMMU support.
 *
 * Copyright (C) 1999, 2007 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1999, 2000 Jakub Jelinek (jakub@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>

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
static void __iommu_flushall(struct iommu *iommu)
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

/* Based largely upon the ppc64 iommu allocator.  */
static long arena_alloc(struct iommu *iommu, unsigned long npages)
{
	struct iommu_arena *arena = &iommu->arena;
	unsigned long n, i, start, end, limit;
	int pass;

	limit = arena->limit;
	start = arena->hint;
	pass = 0;

again:
	n = find_next_zero_bit(arena->map, limit, start);
	end = n + npages;
	if (unlikely(end >= limit)) {
		if (likely(pass < 1)) {
			limit = start;
			start = 0;
			__iommu_flushall(iommu);
			pass++;
			goto again;
		} else {
			/* Scanned the whole thing, give up. */
			return -1;
		}
	}

	for (i = n; i < end; i++) {
		if (test_bit(i, arena->map)) {
			start = i + 1;
			goto again;
		}
	}

	for (i = n; i < end; i++)
		__set_bit(i, arena->map);

	arena->hint = end;

	return n;
}

static void arena_free(struct iommu_arena *arena, unsigned long base, unsigned long npages)
{
	unsigned long i;

	for (i = base; i < (base + npages); i++)
		__clear_bit(i, arena->map);
}

int iommu_table_init(struct iommu *iommu, int tsbsize,
		     u32 dma_offset, u32 dma_addr_mask)
{
	unsigned long i, tsbbase, order, sz, num_tsb_entries;

	num_tsb_entries = tsbsize / sizeof(iopte_t);

	/* Setup initial software IOMMU state. */
	spin_lock_init(&iommu->lock);
	iommu->ctx_lowest_free = 1;
	iommu->page_table_map_base = dma_offset;
	iommu->dma_addr_mask = dma_addr_mask;

	/* Allocate and initialize the free area map.  */
	sz = num_tsb_entries / 8;
	sz = (sz + 7UL) & ~7UL;
	iommu->arena.map = kzalloc(sz, GFP_KERNEL);
	if (!iommu->arena.map) {
		printk(KERN_ERR "IOMMU: Error, kmalloc(arena.map) failed.\n");
		return -ENOMEM;
	}
	iommu->arena.limit = num_tsb_entries;

	/* Allocate and initialize the dummy page which we
	 * set inactive IO PTEs to point to.
	 */
	iommu->dummy_page = __get_free_pages(GFP_KERNEL, 0);
	if (!iommu->dummy_page) {
		printk(KERN_ERR "IOMMU: Error, gfp(dummy_page) failed.\n");
		goto out_free_map;
	}
	memset((void *)iommu->dummy_page, 0, PAGE_SIZE);
	iommu->dummy_page_pa = (unsigned long) __pa(iommu->dummy_page);

	/* Now allocate and setup the IOMMU page table itself.  */
	order = get_order(tsbsize);
	tsbbase = __get_free_pages(GFP_KERNEL, order);
	if (!tsbbase) {
		printk(KERN_ERR "IOMMU: Error, gfp(tsb) failed.\n");
		goto out_free_dummy_page;
	}
	iommu->page_table = (iopte_t *)tsbbase;

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

static inline iopte_t *alloc_npages(struct iommu *iommu, unsigned long npages)
{
	long entry;

	entry = arena_alloc(iommu, npages);
	if (unlikely(entry < 0))
		return NULL;

	return iommu->page_table + entry;
}

static inline void free_npages(struct iommu *iommu, dma_addr_t base, unsigned long npages)
{
	arena_free(&iommu->arena, base >> IO_PAGE_SHIFT, npages);
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
	struct iommu *iommu;
	iopte_t *iopte;
	unsigned long flags, order, first_page;
	void *ret;
	int npages;

	size = IO_PAGE_ALIGN(size);
	order = get_order(size);
	if (order >= 10)
		return NULL;

	first_page = __get_free_pages(gfp, order);
	if (first_page == 0UL)
		return NULL;
	memset((char *)first_page, 0, PAGE_SIZE << order);

	iommu = dev->archdata.iommu;

	spin_lock_irqsave(&iommu->lock, flags);
	iopte = alloc_npages(iommu, size >> IO_PAGE_SHIFT);
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

	free_npages(iommu, dvma - iommu->page_table_map_base, npages);

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
	base = alloc_npages(iommu, npages);
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
			       "timeout matchreg[%lx] ctx[%lx]\n",
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

	free_npages(iommu, bus_addr - iommu->page_table_map_base, npages);

	iommu_free_ctx(iommu, ctx);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

#define SG_ENT_PHYS_ADDRESS(SG)	\
	(__pa(page_address((SG)->page)) + (SG)->offset)

static inline void fill_sg(iopte_t *iopte, struct scatterlist *sg,
			   int nused, int nelems,
			   unsigned long iopte_protection)
{
	struct scatterlist *dma_sg = sg;
	struct scatterlist *sg_end = sg + nelems;
	int i;

	for (i = 0; i < nused; i++) {
		unsigned long pteval = ~0UL;
		u32 dma_npages;

		dma_npages = ((dma_sg->dma_address & (IO_PAGE_SIZE - 1UL)) +
			      dma_sg->dma_length +
			      ((IO_PAGE_SIZE - 1UL))) >> IO_PAGE_SHIFT;
		do {
			unsigned long offset;
			signed int len;

			/* If we are here, we know we have at least one
			 * more page to map.  So walk forward until we
			 * hit a page crossing, and begin creating new
			 * mappings from that spot.
			 */
			for (;;) {
				unsigned long tmp;

				tmp = SG_ENT_PHYS_ADDRESS(sg);
				len = sg->length;
				if (((tmp ^ pteval) >> IO_PAGE_SHIFT) != 0UL) {
					pteval = tmp & IO_PAGE_MASK;
					offset = tmp & (IO_PAGE_SIZE - 1UL);
					break;
				}
				if (((tmp ^ (tmp + len - 1UL)) >> IO_PAGE_SHIFT) != 0UL) {
					pteval = (tmp + IO_PAGE_SIZE) & IO_PAGE_MASK;
					offset = 0UL;
					len -= (IO_PAGE_SIZE - (tmp & (IO_PAGE_SIZE - 1UL)));
					break;
				}
				sg++;
			}

			pteval = iopte_protection | (pteval & IOPTE_PAGE);
			while (len > 0) {
				*iopte++ = __iopte(pteval);
				pteval += IO_PAGE_SIZE;
				len -= (IO_PAGE_SIZE - offset);
				offset = 0;
				dma_npages--;
			}

			pteval = (pteval & IOPTE_PAGE) + len;
			sg++;

			/* Skip over any tail mappings we've fully mapped,
			 * adjusting pteval along the way.  Stop when we
			 * detect a page crossing event.
			 */
			while (sg < sg_end &&
			       (pteval << (64 - IO_PAGE_SHIFT)) != 0UL &&
			       (pteval == SG_ENT_PHYS_ADDRESS(sg)) &&
			       ((pteval ^
				 (SG_ENT_PHYS_ADDRESS(sg) + sg->length - 1UL)) >> IO_PAGE_SHIFT) == 0UL) {
				pteval += sg->length;
				sg++;
			}
			if ((pteval << (64 - IO_PAGE_SHIFT)) == 0UL)
				pteval = ~0UL;
		} while (dma_npages != 0);
		dma_sg++;
	}
}

static int dma_4u_map_sg(struct device *dev, struct scatterlist *sglist,
			 int nelems, enum dma_data_direction direction)
{
	struct iommu *iommu;
	struct strbuf *strbuf;
	unsigned long flags, ctx, npages, iopte_protection;
	iopte_t *base;
	u32 dma_base;
	struct scatterlist *sgtmp;
	int used;

	/* Fast path single entry scatterlists. */
	if (nelems == 1) {
		sglist->dma_address =
			dma_4u_map_single(dev,
					  (page_address(sglist->page) +
					   sglist->offset),
					  sglist->length, direction);
		if (unlikely(sglist->dma_address == DMA_ERROR_CODE))
			return 0;
		sglist->dma_length = sglist->length;
		return 1;
	}

	iommu = dev->archdata.iommu;
	strbuf = dev->archdata.stc;

	if (unlikely(direction == DMA_NONE))
		goto bad_no_ctx;

	/* Step 1: Prepare scatter list. */

	npages = prepare_sg(sglist, nelems);

	/* Step 2: Allocate a cluster and context, if necessary. */

	spin_lock_irqsave(&iommu->lock, flags);

	base = alloc_npages(iommu, npages);
	ctx = 0;
	if (iommu->iommu_ctxflush)
		ctx = iommu_alloc_ctx(iommu);

	spin_unlock_irqrestore(&iommu->lock, flags);

	if (base == NULL)
		goto bad;

	dma_base = iommu->page_table_map_base +
		((base - iommu->page_table) << IO_PAGE_SHIFT);

	/* Step 3: Normalize DMA addresses. */
	used = nelems;

	sgtmp = sglist;
	while (used && sgtmp->dma_length) {
		sgtmp->dma_address += dma_base;
		sgtmp++;
		used--;
	}
	used = nelems - used;

	/* Step 4: Create the mappings. */
	if (strbuf->strbuf_enabled)
		iopte_protection = IOPTE_STREAMING(ctx);
	else
		iopte_protection = IOPTE_CONSISTENT(ctx);
	if (direction != DMA_TO_DEVICE)
		iopte_protection |= IOPTE_WRITE;

	fill_sg(base, sglist, used, nelems, iopte_protection);

#ifdef VERIFY_SG
	verify_sglist(sglist, nelems, base, npages);
#endif

	return used;

bad:
	iommu_free_ctx(iommu, ctx);
bad_no_ctx:
	if (printk_ratelimit())
		WARN_ON(1);
	return 0;
}

static void dma_4u_unmap_sg(struct device *dev, struct scatterlist *sglist,
			    int nelems, enum dma_data_direction direction)
{
	struct iommu *iommu;
	struct strbuf *strbuf;
	iopte_t *base;
	unsigned long flags, ctx, i, npages;
	u32 bus_addr;

	if (unlikely(direction == DMA_NONE)) {
		if (printk_ratelimit())
			WARN_ON(1);
	}

	iommu = dev->archdata.iommu;
	strbuf = dev->archdata.stc;

	bus_addr = sglist->dma_address & IO_PAGE_MASK;

	for (i = 1; i < nelems; i++)
		if (sglist[i].dma_length == 0)
			break;
	i--;
	npages = (IO_PAGE_ALIGN(sglist[i].dma_address + sglist[i].dma_length) -
		  bus_addr) >> IO_PAGE_SHIFT;

	base = iommu->page_table +
		((bus_addr - iommu->page_table_map_base) >> IO_PAGE_SHIFT);

	spin_lock_irqsave(&iommu->lock, flags);

	/* Record the context, if any. */
	ctx = 0;
	if (iommu->iommu_ctxflush)
		ctx = (iopte_val(*base) & IOPTE_CONTEXT) >> 47UL;

	/* Step 1: Kick data out of streaming buffers if necessary. */
	if (strbuf->strbuf_enabled)
		strbuf_flush(strbuf, iommu, bus_addr, ctx, npages, direction);

	/* Step 2: Clear out the TSB entries. */
	for (i = 0; i < npages; i++)
		iopte_make_dummy(iommu, base + i);

	free_npages(iommu, bus_addr - iommu->page_table_map_base, npages);

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
	for(i = 1; i < nelems; i++)
		if (!sglist[i].dma_length)
			break;
	i--;
	npages = (IO_PAGE_ALIGN(sglist[i].dma_address + sglist[i].dma_length)
		  - bus_addr) >> IO_PAGE_SHIFT;
	strbuf_flush(strbuf, iommu, bus_addr, ctx, npages, direction);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

const struct dma_ops sun4u_dma_ops = {
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
