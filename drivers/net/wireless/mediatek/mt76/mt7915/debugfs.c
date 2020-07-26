// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7915.h"
#include "eeprom.h"

/** global debugfs **/

/* test knob of system layer 1/2 error recovery */
static int mt7915_ser_trigger_set(void *data, u64 val)
{
	enum {
		SER_SET_RECOVER_L1 = 1,
		SER_SET_RECOVER_L2,
		SER_ENABLE = 2,
		SER_RECOVER
	};
	struct mt7915_dev *dev = data;
	int ret = 0;

	switch (val) {
	case SER_SET_RECOVER_L1:
	case SER_SET_RECOVER_L2:
		/* fall through */
		ret = mt7915_mcu_set_ser(dev, SER_ENABLE, BIT(val), 0);
		if (ret)
			return ret;

		return mt7915_mcu_set_ser(dev, SER_RECOVER, val, 0);
	default:
		break;
	}

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_ser_trigger, NULL,
			 mt7915_ser_trigger_set, "%lld\n");

static int
mt7915_radar_trigger(void *data, u64 val)
{
	struct mt7915_dev *dev = data;

	return mt7915_mcu_rdd_cmd(dev, RDD_RADAR_EMULATE, 1, 0, 0);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_radar_trigger, NULL,
			 mt7915_radar_trigger, "%lld\n");

static int
mt7915_dbdc_set(void *data, u64 val)
{
	struct mt7915_dev *dev = data;

	if (val)
		mt7915_register_ext_phy(dev);
	else
		mt7915_unregister_ext_phy(dev);

	return 0;
}

static int
mt7915_dbdc_get(void *data, u64 *val)
{
	struct mt7915_dev *dev = data;

	*val = !!mt7915_ext_phy(dev);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_dbdc, mt7915_dbdc_get,
			 mt7915_dbdc_set, "%lld\n");

static int
mt7915_fw_debug_set(void *data, u64 val)
{
	struct mt7915_dev *dev = data;
	enum {
		DEBUG_TXCMD = 62,
		DEBUG_CMD_RPT_TX,
		DEBUG_CMD_RPT_TRIG,
		DEBUG_SPL,
		DEBUG_RPT_RX,
	} debug;

	dev->fw_debug = !!val;

	mt7915_mcu_fw_log_2_host(dev, dev->fw_debug ? 2 : 0);

	for (debug = DEBUG_TXCMD; debug <= DEBUG_RPT_RX; debug++)
		mt7915_mcu_fw_dbg_ctrl(dev, debug, dev->fw_debug);

	return 0;
}

static int
mt7915_fw_debug_get(void *data, u64 *val)
{
	struct mt7915_dev *dev = data;

	*val = dev->fw_debug;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug, mt7915_fw_debug_get,
			 mt7915_fw_debug_set, "%lld\n");

static void
mt7915_ampdu_stat_read_phy(struct mt7915_phy *phy,
			   struct seq_file *file)
{
	struct mt7915_dev *dev = file->private;
	bool ext_phy = phy != &dev->phy;
	int bound[15], range[4], i, n;

	if (!phy)
		return;

	/* Tx ampdu stat */
	for (i = 0; i < ARRAY_SIZE(range); i++)
		range[i] = mt76_rr(dev, MT_MIB_ARNG(ext_phy, i));

	for (i = 0; i < ARRAY_SIZE(bound); i++)
		bound[i] = MT_MIB_ARNCR_RANGE(range[i / 4], i) + 1;

	seq_printf(file, "\nPhy %d\n", ext_phy);

	seq_printf(file, "Length: %8d | ", bound[0]);
	for (i = 0; i < ARRAY_SIZE(bound) - 1; i++)
		seq_printf(file, "%3d -%3d | ",
			   bound[i] + 1, bound[i + 1]);

	seq_puts(file, "\nCount:  ");
	n = ext_phy ? ARRAY_SIZE(dev->mt76.aggr_stats) / 2 : 0;
	for (i = 0; i < ARRAY_SIZE(bound); i++)
		seq_printf(file, "%8d | ", dev->mt76.aggr_stats[i + n]);
	seq_puts(file, "\n");

	seq_printf(file, "BA miss count: %d\n", phy->mib.ba_miss_cnt);
}

static void
mt7915_txbf_stat_read_phy(struct mt7915_phy *phy, struct seq_file *s)
{
	struct mt7915_dev *dev = s->private;
	bool ext_phy = phy != &dev->phy;
	int cnt;

	if (!phy)
		return;

	/* Tx Beamformer monitor */
	seq_puts(s, "\nTx Beamformer applied PPDU counts: ");

	cnt = mt76_rr(dev, MT_ETBF_TX_APP_CNT(ext_phy));
	seq_printf(s, "iBF: %ld, eBF: %ld\n",
		   FIELD_GET(MT_ETBF_TX_IBF_CNT, cnt),
		   FIELD_GET(MT_ETBF_TX_EBF_CNT, cnt));

	/* Tx Beamformer Rx feedback monitor */
	seq_puts(s, "Tx Beamformer Rx feedback statistics: ");

	cnt = mt76_rr(dev, MT_ETBF_RX_FB_CNT(ext_phy));
	seq_printf(s, "All: %ld, HE: %ld, VHT: %ld, HT: %ld\n",
		   FIELD_GET(MT_ETBF_RX_FB_ALL, cnt),
		   FIELD_GET(MT_ETBF_RX_FB_HE, cnt),
		   FIELD_GET(MT_ETBF_RX_FB_VHT, cnt),
		   FIELD_GET(MT_ETBF_RX_FB_HT, cnt));

	/* Tx Beamformee Rx NDPA & Tx feedback report */
	cnt = mt76_rr(dev, MT_ETBF_TX_NDP_BFRP(ext_phy));
	seq_printf(s, "Tx Beamformee successful feedback frames: %ld\n",
		   FIELD_GET(MT_ETBF_TX_FB_CPL, cnt));
	seq_printf(s, "Tx Beamformee feedback triggered counts: %ld\n",
		   FIELD_GET(MT_ETBF_TX_FB_TRI, cnt));

	/* Tx SU & MU counters */
	cnt = mt76_rr(dev, MT_MIB_SDR34(ext_phy));
	seq_printf(s, "Tx multi-user Beamforming counts: %ld\n",
		   FIELD_GET(MT_MIB_MU_BF_TX_CNT, cnt));
	cnt = mt76_rr(dev, MT_MIB_DR8(ext_phy));
	seq_printf(s, "Tx multi-user MPDU counts: %d\n", cnt);
	cnt = mt76_rr(dev, MT_MIB_DR9(ext_phy));
	seq_printf(s, "Tx multi-user successful MPDU counts: %d\n", cnt);
	cnt = mt76_rr(dev, MT_MIB_DR11(ext_phy));
	seq_printf(s, "Tx single-user successful MPDU counts: %d\n", cnt);

	seq_puts(s, "\n");
}

static int
mt7915_tx_stats_read(struct seq_file *file, void *data)
{
	struct mt7915_dev *dev = file->private;
	int stat[8], i, n;

	mt7915_ampdu_stat_read_phy(&dev->phy, file);
	mt7915_txbf_stat_read_phy(&dev->phy, file);

	mt7915_ampdu_stat_read_phy(mt7915_ext_phy(dev), file);
	mt7915_txbf_stat_read_phy(mt7915_ext_phy(dev), file);

	/* Tx amsdu info */
	seq_puts(file, "Tx MSDU stat:\n");
	for (i = 0, n = 0; i < ARRAY_SIZE(stat); i++) {
		stat[i] = mt76_rr(dev,  MT_PLE_AMSDU_PACK_MSDU_CNT(i));
		n += stat[i];
	}

	for (i = 0; i < ARRAY_SIZE(stat); i++) {
		seq_printf(file, "AMSDU pack count of %d MSDU in TXD: 0x%x ",
			   i + 1, stat[i]);
		if (n != 0)
			seq_printf(file, "(%d%%)\n", stat[i] * 100 / n);
		else
			seq_puts(file, "\n");
	}

	return 0;
}

static int
mt7915_tx_stats_open(struct inode *inode, struct file *f)
{
	return single_open(f, mt7915_tx_stats_read, inode->i_private);
}

static const struct file_operations fops_tx_stats = {
	.open = mt7915_tx_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int mt7915_read_temperature(struct seq_file *s, void *data)
{
	struct mt7915_dev *dev = dev_get_drvdata(s->private);
	int temp;

	/* cpu */
	temp = mt7915_mcu_get_temperature(dev, 0);
	seq_printf(s, "Temperature: %d\n", temp);

	return 0;
}

static int
mt7915_queues_acq(struct seq_file *s, void *data)
{
	struct mt7915_dev *dev = dev_get_drvdata(s->private);
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
mt7915_queues_read(struct seq_file *s, void *data)
{
	struct mt7915_dev *dev = dev_get_drvdata(s->private);
	static const struct {
		char *queue;
		int id;
	} queue_map[] = {
		{ "WFDMA0", MT_TXQ_BE },
		{ "MCUWM", MT_TXQ_MCU },
		{ "MCUWA", MT_TXQ_MCU_WA },
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

static void
mt7915_puts_rate_txpower(struct seq_file *s, s8 *delta,
			 s8 txpower_cur, int band)
{
	static const char * const sku_group_name[] = {
		"CCK", "OFDM", "HT20", "HT40",
		"VHT20", "VHT40", "VHT80", "VHT160",
		"RU26", "RU52", "RU106", "RU242/SU20",
		"RU484/SU40", "RU996/SU80", "RU2x996/SU160"
	};
	s8 txpower[MT7915_SKU_RATE_NUM];
	int i, idx = 0;

	for (i = 0; i < MT7915_SKU_RATE_NUM; i++)
		txpower[i] = DIV_ROUND_UP(txpower_cur + delta[i], 2);

	for (i = 0; i < MAX_SKU_RATE_GROUP_NUM; i++) {
		const struct sku_group *sku = &mt7915_sku_groups[i];
		u32 offset = sku->offset[band];

		if (!offset) {
			idx += sku->len;
			continue;
		}

		mt76_seq_puts_array(s, sku_group_name[i],
				    txpower + idx, sku->len);
		idx += sku->len;
	}
}

static int
mt7915_read_rate_txpower(struct seq_file *s, void *data)
{
	struct mt7915_dev *dev = dev_get_drvdata(s->private);
	struct mt76_phy *mphy = &dev->mphy;
	enum nl80211_band band = mphy->chandef.chan->band;
	s8 *delta = dev->rate_power[band];
	s8 txpower_base = mphy->txpower_cur - delta[MT7915_SKU_MAX_DELTA_IDX];

	seq_puts(s, "Band 0:\n");
	mt7915_puts_rate_txpower(s, delta, txpower_base, band);

	if (dev->mt76.phy2) {
		mphy = dev->mt76.phy2;
		band = mphy->chandef.chan->band;
		delta = dev->rate_power[band];
		txpower_base = mphy->txpower_cur -
			       delta[MT7915_SKU_MAX_DELTA_IDX];

		seq_puts(s, "Band 1:\n");
		mt7915_puts_rate_txpower(s, delta, txpower_base, band);
	}

	return 0;
}

int mt7915_init_debugfs(struct mt7915_dev *dev)
{
	struct dentry *dir;

	dir = mt76_register_debugfs(&dev->mt76);
	if (!dir)
		return -ENOMEM;

	debugfs_create_devm_seqfile(dev->mt76.dev, "queues", dir,
				    mt7915_queues_read);
	debugfs_create_devm_seqfile(dev->mt76.dev, "acq", dir,
				    mt7915_queues_acq);
	debugfs_create_file("tx_stats", 0400, dir, dev, &fops_tx_stats);
	debugfs_create_file("dbdc", 0600, dir, dev, &fops_dbdc);
	debugfs_create_file("fw_debug", 0600, dir, dev, &fops_fw_debug);
	debugfs_create_u32("dfs_hw_pattern", 0400, dir, &dev->hw_pattern);
	/* test knobs */
	debugfs_create_file("radar_trigger", 0200, dir, dev,
			    &fops_radar_trigger);
	debugfs_create_file("ser_trigger", 0200, dir, dev, &fops_ser_trigger);
	debugfs_create_devm_seqfile(dev->mt76.dev, "temperature", dir,
				    mt7915_read_temperature);
	debugfs_create_devm_seqfile(dev->mt76.dev, "txpower_sku", dir,
				    mt7915_read_rate_txpower);

	return 0;
}

#ifdef CONFIG_MAC80211_DEBUGFS
/** per-station debugfs **/

/* usage: <tx mode> <ldpc> <stbc> <bw> <gi> <nss> <mcs> */
static int mt7915_sta_fixed_rate_set(void *data, u64 rate)
{
	struct ieee80211_sta *sta = data;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;

	return mt7915_mcu_set_fixed_rate(msta->vif->phy->dev, sta, rate);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fixed_rate, NULL,
			 mt7915_sta_fixed_rate_set, "%llx\n");

static int
mt7915_sta_stats_read(struct seq_file *s, void *data)
{
	struct ieee80211_sta *sta = s->private;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_sta_stats *stats = &msta->stats;
	struct rate_info *rate = &stats->prob_rate;
	static const char * const bw[] = {
		"BW20", "BW5", "BW10", "BW40",
		"BW80", "BW160", "BW_HE_RU"
	};

	if (!rate->legacy && !rate->flags)
		return 0;

	seq_puts(s, "Probing rate - ");
	if (rate->flags & RATE_INFO_FLAGS_MCS)
		seq_puts(s, "HT ");
	else if (rate->flags & RATE_INFO_FLAGS_VHT_MCS)
		seq_puts(s, "VHT ");
	else if (rate->flags & RATE_INFO_FLAGS_HE_MCS)
		seq_puts(s, "HE ");
	else
		seq_printf(s, "Bitrate %d\n", rate->legacy);

	if (rate->flags) {
		seq_printf(s, "%s NSS%d MCS%d ",
			   bw[rate->bw], rate->nss, rate->mcs);

		if (rate->flags & RATE_INFO_FLAGS_SHORT_GI)
			seq_puts(s, "SGI ");
		else if (rate->he_gi)
			seq_puts(s, "HE GI ");

		if (rate->he_dcm)
			seq_puts(s, "DCM ");
	}

	seq_printf(s, "\nPPDU PER: %ld.%1ld%%\n",
		   stats->per / 10, stats->per % 10);

	return 0;
}

static int
mt7915_sta_stats_open(struct inode *inode, struct file *f)
{
	return single_open(f, mt7915_sta_stats_read, inode->i_private);
}

static const struct file_operations fops_sta_stats = {
	.open = mt7915_sta_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void mt7915_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir)
{
	debugfs_create_file("fixed_rate", 0600, dir, sta, &fops_fixed_rate);
	debugfs_create_file("stats", 0400, dir, sta, &fops_sta_stats);
}
#endif
