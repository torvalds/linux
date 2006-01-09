/*
 * Copyright (C) 2004 IBM Corporation
 *
 * Implements the generic device dma API for ppc64. Handles
 * the pci and vio busses
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
/* Include the busses we support */
#include <linux/pci.h>
#include <asm/vio.h>
#include <asm/ibmebus.h>
#include <asm/scatterlist.h>
#include <asm/bug.h>

static struct dma_mapping_ops *get_dma_ops(struct device *dev)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type)
		return &pci_dma_ops;
#endif
#ifdef CONFIG_IBMVIO
	if (dev->bus == &vio_bus_type)
		return &vio_dma_ops;
#endif
#ifdef CONFIG_IBMEBUS
	if (dev->bus == &ibmebus_bus_type)
		return &ibmebus_dma_ops;
#endif
	return NULL;
}

int dma_supported(struct device *dev, u64 mask)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	if (dma_ops)
		return dma_ops->dma_supported(dev, mask);
	BUG();
	return 0;
}
EXPORT_SYMBOL(dma_supported);

int dma_set_mask(struct device *dev, u64 dma_mask)
{
#ifdef CONFIG_PCI
	if (dev->bus == &pci_bus_type)
		return pci_set_dma_mask(to_pci_dev(dev), dma_mask);
#endif
#ifdef CONFIG_IBMVIO
	if (dev->bus == &vio_bus_type)
		return -EIO;
#endif /* CONFIG_IBMVIO */
#ifdef CONFIG_IBMEBUS
	if (dev->bus == &ibmebus_bus_type)
		return -EIO;
#endif
	BUG();
	return 0;
}
EXPORT_SYMBOL(dma_set_mask);

void *dma_alloc_coherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t flag)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	if (dma_ops)
		return dma_ops->alloc_coherent(dev, size, dma_handle, flag);
	BUG();
	return NULL;
}
EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_handle)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	if (dma_ops)
		dma_ops->free_coherent(dev, size, cpu_addr, dma_handle);
	else
		BUG();
}
EXPORT_SYMBOL(dma_free_coherent);

dma_addr_t dma_map_single(struct device *dev, void *cpu_addr, size_t size,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	if (dma_ops)
		return dma_ops->map_single(dev, cpu_addr, size, direction);
	BUG();
	return (dma_addr_t)0;
}
EXPORT_SYMBOL(dma_map_single);

void dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	if (dma_ops)
		dma_ops->unmap_single(dev, dma_addr, size, direction);
	else
		BUG();
}
EXPORT_SYMBOL(dma_unmap_single);

dma_addr_t dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	if (dma_ops)
		return dma_ops->map_single(dev,
				(page_address(page) + offset), size, direction);
	BUG();
	return (dma_addr_t)0;
}
EXPORT_SYMBOL(dma_map_page);

void dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	if (dma_ops)
		dma_ops->unmap_single(dev, dma_address, size, direction);
	else
		BUG();
}
EXPORT_SYMBOL(dma_unmap_page);

int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	if (dma_ops)
		return dma_ops->map_sg(dev, sg, nents, direction);
	BUG();
	return 0;
}
EXPORT_SYMBOL(dma_map_sg);

void dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	if (dma_ops)
		dma_ops->unmap_sg(dev, sg, nhwentries, direction);
	else
		BUG();
}
EXPORT_SYMBOL(dma_unmap_sg);
