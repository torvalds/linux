// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#include <linux/net/intel/libie/pci.h>

/**
 * libie_find_mmio_region - find MMIO region containing a range
 * @mmio_list: list that contains MMIO region info
 * @offset: range start offset
 * @size: range size
 * @bar_idx: BAR index containing the range to search
 *
 * MMIO regions mappings are traversed from oldest to newest.
 *
 * Return: pointer to a MMIO region overlapping with the range in any way or
 *	   NULL if no such region is mapped.
 */
static struct libie_pci_mmio_region *
libie_find_mmio_region(const struct list_head *mmio_list,
		       resource_size_t offset, resource_size_t size,
		       int bar_idx)
{
	resource_size_t end_offset = offset + size;
	struct libie_pci_mmio_region *mr;

	list_for_each_entry(mr, mmio_list, list) {
		resource_size_t mr_end = mr->offset + mr->size;
		resource_size_t mr_start = mr->offset;

		if (mr->bar_idx != bar_idx)
			continue;
		if (offset < mr_end && end_offset > mr_start)
			return mr;
	}

	return NULL;
}

/**
 * __libie_pci_get_mmio_addr - get the MMIO virtual address
 * @mmio_info: contains list of MMIO regions
 * @offset: register offset to find
 * @num_args: number of additional arguments present
 * @...: optional BAR index (0 by default)
 *
 * This function finds the virtual address of a register offset by iterating
 * through the non-linear MMIO regions that are mapped by the driver.
 *
 * The list is traversed oldest to newest, so accessing an older mapping
 * via this function while deleting a newer one is allowed.
 *
 * Return: valid MMIO virtual address or NULL.
 */
void __iomem *__libie_pci_get_mmio_addr(struct libie_mmio_info *mmio_info,
					resource_size_t offset,
					int num_args, ...)
{
	struct libie_pci_mmio_region *mr;
	int bar_idx = 0;
	va_list args;

	if (num_args) {
		va_start(args, num_args);
		bar_idx = va_arg(args, int);
		va_end(args);
	}

	list_for_each_entry(mr, &mmio_info->mmio_list, list)
		if (bar_idx == mr->bar_idx && offset >= mr->offset &&
		    offset < mr->offset + mr->size) {
			offset -= mr->offset;

			return mr->addr + offset;
		}

	WARN_ONCE(true, "Access to an unmapped MMIO region (BAR%d, offset %pa)",
		  bar_idx, &offset);

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(__libie_pci_get_mmio_addr, "LIBIE_PCI");

/**
 * __libie_pci_map_mmio_region - map PCI device MMIO region
 * @mmio_info: struct to store the mapped MMIO region
 * @offset: MMIO region start offset
 * @size: MMIO region size
 * @num_args: number of additional arguments present
 * @...: optional BAR index (0 by default)
 *
 * Return: true if the requested address range is accessible through
 *	   new or existing mapping, false otherwise.
 */
bool __libie_pci_map_mmio_region(struct libie_mmio_info *mmio_info,
				 resource_size_t offset,
				 resource_size_t size, int num_args, ...)
{
	struct pci_dev *pdev = mmio_info->pdev;
	struct libie_pci_mmio_region *mr;
	resource_size_t end_offset;
	void __iomem *va;
	int bar_idx = 0;
	va_list args;

	if (num_args) {
		va_start(args, num_args);
		bar_idx = va_arg(args, int);
		va_end(args);
	}

	if (bar_idx >= PCI_STD_NUM_BARS || bar_idx < 0 ||
	    !pci_resource_is_mem(pdev, bar_idx))
		return false;

	/* pci_iomap_range() silently maps less
	 * if the requested length is too big
	 */
	if (!size || check_add_overflow(offset, size, &end_offset) ||
	    end_offset > pci_resource_len(pdev, bar_idx))
		return false;

	mr = libie_find_mmio_region(&mmio_info->mmio_list, offset, size,
				    bar_idx);
	if (mr) {
		pci_warn(pdev,
			 "Mapping of BAR%u (offset=%llu, size=%llu) intersecting region (offset=%llu, size=%llu) already exists\n",
			 bar_idx, (unsigned long long)mr->offset,
			 (unsigned long long)mr->size,
			 (unsigned long long)offset, (unsigned long long)size);
		return mr->offset <= offset &&
		       mr->offset + mr->size >= end_offset;
	}

	va = pci_iomap_range(mmio_info->pdev, bar_idx, offset, size);
	if (!va) {
		pci_err(pdev, "Failed to map BAR%u region\n", bar_idx);
		return false;
	}

	mr = kvzalloc_obj(*mr);
	if (!mr) {
		pci_iounmap(pdev, va);
		return false;
	}

	mr->addr = va;
	mr->offset = offset;
	mr->size = size;
	mr->bar_idx = bar_idx;

	list_add_tail(&mr->list, &mmio_info->mmio_list);

	return true;
}
EXPORT_SYMBOL_NS_GPL(__libie_pci_map_mmio_region, "LIBIE_PCI");

/**
 * libie_pci_unmap_fltr_regs - unmap selected PCI device MMIO regions
 * @mmio_info: contains list of MMIO regions to unmap
 * @fltr: returns true, if region is to be unmapped
 */
void libie_pci_unmap_fltr_regs(struct libie_mmio_info *mmio_info,
			       bool (*fltr)(struct libie_mmio_info *mmio_info,
					    struct libie_pci_mmio_region *reg))
{
	struct libie_pci_mmio_region *mr, *tmp;

	list_for_each_entry_safe(mr, tmp, &mmio_info->mmio_list, list) {
		if (!fltr(mmio_info, mr))
			continue;
		list_del(&mr->list);
		pci_iounmap(mmio_info->pdev, mr->addr);
		kvfree(mr);
	}
}
EXPORT_SYMBOL_NS_GPL(libie_pci_unmap_fltr_regs, "LIBIE_PCI");

/**
 * libie_pci_unmap_all_mmio_regions - unmap all PCI device MMIO regions
 * @mmio_info: contains list of MMIO regions to unmap
 */
void libie_pci_unmap_all_mmio_regions(struct libie_mmio_info *mmio_info)
{
	struct libie_pci_mmio_region *mr, *tmp;

	list_for_each_entry_safe(mr, tmp, &mmio_info->mmio_list, list) {
		list_del(&mr->list);
		pci_iounmap(mmio_info->pdev, mr->addr);
		kvfree(mr);
	}
}
EXPORT_SYMBOL_NS_GPL(libie_pci_unmap_all_mmio_regions, "LIBIE_PCI");

/**
 * libie_pci_init_dev - enable and configure the device
 * @pdev: PCI device information
 *
 * Enable the device, request memory regions, set 64-bit DMA mask
 * and coherent DMA mask, and enable bus-mastering
 *
 * Return: %0 on success, -%errno on failure.
 */
int libie_pci_init_dev(struct pci_dev *pdev)
{
	int err;

	err = pcim_enable_device(pdev);
	if (err)
		return err;

	for (int bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		if (pci_resource_flags(pdev, bar) & IORESOURCE_MEM) {
			err = pcim_request_region(pdev, bar, pci_name(pdev));
			if (err)
				return err;
		}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err)
		return err;

	pci_set_master(pdev);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(libie_pci_init_dev, "LIBIE_PCI");

MODULE_DESCRIPTION("Common Ethernet PCI library");
MODULE_LICENSE("GPL");
