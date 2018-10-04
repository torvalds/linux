/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/debugfs.h>

#include "mt76x0.h"
#include "eeprom.h"

static int
mt76x0_ampdu_stat_read(struct seq_file *file, void *data)
{
	struct mt76x02_dev *dev = file->private;
	int i, j;

#define stat_printf(grp, off, name)					\
	seq_printf(file, #name ":\t%llu\n", dev->stats.grp[off])

	stat_printf(rx_stat, 0, rx_crc_err);
	stat_printf(rx_stat, 1, rx_phy_err);
	stat_printf(rx_stat, 2, rx_false_cca);
	stat_printf(rx_stat, 3, rx_plcp_err);
	stat_printf(rx_stat, 4, rx_fifo_overflow);
	stat_printf(rx_stat, 5, rx_duplicate);

	stat_printf(tx_stat, 0, tx_fail_cnt);
	stat_printf(tx_stat, 1, tx_bcn_cnt);
	stat_printf(tx_stat, 2, tx_success);
	stat_printf(tx_stat, 3, tx_retransmit);
	stat_printf(tx_stat, 4, tx_zero_len);
	stat_printf(tx_stat, 5, tx_underflow);

	stat_printf(aggr_stat, 0, non_aggr_tx);
	stat_printf(aggr_stat, 1, aggr_tx);

	stat_printf(zero_len_del, 0, tx_zero_len_del);
	stat_printf(zero_len_del, 1, rx_zero_len_del);
#undef stat_printf

	seq_puts(file, "Aggregations stats:\n");
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 8; j++)
			seq_printf(file, "%08llx ",
				   dev->stats.aggr_n[i * 8 + j]);
		seq_putc(file, '\n');
	}

	seq_printf(file, "recent average AMPDU len: %d\n",
		   atomic_read(&dev->avg_ampdu_len));

	return 0;
}

static int
mt76x0_ampdu_stat_open(struct inode *inode, struct file *f)
{
	return single_open(f, mt76x0_ampdu_stat_read, inode->i_private);
}

static const struct file_operations fops_ampdu_stat = {
	.open = mt76x0_ampdu_stat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void mt76x0_init_debugfs(struct mt76x02_dev *dev)
{
	struct dentry *dir;

	dir = mt76_register_debugfs(&dev->mt76);
	if (!dir)
		return;

	debugfs_create_file("ampdu_stat", S_IRUSR, dir, dev, &fops_ampdu_stat);
}
