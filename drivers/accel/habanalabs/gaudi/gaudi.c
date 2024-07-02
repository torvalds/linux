// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2022 HabanaLabs, Ltd.
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
 * QMAN DMA channels 0,1 (PCI DMAN):
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
 * QMAN DMA 2-7, TPC, MME, NIC:
 * PQ is secured and is located on the Host (HBM CON TPC3 bug)
 * CQ, CP and the engine are not secured
 *
 */

#define GAUDI_BOOT_FIT_FILE	"habanalabs/gaudi/gaudi-boot-fit.itb"
#define GAUDI_LINUX_FW_FILE	"habanalabs/gaudi/gaudi-fit.itb"
#define GAUDI_TPC_FW_FILE	"habanalabs/gaudi/gaudi_tpc.bin"

MODULE_FIRMWARE(GAUDI_BOOT_FIT_FILE);
MODULE_FIRMWARE(GAUDI_LINUX_FW_FILE);
MODULE_FIRMWARE(GAUDI_TPC_FW_FILE);

#define GAUDI_DMA_POOL_BLK_SIZE		0x100 /* 256 bytes */

#define GAUDI_RESET_TIMEOUT_MSEC	2000		/* 2000ms */
#define GAUDI_RESET_WAIT_MSEC		1		/* 1ms */
#define GAUDI_CPU_RESET_WAIT_MSEC	200		/* 200ms */
#define GAUDI_TEST_QUEUE_WAIT_USEC	100000		/* 100ms */

#define GAUDI_PLDM_RESET_WAIT_MSEC	1000		/* 1s */
#define GAUDI_PLDM_HRESET_TIMEOUT_MSEC	20000		/* 20s */
#define GAUDI_PLDM_TEST_QUEUE_WAIT_USEC	1000000		/* 1s */
#define GAUDI_PLDM_MMU_TIMEOUT_USEC	(MMU_CONFIG_TIMEOUT_USEC * 100)
#define GAUDI_PLDM_QMAN0_TIMEOUT_USEC	(HL_DEVICE_TIMEOUT_USEC * 30)
#define GAUDI_PLDM_TPC_KERNEL_WAIT_USEC	(HL_DEVICE_TIMEOUT_USEC * 30)
#define GAUDI_BOOT_FIT_REQ_TIMEOUT_USEC	4000000		/* 4s */
#define GAUDI_MSG_TO_CPU_TIMEOUT_USEC	4000000		/* 4s */
#define GAUDI_WAIT_FOR_BL_TIMEOUT_USEC	15000000	/* 15s */

#define GAUDI_QMAN0_FENCE_VAL		0x72E91AB9

#define GAUDI_MAX_STRING_LEN		20

#define GAUDI_CB_POOL_CB_CNT		512
#define GAUDI_CB_POOL_CB_SIZE		0x20000 /* 128KB */

#define GAUDI_ALLOC_CPU_MEM_RETRY_CNT	3

#define GAUDI_NUM_OF_TPC_INTR_CAUSE	20

#define GAUDI_NUM_OF_QM_ERR_CAUSE	16

#define GAUDI_NUM_OF_QM_ARB_ERR_CAUSE	3

#define GAUDI_ARB_WDT_TIMEOUT		0xEE6b27FF /* 8 seconds */

#define HBM_SCRUBBING_TIMEOUT_US	1000000 /* 1s */

#define BIN_REG_STRING_SIZE	sizeof("0b10101010101010101010101010101010")

#define MONITOR_SOB_STRING_SIZE		256

static u32 gaudi_stream_master[GAUDI_STREAM_MASTER_ARR_SIZE] = {
	GAUDI_QUEUE_ID_DMA_0_0,
	GAUDI_QUEUE_ID_DMA_0_1,
	GAUDI_QUEUE_ID_DMA_0_2,
	GAUDI_QUEUE_ID_DMA_0_3,
	GAUDI_QUEUE_ID_DMA_1_0,
	GAUDI_QUEUE_ID_DMA_1_1,
	GAUDI_QUEUE_ID_DMA_1_2,
	GAUDI_QUEUE_ID_DMA_1_3
};

static const u8 gaudi_dma_assignment[GAUDI_DMA_MAX] = {
	[GAUDI_PCI_DMA_1] = GAUDI_ENGINE_ID_DMA_0,
	[GAUDI_PCI_DMA_2] = GAUDI_ENGINE_ID_DMA_1,
	[GAUDI_HBM_DMA_1] = GAUDI_ENGINE_ID_DMA_2,
	[GAUDI_HBM_DMA_2] = GAUDI_ENGINE_ID_DMA_3,
	[GAUDI_HBM_DMA_3] = GAUDI_ENGINE_ID_DMA_4,
	[GAUDI_HBM_DMA_4] = GAUDI_ENGINE_ID_DMA_5,
	[GAUDI_HBM_DMA_5] = GAUDI_ENGINE_ID_DMA_6,
	[GAUDI_HBM_DMA_6] = GAUDI_ENGINE_ID_DMA_7
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
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_5_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_5_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_5_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_DMA_5_3 */
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
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_0_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_0_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_0_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_0_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_1_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_1_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_1_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_1_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_2_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_2_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_2_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_2_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_3_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_3_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_3_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_3_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_4_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_4_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_4_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_4_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_5_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_5_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_5_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_5_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_6_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_6_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_6_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_6_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_7_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_7_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_7_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_7_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_8_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_8_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_8_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_8_3 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_9_0 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_9_1 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_9_2 */
	QUEUE_TYPE_INT, /* GAUDI_QUEUE_ID_NIC_9_3 */
};

static struct hl_hw_obj_name_entry gaudi_so_id_to_str[] = {
	{ .id = 0,  .name = "SYNC_OBJ_DMA_DOWN_FEEDBACK" },
	{ .id = 1,  .name = "SYNC_OBJ_DMA_UP_FEEDBACK" },
	{ .id = 2,  .name = "SYNC_OBJ_DMA_STATIC_DRAM_SRAM_FEEDBACK" },
	{ .id = 3,  .name = "SYNC_OBJ_DMA_SRAM_DRAM_FEEDBACK" },
	{ .id = 4,  .name = "SYNC_OBJ_FIRST_COMPUTE_FINISH" },
	{ .id = 5,  .name = "SYNC_OBJ_HOST_DRAM_DONE" },
	{ .id = 6,  .name = "SYNC_OBJ_DBG_CTR_DEPRECATED" },
	{ .id = 7,  .name = "SYNC_OBJ_DMA_ACTIVATIONS_DRAM_SRAM_FEEDBACK" },
	{ .id = 8,  .name = "SYNC_OBJ_ENGINE_SEM_MME_0" },
	{ .id = 9,  .name = "SYNC_OBJ_ENGINE_SEM_MME_1" },
	{ .id = 10, .name = "SYNC_OBJ_ENGINE_SEM_TPC_0" },
	{ .id = 11, .name = "SYNC_OBJ_ENGINE_SEM_TPC_1" },
	{ .id = 12, .name = "SYNC_OBJ_ENGINE_SEM_TPC_2" },
	{ .id = 13, .name = "SYNC_OBJ_ENGINE_SEM_TPC_3" },
	{ .id = 14, .name = "SYNC_OBJ_ENGINE_SEM_TPC_4" },
	{ .id = 15, .name = "SYNC_OBJ_ENGINE_SEM_TPC_5" },
	{ .id = 16, .name = "SYNC_OBJ_ENGINE_SEM_TPC_6" },
	{ .id = 17, .name = "SYNC_OBJ_ENGINE_SEM_TPC_7" },
	{ .id = 18, .name = "SYNC_OBJ_ENGINE_SEM_DMA_1" },
	{ .id = 19, .name = "SYNC_OBJ_ENGINE_SEM_DMA_2" },
	{ .id = 20, .name = "SYNC_OBJ_ENGINE_SEM_DMA_3" },
	{ .id = 21, .name = "SYNC_OBJ_ENGINE_SEM_DMA_4" },
	{ .id = 22, .name = "SYNC_OBJ_ENGINE_SEM_DMA_5" },
	{ .id = 23, .name = "SYNC_OBJ_ENGINE_SEM_DMA_6" },
	{ .id = 24, .name = "SYNC_OBJ_ENGINE_SEM_DMA_7" },
	{ .id = 25, .name = "SYNC_OBJ_DBG_CTR_0" },
	{ .id = 26, .name = "SYNC_OBJ_DBG_CTR_1" },
};

static struct hl_hw_obj_name_entry gaudi_monitor_id_to_str[] = {
	{ .id = 200, .name = "MON_OBJ_DMA_DOWN_FEEDBACK_RESET" },
	{ .id = 201, .name = "MON_OBJ_DMA_UP_FEEDBACK_RESET" },
	{ .id = 203, .name = "MON_OBJ_DRAM_TO_SRAM_QUEUE_FENCE" },
	{ .id = 204, .name = "MON_OBJ_TPC_0_CLK_GATE" },
	{ .id = 205, .name = "MON_OBJ_TPC_1_CLK_GATE" },
	{ .id = 206, .name = "MON_OBJ_TPC_2_CLK_GATE" },
	{ .id = 207, .name = "MON_OBJ_TPC_3_CLK_GATE" },
	{ .id = 208, .name = "MON_OBJ_TPC_4_CLK_GATE" },
	{ .id = 209, .name = "MON_OBJ_TPC_5_CLK_GATE" },
	{ .id = 210, .name = "MON_OBJ_TPC_6_CLK_GATE" },
	{ .id = 211, .name = "MON_OBJ_TPC_7_CLK_GATE" },
};

static s64 gaudi_state_dump_specs_props[] = {
	[SP_SYNC_OBJ_BASE_ADDR] = mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0,
	[SP_NEXT_SYNC_OBJ_ADDR] = NEXT_SYNC_OBJ_ADDR_INTERVAL,
	[SP_SYNC_OBJ_AMOUNT] = NUM_OF_SOB_IN_BLOCK,
	[SP_MON_OBJ_WR_ADDR_LOW] =
		mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0,
	[SP_MON_OBJ_WR_ADDR_HIGH] =
		mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRH_0,
	[SP_MON_OBJ_WR_DATA] = mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_DATA_0,
	[SP_MON_OBJ_ARM_DATA] = mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_ARM_0,
	[SP_MON_OBJ_STATUS] = mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_STATUS_0,
	[SP_MONITORS_AMOUNT] = NUM_OF_MONITORS_IN_BLOCK,
	[SP_TPC0_CMDQ] = mmTPC0_QM_GLBL_CFG0,
	[SP_TPC0_CFG_SO] = mmTPC0_CFG_QM_SYNC_OBJECT_ADDR,
	[SP_NEXT_TPC] = mmTPC1_QM_GLBL_CFG0 - mmTPC0_QM_GLBL_CFG0,
	[SP_MME_CMDQ] = mmMME0_QM_GLBL_CFG0,
	[SP_MME_CFG_SO] = mmMME0_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL,
	[SP_NEXT_MME] = mmMME2_QM_GLBL_CFG0 - mmMME0_QM_GLBL_CFG0,
	[SP_DMA_CMDQ] = mmDMA0_QM_GLBL_CFG0,
	[SP_DMA_CFG_SO] = mmDMA0_CORE_WR_COMP_ADDR_LO,
	[SP_DMA_QUEUES_OFFSET] = mmDMA1_QM_GLBL_CFG0 - mmDMA0_QM_GLBL_CFG0,
	[SP_NUM_OF_MME_ENGINES] = NUM_OF_MME_ENGINES,
	[SP_SUB_MME_ENG_NUM] = NUM_OF_MME_SUB_ENGINES,
	[SP_NUM_OF_DMA_ENGINES] = NUM_OF_DMA_ENGINES,
	[SP_NUM_OF_TPC_ENGINES] = NUM_OF_TPC_ENGINES,
	[SP_ENGINE_NUM_OF_QUEUES] = NUM_OF_QUEUES,
	[SP_ENGINE_NUM_OF_STREAMS] = NUM_OF_STREAMS,
	[SP_ENGINE_NUM_OF_FENCES] = NUM_OF_FENCES,
	[SP_FENCE0_CNT_OFFSET] =
		mmDMA0_QM_CP_FENCE0_CNT_0 - mmDMA0_QM_GLBL_CFG0,
	[SP_FENCE0_RDATA_OFFSET] =
		mmDMA0_QM_CP_FENCE0_RDATA_0 - mmDMA0_QM_GLBL_CFG0,
	[SP_CP_STS_OFFSET] = mmDMA0_QM_CP_STS_0 - mmDMA0_QM_GLBL_CFG0,
	[SP_NUM_CORES] = 1,
};

static const int gaudi_queue_id_to_engine_id[] = {
	[GAUDI_QUEUE_ID_DMA_0_0...GAUDI_QUEUE_ID_DMA_0_3] = GAUDI_ENGINE_ID_DMA_0,
	[GAUDI_QUEUE_ID_DMA_1_0...GAUDI_QUEUE_ID_DMA_1_3] = GAUDI_ENGINE_ID_DMA_1,
	[GAUDI_QUEUE_ID_CPU_PQ] = GAUDI_ENGINE_ID_SIZE,
	[GAUDI_QUEUE_ID_DMA_2_0...GAUDI_QUEUE_ID_DMA_2_3] = GAUDI_ENGINE_ID_DMA_2,
	[GAUDI_QUEUE_ID_DMA_3_0...GAUDI_QUEUE_ID_DMA_3_3] = GAUDI_ENGINE_ID_DMA_3,
	[GAUDI_QUEUE_ID_DMA_4_0...GAUDI_QUEUE_ID_DMA_4_3] = GAUDI_ENGINE_ID_DMA_4,
	[GAUDI_QUEUE_ID_DMA_5_0...GAUDI_QUEUE_ID_DMA_5_3] = GAUDI_ENGINE_ID_DMA_5,
	[GAUDI_QUEUE_ID_DMA_6_0...GAUDI_QUEUE_ID_DMA_6_3] = GAUDI_ENGINE_ID_DMA_6,
	[GAUDI_QUEUE_ID_DMA_7_0...GAUDI_QUEUE_ID_DMA_7_3] = GAUDI_ENGINE_ID_DMA_7,
	[GAUDI_QUEUE_ID_MME_0_0...GAUDI_QUEUE_ID_MME_0_3] = GAUDI_ENGINE_ID_MME_0,
	[GAUDI_QUEUE_ID_MME_1_0...GAUDI_QUEUE_ID_MME_1_3] = GAUDI_ENGINE_ID_MME_2,
	[GAUDI_QUEUE_ID_TPC_0_0...GAUDI_QUEUE_ID_TPC_0_3] = GAUDI_ENGINE_ID_TPC_0,
	[GAUDI_QUEUE_ID_TPC_1_0...GAUDI_QUEUE_ID_TPC_1_3] = GAUDI_ENGINE_ID_TPC_1,
	[GAUDI_QUEUE_ID_TPC_2_0...GAUDI_QUEUE_ID_TPC_2_3] = GAUDI_ENGINE_ID_TPC_2,
	[GAUDI_QUEUE_ID_TPC_3_0...GAUDI_QUEUE_ID_TPC_3_3] = GAUDI_ENGINE_ID_TPC_3,
	[GAUDI_QUEUE_ID_TPC_4_0...GAUDI_QUEUE_ID_TPC_4_3] = GAUDI_ENGINE_ID_TPC_4,
	[GAUDI_QUEUE_ID_TPC_5_0...GAUDI_QUEUE_ID_TPC_5_3] = GAUDI_ENGINE_ID_TPC_5,
	[GAUDI_QUEUE_ID_TPC_6_0...GAUDI_QUEUE_ID_TPC_6_3] = GAUDI_ENGINE_ID_TPC_6,
	[GAUDI_QUEUE_ID_TPC_7_0...GAUDI_QUEUE_ID_TPC_7_3] = GAUDI_ENGINE_ID_TPC_7,
	[GAUDI_QUEUE_ID_NIC_0_0...GAUDI_QUEUE_ID_NIC_0_3] = GAUDI_ENGINE_ID_NIC_0,
	[GAUDI_QUEUE_ID_NIC_1_0...GAUDI_QUEUE_ID_NIC_1_3] = GAUDI_ENGINE_ID_NIC_1,
	[GAUDI_QUEUE_ID_NIC_2_0...GAUDI_QUEUE_ID_NIC_2_3] = GAUDI_ENGINE_ID_NIC_2,
	[GAUDI_QUEUE_ID_NIC_3_0...GAUDI_QUEUE_ID_NIC_3_3] = GAUDI_ENGINE_ID_NIC_3,
	[GAUDI_QUEUE_ID_NIC_4_0...GAUDI_QUEUE_ID_NIC_4_3] = GAUDI_ENGINE_ID_NIC_4,
	[GAUDI_QUEUE_ID_NIC_5_0...GAUDI_QUEUE_ID_NIC_5_3] = GAUDI_ENGINE_ID_NIC_5,
	[GAUDI_QUEUE_ID_NIC_6_0...GAUDI_QUEUE_ID_NIC_6_3] = GAUDI_ENGINE_ID_NIC_6,
	[GAUDI_QUEUE_ID_NIC_7_0...GAUDI_QUEUE_ID_NIC_7_3] = GAUDI_ENGINE_ID_NIC_7,
	[GAUDI_QUEUE_ID_NIC_8_0...GAUDI_QUEUE_ID_NIC_8_3] = GAUDI_ENGINE_ID_NIC_8,
	[GAUDI_QUEUE_ID_NIC_9_0...GAUDI_QUEUE_ID_NIC_9_3] = GAUDI_ENGINE_ID_NIC_9,
};

/* The order here is opposite to the order of the indexing in the h/w.
 * i.e. SYNC_MGR_W_S is actually 0, SYNC_MGR_E_S is 1, etc.
 */
static const char * const gaudi_sync_manager_names[] = {
	"SYNC_MGR_E_N",
	"SYNC_MGR_W_N",
	"SYNC_MGR_E_S",
	"SYNC_MGR_W_S",
	NULL
};

struct ecc_info_extract_params {
	u64 block_address;
	u32 num_memories;
	bool derr;
};

static int gaudi_mmu_update_asid_hop0_addr(struct hl_device *hdev, u32 asid,
								u64 phys_addr);
static int gaudi_send_job_on_qman0(struct hl_device *hdev,
					struct hl_cs_job *job);
static int gaudi_memset_device_memory(struct hl_device *hdev, u64 addr,
					u32 size, u64 val);
static int gaudi_memset_registers(struct hl_device *hdev, u64 reg_base,
					u32 num_regs, u32 val);
static int gaudi_run_tpc_kernel(struct hl_device *hdev, u64 tpc_kernel,
				u32 tpc_id);
static int gaudi_mmu_clear_pgt_range(struct hl_device *hdev);
static int gaudi_cpucp_info_get(struct hl_device *hdev);
static void gaudi_disable_clock_gating(struct hl_device *hdev);
static void gaudi_mmu_prepare(struct hl_device *hdev, u32 asid);
static u32 gaudi_gen_signal_cb(struct hl_device *hdev, void *data, u16 sob_id,
				u32 size, bool eb);
static u32 gaudi_gen_wait_cb(struct hl_device *hdev,
				struct hl_gen_wait_properties *prop);
static inline enum hl_collective_mode
get_collective_mode(struct hl_device *hdev, u32 queue_id)
{
	if (gaudi_queue_type[queue_id] == QUEUE_TYPE_EXT)
		return HL_COLLECTIVE_MASTER;

	if (queue_id >= GAUDI_QUEUE_ID_DMA_5_0 &&
			queue_id <= GAUDI_QUEUE_ID_DMA_5_3)
		return HL_COLLECTIVE_SLAVE;

	if (queue_id >= GAUDI_QUEUE_ID_TPC_7_0 &&
			queue_id <= GAUDI_QUEUE_ID_TPC_7_3)
		return HL_COLLECTIVE_SLAVE;

	if (queue_id >= GAUDI_QUEUE_ID_NIC_0_0 &&
			queue_id <= GAUDI_QUEUE_ID_NIC_9_3)
		return HL_COLLECTIVE_SLAVE;

	return HL_COLLECTIVE_NOT_SUPPORTED;
}

static inline void set_default_power_values(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	if (hdev->card_type == cpucp_card_type_pmc) {
		prop->max_power_default = MAX_POWER_DEFAULT_PMC;

		if (prop->fw_security_enabled)
			prop->dc_power_default = DC_POWER_DEFAULT_PMC_SEC;
		else
			prop->dc_power_default = DC_POWER_DEFAULT_PMC;
	} else {
		prop->max_power_default = MAX_POWER_DEFAULT_PCI;
		prop->dc_power_default = DC_POWER_DEFAULT_PCI;
	}
}

static int gaudi_set_fixed_properties(struct hl_device *hdev)
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
			prop->hw_queues_props[i].supports_sync_stream = 1;
			prop->hw_queues_props[i].cb_alloc_flags =
				CB_ALLOC_KERNEL;
			num_sync_stream_queues++;
		} else if (gaudi_queue_type[i] == QUEUE_TYPE_CPU) {
			prop->hw_queues_props[i].type = QUEUE_TYPE_CPU;
			prop->hw_queues_props[i].driver_only = 1;
			prop->hw_queues_props[i].supports_sync_stream = 0;
			prop->hw_queues_props[i].cb_alloc_flags =
				CB_ALLOC_KERNEL;
		} else if (gaudi_queue_type[i] == QUEUE_TYPE_INT) {
			prop->hw_queues_props[i].type = QUEUE_TYPE_INT;
			prop->hw_queues_props[i].driver_only = 0;
			prop->hw_queues_props[i].supports_sync_stream = 0;
			prop->hw_queues_props[i].cb_alloc_flags =
				CB_ALLOC_USER;

		}
		prop->hw_queues_props[i].collective_mode =
						get_collective_mode(hdev, i);
	}

	prop->cache_line_size = DEVICE_CACHE_LINE_SIZE;
	prop->cfg_base_address = CFG_BASE;
	prop->device_dma_offset_for_host_access = HOST_PHYS_BASE;
	prop->host_base_address = HOST_PHYS_BASE;
	prop->host_end_address = prop->host_base_address + HOST_PHYS_SIZE;
	prop->completion_queues_count = NUMBER_OF_CMPLT_QUEUES;
	prop->completion_mode = HL_COMPLETION_MODE_JOB;
	prop->collective_first_sob = 0;
	prop->collective_first_mon = 0;

	/* 2 SOBs per internal queue stream are reserved for collective */
	prop->sync_stream_first_sob =
			ALIGN(NUMBER_OF_SOBS_IN_GRP, HL_MAX_SOBS_PER_MONITOR)
			* QMAN_STREAMS * HL_RSVD_SOBS;

	/* 1 monitor per internal queue stream are reserved for collective
	 * 2 monitors per external queue stream are reserved for collective
	 */
	prop->sync_stream_first_mon =
			(NUMBER_OF_COLLECTIVE_QUEUES * QMAN_STREAMS) +
			(NUMBER_OF_EXT_HW_QUEUES * 2);

	prop->dram_base_address = DRAM_PHYS_BASE;
	prop->dram_size = GAUDI_HBM_SIZE_32GB;
	prop->dram_end_address = prop->dram_base_address + prop->dram_size;
	prop->dram_user_base_address = DRAM_BASE_ADDR_USER;

	prop->sram_base_address = SRAM_BASE_ADDR;
	prop->sram_size = SRAM_SIZE;
	prop->sram_end_address = prop->sram_base_address + prop->sram_size;
	prop->sram_user_base_address =
			prop->sram_base_address + SRAM_USER_BASE_OFFSET;

	prop->mmu_cache_mng_addr = MMU_CACHE_MNG_ADDR;
	prop->mmu_cache_mng_size = MMU_CACHE_MNG_SIZE;

	prop->mmu_pgt_addr = MMU_PAGE_TABLES_ADDR;
	if (hdev->pldm)
		prop->mmu_pgt_size = 0x800000; /* 8MB */
	else
		prop->mmu_pgt_size = MMU_PAGE_TABLES_SIZE;
	prop->mmu_pte_size = HL_PTE_SIZE;
	prop->dram_page_size = PAGE_SIZE_2MB;
	prop->device_mem_alloc_default_page_size = prop->dram_page_size;
	prop->dram_supports_virtual_memory = false;

	prop->pmmu.hop_shifts[MMU_HOP0] = MMU_V1_1_HOP0_SHIFT;
	prop->pmmu.hop_shifts[MMU_HOP1] = MMU_V1_1_HOP1_SHIFT;
	prop->pmmu.hop_shifts[MMU_HOP2] = MMU_V1_1_HOP2_SHIFT;
	prop->pmmu.hop_shifts[MMU_HOP3] = MMU_V1_1_HOP3_SHIFT;
	prop->pmmu.hop_shifts[MMU_HOP4] = MMU_V1_1_HOP4_SHIFT;
	prop->pmmu.hop_masks[MMU_HOP0] = MMU_V1_1_HOP0_MASK;
	prop->pmmu.hop_masks[MMU_HOP1] = MMU_V1_1_HOP1_MASK;
	prop->pmmu.hop_masks[MMU_HOP2] = MMU_V1_1_HOP2_MASK;
	prop->pmmu.hop_masks[MMU_HOP3] = MMU_V1_1_HOP3_MASK;
	prop->pmmu.hop_masks[MMU_HOP4] = MMU_V1_1_HOP4_MASK;
	prop->pmmu.start_addr = VA_HOST_SPACE_START;
	prop->pmmu.end_addr =
			(VA_HOST_SPACE_START + VA_HOST_SPACE_SIZE / 2) - 1;
	prop->pmmu.page_size = PAGE_SIZE_4KB;
	prop->pmmu.num_hops = MMU_ARCH_5_HOPS;
	prop->pmmu.last_mask = LAST_MASK;
	/* TODO: will be duplicated until implementing per-MMU props */
	prop->pmmu.hop_table_size = HOP_TABLE_SIZE_512_PTE;
	prop->pmmu.hop0_tables_total_size = HOP0_512_PTE_TABLES_TOTAL_SIZE;

	/* PMMU and HPMMU are the same except of page size */
	memcpy(&prop->pmmu_huge, &prop->pmmu, sizeof(prop->pmmu));
	prop->pmmu_huge.page_size = PAGE_SIZE_2MB;

	/* shifts and masks are the same in PMMU and DMMU */
	memcpy(&prop->dmmu, &prop->pmmu, sizeof(prop->pmmu));
	prop->dmmu.start_addr = (VA_HOST_SPACE_START + VA_HOST_SPACE_SIZE / 2);
	prop->dmmu.end_addr = VA_HOST_SPACE_END;
	prop->dmmu.page_size = PAGE_SIZE_2MB;
	prop->dmmu.pgt_size = prop->mmu_pgt_size;

	prop->cfg_size = CFG_SIZE;
	prop->max_asid = MAX_ASID;
	prop->num_of_events = GAUDI_EVENT_SIZE;
	prop->max_num_of_engines = GAUDI_ENGINE_ID_SIZE;
	prop->tpc_enabled_mask = TPC_ENABLED_MASK;

	set_default_power_values(hdev);

	prop->cb_pool_cb_cnt = GAUDI_CB_POOL_CB_CNT;
	prop->cb_pool_cb_size = GAUDI_CB_POOL_CB_SIZE;

	prop->pcie_dbi_base_address = mmPCIE_DBI_BASE;
	prop->pcie_aux_dbi_reg_addr = CFG_BASE + mmPCIE_AUX_DBI;

	strscpy_pad(prop->cpucp_info.card_name, GAUDI_DEFAULT_CARD_NAME,
					CARD_NAME_MAX_LEN);

	prop->max_pending_cs = GAUDI_MAX_PENDING_CS;

	prop->first_available_user_sob[HL_GAUDI_WS_DCORE] =
			prop->sync_stream_first_sob +
			(num_sync_stream_queues * HL_RSVD_SOBS);
	prop->first_available_user_mon[HL_GAUDI_WS_DCORE] =
			prop->sync_stream_first_mon +
			(num_sync_stream_queues * HL_RSVD_MONS);

	prop->first_available_user_interrupt = USHRT_MAX;
	prop->tpc_interrupt_id = USHRT_MAX;

	/* single msi */
	prop->eq_interrupt_id = 0;

	for (i = 0 ; i < HL_MAX_DCORES ; i++)
		prop->first_available_cq[i] = USHRT_MAX;

	prop->fw_cpu_boot_dev_sts0_valid = false;
	prop->fw_cpu_boot_dev_sts1_valid = false;
	prop->hard_reset_done_by_fw = false;
	prop->gic_interrupts_enable = true;

	prop->server_type = HL_SERVER_TYPE_UNKNOWN;

	prop->clk_pll_index = HL_GAUDI_MME_PLL;
	prop->max_freq_value = GAUDI_MAX_CLK_FREQ;

	prop->use_get_power_for_reset_history = true;

	prop->configurable_stop_on_err = true;

	prop->set_max_power_on_device_init = true;

	prop->dma_mask = 48;

	prop->hbw_flush_reg = mmPCIE_WRAP_RR_ELBI_RD_SEC_REG_CTRL;

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

	if (hdev->asic_prop.iatu_done_by_fw)
		return U64_MAX;

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

	if (hdev->asic_prop.iatu_done_by_fw)
		return 0;

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

	/* Outbound Region 0 - Point to Host */
	outbound_region.addr = HOST_PHYS_BASE;
	outbound_region.size = HOST_PHYS_SIZE;
	rc = hl_pci_set_outbound_region(hdev, &outbound_region);

done:
	return rc;
}

static enum hl_device_hw_state gaudi_get_hw_state(struct hl_device *hdev)
{
	return RREG32(mmHW_STATE);
}

static int gaudi_early_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pci_dev *pdev = hdev->pdev;
	resource_size_t pci_bar_size;
	u32 fw_boot_status;
	int rc;

	rc = gaudi_set_fixed_properties(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed setting fixed properties\n");
		return rc;
	}

	/* Check BAR sizes */
	pci_bar_size = pci_resource_len(pdev, SRAM_BAR_ID);

	if (pci_bar_size != SRAM_BAR_SIZE) {
		dev_err(hdev->dev, "Not " HL_NAME "? BAR %d size %pa, expecting %llu\n",
			SRAM_BAR_ID, &pci_bar_size, SRAM_BAR_SIZE);
		rc = -ENODEV;
		goto free_queue_props;
	}

	pci_bar_size = pci_resource_len(pdev, CFG_BAR_ID);

	if (pci_bar_size != CFG_BAR_SIZE) {
		dev_err(hdev->dev, "Not " HL_NAME "? BAR %d size %pa, expecting %llu\n",
			CFG_BAR_ID, &pci_bar_size, CFG_BAR_SIZE);
		rc = -ENODEV;
		goto free_queue_props;
	}

	prop->dram_pci_bar_size = pci_resource_len(pdev, HBM_BAR_ID);
	hdev->dram_pci_bar_start = pci_resource_start(pdev, HBM_BAR_ID);

	/* If FW security is enabled at this point it means no access to ELBI */
	if (hdev->asic_prop.fw_security_enabled) {
		hdev->asic_prop.iatu_done_by_fw = true;

		/*
		 * GIC-security-bit can ONLY be set by CPUCP, so in this stage
		 * decision can only be taken based on PCI ID security.
		 */
		hdev->asic_prop.gic_interrupts_enable = false;
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
	rc = hl_fw_read_preboot_status(hdev);
	if (rc) {
		if (hdev->reset_on_preboot_fail)
			/* we are already on failure flow, so don't check if hw_fini fails. */
			hdev->asic_funcs->hw_fini(hdev, true, false);
		goto pci_fini;
	}

	if (gaudi_get_hw_state(hdev) == HL_DEVICE_HW_STATE_DIRTY) {
		dev_dbg(hdev->dev, "H/W state is dirty, must reset before initializing\n");
		rc = hdev->asic_funcs->hw_fini(hdev, true, false);
		if (rc) {
			dev_err(hdev->dev, "failed to reset HW in dirty state (%d)\n", rc);
			goto pci_fini;
		}
	}

	return 0;

pci_fini:
	hl_pci_fini(hdev);
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
static int gaudi_fetch_psoc_frequency(struct hl_device *hdev)
{
	u32 nr = 0, nf = 0, od = 0, div_fctr = 0, pll_clk, div_sel;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u16 pll_freq_arr[HL_PLL_NUM_OUTPUTS], freq;
	int rc;

	if ((hdev->fw_components & FW_TYPE_LINUX) &&
			(prop->fw_app_cpu_boot_dev_sts0 & CPU_BOOT_DEV_STS0_PLL_INFO_EN)) {
		struct gaudi_device *gaudi = hdev->asic_specific;

		if (!(gaudi->hw_cap_initialized & HW_CAP_CPU_Q))
			return 0;

		rc = hl_fw_cpucp_pll_info_get(hdev, HL_GAUDI_CPU_PLL, pll_freq_arr);

		if (rc)
			return rc;

		freq = pll_freq_arr[2];
	} else {
		/* Backward compatibility */
		div_fctr = RREG32(mmPSOC_CPU_PLL_DIV_FACTOR_2);
		div_sel = RREG32(mmPSOC_CPU_PLL_DIV_SEL_2);
		nr = RREG32(mmPSOC_CPU_PLL_NR);
		nf = RREG32(mmPSOC_CPU_PLL_NF);
		od = RREG32(mmPSOC_CPU_PLL_OD);

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
			dev_warn(hdev->dev, "Received invalid div select value: %#x", div_sel);
			freq = 0;
		}
	}

	prop->psoc_timestamp_frequency = freq;
	prop->psoc_pci_pll_nr = nr;
	prop->psoc_pci_pll_nf = nf;
	prop->psoc_pci_pll_od = od;
	prop->psoc_pci_pll_div_factor = div_fctr;

	return 0;
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

	/* TPC_CMD is configured with I$ prefetch enabled, so address should be aligned to 8KB */
	dst_addr = FIELD_PREP(GAUDI_PKT_LIN_DMA_DST_ADDR_MASK,
				round_up(prop->sram_user_base_address, SZ_8K));
	init_tpc_mem_pkt->dst_addr |= cpu_to_le64(dst_addr);

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
	atomic_dec(&cb->cs_cnt);

release_cb:
	hl_cb_put(cb);
	hl_cb_destroy(&hdev->kernel_mem_mgr, cb->buf->handle);

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
	int rc, count = 5;

again:
	rc = request_firmware(&fw, GAUDI_TPC_FW_FILE, hdev->dev);
	if (rc == -EINTR && count-- > 0) {
		msleep(50);
		goto again;
	}

	if (rc) {
		dev_err(hdev->dev, "Failed to load firmware file %s\n",
				GAUDI_TPC_FW_FILE);
		goto out;
	}

	fw_size = fw->size;
	cpu_addr = hl_asic_dma_alloc_coherent(hdev, fw_size, &dma_handle, GFP_KERNEL | __GFP_ZERO);
	if (!cpu_addr) {
		dev_err(hdev->dev,
			"Failed to allocate %zu of dma memory for TPC kernel\n",
			fw_size);
		rc = -ENOMEM;
		goto out;
	}

	memcpy(cpu_addr, fw->data, fw_size);

	rc = _gaudi_init_tpc_mem(hdev, dma_handle, fw_size);

	hl_asic_dma_free_coherent(hdev, fw->size, cpu_addr, dma_handle);

out:
	release_firmware(fw);
	return rc;
}

static void gaudi_collective_map_sobs(struct hl_device *hdev, u32 stream)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_collective_properties *prop = &gaudi->collective_props;
	struct hl_hw_queue *q;
	u32 i, sob_id, sob_group_id, queue_id;

	/* Iterate through SOB groups and assign a SOB for each slave queue */
	sob_group_id =
		stream * HL_RSVD_SOBS + prop->curr_sob_group_idx[stream];
	sob_id = prop->hw_sob_group[sob_group_id].base_sob_id;

	queue_id = GAUDI_QUEUE_ID_NIC_0_0 + stream;
	for (i = 0 ; i < NIC_NUMBER_OF_ENGINES ; i++) {
		q = &hdev->kernel_queues[queue_id + (4 * i)];
		q->sync_stream_prop.collective_sob_id = sob_id + i;
	}

	/* Both DMA5 and TPC7 use the same resources since only a single
	 * engine need to participate in the reduction process
	 */
	queue_id = GAUDI_QUEUE_ID_DMA_5_0 + stream;
	q = &hdev->kernel_queues[queue_id];
	q->sync_stream_prop.collective_sob_id =
			sob_id + NIC_NUMBER_OF_ENGINES;

	queue_id = GAUDI_QUEUE_ID_TPC_7_0 + stream;
	q = &hdev->kernel_queues[queue_id];
	q->sync_stream_prop.collective_sob_id =
			sob_id + NIC_NUMBER_OF_ENGINES;
}

static void gaudi_sob_group_hw_reset(struct kref *ref)
{
	struct gaudi_hw_sob_group *hw_sob_group =
		container_of(ref, struct gaudi_hw_sob_group, kref);
	struct hl_device *hdev = hw_sob_group->hdev;
	int i;

	for (i = 0 ; i < NUMBER_OF_SOBS_IN_GRP ; i++)
		WREG32((mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_SOB_OBJ_0 +
			(hw_sob_group->base_sob_id * 4) + (i * 4)), 0);

	kref_init(&hw_sob_group->kref);
}

static void gaudi_sob_group_reset_error(struct kref *ref)
{
	struct gaudi_hw_sob_group *hw_sob_group =
		container_of(ref, struct gaudi_hw_sob_group, kref);
	struct hl_device *hdev = hw_sob_group->hdev;

	dev_crit(hdev->dev,
		"SOB release shouldn't be called here, base_sob_id: %d\n",
		hw_sob_group->base_sob_id);
}

static void gaudi_collective_mstr_sob_mask_set(struct gaudi_device *gaudi)
{
	struct gaudi_collective_properties *prop;
	int i;

	prop = &gaudi->collective_props;

	memset(prop->mstr_sob_mask, 0, sizeof(prop->mstr_sob_mask));

	for (i = 0 ; i < NIC_NUMBER_OF_ENGINES ; i++)
		if (gaudi->hw_cap_initialized & BIT(HW_CAP_NIC_SHIFT + i))
			prop->mstr_sob_mask[i / HL_MAX_SOBS_PER_MONITOR] |=
					BIT(i % HL_MAX_SOBS_PER_MONITOR);
	/* Set collective engine bit */
	prop->mstr_sob_mask[i / HL_MAX_SOBS_PER_MONITOR] |=
				BIT(i % HL_MAX_SOBS_PER_MONITOR);
}

static int gaudi_collective_init(struct hl_device *hdev)
{
	u32 i, sob_id, reserved_sobs_per_group;
	struct gaudi_collective_properties *prop;
	struct gaudi_device *gaudi;

	gaudi = hdev->asic_specific;
	prop = &gaudi->collective_props;
	sob_id = hdev->asic_prop.collective_first_sob;

	/* First sob in group must be aligned to HL_MAX_SOBS_PER_MONITOR */
	reserved_sobs_per_group =
		ALIGN(NUMBER_OF_SOBS_IN_GRP, HL_MAX_SOBS_PER_MONITOR);

	/* Init SOB groups */
	for (i = 0 ; i < NUM_SOB_GROUPS; i++) {
		prop->hw_sob_group[i].hdev = hdev;
		prop->hw_sob_group[i].base_sob_id = sob_id;
		sob_id += reserved_sobs_per_group;
		gaudi_sob_group_hw_reset(&prop->hw_sob_group[i].kref);
	}

	for (i = 0 ; i < QMAN_STREAMS; i++) {
		prop->next_sob_group_val[i] = 1;
		prop->curr_sob_group_idx[i] = 0;
		gaudi_collective_map_sobs(hdev, i);
	}

	gaudi_collective_mstr_sob_mask_set(gaudi);

	return 0;
}

static void gaudi_reset_sob_group(struct hl_device *hdev, u16 sob_group)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_collective_properties *cprop = &gaudi->collective_props;

	kref_put(&cprop->hw_sob_group[sob_group].kref,
					gaudi_sob_group_hw_reset);
}

static void gaudi_collective_master_init_job(struct hl_device *hdev,
		struct hl_cs_job *job, u32 stream, u32 sob_group_offset)
{
	u32 master_sob_base, master_monitor, queue_id, cb_size = 0;
	struct gaudi_collective_properties *cprop;
	struct hl_gen_wait_properties wait_prop;
	struct hl_sync_stream_properties *prop;
	struct gaudi_device *gaudi;

	gaudi = hdev->asic_specific;
	cprop = &gaudi->collective_props;
	queue_id = job->hw_queue_id;
	prop = &hdev->kernel_queues[queue_id].sync_stream_prop;

	master_sob_base =
		cprop->hw_sob_group[sob_group_offset].base_sob_id;
	master_monitor = prop->collective_mstr_mon_id[0];

	cprop->hw_sob_group[sob_group_offset].queue_id = queue_id;

	dev_dbg(hdev->dev,
		"Generate master wait CBs, sob %d (mask %#x), val:0x%x, mon %u, q %d\n",
		master_sob_base, cprop->mstr_sob_mask[0],
		cprop->next_sob_group_val[stream],
		master_monitor, queue_id);

	wait_prop.data = (void *) job->patched_cb;
	wait_prop.sob_base = master_sob_base;
	wait_prop.sob_mask = cprop->mstr_sob_mask[0];
	wait_prop.sob_val = cprop->next_sob_group_val[stream];
	wait_prop.mon_id = master_monitor;
	wait_prop.q_idx = queue_id;
	wait_prop.size = cb_size;
	cb_size += gaudi_gen_wait_cb(hdev, &wait_prop);

	master_sob_base += HL_MAX_SOBS_PER_MONITOR;
	master_monitor = prop->collective_mstr_mon_id[1];

	dev_dbg(hdev->dev,
		"Generate master wait CBs, sob %d (mask %#x), val:0x%x, mon %u, q %d\n",
		master_sob_base, cprop->mstr_sob_mask[1],
		cprop->next_sob_group_val[stream],
		master_monitor, queue_id);

	wait_prop.sob_base = master_sob_base;
	wait_prop.sob_mask = cprop->mstr_sob_mask[1];
	wait_prop.mon_id = master_monitor;
	wait_prop.size = cb_size;
	cb_size += gaudi_gen_wait_cb(hdev, &wait_prop);
}

static void gaudi_collective_slave_init_job(struct hl_device *hdev,
		struct hl_cs_job *job, struct hl_cs_compl *cs_cmpl)
{
	struct hl_gen_wait_properties wait_prop;
	struct hl_sync_stream_properties *prop;
	u32 queue_id, cb_size = 0;

	queue_id = job->hw_queue_id;
	prop = &hdev->kernel_queues[queue_id].sync_stream_prop;

	if (job->cs->encaps_signals) {
		/* use the encaps signal handle store earlier in the flow
		 * and set the SOB information from the encaps
		 * signals handle
		 */
		hl_hw_queue_encaps_sig_set_sob_info(hdev, job->cs, job,
						cs_cmpl);

		dev_dbg(hdev->dev, "collective wait: Sequence %llu found, sob_id: %u,  wait for sob_val: %u\n",
				job->cs->sequence,
				cs_cmpl->hw_sob->sob_id,
				cs_cmpl->sob_val);
	}

	/* Add to wait CBs using slave monitor */
	wait_prop.data = (void *) job->user_cb;
	wait_prop.sob_base = cs_cmpl->hw_sob->sob_id;
	wait_prop.sob_mask = 0x1;
	wait_prop.sob_val = cs_cmpl->sob_val;
	wait_prop.mon_id = prop->collective_slave_mon_id;
	wait_prop.q_idx = queue_id;
	wait_prop.size = cb_size;

	dev_dbg(hdev->dev,
		"Generate slave wait CB, sob %d, val:%x, mon %d, q %d\n",
		cs_cmpl->hw_sob->sob_id, cs_cmpl->sob_val,
		prop->collective_slave_mon_id, queue_id);

	cb_size += gaudi_gen_wait_cb(hdev, &wait_prop);

	dev_dbg(hdev->dev,
		"generate signal CB, sob_id: %d, sob val: 1, q_idx: %d\n",
		prop->collective_sob_id, queue_id);

	cb_size += gaudi_gen_signal_cb(hdev, job->user_cb,
			prop->collective_sob_id, cb_size, false);
}

static int gaudi_collective_wait_init_cs(struct hl_cs *cs)
{
	struct hl_cs_compl *signal_cs_cmpl =
		container_of(cs->signal_fence, struct hl_cs_compl, base_fence);
	struct hl_cs_compl *cs_cmpl =
		container_of(cs->fence, struct hl_cs_compl, base_fence);
	struct hl_cs_encaps_sig_handle *handle = cs->encaps_sig_hdl;
	struct gaudi_collective_properties *cprop;
	u32 stream, queue_id, sob_group_offset;
	struct gaudi_device *gaudi;
	struct hl_device *hdev;
	struct hl_cs_job *job;
	struct hl_ctx *ctx;

	ctx = cs->ctx;
	hdev = ctx->hdev;
	gaudi = hdev->asic_specific;
	cprop = &gaudi->collective_props;

	if (cs->encaps_signals) {
		cs_cmpl->hw_sob = handle->hw_sob;
		/* at this checkpoint we only need the hw_sob pointer
		 * for the completion check before start going over the jobs
		 * of the master/slaves, the sob_value will be taken later on
		 * in gaudi_collective_slave_init_job depends on each
		 * job wait offset value.
		 */
		cs_cmpl->sob_val = 0;
	} else {
		/* copy the SOB id and value of the signal CS */
		cs_cmpl->hw_sob = signal_cs_cmpl->hw_sob;
		cs_cmpl->sob_val = signal_cs_cmpl->sob_val;
	}

	/* check again if the signal cs already completed.
	 * if yes then don't send any wait cs since the hw_sob
	 * could be in reset already. if signal is not completed
	 * then get refcount to hw_sob to prevent resetting the sob
	 * while wait cs is not submitted.
	 * note that this check is protected by two locks,
	 * hw queue lock and completion object lock,
	 * and the same completion object lock also protects
	 * the hw_sob reset handler function.
	 * The hw_queue lock prevent out of sync of hw_sob
	 * refcount value, changed by signal/wait flows.
	 */
	spin_lock(&signal_cs_cmpl->lock);

	if (completion_done(&cs->signal_fence->completion)) {
		spin_unlock(&signal_cs_cmpl->lock);
		return -EINVAL;
	}
	/* Increment kref since all slave queues are now waiting on it */
	kref_get(&cs_cmpl->hw_sob->kref);

	spin_unlock(&signal_cs_cmpl->lock);

	/* Calculate the stream from collective master queue (1st job) */
	job = list_first_entry(&cs->job_list, struct hl_cs_job, cs_node);
	stream = job->hw_queue_id % 4;
	sob_group_offset =
		stream * HL_RSVD_SOBS + cprop->curr_sob_group_idx[stream];

	list_for_each_entry(job, &cs->job_list, cs_node) {
		queue_id = job->hw_queue_id;

		if (hdev->kernel_queues[queue_id].collective_mode ==
				HL_COLLECTIVE_MASTER)
			gaudi_collective_master_init_job(hdev, job, stream,
						sob_group_offset);
		else
			gaudi_collective_slave_init_job(hdev, job, cs_cmpl);
	}

	cs_cmpl->sob_group = sob_group_offset;

	/* Handle sob group kref and wraparound */
	kref_get(&cprop->hw_sob_group[sob_group_offset].kref);
	cprop->next_sob_group_val[stream]++;

	if (cprop->next_sob_group_val[stream] == HL_MAX_SOB_VAL) {
		/*
		 * Decrement as we reached the max value.
		 * The release function won't be called here as we've
		 * just incremented the refcount.
		 */
		kref_put(&cprop->hw_sob_group[sob_group_offset].kref,
				gaudi_sob_group_reset_error);
		cprop->next_sob_group_val[stream] = 1;
		/* only two SOBs are currently in use */
		cprop->curr_sob_group_idx[stream] =
			(cprop->curr_sob_group_idx[stream] + 1) &
							(HL_RSVD_SOBS - 1);

		gaudi_collective_map_sobs(hdev, stream);

		dev_dbg(hdev->dev, "switched to SOB group %d, stream: %d\n",
				cprop->curr_sob_group_idx[stream], stream);
	}

	mb();
	hl_fence_put(cs->signal_fence);
	cs->signal_fence = NULL;

	return 0;
}

static u32 gaudi_get_patched_cb_extra_size(u32 user_cb_size)
{
	u32 cacheline_end, additional_commands;

	cacheline_end = round_up(user_cb_size, DEVICE_CACHE_LINE_SIZE);
	additional_commands = sizeof(struct packet_msg_prot) * 2;

	if (user_cb_size + additional_commands > cacheline_end)
		return cacheline_end - user_cb_size + additional_commands;
	else
		return additional_commands;
}

static int gaudi_collective_wait_create_job(struct hl_device *hdev,
		struct hl_ctx *ctx, struct hl_cs *cs,
		enum hl_collective_mode mode, u32 queue_id, u32 wait_queue_id,
		u32 encaps_signal_offset)
{
	struct hw_queue_properties *hw_queue_prop;
	struct hl_cs_counters_atomic *cntr;
	struct hl_cs_job *job;
	struct hl_cb *cb;
	u32 cb_size;
	bool patched_cb;

	cntr = &hdev->aggregated_cs_counters;

	if (mode == HL_COLLECTIVE_MASTER) {
		/* CB size of collective master queue contains
		 * 4 msg short packets for monitor 1 configuration
		 * 1 fence packet
		 * 4 msg short packets for monitor 2 configuration
		 * 1 fence packet
		 * 2 msg prot packets for completion and MSI
		 */
		cb_size = sizeof(struct packet_msg_short) * 8 +
				sizeof(struct packet_fence) * 2 +
				sizeof(struct packet_msg_prot) * 2;
		patched_cb = true;
	} else {
		/* CB size of collective slave queues contains
		 * 4 msg short packets for monitor configuration
		 * 1 fence packet
		 * 1 additional msg short packet for sob signal
		 */
		cb_size = sizeof(struct packet_msg_short) * 5 +
				sizeof(struct packet_fence);
		patched_cb = false;
	}

	hw_queue_prop = &hdev->asic_prop.hw_queues_props[queue_id];
	job = hl_cs_allocate_job(hdev, hw_queue_prop->type, true);
	if (!job) {
		atomic64_inc(&ctx->cs_counters.out_of_mem_drop_cnt);
		atomic64_inc(&cntr->out_of_mem_drop_cnt);
		dev_err(hdev->dev, "Failed to allocate a new job\n");
		return -ENOMEM;
	}

	/* Allocate internal mapped CB for non patched CBs */
	cb = hl_cb_kernel_create(hdev, cb_size, !patched_cb);
	if (!cb) {
		atomic64_inc(&ctx->cs_counters.out_of_mem_drop_cnt);
		atomic64_inc(&cntr->out_of_mem_drop_cnt);
		kfree(job);
		return -EFAULT;
	}

	job->id = 0;
	job->cs = cs;
	job->user_cb = cb;
	atomic_inc(&job->user_cb->cs_cnt);
	job->user_cb_size = cb_size;
	job->hw_queue_id = queue_id;

	/* since its guaranteed to have only one chunk in the collective wait
	 * cs, we can use this chunk to set the encapsulated signal offset
	 * in the jobs.
	 */
	if (cs->encaps_signals)
		job->encaps_sig_wait_offset = encaps_signal_offset;

	/*
	 * No need in parsing, user CB is the patched CB.
	 * We call hl_cb_destroy() out of two reasons - we don't need
	 * the CB in the CB idr anymore and to decrement its refcount as
	 * it was incremented inside hl_cb_kernel_create().
	 */
	if (patched_cb)
		job->patched_cb = job->user_cb;
	else
		job->patched_cb = NULL;

	job->job_cb_size = job->user_cb_size;
	hl_cb_destroy(&hdev->kernel_mem_mgr, cb->buf->handle);

	/* increment refcount as for external queues we get completion */
	if (hw_queue_prop->type == QUEUE_TYPE_EXT)
		cs_get(cs);

	cs->jobs_in_queue_cnt[job->hw_queue_id]++;

	list_add_tail(&job->cs_node, &cs->job_list);

	hl_debugfs_add_job(hdev, job);

	return 0;
}

static int gaudi_collective_wait_create_jobs(struct hl_device *hdev,
		struct hl_ctx *ctx, struct hl_cs *cs,
		u32 wait_queue_id, u32 collective_engine_id,
		u32 encaps_signal_offset)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct hw_queue_properties *hw_queue_prop;
	u32 queue_id, collective_queue, num_jobs;
	u32 stream, nic_queue, nic_idx = 0;
	bool skip;
	int i, rc = 0;

	/* Verify wait queue id is configured as master */
	hw_queue_prop = &hdev->asic_prop.hw_queues_props[wait_queue_id];
	if (!(hw_queue_prop->collective_mode == HL_COLLECTIVE_MASTER)) {
		dev_err(hdev->dev,
			"Queue %d is not configured as collective master\n",
			wait_queue_id);
		return -EINVAL;
	}

	/* Verify engine id is supported */
	if (collective_engine_id != GAUDI_ENGINE_ID_DMA_5 &&
			collective_engine_id != GAUDI_ENGINE_ID_TPC_7) {
		dev_err(hdev->dev,
			"Collective wait does not support engine %u\n",
			collective_engine_id);
		return -EINVAL;
	}

	stream = wait_queue_id % 4;

	if (collective_engine_id == GAUDI_ENGINE_ID_DMA_5)
		collective_queue = GAUDI_QUEUE_ID_DMA_5_0 + stream;
	else
		collective_queue = GAUDI_QUEUE_ID_TPC_7_0 + stream;

	num_jobs = NUMBER_OF_SOBS_IN_GRP + 1;
	nic_queue = GAUDI_QUEUE_ID_NIC_0_0 + stream;

	/* First job goes to the collective master queue, it will wait for
	 * the collective slave queues to finish execution.
	 * The synchronization is done using two monitors:
	 * First monitor for NICs 0-7, second monitor for NICs 8-9 and the
	 * reduction engine (DMA5/TPC7).
	 *
	 * Rest of the jobs goes to the collective slave queues which will
	 * all wait for the user to signal sob 'cs_cmpl->sob_val'.
	 */
	for (i = 0 ; i < num_jobs ; i++) {
		if (i == 0) {
			queue_id = wait_queue_id;
			rc = gaudi_collective_wait_create_job(hdev, ctx, cs,
				HL_COLLECTIVE_MASTER, queue_id,
				wait_queue_id, encaps_signal_offset);
		} else {
			if (nic_idx < NIC_NUMBER_OF_ENGINES) {
				if (gaudi->hw_cap_initialized &
					BIT(HW_CAP_NIC_SHIFT + nic_idx))
					skip = false;
				else
					skip = true;

				queue_id = nic_queue;
				nic_queue += 4;
				nic_idx++;

				if (skip)
					continue;
			} else {
				queue_id = collective_queue;
			}

			rc = gaudi_collective_wait_create_job(hdev, ctx, cs,
				HL_COLLECTIVE_SLAVE, queue_id,
				wait_queue_id, encaps_signal_offset);
		}

		if (rc)
			return rc;
	}

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

	if ((hdev->card_type == cpucp_card_type_pci) &&
			(hdev->nic_ports_mask & 0x3)) {
		dev_info(hdev->dev,
			"PCI card detected, only 8 ports are enabled\n");
		hdev->nic_ports_mask &= ~0x3;

		/* Stop and disable unused NIC QMANs */
		WREG32(mmNIC0_QM0_GLBL_CFG1, NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
					NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
					NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);

		WREG32(mmNIC0_QM1_GLBL_CFG1, NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
					NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
					NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);

		WREG32(mmNIC0_QM0_GLBL_CFG0, 0);
		WREG32(mmNIC0_QM1_GLBL_CFG0, 0);

		gaudi->hw_cap_initialized &= ~(HW_CAP_NIC0 | HW_CAP_NIC1);
	}

	rc = hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_ENABLE_PCI_ACCESS, 0x0);
	if (rc) {
		dev_err(hdev->dev, "Failed to enable PCI access from CPU\n");
		return rc;
	}

	/* Scrub both SRAM and DRAM */
	rc = hdev->asic_funcs->scrub_device_mem(hdev);
	if (rc)
		goto disable_pci_access;

	rc = gaudi_fetch_psoc_frequency(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to fetch psoc frequency\n");
		goto disable_pci_access;
	}

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

	rc = gaudi_collective_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to init collective\n");
		goto disable_pci_access;
	}

	/* We only support a single ASID for the user, so for the sake of optimization, just
	 * initialize the ASID one time during device initialization with the fixed value of 1
	 */
	gaudi_mmu_prepare(hdev, 1);

	hl_fw_set_pll_profile(hdev);

	return 0;

disable_pci_access:
	hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_DISABLE_PCI_ACCESS, 0x0);

	return rc;
}

static void gaudi_late_fini(struct hl_device *hdev)
{
	hl_hwmon_release_resources(hdev);
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
		virt_addr_arr[i] = hl_asic_dma_alloc_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE,
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

	if (!hdev->asic_prop.fw_security_enabled)
		GAUDI_PCI_TO_CPU_ADDR(hdev->cpu_accessible_dma_address);

free_dma_mem_arr:
	for (j = 0 ; j < i ; j++)
		hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, virt_addr_arr[j],
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
		hl_asic_dma_free_coherent(hdev, q->pq_size, q->pq_kernel_addr, q->pq_dma_addr);
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
		case GAUDI_QUEUE_ID_DMA_2_0 ... GAUDI_QUEUE_ID_DMA_7_3:
			q->pq_size = HBM_DMA_QMAN_SIZE_IN_BYTES;
			break;
		case GAUDI_QUEUE_ID_MME_0_0 ... GAUDI_QUEUE_ID_MME_1_3:
			q->pq_size = MME_QMAN_SIZE_IN_BYTES;
			break;
		case GAUDI_QUEUE_ID_TPC_0_0 ... GAUDI_QUEUE_ID_TPC_7_3:
			q->pq_size = TPC_QMAN_SIZE_IN_BYTES;
			break;
		case GAUDI_QUEUE_ID_NIC_0_0 ... GAUDI_QUEUE_ID_NIC_9_3:
			q->pq_size = NIC_QMAN_SIZE_IN_BYTES;
			break;
		default:
			dev_err(hdev->dev, "Bad internal queue index %d", i);
			rc = -EINVAL;
			goto free_internal_qmans_pq_mem;
		}

		q->pq_kernel_addr = hl_asic_dma_alloc_coherent(hdev, q->pq_size, &q->pq_dma_addr,
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

static void gaudi_set_pci_memory_regions(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pci_mem_region *region;

	/* CFG */
	region = &hdev->pci_mem_region[PCI_REGION_CFG];
	region->region_base = CFG_BASE;
	region->region_size = CFG_SIZE;
	region->offset_in_bar = CFG_BASE - SPI_FLASH_BASE_ADDR;
	region->bar_size = CFG_BAR_SIZE;
	region->bar_id = CFG_BAR_ID;
	region->used = 1;

	/* SRAM */
	region = &hdev->pci_mem_region[PCI_REGION_SRAM];
	region->region_base = SRAM_BASE_ADDR;
	region->region_size = SRAM_SIZE;
	region->offset_in_bar = 0;
	region->bar_size = SRAM_BAR_SIZE;
	region->bar_id = SRAM_BAR_ID;
	region->used = 1;

	/* DRAM */
	region = &hdev->pci_mem_region[PCI_REGION_DRAM];
	region->region_base = DRAM_PHYS_BASE;
	region->region_size = hdev->asic_prop.dram_size;
	region->offset_in_bar = 0;
	region->bar_size = prop->dram_pci_bar_size;
	region->bar_id = HBM_BAR_ID;
	region->used = 1;

	/* SP SRAM */
	region = &hdev->pci_mem_region[PCI_REGION_SP_SRAM];
	region->region_base = PSOC_SCRATCHPAD_ADDR;
	region->region_size = PSOC_SCRATCHPAD_SIZE;
	region->offset_in_bar = PSOC_SCRATCHPAD_ADDR - SPI_FLASH_BASE_ADDR;
	region->bar_size = CFG_BAR_SIZE;
	region->bar_id = CFG_BAR_ID;
	region->used = 1;
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

	hdev->supports_sync_stream = true;
	hdev->supports_coresight = true;
	hdev->supports_staged_submission = true;
	hdev->supports_wait_for_multi_cs = true;

	hdev->asic_funcs->set_pci_memory_regions(hdev);
	hdev->stream_master_qid_arr =
				hdev->asic_funcs->get_stream_master_qid_arr();
	hdev->stream_master_qid_arr_size = GAUDI_STREAM_MASTER_ARR_SIZE;

	return 0;

free_cpu_accessible_dma_pool:
	gen_pool_destroy(hdev->cpu_accessible_dma_pool);
free_cpu_dma_mem:
	if (!hdev->asic_prop.fw_security_enabled)
		GAUDI_CPU_TO_PCI_ADDR(hdev->cpu_accessible_dma_address,
					hdev->cpu_pci_msb_addr);
	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, hdev->cpu_accessible_dma_mem,
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

	if (!hdev->asic_prop.fw_security_enabled)
		GAUDI_CPU_TO_PCI_ADDR(hdev->cpu_accessible_dma_address,
					hdev->cpu_pci_msb_addr);

	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, hdev->cpu_accessible_dma_mem,
					hdev->cpu_accessible_dma_address);

	dma_pool_destroy(hdev->dma_pool);

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

	dev_dbg(hdev->dev, "Working in single MSI IRQ mode\n");

	irq = gaudi_pci_irq_vector(hdev, 0, false);
	rc = request_irq(irq, gaudi_irq_handler_single, 0,
			"gaudi single msi", hdev);
	if (rc)
		dev_err(hdev->dev,
			"Failed to request single MSI IRQ\n");

	return rc;
}

static int gaudi_enable_msi(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int rc;

	if (gaudi->hw_cap_initialized & HW_CAP_MSI)
		return 0;

	rc = pci_alloc_irq_vectors(hdev->pdev, 1, 1, PCI_IRQ_MSI);
	if (rc < 0) {
		dev_err(hdev->dev, "MSI: Failed to enable support %d\n", rc);
		return rc;
	}

	rc = gaudi_enable_msi_single(hdev);
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

	if (!(gaudi->hw_cap_initialized & HW_CAP_MSI))
		return;

	/* Wait for all pending IRQs to be finished */
	synchronize_irq(gaudi_pci_irq_vector(hdev, 0, false));
}

static void gaudi_disable_msi(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MSI))
		return;

	gaudi_sync_irqs(hdev);
	free_irq(gaudi_pci_irq_vector(hdev, 0, false), hdev);
	pci_free_irq_vectors(hdev->pdev);

	gaudi->hw_cap_initialized &= ~HW_CAP_MSI;
}

static void gaudi_init_scrambler_sram(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (hdev->asic_prop.fw_security_enabled)
		return;

	if (hdev->asic_prop.fw_app_cpu_boot_dev_sts0 &
						CPU_BOOT_DEV_STS0_SRAM_SCR_EN)
		return;

	if (gaudi->hw_cap_initialized & HW_CAP_SRAM_SCRAMBLER)
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

	if (hdev->asic_prop.fw_security_enabled)
		return;

	if (hdev->asic_prop.fw_bootfit_cpu_boot_dev_sts0 &
					CPU_BOOT_DEV_STS0_DRAM_SCR_EN)
		return;

	if (gaudi->hw_cap_initialized & HW_CAP_HBM_SCRAMBLER)
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
	if (hdev->asic_prop.fw_security_enabled)
		return;

	if (hdev->asic_prop.fw_bootfit_cpu_boot_dev_sts0 &
					CPU_BOOT_DEV_STS0_E2E_CRED_EN)
		return;

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
	u32 hbm0_wr, hbm1_wr, hbm0_rd, hbm1_rd;

	if (hdev->asic_prop.fw_security_enabled)
		return;

	if (hdev->asic_prop.fw_bootfit_cpu_boot_dev_sts0 &
						CPU_BOOT_DEV_STS0_HBM_CRED_EN)
		return;

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

	for (tpc_id = 0, tpc_offset = 0;
				tpc_id < TPC_NUMBER_OF_ENGINES;
				tpc_id++, tpc_offset += TPC_CFG_OFFSET) {
		/* Mask all arithmetic interrupts from TPC */
		WREG32(mmTPC0_CFG_TPC_INTR_MASK + tpc_offset, 0x8FFE);
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
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 mtr_base_en_lo, mtr_base_en_hi, mtr_base_ws_lo, mtr_base_ws_hi;
	u32 so_base_en_lo, so_base_en_hi, so_base_ws_lo, so_base_ws_hi;
	u32 q_off, dma_qm_offset;
	u32 dma_qm_err_cfg, irq_handler_offset;

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
		irq_handler_offset = hdev->asic_prop.gic_interrupts_enable ?
				mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR :
				le32_to_cpu(dyn_regs->gic_dma_qm_irq_ctrl);

		/* Configure RAZWI IRQ */
		dma_qm_err_cfg = PCI_DMA_QMAN_GLBL_ERR_CFG_MSG_EN_MASK;
		if (hdev->stop_on_err)
			dma_qm_err_cfg |=
				PCI_DMA_QMAN_GLBL_ERR_CFG_STOP_ON_ERR_EN_MASK;

		WREG32(mmDMA0_QM_GLBL_ERR_CFG + dma_qm_offset, dma_qm_err_cfg);

		WREG32(mmDMA0_QM_GLBL_ERR_ADDR_LO + dma_qm_offset,
			lower_32_bits(CFG_BASE + irq_handler_offset));
		WREG32(mmDMA0_QM_GLBL_ERR_ADDR_HI + dma_qm_offset,
			upper_32_bits(CFG_BASE + irq_handler_offset));

		WREG32(mmDMA0_QM_GLBL_ERR_WDATA + dma_qm_offset,
			gaudi_irq_map_table[GAUDI_EVENT_DMA0_QM].cpu_id +
									dma_id);

		WREG32(mmDMA0_QM_ARB_ERR_MSG_EN + dma_qm_offset,
				QM_ARB_ERR_MSG_EN_MASK);

		/* Set timeout to maximum */
		WREG32(mmDMA0_QM_ARB_SLV_CHOISE_WDT + dma_qm_offset, GAUDI_ARB_WDT_TIMEOUT);

		WREG32(mmDMA0_QM_GLBL_PROT + dma_qm_offset,
				QMAN_EXTERNAL_MAKE_TRUSTED);

		WREG32(mmDMA0_QM_GLBL_CFG1 + dma_qm_offset, 0);
	}
}

static void gaudi_init_dma_core(struct hl_device *hdev, int dma_id)
{
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 dma_err_cfg = 1 << DMA0_CORE_ERR_CFG_ERR_MSG_EN_SHIFT;
	u32 dma_offset = dma_id * DMA_CORE_OFFSET;
	u32 irq_handler_offset;

	/* Set to maximum possible according to physical size */
	WREG32(mmDMA0_CORE_RD_MAX_OUTSTAND + dma_offset, 0);
	WREG32(mmDMA0_CORE_RD_MAX_SIZE + dma_offset, 0);

	/* WA for H/W bug H3-2116 */
	WREG32(mmDMA0_CORE_LBW_MAX_OUTSTAND + dma_offset, 15);

	/* STOP_ON bit implies no completion to operation in case of RAZWI */
	if (hdev->stop_on_err)
		dma_err_cfg |= 1 << DMA0_CORE_ERR_CFG_STOP_ON_ERR_SHIFT;

	WREG32(mmDMA0_CORE_ERR_CFG + dma_offset, dma_err_cfg);

	irq_handler_offset = hdev->asic_prop.gic_interrupts_enable ?
			mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR :
			le32_to_cpu(dyn_regs->gic_dma_core_irq_ctrl);

	WREG32(mmDMA0_CORE_ERRMSG_ADDR_LO + dma_offset,
		lower_32_bits(CFG_BASE + irq_handler_offset));
	WREG32(mmDMA0_CORE_ERRMSG_ADDR_HI + dma_offset,
		upper_32_bits(CFG_BASE + irq_handler_offset));

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
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 mtr_base_en_lo, mtr_base_en_hi, mtr_base_ws_lo, mtr_base_ws_hi;
	u32 so_base_en_lo, so_base_en_hi, so_base_ws_lo, so_base_ws_hi;
	u32 dma_qm_err_cfg, irq_handler_offset;
	u32 q_off, dma_qm_offset;

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
		irq_handler_offset = hdev->asic_prop.gic_interrupts_enable ?
				mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR :
				le32_to_cpu(dyn_regs->gic_dma_qm_irq_ctrl);

		WREG32(mmDMA0_QM_CP_LDMA_TSIZE_OFFSET_0 + q_off,
							QMAN_LDMA_SIZE_OFFSET);
		WREG32(mmDMA0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_SRC_OFFSET);
		WREG32(mmDMA0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_DST_OFFSET);

		/* Configure RAZWI IRQ */
		dma_qm_err_cfg = HBM_DMA_QMAN_GLBL_ERR_CFG_MSG_EN_MASK;
		if (hdev->stop_on_err)
			dma_qm_err_cfg |=
				HBM_DMA_QMAN_GLBL_ERR_CFG_STOP_ON_ERR_EN_MASK;

		WREG32(mmDMA0_QM_GLBL_ERR_CFG + dma_qm_offset, dma_qm_err_cfg);

		WREG32(mmDMA0_QM_GLBL_ERR_ADDR_LO + dma_qm_offset,
			lower_32_bits(CFG_BASE + irq_handler_offset));
		WREG32(mmDMA0_QM_GLBL_ERR_ADDR_HI + dma_qm_offset,
			upper_32_bits(CFG_BASE + irq_handler_offset));

		WREG32(mmDMA0_QM_GLBL_ERR_WDATA + dma_qm_offset,
			gaudi_irq_map_table[GAUDI_EVENT_DMA0_QM].cpu_id +
									dma_id);

		WREG32(mmDMA0_QM_ARB_ERR_MSG_EN + dma_qm_offset,
				QM_ARB_ERR_MSG_EN_MASK);

		/* Set timeout to maximum */
		WREG32(mmDMA0_QM_ARB_SLV_CHOISE_WDT + dma_qm_offset, GAUDI_ARB_WDT_TIMEOUT);

		WREG32(mmDMA0_QM_GLBL_CFG1 + dma_qm_offset, 0);
		WREG32(mmDMA0_QM_GLBL_PROT + dma_qm_offset,
				QMAN_INTERNAL_MAKE_TRUSTED);
	}

	WREG32(mmDMA0_QM_CP_MSG_BASE0_ADDR_LO_0 + q_off, mtr_base_en_lo);
	WREG32(mmDMA0_QM_CP_MSG_BASE0_ADDR_HI_0 + q_off, mtr_base_en_hi);
	WREG32(mmDMA0_QM_CP_MSG_BASE1_ADDR_LO_0 + q_off, so_base_en_lo);
	WREG32(mmDMA0_QM_CP_MSG_BASE1_ADDR_HI_0 + q_off, so_base_en_hi);

	/* Configure DMA5 CP_MSG_BASE 2/3 for sync stream collective */
	if (gaudi_dma_assignment[dma_id] == GAUDI_ENGINE_ID_DMA_5) {
		WREG32(mmDMA0_QM_CP_MSG_BASE2_ADDR_LO_0 + q_off,
				mtr_base_ws_lo);
		WREG32(mmDMA0_QM_CP_MSG_BASE2_ADDR_HI_0 + q_off,
				mtr_base_ws_hi);
		WREG32(mmDMA0_QM_CP_MSG_BASE3_ADDR_LO_0 + q_off,
				so_base_ws_lo);
		WREG32(mmDMA0_QM_CP_MSG_BASE3_ADDR_HI_0 + q_off,
				so_base_ws_hi);
	}
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
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 mtr_base_lo, mtr_base_hi;
	u32 so_base_lo, so_base_hi;
	u32 irq_handler_offset;
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
		irq_handler_offset = hdev->asic_prop.gic_interrupts_enable ?
				mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR :
				le32_to_cpu(dyn_regs->gic_mme_qm_irq_ctrl);

		WREG32(mmMME0_QM_CP_LDMA_TSIZE_OFFSET_0 + q_off,
							QMAN_LDMA_SIZE_OFFSET);
		WREG32(mmMME0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_SRC_OFFSET);
		WREG32(mmMME0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_DST_OFFSET);

		/* Configure RAZWI IRQ */
		mme_id = mme_offset /
				(mmMME1_QM_GLBL_CFG0 - mmMME0_QM_GLBL_CFG0) / 2;

		mme_qm_err_cfg = MME_QMAN_GLBL_ERR_CFG_MSG_EN_MASK;
		if (hdev->stop_on_err)
			mme_qm_err_cfg |=
				MME_QMAN_GLBL_ERR_CFG_STOP_ON_ERR_EN_MASK;

		WREG32(mmMME0_QM_GLBL_ERR_CFG + mme_offset, mme_qm_err_cfg);

		WREG32(mmMME0_QM_GLBL_ERR_ADDR_LO + mme_offset,
			lower_32_bits(CFG_BASE + irq_handler_offset));
		WREG32(mmMME0_QM_GLBL_ERR_ADDR_HI + mme_offset,
			upper_32_bits(CFG_BASE + irq_handler_offset));

		WREG32(mmMME0_QM_GLBL_ERR_WDATA + mme_offset,
			gaudi_irq_map_table[GAUDI_EVENT_MME0_QM].cpu_id +
									mme_id);

		WREG32(mmMME0_QM_ARB_ERR_MSG_EN + mme_offset,
				QM_ARB_ERR_MSG_EN_MASK);

		/* Set timeout to maximum */
		WREG32(mmMME0_QM_ARB_SLV_CHOISE_WDT + mme_offset, GAUDI_ARB_WDT_TIMEOUT);

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
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 mtr_base_en_lo, mtr_base_en_hi, mtr_base_ws_lo, mtr_base_ws_hi;
	u32 so_base_en_lo, so_base_en_hi, so_base_ws_lo, so_base_ws_hi;
	u32 tpc_qm_err_cfg, irq_handler_offset;
	u32 q_off, tpc_id;

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

	q_off = tpc_offset + qman_id * 4;

	tpc_id = tpc_offset /
			(mmTPC1_QM_GLBL_CFG0 - mmTPC0_QM_GLBL_CFG0);

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
		irq_handler_offset = hdev->asic_prop.gic_interrupts_enable ?
				mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR :
				le32_to_cpu(dyn_regs->gic_tpc_qm_irq_ctrl);

		WREG32(mmTPC0_QM_CP_LDMA_TSIZE_OFFSET_0 + q_off,
							QMAN_LDMA_SIZE_OFFSET);
		WREG32(mmTPC0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_SRC_OFFSET);
		WREG32(mmTPC0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_DST_OFFSET);

		/* Configure RAZWI IRQ */
		tpc_qm_err_cfg = TPC_QMAN_GLBL_ERR_CFG_MSG_EN_MASK;
		if (hdev->stop_on_err)
			tpc_qm_err_cfg |=
				TPC_QMAN_GLBL_ERR_CFG_STOP_ON_ERR_EN_MASK;

		WREG32(mmTPC0_QM_GLBL_ERR_CFG + tpc_offset, tpc_qm_err_cfg);

		WREG32(mmTPC0_QM_GLBL_ERR_ADDR_LO + tpc_offset,
			lower_32_bits(CFG_BASE + irq_handler_offset));
		WREG32(mmTPC0_QM_GLBL_ERR_ADDR_HI + tpc_offset,
			upper_32_bits(CFG_BASE + irq_handler_offset));

		WREG32(mmTPC0_QM_GLBL_ERR_WDATA + tpc_offset,
			gaudi_irq_map_table[GAUDI_EVENT_TPC0_QM].cpu_id +
									tpc_id);

		WREG32(mmTPC0_QM_ARB_ERR_MSG_EN + tpc_offset,
				QM_ARB_ERR_MSG_EN_MASK);

		/* Set timeout to maximum */
		WREG32(mmTPC0_QM_ARB_SLV_CHOISE_WDT + tpc_offset, GAUDI_ARB_WDT_TIMEOUT);

		WREG32(mmTPC0_QM_GLBL_CFG1 + tpc_offset, 0);
		WREG32(mmTPC0_QM_GLBL_PROT + tpc_offset,
				QMAN_INTERNAL_MAKE_TRUSTED);
	}

	WREG32(mmTPC0_QM_CP_MSG_BASE0_ADDR_LO_0 + q_off, mtr_base_en_lo);
	WREG32(mmTPC0_QM_CP_MSG_BASE0_ADDR_HI_0 + q_off, mtr_base_en_hi);
	WREG32(mmTPC0_QM_CP_MSG_BASE1_ADDR_LO_0 + q_off, so_base_en_lo);
	WREG32(mmTPC0_QM_CP_MSG_BASE1_ADDR_HI_0 + q_off, so_base_en_hi);

	/* Configure TPC7 CP_MSG_BASE 2/3 for sync stream collective */
	if (tpc_id == 6) {
		WREG32(mmTPC0_QM_CP_MSG_BASE2_ADDR_LO_0 + q_off,
				mtr_base_ws_lo);
		WREG32(mmTPC0_QM_CP_MSG_BASE2_ADDR_HI_0 + q_off,
				mtr_base_ws_hi);
		WREG32(mmTPC0_QM_CP_MSG_BASE3_ADDR_LO_0 + q_off,
				so_base_ws_lo);
		WREG32(mmTPC0_QM_CP_MSG_BASE3_ADDR_HI_0 + q_off,
				so_base_ws_hi);
	}
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

static void gaudi_init_nic_qman(struct hl_device *hdev, u32 nic_offset,
				int qman_id, u64 qman_base_addr, int nic_id)
{
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 mtr_base_en_lo, mtr_base_en_hi, mtr_base_ws_lo, mtr_base_ws_hi;
	u32 so_base_en_lo, so_base_en_hi, so_base_ws_lo, so_base_ws_hi;
	u32 nic_qm_err_cfg, irq_handler_offset;
	u32 q_off;

	mtr_base_en_lo = lower_32_bits((CFG_BASE & U32_MAX) +
			mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	mtr_base_en_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	so_base_en_lo = lower_32_bits((CFG_BASE & U32_MAX) +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0);
	so_base_en_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0);
	mtr_base_ws_lo = lower_32_bits((CFG_BASE & U32_MAX) +
				mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	mtr_base_ws_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	so_base_ws_lo = lower_32_bits((CFG_BASE & U32_MAX) +
				mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_SOB_OBJ_0);
	so_base_ws_hi = upper_32_bits(CFG_BASE +
				mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_SOB_OBJ_0);

	q_off = nic_offset + qman_id * 4;

	WREG32(mmNIC0_QM0_PQ_BASE_LO_0 + q_off, lower_32_bits(qman_base_addr));
	WREG32(mmNIC0_QM0_PQ_BASE_HI_0 + q_off, upper_32_bits(qman_base_addr));

	WREG32(mmNIC0_QM0_PQ_SIZE_0 + q_off, ilog2(NIC_QMAN_LENGTH));
	WREG32(mmNIC0_QM0_PQ_PI_0 + q_off, 0);
	WREG32(mmNIC0_QM0_PQ_CI_0 + q_off, 0);

	WREG32(mmNIC0_QM0_CP_LDMA_TSIZE_OFFSET_0 + q_off,
							QMAN_LDMA_SIZE_OFFSET);
	WREG32(mmNIC0_QM0_CP_LDMA_SRC_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_SRC_OFFSET);
	WREG32(mmNIC0_QM0_CP_LDMA_DST_BASE_LO_OFFSET_0 + q_off,
							QMAN_LDMA_DST_OFFSET);

	WREG32(mmNIC0_QM0_CP_MSG_BASE0_ADDR_LO_0 + q_off, mtr_base_en_lo);
	WREG32(mmNIC0_QM0_CP_MSG_BASE0_ADDR_HI_0 + q_off, mtr_base_en_hi);
	WREG32(mmNIC0_QM0_CP_MSG_BASE1_ADDR_LO_0 + q_off, so_base_en_lo);
	WREG32(mmNIC0_QM0_CP_MSG_BASE1_ADDR_HI_0 + q_off, so_base_en_hi);

	/* Configure NIC CP_MSG_BASE 2/3 for sync stream collective */
	WREG32(mmNIC0_QM0_CP_MSG_BASE2_ADDR_LO_0 + q_off, mtr_base_ws_lo);
	WREG32(mmNIC0_QM0_CP_MSG_BASE2_ADDR_HI_0 + q_off, mtr_base_ws_hi);
	WREG32(mmNIC0_QM0_CP_MSG_BASE3_ADDR_LO_0 + q_off, so_base_ws_lo);
	WREG32(mmNIC0_QM0_CP_MSG_BASE3_ADDR_HI_0 + q_off, so_base_ws_hi);

	if (qman_id == 0) {
		irq_handler_offset = hdev->asic_prop.gic_interrupts_enable ?
				mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR :
				le32_to_cpu(dyn_regs->gic_nic_qm_irq_ctrl);

		/* Configure RAZWI IRQ */
		nic_qm_err_cfg = NIC_QMAN_GLBL_ERR_CFG_MSG_EN_MASK;
		if (hdev->stop_on_err)
			nic_qm_err_cfg |=
				NIC_QMAN_GLBL_ERR_CFG_STOP_ON_ERR_EN_MASK;

		WREG32(mmNIC0_QM0_GLBL_ERR_CFG + nic_offset, nic_qm_err_cfg);

		WREG32(mmNIC0_QM0_GLBL_ERR_ADDR_LO + nic_offset,
			lower_32_bits(CFG_BASE + irq_handler_offset));
		WREG32(mmNIC0_QM0_GLBL_ERR_ADDR_HI + nic_offset,
			upper_32_bits(CFG_BASE + irq_handler_offset));

		WREG32(mmNIC0_QM0_GLBL_ERR_WDATA + nic_offset,
			gaudi_irq_map_table[GAUDI_EVENT_NIC0_QM0].cpu_id +
									nic_id);

		WREG32(mmNIC0_QM0_ARB_ERR_MSG_EN + nic_offset,
				QM_ARB_ERR_MSG_EN_MASK);

		/* Set timeout to maximum */
		WREG32(mmNIC0_QM0_ARB_SLV_CHOISE_WDT + nic_offset, GAUDI_ARB_WDT_TIMEOUT);

		WREG32(mmNIC0_QM0_GLBL_CFG1 + nic_offset, 0);
		WREG32(mmNIC0_QM0_GLBL_PROT + nic_offset,
				QMAN_INTERNAL_MAKE_TRUSTED);
	}
}

static void gaudi_init_nic_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_internal_qman_info *q;
	u64 qman_base_addr;
	u32 nic_offset = 0;
	u32 nic_delta_between_qmans =
			mmNIC0_QM1_GLBL_CFG0 - mmNIC0_QM0_GLBL_CFG0;
	u32 nic_delta_between_nics =
			mmNIC1_QM0_GLBL_CFG0 - mmNIC0_QM0_GLBL_CFG0;
	int i, nic_id, internal_q_index;

	if (!hdev->nic_ports_mask)
		return;

	if (gaudi->hw_cap_initialized & HW_CAP_NIC_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing NIC QMANs\n");

	for (nic_id = 0 ; nic_id < NIC_NUMBER_OF_ENGINES ; nic_id++) {
		if (!(hdev->nic_ports_mask & (1 << nic_id))) {
			nic_offset += nic_delta_between_qmans;
			if (nic_id & 1) {
				nic_offset -= (nic_delta_between_qmans * 2);
				nic_offset += nic_delta_between_nics;
			}
			continue;
		}

		for (i = 0 ; i < QMAN_STREAMS ; i++) {
			internal_q_index = GAUDI_QUEUE_ID_NIC_0_0 +
						nic_id * QMAN_STREAMS + i;
			q = &gaudi->internal_qmans[internal_q_index];
			qman_base_addr = (u64) q->pq_dma_addr;
			gaudi_init_nic_qman(hdev, nic_offset, (i & 0x3),
						qman_base_addr, nic_id);
		}

		/* Enable the QMAN */
		WREG32(mmNIC0_QM0_GLBL_CFG0 + nic_offset, NIC_QMAN_ENABLE);

		nic_offset += nic_delta_between_qmans;
		if (nic_id & 1) {
			nic_offset -= (nic_delta_between_qmans * 2);
			nic_offset += nic_delta_between_nics;
		}

		gaudi->hw_cap_initialized |= 1 << (HW_CAP_NIC_SHIFT + nic_id);
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

static void gaudi_disable_nic_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 nic_mask, nic_offset = 0;
	u32 nic_delta_between_qmans =
			mmNIC0_QM1_GLBL_CFG0 - mmNIC0_QM0_GLBL_CFG0;
	u32 nic_delta_between_nics =
			mmNIC1_QM0_GLBL_CFG0 - mmNIC0_QM0_GLBL_CFG0;
	int nic_id;

	for (nic_id = 0 ; nic_id < NIC_NUMBER_OF_ENGINES ; nic_id++) {
		nic_mask = 1 << (HW_CAP_NIC_SHIFT + nic_id);

		if (gaudi->hw_cap_initialized & nic_mask)
			WREG32(mmNIC0_QM0_GLBL_CFG0 + nic_offset, 0);

		nic_offset += nic_delta_between_qmans;
		if (nic_id & 1) {
			nic_offset -= (nic_delta_between_qmans * 2);
			nic_offset += nic_delta_between_nics;
		}
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

static void gaudi_stop_nic_qmans(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	/* Stop upper CPs of QMANs */

	if (gaudi->hw_cap_initialized & HW_CAP_NIC0)
		WREG32(mmNIC0_QM0_GLBL_CFG1,
				NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);

	if (gaudi->hw_cap_initialized & HW_CAP_NIC1)
		WREG32(mmNIC0_QM1_GLBL_CFG1,
				NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);

	if (gaudi->hw_cap_initialized & HW_CAP_NIC2)
		WREG32(mmNIC1_QM0_GLBL_CFG1,
				NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);

	if (gaudi->hw_cap_initialized & HW_CAP_NIC3)
		WREG32(mmNIC1_QM1_GLBL_CFG1,
				NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);

	if (gaudi->hw_cap_initialized & HW_CAP_NIC4)
		WREG32(mmNIC2_QM0_GLBL_CFG1,
				NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);

	if (gaudi->hw_cap_initialized & HW_CAP_NIC5)
		WREG32(mmNIC2_QM1_GLBL_CFG1,
				NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);

	if (gaudi->hw_cap_initialized & HW_CAP_NIC6)
		WREG32(mmNIC3_QM0_GLBL_CFG1,
				NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);

	if (gaudi->hw_cap_initialized & HW_CAP_NIC7)
		WREG32(mmNIC3_QM1_GLBL_CFG1,
				NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);

	if (gaudi->hw_cap_initialized & HW_CAP_NIC8)
		WREG32(mmNIC4_QM0_GLBL_CFG1,
				NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);

	if (gaudi->hw_cap_initialized & HW_CAP_NIC9)
		WREG32(mmNIC4_QM1_GLBL_CFG1,
				NIC0_QM0_GLBL_CFG1_PQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CQF_STOP_MASK |
				NIC0_QM0_GLBL_CFG1_CP_STOP_MASK);
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

static void gaudi_disable_clock_gating(struct hl_device *hdev)
{
	u32 qman_offset;
	int i;

	if (hdev->asic_prop.fw_security_enabled)
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

static void gaudi_halt_engines(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	u32 wait_timeout_ms;

	if (hdev->pldm)
		wait_timeout_ms = GAUDI_PLDM_RESET_WAIT_MSEC;
	else
		wait_timeout_ms = GAUDI_RESET_WAIT_MSEC;

	if (fw_reset)
		goto skip_engines;

	gaudi_stop_nic_qmans(hdev);
	gaudi_stop_mme_qmans(hdev);
	gaudi_stop_tpc_qmans(hdev);
	gaudi_stop_hbm_dma_qmans(hdev);
	gaudi_stop_pci_dma_qmans(hdev);

	msleep(wait_timeout_ms);

	gaudi_pci_dma_stall(hdev);
	gaudi_hbm_dma_stall(hdev);
	gaudi_tpc_stall(hdev);
	gaudi_mme_stall(hdev);

	msleep(wait_timeout_ms);

	gaudi_disable_nic_qmans(hdev);
	gaudi_disable_mme_qmans(hdev);
	gaudi_disable_tpc_qmans(hdev);
	gaudi_disable_hbm_dma_qmans(hdev);
	gaudi_disable_pci_dma_qmans(hdev);

	gaudi_disable_timestamp(hdev);

skip_engines:
	gaudi_disable_msi(hdev);
}

static int gaudi_mmu_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi_device *gaudi = hdev->asic_specific;
	u64 hop0_addr;
	int rc, i;

	if (gaudi->hw_cap_initialized & HW_CAP_MMU)
		return 0;

	for (i = 0 ; i < prop->max_asid ; i++) {
		hop0_addr = prop->mmu_pgt_addr +
				(i * prop->dmmu.hop_table_size);

		rc = gaudi_mmu_update_asid_hop0_addr(hdev, i, hop0_addr);
		if (rc) {
			dev_err(hdev->dev,
				"failed to set hop0 addr for asid %d\n", i);
			return rc;
		}
	}

	/* init MMU cache manage page */
	WREG32(mmSTLB_CACHE_INV_BASE_39_8, prop->mmu_cache_mng_addr >> 8);
	WREG32(mmSTLB_CACHE_INV_BASE_49_40, prop->mmu_cache_mng_addr >> 40);

	/* mem cache invalidation */
	WREG32(mmSTLB_MEM_CACHE_INVALIDATION, 1);

	rc = hl_mmu_invalidate_cache(hdev, true, 0);
	if (rc)
		return rc;

	WREG32(mmMMU_UP_MMU_ENABLE, 1);
	WREG32(mmMMU_UP_SPI_MASK, 0xF);

	WREG32(mmSTLB_HOP_CONFIGURATION, 0x30440);

	/*
	 * The H/W expects the first PI after init to be 1. After wraparound
	 * we'll write 0.
	 */
	gaudi->mmu_cache_inv_pi = 1;

	gaudi->hw_cap_initialized |= HW_CAP_MMU;

	return 0;
}

static int gaudi_load_firmware_to_device(struct hl_device *hdev)
{
	void __iomem *dst;

	dst = hdev->pcie_bar[HBM_BAR_ID] + LINUX_FW_OFFSET;

	return hl_fw_load_fw_to_device(hdev, GAUDI_LINUX_FW_FILE, dst, 0, 0);
}

static int gaudi_load_boot_fit_to_device(struct hl_device *hdev)
{
	void __iomem *dst;

	dst = hdev->pcie_bar[SRAM_BAR_ID] + BOOT_FIT_SRAM_OFFSET;

	return hl_fw_load_fw_to_device(hdev, GAUDI_BOOT_FIT_FILE, dst, 0, 0);
}

static void gaudi_init_dynamic_firmware_loader(struct hl_device *hdev)
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

	dynamic_loader->wait_for_bl_timeout = GAUDI_WAIT_FOR_BL_TIMEOUT_USEC;
}

static void gaudi_init_static_firmware_loader(struct hl_device *hdev)
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
	static_loader->cpu_reset_wait_msec = hdev->pldm ?
			GAUDI_PLDM_RESET_WAIT_MSEC :
			GAUDI_CPU_RESET_WAIT_MSEC;
}

static void gaudi_init_firmware_preload_params(struct hl_device *hdev)
{
	struct pre_fw_load_props *pre_fw_load = &hdev->fw_loader.pre_fw_load;

	pre_fw_load->cpu_boot_status_reg = mmPSOC_GLOBAL_CONF_CPU_BOOT_STATUS;
	pre_fw_load->sts_boot_dev_sts0_reg = mmCPU_BOOT_DEV_STS0;
	pre_fw_load->sts_boot_dev_sts1_reg = mmCPU_BOOT_DEV_STS1;
	pre_fw_load->boot_err0_reg = mmCPU_BOOT_ERR0;
	pre_fw_load->boot_err1_reg = mmCPU_BOOT_ERR1;
	pre_fw_load->wait_for_preboot_timeout = GAUDI_BOOT_FIT_REQ_TIMEOUT_USEC;
}

static void gaudi_init_firmware_loader(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct fw_load_mgr *fw_loader = &hdev->fw_loader;

	/* fill common fields */
	fw_loader->fw_comp_loaded = FW_TYPE_NONE;
	fw_loader->boot_fit_img.image_name = GAUDI_BOOT_FIT_FILE;
	fw_loader->linux_img.image_name = GAUDI_LINUX_FW_FILE;
	fw_loader->cpu_timeout = GAUDI_CPU_TIMEOUT_USEC;
	fw_loader->boot_fit_timeout = GAUDI_BOOT_FIT_REQ_TIMEOUT_USEC;
	fw_loader->skip_bmc = !hdev->bmc_enable;
	fw_loader->sram_bar_id = SRAM_BAR_ID;
	fw_loader->dram_bar_id = HBM_BAR_ID;

	if (prop->dynamic_fw_load)
		gaudi_init_dynamic_firmware_loader(hdev);
	else
		gaudi_init_static_firmware_loader(hdev);
}

static int gaudi_init_cpu(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int rc;

	if (!(hdev->fw_components & FW_TYPE_PREBOOT_CPU))
		return 0;

	if (gaudi->hw_cap_initialized & HW_CAP_CPU)
		return 0;

	/*
	 * The device CPU works with 40 bits addresses.
	 * This register sets the extension to 50 bits.
	 */
	if (!hdev->asic_prop.fw_security_enabled)
		WREG32(mmCPU_IF_CPU_MSB_ADDR, hdev->cpu_pci_msb_addr);

	rc = hl_fw_init_cpu(hdev);

	if (rc)
		return rc;

	gaudi->hw_cap_initialized |= HW_CAP_CPU;

	return 0;
}

static int gaudi_init_cpu_queues(struct hl_device *hdev, u32 cpu_timeout)
{
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 status, irq_handler_offset;
	struct hl_eq *eq;
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

	WREG32(mmCPU_IF_QUEUE_INIT, PQ_INIT_STATUS_READY_FOR_CP_SINGLE_MSI);

	irq_handler_offset = prop->gic_interrupts_enable ?
			mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR :
			le32_to_cpu(dyn_regs->gic_host_pi_upd_irq);

	WREG32(irq_handler_offset,
		gaudi_irq_map_table[GAUDI_EVENT_PI_UPDATE].cpu_id);

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

	/* update FW application security bits */
	if (prop->fw_cpu_boot_dev_sts0_valid)
		prop->fw_app_cpu_boot_dev_sts0 = RREG32(mmCPU_BOOT_DEV_STS0);
	if (prop->fw_cpu_boot_dev_sts1_valid)
		prop->fw_app_cpu_boot_dev_sts1 = RREG32(mmCPU_BOOT_DEV_STS1);

	gaudi->hw_cap_initialized |= HW_CAP_CPU_Q;
	return 0;
}

static void gaudi_pre_hw_init(struct hl_device *hdev)
{
	/* Perform read from the device to make sure device is up */
	RREG32(mmHW_STATE);

	if (!hdev->asic_prop.fw_security_enabled) {
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
	}

	/*
	 * Let's mark in the H/W that we have reached this point. We check
	 * this value in the reset_before_init function to understand whether
	 * we need to reset the chip before doing H/W init. This register is
	 * cleared by the H/W upon H/W reset
	 */
	WREG32(mmHW_STATE, HL_DEVICE_HW_STATE_DIRTY);
}

static int gaudi_hw_init(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int rc;

	gaudi_pre_hw_init(hdev);

	/* If iATU is done by FW, the HBM bar ALWAYS points to DRAM_PHYS_BASE.
	 * So we set it here and if anyone tries to move it later to
	 * a different address, there will be an error
	 */
	if (hdev->asic_prop.iatu_done_by_fw)
		gaudi->hbm_bar_cur_addr = DRAM_PHYS_BASE;

	/*
	 * Before pushing u-boot/linux to device, need to set the hbm bar to
	 * base address of dram
	 */
	if (gaudi_set_hbm_bar_base(hdev, DRAM_PHYS_BASE) == U64_MAX) {
		dev_err(hdev->dev,
			"failed to map HBM bar to DRAM base address\n");
		return -EIO;
	}

	rc = gaudi_init_cpu(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU\n");
		return rc;
	}

	/* In case the clock gating was enabled in preboot we need to disable
	 * it here before touching the MME/TPC registers.
	 */
	gaudi_disable_clock_gating(hdev);

	/* SRAM scrambler must be initialized after CPU is running from HBM */
	gaudi_init_scrambler_sram(hdev);

	/* This is here just in case we are working without CPU */
	gaudi_init_scrambler_hbm(hdev);

	gaudi_init_golden_registers(hdev);

	rc = gaudi_mmu_init(hdev);
	if (rc)
		return rc;

	gaudi_init_security(hdev);

	gaudi_init_pci_dma_qmans(hdev);

	gaudi_init_hbm_dma_qmans(hdev);

	gaudi_init_mme_qmans(hdev);

	gaudi_init_tpc_qmans(hdev);

	gaudi_init_nic_qmans(hdev);

	gaudi_enable_timestamp(hdev);

	/* MSI must be enabled before CPU queues and NIC are initialized */
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
	RREG32(mmHW_STATE);

	return 0;

disable_msi:
	gaudi_disable_msi(hdev);
disable_queues:
	gaudi_disable_mme_qmans(hdev);
	gaudi_disable_pci_dma_qmans(hdev);

	return rc;
}

static int gaudi_hw_fini(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 status, reset_timeout_ms, cpu_timeout_ms, irq_handler_offset;
	struct gaudi_device *gaudi = hdev->asic_specific;
	bool driver_performs_reset;

	if (!hard_reset) {
		dev_err(hdev->dev, "GAUDI doesn't support soft-reset\n");
		return 0;
	}

	if (hdev->pldm) {
		reset_timeout_ms = GAUDI_PLDM_HRESET_TIMEOUT_MSEC;
		cpu_timeout_ms = GAUDI_PLDM_RESET_WAIT_MSEC;
	} else {
		reset_timeout_ms = GAUDI_RESET_TIMEOUT_MSEC;
		cpu_timeout_ms = GAUDI_CPU_RESET_WAIT_MSEC;
	}

	if (fw_reset) {
		dev_dbg(hdev->dev,
			"Firmware performs HARD reset, going to wait %dms\n",
			reset_timeout_ms);

		goto skip_reset;
	}

	driver_performs_reset = !!(!hdev->asic_prop.fw_security_enabled &&
					!hdev->asic_prop.hard_reset_done_by_fw);

	/* Set device to handle FLR by H/W as we will put the device CPU to
	 * halt mode
	 */
	if (driver_performs_reset)
		WREG32(mmPCIE_AUX_FLR_CTRL, (PCIE_AUX_FLR_CTRL_HW_CTRL_MASK |
					PCIE_AUX_FLR_CTRL_INT_MASK_MASK));

	/* If linux is loaded in the device CPU we need to communicate with it
	 * via the GIC. Otherwise, we need to use COMMS or the MSG_TO_CPU
	 * registers in case of old F/Ws
	 */
	if (hdev->fw_loader.fw_comp_loaded & FW_TYPE_LINUX) {
		irq_handler_offset = hdev->asic_prop.gic_interrupts_enable ?
				mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR :
				le32_to_cpu(dyn_regs->gic_host_halt_irq);

		WREG32(irq_handler_offset,
			gaudi_irq_map_table[GAUDI_EVENT_HALT_MACHINE].cpu_id);

		/* This is a hail-mary attempt to revive the card in the small chance that the
		 * f/w has experienced a watchdog event, which caused it to return back to preboot.
		 * In that case, triggering reset through GIC won't help. We need to trigger the
		 * reset as if Linux wasn't loaded.
		 *
		 * We do it only if the reset cause was HB, because that would be the indication
		 * of such an event.
		 *
		 * In case watchdog hasn't expired but we still got HB, then this won't do any
		 * damage.
		 */
		if (hdev->reset_info.curr_reset_cause == HL_RESET_CAUSE_HEARTBEAT) {
			if (hdev->asic_prop.hard_reset_done_by_fw)
				hl_fw_ask_hard_reset_without_linux(hdev);
			else
				hl_fw_ask_halt_machine_without_linux(hdev);
		}
	} else {
		if (hdev->asic_prop.hard_reset_done_by_fw)
			hl_fw_ask_hard_reset_without_linux(hdev);
		else
			hl_fw_ask_halt_machine_without_linux(hdev);
	}

	if (driver_performs_reset) {

		/* Configure the reset registers. Must be done as early as
		 * possible in case we fail during H/W initialization
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

		msleep(cpu_timeout_ms);

		/* Tell ASIC not to re-initialize PCIe */
		WREG32(mmPREBOOT_PCIE_EN, LKD_HARD_RESET_MAGIC);

		/* Restart BTL/BLR upon hard-reset */
		WREG32(mmPSOC_GLOBAL_CONF_BOOT_SEQ_RE_START, 1);

		WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST,
			1 << PSOC_GLOBAL_CONF_SW_ALL_RST_IND_SHIFT);

		dev_dbg(hdev->dev,
			"Issued HARD reset command, going to wait %dms\n",
			reset_timeout_ms);
	} else {
		dev_dbg(hdev->dev,
			"Firmware performs HARD reset, going to wait %dms\n",
			reset_timeout_ms);
	}

skip_reset:
	/*
	 * After hard reset, we can't poll the BTM_FSM register because the PSOC
	 * itself is in reset. Need to wait until the reset is deasserted
	 */
	msleep(reset_timeout_ms);

	status = RREG32(mmPSOC_GLOBAL_CONF_BTM_FSM);
	if (status & PSOC_GLOBAL_CONF_BTM_FSM_STATE_MASK) {
		dev_err(hdev->dev, "Timeout while waiting for device to reset 0x%x\n", status);
		return -ETIMEDOUT;
	}

	if (gaudi) {
		gaudi->hw_cap_initialized &= ~(HW_CAP_CPU | HW_CAP_CPU_Q | HW_CAP_HBM |
						HW_CAP_PCI_DMA | HW_CAP_MME | HW_CAP_TPC_MASK |
						HW_CAP_HBM_DMA | HW_CAP_PLL | HW_CAP_NIC_MASK |
						HW_CAP_MMU | HW_CAP_SRAM_SCRAMBLER |
						HW_CAP_HBM_SCRAMBLER);

		memset(gaudi->events_stat, 0, sizeof(gaudi->events_stat));

		hdev->device_cpu_is_halted = false;
	}
	return 0;
}

static int gaudi_suspend(struct hl_device *hdev)
{
	int rc;

	rc = hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_DISABLE_PCI_ACCESS, 0x0);
	if (rc)
		dev_err(hdev->dev, "Failed to disable PCI access from CPU\n");

	return rc;
}

static int gaudi_resume(struct hl_device *hdev)
{
	return gaudi_init_iatu(hdev);
}

static int gaudi_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
			void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	int rc;

	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP |
			VM_DONTCOPY | VM_NORESERVE);

	rc = dma_mmap_coherent(hdev->dev, vma, cpu_addr,
				(dma_addr - HOST_PHYS_BASE), size);
	if (rc)
		dev_err(hdev->dev, "dma_mmap_coherent error %d", rc);

	return rc;
}

static void gaudi_ring_doorbell(struct hl_device *hdev, u32 hw_queue_id, u32 pi)
{
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 db_reg_offset, db_value, dma_qm_offset, q_off, irq_handler_offset;
	struct gaudi_device *gaudi = hdev->asic_specific;
	bool invalid_queue = false;
	int dma_id;

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
		dma_id = gaudi_dma_assignment[GAUDI_HBM_DMA_4];
		dma_qm_offset = dma_id * DMA_QMAN_OFFSET;
		q_off = dma_qm_offset + ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmDMA0_QM_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_DMA_6_0...GAUDI_QUEUE_ID_DMA_6_3:
		dma_id = gaudi_dma_assignment[GAUDI_HBM_DMA_5];
		dma_qm_offset = dma_id * DMA_QMAN_OFFSET;
		q_off = dma_qm_offset + ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmDMA0_QM_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_DMA_7_0...GAUDI_QUEUE_ID_DMA_7_3:
		dma_id = gaudi_dma_assignment[GAUDI_HBM_DMA_6];
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

	case GAUDI_QUEUE_ID_NIC_0_0...GAUDI_QUEUE_ID_NIC_0_3:
		if (!(gaudi->hw_cap_initialized & HW_CAP_NIC0))
			invalid_queue = true;

		q_off = ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmNIC0_QM0_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_NIC_1_0...GAUDI_QUEUE_ID_NIC_1_3:
		if (!(gaudi->hw_cap_initialized & HW_CAP_NIC1))
			invalid_queue = true;

		q_off = ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmNIC0_QM1_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_NIC_2_0...GAUDI_QUEUE_ID_NIC_2_3:
		if (!(gaudi->hw_cap_initialized & HW_CAP_NIC2))
			invalid_queue = true;

		q_off = ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmNIC1_QM0_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_NIC_3_0...GAUDI_QUEUE_ID_NIC_3_3:
		if (!(gaudi->hw_cap_initialized & HW_CAP_NIC3))
			invalid_queue = true;

		q_off = ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmNIC1_QM1_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_NIC_4_0...GAUDI_QUEUE_ID_NIC_4_3:
		if (!(gaudi->hw_cap_initialized & HW_CAP_NIC4))
			invalid_queue = true;

		q_off = ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmNIC2_QM0_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_NIC_5_0...GAUDI_QUEUE_ID_NIC_5_3:
		if (!(gaudi->hw_cap_initialized & HW_CAP_NIC5))
			invalid_queue = true;

		q_off = ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmNIC2_QM1_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_NIC_6_0...GAUDI_QUEUE_ID_NIC_6_3:
		if (!(gaudi->hw_cap_initialized & HW_CAP_NIC6))
			invalid_queue = true;

		q_off = ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmNIC3_QM0_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_NIC_7_0...GAUDI_QUEUE_ID_NIC_7_3:
		if (!(gaudi->hw_cap_initialized & HW_CAP_NIC7))
			invalid_queue = true;

		q_off = ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmNIC3_QM1_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_NIC_8_0...GAUDI_QUEUE_ID_NIC_8_3:
		if (!(gaudi->hw_cap_initialized & HW_CAP_NIC8))
			invalid_queue = true;

		q_off = ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmNIC4_QM0_PQ_PI_0 + q_off;
		break;

	case GAUDI_QUEUE_ID_NIC_9_0...GAUDI_QUEUE_ID_NIC_9_3:
		if (!(gaudi->hw_cap_initialized & HW_CAP_NIC9))
			invalid_queue = true;

		q_off = ((hw_queue_id - 1) & 0x3) * 4;
		db_reg_offset = mmNIC4_QM1_PQ_PI_0 + q_off;
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

	if (hw_queue_id == GAUDI_QUEUE_ID_CPU_PQ) {
		/* make sure device CPU will read latest data from host */
		mb();

		irq_handler_offset = hdev->asic_prop.gic_interrupts_enable ?
				mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR :
				le32_to_cpu(dyn_regs->gic_host_pi_upd_irq);

		WREG32(irq_handler_offset,
			gaudi_irq_map_table[GAUDI_EVENT_PI_UPDATE].cpu_id);
	}
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

static int gaudi_scrub_device_dram(struct hl_device *hdev, u64 val)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 cur_addr = prop->dram_user_base_address;
	u32 chunk_size, busy;
	int rc, dma_id;

	while (cur_addr < prop->dram_end_address) {
		for (dma_id = 0 ; dma_id < DMA_NUMBER_OF_CHANNELS ; dma_id++) {
			u32 dma_offset = dma_id * DMA_CORE_OFFSET;

			chunk_size =
			min((u64)SZ_2G, prop->dram_end_address - cur_addr);

			dev_dbg(hdev->dev,
				"Doing HBM scrubbing for 0x%09llx - 0x%09llx\n",
				cur_addr, cur_addr + chunk_size);

			WREG32(mmDMA0_CORE_SRC_BASE_LO + dma_offset,
					lower_32_bits(val));
			WREG32(mmDMA0_CORE_SRC_BASE_HI + dma_offset,
					upper_32_bits(val));
			WREG32(mmDMA0_CORE_DST_BASE_LO + dma_offset,
						lower_32_bits(cur_addr));
			WREG32(mmDMA0_CORE_DST_BASE_HI + dma_offset,
						upper_32_bits(cur_addr));
			WREG32(mmDMA0_CORE_DST_TSIZE_0 + dma_offset,
					chunk_size);
			WREG32(mmDMA0_CORE_COMMIT + dma_offset,
					((1 << DMA0_CORE_COMMIT_LIN_SHIFT) |
					(1 << DMA0_CORE_COMMIT_MEM_SET_SHIFT)));

			cur_addr += chunk_size;

			if (cur_addr == prop->dram_end_address)
				break;
		}

		for (dma_id = 0 ; dma_id < DMA_NUMBER_OF_CHANNELS ; dma_id++) {
			u32 dma_offset = dma_id * DMA_CORE_OFFSET;

			rc = hl_poll_timeout(
				hdev,
				mmDMA0_CORE_STS0 + dma_offset,
				busy,
				((busy & DMA0_CORE_STS0_BUSY_MASK) == 0),
				1000,
				HBM_SCRUBBING_TIMEOUT_US);

			if (rc) {
				dev_err(hdev->dev,
					"DMA Timeout during HBM scrubbing of DMA #%d\n",
					dma_id);
				return -EIO;
			}
		}
	}

	return 0;
}

static int gaudi_scrub_device_mem(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 wait_to_idle_time = HBM_SCRUBBING_TIMEOUT_US;
	u64 addr, size, val = hdev->memory_scrub_val;
	ktime_t timeout;
	int rc = 0;

	if (!hdev->memory_scrub)
		return 0;

	timeout = ktime_add_us(ktime_get(), wait_to_idle_time);
	while (!hdev->asic_funcs->is_device_idle(hdev, NULL, 0, NULL)) {
		if (ktime_compare(ktime_get(), timeout) > 0) {
			dev_err(hdev->dev, "waiting for idle timeout\n");
			return -ETIMEDOUT;
		}
		usleep_range((1000 >> 2) + 1, 1000);
	}

	/* Scrub SRAM */
	addr = prop->sram_user_base_address;
	size = hdev->pldm ? 0x10000 : prop->sram_size - SRAM_USER_BASE_OFFSET;

	dev_dbg(hdev->dev, "Scrubbing SRAM: 0x%09llx - 0x%09llx val: 0x%llx\n",
			addr, addr + size, val);
	rc = gaudi_memset_device_memory(hdev, addr, size, val);
	if (rc) {
		dev_err(hdev->dev, "Failed to clear SRAM (%d)\n", rc);
		return rc;
	}

	/* Scrub HBM using all DMA channels in parallel */
	rc = gaudi_scrub_device_dram(hdev, val);
	if (rc) {
		dev_err(hdev->dev, "Failed to clear HBM (%d)\n", rc);
		return rc;
	}

	return 0;
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
				u16 len, u32 timeout, u64 *result)
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

	fence_ptr = hl_asic_dma_pool_zalloc(hdev, 4, GFP_KERNEL, &fence_dma_addr);
	if (!fence_ptr) {
		dev_err(hdev->dev,
			"Failed to allocate memory for H/W queue %d testing\n",
			hw_queue_id);
		return -ENOMEM;
	}

	*fence_ptr = 0;

	fence_pkt = hl_asic_dma_pool_zalloc(hdev, sizeof(struct packet_msg_prot), GFP_KERNEL,
						&pkt_dma_addr);
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
	hl_asic_dma_pool_free(hdev, (void *) fence_pkt, pkt_dma_addr);
free_fence_ptr:
	hl_asic_dma_pool_free(hdev, (void *) fence_ptr, fence_dma_addr);
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

static u32 gaudi_get_dma_desc_list_size(struct hl_device *hdev, struct sg_table *sgt)
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

	userptr = kzalloc(sizeof(*userptr), GFP_KERNEL);
	if (!userptr)
		return -ENOMEM;

	rc = hl_pin_host_memory(hdev, addr, le32_to_cpu(user_dma_pkt->tsize),
				userptr);
	if (rc)
		goto free_userptr;

	list_add_tail(&userptr->job_node, parser->job_userptr_list);

	rc = hl_dma_map_sgtable(hdev, userptr->sgt, dir);
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
	list_del(&userptr->job_node);
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
	 * 1. Optional NOP padding for cacheline alignment
	 * 2. A packet that will act as a completion packet
	 * 3. A packet that will generate MSI interrupt
	 */
	if (parser->completion)
		parser->patched_cb_size += gaudi_get_patched_cb_extra_size(
			parser->patched_cb_size);

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
	u64 handle;
	u32 patched_cb_size;
	struct hl_cb *user_cb;
	int rc;

	/*
	 * The new CB should have space at the end for two MSG_PROT packets:
	 * 1. Optional NOP padding for cacheline alignment
	 * 2. A packet that will act as a completion packet
	 * 3. A packet that will generate MSI interrupt
	 */
	if (parser->completion)
		parser->patched_cb_size = parser->user_cb_size +
				gaudi_get_patched_cb_extra_size(parser->user_cb_size);
	else
		parser->patched_cb_size = parser->user_cb_size;

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
	/* hl_cb_get should never fail */
	if (!parser->patched_cb) {
		dev_crit(hdev->dev, "DMA CB handle invalid 0x%llx\n", handle);
		rc = -EFAULT;
		goto out;
	}

	/*
	 * We are protected from overflow because the check
	 * "parser->user_cb_size <= parser->user_cb->size" was done in get_cb_from_cs_chunk()
	 * in the common code. That check is done only if is_kernel_allocated_cb is true.
	 *
	 * There is no option to reach here without going through that check because:
	 * 1. validate_queue_index() assigns true to is_kernel_allocated_cb for any submission to
	 *    an external queue.
	 * 2. For Gaudi, we only parse CBs that were submitted to the external queues.
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
	hl_cb_destroy(&hdev->kernel_mem_mgr, handle);

	return rc;
}

static int gaudi_parse_cb_no_mmu(struct hl_device *hdev,
		struct hl_cs_parser *parser)
{
	u64 handle;
	int rc;

	rc = gaudi_validate_cb(hdev, parser, false);

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
	hl_cb_destroy(&hdev->kernel_mem_mgr, handle);

free_userptr:
	if (rc)
		hl_userptr_delete_list(hdev, parser->job_userptr_list);
	return rc;
}

static int gaudi_parse_cb_no_ext_queue(struct hl_device *hdev,
					struct hl_cs_parser *parser)
{
	struct asic_fixed_properties *asic_prop = &hdev->asic_prop;
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 nic_queue_offset, nic_mask_q_id;

	if ((parser->hw_queue_id >= GAUDI_QUEUE_ID_NIC_0_0) &&
			(parser->hw_queue_id <= GAUDI_QUEUE_ID_NIC_9_3)) {
		nic_queue_offset = parser->hw_queue_id - GAUDI_QUEUE_ID_NIC_0_0;
		nic_mask_q_id = 1 << (HW_CAP_NIC_SHIFT + (nic_queue_offset >> 2));

		if (!(gaudi->hw_cap_initialized & nic_mask_q_id)) {
			dev_err(hdev->dev, "h/w queue %d is disabled\n", parser->hw_queue_id);
			return -EINVAL;
		}
	}

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

static void gaudi_add_end_of_cb_packets(struct hl_device *hdev, void *kernel_address,
				u32 len, u32 original_len, u64 cq_addr, u32 cq_val,
				u32 msi_vec, bool eb)
{
	struct packet_msg_prot *cq_pkt;
	struct packet_nop *cq_padding;
	u64 msi_addr;
	u32 tmp;

	cq_padding = kernel_address + original_len;
	cq_pkt = kernel_address + len - (sizeof(struct packet_msg_prot) * 2);

	while ((void *)cq_padding < (void *)cq_pkt) {
		cq_padding->ctl = cpu_to_le32(FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_NOP));
		cq_padding++;
	}

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
	msi_addr = hdev->pdev ? mmPCIE_CORE_MSI_REQ : mmPCIE_MSI_INTR_0 + msi_vec * 4;
	cq_pkt->addr = cpu_to_le64(CFG_BASE + msi_addr);
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
	atomic_inc(&job->user_cb->cs_cnt);
	job->user_cb_size = cb_size;
	job->hw_queue_id = GAUDI_QUEUE_ID_DMA_0_0;
	job->patched_cb = job->user_cb;
	job->job_cb_size = job->user_cb_size + sizeof(struct packet_msg_prot);

	hl_debugfs_add_job(hdev, job);

	rc = gaudi_send_job_on_qman0(hdev, job);
	hl_debugfs_remove_job(hdev, job);
	kfree(job);
	atomic_dec(&cb->cs_cnt);

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
	hl_cb_destroy(&hdev->kernel_mem_mgr, cb->buf->handle);

	return rc;
}

static int gaudi_memset_registers(struct hl_device *hdev, u64 reg_base,
					u32 num_regs, u32 val)
{
	struct packet_msg_long *pkt;
	struct hl_cs_job *job;
	u32 cb_size, ctl;
	struct hl_cb *cb;
	int i, rc;

	cb_size = (sizeof(*pkt) * num_regs) + sizeof(struct packet_msg_prot);

	if (cb_size > SZ_2M) {
		dev_err(hdev->dev, "CB size must be smaller than %uMB", SZ_2M);
		return -ENOMEM;
	}

	cb = hl_cb_kernel_create(hdev, cb_size, false);
	if (!cb)
		return -EFAULT;

	pkt = cb->kernel_address;

	ctl = FIELD_PREP(GAUDI_PKT_LONG_CTL_OP_MASK, 0); /* write the value */
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_MSG_LONG);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_EB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_RB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_MB_MASK, 1);

	for (i = 0; i < num_regs ; i++, pkt++) {
		pkt->ctl = cpu_to_le32(ctl);
		pkt->value = cpu_to_le32(val);
		pkt->addr = cpu_to_le64(reg_base + (i * 4));
	}

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
	job->hw_queue_id = GAUDI_QUEUE_ID_DMA_0_0;
	job->patched_cb = job->user_cb;
	job->job_cb_size = cb_size;

	hl_debugfs_add_job(hdev, job);

	rc = gaudi_send_job_on_qman0(hdev, job);
	hl_debugfs_remove_job(hdev, job);
	kfree(job);
	atomic_dec(&cb->cs_cnt);

release_cb:
	hl_cb_put(cb);
	hl_cb_destroy(&hdev->kernel_mem_mgr, cb->buf->handle);

	return rc;
}

static int gaudi_restore_sm_registers(struct hl_device *hdev)
{
	u64 base_addr;
	u32 num_regs;
	int rc;

	base_addr = CFG_BASE + mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0;
	num_regs = NUM_OF_SOB_IN_BLOCK;
	rc = gaudi_memset_registers(hdev, base_addr, num_regs, 0);
	if (rc) {
		dev_err(hdev->dev, "failed resetting SM registers");
		return -ENOMEM;
	}

	base_addr = CFG_BASE +  mmSYNC_MNGR_E_S_SYNC_MNGR_OBJS_SOB_OBJ_0;
	num_regs = NUM_OF_SOB_IN_BLOCK;
	rc = gaudi_memset_registers(hdev, base_addr, num_regs, 0);
	if (rc) {
		dev_err(hdev->dev, "failed resetting SM registers");
		return -ENOMEM;
	}

	base_addr = CFG_BASE +  mmSYNC_MNGR_W_N_SYNC_MNGR_OBJS_SOB_OBJ_0;
	num_regs = NUM_OF_SOB_IN_BLOCK;
	rc = gaudi_memset_registers(hdev, base_addr, num_regs, 0);
	if (rc) {
		dev_err(hdev->dev, "failed resetting SM registers");
		return -ENOMEM;
	}

	base_addr = CFG_BASE +  mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_STATUS_0;
	num_regs = NUM_OF_MONITORS_IN_BLOCK;
	rc = gaudi_memset_registers(hdev, base_addr, num_regs, 0);
	if (rc) {
		dev_err(hdev->dev, "failed resetting SM registers");
		return -ENOMEM;
	}

	base_addr = CFG_BASE +  mmSYNC_MNGR_E_S_SYNC_MNGR_OBJS_MON_STATUS_0;
	num_regs = NUM_OF_MONITORS_IN_BLOCK;
	rc = gaudi_memset_registers(hdev, base_addr, num_regs, 0);
	if (rc) {
		dev_err(hdev->dev, "failed resetting SM registers");
		return -ENOMEM;
	}

	base_addr = CFG_BASE +  mmSYNC_MNGR_W_N_SYNC_MNGR_OBJS_MON_STATUS_0;
	num_regs = NUM_OF_MONITORS_IN_BLOCK;
	rc = gaudi_memset_registers(hdev, base_addr, num_regs, 0);
	if (rc) {
		dev_err(hdev->dev, "failed resetting SM registers");
		return -ENOMEM;
	}

	base_addr = CFG_BASE +  mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_SOB_OBJ_0 +
			(GAUDI_FIRST_AVAILABLE_W_S_SYNC_OBJECT * 4);
	num_regs = NUM_OF_SOB_IN_BLOCK - GAUDI_FIRST_AVAILABLE_W_S_SYNC_OBJECT;
	rc = gaudi_memset_registers(hdev, base_addr, num_regs, 0);
	if (rc) {
		dev_err(hdev->dev, "failed resetting SM registers");
		return -ENOMEM;
	}

	base_addr = CFG_BASE +  mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_STATUS_0 +
			(GAUDI_FIRST_AVAILABLE_W_S_MONITOR * 4);
	num_regs = NUM_OF_MONITORS_IN_BLOCK - GAUDI_FIRST_AVAILABLE_W_S_MONITOR;
	rc = gaudi_memset_registers(hdev, base_addr, num_regs, 0);
	if (rc) {
		dev_err(hdev->dev, "failed resetting SM registers");
		return -ENOMEM;
	}

	return 0;
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

	for (i = 0 ; i < NIC_NUMBER_OF_ENGINES ; i++) {
		qman_offset = (i >> 1) * NIC_MACRO_QMAN_OFFSET +
				(i & 0x1) * NIC_ENGINE_QMAN_OFFSET;
		WREG32(mmNIC0_QM0_ARB_CFG_0 + qman_offset, 0);
	}
}

static int gaudi_restore_user_registers(struct hl_device *hdev)
{
	int rc;

	rc = gaudi_restore_sm_registers(hdev);
	if (rc)
		return rc;

	gaudi_restore_dma_registers(hdev);
	gaudi_restore_qm_registers(hdev);

	return 0;
}

static int gaudi_context_switch(struct hl_device *hdev, u32 asid)
{
	return 0;
}

static int gaudi_mmu_clear_pgt_range(struct hl_device *hdev)
{
	u32 size = hdev->asic_prop.mmu_pgt_size +
			hdev->asic_prop.mmu_cache_mng_size;
	struct gaudi_device *gaudi = hdev->asic_specific;
	u64 addr = hdev->asic_prop.mmu_pgt_addr;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MMU))
		return 0;

	return gaudi_memset_device_memory(hdev, addr, size, 0);
}

static void gaudi_restore_phase_topology(struct hl_device *hdev)
{

}

static int gaudi_dma_core_transfer(struct hl_device *hdev, int dma_id, u64 addr,
					u32 size_to_dma, dma_addr_t dma_addr)
{
	u32 err_cause, val;
	u64 dma_offset;
	int rc;

	dma_offset = dma_id * DMA_CORE_OFFSET;

	WREG32(mmDMA0_CORE_SRC_BASE_LO + dma_offset, lower_32_bits(addr));
	WREG32(mmDMA0_CORE_SRC_BASE_HI + dma_offset, upper_32_bits(addr));
	WREG32(mmDMA0_CORE_DST_BASE_LO + dma_offset, lower_32_bits(dma_addr));
	WREG32(mmDMA0_CORE_DST_BASE_HI + dma_offset, upper_32_bits(dma_addr));
	WREG32(mmDMA0_CORE_DST_TSIZE_0 + dma_offset, size_to_dma);
	WREG32(mmDMA0_CORE_COMMIT + dma_offset,
			(1 << DMA0_CORE_COMMIT_LIN_SHIFT));

	rc = hl_poll_timeout(
		hdev,
		mmDMA0_CORE_STS0 + dma_offset,
		val,
		((val & DMA0_CORE_STS0_BUSY_MASK) == 0),
		0,
		1000000);

	if (rc) {
		dev_err(hdev->dev,
			"DMA %d timed-out during reading of 0x%llx\n",
			dma_id, addr);
		return -EIO;
	}

	/* Verify DMA is OK */
	err_cause = RREG32(mmDMA0_CORE_ERR_CAUSE + dma_offset);
	if (err_cause) {
		dev_err(hdev->dev, "DMA Failed, cause 0x%x\n", err_cause);
		dev_dbg(hdev->dev,
			"Clearing DMA0 engine from errors (cause 0x%x)\n",
			err_cause);
		WREG32(mmDMA0_CORE_ERR_CAUSE + dma_offset, err_cause);

		return -EIO;
	}

	return 0;
}

static int gaudi_debugfs_read_dma(struct hl_device *hdev, u64 addr, u32 size,
				void *blob_addr)
{
	u32 dma_core_sts0, err_cause, cfg1, size_left, pos, size_to_dma;
	u32 qm_glbl_sts0, qm_cgm_sts;
	u64 dma_offset, qm_offset;
	dma_addr_t dma_addr;
	void *kernel_addr;
	bool is_eng_idle;
	int rc = 0, dma_id;

	kernel_addr = hl_asic_dma_alloc_coherent(hdev, SZ_2M, &dma_addr, GFP_KERNEL | __GFP_ZERO);

	if (!kernel_addr)
		return -ENOMEM;

	hdev->asic_funcs->hw_queues_lock(hdev);

	dma_id = gaudi_dma_assignment[GAUDI_PCI_DMA_1];
	dma_offset = dma_id * DMA_CORE_OFFSET;
	qm_offset = dma_id * DMA_QMAN_OFFSET;
	dma_core_sts0 = RREG32(mmDMA0_CORE_STS0 + dma_offset);
	qm_glbl_sts0 = RREG32(mmDMA0_QM_GLBL_STS0 + qm_offset);
	qm_cgm_sts = RREG32(mmDMA0_QM_CGM_STS + qm_offset);
	is_eng_idle = IS_QM_IDLE(qm_glbl_sts0, qm_cgm_sts) &&
		      IS_DMA_IDLE(dma_core_sts0);

	if (!is_eng_idle) {
		dma_id = gaudi_dma_assignment[GAUDI_PCI_DMA_2];
		dma_offset = dma_id * DMA_CORE_OFFSET;
		qm_offset = dma_id * DMA_QMAN_OFFSET;
		dma_core_sts0 = RREG32(mmDMA0_CORE_STS0 + dma_offset);
		qm_glbl_sts0 = RREG32(mmDMA0_QM_GLBL_STS0 + qm_offset);
		qm_cgm_sts = RREG32(mmDMA0_QM_CGM_STS + qm_offset);
		is_eng_idle = IS_QM_IDLE(qm_glbl_sts0, qm_cgm_sts) &&
			      IS_DMA_IDLE(dma_core_sts0);

		if (!is_eng_idle) {
			dev_err_ratelimited(hdev->dev,
				"Can't read via DMA because it is BUSY\n");
			rc = -EAGAIN;
			goto out;
		}
	}

	cfg1 = RREG32(mmDMA0_QM_GLBL_CFG1 + qm_offset);
	WREG32(mmDMA0_QM_GLBL_CFG1 + qm_offset,
			0xF << DMA0_QM_GLBL_CFG1_CP_STOP_SHIFT);

	/* TODO: remove this by mapping the DMA temporary buffer to the MMU
	 * using the compute ctx ASID, if exists. If not, use the kernel ctx
	 * ASID
	 */
	WREG32_OR(mmDMA0_CORE_PROT + dma_offset, BIT(DMA0_CORE_PROT_VAL_SHIFT));

	/* Verify DMA is OK */
	err_cause = RREG32(mmDMA0_CORE_ERR_CAUSE + dma_offset);
	if (err_cause) {
		dev_dbg(hdev->dev,
			"Clearing DMA0 engine from errors (cause 0x%x)\n",
			err_cause);
		WREG32(mmDMA0_CORE_ERR_CAUSE + dma_offset, err_cause);
	}

	pos = 0;
	size_left = size;
	size_to_dma = SZ_2M;

	while (size_left > 0) {

		if (size_left < SZ_2M)
			size_to_dma = size_left;

		rc = gaudi_dma_core_transfer(hdev, dma_id, addr, size_to_dma,
						dma_addr);
		if (rc)
			break;

		memcpy(blob_addr + pos, kernel_addr, size_to_dma);

		if (size_left <= SZ_2M)
			break;

		pos += SZ_2M;
		addr += SZ_2M;
		size_left -= SZ_2M;
	}

	/* TODO: remove this by mapping the DMA temporary buffer to the MMU
	 * using the compute ctx ASID, if exists. If not, use the kernel ctx
	 * ASID
	 */
	WREG32_AND(mmDMA0_CORE_PROT + dma_offset,
			~BIT(DMA0_CORE_PROT_VAL_SHIFT));

	WREG32(mmDMA0_QM_GLBL_CFG1 + qm_offset, cfg1);

out:
	hdev->asic_funcs->hw_queues_unlock(hdev);

	hl_asic_dma_free_coherent(hdev, SZ_2M, kernel_addr, dma_addr);

	return rc;
}

static u64 gaudi_read_pte(struct hl_device *hdev, u64 addr)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (hdev->reset_info.hard_reset_pending)
		return U64_MAX;

	return readq(hdev->pcie_bar[HBM_BAR_ID] +
			(addr - gaudi->hbm_bar_cur_addr));
}

static void gaudi_write_pte(struct hl_device *hdev, u64 addr, u64 val)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (hdev->reset_info.hard_reset_pending)
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
		dev_crit(hdev->dev, "asid %u is too big\n", asid);
		return;
	}

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

	if (gaudi->hw_cap_initialized & HW_CAP_NIC0) {
		gaudi_mmu_prepare_reg(hdev, mmNIC0_QM0_GLBL_NON_SECURE_PROPS_0,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC0_QM0_GLBL_NON_SECURE_PROPS_1,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC0_QM0_GLBL_NON_SECURE_PROPS_2,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC0_QM0_GLBL_NON_SECURE_PROPS_3,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC0_QM0_GLBL_NON_SECURE_PROPS_4,
				asid);
	}

	if (gaudi->hw_cap_initialized & HW_CAP_NIC1) {
		gaudi_mmu_prepare_reg(hdev, mmNIC0_QM1_GLBL_NON_SECURE_PROPS_0,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC0_QM1_GLBL_NON_SECURE_PROPS_1,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC0_QM1_GLBL_NON_SECURE_PROPS_2,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC0_QM1_GLBL_NON_SECURE_PROPS_3,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC0_QM1_GLBL_NON_SECURE_PROPS_4,
				asid);
	}

	if (gaudi->hw_cap_initialized & HW_CAP_NIC2) {
		gaudi_mmu_prepare_reg(hdev, mmNIC1_QM0_GLBL_NON_SECURE_PROPS_0,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC1_QM0_GLBL_NON_SECURE_PROPS_1,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC1_QM0_GLBL_NON_SECURE_PROPS_2,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC1_QM0_GLBL_NON_SECURE_PROPS_3,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC1_QM0_GLBL_NON_SECURE_PROPS_4,
				asid);
	}

	if (gaudi->hw_cap_initialized & HW_CAP_NIC3) {
		gaudi_mmu_prepare_reg(hdev, mmNIC1_QM1_GLBL_NON_SECURE_PROPS_0,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC1_QM1_GLBL_NON_SECURE_PROPS_1,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC1_QM1_GLBL_NON_SECURE_PROPS_2,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC1_QM1_GLBL_NON_SECURE_PROPS_3,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC1_QM1_GLBL_NON_SECURE_PROPS_4,
				asid);
	}

	if (gaudi->hw_cap_initialized & HW_CAP_NIC4) {
		gaudi_mmu_prepare_reg(hdev, mmNIC2_QM0_GLBL_NON_SECURE_PROPS_0,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC2_QM0_GLBL_NON_SECURE_PROPS_1,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC2_QM0_GLBL_NON_SECURE_PROPS_2,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC2_QM0_GLBL_NON_SECURE_PROPS_3,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC2_QM0_GLBL_NON_SECURE_PROPS_4,
				asid);
	}

	if (gaudi->hw_cap_initialized & HW_CAP_NIC5) {
		gaudi_mmu_prepare_reg(hdev, mmNIC2_QM1_GLBL_NON_SECURE_PROPS_0,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC2_QM1_GLBL_NON_SECURE_PROPS_1,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC2_QM1_GLBL_NON_SECURE_PROPS_2,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC2_QM1_GLBL_NON_SECURE_PROPS_3,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC2_QM1_GLBL_NON_SECURE_PROPS_4,
				asid);
	}

	if (gaudi->hw_cap_initialized & HW_CAP_NIC6) {
		gaudi_mmu_prepare_reg(hdev, mmNIC3_QM0_GLBL_NON_SECURE_PROPS_0,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC3_QM0_GLBL_NON_SECURE_PROPS_1,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC3_QM0_GLBL_NON_SECURE_PROPS_2,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC3_QM0_GLBL_NON_SECURE_PROPS_3,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC3_QM0_GLBL_NON_SECURE_PROPS_4,
				asid);
	}

	if (gaudi->hw_cap_initialized & HW_CAP_NIC7) {
		gaudi_mmu_prepare_reg(hdev, mmNIC3_QM1_GLBL_NON_SECURE_PROPS_0,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC3_QM1_GLBL_NON_SECURE_PROPS_1,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC3_QM1_GLBL_NON_SECURE_PROPS_2,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC3_QM1_GLBL_NON_SECURE_PROPS_3,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC3_QM1_GLBL_NON_SECURE_PROPS_4,
				asid);
	}

	if (gaudi->hw_cap_initialized & HW_CAP_NIC8) {
		gaudi_mmu_prepare_reg(hdev, mmNIC4_QM0_GLBL_NON_SECURE_PROPS_0,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC4_QM0_GLBL_NON_SECURE_PROPS_1,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC4_QM0_GLBL_NON_SECURE_PROPS_2,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC4_QM0_GLBL_NON_SECURE_PROPS_3,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC4_QM0_GLBL_NON_SECURE_PROPS_4,
				asid);
	}

	if (gaudi->hw_cap_initialized & HW_CAP_NIC9) {
		gaudi_mmu_prepare_reg(hdev, mmNIC4_QM1_GLBL_NON_SECURE_PROPS_0,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC4_QM1_GLBL_NON_SECURE_PROPS_1,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC4_QM1_GLBL_NON_SECURE_PROPS_2,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC4_QM1_GLBL_NON_SECURE_PROPS_3,
				asid);
		gaudi_mmu_prepare_reg(hdev, mmNIC4_QM1_GLBL_NON_SECURE_PROPS_4,
				asid);
	}

	gaudi_mmu_prepare_reg(hdev, mmPSOC_GLOBAL_CONF_TRACE_ARUSER, asid);
	gaudi_mmu_prepare_reg(hdev, mmPSOC_GLOBAL_CONF_TRACE_AWUSER, asid);
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

	fence_ptr = hl_asic_dma_pool_zalloc(hdev, 4, GFP_KERNEL, &fence_dma_addr);
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

	WREG32(mmDMA0_CORE_PROT + dma_offset,
			BIT(DMA0_CORE_PROT_ERR_VAL_SHIFT) | BIT(DMA0_CORE_PROT_VAL_SHIFT));

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
	WREG32(mmDMA0_CORE_PROT + dma_offset, BIT(DMA0_CORE_PROT_ERR_VAL_SHIFT));

	hl_asic_dma_pool_free(hdev, (void *) fence_ptr, fence_dma_addr);
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

static const char *gaudi_get_razwi_initiator_dma_name(struct hl_device *hdev, u32 x_y,
							bool is_write, u16 *engine_id_1,
							u16 *engine_id_2)
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
		if ((err_cause[0] & mask) && !(err_cause[1] & mask)) {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_0;
			return "DMA0";
		} else if (!(err_cause[0] & mask) && (err_cause[1] & mask)) {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_2;
			return "DMA2";
		} else {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_0;
			*engine_id_2 = GAUDI_ENGINE_ID_DMA_2;
			return "DMA0 or DMA2";
		}
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_S_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_S_1:
		if ((err_cause[0] & mask) && !(err_cause[1] & mask)) {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_1;
			return "DMA1";
		} else if (!(err_cause[0] & mask) && (err_cause[1] & mask)) {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_3;
			return "DMA3";
		} else {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_1;
			*engine_id_2 = GAUDI_ENGINE_ID_DMA_3;
			return "DMA1 or DMA3";
		}
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_N_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_W_N_1:
		if ((err_cause[0] & mask) && !(err_cause[1] & mask)) {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_4;
			return "DMA4";
		} else if (!(err_cause[0] & mask) && (err_cause[1] & mask)) {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_6;
			return "DMA6";
		} else {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_4;
			*engine_id_2 = GAUDI_ENGINE_ID_DMA_6;
			return "DMA4 or DMA6";
		}
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_N_0:
	case RAZWI_INITIATOR_ID_X_Y_DMA_IF_E_N_1:
		if ((err_cause[0] & mask) && !(err_cause[1] & mask)) {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_5;
			return "DMA5";
		} else if (!(err_cause[0] & mask) && (err_cause[1] & mask)) {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_7;
			return "DMA7";
		} else {
			*engine_id_1 = GAUDI_ENGINE_ID_DMA_5;
			*engine_id_2 = GAUDI_ENGINE_ID_DMA_7;
			return "DMA5 or DMA7";
		}
	}

unknown_initiator:
	return "unknown initiator";
}

static const char *gaudi_get_razwi_initiator_name(struct hl_device *hdev, bool is_write,
							u16 *engine_id_1, u16 *engine_id_2)
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
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_TPC)) {
			*engine_id_1 = GAUDI_ENGINE_ID_TPC_0;
			return "TPC0";
		}
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_NIC)) {
			*engine_id_1 = GAUDI_ENGINE_ID_NIC_0;
			return "NIC0";
		}
		break;
	case RAZWI_INITIATOR_ID_X_Y_TPC1:
		*engine_id_1 = GAUDI_ENGINE_ID_TPC_1;
		return "TPC1";
	case RAZWI_INITIATOR_ID_X_Y_MME0_0:
	case RAZWI_INITIATOR_ID_X_Y_MME0_1:
		*engine_id_1 = GAUDI_ENGINE_ID_MME_0;
		return "MME0";
	case RAZWI_INITIATOR_ID_X_Y_MME1_0:
	case RAZWI_INITIATOR_ID_X_Y_MME1_1:
		*engine_id_1 = GAUDI_ENGINE_ID_MME_1;
		return "MME1";
	case RAZWI_INITIATOR_ID_X_Y_TPC2:
		*engine_id_1 = GAUDI_ENGINE_ID_TPC_2;
		return "TPC2";
	case RAZWI_INITIATOR_ID_X_Y_TPC3_PCI_CPU_PSOC:
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_TPC)) {
			*engine_id_1 = GAUDI_ENGINE_ID_TPC_3;
			return "TPC3";
		}
		/* PCI, CPU or PSOC does not have engine id*/
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
		return gaudi_get_razwi_initiator_dma_name(hdev, x_y, is_write,
				engine_id_1, engine_id_2);
	case RAZWI_INITIATOR_ID_X_Y_TPC4_NIC1_NIC2:
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_TPC)) {
			*engine_id_1 = GAUDI_ENGINE_ID_TPC_4;
			return "TPC4";
		}
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_NIC)) {
			*engine_id_1 = GAUDI_ENGINE_ID_NIC_1;
			return "NIC1";
		}
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_NIC_FT)) {
			*engine_id_1 = GAUDI_ENGINE_ID_NIC_2;
			return "NIC2";
		}
		break;
	case RAZWI_INITIATOR_ID_X_Y_TPC5:
		*engine_id_1 = GAUDI_ENGINE_ID_TPC_5;
		return "TPC5";
	case RAZWI_INITIATOR_ID_X_Y_MME2_0:
	case RAZWI_INITIATOR_ID_X_Y_MME2_1:
		*engine_id_1 = GAUDI_ENGINE_ID_MME_2;
		return "MME2";
	case RAZWI_INITIATOR_ID_X_Y_MME3_0:
	case RAZWI_INITIATOR_ID_X_Y_MME3_1:
		*engine_id_1 = GAUDI_ENGINE_ID_MME_3;
		return "MME3";
	case RAZWI_INITIATOR_ID_X_Y_TPC6:
		*engine_id_1 = GAUDI_ENGINE_ID_TPC_6;
		return "TPC6";
	case RAZWI_INITIATOR_ID_X_Y_TPC7_NIC4_NIC5:
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_TPC)) {
			*engine_id_1 = GAUDI_ENGINE_ID_TPC_7;
			return "TPC7";
		}
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_NIC)) {
			*engine_id_1 = GAUDI_ENGINE_ID_NIC_4;
			return "NIC4";
		}
		if (axi_id == RAZWI_INITIATOR_ID_AXI_ID(AXI_ID_NIC_FT)) {
			*engine_id_1 = GAUDI_ENGINE_ID_NIC_5;
			return "NIC5";
		}
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

static void gaudi_print_and_get_razwi_info(struct hl_device *hdev, u16 *engine_id_1,
						u16 *engine_id_2, bool *is_read, bool *is_write)
{

	if (RREG32(mmMMU_UP_RAZWI_WRITE_VLD)) {
		dev_err_ratelimited(hdev->dev,
			"RAZWI event caused by illegal write of %s\n",
			gaudi_get_razwi_initiator_name(hdev, true, engine_id_1, engine_id_2));
		WREG32(mmMMU_UP_RAZWI_WRITE_VLD, 0);
		*is_write = true;
	}

	if (RREG32(mmMMU_UP_RAZWI_READ_VLD)) {
		dev_err_ratelimited(hdev->dev,
			"RAZWI event caused by illegal read of %s\n",
			gaudi_get_razwi_initiator_name(hdev, false, engine_id_1, engine_id_2));
		WREG32(mmMMU_UP_RAZWI_READ_VLD, 0);
		*is_read = true;
	}
}

static void gaudi_print_and_get_mmu_error_info(struct hl_device *hdev, u64 *addr, u64 *event_mask)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 val;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MMU))
		return;

	val = RREG32(mmMMU_UP_PAGE_ERROR_CAPTURE);
	if (val & MMU_UP_PAGE_ERROR_CAPTURE_ENTRY_VALID_MASK) {
		*addr = val & MMU_UP_PAGE_ERROR_CAPTURE_VA_49_32_MASK;
		*addr <<= 32;
		*addr |= RREG32(mmMMU_UP_PAGE_ERROR_CAPTURE_VA);

		dev_err_ratelimited(hdev->dev, "MMU page fault on va 0x%llx\n", *addr);
		hl_handle_page_fault(hdev, *addr, 0, true, event_mask);

		WREG32(mmMMU_UP_PAGE_ERROR_CAPTURE, 0);
	}

	val = RREG32(mmMMU_UP_ACCESS_ERROR_CAPTURE);
	if (val & MMU_UP_ACCESS_ERROR_CAPTURE_ENTRY_VALID_MASK) {
		*addr = val & MMU_UP_ACCESS_ERROR_CAPTURE_VA_49_32_MASK;
		*addr <<= 32;
		*addr |= RREG32(mmMMU_UP_ACCESS_ERROR_CAPTURE_VA);

		dev_err_ratelimited(hdev->dev, "MMU access error on va 0x%llx\n", *addr);

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
	u32 i, num_mem_regs, reg, err_bit;
	u64 err_addr, err_word = 0;

	num_mem_regs = params->num_memories / 32 +
			((params->num_memories % 32) ? 1 : 0);

	if (params->block_address >= CFG_BASE)
		params->block_address -= CFG_BASE;

	if (params->derr)
		err_addr = params->block_address + GAUDI_ECC_DERR0_OFFSET;
	else
		err_addr = params->block_address + GAUDI_ECC_SERR0_OFFSET;

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
		return -EINVAL;
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

	return 0;
}

/*
 * gaudi_queue_idx_dec - decrement queue index (pi/ci) and handle wrap
 *
 * @idx: the current pi/ci value
 * @q_len: the queue length (power of 2)
 *
 * @return the cyclically decremented index
 */
static inline u32 gaudi_queue_idx_dec(u32 idx, u32 q_len)
{
	u32 mask = q_len - 1;

	/*
	 * modular decrement is equivalent to adding (queue_size -1)
	 * later we take LSBs to make sure the value is in the
	 * range [0, queue_len - 1]
	 */
	return (idx + q_len - 1) & mask;
}

/**
 * gaudi_handle_sw_config_stream_data - print SW config stream data
 *
 * @hdev: pointer to the habanalabs device structure
 * @stream: the QMAN's stream
 * @qman_base: base address of QMAN registers block
 * @event_mask: mask of the last events occurred
 */
static void gaudi_handle_sw_config_stream_data(struct hl_device *hdev, u32 stream,
						u64 qman_base, u64 event_mask)
{
	u64 cq_ptr_lo, cq_ptr_hi, cq_tsize, cq_ptr;
	u32 cq_ptr_lo_off, size;

	cq_ptr_lo_off = mmTPC0_QM_CQ_PTR_LO_1 - mmTPC0_QM_CQ_PTR_LO_0;

	cq_ptr_lo = qman_base + (mmTPC0_QM_CQ_PTR_LO_0 - mmTPC0_QM_BASE) +
						stream * cq_ptr_lo_off;
	cq_ptr_hi = cq_ptr_lo +
				(mmTPC0_QM_CQ_PTR_HI_0 - mmTPC0_QM_CQ_PTR_LO_0);
	cq_tsize = cq_ptr_lo +
				(mmTPC0_QM_CQ_TSIZE_0 - mmTPC0_QM_CQ_PTR_LO_0);

	cq_ptr = (((u64) RREG32(cq_ptr_hi)) << 32) | RREG32(cq_ptr_lo);
	size = RREG32(cq_tsize);
	dev_info(hdev->dev, "stop on err: stream: %u, addr: %#llx, size: %u\n",
							stream, cq_ptr, size);

	if (event_mask & HL_NOTIFIER_EVENT_UNDEFINED_OPCODE) {
		hdev->captured_err_info.undef_opcode.cq_addr = cq_ptr;
		hdev->captured_err_info.undef_opcode.cq_size = size;
		hdev->captured_err_info.undef_opcode.stream_id = stream;
	}
}

/**
 * gaudi_handle_last_pqes_on_err - print last PQEs on error
 *
 * @hdev: pointer to the habanalabs device structure
 * @qid_base: first QID of the QMAN (out of 4 streams)
 * @stream: the QMAN's stream
 * @qman_base: base address of QMAN registers block
 * @event_mask: mask of the last events occurred
 * @pr_sw_conf: if true print the SW config stream data (CQ PTR and SIZE)
 */
static void gaudi_handle_last_pqes_on_err(struct hl_device *hdev, u32 qid_base,
						u32 stream, u64 qman_base,
						u64 event_mask,
						bool pr_sw_conf)
{
	u32 ci, qm_ci_stream_off, queue_len;
	struct hl_hw_queue *q;
	u64 pq_ci, addr[PQ_FETCHER_CACHE_SIZE];
	int i;

	q = &hdev->kernel_queues[qid_base + stream];

	qm_ci_stream_off = mmTPC0_QM_PQ_CI_1 - mmTPC0_QM_PQ_CI_0;
	pq_ci = qman_base + (mmTPC0_QM_PQ_CI_0 - mmTPC0_QM_BASE) +
						stream * qm_ci_stream_off;

	queue_len = (q->queue_type == QUEUE_TYPE_INT) ?
					q->int_queue_len : HL_QUEUE_LENGTH;

	hdev->asic_funcs->hw_queues_lock(hdev);

	if (pr_sw_conf)
		gaudi_handle_sw_config_stream_data(hdev, stream, qman_base, event_mask);

	ci = RREG32(pq_ci);

	/* we should start printing form ci -1 */
	ci = gaudi_queue_idx_dec(ci, queue_len);
	memset(addr, 0, sizeof(addr));

	for (i = 0; i < PQ_FETCHER_CACHE_SIZE; i++) {
		struct hl_bd *bd;
		u32 len;

		bd = q->kernel_address;
		bd += ci;

		len = le32_to_cpu(bd->len);
		/* len 0 means uninitialized entry- break */
		if (!len)
			break;

		addr[i] = le64_to_cpu(bd->ptr);

		dev_info(hdev->dev, "stop on err PQE(stream %u): ci: %u, addr: %#llx, size: %u\n",
							stream, ci, addr[i], len);

		/* get previous ci, wrap if needed */
		ci = gaudi_queue_idx_dec(ci, queue_len);
	}

	if (event_mask & HL_NOTIFIER_EVENT_UNDEFINED_OPCODE) {
		struct undefined_opcode_info *undef_opcode = &hdev->captured_err_info.undef_opcode;
		u32 arr_idx = undef_opcode->cb_addr_streams_len;

		if (arr_idx == 0) {
			undef_opcode->timestamp = ktime_get();
			undef_opcode->engine_id = gaudi_queue_id_to_engine_id[qid_base];
		}

		memcpy(undef_opcode->cb_addr_streams[arr_idx], addr, sizeof(addr));
		undef_opcode->cb_addr_streams_len++;
	}

	hdev->asic_funcs->hw_queues_unlock(hdev);
}

/**
 * handle_qman_data_on_err - extract QMAN data on error
 *
 * @hdev: pointer to the habanalabs device structure
 * @qid_base: first QID of the QMAN (out of 4 streams)
 * @stream: the QMAN's stream
 * @qman_base: base address of QMAN registers block
 * @event_mask: mask of the last events occurred
 *
 * This function attempt to exatract as much data as possible on QMAN error.
 * On upper CP print the SW config stream data and last 8 PQEs.
 * On lower CP print SW config data and last PQEs of ALL 4 upper CPs
 */
static void handle_qman_data_on_err(struct hl_device *hdev, u32 qid_base,
				   u32 stream, u64 qman_base, u64 event_mask)
{
	u32 i;

	if (stream != QMAN_STREAMS) {
		gaudi_handle_last_pqes_on_err(hdev, qid_base, stream,
			qman_base, event_mask, true);
		return;
	}

	/* handle Lower-CP */
	gaudi_handle_sw_config_stream_data(hdev, stream, qman_base, event_mask);

	for (i = 0; i < QMAN_STREAMS; i++)
		gaudi_handle_last_pqes_on_err(hdev, qid_base, i,
			qman_base, event_mask, false);
}

static void gaudi_handle_qman_err_generic(struct hl_device *hdev,
					  const char *qm_name,
					  u64 qman_base,
					  u32 qid_base,
					  u64 *event_mask)
{
	u32 i, j, glbl_sts_val, arb_err_val, glbl_sts_clr_val;
	u64 glbl_sts_addr, arb_err_addr;
	char reg_desc[32];

	glbl_sts_addr = qman_base + (mmTPC0_QM_GLBL_STS1_0 - mmTPC0_QM_BASE);
	arb_err_addr = qman_base + (mmTPC0_QM_ARB_ERR_CAUSE - mmTPC0_QM_BASE);

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
		/* check for undefined opcode */
		if (glbl_sts_val & TPC0_QM_GLBL_STS1_CP_UNDEF_CMD_ERR_MASK &&
				hdev->captured_err_info.undef_opcode.write_enable) {
			memset(&hdev->captured_err_info.undef_opcode, 0,
						sizeof(hdev->captured_err_info.undef_opcode));

			hdev->captured_err_info.undef_opcode.write_enable = false;
			*event_mask |= HL_NOTIFIER_EVENT_UNDEFINED_OPCODE;
		}

		/* Write 1 clear errors */
		if (!hdev->stop_on_err)
			WREG32(glbl_sts_addr + 4 * i, glbl_sts_clr_val);
		else
			handle_qman_data_on_err(hdev, qid_base, i, qman_base, *event_mask);
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

static void gaudi_print_sm_sei_info(struct hl_device *hdev, u16 event_type,
		struct hl_eq_sm_sei_data *sei_data)
{
	u32 index = event_type - GAUDI_EVENT_DMA_IF_SEI_0;

	/* Flip the bits as the enum is ordered in the opposite way */
	index = (index ^ 0x3) & 0x3;

	switch (sei_data->sei_cause) {
	case SM_SEI_SO_OVERFLOW:
		dev_err_ratelimited(hdev->dev,
			"%s SEI Error: SOB Group %u overflow/underflow",
			gaudi_sync_manager_names[index],
			le32_to_cpu(sei_data->sei_log));
		break;
	case SM_SEI_LBW_4B_UNALIGNED:
		dev_err_ratelimited(hdev->dev,
			"%s SEI Error: Unaligned 4B LBW access, monitor agent address low - %#x",
			gaudi_sync_manager_names[index],
			le32_to_cpu(sei_data->sei_log));
		break;
	case SM_SEI_AXI_RESPONSE_ERR:
		dev_err_ratelimited(hdev->dev,
			"%s SEI Error: AXI ID %u response error",
			gaudi_sync_manager_names[index],
			le32_to_cpu(sei_data->sei_log));
		break;
	default:
		dev_err_ratelimited(hdev->dev, "Unknown SM SEI cause %u",
				le32_to_cpu(sei_data->sei_log));
		break;
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

	if (hdev->asic_prop.fw_security_enabled) {
		extract_info_from_fw = true;
		goto extract_ecc_info;
	}

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
		extract_info_from_fw = false;
		break;
	case GAUDI_EVENT_TPC0_DERR ... GAUDI_EVENT_TPC7_DERR:
		index = event_type - GAUDI_EVENT_TPC0_DERR;
		params.block_address =
			mmTPC0_CFG_BASE + index * TPC_CFG_OFFSET;
		params.num_memories = 90;
		params.derr = true;
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
		extract_info_from_fw = false;
		break;
	default:
		return;
	}

extract_ecc_info:
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

static void gaudi_handle_qman_err(struct hl_device *hdev, u16 event_type, u64 *event_mask)
{
	u64 qman_base;
	char desc[32];
	u32 qid_base;
	u8 index;

	switch (event_type) {
	case GAUDI_EVENT_TPC0_QM ... GAUDI_EVENT_TPC7_QM:
		index = event_type - GAUDI_EVENT_TPC0_QM;
		qid_base = GAUDI_QUEUE_ID_TPC_0_0 + index * QMAN_STREAMS;
		qman_base = mmTPC0_QM_BASE + index * TPC_QMAN_OFFSET;
		snprintf(desc, ARRAY_SIZE(desc), "%s%d", "TPC_QM", index);
		break;
	case GAUDI_EVENT_MME0_QM ... GAUDI_EVENT_MME2_QM:
		if (event_type == GAUDI_EVENT_MME0_QM) {
			index = 0;
			qid_base = GAUDI_QUEUE_ID_MME_0_0;
		} else { /* event_type == GAUDI_EVENT_MME2_QM */
			index = 2;
			qid_base = GAUDI_QUEUE_ID_MME_1_0;
		}
		qman_base = mmMME0_QM_BASE + index * MME_QMAN_OFFSET;
		snprintf(desc, ARRAY_SIZE(desc), "%s%d", "MME_QM", index);
		break;
	case GAUDI_EVENT_DMA0_QM ... GAUDI_EVENT_DMA7_QM:
		index = event_type - GAUDI_EVENT_DMA0_QM;
		qid_base = GAUDI_QUEUE_ID_DMA_0_0 + index * QMAN_STREAMS;
		/* skip GAUDI_QUEUE_ID_CPU_PQ if necessary */
		if (index > 1)
			qid_base++;
		qman_base = mmDMA0_QM_BASE + index * DMA_QMAN_OFFSET;
		snprintf(desc, ARRAY_SIZE(desc), "%s%d", "DMA_QM", index);
		break;
	case GAUDI_EVENT_NIC0_QM0:
		qid_base = GAUDI_QUEUE_ID_NIC_0_0;
		qman_base = mmNIC0_QM0_BASE;
		snprintf(desc, ARRAY_SIZE(desc), "NIC0_QM0");
		break;
	case GAUDI_EVENT_NIC0_QM1:
		qid_base = GAUDI_QUEUE_ID_NIC_1_0;
		qman_base = mmNIC0_QM1_BASE;
		snprintf(desc, ARRAY_SIZE(desc), "NIC0_QM1");
		break;
	case GAUDI_EVENT_NIC1_QM0:
		qid_base = GAUDI_QUEUE_ID_NIC_2_0;
		qman_base = mmNIC1_QM0_BASE;
		snprintf(desc, ARRAY_SIZE(desc), "NIC1_QM0");
		break;
	case GAUDI_EVENT_NIC1_QM1:
		qid_base = GAUDI_QUEUE_ID_NIC_3_0;
		qman_base = mmNIC1_QM1_BASE;
		snprintf(desc, ARRAY_SIZE(desc), "NIC1_QM1");
		break;
	case GAUDI_EVENT_NIC2_QM0:
		qid_base = GAUDI_QUEUE_ID_NIC_4_0;
		qman_base = mmNIC2_QM0_BASE;
		snprintf(desc, ARRAY_SIZE(desc), "NIC2_QM0");
		break;
	case GAUDI_EVENT_NIC2_QM1:
		qid_base = GAUDI_QUEUE_ID_NIC_5_0;
		qman_base = mmNIC2_QM1_BASE;
		snprintf(desc, ARRAY_SIZE(desc), "NIC2_QM1");
		break;
	case GAUDI_EVENT_NIC3_QM0:
		qid_base = GAUDI_QUEUE_ID_NIC_6_0;
		qman_base = mmNIC3_QM0_BASE;
		snprintf(desc, ARRAY_SIZE(desc), "NIC3_QM0");
		break;
	case GAUDI_EVENT_NIC3_QM1:
		qid_base = GAUDI_QUEUE_ID_NIC_7_0;
		qman_base = mmNIC3_QM1_BASE;
		snprintf(desc, ARRAY_SIZE(desc), "NIC3_QM1");
		break;
	case GAUDI_EVENT_NIC4_QM0:
		qid_base = GAUDI_QUEUE_ID_NIC_8_0;
		qman_base = mmNIC4_QM0_BASE;
		snprintf(desc, ARRAY_SIZE(desc), "NIC4_QM0");
		break;
	case GAUDI_EVENT_NIC4_QM1:
		qid_base = GAUDI_QUEUE_ID_NIC_9_0;
		qman_base = mmNIC4_QM1_BASE;
		snprintf(desc, ARRAY_SIZE(desc), "NIC4_QM1");
		break;
	default:
		return;
	}

	gaudi_handle_qman_err_generic(hdev, desc, qman_base, qid_base, event_mask);
}

static void gaudi_print_irq_info(struct hl_device *hdev, u16 event_type,
					bool check_razwi, u64 *event_mask)
{
	bool is_read = false, is_write = false;
	u16 engine_id[2], num_of_razwi_eng = 0;
	char desc[64] = "";
	u64 razwi_addr = 0;
	u8 razwi_flags = 0;

	/*
	 * Init engine id by default as not valid and only if razwi initiated from engine with
	 * engine id it will get valid value.
	 */
	engine_id[0] = HL_RAZWI_NA_ENG_ID;
	engine_id[1] = HL_RAZWI_NA_ENG_ID;

	gaudi_get_event_desc(event_type, desc, sizeof(desc));
	dev_err_ratelimited(hdev->dev, "Received H/W interrupt %d [\"%s\"]\n",
		event_type, desc);

	if (check_razwi) {
		gaudi_print_and_get_razwi_info(hdev, &engine_id[0], &engine_id[1], &is_read,
						&is_write);
		gaudi_print_and_get_mmu_error_info(hdev, &razwi_addr, event_mask);

		if (is_read)
			razwi_flags |= HL_RAZWI_READ;
		if (is_write)
			razwi_flags |= HL_RAZWI_WRITE;

		if (engine_id[0] != HL_RAZWI_NA_ENG_ID) {
			if (engine_id[1] != HL_RAZWI_NA_ENG_ID)
				num_of_razwi_eng = 2;
			else
				num_of_razwi_eng = 1;
		}

		if (razwi_flags)
			hl_handle_razwi(hdev, razwi_addr, engine_id, num_of_razwi_eng,
					razwi_flags, event_mask);
	}
}

static void gaudi_print_out_of_sync_info(struct hl_device *hdev,
					struct cpucp_pkt_sync_err *sync_err)
{
	struct hl_hw_queue *q = &hdev->kernel_queues[GAUDI_QUEUE_ID_CPU_PQ];

	dev_err(hdev->dev, "Out of sync with FW, FW: pi=%u, ci=%u, LKD: pi=%u, ci=%d\n",
		le32_to_cpu(sync_err->pi), le32_to_cpu(sync_err->ci), q->pi, atomic_read(&q->ci));
}

static void gaudi_print_fw_alive_info(struct hl_device *hdev,
					struct hl_eq_fw_alive *fw_alive)
{
	dev_err(hdev->dev,
		"FW alive report: severity=%s, process_id=%u, thread_id=%u, uptime=%llu seconds\n",
		(fw_alive->severity == FW_ALIVE_SEVERITY_MINOR) ? "Minor" : "Critical",
		le32_to_cpu(fw_alive->process_id),
		le32_to_cpu(fw_alive->thread_id),
		le64_to_cpu(fw_alive->uptime_seconds));
}

static void gaudi_print_nic_axi_irq_info(struct hl_device *hdev, u16 event_type,
						void *data)
{
	char desc[64] = "", *type;
	struct eq_nic_sei_event *eq_nic_sei = data;
	u16 nic_id = event_type - GAUDI_EVENT_NIC_SEI_0;

	switch (eq_nic_sei->axi_error_cause) {
	case RXB:
		type = "RXB";
		break;
	case RXE:
		type = "RXE";
		break;
	case TXS:
		type = "TXS";
		break;
	case TXE:
		type = "TXE";
		break;
	case QPC_RESP:
		type = "QPC_RESP";
		break;
	case NON_AXI_ERR:
		type = "NON_AXI_ERR";
		break;
	case TMR:
		type = "TMR";
		break;
	default:
		dev_err(hdev->dev, "unknown NIC AXI cause %d\n",
			eq_nic_sei->axi_error_cause);
		type = "N/A";
		break;
	}

	snprintf(desc, sizeof(desc), "NIC%d_%s%d", nic_id, type,
			eq_nic_sei->id);
	dev_err_ratelimited(hdev->dev, "Received H/W interrupt %d [\"%s\"]\n",
		event_type, desc);
}

static int gaudi_compute_reset_late_init(struct hl_device *hdev)
{
	/* GAUDI doesn't support any reset except hard-reset */
	return -EPERM;
}

static int gaudi_hbm_read_interrupts(struct hl_device *hdev, int device,
			struct hl_eq_hbm_ecc_data *hbm_ecc_data)
{
	u32 base, val, val2, wr_par, rd_par, ca_par, derr, serr, type, ch;
	int rc = 0;

	if (hdev->asic_prop.fw_app_cpu_boot_dev_sts0 &
					CPU_BOOT_DEV_STS0_HBM_ECC_EN) {
		if (!hbm_ecc_data) {
			dev_err(hdev->dev, "No FW ECC data");
			return 0;
		}

		wr_par = FIELD_GET(CPUCP_PKT_HBM_ECC_INFO_WR_PAR_MASK,
				le32_to_cpu(hbm_ecc_data->hbm_ecc_info));
		rd_par = FIELD_GET(CPUCP_PKT_HBM_ECC_INFO_RD_PAR_MASK,
				le32_to_cpu(hbm_ecc_data->hbm_ecc_info));
		ca_par = FIELD_GET(CPUCP_PKT_HBM_ECC_INFO_CA_PAR_MASK,
				le32_to_cpu(hbm_ecc_data->hbm_ecc_info));
		derr = FIELD_GET(CPUCP_PKT_HBM_ECC_INFO_DERR_MASK,
				le32_to_cpu(hbm_ecc_data->hbm_ecc_info));
		serr = FIELD_GET(CPUCP_PKT_HBM_ECC_INFO_SERR_MASK,
				le32_to_cpu(hbm_ecc_data->hbm_ecc_info));
		type = FIELD_GET(CPUCP_PKT_HBM_ECC_INFO_TYPE_MASK,
				le32_to_cpu(hbm_ecc_data->hbm_ecc_info));
		ch = FIELD_GET(CPUCP_PKT_HBM_ECC_INFO_HBM_CH_MASK,
				le32_to_cpu(hbm_ecc_data->hbm_ecc_info));

		dev_err(hdev->dev,
			"HBM%d pc%d interrupts info: WR_PAR=%d, RD_PAR=%d, CA_PAR=%d, SERR=%d, DERR=%d\n",
			device, ch, wr_par, rd_par, ca_par, serr, derr);
		dev_err(hdev->dev,
			"HBM%d pc%d ECC info: 1ST_ERR_ADDR=0x%x, 1ST_ERR_TYPE=%d, SEC_CONT_CNT=%u, SEC_CNT=%d, DEC_CNT=%d\n",
			device, ch, hbm_ecc_data->first_addr, type,
			hbm_ecc_data->sec_cont_cnt, hbm_ecc_data->sec_cnt,
			hbm_ecc_data->dec_cnt);
		return 0;
	}

	if (hdev->asic_prop.fw_security_enabled) {
		dev_info(hdev->dev, "Cannot access MC regs for ECC data while security is enabled\n");
		return 0;
	}

	base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		val = RREG32_MASK(base + ch * 0x1000 + 0x06C, 0x0000FFFF);
		val = (val & 0xFF) | ((val >> 8) & 0xFF);
		if (val) {
			rc = -EIO;
			dev_err(hdev->dev,
				"HBM%d pc%d interrupts info: WR_PAR=%d, RD_PAR=%d, CA_PAR=%d, SERR=%d, DERR=%d\n",
				device, ch * 2, val & 0x1, (val >> 1) & 0x1,
				(val >> 2) & 0x1, (val >> 3) & 0x1,
				(val >> 4) & 0x1);

			val2 = RREG32(base + ch * 0x1000 + 0x060);
			dev_err(hdev->dev,
				"HBM%d pc%d ECC info: 1ST_ERR_ADDR=0x%x, 1ST_ERR_TYPE=%d, SEC_CONT_CNT=%d, SEC_CNT=%d, DEC_CNT=%d\n",
				device, ch * 2,
				RREG32(base + ch * 0x1000 + 0x064),
				(val2 & 0x200) >> 9, (val2 & 0xFC00) >> 10,
				(val2 & 0xFF0000) >> 16,
				(val2 & 0xFF000000) >> 24);
		}

		val = RREG32_MASK(base + ch * 0x1000 + 0x07C, 0x0000FFFF);
		val = (val & 0xFF) | ((val >> 8) & 0xFF);
		if (val) {
			rc = -EIO;
			dev_err(hdev->dev,
				"HBM%d pc%d interrupts info: WR_PAR=%d, RD_PAR=%d, CA_PAR=%d, SERR=%d, DERR=%d\n",
				device, ch * 2 + 1, val & 0x1, (val >> 1) & 0x1,
				(val >> 2) & 0x1, (val >> 3) & 0x1,
				(val >> 4) & 0x1);

			val2 = RREG32(base + ch * 0x1000 + 0x070);
			dev_err(hdev->dev,
				"HBM%d pc%d ECC info: 1ST_ERR_ADDR=0x%x, 1ST_ERR_TYPE=%d, SEC_CONT_CNT=%d, SEC_CNT=%d, DEC_CNT=%d\n",
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
		rc = -EIO;
		dev_err(hdev->dev,
			"HBM %d MC SRAM SERR info: Reg 0x8F30=0x%x, Reg 0x8F34=0x%x\n",
			device, val, val2);
	}
	val  = RREG32(base + 0x8F40);
	val2 = RREG32(base + 0x8F44);
	if (val | val2) {
		rc = -EIO;
		dev_err(hdev->dev,
			"HBM %d MC SRAM DERR info: Reg 0x8F40=0x%x, Reg 0x8F44=0x%x\n",
			device, val, val2);
	}

	return rc;
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
	u32 tpc_offset = tpc_id * TPC_CFG_OFFSET, tpc_interrupts_cause, i;
	bool soft_reset_required = false;

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

static void gaudi_print_clk_change_info(struct hl_device *hdev, u16 event_type, u64 *event_mask)
{
	ktime_t zero_time = ktime_set(0, 0);

	mutex_lock(&hdev->clk_throttling.lock);

	switch (event_type) {
	case GAUDI_EVENT_FIX_POWER_ENV_S:
		hdev->clk_throttling.current_reason |= HL_CLK_THROTTLE_POWER;
		hdev->clk_throttling.aggregated_reason |= HL_CLK_THROTTLE_POWER;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_POWER].start = ktime_get();
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_POWER].end = zero_time;
		dev_info_ratelimited(hdev->dev,
			"Clock throttling due to power consumption\n");
		break;

	case GAUDI_EVENT_FIX_POWER_ENV_E:
		hdev->clk_throttling.current_reason &= ~HL_CLK_THROTTLE_POWER;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_POWER].end = ktime_get();
		dev_info_ratelimited(hdev->dev,
			"Power envelop is safe, back to optimal clock\n");
		break;

	case GAUDI_EVENT_FIX_THERMAL_ENV_S:
		hdev->clk_throttling.current_reason |= HL_CLK_THROTTLE_THERMAL;
		hdev->clk_throttling.aggregated_reason |= HL_CLK_THROTTLE_THERMAL;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_THERMAL].start = ktime_get();
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_THERMAL].end = zero_time;
		*event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		dev_info_ratelimited(hdev->dev,
			"Clock throttling due to overheating\n");
		break;

	case GAUDI_EVENT_FIX_THERMAL_ENV_E:
		hdev->clk_throttling.current_reason &= ~HL_CLK_THROTTLE_THERMAL;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_THERMAL].end = ktime_get();
		*event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
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

static void gaudi_handle_eqe(struct hl_device *hdev, struct hl_eq_entry *eq_entry)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct hl_info_fw_err_info fw_err_info;
	u64 data = le64_to_cpu(eq_entry->data[0]), event_mask = 0;
	u32 ctl = le32_to_cpu(eq_entry->hdr.ctl);
	u32 fw_fatal_err_flag = 0, flags = 0;
	u16 event_type = ((ctl & EQ_CTL_EVENT_TYPE_MASK)
			>> EQ_CTL_EVENT_TYPE_SHIFT);
	bool reset_required, reset_direct = false;
	u8 cause;
	int rc;

	if (event_type >= GAUDI_EVENT_SIZE) {
		dev_err(hdev->dev, "Event type %u exceeds maximum of %u",
				event_type, GAUDI_EVENT_SIZE - 1);
		return;
	}

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
	case GAUDI_EVENT_NIC0_DERR ... GAUDI_EVENT_NIC4_DERR:
	case GAUDI_EVENT_DMA_IF0_DERR ... GAUDI_EVENT_DMA_IF3_DERR:
	case GAUDI_EVENT_HBM_0_DERR ... GAUDI_EVENT_HBM_3_DERR:
	case GAUDI_EVENT_MMU_DERR:
	case GAUDI_EVENT_NIC0_CS_DBG_DERR ... GAUDI_EVENT_NIC4_CS_DBG_DERR:
		gaudi_print_irq_info(hdev, event_type, true, &event_mask);
		gaudi_handle_ecc_event(hdev, event_type, &eq_entry->ecc_data);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		fw_fatal_err_flag = HL_DRV_RESET_FW_FATAL_ERR;
		goto reset_device;

	case GAUDI_EVENT_GIC500:
	case GAUDI_EVENT_AXI_ECC:
	case GAUDI_EVENT_L2_RAM_ECC:
	case GAUDI_EVENT_PLL0 ... GAUDI_EVENT_PLL17:
		gaudi_print_irq_info(hdev, event_type, false, &event_mask);
		fw_fatal_err_flag = HL_DRV_RESET_FW_FATAL_ERR;
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		goto reset_device;

	case GAUDI_EVENT_HBM0_SPI_0:
	case GAUDI_EVENT_HBM1_SPI_0:
	case GAUDI_EVENT_HBM2_SPI_0:
	case GAUDI_EVENT_HBM3_SPI_0:
		gaudi_print_irq_info(hdev, event_type, false, &event_mask);
		gaudi_hbm_read_interrupts(hdev,
				gaudi_hbm_event_to_dev(event_type),
				&eq_entry->hbm_ecc_data);
		fw_fatal_err_flag = HL_DRV_RESET_FW_FATAL_ERR;
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		goto reset_device;

	case GAUDI_EVENT_HBM0_SPI_1:
	case GAUDI_EVENT_HBM1_SPI_1:
	case GAUDI_EVENT_HBM2_SPI_1:
	case GAUDI_EVENT_HBM3_SPI_1:
		gaudi_print_irq_info(hdev, event_type, false, &event_mask);
		gaudi_hbm_read_interrupts(hdev,
				gaudi_hbm_event_to_dev(event_type),
				&eq_entry->hbm_ecc_data);
		hl_fw_unmask_irq(hdev, event_type);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		break;

	case GAUDI_EVENT_TPC0_DEC:
	case GAUDI_EVENT_TPC1_DEC:
	case GAUDI_EVENT_TPC2_DEC:
	case GAUDI_EVENT_TPC3_DEC:
	case GAUDI_EVENT_TPC4_DEC:
	case GAUDI_EVENT_TPC5_DEC:
	case GAUDI_EVENT_TPC6_DEC:
	case GAUDI_EVENT_TPC7_DEC:
		/* In TPC DEC event, notify on TPC assertion. While there isn't
		 * a specific event for assertion yet, the FW generates TPC DEC event.
		 * The SW upper layer will inspect an internal mapped area to indicate
		 * if the event is a TPC Assertion or a "real" TPC DEC.
		 */
		event_mask |= HL_NOTIFIER_EVENT_TPC_ASSERT;
		gaudi_print_irq_info(hdev, event_type, true, &event_mask);
		reset_required = gaudi_tpc_read_interrupts(hdev,
					tpc_dec_event_to_tpc_id(event_type),
					"AXI_SLV_DEC_Error");
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		if (reset_required) {
			dev_err(hdev->dev, "reset required due to %s\n",
				gaudi_irq_map_table[event_type].name);

			reset_direct = true;
			goto reset_device;
		} else {
			hl_fw_unmask_irq(hdev, event_type);
			event_mask |= HL_NOTIFIER_EVENT_DEVICE_RESET;
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
		gaudi_print_irq_info(hdev, event_type, true, &event_mask);
		reset_required = gaudi_tpc_read_interrupts(hdev,
					tpc_krn_event_to_tpc_id(event_type),
					"KRN_ERR");
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		if (reset_required) {
			dev_err(hdev->dev, "reset required due to %s\n",
				gaudi_irq_map_table[event_type].name);

			reset_direct = true;
			goto reset_device;
		} else {
			hl_fw_unmask_irq(hdev, event_type);
			event_mask |= HL_NOTIFIER_EVENT_DEVICE_RESET;
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
	case GAUDI_EVENT_NIC0_SERR ... GAUDI_EVENT_NIC4_SERR:
	case GAUDI_EVENT_DMA_IF0_SERR ... GAUDI_EVENT_DMA_IF3_SERR:
	case GAUDI_EVENT_HBM_0_SERR ... GAUDI_EVENT_HBM_3_SERR:
		fallthrough;
	case GAUDI_EVENT_MMU_SERR:
		gaudi_print_irq_info(hdev, event_type, true, &event_mask);
		gaudi_handle_ecc_event(hdev, event_type, &eq_entry->ecc_data);
		hl_fw_unmask_irq(hdev, event_type);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		break;

	case GAUDI_EVENT_PCIE_DEC:
	case GAUDI_EVENT_CPU_AXI_SPLITTER:
	case GAUDI_EVENT_PSOC_AXI_DEC:
	case GAUDI_EVENT_PSOC_PRSTN_FALL:
		gaudi_print_irq_info(hdev, event_type, true, &event_mask);
		hl_fw_unmask_irq(hdev, event_type);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		break;

	case GAUDI_EVENT_MMU_PAGE_FAULT:
	case GAUDI_EVENT_MMU_WR_PERM:
		gaudi_print_irq_info(hdev, event_type, true, &event_mask);
		hl_fw_unmask_irq(hdev, event_type);
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		break;

	case GAUDI_EVENT_MME0_WBC_RSP:
	case GAUDI_EVENT_MME0_SBAB0_RSP:
	case GAUDI_EVENT_MME1_WBC_RSP:
	case GAUDI_EVENT_MME1_SBAB0_RSP:
	case GAUDI_EVENT_MME2_WBC_RSP:
	case GAUDI_EVENT_MME2_SBAB0_RSP:
	case GAUDI_EVENT_MME3_WBC_RSP:
	case GAUDI_EVENT_MME3_SBAB0_RSP:
	case GAUDI_EVENT_RAZWI_OR_ADC:
	case GAUDI_EVENT_MME0_QM ... GAUDI_EVENT_MME2_QM:
	case GAUDI_EVENT_DMA0_QM ... GAUDI_EVENT_DMA7_QM:
		fallthrough;
	case GAUDI_EVENT_NIC0_QM0:
	case GAUDI_EVENT_NIC0_QM1:
	case GAUDI_EVENT_NIC1_QM0:
	case GAUDI_EVENT_NIC1_QM1:
	case GAUDI_EVENT_NIC2_QM0:
	case GAUDI_EVENT_NIC2_QM1:
	case GAUDI_EVENT_NIC3_QM0:
	case GAUDI_EVENT_NIC3_QM1:
	case GAUDI_EVENT_NIC4_QM0:
	case GAUDI_EVENT_NIC4_QM1:
	case GAUDI_EVENT_DMA0_CORE ... GAUDI_EVENT_DMA7_CORE:
	case GAUDI_EVENT_TPC0_QM ... GAUDI_EVENT_TPC7_QM:
		gaudi_print_irq_info(hdev, event_type, true, &event_mask);
		gaudi_handle_qman_err(hdev, event_type, &event_mask);
		hl_fw_unmask_irq(hdev, event_type);
		event_mask |= (HL_NOTIFIER_EVENT_USER_ENGINE_ERR | HL_NOTIFIER_EVENT_DEVICE_RESET);
		break;

	case GAUDI_EVENT_RAZWI_OR_ADC_SW:
		gaudi_print_irq_info(hdev, event_type, true, &event_mask);
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		goto reset_device;

	case GAUDI_EVENT_TPC0_BMON_SPMU:
	case GAUDI_EVENT_TPC1_BMON_SPMU:
	case GAUDI_EVENT_TPC2_BMON_SPMU:
	case GAUDI_EVENT_TPC3_BMON_SPMU:
	case GAUDI_EVENT_TPC4_BMON_SPMU:
	case GAUDI_EVENT_TPC5_BMON_SPMU:
	case GAUDI_EVENT_TPC6_BMON_SPMU:
	case GAUDI_EVENT_TPC7_BMON_SPMU:
	case GAUDI_EVENT_DMA_BM_CH0 ... GAUDI_EVENT_DMA_BM_CH7:
		gaudi_print_irq_info(hdev, event_type, false, &event_mask);
		hl_fw_unmask_irq(hdev, event_type);
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		break;

	case GAUDI_EVENT_NIC_SEI_0 ... GAUDI_EVENT_NIC_SEI_4:
		gaudi_print_nic_axi_irq_info(hdev, event_type, &data);
		hl_fw_unmask_irq(hdev, event_type);
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		break;

	case GAUDI_EVENT_DMA_IF_SEI_0 ... GAUDI_EVENT_DMA_IF_SEI_3:
		gaudi_print_irq_info(hdev, event_type, false, &event_mask);
		gaudi_print_sm_sei_info(hdev, event_type,
					&eq_entry->sm_sei_data);
		rc = hl_state_dump(hdev);
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		if (rc)
			dev_err(hdev->dev,
				"Error during system state dump %d\n", rc);
		hl_fw_unmask_irq(hdev, event_type);
		break;

	case GAUDI_EVENT_STATUS_NIC0_ENG0 ... GAUDI_EVENT_STATUS_NIC4_ENG1:
		break;

	case GAUDI_EVENT_FIX_POWER_ENV_S ... GAUDI_EVENT_FIX_THERMAL_ENV_E:
		gaudi_print_clk_change_info(hdev, event_type, &event_mask);
		hl_fw_unmask_irq(hdev, event_type);
		break;

	case GAUDI_EVENT_PSOC_GPIO_U16_0:
		cause = le64_to_cpu(eq_entry->data[0]) & 0xFF;
		dev_err(hdev->dev,
			"Received high temp H/W interrupt %d (cause %d)\n",
			event_type, cause);
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		break;

	case GAUDI_EVENT_DEV_RESET_REQ:
		gaudi_print_irq_info(hdev, event_type, false, &event_mask);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		goto reset_device;

	case GAUDI_EVENT_PKT_QUEUE_OUT_SYNC:
		gaudi_print_irq_info(hdev, event_type, false, &event_mask);
		gaudi_print_out_of_sync_info(hdev, &eq_entry->pkt_sync_err);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		goto reset_device;

	case GAUDI_EVENT_FW_ALIVE_S:
		gaudi_print_irq_info(hdev, event_type, false, &event_mask);
		gaudi_print_fw_alive_info(hdev, &eq_entry->fw_alive);
		fw_err_info.err_type = HL_INFO_FW_REPORTED_ERR;
		fw_err_info.event_id = event_type;
		fw_err_info.event_mask = &event_mask;
		hl_handle_fw_err(hdev, &fw_err_info);
		goto reset_device;

	default:
		dev_err(hdev->dev, "Received invalid H/W interrupt %d\n",
				event_type);
		break;
	}

	if (event_mask)
		hl_notifier_event_send_all(hdev, event_mask);

	return;

reset_device:
	reset_required = true;

	if (hdev->asic_prop.fw_security_enabled && !reset_direct) {
		flags = HL_DRV_RESET_HARD | HL_DRV_RESET_BYPASS_REQ_TO_FW | fw_fatal_err_flag;

		/* notify on device unavailable while the reset triggered by fw */
		event_mask |= (HL_NOTIFIER_EVENT_DEVICE_RESET |
					HL_NOTIFIER_EVENT_DEVICE_UNAVAILABLE);
	} else if (hdev->hard_reset_on_fw_events) {
		flags = HL_DRV_RESET_HARD | HL_DRV_RESET_DELAY | fw_fatal_err_flag;
		event_mask |= HL_NOTIFIER_EVENT_DEVICE_RESET;
	} else {
		reset_required = false;
	}

	if (reset_required) {
		/* escalate general hw errors to critical/fatal error */
		if (event_mask & HL_NOTIFIER_EVENT_GENERAL_HW_ERR)
			hl_handle_critical_hw_err(hdev, event_type, &event_mask);

		hl_device_cond_reset(hdev, flags, event_mask);
	} else {
		hl_fw_unmask_irq(hdev, event_type);
		/* Notification on occurred event needs to be sent although reset is not executed */
		if (event_mask)
			hl_notifier_event_send_all(hdev, event_mask);
	}
}

static void *gaudi_get_events_stat(struct hl_device *hdev, bool aggregate, u32 *size)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (aggregate) {
		*size = (u32) sizeof(gaudi->events_stat_aggregate);
		return gaudi->events_stat_aggregate;
	}

	*size = (u32) sizeof(gaudi->events_stat);
	return gaudi->events_stat;
}

static int gaudi_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard, u32 flags)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 status, timeout_usec;
	int rc;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MMU) ||
		hdev->reset_info.hard_reset_pending)
		return 0;

	if (hdev->pldm)
		timeout_usec = GAUDI_PLDM_MMU_TIMEOUT_USEC;
	else
		timeout_usec = MMU_CONFIG_TIMEOUT_USEC;

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

	return rc;
}

static int gaudi_mmu_invalidate_cache_range(struct hl_device *hdev,
						bool is_hard, u32 flags,
						u32 asid, u64 va, u64 size)
{
	/* Treat as invalidate all because there is no range invalidation
	 * in Gaudi
	 */
	return hdev->asic_funcs->mmu_invalidate_cache(hdev, is_hard, flags);
}

static int gaudi_mmu_update_asid_hop0_addr(struct hl_device *hdev, u32 asid, u64 phys_addr)
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

	rc = hl_fw_cpucp_handshake(hdev, mmCPU_BOOT_DEV_STS0,
					mmCPU_BOOT_DEV_STS1, mmCPU_BOOT_ERR0,
					mmCPU_BOOT_ERR1);
	if (rc)
		return rc;

	if (!strlen(prop->cpucp_info.card_name))
		strscpy_pad(prop->cpucp_info.card_name, GAUDI_DEFAULT_CARD_NAME,
				CARD_NAME_MAX_LEN);

	hdev->card_type = le32_to_cpu(hdev->asic_prop.cpucp_info.card_type);

	set_default_power_values(hdev);

	return 0;
}

static bool gaudi_is_device_idle(struct hl_device *hdev, u64 *mask_arr, u8 mask_len,
		struct engines_data *e)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	const char *fmt = "%-5d%-9s%#-14x%#-12x%#x\n";
	const char *mme_slave_fmt = "%-5d%-9s%-14s%-12s%#x\n";
	const char *nic_fmt = "%-5d%-9s%#-14x%#x\n";
	unsigned long *mask = (unsigned long *)mask_arr;
	u32 qm_glbl_sts0, qm_cgm_sts, dma_core_sts0, tpc_cfg_sts, mme_arch_sts;
	bool is_idle = true, is_eng_idle, is_slave;
	u64 offset;
	int i, dma_id, port;

	if (e)
		hl_engine_data_sprintf(e,
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

		if (mask && !is_eng_idle)
			set_bit(GAUDI_ENGINE_ID_DMA_0 + dma_id, mask);
		if (e)
			hl_engine_data_sprintf(e, fmt, dma_id,
				is_eng_idle ? "Y" : "N", qm_glbl_sts0,
				qm_cgm_sts, dma_core_sts0);
	}

	if (e)
		hl_engine_data_sprintf(e,
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

		if (mask && !is_eng_idle)
			set_bit(GAUDI_ENGINE_ID_TPC_0 + i, mask);
		if (e)
			hl_engine_data_sprintf(e, fmt, i,
				is_eng_idle ? "Y" : "N",
				qm_glbl_sts0, qm_cgm_sts, tpc_cfg_sts);
	}

	if (e)
		hl_engine_data_sprintf(e,
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

		if (mask && !is_eng_idle)
			set_bit(GAUDI_ENGINE_ID_MME_0 + i, mask);
		if (e) {
			if (!is_slave)
				hl_engine_data_sprintf(e, fmt, i,
					is_eng_idle ? "Y" : "N",
					qm_glbl_sts0, qm_cgm_sts, mme_arch_sts);
			else
				hl_engine_data_sprintf(e, mme_slave_fmt, i,
					is_eng_idle ? "Y" : "N", "-",
					"-", mme_arch_sts);
		}
	}

	if (e)
		hl_engine_data_sprintf(e,
				"\nNIC  is_idle  QM_GLBL_STS0  QM_CGM_STS\n"
				"---  -------  ------------  ----------\n");

	for (i = 0 ; i < (NIC_NUMBER_OF_ENGINES / 2) ; i++) {
		offset = i * NIC_MACRO_QMAN_OFFSET;
		port = 2 * i;
		if (gaudi->hw_cap_initialized & BIT(HW_CAP_NIC_SHIFT + port)) {
			qm_glbl_sts0 = RREG32(mmNIC0_QM0_GLBL_STS0 + offset);
			qm_cgm_sts = RREG32(mmNIC0_QM0_CGM_STS + offset);
			is_eng_idle = IS_QM_IDLE(qm_glbl_sts0, qm_cgm_sts);
			is_idle &= is_eng_idle;

			if (mask && !is_eng_idle)
				set_bit(GAUDI_ENGINE_ID_NIC_0 + port, mask);
			if (e)
				hl_engine_data_sprintf(e, nic_fmt, port,
						is_eng_idle ? "Y" : "N",
						qm_glbl_sts0, qm_cgm_sts);
		}

		port = 2 * i + 1;
		if (gaudi->hw_cap_initialized & BIT(HW_CAP_NIC_SHIFT + port)) {
			qm_glbl_sts0 = RREG32(mmNIC0_QM1_GLBL_STS0 + offset);
			qm_cgm_sts = RREG32(mmNIC0_QM1_CGM_STS + offset);
			is_eng_idle = IS_QM_IDLE(qm_glbl_sts0, qm_cgm_sts);
			is_idle &= is_eng_idle;

			if (mask && !is_eng_idle)
				set_bit(GAUDI_ENGINE_ID_NIC_0 + port, mask);
			if (e)
				hl_engine_data_sprintf(e, nic_fmt, port,
						is_eng_idle ? "Y" : "N",
						qm_glbl_sts0, qm_cgm_sts);
		}
	}

	if (e)
		hl_engine_data_sprintf(e, "\n");

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

static int gaudi_get_monitor_dump(struct hl_device *hdev, void *data)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	return hl_fw_get_monitor_dump(hdev, data);
}

/*
 * this function should be used only during initialization and/or after reset,
 * when there are no active users.
 */
static int gaudi_run_tpc_kernel(struct hl_device *hdev, u64 tpc_kernel,	u32 tpc_id)
{
	u64 kernel_timeout;
	u32 status, offset;
	int rc;

	offset = tpc_id * (mmTPC1_CFG_STATUS - mmTPC0_CFG_STATUS);

	if (hdev->pldm)
		kernel_timeout = GAUDI_PLDM_TPC_KERNEL_WAIT_USEC;
	else
		kernel_timeout = HL_DEVICE_TIMEOUT_USEC;

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
		return -EIO;
	}

	rc = hl_poll_timeout(
		hdev,
		mmTPC0_CFG_WQ_INFLIGHT_CNTR + offset,
		status,
		(status == 0),
		1000,
		kernel_timeout);

	if (rc) {
		dev_err(hdev->dev,
			"Timeout while waiting for TPC%d kernel to execute\n",
			tpc_id);
		return -EIO;
	}

	return 0;
}

static int gaudi_internal_cb_pool_init(struct hl_device *hdev,
		struct hl_ctx *ctx)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int min_alloc_order, rc, collective_cb_size;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MMU))
		return 0;

	hdev->internal_cb_pool_virt_addr = hl_asic_dma_alloc_coherent(hdev,
							HOST_SPACE_INTERNAL_CB_SZ,
							&hdev->internal_cb_pool_dma_addr,
							GFP_KERNEL | __GFP_ZERO);

	if (!hdev->internal_cb_pool_virt_addr)
		return -ENOMEM;

	collective_cb_size = sizeof(struct packet_msg_short) * 5 +
			sizeof(struct packet_fence);
	min_alloc_order = ilog2(collective_cb_size);

	hdev->internal_cb_pool = gen_pool_create(min_alloc_order, -1);
	if (!hdev->internal_cb_pool) {
		dev_err(hdev->dev,
			"Failed to create internal CB pool\n");
		rc = -ENOMEM;
		goto free_internal_cb_pool;
	}

	rc = gen_pool_add(hdev->internal_cb_pool,
				(uintptr_t) hdev->internal_cb_pool_virt_addr,
				HOST_SPACE_INTERNAL_CB_SZ, -1);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to add memory to internal CB pool\n");
		rc = -EFAULT;
		goto destroy_internal_cb_pool;
	}

	hdev->internal_cb_va_base = hl_reserve_va_block(hdev, ctx,
			HL_VA_RANGE_TYPE_HOST, HOST_SPACE_INTERNAL_CB_SZ,
			HL_MMU_VA_ALIGNMENT_NOT_NEEDED);

	if (!hdev->internal_cb_va_base) {
		rc = -ENOMEM;
		goto destroy_internal_cb_pool;
	}

	mutex_lock(&hdev->mmu_lock);

	rc = hl_mmu_map_contiguous(ctx, hdev->internal_cb_va_base,
			hdev->internal_cb_pool_dma_addr,
			HOST_SPACE_INTERNAL_CB_SZ);
	if (rc)
		goto unreserve_internal_cb_pool;

	rc = hl_mmu_invalidate_cache(hdev, false, MMU_OP_USERPTR);
	if (rc)
		goto unmap_internal_cb_pool;

	mutex_unlock(&hdev->mmu_lock);

	return 0;

unmap_internal_cb_pool:
	hl_mmu_unmap_contiguous(ctx, hdev->internal_cb_va_base,
			HOST_SPACE_INTERNAL_CB_SZ);
unreserve_internal_cb_pool:
	mutex_unlock(&hdev->mmu_lock);
	hl_unreserve_va_block(hdev, ctx, hdev->internal_cb_va_base,
			HOST_SPACE_INTERNAL_CB_SZ);
destroy_internal_cb_pool:
	gen_pool_destroy(hdev->internal_cb_pool);
free_internal_cb_pool:
	hl_asic_dma_free_coherent(hdev, HOST_SPACE_INTERNAL_CB_SZ, hdev->internal_cb_pool_virt_addr,
					hdev->internal_cb_pool_dma_addr);

	return rc;
}

static void gaudi_internal_cb_pool_fini(struct hl_device *hdev,
		struct hl_ctx *ctx)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (!(gaudi->hw_cap_initialized & HW_CAP_MMU))
		return;

	mutex_lock(&hdev->mmu_lock);
	hl_mmu_unmap_contiguous(ctx, hdev->internal_cb_va_base,
			HOST_SPACE_INTERNAL_CB_SZ);
	hl_unreserve_va_block(hdev, ctx, hdev->internal_cb_va_base,
			HOST_SPACE_INTERNAL_CB_SZ);
	hl_mmu_invalidate_cache(hdev, true, MMU_OP_USERPTR);
	mutex_unlock(&hdev->mmu_lock);

	gen_pool_destroy(hdev->internal_cb_pool);

	hl_asic_dma_free_coherent(hdev, HOST_SPACE_INTERNAL_CB_SZ, hdev->internal_cb_pool_virt_addr,
					hdev->internal_cb_pool_dma_addr);
}

static int gaudi_ctx_init(struct hl_ctx *ctx)
{
	int rc;

	if (ctx->asid == HL_KERNEL_ASID_ID)
		return 0;

	rc = gaudi_internal_cb_pool_init(ctx->hdev, ctx);
	if (rc)
		return rc;

	rc = gaudi_restore_user_registers(ctx->hdev);
	if (rc)
		gaudi_internal_cb_pool_fini(ctx->hdev, ctx);

	return rc;
}

static void gaudi_ctx_fini(struct hl_ctx *ctx)
{
	if (ctx->asid == HL_KERNEL_ASID_ID)
		return;

	gaudi_internal_cb_pool_fini(ctx->hdev, ctx);
}

static int gaudi_pre_schedule_cs(struct hl_cs *cs)
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

static u32 gaudi_get_sob_addr(struct hl_device *hdev, u32 sob_id)
{
	return mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_SOB_OBJ_0 + (sob_id * 4);
}

static u32 gaudi_gen_signal_cb(struct hl_device *hdev, void *data, u16 sob_id,
				u32 size, bool eb)
{
	struct hl_cb *cb = (struct hl_cb *) data;
	struct packet_msg_short *pkt;
	u32 value, ctl, pkt_size = sizeof(*pkt);

	pkt = cb->kernel_address + size;
	memset(pkt, 0, pkt_size);

	/* Inc by 1, Mode ADD */
	value = FIELD_PREP(GAUDI_PKT_SHORT_VAL_SOB_SYNC_VAL_MASK, 1);
	value |= FIELD_PREP(GAUDI_PKT_SHORT_VAL_SOB_MOD_MASK, 1);

	ctl = FIELD_PREP(GAUDI_PKT_SHORT_CTL_ADDR_MASK, sob_id * 4);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_OP_MASK, 0); /* write the value */
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_BASE_MASK, 3); /* W_S SOB base */
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_MSG_SHORT);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_EB_MASK, eb);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_RB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_MB_MASK, 1);

	pkt->value = cpu_to_le32(value);
	pkt->ctl = cpu_to_le32(ctl);

	return size + pkt_size;
}

static u32 gaudi_add_mon_msg_short(struct packet_msg_short *pkt, u32 value,
					u16 addr)
{
	u32 ctl, pkt_size = sizeof(*pkt);

	memset(pkt, 0, pkt_size);

	ctl = FIELD_PREP(GAUDI_PKT_SHORT_CTL_ADDR_MASK, addr);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_BASE_MASK, 2);  /* W_S MON base */
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_MSG_SHORT);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_EB_MASK, 0);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_RB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_MB_MASK, 0); /* last pkt MB */

	pkt->value = cpu_to_le32(value);
	pkt->ctl = cpu_to_le32(ctl);

	return pkt_size;
}

static u32 gaudi_add_arm_monitor_pkt(struct hl_device *hdev,
		struct packet_msg_short *pkt, u16 sob_base, u8 sob_mask,
		u16 sob_val, u16 mon_id)
{
	u64 monitor_base;
	u32 ctl, value, pkt_size = sizeof(*pkt);
	u16 msg_addr_offset;
	u8 mask;

	if (hl_gen_sob_mask(sob_base, sob_mask, &mask)) {
		dev_err(hdev->dev,
			"sob_base %u (mask %#x) is not valid\n",
			sob_base, sob_mask);
		return 0;
	}

	/*
	 * monitor_base should be the content of the base0 address registers,
	 * so it will be added to the msg short offsets
	 */
	monitor_base = mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0;

	msg_addr_offset =
		(mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_ARM_0 + mon_id * 4) -
				monitor_base;

	memset(pkt, 0, pkt_size);

	/* Monitor config packet: bind the monitor to a sync object */
	value = FIELD_PREP(GAUDI_PKT_SHORT_VAL_MON_SYNC_GID_MASK, sob_base / 8);
	value |= FIELD_PREP(GAUDI_PKT_SHORT_VAL_MON_SYNC_VAL_MASK, sob_val);
	value |= FIELD_PREP(GAUDI_PKT_SHORT_VAL_MON_MODE_MASK,
			0); /* GREATER OR EQUAL*/
	value |= FIELD_PREP(GAUDI_PKT_SHORT_VAL_MON_MASK_MASK, mask);

	ctl = FIELD_PREP(GAUDI_PKT_SHORT_CTL_ADDR_MASK, msg_addr_offset);
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_OP_MASK, 0); /* write the value */
	ctl |= FIELD_PREP(GAUDI_PKT_SHORT_CTL_BASE_MASK, 2); /* W_S MON base */
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_MSG_SHORT);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_EB_MASK, 0);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_RB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_MB_MASK, 1);

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

	ctl = FIELD_PREP(GAUDI_PKT_CTL_OPCODE_MASK, PACKET_FENCE);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_EB_MASK, 0);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_RB_MASK, 1);
	ctl |= FIELD_PREP(GAUDI_PKT_CTL_MB_MASK, 1);

	pkt->cfg = cpu_to_le32(cfg);
	pkt->ctl = cpu_to_le32(ctl);

	return pkt_size;
}

static int gaudi_get_fence_addr(struct hl_device *hdev, u32 queue_id, u64 *addr)
{
	u32 offset, nic_index;

	switch (queue_id) {
	case GAUDI_QUEUE_ID_DMA_0_0:
		offset = mmDMA0_QM_CP_FENCE2_RDATA_0;
		break;
	case GAUDI_QUEUE_ID_DMA_0_1:
		offset = mmDMA0_QM_CP_FENCE2_RDATA_1;
		break;
	case GAUDI_QUEUE_ID_DMA_0_2:
		offset = mmDMA0_QM_CP_FENCE2_RDATA_2;
		break;
	case GAUDI_QUEUE_ID_DMA_0_3:
		offset = mmDMA0_QM_CP_FENCE2_RDATA_3;
		break;
	case GAUDI_QUEUE_ID_DMA_1_0:
		offset = mmDMA1_QM_CP_FENCE2_RDATA_0;
		break;
	case GAUDI_QUEUE_ID_DMA_1_1:
		offset = mmDMA1_QM_CP_FENCE2_RDATA_1;
		break;
	case GAUDI_QUEUE_ID_DMA_1_2:
		offset = mmDMA1_QM_CP_FENCE2_RDATA_2;
		break;
	case GAUDI_QUEUE_ID_DMA_1_3:
		offset = mmDMA1_QM_CP_FENCE2_RDATA_3;
		break;
	case GAUDI_QUEUE_ID_DMA_5_0:
		offset = mmDMA5_QM_CP_FENCE2_RDATA_0;
		break;
	case GAUDI_QUEUE_ID_DMA_5_1:
		offset = mmDMA5_QM_CP_FENCE2_RDATA_1;
		break;
	case GAUDI_QUEUE_ID_DMA_5_2:
		offset = mmDMA5_QM_CP_FENCE2_RDATA_2;
		break;
	case GAUDI_QUEUE_ID_DMA_5_3:
		offset = mmDMA5_QM_CP_FENCE2_RDATA_3;
		break;
	case GAUDI_QUEUE_ID_TPC_7_0:
		offset = mmTPC7_QM_CP_FENCE2_RDATA_0;
		break;
	case GAUDI_QUEUE_ID_TPC_7_1:
		offset = mmTPC7_QM_CP_FENCE2_RDATA_1;
		break;
	case GAUDI_QUEUE_ID_TPC_7_2:
		offset = mmTPC7_QM_CP_FENCE2_RDATA_2;
		break;
	case GAUDI_QUEUE_ID_TPC_7_3:
		offset = mmTPC7_QM_CP_FENCE2_RDATA_3;
		break;
	case GAUDI_QUEUE_ID_NIC_0_0:
	case GAUDI_QUEUE_ID_NIC_1_0:
	case GAUDI_QUEUE_ID_NIC_2_0:
	case GAUDI_QUEUE_ID_NIC_3_0:
	case GAUDI_QUEUE_ID_NIC_4_0:
	case GAUDI_QUEUE_ID_NIC_5_0:
	case GAUDI_QUEUE_ID_NIC_6_0:
	case GAUDI_QUEUE_ID_NIC_7_0:
	case GAUDI_QUEUE_ID_NIC_8_0:
	case GAUDI_QUEUE_ID_NIC_9_0:
		nic_index = (queue_id - GAUDI_QUEUE_ID_NIC_0_0) >> 2;
		offset = mmNIC0_QM0_CP_FENCE2_RDATA_0 +
				(nic_index >> 1) * NIC_MACRO_QMAN_OFFSET +
				(nic_index & 0x1) * NIC_ENGINE_QMAN_OFFSET;
		break;
	case GAUDI_QUEUE_ID_NIC_0_1:
	case GAUDI_QUEUE_ID_NIC_1_1:
	case GAUDI_QUEUE_ID_NIC_2_1:
	case GAUDI_QUEUE_ID_NIC_3_1:
	case GAUDI_QUEUE_ID_NIC_4_1:
	case GAUDI_QUEUE_ID_NIC_5_1:
	case GAUDI_QUEUE_ID_NIC_6_1:
	case GAUDI_QUEUE_ID_NIC_7_1:
	case GAUDI_QUEUE_ID_NIC_8_1:
	case GAUDI_QUEUE_ID_NIC_9_1:
		nic_index = (queue_id - GAUDI_QUEUE_ID_NIC_0_1) >> 2;
		offset = mmNIC0_QM0_CP_FENCE2_RDATA_1 +
				(nic_index >> 1) * NIC_MACRO_QMAN_OFFSET +
				(nic_index & 0x1) * NIC_ENGINE_QMAN_OFFSET;
		break;
	case GAUDI_QUEUE_ID_NIC_0_2:
	case GAUDI_QUEUE_ID_NIC_1_2:
	case GAUDI_QUEUE_ID_NIC_2_2:
	case GAUDI_QUEUE_ID_NIC_3_2:
	case GAUDI_QUEUE_ID_NIC_4_2:
	case GAUDI_QUEUE_ID_NIC_5_2:
	case GAUDI_QUEUE_ID_NIC_6_2:
	case GAUDI_QUEUE_ID_NIC_7_2:
	case GAUDI_QUEUE_ID_NIC_8_2:
	case GAUDI_QUEUE_ID_NIC_9_2:
		nic_index = (queue_id - GAUDI_QUEUE_ID_NIC_0_2) >> 2;
		offset = mmNIC0_QM0_CP_FENCE2_RDATA_2 +
				(nic_index >> 1) * NIC_MACRO_QMAN_OFFSET +
				(nic_index & 0x1) * NIC_ENGINE_QMAN_OFFSET;
		break;
	case GAUDI_QUEUE_ID_NIC_0_3:
	case GAUDI_QUEUE_ID_NIC_1_3:
	case GAUDI_QUEUE_ID_NIC_2_3:
	case GAUDI_QUEUE_ID_NIC_3_3:
	case GAUDI_QUEUE_ID_NIC_4_3:
	case GAUDI_QUEUE_ID_NIC_5_3:
	case GAUDI_QUEUE_ID_NIC_6_3:
	case GAUDI_QUEUE_ID_NIC_7_3:
	case GAUDI_QUEUE_ID_NIC_8_3:
	case GAUDI_QUEUE_ID_NIC_9_3:
		nic_index = (queue_id - GAUDI_QUEUE_ID_NIC_0_3) >> 2;
		offset = mmNIC0_QM0_CP_FENCE2_RDATA_3 +
				(nic_index >> 1) * NIC_MACRO_QMAN_OFFSET +
				(nic_index & 0x1) * NIC_ENGINE_QMAN_OFFSET;
		break;
	default:
		return -EINVAL;
	}

	*addr = CFG_BASE + offset;

	return 0;
}

static u32 gaudi_add_mon_pkts(void *buf, u16 mon_id, u64 fence_addr)
{
	u64 monitor_base;
	u32 size = 0;
	u16 msg_addr_offset;

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

	return size;
}

static u32 gaudi_gen_wait_cb(struct hl_device *hdev,
				struct hl_gen_wait_properties *prop)
{
	struct hl_cb *cb = (struct hl_cb *) prop->data;
	void *buf = cb->kernel_address;
	u64 fence_addr = 0;
	u32 size = prop->size;

	if (gaudi_get_fence_addr(hdev, prop->q_idx, &fence_addr)) {
		dev_crit(hdev->dev, "wrong queue id %d for wait packet\n",
				prop->q_idx);
		return 0;
	}

	size += gaudi_add_mon_pkts(buf + size, prop->mon_id, fence_addr);
	size += gaudi_add_arm_monitor_pkt(hdev, buf + size, prop->sob_base,
			prop->sob_mask, prop->sob_val, prop->mon_id);
	size += gaudi_add_fence_pkt(buf + size);

	return size;
}

static void gaudi_reset_sob(struct hl_device *hdev, void *data)
{
	struct hl_hw_sob *hw_sob = (struct hl_hw_sob *) data;

	dev_dbg(hdev->dev, "reset SOB, q_idx: %d, sob_id: %d\n", hw_sob->q_idx,
		hw_sob->sob_id);

	WREG32(mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_SOB_OBJ_0 +
			hw_sob->sob_id * 4, 0);

	kref_init(&hw_sob->kref);
}

static u64 gaudi_get_device_time(struct hl_device *hdev)
{
	u64 device_time = ((u64) RREG32(mmPSOC_TIMESTAMP_CNTCVU)) << 32;

	return device_time | RREG32(mmPSOC_TIMESTAMP_CNTCVL);
}

static int gaudi_get_hw_block_id(struct hl_device *hdev, u64 block_addr,
				u32 *block_size, u32 *block_id)
{
	return -EPERM;
}

static int gaudi_block_mmap(struct hl_device *hdev,
				struct vm_area_struct *vma,
				u32 block_id, u32 block_size)
{
	return -EPERM;
}

static void gaudi_enable_events_from_fw(struct hl_device *hdev)
{
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 irq_handler_offset = hdev->asic_prop.gic_interrupts_enable ?
			mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR :
			le32_to_cpu(dyn_regs->gic_host_ints_irq);

	WREG32(irq_handler_offset,
		gaudi_irq_map_table[GAUDI_EVENT_INTS_REGISTER].cpu_id);
}

static int gaudi_ack_mmu_page_fault_or_access_error(struct hl_device *hdev, u64 mmu_cap_mask)
{
	return -EINVAL;
}

static int gaudi_map_pll_idx_to_fw_idx(u32 pll_idx)
{
	switch (pll_idx) {
	case HL_GAUDI_CPU_PLL: return CPU_PLL;
	case HL_GAUDI_PCI_PLL: return PCI_PLL;
	case HL_GAUDI_NIC_PLL: return NIC_PLL;
	case HL_GAUDI_DMA_PLL: return DMA_PLL;
	case HL_GAUDI_MESH_PLL: return MESH_PLL;
	case HL_GAUDI_MME_PLL: return MME_PLL;
	case HL_GAUDI_TPC_PLL: return TPC_PLL;
	case HL_GAUDI_IF_PLL: return IF_PLL;
	case HL_GAUDI_SRAM_PLL: return SRAM_PLL;
	case HL_GAUDI_HBM_PLL: return HBM_PLL;
	default: return -EINVAL;
	}
}

static int gaudi_add_sync_to_engine_map_entry(
	struct hl_sync_to_engine_map *map, u32 reg_value,
	enum hl_sync_engine_type engine_type, u32 engine_id)
{
	struct hl_sync_to_engine_map_entry *entry;

	/* Reg value represents a partial address of sync object,
	 * it is used as unique identifier. For this we need to
	 * clear the cutoff cfg base bits from the value.
	 */
	if (reg_value == 0 || reg_value == 0xffffffff)
		return 0;
	reg_value -= lower_32_bits(CFG_BASE);

	/* create a new hash entry */
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	entry->engine_type = engine_type;
	entry->engine_id = engine_id;
	entry->sync_id = reg_value;
	hash_add(map->tb, &entry->node, reg_value);

	return 0;
}

static int gaudi_gen_sync_to_engine_map(struct hl_device *hdev,
				struct hl_sync_to_engine_map *map)
{
	struct hl_state_dump_specs *sds = &hdev->state_dump_specs;
	int i, j, rc;
	u32 reg_value;

	/* Iterate over TPC engines */
	for (i = 0; i < sds->props[SP_NUM_OF_TPC_ENGINES]; ++i) {

		reg_value = RREG32(sds->props[SP_TPC0_CFG_SO] +
					sds->props[SP_NEXT_TPC] * i);

		rc = gaudi_add_sync_to_engine_map_entry(map, reg_value,
							ENGINE_TPC, i);
		if (rc)
			goto free_sync_to_engine_map;
	}

	/* Iterate over MME engines */
	for (i = 0; i < sds->props[SP_NUM_OF_MME_ENGINES]; ++i) {
		for (j = 0; j < sds->props[SP_SUB_MME_ENG_NUM]; ++j) {

			reg_value = RREG32(sds->props[SP_MME_CFG_SO] +
						sds->props[SP_NEXT_MME] * i +
						j * sizeof(u32));

			rc = gaudi_add_sync_to_engine_map_entry(
				map, reg_value, ENGINE_MME,
				i * sds->props[SP_SUB_MME_ENG_NUM] + j);
			if (rc)
				goto free_sync_to_engine_map;
		}
	}

	/* Iterate over DMA engines */
	for (i = 0; i < sds->props[SP_NUM_OF_DMA_ENGINES]; ++i) {
		reg_value = RREG32(sds->props[SP_DMA_CFG_SO] +
					sds->props[SP_DMA_QUEUES_OFFSET] * i);
		rc = gaudi_add_sync_to_engine_map_entry(map, reg_value,
							ENGINE_DMA, i);
		if (rc)
			goto free_sync_to_engine_map;
	}

	return 0;

free_sync_to_engine_map:
	hl_state_dump_free_sync_to_engine_map(map);

	return rc;
}

static int gaudi_monitor_valid(struct hl_mon_state_dump *mon)
{
	return FIELD_GET(
		SYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_STATUS_0_VALID_MASK,
		mon->status);
}

static void gaudi_fill_sobs_from_mon(char *sobs, struct hl_mon_state_dump *mon)
{
	const size_t max_write = 10;
	u32 gid, mask, sob;
	int i, offset;

	/* Sync object ID is calculated as follows:
	 * (8 * group_id + cleared bits in mask)
	 */
	gid = FIELD_GET(SYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_ARM_0_SID_MASK,
			mon->arm_data);
	mask = FIELD_GET(SYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_ARM_0_MASK_MASK,
			mon->arm_data);

	for (i = 0, offset = 0; mask && offset < MONITOR_SOB_STRING_SIZE -
		max_write; mask >>= 1, i++) {
		if (!(mask & 1)) {
			sob = gid * MONITOR_MAX_SOBS + i;

			if (offset > 0)
				offset += snprintf(sobs + offset, max_write,
							", ");

			offset += snprintf(sobs + offset, max_write, "%u", sob);
		}
	}
}

static int gaudi_print_single_monitor(char **buf, size_t *size, size_t *offset,
				struct hl_device *hdev,
				struct hl_mon_state_dump *mon)
{
	const char *name;
	char scratch_buf1[BIN_REG_STRING_SIZE],
		scratch_buf2[BIN_REG_STRING_SIZE];
	char monitored_sobs[MONITOR_SOB_STRING_SIZE] = {0};

	name = hl_state_dump_get_monitor_name(hdev, mon);
	if (!name)
		name = "";

	gaudi_fill_sobs_from_mon(monitored_sobs, mon);

	return hl_snprintf_resize(
		buf, size, offset,
		"Mon id: %u%s, wait for group id: %u mask %s to reach val: %u and write %u to address 0x%llx. Pending: %s. Means sync objects [%s] are being monitored.",
		mon->id, name,
		FIELD_GET(SYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_ARM_0_SID_MASK,
				mon->arm_data),
		hl_format_as_binary(
			scratch_buf1, sizeof(scratch_buf1),
			FIELD_GET(
				SYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_ARM_0_MASK_MASK,
				mon->arm_data)),
		FIELD_GET(SYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_ARM_0_SOD_MASK,
				mon->arm_data),
		mon->wr_data,
		(((u64)mon->wr_addr_high) << 32) | mon->wr_addr_low,
		hl_format_as_binary(
			scratch_buf2, sizeof(scratch_buf2),
			FIELD_GET(
				SYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_STATUS_0_PENDING_MASK,
				mon->status)),
		monitored_sobs);
}


static int gaudi_print_fences_single_engine(
	struct hl_device *hdev, u64 base_offset, u64 status_base_offset,
	enum hl_sync_engine_type engine_type, u32 engine_id, char **buf,
	size_t *size, size_t *offset)
{
	struct hl_state_dump_specs *sds = &hdev->state_dump_specs;
	int rc = -ENOMEM, i;
	u32 *statuses, *fences;

	statuses = kcalloc(sds->props[SP_ENGINE_NUM_OF_QUEUES],
			sizeof(*statuses), GFP_KERNEL);
	if (!statuses)
		goto out;

	fences = kcalloc(sds->props[SP_ENGINE_NUM_OF_FENCES] *
				sds->props[SP_ENGINE_NUM_OF_QUEUES],
			 sizeof(*fences), GFP_KERNEL);
	if (!fences)
		goto free_status;

	for (i = 0; i < sds->props[SP_ENGINE_NUM_OF_FENCES]; ++i)
		statuses[i] = RREG32(status_base_offset + i * sizeof(u32));

	for (i = 0; i < sds->props[SP_ENGINE_NUM_OF_FENCES] *
				sds->props[SP_ENGINE_NUM_OF_QUEUES]; ++i)
		fences[i] = RREG32(base_offset + i * sizeof(u32));

	/* The actual print */
	for (i = 0; i < sds->props[SP_ENGINE_NUM_OF_QUEUES]; ++i) {
		u32 fence_id;
		u64 fence_cnt, fence_rdata;
		const char *engine_name;

		if (!FIELD_GET(TPC0_QM_CP_STS_0_FENCE_IN_PROGRESS_MASK,
			statuses[i]))
			continue;

		fence_id =
			FIELD_GET(TPC0_QM_CP_STS_0_FENCE_ID_MASK, statuses[i]);
		fence_cnt = base_offset + CFG_BASE +
			sizeof(u32) *
			(i + fence_id * sds->props[SP_ENGINE_NUM_OF_QUEUES]);
		fence_rdata = fence_cnt - sds->props[SP_FENCE0_CNT_OFFSET] +
				sds->props[SP_FENCE0_RDATA_OFFSET];
		engine_name = hl_sync_engine_to_string(engine_type);

		rc = hl_snprintf_resize(
			buf, size, offset,
			"%s%u, stream %u: fence id %u cnt = 0x%llx (%s%u_QM.CP_FENCE%u_CNT_%u) rdata = 0x%llx (%s%u_QM.CP_FENCE%u_RDATA_%u) value = %u, cp_status = %u\n",
			engine_name, engine_id,
			i, fence_id,
			fence_cnt, engine_name, engine_id, fence_id, i,
			fence_rdata, engine_name, engine_id, fence_id, i,
			fences[fence_id],
			statuses[i]);
		if (rc)
			goto free_fences;
	}

	rc = 0;

free_fences:
	kfree(fences);
free_status:
	kfree(statuses);
out:
	return rc;
}


static struct hl_state_dump_specs_funcs gaudi_state_dump_funcs = {
	.monitor_valid = gaudi_monitor_valid,
	.print_single_monitor = gaudi_print_single_monitor,
	.gen_sync_to_engine_map = gaudi_gen_sync_to_engine_map,
	.print_fences_single_engine = gaudi_print_fences_single_engine,
};

static void gaudi_state_dump_init(struct hl_device *hdev)
{
	struct hl_state_dump_specs *sds = &hdev->state_dump_specs;
	int i;

	for (i = 0; i < ARRAY_SIZE(gaudi_so_id_to_str); ++i)
		hash_add(sds->so_id_to_str_tb,
			&gaudi_so_id_to_str[i].node,
			gaudi_so_id_to_str[i].id);

	for (i = 0; i < ARRAY_SIZE(gaudi_monitor_id_to_str); ++i)
		hash_add(sds->monitor_id_to_str_tb,
			&gaudi_monitor_id_to_str[i].node,
			gaudi_monitor_id_to_str[i].id);

	sds->props = gaudi_state_dump_specs_props;

	sds->sync_namager_names = gaudi_sync_manager_names;

	sds->funcs = gaudi_state_dump_funcs;
}

static u32 *gaudi_get_stream_master_qid_arr(void)
{
	return gaudi_stream_master;
}

static int gaudi_set_dram_properties(struct hl_device *hdev)
{
	return 0;
}

static int gaudi_set_binning_masks(struct hl_device *hdev)
{
	return 0;
}

static void gaudi_check_if_razwi_happened(struct hl_device *hdev)
{
}

static ssize_t infineon_ver_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	struct cpucp_info *cpucp_info;

	cpucp_info = &hdev->asic_prop.cpucp_info;

	return sprintf(buf, "%#04x\n", le32_to_cpu(cpucp_info->infineon_version));
}

static DEVICE_ATTR_RO(infineon_ver);

static struct attribute *gaudi_vrm_dev_attrs[] = {
	&dev_attr_infineon_ver.attr,
	NULL,
};

static void gaudi_add_device_attr(struct hl_device *hdev, struct attribute_group *dev_clk_attr_grp,
					struct attribute_group *dev_vrm_attr_grp)
{
	hl_sysfs_add_dev_clk_attr(hdev, dev_clk_attr_grp);
	dev_vrm_attr_grp->attrs = gaudi_vrm_dev_attrs;
}

static int gaudi_send_device_activity(struct hl_device *hdev, bool open)
{
	return 0;
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
	.mmap = gaudi_mmap,
	.ring_doorbell = gaudi_ring_doorbell,
	.pqe_write = gaudi_pqe_write,
	.asic_dma_alloc_coherent = gaudi_dma_alloc_coherent,
	.asic_dma_free_coherent = gaudi_dma_free_coherent,
	.scrub_device_mem = gaudi_scrub_device_mem,
	.scrub_device_dram = gaudi_scrub_device_dram,
	.get_int_queue_base = gaudi_get_int_queue_base,
	.test_queues = gaudi_test_queues,
	.asic_dma_pool_zalloc = gaudi_dma_pool_zalloc,
	.asic_dma_pool_free = gaudi_dma_pool_free,
	.cpu_accessible_dma_pool_alloc = gaudi_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = gaudi_cpu_accessible_dma_pool_free,
	.dma_unmap_sgtable = hl_asic_dma_unmap_sgtable,
	.cs_parser = gaudi_cs_parser,
	.dma_map_sgtable = hl_asic_dma_map_sgtable,
	.add_end_of_cb_packets = gaudi_add_end_of_cb_packets,
	.update_eq_ci = gaudi_update_eq_ci,
	.context_switch = gaudi_context_switch,
	.restore_phase_topology = gaudi_restore_phase_topology,
	.debugfs_read_dma = gaudi_debugfs_read_dma,
	.add_device_attr = gaudi_add_device_attr,
	.handle_eqe = gaudi_handle_eqe,
	.get_events_stat = gaudi_get_events_stat,
	.read_pte = gaudi_read_pte,
	.write_pte = gaudi_write_pte,
	.mmu_invalidate_cache = gaudi_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = gaudi_mmu_invalidate_cache_range,
	.mmu_prefetch_cache_range = NULL,
	.send_heartbeat = gaudi_send_heartbeat,
	.debug_coresight = gaudi_debug_coresight,
	.is_device_idle = gaudi_is_device_idle,
	.compute_reset_late_init = gaudi_compute_reset_late_init,
	.hw_queues_lock = gaudi_hw_queues_lock,
	.hw_queues_unlock = gaudi_hw_queues_unlock,
	.get_pci_id = gaudi_get_pci_id,
	.get_eeprom_data = gaudi_get_eeprom_data,
	.get_monitor_dump = gaudi_get_monitor_dump,
	.send_cpu_message = gaudi_send_cpu_message,
	.pci_bars_map = gaudi_pci_bars_map,
	.init_iatu = gaudi_init_iatu,
	.rreg = hl_rreg,
	.wreg = hl_wreg,
	.halt_coresight = gaudi_halt_coresight,
	.ctx_init = gaudi_ctx_init,
	.ctx_fini = gaudi_ctx_fini,
	.pre_schedule_cs = gaudi_pre_schedule_cs,
	.get_queue_id_for_cq = gaudi_get_queue_id_for_cq,
	.load_firmware_to_device = gaudi_load_firmware_to_device,
	.load_boot_fit_to_device = gaudi_load_boot_fit_to_device,
	.get_signal_cb_size = gaudi_get_signal_cb_size,
	.get_wait_cb_size = gaudi_get_wait_cb_size,
	.gen_signal_cb = gaudi_gen_signal_cb,
	.gen_wait_cb = gaudi_gen_wait_cb,
	.reset_sob = gaudi_reset_sob,
	.reset_sob_group = gaudi_reset_sob_group,
	.get_device_time = gaudi_get_device_time,
	.pb_print_security_errors = NULL,
	.collective_wait_init_cs = gaudi_collective_wait_init_cs,
	.collective_wait_create_jobs = gaudi_collective_wait_create_jobs,
	.get_dec_base_addr = NULL,
	.scramble_addr = hl_mmu_scramble_addr,
	.descramble_addr = hl_mmu_descramble_addr,
	.ack_protection_bits_errors = gaudi_ack_protection_bits_errors,
	.get_hw_block_id = gaudi_get_hw_block_id,
	.hw_block_mmap = gaudi_block_mmap,
	.enable_events_from_fw = gaudi_enable_events_from_fw,
	.ack_mmu_errors = gaudi_ack_mmu_page_fault_or_access_error,
	.map_pll_idx_to_fw_idx = gaudi_map_pll_idx_to_fw_idx,
	.init_firmware_preload_params = gaudi_init_firmware_preload_params,
	.init_firmware_loader = gaudi_init_firmware_loader,
	.init_cpu_scrambler_dram = gaudi_init_scrambler_hbm,
	.state_dump_init = gaudi_state_dump_init,
	.get_sob_addr = gaudi_get_sob_addr,
	.set_pci_memory_regions = gaudi_set_pci_memory_regions,
	.get_stream_master_qid_arr = gaudi_get_stream_master_qid_arr,
	.check_if_razwi_happened = gaudi_check_if_razwi_happened,
	.mmu_get_real_page_size = hl_mmu_get_real_page_size,
	.access_dev_mem = hl_access_dev_mem,
	.set_dram_bar_base = gaudi_set_hbm_bar_base,
	.send_device_activity = gaudi_send_device_activity,
	.set_dram_properties = gaudi_set_dram_properties,
	.set_binning_masks = gaudi_set_binning_masks,
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
