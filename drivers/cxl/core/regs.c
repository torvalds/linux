// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <cxlmem.h>
#include <cxlpci.h>

#include "core.h"

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
	u32 cap_array;

	*map = (struct cxl_component_reg_map) { 0 };

	/*
	 * CXL.cache and CXL.mem registers are at offset 0x1000 as defined in
	 * CXL 2.0 8.2.4 Table 141.
	 */
	base += CXL_CM_OFFSET;

	cap_array = readl(base + CXL_CM_CAP_HDR_OFFSET);

	if (FIELD_GET(CXL_CM_CAP_HDR_ID_MASK, cap_array) != CM_CAP_HDR_CAP_ID) {
		dev_err(dev,
			"Couldn't locate the CXL.cache and CXL.mem capability array header.\n");
		return;
	}

	/* It's assumed that future versions will be backward compatible */
	cap_count = FIELD_GET(CXL_CM_CAP_HDR_ARRAY_SIZE_MASK, cap_array);

	for (cap = 1; cap <= cap_count; cap++) {
		void __iomem *register_block;
		struct cxl_reg_map *rmap;
		u16 cap_id, offset;
		u32 length, hdr;

		hdr = readl(base + cap * 0x4);

		cap_id = FIELD_GET(CXL_CM_CAP_HDR_ID_MASK, hdr);
		offset = FIELD_GET(CXL_CM_CAP_PTR_MASK, hdr);
		register_block = base + offset;
		hdr = readl(register_block);

		rmap = NULL;
		switch (cap_id) {
		case CXL_CM_CAP_CAP_ID_HDM: {
			int decoder_cnt;

			dev_dbg(dev, "found HDM decoder capability (0x%x)\n",
				offset);

			decoder_cnt = cxl_hdm_decoder_count(hdr);
			length = 0x20 * decoder_cnt + 0x10;
			rmap = &map->hdm_decoder;
			break;
		}
		case CXL_CM_CAP_CAP_ID_RAS:
			dev_dbg(dev, "found RAS capability (0x%x)\n",
				offset);
			length = CXL_RAS_CAPABILITY_LENGTH;
			rmap = &map->ras;
			break;
		default:
			dev_dbg(dev, "Unknown CM cap ID: %d (0x%x)\n", cap_id,
				offset);
			break;
		}

		if (!rmap)
			continue;
		rmap->valid = true;
		rmap->id = cap_id;
		rmap->offset = CXL_CM_OFFSET + offset;
		rmap->size = length;
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
		struct cxl_reg_map *rmap;
		u32 offset, length;
		u16 cap_id;

		cap_id = FIELD_GET(CXLDEV_CAP_HDR_CAP_ID_MASK,
				   readl(base + cap * 0x10));
		offset = readl(base + cap * 0x10 + 0x4);
		length = readl(base + cap * 0x10 + 0x8);

		rmap = NULL;
		switch (cap_id) {
		case CXLDEV_CAP_CAP_ID_DEVICE_STATUS:
			dev_dbg(dev, "found Status capability (0x%x)\n", offset);
			rmap = &map->status;
			break;
		case CXLDEV_CAP_CAP_ID_PRIMARY_MAILBOX:
			dev_dbg(dev, "found Mailbox capability (0x%x)\n", offset);
			rmap = &map->mbox;
			break;
		case CXLDEV_CAP_CAP_ID_SECONDARY_MAILBOX:
			dev_dbg(dev, "found Secondary Mailbox capability (0x%x)\n", offset);
			break;
		case CXLDEV_CAP_CAP_ID_MEMDEV:
			dev_dbg(dev, "found Memory Device capability (0x%x)\n", offset);
			rmap = &map->memdev;
			break;
		default:
			if (cap_id >= 0x8000)
				dev_dbg(dev, "Vendor cap ID: %#x offset: %#x\n", cap_id, offset);
			else
				dev_dbg(dev, "Unknown cap ID: %#x offset: %#x\n", cap_id, offset);
			break;
		}

		if (!rmap)
			continue;
		rmap->valid = true;
		rmap->id = cap_id;
		rmap->offset = offset;
		rmap->size = length;
	}
}
EXPORT_SYMBOL_NS_GPL(cxl_probe_device_regs, CXL);

void __iomem *devm_cxl_iomap_block(struct device *dev, resource_size_t addr,
				   resource_size_t length)
{
	void __iomem *ret_val;
	struct resource *res;

	if (WARN_ON_ONCE(addr == CXL_RESOURCE_NONE))
		return NULL;

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

int cxl_map_component_regs(struct device *dev, struct cxl_component_regs *regs,
			   struct cxl_register_map *map, unsigned long map_mask)
{
	struct mapinfo {
		struct cxl_reg_map *rmap;
		void __iomem **addr;
	} mapinfo[] = {
		{ &map->component_map.hdm_decoder, &regs->hdm_decoder },
		{ &map->component_map.ras, &regs->ras },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(mapinfo); i++) {
		struct mapinfo *mi = &mapinfo[i];
		resource_size_t phys_addr;
		resource_size_t length;

		if (!mi->rmap->valid)
			continue;
		if (!test_bit(mi->rmap->id, &map_mask))
			continue;
		phys_addr = map->resource + mi->rmap->offset;
		length = mi->rmap->size;
		*(mi->addr) = devm_cxl_iomap_block(dev, phys_addr, length);
		if (!*(mi->addr))
			return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_map_component_regs, CXL);

int cxl_map_device_regs(struct device *dev,
			struct cxl_device_regs *regs,
			struct cxl_register_map *map)
{
	resource_size_t phys_addr = map->resource;
	struct mapinfo {
		struct cxl_reg_map *rmap;
		void __iomem **addr;
	} mapinfo[] = {
		{ &map->device_map.status, &regs->status, },
		{ &map->device_map.mbox, &regs->mbox, },
		{ &map->device_map.memdev, &regs->memdev, },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(mapinfo); i++) {
		struct mapinfo *mi = &mapinfo[i];
		resource_size_t length;
		resource_size_t addr;

		if (!mi->rmap->valid)
			continue;

		addr = phys_addr + mi->rmap->offset;
		length = mi->rmap->size;
		*(mi->addr) = devm_cxl_iomap_block(dev, addr, length);
		if (!*(mi->addr))
			return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_map_device_regs, CXL);

static bool cxl_decode_regblock(struct pci_dev *pdev, u32 reg_lo, u32 reg_hi,
				struct cxl_register_map *map)
{
	int bar = FIELD_GET(CXL_DVSEC_REG_LOCATOR_BIR_MASK, reg_lo);
	u64 offset = ((u64)reg_hi << 32) |
		     (reg_lo & CXL_DVSEC_REG_LOCATOR_BLOCK_OFF_LOW_MASK);

	if (offset > pci_resource_len(pdev, bar)) {
		dev_warn(&pdev->dev,
			 "BAR%d: %pr: too small (offset: %pa, type: %d)\n", bar,
			 &pdev->resource[bar], &offset, map->reg_type);
		return false;
	}

	map->reg_type = FIELD_GET(CXL_DVSEC_REG_LOCATOR_BLOCK_ID_MASK, reg_lo);
	map->resource = pci_resource_start(pdev, bar) + offset;
	map->max_size = pci_resource_len(pdev, bar) - offset;
	return true;
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

	map->resource = CXL_RESOURCE_NONE;
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

		if (!cxl_decode_regblock(pdev, reg_lo, reg_hi, map))
			continue;

		if (map->reg_type == type)
			return 0;
	}

	map->resource = CXL_RESOURCE_NONE;
	return -ENODEV;
}
EXPORT_SYMBOL_NS_GPL(cxl_find_regblock, CXL);

resource_size_t __rcrb_to_component(struct device *dev, struct cxl_rcrb_info *ri,
				    enum cxl_rcrb which)
{
	resource_size_t component_reg_phys;
	resource_size_t rcrb = ri->base;
	void __iomem *addr;
	u32 bar0, bar1;
	u16 cmd;
	u32 id;

	if (which == CXL_RCRB_UPSTREAM)
		rcrb += SZ_4K;

	/*
	 * RCRB's BAR[0..1] point to component block containing CXL
	 * subsystem component registers. MEMBAR extraction follows
	 * the PCI Base spec here, esp. 64 bit extraction and memory
	 * ranges alignment (6.0, 7.5.1.2.1).
	 */
	if (!request_mem_region(rcrb, SZ_4K, "CXL RCRB"))
		return CXL_RESOURCE_NONE;
	addr = ioremap(rcrb, SZ_4K);
	if (!addr) {
		dev_err(dev, "Failed to map region %pr\n", addr);
		release_mem_region(rcrb, SZ_4K);
		return CXL_RESOURCE_NONE;
	}

	id = readl(addr + PCI_VENDOR_ID);
	cmd = readw(addr + PCI_COMMAND);
	bar0 = readl(addr + PCI_BASE_ADDRESS_0);
	bar1 = readl(addr + PCI_BASE_ADDRESS_1);
	iounmap(addr);
	release_mem_region(rcrb, SZ_4K);

	/*
	 * Sanity check, see CXL 3.0 Figure 9-8 CXL Device that Does Not
	 * Remap Upstream Port and Component Registers
	 */
	if (id == U32_MAX) {
		if (which == CXL_RCRB_DOWNSTREAM)
			dev_err(dev, "Failed to access Downstream Port RCRB\n");
		return CXL_RESOURCE_NONE;
	}
	if (!(cmd & PCI_COMMAND_MEMORY))
		return CXL_RESOURCE_NONE;
	/* The RCRB is a Memory Window, and the MEM_TYPE_1M bit is obsolete */
	if (bar0 & (PCI_BASE_ADDRESS_MEM_TYPE_1M | PCI_BASE_ADDRESS_SPACE_IO))
		return CXL_RESOURCE_NONE;

	component_reg_phys = bar0 & PCI_BASE_ADDRESS_MEM_MASK;
	if (bar0 & PCI_BASE_ADDRESS_MEM_TYPE_64)
		component_reg_phys |= ((u64)bar1) << 32;

	if (!component_reg_phys)
		return CXL_RESOURCE_NONE;

	/* MEMBAR is block size (64k) aligned. */
	if (!IS_ALIGNED(component_reg_phys, CXL_COMPONENT_REG_BLOCK_SIZE))
		return CXL_RESOURCE_NONE;

	return component_reg_phys;
}

resource_size_t cxl_rcd_component_reg_phys(struct device *dev,
					   struct cxl_dport *dport)
{
	if (!dport->rch)
		return CXL_RESOURCE_NONE;
	return __rcrb_to_component(dev, &dport->rcrb, CXL_RCRB_UPSTREAM);
}
EXPORT_SYMBOL_NS_GPL(cxl_rcd_component_reg_phys, CXL);
