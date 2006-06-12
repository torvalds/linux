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

#define PGLIST_NENTS	(PAGE_SIZE / sizeof(u64))

struct pci_iommu_batch {
	struct pci_dev	*pdev;		/* Device mapping is for.	*/
	unsigned long	prot;		/* IOMMU page protections	*/
	unsigned long	entry;		/* Index into IOTSB.		*/
	u64		*pglist;	/* List of physical pages	*/
	unsigned long	npages;		/* Number of pages in list.	*/
};

static DEFINE_PER_CPU(struct pci_iommu_batch, pci_iommu_batch);

/* Interrupts must be disabled.  */
static inline void pci_iommu_batch_start(struct pci_dev *pdev, unsigned long prot, unsigned long entry)
{
	struct pci_iommu_batch *p = &__get_cpu_var(pci_iommu_batch);

	p->pdev		= pdev;
	p->prot		= prot;
	p->entry	= entry;
	p->npages	= 0;
}

/* Interrupts must be disabled.  */
static long pci_iommu_batch_flush(struct pci_iommu_batch *p)
{
	struct pcidev_cookie *pcp = p->pdev->sysdata;
	unsigned long devhandle = pcp->pbm->devhandle;
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
	struct pci_iommu_batch *p = &__get_cpu_var(pci_iommu_batch);

	BUG_ON(p->npages >= PGLIST_NENTS);

	p->pglist[p->npages++] = phys_page;
	if (p->npages == PGLIST_NENTS)
		return pci_iommu_batch_flush(p);

	return 0;
}

/* Interrupts must be disabled.  */
static inline long pci_iommu_batch_end(void)
{
	struct pci_iommu_batch *p = &__get_cpu_var(pci_iommu_batch);

	BUG_ON(p->npages >= PGLIST_NENTS);

	return pci_iommu_batch_flush(p);
}

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

static void *pci_4v_alloc_consistent(struct pci_dev *pdev, size_t size, dma_addr_t *dma_addrp, gfp_t gfp)
{
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
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

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;

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
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	unsigned long flags, order, npages, entry;
	u32 devhandle;

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
	unsigned long i, base_paddr;
	u32 bus_addr, ret;
	unsigned long prot;
	long entry;

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;

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
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	unsigned long flags, npages;
	long entry;
	u32 devhandle;

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
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
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

	pcp = pdev->sysdata;
	iommu = pcp->pbm->iommu;
	
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
	struct pcidev_cookie *pcp;
	struct pci_iommu *iommu;
	unsigned long flags, i, npages;
	long entry;
	u32 devhandle, bus_addr;

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

struct pdev_entry {
	struct pdev_entry	*next;
	u32			devhandle;
	unsigned int		bus;
	unsigned int		device;
	unsigned int		func;
};

#define PDEV_HTAB_SIZE	16
#define PDEV_HTAB_MASK	(PDEV_HTAB_SIZE - 1)
static struct pdev_entry *pdev_htab[PDEV_HTAB_SIZE];

static inline unsigned int pdev_hashfn(u32 devhandle, unsigned int bus, unsigned int device, unsigned int func)
{
	unsigned int val;

	val = (devhandle ^ (devhandle >> 4));
	val ^= bus;
	val ^= device;
	val ^= func;

	return val & PDEV_HTAB_MASK;
}

static int pdev_htab_add(u32 devhandle, unsigned int bus, unsigned int device, unsigned int func)
{
	struct pdev_entry *p = kmalloc(sizeof(*p), GFP_KERNEL);
	struct pdev_entry **slot;

	if (!p)
		return -ENOMEM;

	slot = &pdev_htab[pdev_hashfn(devhandle, bus, device, func)];
	p->next = *slot;
	*slot = p;

	p->devhandle = devhandle;
	p->bus = bus;
	p->device = device;
	p->func = func;

	return 0;
}

/* Recursively descend into the OBP device tree, rooted at toplevel_node,
 * looking for a PCI device matching bus and devfn.
 */
static int obp_find(struct linux_prom_pci_registers *pregs, int toplevel_node, unsigned int bus, unsigned int devfn)
{
	toplevel_node = prom_getchild(toplevel_node);

	while (toplevel_node != 0) {
		int ret = obp_find(pregs, toplevel_node, bus, devfn);

		if (ret != 0)
			return ret;

		ret = prom_getproperty(toplevel_node, "reg", (char *) pregs,
				       sizeof(*pregs) * PROMREG_MAX);
		if (ret == 0 || ret == -1)
			goto next_sibling;

		if (((pregs[0].phys_hi >> 16) & 0xff) == bus &&
		    ((pregs[0].phys_hi >> 8) & 0xff) == devfn)
			break;

	next_sibling:
		toplevel_node = prom_getsibling(toplevel_node);
	}

	return toplevel_node;
}

static int pdev_htab_populate(struct pci_pbm_info *pbm)
{
	struct linux_prom_pci_registers pr[PROMREG_MAX];
	u32 devhandle = pbm->devhandle;
	unsigned int bus;

	for (bus = pbm->pci_first_busno; bus <= pbm->pci_last_busno; bus++) {
		unsigned int devfn;

		for (devfn = 0; devfn < 256; devfn++) {
			unsigned int device = PCI_SLOT(devfn);
			unsigned int func = PCI_FUNC(devfn);

			if (obp_find(pr, pbm->prom_node, bus, devfn)) {
				int err = pdev_htab_add(devhandle, bus,
							device, func);
				if (err)
					return err;
			}
		}
	}

	return 0;
}

static struct pdev_entry *pdev_find(u32 devhandle, unsigned int bus, unsigned int device, unsigned int func)
{
	struct pdev_entry *p;

	p = pdev_htab[pdev_hashfn(devhandle, bus, device, func)];
	while (p) {
		if (p->devhandle == devhandle &&
		    p->bus == bus &&
		    p->device == device &&
		    p->func == func)
			break;

		p = p->next;
	}

	return p;
}

static inline int pci_sun4v_out_of_range(struct pci_pbm_info *pbm, unsigned int bus, unsigned int device, unsigned int func)
{
	if (bus < pbm->pci_first_busno ||
	    bus > pbm->pci_last_busno)
		return 1;
	return pdev_find(pbm->devhandle, bus, device, func) == NULL;
}

static int pci_sun4v_read_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
				  int where, int size, u32 *value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	u32 devhandle = pbm->devhandle;
	unsigned int bus = bus_dev->number;
	unsigned int device = PCI_SLOT(devfn);
	unsigned int func = PCI_FUNC(devfn);
	unsigned long ret;

	if (pci_sun4v_out_of_range(pbm, bus, device, func)) {
		ret = ~0UL;
	} else {
		ret = pci_sun4v_config_get(devhandle,
				HV_PCI_DEVICE_BUILD(bus, device, func),
				where, size);
#if 0
		printk("rcfg: [%x:%x:%x:%d]=[%lx]\n",
		       devhandle, HV_PCI_DEVICE_BUILD(bus, device, func),
		       where, size, ret);
#endif
	}
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
	u32 devhandle = pbm->devhandle;
	unsigned int bus = bus_dev->number;
	unsigned int device = PCI_SLOT(devfn);
	unsigned int func = PCI_FUNC(devfn);
	unsigned long ret;

	if (pci_sun4v_out_of_range(pbm, bus, device, func)) {
		/* Do nothing. */
	} else {
		ret = pci_sun4v_config_put(devhandle,
				HV_PCI_DEVICE_BUILD(bus, device, func),
				where, size, value);
#if 0
		printk("wcfg: [%x:%x:%x:%d] v[%x] == [%lx]\n",
		       devhandle, HV_PCI_DEVICE_BUILD(bus, device, func),
		       where, size, value, ret);
#endif
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_sun4v_ops = {
	.read =		pci_sun4v_read_pci_cfg,
	.write =	pci_sun4v_write_pci_cfg,
};


static void pbm_scan_bus(struct pci_controller_info *p,
			 struct pci_pbm_info *pbm)
{
	struct pcidev_cookie *cookie = kmalloc(sizeof(*cookie), GFP_KERNEL);

	if (!cookie) {
		prom_printf("%s: Critical allocation failure.\n", pbm->name);
		prom_halt();
	}

	/* All we care about is the PBM. */
	memset(cookie, 0, sizeof(*cookie));
	cookie->pbm = pbm;

	pbm->pci_bus = pci_scan_bus(pbm->pci_first_busno, p->pci_ops, pbm);
#if 0
	pci_fixup_host_bridge_self(pbm->pci_bus);
	pbm->pci_bus->self->sysdata = cookie;
#endif
	pci_fill_in_pbm_cookies(pbm->pci_bus, pbm,
				pbm->prom_node);
	pci_record_assignments(pbm, pbm->pci_bus);
	pci_assign_unassigned(pbm, pbm->pci_bus);
	pci_fixup_irq(pbm, pbm->pci_bus);
	pci_determine_66mhz_disposition(pbm, pbm->pci_bus);
	pci_setup_busmastering(pbm, pbm->pci_bus);
}

static void pci_sun4v_scan_bus(struct pci_controller_info *p)
{
	if (p->pbm_A.prom_node) {
		p->pbm_A.is_66mhz_capable =
			prom_getbool(p->pbm_A.prom_node, "66mhz-capable");

		pbm_scan_bus(p, &p->pbm_A);
	}
	if (p->pbm_B.prom_node) {
		p->pbm_B.is_66mhz_capable =
			prom_getbool(p->pbm_B.prom_node, "66mhz-capable");

		pbm_scan_bus(p, &p->pbm_B);
	}

	/* XXX register error interrupt handlers XXX */
}

static unsigned int pci_sun4v_irq_build(struct pci_pbm_info *pbm,
					struct pci_dev *pdev,
					unsigned int devino)
{
	u32 devhandle = pbm->devhandle;
	int pil;

	pil = 5;
	if (pdev) {
		switch ((pdev->class >> 16) & 0xff) {
		case PCI_BASE_CLASS_STORAGE:
			pil = 5;
			break;

		case PCI_BASE_CLASS_NETWORK:
			pil = 6;
			break;

		case PCI_BASE_CLASS_DISPLAY:
			pil = 9;
			break;

		case PCI_BASE_CLASS_MULTIMEDIA:
		case PCI_BASE_CLASS_MEMORY:
		case PCI_BASE_CLASS_BRIDGE:
		case PCI_BASE_CLASS_SERIAL:
			pil = 10;
			break;

		default:
			pil = 5;
			break;
		};
	}
	BUG_ON(PIL_RESERVED(pil));

	return sun4v_build_irq(devhandle, devino, pil, IBF_PCI);
}

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

	/* XXX 64-bit MEM handling is not %100 correct... XXX */
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

		case 3:
			/* XXX 64-bit MEM handling XXX */

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

static unsigned long probe_existing_entries(struct pci_pbm_info *pbm,
					    struct pci_iommu *iommu)
{
	struct pci_iommu_arena *arena = &iommu->arena;
	unsigned long i, cnt = 0;
	u32 devhandle;

	devhandle = pbm->devhandle;
	for (i = 0; i < arena->limit; i++) {
		unsigned long ret, io_attrs, ra;

		ret = pci_sun4v_iommu_getmap(devhandle,
					     HV_PCI_TSBID(0, i),
					     &io_attrs, &ra);
		if (ret == HV_EOK) {
			cnt++;
			__set_bit(i, arena->map);
		}
	}

	return cnt;
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
	iommu->arena.map = kmalloc(sz, GFP_KERNEL);
	if (!iommu->arena.map) {
		prom_printf("PCI_IOMMU: Error, kmalloc(arena.map) failed.\n");
		prom_halt();
	}
	memset(iommu->arena.map, 0, sz);
	iommu->arena.limit = num_tsb_entries;

	sz = probe_existing_entries(pbm, iommu);

	printk("%s: TSB entries [%lu], existing mapings [%lu]\n",
	       pbm->name, num_tsb_entries, sz);
}

static void pci_sun4v_get_bus_range(struct pci_pbm_info *pbm)
{
	unsigned int busrange[2];
	int prom_node = pbm->prom_node;
	int err;

	err = prom_getproperty(prom_node, "bus-range",
			       (char *)&busrange[0],
			       sizeof(busrange));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no bus-range.\n", pbm->name);
		prom_halt();
	}

	pbm->pci_first_busno = busrange[0];
	pbm->pci_last_busno = busrange[1];

}

static void pci_sun4v_pbm_init(struct pci_controller_info *p, int prom_node, u32 devhandle)
{
	struct pci_pbm_info *pbm;
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

	printk("%s: devhandle[%x] prom_node[%x:%x]\n",
	       pbm->name, pbm->devhandle,
	       pbm->prom_node, prom_getchild(pbm->prom_node));

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
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no interrupt-map property.\n",
			    pbm->name);
		prom_halt();
	}

	pbm->num_pbm_intmap = (err / sizeof(struct linux_prom_pci_intmap));
	err = prom_getproperty(prom_node, "interrupt-map-mask",
			       (char *)&pbm->pbm_intmask,
			       sizeof(pbm->pbm_intmask));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no interrupt-map-mask.\n",
			    pbm->name);
		prom_halt();
	}

	pci_sun4v_get_bus_range(pbm);
	pci_sun4v_iommu_init(pbm);

	pdev_htab_populate(pbm);
}

void sun4v_pci_init(int node, char *model_name)
{
	struct pci_controller_info *p;
	struct pci_iommu *iommu;
	struct linux_prom64_registers regs;
	u32 devhandle;
	int i;

	prom_getproperty(node, "reg", (char *)&regs, sizeof(regs));
	devhandle = (regs.phys_addr >> 32UL) & 0x0fffffff;

	for (p = pci_controller_root; p; p = p->next) {
		struct pci_pbm_info *pbm;

		if (p->pbm_A.prom_node && p->pbm_B.prom_node)
			continue;

		pbm = (p->pbm_A.prom_node ?
		       &p->pbm_A :
		       &p->pbm_B);

		if (pbm->devhandle == (devhandle ^ 0x40)) {
			pci_sun4v_pbm_init(p, node, devhandle);
			return;
		}
	}

	for_each_possible_cpu(i) {
		unsigned long page = get_zeroed_page(GFP_ATOMIC);

		if (!page)
			goto fatal_memory_error;

		per_cpu(pci_iommu_batch, i).pglist = (u64 *) page;
	}

	p = kmalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p)
		goto fatal_memory_error;

	memset(p, 0, sizeof(*p));

	iommu = kmalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu)
		goto fatal_memory_error;

	memset(iommu, 0, sizeof(*iommu));
	p->pbm_A.iommu = iommu;

	iommu = kmalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu)
		goto fatal_memory_error;

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
	return;

fatal_memory_error:
	prom_printf("SUN4V_PCI: Fatal memory allocation error.\n");
	prom_halt();
}
