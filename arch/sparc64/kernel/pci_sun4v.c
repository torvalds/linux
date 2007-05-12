/* pci_sun4v.c: SUN4V specific PCI controller support.
 *
 * Copyright (C) 2006, 2007 David S. Miller (davem@davemloft.net)
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

#include <asm/iommu.h>
#include <asm/irq.h>
#include <asm/upa.h>
#include <asm/pstate.h>
#include <asm/oplib.h>
#include <asm/hypervisor.h>
#include <asm/prom.h>

#include "pci_impl.h"
#include "iommu_common.h"

#include "pci_sun4v.h"

#define PGLIST_NENTS	(PAGE_SIZE / sizeof(u64))

struct iommu_batch {
	struct pci_dev	*pdev;		/* Device mapping is for.	*/
	unsigned long	prot;		/* IOMMU page protections	*/
	unsigned long	entry;		/* Index into IOTSB.		*/
	u64		*pglist;	/* List of physical pages	*/
	unsigned long	npages;		/* Number of pages in list.	*/
};

static DEFINE_PER_CPU(struct iommu_batch, pci_iommu_batch);

/* Interrupts must be disabled.  */
static inline void pci_iommu_batch_start(struct pci_dev *pdev, unsigned long prot, unsigned long entry)
{
	struct iommu_batch *p = &__get_cpu_var(pci_iommu_batch);

	p->pdev		= pdev;
	p->prot		= prot;
	p->entry	= entry;
	p->npages	= 0;
}

/* Interrupts must be disabled.  */
static long pci_iommu_batch_flush(struct iommu_batch *p)
{
	struct pci_pbm_info *pbm = p->pdev->dev.archdata.host_controller;
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
				printk("pci_iommu_batch_flush: IOMMU map of "
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

/* Interrupts must be disabled.  */
static inline long pci_iommu_batch_add(u64 phys_page)
{
	struct iommu_batch *p = &__get_cpu_var(pci_iommu_batch);

	BUG_ON(p->npages >= PGLIST_NENTS);

	p->pglist[p->npages++] = phys_page;
	if (p->npages == PGLIST_NENTS)
		return pci_iommu_batch_flush(p);

	return 0;
}

/* Interrupts must be disabled.  */
static inline long pci_iommu_batch_end(void)
{
	struct iommu_batch *p = &__get_cpu_var(pci_iommu_batch);

	BUG_ON(p->npages >= PGLIST_NENTS);

	return pci_iommu_batch_flush(p);
}

static long pci_arena_alloc(struct iommu_arena *arena, unsigned long npages)
{
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

static void pci_arena_free(struct iommu_arena *arena, unsigned long base, unsigned long npages)
{
	unsigned long i;

	for (i = base; i < (base + npages); i++)
		__clear_bit(i, arena->map);
}

static void *pci_4v_alloc_consistent(struct pci_dev *pdev, size_t size, dma_addr_t *dma_addrp, gfp_t gfp)
{
	struct iommu *iommu;
	unsigned long flags, order, first_page, npages, n;
	void *ret;
	long entry;

	size = IO_PAGE_ALIGN(size);
	order = get_order(size);
	if (unlikely(order >= MAX_ORDER))
		return NULL;

	npages = size >> IO_PAGE_SHIFT;

	first_page = __get_free_pages(gfp, order);
	if (unlikely(first_page == 0UL))
		return NULL;

	memset((char *)first_page, 0, PAGE_SIZE << order);

	iommu = pdev->dev.archdata.iommu;

	spin_lock_irqsave(&iommu->lock, flags);
	entry = pci_arena_alloc(&iommu->arena, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);

	if (unlikely(entry < 0L))
		goto arena_alloc_fail;

	*dma_addrp = (iommu->page_table_map_base +
		      (entry << IO_PAGE_SHIFT));
	ret = (void *) first_page;
	first_page = __pa(first_page);

	local_irq_save(flags);

	pci_iommu_batch_start(pdev,
			      (HV_PCI_MAP_ATTR_READ |
			       HV_PCI_MAP_ATTR_WRITE),
			      entry);

	for (n = 0; n < npages; n++) {
		long err = pci_iommu_batch_add(first_page + (n * PAGE_SIZE));
		if (unlikely(err < 0L))
			goto iommu_map_fail;
	}

	if (unlikely(pci_iommu_batch_end() < 0L))
		goto iommu_map_fail;

	local_irq_restore(flags);

	return ret;

iommu_map_fail:
	/* Interrupts are disabled.  */
	spin_lock(&iommu->lock);
	pci_arena_free(&iommu->arena, entry, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);

arena_alloc_fail:
	free_pages(first_page, order);
	return NULL;
}

static void pci_4v_free_consistent(struct pci_dev *pdev, size_t size, void *cpu, dma_addr_t dvma)
{
	struct pci_pbm_info *pbm;
	struct iommu *iommu;
	unsigned long flags, order, npages, entry;
	u32 devhandle;

	npages = IO_PAGE_ALIGN(size) >> IO_PAGE_SHIFT;
	iommu = pdev->dev.archdata.iommu;
	pbm = pdev->dev.archdata.host_controller;
	devhandle = pbm->devhandle;
	entry = ((dvma - iommu->page_table_map_base) >> IO_PAGE_SHIFT);

	spin_lock_irqsave(&iommu->lock, flags);

	pci_arena_free(&iommu->arena, entry, npages);

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

static dma_addr_t pci_4v_map_single(struct pci_dev *pdev, void *ptr, size_t sz, int direction)
{
	struct iommu *iommu;
	unsigned long flags, npages, oaddr;
	unsigned long i, base_paddr;
	u32 bus_addr, ret;
	unsigned long prot;
	long entry;

	iommu = pdev->dev.archdata.iommu;

	if (unlikely(direction == PCI_DMA_NONE))
		goto bad;

	oaddr = (unsigned long)ptr;
	npages = IO_PAGE_ALIGN(oaddr + sz) - (oaddr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;

	spin_lock_irqsave(&iommu->lock, flags);
	entry = pci_arena_alloc(&iommu->arena, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);

	if (unlikely(entry < 0L))
		goto bad;

	bus_addr = (iommu->page_table_map_base +
		    (entry << IO_PAGE_SHIFT));
	ret = bus_addr | (oaddr & ~IO_PAGE_MASK);
	base_paddr = __pa(oaddr & IO_PAGE_MASK);
	prot = HV_PCI_MAP_ATTR_READ;
	if (direction != PCI_DMA_TODEVICE)
		prot |= HV_PCI_MAP_ATTR_WRITE;

	local_irq_save(flags);

	pci_iommu_batch_start(pdev, prot, entry);

	for (i = 0; i < npages; i++, base_paddr += IO_PAGE_SIZE) {
		long err = pci_iommu_batch_add(base_paddr);
		if (unlikely(err < 0L))
			goto iommu_map_fail;
	}
	if (unlikely(pci_iommu_batch_end() < 0L))
		goto iommu_map_fail;

	local_irq_restore(flags);

	return ret;

bad:
	if (printk_ratelimit())
		WARN_ON(1);
	return PCI_DMA_ERROR_CODE;

iommu_map_fail:
	/* Interrupts are disabled.  */
	spin_lock(&iommu->lock);
	pci_arena_free(&iommu->arena, entry, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);

	return PCI_DMA_ERROR_CODE;
}

static void pci_4v_unmap_single(struct pci_dev *pdev, dma_addr_t bus_addr, size_t sz, int direction)
{
	struct pci_pbm_info *pbm;
	struct iommu *iommu;
	unsigned long flags, npages;
	long entry;
	u32 devhandle;

	if (unlikely(direction == PCI_DMA_NONE)) {
		if (printk_ratelimit())
			WARN_ON(1);
		return;
	}

	iommu = pdev->dev.archdata.iommu;
	pbm = pdev->dev.archdata.host_controller;
	devhandle = pbm->devhandle;

	npages = IO_PAGE_ALIGN(bus_addr + sz) - (bus_addr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;
	bus_addr &= IO_PAGE_MASK;

	spin_lock_irqsave(&iommu->lock, flags);

	entry = (bus_addr - iommu->page_table_map_base) >> IO_PAGE_SHIFT;
	pci_arena_free(&iommu->arena, entry, npages);

	do {
		unsigned long num;

		num = pci_sun4v_iommu_demap(devhandle, HV_PCI_TSBID(0, entry),
					    npages);
		entry += num;
		npages -= num;
	} while (npages != 0);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

#define SG_ENT_PHYS_ADDRESS(SG)	\
	(__pa(page_address((SG)->page)) + (SG)->offset)

static inline long fill_sg(long entry, struct pci_dev *pdev,
			   struct scatterlist *sg,
			   int nused, int nelems, unsigned long prot)
{
	struct scatterlist *dma_sg = sg;
	struct scatterlist *sg_end = sg + nelems;
	unsigned long flags;
	int i;

	local_irq_save(flags);

	pci_iommu_batch_start(pdev, prot, entry);

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

			pteval = (pteval & IOPTE_PAGE);
			while (len > 0) {
				long err;

				err = pci_iommu_batch_add(pteval);
				if (unlikely(err < 0L))
					goto iommu_map_failed;

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

	if (unlikely(pci_iommu_batch_end() < 0L))
		goto iommu_map_failed;

	local_irq_restore(flags);
	return 0;

iommu_map_failed:
	local_irq_restore(flags);
	return -1L;
}

static int pci_4v_map_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems, int direction)
{
	struct iommu *iommu;
	unsigned long flags, npages, prot;
	u32 dma_base;
	struct scatterlist *sgtmp;
	long entry, err;
	int used;

	/* Fast path single entry scatterlists. */
	if (nelems == 1) {
		sglist->dma_address =
			pci_4v_map_single(pdev,
					  (page_address(sglist->page) + sglist->offset),
					  sglist->length, direction);
		if (unlikely(sglist->dma_address == PCI_DMA_ERROR_CODE))
			return 0;
		sglist->dma_length = sglist->length;
		return 1;
	}

	iommu = pdev->dev.archdata.iommu;
	
	if (unlikely(direction == PCI_DMA_NONE))
		goto bad;

	/* Step 1: Prepare scatter list. */
	npages = prepare_sg(sglist, nelems);

	/* Step 2: Allocate a cluster and context, if necessary. */
	spin_lock_irqsave(&iommu->lock, flags);
	entry = pci_arena_alloc(&iommu->arena, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);

	if (unlikely(entry < 0L))
		goto bad;

	dma_base = iommu->page_table_map_base +
		(entry << IO_PAGE_SHIFT);

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
	prot = HV_PCI_MAP_ATTR_READ;
	if (direction != PCI_DMA_TODEVICE)
		prot |= HV_PCI_MAP_ATTR_WRITE;

	err = fill_sg(entry, pdev, sglist, used, nelems, prot);
	if (unlikely(err < 0L))
		goto iommu_map_failed;

	return used;

bad:
	if (printk_ratelimit())
		WARN_ON(1);
	return 0;

iommu_map_failed:
	spin_lock_irqsave(&iommu->lock, flags);
	pci_arena_free(&iommu->arena, entry, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);

	return 0;
}

static void pci_4v_unmap_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems, int direction)
{
	struct pci_pbm_info *pbm;
	struct iommu *iommu;
	unsigned long flags, i, npages;
	long entry;
	u32 devhandle, bus_addr;

	if (unlikely(direction == PCI_DMA_NONE)) {
		if (printk_ratelimit())
			WARN_ON(1);
	}

	iommu = pdev->dev.archdata.iommu;
	pbm = pdev->dev.archdata.host_controller;
	devhandle = pbm->devhandle;
	
	bus_addr = sglist->dma_address & IO_PAGE_MASK;

	for (i = 1; i < nelems; i++)
		if (sglist[i].dma_length == 0)
			break;
	i--;
	npages = (IO_PAGE_ALIGN(sglist[i].dma_address + sglist[i].dma_length) -
		  bus_addr) >> IO_PAGE_SHIFT;

	entry = ((bus_addr - iommu->page_table_map_base) >> IO_PAGE_SHIFT);

	spin_lock_irqsave(&iommu->lock, flags);

	pci_arena_free(&iommu->arena, entry, npages);

	do {
		unsigned long num;

		num = pci_sun4v_iommu_demap(devhandle, HV_PCI_TSBID(0, entry),
					    npages);
		entry += num;
		npages -= num;
	} while (npages != 0);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

static void pci_4v_dma_sync_single_for_cpu(struct pci_dev *pdev, dma_addr_t bus_addr, size_t sz, int direction)
{
	/* Nothing to do... */
}

static void pci_4v_dma_sync_sg_for_cpu(struct pci_dev *pdev, struct scatterlist *sglist, int nelems, int direction)
{
	/* Nothing to do... */
}

const struct pci_iommu_ops pci_sun4v_iommu_ops = {
	.alloc_consistent		= pci_4v_alloc_consistent,
	.free_consistent		= pci_4v_free_consistent,
	.map_single			= pci_4v_map_single,
	.unmap_single			= pci_4v_unmap_single,
	.map_sg				= pci_4v_map_sg,
	.unmap_sg			= pci_4v_unmap_sg,
	.dma_sync_single_for_cpu	= pci_4v_dma_sync_single_for_cpu,
	.dma_sync_sg_for_cpu		= pci_4v_dma_sync_sg_for_cpu,
};

static void pci_sun4v_scan_bus(struct pci_pbm_info *pbm)
{
	struct property *prop;
	struct device_node *dp;

	dp = pbm->prom_node;
	prop = of_find_property(dp, "66mhz-capable", NULL);
	pbm->is_66mhz_capable = (prop != NULL);
	pbm->pci_bus = pci_scan_one_pbm(pbm);

	/* XXX register error interrupt handlers XXX */
}

static unsigned long probe_existing_entries(struct pci_pbm_info *pbm,
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

static void pci_sun4v_iommu_init(struct pci_pbm_info *pbm)
{
	struct iommu *iommu = pbm->iommu;
	struct property *prop;
	unsigned long num_tsb_entries, sz;
	u32 vdma[2], dma_mask, dma_offset;
	int tsbsize;

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

	dma_mask = vdma[0];
	switch (vdma[1]) {
		case 0x20000000:
			dma_mask |= 0x1fffffff;
			tsbsize = 64;
			break;

		case 0x40000000:
			dma_mask |= 0x3fffffff;
			tsbsize = 128;
			break;

		case 0x80000000:
			dma_mask |= 0x7fffffff;
			tsbsize = 256;
			break;

		default:
			prom_printf("PCI-SUN4V: strange virtual-dma size.\n");
			prom_halt();
	};

	tsbsize *= (8 * 1024);

	num_tsb_entries = tsbsize / sizeof(iopte_t);

	dma_offset = vdma[0];

	/* Setup initial software IOMMU state. */
	spin_lock_init(&iommu->lock);
	iommu->ctx_lowest_free = 1;
	iommu->page_table_map_base = dma_offset;
	iommu->dma_addr_mask = dma_mask;

	/* Allocate and initialize the free area map.  */
	sz = num_tsb_entries / 8;
	sz = (sz + 7UL) & ~7UL;
	iommu->arena.map = kzalloc(sz, GFP_KERNEL);
	if (!iommu->arena.map) {
		prom_printf("PCI_IOMMU: Error, kmalloc(arena.map) failed.\n");
		prom_halt();
	}
	iommu->arena.limit = num_tsb_entries;

	sz = probe_existing_entries(pbm, iommu);
	if (sz)
		printk("%s: Imported %lu TSB entries from OBP\n",
		       pbm->name, sz);
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

/* For now this just runs as a pre-handler for the real interrupt handler.
 * So we just walk through the queue and ACK all the entries, update the
 * head pointer, and return.
 *
 * In the longer term it would be nice to do something more integrated
 * wherein we can pass in some of this MSI info to the drivers.  This
 * would be most useful for PCIe fabric error messages, although we could
 * invoke those directly from the loop here in order to pass the info around.
 */
static void pci_sun4v_msi_prehandler(unsigned int ino, void *data1, void *data2)
{
	struct pci_pbm_info *pbm = data1;
	struct pci_sun4v_msiq_entry *base, *ep;
	unsigned long msiqid, orig_head, head, type, err;

	msiqid = (unsigned long) data2;

	head = 0xdeadbeef;
	err = pci_sun4v_msiq_gethead(pbm->devhandle, msiqid, &head);
	if (unlikely(err))
		goto hv_error_get;

	if (unlikely(head >= (pbm->msiq_ent_count * sizeof(struct pci_sun4v_msiq_entry))))
		goto bad_offset;

	head /= sizeof(struct pci_sun4v_msiq_entry);
	orig_head = head;
	base = (pbm->msi_queues + ((msiqid - pbm->msiq_first) *
				   (pbm->msiq_ent_count *
				    sizeof(struct pci_sun4v_msiq_entry))));
	ep = &base[head];
	while ((ep->version_type & MSIQ_TYPE_MASK) != 0) {
		type = (ep->version_type & MSIQ_TYPE_MASK) >> MSIQ_TYPE_SHIFT;
		if (unlikely(type != MSIQ_TYPE_MSI32 &&
			     type != MSIQ_TYPE_MSI64))
			goto bad_type;

		pci_sun4v_msi_setstate(pbm->devhandle,
				       ep->msi_data /* msi_num */,
				       HV_MSISTATE_IDLE);

		/* Clear the entry.  */
		ep->version_type &= ~MSIQ_TYPE_MASK;

		/* Go to next entry in ring.  */
		head++;
		if (head >= pbm->msiq_ent_count)
			head = 0;
		ep = &base[head];
	}

	if (likely(head != orig_head)) {
		/* ACK entries by updating head pointer.  */
		head *= sizeof(struct pci_sun4v_msiq_entry);
		err = pci_sun4v_msiq_sethead(pbm->devhandle, msiqid, head);
		if (unlikely(err))
			goto hv_error_set;
	}
	return;

hv_error_set:
	printk(KERN_EMERG "MSI: Hypervisor set head gives error %lu\n", err);
	goto hv_error_cont;

hv_error_get:
	printk(KERN_EMERG "MSI: Hypervisor get head gives error %lu\n", err);

hv_error_cont:
	printk(KERN_EMERG "MSI: devhandle[%x] msiqid[%lx] head[%lu]\n",
	       pbm->devhandle, msiqid, head);
	return;

bad_offset:
	printk(KERN_EMERG "MSI: Hypervisor gives bad offset %lx max(%lx)\n",
	       head, pbm->msiq_ent_count * sizeof(struct pci_sun4v_msiq_entry));
	return;

bad_type:
	printk(KERN_EMERG "MSI: Entry has bad type %lx\n", type);
	return;
}

static int msi_bitmap_alloc(struct pci_pbm_info *pbm)
{
	unsigned long size, bits_per_ulong;

	bits_per_ulong = sizeof(unsigned long) * 8;
	size = (pbm->msi_num + (bits_per_ulong - 1)) & ~(bits_per_ulong - 1);
	size /= 8;
	BUG_ON(size % sizeof(unsigned long));

	pbm->msi_bitmap = kzalloc(size, GFP_KERNEL);
	if (!pbm->msi_bitmap)
		return -ENOMEM;

	return 0;
}

static void msi_bitmap_free(struct pci_pbm_info *pbm)
{
	kfree(pbm->msi_bitmap);
	pbm->msi_bitmap = NULL;
}

static int msi_queue_alloc(struct pci_pbm_info *pbm)
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


static int alloc_msi(struct pci_pbm_info *pbm)
{
	int i;

	for (i = 0; i < pbm->msi_num; i++) {
		if (!test_and_set_bit(i, pbm->msi_bitmap))
			return i + pbm->msi_first;
	}

	return -ENOENT;
}

static void free_msi(struct pci_pbm_info *pbm, int msi_num)
{
	msi_num -= pbm->msi_first;
	clear_bit(msi_num, pbm->msi_bitmap);
}

static int pci_sun4v_setup_msi_irq(unsigned int *virt_irq_p,
				   struct pci_dev *pdev,
				   struct msi_desc *entry)
{
	struct pci_pbm_info *pbm = pdev->dev.archdata.host_controller;
	unsigned long devino, msiqid;
	struct msi_msg msg;
	int msi_num, err;

	*virt_irq_p = 0;

	msi_num = alloc_msi(pbm);
	if (msi_num < 0)
		return msi_num;

	devino = sun4v_build_msi(pbm->devhandle, virt_irq_p,
				 pbm->msiq_first_devino,
				 (pbm->msiq_first_devino +
				  pbm->msiq_num));
	err = -ENOMEM;
	if (!devino)
		goto out_err;

	msiqid = ((devino - pbm->msiq_first_devino) +
		  pbm->msiq_first);

	err = -EINVAL;
	if (pci_sun4v_msiq_setstate(pbm->devhandle, msiqid, HV_MSIQSTATE_IDLE))
	if (err)
		goto out_err;

	if (pci_sun4v_msiq_setvalid(pbm->devhandle, msiqid, HV_MSIQ_VALID))
		goto out_err;

	if (pci_sun4v_msi_setmsiq(pbm->devhandle,
				  msi_num, msiqid,
				  (entry->msi_attrib.is_64 ?
				   HV_MSITYPE_MSI64 : HV_MSITYPE_MSI32)))
		goto out_err;

	if (pci_sun4v_msi_setstate(pbm->devhandle, msi_num, HV_MSISTATE_IDLE))
		goto out_err;

	if (pci_sun4v_msi_setvalid(pbm->devhandle, msi_num, HV_MSIVALID_VALID))
		goto out_err;

	pdev->dev.archdata.msi_num = msi_num;

	if (entry->msi_attrib.is_64) {
		msg.address_hi = pbm->msi64_start >> 32;
		msg.address_lo = pbm->msi64_start & 0xffffffff;
	} else {
		msg.address_hi = 0;
		msg.address_lo = pbm->msi32_start;
	}
	msg.data = msi_num;

	set_irq_msi(*virt_irq_p, entry);
	write_msi_msg(*virt_irq_p, &msg);

	irq_install_pre_handler(*virt_irq_p,
				pci_sun4v_msi_prehandler,
				pbm, (void *) msiqid);

	return 0;

out_err:
	free_msi(pbm, msi_num);
	sun4v_destroy_msi(*virt_irq_p);
	*virt_irq_p = 0;
	return err;

}

static void pci_sun4v_teardown_msi_irq(unsigned int virt_irq,
				       struct pci_dev *pdev)
{
	struct pci_pbm_info *pbm = pdev->dev.archdata.host_controller;
	unsigned long msiqid, err;
	unsigned int msi_num;

	msi_num = pdev->dev.archdata.msi_num;
	err = pci_sun4v_msi_getmsiq(pbm->devhandle, msi_num, &msiqid);
	if (err) {
		printk(KERN_ERR "%s: getmsiq gives error %lu\n",
		       pbm->name, err);
		return;
	}

	pci_sun4v_msi_setvalid(pbm->devhandle, msi_num, HV_MSIVALID_INVALID);
	pci_sun4v_msiq_setvalid(pbm->devhandle, msiqid, HV_MSIQ_INVALID);

	free_msi(pbm, msi_num);

	/* The sun4v_destroy_msi() will liberate the devino and thus the MSIQ
	 * allocation.
	 */
	sun4v_destroy_msi(virt_irq);
}

static void pci_sun4v_msi_init(struct pci_pbm_info *pbm)
{
	const u32 *val;
	int len;

	val = of_get_property(pbm->prom_node, "#msi-eqs", &len);
	if (!val || len != 4)
		goto no_msi;
	pbm->msiq_num = *val;
	if (pbm->msiq_num) {
		const struct msiq_prop {
			u32 first_msiq;
			u32 num_msiq;
			u32 first_devino;
		} *mqp;
		const struct msi_range_prop {
			u32 first_msi;
			u32 num_msi;
		} *mrng;
		const struct addr_range_prop {
			u32 msi32_high;
			u32 msi32_low;
			u32 msi32_len;
			u32 msi64_high;
			u32 msi64_low;
			u32 msi64_len;
		} *arng;

		val = of_get_property(pbm->prom_node, "msi-eq-size", &len);
		if (!val || len != 4)
			goto no_msi;

		pbm->msiq_ent_count = *val;

		mqp = of_get_property(pbm->prom_node,
				      "msi-eq-to-devino", &len);
		if (!mqp || len != sizeof(struct msiq_prop))
			goto no_msi;

		pbm->msiq_first = mqp->first_msiq;
		pbm->msiq_first_devino = mqp->first_devino;

		val = of_get_property(pbm->prom_node, "#msi", &len);
		if (!val || len != 4)
			goto no_msi;
		pbm->msi_num = *val;

		mrng = of_get_property(pbm->prom_node, "msi-ranges", &len);
		if (!mrng || len != sizeof(struct msi_range_prop))
			goto no_msi;
		pbm->msi_first = mrng->first_msi;

		val = of_get_property(pbm->prom_node, "msi-data-mask", &len);
		if (!val || len != 4)
			goto no_msi;
		pbm->msi_data_mask = *val;

		val = of_get_property(pbm->prom_node, "msix-data-width", &len);
		if (!val || len != 4)
			goto no_msi;
		pbm->msix_data_width = *val;

		arng = of_get_property(pbm->prom_node, "msi-address-ranges",
				       &len);
		if (!arng || len != sizeof(struct addr_range_prop))
			goto no_msi;
		pbm->msi32_start = ((u64)arng->msi32_high << 32) |
			(u64) arng->msi32_low;
		pbm->msi64_start = ((u64)arng->msi64_high << 32) |
			(u64) arng->msi64_low;
		pbm->msi32_len = arng->msi32_len;
		pbm->msi64_len = arng->msi64_len;

		if (msi_bitmap_alloc(pbm))
			goto no_msi;

		if (msi_queue_alloc(pbm)) {
			msi_bitmap_free(pbm);
			goto no_msi;
		}

		printk(KERN_INFO "%s: MSI Queue first[%u] num[%u] count[%u] "
		       "devino[0x%x]\n",
		       pbm->name,
		       pbm->msiq_first, pbm->msiq_num,
		       pbm->msiq_ent_count,
		       pbm->msiq_first_devino);
		printk(KERN_INFO "%s: MSI first[%u] num[%u] mask[0x%x] "
		       "width[%u]\n",
		       pbm->name,
		       pbm->msi_first, pbm->msi_num, pbm->msi_data_mask,
		       pbm->msix_data_width);
		printk(KERN_INFO "%s: MSI addr32[0x%lx:0x%x] "
		       "addr64[0x%lx:0x%x]\n",
		       pbm->name,
		       pbm->msi32_start, pbm->msi32_len,
		       pbm->msi64_start, pbm->msi64_len);
		printk(KERN_INFO "%s: MSI queues at RA [%p]\n",
		       pbm->name,
		       pbm->msi_queues);
	}
	pbm->setup_msi_irq = pci_sun4v_setup_msi_irq;
	pbm->teardown_msi_irq = pci_sun4v_teardown_msi_irq;

	return;

no_msi:
	pbm->msiq_num = 0;
	printk(KERN_INFO "%s: No MSI support.\n", pbm->name);
}
#else /* CONFIG_PCI_MSI */
static void pci_sun4v_msi_init(struct pci_pbm_info *pbm)
{
}
#endif /* !(CONFIG_PCI_MSI) */

static void pci_sun4v_pbm_init(struct pci_controller_info *p, struct device_node *dp, u32 devhandle)
{
	struct pci_pbm_info *pbm;

	if (devhandle & 0x40)
		pbm = &p->pbm_B;
	else
		pbm = &p->pbm_A;

	pbm->next = pci_pbm_root;
	pci_pbm_root = pbm;

	pbm->scan_bus = pci_sun4v_scan_bus;
	pbm->pci_ops = &sun4v_pci_ops;
	pbm->config_space_reg_bits = 12;

	pbm->index = pci_num_pbms++;

	pbm->parent = p;
	pbm->prom_node = dp;

	pbm->devhandle = devhandle;

	pbm->name = dp->full_name;

	printk("%s: SUN4V PCI Bus Module\n", pbm->name);

	pci_determine_mem_io_space(pbm);

	pci_get_pbm_props(pbm);
	pci_sun4v_iommu_init(pbm);
	pci_sun4v_msi_init(pbm);
}

void sun4v_pci_init(struct device_node *dp, char *model_name)
{
	struct pci_controller_info *p;
	struct pci_pbm_info *pbm;
	struct iommu *iommu;
	struct property *prop;
	struct linux_prom64_registers *regs;
	u32 devhandle;
	int i;

	prop = of_find_property(dp, "reg", NULL);
	regs = prop->value;

	devhandle = (regs->phys_addr >> 32UL) & 0x0fffffff;

	for (pbm = pci_pbm_root; pbm; pbm = pbm->next) {
		if (pbm->devhandle == (devhandle ^ 0x40)) {
			pci_sun4v_pbm_init(pbm->parent, dp, devhandle);
			return;
		}
	}

	for_each_possible_cpu(i) {
		unsigned long page = get_zeroed_page(GFP_ATOMIC);

		if (!page)
			goto fatal_memory_error;

		per_cpu(pci_iommu_batch, i).pglist = (u64 *) page;
	}

	p = kzalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p)
		goto fatal_memory_error;

	iommu = kzalloc(sizeof(struct iommu), GFP_ATOMIC);
	if (!iommu)
		goto fatal_memory_error;

	p->pbm_A.iommu = iommu;

	iommu = kzalloc(sizeof(struct iommu), GFP_ATOMIC);
	if (!iommu)
		goto fatal_memory_error;

	p->pbm_B.iommu = iommu;

	/* Like PSYCHO and SCHIZO we have a 2GB aligned area
	 * for memory space.
	 */
	pci_memspace_mask = 0x7fffffffUL;

	pci_sun4v_pbm_init(p, dp, devhandle);
	return;

fatal_memory_error:
	prom_printf("SUN4V_PCI: Fatal memory allocation error.\n");
	prom_halt();
}
