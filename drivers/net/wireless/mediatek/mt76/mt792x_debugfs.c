// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc. */

#include "mt792x.h"

static void
mt792x_ampdu_stat_read_phy(struct mt792x_phy *phy,
			   struct seq_file *file)
{
	struct mt792x_dev *dev = file->private;
	int bound[15], range[4], i;

	if (!phy)
		return;

	mt792x_mac_update_mib_stats(phy);

	/* Tx ampdu stat */
	for (i = 0; i < ARRAY_SIZE(range); i++)
		range[i] = mt76_rr(dev, MT_MIB_ARNG(0, i));

	for (i = 0; i < ARRAY_SIZE(bound); i++)
		bound[i] = MT_MIB_ARNCR_RANGE(range[i / 4], i % 4) + 1;

	seq_puts(file, "\nPhy0\n");

	seq_printf(file, "Length: %8d | ", bound[0]);
	for (i = 0; i < ARRAY_SIZE(bound) - 1; i++)
		seq_printf(file, "%3d  %3d | ", bound[i] + 1, bound[i + 1]);

	seq_puts(file, "\nCount:  ");
	for (i = 0; i < ARRAY_SIZE(bound); i++)
		seq_printf(file, "%8d | ", phy->mt76->aggr_stats[i]);
	seq_puts(file, "\n");

	seq_printf(file, "BA miss count: %d\n", phy->mib.ba_miss_cnt);
}

int mt792x_tx_stats_show(struct seq_file *file, void *data)
{
	struct mt792x_dev *dev = file->private;
	struct mt792x_phy *phy = &dev->phy;
	struct mt76_mib_stats *mib = &phy->mib;
	int i;

	mt792x_mutex_acquire(dev);

	mt792x_ampdu_stat_read_phy(phy, file);

	seq_puts(file, "Tx MSDU stat:\n");
	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++) {
		seq_printf(file, "AMSDU pack count of %d MSDU in TXD: %8d ",
			   i + 1, mib->tx_amsdu[i]);
		if (mib->tx_amsdu_cnt)
			seq_printf(file, "(%3d%%)\n",
				   mib->tx_amsdu[i] * 100 / mib->tx_amsdu_cnt);
		else
			seq_puts(file, "\n");
	}

	mt792x_mutex_release(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_tx_stats_show);

int mt792x_queues_acq(struct seq_file *s, void *data)
{
	struct mt792x_dev *dev = dev_get_drvdata(s->private);
	int i;

	mt792x_mutex_acquire(dev);

	for (i = 0; i < 4; i++) {
		u32 ctrl, val, qlen = 0;
		int j;

		val = mt76_rr(dev, MT_PLE_AC_QEMPTY(i));
		ctrl = BIT(31) | BIT(11) | (i << 24);

		for (j = 0; j < 32; j++) {
			if (val & BIT(j))
				continue;

			mt76_wr(dev, MT_PLE_FL_Q0_CTRL, ctrl | j);
			qlen += mt76_get_field(dev, MT_PLE_FL_Q3_CTRL,
					       GENMASK(11, 0));
		}
		seq_printf(s, "AC%d: queued=%d\n", i, qlen);
	}

	mt792x_mutex_release(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_queues_acq);

int mt792x_queues_read(struct seq_file *s, void *data)
{
	struct mt792x_dev *dev = dev_get_drvdata(s->private);
	struct {
		struct mt76_queue *q;
		char *queue;
	} queue_map[] = {
		{ dev->mphy.q_tx[MT_TXQ_BE],	 "WFDMA0" },
		{ dev->mt76.q_mcu[MT_MCUQ_WM],	 "MCUWM"  },
		{ dev->mt76.q_mcu[MT_MCUQ_FWDL], "MCUFWQ" },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(queue_map); i++) {
		struct mt76_queue *q = queue_map[i].q;

		if (!q)
			continue;

		seq_printf(s,
			   "%s:	queued=%d head=%d tail=%d\n",
			   queue_map[i].queue, q->queued, q->head,
			   q->tail);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_queues_read);

int mt792x_pm_stats(struct seq_file *s, void *data)
{
	struct mt792x_dev *dev = dev_get_drvdata(s->private);
	struct mt76_connac_pm *pm = &dev->pm;

	unsigned long awake_time = pm->stats.awake_time;
	unsigned long doze_time = pm->stats.doze_time;

	if (!test_bit(MT76_STATE_PM, &dev->mphy.state))
		awake_time += jiffies - pm->stats.last_wake_event;
	else
		doze_time += jiffies - pm->stats.last_doze_event;

	seq_printf(s, "awake time: %14u\ndoze time: %15u\n",
		   jiffies_to_msecs(awake_time),
		   jiffies_to_msecs(doze_time));

	seq_printf(s, "low power wakes: %9d\n", pm->stats.lp_wake);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_pm_stats);

int mt792x_pm_idle_timeout_set(void *data, u64 val)
{
	struct mt792x_dev *dev = data;

	dev->pm.idle_timeout = msecs_to_jiffies(val);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_pm_idle_timeout_set);

int mt792x_pm_idle_timeout_get(void *data, u64 *val)
{
	struct mt792x_dev *dev = data;

	*val = jiffies_to_msecs(dev->pm.idle_timeout);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_pm_idle_timeout_get);
