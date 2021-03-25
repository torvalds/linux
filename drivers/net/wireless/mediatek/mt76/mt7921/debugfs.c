// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7921.h"
#include "eeprom.h"

static int
mt7921_fw_debug_set(void *data, u64 val)
{
	struct mt7921_dev *dev = data;

	dev->fw_debug = (u8)val;

	mt7921_mcu_fw_log_2_host(dev, dev->fw_debug);

	return 0;
}

static int
mt7921_fw_debug_get(void *data, u64 *val)
{
	struct mt7921_dev *dev = data;

	*val = dev->fw_debug;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug, mt7921_fw_debug_get,
			 mt7921_fw_debug_set, "%lld\n");

static void
mt7921_ampdu_stat_read_phy(struct mt7921_phy *phy,
			   struct seq_file *file)
{
	struct mt7921_dev *dev = file->private;
	int bound[15], range[4], i;

	if (!phy)
		return;

	/* Tx ampdu stat */
	for (i = 0; i < ARRAY_SIZE(range); i++)
		range[i] = mt76_rr(dev, MT_MIB_ARNG(0, i));

	for (i = 0; i < ARRAY_SIZE(bound); i++)
		bound[i] = MT_MIB_ARNCR_RANGE(range[i / 4], i) + 1;

	seq_printf(file, "\nPhy0\n");

	seq_printf(file, "Length: %8d | ", bound[0]);
	for (i = 0; i < ARRAY_SIZE(bound) - 1; i++)
		seq_printf(file, "%3d -%3d | ",
			   bound[i] + 1, bound[i + 1]);

	seq_puts(file, "\nCount:  ");
	for (i = 0; i < ARRAY_SIZE(bound); i++)
		seq_printf(file, "%8d | ", dev->mt76.aggr_stats[i]);
	seq_puts(file, "\n");

	seq_printf(file, "BA miss count: %d\n", phy->mib.ba_miss_cnt);
}

static int
mt7921_tx_stats_read(struct seq_file *file, void *data)
{
	struct mt7921_dev *dev = file->private;
	int stat[8], i, n;

	mt7921_ampdu_stat_read_phy(&dev->phy, file);

	/* Tx amsdu info */
	seq_puts(file, "Tx MSDU stat:\n");
	for (i = 0, n = 0; i < ARRAY_SIZE(stat); i++) {
		stat[i] = mt76_rr(dev,  MT_PLE_AMSDU_PACK_MSDU_CNT(i));
		n += stat[i];
	}

	for (i = 0; i < ARRAY_SIZE(stat); i++) {
		seq_printf(file, "AMSDU pack count of %d MSDU in TXD: 0x%x ",
			   i + 1, stat[i]);
		if (n != 0)
			seq_printf(file, "(%d%%)\n", stat[i] * 100 / n);
		else
			seq_puts(file, "\n");
	}

	return 0;
}

static int
mt7921_tx_stats_open(struct inode *inode, struct file *f)
{
	return single_open(f, mt7921_tx_stats_read, inode->i_private);
}

static const struct file_operations fops_tx_stats = {
	.open = mt7921_tx_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int
mt7921_queues_acq(struct seq_file *s, void *data)
{
	struct mt7921_dev *dev = dev_get_drvdata(s->private);
	int i;

	for (i = 0; i < 16; i++) {
		int j, acs = i / 4, index = i % 4;
		u32 ctrl, val, qlen = 0;

		val = mt76_rr(dev, MT_PLE_AC_QEMPTY(acs, index));
		ctrl = BIT(31) | BIT(15) | (acs << 8);

		for (j = 0; j < 32; j++) {
			if (val & BIT(j))
				continue;

			mt76_wr(dev, MT_PLE_FL_Q0_CTRL,
				ctrl | (j + (index << 5)));
			qlen += mt76_get_field(dev, MT_PLE_FL_Q3_CTRL,
					       GENMASK(11, 0));
		}
		seq_printf(s, "AC%d%d: queued=%d\n", acs, index, qlen);
	}

	return 0;
}

static int
mt7921_queues_read(struct seq_file *s, void *data)
{
	struct mt7921_dev *dev = dev_get_drvdata(s->private);
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

static int
mt7921_pm_set(void *data, u64 val)
{
	struct mt7921_dev *dev = data;
	struct mt76_phy *mphy = dev->phy.mt76;
	int ret = 0;

	mt7921_mutex_acquire(dev);

	dev->pm.enable = val;

	ieee80211_iterate_active_interfaces(mphy->hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7921_pm_interface_iter, mphy->priv);
	mt7921_mutex_release(dev);

	return ret;
}

static int
mt7921_pm_get(void *data, u64 *val)
{
	struct mt7921_dev *dev = data;

	*val = dev->pm.enable;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_pm, mt7921_pm_get, mt7921_pm_set, "%lld\n");

static int
mt7921_pm_idle_timeout_set(void *data, u64 val)
{
	struct mt7921_dev *dev = data;

	dev->pm.idle_timeout = msecs_to_jiffies(val);

	return 0;
}

static int
mt7921_pm_idle_timeout_get(void *data, u64 *val)
{
	struct mt7921_dev *dev = data;

	*val = jiffies_to_msecs(dev->pm.idle_timeout);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_pm_idle_timeout, mt7921_pm_idle_timeout_get,
			 mt7921_pm_idle_timeout_set, "%lld\n");

static int mt7921_config(void *data, u64 val)
{
	struct mt7921_dev *dev = data;
	int ret;

	mt7921_mutex_acquire(dev);
	ret = mt76_connac_mcu_chip_config(&dev->mt76);
	mt7921_mutex_release(dev);

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_config, NULL, mt7921_config, "%lld\n");

int mt7921_init_debugfs(struct mt7921_dev *dev)
{
	struct dentry *dir;

	dir = mt76_register_debugfs(&dev->mt76);
	if (!dir)
		return -ENOMEM;

	debugfs_create_devm_seqfile(dev->mt76.dev, "queues", dir,
				    mt7921_queues_read);
	debugfs_create_devm_seqfile(dev->mt76.dev, "acq", dir,
				    mt7921_queues_acq);
	debugfs_create_file("tx_stats", 0400, dir, dev, &fops_tx_stats);
	debugfs_create_file("fw_debug", 0600, dir, dev, &fops_fw_debug);
	debugfs_create_file("runtime-pm", 0600, dir, dev, &fops_pm);
	debugfs_create_file("idle-timeout", 0600, dir, dev,
			    &fops_pm_idle_timeout);
	debugfs_create_file("chip_config", 0600, dir, dev, &fops_config);

	return 0;
}
