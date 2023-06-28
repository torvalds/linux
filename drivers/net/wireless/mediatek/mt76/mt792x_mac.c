// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc. */

#include <linux/module.h>

#include "mt792x.h"
#include "mt792x_regs.h"

void mt792x_mac_work(struct work_struct *work)
{
	struct mt792x_phy *phy;
	struct mt76_phy *mphy;

	mphy = (struct mt76_phy *)container_of(work, struct mt76_phy,
					       mac_work.work);
	phy = mphy->priv;

	mt792x_mutex_acquire(phy->dev);

	mt76_update_survey(mphy);
	if (++mphy->mac_work_count == 2) {
		mphy->mac_work_count = 0;

		mt792x_mac_update_mib_stats(phy);
	}

	mt792x_mutex_release(phy->dev);

	mt76_tx_status_check(mphy->dev, false);
	ieee80211_queue_delayed_work(phy->mt76->hw, &mphy->mac_work,
				     MT792x_WATCHDOG_TIME);
}
EXPORT_SYMBOL_GPL(mt792x_mac_work);

void mt792x_mac_set_timeing(struct mt792x_phy *phy)
{
	s16 coverage_class = phy->coverage_class;
	struct mt792x_dev *dev = phy->dev;
	u32 val, reg_offset;
	u32 cck = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 231) |
		  FIELD_PREP(MT_TIMEOUT_VAL_CCA, 48);
	u32 ofdm = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 60) |
		   FIELD_PREP(MT_TIMEOUT_VAL_CCA, 28);
	bool is_2ghz = phy->mt76->chandef.chan->band == NL80211_BAND_2GHZ;
	int sifs = is_2ghz ? 10 : 16, offset;

	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	mt76_set(dev, MT_ARB_SCR(0),
		 MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
	udelay(1);

	offset = 3 * coverage_class;
	reg_offset = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, offset) |
		     FIELD_PREP(MT_TIMEOUT_VAL_CCA, offset);

	mt76_wr(dev, MT_TMAC_CDTR(0), cck + reg_offset);
	mt76_wr(dev, MT_TMAC_ODTR(0), ofdm + reg_offset);
	mt76_wr(dev, MT_TMAC_ICR0(0),
		FIELD_PREP(MT_IFS_EIFS, 360) |
		FIELD_PREP(MT_IFS_RIFS, 2) |
		FIELD_PREP(MT_IFS_SIFS, sifs) |
		FIELD_PREP(MT_IFS_SLOT, phy->slottime));

	if (phy->slottime < 20 || !is_2ghz)
		val = MT792x_CFEND_RATE_DEFAULT;
	else
		val = MT792x_CFEND_RATE_11B;

	mt76_rmw_field(dev, MT_AGG_ACR0(0), MT_AGG_ACR_CFEND_RATE, val);
	mt76_clear(dev, MT_ARB_SCR(0),
		   MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
}
EXPORT_SYMBOL_GPL(mt792x_mac_set_timeing);

void mt792x_mac_update_mib_stats(struct mt792x_phy *phy)
{
	struct mt76_mib_stats *mib = &phy->mib;
	struct mt792x_dev *dev = phy->dev;
	int i, aggr0 = 0, aggr1;
	u32 val;

	mib->fcs_err_cnt += mt76_get_field(dev, MT_MIB_SDR3(0),
					   MT_MIB_SDR3_FCS_ERR_MASK);
	mib->ack_fail_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR3(0),
					    MT_MIB_ACK_FAIL_COUNT_MASK);
	mib->ba_miss_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR2(0),
					   MT_MIB_BA_FAIL_COUNT_MASK);
	mib->rts_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR0(0),
				       MT_MIB_RTS_COUNT_MASK);
	mib->rts_retries_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR1(0),
					       MT_MIB_RTS_FAIL_COUNT_MASK);

	mib->tx_ampdu_cnt += mt76_rr(dev, MT_MIB_SDR12(0));
	mib->tx_mpdu_attempts_cnt += mt76_rr(dev, MT_MIB_SDR14(0));
	mib->tx_mpdu_success_cnt += mt76_rr(dev, MT_MIB_SDR15(0));

	val = mt76_rr(dev, MT_MIB_SDR32(0));
	mib->tx_pkt_ebf_cnt += FIELD_GET(MT_MIB_SDR9_EBF_CNT_MASK, val);
	mib->tx_pkt_ibf_cnt += FIELD_GET(MT_MIB_SDR9_IBF_CNT_MASK, val);

	val = mt76_rr(dev, MT_ETBF_TX_APP_CNT(0));
	mib->tx_bf_ibf_ppdu_cnt += FIELD_GET(MT_ETBF_TX_IBF_CNT, val);
	mib->tx_bf_ebf_ppdu_cnt += FIELD_GET(MT_ETBF_TX_EBF_CNT, val);

	val = mt76_rr(dev, MT_ETBF_RX_FB_CNT(0));
	mib->tx_bf_rx_fb_all_cnt += FIELD_GET(MT_ETBF_RX_FB_ALL, val);
	mib->tx_bf_rx_fb_he_cnt += FIELD_GET(MT_ETBF_RX_FB_HE, val);
	mib->tx_bf_rx_fb_vht_cnt += FIELD_GET(MT_ETBF_RX_FB_VHT, val);
	mib->tx_bf_rx_fb_ht_cnt += FIELD_GET(MT_ETBF_RX_FB_HT, val);

	mib->rx_mpdu_cnt += mt76_rr(dev, MT_MIB_SDR5(0));
	mib->rx_ampdu_cnt += mt76_rr(dev, MT_MIB_SDR22(0));
	mib->rx_ampdu_bytes_cnt += mt76_rr(dev, MT_MIB_SDR23(0));
	mib->rx_ba_cnt += mt76_rr(dev, MT_MIB_SDR31(0));

	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++) {
		val = mt76_rr(dev, MT_PLE_AMSDU_PACK_MSDU_CNT(i));
		mib->tx_amsdu[i] += val;
		mib->tx_amsdu_cnt += val;
	}

	for (i = 0, aggr1 = aggr0 + 8; i < 4; i++) {
		u32 val2;

		val = mt76_rr(dev, MT_TX_AGG_CNT(0, i));
		val2 = mt76_rr(dev, MT_TX_AGG_CNT2(0, i));

		phy->mt76->aggr_stats[aggr0++] += val & 0xffff;
		phy->mt76->aggr_stats[aggr0++] += val >> 16;
		phy->mt76->aggr_stats[aggr1++] += val2 & 0xffff;
		phy->mt76->aggr_stats[aggr1++] += val2 >> 16;
	}
}
EXPORT_SYMBOL_GPL(mt792x_mac_update_mib_stats);
