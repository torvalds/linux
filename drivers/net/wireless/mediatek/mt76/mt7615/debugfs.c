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

int mt7615_init_debugfs(struct mt7615_dev *dev)
{
	struct dentry *dir;

	dir = mt76_register_debugfs(&dev->mt76);
	if (!dir)
		return -ENOMEM;

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

	return 0;
}
