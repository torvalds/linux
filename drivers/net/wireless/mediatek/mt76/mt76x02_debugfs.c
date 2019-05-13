/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/debugfs.h>
#include "mt76x02.h"

static int
mt76x02_ampdu_stat_read(struct seq_file *file, void *data)
{
	struct mt76x02_dev *dev = file->private;
	int i, j;

	for (i = 0; i < 4; i++) {
		seq_puts(file, "Length: ");
		for (j = 0; j < 8; j++)
			seq_printf(file, "%8d | ", i * 8 + j + 1);
		seq_puts(file, "\n");
		seq_puts(file, "Count:  ");
		for (j = 0; j < 8; j++)
			seq_printf(file, "%8d | ", dev->aggr_stats[i * 8 + j]);
		seq_puts(file, "\n");
		seq_puts(file, "--------");
		for (j = 0; j < 8; j++)
			seq_puts(file, "-----------");
		seq_puts(file, "\n");
	}

	return 0;
}

static int
mt76x02_ampdu_stat_open(struct inode *inode, struct file *f)
{
	return single_open(f, mt76x02_ampdu_stat_read, inode->i_private);
}

static int read_txpower(struct seq_file *file, void *data)
{
	struct mt76x02_dev *dev = dev_get_drvdata(file->private);

	seq_printf(file, "Target power: %d\n", dev->target_power);

	mt76_seq_puts_array(file, "Delta", dev->target_power_delta,
			    ARRAY_SIZE(dev->target_power_delta));
	return 0;
}

static const struct file_operations fops_ampdu_stat = {
	.open = mt76x02_ampdu_stat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int
mt76x02_dfs_stat_read(struct seq_file *file, void *data)
{
	struct mt76x02_dev *dev = file->private;
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
	int i;

	seq_printf(file, "allocated sequences:\t%d\n",
		   dfs_pd->seq_stats.seq_pool_len);
	seq_printf(file, "used sequences:\t\t%d\n",
		   dfs_pd->seq_stats.seq_len);
	seq_puts(file, "\n");

	for (i = 0; i < MT_DFS_NUM_ENGINES; i++) {
		seq_printf(file, "engine: %d\n", i);
		seq_printf(file, "  hw pattern detected:\t%d\n",
			   dfs_pd->stats[i].hw_pattern);
		seq_printf(file, "  hw pulse discarded:\t%d\n",
			   dfs_pd->stats[i].hw_pulse_discarded);
		seq_printf(file, "  sw pattern detected:\t%d\n",
			   dfs_pd->stats[i].sw_pattern);
	}

	return 0;
}

static int
mt76x02_dfs_stat_open(struct inode *inode, struct file *f)
{
	return single_open(f, mt76x02_dfs_stat_read, inode->i_private);
}

static const struct file_operations fops_dfs_stat = {
	.open = mt76x02_dfs_stat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int read_agc(struct seq_file *file, void *data)
{
	struct mt76x02_dev *dev = dev_get_drvdata(file->private);

	seq_printf(file, "avg_rssi: %d\n", dev->cal.avg_rssi_all);
	seq_printf(file, "low_gain: %d\n", dev->cal.low_gain);
	seq_printf(file, "false_cca: %d\n", dev->cal.false_cca);
	seq_printf(file, "agc_gain_adjust: %d\n", dev->cal.agc_gain_adjust);

	return 0;
}

static int
mt76_edcca_set(void *data, u64 val)
{
	struct mt76x02_dev *dev = data;
	enum nl80211_dfs_regions region = dev->dfs_pd.region;

	mutex_lock(&dev->mt76.mutex);

	dev->ed_monitor_enabled = !!val;
	dev->ed_monitor = dev->ed_monitor_enabled &&
			  region == NL80211_DFS_ETSI;
	mt76x02_edcca_init(dev);

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static int
mt76_edcca_get(void *data, u64 *val)
{
	struct mt76x02_dev *dev = data;

	*val = dev->ed_monitor_enabled;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_edcca, mt76_edcca_get, mt76_edcca_set,
			 "%lld\n");

void mt76x02_init_debugfs(struct mt76x02_dev *dev)
{
	struct dentry *dir;

	dir = mt76_register_debugfs(&dev->mt76);
	if (!dir)
		return;

	debugfs_create_u8("temperature", 0400, dir, &dev->cal.temp);
	debugfs_create_bool("tpc", 0600, dir, &dev->enable_tpc);

	debugfs_create_file("edcca", 0600, dir, dev, &fops_edcca);
	debugfs_create_file("ampdu_stat", 0400, dir, dev, &fops_ampdu_stat);
	debugfs_create_file("dfs_stats", 0400, dir, dev, &fops_dfs_stat);
	debugfs_create_devm_seqfile(dev->mt76.dev, "txpower", dir,
				    read_txpower);

	debugfs_create_devm_seqfile(dev->mt76.dev, "agc", dir, read_agc);

	debugfs_create_u32("tx_hang_reset", 0400, dir, &dev->tx_hang_reset);
}
EXPORT_SYMBOL_GPL(mt76x02_init_debugfs);
