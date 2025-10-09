// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/etherdevice.h>
#include <linux/ipv6.h>
#include <linux/types.h>
#include <net/netdev_queues.h>

#include "fbnic.h"
#include "fbnic_netdev.h"
#include "fbnic_txrx.h"

int __fbnic_open(struct fbnic_net *fbn)
{
	struct fbnic_dev *fbd = fbn->fbd;
	int err;

	err = fbnic_alloc_napi_vectors(fbn);
	if (err)
		return err;

	err = fbnic_alloc_resources(fbn);
	if (err)
		goto free_napi_vectors;

	err = fbnic_set_netif_queues(fbn);
	if (err)
		goto free_resources;

	/* Send ownership message and flush to verify FW has seen it */
	err = fbnic_fw_xmit_ownership_msg(fbd, true);
	if (err) {
		dev_warn(fbd->dev,
			 "Error %d sending host ownership message to the firmware\n",
			 err);
		goto err_reset_queues;
	}

	err = fbnic_time_start(fbn);
	if (err)
		goto release_ownership;

	err = fbnic_fw_init_heartbeat(fbd, false);
	if (err)
		goto time_stop;

	err = fbnic_pcs_request_irq(fbd);
	if (err)
		goto time_stop;

	/* Pull the BMC config and initialize the RPC */
	fbnic_bmc_rpc_init(fbd);
	fbnic_rss_reinit(fbd, fbn);

	phylink_resume(fbn->phylink);

	return 0;
time_stop:
	fbnic_time_stop(fbn);
release_ownership:
	fbnic_fw_xmit_ownership_msg(fbn->fbd, false);
err_reset_queues:
	fbnic_reset_netif_queues(fbn);
free_resources:
	fbnic_free_resources(fbn);
free_napi_vectors:
	fbnic_free_napi_vectors(fbn);
	return err;
}

static int fbnic_open(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	int err;

	fbnic_napi_name_irqs(fbn->fbd);

	err = __fbnic_open(fbn);
	if (!err)
		fbnic_up(fbn);

	return err;
}

static int fbnic_stop(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	phylink_suspend(fbn->phylink, fbnic_bmc_present(fbn->fbd));

	fbnic_down(fbn);
	fbnic_pcs_free_irq(fbn->fbd);

	fbnic_time_stop(fbn);
	fbnic_fw_xmit_ownership_msg(fbn->fbd, false);

	fbnic_reset_netif_queues(fbn);
	fbnic_free_resources(fbn);
	fbnic_free_napi_vectors(fbn);

	return 0;
}

static int fbnic_uc_sync(struct net_device *netdev, const unsigned char *addr)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_mac_addr *avail_addr;

	if (WARN_ON(!is_valid_ether_addr(addr)))
		return -EADDRNOTAVAIL;

	avail_addr = __fbnic_uc_sync(fbn->fbd, addr);
	if (!avail_addr)
		return -ENOSPC;

	/* Add type flag indicating this address is in use by the host */
	set_bit(FBNIC_MAC_ADDR_T_UNICAST, avail_addr->act_tcam);

	return 0;
}

static int fbnic_uc_unsync(struct net_device *netdev, const unsigned char *addr)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;
	int i, ret;

	/* Scan from middle of list to bottom, filling bottom up.
	 * Skip the first entry which is reserved for dev_addr and
	 * leave the last entry to use for promiscuous filtering.
	 */
	for (i = fbd->mac_addr_boundary, ret = -ENOENT;
	     i < FBNIC_RPC_TCAM_MACDA_HOST_ADDR_IDX && ret; i++) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		if (!ether_addr_equal(mac_addr->value.addr8, addr))
			continue;

		ret = __fbnic_uc_unsync(mac_addr);
	}

	return ret;
}

static int fbnic_mc_sync(struct net_device *netdev, const unsigned char *addr)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_mac_addr *avail_addr;

	if (WARN_ON(!is_multicast_ether_addr(addr)))
		return -EADDRNOTAVAIL;

	avail_addr = __fbnic_mc_sync(fbn->fbd, addr);
	if (!avail_addr)
		return -ENOSPC;

	/* Add type flag indicating this address is in use by the host */
	set_bit(FBNIC_MAC_ADDR_T_MULTICAST, avail_addr->act_tcam);

	return 0;
}

static int fbnic_mc_unsync(struct net_device *netdev, const unsigned char *addr)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;
	int i, ret;

	/* Scan from middle of list to top, filling top down.
	 * Skip over the address reserved for the BMC MAC and
	 * exclude index 0 as that belongs to the broadcast address
	 */
	for (i = fbd->mac_addr_boundary, ret = -ENOENT;
	     --i > FBNIC_RPC_TCAM_MACDA_BROADCAST_IDX && ret;) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		if (!ether_addr_equal(mac_addr->value.addr8, addr))
			continue;

		ret = __fbnic_mc_unsync(mac_addr);
	}

	return ret;
}

void __fbnic_set_rx_mode(struct fbnic_dev *fbd)
{
	bool uc_promisc = false, mc_promisc = false;
	struct net_device *netdev = fbd->netdev;
	struct fbnic_mac_addr *mac_addr;
	int err;

	/* Populate host address from dev_addr */
	mac_addr = &fbd->mac_addr[FBNIC_RPC_TCAM_MACDA_HOST_ADDR_IDX];
	if (!ether_addr_equal(mac_addr->value.addr8, netdev->dev_addr) ||
	    mac_addr->state != FBNIC_TCAM_S_VALID) {
		ether_addr_copy(mac_addr->value.addr8, netdev->dev_addr);
		mac_addr->state = FBNIC_TCAM_S_UPDATE;
		set_bit(FBNIC_MAC_ADDR_T_UNICAST, mac_addr->act_tcam);
	}

	/* Populate broadcast address if broadcast is enabled */
	mac_addr = &fbd->mac_addr[FBNIC_RPC_TCAM_MACDA_BROADCAST_IDX];
	if (netdev->flags & IFF_BROADCAST) {
		if (!is_broadcast_ether_addr(mac_addr->value.addr8) ||
		    mac_addr->state != FBNIC_TCAM_S_VALID) {
			eth_broadcast_addr(mac_addr->value.addr8);
			mac_addr->state = FBNIC_TCAM_S_ADD;
		}
		set_bit(FBNIC_MAC_ADDR_T_BROADCAST, mac_addr->act_tcam);
	} else if (mac_addr->state == FBNIC_TCAM_S_VALID) {
		__fbnic_xc_unsync(mac_addr, FBNIC_MAC_ADDR_T_BROADCAST);
	}

	/* Synchronize unicast and multicast address lists */
	err = __dev_uc_sync(netdev, fbnic_uc_sync, fbnic_uc_unsync);
	if (err == -ENOSPC)
		uc_promisc = true;
	err = __dev_mc_sync(netdev, fbnic_mc_sync, fbnic_mc_unsync);
	if (err == -ENOSPC)
		mc_promisc = true;

	uc_promisc |= !!(netdev->flags & IFF_PROMISC);
	mc_promisc |= !!(netdev->flags & IFF_ALLMULTI) || uc_promisc;

	/* Update the promiscuous rules */
	fbnic_promisc_sync(fbd, uc_promisc, mc_promisc);

	/* Add rules for BMC all multicast if it is enabled */
	fbnic_bmc_rpc_all_multi_config(fbd, mc_promisc);

	/* Sift out any unshared BMC rules and place them in BMC only section */
	fbnic_sift_macda(fbd);

	/* Write updates to hardware */
	fbnic_write_rules(fbd);
	fbnic_write_macda(fbd);
	fbnic_write_tce_tcam(fbd);
}

static void fbnic_set_rx_mode(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	/* No need to update the hardware if we are not running */
	if (netif_running(netdev))
		__fbnic_set_rx_mode(fbd);
}

static int fbnic_set_mac(struct net_device *netdev, void *p)
{
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(netdev, addr->sa_data);

	fbnic_set_rx_mode(netdev);

	return 0;
}

void fbnic_clear_rx_mode(struct fbnic_dev *fbd)
{
	struct net_device *netdev = fbd->netdev;
	int idx;

	for (idx = ARRAY_SIZE(fbd->mac_addr); idx--;) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[idx];

		if (mac_addr->state != FBNIC_TCAM_S_VALID)
			continue;

		bitmap_clear(mac_addr->act_tcam,
			     FBNIC_MAC_ADDR_T_HOST_START,
			     FBNIC_MAC_ADDR_T_HOST_LEN);

		if (bitmap_empty(mac_addr->act_tcam,
				 FBNIC_RPC_TCAM_ACT_NUM_ENTRIES))
			mac_addr->state = FBNIC_TCAM_S_DELETE;
	}

	/* Write updates to hardware */
	fbnic_write_macda(fbd);

	__dev_uc_unsync(netdev, NULL);
	__dev_mc_unsync(netdev, NULL);
}

static int fbnic_hwtstamp_get(struct net_device *netdev,
			      struct kernel_hwtstamp_config *config)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	*config = fbn->hwtstamp_config;

	return 0;
}

static int fbnic_hwtstamp_set(struct net_device *netdev,
			      struct kernel_hwtstamp_config *config,
			      struct netlink_ext_ack *extack)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	int old_rx_filter;

	if (config->source != HWTSTAMP_SOURCE_NETDEV)
		return -EOPNOTSUPP;

	if (!kernel_hwtstamp_config_changed(config, &fbn->hwtstamp_config))
		return 0;

	/* Upscale the filters */
	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
		break;
	case HWTSTAMP_FILTER_NTP_ALL:
		config->rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	default:
		return -ERANGE;
	}

	/* Configure */
	old_rx_filter = fbn->hwtstamp_config.rx_filter;
	memcpy(&fbn->hwtstamp_config, config, sizeof(*config));

	if (old_rx_filter != config->rx_filter && netif_running(fbn->netdev)) {
		fbnic_rss_reinit(fbn->fbd, fbn);
		fbnic_write_rules(fbn->fbd);
	}

	/* Save / report back filter configuration
	 * Note that our filter configuration is inexact. Instead of
	 * filtering for a specific UDP port or L2 Ethertype we are
	 * filtering in all UDP or all non-IP packets for timestamping. So
	 * if anything other than FILTER_ALL is requested we report
	 * FILTER_SOME indicating that we will be timestamping a few
	 * additional packets.
	 */
	if (config->rx_filter > HWTSTAMP_FILTER_ALL)
		config->rx_filter = HWTSTAMP_FILTER_SOME;

	return 0;
}

static void fbnic_get_stats64(struct net_device *dev,
			      struct rtnl_link_stats64 *stats64)
{
	u64 rx_bytes, rx_packets, rx_dropped = 0, rx_errors = 0;
	u64 rx_over = 0, rx_missed = 0, rx_length = 0;
	u64 tx_bytes, tx_packets, tx_dropped = 0;
	struct fbnic_net *fbn = netdev_priv(dev);
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_queue_stats *stats;

	unsigned int start, i;

	fbnic_get_hw_stats(fbd);

	stats = &fbn->tx_stats;

	tx_bytes = stats->bytes;
	tx_packets = stats->packets;
	tx_dropped = stats->dropped;

	/* Record drops from Tx HW Datapath */
	spin_lock(&fbd->hw_stats.lock);
	tx_dropped += fbd->hw_stats.tmi.drop.frames.value +
		      fbd->hw_stats.tti.cm_drop.frames.value +
		      fbd->hw_stats.tti.frame_drop.frames.value +
		      fbd->hw_stats.tti.tbi_drop.frames.value;
	spin_unlock(&fbd->hw_stats.lock);

	stats64->tx_bytes = tx_bytes;
	stats64->tx_packets = tx_packets;
	stats64->tx_dropped = tx_dropped;

	for (i = 0; i < fbn->num_tx_queues; i++) {
		struct fbnic_ring *txr = fbn->tx[i];

		if (!txr)
			continue;

		stats = &txr->stats;
		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			tx_bytes = stats->bytes;
			tx_packets = stats->packets;
			tx_dropped = stats->dropped;
		} while (u64_stats_fetch_retry(&stats->syncp, start));

		stats64->tx_bytes += tx_bytes;
		stats64->tx_packets += tx_packets;
		stats64->tx_dropped += tx_dropped;
	}

	stats = &fbn->rx_stats;

	rx_bytes = stats->bytes;
	rx_packets = stats->packets;
	rx_dropped = stats->dropped;

	spin_lock(&fbd->hw_stats.lock);
	/* Record drops for the host FIFOs.
	 * 4: network to Host,	6: BMC to Host
	 * Exclude the BMC and MC FIFOs as those stats may contain drops
	 * due to unrelated items such as TCAM misses. They are still
	 * accessible through the ethtool stats.
	 */
	i = FBNIC_RXB_FIFO_HOST;
	rx_missed += fbd->hw_stats.rxb.fifo[i].drop.frames.value;
	i = FBNIC_RXB_FIFO_BMC_TO_HOST;
	rx_missed += fbd->hw_stats.rxb.fifo[i].drop.frames.value;

	for (i = 0; i < fbd->max_num_queues; i++) {
		/* Report packets dropped due to CQ/BDQ being full/empty */
		rx_over += fbd->hw_stats.hw_q[i].rde_pkt_cq_drop.value;
		rx_over += fbd->hw_stats.hw_q[i].rde_pkt_bdq_drop.value;

		/* Report packets with errors */
		rx_errors += fbd->hw_stats.hw_q[i].rde_pkt_err.value;
	}
	spin_unlock(&fbd->hw_stats.lock);

	stats64->rx_bytes = rx_bytes;
	stats64->rx_packets = rx_packets;
	stats64->rx_dropped = rx_dropped;
	stats64->rx_over_errors = rx_over;
	stats64->rx_errors = rx_errors;
	stats64->rx_missed_errors = rx_missed;

	for (i = 0; i < fbn->num_rx_queues; i++) {
		struct fbnic_ring *xdpr = fbn->tx[FBNIC_MAX_TXQS + i];
		struct fbnic_ring *rxr = fbn->rx[i];

		if (!rxr)
			continue;

		stats = &rxr->stats;
		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			rx_bytes = stats->bytes;
			rx_packets = stats->packets;
			rx_dropped = stats->dropped;
			rx_length = stats->rx.length_errors;
		} while (u64_stats_fetch_retry(&stats->syncp, start));

		stats64->rx_bytes += rx_bytes;
		stats64->rx_packets += rx_packets;
		stats64->rx_dropped += rx_dropped;
		stats64->rx_errors += rx_length;
		stats64->rx_length_errors += rx_length;

		if (!xdpr)
			continue;

		stats = &xdpr->stats;
		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			tx_bytes = stats->bytes;
			tx_packets = stats->packets;
			tx_dropped = stats->dropped;
		} while (u64_stats_fetch_retry(&stats->syncp, start));

		stats64->tx_bytes += tx_bytes;
		stats64->tx_packets += tx_packets;
		stats64->tx_dropped += tx_dropped;
	}
}

bool fbnic_check_split_frames(struct bpf_prog *prog, unsigned int mtu,
			      u32 hds_thresh)
{
	if (!prog)
		return false;

	if (prog->aux->xdp_has_frags)
		return false;

	return mtu + ETH_HLEN > hds_thresh;
}

static int fbnic_bpf(struct net_device *netdev, struct netdev_bpf *bpf)
{
	struct bpf_prog *prog = bpf->prog, *prev_prog;
	struct fbnic_net *fbn = netdev_priv(netdev);

	if (bpf->command != XDP_SETUP_PROG)
		return -EINVAL;

	if (fbnic_check_split_frames(prog, netdev->mtu,
				     fbn->hds_thresh)) {
		NL_SET_ERR_MSG_MOD(bpf->extack,
				   "MTU too high, or HDS threshold is too low for single buffer XDP");
		return -EOPNOTSUPP;
	}

	prev_prog = xchg(&fbn->xdp_prog, prog);
	if (prev_prog)
		bpf_prog_put(prev_prog);

	return 0;
}

static const struct net_device_ops fbnic_netdev_ops = {
	.ndo_open		= fbnic_open,
	.ndo_stop		= fbnic_stop,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_start_xmit		= fbnic_xmit_frame,
	.ndo_features_check	= fbnic_features_check,
	.ndo_set_mac_address	= fbnic_set_mac,
	.ndo_set_rx_mode	= fbnic_set_rx_mode,
	.ndo_get_stats64	= fbnic_get_stats64,
	.ndo_bpf		= fbnic_bpf,
	.ndo_hwtstamp_get	= fbnic_hwtstamp_get,
	.ndo_hwtstamp_set	= fbnic_hwtstamp_set,
};

static void fbnic_get_queue_stats_rx(struct net_device *dev, int idx,
				     struct netdev_queue_stats_rx *rx)
{
	u64 bytes, packets, alloc_fail, alloc_fail_bdq;
	struct fbnic_net *fbn = netdev_priv(dev);
	struct fbnic_ring *rxr = fbn->rx[idx];
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_queue_stats *stats;
	u64 csum_complete, csum_none;
	struct fbnic_q_triad *qt;
	unsigned int start;

	if (!rxr)
		return;

	/* fbn->rx points to completion queues */
	qt = container_of(rxr, struct fbnic_q_triad, cmpl);

	stats = &rxr->stats;
	do {
		start = u64_stats_fetch_begin(&stats->syncp);
		bytes = stats->bytes;
		packets = stats->packets;
		alloc_fail = stats->rx.alloc_failed;
		csum_complete = stats->rx.csum_complete;
		csum_none = stats->rx.csum_none;
	} while (u64_stats_fetch_retry(&stats->syncp, start));

	stats = &qt->sub0.stats;
	do {
		start = u64_stats_fetch_begin(&stats->syncp);
		alloc_fail_bdq = stats->bdq.alloc_failed;
	} while (u64_stats_fetch_retry(&stats->syncp, start));
	alloc_fail += alloc_fail_bdq;

	stats = &qt->sub1.stats;
	do {
		start = u64_stats_fetch_begin(&stats->syncp);
		alloc_fail_bdq = stats->bdq.alloc_failed;
	} while (u64_stats_fetch_retry(&stats->syncp, start));
	alloc_fail += alloc_fail_bdq;

	rx->bytes = bytes;
	rx->packets = packets;
	rx->alloc_fail = alloc_fail;
	rx->csum_complete = csum_complete;
	rx->csum_none = csum_none;

	fbnic_get_hw_q_stats(fbd, fbd->hw_stats.hw_q);

	spin_lock(&fbd->hw_stats.lock);
	rx->hw_drop_overruns = fbd->hw_stats.hw_q[idx].rde_pkt_cq_drop.value +
			       fbd->hw_stats.hw_q[idx].rde_pkt_bdq_drop.value;
	rx->hw_drops = fbd->hw_stats.hw_q[idx].rde_pkt_err.value +
		       rx->hw_drop_overruns;
	spin_unlock(&fbd->hw_stats.lock);
}

static void fbnic_get_queue_stats_tx(struct net_device *dev, int idx,
				     struct netdev_queue_stats_tx *tx)
{
	struct fbnic_net *fbn = netdev_priv(dev);
	struct fbnic_ring *txr = fbn->tx[idx];
	struct fbnic_queue_stats *stats;
	u64 stop, wake, csum, lso;
	struct fbnic_ring *xdpr;
	unsigned int start;
	u64 bytes, packets;

	if (!txr)
		return;

	stats = &txr->stats;
	do {
		start = u64_stats_fetch_begin(&stats->syncp);
		bytes = stats->bytes;
		packets = stats->packets;
		csum = stats->twq.csum_partial;
		lso = stats->twq.lso;
		stop = stats->twq.stop;
		wake = stats->twq.wake;
	} while (u64_stats_fetch_retry(&stats->syncp, start));

	tx->bytes = bytes;
	tx->packets = packets;
	tx->needs_csum = csum + lso;
	tx->hw_gso_wire_packets = lso;
	tx->stop = stop;
	tx->wake = wake;

	xdpr = fbn->tx[FBNIC_MAX_TXQS + idx];
	if (xdpr) {
		stats = &xdpr->stats;
		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			bytes = stats->bytes;
			packets = stats->packets;
		} while (u64_stats_fetch_retry(&stats->syncp, start));

		tx->bytes += bytes;
		tx->packets += packets;
	}
}

static void fbnic_get_base_stats(struct net_device *dev,
				 struct netdev_queue_stats_rx *rx,
				 struct netdev_queue_stats_tx *tx)
{
	struct fbnic_net *fbn = netdev_priv(dev);

	tx->bytes = fbn->tx_stats.bytes;
	tx->packets = fbn->tx_stats.packets;
	tx->needs_csum = fbn->tx_stats.twq.csum_partial + fbn->tx_stats.twq.lso;
	tx->hw_gso_wire_packets = fbn->tx_stats.twq.lso;
	tx->stop = fbn->tx_stats.twq.stop;
	tx->wake = fbn->tx_stats.twq.wake;

	rx->bytes = fbn->rx_stats.bytes;
	rx->packets = fbn->rx_stats.packets;
	rx->alloc_fail = fbn->rx_stats.rx.alloc_failed +
		fbn->bdq_stats.bdq.alloc_failed;
	rx->csum_complete = fbn->rx_stats.rx.csum_complete;
	rx->csum_none = fbn->rx_stats.rx.csum_none;
}

static const struct netdev_stat_ops fbnic_stat_ops = {
	.get_queue_stats_rx	= fbnic_get_queue_stats_rx,
	.get_queue_stats_tx	= fbnic_get_queue_stats_tx,
	.get_base_stats		= fbnic_get_base_stats,
};

void fbnic_reset_queues(struct fbnic_net *fbn,
			unsigned int tx, unsigned int rx)
{
	struct fbnic_dev *fbd = fbn->fbd;
	unsigned int max_napis;

	max_napis = fbd->num_irqs - FBNIC_NON_NAPI_VECTORS;

	tx = min(tx, max_napis);
	fbn->num_tx_queues = tx;

	rx = min(rx, max_napis);
	fbn->num_rx_queues = rx;

	fbn->num_napi = max(tx, rx);
}

/**
 * fbnic_netdev_free - Free the netdev associate with fbnic
 * @fbd: Driver specific structure to free netdev from
 *
 * Allocate and initialize the netdev and netdev private structure. Bind
 * together the hardware, netdev, and pci data structures.
 **/
void fbnic_netdev_free(struct fbnic_dev *fbd)
{
	struct fbnic_net *fbn = netdev_priv(fbd->netdev);

	if (fbn->phylink)
		phylink_destroy(fbn->phylink);

	free_netdev(fbd->netdev);
	fbd->netdev = NULL;
}

/**
 * fbnic_netdev_alloc - Allocate a netdev and associate with fbnic
 * @fbd: Driver specific structure to associate netdev with
 *
 * Allocate and initialize the netdev and netdev private structure. Bind
 * together the hardware, netdev, and pci data structures.
 *
 *  Return: Pointer to net_device on success, NULL on failure
 **/
struct net_device *fbnic_netdev_alloc(struct fbnic_dev *fbd)
{
	struct net_device *netdev;
	struct fbnic_net *fbn;
	int default_queues;

	netdev = alloc_etherdev_mq(sizeof(*fbn), FBNIC_MAX_RXQS);
	if (!netdev)
		return NULL;

	SET_NETDEV_DEV(netdev, fbd->dev);
	fbd->netdev = netdev;

	netdev->netdev_ops = &fbnic_netdev_ops;
	netdev->stat_ops = &fbnic_stat_ops;
	netdev->queue_mgmt_ops = &fbnic_queue_mgmt_ops;
	netdev->netmem_tx = true;

	fbnic_set_ethtool_ops(netdev);

	fbn = netdev_priv(netdev);

	fbn->netdev = netdev;
	fbn->fbd = fbd;

	fbn->txq_size = FBNIC_TXQ_SIZE_DEFAULT;
	fbn->hpq_size = FBNIC_HPQ_SIZE_DEFAULT;
	fbn->ppq_size = FBNIC_PPQ_SIZE_DEFAULT;
	fbn->rcq_size = FBNIC_RCQ_SIZE_DEFAULT;

	fbn->tx_usecs = FBNIC_TX_USECS_DEFAULT;
	fbn->rx_usecs = FBNIC_RX_USECS_DEFAULT;
	fbn->rx_max_frames = FBNIC_RX_FRAMES_DEFAULT;

	/* Initialize the hds_thresh */
	netdev->cfg->hds_thresh = FBNIC_HDS_THRESH_DEFAULT;
	fbn->hds_thresh = FBNIC_HDS_THRESH_DEFAULT;

	default_queues = netif_get_num_default_rss_queues();
	if (default_queues > fbd->max_num_queues)
		default_queues = fbd->max_num_queues;

	fbnic_reset_queues(fbn, default_queues, default_queues);

	fbnic_reset_indir_tbl(fbn);
	fbnic_rss_key_fill(fbn->rss_key);
	fbnic_rss_init_en_mask(fbn);

	netdev->priv_flags |= IFF_UNICAST_FLT;

	netdev->gso_partial_features =
		NETIF_F_GSO_GRE |
		NETIF_F_GSO_GRE_CSUM |
		NETIF_F_GSO_IPXIP4 |
		NETIF_F_GSO_UDP_TUNNEL |
		NETIF_F_GSO_UDP_TUNNEL_CSUM;

	netdev->features |=
		netdev->gso_partial_features |
		FBNIC_TUN_GSO_FEATURES |
		NETIF_F_RXHASH |
		NETIF_F_SG |
		NETIF_F_HW_CSUM |
		NETIF_F_RXCSUM |
		NETIF_F_TSO |
		NETIF_F_TSO_ECN |
		NETIF_F_TSO6 |
		NETIF_F_GSO_PARTIAL |
		NETIF_F_GSO_UDP_L4;

	netdev->hw_features |= netdev->features;
	netdev->vlan_features |= netdev->features;
	netdev->hw_enc_features |= netdev->features;
	netdev->features |= NETIF_F_NTUPLE;

	netdev->min_mtu = IPV6_MIN_MTU;
	netdev->max_mtu = FBNIC_MAX_JUMBO_FRAME_SIZE - ETH_HLEN;

	/* TBD: This is workaround for BMC as phylink doesn't have support
	 * for leavling the link enabled if a BMC is present.
	 */
	netdev->ethtool->wol_enabled = true;

	netif_carrier_off(netdev);

	netif_tx_stop_all_queues(netdev);

	if (fbnic_phylink_init(netdev)) {
		fbnic_netdev_free(fbd);
		return NULL;
	}

	return netdev;
}

static int fbnic_dsn_to_mac_addr(u64 dsn, char *addr)
{
	addr[0] = (dsn >> 56) & 0xFF;
	addr[1] = (dsn >> 48) & 0xFF;
	addr[2] = (dsn >> 40) & 0xFF;
	addr[3] = (dsn >> 16) & 0xFF;
	addr[4] = (dsn >> 8) & 0xFF;
	addr[5] = dsn & 0xFF;

	return is_valid_ether_addr(addr) ? 0 : -EINVAL;
}

/**
 * fbnic_netdev_register - Initialize general software structures
 * @netdev: Netdev containing structure to initialize and register
 *
 * Initialize the MAC address for the netdev and register it.
 *
 *  Return: 0 on success, negative on failure
 **/
int fbnic_netdev_register(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;
	u64 dsn = fbd->dsn;
	u8 addr[ETH_ALEN];
	int err;

	err = fbnic_dsn_to_mac_addr(dsn, addr);
	if (!err) {
		ether_addr_copy(netdev->perm_addr, addr);
		eth_hw_addr_set(netdev, addr);
	} else {
		/* A randomly assigned MAC address will cause provisioning
		 * issues so instead just fail to spawn the netdev and
		 * avoid any confusion.
		 */
		dev_err(fbd->dev, "MAC addr %pM invalid\n", addr);
		return err;
	}

	return register_netdev(netdev);
}

void fbnic_netdev_unregister(struct net_device *netdev)
{
	unregister_netdev(netdev);
}
