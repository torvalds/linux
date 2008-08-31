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
#include <linux/log2.h>
#include <linux/of_device.h>

#include <asm/iommu.h>
#include <asm/irq.h>
#include <asm/hypervisor.h>
#include <asm/prom.h>

#include "pci_impl.h"
#include "iommu_common.h"

#include "pci_sun4v.h"

#define DRIVER_NAME	"pci_sun4v"
#define PFX		DRIVER_NAME ": "

static unsigned long vpci_major = 1;
static unsigned long vpci_minor = 1;

#define PGLIST_NENTS	(PAGE_SIZE / sizeof(u64))

struct iommu_batch {
	struct device	*dev;		/* Device mapping is for.	*/
	unsigned long	prot;		/* IOMMU page protections	*/
	unsigned long	entry;		/* Index into IOTSB.		*/
	u64		*pglist;	/* List of physical pages	*/
	unsigned long	npages;		/* Number of pages in list.	*/
};

static DEFINE_PER_CPU(struct iommu_batch, iommu_batch);

/* Interrupts must be disabled.  */
static inline void iommu_batch_start(struct device *dev, unsigned long prot, unsigned long entry)
{
	struct iommu_batch *p = &__get_cpu_var(iommu_batch);

	p->dev		= dev;
	p->prot		= prot;
	p->entry	= entry;
	p->npages	= 0;
}

/* Interrupts must be disabled.  */
static long iommu_batch_flush(struct iommu_batch *p)
{
	struct pci_pbm_info *pbm = p->dev->archdata.host_controller;
	unsigned long devhandle = pbm->devhandle;
	unsigned long prot = p->prot;
	unsigned long entry = p->entry;
	u64 *pglist = p->pglist;
	unsigned long npages = p->npages;

	while (npages != 0) {
		long num;

		num = pci_sun4v_iommu_map(devhandle, HV_PCI_TSBID(0, entry),
					  npages, prot, __pa(pglist));
		if (unlikely(num < 0)) {
			if (printk_ratelimit())
				printk("iommu_batch_flush: IOMMU map of "
				       "[%08lx:%08lx:%lx:%lx:%lx] failed with "
				       "status %ld\n",
				       devhandle, HV_PCI_TSBID(0, entry),
				       npages, prot, __pa(pglist), num);
			return -1;
		}

		entry += num;
		npages -= num;
		pglist += num;
	}

	p->entry = entry;
	p->npages = 0;

	return 0;
}

static inline void iommu_batch_new_entry(unsigned long entry)
{
	struct iommu_batch *p = &__get_cpu_var(iommu_batch);

	if (p->entry + p->npages == entry)
		return;
	if (p->entry != ~0UL)
		iommu_batch_flush(p);
	p->entry = entry;
}

/* Interrupts must be disabled.  */
static inline long iommu_batch_add(u64 phys_page)
{
	struct iommu_batch *p = &__get_cpu_var(iommu_batch);

	BUG_ON(p->npages >= PGLIST_NENTS);

	p->pglist[p->npages++] = phys_page;
	if (p->npages == PGLIST_NENTS)
		return iommu_batch_flush(p);

	return 0;
}

/* Interrupts must be disabled.  */
static inline long iommu_batch_end(void)
{
	struct iommu_batch *p = &__get_cpu_var(iommu_batch);

	BUG_ON(p->npages >= PGLIST_NENTS);

	return iommu_batch_flush(p);
}

static void *dma_4v_alloc_coherent(struct device *dev, size_t size,
				   dma_addr_t *dma_addrp, gfp_t gfp)
{
	unsigned long flags, order, first_page, npages, n;
	struct iommu *iommu;
	struct page *page;
	void *ret;
	long entry;
	int nid;

	size = IO_PAGE_ALIGN(size);
	order = get_order(size);
	if (unlikely(order >= MAX_ORDER))
		return NULL;

	npages = size >> IO_PAGE_SHIFT;

	nid = dev->archdata.numa_node;
	page = alloc_pages_node(nid, gfp, order);
	if (unlikely(!page))
		return NULL;

	first_page = (unsigned long) page_address(page);
	memset((char *)first_page, 0, PAGE_SIZE << order);

	iommu = dev->archdata.iommu;

	spin_lock_irqsave(&iommu->lock, flags);
	entry = iommu_range_alloc(dev, iommu, npages, NULL);
	spin_unlock_irqrestore(&iommu->lock, flags);

	if (unlikely(entry == DMA_ERROR_CODE))
		goto range_alloc_fail;

	*dma_addrp = (iommu->page_table_map_base +
		      (entry << IO_PAGE_SHIFT));
	ret = (void *) first_page;
	first_page = __pa(first_page);

	local_irq_save(flags);

	iommu_batch_start(dev,
			  (HV_PCI_MAP_ATTR_READ |
			   HV_PCI_MAP_ATTR_WRITE),
			  entry);

	for (n = 0; n < npages; n++) {
		long err = iommu_batch_add(first_page + (n * PAGE_SIZE));
		if (unlikely(err < 0L))
			goto iommu_map_fail;
	}

	if (unlikely(iommu_batch_end() < 0L))
		goto iommu_map_fail;

	local_irq_restore(flags);

	return ret;

iommu_map_fail:
	/* Interrupts are disabled.  */
	spin_lock(&iommu->lock);
	iommu_range_free(iommu, *dma_addrp, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);

range_alloc_fail:
	free_pages(first_page, order);
	return NULL;
}

static void dma_4v_free_coherent(struct device *dev, size_t size, void *cpu,
				 dma_addr_t dvma)
{
	struct pci_pbm_info *pbm;
	struct iommu *iommu;
	unsigned long flags, order, npages, entry;
	u32 devhandle;

	npages = IO_PAGE_ALIGN(size) >> IO_PAGE_SHIFT;
	iommu = dev->archdata.iommu;
	pbm = dev->archdata.host_controller;
	devhandle = pbm->devhandle;
	entry = ((dvma - iommu->page_table_map_base) >> IO_PAGE_SHIFT);

	spin_lock_irqsave(&iommu->lock, flags);

	iommu_range_free(iommu, dvma, npages);

	do {
		unsigned long num;

		num = pci_sun4v_iommu_demap(devhandle, HV_PCI_TSBID(0, entry),
					    npages);
		entry += num;
		npages -= num;
	} while (npages != 0);

	spin_unlock_irqrestore(&iommu->lock, flags);

	order = get_order(size);
	if (order < 10)
		free_pages((unsigned long)cpu, order);
}

static dma_addr_t dma_4v_map_single(struct device *dev, void *ptr, size_t sz,
				    enum dma_data_direction direction)
{
	struct iommu *iommu;
	unsigned long flags, npages, oaddr;
	unsigned long i, base_paddr;
	u32 bus_addr, ret;
	unsigned long prot;
	long entry;

	iommu = dev->archdata.iommu;

	if (unlikely(direction == DMA_NONE))
		goto bad;

	oaddr = (unsigned long)ptr;
	npages = IO_PAGE_ALIGN(oaddr + sz) - (oaddr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;

	spin_lock_irqsave(&iommu->lock, flags);
	entry = iommu_range_alloc(dev, iommu, npages, NULL);
	spin_unlock_irqrestore(&iommu->lock, flags);

	if (unlikely(entry == DMA_ERROR_CODE))
		goto bad;

	bus_addr = (iommu->page_table_map_base +
		    (entry << IO_PAGE_SHIFT));
	ret = bus_addr | (oaddr & ~IO_PAGE_MASK);
	base_paddr = __pa(oaddr & IO_PAGE_MASK);
	prot = HV_PCI_MAP_ATTR_READ;
	if (direction != DMA_TO_DEVICE)
		prot |= HV_PCI_MAP_ATTR_WRITE;

	local_irq_save(flags);

	iommu_batch_start(dev, prot, entry);

	for (i = 0; i < npages; i++, base_paddr += IO_PAGE_SIZE) {
		long err = iommu_batch_add(base_paddr);
		if (unlikely(err < 0L))
			goto iommu_map_fail;
	}
	if (unlikely(iommu_batch_end() < 0L))
		goto iommu_map_fail;

	local_irq_restore(flags);

	return ret;

bad:
	if (printk_ratelimit())
		WARN_ON(1);
	return DMA_ERROR_CODE;

iommu_map_fail:
	/* Interrupts are disabled.  */
	spin_lock(&iommu->lock);
	iommu_range_free(iommu, bus_addr, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);

	return DMA_ERROR_CODE;
}

static void dma_4v_unmap_single(struct device *dev, dma_addr_t bus_addr,
				size_t sz, enum dma_data_direction direction)
{
	struct pci_pbm_info *pbm;
	struct iommu *iommu;
	unsigned long flags, npages;
	long entry;
	u32 devhandle;

	if (unlikely(direction == DMA_NONE)) {
		if (printk_ratelimit())
			WARN_ON(1);
		return;
	}

	iommu = dev->archdata.iommu;
	pbm = dev->archdata.host_controller;
	devhandle = pbm->devhandle;

	npages = IO_PAGE_ALIGN(bus_addr + sz) - (bus_addr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;
	bus_addr &= IO_PAGE_MASK;

	spin_lock_irqsave(&iommu->lock, flags);

	iommu_range_free(iommu, bus_addr, npages);

	entry = (bus_addr - iommu->page_table_map_base) >> IO_PAGE_SHIFT;
	do {
		unsigned long num;

		num = pci_sun4v_iommu_demap(devhandle, HV_PCI_TSBID(0, entry),
					    npages);
		entry += num;
		npages -= num;
	} while (npages != 0);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

static int dma_4v_map_sg(struct device *dev, struct scatterlist *sglist,
			 int nelems, enum dma_data_direction direction)
{
	struct scatterlist *s, *outs, *segstart;
	unsigned long flags, handle, prot;
	dma_addr_t dma_next = 0, dma_addr;
	unsigned int max_seg_size;
	unsigned long seg_boundary_size;
	int outcount, incount, i;
	struct iommu *iommu;
	unsigned long base_shift;
	long err;

	BUG_ON(direction == DMA_NONE);

	iommu = dev->archdata.iommu;
	if (nelems == 0 || !iommu)
		return 0;
	
	prot = HV_PCI_MAP_ATTR_READ;
	if (direction != DMA_TO_DEVICE)
		prot |= HV_PCI_MAP_ATTR_WRITE;

	outs = s = segstart = &sglist[0];
	outcount = 1;
	incount = nelems;
	handle = 0;

	/* Init first segment length for backout at failure */
	outs->dma_length = 0;

	spin_lock_irqsave(&iommu->lock, flags);

	iommu_batch_start(dev, prot, ~0UL);

	max_seg_size = dma_get_max_seg_size(dev);
	seg_boundary_size = ALIGN(dma_get_seg_boundary(dev) + 1,
				  IO_PAGE_SIZE) >> IO_PAGE_SHIFT;
	base_shift = iommu->page_table_map_base >> IO_PAGE_SHIFT;
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
		npages = iommu_num_pages(paddr, slen);
		entry = iommu_range_alloc(dev, iommu, npages, &handle);

		/* Handle failure */
		if (unlikely(entry == DMA_ERROR_CODE)) {
			if (printk_ratelimit())
				printk(KERN_INFO "iommu_alloc failed, iommu %p paddr %lx"
				       " npages %lx\n", iommu, paddr, npages);
			goto iommu_map_failed;
		}

		iommu_batch_new_entry(entry);

		/* Convert entry to a dma_addr_t */
		dma_addr = iommu->page_table_map_base +
			(entry << IO_PAGE_SHIFT);
		dma_addr |= (s->offset & ~IO_PAGE_MASK);

		/* Insert into HW table */
		paddr &= IO_PAGE_MASK;
		while (npages--) {
			err = iommu_batch_add(paddr);
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

	err = iommu_batch_end();

	if (unlikely(err < 0L))
		goto iommu_map_failed;

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
			unsigned long vaddr, npages;

			vaddr = s->dma_address & IO_PAGE_MASK;
			npages = iommu_num_pages(s->dma_address, s->dma_length);
			iommu_range_free(iommu, vaddr, npages);
			/* XXX demap? XXX */
			s->dma_address = DMA_ERROR_CODE;
			s->dma_length = 0;
		}
		if (s == outs)
			break;
	}
	spin_unlock_irqrestore(&iommu->lock, flags);

	return 0;
}

static void dma_4v_unmap_sg(struct device *dev, struct scatterlist *sglist,
			    int nelems, enum dma_data_direction direction)
{
	struct pci_pbm_info *pbm;
	struct scatterlist *sg;
	struct iommu *iommu;
	unsigned long flags;
	u32 devhandle;

	BUG_ON(direction == DMA_NONE);

	iommu = dev->archdata.iommu;
	pbm = dev->archdata.host_controller;
	devhandle = pbm->devhandle;
	
	spin_lock_irqsave(&iommu->lock, flags);

	sg = sglist;
	while (nelems--) {
		dma_addr_t dma_handle = sg->dma_address;
		unsigned int len = sg->dma_length;
		unsigned long npages, entry;

		if (!len)
			break;
		npages = iommu_num_pages(dma_handle, len);
		iommu_range_free(iommu, dma_handle, npages);

		entry = ((dma_handle - iommu->page_table_map_base) >> IO_PAGE_SHIFT);
		while (npages) {
			unsigned long num;

			num = pci_sun4v_iommu_demap(devhandle, HV_PCI_TSBID(0, entry),
						    npages);
			entry += num;
			npages -= num;
		}

		sg = sg_next(sg);
	}

	spin_unlock_irqrestore(&iommu->lock, flags);
}

static void dma_4v_sync_single_for_cpu(struct device *dev,
				       dma_addr_t bus_addr, size_t sz,
				       enum dma_data_direction direction)
{
	/* Nothing to do... */
}

static void dma_4v_sync_sg_for_cpu(struct device *dev,
				   struct scatterlist *sglist, int nelems,
				   enum dma_data_direction direction)
{
	/* Nothing to do... */
}

static const struct dma_ops sun4v_dma_ops = {
	.alloc_coherent			= dma_4v_alloc_coherent,
	.free_coherent			= dma_4v_free_coherent,
	.map_single			= dma_4v_map_single,
	.unmap_single			= dma_4v_unmap_single,
	.map_sg				= dma_4v_map_sg,
	.unmap_sg			= dma_4v_unmap_sg,
	.sync_single_for_cpu		= dma_4v_sync_single_for_cpu,
	.sync_sg_for_cpu		= dma_4v_sync_sg_for_cpu,
};

static void __init pci_sun4v_scan_bus(struct pci_pbm_info *pbm)
{
	struct property *prop;
	struct device_node *dp;

	dp = pbm->prom_node;
	prop = of_find_property(dp, "66mhz-capable", NULL);
	pbm->is_66mhz_capable = (prop != NULL);
	pbm->pci_bus = pci_scan_one_pbm(pbm);

	/* XXX register error interrupt handlers XXX */
}

static unsigned long __init probe_existing_entries(struct pci_pbm_info *pbm,
						   struct iommu *iommu)
{
	struct iommu_arena *arena = &iommu->arena;
	unsigned long i, cnt = 0;
	u32 devhandle;

	devhandle = pbm->devhandle;
	for (i = 0; i < arena->limit; i++) {
		unsigned long ret, io_attrs, ra;

		ret = pci_sun4v_iommu_getmap(devhandle,
					     HV_PCI_TSBID(0, i),
					     &io_attrs, &ra);
		if (ret == HV_EOK) {
			if (page_in_phys_avail(ra)) {
				pci_sun4v_iommu_demap(devhandle,
						      HV_PCI_TSBID(0, i), 1);
			} else {
				cnt++;
				__set_bit(i, arena->map);
			}
		}
	}

	return cnt;
}

static int __init pci_sun4v_iommu_init(struct pci_pbm_info *pbm)
{
	struct iommu *iommu = pbm->iommu;
	struct property *prop;
	unsigned long num_tsb_entries, sz, tsbsize;
	u32 vdma[2], dma_mask, dma_offset;

	prop = of_find_property(pbm->prom_node, "virtual-dma", NULL);
	if (prop) {
		u32 *val = prop->value;

		vdma[0] = val[0];
		vdma[1] = val[1];
	} else {
		/* No property, use default values. */
		vdma[0] = 0x80000000;
		vdma[1] = 0x80000000;
	}

	if ((vdma[0] | vdma[1]) & ~IO_PAGE_MASK) {
		printk(KERN_ERR PFX "Strange virtual-dma[%08x:%08x].\n",
		       vdma[0], vdma[1]);
		return -EINVAL;
	};

	dma_mask = (roundup_pow_of_two(vdma[1]) - 1UL);
	num_tsb_entries = vdma[1] / IO_PAGE_SIZE;
	tsbsize = num_tsb_entries * sizeof(iopte_t);

	dma_offset = vdma[0];

	/* Setup initial software IOMMU state. */
	spin_lock_init(&iommu->lock);
	iommu->ctx_lowest_free = 1;
	iommu->page_table_map_base = dma_offset;
	iommu->dma_addr_mask = dma_mask;

	/* Allocate and initialize the free area map.  */
	sz = (num_tsb_entries + 7) / 8;
	sz = (sz + 7UL) & ~7UL;
	iommu->arena.map = kzalloc(sz, GFP_KERNEL);
	if (!iommu->arena.map) {
		printk(KERN_ERR PFX "Error, kmalloc(arena.map) failed.\n");
		return -ENOMEM;
	}
	iommu->arena.limit = num_tsb_entries;

	sz = probe_existing_entries(pbm, iommu);
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
	unsigned int virt_irq = sun4v_build_irq(pbm->devhandle, devino);

	if (!virt_irq)
		return -ENOMEM;

	if (pci_sun4v_msiq_setstate(pbm->devhandle, msiqid, HV_MSIQSTATE_IDLE))
		return -EINVAL;
	if (pci_sun4v_msiq_setvalid(pbm->devhandle, msiqid, HV_MSIQ_VALID))
		return -EINVAL;

	return virt_irq;
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

static int __init pci_sun4v_pbm_init(struct pci_controller_info *p,
				     struct device_node *dp, u32 devhandle)
{
	struct pci_pbm_info *pbm;
	int err;

	if (devhandle & 0x40)
		pbm = &p->pbm_B;
	else
		pbm = &p->pbm_A;

	pbm->next = pci_pbm_root;
	pci_pbm_root = pbm;

	pbm->numa_node = of_node_to_nid(dp);

	pbm->pci_ops = &sun4v_pci_ops;
	pbm->config_space_reg_bits = 12;

	pbm->index = pci_num_pbms++;

	pbm->parent = p;
	pbm->prom_node = dp;

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

	pci_sun4v_scan_bus(pbm);

	return 0;
}

static int __devinit pci_sun4v_probe(struct of_device *op,
				     const struct of_device_id *match)
{
	const struct linux_prom64_registers *regs;
	static int hvapi_negotiated = 0;
	struct pci_controller_info *p;
	struct pci_pbm_info *pbm;
	struct device_node *dp;
	struct iommu *iommu;
	u32 devhandle;
	int i;

	dp = op->node;

	if (!hvapi_negotiated++) {
		int err = sun4v_hvapi_register(HV_GRP_PCI,
					       vpci_major,
					       &vpci_minor);

		if (err) {
			printk(KERN_ERR PFX "Could not register hvapi, "
			       "err=%d\n", err);
			return err;
		}
		printk(KERN_INFO PFX "Registered hvapi major[%lu] minor[%lu]\n",
		       vpci_major, vpci_minor);

		dma_ops = &sun4v_dma_ops;
	}

	regs = of_get_property(dp, "reg", NULL);
	if (!regs) {
		printk(KERN_ERR PFX "Could not find config registers\n");
		return -ENODEV;
	}
	devhandle = (regs->phys_addr >> 32UL) & 0x0fffffff;

	for (pbm = pci_pbm_root; pbm; pbm = pbm->next) {
		if (pbm->devhandle == (devhandle ^ 0x40)) {
			return pci_sun4v_pbm_init(pbm->parent, dp, devhandle);
		}
	}

	for_each_possible_cpu(i) {
		unsigned long page = get_zeroed_page(GFP_ATOMIC);

		if (!page)
			return -ENOMEM;

		per_cpu(iommu_batch, i).pglist = (u64 *) page;
	}

	p = kzalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p) {
		printk(KERN_ERR PFX "Could not allocate pci_controller_info\n");
		goto out_free;
	}

	iommu = kzalloc(sizeof(struct iommu), GFP_ATOMIC);
	if (!iommu) {
		printk(KERN_ERR PFX "Could not allocate pbm A iommu\n");
		goto out_free;
	}

	p->pbm_A.iommu = iommu;

	iommu = kzalloc(sizeof(struct iommu), GFP_ATOMIC);
	if (!iommu) {
		printk(KERN_ERR PFX "Could not allocate pbm B iommu\n");
		goto out_free;
	}

	p->pbm_B.iommu = iommu;

	return pci_sun4v_pbm_init(p, dp, devhandle);

out_free:
	if (p) {
		if (p->pbm_A.iommu)
			kfree(p->pbm_A.iommu);
		if (p->pbm_B.iommu)
			kfree(p->pbm_B.iommu);
		kfree(p);
	}
	return -ENOMEM;
}

static struct of_device_id __initdata pci_sun4v_match[] = {
	{
		.name = "pci",
		.compatible = "SUNW,sun4v-pci",
	},
	{},
};

static struct of_platform_driver pci_sun4v_driver = {
	.name		= DRIVER_NAME,
	.match_table	= pci_sun4v_match,
	.probe		= pci_sun4v_probe,
};

static int __init pci_sun4v_init(void)
{
	return of_register_driver(&pci_sun4v_driver, &of_bus_type);
}

subsys_initcall(pci_sun4v_init);
