// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */
#include "mt76.h"

static int
mt76_reg_set(void *data, u64 val)
{
	struct mt76_dev *dev = data;

	dev->bus->wr(dev, dev->debugfs_reg, val);
	return 0;
}

static int
mt76_reg_get(void *data, u64 *val)
{
	struct mt76_dev *dev = data;

	*val = dev->bus->rr(dev, dev->debugfs_reg);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_regval, mt76_reg_get, mt76_reg_set,
			 "0x%08llx\n");

static int
mt76_queues_read(struct seq_file *s, void *data)
{
	struct mt76_dev *dev = dev_get_drvdata(s->private);
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->q_tx); i++) {
		struct mt76_sw_queue *q = &dev->q_tx[i];

		if (!q->q)
			continue;

		seq_printf(s,
			   "%d:	queued=%d head=%d tail=%d swq_queued=%d\n",
			   i, q->q->queued, q->q->head, q->q->tail,
			   q->swq_queued);
	}

	return 0;
}

void mt76_seq_puts_array(struct seq_file *file, const char *str,
			 s8 *val, int len)
{
	int i;

	seq_printf(file, "%10s:", str);
	for (i = 0; i < len; i++)
		seq_printf(file, " %2d", val[i]);
	seq_puts(file, "\n");
}
EXPORT_SYMBOL_GPL(mt76_seq_puts_array);

static int mt76_read_rate_txpower(struct seq_file *s, void *data)
{
	struct mt76_dev *dev = dev_get_drvdata(s->private);

	mt76_seq_puts_array(s, "CCK", dev->rate_power.cck,
			    ARRAY_SIZE(dev->rate_power.cck));
	mt76_seq_puts_array(s, "OFDM", dev->rate_power.ofdm,
			    ARRAY_SIZE(dev->rate_power.ofdm));
	mt76_seq_puts_array(s, "STBC", dev->rate_power.stbc,
			    ARRAY_SIZE(dev->rate_power.stbc));
	mt76_seq_puts_array(s, "HT", dev->rate_power.ht,
			    ARRAY_SIZE(dev->rate_power.ht));
	mt76_seq_puts_array(s, "VHT", dev->rate_power.vht,
			    ARRAY_SIZE(dev->rate_power.vht));
	return 0;
}

struct dentry *mt76_register_debugfs(struct mt76_dev *dev)
{
	struct dentry *dir;

	dir = debugfs_create_dir("mt76", dev->hw->wiphy->debugfsdir);
	if (!dir)
		return NULL;

	debugfs_create_u8("led_pin", 0600, dir, &dev->led_pin);
	debugfs_create_u32("regidx", 0600, dir, &dev->debugfs_reg);
	debugfs_create_file_unsafe("regval", 0600, dir, dev,
				   &fops_regval);
	debugfs_create_blob("eeprom", 0400, dir, &dev->eeprom);
	if (dev->otp.data)
		debugfs_create_blob("otp", 0400, dir, &dev->otp);
	debugfs_create_devm_seqfile(dev->dev, "queues", dir, mt76_queues_read);
	debugfs_create_devm_seqfile(dev->dev, "rate_txpower", dir,
				    mt76_read_rate_txpower);

	return dir;
}
EXPORT_SYMBOL_GPL(mt76_register_debugfs);
