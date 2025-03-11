/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#ifndef __IVPU_HW_H__
#define __IVPU_HW_H__

#include <linux/kfifo.h>

#include "ivpu_drv.h"
#include "ivpu_hw_btrs.h"
#include "ivpu_hw_ip.h"

#define IVPU_HW_IRQ_FIFO_LENGTH 1024

#define IVPU_HW_IRQ_SRC_IPC 1
#define IVPU_HW_IRQ_SRC_MMU_EVTQ 2
#define IVPU_HW_IRQ_SRC_DCT 3

struct ivpu_addr_range {
	resource_size_t start;
	resource_size_t end;
};

struct ivpu_hw_info {
	struct {
		bool (*btrs_irq_handler)(struct ivpu_device *vdev, int irq);
		bool (*ip_irq_handler)(struct ivpu_device *vdev, int irq);
		DECLARE_KFIFO(fifo, u8, IVPU_HW_IRQ_FIFO_LENGTH);
	} irq;
	struct {
		struct ivpu_addr_range global;
		struct ivpu_addr_range user;
		struct ivpu_addr_range shave;
		struct ivpu_addr_range dma;
	} ranges;
	struct {
		u8 min_ratio;
		u8 max_ratio;
		/*
		 * Pll ratio for the efficiency frequency. The VPU has optimum
		 * performance to power ratio at this frequency.
		 */
		u8 pn_ratio;
		u32 profiling_freq;
	} pll;
	u32 tile_fuse;
	u32 sku;
	u16 config;
	int dma_bits;
	ktime_t d0i3_entry_host_ts;
	u64 d0i3_entry_vpu_ts;
	atomic_t firewall_irq_counter;
};

int ivpu_hw_init(struct ivpu_device *vdev);
int ivpu_hw_power_up(struct ivpu_device *vdev);
int ivpu_hw_power_down(struct ivpu_device *vdev);
int ivpu_hw_reset(struct ivpu_device *vdev);
int ivpu_hw_boot_fw(struct ivpu_device *vdev);
void ivpu_hw_profiling_freq_drive(struct ivpu_device *vdev, bool enable);
void ivpu_irq_handlers_init(struct ivpu_device *vdev);
void ivpu_hw_irq_enable(struct ivpu_device *vdev);
void ivpu_hw_irq_disable(struct ivpu_device *vdev);
irqreturn_t ivpu_hw_irq_handler(int irq, void *ptr);

static inline u32 ivpu_hw_btrs_irq_handler(struct ivpu_device *vdev, int irq)
{
	return vdev->hw->irq.btrs_irq_handler(vdev, irq);
}

static inline u32 ivpu_hw_ip_irq_handler(struct ivpu_device *vdev, int irq)
{
	return vdev->hw->irq.ip_irq_handler(vdev, irq);
}

static inline void ivpu_hw_range_init(struct ivpu_addr_range *range, u64 start, u64 size)
{
	range->start = start;
	range->end = start + size;
}

static inline u64 ivpu_hw_range_size(const struct ivpu_addr_range *range)
{
	return range->end - range->start;
}

static inline u32 ivpu_hw_ratio_to_freq(struct ivpu_device *vdev, u32 ratio)
{
	return ivpu_hw_btrs_ratio_to_freq(vdev, ratio);
}

static inline void ivpu_hw_irq_clear(struct ivpu_device *vdev)
{
	ivpu_hw_ip_irq_clear(vdev);
}

static inline u32 ivpu_hw_pll_freq_get(struct ivpu_device *vdev)
{
	return ivpu_hw_btrs_pll_freq_get(vdev);
}

static inline u32 ivpu_hw_profiling_freq_get(struct ivpu_device *vdev)
{
	return vdev->hw->pll.profiling_freq;
}

static inline void ivpu_hw_diagnose_failure(struct ivpu_device *vdev)
{
	ivpu_hw_ip_diagnose_failure(vdev);
	ivpu_hw_btrs_diagnose_failure(vdev);
}

static inline u32 ivpu_hw_telemetry_offset_get(struct ivpu_device *vdev)
{
	return ivpu_hw_btrs_telemetry_offset_get(vdev);
}

static inline u32 ivpu_hw_telemetry_size_get(struct ivpu_device *vdev)
{
	return ivpu_hw_btrs_telemetry_size_get(vdev);
}

static inline u32 ivpu_hw_telemetry_enable_get(struct ivpu_device *vdev)
{
	return ivpu_hw_btrs_telemetry_enable_get(vdev);
}

static inline bool ivpu_hw_is_idle(struct ivpu_device *vdev)
{
	return ivpu_hw_btrs_is_idle(vdev);
}

static inline int ivpu_hw_wait_for_idle(struct ivpu_device *vdev)
{
	return ivpu_hw_btrs_wait_for_idle(vdev);
}

static inline void ivpu_hw_ipc_tx_set(struct ivpu_device *vdev, u32 vpu_addr)
{
	ivpu_hw_ip_ipc_tx_set(vdev, vpu_addr);
}

static inline void ivpu_hw_db_set(struct ivpu_device *vdev, u32 db_id)
{
	ivpu_hw_ip_db_set(vdev, db_id);
}

static inline u32 ivpu_hw_ipc_rx_addr_get(struct ivpu_device *vdev)
{
	return ivpu_hw_ip_ipc_rx_addr_get(vdev);
}

static inline u32 ivpu_hw_ipc_rx_count_get(struct ivpu_device *vdev)
{
	return ivpu_hw_ip_ipc_rx_count_get(vdev);
}

#endif /* __IVPU_HW_H__ */
