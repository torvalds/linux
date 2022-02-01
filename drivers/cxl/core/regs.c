// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <cxlmem.h>
#include <cxlpci.h>

/**
 * DOC: cxl registers
 *
 * CXL device capabilities are enumerated by PCI DVSEC (Designated
 * Vendor-specific) and / or descriptors provided by platform firmware.
 * They can be defined as a set like the device and component registers
 * mandated by CXL Section 8.1.12.2 Memory Device PCIe Capabilities and
 * Extended Capabilities, or they can be individual capabilities
 * appended to bridged and endpoint devices.
 *
 * Provide common infrastructure for enumerating and mapping these
 * discrete capabilities.
 */

/**
 * cxl_probe_component_regs() - Detect CXL Component register blocks
 * @dev: Host device of the @base mapping
 * @base: Mapping containing the HDM Decoder Capability Header
 * @map: Map object describing the register block information found
 *
 * See CXL 2.0 8.2.4 Component Register Layout and Definition
 * See CXL 2.0 8.2.5.5 CXL Device Register Interface
 *
 * Probe for component register information and return it in map object.
 */
void cxl_probe_component_regs(struct device *dev, void __iomem *base,
			      struct cxl_component_reg_map *map)
{
	int cap, cap_count;
	u64 cap_array;

	*map = (struct cxl_component_reg_map) { 0 };

	/*
	 * CXL.cache and CXL.mem registers are at offset 0x1000 as defined in
	 * CXL 2.0 8.2.4 Table 141.
	 */
	base += CXL_CM_OFFSET;

	cap_array = readq(base + CXL_CM_CAP_HDR_OFFSET);

	if (FIELD_GET(CXL_CM_CAP_HDR_ID_MASK, cap_array) != CM_CAP_HDR_CAP_ID) {
		dev_err(dev,
			"Couldn't locate the CXL.cache and CXL.mem capability array header.\n");
		return;
	}

	/* It's assumed that future versions will be backward compatible */
	cap_count = FIELD_GET(CXL_CM_CAP_HDR_ARRAY_SIZE_MASK, cap_array);

	for (cap = 1; cap <= cap_count; cap++) {
		void __iomem *register_block;
		u32 hdr;
		int decoder_cnt;
		u16 cap_id, offset;
		u32 length;

		hdr = readl(base + cap * 0x4);

		cap_id = FIELD_GET(CXL_CM_CAP_HDR_ID_MASK, hdr);
		offset = FIELD_GET(CXL_CM_CAP_PTR_MASK, hdr);
		register_block = base + offset;

		switch (cap_id) {
		case CXL_CM_CAP_CAP_ID_HDM:
			dev_dbg(dev, "found HDM decoder capability (0x%x)\n",
				offset);

			hdr = readl(register_block);

			decoder_cnt = cxl_hdm_decoder_count(hdr);
			length = 0x20 * decoder_cnt + 0x10;

			map->hdm_decoder.valid = true;
			map->hdm_decoder.offset = CXL_CM_OFFSET + offset;
			map->hdm_decoder.size = length;
			break;
		default:
			dev_dbg(dev, "Unknown CM cap ID: %d (0x%x)\n", cap_id,
				offset);
			break;
		}
	}
}
EXPORT_SYMBOL_NS_GPL(cxl_probe_component_regs, CXL);

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
EXPORT_SYMBOL_NS_GPL(cxl_probe_device_regs, CXL);

void __iomem *devm_cxl_iomap_block(struct device *dev, resource_size_t addr,
				   resource_size_t length)
{
	void __iomem *ret_val;
	struct resource *res;

	res = devm_request_mem_region(dev, addr, length, dev_name(dev));
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

int cxl_map_component_regs(struct pci_dev *pdev,
			   struct cxl_component_regs *regs,
			   struct cxl_register_map *map)
{
	struct device *dev = &pdev->dev;
	resource_size_t phys_addr;
	resource_size_t length;

	phys_addr = pci_resource_start(pdev, map->barno);
	phys_addr += map->block_offset;

	phys_addr += map->component_map.hdm_decoder.offset;
	length = map->component_map.hdm_decoder.size;
	regs->hdm_decoder = devm_cxl_iomap_block(dev, phys_addr, length);
	if (!regs->hdm_decoder)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_map_component_regs, CXL);

int cxl_map_device_regs(struct pci_dev *pdev,
			struct cxl_device_regs *regs,
			struct cxl_register_map *map)
{
	struct device *dev = &pdev->dev;
	resource_size_t phys_addr;

	phys_addr = pci_resource_start(pdev, map->barno);
	phys_addr += map->block_offset;

	if (map->device_map.status.valid) {
		resource_size_t addr;
		resource_size_t length;

		addr = phys_addr + map->device_map.status.offset;
		length = map->device_map.status.size;
		regs->status = devm_cxl_iomap_block(dev, addr, length);
		if (!regs->status)
			return -ENOMEM;
	}

	if (map->device_map.mbox.valid) {
		resource_size_t addr;
		resource_size_t length;

		addr = phys_addr + map->device_map.mbox.offset;
		length = map->device_map.mbox.size;
		regs->mbox = devm_cxl_iomap_block(dev, addr, length);
		if (!regs->mbox)
			return -ENOMEM;
	}

	if (map->device_map.memdev.valid) {
		resource_size_t addr;
		resource_size_t length;

		addr = phys_addr + map->device_map.memdev.offset;
		length = map->device_map.memdev.size;
		regs->memdev = devm_cxl_iomap_block(dev, addr, length);
		if (!regs->memdev)
			return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_map_device_regs, CXL);

static void cxl_decode_regblock(u32 reg_lo, u32 reg_hi,
				struct cxl_register_map *map)
{
	map->block_offset = ((u64)reg_hi << 32) |
			    (reg_lo & CXL_DVSEC_REG_LOCATOR_BLOCK_OFF_LOW_MASK);
	map->barno = FIELD_GET(CXL_DVSEC_REG_LOCATOR_BIR_MASK, reg_lo);
	map->reg_type = FIELD_GET(CXL_DVSEC_REG_LOCATOR_BLOCK_ID_MASK, reg_lo);
}

/**
 * cxl_find_regblock() - Locate register blocks by type
 * @pdev: The CXL PCI device to enumerate.
 * @type: Register Block Indicator id
 * @map: Enumeration output, clobbered on error
 *
 * Return: 0 if register block enumerated, negative error code otherwise
 *
 * A CXL DVSEC may point to one or more register blocks, search for them
 * by @type.
 */
int cxl_find_regblock(struct pci_dev *pdev, enum cxl_regloc_type type,
		      struct cxl_register_map *map)
{
	u32 regloc_size, regblocks;
	int regloc, i;

	map->block_offset = U64_MAX;
	regloc = pci_find_dvsec_capability(pdev, PCI_DVSEC_VENDOR_ID_CXL,
					   CXL_DVSEC_REG_LOCATOR);
	if (!regloc)
		return -ENXIO;

	pci_read_config_dword(pdev, regloc + PCI_DVSEC_HEADER1, &regloc_size);
	regloc_size = FIELD_GET(PCI_DVSEC_HEADER1_LENGTH_MASK, regloc_size);

	regloc += CXL_DVSEC_REG_LOCATOR_BLOCK1_OFFSET;
	regblocks = (regloc_size - CXL_DVSEC_REG_LOCATOR_BLOCK1_OFFSET) / 8;

	for (i = 0; i < regblocks; i++, regloc += 8) {
		u32 reg_lo, reg_hi;

		pci_read_config_dword(pdev, regloc, &reg_lo);
		pci_read_config_dword(pdev, regloc + 4, &reg_hi);

		cxl_decode_regblock(reg_lo, reg_hi, map);

		if (map->reg_type == type)
			return 0;
	}

	map->block_offset = U64_MAX;
	return -ENODEV;
}
EXPORT_SYMBOL_NS_GPL(cxl_find_regblock, CXL);
