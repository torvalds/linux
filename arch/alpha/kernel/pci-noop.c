/*
 *	linux/arch/alpha/kernel/pci-noop.c
 *
 * Stub PCI interfaces for Jensen-specific kernels.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>

#include "proto.h"


/*
 * The PCI controller list.
 */

struct pci_controller *hose_head, **hose_tail = &hose_head;
struct pci_controller *pci_isa_hose;


struct pci_controller * __init
alloc_pci_controller(void)
{
	struct pci_controller *hose;

	hose = alloc_bootmem(sizeof(*hose));

	*hose_tail = hose;
	hose_tail = &hose->next;

	return hose;
}

struct resource * __init
alloc_resource(void)
{
	struct resource *res;

	res = alloc_bootmem(sizeof(*res));

	return res;
}

asmlinkage long
sys_pciconfig_iobase(long which, unsigned long bus, unsigned long dfn)
{
	struct pci_controller *hose;

	/* from hose or from bus.devfn */
	if (which & IOBASE_FROM_HOSE) {
		for (hose = hose_head; hose; hose = hose->next) 
			if (hose->index == bus)
				break;
		if (!hose)
			return -ENODEV;
	} else {
		/* Special hook for ISA access.  */
		if (bus == 0 && dfn == 0)
			hose = pci_isa_hose;
		else
			return -ENODEV;
	}

	switch (which & ~IOBASE_FROM_HOSE) {
	case IOBASE_HOSE:
		return hose->index;
	case IOBASE_SPARSE_MEM:
		return hose->sparse_mem_base;
	case IOBASE_DENSE_MEM:
		return hose->dense_mem_base;
	case IOBASE_SPARSE_IO:
		return hose->sparse_io_base;
	case IOBASE_DENSE_IO:
		return hose->dense_io_base;
	case IOBASE_ROOT_BUS:
		return hose->bus->number;
	}

	return -EOPNOTSUPP;
}

asmlinkage long
sys_pciconfig_read(unsigned long bus, unsigned long dfn,
		   unsigned long off, unsigned long len, void *buf)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	else
		return -ENODEV;
}

asmlinkage long
sys_pciconfig_write(unsigned long bus, unsigned long dfn,
		    unsigned long off, unsigned long len, void *buf)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	else
		return -ENODEV;
}

/* Stubs for the routines in pci_iommu.c: */

void *
pci_alloc_consistent(struct pci_dev *pdev, size_t size, dma_addr_t *dma_addrp)
{
	return NULL;
}

void
pci_free_consistent(struct pci_dev *pdev, size_t size, void *cpu_addr,
		    dma_addr_t dma_addr)
{
}

dma_addr_t
pci_map_single(struct pci_dev *pdev, void *cpu_addr, size_t size,
	       int direction)
{
	return (dma_addr_t) 0;
}

void
pci_unmap_single(struct pci_dev *pdev, dma_addr_t dma_addr, size_t size,
		 int direction)
{
}

int
pci_map_sg(struct pci_dev *pdev, struct scatterlist *sg, int nents,
	   int direction)
{
	return 0;
}

void
pci_unmap_sg(struct pci_dev *pdev, struct scatterlist *sg, int nents,
	     int direction)
{
}

int
pci_dma_supported(struct pci_dev *hwdev, dma_addr_t mask)
{
	return 0;
}

/* Generic DMA mapping functions: */

void *
dma_alloc_coherent(struct device *dev, size_t size,
		   dma_addr_t *dma_handle, gfp_t gfp)
{
	void *ret;

	if (!dev || *dev->dma_mask >= 0xffffffffUL)
		gfp &= ~GFP_DMA;
	ret = (void *)__get_free_pages(gfp, get_order(size));
	if (ret) {
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
	}
	return ret;
}

EXPORT_SYMBOL(dma_alloc_coherent);

int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	   enum dma_data_direction direction)
{
	int i;

	for (i = 0; i < nents; i++ ) {
		void *va;

		BUG_ON(!sg[i].page);
		va = page_address(sg[i].page) + sg[i].offset;
		sg_dma_address(sg + i) = (dma_addr_t)virt_to_bus(va);
		sg_dma_len(sg + i) = sg[i].length;
	}

	return nents;
}

EXPORT_SYMBOL(dma_map_sg);

int
dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	return NULL;
}

void pci_iounmap(struct pci_dev *dev, void __iomem * addr)
{
}

EXPORT_SYMBOL(pci_iomap);
EXPORT_SYMBOL(pci_iounmap);
