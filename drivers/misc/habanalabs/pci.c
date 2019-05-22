// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"
#include "include/hw_ip/pci/pci_general.h"

#include <linux/pci.h>

/**
 * hl_pci_bars_map() - Map PCI BARs.
 * @hdev: Pointer to hl_device structure.
 * @bar_name: Array of BAR names.
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

/*
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

/*
 * hl_pci_elbi_write() - Write through the ELBI interface.
 * @hdev: Pointer to hl_device structure.
 *
 * Return: 0 on success, negative value for failure.
 */
static int hl_pci_elbi_write(struct hl_device *hdev, u64 addr, u32 data)
{
	struct pci_dev *pdev = hdev->pdev;
	ktime_t timeout;
	u32 val;

	/* Clear previous status */
	pci_write_config_dword(pdev, mmPCI_CONFIG_ELBI_STS, 0);

	pci_write_config_dword(pdev, mmPCI_CONFIG_ELBI_ADDR, (u32) addr);
	pci_write_config_dword(pdev, mmPCI_CONFIG_ELBI_DATA, data);
	pci_write_config_dword(pdev, mmPCI_CONFIG_ELBI_CTRL,
				PCI_CONFIG_ELBI_CTRL_WRITE);

	timeout = ktime_add_ms(ktime_get(), 10);
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

	if (val & PCI_CONFIG_ELBI_STS_ERR) {
		dev_err(hdev->dev, "Error writing to ELBI\n");
		return -EIO;
	}

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
 *
 * Return: 0 on success, negative value for failure.
 */
int hl_pci_iatu_write(struct hl_device *hdev, u32 addr, u32 data)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 dbi_offset;
	int rc;

	dbi_offset = addr & 0xFFF;

	rc = hl_pci_elbi_write(hdev, prop->pcie_aux_dbi_reg_addr, 0x00300000);
	rc |= hl_pci_elbi_write(hdev, prop->pcie_dbi_base_address + dbi_offset,
				data);

	if (rc)
		return -EIO;

	return 0;
}

/*
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
 * hl_pci_set_dram_bar_base() - Set DDR BAR to map specific device address.
 * @hdev: Pointer to hl_device structure.
 * @inbound_region: Inbound region number.
 * @bar: PCI BAR number.
 * @addr: Address in DRAM. Must be aligned to DRAM bar size.
 *
 * Configure the iATU so that the DRAM bar will start at the specified address.
 *
 * Return: 0 on success, negative value for failure.
 */
int hl_pci_set_dram_bar_base(struct hl_device *hdev, u8 inbound_region, u8 bar,
				u64 addr)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 offset;
	int rc;

	switch (inbound_region) {
	case 0:
		offset = 0x100;
		break;
	case 1:
		offset = 0x300;
		break;
	case 2:
		offset = 0x500;
		break;
	default:
		dev_err(hdev->dev, "Invalid inbound region %d\n",
			inbound_region);
		return -EINVAL;
	}

	if (bar != 0 && bar != 2 && bar != 4) {
		dev_err(hdev->dev, "Invalid PCI BAR %d\n", bar);
		return -EINVAL;
	}

	/* Point to the specified address */
	rc = hl_pci_iatu_write(hdev, offset + 0x14, lower_32_bits(addr));
	rc |= hl_pci_iatu_write(hdev, offset + 0x18, upper_32_bits(addr));
	rc |= hl_pci_iatu_write(hdev, offset + 0x0, 0);
	/* Enable + BAR match + match enable + BAR number */
	rc |= hl_pci_iatu_write(hdev, offset + 0x4, 0xC0080000 | (bar << 8));

	/* Return the DBI window to the default location */
	rc |= hl_pci_elbi_write(hdev, prop->pcie_aux_dbi_reg_addr, 0);
	rc |= hl_pci_elbi_write(hdev, prop->pcie_aux_dbi_reg_addr + 4, 0);

	if (rc)
		dev_err(hdev->dev, "failed to map DRAM bar to 0x%08llx\n",
			addr);

	return rc;
}

/**
 * hl_pci_init_iatu() - Initialize the iATU unit inside the PCI controller.
 * @hdev: Pointer to hl_device structure.
 * @sram_base_address: SRAM base address.
 * @dram_base_address: DRAM base address.
 * @host_phys_base_address: Base physical address of host memory for device
 *                          transactions.
 * @host_phys_size: Size of host memory for device transactions.
 *
 * This is needed in case the firmware doesn't initialize the iATU.
 *
 * Return: 0 on success, negative value for failure.
 */
int hl_pci_init_iatu(struct hl_device *hdev, u64 sram_base_address,
			u64 dram_base_address, u64 host_phys_base_address,
			u64 host_phys_size)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 host_phys_end_addr;
	int rc = 0;

	/* Inbound Region 0 - Bar 0 - Point to SRAM base address */
	rc  = hl_pci_iatu_write(hdev, 0x114, lower_32_bits(sram_base_address));
	rc |= hl_pci_iatu_write(hdev, 0x118, upper_32_bits(sram_base_address));
	rc |= hl_pci_iatu_write(hdev, 0x100, 0);
	/* Enable + Bar match + match enable */
	rc |= hl_pci_iatu_write(hdev, 0x104, 0xC0080000);

	/* Point to DRAM */
	if (!hdev->asic_funcs->set_dram_bar_base)
		return -EINVAL;
	if (hdev->asic_funcs->set_dram_bar_base(hdev, dram_base_address) ==
								U64_MAX)
		return -EIO;


	/* Outbound Region 0 - Point to Host */
	host_phys_end_addr = host_phys_base_address + host_phys_size - 1;
	rc |= hl_pci_iatu_write(hdev, 0x008,
				lower_32_bits(host_phys_base_address));
	rc |= hl_pci_iatu_write(hdev, 0x00C,
				upper_32_bits(host_phys_base_address));
	rc |= hl_pci_iatu_write(hdev, 0x010, lower_32_bits(host_phys_end_addr));
	rc |= hl_pci_iatu_write(hdev, 0x014, 0);
	rc |= hl_pci_iatu_write(hdev, 0x018, 0);
	rc |= hl_pci_iatu_write(hdev, 0x020, upper_32_bits(host_phys_end_addr));
	/* Increase region size */
	rc |= hl_pci_iatu_write(hdev, 0x000, 0x00002000);
	/* Enable */
	rc |= hl_pci_iatu_write(hdev, 0x004, 0x80000000);

	/* Return the DBI window to the default location */
	rc |= hl_pci_elbi_write(hdev, prop->pcie_aux_dbi_reg_addr, 0);
	rc |= hl_pci_elbi_write(hdev, prop->pcie_aux_dbi_reg_addr + 4, 0);

	if (rc)
		return -EIO;

	return 0;
}

/**
 * hl_pci_set_dma_mask() - Set DMA masks for the device.
 * @hdev: Pointer to hl_device structure.
 * @dma_mask: number of bits for the requested dma mask.
 *
 * This function sets the DMA masks (regular and consistent) for a specified
 * value. If it doesn't succeed, it tries to set it to a fall-back value
 *
 * Return: 0 on success, non-zero for failure.
 */
int hl_pci_set_dma_mask(struct hl_device *hdev, u8 dma_mask)
{
	struct pci_dev *pdev = hdev->pdev;
	int rc;

	/* set DMA mask */
	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(dma_mask));
	if (rc) {
		dev_warn(hdev->dev,
			"Failed to set pci dma mask to %d bits, error %d\n",
			dma_mask, rc);

		dma_mask = hdev->dma_mask;

		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(dma_mask));
		if (rc) {
			dev_err(hdev->dev,
				"Failed to set pci dma mask to %d bits, error %d\n",
				dma_mask, rc);
			return rc;
		}
	}

	/*
	 * We managed to set the dma mask, so update the dma mask field. If
	 * the set to the coherent mask will fail with that mask, we will
	 * fail the entire function
	 */
	hdev->dma_mask = dma_mask;

	rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(dma_mask));
	if (rc) {
		dev_err(hdev->dev,
			"Failed to set pci consistent dma mask to %d bits, error %d\n",
			dma_mask, rc);
		return rc;
	}

	return 0;
}

/**
 * hl_pci_init() - PCI initialization code.
 * @hdev: Pointer to hl_device structure.
 * @dma_mask: number of bits for the requested dma mask.
 *
 * Set DMA masks, initialize the PCI controller and map the PCI BARs.
 *
 * Return: 0 on success, non-zero for failure.
 */
int hl_pci_init(struct hl_device *hdev, u8 dma_mask)
{
	struct pci_dev *pdev = hdev->pdev;
	int rc;

	rc = hl_pci_set_dma_mask(hdev, dma_mask);
	if (rc)
		return rc;

	if (hdev->reset_pcilink)
		hl_pci_reset_link_through_bridge(hdev);

	rc = pci_enable_device_mem(pdev);
	if (rc) {
		dev_err(hdev->dev, "can't enable PCI device\n");
		return rc;
	}

	pci_set_master(pdev);

	rc = hdev->asic_funcs->init_iatu(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize iATU\n");
		goto disable_device;
	}

	rc = hdev->asic_funcs->pci_bars_map(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize PCI BARs\n");
		goto disable_device;
	}

	return 0;

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
