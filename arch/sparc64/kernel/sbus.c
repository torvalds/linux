/* $Id: sbus.c,v 1.19 2002/01/23 11:27:32 davem Exp $
 * sbus.c: UltraSparc SBUS controller support.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/page.h>
#include <asm/sbus.h>
#include <asm/io.h>
#include <asm/upa.h>
#include <asm/cache.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/starfire.h>

#include "iommu_common.h"

#define MAP_BASE	((u32)0xc0000000)

struct sbus_info {
	struct iommu	iommu;
	struct strbuf	strbuf;
};

/* Offsets from iommu_regs */
#define SYSIO_IOMMUREG_BASE	0x2400UL
#define IOMMU_CONTROL	(0x2400UL - 0x2400UL)	/* IOMMU control register */
#define IOMMU_TSBBASE	(0x2408UL - 0x2400UL)	/* TSB base address register */
#define IOMMU_FLUSH	(0x2410UL - 0x2400UL)	/* IOMMU flush register */
#define IOMMU_VADIAG	(0x4400UL - 0x2400UL)	/* SBUS virtual address diagnostic */
#define IOMMU_TAGCMP	(0x4408UL - 0x2400UL)	/* TLB tag compare diagnostics */
#define IOMMU_LRUDIAG	(0x4500UL - 0x2400UL)	/* IOMMU LRU queue diagnostics */
#define IOMMU_TAGDIAG	(0x4580UL - 0x2400UL)	/* TLB tag diagnostics */
#define IOMMU_DRAMDIAG	(0x4600UL - 0x2400UL)	/* TLB data RAM diagnostics */

#define IOMMU_DRAM_VALID	(1UL << 30UL)

static void __iommu_flushall(struct iommu *iommu)
{
	unsigned long tag;
	int entry;

	tag = iommu->iommu_control + (IOMMU_TAGDIAG - IOMMU_CONTROL);
	for (entry = 0; entry < 16; entry++) {
		upa_writeq(0, tag);
		tag += 8UL;
	}
	upa_readq(iommu->write_complete_reg);
}

/* Offsets from strbuf_regs */
#define SYSIO_STRBUFREG_BASE	0x2800UL
#define STRBUF_CONTROL	(0x2800UL - 0x2800UL)	/* Control */
#define STRBUF_PFLUSH	(0x2808UL - 0x2800UL)	/* Page flush/invalidate */
#define STRBUF_FSYNC	(0x2810UL - 0x2800UL)	/* Flush synchronization */
#define STRBUF_DRAMDIAG	(0x5000UL - 0x2800UL)	/* data RAM diagnostic */
#define STRBUF_ERRDIAG	(0x5400UL - 0x2800UL)	/* error status diagnostics */
#define STRBUF_PTAGDIAG	(0x5800UL - 0x2800UL)	/* Page tag diagnostics */
#define STRBUF_LTAGDIAG	(0x5900UL - 0x2800UL)	/* Line tag diagnostics */

#define STRBUF_TAG_VALID	0x02UL

static void sbus_strbuf_flush(struct iommu *iommu, struct strbuf *strbuf, u32 base, unsigned long npages, int direction)
{
	unsigned long n;
	int limit;

	n = npages;
	while (n--)
		upa_writeq(base + (n << IO_PAGE_SHIFT), strbuf->strbuf_pflush);

	/* If the device could not have possibly put dirty data into
	 * the streaming cache, no flush-flag synchronization needs
	 * to be performed.
	 */
	if (direction == SBUS_DMA_TODEVICE)
		return;

	*(strbuf->strbuf_flushflag) = 0UL;

	/* Whoopee cushion! */
	upa_writeq(strbuf->strbuf_flushflag_pa, strbuf->strbuf_fsync);
	upa_readq(iommu->write_complete_reg);

	limit = 100000;
	while (*(strbuf->strbuf_flushflag) == 0UL) {
		limit--;
		if (!limit)
			break;
		udelay(1);
		rmb();
	}
	if (!limit)
		printk(KERN_WARNING "sbus_strbuf_flush: flushflag timeout "
		       "vaddr[%08x] npages[%ld]\n",
		       base, npages);
}

/* Based largely upon the ppc64 iommu allocator.  */
static long sbus_arena_alloc(struct iommu *iommu, unsigned long npages)
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

static void sbus_arena_free(struct iommu_arena *arena, unsigned long base, unsigned long npages)
{
	unsigned long i;

	for (i = base; i < (base + npages); i++)
		__clear_bit(i, arena->map);
}

static void sbus_iommu_table_init(struct iommu *iommu, unsigned int tsbsize)
{
	unsigned long tsbbase, order, sz, num_tsb_entries;

	num_tsb_entries = tsbsize / sizeof(iopte_t);

	/* Setup initial software IOMMU state. */
	spin_lock_init(&iommu->lock);
	iommu->page_table_map_base = MAP_BASE;

	/* Allocate and initialize the free area map.  */
	sz = num_tsb_entries / 8;
	sz = (sz + 7UL) & ~7UL;
	iommu->arena.map = kzalloc(sz, GFP_KERNEL);
	if (!iommu->arena.map) {
		prom_printf("SBUS_IOMMU: Error, kmalloc(arena.map) failed.\n");
		prom_halt();
	}
	iommu->arena.limit = num_tsb_entries;

	/* Now allocate and setup the IOMMU page table itself.  */
	order = get_order(tsbsize);
	tsbbase = __get_free_pages(GFP_KERNEL, order);
	if (!tsbbase) {
		prom_printf("IOMMU: Error, gfp(tsb) failed.\n");
		prom_halt();
	}
	iommu->page_table = (iopte_t *)tsbbase;
	memset(iommu->page_table, 0, tsbsize);
}

static inline iopte_t *alloc_npages(struct iommu *iommu, unsigned long npages)
{
	long entry;

	entry = sbus_arena_alloc(iommu, npages);
	if (unlikely(entry < 0))
		return NULL;

	return iommu->page_table + entry;
}

static inline void free_npages(struct iommu *iommu, dma_addr_t base, unsigned long npages)
{
	sbus_arena_free(&iommu->arena, base >> IO_PAGE_SHIFT, npages);
}

void *sbus_alloc_consistent(struct sbus_dev *sdev, size_t size, dma_addr_t *dvma_addr)
{
	struct sbus_info *info;
	struct iommu *iommu;
	iopte_t *iopte;
	unsigned long flags, order, first_page;
	void *ret;
	int npages;

	size = IO_PAGE_ALIGN(size);
	order = get_order(size);
	if (order >= 10)
		return NULL;

	first_page = __get_free_pages(GFP_KERNEL|__GFP_COMP, order);
	if (first_page == 0UL)
		return NULL;
	memset((char *)first_page, 0, PAGE_SIZE << order);

	info = sdev->bus->iommu;
	iommu = &info->iommu;

	spin_lock_irqsave(&iommu->lock, flags);
	iopte = alloc_npages(iommu, size >> IO_PAGE_SHIFT);
	spin_unlock_irqrestore(&iommu->lock, flags);

	if (unlikely(iopte == NULL)) {
		free_pages(first_page, order);
		return NULL;
	}

	*dvma_addr = (iommu->page_table_map_base +
		      ((iopte - iommu->page_table) << IO_PAGE_SHIFT));
	ret = (void *) first_page;
	npages = size >> IO_PAGE_SHIFT;
	first_page = __pa(first_page);
	while (npages--) {
		iopte_val(*iopte) = (IOPTE_VALID | IOPTE_CACHE |
				     IOPTE_WRITE |
				     (first_page & IOPTE_PAGE));
		iopte++;
		first_page += IO_PAGE_SIZE;
	}

	return ret;
}

void sbus_free_consistent(struct sbus_dev *sdev, size_t size, void *cpu, dma_addr_t dvma)
{
	struct sbus_info *info;
	struct iommu *iommu;
	iopte_t *iopte;
	unsigned long flags, order, npages;

	npages = IO_PAGE_ALIGN(size) >> IO_PAGE_SHIFT;
	info = sdev->bus->iommu;
	iommu = &info->iommu;
	iopte = iommu->page_table +
		((dvma - iommu->page_table_map_base) >> IO_PAGE_SHIFT);

	spin_lock_irqsave(&iommu->lock, flags);

	free_npages(iommu, dvma - iommu->page_table_map_base, npages);

	spin_unlock_irqrestore(&iommu->lock, flags);

	order = get_order(size);
	if (order < 10)
		free_pages((unsigned long)cpu, order);
}

dma_addr_t sbus_map_single(struct sbus_dev *sdev, void *ptr, size_t sz, int direction)
{
	struct sbus_info *info;
	struct iommu *iommu;
	iopte_t *base;
	unsigned long flags, npages, oaddr;
	unsigned long i, base_paddr;
	u32 bus_addr, ret;
	unsigned long iopte_protection;

	info = sdev->bus->iommu;
	iommu = &info->iommu;

	if (unlikely(direction == SBUS_DMA_NONE))
		BUG();

	oaddr = (unsigned long)ptr;
	npages = IO_PAGE_ALIGN(oaddr + sz) - (oaddr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;

	spin_lock_irqsave(&iommu->lock, flags);
	base = alloc_npages(iommu, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);

	if (unlikely(!base))
		BUG();

	bus_addr = (iommu->page_table_map_base +
		    ((base - iommu->page_table) << IO_PAGE_SHIFT));
	ret = bus_addr | (oaddr & ~IO_PAGE_MASK);
	base_paddr = __pa(oaddr & IO_PAGE_MASK);

	iopte_protection = IOPTE_VALID | IOPTE_STBUF | IOPTE_CACHE;
	if (direction != SBUS_DMA_TODEVICE)
		iopte_protection |= IOPTE_WRITE;

	for (i = 0; i < npages; i++, base++, base_paddr += IO_PAGE_SIZE)
		iopte_val(*base) = iopte_protection | base_paddr;

	return ret;
}

void sbus_unmap_single(struct sbus_dev *sdev, dma_addr_t bus_addr, size_t sz, int direction)
{
	struct sbus_info *info = sdev->bus->iommu;
	struct iommu *iommu = &info->iommu;
	struct strbuf *strbuf = &info->strbuf;
	iopte_t *base;
	unsigned long flags, npages, i;

	if (unlikely(direction == SBUS_DMA_NONE))
		BUG();

	npages = IO_PAGE_ALIGN(bus_addr + sz) - (bus_addr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;
	base = iommu->page_table +
		((bus_addr - iommu->page_table_map_base) >> IO_PAGE_SHIFT);

	bus_addr &= IO_PAGE_MASK;

	spin_lock_irqsave(&iommu->lock, flags);
	sbus_strbuf_flush(iommu, strbuf, bus_addr, npages, direction);
	for (i = 0; i < npages; i++)
		iopte_val(base[i]) = 0UL;
	free_npages(iommu, bus_addr - iommu->page_table_map_base, npages);
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

int sbus_map_sg(struct sbus_dev *sdev, struct scatterlist *sglist, int nelems, int direction)
{
	struct sbus_info *info;
	struct iommu *iommu;
	unsigned long flags, npages, iopte_protection;
	iopte_t *base;
	u32 dma_base;
	struct scatterlist *sgtmp;
	int used;

	/* Fast path single entry scatterlists. */
	if (nelems == 1) {
		sglist->dma_address =
			sbus_map_single(sdev,
					(page_address(sglist->page) + sglist->offset),
					sglist->length, direction);
		sglist->dma_length = sglist->length;
		return 1;
	}

	info = sdev->bus->iommu;
	iommu = &info->iommu;

	if (unlikely(direction == SBUS_DMA_NONE))
		BUG();

	npages = prepare_sg(sglist, nelems);

	spin_lock_irqsave(&iommu->lock, flags);
	base = alloc_npages(iommu, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);

	if (unlikely(base == NULL))
		BUG();

	dma_base = iommu->page_table_map_base +
		((base - iommu->page_table) << IO_PAGE_SHIFT);

	/* Normalize DVMA addresses. */
	used = nelems;

	sgtmp = sglist;
	while (used && sgtmp->dma_length) {
		sgtmp->dma_address += dma_base;
		sgtmp++;
		used--;
	}
	used = nelems - used;

	iopte_protection = IOPTE_VALID | IOPTE_STBUF | IOPTE_CACHE;
	if (direction != SBUS_DMA_TODEVICE)
		iopte_protection |= IOPTE_WRITE;

	fill_sg(base, sglist, used, nelems, iopte_protection);

#ifdef VERIFY_SG
	verify_sglist(sglist, nelems, base, npages);
#endif

	return used;
}

void sbus_unmap_sg(struct sbus_dev *sdev, struct scatterlist *sglist, int nelems, int direction)
{
	struct sbus_info *info;
	struct iommu *iommu;
	struct strbuf *strbuf;
	iopte_t *base;
	unsigned long flags, i, npages;
	u32 bus_addr;

	if (unlikely(direction == SBUS_DMA_NONE))
		BUG();

	info = sdev->bus->iommu;
	iommu = &info->iommu;
	strbuf = &info->strbuf;

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
	sbus_strbuf_flush(iommu, strbuf, bus_addr, npages, direction);
	for (i = 0; i < npages; i++)
		iopte_val(base[i]) = 0UL;
	free_npages(iommu, bus_addr - iommu->page_table_map_base, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);
}

void sbus_dma_sync_single_for_cpu(struct sbus_dev *sdev, dma_addr_t bus_addr, size_t sz, int direction)
{
	struct sbus_info *info;
	struct iommu *iommu;
	struct strbuf *strbuf;
	unsigned long flags, npages;

	info = sdev->bus->iommu;
	iommu = &info->iommu;
	strbuf = &info->strbuf;

	npages = IO_PAGE_ALIGN(bus_addr + sz) - (bus_addr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;
	bus_addr &= IO_PAGE_MASK;

	spin_lock_irqsave(&iommu->lock, flags);
	sbus_strbuf_flush(iommu, strbuf, bus_addr, npages, direction);
	spin_unlock_irqrestore(&iommu->lock, flags);
}

void sbus_dma_sync_single_for_device(struct sbus_dev *sdev, dma_addr_t base, size_t size, int direction)
{
}

void sbus_dma_sync_sg_for_cpu(struct sbus_dev *sdev, struct scatterlist *sglist, int nelems, int direction)
{
	struct sbus_info *info;
	struct iommu *iommu;
	struct strbuf *strbuf;
	unsigned long flags, npages, i;
	u32 bus_addr;

	info = sdev->bus->iommu;
	iommu = &info->iommu;
	strbuf = &info->strbuf;

	bus_addr = sglist[0].dma_address & IO_PAGE_MASK;
	for (i = 0; i < nelems; i++) {
		if (!sglist[i].dma_length)
			break;
	}
	i--;
	npages = (IO_PAGE_ALIGN(sglist[i].dma_address + sglist[i].dma_length)
		  - bus_addr) >> IO_PAGE_SHIFT;

	spin_lock_irqsave(&iommu->lock, flags);
	sbus_strbuf_flush(iommu, strbuf, bus_addr, npages, direction);
	spin_unlock_irqrestore(&iommu->lock, flags);
}

void sbus_dma_sync_sg_for_device(struct sbus_dev *sdev, struct scatterlist *sg, int nents, int direction)
{
}

/* Enable 64-bit DVMA mode for the given device. */
void sbus_set_sbus64(struct sbus_dev *sdev, int bursts)
{
	struct sbus_info *info = sdev->bus->iommu;
	struct iommu *iommu = &info->iommu;
	int slot = sdev->slot;
	unsigned long cfg_reg;
	u64 val;

	cfg_reg = iommu->write_complete_reg;
	switch (slot) {
	case 0:
		cfg_reg += 0x20UL;
		break;
	case 1:
		cfg_reg += 0x28UL;
		break;
	case 2:
		cfg_reg += 0x30UL;
		break;
	case 3:
		cfg_reg += 0x38UL;
		break;
	case 13:
		cfg_reg += 0x40UL;
		break;
	case 14:
		cfg_reg += 0x48UL;
		break;
	case 15:
		cfg_reg += 0x50UL;
		break;

	default:
		return;
	};

	val = upa_readq(cfg_reg);
	if (val & (1UL << 14UL)) {
		/* Extended transfer mode already enabled. */
		return;
	}

	val |= (1UL << 14UL);

	if (bursts & DMA_BURST8)
		val |= (1UL << 1UL);
	if (bursts & DMA_BURST16)
		val |= (1UL << 2UL);
	if (bursts & DMA_BURST32)
		val |= (1UL << 3UL);
	if (bursts & DMA_BURST64)
		val |= (1UL << 4UL);
	upa_writeq(val, cfg_reg);
}

/* INO number to IMAP register offset for SYSIO external IRQ's.
 * This should conform to both Sunfire/Wildfire server and Fusion
 * desktop designs.
 */
#define SYSIO_IMAP_SLOT0	0x2c00UL
#define SYSIO_IMAP_SLOT1	0x2c08UL
#define SYSIO_IMAP_SLOT2	0x2c10UL
#define SYSIO_IMAP_SLOT3	0x2c18UL
#define SYSIO_IMAP_SCSI		0x3000UL
#define SYSIO_IMAP_ETH		0x3008UL
#define SYSIO_IMAP_BPP		0x3010UL
#define SYSIO_IMAP_AUDIO	0x3018UL
#define SYSIO_IMAP_PFAIL	0x3020UL
#define SYSIO_IMAP_KMS		0x3028UL
#define SYSIO_IMAP_FLPY		0x3030UL
#define SYSIO_IMAP_SHW		0x3038UL
#define SYSIO_IMAP_KBD		0x3040UL
#define SYSIO_IMAP_MS		0x3048UL
#define SYSIO_IMAP_SER		0x3050UL
#define SYSIO_IMAP_TIM0		0x3060UL
#define SYSIO_IMAP_TIM1		0x3068UL
#define SYSIO_IMAP_UE		0x3070UL
#define SYSIO_IMAP_CE		0x3078UL
#define SYSIO_IMAP_SBERR	0x3080UL
#define SYSIO_IMAP_PMGMT	0x3088UL
#define SYSIO_IMAP_GFX		0x3090UL
#define SYSIO_IMAP_EUPA		0x3098UL

#define bogon     ((unsigned long) -1)
static unsigned long sysio_irq_offsets[] = {
	/* SBUS Slot 0 --> 3, level 1 --> 7 */
	SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0,
	SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0,
	SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1,
	SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1,
	SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2,
	SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2,
	SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3,
	SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3,

	/* Onboard devices (not relevant/used on SunFire). */
	SYSIO_IMAP_SCSI,
	SYSIO_IMAP_ETH,
	SYSIO_IMAP_BPP,
	bogon,
	SYSIO_IMAP_AUDIO,
	SYSIO_IMAP_PFAIL,
	bogon,
	bogon,
	SYSIO_IMAP_KMS,
	SYSIO_IMAP_FLPY,
	SYSIO_IMAP_SHW,
	SYSIO_IMAP_KBD,
	SYSIO_IMAP_MS,
	SYSIO_IMAP_SER,
	bogon,
	bogon,
	SYSIO_IMAP_TIM0,
	SYSIO_IMAP_TIM1,
	bogon,
	bogon,
	SYSIO_IMAP_UE,
	SYSIO_IMAP_CE,
	SYSIO_IMAP_SBERR,
	SYSIO_IMAP_PMGMT,
};

#undef bogon

#define NUM_SYSIO_OFFSETS ARRAY_SIZE(sysio_irq_offsets)

/* Convert Interrupt Mapping register pointer to associated
 * Interrupt Clear register pointer, SYSIO specific version.
 */
#define SYSIO_ICLR_UNUSED0	0x3400UL
#define SYSIO_ICLR_SLOT0	0x3408UL
#define SYSIO_ICLR_SLOT1	0x3448UL
#define SYSIO_ICLR_SLOT2	0x3488UL
#define SYSIO_ICLR_SLOT3	0x34c8UL
static unsigned long sysio_imap_to_iclr(unsigned long imap)
{
	unsigned long diff = SYSIO_ICLR_UNUSED0 - SYSIO_IMAP_SLOT0;
	return imap + diff;
}

unsigned int sbus_build_irq(void *buscookie, unsigned int ino)
{
	struct sbus_bus *sbus = (struct sbus_bus *)buscookie;
	struct sbus_info *info = sbus->iommu;
	struct iommu *iommu = &info->iommu;
	unsigned long reg_base = iommu->write_complete_reg - 0x2000UL;
	unsigned long imap, iclr;
	int sbus_level = 0;

	imap = sysio_irq_offsets[ino];
	if (imap == ((unsigned long)-1)) {
		prom_printf("get_irq_translations: Bad SYSIO INO[%x]\n",
			    ino);
		prom_halt();
	}
	imap += reg_base;

	/* SYSIO inconsistency.  For external SLOTS, we have to select
	 * the right ICLR register based upon the lower SBUS irq level
	 * bits.
	 */
	if (ino >= 0x20) {
		iclr = sysio_imap_to_iclr(imap);
	} else {
		int sbus_slot = (ino & 0x18)>>3;
		
		sbus_level = ino & 0x7;

		switch(sbus_slot) {
		case 0:
			iclr = reg_base + SYSIO_ICLR_SLOT0;
			break;
		case 1:
			iclr = reg_base + SYSIO_ICLR_SLOT1;
			break;
		case 2:
			iclr = reg_base + SYSIO_ICLR_SLOT2;
			break;
		default:
		case 3:
			iclr = reg_base + SYSIO_ICLR_SLOT3;
			break;
		};

		iclr += ((unsigned long)sbus_level - 1UL) * 8UL;
	}
	return build_irq(sbus_level, iclr, imap);
}

/* Error interrupt handling. */
#define SYSIO_UE_AFSR	0x0030UL
#define SYSIO_UE_AFAR	0x0038UL
#define  SYSIO_UEAFSR_PPIO  0x8000000000000000UL /* Primary PIO cause         */
#define  SYSIO_UEAFSR_PDRD  0x4000000000000000UL /* Primary DVMA read cause   */
#define  SYSIO_UEAFSR_PDWR  0x2000000000000000UL /* Primary DVMA write cause  */
#define  SYSIO_UEAFSR_SPIO  0x1000000000000000UL /* Secondary PIO is cause    */
#define  SYSIO_UEAFSR_SDRD  0x0800000000000000UL /* Secondary DVMA read cause */
#define  SYSIO_UEAFSR_SDWR  0x0400000000000000UL /* Secondary DVMA write cause*/
#define  SYSIO_UEAFSR_RESV1 0x03ff000000000000UL /* Reserved                  */
#define  SYSIO_UEAFSR_DOFF  0x0000e00000000000UL /* Doubleword Offset         */
#define  SYSIO_UEAFSR_SIZE  0x00001c0000000000UL /* Bad transfer size 2^SIZE  */
#define  SYSIO_UEAFSR_MID   0x000003e000000000UL /* UPA MID causing the fault */
#define  SYSIO_UEAFSR_RESV2 0x0000001fffffffffUL /* Reserved                  */
static irqreturn_t sysio_ue_handler(int irq, void *dev_id)
{
	struct sbus_bus *sbus = dev_id;
	struct sbus_info *info = sbus->iommu;
	struct iommu *iommu = &info->iommu;
	unsigned long reg_base = iommu->write_complete_reg - 0x2000UL;
	unsigned long afsr_reg, afar_reg;
	unsigned long afsr, afar, error_bits;
	int reported;

	afsr_reg = reg_base + SYSIO_UE_AFSR;
	afar_reg = reg_base + SYSIO_UE_AFAR;

	/* Latch error status. */
	afsr = upa_readq(afsr_reg);
	afar = upa_readq(afar_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SYSIO_UEAFSR_PPIO | SYSIO_UEAFSR_PDRD | SYSIO_UEAFSR_PDWR |
		 SYSIO_UEAFSR_SPIO | SYSIO_UEAFSR_SDRD | SYSIO_UEAFSR_SDWR);
	upa_writeq(error_bits, afsr_reg);

	/* Log the error. */
	printk("SYSIO[%x]: Uncorrectable ECC Error, primary error type[%s]\n",
	       sbus->portid,
	       (((error_bits & SYSIO_UEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & SYSIO_UEAFSR_PDRD) ?
		  "DVMA Read" :
		  ((error_bits & SYSIO_UEAFSR_PDWR) ?
		   "DVMA Write" : "???")))));
	printk("SYSIO[%x]: DOFF[%lx] SIZE[%lx] MID[%lx]\n",
	       sbus->portid,
	       (afsr & SYSIO_UEAFSR_DOFF) >> 45UL,
	       (afsr & SYSIO_UEAFSR_SIZE) >> 42UL,
	       (afsr & SYSIO_UEAFSR_MID) >> 37UL);
	printk("SYSIO[%x]: AFAR[%016lx]\n", sbus->portid, afar);
	printk("SYSIO[%x]: Secondary UE errors [", sbus->portid);
	reported = 0;
	if (afsr & SYSIO_UEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & SYSIO_UEAFSR_SDRD) {
		reported++;
		printk("(DVMA Read)");
	}
	if (afsr & SYSIO_UEAFSR_SDWR) {
		reported++;
		printk("(DVMA Write)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	return IRQ_HANDLED;
}

#define SYSIO_CE_AFSR	0x0040UL
#define SYSIO_CE_AFAR	0x0048UL
#define  SYSIO_CEAFSR_PPIO  0x8000000000000000UL /* Primary PIO cause         */
#define  SYSIO_CEAFSR_PDRD  0x4000000000000000UL /* Primary DVMA read cause   */
#define  SYSIO_CEAFSR_PDWR  0x2000000000000000UL /* Primary DVMA write cause  */
#define  SYSIO_CEAFSR_SPIO  0x1000000000000000UL /* Secondary PIO cause       */
#define  SYSIO_CEAFSR_SDRD  0x0800000000000000UL /* Secondary DVMA read cause */
#define  SYSIO_CEAFSR_SDWR  0x0400000000000000UL /* Secondary DVMA write cause*/
#define  SYSIO_CEAFSR_RESV1 0x0300000000000000UL /* Reserved                  */
#define  SYSIO_CEAFSR_ESYND 0x00ff000000000000UL /* Syndrome Bits             */
#define  SYSIO_CEAFSR_DOFF  0x0000e00000000000UL /* Double Offset             */
#define  SYSIO_CEAFSR_SIZE  0x00001c0000000000UL /* Bad transfer size 2^SIZE  */
#define  SYSIO_CEAFSR_MID   0x000003e000000000UL /* UPA MID causing the fault */
#define  SYSIO_CEAFSR_RESV2 0x0000001fffffffffUL /* Reserved                  */
static irqreturn_t sysio_ce_handler(int irq, void *dev_id)
{
	struct sbus_bus *sbus = dev_id;
	struct sbus_info *info = sbus->iommu;
	struct iommu *iommu = &info->iommu;
	unsigned long reg_base = iommu->write_complete_reg - 0x2000UL;
	unsigned long afsr_reg, afar_reg;
	unsigned long afsr, afar, error_bits;
	int reported;

	afsr_reg = reg_base + SYSIO_CE_AFSR;
	afar_reg = reg_base + SYSIO_CE_AFAR;

	/* Latch error status. */
	afsr = upa_readq(afsr_reg);
	afar = upa_readq(afar_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SYSIO_CEAFSR_PPIO | SYSIO_CEAFSR_PDRD | SYSIO_CEAFSR_PDWR |
		 SYSIO_CEAFSR_SPIO | SYSIO_CEAFSR_SDRD | SYSIO_CEAFSR_SDWR);
	upa_writeq(error_bits, afsr_reg);

	printk("SYSIO[%x]: Correctable ECC Error, primary error type[%s]\n",
	       sbus->portid,
	       (((error_bits & SYSIO_CEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & SYSIO_CEAFSR_PDRD) ?
		  "DVMA Read" :
		  ((error_bits & SYSIO_CEAFSR_PDWR) ?
		   "DVMA Write" : "???")))));

	/* XXX Use syndrome and afar to print out module string just like
	 * XXX UDB CE trap handler does... -DaveM
	 */
	printk("SYSIO[%x]: DOFF[%lx] ECC Syndrome[%lx] Size[%lx] MID[%lx]\n",
	       sbus->portid,
	       (afsr & SYSIO_CEAFSR_DOFF) >> 45UL,
	       (afsr & SYSIO_CEAFSR_ESYND) >> 48UL,
	       (afsr & SYSIO_CEAFSR_SIZE) >> 42UL,
	       (afsr & SYSIO_CEAFSR_MID) >> 37UL);
	printk("SYSIO[%x]: AFAR[%016lx]\n", sbus->portid, afar);

	printk("SYSIO[%x]: Secondary CE errors [", sbus->portid);
	reported = 0;
	if (afsr & SYSIO_CEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & SYSIO_CEAFSR_SDRD) {
		reported++;
		printk("(DVMA Read)");
	}
	if (afsr & SYSIO_CEAFSR_SDWR) {
		reported++;
		printk("(DVMA Write)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	return IRQ_HANDLED;
}

#define SYSIO_SBUS_AFSR		0x2010UL
#define SYSIO_SBUS_AFAR		0x2018UL
#define  SYSIO_SBAFSR_PLE   0x8000000000000000UL /* Primary Late PIO Error    */
#define  SYSIO_SBAFSR_PTO   0x4000000000000000UL /* Primary SBUS Timeout      */
#define  SYSIO_SBAFSR_PBERR 0x2000000000000000UL /* Primary SBUS Error ACK    */
#define  SYSIO_SBAFSR_SLE   0x1000000000000000UL /* Secondary Late PIO Error  */
#define  SYSIO_SBAFSR_STO   0x0800000000000000UL /* Secondary SBUS Timeout    */
#define  SYSIO_SBAFSR_SBERR 0x0400000000000000UL /* Secondary SBUS Error ACK  */
#define  SYSIO_SBAFSR_RESV1 0x03ff000000000000UL /* Reserved                  */
#define  SYSIO_SBAFSR_RD    0x0000800000000000UL /* Primary was late PIO read */
#define  SYSIO_SBAFSR_RESV2 0x0000600000000000UL /* Reserved                  */
#define  SYSIO_SBAFSR_SIZE  0x00001c0000000000UL /* Size of transfer          */
#define  SYSIO_SBAFSR_MID   0x000003e000000000UL /* MID causing the error     */
#define  SYSIO_SBAFSR_RESV3 0x0000001fffffffffUL /* Reserved                  */
static irqreturn_t sysio_sbus_error_handler(int irq, void *dev_id)
{
	struct sbus_bus *sbus = dev_id;
	struct sbus_info *info = sbus->iommu;
	struct iommu *iommu = &info->iommu;
	unsigned long afsr_reg, afar_reg, reg_base;
	unsigned long afsr, afar, error_bits;
	int reported;

	reg_base = iommu->write_complete_reg - 0x2000UL;
	afsr_reg = reg_base + SYSIO_SBUS_AFSR;
	afar_reg = reg_base + SYSIO_SBUS_AFAR;

	afsr = upa_readq(afsr_reg);
	afar = upa_readq(afar_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SYSIO_SBAFSR_PLE | SYSIO_SBAFSR_PTO | SYSIO_SBAFSR_PBERR |
		 SYSIO_SBAFSR_SLE | SYSIO_SBAFSR_STO | SYSIO_SBAFSR_SBERR);
	upa_writeq(error_bits, afsr_reg);

	/* Log the error. */
	printk("SYSIO[%x]: SBUS Error, primary error type[%s] read(%d)\n",
	       sbus->portid,
	       (((error_bits & SYSIO_SBAFSR_PLE) ?
		 "Late PIO Error" :
		 ((error_bits & SYSIO_SBAFSR_PTO) ?
		  "Time Out" :
		  ((error_bits & SYSIO_SBAFSR_PBERR) ?
		   "Error Ack" : "???")))),
	       (afsr & SYSIO_SBAFSR_RD) ? 1 : 0);
	printk("SYSIO[%x]: size[%lx] MID[%lx]\n",
	       sbus->portid,
	       (afsr & SYSIO_SBAFSR_SIZE) >> 42UL,
	       (afsr & SYSIO_SBAFSR_MID) >> 37UL);
	printk("SYSIO[%x]: AFAR[%016lx]\n", sbus->portid, afar);
	printk("SYSIO[%x]: Secondary SBUS errors [", sbus->portid);
	reported = 0;
	if (afsr & SYSIO_SBAFSR_SLE) {
		reported++;
		printk("(Late PIO Error)");
	}
	if (afsr & SYSIO_SBAFSR_STO) {
		reported++;
		printk("(Time Out)");
	}
	if (afsr & SYSIO_SBAFSR_SBERR) {
		reported++;
		printk("(Error Ack)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	/* XXX check iommu/strbuf for further error status XXX */

	return IRQ_HANDLED;
}

#define ECC_CONTROL	0x0020UL
#define  SYSIO_ECNTRL_ECCEN	0x8000000000000000UL /* Enable ECC Checking   */
#define  SYSIO_ECNTRL_UEEN	0x4000000000000000UL /* Enable UE Interrupts  */
#define  SYSIO_ECNTRL_CEEN	0x2000000000000000UL /* Enable CE Interrupts  */

#define SYSIO_UE_INO		0x34
#define SYSIO_CE_INO		0x35
#define SYSIO_SBUSERR_INO	0x36

static void __init sysio_register_error_handlers(struct sbus_bus *sbus)
{
	struct sbus_info *info = sbus->iommu;
	struct iommu *iommu = &info->iommu;
	unsigned long reg_base = iommu->write_complete_reg - 0x2000UL;
	unsigned int irq;
	u64 control;

	irq = sbus_build_irq(sbus, SYSIO_UE_INO);
	if (request_irq(irq, sysio_ue_handler, 0,
			"SYSIO_UE", sbus) < 0) {
		prom_printf("SYSIO[%x]: Cannot register UE interrupt.\n",
			    sbus->portid);
		prom_halt();
	}

	irq = sbus_build_irq(sbus, SYSIO_CE_INO);
	if (request_irq(irq, sysio_ce_handler, 0,
			"SYSIO_CE", sbus) < 0) {
		prom_printf("SYSIO[%x]: Cannot register CE interrupt.\n",
			    sbus->portid);
		prom_halt();
	}

	irq = sbus_build_irq(sbus, SYSIO_SBUSERR_INO);
	if (request_irq(irq, sysio_sbus_error_handler, 0,
			"SYSIO_SBERR", sbus) < 0) {
		prom_printf("SYSIO[%x]: Cannot register SBUS Error interrupt.\n",
			    sbus->portid);
		prom_halt();
	}

	/* Now turn the error interrupts on and also enable ECC checking. */
	upa_writeq((SYSIO_ECNTRL_ECCEN |
		    SYSIO_ECNTRL_UEEN  |
		    SYSIO_ECNTRL_CEEN),
		   reg_base + ECC_CONTROL);

	control = upa_readq(iommu->write_complete_reg);
	control |= 0x100UL; /* SBUS Error Interrupt Enable */
	upa_writeq(control, iommu->write_complete_reg);
}

/* Boot time initialization. */
static void __init sbus_iommu_init(int __node, struct sbus_bus *sbus)
{
	const struct linux_prom64_registers *pr;
	struct device_node *dp;
	struct sbus_info *info;
	struct iommu *iommu;
	struct strbuf *strbuf;
	unsigned long regs, reg_base;
	u64 control;
	int i;

	dp = of_find_node_by_phandle(__node);

	sbus->portid = of_getintprop_default(dp, "upa-portid", -1);

	pr = of_get_property(dp, "reg", NULL);
	if (!pr) {
		prom_printf("sbus_iommu_init: Cannot map SYSIO control registers.\n");
		prom_halt();
	}
	regs = pr->phys_addr;

	info = kzalloc(sizeof(*info), GFP_ATOMIC);
	if (info == NULL) {
		prom_printf("sbus_iommu_init: Fatal error, "
			    "kmalloc(info) failed\n");
		prom_halt();
	}

	iommu = &info->iommu;
	strbuf = &info->strbuf;

	reg_base = regs + SYSIO_IOMMUREG_BASE;
	iommu->iommu_control = reg_base + IOMMU_CONTROL;
	iommu->iommu_tsbbase = reg_base + IOMMU_TSBBASE;
	iommu->iommu_flush = reg_base + IOMMU_FLUSH;

	reg_base = regs + SYSIO_STRBUFREG_BASE;
	strbuf->strbuf_control = reg_base + STRBUF_CONTROL;
	strbuf->strbuf_pflush = reg_base + STRBUF_PFLUSH;
	strbuf->strbuf_fsync = reg_base + STRBUF_FSYNC;

	strbuf->strbuf_enabled = 1;

	strbuf->strbuf_flushflag = (volatile unsigned long *)
		((((unsigned long)&strbuf->__flushflag_buf[0])
		  + 63UL)
		 & ~63UL);
	strbuf->strbuf_flushflag_pa = (unsigned long)
		__pa(strbuf->strbuf_flushflag);

	/* The SYSIO SBUS control register is used for dummy reads
	 * in order to ensure write completion.
	 */
	iommu->write_complete_reg = regs + 0x2000UL;

	/* Link into SYSIO software state. */
	sbus->iommu = info;

	printk("SYSIO: UPA portID %x, at %016lx\n",
	       sbus->portid, regs);

	/* Setup for TSB_SIZE=7, TBW_SIZE=0, MMU_DE=1, MMU_EN=1 */
	sbus_iommu_table_init(iommu, IO_TSB_SIZE);

	control = upa_readq(iommu->iommu_control);
	control = ((7UL << 16UL)	|
		   (0UL << 2UL)		|
		   (1UL << 1UL)		|
		   (1UL << 0UL));
	upa_writeq(control, iommu->iommu_control);

	/* Clean out any cruft in the IOMMU using
	 * diagnostic accesses.
	 */
	for (i = 0; i < 16; i++) {
		unsigned long dram, tag;

		dram = iommu->iommu_control + (IOMMU_DRAMDIAG - IOMMU_CONTROL);
		tag = iommu->iommu_control + (IOMMU_TAGDIAG - IOMMU_CONTROL);

		dram += (unsigned long)i * 8UL;
		tag += (unsigned long)i * 8UL;
		upa_writeq(0, dram);
		upa_writeq(0, tag);
	}
	upa_readq(iommu->write_complete_reg);

	/* Give the TSB to SYSIO. */
	upa_writeq(__pa(iommu->page_table), iommu->iommu_tsbbase);

	/* Setup streaming buffer, DE=1 SB_EN=1 */
	control = (1UL << 1UL) | (1UL << 0UL);
	upa_writeq(control, strbuf->strbuf_control);

	/* Clear out the tags using diagnostics. */
	for (i = 0; i < 16; i++) {
		unsigned long ptag, ltag;

		ptag = strbuf->strbuf_control +
			(STRBUF_PTAGDIAG - STRBUF_CONTROL);
		ltag = strbuf->strbuf_control +
			(STRBUF_LTAGDIAG - STRBUF_CONTROL);
		ptag += (unsigned long)i * 8UL;
		ltag += (unsigned long)i * 8UL;

		upa_writeq(0UL, ptag);
		upa_writeq(0UL, ltag);
	}

	/* Enable DVMA arbitration for all devices/slots. */
	control = upa_readq(iommu->write_complete_reg);
	control |= 0x3fUL;
	upa_writeq(control, iommu->write_complete_reg);

	/* Now some Xfire specific grot... */
	if (this_is_starfire)
		starfire_hookup(sbus->portid);

	sysio_register_error_handlers(sbus);
}

void sbus_fill_device_irq(struct sbus_dev *sdev)
{
	struct device_node *dp = of_find_node_by_phandle(sdev->prom_node);
	const struct linux_prom_irqs *irqs;

	irqs = of_get_property(dp, "interrupts", NULL);
	if (!irqs) {
		sdev->irqs[0] = 0;
		sdev->num_irqs = 0;
	} else {
		unsigned int pri = irqs[0].pri;

		sdev->num_irqs = 1;
		if (pri < 0x20)
			pri += sdev->slot * 8;

		sdev->irqs[0] =	sbus_build_irq(sdev->bus, pri);
	}
}

void __init sbus_arch_bus_ranges_init(struct device_node *pn, struct sbus_bus *sbus)
{
}

void __init sbus_setup_iommu(struct sbus_bus *sbus, struct device_node *dp)
{
	sbus_iommu_init(dp->node, sbus);
}

void __init sbus_setup_arch_props(struct sbus_bus *sbus, struct device_node *dp)
{
}

int __init sbus_arch_preinit(void)
{
	return 0;
}

void __init sbus_arch_postinit(void)
{
	extern void firetruck_init(void);

	firetruck_init();
}
