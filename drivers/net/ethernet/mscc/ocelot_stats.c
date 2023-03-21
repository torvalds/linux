// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Statistics for Ocelot switch family
 *
 * Copyright (c) 2017 Microsemi Corporation
 * Copyright 2022 NXP
 */
#include <linux/ethtool_netlink.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "ocelot.h"

enum ocelot_stat {
	OCELOT_STAT_RX_OCTETS,
	OCELOT_STAT_RX_UNICAST,
	OCELOT_STAT_RX_MULTICAST,
	OCELOT_STAT_RX_BROADCAST,
	OCELOT_STAT_RX_SHORTS,
	OCELOT_STAT_RX_FRAGMENTS,
	OCELOT_STAT_RX_JABBERS,
	OCELOT_STAT_RX_CRC_ALIGN_ERRS,
	OCELOT_STAT_RX_SYM_ERRS,
	OCELOT_STAT_RX_64,
	OCELOT_STAT_RX_65_127,
	OCELOT_STAT_RX_128_255,
	OCELOT_STAT_RX_256_511,
	OCELOT_STAT_RX_512_1023,
	OCELOT_STAT_RX_1024_1526,
	OCELOT_STAT_RX_1527_MAX,
	OCELOT_STAT_RX_PAUSE,
	OCELOT_STAT_RX_CONTROL,
	OCELOT_STAT_RX_LONGS,
	OCELOT_STAT_RX_CLASSIFIED_DROPS,
	OCELOT_STAT_RX_RED_PRIO_0,
	OCELOT_STAT_RX_RED_PRIO_1,
	OCELOT_STAT_RX_RED_PRIO_2,
	OCELOT_STAT_RX_RED_PRIO_3,
	OCELOT_STAT_RX_RED_PRIO_4,
	OCELOT_STAT_RX_RED_PRIO_5,
	OCELOT_STAT_RX_RED_PRIO_6,
	OCELOT_STAT_RX_RED_PRIO_7,
	OCELOT_STAT_RX_YELLOW_PRIO_0,
	OCELOT_STAT_RX_YELLOW_PRIO_1,
	OCELOT_STAT_RX_YELLOW_PRIO_2,
	OCELOT_STAT_RX_YELLOW_PRIO_3,
	OCELOT_STAT_RX_YELLOW_PRIO_4,
	OCELOT_STAT_RX_YELLOW_PRIO_5,
	OCELOT_STAT_RX_YELLOW_PRIO_6,
	OCELOT_STAT_RX_YELLOW_PRIO_7,
	OCELOT_STAT_RX_GREEN_PRIO_0,
	OCELOT_STAT_RX_GREEN_PRIO_1,
	OCELOT_STAT_RX_GREEN_PRIO_2,
	OCELOT_STAT_RX_GREEN_PRIO_3,
	OCELOT_STAT_RX_GREEN_PRIO_4,
	OCELOT_STAT_RX_GREEN_PRIO_5,
	OCELOT_STAT_RX_GREEN_PRIO_6,
	OCELOT_STAT_RX_GREEN_PRIO_7,
	OCELOT_STAT_RX_ASSEMBLY_ERRS,
	OCELOT_STAT_RX_SMD_ERRS,
	OCELOT_STAT_RX_ASSEMBLY_OK,
	OCELOT_STAT_RX_MERGE_FRAGMENTS,
	OCELOT_STAT_RX_PMAC_OCTETS,
	OCELOT_STAT_RX_PMAC_UNICAST,
	OCELOT_STAT_RX_PMAC_MULTICAST,
	OCELOT_STAT_RX_PMAC_BROADCAST,
	OCELOT_STAT_RX_PMAC_SHORTS,
	OCELOT_STAT_RX_PMAC_FRAGMENTS,
	OCELOT_STAT_RX_PMAC_JABBERS,
	OCELOT_STAT_RX_PMAC_CRC_ALIGN_ERRS,
	OCELOT_STAT_RX_PMAC_SYM_ERRS,
	OCELOT_STAT_RX_PMAC_64,
	OCELOT_STAT_RX_PMAC_65_127,
	OCELOT_STAT_RX_PMAC_128_255,
	OCELOT_STAT_RX_PMAC_256_511,
	OCELOT_STAT_RX_PMAC_512_1023,
	OCELOT_STAT_RX_PMAC_1024_1526,
	OCELOT_STAT_RX_PMAC_1527_MAX,
	OCELOT_STAT_RX_PMAC_PAUSE,
	OCELOT_STAT_RX_PMAC_CONTROL,
	OCELOT_STAT_RX_PMAC_LONGS,
	OCELOT_STAT_TX_OCTETS,
	OCELOT_STAT_TX_UNICAST,
	OCELOT_STAT_TX_MULTICAST,
	OCELOT_STAT_TX_BROADCAST,
	OCELOT_STAT_TX_COLLISION,
	OCELOT_STAT_TX_DROPS,
	OCELOT_STAT_TX_PAUSE,
	OCELOT_STAT_TX_64,
	OCELOT_STAT_TX_65_127,
	OCELOT_STAT_TX_128_255,
	OCELOT_STAT_TX_256_511,
	OCELOT_STAT_TX_512_1023,
	OCELOT_STAT_TX_1024_1526,
	OCELOT_STAT_TX_1527_MAX,
	OCELOT_STAT_TX_YELLOW_PRIO_0,
	OCELOT_STAT_TX_YELLOW_PRIO_1,
	OCELOT_STAT_TX_YELLOW_PRIO_2,
	OCELOT_STAT_TX_YELLOW_PRIO_3,
	OCELOT_STAT_TX_YELLOW_PRIO_4,
	OCELOT_STAT_TX_YELLOW_PRIO_5,
	OCELOT_STAT_TX_YELLOW_PRIO_6,
	OCELOT_STAT_TX_YELLOW_PRIO_7,
	OCELOT_STAT_TX_GREEN_PRIO_0,
	OCELOT_STAT_TX_GREEN_PRIO_1,
	OCELOT_STAT_TX_GREEN_PRIO_2,
	OCELOT_STAT_TX_GREEN_PRIO_3,
	OCELOT_STAT_TX_GREEN_PRIO_4,
	OCELOT_STAT_TX_GREEN_PRIO_5,
	OCELOT_STAT_TX_GREEN_PRIO_6,
	OCELOT_STAT_TX_GREEN_PRIO_7,
	OCELOT_STAT_TX_AGED,
	OCELOT_STAT_TX_MM_HOLD,
	OCELOT_STAT_TX_MERGE_FRAGMENTS,
	OCELOT_STAT_TX_PMAC_OCTETS,
	OCELOT_STAT_TX_PMAC_UNICAST,
	OCELOT_STAT_TX_PMAC_MULTICAST,
	OCELOT_STAT_TX_PMAC_BROADCAST,
	OCELOT_STAT_TX_PMAC_PAUSE,
	OCELOT_STAT_TX_PMAC_64,
	OCELOT_STAT_TX_PMAC_65_127,
	OCELOT_STAT_TX_PMAC_128_255,
	OCELOT_STAT_TX_PMAC_256_511,
	OCELOT_STAT_TX_PMAC_512_1023,
	OCELOT_STAT_TX_PMAC_1024_1526,
	OCELOT_STAT_TX_PMAC_1527_MAX,
	OCELOT_STAT_DROP_LOCAL,
	OCELOT_STAT_DROP_TAIL,
	OCELOT_STAT_DROP_YELLOW_PRIO_0,
	OCELOT_STAT_DROP_YELLOW_PRIO_1,
	OCELOT_STAT_DROP_YELLOW_PRIO_2,
	OCELOT_STAT_DROP_YELLOW_PRIO_3,
	OCELOT_STAT_DROP_YELLOW_PRIO_4,
	OCELOT_STAT_DROP_YELLOW_PRIO_5,
	OCELOT_STAT_DROP_YELLOW_PRIO_6,
	OCELOT_STAT_DROP_YELLOW_PRIO_7,
	OCELOT_STAT_DROP_GREEN_PRIO_0,
	OCELOT_STAT_DROP_GREEN_PRIO_1,
	OCELOT_STAT_DROP_GREEN_PRIO_2,
	OCELOT_STAT_DROP_GREEN_PRIO_3,
	OCELOT_STAT_DROP_GREEN_PRIO_4,
	OCELOT_STAT_DROP_GREEN_PRIO_5,
	OCELOT_STAT_DROP_GREEN_PRIO_6,
	OCELOT_STAT_DROP_GREEN_PRIO_7,
	OCELOT_NUM_STATS,
};

struct ocelot_stat_layout {
	u32 reg;
	char name[ETH_GSTRING_LEN];
};

/* 32-bit counter checked for wraparound by ocelot_port_update_stats()
 * and copied to ocelot->stats.
 */
#define OCELOT_STAT(kind) \
	[OCELOT_STAT_ ## kind] = { .reg = SYS_COUNT_ ## kind }
/* Same as above, except also exported to ethtool -S. Standard counters should
 * only be exposed to more specific interfaces rather than by their string name.
 */
#define OCELOT_STAT_ETHTOOL(kind, ethtool_name) \
	[OCELOT_STAT_ ## kind] = { .reg = SYS_COUNT_ ## kind, .name = ethtool_name }

#define OCELOT_COMMON_STATS \
	OCELOT_STAT_ETHTOOL(RX_OCTETS, "rx_octets"), \
	OCELOT_STAT_ETHTOOL(RX_UNICAST, "rx_unicast"), \
	OCELOT_STAT_ETHTOOL(RX_MULTICAST, "rx_multicast"), \
	OCELOT_STAT_ETHTOOL(RX_BROADCAST, "rx_broadcast"), \
	OCELOT_STAT_ETHTOOL(RX_SHORTS, "rx_shorts"), \
	OCELOT_STAT_ETHTOOL(RX_FRAGMENTS, "rx_fragments"), \
	OCELOT_STAT_ETHTOOL(RX_JABBERS, "rx_jabbers"), \
	OCELOT_STAT_ETHTOOL(RX_CRC_ALIGN_ERRS, "rx_crc_align_errs"), \
	OCELOT_STAT_ETHTOOL(RX_SYM_ERRS, "rx_sym_errs"), \
	OCELOT_STAT_ETHTOOL(RX_64, "rx_frames_below_65_octets"), \
	OCELOT_STAT_ETHTOOL(RX_65_127, "rx_frames_65_to_127_octets"), \
	OCELOT_STAT_ETHTOOL(RX_128_255, "rx_frames_128_to_255_octets"), \
	OCELOT_STAT_ETHTOOL(RX_256_511, "rx_frames_256_to_511_octets"), \
	OCELOT_STAT_ETHTOOL(RX_512_1023, "rx_frames_512_to_1023_octets"), \
	OCELOT_STAT_ETHTOOL(RX_1024_1526, "rx_frames_1024_to_1526_octets"), \
	OCELOT_STAT_ETHTOOL(RX_1527_MAX, "rx_frames_over_1526_octets"), \
	OCELOT_STAT_ETHTOOL(RX_PAUSE, "rx_pause"), \
	OCELOT_STAT_ETHTOOL(RX_CONTROL, "rx_control"), \
	OCELOT_STAT_ETHTOOL(RX_LONGS, "rx_longs"), \
	OCELOT_STAT_ETHTOOL(RX_CLASSIFIED_DROPS, "rx_classified_drops"), \
	OCELOT_STAT_ETHTOOL(RX_RED_PRIO_0, "rx_red_prio_0"), \
	OCELOT_STAT_ETHTOOL(RX_RED_PRIO_1, "rx_red_prio_1"), \
	OCELOT_STAT_ETHTOOL(RX_RED_PRIO_2, "rx_red_prio_2"), \
	OCELOT_STAT_ETHTOOL(RX_RED_PRIO_3, "rx_red_prio_3"), \
	OCELOT_STAT_ETHTOOL(RX_RED_PRIO_4, "rx_red_prio_4"), \
	OCELOT_STAT_ETHTOOL(RX_RED_PRIO_5, "rx_red_prio_5"), \
	OCELOT_STAT_ETHTOOL(RX_RED_PRIO_6, "rx_red_prio_6"), \
	OCELOT_STAT_ETHTOOL(RX_RED_PRIO_7, "rx_red_prio_7"), \
	OCELOT_STAT_ETHTOOL(RX_YELLOW_PRIO_0, "rx_yellow_prio_0"), \
	OCELOT_STAT_ETHTOOL(RX_YELLOW_PRIO_1, "rx_yellow_prio_1"), \
	OCELOT_STAT_ETHTOOL(RX_YELLOW_PRIO_2, "rx_yellow_prio_2"), \
	OCELOT_STAT_ETHTOOL(RX_YELLOW_PRIO_3, "rx_yellow_prio_3"), \
	OCELOT_STAT_ETHTOOL(RX_YELLOW_PRIO_4, "rx_yellow_prio_4"), \
	OCELOT_STAT_ETHTOOL(RX_YELLOW_PRIO_5, "rx_yellow_prio_5"), \
	OCELOT_STAT_ETHTOOL(RX_YELLOW_PRIO_6, "rx_yellow_prio_6"), \
	OCELOT_STAT_ETHTOOL(RX_YELLOW_PRIO_7, "rx_yellow_prio_7"), \
	OCELOT_STAT_ETHTOOL(RX_GREEN_PRIO_0, "rx_green_prio_0"), \
	OCELOT_STAT_ETHTOOL(RX_GREEN_PRIO_1, "rx_green_prio_1"), \
	OCELOT_STAT_ETHTOOL(RX_GREEN_PRIO_2, "rx_green_prio_2"), \
	OCELOT_STAT_ETHTOOL(RX_GREEN_PRIO_3, "rx_green_prio_3"), \
	OCELOT_STAT_ETHTOOL(RX_GREEN_PRIO_4, "rx_green_prio_4"), \
	OCELOT_STAT_ETHTOOL(RX_GREEN_PRIO_5, "rx_green_prio_5"), \
	OCELOT_STAT_ETHTOOL(RX_GREEN_PRIO_6, "rx_green_prio_6"), \
	OCELOT_STAT_ETHTOOL(RX_GREEN_PRIO_7, "rx_green_prio_7"), \
	OCELOT_STAT_ETHTOOL(TX_OCTETS, "tx_octets"), \
	OCELOT_STAT_ETHTOOL(TX_UNICAST, "tx_unicast"), \
	OCELOT_STAT_ETHTOOL(TX_MULTICAST, "tx_multicast"), \
	OCELOT_STAT_ETHTOOL(TX_BROADCAST, "tx_broadcast"), \
	OCELOT_STAT_ETHTOOL(TX_COLLISION, "tx_collision"), \
	OCELOT_STAT_ETHTOOL(TX_DROPS, "tx_drops"), \
	OCELOT_STAT_ETHTOOL(TX_PAUSE, "tx_pause"), \
	OCELOT_STAT_ETHTOOL(TX_64, "tx_frames_below_65_octets"), \
	OCELOT_STAT_ETHTOOL(TX_65_127, "tx_frames_65_to_127_octets"), \
	OCELOT_STAT_ETHTOOL(TX_128_255, "tx_frames_128_255_octets"), \
	OCELOT_STAT_ETHTOOL(TX_256_511, "tx_frames_256_511_octets"), \
	OCELOT_STAT_ETHTOOL(TX_512_1023, "tx_frames_512_1023_octets"), \
	OCELOT_STAT_ETHTOOL(TX_1024_1526, "tx_frames_1024_1526_octets"), \
	OCELOT_STAT_ETHTOOL(TX_1527_MAX, "tx_frames_over_1526_octets"), \
	OCELOT_STAT_ETHTOOL(TX_YELLOW_PRIO_0, "tx_yellow_prio_0"), \
	OCELOT_STAT_ETHTOOL(TX_YELLOW_PRIO_1, "tx_yellow_prio_1"), \
	OCELOT_STAT_ETHTOOL(TX_YELLOW_PRIO_2, "tx_yellow_prio_2"), \
	OCELOT_STAT_ETHTOOL(TX_YELLOW_PRIO_3, "tx_yellow_prio_3"), \
	OCELOT_STAT_ETHTOOL(TX_YELLOW_PRIO_4, "tx_yellow_prio_4"), \
	OCELOT_STAT_ETHTOOL(TX_YELLOW_PRIO_5, "tx_yellow_prio_5"), \
	OCELOT_STAT_ETHTOOL(TX_YELLOW_PRIO_6, "tx_yellow_prio_6"), \
	OCELOT_STAT_ETHTOOL(TX_YELLOW_PRIO_7, "tx_yellow_prio_7"), \
	OCELOT_STAT_ETHTOOL(TX_GREEN_PRIO_0, "tx_green_prio_0"), \
	OCELOT_STAT_ETHTOOL(TX_GREEN_PRIO_1, "tx_green_prio_1"), \
	OCELOT_STAT_ETHTOOL(TX_GREEN_PRIO_2, "tx_green_prio_2"), \
	OCELOT_STAT_ETHTOOL(TX_GREEN_PRIO_3, "tx_green_prio_3"), \
	OCELOT_STAT_ETHTOOL(TX_GREEN_PRIO_4, "tx_green_prio_4"), \
	OCELOT_STAT_ETHTOOL(TX_GREEN_PRIO_5, "tx_green_prio_5"), \
	OCELOT_STAT_ETHTOOL(TX_GREEN_PRIO_6, "tx_green_prio_6"), \
	OCELOT_STAT_ETHTOOL(TX_GREEN_PRIO_7, "tx_green_prio_7"), \
	OCELOT_STAT_ETHTOOL(TX_AGED, "tx_aged"), \
	OCELOT_STAT_ETHTOOL(DROP_LOCAL, "drop_local"), \
	OCELOT_STAT_ETHTOOL(DROP_TAIL, "drop_tail"), \
	OCELOT_STAT_ETHTOOL(DROP_YELLOW_PRIO_0, "drop_yellow_prio_0"), \
	OCELOT_STAT_ETHTOOL(DROP_YELLOW_PRIO_1, "drop_yellow_prio_1"), \
	OCELOT_STAT_ETHTOOL(DROP_YELLOW_PRIO_2, "drop_yellow_prio_2"), \
	OCELOT_STAT_ETHTOOL(DROP_YELLOW_PRIO_3, "drop_yellow_prio_3"), \
	OCELOT_STAT_ETHTOOL(DROP_YELLOW_PRIO_4, "drop_yellow_prio_4"), \
	OCELOT_STAT_ETHTOOL(DROP_YELLOW_PRIO_5, "drop_yellow_prio_5"), \
	OCELOT_STAT_ETHTOOL(DROP_YELLOW_PRIO_6, "drop_yellow_prio_6"), \
	OCELOT_STAT_ETHTOOL(DROP_YELLOW_PRIO_7, "drop_yellow_prio_7"), \
	OCELOT_STAT_ETHTOOL(DROP_GREEN_PRIO_0, "drop_green_prio_0"), \
	OCELOT_STAT_ETHTOOL(DROP_GREEN_PRIO_1, "drop_green_prio_1"), \
	OCELOT_STAT_ETHTOOL(DROP_GREEN_PRIO_2, "drop_green_prio_2"), \
	OCELOT_STAT_ETHTOOL(DROP_GREEN_PRIO_3, "drop_green_prio_3"), \
	OCELOT_STAT_ETHTOOL(DROP_GREEN_PRIO_4, "drop_green_prio_4"), \
	OCELOT_STAT_ETHTOOL(DROP_GREEN_PRIO_5, "drop_green_prio_5"), \
	OCELOT_STAT_ETHTOOL(DROP_GREEN_PRIO_6, "drop_green_prio_6"), \
	OCELOT_STAT_ETHTOOL(DROP_GREEN_PRIO_7, "drop_green_prio_7")

struct ocelot_stats_region {
	struct list_head node;
	u32 base;
	enum ocelot_stat first_stat;
	int count;
	u32 *buf;
};

static const struct ocelot_stat_layout ocelot_stats_layout[OCELOT_NUM_STATS] = {
	OCELOT_COMMON_STATS,
};

static const struct ocelot_stat_layout ocelot_mm_stats_layout[OCELOT_NUM_STATS] = {
	OCELOT_COMMON_STATS,
	OCELOT_STAT(RX_ASSEMBLY_ERRS),
	OCELOT_STAT(RX_SMD_ERRS),
	OCELOT_STAT(RX_ASSEMBLY_OK),
	OCELOT_STAT(RX_MERGE_FRAGMENTS),
	OCELOT_STAT(TX_MERGE_FRAGMENTS),
	OCELOT_STAT(TX_MM_HOLD),
	OCELOT_STAT(RX_PMAC_OCTETS),
	OCELOT_STAT(RX_PMAC_UNICAST),
	OCELOT_STAT(RX_PMAC_MULTICAST),
	OCELOT_STAT(RX_PMAC_BROADCAST),
	OCELOT_STAT(RX_PMAC_SHORTS),
	OCELOT_STAT(RX_PMAC_FRAGMENTS),
	OCELOT_STAT(RX_PMAC_JABBERS),
	OCELOT_STAT(RX_PMAC_CRC_ALIGN_ERRS),
	OCELOT_STAT(RX_PMAC_SYM_ERRS),
	OCELOT_STAT(RX_PMAC_64),
	OCELOT_STAT(RX_PMAC_65_127),
	OCELOT_STAT(RX_PMAC_128_255),
	OCELOT_STAT(RX_PMAC_256_511),
	OCELOT_STAT(RX_PMAC_512_1023),
	OCELOT_STAT(RX_PMAC_1024_1526),
	OCELOT_STAT(RX_PMAC_1527_MAX),
	OCELOT_STAT(RX_PMAC_PAUSE),
	OCELOT_STAT(RX_PMAC_CONTROL),
	OCELOT_STAT(RX_PMAC_LONGS),
	OCELOT_STAT(TX_PMAC_OCTETS),
	OCELOT_STAT(TX_PMAC_UNICAST),
	OCELOT_STAT(TX_PMAC_MULTICAST),
	OCELOT_STAT(TX_PMAC_BROADCAST),
	OCELOT_STAT(TX_PMAC_PAUSE),
	OCELOT_STAT(TX_PMAC_64),
	OCELOT_STAT(TX_PMAC_65_127),
	OCELOT_STAT(TX_PMAC_128_255),
	OCELOT_STAT(TX_PMAC_256_511),
	OCELOT_STAT(TX_PMAC_512_1023),
	OCELOT_STAT(TX_PMAC_1024_1526),
	OCELOT_STAT(TX_PMAC_1527_MAX),
};

static const struct ocelot_stat_layout *
ocelot_get_stats_layout(struct ocelot *ocelot)
{
	if (ocelot->mm_supported)
		return ocelot_mm_stats_layout;

	return ocelot_stats_layout;
}

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
	struct ocelot_stats_region *region;
	int j;

	list_for_each_entry(region, &ocelot->stats_regions, node) {
		unsigned int idx = port * OCELOT_NUM_STATS + region->first_stat;

		for (j = 0; j < region->count; j++) {
			u64 *stat = &ocelot->stats[idx + j];
			u64 val = region->buf[j];

			if (val < (*stat & U32_MAX))
				*stat += (u64)1 << 32;

			*stat = (*stat & ~(u64)U32_MAX) + val;
		}
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
	const struct ocelot_stat_layout *layout;
	int i;

	if (sset != ETH_SS_STATS)
		return;

	layout = ocelot_get_stats_layout(ocelot);

	for (i = 0; i < OCELOT_NUM_STATS; i++) {
		if (layout[i].name[0] == '\0')
			continue;

		memcpy(data, layout[i].name, ETH_GSTRING_LEN);
		data += ETH_GSTRING_LEN;
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
	const struct ocelot_stat_layout *layout;
	int i, num_stats = 0;

	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	layout = ocelot_get_stats_layout(ocelot);

	for (i = 0; i < OCELOT_NUM_STATS; i++)
		if (layout[i].name[0] != '\0')
			num_stats++;

	return num_stats;
}
EXPORT_SYMBOL(ocelot_get_sset_count);

static void ocelot_port_ethtool_stats_cb(struct ocelot *ocelot, int port,
					 void *priv)
{
	const struct ocelot_stat_layout *layout;
	u64 *data = priv;
	int i;

	layout = ocelot_get_stats_layout(ocelot);

	/* Copy all supported counters */
	for (i = 0; i < OCELOT_NUM_STATS; i++) {
		int index = port * OCELOT_NUM_STATS + i;

		if (layout[i].name[0] == '\0')
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

static void ocelot_port_pmac_pause_stats_cb(struct ocelot *ocelot, int port,
					    void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_pause_stats *pause_stats = priv;

	pause_stats->tx_pause_frames = s[OCELOT_STAT_TX_PMAC_PAUSE];
	pause_stats->rx_pause_frames = s[OCELOT_STAT_RX_PMAC_PAUSE];
}

static void ocelot_port_mm_stats_cb(struct ocelot *ocelot, int port,
				    void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_mm_stats *stats = priv;

	stats->MACMergeFrameAssErrorCount = s[OCELOT_STAT_RX_ASSEMBLY_ERRS];
	stats->MACMergeFrameSmdErrorCount = s[OCELOT_STAT_RX_SMD_ERRS];
	stats->MACMergeFrameAssOkCount = s[OCELOT_STAT_RX_ASSEMBLY_OK];
	stats->MACMergeFragCountRx = s[OCELOT_STAT_RX_MERGE_FRAGMENTS];
	stats->MACMergeFragCountTx = s[OCELOT_STAT_TX_MERGE_FRAGMENTS];
	stats->MACMergeHoldCount = s[OCELOT_STAT_TX_MM_HOLD];
}

void ocelot_port_get_pause_stats(struct ocelot *ocelot, int port,
				 struct ethtool_pause_stats *pause_stats)
{
	struct net_device *dev;

	switch (pause_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		ocelot_port_stats_run(ocelot, port, pause_stats,
				      ocelot_port_pause_stats_cb);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		if (ocelot->mm_supported)
			ocelot_port_stats_run(ocelot, port, pause_stats,
					      ocelot_port_pmac_pause_stats_cb);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		dev = ocelot->ops->port_to_netdev(ocelot, port);
		ethtool_aggregate_pause_stats(dev, pause_stats);
		break;
	}
}
EXPORT_SYMBOL_GPL(ocelot_port_get_pause_stats);

void ocelot_port_get_mm_stats(struct ocelot *ocelot, int port,
			      struct ethtool_mm_stats *stats)
{
	if (!ocelot->mm_supported)
		return;

	ocelot_port_stats_run(ocelot, port, stats, ocelot_port_mm_stats_cb);
}
EXPORT_SYMBOL_GPL(ocelot_port_get_mm_stats);

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
	rmon_stats->hist_tx[3] = s[OCELOT_STAT_TX_128_255];
	rmon_stats->hist_tx[4] = s[OCELOT_STAT_TX_256_511];
	rmon_stats->hist_tx[5] = s[OCELOT_STAT_TX_512_1023];
	rmon_stats->hist_tx[6] = s[OCELOT_STAT_TX_1024_1526];
}

static void ocelot_port_pmac_rmon_stats_cb(struct ocelot *ocelot, int port,
					   void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_rmon_stats *rmon_stats = priv;

	rmon_stats->undersize_pkts = s[OCELOT_STAT_RX_PMAC_SHORTS];
	rmon_stats->oversize_pkts = s[OCELOT_STAT_RX_PMAC_LONGS];
	rmon_stats->fragments = s[OCELOT_STAT_RX_PMAC_FRAGMENTS];
	rmon_stats->jabbers = s[OCELOT_STAT_RX_PMAC_JABBERS];

	rmon_stats->hist[0] = s[OCELOT_STAT_RX_PMAC_64];
	rmon_stats->hist[1] = s[OCELOT_STAT_RX_PMAC_65_127];
	rmon_stats->hist[2] = s[OCELOT_STAT_RX_PMAC_128_255];
	rmon_stats->hist[3] = s[OCELOT_STAT_RX_PMAC_256_511];
	rmon_stats->hist[4] = s[OCELOT_STAT_RX_PMAC_512_1023];
	rmon_stats->hist[5] = s[OCELOT_STAT_RX_PMAC_1024_1526];
	rmon_stats->hist[6] = s[OCELOT_STAT_RX_PMAC_1527_MAX];

	rmon_stats->hist_tx[0] = s[OCELOT_STAT_TX_PMAC_64];
	rmon_stats->hist_tx[1] = s[OCELOT_STAT_TX_PMAC_65_127];
	rmon_stats->hist_tx[2] = s[OCELOT_STAT_TX_PMAC_128_255];
	rmon_stats->hist_tx[3] = s[OCELOT_STAT_TX_PMAC_128_255];
	rmon_stats->hist_tx[4] = s[OCELOT_STAT_TX_PMAC_256_511];
	rmon_stats->hist_tx[5] = s[OCELOT_STAT_TX_PMAC_512_1023];
	rmon_stats->hist_tx[6] = s[OCELOT_STAT_TX_PMAC_1024_1526];
}

void ocelot_port_get_rmon_stats(struct ocelot *ocelot, int port,
				struct ethtool_rmon_stats *rmon_stats,
				const struct ethtool_rmon_hist_range **ranges)
{
	struct net_device *dev;

	*ranges = ocelot_rmon_ranges;

	switch (rmon_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		ocelot_port_stats_run(ocelot, port, rmon_stats,
				      ocelot_port_rmon_stats_cb);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		if (ocelot->mm_supported)
			ocelot_port_stats_run(ocelot, port, rmon_stats,
					      ocelot_port_pmac_rmon_stats_cb);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		dev = ocelot->ops->port_to_netdev(ocelot, port);
		ethtool_aggregate_rmon_stats(dev, rmon_stats);
		break;
	}
}
EXPORT_SYMBOL_GPL(ocelot_port_get_rmon_stats);

static void ocelot_port_ctrl_stats_cb(struct ocelot *ocelot, int port, void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_eth_ctrl_stats *ctrl_stats = priv;

	ctrl_stats->MACControlFramesReceived = s[OCELOT_STAT_RX_CONTROL];
}

static void ocelot_port_pmac_ctrl_stats_cb(struct ocelot *ocelot, int port,
					   void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_eth_ctrl_stats *ctrl_stats = priv;

	ctrl_stats->MACControlFramesReceived = s[OCELOT_STAT_RX_PMAC_CONTROL];
}

void ocelot_port_get_eth_ctrl_stats(struct ocelot *ocelot, int port,
				    struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct net_device *dev;

	switch (ctrl_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		ocelot_port_stats_run(ocelot, port, ctrl_stats,
				      ocelot_port_ctrl_stats_cb);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		if (ocelot->mm_supported)
			ocelot_port_stats_run(ocelot, port, ctrl_stats,
					      ocelot_port_pmac_ctrl_stats_cb);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		dev = ocelot->ops->port_to_netdev(ocelot, port);
		ethtool_aggregate_ctrl_stats(dev, ctrl_stats);
		break;
	}
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

static void ocelot_port_pmac_mac_stats_cb(struct ocelot *ocelot, int port,
					  void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_eth_mac_stats *mac_stats = priv;

	mac_stats->OctetsTransmittedOK = s[OCELOT_STAT_TX_PMAC_OCTETS];
	mac_stats->FramesTransmittedOK = s[OCELOT_STAT_TX_PMAC_64] +
					 s[OCELOT_STAT_TX_PMAC_65_127] +
					 s[OCELOT_STAT_TX_PMAC_128_255] +
					 s[OCELOT_STAT_TX_PMAC_256_511] +
					 s[OCELOT_STAT_TX_PMAC_512_1023] +
					 s[OCELOT_STAT_TX_PMAC_1024_1526] +
					 s[OCELOT_STAT_TX_PMAC_1527_MAX];
	mac_stats->OctetsReceivedOK = s[OCELOT_STAT_RX_PMAC_OCTETS];
	mac_stats->FramesReceivedOK = s[OCELOT_STAT_RX_PMAC_64] +
				      s[OCELOT_STAT_RX_PMAC_65_127] +
				      s[OCELOT_STAT_RX_PMAC_128_255] +
				      s[OCELOT_STAT_RX_PMAC_256_511] +
				      s[OCELOT_STAT_RX_PMAC_512_1023] +
				      s[OCELOT_STAT_RX_PMAC_1024_1526] +
				      s[OCELOT_STAT_RX_PMAC_1527_MAX];
	mac_stats->MulticastFramesXmittedOK = s[OCELOT_STAT_TX_PMAC_MULTICAST];
	mac_stats->BroadcastFramesXmittedOK = s[OCELOT_STAT_TX_PMAC_BROADCAST];
	mac_stats->MulticastFramesReceivedOK = s[OCELOT_STAT_RX_PMAC_MULTICAST];
	mac_stats->BroadcastFramesReceivedOK = s[OCELOT_STAT_RX_PMAC_BROADCAST];
	mac_stats->FrameTooLongErrors = s[OCELOT_STAT_RX_PMAC_LONGS];
	/* Sadly, C_RX_CRC is the sum of FCS and alignment errors, they are not
	 * counted individually.
	 */
	mac_stats->FrameCheckSequenceErrors = s[OCELOT_STAT_RX_PMAC_CRC_ALIGN_ERRS];
	mac_stats->AlignmentErrors = s[OCELOT_STAT_RX_PMAC_CRC_ALIGN_ERRS];
}

void ocelot_port_get_eth_mac_stats(struct ocelot *ocelot, int port,
				   struct ethtool_eth_mac_stats *mac_stats)
{
	struct net_device *dev;

	switch (mac_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		ocelot_port_stats_run(ocelot, port, mac_stats,
				      ocelot_port_mac_stats_cb);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		if (ocelot->mm_supported)
			ocelot_port_stats_run(ocelot, port, mac_stats,
					      ocelot_port_pmac_mac_stats_cb);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		dev = ocelot->ops->port_to_netdev(ocelot, port);
		ethtool_aggregate_mac_stats(dev, mac_stats);
		break;
	}
}
EXPORT_SYMBOL_GPL(ocelot_port_get_eth_mac_stats);

static void ocelot_port_phy_stats_cb(struct ocelot *ocelot, int port, void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_eth_phy_stats *phy_stats = priv;

	phy_stats->SymbolErrorDuringCarrier = s[OCELOT_STAT_RX_SYM_ERRS];
}

static void ocelot_port_pmac_phy_stats_cb(struct ocelot *ocelot, int port,
					  void *priv)
{
	u64 *s = &ocelot->stats[port * OCELOT_NUM_STATS];
	struct ethtool_eth_phy_stats *phy_stats = priv;

	phy_stats->SymbolErrorDuringCarrier = s[OCELOT_STAT_RX_PMAC_SYM_ERRS];
}

void ocelot_port_get_eth_phy_stats(struct ocelot *ocelot, int port,
				   struct ethtool_eth_phy_stats *phy_stats)
{
	struct net_device *dev;

	switch (phy_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		ocelot_port_stats_run(ocelot, port, phy_stats,
				      ocelot_port_phy_stats_cb);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		if (ocelot->mm_supported)
			ocelot_port_stats_run(ocelot, port, phy_stats,
					      ocelot_port_pmac_phy_stats_cb);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		dev = ocelot->ops->port_to_netdev(ocelot, port);
		ethtool_aggregate_phy_stats(dev, phy_stats);
		break;
	}
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
	const struct ocelot_stat_layout *layout;
	unsigned int last = 0;
	int i;

	INIT_LIST_HEAD(&ocelot->stats_regions);

	layout = ocelot_get_stats_layout(ocelot);

	for (i = 0; i < OCELOT_NUM_STATS; i++) {
		if (!layout[i].reg)
			continue;

		if (region && ocelot->map[SYS][layout[i].reg & REG_MASK] ==
		    ocelot->map[SYS][last & REG_MASK] + 4) {
			region->count++;
		} else {
			region = devm_kzalloc(ocelot->dev, sizeof(*region),
					      GFP_KERNEL);
			if (!region)
				return -ENOMEM;

			/* enum ocelot_stat must be kept sorted in the same
			 * order as layout[i].reg in order to have efficient
			 * bulking
			 */
			WARN_ON(last >= layout[i].reg);

			region->base = layout[i].reg;
			region->first_stat = i;
			region->count = 1;
			list_add_tail(&region->node, &ocelot->stats_regions);
		}

		last = layout[i].reg;
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

