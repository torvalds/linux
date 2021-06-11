// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2018
 */

#define KMSG_COMPONENT "qeth"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/ethtool.h>
#include "qeth_core.h"


#define QETH_TXQ_STAT(_name, _stat) { \
	.name = _name, \
	.offset = offsetof(struct qeth_out_q_stats, _stat) \
}

#define QETH_CARD_STAT(_name, _stat) { \
	.name = _name, \
	.offset = offsetof(struct qeth_card_stats, _stat) \
}

struct qeth_stats {
	char name[ETH_GSTRING_LEN];
	unsigned int offset;
};

static const struct qeth_stats txq_stats[] = {
	QETH_TXQ_STAT("IO buffers", bufs),
	QETH_TXQ_STAT("IO buffer elements", buf_elements),
	QETH_TXQ_STAT("packed IO buffers", bufs_pack),
	QETH_TXQ_STAT("skbs", tx_packets),
	QETH_TXQ_STAT("packed skbs", skbs_pack),
	QETH_TXQ_STAT("SG skbs", skbs_sg),
	QETH_TXQ_STAT("HW csum skbs", skbs_csum),
	QETH_TXQ_STAT("TSO skbs", skbs_tso),
	QETH_TXQ_STAT("linearized skbs", skbs_linearized),
	QETH_TXQ_STAT("linearized+error skbs", skbs_linearized_fail),
	QETH_TXQ_STAT("TSO bytes", tso_bytes),
	QETH_TXQ_STAT("Packing mode switches", packing_mode_switch),
	QETH_TXQ_STAT("Queue stopped", stopped),
	QETH_TXQ_STAT("Doorbell", doorbell),
	QETH_TXQ_STAT("IRQ for frames", coal_frames),
	QETH_TXQ_STAT("Completion IRQ", completion_irq),
	QETH_TXQ_STAT("Completion yield", completion_yield),
	QETH_TXQ_STAT("Completion timer", completion_timer),
};

static const struct qeth_stats card_stats[] = {
	QETH_CARD_STAT("rx0 IO buffers", rx_bufs),
	QETH_CARD_STAT("rx0 HW csum skbs", rx_skb_csum),
	QETH_CARD_STAT("rx0 SG skbs", rx_sg_skbs),
	QETH_CARD_STAT("rx0 SG page frags", rx_sg_frags),
	QETH_CARD_STAT("rx0 SG page allocs", rx_sg_alloc_page),
	QETH_CARD_STAT("rx0 dropped, no memory", rx_dropped_nomem),
	QETH_CARD_STAT("rx0 dropped, bad format", rx_dropped_notsupp),
	QETH_CARD_STAT("rx0 dropped, runt", rx_dropped_runt),
};

#define TXQ_STATS_LEN	ARRAY_SIZE(txq_stats)
#define CARD_STATS_LEN	ARRAY_SIZE(card_stats)

static void qeth_add_stat_data(u64 **dst, void *src,
			       const struct qeth_stats stats[],
			       unsigned int size)
{
	unsigned int i;
	char *stat;

	for (i = 0; i < size; i++) {
		stat = (char *)src + stats[i].offset;
		**dst = *(u64 *)stat;
		(*dst)++;
	}
}

static void qeth_add_stat_strings(u8 **data, const char *prefix,
				  const struct qeth_stats stats[],
				  unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		ethtool_sprintf(data, "%s%s", prefix, stats[i].name);
}

static int qeth_get_sset_count(struct net_device *dev, int stringset)
{
	struct qeth_card *card = dev->ml_priv;

	switch (stringset) {
	case ETH_SS_STATS:
		return CARD_STATS_LEN +
		       card->qdio.no_out_queues * TXQ_STATS_LEN;
	default:
		return -EINVAL;
	}
}

static void qeth_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct qeth_card *card = dev->ml_priv;
	unsigned int i;

	qeth_add_stat_data(&data, &card->stats, card_stats, CARD_STATS_LEN);
	for (i = 0; i < card->qdio.no_out_queues; i++)
		qeth_add_stat_data(&data, &card->qdio.out_qs[i]->stats,
				   txq_stats, TXQ_STATS_LEN);
}

static void __qeth_set_coalesce(struct net_device *dev,
				struct qeth_qdio_out_q *queue,
				struct ethtool_coalesce *coal)
{
	WRITE_ONCE(queue->coalesce_usecs, coal->tx_coalesce_usecs);
	WRITE_ONCE(queue->max_coalesced_frames, coal->tx_max_coalesced_frames);

	if (coal->tx_coalesce_usecs &&
	    netif_running(dev) &&
	    !qeth_out_queue_is_empty(queue))
		qeth_tx_arm_timer(queue, coal->tx_coalesce_usecs);
}

static int qeth_set_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *coal)
{
	struct qeth_card *card = dev->ml_priv;
	struct qeth_qdio_out_q *queue;
	unsigned int i;

	if (!IS_IQD(card))
		return -EOPNOTSUPP;

	if (!coal->tx_coalesce_usecs && !coal->tx_max_coalesced_frames)
		return -EINVAL;

	qeth_for_each_output_queue(card, queue, i)
		__qeth_set_coalesce(dev, queue, coal);

	return 0;
}

static void qeth_get_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *param)
{
	struct qeth_card *card = dev->ml_priv;

	param->rx_max_pending = QDIO_MAX_BUFFERS_PER_Q;
	param->rx_mini_max_pending = 0;
	param->rx_jumbo_max_pending = 0;
	param->tx_max_pending = QDIO_MAX_BUFFERS_PER_Q;

	param->rx_pending = card->qdio.in_buf_pool.buf_count;
	param->rx_mini_pending = 0;
	param->rx_jumbo_pending = 0;
	param->tx_pending = QDIO_MAX_BUFFERS_PER_Q;
}

static void qeth_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	struct qeth_card *card = dev->ml_priv;
	char prefix[ETH_GSTRING_LEN] = "";
	unsigned int i;

	switch (stringset) {
	case ETH_SS_STATS:
		qeth_add_stat_strings(&data, prefix, card_stats,
				      CARD_STATS_LEN);
		for (i = 0; i < card->qdio.no_out_queues; i++) {
			snprintf(prefix, ETH_GSTRING_LEN, "tx%u ", i);
			qeth_add_stat_strings(&data, prefix, txq_stats,
					      TXQ_STATS_LEN);
		}
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static void qeth_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	struct qeth_card *card = dev->ml_priv;

	strlcpy(info->driver, IS_LAYER2(card) ? "qeth_l2" : "qeth_l3",
		sizeof(info->driver));
	strlcpy(info->fw_version, card->info.mcl_level,
		sizeof(info->fw_version));
	snprintf(info->bus_info, sizeof(info->bus_info), "%s/%s/%s",
		 CARD_RDEV_ID(card), CARD_WDEV_ID(card), CARD_DDEV_ID(card));
}

static void qeth_get_channels(struct net_device *dev,
			      struct ethtool_channels *channels)
{
	struct qeth_card *card = dev->ml_priv;

	channels->max_rx = dev->num_rx_queues;
	channels->max_tx = card->qdio.no_out_queues;
	channels->max_other = 0;
	channels->max_combined = 0;
	channels->rx_count = dev->real_num_rx_queues;
	channels->tx_count = dev->real_num_tx_queues;
	channels->other_count = 0;
	channels->combined_count = 0;
}

static int qeth_set_channels(struct net_device *dev,
			     struct ethtool_channels *channels)
{
	struct qeth_priv *priv = netdev_priv(dev);
	struct qeth_card *card = dev->ml_priv;
	int rc;

	if (channels->rx_count == 0 || channels->tx_count == 0)
		return -EINVAL;
	if (channels->tx_count > card->qdio.no_out_queues)
		return -EINVAL;

	/* Prio-queueing needs all TX queues: */
	if (qeth_uses_tx_prio_queueing(card))
		return -EPERM;

	if (IS_IQD(card)) {
		if (channels->tx_count < QETH_IQD_MIN_TXQ)
			return -EINVAL;

		/* Reject downgrade while running. It could push displaced
		 * ucast flows onto txq0, which is reserved for mcast.
		 */
		if (netif_running(dev) &&
		    channels->tx_count < dev->real_num_tx_queues)
			return -EPERM;
	}

	rc = qeth_set_real_num_tx_queues(card, channels->tx_count);
	if (!rc)
		priv->tx_wanted_queues = channels->tx_count;

	return rc;
}

static int qeth_get_ts_info(struct net_device *dev,
			    struct ethtool_ts_info *info)
{
	struct qeth_card *card = dev->ml_priv;

	if (!IS_IQD(card))
		return -EOPNOTSUPP;

	return ethtool_op_get_ts_info(dev, info);
}

static int qeth_get_tunable(struct net_device *dev,
			    const struct ethtool_tunable *tuna, void *data)
{
	struct qeth_priv *priv = netdev_priv(dev);

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)data = priv->rx_copybreak;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int qeth_set_tunable(struct net_device *dev,
			    const struct ethtool_tunable *tuna,
			    const void *data)
{
	struct qeth_priv *priv = netdev_priv(dev);

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		WRITE_ONCE(priv->rx_copybreak, *(u32 *)data);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int qeth_get_per_queue_coalesce(struct net_device *dev, u32 __queue,
				       struct ethtool_coalesce *coal)
{
	struct qeth_card *card = dev->ml_priv;
	struct qeth_qdio_out_q *queue;

	if (!IS_IQD(card))
		return -EOPNOTSUPP;

	if (__queue >= card->qdio.no_out_queues)
		return -EINVAL;

	queue = card->qdio.out_qs[__queue];

	coal->tx_coalesce_usecs = queue->coalesce_usecs;
	coal->tx_max_coalesced_frames = queue->max_coalesced_frames;
	return 0;
}

static int qeth_set_per_queue_coalesce(struct net_device *dev, u32 queue,
				       struct ethtool_coalesce *coal)
{
	struct qeth_card *card = dev->ml_priv;

	if (!IS_IQD(card))
		return -EOPNOTSUPP;

	if (queue >= card->qdio.no_out_queues)
		return -EINVAL;

	if (!coal->tx_coalesce_usecs && !coal->tx_max_coalesced_frames)
		return -EINVAL;

	__qeth_set_coalesce(dev, card->qdio.out_qs[queue], coal);
	return 0;
}

/* Helper function to fill 'advertising' and 'supported' which are the same. */
/* Autoneg and full-duplex are supported and advertised unconditionally.     */
/* Always advertise and support all speeds up to specified, and only one     */
/* specified port type.							     */
static void qeth_set_ethtool_link_modes(struct ethtool_link_ksettings *cmd,
					enum qeth_link_mode link_mode)
{
	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);
	ethtool_link_ksettings_zero_link_mode(cmd, lp_advertising);

	ethtool_link_ksettings_add_link_mode(cmd, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, Autoneg);

	switch (cmd->base.port) {
	case PORT_TP:
		ethtool_link_ksettings_add_link_mode(cmd, supported, TP);
		ethtool_link_ksettings_add_link_mode(cmd, advertising, TP);

		switch (cmd->base.speed) {
		case SPEED_10000:
			ethtool_link_ksettings_add_link_mode(cmd, supported,
							     10000baseT_Full);
			ethtool_link_ksettings_add_link_mode(cmd, advertising,
							     10000baseT_Full);
			fallthrough;
		case SPEED_1000:
			ethtool_link_ksettings_add_link_mode(cmd, supported,
							     1000baseT_Full);
			ethtool_link_ksettings_add_link_mode(cmd, advertising,
							     1000baseT_Full);
			ethtool_link_ksettings_add_link_mode(cmd, supported,
							     1000baseT_Half);
			ethtool_link_ksettings_add_link_mode(cmd, advertising,
							     1000baseT_Half);
			fallthrough;
		case SPEED_100:
			ethtool_link_ksettings_add_link_mode(cmd, supported,
							     100baseT_Full);
			ethtool_link_ksettings_add_link_mode(cmd, advertising,
							     100baseT_Full);
			ethtool_link_ksettings_add_link_mode(cmd, supported,
							     100baseT_Half);
			ethtool_link_ksettings_add_link_mode(cmd, advertising,
							     100baseT_Half);
			fallthrough;
		case SPEED_10:
			ethtool_link_ksettings_add_link_mode(cmd, supported,
							     10baseT_Full);
			ethtool_link_ksettings_add_link_mode(cmd, advertising,
							     10baseT_Full);
			ethtool_link_ksettings_add_link_mode(cmd, supported,
							     10baseT_Half);
			ethtool_link_ksettings_add_link_mode(cmd, advertising,
							     10baseT_Half);
			break;
		default:
			break;
		}

		break;
	case PORT_FIBRE:
		ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(cmd, advertising, FIBRE);

		switch (cmd->base.speed) {
		case SPEED_25000:
			ethtool_link_ksettings_add_link_mode(cmd, supported,
							     25000baseSR_Full);
			ethtool_link_ksettings_add_link_mode(cmd, advertising,
							     25000baseSR_Full);
			break;
		case SPEED_10000:
			if (link_mode == QETH_LINK_MODE_FIBRE_LONG) {
				ethtool_link_ksettings_add_link_mode(cmd, supported,
								     10000baseLR_Full);
				ethtool_link_ksettings_add_link_mode(cmd, advertising,
								     10000baseLR_Full);
			} else if (link_mode == QETH_LINK_MODE_FIBRE_SHORT) {
				ethtool_link_ksettings_add_link_mode(cmd, supported,
								     10000baseSR_Full);
				ethtool_link_ksettings_add_link_mode(cmd, advertising,
								     10000baseSR_Full);
			}
			break;
		case SPEED_1000:
			ethtool_link_ksettings_add_link_mode(cmd, supported,
							     1000baseX_Full);
			ethtool_link_ksettings_add_link_mode(cmd, advertising,
							     1000baseX_Full);
			break;
		default:
			break;
		}

		break;
	default:
		break;
	}
}

static int qeth_get_link_ksettings(struct net_device *netdev,
				   struct ethtool_link_ksettings *cmd)
{
	struct qeth_card *card = netdev->ml_priv;
	struct qeth_link_info link_info;

	cmd->base.speed = card->info.link_info.speed;
	cmd->base.duplex = card->info.link_info.duplex;
	cmd->base.port = card->info.link_info.port;
	cmd->base.autoneg = AUTONEG_ENABLE;
	cmd->base.phy_address = 0;
	cmd->base.mdio_support = 0;
	cmd->base.eth_tp_mdix = ETH_TP_MDI_INVALID;
	cmd->base.eth_tp_mdix_ctrl = ETH_TP_MDI_INVALID;

	/* Check if we can obtain more accurate information.	 */
	if (!qeth_query_card_info(card, &link_info)) {
		if (link_info.speed != SPEED_UNKNOWN)
			cmd->base.speed = link_info.speed;
		if (link_info.duplex != DUPLEX_UNKNOWN)
			cmd->base.duplex = link_info.duplex;
		if (link_info.port != PORT_OTHER)
			cmd->base.port = link_info.port;
	}

	qeth_set_ethtool_link_modes(cmd, card->info.link_info.link_mode);

	return 0;
}

const struct ethtool_ops qeth_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_TX_USECS |
				     ETHTOOL_COALESCE_TX_MAX_FRAMES,
	.get_link = ethtool_op_get_link,
	.set_coalesce = qeth_set_coalesce,
	.get_ringparam = qeth_get_ringparam,
	.get_strings = qeth_get_strings,
	.get_ethtool_stats = qeth_get_ethtool_stats,
	.get_sset_count = qeth_get_sset_count,
	.get_drvinfo = qeth_get_drvinfo,
	.get_channels = qeth_get_channels,
	.set_channels = qeth_set_channels,
	.get_ts_info = qeth_get_ts_info,
	.get_tunable = qeth_get_tunable,
	.set_tunable = qeth_set_tunable,
	.get_per_queue_coalesce = qeth_get_per_queue_coalesce,
	.set_per_queue_coalesce = qeth_set_per_queue_coalesce,
	.get_link_ksettings = qeth_get_link_ksettings,
};

const struct ethtool_ops qeth_osn_ethtool_ops = {
	.get_strings = qeth_get_strings,
	.get_ethtool_stats = qeth_get_ethtool_stats,
	.get_sset_count = qeth_get_sset_count,
	.get_drvinfo = qeth_get_drvinfo,
};
