/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_NETDEV_H_
#define _FBNIC_NETDEV_H_

#include <linux/types.h>
#include <linux/phylink.h>

#include "fbnic_csr.h"
#include "fbnic_rpc.h"
#include "fbnic_txrx.h"

#define FBNIC_MAX_NAPI_VECTORS		128u
#define FBNIC_MIN_RXD_PER_FRAME		2

/* Natively supported tunnel GSO features (not thru GSO_PARTIAL) */
#define FBNIC_TUN_GSO_FEATURES		NETIF_F_GSO_IPXIP6

struct fbnic_net {
	struct fbnic_ring *tx[FBNIC_MAX_TXQS];
	struct fbnic_ring *rx[FBNIC_MAX_RXQS];

	struct fbnic_napi_vector *napi[FBNIC_MAX_NAPI_VECTORS];

	struct net_device *netdev;
	struct fbnic_dev *fbd;

	u32 txq_size;
	u32 hpq_size;
	u32 ppq_size;
	u32 rcq_size;

	u16 rx_usecs;
	u16 tx_usecs;

	u32 rx_max_frames;

	u16 num_napi;

	struct phylink *phylink;
	struct phylink_config phylink_config;
	struct phylink_pcs phylink_pcs;

	/* TBD: Remove these when phylink supports FEC and lane config */
	u8 fec;
	u8 link_mode;

	/* Cached top bits of the HW time counter for 40b -> 64b conversion */
	u32 time_high;
	/* Protect readers of @time_offset, writers take @time_lock. */
	struct u64_stats_sync time_seq;
	/* Offset in ns between free running NIC PHC and time set via PTP
	 * clock callbacks
	 */
	s64 time_offset;

	u16 num_tx_queues;
	u16 num_rx_queues;

	u8 indir_tbl[FBNIC_RPC_RSS_TBL_COUNT][FBNIC_RPC_RSS_TBL_SIZE];
	u32 rss_key[FBNIC_RPC_RSS_KEY_DWORD_LEN];
	u32 rss_flow_hash[FBNIC_NUM_HASH_OPT];

	/* Storage for stats after ring destruction */
	struct fbnic_queue_stats tx_stats;
	struct fbnic_queue_stats rx_stats;
	u64 link_down_events;

	/* Time stampinn filter config */
	struct kernel_hwtstamp_config hwtstamp_config;
};

int __fbnic_open(struct fbnic_net *fbn);
void fbnic_up(struct fbnic_net *fbn);
void fbnic_down(struct fbnic_net *fbn);
void fbnic_down_noidle(struct fbnic_net *fbn);

struct net_device *fbnic_netdev_alloc(struct fbnic_dev *fbd);
void fbnic_netdev_free(struct fbnic_dev *fbd);
int fbnic_netdev_register(struct net_device *netdev);
void fbnic_netdev_unregister(struct net_device *netdev);
void fbnic_reset_queues(struct fbnic_net *fbn,
			unsigned int tx, unsigned int rx);
void fbnic_set_ethtool_ops(struct net_device *dev);

int fbnic_ptp_setup(struct fbnic_dev *fbd);
void fbnic_ptp_destroy(struct fbnic_dev *fbd);
void fbnic_time_init(struct fbnic_net *fbn);
int fbnic_time_start(struct fbnic_net *fbn);
void fbnic_time_stop(struct fbnic_net *fbn);

void __fbnic_set_rx_mode(struct net_device *netdev);
void fbnic_clear_rx_mode(struct net_device *netdev);

int fbnic_phylink_init(struct net_device *netdev);
#endif /* _FBNIC_NETDEV_H_ */
