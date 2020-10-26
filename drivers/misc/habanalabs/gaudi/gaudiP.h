/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2019-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDIP_H_
#define GAUDIP_H_

#include <uapi/misc/habanalabs.h>
#include "../common/habanalabs.h"
#include "../include/common/hl_boot_if.h"
#include "../include/gaudi/gaudi_packets.h"
#include "../include/gaudi/gaudi.h"
#include "../include/gaudi/gaudi_async_events.h"

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

#define CORESIGHT_TIMEOUT_USEC		100000		/* 100 ms */

#define GAUDI_MAX_CLK_FREQ		2200000000ull	/* 2200 MHz */

#define MAX_POWER_DEFAULT_PCI		200000		/* 200W */
#define MAX_POWER_DEFAULT_PMC		350000		/* 350W */

#define GAUDI_CPU_TIMEOUT_USEC		30000000	/* 30s */

#define TPC_ENABLED_MASK		0xFF

#define GAUDI_HBM_SIZE_32GB		0x800000000ull
#define GAUDI_HBM_DEVICES		4
#define GAUDI_HBM_CHANNELS		8
#define GAUDI_HBM_CFG_BASE		(mmHBM0_BASE - CFG_BASE)
#define GAUDI_HBM_CFG_OFFSET		(mmHBM1_BASE - mmHBM0_BASE)

#define DMA_MAX_TRANSFER_SIZE		U32_MAX

#define GAUDI_DEFAULT_CARD_NAME		"HL2000"

#define GAUDI_MAX_PENDING_CS		1024

#if !IS_MAX_PENDING_CS_VALID(GAUDI_MAX_PENDING_CS)
#error "GAUDI_MAX_PENDING_CS must be power of 2 and greater than 1"
#endif

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

#define QMAN_LDMA_SRC_OFFSET	(mmDMA0_CORE_SRC_BASE_LO - mmDMA0_CORE_CFG_0)
#define QMAN_LDMA_DST_OFFSET	(mmDMA0_CORE_DST_BASE_LO - mmDMA0_CORE_CFG_0)
#define QMAN_LDMA_SIZE_OFFSET	(mmDMA0_CORE_DST_TSIZE_0 - mmDMA0_CORE_CFG_0)

#define QMAN_CPDMA_SRC_OFFSET	(mmDMA0_QM_CQ_PTR_LO_4 - mmDMA0_CORE_CFG_0)
#define QMAN_CPDMA_DST_OFFSET	(mmDMA0_CORE_DST_BASE_LO - mmDMA0_CORE_CFG_0)
#define QMAN_CPDMA_SIZE_OFFSET	(mmDMA0_QM_CQ_TSIZE_4 - mmDMA0_CORE_CFG_0)

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

#define MME_QMAN_LENGTH			1024
#define MME_QMAN_SIZE_IN_BYTES		(MME_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE)

#define HBM_DMA_QMAN_LENGTH		1024
#define HBM_DMA_QMAN_SIZE_IN_BYTES	\
				(HBM_DMA_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE)

#define TPC_QMAN_LENGTH			1024
#define TPC_QMAN_SIZE_IN_BYTES		(TPC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE)

#define SRAM_USER_BASE_OFFSET  GAUDI_DRIVER_SRAM_RESERVED_SIZE_FROM_START

/* Virtual address space */
#define VA_HOST_SPACE_START	0x1000000000000ull	/* 256TB */
#define VA_HOST_SPACE_END	0x3FF8000000000ull	/* 1PB - 1TB */
#define VA_HOST_SPACE_SIZE	(VA_HOST_SPACE_END - \
					VA_HOST_SPACE_START) /* 767TB */

#define HW_CAP_PLL		BIT(0)
#define HW_CAP_HBM		BIT(1)
#define HW_CAP_MMU		BIT(2)
#define HW_CAP_MME		BIT(3)
#define HW_CAP_CPU		BIT(4)
#define HW_CAP_PCI_DMA		BIT(5)
#define HW_CAP_MSI		BIT(6)
#define HW_CAP_CPU_Q		BIT(7)
#define HW_CAP_HBM_DMA		BIT(8)
#define HW_CAP_CLK_GATE		BIT(9)
#define HW_CAP_SRAM_SCRAMBLER	BIT(10)
#define HW_CAP_HBM_SCRAMBLER	BIT(11)

#define HW_CAP_TPC0		BIT(24)
#define HW_CAP_TPC1		BIT(25)
#define HW_CAP_TPC2		BIT(26)
#define HW_CAP_TPC3		BIT(27)
#define HW_CAP_TPC4		BIT(28)
#define HW_CAP_TPC5		BIT(29)
#define HW_CAP_TPC6		BIT(30)
#define HW_CAP_TPC7		BIT(31)
#define HW_CAP_TPC_MASK		GENMASK(31, 24)
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
 * @cpucp_info_get: get information on device from CPU-CP
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
 * @mmu_cache_inv_pi: PI for MMU cache invalidation flow. The H/W expects an
 *                    8-bit value so use u8.
 */
struct gaudi_device {
	int (*cpucp_info_get)(struct hl_device *hdev);

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
	u8				mmu_cache_inv_pi;
};

void gaudi_init_security(struct hl_device *hdev);
void gaudi_add_device_attr(struct hl_device *hdev,
			struct attribute_group *dev_attr_grp);
void gaudi_set_pll_profile(struct hl_device *hdev, enum hl_pll_frequency freq);
int gaudi_debug_coresight(struct hl_device *hdev, void *data);
void gaudi_halt_coresight(struct hl_device *hdev);
int gaudi_get_clk_rate(struct hl_device *hdev, u32 *cur_clk, u32 *max_clk);

#endif /* GAUDIP_H_ */
