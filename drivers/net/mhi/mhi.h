/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MHI Network driver - Network over MHI bus
 *
 * Copyright (C) 2021 Linaro Ltd <loic.poulain@linaro.org>
 */

struct mhi_net_stats {
	u64_stats_t rx_packets;
	u64_stats_t rx_bytes;
	u64_stats_t rx_errors;
	u64_stats_t rx_dropped;
	u64_stats_t rx_length_errors;
	u64_stats_t tx_packets;
	u64_stats_t tx_bytes;
	u64_stats_t tx_errors;
	u64_stats_t tx_dropped;
	struct u64_stats_sync tx_syncp;
	struct u64_stats_sync rx_syncp;
};

struct mhi_net_dev {
	struct mhi_device *mdev;
	struct net_device *ndev;
	struct sk_buff *skbagg_head;
	struct sk_buff *skbagg_tail;
	const struct mhi_net_proto *proto;
	void *proto_data;
	struct delayed_work rx_refill;
	struct mhi_net_stats stats;
	u32 rx_queue_sz;
	int msg_enable;
};

struct mhi_net_proto {
	int (*init)(struct mhi_net_dev *mhi_netdev);
	struct sk_buff * (*tx_fixup)(struct mhi_net_dev *mhi_netdev, struct sk_buff *skb);
	void (*rx)(struct mhi_net_dev *mhi_netdev, struct sk_buff *skb);
};

extern const struct mhi_net_proto proto_mbim;
