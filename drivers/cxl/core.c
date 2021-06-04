// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "cxl.h"

/**
 * DOC: cxl core
 *
 * The CXL core provides a sysfs hierarchy for control devices and a rendezvous
 * point for cross-device interleave coordination through cxl ports.
 */

/**
 * cxl_probe_device_regs() - Detect CXL Device register blocks
 * @dev: Host device of the @base mapping
 * @base: Mapping of CXL 2.0 8.2.8 CXL Device Register Interface
 * @map: Map object describing the register block information found
 *
 * Probe for device register information and return it in map object.
 */
void cxl_probe_device_regs(struct device *dev, void __iomem *base,
			   struct cxl_device_reg_map *map)
{
	int cap, cap_count;
	u64 cap_array;

	*map = (struct cxl_device_reg_map){ 0 };

	cap_array = readq(base + CXLDEV_CAP_ARRAY_OFFSET);
	if (FIELD_GET(CXLDEV_CAP_ARRAY_ID_MASK, cap_array) !=
	    CXLDEV_CAP_ARRAY_CAP_ID)
		return;

	cap_count = FIELD_GET(CXLDEV_CAP_ARRAY_COUNT_MASK, cap_array);

	for (cap = 1; cap <= cap_count; cap++) {
		u32 offset, length;
		u16 cap_id;

		cap_id = FIELD_GET(CXLDEV_CAP_HDR_CAP_ID_MASK,
				   readl(base + cap * 0x10));
		offset = readl(base + cap * 0x10 + 0x4);
		length = readl(base + cap * 0x10 + 0x8);

		switch (cap_id) {
		case CXLDEV_CAP_CAP_ID_DEVICE_STATUS:
			dev_dbg(dev, "found Status capability (0x%x)\n", offset);

			map->status.valid = true;
			map->status.offset = offset;
			map->status.size = length;
			break;
		case CXLDEV_CAP_CAP_ID_PRIMARY_MAILBOX:
			dev_dbg(dev, "found Mailbox capability (0x%x)\n", offset);
			map->mbox.valid = true;
			map->mbox.offset = offset;
			map->mbox.size = length;
			break;
		case CXLDEV_CAP_CAP_ID_SECONDARY_MAILBOX:
			dev_dbg(dev, "found Secondary Mailbox capability (0x%x)\n", offset);
			break;
		case CXLDEV_CAP_CAP_ID_MEMDEV:
			dev_dbg(dev, "found Memory Device capability (0x%x)\n", offset);
			map->memdev.valid = true;
			map->memdev.offset = offset;
			map->memdev.size = length;
			break;
		default:
			if (cap_id >= 0x8000)
				dev_dbg(dev, "Vendor cap ID: %#x offset: %#x\n", cap_id, offset);
			else
				dev_dbg(dev, "Unknown cap ID: %#x offset: %#x\n", cap_id, offset);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(cxl_probe_device_regs);

static void __iomem *devm_cxl_iomap_block(struct pci_dev *pdev,
					  resource_size_t addr,
					  resource_size_t length)
{
	struct device *dev = &pdev->dev;
	void __iomem *ret_val;
	struct resource *res;

	res = devm_request_mem_region(dev, addr, length, pci_name(pdev));
	if (!res) {
		resource_size_t end = addr + length - 1;

		dev_err(dev, "Failed to request region %pa-%pa\n", &addr, &end);
		return NULL;
	}

	ret_val = devm_ioremap(dev, addr, length);
	if (!ret_val)
		dev_err(dev, "Failed to map region %pr\n", res);

	return ret_val;
}

int cxl_map_device_regs(struct pci_dev *pdev,
			struct cxl_device_regs *regs,
			struct cxl_register_map *map)
{
	resource_size_t phys_addr;

	phys_addr = pci_resource_start(pdev, map->barno);
	phys_addr += map->block_offset;

	if (map->device_map.status.valid) {
		resource_size_t addr;
		resource_size_t length;

		addr = phys_addr + map->device_map.status.offset;
		length = map->device_map.status.size;
		regs->status = devm_cxl_iomap_block(pdev, addr, length);
		if (!regs->status)
			return -ENOMEM;
	}

	if (map->device_map.mbox.valid) {
		resource_size_t addr;
		resource_size_t length;

		addr = phys_addr + map->device_map.mbox.offset;
		length = map->device_map.mbox.size;
		regs->mbox = devm_cxl_iomap_block(pdev, addr, length);
		if (!regs->mbox)
			return -ENOMEM;
	}

	if (map->device_map.memdev.valid) {
		resource_size_t addr;
		resource_size_t length;

		addr = phys_addr + map->device_map.memdev.offset;
		length = map->device_map.memdev.size;
		regs->memdev = devm_cxl_iomap_block(pdev, addr, length);
		if (!regs->memdev)
			return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_map_device_regs);

struct bus_type cxl_bus_type = {
	.name = "cxl",
};
EXPORT_SYMBOL_GPL(cxl_bus_type);

static __init int cxl_core_init(void)
{
	return bus_register(&cxl_bus_type);
}

static void cxl_core_exit(void)
{
	bus_unregister(&cxl_bus_type);
}

module_init(cxl_core_init);
module_exit(cxl_core_exit);
MODULE_LICENSE("GPL v2");
