// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "rx.h"
#include "ps.h"
#include "debug.h"
#include "fw.h"

void rtw_rx_stats(struct rtw_dev *rtwdev, struct ieee80211_vif *vif,
		  struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	struct rtw_vif *rtwvif;

	hdr = (struct ieee80211_hdr *)skb->data;

	if (!ieee80211_is_data(hdr->frame_control))
		return;

	if (!is_broadcast_ether_addr(hdr->addr1) &&
	    !is_multicast_ether_addr(hdr->addr1)) {
		rtwdev->stats.rx_unicast += skb->len;
		rtwdev->stats.rx_cnt++;
		if (vif) {
			rtwvif = (struct rtw_vif *)vif->drv_priv;
			rtwvif->stats.rx_unicast += skb->len;
			rtwvif->stats.rx_cnt++;
		}
	}
}
EXPORT_SYMBOL(rtw_rx_stats);

struct rtw_rx_addr_match_data {
	struct rtw_dev *rtwdev;
	struct ieee80211_hdr *hdr;
	struct rtw_rx_pkt_stat *pkt_stat;
	u8 *bssid;
};

static void rtw_rx_phy_stat(struct rtw_dev *rtwdev,
			    struct rtw_rx_pkt_stat *pkt_stat,
			    struct ieee80211_hdr *hdr)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_pkt_count *cur_pkt_cnt = &dm_info->cur_pkt_count;
	u8 rate_ss, rate_ss_evm, evm_id;
	u8 i, idx;

	dm_info->curr_rx_rate = pkt_stat->rate;

	if (ieee80211_is_beacon(hdr->frame_control))
		cur_pkt_cnt->num_bcn_pkt++;

	switch (pkt_stat->rate) {
	case DESC_RATE1M...DESC_RATE11M:
		goto pkt_num;
	case DESC_RATE6M...DESC_RATE54M:
		rate_ss = 0;
		rate_ss_evm = 1;
		evm_id = RTW_EVM_OFDM;
		break;
	case DESC_RATEMCS0...DESC_RATEMCS7:
	case DESC_RATEVHT1SS_MCS0...DESC_RATEVHT1SS_MCS9:
		rate_ss = 1;
		rate_ss_evm = 1;
		evm_id = RTW_EVM_1SS;
		break;
	case DESC_RATEMCS8...DESC_RATEMCS15:
	case DESC_RATEVHT2SS_MCS0...DESC_RATEVHT2SS_MCS9:
		rate_ss = 2;
		rate_ss_evm = 2;
		evm_id = RTW_EVM_2SS_A;
		break;
	default:
		rtw_warn(rtwdev, "unknown pkt rate = %d\n", pkt_stat->rate);
		return;
	}

	for (i = 0; i < rate_ss_evm; i++) {
		idx = evm_id + i;
		ewma_evm_add(&dm_info->ewma_evm[idx],
			     dm_info->rx_evm_dbm[i]);
	}

	for (i = 0; i < rtwdev->hal.rf_path_num; i++) {
		idx = RTW_SNR_OFDM_A + 4 * rate_ss + i;
		ewma_snr_add(&dm_info->ewma_snr[idx],
			     dm_info->rx_snr[i]);
	}
pkt_num:
	cur_pkt_cnt->num_qry_pkt[pkt_stat->rate]++;
}

static void rtw_rx_addr_match_iter(void *data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct rtw_rx_addr_match_data *iter_data = data;
	struct ieee80211_sta *sta;
	struct ieee80211_hdr *hdr = iter_data->hdr;
	struct rtw_dev *rtwdev = iter_data->rtwdev;
	struct rtw_sta_info *si;
	struct rtw_rx_pkt_stat *pkt_stat = iter_data->pkt_stat;
	u8 *bssid = iter_data->bssid;

	if (!ether_addr_equal(vif->bss_conf.bssid, bssid))
		return;

	if (!(ether_addr_equal(vif->addr, hdr->addr1) ||
	      ieee80211_is_beacon(hdr->frame_control)))
		return;

	rtw_rx_phy_stat(rtwdev, pkt_stat, hdr);
	sta = ieee80211_find_sta_by_ifaddr(rtwdev->hw, hdr->addr2,
					   vif->addr);
	if (!sta)
		return;

	si = (struct rtw_sta_info *)sta->drv_priv;
	ewma_rssi_add(&si->avg_rssi, pkt_stat->rssi);
}

static void rtw_rx_addr_match(struct rtw_dev *rtwdev,
			      struct rtw_rx_pkt_stat *pkt_stat,
			      struct ieee80211_hdr *hdr)
{
	struct rtw_rx_addr_match_data data = {};

	if (pkt_stat->crc_err || pkt_stat->icv_err || !pkt_stat->phy_status ||
	    ieee80211_is_ctl(hdr->frame_control))
		return;

	data.rtwdev = rtwdev;
	data.hdr = hdr;
	data.pkt_stat = pkt_stat;
	data.bssid = get_hdr_bssid(hdr);

	rtw_iterate_vifs_atomic(rtwdev, rtw_rx_addr_match_iter, &data);
}

static void rtw_set_rx_freq_by_pktstat(struct rtw_rx_pkt_stat *pkt_stat,
				       struct ieee80211_rx_status *rx_status)
{
	rx_status->freq = pkt_stat->freq;
	rx_status->band = pkt_stat->band;
}

void rtw_rx_fill_rx_status(struct rtw_dev *rtwdev,
			   struct rtw_rx_pkt_stat *pkt_stat,
			   struct ieee80211_hdr *hdr,
			   struct ieee80211_rx_status *rx_status,
			   u8 *phy_status)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	u8 path;

	memset(rx_status, 0, sizeof(*rx_status));
	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;
	if (rtw_fw_feature_check(&rtwdev->fw, FW_FEATURE_SCAN_OFFLOAD))
		rtw_set_rx_freq_by_pktstat(pkt_stat, rx_status);
	if (pkt_stat->crc_err)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	if (pkt_stat->decrypted)
		rx_status->flag |= RX_FLAG_DECRYPTED;

	if (pkt_stat->rate >= DESC_RATEVHT1SS_MCS0)
		rx_status->encoding = RX_ENC_VHT;
	else if (pkt_stat->rate >= DESC_RATEMCS0)
		rx_status->encoding = RX_ENC_HT;

	if (rx_status->band == NL80211_BAND_5GHZ &&
	    pkt_stat->rate >= DESC_RATE6M &&
	    pkt_stat->rate <= DESC_RATE54M) {
		rx_status->rate_idx = pkt_stat->rate - DESC_RATE6M;
	} else if (rx_status->band == NL80211_BAND_2GHZ &&
		   pkt_stat->rate >= DESC_RATE1M &&
		   pkt_stat->rate <= DESC_RATE54M) {
		rx_status->rate_idx = pkt_stat->rate - DESC_RATE1M;
	} else if (pkt_stat->rate >= DESC_RATEMCS0) {
		rtw_desc_to_mcsrate(pkt_stat->rate, &rx_status->rate_idx,
				    &rx_status->nss);
	}

	rx_status->flag |= RX_FLAG_MACTIME_START;
	rx_status->mactime = pkt_stat->tsf_low;

	if (pkt_stat->bw == RTW_CHANNEL_WIDTH_80)
		rx_status->bw = RATE_INFO_BW_80;
	else if (pkt_stat->bw == RTW_CHANNEL_WIDTH_40)
		rx_status->bw = RATE_INFO_BW_40;
	else
		rx_status->bw = RATE_INFO_BW_20;

	rx_status->signal = pkt_stat->signal_power;
	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		rx_status->chains |= BIT(path);
		rx_status->chain_signal[path] = pkt_stat->rx_power[path];
	}

	rtw_rx_addr_match(rtwdev, pkt_stat, hdr);
}
EXPORT_SYMBOL(rtw_rx_fill_rx_status);
