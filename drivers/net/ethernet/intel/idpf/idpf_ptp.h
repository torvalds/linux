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
 * @sys_time_ns_l: low part of the system time register
 * @sys_time_ns_h: high part of the system time register
 * @incval_l: low part of the increment value register
 * @incval_h: high part of the increment value register
 * @shadj_l: low part of the shadow adjust register
 * @shadj_h: high part of the shadow adjust register
 * @phy_incval_l: low part of the PHY increment value register
 * @phy_incval_h: high part of the PHY increment value register
 * @phy_shadj_l: low part of the PHY shadow adjust register
 * @phy_shadj_h: high part of the PHY shadow adjust register
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

	/* System time */
	void __iomem *sys_time_ns_l;
	void __iomem *sys_time_ns_h;

	/* Main timer adjustments */
	void __iomem *incval_l;
	void __iomem *incval_h;
	void __iomem *shadj_l;
	void __iomem *shadj_h;

	/* PHY timer adjustments */
	void __iomem *phy_incval_l;
	void __iomem *phy_incval_h;
	void __iomem *phy_shadj_l;
	void __iomem *phy_shadj_h;

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
 * struct idpf_ptp_secondary_mbx - PTP secondary mailbox
 * @peer_mbx_q_id: PTP mailbox queue ID
 * @peer_id: Peer ID for PTP Device Control daemon
 * @valid: indicates whether secondary mailblox is supported by the Control
 *	   Plane
 */
struct idpf_ptp_secondary_mbx {
	u16 peer_mbx_q_id;
	u16 peer_id;
	bool valid:1;
};

/**
 * enum idpf_ptp_tx_tstamp_state - Tx timestamp states
 * @IDPF_PTP_FREE: Tx timestamp index free to use
 * @IDPF_PTP_REQUEST: Tx timestamp index set to the Tx descriptor
 * @IDPF_PTP_READ_VALUE: Tx timestamp value ready to be read
 */
enum idpf_ptp_tx_tstamp_state {
	IDPF_PTP_FREE,
	IDPF_PTP_REQUEST,
	IDPF_PTP_READ_VALUE,
};

/**
 * struct idpf_ptp_tx_tstamp_status - Parameters to track Tx timestamp
 * @skb: the pointer to the SKB that received the completion tag
 * @state: the state of the Tx timestamp
 */
struct idpf_ptp_tx_tstamp_status {
	struct sk_buff *skb;
	enum idpf_ptp_tx_tstamp_state state;
};

/**
 * struct idpf_ptp_tx_tstamp - Parameters for Tx timestamping
 * @list_member: the list member structure
 * @tx_latch_reg_offset_l: Tx tstamp latch low register offset
 * @tx_latch_reg_offset_h: Tx tstamp latch high register offset
 * @skb: the pointer to the SKB for this timestamp request
 * @tstamp: the Tx tstamp value
 * @idx: the index of the Tx tstamp
 */
struct idpf_ptp_tx_tstamp {
	struct list_head list_member;
	u32 tx_latch_reg_offset_l;
	u32 tx_latch_reg_offset_h;
	struct sk_buff *skb;
	u64 tstamp;
	u32 idx;
};

/**
 * struct idpf_ptp_vport_tx_tstamp_caps - Tx timestamp capabilities
 * @vport_id: the vport id
 * @num_entries: the number of negotiated Tx timestamp entries
 * @tstamp_ns_lo_bit: first bit for nanosecond part of the timestamp
 * @latches_lock: the lock to the lists of free/used timestamp indexes
 * @status_lock: the lock to the status tracker
 * @access: indicates an access to Tx timestamp
 * @latches_free: the list of the free Tx timestamps latches
 * @latches_in_use: the list of the used Tx timestamps latches
 * @tx_tstamp_status: Tx tstamp status tracker
 */
struct idpf_ptp_vport_tx_tstamp_caps {
	u32 vport_id;
	u16 num_entries;
	u16 tstamp_ns_lo_bit;
	spinlock_t latches_lock;
	spinlock_t status_lock;
	bool access:1;
	struct list_head latches_free;
	struct list_head latches_in_use;
	struct idpf_ptp_tx_tstamp_status tx_tstamp_status[];
};

/**
 * struct idpf_ptp - PTP parameters
 * @info: structure defining PTP hardware capabilities
 * @clock: pointer to registered PTP clock device
 * @adapter: back pointer to the adapter
 * @base_incval: base increment value of the PTP clock
 * @max_adj: maximum adjustment of the PTP clock
 * @cmd: HW specific command masks
 * @cached_phc_time: a cached copy of the PHC time for timestamp extension
 * @cached_phc_jiffies: jiffies when cached_phc_time was last updated
 * @dev_clk_regs: the set of registers to access the device clock
 * @caps: PTP capabilities negotiated with the Control Plane
 * @get_dev_clk_time_access: access type for getting the device clock time
 * @get_cross_tstamp_access: access type for the cross timestamping
 * @set_dev_clk_time_access: access type for setting the device clock time
 * @adj_dev_clk_time_access: access type for the adjusting the device clock
 * @tx_tstamp_access: access type for the Tx timestamp value read
 * @rsv: reserved bits
 * @secondary_mbx: parameters for using dedicated PTP mailbox
 * @read_dev_clk_lock: spinlock protecting access to the device clock read
 *		       operation executed by the HW latch
 */
struct idpf_ptp {
	struct ptp_clock_info info;
	struct ptp_clock *clock;
	struct idpf_adapter *adapter;
	u64 base_incval;
	u64 max_adj;
	struct idpf_ptp_cmd cmd;
	u64 cached_phc_time;
	unsigned long cached_phc_jiffies;
	struct idpf_ptp_dev_clk_regs dev_clk_regs;
	u32 caps;
	enum idpf_ptp_access get_dev_clk_time_access:2;
	enum idpf_ptp_access get_cross_tstamp_access:2;
	enum idpf_ptp_access set_dev_clk_time_access:2;
	enum idpf_ptp_access adj_dev_clk_time_access:2;
	enum idpf_ptp_access tx_tstamp_access:2;
	u8 rsv;
	struct idpf_ptp_secondary_mbx secondary_mbx;
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

/**
 * struct idpf_ptp_dev_timers - System time and device time values
 * @sys_time_ns: system time value expressed in nanoseconds
 * @dev_clk_time_ns: device clock time value expressed in nanoseconds
 */
struct idpf_ptp_dev_timers {
	u64 sys_time_ns;
	u64 dev_clk_time_ns;
};

/**
 * idpf_ptp_is_vport_tx_tstamp_ena - Verify the Tx timestamping enablement for
 *				     a given vport.
 * @vport: Virtual port structure
 *
 * Tx timestamp capabilities are negotiated with the Control Plane only if the
 * device clock value can be read, Tx timestamp access type is different than
 * NONE, and the PTP clock for the adapter is created. When all those conditions
 * are satisfied, Tx timestamp feature is enabled and tx_tstamp_caps is
 * allocated and fulfilled.
 *
 * Return: true if the Tx timestamping is enabled, false otherwise.
 */
static inline bool idpf_ptp_is_vport_tx_tstamp_ena(struct idpf_vport *vport)
{
	if (!vport->tx_tstamp_caps)
		return false;
	else
		return true;
}

/**
 * idpf_ptp_is_vport_rx_tstamp_ena - Verify the Rx timestamping enablement for
 *				     a given vport.
 * @vport: Virtual port structure
 *
 * Rx timestamp feature is enabled if the PTP clock for the adapter is created
 * and it is possible to read the value of the device clock. The second
 * assumption comes from the need to extend the Rx timestamp value to 64 bit
 * based on the current device clock time.
 *
 * Return: true if the Rx timestamping is enabled, false otherwise.
 */
static inline bool idpf_ptp_is_vport_rx_tstamp_ena(struct idpf_vport *vport)
{
	if (!vport->adapter->ptp ||
	    vport->adapter->ptp->get_dev_clk_time_access == IDPF_PTP_NONE)
		return false;
	else
		return true;
}

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
int idpf_ptp_init(struct idpf_adapter *adapter);
void idpf_ptp_release(struct idpf_adapter *adapter);
int idpf_ptp_get_caps(struct idpf_adapter *adapter);
void idpf_ptp_get_features_access(const struct idpf_adapter *adapter);
bool idpf_ptp_get_txq_tstamp_capability(struct idpf_tx_queue *txq);
int idpf_ptp_get_dev_clk_time(struct idpf_adapter *adapter,
			      struct idpf_ptp_dev_timers *dev_clk_time);
int idpf_ptp_get_cross_time(struct idpf_adapter *adapter,
			    struct idpf_ptp_dev_timers *cross_time);
int idpf_ptp_set_dev_clk_time(struct idpf_adapter *adapter, u64 time);
int idpf_ptp_adj_dev_clk_fine(struct idpf_adapter *adapter, u64 incval);
int idpf_ptp_adj_dev_clk_time(struct idpf_adapter *adapter, s64 delta);
int idpf_ptp_get_vport_tstamps_caps(struct idpf_vport *vport);
int idpf_ptp_get_tx_tstamp(struct idpf_vport *vport);
int idpf_ptp_set_timestamp_mode(struct idpf_vport *vport,
				struct kernel_hwtstamp_config *config);
u64 idpf_ptp_extend_ts(struct idpf_vport *vport, u64 in_tstamp);
u64 idpf_ptp_tstamp_extend_32b_to_64b(u64 cached_phc_time, u32 in_timestamp);
int idpf_ptp_request_ts(struct idpf_tx_queue *tx_q, struct sk_buff *skb,
			u32 *idx);
void idpf_tstamp_task(struct work_struct *work);
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

static inline bool
idpf_ptp_get_txq_tstamp_capability(struct idpf_tx_queue *txq)
{
	return false;
}

static inline int
idpf_ptp_get_dev_clk_time(struct idpf_adapter *adapter,
			  struct idpf_ptp_dev_timers *dev_clk_time)
{
	return -EOPNOTSUPP;
}

static inline int
idpf_ptp_get_cross_time(struct idpf_adapter *adapter,
			struct idpf_ptp_dev_timers *cross_time)
{
	return -EOPNOTSUPP;
}

static inline int idpf_ptp_set_dev_clk_time(struct idpf_adapter *adapter,
					    u64 time)
{
	return -EOPNOTSUPP;
}

static inline int idpf_ptp_adj_dev_clk_fine(struct idpf_adapter *adapter,
					    u64 incval)
{
	return -EOPNOTSUPP;
}

static inline int idpf_ptp_adj_dev_clk_time(struct idpf_adapter *adapter,
					    s64 delta)
{
	return -EOPNOTSUPP;
}

static inline int idpf_ptp_get_vport_tstamps_caps(struct idpf_vport *vport)
{
	return -EOPNOTSUPP;
}

static inline int idpf_ptp_get_tx_tstamp(struct idpf_vport *vport)
{
	return -EOPNOTSUPP;
}

static inline int
idpf_ptp_set_timestamp_mode(struct idpf_vport *vport,
			    struct kernel_hwtstamp_config *config)
{
	return -EOPNOTSUPP;
}

static inline u64 idpf_ptp_extend_ts(struct idpf_vport *vport, u32 in_tstamp)
{
	return 0;
}

static inline u64 idpf_ptp_tstamp_extend_32b_to_64b(u64 cached_phc_time,
						    u32 in_timestamp)
{
	return 0;
}

static inline int idpf_ptp_request_ts(struct idpf_tx_queue *tx_q,
				      struct sk_buff *skb, u32 *idx)
{
	return -EOPNOTSUPP;
}

static inline void idpf_tstamp_task(struct work_struct *work) { }
#endif /* CONFIG_PTP_1588_CLOCK */
#endif /* _IDPF_PTP_H */
