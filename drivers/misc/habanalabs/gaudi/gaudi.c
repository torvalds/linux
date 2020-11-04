// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudiP.h"
#include "../include/hw_ip/mmu/mmu_general.h"
#include "../include/hw_ip/mmu/mmu_v1_1.h"
#include "../include/gaudi/gaudi_masks.h"
#include "../include/gaudi/gaudi_fw_if.h"
#include "../include/gaudi/gaudi_reg_map.h"
#include "../include/gaudi/gaudi_async_ids_map_extended.h"

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/hwmon.h>
#include <linux/genalloc.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/iommu.h>
#include <linux/seq_file.h>

/*
 * Gaudi security scheme:
 *
 * 1. Host is protected by:
 *        - Range registers
 *        - MMU
 *
 * 2. DDR is protected by:
 *        - Range registers (protect the first 512MB)
 *
 * 3. Configuration is protected by:
 *        - Range registers
 *        - Protection bits
 *
 * MMU is always enabled.
 *
 * QMAN DMA channels 0,1,5 (PCI DMAN):
 *     - DMA is not secured.
 *     - PQ and CQ are secured.
 *     - CP is secured: The driver needs to parse CB but WREG should be allowed
 *                      because of TDMA (tensor DMA). Hence, WREG is always not
 *                      secured.
 *
 * When the driver needs to use DMA it will check that Gaudi is idle, set DMA
 * channel 0 to be secured, execute the DMA and change it back to not secured.
 * Currently, the driver doesn't use the DMA while there are compute jobs
 * running.
 *
 * The current use cases for the driver to use the DMA are:
 *     - Clear SRAM on context switch (happens on context switch when device is
 *       idle)
 *     - MMU page tables area clear (happens on init)
 *
 * QMAN DMA 2-4,6,7, TPC, MME, NIC:
 * PQ is secured and is located on the Host (HBM CON TPC3 bug)
 * CQ, CP and the engine are not secured
 *
 */

#define GAUDI_BOOT_FIT_FILE	"habanalabs/gaudi/gaudi-boot-fit.itb"
#define GAUDI_LINUX_FW_FILE	"habanalabs/gaudi/gaudi-fit.itb"
#define GAUDI_TPC_FW_FILE	"habanalabs/gaudi/gaudi_tpc.bin"

#define GAUDI_DMA_POOL_BLK_SIZE		0x100 /* 256 bytes */

#define GAUDI_RESET_TIMEOUT_MSEC	1000		/* 1000ms */
#define GAUDI_RESET_WAIT_MSEC		1		/* 1ms */
#define GAUDI_CPU_RESET_WAIT_MSEC	200		/* 200ms */
#define GAUDI_TEST_QUEUE_WAIT_USEC	100000		/* 100ms */

#define GAUDI_PLDM_RESET_WAIT_MSEC	1000		/* 1s */
#define GAUDI_PLDM_HRESET_TIMEOUT_MSEC	20000		/* 20s */
#define GAUDI_PLDM_TEST_QUEUE_WAIT_USEC	1000000		/* 1s */
#define GAUDI_PLDM_MMU_TIMEOUT_USEC	(MMU_CONFIG_TIMEOUT_USEC * 100)
#define GAUDI_PLDM_QMAN0_TIMEOUT_USEC	(HL_DEVICE_TIMEOUT_USEC * 30)
#define GAUDI_PLDM_TPC_KERNEL_WAIT_USEC	(HL_DEVICE_TIMEOUT_USEC * 30)
#define GAUDI_BOOT_FIT_REQ_TIMEOUT_USEC	1000000		/* 1s */
#define GAUDI_MSG_TO_CPU_TIMEOUT_USEC	4000000		/* 4s */

#define GAUDI_QMAN0_FENCE_VAL		0x72E91AB9

#define GAUDI_MAX_STRING_LEN		20

#define GAUDI_CB_POOL_CB_CNT		512
#define GAUDI_CB_POOL_CB_SIZE		0x20000 /* 128KB */

#define GAUDI_ALLOC_CPU_MEM_RETRY_CNT	3

#define GAUDI_NUM_OF_TPC_INTR_CAUSE	20

#define GAUDI_NUM_OF_QM_ERR_CAUSE	16

#define GAUDI_NUM_OF_QM_ARB_ERR_CAUSE	3

#define GAUDI_ARB_WDT_TIMEOUT		0x1000000

#define GAUDI_CLK_GATE_DEBUGFS_MASK	(\
		BIT(GAUDI_ENGINE_ID_MME_0) |\
		BIT(GAUDI_ENGINE_ID_MME_2) |\
		GENMASK_ULL(GAUDI_ENGINE_ID_TPC_7, GAUDI_ENGINE_ID_TPC_0))

static const char gaudi_irq_name[GAUDI_MSI_ENTRIES][GAUDI_MAX_STRING_LEN] = {
		"gaudi cq 0_0", "gaudi cq 0_1", "gaudi cq 0_2", "gaudi cq 0_3",
		"gaudi cq 1_0", "gaudi cq 1_1", "gaudi cq 1_2", "gaudi cq 1_3",
		"gaudi cq 5_0", "gaudi cq 5_1", "gaudi cq 5_2", "gaudi cq 5_3",
		"gaudi cpu eq"
};

static const u8 gaudi_dma_assignment[GAUDI_DMA_MAX] = {
	[GAUDI_PCI_DMA_1] = GAUDI_ENGINE_ID_DMA_0,
	[GAUDI_PCI_DMA_2] = GAUDI_ENGINE_ID_DMA_1,
	[GAUDI_PCI_DMA_3] = GAUDI_ENGINE_ID_DMA_5,
	[GAUDI_HBM_DMA_1] = GAUDI_ENGINE_ID_DMA_2,
	[GAUDI_HBM_DMA_2] = GAUDI_ENGINE_ID_DMA_3,
	[GAUDI_HBM_DMA_3] = GAUDI_ENGINE_ID_DMA_4,
	[GAUDI_HBM_DMA_4] = GAUDI_ENGINE_ID_DMA_6,
	[GAUDI_HBM_DMA_5] = GAUDI_ENGINE_ID_DMA_7
};

static const u8 gaudi_cq_assignment[NUMBER_OF_CMPLT_QUEUES] = {
	[0] = GAUDI_QUEUE_ID_DMA_0_0,
	[1] = GAUDI_QUEUE_ID_DMA_0_1,
	[2] = GAUDI_QUEUE_ID_DMA_0_2,
	[3] = GAUDI_QUEUE_ID_DMA_0_3,
	[4] = GAUDI_QUEUE_ID_DMA_1_0,
	[5] = GAUDI_QUEUE_ID_DMA_1_1,
	[6] = GAUDI_QUEUE_ID_DMA_1_2,
	[7] = GAUDI_QUEUE_ID_DMA_1_3,
	[8] = GAUDI_QUEUE_ID_DMA_5_0,
	[9] = GAUDI_QUEUE_ID_DMA_5_1,
	[10] = GAUDI_QUEUE_ID_DMA_5_2,
	[11] = GAUDI_QUEUE_ID_DMA_5_3
};

static const u16 gaudi_packet_sizes[MAX_PACKET_ID] = {
	[PACKET_WREG_32]	= sizeof(struct packet_wreg32),
	[PACKET_WREG_BULK]	= sizeof(struct packet_wreg_bulk),
	[PACKET_MSG_LONG]	= sizeof(struct packet_msg_long),
	[PACKET_MSG_SHORT]	= sizeof(struct packet_msg_short),
	[PACKET_CP_DMA]		= sizeof(struct packet_cp_dma),
	[PACKET_REPEAT]		= sizeof(struct packet_repeat),
	[PACKET_MSG_PROT]	= sizeof(struct packet_msg_prot),
	[PACKET_FENCE]		= sizeof(struct packet_fence),
	[PACKET_LIN_DMA]	= sizeof(struct packet_lin_dma),
	[PACKET_NOP]		= sizeof(struct packet_nop),
	[PACKET_STOP]		= sizeof(struct packet_stop),
	[PACKET_ARB_POINT]	= sizeof(struct packet_arb_point),
	[PACKET_WAIT]		= sizeof(struct packet_wait),
	[PACKET_LOAD_AND_EXE]	= sizeof(struct packet_load_and_exe)
};

static inline bool validate_packet_id(enum packet_id id)
{
	switch (id) {
	case PACKET_WREG_32:
	case PACKET_WREG_BULK:
	case PACKET_MSG_LONG:
	case PACKET_MSG_SHORT:
	case PACKET_CP_DMA:
	case PACKET_REPEAT:
	case PACKET_MSG_PROT:
	case PACKET_FENCE:
	case PACKET_LIN_DMA:
	case PACKET_NOP:
	case PACKET_STOP:
	case PACKET_ARB_POINT:
	case PACKET_WAIT:
	case PACKET_LOAD_AND_EXE:
		return true;
	default:
		return false;
	}
}

static const char * const
gaudi_tpc_interrupts_cause[GAUDI_NUM_OF_TPC_INTR_CAUSE] = {
	"tpc_address_exceed_slm",
	"tpc_div_by_0",
	"tpc_spu_mac_overflow",
	"tpc_spu_addsub_overflow",
	"tpc_spu_abs_overflow",
	"tpc_spu_fp_dst_nan_inf",
	"tpc_spu_fp_dst_denorm",
	"tpc_vpu_mac_overflow",
	"tpc_vpu_addsub_overflow",
	"tpc_vpu_abs_overflow",
	"tpc_vpu_fp_dst_nan_inf",
	"tpc_vpu_fp_dst_denorm",
	"tpc_assertions",
	"tpc_illegal_instruction",
	"tpc_pc_wrap_around",
	"tpc_qm_sw_err",
	"tpc_hbw_rresp_err",
	"tpc_hbw_bresp_err",
	"tpc_lbw_rresp_err",
	"tpc_lbw_bresp_err"
};

static const char * const
gaudi_qman_error_cause[GAUDI_NUM_OF_QM_ERR_CAUSE] = {
	"PQ AXI HBW error",
	"CQ AXI HBW error",
	"CP AXI HBW error",
	"CP error due to undefined OPCODE",
	"CP encountered STOP OPCODE",
	"CP AXI LBW error",
	"CP WRREG32 or WRBULK returned error",
	"N/A",
	"FENCE 0 inc over max value and clipped",
	"FENCE 1 inc over max value and clipped",
	"FENCE 2 inc over max value and clipped",
	"FENCE 3 inc over max value and clipped",
	"FENCE 0 dec under min value and clipped",
	"FENCE 1 dec under min value and clipped",
	"FENCE 2 dec under min value and clipped",
	"FENCE 3 dec under min value and clipped"
};

static const char * const
gaudi_qman_arb_error_cause[GAUDI_NUM_OF_QM_ARB_ERR_CAUSE] = {
	"Choice push while full error",
	"Choice Q watchdog error",
	"MSG AXI LBW returned with error"
};

static enum hl_queue_type gaudi_queue_type[GAUDI_QUEUE_ID_SIZE] = {
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_0_0 */
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_0_1 */
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_0_2 */
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_0_3 */
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_1_0 */
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_1_1 */
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_1_2 */
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_1_3 */
	QUEUE_TYPE_CPU, /* GAUDI_QUEUE_ID_CPU_PQ */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_2_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_2_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_2_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_2_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_3_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_3_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_3_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_3_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_4_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_4_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_4_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_4_3 */
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_5_0 */
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_5_1 */
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_5_2 */
	QUEUE_TYPE_EXT, /* GAUDI_QUEUE_ID_DMA_5_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_6_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_6_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_6_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_6_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_7_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_7_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_7_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_7_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_MME_0_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_MME_0_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_MME_0_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_MME_0_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_MME_1_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_MME_1_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_MME_1_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_MME_1_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_0_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_0_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_0_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_0_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_1_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_1_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_1_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_1_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_2_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_2_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_2_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_2_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_3_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_3_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_3_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_3_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_4_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_4_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_4_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_4_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_5_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_5_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_5_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_5_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_6_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_6_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_6_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_6_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_7_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_7_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_7_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_TPC_7_3 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_0_0 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_0_1 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_0_2 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_0_3 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_1_0 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_1_1 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_1_2 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_1_3 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_2_0 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_2_1 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_2_2 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_2_3 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_3_0 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_3_1 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_3_2 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_3_3 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_4_0 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_4_1 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_4_2 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_4_3 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_5_0 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_5_1 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_5_2 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_5_3 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_6_0 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_6_1 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_6_2 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_6_3 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_7_0 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_7_1 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_7_2 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_7_3 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_8_0 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_8_1 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_8_2 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_8_3 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_9_0 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_9_1 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_9_2 */
	QUEUE_TYPE_NA,  /* GAUDI_QUEUE_ID_NIC_9_3 */
};

struct ecc_info_extract_params {
	u64 block_address;
	u32 num_memories;
	bool derr;
	bool disable_clock_gating;
};

static int gaudi_mmu_update_asid_hop0_addr(struct hl_device *hdev, u32 asid,
								u64 phys_addr);
static int gaudi_send_job_on_qman0(struct hl_device *hdev,
					struct hl_cs_job *job);
static int gaudi_memset_device_memory(struct hl_device *hdev, u64 addr,
					u32 size, u64 val);
static int gaudi_run_tpc_kernel(struct hl_device *hdev, u64 tpc_kernel,
				u32 tpc_id);
static int gaudi_mmu_clear_pgt_range(struct hl_device *hdev);
static int gaudi_cpucp_info_get(struct hl_device *hdev);
static void gaudi_disable_clock_gating(struct hl_device *hdev);
static void gaudi_mmu_prepare(struct hl_device *hdev, u32 asid);

static int gaudi_get_fixed_properties(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 num_sync_stream_queues = 0;
	int i;

	prop->max_queues = GAUDI_QUEUE_ID_SIZE;
	prop->hw_queues_props = kcalloc(prop->max_queues,
			sizeof(struct hw_queue_properties),
			GFP_KERNEL);

	if (!prop->hw_queues_props)
		return -ENOMEM;

	for (i = 0 ; i < prop->max_queues ; i++) {
		if (gaudi_queue_type[i] == QUEUE_TYPE_EXT) {
			prop->hw_queues_props[i].type = QUEUE_TYPE_EXT;
			prop->hw_queues_props[i].driver_only = 0;
			prop->hw_queues_props[i].requires_kernel_cb = 1;
			prop->hw_queues_props[i].supports_sync_stream = 1;
			num_sync_stream_queues++;
		} else if (gaudi_queue_type[i] == QUEUE_TYPE_CPU) {
			prop->hw_queues_props[i].type = QUEUE_TYPE_CPU;
			prop->hw_queues_props[i].driver_only = 1;
			prop->hw_queues_props[i].requires_kernel_cb = 0;
			prop->hw_queues_props[i].supports_sync_stream = 0;
		} else if (gaudi_queue_type[i] == QUEUE_TYPE_INT) {
			prop->hw_queues_props[i].type = QUEUE_TYPE_INT;
			prop->hw_queues_props[i].driver_only = 0;
			prop->hw_queues_props[i].requires_kernel_cb = 0;
		} else if (gaudi_queue_type[i] == QUEUE_TYPE_NA) {
			prop->hw_queues_props[i].type = QUEUE_TYPE_NA;
			prop->hw_queues_props[i].driver_only = 0;
			prop->hw_queues_props[i].requires_kernel_cb = 0;
			prop->hw_queues_props[i].supports_sync_stream = 0;
		}
	}

	prop->completion_queues_count = NUMBER_OF_CMPLT_QUEUES;
	prop->sync_stream_first_sob = 0;
	prop->sync_stream_first_mon = 0;
	prop->dram_base_address = DRAM_PHYS_BASE;
	prop->dram_size = GAUDI_HBM_SIZE_32GB;
	prop->dram_end_address = prop->dram_base_address +
					prop->dram_size;
	prop->dram_user_base_address = DRAM_BASE_ADDR_USER;

	prop->sram_base_address = SRAM_BASE_ADDR;
	prop->sram_size = SRAM_SIZE;
	prop->sram_end_address = prop->sram_base_address +
					prop->sram_size;
	prop->sram_user_base_address = prop->sram_base_address +
					SRAM_USER_BASE_OFFSET;

	prop->mmu_pgt_addr = MMU_PAGE_TABLES_ADDR;
	if (hdev->pldm)
		prop->mmu_pgt_size = 0x800000; /* 8MB */
	else
		prop->mmu_pgt_size = MMU_PAGE_TABLES_SIZE;
	prop->mmu_pte_size = HL_PTE_SIZE;
	prop->mmu_hop_table_size = HOP_TABLE_SIZE;
	prop->mmu_hop0_tables_total_size = HOP0_TABLES_TOTAL_SIZE;
	prop->dram_page_size = PAGE_SIZE_2MB;

	prop->pmmu.hop0_shift = HOP0_SHIFT;
	prop->pmmu.hop1_shift = HOP1_SHIFT;
	prop->pmmu.hop2_shift = HOP2_SHIFT;
	prop->pmmu.hop3_shift = HOP3_SHIFT;
	prop->pmmu.hop4_shift = HOP4_SHIFT;
	prop->pmmu.hop0_mask = HOP0_MASK;
	prop->pmmu.hop1_mask = HOP1_MASK;
	prop->pmmu.hop2_mask = HOP2_MASK;
	prop->pmmu.hop3_mask = HOP3_MASK;
	prop->pmmu.hop4_mask = HOP4_MASK;
	prop->pmmu.start_addr = VA_HOST_SPACE_START;
	prop->pmmu.end_addr =
			(VA_HOST_SPACE_START + VA_HOST_SPACE_SIZE / 2) - 1;
	prop->pmmu.page_size = PAGE_SIZE_4KB;
	prop->pmmu.num_hops = MMU_ARCH_5_HOPS;

	/* PMMU and HPMMU are the same except of page size */
	memcpy(&prop->pmmu_huge, &prop->pmmu, sizeof(prop->pmmu));
	prop->pmmu_huge.page_size = PAGE_SIZE_2MB;

	/* shifts and masks are the same in PMMU and DMMU */
	memcpy(&prop->dmmu, &prop->pmmu, sizeof(prop->pmmu));
	prop->dmmu.start_addr = (VA_HOST_SPACE_START + VA_HOST_SPACE_SIZE / 2);
	prop->dmmu.end_addr = VA_HOST_SPACE_END;
	prop->dmmu.page_size = PAGE_SIZE_2MB;

	prop->cfg_size = CFG_SIZE;
	prop->max_asid = MAX_ASID;
	prop->num_of_events = GAUDI_EVENT_SIZE;
	prop->tpc_enabled_mask = TPC_ENABLED_MASK;

	prop->max_power_default = MAX_POWER_DEFAULT_PCI;

	prop->cb_pool_cb_cnt = GAUDI_CB_POOL_CB_CNT;
	prop->cb_pool_cb_size = GAUDI_CB_POOL_CB_SIZE;

	prop->pcie_dbi_base_address = mmPCIE_DBI_BASE;
	prop->pcie_aux_dbi_reg_addr = CFG_BASE + mmPCIE_AUX_DBI;

	strncpy(prop->cpucp_info.card_name, GAUDI_DEFAULT_CARD_NAME,
					CARD_NAME_MAX_LEN);

	prop->max_pending_cs = GAUDI_MAX_PENDING_CS;

	prop->first_available_user_sob[HL_GAUDI_WS_DCORE] =
			num_sync_stream_queues * HL_RSVD_SOBS;
	prop->first_available_user_mon[HL_GAUDI_WS_DCORE] =
			num_sync_stream_queues * HL_RSVD_MONS;

	return 0;
}

static int gaudi_pci_bars_map(struct hl_device *hdev)
{
	static const char * const name[] = {"SRAM", "CFG", "HBM"};
	bool is_wc[3] = {false, false, true};
	int rc;

	rc = hl_pci_bars_map(hdev, name, is_wc);
	if (rc)
		return rc;

	hdev->rmmio = hdev->pcie_bar[CFG_BAR_ID] +
			(CFG_BASE - SPI_FLASH_BASE_ADDR);

	return 0;
}

static u64 gaudi_set_hbm_bar_base(struct hl_device *hdev, u64 addr)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct hl_inbound_pci_region pci_region;
	u64 old_addr = addr;
	int rc;

	if ((gaudi) && (gaudi->hbm_bar_cur_addr == addr))
		return old_addr;

	/* Inbound Region 2 - Bar 4 - Point to HBM */
	pci_region.mode = PCI_BAR_MATCH_MODE;
	pci_region.bar = HBM_BAR_ID;
	pci_region.addr = addr;
	rc = hl_pci_set_inbound_region(hdev, 2, &pci_region);
	if (rc)
		return U64_MAX;

	if (gaudi) {
		old_addr = gaudi->hbm_bar_cur_addr;
		gaudi->hbm_bar_cur_addr = addr;
	}

	return old_addr;
}

static int gaudi_init_iatu(struct hl_device *hdev)
{
	struct hl_inbound_pci_region inbound_region;
	struct hl_outbound_pci_region outbound_region;
	int rc;

	/* Inbound Region 0 - Bar 0 - Point to SRAM + CFG */
	inbound_region.mode = PCI_BAR_MATCH_MODE;
	inbound_region.bar = SRAM_BAR_ID;
	inbound_region.addr = SRAM_BASE_ADDR;
	rc = hl_pci_set_inbound_region(hdev, 0, &inbound_region);
	if (rc)
		goto done;

	/* Inbound Region 1 - Bar 2 - Point to SPI FLASH */
	inbound_region.mode = PCI_BAR_MATCH_MODE;
	inbound_region.bar = CFG_BAR_ID;
	inbound_region.addr = SPI_FLASH_BASE_ADDR;
	rc = hl_pci_set_inbound_region(hdev, 1, &inbound_region);
	if (rc)
		goto done;

	/* Inbound Region 2 - Bar 4 - Point to HBM */
	inbound_region.mode = PCI_BAR_MATCH_MODE;
	inbound_region.bar = HBM_BAR_ID;
	inbound_region.addr = DRAM_PHYS_BASE;
	rc = hl_pci_set_inbound_region(hdev, 2, &inbound_region);
	if (rc)
		goto done;

	hdev->asic_funcs->set_dma_mask_from_fw(hdev);

	/* Outbound Region 0 - Point to Host */
	outbound_region.addr = HOST_PHYS_BASE;
	outbound_region.size = HOST_PHYS_SIZE;
	rc = hl_pci_set_outbound_region(hdev, &outbound_region);

done:
	return rc;
}

static int gaudi_early_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pci_dev *pdev = hdev->pdev;
	int rc;

	rc = gaudi_get_fixed_properties(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to get fixed properties\n");
		return rc;
	}

	/* Check BAR sizes */
	if (pci_resource_len(pdev, SRAM_BAR_ID) != SRAM_BAR_SIZE) {
		dev_err(hdev->dev,
			"Not " HL_NAME "? BAR %d size %llu, expecting %llu\n",
			SRAM_BAR_ID,
			(unsigned long long) pci_resource_len(pdev,
							SRAM_BAR_ID),
			SRAM_BAR_SIZE);
		rc = -ENODEV;
		goto free_queue_props;
	}

	if (pci_resource_len(pdev, CFG_BAR_ID) != CFG_BAR_SIZE) {
		dev_err(hdev->dev,
			"Not " HL_NAME "? BAR %d size %llu, expecting %llu\n",
			CFG_BAR_ID,
			(unsigned long long) pci_resource_len(pdev,
								CFG_BAR_ID),
			CFG_BAR_SIZE);
		rc = -ENODEV;
		goto free_queue_props;
	}

	prop->dram_pci_bar_size = pci_resource_len(pdev, HBM_BAR_ID);

	rc = hl_pci_init(hdev, mmPSOC_GLOBAL_CONF_CPU_BOOT_STATUS,
			mmCPU_BOOT_ERR0, GAUDI_BOOT_FIT_REQ_TIMEOUT_USEC);
	if (rc)
		goto free_queue_props;

	/* GAUDI Firmware does not yet support security */
	prop->fw_security_disabled = true;
	dev_info(hdev->dev, "firmware-level security is disabled\n");

	return 0;

free_queue_props:
	kfree(hdev->asic_prop.hw_queues_props);
	return rc;
}

static int gaudi_early_fini(struct hl_device *hdev)
{
	kfree(hdev->asic_prop.hw_queues_props);
	hl_pci_fini(hdev);

	return 0;
}

/**
 * gaudi_fetch_psoc_frequency - Fetch PSOC frequency values
 *
 * @hdev: pointer to hl_device structure
 *
 */
static void gaudi_fetch_psoc_frequency(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 trace_freq = 0;
	u32 pll_clk = 0;
	u32 div_fctr = RREG32(mmPSOC_CPU_PLL_DIV_FACTOR_2);
	u32 div_sel = RREG32(mmPSOC_CPU_PLL_DIV_SEL_2);
	u32 nr = RREG32(mmPSOC_CPU_PLL_NR);
	u32 nf = RREG32(mmPSOC_CPU_PLL_NF);
	u32 od = RREG32(mmPSOC_CPU_PLL_OD);

	if (div_sel == DIV_SEL_REF_CLK || div_sel == DIV_SEL_DIVIDED_REF) {
		if (div_sel == DIV_SEL_REF_CLK)
			trace_freq = PLL_REF_CLK;
		else
			trace_freq = PLL_REF_CLK / (div_fctr + 1);
	} else if (div_sel == DIV_SEL_PLL_CLK ||
					div_sel == DIV_SEL_DIVIDED_PLL) {
		pll_clk = PLL_REF_CLK * (nf + 1) / ((nr + 1) * (od + 1));
		if (div_sel == DIV_SEL_PLL_CLK)
			trace_freq = pll_clk;
		else
			trace_freq = pll_clk / (div_fctr + 1);
	} else {
		dev_warn(hdev->dev,
			"Received invalid div select value: %d", div_sel);
	}

	prop->psoc_timestamp_frequency = trace_freq;
	prop->psoc_pci_pll_nr = nr;
	prop->psoc_pci_pll_nf = nf;
	prop->psoc_pci_pll_od = od;
	prop->psoc_pci_pll_div_factor = div_fctr;
}

static int _gaudi_init_tpc_mem(struct hl_device *hdev,
		dma_addr_t tpc_kernel_src_addr, u32 tpc_kernel_size)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct packet_lin_dma *init_tpc_mem_pkt;
	struct hl_cs_job *job;
	struct hl_cb *cb;
	u64 dst_addr;
	u32 cb_size, ctl;
	u8 tpc_id;
	int rc;

	cb = hl_cb_kernel_create(hdev, PAGE_SIZE, false);
	if (!cb)
		return -EFAULT;

	init_tpc_mem_pkt = cb->kernel_address;
	cb_size = sizeof(*init_tpc_mem_pkt);
	memset(init_tpc_mem_pkt, 0, cb_size);

	init_tpc_mem_pkt->tsize = cpu_to_le32(tpc_kernel_size);

	ctl = FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_LIN_DMA);
	ctl |= FIELD_PREP(GAUDI_PKT_LIN_DMA_CTL_LIN_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_RB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_MB_MASK, 1);

	init_tpc_mem_pkt->ctl = cpu_to_le32(ctl);

	init_tpc_mem_pkt->src_addr = cpu_to_le64(tpc_kernel_src_addr);
	dst_addr = (prop->sram_user_base_address &
			GAUDI_PKT_LIN_DMA_DST_ADDR_MASK) >>
			GAUDI_PKT_LIN_DMA_DST_ADDR_SHIFT;
	init_tpc_mem_pkt->dst_addr |= cpu_to_le64(dst_addr);

	job = hl_cs_allocate_job(hdev, QUEUE_TYPE_EXT, true);
	if (!job) {
		dev_err(hdev->dev, "Failed to allocate a new job\n");
		rc = -ENOMEM;
		goto release_cb;
	}

	job->id = 0;
	job->user_cb = cb;
	job->user_cb->cs_cnt++;
	job->user_cb_size = cb_size;
	job->hw_queue_id = GAUDI_QUEUE_ID_DMA_0_0;
	job->patched_cb = job->user_cb;
	job->job_cb_size = job->user_cb_size + sizeof(struct packet_msg_prot);

	hl_debugfs_add_job(hdev, job);

	rc = gaudi_send_job_on_qman0(hdev, job);

	if (rc)
		goto free_job;

	for (tpc_id = 0 ; tpc_id < TPC_NUMBER_OF_ENGINES ; tpc_id++) {
		rc = gaudi_run_tpc_kernel(hdev, dst_addr, tpc_id);
		if (rc)
			break;
	}

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

/*
 * gaudi_init_tpc_mem() - Initialize TPC memories.
 * @hdev: Pointer to hl_device structure.
 *
 * Copy TPC kernel fw from firmware file and run it to initialize TPC memories.
 *
 * Return: 0 for success, negative value for error.
 */
static int gaudi_init_tpc_mem(struct hl_device *hdev)
{
	const struct firmware *fw;
	size_t fw_size;
	void *cpu_addr;
	dma_addr_t dma_handle;
	int rc;

	rc = request_firmware(&fw, GAUDI_TPC_FW_FILE, hdev->dev);
	if (rc) {
		dev_err(hdev->dev, "Firmware file %s is not found!\n",
				GAUDI_TPC_FW_FILE);
		goto out;
	}

	fw_size = fw->size;
	cpu_addr = hdev->asic_funcs->asic_dma_alloc_coherent(hdev, fw_size,
			&dma_handle, GFP_KERNEL | __GFP_ZERO);
	if (!cpu_addr) {
		dev_err(hdev->dev,
			"Failed to allocate %zu of dma memory for TPC kernel\n",
			fw_size);
		rc = -ENOMEM;
		goto out;
	}

	memcpy(cpu_addr, fw->data, fw_size);

	rc = _gaudi_init_tpc_mem(hdev, dma_handle, fw_size);

	hdev->asic_funcs->asic_dma_free_coherent(hdev, fw->size, cpu_addr,
			dma_handle);

out:
	release_firmware(fw);
	return rc;
}

static int gaudi_late_init(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int rc;

	rc = gaudi->cpucp_info_get(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to get cpucp info\n");
		return rc;
	}

	rc = hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_ENABLE_PCI_ACCESS);
	if (rc) {
		dev_err(hdev->dev, "Failed to enable PCI access from CPU\n");
		return rc;
	}

	WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR, GAUDI_EVENT_INTS_REGISTER);

	gaudi_fetch_psoc_frequency(hdev);

	rc = gaudi_mmu_clear_pgt_range(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to clear MMU page tables range\n");
		goto disable_pci_access;
	}

	rc = gaudi_init_tpc_mem(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize TPC memories\n");
		goto disable_pci_access;
	}

	return 0;

disable_pci_access:
	hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_DISABLE_PCI_ACCESS);

	return rc;
}

static void gaudi_late_fini(struct hl_device *hdev)
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

static int gaudi_alloc_cpu_accessible_dma_mem(struct hl_device *hdev)
{
	dma_addr_t dma_addr_arr[GAUDI_ALLOC_CPU_MEM_RETRY_CNT] = {}, end_addr;
	void *virt_addr_arr[GAUDI_ALLOC_CPU_MEM_RETRY_CNT] = {};
	int i, j, rc = 0;

	/*
	 * The device CPU works with 40-bits addresses, while bit 39 must be set
	 * to '1' when accessing the host.
	 * Bits 49:39 of the full host address are saved for a later
	 * configuration of the HW to perform extension to 50 bits.
	 * Because there is a single HW register that holds the extension bits,
	 * these bits must be identical in all allocated range.
	 */

	for (i = 0 ; i < GAUDI_ALLOC_CPU_MEM_RETRY_CNT ; i++) {
		virt_addr_arr[i] =
			hdev->asic_funcs->asic_dma_alloc_coherent(hdev,
						HL_CPU_ACCESSIBLE_MEM_SIZE,
						&dma_addr_arr[i],
						GFP_KERNEL | __GFP_ZERO);
		if (!virt_addr_arr[i]) {
			rc = -ENOMEM;
			goto free_dma_mem_arr;
		}

		end_addr = dma_addr_arr[i] + HL_CPU_ACCESSIBLE_MEM_SIZE - 1;
		if (GAUDI_CPU_PCI_MSB_ADDR(dma_addr_arr[i]) ==
				GAUDI_CPU_PCI_MSB_ADDR(end_addr))
			break;
	}

	if (i == GAUDI_ALLOC_CPU_MEM_RETRY_CNT) {
		dev_err(hdev->dev,
			"MSB of CPU accessible DMA memory are not identical in all range\n");
		rc = -EFAULT;
		goto free_dma_mem_arr;
	}

	hdev->cpu_accessible_dma_mem = virt_addr_arr[i];
	hdev->cpu_accessible_dma_address = dma_addr_arr[i];
	hdev->cpu_pci_msb_addr =
		GAUDI_CPU_PCI_MSB_ADDR(hdev->cpu_accessible_dma_address);

	GAUDI_PCI_TO_CPU_ADDR(hdev->cpu_accessible_dma_address);

free_dma_mem_arr:
	for (j = 0 ; j < i ; j++)
		hdev->asic_funcs->asic_dma_free_coherent(hdev,
						HL_CPU_ACCESSIBLE_MEM_SIZE,
						virt_addr_arr[j],
						dma_addr_arr[j]);

	return rc;
}

static void gaudi_free_internal_qmans_pq_mem(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_internal_qman_info *q;
	u32 i;

	for (i = 0 ; i < GAUDI_QUEUE_ID_SIZE ; i++) {
		q = &gaudi->internal_qmans[i];
		if (!q->pq_kernel_addr)
			continue;
		hdev->asic_funcs->asic_dma_free_coherent(hdev, q->pq_size,
							q->pq_kernel_addr,
							q->pq_dma_addr);
	}
}

static int gaudi_alloc_internal_qmans_pq_mem(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_internal_qman_info *q;
	int rc, i;

	for (i = 0 ; i < GAUDI_QUEUE_ID_SIZE ; i++) {
		if (gaudi_queue_type[i] != QUEUE_TYPE_INT)
			continue;

		q = &gaudi->internal_qmans[i];

		switch (i) {
		case GAUDI_QUEUE_ID_DMA_2_0 ... GAUDI_QUEUE_ID_DMA_4_3:
		case GAUDI_QUEUE_ID_DMA_6_0 ... GAUDI_QUEUE_ID_DMA_7_3:
			q->pq_size = HBM_DMA_QMAN_SIZE_IN_BYTES;
			break;
		case GAUDI_QUEUE_ID_MME_0_0 ... GAUDI_QUEUE_ID_MME_1_3:
			q->pq_size = MME_QMAN_SIZE_IN_BYTES;
			break;
		case GAUDI_QUEUE_ID_TPC_0_0 ... GAUDI_QUEUE_ID_TPC_7_3:
			q->pq_size = TPC_QMAN_SIZE_IN_BYTES;
			break;
		default:
			dev_err(hdev->dev, "Bad internal queue index %d", i);
			rc = -EINVAL;
			goto free_internal_qmans_pq_mem;
		}

		q->pq_kernel_addr = hdev->asic_funcs->asic_dma_alloc_coherent(
						hdev, q->pq_size,
						&q->pq_dma_addr,
						GFP_KERNEL | __GFP_ZERO);
		if (!q->pq_kernel_addr) {
			rc = -ENOMEM;
			goto free_internal_qmans_pq_mem;
		}
	}

	return 0;

free_internal_qmans_pq_mem:
	gaudi_free_internal_qmans_pq_mem(hdev);
	return rc;
}

static int gaudi_sw_init(struct hl_device *hdev)
{
	struct gaudi_device *gaudi;
	u32 i, event_id = 0;
	int rc;

	/* Allocate device structure */
	gaudi = kzalloc(sizeof(*gaudi), GFP_KERNEL);
	if (!gaudi)
		return -ENOMEM;

	for (i = 0 ; i < ARRAY_SIZE(gaudi_irq_map_table) ; i++) {
		if (gaudi_irq_map_table[i].valid) {
			if (event_id == GAUDI_EVENT_SIZE) {
				dev_err(hdev->dev,
					"Event array exceeds the limit of %u events\n",
					GAUDI_EVENT_SIZE);
				rc = -EINVAL;
				goto free_gaudi_device;
			}

			gaudi->events[event_id++] =
					gaudi_irq_map_table[i].fc_id;
		}
	}

	gaudi->cpucp_info_get = gaudi_cpucp_info_get;

	gaudi->max_freq_value = GAUDI_MAX_CLK_FREQ;

	hdev->asic_specific = gaudi;

	/* Create DMA pool for small allocations */
	hdev->dma_pool = dma_pool_create(dev_name(hdev->dev),
			&hdev->pdev->dev, GAUDI_DMA_POOL_BLK_SIZE, 8, 0);
	if (!hdev->dma_pool) {
		dev_err(hdev->dev, "failed to create DMA pool\n");
		rc = -ENOMEM;
		goto free_gaudi_device;
	}

	rc = gaudi_alloc_cpu_accessible_dma_mem(hdev);
	if (rc)
		goto free_dma_pool;

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

	rc = gaudi_alloc_internal_qmans_pq_mem(hdev);
	if (rc)
		goto free_cpu_accessible_dma_pool;

	spin_lock_init(&gaudi->hw_queues_lock);
	mutex_init(&gaudi->clk_gate_mutex);

	hdev->supports_sync_stream = true;
	hdev->supports_coresight = true;

	return 0;

free_cpu_accessible_dma_pool:
	gen_pool_destroy(hdev->cpu_accessible_dma_pool);
free_cpu_dma_mem:
	GAUDI_CPU_TO_PCI_ADDR(hdev->cpu_accessible_dma_address,
				hdev->cpu_pci_msb_addr);
	hdev->asic_funcs->asic_dma_free_coherent(hdev,
			HL_CPU_ACCESSIBLE_MEM_SIZE,
			hdev->cpu_accessible_dma_mem,
			hdev->cpu_accessible_dma_address);
free_dma_pool:
	dma_pool_destroy(hdev->dma_pool);
free_gaudi_device:
	kfree(gaudi);
	return rc;
}

static int gaudi_sw_fini(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	gaudi_free_internal_qmans_pq_mem(hdev);

	gen_pool_destroy(hdev->cpu_accessible_dma_pool);

	GAUDI_CPU_TO_PCI_ADDR(hdev->cpu_accessible_dma_address,
					hdev->cpu_pci_msb_addr);
	hdev->asic_funcs->asic_dma_free_coherent(hdev,
			HL_CPU_ACCESSIBLE_MEM_SIZE,
			hdev->cpu_accessible_dma_mem,
			hdev->cpu_accessible_dma_address);

	dma_pool_destroy(hdev->dma_pool);

	mutex_destroy(&gaudi->clk_gate_mutex);

	kfree(gaudi);

	return 0;
}

static irqreturn_t gaudi_irq_handler_single(int irq, void *arg)
{
	struct hl_device *hdev = arg;
	int i;

	if (hdev->disabled)
		return IRQ_HANDLED;

	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		hl_irq_handler_cq(irq, &hdev->completion_queue[i]);

	hl_irq_handler_eq(irq, &hdev->event_queue);

	return IRQ_HANDLED;
}

/*
 * For backward compatibility, new MSI interrupts should be set after the
 * existing CPU and NIC interrupts.
 */
static int gaudi_pci_irq_vector(struct hl_device *hdev, unsigned int nr,
				bool cpu_eq)
{
	int msi_vec;

	if ((nr != GAUDI_EVENT_QUEUE_MSI_IDX) && (cpu_eq))
		dev_crit(hdev->dev, "CPU EQ must use IRQ %d\n",
				GAUDI_EVENT_QUEUE_MSI_IDX);

	msi_vec = ((nr < GAUDI_EVENT_QUEUE_MSI_IDX) || (cpu_eq)) ? nr :
			(nr + NIC_NUMBER_OF_ENGINES + 1);

	return pci_irq_vector(hdev->pdev, msi_vec);
}

static int gaudi_enable_msi_single(struct hl_device *hdev)
{
	int rc, irq;

	dev_info(hdev->dev, "Working in single MSI IRQ mode\n");

	irq = gaudi_pci_irq_vector(hdev, 0, false);
	rc = request_irq(irq, gaudi_irq_handler_single, 0,
			"gaudi single msi", hdev);
	if (rc)
		dev_err(hdev->dev,
			"Failed to request single MSI IRQ\n");

	return rc;
}

static int gaudi_enable_msi_multi(struct hl_device *hdev)
{
	int cq_cnt = hdev->asic_prop.completion_queues_count;
	int rc, i, irq_cnt_init, irq;

	for (i = 0, irq_cnt_init = 0 ; i < cq_cnt ; i++, irq_cnt_init++) {
		irq = gaudi_pci_irq_vector(hdev, i, false);
		rc = request_irq(irq, hl_irq_handler_cq, 0, gaudi_irq_name[i],
				&hdev->completion_queue[i]);
		if (rc) {
			dev_err(hdev->dev, "Failed to request IRQ %d", irq);
			goto free_irqs;
		}
	}

	irq = gaudi_pci_irq_vector(hdev, GAUDI_EVENT_QUEUE_MSI_IDX, true);
	rc = request_irq(irq, hl_irq_handler_eq, 0, gaudi_irq_name[cq_cnt],
				&hdev->event_queue);
	if (rc) {
		dev_err(hdev->dev, "Failed to request IRQ %d", irq);
		goto free_irqs;
	}

	return 0;

free_irqs:
	for (i = 0 ; i < irq_cnt_init ; i++)
		free_irq(gaudi_pci_irq_vector(hdev, i, false),
				&hdev->completion_queue[i]);
	return rc;
}

static int gaudi_enable_msi(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int rc;

	if (gaudi->hw_cap_initialized & HW_CAP_MSI)
		return 0;

	rc = pci_alloc_irq_vectors(hdev->pdev, 1, GAUDI_MSI_ENTRIES,
					PCI_IRQ_MSI);
	if (rc < 0) {
		dev_err(hdev->dev, "MSI: Failed to enable support %d\n", rc);
		return rc;
	}

	if (rc < NUMBER_OF_INTERRUPTS) {
		gaudi->multi_msi_mode = false;
		rc = gaudi_enable_msi_single(hdev);
	} else {
		gaudi->multi_msi_mode = true;
		rc = gaudi_enable_msi_multi(hdev);
	}

	if (rc)
		goto free_pci_irq_vectors;

	gaudi->hw_cap_initialized |= HW_CAP_MSI;

	return 0;

free_pci_irq_vectors:
	pci_free_irq_vectors(hdev->pdev);
	return rc;
}

static void gaudi_sync_irqs(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int i, cq_cnt = hdev->asic_prop.completion_queues_count;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MSI))
		return;

	/* Wait for all pending IRQs to be finished */
	if (gaudi->multi_msi_mode) {
		for (i = 0 ; i < cq_cnt ; i++)
			synchronize_irq(gaudi_pci_irq_vector(hdev, i, false));

		synchronize_irq(gaudi_pci_irq_vector(hdev,
						GAUDI_EVENT_QUEUE_MSI_IDX,
						true));
	} else {
		synchronize_irq(gaudi_pci_irq_vector(hdev, 0, false));
	}
}

static void gaudi_disable_msi(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int i, irq, cq_cnt = hdev->asic_prop.completion_queues_count;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MSI))
		return;

	gaudi_sync_irqs(hdev);

	if (gaudi->multi_msi_mode) {
		irq = gaudi_pci_irq_vector(hdev, GAUDI_EVENT_QUEUE_MSI_IDX,
						true);
		free_irq(irq, &hdev->event_queue);

		for (i = 0 ; i < cq_cnt ; i++) {
			irq = gaudi_pci_irq_vector(hdev, i, false);
			free_irq(irq, &hdev->completion_queue[i]);
		}
	} else {
		free_irq(gaudi_pci_irq_vector(hdev, 0, false), hdev);
	}

	pci_free_irq_vectors(hdev->pdev);

	gaudi->hw_cap_initialized &= ~HW_CAP_MSI;
}

static void gaudi_init_scrambler_sram(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (gaudi->hw_cap_initialized & HW_CAP_SRAM_SCRAMBLER)
		return;

	if (!hdev->sram_scrambler_enable)
		return;

	WREG32(mmNIF_RTR_CTRL_0_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_1_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_2_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_3_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_4_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_5_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_6_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_7_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);

	WREG32(mmSIF_RTR_CTRL_0_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_1_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_2_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_3_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_4_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_5_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_6_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_7_SCRAM_SRAM_EN,
			1 << IF_RTR_CTRL_SCRAM_SRAM_EN_VAL_SHIFT);

	WREG32(mmDMA_IF_E_N_DOWN_CH0_SCRAM_SRAM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_E_N_DOWN_CH1_SCRAM_SRAM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_E_S_DOWN_CH0_SCRAM_SRAM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_E_S_DOWN_CH1_SCRAM_SRAM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_N_DOWN_CH0_SCRAM_SRAM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_N_DOWN_CH1_SCRAM_SRAM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_S_DOWN_CH0_SCRAM_SRAM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_SRAM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_S_DOWN_CH1_SCRAM_SRAM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_SRAM_EN_VAL_SHIFT);

	gaudi->hw_cap_initialized |= HW_CAP_SRAM_SCRAMBLER;
}

static void gaudi_init_scrambler_hbm(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (gaudi->hw_cap_initialized & HW_CAP_HBM_SCRAMBLER)
		return;

	if (!hdev->dram_scrambler_enable)
		return;

	WREG32(mmNIF_RTR_CTRL_0_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_1_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_2_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_3_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_4_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_5_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_6_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_7_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);

	WREG32(mmSIF_RTR_CTRL_0_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_1_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_2_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_3_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_4_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_5_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_6_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_7_SCRAM_HBM_EN,
			1 << IF_RTR_CTRL_SCRAM_HBM_EN_VAL_SHIFT);

	WREG32(mmDMA_IF_E_N_DOWN_CH0_SCRAM_HBM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_E_N_DOWN_CH1_SCRAM_HBM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_E_S_DOWN_CH0_SCRAM_HBM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_E_S_DOWN_CH1_SCRAM_HBM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_N_DOWN_CH0_SCRAM_HBM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_N_DOWN_CH1_SCRAM_HBM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_S_DOWN_CH0_SCRAM_HBM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_S_DOWN_CH1_SCRAM_HBM_EN,
			1 << DMA_IF_DOWN_CHX_SCRAM_HBM_EN_VAL_SHIFT);

	gaudi->hw_cap_initialized |= HW_CAP_HBM_SCRAMBLER;
}

static void gaudi_init_e2e(struct hl_device *hdev)
{
	WREG32(mmSIF_RTR_CTRL_0_E2E_HBM_WR_SIZE, 247 >> 3);
	WREG32(mmSIF_RTR_CTRL_0_E2E_HBM_RD_SIZE, 785 >> 3);
	WREG32(mmSIF_RTR_CTRL_0_E2E_PCI_WR_SIZE, 49);
	WREG32(mmSIF_RTR_CTRL_0_E2E_PCI_RD_SIZE, 101);

	WREG32(mmSIF_RTR_CTRL_1_E2E_HBM_WR_SIZE, 275 >> 3);
	WREG32(mmSIF_RTR_CTRL_1_E2E_HBM_RD_SIZE, 614 >> 3);
	WREG32(mmSIF_RTR_CTRL_1_E2E_PCI_WR_SIZE, 1);
	WREG32(mmSIF_RTR_CTRL_1_E2E_PCI_RD_SIZE, 39);

	WREG32(mmSIF_RTR_CTRL_2_E2E_HBM_WR_SIZE, 1);
	WREG32(mmSIF_RTR_CTRL_2_E2E_HBM_RD_SIZE, 1);
	WREG32(mmSIF_RTR_CTRL_2_E2E_PCI_WR_SIZE, 1);
	WREG32(mmSIF_RTR_CTRL_2_E2E_PCI_RD_SIZE, 32);

	WREG32(mmSIF_RTR_CTRL_3_E2E_HBM_WR_SIZE, 176 >> 3);
	WREG32(mmSIF_RTR_CTRL_3_E2E_HBM_RD_SIZE, 32 >> 3);
	WREG32(mmSIF_RTR_CTRL_3_E2E_PCI_WR_SIZE, 19);
	WREG32(mmSIF_RTR_CTRL_3_E2E_PCI_RD_SIZE, 32);

	WREG32(mmSIF_RTR_CTRL_4_E2E_HBM_WR_SIZE, 176 >> 3);
	WREG32(mmSIF_RTR_CTRL_4_E2E_HBM_RD_SIZE, 32 >> 3);
	WREG32(mmSIF_RTR_CTRL_4_E2E_PCI_WR_SIZE, 19);
	WREG32(mmSIF_RTR_CTRL_4_E2E_PCI_RD_SIZE, 32);

	WREG32(mmSIF_RTR_CTRL_5_E2E_HBM_WR_SIZE, 1);
	WREG32(mmSIF_RTR_CTRL_5_E2E_HBM_RD_SIZE, 1);
	WREG32(mmSIF_RTR_CTRL_5_E2E_PCI_WR_SIZE, 1);
	WREG32(mmSIF_RTR_CTRL_5_E2E_PCI_RD_SIZE, 32);

	WREG32(mmSIF_RTR_CTRL_6_E2E_HBM_WR_SIZE, 275 >> 3);
	WREG32(mmSIF_RTR_CTRL_6_E2E_HBM_RD_SIZE, 614 >> 3);
	WREG32(mmSIF_RTR_CTRL_6_E2E_PCI_WR_SIZE, 1);
	WREG32(mmSIF_RTR_CTRL_6_E2E_PCI_RD_SIZE, 39);

	WREG32(mmSIF_RTR_CTRL_7_E2E_HBM_WR_SIZE, 297 >> 3);
	WREG32(mmSIF_RTR_CTRL_7_E2E_HBM_RD_SIZE, 908 >> 3);
	WREG32(mmSIF_RTR_CTRL_7_E2E_PCI_WR_SIZE, 19);
	WREG32(mmSIF_RTR_CTRL_7_E2E_PCI_RD_SIZE, 19);

	WREG32(mmNIF_RTR_CTRL_0_E2E_HBM_WR_SIZE, 318 >> 3);
	WREG32(mmNIF_RTR_CTRL_0_E2E_HBM_RD_SIZE, 956 >> 3);
	WREG32(mmNIF_RTR_CTRL_0_E2E_PCI_WR_SIZE, 79);
	WREG32(mmNIF_RTR_CTRL_0_E2E_PCI_RD_SIZE, 163);

	WREG32(mmNIF_RTR_CTRL_1_E2E_HBM_WR_SIZE, 275 >> 3);
	WREG32(mmNIF_RTR_CTRL_1_E2E_HBM_RD_SIZE, 614 >> 3);
	WREG32(mmNIF_RTR_CTRL_1_E2E_PCI_WR_SIZE, 1);
	WREG32(mmNIF_RTR_CTRL_1_E2E_PCI_RD_SIZE, 39);

	WREG32(mmNIF_RTR_CTRL_2_E2E_HBM_WR_SIZE, 1);
	WREG32(mmNIF_RTR_CTRL_2_E2E_HBM_RD_SIZE, 1);
	WREG32(mmNIF_RTR_CTRL_2_E2E_PCI_WR_SIZE, 1);
	WREG32(mmNIF_RTR_CTRL_2_E2E_PCI_RD_SIZE, 32);

	WREG32(mmNIF_RTR_CTRL_3_E2E_HBM_WR_SIZE, 176 >> 3);
	WREG32(mmNIF_RTR_CTRL_3_E2E_HBM_RD_SIZE, 32 >> 3);
	WREG32(mmNIF_RTR_CTRL_3_E2E_PCI_WR_SIZE, 19);
	WREG32(mmNIF_RTR_CTRL_3_E2E_PCI_RD_SIZE, 32);

	WREG32(mmNIF_RTR_CTRL_4_E2E_HBM_WR_SIZE, 176 >> 3);
	WREG32(mmNIF_RTR_CTRL_4_E2E_HBM_RD_SIZE, 32 >> 3);
	WREG32(mmNIF_RTR_CTRL_4_E2E_PCI_WR_SIZE, 19);
	WREG32(mmNIF_RTR_CTRL_4_E2E_PCI_RD_SIZE, 32);

	WREG32(mmNIF_RTR_CTRL_5_E2E_HBM_WR_SIZE, 1);
	WREG32(mmNIF_RTR_CTRL_5_E2E_HBM_RD_SIZE, 1);
	WREG32(mmNIF_RTR_CTRL_5_E2E_PCI_WR_SIZE, 1);
	WREG32(mmNIF_RTR_CTRL_5_E2E_PCI_RD_SIZE, 32);

	WREG32(mmNIF_RTR_CTRL_6_E2E_HBM_WR_SIZE, 275 >> 3);
	WREG32(mmNIF_RTR_CTRL_6_E2E_HBM_RD_SIZE, 614 >> 3);
	WREG32(mmNIF_RTR_CTRL_6_E2E_PCI_WR_SIZE, 1);
	WREG32(mmNIF_RTR_CTRL_6_E2E_PCI_RD_SIZE, 39);

	WREG32(mmNIF_RTR_CTRL_7_E2E_HBM_WR_SIZE, 318 >> 3);
	WREG32(mmNIF_RTR_CTRL_7_E2E_HBM_RD_SIZE, 956 >> 3);
	WREG32(mmNIF_RTR_CTRL_7_E2E_PCI_WR_SIZE, 79);
	WREG32(mmNIF_RTR_CTRL_7_E2E_PCI_RD_SIZE, 79);

	WREG32(mmDMA_IF_E_N_DOWN_CH0_E2E_HBM_WR_SIZE, 344 >> 3);
	WREG32(mmDMA_IF_E_N_DOWN_CH0_E2E_HBM_RD_SIZE, 1000 >> 3);
	WREG32(mmDMA_IF_E_N_DOWN_CH0_E2E_PCI_WR_SIZE, 162);
	WREG32(mmDMA_IF_E_N_DOWN_CH0_E2E_PCI_RD_SIZE, 338);

	WREG32(mmDMA_IF_E_N_DOWN_CH1_E2E_HBM_WR_SIZE, 344 >> 3);
	WREG32(mmDMA_IF_E_N_DOWN_CH1_E2E_HBM_RD_SIZE, 1000 >> 3);
	WREG32(mmDMA_IF_E_N_DOWN_CH1_E2E_PCI_WR_SIZE, 162);
	WREG32(mmDMA_IF_E_N_DOWN_CH1_E2E_PCI_RD_SIZE, 338);

	WREG32(mmDMA_IF_E_S_DOWN_CH0_E2E_HBM_WR_SIZE, 344 >> 3);
	WREG32(mmDMA_IF_E_S_DOWN_CH0_E2E_HBM_RD_SIZE, 1000 >> 3);
	WREG32(mmDMA_IF_E_S_DOWN_CH0_E2E_PCI_WR_SIZE, 162);
	WREG32(mmDMA_IF_E_S_DOWN_CH0_E2E_PCI_RD_SIZE, 338);

	WREG32(mmDMA_IF_E_S_DOWN_CH1_E2E_HBM_WR_SIZE, 344 >> 3);
	WREG32(mmDMA_IF_E_S_DOWN_CH1_E2E_HBM_RD_SIZE, 1000 >> 3);
	WREG32(mmDMA_IF_E_S_DOWN_CH1_E2E_PCI_WR_SIZE, 162);
	WREG32(mmDMA_IF_E_S_DOWN_CH1_E2E_PCI_RD_SIZE, 338);

	WREG32(mmDMA_IF_W_N_DOWN_CH0_E2E_HBM_WR_SIZE, 344 >> 3);
	WREG32(mmDMA_IF_W_N_DOWN_CH0_E2E_HBM_RD_SIZE, 1000 >> 3);
	WREG32(mmDMA_IF_W_N_DOWN_CH0_E2E_PCI_WR_SIZE, 162);
	WREG32(mmDMA_IF_W_N_DOWN_CH0_E2E_PCI_RD_SIZE, 338);

	WREG32(mmDMA_IF_W_N_DOWN_CH1_E2E_HBM_WR_SIZE, 344 >> 3);
	WREG32(mmDMA_IF_W_N_DOWN_CH1_E2E_HBM_RD_SIZE, 1000 >> 3);
	WREG32(mmDMA_IF_W_N_DOWN_CH1_E2E_PCI_WR_SIZE, 162);
	WREG32(mmDMA_IF_W_N_DOWN_CH1_E2E_PCI_RD_SIZE, 338);

	WREG32(mmDMA_IF_W_S_DOWN_CH0_E2E_HBM_WR_SIZE, 344 >> 3);
	WREG32(mmDMA_IF_W_S_DOWN_CH0_E2E_HBM_RD_SIZE, 1000 >> 3);
	WREG32(mmDMA_IF_W_S_DOWN_CH0_E2E_PCI_WR_SIZE, 162);
	WREG32(mmDMA_IF_W_S_DOWN_CH0_E2E_PCI_RD_SIZE, 338);

	WREG32(mmDMA_IF_W_S_DOWN_CH1_E2E_HBM_WR_SIZE, 344 >> 3);
	WREG32(mmDMA_IF_W_S_DOWN_CH1_E2E_HBM_RD_SIZE, 1000 >> 3);
	WREG32(mmDMA_IF_W_S_DOWN_CH1_E2E_PCI_WR_SIZE, 162);
	WREG32(mmDMA_IF_W_S_DOWN_CH1_E2E_PCI_RD_SIZE, 338);

	if (!hdev->dram_scrambler_enable) {
		WREG32(mmSIF_RTR_CTRL_0_NL_HBM_SEL_0, 0x21);
		WREG32(mmSIF_RTR_CTRL_0_NL_HBM_SEL_1, 0x22);
		WREG32(mmSIF_RTR_CTRL_0_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmSIF_RTR_CTRL_0_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmSIF_RTR_CTRL_1_NL_HBM_SEL_0, 0x21);
		WREG32(mmSIF_RTR_CTRL_1_NL_HBM_SEL_1, 0x22);
		WREG32(mmSIF_RTR_CTRL_1_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmSIF_RTR_CTRL_1_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmSIF_RTR_CTRL_2_NL_HBM_SEL_0, 0x21);
		WREG32(mmSIF_RTR_CTRL_2_NL_HBM_SEL_1, 0x22);
		WREG32(mmSIF_RTR_CTRL_2_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmSIF_RTR_CTRL_2_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmSIF_RTR_CTRL_3_NL_HBM_SEL_0, 0x21);
		WREG32(mmSIF_RTR_CTRL_3_NL_HBM_SEL_1, 0x22);
		WREG32(mmSIF_RTR_CTRL_3_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmSIF_RTR_CTRL_3_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmSIF_RTR_CTRL_4_NL_HBM_SEL_0, 0x21);
		WREG32(mmSIF_RTR_CTRL_4_NL_HBM_SEL_1, 0x22);
		WREG32(mmSIF_RTR_CTRL_4_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmSIF_RTR_CTRL_4_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmSIF_RTR_CTRL_5_NL_HBM_SEL_0, 0x21);
		WREG32(mmSIF_RTR_CTRL_5_NL_HBM_SEL_1, 0x22);
		WREG32(mmSIF_RTR_CTRL_5_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmSIF_RTR_CTRL_5_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmSIF_RTR_CTRL_6_NL_HBM_SEL_0, 0x21);
		WREG32(mmSIF_RTR_CTRL_6_NL_HBM_SEL_1, 0x22);
		WREG32(mmSIF_RTR_CTRL_6_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmSIF_RTR_CTRL_6_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmSIF_RTR_CTRL_7_NL_HBM_SEL_0, 0x21);
		WREG32(mmSIF_RTR_CTRL_7_NL_HBM_SEL_1, 0x22);
		WREG32(mmSIF_RTR_CTRL_7_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmSIF_RTR_CTRL_7_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmNIF_RTR_CTRL_0_NL_HBM_SEL_0, 0x21);
		WREG32(mmNIF_RTR_CTRL_0_NL_HBM_SEL_1, 0x22);
		WREG32(mmNIF_RTR_CTRL_0_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmNIF_RTR_CTRL_0_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmNIF_RTR_CTRL_1_NL_HBM_SEL_0, 0x21);
		WREG32(mmNIF_RTR_CTRL_1_NL_HBM_SEL_1, 0x22);
		WREG32(mmNIF_RTR_CTRL_1_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmNIF_RTR_CTRL_1_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmNIF_RTR_CTRL_2_NL_HBM_SEL_0, 0x21);
		WREG32(mmNIF_RTR_CTRL_2_NL_HBM_SEL_1, 0x22);
		WREG32(mmNIF_RTR_CTRL_2_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmNIF_RTR_CTRL_2_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmNIF_RTR_CTRL_3_NL_HBM_SEL_0, 0x21);
		WREG32(mmNIF_RTR_CTRL_3_NL_HBM_SEL_1, 0x22);
		WREG32(mmNIF_RTR_CTRL_3_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmNIF_RTR_CTRL_3_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmNIF_RTR_CTRL_4_NL_HBM_SEL_0, 0x21);
		WREG32(mmNIF_RTR_CTRL_4_NL_HBM_SEL_1, 0x22);
		WREG32(mmNIF_RTR_CTRL_4_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmNIF_RTR_CTRL_4_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmNIF_RTR_CTRL_5_NL_HBM_SEL_0, 0x21);
		WREG32(mmNIF_RTR_CTRL_5_NL_HBM_SEL_1, 0x22);
		WREG32(mmNIF_RTR_CTRL_5_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmNIF_RTR_CTRL_5_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmNIF_RTR_CTRL_6_NL_HBM_SEL_0, 0x21);
		WREG32(mmNIF_RTR_CTRL_6_NL_HBM_SEL_1, 0x22);
		WREG32(mmNIF_RTR_CTRL_6_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmNIF_RTR_CTRL_6_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmNIF_RTR_CTRL_7_NL_HBM_SEL_0, 0x21);
		WREG32(mmNIF_RTR_CTRL_7_NL_HBM_SEL_1, 0x22);
		WREG32(mmNIF_RTR_CTRL_7_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmNIF_RTR_CTRL_7_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmDMA_IF_E_N_DOWN_CH0_NL_HBM_SEL_0, 0x21);
		WREG32(mmDMA_IF_E_N_DOWN_CH0_NL_HBM_SEL_1, 0x22);
		WREG32(mmDMA_IF_E_N_DOWN_CH0_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmDMA_IF_E_N_DOWN_CH0_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmDMA_IF_E_N_DOWN_CH1_NL_HBM_SEL_0, 0x21);
		WREG32(mmDMA_IF_E_N_DOWN_CH1_NL_HBM_SEL_1, 0x22);
		WREG32(mmDMA_IF_E_N_DOWN_CH1_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmDMA_IF_E_N_DOWN_CH1_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmDMA_IF_E_S_DOWN_CH0_NL_HBM_SEL_0, 0x21);
		WREG32(mmDMA_IF_E_S_DOWN_CH0_NL_HBM_SEL_1, 0x22);
		WREG32(mmDMA_IF_E_S_DOWN_CH0_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmDMA_IF_E_S_DOWN_CH0_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmDMA_IF_E_S_DOWN_CH1_NL_HBM_SEL_0, 0x21);
		WREG32(mmDMA_IF_E_S_DOWN_CH1_NL_HBM_SEL_1, 0x22);
		WREG32(mmDMA_IF_E_S_DOWN_CH1_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmDMA_IF_E_S_DOWN_CH1_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmDMA_IF_W_N_DOWN_CH0_NL_HBM_SEL_0, 0x21);
		WREG32(mmDMA_IF_W_N_DOWN_CH0_NL_HBM_SEL_1, 0x22);
		WREG32(mmDMA_IF_W_N_DOWN_CH0_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmDMA_IF_W_N_DOWN_CH0_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmDMA_IF_W_N_DOWN_CH1_NL_HBM_SEL_0, 0x21);
		WREG32(mmDMA_IF_W_N_DOWN_CH1_NL_HBM_SEL_1, 0x22);
		WREG32(mmDMA_IF_W_N_DOWN_CH1_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmDMA_IF_W_N_DOWN_CH1_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmDMA_IF_W_S_DOWN_CH0_NL_HBM_SEL_0, 0x21);
		WREG32(mmDMA_IF_W_S_DOWN_CH0_NL_HBM_SEL_1, 0x22);
		WREG32(mmDMA_IF_W_S_DOWN_CH0_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmDMA_IF_W_S_DOWN_CH0_NL_HBM_PC_SEL_3, 0x20);

		WREG32(mmDMA_IF_W_S_DOWN_CH1_NL_HBM_SEL_0, 0x21);
		WREG32(mmDMA_IF_W_S_DOWN_CH1_NL_HBM_SEL_1, 0x22);
		WREG32(mmDMA_IF_W_S_DOWN_CH1_NL_HBM_OFFSET_18, 0x1F);
		WREG32(mmDMA_IF_W_S_DOWN_CH1_NL_HBM_PC_SEL_3, 0x20);
	}

	WREG32(mmSIF_RTR_CTRL_0_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_0_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmSIF_RTR_CTRL_1_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_1_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmSIF_RTR_CTRL_2_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_2_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmSIF_RTR_CTRL_3_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_3_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmSIF_RTR_CTRL_4_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_4_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmSIF_RTR_CTRL_5_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_5_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmSIF_RTR_CTRL_6_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_6_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmSIF_RTR_CTRL_7_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmSIF_RTR_CTRL_7_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmNIF_RTR_CTRL_0_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_0_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmNIF_RTR_CTRL_1_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_1_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmNIF_RTR_CTRL_2_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_2_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmNIF_RTR_CTRL_3_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_3_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmNIF_RTR_CTRL_4_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_4_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmNIF_RTR_CTRL_5_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_5_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmNIF_RTR_CTRL_6_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_6_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmNIF_RTR_CTRL_7_E2E_HBM_EN,
			1 << IF_RTR_CTRL_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmNIF_RTR_CTRL_7_E2E_PCI_EN,
			1 << IF_RTR_CTRL_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmDMA_IF_E_N_DOWN_CH0_E2E_HBM_EN,
			1 << DMA_IF_DOWN_CHX_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_E_N_DOWN_CH0_E2E_PCI_EN,
			1 << DMA_IF_DOWN_CHX_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmDMA_IF_E_N_DOWN_CH1_E2E_HBM_EN,
			1 << DMA_IF_DOWN_CHX_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_E_N_DOWN_CH1_E2E_PCI_EN,
			1 << DMA_IF_DOWN_CHX_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmDMA_IF_E_S_DOWN_CH0_E2E_HBM_EN,
			1 << DMA_IF_DOWN_CHX_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_E_S_DOWN_CH0_E2E_PCI_EN,
			1 << DMA_IF_DOWN_CHX_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmDMA_IF_E_S_DOWN_CH1_E2E_HBM_EN,
			1 << DMA_IF_DOWN_CHX_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_E_S_DOWN_CH1_E2E_PCI_EN,
			1 << DMA_IF_DOWN_CHX_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmDMA_IF_W_N_DOWN_CH0_E2E_HBM_EN,
			1 << DMA_IF_DOWN_CHX_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_N_DOWN_CH0_E2E_PCI_EN,
			1 << DMA_IF_DOWN_CHX_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmDMA_IF_W_N_DOWN_CH1_E2E_HBM_EN,
			1 << DMA_IF_DOWN_CHX_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_N_DOWN_CH1_E2E_PCI_EN,
			1 << DMA_IF_DOWN_CHX_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmDMA_IF_W_S_DOWN_CH0_E2E_HBM_EN,
			1 << DMA_IF_DOWN_CHX_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_S_DOWN_CH0_E2E_PCI_EN,
			1 << DMA_IF_DOWN_CHX_E2E_PCI_EN_VAL_SHIFT);

	WREG32(mmDMA_IF_W_S_DOWN_CH1_E2E_HBM_EN,
			1 << DMA_IF_DOWN_CHX_E2E_HBM_EN_VAL_SHIFT);
	WREG32(mmDMA_IF_W_S_DOWN_CH1_E2E_PCI_EN,
			1 << DMA_IF_DOWN_CHX_E2E_PCI_EN_VAL_SHIFT);
}

static void gaudi_init_hbm_cred(struct hl_device *hdev)
{
	uint32_t hbm0_wr, hbm1_wr, hbm0_rd, hbm1_rd;

	hbm0_wr = 0x33333333;
	hbm0_rd = 0x77777777;
	hbm1_wr = 0x55555555;
	hbm1_rd = 0xDDDDDDDD;

	WREG32(mmDMA_IF_E_N_HBM0_WR_CRED_CNT, hbm0_wr);
	WREG32(mmDMA_IF_E_N_HBM1_WR_CRED_CNT, hbm1_wr);
	WREG32(mmDMA_IF_E_N_HBM0_RD_CRED_CNT, hbm0_rd);
	WREG32(mmDMA_IF_E_N_HBM1_RD_CRED_CNT, hbm1_rd);

	WREG32(mmDMA_IF_E_S_HBM0_WR_CRED_CNT, hbm0_wr);
	WREG32(mmDMA_IF_E_S_HBM1_WR_CRED_CNT, hbm1_wr);
	WREG32(mmDMA_IF_E_S_HBM0_RD_CRED_CNT, hbm0_rd);
	WREG32(mmDMA_IF_E_S_HBM1_RD_CRED_CNT, hbm1_rd);

	WREG32(mmDMA_IF_W_N_HBM0_WR_CRED_CNT, hbm0_wr);
	WREG32(mmDMA_IF_W_N_HBM1_WR_CRED_CNT, hbm1_wr);
	WREG32(mmDMA_IF_W_N_HBM0_RD_CRED_CNT, hbm0_rd);
	WREG32(mmDMA_IF_W_N_HBM1_RD_CRED_CNT, hbm1_rd);

	WREG32(mmDMA_IF_W_S_HBM0_WR_CRED_CNT, hbm0_wr);
	WREG32(mmDMA_IF_W_S_HBM1_WR_CRED_CNT, hbm1_wr);
	WREG32(mmDMA_IF_W_S_HBM0_RD_CRED_CNT, hbm0_rd);
	WREG32(mmDMA_IF_W_S_HBM1_RD_CRED_CNT, hbm1_rd);

	WREG32(mmDMA_IF_E_N_HBM_CRED_EN_0,
			(1 << DMA_IF_HBM_CRED_EN_READ_CREDIT_EN_SHIFT) |
			(1 << DMA_IF_HBM_CRED_EN_WRITE_CREDIT_EN_SHIFT));
	WREG32(mmDMA_IF_E_S_HBM_CRED_EN_0,
			(1 << DMA_IF_HBM_CRED_EN_READ_CREDIT_EN_SHIFT) |
			(1 << DMA_IF_HBM_CRED_EN_WRITE_CREDIT_EN_SHIFT));
	WREG32(mmDMA_IF_W_N_HBM_CRED_EN_0,
			(1 << DMA_IF_HBM_CRED_EN_READ_CREDIT_EN_SHIFT) |
			(1 << DMA_IF_HBM_CRED_EN_WRITE_CREDIT_EN_SHIFT));
	WREG32(mmDMA_IF_W_S_HBM_CRED_EN_0,
			(1 << DMA_IF_HBM_CRED_EN_READ_CREDIT_EN_SHIFT) |
			(1 << DMA_IF_HBM_CRED_EN_WRITE_CREDIT_EN_SHIFT));

	WREG32(mmDMA_IF_E_N_HBM_CRED_EN_1,
			(1 << DMA_IF_HBM_CRED_EN_READ_CREDIT_EN_SHIFT) |
			(1 << DMA_IF_HBM_CRED_EN_WRITE_CREDIT_EN_SHIFT));
	WREG32(mmDMA_IF_E_S_HBM_CRED_EN_1,
			(1 << DMA_IF_HBM_CRED_EN_READ_CREDIT_EN_SHIFT) |
			(1 << DMA_IF_HBM_CRED_EN_WRITE_CREDIT_EN_SHIFT));
	WREG32(mmDMA_IF_W_N_HBM_CRED_EN_1,
			(1 << DMA_IF_HBM_CRED_EN_READ_CREDIT_EN_SHIFT) |
			(1 << DMA_IF_HBM_CRED_EN_WRITE_CREDIT_EN_SHIFT));
	WREG32(mmDMA_IF_W_S_HBM_CRED_EN_1,
			(1 << DMA_IF_HBM_CRED_EN_READ_CREDIT_EN_SHIFT) |
			(1 << DMA_IF_HBM_CRED_EN_WRITE_CREDIT_EN_SHIFT));
}

static void gaudi_init_golden_registers(struct hl_device *hdev)
{
	u32 tpc_offset;
	int tpc_id, i;

	gaudi_init_e2e(hdev);

	gaudi_init_hbm_cred(hdev);

	hdev->asic_funcs->disable_clock_gating(hdev);

	for (tpc_id = 0, tpc_offset = 0;
				tpc_id < TPC_NUMBER_OF_ENGINES;
				tpc_id++, tpc_offset += TPC_CFG_OFFSET) {
		/* Mask all arithmetic interrupts from TPC */
		WREG32(mmTPC0_CFG_TPC_INTR_MASK + tpc_offset, 0x8FFF);
		/* Set 16 cache lines */
		WREG32_FIELD(TPC0_CFG_MSS_CONFIG, tpc_offset,
				ICACHE_FETCH_LINE_NUM, 2);
	}

	/* Make sure 1st 128 bytes in SRAM are 0 for Tensor DMA */
	for (i = 0 ; i < 128 ; i += 8)
		writeq(0, hdev->pcie_bar[SRAM_BAR_ID] + i);

	WREG32(mmMME0_CTRL_EUS_ROLLUP_CNT_ADD, 3);
	WREG32(mmMME1_CTRL_EUS_ROLLUP_CNT_ADD, 3);
	WREG32(mmMME2_CTRL_EUS_ROLLUP_CNT_ADD, 3);
	WREG32(mmMME3_CTRL_EUS_ROLLUP_CNT_ADD, 3);
}

static void gaudi_init_pci_dma_qman(struct hl_device *hdev, int dma_id,
					int qman_id, dma_addr_t qman_pq_addr)
{
	u32 mtr_base_en_lo, mtr_base_en_hi, mtr_base_ws_lo, mtr_base_ws_hi;
	u32 so_base_en_lo, so_base_en_hi, so_base_ws_lo, so_base_ws_hi;
	u32 q_off, dma_qm_offset;
	u32 dma_qm_err_cfg;

	dma_qm_offset = dma_id * DMA_QMAN_OFFSET;

	mtr_base_en_lo = lower_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	mtr_base_en_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	so_base_en_lo = lower_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0);
	so_base_en_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0);
	mtr_base_ws_lo = lower_32_bits(CFG_BASE +
				mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	mtr_base_ws_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	so_base_ws_lo = lower_32_bits(CFG_BASE +
				mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_SOB_OBJ_0);
	so_base_ws_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_SOB_OBJ_0);

	q_off = dma_qm_offset + qman_id * 4;

	WREG32(mmDMA0_QM_PQ_BASE_LO_0 + q_off, lower_32_bits(qman_pq_addr));
	WREG32(mmDMA0_QM_PQ_BASE_HI_0 + q_off, upper_32_bits(qman_pq_addr));

	WREG32(mmDMA0_QM_PQ_SIZE_0 + q_off, ilog2(HL_QUEUE_LENGTH));
	WREG32(mmDMA0_QM_PQ_PI_0 + q_off, 0);
	WREG32(mmDMA0_QM_PQ_CI_0 + q_off, 0);

	WREG32(mmDMA0_QM_CP_LDMA_TSIZE_OFFSET_0 + q_off, QMAN_LDMA_SIZE_OFFSET);
	WREG32(mmDMA0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_SRC_OFFSET);
	WREG32(mmDMA0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_DST_OFFSET);

	WREG32(mmDMA0_QM_CP_MSG_BASE0_ADDR_LO_0 + q_off, mtr_base_en_lo);
	WREG32(mmDMA0_QM_CP_MSG_BASE0_ADDR_HI_0 + q_off, mtr_base_en_hi);
	WREG32(mmDMA0_QM_CP_MSG_BASE1_ADDR_LO_0 + q_off, so_base_en_lo);
	WREG32(mmDMA0_QM_CP_MSG_BASE1_ADDR_HI_0 + q_off, so_base_en_hi);
	WREG32(mmDMA0_QM_CP_MSG_BASE2_ADDR_LO_0 + q_off, mtr_base_ws_lo);
	WREG32(mmDMA0_QM_CP_MSG_BASE2_ADDR_HI_0 + q_off, mtr_base_ws_hi);
	WREG32(mmDMA0_QM_CP_MSG_BASE3_ADDR_LO_0 + q_off, so_base_ws_lo);
	WREG32(mmDMA0_QM_CP_MSG_BASE3_ADDR_HI_0 + q_off, so_base_ws_hi);

	WREG32(mmDMA0_QM_CP_BARRIER_CFG_0 + q_off, 0x100);

	/* The following configuration is needed only once per QMAN */
	if (qman_id == 0) {
		/* Configure RAZWI IRQ */
		dma_qm_err_cfg = PCI_DMA_QMAN_GLBL_ERR_CFG_MSG_EN_MASK;
		if (hdev->stop_on_err) {
			dma_qm_err_cfg |=
				PCI_DMA_QMAN_GLBL_ERR_CFG_STOP_ON_ERR_EN_MASK;
		}

		WREG32(mmDMA0_QM_GLBL_ERR_CFG + dma_qm_offset, dma_qm_err_cfg);
		WREG32(mmDMA0_QM_GLBL_ERR_ADDR_LO + dma_qm_offset,
			lower_32_bits(CFG_BASE +
					mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR));
		WREG32(mmDMA0_QM_GLBL_ERR_ADDR_HI + dma_qm_offset,
			upper_32_bits(CFG_BASE +
					mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR));
		WREG32(mmDMA0_QM_GLBL_ERR_WDATA + dma_qm_offset,
			gaudi_irq_map_table[GAUDI_EVENT_DMA0_QM].cpu_id +
									dma_id);

		WREG32(mmDMA0_QM_ARB_ERR_MSG_EN + dma_qm_offset,
				QM_ARB_ERR_MSG_EN_MASK);

		/* Increase ARB WDT to support streams architecture */
		WREG32(mmDMA0_QM_ARB_SLV_CHOISE_WDT + dma_qm_offset,
				GAUDI_ARB_WDT_TIMEOUT);

		WREG32(mmDMA0_QM_GLBL_PROT + dma_qm_offset,
				QMAN_EXTERNAL_MAKE_TRUSTED);

		WREG32(mmDMA0_QM_GLBL_CFG1 + dma_qm_offset, 0);
	}
}

static void gaudi_init_dma_core(struct hl_device *hdev, int dma_id)
{
	u32 dma_offset = dma_id * DMA_CORE_OFFSET;
	u32 dma_err_cfg = 1 << DMA0_CORE_ERR_CFG_ERR_MSG_EN_SHIFT;

	/* Set to maximum possible according to physical size */
	WREG32(mmDMA0_CORE_RD_MAX_OUTSTAND + dma_offset, 0);
	WREG32(mmDMA0_CORE_RD_MAX_SIZE + dma_offset, 0);

	/* WA for H/W bug H3-2116 */
	WREG32(mmDMA0_CORE_LBW_MAX_OUTSTAND + dma_offset, 15);

	/* STOP_ON bit implies no completion to operation in case of RAZWI */
	if (hdev->stop_on_err)
		dma_err_cfg |= 1 << DMA0_CORE_ERR_CFG_STOP_ON_ERR_SHIFT;

	WREG32(mmDMA0_CORE_ERR_CFG + dma_offset, dma_err_cfg);
	WREG32(mmDMA0_CORE_ERRMSG_ADDR_LO + dma_offset,
		lower_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR));
	WREG32(mmDMA0_CORE_ERRMSG_ADDR_HI + dma_offset,
		upper_32_bits(CFG_BASE + mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR));
	WREG32(mmDMA0_CORE_ERRMSG_WDATA + dma_offset,
		gaudi_irq_map_table[GAUDI_EVENT_DMA0_CORE].cpu_id + dma_id);
	WREG32(mmDMA0_CORE_PROT + dma_offset,
			1 << DMA0_CORE_PROT_ERR_VAL_SHIFT);
	/* If the channel is secured, it should be in MMU bypass mode */
	WREG32(mmDMA0_CORE_SECURE_PROPS + dma_offset,
			1 << DMA0_CORE_SECURE_PROPS_MMBP_SHIFT);
	WREG32(mmDMA0_CORE_CFG_0 + dma_offset, 1 << DMA0_CORE_CFG_0_EN_SHIFT);
}

static void gaudi_enable_qman(struct hl_device *hdev, int dma_id,
				u32 enable_mask)
{
	u32 dma_qm_offset = dma_id * DMA_QMAN_OFFSET;

	WREG32(mmDMA0_QM_GLBL_CFG0 + dma_qm_offset, enable_mask);
}

static void gaudi_init_pci_dma_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct hl_hw_queue *q;
	int i, j, dma_id, cpu_skip, nic_skip, cq_id = 0, q_idx, msi_vec = 0;

	if (gaudi->hw_cap_initialized & HW_CAP_PCI_DMA)
		return;

	for (i = 0 ; i < PCI_DMA_NUMBER_OF_CHNLS ; i++) {
		dma_id = gaudi_dma_assignment[i];
		/*
		 * For queues after the CPU Q need to add 1 to get the correct
		 * queue. In addition, need to add the CPU EQ and NIC IRQs in
		 * order to get the correct MSI register.
		 */
		if (dma_id > 1) {
			cpu_skip = 1;
			nic_skip = NIC_NUMBER_OF_ENGINES;
		} else {
			cpu_skip = 0;
			nic_skip = 0;
		}

		for (j = 0 ; j < QMAN_STREAMS ; j++) {
			q_idx = 4 * dma_id + j + cpu_skip;
			q = &hdev->kernel_queues[q_idx];
			q->cq_id = cq_id++;
			q->msi_vec = nic_skip + cpu_skip + msi_vec++;
			gaudi_init_pci_dma_qman(hdev, dma_id, j,
						q->bus_address);
		}

		gaudi_init_dma_core(hdev, dma_id);

		gaudi_enable_qman(hdev, dma_id, PCI_DMA_QMAN_ENABLE);
	}

	gaudi->hw_cap_initialized |= HW_CAP_PCI_DMA;
}

static void gaudi_init_hbm_dma_qman(struct hl_device *hdev, int dma_id,
					int qman_id, u64 qman_base_addr)
{
	u32 mtr_base_lo, mtr_base_hi;
	u32 so_base_lo, so_base_hi;
	u32 q_off, dma_qm_offset;
	u32 dma_qm_err_cfg;

	dma_qm_offset = dma_id * DMA_QMAN_OFFSET;

	mtr_base_lo = lower_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	mtr_base_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	so_base_lo = lower_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0);

	q_off = dma_qm_offset + qman_id * 4;

	if (qman_id < 4) {
		WREG32(mmDMA0_QM_PQ_BASE_LO_0 + q_off,
					lower_32_bits(qman_base_addr));
		WREG32(mmDMA0_QM_PQ_BASE_HI_0 + q_off,
					upper_32_bits(qman_base_addr));

		WREG32(mmDMA0_QM_PQ_SIZE_0 + q_off, ilog2(HBM_DMA_QMAN_LENGTH));
		WREG32(mmDMA0_QM_PQ_PI_0 + q_off, 0);
		WREG32(mmDMA0_QM_PQ_CI_0 + q_off, 0);

		WREG32(mmDMA0_QM_CP_LDMA_TSIZE_OFFSET_0 + q_off,
							QMAN_CPDMA_SIZE_OFFSET);
		WREG32(mmDMA0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0 + q_off,
							QMAN_CPDMA_SRC_OFFSET);
		WREG32(mmDMA0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0 + q_off,
							QMAN_CPDMA_DST_OFFSET);
	} else {
		WREG32(mmDMA0_QM_CP_LDMA_TSIZE_OFFSET_0 + q_off,
							QMAN_LDMA_SIZE_OFFSET);
		WREG32(mmDMA0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_SRC_OFFSET);
		WREG32(mmDMA0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_DST_OFFSET);

		/* Configure RAZWI IRQ */
		dma_qm_err_cfg = HBM_DMA_QMAN_GLBL_ERR_CFG_MSG_EN_MASK;
		if (hdev->stop_on_err) {
			dma_qm_err_cfg |=
				HBM_DMA_QMAN_GLBL_ERR_CFG_STOP_ON_ERR_EN_MASK;
		}
		WREG32(mmDMA0_QM_GLBL_ERR_CFG + dma_qm_offset, dma_qm_err_cfg);

		WREG32(mmDMA0_QM_GLBL_ERR_ADDR_LO + dma_qm_offset,
			lower_32_bits(CFG_BASE +
					mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR));
		WREG32(mmDMA0_QM_GLBL_ERR_ADDR_HI + dma_qm_offset,
			upper_32_bits(CFG_BASE +
					mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR));
		WREG32(mmDMA0_QM_GLBL_ERR_WDATA + dma_qm_offset,
			gaudi_irq_map_table[GAUDI_EVENT_DMA0_QM].cpu_id +
									dma_id);

		WREG32(mmDMA0_QM_ARB_ERR_MSG_EN + dma_qm_offset,
				QM_ARB_ERR_MSG_EN_MASK);

		/* Increase ARB WDT to support streams architecture */
		WREG32(mmDMA0_QM_ARB_SLV_CHOISE_WDT + dma_qm_offset,
				GAUDI_ARB_WDT_TIMEOUT);

		WREG32(mmDMA0_QM_GLBL_CFG1 + dma_qm_offset, 0);
		WREG32(mmDMA0_QM_GLBL_PROT + dma_qm_offset,
				QMAN_INTERNAL_MAKE_TRUSTED);
	}

	WREG32(mmDMA0_QM_CP_MSG_BASE0_ADDR_LO_0 + q_off, mtr_base_lo);
	WREG32(mmDMA0_QM_CP_MSG_BASE0_ADDR_HI_0 + q_off, mtr_base_hi);
	WREG32(mmDMA0_QM_CP_MSG_BASE1_ADDR_LO_0 + q_off, so_base_lo);
	WREG32(mmDMA0_QM_CP_MSG_BASE1_ADDR_HI_0 + q_off, so_base_hi);
}

static void gaudi_init_hbm_dma_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_internal_qman_info *q;
	u64 qman_base_addr;
	int i, j, dma_id, internal_q_index;

	if (gaudi->hw_cap_initialized & HW_CAP_HBM_DMA)
		return;

	for (i = 0 ; i < HBM_DMA_NUMBER_OF_CHNLS ; i++) {
		dma_id = gaudi_dma_assignment[GAUDI_HBM_DMA_1 + i];

		for (j = 0 ; j < QMAN_STREAMS ; j++) {
			 /*
			  * Add the CPU queue in order to get the correct queue
			  * number as all internal queue are placed after it
			  */
			internal_q_index = dma_id * QMAN_STREAMS + j + 1;

			q = &gaudi->internal_qmans[internal_q_index];
			qman_base_addr = (u64) q->pq_dma_addr;
			gaudi_init_hbm_dma_qman(hdev, dma_id, j,
						qman_base_addr);
		}

		/* Initializing lower CP for HBM DMA QMAN */
		gaudi_init_hbm_dma_qman(hdev, dma_id, 4, 0);

		gaudi_init_dma_core(hdev, dma_id);

		gaudi_enable_qman(hdev, dma_id, HBM_DMA_QMAN_ENABLE);
	}

	gaudi->hw_cap_initialized |= HW_CAP_HBM_DMA;
}

static void gaudi_init_mme_qman(struct hl_device *hdev, u32 mme_offset,
					int qman_id, u64 qman_base_addr)
{
	u32 mtr_base_lo, mtr_base_hi;
	u32 so_base_lo, so_base_hi;
	u32 q_off, mme_id;
	u32 mme_qm_err_cfg;

	mtr_base_lo = lower_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	mtr_base_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	so_base_lo = lower_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0);

	q_off = mme_offset + qman_id * 4;

	if (qman_id < 4) {
		WREG32(mmMME0_QM_PQ_BASE_LO_0 + q_off,
					lower_32_bits(qman_base_addr));
		WREG32(mmMME0_QM_PQ_BASE_HI_0 + q_off,
					upper_32_bits(qman_base_addr));

		WREG32(mmMME0_QM_PQ_SIZE_0 + q_off, ilog2(MME_QMAN_LENGTH));
		WREG32(mmMME0_QM_PQ_PI_0 + q_off, 0);
		WREG32(mmMME0_QM_PQ_CI_0 + q_off, 0);

		WREG32(mmMME0_QM_CP_LDMA_TSIZE_OFFSET_0 + q_off,
							QMAN_CPDMA_SIZE_OFFSET);
		WREG32(mmMME0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0 + q_off,
							QMAN_CPDMA_SRC_OFFSET);
		WREG32(mmMME0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0 + q_off,
							QMAN_CPDMA_DST_OFFSET);
	} else {
		WREG32(mmMME0_QM_CP_LDMA_TSIZE_OFFSET_0 + q_off,
							QMAN_LDMA_SIZE_OFFSET);
		WREG32(mmMME0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_SRC_OFFSET);
		WREG32(mmMME0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_DST_OFFSET);

		/* Configure RAZWI IRQ */
		mme_id = mme_offset /
				(mmMME1_QM_GLBL_CFG0 - mmMME0_QM_GLBL_CFG0);

		mme_qm_err_cfg = MME_QMAN_GLBL_ERR_CFG_MSG_EN_MASK;
		if (hdev->stop_on_err) {
			mme_qm_err_cfg |=
				MME_QMAN_GLBL_ERR_CFG_STOP_ON_ERR_EN_MASK;
		}
		WREG32(mmMME0_QM_GLBL_ERR_CFG + mme_offset, mme_qm_err_cfg);
		WREG32(mmMME0_QM_GLBL_ERR_ADDR_LO + mme_offset,
			lower_32_bits(CFG_BASE +
					mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR));
		WREG32(mmMME0_QM_GLBL_ERR_ADDR_HI + mme_offset,
			upper_32_bits(CFG_BASE +
					mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR));
		WREG32(mmMME0_QM_GLBL_ERR_WDATA + mme_offset,
			gaudi_irq_map_table[GAUDI_EVENT_MME0_QM].cpu_id +
									mme_id);

		WREG32(mmMME0_QM_ARB_ERR_MSG_EN + mme_offset,
				QM_ARB_ERR_MSG_EN_MASK);

		/* Increase ARB WDT to support streams architecture */
		WREG32(mmMME0_QM_ARB_SLV_CHOISE_WDT + mme_offset,
				GAUDI_ARB_WDT_TIMEOUT);

		WREG32(mmMME0_QM_GLBL_CFG1 + mme_offset, 0);
		WREG32(mmMME0_QM_GLBL_PROT + mme_offset,
				QMAN_INTERNAL_MAKE_TRUSTED);
	}

	WREG32(mmMME0_QM_CP_MSG_BASE0_ADDR_LO_0 + q_off, mtr_base_lo);
	WREG32(mmMME0_QM_CP_MSG_BASE0_ADDR_HI_0 + q_off, mtr_base_hi);
	WREG32(mmMME0_QM_CP_MSG_BASE1_ADDR_LO_0 + q_off, so_base_lo);
	WREG32(mmMME0_QM_CP_MSG_BASE1_ADDR_HI_0 + q_off, so_base_hi);
}

static void gaudi_init_mme_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_internal_qman_info *q;
	u64 qman_base_addr;
	u32 mme_offset;
	int i, internal_q_index;

	if (gaudi->hw_cap_initialized & HW_CAP_MME)
		return;

	/*
	 * map GAUDI_QUEUE_ID_MME_0_X to the N_W_MME (mmMME2_QM_BASE)
	 * and GAUDI_QUEUE_ID_MME_1_X to the S_W_MME (mmMME0_QM_BASE)
	 */

	mme_offset = mmMME2_QM_GLBL_CFG0 - mmMME0_QM_GLBL_CFG0;

	for (i = 0 ; i < MME_NUMBER_OF_QMANS ; i++) {
		internal_q_index = GAUDI_QUEUE_ID_MME_0_0 + i;
		q = &gaudi->internal_qmans[internal_q_index];
		qman_base_addr = (u64) q->pq_dma_addr;
		gaudi_init_mme_qman(hdev, mme_offset, (i & 0x3),
					qman_base_addr);
		if (i == 3)
			mme_offset = 0;
	}

	/* Initializing lower CP for MME QMANs */
	mme_offset = mmMME2_QM_GLBL_CFG0 - mmMME0_QM_GLBL_CFG0;
	gaudi_init_mme_qman(hdev, mme_offset, 4, 0);
	gaudi_init_mme_qman(hdev, 0, 4, 0);

	WREG32(mmMME2_QM_GLBL_CFG0, QMAN_MME_ENABLE);
	WREG32(mmMME0_QM_GLBL_CFG0, QMAN_MME_ENABLE);

	gaudi->hw_cap_initialized |= HW_CAP_MME;
}

static void gaudi_init_tpc_qman(struct hl_device *hdev, u32 tpc_offset,
				int qman_id, u64 qman_base_addr)
{
	u32 mtr_base_lo, mtr_base_hi;
	u32 so_base_lo, so_base_hi;
	u32 q_off, tpc_id;
	u32 tpc_qm_err_cfg;

	mtr_base_lo = lower_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	mtr_base_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	so_base_lo = lower_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0);

	q_off = tpc_offset + qman_id * 4;

	if (qman_id < 4) {
		WREG32(mmTPC0_QM_PQ_BASE_LO_0 + q_off,
					lower_32_bits(qman_base_addr));
		WREG32(mmTPC0_QM_PQ_BASE_HI_0 + q_off,
					upper_32_bits(qman_base_addr));

		WREG32(mmTPC0_QM_PQ_SIZE_0 + q_off, ilog2(TPC_QMAN_LENGTH));
		WREG32(mmTPC0_QM_PQ_PI_0 + q_off, 0);
		WREG32(mmTPC0_QM_PQ_CI_0 + q_off, 0);

		WREG32(mmTPC0_QM_CP_LDMA_TSIZE_OFFSET_0 + q_off,
							QMAN_CPDMA_SIZE_OFFSET);
		WREG32(mmTPC0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0 + q_off,
							QMAN_CPDMA_SRC_OFFSET);
		WREG32(mmTPC0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0 + q_off,
							QMAN_CPDMA_DST_OFFSET);
	} else {
		WREG32(mmTPC0_QM_CP_LDMA_TSIZE_OFFSET_0 + q_off,
							QMAN_LDMA_SIZE_OFFSET);
		WREG32(mmTPC0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_SRC_OFFSET);
		WREG32(mmTPC0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_DST_OFFSET);

		/* Configure RAZWI IRQ */
		tpc_id = tpc_offset /
				(mmTPC1_QM_GLBL_CFG0 - mmTPC0_QM_GLBL_CFG0);

		tpc_qm_err_cfg = TPC_QMAN_GLBL_ERR_CFG_MSG_EN_MASK;
		if (hdev->stop_on_err) {
			tpc_qm_err_cfg |=
				TPC_QMAN_GLBL_ERR_CFG_STOP_ON_ERR_EN_MASK;
		}

		WREG32(mmTPC0_QM_GLBL_ERR_CFG + tpc_offset, tpc_qm_err_cfg);
		WREG32(mmTPC0_QM_GLBL_ERR_ADDR_LO + tpc_offset,
			lower_32_bits(CFG_BASE +
				mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR));
		WREG32(mmTPC0_QM_GLBL_ERR_ADDR_HI + tpc_offset,
			upper_32_bits(CFG_BASE +
				mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR));
		WREG32(mmTPC0_QM_GLBL_ERR_WDATA + tpc_offset,
			gaudi_irq_map_table[GAUDI_EVENT_TPC0_QM].cpu_id +
									tpc_id);

		WREG32(mmTPC0_QM_ARB_ERR_MSG_EN + tpc_offset,
				QM_ARB_ERR_MSG_EN_MASK);

		/* Increase ARB WDT to support streams architecture */
		WREG32(mmTPC0_QM_ARB_SLV_CHOISE_WDT + tpc_offset,
				GAUDI_ARB_WDT_TIMEOUT);

		WREG32(mmTPC0_QM_GLBL_CFG1 + tpc_offset, 0);
		WREG32(mmTPC0_QM_GLBL_PROT + tpc_offset,
				QMAN_INTERNAL_MAKE_TRUSTED);
	}

	WREG32(mmTPC0_QM_CP_MSG_BASE0_ADDR_LO_0 + q_off, mtr_base_lo);
	WREG32(mmTPC0_QM_CP_MSG_BASE0_ADDR_HI_0 + q_off, mtr_base_hi);
	WREG32(mmTPC0_QM_CP_MSG_BASE1_ADDR_LO_0 + q_off, so_base_lo);
	WREG32(mmTPC0_QM_CP_MSG_BASE1_ADDR_HI_0 + q_off, so_base_hi);
}

static void gaudi_init_tpc_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_internal_qman_info *q;
	u64 qman_base_addr;
	u32 so_base_hi, tpc_offset = 0;
	u32 tpc_delta = mmTPC1_CFG_SM_BASE_ADDRESS_HIGH -
			mmTPC0_CFG_SM_BASE_ADDRESS_HIGH;
	int i, tpc_id, internal_q_index;

	if (gaudi->hw_cap_initialized & HW_CAP_TPC_MASK)
		return;

	so_base_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0);

	for (tpc_id = 0 ; tpc_id < TPC_NUMBER_OF_ENGINES ; tpc_id++) {
		for (i = 0 ; i < QMAN_STREAMS ; i++) {
			internal_q_index = GAUDI_QUEUE_ID_TPC_0_0 +
						tpc_id * QMAN_STREAMS + i;
			q = &gaudi->internal_qmans[internal_q_index];
			qman_base_addr = (u64) q->pq_dma_addr;
			gaudi_init_tpc_qman(hdev, tpc_offset, i,
						qman_base_addr);

			if (i == 3) {
				/* Initializing lower CP for TPC QMAN */
				gaudi_init_tpc_qman(hdev, tpc_offset, 4, 0);

				/* Enable the QMAN and TPC channel */
				WREG32(mmTPC0_QM_GLBL_CFG0 + tpc_offset,
						QMAN_TPC_ENABLE);
			}
		}

		WREG32(mmTPC0_CFG_SM_BASE_ADDRESS_HIGH + tpc_id * tpc_delta,
				so_base_hi);

		tpc_offset += mmTPC1_QM_GLBL_CFG0 - mmTPC0_QM_GLBL_CFG0;

		gaudi->hw_cap_initialized |=
				FIELD_PREP(HW_CAP_TPC_MASK, 1 << tpc_id);
	}
}

static void gaudi_disable_pci_dma_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_PCI_DMA))
		return;

	WREG32(mmDMA0_QM_GLBL_CFG0, 0);
	WREG32(mmDMA1_QM_GLBL_CFG0, 0);
	WREG32(mmDMA5_QM_GLBL_CFG0, 0);
}

static void gaudi_disable_hbm_dma_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_HBM_DMA))
		return;

	WREG32(mmDMA2_QM_GLBL_CFG0, 0);
	WREG32(mmDMA3_QM_GLBL_CFG0, 0);
	WREG32(mmDMA4_QM_GLBL_CFG0, 0);
	WREG32(mmDMA6_QM_GLBL_CFG0, 0);
	WREG32(mmDMA7_QM_GLBL_CFG0, 0);
}

static void gaudi_disable_mme_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MME))
		return;

	WREG32(mmMME2_QM_GLBL_CFG0, 0);
	WREG32(mmMME0_QM_GLBL_CFG0, 0);
}

static void gaudi_disable_tpc_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 tpc_offset = 0;
	int tpc_id;

	if (!(gaudi->hw_cap_initialized & HW_CAP_TPC_MASK))
		return;

	for (tpc_id = 0 ; tpc_id < TPC_NUMBER_OF_ENGINES ; tpc_id++) {
		WREG32(mmTPC0_QM_GLBL_CFG0 + tpc_offset, 0);
		tpc_offset += mmTPC1_QM_GLBL_CFG0 - mmTPC0_QM_GLBL_CFG0;
	}
}

static void gaudi_stop_pci_dma_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_PCI_DMA))
		return;

	/* Stop upper CPs of QMANs 0.0 to 1.3 and 5.0 to 5.3 */
	WREG32(mmDMA0_QM_GLBL_CFG1, 0xF << DMA0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmDMA1_QM_GLBL_CFG1, 0xF << DMA0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmDMA5_QM_GLBL_CFG1, 0xF << DMA0_QM_GLBL_CFG1_CP_STOP_SHIFT);
}

static void gaudi_stop_hbm_dma_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_HBM_DMA))
		return;

	/* Stop CPs of HBM DMA QMANs */

	WREG32(mmDMA2_QM_GLBL_CFG1, 0x1F << DMA0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmDMA3_QM_GLBL_CFG1, 0x1F << DMA0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmDMA4_QM_GLBL_CFG1, 0x1F << DMA0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmDMA6_QM_GLBL_CFG1, 0x1F << DMA0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmDMA7_QM_GLBL_CFG1, 0x1F << DMA0_QM_GLBL_CFG1_CP_STOP_SHIFT);
}

static void gaudi_stop_mme_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MME))
		return;

	/* Stop CPs of MME QMANs */
	WREG32(mmMME2_QM_GLBL_CFG1, 0x1F << MME0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmMME0_QM_GLBL_CFG1, 0x1F << MME0_QM_GLBL_CFG1_CP_STOP_SHIFT);
}

static void gaudi_stop_tpc_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_TPC_MASK))
		return;

	WREG32(mmTPC0_QM_GLBL_CFG1, 0x1F << TPC0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmTPC1_QM_GLBL_CFG1, 0x1F << TPC0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmTPC2_QM_GLBL_CFG1, 0x1F << TPC0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmTPC3_QM_GLBL_CFG1, 0x1F << TPC0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmTPC4_QM_GLBL_CFG1, 0x1F << TPC0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmTPC5_QM_GLBL_CFG1, 0x1F << TPC0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmTPC6_QM_GLBL_CFG1, 0x1F << TPC0_QM_GLBL_CFG1_CP_STOP_SHIFT);
	WREG32(mmTPC7_QM_GLBL_CFG1, 0x1F << TPC0_QM_GLBL_CFG1_CP_STOP_SHIFT);
}

static void gaudi_pci_dma_stall(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_PCI_DMA))
		return;

	WREG32(mmDMA0_CORE_CFG_1, 1 << DMA0_CORE_CFG_1_HALT_SHIFT);
	WREG32(mmDMA1_CORE_CFG_1, 1 << DMA0_CORE_CFG_1_HALT_SHIFT);
	WREG32(mmDMA5_CORE_CFG_1, 1 << DMA0_CORE_CFG_1_HALT_SHIFT);
}

static void gaudi_hbm_dma_stall(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_HBM_DMA))
		return;

	WREG32(mmDMA2_CORE_CFG_1, 1 << DMA0_CORE_CFG_1_HALT_SHIFT);
	WREG32(mmDMA3_CORE_CFG_1, 1 << DMA0_CORE_CFG_1_HALT_SHIFT);
	WREG32(mmDMA4_CORE_CFG_1, 1 << DMA0_CORE_CFG_1_HALT_SHIFT);
	WREG32(mmDMA6_CORE_CFG_1, 1 << DMA0_CORE_CFG_1_HALT_SHIFT);
	WREG32(mmDMA7_CORE_CFG_1, 1 << DMA0_CORE_CFG_1_HALT_SHIFT);
}

static void gaudi_mme_stall(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MME))
		return;

	/* WA for H3-1800 bug: do ACC and SBAB writes twice */
	WREG32(mmMME0_ACC_ACC_STALL, 1 << MME_ACC_ACC_STALL_R_SHIFT);
	WREG32(mmMME0_ACC_ACC_STALL, 1 << MME_ACC_ACC_STALL_R_SHIFT);
	WREG32(mmMME0_SBAB_SB_STALL, 1 << MME_SBAB_SB_STALL_R_SHIFT);
	WREG32(mmMME0_SBAB_SB_STALL, 1 << MME_SBAB_SB_STALL_R_SHIFT);
	WREG32(mmMME1_ACC_ACC_STALL, 1 << MME_ACC_ACC_STALL_R_SHIFT);
	WREG32(mmMME1_ACC_ACC_STALL, 1 << MME_ACC_ACC_STALL_R_SHIFT);
	WREG32(mmMME1_SBAB_SB_STALL, 1 << MME_SBAB_SB_STALL_R_SHIFT);
	WREG32(mmMME1_SBAB_SB_STALL, 1 << MME_SBAB_SB_STALL_R_SHIFT);
	WREG32(mmMME2_ACC_ACC_STALL, 1 << MME_ACC_ACC_STALL_R_SHIFT);
	WREG32(mmMME2_ACC_ACC_STALL, 1 << MME_ACC_ACC_STALL_R_SHIFT);
	WREG32(mmMME2_SBAB_SB_STALL, 1 << MME_SBAB_SB_STALL_R_SHIFT);
	WREG32(mmMME2_SBAB_SB_STALL, 1 << MME_SBAB_SB_STALL_R_SHIFT);
	WREG32(mmMME3_ACC_ACC_STALL, 1 << MME_ACC_ACC_STALL_R_SHIFT);
	WREG32(mmMME3_ACC_ACC_STALL, 1 << MME_ACC_ACC_STALL_R_SHIFT);
	WREG32(mmMME3_SBAB_SB_STALL, 1 << MME_SBAB_SB_STALL_R_SHIFT);
	WREG32(mmMME3_SBAB_SB_STALL, 1 << MME_SBAB_SB_STALL_R_SHIFT);
}

static void gaudi_tpc_stall(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_TPC_MASK))
		return;

	WREG32(mmTPC0_CFG_TPC_STALL, 1 << TPC0_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC1_CFG_TPC_STALL, 1 << TPC0_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC2_CFG_TPC_STALL, 1 << TPC0_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC3_CFG_TPC_STALL, 1 << TPC0_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC4_CFG_TPC_STALL, 1 << TPC0_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC5_CFG_TPC_STALL, 1 << TPC0_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC6_CFG_TPC_STALL, 1 << TPC0_CFG_TPC_STALL_V_SHIFT);
	WREG32(mmTPC7_CFG_TPC_STALL, 1 << TPC0_CFG_TPC_STALL_V_SHIFT);
}

static void gaudi_set_clock_gating(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 qman_offset;
	bool enable;
	int i;

	/* In case we are during debug session, don't enable the clock gate
	 * as it may interfere
	 */
	if (hdev->in_debug)
		return;

	for (i = GAUDI_PCI_DMA_1, qman_offset = 0 ; i < GAUDI_HBM_DMA_1 ; i++) {
		enable = !!(hdev->clock_gating_mask &
				(BIT_ULL(gaudi_dma_assignment[i])));

		qman_offset = gaudi_dma_assignment[i] * DMA_QMAN_OFFSET;
		WREG32(mmDMA0_QM_CGM_CFG1 + qman_offset,
				enable ? QMAN_CGM1_PWR_GATE_EN : 0);
		WREG32(mmDMA0_QM_CGM_CFG + qman_offset,
				enable ? QMAN_UPPER_CP_CGM_PWR_GATE_EN : 0);
	}

	for (i = GAUDI_HBM_DMA_1 ; i < GAUDI_DMA_MAX ; i++) {
		enable = !!(hdev->clock_gating_mask &
				(BIT_ULL(gaudi_dma_assignment[i])));

		qman_offset = gaudi_dma_assignment[i] * DMA_QMAN_OFFSET;
		WREG32(mmDMA0_QM_CGM_CFG1 + qman_offset,
				enable ? QMAN_CGM1_PWR_GATE_EN : 0);
		WREG32(mmDMA0_QM_CGM_CFG + qman_offset,
				enable ? QMAN_COMMON_CP_CGM_PWR_GATE_EN : 0);
	}

	enable = !!(hdev->clock_gating_mask & (BIT_ULL(GAUDI_ENGINE_ID_MME_0)));
	WREG32(mmMME0_QM_CGM_CFG1, enable ? QMAN_CGM1_PWR_GATE_EN : 0);
	WREG32(mmMME0_QM_CGM_CFG, enable ? QMAN_COMMON_CP_CGM_PWR_GATE_EN : 0);

	enable = !!(hdev->clock_gating_mask & (BIT_ULL(GAUDI_ENGINE_ID_MME_2)));
	WREG32(mmMME2_QM_CGM_CFG1, enable ? QMAN_CGM1_PWR_GATE_EN : 0);
	WREG32(mmMME2_QM_CGM_CFG, enable ? QMAN_COMMON_CP_CGM_PWR_GATE_EN : 0);

	for (i = 0, qman_offset = 0 ; i < TPC_NUMBER_OF_ENGINES ; i++) {
		enable = !!(hdev->clock_gating_mask &
				(BIT_ULL(GAUDI_ENGINE_ID_TPC_0 + i)));

		WREG32(mmTPC0_QM_CGM_CFG1 + qman_offset,
				enable ? QMAN_CGM1_PWR_GATE_EN : 0);
		WREG32(mmTPC0_QM_CGM_CFG + qman_offset,
				enable ? QMAN_COMMON_CP_CGM_PWR_GATE_EN : 0);

		qman_offset += TPC_QMAN_OFFSET;
	}

	gaudi->hw_cap_initialized |= HW_CAP_CLK_GATE;
}

static void gaudi_disable_clock_gating(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 qman_offset;
	int i;

	if (!(gaudi->hw_cap_initialized & HW_CAP_CLK_GATE))
		return;

	for (i = 0, qman_offset = 0 ; i < DMA_NUMBER_OF_CHANNELS ; i++) {
		WREG32(mmDMA0_QM_CGM_CFG + qman_offset, 0);
		WREG32(mmDMA0_QM_CGM_CFG1 + qman_offset, 0);

		qman_offset += (mmDMA1_QM_CGM_CFG - mmDMA0_QM_CGM_CFG);
	}

	WREG32(mmMME0_QM_CGM_CFG, 0);
	WREG32(mmMME0_QM_CGM_CFG1, 0);
	WREG32(mmMME2_QM_CGM_CFG, 0);
	WREG32(mmMME2_QM_CGM_CFG1, 0);

	for (i = 0, qman_offset = 0 ; i < TPC_NUMBER_OF_ENGINES ; i++) {
		WREG32(mmTPC0_QM_CGM_CFG + qman_offset, 0);
		WREG32(mmTPC0_QM_CGM_CFG1 + qman_offset, 0);

		qman_offset += (mmTPC1_QM_CGM_CFG - mmTPC0_QM_CGM_CFG);
	}

	gaudi->hw_cap_initialized &= ~(HW_CAP_CLK_GATE);
}

static void gaudi_enable_timestamp(struct hl_device *hdev)
{
	/* Disable the timestamp counter */
	WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE, 0);

	/* Zero the lower/upper parts of the 64-bit counter */
	WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE + 0xC, 0);
	WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE + 0x8, 0);

	/* Enable the counter */
	WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE, 1);
}

static void gaudi_disable_timestamp(struct hl_device *hdev)
{
	/* Disable the timestamp counter */
	WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE, 0);
}

static void gaudi_halt_engines(struct hl_device *hdev, bool hard_reset)
{
	u32 wait_timeout_ms;

	dev_info(hdev->dev,
		"Halting compute engines and disabling interrupts\n");

	if (hdev->pldm)
		wait_timeout_ms = GAUDI_PLDM_RESET_WAIT_MSEC;
	else
		wait_timeout_ms = GAUDI_RESET_WAIT_MSEC;


	gaudi_stop_mme_qmans(hdev);
	gaudi_stop_tpc_qmans(hdev);
	gaudi_stop_hbm_dma_qmans(hdev);
	gaudi_stop_pci_dma_qmans(hdev);

	hdev->asic_funcs->disable_clock_gating(hdev);

	msleep(wait_timeout_ms);

	gaudi_pci_dma_stall(hdev);
	gaudi_hbm_dma_stall(hdev);
	gaudi_tpc_stall(hdev);
	gaudi_mme_stall(hdev);

	msleep(wait_timeout_ms);

	gaudi_disable_mme_qmans(hdev);
	gaudi_disable_tpc_qmans(hdev);
	gaudi_disable_hbm_dma_qmans(hdev);
	gaudi_disable_pci_dma_qmans(hdev);

	gaudi_disable_timestamp(hdev);

	gaudi_disable_msi(hdev);
}

static int gaudi_mmu_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi_device *gaudi = hdev->asic_specific;
	u64 hop0_addr;
	int rc, i;

	if (!hdev->mmu_enable)
		return 0;

	if (gaudi->hw_cap_initialized & HW_CAP_MMU)
		return 0;

	hdev->dram_supports_virtual_memory = false;

	for (i = 0 ; i < prop->max_asid ; i++) {
		hop0_addr = prop->mmu_pgt_addr +
				(i * prop->mmu_hop_table_size);

		rc = gaudi_mmu_update_asid_hop0_addr(hdev, i, hop0_addr);
		if (rc) {
			dev_err(hdev->dev,
				"failed to set hop0 addr for asid %d\n", i);
			goto err;
		}
	}

	/* init MMU cache manage page */
	WREG32(mmSTLB_CACHE_INV_BASE_39_8, MMU_CACHE_MNG_ADDR >> 8);
	WREG32(mmSTLB_CACHE_INV_BASE_49_40, MMU_CACHE_MNG_ADDR >> 40);

	hdev->asic_funcs->mmu_invalidate_cache(hdev, true, 0);

	WREG32(mmMMU_UP_MMU_ENABLE, 1);
	WREG32(mmMMU_UP_SPI_MASK, 0xF);

	WREG32(mmSTLB_HOP_CONFIGURATION,
			hdev->mmu_huge_page_opt ? 0x30440 : 0x40440);

	/*
	 * The H/W expects the first PI after init to be 1. After wraparound
	 * we'll write 0.
	 */
	gaudi->mmu_cache_inv_pi = 1;

	gaudi->hw_cap_initialized |= HW_CAP_MMU;

	return 0;

err:
	return rc;
}

static int gaudi_load_firmware_to_device(struct hl_device *hdev)
{
	void __iomem *dst;

	/* HBM scrambler must be initialized before pushing F/W to HBM */
	gaudi_init_scrambler_hbm(hdev);

	dst = hdev->pcie_bar[HBM_BAR_ID] + LINUX_FW_OFFSET;

	return hl_fw_load_fw_to_device(hdev, GAUDI_LINUX_FW_FILE, dst);
}

static int gaudi_load_boot_fit_to_device(struct hl_device *hdev)
{
	void __iomem *dst;

	dst = hdev->pcie_bar[SRAM_BAR_ID] + BOOT_FIT_SRAM_OFFSET;

	return hl_fw_load_fw_to_device(hdev, GAUDI_BOOT_FIT_FILE, dst);
}

static void gaudi_read_device_fw_version(struct hl_device *hdev,
					enum hl_fw_component fwc)
{
	const char *name;
	u32 ver_off;
	char *dest;

	switch (fwc) {
	case FW_COMP_UBOOT:
		ver_off = RREG32(mmUBOOT_VER_OFFSET);
		dest = hdev->asic_prop.uboot_ver;
		name = "U-Boot";
		break;
	case FW_COMP_PREBOOT:
		ver_off = RREG32(mmPREBOOT_VER_OFFSET);
		dest = hdev->asic_prop.preboot_ver;
		name = "Preboot";
		break;
	default:
		dev_warn(hdev->dev, "Undefined FW component: %d\n", fwc);
		return;
	}

	ver_off &= ~((u32)SRAM_BASE_ADDR);

	if (ver_off < SRAM_SIZE - VERSION_MAX_LEN) {
		memcpy_fromio(dest, hdev->pcie_bar[SRAM_BAR_ID] + ver_off,
							VERSION_MAX_LEN);
	} else {
		dev_err(hdev->dev, "%s version offset (0x%x) is above SRAM\n",
								name, ver_off);
		strcpy(dest, "unavailable");
	}
}

static int gaudi_init_cpu(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int rc;

	if (!hdev->cpu_enable)
		return 0;

	if (gaudi->hw_cap_initialized & HW_CAP_CPU)
		return 0;

	/*
	 * The device CPU works with 40 bits addresses.
	 * This register sets the extension to 50 bits.
	 */
	WREG32(mmCPU_IF_CPU_MSB_ADDR, hdev->cpu_pci_msb_addr);

	rc = hl_fw_init_cpu(hdev, mmPSOC_GLOBAL_CONF_CPU_BOOT_STATUS,
			mmPSOC_GLOBAL_CONF_KMD_MSG_TO_CPU,
			mmCPU_CMD_STATUS_TO_HOST,
			mmCPU_BOOT_ERR0,
			!hdev->bmc_enable, GAUDI_CPU_TIMEOUT_USEC,
			GAUDI_BOOT_FIT_REQ_TIMEOUT_USEC);

	if (rc)
		return rc;

	gaudi->hw_cap_initialized |= HW_CAP_CPU;

	return 0;
}

static int gaudi_init_cpu_queues(struct hl_device *hdev, u32 cpu_timeout)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct hl_eq *eq;
	u32 status;
	struct hl_hw_queue *cpu_pq =
			&hdev->kernel_queues[GAUDI_QUEUE_ID_CPU_PQ];
	int err;

	if (!hdev->cpu_queues_enable)
		return 0;

	if (gaudi->hw_cap_initialized & HW_CAP_CPU_Q)
		return 0;

	eq = &hdev->event_queue;

	WREG32(mmCPU_IF_PQ_BASE_ADDR_LOW, lower_32_bits(cpu_pq->bus_address));
	WREG32(mmCPU_IF_PQ_BASE_ADDR_HIGH, upper_32_bits(cpu_pq->bus_address));

	WREG32(mmCPU_IF_EQ_BASE_ADDR_LOW, lower_32_bits(eq->bus_address));
	WREG32(mmCPU_IF_EQ_BASE_ADDR_HIGH, upper_32_bits(eq->bus_address));

	WREG32(mmCPU_IF_CQ_BASE_ADDR_LOW,
			lower_32_bits(hdev->cpu_accessible_dma_address));
	WREG32(mmCPU_IF_CQ_BASE_ADDR_HIGH,
			upper_32_bits(hdev->cpu_accessible_dma_address));

	WREG32(mmCPU_IF_PQ_LENGTH, HL_QUEUE_SIZE_IN_BYTES);
	WREG32(mmCPU_IF_EQ_LENGTH, HL_EQ_SIZE_IN_BYTES);
	WREG32(mmCPU_IF_CQ_LENGTH, HL_CPU_ACCESSIBLE_MEM_SIZE);

	/* Used for EQ CI */
	WREG32(mmCPU_IF_EQ_RD_OFFS, 0);

	WREG32(mmCPU_IF_PF_PQ_PI, 0);

	if (gaudi->multi_msi_mode)
		WREG32(mmCPU_IF_QUEUE_INIT, PQ_INIT_STATUS_READY_FOR_CP);
	else
		WREG32(mmCPU_IF_QUEUE_INIT,
			PQ_INIT_STATUS_READY_FOR_CP_SINGLE_MSI);

	WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR, GAUDI_EVENT_PI_UPDATE);

	err = hl_poll_timeout(
		hdev,
		mmCPU_IF_QUEUE_INIT,
		status,
		(status == PQ_INIT_STATUS_READY_FOR_HOST),
		1000,
		cpu_timeout);

	if (err) {
		dev_err(hdev->dev,
			"Failed to communicate with Device CPU (CPU-CP timeout)\n");
		return -EIO;
	}

	gaudi->hw_cap_initialized |= HW_CAP_CPU_Q;
	return 0;
}

static void gaudi_pre_hw_init(struct hl_device *hdev)
{
	/* Perform read from the device to make sure device is up */
	RREG32(mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG);

	/* Set the access through PCI bars (Linux driver only) as
	 * secured
	 */
	WREG32(mmPCIE_WRAP_LBW_PROT_OVR,
			(PCIE_WRAP_LBW_PROT_OVR_RD_EN_MASK |
			PCIE_WRAP_LBW_PROT_OVR_WR_EN_MASK));

	/* Perform read to flush the waiting writes to ensure
	 * configuration was set in the device
	 */
	RREG32(mmPCIE_WRAP_LBW_PROT_OVR);

	/*
	 * Let's mark in the H/W that we have reached this point. We check
	 * this value in the reset_before_init function to understand whether
	 * we need to reset the chip before doing H/W init. This register is
	 * cleared by the H/W upon H/W reset
	 */
	WREG32(mmHW_STATE, HL_DEVICE_HW_STATE_DIRTY);

	/* Configure the reset registers. Must be done as early as possible
	 * in case we fail during H/W initialization
	 */
	WREG32(mmPSOC_GLOBAL_CONF_SOFT_RST_CFG_H,
					(CFG_RST_H_DMA_MASK |
					CFG_RST_H_MME_MASK |
					CFG_RST_H_SM_MASK |
					CFG_RST_H_TPC_7_MASK));

	WREG32(mmPSOC_GLOBAL_CONF_SOFT_RST_CFG_L, CFG_RST_L_TPC_MASK);

	WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST_CFG_H,
					(CFG_RST_H_HBM_MASK |
					CFG_RST_H_TPC_7_MASK |
					CFG_RST_H_NIC_MASK |
					CFG_RST_H_SM_MASK |
					CFG_RST_H_DMA_MASK |
					CFG_RST_H_MME_MASK |
					CFG_RST_H_CPU_MASK |
					CFG_RST_H_MMU_MASK));

	WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST_CFG_L,
					(CFG_RST_L_IF_MASK |
					CFG_RST_L_PSOC_MASK |
					CFG_RST_L_TPC_MASK));
}

static int gaudi_hw_init(struct hl_device *hdev)
{
	int rc;

	dev_info(hdev->dev, "Starting initialization of H/W\n");

	gaudi_pre_hw_init(hdev);

	gaudi_init_pci_dma_qmans(hdev);

	gaudi_init_hbm_dma_qmans(hdev);

	rc = gaudi_init_cpu(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU\n");
		return rc;
	}

	/* SRAM scrambler must be initialized after CPU is running from HBM */
	gaudi_init_scrambler_sram(hdev);

	/* This is here just in case we are working without CPU */
	gaudi_init_scrambler_hbm(hdev);

	gaudi_init_golden_registers(hdev);

	rc = gaudi_mmu_init(hdev);
	if (rc)
		return rc;

	gaudi_init_security(hdev);

	gaudi_init_mme_qmans(hdev);

	gaudi_init_tpc_qmans(hdev);

	hdev->asic_funcs->set_clock_gating(hdev);

	gaudi_enable_timestamp(hdev);

	/* MSI must be enabled before CPU queues are initialized */
	rc = gaudi_enable_msi(hdev);
	if (rc)
		goto disable_queues;

	/* must be called after MSI was enabled */
	rc = gaudi_init_cpu_queues(hdev, GAUDI_CPU_TIMEOUT_USEC);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU H/W queues %d\n",
			rc);
		goto disable_msi;
	}

	/* Perform read from the device to flush all configuration */
	RREG32(mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG);

	return 0;

disable_msi:
	gaudi_disable_msi(hdev);
disable_queues:
	gaudi_disable_mme_qmans(hdev);
	gaudi_disable_pci_dma_qmans(hdev);

	return rc;
}

static void gaudi_hw_fini(struct hl_device *hdev, bool hard_reset)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 status, reset_timeout_ms, cpu_timeout_ms, boot_strap = 0;

	if (!hard_reset) {
		dev_err(hdev->dev, "GAUDI doesn't support soft-reset\n");
		return;
	}

	if (hdev->pldm) {
		reset_timeout_ms = GAUDI_PLDM_HRESET_TIMEOUT_MSEC;
		cpu_timeout_ms = GAUDI_PLDM_RESET_WAIT_MSEC;
	} else {
		reset_timeout_ms = GAUDI_RESET_TIMEOUT_MSEC;
		cpu_timeout_ms = GAUDI_CPU_RESET_WAIT_MSEC;
	}

	/* Set device to handle FLR by H/W as we will put the device CPU to
	 * halt mode
	 */
	WREG32(mmPCIE_AUX_FLR_CTRL, (PCIE_AUX_FLR_CTRL_HW_CTRL_MASK |
					PCIE_AUX_FLR_CTRL_INT_MASK_MASK));

	/* I don't know what is the state of the CPU so make sure it is
	 * stopped in any means necessary
	 */
	WREG32(mmPSOC_GLOBAL_CONF_KMD_MSG_TO_CPU, KMD_MSG_GOTO_WFE);
	WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR, GAUDI_EVENT_HALT_MACHINE);

	msleep(cpu_timeout_ms);

	/* Tell ASIC not to re-initialize PCIe */
	WREG32(mmPREBOOT_PCIE_EN, LKD_HARD_RESET_MAGIC);

	boot_strap = RREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS);

	/* H/W bug WA:
	 * rdata[31:0] = strap_read_val;
	 * wdata[31:0] = rdata[30:21],1'b0,rdata[20:0]
	 */
	boot_strap = (((boot_strap & 0x7FE00000) << 1) |
			(boot_strap & 0x001FFFFF));
	WREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS, boot_strap & ~0x2);

	/* Restart BTL/BLR upon hard-reset */
	WREG32(mmPSOC_GLOBAL_CONF_BOOT_SEQ_RE_START, 1);

	WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST,
			1 << PSOC_GLOBAL_CONF_SW_ALL_RST_IND_SHIFT);
	dev_info(hdev->dev,
		"Issued HARD reset command, going to wait %dms\n",
		reset_timeout_ms);

	/*
	 * After hard reset, we can't poll the BTM_FSM register because the PSOC
	 * itself is in reset. Need to wait until the reset is deasserted
	 */
	msleep(reset_timeout_ms);

	status = RREG32(mmPSOC_GLOBAL_CONF_BTM_FSM);
	if (status & PSOC_GLOBAL_CONF_BTM_FSM_STATE_MASK)
		dev_err(hdev->dev,
			"Timeout while waiting for device to reset 0x%x\n",
			status);

	WREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS, boot_strap);

	gaudi->hw_cap_initialized &= ~(HW_CAP_CPU | HW_CAP_CPU_Q |
					HW_CAP_HBM | HW_CAP_PCI_DMA |
					HW_CAP_MME | HW_CAP_TPC_MASK |
					HW_CAP_HBM_DMA | HW_CAP_PLL |
					HW_CAP_MMU |
					HW_CAP_SRAM_SCRAMBLER |
					HW_CAP_HBM_SCRAMBLER |
					HW_CAP_CLK_GATE);

	memset(gaudi->events_stat, 0, sizeof(gaudi->events_stat));
}

static int gaudi_suspend(struct hl_device *hdev)
{
	int rc;

	rc = hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_DISABLE_PCI_ACCESS);
	if (rc)
		dev_err(hdev->dev, "Failed to disable PCI access from CPU\n");

	return rc;
}

static int gaudi_resume(struct hl_device *hdev)
{
	return gaudi_init_iatu(hdev);
}

static int gaudi_cb_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
			void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	int rc;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP |
			VM_DONTCOPY | VM_NORESERVE;

	rc = dma_mmap_coherent(hdev->dev, vma, cpu_addr, dma_addr, size);
	if (rc)
		dev_err(hdev->dev, "dma_mmap_coherent error %d", rc);

	return rc;
}

static void gaudi_ring_doorbell(struct hl_device *hdev, u32 hw_queue_id, u32 pi)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 db_reg_offset, db_value, dma_qm_offset, q_off;
	int dma_id;
	bool invalid_queue = false;

	switch (hw_queue_id) {
	case GAUDI_QUEUE_ID_DMA_0_0...GAUDI_QUEUE_ID_DMA_0_3:
		dma_id = gaudi_dma_assignment[GAUDI_PCI_DMA_1];
		dma_qm_offset = dma_id * DMA_QMAN_OFFSET;
		q_off = dma_qm_offset + (hw_queue_id & 0x3) * 4;
		db_reg_offset = mmDMA0_QM_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_DMA_1_0...GAUDI_QUEUE_ID_DMA_1_3:
		dma_id = gaudi_dma_assignment[GAUDI_PCI_DMA_2];
		dma_qm_offset = dma_id * DMA_QMAN_OFFSET;
		q_off = dma_qm_offset + (hw_queue_id & 0x3) * 4;
		db_reg_offset = mmDMA0_QM_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_DMA_2_0...GAUDI_QUEUE_ID_DMA_2_3:
		dma_id = gaudi_dma_assignment[GAUDI_HBM_DMA_1];
		dma_qm_offset = dma_id * DMA_QMAN_OFFSET;
		q_off = dma_qm_offset + ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmDMA0_QM_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_DMA_3_0...GAUDI_QUEUE_ID_DMA_3_3:
		dma_id = gaudi_dma_assignment[GAUDI_HBM_DMA_2];
		dma_qm_offset = dma_id * DMA_QMAN_OFFSET;
		q_off = dma_qm_offset + ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmDMA0_QM_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_DMA_4_0...GAUDI_QUEUE_ID_DMA_4_3:
		dma_id = gaudi_dma_assignment[GAUDI_HBM_DMA_3];
		dma_qm_offset = dma_id * DMA_QMAN_OFFSET;
		q_off = dma_qm_offset + ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmDMA0_QM_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_DMA_5_0...GAUDI_QUEUE_ID_DMA_5_3:
		dma_id = gaudi_dma_assignment[GAUDI_PCI_DMA_3];
		dma_qm_offset = dma_id * DMA_QMAN_OFFSET;
		q_off = dma_qm_offset + ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmDMA0_QM_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_DMA_6_0...GAUDI_QUEUE_ID_DMA_6_3:
		dma_id = gaudi_dma_assignment[GAUDI_HBM_DMA_4];
		dma_qm_offset = dma_id * DMA_QMAN_OFFSET;
		q_off = dma_qm_offset + ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmDMA0_QM_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_DMA_7_0...GAUDI_QUEUE_ID_DMA_7_3:
		dma_id = gaudi_dma_assignment[GAUDI_HBM_DMA_5];
		dma_qm_offset = dma_id * DMA_QMAN_OFFSET;
		q_off = dma_qm_offset + ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmDMA0_QM_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_CPU_PQ:
		if (gaudi->hw_cap_initialized & HW_CAP_CPU_Q)
			db_reg_offset = mmCPU_IF_PF_PQ_PI;
		else
			invalid_queue = true;
		break;

	case GAUDI_QUEUE_ID_MME_0_0:
		db_reg_offset = mmMME2_QM_PQ_PI_0;
		break;

	case GAUDI_QUEUE_ID_MME_0_1:
		db_reg_offset = mmMME2_QM_PQ_PI_1;
		break;

	case GAUDI_QUEUE_ID_MME_0_2:
		db_reg_offset = mmMME2_QM_PQ_PI_2;
		break;

	case GAUDI_QUEUE_ID_MME_0_3:
		db_reg_offset = mmMME2_QM_PQ_PI_3;
		break;

	case GAUDI_QUEUE_ID_MME_1_0:
		db_reg_offset = mmMME0_QM_PQ_PI_0;
		break;

	case GAUDI_QUEUE_ID_MME_1_1:
		db_reg_offset = mmMME0_QM_PQ_PI_1;
		break;

	case GAUDI_QUEUE_ID_MME_1_2:
		db_reg_offset = mmMME0_QM_PQ_PI_2;
		break;

	case GAUDI_QUEUE_ID_MME_1_3:
		db_reg_offset = mmMME0_QM_PQ_PI_3;
		break;

	case GAUDI_QUEUE_ID_TPC_0_0:
		db_reg_offset = mmTPC0_QM_PQ_PI_0;
		break;

	case GAUDI_QUEUE_ID_TPC_0_1:
		db_reg_offset = mmTPC0_QM_PQ_PI_1;
		break;

	case GAUDI_QUEUE_ID_TPC_0_2:
		db_reg_offset = mmTPC0_QM_PQ_PI_2;
		break;

	case GAUDI_QUEUE_ID_TPC_0_3:
		db_reg_offset = mmTPC0_QM_PQ_PI_3;
		break;

	case GAUDI_QUEUE_ID_TPC_1_0:
		db_reg_offset = mmTPC1_QM_PQ_PI_0;
		break;

	case GAUDI_QUEUE_ID_TPC_1_1:
		db_reg_offset = mmTPC1_QM_PQ_PI_1;
		break;

	case GAUDI_QUEUE_ID_TPC_1_2:
		db_reg_offset = mmTPC1_QM_PQ_PI_2;
		break;

	case GAUDI_QUEUE_ID_TPC_1_3:
		db_reg_offset = mmTPC1_QM_PQ_PI_3;
		break;

	case GAUDI_QUEUE_ID_TPC_2_0:
		db_reg_offset = mmTPC2_QM_PQ_PI_0;
		break;

	case GAUDI_QUEUE_ID_TPC_2_1:
		db_reg_offset = mmTPC2_QM_PQ_PI_1;
		break;

	case GAUDI_QUEUE_ID_TPC_2_2:
		db_reg_offset = mmTPC2_QM_PQ_PI_2;
		break;

	case GAUDI_QUEUE_ID_TPC_2_3:
		db_reg_offset = mmTPC2_QM_PQ_PI_3;
		break;

	case GAUDI_QUEUE_ID_TPC_3_0:
		db_reg_offset = mmTPC3_QM_PQ_PI_0;
		break;

	case GAUDI_QUEUE_ID_TPC_3_1:
		db_reg_offset = mmTPC3_QM_PQ_PI_1;
		break;

	case GAUDI_QUEUE_ID_TPC_3_2:
		db_reg_offset = mmTPC3_QM_PQ_PI_2;
		break;

	case GAUDI_QUEUE_ID_TPC_3_3:
		db_reg_offset = mmTPC3_QM_PQ_PI_3;
		break;

	case GAUDI_QUEUE_ID_TPC_4_0:
		db_reg_offset = mmTPC4_QM_PQ_PI_0;
		break;

	case GAUDI_QUEUE_ID_TPC_4_1:
		db_reg_offset = mmTPC4_QM_PQ_PI_1;
		break;

	case GAUDI_QUEUE_ID_TPC_4_2:
		db_reg_offset = mmTPC4_QM_PQ_PI_2;
		break;

	case GAUDI_QUEUE_ID_TPC_4_3:
		db_reg_offset = mmTPC4_QM_PQ_PI_3;
		break;

	case GAUDI_QUEUE_ID_TPC_5_0:
		db_reg_offset = mmTPC5_QM_PQ_PI_0;
		break;

	case GAUDI_QUEUE_ID_TPC_5_1:
		db_reg_offset = mmTPC5_QM_PQ_PI_1;
		break;

	case GAUDI_QUEUE_ID_TPC_5_2:
		db_reg_offset = mmTPC5_QM_PQ_PI_2;
		break;

	case GAUDI_QUEUE_ID_TPC_5_3:
		db_reg_offset = mmTPC5_QM_PQ_PI_3;
		break;

	case GAUDI_QUEUE_ID_TPC_6_0:
		db_reg_offset = mmTPC6_QM_PQ_PI_0;
		break;

	case GAUDI_QUEUE_ID_TPC_6_1:
		db_reg_offset = mmTPC6_QM_PQ_PI_1;
		break;

	case GAUDI_QUEUE_ID_TPC_6_2:
		db_reg_offset = mmTPC6_QM_PQ_PI_2;
		break;

	case GAUDI_QUEUE_ID_TPC_6_3:
		db_reg_offset = mmTPC6_QM_PQ_PI_3;
		break;

	case GAUDI_QUEUE_ID_TPC_7_0:
		db_reg_offset = mmTPC7_QM_PQ_PI_0;
		break;

	case GAUDI_QUEUE_ID_TPC_7_1:
		db_reg_offset = mmTPC7_QM_PQ_PI_1;
		break;

	case GAUDI_QUEUE_ID_TPC_7_2:
		db_reg_offset = mmTPC7_QM_PQ_PI_2;
		break;

	case GAUDI_QUEUE_ID_TPC_7_3:
		db_reg_offset = mmTPC7_QM_PQ_PI_3;
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

	if (hw_queue_id == GAUDI_QUEUE_ID_CPU_PQ)
		WREG32(mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR,
				GAUDI_EVENT_PI_UPDATE);
}

static void gaudi_pqe_write(struct hl_device *hdev, __le64 *pqe,
				struct hl_bd *bd)
{
	__le64 *pbd = (__le64 *) bd;

	/* The QMANs are on the host memory so a simple copy suffice */
	pqe[0] = pbd[0];
	pqe[1] = pbd[1];
}

static void *gaudi_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	void *kernel_addr = dma_alloc_coherent(&hdev->pdev->dev, size,
						dma_handle, flags);

	/* Shift to the device's base physical address of host memory */
	if (kernel_addr)
		*dma_handle += HOST_PHYS_BASE;

	return kernel_addr;
}

static void gaudi_dma_free_coherent(struct hl_device *hdev, size_t size,
		void *cpu_addr, dma_addr_t dma_handle)
{
	/* Cancel the device's base physical address of host memory */
	dma_addr_t fixed_dma_handle = dma_handle - HOST_PHYS_BASE;

	dma_free_coherent(&hdev->pdev->dev, size, cpu_addr, fixed_dma_handle);
}

static void *gaudi_get_int_queue_base(struct hl_device *hdev,
				u32 queue_id, dma_addr_t *dma_handle,
				u16 *queue_len)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_internal_qman_info *q;

	if (queue_id >= GAUDI_QUEUE_ID_SIZE ||
			gaudi_queue_type[queue_id] != QUEUE_TYPE_INT) {
		dev_err(hdev->dev, "Got invalid queue id %d\n", queue_id);
		return NULL;
	}

	q = &gaudi->internal_qmans[queue_id];
	*dma_handle = q->pq_dma_addr;
	*queue_len = q->pq_size / QMAN_PQ_ENTRY_SIZE;

	return q->pq_kernel_addr;
}

static int gaudi_send_cpu_message(struct hl_device *hdev, u32 *msg,
				u16 len, u32 timeout, long *result)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_CPU_Q)) {
		if (result)
			*result = 0;
		return 0;
	}

	if (!timeout)
		timeout = GAUDI_MSG_TO_CPU_TIMEOUT_USEC;

	return hl_fw_send_cpu_message(hdev, GAUDI_QUEUE_ID_CPU_PQ, msg, len,
						timeout, result);
}

static int gaudi_test_queue(struct hl_device *hdev, u32 hw_queue_id)
{
	struct packet_msg_prot *fence_pkt;
	dma_addr_t pkt_dma_addr;
	u32 fence_val, tmp, timeout_usec;
	dma_addr_t fence_dma_addr;
	u32 *fence_ptr;
	int rc;

	if (hdev->pldm)
		timeout_usec = GAUDI_PLDM_TEST_QUEUE_WAIT_USEC;
	else
		timeout_usec = GAUDI_TEST_QUEUE_WAIT_USEC;

	fence_val = GAUDI_QMAN0_FENCE_VAL;

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

	tmp = FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_MSG_PROT);
	tmp |= FIELD_PREP(GAUDI_PKT_CTL_EB_MASK, 1);
	tmp |= FIELD_PREP(GAUDI_PKT_CTL_MB_MASK, 1);

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
					1000, timeout_usec, true);

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

static int gaudi_test_cpu_queue(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	/*
	 * check capability here as send_cpu_message() won't update the result
	 * value if no capability
	 */
	if (!(gaudi->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	return hl_fw_test_cpu_queue(hdev);
}

static int gaudi_test_queues(struct hl_device *hdev)
{
	int i, rc, ret_val = 0;

	for (i = 0 ; i < hdev->asic_prop.max_queues ; i++) {
		if (hdev->asic_prop.hw_queues_props[i].type == QUEUE_TYPE_EXT) {
			rc = gaudi_test_queue(hdev, i);
			if (rc)
				ret_val = -EINVAL;
		}
	}

	rc = gaudi_test_cpu_queue(hdev);
	if (rc)
		ret_val = -EINVAL;

	return ret_val;
}

static void *gaudi_dma_pool_zalloc(struct hl_device *hdev, size_t size,
		gfp_t mem_flags, dma_addr_t *dma_handle)
{
	void *kernel_addr;

	if (size > GAUDI_DMA_POOL_BLK_SIZE)
		return NULL;

	kernel_addr = dma_pool_zalloc(hdev->dma_pool, mem_flags, dma_handle);

	/* Shift to the device's base physical address of host memory */
	if (kernel_addr)
		*dma_handle += HOST_PHYS_BASE;

	return kernel_addr;
}

static void gaudi_dma_pool_free(struct hl_device *hdev, void *vaddr,
			dma_addr_t dma_addr)
{
	/* Cancel the device's base physical address of host memory */
	dma_addr_t fixed_dma_addr = dma_addr - HOST_PHYS_BASE;

	dma_pool_free(hdev->dma_pool, vaddr, fixed_dma_addr);
}

static void *gaudi_cpu_accessible_dma_pool_alloc(struct hl_device *hdev,
					size_t size, dma_addr_t *dma_handle)
{
	return hl_fw_cpu_accessible_dma_pool_alloc(hdev, size, dma_handle);
}

static void gaudi_cpu_accessible_dma_pool_free(struct hl_device *hdev,
						size_t size, void *vaddr)
{
	hl_fw_cpu_accessible_dma_pool_free(hdev, size, vaddr);
}

static int gaudi_dma_map_sg(struct hl_device *hdev, struct scatterlist *sgl,
			int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	if (!dma_map_sg(&hdev->pdev->dev, sgl, nents, dir))
		return -ENOMEM;

	/* Shift to the device's base physical address of host memory */
	for_each_sg(sgl, sg, nents, i)
		sg->dma_address += HOST_PHYS_BASE;

	return 0;
}

static void gaudi_dma_unmap_sg(struct hl_device *hdev, struct scatterlist *sgl,
			int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	/* Cancel the device's base physical address of host memory */
	for_each_sg(sgl, sg, nents, i)
		sg->dma_address -= HOST_PHYS_BASE;

	dma_unmap_sg(&hdev->pdev->dev, sgl, nents, dir);
}

static u32 gaudi_get_dma_desc_list_size(struct hl_device *hdev,
					struct sg_table *sgt)
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

static int gaudi_pin_memory_before_cs(struct hl_device *hdev,
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
			gaudi_get_dma_desc_list_size(hdev, userptr->sgt);

	return 0;

unpin_memory:
	hl_unpin_host_memory(hdev, userptr);
free_userptr:
	kfree(userptr);
	return rc;
}

static int gaudi_validate_dma_pkt_host(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt,
				bool src_in_host)
{
	enum dma_data_direction dir;
	bool skip_host_mem_pin = false, user_memset;
	u64 addr;
	int rc = 0;

	user_memset = (le32_to_cpu(user_dma_pkt->ctl) &
			GAUDI_PKT_LIN_DMA_CTL_MEMSET_MASK) >>
			GAUDI_PKT_LIN_DMA_CTL_MEMSET_SHIFT;

	if (src_in_host) {
		if (user_memset)
			skip_host_mem_pin = true;

		dev_dbg(hdev->dev, "DMA direction is HOST --> DEVICE\n");
		dir = DMA_TO_DEVICE;
		addr = le64_to_cpu(user_dma_pkt->src_addr);
	} else {
		dev_dbg(hdev->dev, "DMA direction is DEVICE --> HOST\n");
		dir = DMA_FROM_DEVICE;
		addr = (le64_to_cpu(user_dma_pkt->dst_addr) &
				GAUDI_PKT_LIN_DMA_DST_ADDR_MASK) >>
				GAUDI_PKT_LIN_DMA_DST_ADDR_SHIFT;
	}

	if (skip_host_mem_pin)
		parser->patched_cb_size += sizeof(*user_dma_pkt);
	else
		rc = gaudi_pin_memory_before_cs(hdev, parser, user_dma_pkt,
						addr, dir);

	return rc;
}

static int gaudi_validate_dma_pkt_no_mmu(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt)
{
	bool src_in_host = false;
	u64 dst_addr = (le64_to_cpu(user_dma_pkt->dst_addr) &
			GAUDI_PKT_LIN_DMA_DST_ADDR_MASK) >>
			GAUDI_PKT_LIN_DMA_DST_ADDR_SHIFT;

	dev_dbg(hdev->dev, "DMA packet details:\n");
	dev_dbg(hdev->dev, "source == 0x%llx\n",
				le64_to_cpu(user_dma_pkt->src_addr));
	dev_dbg(hdev->dev, "destination == 0x%llx\n", dst_addr);
	dev_dbg(hdev->dev, "size == %u\n", le32_to_cpu(user_dma_pkt->tsize));

	/*
	 * Special handling for DMA with size 0. Bypass all validations
	 * because no transactions will be done except for WR_COMP, which
	 * is not a security issue
	 */
	if (!le32_to_cpu(user_dma_pkt->tsize)) {
		parser->patched_cb_size += sizeof(*user_dma_pkt);
		return 0;
	}

	if (parser->hw_queue_id <= GAUDI_QUEUE_ID_DMA_0_3)
		src_in_host = true;

	return gaudi_validate_dma_pkt_host(hdev, parser, user_dma_pkt,
						src_in_host);
}

static int gaudi_validate_load_and_exe_pkt(struct hl_device *hdev,
					struct hl_cs_parser *parser,
					struct packet_load_and_exe *user_pkt)
{
	u32 cfg;

	cfg = le32_to_cpu(user_pkt->cfg);

	if (cfg & GAUDI_PKT_LOAD_AND_EXE_CFG_DST_MASK) {
		dev_err(hdev->dev,
			"User not allowed to use Load and Execute\n");
		return -EPERM;
	}

	parser->patched_cb_size += sizeof(struct packet_load_and_exe);

	return 0;
}

static int gaudi_validate_cb(struct hl_device *hdev,
			struct hl_cs_parser *parser, bool is_mmu)
{
	u32 cb_parsed_length = 0;
	int rc = 0;

	parser->patched_cb_size = 0;

	/* cb_user_size is more than 0 so loop will always be executed */
	while (cb_parsed_length < parser->user_cb_size) {
		enum packet_id pkt_id;
		u16 pkt_size;
		struct gaudi_packet *user_pkt;

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

		pkt_size = gaudi_packet_sizes[pkt_id];
		cb_parsed_length += pkt_size;
		if (cb_parsed_length > parser->user_cb_size) {
			dev_err(hdev->dev,
				"packet 0x%x is out of CB boundary\n", pkt_id);
			rc = -EINVAL;
			break;
		}

		switch (pkt_id) {
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

		case PACKET_WREG_BULK:
			dev_err(hdev->dev,
				"User not allowed to use WREG_BULK\n");
			rc = -EPERM;
			break;

		case PACKET_LOAD_AND_EXE:
			rc = gaudi_validate_load_and_exe_pkt(hdev, parser,
				(struct packet_load_and_exe *) user_pkt);
			break;

		case PACKET_LIN_DMA:
			parser->contains_dma_pkt = true;
			if (is_mmu)
				parser->patched_cb_size += pkt_size;
			else
				rc = gaudi_validate_dma_pkt_no_mmu(hdev, parser,
					(struct packet_lin_dma *) user_pkt);
			break;

		case PACKET_WREG_32:
		case PACKET_MSG_LONG:
		case PACKET_MSG_SHORT:
		case PACKET_REPEAT:
		case PACKET_FENCE:
		case PACKET_NOP:
		case PACKET_ARB_POINT:
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

static int gaudi_patch_dma_packet(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt,
				struct packet_lin_dma *new_dma_pkt,
				u32 *new_dma_pkt_size)
{
	struct hl_userptr *userptr;
	struct scatterlist *sg, *sg_next_iter;
	u32 count, dma_desc_cnt, user_wrcomp_en_mask, ctl;
	u64 len, len_next;
	dma_addr_t dma_addr, dma_addr_next;
	u64 device_memory_addr, addr;
	enum dma_data_direction dir;
	struct sg_table *sgt;
	bool src_in_host = false;
	bool skip_host_mem_pin = false;
	bool user_memset;

	ctl = le32_to_cpu(user_dma_pkt->ctl);

	if (parser->hw_queue_id <= GAUDI_QUEUE_ID_DMA_0_3)
		src_in_host = true;

	user_memset = (ctl & GAUDI_PKT_LIN_DMA_CTL_MEMSET_MASK) >>
			GAUDI_PKT_LIN_DMA_CTL_MEMSET_SHIFT;

	if (src_in_host) {
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
		(!hl_userptr_is_pinned(hdev, addr,
					le32_to_cpu(user_dma_pkt->tsize),
					parser->job_userptr_list, &userptr))) {
		dev_err(hdev->dev, "Userptr 0x%llx + 0x%x NOT mapped\n",
				addr, user_dma_pkt->tsize);
		return -EFAULT;
	}

	if ((user_memset) && (dir == DMA_TO_DEVICE)) {
		memcpy(new_dma_pkt, user_dma_pkt, sizeof(*user_dma_pkt));
		*new_dma_pkt_size = sizeof(*user_dma_pkt);
		return 0;
	}

	user_wrcomp_en_mask = ctl & GAUDI_PKT_LIN_DMA_CTL_WRCOMP_EN_MASK;

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
			ctl &= ~GAUDI_PKT_CTL_EB_MASK;
		ctl &= ~GAUDI_PKT_LIN_DMA_CTL_WRCOMP_EN_MASK;
		new_dma_pkt->ctl = cpu_to_le32(ctl);
		new_dma_pkt->tsize = cpu_to_le32(len);

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

	/* Fix the last dma packet - wrcomp must be as user set it */
	new_dma_pkt--;
	new_dma_pkt->ctl |= cpu_to_le32(user_wrcomp_en_mask);

	*new_dma_pkt_size = dma_desc_cnt * sizeof(struct packet_lin_dma);

	return 0;
}

static int gaudi_patch_cb(struct hl_device *hdev,
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
		struct gaudi_packet *user_pkt, *kernel_pkt;

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

		pkt_size = gaudi_packet_sizes[pkt_id];
		cb_parsed_length += pkt_size;
		if (cb_parsed_length > parser->user_cb_size) {
			dev_err(hdev->dev,
				"packet 0x%x is out of CB boundary\n", pkt_id);
			rc = -EINVAL;
			break;
		}

		switch (pkt_id) {
		case PACKET_LIN_DMA:
			rc = gaudi_patch_dma_packet(hdev, parser,
					(struct packet_lin_dma *) user_pkt,
					(struct packet_lin_dma *) kernel_pkt,
					&new_pkt_size);
			cb_patched_cur_length += new_pkt_size;
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

		case PACKET_WREG_32:
		case PACKET_WREG_BULK:
		case PACKET_MSG_LONG:
		case PACKET_MSG_SHORT:
		case PACKET_REPEAT:
		case PACKET_FENCE:
		case PACKET_NOP:
		case PACKET_ARB_POINT:
		case PACKET_LOAD_AND_EXE:
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

static int gaudi_parse_cb_mmu(struct hl_device *hdev,
		struct hl_cs_parser *parser)
{
	u64 patched_cb_handle;
	u32 patched_cb_size;
	struct hl_cb *user_cb;
	int rc;

	/*
	 * The new CB should have space at the end for two MSG_PROT pkt:
	 * 1. A packet that will act as a completion packet
	 * 2. A packet that will generate MSI interrupt
	 */
	parser->patched_cb_size = parser->user_cb_size +
			sizeof(struct packet_msg_prot) * 2;

	rc = hl_cb_create(hdev, &hdev->kernel_cb_mgr, hdev->kernel_ctx,
				parser->patched_cb_size, false, false,
				&patched_cb_handle);

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
	memcpy(parser->patched_cb->kernel_address,
		parser->user_cb->kernel_address,
		parser->user_cb_size);

	patched_cb_size = parser->patched_cb_size;

	/* Validate patched CB instead of user CB */
	user_cb = parser->user_cb;
	parser->user_cb = parser->patched_cb;
	rc = gaudi_validate_cb(hdev, parser, true);
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

static int gaudi_parse_cb_no_mmu(struct hl_device *hdev,
		struct hl_cs_parser *parser)
{
	u64 patched_cb_handle;
	int rc;

	rc = gaudi_validate_cb(hdev, parser, false);

	if (rc)
		goto free_userptr;

	rc = hl_cb_create(hdev, &hdev->kernel_cb_mgr, hdev->kernel_ctx,
				parser->patched_cb_size, false, false,
				&patched_cb_handle);
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

	rc = gaudi_patch_cb(hdev, parser);

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

static int gaudi_parse_cb_no_ext_queue(struct hl_device *hdev,
					struct hl_cs_parser *parser)
{
	struct asic_fixed_properties *asic_prop = &hdev->asic_prop;

	/* For internal queue jobs just check if CB address is valid */
	if (hl_mem_area_inside_range((u64) (uintptr_t) parser->user_cb,
					parser->user_cb_size,
					asic_prop->sram_user_base_address,
					asic_prop->sram_end_address))
		return 0;

	if (hl_mem_area_inside_range((u64) (uintptr_t) parser->user_cb,
					parser->user_cb_size,
					asic_prop->dram_user_base_address,
					asic_prop->dram_end_address))
		return 0;

	/* PMMU and HPMMU addresses are equal, check only one of them */
	if (hl_mem_area_inside_range((u64) (uintptr_t) parser->user_cb,
					parser->user_cb_size,
					asic_prop->pmmu.start_addr,
					asic_prop->pmmu.end_addr))
		return 0;

	dev_err(hdev->dev,
		"CB address 0x%px + 0x%x for internal QMAN is not valid\n",
		parser->user_cb, parser->user_cb_size);

	return -EFAULT;
}

static int gaudi_cs_parser(struct hl_device *hdev, struct hl_cs_parser *parser)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (parser->queue_type == QUEUE_TYPE_INT)
		return gaudi_parse_cb_no_ext_queue(hdev, parser);

	if (gaudi->hw_cap_initialized & HW_CAP_MMU)
		return gaudi_parse_cb_mmu(hdev, parser);
	else
		return gaudi_parse_cb_no_mmu(hdev, parser);
}

static void gaudi_add_end_of_cb_packets(struct hl_device *hdev,
					void *kernel_address, u32 len,
					u64 cq_addr, u32 cq_val, u32 msi_vec,
					bool eb)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct packet_msg_prot *cq_pkt;
	u32 tmp;

	cq_pkt = kernel_address + len - (sizeof(struct packet_msg_prot) * 2);

	tmp = FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_MSG_PROT);
	tmp |= FIELD_PREP(GAUDI_PKT_CTL_MB_MASK, 1);

	if (eb)
		tmp |= FIELD_PREP(GAUDI_PKT_CTL_EB_MASK, 1);

	cq_pkt->ctl = cpu_to_le32(tmp);
	cq_pkt->value = cpu_to_le32(cq_val);
	cq_pkt->addr = cpu_to_le64(cq_addr);

	cq_pkt++;

	tmp = FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_MSG_PROT);
	tmp |= FIELD_PREP(GAUDI_PKT_CTL_MB_MASK, 1);
	cq_pkt->ctl = cpu_to_le32(tmp);
	cq_pkt->value = cpu_to_le32(1);

	if (!gaudi->multi_msi_mode)
		msi_vec = 0;

	cq_pkt->addr = cpu_to_le64(CFG_BASE + mmPCIE_MSI_INTR_0 + msi_vec * 4);
}

static void gaudi_update_eq_ci(struct hl_device *hdev, u32 val)
{
	WREG32(mmCPU_IF_EQ_RD_OFFS, val);
}

static int gaudi_memset_device_memory(struct hl_device *hdev, u64 addr,
					u32 size, u64 val)
{
	struct packet_lin_dma *lin_dma_pkt;
	struct hl_cs_job *job;
	u32 cb_size, ctl, err_cause;
	struct hl_cb *cb;
	int rc;

	cb = hl_cb_kernel_create(hdev, PAGE_SIZE, false);
	if (!cb)
		return -EFAULT;

	lin_dma_pkt = cb->kernel_address;
	memset(lin_dma_pkt, 0, sizeof(*lin_dma_pkt));
	cb_size = sizeof(*lin_dma_pkt);

	ctl = FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_LIN_DMA);
	ctl |= FIELD_PREP(GAUDI_PKT_LIN_DMA_CTL_MEMSET_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_LIN_DMA_CTL_LIN_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_MB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_RB_MASK, 1);

	lin_dma_pkt->ctl = cpu_to_le32(ctl);
	lin_dma_pkt->src_addr = cpu_to_le64(val);
	lin_dma_pkt->dst_addr |= cpu_to_le64(addr);
	lin_dma_pkt->tsize = cpu_to_le32(size);

	job = hl_cs_allocate_job(hdev, QUEUE_TYPE_EXT, true);
	if (!job) {
		dev_err(hdev->dev, "Failed to allocate a new job\n");
		rc = -ENOMEM;
		goto release_cb;
	}

	/* Verify DMA is OK */
	err_cause = RREG32(mmDMA0_CORE_ERR_CAUSE);
	if (err_cause && !hdev->init_done) {
		dev_dbg(hdev->dev,
			"Clearing DMA0 engine from errors (cause 0x%x)\n",
			err_cause);
		WREG32(mmDMA0_CORE_ERR_CAUSE, err_cause);
	}

	job->id = 0;
	job->user_cb = cb;
	job->user_cb->cs_cnt++;
	job->user_cb_size = cb_size;
	job->hw_queue_id = GAUDI_QUEUE_ID_DMA_0_0;
	job->patched_cb = job->user_cb;
	job->job_cb_size = job->user_cb_size + sizeof(struct packet_msg_prot);

	hl_debugfs_add_job(hdev, job);

	rc = gaudi_send_job_on_qman0(hdev, job);
	hl_debugfs_remove_job(hdev, job);
	kfree(job);
	cb->cs_cnt--;

	/* Verify DMA is OK */
	err_cause = RREG32(mmDMA0_CORE_ERR_CAUSE);
	if (err_cause) {
		dev_err(hdev->dev, "DMA Failed, cause 0x%x\n", err_cause);
		rc = -EIO;
		if (!hdev->init_done) {
			dev_dbg(hdev->dev,
				"Clearing DMA0 engine from errors (cause 0x%x)\n",
				err_cause);
			WREG32(mmDMA0_CORE_ERR_CAUSE, err_cause);
		}
	}

release_cb:
	hl_cb_put(cb);
	hl_cb_destroy(hdev, &hdev->kernel_cb_mgr, cb->id << PAGE_SHIFT);

	return rc;
}

static void gaudi_restore_sm_registers(struct hl_device *hdev)
{
	int i;

	for (i = 0 ; i < NUM_OF_SOB_IN_BLOCK << 2 ; i += 4) {
		WREG32(mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0 + i, 0);
		WREG32(mmSYNC_MNGR_E_S_SYNC_MNGR_OBJS_SOB_OBJ_0 + i, 0);
		WREG32(mmSYNC_MNGR_W_N_SYNC_MNGR_OBJS_SOB_OBJ_0 + i, 0);
	}

	for (i = 0 ; i < NUM_OF_MONITORS_IN_BLOCK << 2 ; i += 4) {
		WREG32(mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_STATUS_0 + i, 0);
		WREG32(mmSYNC_MNGR_E_S_SYNC_MNGR_OBJS_MON_STATUS_0 + i, 0);
		WREG32(mmSYNC_MNGR_W_N_SYNC_MNGR_OBJS_MON_STATUS_0 + i, 0);
	}

	i = GAUDI_FIRST_AVAILABLE_W_S_SYNC_OBJECT * 4;

	for (; i < NUM_OF_SOB_IN_BLOCK << 2 ; i += 4)
		WREG32(mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_SOB_OBJ_0 + i, 0);

	i = GAUDI_FIRST_AVAILABLE_W_S_MONITOR * 4;

	for (; i < NUM_OF_MONITORS_IN_BLOCK << 2 ; i += 4)
		WREG32(mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_STATUS_0 + i, 0);
}

static void gaudi_restore_dma_registers(struct hl_device *hdev)
{
	u32 sob_delta = mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_1 -
			mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0;
	int i;

	for (i = 0 ; i < DMA_NUMBER_OF_CHANNELS ; i++) {
		u64 sob_addr = CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0 +
				(i * sob_delta);
		u32 dma_offset = i * DMA_CORE_OFFSET;

		WREG32(mmDMA0_CORE_WR_COMP_ADDR_LO + dma_offset,
				lower_32_bits(sob_addr));
		WREG32(mmDMA0_CORE_WR_COMP_ADDR_HI + dma_offset,
				upper_32_bits(sob_addr));
		WREG32(mmDMA0_CORE_WR_COMP_WDATA + dma_offset, 0x80000001);

		/* For DMAs 2-7, need to restore WR_AWUSER_31_11 as it can be
		 * modified by the user for SRAM reduction
		 */
		if (i > 1)
			WREG32(mmDMA0_CORE_WR_AWUSER_31_11 + dma_offset,
								0x00000001);
	}
}

static void gaudi_restore_qm_registers(struct hl_device *hdev)
{
	u32 qman_offset;
	int i;

	for (i = 0 ; i < DMA_NUMBER_OF_CHANNELS ; i++) {
		qman_offset = i * DMA_QMAN_OFFSET;
		WREG32(mmDMA0_QM_ARB_CFG_0 + qman_offset, 0);
	}

	for (i = 0 ; i < MME_NUMBER_OF_MASTER_ENGINES ; i++) {
		qman_offset = i * (mmMME2_QM_BASE - mmMME0_QM_BASE);
		WREG32(mmMME0_QM_ARB_CFG_0 + qman_offset, 0);
	}

	for (i = 0 ; i < TPC_NUMBER_OF_ENGINES ; i++) {
		qman_offset = i * TPC_QMAN_OFFSET;
		WREG32(mmTPC0_QM_ARB_CFG_0 + qman_offset, 0);
	}
}

static void gaudi_restore_user_registers(struct hl_device *hdev)
{
	gaudi_restore_sm_registers(hdev);
	gaudi_restore_dma_registers(hdev);
	gaudi_restore_qm_registers(hdev);
}

static int gaudi_context_switch(struct hl_device *hdev, u32 asid)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 addr = prop->sram_user_base_address;
	u32 size = hdev->pldm ? 0x10000 :
			(prop->sram_size - SRAM_USER_BASE_OFFSET);
	u64 val = 0x7777777777777777ull;
	int rc;

	rc = gaudi_memset_device_memory(hdev, addr, size, val);
	if (rc) {
		dev_err(hdev->dev, "Failed to clear SRAM in context switch\n");
		return rc;
	}

	gaudi_mmu_prepare(hdev, asid);

	gaudi_restore_user_registers(hdev);

	return 0;
}

static int gaudi_mmu_clear_pgt_range(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi_device *gaudi = hdev->asic_specific;
	u64 addr = prop->mmu_pgt_addr;
	u32 size = prop->mmu_pgt_size + MMU_CACHE_MNG_SIZE;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MMU))
		return 0;

	return gaudi_memset_device_memory(hdev, addr, size, 0);
}

static void gaudi_restore_phase_topology(struct hl_device *hdev)
{

}

static int gaudi_debugfs_read32(struct hl_device *hdev, u64 addr, u32 *val)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi_device *gaudi = hdev->asic_specific;
	u64 hbm_bar_addr;
	int rc = 0;

	if ((addr >= CFG_BASE) && (addr < CFG_BASE + CFG_SIZE)) {

		if ((gaudi->hw_cap_initialized & HW_CAP_CLK_GATE) &&
				(hdev->clock_gating_mask &
						GAUDI_CLK_GATE_DEBUGFS_MASK)) {

			dev_err_ratelimited(hdev->dev,
				"Can't read register - clock gating is enabled!\n");
			rc = -EFAULT;
		} else {
			*val = RREG32(addr - CFG_BASE);
		}

	} else if ((addr >= SRAM_BASE_ADDR) &&
			(addr < SRAM_BASE_ADDR + SRAM_BAR_SIZE)) {
		*val = readl(hdev->pcie_bar[SRAM_BAR_ID] +
				(addr - SRAM_BASE_ADDR));
	} else if (addr < DRAM_PHYS_BASE + hdev->asic_prop.dram_size) {
		u64 bar_base_addr = DRAM_PHYS_BASE +
				(addr & ~(prop->dram_pci_bar_size - 0x1ull));

		hbm_bar_addr = gaudi_set_hbm_bar_base(hdev, bar_base_addr);
		if (hbm_bar_addr != U64_MAX) {
			*val = readl(hdev->pcie_bar[HBM_BAR_ID] +
						(addr - bar_base_addr));

			hbm_bar_addr = gaudi_set_hbm_bar_base(hdev,
						hbm_bar_addr);
		}
		if (hbm_bar_addr == U64_MAX)
			rc = -EIO;
	} else if (addr >= HOST_PHYS_BASE && !iommu_present(&pci_bus_type)) {
		*val = *(u32 *) phys_to_virt(addr - HOST_PHYS_BASE);
	} else {
		rc = -EFAULT;
	}

	return rc;
}

static int gaudi_debugfs_write32(struct hl_device *hdev, u64 addr, u32 val)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi_device *gaudi = hdev->asic_specific;
	u64 hbm_bar_addr;
	int rc = 0;

	if ((addr >= CFG_BASE) && (addr < CFG_BASE + CFG_SIZE)) {

		if ((gaudi->hw_cap_initialized & HW_CAP_CLK_GATE) &&
				(hdev->clock_gating_mask &
						GAUDI_CLK_GATE_DEBUGFS_MASK)) {

			dev_err_ratelimited(hdev->dev,
				"Can't write register - clock gating is enabled!\n");
			rc = -EFAULT;
		} else {
			WREG32(addr - CFG_BASE, val);
		}

	} else if ((addr >= SRAM_BASE_ADDR) &&
			(addr < SRAM_BASE_ADDR + SRAM_BAR_SIZE)) {
		writel(val, hdev->pcie_bar[SRAM_BAR_ID] +
					(addr - SRAM_BASE_ADDR));
	} else if (addr < DRAM_PHYS_BASE + hdev->asic_prop.dram_size) {
		u64 bar_base_addr = DRAM_PHYS_BASE +
				(addr & ~(prop->dram_pci_bar_size - 0x1ull));

		hbm_bar_addr = gaudi_set_hbm_bar_base(hdev, bar_base_addr);
		if (hbm_bar_addr != U64_MAX) {
			writel(val, hdev->pcie_bar[HBM_BAR_ID] +
						(addr - bar_base_addr));

			hbm_bar_addr = gaudi_set_hbm_bar_base(hdev,
						hbm_bar_addr);
		}
		if (hbm_bar_addr == U64_MAX)
			rc = -EIO;
	} else if (addr >= HOST_PHYS_BASE && !iommu_present(&pci_bus_type)) {
		*(u32 *) phys_to_virt(addr - HOST_PHYS_BASE) = val;
	} else {
		rc = -EFAULT;
	}

	return rc;
}

static int gaudi_debugfs_read64(struct hl_device *hdev, u64 addr, u64 *val)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi_device *gaudi = hdev->asic_specific;
	u64 hbm_bar_addr;
	int rc = 0;

	if ((addr >= CFG_BASE) && (addr <= CFG_BASE + CFG_SIZE - sizeof(u64))) {

		if ((gaudi->hw_cap_initialized & HW_CAP_CLK_GATE) &&
				(hdev->clock_gating_mask &
						GAUDI_CLK_GATE_DEBUGFS_MASK)) {

			dev_err_ratelimited(hdev->dev,
				"Can't read register - clock gating is enabled!\n");
			rc = -EFAULT;
		} else {
			u32 val_l = RREG32(addr - CFG_BASE);
			u32 val_h = RREG32(addr + sizeof(u32) - CFG_BASE);

			*val = (((u64) val_h) << 32) | val_l;
		}

	} else if ((addr >= SRAM_BASE_ADDR) &&
		   (addr <= SRAM_BASE_ADDR + SRAM_BAR_SIZE - sizeof(u64))) {
		*val = readq(hdev->pcie_bar[SRAM_BAR_ID] +
				(addr - SRAM_BASE_ADDR));
	} else if (addr <=
		    DRAM_PHYS_BASE + hdev->asic_prop.dram_size - sizeof(u64)) {
		u64 bar_base_addr = DRAM_PHYS_BASE +
				(addr & ~(prop->dram_pci_bar_size - 0x1ull));

		hbm_bar_addr = gaudi_set_hbm_bar_base(hdev, bar_base_addr);
		if (hbm_bar_addr != U64_MAX) {
			*val = readq(hdev->pcie_bar[HBM_BAR_ID] +
						(addr - bar_base_addr));

			hbm_bar_addr = gaudi_set_hbm_bar_base(hdev,
						hbm_bar_addr);
		}
		if (hbm_bar_addr == U64_MAX)
			rc = -EIO;
	} else if (addr >= HOST_PHYS_BASE && !iommu_present(&pci_bus_type)) {
		*val = *(u64 *) phys_to_virt(addr - HOST_PHYS_BASE);
	} else {
		rc = -EFAULT;
	}

	return rc;
}

static int gaudi_debugfs_write64(struct hl_device *hdev, u64 addr, u64 val)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi_device *gaudi = hdev->asic_specific;
	u64 hbm_bar_addr;
	int rc = 0;

	if ((addr >= CFG_BASE) && (addr <= CFG_BASE + CFG_SIZE - sizeof(u64))) {

		if ((gaudi->hw_cap_initialized & HW_CAP_CLK_GATE) &&
				(hdev->clock_gating_mask &
						GAUDI_CLK_GATE_DEBUGFS_MASK)) {

			dev_err_ratelimited(hdev->dev,
				"Can't write register - clock gating is enabled!\n");
			rc = -EFAULT;
		} else {
			WREG32(addr - CFG_BASE, lower_32_bits(val));
			WREG32(addr + sizeof(u32) - CFG_BASE,
				upper_32_bits(val));
		}

	} else if ((addr >= SRAM_BASE_ADDR) &&
		   (addr <= SRAM_BASE_ADDR + SRAM_BAR_SIZE - sizeof(u64))) {
		writeq(val, hdev->pcie_bar[SRAM_BAR_ID] +
					(addr - SRAM_BASE_ADDR));
	} else if (addr <=
		    DRAM_PHYS_BASE + hdev->asic_prop.dram_size - sizeof(u64)) {
		u64 bar_base_addr = DRAM_PHYS_BASE +
				(addr & ~(prop->dram_pci_bar_size - 0x1ull));

		hbm_bar_addr = gaudi_set_hbm_bar_base(hdev, bar_base_addr);
		if (hbm_bar_addr != U64_MAX) {
			writeq(val, hdev->pcie_bar[HBM_BAR_ID] +
						(addr - bar_base_addr));

			hbm_bar_addr = gaudi_set_hbm_bar_base(hdev,
						hbm_bar_addr);
		}
		if (hbm_bar_addr == U64_MAX)
			rc = -EIO;
	} else if (addr >= HOST_PHYS_BASE && !iommu_present(&pci_bus_type)) {
		*(u64 *) phys_to_virt(addr - HOST_PHYS_BASE) = val;
	} else {
		rc = -EFAULT;
	}

	return rc;
}

static u64 gaudi_read_pte(struct hl_device *hdev, u64 addr)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (hdev->hard_reset_pending)
		return U64_MAX;

	return readq(hdev->pcie_bar[HBM_BAR_ID] +
			(addr - gaudi->hbm_bar_cur_addr));
}

static void gaudi_write_pte(struct hl_device *hdev, u64 addr, u64 val)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (hdev->hard_reset_pending)
		return;

	writeq(val, hdev->pcie_bar[HBM_BAR_ID] +
			(addr - gaudi->hbm_bar_cur_addr));
}

void gaudi_mmu_prepare_reg(struct hl_device *hdev, u64 reg, u32 asid)
{
	/* mask to zero the MMBP and ASID bits */
	WREG32_AND(reg, ~0x7FF);
	WREG32_OR(reg, asid);
}

static void gaudi_mmu_prepare(struct hl_device *hdev, u32 asid)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MMU))
		return;

	if (asid & ~DMA0_QM_GLBL_NON_SECURE_PROPS_0_ASID_MASK) {
		WARN(1, "asid %u is too big\n", asid);
		return;
	}

	mutex_lock(&gaudi->clk_gate_mutex);

	hdev->asic_funcs->disable_clock_gating(hdev);

	gaudi_mmu_prepare_reg(hdev, mmDMA0_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA0_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA0_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA0_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA0_QM_GLBL_NON_SECURE_PROPS_4, asid);

	gaudi_mmu_prepare_reg(hdev, mmDMA1_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA1_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA1_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA1_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA1_QM_GLBL_NON_SECURE_PROPS_4, asid);

	gaudi_mmu_prepare_reg(hdev, mmDMA2_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA2_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA2_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA2_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA2_QM_GLBL_NON_SECURE_PROPS_4, asid);

	gaudi_mmu_prepare_reg(hdev, mmDMA3_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA3_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA3_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA3_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA3_QM_GLBL_NON_SECURE_PROPS_4, asid);

	gaudi_mmu_prepare_reg(hdev, mmDMA4_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA4_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA4_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA4_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA4_QM_GLBL_NON_SECURE_PROPS_4, asid);

	gaudi_mmu_prepare_reg(hdev, mmDMA5_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA5_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA5_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA5_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA5_QM_GLBL_NON_SECURE_PROPS_4, asid);

	gaudi_mmu_prepare_reg(hdev, mmDMA6_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA6_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA6_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA6_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA6_QM_GLBL_NON_SECURE_PROPS_4, asid);

	gaudi_mmu_prepare_reg(hdev, mmDMA7_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA7_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA7_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA7_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA7_QM_GLBL_NON_SECURE_PROPS_4, asid);

	gaudi_mmu_prepare_reg(hdev, mmDMA0_CORE_NON_SECURE_PROPS, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA1_CORE_NON_SECURE_PROPS, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA2_CORE_NON_SECURE_PROPS, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA3_CORE_NON_SECURE_PROPS, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA4_CORE_NON_SECURE_PROPS, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA5_CORE_NON_SECURE_PROPS, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA6_CORE_NON_SECURE_PROPS, asid);
	gaudi_mmu_prepare_reg(hdev, mmDMA7_CORE_NON_SECURE_PROPS, asid);

	gaudi_mmu_prepare_reg(hdev, mmTPC0_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC0_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC0_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC0_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC0_QM_GLBL_NON_SECURE_PROPS_4, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC0_CFG_ARUSER_LO, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC0_CFG_AWUSER_LO, asid);

	gaudi_mmu_prepare_reg(hdev, mmTPC1_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC1_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC1_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC1_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC1_QM_GLBL_NON_SECURE_PROPS_4, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC1_CFG_ARUSER_LO, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC1_CFG_AWUSER_LO, asid);

	gaudi_mmu_prepare_reg(hdev, mmTPC2_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC2_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC2_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC2_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC2_QM_GLBL_NON_SECURE_PROPS_4, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC2_CFG_ARUSER_LO, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC2_CFG_AWUSER_LO, asid);

	gaudi_mmu_prepare_reg(hdev, mmTPC3_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC3_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC3_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC3_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC3_QM_GLBL_NON_SECURE_PROPS_4, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC3_CFG_ARUSER_LO, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC3_CFG_AWUSER_LO, asid);

	gaudi_mmu_prepare_reg(hdev, mmTPC4_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC4_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC4_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC4_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC4_QM_GLBL_NON_SECURE_PROPS_4, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC4_CFG_ARUSER_LO, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC4_CFG_AWUSER_LO, asid);

	gaudi_mmu_prepare_reg(hdev, mmTPC5_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC5_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC5_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC5_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC5_QM_GLBL_NON_SECURE_PROPS_4, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC5_CFG_ARUSER_LO, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC5_CFG_AWUSER_LO, asid);

	gaudi_mmu_prepare_reg(hdev, mmTPC6_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC6_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC6_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC6_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC6_QM_GLBL_NON_SECURE_PROPS_4, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC6_CFG_ARUSER_LO, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC6_CFG_AWUSER_LO, asid);

	gaudi_mmu_prepare_reg(hdev, mmTPC7_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC7_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC7_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC7_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC7_QM_GLBL_NON_SECURE_PROPS_4, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC7_CFG_ARUSER_LO, asid);
	gaudi_mmu_prepare_reg(hdev, mmTPC7_CFG_AWUSER_LO, asid);

	gaudi_mmu_prepare_reg(hdev, mmMME0_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME0_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME0_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME0_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME0_QM_GLBL_NON_SECURE_PROPS_4, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME2_QM_GLBL_NON_SECURE_PROPS_0, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME2_QM_GLBL_NON_SECURE_PROPS_1, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME2_QM_GLBL_NON_SECURE_PROPS_2, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME2_QM_GLBL_NON_SECURE_PROPS_3, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME2_QM_GLBL_NON_SECURE_PROPS_4, asid);

	gaudi_mmu_prepare_reg(hdev, mmMME0_SBAB_ARUSER0, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME0_SBAB_ARUSER1, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME1_SBAB_ARUSER0, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME1_SBAB_ARUSER1, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME2_SBAB_ARUSER0, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME2_SBAB_ARUSER1, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME3_SBAB_ARUSER0, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME3_SBAB_ARUSER1, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME0_ACC_WBC, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME1_ACC_WBC, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME2_ACC_WBC, asid);
	gaudi_mmu_prepare_reg(hdev, mmMME3_ACC_WBC, asid);

	hdev->asic_funcs->set_clock_gating(hdev);

	mutex_unlock(&gaudi->clk_gate_mutex);
}

static int gaudi_send_job_on_qman0(struct hl_device *hdev,
		struct hl_cs_job *job)
{
	struct packet_msg_prot *fence_pkt;
	u32 *fence_ptr;
	dma_addr_t fence_dma_addr;
	struct hl_cb *cb;
	u32 tmp, timeout, dma_offset;
	int rc;

	if (hdev->pldm)
		timeout = GAUDI_PLDM_QMAN0_TIMEOUT_USEC;
	else
		timeout = HL_DEVICE_TIMEOUT_USEC;

	if (!hdev->asic_funcs->is_device_idle(hdev, NULL, NULL)) {
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

	cb = job->patched_cb;

	fence_pkt = cb->kernel_address +
			job->job_cb_size - sizeof(struct packet_msg_prot);

	tmp = FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_MSG_PROT);
	tmp |= FIELD_PREP(GAUDI_PKT_CTL_EB_MASK, 1);
	tmp |= FIELD_PREP(GAUDI_PKT_CTL_MB_MASK, 1);

	fence_pkt->ctl = cpu_to_le32(tmp);
	fence_pkt->value = cpu_to_le32(GAUDI_QMAN0_FENCE_VAL);
	fence_pkt->addr = cpu_to_le64(fence_dma_addr);

	dma_offset = gaudi_dma_assignment[GAUDI_PCI_DMA_1] * DMA_CORE_OFFSET;

	WREG32_OR(mmDMA0_CORE_PROT + dma_offset, BIT(DMA0_CORE_PROT_VAL_SHIFT));

	rc = hl_hw_queue_send_cb_no_cmpl(hdev, GAUDI_QUEUE_ID_DMA_0_0,
					job->job_cb_size, cb->bus_address);
	if (rc) {
		dev_err(hdev->dev, "Failed to send CB on QMAN0, %d\n", rc);
		goto free_fence_ptr;
	}

	rc = hl_poll_timeout_memory(hdev, fence_ptr, tmp,
				(tmp == GAUDI_QMAN0_FENCE_VAL), 1000,
				timeout, true);

	hl_hw_queue_inc_ci_kernel(hdev, GAUDI_QUEUE_ID_DMA_0_0);

	if (rc == -ETIMEDOUT) {
		dev_err(hdev->dev, "QMAN0 Job timeout (0x%x)\n", tmp);
		goto free_fence_ptr;
	}

free_fence_ptr:
	WREG32_AND(mmDMA0_CORE_PROT + dma_offset,
			~BIT(DMA0_CORE_PROT_VAL_SHIFT));

	hdev->asic_funcs->asic_dma_pool_free(hdev, (void *) fence_ptr,
					fence_dma_addr);
	return rc;
}

static void gaudi_get_event_desc(u16 event_type, char *desc, size_t size)
{
	if (event_type >= GAUDI_EVENT_SIZE)
		goto event_not_supported;

	if (!gaudi_irq_map_table[event_type].valid)
		goto event_not_supported;

	snprintf(desc, size, gaudi_irq_map_table[event_type].name);

	return;

event_not_supported:
	snprintf(desc, size, "N/A");
}

static const char *gaudi_get_razwi_initiator_dma_name(struct hl_device *hdev,
							u32 x_y, bool is_write)
{
	u32 dma_id[2], dma_offset, err_cause[2], mask, i;

	mask = is_write ? DMA0_CORE_ERR_CAUSE_HBW_WR_ERR_MASK :
				DMA0_CORE_ERR_CAUSE_HBW_RD_ERR_MASK;

	switch (x_y) {
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_S_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_S_1:
		dma_id[0] = 0;
		dma_id[1] = 2;
		break;
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_S_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_S_1:
		dma_id[0] = 1;
		dma_id[1] = 3;
		break;
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_N_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_N_1:
		dma_id[0] = 4;
		dma_id[1] = 6;
		break;
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_N_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_N_1:
		dma_id[0] = 5;
		dma_id[1] = 7;
		break;
	default:
		goto unknown_initiator;
	}

	for (i = 0 ; i < 2 ; i++) {
		dma_offset = dma_id[i] * DMA_CORE_OFFSET;
		err_cause[i] = RREG32(mmDMA0_CORE_ERR_CAUSE + dma_offset);
	}

	switch (x_y) {
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_S_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_S_1:
		if ((err_cause[0] & mask) && !(err_cause[1] & mask))
			return "DMA0";
		else if (!(err_cause[0] & mask) && (err_cause[1] & mask))
			return "DMA2";
		else
			return "DMA0 or DMA2";
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_S_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_S_1:
		if ((err_cause[0] & mask) && !(err_cause[1] & mask))
			return "DMA1";
		else if (!(err_cause[0] & mask) && (err_cause[1] & mask))
			return "DMA3";
		else
			return "DMA1 or DMA3";
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_N_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_N_1:
		if ((err_cause[0] & mask) && !(err_cause[1] & mask))
			return "DMA4";
		else if (!(err_cause[0] & mask) && (err_cause[1] & mask))
			return "DMA6";
		else
			return "DMA4 or DMA6";
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_N_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_N_1:
		if ((err_cause[0] & mask) && !(err_cause[1] & mask))
			return "DMA5";
		else if (!(err_cause[0] & mask) && (err_cause[1] & mask))
			return "DMA7";
		else
			return "DMA5 or DMA7";
	}

unknown_initiator:
	return "unknown initiator";
}

static const char *gaudi_get_razwi_initiator_name(struct hl_device *hdev,
							bool is_write)
{
	u32 val, x_y, axi_id;

	val = is_write ? RREG32(mmMMU_UP_RAZWI_WRITE_ID) :
				RREG32(mmMMU_UP_RAZWI_READ_ID);
	x_y = val & ((RAZWI_INITIATOR_Y_MASK << RAZWI_INITIATOR_Y_SHIFT) |
			(RAZWI_INITIATOR_X_MASK << RAZWI_INITIATOR_X_SHIFT));
	axi_id = val & (RAZWI_INITIATOR_AXI_ID_MASK <<
			RAZWI_INITIATOR_AXI_ID_SHIFT);

	switch (x_y) {
	case RAZWI_INITIATOR_ID_X_Y_TPC0_NIC0:
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_TPC))
			return "TPC0";
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_NIC))
			return "NIC0";
		break;
	case RAZWI_INITIATOR_ID_X_Y_TPC1:
		return "TPC1";
	case RAZWI_INITIATOR_ID_X_Y_MME0_0:
	case RAZWI_INITIATOR_ID_X_Y_MME0_1:
		return "MME0";
	case RAZWI_INITIATOR_ID_X_Y_MME1_0:
	case RAZWI_INITIATOR_ID_X_Y_MME1_1:
		return "MME1";
	case RAZWI_INITIATOR_ID_X_Y_TPC2:
		return "TPC2";
	case RAZWI_INITIATOR_ID_X_Y_TPC3_PCI_CPU_PSOC:
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_TPC))
			return "TPC3";
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_PCI))
			return "PCI";
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_CPU))
			return "CPU";
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_PSOC))
			return "PSOC";
		break;
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_S_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_S_1:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_S_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_S_1:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_N_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_N_1:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_N_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_N_1:
		return gaudi_get_razwi_initiator_dma_name(hdev, x_y, is_write);
	case RAZWI_INITIATOR_ID_X_Y_TPC4_NIC1_NIC2:
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_TPC))
			return "TPC4";
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_NIC))
			return "NIC1";
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_NIC_FT))
			return "NIC2";
		break;
	case RAZWI_INITIATOR_ID_X_Y_TPC5:
		return "TPC5";
	case RAZWI_INITIATOR_ID_X_Y_MME2_0:
	case RAZWI_INITIATOR_ID_X_Y_MME2_1:
		return "MME2";
	case RAZWI_INITIATOR_ID_X_Y_MME3_0:
	case RAZWI_INITIATOR_ID_X_Y_MME3_1:
		return "MME3";
	case RAZWI_INITIATOR_ID_X_Y_TPC6:
		return "TPC6";
	case RAZWI_INITIATOR_ID_X_Y_TPC7_NIC4_NIC5:
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_TPC))
			return "TPC7";
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_NIC))
			return "NIC4";
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_NIC_FT))
			return "NIC5";
		break;
	default:
		break;
	}

	dev_err(hdev->dev,
		"Unknown RAZWI initiator ID 0x%x [Y=%d, X=%d, AXI_ID=%d]\n",
		val,
		(val >> RAZWI_INITIATOR_Y_SHIFT) & RAZWI_INITIATOR_Y_MASK,
		(val >> RAZWI_INITIATOR_X_SHIFT) & RAZWI_INITIATOR_X_MASK,
		(val >> RAZWI_INITIATOR_AXI_ID_SHIFT) &
			RAZWI_INITIATOR_AXI_ID_MASK);

	return "unknown initiator";
}

static void gaudi_print_razwi_info(struct hl_device *hdev)
{
	if (RREG32(mmMMU_UP_RAZWI_WRITE_VLD)) {
		dev_err_ratelimited(hdev->dev,
			"RAZWI event caused by illegal write of %s\n",
			gaudi_get_razwi_initiator_name(hdev, true));
		WREG32(mmMMU_UP_RAZWI_WRITE_VLD, 0);
	}

	if (RREG32(mmMMU_UP_RAZWI_READ_VLD)) {
		dev_err_ratelimited(hdev->dev,
			"RAZWI event caused by illegal read of %s\n",
			gaudi_get_razwi_initiator_name(hdev, false));
		WREG32(mmMMU_UP_RAZWI_READ_VLD, 0);
	}
}

static void gaudi_print_mmu_error_info(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u64 addr;
	u32 val;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MMU))
		return;

	val = RREG32(mmMMU_UP_PAGE_ERROR_CAPTURE);
	if (val & MMU_UP_PAGE_ERROR_CAPTURE_ENTRY_VALID_MASK) {
		addr = val & MMU_UP_PAGE_ERROR_CAPTURE_VA_49_32_MASK;
		addr <<= 32;
		addr |= RREG32(mmMMU_UP_PAGE_ERROR_CAPTURE_VA);

		dev_err_ratelimited(hdev->dev, "MMU page fault on va 0x%llx\n",
					addr);

		WREG32(mmMMU_UP_PAGE_ERROR_CAPTURE, 0);
	}

	val = RREG32(mmMMU_UP_ACCESS_ERROR_CAPTURE);
	if (val & MMU_UP_ACCESS_ERROR_CAPTURE_ENTRY_VALID_MASK) {
		addr = val & MMU_UP_ACCESS_ERROR_CAPTURE_VA_49_32_MASK;
		addr <<= 32;
		addr |= RREG32(mmMMU_UP_ACCESS_ERROR_CAPTURE_VA);

		dev_err_ratelimited(hdev->dev,
				"MMU access error on va 0x%llx\n", addr);

		WREG32(mmMMU_UP_ACCESS_ERROR_CAPTURE, 0);
	}
}

/*
 *  +-------------------+------------------------------------------------------+
 *  | Configuration Reg |                     Description                      |
 *  |      Address      |                                                      |
 *  +-------------------+------------------------------------------------------+
 *  |  0xF30 - 0xF3F    |ECC single error indication (1 bit per memory wrapper)|
 *  |                   |0xF30 memory wrappers 31:0 (MSB to LSB)               |
 *  |                   |0xF34 memory wrappers 63:32                           |
 *  |                   |0xF38 memory wrappers 95:64                           |
 *  |                   |0xF3C memory wrappers 127:96                          |
 *  +-------------------+------------------------------------------------------+
 *  |  0xF40 - 0xF4F    |ECC double error indication (1 bit per memory wrapper)|
 *  |                   |0xF40 memory wrappers 31:0 (MSB to LSB)               |
 *  |                   |0xF44 memory wrappers 63:32                           |
 *  |                   |0xF48 memory wrappers 95:64                           |
 *  |                   |0xF4C memory wrappers 127:96                          |
 *  +-------------------+------------------------------------------------------+
 */
static int gaudi_extract_ecc_info(struct hl_device *hdev,
		struct ecc_info_extract_params *params, u64 *ecc_address,
		u64 *ecc_syndrom, u8 *memory_wrapper_idx)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 i, num_mem_regs, reg, err_bit;
	u64 err_addr, err_word = 0;
	int rc = 0;

	num_mem_regs = params->num_memories / 32 +
			((params->num_memories % 32) ? 1 : 0);

	if (params->block_address >= CFG_BASE)
		params->block_address -= CFG_BASE;

	if (params->derr)
		err_addr = params->block_address + GAUDI_ECC_DERR0_OFFSET;
	else
		err_addr = params->block_address + GAUDI_ECC_SERR0_OFFSET;

	if (params->disable_clock_gating) {
		mutex_lock(&gaudi->clk_gate_mutex);
		hdev->asic_funcs->disable_clock_gating(hdev);
	}

	/* Set invalid wrapper index */
	*memory_wrapper_idx = 0xFF;

	/* Iterate through memory wrappers, a single bit must be set */
	for (i = 0 ; i < num_mem_regs ; i++) {
		err_addr += i * 4;
		err_word = RREG32(err_addr);
		if (err_word) {
			err_bit = __ffs(err_word);
			*memory_wrapper_idx = err_bit + (32 * i);
			break;
		}
	}

	if (*memory_wrapper_idx == 0xFF) {
		dev_err(hdev->dev, "ECC error information cannot be found\n");
		rc = -EINVAL;
		goto enable_clk_gate;
	}

	WREG32(params->block_address + GAUDI_ECC_MEM_SEL_OFFSET,
			*memory_wrapper_idx);

	*ecc_address =
		RREG32(params->block_address + GAUDI_ECC_ADDRESS_OFFSET);
	*ecc_syndrom =
		RREG32(params->block_address + GAUDI_ECC_SYNDROME_OFFSET);

	/* Clear error indication */
	reg = RREG32(params->block_address + GAUDI_ECC_MEM_INFO_CLR_OFFSET);
	if (params->derr)
		reg |= FIELD_PREP(GAUDI_ECC_MEM_INFO_CLR_DERR_MASK, 1);
	else
		reg |= FIELD_PREP(GAUDI_ECC_MEM_INFO_CLR_SERR_MASK, 1);

	WREG32(params->block_address + GAUDI_ECC_MEM_INFO_CLR_OFFSET, reg);

enable_clk_gate:
	if (params->disable_clock_gating) {
		hdev->asic_funcs->set_clock_gating(hdev);

		mutex_unlock(&gaudi->clk_gate_mutex);
	}

	return rc;
}

static void gaudi_handle_qman_err_generic(struct hl_device *hdev,
					  const char *qm_name,
					  u64 glbl_sts_addr,
					  u64 arb_err_addr)
{
	u32 i, j, glbl_sts_val, arb_err_val, glbl_sts_clr_val;
	char reg_desc[32];

	/* Iterate through all stream GLBL_STS1 registers + Lower CP */
	for (i = 0 ; i < QMAN_STREAMS + 1 ; i++) {
		glbl_sts_clr_val = 0;
		glbl_sts_val = RREG32(glbl_sts_addr + 4 * i);

		if (!glbl_sts_val)
			continue;

		if (i == QMAN_STREAMS)
			snprintf(reg_desc, ARRAY_SIZE(reg_desc), "LowerCP");
		else
			snprintf(reg_desc, ARRAY_SIZE(reg_desc), "stream%u", i);

		for (j = 0 ; j < GAUDI_NUM_OF_QM_ERR_CAUSE ; j++) {
			if (glbl_sts_val & BIT(j)) {
				dev_err_ratelimited(hdev->dev,
						"%s %s. err cause: %s\n",
						qm_name, reg_desc,
						gaudi_qman_error_cause[j]);
				glbl_sts_clr_val |= BIT(j);
			}
		}

		/* Write 1 clear errors */
		WREG32(glbl_sts_addr + 4 * i, glbl_sts_clr_val);
	}

	arb_err_val = RREG32(arb_err_addr);

	if (!arb_err_val)
		return;

	for (j = 0 ; j < GAUDI_NUM_OF_QM_ARB_ERR_CAUSE ; j++) {
		if (arb_err_val & BIT(j)) {
			dev_err_ratelimited(hdev->dev,
					"%s ARB_ERR. err cause: %s\n",
					qm_name,
					gaudi_qman_arb_error_cause[j]);
		}
	}
}

static void gaudi_handle_ecc_event(struct hl_device *hdev, u16 event_type,
		struct hl_eq_ecc_data *ecc_data)
{
	struct ecc_info_extract_params params;
	u64 ecc_address = 0, ecc_syndrom = 0;
	u8 index, memory_wrapper_idx = 0;
	bool extract_info_from_fw;
	int rc;

	switch (event_type) {
	case GAUDI_EVENT_PCIE_CORE_SERR ... GAUDI_EVENT_PCIE_PHY_DERR:
	case GAUDI_EVENT_DMA0_SERR_ECC ... GAUDI_EVENT_MMU_DERR:
		extract_info_from_fw = true;
		break;
	case GAUDI_EVENT_TPC0_SERR ... GAUDI_EVENT_TPC7_SERR:
		index = event_type - GAUDI_EVENT_TPC0_SERR;
		params.block_address = mmTPC0_CFG_BASE + index * TPC_CFG_OFFSET;
		params.num_memories = 90;
		params.derr = false;
		params.disable_clock_gating = true;
		extract_info_from_fw = false;
		break;
	case GAUDI_EVENT_TPC0_DERR ... GAUDI_EVENT_TPC7_DERR:
		index = event_type - GAUDI_EVENT_TPC0_DERR;
		params.block_address =
			mmTPC0_CFG_BASE + index * TPC_CFG_OFFSET;
		params.num_memories = 90;
		params.derr = true;
		params.disable_clock_gating = true;
		extract_info_from_fw = false;
		break;
	case GAUDI_EVENT_MME0_ACC_SERR:
	case GAUDI_EVENT_MME1_ACC_SERR:
	case GAUDI_EVENT_MME2_ACC_SERR:
	case GAUDI_EVENT_MME3_ACC_SERR:
		index = (event_type - GAUDI_EVENT_MME0_ACC_SERR) / 4;
		params.block_address = mmMME0_ACC_BASE + index * MME_ACC_OFFSET;
		params.num_memories = 128;
		params.derr = false;
		params.disable_clock_gating = true;
		extract_info_from_fw = false;
		break;
	case GAUDI_EVENT_MME0_ACC_DERR:
	case GAUDI_EVENT_MME1_ACC_DERR:
	case GAUDI_EVENT_MME2_ACC_DERR:
	case GAUDI_EVENT_MME3_ACC_DERR:
		index = (event_type - GAUDI_EVENT_MME0_ACC_DERR) / 4;
		params.block_address = mmMME0_ACC_BASE + index * MME_ACC_OFFSET;
		params.num_memories = 128;
		params.derr = true;
		params.disable_clock_gating = true;
		extract_info_from_fw = false;
		break;
	case GAUDI_EVENT_MME0_SBAB_SERR:
	case GAUDI_EVENT_MME1_SBAB_SERR:
	case GAUDI_EVENT_MME2_SBAB_SERR:
	case GAUDI_EVENT_MME3_SBAB_SERR:
		index = (event_type - GAUDI_EVENT_MME0_SBAB_SERR) / 4;
		params.block_address =
			mmMME0_SBAB_BASE + index * MME_ACC_OFFSET;
		params.num_memories = 33;
		params.derr = false;
		params.disable_clock_gating = true;
		extract_info_from_fw = false;
		break;
	case GAUDI_EVENT_MME0_SBAB_DERR:
	case GAUDI_EVENT_MME1_SBAB_DERR:
	case GAUDI_EVENT_MME2_SBAB_DERR:
	case GAUDI_EVENT_MME3_SBAB_DERR:
		index = (event_type - GAUDI_EVENT_MME0_SBAB_DERR) / 4;
		params.block_address =
			mmMME0_SBAB_BASE + index * MME_ACC_OFFSET;
		params.num_memories = 33;
		params.derr = true;
		params.disable_clock_gating = true;
	default:
		return;
	}

	if (extract_info_from_fw) {
		ecc_address = le64_to_cpu(ecc_data->ecc_address);
		ecc_syndrom = le64_to_cpu(ecc_data->ecc_syndrom);
		memory_wrapper_idx = ecc_data->memory_wrapper_idx;
	} else {
		rc = gaudi_extract_ecc_info(hdev, &params, &ecc_address,
				&ecc_syndrom, &memory_wrapper_idx);
		if (rc)
			return;
	}

	dev_err(hdev->dev,
		"ECC error detected. address: %#llx. Syndrom: %#llx. block id %u\n",
		ecc_address, ecc_syndrom, memory_wrapper_idx);
}

static void gaudi_handle_qman_err(struct hl_device *hdev, u16 event_type)
{
	u64 glbl_sts_addr, arb_err_addr;
	u8 index;
	char desc[32];

	switch (event_type) {
	case GAUDI_EVENT_TPC0_QM ... GAUDI_EVENT_TPC7_QM:
		index = event_type - GAUDI_EVENT_TPC0_QM;
		glbl_sts_addr =
			mmTPC0_QM_GLBL_STS1_0 + index * TPC_QMAN_OFFSET;
		arb_err_addr =
			mmTPC0_QM_ARB_ERR_CAUSE + index * TPC_QMAN_OFFSET;
		snprintf(desc, ARRAY_SIZE(desc), "%s%d", "TPC_QM", index);
		break;
	case GAUDI_EVENT_MME0_QM ... GAUDI_EVENT_MME2_QM:
		index = event_type - GAUDI_EVENT_MME0_QM;
		glbl_sts_addr =
			mmMME0_QM_GLBL_STS1_0 + index * MME_QMAN_OFFSET;
		arb_err_addr =
			mmMME0_QM_ARB_ERR_CAUSE + index * MME_QMAN_OFFSET;
		snprintf(desc, ARRAY_SIZE(desc), "%s%d", "MME_QM", index);
		break;
	case GAUDI_EVENT_DMA0_QM ... GAUDI_EVENT_DMA7_QM:
		index = event_type - GAUDI_EVENT_DMA0_QM;
		glbl_sts_addr =
			mmDMA0_QM_GLBL_STS1_0 + index * DMA_QMAN_OFFSET;
		arb_err_addr =
			mmDMA0_QM_ARB_ERR_CAUSE + index * DMA_QMAN_OFFSET;
		snprintf(desc, ARRAY_SIZE(desc), "%s%d", "DMA_QM", index);
		break;
	default:
		return;
	}

	gaudi_handle_qman_err_generic(hdev, desc, glbl_sts_addr, arb_err_addr);
}

static void gaudi_print_irq_info(struct hl_device *hdev, u16 event_type,
					bool razwi)
{
	char desc[64] = "";

	gaudi_get_event_desc(event_type, desc, sizeof(desc));
	dev_err_ratelimited(hdev->dev, "Received H/W interrupt %d [\"%s\"]\n",
		event_type, desc);

	if (razwi) {
		gaudi_print_razwi_info(hdev);
		gaudi_print_mmu_error_info(hdev);
	}
}

static int gaudi_soft_reset_late_init(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	/* Unmask all IRQs since some could have been received
	 * during the soft reset
	 */
	return hl_fw_unmask_irq_arr(hdev, gaudi->events, sizeof(gaudi->events));
}

static int gaudi_hbm_read_interrupts(struct hl_device *hdev, int device)
{
	int ch, err = 0;
	u32 base, val, val2;

	base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		val = RREG32_MASK(base + ch * 0x1000 + 0x06C, 0x0000FFFF);
		val = (val & 0xFF) | ((val >> 8) & 0xFF);
		if (val) {
			err = 1;
			dev_err(hdev->dev,
				"HBM%d pc%d interrupts info: WR_PAR=%d, RD_PAR=%d, CA_PAR=%d, SERR=%d, DERR=%d\n",
				device, ch * 2, val & 0x1, (val >> 1) & 0x1,
				(val >> 2) & 0x1, (val >> 3) & 0x1,
				(val >> 4) & 0x1);

			val2 = RREG32(base + ch * 0x1000 + 0x060);
			dev_err(hdev->dev,
				"HBM%d pc%d ECC info: 1ST_ERR_ADDR=0x%x, 1ST_ERR_TYPE=%d, SEC_CONT_CNT=%d, SEC_CNT=%d, DED_CNT=%d\n",
				device, ch * 2,
				RREG32(base + ch * 0x1000 + 0x064),
				(val2 & 0x200) >> 9, (val2 & 0xFC00) >> 10,
				(val2 & 0xFF0000) >> 16,
				(val2 & 0xFF000000) >> 24);
		}

		val = RREG32_MASK(base + ch * 0x1000 + 0x07C, 0x0000FFFF);
		val = (val & 0xFF) | ((val >> 8) & 0xFF);
		if (val) {
			err = 1;
			dev_err(hdev->dev,
				"HBM%d pc%d interrupts info: WR_PAR=%d, RD_PAR=%d, CA_PAR=%d, SERR=%d, DERR=%d\n",
				device, ch * 2 + 1, val & 0x1, (val >> 1) & 0x1,
				(val >> 2) & 0x1, (val >> 3) & 0x1,
				(val >> 4) & 0x1);

			val2 = RREG32(base + ch * 0x1000 + 0x070);
			dev_err(hdev->dev,
				"HBM%d pc%d ECC info: 1ST_ERR_ADDR=0x%x, 1ST_ERR_TYPE=%d, SEC_CONT_CNT=%d, SEC_CNT=%d, DED_CNT=%d\n",
				device, ch * 2 + 1,
				RREG32(base + ch * 0x1000 + 0x074),
				(val2 & 0x200) >> 9, (val2 & 0xFC00) >> 10,
				(val2 & 0xFF0000) >> 16,
				(val2 & 0xFF000000) >> 24);
		}

		/* Clear interrupts */
		RMWREG32(base + (ch * 0x1000) + 0x060, 0x1C8, 0x1FF);
		RMWREG32(base + (ch * 0x1000) + 0x070, 0x1C8, 0x1FF);
		WREG32(base + (ch * 0x1000) + 0x06C, 0x1F1F);
		WREG32(base + (ch * 0x1000) + 0x07C, 0x1F1F);
		RMWREG32(base + (ch * 0x1000) + 0x060, 0x0, 0xF);
		RMWREG32(base + (ch * 0x1000) + 0x070, 0x0, 0xF);
	}

	val  = RREG32(base + 0x8F30);
	val2 = RREG32(base + 0x8F34);
	if (val | val2) {
		err = 1;
		dev_err(hdev->dev,
			"HBM %d MC SRAM SERR info: Reg 0x8F30=0x%x, Reg 0x8F34=0x%x\n",
			device, val, val2);
	}
	val  = RREG32(base + 0x8F40);
	val2 = RREG32(base + 0x8F44);
	if (val | val2) {
		err = 1;
		dev_err(hdev->dev,
			"HBM %d MC SRAM DERR info: Reg 0x8F40=0x%x, Reg 0x8F44=0x%x\n",
			device, val, val2);
	}

	return err;
}

static int gaudi_hbm_event_to_dev(u16 hbm_event_type)
{
	switch (hbm_event_type) {
	case GAUDI_EVENT_HBM0_SPI_0:
	case GAUDI_EVENT_HBM0_SPI_1:
		return 0;
	case GAUDI_EVENT_HBM1_SPI_0:
	case GAUDI_EVENT_HBM1_SPI_1:
		return 1;
	case GAUDI_EVENT_HBM2_SPI_0:
	case GAUDI_EVENT_HBM2_SPI_1:
		return 2;
	case GAUDI_EVENT_HBM3_SPI_0:
	case GAUDI_EVENT_HBM3_SPI_1:
		return 3;
	default:
		break;
	}

	/* Should never happen */
	return 0;
}

static bool gaudi_tpc_read_interrupts(struct hl_device *hdev, u8 tpc_id,
					char *interrupt_name)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 tpc_offset = tpc_id * TPC_CFG_OFFSET, tpc_interrupts_cause, i;
	bool soft_reset_required = false;

	/* Accessing the TPC_INTR_CAUSE registers requires disabling the clock
	 * gating, and thus cannot be done in CPU-CP and should be done instead
	 * by the driver.
	 */

	mutex_lock(&gaudi->clk_gate_mutex);

	hdev->asic_funcs->disable_clock_gating(hdev);

	tpc_interrupts_cause = RREG32(mmTPC0_CFG_TPC_INTR_CAUSE + tpc_offset) &
				TPC0_CFG_TPC_INTR_CAUSE_CAUSE_MASK;

	for (i = 0 ; i < GAUDI_NUM_OF_TPC_INTR_CAUSE ; i++)
		if (tpc_interrupts_cause & BIT(i)) {
			dev_err_ratelimited(hdev->dev,
					"TPC%d_%s interrupt cause: %s\n",
					tpc_id, interrupt_name,
					gaudi_tpc_interrupts_cause[i]);
			/* If this is QM error, we need to soft-reset */
			if (i == 15)
				soft_reset_required = true;
		}

	/* Clear interrupts */
	WREG32(mmTPC0_CFG_TPC_INTR_CAUSE + tpc_offset, 0);

	hdev->asic_funcs->set_clock_gating(hdev);

	mutex_unlock(&gaudi->clk_gate_mutex);

	return soft_reset_required;
}

static int tpc_dec_event_to_tpc_id(u16 tpc_dec_event_type)
{
	return (tpc_dec_event_type - GAUDI_EVENT_TPC0_DEC) >> 1;
}

static int tpc_krn_event_to_tpc_id(u16 tpc_dec_event_type)
{
	return (tpc_dec_event_type - GAUDI_EVENT_TPC0_KRN_ERR) / 6;
}

static void gaudi_print_clk_change_info(struct hl_device *hdev,
					u16 event_type)
{
	switch (event_type) {
	case GAUDI_EVENT_FIX_POWER_ENV_S:
		hdev->clk_throttling_reason |= HL_CLK_THROTTLE_POWER;
		dev_info_ratelimited(hdev->dev,
			"Clock throttling due to power consumption\n");
		break;

	case GAUDI_EVENT_FIX_POWER_ENV_E:
		hdev->clk_throttling_reason &= ~HL_CLK_THROTTLE_POWER;
		dev_info_ratelimited(hdev->dev,
			"Power envelop is safe, back to optimal clock\n");
		break;

	case GAUDI_EVENT_FIX_THERMAL_ENV_S:
		hdev->clk_throttling_reason |= HL_CLK_THROTTLE_THERMAL;
		dev_info_ratelimited(hdev->dev,
			"Clock throttling due to overheating\n");
		break;

	case GAUDI_EVENT_FIX_THERMAL_ENV_E:
		hdev->clk_throttling_reason &= ~HL_CLK_THROTTLE_THERMAL;
		dev_info_ratelimited(hdev->dev,
			"Thermal envelop is safe, back to optimal clock\n");
		break;

	default:
		dev_err(hdev->dev, "Received invalid clock change event %d\n",
			event_type);
		break;
	}
}

static void gaudi_handle_eqe(struct hl_device *hdev,
				struct hl_eq_entry *eq_entry)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 ctl = le32_to_cpu(eq_entry->hdr.ctl);
	u16 event_type = ((ctl & EQ_CTL_EVENT_TYPE_MASK)
			>> EQ_CTL_EVENT_TYPE_SHIFT);
	u8 cause;
	bool reset_required;

	gaudi->events_stat[event_type]++;
	gaudi->events_stat_aggregate[event_type]++;

	switch (event_type) {
	case GAUDI_EVENT_PCIE_CORE_DERR:
	case GAUDI_EVENT_PCIE_IF_DERR:
	case GAUDI_EVENT_PCIE_PHY_DERR:
	case GAUDI_EVENT_TPC0_DERR ... GAUDI_EVENT_TPC7_DERR:
	case GAUDI_EVENT_MME0_ACC_DERR:
	case GAUDI_EVENT_MME0_SBAB_DERR:
	case GAUDI_EVENT_MME1_ACC_DERR:
	case GAUDI_EVENT_MME1_SBAB_DERR:
	case GAUDI_EVENT_MME2_ACC_DERR:
	case GAUDI_EVENT_MME2_SBAB_DERR:
	case GAUDI_EVENT_MME3_ACC_DERR:
	case GAUDI_EVENT_MME3_SBAB_DERR:
	case GAUDI_EVENT_DMA0_DERR_ECC ... GAUDI_EVENT_DMA7_DERR_ECC:
		fallthrough;
	case GAUDI_EVENT_CPU_IF_ECC_DERR:
	case GAUDI_EVENT_PSOC_MEM_DERR:
	case GAUDI_EVENT_PSOC_CORESIGHT_DERR:
	case GAUDI_EVENT_SRAM0_DERR ... GAUDI_EVENT_SRAM28_DERR:
	case GAUDI_EVENT_DMA_IF0_DERR ... GAUDI_EVENT_DMA_IF3_DERR:
	case GAUDI_EVENT_HBM_0_DERR ... GAUDI_EVENT_HBM_3_DERR:
	case GAUDI_EVENT_MMU_DERR:
		gaudi_print_irq_info(hdev, event_type, true);
		gaudi_handle_ecc_event(hdev, event_type, &eq_entry->ecc_data);
		if (hdev->hard_reset_on_fw_events)
			hl_device_reset(hdev, true, false);
		break;

	case GAUDI_EVENT_GIC500:
	case GAUDI_EVENT_AXI_ECC:
	case GAUDI_EVENT_L2_RAM_ECC:
	case GAUDI_EVENT_PLL0 ... GAUDI_EVENT_PLL17:
		gaudi_print_irq_info(hdev, event_type, false);
		if (hdev->hard_reset_on_fw_events)
			hl_device_reset(hdev, true, false);
		break;

	case GAUDI_EVENT_HBM0_SPI_0:
	case GAUDI_EVENT_HBM1_SPI_0:
	case GAUDI_EVENT_HBM2_SPI_0:
	case GAUDI_EVENT_HBM3_SPI_0:
		gaudi_print_irq_info(hdev, event_type, false);
		gaudi_hbm_read_interrupts(hdev,
					  gaudi_hbm_event_to_dev(event_type));
		if (hdev->hard_reset_on_fw_events)
			hl_device_reset(hdev, true, false);
		break;

	case GAUDI_EVENT_HBM0_SPI_1:
	case GAUDI_EVENT_HBM1_SPI_1:
	case GAUDI_EVENT_HBM2_SPI_1:
	case GAUDI_EVENT_HBM3_SPI_1:
		gaudi_print_irq_info(hdev, event_type, false);
		gaudi_hbm_read_interrupts(hdev,
					  gaudi_hbm_event_to_dev(event_type));
		break;

	case GAUDI_EVENT_TPC0_DEC:
	case GAUDI_EVENT_TPC1_DEC:
	case GAUDI_EVENT_TPC2_DEC:
	case GAUDI_EVENT_TPC3_DEC:
	case GAUDI_EVENT_TPC4_DEC:
	case GAUDI_EVENT_TPC5_DEC:
	case GAUDI_EVENT_TPC6_DEC:
	case GAUDI_EVENT_TPC7_DEC:
		gaudi_print_irq_info(hdev, event_type, true);
		reset_required = gaudi_tpc_read_interrupts(hdev,
					tpc_dec_event_to_tpc_id(event_type),
					"AXI_SLV_DEC_Error");
		if (reset_required) {
			dev_err(hdev->dev, "hard reset required due to %s\n",
				gaudi_irq_map_table[event_type].name);

			if (hdev->hard_reset_on_fw_events)
				hl_device_reset(hdev, true, false);
		} else {
			hl_fw_unmask_irq(hdev, event_type);
		}
		break;

	case GAUDI_EVENT_TPC0_KRN_ERR:
	case GAUDI_EVENT_TPC1_KRN_ERR:
	case GAUDI_EVENT_TPC2_KRN_ERR:
	case GAUDI_EVENT_TPC3_KRN_ERR:
	case GAUDI_EVENT_TPC4_KRN_ERR:
	case GAUDI_EVENT_TPC5_KRN_ERR:
	case GAUDI_EVENT_TPC6_KRN_ERR:
	case GAUDI_EVENT_TPC7_KRN_ERR:
		gaudi_print_irq_info(hdev, event_type, true);
		reset_required = gaudi_tpc_read_interrupts(hdev,
					tpc_krn_event_to_tpc_id(event_type),
					"KRN_ERR");
		if (reset_required) {
			dev_err(hdev->dev, "hard reset required due to %s\n",
				gaudi_irq_map_table[event_type].name);

			if (hdev->hard_reset_on_fw_events)
				hl_device_reset(hdev, true, false);
		} else {
			hl_fw_unmask_irq(hdev, event_type);
		}
		break;

	case GAUDI_EVENT_PCIE_CORE_SERR:
	case GAUDI_EVENT_PCIE_IF_SERR:
	case GAUDI_EVENT_PCIE_PHY_SERR:
	case GAUDI_EVENT_TPC0_SERR ... GAUDI_EVENT_TPC7_SERR:
	case GAUDI_EVENT_MME0_ACC_SERR:
	case GAUDI_EVENT_MME0_SBAB_SERR:
	case GAUDI_EVENT_MME1_ACC_SERR:
	case GAUDI_EVENT_MME1_SBAB_SERR:
	case GAUDI_EVENT_MME2_ACC_SERR:
	case GAUDI_EVENT_MME2_SBAB_SERR:
	case GAUDI_EVENT_MME3_ACC_SERR:
	case GAUDI_EVENT_MME3_SBAB_SERR:
	case GAUDI_EVENT_DMA0_SERR_ECC ... GAUDI_EVENT_DMA7_SERR_ECC:
	case GAUDI_EVENT_CPU_IF_ECC_SERR:
	case GAUDI_EVENT_PSOC_MEM_SERR:
	case GAUDI_EVENT_PSOC_CORESIGHT_SERR:
	case GAUDI_EVENT_SRAM0_SERR ... GAUDI_EVENT_SRAM28_SERR:
	case GAUDI_EVENT_DMA_IF0_SERR ... GAUDI_EVENT_DMA_IF3_SERR:
	case GAUDI_EVENT_HBM_0_SERR ... GAUDI_EVENT_HBM_3_SERR:
		fallthrough;
	case GAUDI_EVENT_MMU_SERR:
		gaudi_print_irq_info(hdev, event_type, true);
		gaudi_handle_ecc_event(hdev, event_type, &eq_entry->ecc_data);
		hl_fw_unmask_irq(hdev, event_type);
		break;

	case GAUDI_EVENT_PCIE_DEC:
	case GAUDI_EVENT_MME0_WBC_RSP:
	case GAUDI_EVENT_MME0_SBAB0_RSP:
	case GAUDI_EVENT_MME1_WBC_RSP:
	case GAUDI_EVENT_MME1_SBAB0_RSP:
	case GAUDI_EVENT_MME2_WBC_RSP:
	case GAUDI_EVENT_MME2_SBAB0_RSP:
	case GAUDI_EVENT_MME3_WBC_RSP:
	case GAUDI_EVENT_MME3_SBAB0_RSP:
	case GAUDI_EVENT_CPU_AXI_SPLITTER:
	case GAUDI_EVENT_PSOC_AXI_DEC:
	case GAUDI_EVENT_PSOC_PRSTN_FALL:
	case GAUDI_EVENT_MMU_PAGE_FAULT:
	case GAUDI_EVENT_MMU_WR_PERM:
	case GAUDI_EVENT_RAZWI_OR_ADC:
	case GAUDI_EVENT_TPC0_QM ... GAUDI_EVENT_TPC7_QM:
	case GAUDI_EVENT_MME0_QM ... GAUDI_EVENT_MME2_QM:
	case GAUDI_EVENT_DMA0_QM ... GAUDI_EVENT_DMA7_QM:
		fallthrough;
	case GAUDI_EVENT_DMA0_CORE ... GAUDI_EVENT_DMA7_CORE:
		gaudi_print_irq_info(hdev, event_type, true);
		gaudi_handle_qman_err(hdev, event_type);
		hl_fw_unmask_irq(hdev, event_type);
		break;

	case GAUDI_EVENT_RAZWI_OR_ADC_SW:
		gaudi_print_irq_info(hdev, event_type, true);
		if (hdev->hard_reset_on_fw_events)
			hl_device_reset(hdev, true, false);
		break;

	case GAUDI_EVENT_TPC0_BMON_SPMU:
	case GAUDI_EVENT_TPC1_BMON_SPMU:
	case GAUDI_EVENT_TPC2_BMON_SPMU:
	case GAUDI_EVENT_TPC3_BMON_SPMU:
	case GAUDI_EVENT_TPC4_BMON_SPMU:
	case GAUDI_EVENT_TPC5_BMON_SPMU:
	case GAUDI_EVENT_TPC6_BMON_SPMU:
	case GAUDI_EVENT_TPC7_BMON_SPMU:
	case GAUDI_EVENT_DMA_BM_CH0 ... GAUDI_EVENT_DMA_BM_CH7:
		gaudi_print_irq_info(hdev, event_type, false);
		hl_fw_unmask_irq(hdev, event_type);
		break;

	case GAUDI_EVENT_FIX_POWER_ENV_S ... GAUDI_EVENT_FIX_THERMAL_ENV_E:
		gaudi_print_clk_change_info(hdev, event_type);
		hl_fw_unmask_irq(hdev, event_type);
		break;

	case GAUDI_EVENT_PSOC_GPIO_U16_0:
		cause = le64_to_cpu(eq_entry->data[0]) & 0xFF;
		dev_err(hdev->dev,
			"Received high temp H/W interrupt %d (cause %d)\n",
			event_type, cause);
		break;

	default:
		dev_err(hdev->dev, "Received invalid H/W interrupt %d\n",
				event_type);
		break;
	}
}

static void *gaudi_get_events_stat(struct hl_device *hdev, bool aggregate,
					u32 *size)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (aggregate) {
		*size = (u32) sizeof(gaudi->events_stat_aggregate);
		return gaudi->events_stat_aggregate;
	}

	*size = (u32) sizeof(gaudi->events_stat);
	return gaudi->events_stat;
}

static int gaudi_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard,
					u32 flags)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 status, timeout_usec;
	int rc;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MMU) ||
		hdev->hard_reset_pending)
		return 0;

	if (hdev->pldm)
		timeout_usec = GAUDI_PLDM_MMU_TIMEOUT_USEC;
	else
		timeout_usec = MMU_CONFIG_TIMEOUT_USEC;

	mutex_lock(&hdev->mmu_cache_lock);

	/* L0 & L1 invalidation */
	WREG32(mmSTLB_INV_PS, 3);
	WREG32(mmSTLB_CACHE_INV, gaudi->mmu_cache_inv_pi++);
	WREG32(mmSTLB_INV_PS, 2);

	rc = hl_poll_timeout(
		hdev,
		mmSTLB_INV_PS,
		status,
		!status,
		1000,
		timeout_usec);

	WREG32(mmSTLB_INV_SET, 0);

	mutex_unlock(&hdev->mmu_cache_lock);

	if (rc) {
		dev_err_ratelimited(hdev->dev,
					"MMU cache invalidation timeout\n");
		hl_device_reset(hdev, true, false);
	}

	return rc;
}

static int gaudi_mmu_invalidate_cache_range(struct hl_device *hdev,
				bool is_hard, u32 asid, u64 va, u64 size)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 status, timeout_usec;
	u32 inv_data;
	u32 pi;
	int rc;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MMU) ||
		hdev->hard_reset_pending)
		return 0;

	mutex_lock(&hdev->mmu_cache_lock);

	if (hdev->pldm)
		timeout_usec = GAUDI_PLDM_MMU_TIMEOUT_USEC;
	else
		timeout_usec = MMU_CONFIG_TIMEOUT_USEC;

	/*
	 * TODO: currently invalidate entire L0 & L1 as in regular hard
	 * invalidation. Need to apply invalidation of specific cache
	 * lines with mask of ASID & VA & size.
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

	if (rc) {
		dev_err_ratelimited(hdev->dev,
					"MMU cache invalidation timeout\n");
		hl_device_reset(hdev, true, false);
	}

	return rc;
}

static int gaudi_mmu_update_asid_hop0_addr(struct hl_device *hdev,
					u32 asid, u64 phys_addr)
{
	u32 status, timeout_usec;
	int rc;

	if (hdev->pldm)
		timeout_usec = GAUDI_PLDM_MMU_TIMEOUT_USEC;
	else
		timeout_usec = MMU_CONFIG_TIMEOUT_USEC;

	WREG32(MMU_ASID, asid);
	WREG32(MMU_HOP0_PA43_12, phys_addr >> MMU_HOP0_PA43_12_SHIFT);
	WREG32(MMU_HOP0_PA49_44, phys_addr >> MMU_HOP0_PA49_44_SHIFT);
	WREG32(MMU_BUSY, 0x80000000);

	rc = hl_poll_timeout(
		hdev,
		MMU_BUSY,
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

static int gaudi_send_heartbeat(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	return hl_fw_send_heartbeat(hdev);
}

static int gaudi_cpucp_info_get(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	if (!(gaudi->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	rc = hl_fw_cpucp_info_get(hdev);
	if (rc)
		return rc;

	if (!strlen(prop->cpucp_info.card_name))
		strncpy(prop->cpucp_info.card_name, GAUDI_DEFAULT_CARD_NAME,
				CARD_NAME_MAX_LEN);

	hdev->card_type = le32_to_cpu(hdev->asic_prop.cpucp_info.card_type);

	if (hdev->card_type == cpucp_card_type_pci)
		prop->max_power_default = MAX_POWER_DEFAULT_PCI;
	else if (hdev->card_type == cpucp_card_type_pmc)
		prop->max_power_default = MAX_POWER_DEFAULT_PMC;

	hdev->max_power = prop->max_power_default;

	return 0;
}

static bool gaudi_is_device_idle(struct hl_device *hdev, u64 *mask,
					struct seq_file *s)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	const char *fmt = "%-5d%-9s%#-14x%#-12x%#x\n";
	const char *mme_slave_fmt = "%-5d%-9s%-14s%-12s%#x\n";
	u32 qm_glbl_sts0, qm_cgm_sts, dma_core_sts0, tpc_cfg_sts, mme_arch_sts;
	bool is_idle = true, is_eng_idle, is_slave;
	u64 offset;
	int i, dma_id;

	mutex_lock(&gaudi->clk_gate_mutex);

	hdev->asic_funcs->disable_clock_gating(hdev);

	if (s)
		seq_puts(s,
			"\nDMA  is_idle  QM_GLBL_STS0  QM_CGM_STS  DMA_CORE_STS0\n"
			"---  -------  ------------  ----------  -------------\n");

	for (i = 0 ; i < DMA_NUMBER_OF_CHNLS ; i++) {
		dma_id = gaudi_dma_assignment[i];
		offset = dma_id * DMA_QMAN_OFFSET;

		qm_glbl_sts0 = RREG32(mmDMA0_QM_GLBL_STS0 + offset);
		qm_cgm_sts = RREG32(mmDMA0_QM_CGM_STS + offset);
		dma_core_sts0 = RREG32(mmDMA0_CORE_STS0 + offset);
		is_eng_idle = IS_QM_IDLE(qm_glbl_sts0, qm_cgm_sts) &&
				IS_DMA_IDLE(dma_core_sts0);
		is_idle &= is_eng_idle;

		if (mask)
			*mask |= ((u64) !is_eng_idle) <<
					(GAUDI_ENGINE_ID_DMA_0 + dma_id);
		if (s)
			seq_printf(s, fmt, dma_id,
				is_eng_idle ? "Y" : "N", qm_glbl_sts0,
				qm_cgm_sts, dma_core_sts0);
	}

	if (s)
		seq_puts(s,
			"\nTPC  is_idle  QM_GLBL_STS0  QM_CGM_STS  CFG_STATUS\n"
			"---  -------  ------------  ----------  ----------\n");

	for (i = 0 ; i < TPC_NUMBER_OF_ENGINES ; i++) {
		offset = i * TPC_QMAN_OFFSET;
		qm_glbl_sts0 = RREG32(mmTPC0_QM_GLBL_STS0 + offset);
		qm_cgm_sts = RREG32(mmTPC0_QM_CGM_STS + offset);
		tpc_cfg_sts = RREG32(mmTPC0_CFG_STATUS + offset);
		is_eng_idle = IS_QM_IDLE(qm_glbl_sts0, qm_cgm_sts) &&
				IS_TPC_IDLE(tpc_cfg_sts);
		is_idle &= is_eng_idle;

		if (mask)
			*mask |= ((u64) !is_eng_idle) <<
						(GAUDI_ENGINE_ID_TPC_0 + i);
		if (s)
			seq_printf(s, fmt, i,
				is_eng_idle ? "Y" : "N",
				qm_glbl_sts0, qm_cgm_sts, tpc_cfg_sts);
	}

	if (s)
		seq_puts(s,
			"\nMME  is_idle  QM_GLBL_STS0  QM_CGM_STS  ARCH_STATUS\n"
			"---  -------  ------------  ----------  -----------\n");

	for (i = 0 ; i < MME_NUMBER_OF_ENGINES ; i++) {
		offset = i * MME_QMAN_OFFSET;
		mme_arch_sts = RREG32(mmMME0_CTRL_ARCH_STATUS + offset);
		is_eng_idle = IS_MME_IDLE(mme_arch_sts);

		/* MME 1 & 3 are slaves, no need to check their QMANs */
		is_slave = i % 2;
		if (!is_slave) {
			qm_glbl_sts0 = RREG32(mmMME0_QM_GLBL_STS0 + offset);
			qm_cgm_sts = RREG32(mmMME0_QM_CGM_STS + offset);
			is_eng_idle &= IS_QM_IDLE(qm_glbl_sts0, qm_cgm_sts);
		}

		is_idle &= is_eng_idle;

		if (mask)
			*mask |= ((u64) !is_eng_idle) <<
						(GAUDI_ENGINE_ID_MME_0 + i);
		if (s) {
			if (!is_slave)
				seq_printf(s, fmt, i,
					is_eng_idle ? "Y" : "N",
					qm_glbl_sts0, qm_cgm_sts, mme_arch_sts);
			else
				seq_printf(s, mme_slave_fmt, i,
					is_eng_idle ? "Y" : "N", "-",
					"-", mme_arch_sts);
		}
	}

	if (s)
		seq_puts(s, "\n");

	hdev->asic_funcs->set_clock_gating(hdev);

	mutex_unlock(&gaudi->clk_gate_mutex);

	return is_idle;
}

static void gaudi_hw_queues_lock(struct hl_device *hdev)
	__acquires(&gaudi->hw_queues_lock)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	spin_lock(&gaudi->hw_queues_lock);
}

static void gaudi_hw_queues_unlock(struct hl_device *hdev)
	__releases(&gaudi->hw_queues_lock)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	spin_unlock(&gaudi->hw_queues_lock);
}

static u32 gaudi_get_pci_id(struct hl_device *hdev)
{
	return hdev->pdev->device;
}

static int gaudi_get_eeprom_data(struct hl_device *hdev, void *data,
				size_t max_size)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	return hl_fw_get_eeprom_data(hdev, data, max_size);
}

/*
 * this function should be used only during initialization and/or after reset,
 * when there are no active users.
 */
static int gaudi_run_tpc_kernel(struct hl_device *hdev, u64 tpc_kernel,
				u32 tpc_id)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u64 kernel_timeout;
	u32 status, offset;
	int rc;

	offset = tpc_id * (mmTPC1_CFG_STATUS - mmTPC0_CFG_STATUS);

	if (hdev->pldm)
		kernel_timeout = GAUDI_PLDM_TPC_KERNEL_WAIT_USEC;
	else
		kernel_timeout = HL_DEVICE_TIMEOUT_USEC;

	mutex_lock(&gaudi->clk_gate_mutex);

	hdev->asic_funcs->disable_clock_gating(hdev);

	WREG32(mmTPC0_CFG_QM_KERNEL_BASE_ADDRESS_LOW + offset,
			lower_32_bits(tpc_kernel));
	WREG32(mmTPC0_CFG_QM_KERNEL_BASE_ADDRESS_HIGH + offset,
			upper_32_bits(tpc_kernel));

	WREG32(mmTPC0_CFG_ICACHE_BASE_ADDERESS_LOW + offset,
			lower_32_bits(tpc_kernel));
	WREG32(mmTPC0_CFG_ICACHE_BASE_ADDERESS_HIGH + offset,
			upper_32_bits(tpc_kernel));
	/* set a valid LUT pointer, content is of no significance */
	WREG32(mmTPC0_CFG_LUT_FUNC256_BASE_ADDR_LO + offset,
			lower_32_bits(tpc_kernel));
	WREG32(mmTPC0_CFG_LUT_FUNC256_BASE_ADDR_HI + offset,
			upper_32_bits(tpc_kernel));

	WREG32(mmTPC0_CFG_QM_SYNC_OBJECT_ADDR + offset,
			lower_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0));

	WREG32(mmTPC0_CFG_TPC_CMD + offset,
			(1 << TPC0_CFG_TPC_CMD_ICACHE_INVALIDATE_SHIFT |
			1 << TPC0_CFG_TPC_CMD_ICACHE_PREFETCH_64KB_SHIFT));
	/* wait a bit for the engine to start executing */
	usleep_range(1000, 1500);

	/* wait until engine has finished executing */
	rc = hl_poll_timeout(
		hdev,
		mmTPC0_CFG_STATUS + offset,
		status,
		(status & TPC0_CFG_STATUS_VECTOR_PIPE_EMPTY_MASK) ==
				TPC0_CFG_STATUS_VECTOR_PIPE_EMPTY_MASK,
		1000,
		kernel_timeout);

	if (rc) {
		dev_err(hdev->dev,
			"Timeout while waiting for TPC%d icache prefetch\n",
			tpc_id);
		hdev->asic_funcs->set_clock_gating(hdev);
		mutex_unlock(&gaudi->clk_gate_mutex);
		return -EIO;
	}

	WREG32(mmTPC0_CFG_TPC_EXECUTE + offset,
			1 << TPC0_CFG_TPC_EXECUTE_V_SHIFT);

	/* wait a bit for the engine to start executing */
	usleep_range(1000, 1500);

	/* wait until engine has finished executing */
	rc = hl_poll_timeout(
		hdev,
		mmTPC0_CFG_STATUS + offset,
		status,
		(status & TPC0_CFG_STATUS_VECTOR_PIPE_EMPTY_MASK) ==
				TPC0_CFG_STATUS_VECTOR_PIPE_EMPTY_MASK,
		1000,
		kernel_timeout);

	if (rc) {
		dev_err(hdev->dev,
			"Timeout while waiting for TPC%d vector pipe\n",
			tpc_id);
		hdev->asic_funcs->set_clock_gating(hdev);
		mutex_unlock(&gaudi->clk_gate_mutex);
		return -EIO;
	}

	rc = hl_poll_timeout(
		hdev,
		mmTPC0_CFG_WQ_INFLIGHT_CNTR + offset,
		status,
		(status == 0),
		1000,
		kernel_timeout);

	hdev->asic_funcs->set_clock_gating(hdev);
	mutex_unlock(&gaudi->clk_gate_mutex);

	if (rc) {
		dev_err(hdev->dev,
			"Timeout while waiting for TPC%d kernel to execute\n",
			tpc_id);
		return -EIO;
	}

	return 0;
}

static enum hl_device_hw_state gaudi_get_hw_state(struct hl_device *hdev)
{
	return RREG32(mmHW_STATE);
}

static int gaudi_ctx_init(struct hl_ctx *ctx)
{
	return 0;
}

static u32 gaudi_get_queue_id_for_cq(struct hl_device *hdev, u32 cq_idx)
{
	return gaudi_cq_assignment[cq_idx];
}

static u32 gaudi_get_signal_cb_size(struct hl_device *hdev)
{
	return sizeof(struct packet_msg_short) +
			sizeof(struct packet_msg_prot) * 2;
}

static u32 gaudi_get_wait_cb_size(struct hl_device *hdev)
{
	return sizeof(struct packet_msg_short) * 4 +
			sizeof(struct packet_fence) +
			sizeof(struct packet_msg_prot) * 2;
}

static void gaudi_gen_signal_cb(struct hl_device *hdev, void *data, u16 sob_id)
{
	struct hl_cb *cb = (struct hl_cb *) data;
	struct packet_msg_short *pkt;
	u32 value, ctl;

	pkt = cb->kernel_address;
	memset(pkt, 0, sizeof(*pkt));

	/* Inc by 1, Mode ADD */
	value = FIELD_PREP(GAUDI_PKT_SHORT_VAL_SOB_SYNC_VAL_MASK, 1);
	value |= FIELD_PREP(GAUDI_PKT_SHORT_VAL_SOB_MOD_MASK, 1);

	ctl = FIELD_PREP(GAUDI_PKT_SHORT_CTL_ADDR_MASK, sob_id * 4);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_OP_MASK, 0); /* write the value */
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_BASE_MASK, 3); /* W_S SOB base */
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_OPCODE_MASK, PACKET_MSG_SHORT);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_EB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_RB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_MB_MASK, 1);

	pkt->value = cpu_to_le32(value);
	pkt->ctl = cpu_to_le32(ctl);
}

static u32 gaudi_add_mon_msg_short(struct packet_msg_short *pkt, u32 value,
					u16 addr)
{
	u32 ctl, pkt_size = sizeof(*pkt);

	memset(pkt, 0, pkt_size);

	ctl = FIELD_PREP(GAUDI_PKT_SHORT_CTL_ADDR_MASK, addr);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_BASE_MASK, 2);  /* W_S MON base */
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_OPCODE_MASK, PACKET_MSG_SHORT);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_EB_MASK, 0);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_RB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_MB_MASK, 0); /* last pkt MB */

	pkt->value = cpu_to_le32(value);
	pkt->ctl = cpu_to_le32(ctl);

	return pkt_size;
}

static u32 gaudi_add_arm_monitor_pkt(struct packet_msg_short *pkt, u16 sob_id,
					u16 sob_val, u16 addr)
{
	u32 ctl, value, pkt_size = sizeof(*pkt);
	u8 mask = ~(1 << (sob_id & 0x7));

	memset(pkt, 0, pkt_size);

	value = FIELD_PREP(GAUDI_PKT_SHORT_VAL_MON_SYNC_GID_MASK, sob_id / 8);
	value |= FIELD_PREP(GAUDI_PKT_SHORT_VAL_MON_SYNC_VAL_MASK, sob_val);
	value |= FIELD_PREP(GAUDI_PKT_SHORT_VAL_MON_MODE_MASK,
			0); /* GREATER OR EQUAL*/
	value |= FIELD_PREP(GAUDI_PKT_SHORT_VAL_MON_MASK_MASK, mask);

	ctl = FIELD_PREP(GAUDI_PKT_SHORT_CTL_ADDR_MASK, addr);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_OP_MASK, 0); /* write the value */
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_BASE_MASK, 2); /* W_S MON base */
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_OPCODE_MASK, PACKET_MSG_SHORT);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_EB_MASK, 0);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_RB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_MB_MASK, 1);

	pkt->value = cpu_to_le32(value);
	pkt->ctl = cpu_to_le32(ctl);

	return pkt_size;
}

static u32 gaudi_add_fence_pkt(struct packet_fence *pkt)
{
	u32 ctl, cfg, pkt_size = sizeof(*pkt);

	memset(pkt, 0, pkt_size);

	cfg = FIELD_PREP(GAUDI_PKT_FENCE_CFG_DEC_VAL_MASK, 1);
	cfg |= FIELD_PREP(GAUDI_PKT_FENCE_CFG_TARGET_VAL_MASK, 1);
	cfg |= FIELD_PREP(GAUDI_PKT_FENCE_CFG_ID_MASK, 2);

	ctl = FIELD_PREP(GAUDI_PKT_FENCE_CTL_OPCODE_MASK, PACKET_FENCE);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_EB_MASK, 0);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_RB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_MB_MASK, 1);

	pkt->cfg = cpu_to_le32(cfg);
	pkt->ctl = cpu_to_le32(ctl);

	return pkt_size;
}

static void gaudi_gen_wait_cb(struct hl_device *hdev, void *data, u16 sob_id,
			u16 sob_val, u16 mon_id, u32 q_idx)
{
	struct hl_cb *cb = (struct hl_cb *) data;
	void *buf = cb->kernel_address;
	u64 monitor_base, fence_addr = 0;
	u32 size = 0;
	u16 msg_addr_offset;

	switch (q_idx) {
	case GAUDI_QUEUE_ID_DMA_0_0:
		fence_addr = mmDMA0_QM_CP_FENCE2_RDATA_0;
		break;
	case GAUDI_QUEUE_ID_DMA_0_1:
		fence_addr = mmDMA0_QM_CP_FENCE2_RDATA_1;
		break;
	case GAUDI_QUEUE_ID_DMA_0_2:
		fence_addr = mmDMA0_QM_CP_FENCE2_RDATA_2;
		break;
	case GAUDI_QUEUE_ID_DMA_0_3:
		fence_addr = mmDMA0_QM_CP_FENCE2_RDATA_3;
		break;
	case GAUDI_QUEUE_ID_DMA_1_0:
		fence_addr = mmDMA1_QM_CP_FENCE2_RDATA_0;
		break;
	case GAUDI_QUEUE_ID_DMA_1_1:
		fence_addr = mmDMA1_QM_CP_FENCE2_RDATA_1;
		break;
	case GAUDI_QUEUE_ID_DMA_1_2:
		fence_addr = mmDMA1_QM_CP_FENCE2_RDATA_2;
		break;
	case GAUDI_QUEUE_ID_DMA_1_3:
		fence_addr = mmDMA1_QM_CP_FENCE2_RDATA_3;
		break;
	case GAUDI_QUEUE_ID_DMA_5_0:
		fence_addr = mmDMA5_QM_CP_FENCE2_RDATA_0;
		break;
	case GAUDI_QUEUE_ID_DMA_5_1:
		fence_addr = mmDMA5_QM_CP_FENCE2_RDATA_1;
		break;
	case GAUDI_QUEUE_ID_DMA_5_2:
		fence_addr = mmDMA5_QM_CP_FENCE2_RDATA_2;
		break;
	case GAUDI_QUEUE_ID_DMA_5_3:
		fence_addr = mmDMA5_QM_CP_FENCE2_RDATA_3;
		break;
	default:
		/* queue index should be valid here */
		dev_crit(hdev->dev, "wrong queue id %d for wait packet\n",
				q_idx);
		return;
	}

	fence_addr += CFG_BASE;

	/*
	 * monitor_base should be the content of the base0 address registers,
	 * so it will be added to the msg short offsets
	 */
	monitor_base = mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0;

	/* First monitor config packet: low address of the sync */
	msg_addr_offset =
		(mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0 + mon_id * 4) -
				monitor_base;

	size += gaudi_add_mon_msg_short(buf + size, (u32) fence_addr,
					msg_addr_offset);

	/* Second monitor config packet: high address of the sync */
	msg_addr_offset =
		(mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_ADDRH_0 + mon_id * 4) -
				monitor_base;

	size += gaudi_add_mon_msg_short(buf + size, (u32) (fence_addr >> 32),
					msg_addr_offset);

	/*
	 * Third monitor config packet: the payload, i.e. what to write when the
	 * sync triggers
	 */
	msg_addr_offset =
		(mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_DATA_0 + mon_id * 4) -
				monitor_base;

	size += gaudi_add_mon_msg_short(buf + size, 1, msg_addr_offset);

	/* Fourth monitor config packet: bind the monitor to a sync object */
	msg_addr_offset =
		(mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_ARM_0 + mon_id * 4) -
				monitor_base;
	size += gaudi_add_arm_monitor_pkt(buf + size, sob_id, sob_val,
						msg_addr_offset);

	/* Fence packet */
	size += gaudi_add_fence_pkt(buf + size);
}

static void gaudi_reset_sob(struct hl_device *hdev, void *data)
{
	struct hl_hw_sob *hw_sob = (struct hl_hw_sob *) data;

	dev_dbg(hdev->dev, "reset SOB, q_idx: %d, sob_id: %d\n", hw_sob->q_idx,
		hw_sob->sob_id);

	WREG32(mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_SOB_OBJ_0 + hw_sob->sob_id * 4,
		0);

	kref_init(&hw_sob->kref);
}

static void gaudi_set_dma_mask_from_fw(struct hl_device *hdev)
{
	if (RREG32(mmPSOC_GLOBAL_CONF_NON_RST_FLOPS_0) ==
							HL_POWER9_HOST_MAGIC) {
		hdev->power9_64bit_dma_enable = 1;
		hdev->dma_mask = 64;
	} else {
		hdev->power9_64bit_dma_enable = 0;
		hdev->dma_mask = 48;
	}
}

static u64 gaudi_get_device_time(struct hl_device *hdev)
{
	u64 device_time = ((u64) RREG32(mmPSOC_TIMESTAMP_CNTCVU)) << 32;

	return device_time | RREG32(mmPSOC_TIMESTAMP_CNTCVL);
}

static const struct hl_asic_funcs gaudi_funcs = {
	.early_init = gaudi_early_init,
	.early_fini = gaudi_early_fini,
	.late_init = gaudi_late_init,
	.late_fini = gaudi_late_fini,
	.sw_init = gaudi_sw_init,
	.sw_fini = gaudi_sw_fini,
	.hw_init = gaudi_hw_init,
	.hw_fini = gaudi_hw_fini,
	.halt_engines = gaudi_halt_engines,
	.suspend = gaudi_suspend,
	.resume = gaudi_resume,
	.cb_mmap = gaudi_cb_mmap,
	.ring_doorbell = gaudi_ring_doorbell,
	.pqe_write = gaudi_pqe_write,
	.asic_dma_alloc_coherent = gaudi_dma_alloc_coherent,
	.asic_dma_free_coherent = gaudi_dma_free_coherent,
	.get_int_queue_base = gaudi_get_int_queue_base,
	.test_queues = gaudi_test_queues,
	.asic_dma_pool_zalloc = gaudi_dma_pool_zalloc,
	.asic_dma_pool_free = gaudi_dma_pool_free,
	.cpu_accessible_dma_pool_alloc = gaudi_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = gaudi_cpu_accessible_dma_pool_free,
	.hl_dma_unmap_sg = gaudi_dma_unmap_sg,
	.cs_parser = gaudi_cs_parser,
	.asic_dma_map_sg = gaudi_dma_map_sg,
	.get_dma_desc_list_size = gaudi_get_dma_desc_list_size,
	.add_end_of_cb_packets = gaudi_add_end_of_cb_packets,
	.update_eq_ci = gaudi_update_eq_ci,
	.context_switch = gaudi_context_switch,
	.restore_phase_topology = gaudi_restore_phase_topology,
	.debugfs_read32 = gaudi_debugfs_read32,
	.debugfs_write32 = gaudi_debugfs_write32,
	.debugfs_read64 = gaudi_debugfs_read64,
	.debugfs_write64 = gaudi_debugfs_write64,
	.add_device_attr = gaudi_add_device_attr,
	.handle_eqe = gaudi_handle_eqe,
	.set_pll_profile = gaudi_set_pll_profile,
	.get_events_stat = gaudi_get_events_stat,
	.read_pte = gaudi_read_pte,
	.write_pte = gaudi_write_pte,
	.mmu_invalidate_cache = gaudi_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = gaudi_mmu_invalidate_cache_range,
	.send_heartbeat = gaudi_send_heartbeat,
	.set_clock_gating = gaudi_set_clock_gating,
	.disable_clock_gating = gaudi_disable_clock_gating,
	.debug_coresight = gaudi_debug_coresight,
	.is_device_idle = gaudi_is_device_idle,
	.soft_reset_late_init = gaudi_soft_reset_late_init,
	.hw_queues_lock = gaudi_hw_queues_lock,
	.hw_queues_unlock = gaudi_hw_queues_unlock,
	.get_pci_id = gaudi_get_pci_id,
	.get_eeprom_data = gaudi_get_eeprom_data,
	.send_cpu_message = gaudi_send_cpu_message,
	.get_hw_state = gaudi_get_hw_state,
	.pci_bars_map = gaudi_pci_bars_map,
	.init_iatu = gaudi_init_iatu,
	.rreg = hl_rreg,
	.wreg = hl_wreg,
	.halt_coresight = gaudi_halt_coresight,
	.ctx_init = gaudi_ctx_init,
	.get_clk_rate = gaudi_get_clk_rate,
	.get_queue_id_for_cq = gaudi_get_queue_id_for_cq,
	.read_device_fw_version = gaudi_read_device_fw_version,
	.load_firmware_to_device = gaudi_load_firmware_to_device,
	.load_boot_fit_to_device = gaudi_load_boot_fit_to_device,
	.get_signal_cb_size = gaudi_get_signal_cb_size,
	.get_wait_cb_size = gaudi_get_wait_cb_size,
	.gen_signal_cb = gaudi_gen_signal_cb,
	.gen_wait_cb = gaudi_gen_wait_cb,
	.reset_sob = gaudi_reset_sob,
	.set_dma_mask_from_fw = gaudi_set_dma_mask_from_fw,
	.get_device_time = gaudi_get_device_time
};

/**
 * gaudi_set_asic_funcs - set GAUDI function pointers
 *
 * @hdev: pointer to hl_device structure
 *
 */
void gaudi_set_asic_funcs(struct hl_device *hdev)
{
	hdev->asic_funcs = &gaudi_funcs;
}
