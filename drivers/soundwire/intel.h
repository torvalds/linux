/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/* Copyright(c) 2015-17 Intel Corporation. */

#ifndef __SDW_INTEL_LOCAL_H
#define __SDW_INTEL_LOCAL_H

/**
 * struct sdw_intel_link_res - Soundwire Intel link resource structure,
 * typically populated by the controller driver.
 * @hw_ops: platform-specific ops
 * @mmio_base: mmio base of SoundWire registers
 * @registers: Link IO registers base
 * @shim: Audio shim pointer
 * @shim_vs: Audio vendor-specific shim pointer
 * @alh: ALH (Audio Link Hub) pointer
 * @irq: Interrupt line
 * @ops: Shim callback ops
 * @dev: device implementing hw_params and free callbacks
 * @shim_lock: mutex to handle access to shared SHIM registers
 * @shim_mask: global pointer to check SHIM register initialization
 * @clock_stop_quirks: mask defining requested behavior on pm_suspend
 * @link_mask: global mask needed for power-up/down sequences
 * @cdns: Cadence master descriptor
 * @list: used to walk-through all masters exposed by the same controller
 */
struct sdw_intel_link_res {
	const struct sdw_intel_hw_ops *hw_ops;

	void __iomem *mmio_base; /* not strictly needed, useful for debug */
	void __iomem *registers;
	void __iomem *shim;
	void __iomem *shim_vs;
	void __iomem *alh;
	int irq;
	const struct sdw_intel_ops *ops;
	struct device *dev;
	struct mutex *shim_lock; /* protect shared registers */
	u32 *shim_mask;
	u32 clock_stop_quirks;
	u32 link_mask;
	struct sdw_cdns *cdns;
	struct list_head list;
};

struct sdw_intel {
	struct sdw_cdns cdns;
	int instance;
	struct sdw_intel_link_res *link_res;
	bool startup_done;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
#endif
};

enum intel_pdi_type {
	INTEL_PDI_IN = 0,
	INTEL_PDI_OUT = 1,
	INTEL_PDI_BD = 2,
};

/*
 * Read, write helpers for HW registers
 */
static inline int intel_readl(void __iomem *base, int offset)
{
	return readl(base + offset);
}

static inline void intel_writel(void __iomem *base, int offset, int value)
{
	writel(value, base + offset);
}

static inline u16 intel_readw(void __iomem *base, int offset)
{
	return readw(base + offset);
}

static inline void intel_writew(void __iomem *base, int offset, u16 value)
{
	writew(value, base + offset);
}

#define cdns_to_intel(_cdns) container_of(_cdns, struct sdw_intel, cdns)

#define INTEL_MASTER_RESET_ITERATIONS	10

#define SDW_INTEL_CHECK_OPS(sdw, cb)	((sdw) && (sdw)->link_res && (sdw)->link_res->hw_ops && \
					 (sdw)->link_res->hw_ops->cb)
#define SDW_INTEL_OPS(sdw, cb)		((sdw)->link_res->hw_ops->cb)

static inline void sdw_intel_debugfs_init(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, debugfs_init))
		SDW_INTEL_OPS(sdw, debugfs_init)(sdw);
}

static inline void sdw_intel_debugfs_exit(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, debugfs_exit))
		SDW_INTEL_OPS(sdw, debugfs_exit)(sdw);
}

static inline int sdw_intel_register_dai(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, register_dai))
		return SDW_INTEL_OPS(sdw, register_dai)(sdw);
	return -ENOTSUPP;
}

static inline void sdw_intel_check_clock_stop(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, check_clock_stop))
		SDW_INTEL_OPS(sdw, check_clock_stop)(sdw);
}

static inline int sdw_intel_start_bus(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, start_bus))
		return SDW_INTEL_OPS(sdw, start_bus)(sdw);
	return -ENOTSUPP;
}

static inline int sdw_intel_start_bus_after_reset(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, start_bus_after_reset))
		return SDW_INTEL_OPS(sdw, start_bus_after_reset)(sdw);
	return -ENOTSUPP;
}

static inline int sdw_intel_start_bus_after_clock_stop(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, start_bus_after_clock_stop))
		return SDW_INTEL_OPS(sdw, start_bus_after_clock_stop)(sdw);
	return -ENOTSUPP;
}

static inline int sdw_intel_stop_bus(struct sdw_intel *sdw, bool clock_stop)
{
	if (SDW_INTEL_CHECK_OPS(sdw, stop_bus))
		return SDW_INTEL_OPS(sdw, stop_bus)(sdw, clock_stop);
	return -ENOTSUPP;
}

static inline int sdw_intel_link_power_up(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, link_power_up))
		return SDW_INTEL_OPS(sdw, link_power_up)(sdw);
	return -ENOTSUPP;
}

static inline int sdw_intel_link_power_down(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, link_power_down))
		return SDW_INTEL_OPS(sdw, link_power_down)(sdw);
	return -ENOTSUPP;
}

static inline int sdw_intel_shim_check_wake(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, shim_check_wake))
		return SDW_INTEL_OPS(sdw, shim_check_wake)(sdw);
	return -ENOTSUPP;
}

static inline void sdw_intel_shim_wake(struct sdw_intel *sdw, bool wake_enable)
{
	if (SDW_INTEL_CHECK_OPS(sdw, shim_wake))
		SDW_INTEL_OPS(sdw, shim_wake)(sdw, wake_enable);
}

static inline void sdw_intel_sync_arm(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, sync_arm))
		SDW_INTEL_OPS(sdw, sync_arm)(sdw);
}

static inline int sdw_intel_sync_go_unlocked(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, sync_go_unlocked))
		return SDW_INTEL_OPS(sdw, sync_go_unlocked)(sdw);
	return -ENOTSUPP;
}

static inline int sdw_intel_sync_go(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, sync_go))
		return SDW_INTEL_OPS(sdw, sync_go)(sdw);
	return -ENOTSUPP;
}

static inline bool sdw_intel_sync_check_cmdsync_unlocked(struct sdw_intel *sdw)
{
	if (SDW_INTEL_CHECK_OPS(sdw, sync_check_cmdsync_unlocked))
		return SDW_INTEL_OPS(sdw, sync_check_cmdsync_unlocked)(sdw);
	return false;
}

/* common bus management */
int intel_start_bus(struct sdw_intel *sdw);
int intel_start_bus_after_reset(struct sdw_intel *sdw);
void intel_check_clock_stop(struct sdw_intel *sdw);
int intel_start_bus_after_clock_stop(struct sdw_intel *sdw);
int intel_stop_bus(struct sdw_intel *sdw, bool clock_stop);

/* common bank switch routines */
int intel_pre_bank_switch(struct sdw_intel *sdw);
int intel_post_bank_switch(struct sdw_intel *sdw);

#endif /* __SDW_INTEL_LOCAL_H */
