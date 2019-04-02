// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "goyaP.h"
#include "include/hw_ip/mmu/mmu_general.h"
#include "include/hw_ip/mmu/mmu_v1_0.h"
#include "include/goya/asic_reg/goya_masks.h"

#include <linux/pci.h>
#include <linux/genalloc.h>
#include <linux/hwmon.h>
#include <linux/io-64-nonatomic-lo-hi.h>

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

#define GOYA_MMU_REGS_NUM		63

#define GOYA_DMA_POOL_BLK_SIZE		0x100		/* 256 bytes */

#define GOYA_RESET_TIMEOUT_MSEC		500		/* 500ms */
#define GOYA_PLDM_RESET_TIMEOUT_MSEC	20000		/* 20s */
#define GOYA_RESET_WAIT_MSEC		1		/* 1ms */
#define GOYA_CPU_RESET_WAIT_MSEC	100		/* 100ms */
#define GOYA_PLDM_RESET_WAIT_MSEC	1000		/* 1s */
#define GOYA_TEST_QUEUE_WAIT_USEC	100000		/* 100ms */
#define GOYA_PLDM_MMU_TIMEOUT_USEC	(MMU_CONFIG_TIMEOUT_USEC * 100)
#define GOYA_PLDM_QMAN0_TIMEOUT_USEC	(HL_DEVICE_TIMEOUT_USEC * 30)

#define GOYA_QMAN0_FENCE_VAL		0xD169B243

#define GOYA_MAX_STRING_LEN		20

#define GOYA_CB_POOL_CB_CNT		512
#define GOYA_CB_POOL_CB_SIZE		0x20000		/* 128KB */

static const char goya_irq_name[GOYA_MSIX_ENTRIES][GOYA_MAX_STRING_LEN] = {
		"goya cq 0", "goya cq 1", "goya cq 2", "goya cq 3",
		"goya cq 4", "goya cpu eq"
};

static u16 goya_packet_sizes[MAX_PACKET_ID] = {
	[PACKET_WREG_32]	= sizeof(struct packet_wreg32),
	[PACKET_WREG_BULK]	= sizeof(struct packet_wreg_bulk),
	[PACKET_MSG_LONG]	= sizeof(struct packet_msg_long),
	[PACKET_MSG_SHORT]	= sizeof(struct packet_msg_short),
	[PACKET_CP_DMA]		= sizeof(struct packet_cp_dma),
	[PACKET_MSG_PROT]	= sizeof(struct packet_msg_prot),
	[PACKET_FENCE]		= sizeof(struct packet_fence),
	[PACKET_LIN_DMA]	= sizeof(struct packet_lin_dma),
	[PACKET_NOP]		= sizeof(struct packet_nop),
	[PACKET_STOP]		= sizeof(struct packet_stop)
};

static u64 goya_mmu_regs[GOYA_MMU_REGS_NUM] = {
	mmDMA_QM_0_GLBL_NON_SECURE_PROPS,
	mmDMA_QM_1_GLBL_NON_SECURE_PROPS,
	mmDMA_QM_2_GLBL_NON_SECURE_PROPS,
	mmDMA_QM_3_GLBL_NON_SECURE_PROPS,
	mmDMA_QM_4_GLBL_NON_SECURE_PROPS,
	mmTPC0_QM_GLBL_SECURE_PROPS,
	mmTPC0_QM_GLBL_NON_SECURE_PROPS,
	mmTPC0_CMDQ_GLBL_SECURE_PROPS,
	mmTPC0_CMDQ_GLBL_NON_SECURE_PROPS,
	mmTPC0_CFG_ARUSER,
	mmTPC0_CFG_AWUSER,
	mmTPC1_QM_GLBL_SECURE_PROPS,
	mmTPC1_QM_GLBL_NON_SECURE_PROPS,
	mmTPC1_CMDQ_GLBL_SECURE_PROPS,
	mmTPC1_CMDQ_GLBL_NON_SECURE_PROPS,
	mmTPC1_CFG_ARUSER,
	mmTPC1_CFG_AWUSER,
	mmTPC2_QM_GLBL_SECURE_PROPS,
	mmTPC2_QM_GLBL_NON_SECURE_PROPS,
	mmTPC2_CMDQ_GLBL_SECURE_PROPS,
	mmTPC2_CMDQ_GLBL_NON_SECURE_PROPS,
	mmTPC2_CFG_ARUSER,
	mmTPC2_CFG_AWUSER,
	mmTPC3_QM_GLBL_SECURE_PROPS,
	mmTPC3_QM_GLBL_NON_SECURE_PROPS,
	mmTPC3_CMDQ_GLBL_SECURE_PROPS,
	mmTPC3_CMDQ_GLBL_NON_SECURE_PROPS,
	mmTPC3_CFG_ARUSER,
	mmTPC3_CFG_AWUSER,
	mmTPC4_QM_GLBL_SECURE_PROPS,
	mmTPC4_QM_GLBL_NON_SECURE_PROPS,
	mmTPC4_CMDQ_GLBL_SECURE_PROPS,
	mmTPC4_CMDQ_GLBL_NON_SECURE_PROPS,
	mmTPC4_CFG_ARUSER,
	mmTPC4_CFG_AWUSER,
	mmTPC5_QM_GLBL_SECURE_PROPS,
	mmTPC5_QM_GLBL_NON_SECURE_PROPS,
	mmTPC5_CMDQ_GLBL_SECURE_PROPS,
	mmTPC5_CMDQ_GLBL_NON_SECURE_PROPS,
	mmTPC5_CFG_ARUSER,
	mmTPC5_CFG_AWUSER,
	mmTPC6_QM_GLBL_SECURE_PROPS,
	mmTPC6_QM_GLBL_NON_SECURE_PROPS,
	mmTPC6_CMDQ_GLBL_SECURE_PROPS,
	mmTPC6_CMDQ_GLBL_NON_SECURE_PROPS,
	mmTPC6_CFG_ARUSER,
	mmTPC6_CFG_AWUSER,
	mmTPC7_QM_GLBL_SECURE_PROPS,
	mmTPC7_QM_GLBL_NON_SECURE_PROPS,
	mmTPC7_CMDQ_GLBL_SECURE_PROPS,
	mmTPC7_CMDQ_GLBL_NON_SECURE_PROPS,
	mmTPC7_CFG_ARUSER,
	mmTPC7_CFG_AWUSER,
	mmMME_QM_GLBL_SECURE_PROPS,
	mmMME_QM_GLBL_NON_SECURE_PROPS,
	mmMME_CMDQ_GLBL_SECURE_PROPS,
	mmMME_CMDQ_GLBL_NON_SECURE_PROPS,
	mmMME_SBA_CONTROL_DATA,
	mmMME_SBB_CONTROL_DATA,
	mmMME_SBC_CONTROL_DATA,
	mmMME_WBC_CONTROL_DATA,
	mmPCIE_WRAP_PSOC_ARUSER,
	mmPCIE_WRAP_PSOC_AWUSER
};

static u32 goya_all_events[] = {
	GOYA_ASYNC_EVENT_ID_PCIE_IF,
	GOYA_ASYNC_EVENT_ID_TPC0_ECC,
	GOYA_ASYNC_EVENT_ID_TPC1_ECC,
	GOYA_ASYNC_EVENT_ID_TPC2_ECC,
	GOYA_ASYNC_EVENT_ID_TPC3_ECC,
	GOYA_ASYNC_EVENT_ID_TPC4_ECC,
	GOYA_ASYNC_EVENT_ID_TPC5_ECC,
	GOYA_ASYNC_EVENT_ID_TPC6_ECC,
	GOYA_ASYNC_EVENT_ID_TPC7_ECC,
	GOYA_ASYNC_EVENT_ID_MME_ECC,
	GOYA_ASYNC_EVENT_ID_MME_ECC_EXT,
	GOYA_ASYNC_EVENT_ID_MMU_ECC,
	GOYA_ASYNC_EVENT_ID_DMA_MACRO,
	GOYA_ASYNC_EVENT_ID_DMA_ECC,
	GOYA_ASYNC_EVENT_ID_CPU_IF_ECC,
	GOYA_ASYNC_EVENT_ID_PSOC_MEM,
	GOYA_ASYNC_EVENT_ID_PSOC_CORESIGHT,
	GOYA_ASYNC_EVENT_ID_SRAM0,
	GOYA_ASYNC_EVENT_ID_SRAM1,
	GOYA_ASYNC_EVENT_ID_SRAM2,
	GOYA_ASYNC_EVENT_ID_SRAM3,
	GOYA_ASYNC_EVENT_ID_SRAM4,
	GOYA_ASYNC_EVENT_ID_SRAM5,
	GOYA_ASYNC_EVENT_ID_SRAM6,
	GOYA_ASYNC_EVENT_ID_SRAM7,
	GOYA_ASYNC_EVENT_ID_SRAM8,
	GOYA_ASYNC_EVENT_ID_SRAM9,
	GOYA_ASYNC_EVENT_ID_SRAM10,
	GOYA_ASYNC_EVENT_ID_SRAM11,
	GOYA_ASYNC_EVENT_ID_SRAM12,
	GOYA_ASYNC_EVENT_ID_SRAM13,
	GOYA_ASYNC_EVENT_ID_SRAM14,
	GOYA_ASYNC_EVENT_ID_SRAM15,
	GOYA_ASYNC_EVENT_ID_SRAM16,
	GOYA_ASYNC_EVENT_ID_SRAM17,
	GOYA_ASYNC_EVENT_ID_SRAM18,
	GOYA_ASYNC_EVENT_ID_SRAM19,
	GOYA_ASYNC_EVENT_ID_SRAM20,
	GOYA_ASYNC_EVENT_ID_SRAM21,
	GOYA_ASYNC_EVENT_ID_SRAM22,
	GOYA_ASYNC_EVENT_ID_SRAM23,
	GOYA_ASYNC_EVENT_ID_SRAM24,
	GOYA_ASYNC_EVENT_ID_SRAM25,
	GOYA_ASYNC_EVENT_ID_SRAM26,
	GOYA_ASYNC_EVENT_ID_SRAM27,
	GOYA_ASYNC_EVENT_ID_SRAM28,
	GOYA_ASYNC_EVENT_ID_SRAM29,
	GOYA_ASYNC_EVENT_ID_GIC500,
	GOYA_ASYNC_EVENT_ID_PLL0,
	GOYA_ASYNC_EVENT_ID_PLL1,
	GOYA_ASYNC_EVENT_ID_PLL3,
	GOYA_ASYNC_EVENT_ID_PLL4,
	GOYA_ASYNC_EVENT_ID_PLL5,
	GOYA_ASYNC_EVENT_ID_PLL6,
	GOYA_ASYNC_EVENT_ID_AXI_ECC,
	GOYA_ASYNC_EVENT_ID_L2_RAM_ECC,
	GOYA_ASYNC_EVENT_ID_PSOC_GPIO_05_SW_RESET,
	GOYA_ASYNC_EVENT_ID_PSOC_GPIO_10_VRHOT_ICRIT,
	GOYA_ASYNC_EVENT_ID_PCIE_DEC,
	GOYA_ASYNC_EVENT_ID_TPC0_DEC,
	GOYA_ASYNC_EVENT_ID_TPC1_DEC,
	GOYA_ASYNC_EVENT_ID_TPC2_DEC,
	GOYA_ASYNC_EVENT_ID_TPC3_DEC,
	GOYA_ASYNC_EVENT_ID_TPC4_DEC,
	GOYA_ASYNC_EVENT_ID_TPC5_DEC,
	GOYA_ASYNC_EVENT_ID_TPC6_DEC,
	GOYA_ASYNC_EVENT_ID_TPC7_DEC,
	GOYA_ASYNC_EVENT_ID_MME_WACS,
	GOYA_ASYNC_EVENT_ID_MME_WACSD,
	GOYA_ASYNC_EVENT_ID_CPU_AXI_SPLITTER,
	GOYA_ASYNC_EVENT_ID_PSOC_AXI_DEC,
	GOYA_ASYNC_EVENT_ID_PSOC,
	GOYA_ASYNC_EVENT_ID_TPC0_KRN_ERR,
	GOYA_ASYNC_EVENT_ID_TPC1_KRN_ERR,
	GOYA_ASYNC_EVENT_ID_TPC2_KRN_ERR,
	GOYA_ASYNC_EVENT_ID_TPC3_KRN_ERR,
	GOYA_ASYNC_EVENT_ID_TPC4_KRN_ERR,
	GOYA_ASYNC_EVENT_ID_TPC5_KRN_ERR,
	GOYA_ASYNC_EVENT_ID_TPC6_KRN_ERR,
	GOYA_ASYNC_EVENT_ID_TPC7_KRN_ERR,
	GOYA_ASYNC_EVENT_ID_TPC0_CMDQ,
	GOYA_ASYNC_EVENT_ID_TPC1_CMDQ,
	GOYA_ASYNC_EVENT_ID_TPC2_CMDQ,
	GOYA_ASYNC_EVENT_ID_TPC3_CMDQ,
	GOYA_ASYNC_EVENT_ID_TPC4_CMDQ,
	GOYA_ASYNC_EVENT_ID_TPC5_CMDQ,
	GOYA_ASYNC_EVENT_ID_TPC6_CMDQ,
	GOYA_ASYNC_EVENT_ID_TPC7_CMDQ,
	GOYA_ASYNC_EVENT_ID_TPC0_QM,
	GOYA_ASYNC_EVENT_ID_TPC1_QM,
	GOYA_ASYNC_EVENT_ID_TPC2_QM,
	GOYA_ASYNC_EVENT_ID_TPC3_QM,
	GOYA_ASYNC_EVENT_ID_TPC4_QM,
	GOYA_ASYNC_EVENT_ID_TPC5_QM,
	GOYA_ASYNC_EVENT_ID_TPC6_QM,
	GOYA_ASYNC_EVENT_ID_TPC7_QM,
	GOYA_ASYNC_EVENT_ID_MME_QM,
	GOYA_ASYNC_EVENT_ID_MME_CMDQ,
	GOYA_ASYNC_EVENT_ID_DMA0_QM,
	GOYA_ASYNC_EVENT_ID_DMA1_QM,
	GOYA_ASYNC_EVENT_ID_DMA2_QM,
	GOYA_ASYNC_EVENT_ID_DMA3_QM,
	GOYA_ASYNC_EVENT_ID_DMA4_QM,
	GOYA_ASYNC_EVENT_ID_DMA0_CH,
	GOYA_ASYNC_EVENT_ID_DMA1_CH,
	GOYA_ASYNC_EVENT_ID_DMA2_CH,
	GOYA_ASYNC_EVENT_ID_DMA3_CH,
	GOYA_ASYNC_EVENT_ID_DMA4_CH,
	GOYA_ASYNC_EVENT_ID_TPC0_BMON_SPMU,
	GOYA_ASYNC_EVENT_ID_TPC1_BMON_SPMU,
	GOYA_ASYNC_EVENT_ID_TPC2_BMON_SPMU,
	GOYA_ASYNC_EVENT_ID_TPC3_BMON_SPMU,
	GOYA_ASYNC_EVENT_ID_TPC4_BMON_SPMU,
	GOYA_ASYNC_EVENT_ID_TPC5_BMON_SPMU,
	GOYA_ASYNC_EVENT_ID_TPC6_BMON_SPMU,
	GOYA_ASYNC_EVENT_ID_TPC7_BMON_SPMU,
	GOYA_ASYNC_EVENT_ID_DMA_BM_CH0,
	GOYA_ASYNC_EVENT_ID_DMA_BM_CH1,
	GOYA_ASYNC_EVENT_ID_DMA_BM_CH2,
	GOYA_ASYNC_EVENT_ID_DMA_BM_CH3,
	GOYA_ASYNC_EVENT_ID_DMA_BM_CH4
};

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

	prop->mmu_pgt_addr = MMU_PAGE_TABLES_ADDR;
	prop->mmu_dram_default_page_addr = MMU_DRAM_DEFAULT_PAGE_ADDR;
	if (hdev->pldm)
		prop->mmu_pgt_size = 0x800000; /* 8MB */
	else
		prop->mmu_pgt_size = MMU_PAGE_TABLES_SIZE;
	prop->mmu_pte_size = HL_PTE_SIZE;
	prop->mmu_hop_table_size = HOP_TABLE_SIZE;
	prop->mmu_hop0_tables_total_size = HOP0_TABLES_TOTAL_SIZE;
	prop->dram_page_size = PAGE_SIZE_2MB;

	prop->host_phys_base_address = HOST_PHYS_BASE;
	prop->va_space_host_start_address = VA_HOST_SPACE_START;
	prop->va_space_host_end_address = VA_HOST_SPACE_END;
	prop->va_space_dram_start_address = VA_DDR_SPACE_START;
	prop->va_space_dram_end_address = VA_DDR_SPACE_END;
	prop->dram_size_for_default_page_mapping =
			prop->va_space_dram_end_address;
	prop->cfg_size = CFG_SIZE;
	prop->max_asid = MAX_ASID;
	prop->num_of_events = GOYA_ASYNC_EVENT_ID_SIZE;
	prop->high_pll = PLL_HIGH_DEFAULT;
	prop->cb_pool_cb_cnt = GOYA_CB_POOL_CB_CNT;
	prop->cb_pool_cb_size = GOYA_CB_POOL_CB_SIZE;
	prop->max_power_default = MAX_POWER_DEFAULT;
	prop->tpc_enabled_mask = TPC_ENABLED_MASK;
	prop->pcie_dbi_base_address = mmPCIE_DBI_BASE;
	prop->pcie_aux_dbi_reg_addr = CFG_BASE + mmPCIE_AUX_DBI;
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
static int goya_pci_bars_map(struct hl_device *hdev)
{
	static const char * const name[] = {"SRAM_CFG", "MSIX", "DDR"};
	bool is_wc[3] = {false, false, true};
	int rc;

	rc = hl_pci_bars_map(hdev, name, is_wc);
	if (rc)
		return rc;

	hdev->rmmio = hdev->pcie_bar[SRAM_CFG_BAR_ID] +
			(CFG_BASE - SRAM_BASE_ADDR);

	return 0;
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
	rc = hl_pci_set_dram_bar_base(hdev, 1, 4, addr);
	if (rc)
		return rc;

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
	return hl_pci_init_iatu(hdev, SRAM_BASE_ADDR, DRAM_PHYS_BASE,
				HOST_PHYS_SIZE);
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

	rc = hl_pci_init(hdev, 39);
	if (rc)
		return rc;

	if (!hdev->pldm) {
		val = RREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS);
		if (val & PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_SRIOV_EN_MASK)
			dev_warn(hdev->dev,
				"PCI strap is not configured correctly, PCI bus errors may occur\n");
	}

	return 0;
}

/*
 * goya_early_fini - GOYA early finalization code
 *
 * @hdev: pointer to hl_device structure
 *
 * Unmap PCI bars
 *
 */
static int goya_early_fini(struct hl_device *hdev)
{
	hl_pci_fini(hdev);

	return 0;
}

static void goya_mmu_prepare_reg(struct hl_device *hdev, u64 reg, u32 asid)
{
	/* mask to zero the MMBP and ASID bits */
	WREG32_AND(reg, ~0x7FF);
	WREG32_OR(reg, asid);
}

static void goya_qman0_set_security(struct hl_device *hdev, bool secure)
{
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU))
		return;

	if (secure)
		WREG32(mmDMA_QM_0_GLBL_PROT, QMAN_DMA_FULLY_TRUSTED);
	else
		WREG32(mmDMA_QM_0_GLBL_PROT, QMAN_DMA_PARTLY_TRUSTED);

	RREG32(mmDMA_QM_0_GLBL_PROT);
}

/*
 * goya_fetch_psoc_frequency - Fetch PSOC frequency values
 *
 * @hdev: pointer to hl_device structure
 *
 */
static void goya_fetch_psoc_frequency(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	prop->psoc_pci_pll_nr = RREG32(mmPSOC_PCI_PLL_NR);
	prop->psoc_pci_pll_nf = RREG32(mmPSOC_PCI_PLL_NF);
	prop->psoc_pci_pll_od = RREG32(mmPSOC_PCI_PLL_OD);
	prop->psoc_pci_pll_div_factor = RREG32(mmPSOC_PCI_PLL_DIV_FACTOR_1);
}

/*
 * goya_late_init - GOYA late initialization code
 *
 * @hdev: pointer to hl_device structure
 *
 * Get ArmCP info and send message to CPU to enable PCI access
 */
static int goya_late_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	rc = goya_armcp_info_get(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to get armcp info\n");
		return rc;
	}

	/* Now that we have the DRAM size in ASIC prop, we need to check
	 * its size and configure the DMA_IF DDR wrap protection (which is in
	 * the MMU block) accordingly. The value is the log2 of the DRAM size
	 */
	WREG32(mmMMU_LOG2_DDR_SIZE, ilog2(prop->dram_size));

	rc = hl_fw_send_pci_access_msg(hdev, ARMCP_PACKET_ENABLE_PCI_ACCESS);
	if (rc) {
		dev_err(hdev->dev, "Failed to enable PCI access from CPU\n");
		return rc;
	}

	WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR,
			GOYA_ASYNC_EVENT_ID_INTS_REGISTER);

	goya_fetch_psoc_frequency(hdev);

	rc = goya_mmu_clear_pgt_range(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to clear MMU page tables range\n");
		goto disable_pci_access;
	}

	rc = goya_mmu_set_dram_default_page(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to set DRAM default page\n");
		goto disable_pci_access;
	}

	return 0;

disable_pci_access:
	hl_fw_send_pci_access_msg(hdev, ARMCP_PACKET_DISABLE_PCI_ACCESS);

	return rc;
}

/*
 * goya_late_fini - GOYA late tear-down code
 *
 * @hdev: pointer to hl_device structure
 *
 * Free sensors allocated structures
 */
void goya_late_fini(struct hl_device *hdev)
{
	const struct hwmon_channel_info **channel_info_arr;
	int i = 0;

	if (!hdev->hl_chip_info->info)
		return;

	channel_info_arr = hdev->hl_chip_info->info;

	while (channel_info_arr[i]) {
		kfree(channel_info_arr[i]->config);
		kfree(channel_info_arr[i]);
		i++;
	}

	kfree(channel_info_arr);

	hdev->hl_chip_info->info = NULL;
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

	goya->mme_clk = GOYA_PLL_FREQ_LOW;
	goya->tpc_clk = GOYA_PLL_FREQ_LOW;
	goya->ic_clk = GOYA_PLL_FREQ_LOW;

	goya->mmu_prepare_reg = goya_mmu_prepare_reg;
	goya->qman0_set_security = goya_qman0_set_security;

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
					HL_CPU_ACCESSIBLE_MEM_SIZE,
					&hdev->cpu_accessible_dma_address,
					GFP_KERNEL | __GFP_ZERO);

	if (!hdev->cpu_accessible_dma_mem) {
		rc = -ENOMEM;
		goto free_dma_pool;
	}

	hdev->cpu_accessible_dma_pool = gen_pool_create(HL_CPU_PKT_SHIFT, -1);
	if (!hdev->cpu_accessible_dma_pool) {
		dev_err(hdev->dev,
			"Failed to create CPU accessible DMA pool\n");
		rc = -ENOMEM;
		goto free_cpu_pq_dma_mem;
	}

	rc = gen_pool_add(hdev->cpu_accessible_dma_pool,
				(uintptr_t) hdev->cpu_accessible_dma_mem,
				HL_CPU_ACCESSIBLE_MEM_SIZE, -1);
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
	hdev->asic_funcs->dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE,
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
static int goya_sw_fini(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;

	gen_pool_destroy(hdev->cpu_accessible_dma_pool);

	hdev->asic_funcs->dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE,
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

	if (goya->hw_cap_initialized & HW_CAP_MMU)
		WREG32(mmDMA_QM_0_GLBL_PROT + reg_off, QMAN_DMA_PARTLY_TRUSTED);
	else
		WREG32(mmDMA_QM_0_GLBL_PROT + reg_off, QMAN_DMA_FULLY_TRUSTED);

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

	if (dma_id)
		sob_addr = CFG_BASE + mmSYNC_MNGR_SOB_OBJ_1000 +
				(dma_id - 1) * 4;
	else
		sob_addr = CFG_BASE + mmSYNC_MNGR_SOB_OBJ_1007;

	WREG32(mmDMA_CH_0_WR_COMP_ADDR_LO + reg_off, lower_32_bits(sob_addr));
	WREG32(mmDMA_CH_0_WR_COMP_ADDR_HI + reg_off, upper_32_bits(sob_addr));
	WREG32(mmDMA_CH_0_WR_COMP_WDATA + reg_off, 0x80000001);
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

/*
 * goya_init_cpu_queues - Initialize PQ/CQ/EQ of CPU
 *
 * @hdev: pointer to hl_device structure
 *
 * Returns 0 on success
 *
 */
static int goya_init_cpu_queues(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	struct hl_eq *eq;
	dma_addr_t bus_address;
	u32 status;
	struct hl_hw_queue *cpu_pq = &hdev->kernel_queues[GOYA_QUEUE_ID_CPU_PQ];
	int err;

	if (!hdev->cpu_queues_enable)
		return 0;

	if (goya->hw_cap_initialized & HW_CAP_CPU_Q)
		return 0;

	eq = &hdev->event_queue;

	bus_address = cpu_pq->bus_address +
			hdev->asic_prop.host_phys_base_address;
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_0, lower_32_bits(bus_address));
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_1, upper_32_bits(bus_address));

	bus_address = eq->bus_address + hdev->asic_prop.host_phys_base_address;
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_2, lower_32_bits(bus_address));
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_3, upper_32_bits(bus_address));

	bus_address = hdev->cpu_accessible_dma_address +
			hdev->asic_prop.host_phys_base_address;
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_8, lower_32_bits(bus_address));
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_9, upper_32_bits(bus_address));

	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_5, HL_QUEUE_SIZE_IN_BYTES);
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_4, HL_EQ_SIZE_IN_BYTES);
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_10, HL_CPU_ACCESSIBLE_MEM_SIZE);

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

	WREG32(mmTPC_PLL_CLK_RLX_0, 0x200020);

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

static void goya_dma_stall(struct hl_device *hdev)
{
	WREG32(mmDMA_QM_0_GLBL_CFG1, 1 << DMA_QM_0_GLBL_CFG1_DMA_STOP_SHIFT);
	WREG32(mmDMA_QM_1_GLBL_CFG1, 1 << DMA_QM_1_GLBL_CFG1_DMA_STOP_SHIFT);
	WREG32(mmDMA_QM_2_GLBL_CFG1, 1 << DMA_QM_2_GLBL_CFG1_DMA_STOP_SHIFT);
	WREG32(mmDMA_QM_3_GLBL_CFG1, 1 << DMA_QM_3_GLBL_CFG1_DMA_STOP_SHIFT);
	WREG32(mmDMA_QM_4_GLBL_CFG1, 1 << DMA_QM_4_GLBL_CFG1_DMA_STOP_SHIFT);
}

static void goya_tpc_stall(struct hl_device *hdev)
{
	WREG32(mmTPC0_CFG_TPC_STALL, 1 << TPC0_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC1_CFG_TPC_STALL, 1 << TPC1_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC2_CFG_TPC_STALL, 1 << TPC2_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC3_CFG_TPC_STALL, 1 << TPC3_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC4_CFG_TPC_STALL, 1 << TPC4_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC5_CFG_TPC_STALL, 1 << TPC5_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC6_CFG_TPC_STALL, 1 << TPC6_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC7_CFG_TPC_STALL, 1 << TPC7_CFG_TPC_STALL_V_SHIFT);
}

static void goya_mme_stall(struct hl_device *hdev)
{
	WREG32(mmMME_STALL, 0xFFFFFFFF);
}

static int goya_enable_msix(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	int cq_cnt = hdev->asic_prop.completion_queues_count;
	int rc, i, irq_cnt_init, irq;

	if (goya->hw_cap_initialized & HW_CAP_MSIX)
		return 0;

	rc = pci_alloc_irq_vectors(hdev->pdev, GOYA_MSIX_ENTRIES,
				GOYA_MSIX_ENTRIES, PCI_IRQ_MSIX);
	if (rc < 0) {
		dev_err(hdev->dev,
			"MSI-X: Failed to enable support -- %d/%d\n",
			GOYA_MSIX_ENTRIES, rc);
		return rc;
	}

	for (i = 0, irq_cnt_init = 0 ; i < cq_cnt ; i++, irq_cnt_init++) {
		irq = pci_irq_vector(hdev->pdev, i);
		rc = request_irq(irq, hl_irq_handler_cq, 0, goya_irq_name[i],
				&hdev->completion_queue[i]);
		if (rc) {
			dev_err(hdev->dev, "Failed to request IRQ %d", irq);
			goto free_irqs;
		}
	}

	irq = pci_irq_vector(hdev->pdev, GOYA_EVENT_QUEUE_MSIX_IDX);

	rc = request_irq(irq, hl_irq_handler_eq, 0,
			goya_irq_name[GOYA_EVENT_QUEUE_MSIX_IDX],
			&hdev->event_queue);
	if (rc) {
		dev_err(hdev->dev, "Failed to request IRQ %d", irq);
		goto free_irqs;
	}

	goya->hw_cap_initialized |= HW_CAP_MSIX;
	return 0;

free_irqs:
	for (i = 0 ; i < irq_cnt_init ; i++)
		free_irq(pci_irq_vector(hdev->pdev, i),
			&hdev->completion_queue[i]);

	pci_free_irq_vectors(hdev->pdev);
	return rc;
}

static void goya_sync_irqs(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	int i;

	if (!(goya->hw_cap_initialized & HW_CAP_MSIX))
		return;

	/* Wait for all pending IRQs to be finished */
	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		synchronize_irq(pci_irq_vector(hdev->pdev, i));

	synchronize_irq(pci_irq_vector(hdev->pdev, GOYA_EVENT_QUEUE_MSIX_IDX));
}

static void goya_disable_msix(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	int i, irq;

	if (!(goya->hw_cap_initialized & HW_CAP_MSIX))
		return;

	goya_sync_irqs(hdev);

	irq = pci_irq_vector(hdev->pdev, GOYA_EVENT_QUEUE_MSIX_IDX);
	free_irq(irq, &hdev->event_queue);

	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++) {
		irq = pci_irq_vector(hdev->pdev, i);
		free_irq(irq, &hdev->completion_queue[i]);
	}

	pci_free_irq_vectors(hdev->pdev);

	goya->hw_cap_initialized &= ~HW_CAP_MSIX;
}

static void goya_halt_engines(struct hl_device *hdev, bool hard_reset)
{
	u32 wait_timeout_ms, cpu_timeout_ms;

	dev_info(hdev->dev,
		"Halting compute engines and disabling interrupts\n");

	if (hdev->pldm) {
		wait_timeout_ms = GOYA_PLDM_RESET_WAIT_MSEC;
		cpu_timeout_ms = GOYA_PLDM_RESET_WAIT_MSEC;
	} else {
		wait_timeout_ms = GOYA_RESET_WAIT_MSEC;
		cpu_timeout_ms = GOYA_CPU_RESET_WAIT_MSEC;
	}

	if (hard_reset) {
		/*
		 * I don't know what is the state of the CPU so make sure it is
		 * stopped in any means necessary
		 */
		WREG32(mmPSOC_GLOBAL_CONF_UBOOT_MAGIC, KMD_MSG_GOTO_WFE);
		WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR,
			GOYA_ASYNC_EVENT_ID_HALT_MACHINE);
		msleep(cpu_timeout_ms);
	}

	goya_stop_external_queues(hdev);
	goya_stop_internal_queues(hdev);

	msleep(wait_timeout_ms);

	goya_dma_stall(hdev);
	goya_tpc_stall(hdev);
	goya_mme_stall(hdev);

	msleep(wait_timeout_ms);

	goya_disable_external_queues(hdev);
	goya_disable_internal_queues(hdev);

	if (hard_reset)
		goya_disable_msix(hdev);
	else
		goya_sync_irqs(hdev);
}

/*
 * goya_push_uboot_to_device() - Push u-boot FW code to device.
 * @hdev: Pointer to hl_device structure.
 *
 * Copy u-boot fw code from firmware file to SRAM BAR.
 *
 * Return: 0 on success, non-zero for failure.
 */
static int goya_push_uboot_to_device(struct hl_device *hdev)
{
	char fw_name[200];
	void __iomem *dst;

	snprintf(fw_name, sizeof(fw_name), "habanalabs/goya/goya-u-boot.bin");
	dst = hdev->pcie_bar[SRAM_CFG_BAR_ID] + UBOOT_FW_OFFSET;

	return hl_fw_push_fw_to_device(hdev, fw_name, dst);
}

/*
 * goya_push_linux_to_device() - Push LINUX FW code to device.
 * @hdev: Pointer to hl_device structure.
 *
 * Copy LINUX fw code from firmware file to HBM BAR.
 *
 * Return: 0 on success, non-zero for failure.
 */
static int goya_push_linux_to_device(struct hl_device *hdev)
{
	char fw_name[200];
	void __iomem *dst;

	snprintf(fw_name, sizeof(fw_name), "habanalabs/goya/goya-fit.itb");
	dst = hdev->pcie_bar[DDR_BAR_ID] + LINUX_FW_OFFSET;

	return hl_fw_push_fw_to_device(hdev, fw_name, dst);
}

static int goya_pldm_init_cpu(struct hl_device *hdev)
{
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

	rc = goya_push_uboot_to_device(hdev);
	if (rc)
		return rc;

	rc = goya_push_linux_to_device(hdev);
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
		case CPU_BOOT_STATUS_UBOOT_NOT_READY:
			dev_err(hdev->dev,
				"ARM status %d - u-boot stopped by user\n",
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

	rc = goya_push_linux_to_device(hdev);
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

static int goya_mmu_update_asid_hop0_addr(struct hl_device *hdev, u32 asid,
						u64 phys_addr)
{
	u32 status, timeout_usec;
	int rc;

	if (hdev->pldm)
		timeout_usec = GOYA_PLDM_MMU_TIMEOUT_USEC;
	else
		timeout_usec = MMU_CONFIG_TIMEOUT_USEC;

	WREG32(MMU_HOP0_PA43_12, phys_addr >> MMU_HOP0_PA43_12_SHIFT);
	WREG32(MMU_HOP0_PA49_44, phys_addr >> MMU_HOP0_PA49_44_SHIFT);
	WREG32(MMU_ASID_BUSY, 0x80000000 | asid);

	rc = hl_poll_timeout(
		hdev,
		MMU_ASID_BUSY,
		status,
		!(status & 0x80000000),
		1000,
		timeout_usec);

	if (rc) {
		dev_err(hdev->dev,
			"Timeout during MMU hop0 config of asid %d\n", asid);
		return rc;
	}

	return 0;
}

static int goya_mmu_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct goya_device *goya = hdev->asic_specific;
	u64 hop0_addr;
	int rc, i;

	if (!hdev->mmu_enable)
		return 0;

	if (goya->hw_cap_initialized & HW_CAP_MMU)
		return 0;

	hdev->dram_supports_virtual_memory = true;
	hdev->dram_default_page_mapping = true;

	for (i = 0 ; i < prop->max_asid ; i++) {
		hop0_addr = prop->mmu_pgt_addr +
				(i * prop->mmu_hop_table_size);

		rc = goya_mmu_update_asid_hop0_addr(hdev, i, hop0_addr);
		if (rc) {
			dev_err(hdev->dev,
				"failed to set hop0 addr for asid %d\n", i);
			goto err;
		}
	}

	goya->hw_cap_initialized |= HW_CAP_MMU;

	/* init MMU cache manage page */
	WREG32(mmSTLB_CACHE_INV_BASE_39_8,
				lower_32_bits(MMU_CACHE_MNG_ADDR >> 8));
	WREG32(mmSTLB_CACHE_INV_BASE_49_40, MMU_CACHE_MNG_ADDR >> 40);

	/* Remove follower feature due to performance bug */
	WREG32_AND(mmSTLB_STLB_FEATURE_EN,
			(~STLB_STLB_FEATURE_EN_FOLLOWER_EN_MASK));

	hdev->asic_funcs->mmu_invalidate_cache(hdev, true);

	WREG32(mmMMU_MMU_ENABLE, 1);
	WREG32(mmMMU_SPI_MASK, 0xF);

	return 0;

err:
	return rc;
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

	/*
	 * Let's mark in the H/W that we have reached this point. We check
	 * this value in the reset_before_init function to understand whether
	 * we need to reset the chip before doing H/W init. This register is
	 * cleared by the H/W upon H/W reset
	 */
	WREG32(mmPSOC_GLOBAL_CONF_APP_STATUS, HL_DEVICE_HW_STATE_DIRTY);

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

	rc = goya_mmu_init(hdev);
	if (rc)
		return rc;

	goya_init_security(hdev);

	goya_init_dma_qmans(hdev);

	goya_init_mme_qmans(hdev);

	goya_init_tpc_qmans(hdev);

	/* MSI-X must be enabled before CPU queues are initialized */
	rc = goya_enable_msix(hdev);
	if (rc)
		goto disable_queues;

	rc = goya_init_cpu_queues(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU H/W queues %d\n",
			rc);
		goto disable_msix;
	}

	/*
	 * Check if we managed to set the DMA mask to more then 32 bits. If so,
	 * let's try to increase it again because in Goya we set the initial
	 * dma mask to less then 39 bits so that the allocation of the memory
	 * area for the device's cpu will be under 39 bits
	 */
	if (hdev->dma_mask > 32) {
		rc = hl_pci_set_dma_mask(hdev, 48);
		if (rc)
			goto disable_pci_access;
	}

	/* Perform read from the device to flush all MSI-X configuration */
	val = RREG32(mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG);

	return 0;

disable_pci_access:
	hl_fw_send_pci_access_msg(hdev, ARMCP_PACKET_DISABLE_PCI_ACCESS);
disable_msix:
	goya_disable_msix(hdev);
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

	if (!hard_reset) {
		goya->hw_cap_initialized &= ~(HW_CAP_DMA | HW_CAP_MME |
						HW_CAP_GOLDEN | HW_CAP_TPC);
		WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR,
				GOYA_ASYNC_EVENT_ID_SOFT_RESET);
		return;
	}

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
	memset(goya->events_stat, 0, sizeof(goya->events_stat));

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

	rc = hl_fw_send_pci_access_msg(hdev, ARMCP_PACKET_DISABLE_PCI_ACCESS);
	if (rc)
		dev_err(hdev->dev, "Failed to disable PCI access from CPU\n");

	return rc;
}

int goya_resume(struct hl_device *hdev)
{
	return goya_init_iatu(hdev);
}

static int goya_cb_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
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

static void goya_ring_doorbell(struct hl_device *hdev, u32 hw_queue_id, u32 pi)
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

static void *goya_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	return dma_alloc_coherent(&hdev->pdev->dev, size, dma_handle, flags);
}

static void goya_dma_free_coherent(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle)
{
	dma_free_coherent(&hdev->pdev->dev, size, cpu_addr, dma_handle);
}

void *goya_get_int_queue_base(struct hl_device *hdev, u32 queue_id,
				dma_addr_t *dma_handle,	u16 *queue_len)
{
	void *base;
	u32 offset;

	*dma_handle = hdev->asic_prop.sram_base_address;

	base = (void *) hdev->pcie_bar[SRAM_CFG_BAR_ID];

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

static int goya_send_job_on_qman0(struct hl_device *hdev, struct hl_cs_job *job)
{
	struct goya_device *goya = hdev->asic_specific;
	struct packet_msg_prot *fence_pkt;
	u32 *fence_ptr;
	dma_addr_t fence_dma_addr;
	struct hl_cb *cb;
	u32 tmp, timeout;
	char buf[16] = {};
	int rc;

	if (hdev->pldm)
		timeout = GOYA_PLDM_QMAN0_TIMEOUT_USEC;
	else
		timeout = HL_DEVICE_TIMEOUT_USEC;

	if (!hdev->asic_funcs->is_device_idle(hdev, buf, sizeof(buf))) {
		dev_err_ratelimited(hdev->dev,
			"Can't send KMD job on QMAN0 because %s is busy\n",
			buf);
		return -EBUSY;
	}

	fence_ptr = hdev->asic_funcs->dma_pool_zalloc(hdev, 4, GFP_KERNEL,
							&fence_dma_addr);
	if (!fence_ptr) {
		dev_err(hdev->dev,
			"Failed to allocate fence memory for QMAN0\n");
		return -ENOMEM;
	}

	*fence_ptr = 0;

	goya->qman0_set_security(hdev, true);

	/*
	 * goya cs parser saves space for 2xpacket_msg_prot at end of CB. For
	 * synchronized kernel jobs we only need space for 1 packet_msg_prot
	 */
	job->job_cb_size -= sizeof(struct packet_msg_prot);

	cb = job->patched_cb;

	fence_pkt = (struct packet_msg_prot *) (uintptr_t) (cb->kernel_address +
			job->job_cb_size - sizeof(struct packet_msg_prot));

	tmp = (PACKET_MSG_PROT << GOYA_PKT_CTL_OPCODE_SHIFT) |
			(1 << GOYA_PKT_CTL_EB_SHIFT) |
			(1 << GOYA_PKT_CTL_MB_SHIFT);
	fence_pkt->ctl = cpu_to_le32(tmp);
	fence_pkt->value = cpu_to_le32(GOYA_QMAN0_FENCE_VAL);
	fence_pkt->addr = cpu_to_le64(fence_dma_addr +
					hdev->asic_prop.host_phys_base_address);

	rc = hl_hw_queue_send_cb_no_cmpl(hdev, GOYA_QUEUE_ID_DMA_0,
					job->job_cb_size, cb->bus_address);
	if (rc) {
		dev_err(hdev->dev, "Failed to send CB on QMAN0, %d\n", rc);
		goto free_fence_ptr;
	}

	rc = hl_poll_timeout_memory(hdev, (u64) (uintptr_t) fence_ptr, timeout,
					&tmp);

	hl_hw_queue_inc_ci_kernel(hdev, GOYA_QUEUE_ID_DMA_0);

	if ((rc) || (tmp != GOYA_QMAN0_FENCE_VAL)) {
		dev_err(hdev->dev, "QMAN0 Job hasn't finished in time\n");
		rc = -ETIMEDOUT;
	}

free_fence_ptr:
	hdev->asic_funcs->dma_pool_free(hdev, (void *) fence_ptr,
					fence_dma_addr);

	goya->qman0_set_security(hdev, false);

	return rc;
}

int goya_send_cpu_message(struct hl_device *hdev, u32 *msg, u16 len,
				u32 timeout, long *result)
{
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_CPU_Q)) {
		if (result)
			*result = 0;
		return 0;
	}

	return hl_fw_send_cpu_message(hdev, GOYA_QUEUE_ID_CPU_PQ, msg, len,
					timeout, result);
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

	tmp = (PACKET_MSG_PROT << GOYA_PKT_CTL_OPCODE_SHIFT) |
			(1 << GOYA_PKT_CTL_EB_SHIFT) |
			(1 << GOYA_PKT_CTL_MB_SHIFT);
	fence_pkt->ctl = cpu_to_le32(tmp);
	fence_pkt->value = cpu_to_le32(fence_val);
	fence_pkt->addr = cpu_to_le64(fence_dma_addr +
					hdev->asic_prop.host_phys_base_address);

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
	struct goya_device *goya = hdev->asic_specific;

	/*
	 * check capability here as send_cpu_message() won't update the result
	 * value if no capability
	 */
	if (!(goya->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	return hl_fw_test_cpu_queue(hdev);
}

int goya_test_queues(struct hl_device *hdev)
{
	int i, rc, ret_val = 0;

	for (i = 0 ; i < NUMBER_OF_EXT_HW_QUEUES ; i++) {
		rc = goya_test_queue(hdev, i);
		if (rc)
			ret_val = -EINVAL;
	}

	if (hdev->cpu_queues_enable) {
		rc = goya_test_cpu_queue(hdev);
		if (rc)
			ret_val = -EINVAL;
	}

	return ret_val;
}

static void *goya_dma_pool_zalloc(struct hl_device *hdev, size_t size,
					gfp_t mem_flags, dma_addr_t *dma_handle)
{
	if (size > GOYA_DMA_POOL_BLK_SIZE)
		return NULL;

	return dma_pool_zalloc(hdev->dma_pool, mem_flags, dma_handle);
}

static void goya_dma_pool_free(struct hl_device *hdev, void *vaddr,
				dma_addr_t dma_addr)
{
	dma_pool_free(hdev->dma_pool, vaddr, dma_addr);
}

void *goya_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle)
{
	return hl_fw_cpu_accessible_dma_pool_alloc(hdev, size, dma_handle);
}

void goya_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size,
					void *vaddr)
{
	hl_fw_cpu_accessible_dma_pool_free(hdev, size, vaddr);
}

static int goya_dma_map_sg(struct hl_device *hdev, struct scatterlist *sg,
				int nents, enum dma_data_direction dir)
{
	if (!dma_map_sg(&hdev->pdev->dev, sg, nents, dir))
		return -ENOMEM;

	return 0;
}

static void goya_dma_unmap_sg(struct hl_device *hdev, struct scatterlist *sg,
				int nents, enum dma_data_direction dir)
{
	dma_unmap_sg(&hdev->pdev->dev, sg, nents, dir);
}

u32 goya_get_dma_desc_list_size(struct hl_device *hdev, struct sg_table *sgt)
{
	struct scatterlist *sg, *sg_next_iter;
	u32 count, dma_desc_cnt;
	u64 len, len_next;
	dma_addr_t addr, addr_next;

	dma_desc_cnt = 0;

	for_each_sg(sgt->sgl, sg, sgt->nents, count) {

		len = sg_dma_len(sg);
		addr = sg_dma_address(sg);

		if (len == 0)
			break;

		while ((count + 1) < sgt->nents) {
			sg_next_iter = sg_next(sg);
			len_next = sg_dma_len(sg_next_iter);
			addr_next = sg_dma_address(sg_next_iter);

			if (len_next == 0)
				break;

			if ((addr + len == addr_next) &&
				(len + len_next <= DMA_MAX_TRANSFER_SIZE)) {
				len += len_next;
				count++;
				sg = sg_next_iter;
			} else {
				break;
			}
		}

		dma_desc_cnt++;
	}

	return dma_desc_cnt * sizeof(struct packet_lin_dma);
}

static int goya_pin_memory_before_cs(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt,
				u64 addr, enum dma_data_direction dir)
{
	struct hl_userptr *userptr;
	int rc;

	if (hl_userptr_is_pinned(hdev, addr, le32_to_cpu(user_dma_pkt->tsize),
			parser->job_userptr_list, &userptr))
		goto already_pinned;

	userptr = kzalloc(sizeof(*userptr), GFP_ATOMIC);
	if (!userptr)
		return -ENOMEM;

	rc = hl_pin_host_memory(hdev, addr, le32_to_cpu(user_dma_pkt->tsize),
				userptr);
	if (rc)
		goto free_userptr;

	list_add_tail(&userptr->job_node, parser->job_userptr_list);

	rc = hdev->asic_funcs->asic_dma_map_sg(hdev, userptr->sgt->sgl,
					userptr->sgt->nents, dir);
	if (rc) {
		dev_err(hdev->dev, "failed to map sgt with DMA region\n");
		goto unpin_memory;
	}

	userptr->dma_mapped = true;
	userptr->dir = dir;

already_pinned:
	parser->patched_cb_size +=
			goya_get_dma_desc_list_size(hdev, userptr->sgt);

	return 0;

unpin_memory:
	hl_unpin_host_memory(hdev, userptr);
free_userptr:
	kfree(userptr);
	return rc;
}

static int goya_validate_dma_pkt_host(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt)
{
	u64 device_memory_addr, addr;
	enum dma_data_direction dir;
	enum goya_dma_direction user_dir;
	bool sram_addr = true;
	bool skip_host_mem_pin = false;
	bool user_memset;
	u32 ctl;
	int rc = 0;

	ctl = le32_to_cpu(user_dma_pkt->ctl);

	user_dir = (ctl & GOYA_PKT_LIN_DMA_CTL_DMA_DIR_MASK) >>
			GOYA_PKT_LIN_DMA_CTL_DMA_DIR_SHIFT;

	user_memset = (ctl & GOYA_PKT_LIN_DMA_CTL_MEMSET_MASK) >>
			GOYA_PKT_LIN_DMA_CTL_MEMSET_SHIFT;

	switch (user_dir) {
	case DMA_HOST_TO_DRAM:
		dev_dbg(hdev->dev, "DMA direction is HOST --> DRAM\n");
		dir = DMA_TO_DEVICE;
		sram_addr = false;
		addr = le64_to_cpu(user_dma_pkt->src_addr);
		device_memory_addr = le64_to_cpu(user_dma_pkt->dst_addr);
		if (user_memset)
			skip_host_mem_pin = true;
		break;

	case DMA_DRAM_TO_HOST:
		dev_dbg(hdev->dev, "DMA direction is DRAM --> HOST\n");
		dir = DMA_FROM_DEVICE;
		sram_addr = false;
		addr = le64_to_cpu(user_dma_pkt->dst_addr);
		device_memory_addr = le64_to_cpu(user_dma_pkt->src_addr);
		break;

	case DMA_HOST_TO_SRAM:
		dev_dbg(hdev->dev, "DMA direction is HOST --> SRAM\n");
		dir = DMA_TO_DEVICE;
		addr = le64_to_cpu(user_dma_pkt->src_addr);
		device_memory_addr = le64_to_cpu(user_dma_pkt->dst_addr);
		if (user_memset)
			skip_host_mem_pin = true;
		break;

	case DMA_SRAM_TO_HOST:
		dev_dbg(hdev->dev, "DMA direction is SRAM --> HOST\n");
		dir = DMA_FROM_DEVICE;
		addr = le64_to_cpu(user_dma_pkt->dst_addr);
		device_memory_addr = le64_to_cpu(user_dma_pkt->src_addr);
		break;
	default:
		dev_err(hdev->dev, "DMA direction is undefined\n");
		return -EFAULT;
	}

	if (parser->ctx_id != HL_KERNEL_ASID_ID) {
		if (sram_addr) {
			if (!hl_mem_area_inside_range(device_memory_addr,
					le32_to_cpu(user_dma_pkt->tsize),
					hdev->asic_prop.sram_user_base_address,
					hdev->asic_prop.sram_end_address)) {

				dev_err(hdev->dev,
					"SRAM address 0x%llx + 0x%x is invalid\n",
					device_memory_addr,
					user_dma_pkt->tsize);
				return -EFAULT;
			}
		} else {
			if (!hl_mem_area_inside_range(device_memory_addr,
					le32_to_cpu(user_dma_pkt->tsize),
					hdev->asic_prop.dram_user_base_address,
					hdev->asic_prop.dram_end_address)) {

				dev_err(hdev->dev,
					"DRAM address 0x%llx + 0x%x is invalid\n",
					device_memory_addr,
					user_dma_pkt->tsize);
				return -EFAULT;
			}
		}
	}

	if (skip_host_mem_pin)
		parser->patched_cb_size += sizeof(*user_dma_pkt);
	else {
		if ((dir == DMA_TO_DEVICE) &&
				(parser->hw_queue_id > GOYA_QUEUE_ID_DMA_1)) {
			dev_err(hdev->dev,
				"Can't DMA from host on queue other then 1\n");
			return -EFAULT;
		}

		rc = goya_pin_memory_before_cs(hdev, parser, user_dma_pkt,
						addr, dir);
	}

	return rc;
}

static int goya_validate_dma_pkt_no_host(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt)
{
	u64 sram_memory_addr, dram_memory_addr;
	enum goya_dma_direction user_dir;
	u32 ctl;

	ctl = le32_to_cpu(user_dma_pkt->ctl);
	user_dir = (ctl & GOYA_PKT_LIN_DMA_CTL_DMA_DIR_MASK) >>
			GOYA_PKT_LIN_DMA_CTL_DMA_DIR_SHIFT;

	if (user_dir == DMA_DRAM_TO_SRAM) {
		dev_dbg(hdev->dev, "DMA direction is DRAM --> SRAM\n");
		dram_memory_addr = le64_to_cpu(user_dma_pkt->src_addr);
		sram_memory_addr = le64_to_cpu(user_dma_pkt->dst_addr);
	} else {
		dev_dbg(hdev->dev, "DMA direction is SRAM --> DRAM\n");
		sram_memory_addr = le64_to_cpu(user_dma_pkt->src_addr);
		dram_memory_addr = le64_to_cpu(user_dma_pkt->dst_addr);
	}

	if (!hl_mem_area_inside_range(sram_memory_addr,
				le32_to_cpu(user_dma_pkt->tsize),
				hdev->asic_prop.sram_user_base_address,
				hdev->asic_prop.sram_end_address)) {
		dev_err(hdev->dev, "SRAM address 0x%llx + 0x%x is invalid\n",
			sram_memory_addr, user_dma_pkt->tsize);
		return -EFAULT;
	}

	if (!hl_mem_area_inside_range(dram_memory_addr,
				le32_to_cpu(user_dma_pkt->tsize),
				hdev->asic_prop.dram_user_base_address,
				hdev->asic_prop.dram_end_address)) {
		dev_err(hdev->dev, "DRAM address 0x%llx + 0x%x is invalid\n",
			dram_memory_addr, user_dma_pkt->tsize);
		return -EFAULT;
	}

	parser->patched_cb_size += sizeof(*user_dma_pkt);

	return 0;
}

static int goya_validate_dma_pkt_no_mmu(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt)
{
	enum goya_dma_direction user_dir;
	u32 ctl;
	int rc;

	dev_dbg(hdev->dev, "DMA packet details:\n");
	dev_dbg(hdev->dev, "source == 0x%llx\n", user_dma_pkt->src_addr);
	dev_dbg(hdev->dev, "destination == 0x%llx\n", user_dma_pkt->dst_addr);
	dev_dbg(hdev->dev, "size == %u\n", user_dma_pkt->tsize);

	ctl = le32_to_cpu(user_dma_pkt->ctl);
	user_dir = (ctl & GOYA_PKT_LIN_DMA_CTL_DMA_DIR_MASK) >>
			GOYA_PKT_LIN_DMA_CTL_DMA_DIR_SHIFT;

	/*
	 * Special handling for DMA with size 0. The H/W has a bug where
	 * this can cause the QMAN DMA to get stuck, so block it here.
	 */
	if (user_dma_pkt->tsize == 0) {
		dev_err(hdev->dev,
			"Got DMA with size 0, might reset the device\n");
		return -EINVAL;
	}

	if ((user_dir == DMA_DRAM_TO_SRAM) || (user_dir == DMA_SRAM_TO_DRAM))
		rc = goya_validate_dma_pkt_no_host(hdev, parser, user_dma_pkt);
	else
		rc = goya_validate_dma_pkt_host(hdev, parser, user_dma_pkt);

	return rc;
}

static int goya_validate_dma_pkt_mmu(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt)
{
	dev_dbg(hdev->dev, "DMA packet details:\n");
	dev_dbg(hdev->dev, "source == 0x%llx\n", user_dma_pkt->src_addr);
	dev_dbg(hdev->dev, "destination == 0x%llx\n", user_dma_pkt->dst_addr);
	dev_dbg(hdev->dev, "size == %u\n", user_dma_pkt->tsize);

	/*
	 * WA for HW-23.
	 * We can't allow user to read from Host using QMANs other than 1.
	 */
	if (parser->hw_queue_id > GOYA_QUEUE_ID_DMA_1 &&
		hl_mem_area_inside_range(le64_to_cpu(user_dma_pkt->src_addr),
				le32_to_cpu(user_dma_pkt->tsize),
				hdev->asic_prop.va_space_host_start_address,
				hdev->asic_prop.va_space_host_end_address)) {
		dev_err(hdev->dev,
			"Can't DMA from host on queue other then 1\n");
		return -EFAULT;
	}

	if (user_dma_pkt->tsize == 0) {
		dev_err(hdev->dev,
			"Got DMA with size 0, might reset the device\n");
		return -EINVAL;
	}

	parser->patched_cb_size += sizeof(*user_dma_pkt);

	return 0;
}

static int goya_validate_wreg32(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_wreg32 *wreg_pkt)
{
	struct goya_device *goya = hdev->asic_specific;
	u32 sob_start_addr, sob_end_addr;
	u16 reg_offset;

	reg_offset = le32_to_cpu(wreg_pkt->ctl) &
			GOYA_PKT_WREG32_CTL_REG_OFFSET_MASK;

	dev_dbg(hdev->dev, "WREG32 packet details:\n");
	dev_dbg(hdev->dev, "reg_offset == 0x%x\n", reg_offset);
	dev_dbg(hdev->dev, "value      == 0x%x\n", wreg_pkt->value);

	if (reg_offset != (mmDMA_CH_0_WR_COMP_ADDR_LO & 0x1FFF)) {
		dev_err(hdev->dev, "WREG32 packet with illegal address 0x%x\n",
			reg_offset);
		return -EPERM;
	}

	/*
	 * With MMU, DMA channels are not secured, so it doesn't matter where
	 * the WR COMP will be written to because it will go out with
	 * non-secured property
	 */
	if (goya->hw_cap_initialized & HW_CAP_MMU)
		return 0;

	sob_start_addr = lower_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);
	sob_end_addr = lower_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_1023);

	if ((le32_to_cpu(wreg_pkt->value) < sob_start_addr) ||
			(le32_to_cpu(wreg_pkt->value) > sob_end_addr)) {

		dev_err(hdev->dev, "WREG32 packet with illegal value 0x%x\n",
			wreg_pkt->value);
		return -EPERM;
	}

	return 0;
}

static int goya_validate_cb(struct hl_device *hdev,
			struct hl_cs_parser *parser, bool is_mmu)
{
	u32 cb_parsed_length = 0;
	int rc = 0;

	parser->patched_cb_size = 0;

	/* cb_user_size is more than 0 so loop will always be executed */
	while (cb_parsed_length < parser->user_cb_size) {
		enum packet_id pkt_id;
		u16 pkt_size;
		void *user_pkt;

		user_pkt = (void *) (uintptr_t)
			(parser->user_cb->kernel_address + cb_parsed_length);

		pkt_id = (enum packet_id) (((*(u64 *) user_pkt) &
				PACKET_HEADER_PACKET_ID_MASK) >>
					PACKET_HEADER_PACKET_ID_SHIFT);

		pkt_size = goya_packet_sizes[pkt_id];
		cb_parsed_length += pkt_size;
		if (cb_parsed_length > parser->user_cb_size) {
			dev_err(hdev->dev,
				"packet 0x%x is out of CB boundary\n", pkt_id);
			rc = -EINVAL;
			break;
		}

		switch (pkt_id) {
		case PACKET_WREG_32:
			/*
			 * Although it is validated after copy in patch_cb(),
			 * need to validate here as well because patch_cb() is
			 * not called in MMU path while this function is called
			 */
			rc = goya_validate_wreg32(hdev, parser, user_pkt);
			break;

		case PACKET_WREG_BULK:
			dev_err(hdev->dev,
				"User not allowed to use WREG_BULK\n");
			rc = -EPERM;
			break;

		case PACKET_MSG_PROT:
			dev_err(hdev->dev,
				"User not allowed to use MSG_PROT\n");
			rc = -EPERM;
			break;

		case PACKET_CP_DMA:
			dev_err(hdev->dev, "User not allowed to use CP_DMA\n");
			rc = -EPERM;
			break;

		case PACKET_STOP:
			dev_err(hdev->dev, "User not allowed to use STOP\n");
			rc = -EPERM;
			break;

		case PACKET_LIN_DMA:
			if (is_mmu)
				rc = goya_validate_dma_pkt_mmu(hdev, parser,
						user_pkt);
			else
				rc = goya_validate_dma_pkt_no_mmu(hdev, parser,
						user_pkt);
			break;

		case PACKET_MSG_LONG:
		case PACKET_MSG_SHORT:
		case PACKET_FENCE:
		case PACKET_NOP:
			parser->patched_cb_size += pkt_size;
			break;

		default:
			dev_err(hdev->dev, "Invalid packet header 0x%x\n",
				pkt_id);
			rc = -EINVAL;
			break;
		}

		if (rc)
			break;
	}

	/*
	 * The new CB should have space at the end for two MSG_PROT packets:
	 * 1. A packet that will act as a completion packet
	 * 2. A packet that will generate MSI-X interrupt
	 */
	parser->patched_cb_size += sizeof(struct packet_msg_prot) * 2;

	return rc;
}

static int goya_patch_dma_packet(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt,
				struct packet_lin_dma *new_dma_pkt,
				u32 *new_dma_pkt_size)
{
	struct hl_userptr *userptr;
	struct scatterlist *sg, *sg_next_iter;
	u32 count, dma_desc_cnt;
	u64 len, len_next;
	dma_addr_t dma_addr, dma_addr_next;
	enum goya_dma_direction user_dir;
	u64 device_memory_addr, addr;
	enum dma_data_direction dir;
	struct sg_table *sgt;
	bool skip_host_mem_pin = false;
	bool user_memset;
	u32 user_rdcomp_mask, user_wrcomp_mask, ctl;

	ctl = le32_to_cpu(user_dma_pkt->ctl);

	user_dir = (ctl & GOYA_PKT_LIN_DMA_CTL_DMA_DIR_MASK) >>
			GOYA_PKT_LIN_DMA_CTL_DMA_DIR_SHIFT;

	user_memset = (ctl & GOYA_PKT_LIN_DMA_CTL_MEMSET_MASK) >>
			GOYA_PKT_LIN_DMA_CTL_MEMSET_SHIFT;

	if ((user_dir == DMA_DRAM_TO_SRAM) || (user_dir == DMA_SRAM_TO_DRAM) ||
			(user_dma_pkt->tsize == 0)) {
		memcpy(new_dma_pkt, user_dma_pkt, sizeof(*new_dma_pkt));
		*new_dma_pkt_size = sizeof(*new_dma_pkt);
		return 0;
	}

	if ((user_dir == DMA_HOST_TO_DRAM) || (user_dir == DMA_HOST_TO_SRAM)) {
		addr = le64_to_cpu(user_dma_pkt->src_addr);
		device_memory_addr = le64_to_cpu(user_dma_pkt->dst_addr);
		dir = DMA_TO_DEVICE;
		if (user_memset)
			skip_host_mem_pin = true;
	} else {
		addr = le64_to_cpu(user_dma_pkt->dst_addr);
		device_memory_addr = le64_to_cpu(user_dma_pkt->src_addr);
		dir = DMA_FROM_DEVICE;
	}

	if ((!skip_host_mem_pin) &&
		(hl_userptr_is_pinned(hdev, addr,
			le32_to_cpu(user_dma_pkt->tsize),
			parser->job_userptr_list, &userptr) == false)) {
		dev_err(hdev->dev, "Userptr 0x%llx + 0x%x NOT mapped\n",
				addr, user_dma_pkt->tsize);
		return -EFAULT;
	}

	if ((user_memset) && (dir == DMA_TO_DEVICE)) {
		memcpy(new_dma_pkt, user_dma_pkt, sizeof(*user_dma_pkt));
		*new_dma_pkt_size = sizeof(*user_dma_pkt);
		return 0;
	}

	user_rdcomp_mask = ctl & GOYA_PKT_LIN_DMA_CTL_RDCOMP_MASK;

	user_wrcomp_mask = ctl & GOYA_PKT_LIN_DMA_CTL_WRCOMP_MASK;

	sgt = userptr->sgt;
	dma_desc_cnt = 0;

	for_each_sg(sgt->sgl, sg, sgt->nents, count) {
		len = sg_dma_len(sg);
		dma_addr = sg_dma_address(sg);

		if (len == 0)
			break;

		while ((count + 1) < sgt->nents) {
			sg_next_iter = sg_next(sg);
			len_next = sg_dma_len(sg_next_iter);
			dma_addr_next = sg_dma_address(sg_next_iter);

			if (len_next == 0)
				break;

			if ((dma_addr + len == dma_addr_next) &&
				(len + len_next <= DMA_MAX_TRANSFER_SIZE)) {
				len += len_next;
				count++;
				sg = sg_next_iter;
			} else {
				break;
			}
		}

		ctl = le32_to_cpu(user_dma_pkt->ctl);
		if (likely(dma_desc_cnt))
			ctl &= ~GOYA_PKT_CTL_EB_MASK;
		ctl &= ~(GOYA_PKT_LIN_DMA_CTL_RDCOMP_MASK |
				GOYA_PKT_LIN_DMA_CTL_WRCOMP_MASK);
		new_dma_pkt->ctl = cpu_to_le32(ctl);
		new_dma_pkt->tsize = cpu_to_le32((u32) len);

		dma_addr += hdev->asic_prop.host_phys_base_address;

		if (dir == DMA_TO_DEVICE) {
			new_dma_pkt->src_addr = cpu_to_le64(dma_addr);
			new_dma_pkt->dst_addr = cpu_to_le64(device_memory_addr);
		} else {
			new_dma_pkt->src_addr = cpu_to_le64(device_memory_addr);
			new_dma_pkt->dst_addr = cpu_to_le64(dma_addr);
		}

		if (!user_memset)
			device_memory_addr += len;
		dma_desc_cnt++;
		new_dma_pkt++;
	}

	if (!dma_desc_cnt) {
		dev_err(hdev->dev,
			"Error of 0 SG entries when patching DMA packet\n");
		return -EFAULT;
	}

	/* Fix the last dma packet - rdcomp/wrcomp must be as user set them */
	new_dma_pkt--;
	new_dma_pkt->ctl |= cpu_to_le32(user_rdcomp_mask | user_wrcomp_mask);

	*new_dma_pkt_size = dma_desc_cnt * sizeof(struct packet_lin_dma);

	return 0;
}

static int goya_patch_cb(struct hl_device *hdev,
				struct hl_cs_parser *parser)
{
	u32 cb_parsed_length = 0;
	u32 cb_patched_cur_length = 0;
	int rc = 0;

	/* cb_user_size is more than 0 so loop will always be executed */
	while (cb_parsed_length < parser->user_cb_size) {
		enum packet_id pkt_id;
		u16 pkt_size;
		u32 new_pkt_size = 0;
		void *user_pkt, *kernel_pkt;

		user_pkt = (void *) (uintptr_t)
			(parser->user_cb->kernel_address + cb_parsed_length);
		kernel_pkt = (void *) (uintptr_t)
			(parser->patched_cb->kernel_address +
					cb_patched_cur_length);

		pkt_id = (enum packet_id) (((*(u64 *) user_pkt) &
				PACKET_HEADER_PACKET_ID_MASK) >>
					PACKET_HEADER_PACKET_ID_SHIFT);

		pkt_size = goya_packet_sizes[pkt_id];
		cb_parsed_length += pkt_size;
		if (cb_parsed_length > parser->user_cb_size) {
			dev_err(hdev->dev,
				"packet 0x%x is out of CB boundary\n", pkt_id);
			rc = -EINVAL;
			break;
		}

		switch (pkt_id) {
		case PACKET_LIN_DMA:
			rc = goya_patch_dma_packet(hdev, parser, user_pkt,
						kernel_pkt, &new_pkt_size);
			cb_patched_cur_length += new_pkt_size;
			break;

		case PACKET_WREG_32:
			memcpy(kernel_pkt, user_pkt, pkt_size);
			cb_patched_cur_length += pkt_size;
			rc = goya_validate_wreg32(hdev, parser, kernel_pkt);
			break;

		case PACKET_WREG_BULK:
			dev_err(hdev->dev,
				"User not allowed to use WREG_BULK\n");
			rc = -EPERM;
			break;

		case PACKET_MSG_PROT:
			dev_err(hdev->dev,
				"User not allowed to use MSG_PROT\n");
			rc = -EPERM;
			break;

		case PACKET_CP_DMA:
			dev_err(hdev->dev, "User not allowed to use CP_DMA\n");
			rc = -EPERM;
			break;

		case PACKET_STOP:
			dev_err(hdev->dev, "User not allowed to use STOP\n");
			rc = -EPERM;
			break;

		case PACKET_MSG_LONG:
		case PACKET_MSG_SHORT:
		case PACKET_FENCE:
		case PACKET_NOP:
			memcpy(kernel_pkt, user_pkt, pkt_size);
			cb_patched_cur_length += pkt_size;
			break;

		default:
			dev_err(hdev->dev, "Invalid packet header 0x%x\n",
				pkt_id);
			rc = -EINVAL;
			break;
		}

		if (rc)
			break;
	}

	return rc;
}

static int goya_parse_cb_mmu(struct hl_device *hdev,
		struct hl_cs_parser *parser)
{
	u64 patched_cb_handle;
	u32 patched_cb_size;
	struct hl_cb *user_cb;
	int rc;

	/*
	 * The new CB should have space at the end for two MSG_PROT pkt:
	 * 1. A packet that will act as a completion packet
	 * 2. A packet that will generate MSI-X interrupt
	 */
	parser->patched_cb_size = parser->user_cb_size +
			sizeof(struct packet_msg_prot) * 2;

	rc = hl_cb_create(hdev, &hdev->kernel_cb_mgr,
				parser->patched_cb_size,
				&patched_cb_handle, HL_KERNEL_ASID_ID);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to allocate patched CB for DMA CS %d\n",
			rc);
		return rc;
	}

	patched_cb_handle >>= PAGE_SHIFT;
	parser->patched_cb = hl_cb_get(hdev, &hdev->kernel_cb_mgr,
				(u32) patched_cb_handle);
	/* hl_cb_get should never fail here so use kernel WARN */
	WARN(!parser->patched_cb, "DMA CB handle invalid 0x%x\n",
			(u32) patched_cb_handle);
	if (!parser->patched_cb) {
		rc = -EFAULT;
		goto out;
	}

	/*
	 * The check that parser->user_cb_size <= parser->user_cb->size was done
	 * in validate_queue_index().
	 */
	memcpy((void *) (uintptr_t) parser->patched_cb->kernel_address,
		(void *) (uintptr_t) parser->user_cb->kernel_address,
		parser->user_cb_size);

	patched_cb_size = parser->patched_cb_size;

	/* validate patched CB instead of user CB */
	user_cb = parser->user_cb;
	parser->user_cb = parser->patched_cb;
	rc = goya_validate_cb(hdev, parser, true);
	parser->user_cb = user_cb;

	if (rc) {
		hl_cb_put(parser->patched_cb);
		goto out;
	}

	if (patched_cb_size != parser->patched_cb_size) {
		dev_err(hdev->dev, "user CB size mismatch\n");
		hl_cb_put(parser->patched_cb);
		rc = -EINVAL;
		goto out;
	}

out:
	/*
	 * Always call cb destroy here because we still have 1 reference
	 * to it by calling cb_get earlier. After the job will be completed,
	 * cb_put will release it, but here we want to remove it from the
	 * idr
	 */
	hl_cb_destroy(hdev, &hdev->kernel_cb_mgr,
					patched_cb_handle << PAGE_SHIFT);

	return rc;
}

static int goya_parse_cb_no_mmu(struct hl_device *hdev,
				struct hl_cs_parser *parser)
{
	u64 patched_cb_handle;
	int rc;

	rc = goya_validate_cb(hdev, parser, false);

	if (rc)
		goto free_userptr;

	rc = hl_cb_create(hdev, &hdev->kernel_cb_mgr,
				parser->patched_cb_size,
				&patched_cb_handle, HL_KERNEL_ASID_ID);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to allocate patched CB for DMA CS %d\n", rc);
		goto free_userptr;
	}

	patched_cb_handle >>= PAGE_SHIFT;
	parser->patched_cb = hl_cb_get(hdev, &hdev->kernel_cb_mgr,
				(u32) patched_cb_handle);
	/* hl_cb_get should never fail here so use kernel WARN */
	WARN(!parser->patched_cb, "DMA CB handle invalid 0x%x\n",
			(u32) patched_cb_handle);
	if (!parser->patched_cb) {
		rc = -EFAULT;
		goto out;
	}

	rc = goya_patch_cb(hdev, parser);

	if (rc)
		hl_cb_put(parser->patched_cb);

out:
	/*
	 * Always call cb destroy here because we still have 1 reference
	 * to it by calling cb_get earlier. After the job will be completed,
	 * cb_put will release it, but here we want to remove it from the
	 * idr
	 */
	hl_cb_destroy(hdev, &hdev->kernel_cb_mgr,
				patched_cb_handle << PAGE_SHIFT);

free_userptr:
	if (rc)
		hl_userptr_delete_list(hdev, parser->job_userptr_list);
	return rc;
}

static int goya_parse_cb_no_ext_quque(struct hl_device *hdev,
					struct hl_cs_parser *parser)
{
	struct asic_fixed_properties *asic_prop = &hdev->asic_prop;
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU)) {
		/* For internal queue jobs, just check if cb address is valid */
		if (hl_mem_area_inside_range(
				(u64) (uintptr_t) parser->user_cb,
				parser->user_cb_size,
				asic_prop->sram_user_base_address,
				asic_prop->sram_end_address))
			return 0;

		if (hl_mem_area_inside_range(
				(u64) (uintptr_t) parser->user_cb,
				parser->user_cb_size,
				asic_prop->dram_user_base_address,
				asic_prop->dram_end_address))
			return 0;

		dev_err(hdev->dev,
			"Internal CB address %px + 0x%x is not in SRAM nor in DRAM\n",
			parser->user_cb, parser->user_cb_size);

		return -EFAULT;
	}

	return 0;
}

int goya_cs_parser(struct hl_device *hdev, struct hl_cs_parser *parser)
{
	struct goya_device *goya = hdev->asic_specific;

	if (!parser->ext_queue)
		return goya_parse_cb_no_ext_quque(hdev, parser);

	if ((goya->hw_cap_initialized & HW_CAP_MMU) && parser->use_virt_addr)
		return goya_parse_cb_mmu(hdev, parser);
	else
		return goya_parse_cb_no_mmu(hdev, parser);
}

void goya_add_end_of_cb_packets(u64 kernel_address, u32 len, u64 cq_addr,
				u32 cq_val, u32 msix_vec)
{
	struct packet_msg_prot *cq_pkt;
	u32 tmp;

	cq_pkt = (struct packet_msg_prot *) (uintptr_t)
		(kernel_address + len - (sizeof(struct packet_msg_prot) * 2));

	tmp = (PACKET_MSG_PROT << GOYA_PKT_CTL_OPCODE_SHIFT) |
			(1 << GOYA_PKT_CTL_EB_SHIFT) |
			(1 << GOYA_PKT_CTL_MB_SHIFT);
	cq_pkt->ctl = cpu_to_le32(tmp);
	cq_pkt->value = cpu_to_le32(cq_val);
	cq_pkt->addr = cpu_to_le64(cq_addr);

	cq_pkt++;

	tmp = (PACKET_MSG_PROT << GOYA_PKT_CTL_OPCODE_SHIFT) |
			(1 << GOYA_PKT_CTL_MB_SHIFT);
	cq_pkt->ctl = cpu_to_le32(tmp);
	cq_pkt->value = cpu_to_le32(msix_vec & 0x7FF);
	cq_pkt->addr = cpu_to_le64(CFG_BASE + mmPCIE_DBI_MSIX_DOORBELL_OFF);
}

static void goya_update_eq_ci(struct hl_device *hdev, u32 val)
{
	WREG32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_6, val);
}

static void goya_restore_phase_topology(struct hl_device *hdev)
{
	int i, num_of_sob_in_longs, num_of_mon_in_longs;

	num_of_sob_in_longs =
		((mmSYNC_MNGR_SOB_OBJ_1023 - mmSYNC_MNGR_SOB_OBJ_0) + 4);

	num_of_mon_in_longs =
		((mmSYNC_MNGR_MON_STATUS_255 - mmSYNC_MNGR_MON_STATUS_0) + 4);

	for (i = 0 ; i < num_of_sob_in_longs ; i += 4)
		WREG32(mmSYNC_MNGR_SOB_OBJ_0 + i, 0);

	for (i = 0 ; i < num_of_mon_in_longs ; i += 4)
		WREG32(mmSYNC_MNGR_MON_STATUS_0 + i, 0);

	/* Flush all WREG to prevent race */
	i = RREG32(mmSYNC_MNGR_SOB_OBJ_0);
}

/*
 * goya_debugfs_read32 - read a 32bit value from a given device address
 *
 * @hdev:	pointer to hl_device structure
 * @addr:	address in device
 * @val:	returned value
 *
 * In case of DDR address that is not mapped into the default aperture that
 * the DDR bar exposes, the function will configure the iATU so that the DDR
 * bar will be positioned at a base address that allows reading from the
 * required address. Configuring the iATU during normal operation can
 * lead to undefined behavior and therefore, should be done with extreme care
 *
 */
static int goya_debugfs_read32(struct hl_device *hdev, u64 addr, u32 *val)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc = 0;

	if ((addr >= CFG_BASE) && (addr < CFG_BASE + CFG_SIZE)) {
		*val = RREG32(addr - CFG_BASE);

	} else if ((addr >= SRAM_BASE_ADDR) &&
			(addr < SRAM_BASE_ADDR + SRAM_SIZE)) {

		*val = readl(hdev->pcie_bar[SRAM_CFG_BAR_ID] +
				(addr - SRAM_BASE_ADDR));

	} else if ((addr >= DRAM_PHYS_BASE) &&
			(addr < DRAM_PHYS_BASE + hdev->asic_prop.dram_size)) {

		u64 bar_base_addr = DRAM_PHYS_BASE +
				(addr & ~(prop->dram_pci_bar_size - 0x1ull));

		rc = goya_set_ddr_bar_base(hdev, bar_base_addr);
		if (!rc) {
			*val = readl(hdev->pcie_bar[DDR_BAR_ID] +
						(addr - bar_base_addr));

			rc = goya_set_ddr_bar_base(hdev, DRAM_PHYS_BASE +
				(MMU_PAGE_TABLES_ADDR &
					~(prop->dram_pci_bar_size - 0x1ull)));
		}
	} else {
		rc = -EFAULT;
	}

	return rc;
}

/*
 * goya_debugfs_write32 - write a 32bit value to a given device address
 *
 * @hdev:	pointer to hl_device structure
 * @addr:	address in device
 * @val:	returned value
 *
 * In case of DDR address that is not mapped into the default aperture that
 * the DDR bar exposes, the function will configure the iATU so that the DDR
 * bar will be positioned at a base address that allows writing to the
 * required address. Configuring the iATU during normal operation can
 * lead to undefined behavior and therefore, should be done with extreme care
 *
 */
static int goya_debugfs_write32(struct hl_device *hdev, u64 addr, u32 val)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc = 0;

	if ((addr >= CFG_BASE) && (addr < CFG_BASE + CFG_SIZE)) {
		WREG32(addr - CFG_BASE, val);

	} else if ((addr >= SRAM_BASE_ADDR) &&
			(addr < SRAM_BASE_ADDR + SRAM_SIZE)) {

		writel(val, hdev->pcie_bar[SRAM_CFG_BAR_ID] +
					(addr - SRAM_BASE_ADDR));

	} else if ((addr >= DRAM_PHYS_BASE) &&
			(addr < DRAM_PHYS_BASE + hdev->asic_prop.dram_size)) {

		u64 bar_base_addr = DRAM_PHYS_BASE +
				(addr & ~(prop->dram_pci_bar_size - 0x1ull));

		rc = goya_set_ddr_bar_base(hdev, bar_base_addr);
		if (!rc) {
			writel(val, hdev->pcie_bar[DDR_BAR_ID] +
						(addr - bar_base_addr));

			rc = goya_set_ddr_bar_base(hdev, DRAM_PHYS_BASE +
				(MMU_PAGE_TABLES_ADDR &
					~(prop->dram_pci_bar_size - 0x1ull)));
		}
	} else {
		rc = -EFAULT;
	}

	return rc;
}

static u64 goya_read_pte(struct hl_device *hdev, u64 addr)
{
	struct goya_device *goya = hdev->asic_specific;

	return readq(hdev->pcie_bar[DDR_BAR_ID] +
			(addr - goya->ddr_bar_cur_addr));
}

static void goya_write_pte(struct hl_device *hdev, u64 addr, u64 val)
{
	struct goya_device *goya = hdev->asic_specific;

	writeq(val, hdev->pcie_bar[DDR_BAR_ID] +
			(addr - goya->ddr_bar_cur_addr));
}

static const char *_goya_get_event_desc(u16 event_type)
{
	switch (event_type) {
	case GOYA_ASYNC_EVENT_ID_PCIE_DEC:
		return "PCIe_dec";
	case GOYA_ASYNC_EVENT_ID_TPC0_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC1_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC2_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC3_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC4_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC5_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC6_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC7_DEC:
		return "TPC%d_dec";
	case GOYA_ASYNC_EVENT_ID_MME_WACS:
		return "MME_wacs";
	case GOYA_ASYNC_EVENT_ID_MME_WACSD:
		return "MME_wacsd";
	case GOYA_ASYNC_EVENT_ID_CPU_AXI_SPLITTER:
		return "CPU_axi_splitter";
	case GOYA_ASYNC_EVENT_ID_PSOC_AXI_DEC:
		return "PSOC_axi_dec";
	case GOYA_ASYNC_EVENT_ID_PSOC:
		return "PSOC";
	case GOYA_ASYNC_EVENT_ID_TPC0_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC1_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC2_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC3_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC4_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC5_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC6_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC7_KRN_ERR:
		return "TPC%d_krn_err";
	case GOYA_ASYNC_EVENT_ID_TPC0_CMDQ ... GOYA_ASYNC_EVENT_ID_TPC7_CMDQ:
		return "TPC%d_cq";
	case GOYA_ASYNC_EVENT_ID_TPC0_QM ... GOYA_ASYNC_EVENT_ID_TPC7_QM:
		return "TPC%d_qm";
	case GOYA_ASYNC_EVENT_ID_MME_QM:
		return "MME_qm";
	case GOYA_ASYNC_EVENT_ID_MME_CMDQ:
		return "MME_cq";
	case GOYA_ASYNC_EVENT_ID_DMA0_QM ... GOYA_ASYNC_EVENT_ID_DMA4_QM:
		return "DMA%d_qm";
	case GOYA_ASYNC_EVENT_ID_DMA0_CH ... GOYA_ASYNC_EVENT_ID_DMA4_CH:
		return "DMA%d_ch";
	default:
		return "N/A";
	}
}

static void goya_get_event_desc(u16 event_type, char *desc, size_t size)
{
	u8 index;

	switch (event_type) {
	case GOYA_ASYNC_EVENT_ID_TPC0_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC1_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC2_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC3_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC4_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC5_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC6_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC7_DEC:
		index = (event_type - GOYA_ASYNC_EVENT_ID_TPC0_DEC) / 3;
		snprintf(desc, size, _goya_get_event_desc(event_type), index);
		break;
	case GOYA_ASYNC_EVENT_ID_TPC0_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC1_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC2_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC3_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC4_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC5_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC6_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC7_KRN_ERR:
		index = (event_type - GOYA_ASYNC_EVENT_ID_TPC0_KRN_ERR) / 10;
		snprintf(desc, size, _goya_get_event_desc(event_type), index);
		break;
	case GOYA_ASYNC_EVENT_ID_TPC0_CMDQ ... GOYA_ASYNC_EVENT_ID_TPC7_CMDQ:
		index = event_type - GOYA_ASYNC_EVENT_ID_TPC0_CMDQ;
		snprintf(desc, size, _goya_get_event_desc(event_type), index);
		break;
	case GOYA_ASYNC_EVENT_ID_TPC0_QM ... GOYA_ASYNC_EVENT_ID_TPC7_QM:
		index = event_type - GOYA_ASYNC_EVENT_ID_TPC0_QM;
		snprintf(desc, size, _goya_get_event_desc(event_type), index);
		break;
	case GOYA_ASYNC_EVENT_ID_DMA0_QM ... GOYA_ASYNC_EVENT_ID_DMA4_QM:
		index = event_type - GOYA_ASYNC_EVENT_ID_DMA0_QM;
		snprintf(desc, size, _goya_get_event_desc(event_type), index);
		break;
	case GOYA_ASYNC_EVENT_ID_DMA0_CH ... GOYA_ASYNC_EVENT_ID_DMA4_CH:
		index = event_type - GOYA_ASYNC_EVENT_ID_DMA0_CH;
		snprintf(desc, size, _goya_get_event_desc(event_type), index);
		break;
	default:
		snprintf(desc, size, _goya_get_event_desc(event_type));
		break;
	}
}

static void goya_print_razwi_info(struct hl_device *hdev)
{
	if (RREG32(mmDMA_MACRO_RAZWI_LBW_WT_VLD)) {
		dev_err(hdev->dev, "Illegal write to LBW\n");
		WREG32(mmDMA_MACRO_RAZWI_LBW_WT_VLD, 0);
	}

	if (RREG32(mmDMA_MACRO_RAZWI_LBW_RD_VLD)) {
		dev_err(hdev->dev, "Illegal read from LBW\n");
		WREG32(mmDMA_MACRO_RAZWI_LBW_RD_VLD, 0);
	}

	if (RREG32(mmDMA_MACRO_RAZWI_HBW_WT_VLD)) {
		dev_err(hdev->dev, "Illegal write to HBW\n");
		WREG32(mmDMA_MACRO_RAZWI_HBW_WT_VLD, 0);
	}

	if (RREG32(mmDMA_MACRO_RAZWI_HBW_RD_VLD)) {
		dev_err(hdev->dev, "Illegal read from HBW\n");
		WREG32(mmDMA_MACRO_RAZWI_HBW_RD_VLD, 0);
	}
}

static void goya_print_mmu_error_info(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	u64 addr;
	u32 val;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU))
		return;

	val = RREG32(mmMMU_PAGE_ERROR_CAPTURE);
	if (val & MMU_PAGE_ERROR_CAPTURE_ENTRY_VALID_MASK) {
		addr = val & MMU_PAGE_ERROR_CAPTURE_VA_49_32_MASK;
		addr <<= 32;
		addr |= RREG32(mmMMU_PAGE_ERROR_CAPTURE_VA);

		dev_err(hdev->dev, "MMU page fault on va 0x%llx\n", addr);

		WREG32(mmMMU_PAGE_ERROR_CAPTURE, 0);
	}
}

static void goya_print_irq_info(struct hl_device *hdev, u16 event_type)
{
	char desc[20] = "";

	goya_get_event_desc(event_type, desc, sizeof(desc));
	dev_err(hdev->dev, "Received H/W interrupt %d [\"%s\"]\n",
		event_type, desc);

	goya_print_razwi_info(hdev);
	goya_print_mmu_error_info(hdev);
}

static int goya_unmask_irq_arr(struct hl_device *hdev, u32 *irq_arr,
		size_t irq_arr_size)
{
	struct armcp_unmask_irq_arr_packet *pkt;
	size_t total_pkt_size;
	long result;
	int rc;

	total_pkt_size = sizeof(struct armcp_unmask_irq_arr_packet) +
			irq_arr_size;

	/* data should be aligned to 8 bytes in order to ArmCP to copy it */
	total_pkt_size = (total_pkt_size + 0x7) & ~0x7;

	/* total_pkt_size is casted to u16 later on */
	if (total_pkt_size > USHRT_MAX) {
		dev_err(hdev->dev, "too many elements in IRQ array\n");
		return -EINVAL;
	}

	pkt = kzalloc(total_pkt_size, GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	pkt->length = cpu_to_le32(irq_arr_size / sizeof(irq_arr[0]));
	memcpy(&pkt->irqs, irq_arr, irq_arr_size);

	pkt->armcp_pkt.ctl = cpu_to_le32(ARMCP_PACKET_UNMASK_RAZWI_IRQ_ARRAY <<
						ARMCP_PKT_CTL_OPCODE_SHIFT);

	rc = goya_send_cpu_message(hdev, (u32 *) pkt, total_pkt_size,
			HL_DEVICE_TIMEOUT_USEC, &result);

	if (rc)
		dev_err(hdev->dev, "failed to unmask IRQ array\n");

	kfree(pkt);

	return rc;
}

static int goya_soft_reset_late_init(struct hl_device *hdev)
{
	/*
	 * Unmask all IRQs since some could have been received
	 * during the soft reset
	 */
	return goya_unmask_irq_arr(hdev, goya_all_events,
					sizeof(goya_all_events));
}

static int goya_unmask_irq(struct hl_device *hdev, u16 event_type)
{
	struct armcp_packet pkt;
	long result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(ARMCP_PACKET_UNMASK_RAZWI_IRQ <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(event_type);

	rc = goya_send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
			HL_DEVICE_TIMEOUT_USEC, &result);

	if (rc)
		dev_err(hdev->dev, "failed to unmask RAZWI IRQ %d", event_type);

	return rc;
}

void goya_handle_eqe(struct hl_device *hdev, struct hl_eq_entry *eq_entry)
{
	u32 ctl = le32_to_cpu(eq_entry->hdr.ctl);
	u16 event_type = ((ctl & EQ_CTL_EVENT_TYPE_MASK)
				>> EQ_CTL_EVENT_TYPE_SHIFT);
	struct goya_device *goya = hdev->asic_specific;

	goya->events_stat[event_type]++;

	switch (event_type) {
	case GOYA_ASYNC_EVENT_ID_PCIE_IF:
	case GOYA_ASYNC_EVENT_ID_TPC0_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC1_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC2_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC3_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC4_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC5_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC6_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC7_ECC:
	case GOYA_ASYNC_EVENT_ID_MME_ECC:
	case GOYA_ASYNC_EVENT_ID_MME_ECC_EXT:
	case GOYA_ASYNC_EVENT_ID_MMU_ECC:
	case GOYA_ASYNC_EVENT_ID_DMA_MACRO:
	case GOYA_ASYNC_EVENT_ID_DMA_ECC:
	case GOYA_ASYNC_EVENT_ID_CPU_IF_ECC:
	case GOYA_ASYNC_EVENT_ID_PSOC_MEM:
	case GOYA_ASYNC_EVENT_ID_PSOC_CORESIGHT:
	case GOYA_ASYNC_EVENT_ID_SRAM0 ... GOYA_ASYNC_EVENT_ID_SRAM29:
	case GOYA_ASYNC_EVENT_ID_GIC500:
	case GOYA_ASYNC_EVENT_ID_PLL0:
	case GOYA_ASYNC_EVENT_ID_PLL1:
	case GOYA_ASYNC_EVENT_ID_PLL3:
	case GOYA_ASYNC_EVENT_ID_PLL4:
	case GOYA_ASYNC_EVENT_ID_PLL5:
	case GOYA_ASYNC_EVENT_ID_PLL6:
	case GOYA_ASYNC_EVENT_ID_AXI_ECC:
	case GOYA_ASYNC_EVENT_ID_L2_RAM_ECC:
	case GOYA_ASYNC_EVENT_ID_PSOC_GPIO_05_SW_RESET:
	case GOYA_ASYNC_EVENT_ID_PSOC_GPIO_10_VRHOT_ICRIT:
		dev_err(hdev->dev,
			"Received H/W interrupt %d, reset the chip\n",
			event_type);
		hl_device_reset(hdev, true, false);
		break;

	case GOYA_ASYNC_EVENT_ID_PCIE_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC0_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC1_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC2_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC3_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC4_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC5_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC6_DEC:
	case GOYA_ASYNC_EVENT_ID_TPC7_DEC:
	case GOYA_ASYNC_EVENT_ID_MME_WACS:
	case GOYA_ASYNC_EVENT_ID_MME_WACSD:
	case GOYA_ASYNC_EVENT_ID_CPU_AXI_SPLITTER:
	case GOYA_ASYNC_EVENT_ID_PSOC_AXI_DEC:
	case GOYA_ASYNC_EVENT_ID_PSOC:
	case GOYA_ASYNC_EVENT_ID_TPC0_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC1_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC2_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC3_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC4_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC5_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC6_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC7_KRN_ERR:
	case GOYA_ASYNC_EVENT_ID_TPC0_CMDQ ... GOYA_ASYNC_EVENT_ID_TPC7_QM:
	case GOYA_ASYNC_EVENT_ID_MME_QM:
	case GOYA_ASYNC_EVENT_ID_MME_CMDQ:
	case GOYA_ASYNC_EVENT_ID_DMA0_QM ... GOYA_ASYNC_EVENT_ID_DMA4_QM:
	case GOYA_ASYNC_EVENT_ID_DMA0_CH ... GOYA_ASYNC_EVENT_ID_DMA4_CH:
		goya_print_irq_info(hdev, event_type);
		goya_unmask_irq(hdev, event_type);
		break;

	case GOYA_ASYNC_EVENT_ID_TPC0_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC1_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC2_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC3_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC4_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC5_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC6_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC7_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_DMA_BM_CH0:
	case GOYA_ASYNC_EVENT_ID_DMA_BM_CH1:
	case GOYA_ASYNC_EVENT_ID_DMA_BM_CH2:
	case GOYA_ASYNC_EVENT_ID_DMA_BM_CH3:
	case GOYA_ASYNC_EVENT_ID_DMA_BM_CH4:
		dev_info(hdev->dev, "Received H/W interrupt %d\n", event_type);
		break;

	default:
		dev_err(hdev->dev, "Received invalid H/W interrupt %d\n",
				event_type);
		break;
	}
}

void *goya_get_events_stat(struct hl_device *hdev, u32 *size)
{
	struct goya_device *goya = hdev->asic_specific;

	*size = (u32) sizeof(goya->events_stat);

	return goya->events_stat;
}

static int goya_memset_device_memory(struct hl_device *hdev, u64 addr, u32 size,
				u64 val, bool is_dram)
{
	struct packet_lin_dma *lin_dma_pkt;
	struct hl_cs_parser parser;
	struct hl_cs_job *job;
	u32 cb_size, ctl;
	struct hl_cb *cb;
	int rc;

	cb = hl_cb_kernel_create(hdev, PAGE_SIZE);
	if (!cb)
		return -EFAULT;

	lin_dma_pkt = (struct packet_lin_dma *) (uintptr_t) cb->kernel_address;

	memset(lin_dma_pkt, 0, sizeof(*lin_dma_pkt));
	cb_size = sizeof(*lin_dma_pkt);

	ctl = ((PACKET_LIN_DMA << GOYA_PKT_CTL_OPCODE_SHIFT) |
			(1 << GOYA_PKT_LIN_DMA_CTL_MEMSET_SHIFT) |
			(1 << GOYA_PKT_LIN_DMA_CTL_WO_SHIFT) |
			(1 << GOYA_PKT_CTL_RB_SHIFT) |
			(1 << GOYA_PKT_CTL_MB_SHIFT));
	ctl |= (is_dram ? DMA_HOST_TO_DRAM : DMA_HOST_TO_SRAM) <<
			GOYA_PKT_LIN_DMA_CTL_DMA_DIR_SHIFT;
	lin_dma_pkt->ctl = cpu_to_le32(ctl);

	lin_dma_pkt->src_addr = cpu_to_le64(val);
	lin_dma_pkt->dst_addr = cpu_to_le64(addr);
	lin_dma_pkt->tsize = cpu_to_le32(size);

	job = hl_cs_allocate_job(hdev, true);
	if (!job) {
		dev_err(hdev->dev, "Failed to allocate a new job\n");
		rc = -ENOMEM;
		goto release_cb;
	}

	job->id = 0;
	job->user_cb = cb;
	job->user_cb->cs_cnt++;
	job->user_cb_size = cb_size;
	job->hw_queue_id = GOYA_QUEUE_ID_DMA_0;

	hl_debugfs_add_job(hdev, job);

	parser.ctx_id = HL_KERNEL_ASID_ID;
	parser.cs_sequence = 0;
	parser.job_id = job->id;
	parser.hw_queue_id = job->hw_queue_id;
	parser.job_userptr_list = &job->userptr_list;
	parser.user_cb = job->user_cb;
	parser.user_cb_size = job->user_cb_size;
	parser.ext_queue = job->ext_queue;
	parser.use_virt_addr = hdev->mmu_enable;

	rc = hdev->asic_funcs->cs_parser(hdev, &parser);
	if (rc) {
		dev_err(hdev->dev, "Failed to parse kernel CB\n");
		goto free_job;
	}

	job->patched_cb = parser.patched_cb;
	job->job_cb_size = parser.patched_cb_size;
	job->patched_cb->cs_cnt++;

	rc = goya_send_job_on_qman0(hdev, job);

	job->patched_cb->cs_cnt--;
	hl_cb_put(job->patched_cb);

free_job:
	hl_userptr_delete_list(hdev, &job->userptr_list);
	hl_debugfs_remove_job(hdev, job);
	kfree(job);
	cb->cs_cnt--;

release_cb:
	hl_cb_put(cb);
	hl_cb_destroy(hdev, &hdev->kernel_cb_mgr, cb->id << PAGE_SHIFT);

	return rc;
}

static int goya_context_switch(struct hl_device *hdev, u32 asid)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 addr = prop->sram_base_address;
	u32 size = hdev->pldm ? 0x10000 : prop->sram_size;
	u64 val = 0x7777777777777777ull;
	int rc;

	rc = goya_memset_device_memory(hdev, addr, size, val, false);
	if (rc) {
		dev_err(hdev->dev, "Failed to clear SRAM in context switch\n");
		return rc;
	}

	WREG32(mmTPC_PLL_CLK_RLX_0, 0x200020);
	goya_mmu_prepare(hdev, asid);

	return 0;
}

int goya_mmu_clear_pgt_range(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct goya_device *goya = hdev->asic_specific;
	u64 addr = prop->mmu_pgt_addr;
	u32 size = prop->mmu_pgt_size + MMU_DRAM_DEFAULT_PAGE_SIZE +
			MMU_CACHE_MNG_SIZE;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU))
		return 0;

	return goya_memset_device_memory(hdev, addr, size, 0, true);
}

int goya_mmu_set_dram_default_page(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	u64 addr = hdev->asic_prop.mmu_dram_default_page_addr;
	u32 size = MMU_DRAM_DEFAULT_PAGE_SIZE;
	u64 val = 0x9999999999999999ull;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU))
		return 0;

	return goya_memset_device_memory(hdev, addr, size, val, true);
}

void goya_mmu_prepare(struct hl_device *hdev, u32 asid)
{
	struct goya_device *goya = hdev->asic_specific;
	int i;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU))
		return;

	if (asid & ~MME_QM_GLBL_SECURE_PROPS_ASID_MASK) {
		WARN(1, "asid %u is too big\n", asid);
		return;
	}

	/* zero the MMBP and ASID bits and then set the ASID */
	for (i = 0 ; i < GOYA_MMU_REGS_NUM ; i++)
		goya->mmu_prepare_reg(hdev, goya_mmu_regs[i], asid);
}

static void goya_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard)
{
	struct goya_device *goya = hdev->asic_specific;
	u32 status, timeout_usec;
	int rc;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU))
		return;

	/* no need in L1 only invalidation in Goya */
	if (!is_hard)
		return;

	if (hdev->pldm)
		timeout_usec = GOYA_PLDM_MMU_TIMEOUT_USEC;
	else
		timeout_usec = MMU_CONFIG_TIMEOUT_USEC;

	mutex_lock(&hdev->mmu_cache_lock);

	/* L0 & L1 invalidation */
	WREG32(mmSTLB_INV_ALL_START, 1);

	rc = hl_poll_timeout(
		hdev,
		mmSTLB_INV_ALL_START,
		status,
		!status,
		1000,
		timeout_usec);

	mutex_unlock(&hdev->mmu_cache_lock);

	if (rc)
		dev_notice_ratelimited(hdev->dev,
			"Timeout when waiting for MMU cache invalidation\n");
}

static void goya_mmu_invalidate_cache_range(struct hl_device *hdev,
		bool is_hard, u32 asid, u64 va, u64 size)
{
	struct goya_device *goya = hdev->asic_specific;
	u32 status, timeout_usec, inv_data, pi;
	int rc;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU))
		return;

	/* no need in L1 only invalidation in Goya */
	if (!is_hard)
		return;

	if (hdev->pldm)
		timeout_usec = GOYA_PLDM_MMU_TIMEOUT_USEC;
	else
		timeout_usec = MMU_CONFIG_TIMEOUT_USEC;

	mutex_lock(&hdev->mmu_cache_lock);

	/*
	 * TODO: currently invalidate entire L0 & L1 as in regular hard
	 * invalidation. Need to apply invalidation of specific cache lines with
	 * mask of ASID & VA & size.
	 * Note that L1 with be flushed entirely in any case.
	 */

	/* L0 & L1 invalidation */
	inv_data = RREG32(mmSTLB_CACHE_INV);
	/* PI is 8 bit */
	pi = ((inv_data & STLB_CACHE_INV_PRODUCER_INDEX_MASK) + 1) & 0xFF;
	WREG32(mmSTLB_CACHE_INV,
			(inv_data & STLB_CACHE_INV_INDEX_MASK_MASK) | pi);

	rc = hl_poll_timeout(
		hdev,
		mmSTLB_INV_CONSUMER_INDEX,
		status,
		status == pi,
		1000,
		timeout_usec);

	mutex_unlock(&hdev->mmu_cache_lock);

	if (rc)
		dev_notice_ratelimited(hdev->dev,
			"Timeout when waiting for MMU cache invalidation\n");
}

int goya_send_heartbeat(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	return hl_fw_send_heartbeat(hdev);
}

int goya_armcp_info_get(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 dram_size;
	int rc;

	if (!(goya->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	rc = hl_fw_armcp_info_get(hdev);
	if (rc)
		return rc;

	dram_size = le64_to_cpu(prop->armcp_info.dram_size);
	if (dram_size) {
		if ((!is_power_of_2(dram_size)) ||
				(dram_size < DRAM_PHYS_DEFAULT_SIZE)) {
			dev_err(hdev->dev,
				"F/W reported invalid DRAM size %llu. Trying to use default size\n",
				dram_size);
			dram_size = DRAM_PHYS_DEFAULT_SIZE;
		}

		prop->dram_size = dram_size;
		prop->dram_end_address = prop->dram_base_address + dram_size;
	}

	return 0;
}

static bool goya_is_device_idle(struct hl_device *hdev, char *buf, size_t size)
{
	u64 offset, dma_qm_reg, tpc_qm_reg, tpc_cmdq_reg, tpc_cfg_reg;
	int i;

	offset = mmDMA_QM_1_GLBL_STS0 - mmDMA_QM_0_GLBL_STS0;

	for (i = 0 ; i < DMA_MAX_NUM ; i++) {
		dma_qm_reg = mmDMA_QM_0_GLBL_STS0 + i * offset;

		if ((RREG32(dma_qm_reg) & DMA_QM_IDLE_MASK) !=
				DMA_QM_IDLE_MASK)
			return HL_ENG_BUSY(buf, size, "DMA%d_QM", i);
	}

	offset = mmTPC1_QM_GLBL_STS0 - mmTPC0_QM_GLBL_STS0;

	for (i = 0 ; i < TPC_MAX_NUM ; i++) {
		tpc_qm_reg = mmTPC0_QM_GLBL_STS0 + i * offset;
		tpc_cmdq_reg = mmTPC0_CMDQ_GLBL_STS0 + i * offset;
		tpc_cfg_reg = mmTPC0_CFG_STATUS + i * offset;

		if ((RREG32(tpc_qm_reg) & TPC_QM_IDLE_MASK) !=
				TPC_QM_IDLE_MASK)
			return HL_ENG_BUSY(buf, size, "TPC%d_QM", i);

		if ((RREG32(tpc_cmdq_reg) & TPC_CMDQ_IDLE_MASK) !=
				TPC_CMDQ_IDLE_MASK)
			return HL_ENG_BUSY(buf, size, "TPC%d_CMDQ", i);

		if ((RREG32(tpc_cfg_reg) & TPC_CFG_IDLE_MASK) !=
				TPC_CFG_IDLE_MASK)
			return HL_ENG_BUSY(buf, size, "TPC%d_CFG", i);
	}

	if ((RREG32(mmMME_QM_GLBL_STS0) & MME_QM_IDLE_MASK) !=
			MME_QM_IDLE_MASK)
		return HL_ENG_BUSY(buf, size, "MME_QM");

	if ((RREG32(mmMME_CMDQ_GLBL_STS0) & MME_CMDQ_IDLE_MASK) !=
			MME_CMDQ_IDLE_MASK)
		return HL_ENG_BUSY(buf, size, "MME_CMDQ");

	if ((RREG32(mmMME_ARCH_STATUS) & MME_ARCH_IDLE_MASK) !=
			MME_ARCH_IDLE_MASK)
		return HL_ENG_BUSY(buf, size, "MME_ARCH");

	if (RREG32(mmMME_SHADOW_0_STATUS) & MME_SHADOW_IDLE_MASK)
		return HL_ENG_BUSY(buf, size, "MME");

	return true;
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

static u32 goya_get_pci_id(struct hl_device *hdev)
{
	return hdev->pdev->device;
}

static int goya_get_eeprom_data(struct hl_device *hdev, void *data,
				size_t max_size)
{
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	return hl_fw_get_eeprom_data(hdev, data, max_size);
}

static enum hl_device_hw_state goya_get_hw_state(struct hl_device *hdev)
{
	return RREG32(mmPSOC_GLOBAL_CONF_APP_STATUS);
}

static const struct hl_asic_funcs goya_funcs = {
	.early_init = goya_early_init,
	.early_fini = goya_early_fini,
	.late_init = goya_late_init,
	.late_fini = goya_late_fini,
	.sw_init = goya_sw_init,
	.sw_fini = goya_sw_fini,
	.hw_init = goya_hw_init,
	.hw_fini = goya_hw_fini,
	.halt_engines = goya_halt_engines,
	.suspend = goya_suspend,
	.resume = goya_resume,
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
	.hl_dma_unmap_sg = goya_dma_unmap_sg,
	.cs_parser = goya_cs_parser,
	.asic_dma_map_sg = goya_dma_map_sg,
	.get_dma_desc_list_size = goya_get_dma_desc_list_size,
	.add_end_of_cb_packets = goya_add_end_of_cb_packets,
	.update_eq_ci = goya_update_eq_ci,
	.context_switch = goya_context_switch,
	.restore_phase_topology = goya_restore_phase_topology,
	.debugfs_read32 = goya_debugfs_read32,
	.debugfs_write32 = goya_debugfs_write32,
	.add_device_attr = goya_add_device_attr,
	.handle_eqe = goya_handle_eqe,
	.set_pll_profile = goya_set_pll_profile,
	.get_events_stat = goya_get_events_stat,
	.read_pte = goya_read_pte,
	.write_pte = goya_write_pte,
	.mmu_invalidate_cache = goya_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = goya_mmu_invalidate_cache_range,
	.send_heartbeat = goya_send_heartbeat,
	.debug_coresight = goya_debug_coresight,
	.is_device_idle = goya_is_device_idle,
	.soft_reset_late_init = goya_soft_reset_late_init,
	.hw_queues_lock = goya_hw_queues_lock,
	.hw_queues_unlock = goya_hw_queues_unlock,
	.get_pci_id = goya_get_pci_id,
	.get_eeprom_data = goya_get_eeprom_data,
	.send_cpu_message = goya_send_cpu_message,
	.get_hw_state = goya_get_hw_state,
	.pci_bars_map = goya_pci_bars_map,
	.set_dram_bar_base = goya_set_ddr_bar_base,
	.init_iatu = goya_init_iatu
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
