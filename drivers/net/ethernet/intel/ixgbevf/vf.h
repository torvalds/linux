/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 1999 - 2024 Intel Corporation. */

#ifndef __IXGBE_VF_H__
#define __IXGBE_VF_H__

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>

#include "defines.h"
#include "regs.h"
#include "mbx.h"

struct ixgbe_hw;

struct ixgbe_mac_operations {
	s32 (*init_hw)(struct ixgbe_hw *);
	s32 (*reset_hw)(struct ixgbe_hw *);
	s32 (*start_hw)(struct ixgbe_hw *);
	s32 (*clear_hw_cntrs)(struct ixgbe_hw *);
	enum ixgbe_media_type (*get_media_type)(struct ixgbe_hw *);
	s32 (*get_mac_addr)(struct ixgbe_hw *, u8 *);
	s32 (*stop_adapter)(struct ixgbe_hw *);
	s32 (*get_bus_info)(struct ixgbe_hw *);
	s32 (*negotiate_api_version)(struct ixgbe_hw *hw, int api);

	/* Link */
	s32 (*setup_link)(struct ixgbe_hw *, ixgbe_link_speed, bool, bool);
	s32 (*check_link)(struct ixgbe_hw *, ixgbe_link_speed *, bool *, bool);
	s32 (*get_link_capabilities)(struct ixgbe_hw *, ixgbe_link_speed *,
				     bool *);

	/* RAR, Multicast, VLAN */
	s32 (*set_rar)(struct ixgbe_hw *, u32, u8 *, u32);
	s32 (*set_uc_addr)(struct ixgbe_hw *, u32, u8 *);
	s32 (*init_rx_addrs)(struct ixgbe_hw *);
	s32 (*update_mc_addr_list)(struct ixgbe_hw *, struct net_device *);
	s32 (*update_xcast_mode)(struct ixgbe_hw *, int);
	s32 (*get_link_state)(struct ixgbe_hw *hw, bool *link_state);
	s32 (*enable_mc)(struct ixgbe_hw *);
	s32 (*disable_mc)(struct ixgbe_hw *);
	s32 (*clear_vfta)(struct ixgbe_hw *);
	s32 (*set_vfta)(struct ixgbe_hw *, u32, u32, bool);
	s32 (*set_rlpml)(struct ixgbe_hw *, u16);
};

enum ixgbe_mac_type {
	ixgbe_mac_unknown = 0,
	ixgbe_mac_82599_vf,
	ixgbe_mac_X540_vf,
	ixgbe_mac_X550_vf,
	ixgbe_mac_X550EM_x_vf,
	ixgbe_mac_x550em_a_vf,
	ixgbe_mac_e610,
	ixgbe_mac_e610_vf,
	ixgbe_num_macs
};

struct ixgbe_mac_info {
	struct ixgbe_mac_operations ops;
	u8 addr[6];
	u8 perm_addr[6];

	enum ixgbe_mac_type type;

	s32  mc_filter_type;

	bool get_link_status;
	u32  max_tx_queues;
	u32  max_rx_queues;
	u32  max_msix_vectors;
};

struct ixgbe_mbx_operations {
	s32 (*init_params)(struct ixgbe_hw *hw);
	void (*release)(struct ixgbe_hw *hw);
	s32 (*read)(struct ixgbe_hw *, u32 *, u16);
	s32 (*write)(struct ixgbe_hw *, u32 *, u16);
	s32 (*check_for_msg)(struct ixgbe_hw *);
	s32 (*check_for_ack)(struct ixgbe_hw *);
	s32 (*check_for_rst)(struct ixgbe_hw *);
};

struct ixgbe_mbx_stats {
	u32 msgs_tx;
	u32 msgs_rx;

	u32 acks;
	u32 reqs;
	u32 rsts;
};

struct ixgbe_mbx_info {
	struct ixgbe_mbx_operations ops;
	struct ixgbe_mbx_stats stats;
	u32 timeout;
	u32 udelay;
	u32 vf_mailbox;
	u16 size;
};

struct ixgbe_hw {
	void *back;

	u8 __iomem *hw_addr;

	struct ixgbe_mac_info mac;
	struct ixgbe_mbx_info mbx;

	u16 device_id;
	u16 subsystem_vendor_id;
	u16 subsystem_device_id;
	u16 vendor_id;

	u8  revision_id;
	bool adapter_stopped;

	int api_version;
};

struct ixgbevf_hw_stats {
	u64 base_vfgprc;
	u64 base_vfgptc;
	u64 base_vfgorc;
	u64 base_vfgotc;
	u64 base_vfmprc;

	u64 last_vfgprc;
	u64 last_vfgptc;
	u64 last_vfgorc;
	u64 last_vfgotc;
	u64 last_vfmprc;

	u64 vfgprc;
	u64 vfgptc;
	u64 vfgorc;
	u64 vfgotc;
	u64 vfmprc;

	u64 saved_reset_vfgprc;
	u64 saved_reset_vfgptc;
	u64 saved_reset_vfgorc;
	u64 saved_reset_vfgotc;
	u64 saved_reset_vfmprc;
};

struct ixgbevf_info {
	enum ixgbe_mac_type mac;
	const struct ixgbe_mac_operations *mac_ops;
};

#define IXGBE_FAILED_READ_REG 0xffffffffU

#define IXGBE_REMOVED(a) unlikely(!(a))

static inline void ixgbe_write_reg(struct ixgbe_hw *hw, u32 reg, u32 value)
{
	u8 __iomem *reg_addr = READ_ONCE(hw->hw_addr);

	if (IXGBE_REMOVED(reg_addr))
		return;
	writel(value, reg_addr + reg);
}

#define IXGBE_WRITE_REG(h, r, v) ixgbe_write_reg(h, r, v)

u32 ixgbevf_read_reg(struct ixgbe_hw *hw, u32 reg);
#define IXGBE_READ_REG(h, r) ixgbevf_read_reg(h, r)

static inline void ixgbe_write_reg_array(struct ixgbe_hw *hw, u32 reg,
					 u32 offset, u32 value)
{
	ixgbe_write_reg(hw, reg + (offset << 2), value);
}

#define IXGBE_WRITE_REG_ARRAY(h, r, o, v) ixgbe_write_reg_array(h, r, o, v)

static inline u32 ixgbe_read_reg_array(struct ixgbe_hw *hw, u32 reg,
				       u32 offset)
{
	return ixgbevf_read_reg(hw, reg + (offset << 2));
}

#define IXGBE_READ_REG_ARRAY(h, r, o) ixgbe_read_reg_array(h, r, o)

int ixgbevf_get_queues(struct ixgbe_hw *hw, unsigned int *num_tcs,
		       unsigned int *default_tc);
int ixgbevf_get_reta_locked(struct ixgbe_hw *hw, u32 *reta, int num_rx_queues);
int ixgbevf_get_rss_key_locked(struct ixgbe_hw *hw, u8 *rss_key);
#endif /* __IXGBE_VF_H__ */
