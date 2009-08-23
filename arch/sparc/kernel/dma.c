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

#include "dma.h"

int dma_supported(struct device *dev, u64 mask)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type)
		return pci_dma_supported(to_pci_dev(dev), mask);
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

static void *dma32_alloc_coherent(struct device *dev, size_t size,
				  dma_addr_t *dma_handle, gfp_t flag)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type)
		return pci_alloc_consistent(to_pci_dev(dev), size, dma_handle);
#endif
	return sbus_alloc_consistent(dev, size, dma_handle);
}

static void dma32_free_coherent(struct device *dev, size_t size,
				void *cpu_addr, dma_addr_t dma_handle)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type) {
		pci_free_consistent(to_pci_dev(dev), size,
				    cpu_addr, dma_handle);
		return;
	}
#endif
	sbus_free_consistent(dev, size, cpu_addr, dma_handle);
}

static dma_addr_t dma32_map_page(struct device *dev, struct page *page,
				 unsigned long offset, size_t size,
				 enum dma_data_direction direction)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type)
		return pci_map_page(to_pci_dev(dev), page, offset,
				    size, (int)direction);
#endif
	return sbus_map_single(dev, page_address(page) + offset,
			       size, (int)direction);
}

static void dma32_unmap_page(struct device *dev, dma_addr_t dma_address,
			     size_t size, enum dma_data_direction direction)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type) {
		pci_unmap_page(to_pci_dev(dev), dma_address,
			       size, (int)direction);
		return;
	}
#endif
	sbus_unmap_single(dev, dma_address, size, (int)direction);
}

static int dma32_map_sg(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction direction)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type)
		return pci_map_sg(to_pci_dev(dev), sg, nents, (int)direction);
#endif
	return sbus_map_sg(dev, sg, nents, direction);
}

void dma32_unmap_sg(struct device *dev, struct scatterlist *sg,
		    int nents, enum dma_data_direction direction)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type) {
		pci_unmap_sg(to_pci_dev(dev), sg, nents, (int)direction);
		return;
	}
#endif
	sbus_unmap_sg(dev, sg, nents, (int)direction);
}

static void dma32_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
				      size_t size,
				      enum dma_data_direction direction)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type) {
		pci_dma_sync_single_for_cpu(to_pci_dev(dev), dma_handle,
					    size, (int)direction);
		return;
	}
#endif
	sbus_dma_sync_single_for_cpu(dev, dma_handle, size, (int) direction);
}

static void dma32_sync_single_for_device(struct device *dev,
					 dma_addr_t dma_handle, size_t size,
					 enum dma_data_direction direction)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type) {
		pci_dma_sync_single_for_device(to_pci_dev(dev), dma_handle,
					       size, (int)direction);
		return;
	}
#endif
	sbus_dma_sync_single_for_device(dev, dma_handle, size, (int) direction);
}

static void dma32_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
				  int nelems, enum dma_data_direction direction)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type) {
		pci_dma_sync_sg_for_cpu(to_pci_dev(dev), sg,
					nelems, (int)direction);
		return;
	}
#endif
	BUG();
}

static void dma32_sync_sg_for_device(struct device *dev,
				     struct scatterlist *sg, int nelems,
				     enum dma_data_direction direction)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type) {
		pci_dma_sync_sg_for_device(to_pci_dev(dev), sg,
					   nelems, (int)direction);
		return;
	}
#endif
	BUG();
}

static const struct dma_ops dma32_dma_ops = {
	.alloc_coherent		= dma32_alloc_coherent,
	.free_coherent		= dma32_free_coherent,
	.map_page		= dma32_map_page,
	.unmap_page		= dma32_unmap_page,
	.map_sg			= dma32_map_sg,
	.unmap_sg		= dma32_unmap_sg,
	.sync_single_for_cpu	= dma32_sync_single_for_cpu,
	.sync_single_for_device	= dma32_sync_single_for_device,
	.sync_sg_for_cpu	= dma32_sync_sg_for_cpu,
	.sync_sg_for_device	= dma32_sync_sg_for_device,
};

const struct dma_ops *dma_ops = &dma32_dma_ops;
EXPORT_SYMBOL(dma_ops);
