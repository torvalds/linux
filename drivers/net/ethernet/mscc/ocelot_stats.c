// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Statistics for Ocelot switch family
 *
 * Copyright (c) 2017 Microsemi Corporation
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

void ocelot_get_ethtool_stats(struct ocelot *ocelot, int port, u64 *data)
{
	int i, err;

	mutex_lock(&ocelot->stat_view_lock);

	/* check and update now */
	err = ocelot_port_update_stats(ocelot, port);

	spin_lock(&ocelot->stats_lock);

	ocelot_port_transfer_stats(ocelot, port);

	/* Copy all supported counters */
	for (i = 0; i < OCELOT_NUM_STATS; i++) {
		int index = port * OCELOT_NUM_STATS + i;

		if (ocelot->stats_layout[i].name[0] == '\0')
			continue;

		*data++ = ocelot->stats[index];
	}

	spin_unlock(&ocelot->stats_lock);

	mutex_unlock(&ocelot->stat_view_lock);

	if (err)
		dev_err(ocelot->dev, "Error %d updating ethtool stats\n", err);
}
EXPORT_SYMBOL(ocelot_get_ethtool_stats);

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
		if (ocelot->stats_layout[i].name[0] == '\0')
			continue;

		if (region && ocelot->stats_layout[i].reg == last + 4) {
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
