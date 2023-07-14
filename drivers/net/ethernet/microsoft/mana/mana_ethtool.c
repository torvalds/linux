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
	u8 *p = data;
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(mana_eth_stats); i++) {
		memcpy(p, mana_eth_stats[i].name, ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
	}

	for (i = 0; i < num_queues; i++) {
		sprintf(p, "rx_%d_packets", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "rx_%d_bytes", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "rx_%d_xdp_drop", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "rx_%d_xdp_tx", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "rx_%d_xdp_redirect", i);
		p += ETH_GSTRING_LEN;
	}

	for (i = 0; i < num_queues; i++) {
		sprintf(p, "tx_%d_packets", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_%d_bytes", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_%d_xdp_xmit", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_%d_tso_packets", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_%d_tso_bytes", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_%d_tso_inner_packets", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_%d_tso_inner_bytes", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_%d_long_pkt_fmt", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_%d_short_pkt_fmt", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_%d_csum_partial", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_%d_mana_map_err", i);
		p += ETH_GSTRING_LEN;
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
	return MANA_INDIRECT_TABLE_SIZE;
}

static int mana_get_rxfh(struct net_device *ndev, u32 *indir, u8 *key,
			 u8 *hfunc)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	int i;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP; /* Toeplitz */

	if (indir) {
		for (i = 0; i < MANA_INDIRECT_TABLE_SIZE; i++)
			indir[i] = apc->indir_table[i];
	}

	if (key)
		memcpy(key, apc->hashkey, MANA_HASH_KEY_SIZE);

	return 0;
}

static int mana_set_rxfh(struct net_device *ndev, const u32 *indir,
			 const u8 *key, const u8 hfunc)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	bool update_hash = false, update_table = false;
	u32 save_table[MANA_INDIRECT_TABLE_SIZE];
	u8 save_key[MANA_HASH_KEY_SIZE];
	int i, err;

	if (!apc->port_is_up)
		return -EOPNOTSUPP;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (indir) {
		for (i = 0; i < MANA_INDIRECT_TABLE_SIZE; i++)
			if (indir[i] >= apc->num_queues)
				return -EINVAL;

		update_table = true;
		for (i = 0; i < MANA_INDIRECT_TABLE_SIZE; i++) {
			save_table[i] = apc->indir_table[i];
			apc->indir_table[i] = indir[i];
		}
	}

	if (key) {
		update_hash = true;
		memcpy(save_key, apc->hashkey, MANA_HASH_KEY_SIZE);
		memcpy(apc->hashkey, key, MANA_HASH_KEY_SIZE);
	}

	err = mana_config_rss(apc, TRI_STATE_TRUE, update_hash, update_table);

	if (err) { /* recover to original values */
		if (update_table) {
			for (i = 0; i < MANA_INDIRECT_TABLE_SIZE; i++)
				apc->indir_table[i] = save_table[i];
		}

		if (update_hash)
			memcpy(apc->hashkey, save_key, MANA_HASH_KEY_SIZE);

		mana_config_rss(apc, TRI_STATE_TRUE, update_hash, update_table);
	}

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
	int err, err2;

	err = mana_detach(ndev, false);
	if (err) {
		netdev_err(ndev, "mana_detach failed: %d\n", err);
		return err;
	}

	apc->num_queues = new_count;
	err = mana_attach(ndev);
	if (!err)
		return 0;

	netdev_err(ndev, "mana_attach failed: %d\n", err);

	/* Try to roll it back to the old configuration. */
	apc->num_queues = old_count;
	err2 = mana_attach(ndev);
	if (err2)
		netdev_err(ndev, "mana re-attach failed: %d\n", err2);

	return err;
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
};
