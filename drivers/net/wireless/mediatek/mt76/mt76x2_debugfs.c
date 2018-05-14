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
#include "mt76x2.h"

static int
mt76x2_ampdu_stat_read(struct seq_file *file, void *data)
{
	struct mt76x2_dev *dev = file->private;
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
mt76x2_ampdu_stat_open(struct inode *inode, struct file *f)
{
	return single_open(f, mt76x2_ampdu_stat_read, inode->i_private);
}

static void
seq_puts_array(struct seq_file *file, const char *str, s8 *val, int len)
{
	int i;

	seq_printf(file, "%10s:", str);
	for (i = 0; i < len; i++)
		seq_printf(file, " %2d", val[i]);
	seq_puts(file, "\n");
}

static int read_txpower(struct seq_file *file, void *data)
{
	struct mt76x2_dev *dev = dev_get_drvdata(file->private);

	seq_printf(file, "Target power: %d\n", dev->target_power);

	seq_puts_array(file, "Delta", dev->target_power_delta,
		       ARRAY_SIZE(dev->target_power_delta));
	seq_puts_array(file, "CCK", dev->rate_power.cck,
		       ARRAY_SIZE(dev->rate_power.cck));
	seq_puts_array(file, "OFDM", dev->rate_power.ofdm,
		       ARRAY_SIZE(dev->rate_power.ofdm));
	seq_puts_array(file, "HT", dev->rate_power.ht,
		       ARRAY_SIZE(dev->rate_power.ht));
	seq_puts_array(file, "VHT", dev->rate_power.vht,
		       ARRAY_SIZE(dev->rate_power.vht));
	return 0;
}

static const struct file_operations fops_ampdu_stat = {
	.open = mt76x2_ampdu_stat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int
mt76x2_dfs_stat_read(struct seq_file *file, void *data)
{
	int i;
	struct mt76x2_dev *dev = file->private;
	struct mt76x2_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;

	for (i = 0; i < MT_DFS_NUM_ENGINES; i++) {
		seq_printf(file, "engine: %d\n", i);
		seq_printf(file, "  hw pattern detected:\t%d\n",
			   dfs_pd->stats[i].hw_pattern);
		seq_printf(file, "  hw pulse discarded:\t%d\n",
			   dfs_pd->stats[i].hw_pulse_discarded);
	}

	return 0;
}

static int
mt76x2_dfs_stat_open(struct inode *inode, struct file *f)
{
	return single_open(f, mt76x2_dfs_stat_read, inode->i_private);
}

static const struct file_operations fops_dfs_stat = {
	.open = mt76x2_dfs_stat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void mt76x2_init_debugfs(struct mt76x2_dev *dev)
{
	struct dentry *dir;

	dir = mt76_register_debugfs(&dev->mt76);
	if (!dir)
		return;

	debugfs_create_u8("temperature", S_IRUSR, dir, &dev->cal.temp);
	debugfs_create_bool("tpc", S_IRUSR | S_IWUSR, dir, &dev->enable_tpc);

	debugfs_create_file("ampdu_stat", S_IRUSR, dir, dev, &fops_ampdu_stat);
	debugfs_create_file("dfs_stats", S_IRUSR, dir, dev, &fops_dfs_stat);
	debugfs_create_devm_seqfile(dev->mt76.dev, "txpower", dir,
				    read_txpower);
}
