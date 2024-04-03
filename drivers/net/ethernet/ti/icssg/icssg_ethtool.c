// SPDX-License-Identifier: GPL-2.0
/* Texas Instruments ICSSG Ethernet driver
 *
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#include "icssg_prueth.h"
#include "icssg_stats.h"

static void emac_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *info)
{
	struct prueth_emac *emac = netdev_priv(ndev);
	struct prueth *prueth = emac->prueth;

	strscpy(info->driver, dev_driver_string(prueth->dev),
		sizeof(info->driver));
	strscpy(info->bus_info, dev_name(prueth->dev), sizeof(info->bus_info));
}

static u32 emac_get_msglevel(struct net_device *ndev)
{
	struct prueth_emac *emac = netdev_priv(ndev);

	return emac->msg_enable;
}

static void emac_set_msglevel(struct net_device *ndev, u32 value)
{
	struct prueth_emac *emac = netdev_priv(ndev);

	emac->msg_enable = value;
}

static int emac_get_link_ksettings(struct net_device *ndev,
				   struct ethtool_link_ksettings *ecmd)
{
	return phy_ethtool_get_link_ksettings(ndev, ecmd);
}

static int emac_set_link_ksettings(struct net_device *ndev,
				   const struct ethtool_link_ksettings *ecmd)
{
	return phy_ethtool_set_link_ksettings(ndev, ecmd);
}

static int emac_get_eee(struct net_device *ndev, struct ethtool_keee *edata)
{
	if (!ndev->phydev)
		return -EOPNOTSUPP;

	return phy_ethtool_get_eee(ndev->phydev, edata);
}

static int emac_set_eee(struct net_device *ndev, struct ethtool_keee *edata)
{
	if (!ndev->phydev)
		return -EOPNOTSUPP;

	return phy_ethtool_set_eee(ndev->phydev, edata);
}

static int emac_nway_reset(struct net_device *ndev)
{
	return phy_ethtool_nway_reset(ndev);
}

static int emac_get_sset_count(struct net_device *ndev, int stringset)
{
	switch (stringset) {
	case ETH_SS_STATS:
		return ICSSG_NUM_ETHTOOL_STATS;
	default:
		return -EOPNOTSUPP;
	}
}

static void emac_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(icssg_all_stats); i++) {
			if (!icssg_all_stats[i].standard_stats) {
				memcpy(p, icssg_all_stats[i].name,
				       ETH_GSTRING_LEN);
				p += ETH_GSTRING_LEN;
			}
		}
		break;
	default:
		break;
	}
}

static void emac_get_ethtool_stats(struct net_device *ndev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct prueth_emac *emac = netdev_priv(ndev);
	int i;

	emac_update_hardware_stats(emac);

	for (i = 0; i < ARRAY_SIZE(icssg_all_stats); i++)
		if (!icssg_all_stats[i].standard_stats)
			*(data++) = emac->stats[i];
}

static int emac_get_ts_info(struct net_device *ndev,
			    struct ethtool_ts_info *info)
{
	struct prueth_emac *emac = netdev_priv(ndev);

	info->so_timestamping =
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;

	info->phc_index = icss_iep_get_ptp_clock_idx(emac->iep);
	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) | BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

static int emac_set_channels(struct net_device *ndev,
			     struct ethtool_channels *ch)
{
	struct prueth_emac *emac = netdev_priv(ndev);

	/* Check if interface is up. Can change the num queues when
	 * the interface is down.
	 */
	if (netif_running(emac->ndev))
		return -EBUSY;

	emac->tx_ch_num = ch->tx_count;

	return 0;
}

static void emac_get_channels(struct net_device *ndev,
			      struct ethtool_channels *ch)
{
	struct prueth_emac *emac = netdev_priv(ndev);

	ch->max_rx = 1;
	ch->max_tx = PRUETH_MAX_TX_QUEUES;
	ch->rx_count = 1;
	ch->tx_count = emac->tx_ch_num;
}

static const struct ethtool_rmon_hist_range emac_rmon_ranges[] = {
	{    0,   64},
	{   65,  128},
	{  129,  256},
	{  257,  512},
	{  513, PRUETH_MAX_PKT_SIZE},
	{}
};

static void emac_get_rmon_stats(struct net_device *ndev,
				struct ethtool_rmon_stats *rmon_stats,
				const struct ethtool_rmon_hist_range **ranges)
{
	struct prueth_emac *emac = netdev_priv(ndev);

	*ranges = emac_rmon_ranges;

	rmon_stats->undersize_pkts = emac_get_stat_by_name(emac, "rx_bucket1_frames") -
				     emac_get_stat_by_name(emac, "rx_64B_frames");

	rmon_stats->hist[0] = emac_get_stat_by_name(emac, "rx_bucket1_frames");
	rmon_stats->hist[1] = emac_get_stat_by_name(emac, "rx_bucket2_frames");
	rmon_stats->hist[2] = emac_get_stat_by_name(emac, "rx_bucket3_frames");
	rmon_stats->hist[3] = emac_get_stat_by_name(emac, "rx_bucket4_frames");
	rmon_stats->hist[4] = emac_get_stat_by_name(emac, "rx_bucket5_frames");

	rmon_stats->hist_tx[0] = emac_get_stat_by_name(emac, "tx_bucket1_frames");
	rmon_stats->hist_tx[1] = emac_get_stat_by_name(emac, "tx_bucket2_frames");
	rmon_stats->hist_tx[2] = emac_get_stat_by_name(emac, "tx_bucket3_frames");
	rmon_stats->hist_tx[3] = emac_get_stat_by_name(emac, "tx_bucket4_frames");
	rmon_stats->hist_tx[4] = emac_get_stat_by_name(emac, "tx_bucket5_frames");
}

const struct ethtool_ops icssg_ethtool_ops = {
	.get_drvinfo = emac_get_drvinfo,
	.get_msglevel = emac_get_msglevel,
	.set_msglevel = emac_set_msglevel,
	.get_sset_count = emac_get_sset_count,
	.get_ethtool_stats = emac_get_ethtool_stats,
	.get_strings = emac_get_strings,
	.get_ts_info = emac_get_ts_info,
	.get_channels = emac_get_channels,
	.set_channels = emac_set_channels,
	.get_link_ksettings = emac_get_link_ksettings,
	.set_link_ksettings = emac_set_link_ksettings,
	.get_link = ethtool_op_get_link,
	.get_eee = emac_get_eee,
	.set_eee = emac_set_eee,
	.nway_reset = emac_nway_reset,
	.get_rmon_stats = emac_get_rmon_stats,
};
