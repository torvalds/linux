// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "goyaP.h"
#include "include/goya/asic_reg/goya_masks.h"

#include <linux/pci.h>
#include <linux/genalloc.h>
#include <linux/firmware.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/io-64-nonatomic-hi-lo.h>

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

#define GOYA_CB_POOL_CB_CNT		512
#define GOYA_CB_POOL_CB_SIZE		0x20000		/* 128KB */

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
	prop->cb_pool_cb_cnt = GOYA_CB_POOL_CB_CNT;
	prop->cb_pool_cb_size = GOYA_CB_POOL_CB_SIZE;
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

	if (!hdev->pldm) {
		val = RREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS);
		if (val & PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_SRIOV_EN_MASK)
			dev_warn(hdev->dev,
				"PCI strap is not configured correctly, PCI bus errors may occur\n");
	}

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

static void goya_set_pll_refclk(struct hl_device *hdev)
{
	WREG32(mmCPU_PLL_DIV_SEL_0, 0x0);
	WREG32(mmCPU_PLL_DIV_SEL_1, 0x0);
	WREG32(mmCPU_PLL_DIV_SEL_2, 0x0);
	WREG32(mmCPU_PLL_DIV_SEL_3, 0x0);

	WREG32(mmIC_PLL_DIV_SEL_0, 0x0);
	WREG32(mmIC_PLL_DIV_SEL_1, 0x0);
	WREG32(mmIC_PLL_DIV_SEL_2, 0x0);
	WREG32(mmIC_PLL_DIV_SEL_3, 0x0);

	WREG32(mmMC_PLL_DIV_SEL_0, 0x0);
	WREG32(mmMC_PLL_DIV_SEL_1, 0x0);
	WREG32(mmMC_PLL_DIV_SEL_2, 0x0);
	WREG32(mmMC_PLL_DIV_SEL_3, 0x0);

	WREG32(mmPSOC_MME_PLL_DIV_SEL_0, 0x0);
	WREG32(mmPSOC_MME_PLL_DIV_SEL_1, 0x0);
	WREG32(mmPSOC_MME_PLL_DIV_SEL_2, 0x0);
	WREG32(mmPSOC_MME_PLL_DIV_SEL_3, 0x0);

	WREG32(mmPSOC_PCI_PLL_DIV_SEL_0, 0x0);
	WREG32(mmPSOC_PCI_PLL_DIV_SEL_1, 0x0);
	WREG32(mmPSOC_PCI_PLL_DIV_SEL_2, 0x0);
	WREG32(mmPSOC_PCI_PLL_DIV_SEL_3, 0x0);

	WREG32(mmPSOC_EMMC_PLL_DIV_SEL_0, 0x0);
	WREG32(mmPSOC_EMMC_PLL_DIV_SEL_1, 0x0);
	WREG32(mmPSOC_EMMC_PLL_DIV_SEL_2, 0x0);
	WREG32(mmPSOC_EMMC_PLL_DIV_SEL_3, 0x0);

	WREG32(mmTPC_PLL_DIV_SEL_0, 0x0);
	WREG32(mmTPC_PLL_DIV_SEL_1, 0x0);
	WREG32(mmTPC_PLL_DIV_SEL_2, 0x0);
	WREG32(mmTPC_PLL_DIV_SEL_3, 0x0);
}

static void goya_disable_clk_rlx(struct hl_device *hdev)
{
	WREG32(mmPSOC_MME_PLL_CLK_RLX_0, 0x100010);
	WREG32(mmIC_PLL_CLK_RLX_0, 0x100010);
}

static void _goya_tpc_mbist_workaround(struct hl_device *hdev, u8 tpc_id)
{
	u64 tpc_eml_address;
	u32 val, tpc_offset, tpc_eml_offset, tpc_slm_offset;
	int err, slm_index;

	tpc_offset = tpc_id * 0x40000;
	tpc_eml_offset = tpc_id * 0x200000;
	tpc_eml_address = (mmTPC0_EML_CFG_BASE + tpc_eml_offset - CFG_BASE);
	tpc_slm_offset = tpc_eml_address + 0x100000;

	/*
	 * Workaround for Bug H2 #2443 :
	 * "TPC SB is not initialized on chip reset"
	 */

	val = RREG32(mmTPC0_CFG_FUNC_MBIST_CNTRL + tpc_offset);
	if (val & TPC0_CFG_FUNC_MBIST_CNTRL_MBIST_ACTIVE_MASK)
		dev_warn(hdev->dev, "TPC%d MBIST ACTIVE is not cleared\n",
			tpc_id);

	WREG32(mmTPC0_CFG_FUNC_MBIST_PAT + tpc_offset, val & 0xFFFFF000);

	WREG32(mmTPC0_CFG_FUNC_MBIST_MEM_0 + tpc_offset, 0x37FF);
	WREG32(mmTPC0_CFG_FUNC_MBIST_MEM_1 + tpc_offset, 0x303F);
	WREG32(mmTPC0_CFG_FUNC_MBIST_MEM_2 + tpc_offset, 0x71FF);
	WREG32(mmTPC0_CFG_FUNC_MBIST_MEM_3 + tpc_offset, 0x71FF);
	WREG32(mmTPC0_CFG_FUNC_MBIST_MEM_4 + tpc_offset, 0x70FF);
	WREG32(mmTPC0_CFG_FUNC_MBIST_MEM_5 + tpc_offset, 0x70FF);
	WREG32(mmTPC0_CFG_FUNC_MBIST_MEM_6 + tpc_offset, 0x70FF);
	WREG32(mmTPC0_CFG_FUNC_MBIST_MEM_7 + tpc_offset, 0x70FF);
	WREG32(mmTPC0_CFG_FUNC_MBIST_MEM_8 + tpc_offset, 0x70FF);
	WREG32(mmTPC0_CFG_FUNC_MBIST_MEM_9 + tpc_offset, 0x70FF);

	WREG32_OR(mmTPC0_CFG_FUNC_MBIST_CNTRL + tpc_offset,
		1 << TPC0_CFG_FUNC_MBIST_CNTRL_MBIST_START_SHIFT);

	err = hl_poll_timeout(
		hdev,
		mmTPC0_CFG_FUNC_MBIST_CNTRL + tpc_offset,
		val,
		(val & TPC0_CFG_FUNC_MBIST_CNTRL_MBIST_DONE_MASK),
		1000,
		HL_DEVICE_TIMEOUT_USEC);

	if (err)
		dev_err(hdev->dev,
			"Timeout while waiting for TPC%d MBIST DONE\n", tpc_id);

	WREG32_OR(mmTPC0_EML_CFG_DBG_CNT + tpc_eml_offset,
		1 << TPC0_EML_CFG_DBG_CNT_CORE_RST_SHIFT);

	msleep(GOYA_RESET_WAIT_MSEC);

	WREG32_AND(mmTPC0_EML_CFG_DBG_CNT + tpc_eml_offset,
		~(1 << TPC0_EML_CFG_DBG_CNT_CORE_RST_SHIFT));

	msleep(GOYA_RESET_WAIT_MSEC);

	for (slm_index = 0 ; slm_index < 256 ; slm_index++)
		WREG32(tpc_slm_offset + (slm_index << 2), 0);

	val = RREG32(tpc_slm_offset);
}

static void goya_tpc_mbist_workaround(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	int i;

	if (hdev->pldm)
		return;

	if (goya->hw_cap_initialized & HW_CAP_TPC_MBIST)
		return;

	/* Workaround for H2 #2443 */

	for (i = 0 ; i < TPC_MAX_NUM ; i++)
		_goya_tpc_mbist_workaround(hdev, i);

	goya->hw_cap_initialized |= HW_CAP_TPC_MBIST;
}

/*
 * goya_init_golden_registers - Initialize golden registers
 *
 * @hdev: pointer to hl_device structure
 *
 * Initialize the H/W registers of the device
 *
 */
static void goya_init_golden_registers(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	u32 polynom[10], tpc_intr_mask, offset;
	int i;

	if (goya->hw_cap_initialized & HW_CAP_GOLDEN)
		return;

	polynom[0] = 0x00020080;
	polynom[1] = 0x00401000;
	polynom[2] = 0x00200800;
	polynom[3] = 0x00002000;
	polynom[4] = 0x00080200;
	polynom[5] = 0x00040100;
	polynom[6] = 0x00100400;
	polynom[7] = 0x00004000;
	polynom[8] = 0x00010000;
	polynom[9] = 0x00008000;

	/* Mask all arithmetic interrupts from TPC */
	tpc_intr_mask = 0x7FFF;

	for (i = 0, offset = 0 ; i < 6 ; i++, offset += 0x20000) {
		WREG32(mmSRAM_Y0_X0_RTR_HBW_RD_RQ_L_ARB + offset, 0x302);
		WREG32(mmSRAM_Y0_X1_RTR_HBW_RD_RQ_L_ARB + offset, 0x302);
		WREG32(mmSRAM_Y0_X2_RTR_HBW_RD_RQ_L_ARB + offset, 0x302);
		WREG32(mmSRAM_Y0_X3_RTR_HBW_RD_RQ_L_ARB + offset, 0x302);
		WREG32(mmSRAM_Y0_X4_RTR_HBW_RD_RQ_L_ARB + offset, 0x302);

		WREG32(mmSRAM_Y0_X0_RTR_HBW_DATA_L_ARB + offset, 0x204);
		WREG32(mmSRAM_Y0_X1_RTR_HBW_DATA_L_ARB + offset, 0x204);
		WREG32(mmSRAM_Y0_X2_RTR_HBW_DATA_L_ARB + offset, 0x204);
		WREG32(mmSRAM_Y0_X3_RTR_HBW_DATA_L_ARB + offset, 0x204);
		WREG32(mmSRAM_Y0_X4_RTR_HBW_DATA_L_ARB + offset, 0x204);


		WREG32(mmSRAM_Y0_X0_RTR_HBW_DATA_E_ARB + offset, 0x206);
		WREG32(mmSRAM_Y0_X1_RTR_HBW_DATA_E_ARB + offset, 0x206);
		WREG32(mmSRAM_Y0_X2_RTR_HBW_DATA_E_ARB + offset, 0x206);
		WREG32(mmSRAM_Y0_X3_RTR_HBW_DATA_E_ARB + offset, 0x207);
		WREG32(mmSRAM_Y0_X4_RTR_HBW_DATA_E_ARB + offset, 0x207);

		WREG32(mmSRAM_Y0_X0_RTR_HBW_DATA_W_ARB + offset, 0x207);
		WREG32(mmSRAM_Y0_X1_RTR_HBW_DATA_W_ARB + offset, 0x207);
		WREG32(mmSRAM_Y0_X2_RTR_HBW_DATA_W_ARB + offset, 0x206);
		WREG32(mmSRAM_Y0_X3_RTR_HBW_DATA_W_ARB + offset, 0x206);
		WREG32(mmSRAM_Y0_X4_RTR_HBW_DATA_W_ARB + offset, 0x206);

		WREG32(mmSRAM_Y0_X0_RTR_HBW_WR_RS_E_ARB + offset, 0x101);
		WREG32(mmSRAM_Y0_X1_RTR_HBW_WR_RS_E_ARB + offset, 0x102);
		WREG32(mmSRAM_Y0_X2_RTR_HBW_WR_RS_E_ARB + offset, 0x103);
		WREG32(mmSRAM_Y0_X3_RTR_HBW_WR_RS_E_ARB + offset, 0x104);
		WREG32(mmSRAM_Y0_X4_RTR_HBW_WR_RS_E_ARB + offset, 0x105);

		WREG32(mmSRAM_Y0_X0_RTR_HBW_WR_RS_W_ARB + offset, 0x105);
		WREG32(mmSRAM_Y0_X1_RTR_HBW_WR_RS_W_ARB + offset, 0x104);
		WREG32(mmSRAM_Y0_X2_RTR_HBW_WR_RS_W_ARB + offset, 0x103);
		WREG32(mmSRAM_Y0_X3_RTR_HBW_WR_RS_W_ARB + offset, 0x102);
		WREG32(mmSRAM_Y0_X4_RTR_HBW_WR_RS_W_ARB + offset, 0x101);
	}

	WREG32(mmMME_STORE_MAX_CREDIT, 0x21);
	WREG32(mmMME_AGU, 0x0f0f0f10);
	WREG32(mmMME_SEI_MASK, ~0x0);

	WREG32(mmMME6_RTR_HBW_RD_RQ_N_ARB, 0x01010101);
	WREG32(mmMME5_RTR_HBW_RD_RQ_N_ARB, 0x01040101);
	WREG32(mmMME4_RTR_HBW_RD_RQ_N_ARB, 0x01030101);
	WREG32(mmMME3_RTR_HBW_RD_RQ_N_ARB, 0x01020101);
	WREG32(mmMME2_RTR_HBW_RD_RQ_N_ARB, 0x01010101);
	WREG32(mmMME1_RTR_HBW_RD_RQ_N_ARB, 0x07010701);
	WREG32(mmMME6_RTR_HBW_RD_RQ_S_ARB, 0x04010401);
	WREG32(mmMME5_RTR_HBW_RD_RQ_S_ARB, 0x04050401);
	WREG32(mmMME4_RTR_HBW_RD_RQ_S_ARB, 0x03070301);
	WREG32(mmMME3_RTR_HBW_RD_RQ_S_ARB, 0x01030101);
	WREG32(mmMME2_RTR_HBW_RD_RQ_S_ARB, 0x01040101);
	WREG32(mmMME1_RTR_HBW_RD_RQ_S_ARB, 0x01050105);
	WREG32(mmMME6_RTR_HBW_RD_RQ_W_ARB, 0x01010501);
	WREG32(mmMME5_RTR_HBW_RD_RQ_W_ARB, 0x01010501);
	WREG32(mmMME4_RTR_HBW_RD_RQ_W_ARB, 0x01040301);
	WREG32(mmMME3_RTR_HBW_RD_RQ_W_ARB, 0x01030401);
	WREG32(mmMME2_RTR_HBW_RD_RQ_W_ARB, 0x01040101);
	WREG32(mmMME1_RTR_HBW_RD_RQ_W_ARB, 0x01050101);
	WREG32(mmMME6_RTR_HBW_WR_RQ_N_ARB, 0x02020202);
	WREG32(mmMME5_RTR_HBW_WR_RQ_N_ARB, 0x01070101);
	WREG32(mmMME4_RTR_HBW_WR_RQ_N_ARB, 0x02020201);
	WREG32(mmMME3_RTR_HBW_WR_RQ_N_ARB, 0x07020701);
	WREG32(mmMME2_RTR_HBW_WR_RQ_N_ARB, 0x01020101);
	WREG32(mmMME1_RTR_HBW_WR_RQ_S_ARB, 0x01010101);
	WREG32(mmMME6_RTR_HBW_WR_RQ_S_ARB, 0x01070101);
	WREG32(mmMME5_RTR_HBW_WR_RQ_S_ARB, 0x01070101);
	WREG32(mmMME4_RTR_HBW_WR_RQ_S_ARB, 0x07020701);
	WREG32(mmMME3_RTR_HBW_WR_RQ_S_ARB, 0x02020201);
	WREG32(mmMME2_RTR_HBW_WR_RQ_S_ARB, 0x01070101);
	WREG32(mmMME1_RTR_HBW_WR_RQ_S_ARB, 0x01020102);
	WREG32(mmMME6_RTR_HBW_WR_RQ_W_ARB, 0x01020701);
	WREG32(mmMME5_RTR_HBW_WR_RQ_W_ARB, 0x01020701);
	WREG32(mmMME4_RTR_HBW_WR_RQ_W_ARB, 0x07020707);
	WREG32(mmMME3_RTR_HBW_WR_RQ_W_ARB, 0x01020201);
	WREG32(mmMME2_RTR_HBW_WR_RQ_W_ARB, 0x01070201);
	WREG32(mmMME1_RTR_HBW_WR_RQ_W_ARB, 0x01070201);
	WREG32(mmMME6_RTR_HBW_RD_RS_N_ARB, 0x01070102);
	WREG32(mmMME5_RTR_HBW_RD_RS_N_ARB, 0x01070102);
	WREG32(mmMME4_RTR_HBW_RD_RS_N_ARB, 0x01060102);
	WREG32(mmMME3_RTR_HBW_RD_RS_N_ARB, 0x01040102);
	WREG32(mmMME2_RTR_HBW_RD_RS_N_ARB, 0x01020102);
	WREG32(mmMME1_RTR_HBW_RD_RS_N_ARB, 0x01020107);
	WREG32(mmMME6_RTR_HBW_RD_RS_S_ARB, 0x01020106);
	WREG32(mmMME5_RTR_HBW_RD_RS_S_ARB, 0x01020102);
	WREG32(mmMME4_RTR_HBW_RD_RS_S_ARB, 0x01040102);
	WREG32(mmMME3_RTR_HBW_RD_RS_S_ARB, 0x01060102);
	WREG32(mmMME2_RTR_HBW_RD_RS_S_ARB, 0x01070102);
	WREG32(mmMME1_RTR_HBW_RD_RS_S_ARB, 0x01070102);
	WREG32(mmMME6_RTR_HBW_RD_RS_E_ARB, 0x01020702);
	WREG32(mmMME5_RTR_HBW_RD_RS_E_ARB, 0x01020702);
	WREG32(mmMME4_RTR_HBW_RD_RS_E_ARB, 0x01040602);
	WREG32(mmMME3_RTR_HBW_RD_RS_E_ARB, 0x01060402);
	WREG32(mmMME2_RTR_HBW_RD_RS_E_ARB, 0x01070202);
	WREG32(mmMME1_RTR_HBW_RD_RS_E_ARB, 0x01070102);
	WREG32(mmMME6_RTR_HBW_RD_RS_W_ARB, 0x01060401);
	WREG32(mmMME5_RTR_HBW_RD_RS_W_ARB, 0x01060401);
	WREG32(mmMME4_RTR_HBW_RD_RS_W_ARB, 0x01060401);
	WREG32(mmMME3_RTR_HBW_RD_RS_W_ARB, 0x01060401);
	WREG32(mmMME2_RTR_HBW_RD_RS_W_ARB, 0x01060401);
	WREG32(mmMME1_RTR_HBW_RD_RS_W_ARB, 0x01060401);
	WREG32(mmMME6_RTR_HBW_WR_RS_N_ARB, 0x01050101);
	WREG32(mmMME5_RTR_HBW_WR_RS_N_ARB, 0x01040101);
	WREG32(mmMME4_RTR_HBW_WR_RS_N_ARB, 0x01030101);
	WREG32(mmMME3_RTR_HBW_WR_RS_N_ARB, 0x01020101);
	WREG32(mmMME2_RTR_HBW_WR_RS_N_ARB, 0x01010101);
	WREG32(mmMME1_RTR_HBW_WR_RS_N_ARB, 0x01010107);
	WREG32(mmMME6_RTR_HBW_WR_RS_S_ARB, 0x01010107);
	WREG32(mmMME5_RTR_HBW_WR_RS_S_ARB, 0x01010101);
	WREG32(mmMME4_RTR_HBW_WR_RS_S_ARB, 0x01020101);
	WREG32(mmMME3_RTR_HBW_WR_RS_S_ARB, 0x01030101);
	WREG32(mmMME2_RTR_HBW_WR_RS_S_ARB, 0x01040101);
	WREG32(mmMME1_RTR_HBW_WR_RS_S_ARB, 0x01050101);
	WREG32(mmMME6_RTR_HBW_WR_RS_E_ARB, 0x01010501);
	WREG32(mmMME5_RTR_HBW_WR_RS_E_ARB, 0x01010501);
	WREG32(mmMME4_RTR_HBW_WR_RS_E_ARB, 0x01040301);
	WREG32(mmMME3_RTR_HBW_WR_RS_E_ARB, 0x01030401);
	WREG32(mmMME2_RTR_HBW_WR_RS_E_ARB, 0x01040101);
	WREG32(mmMME1_RTR_HBW_WR_RS_E_ARB, 0x01050101);
	WREG32(mmMME6_RTR_HBW_WR_RS_W_ARB, 0x01010101);
	WREG32(mmMME5_RTR_HBW_WR_RS_W_ARB, 0x01010101);
	WREG32(mmMME4_RTR_HBW_WR_RS_W_ARB, 0x01010101);
	WREG32(mmMME3_RTR_HBW_WR_RS_W_ARB, 0x01010101);
	WREG32(mmMME2_RTR_HBW_WR_RS_W_ARB, 0x01010101);
	WREG32(mmMME1_RTR_HBW_WR_RS_W_ARB, 0x01010101);

	WREG32(mmTPC1_RTR_HBW_RD_RQ_N_ARB, 0x01010101);
	WREG32(mmTPC1_RTR_HBW_RD_RQ_S_ARB, 0x01010101);
	WREG32(mmTPC1_RTR_HBW_RD_RQ_E_ARB, 0x01060101);
	WREG32(mmTPC1_RTR_HBW_WR_RQ_N_ARB, 0x02020102);
	WREG32(mmTPC1_RTR_HBW_WR_RQ_S_ARB, 0x01010101);
	WREG32(mmTPC1_RTR_HBW_WR_RQ_E_ARB, 0x02070202);
	WREG32(mmTPC1_RTR_HBW_RD_RS_N_ARB, 0x01020201);
	WREG32(mmTPC1_RTR_HBW_RD_RS_S_ARB, 0x01070201);
	WREG32(mmTPC1_RTR_HBW_RD_RS_W_ARB, 0x01070202);
	WREG32(mmTPC1_RTR_HBW_WR_RS_N_ARB, 0x01010101);
	WREG32(mmTPC1_RTR_HBW_WR_RS_S_ARB, 0x01050101);
	WREG32(mmTPC1_RTR_HBW_WR_RS_W_ARB, 0x01050101);

	WREG32(mmTPC2_RTR_HBW_RD_RQ_N_ARB, 0x01020101);
	WREG32(mmTPC2_RTR_HBW_RD_RQ_S_ARB, 0x01050101);
	WREG32(mmTPC2_RTR_HBW_RD_RQ_E_ARB, 0x01010201);
	WREG32(mmTPC2_RTR_HBW_WR_RQ_N_ARB, 0x02040102);
	WREG32(mmTPC2_RTR_HBW_WR_RQ_S_ARB, 0x01050101);
	WREG32(mmTPC2_RTR_HBW_WR_RQ_E_ARB, 0x02060202);
	WREG32(mmTPC2_RTR_HBW_RD_RS_N_ARB, 0x01020201);
	WREG32(mmTPC2_RTR_HBW_RD_RS_S_ARB, 0x01070201);
	WREG32(mmTPC2_RTR_HBW_RD_RS_W_ARB, 0x01070202);
	WREG32(mmTPC2_RTR_HBW_WR_RS_N_ARB, 0x01010101);
	WREG32(mmTPC2_RTR_HBW_WR_RS_S_ARB, 0x01040101);
	WREG32(mmTPC2_RTR_HBW_WR_RS_W_ARB, 0x01040101);

	WREG32(mmTPC3_RTR_HBW_RD_RQ_N_ARB, 0x01030101);
	WREG32(mmTPC3_RTR_HBW_RD_RQ_S_ARB, 0x01040101);
	WREG32(mmTPC3_RTR_HBW_RD_RQ_E_ARB, 0x01040301);
	WREG32(mmTPC3_RTR_HBW_WR_RQ_N_ARB, 0x02060102);
	WREG32(mmTPC3_RTR_HBW_WR_RQ_S_ARB, 0x01040101);
	WREG32(mmTPC3_RTR_HBW_WR_RQ_E_ARB, 0x01040301);
	WREG32(mmTPC3_RTR_HBW_RD_RS_N_ARB, 0x01040201);
	WREG32(mmTPC3_RTR_HBW_RD_RS_S_ARB, 0x01060201);
	WREG32(mmTPC3_RTR_HBW_RD_RS_W_ARB, 0x01060402);
	WREG32(mmTPC3_RTR_HBW_WR_RS_N_ARB, 0x01020101);
	WREG32(mmTPC3_RTR_HBW_WR_RS_S_ARB, 0x01030101);
	WREG32(mmTPC3_RTR_HBW_WR_RS_W_ARB, 0x01030401);

	WREG32(mmTPC4_RTR_HBW_RD_RQ_N_ARB, 0x01040101);
	WREG32(mmTPC4_RTR_HBW_RD_RQ_S_ARB, 0x01030101);
	WREG32(mmTPC4_RTR_HBW_RD_RQ_E_ARB, 0x01030401);
	WREG32(mmTPC4_RTR_HBW_WR_RQ_N_ARB, 0x02070102);
	WREG32(mmTPC4_RTR_HBW_WR_RQ_S_ARB, 0x01030101);
	WREG32(mmTPC4_RTR_HBW_WR_RQ_E_ARB, 0x02060702);
	WREG32(mmTPC4_RTR_HBW_RD_RS_N_ARB, 0x01060201);
	WREG32(mmTPC4_RTR_HBW_RD_RS_S_ARB, 0x01040201);
	WREG32(mmTPC4_RTR_HBW_RD_RS_W_ARB, 0x01040602);
	WREG32(mmTPC4_RTR_HBW_WR_RS_N_ARB, 0x01030101);
	WREG32(mmTPC4_RTR_HBW_WR_RS_S_ARB, 0x01020101);
	WREG32(mmTPC4_RTR_HBW_WR_RS_W_ARB, 0x01040301);

	WREG32(mmTPC5_RTR_HBW_RD_RQ_N_ARB, 0x01050101);
	WREG32(mmTPC5_RTR_HBW_RD_RQ_S_ARB, 0x01020101);
	WREG32(mmTPC5_RTR_HBW_RD_RQ_E_ARB, 0x01200501);
	WREG32(mmTPC5_RTR_HBW_WR_RQ_N_ARB, 0x02070102);
	WREG32(mmTPC5_RTR_HBW_WR_RQ_S_ARB, 0x01020101);
	WREG32(mmTPC5_RTR_HBW_WR_RQ_E_ARB, 0x02020602);
	WREG32(mmTPC5_RTR_HBW_RD_RS_N_ARB, 0x01070201);
	WREG32(mmTPC5_RTR_HBW_RD_RS_S_ARB, 0x01020201);
	WREG32(mmTPC5_RTR_HBW_RD_RS_W_ARB, 0x01020702);
	WREG32(mmTPC5_RTR_HBW_WR_RS_N_ARB, 0x01040101);
	WREG32(mmTPC5_RTR_HBW_WR_RS_S_ARB, 0x01010101);
	WREG32(mmTPC5_RTR_HBW_WR_RS_W_ARB, 0x01010501);

	WREG32(mmTPC6_RTR_HBW_RD_RQ_N_ARB, 0x01010101);
	WREG32(mmTPC6_RTR_HBW_RD_RQ_S_ARB, 0x01010101);
	WREG32(mmTPC6_RTR_HBW_RD_RQ_E_ARB, 0x01010601);
	WREG32(mmTPC6_RTR_HBW_WR_RQ_N_ARB, 0x01010101);
	WREG32(mmTPC6_RTR_HBW_WR_RQ_S_ARB, 0x01010101);
	WREG32(mmTPC6_RTR_HBW_WR_RQ_E_ARB, 0x02020702);
	WREG32(mmTPC6_RTR_HBW_RD_RS_N_ARB, 0x01010101);
	WREG32(mmTPC6_RTR_HBW_RD_RS_S_ARB, 0x01010101);
	WREG32(mmTPC6_RTR_HBW_RD_RS_W_ARB, 0x01020702);
	WREG32(mmTPC6_RTR_HBW_WR_RS_N_ARB, 0x01050101);
	WREG32(mmTPC6_RTR_HBW_WR_RS_S_ARB, 0x01010101);
	WREG32(mmTPC6_RTR_HBW_WR_RS_W_ARB, 0x01010501);

	for (i = 0, offset = 0 ; i < 10 ; i++, offset += 4) {
		WREG32(mmMME1_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmMME2_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmMME3_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmMME4_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmMME5_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmMME6_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);

		WREG32(mmTPC0_NRTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmTPC1_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmTPC2_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmTPC3_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmTPC4_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmTPC5_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmTPC6_RTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmTPC7_NRTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);

		WREG32(mmPCI_NRTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
		WREG32(mmDMA_NRTR_SPLIT_COEF_0 + offset, polynom[i] >> 7);
	}

	for (i = 0, offset = 0 ; i < 6 ; i++, offset += 0x40000) {
		WREG32(mmMME1_RTR_SCRAMB_EN + offset,
				1 << MME1_RTR_SCRAMB_EN_VAL_SHIFT);
		WREG32(mmMME1_RTR_NON_LIN_SCRAMB + offset,
				1 << MME1_RTR_NON_LIN_SCRAMB_EN_SHIFT);
	}

	for (i = 0, offset = 0 ; i < 8 ; i++, offset += 0x40000) {
		/*
		 * Workaround for Bug H2 #2441 :
		 * "ST.NOP set trace event illegal opcode"
		 */
		WREG32(mmTPC0_CFG_TPC_INTR_MASK + offset, tpc_intr_mask);

		WREG32(mmTPC0_NRTR_SCRAMB_EN + offset,
				1 << TPC0_NRTR_SCRAMB_EN_VAL_SHIFT);
		WREG32(mmTPC0_NRTR_NON_LIN_SCRAMB + offset,
				1 << TPC0_NRTR_NON_LIN_SCRAMB_EN_SHIFT);
	}

	WREG32(mmDMA_NRTR_SCRAMB_EN, 1 << DMA_NRTR_SCRAMB_EN_VAL_SHIFT);
	WREG32(mmDMA_NRTR_NON_LIN_SCRAMB,
			1 << DMA_NRTR_NON_LIN_SCRAMB_EN_SHIFT);

	WREG32(mmPCI_NRTR_SCRAMB_EN, 1 << PCI_NRTR_SCRAMB_EN_VAL_SHIFT);
	WREG32(mmPCI_NRTR_NON_LIN_SCRAMB,
			1 << PCI_NRTR_NON_LIN_SCRAMB_EN_SHIFT);

	/*
	 * Workaround for H2 #HW-23 bug
	 * Set DMA max outstanding read requests to 240 on DMA CH 1. Set it
	 * to 16 on KMD DMA
	 * We need to limit only these DMAs because the user can only read
	 * from Host using DMA CH 1
	 */
	WREG32(mmDMA_CH_0_CFG0, 0x0fff0010);
	WREG32(mmDMA_CH_1_CFG0, 0x0fff00F0);

	goya->hw_cap_initialized |= HW_CAP_GOLDEN;
}


/*
 * goya_push_fw_to_device - Push FW code to device
 *
 * @hdev: pointer to hl_device structure
 *
 * Copy fw code from firmware file to device memory.
 * Returns 0 on success
 *
 */
static int goya_push_fw_to_device(struct hl_device *hdev, const char *fw_name,
					void __iomem *dst)
{
	const struct firmware *fw;
	const u64 *fw_data;
	size_t fw_size, i;
	int rc;

	rc = request_firmware(&fw, fw_name, hdev->dev);

	if (rc) {
		dev_err(hdev->dev, "Failed to request %s\n", fw_name);
		goto out;
	}

	fw_size = fw->size;
	if ((fw_size % 4) != 0) {
		dev_err(hdev->dev, "illegal %s firmware size %zu\n",
			fw_name, fw_size);
		rc = -EINVAL;
		goto out;
	}

	dev_dbg(hdev->dev, "%s firmware size == %zu\n", fw_name, fw_size);

	fw_data = (const u64 *) fw->data;

	if ((fw->size % 8) != 0)
		fw_size -= 8;

	for (i = 0 ; i < fw_size ; i += 8, fw_data++, dst += 8) {
		if (!(i & (0x80000 - 1))) {
			dev_dbg(hdev->dev,
				"copied so far %zu out of %zu for %s firmware",
				i, fw_size, fw_name);
			usleep_range(20, 100);
		}

		writeq(*fw_data, dst);
	}

	if ((fw->size % 8) != 0)
		writel(*(const u32 *) fw_data, dst);

out:
	release_firmware(fw);
	return rc;
}

static int goya_pldm_init_cpu(struct hl_device *hdev)
{
	char fw_name[200];
	void __iomem *dst;
	u32 val, unit_rst_val;
	int rc;

	/* Must initialize SRAM scrambler before pushing u-boot to SRAM */
	goya_init_golden_registers(hdev);

	/* Put ARM cores into reset */
	WREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL, CPU_RESET_ASSERT);
	val = RREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL);

	/* Reset the CA53 MACRO */
	unit_rst_val = RREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N);
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N, CA53_RESET);
	val = RREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N);
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N, unit_rst_val);
	val = RREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N);

	snprintf(fw_name, sizeof(fw_name), "habanalabs/goya/goya-u-boot.bin");
	dst = hdev->pcie_bar[SRAM_CFG_BAR_ID] + UBOOT_FW_OFFSET;
	rc = goya_push_fw_to_device(hdev, fw_name, dst);
	if (rc)
		return rc;

	snprintf(fw_name, sizeof(fw_name), "habanalabs/goya/goya-fit.itb");
	dst = hdev->pcie_bar[DDR_BAR_ID] + LINUX_FW_OFFSET;
	rc = goya_push_fw_to_device(hdev, fw_name, dst);
	if (rc)
		return rc;

	WREG32(mmPSOC_GLOBAL_CONF_UBOOT_MAGIC, KMD_MSG_FIT_RDY);
	WREG32(mmPSOC_GLOBAL_CONF_WARM_REBOOT, CPU_BOOT_STATUS_NA);

	WREG32(mmCPU_CA53_CFG_RST_ADDR_LSB_0,
		lower_32_bits(SRAM_BASE_ADDR + UBOOT_FW_OFFSET));
	WREG32(mmCPU_CA53_CFG_RST_ADDR_MSB_0,
		upper_32_bits(SRAM_BASE_ADDR + UBOOT_FW_OFFSET));

	/* Release ARM core 0 from reset */
	WREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL,
					CPU_RESET_CORE0_DEASSERT);
	val = RREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL);

	return 0;
}

/*
 * FW component passes an offset from SRAM_BASE_ADDR in SCRATCHPAD_xx.
 * The version string should be located by that offset.
 */
static void goya_read_device_fw_version(struct hl_device *hdev,
					enum goya_fw_component fwc)
{
	const char *name;
	u32 ver_off;
	char *dest;

	switch (fwc) {
	case FW_COMP_UBOOT:
		ver_off = RREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_29);
		dest = hdev->asic_prop.uboot_ver;
		name = "U-Boot";
		break;
	case FW_COMP_PREBOOT:
		ver_off = RREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_28);
		dest = hdev->asic_prop.preboot_ver;
		name = "Preboot";
		break;
	default:
		dev_warn(hdev->dev, "Undefined FW component: %d\n", fwc);
		return;
	}

	ver_off &= ~((u32)SRAM_BASE_ADDR);

	if (ver_off < SRAM_SIZE - VERSION_MAX_LEN) {
		memcpy_fromio(dest, hdev->pcie_bar[SRAM_CFG_BAR_ID] + ver_off,
							VERSION_MAX_LEN);
	} else {
		dev_err(hdev->dev, "%s version offset (0x%x) is above SRAM\n",
								name, ver_off);
		strcpy(dest, "unavailable");
	}
}

static int goya_init_cpu(struct hl_device *hdev, u32 cpu_timeout)
{
	struct goya_device *goya = hdev->asic_specific;
	char fw_name[200];
	void __iomem *dst;
	u32 status;
	int rc;

	if (!hdev->cpu_enable)
		return 0;

	if (goya->hw_cap_initialized & HW_CAP_CPU)
		return 0;

	/*
	 * Before pushing u-boot/linux to device, need to set the ddr bar to
	 * base address of dram
	 */
	rc = goya_set_ddr_bar_base(hdev, DRAM_PHYS_BASE);
	if (rc) {
		dev_err(hdev->dev,
			"failed to map DDR bar to DRAM base address\n");
		return rc;
	}

	if (hdev->pldm) {
		rc = goya_pldm_init_cpu(hdev);
		if (rc)
			return rc;

		goto out;
	}

	/* Make sure CPU boot-loader is running */
	rc = hl_poll_timeout(
		hdev,
		mmPSOC_GLOBAL_CONF_WARM_REBOOT,
		status,
		(status == CPU_BOOT_STATUS_DRAM_RDY) ||
		(status == CPU_BOOT_STATUS_SRAM_AVAIL),
		10000,
		cpu_timeout);

	if (rc) {
		dev_err(hdev->dev, "Error in ARM u-boot!");
		switch (status) {
		case CPU_BOOT_STATUS_NA:
			dev_err(hdev->dev,
				"ARM status %d - BTL did NOT run\n", status);
			break;
		case CPU_BOOT_STATUS_IN_WFE:
			dev_err(hdev->dev,
				"ARM status %d - Inside WFE loop\n", status);
			break;
		case CPU_BOOT_STATUS_IN_BTL:
			dev_err(hdev->dev,
				"ARM status %d - Stuck in BTL\n", status);
			break;
		case CPU_BOOT_STATUS_IN_PREBOOT:
			dev_err(hdev->dev,
				"ARM status %d - Stuck in Preboot\n", status);
			break;
		case CPU_BOOT_STATUS_IN_SPL:
			dev_err(hdev->dev,
				"ARM status %d - Stuck in SPL\n", status);
			break;
		case CPU_BOOT_STATUS_IN_UBOOT:
			dev_err(hdev->dev,
				"ARM status %d - Stuck in u-boot\n", status);
			break;
		case CPU_BOOT_STATUS_DRAM_INIT_FAIL:
			dev_err(hdev->dev,
				"ARM status %d - DDR initialization failed\n",
				status);
			break;
		default:
			dev_err(hdev->dev,
				"ARM status %d - Invalid status code\n",
				status);
			break;
		}
		return -EIO;
	}

	/* Read U-Boot version now in case we will later fail */
	goya_read_device_fw_version(hdev, FW_COMP_UBOOT);
	goya_read_device_fw_version(hdev, FW_COMP_PREBOOT);

	if (status == CPU_BOOT_STATUS_SRAM_AVAIL)
		goto out;

	if (!hdev->fw_loading) {
		dev_info(hdev->dev, "Skip loading FW\n");
		goto out;
	}

	snprintf(fw_name, sizeof(fw_name), "habanalabs/goya/goya-fit.itb");
	dst = hdev->pcie_bar[DDR_BAR_ID] + LINUX_FW_OFFSET;
	rc = goya_push_fw_to_device(hdev, fw_name, dst);
	if (rc)
		return rc;

	WREG32(mmPSOC_GLOBAL_CONF_UBOOT_MAGIC, KMD_MSG_FIT_RDY);

	rc = hl_poll_timeout(
		hdev,
		mmPSOC_GLOBAL_CONF_WARM_REBOOT,
		status,
		(status == CPU_BOOT_STATUS_SRAM_AVAIL),
		10000,
		cpu_timeout);

	if (rc) {
		if (status == CPU_BOOT_STATUS_FIT_CORRUPTED)
			dev_err(hdev->dev,
				"ARM u-boot reports FIT image is corrupted\n");
		else
			dev_err(hdev->dev,
				"ARM Linux failed to load, %d\n", status);
		WREG32(mmPSOC_GLOBAL_CONF_UBOOT_MAGIC, KMD_MSG_NA);
		return -EIO;
	}

	dev_info(hdev->dev, "Successfully loaded firmware to device\n");

out:
	goya->hw_cap_initialized |= HW_CAP_CPU;

	return 0;
}

/*
 * goya_hw_init - Goya hardware initialization code
 *
 * @hdev: pointer to hl_device structure
 *
 * Returns 0 on success
 *
 */
static int goya_hw_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 val;
	int rc;

	dev_info(hdev->dev, "Starting initialization of H/W\n");

	/* Perform read from the device to make sure device is up */
	val = RREG32(mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG);

	rc = goya_init_cpu(hdev, GOYA_CPU_TIMEOUT_USEC);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU\n");
		return rc;
	}

	goya_tpc_mbist_workaround(hdev);

	goya_init_golden_registers(hdev);

	/*
	 * After CPU initialization is finished, change DDR bar mapping inside
	 * iATU to point to the start address of the MMU page tables
	 */
	rc = goya_set_ddr_bar_base(hdev, DRAM_PHYS_BASE +
		(MMU_PAGE_TABLES_ADDR & ~(prop->dram_pci_bar_size - 0x1ull)));
	if (rc) {
		dev_err(hdev->dev,
			"failed to map DDR bar to MMU page tables\n");
		return rc;
	}

	goya_init_security(hdev);

	/* CPU initialization is finished, we can now move to 48 bit DMA mask */
	rc = pci_set_dma_mask(hdev->pdev, DMA_BIT_MASK(48));
	if (rc) {
		dev_warn(hdev->dev, "Unable to set pci dma mask to 48 bits\n");
		rc = pci_set_dma_mask(hdev->pdev, DMA_BIT_MASK(32));
		if (rc) {
			dev_err(hdev->dev,
				"Unable to set pci dma mask to 32 bits\n");
			return rc;
		}
	}

	rc = pci_set_consistent_dma_mask(hdev->pdev, DMA_BIT_MASK(48));
	if (rc) {
		dev_warn(hdev->dev,
			"Unable to set pci consistent dma mask to 48 bits\n");
		rc = pci_set_consistent_dma_mask(hdev->pdev, DMA_BIT_MASK(32));
		if (rc) {
			dev_err(hdev->dev,
				"Unable to set pci consistent dma mask to 32 bits\n");
			return rc;
		}
	}

	/* Perform read from the device to flush all MSI-X configuration */
	val = RREG32(mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG);

	return 0;
}

/*
 * goya_hw_fini - Goya hardware tear-down code
 *
 * @hdev: pointer to hl_device structure
 * @hard_reset: should we do hard reset to all engines or just reset the
 *              compute/dma engines
 */
static void goya_hw_fini(struct hl_device *hdev, bool hard_reset)
{
	struct goya_device *goya = hdev->asic_specific;
	u32 reset_timeout_ms, status;

	if (hdev->pldm)
		reset_timeout_ms = GOYA_PLDM_RESET_TIMEOUT_MSEC;
	else
		reset_timeout_ms = GOYA_RESET_TIMEOUT_MSEC;

	if (hard_reset) {
		goya_set_ddr_bar_base(hdev, DRAM_PHYS_BASE);
		goya_disable_clk_rlx(hdev);
		goya_set_pll_refclk(hdev);

		WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST_CFG, RESET_ALL);
		dev_info(hdev->dev,
			"Issued HARD reset command, going to wait %dms\n",
			reset_timeout_ms);
	} else {
		WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST_CFG, DMA_MME_TPC_RESET);
		dev_info(hdev->dev,
			"Issued SOFT reset command, going to wait %dms\n",
			reset_timeout_ms);
	}

	/*
	 * After hard reset, we can't poll the BTM_FSM register because the PSOC
	 * itself is in reset. In either reset we need to wait until the reset
	 * is deasserted
	 */
	msleep(reset_timeout_ms);

	status = RREG32(mmPSOC_GLOBAL_CONF_BTM_FSM);
	if (status & PSOC_GLOBAL_CONF_BTM_FSM_STATE_MASK)
		dev_err(hdev->dev,
			"Timeout while waiting for device to reset 0x%x\n",
			status);

	/* Chicken bit to re-initiate boot sequencer flow */
	WREG32(mmPSOC_GLOBAL_CONF_BOOT_SEQ_RE_START,
		1 << PSOC_GLOBAL_CONF_BOOT_SEQ_RE_START_IND_SHIFT);
	/* Move boot manager FSM to pre boot sequencer init state */
	WREG32(mmPSOC_GLOBAL_CONF_SW_BTM_FSM,
			0xA << PSOC_GLOBAL_CONF_SW_BTM_FSM_CTRL_SHIFT);

	goya->hw_cap_initialized &= ~(HW_CAP_CPU | HW_CAP_CPU_Q |
					HW_CAP_DDR_0 | HW_CAP_DDR_1 |
					HW_CAP_DMA | HW_CAP_MME |
					HW_CAP_MMU | HW_CAP_TPC_MBIST |
					HW_CAP_GOLDEN | HW_CAP_TPC);

	if (!hdev->pldm) {
		int rc;
		/* In case we are running inside VM and the VM is
		 * shutting down, we need to make sure CPU boot-loader
		 * is running before we can continue the VM shutdown.
		 * That is because the VM will send an FLR signal that
		 * we must answer
		 */
		dev_info(hdev->dev,
			"Going to wait up to %ds for CPU boot loader\n",
			GOYA_CPU_TIMEOUT_USEC / 1000 / 1000);

		rc = hl_poll_timeout(
			hdev,
			mmPSOC_GLOBAL_CONF_WARM_REBOOT,
			status,
			(status == CPU_BOOT_STATUS_DRAM_RDY),
			10000,
			GOYA_CPU_TIMEOUT_USEC);
		if (rc)
			dev_err(hdev->dev,
				"failed to wait for CPU boot loader\n");
	}
}

int goya_suspend(struct hl_device *hdev)
{
	return 0;
}

int goya_resume(struct hl_device *hdev)
{
	return 0;
}

int goya_mmap(struct hl_fpriv *hpriv, struct vm_area_struct *vma)
{
	return -EINVAL;
}

int goya_cb_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
		u64 kaddress, phys_addr_t paddress, u32 size)
{
	int rc;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP |
			VM_DONTCOPY | VM_NORESERVE;

	rc = remap_pfn_range(vma, vma->vm_start, paddress >> PAGE_SHIFT,
				size, vma->vm_page_prot);
	if (rc)
		dev_err(hdev->dev, "remap_pfn_range error %d", rc);

	return rc;
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
	.hw_init = goya_hw_init,
	.hw_fini = goya_hw_fini,
	.suspend = goya_suspend,
	.resume = goya_resume,
	.mmap = goya_mmap,
	.cb_mmap = goya_cb_mmap,
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
