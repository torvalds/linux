// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "tx.h"
#include "fw.h"
#include "ps.h"
#include "debug.h"

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
	struct rtw_tx_desc *tx_desc = (struct rtw_tx_desc *)skb->data;
	bool more_data = false;

	if (pkt_info->qsel == TX_DESC_QSEL_HIGH)
		more_data = true;

	tx_desc->w0 = le32_encode_bits(pkt_info->tx_pkt_size, RTW_TX_DESC_W0_TXPKTSIZE) |
		      le32_encode_bits(pkt_info->offset, RTW_TX_DESC_W0_OFFSET) |
		      le32_encode_bits(pkt_info->bmc, RTW_TX_DESC_W0_BMC) |
		      le32_encode_bits(pkt_info->ls, RTW_TX_DESC_W0_LS) |
		      le32_encode_bits(pkt_info->dis_qselseq, RTW_TX_DESC_W0_DISQSELSEQ);

	tx_desc->w1 = le32_encode_bits(pkt_info->qsel, RTW_TX_DESC_W1_QSEL) |
		      le32_encode_bits(pkt_info->rate_id, RTW_TX_DESC_W1_RATE_ID) |
		      le32_encode_bits(pkt_info->sec_type, RTW_TX_DESC_W1_SEC_TYPE) |
		      le32_encode_bits(pkt_info->pkt_offset, RTW_TX_DESC_W1_PKT_OFFSET) |
		      le32_encode_bits(more_data, RTW_TX_DESC_W1_MORE_DATA);

	tx_desc->w2 = le32_encode_bits(pkt_info->ampdu_en, RTW_TX_DESC_W2_AGG_EN) |
		      le32_encode_bits(pkt_info->report, RTW_TX_DESC_W2_SPE_RPT) |
		      le32_encode_bits(pkt_info->ampdu_density, RTW_TX_DESC_W2_AMPDU_DEN) |
		      le32_encode_bits(pkt_info->bt_null, RTW_TX_DESC_W2_BT_NULL);

	tx_desc->w3 = le32_encode_bits(pkt_info->hw_ssn_sel, RTW_TX_DESC_W3_HW_SSN_SEL) |
		      le32_encode_bits(pkt_info->use_rate, RTW_TX_DESC_W3_USE_RATE) |
		      le32_encode_bits(pkt_info->dis_rate_fallback, RTW_TX_DESC_W3_DISDATAFB) |
		      le32_encode_bits(pkt_info->rts, RTW_TX_DESC_W3_USE_RTS) |
		      le32_encode_bits(pkt_info->nav_use_hdr, RTW_TX_DESC_W3_NAVUSEHDR) |
		      le32_encode_bits(pkt_info->ampdu_factor, RTW_TX_DESC_W3_MAX_AGG_NUM);

	tx_desc->w4 = le32_encode_bits(pkt_info->rate, RTW_TX_DESC_W4_DATARATE);

	tx_desc->w5 = le32_encode_bits(pkt_info->short_gi, RTW_TX_DESC_W5_DATA_SHORT) |
		      le32_encode_bits(pkt_info->bw, RTW_TX_DESC_W5_DATA_BW) |
		      le32_encode_bits(pkt_info->ldpc, RTW_TX_DESC_W5_DATA_LDPC) |
		      le32_encode_bits(pkt_info->stbc, RTW_TX_DESC_W5_DATA_STBC);

	tx_desc->w6 = le32_encode_bits(pkt_info->sn, RTW_TX_DESC_W6_SW_DEFINE);

	tx_desc->w8 = le32_encode_bits(pkt_info->en_hwseq, RTW_TX_DESC_W8_EN_HWSEQ);

	tx_desc->w9 = le32_encode_bits(pkt_info->seq, RTW_TX_DESC_W9_SW_SEQ);

	if (pkt_info->rts) {
		tx_desc->w4 |= le32_encode_bits(DESC_RATE24M, RTW_TX_DESC_W4_RTSRATE);
		tx_desc->w5 |= le32_encode_bits(1, RTW_TX_DESC_W5_DATA_RTS_SHORT);
	}

	if (pkt_info->tim_offset)
		tx_desc->w9 |= le32_encode_bits(1, RTW_TX_DESC_W9_TIM_EN) |
			       le32_encode_bits(pkt_info->tim_offset, RTW_TX_DESC_W9_TIM_OFFSET);
}
EXPORT_SYMBOL(rtw_tx_fill_tx_desc);

static u8 get_tx_ampdu_factor(struct ieee80211_sta *sta)
{
	u8 exp = sta->deflink.ht_cap.ampdu_factor;

	/* the least ampdu factor is 8K, and the value in the tx desc is the
	 * max aggregation num, which represents val * 2 packets can be
	 * aggregated in an AMPDU, so here we should use 8/2=4 as the base
	 */
	return (BIT(2) << exp) - 1;
}

static u8 get_tx_ampdu_density(struct ieee80211_sta *sta)
{
	return sta->deflink.ht_cap.ampdu_density;
}

static u8 get_highest_ht_tx_rate(struct rtw_dev *rtwdev,
				 struct ieee80211_sta *sta)
{
	u8 rate;

	if (rtwdev->hal.rf_type == RF_2T2R && sta->deflink.ht_cap.mcs.rx_mask[1] != 0)
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

	tx_mcs_map = le16_to_cpu(sta->deflink.vht_cap.vht_mcs.tx_mcs_map);
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

	rtw_warn(rtwdev, "failed to get tx report from firmware\n");

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

void rtw_tx_report_handle(struct rtw_dev *rtwdev, struct sk_buff *skb, int src)
{
	struct rtw_tx_report *tx_report = &rtwdev->tx_report;
	struct rtw_c2h_cmd *c2h;
	struct sk_buff *cur, *tmp;
	unsigned long flags;
	u8 sn, st;
	u8 *n;

	c2h = get_c2h_from_skb(skb);

	if (src == C2H_CCX_TX_RPT) {
		sn = GET_CCX_REPORT_SEQNUM_V0(c2h->payload);
		st = GET_CCX_REPORT_STATUS_V0(c2h->payload);
	} else {
		sn = GET_CCX_REPORT_SEQNUM_V1(c2h->payload);
		st = GET_CCX_REPORT_STATUS_V1(c2h->payload);
	}

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

static u8 rtw_get_mgmt_rate(struct rtw_dev *rtwdev, struct sk_buff *skb,
			    u8 lowest_rate, bool ignore_rate)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = tx_info->control.vif;
	bool force_lowest = test_bit(RTW_FLAG_FORCE_LOWEST_RATE, rtwdev->flags);

	if (!vif || !vif->bss_conf.basic_rates || ignore_rate || force_lowest)
		return lowest_rate;

	return __ffs(vif->bss_conf.basic_rates) + lowest_rate;
}

static void rtw_tx_pkt_info_update_rate(struct rtw_dev *rtwdev,
					struct rtw_tx_pkt_info *pkt_info,
					struct sk_buff *skb,
					bool ignore_rate)
{
	if (rtwdev->hal.current_band_type == RTW_BAND_2G) {
		pkt_info->rate_id = RTW_RATEID_B_20M;
		pkt_info->rate = rtw_get_mgmt_rate(rtwdev, skb, DESC_RATE1M,
						   ignore_rate);
	} else {
		pkt_info->rate_id = RTW_RATEID_G;
		pkt_info->rate = rtw_get_mgmt_rate(rtwdev, skb, DESC_RATE6M,
						   ignore_rate);
	}

	pkt_info->use_rate = true;
	pkt_info->dis_rate_fallback = true;
}

static void rtw_tx_pkt_info_update_sec(struct rtw_dev *rtwdev,
				       struct rtw_tx_pkt_info *pkt_info,
				       struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u8 sec_type = 0;

	if (info && info->control.hw_key) {
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

	pkt_info->sec_type = sec_type;
}

static void rtw_tx_mgmt_pkt_info_update(struct rtw_dev *rtwdev,
					struct rtw_tx_pkt_info *pkt_info,
					struct ieee80211_sta *sta,
					struct sk_buff *skb)
{
	rtw_tx_pkt_info_update_rate(rtwdev, pkt_info, skb, false);
	pkt_info->dis_qselseq = true;
	pkt_info->en_hwseq = true;
	pkt_info->hw_ssn_sel = 0;
	/* TODO: need to change hw port and hw ssn sel for multiple vifs */
}

static void rtw_tx_data_pkt_info_update(struct rtw_dev *rtwdev,
					struct rtw_tx_pkt_info *pkt_info,
					struct ieee80211_sta *sta,
					struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hw *hw = rtwdev->hw;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_sta_info *si;
	u8 fix_rate;
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

	if (info->control.use_rts || skb->len > hw->wiphy->rts_threshold)
		pkt_info->rts = true;

	if (sta->deflink.vht_cap.vht_supported)
		rate = get_highest_vht_tx_rate(rtwdev, sta);
	else if (sta->deflink.ht_cap.ht_supported)
		rate = get_highest_ht_tx_rate(rtwdev, sta);
	else if (sta->deflink.supp_rates[0] <= 0xf)
		rate = DESC_RATE11M;
	else
		rate = DESC_RATE54M;

	si = (struct rtw_sta_info *)sta->drv_priv;

	bw = si->bw_mode;
	rate_id = si->rate_id;
	stbc = rtwdev->hal.txrx_1ss ? false : si->stbc_en;
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

	fix_rate = dm_info->fix_rate;
	if (fix_rate < DESC_RATE_MAX) {
		pkt_info->rate = fix_rate;
		pkt_info->dis_rate_fallback = true;
		pkt_info->use_rate = true;
	}
}

void rtw_tx_pkt_info_update(struct rtw_dev *rtwdev,
			    struct rtw_tx_pkt_info *pkt_info,
			    struct ieee80211_sta *sta,
			    struct sk_buff *skb)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct rtw_sta_info *si;
	struct ieee80211_vif *vif = NULL;
	__le16 fc = hdr->frame_control;
	bool bmc;

	if (sta) {
		si = (struct rtw_sta_info *)sta->drv_priv;
		vif = si->vif;
	}

	if (ieee80211_is_mgmt(fc) || ieee80211_is_nullfunc(fc))
		rtw_tx_mgmt_pkt_info_update(rtwdev, pkt_info, sta, skb);
	else if (ieee80211_is_data(fc))
		rtw_tx_data_pkt_info_update(rtwdev, pkt_info, sta, skb);

	bmc = is_broadcast_ether_addr(hdr->addr1) ||
	      is_multicast_ether_addr(hdr->addr1);

	if (info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS)
		rtw_tx_report_enable(rtwdev, pkt_info);

	pkt_info->bmc = bmc;
	rtw_tx_pkt_info_update_sec(rtwdev, pkt_info, skb);
	pkt_info->tx_pkt_size = skb->len;
	pkt_info->offset = chip->tx_pkt_desc_sz;
	pkt_info->qsel = skb->priority;
	pkt_info->ls = true;

	/* maybe merge with tx status ? */
	rtw_tx_stats(rtwdev, vif, skb);
}

void rtw_tx_rsvd_page_pkt_info_update(struct rtw_dev *rtwdev,
				      struct rtw_tx_pkt_info *pkt_info,
				      struct sk_buff *skb,
				      enum rtw_rsvd_packet_type type)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	bool bmc;

	/* A beacon or dummy reserved page packet indicates that it is the first
	 * reserved page, and the qsel of it will be set in each hci.
	 */
	if (type != RSVD_BEACON && type != RSVD_DUMMY)
		pkt_info->qsel = TX_DESC_QSEL_MGMT;

	rtw_tx_pkt_info_update_rate(rtwdev, pkt_info, skb, true);

	bmc = is_broadcast_ether_addr(hdr->addr1) ||
	      is_multicast_ether_addr(hdr->addr1);
	pkt_info->bmc = bmc;
	pkt_info->tx_pkt_size = skb->len;
	pkt_info->offset = chip->tx_pkt_desc_sz;
	pkt_info->ls = true;
	if (type == RSVD_PS_POLL) {
		pkt_info->nav_use_hdr = true;
	} else {
		pkt_info->dis_qselseq = true;
		pkt_info->en_hwseq = true;
		pkt_info->hw_ssn_sel = 0;
	}
	if (type == RSVD_QOS_NULL)
		pkt_info->bt_null = true;

	if (type == RSVD_BEACON) {
		struct rtw_rsvd_page *rsvd_pkt;
		int hdr_len;

		rsvd_pkt = list_first_entry_or_null(&rtwdev->rsvd_page_list,
						    struct rtw_rsvd_page,
						    build_list);
		if (rsvd_pkt && rsvd_pkt->tim_offset != 0) {
			hdr_len = sizeof(struct ieee80211_hdr_3addr);
			pkt_info->tim_offset = rsvd_pkt->tim_offset - hdr_len;
		}
	}

	rtw_tx_pkt_info_update_sec(rtwdev, pkt_info, skb);

	/* TODO: need to change hw port and hw ssn sel for multiple vifs */
}

struct sk_buff *
rtw_tx_write_data_rsvd_page_get(struct rtw_dev *rtwdev,
				struct rtw_tx_pkt_info *pkt_info,
				u8 *buf, u32 size)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct sk_buff *skb;
	u32 tx_pkt_desc_sz;
	u32 length;

	tx_pkt_desc_sz = chip->tx_pkt_desc_sz;
	length = size + tx_pkt_desc_sz;
	skb = dev_alloc_skb(length);
	if (!skb) {
		rtw_err(rtwdev, "failed to alloc write data rsvd page skb\n");
		return NULL;
	}

	skb_reserve(skb, tx_pkt_desc_sz);
	skb_put_data(skb, buf, size);
	rtw_tx_rsvd_page_pkt_info_update(rtwdev, pkt_info, skb, RSVD_BEACON);

	return skb;
}
EXPORT_SYMBOL(rtw_tx_write_data_rsvd_page_get);

struct sk_buff *
rtw_tx_write_data_h2c_get(struct rtw_dev *rtwdev,
			  struct rtw_tx_pkt_info *pkt_info,
			  u8 *buf, u32 size)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct sk_buff *skb;
	u32 tx_pkt_desc_sz;
	u32 length;

	tx_pkt_desc_sz = chip->tx_pkt_desc_sz;
	length = size + tx_pkt_desc_sz;
	skb = dev_alloc_skb(length);
	if (!skb) {
		rtw_err(rtwdev, "failed to alloc write data h2c skb\n");
		return NULL;
	}

	skb_reserve(skb, tx_pkt_desc_sz);
	skb_put_data(skb, buf, size);
	pkt_info->tx_pkt_size = size;

	return skb;
}
EXPORT_SYMBOL(rtw_tx_write_data_h2c_get);

void rtw_tx(struct rtw_dev *rtwdev,
	    struct ieee80211_tx_control *control,
	    struct sk_buff *skb)
{
	struct rtw_tx_pkt_info pkt_info = {0};
	int ret;

	rtw_tx_pkt_info_update(rtwdev, &pkt_info, control->sta, skb);
	ret = rtw_hci_tx_write(rtwdev, &pkt_info, skb);
	if (ret) {
		rtw_err(rtwdev, "failed to write TX skb to HCI\n");
		goto out;
	}

	rtw_hci_tx_kick_off(rtwdev);

	return;

out:
	ieee80211_free_txskb(rtwdev->hw, skb);
}

static void rtw_txq_check_agg(struct rtw_dev *rtwdev,
			      struct rtw_txq *rtwtxq,
			      struct sk_buff *skb)
{
	struct ieee80211_txq *txq = rtwtxq_to_txq(rtwtxq);
	struct ieee80211_tx_info *info;
	struct rtw_sta_info *si;

	if (test_bit(RTW_TXQ_AMPDU, &rtwtxq->flags)) {
		info = IEEE80211_SKB_CB(skb);
		info->flags |= IEEE80211_TX_CTL_AMPDU;
		return;
	}

	if (skb_get_queue_mapping(skb) == IEEE80211_AC_VO)
		return;

	if (test_bit(RTW_TXQ_BLOCK_BA, &rtwtxq->flags))
		return;

	if (unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)))
		return;

	if (!txq->sta)
		return;

	si = (struct rtw_sta_info *)txq->sta->drv_priv;
	set_bit(txq->tid, si->tid_ba);

	ieee80211_queue_work(rtwdev->hw, &rtwdev->ba_work);
}

static int rtw_txq_push_skb(struct rtw_dev *rtwdev,
			    struct rtw_txq *rtwtxq,
			    struct sk_buff *skb)
{
	struct ieee80211_txq *txq = rtwtxq_to_txq(rtwtxq);
	struct rtw_tx_pkt_info pkt_info = {0};
	int ret;

	rtw_txq_check_agg(rtwdev, rtwtxq, skb);

	rtw_tx_pkt_info_update(rtwdev, &pkt_info, txq->sta, skb);
	ret = rtw_hci_tx_write(rtwdev, &pkt_info, skb);
	if (ret) {
		rtw_err(rtwdev, "failed to write TX skb to HCI\n");
		return ret;
	}
	rtwtxq->last_push = jiffies;

	return 0;
}

static struct sk_buff *rtw_txq_dequeue(struct rtw_dev *rtwdev,
				       struct rtw_txq *rtwtxq)
{
	struct ieee80211_txq *txq = rtwtxq_to_txq(rtwtxq);
	struct sk_buff *skb;

	skb = ieee80211_tx_dequeue(rtwdev->hw, txq);
	if (!skb)
		return NULL;

	return skb;
}

static void rtw_txq_push(struct rtw_dev *rtwdev,
			 struct rtw_txq *rtwtxq,
			 unsigned long frames)
{
	struct sk_buff *skb;
	int ret;
	int i;

	rcu_read_lock();

	for (i = 0; i < frames; i++) {
		skb = rtw_txq_dequeue(rtwdev, rtwtxq);
		if (!skb)
			break;

		ret = rtw_txq_push_skb(rtwdev, rtwtxq, skb);
		if (ret) {
			rtw_err(rtwdev, "failed to pusk skb, ret %d\n", ret);
			break;
		}
	}

	rcu_read_unlock();
}

void __rtw_tx_work(struct rtw_dev *rtwdev)
{
	struct rtw_txq *rtwtxq, *tmp;

	spin_lock_bh(&rtwdev->txq_lock);

	list_for_each_entry_safe(rtwtxq, tmp, &rtwdev->txqs, list) {
		struct ieee80211_txq *txq = rtwtxq_to_txq(rtwtxq);
		unsigned long frame_cnt;
		unsigned long byte_cnt;

		ieee80211_txq_get_depth(txq, &frame_cnt, &byte_cnt);
		rtw_txq_push(rtwdev, rtwtxq, frame_cnt);

		list_del_init(&rtwtxq->list);
	}

	rtw_hci_tx_kick_off(rtwdev);

	spin_unlock_bh(&rtwdev->txq_lock);
}

void rtw_tx_work(struct work_struct *w)
{
	struct rtw_dev *rtwdev = container_of(w, struct rtw_dev, tx_work);

	__rtw_tx_work(rtwdev);
}

void rtw_txq_init(struct rtw_dev *rtwdev, struct ieee80211_txq *txq)
{
	struct rtw_txq *rtwtxq;

	if (!txq)
		return;

	rtwtxq = (struct rtw_txq *)txq->drv_priv;
	INIT_LIST_HEAD(&rtwtxq->list);
}

void rtw_txq_cleanup(struct rtw_dev *rtwdev, struct ieee80211_txq *txq)
{
	struct rtw_txq *rtwtxq;

	if (!txq)
		return;

	rtwtxq = (struct rtw_txq *)txq->drv_priv;
	spin_lock_bh(&rtwdev->txq_lock);
	if (!list_empty(&rtwtxq->list))
		list_del_init(&rtwtxq->list);
	spin_unlock_bh(&rtwdev->txq_lock);
}

static const enum rtw_tx_queue_type ac_to_hwq[] = {
	[IEEE80211_AC_VO] = RTW_TX_QUEUE_VO,
	[IEEE80211_AC_VI] = RTW_TX_QUEUE_VI,
	[IEEE80211_AC_BE] = RTW_TX_QUEUE_BE,
	[IEEE80211_AC_BK] = RTW_TX_QUEUE_BK,
};

static_assert(ARRAY_SIZE(ac_to_hwq) == IEEE80211_NUM_ACS);

enum rtw_tx_queue_type rtw_tx_ac_to_hwq(enum ieee80211_ac_numbers ac)
{
	if (WARN_ON(unlikely(ac >= IEEE80211_NUM_ACS)))
		return RTW_TX_QUEUE_BE;

	return ac_to_hwq[ac];
}
EXPORT_SYMBOL(rtw_tx_ac_to_hwq);

enum rtw_tx_queue_type rtw_tx_queue_mapping(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	__le16 fc = hdr->frame_control;
	u8 q_mapping = skb_get_queue_mapping(skb);
	enum rtw_tx_queue_type queue;

	if (unlikely(ieee80211_is_beacon(fc)))
		queue = RTW_TX_QUEUE_BCN;
	else if (unlikely(ieee80211_is_mgmt(fc) || ieee80211_is_ctl(fc)))
		queue = RTW_TX_QUEUE_MGMT;
	else if (is_broadcast_ether_addr(hdr->addr1) ||
		 is_multicast_ether_addr(hdr->addr1))
		queue = RTW_TX_QUEUE_HI0;
	else if (WARN_ON_ONCE(q_mapping >= ARRAY_SIZE(ac_to_hwq)))
		queue = ac_to_hwq[IEEE80211_AC_BE];
	else
		queue = ac_to_hwq[q_mapping];

	return queue;
}
EXPORT_SYMBOL(rtw_tx_queue_mapping);
