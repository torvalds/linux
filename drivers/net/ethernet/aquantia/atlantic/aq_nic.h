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

struct aq_ring_s;
struct aq_pci_func_s;
struct aq_hw_ops;

#define AQ_NIC_FC_OFF    0U
#define AQ_NIC_FC_TX     1U
#define AQ_NIC_FC_RX     2U
#define AQ_NIC_FC_FULL   3U
#define AQ_NIC_FC_AUTO   4U

#define AQ_NIC_RATE_10G        BIT(0)
#define AQ_NIC_RATE_5G         BIT(1)
#define AQ_NIC_RATE_5GSR       BIT(2)
#define AQ_NIC_RATE_2GS        BIT(3)
#define AQ_NIC_RATE_1G         BIT(4)
#define AQ_NIC_RATE_100M       BIT(5)

struct aq_nic_cfg_s {
	struct aq_hw_caps_s *aq_hw_caps;
	u64 hw_features;
	u32 rxds;		/* rx ring size, descriptors # */
	u32 txds;		/* tx ring size, descriptors # */
	u32 vecs;		/* vecs==allocated irqs */
	u32 irq_type;
	u32 itr;
	u32 num_rss_queues;
	u32 mtu;
	u32 ucp_0x364;
	u32 flow_control;
	u32 link_speed_msk;
	u32 vlan_id;
	u16 is_mc_list_enabled;
	u16 mc_list_count;
	bool is_autoneg;
	bool is_interrupt_moderation;
	bool is_polling;
	bool is_rss;
	bool is_lro;
	u8  tcs;
	struct aq_rss_parameters aq_rss;
};

#define AQ_NIC_FLAG_STARTED     0x00000004U
#define AQ_NIC_FLAG_STOPPING    0x00000008U
#define AQ_NIC_FLAG_RESETTING   0x00000010U
#define AQ_NIC_FLAG_CLOSING     0x00000020U
#define AQ_NIC_LINK_DOWN        0x04000000U
#define AQ_NIC_FLAG_ERR_UNPLUG  0x40000000U
#define AQ_NIC_FLAG_ERR_HW      0x80000000U

#define AQ_NIC_TCVEC2RING(_NIC_, _TC_, _VEC_) \
	((_TC_) * AQ_CFG_TCS_MAX + (_VEC_))

struct aq_nic_s *aq_nic_alloc_cold(const struct net_device_ops *ndev_ops,
				   const struct ethtool_ops *et_ops,
				   struct device *dev,
				   struct aq_pci_func_s *aq_pci_func,
				   unsigned int port,
				   const struct aq_hw_ops *aq_hw_ops);
int aq_nic_ndev_init(struct aq_nic_s *self);
struct aq_nic_s *aq_nic_alloc_hot(struct net_device *ndev);
void aq_nic_set_tx_ring(struct aq_nic_s *self, unsigned int idx,
			struct aq_ring_s *ring);
struct device *aq_nic_get_dev(struct aq_nic_s *self);
struct net_device *aq_nic_get_ndev(struct aq_nic_s *self);
int aq_nic_init(struct aq_nic_s *self);
int aq_nic_cfg_start(struct aq_nic_s *self);
int aq_nic_ndev_register(struct aq_nic_s *self);
void aq_nic_ndev_queue_start(struct aq_nic_s *self, unsigned int idx);
void aq_nic_ndev_queue_stop(struct aq_nic_s *self, unsigned int idx);
void aq_nic_ndev_free(struct aq_nic_s *self);
int aq_nic_start(struct aq_nic_s *self);
int aq_nic_xmit(struct aq_nic_s *self, struct sk_buff *skb);
int aq_nic_get_regs(struct aq_nic_s *self, struct ethtool_regs *regs, void *p);
int aq_nic_get_regs_count(struct aq_nic_s *self);
void aq_nic_get_stats(struct aq_nic_s *self, u64 *data);
int aq_nic_stop(struct aq_nic_s *self);
void aq_nic_deinit(struct aq_nic_s *self);
void aq_nic_free_hot_resources(struct aq_nic_s *self);
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

#endif /* AQ_NIC_H */
