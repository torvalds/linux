// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/relay.h>
#include "mt7915.h"
#include "eeprom.h"
#include "mcu.h"
#include "mac.h"

#define FW_BIN_LOG_MAGIC	0x44e98caf

/** global debugfs **/

struct hw_queue_map {
	const char *name;
	u8 index;
	u8 pid;
	u8 qid;
};

static int
mt7915_implicit_txbf_set(void *data, u64 val)
{
	struct mt7915_dev *dev = data;

	/* The existing connected stations shall reconnect to apply
	 * new implicit txbf configuration.
	 */
	dev->ibf = !!val;

	return mt7915_mcu_set_txbf(dev, MT_BF_TYPE_UPDATE);
}

static int
mt7915_implicit_txbf_get(void *data, u64 *val)
{
	struct mt7915_dev *dev = data;

	*val = dev->ibf;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_implicit_txbf, mt7915_implicit_txbf_get,
			 mt7915_implicit_txbf_set, "%lld\n");

/* test knob of system error recovery */
static ssize_t
mt7915_sys_recovery_set(struct file *file, const char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct mt7915_phy *phy = file->private_data;
	struct mt7915_dev *dev = phy->dev;
	bool band = phy->mt76->band_idx;
	char buf[16];
	int ret = 0;
	u16 val;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (kstrtou16(buf, 0, &val))
		return -EINVAL;

	switch (val) {
	/*
	 * 0: grab firmware current SER state.
	 * 1: trigger & enable system error L1 recovery.
	 * 2: trigger & enable system error L2 recovery.
	 * 3: trigger & enable system error L3 rx abort.
	 * 4: trigger & enable system error L3 tx abort
	 * 5: trigger & enable system error L3 tx disable.
	 * 6: trigger & enable system error L3 bf recovery.
	 * 7: trigger & enable system error full recovery.
	 * 8: trigger firmware crash.
	 */
	case SER_QUERY:
		ret = mt7915_mcu_set_ser(dev, 0, 0, band);
		break;
	case SER_SET_RECOVER_L1:
	case SER_SET_RECOVER_L2:
	case SER_SET_RECOVER_L3_RX_ABORT:
	case SER_SET_RECOVER_L3_TX_ABORT:
	case SER_SET_RECOVER_L3_TX_DISABLE:
	case SER_SET_RECOVER_L3_BF:
		ret = mt7915_mcu_set_ser(dev, SER_ENABLE, BIT(val), band);
		if (ret)
			return ret;

		ret = mt7915_mcu_set_ser(dev, SER_RECOVER, val, band);
		break;

	/* enable full chip reset */
	case SER_SET_RECOVER_FULL:
		mt76_set(dev, MT_WFDMA0_MCU_HOST_INT_ENA, MT_MCU_CMD_WDT_MASK);
		ret = mt7915_mcu_set_ser(dev, 1, 3, band);
		if (ret)
			return ret;

		dev->recovery.state |= MT_MCU_CMD_WDT_MASK;
		mt7915_reset(dev);
		break;

	/* WARNING: trigger firmware crash */
	case SER_SET_SYSTEM_ASSERT:
		mt76_wr(dev, MT_MCU_WM_CIRQ_EINT_MASK_CLR_ADDR, BIT(18));
		mt76_wr(dev, MT_MCU_WM_CIRQ_EINT_SOFT_ADDR, BIT(18));
		break;
	default:
		break;
	}

	return ret ? ret : count;
}

static ssize_t
mt7915_sys_recovery_get(struct file *file, char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct mt7915_phy *phy = file->private_data;
	struct mt7915_dev *dev = phy->dev;
	char *buff;
	int desc = 0;
	ssize_t ret;
	static const size_t bufsz = 1024;

	buff = kmalloc(bufsz, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	/* HELP */
	desc += scnprintf(buff + desc, bufsz - desc,
			  "Please echo the correct value ...\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "0: grab firmware transient SER state\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "1: trigger system error L1 recovery\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "2: trigger system error L2 recovery\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "3: trigger system error L3 rx abort\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "4: trigger system error L3 tx abort\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "5: trigger system error L3 tx disable\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "6: trigger system error L3 bf recovery\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "7: trigger system error full recovery\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "8: trigger firmware crash\n");

	/* SER statistics */
	desc += scnprintf(buff + desc, bufsz - desc,
			  "\nlet's dump firmware SER statistics...\n");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_STATUS        = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_SER_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PLE_ERR       = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PLE_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PLE_ERR_1     = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PLE1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PLE_ERR_AMSDU = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PLE_AMSDU_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PSE_ERR       = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PSE_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_PSE_ERR_1     = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_PSE1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR6_B0 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR6_BN0_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR6_B1 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR6_BN1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR7_B0 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR7_BN0_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "::E  R , SER_LMAC_WISR7_B1 = 0x%08x\n",
			  mt76_rr(dev, MT_SWDEF_LAMC_WISR7_BN1_STATS));
	desc += scnprintf(buff + desc, bufsz - desc,
			  "\nSYS_RESET_COUNT: WM %d, WA %d\n",
			  dev->recovery.wm_reset_count,
			  dev->recovery.wa_reset_count);

	ret = simple_read_from_buffer(user_buf, count, ppos, buff, desc);
	kfree(buff);
	return ret;
}

static const struct file_operations mt7915_sys_recovery_ops = {
	.write = mt7915_sys_recovery_set,
	.read = mt7915_sys_recovery_get,
	.open = simple_open,
	.llseek = default_llseek,
};

static int
mt7915_radar_trigger(void *data, u64 val)
{
	struct mt7915_dev *dev = data;

	if (val > MT_RX_SEL2)
		return -EINVAL;

	return mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_RADAR_EMULATE,
				       val, 0, 0);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_radar_trigger, NULL,
			 mt7915_radar_trigger, "%lld\n");

static int
mt7915_muru_debug_set(void *data, u64 val)
{
	struct mt7915_dev *dev = data;

	dev->muru_debug = val;
	mt7915_mcu_muru_debug_set(dev, dev->muru_debug);

	return 0;
}

static int
mt7915_muru_debug_get(void *data, u64 *val)
{
	struct mt7915_dev *dev = data;

	*val = dev->muru_debug;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_muru_debug, mt7915_muru_debug_get,
			 mt7915_muru_debug_set, "%lld\n");

static int mt7915_muru_stats_show(struct seq_file *file, void *data)
{
	struct mt7915_phy *phy = file->private;
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_mcu_muru_stats mu_stats = {};
	static const char * const dl_non_he_type[] = {
		"CCK", "OFDM", "HT MIX", "HT GF",
		"VHT SU", "VHT 2MU", "VHT 3MU", "VHT 4MU"
	};
	static const char * const dl_he_type[] = {
		"HE SU", "HE EXT", "HE 2MU", "HE 3MU", "HE 4MU",
		"HE 2RU", "HE 3RU", "HE 4RU", "HE 5-8RU", "HE 9-16RU",
		"HE >16RU"
	};
	static const char * const ul_he_type[] = {
		"HE 2MU", "HE 3MU", "HE 4MU", "HE SU", "HE 2RU",
		"HE 3RU", "HE 4RU", "HE 5-8RU", "HE 9-16RU", "HE >16RU"
	};
	int ret, i;
	u64 total_ppdu_cnt, sub_total_cnt;

	if (!dev->muru_debug) {
		seq_puts(file, "Please enable muru_debug first.\n");
		return 0;
	}

	mutex_lock(&dev->mt76.mutex);

	ret = mt7915_mcu_muru_debug_get(phy, &mu_stats);
	if (ret)
		goto exit;

	/* Non-HE Downlink*/
	seq_puts(file, "[Non-HE]\nDownlink\nData Type:  ");

	for (i = 0; i < 5; i++)
		seq_printf(file, "%8s | ", dl_non_he_type[i]);

#define __dl_u32(s)     le32_to_cpu(mu_stats.dl.s)
	seq_puts(file, "\nTotal Count:");
	seq_printf(file, "%8u | %8u | %8u | %8u | %8u | ",
		   __dl_u32(cck_cnt),
		   __dl_u32(ofdm_cnt),
		   __dl_u32(htmix_cnt),
		   __dl_u32(htgf_cnt),
		   __dl_u32(vht_su_cnt));

	seq_puts(file, "\nDownlink MU-MIMO\nData Type:  ");

	for (i = 5; i < 8; i++)
		seq_printf(file, "%8s | ", dl_non_he_type[i]);

	seq_puts(file, "\nTotal Count:");
	seq_printf(file, "%8u | %8u | %8u | ",
		   __dl_u32(vht_2mu_cnt),
		   __dl_u32(vht_3mu_cnt),
		   __dl_u32(vht_4mu_cnt));

	sub_total_cnt = __dl_u32(vht_2mu_cnt) +
		__dl_u32(vht_3mu_cnt) +
		__dl_u32(vht_4mu_cnt);

	seq_printf(file, "\nTotal non-HE MU-MIMO DL PPDU count: %lld",
		   sub_total_cnt);

	total_ppdu_cnt = sub_total_cnt +
		__dl_u32(cck_cnt) +
		__dl_u32(ofdm_cnt) +
		__dl_u32(htmix_cnt) +
		__dl_u32(htgf_cnt) +
		__dl_u32(vht_su_cnt);

	seq_printf(file, "\nAll non-HE DL PPDU count: %lld", total_ppdu_cnt);

	/* HE Downlink */
	seq_puts(file, "\n\n[HE]\nDownlink\nData Type:  ");

	for (i = 0; i < 2; i++)
		seq_printf(file, "%8s | ", dl_he_type[i]);

	seq_puts(file, "\nTotal Count:");
	seq_printf(file, "%8u | %8u | ",
		   __dl_u32(he_su_cnt),
		   __dl_u32(he_ext_su_cnt));

	seq_puts(file, "\nDownlink MU-MIMO\nData Type:  ");

	for (i = 2; i < 5; i++)
		seq_printf(file, "%8s | ", dl_he_type[i]);

	seq_puts(file, "\nTotal Count:");
	seq_printf(file, "%8u | %8u | %8u | ",
		   __dl_u32(he_2mu_cnt),
		   __dl_u32(he_3mu_cnt),
		   __dl_u32(he_4mu_cnt));

	seq_puts(file, "\nDownlink OFDMA\nData Type:  ");

	for (i = 5; i < 11; i++)
		seq_printf(file, "%8s | ", dl_he_type[i]);

	seq_puts(file, "\nTotal Count:");
	seq_printf(file, "%8u | %8u | %8u | %8u | %9u | %8u | ",
		   __dl_u32(he_2ru_cnt),
		   __dl_u32(he_3ru_cnt),
		   __dl_u32(he_4ru_cnt),
		   __dl_u32(he_5to8ru_cnt),
		   __dl_u32(he_9to16ru_cnt),
		   __dl_u32(he_gtr16ru_cnt));

	sub_total_cnt = __dl_u32(he_2mu_cnt) +
		__dl_u32(he_3mu_cnt) +
		__dl_u32(he_4mu_cnt);
	total_ppdu_cnt = sub_total_cnt;

	seq_printf(file, "\nTotal HE MU-MIMO DL PPDU count: %lld",
		   sub_total_cnt);

	sub_total_cnt = __dl_u32(he_2ru_cnt) +
		__dl_u32(he_3ru_cnt) +
		__dl_u32(he_4ru_cnt) +
		__dl_u32(he_5to8ru_cnt) +
		__dl_u32(he_9to16ru_cnt) +
		__dl_u32(he_gtr16ru_cnt);
	total_ppdu_cnt += sub_total_cnt;

	seq_printf(file, "\nTotal HE OFDMA DL PPDU count: %lld",
		   sub_total_cnt);

	total_ppdu_cnt += __dl_u32(he_su_cnt) +
		__dl_u32(he_ext_su_cnt);

	seq_printf(file, "\nAll HE DL PPDU count: %lld", total_ppdu_cnt);
#undef __dl_u32

	/* HE Uplink */
	seq_puts(file, "\n\nUplink");
	seq_puts(file, "\nTrigger-based Uplink MU-MIMO\nData Type:  ");

	for (i = 0; i < 3; i++)
		seq_printf(file, "%8s | ", ul_he_type[i]);

#define __ul_u32(s)     le32_to_cpu(mu_stats.ul.s)
	seq_puts(file, "\nTotal Count:");
	seq_printf(file, "%8u | %8u | %8u | ",
		   __ul_u32(hetrig_2mu_cnt),
		   __ul_u32(hetrig_3mu_cnt),
		   __ul_u32(hetrig_4mu_cnt));

	seq_puts(file, "\nTrigger-based Uplink OFDMA\nData Type:  ");

	for (i = 3; i < 10; i++)
		seq_printf(file, "%8s | ", ul_he_type[i]);

	seq_puts(file, "\nTotal Count:");
	seq_printf(file, "%8u | %8u | %8u | %8u | %8u | %9u |  %7u | ",
		   __ul_u32(hetrig_su_cnt),
		   __ul_u32(hetrig_2ru_cnt),
		   __ul_u32(hetrig_3ru_cnt),
		   __ul_u32(hetrig_4ru_cnt),
		   __ul_u32(hetrig_5to8ru_cnt),
		   __ul_u32(hetrig_9to16ru_cnt),
		   __ul_u32(hetrig_gtr16ru_cnt));

	sub_total_cnt = __ul_u32(hetrig_2mu_cnt) +
		__ul_u32(hetrig_3mu_cnt) +
		__ul_u32(hetrig_4mu_cnt);
	total_ppdu_cnt = sub_total_cnt;

	seq_printf(file, "\nTotal HE MU-MIMO UL TB PPDU count: %lld",
		   sub_total_cnt);

	sub_total_cnt = __ul_u32(hetrig_2ru_cnt) +
		__ul_u32(hetrig_3ru_cnt) +
		__ul_u32(hetrig_4ru_cnt) +
		__ul_u32(hetrig_5to8ru_cnt) +
		__ul_u32(hetrig_9to16ru_cnt) +
		__ul_u32(hetrig_gtr16ru_cnt);
	total_ppdu_cnt += sub_total_cnt;

	seq_printf(file, "\nTotal HE OFDMA UL TB PPDU count: %lld",
		   sub_total_cnt);

	total_ppdu_cnt += __ul_u32(hetrig_su_cnt);

	seq_printf(file, "\nAll HE UL TB PPDU count: %lld\n", total_ppdu_cnt);
#undef __ul_u32

exit:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}
DEFINE_SHOW_ATTRIBUTE(mt7915_muru_stats);

static int
mt7915_rdd_monitor(struct seq_file *s, void *data)
{
	struct mt7915_dev *dev = dev_get_drvdata(s->private);
	struct cfg80211_chan_def *chandef = &dev->rdd2_chandef;
	const char *bw;
	int ret = 0;

	mutex_lock(&dev->mt76.mutex);

	if (!cfg80211_chandef_valid(chandef)) {
		ret = -EINVAL;
		goto out;
	}

	if (!dev->rdd2_phy) {
		seq_puts(s, "not running\n");
		goto out;
	}

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_40:
		bw = "40";
		break;
	case NL80211_CHAN_WIDTH_80:
		bw = "80";
		break;
	case NL80211_CHAN_WIDTH_160:
		bw = "160";
		break;
	case NL80211_CHAN_WIDTH_80P80:
		bw = "80P80";
		break;
	default:
		bw = "20";
		break;
	}

	seq_printf(s, "channel %d (%d MHz) width %s MHz center1: %d MHz\n",
		   chandef->chan->hw_value, chandef->chan->center_freq,
		   bw, chandef->center_freq1);
out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7915_fw_debug_wm_set(void *data, u64 val)
{
	struct mt7915_dev *dev = data;
	enum {
		DEBUG_TXCMD = 62,
		DEBUG_CMD_RPT_TX,
		DEBUG_CMD_RPT_TRIG,
		DEBUG_SPL,
		DEBUG_RPT_RX,
	} debug;
	bool tx, rx, en;
	int ret;

	dev->fw.debug_wm = val ? MCU_FW_LOG_TO_HOST : 0;

	if (dev->fw.debug_bin)
		val = 16;
	else
		val = dev->fw.debug_wm;

	tx = dev->fw.debug_wm || (dev->fw.debug_bin & BIT(1));
	rx = dev->fw.debug_wm || (dev->fw.debug_bin & BIT(2));
	en = dev->fw.debug_wm || (dev->fw.debug_bin & BIT(0));

	ret = mt7915_mcu_fw_log_2_host(dev, MCU_FW_LOG_WM, val);
	if (ret)
		goto out;

	for (debug = DEBUG_TXCMD; debug <= DEBUG_RPT_RX; debug++) {
		if (debug == DEBUG_RPT_RX)
			val = en && rx;
		else
			val = en && tx;

		ret = mt7915_mcu_fw_dbg_ctrl(dev, debug, val);
		if (ret)
			goto out;
	}

	/* WM CPU info record control */
	mt76_clear(dev, MT_CPU_UTIL_CTRL, BIT(0));
	mt76_wr(dev, MT_DIC_CMD_REG_CMD, BIT(2) | BIT(13) | !dev->fw.debug_wm);
	mt76_wr(dev, MT_MCU_WM_CIRQ_IRQ_MASK_CLR_ADDR, BIT(5));
	mt76_wr(dev, MT_MCU_WM_CIRQ_IRQ_SOFT_ADDR, BIT(5));

out:
	if (ret)
		dev->fw.debug_wm = 0;

	return ret;
}

static int
mt7915_fw_debug_wm_get(void *data, u64 *val)
{
	struct mt7915_dev *dev = data;

	*val = dev->fw.debug_wm;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_wm, mt7915_fw_debug_wm_get,
			 mt7915_fw_debug_wm_set, "%lld\n");

static int
mt7915_fw_debug_wa_set(void *data, u64 val)
{
	struct mt7915_dev *dev = data;
	int ret;

	dev->fw.debug_wa = val ? MCU_FW_LOG_TO_HOST : 0;

	ret = mt7915_mcu_fw_log_2_host(dev, MCU_FW_LOG_WA, dev->fw.debug_wa);
	if (ret)
		goto out;

	ret = mt7915_mcu_wa_cmd(dev, MCU_WA_PARAM_CMD(SET),
				MCU_WA_PARAM_PDMA_RX, !!dev->fw.debug_wa, 0);
out:
	if (ret)
		dev->fw.debug_wa = 0;

	return ret;
}

static int
mt7915_fw_debug_wa_get(void *data, u64 *val)
{
	struct mt7915_dev *dev = data;

	*val = dev->fw.debug_wa;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_wa, mt7915_fw_debug_wa_get,
			 mt7915_fw_debug_wa_set, "%lld\n");

static struct dentry *
create_buf_file_cb(const char *filename, struct dentry *parent, umode_t mode,
		   struct rchan_buf *buf, int *is_global)
{
	struct dentry *f;

	f = debugfs_create_file("fwlog_data", mode, parent, buf,
				&relay_file_operations);
	if (IS_ERR(f))
		return NULL;

	*is_global = 1;

	return f;
}

static int
remove_buf_file_cb(struct dentry *f)
{
	debugfs_remove(f);

	return 0;
}

static int
mt7915_fw_debug_bin_set(void *data, u64 val)
{
	static struct rchan_callbacks relay_cb = {
		.create_buf_file = create_buf_file_cb,
		.remove_buf_file = remove_buf_file_cb,
	};
	struct mt7915_dev *dev = data;

	if (!dev->relay_fwlog)
		dev->relay_fwlog = relay_open("fwlog_data", dev->debugfs_dir,
					    1500, 512, &relay_cb, NULL);
	if (!dev->relay_fwlog)
		return -ENOMEM;

	dev->fw.debug_bin = val;

	relay_reset(dev->relay_fwlog);

	return mt7915_fw_debug_wm_set(dev, dev->fw.debug_wm);
}

static int
mt7915_fw_debug_bin_get(void *data, u64 *val)
{
	struct mt7915_dev *dev = data;

	*val = dev->fw.debug_bin;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug_bin, mt7915_fw_debug_bin_get,
			 mt7915_fw_debug_bin_set, "%lld\n");

static int
mt7915_fw_util_wm_show(struct seq_file *file, void *data)
{
	struct mt7915_dev *dev = file->private;

	seq_printf(file, "Program counter: 0x%x\n", mt76_rr(dev, MT_WM_MCU_PC));

	if (dev->fw.debug_wm) {
		seq_printf(file, "Busy: %u%%  Peak busy: %u%%\n",
			   mt76_rr(dev, MT_CPU_UTIL_BUSY_PCT),
			   mt76_rr(dev, MT_CPU_UTIL_PEAK_BUSY_PCT));
		seq_printf(file, "Idle count: %u  Peak idle count: %u\n",
			   mt76_rr(dev, MT_CPU_UTIL_IDLE_CNT),
			   mt76_rr(dev, MT_CPU_UTIL_PEAK_IDLE_CNT));
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7915_fw_util_wm);

static int
mt7915_fw_util_wa_show(struct seq_file *file, void *data)
{
	struct mt7915_dev *dev = file->private;

	seq_printf(file, "Program counter: 0x%x\n", mt76_rr(dev, MT_WA_MCU_PC));

	if (dev->fw.debug_wa)
		return mt7915_mcu_wa_cmd(dev, MCU_WA_PARAM_CMD(QUERY),
					 MCU_WA_PARAM_CPU_UTIL, 0, 0);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7915_fw_util_wa);

static void
mt7915_ampdu_stat_read_phy(struct mt7915_phy *phy,
			   struct seq_file *file)
{
	struct mt7915_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	int bound[15], range[4], i;
	u8 band = phy->mt76->band_idx;

	/* Tx ampdu stat */
	for (i = 0; i < ARRAY_SIZE(range); i++)
		range[i] = mt76_rr(dev, MT_MIB_ARNG(band, i));

	for (i = 0; i < ARRAY_SIZE(bound); i++)
		bound[i] = MT_MIB_ARNCR_RANGE(range[i / 4], i % 4) + 1;

	seq_printf(file, "\nPhy %d, Phy band %d\n", ext_phy, band);

	seq_printf(file, "Length: %8d | ", bound[0]);
	for (i = 0; i < ARRAY_SIZE(bound) - 1; i++)
		seq_printf(file, "%3d -%3d | ",
			   bound[i] + 1, bound[i + 1]);

	seq_puts(file, "\nCount:  ");
	for (i = 0; i < ARRAY_SIZE(bound); i++)
		seq_printf(file, "%8d | ", phy->mt76->aggr_stats[i]);
	seq_puts(file, "\n");

	seq_printf(file, "BA miss count: %d\n", phy->mib.ba_miss_cnt);
}

static void
mt7915_txbf_stat_read_phy(struct mt7915_phy *phy, struct seq_file *s)
{
	static const char * const bw[] = {
		"BW20", "BW40", "BW80", "BW160"
	};
	struct mib_stats *mib = &phy->mib;

	/* Tx Beamformer monitor */
	seq_puts(s, "\nTx Beamformer applied PPDU counts: ");

	seq_printf(s, "iBF: %d, eBF: %d\n",
		   mib->tx_bf_ibf_ppdu_cnt,
		   mib->tx_bf_ebf_ppdu_cnt);

	/* Tx Beamformer Rx feedback monitor */
	seq_puts(s, "Tx Beamformer Rx feedback statistics: ");

	seq_printf(s, "All: %d, HE: %d, VHT: %d, HT: %d, ",
		   mib->tx_bf_rx_fb_all_cnt,
		   mib->tx_bf_rx_fb_he_cnt,
		   mib->tx_bf_rx_fb_vht_cnt,
		   mib->tx_bf_rx_fb_ht_cnt);

	seq_printf(s, "%s, NC: %d, NR: %d\n",
		   bw[mib->tx_bf_rx_fb_bw],
		   mib->tx_bf_rx_fb_nc_cnt,
		   mib->tx_bf_rx_fb_nr_cnt);

	/* Tx Beamformee Rx NDPA & Tx feedback report */
	seq_printf(s, "Tx Beamformee successful feedback frames: %d\n",
		   mib->tx_bf_fb_cpl_cnt);
	seq_printf(s, "Tx Beamformee feedback triggered counts: %d\n",
		   mib->tx_bf_fb_trig_cnt);

	/* Tx SU & MU counters */
	seq_printf(s, "Tx multi-user Beamforming counts: %d\n",
		   mib->tx_bf_cnt);
	seq_printf(s, "Tx multi-user MPDU counts: %d\n", mib->tx_mu_mpdu_cnt);
	seq_printf(s, "Tx multi-user successful MPDU counts: %d\n",
		   mib->tx_mu_acked_mpdu_cnt);
	seq_printf(s, "Tx single-user successful MPDU counts: %d\n",
		   mib->tx_su_acked_mpdu_cnt);

	seq_puts(s, "\n");
}

static int
mt7915_tx_stats_show(struct seq_file *file, void *data)
{
	struct mt7915_phy *phy = file->private;
	struct mt7915_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	int i;

	mutex_lock(&dev->mt76.mutex);

	mt7915_ampdu_stat_read_phy(phy, file);
	mt7915_mac_update_stats(phy);
	mt7915_txbf_stat_read_phy(phy, file);

	/* Tx amsdu info */
	seq_puts(file, "Tx MSDU statistics:\n");
	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++) {
		seq_printf(file, "AMSDU pack count of %d MSDU in TXD: %8d ",
			   i + 1, mib->tx_amsdu[i]);
		if (mib->tx_amsdu_cnt)
			seq_printf(file, "(%3d%%)\n",
				   mib->tx_amsdu[i] * 100 / mib->tx_amsdu_cnt);
		else
			seq_puts(file, "\n");
	}

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7915_tx_stats);

static void
mt7915_hw_queue_read(struct seq_file *s, u32 size,
		     const struct hw_queue_map *map)
{
	struct mt7915_phy *phy = s->private;
	struct mt7915_dev *dev = phy->dev;
	u32 i, val;

	val = mt76_rr(dev, MT_FL_Q_EMPTY);
	for (i = 0; i < size; i++) {
		u32 ctrl, head, tail, queued;

		if (val & BIT(map[i].index))
			continue;

		ctrl = BIT(31) | (map[i].pid << 10) | ((u32)map[i].qid << 24);
		mt76_wr(dev, MT_FL_Q0_CTRL, ctrl);

		head = mt76_get_field(dev, MT_FL_Q2_CTRL,
				      GENMASK(11, 0));
		tail = mt76_get_field(dev, MT_FL_Q2_CTRL,
				      GENMASK(27, 16));
		queued = mt76_get_field(dev, MT_FL_Q3_CTRL,
					GENMASK(11, 0));

		seq_printf(s, "\t%s: ", map[i].name);
		seq_printf(s, "queued:0x%03x head:0x%03x tail:0x%03x\n",
			   queued, head, tail);
	}
}

static void
mt7915_sta_hw_queue_read(void *data, struct ieee80211_sta *sta)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_dev *dev = msta->vif->phy->dev;
	struct seq_file *s = data;
	u8 ac;

	for (ac = 0; ac < 4; ac++) {
		u32 qlen, ctrl, val;
		u32 idx = msta->wcid.idx >> 5;
		u8 offs = msta->wcid.idx & GENMASK(4, 0);

		ctrl = BIT(31) | BIT(11) | (ac << 24);
		val = mt76_rr(dev, MT_PLE_AC_QEMPTY(ac, idx));

		if (val & BIT(offs))
			continue;

		mt76_wr(dev, MT_FL_Q0_CTRL, ctrl | msta->wcid.idx);
		qlen = mt76_get_field(dev, MT_FL_Q3_CTRL,
				      GENMASK(11, 0));
		seq_printf(s, "\tSTA %pM wcid %d: AC%d%d queued:%d\n",
			   sta->addr, msta->wcid.idx,
			   msta->vif->mt76.wmm_idx, ac, qlen);
	}
}

static int
mt7915_hw_queues_show(struct seq_file *file, void *data)
{
	struct mt7915_phy *phy = file->private;
	struct mt7915_dev *dev = phy->dev;
	static const struct hw_queue_map ple_queue_map[] = {
		{ "CPU_Q0",  0,  1, MT_CTX0	      },
		{ "CPU_Q1",  1,  1, MT_CTX0 + 1	      },
		{ "CPU_Q2",  2,  1, MT_CTX0 + 2	      },
		{ "CPU_Q3",  3,  1, MT_CTX0 + 3	      },
		{ "ALTX_Q0", 8,  2, MT_LMAC_ALTX0     },
		{ "BMC_Q0",  9,  2, MT_LMAC_BMC0      },
		{ "BCN_Q0",  10, 2, MT_LMAC_BCN0      },
		{ "PSMP_Q0", 11, 2, MT_LMAC_PSMP0     },
		{ "ALTX_Q1", 12, 2, MT_LMAC_ALTX0 + 4 },
		{ "BMC_Q1",  13, 2, MT_LMAC_BMC0  + 4 },
		{ "BCN_Q1",  14, 2, MT_LMAC_BCN0  + 4 },
		{ "PSMP_Q1", 15, 2, MT_LMAC_PSMP0 + 4 },
	};
	static const struct hw_queue_map pse_queue_map[] = {
		{ "CPU Q0",  0,  1, MT_CTX0	      },
		{ "CPU Q1",  1,  1, MT_CTX0 + 1	      },
		{ "CPU Q2",  2,  1, MT_CTX0 + 2	      },
		{ "CPU Q3",  3,  1, MT_CTX0 + 3	      },
		{ "HIF_Q0",  8,  0, MT_HIF0	      },
		{ "HIF_Q1",  9,  0, MT_HIF0 + 1	      },
		{ "HIF_Q2",  10, 0, MT_HIF0 + 2	      },
		{ "HIF_Q3",  11, 0, MT_HIF0 + 3	      },
		{ "HIF_Q4",  12, 0, MT_HIF0 + 4	      },
		{ "HIF_Q5",  13, 0, MT_HIF0 + 5	      },
		{ "LMAC_Q",  16, 2, 0		      },
		{ "MDP_TXQ", 17, 2, 1		      },
		{ "MDP_RXQ", 18, 2, 2		      },
		{ "SEC_TXQ", 19, 2, 3		      },
		{ "SEC_RXQ", 20, 2, 4		      },
	};
	u32 val, head, tail;

	/* ple queue */
	val = mt76_rr(dev, MT_PLE_FREEPG_CNT);
	head = mt76_get_field(dev, MT_PLE_FREEPG_HEAD_TAIL, GENMASK(11, 0));
	tail = mt76_get_field(dev, MT_PLE_FREEPG_HEAD_TAIL, GENMASK(27, 16));
	seq_puts(file, "PLE page info:\n");
	seq_printf(file,
		   "\tTotal free page: 0x%08x head: 0x%03x tail: 0x%03x\n",
		   val, head, tail);

	val = mt76_rr(dev, MT_PLE_PG_HIF_GROUP);
	head = mt76_get_field(dev, MT_PLE_HIF_PG_INFO, GENMASK(11, 0));
	tail = mt76_get_field(dev, MT_PLE_HIF_PG_INFO, GENMASK(27, 16));
	seq_printf(file, "\tHIF free page: 0x%03x res: 0x%03x used: 0x%03x\n",
		   val, head, tail);

	seq_puts(file, "PLE non-empty queue info:\n");
	mt7915_hw_queue_read(file, ARRAY_SIZE(ple_queue_map),
			     &ple_queue_map[0]);

	/* iterate per-sta ple queue */
	ieee80211_iterate_stations_atomic(phy->mt76->hw,
					  mt7915_sta_hw_queue_read, file);
	/* pse queue */
	seq_puts(file, "PSE non-empty queue info:\n");
	mt7915_hw_queue_read(file, ARRAY_SIZE(pse_queue_map),
			     &pse_queue_map[0]);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7915_hw_queues);

static int
mt7915_xmit_queues_show(struct seq_file *file, void *data)
{
	struct mt7915_phy *phy = file->private;
	struct mt7915_dev *dev = phy->dev;
	struct {
		struct mt76_queue *q;
		char *queue;
	} queue_map[] = {
		{ phy->mt76->q_tx[MT_TXQ_BE],	 "   MAIN"  },
		{ dev->mt76.q_mcu[MT_MCUQ_WM],	 "  MCUWM"  },
		{ dev->mt76.q_mcu[MT_MCUQ_WA],	 "  MCUWA"  },
		{ dev->mt76.q_mcu[MT_MCUQ_FWDL], "MCUFWDL" },
	};
	int i;

	seq_puts(file, "     queue | hw-queued |      head |      tail |\n");
	for (i = 0; i < ARRAY_SIZE(queue_map); i++) {
		struct mt76_queue *q = queue_map[i].q;

		if (!q)
			continue;

		seq_printf(file, "   %s | %9d | %9d | %9d |\n",
			   queue_map[i].queue, q->queued, q->head,
			   q->tail);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7915_xmit_queues);

#define mt7915_txpower_puts(rate)						\
({										\
	len += scnprintf(buf + len, sz - len, "%-16s:", #rate " (TMAC)");	\
	for (i = 0; i < mt7915_sku_group_len[SKU_##rate]; i++, offs++)		\
		len += scnprintf(buf + len, sz - len, " %6d", txpwr[offs]);	\
	len += scnprintf(buf + len, sz - len, "\n");				\
})

#define mt7915_txpower_sets(rate, pwr, flag)			\
({								\
	offs += len;						\
	len = mt7915_sku_group_len[rate];			\
	if (mode == flag) {					\
		for (i = 0; i < len; i++)			\
			req.txpower_sku[offs + i] = pwr;	\
	}							\
})

static ssize_t
mt7915_rate_txpower_get(struct file *file, char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct mt7915_phy *phy = file->private_data;
	struct mt7915_dev *dev = phy->dev;
	s8 txpwr[MT7915_SKU_RATE_NUM];
	static const size_t sz = 2048;
	u8 band = phy->mt76->band_idx;
	int i, offs = 0, len = 0;
	ssize_t ret;
	char *buf;
	u32 reg;

	buf = kzalloc(sz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = mt7915_mcu_get_txpower_sku(phy, txpwr, sizeof(txpwr));
	if (ret)
		goto out;

	/* Txpower propagation path: TMAC -> TXV -> BBP */
	len += scnprintf(buf + len, sz - len,
			 "\nPhy%d Tx power table (channel %d)\n",
			 phy != &dev->phy, phy->mt76->chandef.chan->hw_value);
	len += scnprintf(buf + len, sz - len, "%-16s  %6s %6s %6s %6s\n",
			 " ", "1m", "2m", "5m", "11m");
	mt7915_txpower_puts(CCK);

	len += scnprintf(buf + len, sz - len,
			 "%-16s  %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 " ", "6m", "9m", "12m", "18m", "24m", "36m", "48m",
			 "54m");
	mt7915_txpower_puts(OFDM);

	len += scnprintf(buf + len, sz - len,
			 "%-16s  %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4",
			 "mcs5", "mcs6", "mcs7");
	mt7915_txpower_puts(HT_BW20);

	len += scnprintf(buf + len, sz - len,
			 "%-16s  %6s %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5",
			 "mcs6", "mcs7", "mcs32");
	mt7915_txpower_puts(HT_BW40);

	len += scnprintf(buf + len, sz - len,
			 "%-16s  %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n",
			 " ", "mcs0", "mcs1", "mcs2", "mcs3", "mcs4", "mcs5",
			 "mcs6", "mcs7", "mcs8", "mcs9", "mcs10", "mcs11");
	mt7915_txpower_puts(VHT_BW20);
	mt7915_txpower_puts(VHT_BW40);
	mt7915_txpower_puts(VHT_BW80);
	mt7915_txpower_puts(VHT_BW160);
	mt7915_txpower_puts(HE_RU26);
	mt7915_txpower_puts(HE_RU52);
	mt7915_txpower_puts(HE_RU106);
	mt7915_txpower_puts(HE_RU242);
	mt7915_txpower_puts(HE_RU484);
	mt7915_txpower_puts(HE_RU996);
	mt7915_txpower_puts(HE_RU2x996);

	reg = is_mt7915(&dev->mt76) ? MT_WF_PHY_TPC_CTRL_STAT(band) :
	      MT_WF_PHY_TPC_CTRL_STAT_MT7916(band);

	len += scnprintf(buf + len, sz - len, "\nTx power (bbp)  : %6ld\n",
			 mt76_get_field(dev, reg, MT_WF_PHY_TPC_POWER));

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

out:
	kfree(buf);
	return ret;
}

static ssize_t
mt7915_rate_txpower_set(struct file *file, const char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct mt7915_phy *phy = file->private_data;
	struct mt7915_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct mt7915_mcu_txpower_sku req = {
		.format_id = TX_POWER_LIMIT_TABLE,
		.band_idx = phy->mt76->band_idx,
	};
	char buf[100];
	int i, ret, pwr160 = 0, pwr80 = 0, pwr40 = 0, pwr20 = 0;
	enum mac80211_rx_encoding mode;
	u32 offs = 0, len = 0;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	if (sscanf(buf, "%u %u %u %u %u",
		   &mode, &pwr160, &pwr80, &pwr40, &pwr20) != 5) {
		dev_warn(dev->mt76.dev,
			 "per bandwidth power limit: Mode BW160 BW80 BW40 BW20");
		return -EINVAL;
	}

	if (mode > RX_ENC_HE)
		return -EINVAL;

	if (pwr160)
		pwr160 = mt7915_get_power_bound(phy, pwr160);
	if (pwr80)
		pwr80 = mt7915_get_power_bound(phy, pwr80);
	if (pwr40)
		pwr40 = mt7915_get_power_bound(phy, pwr40);
	if (pwr20)
		pwr20 = mt7915_get_power_bound(phy, pwr20);

	if (pwr160 < 0 || pwr80 < 0 || pwr40 < 0 || pwr20 < 0)
		return -EINVAL;

	mutex_lock(&dev->mt76.mutex);
	ret = mt7915_mcu_get_txpower_sku(phy, req.txpower_sku,
					 sizeof(req.txpower_sku));
	if (ret)
		goto out;

	mt7915_txpower_sets(SKU_CCK, pwr20, RX_ENC_LEGACY);
	mt7915_txpower_sets(SKU_OFDM, pwr20, RX_ENC_LEGACY);
	if (mode == RX_ENC_LEGACY)
		goto skip;

	mt7915_txpower_sets(SKU_HT_BW20, pwr20, RX_ENC_HT);
	mt7915_txpower_sets(SKU_HT_BW40, pwr40, RX_ENC_HT);
	if (mode == RX_ENC_HT)
		goto skip;

	mt7915_txpower_sets(SKU_VHT_BW20, pwr20, RX_ENC_VHT);
	mt7915_txpower_sets(SKU_VHT_BW40, pwr40, RX_ENC_VHT);
	mt7915_txpower_sets(SKU_VHT_BW80, pwr80, RX_ENC_VHT);
	mt7915_txpower_sets(SKU_VHT_BW160, pwr160, RX_ENC_VHT);
	if (mode == RX_ENC_VHT)
		goto skip;

	mt7915_txpower_sets(SKU_HE_RU26, pwr20, RX_ENC_HE + 1);
	mt7915_txpower_sets(SKU_HE_RU52, pwr20, RX_ENC_HE + 1);
	mt7915_txpower_sets(SKU_HE_RU106, pwr20, RX_ENC_HE + 1);
	mt7915_txpower_sets(SKU_HE_RU242, pwr20, RX_ENC_HE);
	mt7915_txpower_sets(SKU_HE_RU484, pwr40, RX_ENC_HE);
	mt7915_txpower_sets(SKU_HE_RU996, pwr80, RX_ENC_HE);
	mt7915_txpower_sets(SKU_HE_RU2x996, pwr160, RX_ENC_HE);
skip:
	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(TX_POWER_FEATURE_CTRL),
				&req, sizeof(req), true);
	if (ret)
		goto out;

	mphy->txpower_cur = max(mphy->txpower_cur,
				max(pwr160, max(pwr80, max(pwr40, pwr20))));
out:
	mutex_unlock(&dev->mt76.mutex);

	return ret ? ret : count;
}

static const struct file_operations mt7915_rate_txpower_fops = {
	.write = mt7915_rate_txpower_set,
	.read = mt7915_rate_txpower_get,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int
mt7915_twt_stats(struct seq_file *s, void *data)
{
	struct mt7915_dev *dev = dev_get_drvdata(s->private);
	struct mt7915_twt_flow *iter;

	rcu_read_lock();

	seq_puts(s, "     wcid |       id |    flags |      exp | mantissa");
	seq_puts(s, " | duration |            tsf |\n");
	list_for_each_entry_rcu(iter, &dev->twt_list, list)
		seq_printf(s,
			"%9d | %8d | %5c%c%c%c | %8d | %8d | %8d | %14lld |\n",
			iter->wcid, iter->id,
			iter->sched ? 's' : 'u',
			iter->protection ? 'p' : '-',
			iter->trigger ? 't' : '-',
			iter->flowtype ? '-' : 'a',
			iter->exp, iter->mantissa,
			iter->duration, iter->tsf);

	rcu_read_unlock();

	return 0;
}

/* The index of RF registers use the generic regidx, combined with two parts:
 * WF selection [31:24] and offset [23:0].
 */
static int
mt7915_rf_regval_get(void *data, u64 *val)
{
	struct mt7915_dev *dev = data;
	u32 regval;
	int ret;

	ret = mt7915_mcu_rf_regval(dev, dev->mt76.debugfs_reg, &regval, false);
	if (ret)
		return ret;

	*val = regval;

	return 0;
}

static int
mt7915_rf_regval_set(void *data, u64 val)
{
	struct mt7915_dev *dev = data;
	u32 val32 = val;

	return mt7915_mcu_rf_regval(dev, dev->mt76.debugfs_reg, &val32, true);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_rf_regval, mt7915_rf_regval_get,
			 mt7915_rf_regval_set, "0x%08llx\n");

int mt7915_init_debugfs(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	struct dentry *dir;

	dir = mt76_register_debugfs_fops(phy->mt76, NULL);
	if (!dir)
		return -ENOMEM;
	debugfs_create_file("muru_debug", 0600, dir, dev, &fops_muru_debug);
	debugfs_create_file("muru_stats", 0400, dir, phy,
			    &mt7915_muru_stats_fops);
	debugfs_create_file("hw-queues", 0400, dir, phy,
			    &mt7915_hw_queues_fops);
	debugfs_create_file("xmit-queues", 0400, dir, phy,
			    &mt7915_xmit_queues_fops);
	debugfs_create_file("tx_stats", 0400, dir, phy, &mt7915_tx_stats_fops);
	debugfs_create_file("sys_recovery", 0600, dir, phy,
			    &mt7915_sys_recovery_ops);
	debugfs_create_file("fw_debug_wm", 0600, dir, dev, &fops_fw_debug_wm);
	debugfs_create_file("fw_debug_wa", 0600, dir, dev, &fops_fw_debug_wa);
	debugfs_create_file("fw_debug_bin", 0600, dir, dev, &fops_fw_debug_bin);
	debugfs_create_file("fw_util_wm", 0400, dir, dev,
			    &mt7915_fw_util_wm_fops);
	debugfs_create_file("fw_util_wa", 0400, dir, dev,
			    &mt7915_fw_util_wa_fops);
	debugfs_create_file("implicit_txbf", 0600, dir, dev,
			    &fops_implicit_txbf);
	debugfs_create_file("txpower_sku", 0400, dir, phy,
			    &mt7915_rate_txpower_fops);
	debugfs_create_devm_seqfile(dev->mt76.dev, "twt_stats", dir,
				    mt7915_twt_stats);
	debugfs_create_file("rf_regval", 0600, dir, dev, &fops_rf_regval);

	if (!dev->dbdc_support || phy->mt76->band_idx) {
		debugfs_create_u32("dfs_hw_pattern", 0400, dir,
				   &dev->hw_pattern);
		debugfs_create_file("radar_trigger", 0200, dir, dev,
				    &fops_radar_trigger);
		debugfs_create_devm_seqfile(dev->mt76.dev, "rdd_monitor", dir,
					    mt7915_rdd_monitor);
	}

	if (!ext_phy)
		dev->debugfs_dir = dir;

	return 0;
}

static void
mt7915_debugfs_write_fwlog(struct mt7915_dev *dev, const void *hdr, int hdrlen,
			 const void *data, int len)
{
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;
	void *dest;

	spin_lock_irqsave(&lock, flags);
	dest = relay_reserve(dev->relay_fwlog, hdrlen + len + 4);
	if (dest) {
		*(u32 *)dest = hdrlen + len;
		dest += 4;

		if (hdrlen) {
			memcpy(dest, hdr, hdrlen);
			dest += hdrlen;
		}

		memcpy(dest, data, len);
		relay_flush(dev->relay_fwlog);
	}
	spin_unlock_irqrestore(&lock, flags);
}

void mt7915_debugfs_rx_fw_monitor(struct mt7915_dev *dev, const void *data, int len)
{
	struct {
		__le32 magic;
		__le32 timestamp;
		__le16 msg_type;
		__le16 len;
	} hdr = {
		.magic = cpu_to_le32(FW_BIN_LOG_MAGIC),
		.msg_type = cpu_to_le16(PKT_TYPE_RX_FW_MONITOR),
	};

	if (!dev->relay_fwlog)
		return;

	hdr.timestamp = cpu_to_le32(mt76_rr(dev, MT_LPON_FRCR(0)));
	hdr.len = *(__le16 *)data;
	mt7915_debugfs_write_fwlog(dev, &hdr, sizeof(hdr), data, len);
}

bool mt7915_debugfs_rx_log(struct mt7915_dev *dev, const void *data, int len)
{
	if (get_unaligned_le32(data) != FW_BIN_LOG_MAGIC)
		return false;

	if (dev->relay_fwlog)
		mt7915_debugfs_write_fwlog(dev, NULL, 0, data, len);

	return true;
}

#ifdef CONFIG_MAC80211_DEBUGFS
/** per-station debugfs **/

static ssize_t mt7915_sta_fixed_rate_set(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_dev *dev = msta->vif->phy->dev;
	struct ieee80211_vif *vif;
	struct sta_phy phy = {};
	char buf[100];
	int ret;
	u32 field;
	u8 i, gi, he_ltf;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (count && buf[count - 1] == '\n')
		buf[count - 1] = '\0';
	else
		buf[count] = '\0';

	/* mode - cck: 0, ofdm: 1, ht: 2, gf: 3, vht: 4, he_su: 8, he_er: 9
	 * bw - bw20: 0, bw40: 1, bw80: 2, bw160: 3
	 * nss - vht: 1~4, he: 1~4, others: ignore
	 * mcs - cck: 0~4, ofdm: 0~7, ht: 0~32, vht: 0~9, he_su: 0~11, he_er: 0~2
	 * gi - (ht/vht) lgi: 0, sgi: 1; (he) 0.8us: 0, 1.6us: 1, 3.2us: 2
	 * ldpc - off: 0, on: 1
	 * stbc - off: 0, on: 1
	 * he_ltf - 1xltf: 0, 2xltf: 1, 4xltf: 2
	 */
	if (sscanf(buf, "%hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu",
		   &phy.type, &phy.bw, &phy.nss, &phy.mcs, &gi,
		   &phy.ldpc, &phy.stbc, &he_ltf) != 8) {
		dev_warn(dev->mt76.dev,
			 "format: Mode BW NSS MCS (HE)GI LDPC STBC HE_LTF\n");
		field = RATE_PARAM_AUTO;
		goto out;
	}

	phy.ldpc = (phy.bw || phy.ldpc) * GENMASK(2, 0);
	for (i = 0; i <= phy.bw; i++) {
		phy.sgi |= gi << (i << sta->deflink.he_cap.has_he);
		phy.he_ltf |= he_ltf << (i << sta->deflink.he_cap.has_he);
	}
	field = RATE_PARAM_FIXED;

out:
	vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);
	ret = mt7915_mcu_set_fixed_rate_ctrl(dev, vif, sta, &phy, field);
	if (ret)
		return -EFAULT;

	return count;
}

static const struct file_operations fops_fixed_rate = {
	.write = mt7915_sta_fixed_rate_set,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int
mt7915_queues_show(struct seq_file *s, void *data)
{
	struct ieee80211_sta *sta = s->private;

	mt7915_sta_hw_queue_read(s, sta);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7915_queues);

void mt7915_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir)
{
	debugfs_create_file("fixed_rate", 0600, dir, sta, &fops_fixed_rate);
	debugfs_create_file("hw-queues", 0400, dir, sta, &mt7915_queues_fops);
}

#endif
