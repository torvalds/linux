/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2019-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDIP_H_
#define GAUDIP_H_

#include <uapi/misc/habanalabs.h>
#include "habanalabs.h"
#include "include/hl_boot_if.h"
#include "include/gaudi/gaudi_packets.h"
#include "include/gaudi/gaudi.h"
#include "include/gaudi/gaudi_async_events.h"

#define NUMBER_OF_EXT_HW_QUEUES		12
#define NUMBER_OF_CMPLT_QUEUES		NUMBER_OF_EXT_HW_QUEUES
#define NUMBER_OF_CPU_HW_QUEUES		1
#define NUMBER_OF_INT_HW_QUEUES		100
#define NUMBER_OF_HW_QUEUES		(NUMBER_OF_EXT_HW_QUEUES + \
					NUMBER_OF_CPU_HW_QUEUES + \
					NUMBER_OF_INT_HW_QUEUES)

/*
 * Number of MSI interrupts IDS:
 * Each completion queue has 1 ID
 * The event queue has 1 ID
 */
#define NUMBER_OF_INTERRUPTS		(NUMBER_OF_CMPLT_QUEUES + \
						NUMBER_OF_CPU_HW_QUEUES)

#if (NUMBER_OF_INTERRUPTS > GAUDI_MSI_ENTRIES)
#error "Number of MSI interrupts must be smaller or equal to GAUDI_MSI_ENTRIES"
#endif

#define QMAN_FENCE_TIMEOUT_USEC		10000		/* 10 ms */

#define CORESIGHT_TIMEOUT_USEC		100000		/* 100 ms */

#define GAUDI_MAX_CLK_FREQ		2200000000ull	/* 2200 MHz */

#define MAX_POWER_DEFAULT		200000		/* 200W */

#define GAUDI_CPU_TIMEOUT_USEC		15000000	/* 15s */

#define TPC_ENABLED_MASK		0xFF

#define GAUDI_HBM_SIZE_32GB		0x800000000ull
#define GAUDI_HBM_DEVICES		4
#define GAUDI_HBM_CHANNELS		8
#define GAUDI_HBM_CFG_BASE		(mmHBM0_BASE - CFG_BASE)
#define GAUDI_HBM_CFG_OFFSET		(mmHBM1_BASE - mmHBM0_BASE)

#define DMA_MAX_TRANSFER_SIZE		U32_MAX

#define GAUDI_DEFAULT_CARD_NAME		"HL2000"

#define PCI_DMA_NUMBER_OF_CHNLS		3
#define HBM_DMA_NUMBER_OF_CHNLS		5
#define DMA_NUMBER_OF_CHNLS		(PCI_DMA_NUMBER_OF_CHNLS + \
						HBM_DMA_NUMBER_OF_CHNLS)

#define MME_NUMBER_OF_SLAVE_ENGINES	2
#define MME_NUMBER_OF_ENGINES		(MME_NUMBER_OF_MASTER_ENGINES + \
					MME_NUMBER_OF_SLAVE_ENGINES)
#define MME_NUMBER_OF_QMANS		(MME_NUMBER_OF_MASTER_ENGINES * \
					QMAN_STREAMS)

#define QMAN_STREAMS		4

#define DMA_QMAN_OFFSET		(mmDMA1_QM_BASE - mmDMA0_QM_BASE)
#define TPC_QMAN_OFFSET		(mmTPC1_QM_BASE - mmTPC0_QM_BASE)
#define MME_QMAN_OFFSET		(mmMME1_QM_BASE - mmMME0_QM_BASE)
#define NIC_MACRO_QMAN_OFFSET	(mmNIC1_QM0_BASE - mmNIC0_QM0_BASE)

#define TPC_CFG_OFFSET		(mmTPC1_CFG_BASE - mmTPC0_CFG_BASE)

#define DMA_CORE_OFFSET		(mmDMA1_CORE_BASE - mmDMA0_CORE_BASE)

#define SIF_RTR_CTRL_OFFSET	(mmSIF_RTR_CTRL_1_BASE - mmSIF_RTR_CTRL_0_BASE)

#define NIF_RTR_CTRL_OFFSET	(mmNIF_RTR_CTRL_1_BASE - mmNIF_RTR_CTRL_0_BASE)

#define MME_ACC_OFFSET		(mmMME1_ACC_BASE - mmMME0_ACC_BASE)
#define SRAM_BANK_OFFSET	(mmSRAM_Y0_X1_RTR_BASE - mmSRAM_Y0_X0_RTR_BASE)

#define NUM_OF_SOB_IN_BLOCK		\
	(((mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_2047 - \
	mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0) + 4) >> 2)

#define NUM_OF_MONITORS_IN_BLOCK	\
	(((mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_STATUS_511 - \
	mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_STATUS_0) + 4) >> 2)


/* DRAM Memory Map */

#define CPU_FW_IMAGE_SIZE	0x10000000	/* 256MB */
#define MMU_PAGE_TABLES_SIZE	0x0BF00000	/* 191MB */
#define MMU_CACHE_MNG_SIZE	0x00100000	/* 1MB */
#define RESERVED		0x04000000	/* 64MB */

#define CPU_FW_IMAGE_ADDR	DRAM_PHYS_BASE
#define MMU_PAGE_TABLES_ADDR	(CPU_FW_IMAGE_ADDR + CPU_FW_IMAGE_SIZE)
#define MMU_CACHE_MNG_ADDR	(MMU_PAGE_TABLES_ADDR + MMU_PAGE_TABLES_SIZE)

#define DRAM_DRIVER_END_ADDR	(MMU_CACHE_MNG_ADDR + MMU_CACHE_MNG_SIZE +\
								RESERVED)

#define DRAM_BASE_ADDR_USER	0x20000000

#if (DRAM_DRIVER_END_ADDR > DRAM_BASE_ADDR_USER)
#error "Driver must reserve no more than 512MB"
#endif

/* Internal QMANs PQ sizes */

#define MME_QMAN_LENGTH			64
#define MME_QMAN_SIZE_IN_BYTES		(MME_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE)

#define HBM_DMA_QMAN_LENGTH		64
#define HBM_DMA_QMAN_SIZE_IN_BYTES	\
				(HBM_DMA_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE)

#define TPC_QMAN_LENGTH			64
#define TPC_QMAN_SIZE_IN_BYTES		(TPC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE)

#define SRAM_USER_BASE_OFFSET  GAUDI_DRIVER_SRAM_RESERVED_SIZE_FROM_START

/* Virtual address space */
#define VA_HOST_SPACE_START	0x1000000000000ull	/* 256TB */
#define VA_HOST_SPACE_END	0x3FF8000000000ull	/* 1PB - 1TB */
#define VA_HOST_SPACE_SIZE	(VA_HOST_SPACE_END - \
					VA_HOST_SPACE_START) /* 767TB */

#define HW_CAP_PLL		0x00000001
#define HW_CAP_HBM		0x00000002
#define HW_CAP_MMU		0x00000004
#define HW_CAP_MME		0x00000008
#define HW_CAP_CPU		0x00000010
#define HW_CAP_PCI_DMA		0x00000020
#define HW_CAP_MSI		0x00000040
#define HW_CAP_CPU_Q		0x00000080
#define HW_CAP_HBM_DMA		0x00000100
#define HW_CAP_CLK_GATE		0x00000200
#define HW_CAP_SRAM_SCRAMBLER	0x00000400
#define HW_CAP_HBM_SCRAMBLER	0x00000800

#define HW_CAP_TPC0		0x01000000
#define HW_CAP_TPC1		0x02000000
#define HW_CAP_TPC2		0x04000000
#define HW_CAP_TPC3		0x08000000
#define HW_CAP_TPC4		0x10000000
#define HW_CAP_TPC5		0x20000000
#define HW_CAP_TPC6		0x40000000
#define HW_CAP_TPC7		0x80000000
#define HW_CAP_TPC_MASK		0xFF000000
#define HW_CAP_TPC_SHIFT	24

#define GAUDI_CPU_PCI_MSB_ADDR(addr)	(((addr) & GENMASK_ULL(49, 39)) >> 39)
#define GAUDI_PCI_TO_CPU_ADDR(addr)			\
	do {						\
		(addr) &= ~GENMASK_ULL(49, 39);		\
		(addr) |= BIT_ULL(39);			\
	} while (0)
#define GAUDI_CPU_TO_PCI_ADDR(addr, extension)		\
	do {						\
		(addr) &= ~GENMASK_ULL(49, 39);		\
		(addr) |= (u64) (extension) << 39;	\
	} while (0)

enum gaudi_dma_channels {
	GAUDI_PCI_DMA_1,
	GAUDI_PCI_DMA_2,
	GAUDI_PCI_DMA_3,
	GAUDI_HBM_DMA_1,
	GAUDI_HBM_DMA_2,
	GAUDI_HBM_DMA_3,
	GAUDI_HBM_DMA_4,
	GAUDI_HBM_DMA_5,
	GAUDI_DMA_MAX
};

enum gaudi_tpc_mask {
	GAUDI_TPC_MASK_TPC0 = 0x01,
	GAUDI_TPC_MASK_TPC1 = 0x02,
	GAUDI_TPC_MASK_TPC2 = 0x04,
	GAUDI_TPC_MASK_TPC3 = 0x08,
	GAUDI_TPC_MASK_TPC4 = 0x10,
	GAUDI_TPC_MASK_TPC5 = 0x20,
	GAUDI_TPC_MASK_TPC6 = 0x40,
	GAUDI_TPC_MASK_TPC7 = 0x80,
	GAUDI_TPC_MASK_ALL = 0xFF
};

/**
 * struct gaudi_internal_qman_info - Internal QMAN information.
 * @pq_kernel_addr: Kernel address of the PQ memory area in the host.
 * @pq_dma_addr: DMA address of the PQ memory area in the host.
 * @pq_size: Size of allocated host memory for PQ.
 */
struct gaudi_internal_qman_info {
	void		*pq_kernel_addr;
	dma_addr_t	pq_dma_addr;
	size_t		pq_size;
};

/**
 * struct gaudi_device - ASIC specific manage structure.
 * @armcp_info_get: get information on device from ArmCP
 * @hw_queues_lock: protects the H/W queues from concurrent access.
 * @clk_gate_mutex: protects code areas that require clock gating to be disabled
 *                  temporarily
 * @internal_qmans: Internal QMANs information. The array size is larger than
 *                  the actual number of internal queues because they are not in
 *                  consecutive order.
 * @hbm_bar_cur_addr: current address of HBM PCI bar.
 * @max_freq_value: current max clk frequency.
 * @events: array that holds all event id's
 * @events_stat: array that holds histogram of all received events.
 * @events_stat_aggregate: same as events_stat but doesn't get cleared on reset
 * @hw_cap_initialized: This field contains a bit per H/W engine. When that
 *                      engine is initialized, that bit is set by the driver to
 *                      signal we can use this engine in later code paths.
 *                      Each bit is cleared upon reset of its corresponding H/W
 *                      engine.
 * @multi_msi_mode: whether we are working in multi MSI single MSI mode.
 *                  Multi MSI is possible only with IOMMU enabled.
 * @ext_queue_idx: helper index for external queues initialization.
 */
struct gaudi_device {
	int (*armcp_info_get)(struct hl_device *hdev);

	/* TODO: remove hw_queues_lock after moving to scheduler code */
	spinlock_t			hw_queues_lock;
	struct mutex			clk_gate_mutex;

	struct gaudi_internal_qman_info	internal_qmans[GAUDI_QUEUE_ID_SIZE];

	u64				hbm_bar_cur_addr;
	u64				max_freq_value;

	u32				events[GAUDI_EVENT_SIZE];
	u32				events_stat[GAUDI_EVENT_SIZE];
	u32				events_stat_aggregate[GAUDI_EVENT_SIZE];
	u32				hw_cap_initialized;
	u8				multi_msi_mode;
	u8				ext_queue_idx;
};

void gaudi_init_security(struct hl_device *hdev);
void gaudi_add_device_attr(struct hl_device *hdev,
			struct attribute_group *dev_attr_grp);
void gaudi_set_pll_profile(struct hl_device *hdev, enum hl_pll_frequency freq);
int gaudi_debug_coresight(struct hl_device *hdev, void *data);
void gaudi_halt_coresight(struct hl_device *hdev);
int gaudi_get_clk_rate(struct hl_device *hdev, u32 *cur_clk, u32 *max_clk);

#endif /* GAUDIP_H_ */
