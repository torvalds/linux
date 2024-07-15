/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __IVPU_HW_H__
#define __IVPU_HW_H__

#include "ivpu_drv.h"

struct ivpu_hw_ops {
	int (*info_init)(struct ivpu_device *vdev);
	int (*power_up)(struct ivpu_device *vdev);
	int (*boot_fw)(struct ivpu_device *vdev);
	int (*power_down)(struct ivpu_device *vdev);
	int (*reset)(struct ivpu_device *vdev);
	bool (*is_idle)(struct ivpu_device *vdev);
	int (*wait_for_idle)(struct ivpu_device *vdev);
	void (*wdt_disable)(struct ivpu_device *vdev);
	void (*diagnose_failure)(struct ivpu_device *vdev);
	u32 (*profiling_freq_get)(struct ivpu_device *vdev);
	void (*profiling_freq_drive)(struct ivpu_device *vdev, bool enable);
	u32 (*reg_pll_freq_get)(struct ivpu_device *vdev);
	u32 (*ratio_to_freq)(struct ivpu_device *vdev, u32 ratio);
	u32 (*reg_telemetry_offset_get)(struct ivpu_device *vdev);
	u32 (*reg_telemetry_size_get)(struct ivpu_device *vdev);
	u32 (*reg_telemetry_enable_get)(struct ivpu_device *vdev);
	void (*reg_db_set)(struct ivpu_device *vdev, u32 db_id);
	u32 (*reg_ipc_rx_addr_get)(struct ivpu_device *vdev);
	u32 (*reg_ipc_rx_count_get)(struct ivpu_device *vdev);
	void (*reg_ipc_tx_set)(struct ivpu_device *vdev, u32 vpu_addr);
	void (*irq_clear)(struct ivpu_device *vdev);
	void (*irq_enable)(struct ivpu_device *vdev);
	void (*irq_disable)(struct ivpu_device *vdev);
	irqreturn_t (*irq_handler)(int irq, void *ptr);
};

struct ivpu_addr_range {
	resource_size_t start;
	resource_size_t end;
};

struct ivpu_hw_info {
	const struct ivpu_hw_ops *ops;
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
};

extern const struct ivpu_hw_ops ivpu_hw_37xx_ops;
extern const struct ivpu_hw_ops ivpu_hw_40xx_ops;

static inline int ivpu_hw_info_init(struct ivpu_device *vdev)
{
	return vdev->hw->ops->info_init(vdev);
};

static inline int ivpu_hw_power_up(struct ivpu_device *vdev)
{
	ivpu_dbg(vdev, PM, "HW power up\n");

	return vdev->hw->ops->power_up(vdev);
};

static inline int ivpu_hw_boot_fw(struct ivpu_device *vdev)
{
	return vdev->hw->ops->boot_fw(vdev);
};

static inline bool ivpu_hw_is_idle(struct ivpu_device *vdev)
{
	return vdev->hw->ops->is_idle(vdev);
};

static inline int ivpu_hw_wait_for_idle(struct ivpu_device *vdev)
{
	return vdev->hw->ops->wait_for_idle(vdev);
};

static inline int ivpu_hw_power_down(struct ivpu_device *vdev)
{
	ivpu_dbg(vdev, PM, "HW power down\n");

	return vdev->hw->ops->power_down(vdev);
};

static inline int ivpu_hw_reset(struct ivpu_device *vdev)
{
	ivpu_dbg(vdev, PM, "HW reset\n");

	return vdev->hw->ops->reset(vdev);
};

static inline void ivpu_hw_wdt_disable(struct ivpu_device *vdev)
{
	vdev->hw->ops->wdt_disable(vdev);
};

static inline u32 ivpu_hw_profiling_freq_get(struct ivpu_device *vdev)
{
	return vdev->hw->ops->profiling_freq_get(vdev);
};

static inline void ivpu_hw_profiling_freq_drive(struct ivpu_device *vdev, bool enable)
{
	return vdev->hw->ops->profiling_freq_drive(vdev, enable);
};

/* Register indirect accesses */
static inline u32 ivpu_hw_reg_pll_freq_get(struct ivpu_device *vdev)
{
	return vdev->hw->ops->reg_pll_freq_get(vdev);
};

static inline u32 ivpu_hw_ratio_to_freq(struct ivpu_device *vdev, u32 ratio)
{
	return vdev->hw->ops->ratio_to_freq(vdev, ratio);
}

static inline u32 ivpu_hw_reg_telemetry_offset_get(struct ivpu_device *vdev)
{
	return vdev->hw->ops->reg_telemetry_offset_get(vdev);
};

static inline u32 ivpu_hw_reg_telemetry_size_get(struct ivpu_device *vdev)
{
	return vdev->hw->ops->reg_telemetry_size_get(vdev);
};

static inline u32 ivpu_hw_reg_telemetry_enable_get(struct ivpu_device *vdev)
{
	return vdev->hw->ops->reg_telemetry_enable_get(vdev);
};

static inline void ivpu_hw_reg_db_set(struct ivpu_device *vdev, u32 db_id)
{
	vdev->hw->ops->reg_db_set(vdev, db_id);
};

static inline u32 ivpu_hw_reg_ipc_rx_addr_get(struct ivpu_device *vdev)
{
	return vdev->hw->ops->reg_ipc_rx_addr_get(vdev);
};

static inline u32 ivpu_hw_reg_ipc_rx_count_get(struct ivpu_device *vdev)
{
	return vdev->hw->ops->reg_ipc_rx_count_get(vdev);
};

static inline void ivpu_hw_reg_ipc_tx_set(struct ivpu_device *vdev, u32 vpu_addr)
{
	vdev->hw->ops->reg_ipc_tx_set(vdev, vpu_addr);
};

static inline void ivpu_hw_irq_clear(struct ivpu_device *vdev)
{
	vdev->hw->ops->irq_clear(vdev);
};

static inline void ivpu_hw_irq_enable(struct ivpu_device *vdev)
{
	vdev->hw->ops->irq_enable(vdev);
};

static inline void ivpu_hw_irq_disable(struct ivpu_device *vdev)
{
	vdev->hw->ops->irq_disable(vdev);
};

static inline void ivpu_hw_init_range(struct ivpu_addr_range *range, u64 start, u64 size)
{
	range->start = start;
	range->end = start + size;
}

static inline u64 ivpu_hw_range_size(const struct ivpu_addr_range *range)
{
	return range->end - range->start;
}

static inline void ivpu_hw_diagnose_failure(struct ivpu_device *vdev)
{
	vdev->hw->ops->diagnose_failure(vdev);
}

#endif /* __IVPU_HW_H__ */
