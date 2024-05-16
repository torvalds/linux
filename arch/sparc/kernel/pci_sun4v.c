// SPDX-License-Identifier: GPL-2.0
/* pci_sun4v.c: SUN4V specific PCI controller support.
 *
 * Copyright (C) 2006, 2007, 2008 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/export.h>
#include <linux/log2.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/dma-map-ops.h>
#include <asm/iommu-common.h>

#include <asm/iommu.h>
#include <asm/irq.h>
#include <asm/hypervisor.h>
#include <asm/prom.h>

#include "pci_impl.h"
#include "iommu_common.h"
#include "kernel.h"

#include "pci_sun4v.h"

#define DRIVER_NAME	"pci_sun4v"
#define PFX		DRIVER_NAME ": "

static unsigned long vpci_major;
static unsigned long vpci_minor;

struct vpci_version {
	unsigned long major;
	unsigned long minor;
};

/* Ordered from largest major to lowest */
static struct vpci_version vpci_versions[] = {
	{ .major = 2, .minor = 0 },
	{ .major = 1, .minor = 1 },
};

static unsigned long vatu_major = 1;
static unsigned long vatu_minor = 1;

#define PGLIST_NENTS	(PAGE_SIZE / sizeof(u64))

struct iommu_batch {
	struct device	*dev;		/* Device mapping is for.	*/
	unsigned long	prot;		/* IOMMU page protections	*/
	unsigned long	entry;		/* Index into IOTSB.		*/
	u64		*pglist;	/* List of physical pages	*/
	unsigned long	npages;		/* Number of pages in list.	*/
};

static DEFINE_PER_CPU(struct iommu_batch, iommu_batch);
static int iommu_batch_initialized;

/* Interrupts must be disabled.  */
static inline void iommu_batch_start(struct device *dev, unsigned long prot, unsigned long entry)
{
	struct iommu_batch *p = this_cpu_ptr(&iommu_batch);

	p->dev		= dev;
	p->prot		= prot;
	p->entry	= entry;
	p->npages	= 0;
}

static inline bool iommu_use_atu(struct iommu *iommu, u64 mask)
{
	return iommu->atu && mask > DMA_BIT_MASK(32);
}

/* Interrupts must be disabled.  */
static long iommu_batch_flush(struct iommu_batch *p, u64 mask)
{
	struct pci_pbm_info *pbm = p->dev->archdata.host_controller;
	u64 *pglist = p->pglist;
	u64 index_count;
	unsigned long devhandle = pbm->devhandle;
	unsigned long prot = p->prot;
	unsigned long entry = p->entry;
	unsigned long npages = p->npages;
	unsigned long iotsb_num;
	unsigned long ret;
	long num;

	/* VPCI maj=1, min=[0,1] only supports read and write */
	if (vpci_major < 2)
		prot &= (HV_PCI_MAP_ATTR_READ | HV_PCI_MAP_ATTR_WRITE);

	while (npages != 0) {
		if (!iommu_use_atu(pbm->iommu, mask)) {
			num = pci_sun4v_iommu_map(devhandle,
						  HV_PCI_TSBID(0, entry),
						  npages,
						  prot,
						  __pa(pglist));
			if (unlikely(num < 0)) {
				pr_err_ratelimited("%s: IOMMU map of [%08lx:%08llx:%lx:%lx:%lx] failed with status %ld\n",
						   __func__,
						   devhandle,
						   HV_PCI_TSBID(0, entry),
						   npages, prot, __pa(pglist),
						   num);
				return -1;
			}
		} else {
			index_count = HV_PCI_IOTSB_INDEX_COUNT(npages, entry),
			iotsb_num = pbm->iommu->atu->iotsb->iotsb_num;
			ret = pci_sun4v_iotsb_map(devhandle,
						  iotsb_num,
						  index_count,
						  prot,
						  __pa(pglist),
						  &num);
			if (unlikely(ret != HV_EOK)) {
				pr_err_ratelimited("%s: ATU map of [%08lx:%lx:%llx:%lx:%lx] failed with status %ld\n",
						   __func__,
						   devhandle, iotsb_num,
						   index_count, prot,
						   __pa(pglist), ret);
				return -1;
			}
		}
		entry += num;
		npages -= num;
		pglist += num;
	}

	p->entry = entry;
	p->npages = 0;

	return 0;
}

static inline void iommu_batch_new_entry(unsigned long entry, u64 mask)
{
	struct iommu_batch *p = this_cpu_ptr(&iommu_batch);

	if (p->entry + p->npages == entry)
		return;
	if (p->entry != ~0UL)
		iommu_batch_flush(p, mask);
	p->entry = entry;
}

/* Interrupts must be disabled.  */
static inline long iommu_batch_add(u64 phys_page, u64 mask)
{
	struct iommu_batch *p = this_cpu_ptr(&iommu_batch);

	BUG_ON(p->npages >= PGLIST_NENTS);

	p->pglist[p->npages++] = phys_page;
	if (p->npages == PGLIST_NENTS)
		return iommu_batch_flush(p, mask);

	return 0;
}

/* Interrupts must be disabled.  */
static inline long iommu_batch_end(u64 mask)
{
	struct iommu_batch *p = this_cpu_ptr(&iommu_batch);

	BUG_ON(p->npages >= PGLIST_NENTS);

	return iommu_batch_flush(p, mask);
}

static void *dma_4v_alloc_coherent(struct device *dev, size_t size,
				   dma_addr_t *dma_addrp, gfp_t gfp,
				   unsigned long attrs)
{
	u64 mask;
	unsigned long flags, order, first_page, npages, n;
	unsigned long prot = 0;
	struct iommu *iommu;
	struct iommu_map_table *tbl;
	struct page *page;
	void *ret;
	long entry;
	int nid;

	size = IO_PAGE_ALIGN(size);
	order = get_order(size);
	if (unlikely(order > MAX_ORDER))
		return NULL;

	npages = size >> IO_PAGE_SHIFT;

	if (attrs & DMA_ATTR_WEAK_ORDERING)
		prot = HV_PCI_MAP_ATTR_RELAXED_ORDER;

	nid = dev->archdata.numa_node;
	page = alloc_pages_node(nid, gfp, order);
	if (unlikely(!page))
		return NULL;

	first_page = (unsigned long) page_address(page);
	memset((char *)first_page, 0, PAGE_SIZE << order);

	iommu = dev->archdata.iommu;
	mask = dev->coherent_dma_mask;
	if (!iommu_use_atu(iommu, mask))
		tbl = &iommu->tbl;
	else
		tbl = &iommu->atu->tbl;

	entry = iommu_tbl_range_alloc(dev, tbl, npages, NULL,
				      (unsigned long)(-1), 0);

	if (unlikely(entry == IOMMU_ERROR_CODE))
		goto range_alloc_fail;

	*dma_addrp = (tbl->table_map_base + (entry << IO_PAGE_SHIFT));
	ret = (void *) first_page;
	first_page = __pa(first_page);

	local_irq_save(flags);

	iommu_batch_start(dev,
			  (HV_PCI_MAP_ATTR_READ | prot |
			   HV_PCI_MAP_ATTR_WRITE),
			  entry);

	for (n = 0; n < npages; n++) {
		long err = iommu_batch_add(first_page + (n * PAGE_SIZE), mask);
		if (unlikely(err < 0L))
			goto iommu_map_fail;
	}

	if (unlikely(iommu_batch_end(mask) < 0L))
		goto iommu_map_fail;

	local_irq_restore(flags);

	return ret;

iommu_map_fail:
	local_irq_restore(flags);
	iommu_tbl_range_free(tbl, *dma_addrp, npages, IOMMU_ERROR_CODE);

range_alloc_fail:
	free_pages(first_page, order);
	return NULL;
}

unsigned long dma_4v_iotsb_bind(unsigned long devhandle,
				unsigned long iotsb_num,
				struct pci_bus *bus_dev)
{
	struct pci_dev *pdev;
	unsigned long err;
	unsigned int bus;
	unsigned int device;
	unsigned int fun;

	list_for_each_entry(pdev, &bus_dev->devices, bus_list) {
		if (pdev->subordinate) {
			/* No need to bind pci bridge */
			dma_4v_iotsb_bind(devhandle, iotsb_num,
					  pdev->subordinate);
		} else {
			bus = bus_dev->number;
			device = PCI_SLOT(pdev->devfn);
			fun = PCI_FUNC(pdev->devfn);
			err = pci_sun4v_iotsb_bind(devhandle, iotsb_num,
						   HV_PCI_DEVICE_BUILD(bus,
								       device,
								       fun));

			/* If bind fails for one device it is going to fail
			 * for rest of the devices because we are sharing
			 * IOTSB. So in case of failure simply return with
			 * error.
			 */
			if (err)
				return err;
		}
	}

	return 0;
}

static void dma_4v_iommu_demap(struct device *dev, unsigned long devhandle,
			       dma_addr_t dvma, unsigned long iotsb_num,
			       unsigned long entry, unsigned long npages)
{
	unsigned long num, flags;
	unsigned long ret;

	local_irq_save(flags);
	do {
		if (dvma <= DMA_BIT_MASK(32)) {
			num = pci_sun4v_iommu_demap(devhandle,
						    HV_PCI_TSBID(0, entry),
						    npages);
		} else {
			ret = pci_sun4v_iotsb_demap(devhandle, iotsb_num,
						    entry, npages, &num);
			if (unlikely(ret != HV_EOK)) {
				pr_err_ratelimited("pci_iotsb_demap() failed with error: %ld\n",
						   ret);
			}
		}
		entry += num;
		npages -= num;
	} while (npages != 0);
	local_irq_restore(flags);
}

static void dma_4v_free_coherent(struct device *dev, size_t size, void *cpu,
				 dma_addr_t dvma, unsigned long attrs)
{
	struct pci_pbm_info *pbm;
	struct iommu *iommu;
	struct atu *atu;
	struct iommu_map_table *tbl;
	unsigned long order, npages, entry;
	unsigned long iotsb_num;
	u32 devhandle;

	npages = IO_PAGE_ALIGN(size) >> IO_PAGE_SHIFT;
	iommu = dev->archdata.iommu;
	pbm = dev->archdata.host_controller;
	atu = iommu->atu;
	devhandle = pbm->devhandle;

	if (!iommu_use_atu(iommu, dvma)) {
		tbl = &iommu->tbl;
		iotsb_num = 0; /* we don't care for legacy iommu */
	} else {
		tbl = &atu->tbl;
		iotsb_num = atu->iotsb->iotsb_num;
	}
	entry = ((dvma - tbl->table_map_base) >> IO_PAGE_SHIFT);
	dma_4v_iommu_demap(dev, devhandle, dvma, iotsb_num, entry, npages);
	iommu_tbl_range_free(tbl, dvma, npages, IOMMU_ERROR_CODE);
	order = get_order(size);
	if (order < 10)
		free_pages((unsigned long)cpu, order);
}

static dma_addr_t dma_4v_map_page(struct device *dev, struct page *page,
				  unsigned long offset, size_t sz,
				  enum dma_data_direction direction,
				  unsigned long attrs)
{
	struct iommu *iommu;
	struct atu *atu;
	struct iommu_map_table *tbl;
	u64 mask;
	unsigned long flags, npages, oaddr;
	unsigned long i, base_paddr;
	unsigned long prot;
	dma_addr_t bus_addr, ret;
	long entry;

	iommu = dev->archdata.iommu;
	atu = iommu->atu;

	if (unlikely(direction == DMA_NONE))
		goto bad;

	oaddr = (unsigned long)(page_address(page) + offset);
	npages = IO_PAGE_ALIGN(oaddr + sz) - (oaddr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;

	mask = *dev->dma_mask;
	if (!iommu_use_atu(iommu, mask))
		tbl = &iommu->tbl;
	else
		tbl = &atu->tbl;

	entry = iommu_tbl_range_alloc(dev, tbl, npages, NULL,
				      (unsigned long)(-1), 0);

	if (unlikely(entry == IOMMU_ERROR_CODE))
		goto bad;

	bus_addr = (tbl->table_map_base + (entry << IO_PAGE_SHIFT));
	ret = bus_addr | (oaddr & ~IO_PAGE_MASK);
	base_paddr = __pa(oaddr & IO_PAGE_MASK);
	prot = HV_PCI_MAP_ATTR_READ;
	if (direction != DMA_TO_DEVICE)
		prot |= HV_PCI_MAP_ATTR_WRITE;

	if (attrs & DMA_ATTR_WEAK_ORDERING)
		prot |= HV_PCI_MAP_ATTR_RELAXED_ORDER;

	local_irq_save(flags);

	iommu_batch_start(dev, prot, entry);

	for (i = 0; i < npages; i++, base_paddr += IO_PAGE_SIZE) {
		long err = iommu_batch_add(base_paddr, mask);
		if (unlikely(err < 0L))
			goto iommu_map_fail;
	}
	if (unlikely(iommu_batch_end(mask) < 0L))
		goto iommu_map_fail;

	local_irq_restore(flags);

	return ret;

bad:
	if (printk_ratelimit())
		WARN_ON(1);
	return DMA_MAPPING_ERROR;

iommu_map_fail:
	local_irq_restore(flags);
	iommu_tbl_range_free(tbl, bus_addr, npages, IOMMU_ERROR_CODE);
	return DMA_MAPPING_ERROR;
}

static void dma_4v_unmap_page(struct device *dev, dma_addr_t bus_addr,
			      size_t sz, enum dma_data_direction direction,
			      unsigned long attrs)
{
	struct pci_pbm_info *pbm;
	struct iommu *iommu;
	struct atu *atu;
	struct iommu_map_table *tbl;
	unsigned long npages;
	unsigned long iotsb_num;
	long entry;
	u32 devhandle;

	if (unlikely(direction == DMA_NONE)) {
		if (printk_ratelimit())
			WARN_ON(1);
		return;
	}

	iommu = dev->archdata.iommu;
	pbm = dev->archdata.host_controller;
	atu = iommu->atu;
	devhandle = pbm->devhandle;

	npages = IO_PAGE_ALIGN(bus_addr + sz) - (bus_addr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;
	bus_addr &= IO_PAGE_MASK;

	if (bus_addr <= DMA_BIT_MASK(32)) {
		iotsb_num = 0; /* we don't care for legacy iommu */
		tbl = &iommu->tbl;
	} else {
		iotsb_num = atu->iotsb->iotsb_num;
		tbl = &atu->tbl;
	}
	entry = (bus_addr - tbl->table_map_base) >> IO_PAGE_SHIFT;
	dma_4v_iommu_demap(dev, devhandle, bus_addr, iotsb_num, entry, npages);
	iommu_tbl_range_free(tbl, bus_addr, npages, IOMMU_ERROR_CODE);
}

static int dma_4v_map_sg(struct device *dev, struct scatterlist *sglist,
			 int nelems, enum dma_data_direction direction,
			 unsigned long attrs)
{
	struct scatterlist *s, *outs, *segstart;
	unsigned long flags, handle, prot;
	dma_addr_t dma_next = 0, dma_addr;
	unsigned int max_seg_size;
	unsigned long seg_boundary_size;
	int outcount, incount, i;
	struct iommu *iommu;
	struct atu *atu;
	struct iommu_map_table *tbl;
	u64 mask;
	unsigned long base_shift;
	long err;

	BUG_ON(direction == DMA_NONE);

	iommu = dev->archdata.iommu;
	if (nelems == 0 || !iommu)
		return -EINVAL;
	atu = iommu->atu;

	prot = HV_PCI_MAP_ATTR_READ;
	if (direction != DMA_TO_DEVICE)
		prot |= HV_PCI_MAP_ATTR_WRITE;

	if (attrs & DMA_ATTR_WEAK_ORDERING)
		prot |= HV_PCI_MAP_ATTR_RELAXED_ORDER;

	outs = s = segstart = &sglist[0];
	outcount = 1;
	incount = nelems;
	handle = 0;

	/* Init first segment length for backout at failure */
	outs->dma_length = 0;

	local_irq_save(flags);

	iommu_batch_start(dev, prot, ~0UL);

	max_seg_size = dma_get_max_seg_size(dev);
	seg_boundary_size = dma_get_seg_boundary_nr_pages(dev, IO_PAGE_SHIFT);

	mask = *dev->dma_mask;
	if (!iommu_use_atu(iommu, mask))
		tbl = &iommu->tbl;
	else
		tbl = &atu->tbl;

	base_shift = tbl->table_map_base >> IO_PAGE_SHIFT;

	for_each_sg(sglist, s, nelems, i) {
		unsigned long paddr, npages, entry, out_entry = 0, slen;

		slen = s->length;
		/* Sanity check */
		if (slen == 0) {
			dma_next = 0;
			continue;
		}
		/* Allocate iommu entries for that segment */
		paddr = (unsigned long) SG_ENT_PHYS_ADDRESS(s);
		npages = iommu_num_pages(paddr, slen, IO_PAGE_SIZE);
		entry = iommu_tbl_range_alloc(dev, tbl, npages,
					      &handle, (unsigned long)(-1), 0);

		/* Handle failure */
		if (unlikely(entry == IOMMU_ERROR_CODE)) {
			pr_err_ratelimited("iommu_alloc failed, iommu %p paddr %lx npages %lx\n",
					   tbl, paddr, npages);
			goto iommu_map_failed;
		}

		iommu_batch_new_entry(entry, mask);

		/* Convert entry to a dma_addr_t */
		dma_addr = tbl->table_map_base + (entry << IO_PAGE_SHIFT);
		dma_addr |= (s->offset & ~IO_PAGE_MASK);

		/* Insert into HW table */
		paddr &= IO_PAGE_MASK;
		while (npages--) {
			err = iommu_batch_add(paddr, mask);
			if (unlikely(err < 0L))
				goto iommu_map_failed;
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

	err = iommu_batch_end(mask);

	if (unlikely(err < 0L))
		goto iommu_map_failed;

	local_irq_restore(flags);

	if (outcount < incount) {
		outs = sg_next(outs);
		outs->dma_length = 0;
	}

	return outcount;

iommu_map_failed:
	for_each_sg(sglist, s, nelems, i) {
		if (s->dma_length != 0) {
			unsigned long vaddr, npages;

			vaddr = s->dma_address & IO_PAGE_MASK;
			npages = iommu_num_pages(s->dma_address, s->dma_length,
						 IO_PAGE_SIZE);
			iommu_tbl_range_free(tbl, vaddr, npages,
					     IOMMU_ERROR_CODE);
			/* XXX demap? XXX */
			s->dma_length = 0;
		}
		if (s == outs)
			break;
	}
	local_irq_restore(flags);

	return -EINVAL;
}

static void dma_4v_unmap_sg(struct device *dev, struct scatterlist *sglist,
			    int nelems, enum dma_data_direction direction,
			    unsigned long attrs)
{
	struct pci_pbm_info *pbm;
	struct scatterlist *sg;
	struct iommu *iommu;
	struct atu *atu;
	unsigned long flags, entry;
	unsigned long iotsb_num;
	u32 devhandle;

	BUG_ON(direction == DMA_NONE);

	iommu = dev->archdata.iommu;
	pbm = dev->archdata.host_controller;
	atu = iommu->atu;
	devhandle = pbm->devhandle;
	
	local_irq_save(flags);

	sg = sglist;
	while (nelems--) {
		dma_addr_t dma_handle = sg->dma_address;
		unsigned int len = sg->dma_length;
		unsigned long npages;
		struct iommu_map_table *tbl;
		unsigned long shift = IO_PAGE_SHIFT;

		if (!len)
			break;
		npages = iommu_num_pages(dma_handle, len, IO_PAGE_SIZE);

		if (dma_handle <= DMA_BIT_MASK(32)) {
			iotsb_num = 0; /* we don't care for legacy iommu */
			tbl = &iommu->tbl;
		} else {
			iotsb_num = atu->iotsb->iotsb_num;
			tbl = &atu->tbl;
		}
		entry = ((dma_handle - tbl->table_map_base) >> shift);
		dma_4v_iommu_demap(dev, devhandle, dma_handle, iotsb_num,
				   entry, npages);
		iommu_tbl_range_free(tbl, dma_handle, npages,
				     IOMMU_ERROR_CODE);
		sg = sg_next(sg);
	}

	local_irq_restore(flags);
}

static int dma_4v_supported(struct device *dev, u64 device_mask)
{
	struct iommu *iommu = dev->archdata.iommu;

	if (ali_sound_dma_hack(dev, device_mask))
		return 1;
	if (device_mask < iommu->dma_addr_mask)
		return 0;
	return 1;
}

static const struct dma_map_ops sun4v_dma_ops = {
	.alloc				= dma_4v_alloc_coherent,
	.free				= dma_4v_free_coherent,
	.map_page			= dma_4v_map_page,
	.unmap_page			= dma_4v_unmap_page,
	.map_sg				= dma_4v_map_sg,
	.unmap_sg			= dma_4v_unmap_sg,
	.dma_supported			= dma_4v_supported,
};

static void pci_sun4v_scan_bus(struct pci_pbm_info *pbm, struct device *parent)
{
	struct property *prop;
	struct device_node *dp;

	dp = pbm->op->dev.of_node;
	prop = of_find_property(dp, "66mhz-capable", NULL);
	pbm->is_66mhz_capable = (prop != NULL);
	pbm->pci_bus = pci_scan_one_pbm(pbm, parent);

	/* XXX register error interrupt handlers XXX */
}

static unsigned long probe_existing_entries(struct pci_pbm_info *pbm,
					    struct iommu_map_table *iommu)
{
	struct iommu_pool *pool;
	unsigned long i, pool_nr, cnt = 0;
	u32 devhandle;

	devhandle = pbm->devhandle;
	for (pool_nr = 0; pool_nr < iommu->nr_pools; pool_nr++) {
		pool = &(iommu->pools[pool_nr]);
		for (i = pool->start; i <= pool->end; i++) {
			unsigned long ret, io_attrs, ra;

			ret = pci_sun4v_iommu_getmap(devhandle,
						     HV_PCI_TSBID(0, i),
						     &io_attrs, &ra);
			if (ret == HV_EOK) {
				if (page_in_phys_avail(ra)) {
					pci_sun4v_iommu_demap(devhandle,
							      HV_PCI_TSBID(0,
							      i), 1);
				} else {
					cnt++;
					__set_bit(i, iommu->map);
				}
			}
		}
	}
	return cnt;
}

static int pci_sun4v_atu_alloc_iotsb(struct pci_pbm_info *pbm)
{
	struct atu *atu = pbm->iommu->atu;
	struct atu_iotsb *iotsb;
	void *table;
	u64 table_size;
	u64 iotsb_num;
	unsigned long order;
	unsigned long err;

	iotsb = kzalloc(sizeof(*iotsb), GFP_KERNEL);
	if (!iotsb) {
		err = -ENOMEM;
		goto out_err;
	}
	atu->iotsb = iotsb;

	/* calculate size of IOTSB */
	table_size = (atu->size / IO_PAGE_SIZE) * 8;
	order = get_order(table_size);
	table = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!table) {
		err = -ENOMEM;
		goto table_failed;
	}
	iotsb->table = table;
	iotsb->ra = __pa(table);
	iotsb->dvma_size = atu->size;
	iotsb->dvma_base = atu->base;
	iotsb->table_size = table_size;
	iotsb->page_size = IO_PAGE_SIZE;

	/* configure and register IOTSB with HV */
	err = pci_sun4v_iotsb_conf(pbm->devhandle,
				   iotsb->ra,
				   iotsb->table_size,
				   iotsb->page_size,
				   iotsb->dvma_base,
				   &iotsb_num);
	if (err) {
		pr_err(PFX "pci_iotsb_conf failed error: %ld\n", err);
		goto iotsb_conf_failed;
	}
	iotsb->iotsb_num = iotsb_num;

	err = dma_4v_iotsb_bind(pbm->devhandle, iotsb_num, pbm->pci_bus);
	if (err) {
		pr_err(PFX "pci_iotsb_bind failed error: %ld\n", err);
		goto iotsb_conf_failed;
	}

	return 0;

iotsb_conf_failed:
	free_pages((unsigned long)table, order);
table_failed:
	kfree(iotsb);
out_err:
	return err;
}

static int pci_sun4v_atu_init(struct pci_pbm_info *pbm)
{
	struct atu *atu = pbm->iommu->atu;
	unsigned long err;
	const u64 *ranges;
	u64 map_size, num_iotte;
	u64 dma_mask;
	const u32 *page_size;
	int len;

	ranges = of_get_property(pbm->op->dev.of_node, "iommu-address-ranges",
				 &len);
	if (!ranges) {
		pr_err(PFX "No iommu-address-ranges\n");
		return -EINVAL;
	}

	page_size = of_get_property(pbm->op->dev.of_node, "iommu-pagesizes",
				    NULL);
	if (!page_size) {
		pr_err(PFX "No iommu-pagesizes\n");
		return -EINVAL;
	}

	/* There are 4 iommu-address-ranges supported. Each range is pair of
	 * {base, size}. The ranges[0] and ranges[1] are 32bit address space
	 * while ranges[2] and ranges[3] are 64bit space.  We want to use 64bit
	 * address ranges to support 64bit addressing. Because 'size' for
	 * address ranges[2] and ranges[3] are same we can select either of
	 * ranges[2] or ranges[3] for mapping. However due to 'size' is too
	 * large for OS to allocate IOTSB we are using fix size 32G
	 * (ATU_64_SPACE_SIZE) which is more than enough for all PCIe devices
	 * to share.
	 */
	atu->ranges = (struct atu_ranges *)ranges;
	atu->base = atu->ranges[3].base;
	atu->size = ATU_64_SPACE_SIZE;

	/* Create IOTSB */
	err = pci_sun4v_atu_alloc_iotsb(pbm);
	if (err) {
		pr_err(PFX "Error creating ATU IOTSB\n");
		return err;
	}

	/* Create ATU iommu map.
	 * One bit represents one iotte in IOTSB table.
	 */
	dma_mask = (roundup_pow_of_two(atu->size) - 1UL);
	num_iotte = atu->size / IO_PAGE_SIZE;
	map_size = num_iotte / 8;
	atu->tbl.table_map_base = atu->base;
	atu->dma_addr_mask = dma_mask;
	atu->tbl.map = kzalloc(map_size, GFP_KERNEL);
	if (!atu->tbl.map)
		return -ENOMEM;

	iommu_tbl_pool_init(&atu->tbl, num_iotte, IO_PAGE_SHIFT,
			    NULL, false /* no large_pool */,
			    0 /* default npools */,
			    false /* want span boundary checking */);

	return 0;
}

static int pci_sun4v_iommu_init(struct pci_pbm_info *pbm)
{
	static const u32 vdma_default[] = { 0x80000000, 0x80000000 };
	struct iommu *iommu = pbm->iommu;
	unsigned long num_tsb_entries, sz;
	u32 dma_mask, dma_offset;
	const u32 *vdma;

	vdma = of_get_property(pbm->op->dev.of_node, "virtual-dma", NULL);
	if (!vdma)
		vdma = vdma_default;

	if ((vdma[0] | vdma[1]) & ~IO_PAGE_MASK) {
		printk(KERN_ERR PFX "Strange virtual-dma[%08x:%08x].\n",
		       vdma[0], vdma[1]);
		return -EINVAL;
	}

	dma_mask = (roundup_pow_of_two(vdma[1]) - 1UL);
	num_tsb_entries = vdma[1] / IO_PAGE_SIZE;

	dma_offset = vdma[0];

	/* Setup initial software IOMMU state. */
	spin_lock_init(&iommu->lock);
	iommu->ctx_lowest_free = 1;
	iommu->tbl.table_map_base = dma_offset;
	iommu->dma_addr_mask = dma_mask;

	/* Allocate and initialize the free area map.  */
	sz = (num_tsb_entries + 7) / 8;
	sz = (sz + 7UL) & ~7UL;
	iommu->tbl.map = kzalloc(sz, GFP_KERNEL);
	if (!iommu->tbl.map) {
		printk(KERN_ERR PFX "Error, kmalloc(arena.map) failed.\n");
		return -ENOMEM;
	}
	iommu_tbl_pool_init(&iommu->tbl, num_tsb_entries, IO_PAGE_SHIFT,
			    NULL, false /* no large_pool */,
			    0 /* default npools */,
			    false /* want span boundary checking */);
	sz = probe_existing_entries(pbm, &iommu->tbl);
	if (sz)
		printk("%s: Imported %lu TSB entries from OBP\n",
		       pbm->name, sz);

	return 0;
}

#ifdef CONFIG_PCI_MSI
struct pci_sun4v_msiq_entry {
	u64		version_type;
#define MSIQ_VERSION_MASK		0xffffffff00000000UL
#define MSIQ_VERSION_SHIFT		32
#define MSIQ_TYPE_MASK			0x00000000000000ffUL
#define MSIQ_TYPE_SHIFT			0
#define MSIQ_TYPE_NONE			0x00
#define MSIQ_TYPE_MSG			0x01
#define MSIQ_TYPE_MSI32			0x02
#define MSIQ_TYPE_MSI64			0x03
#define MSIQ_TYPE_INTX			0x08
#define MSIQ_TYPE_NONE2			0xff

	u64		intx_sysino;
	u64		reserved1;
	u64		stick;
	u64		req_id;  /* bus/device/func */
#define MSIQ_REQID_BUS_MASK		0xff00UL
#define MSIQ_REQID_BUS_SHIFT		8
#define MSIQ_REQID_DEVICE_MASK		0x00f8UL
#define MSIQ_REQID_DEVICE_SHIFT		3
#define MSIQ_REQID_FUNC_MASK		0x0007UL
#define MSIQ_REQID_FUNC_SHIFT		0

	u64		msi_address;

	/* The format of this value is message type dependent.
	 * For MSI bits 15:0 are the data from the MSI packet.
	 * For MSI-X bits 31:0 are the data from the MSI packet.
	 * For MSG, the message code and message routing code where:
	 * 	bits 39:32 is the bus/device/fn of the msg target-id
	 *	bits 18:16 is the message routing code
	 *	bits 7:0 is the message code
	 * For INTx the low order 2-bits are:
	 *	00 - INTA
	 *	01 - INTB
	 *	10 - INTC
	 *	11 - INTD
	 */
	u64		msi_data;

	u64		reserved2;
};

static int pci_sun4v_get_head(struct pci_pbm_info *pbm, unsigned long msiqid,
			      unsigned long *head)
{
	unsigned long err, limit;

	err = pci_sun4v_msiq_gethead(pbm->devhandle, msiqid, head);
	if (unlikely(err))
		return -ENXIO;

	limit = pbm->msiq_ent_count * sizeof(struct pci_sun4v_msiq_entry);
	if (unlikely(*head >= limit))
		return -EFBIG;

	return 0;
}

static int pci_sun4v_dequeue_msi(struct pci_pbm_info *pbm,
				 unsigned long msiqid, unsigned long *head,
				 unsigned long *msi)
{
	struct pci_sun4v_msiq_entry *ep;
	unsigned long err, type;

	/* Note: void pointer arithmetic, 'head' is a byte offset  */
	ep = (pbm->msi_queues + ((msiqid - pbm->msiq_first) *
				 (pbm->msiq_ent_count *
				  sizeof(struct pci_sun4v_msiq_entry))) +
	      *head);

	if ((ep->version_type & MSIQ_TYPE_MASK) == 0)
		return 0;

	type = (ep->version_type & MSIQ_TYPE_MASK) >> MSIQ_TYPE_SHIFT;
	if (unlikely(type != MSIQ_TYPE_MSI32 &&
		     type != MSIQ_TYPE_MSI64))
		return -EINVAL;

	*msi = ep->msi_data;

	err = pci_sun4v_msi_setstate(pbm->devhandle,
				     ep->msi_data /* msi_num */,
				     HV_MSISTATE_IDLE);
	if (unlikely(err))
		return -ENXIO;

	/* Clear the entry.  */
	ep->version_type &= ~MSIQ_TYPE_MASK;

	(*head) += sizeof(struct pci_sun4v_msiq_entry);
	if (*head >=
	    (pbm->msiq_ent_count * sizeof(struct pci_sun4v_msiq_entry)))
		*head = 0;

	return 1;
}

static int pci_sun4v_set_head(struct pci_pbm_info *pbm, unsigned long msiqid,
			      unsigned long head)
{
	unsigned long err;

	err = pci_sun4v_msiq_sethead(pbm->devhandle, msiqid, head);
	if (unlikely(err))
		return -EINVAL;

	return 0;
}

static int pci_sun4v_msi_setup(struct pci_pbm_info *pbm, unsigned long msiqid,
			       unsigned long msi, int is_msi64)
{
	if (pci_sun4v_msi_setmsiq(pbm->devhandle, msi, msiqid,
				  (is_msi64 ?
				   HV_MSITYPE_MSI64 : HV_MSITYPE_MSI32)))
		return -ENXIO;
	if (pci_sun4v_msi_setstate(pbm->devhandle, msi, HV_MSISTATE_IDLE))
		return -ENXIO;
	if (pci_sun4v_msi_setvalid(pbm->devhandle, msi, HV_MSIVALID_VALID))
		return -ENXIO;
	return 0;
}

static int pci_sun4v_msi_teardown(struct pci_pbm_info *pbm, unsigned long msi)
{
	unsigned long err, msiqid;

	err = pci_sun4v_msi_getmsiq(pbm->devhandle, msi, &msiqid);
	if (err)
		return -ENXIO;

	pci_sun4v_msi_setvalid(pbm->devhandle, msi, HV_MSIVALID_INVALID);

	return 0;
}

static int pci_sun4v_msiq_alloc(struct pci_pbm_info *pbm)
{
	unsigned long q_size, alloc_size, pages, order;
	int i;

	q_size = pbm->msiq_ent_count * sizeof(struct pci_sun4v_msiq_entry);
	alloc_size = (pbm->msiq_num * q_size);
	order = get_order(alloc_size);
	pages = __get_free_pages(GFP_KERNEL | __GFP_COMP, order);
	if (pages == 0UL) {
		printk(KERN_ERR "MSI: Cannot allocate MSI queues (o=%lu).\n",
		       order);
		return -ENOMEM;
	}
	memset((char *)pages, 0, PAGE_SIZE << order);
	pbm->msi_queues = (void *) pages;

	for (i = 0; i < pbm->msiq_num; i++) {
		unsigned long err, base = __pa(pages + (i * q_size));
		unsigned long ret1, ret2;

		err = pci_sun4v_msiq_conf(pbm->devhandle,
					  pbm->msiq_first + i,
					  base, pbm->msiq_ent_count);
		if (err) {
			printk(KERN_ERR "MSI: msiq register fails (err=%lu)\n",
			       err);
			goto h_error;
		}

		err = pci_sun4v_msiq_info(pbm->devhandle,
					  pbm->msiq_first + i,
					  &ret1, &ret2);
		if (err) {
			printk(KERN_ERR "MSI: Cannot read msiq (err=%lu)\n",
			       err);
			goto h_error;
		}
		if (ret1 != base || ret2 != pbm->msiq_ent_count) {
			printk(KERN_ERR "MSI: Bogus qconf "
			       "expected[%lx:%x] got[%lx:%lx]\n",
			       base, pbm->msiq_ent_count,
			       ret1, ret2);
			goto h_error;
		}
	}

	return 0;

h_error:
	free_pages(pages, order);
	return -EINVAL;
}

static void pci_sun4v_msiq_free(struct pci_pbm_info *pbm)
{
	unsigned long q_size, alloc_size, pages, order;
	int i;

	for (i = 0; i < pbm->msiq_num; i++) {
		unsigned long msiqid = pbm->msiq_first + i;

		(void) pci_sun4v_msiq_conf(pbm->devhandle, msiqid, 0UL, 0);
	}

	q_size = pbm->msiq_ent_count * sizeof(struct pci_sun4v_msiq_entry);
	alloc_size = (pbm->msiq_num * q_size);
	order = get_order(alloc_size);

	pages = (unsigned long) pbm->msi_queues;

	free_pages(pages, order);

	pbm->msi_queues = NULL;
}

static int pci_sun4v_msiq_build_irq(struct pci_pbm_info *pbm,
				    unsigned long msiqid,
				    unsigned long devino)
{
	unsigned int irq = sun4v_build_irq(pbm->devhandle, devino);

	if (!irq)
		return -ENOMEM;

	if (pci_sun4v_msiq_setvalid(pbm->devhandle, msiqid, HV_MSIQ_VALID))
		return -EINVAL;
	if (pci_sun4v_msiq_setstate(pbm->devhandle, msiqid, HV_MSIQSTATE_IDLE))
		return -EINVAL;

	return irq;
}

static const struct sparc64_msiq_ops pci_sun4v_msiq_ops = {
	.get_head	=	pci_sun4v_get_head,
	.dequeue_msi	=	pci_sun4v_dequeue_msi,
	.set_head	=	pci_sun4v_set_head,
	.msi_setup	=	pci_sun4v_msi_setup,
	.msi_teardown	=	pci_sun4v_msi_teardown,
	.msiq_alloc	=	pci_sun4v_msiq_alloc,
	.msiq_free	=	pci_sun4v_msiq_free,
	.msiq_build_irq	=	pci_sun4v_msiq_build_irq,
};

static void pci_sun4v_msi_init(struct pci_pbm_info *pbm)
{
	sparc64_pbm_msi_init(pbm, &pci_sun4v_msiq_ops);
}
#else /* CONFIG_PCI_MSI */
static void pci_sun4v_msi_init(struct pci_pbm_info *pbm)
{
}
#endif /* !(CONFIG_PCI_MSI) */

static int pci_sun4v_pbm_init(struct pci_pbm_info *pbm,
			      struct platform_device *op, u32 devhandle)
{
	struct device_node *dp = op->dev.of_node;
	int err;

	pbm->numa_node = of_node_to_nid(dp);

	pbm->pci_ops = &sun4v_pci_ops;
	pbm->config_space_reg_bits = 12;

	pbm->index = pci_num_pbms++;

	pbm->op = op;

	pbm->devhandle = devhandle;

	pbm->name = dp->full_name;

	printk("%s: SUN4V PCI Bus Module\n", pbm->name);
	printk("%s: On NUMA node %d\n", pbm->name, pbm->numa_node);

	pci_determine_mem_io_space(pbm);

	pci_get_pbm_props(pbm);

	err = pci_sun4v_iommu_init(pbm);
	if (err)
		return err;

	pci_sun4v_msi_init(pbm);

	pci_sun4v_scan_bus(pbm, &op->dev);

	/* if atu_init fails its not complete failure.
	 * we can still continue using legacy iommu.
	 */
	if (pbm->iommu->atu) {
		err = pci_sun4v_atu_init(pbm);
		if (err) {
			kfree(pbm->iommu->atu);
			pbm->iommu->atu = NULL;
			pr_err(PFX "ATU init failed, err=%d\n", err);
		}
	}

	pbm->next = pci_pbm_root;
	pci_pbm_root = pbm;

	return 0;
}

static int pci_sun4v_probe(struct platform_device *op)
{
	const struct linux_prom64_registers *regs;
	static int hvapi_negotiated = 0;
	struct pci_pbm_info *pbm;
	struct device_node *dp;
	struct iommu *iommu;
	struct atu *atu;
	u32 devhandle;
	int i, err = -ENODEV;
	static bool hv_atu = true;

	dp = op->dev.of_node;

	if (!hvapi_negotiated++) {
		for (i = 0; i < ARRAY_SIZE(vpci_versions); i++) {
			vpci_major = vpci_versions[i].major;
			vpci_minor = vpci_versions[i].minor;

			err = sun4v_hvapi_register(HV_GRP_PCI, vpci_major,
						   &vpci_minor);
			if (!err)
				break;
		}

		if (err) {
			pr_err(PFX "Could not register hvapi, err=%d\n", err);
			return err;
		}
		pr_info(PFX "Registered hvapi major[%lu] minor[%lu]\n",
			vpci_major, vpci_minor);

		err = sun4v_hvapi_register(HV_GRP_ATU, vatu_major, &vatu_minor);
		if (err) {
			/* don't return an error if we fail to register the
			 * ATU group, but ATU hcalls won't be available.
			 */
			hv_atu = false;
		} else {
			pr_info(PFX "Registered hvapi ATU major[%lu] minor[%lu]\n",
				vatu_major, vatu_minor);
		}

		dma_ops = &sun4v_dma_ops;
	}

	regs = of_get_property(dp, "reg", NULL);
	err = -ENODEV;
	if (!regs) {
		printk(KERN_ERR PFX "Could not find config registers\n");
		goto out_err;
	}
	devhandle = (regs->phys_addr >> 32UL) & 0x0fffffff;

	err = -ENOMEM;
	if (!iommu_batch_initialized) {
		for_each_possible_cpu(i) {
			unsigned long page = get_zeroed_page(GFP_KERNEL);

			if (!page)
				goto out_err;

			per_cpu(iommu_batch, i).pglist = (u64 *) page;
		}
		iommu_batch_initialized = 1;
	}

	pbm = kzalloc(sizeof(*pbm), GFP_KERNEL);
	if (!pbm) {
		printk(KERN_ERR PFX "Could not allocate pci_pbm_info\n");
		goto out_err;
	}

	iommu = kzalloc(sizeof(struct iommu), GFP_KERNEL);
	if (!iommu) {
		printk(KERN_ERR PFX "Could not allocate pbm iommu\n");
		goto out_free_controller;
	}

	pbm->iommu = iommu;
	iommu->atu = NULL;
	if (hv_atu) {
		atu = kzalloc(sizeof(*atu), GFP_KERNEL);
		if (!atu)
			pr_err(PFX "Could not allocate atu\n");
		else
			iommu->atu = atu;
	}

	err = pci_sun4v_pbm_init(pbm, op, devhandle);
	if (err)
		goto out_free_iommu;

	dev_set_drvdata(&op->dev, pbm);

	return 0;

out_free_iommu:
	kfree(iommu->atu);
	kfree(pbm->iommu);

out_free_controller:
	kfree(pbm);

out_err:
	return err;
}

static const struct of_device_id pci_sun4v_match[] = {
	{
		.name = "pci",
		.compatible = "SUNW,sun4v-pci",
	},
	{},
};

static struct platform_driver pci_sun4v_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = pci_sun4v_match,
	},
	.probe		= pci_sun4v_probe,
};

static int __init pci_sun4v_init(void)
{
	return platform_driver_register(&pci_sun4v_driver);
}

subsys_initcall(pci_sun4v_init);
