/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_nic.h: Declaration of common code for NIC. */

#ifndef AQ_NIC_H
#define AQ_NIC_H

#include "aq_common.h"
#include "aq_rss.h"
#include "aq_hw.h"

struct aq_ring_s;
struct aq_hw_ops;
struct aq_fw_s;
struct aq_vec_s;

struct aq_nic_cfg_s {
	const struct aq_hw_caps_s *aq_hw_caps;
	u64 features;
	u32 rxds;		/* rx ring size, descriptors # */
	u32 txds;		/* tx ring size, descriptors # */
	u32 vecs;		/* allocated rx/tx vectors */
	u32 link_irq_vec;
	u32 irq_type;
	u32 itr;
	u16 rx_itr;
	u16 tx_itr;
	u32 rxpageorder;
	u32 num_rss_queues;
	u32 mtu;
	u32 flow_control;
	u32 link_speed_msk;
	u32 wol;
	u16 is_mc_list_enabled;
	u16 mc_list_count;
	bool is_autoneg;
	bool is_polling;
	bool is_rss;
	bool is_lro;
	u8  tcs;
	struct aq_rss_parameters aq_rss;
	u32 eee_speeds;
};

#define AQ_NIC_FLAG_STARTED     0x00000004U
#define AQ_NIC_FLAG_STOPPING    0x00000008U
#define AQ_NIC_FLAG_RESETTING   0x00000010U
#define AQ_NIC_FLAG_CLOSING     0x00000020U
#define AQ_NIC_LINK_DOWN        0x04000000U
#define AQ_NIC_FLAG_ERR_UNPLUG  0x40000000U
#define AQ_NIC_FLAG_ERR_HW      0x80000000U

#define AQ_NIC_WOL_ENABLED	BIT(0)

#define AQ_NIC_TCVEC2RING(_NIC_, _TC_, _VEC_) \
	((_TC_) * AQ_CFG_TCS_MAX + (_VEC_))

struct aq_hw_rx_fl2 {
	struct aq_rx_filter_vlan aq_vlans[AQ_VLAN_MAX_FILTERS];
};

struct aq_hw_rx_fl3l4 {
	u8   active_ipv4;
	u8   active_ipv6:2;
	u8 is_ipv6;
};

struct aq_hw_rx_fltrs_s {
	struct hlist_head     filter_list;
	u16                   active_filters;
	struct aq_hw_rx_fl2   fl2;
	struct aq_hw_rx_fl3l4 fl3l4;
};

struct aq_nic_s {
	atomic_t flags;
	struct aq_vec_s *aq_vec[AQ_CFG_VECS_MAX];
	struct aq_ring_s *aq_ring_tx[AQ_CFG_VECS_MAX * AQ_CFG_TCS_MAX];
	struct aq_hw_s *aq_hw;
	struct net_device *ndev;
	unsigned int aq_vecs;
	unsigned int packet_filter;
	unsigned int power_state;
	u8 port;
	const struct aq_hw_ops *aq_hw_ops;
	const struct aq_fw_ops *aq_fw_ops;
	struct aq_nic_cfg_s aq_nic_cfg;
	struct timer_list service_timer;
	struct work_struct service_task;
	struct timer_list polling_timer;
	struct aq_hw_link_status_s link_status;
	struct {
		u32 count;
		u8 ar[AQ_HW_MULTICAST_ADDRESS_MAX][ETH_ALEN];
	} mc_list;
	/* Bitmask of currently assigned vlans from linux */
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];

	struct pci_dev *pdev;
	unsigned int msix_entry_mask;
	u32 irqvecs;
	/* mutex to serialize FW interface access operations */
	struct mutex fwreq_mutex;
	struct aq_hw_rx_fltrs_s aq_hw_rx_fltrs;
};

static inline struct device *aq_nic_get_dev(struct aq_nic_s *self)
{
	return self->ndev->dev.parent;
}

void aq_nic_ndev_init(struct aq_nic_s *self);
struct aq_nic_s *aq_nic_alloc_hot(struct net_device *ndev);
void aq_nic_set_tx_ring(struct aq_nic_s *self, unsigned int idx,
			struct aq_ring_s *ring);
struct net_device *aq_nic_get_ndev(struct aq_nic_s *self);
int aq_nic_init(struct aq_nic_s *self);
void aq_nic_cfg_start(struct aq_nic_s *self);
int aq_nic_ndev_register(struct aq_nic_s *self);
void aq_nic_ndev_free(struct aq_nic_s *self);
int aq_nic_start(struct aq_nic_s *self);
int aq_nic_xmit(struct aq_nic_s *self, struct sk_buff *skb);
int aq_nic_get_regs(struct aq_nic_s *self, struct ethtool_regs *regs, void *p);
int aq_nic_get_regs_count(struct aq_nic_s *self);
void aq_nic_get_stats(struct aq_nic_s *self, u64 *data);
int aq_nic_stop(struct aq_nic_s *self);
void aq_nic_deinit(struct aq_nic_s *self);
void aq_nic_free_hot_resources(struct aq_nic_s *self);
void aq_nic_free_vectors(struct aq_nic_s *self);
int aq_nic_set_mtu(struct aq_nic_s *self, int new_mtu);
int aq_nic_set_mac(struct aq_nic_s *self, struct net_device *ndev);
int aq_nic_set_packet_filter(struct aq_nic_s *self, unsigned int flags);
int aq_nic_set_multicast_list(struct aq_nic_s *self, struct net_device *ndev);
unsigned int aq_nic_get_link_speed(struct aq_nic_s *self);
void aq_nic_get_link_ksettings(struct aq_nic_s *self,
			       struct ethtool_link_ksettings *cmd);
int aq_nic_set_link_ksettings(struct aq_nic_s *self,
			      const struct ethtool_link_ksettings *cmd);
struct aq_nic_cfg_s *aq_nic_get_cfg(struct aq_nic_s *self);
u32 aq_nic_get_fw_version(struct aq_nic_s *self);
int aq_nic_change_pm_state(struct aq_nic_s *self, pm_message_t *pm_msg);
int aq_nic_update_interrupt_moderation_settings(struct aq_nic_s *self);
void aq_nic_shutdown(struct aq_nic_s *self);

#endif /* AQ_NIC_H */
