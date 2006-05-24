/* $Id: pci_iommu.c,v 1.17 2001/12/17 07:05:09 davem Exp $
 * pci_iommu.c: UltraSparc PCI controller IOM/STC support.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 * Copyright (C) 1999, 2000 Jakub Jelinek (jakub@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/delay.h>

#include <asm/pbm.h>

#include "iommu_common.h"

#define PCI_STC_CTXMATCH_ADDR(STC, CTX)	\
	((STC)->strbuf_ctxmatch_base + ((CTX) << 3))

/* Accessing IOMMU and Streaming Buffer registers.
 * REG parameter is a physical address.  All registers
 * are 64-bits in size.
 */
#define pci_iommu_read(__reg) \
({	u64 __ret; \
	__asm__ __volatile__("ldxa [%1] %2, %0" \
			     : "=r" (__ret) \
			     : "r" (__reg), "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory"); \
	__ret; \
})
#define pci_iommu_write(__reg, __val) \
	__asm__ __volatile__("stxa %0, [%1] %2" \
			     : /* no outputs */ \
			     : "r" (__val), "r" (__reg), \
			       "i" (ASI_PHYS_BYPASS_EC_E))

/* Must be invoked under the IOMMU lock. */
static void __iommu_flushall(struct pci_iommu *iommu)
{
	unsigned long tag;
	int entry;

	tag = iommu->iommu_flush + (0xa580UL - 0x0210UL);
	for (entry = 0; entry < 16; entry++) {
		pci_iommu_write(tag, 0);
		tag += 8;
	}

	/* Ensure completion of previous PIO writes. */
	(void) pci_iommu_read(iommu->write_complete_reg);
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

static void inline iopte_make_dummy(struct pci_iommu *iommu, iopte_t *iopte)
{
	unsigned long val = iopte_val(*iopte);

	val &= ~IOPTE_PAGE;
	val |= iommu->dummy_page_pa;

	iopte_val(*iopte) = val;
}

/* Based largely upon the ppc64 iommu allocator.  */
static long pci_arena_alloc(struct pci_iommu *iommu, unsigned long npages)
{
	struct pci_iommu_arena *arena = &iommu->arena;
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

static void pci_arena_free(struct pci_iommu_arena *arena, unsigned long base, unsigned long npages)
{
	unsigned long i;

	for (i = base; i < (base + npages); i++)
		__clear_bit(i, arena->map);
}

void pci_iommu_table_init(struct pci_iommu *iommu, int tsbsize, u32 dma_offset, u32 dma_addr_mask)
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
		prom_printf("PCI_IOMMU: Error, kmalloc(arena.map) failed.\n");
		prom_halt();
	}
	iommu->arena.limit = num_tsb_entries;

	/* Allocate and initialize the dummy page which we
	 * set inactive IO PTEs to point to.
	 */
	iommu->dummy_page = __get_free_pages(GFP_KERNEL, 0);
	if (!iommu->dummy_page) {
		prom_printf("PCI_IOMMU: Error, gfp(dummy_page) failed.\n");
		prom_halt();
	}
	memset((void *)iommu->dummy_page, 0, PAGE_SIZE);
	iommu->dummy_page_pa = (unsigned long) __pa(iommu->dummy_page);

	/* Now allocate and setup the IOMMU page table itself.  */
	order = get_order(tsbsize);
	tsbbase = __get_free_pages(GFP_KERNEL, order);
	if (!tsbbase) {
		prom_printf("PCI_IOMMU: Error, gfp(tsb) failed.\n");
		prom_halt();
	}
	iommu->page_table = (iopte_t *)tsbbase;

	for (i = 0; i < num_tsb_entries; i++)
		iopte_make_dummy(iommu, &iommu->page_table[i]);
}

static inline iopte_t *alloc_npages(struct pci_iommu *iommu, unsigned long npages)
{
	long entry;

	entry = pci_arena_alloc(iommu, npages);
	if (unlikely(entry < 0))
		return NULL;

	return iommu->page_table + entry;
}

static inline void free_npages(struct pci_iommu *iommu, dma_addr_t base, unsigned long npages)
{
	pci_arena_free(&iommu->arena, base >> IO_PAGE_SHIFT, npages);
}

static int iommu_alloc_ctx(struct pci_iommu *iommu)
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

static inline void iommu_free_ctx(struct pci_iommu *iommu, int ctx)
{
	if (likely(ctx)) {
		__clear_bit(ctx, iommu->ctx_bitmap);
		if (ctx < iommu->ctx_lowest_free)
			iommu->ctx_lowest_free = ctx;
	}
}

/* Allocate and map kernel buffer of size SIZE using consistent mode
 * DMA for PCI device PDEV.  Return non-NULL cpu-side address if
 * successful and set *DMA_ADDRP to the PCI side dma address.
 */
static void *pci_4u_alloc_consistent(struct pci_dev *pdev, size_t size, dma_addr_t *dma_addrp, gfp_t gfp)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
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

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;

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

/* Free and unmap a consistent DMA translation. */
static void pci_4u_free_consistent(struct pci_dev *pdev, size_t size, void *cpu, dma_addr_t dvma)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	iopte_t *iopte;
	unsigned long flags, order, npages;

	npages = IO_PAGE_ALIGN(size) >> IO_PAGE_SHIFT;
	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	iopte = iommu->page_table +
		((dvma - iommu->page_table_map_base) >> IO_PAGE_SHIFT);

	spin_lock_irqsave(&iommu->lock, flags);

	free_npages(iommu, dvma, npages);

	spin_unlock_irqrestore(&iommu->lock, flags);

	order = get_order(size);
	if (order < 10)
		free_pages((unsigned long)cpu, order);
}

/* Map a single buffer at PTR of SZ bytes for PCI DMA
 * in streaming mode.
 */
static dma_addr_t pci_4u_map_single(struct pci_dev *pdev, void *ptr, size_t sz, int direction)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	struct pci_strbuf *strbuf;
	iopte_t *base;
	unsigned long flags, npages, oaddr;
	unsigned long i, base_paddr, ctx;
	u32 bus_addr, ret;
	unsigned long iopte_protection;

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	strbuf = &pcp->pbm->stc;

	if (unlikely(direction == PCI_DMA_NONE))
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
	if (direction != PCI_DMA_TODEVICE)
		iopte_protection |= IOPTE_WRITE;

	for (i = 0; i < npages; i++, base++, base_paddr += IO_PAGE_SIZE)
		iopte_val(*base) = iopte_protection | base_paddr;

	return ret;

bad:
	iommu_free_ctx(iommu, ctx);
bad_no_ctx:
	if (printk_ratelimit())
		WARN_ON(1);
	return PCI_DMA_ERROR_CODE;
}

static void pci_strbuf_flush(struct pci_strbuf *strbuf, struct pci_iommu *iommu, u32 vaddr, unsigned long ctx, unsigned long npages, int direction)
{
	int limit;

	if (strbuf->strbuf_ctxflush &&
	    iommu->iommu_ctxflush) {
		unsigned long matchreg, flushreg;
		u64 val;

		flushreg = strbuf->strbuf_ctxflush;
		matchreg = PCI_STC_CTXMATCH_ADDR(strbuf, ctx);

		pci_iommu_write(flushreg, ctx);
		val = pci_iommu_read(matchreg);
		val &= 0xffff;
		if (!val)
			goto do_flush_sync;

		while (val) {
			if (val & 0x1)
				pci_iommu_write(flushreg, ctx);
			val >>= 1;
		}
		val = pci_iommu_read(matchreg);
		if (unlikely(val)) {
			printk(KERN_WARNING "pci_strbuf_flush: ctx flush "
			       "timeout matchreg[%lx] ctx[%lx]\n",
			       val, ctx);
			goto do_page_flush;
		}
	} else {
		unsigned long i;

	do_page_flush:
		for (i = 0; i < npages; i++, vaddr += IO_PAGE_SIZE)
			pci_iommu_write(strbuf->strbuf_pflush, vaddr);
	}

do_flush_sync:
	/* If the device could not have possibly put dirty data into
	 * the streaming cache, no flush-flag synchronization needs
	 * to be performed.
	 */
	if (direction == PCI_DMA_TODEVICE)
		return;

	PCI_STC_FLUSHFLAG_INIT(strbuf);
	pci_iommu_write(strbuf->strbuf_fsync, strbuf->strbuf_flushflag_pa);
	(void) pci_iommu_read(iommu->write_complete_reg);

	limit = 100000;
	while (!PCI_STC_FLUSHFLAG_SET(strbuf)) {
		limit--;
		if (!limit)
			break;
		udelay(1);
		rmb();
	}
	if (!limit)
		printk(KERN_WARNING "pci_strbuf_flush: flushflag timeout "
		       "vaddr[%08x] ctx[%lx] npages[%ld]\n",
		       vaddr, ctx, npages);
}

/* Unmap a single streaming mode DMA translation. */
static void pci_4u_unmap_single(struct pci_dev *pdev, dma_addr_t bus_addr, size_t sz, int direction)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	struct pci_strbuf *strbuf;
	iopte_t *base;
	unsigned long flags, npages, ctx, i;

	if (unlikely(direction == PCI_DMA_NONE)) {
		if (printk_ratelimit())
			WARN_ON(1);
		return;
	}

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	strbuf = &pcp->pbm->stc;

	npages = IO_PAGE_ALIGN(bus_addr + sz) - (bus_addr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;
	base = iommu->page_table +
		((bus_addr - iommu->page_table_map_base) >> IO_PAGE_SHIFT);
#ifdef DEBUG_PCI_IOMMU
	if (IOPTE_IS_DUMMY(iommu, base))
		printk("pci_unmap_single called on non-mapped region %08x,%08x from %016lx\n",
		       bus_addr, sz, __builtin_return_address(0));
#endif
	bus_addr &= IO_PAGE_MASK;

	spin_lock_irqsave(&iommu->lock, flags);

	/* Record the context, if any. */
	ctx = 0;
	if (iommu->iommu_ctxflush)
		ctx = (iopte_val(*base) & IOPTE_CONTEXT) >> 47UL;

	/* Step 1: Kick data out of streaming buffers if necessary. */
	if (strbuf->strbuf_enabled)
		pci_strbuf_flush(strbuf, iommu, bus_addr, ctx,
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
			   int nused, int nelems, unsigned long iopte_protection)
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

/* Map a set of buffers described by SGLIST with NELEMS array
 * elements in streaming mode for PCI DMA.
 * When making changes here, inspect the assembly output. I was having
 * hard time to kepp this routine out of using stack slots for holding variables.
 */
static int pci_4u_map_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems, int direction)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	struct pci_strbuf *strbuf;
	unsigned long flags, ctx, npages, iopte_protection;
	iopte_t *base;
	u32 dma_base;
	struct scatterlist *sgtmp;
	int used;

	/* Fast path single entry scatterlists. */
	if (nelems == 1) {
		sglist->dma_address =
			pci_4u_map_single(pdev,
					  (page_address(sglist->page) + sglist->offset),
					  sglist->length, direction);
		if (unlikely(sglist->dma_address == PCI_DMA_ERROR_CODE))
			return 0;
		sglist->dma_length = sglist->length;
		return 1;
	}

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	strbuf = &pcp->pbm->stc;
	
	if (unlikely(direction == PCI_DMA_NONE))
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
	if (direction != PCI_DMA_TODEVICE)
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

/* Unmap a set of streaming mode DMA translations. */
static void pci_4u_unmap_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems, int direction)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	struct pci_strbuf *strbuf;
	iopte_t *base;
	unsigned long flags, ctx, i, npages;
	u32 bus_addr;

	if (unlikely(direction == PCI_DMA_NONE)) {
		if (printk_ratelimit())
			WARN_ON(1);
	}

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	strbuf = &pcp->pbm->stc;
	
	bus_addr = sglist->dma_address & IO_PAGE_MASK;

	for (i = 1; i < nelems; i++)
		if (sglist[i].dma_length == 0)
			break;
	i--;
	npages = (IO_PAGE_ALIGN(sglist[i].dma_address + sglist[i].dma_length) -
		  bus_addr) >> IO_PAGE_SHIFT;

	base = iommu->page_table +
		((bus_addr - iommu->page_table_map_base) >> IO_PAGE_SHIFT);

#ifdef DEBUG_PCI_IOMMU
	if (IOPTE_IS_DUMMY(iommu, base))
		printk("pci_unmap_sg called on non-mapped region %016lx,%d from %016lx\n", sglist->dma_address, nelems, __builtin_return_address(0));
#endif

	spin_lock_irqsave(&iommu->lock, flags);

	/* Record the context, if any. */
	ctx = 0;
	if (iommu->iommu_ctxflush)
		ctx = (iopte_val(*base) & IOPTE_CONTEXT) >> 47UL;

	/* Step 1: Kick data out of streaming buffers if necessary. */
	if (strbuf->strbuf_enabled)
		pci_strbuf_flush(strbuf, iommu, bus_addr, ctx, npages, direction);

	/* Step 2: Clear out the TSB entries. */
	for (i = 0; i < npages; i++)
		iopte_make_dummy(iommu, base + i);

	free_npages(iommu, bus_addr - iommu->page_table_map_base, npages);

	iommu_free_ctx(iommu, ctx);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

/* Make physical memory consistent for a single
 * streaming mode DMA translation after a transfer.
 */
static void pci_4u_dma_sync_single_for_cpu(struct pci_dev *pdev, dma_addr_t bus_addr, size_t sz, int direction)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	struct pci_strbuf *strbuf;
	unsigned long flags, ctx, npages;

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	strbuf = &pcp->pbm->stc;

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
	pci_strbuf_flush(strbuf, iommu, bus_addr, ctx, npages, direction);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

/* Make physical memory consistent for a set of streaming
 * mode DMA translations after a transfer.
 */
static void pci_4u_dma_sync_sg_for_cpu(struct pci_dev *pdev, struct scatterlist *sglist, int nelems, int direction)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	struct pci_strbuf *strbuf;
	unsigned long flags, ctx, npages, i;
	u32 bus_addr;

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	strbuf = &pcp->pbm->stc;

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
	pci_strbuf_flush(strbuf, iommu, bus_addr, ctx, npages, direction);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

struct pci_iommu_ops pci_sun4u_iommu_ops = {
	.alloc_consistent		= pci_4u_alloc_consistent,
	.free_consistent		= pci_4u_free_consistent,
	.map_single			= pci_4u_map_single,
	.unmap_single			= pci_4u_unmap_single,
	.map_sg				= pci_4u_map_sg,
	.unmap_sg			= pci_4u_unmap_sg,
	.dma_sync_single_for_cpu	= pci_4u_dma_sync_single_for_cpu,
	.dma_sync_sg_for_cpu		= pci_4u_dma_sync_sg_for_cpu,
};

static void ali_sound_dma_hack(struct pci_dev *pdev, int set_bit)
{
	struct pci_dev *ali_isa_bridge;
	u8 val;

	/* ALI sound chips generate 31-bits of DMA, a special register
	 * determines what bit 31 is emitted as.
	 */
	ali_isa_bridge = pci_get_device(PCI_VENDOR_ID_AL,
					 PCI_DEVICE_ID_AL_M1533,
					 NULL);

	pci_read_config_byte(ali_isa_bridge, 0x7e, &val);
	if (set_bit)
		val |= 0x01;
	else
		val &= ~0x01;
	pci_write_config_byte(ali_isa_bridge, 0x7e, val);
	pci_dev_put(ali_isa_bridge);
}

int pci_dma_supported(struct pci_dev *pdev, u64 device_mask)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	u64 dma_addr_mask;

	if (pdev == NULL) {
		dma_addr_mask = 0xffffffff;
	} else {
		struct pci_iommu *iommu = pcp->pbm->iommu;

		dma_addr_mask = iommu->dma_addr_mask;

		if (pdev->vendor == PCI_VENDOR_ID_AL &&
		    pdev->device == PCI_DEVICE_ID_AL_M5451 &&
		    device_mask == 0x7fffffff) {
			ali_sound_dma_hack(pdev,
					   (dma_addr_mask & 0x80000000) != 0);
			return 1;
		}
	}

	if (device_mask >= (1UL << 32UL))
		return 0;

	return (device_mask & dma_addr_mask) == dma_addr_mask;
}
