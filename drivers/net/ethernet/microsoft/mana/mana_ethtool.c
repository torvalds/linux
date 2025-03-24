// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>

#include <net/mana/mana.h>

static const struct {
	char name[ETH_GSTRING_LEN];
	u16 offset;
} mana_eth_stats[] = {
	{"stop_queue", offsetof(struct mana_ethtool_stats, stop_queue)},
	{"wake_queue", offsetof(struct mana_ethtool_stats, wake_queue)},
	{"hc_rx_discards_no_wqe", offsetof(struct mana_ethtool_stats,
					   hc_rx_discards_no_wqe)},
	{"hc_rx_err_vport_disabled", offsetof(struct mana_ethtool_stats,
					      hc_rx_err_vport_disabled)},
	{"hc_rx_bytes", offsetof(struct mana_ethtool_stats, hc_rx_bytes)},
	{"hc_rx_ucast_pkts", offsetof(struct mana_ethtool_stats,
				      hc_rx_ucast_pkts)},
	{"hc_rx_ucast_bytes", offsetof(struct mana_ethtool_stats,
				       hc_rx_ucast_bytes)},
	{"hc_rx_bcast_pkts", offsetof(struct mana_ethtool_stats,
				      hc_rx_bcast_pkts)},
	{"hc_rx_bcast_bytes", offsetof(struct mana_ethtool_stats,
				       hc_rx_bcast_bytes)},
	{"hc_rx_mcast_pkts", offsetof(struct mana_ethtool_stats,
			hc_rx_mcast_pkts)},
	{"hc_rx_mcast_bytes", offsetof(struct mana_ethtool_stats,
				       hc_rx_mcast_bytes)},
	{"hc_tx_err_gf_disabled", offsetof(struct mana_ethtool_stats,
					   hc_tx_err_gf_disabled)},
	{"hc_tx_err_vport_disabled", offsetof(struct mana_ethtool_stats,
					      hc_tx_err_vport_disabled)},
	{"hc_tx_err_inval_vportoffset_pkt",
	 offsetof(struct mana_ethtool_stats,
		  hc_tx_err_inval_vportoffset_pkt)},
	{"hc_tx_err_vlan_enforcement", offsetof(struct mana_ethtool_stats,
						hc_tx_err_vlan_enforcement)},
	{"hc_tx_err_eth_type_enforcement",
	 offsetof(struct mana_ethtool_stats, hc_tx_err_eth_type_enforcement)},
	{"hc_tx_err_sa_enforcement", offsetof(struct mana_ethtool_stats,
					      hc_tx_err_sa_enforcement)},
	{"hc_tx_err_sqpdid_enforcement",
	 offsetof(struct mana_ethtool_stats, hc_tx_err_sqpdid_enforcement)},
	{"hc_tx_err_cqpdid_enforcement",
	 offsetof(struct mana_ethtool_stats, hc_tx_err_cqpdid_enforcement)},
	{"hc_tx_err_mtu_violation", offsetof(struct mana_ethtool_stats,
					     hc_tx_err_mtu_violation)},
	{"hc_tx_err_inval_oob", offsetof(struct mana_ethtool_stats,
					 hc_tx_err_inval_oob)},
	{"hc_tx_err_gdma", offsetof(struct mana_ethtool_stats,
				    hc_tx_err_gdma)},
	{"hc_tx_bytes", offsetof(struct mana_ethtool_stats, hc_tx_bytes)},
	{"hc_tx_ucast_pkts", offsetof(struct mana_ethtool_stats,
					hc_tx_ucast_pkts)},
	{"hc_tx_ucast_bytes", offsetof(struct mana_ethtool_stats,
					hc_tx_ucast_bytes)},
	{"hc_tx_bcast_pkts", offsetof(struct mana_ethtool_stats,
					hc_tx_bcast_pkts)},
	{"hc_tx_bcast_bytes", offsetof(struct mana_ethtool_stats,
					hc_tx_bcast_bytes)},
	{"hc_tx_mcast_pkts", offsetof(struct mana_ethtool_stats,
					hc_tx_mcast_pkts)},
	{"hc_tx_mcast_bytes", offsetof(struct mana_ethtool_stats,
					hc_tx_mcast_bytes)},
	{"tx_cq_err", offsetof(struct mana_ethtool_stats, tx_cqe_err)},
	{"tx_cqe_unknown_type", offsetof(struct mana_ethtool_stats,
					tx_cqe_unknown_type)},
	{"rx_coalesced_err", offsetof(struct mana_ethtool_stats,
					rx_coalesced_err)},
	{"rx_cqe_unknown_type", offsetof(struct mana_ethtool_stats,
					rx_cqe_unknown_type)},
};

static int mana_get_sset_count(struct net_device *ndev, int stringset)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	unsigned int num_queues = apc->num_queues;

	if (stringset != ETH_SS_STATS)
		return -EINVAL;

	return ARRAY_SIZE(mana_eth_stats) + num_queues *
				(MANA_STATS_RX_COUNT + MANA_STATS_TX_COUNT);
}

static void mana_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	unsigned int num_queues = apc->num_queues;
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(mana_eth_stats); i++)
		ethtool_puts(&data, mana_eth_stats[i].name);

	for (i = 0; i < num_queues; i++) {
		ethtool_sprintf(&data, "rx_%d_packets", i);
		ethtool_sprintf(&data, "rx_%d_bytes", i);
		ethtool_sprintf(&data, "rx_%d_xdp_drop", i);
		ethtool_sprintf(&data, "rx_%d_xdp_tx", i);
		ethtool_sprintf(&data, "rx_%d_xdp_redirect", i);
	}

	for (i = 0; i < num_queues; i++) {
		ethtool_sprintf(&data, "tx_%d_packets", i);
		ethtool_sprintf(&data, "tx_%d_bytes", i);
		ethtool_sprintf(&data, "tx_%d_xdp_xmit", i);
		ethtool_sprintf(&data, "tx_%d_tso_packets", i);
		ethtool_sprintf(&data, "tx_%d_tso_bytes", i);
		ethtool_sprintf(&data, "tx_%d_tso_inner_packets", i);
		ethtool_sprintf(&data, "tx_%d_tso_inner_bytes", i);
		ethtool_sprintf(&data, "tx_%d_long_pkt_fmt", i);
		ethtool_sprintf(&data, "tx_%d_short_pkt_fmt", i);
		ethtool_sprintf(&data, "tx_%d_csum_partial", i);
		ethtool_sprintf(&data, "tx_%d_mana_map_err", i);
	}
}

static void mana_get_ethtool_stats(struct net_device *ndev,
				   struct ethtool_stats *e_stats, u64 *data)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	unsigned int num_queues = apc->num_queues;
	void *eth_stats = &apc->eth_stats;
	struct mana_stats_rx *rx_stats;
	struct mana_stats_tx *tx_stats;
	unsigned int start;
	u64 packets, bytes;
	u64 xdp_redirect;
	u64 xdp_xmit;
	u64 xdp_drop;
	u64 xdp_tx;
	u64 tso_packets;
	u64 tso_bytes;
	u64 tso_inner_packets;
	u64 tso_inner_bytes;
	u64 long_pkt_fmt;
	u64 short_pkt_fmt;
	u64 csum_partial;
	u64 mana_map_err;
	int q, i = 0;

	if (!apc->port_is_up)
		return;
	/* we call mana function to update stats from GDMA */
	mana_query_gf_stats(apc);

	for (q = 0; q < ARRAY_SIZE(mana_eth_stats); q++)
		data[i++] = *(u64 *)(eth_stats + mana_eth_stats[q].offset);

	for (q = 0; q < num_queues; q++) {
		rx_stats = &apc->rxqs[q]->stats;

		do {
			start = u64_stats_fetch_begin(&rx_stats->syncp);
			packets = rx_stats->packets;
			bytes = rx_stats->bytes;
			xdp_drop = rx_stats->xdp_drop;
			xdp_tx = rx_stats->xdp_tx;
			xdp_redirect = rx_stats->xdp_redirect;
		} while (u64_stats_fetch_retry(&rx_stats->syncp, start));

		data[i++] = packets;
		data[i++] = bytes;
		data[i++] = xdp_drop;
		data[i++] = xdp_tx;
		data[i++] = xdp_redirect;
	}

	for (q = 0; q < num_queues; q++) {
		tx_stats = &apc->tx_qp[q].txq.stats;

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

static int mana_get_rxnfc(struct net_device *ndev, struct ethtool_rxnfc *cmd,
			  u32 *rules)
{
	struct mana_port_context *apc = netdev_priv(ndev);

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = apc->num_queues;
		return 0;
	}

	return -EOPNOTSUPP;
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

static int mana_set_channels(struct net_device *ndev,
			     struct ethtool_channels *channels)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	unsigned int new_count = channels->combined_count;
	unsigned int old_count = apc->num_queues;
	int err;

	err = mana_pre_alloc_rxbufs(apc, ndev->mtu, new_count);
	if (err) {
		netdev_err(ndev, "Insufficient memory for new allocations");
		return err;
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
	cmd->base.duplex = DUPLEX_FULL;
	cmd->base.port = PORT_OTHER;

	return 0;
}

const struct ethtool_ops mana_ethtool_ops = {
	.get_ethtool_stats	= mana_get_ethtool_stats,
	.get_sset_count		= mana_get_sset_count,
	.get_strings		= mana_get_strings,
	.get_rxnfc		= mana_get_rxnfc,
	.get_rxfh_key_size	= mana_get_rxfh_key_size,
	.get_rxfh_indir_size	= mana_rss_indir_size,
	.get_rxfh		= mana_get_rxfh,
	.set_rxfh		= mana_set_rxfh,
	.get_channels		= mana_get_channels,
	.set_channels		= mana_set_channels,
	.get_ringparam          = mana_get_ringparam,
	.set_ringparam          = mana_set_ringparam,
	.get_link_ksettings	= mana_get_link_ksettings,
	.get_link		= ethtool_op_get_link,
};
