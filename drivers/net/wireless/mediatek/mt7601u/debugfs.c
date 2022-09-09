// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 */

#include <linux/debugfs.h>

#include "mt7601u.h"
#include "eeprom.h"

static int
mt76_reg_set(void *data, u64 val)
{
	struct mt7601u_dev *dev = data;

	mt76_wr(dev, dev->debugfs_reg, val);
	return 0;
}

static int
mt76_reg_get(void *data, u64 *val)
{
	struct mt7601u_dev *dev = data;

	*val = mt76_rr(dev, dev->debugfs_reg);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_regval, mt76_reg_get, mt76_reg_set, "0x%08llx\n");

static int
mt7601u_ampdu_stat_show(struct seq_file *file, void *data)
{
	struct mt7601u_dev *dev = file->private;
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

DEFINE_SHOW_ATTRIBUTE(mt7601u_ampdu_stat);

static int
mt7601u_eeprom_param_show(struct seq_file *file, void *data)
{
	struct mt7601u_dev *dev = file->private;
	struct mt7601u_rate_power *rp = &dev->ee->power_rate_table;
	struct tssi_data *td = &dev->ee->tssi_data;
	int i;

	seq_printf(file, "RF freq offset: %hhx\n", dev->ee->rf_freq_off);
	seq_printf(file, "RSSI offset: %hhx %hhx\n",
		   dev->ee->rssi_offset[0], dev->ee->rssi_offset[1]);
	seq_printf(file, "Reference temp: %hhx\n", dev->ee->ref_temp);
	seq_printf(file, "LNA gain: %hhx\n", dev->ee->lna_gain);
	seq_printf(file, "Reg channels: %hhu-%d\n", dev->ee->reg.start,
		   dev->ee->reg.start + dev->ee->reg.num - 1);

	seq_puts(file, "Per rate power:\n");
	for (i = 0; i < 2; i++)
		seq_printf(file, "\t raw:%02hhx bw20:%02hhx bw40:%02hhx\n",
			   rp->cck[i].raw, rp->cck[i].bw20, rp->cck[i].bw40);
	for (i = 0; i < 4; i++)
		seq_printf(file, "\t raw:%02hhx bw20:%02hhx bw40:%02hhx\n",
			   rp->ofdm[i].raw, rp->ofdm[i].bw20, rp->ofdm[i].bw40);
	for (i = 0; i < 4; i++)
		seq_printf(file, "\t raw:%02hhx bw20:%02hhx bw40:%02hhx\n",
			   rp->ht[i].raw, rp->ht[i].bw20, rp->ht[i].bw40);

	seq_puts(file, "Per channel power:\n");
	for (i = 0; i < 7; i++)
		seq_printf(file, "\t tx_power  ch%u:%02hhx ch%u:%02hhx\n",
			   i * 2 + 1, dev->ee->chan_pwr[i * 2],
			   i * 2 + 2, dev->ee->chan_pwr[i * 2 + 1]);

	if (!dev->ee->tssi_enabled)
		return 0;

	seq_puts(file, "TSSI:\n");
	seq_printf(file, "\t slope:%02hhx\n", td->slope);
	seq_printf(file, "\t offset=%02hhx %02hhx %02hhx\n",
		   td->offset[0], td->offset[1], td->offset[2]);
	seq_printf(file, "\t delta_off:%08x\n", td->tx0_delta_offset);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7601u_eeprom_param);

void mt7601u_init_debugfs(struct mt7601u_dev *dev)
{
	struct dentry *dir;

	dir = debugfs_create_dir("mt7601u", dev->hw->wiphy->debugfsdir);
	if (!dir)
		return;

	debugfs_create_u8("temperature", 0400, dir, &dev->raw_temp);
	debugfs_create_u32("temp_mode", 0400, dir, &dev->temp_mode);

	debugfs_create_u32("regidx", 0600, dir, &dev->debugfs_reg);
	debugfs_create_file("regval", 0600, dir, dev, &fops_regval);
	debugfs_create_file("ampdu_stat", 0400, dir, dev, &mt7601u_ampdu_stat_fops);
	debugfs_create_file("eeprom_param", 0400, dir, dev, &mt7601u_eeprom_param_fops);
}
