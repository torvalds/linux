// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/module.h>
#include <linux/netdevice.h>

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_lif.h"
#include "ionic_ethtool.h"
#include "ionic_stats.h"

static const char ionic_priv_flags_strings[][ETH_GSTRING_LEN] = {
#define PRIV_F_SW_DBG_STATS		BIT(0)
	"sw-dbg-stats",
};
#define PRIV_FLAGS_COUNT ARRAY_SIZE(ionic_priv_flags_strings)

static void ionic_get_stats_strings(struct ionic_lif *lif, u8 *buf)
{
	u32 i;

	for (i = 0; i < ionic_num_stats_grps; i++)
		ionic_stats_groups[i].get_strings(lif, &buf);
}

static void ionic_get_stats(struct net_device *netdev,
			    struct ethtool_stats *stats, u64 *buf)
{
	struct ionic_lif *lif;
	u32 i;

	lif = netdev_priv(netdev);

	memset(buf, 0, stats->n_stats * sizeof(*buf));
	for (i = 0; i < ionic_num_stats_grps; i++)
		ionic_stats_groups[i].get_values(lif, &buf);
}

static int ionic_get_stats_count(struct ionic_lif *lif)
{
	int i, num_stats = 0;

	for (i = 0; i < ionic_num_stats_grps; i++)
		num_stats += ionic_stats_groups[i].get_count(lif);

	return num_stats;
}

static int ionic_get_sset_count(struct net_device *netdev, int sset)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	int count = 0;

	switch (sset) {
	case ETH_SS_STATS:
		count = ionic_get_stats_count(lif);
		break;
	case ETH_SS_PRIV_FLAGS:
		count = PRIV_FLAGS_COUNT;
		break;
	}
	return count;
}

static void ionic_get_strings(struct net_device *netdev,
			      u32 sset, u8 *buf)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_STATS:
		ionic_get_stats_strings(lif, buf);
		break;
	case ETH_SS_PRIV_FLAGS:
		memcpy(buf, ionic_priv_flags_strings,
		       PRIV_FLAGS_COUNT * ETH_GSTRING_LEN);
		break;
	}
}

static void ionic_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic *ionic = lif->ionic;

	strlcpy(drvinfo->driver, IONIC_DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, IONIC_DRV_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, ionic->idev.dev_info.fw_version,
		sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, ionic_bus_info(ionic),
		sizeof(drvinfo->bus_info));
}

static int ionic_get_regs_len(struct net_device *netdev)
{
	return (IONIC_DEV_INFO_REG_COUNT + IONIC_DEV_CMD_REG_COUNT) * sizeof(u32);
}

static void ionic_get_regs(struct net_device *netdev, struct ethtool_regs *regs,
			   void *p)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	unsigned int size;

	regs->version = IONIC_DEV_CMD_REG_VERSION;

	size = IONIC_DEV_INFO_REG_COUNT * sizeof(u32);
	memcpy_fromio(p, lif->ionic->idev.dev_info_regs->words, size);

	size = IONIC_DEV_CMD_REG_COUNT * sizeof(u32);
	memcpy_fromio(p, lif->ionic->idev.dev_cmd_regs->words, size);
}

static int ionic_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings *ks)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_dev *idev = &lif->ionic->idev;
	int copper_seen = 0;

	ethtool_link_ksettings_zero_link_mode(ks, supported);

	/* The port_info data is found in a DMA space that the NIC keeps
	 * up-to-date, so there's no need to request the data from the
	 * NIC, we already have it in our memory space.
	 */

	switch (le16_to_cpu(idev->port_info->status.xcvr.pid)) {
		/* Copper */
	case IONIC_XCVR_PID_QSFP_100G_CR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseCR4_Full);
		copper_seen++;
		break;
	case IONIC_XCVR_PID_QSFP_40GBASE_CR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseCR4_Full);
		copper_seen++;
		break;
	case IONIC_XCVR_PID_SFP_25GBASE_CR_S:
	case IONIC_XCVR_PID_SFP_25GBASE_CR_L:
	case IONIC_XCVR_PID_SFP_25GBASE_CR_N:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseCR_Full);
		copper_seen++;
		break;
	case IONIC_XCVR_PID_SFP_10GBASE_AOC:
	case IONIC_XCVR_PID_SFP_10GBASE_CU:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseCR_Full);
		copper_seen++;
		break;

		/* Fibre */
	case IONIC_XCVR_PID_QSFP_100G_SR4:
	case IONIC_XCVR_PID_QSFP_100G_AOC:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseSR4_Full);
		break;
	case IONIC_XCVR_PID_QSFP_100G_LR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseLR4_ER4_Full);
		break;
	case IONIC_XCVR_PID_QSFP_100G_ER4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseLR4_ER4_Full);
		break;
	case IONIC_XCVR_PID_QSFP_40GBASE_SR4:
	case IONIC_XCVR_PID_QSFP_40GBASE_AOC:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseSR4_Full);
		break;
	case IONIC_XCVR_PID_QSFP_40GBASE_LR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseLR4_Full);
		break;
	case IONIC_XCVR_PID_SFP_25GBASE_SR:
	case IONIC_XCVR_PID_SFP_25GBASE_AOC:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseSR_Full);
		break;
	case IONIC_XCVR_PID_SFP_10GBASE_SR:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseSR_Full);
		break;
	case IONIC_XCVR_PID_SFP_10GBASE_LR:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseLR_Full);
		break;
	case IONIC_XCVR_PID_SFP_10GBASE_LRM:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseLRM_Full);
		break;
	case IONIC_XCVR_PID_SFP_10GBASE_ER:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseER_Full);
		break;
	case IONIC_XCVR_PID_UNKNOWN:
		/* This means there's no module plugged in */
		break;
	default:
		dev_info(lif->ionic->dev, "unknown xcvr type pid=%d / 0x%x\n",
			 idev->port_info->status.xcvr.pid,
			 idev->port_info->status.xcvr.pid);
		break;
	}

	bitmap_copy(ks->link_modes.advertising, ks->link_modes.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);

	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_BASER);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_RS);
	if (idev->port_info->config.fec_type == IONIC_PORT_FEC_TYPE_FC)
		ethtool_link_ksettings_add_link_mode(ks, advertising, FEC_BASER);
	else if (idev->port_info->config.fec_type == IONIC_PORT_FEC_TYPE_RS)
		ethtool_link_ksettings_add_link_mode(ks, advertising, FEC_RS);

	ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(ks, supported, Pause);

	if (idev->port_info->status.xcvr.phy == IONIC_PHY_TYPE_COPPER ||
	    copper_seen)
		ks->base.port = PORT_DA;
	else if (idev->port_info->status.xcvr.phy == IONIC_PHY_TYPE_FIBER)
		ks->base.port = PORT_FIBRE;
	else
		ks->base.port = PORT_NONE;

	if (ks->base.port != PORT_NONE) {
		ks->base.speed = le32_to_cpu(lif->info->status.link_speed);

		if (le16_to_cpu(lif->info->status.link_status))
			ks->base.duplex = DUPLEX_FULL;
		else
			ks->base.duplex = DUPLEX_UNKNOWN;

		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);

		if (idev->port_info->config.an_enable) {
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     Autoneg);
			ks->base.autoneg = AUTONEG_ENABLE;
		}
	}

	return 0;
}

static int ionic_set_link_ksettings(struct net_device *netdev,
				    const struct ethtool_link_ksettings *ks)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic *ionic = lif->ionic;
	struct ionic_dev *idev;
	int err = 0;

	idev = &lif->ionic->idev;

	/* set autoneg */
	if (ks->base.autoneg != idev->port_info->config.an_enable) {
		mutex_lock(&ionic->dev_cmd_lock);
		ionic_dev_cmd_port_autoneg(idev, ks->base.autoneg);
		err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
		mutex_unlock(&ionic->dev_cmd_lock);
		if (err)
			return err;
	}

	/* set speed */
	if (ks->base.speed != le32_to_cpu(idev->port_info->config.speed)) {
		mutex_lock(&ionic->dev_cmd_lock);
		ionic_dev_cmd_port_speed(idev, ks->base.speed);
		err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
		mutex_unlock(&ionic->dev_cmd_lock);
		if (err)
			return err;
	}

	return 0;
}

static void ionic_get_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pause)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	u8 pause_type;

	pause->autoneg = 0;

	pause_type = lif->ionic->idev.port_info->config.pause_type;
	if (pause_type) {
		pause->rx_pause = pause_type & IONIC_PAUSE_F_RX ? 1 : 0;
		pause->tx_pause = pause_type & IONIC_PAUSE_F_TX ? 1 : 0;
	}
}

static int ionic_set_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic *ionic = lif->ionic;
	u32 requested_pause;
	int err;

	if (pause->autoneg)
		return -EOPNOTSUPP;

	/* change both at the same time */
	requested_pause = IONIC_PORT_PAUSE_TYPE_LINK;
	if (pause->rx_pause)
		requested_pause |= IONIC_PAUSE_F_RX;
	if (pause->tx_pause)
		requested_pause |= IONIC_PAUSE_F_TX;

	if (requested_pause == lif->ionic->idev.port_info->config.pause_type)
		return 0;

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_port_pause(&lif->ionic->idev, requested_pause);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	mutex_unlock(&ionic->dev_cmd_lock);
	if (err)
		return err;

	return 0;
}

static int ionic_get_fecparam(struct net_device *netdev,
			      struct ethtool_fecparam *fec)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	switch (lif->ionic->idev.port_info->config.fec_type) {
	case IONIC_PORT_FEC_TYPE_NONE:
		fec->active_fec = ETHTOOL_FEC_OFF;
		break;
	case IONIC_PORT_FEC_TYPE_RS:
		fec->active_fec = ETHTOOL_FEC_RS;
		break;
	case IONIC_PORT_FEC_TYPE_FC:
		fec->active_fec = ETHTOOL_FEC_BASER;
		break;
	}

	fec->fec = ETHTOOL_FEC_OFF | ETHTOOL_FEC_RS | ETHTOOL_FEC_BASER;

	return 0;
}

static int ionic_set_fecparam(struct net_device *netdev,
			      struct ethtool_fecparam *fec)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	u8 fec_type;
	int ret = 0;

	if (lif->ionic->idev.port_info->config.an_enable) {
		netdev_err(netdev, "FEC request not allowed while autoneg is enabled\n");
		return -EINVAL;
	}

	switch (fec->fec) {
	case ETHTOOL_FEC_NONE:
		fec_type = IONIC_PORT_FEC_TYPE_NONE;
		break;
	case ETHTOOL_FEC_OFF:
		fec_type = IONIC_PORT_FEC_TYPE_NONE;
		break;
	case ETHTOOL_FEC_RS:
		fec_type = IONIC_PORT_FEC_TYPE_RS;
		break;
	case ETHTOOL_FEC_BASER:
		fec_type = IONIC_PORT_FEC_TYPE_FC;
		break;
	case ETHTOOL_FEC_AUTO:
	default:
		netdev_err(netdev, "FEC request 0x%04x not supported\n",
			   fec->fec);
		return -EINVAL;
	}

	if (fec_type != lif->ionic->idev.port_info->config.fec_type) {
		mutex_lock(&lif->ionic->dev_cmd_lock);
		ionic_dev_cmd_port_fec(&lif->ionic->idev, fec_type);
		ret = ionic_dev_cmd_wait(lif->ionic, DEVCMD_TIMEOUT);
		mutex_unlock(&lif->ionic->dev_cmd_lock);
	}

	return ret;
}

static int ionic_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coalesce)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	/* Tx uses Rx interrupt */
	coalesce->tx_coalesce_usecs = lif->rx_coalesce_usecs;
	coalesce->rx_coalesce_usecs = lif->rx_coalesce_usecs;

	return 0;
}

static int ionic_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coalesce)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_identity *ident;
	struct ionic_qcq *qcq;
	unsigned int i;
	u32 coal;

	if (coalesce->rx_max_coalesced_frames ||
	    coalesce->rx_coalesce_usecs_irq ||
	    coalesce->rx_max_coalesced_frames_irq ||
	    coalesce->tx_max_coalesced_frames ||
	    coalesce->tx_coalesce_usecs_irq ||
	    coalesce->tx_max_coalesced_frames_irq ||
	    coalesce->stats_block_coalesce_usecs ||
	    coalesce->use_adaptive_rx_coalesce ||
	    coalesce->use_adaptive_tx_coalesce ||
	    coalesce->pkt_rate_low ||
	    coalesce->rx_coalesce_usecs_low ||
	    coalesce->rx_max_coalesced_frames_low ||
	    coalesce->tx_coalesce_usecs_low ||
	    coalesce->tx_max_coalesced_frames_low ||
	    coalesce->pkt_rate_high ||
	    coalesce->rx_coalesce_usecs_high ||
	    coalesce->rx_max_coalesced_frames_high ||
	    coalesce->tx_coalesce_usecs_high ||
	    coalesce->tx_max_coalesced_frames_high ||
	    coalesce->rate_sample_interval)
		return -EINVAL;

	ident = &lif->ionic->ident;
	if (ident->dev.intr_coal_div == 0) {
		netdev_warn(netdev, "bad HW value in dev.intr_coal_div = %d\n",
			    ident->dev.intr_coal_div);
		return -EIO;
	}

	/* Tx uses Rx interrupt, so only change Rx */
	if (coalesce->tx_coalesce_usecs != lif->rx_coalesce_usecs) {
		netdev_warn(netdev, "only the rx-usecs can be changed\n");
		return -EINVAL;
	}

	/* Convert the usec request to a HW useable value.  If they asked
	 * for non-zero and it resolved to zero, bump it up
	 */
	coal = ionic_coal_usec_to_hw(lif->ionic, coalesce->rx_coalesce_usecs);
	if (!coal && coalesce->rx_coalesce_usecs)
		coal = 1;

	if (coal > IONIC_INTR_CTRL_COAL_MAX)
		return -ERANGE;

	/* Save the new value */
	lif->rx_coalesce_usecs = coalesce->rx_coalesce_usecs;
	if (coal != lif->rx_coalesce_hw) {
		lif->rx_coalesce_hw = coal;

		if (test_bit(IONIC_LIF_UP, lif->state)) {
			for (i = 0; i < lif->nxqs; i++) {
				qcq = lif->rxqcqs[i].qcq;
				ionic_intr_coal_init(lif->ionic->idev.intr_ctrl,
						     qcq->intr.index,
						     lif->rx_coalesce_hw);
			}
		}
	}

	return 0;
}

static void ionic_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	ring->tx_max_pending = IONIC_MAX_TXRX_DESC;
	ring->tx_pending = lif->ntxq_descs;
	ring->rx_max_pending = IONIC_MAX_TXRX_DESC;
	ring->rx_pending = lif->nrxq_descs;
}

static int ionic_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	bool running;
	int err;

	if (ring->rx_mini_pending || ring->rx_jumbo_pending) {
		netdev_info(netdev, "Changing jumbo or mini descriptors not supported\n");
		return -EINVAL;
	}

	if (!is_power_of_2(ring->tx_pending) ||
	    !is_power_of_2(ring->rx_pending)) {
		netdev_info(netdev, "Descriptor count must be a power of 2\n");
		return -EINVAL;
	}

	/* if nothing to do return success */
	if (ring->tx_pending == lif->ntxq_descs &&
	    ring->rx_pending == lif->nrxq_descs)
		return 0;

	err = ionic_wait_for_bit(lif, IONIC_LIF_QUEUE_RESET);
	if (err)
		return err;

	running = test_bit(IONIC_LIF_UP, lif->state);
	if (running)
		ionic_stop(netdev);

	lif->ntxq_descs = ring->tx_pending;
	lif->nrxq_descs = ring->rx_pending;

	if (running)
		ionic_open(netdev);
	clear_bit(IONIC_LIF_QUEUE_RESET, lif->state);

	return 0;
}

static void ionic_get_channels(struct net_device *netdev,
			       struct ethtool_channels *ch)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	/* report maximum channels */
	ch->max_combined = lif->ionic->ntxqs_per_lif;

	/* report current channels */
	ch->combined_count = lif->nxqs;
}

static int ionic_set_channels(struct net_device *netdev,
			      struct ethtool_channels *ch)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	bool running;
	int err;

	if (!ch->combined_count || ch->other_count ||
	    ch->rx_count || ch->tx_count)
		return -EINVAL;

	if (ch->combined_count == lif->nxqs)
		return 0;

	err = ionic_wait_for_bit(lif, IONIC_LIF_QUEUE_RESET);
	if (err)
		return err;

	running = test_bit(IONIC_LIF_UP, lif->state);
	if (running)
		ionic_stop(netdev);

	lif->nxqs = ch->combined_count;

	if (running)
		ionic_open(netdev);
	clear_bit(IONIC_LIF_QUEUE_RESET, lif->state);

	return 0;
}

static u32 ionic_get_priv_flags(struct net_device *netdev)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	u32 priv_flags = 0;

	if (test_bit(IONIC_LIF_SW_DEBUG_STATS, lif->state))
		priv_flags |= PRIV_F_SW_DBG_STATS;

	return priv_flags;
}

static int ionic_set_priv_flags(struct net_device *netdev, u32 priv_flags)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	u32 flags = lif->flags;

	clear_bit(IONIC_LIF_SW_DEBUG_STATS, lif->state);
	if (priv_flags & PRIV_F_SW_DBG_STATS)
		set_bit(IONIC_LIF_SW_DEBUG_STATS, lif->state);

	if (flags != lif->flags)
		lif->flags = flags;

	return 0;
}

static int ionic_get_rxnfc(struct net_device *netdev,
			   struct ethtool_rxnfc *info, u32 *rules)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	int err = 0;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = lif->nxqs;
		break;
	default:
		netdev_err(netdev, "Command parameter %d is not supported\n",
			   info->cmd);
		err = -EOPNOTSUPP;
	}

	return err;
}

static u32 ionic_get_rxfh_indir_size(struct net_device *netdev)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	return le16_to_cpu(lif->ionic->ident.lif.eth.rss_ind_tbl_sz);
}

static u32 ionic_get_rxfh_key_size(struct net_device *netdev)
{
	return IONIC_RSS_HASH_KEY_SIZE;
}

static int ionic_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			  u8 *hfunc)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	unsigned int i, tbl_sz;

	if (indir) {
		tbl_sz = le16_to_cpu(lif->ionic->ident.lif.eth.rss_ind_tbl_sz);
		for (i = 0; i < tbl_sz; i++)
			indir[i] = lif->rss_ind_tbl[i];
	}

	if (key)
		memcpy(key, lif->rss_hash_key, IONIC_RSS_HASH_KEY_SIZE);

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	return 0;
}

static int ionic_set_rxfh(struct net_device *netdev, const u32 *indir,
			  const u8 *key, const u8 hfunc)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	int err;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	err = ionic_lif_rss_config(lif, lif->rss_types, key, indir);
	if (err)
		return err;

	return 0;
}

static int ionic_set_tunable(struct net_device *dev,
			     const struct ethtool_tunable *tuna,
			     const void *data)
{
	struct ionic_lif *lif = netdev_priv(dev);

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		lif->rx_copybreak = *(u32 *)data;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ionic_get_tunable(struct net_device *netdev,
			     const struct ethtool_tunable *tuna, void *data)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)data = lif->rx_copybreak;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ionic_get_module_info(struct net_device *netdev,
				 struct ethtool_modinfo *modinfo)

{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_dev *idev = &lif->ionic->idev;
	struct ionic_xcvr_status *xcvr;

	xcvr = &idev->port_info->status.xcvr;

	/* report the module data type and length */
	switch (xcvr->sprom[0]) {
	case 0x03: /* SFP */
		modinfo->type = ETH_MODULE_SFF_8079;
		modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
		break;
	case 0x0D: /* QSFP */
	case 0x11: /* QSFP28 */
		modinfo->type = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		break;
	default:
		netdev_info(netdev, "unknown xcvr type 0x%02x\n",
			    xcvr->sprom[0]);
		break;
	}

	return 0;
}

static int ionic_get_module_eeprom(struct net_device *netdev,
				   struct ethtool_eeprom *ee,
				   u8 *data)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_dev *idev = &lif->ionic->idev;
	struct ionic_xcvr_status *xcvr;
	char tbuf[sizeof(xcvr->sprom)];
	int count = 10;
	u32 len;

	/* The NIC keeps the module prom up-to-date in the DMA space
	 * so we can simply copy the module bytes into the data buffer.
	 */
	xcvr = &idev->port_info->status.xcvr;
	len = min_t(u32, sizeof(xcvr->sprom), ee->len);

	do {
		memcpy(data, xcvr->sprom, len);
		memcpy(tbuf, xcvr->sprom, len);

		/* Let's make sure we got a consistent copy */
		if (!memcmp(data, tbuf, len))
			break;

	} while (--count);

	if (!count)
		return -ETIMEDOUT;

	return 0;
}

static int ionic_nway_reset(struct net_device *netdev)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic *ionic = lif->ionic;
	int err = 0;

	/* flap the link to force auto-negotiation */

	mutex_lock(&ionic->dev_cmd_lock);

	ionic_dev_cmd_port_state(&ionic->idev, IONIC_PORT_ADMIN_STATE_DOWN);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);

	if (!err) {
		ionic_dev_cmd_port_state(&ionic->idev, IONIC_PORT_ADMIN_STATE_UP);
		err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	}

	mutex_unlock(&ionic->dev_cmd_lock);

	return err;
}

static const struct ethtool_ops ionic_ethtool_ops = {
	.get_drvinfo		= ionic_get_drvinfo,
	.get_regs_len		= ionic_get_regs_len,
	.get_regs		= ionic_get_regs,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= ionic_get_link_ksettings,
	.set_link_ksettings	= ionic_set_link_ksettings,
	.get_coalesce		= ionic_get_coalesce,
	.set_coalesce		= ionic_set_coalesce,
	.get_ringparam		= ionic_get_ringparam,
	.set_ringparam		= ionic_set_ringparam,
	.get_channels		= ionic_get_channels,
	.set_channels		= ionic_set_channels,
	.get_strings		= ionic_get_strings,
	.get_ethtool_stats	= ionic_get_stats,
	.get_sset_count		= ionic_get_sset_count,
	.get_priv_flags		= ionic_get_priv_flags,
	.set_priv_flags		= ionic_set_priv_flags,
	.get_rxnfc		= ionic_get_rxnfc,
	.get_rxfh_indir_size	= ionic_get_rxfh_indir_size,
	.get_rxfh_key_size	= ionic_get_rxfh_key_size,
	.get_rxfh		= ionic_get_rxfh,
	.set_rxfh		= ionic_set_rxfh,
	.get_tunable		= ionic_get_tunable,
	.set_tunable		= ionic_set_tunable,
	.get_module_info	= ionic_get_module_info,
	.get_module_eeprom	= ionic_get_module_eeprom,
	.get_pauseparam		= ionic_get_pauseparam,
	.set_pauseparam		= ionic_set_pauseparam,
	.get_fecparam		= ionic_get_fecparam,
	.set_fecparam		= ionic_set_fecparam,
	.nway_reset		= ionic_nway_reset,
};

void ionic_ethtool_set_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ionic_ethtool_ops;
}
