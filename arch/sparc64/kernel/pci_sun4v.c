/* pci_sun4v.c: SUN4V specific PCI controller support.
 *
 * Copyright (C) 2006 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>

#include <asm/pbm.h>
#include <asm/iommu.h>
#include <asm/irq.h>
#include <asm/upa.h>
#include <asm/pstate.h>
#include <asm/oplib.h>
#include <asm/hypervisor.h>

#include "pci_impl.h"
#include "iommu_common.h"

#include "pci_sun4v.h"

#define PGLIST_NENTS	2048

struct sun4v_pglist {
	u64	pglist[PGLIST_NENTS];
};

static DEFINE_PER_CPU(struct sun4v_pglist, iommu_pglists);

static long pci_arena_alloc(struct pci_iommu_arena *arena, unsigned long npages)
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

static void pci_arena_free(struct pci_iommu_arena *arena, unsigned long base, unsigned long npages)
{
	unsigned long i;

	for (i = base; i < (base + npages); i++)
		__clear_bit(i, arena->map);
}

static void *pci_4v_alloc_consistent(struct pci_dev *pdev, size_t size, dma_addr_t *dma_addrp)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	unsigned long devhandle, flags, order, first_page, npages, n;
	void *ret;
	long entry;
	u64 *pglist;
	int cpu;

	size = IO_PAGE_ALIGN(size);
	order = get_order(size);
	if (order >= MAX_ORDER)
		return NULL;

	npages = size >> IO_PAGE_SHIFT;
	if (npages > PGLIST_NENTS)
		return NULL;

	first_page = __get_free_pages(GFP_ATOMIC, order);
	if (first_page == 0UL)
		return NULL;
	memset((char *)first_page, 0, PAGE_SIZE << order);

	pcp = pdev->sysdata;
	devhandle = pcp->pbm->devhandle;
	iommu = pcp->pbm->iommu;

	spin_lock_irqsave(&iommu->lock, flags);
	entry = pci_arena_alloc(&iommu->arena, npages);
	spin_unlock_irqrestore(&iommu->lock, flags);

	if (unlikely(entry < 0L)) {
		free_pages(first_page, order);
		return NULL;
	}

	*dma_addrp = (iommu->page_table_map_base +
		      (entry << IO_PAGE_SHIFT));
	ret = (void *) first_page;
	first_page = __pa(first_page);

	cpu = get_cpu();

	pglist = &__get_cpu_var(iommu_pglists).pglist[0];
	for (n = 0; n < npages; n++)
		pglist[n] = first_page + (n * PAGE_SIZE);

	do {
		unsigned long num;

		num = pci_sun4v_iommu_map(devhandle, HV_PCI_TSBID(0, entry),
					  npages,
					  (HV_PCI_MAP_ATTR_READ |
					   HV_PCI_MAP_ATTR_WRITE),
					  __pa(pglist));
		entry += num;
		npages -= num;
		pglist += num;
	} while (npages != 0);

	put_cpu();

	return ret;
}

static void pci_4v_free_consistent(struct pci_dev *pdev, size_t size, void *cpu, dma_addr_t dvma)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	unsigned long flags, order, npages, entry, devhandle;

	npages = IO_PAGE_ALIGN(size) >> IO_PAGE_SHIFT;
	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	devhandle = pcp->pbm->devhandle;
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
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	unsigned long flags, npages, oaddr;
	unsigned long i, base_paddr, devhandle;
	u32 bus_addr, ret;
	unsigned long prot;
	long entry;
	u64 *pglist;
	int cpu;

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	devhandle = pcp->pbm->devhandle;

	if (unlikely(direction == PCI_DMA_NONE))
		goto bad;

	oaddr = (unsigned long)ptr;
	npages = IO_PAGE_ALIGN(oaddr + sz) - (oaddr & IO_PAGE_MASK);
	npages >>= IO_PAGE_SHIFT;
	if (unlikely(npages > PGLIST_NENTS))
		goto bad;

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

	cpu = get_cpu();

	pglist = &__get_cpu_var(iommu_pglists).pglist[0];
	for (i = 0; i < npages; i++, base_paddr += IO_PAGE_SIZE)
		pglist[i] = base_paddr;

	do {
		unsigned long num;

		num = pci_sun4v_iommu_map(devhandle, HV_PCI_TSBID(0, entry),
					  npages, prot,
					  __pa(pglist));
		entry += num;
		npages -= num;
		pglist += num;
	} while (npages != 0);

	put_cpu();

	return ret;

bad:
	if (printk_ratelimit())
		WARN_ON(1);
	return PCI_DMA_ERROR_CODE;
}

static void pci_4v_unmap_single(struct pci_dev *pdev, dma_addr_t bus_addr, size_t sz, int direction)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	unsigned long flags, npages, devhandle;
	long entry;

	if (unlikely(direction == PCI_DMA_NONE)) {
		if (printk_ratelimit())
			WARN_ON(1);
		return;
	}

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	devhandle = pcp->pbm->devhandle;

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

static inline void fill_sg(long entry, unsigned long devhandle,
			   struct scatterlist *sg,
			   int nused, int nelems, unsigned long prot)
{
	struct scatterlist *dma_sg = sg;
	struct scatterlist *sg_end = sg + nelems;
	int i, cpu, pglist_ent;
	u64 *pglist;

	cpu = get_cpu();
	pglist = &__get_cpu_var(iommu_pglists).pglist[0];
	pglist_ent = 0;
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
				pglist[pglist_ent++] = pteval;
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

	BUG_ON(pglist_ent == 0);

	do {
		unsigned long num;

		num = pci_sun4v_iommu_demap(devhandle, HV_PCI_TSBID(0, entry),
					    pglist_ent);
		entry += num;
		pglist_ent -= num;
	} while (pglist_ent != 0);

	put_cpu();
}

static int pci_4v_map_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems, int direction)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	unsigned long flags, npages, prot, devhandle;
	u32 dma_base;
	struct scatterlist *sgtmp;
	long entry;
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

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	devhandle = pcp->pbm->devhandle;
	
	if (unlikely(direction == PCI_DMA_NONE))
		goto bad;

	/* Step 1: Prepare scatter list. */
	npages = prepare_sg(sglist, nelems);
	if (unlikely(npages > PGLIST_NENTS))
		goto bad;

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

	fill_sg(entry, devhandle, sglist, used, nelems, prot);

	return used;

bad:
	if (printk_ratelimit())
		WARN_ON(1);
	return 0;
}

static void pci_4v_unmap_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems, int direction)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	unsigned long flags, i, npages, devhandle;
	long entry;
	u32 bus_addr;

	if (unlikely(direction == PCI_DMA_NONE)) {
		if (printk_ratelimit())
			WARN_ON(1);
	}

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	devhandle = pcp->pbm->devhandle;
	
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

struct pci_iommu_ops pci_sun4v_iommu_ops = {
	.alloc_consistent		= pci_4v_alloc_consistent,
	.free_consistent		= pci_4v_free_consistent,
	.map_single			= pci_4v_map_single,
	.unmap_single			= pci_4v_unmap_single,
	.map_sg				= pci_4v_map_sg,
	.unmap_sg			= pci_4v_unmap_sg,
	.dma_sync_single_for_cpu	= pci_4v_dma_sync_single_for_cpu,
	.dma_sync_sg_for_cpu		= pci_4v_dma_sync_sg_for_cpu,
};

/* SUN4V PCI configuration space accessors. */

static int pci_sun4v_read_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
				  int where, int size, u32 *value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned long devhandle = pbm->devhandle;
	unsigned int bus = bus_dev->number;
	unsigned int device = PCI_SLOT(devfn);
	unsigned int func = PCI_FUNC(devfn);
	unsigned long ret;

	ret = pci_sun4v_config_get(devhandle,
				   HV_PCI_DEVICE_BUILD(bus, device, func),
				   where, size);
	switch (size) {
	case 1:
		*value = ret & 0xff;
		break;
	case 2:
		*value = ret & 0xffff;
		break;
	case 4:
		*value = ret & 0xffffffff;
		break;
	};


	return PCIBIOS_SUCCESSFUL;
}

static int pci_sun4v_write_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
				   int where, int size, u32 value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned long devhandle = pbm->devhandle;
	unsigned int bus = bus_dev->number;
	unsigned int device = PCI_SLOT(devfn);
	unsigned int func = PCI_FUNC(devfn);
	unsigned long ret;

	ret = pci_sun4v_config_put(devhandle,
				   HV_PCI_DEVICE_BUILD(bus, device, func),
				   where, size, value);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_sun4v_ops = {
	.read =		pci_sun4v_read_pci_cfg,
	.write =	pci_sun4v_write_pci_cfg,
};


static void pci_sun4v_scan_bus(struct pci_controller_info *p)
{
	/* XXX Implement me! XXX */
}

static unsigned int pci_sun4v_irq_build(struct pci_pbm_info *pbm,
					struct pci_dev *pdev,
					unsigned int ino)
{
	/* XXX Implement me! XXX */
	return 0;
}

/* XXX correct? XXX */
static void pci_sun4v_base_address_update(struct pci_dev *pdev, int resource)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_pbm_info *pbm = pcp->pbm;
	struct resource *res, *root;
	u32 reg;
	int where, size, is_64bit;

	res = &pdev->resource[resource];
	if (resource < 6) {
		where = PCI_BASE_ADDRESS_0 + (resource * 4);
	} else if (resource == PCI_ROM_RESOURCE) {
		where = pdev->rom_base_reg;
	} else {
		/* Somebody might have asked allocation of a non-standard resource */
		return;
	}

	is_64bit = 0;
	if (res->flags & IORESOURCE_IO)
		root = &pbm->io_space;
	else {
		root = &pbm->mem_space;
		if ((res->flags & PCI_BASE_ADDRESS_MEM_TYPE_MASK)
		    == PCI_BASE_ADDRESS_MEM_TYPE_64)
			is_64bit = 1;
	}

	size = res->end - res->start;
	pci_read_config_dword(pdev, where, &reg);
	reg = ((reg & size) |
	       (((u32)(res->start - root->start)) & ~size));
	if (resource == PCI_ROM_RESOURCE) {
		reg |= PCI_ROM_ADDRESS_ENABLE;
		res->flags |= IORESOURCE_ROM_ENABLE;
	}
	pci_write_config_dword(pdev, where, reg);

	/* This knows that the upper 32-bits of the address
	 * must be zero.  Our PCI common layer enforces this.
	 */
	if (is_64bit)
		pci_write_config_dword(pdev, where + 4, 0);
}

/* XXX correct? XXX */
static void pci_sun4v_resource_adjust(struct pci_dev *pdev,
				      struct resource *res,
				      struct resource *root)
{
	res->start += root->start;
	res->end += root->start;
}

/* Use ranges property to determine where PCI MEM, I/O, and Config
 * space are for this PCI bus module.
 */
static void pci_sun4v_determine_mem_io_space(struct pci_pbm_info *pbm)
{
	int i, saw_mem, saw_io;

	saw_mem = saw_io = 0;
	for (i = 0; i < pbm->num_pbm_ranges; i++) {
		struct linux_prom_pci_ranges *pr = &pbm->pbm_ranges[i];
		unsigned long a;
		int type;

		type = (pr->child_phys_hi >> 24) & 0x3;
		a = (((unsigned long)pr->parent_phys_hi << 32UL) |
		     ((unsigned long)pr->parent_phys_lo  <<  0UL));

		switch (type) {
		case 1:
			/* 16-bit IO space, 16MB */
			pbm->io_space.start = a;
			pbm->io_space.end = a + ((16UL*1024UL*1024UL) - 1UL);
			pbm->io_space.flags = IORESOURCE_IO;
			saw_io = 1;
			break;

		case 2:
			/* 32-bit MEM space, 2GB */
			pbm->mem_space.start = a;
			pbm->mem_space.end = a + (0x80000000UL - 1UL);
			pbm->mem_space.flags = IORESOURCE_MEM;
			saw_mem = 1;
			break;

		default:
			break;
		};
	}

	if (!saw_io || !saw_mem) {
		prom_printf("%s: Fatal error, missing %s PBM range.\n",
			    pbm->name,
			    (!saw_io ? "IO" : "MEM"));
		prom_halt();
	}

	printk("%s: PCI IO[%lx] MEM[%lx]\n",
	       pbm->name,
	       pbm->io_space.start,
	       pbm->mem_space.start);
}

static void pbm_register_toplevel_resources(struct pci_controller_info *p,
					    struct pci_pbm_info *pbm)
{
	pbm->io_space.name = pbm->mem_space.name = pbm->name;

	request_resource(&ioport_resource, &pbm->io_space);
	request_resource(&iomem_resource, &pbm->mem_space);
	pci_register_legacy_regions(&pbm->io_space,
				    &pbm->mem_space);
}

static void probe_existing_entries(struct pci_pbm_info *pbm,
				   struct pci_iommu *iommu)
{
	struct pci_iommu_arena *arena = &iommu->arena;
	unsigned long i, devhandle;

	devhandle = pbm->devhandle;
	for (i = 0; i < arena->limit; i++) {
		unsigned long ret, io_attrs, ra;

		ret = pci_sun4v_iommu_getmap(devhandle,
					     HV_PCI_TSBID(0, i),
					     &io_attrs, &ra);
		if (ret == HV_EOK)
			__set_bit(i, arena->map);
	}
}

static void pci_sun4v_iommu_init(struct pci_pbm_info *pbm)
{
	struct pci_iommu *iommu = pbm->iommu;
	unsigned long num_tsb_entries, sz;
	u32 vdma[2], dma_mask, dma_offset;
	int err, tsbsize;

	err = prom_getproperty(pbm->prom_node, "virtual-dma",
			       (char *)&vdma[0], sizeof(vdma));
	if (err == 0 || err == -1) {
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
			tsbsize = 128;
			break;

		default:
			prom_printf("PCI-SUN4V: strange virtual-dma size.\n");
			prom_halt();
	};

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
	iommu->arena.map = kmalloc(sz, GFP_KERNEL);
	if (!iommu->arena.map) {
		prom_printf("PCI_IOMMU: Error, kmalloc(arena.map) failed.\n");
		prom_halt();
	}
	memset(iommu->arena.map, 0, sz);
	iommu->arena.limit = num_tsb_entries;

	probe_existing_entries(pbm, iommu);
}

static void pci_sun4v_pbm_init(struct pci_controller_info *p, int prom_node, unsigned int devhandle)
{
	struct pci_pbm_info *pbm;
	unsigned int busrange[2];
	int err, i;

	if (devhandle & 0x40)
		pbm = &p->pbm_B;
	else
		pbm = &p->pbm_A;

	pbm->parent = p;
	pbm->prom_node = prom_node;
	pbm->pci_first_slot = 1;

	pbm->devhandle = devhandle;

	sprintf(pbm->name, "SUN4V-PCI%d PBM%c",
		p->index, (pbm == &p->pbm_A ? 'A' : 'B'));

	printk("%s: devhandle[%x]\n", pbm->name, pbm->devhandle);

	prom_getstring(prom_node, "name",
		       pbm->prom_name, sizeof(pbm->prom_name));

	err = prom_getproperty(prom_node, "ranges",
			       (char *) pbm->pbm_ranges,
			       sizeof(pbm->pbm_ranges));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no ranges property.\n",
			    pbm->name);
		prom_halt();
	}

	pbm->num_pbm_ranges =
		(err / sizeof(struct linux_prom_pci_ranges));

	/* Mask out the top 8 bits of the ranges, leaving the real
	 * physical address.
	 */
	for (i = 0; i < pbm->num_pbm_ranges; i++)
		pbm->pbm_ranges[i].parent_phys_hi &= 0x0fffffff;

	pci_sun4v_determine_mem_io_space(pbm);
	pbm_register_toplevel_resources(p, pbm);

	err = prom_getproperty(prom_node, "interrupt-map",
			       (char *)pbm->pbm_intmap,
			       sizeof(pbm->pbm_intmap));
	if (err != -1) {
		pbm->num_pbm_intmap = (err / sizeof(struct linux_prom_pci_intmap));
		err = prom_getproperty(prom_node, "interrupt-map-mask",
				       (char *)&pbm->pbm_intmask,
				       sizeof(pbm->pbm_intmask));
		if (err == -1) {
			prom_printf("%s: Fatal error, no "
				    "interrupt-map-mask.\n", pbm->name);
			prom_halt();
		}
	} else {
		pbm->num_pbm_intmap = 0;
		memset(&pbm->pbm_intmask, 0, sizeof(pbm->pbm_intmask));
	}

	err = prom_getproperty(prom_node, "bus-range",
			       (char *)&busrange[0],
			       sizeof(busrange));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no bus-range.\n", pbm->name);
		prom_halt();
	}
	pbm->pci_first_busno = busrange[0];
	pbm->pci_last_busno = busrange[1];

	pci_sun4v_iommu_init(pbm);
}

void sun4v_pci_init(int node, char *model_name)
{
	struct pci_controller_info *p;
	struct pci_iommu *iommu;
	struct linux_prom64_registers regs;
	unsigned int devhandle;

	prom_getproperty(node, "reg", (char *)&regs, sizeof(regs));
	devhandle = (regs.phys_addr >> 32UL) & 0x0fffffff;;

	for (p = pci_controller_root; p; p = p->next) {
		struct pci_pbm_info *pbm;

		if (p->pbm_A.prom_node && p->pbm_B.prom_node)
			continue;

		pbm = (p->pbm_A.prom_node ?
		       &p->pbm_A :
		       &p->pbm_B);

		if (pbm->devhandle == (devhandle ^ 0x40))
			pci_sun4v_pbm_init(p, node, devhandle);
	}

	p = kmalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p) {
		prom_printf("SUN4V_PCI: Fatal memory allocation error.\n");
		prom_halt();
	}
	memset(p, 0, sizeof(*p));

	iommu = kmalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu) {
		prom_printf("SCHIZO: Fatal memory allocation error.\n");
		prom_halt();
	}
	memset(iommu, 0, sizeof(*iommu));
	p->pbm_A.iommu = iommu;

	iommu = kmalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu) {
		prom_printf("SCHIZO: Fatal memory allocation error.\n");
		prom_halt();
	}
	memset(iommu, 0, sizeof(*iommu));
	p->pbm_B.iommu = iommu;

	p->next = pci_controller_root;
	pci_controller_root = p;

	p->index = pci_num_controllers++;
	p->pbms_same_domain = 0;

	p->scan_bus = pci_sun4v_scan_bus;
	p->irq_build = pci_sun4v_irq_build;
	p->base_address_update = pci_sun4v_base_address_update;
	p->resource_adjust = pci_sun4v_resource_adjust;
	p->pci_ops = &pci_sun4v_ops;

	/* Like PSYCHO and SCHIZO we have a 2GB aligned area
	 * for memory space.
	 */
	pci_memspace_mask = 0x7fffffffUL;

	pci_sun4v_pbm_init(p, node, devhandle);

	prom_printf("sun4v_pci_init: Implement me.\n");
	prom_halt();
}
