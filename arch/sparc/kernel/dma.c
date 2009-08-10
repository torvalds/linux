/* dma.c: PCI and SBUS DMA accessors for 32-bit sparc.
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/mm.h>

#ifdef CONFIG_PCI
#include <linux/pci.h>
#endif

/*
 * Return whether the given PCI device DMA address mask can be
 * supported properly.  For example, if your device can only drive the
 * low 24-bits during PCI bus mastering, then you would pass
 * 0x00ffffff as the mask to this function.
 */
int dma_supported(struct device *dev, u64 mask)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type)
		return 1;
#endif
	return 0;
}
EXPORT_SYMBOL(dma_supported);

int dma_set_mask(struct device *dev, u64 dma_mask)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type)
		return pci_set_dma_mask(to_pci_dev(dev), dma_mask);
#endif
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(dma_set_mask);
