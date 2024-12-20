// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/pci.h>

#include "fbnic.h"
#include "fbnic_netdev.h"
#include "fbnic_tlv.h"

struct fbnic_stat {
	u8 string[ETH_GSTRING_LEN];
	unsigned int size;
	unsigned int offset;
};

#define FBNIC_STAT_FIELDS(type, name, stat) { \
	.string = name, \
	.size = sizeof_field(struct type, stat), \
	.offset = offsetof(struct type, stat), \
}

/* Hardware statistics not captured in rtnl_link_stats */
#define FBNIC_HW_STAT(name, stat) \
	FBNIC_STAT_FIELDS(fbnic_hw_stats, name, stat)

static const struct fbnic_stat fbnic_gstrings_hw_stats[] = {
	/* RPC */
	FBNIC_HW_STAT("rpc_unkn_etype", rpc.unkn_etype),
	FBNIC_HW_STAT("rpc_unkn_ext_hdr", rpc.unkn_ext_hdr),
	FBNIC_HW_STAT("rpc_ipv4_frag", rpc.ipv4_frag),
	FBNIC_HW_STAT("rpc_ipv6_frag", rpc.ipv6_frag),
	FBNIC_HW_STAT("rpc_ipv4_esp", rpc.ipv4_esp),
	FBNIC_HW_STAT("rpc_ipv6_esp", rpc.ipv6_esp),
	FBNIC_HW_STAT("rpc_tcp_opt_err", rpc.tcp_opt_err),
	FBNIC_HW_STAT("rpc_out_of_hdr_err", rpc.out_of_hdr_err),
};

#define FBNIC_HW_FIXED_STATS_LEN ARRAY_SIZE(fbnic_gstrings_hw_stats)
#define FBNIC_HW_STATS_LEN	FBNIC_HW_FIXED_STATS_LEN

static void
fbnic_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	fbnic_get_fw_ver_commit_str(fbd, drvinfo->fw_version,
				    sizeof(drvinfo->fw_version));
}

static int fbnic_get_regs_len(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	return fbnic_csr_regs_len(fbn->fbd) * sizeof(u32);
}

static void fbnic_get_regs(struct net_device *netdev,
			   struct ethtool_regs *regs, void *data)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	fbnic_csr_get_regs(fbn->fbd, data, &regs->version);
}

static struct fbnic_net *fbnic_clone_create(struct fbnic_net *orig)
{
	struct fbnic_net *clone;

	clone = kmemdup(orig, sizeof(*orig), GFP_KERNEL);
	if (!clone)
		return NULL;

	memset(clone->tx, 0, sizeof(clone->tx));
	memset(clone->rx, 0, sizeof(clone->rx));
	memset(clone->napi, 0, sizeof(clone->napi));
	return clone;
}

static void fbnic_clone_swap_cfg(struct fbnic_net *orig,
				 struct fbnic_net *clone)
{
	swap(clone->rcq_size, orig->rcq_size);
	swap(clone->hpq_size, orig->hpq_size);
	swap(clone->ppq_size, orig->ppq_size);
	swap(clone->txq_size, orig->txq_size);
	swap(clone->num_rx_queues, orig->num_rx_queues);
	swap(clone->num_tx_queues, orig->num_tx_queues);
	swap(clone->num_napi, orig->num_napi);
}

static void fbnic_aggregate_vector_counters(struct fbnic_net *fbn,
					    struct fbnic_napi_vector *nv)
{
	int i, j;

	for (i = 0; i < nv->txt_count; i++) {
		fbnic_aggregate_ring_tx_counters(fbn, &nv->qt[i].sub0);
		fbnic_aggregate_ring_tx_counters(fbn, &nv->qt[i].sub1);
		fbnic_aggregate_ring_tx_counters(fbn, &nv->qt[i].cmpl);
	}

	for (j = 0; j < nv->rxt_count; j++, i++) {
		fbnic_aggregate_ring_rx_counters(fbn, &nv->qt[i].sub0);
		fbnic_aggregate_ring_rx_counters(fbn, &nv->qt[i].sub1);
		fbnic_aggregate_ring_rx_counters(fbn, &nv->qt[i].cmpl);
	}
}

static void fbnic_clone_swap(struct fbnic_net *orig,
			     struct fbnic_net *clone)
{
	struct fbnic_dev *fbd = orig->fbd;
	unsigned int i;

	for (i = 0; i < max(clone->num_napi, orig->num_napi); i++)
		fbnic_synchronize_irq(fbd, FBNIC_NON_NAPI_VECTORS + i);
	for (i = 0; i < orig->num_napi; i++)
		fbnic_aggregate_vector_counters(orig, orig->napi[i]);

	fbnic_clone_swap_cfg(orig, clone);

	for (i = 0; i < ARRAY_SIZE(orig->napi); i++)
		swap(clone->napi[i], orig->napi[i]);
	for (i = 0; i < ARRAY_SIZE(orig->tx); i++)
		swap(clone->tx[i], orig->tx[i]);
	for (i = 0; i < ARRAY_SIZE(orig->rx); i++)
		swap(clone->rx[i], orig->rx[i]);
}

static void fbnic_clone_free(struct fbnic_net *clone)
{
	kfree(clone);
}

static void fbnic_get_strings(struct net_device *dev, u32 sset, u8 *data)
{
	int i;

	switch (sset) {
	case ETH_SS_STATS:
		for (i = 0; i < FBNIC_HW_STATS_LEN; i++)
			ethtool_puts(&data, fbnic_gstrings_hw_stats[i].string);
		break;
	}
}

static void fbnic_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct fbnic_net *fbn = netdev_priv(dev);
	const struct fbnic_stat *stat;
	int i;

	fbnic_get_hw_stats(fbn->fbd);

	for (i = 0; i < FBNIC_HW_STATS_LEN; i++) {
		stat = &fbnic_gstrings_hw_stats[i];
		data[i] = *(u64 *)((u8 *)&fbn->fbd->hw_stats + stat->offset);
	}
}

static int fbnic_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return FBNIC_HW_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static int fbnic_get_rss_hash_idx(u32 flow_type)
{
	switch (flow_type & ~(FLOW_EXT | FLOW_MAC_EXT | FLOW_RSS)) {
	case TCP_V4_FLOW:
		return FBNIC_TCP4_HASH_OPT;
	case TCP_V6_FLOW:
		return FBNIC_TCP6_HASH_OPT;
	case UDP_V4_FLOW:
		return FBNIC_UDP4_HASH_OPT;
	case UDP_V6_FLOW:
		return FBNIC_UDP6_HASH_OPT;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case SCTP_V4_FLOW:
	case IPV4_FLOW:
	case IPV4_USER_FLOW:
		return FBNIC_IPV4_HASH_OPT;
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case SCTP_V6_FLOW:
	case IPV6_FLOW:
	case IPV6_USER_FLOW:
		return FBNIC_IPV6_HASH_OPT;
	case ETHER_FLOW:
		return FBNIC_ETHER_HASH_OPT;
	}

	return -1;
}

static int
fbnic_get_rss_hash_opts(struct fbnic_net *fbn, struct ethtool_rxnfc *cmd)
{
	int hash_opt_idx = fbnic_get_rss_hash_idx(cmd->flow_type);

	if (hash_opt_idx < 0)
		return -EINVAL;

	/* Report options from rss_en table in fbn */
	cmd->data = fbn->rss_flow_hash[hash_opt_idx];

	return 0;
}

static int fbnic_get_rxnfc(struct net_device *netdev,
			   struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = fbn->num_rx_queues;
		ret = 0;
		break;
	case ETHTOOL_GRXFH:
		ret = fbnic_get_rss_hash_opts(fbn, cmd);
		break;
	}

	return ret;
}

#define FBNIC_L2_HASH_OPTIONS \
	(RXH_L2DA | RXH_DISCARD)
#define FBNIC_L3_HASH_OPTIONS \
	(FBNIC_L2_HASH_OPTIONS | RXH_IP_SRC | RXH_IP_DST)
#define FBNIC_L4_HASH_OPTIONS \
	(FBNIC_L3_HASH_OPTIONS | RXH_L4_B_0_1 | RXH_L4_B_2_3)

static int
fbnic_set_rss_hash_opts(struct fbnic_net *fbn, const struct ethtool_rxnfc *cmd)
{
	int hash_opt_idx;

	/* Verify the type requested is correct */
	hash_opt_idx = fbnic_get_rss_hash_idx(cmd->flow_type);
	if (hash_opt_idx < 0)
		return -EINVAL;

	/* Verify the fields asked for can actually be assigned based on type */
	if (cmd->data & ~FBNIC_L4_HASH_OPTIONS ||
	    (hash_opt_idx > FBNIC_L4_HASH_OPT &&
	     cmd->data & ~FBNIC_L3_HASH_OPTIONS) ||
	    (hash_opt_idx > FBNIC_IP_HASH_OPT &&
	     cmd->data & ~FBNIC_L2_HASH_OPTIONS))
		return -EINVAL;

	fbn->rss_flow_hash[hash_opt_idx] = cmd->data;

	if (netif_running(fbn->netdev)) {
		fbnic_rss_reinit(fbn->fbd, fbn);
		fbnic_write_rules(fbn->fbd);
	}

	return 0;
}

static int fbnic_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		ret = fbnic_set_rss_hash_opts(fbn, cmd);
		break;
	}

	return ret;
}

static u32 fbnic_get_rxfh_key_size(struct net_device *netdev)
{
	return FBNIC_RPC_RSS_KEY_BYTE_LEN;
}

static u32 fbnic_get_rxfh_indir_size(struct net_device *netdev)
{
	return FBNIC_RPC_RSS_TBL_SIZE;
}

static int
fbnic_get_rxfh(struct net_device *netdev, struct ethtool_rxfh_param *rxfh)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	unsigned int i;

	rxfh->hfunc = ETH_RSS_HASH_TOP;

	if (rxfh->key) {
		for (i = 0; i < FBNIC_RPC_RSS_KEY_BYTE_LEN; i++) {
			u32 rss_key = fbn->rss_key[i / 4] << ((i % 4) * 8);

			rxfh->key[i] = rss_key >> 24;
		}
	}

	if (rxfh->indir) {
		for (i = 0; i < FBNIC_RPC_RSS_TBL_SIZE; i++)
			rxfh->indir[i] = fbn->indir_tbl[0][i];
	}

	return 0;
}

static unsigned int
fbnic_set_indir(struct fbnic_net *fbn, unsigned int idx, const u32 *indir)
{
	unsigned int i, changes = 0;

	for (i = 0; i < FBNIC_RPC_RSS_TBL_SIZE; i++) {
		if (fbn->indir_tbl[idx][i] == indir[i])
			continue;

		fbn->indir_tbl[idx][i] = indir[i];
		changes++;
	}

	return changes;
}

static int
fbnic_set_rxfh(struct net_device *netdev, struct ethtool_rxfh_param *rxfh,
	       struct netlink_ext_ack *extack)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	unsigned int i, changes = 0;

	if (rxfh->hfunc != ETH_RSS_HASH_NO_CHANGE &&
	    rxfh->hfunc != ETH_RSS_HASH_TOP)
		return -EINVAL;

	if (rxfh->key) {
		u32 rss_key = 0;

		for (i = FBNIC_RPC_RSS_KEY_BYTE_LEN; i--;) {
			rss_key >>= 8;
			rss_key |= (u32)(rxfh->key[i]) << 24;

			if (i % 4)
				continue;

			if (fbn->rss_key[i / 4] == rss_key)
				continue;

			fbn->rss_key[i / 4] = rss_key;
			changes++;
		}
	}

	if (rxfh->indir)
		changes += fbnic_set_indir(fbn, 0, rxfh->indir);

	if (changes && netif_running(netdev))
		fbnic_rss_reinit_hw(fbn->fbd, fbn);

	return 0;
}

static void fbnic_get_channels(struct net_device *netdev,
			       struct ethtool_channels *ch)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	ch->max_rx = fbd->max_num_queues;
	ch->max_tx = fbd->max_num_queues;
	ch->max_combined = min(ch->max_rx, ch->max_tx);
	ch->max_other =	FBNIC_NON_NAPI_VECTORS;

	if (fbn->num_rx_queues > fbn->num_napi ||
	    fbn->num_tx_queues > fbn->num_napi)
		ch->combined_count = min(fbn->num_rx_queues,
					 fbn->num_tx_queues);
	else
		ch->combined_count =
			fbn->num_rx_queues + fbn->num_tx_queues - fbn->num_napi;
	ch->rx_count = fbn->num_rx_queues - ch->combined_count;
	ch->tx_count = fbn->num_tx_queues - ch->combined_count;
	ch->other_count = FBNIC_NON_NAPI_VECTORS;
}

static void fbnic_set_queues(struct fbnic_net *fbn, struct ethtool_channels *ch,
			     unsigned int max_napis)
{
	fbn->num_rx_queues = ch->rx_count + ch->combined_count;
	fbn->num_tx_queues = ch->tx_count + ch->combined_count;
	fbn->num_napi = min(ch->rx_count + ch->tx_count + ch->combined_count,
			    max_napis);
}

static int fbnic_set_channels(struct net_device *netdev,
			      struct ethtool_channels *ch)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	unsigned int max_napis, standalone;
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_net *clone;
	int err;

	max_napis = fbd->num_irqs - FBNIC_NON_NAPI_VECTORS;
	standalone = ch->rx_count + ch->tx_count;

	/* Limits for standalone queues:
	 *  - each queue has it's own NAPI (num_napi >= rx + tx + combined)
	 *  - combining queues (combined not 0, rx or tx must be 0)
	 */
	if ((ch->rx_count && ch->tx_count && ch->combined_count) ||
	    (standalone && standalone + ch->combined_count > max_napis) ||
	    ch->rx_count + ch->combined_count > fbd->max_num_queues ||
	    ch->tx_count + ch->combined_count > fbd->max_num_queues ||
	    ch->other_count != FBNIC_NON_NAPI_VECTORS)
		return -EINVAL;

	if (!netif_running(netdev)) {
		fbnic_set_queues(fbn, ch, max_napis);
		fbnic_reset_indir_tbl(fbn);
		return 0;
	}

	clone = fbnic_clone_create(fbn);
	if (!clone)
		return -ENOMEM;

	fbnic_set_queues(clone, ch, max_napis);

	err = fbnic_alloc_napi_vectors(clone);
	if (err)
		goto err_free_clone;

	err = fbnic_alloc_resources(clone);
	if (err)
		goto err_free_napis;

	fbnic_down_noidle(fbn);
	err = fbnic_wait_all_queues_idle(fbn->fbd, true);
	if (err)
		goto err_start_stack;

	err = fbnic_set_netif_queues(clone);
	if (err)
		goto err_start_stack;

	/* Nothing can fail past this point */
	fbnic_flush(fbn);

	fbnic_clone_swap(fbn, clone);

	/* Reset RSS indirection table */
	fbnic_reset_indir_tbl(fbn);

	fbnic_up(fbn);

	fbnic_free_resources(clone);
	fbnic_free_napi_vectors(clone);
	fbnic_clone_free(clone);

	return 0;

err_start_stack:
	fbnic_flush(fbn);
	fbnic_up(fbn);
	fbnic_free_resources(clone);
err_free_napis:
	fbnic_free_napi_vectors(clone);
err_free_clone:
	fbnic_clone_free(clone);
	return err;
}

static int
fbnic_get_ts_info(struct net_device *netdev,
		  struct kernel_ethtool_ts_info *tsinfo)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	tsinfo->phc_index = ptp_clock_index(fbn->fbd->ptp);

	tsinfo->so_timestamping =
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;

	tsinfo->tx_types =
		BIT(HWTSTAMP_TX_OFF) |
		BIT(HWTSTAMP_TX_ON);

	tsinfo->rx_filters =
		BIT(HWTSTAMP_FILTER_NONE) |
		BIT(HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
		BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
		BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
		BIT(HWTSTAMP_FILTER_PTP_V2_EVENT) |
		BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

static void fbnic_get_ts_stats(struct net_device *netdev,
			       struct ethtool_ts_stats *ts_stats)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	u64 ts_packets, ts_lost;
	struct fbnic_ring *ring;
	unsigned int start;
	int i;

	ts_stats->pkts = fbn->tx_stats.ts_packets;
	ts_stats->lost = fbn->tx_stats.ts_lost;
	for (i = 0; i < fbn->num_tx_queues; i++) {
		ring = fbn->tx[i];
		do {
			start = u64_stats_fetch_begin(&ring->stats.syncp);
			ts_packets = ring->stats.ts_packets;
			ts_lost = ring->stats.ts_lost;
		} while (u64_stats_fetch_retry(&ring->stats.syncp, start));
		ts_stats->pkts += ts_packets;
		ts_stats->lost += ts_lost;
	}
}

static void fbnic_set_counter(u64 *stat, struct fbnic_stat_counter *counter)
{
	if (counter->reported)
		*stat = counter->value;
}

static void
fbnic_get_eth_mac_stats(struct net_device *netdev,
			struct ethtool_eth_mac_stats *eth_mac_stats)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_mac_stats *mac_stats;
	struct fbnic_dev *fbd = fbn->fbd;
	const struct fbnic_mac *mac;

	mac_stats = &fbd->hw_stats.mac;
	mac = fbd->mac;

	mac->get_eth_mac_stats(fbd, false, &mac_stats->eth_mac);

	fbnic_set_counter(&eth_mac_stats->FramesTransmittedOK,
			  &mac_stats->eth_mac.FramesTransmittedOK);
	fbnic_set_counter(&eth_mac_stats->FramesReceivedOK,
			  &mac_stats->eth_mac.FramesReceivedOK);
	fbnic_set_counter(&eth_mac_stats->FrameCheckSequenceErrors,
			  &mac_stats->eth_mac.FrameCheckSequenceErrors);
	fbnic_set_counter(&eth_mac_stats->AlignmentErrors,
			  &mac_stats->eth_mac.AlignmentErrors);
	fbnic_set_counter(&eth_mac_stats->OctetsTransmittedOK,
			  &mac_stats->eth_mac.OctetsTransmittedOK);
	fbnic_set_counter(&eth_mac_stats->FramesLostDueToIntMACXmitError,
			  &mac_stats->eth_mac.FramesLostDueToIntMACXmitError);
	fbnic_set_counter(&eth_mac_stats->OctetsReceivedOK,
			  &mac_stats->eth_mac.OctetsReceivedOK);
	fbnic_set_counter(&eth_mac_stats->FramesLostDueToIntMACRcvError,
			  &mac_stats->eth_mac.FramesLostDueToIntMACRcvError);
	fbnic_set_counter(&eth_mac_stats->MulticastFramesXmittedOK,
			  &mac_stats->eth_mac.MulticastFramesXmittedOK);
	fbnic_set_counter(&eth_mac_stats->BroadcastFramesXmittedOK,
			  &mac_stats->eth_mac.BroadcastFramesXmittedOK);
	fbnic_set_counter(&eth_mac_stats->MulticastFramesReceivedOK,
			  &mac_stats->eth_mac.MulticastFramesReceivedOK);
	fbnic_set_counter(&eth_mac_stats->BroadcastFramesReceivedOK,
			  &mac_stats->eth_mac.BroadcastFramesReceivedOK);
	fbnic_set_counter(&eth_mac_stats->FrameTooLongErrors,
			  &mac_stats->eth_mac.FrameTooLongErrors);
}

static const struct ethtool_ops fbnic_ethtool_ops = {
	.get_drvinfo		= fbnic_get_drvinfo,
	.get_regs_len		= fbnic_get_regs_len,
	.get_regs		= fbnic_get_regs,
	.get_strings		= fbnic_get_strings,
	.get_ethtool_stats	= fbnic_get_ethtool_stats,
	.get_sset_count		= fbnic_get_sset_count,
	.get_rxnfc		= fbnic_get_rxnfc,
	.set_rxnfc		= fbnic_set_rxnfc,
	.get_rxfh_key_size	= fbnic_get_rxfh_key_size,
	.get_rxfh_indir_size	= fbnic_get_rxfh_indir_size,
	.get_rxfh		= fbnic_get_rxfh,
	.set_rxfh		= fbnic_set_rxfh,
	.get_channels		= fbnic_get_channels,
	.set_channels		= fbnic_set_channels,
	.get_ts_info		= fbnic_get_ts_info,
	.get_ts_stats		= fbnic_get_ts_stats,
	.get_eth_mac_stats	= fbnic_get_eth_mac_stats,
};

void fbnic_set_ethtool_ops(struct net_device *dev)
{
	dev->ethtool_ops = &fbnic_ethtool_ops;
}
