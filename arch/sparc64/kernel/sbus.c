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
#include <asm/starfire.h>

#include "iommu_common.h"

/* These should be allocated on an SMP_CACHE_BYTES
 * aligned boundary for optimal performance.
 *
 * On SYSIO, using an 8K page size we have 1GB of SBUS
 * DMA space mapped.  We divide this space into equally
 * sized clusters. We allocate a DMA mapping from the
 * cluster that matches the order of the allocation, or
 * if the order is greater than the number of clusters,
 * we try to allocate from the last cluster.
 */

#define NCLUSTERS	8UL
#define ONE_GIG		(1UL * 1024UL * 1024UL * 1024UL)
#define CLUSTER_SIZE	(ONE_GIG / NCLUSTERS)
#define CLUSTER_MASK	(CLUSTER_SIZE - 1)
#define CLUSTER_NPAGES	(CLUSTER_SIZE >> IO_PAGE_SHIFT)
#define MAP_BASE	((u32)0xc0000000)

struct sbus_iommu {
/*0x00*/spinlock_t		lock;

/*0x08*/iopte_t			*page_table;
/*0x10*/unsigned long		strbuf_regs;
/*0x18*/unsigned long		iommu_regs;
/*0x20*/unsigned long		sbus_control_reg;

/*0x28*/volatile unsigned long	strbuf_flushflag;

	/* If NCLUSTERS is ever decresed to 4 or lower,
	 * you must increase the size of the type of
	 * these counters.  You have been duly warned. -DaveM
	 */
/*0x30*/struct {
		u16	next;
		u16	flush;
	} alloc_info[NCLUSTERS];

	/* The lowest used consistent mapping entry.  Since
	 * we allocate consistent maps out of cluster 0 this
	 * is relative to the beginning of closter 0.
	 */
/*0x50*/u32		lowest_consistent_map;
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

static void __iommu_flushall(struct sbus_iommu *iommu)
{
	unsigned long tag = iommu->iommu_regs + IOMMU_TAGDIAG;
	int entry;

	for (entry = 0; entry < 16; entry++) {
		upa_writeq(0, tag);
		tag += 8UL;
	}
	upa_readq(iommu->sbus_control_reg);

	for (entry = 0; entry < NCLUSTERS; entry++) {
		iommu->alloc_info[entry].flush =
			iommu->alloc_info[entry].next;
	}
}

static void iommu_flush(struct sbus_iommu *iommu, u32 base, unsigned long npages)
{
	while (npages--)
		upa_writeq(base + (npages << IO_PAGE_SHIFT),
			   iommu->iommu_regs + IOMMU_FLUSH);
	upa_readq(iommu->sbus_control_reg);
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

static void sbus_strbuf_flush(struct sbus_iommu *iommu, u32 base, unsigned long npages, int direction)
{
	unsigned long n;
	int limit;

	n = npages;
	while (n--)
		upa_writeq(base + (n << IO_PAGE_SHIFT),
			   iommu->strbuf_regs + STRBUF_PFLUSH);

	/* If the device could not have possibly put dirty data into
	 * the streaming cache, no flush-flag synchronization needs
	 * to be performed.
	 */
	if (direction == SBUS_DMA_TODEVICE)
		return;

	iommu->strbuf_flushflag = 0UL;

	/* Whoopee cushion! */
	upa_writeq(__pa(&iommu->strbuf_flushflag),
		   iommu->strbuf_regs + STRBUF_FSYNC);
	upa_readq(iommu->sbus_control_reg);

	limit = 100000;
	while (iommu->strbuf_flushflag == 0UL) {
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

static iopte_t *alloc_streaming_cluster(struct sbus_iommu *iommu, unsigned long npages)
{
	iopte_t *iopte, *limit, *first, *cluster;
	unsigned long cnum, ent, nent, flush_point, found;

	cnum = 0;
	nent = 1;
	while ((1UL << cnum) < npages)
		cnum++;
	if(cnum >= NCLUSTERS) {
		nent = 1UL << (cnum - NCLUSTERS);
		cnum = NCLUSTERS - 1;
	}
	iopte  = iommu->page_table + (cnum * CLUSTER_NPAGES);

	if (cnum == 0)
		limit = (iommu->page_table +
			 iommu->lowest_consistent_map);
	else
		limit = (iopte + CLUSTER_NPAGES);

	iopte += ((ent = iommu->alloc_info[cnum].next) << cnum);
	flush_point = iommu->alloc_info[cnum].flush;

	first = iopte;
	cluster = NULL;
	found = 0;
	for (;;) {
		if (iopte_val(*iopte) == 0UL) {
			found++;
			if (!cluster)
				cluster = iopte;
		} else {
			/* Used cluster in the way */
			cluster = NULL;
			found = 0;
		}

		if (found == nent)
			break;

		iopte += (1 << cnum);
		ent++;
		if (iopte >= limit) {
			iopte = (iommu->page_table + (cnum * CLUSTER_NPAGES));
			ent = 0;

			/* Multiple cluster allocations must not wrap */
			cluster = NULL;
			found = 0;
		}
		if (ent == flush_point)
			__iommu_flushall(iommu);
		if (iopte == first)
			goto bad;
	}

	/* ent/iopte points to the last cluster entry we're going to use,
	 * so save our place for the next allocation.
	 */
	if ((iopte + (1 << cnum)) >= limit)
		ent = 0;
	else
		ent = ent + 1;
	iommu->alloc_info[cnum].next = ent;
	if (ent == flush_point)
		__iommu_flushall(iommu);

	/* I've got your streaming cluster right here buddy boy... */
	return cluster;

bad:
	printk(KERN_EMERG "sbus: alloc_streaming_cluster of npages(%ld) failed!\n",
	       npages);
	return NULL;
}

static void free_streaming_cluster(struct sbus_iommu *iommu, u32 base, unsigned long npages)
{
	unsigned long cnum, ent, nent;
	iopte_t *iopte;

	cnum = 0;
	nent = 1;
	while ((1UL << cnum) < npages)
		cnum++;
	if(cnum >= NCLUSTERS) {
		nent = 1UL << (cnum - NCLUSTERS);
		cnum = NCLUSTERS - 1;
	}
	ent = (base & CLUSTER_MASK) >> (IO_PAGE_SHIFT + cnum);
	iopte = iommu->page_table + ((base - MAP_BASE) >> IO_PAGE_SHIFT);
	do {
		iopte_val(*iopte) = 0UL;
		iopte += 1 << cnum;
	} while(--nent);

	/* If the global flush might not have caught this entry,
	 * adjust the flush point such that we will flush before
	 * ever trying to reuse it.
	 */
#define between(X,Y,Z)	(((Z) - (Y)) >= ((X) - (Y)))
	if (between(ent, iommu->alloc_info[cnum].next, iommu->alloc_info[cnum].flush))
		iommu->alloc_info[cnum].flush = ent;
#undef between
}

/* We allocate consistent mappings from the end of cluster zero. */
static iopte_t *alloc_consistent_cluster(struct sbus_iommu *iommu, unsigned long npages)
{
	iopte_t *iopte;

	iopte = iommu->page_table + (1 * CLUSTER_NPAGES);
	while (iopte > iommu->page_table) {
		iopte--;
		if (!(iopte_val(*iopte) & IOPTE_VALID)) {
			unsigned long tmp = npages;

			while (--tmp) {
				iopte--;
				if (iopte_val(*iopte) & IOPTE_VALID)
					break;
			}
			if (tmp == 0) {
				u32 entry = (iopte - iommu->page_table);

				if (entry < iommu->lowest_consistent_map)
					iommu->lowest_consistent_map = entry;
				return iopte;
			}
		}
	}
	return NULL;
}

static void free_consistent_cluster(struct sbus_iommu *iommu, u32 base, unsigned long npages)
{
	iopte_t *iopte = iommu->page_table + ((base - MAP_BASE) >> IO_PAGE_SHIFT);

	if ((iopte - iommu->page_table) == iommu->lowest_consistent_map) {
		iopte_t *walk = iopte + npages;
		iopte_t *limit;

		limit = iommu->page_table + CLUSTER_NPAGES;
		while (walk < limit) {
			if (iopte_val(*walk) != 0UL)
				break;
			walk++;
		}
		iommu->lowest_consistent_map =
			(walk - iommu->page_table);
	}

	while (npages--)
		*iopte++ = __iopte(0UL);
}

void *sbus_alloc_consistent(struct sbus_dev *sdev, size_t size, dma_addr_t *dvma_addr)
{
	unsigned long order, first_page, flags;
	struct sbus_iommu *iommu;
	iopte_t *iopte;
	void *ret;
	int npages;

	if (size <= 0 || sdev == NULL || dvma_addr == NULL)
		return NULL;

	size = IO_PAGE_ALIGN(size);
	order = get_order(size);
	if (order >= 10)
		return NULL;
	first_page = __get_free_pages(GFP_KERNEL, order);
	if (first_page == 0UL)
		return NULL;
	memset((char *)first_page, 0, PAGE_SIZE << order);

	iommu = sdev->bus->iommu;

	spin_lock_irqsave(&iommu->lock, flags);
	iopte = alloc_consistent_cluster(iommu, size >> IO_PAGE_SHIFT);
	if (iopte == NULL) {
		spin_unlock_irqrestore(&iommu->lock, flags);
		free_pages(first_page, order);
		return NULL;
	}

	/* Ok, we're committed at this point. */
	*dvma_addr = MAP_BASE +	((iopte - iommu->page_table) << IO_PAGE_SHIFT);
	ret = (void *) first_page;
	npages = size >> IO_PAGE_SHIFT;
	while (npages--) {
		*iopte++ = __iopte(IOPTE_VALID | IOPTE_CACHE | IOPTE_WRITE |
				   (__pa(first_page) & IOPTE_PAGE));
		first_page += IO_PAGE_SIZE;
	}
	iommu_flush(iommu, *dvma_addr, size >> IO_PAGE_SHIFT);
	spin_unlock_irqrestore(&iommu->lock, flags);

	return ret;
}

void sbus_free_consistent(struct sbus_dev *sdev, size_t size, void *cpu, dma_addr_t dvma)
{
	unsigned long order, npages;
	struct sbus_iommu *iommu;

	if (size <= 0 || sdev == NULL || cpu == NULL)
		return;

	npages = IO_PAGE_ALIGN(size) >> IO_PAGE_SHIFT;
	iommu = sdev->bus->iommu;

	spin_lock_irq(&iommu->lock);
	free_consistent_cluster(iommu, dvma, npages);
	iommu_flush(iommu, dvma, npages);
	spin_unlock_irq(&iommu->lock);

	order = get_order(size);
	if (order < 10)
		free_pages((unsigned long)cpu, order);
}

dma_addr_t sbus_map_single(struct sbus_dev *sdev, void *ptr, size_t size, int dir)
{
	struct sbus_iommu *iommu = sdev->bus->iommu;
	unsigned long npages, pbase, flags;
	iopte_t *iopte;
	u32 dma_base, offset;
	unsigned long iopte_bits;

	if (dir == SBUS_DMA_NONE)
		BUG();

	pbase = (unsigned long) ptr;
	offset = (u32) (pbase & ~IO_PAGE_MASK);
	size = (IO_PAGE_ALIGN(pbase + size) - (pbase & IO_PAGE_MASK));
	pbase = (unsigned long) __pa(pbase & IO_PAGE_MASK);

	spin_lock_irqsave(&iommu->lock, flags);
	npages = size >> IO_PAGE_SHIFT;
	iopte = alloc_streaming_cluster(iommu, npages);
	if (iopte == NULL)
		goto bad;
	dma_base = MAP_BASE + ((iopte - iommu->page_table) << IO_PAGE_SHIFT);
	npages = size >> IO_PAGE_SHIFT;
	iopte_bits = IOPTE_VALID | IOPTE_STBUF | IOPTE_CACHE;
	if (dir != SBUS_DMA_TODEVICE)
		iopte_bits |= IOPTE_WRITE;
	while (npages--) {
		*iopte++ = __iopte(iopte_bits | (pbase & IOPTE_PAGE));
		pbase += IO_PAGE_SIZE;
	}
	npages = size >> IO_PAGE_SHIFT;
	spin_unlock_irqrestore(&iommu->lock, flags);

	return (dma_base | offset);

bad:
	spin_unlock_irqrestore(&iommu->lock, flags);
	BUG();
	return 0;
}

void sbus_unmap_single(struct sbus_dev *sdev, dma_addr_t dma_addr, size_t size, int direction)
{
	struct sbus_iommu *iommu = sdev->bus->iommu;
	u32 dma_base = dma_addr & IO_PAGE_MASK;
	unsigned long flags;

	size = (IO_PAGE_ALIGN(dma_addr + size) - dma_base);

	spin_lock_irqsave(&iommu->lock, flags);
	free_streaming_cluster(iommu, dma_base, size >> IO_PAGE_SHIFT);
	sbus_strbuf_flush(iommu, dma_base, size >> IO_PAGE_SHIFT, direction);
	spin_unlock_irqrestore(&iommu->lock, flags);
}

#define SG_ENT_PHYS_ADDRESS(SG)	\
	(__pa(page_address((SG)->page)) + (SG)->offset)

static inline void fill_sg(iopte_t *iopte, struct scatterlist *sg, int nused, int nelems, unsigned long iopte_bits)
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

				tmp = (unsigned long) SG_ENT_PHYS_ADDRESS(sg);
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

			pteval = ((pteval & IOPTE_PAGE) | iopte_bits);
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

int sbus_map_sg(struct sbus_dev *sdev, struct scatterlist *sg, int nents, int dir)
{
	struct sbus_iommu *iommu = sdev->bus->iommu;
	unsigned long flags, npages;
	iopte_t *iopte;
	u32 dma_base;
	struct scatterlist *sgtmp;
	int used;
	unsigned long iopte_bits;

	if (dir == SBUS_DMA_NONE)
		BUG();

	/* Fast path single entry scatterlists. */
	if (nents == 1) {
		sg->dma_address =
			sbus_map_single(sdev,
					(page_address(sg->page) + sg->offset),
					sg->length, dir);
		sg->dma_length = sg->length;
		return 1;
	}

	npages = prepare_sg(sg, nents);

	spin_lock_irqsave(&iommu->lock, flags);
	iopte = alloc_streaming_cluster(iommu, npages);
	if (iopte == NULL)
		goto bad;
	dma_base = MAP_BASE + ((iopte - iommu->page_table) << IO_PAGE_SHIFT);

	/* Normalize DVMA addresses. */
	sgtmp = sg;
	used = nents;

	while (used && sgtmp->dma_length) {
		sgtmp->dma_address += dma_base;
		sgtmp++;
		used--;
	}
	used = nents - used;

	iopte_bits = IOPTE_VALID | IOPTE_STBUF | IOPTE_CACHE;
	if (dir != SBUS_DMA_TODEVICE)
		iopte_bits |= IOPTE_WRITE;

	fill_sg(iopte, sg, used, nents, iopte_bits);
#ifdef VERIFY_SG
	verify_sglist(sg, nents, iopte, npages);
#endif
	spin_unlock_irqrestore(&iommu->lock, flags);

	return used;

bad:
	spin_unlock_irqrestore(&iommu->lock, flags);
	BUG();
	return 0;
}

void sbus_unmap_sg(struct sbus_dev *sdev, struct scatterlist *sg, int nents, int direction)
{
	unsigned long size, flags;
	struct sbus_iommu *iommu;
	u32 dvma_base;
	int i;

	/* Fast path single entry scatterlists. */
	if (nents == 1) {
		sbus_unmap_single(sdev, sg->dma_address, sg->dma_length, direction);
		return;
	}

	dvma_base = sg[0].dma_address & IO_PAGE_MASK;
	for (i = 0; i < nents; i++) {
		if (sg[i].dma_length == 0)
			break;
	}
	i--;
	size = IO_PAGE_ALIGN(sg[i].dma_address + sg[i].dma_length) - dvma_base;

	iommu = sdev->bus->iommu;
	spin_lock_irqsave(&iommu->lock, flags);
	free_streaming_cluster(iommu, dvma_base, size >> IO_PAGE_SHIFT);
	sbus_strbuf_flush(iommu, dvma_base, size >> IO_PAGE_SHIFT, direction);
	spin_unlock_irqrestore(&iommu->lock, flags);
}

void sbus_dma_sync_single_for_cpu(struct sbus_dev *sdev, dma_addr_t base, size_t size, int direction)
{
	struct sbus_iommu *iommu = sdev->bus->iommu;
	unsigned long flags;

	size = (IO_PAGE_ALIGN(base + size) - (base & IO_PAGE_MASK));

	spin_lock_irqsave(&iommu->lock, flags);
	sbus_strbuf_flush(iommu, base & IO_PAGE_MASK, size >> IO_PAGE_SHIFT, direction);
	spin_unlock_irqrestore(&iommu->lock, flags);
}

void sbus_dma_sync_single_for_device(struct sbus_dev *sdev, dma_addr_t base, size_t size, int direction)
{
}

void sbus_dma_sync_sg_for_cpu(struct sbus_dev *sdev, struct scatterlist *sg, int nents, int direction)
{
	struct sbus_iommu *iommu = sdev->bus->iommu;
	unsigned long flags, size;
	u32 base;
	int i;

	base = sg[0].dma_address & IO_PAGE_MASK;
	for (i = 0; i < nents; i++) {
		if (sg[i].dma_length == 0)
			break;
	}
	i--;
	size = IO_PAGE_ALIGN(sg[i].dma_address + sg[i].dma_length) - base;

	spin_lock_irqsave(&iommu->lock, flags);
	sbus_strbuf_flush(iommu, base, size >> IO_PAGE_SHIFT, direction);
	spin_unlock_irqrestore(&iommu->lock, flags);
}

void sbus_dma_sync_sg_for_device(struct sbus_dev *sdev, struct scatterlist *sg, int nents, int direction)
{
}

/* Enable 64-bit DVMA mode for the given device. */
void sbus_set_sbus64(struct sbus_dev *sdev, int bursts)
{
	struct sbus_iommu *iommu = sdev->bus->iommu;
	int slot = sdev->slot;
	unsigned long cfg_reg;
	u64 val;

	cfg_reg = iommu->sbus_control_reg;
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

/* SBUS SYSIO INO number to Sparc PIL level. */
static unsigned char sysio_ino_to_pil[] = {
	0, 4, 4, 7, 5, 7, 8, 9,		/* SBUS slot 0 */
	0, 4, 4, 7, 5, 7, 8, 9,		/* SBUS slot 1 */
	0, 4, 4, 7, 5, 7, 8, 9,		/* SBUS slot 2 */
	0, 4, 4, 7, 5, 7, 8, 9,		/* SBUS slot 3 */
	4, /* Onboard SCSI */
	5, /* Onboard Ethernet */
/*XXX*/	8, /* Onboard BPP */
	0, /* Bogon */
       13, /* Audio */
/*XXX*/15, /* PowerFail */
	0, /* Bogon */
	0, /* Bogon */
       12, /* Zilog Serial Channels (incl. Keyboard/Mouse lines) */
       11, /* Floppy */
	0, /* Spare Hardware (bogon for now) */
	0, /* Keyboard (bogon for now) */
	0, /* Mouse (bogon for now) */
	0, /* Serial (bogon for now) */
     0, 0, /* Bogon, Bogon */
       10, /* Timer 0 */
       11, /* Timer 1 */
     0, 0, /* Bogon, Bogon */
       15, /* Uncorrectable SBUS Error */
       15, /* Correctable SBUS Error */
       15, /* SBUS Error */
/*XXX*/ 0, /* Power Management (bogon for now) */
};

/* INO number to IMAP register offset for SYSIO external IRQ's.
 * This should conform to both Sunfire/Wildfire server and Fusion
 * desktop designs.
 */
#define SYSIO_IMAP_SLOT0	0x2c04UL
#define SYSIO_IMAP_SLOT1	0x2c0cUL
#define SYSIO_IMAP_SLOT2	0x2c14UL
#define SYSIO_IMAP_SLOT3	0x2c1cUL
#define SYSIO_IMAP_SCSI		0x3004UL
#define SYSIO_IMAP_ETH		0x300cUL
#define SYSIO_IMAP_BPP		0x3014UL
#define SYSIO_IMAP_AUDIO	0x301cUL
#define SYSIO_IMAP_PFAIL	0x3024UL
#define SYSIO_IMAP_KMS		0x302cUL
#define SYSIO_IMAP_FLPY		0x3034UL
#define SYSIO_IMAP_SHW		0x303cUL
#define SYSIO_IMAP_KBD		0x3044UL
#define SYSIO_IMAP_MS		0x304cUL
#define SYSIO_IMAP_SER		0x3054UL
#define SYSIO_IMAP_TIM0		0x3064UL
#define SYSIO_IMAP_TIM1		0x306cUL
#define SYSIO_IMAP_UE		0x3074UL
#define SYSIO_IMAP_CE		0x307cUL
#define SYSIO_IMAP_SBERR	0x3084UL
#define SYSIO_IMAP_PMGMT	0x308cUL
#define SYSIO_IMAP_GFX		0x3094UL
#define SYSIO_IMAP_EUPA		0x309cUL

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
#define SYSIO_ICLR_SLOT0	0x340cUL
#define SYSIO_ICLR_SLOT1	0x344cUL
#define SYSIO_ICLR_SLOT2	0x348cUL
#define SYSIO_ICLR_SLOT3	0x34ccUL
static unsigned long sysio_imap_to_iclr(unsigned long imap)
{
	unsigned long diff = SYSIO_ICLR_UNUSED0 - SYSIO_IMAP_SLOT0;
	return imap + diff;
}

unsigned int sbus_build_irq(void *buscookie, unsigned int ino)
{
	struct sbus_bus *sbus = (struct sbus_bus *)buscookie;
	struct sbus_iommu *iommu = sbus->iommu;
	unsigned long reg_base = iommu->sbus_control_reg - 0x2000UL;
	unsigned long imap, iclr;
	int pil, sbus_level = 0;

	pil = sysio_ino_to_pil[ino];
	if (!pil) {
		printk("sbus_irq_build: Bad SYSIO INO[%x]\n", ino);
		panic("Bad SYSIO IRQ translations...");
	}

	if (PIL_RESERVED(pil))
		BUG();

	imap = sysio_irq_offsets[ino];
	if (imap == ((unsigned long)-1)) {
		prom_printf("get_irq_translations: Bad SYSIO INO[%x] cpu[%d]\n",
			    ino, pil);
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
	return build_irq(pil, sbus_level, iclr, imap);
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
static irqreturn_t sysio_ue_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct sbus_bus *sbus = dev_id;
	struct sbus_iommu *iommu = sbus->iommu;
	unsigned long reg_base = iommu->sbus_control_reg - 0x2000UL;
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
static irqreturn_t sysio_ce_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct sbus_bus *sbus = dev_id;
	struct sbus_iommu *iommu = sbus->iommu;
	unsigned long reg_base = iommu->sbus_control_reg - 0x2000UL;
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
static irqreturn_t sysio_sbus_error_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct sbus_bus *sbus = dev_id;
	struct sbus_iommu *iommu = sbus->iommu;
	unsigned long afsr_reg, afar_reg, reg_base;
	unsigned long afsr, afar, error_bits;
	int reported;

	reg_base = iommu->sbus_control_reg - 0x2000UL;
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
	struct sbus_iommu *iommu = sbus->iommu;
	unsigned long reg_base = iommu->sbus_control_reg - 0x2000UL;
	unsigned int irq;
	u64 control;

	irq = sbus_build_irq(sbus, SYSIO_UE_INO);
	if (request_irq(irq, sysio_ue_handler,
			SA_SHIRQ, "SYSIO UE", sbus) < 0) {
		prom_printf("SYSIO[%x]: Cannot register UE interrupt.\n",
			    sbus->portid);
		prom_halt();
	}

	irq = sbus_build_irq(sbus, SYSIO_CE_INO);
	if (request_irq(irq, sysio_ce_handler,
			SA_SHIRQ, "SYSIO CE", sbus) < 0) {
		prom_printf("SYSIO[%x]: Cannot register CE interrupt.\n",
			    sbus->portid);
		prom_halt();
	}

	irq = sbus_build_irq(sbus, SYSIO_SBUSERR_INO);
	if (request_irq(irq, sysio_sbus_error_handler,
			SA_SHIRQ, "SYSIO SBUS Error", sbus) < 0) {
		prom_printf("SYSIO[%x]: Cannot register SBUS Error interrupt.\n",
			    sbus->portid);
		prom_halt();
	}

	/* Now turn the error interrupts on and also enable ECC checking. */
	upa_writeq((SYSIO_ECNTRL_ECCEN |
		    SYSIO_ECNTRL_UEEN  |
		    SYSIO_ECNTRL_CEEN),
		   reg_base + ECC_CONTROL);

	control = upa_readq(iommu->sbus_control_reg);
	control |= 0x100UL; /* SBUS Error Interrupt Enable */
	upa_writeq(control, iommu->sbus_control_reg);
}

/* Boot time initialization. */
void __init sbus_iommu_init(int prom_node, struct sbus_bus *sbus)
{
	struct linux_prom64_registers rprop;
	struct sbus_iommu *iommu;
	unsigned long regs, tsb_base;
	u64 control;
	int err, i;

	sbus->portid = prom_getintdefault(sbus->prom_node,
					  "upa-portid", -1);

	err = prom_getproperty(prom_node, "reg",
			       (char *)&rprop, sizeof(rprop));
	if (err < 0) {
		prom_printf("sbus_iommu_init: Cannot map SYSIO control registers.\n");
		prom_halt();
	}
	regs = rprop.phys_addr;

	iommu = kmalloc(sizeof(*iommu) + SMP_CACHE_BYTES, GFP_ATOMIC);
	if (iommu == NULL) {
		prom_printf("sbus_iommu_init: Fatal error, kmalloc(iommu) failed\n");
		prom_halt();
	}

	/* Align on E$ line boundary. */
	iommu = (struct sbus_iommu *)
		(((unsigned long)iommu + (SMP_CACHE_BYTES - 1UL)) &
		 ~(SMP_CACHE_BYTES - 1UL));

	memset(iommu, 0, sizeof(*iommu));

	/* We start with no consistent mappings. */
	iommu->lowest_consistent_map = CLUSTER_NPAGES;

	for (i = 0; i < NCLUSTERS; i++) {
		iommu->alloc_info[i].flush = 0;
		iommu->alloc_info[i].next = 0;
	}

	/* Setup spinlock. */
	spin_lock_init(&iommu->lock);

	/* Init register offsets. */
	iommu->iommu_regs = regs + SYSIO_IOMMUREG_BASE;
	iommu->strbuf_regs = regs + SYSIO_STRBUFREG_BASE;

	/* The SYSIO SBUS control register is used for dummy reads
	 * in order to ensure write completion.
	 */
	iommu->sbus_control_reg = regs + 0x2000UL;

	/* Link into SYSIO software state. */
	sbus->iommu = iommu;

	printk("SYSIO: UPA portID %x, at %016lx\n",
	       sbus->portid, regs);

	/* Setup for TSB_SIZE=7, TBW_SIZE=0, MMU_DE=1, MMU_EN=1 */
	control = upa_readq(iommu->iommu_regs + IOMMU_CONTROL);
	control = ((7UL << 16UL)	|
		   (0UL << 2UL)		|
		   (1UL << 1UL)		|
		   (1UL << 0UL));

	/* Using the above configuration we need 1MB iommu page
	 * table (128K ioptes * 8 bytes per iopte).  This is
	 * page order 7 on UltraSparc.
	 */
	tsb_base = __get_free_pages(GFP_ATOMIC, get_order(IO_TSB_SIZE));
	if (tsb_base == 0UL) {
		prom_printf("sbus_iommu_init: Fatal error, cannot alloc TSB table.\n");
		prom_halt();
	}

	iommu->page_table = (iopte_t *) tsb_base;
	memset(iommu->page_table, 0, IO_TSB_SIZE);

	upa_writeq(control, iommu->iommu_regs + IOMMU_CONTROL);

	/* Clean out any cruft in the IOMMU using
	 * diagnostic accesses.
	 */
	for (i = 0; i < 16; i++) {
		unsigned long dram = iommu->iommu_regs + IOMMU_DRAMDIAG;
		unsigned long tag = iommu->iommu_regs + IOMMU_TAGDIAG;

		dram += (unsigned long)i * 8UL;
		tag += (unsigned long)i * 8UL;
		upa_writeq(0, dram);
		upa_writeq(0, tag);
	}
	upa_readq(iommu->sbus_control_reg);

	/* Give the TSB to SYSIO. */
	upa_writeq(__pa(tsb_base), iommu->iommu_regs + IOMMU_TSBBASE);

	/* Setup streaming buffer, DE=1 SB_EN=1 */
	control = (1UL << 1UL) | (1UL << 0UL);
	upa_writeq(control, iommu->strbuf_regs + STRBUF_CONTROL);

	/* Clear out the tags using diagnostics. */
	for (i = 0; i < 16; i++) {
		unsigned long ptag, ltag;

		ptag = iommu->strbuf_regs + STRBUF_PTAGDIAG;
		ltag = iommu->strbuf_regs + STRBUF_LTAGDIAG;
		ptag += (unsigned long)i * 8UL;
		ltag += (unsigned long)i * 8UL;

		upa_writeq(0UL, ptag);
		upa_writeq(0UL, ltag);
	}

	/* Enable DVMA arbitration for all devices/slots. */
	control = upa_readq(iommu->sbus_control_reg);
	control |= 0x3fUL;
	upa_writeq(control, iommu->sbus_control_reg);

	/* Now some Xfire specific grot... */
	if (this_is_starfire)
		sbus->starfire_cookie = starfire_hookup(sbus->portid);
	else
		sbus->starfire_cookie = NULL;

	sysio_register_error_handlers(sbus);
}
