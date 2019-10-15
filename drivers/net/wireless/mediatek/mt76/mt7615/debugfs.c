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

int mt7615_init_debugfs(struct mt7615_dev *dev)
{
	struct dentry *dir;

	dir = mt76_register_debugfs(&dev->mt76);
	if (!dir)
		return -ENOMEM;

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
