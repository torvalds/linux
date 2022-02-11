// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7915.h"
#include "mac.h"
#include "mcu.h"
#include "testmode.h"

enum {
	TM_CHANGED_TXPOWER,
	TM_CHANGED_FREQ_OFFSET,

	/* must be last */
	NUM_TM_CHANGED
};

static const u8 tm_change_map[] = {
	[TM_CHANGED_TXPOWER] = MT76_TM_ATTR_TX_POWER,
	[TM_CHANGED_FREQ_OFFSET] = MT76_TM_ATTR_FREQ_OFFSET,
};

struct reg_band {
	u32 band[2];
};

#define REG_BAND(_list, _reg) \
		{ _list.band[0] = MT_##_reg(0);	\
		  _list.band[1] = MT_##_reg(1); }
#define REG_BAND_IDX(_list, _reg, _idx) \
		{ _list.band[0] = MT_##_reg(0, _idx);	\
		  _list.band[1] = MT_##_reg(1, _idx); }

#define TM_REG_MAX_ID	17
static struct reg_band reg_backup_list[TM_REG_MAX_ID];


static int
mt7915_tm_set_tx_power(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct cfg80211_chan_def *chandef = &mphy->chandef;
	int freq = chandef->center_freq1;
	int ret;
	struct {
		u8 format_id;
		u8 dbdc_idx;
		s8 tx_power;
		u8 ant_idx;	/* Only 0 is valid */
		u8 center_chan;
		u8 rsv[3];
	} __packed req = {
		.format_id = 0xf,
		.dbdc_idx = phy != &dev->phy,
		.center_chan = ieee80211_frequency_to_channel(freq),
	};
	u8 *tx_power = NULL;

	if (phy->mt76->test.state != MT76_TM_STATE_OFF)
		tx_power = phy->mt76->test.tx_power;

	/* Tx power of the other antennas are the same as antenna 0 */
	if (tx_power && tx_power[0])
		req.tx_power = tx_power[0];

	ret = mt76_mcu_send_msg(&dev->mt76,
				MCU_EXT_CMD(TX_POWER_FEATURE_CTRL),
				&req, sizeof(req), false);

	return ret;
}

static int
mt7915_tm_set_freq_offset(struct mt7915_phy *phy, bool en, u32 val)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_tm_cmd req = {
		.testmode_en = en,
		.param_idx = MCU_ATE_SET_FREQ_OFFSET,
		.param.freq.band = phy != &dev->phy,
		.param.freq.freq_offset = cpu_to_le32(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(ATE_CTRL), &req,
				 sizeof(req), false);
}

static int
mt7915_tm_mode_ctrl(struct mt7915_dev *dev, bool enable)
{
	struct {
		u8 format_id;
		bool enable;
		u8 rsv[2];
	} __packed req = {
		.format_id = 0x6,
		.enable = enable,
	};

	return mt76_mcu_send_msg(&dev->mt76,
				 MCU_EXT_CMD(TX_POWER_FEATURE_CTRL),
				 &req, sizeof(req), false);
}

static int
mt7915_tm_set_trx(struct mt7915_phy *phy, int type, bool en)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_tm_cmd req = {
		.testmode_en = 1,
		.param_idx = MCU_ATE_SET_TRX,
		.param.trx.type = type,
		.param.trx.enable = en,
		.param.trx.band = phy != &dev->phy,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(ATE_CTRL), &req,
				 sizeof(req), false);
}

static int
mt7915_tm_clean_hwq(struct mt7915_phy *phy, u8 wcid)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_tm_cmd req = {
		.testmode_en = 1,
		.param_idx = MCU_ATE_CLEAN_TXQUEUE,
		.param.clean.wcid = wcid,
		.param.clean.band = phy != &dev->phy,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(ATE_CTRL), &req,
				 sizeof(req), false);
}

static int
mt7915_tm_set_slot_time(struct mt7915_phy *phy, u8 slot_time, u8 sifs)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_tm_cmd req = {
		.testmode_en = !(phy->mt76->test.state == MT76_TM_STATE_OFF),
		.param_idx = MCU_ATE_SET_SLOT_TIME,
		.param.slot.slot_time = slot_time,
		.param.slot.sifs = sifs,
		.param.slot.rifs = 2,
		.param.slot.eifs = cpu_to_le16(60),
		.param.slot.band = phy != &dev->phy,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(ATE_CTRL), &req,
				 sizeof(req), false);
}

static int
mt7915_tm_set_tam_arb(struct mt7915_phy *phy, bool enable, bool mu)
{
	struct mt7915_dev *dev = phy->dev;
	u32 op_mode;

	if (!enable)
		op_mode = TAM_ARB_OP_MODE_NORMAL;
	else if (mu)
		op_mode = TAM_ARB_OP_MODE_TEST;
	else
		op_mode = TAM_ARB_OP_MODE_FORCE_SU;

	return mt7915_mcu_set_muru_ctrl(dev, MURU_SET_ARB_OP_MODE, op_mode);
}

static int
mt7915_tm_set_wmm_qid(struct mt7915_dev *dev, u8 qid, u8 aifs, u8 cw_min,
		      u16 cw_max, u16 txop)
{
	struct mt7915_mcu_tx req = { .total = 1 };
	struct edca *e = &req.edca[0];

	e->queue = qid;
	e->set = WMM_PARAM_SET;

	e->aifs = aifs;
	e->cw_min = cw_min;
	e->cw_max = cpu_to_le16(cw_max);
	e->txop = cpu_to_le16(txop);

	return mt7915_mcu_update_edca(dev, &req);
}

static int
mt7915_tm_set_ipg_params(struct mt7915_phy *phy, u32 ipg, u8 mode)
{
#define TM_DEFAULT_SIFS	10
#define TM_MAX_SIFS	127
#define TM_MAX_AIFSN	0xf
#define TM_MIN_AIFSN	0x1
#define BBP_PROC_TIME	1500
	struct mt7915_dev *dev = phy->dev;
	u8 sig_ext = (mode == MT76_TM_TX_MODE_CCK) ? 0 : 6;
	u8 slot_time = 9, sifs = TM_DEFAULT_SIFS;
	u8 aifsn = TM_MIN_AIFSN;
	u32 i2t_time, tr2t_time, txv_time;
	bool ext_phy = phy != &dev->phy;
	u16 cw = 0;

	if (ipg < sig_ext + slot_time + sifs)
		ipg = 0;

	if (!ipg)
		goto done;

	ipg -= sig_ext;

	if (ipg <= (TM_MAX_SIFS + slot_time)) {
		sifs = ipg - slot_time;
	} else {
		u32 val = (ipg + slot_time) / slot_time;

		while (val >>= 1)
			cw++;

		if (cw > 16)
			cw = 16;

		ipg -= ((1 << cw) - 1) * slot_time;

		aifsn = ipg / slot_time;
		if (aifsn > TM_MAX_AIFSN)
			aifsn = TM_MAX_AIFSN;

		ipg -= aifsn * slot_time;

		if (ipg > TM_DEFAULT_SIFS) {
			if (ipg < TM_MAX_SIFS)
				sifs = ipg;
			else
				sifs = TM_MAX_SIFS;
		}
	}
done:
	txv_time = mt76_get_field(dev, MT_TMAC_ATCR(ext_phy),
				  MT_TMAC_ATCR_TXV_TOUT);
	txv_time *= 50;	/* normal clock time */

	i2t_time = (slot_time * 1000 - txv_time - BBP_PROC_TIME) / 50;
	tr2t_time = (sifs * 1000 - txv_time - BBP_PROC_TIME) / 50;

	mt76_set(dev, MT_TMAC_TRCR0(ext_phy),
		 FIELD_PREP(MT_TMAC_TRCR0_TR2T_CHK, tr2t_time) |
		 FIELD_PREP(MT_TMAC_TRCR0_I2T_CHK, i2t_time));

	mt7915_tm_set_slot_time(phy, slot_time, sifs);

	return mt7915_tm_set_wmm_qid(dev,
				     mt76_connac_lmac_mapping(IEEE80211_AC_BE),
				     aifsn, cw, cw, 0);
}

static int
mt7915_tm_set_tx_len(struct mt7915_phy *phy, u32 tx_time)
{
	struct mt76_phy *mphy = phy->mt76;
	struct mt76_testmode_data *td = &mphy->test;
	struct ieee80211_supported_band *sband;
	struct rate_info rate = {};
	u16 flags = 0, tx_len;
	u32 bitrate;
	int ret;

	if (!tx_time)
		return 0;

	rate.mcs = td->tx_rate_idx;
	rate.nss = td->tx_rate_nss;

	switch (td->tx_rate_mode) {
	case MT76_TM_TX_MODE_CCK:
	case MT76_TM_TX_MODE_OFDM:
		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &mphy->sband_5g.sband;
		else
			sband = &mphy->sband_2g.sband;

		rate.legacy = sband->bitrates[rate.mcs].bitrate;
		break;
	case MT76_TM_TX_MODE_HT:
		rate.mcs += rate.nss * 8;
		flags |= RATE_INFO_FLAGS_MCS;

		if (td->tx_rate_sgi)
			flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT76_TM_TX_MODE_VHT:
		flags |= RATE_INFO_FLAGS_VHT_MCS;

		if (td->tx_rate_sgi)
			flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT76_TM_TX_MODE_HE_SU:
	case MT76_TM_TX_MODE_HE_EXT_SU:
	case MT76_TM_TX_MODE_HE_TB:
	case MT76_TM_TX_MODE_HE_MU:
		rate.he_gi = td->tx_rate_sgi;
		flags |= RATE_INFO_FLAGS_HE_MCS;
		break;
	default:
		break;
	}
	rate.flags = flags;

	switch (mphy->chandef.width) {
	case NL80211_CHAN_WIDTH_160:
	case NL80211_CHAN_WIDTH_80P80:
		rate.bw = RATE_INFO_BW_160;
		break;
	case NL80211_CHAN_WIDTH_80:
		rate.bw = RATE_INFO_BW_80;
		break;
	case NL80211_CHAN_WIDTH_40:
		rate.bw = RATE_INFO_BW_40;
		break;
	default:
		rate.bw = RATE_INFO_BW_20;
		break;
	}

	bitrate = cfg80211_calculate_bitrate(&rate);
	tx_len = bitrate * tx_time / 10 / 8;

	ret = mt76_testmode_alloc_skb(phy->mt76, tx_len);
	if (ret)
		return ret;

	return 0;
}

static void
mt7915_tm_reg_backup_restore(struct mt7915_phy *phy)
{
	int n_regs = ARRAY_SIZE(reg_backup_list);
	struct mt7915_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	u32 *b = phy->test.reg_backup;
	int i;

	REG_BAND_IDX(reg_backup_list[0], AGG_PCR0, 0);
	REG_BAND_IDX(reg_backup_list[1], AGG_PCR0, 1);
	REG_BAND_IDX(reg_backup_list[2], AGG_AWSCR0, 0);
	REG_BAND_IDX(reg_backup_list[3], AGG_AWSCR0, 1);
	REG_BAND_IDX(reg_backup_list[4], AGG_AWSCR0, 2);
	REG_BAND_IDX(reg_backup_list[5], AGG_AWSCR0, 3);
	REG_BAND(reg_backup_list[6], AGG_MRCR);
	REG_BAND(reg_backup_list[7], TMAC_TFCR0);
	REG_BAND(reg_backup_list[8], TMAC_TCR0);
	REG_BAND(reg_backup_list[9], AGG_ATCR1);
	REG_BAND(reg_backup_list[10], AGG_ATCR3);
	REG_BAND(reg_backup_list[11], TMAC_TRCR0);
	REG_BAND(reg_backup_list[12], TMAC_ICR0);
	REG_BAND_IDX(reg_backup_list[13], ARB_DRNGR0, 0);
	REG_BAND_IDX(reg_backup_list[14], ARB_DRNGR0, 1);
	REG_BAND(reg_backup_list[15], WF_RFCR);
	REG_BAND(reg_backup_list[16], WF_RFCR1);

	if (phy->mt76->test.state == MT76_TM_STATE_OFF) {
		for (i = 0; i < n_regs; i++)
			mt76_wr(dev, reg_backup_list[i].band[ext_phy], b[i]);
		return;
	}

	if (!b) {
		b = devm_kzalloc(dev->mt76.dev, 4 * n_regs, GFP_KERNEL);
		if (!b)
			return;

		phy->test.reg_backup = b;
		for (i = 0; i < n_regs; i++)
			b[i] = mt76_rr(dev, reg_backup_list[i].band[ext_phy]);
	}

	mt76_clear(dev, MT_AGG_PCR0(ext_phy, 0), MT_AGG_PCR0_MM_PROT |
		   MT_AGG_PCR0_GF_PROT | MT_AGG_PCR0_ERP_PROT |
		   MT_AGG_PCR0_VHT_PROT | MT_AGG_PCR0_BW20_PROT |
		   MT_AGG_PCR0_BW40_PROT | MT_AGG_PCR0_BW80_PROT);
	mt76_set(dev, MT_AGG_PCR0(ext_phy, 0), MT_AGG_PCR0_PTA_WIN_DIS);

	mt76_wr(dev, MT_AGG_PCR0(ext_phy, 1), MT_AGG_PCR1_RTS0_NUM_THRES |
		MT_AGG_PCR1_RTS0_LEN_THRES);

	mt76_clear(dev, MT_AGG_MRCR(ext_phy), MT_AGG_MRCR_BAR_CNT_LIMIT |
		   MT_AGG_MRCR_LAST_RTS_CTS_RN | MT_AGG_MRCR_RTS_FAIL_LIMIT |
		   MT_AGG_MRCR_TXCMD_RTS_FAIL_LIMIT);

	mt76_rmw(dev, MT_AGG_MRCR(ext_phy), MT_AGG_MRCR_RTS_FAIL_LIMIT |
		 MT_AGG_MRCR_TXCMD_RTS_FAIL_LIMIT,
		 FIELD_PREP(MT_AGG_MRCR_RTS_FAIL_LIMIT, 1) |
		 FIELD_PREP(MT_AGG_MRCR_TXCMD_RTS_FAIL_LIMIT, 1));

	mt76_wr(dev, MT_TMAC_TFCR0(ext_phy), 0);
	mt76_clear(dev, MT_TMAC_TCR0(ext_phy), MT_TMAC_TCR0_TBTT_STOP_CTRL);

	/* config rx filter for testmode rx */
	mt76_wr(dev, MT_WF_RFCR(ext_phy), 0xcf70a);
	mt76_wr(dev, MT_WF_RFCR1(ext_phy), 0);
}

static void
mt7915_tm_init(struct mt7915_phy *phy, bool en)
{
	struct mt7915_dev *dev = phy->dev;

	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	mt7915_mcu_set_sku_en(phy, !en);

	mt7915_tm_mode_ctrl(dev, en);
	mt7915_tm_reg_backup_restore(phy);
	mt7915_tm_set_trx(phy, TM_MAC_TXRX, !en);

	mt7915_mcu_add_bss_info(phy, phy->monitor_vif, en);
	mt7915_mcu_add_sta(dev, phy->monitor_vif, NULL, en);

	if (!en)
		mt7915_tm_set_tam_arb(phy, en, 0);
}

static void
mt7915_tm_update_channel(struct mt7915_phy *phy)
{
	mutex_unlock(&phy->dev->mt76.mutex);
	mt7915_set_channel(phy);
	mutex_lock(&phy->dev->mt76.mutex);

	mt7915_mcu_set_chan_info(phy, MCU_EXT_CMD(SET_RX_PATH));
}

static void
mt7915_tm_set_tx_frames(struct mt7915_phy *phy, bool en)
{
	static const u8 spe_idx_map[] = {0, 0, 1, 0, 3, 2, 4, 0,
					 9, 8, 6, 10, 16, 12, 18, 0};
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct mt7915_dev *dev = phy->dev;
	struct ieee80211_tx_info *info;
	u8 duty_cycle = td->tx_duty_cycle;
	u32 tx_time = td->tx_time;
	u32 ipg = td->tx_ipg;

	mt7915_tm_set_trx(phy, TM_MAC_RX_RXV, false);
	mt7915_tm_clean_hwq(phy, dev->mt76.global_wcid.idx);

	if (en) {
		mt7915_tm_update_channel(phy);

		if (td->tx_spe_idx) {
			phy->test.spe_idx = td->tx_spe_idx;
		} else {
			u8 tx_ant = td->tx_antenna_mask;

			if (phy != &dev->phy)
				tx_ant >>= 2;
			phy->test.spe_idx = spe_idx_map[tx_ant];
		}
	}

	mt7915_tm_set_tam_arb(phy, en,
			      td->tx_rate_mode == MT76_TM_TX_MODE_HE_MU);

	/* if all three params are set, duty_cycle will be ignored */
	if (duty_cycle && tx_time && !ipg) {
		ipg = tx_time * 100 / duty_cycle - tx_time;
	} else if (duty_cycle && !tx_time && ipg) {
		if (duty_cycle < 100)
			tx_time = duty_cycle * ipg / (100 - duty_cycle);
	}

	mt7915_tm_set_ipg_params(phy, ipg, td->tx_rate_mode);
	mt7915_tm_set_tx_len(phy, tx_time);

	if (ipg)
		td->tx_queued_limit = MT76_TM_TIMEOUT * 1000000 / ipg / 2;

	if (!en || !td->tx_skb)
		return;

	info = IEEE80211_SKB_CB(td->tx_skb);
	info->control.vif = phy->monitor_vif;

	mt7915_tm_set_trx(phy, TM_MAC_TX, en);
}

static void
mt7915_tm_set_rx_frames(struct mt7915_phy *phy, bool en)
{
	mt7915_tm_set_trx(phy, TM_MAC_RX_RXV, false);

	if (en) {
		struct mt7915_dev *dev = phy->dev;

		mt7915_tm_update_channel(phy);

		/* read-clear */
		mt76_rr(dev, MT_MIB_SDR3(phy != &dev->phy));
		mt7915_tm_set_trx(phy, TM_MAC_RX_RXV, en);
	}
}

static int
mt7915_tm_rf_switch_mode(struct mt7915_dev *dev, u32 oper)
{
	struct mt7915_tm_rf_test req = {
		.op.op_mode = cpu_to_le32(oper),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RF_TEST), &req,
				 sizeof(req), true);
}

static int
mt7915_tm_set_tx_cont(struct mt7915_phy *phy, bool en)
{
#define TX_CONT_START	0x05
#define TX_CONT_STOP	0x06
	struct mt7915_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq1 = ieee80211_frequency_to_channel(chandef->center_freq1);
	struct mt76_testmode_data *td = &phy->mt76->test;
	u32 func_idx = en ? TX_CONT_START : TX_CONT_STOP;
	u8 rate_idx = td->tx_rate_idx, mode;
	u16 rateval;
	struct mt7915_tm_rf_test req = {
		.action = 1,
		.icap_len = 120,
		.op.rf.func_idx = cpu_to_le32(func_idx),
	};
	struct tm_tx_cont *tx_cont = &req.op.rf.param.tx_cont;

	tx_cont->control_ch = chandef->chan->hw_value;
	tx_cont->center_ch = freq1;
	tx_cont->tx_ant = td->tx_antenna_mask;
	tx_cont->band = phy != &dev->phy;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_40:
		tx_cont->bw = CMD_CBW_40MHZ;
		break;
	case NL80211_CHAN_WIDTH_80:
		tx_cont->bw = CMD_CBW_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		tx_cont->bw = CMD_CBW_8080MHZ;
		break;
	case NL80211_CHAN_WIDTH_160:
		tx_cont->bw = CMD_CBW_160MHZ;
		break;
	case NL80211_CHAN_WIDTH_5:
		tx_cont->bw = CMD_CBW_5MHZ;
		break;
	case NL80211_CHAN_WIDTH_10:
		tx_cont->bw = CMD_CBW_10MHZ;
		break;
	case NL80211_CHAN_WIDTH_20:
		tx_cont->bw = CMD_CBW_20MHZ;
		break;
	case NL80211_CHAN_WIDTH_20_NOHT:
		tx_cont->bw = CMD_CBW_20MHZ;
		break;
	default:
		return -EINVAL;
	}

	if (!en) {
		req.op.rf.param.func_data = cpu_to_le32(phy != &dev->phy);
		goto out;
	}

	if (td->tx_rate_mode <= MT76_TM_TX_MODE_OFDM) {
		struct ieee80211_supported_band *sband;
		u8 idx = rate_idx;

		if (chandef->chan->band == NL80211_BAND_5GHZ)
			sband = &phy->mt76->sband_5g.sband;
		else
			sband = &phy->mt76->sband_2g.sband;

		if (td->tx_rate_mode == MT76_TM_TX_MODE_OFDM)
			idx += 4;
		rate_idx = sband->bitrates[idx].hw_value & 0xff;
	}

	switch (td->tx_rate_mode) {
	case MT76_TM_TX_MODE_CCK:
		mode = MT_PHY_TYPE_CCK;
		break;
	case MT76_TM_TX_MODE_OFDM:
		mode = MT_PHY_TYPE_OFDM;
		break;
	case MT76_TM_TX_MODE_HT:
		mode = MT_PHY_TYPE_HT;
		break;
	case MT76_TM_TX_MODE_VHT:
		mode = MT_PHY_TYPE_VHT;
		break;
	case MT76_TM_TX_MODE_HE_SU:
		mode = MT_PHY_TYPE_HE_SU;
		break;
	case MT76_TM_TX_MODE_HE_EXT_SU:
		mode = MT_PHY_TYPE_HE_EXT_SU;
		break;
	case MT76_TM_TX_MODE_HE_TB:
		mode = MT_PHY_TYPE_HE_TB;
		break;
	case MT76_TM_TX_MODE_HE_MU:
		mode = MT_PHY_TYPE_HE_MU;
		break;
	default:
		return -EINVAL;
	}

	rateval =  mode << 6 | rate_idx;
	tx_cont->rateval = cpu_to_le16(rateval);

out:
	if (!en) {
		int ret;

		ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RF_TEST), &req,
					sizeof(req), true);
		if (ret)
			return ret;

		return mt7915_tm_rf_switch_mode(dev, RF_OPER_NORMAL);
	}

	mt7915_tm_rf_switch_mode(dev, RF_OPER_RF_TEST);
	mt7915_tm_update_channel(phy);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RF_TEST), &req,
				 sizeof(req), true);
}

static void
mt7915_tm_update_params(struct mt7915_phy *phy, u32 changed)
{
	struct mt76_testmode_data *td = &phy->mt76->test;
	bool en = phy->mt76->test.state != MT76_TM_STATE_OFF;

	if (changed & BIT(TM_CHANGED_FREQ_OFFSET))
		mt7915_tm_set_freq_offset(phy, en, en ? td->freq_offset : 0);
	if (changed & BIT(TM_CHANGED_TXPOWER))
		mt7915_tm_set_tx_power(phy);
}

static int
mt7915_tm_set_state(struct mt76_phy *mphy, enum mt76_testmode_state state)
{
	struct mt76_testmode_data *td = &mphy->test;
	struct mt7915_phy *phy = mphy->priv;
	enum mt76_testmode_state prev_state = td->state;

	mphy->test.state = state;

	if (prev_state == MT76_TM_STATE_TX_FRAMES ||
	    state == MT76_TM_STATE_TX_FRAMES)
		mt7915_tm_set_tx_frames(phy, state == MT76_TM_STATE_TX_FRAMES);
	else if (prev_state == MT76_TM_STATE_RX_FRAMES ||
		 state == MT76_TM_STATE_RX_FRAMES)
		mt7915_tm_set_rx_frames(phy, state == MT76_TM_STATE_RX_FRAMES);
	else if (prev_state == MT76_TM_STATE_TX_CONT ||
		 state == MT76_TM_STATE_TX_CONT)
		mt7915_tm_set_tx_cont(phy, state == MT76_TM_STATE_TX_CONT);
	else if (prev_state == MT76_TM_STATE_OFF ||
		 state == MT76_TM_STATE_OFF)
		mt7915_tm_init(phy, !(state == MT76_TM_STATE_OFF));

	if ((state == MT76_TM_STATE_IDLE &&
	     prev_state == MT76_TM_STATE_OFF) ||
	    (state == MT76_TM_STATE_OFF &&
	     prev_state == MT76_TM_STATE_IDLE)) {
		u32 changed = 0;
		int i;

		for (i = 0; i < ARRAY_SIZE(tm_change_map); i++) {
			u16 cur = tm_change_map[i];

			if (td->param_set[cur / 32] & BIT(cur % 32))
				changed |= BIT(i);
		}

		mt7915_tm_update_params(phy, changed);
	}

	return 0;
}

static int
mt7915_tm_set_params(struct mt76_phy *mphy, struct nlattr **tb,
		     enum mt76_testmode_state new_state)
{
	struct mt76_testmode_data *td = &mphy->test;
	struct mt7915_phy *phy = mphy->priv;
	u32 changed = 0;
	int i;

	BUILD_BUG_ON(NUM_TM_CHANGED >= 32);

	if (new_state == MT76_TM_STATE_OFF ||
	    td->state == MT76_TM_STATE_OFF)
		return 0;

	if (td->tx_antenna_mask & ~mphy->chainmask)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(tm_change_map); i++) {
		if (tb[tm_change_map[i]])
			changed |= BIT(i);
	}

	mt7915_tm_update_params(phy, changed);

	return 0;
}

static int
mt7915_tm_dump_stats(struct mt76_phy *mphy, struct sk_buff *msg)
{
	struct mt7915_phy *phy = mphy->priv;
	struct mt7915_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	enum mt76_rxq_id q;
	void *rx, *rssi;
	u16 fcs_err;
	int i;
	u32 cnt;

	rx = nla_nest_start(msg, MT76_TM_STATS_ATTR_LAST_RX);
	if (!rx)
		return -ENOMEM;

	if (nla_put_s32(msg, MT76_TM_RX_ATTR_FREQ_OFFSET, phy->test.last_freq_offset))
		return -ENOMEM;

	rssi = nla_nest_start(msg, MT76_TM_RX_ATTR_RCPI);
	if (!rssi)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(phy->test.last_rcpi); i++)
		if (nla_put_u8(msg, i, phy->test.last_rcpi[i]))
			return -ENOMEM;

	nla_nest_end(msg, rssi);

	rssi = nla_nest_start(msg, MT76_TM_RX_ATTR_IB_RSSI);
	if (!rssi)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(phy->test.last_ib_rssi); i++)
		if (nla_put_s8(msg, i, phy->test.last_ib_rssi[i]))
			return -ENOMEM;

	nla_nest_end(msg, rssi);

	rssi = nla_nest_start(msg, MT76_TM_RX_ATTR_WB_RSSI);
	if (!rssi)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(phy->test.last_wb_rssi); i++)
		if (nla_put_s8(msg, i, phy->test.last_wb_rssi[i]))
			return -ENOMEM;

	nla_nest_end(msg, rssi);

	if (nla_put_u8(msg, MT76_TM_RX_ATTR_SNR, phy->test.last_snr))
		return -ENOMEM;

	nla_nest_end(msg, rx);

	cnt = mt76_rr(dev, MT_MIB_SDR3(ext_phy));
	fcs_err = is_mt7915(&dev->mt76) ? FIELD_GET(MT_MIB_SDR3_FCS_ERR_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR3_FCS_ERR_MASK_MT7916, cnt);

	q = ext_phy ? MT_RXQ_EXT : MT_RXQ_MAIN;
	mphy->test.rx_stats.packets[q] += fcs_err;
	mphy->test.rx_stats.fcs_error[q] += fcs_err;

	return 0;
}

const struct mt76_testmode_ops mt7915_testmode_ops = {
	.set_state = mt7915_tm_set_state,
	.set_params = mt7915_tm_set_params,
	.dump_stats = mt7915_tm_dump_stats,
};
