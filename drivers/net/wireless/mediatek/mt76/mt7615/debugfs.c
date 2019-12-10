// SPDX-License-Identifier: ISC

#include "mt7615.h"

static int
mt7615_radar_pattern_set(void *data, u64 val)
{
	struct mt7615_dev *dev = data;

	return mt7615_mcu_rdd_send_pattern(dev);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_radar_pattern, NULL,
			 mt7615_radar_pattern_set, "%lld\n");

static int
mt7615_scs_set(void *data, u64 val)
{
	struct mt7615_dev *dev = data;

	mt7615_mac_set_scs(dev, val);

	return 0;
}

static int
mt7615_scs_get(void *data, u64 *val)
{
	struct mt7615_dev *dev = data;

	*val = dev->scs_en;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_scs, mt7615_scs_get,
			 mt7615_scs_set, "%lld\n");

static int
mt7615_ampdu_stat_read(struct seq_file *file, void *data)
{
	struct mt7615_dev *dev = file->private;
	int bound[7], i, range;

	range = mt76_rr(dev, MT_AGG_ASRCR0);
	for (i = 0; i < 4; i++)
		bound[i] = MT_AGG_ASRCR_RANGE(range, i) + 1;
	range = mt76_rr(dev, MT_AGG_ASRCR1);
	for (i = 0; i < 3; i++)
		bound[i + 4] = MT_AGG_ASRCR_RANGE(range, i) + 1;

	seq_printf(file, "Length: %8d | ", bound[0]);
	for (i = 0; i < ARRAY_SIZE(bound) - 1; i++)
		seq_printf(file, "%3d -%3d | ",
			   bound[i], bound[i + 1]);
	seq_puts(file, "\nCount:  ");
	for (i = 0; i < ARRAY_SIZE(bound); i++)
		seq_printf(file, "%8d | ", dev->mt76.aggr_stats[i]);
	seq_puts(file, "\n");

	return 0;
}

static int
mt7615_ampdu_stat_open(struct inode *inode, struct file *f)
{
	return single_open(f, mt7615_ampdu_stat_read, inode->i_private);
}

static const struct file_operations fops_ampdu_stat = {
	.open = mt7615_ampdu_stat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int
mt7615_radio_read(struct seq_file *s, void *data)
{
	struct mt7615_dev *dev = dev_get_drvdata(s->private);

	seq_printf(s, "Sensitivity: ofdm=%d cck=%d\n",
		   dev->ofdm_sensitivity, dev->cck_sensitivity);
	seq_printf(s, "False CCA: ofdm=%d cck=%d\n",
		   dev->false_cca_ofdm, dev->false_cca_cck);

	return 0;
}

static int mt7615_read_temperature(struct seq_file *s, void *data)
{
	struct mt7615_dev *dev = dev_get_drvdata(s->private);
	int temp;

	/* cpu */
	temp = mt7615_mcu_get_temperature(dev, 0);
	seq_printf(s, "Temperature: %d\n", temp);

	return 0;
}

static int
mt7615_queues_acq(struct seq_file *s, void *data)
{
	struct mt7615_dev *dev = dev_get_drvdata(s->private);
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
mt7615_queues_read(struct seq_file *s, void *data)
{
	struct mt7615_dev *dev = dev_get_drvdata(s->private);
	static const struct {
		char *queue;
		int id;
	} queue_map[] = {
		{ "PDMA0", MT_TXQ_BE },
		{ "MCUQ", MT_TXQ_MCU },
		{ "MCUFWQ", MT_TXQ_FWDL },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(queue_map); i++) {
		struct mt76_sw_queue *q = &dev->mt76.q_tx[queue_map[i].id];

		if (!q->q)
			continue;

		seq_printf(s,
			   "%s:	queued=%d head=%d tail=%d\n",
			   queue_map[i].queue, q->q->queued, q->q->head,
			   q->q->tail);
	}

	return 0;
}

int mt7615_init_debugfs(struct mt7615_dev *dev)
{
	struct dentry *dir;

	dir = mt76_register_debugfs(&dev->mt76);
	if (!dir)
		return -ENOMEM;

	debugfs_create_devm_seqfile(dev->mt76.dev, "queues", dir,
				    mt7615_queues_read);
	debugfs_create_devm_seqfile(dev->mt76.dev, "acq", dir,
				    mt7615_queues_acq);
	debugfs_create_file("ampdu_stat", 0400, dir, dev, &fops_ampdu_stat);
	debugfs_create_file("scs", 0600, dir, dev, &fops_scs);
	debugfs_create_devm_seqfile(dev->mt76.dev, "radio", dir,
				    mt7615_radio_read);
	debugfs_create_u32("dfs_hw_pattern", 0400, dir, &dev->hw_pattern);
	/* test pattern knobs */
	debugfs_create_u8("pattern_len", 0600, dir,
			  &dev->radar_pattern.n_pulses);
	debugfs_create_u32("pulse_period", 0600, dir,
			   &dev->radar_pattern.period);
	debugfs_create_u16("pulse_width", 0600, dir,
			   &dev->radar_pattern.width);
	debugfs_create_u16("pulse_power", 0600, dir,
			   &dev->radar_pattern.power);
	debugfs_create_file("radar_trigger", 0200, dir, dev,
			    &fops_radar_pattern);
	debugfs_create_devm_seqfile(dev->mt76.dev, "temperature", dir,
				    mt7615_read_temperature);

	return 0;
}
