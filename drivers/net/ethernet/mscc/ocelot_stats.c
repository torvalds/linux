// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Statistics for Ocelot switch family
 *
 * Copyright (c) 2017 Microsemi Corporation
 * Copyright 2022 NXP
 */
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "ocelot.h"

/* Read the counters from hardware and keep them in region->buf.
 * Caller must hold &ocelot->stat_view_lock.
 */
static int ocelot_port_update_stats(struct ocelot *ocelot, int port)
{
	struct ocelot_stats_region *region;
	int err;

	/* Configure the port to read the stats from */
	ocelot_write(ocelot, SYS_STAT_CFG_STAT_VIEW(port), SYS_STAT_CFG);

	list_for_each_entry(region, &ocelot->stats_regions, node) {
		err = ocelot_bulk_read(ocelot, region->base, region->buf,
				       region->count);
		if (err)
			return err;
	}

	return 0;
}

/* Transfer the counters from region->buf to ocelot->stats.
 * Caller must hold &ocelot->stat_view_lock and &ocelot->stats_lock.
 */
static void ocelot_port_transfer_stats(struct ocelot *ocelot, int port)
{
	unsigned int idx = port * OCELOT_NUM_STATS;
	struct ocelot_stats_region *region;
	int j;

	list_for_each_entry(region, &ocelot->stats_regions, node) {
		for (j = 0; j < region->count; j++) {
			u64 *stat = &ocelot->stats[idx + j];
			u64 val = region->buf[j];

			if (val < (*stat & U32_MAX))
				*stat += (u64)1 << 32;

			*stat = (*stat & ~(u64)U32_MAX) + val;
		}

		idx += region->count;
	}
}

static void ocelot_check_stats_work(struct work_struct *work)
{
	struct delayed_work *del_work = to_delayed_work(work);
	struct ocelot *ocelot = container_of(del_work, struct ocelot,
					     stats_work);
	int port, err;

	mutex_lock(&ocelot->stat_view_lock);

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		err = ocelot_port_update_stats(ocelot, port);
		if (err)
			break;

		spin_lock(&ocelot->stats_lock);
		ocelot_port_transfer_stats(ocelot, port);
		spin_unlock(&ocelot->stats_lock);
	}

	if (!err && ocelot->ops->update_stats)
		ocelot->ops->update_stats(ocelot);

	mutex_unlock(&ocelot->stat_view_lock);

	if (err)
		dev_err(ocelot->dev, "Error %d updating ethtool stats\n",  err);

	queue_delayed_work(ocelot->stats_queue, &ocelot->stats_work,
			   OCELOT_STATS_CHECK_DELAY);
}

void ocelot_get_strings(struct ocelot *ocelot, int port, u32 sset, u8 *data)
{
	int i;

	if (sset != ETH_SS_STATS)
		return;

	for (i = 0; i < OCELOT_NUM_STATS; i++) {
		if (ocelot->stats_layout[i].name[0] == '\0')
			continue;

		memcpy(data + i * ETH_GSTRING_LEN, ocelot->stats_layout[i].name,
		       ETH_GSTRING_LEN);
	}
}
EXPORT_SYMBOL(ocelot_get_strings);

/* Update ocelot->stats for the given port and run the given callback */
static void ocelot_port_stats_run(struct ocelot *ocelot, int port, void *priv,
				  void (*cb)(struct ocelot *ocelot, int port,
					     void *priv))
{
	int err;

	mutex_lock(&ocelot->stat_view_lock);

	err = ocelot_port_update_stats(ocelot, port);
	if (err) {
		dev_err(ocelot->dev, "Failed to update port %d stats: %pe\n",
			port, ERR_PTR(err));
		goto out_unlock;
	}

	spin_lock(&ocelot->stats_lock);

	ocelot_port_transfer_stats(ocelot, port);
	cb(ocelot, port, priv);

	spin_unlock(&ocelot->stats_lock);

out_unlock:
	mutex_unlock(&ocelot->stat_view_lock);
}

int ocelot_get_sset_count(struct ocelot *ocelot, int port, int sset)
{
	int i, num_stats = 0;

	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	for (i = 0; i < OCELOT_NUM_STATS; i++)
		if (ocelot->stats_layout[i].name[0] != '\0')
			num_stats++;

	return num_stats;
}
EXPORT_SYMBOL(ocelot_get_sset_count);

static void ocelot_port_ethtool_stats_cb(struct ocelot *ocelot, int port,
					 void *priv)
{
	u64 *data = priv;
	int i;

	/* Copy all supported counters */
	for (i = 0; i < OCELOT_NUM_STATS; i++) {
		int index = port * OCELOT_NUM_STATS + i;

		if (ocelot->stats_layout[i].name[0] == '\0')
			continue;

		*data++ = ocelot->stats[index];
	}
}

void ocelot_get_ethtool_stats(struct ocelot *ocelot, int port, u64 *data)
{
	ocelot_port_stats_run(ocelot, port, data, ocelot_port_ethtool_stats_cb);
}
EXPORT_SYMBOL(ocelot_get_ethtool_stats);

static void ocelot_port_pause_stats_cb(struct ocelot *ocelot, int port, void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_pause_stats *pause_stats = priv;

	pause_stats->tx_pause_frames = s[OCELOT_STAT_TX_PAUSE];
	pause_stats->rx_pause_frames = s[OCELOT_STAT_RX_PAUSE];
}

void ocelot_port_get_pause_stats(struct ocelot *ocelot, int port,
				 struct ethtool_pause_stats *pause_stats)
{
	ocelot_port_stats_run(ocelot, port, pause_stats,
			      ocelot_port_pause_stats_cb);
}
EXPORT_SYMBOL_GPL(ocelot_port_get_pause_stats);

static const struct ethtool_rmon_hist_range ocelot_rmon_ranges[] = {
	{   64,    64 },
	{   65,   127 },
	{  128,   255 },
	{  256,   511 },
	{  512,  1023 },
	{ 1024,  1526 },
	{ 1527, 65535 },
	{},
};

static void ocelot_port_rmon_stats_cb(struct ocelot *ocelot, int port, void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_rmon_stats *rmon_stats = priv;

	rmon_stats->undersize_pkts = s[OCELOT_STAT_RX_SHORTS];
	rmon_stats->oversize_pkts = s[OCELOT_STAT_RX_LONGS];
	rmon_stats->fragments = s[OCELOT_STAT_RX_FRAGMENTS];
	rmon_stats->jabbers = s[OCELOT_STAT_RX_JABBERS];

	rmon_stats->hist[0] = s[OCELOT_STAT_RX_64];
	rmon_stats->hist[1] = s[OCELOT_STAT_RX_65_127];
	rmon_stats->hist[2] = s[OCELOT_STAT_RX_128_255];
	rmon_stats->hist[3] = s[OCELOT_STAT_RX_256_511];
	rmon_stats->hist[4] = s[OCELOT_STAT_RX_512_1023];
	rmon_stats->hist[5] = s[OCELOT_STAT_RX_1024_1526];
	rmon_stats->hist[6] = s[OCELOT_STAT_RX_1527_MAX];

	rmon_stats->hist_tx[0] = s[OCELOT_STAT_TX_64];
	rmon_stats->hist_tx[1] = s[OCELOT_STAT_TX_65_127];
	rmon_stats->hist_tx[2] = s[OCELOT_STAT_TX_128_255];
	rmon_stats->hist_tx[3] = s[OCELOT_STAT_TX_256_511];
	rmon_stats->hist_tx[4] = s[OCELOT_STAT_TX_512_1023];
	rmon_stats->hist_tx[5] = s[OCELOT_STAT_TX_1024_1526];
	rmon_stats->hist_tx[6] = s[OCELOT_STAT_TX_1527_MAX];
}

void ocelot_port_get_rmon_stats(struct ocelot *ocelot, int port,
				struct ethtool_rmon_stats *rmon_stats,
				const struct ethtool_rmon_hist_range **ranges)
{
	*ranges = ocelot_rmon_ranges;

	ocelot_port_stats_run(ocelot, port, rmon_stats,
			      ocelot_port_rmon_stats_cb);
}
EXPORT_SYMBOL_GPL(ocelot_port_get_rmon_stats);

static void ocelot_port_ctrl_stats_cb(struct ocelot *ocelot, int port, void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_eth_ctrl_stats *ctrl_stats = priv;

	ctrl_stats->MACControlFramesReceived = s[OCELOT_STAT_RX_CONTROL];
}

void ocelot_port_get_eth_ctrl_stats(struct ocelot *ocelot, int port,
				    struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	ocelot_port_stats_run(ocelot, port, ctrl_stats,
			      ocelot_port_ctrl_stats_cb);
}
EXPORT_SYMBOL_GPL(ocelot_port_get_eth_ctrl_stats);

static void ocelot_port_mac_stats_cb(struct ocelot *ocelot, int port, void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_eth_mac_stats *mac_stats = priv;

	mac_stats->OctetsTransmittedOK = s[OCELOT_STAT_TX_OCTETS];
	mac_stats->FramesTransmittedOK = s[OCELOT_STAT_TX_64] +
					 s[OCELOT_STAT_TX_65_127] +
					 s[OCELOT_STAT_TX_128_255] +
					 s[OCELOT_STAT_TX_256_511] +
					 s[OCELOT_STAT_TX_512_1023] +
					 s[OCELOT_STAT_TX_1024_1526] +
					 s[OCELOT_STAT_TX_1527_MAX];
	mac_stats->OctetsReceivedOK = s[OCELOT_STAT_RX_OCTETS];
	mac_stats->FramesReceivedOK = s[OCELOT_STAT_RX_GREEN_PRIO_0] +
				      s[OCELOT_STAT_RX_GREEN_PRIO_1] +
				      s[OCELOT_STAT_RX_GREEN_PRIO_2] +
				      s[OCELOT_STAT_RX_GREEN_PRIO_3] +
				      s[OCELOT_STAT_RX_GREEN_PRIO_4] +
				      s[OCELOT_STAT_RX_GREEN_PRIO_5] +
				      s[OCELOT_STAT_RX_GREEN_PRIO_6] +
				      s[OCELOT_STAT_RX_GREEN_PRIO_7] +
				      s[OCELOT_STAT_RX_YELLOW_PRIO_0] +
				      s[OCELOT_STAT_RX_YELLOW_PRIO_1] +
				      s[OCELOT_STAT_RX_YELLOW_PRIO_2] +
				      s[OCELOT_STAT_RX_YELLOW_PRIO_3] +
				      s[OCELOT_STAT_RX_YELLOW_PRIO_4] +
				      s[OCELOT_STAT_RX_YELLOW_PRIO_5] +
				      s[OCELOT_STAT_RX_YELLOW_PRIO_6] +
				      s[OCELOT_STAT_RX_YELLOW_PRIO_7];
	mac_stats->MulticastFramesXmittedOK = s[OCELOT_STAT_TX_MULTICAST];
	mac_stats->BroadcastFramesXmittedOK = s[OCELOT_STAT_TX_BROADCAST];
	mac_stats->MulticastFramesReceivedOK = s[OCELOT_STAT_RX_MULTICAST];
	mac_stats->BroadcastFramesReceivedOK = s[OCELOT_STAT_RX_BROADCAST];
	mac_stats->FrameTooLongErrors = s[OCELOT_STAT_RX_LONGS];
	/* Sadly, C_RX_CRC is the sum of FCS and alignment errors, they are not
	 * counted individually.
	 */
	mac_stats->FrameCheckSequenceErrors = s[OCELOT_STAT_RX_CRC_ALIGN_ERRS];
	mac_stats->AlignmentErrors = s[OCELOT_STAT_RX_CRC_ALIGN_ERRS];
}

void ocelot_port_get_eth_mac_stats(struct ocelot *ocelot, int port,
				   struct ethtool_eth_mac_stats *mac_stats)
{
	ocelot_port_stats_run(ocelot, port, mac_stats,
			      ocelot_port_mac_stats_cb);
}
EXPORT_SYMBOL_GPL(ocelot_port_get_eth_mac_stats);

static void ocelot_port_phy_stats_cb(struct ocelot *ocelot, int port, void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_eth_phy_stats *phy_stats = priv;

	phy_stats->SymbolErrorDuringCarrier = s[OCELOT_STAT_RX_SYM_ERRS];
}

void ocelot_port_get_eth_phy_stats(struct ocelot *ocelot, int port,
				   struct ethtool_eth_phy_stats *phy_stats)
{
	ocelot_port_stats_run(ocelot, port, phy_stats,
			      ocelot_port_phy_stats_cb);
}
EXPORT_SYMBOL_GPL(ocelot_port_get_eth_phy_stats);

void ocelot_port_get_stats64(struct ocelot *ocelot, int port,
			     struct rtnl_link_stats64 *stats)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];

	spin_lock(&ocelot->stats_lock);

	/* Get Rx stats */
	stats->rx_bytes = s[OCELOT_STAT_RX_OCTETS];
	stats->rx_packets = s[OCELOT_STAT_RX_SHORTS] +
			    s[OCELOT_STAT_RX_FRAGMENTS] +
			    s[OCELOT_STAT_RX_JABBERS] +
			    s[OCELOT_STAT_RX_LONGS] +
			    s[OCELOT_STAT_RX_64] +
			    s[OCELOT_STAT_RX_65_127] +
			    s[OCELOT_STAT_RX_128_255] +
			    s[OCELOT_STAT_RX_256_511] +
			    s[OCELOT_STAT_RX_512_1023] +
			    s[OCELOT_STAT_RX_1024_1526] +
			    s[OCELOT_STAT_RX_1527_MAX];
	stats->multicast = s[OCELOT_STAT_RX_MULTICAST];
	stats->rx_missed_errors = s[OCELOT_STAT_DROP_TAIL];
	stats->rx_dropped = s[OCELOT_STAT_RX_RED_PRIO_0] +
			    s[OCELOT_STAT_RX_RED_PRIO_1] +
			    s[OCELOT_STAT_RX_RED_PRIO_2] +
			    s[OCELOT_STAT_RX_RED_PRIO_3] +
			    s[OCELOT_STAT_RX_RED_PRIO_4] +
			    s[OCELOT_STAT_RX_RED_PRIO_5] +
			    s[OCELOT_STAT_RX_RED_PRIO_6] +
			    s[OCELOT_STAT_RX_RED_PRIO_7] +
			    s[OCELOT_STAT_DROP_LOCAL] +
			    s[OCELOT_STAT_DROP_YELLOW_PRIO_0] +
			    s[OCELOT_STAT_DROP_YELLOW_PRIO_1] +
			    s[OCELOT_STAT_DROP_YELLOW_PRIO_2] +
			    s[OCELOT_STAT_DROP_YELLOW_PRIO_3] +
			    s[OCELOT_STAT_DROP_YELLOW_PRIO_4] +
			    s[OCELOT_STAT_DROP_YELLOW_PRIO_5] +
			    s[OCELOT_STAT_DROP_YELLOW_PRIO_6] +
			    s[OCELOT_STAT_DROP_YELLOW_PRIO_7] +
			    s[OCELOT_STAT_DROP_GREEN_PRIO_0] +
			    s[OCELOT_STAT_DROP_GREEN_PRIO_1] +
			    s[OCELOT_STAT_DROP_GREEN_PRIO_2] +
			    s[OCELOT_STAT_DROP_GREEN_PRIO_3] +
			    s[OCELOT_STAT_DROP_GREEN_PRIO_4] +
			    s[OCELOT_STAT_DROP_GREEN_PRIO_5] +
			    s[OCELOT_STAT_DROP_GREEN_PRIO_6] +
			    s[OCELOT_STAT_DROP_GREEN_PRIO_7];

	/* Get Tx stats */
	stats->tx_bytes = s[OCELOT_STAT_TX_OCTETS];
	stats->tx_packets = s[OCELOT_STAT_TX_64] +
			    s[OCELOT_STAT_TX_65_127] +
			    s[OCELOT_STAT_TX_128_255] +
			    s[OCELOT_STAT_TX_256_511] +
			    s[OCELOT_STAT_TX_512_1023] +
			    s[OCELOT_STAT_TX_1024_1526] +
			    s[OCELOT_STAT_TX_1527_MAX];
	stats->tx_dropped = s[OCELOT_STAT_TX_DROPS] +
			    s[OCELOT_STAT_TX_AGED];
	stats->collisions = s[OCELOT_STAT_TX_COLLISION];

	spin_unlock(&ocelot->stats_lock);
}
EXPORT_SYMBOL(ocelot_port_get_stats64);

static int ocelot_prepare_stats_regions(struct ocelot *ocelot)
{
	struct ocelot_stats_region *region = NULL;
	unsigned int last;
	int i;

	INIT_LIST_HEAD(&ocelot->stats_regions);

	for (i = 0; i < OCELOT_NUM_STATS; i++) {
		if (!ocelot->stats_layout[i].reg)
			continue;

		if (region && ocelot->map[SYS][ocelot->stats_layout[i].reg & REG_MASK] ==
		    ocelot->map[SYS][last & REG_MASK] + 4) {
			region->count++;
		} else {
			region = devm_kzalloc(ocelot->dev, sizeof(*region),
					      GFP_KERNEL);
			if (!region)
				return -ENOMEM;

			region->base = ocelot->stats_layout[i].reg;
			region->count = 1;
			list_add_tail(&region->node, &ocelot->stats_regions);
		}

		last = ocelot->stats_layout[i].reg;
	}

	list_for_each_entry(region, &ocelot->stats_regions, node) {
		region->buf = devm_kcalloc(ocelot->dev, region->count,
					   sizeof(*region->buf), GFP_KERNEL);
		if (!region->buf)
			return -ENOMEM;
	}

	return 0;
}

int ocelot_stats_init(struct ocelot *ocelot)
{
	char queue_name[32];
	int ret;

	ocelot->stats = devm_kcalloc(ocelot->dev,
				     ocelot->num_phys_ports * OCELOT_NUM_STATS,
				     sizeof(u64), GFP_KERNEL);
	if (!ocelot->stats)
		return -ENOMEM;

	snprintf(queue_name, sizeof(queue_name), "%s-stats",
		 dev_name(ocelot->dev));
	ocelot->stats_queue = create_singlethread_workqueue(queue_name);
	if (!ocelot->stats_queue)
		return -ENOMEM;

	spin_lock_init(&ocelot->stats_lock);
	mutex_init(&ocelot->stat_view_lock);

	ret = ocelot_prepare_stats_regions(ocelot);
	if (ret) {
		destroy_workqueue(ocelot->stats_queue);
		return ret;
	}

	INIT_DELAYED_WORK(&ocelot->stats_work, ocelot_check_stats_work);
	queue_delayed_work(ocelot->stats_queue, &ocelot->stats_work,
			   OCELOT_STATS_CHECK_DELAY);

	return 0;
}

void ocelot_stats_deinit(struct ocelot *ocelot)
{
	cancel_delayed_work(&ocelot->stats_work);
	destroy_workqueue(ocelot->stats_queue);
}
