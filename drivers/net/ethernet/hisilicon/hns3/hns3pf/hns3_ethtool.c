/*
 * Copyright (c) 2016~2017 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/etherdevice.h>
#include <linux/string.h>

#include "hns3_enet.h"

struct hns3_stats {
	char stats_string[ETH_GSTRING_LEN];
	int stats_size;
	int stats_offset;
};

/* tqp related stats */
#define HNS3_TQP_STAT(_string, _member)	{			\
	.stats_string = _string,				\
	.stats_size = FIELD_SIZEOF(struct ring_stats, _member),	\
	.stats_offset = offsetof(struct hns3_enet_ring, stats) +\
			offsetof(struct ring_stats, _member),   \
}								\

static const struct hns3_stats hns3_txq_stats[] = {
	/* Tx per-queue statistics */
	HNS3_TQP_STAT("tx_io_err_cnt", io_err_cnt),
	HNS3_TQP_STAT("tx_sw_err_cnt", sw_err_cnt),
	HNS3_TQP_STAT("tx_seg_pkt_cnt", seg_pkt_cnt),
	HNS3_TQP_STAT("tx_pkts", tx_pkts),
	HNS3_TQP_STAT("tx_bytes", tx_bytes),
	HNS3_TQP_STAT("tx_err_cnt", tx_err_cnt),
	HNS3_TQP_STAT("tx_restart_queue", restart_queue),
	HNS3_TQP_STAT("tx_busy", tx_busy),
};

#define HNS3_TXQ_STATS_COUNT ARRAY_SIZE(hns3_txq_stats)

static const struct hns3_stats hns3_rxq_stats[] = {
	/* Rx per-queue statistics */
	HNS3_TQP_STAT("rx_io_err_cnt", io_err_cnt),
	HNS3_TQP_STAT("rx_sw_err_cnt", sw_err_cnt),
	HNS3_TQP_STAT("rx_seg_pkt_cnt", seg_pkt_cnt),
	HNS3_TQP_STAT("rx_pkts", rx_pkts),
	HNS3_TQP_STAT("rx_bytes", rx_bytes),
	HNS3_TQP_STAT("rx_err_cnt", rx_err_cnt),
	HNS3_TQP_STAT("rx_reuse_pg_cnt", reuse_pg_cnt),
	HNS3_TQP_STAT("rx_err_pkt_len", err_pkt_len),
	HNS3_TQP_STAT("rx_non_vld_descs", non_vld_descs),
	HNS3_TQP_STAT("rx_err_bd_num", err_bd_num),
	HNS3_TQP_STAT("rx_l2_err", l2_err),
	HNS3_TQP_STAT("rx_l3l4_csum_err", l3l4_csum_err),
};

#define HNS3_RXQ_STATS_COUNT ARRAY_SIZE(hns3_rxq_stats)

#define HNS3_TQP_STATS_COUNT (HNS3_TXQ_STATS_COUNT + HNS3_RXQ_STATS_COUNT)

struct hns3_link_mode_mapping {
	u32 hns3_link_mode;
	u32 ethtool_link_mode;
};

static const struct hns3_link_mode_mapping hns3_lm_map[] = {
	{HNS3_LM_FIBRE_BIT, ETHTOOL_LINK_MODE_FIBRE_BIT},
	{HNS3_LM_AUTONEG_BIT, ETHTOOL_LINK_MODE_Autoneg_BIT},
	{HNS3_LM_TP_BIT, ETHTOOL_LINK_MODE_TP_BIT},
	{HNS3_LM_PAUSE_BIT, ETHTOOL_LINK_MODE_Pause_BIT},
	{HNS3_LM_BACKPLANE_BIT, ETHTOOL_LINK_MODE_Backplane_BIT},
	{HNS3_LM_10BASET_HALF_BIT, ETHTOOL_LINK_MODE_10baseT_Half_BIT},
	{HNS3_LM_10BASET_FULL_BIT, ETHTOOL_LINK_MODE_10baseT_Full_BIT},
	{HNS3_LM_100BASET_HALF_BIT, ETHTOOL_LINK_MODE_100baseT_Half_BIT},
	{HNS3_LM_100BASET_FULL_BIT, ETHTOOL_LINK_MODE_100baseT_Full_BIT},
	{HNS3_LM_1000BASET_FULL_BIT, ETHTOOL_LINK_MODE_1000baseT_Full_BIT},
};

static void hns3_driv_to_eth_caps(u32 caps, struct ethtool_link_ksettings *cmd,
				  bool is_advertised)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hns3_lm_map); i++) {
		if (!(caps & hns3_lm_map[i].hns3_link_mode))
			continue;

		if (is_advertised) {
			ethtool_link_ksettings_zero_link_mode(cmd,
							      advertising);
			__set_bit(hns3_lm_map[i].ethtool_link_mode,
				  cmd->link_modes.advertising);
		} else {
			ethtool_link_ksettings_zero_link_mode(cmd,
							      supported);
			__set_bit(hns3_lm_map[i].ethtool_link_mode,
				  cmd->link_modes.supported);
		}
	}
}

static int hns3_get_sset_count(struct net_device *netdev, int stringset)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	const struct hnae3_ae_ops *ops = h->ae_algo->ops;

	if (!ops->get_sset_count)
		return -EOPNOTSUPP;

	switch (stringset) {
	case ETH_SS_STATS:
		return ((HNS3_TQP_STATS_COUNT * h->kinfo.num_tqps) +
			ops->get_sset_count(h, stringset));

	case ETH_SS_TEST:
		return ops->get_sset_count(h, stringset);
	}

	return 0;
}

static void *hns3_update_strings(u8 *data, const struct hns3_stats *stats,
				 u32 stat_count, u32 num_tqps)
{
#define MAX_PREFIX_SIZE (8 + 4)
	u32 size_left;
	u32 i, j;
	u32 n1;

	for (i = 0; i < num_tqps; i++) {
		for (j = 0; j < stat_count; j++) {
			data[ETH_GSTRING_LEN - 1] = '\0';

			/* first, prepend the prefix string */
			n1 = snprintf(data, MAX_PREFIX_SIZE, "rcb_q%d_", i);
			n1 = min_t(uint, n1, MAX_PREFIX_SIZE - 1);
			size_left = (ETH_GSTRING_LEN - 1) - n1;

			/* now, concatenate the stats string to it */
			strncat(data, stats[j].stats_string, size_left);
			data += ETH_GSTRING_LEN;
		}
	}

	return data;
}

static u8 *hns3_get_strings_tqps(struct hnae3_handle *handle, u8 *data)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;

	/* get strings for Tx */
	data = hns3_update_strings(data, hns3_txq_stats, HNS3_TXQ_STATS_COUNT,
				   kinfo->num_tqps);

	/* get strings for Rx */
	data = hns3_update_strings(data, hns3_rxq_stats, HNS3_RXQ_STATS_COUNT,
				   kinfo->num_tqps);

	return data;
}

static void hns3_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	const struct hnae3_ae_ops *ops = h->ae_algo->ops;
	char *buff = (char *)data;

	if (!ops->get_strings)
		return;

	switch (stringset) {
	case ETH_SS_STATS:
		buff = hns3_get_strings_tqps(h, buff);
		h->ae_algo->ops->get_strings(h, stringset, (u8 *)buff);
		break;
	case ETH_SS_TEST:
		ops->get_strings(h, stringset, data);
		break;
	}
}

static u64 *hns3_get_stats_tqps(struct hnae3_handle *handle, u64 *data)
{
	struct hns3_nic_priv *nic_priv = (struct hns3_nic_priv *)handle->priv;
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hns3_enet_ring *ring;
	u8 *stat;
	int i, j;

	/* get stats for Tx */
	for (i = 0; i < kinfo->num_tqps; i++) {
		ring = nic_priv->ring_data[i].ring;
		for (j = 0; j < HNS3_TXQ_STATS_COUNT; j++) {
			stat = (u8 *)ring + hns3_txq_stats[j].stats_offset;
			*data++ = *(u64 *)stat;
		}
	}

	/* get stats for Rx */
	for (i = 0; i < kinfo->num_tqps; i++) {
		ring = nic_priv->ring_data[i + kinfo->num_tqps].ring;
		for (j = 0; j < HNS3_RXQ_STATS_COUNT; j++) {
			stat = (u8 *)ring + hns3_rxq_stats[j].stats_offset;
			*data++ = *(u64 *)stat;
		}
	}

	return data;
}

/* hns3_get_stats - get detail statistics.
 * @netdev: net device
 * @stats: statistics info.
 * @data: statistics data.
 */
void hns3_get_stats(struct net_device *netdev, struct ethtool_stats *stats,
		    u64 *data)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	u64 *p = data;

	if (!h->ae_algo->ops->get_stats || !h->ae_algo->ops->update_stats) {
		netdev_err(netdev, "could not get any statistics\n");
		return;
	}

	h->ae_algo->ops->update_stats(h, &netdev->stats);

	/* get per-queue stats */
	p = hns3_get_stats_tqps(h, p);

	/* get MAC & other misc hardware stats */
	h->ae_algo->ops->get_stats(h, p);
}

static void hns3_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;

	strncpy(drvinfo->version, hns3_driver_version,
		sizeof(drvinfo->version));
	drvinfo->version[sizeof(drvinfo->version) - 1] = '\0';

	strncpy(drvinfo->driver, h->pdev->driver->name,
		sizeof(drvinfo->driver));
	drvinfo->driver[sizeof(drvinfo->driver) - 1] = '\0';

	strncpy(drvinfo->bus_info, pci_name(h->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->bus_info[ETHTOOL_BUSINFO_LEN - 1] = '\0';

	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version), "0x%08x",
		 priv->ae_handle->ae_algo->ops->get_fw_version(h));
}

static u32 hns3_get_link(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h;

	h = priv->ae_handle;

	if (h->ae_algo && h->ae_algo->ops && h->ae_algo->ops->get_status)
		return h->ae_algo->ops->get_status(h);
	else
		return 0;
}

static void hns3_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *param)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int queue_num = priv->ae_handle->kinfo.num_tqps;

	param->tx_max_pending = HNS3_RING_MAX_PENDING;
	param->rx_max_pending = HNS3_RING_MAX_PENDING;

	param->tx_pending = priv->ring_data[0].ring->desc_num;
	param->rx_pending = priv->ring_data[queue_num].ring->desc_num;
}

static void hns3_get_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *param)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;

	if (h->ae_algo && h->ae_algo->ops && h->ae_algo->ops->get_pauseparam)
		h->ae_algo->ops->get_pauseparam(h, &param->autoneg,
			&param->rx_pause, &param->tx_pause);
}

static int hns3_get_link_ksettings(struct net_device *netdev,
				   struct ethtool_link_ksettings *cmd)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	u32 supported_caps;
	u32 advertised_caps;
	u8 media_type = HNAE3_MEDIA_TYPE_UNKNOWN;
	u8 link_stat;
	u8 auto_neg;
	u8 duplex;
	u32 speed;

	if (!h->ae_algo || !h->ae_algo->ops)
		return -EOPNOTSUPP;

	/* 1.auto_neg & speed & duplex from cmd */
	if (h->ae_algo->ops->get_ksettings_an_result) {
		h->ae_algo->ops->get_ksettings_an_result(h, &auto_neg,
							 &speed, &duplex);
		cmd->base.autoneg = auto_neg;
		cmd->base.speed = speed;
		cmd->base.duplex = duplex;

		link_stat = hns3_get_link(netdev);
		if (!link_stat) {
			cmd->base.speed = (u32)SPEED_UNKNOWN;
			cmd->base.duplex = DUPLEX_UNKNOWN;
		}
	}

	/* 2.media_type get from bios parameter block */
	if (h->ae_algo->ops->get_media_type) {
		h->ae_algo->ops->get_media_type(h, &media_type);

		switch (media_type) {
		case HNAE3_MEDIA_TYPE_FIBER:
			cmd->base.port = PORT_FIBRE;
			supported_caps = HNS3_LM_FIBRE_BIT |
					 HNS3_LM_AUTONEG_BIT |
					 HNS3_LM_PAUSE_BIT |
					 HNS3_LM_1000BASET_FULL_BIT;

			advertised_caps = supported_caps;
			break;
		case HNAE3_MEDIA_TYPE_COPPER:
			cmd->base.port = PORT_TP;
			supported_caps = HNS3_LM_TP_BIT |
					 HNS3_LM_AUTONEG_BIT |
					 HNS3_LM_PAUSE_BIT |
					 HNS3_LM_1000BASET_FULL_BIT |
					 HNS3_LM_100BASET_FULL_BIT |
					 HNS3_LM_100BASET_HALF_BIT |
					 HNS3_LM_10BASET_FULL_BIT |
					 HNS3_LM_10BASET_HALF_BIT;
			advertised_caps = supported_caps;
			break;
		case HNAE3_MEDIA_TYPE_BACKPLANE:
			cmd->base.port = PORT_NONE;
			supported_caps = HNS3_LM_BACKPLANE_BIT |
					 HNS3_LM_PAUSE_BIT |
					 HNS3_LM_AUTONEG_BIT |
					 HNS3_LM_1000BASET_FULL_BIT |
					 HNS3_LM_100BASET_FULL_BIT |
					 HNS3_LM_100BASET_HALF_BIT |
					 HNS3_LM_10BASET_FULL_BIT |
					 HNS3_LM_10BASET_HALF_BIT;

			advertised_caps = supported_caps;
			break;
		case HNAE3_MEDIA_TYPE_UNKNOWN:
		default:
			cmd->base.port = PORT_OTHER;
			supported_caps = 0;
			advertised_caps = 0;
			break;
		}

		if (!cmd->base.autoneg)
			advertised_caps &= ~HNS3_LM_AUTONEG_BIT;

		/* now, map driver link modes to ethtool link modes */
		hns3_driv_to_eth_caps(supported_caps, cmd, false);
		hns3_driv_to_eth_caps(advertised_caps, cmd, true);
	}

	/* 3.mdix_ctrl&mdix get from phy reg */
	if (h->ae_algo->ops->get_mdix_mode)
		h->ae_algo->ops->get_mdix_mode(h, &cmd->base.eth_tp_mdix_ctrl,
					       &cmd->base.eth_tp_mdix);
	/* 4.mdio_support */
	cmd->base.mdio_support = ETH_MDIO_SUPPORTS_C22;

	return 0;
}

static u32 hns3_get_rss_key_size(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;

	if (!h->ae_algo || !h->ae_algo->ops ||
	    !h->ae_algo->ops->get_rss_key_size)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->get_rss_key_size(h);
}

static u32 hns3_get_rss_indir_size(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;

	if (!h->ae_algo || !h->ae_algo->ops ||
	    !h->ae_algo->ops->get_rss_indir_size)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->get_rss_indir_size(h);
}

static int hns3_get_rss(struct net_device *netdev, u32 *indir, u8 *key,
			u8 *hfunc)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;

	if (!h->ae_algo || !h->ae_algo->ops || !h->ae_algo->ops->get_rss)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->get_rss(h, indir, key, hfunc);
}

static int hns3_set_rss(struct net_device *netdev, const u32 *indir,
			const u8 *key, const u8 hfunc)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;

	if (!h->ae_algo || !h->ae_algo->ops || !h->ae_algo->ops->set_rss)
		return -EOPNOTSUPP;

	/* currently we only support Toeplitz hash */
	if ((hfunc != ETH_RSS_HASH_NO_CHANGE) && (hfunc != ETH_RSS_HASH_TOP)) {
		netdev_err(netdev,
			   "hash func not supported (only Toeplitz hash)\n");
		return -EOPNOTSUPP;
	}
	if (!indir) {
		netdev_err(netdev,
			   "set rss failed for indir is empty\n");
		return -EOPNOTSUPP;
	}

	return h->ae_algo->ops->set_rss(h, indir, key, hfunc);
}

static int hns3_get_rxnfc(struct net_device *netdev,
			  struct ethtool_rxnfc *cmd,
			  u32 *rule_locs)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;

	if (!h->ae_algo || !h->ae_algo->ops || !h->ae_algo->ops->get_tc_size)
		return -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = h->ae_algo->ops->get_tc_size(h);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct ethtool_ops hns3_ethtool_ops = {
	.get_drvinfo = hns3_get_drvinfo,
	.get_link = hns3_get_link,
	.get_ringparam = hns3_get_ringparam,
	.get_pauseparam = hns3_get_pauseparam,
	.get_strings = hns3_get_strings,
	.get_ethtool_stats = hns3_get_stats,
	.get_sset_count = hns3_get_sset_count,
	.get_rxnfc = hns3_get_rxnfc,
	.get_rxfh_key_size = hns3_get_rss_key_size,
	.get_rxfh_indir_size = hns3_get_rss_indir_size,
	.get_rxfh = hns3_get_rss,
	.set_rxfh = hns3_set_rss,
	.get_link_ksettings = hns3_get_link_ksettings,
};

void hns3_ethtool_set_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &hns3_ethtool_ops;
}
