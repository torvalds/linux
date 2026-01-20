// SPDX-License-Identifier: BSD-3-Clause-Clear
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

struct mt76_wcid *mt792x_rx_get_wcid(struct mt792x_dev *dev, u16 idx,
				     bool unicast)
{
	struct mt792x_link_sta *link;
	struct mt792x_sta *sta;
	struct mt76_wcid *wcid;

	wcid = mt76_wcid_ptr(dev, idx);
	if (unicast || !wcid)
		return wcid;

	if (!wcid->sta)
		return NULL;

	link = container_of(wcid, struct mt792x_link_sta, wcid);
	sta = link->sta;
	if (!sta->vif)
		return NULL;

	return &sta->vif->sta.deflink.wcid;
}
EXPORT_SYMBOL_GPL(mt792x_rx_get_wcid);

static void
mt792x_mac_rssi_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct sk_buff *skb = priv;
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct ieee80211_hdr *hdr = mt76_skb_get_hdr(skb);

	if (status->signal > 0)
		return;

	if (!ether_addr_equal(vif->addr, hdr->addr1))
		return;

	ewma_rssi_add(&mvif->bss_conf.rssi, -status->signal);
}

void mt792x_mac_assoc_rssi(struct mt792x_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = mt76_skb_get_hdr(skb);

	if (!ieee80211_is_assoc_resp(hdr->frame_control) &&
	    !ieee80211_is_auth(hdr->frame_control))
		return;

	ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt792x_mac_rssi_iter, skb);
}
EXPORT_SYMBOL_GPL(mt792x_mac_assoc_rssi);

void mt792x_mac_reset_counters(struct mt792x_phy *phy)
{
	struct mt792x_dev *dev = phy->dev;
	int i;

	for (i = 0; i < 4; i++) {
		mt76_rr(dev, MT_TX_AGG_CNT(0, i));
		mt76_rr(dev, MT_TX_AGG_CNT2(0, i));
	}

	dev->mt76.phy.survey_time = ktime_get_boottime();
	memset(phy->mt76->aggr_stats, 0, sizeof(phy->mt76->aggr_stats));

	/* reset airtime counters */
	mt76_rr(dev, MT_MIB_SDR9(0));
	mt76_rr(dev, MT_MIB_SDR36(0));
	mt76_rr(dev, MT_MIB_SDR37(0));

	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);
}
EXPORT_SYMBOL_GPL(mt792x_mac_reset_counters);

static u8
mt792x_phy_get_nf(struct mt792x_phy *phy, int idx)
{
	return 0;
}

static void
mt792x_phy_update_channel(struct mt76_phy *mphy, int idx)
{
	struct mt792x_dev *dev = container_of(mphy->dev, struct mt792x_dev, mt76);
	struct mt792x_phy *phy = mphy->priv;
	struct mt76_channel_state *state;
	u64 busy_time, tx_time, rx_time, obss_time;
	int nf;

	busy_time = mt76_get_field(dev, MT_MIB_SDR9(idx),
				   MT_MIB_SDR9_BUSY_MASK);
	tx_time = mt76_get_field(dev, MT_MIB_SDR36(idx),
				 MT_MIB_SDR36_TXTIME_MASK);
	rx_time = mt76_get_field(dev, MT_MIB_SDR37(idx),
				 MT_MIB_SDR37_RXTIME_MASK);
	obss_time = mt76_get_field(dev, MT_WF_RMAC_MIB_AIRTIME14(idx),
				   MT_MIB_OBSSTIME_MASK);

	nf = mt792x_phy_get_nf(phy, idx);
	if (!phy->noise)
		phy->noise = nf << 4;
	else if (nf)
		phy->noise += nf - (phy->noise >> 4);

	state = mphy->chan_state;
	state->cc_busy += busy_time;
	state->cc_tx += tx_time;
	state->cc_rx += rx_time + obss_time;
	state->cc_bss_rx += rx_time;
	state->noise = -(phy->noise >> 4);
}

void mt792x_update_channel(struct mt76_phy *mphy)
{
	struct mt792x_dev *dev = container_of(mphy->dev, struct mt792x_dev, mt76);

	if (mt76_connac_pm_wake(mphy, &dev->pm))
		return;

	mt792x_phy_update_channel(mphy, 0);
	/* reset obss airtime */
	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);
	mt76_connac_power_save_sched(mphy, &dev->pm);
}
EXPORT_SYMBOL_GPL(mt792x_update_channel);

void mt792x_reset(struct mt76_dev *mdev)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	struct mt76_connac_pm *pm = &dev->pm;

	if (!dev->hw_init_done)
		return;

	if (dev->hw_full_reset)
		return;

	if (pm->suspended)
		return;

	queue_work(dev->mt76.wq, &dev->reset_work);
}
EXPORT_SYMBOL_GPL(mt792x_reset);

void mt792x_mac_init_band(struct mt792x_dev *dev, u8 band)
{
	u32 mask, set;

	mt76_rmw_field(dev, MT_TMAC_CTCR0(band),
		       MT_TMAC_CTCR0_INS_DDLMT_REFTIME, 0x3f);
	mt76_set(dev, MT_TMAC_CTCR0(band),
		 MT_TMAC_CTCR0_INS_DDLMT_VHT_SMPDU_EN |
		 MT_TMAC_CTCR0_INS_DDLMT_EN);

	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(band), MT_WF_RMAC_MIB_RXTIME_EN);
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0(band), MT_WF_RMAC_MIB_RXTIME_EN);

	/* enable MIB tx-rx time reporting */
	mt76_set(dev, MT_MIB_SCR1(band), MT_MIB_TXDUR_EN);
	mt76_set(dev, MT_MIB_SCR1(band), MT_MIB_RXDUR_EN);

	mt76_rmw_field(dev, MT_DMA_DCR0(band), MT_DMA_DCR0_MAX_RX_LEN, 1536);
	/* disable rx rate report by default due to hw issues */
	mt76_clear(dev, MT_DMA_DCR0(band), MT_DMA_DCR0_RXD_G5_EN);

	/* filter out non-resp frames and get instantaneous signal reporting */
	mask = MT_WTBLOFF_TOP_RSCR_RCPI_MODE | MT_WTBLOFF_TOP_RSCR_RCPI_PARAM;
	set = FIELD_PREP(MT_WTBLOFF_TOP_RSCR_RCPI_MODE, 0) |
	      FIELD_PREP(MT_WTBLOFF_TOP_RSCR_RCPI_PARAM, 0x3);
	mt76_rmw(dev, MT_WTBLOFF_TOP_RSCR(band), mask, set);
}
EXPORT_SYMBOL_GPL(mt792x_mac_init_band);

void mt792x_pm_wake_work(struct work_struct *work)
{
	struct mt792x_dev *dev;
	struct mt76_phy *mphy;

	dev = (struct mt792x_dev *)container_of(work, struct mt792x_dev,
						pm.wake_work);
	mphy = dev->phy.mt76;

	if (!mt792x_mcu_drv_pmctrl(dev)) {
		struct mt76_dev *mdev = &dev->mt76;
		int i;

		if (mt76_is_sdio(mdev)) {
			mt76_connac_pm_dequeue_skbs(mphy, &dev->pm);
			mt76_worker_schedule(&mdev->sdio.txrx_worker);
		} else {
			local_bh_disable();
			mt76_for_each_q_rx(mdev, i)
				napi_schedule(&mdev->napi[i]);
			local_bh_enable();
			mt76_connac_pm_dequeue_skbs(mphy, &dev->pm);
			mt76_connac_tx_cleanup(mdev);
		}
		if (test_bit(MT76_STATE_RUNNING, &mphy->state))
			ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
						     MT792x_WATCHDOG_TIME);
	}

	ieee80211_wake_queues(mphy->hw);
	wake_up(&dev->pm.wait);
}
EXPORT_SYMBOL_GPL(mt792x_pm_wake_work);

void mt792x_pm_power_save_work(struct work_struct *work)
{
	struct mt792x_dev *dev;
	unsigned long delta;
	struct mt76_phy *mphy;

	dev = (struct mt792x_dev *)container_of(work, struct mt792x_dev,
						pm.ps_work.work);
	mphy = dev->phy.mt76;

	delta = dev->pm.idle_timeout;
	if (test_bit(MT76_HW_SCANNING, &mphy->state) ||
	    test_bit(MT76_HW_SCHED_SCANNING, &mphy->state) ||
	    dev->fw_assert)
		goto out;

	if (mutex_is_locked(&dev->mt76.mutex))
		/* if mt76 mutex is held we should not put the device
		 * to sleep since we are currently accessing device
		 * register map. We need to wait for the next power_save
		 * trigger.
		 */
		goto out;

	if (time_is_after_jiffies(dev->pm.last_activity + delta)) {
		delta = dev->pm.last_activity + delta - jiffies;
		goto out;
	}

	if (!mt792x_mcu_fw_pmctrl(dev)) {
		cancel_delayed_work_sync(&mphy->mac_work);
		return;
	}
out:
	queue_delayed_work(dev->mt76.wq, &dev->pm.ps_work, delta);
}
EXPORT_SYMBOL_GPL(mt792x_pm_power_save_work);
