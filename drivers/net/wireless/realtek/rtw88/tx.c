// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "tx.h"
#include "fw.h"
#include "ps.h"

static
void rtw_tx_stats(struct rtw_dev *rtwdev, struct ieee80211_vif *vif,
		  struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	struct rtw_vif *rtwvif;

	hdr = (struct ieee80211_hdr *)skb->data;

	if (!ieee80211_is_data(hdr->frame_control))
		return;

	if (!is_broadcast_ether_addr(hdr->addr1) &&
	    !is_multicast_ether_addr(hdr->addr1)) {
		rtwdev->stats.tx_unicast += skb->len;
		rtwdev->stats.tx_cnt++;
		if (vif) {
			rtwvif = (struct rtw_vif *)vif->drv_priv;
			rtwvif->stats.tx_unicast += skb->len;
			rtwvif->stats.tx_cnt++;
		}
	}
}

void rtw_tx_fill_tx_desc(struct rtw_tx_pkt_info *pkt_info, struct sk_buff *skb)
{
	__le32 *txdesc = (__le32 *)skb->data;

	SET_TX_DESC_TXPKTSIZE(txdesc,  pkt_info->tx_pkt_size);
	SET_TX_DESC_OFFSET(txdesc, pkt_info->offset);
	SET_TX_DESC_PKT_OFFSET(txdesc, pkt_info->pkt_offset);
	SET_TX_DESC_QSEL(txdesc, pkt_info->qsel);
	SET_TX_DESC_BMC(txdesc, pkt_info->bmc);
	SET_TX_DESC_RATE_ID(txdesc, pkt_info->rate_id);
	SET_TX_DESC_DATARATE(txdesc, pkt_info->rate);
	SET_TX_DESC_DISDATAFB(txdesc, pkt_info->dis_rate_fallback);
	SET_TX_DESC_USE_RATE(txdesc, pkt_info->use_rate);
	SET_TX_DESC_SEC_TYPE(txdesc, pkt_info->sec_type);
	SET_TX_DESC_DATA_BW(txdesc, pkt_info->bw);
	SET_TX_DESC_SW_SEQ(txdesc, pkt_info->seq);
	SET_TX_DESC_MAX_AGG_NUM(txdesc, pkt_info->ampdu_factor);
	SET_TX_DESC_AMPDU_DENSITY(txdesc, pkt_info->ampdu_density);
	SET_TX_DESC_DATA_STBC(txdesc, pkt_info->stbc);
	SET_TX_DESC_DATA_LDPC(txdesc, pkt_info->ldpc);
	SET_TX_DESC_AGG_EN(txdesc, pkt_info->ampdu_en);
	SET_TX_DESC_LS(txdesc, pkt_info->ls);
	SET_TX_DESC_DATA_SHORT(txdesc, pkt_info->short_gi);
	SET_TX_DESC_SPE_RPT(txdesc, pkt_info->report);
	SET_TX_DESC_SW_DEFINE(txdesc, pkt_info->sn);
}
EXPORT_SYMBOL(rtw_tx_fill_tx_desc);

static u8 get_tx_ampdu_factor(struct ieee80211_sta *sta)
{
	u8 exp = sta->ht_cap.ampdu_factor;

	/* the least ampdu factor is 8K, and the value in the tx desc is the
	 * max aggregation num, which represents val * 2 packets can be
	 * aggregated in an AMPDU, so here we should use 8/2=4 as the base
	 */
	return (BIT(2) << exp) - 1;
}

static u8 get_tx_ampdu_density(struct ieee80211_sta *sta)
{
	return sta->ht_cap.ampdu_density;
}

static u8 get_highest_ht_tx_rate(struct rtw_dev *rtwdev,
				 struct ieee80211_sta *sta)
{
	u8 rate;

	if (rtwdev->hal.rf_type == RF_2T2R && sta->ht_cap.mcs.rx_mask[1] != 0)
		rate = DESC_RATEMCS15;
	else
		rate = DESC_RATEMCS7;

	return rate;
}

static u8 get_highest_vht_tx_rate(struct rtw_dev *rtwdev,
				  struct ieee80211_sta *sta)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 rate;
	u16 tx_mcs_map;

	tx_mcs_map = le16_to_cpu(sta->vht_cap.vht_mcs.tx_mcs_map);
	if (efuse->hw_cap.nss == 1) {
		switch (tx_mcs_map & 0x3) {
		case IEEE80211_VHT_MCS_SUPPORT_0_7:
			rate = DESC_RATEVHT1SS_MCS7;
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_8:
			rate = DESC_RATEVHT1SS_MCS8;
			break;
		default:
		case IEEE80211_VHT_MCS_SUPPORT_0_9:
			rate = DESC_RATEVHT1SS_MCS9;
			break;
		}
	} else if (efuse->hw_cap.nss >= 2) {
		switch ((tx_mcs_map & 0xc) >> 2) {
		case IEEE80211_VHT_MCS_SUPPORT_0_7:
			rate = DESC_RATEVHT2SS_MCS7;
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_8:
			rate = DESC_RATEVHT2SS_MCS8;
			break;
		default:
		case IEEE80211_VHT_MCS_SUPPORT_0_9:
			rate = DESC_RATEVHT2SS_MCS9;
			break;
		}
	} else {
		rate = DESC_RATEVHT1SS_MCS9;
	}

	return rate;
}

static void rtw_tx_report_enable(struct rtw_dev *rtwdev,
				 struct rtw_tx_pkt_info *pkt_info)
{
	struct rtw_tx_report *tx_report = &rtwdev->tx_report;

	/* [11:8], reserved, fills with zero
	 * [7:2],  tx report sequence number
	 * [1:0],  firmware use, fills with zero
	 */
	pkt_info->sn = (atomic_inc_return(&tx_report->sn) << 2) & 0xfc;
	pkt_info->report = true;
}

void rtw_tx_report_purge_timer(struct timer_list *t)
{
	struct rtw_dev *rtwdev = from_timer(rtwdev, t, tx_report.purge_timer);
	struct rtw_tx_report *tx_report = &rtwdev->tx_report;
	unsigned long flags;

	if (skb_queue_len(&tx_report->queue) == 0)
		return;

	WARN(1, "purge skb(s) not reported by firmware\n");

	spin_lock_irqsave(&tx_report->q_lock, flags);
	skb_queue_purge(&tx_report->queue);
	spin_unlock_irqrestore(&tx_report->q_lock, flags);
}

void rtw_tx_report_enqueue(struct rtw_dev *rtwdev, struct sk_buff *skb, u8 sn)
{
	struct rtw_tx_report *tx_report = &rtwdev->tx_report;
	unsigned long flags;
	u8 *drv_data;

	/* pass sn to tx report handler through driver data */
	drv_data = (u8 *)IEEE80211_SKB_CB(skb)->status.status_driver_data;
	*drv_data = sn;

	spin_lock_irqsave(&tx_report->q_lock, flags);
	__skb_queue_tail(&tx_report->queue, skb);
	spin_unlock_irqrestore(&tx_report->q_lock, flags);

	mod_timer(&tx_report->purge_timer, jiffies + RTW_TX_PROBE_TIMEOUT);
}
EXPORT_SYMBOL(rtw_tx_report_enqueue);

static void rtw_tx_report_tx_status(struct rtw_dev *rtwdev,
				    struct sk_buff *skb, bool acked)
{
	struct ieee80211_tx_info *info;

	info = IEEE80211_SKB_CB(skb);
	ieee80211_tx_info_clear_status(info);
	if (acked)
		info->flags |= IEEE80211_TX_STAT_ACK;
	else
		info->flags &= ~IEEE80211_TX_STAT_ACK;

	ieee80211_tx_status_irqsafe(rtwdev->hw, skb);
}

void rtw_tx_report_handle(struct rtw_dev *rtwdev, struct sk_buff *skb)
{
	struct rtw_tx_report *tx_report = &rtwdev->tx_report;
	struct rtw_c2h_cmd *c2h;
	struct sk_buff *cur, *tmp;
	unsigned long flags;
	u8 sn, st;
	u8 *n;

	c2h = get_c2h_from_skb(skb);

	sn = GET_CCX_REPORT_SEQNUM(c2h->payload);
	st = GET_CCX_REPORT_STATUS(c2h->payload);

	spin_lock_irqsave(&tx_report->q_lock, flags);
	skb_queue_walk_safe(&tx_report->queue, cur, tmp) {
		n = (u8 *)IEEE80211_SKB_CB(cur)->status.status_driver_data;
		if (*n == sn) {
			__skb_unlink(cur, &tx_report->queue);
			rtw_tx_report_tx_status(rtwdev, cur, st == 0);
			break;
		}
	}
	spin_unlock_irqrestore(&tx_report->q_lock, flags);
}

static void rtw_tx_mgmt_pkt_info_update(struct rtw_dev *rtwdev,
					struct rtw_tx_pkt_info *pkt_info,
					struct ieee80211_tx_control *control,
					struct sk_buff *skb)
{
	pkt_info->use_rate = true;
	pkt_info->rate_id = 6;
	pkt_info->dis_rate_fallback = true;
}

static void rtw_tx_data_pkt_info_update(struct rtw_dev *rtwdev,
					struct rtw_tx_pkt_info *pkt_info,
					struct ieee80211_tx_control *control,
					struct sk_buff *skb)
{
	struct ieee80211_sta *sta = control->sta;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct rtw_sta_info *si;
	u16 seq;
	u8 ampdu_factor = 0;
	u8 ampdu_density = 0;
	bool ampdu_en = false;
	u8 rate = DESC_RATE6M;
	u8 rate_id = 6;
	u8 bw = RTW_CHANNEL_WIDTH_20;
	bool stbc = false;
	bool ldpc = false;

	seq = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;

	/* for broadcast/multicast, use default values */
	if (!sta)
		goto out;

	if (info->flags & IEEE80211_TX_CTL_AMPDU) {
		ampdu_en = true;
		ampdu_factor = get_tx_ampdu_factor(sta);
		ampdu_density = get_tx_ampdu_density(sta);
	}

	if (sta->vht_cap.vht_supported)
		rate = get_highest_vht_tx_rate(rtwdev, sta);
	else if (sta->ht_cap.ht_supported)
		rate = get_highest_ht_tx_rate(rtwdev, sta);
	else if (sta->supp_rates[0] <= 0xf)
		rate = DESC_RATE11M;
	else
		rate = DESC_RATE54M;

	si = (struct rtw_sta_info *)sta->drv_priv;

	bw = si->bw_mode;
	rate_id = si->rate_id;
	stbc = si->stbc_en;
	ldpc = si->ldpc_en;

out:
	pkt_info->seq = seq;
	pkt_info->ampdu_factor = ampdu_factor;
	pkt_info->ampdu_density = ampdu_density;
	pkt_info->ampdu_en = ampdu_en;
	pkt_info->rate = rate;
	pkt_info->rate_id = rate_id;
	pkt_info->bw = bw;
	pkt_info->stbc = stbc;
	pkt_info->ldpc = ldpc;
}

void rtw_tx_pkt_info_update(struct rtw_dev *rtwdev,
			    struct rtw_tx_pkt_info *pkt_info,
			    struct ieee80211_tx_control *control,
			    struct sk_buff *skb)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct rtw_sta_info *si;
	struct ieee80211_vif *vif = NULL;
	__le16 fc = hdr->frame_control;
	u8 sec_type = 0;
	bool bmc;

	if (control->sta) {
		si = (struct rtw_sta_info *)control->sta->drv_priv;
		vif = si->vif;
	}

	if (ieee80211_is_mgmt(fc) || ieee80211_is_nullfunc(fc))
		rtw_tx_mgmt_pkt_info_update(rtwdev, pkt_info, control, skb);
	else if (ieee80211_is_data(fc))
		rtw_tx_data_pkt_info_update(rtwdev, pkt_info, control, skb);

	if (info->control.hw_key) {
		struct ieee80211_key_conf *key = info->control.hw_key;

		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
		case WLAN_CIPHER_SUITE_TKIP:
			sec_type = 0x01;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			sec_type = 0x03;
			break;
		default:
			break;
		}
	}

	bmc = is_broadcast_ether_addr(hdr->addr1) ||
	      is_multicast_ether_addr(hdr->addr1);

	if (info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS)
		rtw_tx_report_enable(rtwdev, pkt_info);

	pkt_info->bmc = bmc;
	pkt_info->sec_type = sec_type;
	pkt_info->tx_pkt_size = skb->len;
	pkt_info->offset = chip->tx_pkt_desc_sz;
	pkt_info->qsel = skb->priority;
	pkt_info->ls = true;

	/* maybe merge with tx status ? */
	rtw_tx_stats(rtwdev, vif, skb);
}

void rtw_rsvd_page_pkt_info_update(struct rtw_dev *rtwdev,
				   struct rtw_tx_pkt_info *pkt_info,
				   struct sk_buff *skb)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	bool bmc;

	bmc = is_broadcast_ether_addr(hdr->addr1) ||
	      is_multicast_ether_addr(hdr->addr1);
	pkt_info->use_rate = true;
	pkt_info->rate_id = 6;
	pkt_info->dis_rate_fallback = true;
	pkt_info->bmc = bmc;
	pkt_info->tx_pkt_size = skb->len;
	pkt_info->offset = chip->tx_pkt_desc_sz;
	pkt_info->qsel = TX_DESC_QSEL_MGMT;
	pkt_info->ls = true;
}
