// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"
#include "../include/hw_ip/pci/pci_general.h"

#include <linux/pci.h>

#define HL_PLDM_PCI_ELBI_TIMEOUT_MSEC	(HL_PCI_ELBI_TIMEOUT_MSEC * 10)

#define IATU_REGION_CTRL_REGION_EN_MASK		BIT(31)
#define IATU_REGION_CTRL_MATCH_MODE_MASK	BIT(30)
#define IATU_REGION_CTRL_NUM_MATCH_EN_MASK	BIT(19)
#define IATU_REGION_CTRL_BAR_NUM_MASK		GENMASK(10, 8)

/**
 * hl_pci_bars_map() - Map PCI BARs.
 * @hdev: Pointer to hl_device structure.
 * @name: Array of BAR names.
 * @is_wc: Array with flag per BAR whether a write-combined mapping is needed.
 *
 * Request PCI regions and map them to kernel virtual addresses.
 *
 * Return: 0 on success, non-zero for failure.
 */
int hl_pci_bars_map(struct hl_device *hdev, const char * const name[3],
			bool is_wc[3])
{
	struct pci_dev *pdev = hdev->pdev;
	int rc, i, bar;

	rc = pci_request_regions(pdev, HL_NAME);
	if (rc) {
		dev_err(hdev->dev, "Cannot obtain PCI resources\n");
		return rc;
	}

	for (i = 0 ; i < 3 ; i++) {
		bar = i * 2; /* 64-bit BARs */
		hdev->pcie_bar[bar] = is_wc[i] ?
				pci_ioremap_wc_bar(pdev, bar) :
				pci_ioremap_bar(pdev, bar);
		if (!hdev->pcie_bar[bar]) {
			dev_err(hdev->dev, "pci_ioremap%s_bar failed for %s\n",
					is_wc[i] ? "_wc" : "", name[i]);
			rc = -ENODEV;
			goto err;
		}
	}

	return 0;

err:
	for (i = 2 ; i >= 0 ; i--) {
		bar = i * 2; /* 64-bit BARs */
		if (hdev->pcie_bar[bar])
			iounmap(hdev->pcie_bar[bar]);
	}

	pci_release_regions(pdev);

	return rc;
}

/**
 * hl_pci_bars_unmap() - Unmap PCI BARS.
 * @hdev: Pointer to hl_device structure.
 *
 * Release all PCI BARs and unmap their virtual addresses.
 */
static void hl_pci_bars_unmap(struct hl_device *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	int i, bar;

	for (i = 2 ; i >= 0 ; i--) {
		bar = i * 2; /* 64-bit BARs */
		iounmap(hdev->pcie_bar[bar]);
	}

	pci_release_regions(pdev);
}

/**
 * hl_pci_elbi_write() - Write through the ELBI interface.
 * @hdev: Pointer to hl_device structure.
 * @addr: Address to write to
 * @data: Data to write
 *
 * Return: 0 on success, negative value for failure.
 */
static int hl_pci_elbi_write(struct hl_device *hdev, u64 addr, u32 data)
{
	struct pci_dev *pdev = hdev->pdev;
	ktime_t timeout;
	u64 msec;
	u32 val;

	if (hdev->pldm)
		msec = HL_PLDM_PCI_ELBI_TIMEOUT_MSEC;
	else
		msec = HL_PCI_ELBI_TIMEOUT_MSEC;

	/* Clear previous status */
	pci_write_config_dword(pdev, mmPCI_CONFIG_ELBI_STS, 0);

	pci_write_config_dword(pdev, mmPCI_CONFIG_ELBI_ADDR, (u32) addr);
	pci_write_config_dword(pdev, mmPCI_CONFIG_ELBI_DATA, data);
	pci_write_config_dword(pdev, mmPCI_CONFIG_ELBI_CTRL,
				PCI_CONFIG_ELBI_CTRL_WRITE);

	timeout = ktime_add_ms(ktime_get(), msec);
	for (;;) {
		pci_read_config_dword(pdev, mmPCI_CONFIG_ELBI_STS, &val);
		if (val & PCI_CONFIG_ELBI_STS_MASK)
			break;
		if (ktime_compare(ktime_get(), timeout) > 0) {
			pci_read_config_dword(pdev, mmPCI_CONFIG_ELBI_STS,
						&val);
			break;
		}

		usleep_range(300, 500);
	}

	if ((val & PCI_CONFIG_ELBI_STS_MASK) == PCI_CONFIG_ELBI_STS_DONE)
		return 0;

	if (val & PCI_CONFIG_ELBI_STS_ERR)
		return -EIO;

	if (!(val & PCI_CONFIG_ELBI_STS_MASK)) {
		dev_err(hdev->dev, "ELBI write didn't finish in time\n");
		return -EIO;
	}

	dev_err(hdev->dev, "ELBI write has undefined bits in status\n");
	return -EIO;
}

/**
 * hl_pci_iatu_write() - iatu write routine.
 * @hdev: Pointer to hl_device structure.
 * @addr: Address to write to
 * @data: Data to write
 *
 * Return: 0 on success, negative value for failure.
 */
int hl_pci_iatu_write(struct hl_device *hdev, u32 addr, u32 data)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 dbi_offset;
	int rc;

	dbi_offset = addr & 0xFFF;

	/* Ignore result of writing to pcie_aux_dbi_reg_addr as it could fail
	 * in case the firmware security is enabled
	 */
	hl_pci_elbi_write(hdev, prop->pcie_aux_dbi_reg_addr, 0x00300000);

	rc = hl_pci_elbi_write(hdev, prop->pcie_dbi_base_address + dbi_offset,
				data);

	if (rc)
		return -EIO;

	return 0;
}

/**
 * hl_pci_reset_link_through_bridge() - Reset PCI link.
 * @hdev: Pointer to hl_device structure.
 */
static void hl_pci_reset_link_through_bridge(struct hl_device *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	struct pci_dev *parent_port;
	u16 val;

	parent_port = pdev->bus->self;
	pci_read_config_word(parent_port, PCI_BRIDGE_CONTROL, &val);
	val |= PCI_BRIDGE_CTL_BUS_RESET;
	pci_write_config_word(parent_port, PCI_BRIDGE_CONTROL, val);
	ssleep(1);

	val &= ~(PCI_BRIDGE_CTL_BUS_RESET);
	pci_write_config_word(parent_port, PCI_BRIDGE_CONTROL, val);
	ssleep(3);
}

/**
 * hl_pci_set_inbound_region() - Configure inbound region
 * @hdev: Pointer to hl_device structure.
 * @region: Inbound region number.
 * @pci_region: Inbound region parameters.
 *
 * Configure the iATU inbound region.
 *
 * Return: 0 on success, negative value for failure.
 */
int hl_pci_set_inbound_region(struct hl_device *hdev, u8 region,
		struct hl_inbound_pci_region *pci_region)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 bar_phys_base, region_base, region_end_address;
	u32 offset, ctrl_reg_val;
	int rc = 0;

	/* region offset */
	offset = (0x200 * region) + 0x100;

	if (pci_region->mode == PCI_ADDRESS_MATCH_MODE) {
		bar_phys_base = hdev->pcie_bar_phys[pci_region->bar];
		region_base = bar_phys_base + pci_region->offset_in_bar;
		region_end_address = region_base + pci_region->size - 1;

		rc |= hl_pci_iatu_write(hdev, offset + 0x8,
				lower_32_bits(region_base));
		rc |= hl_pci_iatu_write(hdev, offset + 0xC,
				upper_32_bits(region_base));
		rc |= hl_pci_iatu_write(hdev, offset + 0x10,
				lower_32_bits(region_end_address));
	}

	/* Point to the specified address */
	rc |= hl_pci_iatu_write(hdev, offset + 0x14,
			lower_32_bits(pci_region->addr));
	rc |= hl_pci_iatu_write(hdev, offset + 0x18,
			upper_32_bits(pci_region->addr));
	rc |= hl_pci_iatu_write(hdev, offset + 0x0, 0);

	/* Enable + bar/address match + match enable + bar number */
	ctrl_reg_val = FIELD_PREP(IATU_REGION_CTRL_REGION_EN_MASK, 1);
	ctrl_reg_val |= FIELD_PREP(IATU_REGION_CTRL_MATCH_MODE_MASK,
			pci_region->mode);
	ctrl_reg_val |= FIELD_PREP(IATU_REGION_CTRL_NUM_MATCH_EN_MASK, 1);

	if (pci_region->mode == PCI_BAR_MATCH_MODE)
		ctrl_reg_val |= FIELD_PREP(IATU_REGION_CTRL_BAR_NUM_MASK,
				pci_region->bar);

	rc |= hl_pci_iatu_write(hdev, offset + 0x4, ctrl_reg_val);

	/* Return the DBI window to the default location
	 * Ignore result of writing to pcie_aux_dbi_reg_addr as it could fail
	 * in case the firmware security is enabled
	 */
	hl_pci_elbi_write(hdev, prop->pcie_aux_dbi_reg_addr, 0);

	if (rc)
		dev_err(hdev->dev, "failed to map bar %u to 0x%08llx\n",
				pci_region->bar, pci_region->addr);

	return rc;
}

/**
 * hl_pci_set_outbound_region() - Configure outbound region 0
 * @hdev: Pointer to hl_device structure.
 * @pci_region: Outbound region parameters.
 *
 * Configure the iATU outbound region 0.
 *
 * Return: 0 on success, negative value for failure.
 */
int hl_pci_set_outbound_region(struct hl_device *hdev,
		struct hl_outbound_pci_region *pci_region)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 outbound_region_end_address;
	int rc = 0;

	/* Outbound Region 0 */
	outbound_region_end_address =
			pci_region->addr + pci_region->size - 1;
	rc |= hl_pci_iatu_write(hdev, 0x008,
				lower_32_bits(pci_region->addr));
	rc |= hl_pci_iatu_write(hdev, 0x00C,
				upper_32_bits(pci_region->addr));
	rc |= hl_pci_iatu_write(hdev, 0x010,
				lower_32_bits(outbound_region_end_address));
	rc |= hl_pci_iatu_write(hdev, 0x014, 0);

	if ((hdev->power9_64bit_dma_enable) && (hdev->dma_mask == 64))
		rc |= hl_pci_iatu_write(hdev, 0x018, 0x08000000);
	else
		rc |= hl_pci_iatu_write(hdev, 0x018, 0);

	rc |= hl_pci_iatu_write(hdev, 0x020,
				upper_32_bits(outbound_region_end_address));
	/* Increase region size */
	rc |= hl_pci_iatu_write(hdev, 0x000, 0x00002000);
	/* Enable */
	rc |= hl_pci_iatu_write(hdev, 0x004, 0x80000000);

	/* Return the DBI window to the default location
	 * Ignore result of writing to pcie_aux_dbi_reg_addr as it could fail
	 * in case the firmware security is enabled
	 */
	hl_pci_elbi_write(hdev, prop->pcie_aux_dbi_reg_addr, 0);

	return rc;
}

/**
 * hl_pci_set_dma_mask() - Set DMA masks for the device.
 * @hdev: Pointer to hl_device structure.
 *
 * This function sets the DMA masks (regular and consistent) for a specified
 * value. If it doesn't succeed, it tries to set it to a fall-back value
 *
 * Return: 0 on success, non-zero for failure.
 */
static int hl_pci_set_dma_mask(struct hl_device *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	int rc;

	/* set DMA mask */
	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(hdev->dma_mask));
	if (rc) {
		dev_err(hdev->dev,
			"Failed to set pci dma mask to %d bits, error %d\n",
			hdev->dma_mask, rc);
		return rc;
	}

	rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(hdev->dma_mask));
	if (rc) {
		dev_err(hdev->dev,
			"Failed to set pci consistent dma mask to %d bits, error %d\n",
			hdev->dma_mask, rc);
		return rc;
	}

	return 0;
}

/**
 * hl_pci_init() - PCI initialization code.
 * @hdev: Pointer to hl_device structure.
 * @cpu_boot_status_reg: status register of the device's CPU
 * @boot_err0_reg: boot error register of the device's CPU
 * @preboot_ver_timeout: how much to wait before bailing out on reading
 *                       the preboot version
 *
 * Set DMA masks, initialize the PCI controller and map the PCI BARs.
 *
 * Return: 0 on success, non-zero for failure.
 */
int hl_pci_init(struct hl_device *hdev, u32 cpu_boot_status_reg,
		u32 boot_err0_reg, u32 preboot_ver_timeout)
{
	struct pci_dev *pdev = hdev->pdev;
	int rc;

	if (hdev->reset_pcilink)
		hl_pci_reset_link_through_bridge(hdev);

	rc = pci_enable_device_mem(pdev);
	if (rc) {
		dev_err(hdev->dev, "can't enable PCI device\n");
		return rc;
	}

	pci_set_master(pdev);

	rc = hdev->asic_funcs->pci_bars_map(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize PCI BARs\n");
		goto disable_device;
	}

	rc = hdev->asic_funcs->init_iatu(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize iATU\n");
		goto unmap_pci_bars;
	}

	rc = hl_pci_set_dma_mask(hdev);
	if (rc)
		goto unmap_pci_bars;

	/* Before continuing in the initialization, we need to read the preboot
	 * version to determine whether we run with a security-enabled firmware
	 * The check will be done in each ASIC's specific code
	 */
	rc = hl_fw_read_preboot_ver(hdev, cpu_boot_status_reg, boot_err0_reg,
					preboot_ver_timeout);
	if (rc)
		goto unmap_pci_bars;

	return 0;

unmap_pci_bars:
	hl_pci_bars_unmap(hdev);
disable_device:
	pci_clear_master(pdev);
	pci_disable_device(pdev);

	return rc;
}

/**
 * hl_fw_fini() - PCI finalization code.
 * @hdev: Pointer to hl_device structure
 *
 * Unmap PCI bars and disable PCI device.
 */
void hl_pci_fini(struct hl_device *hdev)
{
	hl_pci_bars_unmap(hdev);

	pci_clear_master(hdev->pdev);
	pci_disable_device(hdev->pdev);
}
