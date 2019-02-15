// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "goyaP.h"
#include "include/goya/asic_reg/goya_masks.h"

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/genalloc.h>

/*
 * GOYA security scheme:
 *
 * 1. Host is protected by:
 *        - Range registers (When MMU is enabled, DMA RR does NOT protect host)
 *        - MMU
 *
 * 2. DRAM is protected by:
 *        - Range registers (protect the first 512MB)
 *        - MMU (isolation between users)
 *
 * 3. Configuration is protected by:
 *        - Range registers
 *        - Protection bits
 *
 * When MMU is disabled:
 *
 * QMAN DMA: PQ, CQ, CP, DMA are secured.
 * PQ, CB and the data are on the host.
 *
 * QMAN TPC/MME:
 * PQ, CQ and CP are not secured.
 * PQ, CB and the data are on the SRAM/DRAM.
 *
 * Since QMAN DMA is secured, KMD is parsing the DMA CB:
 *     - KMD checks DMA pointer
 *     - WREG, MSG_PROT are not allowed.
 *     - MSG_LONG/SHORT are allowed.
 *
 * A read/write transaction by the QMAN to a protected area will succeed if
 * and only if the QMAN's CP is secured and MSG_PROT is used
 *
 *
 * When MMU is enabled:
 *
 * QMAN DMA: PQ, CQ and CP are secured.
 * MMU is set to bypass on the Secure props register of the QMAN.
 * The reasons we don't enable MMU for PQ, CQ and CP are:
 *     - PQ entry is in kernel address space and KMD doesn't map it.
 *     - CP writes to MSIX register and to kernel address space (completion
 *       queue).
 *
 * DMA is not secured but because CP is secured, KMD still needs to parse the
 * CB, but doesn't need to check the DMA addresses.
 *
 * For QMAN DMA 0, DMA is also secured because only KMD uses this DMA and KMD
 * doesn't map memory in MMU.
 *
 * QMAN TPC/MME: PQ, CQ and CP aren't secured (no change from MMU disabled mode)
 *
 * DMA RR does NOT protect host because DMA is not secured
 *
 */

#define GOYA_MMU_REGS_NUM		61

#define GOYA_DMA_POOL_BLK_SIZE		0x100		/* 256 bytes */

#define GOYA_RESET_TIMEOUT_MSEC		500		/* 500ms */
#define GOYA_PLDM_RESET_TIMEOUT_MSEC	20000		/* 20s */
#define GOYA_RESET_WAIT_MSEC		1		/* 1ms */
#define GOYA_CPU_RESET_WAIT_MSEC	100		/* 100ms */
#define GOYA_PLDM_RESET_WAIT_MSEC	1000		/* 1s */
#define GOYA_CPU_TIMEOUT_USEC		10000000	/* 10s */
#define GOYA_TEST_QUEUE_WAIT_USEC	100000		/* 100ms */

#define GOYA_QMAN0_FENCE_VAL		0xD169B243

#define GOYA_MAX_INITIATORS		20

static void goya_get_fixed_properties(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	prop->completion_queues_count = NUMBER_OF_CMPLT_QUEUES;

	prop->dram_base_address = DRAM_PHYS_BASE;
	prop->dram_size = DRAM_PHYS_DEFAULT_SIZE;
	prop->dram_end_address = prop->dram_base_address + prop->dram_size;
	prop->dram_user_base_address = DRAM_BASE_ADDR_USER;

	prop->sram_base_address = SRAM_BASE_ADDR;
	prop->sram_size = SRAM_SIZE;
	prop->sram_end_address = prop->sram_base_address + prop->sram_size;
	prop->sram_user_base_address = prop->sram_base_address +
						SRAM_USER_BASE_OFFSET;

	prop->host_phys_base_address = HOST_PHYS_BASE;
	prop->va_space_host_start_address = VA_HOST_SPACE_START;
	prop->va_space_host_end_address = VA_HOST_SPACE_END;
	prop->va_space_dram_start_address = VA_DDR_SPACE_START;
	prop->va_space_dram_end_address = VA_DDR_SPACE_END;
	prop->cfg_size = CFG_SIZE;
	prop->max_asid = MAX_ASID;
	prop->tpc_enabled_mask = TPC_ENABLED_MASK;

	prop->high_pll = PLL_HIGH_DEFAULT;
}

/*
 * goya_pci_bars_map - Map PCI BARS of Goya device
 *
 * @hdev: pointer to hl_device structure
 *
 * Request PCI regions and map them to kernel virtual addresses.
 * Returns 0 on success
 *
 */
int goya_pci_bars_map(struct hl_device *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	int rc;

	rc = pci_request_regions(pdev, HL_NAME);
	if (rc) {
		dev_err(hdev->dev, "Cannot obtain PCI resources\n");
		return rc;
	}

	hdev->pcie_bar[SRAM_CFG_BAR_ID] =
			pci_ioremap_bar(pdev, SRAM_CFG_BAR_ID);
	if (!hdev->pcie_bar[SRAM_CFG_BAR_ID]) {
		dev_err(hdev->dev, "pci_ioremap_bar failed for CFG\n");
		rc = -ENODEV;
		goto err_release_regions;
	}

	hdev->pcie_bar[MSIX_BAR_ID] = pci_ioremap_bar(pdev, MSIX_BAR_ID);
	if (!hdev->pcie_bar[MSIX_BAR_ID]) {
		dev_err(hdev->dev, "pci_ioremap_bar failed for MSIX\n");
		rc = -ENODEV;
		goto err_unmap_sram_cfg;
	}

	hdev->pcie_bar[DDR_BAR_ID] = pci_ioremap_wc_bar(pdev, DDR_BAR_ID);
	if (!hdev->pcie_bar[DDR_BAR_ID]) {
		dev_err(hdev->dev, "pci_ioremap_bar failed for DDR\n");
		rc = -ENODEV;
		goto err_unmap_msix;
	}

	hdev->rmmio = hdev->pcie_bar[SRAM_CFG_BAR_ID] +
				(CFG_BASE - SRAM_BASE_ADDR);

	return 0;

err_unmap_msix:
	iounmap(hdev->pcie_bar[MSIX_BAR_ID]);
err_unmap_sram_cfg:
	iounmap(hdev->pcie_bar[SRAM_CFG_BAR_ID]);
err_release_regions:
	pci_release_regions(pdev);

	return rc;
}

/*
 * goya_pci_bars_unmap - Unmap PCI BARS of Goya device
 *
 * @hdev: pointer to hl_device structure
 *
 * Release all PCI BARS and unmap their virtual addresses
 *
 */
static void goya_pci_bars_unmap(struct hl_device *hdev)
{
	struct pci_dev *pdev = hdev->pdev;

	iounmap(hdev->pcie_bar[DDR_BAR_ID]);
	iounmap(hdev->pcie_bar[MSIX_BAR_ID]);
	iounmap(hdev->pcie_bar[SRAM_CFG_BAR_ID]);
	pci_release_regions(pdev);
}

/*
 * goya_elbi_write - Write through the ELBI interface
 *
 * @hdev: pointer to hl_device structure
 *
 * return 0 on success, -1 on failure
 *
 */
static int goya_elbi_write(struct hl_device *hdev, u64 addr, u32 data)
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

/*
 * goya_iatu_write - iatu write routine
 *
 * @hdev: pointer to hl_device structure
 *
 */
static int goya_iatu_write(struct hl_device *hdev, u32 addr, u32 data)
{
	u32 dbi_offset;
	int rc;

	dbi_offset = addr & 0xFFF;

	rc = goya_elbi_write(hdev, CFG_BASE + mmPCIE_AUX_DBI, 0x00300000);
	rc |= goya_elbi_write(hdev, mmPCIE_DBI_BASE + dbi_offset, data);

	if (rc)
		return -EIO;

	return 0;
}

void goya_reset_link_through_bridge(struct hl_device *hdev)
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

/*
 * goya_set_ddr_bar_base - set DDR bar to map specific device address
 *
 * @hdev: pointer to hl_device structure
 * @addr: address in DDR. Must be aligned to DDR bar size
 *
 * This function configures the iATU so that the DDR bar will start at the
 * specified addr.
 *
 */
static int goya_set_ddr_bar_base(struct hl_device *hdev, u64 addr)
{
	struct goya_device *goya = hdev->asic_specific;
	int rc;

	if ((goya) && (goya->ddr_bar_cur_addr == addr))
		return 0;

	/* Inbound Region 1 - Bar 4 - Point to DDR */
	rc = goya_iatu_write(hdev, 0x314, lower_32_bits(addr));
	rc |= goya_iatu_write(hdev, 0x318, upper_32_bits(addr));
	rc |= goya_iatu_write(hdev, 0x300, 0);
	/* Enable + Bar match + match enable + Bar 4 */
	rc |= goya_iatu_write(hdev, 0x304, 0xC0080400);

	/* Return the DBI window to the default location */
	rc |= goya_elbi_write(hdev, CFG_BASE + mmPCIE_AUX_DBI, 0);
	rc |= goya_elbi_write(hdev, CFG_BASE + mmPCIE_AUX_DBI_32, 0);

	if (rc) {
		dev_err(hdev->dev, "failed to map DDR bar to 0x%08llx\n", addr);
		return -EIO;
	}

	if (goya)
		goya->ddr_bar_cur_addr = addr;

	return 0;
}

/*
 * goya_init_iatu - Initialize the iATU unit inside the PCI controller
 *
 * @hdev: pointer to hl_device structure
 *
 * This is needed in case the firmware doesn't initialize the iATU
 *
 */
static int goya_init_iatu(struct hl_device *hdev)
{
	int rc;

	/* Inbound Region 0 - Bar 0 - Point to SRAM_BASE_ADDR */
	rc  = goya_iatu_write(hdev, 0x114, lower_32_bits(SRAM_BASE_ADDR));
	rc |= goya_iatu_write(hdev, 0x118, upper_32_bits(SRAM_BASE_ADDR));
	rc |= goya_iatu_write(hdev, 0x100, 0);
	/* Enable + Bar match + match enable */
	rc |= goya_iatu_write(hdev, 0x104, 0xC0080000);

	/* Inbound Region 1 - Bar 4 - Point to DDR */
	rc |= goya_set_ddr_bar_base(hdev, DRAM_PHYS_BASE);

	/* Outbound Region 0 - Point to Host */
	rc |= goya_iatu_write(hdev, 0x008, lower_32_bits(HOST_PHYS_BASE));
	rc |= goya_iatu_write(hdev, 0x00C, upper_32_bits(HOST_PHYS_BASE));
	rc |= goya_iatu_write(hdev, 0x010,
		lower_32_bits(HOST_PHYS_BASE + HOST_PHYS_SIZE - 1));
	rc |= goya_iatu_write(hdev, 0x014, 0);
	rc |= goya_iatu_write(hdev, 0x018, 0);
	rc |= goya_iatu_write(hdev, 0x020,
		upper_32_bits(HOST_PHYS_BASE + HOST_PHYS_SIZE - 1));
	/* Increase region size */
	rc |= goya_iatu_write(hdev, 0x000, 0x00002000);
	/* Enable */
	rc |= goya_iatu_write(hdev, 0x004, 0x80000000);

	/* Return the DBI window to the default location */
	rc |= goya_elbi_write(hdev, CFG_BASE + mmPCIE_AUX_DBI, 0);
	rc |= goya_elbi_write(hdev, CFG_BASE + mmPCIE_AUX_DBI_32, 0);

	if (rc)
		return -EIO;

	return 0;
}

/*
 * goya_early_init - GOYA early initialization code
 *
 * @hdev: pointer to hl_device structure
 *
 * Verify PCI bars
 * Set DMA masks
 * PCI controller initialization
 * Map PCI bars
 *
 */
static int goya_early_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pci_dev *pdev = hdev->pdev;
	u32 val;
	int rc;

	goya_get_fixed_properties(hdev);

	/* Check BAR sizes */
	if (pci_resource_len(pdev, SRAM_CFG_BAR_ID) != CFG_BAR_SIZE) {
		dev_err(hdev->dev,
			"Not " HL_NAME "? BAR %d size %llu, expecting %llu\n",
			SRAM_CFG_BAR_ID,
			(unsigned long long) pci_resource_len(pdev,
							SRAM_CFG_BAR_ID),
			CFG_BAR_SIZE);
		return -ENODEV;
	}

	if (pci_resource_len(pdev, MSIX_BAR_ID) != MSIX_BAR_SIZE) {
		dev_err(hdev->dev,
			"Not " HL_NAME "? BAR %d size %llu, expecting %llu\n",
			MSIX_BAR_ID,
			(unsigned long long) pci_resource_len(pdev,
								MSIX_BAR_ID),
			MSIX_BAR_SIZE);
		return -ENODEV;
	}

	prop->dram_pci_bar_size = pci_resource_len(pdev, DDR_BAR_ID);

	/* set DMA mask for GOYA */
	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(39));
	if (rc) {
		dev_warn(hdev->dev, "Unable to set pci dma mask to 39 bits\n");
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc) {
			dev_err(hdev->dev,
				"Unable to set pci dma mask to 32 bits\n");
			return rc;
		}
	}

	rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(39));
	if (rc) {
		dev_warn(hdev->dev,
			"Unable to set pci consistent dma mask to 39 bits\n");
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc) {
			dev_err(hdev->dev,
				"Unable to set pci consistent dma mask to 32 bits\n");
			return rc;
		}
	}

	if (hdev->reset_pcilink)
		goya_reset_link_through_bridge(hdev);

	rc = pci_enable_device_mem(pdev);
	if (rc) {
		dev_err(hdev->dev, "can't enable PCI device\n");
		return rc;
	}

	pci_set_master(pdev);

	rc = goya_init_iatu(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize iATU\n");
		goto disable_device;
	}

	rc = goya_pci_bars_map(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize PCI BARS\n");
		goto disable_device;
	}

	val = RREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS);
	if (val & PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_SRIOV_EN_MASK)
		dev_warn(hdev->dev,
			"PCI strap is not configured correctly, PCI bus errors may occur\n");

	return 0;

disable_device:
	pci_clear_master(pdev);
	pci_disable_device(pdev);

	return rc;
}

/*
 * goya_early_fini - GOYA early finalization code
 *
 * @hdev: pointer to hl_device structure
 *
 * Unmap PCI bars
 *
 */
int goya_early_fini(struct hl_device *hdev)
{
	goya_pci_bars_unmap(hdev);

	pci_clear_master(hdev->pdev);
	pci_disable_device(hdev->pdev);

	return 0;
}

/*
 * goya_sw_init - Goya software initialization code
 *
 * @hdev: pointer to hl_device structure
 *
 */
static int goya_sw_init(struct hl_device *hdev)
{
	struct goya_device *goya;
	int rc;

	/* Allocate device structure */
	goya = kzalloc(sizeof(*goya), GFP_KERNEL);
	if (!goya)
		return -ENOMEM;

	/* according to goya_init_iatu */
	goya->ddr_bar_cur_addr = DRAM_PHYS_BASE;
	hdev->asic_specific = goya;

	/* Create DMA pool for small allocations */
	hdev->dma_pool = dma_pool_create(dev_name(hdev->dev),
			&hdev->pdev->dev, GOYA_DMA_POOL_BLK_SIZE, 8, 0);
	if (!hdev->dma_pool) {
		dev_err(hdev->dev, "failed to create DMA pool\n");
		rc = -ENOMEM;
		goto free_goya_device;
	}

	hdev->cpu_accessible_dma_mem =
			hdev->asic_funcs->dma_alloc_coherent(hdev,
					CPU_ACCESSIBLE_MEM_SIZE,
					&hdev->cpu_accessible_dma_address,
					GFP_KERNEL | __GFP_ZERO);

	if (!hdev->cpu_accessible_dma_mem) {
		dev_err(hdev->dev,
			"failed to allocate %d of dma memory for CPU accessible memory space\n",
			CPU_ACCESSIBLE_MEM_SIZE);
		rc = -ENOMEM;
		goto free_dma_pool;
	}

	hdev->cpu_accessible_dma_pool = gen_pool_create(CPU_PKT_SHIFT, -1);
	if (!hdev->cpu_accessible_dma_pool) {
		dev_err(hdev->dev,
			"Failed to create CPU accessible DMA pool\n");
		rc = -ENOMEM;
		goto free_cpu_pq_dma_mem;
	}

	rc = gen_pool_add(hdev->cpu_accessible_dma_pool,
				(uintptr_t) hdev->cpu_accessible_dma_mem,
				CPU_ACCESSIBLE_MEM_SIZE, -1);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to add memory to CPU accessible DMA pool\n");
		rc = -EFAULT;
		goto free_cpu_pq_pool;
	}

	spin_lock_init(&goya->hw_queues_lock);

	return 0;

free_cpu_pq_pool:
	gen_pool_destroy(hdev->cpu_accessible_dma_pool);
free_cpu_pq_dma_mem:
	hdev->asic_funcs->dma_free_coherent(hdev, CPU_ACCESSIBLE_MEM_SIZE,
			hdev->cpu_accessible_dma_mem,
			hdev->cpu_accessible_dma_address);
free_dma_pool:
	dma_pool_destroy(hdev->dma_pool);
free_goya_device:
	kfree(goya);

	return rc;
}

/*
 * goya_sw_fini - Goya software tear-down code
 *
 * @hdev: pointer to hl_device structure
 *
 */
int goya_sw_fini(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;

	gen_pool_destroy(hdev->cpu_accessible_dma_pool);

	hdev->asic_funcs->dma_free_coherent(hdev, CPU_ACCESSIBLE_MEM_SIZE,
			hdev->cpu_accessible_dma_mem,
			hdev->cpu_accessible_dma_address);

	dma_pool_destroy(hdev->dma_pool);

	kfree(goya);

	return 0;
}

int goya_suspend(struct hl_device *hdev)
{
	return 0;
}

int goya_resume(struct hl_device *hdev)
{
	return 0;
}

void *goya_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	return dma_alloc_coherent(&hdev->pdev->dev, size, dma_handle, flags);
}

void goya_dma_free_coherent(struct hl_device *hdev, size_t size, void *cpu_addr,
				dma_addr_t dma_handle)
{
	dma_free_coherent(&hdev->pdev->dev, size, cpu_addr, dma_handle);
}

static const struct hl_asic_funcs goya_funcs = {
	.early_init = goya_early_init,
	.early_fini = goya_early_fini,
	.sw_init = goya_sw_init,
	.sw_fini = goya_sw_fini,
	.suspend = goya_suspend,
	.resume = goya_resume,
	.dma_alloc_coherent = goya_dma_alloc_coherent,
	.dma_free_coherent = goya_dma_free_coherent,
};

/*
 * goya_set_asic_funcs - set Goya function pointers
 *
 * @*hdev: pointer to hl_device structure
 *
 */
void goya_set_asic_funcs(struct hl_device *hdev)
{
	hdev->asic_funcs = &goya_funcs;
}
