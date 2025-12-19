// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "mt7615.h"

static int
mt7615_reg_set(void *data, u64 val)
{
	struct mt7615_dev *dev = data;

	mt7615_mutex_acquire(dev);
	mt76_wr(dev, dev->mt76.debugfs_reg, val);
	mt7615_mutex_release(dev);

	return 0;
}

static int
mt7615_reg_get(void *data, u64 *val)
{
	struct mt7615_dev *dev = data;

	mt7615_mutex_acquire(dev);
	*val = mt76_rr(dev, dev->mt76.debugfs_reg);
	mt7615_mutex_release(dev);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_regval, mt7615_reg_get, mt7615_reg_set,
			 "0x%08llx\n");

static int
mt7615_radar_pattern_set(void *data, u64 val)
{
	struct mt7615_dev *dev = data;
	int err;

	if (!mt7615_wait_for_mcu_init(dev))
		return 0;

	mt7615_mutex_acquire(dev);
	err = mt7615_mcu_rdd_send_pattern(dev);
	mt7615_mutex_release(dev);

	return err;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_radar_pattern, NULL,
			 mt7615_radar_pattern_set, "%lld\n");

static int mt7615_config(void *data, u64 val)
{
	struct mt7615_dev *dev = data;
	int ret;

	mt7615_mutex_acquire(dev);
	ret = mt76_connac_mcu_chip_config(&dev->mt76);
	mt7615_mutex_release(dev);

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_config, NULL, mt7615_config, "%lld\n");

static int
mt7615_scs_set(void *data, u64 val)
{
	struct mt7615_dev *dev = data;
	struct mt7615_phy *ext_phy;

	if (!mt7615_wait_for_mcu_init(dev))
		return 0;

	mt7615_mac_set_scs(&dev->phy, val);
	ext_phy = mt7615_ext_phy(dev);
	if (ext_phy)
		mt7615_mac_set_scs(ext_phy, val);

	return 0;
}

static int
mt7615_scs_get(void *data, u64 *val)
{
	struct mt7615_dev *dev = data;

	*val = dev->phy.scs_en;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_scs, mt7615_scs_get,
			 mt7615_scs_set, "%lld\n");

static int
mt7615_pm_set(void *data, u64 val)
{
	struct mt7615_dev *dev = data;
	struct mt76_connac_pm *pm = &dev->pm;
	int ret = 0;

	if (!mt7615_wait_for_mcu_init(dev))
		return 0;

	if (!mt7615_firmware_offload(dev) || mt76_is_usb(&dev->mt76))
		return -EOPNOTSUPP;

	mutex_lock(&dev->mt76.mutex);

	if (val == pm->enable)
		goto out;

	if (dev->phy.n_beacon_vif) {
		ret = -EBUSY;
		goto out;
	}

	if (!pm->enable) {
		pm->stats.last_wake_event = jiffies;
		pm->stats.last_doze_event = jiffies;
	}
	/* make sure the chip is awake here and ps_work is scheduled
	 * just at end of the this routine.
	 */
	pm->enable = false;
	mt76_connac_pm_wake(&dev->mphy, pm);

	pm->enable = val;
	mt76_connac_power_save_sched(&dev->mphy, pm);
out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7615_pm_get(void *data, u64 *val)
{
	struct mt7615_dev *dev = data;

	*val = dev->pm.enable;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_pm, mt7615_pm_get, mt7615_pm_set, "%lld\n");

static int
mt7615_pm_stats(struct seq_file *s, void *data)
{
	struct mt7615_dev *dev = dev_get_drvdata(s->private);
	struct mt76_connac_pm *pm = &dev->pm;
	unsigned long awake_time = pm->stats.awake_time;
	unsigned long doze_time = pm->stats.doze_time;

	if (!test_bit(MT76_STATE_PM, &dev->mphy.state))
		awake_time += jiffies - pm->stats.last_wake_event;
	else
		doze_time += jiffies - pm->stats.last_doze_event;

	seq_printf(s, "awake time: %14u\ndoze time: %15u\n",
		   jiffies_to_msecs(awake_time),
		   jiffies_to_msecs(doze_time));

	return 0;
}

static int
mt7615_pm_idle_timeout_set(void *data, u64 val)
{
	struct mt7615_dev *dev = data;

	dev->pm.idle_timeout = msecs_to_jiffies(val);

	return 0;
}

static int
mt7615_pm_idle_timeout_get(void *data, u64 *val)
{
	struct mt7615_dev *dev = data;

	*val = jiffies_to_msecs(dev->pm.idle_timeout);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_pm_idle_timeout, mt7615_pm_idle_timeout_get,
			 mt7615_pm_idle_timeout_set, "%lld\n");

static int
mt7615_dbdc_set(void *data, u64 val)
{
	struct mt7615_dev *dev = data;

	if (!mt7615_wait_for_mcu_init(dev))
		return 0;

	if (val)
		mt7615_register_ext_phy(dev);
	else
		mt7615_unregister_ext_phy(dev);

	return 0;
}

static int
mt7615_dbdc_get(void *data, u64 *val)
{
	struct mt7615_dev *dev = data;

	*val = !!mt7615_ext_phy(dev);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_dbdc, mt7615_dbdc_get,
			 mt7615_dbdc_set, "%lld\n");

static int
mt7615_fw_debug_set(void *data, u64 val)
{
	struct mt7615_dev *dev = data;

	if (!mt7615_wait_for_mcu_init(dev))
		return 0;

	dev->fw_debug = val;

	mt7615_mutex_acquire(dev);
	mt7615_mcu_fw_log_2_host(dev, dev->fw_debug ? 2 : 0);
	mt7615_mutex_release(dev);

	return 0;
}

static int
mt7615_fw_debug_get(void *data, u64 *val)
{
	struct mt7615_dev *dev = data;

	*val = dev->fw_debug;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug, mt7615_fw_debug_get,
			 mt7615_fw_debug_set, "%lld\n");

static int
mt7615_reset_test_set(void *data, u64 val)
{
	struct mt7615_dev *dev = data;
	struct sk_buff *skb;

	if (!mt7615_wait_for_mcu_init(dev))
		return 0;

	skb = alloc_skb(1, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, 1);

	mt7615_mutex_acquire(dev);
	mt76_tx_queue_skb_raw(dev, dev->mphy.q_tx[0], skb, 0);
	mt7615_mutex_release(dev);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_reset_test, NULL,
			 mt7615_reset_test_set, "%lld\n");

static void
mt7615_ampdu_stat_read_phy(struct mt7615_phy *phy,
			   struct seq_file *file)
{
	struct mt7615_dev *dev = file->private;
	u32 reg = is_mt7663(&dev->mt76) ? MT_MIB_ARNG(0) : MT_AGG_ASRCR0;
	int bound[7], i, range;

	if (!phy)
		return;

	range = mt76_rr(dev, reg);
	for (i = 0; i < 4; i++)
		bound[i] = MT_AGG_ASRCR_RANGE(range, i) + 1;

	range = mt76_rr(dev, reg + 4);
	for (i = 0; i < 3; i++)
		bound[i + 4] = MT_AGG_ASRCR_RANGE(range, i) + 1;

	seq_printf(file, "\nPhy %d\n", phy != &dev->phy);

	seq_printf(file, "Length: %8d | ", bound[0]);
	for (i = 0; i < ARRAY_SIZE(bound) - 1; i++)
		seq_printf(file, "%3d -%3d | ",
			   bound[i], bound[i + 1]);
	seq_puts(file, "\nCount:  ");

	for (i = 0; i < ARRAY_SIZE(bound); i++)
		seq_printf(file, "%8d | ", phy->mt76->aggr_stats[i]);
	seq_puts(file, "\n");

	seq_printf(file, "BA miss count: %d\n", phy->mib.ba_miss_cnt);
	seq_printf(file, "PER: %ld.%1ld%%\n",
		   phy->mib.aggr_per / 10, phy->mib.aggr_per % 10);
}

static int
mt7615_ampdu_stat_show(struct seq_file *file, void *data)
{
	struct mt7615_dev *dev = file->private;

	mt7615_mutex_acquire(dev);

	mt7615_ampdu_stat_read_phy(&dev->phy, file);
	mt7615_ampdu_stat_read_phy(mt7615_ext_phy(dev), file);

	mt7615_mutex_release(dev);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7615_ampdu_stat);

static void
mt7615_radio_read_phy(struct mt7615_phy *phy, struct seq_file *s)
{
	struct mt7615_dev *dev = dev_get_drvdata(s->private);
	bool ext_phy = phy != &dev->phy;

	if (!phy)
		return;

	seq_printf(s, "Radio %d sensitivity: ofdm=%d cck=%d\n", ext_phy,
		   phy->ofdm_sensitivity, phy->cck_sensitivity);
	seq_printf(s, "Radio %d false CCA: ofdm=%d cck=%d\n", ext_phy,
		   phy->false_cca_ofdm, phy->false_cca_cck);
}

static int
mt7615_radio_read(struct seq_file *s, void *data)
{
	struct mt7615_dev *dev = dev_get_drvdata(s->private);

	mt7615_radio_read_phy(&dev->phy, s);
	mt7615_radio_read_phy(mt7615_ext_phy(dev), s);

	return 0;
}

static int
mt7615_queues_acq(struct seq_file *s, void *data)
{
	struct mt7615_dev *dev = dev_get_drvdata(s->private);
	int i;

	mt7615_mutex_acquire(dev);

	for (i = 0; i < 16; i++) {
		int j, wmm_idx = i % MT7615_MAX_WMM_SETS;
		int acs = i / MT7615_MAX_WMM_SETS;
		u32 ctrl, val, qlen = 0;

		if (wmm_idx == 3 && is_mt7663(&dev->mt76))
			continue;

		val = mt76_rr(dev, MT_PLE_AC_QEMPTY(acs, wmm_idx));
		ctrl = BIT(31) | BIT(15) | (acs << 8);

		for (j = 0; j < 32; j++) {
			if (val & BIT(j))
				continue;

			mt76_wr(dev, MT_PLE_FL_Q0_CTRL,
				ctrl | (j + (wmm_idx << 5)));
			qlen += mt76_get_field(dev, MT_PLE_FL_Q3_CTRL,
					       GENMASK(11, 0));
		}
		seq_printf(s, "AC%d%d: queued=%d\n", wmm_idx, acs, qlen);
	}

	mt7615_mutex_release(dev);

	return 0;
}

static int
mt7615_queues_read(struct seq_file *s, void *data)
{
	struct mt7615_dev *dev = dev_get_drvdata(s->private);
	struct {
		struct mt76_queue *q;
		char *queue;
	} queue_map[] = {
		{ dev->mphy.q_tx[MT_TXQ_BE], "PDMA0" },
		{ dev->mt76.q_mcu[MT_MCUQ_WM], "MCUQ" },
		{ dev->mt76.q_mcu[MT_MCUQ_FWDL], "MCUFWQ" },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(queue_map); i++) {
		struct mt76_queue *q = queue_map[i].q;

		seq_printf(s,
			   "%s:	queued=%d head=%d tail=%d\n",
			   queue_map[i].queue, q->queued, q->head,
			   q->tail);
	}

	return 0;
}

static int
mt7615_rf_reg_set(void *data, u64 val)
{
	struct mt7615_dev *dev = data;

	mt7615_rf_wr(dev, dev->debugfs_rf_wf, dev->debugfs_rf_reg, val);

	return 0;
}

static int
mt7615_rf_reg_get(void *data, u64 *val)
{
	struct mt7615_dev *dev = data;

	*val = mt7615_rf_rr(dev, dev->debugfs_rf_wf, dev->debugfs_rf_reg);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_rf_reg, mt7615_rf_reg_get, mt7615_rf_reg_set,
			 "0x%08llx\n");

static ssize_t
mt7615_ext_mac_addr_read(struct file *file, char __user *userbuf,
			 size_t count, loff_t *ppos)
{
	struct mt7615_dev *dev = file->private_data;
	u32 len = 32 * ((ETH_ALEN * 3) + 4) + 1;
	u8 addr[ETH_ALEN];
	char *buf;
	int ofs = 0;
	int i;

	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < 32; i++) {
		if (!(dev->muar_mask & BIT(i)))
			continue;

		mt76_wr(dev, MT_WF_RMAC_MAR1,
			FIELD_PREP(MT_WF_RMAC_MAR1_IDX, i * 2) |
			MT_WF_RMAC_MAR1_START);
		put_unaligned_le32(mt76_rr(dev, MT_WF_RMAC_MAR0), addr);
		put_unaligned_le16((mt76_rr(dev, MT_WF_RMAC_MAR1) &
				    MT_WF_RMAC_MAR1_ADDR), addr + 4);
		ofs += snprintf(buf + ofs, len - ofs, "%d=%pM\n", i, addr);
	}

	ofs = simple_read_from_buffer(userbuf, count, ppos, buf, ofs);

	kfree(buf);
	return ofs;
}

static ssize_t
mt7615_ext_mac_addr_write(struct file *file, const char __user *userbuf,
			  size_t count, loff_t *ppos)
{
	struct mt7615_dev *dev = file->private_data;
	unsigned long idx = 0;
	u8 addr[ETH_ALEN];
	char buf[32];
	char *p;

	if (count > sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, userbuf, count))
		return -EFAULT;

	buf[sizeof(buf) - 1] = '\0';

	p = strchr(buf, '=');
	if (p) {
		*p = 0;
		p++;

		if (kstrtoul(buf, 0, &idx) || idx > 31)
			return -EINVAL;
	} else {
		idx = 0;
		p = buf;
	}

	if (!mac_pton(p, addr))
		return -EINVAL;

	if (is_valid_ether_addr(addr)) {
		dev->muar_mask |= BIT(idx);
	} else {
		memset(addr, 0, sizeof(addr));
		dev->muar_mask &= ~BIT(idx);
	}

	mt76_rmw_field(dev, MT_WF_RMAC_MORE(0), MT_WF_RMAC_MORE_MUAR_MODE, 1);
	mt76_wr(dev, MT_WF_RMAC_MAR0, get_unaligned_le32(addr));
	mt76_wr(dev, MT_WF_RMAC_MAR1,
		get_unaligned_le16(addr + 4) |
		FIELD_PREP(MT_WF_RMAC_MAR1_IDX, idx * 2) |
		MT_WF_RMAC_MAR1_START |
		MT_WF_RMAC_MAR1_WRITE);

	mt76_rmw_field(dev, MT_WF_RMAC_MORE(0), MT_WF_RMAC_MORE_MUAR_MODE, !!dev->muar_mask);

	return count;
}

static const struct file_operations fops_ext_mac_addr = {
	.open = simple_open,
	.llseek = generic_file_llseek,
	.read = mt7615_ext_mac_addr_read,
	.write = mt7615_ext_mac_addr_write,
	.owner = THIS_MODULE,
};

static int
mt7663s_sched_quota_read(struct seq_file *s, void *data)
{
	struct mt7615_dev *dev = dev_get_drvdata(s->private);
	struct mt76_sdio *sdio = &dev->mt76.sdio;

	seq_printf(s, "pse_data_quota\t%d\n", sdio->sched.pse_data_quota);
	seq_printf(s, "ple_data_quota\t%d\n", sdio->sched.ple_data_quota);
	seq_printf(s, "pse_mcu_quota\t%d\n", sdio->sched.pse_mcu_quota);
	seq_printf(s, "sched_deficit\t%d\n", sdio->sched.deficit);

	return 0;
}

int mt7615_init_debugfs(struct mt7615_dev *dev)
{
	struct dentry *dir;

	dir = mt76_register_debugfs_fops(&dev->mphy, &fops_regval);
	if (!dir)
		return -ENOMEM;

	if (is_mt7615(&dev->mt76))
		debugfs_create_devm_seqfile(dev->mt76.dev, "xmit-queues", dir,
					    mt7615_queues_read);
	else
		debugfs_create_devm_seqfile(dev->mt76.dev, "xmit-queues", dir,
					    mt76_queues_read);
	debugfs_create_devm_seqfile(dev->mt76.dev, "acq", dir,
				    mt7615_queues_acq);
	debugfs_create_file("ampdu_stat", 0400, dir, dev, &mt7615_ampdu_stat_fops);
	debugfs_create_file("scs", 0600, dir, dev, &fops_scs);
	debugfs_create_file("dbdc", 0600, dir, dev, &fops_dbdc);
	debugfs_create_file("fw_debug", 0600, dir, dev, &fops_fw_debug);
	debugfs_create_file("runtime-pm", 0600, dir, dev, &fops_pm);
	debugfs_create_file("idle-timeout", 0600, dir, dev,
			    &fops_pm_idle_timeout);
	debugfs_create_devm_seqfile(dev->mt76.dev, "runtime_pm_stats", dir,
				    mt7615_pm_stats);
	debugfs_create_devm_seqfile(dev->mt76.dev, "radio", dir,
				    mt7615_radio_read);

	if (is_mt7615(&dev->mt76)) {
		debugfs_create_u32("dfs_hw_pattern", 0400, dir,
				   &dev->hw_pattern);
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
	}

	debugfs_create_file("reset_test", 0200, dir, dev,
			    &fops_reset_test);
	debugfs_create_file("ext_mac_addr", 0600, dir, dev, &fops_ext_mac_addr);

	debugfs_create_u32("rf_wfidx", 0600, dir, &dev->debugfs_rf_wf);
	debugfs_create_u32("rf_regidx", 0600, dir, &dev->debugfs_rf_reg);
	debugfs_create_file_unsafe("rf_regval", 0600, dir, dev,
				   &fops_rf_reg);
	if (is_mt7663(&dev->mt76))
		debugfs_create_file("chip_config", 0600, dir, dev,
				    &fops_config);
	if (mt76_is_sdio(&dev->mt76))
		debugfs_create_devm_seqfile(dev->mt76.dev, "sched-quota", dir,
					    mt7663s_sched_quota_read);

	return 0;
}
EXPORT_SYMBOL_GPL(mt7615_init_debugfs);
