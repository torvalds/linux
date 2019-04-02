/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GOYAP_H_
#define GOYAP_H_

#include <uapi/misc/habanalabs.h>
#include "habanalabs.h"
#include "include/hl_boot_if.h"
#include "include/goya/goya_packets.h"
#include "include/goya/goya.h"
#include "include/goya/goya_async_events.h"
#include "include/goya/goya_fw_if.h"

#define NUMBER_OF_CMPLT_QUEUES		5
#define NUMBER_OF_EXT_HW_QUEUES		5
#define NUMBER_OF_CPU_HW_QUEUES		1
#define NUMBER_OF_INT_HW_QUEUES		9
#define NUMBER_OF_HW_QUEUES		(NUMBER_OF_EXT_HW_QUEUES + \
					NUMBER_OF_CPU_HW_QUEUES + \
					NUMBER_OF_INT_HW_QUEUES)

/*
 * Number of MSIX interrupts IDS:
 * Each completion queue has 1 ID
 * The event queue has 1 ID
 */
#define NUMBER_OF_INTERRUPTS		(NUMBER_OF_CMPLT_QUEUES + 1)

#if (NUMBER_OF_HW_QUEUES >= HL_MAX_QUEUES)
#error "Number of H/W queues must be smaller than HL_MAX_QUEUES"
#endif

#if (NUMBER_OF_INTERRUPTS > GOYA_MSIX_ENTRIES)
#error "Number of MSIX interrupts must be smaller or equal to GOYA_MSIX_ENTRIES"
#endif

#define QMAN_FENCE_TIMEOUT_USEC		10000		/* 10 ms */

#define QMAN_STOP_TIMEOUT_USEC		100000		/* 100 ms */

#define CORESIGHT_TIMEOUT_USEC		100000		/* 100 ms */

#define GOYA_CPU_TIMEOUT_USEC		10000000	/* 10s */

#define TPC_ENABLED_MASK		0xFF

#define PLL_HIGH_DEFAULT		1575000000	/* 1.575 GHz */

#define MAX_POWER_DEFAULT		200000		/* 200W */

#define DRAM_PHYS_DEFAULT_SIZE		0x100000000ull	/* 4GB */

/* DRAM Memory Map */

#define CPU_FW_IMAGE_SIZE		0x10000000	/* 256MB */
#define MMU_PAGE_TABLES_SIZE		0x0FC00000	/* 252MB */
#define MMU_DRAM_DEFAULT_PAGE_SIZE	0x00200000	/* 2MB */
#define MMU_CACHE_MNG_SIZE		0x00001000	/* 4KB */

#define CPU_FW_IMAGE_ADDR		DRAM_PHYS_BASE
#define MMU_PAGE_TABLES_ADDR		(CPU_FW_IMAGE_ADDR + CPU_FW_IMAGE_SIZE)
#define MMU_DRAM_DEFAULT_PAGE_ADDR	(MMU_PAGE_TABLES_ADDR + \
						MMU_PAGE_TABLES_SIZE)
#define MMU_CACHE_MNG_ADDR		(MMU_DRAM_DEFAULT_PAGE_ADDR + \
					MMU_DRAM_DEFAULT_PAGE_SIZE)
#define DRAM_KMD_END_ADDR		(MMU_CACHE_MNG_ADDR + \
						MMU_CACHE_MNG_SIZE)

#define DRAM_BASE_ADDR_USER		0x20000000

#if (DRAM_KMD_END_ADDR > DRAM_BASE_ADDR_USER)
#error "KMD must reserve no more than 512MB"
#endif

/*
 * SRAM Memory Map for KMD
 *
 * KMD occupies KMD_SRAM_SIZE bytes from the start of SRAM. It is used for
 * MME/TPC QMANs
 *
 */

#define MME_QMAN_BASE_OFFSET	0x000000	/* Must be 0 */
#define MME_QMAN_LENGTH		64
#define TPC_QMAN_LENGTH		64

#define TPC0_QMAN_BASE_OFFSET	(MME_QMAN_BASE_OFFSET + \
				(MME_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE))
#define TPC1_QMAN_BASE_OFFSET	(TPC0_QMAN_BASE_OFFSET + \
				(TPC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE))
#define TPC2_QMAN_BASE_OFFSET	(TPC1_QMAN_BASE_OFFSET + \
				(TPC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE))
#define TPC3_QMAN_BASE_OFFSET	(TPC2_QMAN_BASE_OFFSET + \
				(TPC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE))
#define TPC4_QMAN_BASE_OFFSET	(TPC3_QMAN_BASE_OFFSET + \
				(TPC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE))
#define TPC5_QMAN_BASE_OFFSET	(TPC4_QMAN_BASE_OFFSET + \
				(TPC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE))
#define TPC6_QMAN_BASE_OFFSET	(TPC5_QMAN_BASE_OFFSET + \
				(TPC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE))
#define TPC7_QMAN_BASE_OFFSET	(TPC6_QMAN_BASE_OFFSET + \
				(TPC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE))

#define SRAM_KMD_RES_OFFSET	(TPC7_QMAN_BASE_OFFSET + \
				(TPC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE))

#if (SRAM_KMD_RES_OFFSET >= GOYA_KMD_SRAM_RESERVED_SIZE_FROM_START)
#error "MME/TPC QMANs SRAM space exceeds limit"
#endif

#define SRAM_USER_BASE_OFFSET	GOYA_KMD_SRAM_RESERVED_SIZE_FROM_START

/* Virtual address space */
#define VA_HOST_SPACE_START	0x1000000000000ull	/* 256TB */
#define VA_HOST_SPACE_END	0x3FF8000000000ull	/* 1PB - 1TB */
#define VA_HOST_SPACE_SIZE	(VA_HOST_SPACE_END - \
					VA_HOST_SPACE_START) /* 767TB */

#define VA_DDR_SPACE_START	0x800000000ull		/* 32GB */
#define VA_DDR_SPACE_END	0x2000000000ull		/* 128GB */
#define VA_DDR_SPACE_SIZE	(VA_DDR_SPACE_END - \
					VA_DDR_SPACE_START)	/* 128GB */

#define DMA_MAX_TRANSFER_SIZE	U32_MAX

#define HW_CAP_PLL		0x00000001
#define HW_CAP_DDR_0		0x00000002
#define HW_CAP_DDR_1		0x00000004
#define HW_CAP_MME		0x00000008
#define HW_CAP_CPU		0x00000010
#define HW_CAP_DMA		0x00000020
#define HW_CAP_MSIX		0x00000040
#define HW_CAP_CPU_Q		0x00000080
#define HW_CAP_MMU		0x00000100
#define HW_CAP_TPC_MBIST	0x00000200
#define HW_CAP_GOLDEN		0x00000400
#define HW_CAP_TPC		0x00000800

enum goya_fw_component {
	FW_COMP_UBOOT,
	FW_COMP_PREBOOT
};

struct goya_device {
	void (*mmu_prepare_reg)(struct hl_device *hdev, u64 reg, u32 asid);
	void (*qman0_set_security)(struct hl_device *hdev, bool secure);

	/* TODO: remove hw_queues_lock after moving to scheduler code */
	spinlock_t	hw_queues_lock;

	u64		mme_clk;
	u64		tpc_clk;
	u64		ic_clk;

	u64		ddr_bar_cur_addr;
	u32		events_stat[GOYA_ASYNC_EVENT_ID_SIZE];
	u32		hw_cap_initialized;
};

int goya_debugfs_i2c_read(struct hl_device *hdev, u8 i2c_bus,
			u8 i2c_addr, u8 i2c_reg, u32 *val);
int goya_debugfs_i2c_write(struct hl_device *hdev, u8 i2c_bus,
			u8 i2c_addr, u8 i2c_reg, u32 val);
int goya_test_cpu_queue(struct hl_device *hdev);
int goya_send_cpu_message(struct hl_device *hdev, u32 *msg, u16 len,
				u32 timeout, long *result);
long goya_get_temperature(struct hl_device *hdev, int sensor_index, u32 attr);
long goya_get_voltage(struct hl_device *hdev, int sensor_index, u32 attr);
long goya_get_current(struct hl_device *hdev, int sensor_index, u32 attr);
long goya_get_fan_speed(struct hl_device *hdev, int sensor_index, u32 attr);
long goya_get_pwm_info(struct hl_device *hdev, int sensor_index, u32 attr);
void goya_set_pwm_info(struct hl_device *hdev, int sensor_index, u32 attr,
			long value);
void goya_debugfs_led_set(struct hl_device *hdev, u8 led, u8 state);
void goya_set_pll_profile(struct hl_device *hdev, enum hl_pll_frequency freq);
void goya_add_device_attr(struct hl_device *hdev,
			struct attribute_group *dev_attr_grp);
int goya_armcp_info_get(struct hl_device *hdev);
void goya_init_security(struct hl_device *hdev);
int goya_debug_coresight(struct hl_device *hdev, void *data);
u64 goya_get_max_power(struct hl_device *hdev);
void goya_set_max_power(struct hl_device *hdev, u64 value);
int goya_test_queues(struct hl_device *hdev);
void goya_mmu_prepare(struct hl_device *hdev, u32 asid);
int goya_mmu_clear_pgt_range(struct hl_device *hdev);
int goya_mmu_set_dram_default_page(struct hl_device *hdev);

void goya_late_fini(struct hl_device *hdev);
int goya_suspend(struct hl_device *hdev);
int goya_resume(struct hl_device *hdev);
void goya_flush_pq_write(struct hl_device *hdev, u64 *pq, u64 exp_val);
void goya_handle_eqe(struct hl_device *hdev, struct hl_eq_entry *eq_entry);
void *goya_get_events_stat(struct hl_device *hdev, u32 *size);
void goya_add_end_of_cb_packets(u64 kernel_address, u32 len, u64 cq_addr,
				u32 cq_val, u32 msix_vec);
int goya_cs_parser(struct hl_device *hdev, struct hl_cs_parser *parser);
void *goya_get_int_queue_base(struct hl_device *hdev, u32 queue_id,
		dma_addr_t *dma_handle,	u16 *queue_len);
u32 goya_get_dma_desc_list_size(struct hl_device *hdev, struct sg_table *sgt);
int goya_test_queue(struct hl_device *hdev, u32 hw_queue_id);
int goya_send_heartbeat(struct hl_device *hdev);
void *goya_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle);
void goya_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size,
					void *vaddr);

#endif /* GOYAP_H_ */
