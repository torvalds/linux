/*
 * Contains routines needed to support swiotlb for ppc.
 *
 * Copyright (C) 2009 Becky Bruce, Freescale Semiconductor
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/dma-mapping.h>
#include <linux/pfn.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pci.h>

#include <asm/machdep.h>
#include <asm/swiotlb.h>
#include <asm/dma.h>
#include <asm/abs_addr.h>

int swiotlb __read_mostly;
unsigned int ppc_swiotlb_enable;

void *swiotlb_bus_to_virt(struct device *hwdev, dma_addr_t addr)
{
	unsigned long pfn = PFN_DOWN(swiotlb_bus_to_phys(hwdev, addr));
	void *pageaddr = page_address(pfn_to_page(pfn));

	if (pageaddr != NULL)
		return pageaddr + (addr % PAGE_SIZE);
	return NULL;
}

dma_addr_t swiotlb_phys_to_bus(struct device *hwdev, phys_addr_t paddr)
{
	return paddr + get_dma_direct_offset(hwdev);
}

phys_addr_t swiotlb_bus_to_phys(struct device *hwdev, dma_addr_t baddr)

{
	return baddr - get_dma_direct_offset(hwdev);
}

/*
 * Determine if an address needs bounce buffering via swiotlb.
 * Going forward I expect the swiotlb code to generalize on using
 * a dma_ops->addr_needs_map, and this function will move from here to the
 * generic swiotlb code.
 */
int
swiotlb_arch_address_needs_mapping(struct device *hwdev, dma_addr_t addr,
				   size_t size)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(hwdev);

	BUG_ON(!dma_ops);
	return dma_ops->addr_needs_map(hwdev, addr, size);
}

/*
 * Determine if an address is reachable by a pci device, or if we must bounce.
 */
static int
swiotlb_pci_addr_needs_map(struct device *hwdev, dma_addr_t addr, size_t size)
{
	u64 mask = dma_get_mask(hwdev);
	dma_addr_t max;
	struct pci_controller *hose;
	struct pci_dev *pdev = to_pci_dev(hwdev);

	hose = pci_bus_to_host(pdev->bus);
	max = hose->dma_window_base_cur + hose->dma_window_size;

	/* check that we're within mapped pci window space */
	if ((addr + size > max) | (addr < hose->dma_window_base_cur))
		return 1;

	return !is_buffer_dma_capable(mask, addr, size);
}

static int
swiotlb_addr_needs_map(struct device *hwdev, dma_addr_t addr, size_t size)
{
	return !is_buffer_dma_capable(dma_get_mask(hwdev), addr, size);
}


/*
 * At the moment, all platforms that use this code only require
 * swiotlb to be used if we're operating on HIGHMEM.  Since
 * we don't ever call anything other than map_sg, unmap_sg,
 * map_page, and unmap_page on highmem, use normal dma_ops
 * for everything else.
 */
struct dma_mapping_ops swiotlb_dma_ops = {
	.alloc_coherent = dma_direct_alloc_coherent,
	.free_coherent = dma_direct_free_coherent,
	.map_sg = swiotlb_map_sg_attrs,
	.unmap_sg = swiotlb_unmap_sg_attrs,
	.dma_supported = swiotlb_dma_supported,
	.map_page = swiotlb_map_page,
	.unmap_page = swiotlb_unmap_page,
	.addr_needs_map = swiotlb_addr_needs_map,
	.sync_single_range_for_cpu = swiotlb_sync_single_range_for_cpu,
	.sync_single_range_for_device = swiotlb_sync_single_range_for_device,
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = swiotlb_sync_sg_for_device
};

struct dma_mapping_ops swiotlb_pci_dma_ops = {
	.alloc_coherent = dma_direct_alloc_coherent,
	.free_coherent = dma_direct_free_coherent,
	.map_sg = swiotlb_map_sg_attrs,
	.unmap_sg = swiotlb_unmap_sg_attrs,
	.dma_supported = swiotlb_dma_supported,
	.map_page = swiotlb_map_page,
	.unmap_page = swiotlb_unmap_page,
	.addr_needs_map = swiotlb_pci_addr_needs_map,
	.sync_single_range_for_cpu = swiotlb_sync_single_range_for_cpu,
	.sync_single_range_for_device = swiotlb_sync_single_range_for_device,
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = swiotlb_sync_sg_for_device
};

static int ppc_swiotlb_bus_notify(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct device *dev = data;

	/* We are only intereted in device addition */
	if (action != BUS_NOTIFY_ADD_DEVICE)
		return 0;

	/* May need to bounce if the device can't address all of DRAM */
	if (dma_get_mask(dev) < lmb_end_of_DRAM())
		set_dma_ops(dev, &swiotlb_dma_ops);

	return NOTIFY_DONE;
}

static struct notifier_block ppc_swiotlb_plat_bus_notifier = {
	.notifier_call = ppc_swiotlb_bus_notify,
	.priority = 0,
};

static struct notifier_block ppc_swiotlb_of_bus_notifier = {
	.notifier_call = ppc_swiotlb_bus_notify,
	.priority = 0,
};

int __init swiotlb_setup_bus_notifier(void)
{
	bus_register_notifier(&platform_bus_type,
			      &ppc_swiotlb_plat_bus_notifier);
	bus_register_notifier(&of_platform_bus_type,
			      &ppc_swiotlb_of_bus_notifier);

	return 0;
}
