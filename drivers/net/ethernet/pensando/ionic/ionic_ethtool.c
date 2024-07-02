// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/sfp.h>

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_lif.h"
#include "ionic_ethtool.h"
#include "ionic_stats.h"

static void ionic_get_stats_strings(struct ionic_lif *lif, u8 *buf)
{
	u32 i;

	for (i = 0; i < ionic_num_stats_grps; i++)
		ionic_stats_groups[i].get_strings(lif, &buf);
}

static void ionic_get_stats(struct net_device *netdev,
			    struct ethtool_stats *stats, u64 *buf)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	u32 i;

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return;

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
	}
}

static void ionic_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic *ionic = lif->ionic;

	strscpy(drvinfo->driver, IONIC_DRV_NAME, sizeof(drvinfo->driver));
	strscpy(drvinfo->fw_version, ionic->idev.dev_info.fw_version,
		sizeof(drvinfo->fw_version));
	strscpy(drvinfo->bus_info, ionic_bus_info(ionic),
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
	struct ionic_dev *idev;
	unsigned int offset;
	unsigned int size;

	regs->version = IONIC_DEV_CMD_REG_VERSION;

	idev = &lif->ionic->idev;
	if (!idev->dev_info_regs)
		return;

	offset = 0;
	size = IONIC_DEV_INFO_REG_COUNT * sizeof(u32);
	memcpy_fromio(p + offset, lif->ionic->idev.dev_info_regs->words, size);

	offset += size;
	size = IONIC_DEV_CMD_REG_COUNT * sizeof(u32);
	memcpy_fromio(p + offset, idev->dev_cmd_regs->words, size);
}

static void ionic_get_link_ext_stats(struct net_device *netdev,
				     struct ethtool_link_ext_stats *stats)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	if (lif->ionic->pdev->is_physfn)
		stats->link_down_events = lif->link_down_count;
}

static int ionic_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings *ks)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_dev *idev = &lif->ionic->idev;
	int copper_seen = 0;

	ethtool_link_ksettings_zero_link_mode(ks, supported);

	if (!idev->port_info) {
		netdev_err(netdev, "port_info not initialized\n");
		return -EOPNOTSUPP;
	}

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
	case IONIC_XCVR_PID_QSFP_100G_CWDM4:
	case IONIC_XCVR_PID_QSFP_100G_PSM4:
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
	case IONIC_XCVR_PID_SFP_25GBASE_ACC:
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
	case IONIC_XCVR_PID_SFP_10GBASE_T:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
		break;
	case IONIC_XCVR_PID_SFP_1000BASE_T:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseT_Full);
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

	linkmode_copy(ks->link_modes.advertising, ks->link_modes.supported);

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
	struct ionic_dev *idev = &lif->ionic->idev;
	struct ionic *ionic = lif->ionic;
	int err = 0;

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return -EBUSY;

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
		pause->rx_pause = (pause_type & IONIC_PAUSE_F_RX) ? 1 : 0;
		pause->tx_pause = (pause_type & IONIC_PAUSE_F_TX) ? 1 : 0;
	}
}

static int ionic_set_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic *ionic = lif->ionic;
	u32 requested_pause;
	int err;

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return -EBUSY;

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

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return -EBUSY;

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
			      struct ethtool_coalesce *coalesce,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	coalesce->tx_coalesce_usecs = lif->tx_coalesce_usecs;
	coalesce->rx_coalesce_usecs = lif->rx_coalesce_usecs;

	if (test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state))
		coalesce->use_adaptive_tx_coalesce = test_bit(IONIC_LIF_F_TX_DIM_INTR, lif->state);
	else
		coalesce->use_adaptive_tx_coalesce = 0;

	coalesce->use_adaptive_rx_coalesce = test_bit(IONIC_LIF_F_RX_DIM_INTR, lif->state);

	return 0;
}

static int ionic_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coalesce,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_identity *ident;
	u32 rx_coal, rx_dim;
	u32 tx_coal, tx_dim;
	unsigned int i;

	ident = &lif->ionic->ident;
	if (ident->dev.intr_coal_div == 0) {
		netdev_warn(netdev, "bad HW value in dev.intr_coal_div = %d\n",
			    ident->dev.intr_coal_div);
		return -EIO;
	}

	/* Tx normally shares Rx interrupt, so only change Rx if not split */
	if (!test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state) &&
	    (coalesce->tx_coalesce_usecs != lif->rx_coalesce_usecs ||
	     coalesce->use_adaptive_tx_coalesce)) {
		netdev_warn(netdev, "only rx parameters can be changed\n");
		return -EINVAL;
	}

	/* Convert the usec request to a HW usable value.  If they asked
	 * for non-zero and it resolved to zero, bump it up
	 */
	rx_coal = ionic_coal_usec_to_hw(lif->ionic, coalesce->rx_coalesce_usecs);
	if (!rx_coal && coalesce->rx_coalesce_usecs)
		rx_coal = 1;
	tx_coal = ionic_coal_usec_to_hw(lif->ionic, coalesce->tx_coalesce_usecs);
	if (!tx_coal && coalesce->tx_coalesce_usecs)
		tx_coal = 1;

	if (rx_coal > IONIC_INTR_CTRL_COAL_MAX ||
	    tx_coal > IONIC_INTR_CTRL_COAL_MAX)
		return -ERANGE;

	/* Save the new values */
	lif->rx_coalesce_usecs = coalesce->rx_coalesce_usecs;
	lif->rx_coalesce_hw = rx_coal;

	if (test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state))
		lif->tx_coalesce_usecs = coalesce->tx_coalesce_usecs;
	else
		lif->tx_coalesce_usecs = coalesce->rx_coalesce_usecs;
	lif->tx_coalesce_hw = tx_coal;

	if (coalesce->use_adaptive_rx_coalesce) {
		set_bit(IONIC_LIF_F_RX_DIM_INTR, lif->state);
		rx_dim = rx_coal;
	} else {
		clear_bit(IONIC_LIF_F_RX_DIM_INTR, lif->state);
		rx_dim = 0;
	}

	if (coalesce->use_adaptive_tx_coalesce) {
		set_bit(IONIC_LIF_F_TX_DIM_INTR, lif->state);
		tx_dim = tx_coal;
	} else {
		clear_bit(IONIC_LIF_F_TX_DIM_INTR, lif->state);
		tx_dim = 0;
	}

	if (test_bit(IONIC_LIF_F_UP, lif->state)) {
		for (i = 0; i < lif->nxqs; i++) {
			if (lif->rxqcqs[i]->flags & IONIC_QCQ_F_INTR) {
				ionic_intr_coal_init(lif->ionic->idev.intr_ctrl,
						     lif->rxqcqs[i]->intr.index,
						     lif->rx_coalesce_hw);
				lif->rxqcqs[i]->intr.dim_coal_hw = rx_dim;
			}

			if (lif->txqcqs[i]->flags & IONIC_QCQ_F_INTR) {
				ionic_intr_coal_init(lif->ionic->idev.intr_ctrl,
						     lif->txqcqs[i]->intr.index,
						     lif->tx_coalesce_hw);
				lif->txqcqs[i]->intr.dim_coal_hw = tx_dim;
			}
		}
	}

	return 0;
}

static int ionic_validate_cmb_config(struct ionic_lif *lif,
				     struct ionic_queue_params *qparam)
{
	int pages_have, pages_required = 0;
	unsigned long sz;

	if (!lif->ionic->idev.cmb_inuse &&
	    (qparam->cmb_tx || qparam->cmb_rx)) {
		netdev_info(lif->netdev, "CMB rings are not supported on this device\n");
		return -EOPNOTSUPP;
	}

	if (qparam->cmb_tx) {
		if (!(lif->qtype_info[IONIC_QTYPE_TXQ].features & IONIC_QIDENT_F_CMB)) {
			netdev_info(lif->netdev,
				    "CMB rings for tx-push are not supported on this device\n");
			return -EOPNOTSUPP;
		}

		sz = sizeof(struct ionic_txq_desc) * qparam->ntxq_descs * qparam->nxqs;
		pages_required += ALIGN(sz, PAGE_SIZE) / PAGE_SIZE;
	}

	if (qparam->cmb_rx) {
		if (!(lif->qtype_info[IONIC_QTYPE_RXQ].features & IONIC_QIDENT_F_CMB)) {
			netdev_info(lif->netdev,
				    "CMB rings for rx-push are not supported on this device\n");
			return -EOPNOTSUPP;
		}

		sz = sizeof(struct ionic_rxq_desc) * qparam->nrxq_descs * qparam->nxqs;
		pages_required += ALIGN(sz, PAGE_SIZE) / PAGE_SIZE;
	}

	pages_have = lif->ionic->bars[IONIC_PCI_BAR_CMB].len / PAGE_SIZE;
	if (pages_required > pages_have) {
		netdev_info(lif->netdev,
			    "Not enough CMB pages for number of queues and size of descriptor rings, need %d have %d",
			    pages_required, pages_have);
		return -ENOMEM;
	}

	return pages_required;
}

static int ionic_cmb_rings_toggle(struct ionic_lif *lif, bool cmb_tx, bool cmb_rx)
{
	struct ionic_queue_params qparam;
	int pages_used;

	if (netif_running(lif->netdev)) {
		netdev_info(lif->netdev, "Please stop device to toggle CMB for tx/rx-push\n");
		return -EBUSY;
	}

	ionic_init_queue_params(lif, &qparam);
	qparam.cmb_tx = cmb_tx;
	qparam.cmb_rx = cmb_rx;
	pages_used = ionic_validate_cmb_config(lif, &qparam);
	if (pages_used < 0)
		return pages_used;

	if (cmb_tx)
		set_bit(IONIC_LIF_F_CMB_TX_RINGS, lif->state);
	else
		clear_bit(IONIC_LIF_F_CMB_TX_RINGS, lif->state);

	if (cmb_rx)
		set_bit(IONIC_LIF_F_CMB_RX_RINGS, lif->state);
	else
		clear_bit(IONIC_LIF_F_CMB_RX_RINGS, lif->state);

	if (cmb_tx || cmb_rx)
		netdev_info(lif->netdev, "Enabling CMB %s %s rings - %d pages\n",
			    cmb_tx ? "TX" : "", cmb_rx ? "RX" : "", pages_used);
	else
		netdev_info(lif->netdev, "Disabling CMB rings\n");

	return 0;
}

static void ionic_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring,
				struct kernel_ethtool_ringparam *kernel_ring,
				struct netlink_ext_ack *extack)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	ring->tx_max_pending = IONIC_MAX_TX_DESC;
	ring->tx_pending = lif->ntxq_descs;
	ring->rx_max_pending = IONIC_MAX_RX_DESC;
	ring->rx_pending = lif->nrxq_descs;
	kernel_ring->tx_push = test_bit(IONIC_LIF_F_CMB_TX_RINGS, lif->state);
	kernel_ring->rx_push = test_bit(IONIC_LIF_F_CMB_RX_RINGS, lif->state);
}

static int ionic_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_queue_params qparam;
	int err;

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return -EBUSY;

	ionic_init_queue_params(lif, &qparam);

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
	    ring->rx_pending == lif->nrxq_descs &&
	    kernel_ring->tx_push == test_bit(IONIC_LIF_F_CMB_TX_RINGS, lif->state) &&
	    kernel_ring->rx_push == test_bit(IONIC_LIF_F_CMB_RX_RINGS, lif->state))
		return 0;

	qparam.ntxq_descs = ring->tx_pending;
	qparam.nrxq_descs = ring->rx_pending;
	qparam.cmb_tx = kernel_ring->tx_push;
	qparam.cmb_rx = kernel_ring->rx_push;

	err = ionic_validate_cmb_config(lif, &qparam);
	if (err < 0)
		return err;

	if (kernel_ring->tx_push != test_bit(IONIC_LIF_F_CMB_TX_RINGS, lif->state) ||
	    kernel_ring->rx_push != test_bit(IONIC_LIF_F_CMB_RX_RINGS, lif->state)) {
		err = ionic_cmb_rings_toggle(lif, kernel_ring->tx_push,
					     kernel_ring->rx_push);
		if (err < 0)
			return err;
	}

	if (ring->tx_pending != lif->ntxq_descs)
		netdev_info(netdev, "Changing Tx ring size from %d to %d\n",
			    lif->ntxq_descs, ring->tx_pending);

	if (ring->rx_pending != lif->nrxq_descs)
		netdev_info(netdev, "Changing Rx ring size from %d to %d\n",
			    lif->nrxq_descs, ring->rx_pending);

	/* if we're not running, just set the values and return */
	if (!netif_running(lif->netdev)) {
		lif->ntxq_descs = ring->tx_pending;
		lif->nrxq_descs = ring->rx_pending;
		return 0;
	}

	mutex_lock(&lif->queue_lock);
	err = ionic_reconfigure_queues(lif, &qparam);
	mutex_unlock(&lif->queue_lock);
	if (err)
		netdev_info(netdev, "Ring reconfiguration failed, changes canceled: %d\n", err);

	return err;
}

static void ionic_get_channels(struct net_device *netdev,
			       struct ethtool_channels *ch)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	/* report maximum channels */
	ch->max_combined = lif->ionic->ntxqs_per_lif;
	ch->max_rx = lif->ionic->ntxqs_per_lif / 2;
	ch->max_tx = lif->ionic->ntxqs_per_lif / 2;

	/* report current channels */
	if (test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state)) {
		ch->rx_count = lif->nxqs;
		ch->tx_count = lif->nxqs;
	} else {
		ch->combined_count = lif->nxqs;
	}
}

static int ionic_set_channels(struct net_device *netdev,
			      struct ethtool_channels *ch)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_queue_params qparam;
	int max_cnt;
	int err;

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return -EBUSY;

	ionic_init_queue_params(lif, &qparam);

	if ((ch->rx_count || ch->tx_count) && lif->xdp_prog) {
		netdev_info(lif->netdev, "Split Tx/Rx interrupts not available when using XDP\n");
		return -EOPNOTSUPP;
	}

	if (ch->rx_count != ch->tx_count) {
		netdev_info(netdev, "The rx and tx count must be equal\n");
		return -EINVAL;
	}

	if (ch->combined_count && ch->rx_count) {
		netdev_info(netdev, "Use either combined or rx and tx, not both\n");
		return -EINVAL;
	}

	max_cnt = lif->ionic->ntxqs_per_lif;
	if (ch->combined_count) {
		if (ch->combined_count > max_cnt)
			return -EINVAL;

		if (test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state))
			netdev_info(lif->netdev, "Sharing queue interrupts\n");
		else if (ch->combined_count == lif->nxqs)
			return 0;

		if (lif->nxqs != ch->combined_count)
			netdev_info(netdev, "Changing queue count from %d to %d\n",
				    lif->nxqs, ch->combined_count);

		qparam.nxqs = ch->combined_count;
		qparam.intr_split = false;
	} else {
		max_cnt /= 2;
		if (ch->rx_count > max_cnt)
			return -EINVAL;

		if (!test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state))
			netdev_info(lif->netdev, "Splitting queue interrupts\n");
		else if (ch->rx_count == lif->nxqs)
			return 0;

		if (lif->nxqs != ch->rx_count)
			netdev_info(netdev, "Changing queue count from %d to %d\n",
				    lif->nxqs, ch->rx_count);

		qparam.nxqs = ch->rx_count;
		qparam.intr_split = true;
	}

	err = ionic_validate_cmb_config(lif, &qparam);
	if (err < 0)
		return err;

	/* if we're not running, just set the values and return */
	if (!netif_running(lif->netdev)) {
		lif->nxqs = qparam.nxqs;

		if (qparam.intr_split) {
			set_bit(IONIC_LIF_F_SPLIT_INTR, lif->state);
		} else {
			clear_bit(IONIC_LIF_F_SPLIT_INTR, lif->state);
			lif->tx_coalesce_usecs = lif->rx_coalesce_usecs;
			lif->tx_coalesce_hw = lif->rx_coalesce_hw;
		}
		return 0;
	}

	mutex_lock(&lif->queue_lock);
	err = ionic_reconfigure_queues(lif, &qparam);
	mutex_unlock(&lif->queue_lock);
	if (err)
		netdev_info(netdev, "Queue reconfiguration failed, changes canceled: %d\n", err);

	return err;
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
		netdev_dbg(netdev, "Command parameter %d is not supported\n",
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

static int ionic_get_rxfh(struct net_device *netdev,
			  struct ethtool_rxfh_param *rxfh)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	unsigned int i, tbl_sz;

	if (rxfh->indir) {
		tbl_sz = le16_to_cpu(lif->ionic->ident.lif.eth.rss_ind_tbl_sz);
		for (i = 0; i < tbl_sz; i++)
			rxfh->indir[i] = lif->rss_ind_tbl[i];
	}

	if (rxfh->key)
		memcpy(rxfh->key, lif->rss_hash_key, IONIC_RSS_HASH_KEY_SIZE);

	rxfh->hfunc = ETH_RSS_HASH_TOP;

	return 0;
}

static int ionic_set_rxfh(struct net_device *netdev,
			  struct ethtool_rxfh_param *rxfh,
			  struct netlink_ext_ack *extack)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	if (rxfh->hfunc != ETH_RSS_HASH_NO_CHANGE &&
	    rxfh->hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	return ionic_lif_rss_config(lif, lif->rss_types,
				    rxfh->key, rxfh->indir);
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
	struct sfp_eeprom_base *sfp;

	xcvr = &idev->port_info->status.xcvr;
	sfp = (struct sfp_eeprom_base *) xcvr->sprom;

	/* report the module data type and length */
	switch (sfp->phys_id) {
	case SFF8024_ID_SFP:
		modinfo->type = ETH_MODULE_SFF_8079;
		modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
		break;
	case SFF8024_ID_QSFP_8436_8636:
	case SFF8024_ID_QSFP28_8636:
		modinfo->type = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		break;
	default:
		netdev_info(netdev, "unknown xcvr type 0x%02x\n",
			    xcvr->sprom[0]);
		modinfo->type = 0;
		modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
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

static int ionic_get_ts_info(struct net_device *netdev,
			     struct ethtool_ts_info *info)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic *ionic = lif->ionic;
	__le64 mask;

	if (!lif->phc || !lif->phc->ptp)
		return ethtool_op_get_ts_info(netdev, info);

	info->phc_index = ptp_clock_index(lif->phc->ptp);

	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	/* tx modes */

	info->tx_types = BIT(HWTSTAMP_TX_OFF) |
			 BIT(HWTSTAMP_TX_ON);

	mask = cpu_to_le64(BIT_ULL(IONIC_TXSTAMP_ONESTEP_SYNC));
	if (ionic->ident.lif.eth.hwstamp_tx_modes & mask)
		info->tx_types |= BIT(HWTSTAMP_TX_ONESTEP_SYNC);

	mask = cpu_to_le64(BIT_ULL(IONIC_TXSTAMP_ONESTEP_P2P));
	if (ionic->ident.lif.eth.hwstamp_tx_modes & mask)
		info->tx_types |= BIT(HWTSTAMP_TX_ONESTEP_P2P);

	/* rx filters */

	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_ALL);

	mask = cpu_to_le64(IONIC_PKT_CLS_NTP_ALL);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_NTP_ALL);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP1_SYNC);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V1_L4_SYNC);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP1_DREQ);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP1_ALL);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V1_L4_EVENT);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP2_L4_SYNC);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V2_L4_SYNC);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP2_L4_DREQ);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP2_L4_ALL);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP2_L2_SYNC);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V2_L2_SYNC);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP2_L2_DREQ);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP2_L2_ALL);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP2_SYNC);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V2_SYNC);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP2_DREQ);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V2_DELAY_REQ);

	mask = cpu_to_le64(IONIC_PKT_CLS_PTP2_ALL);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) == mask)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V2_EVENT);

	return 0;
}

static int ionic_nway_reset(struct net_device *netdev)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic *ionic = lif->ionic;
	int err = 0;

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return -EBUSY;

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
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_TX,
	.supported_ring_params = ETHTOOL_RING_USE_TX_PUSH |
				 ETHTOOL_RING_USE_RX_PUSH,
	.get_drvinfo		= ionic_get_drvinfo,
	.get_regs_len		= ionic_get_regs_len,
	.get_regs		= ionic_get_regs,
	.get_link		= ethtool_op_get_link,
	.get_link_ext_stats	= ionic_get_link_ext_stats,
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
	.get_ts_info		= ionic_get_ts_info,
	.nway_reset		= ionic_nway_reset,
};

void ionic_ethtool_set_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ionic_ethtool_ops;
}
