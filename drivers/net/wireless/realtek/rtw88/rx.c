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

void rtw_update_rx_freq_from_ie(struct rtw_dev *rtwdev, struct sk_buff *skb,
				struct ieee80211_rx_status *rx_status,
				struct rtw_rx_pkt_stat *pkt_stat)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	int channel = rtwdev->hal.current_channel;
	size_t hdr_len, ielen;
	int channel_number;
	u8 *variable;

	if (!test_bit(RTW_FLAG_SCANNING, rtwdev->flags))
		goto fill_rx_status;

	if (ieee80211_is_beacon(mgmt->frame_control)) {
		variable = mgmt->u.beacon.variable;
		hdr_len = offsetof(struct ieee80211_mgmt,
				   u.beacon.variable);
	} else if (ieee80211_is_probe_resp(mgmt->frame_control)) {
		variable = mgmt->u.probe_resp.variable;
		hdr_len = offsetof(struct ieee80211_mgmt,
				   u.probe_resp.variable);
	} else {
		goto fill_rx_status;
	}

	if (skb->len > hdr_len)
		ielen = skb->len - hdr_len;
	else
		goto fill_rx_status;

	channel_number = cfg80211_get_ies_channel_number(variable, ielen,
							 NL80211_BAND_2GHZ);
	if (channel_number != -1)
		channel = channel_number;

fill_rx_status:
	rtw_set_rx_freq_band(pkt_stat, channel);
	rtw_set_rx_freq_by_pktstat(pkt_stat, rx_status);
}
EXPORT_SYMBOL(rtw_update_rx_freq_from_ie);

static void rtw_rx_fill_rx_status(struct rtw_dev *rtwdev,
				  struct rtw_rx_pkt_stat *pkt_stat,
				  struct ieee80211_hdr *hdr,
				  struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	u8 path;

	memset(rx_status, 0, sizeof(*rx_status));
	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;
	if (rtw_fw_feature_check(&rtwdev->fw, FW_FEATURE_SCAN_OFFLOAD) &&
	    test_bit(RTW_FLAG_SCANNING, rtwdev->flags))
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

	if (pkt_stat->phy_status) {
		rx_status->signal = pkt_stat->signal_power;
		for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
			rx_status->chains |= BIT(path);
			rx_status->chain_signal[path] = pkt_stat->rx_power[path];
		}
	} else {
		rx_status->flag |= RX_FLAG_NO_SIGNAL_VAL;
	}

	rtw_rx_addr_match(rtwdev, pkt_stat, hdr);

	/* Rtl8723cs driver checks for size < 14 or size > 8192 and
	 * simply drops the packet.
	 */
	if (rtwdev->chip->id == RTW_CHIP_TYPE_8703B && pkt_stat->pkt_len == 0) {
		rx_status->flag |= RX_FLAG_NO_PSDU;
		rtw_dbg(rtwdev, RTW_DBG_RX, "zero length packet");
	}
}

void rtw_rx_query_rx_desc(struct rtw_dev *rtwdev, void *rx_desc8,
			  struct rtw_rx_pkt_stat *pkt_stat,
			  struct ieee80211_rx_status *rx_status)
{
	u32 desc_sz = rtwdev->chip->rx_pkt_desc_sz;
	struct rtw_rx_desc *rx_desc = rx_desc8;
	struct ieee80211_hdr *hdr;
	u32 enc_type, swdec;
	void *phy_status;

	memset(pkt_stat, 0, sizeof(*pkt_stat));

	pkt_stat->pkt_len = le32_get_bits(rx_desc->w0, RTW_RX_DESC_W0_PKT_LEN);
	pkt_stat->crc_err = le32_get_bits(rx_desc->w0, RTW_RX_DESC_W0_CRC32);
	pkt_stat->icv_err = le32_get_bits(rx_desc->w0, RTW_RX_DESC_W0_ICV_ERR);
	pkt_stat->drv_info_sz = le32_get_bits(rx_desc->w0,
					      RTW_RX_DESC_W0_DRV_INFO_SIZE);
	enc_type = le32_get_bits(rx_desc->w0, RTW_RX_DESC_W0_ENC_TYPE);
	pkt_stat->shift = le32_get_bits(rx_desc->w0, RTW_RX_DESC_W0_SHIFT);
	pkt_stat->phy_status = le32_get_bits(rx_desc->w0, RTW_RX_DESC_W0_PHYST);
	swdec = le32_get_bits(rx_desc->w0, RTW_RX_DESC_W0_SWDEC);
	pkt_stat->decrypted = !swdec && enc_type != RX_DESC_ENC_NONE;

	pkt_stat->cam_id = le32_get_bits(rx_desc->w1, RTW_RX_DESC_W1_MACID);

	pkt_stat->is_c2h = le32_get_bits(rx_desc->w2, RTW_RX_DESC_W2_C2H);
	pkt_stat->ppdu_cnt = le32_get_bits(rx_desc->w2, RTW_RX_DESC_W2_PPDU_CNT);

	pkt_stat->rate = le32_get_bits(rx_desc->w3, RTW_RX_DESC_W3_RX_RATE);

	pkt_stat->bw = le32_get_bits(rx_desc->w4, RTW_RX_DESC_W4_BW);

	pkt_stat->tsf_low = le32_get_bits(rx_desc->w5, RTW_RX_DESC_W5_TSFL);

	/* drv_info_sz is in unit of 8-bytes */
	pkt_stat->drv_info_sz *= 8;

	/* c2h cmd pkt's rx/phy status is not interested */
	if (pkt_stat->is_c2h)
		return;

	phy_status = rx_desc8 + desc_sz + pkt_stat->shift;
	hdr = phy_status + pkt_stat->drv_info_sz;
	pkt_stat->hdr = hdr;

	if (pkt_stat->phy_status)
		rtwdev->chip->ops->query_phy_status(rtwdev, phy_status, pkt_stat);

	rtw_rx_fill_rx_status(rtwdev, pkt_stat, hdr, rx_status);
}
EXPORT_SYMBOL(rtw_rx_query_rx_desc);
