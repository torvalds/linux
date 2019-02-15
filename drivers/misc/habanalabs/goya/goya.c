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
	int i;

	for (i = 0 ; i < NUMBER_OF_EXT_HW_QUEUES ; i++) {
		prop->hw_queues_props[i].type = QUEUE_TYPE_EXT;
		prop->hw_queues_props[i].kmd_only = 0;
	}

	for (; i < NUMBER_OF_EXT_HW_QUEUES + NUMBER_OF_CPU_HW_QUEUES ; i++) {
		prop->hw_queues_props[i].type = QUEUE_TYPE_CPU;
		prop->hw_queues_props[i].kmd_only = 1;
	}

	for (; i < NUMBER_OF_EXT_HW_QUEUES + NUMBER_OF_CPU_HW_QUEUES +
			NUMBER_OF_INT_HW_QUEUES; i++) {
		prop->hw_queues_props[i].type = QUEUE_TYPE_INT;
		prop->hw_queues_props[i].kmd_only = 0;
	}

	for (; i < HL_MAX_QUEUES; i++)
		prop->hw_queues_props[i].type = QUEUE_TYPE_NA;

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

int goya_send_pci_access_msg(struct hl_device *hdev, u32 opcode)
{
	struct armcp_packet pkt;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = opcode << ARMCP_PKT_CTL_OPCODE_SHIFT;

	return hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt,
			sizeof(pkt), HL_DEVICE_TIMEOUT_USEC, NULL);
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

	goya->test_cpu_queue = goya_test_cpu_queue;

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

static void goya_init_dma_qman(struct hl_device *hdev, int dma_id,
		dma_addr_t bus_address)
{
	struct goya_device *goya = hdev->asic_specific;
	u32 mtr_base_lo, mtr_base_hi;
	u32 so_base_lo, so_base_hi;
	u32 gic_base_lo, gic_base_hi;
	u32 reg_off = dma_id * (mmDMA_QM_1_PQ_PI - mmDMA_QM_0_PQ_PI);

	mtr_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	mtr_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	so_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);

	gic_base_lo =
		lower_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);
	gic_base_hi =
		upper_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);

	WREG32(mmDMA_QM_0_PQ_BASE_LO + reg_off, lower_32_bits(bus_address));
	WREG32(mmDMA_QM_0_PQ_BASE_HI + reg_off, upper_32_bits(bus_address));

	WREG32(mmDMA_QM_0_PQ_SIZE + reg_off, ilog2(HL_QUEUE_LENGTH));
	WREG32(mmDMA_QM_0_PQ_PI + reg_off, 0);
	WREG32(mmDMA_QM_0_PQ_CI + reg_off, 0);

	WREG32(mmDMA_QM_0_CP_MSG_BASE0_ADDR_LO + reg_off, mtr_base_lo);
	WREG32(mmDMA_QM_0_CP_MSG_BASE0_ADDR_HI + reg_off, mtr_base_hi);
	WREG32(mmDMA_QM_0_CP_MSG_BASE1_ADDR_LO + reg_off, so_base_lo);
	WREG32(mmDMA_QM_0_CP_MSG_BASE1_ADDR_HI + reg_off, so_base_hi);
	WREG32(mmDMA_QM_0_GLBL_ERR_ADDR_LO + reg_off, gic_base_lo);
	WREG32(mmDMA_QM_0_GLBL_ERR_ADDR_HI + reg_off, gic_base_hi);
	WREG32(mmDMA_QM_0_GLBL_ERR_WDATA + reg_off,
			GOYA_ASYNC_EVENT_ID_DMA0_QM + dma_id);

	/* PQ has buffer of 2 cache lines, while CQ has 8 lines */
	WREG32(mmDMA_QM_0_PQ_CFG1 + reg_off, 0x00020002);
	WREG32(mmDMA_QM_0_CQ_CFG1 + reg_off, 0x00080008);

	if (dma_id == 0)
		WREG32(mmDMA_QM_0_GLBL_PROT + reg_off, QMAN_DMA_FULLY_TRUSTED);
	else
		if (goya->hw_cap_initialized & HW_CAP_MMU)
			WREG32(mmDMA_QM_0_GLBL_PROT + reg_off,
					QMAN_DMA_PARTLY_TRUSTED);
		else
			WREG32(mmDMA_QM_0_GLBL_PROT + reg_off,
					QMAN_DMA_FULLY_TRUSTED);

	WREG32(mmDMA_QM_0_GLBL_ERR_CFG + reg_off, QMAN_DMA_ERR_MSG_EN);
	WREG32(mmDMA_QM_0_GLBL_CFG0 + reg_off, QMAN_DMA_ENABLE);
}

static void goya_init_dma_ch(struct hl_device *hdev, int dma_id)
{
	u32 gic_base_lo, gic_base_hi;
	u64 sob_addr;
	u32 reg_off = dma_id * (mmDMA_CH_1_CFG1 - mmDMA_CH_0_CFG1);

	gic_base_lo =
		lower_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);
	gic_base_hi =
		upper_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);

	WREG32(mmDMA_CH_0_ERRMSG_ADDR_LO + reg_off, gic_base_lo);
	WREG32(mmDMA_CH_0_ERRMSG_ADDR_HI + reg_off, gic_base_hi);
	WREG32(mmDMA_CH_0_ERRMSG_WDATA + reg_off,
			GOYA_ASYNC_EVENT_ID_DMA0_CH + dma_id);

	if (dma_id) {
		sob_addr = CFG_BASE + mmSYNC_MNGR_SOB_OBJ_1000 +
				(dma_id - 1) * 4;
		WREG32(mmDMA_CH_0_WR_COMP_ADDR_LO + reg_off,
				lower_32_bits(sob_addr));
		WREG32(mmDMA_CH_0_WR_COMP_ADDR_HI + reg_off,
				upper_32_bits(sob_addr));
		WREG32(mmDMA_CH_0_WR_COMP_WDATA + reg_off, 0x80000001);
	}
}

/*
 * goya_init_dma_qmans - Initialize QMAN DMA registers
 *
 * @hdev: pointer to hl_device structure
 *
 * Initialize the H/W registers of the QMAN DMA channels
 *
 */
static void goya_init_dma_qmans(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	struct hl_hw_queue *q;
	dma_addr_t bus_address;
	int i;

	if (goya->hw_cap_initialized & HW_CAP_DMA)
		return;

	q = &hdev->kernel_queues[0];

	for (i = 0 ; i < NUMBER_OF_EXT_HW_QUEUES ; i++, q++) {
		bus_address = q->bus_address +
				hdev->asic_prop.host_phys_base_address;

		goya_init_dma_qman(hdev, i, bus_address);
		goya_init_dma_ch(hdev, i);
	}

	goya->hw_cap_initialized |= HW_CAP_DMA;
}

/*
 * goya_disable_external_queues - Disable external queues
 *
 * @hdev: pointer to hl_device structure
 *
 */
static void goya_disable_external_queues(struct hl_device *hdev)
{
	WREG32(mmDMA_QM_0_GLBL_CFG0, 0);
	WREG32(mmDMA_QM_1_GLBL_CFG0, 0);
	WREG32(mmDMA_QM_2_GLBL_CFG0, 0);
	WREG32(mmDMA_QM_3_GLBL_CFG0, 0);
	WREG32(mmDMA_QM_4_GLBL_CFG0, 0);
}

static int goya_stop_queue(struct hl_device *hdev, u32 cfg_reg,
				u32 cp_sts_reg, u32 glbl_sts0_reg)
{
	int rc;
	u32 status;

	/* use the values of TPC0 as they are all the same*/

	WREG32(cfg_reg, 1 << TPC0_QM_GLBL_CFG1_CP_STOP_SHIFT);

	status = RREG32(cp_sts_reg);
	if (status & TPC0_QM_CP_STS_FENCE_IN_PROGRESS_MASK) {
		rc = hl_poll_timeout(
			hdev,
			cp_sts_reg,
			status,
			!(status & TPC0_QM_CP_STS_FENCE_IN_PROGRESS_MASK),
			1000,
			QMAN_FENCE_TIMEOUT_USEC);

		/* if QMAN is stuck in fence no need to check for stop */
		if (rc)
			return 0;
	}

	rc = hl_poll_timeout(
		hdev,
		glbl_sts0_reg,
		status,
		(status & TPC0_QM_GLBL_STS0_CP_IS_STOP_MASK),
		1000,
		QMAN_STOP_TIMEOUT_USEC);

	if (rc) {
		dev_err(hdev->dev,
			"Timeout while waiting for QMAN to stop\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * goya_stop_external_queues - Stop external queues
 *
 * @hdev: pointer to hl_device structure
 *
 * Returns 0 on success
 *
 */
static int goya_stop_external_queues(struct hl_device *hdev)
{
	int rc, retval = 0;

	rc = goya_stop_queue(hdev,
			mmDMA_QM_0_GLBL_CFG1,
			mmDMA_QM_0_CP_STS,
			mmDMA_QM_0_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop DMA QMAN 0\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmDMA_QM_1_GLBL_CFG1,
			mmDMA_QM_1_CP_STS,
			mmDMA_QM_1_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop DMA QMAN 1\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmDMA_QM_2_GLBL_CFG1,
			mmDMA_QM_2_CP_STS,
			mmDMA_QM_2_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop DMA QMAN 2\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmDMA_QM_3_GLBL_CFG1,
			mmDMA_QM_3_CP_STS,
			mmDMA_QM_3_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop DMA QMAN 3\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmDMA_QM_4_GLBL_CFG1,
			mmDMA_QM_4_CP_STS,
			mmDMA_QM_4_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop DMA QMAN 4\n");
		retval = -EIO;
	}

	return retval;
}

static void goya_resume_external_queues(struct hl_device *hdev)
{
	WREG32(mmDMA_QM_0_GLBL_CFG1, 0);
	WREG32(mmDMA_QM_1_GLBL_CFG1, 0);
	WREG32(mmDMA_QM_2_GLBL_CFG1, 0);
	WREG32(mmDMA_QM_3_GLBL_CFG1, 0);
	WREG32(mmDMA_QM_4_GLBL_CFG1, 0);
}

/*
 * goya_init_cpu_queues - Initialize PQ/CQ/EQ of CPU
 *
 * @hdev: pointer to hl_device structure
 *
 * Returns 0 on success
 *
 */
int goya_init_cpu_queues(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	dma_addr_t bus_address;
	u32 status;
	struct hl_hw_queue *cpu_pq = &hdev->kernel_queues[GOYA_QUEUE_ID_CPU_PQ];
	int err;

	if (!hdev->cpu_queues_enable)
		return 0;

	if (goya->hw_cap_initialized & HW_CAP_CPU_Q)
		return 0;

	bus_address = cpu_pq->bus_address +
			hdev->asic_prop.host_phys_base_address;
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_0, lower_32_bits(bus_address));
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_1, upper_32_bits(bus_address));

	bus_address = hdev->cpu_accessible_dma_address +
			hdev->asic_prop.host_phys_base_address;
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_8, lower_32_bits(bus_address));
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_9, upper_32_bits(bus_address));

	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_5, HL_QUEUE_SIZE_IN_BYTES);
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_10, CPU_ACCESSIBLE_MEM_SIZE);

	/* Used for EQ CI */
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_6, 0);

	WREG32(mmCPU_IF_PF_PQ_PI, 0);

	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_7, PQ_INIT_STATUS_READY_FOR_CP);

	WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR,
			GOYA_ASYNC_EVENT_ID_PI_UPDATE);

	err = hl_poll_timeout(
		hdev,
		mmPSOC_GLOBAL_CONF_SCRATCHPAD_7,
		status,
		(status == PQ_INIT_STATUS_READY_FOR_HOST),
		1000,
		GOYA_CPU_TIMEOUT_USEC);

	if (err) {
		dev_err(hdev->dev,
			"Failed to communicate with ARM CPU (ArmCP timeout)\n");
		return -EIO;
	}

	goya->hw_cap_initialized |= HW_CAP_CPU_Q;
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

static void goya_init_mme_qman(struct hl_device *hdev)
{
	u32 mtr_base_lo, mtr_base_hi;
	u32 so_base_lo, so_base_hi;
	u32 gic_base_lo, gic_base_hi;
	u64 qman_base_addr;

	mtr_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	mtr_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	so_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);

	gic_base_lo =
		lower_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);
	gic_base_hi =
		upper_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);

	qman_base_addr = hdev->asic_prop.sram_base_address +
				MME_QMAN_BASE_OFFSET;

	WREG32(mmMME_QM_PQ_BASE_LO, lower_32_bits(qman_base_addr));
	WREG32(mmMME_QM_PQ_BASE_HI, upper_32_bits(qman_base_addr));
	WREG32(mmMME_QM_PQ_SIZE, ilog2(MME_QMAN_LENGTH));
	WREG32(mmMME_QM_PQ_PI, 0);
	WREG32(mmMME_QM_PQ_CI, 0);
	WREG32(mmMME_QM_CP_LDMA_SRC_BASE_LO_OFFSET, 0x10C0);
	WREG32(mmMME_QM_CP_LDMA_SRC_BASE_HI_OFFSET, 0x10C4);
	WREG32(mmMME_QM_CP_LDMA_TSIZE_OFFSET, 0x10C8);
	WREG32(mmMME_QM_CP_LDMA_COMMIT_OFFSET, 0x10CC);

	WREG32(mmMME_QM_CP_MSG_BASE0_ADDR_LO, mtr_base_lo);
	WREG32(mmMME_QM_CP_MSG_BASE0_ADDR_HI, mtr_base_hi);
	WREG32(mmMME_QM_CP_MSG_BASE1_ADDR_LO, so_base_lo);
	WREG32(mmMME_QM_CP_MSG_BASE1_ADDR_HI, so_base_hi);

	/* QMAN CQ has 8 cache lines */
	WREG32(mmMME_QM_CQ_CFG1, 0x00080008);

	WREG32(mmMME_QM_GLBL_ERR_ADDR_LO, gic_base_lo);
	WREG32(mmMME_QM_GLBL_ERR_ADDR_HI, gic_base_hi);

	WREG32(mmMME_QM_GLBL_ERR_WDATA, GOYA_ASYNC_EVENT_ID_MME_QM);

	WREG32(mmMME_QM_GLBL_ERR_CFG, QMAN_MME_ERR_MSG_EN);

	WREG32(mmMME_QM_GLBL_PROT, QMAN_MME_ERR_PROT);

	WREG32(mmMME_QM_GLBL_CFG0, QMAN_MME_ENABLE);
}

static void goya_init_mme_cmdq(struct hl_device *hdev)
{
	u32 mtr_base_lo, mtr_base_hi;
	u32 so_base_lo, so_base_hi;
	u32 gic_base_lo, gic_base_hi;
	u64 qman_base_addr;

	mtr_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	mtr_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	so_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);

	gic_base_lo =
		lower_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);
	gic_base_hi =
		upper_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);

	qman_base_addr = hdev->asic_prop.sram_base_address +
				MME_QMAN_BASE_OFFSET;

	WREG32(mmMME_CMDQ_CP_MSG_BASE0_ADDR_LO, mtr_base_lo);
	WREG32(mmMME_CMDQ_CP_MSG_BASE0_ADDR_HI, mtr_base_hi);
	WREG32(mmMME_CMDQ_CP_MSG_BASE1_ADDR_LO,	so_base_lo);
	WREG32(mmMME_CMDQ_CP_MSG_BASE1_ADDR_HI, so_base_hi);

	/* CMDQ CQ has 20 cache lines */
	WREG32(mmMME_CMDQ_CQ_CFG1, 0x00140014);

	WREG32(mmMME_CMDQ_GLBL_ERR_ADDR_LO, gic_base_lo);
	WREG32(mmMME_CMDQ_GLBL_ERR_ADDR_HI, gic_base_hi);

	WREG32(mmMME_CMDQ_GLBL_ERR_WDATA, GOYA_ASYNC_EVENT_ID_MME_CMDQ);

	WREG32(mmMME_CMDQ_GLBL_ERR_CFG, CMDQ_MME_ERR_MSG_EN);

	WREG32(mmMME_CMDQ_GLBL_PROT, CMDQ_MME_ERR_PROT);

	WREG32(mmMME_CMDQ_GLBL_CFG0, CMDQ_MME_ENABLE);
}

static void goya_init_mme_qmans(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	u32 so_base_lo, so_base_hi;

	if (goya->hw_cap_initialized & HW_CAP_MME)
		return;

	so_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);

	WREG32(mmMME_SM_BASE_ADDRESS_LOW, so_base_lo);
	WREG32(mmMME_SM_BASE_ADDRESS_HIGH, so_base_hi);

	goya_init_mme_qman(hdev);
	goya_init_mme_cmdq(hdev);

	goya->hw_cap_initialized |= HW_CAP_MME;
}

static void goya_init_tpc_qman(struct hl_device *hdev, u32 base_off, int tpc_id)
{
	u32 mtr_base_lo, mtr_base_hi;
	u32 so_base_lo, so_base_hi;
	u32 gic_base_lo, gic_base_hi;
	u64 qman_base_addr;
	u32 reg_off = tpc_id * (mmTPC1_QM_PQ_PI - mmTPC0_QM_PQ_PI);

	mtr_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	mtr_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	so_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);

	gic_base_lo =
		lower_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);
	gic_base_hi =
		upper_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);

	qman_base_addr = hdev->asic_prop.sram_base_address + base_off;

	WREG32(mmTPC0_QM_PQ_BASE_LO + reg_off, lower_32_bits(qman_base_addr));
	WREG32(mmTPC0_QM_PQ_BASE_HI + reg_off, upper_32_bits(qman_base_addr));
	WREG32(mmTPC0_QM_PQ_SIZE + reg_off, ilog2(TPC_QMAN_LENGTH));
	WREG32(mmTPC0_QM_PQ_PI + reg_off, 0);
	WREG32(mmTPC0_QM_PQ_CI + reg_off, 0);
	WREG32(mmTPC0_QM_CP_LDMA_SRC_BASE_LO_OFFSET + reg_off, 0x10C0);
	WREG32(mmTPC0_QM_CP_LDMA_SRC_BASE_HI_OFFSET + reg_off, 0x10C4);
	WREG32(mmTPC0_QM_CP_LDMA_TSIZE_OFFSET + reg_off, 0x10C8);
	WREG32(mmTPC0_QM_CP_LDMA_COMMIT_OFFSET + reg_off, 0x10CC);

	WREG32(mmTPC0_QM_CP_MSG_BASE0_ADDR_LO + reg_off, mtr_base_lo);
	WREG32(mmTPC0_QM_CP_MSG_BASE0_ADDR_HI + reg_off, mtr_base_hi);
	WREG32(mmTPC0_QM_CP_MSG_BASE1_ADDR_LO + reg_off, so_base_lo);
	WREG32(mmTPC0_QM_CP_MSG_BASE1_ADDR_HI + reg_off, so_base_hi);

	WREG32(mmTPC0_QM_CQ_CFG1 + reg_off, 0x00080008);

	WREG32(mmTPC0_QM_GLBL_ERR_ADDR_LO + reg_off, gic_base_lo);
	WREG32(mmTPC0_QM_GLBL_ERR_ADDR_HI + reg_off, gic_base_hi);

	WREG32(mmTPC0_QM_GLBL_ERR_WDATA + reg_off,
			GOYA_ASYNC_EVENT_ID_TPC0_QM + tpc_id);

	WREG32(mmTPC0_QM_GLBL_ERR_CFG + reg_off, QMAN_TPC_ERR_MSG_EN);

	WREG32(mmTPC0_QM_GLBL_PROT + reg_off, QMAN_TPC_ERR_PROT);

	WREG32(mmTPC0_QM_GLBL_CFG0 + reg_off, QMAN_TPC_ENABLE);
}

static void goya_init_tpc_cmdq(struct hl_device *hdev, int tpc_id)
{
	u32 mtr_base_lo, mtr_base_hi;
	u32 so_base_lo, so_base_hi;
	u32 gic_base_lo, gic_base_hi;
	u32 reg_off = tpc_id * (mmTPC1_CMDQ_CQ_CFG1 - mmTPC0_CMDQ_CQ_CFG1);

	mtr_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	mtr_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	so_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);

	gic_base_lo =
		lower_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);
	gic_base_hi =
		upper_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);

	WREG32(mmTPC0_CMDQ_CP_MSG_BASE0_ADDR_LO + reg_off, mtr_base_lo);
	WREG32(mmTPC0_CMDQ_CP_MSG_BASE0_ADDR_HI + reg_off, mtr_base_hi);
	WREG32(mmTPC0_CMDQ_CP_MSG_BASE1_ADDR_LO + reg_off, so_base_lo);
	WREG32(mmTPC0_CMDQ_CP_MSG_BASE1_ADDR_HI + reg_off, so_base_hi);

	WREG32(mmTPC0_CMDQ_CQ_CFG1 + reg_off, 0x00140014);

	WREG32(mmTPC0_CMDQ_GLBL_ERR_ADDR_LO + reg_off, gic_base_lo);
	WREG32(mmTPC0_CMDQ_GLBL_ERR_ADDR_HI + reg_off, gic_base_hi);

	WREG32(mmTPC0_CMDQ_GLBL_ERR_WDATA + reg_off,
			GOYA_ASYNC_EVENT_ID_TPC0_CMDQ + tpc_id);

	WREG32(mmTPC0_CMDQ_GLBL_ERR_CFG + reg_off, CMDQ_TPC_ERR_MSG_EN);

	WREG32(mmTPC0_CMDQ_GLBL_PROT + reg_off, CMDQ_TPC_ERR_PROT);

	WREG32(mmTPC0_CMDQ_GLBL_CFG0 + reg_off, CMDQ_TPC_ENABLE);
}

static void goya_init_tpc_qmans(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	u32 so_base_lo, so_base_hi;
	u32 cfg_off = mmTPC1_CFG_SM_BASE_ADDRESS_LOW -
			mmTPC0_CFG_SM_BASE_ADDRESS_LOW;
	int i;

	if (goya->hw_cap_initialized & HW_CAP_TPC)
		return;

	so_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);

	for (i = 0 ; i < TPC_MAX_NUM ; i++) {
		WREG32(mmTPC0_CFG_SM_BASE_ADDRESS_LOW + i * cfg_off,
				so_base_lo);
		WREG32(mmTPC0_CFG_SM_BASE_ADDRESS_HIGH + i * cfg_off,
				so_base_hi);
	}

	goya_init_tpc_qman(hdev, TPC0_QMAN_BASE_OFFSET, 0);
	goya_init_tpc_qman(hdev, TPC1_QMAN_BASE_OFFSET, 1);
	goya_init_tpc_qman(hdev, TPC2_QMAN_BASE_OFFSET, 2);
	goya_init_tpc_qman(hdev, TPC3_QMAN_BASE_OFFSET, 3);
	goya_init_tpc_qman(hdev, TPC4_QMAN_BASE_OFFSET, 4);
	goya_init_tpc_qman(hdev, TPC5_QMAN_BASE_OFFSET, 5);
	goya_init_tpc_qman(hdev, TPC6_QMAN_BASE_OFFSET, 6);
	goya_init_tpc_qman(hdev, TPC7_QMAN_BASE_OFFSET, 7);

	for (i = 0 ; i < TPC_MAX_NUM ; i++)
		goya_init_tpc_cmdq(hdev, i);

	goya->hw_cap_initialized |= HW_CAP_TPC;
}

/*
 * goya_disable_internal_queues - Disable internal queues
 *
 * @hdev: pointer to hl_device structure
 *
 */
static void goya_disable_internal_queues(struct hl_device *hdev)
{
	WREG32(mmMME_QM_GLBL_CFG0, 0);
	WREG32(mmMME_CMDQ_GLBL_CFG0, 0);

	WREG32(mmTPC0_QM_GLBL_CFG0, 0);
	WREG32(mmTPC0_CMDQ_GLBL_CFG0, 0);

	WREG32(mmTPC1_QM_GLBL_CFG0, 0);
	WREG32(mmTPC1_CMDQ_GLBL_CFG0, 0);

	WREG32(mmTPC2_QM_GLBL_CFG0, 0);
	WREG32(mmTPC2_CMDQ_GLBL_CFG0, 0);

	WREG32(mmTPC3_QM_GLBL_CFG0, 0);
	WREG32(mmTPC3_CMDQ_GLBL_CFG0, 0);

	WREG32(mmTPC4_QM_GLBL_CFG0, 0);
	WREG32(mmTPC4_CMDQ_GLBL_CFG0, 0);

	WREG32(mmTPC5_QM_GLBL_CFG0, 0);
	WREG32(mmTPC5_CMDQ_GLBL_CFG0, 0);

	WREG32(mmTPC6_QM_GLBL_CFG0, 0);
	WREG32(mmTPC6_CMDQ_GLBL_CFG0, 0);

	WREG32(mmTPC7_QM_GLBL_CFG0, 0);
	WREG32(mmTPC7_CMDQ_GLBL_CFG0, 0);
}

/*
 * goya_stop_internal_queues - Stop internal queues
 *
 * @hdev: pointer to hl_device structure
 *
 * Returns 0 on success
 *
 */
static int goya_stop_internal_queues(struct hl_device *hdev)
{
	int rc, retval = 0;

	/*
	 * Each queue (QMAN) is a separate H/W logic. That means that each
	 * QMAN can be stopped independently and failure to stop one does NOT
	 * mandate we should not try to stop other QMANs
	 */

	rc = goya_stop_queue(hdev,
			mmMME_QM_GLBL_CFG1,
			mmMME_QM_CP_STS,
			mmMME_QM_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop MME QMAN\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmMME_CMDQ_GLBL_CFG1,
			mmMME_CMDQ_CP_STS,
			mmMME_CMDQ_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop MME CMDQ\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC0_QM_GLBL_CFG1,
			mmTPC0_QM_CP_STS,
			mmTPC0_QM_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 0 QMAN\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC0_CMDQ_GLBL_CFG1,
			mmTPC0_CMDQ_CP_STS,
			mmTPC0_CMDQ_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 0 CMDQ\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC1_QM_GLBL_CFG1,
			mmTPC1_QM_CP_STS,
			mmTPC1_QM_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 1 QMAN\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC1_CMDQ_GLBL_CFG1,
			mmTPC1_CMDQ_CP_STS,
			mmTPC1_CMDQ_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 1 CMDQ\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC2_QM_GLBL_CFG1,
			mmTPC2_QM_CP_STS,
			mmTPC2_QM_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 2 QMAN\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC2_CMDQ_GLBL_CFG1,
			mmTPC2_CMDQ_CP_STS,
			mmTPC2_CMDQ_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 2 CMDQ\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC3_QM_GLBL_CFG1,
			mmTPC3_QM_CP_STS,
			mmTPC3_QM_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 3 QMAN\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC3_CMDQ_GLBL_CFG1,
			mmTPC3_CMDQ_CP_STS,
			mmTPC3_CMDQ_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 3 CMDQ\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC4_QM_GLBL_CFG1,
			mmTPC4_QM_CP_STS,
			mmTPC4_QM_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 4 QMAN\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC4_CMDQ_GLBL_CFG1,
			mmTPC4_CMDQ_CP_STS,
			mmTPC4_CMDQ_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 4 CMDQ\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC5_QM_GLBL_CFG1,
			mmTPC5_QM_CP_STS,
			mmTPC5_QM_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 5 QMAN\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC5_CMDQ_GLBL_CFG1,
			mmTPC5_CMDQ_CP_STS,
			mmTPC5_CMDQ_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 5 CMDQ\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC6_QM_GLBL_CFG1,
			mmTPC6_QM_CP_STS,
			mmTPC6_QM_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 6 QMAN\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC6_CMDQ_GLBL_CFG1,
			mmTPC6_CMDQ_CP_STS,
			mmTPC6_CMDQ_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 6 CMDQ\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC7_QM_GLBL_CFG1,
			mmTPC7_QM_CP_STS,
			mmTPC7_QM_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 7 QMAN\n");
		retval = -EIO;
	}

	rc = goya_stop_queue(hdev,
			mmTPC7_CMDQ_GLBL_CFG1,
			mmTPC7_CMDQ_CP_STS,
			mmTPC7_CMDQ_GLBL_STS0);

	if (rc) {
		dev_err(hdev->dev, "failed to stop TPC 7 CMDQ\n");
		retval = -EIO;
	}

	return retval;
}

static void goya_resume_internal_queues(struct hl_device *hdev)
{
	WREG32(mmMME_QM_GLBL_CFG1, 0);
	WREG32(mmMME_CMDQ_GLBL_CFG1, 0);

	WREG32(mmTPC0_QM_GLBL_CFG1, 0);
	WREG32(mmTPC0_CMDQ_GLBL_CFG1, 0);

	WREG32(mmTPC1_QM_GLBL_CFG1, 0);
	WREG32(mmTPC1_CMDQ_GLBL_CFG1, 0);

	WREG32(mmTPC2_QM_GLBL_CFG1, 0);
	WREG32(mmTPC2_CMDQ_GLBL_CFG1, 0);

	WREG32(mmTPC3_QM_GLBL_CFG1, 0);
	WREG32(mmTPC3_CMDQ_GLBL_CFG1, 0);

	WREG32(mmTPC4_QM_GLBL_CFG1, 0);
	WREG32(mmTPC4_CMDQ_GLBL_CFG1, 0);

	WREG32(mmTPC5_QM_GLBL_CFG1, 0);
	WREG32(mmTPC5_CMDQ_GLBL_CFG1, 0);

	WREG32(mmTPC6_QM_GLBL_CFG1, 0);
	WREG32(mmTPC6_CMDQ_GLBL_CFG1, 0);

	WREG32(mmTPC7_QM_GLBL_CFG1, 0);
	WREG32(mmTPC7_CMDQ_GLBL_CFG1, 0);
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

	goya_init_dma_qmans(hdev);

	goya_init_mme_qmans(hdev);

	goya_init_tpc_qmans(hdev);

	rc = goya_init_cpu_queues(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU H/W queues %d\n",
			rc);
		goto disable_queues;
	}

	/* CPU initialization is finished, we can now move to 48 bit DMA mask */
	rc = pci_set_dma_mask(hdev->pdev, DMA_BIT_MASK(48));
	if (rc) {
		dev_warn(hdev->dev, "Unable to set pci dma mask to 48 bits\n");
		rc = pci_set_dma_mask(hdev->pdev, DMA_BIT_MASK(32));
		if (rc) {
			dev_err(hdev->dev,
				"Unable to set pci dma mask to 32 bits\n");
			goto disable_pci_access;
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
			goto disable_pci_access;
		}
	}

	/* Perform read from the device to flush all MSI-X configuration */
	val = RREG32(mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG);

	return 0;

disable_pci_access:
	goya_send_pci_access_msg(hdev, ARMCP_PACKET_DISABLE_PCI_ACCESS);
disable_queues:
	goya_disable_internal_queues(hdev);
	goya_disable_external_queues(hdev);

	return rc;
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
	int rc;

	rc = goya_stop_internal_queues(hdev);

	if (rc) {
		dev_err(hdev->dev, "failed to stop internal queues\n");
		return rc;
	}

	rc = goya_stop_external_queues(hdev);

	if (rc) {
		dev_err(hdev->dev, "failed to stop external queues\n");
		return rc;
	}

	rc = goya_send_pci_access_msg(hdev, ARMCP_PACKET_DISABLE_PCI_ACCESS);
	if (rc)
		dev_err(hdev->dev, "Failed to disable PCI access from CPU\n");

	return rc;
}

int goya_resume(struct hl_device *hdev)
{
	int rc;

	goya_resume_external_queues(hdev);
	goya_resume_internal_queues(hdev);

	rc = goya_send_pci_access_msg(hdev, ARMCP_PACKET_ENABLE_PCI_ACCESS);
	if (rc)
		dev_err(hdev->dev, "Failed to enable PCI access from CPU\n");
	return rc;
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

void goya_ring_doorbell(struct hl_device *hdev, u32 hw_queue_id, u32 pi)
{
	u32 db_reg_offset, db_value;
	bool invalid_queue = false;

	switch (hw_queue_id) {
	case GOYA_QUEUE_ID_DMA_0:
		db_reg_offset = mmDMA_QM_0_PQ_PI;
		break;

	case GOYA_QUEUE_ID_DMA_1:
		db_reg_offset = mmDMA_QM_1_PQ_PI;
		break;

	case GOYA_QUEUE_ID_DMA_2:
		db_reg_offset = mmDMA_QM_2_PQ_PI;
		break;

	case GOYA_QUEUE_ID_DMA_3:
		db_reg_offset = mmDMA_QM_3_PQ_PI;
		break;

	case GOYA_QUEUE_ID_DMA_4:
		db_reg_offset = mmDMA_QM_4_PQ_PI;
		break;

	case GOYA_QUEUE_ID_CPU_PQ:
		if (hdev->cpu_queues_enable)
			db_reg_offset = mmCPU_IF_PF_PQ_PI;
		else
			invalid_queue = true;
		break;

	case GOYA_QUEUE_ID_MME:
		db_reg_offset = mmMME_QM_PQ_PI;
		break;

	case GOYA_QUEUE_ID_TPC0:
		db_reg_offset = mmTPC0_QM_PQ_PI;
		break;

	case GOYA_QUEUE_ID_TPC1:
		db_reg_offset = mmTPC1_QM_PQ_PI;
		break;

	case GOYA_QUEUE_ID_TPC2:
		db_reg_offset = mmTPC2_QM_PQ_PI;
		break;

	case GOYA_QUEUE_ID_TPC3:
		db_reg_offset = mmTPC3_QM_PQ_PI;
		break;

	case GOYA_QUEUE_ID_TPC4:
		db_reg_offset = mmTPC4_QM_PQ_PI;
		break;

	case GOYA_QUEUE_ID_TPC5:
		db_reg_offset = mmTPC5_QM_PQ_PI;
		break;

	case GOYA_QUEUE_ID_TPC6:
		db_reg_offset = mmTPC6_QM_PQ_PI;
		break;

	case GOYA_QUEUE_ID_TPC7:
		db_reg_offset = mmTPC7_QM_PQ_PI;
		break;

	default:
		invalid_queue = true;
	}

	if (invalid_queue) {
		/* Should never get here */
		dev_err(hdev->dev, "h/w queue %d is invalid. Can't set pi\n",
			hw_queue_id);
		return;
	}

	db_value = pi;

	/* ring the doorbell */
	WREG32(db_reg_offset, db_value);

	if (hw_queue_id == GOYA_QUEUE_ID_CPU_PQ)
		WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR,
				GOYA_ASYNC_EVENT_ID_PI_UPDATE);
}

void goya_flush_pq_write(struct hl_device *hdev, u64 *pq, u64 exp_val)
{
	/* Not needed in Goya */
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

void *goya_get_int_queue_base(struct hl_device *hdev, u32 queue_id,
				dma_addr_t *dma_handle,	u16 *queue_len)
{
	void *base;
	u32 offset;

	*dma_handle = hdev->asic_prop.sram_base_address;

	base = hdev->pcie_bar[SRAM_CFG_BAR_ID];

	switch (queue_id) {
	case GOYA_QUEUE_ID_MME:
		offset = MME_QMAN_BASE_OFFSET;
		*queue_len = MME_QMAN_LENGTH;
		break;
	case GOYA_QUEUE_ID_TPC0:
		offset = TPC0_QMAN_BASE_OFFSET;
		*queue_len = TPC_QMAN_LENGTH;
		break;
	case GOYA_QUEUE_ID_TPC1:
		offset = TPC1_QMAN_BASE_OFFSET;
		*queue_len = TPC_QMAN_LENGTH;
		break;
	case GOYA_QUEUE_ID_TPC2:
		offset = TPC2_QMAN_BASE_OFFSET;
		*queue_len = TPC_QMAN_LENGTH;
		break;
	case GOYA_QUEUE_ID_TPC3:
		offset = TPC3_QMAN_BASE_OFFSET;
		*queue_len = TPC_QMAN_LENGTH;
		break;
	case GOYA_QUEUE_ID_TPC4:
		offset = TPC4_QMAN_BASE_OFFSET;
		*queue_len = TPC_QMAN_LENGTH;
		break;
	case GOYA_QUEUE_ID_TPC5:
		offset = TPC5_QMAN_BASE_OFFSET;
		*queue_len = TPC_QMAN_LENGTH;
		break;
	case GOYA_QUEUE_ID_TPC6:
		offset = TPC6_QMAN_BASE_OFFSET;
		*queue_len = TPC_QMAN_LENGTH;
		break;
	case GOYA_QUEUE_ID_TPC7:
		offset = TPC7_QMAN_BASE_OFFSET;
		*queue_len = TPC_QMAN_LENGTH;
		break;
	default:
		dev_err(hdev->dev, "Got invalid queue id %d\n", queue_id);
		return NULL;
	}

	base += offset;
	*dma_handle += offset;

	return base;
}

int goya_send_cpu_message(struct hl_device *hdev, u32 *msg, u16 len,
				u32 timeout, long *result)
{
	struct goya_device *goya = hdev->asic_specific;
	struct armcp_packet *pkt;
	dma_addr_t pkt_dma_addr;
	u32 tmp;
	int rc = 0;

	if (!(goya->hw_cap_initialized & HW_CAP_CPU_Q)) {
		if (result)
			*result = 0;
		return 0;
	}

	if (len > CPU_CB_SIZE) {
		dev_err(hdev->dev, "Invalid CPU message size of %d bytes\n",
			len);
		return -ENOMEM;
	}

	pkt = hdev->asic_funcs->cpu_accessible_dma_pool_alloc(hdev, len,
								&pkt_dma_addr);
	if (!pkt) {
		dev_err(hdev->dev,
			"Failed to allocate DMA memory for packet to CPU\n");
		return -ENOMEM;
	}

	memcpy(pkt, msg, len);

	mutex_lock(&hdev->send_cpu_message_lock);

	if (hdev->disabled)
		goto out;

	rc = hl_hw_queue_send_cb_no_cmpl(hdev, GOYA_QUEUE_ID_CPU_PQ, len,
			pkt_dma_addr);
	if (rc) {
		dev_err(hdev->dev, "Failed to send CB on CPU PQ (%d)\n", rc);
		goto out;
	}

	rc = hl_poll_timeout_memory(hdev, (u64) (uintptr_t) &pkt->fence,
					timeout, &tmp);

	hl_hw_queue_inc_ci_kernel(hdev, GOYA_QUEUE_ID_CPU_PQ);

	if (rc == -ETIMEDOUT) {
		dev_err(hdev->dev,
			"Timeout while waiting for CPU packet fence\n");
		goto out;
	}

	if (tmp == ARMCP_PACKET_FENCE_VAL) {
		rc = (pkt->ctl & ARMCP_PKT_CTL_RC_MASK) >>
						ARMCP_PKT_CTL_RC_SHIFT;
		if (rc) {
			dev_err(hdev->dev,
				"F/W ERROR %d for CPU packet %d\n",
				rc, (pkt->ctl & ARMCP_PKT_CTL_OPCODE_MASK)
						>> ARMCP_PKT_CTL_OPCODE_SHIFT);
			rc = -EINVAL;
		} else if (result) {
			*result = pkt->result;
		}
	} else {
		dev_err(hdev->dev, "CPU packet wrong fence value\n");
		rc = -EINVAL;
	}

out:
	mutex_unlock(&hdev->send_cpu_message_lock);

	hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev, len, pkt);

	return rc;
}

int goya_test_queue(struct hl_device *hdev, u32 hw_queue_id)
{
	struct packet_msg_prot *fence_pkt;
	dma_addr_t pkt_dma_addr;
	u32 fence_val, tmp;
	dma_addr_t fence_dma_addr;
	u32 *fence_ptr;
	int rc;

	fence_val = GOYA_QMAN0_FENCE_VAL;

	fence_ptr = hdev->asic_funcs->dma_pool_zalloc(hdev, 4, GFP_KERNEL,
							&fence_dma_addr);
	if (!fence_ptr) {
		dev_err(hdev->dev,
			"Failed to allocate memory for queue testing\n");
		return -ENOMEM;
	}

	*fence_ptr = 0;

	fence_pkt = hdev->asic_funcs->dma_pool_zalloc(hdev,
					sizeof(struct packet_msg_prot),
					GFP_KERNEL, &pkt_dma_addr);
	if (!fence_pkt) {
		dev_err(hdev->dev,
			"Failed to allocate packet for queue testing\n");
		rc = -ENOMEM;
		goto free_fence_ptr;
	}

	fence_pkt->ctl = (PACKET_MSG_PROT << GOYA_PKT_CTL_OPCODE_SHIFT) |
			(1 << GOYA_PKT_CTL_EB_SHIFT) |
			(1 << GOYA_PKT_CTL_MB_SHIFT);
	fence_pkt->value = fence_val;
	fence_pkt->addr = fence_dma_addr +
				hdev->asic_prop.host_phys_base_address;

	rc = hl_hw_queue_send_cb_no_cmpl(hdev, hw_queue_id,
					sizeof(struct packet_msg_prot),
					pkt_dma_addr);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to send fence packet\n");
		goto free_pkt;
	}

	rc = hl_poll_timeout_memory(hdev, (u64) (uintptr_t) fence_ptr,
					GOYA_TEST_QUEUE_WAIT_USEC, &tmp);

	hl_hw_queue_inc_ci_kernel(hdev, hw_queue_id);

	if ((!rc) && (tmp == fence_val)) {
		dev_info(hdev->dev,
			"queue test on H/W queue %d succeeded\n",
			hw_queue_id);
	} else {
		dev_err(hdev->dev,
			"H/W queue %d test failed (scratch(0x%08llX) == 0x%08X)\n",
			hw_queue_id, (unsigned long long) fence_dma_addr, tmp);
		rc = -EINVAL;
	}

free_pkt:
	hdev->asic_funcs->dma_pool_free(hdev, (void *) fence_pkt,
					pkt_dma_addr);
free_fence_ptr:
	hdev->asic_funcs->dma_pool_free(hdev, (void *) fence_ptr,
					fence_dma_addr);
	return rc;
}

int goya_test_cpu_queue(struct hl_device *hdev)
{
	struct armcp_packet test_pkt;
	long result;
	int rc;

	/* cpu_queues_enable flag is always checked in send cpu message */

	memset(&test_pkt, 0, sizeof(test_pkt));

	test_pkt.ctl = ARMCP_PACKET_TEST << ARMCP_PKT_CTL_OPCODE_SHIFT;
	test_pkt.value = ARMCP_PACKET_FENCE_VAL;

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &test_pkt,
			sizeof(test_pkt), HL_DEVICE_TIMEOUT_USEC, &result);

	if (!rc)
		dev_info(hdev->dev, "queue test on CPU queue succeeded\n");
	else
		dev_err(hdev->dev, "CPU queue test failed (0x%08lX)\n", result);

	return rc;
}

static int goya_test_queues(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	int i, rc, ret_val = 0;

	for (i = 0 ; i < NUMBER_OF_EXT_HW_QUEUES ; i++) {
		rc = goya_test_queue(hdev, i);
		if (rc)
			ret_val = -EINVAL;
	}

	if (hdev->cpu_queues_enable) {
		rc = goya->test_cpu_queue(hdev);
		if (rc)
			ret_val = -EINVAL;
	}

	return ret_val;
}

void *goya_dma_pool_zalloc(struct hl_device *hdev, size_t size, gfp_t mem_flags,
				dma_addr_t *dma_handle)
{
	if (size > GOYA_DMA_POOL_BLK_SIZE)
		return NULL;

	return dma_pool_zalloc(hdev->dma_pool, mem_flags, dma_handle);
}

void goya_dma_pool_free(struct hl_device *hdev, void *vaddr,
			dma_addr_t dma_addr)
{
	dma_pool_free(hdev->dma_pool, vaddr, dma_addr);
}

void *goya_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size,
			dma_addr_t *dma_handle)
{
	u64 kernel_addr;

	/* roundup to CPU_PKT_SIZE */
	size = (size + (CPU_PKT_SIZE - 1)) & CPU_PKT_MASK;

	kernel_addr = gen_pool_alloc(hdev->cpu_accessible_dma_pool, size);

	*dma_handle = hdev->cpu_accessible_dma_address +
		(kernel_addr - (u64) (uintptr_t) hdev->cpu_accessible_dma_mem);

	return (void *) (uintptr_t) kernel_addr;
}

void goya_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size,
			void *vaddr)
{
	/* roundup to CPU_PKT_SIZE */
	size = (size + (CPU_PKT_SIZE - 1)) & CPU_PKT_MASK;

	gen_pool_free(hdev->cpu_accessible_dma_pool, (u64) (uintptr_t) vaddr,
			size);
}


static void goya_hw_queues_lock(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;

	spin_lock(&goya->hw_queues_lock);
}

static void goya_hw_queues_unlock(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;

	spin_unlock(&goya->hw_queues_lock);
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
	.ring_doorbell = goya_ring_doorbell,
	.flush_pq_write = goya_flush_pq_write,
	.dma_alloc_coherent = goya_dma_alloc_coherent,
	.dma_free_coherent = goya_dma_free_coherent,
	.get_int_queue_base = goya_get_int_queue_base,
	.test_queues = goya_test_queues,
	.dma_pool_zalloc = goya_dma_pool_zalloc,
	.dma_pool_free = goya_dma_pool_free,
	.cpu_accessible_dma_pool_alloc = goya_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = goya_cpu_accessible_dma_pool_free,
	.hw_queues_lock = goya_hw_queues_lock,
	.hw_queues_unlock = goya_hw_queues_unlock,
	.send_cpu_message = goya_send_cpu_message
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
