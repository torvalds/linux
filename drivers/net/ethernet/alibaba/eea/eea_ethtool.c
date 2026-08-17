// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/rtnetlink.h>

#include "eea_adminq.h"
#include "eea_net.h"
#include "eea_pci.h"

struct eea_stat_desc {
	char desc[ETH_GSTRING_LEN];
	size_t offset;
};

#define EEA_TX_STAT(m)	{#m, offsetof(struct eea_tx_stats, m)}
#define EEA_RX_STAT(m)	{#m, offsetof(struct eea_rx_stats, m)}

static const struct eea_stat_desc eea_rx_stats_desc[] = {
	EEA_RX_STAT(descs),
	EEA_RX_STAT(kicks),
};

static const struct eea_stat_desc eea_tx_stats_desc[] = {
	EEA_TX_STAT(descs),
	EEA_TX_STAT(kicks),
};

#define EEA_TX_STATS_LEN	ARRAY_SIZE(eea_tx_stats_desc)
#define EEA_RX_STATS_LEN	ARRAY_SIZE(eea_rx_stats_desc)

static void eea_get_drvinfo(struct net_device *netdev,
			    struct ethtool_drvinfo *info)
{
	struct eea_net *enet = netdev_priv(netdev);
	struct eea_device *edev = enet->edev;

	strscpy(info->driver,   KBUILD_MODNAME,     sizeof(info->driver));
	strscpy(info->bus_info, eea_pci_name(edev), sizeof(info->bus_info));
}

static void eea_get_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring,
			      struct kernel_ethtool_ringparam *kernel_ring,
			      struct netlink_ext_ack *extack)
{
	struct eea_net *enet = netdev_priv(netdev);

	ring->rx_max_pending = enet->cfg_hw.rx_ring_depth;
	ring->tx_max_pending = enet->cfg_hw.tx_ring_depth;
	ring->rx_pending = enet->cfg.rx_ring_depth;
	ring->tx_pending = enet->cfg.tx_ring_depth;

	kernel_ring->tcp_data_split = enet->cfg.split_hdr ?
				      ETHTOOL_TCP_DATA_SPLIT_ENABLED :
				      ETHTOOL_TCP_DATA_SPLIT_DISABLED;
}

static int eea_set_ringparam(struct net_device *netdev,
			     struct ethtool_ringparam *ring,
			     struct kernel_ethtool_ringparam *kernel_ring,
			     struct netlink_ext_ack *extack)
{
	struct eea_net *enet = netdev_priv(netdev);
	struct eea_net_init_ctx ctx;
	bool need_update = false;
	struct eea_net_cfg *cfg;
	bool sh;

	if (ring->rx_pending < EEA_NET_IO_RING_DEPTH_MIN ||
	    ring->tx_pending < EEA_NET_IO_RING_DEPTH_MIN)
		return -EINVAL;

	if (!is_power_of_2(ring->rx_pending) ||
	    !is_power_of_2(ring->tx_pending))
		return -EINVAL;

	eea_init_ctx(enet, &ctx);

	cfg = &ctx.cfg;

	if (ring->rx_pending != cfg->rx_ring_depth)
		need_update = true;

	if (ring->tx_pending != cfg->tx_ring_depth)
		need_update = true;

	sh = false;

	switch (kernel_ring->tcp_data_split) {
	case ETHTOOL_TCP_DATA_SPLIT_ENABLED:
		sh = true;
		break;

	case ETHTOOL_TCP_DATA_SPLIT_DISABLED:
		sh = false;
		break;

	case ETHTOOL_TCP_DATA_SPLIT_UNKNOWN:
		sh = !!cfg->split_hdr;
		break;
	}

	if (sh != !!(cfg->split_hdr))
		need_update = true;

	if (!need_update)
		return 0;

	cfg->rx_ring_depth = ring->rx_pending;
	cfg->tx_ring_depth = ring->tx_pending;

	/* By default, enet->cfg_hw.split_hdr is 128. */
	cfg->split_hdr = sh ? enet->cfg_hw.split_hdr : 0;

	return eea_reset_hw_resources(enet, &ctx);
}

static int eea_set_channels(struct net_device *netdev,
			    struct ethtool_channels *channels)
{
	struct eea_net *enet = netdev_priv(netdev);
	u16 queue_pairs = channels->combined_count;
	struct eea_net_init_ctx ctx;
	struct eea_net_cfg *cfg;

	eea_init_ctx(enet, &ctx);

	cfg = &ctx.cfg;

	cfg->rx_ring_num = queue_pairs;
	cfg->tx_ring_num = queue_pairs;

	return eea_reset_hw_resources(enet, &ctx);
}

static void eea_get_channels(struct net_device *netdev,
			     struct ethtool_channels *channels)
{
	struct eea_net *enet = netdev_priv(netdev);

	channels->combined_count = enet->cfg.rx_ring_num;
	channels->max_combined   = enet->cfg_hw.rx_ring_num;
}

static void eea_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct eea_net *enet = netdev_priv(netdev);
	u8 *p = data;
	u32 i, j;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < enet->cfg.rx_ring_num; i++) {
		for (j = 0; j < EEA_RX_STATS_LEN; j++)
			ethtool_sprintf(&p, "rx%u_%s", i,
					eea_rx_stats_desc[j].desc);
	}

	for (i = 0; i < enet->cfg.tx_ring_num; i++) {
		for (j = 0; j < EEA_TX_STATS_LEN; j++)
			ethtool_sprintf(&p, "tx%u_%s", i,
					eea_tx_stats_desc[j].desc);
	}
}

static int eea_get_sset_count(struct net_device *netdev, int sset)
{
	struct eea_net *enet = netdev_priv(netdev);

	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	return enet->cfg.rx_ring_num * EEA_RX_STATS_LEN +
		enet->cfg.tx_ring_num * EEA_TX_STATS_LEN;
}

static void eea_stats_fill_for_q(struct u64_stats_sync *syncp, u32 num,
				 const struct eea_stat_desc *desc,
				 u64 *data, u32 idx)
{
	void *stats_base = syncp;
	u32 start, i;

	do {
		start = u64_stats_fetch_begin(syncp);
		for (i = 0; i < num; i++)
			data[idx + i] =
				u64_stats_read(stats_base + desc[i].offset);

	} while (u64_stats_fetch_retry(syncp, start));

	BUILD_BUG_ON(offsetof(struct eea_tx_stats, syncp));
	BUILD_BUG_ON(offsetof(struct eea_rx_stats, syncp));
}

static void eea_get_ethtool_stats(struct net_device *netdev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct eea_net *enet = netdev_priv(netdev);
	u32 i, idx = 0;

	ASSERT_RTNL();

	if (enet->rx) {
		for (i = 0; i < enet->cfg.rx_ring_num; i++) {
			struct eea_net_rx *rx = enet->rx[i];

			eea_stats_fill_for_q(&rx->stats.syncp, EEA_RX_STATS_LEN,
					     eea_rx_stats_desc, data, idx);

			idx += EEA_RX_STATS_LEN;
		}
	}

	if (enet->tx) {
		for (i = 0; i < enet->cfg.tx_ring_num; i++) {
			struct eea_net_tx *tx = &enet->tx[i];

			eea_stats_fill_for_q(&tx->stats.syncp, EEA_TX_STATS_LEN,
					     eea_tx_stats_desc, data, idx);

			idx += EEA_TX_STATS_LEN;
		}
	}
}

void eea_update_rx_stats(struct eea_rx_stats *rx_stats,
			 struct eea_rx_ctx_stats *stats)
{
	u64_stats_update_begin(&rx_stats->syncp);
	u64_stats_add(&rx_stats->descs,             stats->descs);
	u64_stats_add(&rx_stats->packets,           stats->packets);
	u64_stats_add(&rx_stats->bytes,             stats->bytes);
	u64_stats_add(&rx_stats->drops,             stats->drops);
	u64_stats_add(&rx_stats->split_hdr_bytes,   stats->split_hdr_bytes);
	u64_stats_add(&rx_stats->split_hdr_packets, stats->split_hdr_packets);
	u64_stats_add(&rx_stats->length_errors,     stats->length_errors);
	u64_stats_add(&rx_stats->kicks,             stats->kicks);
	u64_stats_update_end(&rx_stats->syncp);
}

static int eea_get_link_ksettings(struct net_device *netdev,
				  struct ethtool_link_ksettings *cmd)
{
	struct eea_net *enet = netdev_priv(netdev);

	cmd->base.speed  = enet->speed;
	cmd->base.duplex = enet->duplex;
	cmd->base.port   = PORT_OTHER;

	return 0;
}

const struct ethtool_ops eea_ethtool_ops = {
	.supported_ring_params = ETHTOOL_RING_USE_TCP_DATA_SPLIT,
	.get_drvinfo        = eea_get_drvinfo,
	.get_link           = ethtool_op_get_link,
	.get_ringparam      = eea_get_ringparam,
	.set_ringparam      = eea_set_ringparam,
	.set_channels       = eea_set_channels,
	.get_channels       = eea_get_channels,
	.get_strings        = eea_get_strings,
	.get_sset_count     = eea_get_sset_count,
	.get_ethtool_stats  = eea_get_ethtool_stats,
	.get_link_ksettings = eea_get_link_ksettings,
};
