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
mt76_reg_set(void *data, u64 val)
{
	struct mt76x0_dev *dev = data;

	mt76_wr(dev, dev->debugfs_reg, val);
	return 0;
}

static int
mt76_reg_get(void *data, u64 *val)
{
	struct mt76x0_dev *dev = data;

	*val = mt76_rr(dev, dev->debugfs_reg);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_regval, mt76_reg_get, mt76_reg_set, "0x%08llx\n");

static int
mt76x0_ampdu_stat_read(struct seq_file *file, void *data)
{
	struct mt76x0_dev *dev = file->private;
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

static int
mt76x0_eeprom_param_read(struct seq_file *file, void *data)
{
	struct mt76x0_dev *dev = file->private;
	u16 val;
	int i;

	seq_printf(file, "RF freq offset: %hhx\n", dev->ee->rf_freq_off);
	seq_printf(file, "RSSI offset 2GHz: %hhx %hhx\n",
		   dev->ee->rssi_offset_2ghz[0], dev->ee->rssi_offset_2ghz[1]);
	seq_printf(file, "RSSI offset 5GHz: %hhx %hhx %hhx\n",
		   dev->ee->rssi_offset_5ghz[0], dev->ee->rssi_offset_5ghz[1],
		   dev->ee->rssi_offset_5ghz[2]);
	seq_printf(file, "Temperature offset: %hhx\n", dev->ee->temp_off);
	seq_printf(file, "LNA gain: %x\n", dev->caldata.lna_gain);

	val = mt76x02_eeprom_get(&dev->mt76, MT_EE_NIC_CONF_0);
	seq_printf(file, "Power Amplifier type %lx\n",
		   val & MT_EE_NIC_CONF_0_PA_TYPE);

	seq_puts(file, "Per channel power:\n");
	for (i = 0; i < 58; i++)
		seq_printf(file, "\t%d chan:%d pwr:%d\n", i, i,
			   dev->ee->tx_pwr_per_chan[i]);

	seq_puts(file, "Per rate power 2GHz:\n");
	for (i = 0; i < 5; i++)
		seq_printf(file, "\t %d bw20:%d bw40:%d\n",
			   i, dev->ee->tx_pwr_cfg_2g[i][0],
			      dev->ee->tx_pwr_cfg_5g[i][1]);

	seq_puts(file, "Per rate power 5GHz:\n");
	for (i = 0; i < 5; i++)
		seq_printf(file, "\t %d bw20:%d bw40:%d\n",
			   i, dev->ee->tx_pwr_cfg_5g[i][0],
			      dev->ee->tx_pwr_cfg_5g[i][1]);

	return 0;
}

static int
mt76x0_eeprom_param_open(struct inode *inode, struct file *f)
{
	return single_open(f, mt76x0_eeprom_param_read, inode->i_private);
}

static const struct file_operations fops_eeprom_param = {
	.open = mt76x0_eeprom_param_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void mt76x0_init_debugfs(struct mt76x0_dev *dev)
{
	struct dentry *dir;

	dir = debugfs_create_dir("mt76x0", dev->mt76.hw->wiphy->debugfsdir);
	if (!dir)
		return;

	debugfs_create_u32("regidx", S_IRUSR | S_IWUSR, dir, &dev->debugfs_reg);
	debugfs_create_file("regval", S_IRUSR | S_IWUSR, dir, dev,
			    &fops_regval);
	debugfs_create_file("ampdu_stat", S_IRUSR, dir, dev, &fops_ampdu_stat);
	debugfs_create_file("eeprom_param", S_IRUSR, dir, dev,
			    &fops_eeprom_param);
}
