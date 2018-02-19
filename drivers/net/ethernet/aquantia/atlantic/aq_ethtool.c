/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_ethtool.c: Definition of ethertool related functions. */

#include "aq_ethtool.h"
#include "aq_nic.h"

static void aq_ethtool_get_regs(struct net_device *ndev,
				struct ethtool_regs *regs, void *p)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	u32 regs_count = aq_nic_get_regs_count(aq_nic);

	memset(p, 0, regs_count * sizeof(u32));
	aq_nic_get_regs(aq_nic, regs, p);
}

static int aq_ethtool_get_regs_len(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	u32 regs_count = aq_nic_get_regs_count(aq_nic);

	return regs_count * sizeof(u32);
}

static u32 aq_ethtool_get_link(struct net_device *ndev)
{
	return ethtool_op_get_link(ndev);
}

static int aq_ethtool_get_link_ksettings(struct net_device *ndev,
					 struct ethtool_link_ksettings *cmd)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	aq_nic_get_link_ksettings(aq_nic, cmd);
	cmd->base.speed = netif_carrier_ok(ndev) ?
				aq_nic_get_link_speed(aq_nic) : 0U;

	return 0;
}

static int
aq_ethtool_set_link_ksettings(struct net_device *ndev,
			      const struct ethtool_link_ksettings *cmd)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	return aq_nic_set_link_ksettings(aq_nic, cmd);
}

static const char aq_ethtool_stat_names[][ETH_GSTRING_LEN] = {
	"InPackets",
	"InUCast",
	"InMCast",
	"InBCast",
	"InErrors",
	"OutPackets",
	"OutUCast",
	"OutMCast",
	"OutBCast",
	"InUCastOctets",
	"OutUCastOctets",
	"InMCastOctets",
	"OutMCastOctets",
	"InBCastOctets",
	"OutBCastOctets",
	"InOctets",
	"OutOctets",
	"InPacketsDma",
	"OutPacketsDma",
	"InOctetsDma",
	"OutOctetsDma",
	"InDroppedDma",
};

static const char aq_ethtool_queue_stat_names[][ETH_GSTRING_LEN] = {
	"Queue[%d] InPackets",
	"Queue[%d] OutPackets",
	"Queue[%d] Restarts",
	"Queue[%d] InJumboPackets",
	"Queue[%d] InLroPackets",
	"Queue[%d] InErrors",
};

static void aq_ethtool_stats(struct net_device *ndev,
			     struct ethtool_stats *stats, u64 *data)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg = aq_nic_get_cfg(aq_nic);

	memset(data, 0, (ARRAY_SIZE(aq_ethtool_stat_names) +
				ARRAY_SIZE(aq_ethtool_queue_stat_names) *
				cfg->vecs) * sizeof(u64));
	aq_nic_get_stats(aq_nic, data);
}

static void aq_ethtool_get_drvinfo(struct net_device *ndev,
				   struct ethtool_drvinfo *drvinfo)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg = aq_nic_get_cfg(aq_nic);
	struct pci_dev *pdev = to_pci_dev(ndev->dev.parent);
	u32 firmware_version = aq_nic_get_fw_version(aq_nic);
	u32 regs_count = aq_nic_get_regs_count(aq_nic);

	strlcat(drvinfo->driver, AQ_CFG_DRV_NAME, sizeof(drvinfo->driver));
	strlcat(drvinfo->version, AQ_CFG_DRV_VERSION, sizeof(drvinfo->version));

	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%u.%u.%u", firmware_version >> 24,
		 (firmware_version >> 16) & 0xFFU, firmware_version & 0xFFFFU);

	strlcpy(drvinfo->bus_info, pdev ? pci_name(pdev) : "",
		sizeof(drvinfo->bus_info));
	drvinfo->n_stats = ARRAY_SIZE(aq_ethtool_stat_names) +
		cfg->vecs * ARRAY_SIZE(aq_ethtool_queue_stat_names);
	drvinfo->testinfo_len = 0;
	drvinfo->regdump_len = regs_count;
	drvinfo->eedump_len = 0;
}

static void aq_ethtool_get_strings(struct net_device *ndev,
				   u32 stringset, u8 *data)
{
	int i, si;
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg = aq_nic_get_cfg(aq_nic);
	u8 *p = data;

	if (stringset == ETH_SS_STATS) {
		memcpy(p, *aq_ethtool_stat_names,
		       sizeof(aq_ethtool_stat_names));
		p = p + sizeof(aq_ethtool_stat_names);
		for (i = 0; i < cfg->vecs; i++) {
			for (si = 0;
				si < ARRAY_SIZE(aq_ethtool_queue_stat_names);
				si++) {
				snprintf(p, ETH_GSTRING_LEN,
					 aq_ethtool_queue_stat_names[si], i);
				p += ETH_GSTRING_LEN;
			}
		}
	}
}

static int aq_ethtool_get_sset_count(struct net_device *ndev, int stringset)
{
	int ret = 0;
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg = aq_nic_get_cfg(aq_nic);

	switch (stringset) {
	case ETH_SS_STATS:
		ret = ARRAY_SIZE(aq_ethtool_stat_names) +
			cfg->vecs * ARRAY_SIZE(aq_ethtool_queue_stat_names);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	return ret;
}

static u32 aq_ethtool_get_rss_indir_size(struct net_device *ndev)
{
	return AQ_CFG_RSS_INDIRECTION_TABLE_MAX;
}

static u32 aq_ethtool_get_rss_key_size(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg = aq_nic_get_cfg(aq_nic);

	return sizeof(cfg->aq_rss.hash_secret_key);
}

static int aq_ethtool_get_rss(struct net_device *ndev, u32 *indir, u8 *key,
			      u8 *hfunc)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg = aq_nic_get_cfg(aq_nic);
	unsigned int i = 0U;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP; /* Toeplitz */
	if (indir) {
		for (i = 0; i < AQ_CFG_RSS_INDIRECTION_TABLE_MAX; i++)
			indir[i] = cfg->aq_rss.indirection_table[i];
	}
	if (key)
		memcpy(key, cfg->aq_rss.hash_secret_key,
		       sizeof(cfg->aq_rss.hash_secret_key));
	return 0;
}

static int aq_ethtool_get_rxnfc(struct net_device *ndev,
				struct ethtool_rxnfc *cmd,
				u32 *rule_locs)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg = aq_nic_get_cfg(aq_nic);
	int err = 0;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = cfg->vecs;
		break;

	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int aq_ethtool_get_coalesce(struct net_device *ndev,
				   struct ethtool_coalesce *coal)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg = aq_nic_get_cfg(aq_nic);

	if (cfg->itr == AQ_CFG_INTERRUPT_MODERATION_ON ||
	    cfg->itr == AQ_CFG_INTERRUPT_MODERATION_AUTO) {
		coal->rx_coalesce_usecs = cfg->rx_itr;
		coal->tx_coalesce_usecs = cfg->tx_itr;
		coal->rx_max_coalesced_frames = 0;
		coal->tx_max_coalesced_frames = 0;
	} else {
		coal->rx_coalesce_usecs = 0;
		coal->tx_coalesce_usecs = 0;
		coal->rx_max_coalesced_frames = 1;
		coal->tx_max_coalesced_frames = 1;
	}
	return 0;
}

static int aq_ethtool_set_coalesce(struct net_device *ndev,
				   struct ethtool_coalesce *coal)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg = aq_nic_get_cfg(aq_nic);

	/* This is not yet supported
	 */
	if (coal->use_adaptive_rx_coalesce || coal->use_adaptive_tx_coalesce)
		return -EOPNOTSUPP;

	/* Atlantic only supports timing based coalescing
	 */
	if (coal->rx_max_coalesced_frames > 1 ||
	    coal->rx_coalesce_usecs_irq ||
	    coal->rx_max_coalesced_frames_irq)
		return -EOPNOTSUPP;

	if (coal->tx_max_coalesced_frames > 1 ||
	    coal->tx_coalesce_usecs_irq ||
	    coal->tx_max_coalesced_frames_irq)
		return -EOPNOTSUPP;

	/* We do not support frame counting. Check this
	 */
	if (!(coal->rx_max_coalesced_frames == !coal->rx_coalesce_usecs))
		return -EOPNOTSUPP;
	if (!(coal->tx_max_coalesced_frames == !coal->tx_coalesce_usecs))
		return -EOPNOTSUPP;

	if (coal->rx_coalesce_usecs > AQ_CFG_INTERRUPT_MODERATION_USEC_MAX ||
	    coal->tx_coalesce_usecs > AQ_CFG_INTERRUPT_MODERATION_USEC_MAX)
		return -EINVAL;

	cfg->itr = AQ_CFG_INTERRUPT_MODERATION_ON;

	cfg->rx_itr = coal->rx_coalesce_usecs;
	cfg->tx_itr = coal->tx_coalesce_usecs;

	return aq_nic_update_interrupt_moderation_settings(aq_nic);
}

const struct ethtool_ops aq_ethtool_ops = {
	.get_link            = aq_ethtool_get_link,
	.get_regs_len        = aq_ethtool_get_regs_len,
	.get_regs            = aq_ethtool_get_regs,
	.get_drvinfo         = aq_ethtool_get_drvinfo,
	.get_strings         = aq_ethtool_get_strings,
	.get_rxfh_indir_size = aq_ethtool_get_rss_indir_size,
	.get_rxfh_key_size   = aq_ethtool_get_rss_key_size,
	.get_rxfh            = aq_ethtool_get_rss,
	.get_rxnfc           = aq_ethtool_get_rxnfc,
	.get_sset_count      = aq_ethtool_get_sset_count,
	.get_ethtool_stats   = aq_ethtool_stats,
	.get_link_ksettings  = aq_ethtool_get_link_ksettings,
	.set_link_ksettings  = aq_ethtool_set_link_ksettings,
	.get_coalesce	     = aq_ethtool_get_coalesce,
	.set_coalesce	     = aq_ethtool_set_coalesce,
};
