/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024 Intel Corporation */

#ifndef _IDPF_PTP_H
#define _IDPF_PTP_H

#include <linux/ptp_clock_kernel.h>

/**
 * struct idpf_ptp_cmd - PTP command masks
 * @exec_cmd_mask: mask to trigger command execution
 * @shtime_enable_mask: mask to enable shadow time
 */
struct idpf_ptp_cmd {
	u32 exec_cmd_mask;
	u32 shtime_enable_mask;
};

/* struct idpf_ptp_dev_clk_regs - PTP device registers
 * @dev_clk_ns_l: low part of the device clock register
 * @dev_clk_ns_h: high part of the device clock register
 * @phy_clk_ns_l: low part of the PHY clock register
 * @phy_clk_ns_h: high part of the PHY clock register
 * @cmd: PTP command register
 * @phy_cmd: PHY command register
 * @cmd_sync: PTP command synchronization register
 */
struct idpf_ptp_dev_clk_regs {
	/* Main clock */
	void __iomem *dev_clk_ns_l;
	void __iomem *dev_clk_ns_h;

	/* PHY timer */
	void __iomem *phy_clk_ns_l;
	void __iomem *phy_clk_ns_h;

	/* Command */
	void __iomem *cmd;
	void __iomem *phy_cmd;
	void __iomem *cmd_sync;
};

/**
 * enum idpf_ptp_access - the type of access to PTP operations
 * @IDPF_PTP_NONE: no access
 * @IDPF_PTP_DIRECT: direct access through BAR registers
 * @IDPF_PTP_MAILBOX: access through mailbox messages
 */
enum idpf_ptp_access {
	IDPF_PTP_NONE = 0,
	IDPF_PTP_DIRECT,
	IDPF_PTP_MAILBOX,
};

/**
 * struct idpf_ptp - PTP parameters
 * @info: structure defining PTP hardware capabilities
 * @clock: pointer to registered PTP clock device
 * @adapter: back pointer to the adapter
 * @cmd: HW specific command masks
 * @dev_clk_regs: the set of registers to access the device clock
 * @caps: PTP capabilities negotiated with the Control Plane
 * @get_dev_clk_time_access: access type for getting the device clock time
 * @rsv: reserved bits
 * @read_dev_clk_lock: spinlock protecting access to the device clock read
 *		       operation executed by the HW latch
 */
struct idpf_ptp {
	struct ptp_clock_info info;
	struct ptp_clock *clock;
	struct idpf_adapter *adapter;
	struct idpf_ptp_cmd cmd;
	struct idpf_ptp_dev_clk_regs dev_clk_regs;
	u32 caps;
	enum idpf_ptp_access get_dev_clk_time_access:2;
	u32 rsv:30;
	spinlock_t read_dev_clk_lock;
};

/**
 * idpf_ptp_info_to_adapter - get driver adapter struct from ptp_clock_info
 * @info: pointer to ptp_clock_info struct
 *
 * Return: pointer to the corresponding adapter struct
 */
static inline struct idpf_adapter *
idpf_ptp_info_to_adapter(const struct ptp_clock_info *info)
{
	const struct idpf_ptp *ptp = container_of_const(info, struct idpf_ptp,
							info);
	return ptp->adapter;
}

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
int idpf_ptp_init(struct idpf_adapter *adapter);
void idpf_ptp_release(struct idpf_adapter *adapter);
int idpf_ptp_get_caps(struct idpf_adapter *adapter);
void idpf_ptp_get_features_access(const struct idpf_adapter *adapter);
#else /* CONFIG_PTP_1588_CLOCK */
static inline int idpf_ptp_init(struct idpf_adapter *adapter)
{
	return 0;
}

static inline void idpf_ptp_release(struct idpf_adapter *adapter) { }

static inline int idpf_ptp_get_caps(struct idpf_adapter *adapter)
{
	return -EOPNOTSUPP;
}

static inline void
idpf_ptp_get_features_access(const struct idpf_adapter *adapter) { }

#endif /* CONFIG_PTP_1588_CLOCK */
#endif /* _IDPF_PTP_H */
