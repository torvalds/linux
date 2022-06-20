// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "goyaP.h"
#include "../include/hw_ip/mmu/mmu_general.h"
#include "../include/hw_ip/mmu/mmu_v1_0.h"
#include "../include/goya/asic_reg/goya_masks.h"
#include "../include/goya/goya_reg_map.h"

#include <linux/pci.h>
#include <linux/hwmon.h>
#include <linux/iommu.h>
#include <linux/seq_file.h>

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
 * Since QMAN DMA is secured, the driver is parsing the DMA CB:
 *     - checks DMA pointer
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
 *     - PQ entry is in kernel address space and the driver doesn't map it.
 *     - CP writes to MSIX register and to kernel address space (completion
 *       queue).
 *
 * DMA is not secured but because CP is secured, the driver still needs to parse
 * the CB, but doesn't need to check the DMA addresses.
 *
 * For QMAN DMA 0, DMA is also secured because only the driver uses this DMA and
 * the driver doesn't map memory in MMU.
 *
 * QMAN TPC/MME: PQ, CQ and CP aren't secured (no change from MMU disabled mode)
 *
 * DMA RR does NOT protect host because DMA is not secured
 *
 */

#define GOYA_BOOT_FIT_FILE	"habanalabs/goya/goya-boot-fit.itb"
#define GOYA_LINUX_FW_FILE	"habanalabs/goya/goya-fit.itb"

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
#define GOYA_BOOT_FIT_REQ_TIMEOUT_USEC	1000000		/* 1s */
#define GOYA_MSG_TO_CPU_TIMEOUT_USEC	4000000		/* 4s */
#define GOYA_WAIT_FOR_BL_TIMEOUT_USEC	15000000	/* 15s */

#define GOYA_QMAN0_FENCE_VAL		0xD169B243

#define GOYA_MAX_STRING_LEN		20

#define GOYA_CB_POOL_CB_CNT		512
#define GOYA_CB_POOL_CB_SIZE		0x20000		/* 128KB */

#define IS_QM_IDLE(engine, qm_glbl_sts0) \
	(((qm_glbl_sts0) & engine##_QM_IDLE_MASK) == engine##_QM_IDLE_MASK)
#define IS_DMA_QM_IDLE(qm_glbl_sts0)	IS_QM_IDLE(DMA, qm_glbl_sts0)
#define IS_TPC_QM_IDLE(qm_glbl_sts0)	IS_QM_IDLE(TPC, qm_glbl_sts0)
#define IS_MME_QM_IDLE(qm_glbl_sts0)	IS_QM_IDLE(MME, qm_glbl_sts0)

#define IS_CMDQ_IDLE(engine, cmdq_glbl_sts0) \
	(((cmdq_glbl_sts0) & engine##_CMDQ_IDLE_MASK) == \
			engine##_CMDQ_IDLE_MASK)
#define IS_TPC_CMDQ_IDLE(cmdq_glbl_sts0) \
	IS_CMDQ_IDLE(TPC, cmdq_glbl_sts0)
#define IS_MME_CMDQ_IDLE(cmdq_glbl_sts0) \
	IS_CMDQ_IDLE(MME, cmdq_glbl_sts0)

#define IS_DMA_IDLE(dma_core_sts0) \
	!((dma_core_sts0) & DMA_CH_0_STS0_DMA_BUSY_MASK)

#define IS_TPC_IDLE(tpc_cfg_sts) \
	(((tpc_cfg_sts) & TPC_CFG_IDLE_MASK) == TPC_CFG_IDLE_MASK)

#define IS_MME_IDLE(mme_arch_sts) \
	(((mme_arch_sts) & MME_ARCH_IDLE_MASK) == MME_ARCH_IDLE_MASK)

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

static inline bool validate_packet_id(enum packet_id id)
{
	switch (id) {
	case PACKET_WREG_32:
	case PACKET_WREG_BULK:
	case PACKET_MSG_LONG:
	case PACKET_MSG_SHORT:
	case PACKET_CP_DMA:
	case PACKET_MSG_PROT:
	case PACKET_FENCE:
	case PACKET_LIN_DMA:
	case PACKET_NOP:
	case PACKET_STOP:
		return true;
	default:
		return false;
	}
}

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
	GOYA_ASYNC_EVENT_ID_DMA_BM_CH4,
	GOYA_ASYNC_EVENT_ID_FIX_POWER_ENV_S,
	GOYA_ASYNC_EVENT_ID_FIX_POWER_ENV_E,
	GOYA_ASYNC_EVENT_ID_FIX_THERMAL_ENV_S,
	GOYA_ASYNC_EVENT_ID_FIX_THERMAL_ENV_E
};

static s64 goya_state_dump_specs_props[SP_MAX] = {0};

static int goya_mmu_clear_pgt_range(struct hl_device *hdev);
static int goya_mmu_set_dram_default_page(struct hl_device *hdev);
static int goya_mmu_add_mappings_for_device_cpu(struct hl_device *hdev);
static void goya_mmu_prepare(struct hl_device *hdev, u32 asid);

int goya_set_fixed_properties(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int i;

	prop->max_queues = GOYA_QUEUE_ID_SIZE;
	prop->hw_queues_props = kcalloc(prop->max_queues,
			sizeof(struct hw_queue_properties),
			GFP_KERNEL);

	if (!prop->hw_queues_props)
		return -ENOMEM;

	for (i = 0 ; i < NUMBER_OF_EXT_HW_QUEUES ; i++) {
		prop->hw_queues_props[i].type = QUEUE_TYPE_EXT;
		prop->hw_queues_props[i].driver_only = 0;
		prop->hw_queues_props[i].cb_alloc_flags = CB_ALLOC_KERNEL;
	}

	for (; i < NUMBER_OF_EXT_HW_QUEUES + NUMBER_OF_CPU_HW_QUEUES ; i++) {
		prop->hw_queues_props[i].type = QUEUE_TYPE_CPU;
		prop->hw_queues_props[i].driver_only = 1;
		prop->hw_queues_props[i].cb_alloc_flags = CB_ALLOC_KERNEL;
	}

	for (; i < NUMBER_OF_EXT_HW_QUEUES + NUMBER_OF_CPU_HW_QUEUES +
			NUMBER_OF_INT_HW_QUEUES; i++) {
		prop->hw_queues_props[i].type = QUEUE_TYPE_INT;
		prop->hw_queues_props[i].driver_only = 0;
		prop->hw_queues_props[i].cb_alloc_flags = CB_ALLOC_USER;
	}

	prop->device_dma_offset_for_host_access = HOST_PHYS_BASE;
	prop->host_base_address = HOST_PHYS_BASE;
	prop->host_end_address = prop->host_base_address + HOST_PHYS_SIZE;
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
	prop->mmu_hop_table_size = HOP_TABLE_SIZE_512_PTE;
	prop->mmu_hop0_tables_total_size = HOP0_512_PTE_TABLES_TOTAL_SIZE;
	prop->dram_page_size = PAGE_SIZE_2MB;
	prop->device_mem_alloc_default_page_size = prop->dram_page_size;
	prop->dram_supports_virtual_memory = true;

	prop->dmmu.hop_shifts[MMU_HOP0] = MMU_V1_0_HOP0_SHIFT;
	prop->dmmu.hop_shifts[MMU_HOP1] = MMU_V1_0_HOP1_SHIFT;
	prop->dmmu.hop_shifts[MMU_HOP2] = MMU_V1_0_HOP2_SHIFT;
	prop->dmmu.hop_shifts[MMU_HOP3] = MMU_V1_0_HOP3_SHIFT;
	prop->dmmu.hop_shifts[MMU_HOP4] = MMU_V1_0_HOP4_SHIFT;
	prop->dmmu.hop_masks[MMU_HOP0] = MMU_V1_0_HOP0_MASK;
	prop->dmmu.hop_masks[MMU_HOP1] = MMU_V1_0_HOP1_MASK;
	prop->dmmu.hop_masks[MMU_HOP2] = MMU_V1_0_HOP2_MASK;
	prop->dmmu.hop_masks[MMU_HOP3] = MMU_V1_0_HOP3_MASK;
	prop->dmmu.hop_masks[MMU_HOP4] = MMU_V1_0_HOP4_MASK;
	prop->dmmu.start_addr = VA_DDR_SPACE_START;
	prop->dmmu.end_addr = VA_DDR_SPACE_END;
	prop->dmmu.page_size = PAGE_SIZE_2MB;
	prop->dmmu.num_hops = MMU_ARCH_5_HOPS;
	prop->dmmu.last_mask = LAST_MASK;
	/* TODO: will be duplicated until implementing per-MMU props */
	prop->dmmu.hop_table_size = prop->mmu_hop_table_size;
	prop->dmmu.hop0_tables_total_size = prop->mmu_hop0_tables_total_size;

	/* shifts and masks are the same in PMMU and DMMU */
	memcpy(&prop->pmmu, &prop->dmmu, sizeof(prop->dmmu));
	prop->pmmu.start_addr = VA_HOST_SPACE_START;
	prop->pmmu.end_addr = VA_HOST_SPACE_END;
	prop->pmmu.page_size = PAGE_SIZE_4KB;
	prop->pmmu.num_hops = MMU_ARCH_5_HOPS;
	prop->pmmu.last_mask = LAST_MASK;
	/* TODO: will be duplicated until implementing per-MMU props */
	prop->pmmu.hop_table_size = prop->mmu_hop_table_size;
	prop->pmmu.hop0_tables_total_size = prop->mmu_hop0_tables_total_size;

	/* PMMU and HPMMU are the same except of page size */
	memcpy(&prop->pmmu_huge, &prop->pmmu, sizeof(prop->pmmu));
	prop->pmmu_huge.page_size = PAGE_SIZE_2MB;

	prop->dram_size_for_default_page_mapping = VA_DDR_SPACE_END;
	prop->cfg_size = CFG_SIZE;
	prop->max_asid = MAX_ASID;
	prop->num_of_events = GOYA_ASYNC_EVENT_ID_SIZE;
	prop->high_pll = PLL_HIGH_DEFAULT;
	prop->cb_pool_cb_cnt = GOYA_CB_POOL_CB_CNT;
	prop->cb_pool_cb_size = GOYA_CB_POOL_CB_SIZE;
	prop->max_power_default = MAX_POWER_DEFAULT;
	prop->dc_power_default = DC_POWER_DEFAULT;
	prop->tpc_enabled_mask = TPC_ENABLED_MASK;
	prop->pcie_dbi_base_address = mmPCIE_DBI_BASE;
	prop->pcie_aux_dbi_reg_addr = CFG_BASE + mmPCIE_AUX_DBI;

	strncpy(prop->cpucp_info.card_name, GOYA_DEFAULT_CARD_NAME,
		CARD_NAME_MAX_LEN);

	prop->max_pending_cs = GOYA_MAX_PENDING_CS;

	prop->first_available_user_msix_interrupt = USHRT_MAX;

	for (i = 0 ; i < HL_MAX_DCORES ; i++)
		prop->first_available_cq[i] = USHRT_MAX;

	prop->fw_cpu_boot_dev_sts0_valid = false;
	prop->fw_cpu_boot_dev_sts1_valid = false;
	prop->hard_reset_done_by_fw = false;
	prop->gic_interrupts_enable = true;

	prop->server_type = HL_SERVER_TYPE_UNKNOWN;

	prop->clk_pll_index = HL_GOYA_MME_PLL;

	prop->use_get_power_for_reset_history = true;

	prop->configurable_stop_on_err = true;

	prop->set_max_power_on_device_init = true;

	prop->dma_mask = 48;

	return 0;
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

static u64 goya_set_ddr_bar_base(struct hl_device *hdev, u64 addr)
{
	struct goya_device *goya = hdev->asic_specific;
	struct hl_inbound_pci_region pci_region;
	u64 old_addr = addr;
	int rc;

	if ((goya) && (goya->ddr_bar_cur_addr == addr))
		return old_addr;

	/* Inbound Region 1 - Bar 4 - Point to DDR */
	pci_region.mode = PCI_BAR_MATCH_MODE;
	pci_region.bar = DDR_BAR_ID;
	pci_region.addr = addr;
	rc = hl_pci_set_inbound_region(hdev, 1, &pci_region);
	if (rc)
		return U64_MAX;

	if (goya) {
		old_addr = goya->ddr_bar_cur_addr;
		goya->ddr_bar_cur_addr = addr;
	}

	return old_addr;
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
	struct hl_inbound_pci_region inbound_region;
	struct hl_outbound_pci_region outbound_region;
	int rc;

	if (hdev->asic_prop.iatu_done_by_fw)
		return 0;

	/* Inbound Region 0 - Bar 0 - Point to SRAM and CFG */
	inbound_region.mode = PCI_BAR_MATCH_MODE;
	inbound_region.bar = SRAM_CFG_BAR_ID;
	inbound_region.addr = SRAM_BASE_ADDR;
	rc = hl_pci_set_inbound_region(hdev, 0, &inbound_region);
	if (rc)
		goto done;

	/* Inbound Region 1 - Bar 4 - Point to DDR */
	inbound_region.mode = PCI_BAR_MATCH_MODE;
	inbound_region.bar = DDR_BAR_ID;
	inbound_region.addr = DRAM_PHYS_BASE;
	rc = hl_pci_set_inbound_region(hdev, 1, &inbound_region);
	if (rc)
		goto done;

	/* Outbound Region 0 - Point to Host  */
	outbound_region.addr = HOST_PHYS_BASE;
	outbound_region.size = HOST_PHYS_SIZE;
	rc = hl_pci_set_outbound_region(hdev, &outbound_region);

done:
	return rc;
}

static enum hl_device_hw_state goya_get_hw_state(struct hl_device *hdev)
{
	return RREG32(mmHW_STATE);
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
	u32 fw_boot_status, val;
	int rc;

	rc = goya_set_fixed_properties(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to get fixed properties\n");
		return rc;
	}

	/* Check BAR sizes */
	if (pci_resource_len(pdev, SRAM_CFG_BAR_ID) != CFG_BAR_SIZE) {
		dev_err(hdev->dev,
			"Not " HL_NAME "? BAR %d size %llu, expecting %llu\n",
			SRAM_CFG_BAR_ID,
			(unsigned long long) pci_resource_len(pdev,
							SRAM_CFG_BAR_ID),
			CFG_BAR_SIZE);
		rc = -ENODEV;
		goto free_queue_props;
	}

	if (pci_resource_len(pdev, MSIX_BAR_ID) != MSIX_BAR_SIZE) {
		dev_err(hdev->dev,
			"Not " HL_NAME "? BAR %d size %llu, expecting %llu\n",
			MSIX_BAR_ID,
			(unsigned long long) pci_resource_len(pdev,
								MSIX_BAR_ID),
			MSIX_BAR_SIZE);
		rc = -ENODEV;
		goto free_queue_props;
	}

	prop->dram_pci_bar_size = pci_resource_len(pdev, DDR_BAR_ID);
	hdev->dram_pci_bar_start = pci_resource_start(pdev, DDR_BAR_ID);

	/* If FW security is enabled at this point it means no access to ELBI */
	if (hdev->asic_prop.fw_security_enabled) {
		hdev->asic_prop.iatu_done_by_fw = true;
		goto pci_init;
	}

	rc = hl_pci_elbi_read(hdev, CFG_BASE + mmCPU_BOOT_DEV_STS0,
				&fw_boot_status);
	if (rc)
		goto free_queue_props;

	/* Check whether FW is configuring iATU */
	if ((fw_boot_status & CPU_BOOT_DEV_STS0_ENABLED) &&
			(fw_boot_status & CPU_BOOT_DEV_STS0_FW_IATU_CONF_EN))
		hdev->asic_prop.iatu_done_by_fw = true;

pci_init:
	rc = hl_pci_init(hdev);
	if (rc)
		goto free_queue_props;

	/* Before continuing in the initialization, we need to read the preboot
	 * version to determine whether we run with a security-enabled firmware
	 */
	rc = hl_fw_read_preboot_status(hdev, mmPSOC_GLOBAL_CONF_CPU_BOOT_STATUS,
					mmCPU_BOOT_DEV_STS0,
					mmCPU_BOOT_DEV_STS1, mmCPU_BOOT_ERR0,
					mmCPU_BOOT_ERR1,
					GOYA_BOOT_FIT_REQ_TIMEOUT_USEC);
	if (rc) {
		if (hdev->reset_on_preboot_fail)
			hdev->asic_funcs->hw_fini(hdev, true, false);
		goto pci_fini;
	}

	if (goya_get_hw_state(hdev) == HL_DEVICE_HW_STATE_DIRTY) {
		dev_info(hdev->dev,
			"H/W state is dirty, must reset before initializing\n");
		hdev->asic_funcs->hw_fini(hdev, true, false);
	}

	if (!hdev->pldm) {
		val = RREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS);
		if (val & PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_SRIOV_EN_MASK)
			dev_warn(hdev->dev,
				"PCI strap is not configured correctly, PCI bus errors may occur\n");
	}

	return 0;

pci_fini:
	hl_pci_fini(hdev);
free_queue_props:
	kfree(hdev->asic_prop.hw_queues_props);
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
static int goya_early_fini(struct hl_device *hdev)
{
	kfree(hdev->asic_prop.hw_queues_props);
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
	u32 nr = 0, nf = 0, od = 0, div_fctr = 0, pll_clk, div_sel;
	u16 pll_freq_arr[HL_PLL_NUM_OUTPUTS], freq;
	int rc;

	if (hdev->asic_prop.fw_security_enabled) {
		struct goya_device *goya = hdev->asic_specific;

		if (!(goya->hw_cap_initialized & HW_CAP_CPU_Q))
			return;

		rc = hl_fw_cpucp_pll_info_get(hdev, HL_GOYA_PCI_PLL,
				pll_freq_arr);

		if (rc)
			return;

		freq = pll_freq_arr[1];
	} else {
		div_fctr = RREG32(mmPSOC_PCI_PLL_DIV_FACTOR_1);
		div_sel = RREG32(mmPSOC_PCI_PLL_DIV_SEL_1);
		nr = RREG32(mmPSOC_PCI_PLL_NR);
		nf = RREG32(mmPSOC_PCI_PLL_NF);
		od = RREG32(mmPSOC_PCI_PLL_OD);

		if (div_sel == DIV_SEL_REF_CLK ||
				div_sel == DIV_SEL_DIVIDED_REF) {
			if (div_sel == DIV_SEL_REF_CLK)
				freq = PLL_REF_CLK;
			else
				freq = PLL_REF_CLK / (div_fctr + 1);
		} else if (div_sel == DIV_SEL_PLL_CLK ||
				div_sel == DIV_SEL_DIVIDED_PLL) {
			pll_clk = PLL_REF_CLK * (nf + 1) /
					((nr + 1) * (od + 1));
			if (div_sel == DIV_SEL_PLL_CLK)
				freq = pll_clk;
			else
				freq = pll_clk / (div_fctr + 1);
		} else {
			dev_warn(hdev->dev,
				"Received invalid div select value: %d",
				div_sel);
			freq = 0;
		}
	}

	prop->psoc_timestamp_frequency = freq;
	prop->psoc_pci_pll_nr = nr;
	prop->psoc_pci_pll_nf = nf;
	prop->psoc_pci_pll_od = od;
	prop->psoc_pci_pll_div_factor = div_fctr;
}

/*
 * goya_set_frequency - set the frequency of the device
 *
 * @hdev: pointer to habanalabs device structure
 * @freq: the new frequency value
 *
 * Change the frequency if needed. This function has no protection against
 * concurrency, therefore it is assumed that the calling function has protected
 * itself against the case of calling this function from multiple threads with
 * different values
 *
 * Returns 0 if no change was done, otherwise returns 1
 */
int goya_set_frequency(struct hl_device *hdev, enum hl_pll_frequency freq)
{
	struct goya_device *goya = hdev->asic_specific;

	if ((goya->pm_mng_profile == PM_MANUAL) ||
			(goya->curr_pll_profile == freq))
		return 0;

	dev_dbg(hdev->dev, "Changing device frequency to %s\n",
		freq == PLL_HIGH ? "high" : "low");

	goya_set_pll_profile(hdev, freq);

	goya->curr_pll_profile = freq;

	return 1;
}

static void goya_set_freq_to_low_job(struct work_struct *work)
{
	struct goya_work_freq *goya_work = container_of(work,
						struct goya_work_freq,
						work_freq.work);
	struct hl_device *hdev = goya_work->hdev;

	mutex_lock(&hdev->fpriv_list_lock);

	if (!hdev->is_compute_ctx_active)
		goya_set_frequency(hdev, PLL_LOW);

	mutex_unlock(&hdev->fpriv_list_lock);

	schedule_delayed_work(&goya_work->work_freq,
			usecs_to_jiffies(HL_PLL_LOW_JOB_FREQ_USEC));
}

int goya_late_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct goya_device *goya = hdev->asic_specific;
	int rc;

	goya_fetch_psoc_frequency(hdev);

	rc = goya_mmu_clear_pgt_range(hdev);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to clear MMU page tables range %d\n", rc);
		return rc;
	}

	rc = goya_mmu_set_dram_default_page(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to set DRAM default page %d\n", rc);
		return rc;
	}

	rc = goya_mmu_add_mappings_for_device_cpu(hdev);
	if (rc)
		return rc;

	rc = goya_init_cpu_queues(hdev);
	if (rc)
		return rc;

	rc = goya_test_cpu_queue(hdev);
	if (rc)
		return rc;

	rc = goya_cpucp_info_get(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to get cpucp info %d\n", rc);
		return rc;
	}

	/* Now that we have the DRAM size in ASIC prop, we need to check
	 * its size and configure the DMA_IF DDR wrap protection (which is in
	 * the MMU block) accordingly. The value is the log2 of the DRAM size
	 */
	WREG32(mmMMU_LOG2_DDR_SIZE, ilog2(prop->dram_size));

	rc = hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_ENABLE_PCI_ACCESS);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to enable PCI access from CPU %d\n", rc);
		return rc;
	}

	/* force setting to low frequency */
	goya->curr_pll_profile = PLL_LOW;

	goya->pm_mng_profile = PM_AUTO;

	goya_set_pll_profile(hdev, PLL_LOW);

	schedule_delayed_work(&goya->goya_work->work_freq,
		usecs_to_jiffies(HL_PLL_LOW_JOB_FREQ_USEC));

	return 0;
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
	struct goya_device *goya = hdev->asic_specific;
	int i = 0;

	cancel_delayed_work_sync(&goya->goya_work->work_freq);

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

static void goya_set_pci_memory_regions(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pci_mem_region *region;

	/* CFG */
	region = &hdev->pci_mem_region[PCI_REGION_CFG];
	region->region_base = CFG_BASE;
	region->region_size = CFG_SIZE;
	region->offset_in_bar = CFG_BASE - SRAM_BASE_ADDR;
	region->bar_size = CFG_BAR_SIZE;
	region->bar_id = SRAM_CFG_BAR_ID;
	region->used = 1;

	/* SRAM */
	region = &hdev->pci_mem_region[PCI_REGION_SRAM];
	region->region_base = SRAM_BASE_ADDR;
	region->region_size = SRAM_SIZE;
	region->offset_in_bar = 0;
	region->bar_size = CFG_BAR_SIZE;
	region->bar_id = SRAM_CFG_BAR_ID;
	region->used = 1;

	/* DRAM */
	region = &hdev->pci_mem_region[PCI_REGION_DRAM];
	region->region_base = DRAM_PHYS_BASE;
	region->region_size = hdev->asic_prop.dram_size;
	region->offset_in_bar = 0;
	region->bar_size = prop->dram_pci_bar_size;
	region->bar_id = DDR_BAR_ID;
	region->used = 1;
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
			hdev->asic_funcs->asic_dma_alloc_coherent(hdev,
					HL_CPU_ACCESSIBLE_MEM_SIZE,
					&hdev->cpu_accessible_dma_address,
					GFP_KERNEL | __GFP_ZERO);

	if (!hdev->cpu_accessible_dma_mem) {
		rc = -ENOMEM;
		goto free_dma_pool;
	}

	dev_dbg(hdev->dev, "cpu accessible memory at bus address %pad\n",
		&hdev->cpu_accessible_dma_address);

	hdev->cpu_accessible_dma_pool = gen_pool_create(ilog2(32), -1);
	if (!hdev->cpu_accessible_dma_pool) {
		dev_err(hdev->dev,
			"Failed to create CPU accessible DMA pool\n");
		rc = -ENOMEM;
		goto free_cpu_dma_mem;
	}

	rc = gen_pool_add(hdev->cpu_accessible_dma_pool,
				(uintptr_t) hdev->cpu_accessible_dma_mem,
				HL_CPU_ACCESSIBLE_MEM_SIZE, -1);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to add memory to CPU accessible DMA pool\n");
		rc = -EFAULT;
		goto free_cpu_accessible_dma_pool;
	}

	spin_lock_init(&goya->hw_queues_lock);
	hdev->supports_coresight = true;
	hdev->asic_prop.supports_soft_reset = true;
	hdev->asic_prop.allow_inference_soft_reset = true;
	hdev->supports_wait_for_multi_cs = false;

	hdev->asic_funcs->set_pci_memory_regions(hdev);

	goya->goya_work = kmalloc(sizeof(struct goya_work_freq), GFP_KERNEL);
	if (!goya->goya_work) {
		rc = -ENOMEM;
		goto free_cpu_accessible_dma_pool;
	}

	goya->goya_work->hdev = hdev;
	INIT_DELAYED_WORK(&goya->goya_work->work_freq, goya_set_freq_to_low_job);

	return 0;

free_cpu_accessible_dma_pool:
	gen_pool_destroy(hdev->cpu_accessible_dma_pool);
free_cpu_dma_mem:
	hdev->asic_funcs->asic_dma_free_coherent(hdev,
			HL_CPU_ACCESSIBLE_MEM_SIZE,
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

	hdev->asic_funcs->asic_dma_free_coherent(hdev,
			HL_CPU_ACCESSIBLE_MEM_SIZE,
			hdev->cpu_accessible_dma_mem,
			hdev->cpu_accessible_dma_address);

	dma_pool_destroy(hdev->dma_pool);

	kfree(goya->goya_work);
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
	u32 dma_err_cfg = QMAN_DMA_ERR_MSG_EN;

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

	if (hdev->stop_on_err)
		dma_err_cfg |= 1 << DMA_QM_0_GLBL_ERR_CFG_DMA_STOP_ON_ERR_SHIFT;

	WREG32(mmDMA_QM_0_GLBL_ERR_CFG + reg_off, dma_err_cfg);
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
void goya_init_dma_qmans(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	struct hl_hw_queue *q;
	int i;

	if (goya->hw_cap_initialized & HW_CAP_DMA)
		return;

	q = &hdev->kernel_queues[0];

	for (i = 0 ; i < NUMBER_OF_EXT_HW_QUEUES ; i++, q++) {
		q->cq_id = q->msi_vec = i;
		goya_init_dma_qman(hdev, i, q->bus_address);
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
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_DMA))
		return;

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

	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_DMA))
		return retval;

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
int goya_init_cpu_queues(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_eq *eq;
	u32 status;
	struct hl_hw_queue *cpu_pq = &hdev->kernel_queues[GOYA_QUEUE_ID_CPU_PQ];
	int err;

	if (!hdev->cpu_queues_enable)
		return 0;

	if (goya->hw_cap_initialized & HW_CAP_CPU_Q)
		return 0;

	eq = &hdev->event_queue;

	WREG32(mmCPU_PQ_BASE_ADDR_LOW, lower_32_bits(cpu_pq->bus_address));
	WREG32(mmCPU_PQ_BASE_ADDR_HIGH, upper_32_bits(cpu_pq->bus_address));

	WREG32(mmCPU_EQ_BASE_ADDR_LOW, lower_32_bits(eq->bus_address));
	WREG32(mmCPU_EQ_BASE_ADDR_HIGH, upper_32_bits(eq->bus_address));

	WREG32(mmCPU_CQ_BASE_ADDR_LOW,
			lower_32_bits(VA_CPU_ACCESSIBLE_MEM_ADDR));
	WREG32(mmCPU_CQ_BASE_ADDR_HIGH,
			upper_32_bits(VA_CPU_ACCESSIBLE_MEM_ADDR));

	WREG32(mmCPU_PQ_LENGTH, HL_QUEUE_SIZE_IN_BYTES);
	WREG32(mmCPU_EQ_LENGTH, HL_EQ_SIZE_IN_BYTES);
	WREG32(mmCPU_CQ_LENGTH, HL_CPU_ACCESSIBLE_MEM_SIZE);

	/* Used for EQ CI */
	WREG32(mmCPU_EQ_CI, 0);

	WREG32(mmCPU_IF_PF_PQ_PI, 0);

	WREG32(mmCPU_PQ_INIT_STATUS, PQ_INIT_STATUS_READY_FOR_CP);

	WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR,
			GOYA_ASYNC_EVENT_ID_PI_UPDATE);

	err = hl_poll_timeout(
		hdev,
		mmCPU_PQ_INIT_STATUS,
		status,
		(status == PQ_INIT_STATUS_READY_FOR_HOST),
		1000,
		GOYA_CPU_TIMEOUT_USEC);

	if (err) {
		dev_err(hdev->dev,
			"Failed to setup communication with device CPU\n");
		return -EIO;
	}

	/* update FW application security bits */
	if (prop->fw_cpu_boot_dev_sts0_valid)
		prop->fw_app_cpu_boot_dev_sts0 = RREG32(mmCPU_BOOT_DEV_STS0);

	if (prop->fw_cpu_boot_dev_sts1_valid)
		prop->fw_app_cpu_boot_dev_sts1 = RREG32(mmCPU_BOOT_DEV_STS1);

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

		WREG32_FIELD(TPC0_CFG_MSS_CONFIG, offset,
				ICACHE_FETCH_LINE_NUM, 2);
	}

	WREG32(mmDMA_NRTR_SCRAMB_EN, 1 << DMA_NRTR_SCRAMB_EN_VAL_SHIFT);
	WREG32(mmDMA_NRTR_NON_LIN_SCRAMB,
			1 << DMA_NRTR_NON_LIN_SCRAMB_EN_SHIFT);

	WREG32(mmPCI_NRTR_SCRAMB_EN, 1 << PCI_NRTR_SCRAMB_EN_VAL_SHIFT);
	WREG32(mmPCI_NRTR_NON_LIN_SCRAMB,
			1 << PCI_NRTR_NON_LIN_SCRAMB_EN_SHIFT);

	/*
	 * Workaround for H2 #HW-23 bug
	 * Set DMA max outstanding read requests to 240 on DMA CH 1.
	 * This limitation is still large enough to not affect Gen4 bandwidth.
	 * We need to only limit that DMA channel because the user can only read
	 * from Host using DMA CH 1
	 */
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

	mtr_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	mtr_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_MON_PAY_ADDRL_0);
	so_base_lo = lower_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE + mmSYNC_MNGR_SOB_OBJ_0);

	gic_base_lo =
		lower_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);
	gic_base_hi =
		upper_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR);

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

void goya_init_mme_qmans(struct hl_device *hdev)
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

void goya_init_tpc_qmans(struct hl_device *hdev)
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
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_MME))
		goto disable_tpc;

	WREG32(mmMME_QM_GLBL_CFG0, 0);
	WREG32(mmMME_CMDQ_GLBL_CFG0, 0);

disable_tpc:
	if (!(goya->hw_cap_initialized & HW_CAP_TPC))
		return;

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
	struct goya_device *goya = hdev->asic_specific;
	int rc, retval = 0;

	if (!(goya->hw_cap_initialized & HW_CAP_MME))
		goto stop_tpc;

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

stop_tpc:
	if (!(goya->hw_cap_initialized & HW_CAP_TPC))
		return retval;

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
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_DMA))
		return;

	WREG32(mmDMA_QM_0_GLBL_CFG1, 1 << DMA_QM_0_GLBL_CFG1_DMA_STOP_SHIFT);
	WREG32(mmDMA_QM_1_GLBL_CFG1, 1 << DMA_QM_1_GLBL_CFG1_DMA_STOP_SHIFT);
	WREG32(mmDMA_QM_2_GLBL_CFG1, 1 << DMA_QM_2_GLBL_CFG1_DMA_STOP_SHIFT);
	WREG32(mmDMA_QM_3_GLBL_CFG1, 1 << DMA_QM_3_GLBL_CFG1_DMA_STOP_SHIFT);
	WREG32(mmDMA_QM_4_GLBL_CFG1, 1 << DMA_QM_4_GLBL_CFG1_DMA_STOP_SHIFT);
}

static void goya_tpc_stall(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_TPC))
		return;

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
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_MME))
		return;

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

static void goya_enable_timestamp(struct hl_device *hdev)
{
	/* Disable the timestamp counter */
	WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE, 0);

	/* Zero the lower/upper parts of the 64-bit counter */
	WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE + 0xC, 0);
	WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE + 0x8, 0);

	/* Enable the counter */
	WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE, 1);
}

static void goya_disable_timestamp(struct hl_device *hdev)
{
	/* Disable the timestamp counter */
	WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE, 0);
}

static void goya_halt_engines(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	u32 wait_timeout_ms;

	if (hdev->pldm)
		wait_timeout_ms = GOYA_PLDM_RESET_WAIT_MSEC;
	else
		wait_timeout_ms = GOYA_RESET_WAIT_MSEC;

	goya_stop_external_queues(hdev);
	goya_stop_internal_queues(hdev);

	msleep(wait_timeout_ms);

	goya_dma_stall(hdev);
	goya_tpc_stall(hdev);
	goya_mme_stall(hdev);

	msleep(wait_timeout_ms);

	goya_disable_external_queues(hdev);
	goya_disable_internal_queues(hdev);

	goya_disable_timestamp(hdev);

	if (hard_reset) {
		goya_disable_msix(hdev);
		goya_mmu_remove_device_cpu_mappings(hdev);
	} else {
		goya_sync_irqs(hdev);
	}
}

/*
 * goya_load_firmware_to_device() - Load LINUX FW code to device.
 * @hdev: Pointer to hl_device structure.
 *
 * Copy LINUX fw code from firmware file to HBM BAR.
 *
 * Return: 0 on success, non-zero for failure.
 */
static int goya_load_firmware_to_device(struct hl_device *hdev)
{
	void __iomem *dst;

	dst = hdev->pcie_bar[DDR_BAR_ID] + LINUX_FW_OFFSET;

	return hl_fw_load_fw_to_device(hdev, GOYA_LINUX_FW_FILE, dst, 0, 0);
}

/*
 * goya_load_boot_fit_to_device() - Load boot fit to device.
 * @hdev: Pointer to hl_device structure.
 *
 * Copy boot fit file to SRAM BAR.
 *
 * Return: 0 on success, non-zero for failure.
 */
static int goya_load_boot_fit_to_device(struct hl_device *hdev)
{
	void __iomem *dst;

	dst = hdev->pcie_bar[SRAM_CFG_BAR_ID] + BOOT_FIT_SRAM_OFFSET;

	return hl_fw_load_fw_to_device(hdev, GOYA_BOOT_FIT_FILE, dst, 0, 0);
}

static void goya_init_dynamic_firmware_loader(struct hl_device *hdev)
{
	struct dynamic_fw_load_mgr *dynamic_loader;
	struct cpu_dyn_regs *dyn_regs;

	dynamic_loader = &hdev->fw_loader.dynamic_loader;

	/*
	 * here we update initial values for few specific dynamic regs (as
	 * before reading the first descriptor from FW those value has to be
	 * hard-coded) in later stages of the protocol those values will be
	 * updated automatically by reading the FW descriptor so data there
	 * will always be up-to-date
	 */
	dyn_regs = &dynamic_loader->comm_desc.cpu_dyn_regs;
	dyn_regs->kmd_msg_to_cpu =
				cpu_to_le32(mmPSOC_GLOBAL_CONF_KMD_MSG_TO_CPU);
	dyn_regs->cpu_cmd_status_to_host =
				cpu_to_le32(mmCPU_CMD_STATUS_TO_HOST);

	dynamic_loader->wait_for_bl_timeout = GOYA_WAIT_FOR_BL_TIMEOUT_USEC;
}

static void goya_init_static_firmware_loader(struct hl_device *hdev)
{
	struct static_fw_load_mgr *static_loader;

	static_loader = &hdev->fw_loader.static_loader;

	static_loader->preboot_version_max_off = SRAM_SIZE - VERSION_MAX_LEN;
	static_loader->boot_fit_version_max_off = SRAM_SIZE - VERSION_MAX_LEN;
	static_loader->kmd_msg_to_cpu_reg = mmPSOC_GLOBAL_CONF_KMD_MSG_TO_CPU;
	static_loader->cpu_cmd_status_to_host_reg = mmCPU_CMD_STATUS_TO_HOST;
	static_loader->cpu_boot_status_reg = mmPSOC_GLOBAL_CONF_CPU_BOOT_STATUS;
	static_loader->cpu_boot_dev_status0_reg = mmCPU_BOOT_DEV_STS0;
	static_loader->cpu_boot_dev_status1_reg = mmCPU_BOOT_DEV_STS1;
	static_loader->boot_err0_reg = mmCPU_BOOT_ERR0;
	static_loader->boot_err1_reg = mmCPU_BOOT_ERR1;
	static_loader->preboot_version_offset_reg = mmPREBOOT_VER_OFFSET;
	static_loader->boot_fit_version_offset_reg = mmUBOOT_VER_OFFSET;
	static_loader->sram_offset_mask = ~(lower_32_bits(SRAM_BASE_ADDR));
}

static void goya_init_firmware_loader(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct fw_load_mgr *fw_loader = &hdev->fw_loader;

	/* fill common fields */
	fw_loader->fw_comp_loaded = FW_TYPE_NONE;
	fw_loader->boot_fit_img.image_name = GOYA_BOOT_FIT_FILE;
	fw_loader->linux_img.image_name = GOYA_LINUX_FW_FILE;
	fw_loader->cpu_timeout = GOYA_CPU_TIMEOUT_USEC;
	fw_loader->boot_fit_timeout = GOYA_BOOT_FIT_REQ_TIMEOUT_USEC;
	fw_loader->skip_bmc = false;
	fw_loader->sram_bar_id = SRAM_CFG_BAR_ID;
	fw_loader->dram_bar_id = DDR_BAR_ID;

	if (prop->dynamic_fw_load)
		goya_init_dynamic_firmware_loader(hdev);
	else
		goya_init_static_firmware_loader(hdev);
}

static int goya_init_cpu(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	int rc;

	if (!(hdev->fw_components & FW_TYPE_PREBOOT_CPU))
		return 0;

	if (goya->hw_cap_initialized & HW_CAP_CPU)
		return 0;

	/*
	 * Before pushing u-boot/linux to device, need to set the ddr bar to
	 * base address of dram
	 */
	if (goya_set_ddr_bar_base(hdev, DRAM_PHYS_BASE) == U64_MAX) {
		dev_err(hdev->dev,
			"failed to map DDR bar to DRAM base address\n");
		return -EIO;
	}

	rc = hl_fw_init_cpu(hdev);

	if (rc)
		return rc;

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

int goya_mmu_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct goya_device *goya = hdev->asic_specific;
	u64 hop0_addr;
	int rc, i;

	if (!hdev->mmu_enable)
		return 0;

	if (goya->hw_cap_initialized & HW_CAP_MMU)
		return 0;

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

	hl_mmu_invalidate_cache(hdev, true, MMU_OP_USERPTR | MMU_OP_PHYS_PACK);

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
	int rc;

	/* Perform read from the device to make sure device is up */
	RREG32(mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG);

	/*
	 * Let's mark in the H/W that we have reached this point. We check
	 * this value in the reset_before_init function to understand whether
	 * we need to reset the chip before doing H/W init. This register is
	 * cleared by the H/W upon H/W reset
	 */
	WREG32(mmHW_STATE, HL_DEVICE_HW_STATE_DIRTY);

	rc = goya_init_cpu(hdev);
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
	if (goya_set_ddr_bar_base(hdev, (MMU_PAGE_TABLES_ADDR &
			~(prop->dram_pci_bar_size - 0x1ull))) == U64_MAX) {
		dev_err(hdev->dev,
			"failed to map DDR bar to MMU page tables\n");
		return -EIO;
	}

	rc = goya_mmu_init(hdev);
	if (rc)
		return rc;

	goya_init_security(hdev);

	goya_init_dma_qmans(hdev);

	goya_init_mme_qmans(hdev);

	goya_init_tpc_qmans(hdev);

	goya_enable_timestamp(hdev);

	/* MSI-X must be enabled before CPU queues are initialized */
	rc = goya_enable_msix(hdev);
	if (rc)
		goto disable_queues;

	/* Perform read from the device to flush all MSI-X configuration */
	RREG32(mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG);

	return 0;

disable_queues:
	goya_disable_internal_queues(hdev);
	goya_disable_external_queues(hdev);

	return rc;
}

static void goya_hw_fini(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct goya_device *goya = hdev->asic_specific;
	u32 reset_timeout_ms, cpu_timeout_ms, status;

	if (hdev->pldm) {
		reset_timeout_ms = GOYA_PLDM_RESET_TIMEOUT_MSEC;
		cpu_timeout_ms = GOYA_PLDM_RESET_WAIT_MSEC;
	} else {
		reset_timeout_ms = GOYA_RESET_TIMEOUT_MSEC;
		cpu_timeout_ms = GOYA_CPU_RESET_WAIT_MSEC;
	}

	if (hard_reset) {
		/* I don't know what is the state of the CPU so make sure it is
		 * stopped in any means necessary
		 */
		WREG32(mmPSOC_GLOBAL_CONF_UBOOT_MAGIC, KMD_MSG_GOTO_WFE);
		WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR,
			GOYA_ASYNC_EVENT_ID_HALT_MACHINE);

		msleep(cpu_timeout_ms);

		goya_set_ddr_bar_base(hdev, DRAM_PHYS_BASE);
		goya_disable_clk_rlx(hdev);
		goya_set_pll_refclk(hdev);

		WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST_CFG, RESET_ALL);
		dev_dbg(hdev->dev,
			"Issued HARD reset command, going to wait %dms\n",
			reset_timeout_ms);
	} else {
		WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST_CFG, DMA_MME_TPC_RESET);
		dev_dbg(hdev->dev,
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

	if (!hard_reset && goya) {
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

	if (goya) {
		goya->hw_cap_initialized &= ~(HW_CAP_CPU | HW_CAP_CPU_Q |
				HW_CAP_DDR_0 | HW_CAP_DDR_1 |
				HW_CAP_DMA | HW_CAP_MME |
				HW_CAP_MMU | HW_CAP_TPC_MBIST |
				HW_CAP_GOLDEN | HW_CAP_TPC);

		memset(goya->events_stat, 0, sizeof(goya->events_stat));
	}
}

int goya_suspend(struct hl_device *hdev)
{
	int rc;

	rc = hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_DISABLE_PCI_ACCESS);
	if (rc)
		dev_err(hdev->dev, "Failed to disable PCI access from CPU\n");

	return rc;
}

int goya_resume(struct hl_device *hdev)
{
	return goya_init_iatu(hdev);
}

static int goya_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
			void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	int rc;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP |
			VM_DONTCOPY | VM_NORESERVE;

	rc = dma_mmap_coherent(hdev->dev, vma, cpu_addr,
				(dma_addr - HOST_PHYS_BASE), size);
	if (rc)
		dev_err(hdev->dev, "dma_mmap_coherent error %d", rc);

	return rc;
}

void goya_ring_doorbell(struct hl_device *hdev, u32 hw_queue_id, u32 pi)
{
	u32 db_reg_offset, db_value;

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
		db_reg_offset = mmCPU_IF_PF_PQ_PI;
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
		/* Should never get here */
		dev_err(hdev->dev, "H/W queue %d is invalid. Can't set pi\n",
			hw_queue_id);
		return;
	}

	db_value = pi;

	/* ring the doorbell */
	WREG32(db_reg_offset, db_value);

	if (hw_queue_id == GOYA_QUEUE_ID_CPU_PQ) {
		/* make sure device CPU will read latest data from host */
		mb();
		WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR,
				GOYA_ASYNC_EVENT_ID_PI_UPDATE);
	}
}

void goya_pqe_write(struct hl_device *hdev, __le64 *pqe, struct hl_bd *bd)
{
	/* The QMANs are on the SRAM so need to copy to IO space */
	memcpy_toio((void __iomem *) pqe, bd, sizeof(struct hl_bd));
}

static void *goya_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	void *kernel_addr = dma_alloc_coherent(&hdev->pdev->dev, size,
						dma_handle, flags);

	/* Shift to the device's base physical address of host memory */
	if (kernel_addr)
		*dma_handle += HOST_PHYS_BASE;

	return kernel_addr;
}

static void goya_dma_free_coherent(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle)
{
	/* Cancel the device's base physical address of host memory */
	dma_addr_t fixed_dma_handle = dma_handle - HOST_PHYS_BASE;

	dma_free_coherent(&hdev->pdev->dev, size, cpu_addr, fixed_dma_handle);
}

int goya_scrub_device_mem(struct hl_device *hdev, u64 addr, u64 size)
{
	return 0;
}

void *goya_get_int_queue_base(struct hl_device *hdev, u32 queue_id,
				dma_addr_t *dma_handle,	u16 *queue_len)
{
	void *base;
	u32 offset;

	*dma_handle = hdev->asic_prop.sram_base_address;

	base = (__force void *) hdev->pcie_bar[SRAM_CFG_BAR_ID];

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
	struct packet_msg_prot *fence_pkt;
	u32 *fence_ptr;
	dma_addr_t fence_dma_addr;
	struct hl_cb *cb;
	u32 tmp, timeout;
	int rc;

	if (hdev->pldm)
		timeout = GOYA_PLDM_QMAN0_TIMEOUT_USEC;
	else
		timeout = HL_DEVICE_TIMEOUT_USEC;

	if (!hdev->asic_funcs->is_device_idle(hdev, NULL, 0, NULL)) {
		dev_err_ratelimited(hdev->dev,
			"Can't send driver job on QMAN0 because the device is not idle\n");
		return -EBUSY;
	}

	fence_ptr = hdev->asic_funcs->asic_dma_pool_zalloc(hdev, 4, GFP_KERNEL,
							&fence_dma_addr);
	if (!fence_ptr) {
		dev_err(hdev->dev,
			"Failed to allocate fence memory for QMAN0\n");
		return -ENOMEM;
	}

	goya_qman0_set_security(hdev, true);

	cb = job->patched_cb;

	fence_pkt = cb->kernel_address +
			job->job_cb_size - sizeof(struct packet_msg_prot);

	tmp = (PACKET_MSG_PROT << GOYA_PKT_CTL_OPCODE_SHIFT) |
			(1 << GOYA_PKT_CTL_EB_SHIFT) |
			(1 << GOYA_PKT_CTL_MB_SHIFT);
	fence_pkt->ctl = cpu_to_le32(tmp);
	fence_pkt->value = cpu_to_le32(GOYA_QMAN0_FENCE_VAL);
	fence_pkt->addr = cpu_to_le64(fence_dma_addr);

	rc = hl_hw_queue_send_cb_no_cmpl(hdev, GOYA_QUEUE_ID_DMA_0,
					job->job_cb_size, cb->bus_address);
	if (rc) {
		dev_err(hdev->dev, "Failed to send CB on QMAN0, %d\n", rc);
		goto free_fence_ptr;
	}

	rc = hl_poll_timeout_memory(hdev, fence_ptr, tmp,
				(tmp == GOYA_QMAN0_FENCE_VAL), 1000,
				timeout, true);

	hl_hw_queue_inc_ci_kernel(hdev, GOYA_QUEUE_ID_DMA_0);

	if (rc == -ETIMEDOUT) {
		dev_err(hdev->dev, "QMAN0 Job timeout (0x%x)\n", tmp);
		goto free_fence_ptr;
	}

free_fence_ptr:
	hdev->asic_funcs->asic_dma_pool_free(hdev, (void *) fence_ptr,
					fence_dma_addr);

	goya_qman0_set_security(hdev, false);

	return rc;
}

int goya_send_cpu_message(struct hl_device *hdev, u32 *msg, u16 len,
				u32 timeout, u64 *result)
{
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_CPU_Q)) {
		if (result)
			*result = 0;
		return 0;
	}

	if (!timeout)
		timeout = GOYA_MSG_TO_CPU_TIMEOUT_USEC;

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

	fence_ptr = hdev->asic_funcs->asic_dma_pool_zalloc(hdev, 4, GFP_KERNEL,
							&fence_dma_addr);
	if (!fence_ptr) {
		dev_err(hdev->dev,
			"Failed to allocate memory for H/W queue %d testing\n",
			hw_queue_id);
		return -ENOMEM;
	}

	*fence_ptr = 0;

	fence_pkt = hdev->asic_funcs->asic_dma_pool_zalloc(hdev,
					sizeof(struct packet_msg_prot),
					GFP_KERNEL, &pkt_dma_addr);
	if (!fence_pkt) {
		dev_err(hdev->dev,
			"Failed to allocate packet for H/W queue %d testing\n",
			hw_queue_id);
		rc = -ENOMEM;
		goto free_fence_ptr;
	}

	tmp = (PACKET_MSG_PROT << GOYA_PKT_CTL_OPCODE_SHIFT) |
			(1 << GOYA_PKT_CTL_EB_SHIFT) |
			(1 << GOYA_PKT_CTL_MB_SHIFT);
	fence_pkt->ctl = cpu_to_le32(tmp);
	fence_pkt->value = cpu_to_le32(fence_val);
	fence_pkt->addr = cpu_to_le64(fence_dma_addr);

	rc = hl_hw_queue_send_cb_no_cmpl(hdev, hw_queue_id,
					sizeof(struct packet_msg_prot),
					pkt_dma_addr);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to send fence packet to H/W queue %d\n",
			hw_queue_id);
		goto free_pkt;
	}

	rc = hl_poll_timeout_memory(hdev, fence_ptr, tmp, (tmp == fence_val),
					1000, GOYA_TEST_QUEUE_WAIT_USEC, true);

	hl_hw_queue_inc_ci_kernel(hdev, hw_queue_id);

	if (rc == -ETIMEDOUT) {
		dev_err(hdev->dev,
			"H/W queue %d test failed (scratch(0x%08llX) == 0x%08X)\n",
			hw_queue_id, (unsigned long long) fence_dma_addr, tmp);
		rc = -EIO;
	}

free_pkt:
	hdev->asic_funcs->asic_dma_pool_free(hdev, (void *) fence_pkt,
					pkt_dma_addr);
free_fence_ptr:
	hdev->asic_funcs->asic_dma_pool_free(hdev, (void *) fence_ptr,
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

	return ret_val;
}

static void *goya_dma_pool_zalloc(struct hl_device *hdev, size_t size,
					gfp_t mem_flags, dma_addr_t *dma_handle)
{
	void *kernel_addr;

	if (size > GOYA_DMA_POOL_BLK_SIZE)
		return NULL;

	kernel_addr =  dma_pool_zalloc(hdev->dma_pool, mem_flags, dma_handle);

	/* Shift to the device's base physical address of host memory */
	if (kernel_addr)
		*dma_handle += HOST_PHYS_BASE;

	return kernel_addr;
}

static void goya_dma_pool_free(struct hl_device *hdev, void *vaddr,
				dma_addr_t dma_addr)
{
	/* Cancel the device's base physical address of host memory */
	dma_addr_t fixed_dma_addr = dma_addr - HOST_PHYS_BASE;

	dma_pool_free(hdev->dma_pool, vaddr, fixed_dma_addr);
}

void *goya_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle)
{
	void *vaddr;

	vaddr = hl_fw_cpu_accessible_dma_pool_alloc(hdev, size, dma_handle);
	*dma_handle = (*dma_handle) - hdev->cpu_accessible_dma_address +
			VA_CPU_ACCESSIBLE_MEM_ADDR;

	return vaddr;
}

void goya_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size,
					void *vaddr)
{
	hl_fw_cpu_accessible_dma_pool_free(hdev, size, vaddr);
}

u32 goya_get_dma_desc_list_size(struct hl_device *hdev, struct sg_table *sgt)
{
	struct scatterlist *sg, *sg_next_iter;
	u32 count, dma_desc_cnt;
	u64 len, len_next;
	dma_addr_t addr, addr_next;

	dma_desc_cnt = 0;

	for_each_sgtable_dma_sg(sgt, sg, count) {
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

	userptr = kzalloc(sizeof(*userptr), GFP_KERNEL);
	if (!userptr)
		return -ENOMEM;

	rc = hl_pin_host_memory(hdev, addr, le32_to_cpu(user_dma_pkt->tsize),
				userptr);
	if (rc)
		goto free_userptr;

	list_add_tail(&userptr->job_node, parser->job_userptr_list);

	rc = hdev->asic_funcs->asic_dma_map_sgtable(hdev, userptr->sgt, dir);
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
	list_del(&userptr->job_node);
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
	dev_dbg(hdev->dev, "source == 0x%llx\n",
		le64_to_cpu(user_dma_pkt->src_addr));
	dev_dbg(hdev->dev, "destination == 0x%llx\n",
		le64_to_cpu(user_dma_pkt->dst_addr));
	dev_dbg(hdev->dev, "size == %u\n", le32_to_cpu(user_dma_pkt->tsize));

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
	dev_dbg(hdev->dev, "source == 0x%llx\n",
		le64_to_cpu(user_dma_pkt->src_addr));
	dev_dbg(hdev->dev, "destination == 0x%llx\n",
		le64_to_cpu(user_dma_pkt->dst_addr));
	dev_dbg(hdev->dev, "size == %u\n", le32_to_cpu(user_dma_pkt->tsize));

	/*
	 * WA for HW-23.
	 * We can't allow user to read from Host using QMANs other than 1.
	 * PMMU and HPMMU addresses are equal, check only one of them.
	 */
	if (parser->hw_queue_id != GOYA_QUEUE_ID_DMA_1 &&
		hl_mem_area_inside_range(le64_to_cpu(user_dma_pkt->src_addr),
				le32_to_cpu(user_dma_pkt->tsize),
				hdev->asic_prop.pmmu.start_addr,
				hdev->asic_prop.pmmu.end_addr)) {
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
	dev_dbg(hdev->dev, "value      == 0x%x\n",
		le32_to_cpu(wreg_pkt->value));

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
		struct goya_packet *user_pkt;

		user_pkt = parser->user_cb->kernel_address + cb_parsed_length;

		pkt_id = (enum packet_id) (
				(le64_to_cpu(user_pkt->header) &
				PACKET_HEADER_PACKET_ID_MASK) >>
					PACKET_HEADER_PACKET_ID_SHIFT);

		if (!validate_packet_id(pkt_id)) {
			dev_err(hdev->dev, "Invalid packet id %u\n", pkt_id);
			rc = -EINVAL;
			break;
		}

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
			rc = goya_validate_wreg32(hdev,
				parser, (struct packet_wreg32 *) user_pkt);
			parser->patched_cb_size += pkt_size;
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
					(struct packet_lin_dma *) user_pkt);
			else
				rc = goya_validate_dma_pkt_no_mmu(hdev, parser,
					(struct packet_lin_dma *) user_pkt);
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

	for_each_sgtable_dma_sg(sgt, sg, count) {
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
		struct goya_packet *user_pkt, *kernel_pkt;

		user_pkt = parser->user_cb->kernel_address + cb_parsed_length;
		kernel_pkt = parser->patched_cb->kernel_address +
					cb_patched_cur_length;

		pkt_id = (enum packet_id) (
				(le64_to_cpu(user_pkt->header) &
				PACKET_HEADER_PACKET_ID_MASK) >>
					PACKET_HEADER_PACKET_ID_SHIFT);

		if (!validate_packet_id(pkt_id)) {
			dev_err(hdev->dev, "Invalid packet id %u\n", pkt_id);
			rc = -EINVAL;
			break;
		}

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
			rc = goya_patch_dma_packet(hdev, parser,
					(struct packet_lin_dma *) user_pkt,
					(struct packet_lin_dma *) kernel_pkt,
					&new_pkt_size);
			cb_patched_cur_length += new_pkt_size;
			break;

		case PACKET_WREG_32:
			memcpy(kernel_pkt, user_pkt, pkt_size);
			cb_patched_cur_length += pkt_size;
			rc = goya_validate_wreg32(hdev, parser,
					(struct packet_wreg32 *) kernel_pkt);
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
	u64 handle;
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

	rc = hl_cb_create(hdev, &hdev->kernel_mem_mgr, hdev->kernel_ctx,
				parser->patched_cb_size, false, false,
				&handle);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to allocate patched CB for DMA CS %d\n",
			rc);
		return rc;
	}

	parser->patched_cb = hl_cb_get(&hdev->kernel_mem_mgr, handle);
	/* hl_cb_get should never fail here */
	if (!parser->patched_cb) {
		dev_crit(hdev->dev, "DMA CB handle invalid 0x%llx\n", handle);
		rc = -EFAULT;
		goto out;
	}

	/*
	 * The check that parser->user_cb_size <= parser->user_cb->size was done
	 * in validate_queue_index().
	 */
	memcpy(parser->patched_cb->kernel_address,
		parser->user_cb->kernel_address,
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
	hl_cb_destroy(&hdev->kernel_mem_mgr, handle);

	return rc;
}

static int goya_parse_cb_no_mmu(struct hl_device *hdev,
				struct hl_cs_parser *parser)
{
	u64 handle;
	int rc;

	rc = goya_validate_cb(hdev, parser, false);

	if (rc)
		goto free_userptr;

	rc = hl_cb_create(hdev, &hdev->kernel_mem_mgr, hdev->kernel_ctx,
				parser->patched_cb_size, false, false,
				&handle);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to allocate patched CB for DMA CS %d\n", rc);
		goto free_userptr;
	}

	parser->patched_cb = hl_cb_get(&hdev->kernel_mem_mgr, handle);
	/* hl_cb_get should never fail here */
	if (!parser->patched_cb) {
		dev_crit(hdev->dev, "DMA CB handle invalid 0x%llx\n", handle);
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
	hl_cb_destroy(&hdev->kernel_mem_mgr, handle);

free_userptr:
	if (rc)
		hl_userptr_delete_list(hdev, parser->job_userptr_list);
	return rc;
}

static int goya_parse_cb_no_ext_queue(struct hl_device *hdev,
					struct hl_cs_parser *parser)
{
	struct asic_fixed_properties *asic_prop = &hdev->asic_prop;
	struct goya_device *goya = hdev->asic_specific;

	if (goya->hw_cap_initialized & HW_CAP_MMU)
		return 0;

	/* For internal queue jobs, just check if CB address is valid */
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
		"Internal CB address 0x%px + 0x%x is not in SRAM nor in DRAM\n",
		parser->user_cb, parser->user_cb_size);

	return -EFAULT;
}

int goya_cs_parser(struct hl_device *hdev, struct hl_cs_parser *parser)
{
	struct goya_device *goya = hdev->asic_specific;

	if (parser->queue_type == QUEUE_TYPE_INT)
		return goya_parse_cb_no_ext_queue(hdev, parser);

	if (goya->hw_cap_initialized & HW_CAP_MMU)
		return goya_parse_cb_mmu(hdev, parser);
	else
		return goya_parse_cb_no_mmu(hdev, parser);
}

void goya_add_end_of_cb_packets(struct hl_device *hdev, void *kernel_address,
				u32 len, u64 cq_addr, u32 cq_val, u32 msix_vec,
				bool eb)
{
	struct packet_msg_prot *cq_pkt;
	u32 tmp;

	cq_pkt = kernel_address + len - (sizeof(struct packet_msg_prot) * 2);

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

void goya_update_eq_ci(struct hl_device *hdev, u32 val)
{
	WREG32(mmCPU_EQ_CI, val);
}

void goya_restore_phase_topology(struct hl_device *hdev)
{

}

static void goya_clear_sm_regs(struct hl_device *hdev)
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

static int goya_debugfs_read_dma(struct hl_device *hdev, u64 addr, u32 size, void *blob_addr)
{
	dev_err(hdev->dev, "Reading via DMA is unimplemented yet\n");
	return -EPERM;
}

static u64 goya_read_pte(struct hl_device *hdev, u64 addr)
{
	struct goya_device *goya = hdev->asic_specific;

	if (hdev->reset_info.hard_reset_pending)
		return U64_MAX;

	return readq(hdev->pcie_bar[DDR_BAR_ID] +
			(addr - goya->ddr_bar_cur_addr));
}

static void goya_write_pte(struct hl_device *hdev, u64 addr, u64 val)
{
	struct goya_device *goya = hdev->asic_specific;

	if (hdev->reset_info.hard_reset_pending)
		return;

	writeq(val, hdev->pcie_bar[DDR_BAR_ID] +
			(addr - goya->ddr_bar_cur_addr));
}

static const char *_goya_get_event_desc(u16 event_type)
{
	switch (event_type) {
	case GOYA_ASYNC_EVENT_ID_PCIE_IF:
		return "PCIe_if";
	case GOYA_ASYNC_EVENT_ID_TPC0_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC1_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC2_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC3_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC4_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC5_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC6_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC7_ECC:
		return "TPC%d_ecc";
	case GOYA_ASYNC_EVENT_ID_MME_ECC:
		return "MME_ecc";
	case GOYA_ASYNC_EVENT_ID_MME_ECC_EXT:
		return "MME_ecc_ext";
	case GOYA_ASYNC_EVENT_ID_MMU_ECC:
		return "MMU_ecc";
	case GOYA_ASYNC_EVENT_ID_DMA_MACRO:
		return "DMA_macro";
	case GOYA_ASYNC_EVENT_ID_DMA_ECC:
		return "DMA_ecc";
	case GOYA_ASYNC_EVENT_ID_CPU_IF_ECC:
		return "CPU_if_ecc";
	case GOYA_ASYNC_EVENT_ID_PSOC_MEM:
		return "PSOC_mem";
	case GOYA_ASYNC_EVENT_ID_PSOC_CORESIGHT:
		return "PSOC_coresight";
	case GOYA_ASYNC_EVENT_ID_SRAM0 ... GOYA_ASYNC_EVENT_ID_SRAM29:
		return "SRAM%d";
	case GOYA_ASYNC_EVENT_ID_GIC500:
		return "GIC500";
	case GOYA_ASYNC_EVENT_ID_PLL0 ... GOYA_ASYNC_EVENT_ID_PLL6:
		return "PLL%d";
	case GOYA_ASYNC_EVENT_ID_AXI_ECC:
		return "AXI_ecc";
	case GOYA_ASYNC_EVENT_ID_L2_RAM_ECC:
		return "L2_ram_ecc";
	case GOYA_ASYNC_EVENT_ID_PSOC_GPIO_05_SW_RESET:
		return "PSOC_gpio_05_sw_reset";
	case GOYA_ASYNC_EVENT_ID_PSOC_GPIO_10_VRHOT_ICRIT:
		return "PSOC_gpio_10_vrhot_icrit";
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
	case GOYA_ASYNC_EVENT_ID_TPC0_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC1_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC2_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC3_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC4_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC5_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC6_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC7_BMON_SPMU:
		return "TPC%d_bmon_spmu";
	case GOYA_ASYNC_EVENT_ID_DMA_BM_CH0 ... GOYA_ASYNC_EVENT_ID_DMA_BM_CH4:
		return "DMA_bm_ch%d";
	case GOYA_ASYNC_EVENT_ID_FIX_POWER_ENV_S:
		return "POWER_ENV_S";
	case GOYA_ASYNC_EVENT_ID_FIX_POWER_ENV_E:
		return "POWER_ENV_E";
	case GOYA_ASYNC_EVENT_ID_FIX_THERMAL_ENV_S:
		return "THERMAL_ENV_S";
	case GOYA_ASYNC_EVENT_ID_FIX_THERMAL_ENV_E:
		return "THERMAL_ENV_E";
	case GOYA_ASYNC_EVENT_PKT_QUEUE_OUT_SYNC:
		return "QUEUE_OUT_OF_SYNC";
	default:
		return "N/A";
	}
}

static void goya_get_event_desc(u16 event_type, char *desc, size_t size)
{
	u8 index;

	switch (event_type) {
	case GOYA_ASYNC_EVENT_ID_TPC0_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC1_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC2_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC3_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC4_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC5_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC6_ECC:
	case GOYA_ASYNC_EVENT_ID_TPC7_ECC:
		index = (event_type - GOYA_ASYNC_EVENT_ID_TPC0_ECC) / 3;
		snprintf(desc, size, _goya_get_event_desc(event_type), index);
		break;
	case GOYA_ASYNC_EVENT_ID_SRAM0 ... GOYA_ASYNC_EVENT_ID_SRAM29:
		index = event_type - GOYA_ASYNC_EVENT_ID_SRAM0;
		snprintf(desc, size, _goya_get_event_desc(event_type), index);
		break;
	case GOYA_ASYNC_EVENT_ID_PLL0 ... GOYA_ASYNC_EVENT_ID_PLL6:
		index = event_type - GOYA_ASYNC_EVENT_ID_PLL0;
		snprintf(desc, size, _goya_get_event_desc(event_type), index);
		break;
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
	case GOYA_ASYNC_EVENT_ID_TPC0_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC1_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC2_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC3_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC4_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC5_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC6_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC7_BMON_SPMU:
		index = (event_type - GOYA_ASYNC_EVENT_ID_TPC0_BMON_SPMU) / 10;
		snprintf(desc, size, _goya_get_event_desc(event_type), index);
		break;
	case GOYA_ASYNC_EVENT_ID_DMA_BM_CH0 ... GOYA_ASYNC_EVENT_ID_DMA_BM_CH4:
		index = event_type - GOYA_ASYNC_EVENT_ID_DMA_BM_CH0;
		snprintf(desc, size, _goya_get_event_desc(event_type), index);
		break;
	case GOYA_ASYNC_EVENT_PKT_QUEUE_OUT_SYNC:
		snprintf(desc, size, _goya_get_event_desc(event_type));
		break;
	default:
		snprintf(desc, size, _goya_get_event_desc(event_type));
		break;
	}
}

static void goya_print_razwi_info(struct hl_device *hdev)
{
	if (RREG32(mmDMA_MACRO_RAZWI_LBW_WT_VLD)) {
		dev_err_ratelimited(hdev->dev, "Illegal write to LBW\n");
		WREG32(mmDMA_MACRO_RAZWI_LBW_WT_VLD, 0);
	}

	if (RREG32(mmDMA_MACRO_RAZWI_LBW_RD_VLD)) {
		dev_err_ratelimited(hdev->dev, "Illegal read from LBW\n");
		WREG32(mmDMA_MACRO_RAZWI_LBW_RD_VLD, 0);
	}

	if (RREG32(mmDMA_MACRO_RAZWI_HBW_WT_VLD)) {
		dev_err_ratelimited(hdev->dev, "Illegal write to HBW\n");
		WREG32(mmDMA_MACRO_RAZWI_HBW_WT_VLD, 0);
	}

	if (RREG32(mmDMA_MACRO_RAZWI_HBW_RD_VLD)) {
		dev_err_ratelimited(hdev->dev, "Illegal read from HBW\n");
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

		dev_err_ratelimited(hdev->dev, "MMU page fault on va 0x%llx\n",
					addr);

		WREG32(mmMMU_PAGE_ERROR_CAPTURE, 0);
	}
}

static void goya_print_out_of_sync_info(struct hl_device *hdev,
					struct cpucp_pkt_sync_err *sync_err)
{
	struct hl_hw_queue *q = &hdev->kernel_queues[GOYA_QUEUE_ID_CPU_PQ];

	dev_err(hdev->dev, "Out of sync with FW, FW: pi=%u, ci=%u, LKD: pi=%u, ci=%u\n",
			sync_err->pi, sync_err->ci, q->pi, atomic_read(&q->ci));
}

static void goya_print_irq_info(struct hl_device *hdev, u16 event_type,
				bool razwi)
{
	char desc[20] = "";

	goya_get_event_desc(event_type, desc, sizeof(desc));
	dev_err_ratelimited(hdev->dev, "Received H/W interrupt %d [\"%s\"]\n",
		event_type, desc);

	if (razwi) {
		goya_print_razwi_info(hdev);
		goya_print_mmu_error_info(hdev);
	}
}

static int goya_unmask_irq_arr(struct hl_device *hdev, u32 *irq_arr,
		size_t irq_arr_size)
{
	struct cpucp_unmask_irq_arr_packet *pkt;
	size_t total_pkt_size;
	u64 result;
	int rc;
	int irq_num_entries, irq_arr_index;
	__le32 *goya_irq_arr;

	total_pkt_size = sizeof(struct cpucp_unmask_irq_arr_packet) +
			irq_arr_size;

	/* data should be aligned to 8 bytes in order to CPU-CP to copy it */
	total_pkt_size = (total_pkt_size + 0x7) & ~0x7;

	/* total_pkt_size is casted to u16 later on */
	if (total_pkt_size > USHRT_MAX) {
		dev_err(hdev->dev, "too many elements in IRQ array\n");
		return -EINVAL;
	}

	pkt = kzalloc(total_pkt_size, GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	irq_num_entries = irq_arr_size / sizeof(irq_arr[0]);
	pkt->length = cpu_to_le32(irq_num_entries);

	/* We must perform any necessary endianness conversation on the irq
	 * array being passed to the goya hardware
	 */
	for (irq_arr_index = 0, goya_irq_arr = (__le32 *) &pkt->irqs;
			irq_arr_index < irq_num_entries ; irq_arr_index++)
		goya_irq_arr[irq_arr_index] =
				cpu_to_le32(irq_arr[irq_arr_index]);

	pkt->cpucp_pkt.ctl = cpu_to_le32(CPUCP_PACKET_UNMASK_RAZWI_IRQ_ARRAY <<
						CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) pkt,
						total_pkt_size,	0, &result);

	if (rc)
		dev_err(hdev->dev, "failed to unmask IRQ array\n");

	kfree(pkt);

	return rc;
}

static int goya_non_hard_reset_late_init(struct hl_device *hdev)
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
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_UNMASK_RAZWI_IRQ <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(event_type);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, &result);

	if (rc)
		dev_err(hdev->dev, "failed to unmask RAZWI IRQ %d", event_type);

	return rc;
}

static void goya_print_clk_change_info(struct hl_device *hdev, u16 event_type)
{
	ktime_t zero_time = ktime_set(0, 0);

	mutex_lock(&hdev->clk_throttling.lock);

	switch (event_type) {
	case GOYA_ASYNC_EVENT_ID_FIX_POWER_ENV_S:
		hdev->clk_throttling.current_reason |= HL_CLK_THROTTLE_POWER;
		hdev->clk_throttling.aggregated_reason |= HL_CLK_THROTTLE_POWER;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_POWER].start = ktime_get();
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_POWER].end = zero_time;
		dev_info_ratelimited(hdev->dev,
			"Clock throttling due to power consumption\n");
		break;

	case GOYA_ASYNC_EVENT_ID_FIX_POWER_ENV_E:
		hdev->clk_throttling.current_reason &= ~HL_CLK_THROTTLE_POWER;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_POWER].end = ktime_get();
		dev_info_ratelimited(hdev->dev,
			"Power envelop is safe, back to optimal clock\n");
		break;

	case GOYA_ASYNC_EVENT_ID_FIX_THERMAL_ENV_S:
		hdev->clk_throttling.current_reason |= HL_CLK_THROTTLE_THERMAL;
		hdev->clk_throttling.aggregated_reason |= HL_CLK_THROTTLE_THERMAL;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_THERMAL].start = ktime_get();
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_THERMAL].end = zero_time;
		dev_info_ratelimited(hdev->dev,
			"Clock throttling due to overheating\n");
		break;

	case GOYA_ASYNC_EVENT_ID_FIX_THERMAL_ENV_E:
		hdev->clk_throttling.current_reason &= ~HL_CLK_THROTTLE_THERMAL;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_THERMAL].end = ktime_get();
		dev_info_ratelimited(hdev->dev,
			"Thermal envelop is safe, back to optimal clock\n");
		break;

	default:
		dev_err(hdev->dev, "Received invalid clock change event %d\n",
			event_type);
		break;
	}

	mutex_unlock(&hdev->clk_throttling.lock);
}

void goya_handle_eqe(struct hl_device *hdev, struct hl_eq_entry *eq_entry)
{
	u32 ctl = le32_to_cpu(eq_entry->hdr.ctl);
	u16 event_type = ((ctl & EQ_CTL_EVENT_TYPE_MASK)
				>> EQ_CTL_EVENT_TYPE_SHIFT);
	struct goya_device *goya = hdev->asic_specific;

	if (event_type >= GOYA_ASYNC_EVENT_ID_SIZE) {
		dev_err(hdev->dev, "Event type %u exceeds maximum of %u",
				event_type, GOYA_ASYNC_EVENT_ID_SIZE - 1);
		return;
	}

	goya->events_stat[event_type]++;
	goya->events_stat_aggregate[event_type]++;

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
	case GOYA_ASYNC_EVENT_ID_PLL0 ... GOYA_ASYNC_EVENT_ID_PLL6:
	case GOYA_ASYNC_EVENT_ID_AXI_ECC:
	case GOYA_ASYNC_EVENT_ID_L2_RAM_ECC:
		goya_print_irq_info(hdev, event_type, false);
		if (hdev->hard_reset_on_fw_events)
			hl_device_reset(hdev, (HL_DRV_RESET_HARD |
						HL_DRV_RESET_FW_FATAL_ERR));
		break;

	case GOYA_ASYNC_EVENT_ID_PSOC_GPIO_05_SW_RESET:
		goya_print_irq_info(hdev, event_type, false);
		if (hdev->hard_reset_on_fw_events)
			hl_device_reset(hdev, HL_DRV_RESET_HARD);
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
		goya_print_irq_info(hdev, event_type, true);
		goya_unmask_irq(hdev, event_type);
		break;

	case GOYA_ASYNC_EVENT_ID_PSOC_GPIO_10_VRHOT_ICRIT:
	case GOYA_ASYNC_EVENT_ID_TPC0_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC1_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC2_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC3_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC4_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC5_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC6_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_TPC7_BMON_SPMU:
	case GOYA_ASYNC_EVENT_ID_DMA_BM_CH0 ... GOYA_ASYNC_EVENT_ID_DMA_BM_CH4:
		goya_print_irq_info(hdev, event_type, false);
		goya_unmask_irq(hdev, event_type);
		break;

	case GOYA_ASYNC_EVENT_ID_FIX_POWER_ENV_S:
	case GOYA_ASYNC_EVENT_ID_FIX_POWER_ENV_E:
	case GOYA_ASYNC_EVENT_ID_FIX_THERMAL_ENV_S:
	case GOYA_ASYNC_EVENT_ID_FIX_THERMAL_ENV_E:
		goya_print_clk_change_info(hdev, event_type);
		goya_unmask_irq(hdev, event_type);
		break;

	case GOYA_ASYNC_EVENT_PKT_QUEUE_OUT_SYNC:
		goya_print_irq_info(hdev, event_type, false);
		goya_print_out_of_sync_info(hdev, &eq_entry->pkt_sync_err);
		if (hdev->hard_reset_on_fw_events)
			hl_device_reset(hdev, HL_DRV_RESET_HARD);
		else
			hl_fw_unmask_irq(hdev, event_type);
		break;

	default:
		dev_err(hdev->dev, "Received invalid H/W interrupt %d\n",
				event_type);
		break;
	}
}

void *goya_get_events_stat(struct hl_device *hdev, bool aggregate, u32 *size)
{
	struct goya_device *goya = hdev->asic_specific;

	if (aggregate) {
		*size = (u32) sizeof(goya->events_stat_aggregate);
		return goya->events_stat_aggregate;
	}

	*size = (u32) sizeof(goya->events_stat);
	return goya->events_stat;
}

static int goya_memset_device_memory(struct hl_device *hdev, u64 addr, u64 size,
				u64 val, bool is_dram)
{
	struct packet_lin_dma *lin_dma_pkt;
	struct hl_cs_job *job;
	u32 cb_size, ctl;
	struct hl_cb *cb;
	int rc, lin_dma_pkts_cnt;

	lin_dma_pkts_cnt = DIV_ROUND_UP_ULL(size, SZ_2G);
	cb_size = lin_dma_pkts_cnt * sizeof(struct packet_lin_dma) +
						sizeof(struct packet_msg_prot);
	cb = hl_cb_kernel_create(hdev, cb_size, false);
	if (!cb)
		return -ENOMEM;

	lin_dma_pkt = cb->kernel_address;

	do {
		memset(lin_dma_pkt, 0, sizeof(*lin_dma_pkt));

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
		if (lin_dma_pkts_cnt > 1)
			lin_dma_pkt->tsize = cpu_to_le32(SZ_2G);
		else
			lin_dma_pkt->tsize = cpu_to_le32(size);

		size -= SZ_2G;
		addr += SZ_2G;
		lin_dma_pkt++;
	} while (--lin_dma_pkts_cnt);

	job = hl_cs_allocate_job(hdev, QUEUE_TYPE_EXT, true);
	if (!job) {
		dev_err(hdev->dev, "Failed to allocate a new job\n");
		rc = -ENOMEM;
		goto release_cb;
	}

	job->id = 0;
	job->user_cb = cb;
	atomic_inc(&job->user_cb->cs_cnt);
	job->user_cb_size = cb_size;
	job->hw_queue_id = GOYA_QUEUE_ID_DMA_0;
	job->patched_cb = job->user_cb;
	job->job_cb_size = job->user_cb_size;

	hl_debugfs_add_job(hdev, job);

	rc = goya_send_job_on_qman0(hdev, job);

	hl_debugfs_remove_job(hdev, job);
	kfree(job);
	atomic_dec(&cb->cs_cnt);

release_cb:
	hl_cb_put(cb);
	hl_cb_destroy(&hdev->kernel_mem_mgr, cb->buf->handle);

	return rc;
}

int goya_context_switch(struct hl_device *hdev, u32 asid)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 addr = prop->sram_base_address, sob_addr;
	u32 size = hdev->pldm ? 0x10000 : prop->sram_size;
	u64 val = 0x7777777777777777ull;
	int rc, dma_id;
	u32 channel_off = mmDMA_CH_1_WR_COMP_ADDR_LO -
					mmDMA_CH_0_WR_COMP_ADDR_LO;

	rc = goya_memset_device_memory(hdev, addr, size, val, false);
	if (rc) {
		dev_err(hdev->dev, "Failed to clear SRAM in context switch\n");
		return rc;
	}

	/* we need to reset registers that the user is allowed to change */
	sob_addr = CFG_BASE + mmSYNC_MNGR_SOB_OBJ_1007;
	WREG32(mmDMA_CH_0_WR_COMP_ADDR_LO, lower_32_bits(sob_addr));

	for (dma_id = 1 ; dma_id < NUMBER_OF_EXT_HW_QUEUES ; dma_id++) {
		sob_addr = CFG_BASE + mmSYNC_MNGR_SOB_OBJ_1000 +
							(dma_id - 1) * 4;
		WREG32(mmDMA_CH_0_WR_COMP_ADDR_LO + channel_off * dma_id,
						lower_32_bits(sob_addr));
	}

	WREG32(mmTPC_PLL_CLK_RLX_0, 0x200020);

	goya_clear_sm_regs(hdev);

	return 0;
}

static int goya_mmu_clear_pgt_range(struct hl_device *hdev)
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

static int goya_mmu_set_dram_default_page(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	u64 addr = hdev->asic_prop.mmu_dram_default_page_addr;
	u32 size = MMU_DRAM_DEFAULT_PAGE_SIZE;
	u64 val = 0x9999999999999999ull;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU))
		return 0;

	return goya_memset_device_memory(hdev, addr, size, val, true);
}

static int goya_mmu_add_mappings_for_device_cpu(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct goya_device *goya = hdev->asic_specific;
	s64 off, cpu_off;
	int rc;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU))
		return 0;

	for (off = 0 ; off < CPU_FW_IMAGE_SIZE ; off += PAGE_SIZE_2MB) {
		rc = hl_mmu_map_page(hdev->kernel_ctx,
			prop->dram_base_address + off,
			prop->dram_base_address + off, PAGE_SIZE_2MB,
			(off + PAGE_SIZE_2MB) == CPU_FW_IMAGE_SIZE);
		if (rc) {
			dev_err(hdev->dev, "Map failed for address 0x%llx\n",
				prop->dram_base_address + off);
			goto unmap;
		}
	}

	if (!(hdev->cpu_accessible_dma_address & (PAGE_SIZE_2MB - 1))) {
		rc = hl_mmu_map_page(hdev->kernel_ctx,
			VA_CPU_ACCESSIBLE_MEM_ADDR,
			hdev->cpu_accessible_dma_address,
			PAGE_SIZE_2MB, true);

		if (rc) {
			dev_err(hdev->dev,
				"Map failed for CPU accessible memory\n");
			off -= PAGE_SIZE_2MB;
			goto unmap;
		}
	} else {
		for (cpu_off = 0 ; cpu_off < SZ_2M ; cpu_off += PAGE_SIZE_4KB) {
			rc = hl_mmu_map_page(hdev->kernel_ctx,
				VA_CPU_ACCESSIBLE_MEM_ADDR + cpu_off,
				hdev->cpu_accessible_dma_address + cpu_off,
				PAGE_SIZE_4KB, true);
			if (rc) {
				dev_err(hdev->dev,
					"Map failed for CPU accessible memory\n");
				cpu_off -= PAGE_SIZE_4KB;
				goto unmap_cpu;
			}
		}
	}

	goya_mmu_prepare_reg(hdev, mmCPU_IF_ARUSER_OVR, HL_KERNEL_ASID_ID);
	goya_mmu_prepare_reg(hdev, mmCPU_IF_AWUSER_OVR, HL_KERNEL_ASID_ID);
	WREG32(mmCPU_IF_ARUSER_OVR_EN, 0x7FF);
	WREG32(mmCPU_IF_AWUSER_OVR_EN, 0x7FF);

	/* Make sure configuration is flushed to device */
	RREG32(mmCPU_IF_AWUSER_OVR_EN);

	goya->device_cpu_mmu_mappings_done = true;

	return 0;

unmap_cpu:
	for (; cpu_off >= 0 ; cpu_off -= PAGE_SIZE_4KB)
		if (hl_mmu_unmap_page(hdev->kernel_ctx,
				VA_CPU_ACCESSIBLE_MEM_ADDR + cpu_off,
				PAGE_SIZE_4KB, true))
			dev_warn_ratelimited(hdev->dev,
				"failed to unmap address 0x%llx\n",
				VA_CPU_ACCESSIBLE_MEM_ADDR + cpu_off);
unmap:
	for (; off >= 0 ; off -= PAGE_SIZE_2MB)
		if (hl_mmu_unmap_page(hdev->kernel_ctx,
				prop->dram_base_address + off, PAGE_SIZE_2MB,
				true))
			dev_warn_ratelimited(hdev->dev,
				"failed to unmap address 0x%llx\n",
				prop->dram_base_address + off);

	return rc;
}

void goya_mmu_remove_device_cpu_mappings(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct goya_device *goya = hdev->asic_specific;
	u32 off, cpu_off;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU))
		return;

	if (!goya->device_cpu_mmu_mappings_done)
		return;

	WREG32(mmCPU_IF_ARUSER_OVR_EN, 0);
	WREG32(mmCPU_IF_AWUSER_OVR_EN, 0);

	if (!(hdev->cpu_accessible_dma_address & (PAGE_SIZE_2MB - 1))) {
		if (hl_mmu_unmap_page(hdev->kernel_ctx,
				VA_CPU_ACCESSIBLE_MEM_ADDR,
				PAGE_SIZE_2MB, true))
			dev_warn(hdev->dev,
				"Failed to unmap CPU accessible memory\n");
	} else {
		for (cpu_off = 0 ; cpu_off < SZ_2M ; cpu_off += PAGE_SIZE_4KB)
			if (hl_mmu_unmap_page(hdev->kernel_ctx,
					VA_CPU_ACCESSIBLE_MEM_ADDR + cpu_off,
					PAGE_SIZE_4KB,
					(cpu_off + PAGE_SIZE_4KB) >= SZ_2M))
				dev_warn_ratelimited(hdev->dev,
					"failed to unmap address 0x%llx\n",
					VA_CPU_ACCESSIBLE_MEM_ADDR + cpu_off);
	}

	for (off = 0 ; off < CPU_FW_IMAGE_SIZE ; off += PAGE_SIZE_2MB)
		if (hl_mmu_unmap_page(hdev->kernel_ctx,
				prop->dram_base_address + off, PAGE_SIZE_2MB,
				(off + PAGE_SIZE_2MB) >= CPU_FW_IMAGE_SIZE))
			dev_warn_ratelimited(hdev->dev,
					"Failed to unmap address 0x%llx\n",
					prop->dram_base_address + off);

	goya->device_cpu_mmu_mappings_done = false;
}

static void goya_mmu_prepare(struct hl_device *hdev, u32 asid)
{
	struct goya_device *goya = hdev->asic_specific;
	int i;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU))
		return;

	if (asid & ~MME_QM_GLBL_SECURE_PROPS_ASID_MASK) {
		dev_crit(hdev->dev, "asid %u is too big\n", asid);
		return;
	}

	/* zero the MMBP and ASID bits and then set the ASID */
	for (i = 0 ; i < GOYA_MMU_REGS_NUM ; i++)
		goya_mmu_prepare_reg(hdev, goya_mmu_regs[i], asid);
}

static int goya_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard,
					u32 flags)
{
	struct goya_device *goya = hdev->asic_specific;
	u32 status, timeout_usec;
	int rc;

	if (!(goya->hw_cap_initialized & HW_CAP_MMU) ||
		hdev->reset_info.hard_reset_pending)
		return 0;

	/* no need in L1 only invalidation in Goya */
	if (!is_hard)
		return 0;

	if (hdev->pldm)
		timeout_usec = GOYA_PLDM_MMU_TIMEOUT_USEC;
	else
		timeout_usec = MMU_CONFIG_TIMEOUT_USEC;

	/* L0 & L1 invalidation */
	WREG32(mmSTLB_INV_ALL_START, 1);

	rc = hl_poll_timeout(
		hdev,
		mmSTLB_INV_ALL_START,
		status,
		!status,
		1000,
		timeout_usec);

	return rc;
}

static int goya_mmu_invalidate_cache_range(struct hl_device *hdev,
						bool is_hard, u32 flags,
						u32 asid, u64 va, u64 size)
{
	/* Treat as invalidate all because there is no range invalidation
	 * in Goya
	 */
	return hl_mmu_invalidate_cache(hdev, is_hard, flags);
}

int goya_send_heartbeat(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;

	if (!(goya->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	return hl_fw_send_heartbeat(hdev);
}

int goya_cpucp_info_get(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 dram_size;
	int rc;

	if (!(goya->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	rc = hl_fw_cpucp_handshake(hdev, mmCPU_BOOT_DEV_STS0,
					mmCPU_BOOT_DEV_STS1, mmCPU_BOOT_ERR0,
					mmCPU_BOOT_ERR1);
	if (rc)
		return rc;

	dram_size = le64_to_cpu(prop->cpucp_info.dram_size);
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

	if (!strlen(prop->cpucp_info.card_name))
		strncpy(prop->cpucp_info.card_name, GOYA_DEFAULT_CARD_NAME,
				CARD_NAME_MAX_LEN);

	return 0;
}

static bool goya_is_device_idle(struct hl_device *hdev, u64 *mask_arr,
					u8 mask_len, struct seq_file *s)
{
	const char *fmt = "%-5d%-9s%#-14x%#-16x%#x\n";
	const char *dma_fmt = "%-5d%-9s%#-14x%#x\n";
	unsigned long *mask = (unsigned long *)mask_arr;
	u32 qm_glbl_sts0, cmdq_glbl_sts0, dma_core_sts0, tpc_cfg_sts,
		mme_arch_sts;
	bool is_idle = true, is_eng_idle;
	u64 offset;
	int i;

	if (s)
		seq_puts(s, "\nDMA  is_idle  QM_GLBL_STS0  DMA_CORE_STS0\n"
				"---  -------  ------------  -------------\n");

	offset = mmDMA_QM_1_GLBL_STS0 - mmDMA_QM_0_GLBL_STS0;

	for (i = 0 ; i < DMA_MAX_NUM ; i++) {
		qm_glbl_sts0 = RREG32(mmDMA_QM_0_GLBL_STS0 + i * offset);
		dma_core_sts0 = RREG32(mmDMA_CH_0_STS0 + i * offset);
		is_eng_idle = IS_DMA_QM_IDLE(qm_glbl_sts0) &&
				IS_DMA_IDLE(dma_core_sts0);
		is_idle &= is_eng_idle;

		if (mask && !is_eng_idle)
			set_bit(GOYA_ENGINE_ID_DMA_0 + i, mask);
		if (s)
			seq_printf(s, dma_fmt, i, is_eng_idle ? "Y" : "N",
					qm_glbl_sts0, dma_core_sts0);
	}

	if (s)
		seq_puts(s,
			"\nTPC  is_idle  QM_GLBL_STS0  CMDQ_GLBL_STS0  CFG_STATUS\n"
			"---  -------  ------------  --------------  ----------\n");

	offset = mmTPC1_QM_GLBL_STS0 - mmTPC0_QM_GLBL_STS0;

	for (i = 0 ; i < TPC_MAX_NUM ; i++) {
		qm_glbl_sts0 = RREG32(mmTPC0_QM_GLBL_STS0 + i * offset);
		cmdq_glbl_sts0 = RREG32(mmTPC0_CMDQ_GLBL_STS0 + i * offset);
		tpc_cfg_sts = RREG32(mmTPC0_CFG_STATUS + i * offset);
		is_eng_idle = IS_TPC_QM_IDLE(qm_glbl_sts0) &&
				IS_TPC_CMDQ_IDLE(cmdq_glbl_sts0) &&
				IS_TPC_IDLE(tpc_cfg_sts);
		is_idle &= is_eng_idle;

		if (mask && !is_eng_idle)
			set_bit(GOYA_ENGINE_ID_TPC_0 + i, mask);
		if (s)
			seq_printf(s, fmt, i, is_eng_idle ? "Y" : "N",
				qm_glbl_sts0, cmdq_glbl_sts0, tpc_cfg_sts);
	}

	if (s)
		seq_puts(s,
			"\nMME  is_idle  QM_GLBL_STS0  CMDQ_GLBL_STS0  ARCH_STATUS\n"
			"---  -------  ------------  --------------  -----------\n");

	qm_glbl_sts0 = RREG32(mmMME_QM_GLBL_STS0);
	cmdq_glbl_sts0 = RREG32(mmMME_CMDQ_GLBL_STS0);
	mme_arch_sts = RREG32(mmMME_ARCH_STATUS);
	is_eng_idle = IS_MME_QM_IDLE(qm_glbl_sts0) &&
			IS_MME_CMDQ_IDLE(cmdq_glbl_sts0) &&
			IS_MME_IDLE(mme_arch_sts);
	is_idle &= is_eng_idle;

	if (mask && !is_eng_idle)
		set_bit(GOYA_ENGINE_ID_MME_0, mask);
	if (s) {
		seq_printf(s, fmt, 0, is_eng_idle ? "Y" : "N", qm_glbl_sts0,
				cmdq_glbl_sts0, mme_arch_sts);
		seq_puts(s, "\n");
	}

	return is_idle;
}

static void goya_hw_queues_lock(struct hl_device *hdev)
	__acquires(&goya->hw_queues_lock)
{
	struct goya_device *goya = hdev->asic_specific;

	spin_lock(&goya->hw_queues_lock);
}

static void goya_hw_queues_unlock(struct hl_device *hdev)
	__releases(&goya->hw_queues_lock)
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

static void goya_cpu_init_scrambler_dram(struct hl_device *hdev)
{

}

static int goya_ctx_init(struct hl_ctx *ctx)
{
	if (ctx->asid != HL_KERNEL_ASID_ID)
		goya_mmu_prepare(ctx->hdev, ctx->asid);

	return 0;
}

u32 goya_get_queue_id_for_cq(struct hl_device *hdev, u32 cq_idx)
{
	return cq_idx;
}

static u32 goya_get_signal_cb_size(struct hl_device *hdev)
{
	return 0;
}

static u32 goya_get_wait_cb_size(struct hl_device *hdev)
{
	return 0;
}

static u32 goya_gen_signal_cb(struct hl_device *hdev, void *data, u16 sob_id,
				u32 size, bool eb)
{
	return 0;
}

static u32 goya_gen_wait_cb(struct hl_device *hdev,
		struct hl_gen_wait_properties *prop)
{
	return 0;
}

static void goya_reset_sob(struct hl_device *hdev, void *data)
{

}

static void goya_reset_sob_group(struct hl_device *hdev, u16 sob_group)
{

}

u64 goya_get_device_time(struct hl_device *hdev)
{
	u64 device_time = ((u64) RREG32(mmPSOC_TIMESTAMP_CNTCVU)) << 32;

	return device_time | RREG32(mmPSOC_TIMESTAMP_CNTCVL);
}

static int goya_collective_wait_init_cs(struct hl_cs *cs)
{
	return 0;
}

static int goya_collective_wait_create_jobs(struct hl_device *hdev,
		struct hl_ctx *ctx, struct hl_cs *cs, u32 wait_queue_id,
		u32 collective_engine_id, u32 encaps_signal_offset)
{
	return -EINVAL;
}

static void goya_ctx_fini(struct hl_ctx *ctx)
{

}

static int goya_get_hw_block_id(struct hl_device *hdev, u64 block_addr,
			u32 *block_size, u32 *block_id)
{
	return -EPERM;
}

static int goya_block_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
				u32 block_id, u32 block_size)
{
	return -EPERM;
}

static void goya_enable_events_from_fw(struct hl_device *hdev)
{
	WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR,
			GOYA_ASYNC_EVENT_ID_INTS_REGISTER);
}

static int goya_map_pll_idx_to_fw_idx(u32 pll_idx)
{
	switch (pll_idx) {
	case HL_GOYA_CPU_PLL: return CPU_PLL;
	case HL_GOYA_PCI_PLL: return PCI_PLL;
	case HL_GOYA_MME_PLL: return MME_PLL;
	case HL_GOYA_TPC_PLL: return TPC_PLL;
	case HL_GOYA_IC_PLL: return IC_PLL;
	case HL_GOYA_MC_PLL: return MC_PLL;
	case HL_GOYA_EMMC_PLL: return EMMC_PLL;
	default: return -EINVAL;
	}
}

static int goya_gen_sync_to_engine_map(struct hl_device *hdev,
				struct hl_sync_to_engine_map *map)
{
	/* Not implemented */
	return 0;
}

static int goya_monitor_valid(struct hl_mon_state_dump *mon)
{
	/* Not implemented */
	return 0;
}

static int goya_print_single_monitor(char **buf, size_t *size, size_t *offset,
				struct hl_device *hdev,
				struct hl_mon_state_dump *mon)
{
	/* Not implemented */
	return 0;
}


static int goya_print_fences_single_engine(
	struct hl_device *hdev, u64 base_offset, u64 status_base_offset,
	enum hl_sync_engine_type engine_type, u32 engine_id, char **buf,
	size_t *size, size_t *offset)
{
	/* Not implemented */
	return 0;
}


static struct hl_state_dump_specs_funcs goya_state_dump_funcs = {
	.monitor_valid = goya_monitor_valid,
	.print_single_monitor = goya_print_single_monitor,
	.gen_sync_to_engine_map = goya_gen_sync_to_engine_map,
	.print_fences_single_engine = goya_print_fences_single_engine,
};

static void goya_state_dump_init(struct hl_device *hdev)
{
	/* Not implemented */
	hdev->state_dump_specs.props = goya_state_dump_specs_props;
	hdev->state_dump_specs.funcs = goya_state_dump_funcs;
}

static u32 goya_get_sob_addr(struct hl_device *hdev, u32 sob_id)
{
	return 0;
}

static u32 *goya_get_stream_master_qid_arr(void)
{
	return NULL;
}

static void goya_get_valid_dram_page_orders(struct hl_info_dev_memalloc_page_sizes *info)
{
	/* set 0 since multiple pages are not supported */
	info->page_order_bitmask = 0;
}

static int goya_get_monitor_dump(struct hl_device *hdev, void *data)
{
	return -EOPNOTSUPP;
}

static int goya_scrub_device_dram(struct hl_device *hdev, u64 val)
{
	return -EOPNOTSUPP;
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
	.mmap = goya_mmap,
	.ring_doorbell = goya_ring_doorbell,
	.pqe_write = goya_pqe_write,
	.asic_dma_alloc_coherent = goya_dma_alloc_coherent,
	.asic_dma_free_coherent = goya_dma_free_coherent,
	.scrub_device_mem = goya_scrub_device_mem,
	.scrub_device_dram = goya_scrub_device_dram,
	.get_int_queue_base = goya_get_int_queue_base,
	.test_queues = goya_test_queues,
	.asic_dma_pool_zalloc = goya_dma_pool_zalloc,
	.asic_dma_pool_free = goya_dma_pool_free,
	.cpu_accessible_dma_pool_alloc = goya_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = goya_cpu_accessible_dma_pool_free,
	.hl_dma_unmap_sgtable = hl_dma_unmap_sgtable,
	.cs_parser = goya_cs_parser,
	.asic_dma_map_sgtable = hl_dma_map_sgtable,
	.get_dma_desc_list_size = goya_get_dma_desc_list_size,
	.add_end_of_cb_packets = goya_add_end_of_cb_packets,
	.update_eq_ci = goya_update_eq_ci,
	.context_switch = goya_context_switch,
	.restore_phase_topology = goya_restore_phase_topology,
	.debugfs_read_dma = goya_debugfs_read_dma,
	.add_device_attr = goya_add_device_attr,
	.handle_eqe = goya_handle_eqe,
	.get_events_stat = goya_get_events_stat,
	.read_pte = goya_read_pte,
	.write_pte = goya_write_pte,
	.mmu_invalidate_cache = goya_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = goya_mmu_invalidate_cache_range,
	.mmu_prefetch_cache_range = NULL,
	.send_heartbeat = goya_send_heartbeat,
	.debug_coresight = goya_debug_coresight,
	.is_device_idle = goya_is_device_idle,
	.non_hard_reset_late_init = goya_non_hard_reset_late_init,
	.hw_queues_lock = goya_hw_queues_lock,
	.hw_queues_unlock = goya_hw_queues_unlock,
	.get_pci_id = goya_get_pci_id,
	.get_eeprom_data = goya_get_eeprom_data,
	.get_monitor_dump = goya_get_monitor_dump,
	.send_cpu_message = goya_send_cpu_message,
	.pci_bars_map = goya_pci_bars_map,
	.init_iatu = goya_init_iatu,
	.rreg = hl_rreg,
	.wreg = hl_wreg,
	.halt_coresight = goya_halt_coresight,
	.ctx_init = goya_ctx_init,
	.ctx_fini = goya_ctx_fini,
	.get_queue_id_for_cq = goya_get_queue_id_for_cq,
	.load_firmware_to_device = goya_load_firmware_to_device,
	.load_boot_fit_to_device = goya_load_boot_fit_to_device,
	.get_signal_cb_size = goya_get_signal_cb_size,
	.get_wait_cb_size = goya_get_wait_cb_size,
	.gen_signal_cb = goya_gen_signal_cb,
	.gen_wait_cb = goya_gen_wait_cb,
	.reset_sob = goya_reset_sob,
	.reset_sob_group = goya_reset_sob_group,
	.get_device_time = goya_get_device_time,
	.collective_wait_init_cs = goya_collective_wait_init_cs,
	.collective_wait_create_jobs = goya_collective_wait_create_jobs,
	.scramble_addr = hl_mmu_scramble_addr,
	.descramble_addr = hl_mmu_descramble_addr,
	.ack_protection_bits_errors = goya_ack_protection_bits_errors,
	.get_hw_block_id = goya_get_hw_block_id,
	.hw_block_mmap = goya_block_mmap,
	.enable_events_from_fw = goya_enable_events_from_fw,
	.map_pll_idx_to_fw_idx = goya_map_pll_idx_to_fw_idx,
	.init_firmware_loader = goya_init_firmware_loader,
	.init_cpu_scrambler_dram = goya_cpu_init_scrambler_dram,
	.state_dump_init = goya_state_dump_init,
	.get_sob_addr = &goya_get_sob_addr,
	.set_pci_memory_regions = goya_set_pci_memory_regions,
	.get_stream_master_qid_arr = goya_get_stream_master_qid_arr,
	.is_valid_dram_page_size = NULL,
	.mmu_get_real_page_size = hl_mmu_get_real_page_size,
	.get_valid_dram_page_orders = goya_get_valid_dram_page_orders,
	.access_dev_mem = hl_access_dev_mem,
	.set_dram_bar_base = goya_set_ddr_bar_base,
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
