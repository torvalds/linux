// SPDX-License-Identifier: GPL-2.0-only
/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2019 aQuantia Corporation. All rights reserved
 */

/* File aq_ethtool.c: Definition of ethertool related functions. */

#include "aq_ethtool.h"
#include "aq_nic.h"
#include "aq_vec.h"
#include "aq_ptp.h"
#include "aq_filters.h"

#include <linux/ptp_clock_kernel.h>

static void aq_ethtool_get_regs(struct net_device *ndev,
				struct ethtool_regs *regs, void *p)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	u32 regs_count;

	regs_count = aq_nic_get_regs_count(aq_nic);

	memset(p, 0, regs_count * sizeof(u32));
	aq_nic_get_regs(aq_nic, regs, p);
}

static int aq_ethtool_get_regs_len(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	u32 regs_count;

	regs_count = aq_nic_get_regs_count(aq_nic);

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

static const char aq_ethtool_priv_flag_names[][ETH_GSTRING_LEN] = {
	"DMASystemLoopback",
	"PKTSystemLoopback",
	"DMANetworkLoopback",
	"PHYInternalLoopback",
	"PHYExternalLoopback",
};

static void aq_ethtool_stats(struct net_device *ndev,
			     struct ethtool_stats *stats, u64 *data)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg;

	cfg = aq_nic_get_cfg(aq_nic);

	memset(data, 0, (ARRAY_SIZE(aq_ethtool_stat_names) +
			 ARRAY_SIZE(aq_ethtool_queue_stat_names) *
			 cfg->vecs) * sizeof(u64));
	aq_nic_get_stats(aq_nic, data);
}

static void aq_ethtool_get_drvinfo(struct net_device *ndev,
				   struct ethtool_drvinfo *drvinfo)
{
	struct pci_dev *pdev = to_pci_dev(ndev->dev.parent);
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg;
	u32 firmware_version;
	u32 regs_count;

	cfg = aq_nic_get_cfg(aq_nic);
	firmware_version = aq_nic_get_fw_version(aq_nic);
	regs_count = aq_nic_get_regs_count(aq_nic);

	strlcat(drvinfo->driver, AQ_CFG_DRV_NAME, sizeof(drvinfo->driver));

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
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg;
	u8 *p = data;
	int i, si;

	cfg = aq_nic_get_cfg(aq_nic);

	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(p, aq_ethtool_stat_names,
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
		break;
	case ETH_SS_PRIV_FLAGS:
		memcpy(p, aq_ethtool_priv_flag_names,
		       sizeof(aq_ethtool_priv_flag_names));
		break;
	}
}

static int aq_ethtool_set_phys_id(struct net_device *ndev,
				  enum ethtool_phys_id_state state)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_hw_s *hw = aq_nic->aq_hw;
	int ret = 0;

	if (!aq_nic->aq_fw_ops->led_control)
		return -EOPNOTSUPP;

	mutex_lock(&aq_nic->fwreq_mutex);

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		ret = aq_nic->aq_fw_ops->led_control(hw, AQ_HW_LED_BLINK |
				 AQ_HW_LED_BLINK << 2 | AQ_HW_LED_BLINK << 4);
		break;
	case ETHTOOL_ID_INACTIVE:
		ret = aq_nic->aq_fw_ops->led_control(hw, AQ_HW_LED_DEFAULT);
		break;
	default:
		break;
	}

	mutex_unlock(&aq_nic->fwreq_mutex);

	return ret;
}

static int aq_ethtool_get_sset_count(struct net_device *ndev, int stringset)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg;
	int ret = 0;

	cfg = aq_nic_get_cfg(aq_nic);

	switch (stringset) {
	case ETH_SS_STATS:
		ret = ARRAY_SIZE(aq_ethtool_stat_names) +
			cfg->vecs * ARRAY_SIZE(aq_ethtool_queue_stat_names);
		break;
	case ETH_SS_PRIV_FLAGS:
		ret = ARRAY_SIZE(aq_ethtool_priv_flag_names);
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
	struct aq_nic_cfg_s *cfg;

	cfg = aq_nic_get_cfg(aq_nic);

	return sizeof(cfg->aq_rss.hash_secret_key);
}

static int aq_ethtool_get_rss(struct net_device *ndev, u32 *indir, u8 *key,
			      u8 *hfunc)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg;
	unsigned int i = 0U;

	cfg = aq_nic_get_cfg(aq_nic);

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

static int aq_ethtool_set_rss(struct net_device *netdev, const u32 *indir,
			      const u8 *key, const u8 hfunc)
{
	struct aq_nic_s *aq_nic = netdev_priv(netdev);
	struct aq_nic_cfg_s *cfg;
	unsigned int i = 0U;
	u32 rss_entries;
	int err = 0;

	cfg = aq_nic_get_cfg(aq_nic);
	rss_entries = cfg->aq_rss.indirection_table_size;

	/* We do not allow change in unsupported parameters */
	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;
	/* Fill out the redirection table */
	if (indir)
		for (i = 0; i < rss_entries; i++)
			cfg->aq_rss.indirection_table[i] = indir[i];

	/* Fill out the rss hash key */
	if (key) {
		memcpy(cfg->aq_rss.hash_secret_key, key,
		       sizeof(cfg->aq_rss.hash_secret_key));
		err = aq_nic->aq_hw_ops->hw_rss_hash_set(aq_nic->aq_hw,
			&cfg->aq_rss);
		if (err)
			return err;
	}

	err = aq_nic->aq_hw_ops->hw_rss_set(aq_nic->aq_hw, &cfg->aq_rss);

	return err;
}

static int aq_ethtool_get_rxnfc(struct net_device *ndev,
				struct ethtool_rxnfc *cmd,
				u32 *rule_locs)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg;
	int err = 0;

	cfg = aq_nic_get_cfg(aq_nic);

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = cfg->vecs;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = aq_get_rxnfc_count_all_rules(aq_nic);
		break;
	case ETHTOOL_GRXCLSRULE:
		err = aq_get_rxnfc_rule(aq_nic, cmd);
		break;
	case ETHTOOL_GRXCLSRLALL:
		err = aq_get_rxnfc_all_rules(aq_nic, cmd, rule_locs);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int aq_ethtool_set_rxnfc(struct net_device *ndev,
				struct ethtool_rxnfc *cmd)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	int err = 0;

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		err = aq_add_rxnfc_rule(aq_nic, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		err = aq_del_rxnfc_rule(aq_nic, cmd);
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
	struct aq_nic_cfg_s *cfg;

	cfg = aq_nic_get_cfg(aq_nic);

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
	struct aq_nic_cfg_s *cfg;

	cfg = aq_nic_get_cfg(aq_nic);

	/* Atlantic only supports timing based coalescing
	 */
	if (coal->rx_max_coalesced_frames > 1 ||
	    coal->tx_max_coalesced_frames > 1)
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

static void aq_ethtool_get_wol(struct net_device *ndev,
			       struct ethtool_wolinfo *wol)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg;

	cfg = aq_nic_get_cfg(aq_nic);

	wol->supported = AQ_NIC_WOL_MODES;
	wol->wolopts = cfg->wol;
}

static int aq_ethtool_set_wol(struct net_device *ndev,
			      struct ethtool_wolinfo *wol)
{
	struct pci_dev *pdev = to_pci_dev(ndev->dev.parent);
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg;
	int err = 0;

	cfg = aq_nic_get_cfg(aq_nic);

	if (wol->wolopts & ~AQ_NIC_WOL_MODES)
		return -EOPNOTSUPP;

	cfg->wol = wol->wolopts;

	err = device_set_wakeup_enable(&pdev->dev, !!cfg->wol);

	return err;
}

static int aq_ethtool_get_ts_info(struct net_device *ndev,
				  struct ethtool_ts_info *info)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	ethtool_op_get_ts_info(ndev, info);

	if (!aq_nic->aq_ptp)
		return 0;

	info->so_timestamping |=
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;

	info->tx_types = BIT(HWTSTAMP_TX_OFF) |
			 BIT(HWTSTAMP_TX_ON);

	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE);

	info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_EVENT);

	info->phc_index = ptp_clock_index(aq_ptp_get_ptp_clock(aq_nic->aq_ptp));

	return 0;
}

static enum hw_atl_fw2x_rate eee_mask_to_ethtool_mask(u32 speed)
{
	u32 rate = 0;

	if (speed & AQ_NIC_RATE_EEE_10G)
		rate |= SUPPORTED_10000baseT_Full;

	if (speed & AQ_NIC_RATE_EEE_2GS)
		rate |= SUPPORTED_2500baseX_Full;

	if (speed & AQ_NIC_RATE_EEE_1G)
		rate |= SUPPORTED_1000baseT_Full;

	return rate;
}

static int aq_ethtool_get_eee(struct net_device *ndev, struct ethtool_eee *eee)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	u32 rate, supported_rates;
	int err = 0;

	if (!aq_nic->aq_fw_ops->get_eee_rate)
		return -EOPNOTSUPP;

	mutex_lock(&aq_nic->fwreq_mutex);
	err = aq_nic->aq_fw_ops->get_eee_rate(aq_nic->aq_hw, &rate,
					      &supported_rates);
	mutex_unlock(&aq_nic->fwreq_mutex);
	if (err < 0)
		return err;

	eee->supported = eee_mask_to_ethtool_mask(supported_rates);

	if (aq_nic->aq_nic_cfg.eee_speeds)
		eee->advertised = eee->supported;

	eee->lp_advertised = eee_mask_to_ethtool_mask(rate);

	eee->eee_enabled = !!eee->advertised;

	eee->tx_lpi_enabled = eee->eee_enabled;
	if (eee->advertised & eee->lp_advertised)
		eee->eee_active = true;

	return 0;
}

static int aq_ethtool_set_eee(struct net_device *ndev, struct ethtool_eee *eee)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	u32 rate, supported_rates;
	struct aq_nic_cfg_s *cfg;
	int err = 0;

	cfg = aq_nic_get_cfg(aq_nic);

	if (unlikely(!aq_nic->aq_fw_ops->get_eee_rate ||
		     !aq_nic->aq_fw_ops->set_eee_rate))
		return -EOPNOTSUPP;

	mutex_lock(&aq_nic->fwreq_mutex);
	err = aq_nic->aq_fw_ops->get_eee_rate(aq_nic->aq_hw, &rate,
					      &supported_rates);
	mutex_unlock(&aq_nic->fwreq_mutex);
	if (err < 0)
		return err;

	if (eee->eee_enabled) {
		rate = supported_rates;
		cfg->eee_speeds = rate;
	} else {
		rate = 0;
		cfg->eee_speeds = 0;
	}

	mutex_lock(&aq_nic->fwreq_mutex);
	err = aq_nic->aq_fw_ops->set_eee_rate(aq_nic->aq_hw, rate);
	mutex_unlock(&aq_nic->fwreq_mutex);

	return err;
}

static int aq_ethtool_nway_reset(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	int err = 0;

	if (unlikely(!aq_nic->aq_fw_ops->renegotiate))
		return -EOPNOTSUPP;

	if (netif_running(ndev)) {
		mutex_lock(&aq_nic->fwreq_mutex);
		err = aq_nic->aq_fw_ops->renegotiate(aq_nic->aq_hw);
		mutex_unlock(&aq_nic->fwreq_mutex);
	}

	return err;
}

static void aq_ethtool_get_pauseparam(struct net_device *ndev,
				      struct ethtool_pauseparam *pause)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	u32 fc = aq_nic->aq_nic_cfg.fc.req;

	pause->autoneg = 0;

	pause->rx_pause = !!(fc & AQ_NIC_FC_RX);
	pause->tx_pause = !!(fc & AQ_NIC_FC_TX);

}

static int aq_ethtool_set_pauseparam(struct net_device *ndev,
				     struct ethtool_pauseparam *pause)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	int err = 0;

	if (!aq_nic->aq_fw_ops->set_flow_control)
		return -EOPNOTSUPP;

	if (pause->autoneg == AUTONEG_ENABLE)
		return -EOPNOTSUPP;

	if (pause->rx_pause)
		aq_nic->aq_hw->aq_nic_cfg->fc.req |= AQ_NIC_FC_RX;
	else
		aq_nic->aq_hw->aq_nic_cfg->fc.req &= ~AQ_NIC_FC_RX;

	if (pause->tx_pause)
		aq_nic->aq_hw->aq_nic_cfg->fc.req |= AQ_NIC_FC_TX;
	else
		aq_nic->aq_hw->aq_nic_cfg->fc.req &= ~AQ_NIC_FC_TX;

	mutex_lock(&aq_nic->fwreq_mutex);
	err = aq_nic->aq_fw_ops->set_flow_control(aq_nic->aq_hw);
	mutex_unlock(&aq_nic->fwreq_mutex);

	return err;
}

static void aq_get_ringparam(struct net_device *ndev,
			     struct ethtool_ringparam *ring)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg;

	cfg = aq_nic_get_cfg(aq_nic);

	ring->rx_pending = cfg->rxds;
	ring->tx_pending = cfg->txds;

	ring->rx_max_pending = cfg->aq_hw_caps->rxds_max;
	ring->tx_max_pending = cfg->aq_hw_caps->txds_max;
}

static int aq_set_ringparam(struct net_device *ndev,
			    struct ethtool_ringparam *ring)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	const struct aq_hw_caps_s *hw_caps;
	bool ndev_running = false;
	struct aq_nic_cfg_s *cfg;
	int err = 0;

	cfg = aq_nic_get_cfg(aq_nic);
	hw_caps = cfg->aq_hw_caps;

	if (ring->rx_mini_pending || ring->rx_jumbo_pending) {
		err = -EOPNOTSUPP;
		goto err_exit;
	}

	if (netif_running(ndev)) {
		ndev_running = true;
		dev_close(ndev);
	}

	aq_nic_free_vectors(aq_nic);

	cfg->rxds = max(ring->rx_pending, hw_caps->rxds_min);
	cfg->rxds = min(cfg->rxds, hw_caps->rxds_max);
	cfg->rxds = ALIGN(cfg->rxds, AQ_HW_RXD_MULTIPLE);

	cfg->txds = max(ring->tx_pending, hw_caps->txds_min);
	cfg->txds = min(cfg->txds, hw_caps->txds_max);
	cfg->txds = ALIGN(cfg->txds, AQ_HW_TXD_MULTIPLE);

	for (aq_nic->aq_vecs = 0; aq_nic->aq_vecs < cfg->vecs;
	     aq_nic->aq_vecs++) {
		aq_nic->aq_vec[aq_nic->aq_vecs] =
		    aq_vec_alloc(aq_nic, aq_nic->aq_vecs, cfg);
		if (unlikely(!aq_nic->aq_vec[aq_nic->aq_vecs])) {
			err = -ENOMEM;
			goto err_exit;
		}
	}
	if (ndev_running)
		err = dev_open(ndev, NULL);

err_exit:
	return err;
}

static u32 aq_get_msg_level(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	return aq_nic->msg_enable;
}

static void aq_set_msg_level(struct net_device *ndev, u32 data)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	aq_nic->msg_enable = data;
}

static u32 aq_ethtool_get_priv_flags(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	return aq_nic->aq_nic_cfg.priv_flags;
}

static int aq_ethtool_set_priv_flags(struct net_device *ndev, u32 flags)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct aq_nic_cfg_s *cfg;
	u32 priv_flags;

	cfg = aq_nic_get_cfg(aq_nic);
	priv_flags = cfg->priv_flags;

	if (flags & ~AQ_PRIV_FLAGS_MASK)
		return -EOPNOTSUPP;

	if (hweight32((flags | priv_flags) & AQ_HW_LOOPBACK_MASK) > 1) {
		netdev_info(ndev, "Can't enable more than one loopback simultaneously\n");
		return -EINVAL;
	}

	cfg->priv_flags = flags;

	if ((priv_flags ^ flags) & BIT(AQ_HW_LOOPBACK_DMA_NET)) {
		if (netif_running(ndev)) {
			dev_close(ndev);

			dev_open(ndev, NULL);
		}
	} else if ((priv_flags ^ flags) & AQ_HW_LOOPBACK_MASK) {
		aq_nic_set_loopback(aq_nic);
	}

	return 0;
}

const struct ethtool_ops aq_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES,
	.get_link            = aq_ethtool_get_link,
	.get_regs_len        = aq_ethtool_get_regs_len,
	.get_regs            = aq_ethtool_get_regs,
	.get_drvinfo         = aq_ethtool_get_drvinfo,
	.get_strings         = aq_ethtool_get_strings,
	.set_phys_id         = aq_ethtool_set_phys_id,
	.get_rxfh_indir_size = aq_ethtool_get_rss_indir_size,
	.get_wol             = aq_ethtool_get_wol,
	.set_wol             = aq_ethtool_set_wol,
	.nway_reset          = aq_ethtool_nway_reset,
	.get_ringparam       = aq_get_ringparam,
	.set_ringparam       = aq_set_ringparam,
	.get_eee             = aq_ethtool_get_eee,
	.set_eee             = aq_ethtool_set_eee,
	.get_pauseparam      = aq_ethtool_get_pauseparam,
	.set_pauseparam      = aq_ethtool_set_pauseparam,
	.get_rxfh_key_size   = aq_ethtool_get_rss_key_size,
	.get_rxfh            = aq_ethtool_get_rss,
	.set_rxfh            = aq_ethtool_set_rss,
	.get_rxnfc           = aq_ethtool_get_rxnfc,
	.set_rxnfc           = aq_ethtool_set_rxnfc,
	.get_msglevel        = aq_get_msg_level,
	.set_msglevel        = aq_set_msg_level,
	.get_sset_count      = aq_ethtool_get_sset_count,
	.get_ethtool_stats   = aq_ethtool_stats,
	.get_priv_flags      = aq_ethtool_get_priv_flags,
	.set_priv_flags      = aq_ethtool_set_priv_flags,
	.get_link_ksettings  = aq_ethtool_get_link_ksettings,
	.set_link_ksettings  = aq_ethtool_set_link_ksettings,
	.get_coalesce	     = aq_ethtool_get_coalesce,
	.set_coalesce	     = aq_ethtool_set_coalesce,
	.get_ts_info         = aq_ethtool_get_ts_info,
};
