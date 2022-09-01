/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2020-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI2P_H_
#define GAUDI2P_H_

#include <uapi/misc/habanalabs.h>
#include "../common/habanalabs.h"
#include "../include/common/hl_boot_if.h"
#include "../include/gaudi2/gaudi2.h"
#include "../include/gaudi2/gaudi2_packets.h"
#include "../include/gaudi2/gaudi2_fw_if.h"
#include "../include/gaudi2/gaudi2_async_events.h"
#include "../include/gaudi2/gaudi2_async_virt_events.h"

#define GAUDI2_LINUX_FW_FILE	"habanalabs/gaudi2/gaudi2-fit.itb"
#define GAUDI2_BOOT_FIT_FILE	"habanalabs/gaudi2/gaudi2-boot-fit.itb"

#define MMU_PAGE_TABLES_INITIAL_SIZE	0x10000000	/* 256MB */

#define GAUDI2_CPU_TIMEOUT_USEC		30000000	/* 30s */

#define GAUDI2_FPGA_CPU_TIMEOUT		100000000	/* 100s */

#define NUMBER_OF_PDMA_QUEUES		2
#define NUMBER_OF_EDMA_QUEUES		8
#define NUMBER_OF_MME_QUEUES		4
#define NUMBER_OF_TPC_QUEUES		25
#define NUMBER_OF_NIC_QUEUES		24
#define NUMBER_OF_ROT_QUEUES		2
#define NUMBER_OF_CPU_QUEUES		1

#define NUMBER_OF_HW_QUEUES		((NUMBER_OF_PDMA_QUEUES + \
					NUMBER_OF_EDMA_QUEUES + \
					NUMBER_OF_MME_QUEUES + \
					NUMBER_OF_TPC_QUEUES + \
					NUMBER_OF_NIC_QUEUES + \
					NUMBER_OF_ROT_QUEUES + \
					NUMBER_OF_CPU_QUEUES) * \
					NUM_OF_PQ_PER_QMAN)

#define NUMBER_OF_QUEUES		(NUMBER_OF_CPU_QUEUES + NUMBER_OF_HW_QUEUES)

#define DCORE_NUM_OF_SOB		\
	(((mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_8191 - \
	mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0) + 4) >> 2)

#define DCORE_NUM_OF_MONITORS		\
	(((mmDCORE0_SYNC_MNGR_OBJS_MON_STATUS_2047 - \
	mmDCORE0_SYNC_MNGR_OBJS_MON_STATUS_0) + 4) >> 2)

#define NUMBER_OF_DEC		((NUM_OF_DEC_PER_DCORE * NUM_OF_DCORES) + NUMBER_OF_PCIE_DEC)

/* Map all arcs dccm + arc schedulers acp blocks */
#define NUM_OF_USER_ACP_BLOCKS		(NUM_OF_SCHEDULER_ARC + 2)
#define NUM_OF_USER_NIC_UMR_BLOCKS	15
#define NUM_OF_EXPOSED_SM_BLOCKS	((NUM_OF_DCORES - 1) * 2)
#define NUM_USER_MAPPED_BLOCKS \
	(NUM_ARC_CPUS + NUM_OF_USER_ACP_BLOCKS + NUMBER_OF_DEC + \
	NUM_OF_EXPOSED_SM_BLOCKS + \
	(NIC_NUMBER_OF_ENGINES * NUM_OF_USER_NIC_UMR_BLOCKS))

/* Within the user mapped array, decoder entries start post all the ARC related
 * entries
 */
#define USR_MAPPED_BLK_DEC_START_IDX \
	(NUM_ARC_CPUS + NUM_OF_USER_ACP_BLOCKS + \
	(NIC_NUMBER_OF_ENGINES * NUM_OF_USER_NIC_UMR_BLOCKS))

#define USR_MAPPED_BLK_SM_START_IDX \
	(NUM_ARC_CPUS + NUM_OF_USER_ACP_BLOCKS + NUMBER_OF_DEC + \
	(NIC_NUMBER_OF_ENGINES * NUM_OF_USER_NIC_UMR_BLOCKS))

#define SM_OBJS_BLOCK_SIZE		(mmDCORE0_SYNC_MNGR_OBJS_SM_SEC_0 - \
					 mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0)

#define GAUDI2_MAX_PENDING_CS		64

#if !IS_MAX_PENDING_CS_VALID(GAUDI2_MAX_PENDING_CS)
#error "GAUDI2_MAX_PENDING_CS must be power of 2 and greater than 1"
#endif

#define CORESIGHT_TIMEOUT_USEC			100000		/* 100 ms */

#define GAUDI2_PREBOOT_REQ_TIMEOUT_USEC		25000000	/* 25s */

#define GAUDI2_BOOT_FIT_REQ_TIMEOUT_USEC	10000000	/* 10s */

#define GAUDI2_NIC_CLK_FREQ			450000000ull	/* 450 MHz */

#define DC_POWER_DEFAULT			60000		/* 60W */

#define GAUDI2_HBM_NUM				6

#define DMA_MAX_TRANSFER_SIZE			U32_MAX

#define GAUDI2_DEFAULT_CARD_NAME		"HL225"

#define QMAN_STREAMS				4
#define PQ_FETCHER_CACHE_SIZE			8
#define NUM_OF_MME_SBTE_PORTS			5
#define NUM_OF_MME_WB_PORTS			2

#define GAUDI2_ENGINE_ID_DCORE_OFFSET \
	(GAUDI2_DCORE1_ENGINE_ID_EDMA_0 - GAUDI2_DCORE0_ENGINE_ID_EDMA_0)

/* DRAM Memory Map */

#define CPU_FW_IMAGE_SIZE			0x10000000	/* 256MB */

/* This define should be used only when working in a debug mode without dram.
 * When working with dram, the driver size will be calculated dynamically.
 */
#define NIC_DEFAULT_DRV_SIZE			0x20000000	/* 512MB */

#define CPU_FW_IMAGE_ADDR			DRAM_PHYS_BASE

#define NIC_NUMBER_OF_PORTS			NIC_NUMBER_OF_ENGINES

#define NUMBER_OF_PCIE_DEC			2
#define PCIE_DEC_SHIFT				8

#define SRAM_USER_BASE_OFFSET			0

/* cluster binning */
#define MAX_FAULTY_HBMS				1
#define GAUDI2_XBAR_EDGE_FULL_MASK		0xF
#define GAUDI2_EDMA_FULL_MASK			0xFF
#define GAUDI2_DRAM_FULL_MASK			0x3F

/* Host virtual address space. */

#define VA_HOST_SPACE_PAGE_START		0xFFF0000000000000ull
#define VA_HOST_SPACE_PAGE_END			0xFFF0800000000000ull /* 140TB */

#define VA_HOST_SPACE_HPAGE_START		0xFFF0800000000000ull
#define VA_HOST_SPACE_HPAGE_END			0xFFF1000000000000ull /* 140TB */

#define VA_HOST_SPACE_USER_MAPPED_CB_START	0xFFF1000000000000ull
#define VA_HOST_SPACE_USER_MAPPED_CB_END	0xFFF1000100000000ull /* 4GB */

/* 140TB */
#define VA_HOST_SPACE_PAGE_SIZE		(VA_HOST_SPACE_PAGE_END - VA_HOST_SPACE_PAGE_START)

/* 140TB */
#define VA_HOST_SPACE_HPAGE_SIZE	(VA_HOST_SPACE_HPAGE_END - VA_HOST_SPACE_HPAGE_START)

#define VA_HOST_SPACE_SIZE		(VA_HOST_SPACE_PAGE_SIZE + VA_HOST_SPACE_HPAGE_SIZE)

#define HOST_SPACE_INTERNAL_CB_SZ		SZ_2M

/*
 * HBM virtual address space
 * Gaudi2 has 6 HBM devices, each supporting 16GB total of 96GB at most.
 * No core separation is supported so we can have one chunk of virtual address
 * space just above the physical ones.
 * The virtual address space starts immediately after the end of the physical
 * address space which is determined at run-time.
 */
#define VA_HBM_SPACE_END		0x1002000000000000ull

#define HW_CAP_PLL			BIT_ULL(0)
#define HW_CAP_DRAM			BIT_ULL(1)
#define HW_CAP_PMMU			BIT_ULL(2)
#define HW_CAP_CPU			BIT_ULL(3)
#define HW_CAP_MSIX			BIT_ULL(4)

#define HW_CAP_CPU_Q			BIT_ULL(5)
#define HW_CAP_CPU_Q_SHIFT		5

#define HW_CAP_CLK_GATE			BIT_ULL(6)
#define HW_CAP_KDMA			BIT_ULL(7)
#define HW_CAP_SRAM_SCRAMBLER		BIT_ULL(8)

#define HW_CAP_DCORE0_DMMU0		BIT_ULL(9)
#define HW_CAP_DCORE0_DMMU1		BIT_ULL(10)
#define HW_CAP_DCORE0_DMMU2		BIT_ULL(11)
#define HW_CAP_DCORE0_DMMU3		BIT_ULL(12)
#define HW_CAP_DCORE1_DMMU0		BIT_ULL(13)
#define HW_CAP_DCORE1_DMMU1		BIT_ULL(14)
#define HW_CAP_DCORE1_DMMU2		BIT_ULL(15)
#define HW_CAP_DCORE1_DMMU3		BIT_ULL(16)
#define HW_CAP_DCORE2_DMMU0		BIT_ULL(17)
#define HW_CAP_DCORE2_DMMU1		BIT_ULL(18)
#define HW_CAP_DCORE2_DMMU2		BIT_ULL(19)
#define HW_CAP_DCORE2_DMMU3		BIT_ULL(20)
#define HW_CAP_DCORE3_DMMU0		BIT_ULL(21)
#define HW_CAP_DCORE3_DMMU1		BIT_ULL(22)
#define HW_CAP_DCORE3_DMMU2		BIT_ULL(23)
#define HW_CAP_DCORE3_DMMU3		BIT_ULL(24)
#define HW_CAP_DMMU_MASK		GENMASK_ULL(24, 9)
#define HW_CAP_DMMU_SHIFT		9
#define HW_CAP_PDMA_MASK		BIT_ULL(26)
#define HW_CAP_EDMA_MASK		GENMASK_ULL(34, 27)
#define HW_CAP_EDMA_SHIFT		27
#define HW_CAP_MME_MASK			GENMASK_ULL(38, 35)
#define HW_CAP_MME_SHIFT		35
#define HW_CAP_ROT_MASK			GENMASK_ULL(40, 39)
#define HW_CAP_ROT_SHIFT		39
#define HW_CAP_HBM_SCRAMBLER_HW_RESET	BIT_ULL(41)
#define HW_CAP_HBM_SCRAMBLER_SW_RESET	BIT_ULL(42)
#define HW_CAP_HBM_SCRAMBLER_MASK	(HW_CAP_HBM_SCRAMBLER_HW_RESET | \
						HW_CAP_HBM_SCRAMBLER_SW_RESET)
#define HW_CAP_HBM_SCRAMBLER_SHIFT	41
#define HW_CAP_RESERVED			BIT(43)
#define HW_CAP_MMU_MASK			(HW_CAP_PMMU | HW_CAP_DMMU_MASK)

/* Range Registers */
#define RR_TYPE_SHORT			0
#define RR_TYPE_LONG			1
#define RR_TYPE_SHORT_PRIV		2
#define RR_TYPE_LONG_PRIV		3
#define NUM_SHORT_LBW_RR		14
#define NUM_LONG_LBW_RR			4
#define NUM_SHORT_HBW_RR		6
#define NUM_LONG_HBW_RR			4

/* RAZWI initiator coordinates- X- 5 bits, Y- 4 bits */
#define RAZWI_INITIATOR_X_SHIFT		0
#define RAZWI_INITIATOR_X_MASK		0x1F
#define RAZWI_INITIATOR_Y_SHIFT		5
#define RAZWI_INITIATOR_Y_MASK		0xF

#define RTR_ID_X_Y(x, y) \
	((((y) & RAZWI_INITIATOR_Y_MASK) << RAZWI_INITIATOR_Y_SHIFT) | \
		(((x) & RAZWI_INITIATOR_X_MASK) << RAZWI_INITIATOR_X_SHIFT))

/* decoders have separate mask */
#define HW_CAP_DEC_SHIFT		0
#define HW_CAP_DEC_MASK			GENMASK_ULL(9, 0)

/* TPCs have separate mask */
#define HW_CAP_TPC_SHIFT		0
#define HW_CAP_TPC_MASK			GENMASK_ULL(24, 0)

/* nics have separate mask */
#define HW_CAP_NIC_SHIFT		0
#define HW_CAP_NIC_MASK			GENMASK_ULL(NIC_NUMBER_OF_ENGINES - 1, 0)

#define GAUDI2_ARC_PCI_MSB_ADDR(addr)	(((addr) & GENMASK_ULL(49, 28)) >> 28)

#define GAUDI2_SOB_INCREMENT_BY_ONE	(FIELD_PREP(DCORE0_SYNC_MNGR_OBJS_SOB_OBJ_VAL_MASK, 1) | \
					FIELD_PREP(DCORE0_SYNC_MNGR_OBJS_SOB_OBJ_INC_MASK, 1))

enum gaudi2_reserved_sob_id {
	GAUDI2_RESERVED_SOB_CS_COMPLETION_FIRST,
	GAUDI2_RESERVED_SOB_CS_COMPLETION_LAST =
			GAUDI2_RESERVED_SOB_CS_COMPLETION_FIRST + GAUDI2_MAX_PENDING_CS - 1,
	GAUDI2_RESERVED_SOB_KDMA_COMPLETION,
	GAUDI2_RESERVED_SOB_DEC_NRM_FIRST,
	GAUDI2_RESERVED_SOB_DEC_NRM_LAST =
			GAUDI2_RESERVED_SOB_DEC_NRM_FIRST + NUMBER_OF_DEC - 1,
	GAUDI2_RESERVED_SOB_DEC_ABNRM_FIRST,
	GAUDI2_RESERVED_SOB_DEC_ABNRM_LAST =
			GAUDI2_RESERVED_SOB_DEC_ABNRM_FIRST + NUMBER_OF_DEC - 1,
	GAUDI2_RESERVED_SOB_NUMBER
};

enum gaudi2_reserved_mon_id {
	GAUDI2_RESERVED_MON_CS_COMPLETION_FIRST,
	GAUDI2_RESERVED_MON_CS_COMPLETION_LAST =
			GAUDI2_RESERVED_MON_CS_COMPLETION_FIRST + GAUDI2_MAX_PENDING_CS - 1,
	GAUDI2_RESERVED_MON_KDMA_COMPLETION,
	GAUDI2_RESERVED_MON_DEC_NRM_FIRST,
	GAUDI2_RESERVED_MON_DEC_NRM_LAST =
			GAUDI2_RESERVED_MON_DEC_NRM_FIRST + 3 * NUMBER_OF_DEC - 1,
	GAUDI2_RESERVED_MON_DEC_ABNRM_FIRST,
	GAUDI2_RESERVED_MON_DEC_ABNRM_LAST =
			GAUDI2_RESERVED_MON_DEC_ABNRM_FIRST + 3 * NUMBER_OF_DEC - 1,
	GAUDI2_RESERVED_MON_NUMBER
};

enum gaudi2_reserved_cq_id {
	GAUDI2_RESERVED_CQ_CS_COMPLETION,
	GAUDI2_RESERVED_CQ_KDMA_COMPLETION,
	GAUDI2_RESERVED_CQ_NUMBER
};

/*
 * Gaudi2 subtitute TPCs Numbering
 * At most- two faulty TPCs are allowed
 * First replacement to a faulty TPC will be TPC24, second- TPC23
 */
enum substitude_tpc {
	FAULTY_TPC_SUBTS_1_TPC_24,
	FAULTY_TPC_SUBTS_2_TPC_23,
	MAX_FAULTY_TPCS
};

enum gaudi2_dma_core_id {
	DMA_CORE_ID_PDMA0, /* Dcore 0 */
	DMA_CORE_ID_PDMA1, /* Dcore 0 */
	DMA_CORE_ID_EDMA0, /* Dcore 0 */
	DMA_CORE_ID_EDMA1, /* Dcore 0 */
	DMA_CORE_ID_EDMA2, /* Dcore 1 */
	DMA_CORE_ID_EDMA3, /* Dcore 1 */
	DMA_CORE_ID_EDMA4, /* Dcore 2 */
	DMA_CORE_ID_EDMA5, /* Dcore 2 */
	DMA_CORE_ID_EDMA6, /* Dcore 3 */
	DMA_CORE_ID_EDMA7, /* Dcore 3 */
	DMA_CORE_ID_KDMA, /* Dcore 0 */
	DMA_CORE_ID_SIZE
};

enum gaudi2_rotator_id {
	ROTATOR_ID_0,
	ROTATOR_ID_1,
	ROTATOR_ID_SIZE,
};

enum gaudi2_mme_id {
	MME_ID_DCORE0,
	MME_ID_DCORE1,
	MME_ID_DCORE2,
	MME_ID_DCORE3,
	MME_ID_SIZE,
};

enum gaudi2_tpc_id {
	TPC_ID_DCORE0_TPC0,
	TPC_ID_DCORE0_TPC1,
	TPC_ID_DCORE0_TPC2,
	TPC_ID_DCORE0_TPC3,
	TPC_ID_DCORE0_TPC4,
	TPC_ID_DCORE0_TPC5,
	TPC_ID_DCORE1_TPC0,
	TPC_ID_DCORE1_TPC1,
	TPC_ID_DCORE1_TPC2,
	TPC_ID_DCORE1_TPC3,
	TPC_ID_DCORE1_TPC4,
	TPC_ID_DCORE1_TPC5,
	TPC_ID_DCORE2_TPC0,
	TPC_ID_DCORE2_TPC1,
	TPC_ID_DCORE2_TPC2,
	TPC_ID_DCORE2_TPC3,
	TPC_ID_DCORE2_TPC4,
	TPC_ID_DCORE2_TPC5,
	TPC_ID_DCORE3_TPC0,
	TPC_ID_DCORE3_TPC1,
	TPC_ID_DCORE3_TPC2,
	TPC_ID_DCORE3_TPC3,
	TPC_ID_DCORE3_TPC4,
	TPC_ID_DCORE3_TPC5,
	/* the PCI TPC is placed last (mapped liked HW) */
	TPC_ID_DCORE0_TPC6,
	TPC_ID_SIZE,
};

enum gaudi2_dec_id {
	DEC_ID_DCORE0_DEC0,
	DEC_ID_DCORE0_DEC1,
	DEC_ID_DCORE1_DEC0,
	DEC_ID_DCORE1_DEC1,
	DEC_ID_DCORE2_DEC0,
	DEC_ID_DCORE2_DEC1,
	DEC_ID_DCORE3_DEC0,
	DEC_ID_DCORE3_DEC1,
	DEC_ID_PCIE_VDEC0,
	DEC_ID_PCIE_VDEC1,
	DEC_ID_SIZE,
};

enum gaudi2_hbm_id {
	HBM_ID0,
	HBM_ID1,
	HBM_ID2,
	HBM_ID3,
	HBM_ID4,
	HBM_ID5,
	HBM_ID_SIZE,
};

/* specific EDMA enumeration */
enum gaudi2_edma_id {
	EDMA_ID_DCORE0_INSTANCE0,
	EDMA_ID_DCORE0_INSTANCE1,
	EDMA_ID_DCORE1_INSTANCE0,
	EDMA_ID_DCORE1_INSTANCE1,
	EDMA_ID_DCORE2_INSTANCE0,
	EDMA_ID_DCORE2_INSTANCE1,
	EDMA_ID_DCORE3_INSTANCE0,
	EDMA_ID_DCORE3_INSTANCE1,
	EDMA_ID_SIZE,
};

/* User interrupt count is aligned with HW CQ count.
 * We have 64 CQ's per dcore, CQ0 in dcore 0 is reserved for legacy mode
 */
#define GAUDI2_NUM_USER_INTERRUPTS 255

enum gaudi2_irq_num {
	GAUDI2_IRQ_NUM_EVENT_QUEUE = GAUDI2_EVENT_QUEUE_MSIX_IDX,
	GAUDI2_IRQ_NUM_DCORE0_DEC0_NRM,
	GAUDI2_IRQ_NUM_DCORE0_DEC0_ABNRM,
	GAUDI2_IRQ_NUM_DCORE0_DEC1_NRM,
	GAUDI2_IRQ_NUM_DCORE0_DEC1_ABNRM,
	GAUDI2_IRQ_NUM_DCORE1_DEC0_NRM,
	GAUDI2_IRQ_NUM_DCORE1_DEC0_ABNRM,
	GAUDI2_IRQ_NUM_DCORE1_DEC1_NRM,
	GAUDI2_IRQ_NUM_DCORE1_DEC1_ABNRM,
	GAUDI2_IRQ_NUM_DCORE2_DEC0_NRM,
	GAUDI2_IRQ_NUM_DCORE2_DEC0_ABNRM,
	GAUDI2_IRQ_NUM_DCORE2_DEC1_NRM,
	GAUDI2_IRQ_NUM_DCORE2_DEC1_ABNRM,
	GAUDI2_IRQ_NUM_DCORE3_DEC0_NRM,
	GAUDI2_IRQ_NUM_DCORE3_DEC0_ABNRM,
	GAUDI2_IRQ_NUM_DCORE3_DEC1_NRM,
	GAUDI2_IRQ_NUM_DCORE3_DEC1_ABNRM,
	GAUDI2_IRQ_NUM_SHARED_DEC0_NRM,
	GAUDI2_IRQ_NUM_SHARED_DEC0_ABNRM,
	GAUDI2_IRQ_NUM_SHARED_DEC1_NRM,
	GAUDI2_IRQ_NUM_SHARED_DEC1_ABNRM,
	GAUDI2_IRQ_NUM_COMPLETION,
	GAUDI2_IRQ_NUM_NIC_PORT_FIRST,
	GAUDI2_IRQ_NUM_NIC_PORT_LAST = (GAUDI2_IRQ_NUM_NIC_PORT_FIRST + NIC_NUMBER_OF_PORTS - 1),
	GAUDI2_IRQ_NUM_RESERVED_FIRST,
	GAUDI2_IRQ_NUM_RESERVED_LAST = (GAUDI2_MSIX_ENTRIES - GAUDI2_NUM_USER_INTERRUPTS - 1),
	GAUDI2_IRQ_NUM_USER_FIRST,
	GAUDI2_IRQ_NUM_USER_LAST = (GAUDI2_IRQ_NUM_USER_FIRST + GAUDI2_NUM_USER_INTERRUPTS - 1),
	GAUDI2_IRQ_NUM_LAST = (GAUDI2_MSIX_ENTRIES - 1)
};

static_assert(GAUDI2_IRQ_NUM_USER_FIRST > GAUDI2_IRQ_NUM_SHARED_DEC1_ABNRM);

/**
 * struct dup_block_ctx - context to initialize unit instances across multiple
 *                        blocks where block can be either a dcore of duplicated
 *                        common module. this code relies on constant offsets
 *                        of blocks and unit instances in a block.
 * @instance_cfg_fn: instance specific configuration function.
 * @data: private configuration data.
 * @base: base address of the first instance in the first block.
 * @block_off: subsequent blocks address spacing.
 * @instance_off: subsequent block's instances address spacing.
 * @enabled_mask: mask of enabled instances (1- enabled, 0- disabled).
 * @blocks: number of blocks.
 * @instances: unit instances per block.
 */
struct dup_block_ctx {
	void (*instance_cfg_fn)(struct hl_device *hdev, u64 base, void *data);
	void *data;
	u64 base;
	u64 block_off;
	u64 instance_off;
	u64 enabled_mask;
	unsigned int blocks;
	unsigned int instances;
};

/**
 * struct gaudi2_device - ASIC specific manage structure.
 * @cpucp_info_get: get information on device from CPU-CP
 * @mapped_blocks: array that holds the base address and size of all blocks
 *                 the user can map.
 * @lfsr_rand_seeds: array of MME ACC random seeds to set.
 * @hw_queues_lock: protects the H/W queues from concurrent access.
 * @kdma_lock: protects the KDMA engine from concurrent access.
 * @scratchpad_kernel_address: general purpose PAGE_SIZE contiguous memory,
 *                             this memory region should be write-only.
 *                             currently used for HBW QMAN writes which is
 *                             redundant.
 * @scratchpad_bus_address: scratchpad bus address
 * @virt_msix_db_cpu_addr: host memory page for the virtual MSI-X doorbell.
 * @virt_msix_db_dma_addr: bus address of the page for the virtual MSI-X doorbell.
 * @dram_bar_cur_addr: current address of DRAM PCI bar.
 * @hw_cap_initialized: This field contains a bit per H/W engine. When that
 *                      engine is initialized, that bit is set by the driver to
 *                      signal we can use this engine in later code paths.
 *                      Each bit is cleared upon reset of its corresponding H/W
 *                      engine.
 * @active_hw_arc: This field contains a bit per ARC of an H/W engine with
 *                 exception of TPC and NIC engines. Once an engine arc is
 *                 initialized, its respective bit is set. Driver can uniquely
 *                 identify each initialized ARC and use this information in
 *                 later code paths. Each respective bit is cleared upon reset
 *                 of its corresponding ARC of the H/W engine.
 * @dec_hw_cap_initialized: This field contains a bit per decoder H/W engine.
 *                      When that engine is initialized, that bit is set by
 *                      the driver to signal we can use this engine in later
 *                      code paths.
 *                      Each bit is cleared upon reset of its corresponding H/W
 *                      engine.
 * @tpc_hw_cap_initialized: This field contains a bit per TPC H/W engine.
 *                      When that engine is initialized, that bit is set by
 *                      the driver to signal we can use this engine in later
 *                      code paths.
 *                      Each bit is cleared upon reset of its corresponding H/W
 *                      engine.
 * @active_tpc_arc: This field contains a bit per ARC of the TPC engines.
 *                  Once an engine arc is initialized, its respective bit is
 *                  set. Each respective bit is cleared upon reset of its
 *                  corresponding ARC of the TPC engine.
 * @nic_hw_cap_initialized: This field contains a bit per nic H/W engine.
 * @active_nic_arc: This field contains a bit per ARC of the NIC engines.
 *                  Once an engine arc is initialized, its respective bit is
 *                  set. Each respective bit is cleared upon reset of its
 *                  corresponding ARC of the NIC engine.
 * @hw_events: array that holds all H/W events that are defined valid.
 * @events_stat: array that holds histogram of all received events.
 * @events_stat_aggregate: same as events_stat but doesn't get cleared on reset.
 * @num_of_valid_hw_events: used to hold the number of valid H/W events.
 * @nic_ports: array that holds all NIC ports manage structures.
 * @nic_macros: array that holds all NIC macro manage structures.
 * @core_info: core info to be used by the Ethernet driver.
 * @aux_ops: functions for core <-> aux drivers communication.
 * @flush_db_fifo: flag to force flush DB FIFO after a write.
 * @hbm_cfg: HBM subsystem settings
 * @hw_queues_lock_mutex: used by simulator instead of hw_queues_lock.
 * @kdma_lock_mutex: used by simulator instead of kdma_lock.
 * @use_deprecated_event_mappings: use old event mappings which are about to be
 *                                 deprecated
 */
struct gaudi2_device {
	int (*cpucp_info_get)(struct hl_device *hdev);

	struct user_mapped_block	mapped_blocks[NUM_USER_MAPPED_BLOCKS];
	int				lfsr_rand_seeds[MME_NUM_OF_LFSR_SEEDS];

	spinlock_t			hw_queues_lock;
	spinlock_t			kdma_lock;

	void				*scratchpad_kernel_address;
	dma_addr_t			scratchpad_bus_address;

	void				*virt_msix_db_cpu_addr;
	dma_addr_t			virt_msix_db_dma_addr;

	u64				dram_bar_cur_addr;
	u64				hw_cap_initialized;
	u64				active_hw_arc;
	u64				dec_hw_cap_initialized;
	u64				tpc_hw_cap_initialized;
	u64				active_tpc_arc;
	u64				nic_hw_cap_initialized;
	u64				active_nic_arc;
	u32				hw_events[GAUDI2_EVENT_SIZE];
	u32				events_stat[GAUDI2_EVENT_SIZE];
	u32				events_stat_aggregate[GAUDI2_EVENT_SIZE];
	u32				num_of_valid_hw_events;
};

extern const u32 gaudi2_dma_core_blocks_bases[DMA_CORE_ID_SIZE];
extern const u32 gaudi2_qm_blocks_bases[GAUDI2_QUEUE_ID_SIZE];
extern const u32 gaudi2_mme_acc_blocks_bases[MME_ID_SIZE];
extern const u32 gaudi2_mme_ctrl_lo_blocks_bases[MME_ID_SIZE];
extern const u32 edma_stream_base[NUM_OF_EDMA_PER_DCORE * NUM_OF_DCORES];
extern const u32 gaudi2_rot_blocks_bases[ROTATOR_ID_SIZE];

void gaudi2_iterate_tpcs(struct hl_device *hdev, struct iterate_module_ctx *ctx);
int gaudi2_coresight_init(struct hl_device *hdev);
int gaudi2_debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, void *data);
void gaudi2_halt_coresight(struct hl_device *hdev, struct hl_ctx *ctx);
void gaudi2_init_blocks(struct hl_device *hdev, struct dup_block_ctx *cfg_ctx);
bool gaudi2_is_hmmu_enabled(struct hl_device *hdev, int dcore_id, int hmmu_id);
void gaudi2_write_rr_to_all_lbw_rtrs(struct hl_device *hdev, u8 rr_type, u32 rr_index, u64 min_val,
					u64 max_val);
void gaudi2_pb_print_security_errors(struct hl_device *hdev, u32 block_addr, u32 cause,
					u32 offended_addr);
int gaudi2_init_security(struct hl_device *hdev);
void gaudi2_ack_protection_bits_errors(struct hl_device *hdev);

#endif /* GAUDI2P_H_ */
