// SPDX-License-Identifier: GPL-2.0
/* Texas Instruments ICSSG Ethernet driver
 *
 * Copyright (C) 2018-2021 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#include "icssg_prueth.h"
#include "icssg_stats.h"
#include <linux/regmap.h>

#define ICSSG_TX_PACKET_OFFSET	0xA0
#define ICSSG_TX_BYTE_OFFSET	0xEC
#define ICSSG_FW_STATS_BASE	0x0248

static u32 stats_base[] = {	0x54c,	/* Slice 0 stats start */
				0xb18,	/* Slice 1 stats start */
};

void emac_update_hardware_stats(struct prueth_emac *emac)
{
	struct prueth *prueth = emac->prueth;
	int slice = prueth_emac_slice(emac);
	u32 base = stats_base[slice];
	u32 tx_pkt_cnt = 0;
	u32 val, reg;
	int i;

	for (i = 0; i < ARRAY_SIZE(icssg_all_miig_stats); i++) {
		regmap_read(prueth->miig_rt,
			    base + icssg_all_miig_stats[i].offset,
			    &val);
		regmap_write(prueth->miig_rt,
			     base + icssg_all_miig_stats[i].offset,
			     val);

		if (icssg_all_miig_stats[i].offset == ICSSG_TX_PACKET_OFFSET)
			tx_pkt_cnt = val;

		emac->stats[i] += val;
		if (icssg_all_miig_stats[i].offset == ICSSG_TX_BYTE_OFFSET)
			emac->stats[i] -= tx_pkt_cnt * 8;
	}

	if (prueth->pa_stats) {
		for (i = 0; i < ARRAY_SIZE(icssg_all_pa_stats); i++) {
			reg = ICSSG_FW_STATS_BASE +
			      icssg_all_pa_stats[i].offset *
			      PRUETH_NUM_MACS + slice * sizeof(u32);
			regmap_read(prueth->pa_stats, reg, &val);
			emac->pa_stats[i] += val;
		}
	}
}

void icssg_stats_work_handler(struct work_struct *work)
{
	struct prueth_emac *emac = container_of(work, struct prueth_emac,
						stats_work.work);
	emac_update_hardware_stats(emac);

	queue_delayed_work(system_long_wq, &emac->stats_work,
			   msecs_to_jiffies((STATS_TIME_LIMIT_1G_MS * 1000) / emac->speed));
}
EXPORT_SYMBOL_GPL(icssg_stats_work_handler);

int emac_get_stat_by_name(struct prueth_emac *emac, char *stat_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(icssg_all_miig_stats); i++) {
		if (!strcmp(icssg_all_miig_stats[i].name, stat_name))
			return emac->stats[icssg_all_miig_stats[i].offset / sizeof(u32)];
	}

	if (emac->prueth->pa_stats) {
		for (i = 0; i < ARRAY_SIZE(icssg_all_pa_stats); i++) {
			if (!strcmp(icssg_all_pa_stats[i].name, stat_name))
				return emac->pa_stats[icssg_all_pa_stats[i].offset / sizeof(u32)];
		}
	}

	netdev_err(emac->ndev, "Invalid stats %s\n", stat_name);
	return -EINVAL;
}
