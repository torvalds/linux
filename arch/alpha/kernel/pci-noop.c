/*
 *	linux/arch/alpha/kernel/pci-noop.c
 *
 * Stub PCI interfaces for Jensen-specific kernels.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/gfp.h>
#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

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

static void *alpha_noop_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t gfp)
{
	void *ret;

	if (!dev || *dev->dma_mask >= 0xffffffffUL)
		gfp &= ~GFP_DMA;
	ret = (void *)__get_free_pages(gfp, get_order(size));
	if (ret) {
		memset(ret, 0, size);
		*dma_handle = virt_to_phys(ret);
	}
	return ret;
}

static void alpha_noop_free_coherent(struct device *dev, size_t size,
				     void *cpu_addr, dma_addr_t dma_addr)
{
	free_pages((unsigned long)cpu_addr, get_order(size));
}

static dma_addr_t alpha_noop_map_page(struct device *dev, struct page *page,
				      unsigned long offset, size_t size,
				      enum dma_data_direction dir,
				      struct dma_attrs *attrs)
{
	return page_to_pa(page) + offset;
}

static int alpha_noop_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
			     enum dma_data_direction dir, struct dma_attrs *attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		void *va;

		BUG_ON(!sg_page(sg));
		va = sg_virt(sg);
		sg_dma_address(sg) = (dma_addr_t)virt_to_phys(va);
		sg_dma_len(sg) = sg->length;
	}

	return nents;
}

static int alpha_noop_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

static int alpha_noop_supported(struct device *dev, u64 mask)
{
	return mask < 0x00ffffffUL ? 0 : 1;
}

static int alpha_noop_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;
	return 0;
}

struct dma_map_ops alpha_noop_ops = {
	.alloc_coherent		= alpha_noop_alloc_coherent,
	.free_coherent		= alpha_noop_free_coherent,
	.map_page		= alpha_noop_map_page,
	.map_sg			= alpha_noop_map_sg,
	.mapping_error		= alpha_noop_mapping_error,
	.dma_supported		= alpha_noop_supported,
	.set_dma_mask		= alpha_noop_set_mask,
};

struct dma_map_ops *dma_ops = &alpha_noop_ops;
EXPORT_SYMBOL(dma_ops);

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	return NULL;
}

void pci_iounmap(struct pci_dev *dev, void __iomem * addr)
{
}

EXPORT_SYMBOL(pci_iomap);
EXPORT_SYMBOL(pci_iounmap);
