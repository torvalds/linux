// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>

#include <net/mana/mana.h>

struct mana_stats_desc {
	char name[ETH_GSTRING_LEN];
	u16 offset;
};

static const struct mana_stats_desc mana_eth_stats[] = {
	{"stop_queue", offsetof(struct mana_ethtool_stats, stop_queue)},
	{"wake_queue", offsetof(struct mana_ethtool_stats, wake_queue)},
	{"tx_cq_err", offsetof(struct mana_ethtool_stats, tx_cqe_err)},
	{"tx_cqe_unknown_type", offsetof(struct mana_ethtool_stats,
					tx_cqe_unknown_type)},
	{"tx_linear_pkt_cnt", offsetof(struct mana_ethtool_stats,
				       tx_linear_pkt_cnt)},
	{"rx_cqe_unknown_type", offsetof(struct mana_ethtool_stats,
					rx_cqe_unknown_type)},
};

static const struct mana_stats_desc mana_hc_stats[] = {
	{"hc_rx_discards_no_wqe", offsetof(struct mana_ethtool_hc_stats,
					   hc_rx_discards_no_wqe)},
	{"hc_rx_err_vport_disabled", offsetof(struct mana_ethtool_hc_stats,
					      hc_rx_err_vport_disabled)},
	{"hc_rx_bytes", offsetof(struct mana_ethtool_hc_stats, hc_rx_bytes)},
	{"hc_rx_ucast_pkts", offsetof(struct mana_ethtool_hc_stats,
				      hc_rx_ucast_pkts)},
	{"hc_rx_ucast_bytes", offsetof(struct mana_ethtool_hc_stats,
				       hc_rx_ucast_bytes)},
	{"hc_rx_bcast_pkts", offsetof(struct mana_ethtool_hc_stats,
				      hc_rx_bcast_pkts)},
	{"hc_rx_bcast_bytes", offsetof(struct mana_ethtool_hc_stats,
				       hc_rx_bcast_bytes)},
	{"hc_rx_mcast_pkts", offsetof(struct mana_ethtool_hc_stats,
				      hc_rx_mcast_pkts)},
	{"hc_rx_mcast_bytes", offsetof(struct mana_ethtool_hc_stats,
				       hc_rx_mcast_bytes)},
	{"hc_tx_err_gf_disabled", offsetof(struct mana_ethtool_hc_stats,
					   hc_tx_err_gf_disabled)},
	{"hc_tx_err_vport_disabled", offsetof(struct mana_ethtool_hc_stats,
					      hc_tx_err_vport_disabled)},
	{"hc_tx_err_inval_vportoffset_pkt",
	 offsetof(struct mana_ethtool_hc_stats,
		  hc_tx_err_inval_vportoffset_pkt)},
	{"hc_tx_err_vlan_enforcement", offsetof(struct mana_ethtool_hc_stats,
						hc_tx_err_vlan_enforcement)},
	{"hc_tx_err_eth_type_enforcement",
	 offsetof(struct mana_ethtool_hc_stats, hc_tx_err_eth_type_enforcement)},
	{"hc_tx_err_sa_enforcement", offsetof(struct mana_ethtool_hc_stats,
					      hc_tx_err_sa_enforcement)},
	{"hc_tx_err_sqpdid_enforcement",
	 offsetof(struct mana_ethtool_hc_stats, hc_tx_err_sqpdid_enforcement)},
	{"hc_tx_err_cqpdid_enforcement",
	 offsetof(struct mana_ethtool_hc_stats, hc_tx_err_cqpdid_enforcement)},
	{"hc_tx_err_mtu_violation", offsetof(struct mana_ethtool_hc_stats,
					     hc_tx_err_mtu_violation)},
	{"hc_tx_err_inval_oob", offsetof(struct mana_ethtool_hc_stats,
					 hc_tx_err_inval_oob)},
	{"hc_tx_err_gdma", offsetof(struct mana_ethtool_hc_stats,
				    hc_tx_err_gdma)},
	{"hc_tx_bytes", offsetof(struct mana_ethtool_hc_stats, hc_tx_bytes)},
	{"hc_tx_ucast_pkts", offsetof(struct mana_ethtool_hc_stats,
					hc_tx_ucast_pkts)},
	{"hc_tx_ucast_bytes", offsetof(struct mana_ethtool_hc_stats,
					hc_tx_ucast_bytes)},
	{"hc_tx_bcast_pkts", offsetof(struct mana_ethtool_hc_stats,
					hc_tx_bcast_pkts)},
	{"hc_tx_bcast_bytes", offsetof(struct mana_ethtool_hc_stats,
					hc_tx_bcast_bytes)},
	{"hc_tx_mcast_pkts", offsetof(struct mana_ethtool_hc_stats,
					hc_tx_mcast_pkts)},
	{"hc_tx_mcast_bytes", offsetof(struct mana_ethtool_hc_stats,
					hc_tx_mcast_bytes)},
};

static const struct mana_stats_desc mana_phy_stats[] = {
	{ "hc_rx_pkt_drop_phy", offsetof(struct mana_ethtool_phy_stats, rx_pkt_drop_phy) },
	{ "hc_tx_pkt_drop_phy", offsetof(struct mana_ethtool_phy_stats, tx_pkt_drop_phy) },
	{ "hc_tc0_rx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, rx_pkt_tc0_phy) },
	{ "hc_tc0_rx_byte_phy", offsetof(struct mana_ethtool_phy_stats, rx_byte_tc0_phy) },
	{ "hc_tc0_tx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, tx_pkt_tc0_phy) },
	{ "hc_tc0_tx_byte_phy", offsetof(struct mana_ethtool_phy_stats, tx_byte_tc0_phy) },
	{ "hc_tc1_rx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, rx_pkt_tc1_phy) },
	{ "hc_tc1_rx_byte_phy", offsetof(struct mana_ethtool_phy_stats, rx_byte_tc1_phy) },
	{ "hc_tc1_tx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, tx_pkt_tc1_phy) },
	{ "hc_tc1_tx_byte_phy", offsetof(struct mana_ethtool_phy_stats, tx_byte_tc1_phy) },
	{ "hc_tc2_rx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, rx_pkt_tc2_phy) },
	{ "hc_tc2_rx_byte_phy", offsetof(struct mana_ethtool_phy_stats, rx_byte_tc2_phy) },
	{ "hc_tc2_tx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, tx_pkt_tc2_phy) },
	{ "hc_tc2_tx_byte_phy", offsetof(struct mana_ethtool_phy_stats, tx_byte_tc2_phy) },
	{ "hc_tc3_rx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, rx_pkt_tc3_phy) },
	{ "hc_tc3_rx_byte_phy", offsetof(struct mana_ethtool_phy_stats, rx_byte_tc3_phy) },
	{ "hc_tc3_tx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, tx_pkt_tc3_phy) },
	{ "hc_tc3_tx_byte_phy", offsetof(struct mana_ethtool_phy_stats, tx_byte_tc3_phy) },
	{ "hc_tc4_rx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, rx_pkt_tc4_phy) },
	{ "hc_tc4_rx_byte_phy", offsetof(struct mana_ethtool_phy_stats, rx_byte_tc4_phy) },
	{ "hc_tc4_tx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, tx_pkt_tc4_phy) },
	{ "hc_tc4_tx_byte_phy", offsetof(struct mana_ethtool_phy_stats, tx_byte_tc4_phy) },
	{ "hc_tc5_rx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, rx_pkt_tc5_phy) },
	{ "hc_tc5_rx_byte_phy", offsetof(struct mana_ethtool_phy_stats, rx_byte_tc5_phy) },
	{ "hc_tc5_tx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, tx_pkt_tc5_phy) },
	{ "hc_tc5_tx_byte_phy", offsetof(struct mana_ethtool_phy_stats, tx_byte_tc5_phy) },
	{ "hc_tc6_rx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, rx_pkt_tc6_phy) },
	{ "hc_tc6_rx_byte_phy", offsetof(struct mana_ethtool_phy_stats, rx_byte_tc6_phy) },
	{ "hc_tc6_tx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, tx_pkt_tc6_phy) },
	{ "hc_tc6_tx_byte_phy", offsetof(struct mana_ethtool_phy_stats, tx_byte_tc6_phy) },
	{ "hc_tc7_rx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, rx_pkt_tc7_phy) },
	{ "hc_tc7_rx_byte_phy", offsetof(struct mana_ethtool_phy_stats, rx_byte_tc7_phy) },
	{ "hc_tc7_tx_pkt_phy", offsetof(struct mana_ethtool_phy_stats, tx_pkt_tc7_phy) },
	{ "hc_tc7_tx_byte_phy", offsetof(struct mana_ethtool_phy_stats, tx_byte_tc7_phy) },
	{ "hc_tc0_rx_pause_phy", offsetof(struct mana_ethtool_phy_stats, rx_pause_tc0_phy) },
	{ "hc_tc0_tx_pause_phy", offsetof(struct mana_ethtool_phy_stats, tx_pause_tc0_phy) },
	{ "hc_tc1_rx_pause_phy", offsetof(struct mana_ethtool_phy_stats, rx_pause_tc1_phy) },
	{ "hc_tc1_tx_pause_phy", offsetof(struct mana_ethtool_phy_stats, tx_pause_tc1_phy) },
	{ "hc_tc2_rx_pause_phy", offsetof(struct mana_ethtool_phy_stats, rx_pause_tc2_phy) },
	{ "hc_tc2_tx_pause_phy", offsetof(struct mana_ethtool_phy_stats, tx_pause_tc2_phy) },
	{ "hc_tc3_rx_pause_phy", offsetof(struct mana_ethtool_phy_stats, rx_pause_tc3_phy) },
	{ "hc_tc3_tx_pause_phy", offsetof(struct mana_ethtool_phy_stats, tx_pause_tc3_phy) },
	{ "hc_tc4_rx_pause_phy", offsetof(struct mana_ethtool_phy_stats, rx_pause_tc4_phy) },
	{ "hc_tc4_tx_pause_phy", offsetof(struct mana_ethtool_phy_stats, tx_pause_tc4_phy) },
	{ "hc_tc5_rx_pause_phy", offsetof(struct mana_ethtool_phy_stats, rx_pause_tc5_phy) },
	{ "hc_tc5_tx_pause_phy", offsetof(struct mana_ethtool_phy_stats, tx_pause_tc5_phy) },
	{ "hc_tc6_rx_pause_phy", offsetof(struct mana_ethtool_phy_stats, rx_pause_tc6_phy) },
	{ "hc_tc6_tx_pause_phy", offsetof(struct mana_ethtool_phy_stats, tx_pause_tc6_phy) },
	{ "hc_tc7_rx_pause_phy", offsetof(struct mana_ethtool_phy_stats, rx_pause_tc7_phy) },
	{ "hc_tc7_tx_pause_phy", offsetof(struct mana_ethtool_phy_stats, tx_pause_tc7_phy) },
};

static const char mana_priv_flags[MANA_PRIV_FLAG_MAX][ETH_GSTRING_LEN] = {
	[MANA_PRIV_FLAG_USE_FULL_PAGE_RXBUF] = "full-page-rx"
};

static int mana_get_sset_count(struct net_device *ndev, int stringset)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	unsigned int num_queues = apc->num_queues;

	switch (stringset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(mana_eth_stats) +
		       ARRAY_SIZE(mana_phy_stats) +
		       ARRAY_SIZE(mana_hc_stats)  +
		       num_queues * (MANA_STATS_RX_COUNT + MANA_STATS_TX_COUNT);

	case ETH_SS_PRIV_FLAGS:
		return MANA_PRIV_FLAG_MAX;

	default:
		return -EINVAL;
	}
}

static void mana_get_strings_stats(struct mana_port_context *apc, u8 **data)
{
	unsigned int num_queues = apc->num_queues;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(mana_eth_stats); i++)
		ethtool_puts(data, mana_eth_stats[i].name);

	for (i = 0; i < ARRAY_SIZE(mana_hc_stats); i++)
		ethtool_puts(data, mana_hc_stats[i].name);

	for (i = 0; i < ARRAY_SIZE(mana_phy_stats); i++)
		ethtool_puts(data, mana_phy_stats[i].name);

	for (i = 0; i < num_queues; i++) {
		ethtool_sprintf(data, "rx_%d_packets", i);
		ethtool_sprintf(data, "rx_%d_bytes", i);
		ethtool_sprintf(data, "rx_%d_xdp_drop", i);
		ethtool_sprintf(data, "rx_%d_xdp_tx", i);
		ethtool_sprintf(data, "rx_%d_xdp_redirect", i);
		ethtool_sprintf(data, "rx_%d_pkt_len0_err", i);
		for (j = 0; j < MANA_CQE_COAL_PKTS_8 - 1; j++)
			ethtool_sprintf(data,
					"rx_%d_coalesced_cqe_%d",
					i,
					j + 2);
	}

	for (i = 0; i < num_queues; i++) {
		ethtool_sprintf(data, "tx_%d_packets", i);
		ethtool_sprintf(data, "tx_%d_bytes", i);
		ethtool_sprintf(data, "tx_%d_xdp_xmit", i);
		ethtool_sprintf(data, "tx_%d_tso_packets", i);
		ethtool_sprintf(data, "tx_%d_tso_bytes", i);
		ethtool_sprintf(data, "tx_%d_tso_inner_packets", i);
		ethtool_sprintf(data, "tx_%d_tso_inner_bytes", i);
		ethtool_sprintf(data, "tx_%d_long_pkt_fmt", i);
		ethtool_sprintf(data, "tx_%d_short_pkt_fmt", i);
		ethtool_sprintf(data, "tx_%d_csum_partial", i);
		ethtool_sprintf(data, "tx_%d_mana_map_err", i);
	}
}

static void mana_get_strings_priv_flags(u8 **data)
{
	int i;

	for (i = 0; i < MANA_PRIV_FLAG_MAX; i++)
		ethtool_puts(data, mana_priv_flags[i]);
}

static void mana_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	struct mana_port_context *apc = netdev_priv(ndev);

	switch (stringset) {
	case ETH_SS_STATS:
		mana_get_strings_stats(apc, &data);
		break;
	case ETH_SS_PRIV_FLAGS:
		mana_get_strings_priv_flags(&data);
		break;
	default:
		break;
	}
}

static void mana_get_ethtool_stats(struct net_device *ndev,
				   struct ethtool_stats *e_stats, u64 *data)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	unsigned int num_queues = apc->num_queues;
	void *eth_stats = &apc->eth_stats;
	void *hc_stats = &apc->ac->hc_stats;
	void *phy_stats = &apc->phy_stats;
	struct mana_stats_rx *rx_stats;
	struct mana_stats_tx *tx_stats;
	unsigned int start;
	u64 packets, bytes;
	u64 xdp_redirect;
	u64 xdp_xmit;
	u64 xdp_drop;
	u64 xdp_tx;
	u64 pkt_len0_err;
	u64 coalesced_cqe[MANA_CQE_COAL_PKTS_8 - 1];
	u64 tso_packets;
	u64 tso_bytes;
	u64 tso_inner_packets;
	u64 tso_inner_bytes;
	u64 long_pkt_fmt;
	u64 short_pkt_fmt;
	u64 csum_partial;
	u64 mana_map_err;
	int q, i = 0, j;

	if (!apc->port_is_up)
		return;

	/* We call this mana function to get the phy stats from GDMA and includes
	 * aggregate tx/rx drop counters, Per-TC(Traffic Channel) tx/rx and pause
	 * counters.
	 */
	mana_query_phy_stats(apc);

	for (q = 0; q < ARRAY_SIZE(mana_eth_stats); q++)
		data[i++] = *(u64 *)(eth_stats + mana_eth_stats[q].offset);

	for (q = 0; q < ARRAY_SIZE(mana_hc_stats); q++)
		data[i++] = *(u64 *)(hc_stats + mana_hc_stats[q].offset);

	for (q = 0; q < ARRAY_SIZE(mana_phy_stats); q++)
		data[i++] = *(u64 *)(phy_stats + mana_phy_stats[q].offset);

	for (q = 0; q < num_queues; q++) {
		rx_stats = &apc->rxqs[q]->stats;

		do {
			start = u64_stats_fetch_begin(&rx_stats->syncp);
			packets = rx_stats->packets;
			bytes = rx_stats->bytes;
			xdp_drop = rx_stats->xdp_drop;
			xdp_tx = rx_stats->xdp_tx;
			xdp_redirect = rx_stats->xdp_redirect;
			pkt_len0_err = rx_stats->pkt_len0_err;
			for (j = 0; j < MANA_CQE_COAL_PKTS_8 - 1; j++)
				coalesced_cqe[j] = rx_stats->coalesced_cqe[j];
		} while (u64_stats_fetch_retry(&rx_stats->syncp, start));

		data[i++] = packets;
		data[i++] = bytes;
		data[i++] = xdp_drop;
		data[i++] = xdp_tx;
		data[i++] = xdp_redirect;
		data[i++] = pkt_len0_err;
		for (j = 0; j < MANA_CQE_COAL_PKTS_8 - 1; j++)
			data[i++] = coalesced_cqe[j];
	}

	for (q = 0; q < num_queues; q++) {
		tx_stats = &apc->tx_qp[q]->txq.stats;

		do {
			start = u64_stats_fetch_begin(&tx_stats->syncp);
			packets = tx_stats->packets;
			bytes = tx_stats->bytes;
			xdp_xmit = tx_stats->xdp_xmit;
			tso_packets = tx_stats->tso_packets;
			tso_bytes = tx_stats->tso_bytes;
			tso_inner_packets = tx_stats->tso_inner_packets;
			tso_inner_bytes = tx_stats->tso_inner_bytes;
			long_pkt_fmt = tx_stats->long_pkt_fmt;
			short_pkt_fmt = tx_stats->short_pkt_fmt;
			csum_partial = tx_stats->csum_partial;
			mana_map_err = tx_stats->mana_map_err;
		} while (u64_stats_fetch_retry(&tx_stats->syncp, start));

		data[i++] = packets;
		data[i++] = bytes;
		data[i++] = xdp_xmit;
		data[i++] = tso_packets;
		data[i++] = tso_bytes;
		data[i++] = tso_inner_packets;
		data[i++] = tso_inner_bytes;
		data[i++] = long_pkt_fmt;
		data[i++] = short_pkt_fmt;
		data[i++] = csum_partial;
		data[i++] = mana_map_err;
	}
}

static u32 mana_get_rx_ring_count(struct net_device *ndev)
{
	struct mana_port_context *apc = netdev_priv(ndev);

	return apc->num_queues;
}

static u32 mana_get_rxfh_key_size(struct net_device *ndev)
{
	return MANA_HASH_KEY_SIZE;
}

static u32 mana_rss_indir_size(struct net_device *ndev)
{
	struct mana_port_context *apc = netdev_priv(ndev);

	return apc->indir_table_sz;
}

static int mana_get_rxfh(struct net_device *ndev,
			 struct ethtool_rxfh_param *rxfh)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	int i;

	rxfh->hfunc = ETH_RSS_HASH_TOP; /* Toeplitz */

	if (rxfh->indir) {
		for (i = 0; i < apc->indir_table_sz; i++)
			rxfh->indir[i] = apc->indir_table[i];
	}

	if (rxfh->key)
		memcpy(rxfh->key, apc->hashkey, MANA_HASH_KEY_SIZE);

	return 0;
}

static int mana_set_rxfh(struct net_device *ndev,
			 struct ethtool_rxfh_param *rxfh,
			 struct netlink_ext_ack *extack)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	bool update_hash = false, update_table = false;
	u8 save_key[MANA_HASH_KEY_SIZE];
	u32 *save_table;
	int i, err;

	if (!apc->port_is_up)
		return -EOPNOTSUPP;

	if (rxfh->hfunc != ETH_RSS_HASH_NO_CHANGE &&
	    rxfh->hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	save_table = kcalloc(apc->indir_table_sz, sizeof(u32), GFP_KERNEL);
	if (!save_table)
		return -ENOMEM;

	if (rxfh->indir) {
		for (i = 0; i < apc->indir_table_sz; i++)
			if (rxfh->indir[i] >= apc->num_queues) {
				err = -EINVAL;
				goto cleanup;
			}

		update_table = true;
		for (i = 0; i < apc->indir_table_sz; i++) {
			save_table[i] = apc->indir_table[i];
			apc->indir_table[i] = rxfh->indir[i];
		}
	}

	if (rxfh->key) {
		update_hash = true;
		memcpy(save_key, apc->hashkey, MANA_HASH_KEY_SIZE);
		memcpy(apc->hashkey, rxfh->key, MANA_HASH_KEY_SIZE);
	}

	err = mana_config_rss(apc, TRI_STATE_TRUE, update_hash, update_table);

	if (err) { /* recover to original values */
		if (update_table) {
			for (i = 0; i < apc->indir_table_sz; i++)
				apc->indir_table[i] = save_table[i];
		}

		if (update_hash)
			memcpy(apc->hashkey, save_key, MANA_HASH_KEY_SIZE);

		mana_config_rss(apc, TRI_STATE_TRUE, update_hash, update_table);
	}

cleanup:
	kfree(save_table);

	return err;
}

static void mana_get_channels(struct net_device *ndev,
			      struct ethtool_channels *channel)
{
	struct mana_port_context *apc = netdev_priv(ndev);

	channel->max_combined = apc->max_queues;
	channel->combined_count = apc->num_queues;
}

#define MANA_RX_CQE_NSEC_DEF 2048
static int mana_get_coalesce(struct net_device *ndev,
			     struct ethtool_coalesce *ec,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct mana_port_context *apc = netdev_priv(ndev);

	kernel_coal->rx_cqe_frames =
		apc->cqe8_coalescing_enable ? MANA_CQE_COAL_PKTS_8 :
		apc->cqe_coalescing_enable ? MANA_RXCOMP_OOB_NUM_PPI : 1;

	kernel_coal->rx_cqe_nsecs = apc->cqe_coalescing_timeout_ns;

	/* Return the default timeout value for old FW not providing
	 * this value.
	 */
	if (apc->port_is_up && apc->cqe_coalescing_enable &&
	    !kernel_coal->rx_cqe_nsecs)
		kernel_coal->rx_cqe_nsecs = MANA_RX_CQE_NSEC_DEF;

	ec->rx_coalesce_usecs = apc->intr_modr_rx_usec;
	ec->rx_max_coalesced_frames = apc->intr_modr_rx_comp;

	ec->tx_coalesce_usecs = apc->intr_modr_tx_usec;
	ec->tx_max_coalesced_frames = apc->intr_modr_tx_comp;

	ec->use_adaptive_rx_coalesce = apc->rx_dim_enabled;
	ec->use_adaptive_tx_coalesce = apc->tx_dim_enabled;

	return 0;
}

static int mana_set_coalesce(struct net_device *ndev,
			     struct ethtool_coalesce *ec,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	struct {
		u16 intr_modr_rx_usec;
		u16 intr_modr_rx_comp;
		u16 intr_modr_tx_usec;
		u16 intr_modr_tx_comp;
		u8 cqe_coalescing_enable;
		u8 cqe8_coalescing_enable;
		bool rx_dim_enabled;
		bool tx_dim_enabled;
	} saved;
	bool modr_changed = false;
	bool dim_changed = false;
	struct gdma_context *gc;
	u32 max_cqe_frames;
	int err;

	gc = apc->ac->gdma_dev->gdma_context;
	max_cqe_frames = gc->cqe8_coalescing_sup ? MANA_CQE_COAL_PKTS_8 :
						   MANA_RXCOMP_OOB_NUM_PPI;

	/* Both static and dynamic interrupt moderation (DIM) rely on the
	 * same HW capability advertised by the PF.
	 */
	if ((ec->use_adaptive_rx_coalesce || ec->use_adaptive_tx_coalesce ||
	     ec->rx_coalesce_usecs || ec->tx_coalesce_usecs ||
	     ec->rx_max_coalesced_frames || ec->tx_max_coalesced_frames) &&
	    !(gc->pf_cap_flags1 & GDMA_PF_CAP_FLAG_1_DYN_INTERRUPT_MODERATION)) {
		NL_SET_ERR_MSG(extack,
			       "Interrupt Moderation is not supported by HW");
		return -EOPNOTSUPP;
	}

	if (kernel_coal->rx_cqe_frames != 1 &&
	    kernel_coal->rx_cqe_frames != MANA_RXCOMP_OOB_NUM_PPI &&
	    kernel_coal->rx_cqe_frames != max_cqe_frames) {
		NL_SET_ERR_MSG_FMT(extack,
				   "rx-frames must be 1 or %u%s, got %u",
				   MANA_RXCOMP_OOB_NUM_PPI,
				   gc->cqe8_coalescing_sup ? " or 8" : "",
				   kernel_coal->rx_cqe_frames);
		return -EINVAL;
	}

	if (ec->rx_coalesce_usecs > MANA_INTR_MODR_USEC_MAX ||
	    ec->tx_coalesce_usecs > MANA_INTR_MODR_USEC_MAX) {
		NL_SET_ERR_MSG_FMT(extack,
				   "coalesce usecs must be <= %lu",
				   MANA_INTR_MODR_USEC_MAX);
		return -EINVAL;
	}

	if (ec->rx_max_coalesced_frames > MANA_INTR_MODR_COMP_MAX ||
	    ec->tx_max_coalesced_frames > MANA_INTR_MODR_COMP_MAX) {
		NL_SET_ERR_MSG_FMT(extack,
				   "coalesce frames must be <= %lu",
				   MANA_INTR_MODR_COMP_MAX);
		return -EINVAL;
	}

	if (ec->rx_coalesce_usecs != apc->intr_modr_rx_usec ||
	    ec->rx_max_coalesced_frames != apc->intr_modr_rx_comp ||
	    ec->tx_coalesce_usecs != apc->intr_modr_tx_usec ||
	    ec->tx_max_coalesced_frames != apc->intr_modr_tx_comp)
		modr_changed = true;

	saved.intr_modr_rx_usec = apc->intr_modr_rx_usec;
	saved.intr_modr_rx_comp = apc->intr_modr_rx_comp;
	saved.intr_modr_tx_usec = apc->intr_modr_tx_usec;
	saved.intr_modr_tx_comp = apc->intr_modr_tx_comp;

	apc->intr_modr_rx_usec = ec->rx_coalesce_usecs;
	apc->intr_modr_rx_comp = ec->rx_max_coalesced_frames;
	apc->intr_modr_tx_usec = ec->tx_coalesce_usecs;
	apc->intr_modr_tx_comp = ec->tx_max_coalesced_frames;

	if (!!ec->use_adaptive_rx_coalesce != apc->rx_dim_enabled ||
	    !!ec->use_adaptive_tx_coalesce != apc->tx_dim_enabled)
		dim_changed = true;

	saved.rx_dim_enabled = apc->rx_dim_enabled;
	saved.tx_dim_enabled = apc->tx_dim_enabled;

	saved.cqe_coalescing_enable = apc->cqe_coalescing_enable;
	saved.cqe8_coalescing_enable = apc->cqe8_coalescing_enable;
	apc->cqe_coalescing_enable =
		kernel_coal->rx_cqe_frames >= MANA_RXCOMP_OOB_NUM_PPI;
	apc->cqe8_coalescing_enable =
		kernel_coal->rx_cqe_frames == MANA_CQE_COAL_PKTS_8;

	if (!apc->port_is_up) {
		WRITE_ONCE(apc->rx_dim_enabled, !!ec->use_adaptive_rx_coalesce);
		WRITE_ONCE(apc->tx_dim_enabled, !!ec->use_adaptive_tx_coalesce);
		return 0;
	}

	if (apc->cqe_coalescing_enable != saved.cqe_coalescing_enable ||
	    apc->cqe8_coalescing_enable != saved.cqe8_coalescing_enable) {
		/* CQE coalescing setting is applied via RSS configuration. */
		err = mana_config_rss(apc, TRI_STATE_TRUE, false, false);
		if (err) {
			netdev_err(ndev, "Change CQE coalescing failed: %d\n",
				   err);
			apc->cqe_coalescing_enable =
				saved.cqe_coalescing_enable;
			apc->cqe8_coalescing_enable =
				saved.cqe8_coalescing_enable;
			apc->intr_modr_rx_usec = saved.intr_modr_rx_usec;
			apc->intr_modr_rx_comp = saved.intr_modr_rx_comp;
			apc->intr_modr_tx_usec = saved.intr_modr_tx_usec;
			apc->intr_modr_tx_comp = saved.intr_modr_tx_comp;
			return err;
		}
	}

	if (modr_changed || dim_changed) {
		bool new_rx_dim = !!ec->use_adaptive_rx_coalesce;
		bool new_tx_dim = !!ec->use_adaptive_tx_coalesce;
		bool disable_rx_dim = saved.rx_dim_enabled && !new_rx_dim;
		bool disable_tx_dim = saved.tx_dim_enabled && !new_tx_dim;
		bool enable_rx_dim = !saved.rx_dim_enabled && new_rx_dim;
		bool enable_tx_dim = !saved.tx_dim_enabled && new_tx_dim;
		int q;

		/* On disable: clear the per-port flag first and
		 * synchronize_net() so any in-flight NAPI poll observes
		 * the new value and will not schedule further DIM work;
		 * then drain pending work and restore the static
		 * moderation values.
		 */
		if (disable_rx_dim)
			WRITE_ONCE(apc->rx_dim_enabled, false);
		if (disable_tx_dim)
			WRITE_ONCE(apc->tx_dim_enabled, false);
		if (disable_rx_dim || disable_tx_dim)
			synchronize_net();

		for (q = 0; q < apc->num_queues; q++) {
			struct mana_cq *rx_cq = &apc->rxqs[q]->rx_cq;
			struct mana_cq *tx_cq = &apc->tx_qp[q]->tx_cq;

			if (disable_rx_dim)
				mana_dim_change(rx_cq, false);
			else if (enable_rx_dim)
				mana_dim_change(rx_cq, true);
			else if (!new_rx_dim && modr_changed)
				mana_gd_ring_dim(rx_cq->gdma_cq,
						 apc->intr_modr_rx_usec, true,
						 apc->intr_modr_rx_comp, true);

			if (disable_tx_dim)
				mana_dim_change(tx_cq, false);
			else if (enable_tx_dim)
				mana_dim_change(tx_cq, true);
			else if (!new_tx_dim && modr_changed)
				mana_gd_ring_dim(tx_cq->gdma_cq,
						 apc->intr_modr_tx_usec, true,
						 apc->intr_modr_tx_comp, true);
		}

		/* Publish the enable flag with release semantics so a
		 * concurrent NAPI poll that observes it set also sees the DIM
		 * (re)init done by mana_dim_change() above.
		 */
		if (enable_rx_dim)
			/* pairs with smp_load_acquire() in mana_update_rx_dim() */
			smp_store_release(&apc->rx_dim_enabled, true);
		if (enable_tx_dim)
			/* pairs with smp_load_acquire() in mana_update_tx_dim() */
			smp_store_release(&apc->tx_dim_enabled, true);
	}

	return 0;
}

/* mana_set_channels - change the number of queues on a port
 *
 * Returns -EBUSY if RDMA holds the vport with EQs sized to the
 * current num_queues.
 */
static int mana_set_channels(struct net_device *ndev,
			     struct ethtool_channels *channels)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	unsigned int new_count = channels->combined_count;
	unsigned int old_count = apc->num_queues;
	int err;

	/* Set channel_changing to block RDMA from grabbing the vport
	 * during the detach/attach window. mana_cfg_vport() checks
	 * this flag under vport_mutex and returns -EBUSY if set.
	 */
	mutex_lock(&apc->vport_mutex);
	if (!apc->port_is_up && apc->vport_use_count) {
		mutex_unlock(&apc->vport_mutex);
		return -EBUSY;
	}
	apc->channel_changing = true;
	mutex_unlock(&apc->vport_mutex);

	err = mana_pre_alloc_rxbufs(apc, ndev->mtu, new_count);
	if (err) {
		netdev_err(ndev, "Insufficient memory for new allocations");
		goto clear_flag;
	}

	err = mana_detach(ndev, false);
	if (err) {
		netdev_err(ndev, "mana_detach failed: %d\n", err);
		goto out;
	}

	apc->num_queues = new_count;
	err = mana_attach(ndev);
	if (err) {
		apc->num_queues = old_count;
		netdev_err(ndev, "mana_attach failed: %d\n", err);
	}

out:
	mana_pre_dealloc_rxbufs(apc);
clear_flag:
	mutex_lock(&apc->vport_mutex);
	apc->channel_changing = false;
	mutex_unlock(&apc->vport_mutex);
	return err;
}

static void mana_get_ringparam(struct net_device *ndev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
{
	struct mana_port_context *apc = netdev_priv(ndev);

	ring->rx_pending = apc->rx_queue_size;
	ring->tx_pending = apc->tx_queue_size;
	ring->rx_max_pending = MAX_RX_BUFFERS_PER_QUEUE;
	ring->tx_max_pending = MAX_TX_BUFFERS_PER_QUEUE;
}

static int mana_set_ringparam(struct net_device *ndev,
			      struct ethtool_ringparam *ring,
			      struct kernel_ethtool_ringparam *kernel_ring,
			      struct netlink_ext_ack *extack)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	u32 new_tx, new_rx;
	u32 old_tx, old_rx;
	int err;

	old_tx = apc->tx_queue_size;
	old_rx = apc->rx_queue_size;

	if (ring->tx_pending < MIN_TX_BUFFERS_PER_QUEUE) {
		NL_SET_ERR_MSG_FMT(extack, "tx:%d less than the min:%d", ring->tx_pending,
				   MIN_TX_BUFFERS_PER_QUEUE);
		return -EINVAL;
	}

	if (ring->rx_pending < MIN_RX_BUFFERS_PER_QUEUE) {
		NL_SET_ERR_MSG_FMT(extack, "rx:%d less than the min:%d", ring->rx_pending,
				   MIN_RX_BUFFERS_PER_QUEUE);
		return -EINVAL;
	}

	new_rx = roundup_pow_of_two(ring->rx_pending);
	new_tx = roundup_pow_of_two(ring->tx_pending);
	netdev_info(ndev, "Using nearest power of 2 values for Txq:%d Rxq:%d\n",
		    new_tx, new_rx);

	/* pre-allocating new buffers to prevent failures in mana_attach() later */
	apc->rx_queue_size = new_rx;
	err = mana_pre_alloc_rxbufs(apc, ndev->mtu, apc->num_queues);
	apc->rx_queue_size = old_rx;
	if (err) {
		netdev_err(ndev, "Insufficient memory for new allocations\n");
		return err;
	}

	err = mana_detach(ndev, false);
	if (err) {
		netdev_err(ndev, "mana_detach failed: %d\n", err);
		goto out;
	}

	apc->tx_queue_size = new_tx;
	apc->rx_queue_size = new_rx;

	err = mana_attach(ndev);
	if (err) {
		netdev_err(ndev, "mana_attach failed: %d\n", err);
		apc->tx_queue_size = old_tx;
		apc->rx_queue_size = old_rx;
	}
out:
	mana_pre_dealloc_rxbufs(apc);
	return err;
}

static int mana_get_link_ksettings(struct net_device *ndev,
				   struct ethtool_link_ksettings *cmd)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	int err;

	err = mana_query_link_cfg(apc);
	cmd->base.speed = (err) ? SPEED_UNKNOWN : apc->max_speed;

	cmd->base.duplex = DUPLEX_FULL;
	cmd->base.port = PORT_OTHER;

	return 0;
}

static u32 mana_get_priv_flags(struct net_device *ndev)
{
	struct mana_port_context *apc = netdev_priv(ndev);

	return apc->priv_flags;
}

static int mana_set_priv_flags(struct net_device *ndev, u32 priv_flags)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	u32 changed = apc->priv_flags ^ priv_flags;
	u32 old_priv_flags = apc->priv_flags;
	int err = 0;

	if (!changed)
		return 0;

	/* Reject unknown bits */
	if (priv_flags & ~GENMASK(MANA_PRIV_FLAG_MAX - 1, 0))
		return -EINVAL;

	apc->priv_flags = priv_flags;

	if (changed & BIT(MANA_PRIV_FLAG_USE_FULL_PAGE_RXBUF)) {
		if (!apc->port_is_up)
			return 0;

		/* If XDP is attached or MTU is jumbo, single-buffer-per-page
		 * is already forced regardless of this flag. Skip the
		 * expensive detach/attach cycle since nothing changes.
		 */
		if (ndev->mtu + MANA_RXBUF_PAD > PAGE_SIZE / 2 ||
		    mana_xdp_get(apc))
			return 0;

		/* Block RDMA from grabbing the vport during detach/attach */
		mutex_lock(&apc->vport_mutex);
		apc->channel_changing = true;
		mutex_unlock(&apc->vport_mutex);

		err = mana_pre_alloc_rxbufs(apc, ndev->mtu, apc->num_queues);
		if (err) {
			netdev_err(ndev,
				   "Insufficient memory for new allocations\n");
			apc->priv_flags = old_priv_flags;
			goto clear_flag;
		}

		err = mana_detach(ndev, false);
		if (err) {
			netdev_err(ndev, "mana_detach failed: %d\n", err);
			apc->priv_flags = old_priv_flags;
			goto out;
		}

		err = mana_attach(ndev);
		if (err) {
			netdev_err(ndev, "mana_attach failed: %d\n", err);
			apc->priv_flags = old_priv_flags;
		}
	}

out:
	mana_pre_dealloc_rxbufs(apc);
clear_flag:
	mutex_lock(&apc->vport_mutex);
	apc->channel_changing = false;
	mutex_unlock(&apc->vport_mutex);

	return err;
}

const struct ethtool_ops mana_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_CQE_FRAMES |
				     ETHTOOL_COALESCE_RX_USECS |
				     ETHTOOL_COALESCE_RX_MAX_FRAMES |
				     ETHTOOL_COALESCE_TX_USECS |
				     ETHTOOL_COALESCE_TX_MAX_FRAMES |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_TX,
	.op_needs_rtnl		= ETHTOOL_OP_NEEDS_RTNL_SCHANNELS |
				  ETHTOOL_OP_NEEDS_RTNL_SRINGPARAM |
				  ETHTOOL_OP_NEEDS_RTNL_SPFLAGS |
				  ETHTOOL_OP_NEEDS_RTNL_GLINK,
	.get_ethtool_stats	= mana_get_ethtool_stats,
	.get_sset_count		= mana_get_sset_count,
	.get_strings		= mana_get_strings,
	.get_rx_ring_count	= mana_get_rx_ring_count,
	.get_rxfh_key_size	= mana_get_rxfh_key_size,
	.get_rxfh_indir_size	= mana_rss_indir_size,
	.get_rxfh		= mana_get_rxfh,
	.set_rxfh		= mana_set_rxfh,
	.get_channels		= mana_get_channels,
	.set_channels		= mana_set_channels,
	.get_coalesce		= mana_get_coalesce,
	.set_coalesce		= mana_set_coalesce,
	.get_ringparam          = mana_get_ringparam,
	.set_ringparam          = mana_set_ringparam,
	.get_link_ksettings	= mana_get_link_ksettings,
	.get_link		= ethtool_op_get_link,
	.get_priv_flags		= mana_get_priv_flags,
	.set_priv_flags		= mana_set_priv_flags,
};
