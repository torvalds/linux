/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 */

#ifndef __HNS_ENET_H
#define __HNS_ENET_H

#include <linux/netdevice.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "hnae.h"

#define HNS_DEBUG_OFFSET	6
#define HNS_SRV_OFFSET		2

enum hns_nic_state {
	NIC_STATE_TESTING = 0,
	NIC_STATE_RESETTING,
	NIC_STATE_REINITING,
	NIC_STATE_DOWN,
	NIC_STATE_DISABLED,
	NIC_STATE_REMOVING,
	NIC_STATE_SERVICE_INITED,
	NIC_STATE_SERVICE_SCHED,
	NIC_STATE2_RESET_REQUESTED,
	NIC_STATE_MAX
};

struct hns_nic_ring_data {
	struct hnae_ring *ring;
	struct napi_struct napi;
	cpumask_t mask; /* affinity mask */
	u32 queue_index;
	int (*poll_one)(struct hns_nic_ring_data *, int, void *);
	void (*ex_process)(struct hns_nic_ring_data *, struct sk_buff *);
	bool (*fini_process)(struct hns_nic_ring_data *);
};

/* compatible the difference between two versions */
struct hns_nic_ops {
	void (*fill_desc)(struct hnae_ring *ring, void *priv,
			  int size, dma_addr_t dma, int frag_end,
			  int buf_num, enum hns_desc_type type, int mtu);
	int (*maybe_stop_tx)(struct sk_buff **out_skb,
			     int *bnum, struct hnae_ring *ring);
	void (*get_rxd_bnum)(u32 bnum_flag, int *out_bnum);
};

struct hns_nic_priv {
	const struct fwnode_handle      *fwnode;
	u32 enet_ver;
	u32 port_id;
	int phy_mode;
	int phy_led_val;
	struct net_device *netdev;
	struct device *dev;
	struct hnae_handle *ae_handle;

	struct hns_nic_ops ops;

	/* the cb for nic to manage the ring buffer, the first half of the
	 * array is for tx_ring and vice versa for the second half
	 */
	struct hns_nic_ring_data *ring_data;

	/* The most recently read link state */
	int link;
	u64 tx_timeout_count;

	unsigned long state;

	struct timer_list service_timer;

	struct work_struct service_task;

	struct notifier_block notifier_block;
};

#define tx_ring_data(priv, idx) ((priv)->ring_data[idx])
#define rx_ring_data(priv, idx) \
	((priv)->ring_data[(priv)->ae_handle->q_num + (idx)])

void hns_ethtool_set_ops(struct net_device *ndev);
void hns_nic_net_reset(struct net_device *ndev);
void hns_nic_net_reinit(struct net_device *netdev);
int hns_nic_init_phy(struct net_device *ndev, struct hnae_handle *h);
netdev_tx_t hns_nic_net_xmit_hw(struct net_device *ndev,
				struct sk_buff *skb,
				struct hns_nic_ring_data *ring_data);

#endif	/**__HNS_ENET_H */
