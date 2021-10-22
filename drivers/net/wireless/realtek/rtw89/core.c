// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "coex.h"
#include "core.h"
#include "efuse.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "ps.h"
#include "reg.h"
#include "sar.h"
#include "ser.h"
#include "txrx.h"
#include "util.h"

static bool rtw89_disable_ps_mode;
module_param_named(disable_ps_mode, rtw89_disable_ps_mode, bool, 0644);
MODULE_PARM_DESC(disable_ps_mode, "Set Y to disable low power mode");

static struct ieee80211_channel rtw89_channels_2ghz[] = {
	{ .center_freq = 2412, .hw_value = 1, },
	{ .center_freq = 2417, .hw_value = 2, },
	{ .center_freq = 2422, .hw_value = 3, },
	{ .center_freq = 2427, .hw_value = 4, },
	{ .center_freq = 2432, .hw_value = 5, },
	{ .center_freq = 2437, .hw_value = 6, },
	{ .center_freq = 2442, .hw_value = 7, },
	{ .center_freq = 2447, .hw_value = 8, },
	{ .center_freq = 2452, .hw_value = 9, },
	{ .center_freq = 2457, .hw_value = 10, },
	{ .center_freq = 2462, .hw_value = 11, },
	{ .center_freq = 2467, .hw_value = 12, },
	{ .center_freq = 2472, .hw_value = 13, },
	{ .center_freq = 2484, .hw_value = 14, },
};

static struct ieee80211_channel rtw89_channels_5ghz[] = {
	{.center_freq = 5180, .hw_value = 36,},
	{.center_freq = 5200, .hw_value = 40,},
	{.center_freq = 5220, .hw_value = 44,},
	{.center_freq = 5240, .hw_value = 48,},
	{.center_freq = 5260, .hw_value = 52,},
	{.center_freq = 5280, .hw_value = 56,},
	{.center_freq = 5300, .hw_value = 60,},
	{.center_freq = 5320, .hw_value = 64,},
	{.center_freq = 5500, .hw_value = 100,},
	{.center_freq = 5520, .hw_value = 104,},
	{.center_freq = 5540, .hw_value = 108,},
	{.center_freq = 5560, .hw_value = 112,},
	{.center_freq = 5580, .hw_value = 116,},
	{.center_freq = 5600, .hw_value = 120,},
	{.center_freq = 5620, .hw_value = 124,},
	{.center_freq = 5640, .hw_value = 128,},
	{.center_freq = 5660, .hw_value = 132,},
	{.center_freq = 5680, .hw_value = 136,},
	{.center_freq = 5700, .hw_value = 140,},
	{.center_freq = 5720, .hw_value = 144,},
	{.center_freq = 5745, .hw_value = 149,},
	{.center_freq = 5765, .hw_value = 153,},
	{.center_freq = 5785, .hw_value = 157,},
	{.center_freq = 5805, .hw_value = 161,},
	{.center_freq = 5825, .hw_value = 165,
	 .flags = IEEE80211_CHAN_NO_HT40MINUS},
};

static struct ieee80211_rate rtw89_bitrates[] = {
	{ .bitrate = 10,  .hw_value = 0x00, },
	{ .bitrate = 20,  .hw_value = 0x01, },
	{ .bitrate = 55,  .hw_value = 0x02, },
	{ .bitrate = 110, .hw_value = 0x03, },
	{ .bitrate = 60,  .hw_value = 0x04, },
	{ .bitrate = 90,  .hw_value = 0x05, },
	{ .bitrate = 120, .hw_value = 0x06, },
	{ .bitrate = 180, .hw_value = 0x07, },
	{ .bitrate = 240, .hw_value = 0x08, },
	{ .bitrate = 360, .hw_value = 0x09, },
	{ .bitrate = 480, .hw_value = 0x0a, },
	{ .bitrate = 540, .hw_value = 0x0b, },
};

u16 rtw89_ra_report_to_bitrate(struct rtw89_dev *rtwdev, u8 rpt_rate)
{
	struct ieee80211_rate rate;

	if (unlikely(rpt_rate >= ARRAY_SIZE(rtw89_bitrates))) {
		rtw89_info(rtwdev, "invalid rpt rate %d\n", rpt_rate);
		return 0;
	}

	rate = rtw89_bitrates[rpt_rate];

	return rate.bitrate;
}

static struct ieee80211_supported_band rtw89_sband_2ghz = {
	.band		= NL80211_BAND_2GHZ,
	.channels	= rtw89_channels_2ghz,
	.n_channels	= ARRAY_SIZE(rtw89_channels_2ghz),
	.bitrates	= rtw89_bitrates,
	.n_bitrates	= ARRAY_SIZE(rtw89_bitrates),
	.ht_cap		= {0},
	.vht_cap	= {0},
};

static struct ieee80211_supported_band rtw89_sband_5ghz = {
	.band		= NL80211_BAND_5GHZ,
	.channels	= rtw89_channels_5ghz,
	.n_channels	= ARRAY_SIZE(rtw89_channels_5ghz),

	/* 5G has no CCK rates, 1M/2M/5.5M/11M */
	.bitrates	= rtw89_bitrates + 4,
	.n_bitrates	= ARRAY_SIZE(rtw89_bitrates) - 4,
	.ht_cap		= {0},
	.vht_cap	= {0},
};

static void rtw89_traffic_stats_accu(struct rtw89_dev *rtwdev,
				     struct rtw89_traffic_stats *stats,
				     struct sk_buff *skb, bool tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (!ieee80211_is_data(hdr->frame_control))
		return;

	if (is_broadcast_ether_addr(hdr->addr1) ||
	    is_multicast_ether_addr(hdr->addr1))
		return;

	if (tx) {
		stats->tx_cnt++;
		stats->tx_unicast += skb->len;
	} else {
		stats->rx_cnt++;
		stats->rx_unicast += skb->len;
	}
}

static void rtw89_get_channel_params(struct cfg80211_chan_def *chandef,
				     struct rtw89_channel_params *chan_param)
{
	struct ieee80211_channel *channel = chandef->chan;
	enum nl80211_chan_width width = chandef->width;
	u8 *cch_by_bw = chan_param->cch_by_bw;
	u32 primary_freq, center_freq;
	u8 center_chan;
	u8 bandwidth = RTW89_CHANNEL_WIDTH_20;
	u8 primary_chan_idx = 0;
	u8 i;

	center_chan = channel->hw_value;
	primary_freq = channel->center_freq;
	center_freq = chandef->center_freq1;

	/* assign the center channel used while 20M bw is selected */
	cch_by_bw[RTW89_CHANNEL_WIDTH_20] = channel->hw_value;

	switch (width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		bandwidth = RTW89_CHANNEL_WIDTH_20;
		primary_chan_idx = RTW89_SC_DONT_CARE;
		break;
	case NL80211_CHAN_WIDTH_40:
		bandwidth = RTW89_CHANNEL_WIDTH_40;
		if (primary_freq > center_freq) {
			primary_chan_idx = RTW89_SC_20_UPPER;
			center_chan -= 2;
		} else {
			primary_chan_idx = RTW89_SC_20_LOWER;
			center_chan += 2;
		}
		break;
	case NL80211_CHAN_WIDTH_80:
		bandwidth = RTW89_CHANNEL_WIDTH_80;
		if (primary_freq > center_freq) {
			if (primary_freq - center_freq == 10) {
				primary_chan_idx = RTW89_SC_20_UPPER;
				center_chan -= 2;
			} else {
				primary_chan_idx = RTW89_SC_20_UPMOST;
				center_chan -= 6;
			}
			/* assign the center channel used
			 * while 40M bw is selected
			 */
			cch_by_bw[RTW89_CHANNEL_WIDTH_40] = center_chan + 4;
		} else {
			if (center_freq - primary_freq == 10) {
				primary_chan_idx = RTW89_SC_20_LOWER;
				center_chan += 2;
			} else {
				primary_chan_idx = RTW89_SC_20_LOWEST;
				center_chan += 6;
			}
			/* assign the center channel used
			 * while 40M bw is selected
			 */
			cch_by_bw[RTW89_CHANNEL_WIDTH_40] = center_chan - 4;
		}
		break;
	default:
		center_chan = 0;
		break;
	}

	chan_param->center_chan = center_chan;
	chan_param->primary_chan = channel->hw_value;
	chan_param->bandwidth = bandwidth;
	chan_param->pri_ch_idx = primary_chan_idx;

	/* assign the center channel used while current bw is selected */
	cch_by_bw[bandwidth] = center_chan;

	for (i = bandwidth + 1; i <= RTW89_MAX_CHANNEL_WIDTH; i++)
		cch_by_bw[i] = 0;
}

void rtw89_set_channel(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_channel_params ch_param;
	struct rtw89_channel_help_params bak;
	u8 center_chan, bandwidth;
	u8 band_type;
	bool band_changed;
	u8 i;

	rtw89_get_channel_params(&hw->conf.chandef, &ch_param);
	if (WARN(ch_param.center_chan == 0, "Invalid channel\n"))
		return;

	center_chan = ch_param.center_chan;
	bandwidth = ch_param.bandwidth;
	band_type = center_chan > 14 ? RTW89_BAND_5G : RTW89_BAND_2G;
	band_changed = hal->current_band_type != band_type ||
		       hal->current_channel == 0;

	hal->current_band_width = bandwidth;
	hal->current_channel = center_chan;
	hal->current_primary_channel = ch_param.primary_chan;
	hal->current_band_type = band_type;

	switch (center_chan) {
	case 1 ... 14:
		hal->current_subband = RTW89_CH_2G;
		break;
	case 36 ... 64:
		hal->current_subband = RTW89_CH_5G_BAND_1;
		break;
	case 100 ... 144:
		hal->current_subband = RTW89_CH_5G_BAND_3;
		break;
	case 149 ... 177:
		hal->current_subband = RTW89_CH_5G_BAND_4;
		break;
	}

	for (i = RTW89_CHANNEL_WIDTH_20; i <= RTW89_MAX_CHANNEL_WIDTH; i++)
		hal->cch_by_bw[i] = ch_param.cch_by_bw[i];

	rtw89_chip_set_channel_prepare(rtwdev, &bak);

	chip->ops->set_channel(rtwdev, &ch_param);

	rtw89_chip_set_txpwr(rtwdev);

	rtw89_chip_set_channel_done(rtwdev, &bak);

	if (band_changed) {
		rtw89_btc_ntfy_switch_band(rtwdev, RTW89_PHY_0, hal->current_band_type);
		rtw89_chip_rfk_band_changed(rtwdev);
	}
}

static enum rtw89_core_tx_type
rtw89_core_get_tx_type(struct rtw89_dev *rtwdev,
		       struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;

	if (ieee80211_is_mgmt(fc) || ieee80211_is_nullfunc(fc))
		return RTW89_CORE_TX_TYPE_MGMT;

	return RTW89_CORE_TX_TYPE_DATA;
}

static void
rtw89_core_tx_update_ampdu_info(struct rtw89_dev *rtwdev,
				struct rtw89_core_tx_request *tx_req, u8 tid)
{
	struct ieee80211_sta *sta = tx_req->sta;
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct rtw89_sta *rtwsta;
	u8 ampdu_num;

	if (!sta) {
		rtw89_warn(rtwdev, "cannot set ampdu info without sta\n");
		return;
	}

	rtwsta = (struct rtw89_sta *)sta->drv_priv;

	ampdu_num = (u8)((rtwsta->ampdu_params[tid].agg_num ?
			  rtwsta->ampdu_params[tid].agg_num :
			  4 << sta->ht_cap.ampdu_factor) - 1);

	desc_info->agg_en = true;
	desc_info->ampdu_density = sta->ht_cap.ampdu_density;
	desc_info->ampdu_num = ampdu_num;
}

static void
rtw89_core_tx_update_sec_key(struct rtw89_dev *rtwdev,
			     struct rtw89_core_tx_request *tx_req)
{
	struct ieee80211_vif *vif = tx_req->vif;
	struct ieee80211_tx_info *info;
	struct ieee80211_key_conf *key;
	struct rtw89_vif *rtwvif;
	struct rtw89_addr_cam_entry *addr_cam;
	struct rtw89_sec_cam_entry *sec_cam;
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct sk_buff *skb = tx_req->skb;
	u8 sec_type = RTW89_SEC_KEY_TYPE_NONE;

	if (!vif) {
		rtw89_warn(rtwdev, "cannot set sec key without vif\n");
		return;
	}

	rtwvif = (struct rtw89_vif *)vif->drv_priv;
	addr_cam = &rtwvif->addr_cam;

	info = IEEE80211_SKB_CB(skb);
	key = info->control.hw_key;
	sec_cam = addr_cam->sec_entries[key->hw_key_idx];
	if (!sec_cam) {
		rtw89_warn(rtwdev, "sec cam entry is empty\n");
		return;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		sec_type = RTW89_SEC_KEY_TYPE_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		sec_type = RTW89_SEC_KEY_TYPE_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		sec_type = RTW89_SEC_KEY_TYPE_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		sec_type = RTW89_SEC_KEY_TYPE_CCMP128;
		break;
	case WLAN_CIPHER_SUITE_CCMP_256:
		sec_type = RTW89_SEC_KEY_TYPE_CCMP256;
		break;
	case WLAN_CIPHER_SUITE_GCMP:
		sec_type = RTW89_SEC_KEY_TYPE_GCMP128;
		break;
	case WLAN_CIPHER_SUITE_GCMP_256:
		sec_type = RTW89_SEC_KEY_TYPE_GCMP256;
		break;
	default:
		rtw89_warn(rtwdev, "key cipher not supported %d\n", key->cipher);
		return;
	}

	desc_info->sec_en = true;
	desc_info->sec_type = sec_type;
	desc_info->sec_cam_idx = sec_cam->sec_cam_idx;
}

static u16 rtw89_core_get_mgmt_rate(struct rtw89_dev *rtwdev,
				    struct rtw89_core_tx_request *tx_req)
{
	struct sk_buff *skb = tx_req->skb;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = tx_info->control.vif;
	struct rtw89_hal *hal = &rtwdev->hal;
	u16 lowest_rate = hal->current_band_type == RTW89_BAND_2G ?
			  RTW89_HW_RATE_CCK1 : RTW89_HW_RATE_OFDM6;

	if (!vif || !vif->bss_conf.basic_rates || !tx_req->sta)
		return lowest_rate;

	return __ffs(vif->bss_conf.basic_rates) + lowest_rate;
}

static void
rtw89_core_tx_update_mgmt_info(struct rtw89_dev *rtwdev,
			       struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	u8 qsel, ch_dma;

	qsel = RTW89_TX_QSEL_B0_MGMT;
	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	desc_info->qsel = RTW89_TX_QSEL_B0_MGMT;
	desc_info->ch_dma = ch_dma;

	/* fixed data rate for mgmt frames */
	desc_info->en_wd_info = true;
	desc_info->use_rate = true;
	desc_info->dis_data_fb = true;
	desc_info->data_rate = rtw89_core_get_mgmt_rate(rtwdev, tx_req);

	rtw89_debug(rtwdev, RTW89_DBG_TXRX,
		    "tx mgmt frame with rate 0x%x on channel %d (bw %d)\n",
		    desc_info->data_rate, rtwdev->hal.current_channel,
		    rtwdev->hal.current_band_width);
}

static void
rtw89_core_tx_update_h2c_info(struct rtw89_dev *rtwdev,
			      struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;

	desc_info->is_bmc = false;
	desc_info->wd_page = false;
	desc_info->ch_dma = RTW89_DMA_H2C;
}

static void rtw89_core_get_no_ul_ofdma_htc(struct rtw89_dev *rtwdev, __le32 *htc)
{
	static const u8 rtw89_bandwidth_to_om[] = {
		[RTW89_CHANNEL_WIDTH_20] = HTC_OM_CHANNEL_WIDTH_20,
		[RTW89_CHANNEL_WIDTH_40] = HTC_OM_CHANNEL_WIDTH_40,
		[RTW89_CHANNEL_WIDTH_80] = HTC_OM_CHANNEL_WIDTH_80,
		[RTW89_CHANNEL_WIDTH_160] = HTC_OM_CHANNEL_WIDTH_160_OR_80_80,
		[RTW89_CHANNEL_WIDTH_80_80] = HTC_OM_CHANNEL_WIDTH_160_OR_80_80,
	};
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 om_bandwidth;

	if (!chip->dis_2g_40m_ul_ofdma ||
	    hal->current_band_type != RTW89_BAND_2G ||
	    hal->current_band_width != RTW89_CHANNEL_WIDTH_40)
		return;

	om_bandwidth = hal->current_band_width < ARRAY_SIZE(rtw89_bandwidth_to_om) ?
		       rtw89_bandwidth_to_om[hal->current_band_width] : 0;
	*htc = le32_encode_bits(RTW89_HTC_VARIANT_HE, RTW89_HTC_MASK_VARIANT) |
	       le32_encode_bits(RTW89_HTC_VARIANT_HE_CID_OM, RTW89_HTC_MASK_CTL_ID) |
	       le32_encode_bits(hal->rx_nss - 1, RTW89_HTC_MASK_HTC_OM_RX_NSS) |
	       le32_encode_bits(om_bandwidth, RTW89_HTC_MASK_HTC_OM_CH_WIDTH) |
	       le32_encode_bits(1, RTW89_HTC_MASK_HTC_OM_UL_MU_DIS) |
	       le32_encode_bits(hal->tx_nss - 1, RTW89_HTC_MASK_HTC_OM_TX_NSTS) |
	       le32_encode_bits(0, RTW89_HTC_MASK_HTC_OM_ER_SU_DIS) |
	       le32_encode_bits(0, RTW89_HTC_MASK_HTC_OM_DL_MU_MIMO_RR) |
	       le32_encode_bits(0, RTW89_HTC_MASK_HTC_OM_UL_MU_DATA_DIS);
}

static bool
__rtw89_core_tx_check_he_qos_htc(struct rtw89_dev *rtwdev,
				 struct rtw89_core_tx_request *tx_req,
				 enum btc_pkt_type pkt_type)
{
	struct ieee80211_sta *sta = tx_req->sta;
	struct sk_buff *skb = tx_req->skb;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;

	/* AP IOT issue with EAPoL, ARP and DHCP */
	if (pkt_type < PACKET_MAX)
		return false;

	if (!sta || !sta->he_cap.has_he)
		return false;

	if (!ieee80211_is_data_qos(fc))
		return false;

	if (skb_headroom(skb) < IEEE80211_HT_CTL_LEN)
		return false;

	return true;
}

static void
__rtw89_core_tx_adjust_he_qos_htc(struct rtw89_dev *rtwdev,
				  struct rtw89_core_tx_request *tx_req)
{
	struct ieee80211_sta *sta = tx_req->sta;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	struct sk_buff *skb = tx_req->skb;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;
	void *data;
	__le32 *htc;
	u8 *qc;
	int hdr_len;

	hdr_len = ieee80211_has_a4(fc) ? 32 : 26;
	data = skb_push(skb, IEEE80211_HT_CTL_LEN);
	memmove(data, data + IEEE80211_HT_CTL_LEN, hdr_len);

	hdr = data;
	htc = data + hdr_len;
	hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_ORDER);
	*htc = rtwsta->htc_template ? rtwsta->htc_template :
	       le32_encode_bits(RTW89_HTC_VARIANT_HE, RTW89_HTC_MASK_VARIANT) |
	       le32_encode_bits(RTW89_HTC_VARIANT_HE_CID_CAS, RTW89_HTC_MASK_CTL_ID);

	qc = data + hdr_len - IEEE80211_QOS_CTL_LEN;
	qc[0] |= IEEE80211_QOS_CTL_EOSP;
}

static void
rtw89_core_tx_update_he_qos_htc(struct rtw89_dev *rtwdev,
				struct rtw89_core_tx_request *tx_req,
				enum btc_pkt_type pkt_type)
{
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct ieee80211_vif *vif = tx_req->vif;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	if (!__rtw89_core_tx_check_he_qos_htc(rtwdev, tx_req, pkt_type))
		goto desc_bk;

	__rtw89_core_tx_adjust_he_qos_htc(rtwdev, tx_req);

	desc_info->pkt_size += IEEE80211_HT_CTL_LEN;
	desc_info->a_ctrl_bsr = true;

desc_bk:
	if (!rtwvif || rtwvif->last_a_ctrl == desc_info->a_ctrl_bsr)
		return;

	rtwvif->last_a_ctrl = desc_info->a_ctrl_bsr;
	desc_info->bk = true;
}

static void
rtw89_core_tx_update_data_info(struct rtw89_dev *rtwdev,
			       struct rtw89_core_tx_request *tx_req)
{
	struct ieee80211_vif *vif = tx_req->vif;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	struct rtw89_phy_rate_pattern *rate_pattern = &rtwvif->rate_pattern;
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct sk_buff *skb = tx_req->skb;
	u8 tid, tid_indicate;
	u8 qsel, ch_dma;

	tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
	tid_indicate = rtw89_core_get_tid_indicate(rtwdev, tid);
	qsel = rtw89_core_get_qsel(rtwdev, tid);
	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	desc_info->ch_dma = ch_dma;
	desc_info->tid_indicate = tid_indicate;
	desc_info->qsel = qsel;

	/* enable wd_info for AMPDU */
	desc_info->en_wd_info = true;

	if (IEEE80211_SKB_CB(skb)->flags & IEEE80211_TX_CTL_AMPDU)
		rtw89_core_tx_update_ampdu_info(rtwdev, tx_req, tid);
	if (IEEE80211_SKB_CB(skb)->control.hw_key)
		rtw89_core_tx_update_sec_key(rtwdev, tx_req);

	if (rate_pattern->enable)
		desc_info->data_retry_lowest_rate = rate_pattern->rate;
	else if (hal->current_band_type == RTW89_BAND_2G)
		desc_info->data_retry_lowest_rate = RTW89_HW_RATE_CCK1;
	else
		desc_info->data_retry_lowest_rate = RTW89_HW_RATE_OFDM6;
}

static enum btc_pkt_type
rtw89_core_tx_btc_spec_pkt_notify(struct rtw89_dev *rtwdev,
				  struct rtw89_core_tx_request *tx_req)
{
	struct sk_buff *skb = tx_req->skb;
	struct udphdr *udphdr;

	if (IEEE80211_SKB_CB(skb)->control.flags & IEEE80211_TX_CTRL_PORT_CTRL_PROTO) {
		ieee80211_queue_work(rtwdev->hw, &rtwdev->btc.eapol_notify_work);
		return PACKET_EAPOL;
	}

	if (skb->protocol == htons(ETH_P_ARP)) {
		ieee80211_queue_work(rtwdev->hw, &rtwdev->btc.arp_notify_work);
		return PACKET_ARP;
	}

	if (skb->protocol == htons(ETH_P_IP) &&
	    ip_hdr(skb)->protocol == IPPROTO_UDP) {
		udphdr = udp_hdr(skb);
		if (((udphdr->source == htons(67) && udphdr->dest == htons(68)) ||
		     (udphdr->source == htons(68) && udphdr->dest == htons(67))) &&
		    skb->len > 282) {
			ieee80211_queue_work(rtwdev->hw, &rtwdev->btc.dhcp_notify_work);
			return PACKET_DHCP;
		}
	}

	if (skb->protocol == htons(ETH_P_IP) &&
	    ip_hdr(skb)->protocol == IPPROTO_ICMP) {
		ieee80211_queue_work(rtwdev->hw, &rtwdev->btc.icmp_notify_work);
		return PACKET_ICMP;
	}

	return PACKET_MAX;
}

static void
rtw89_core_tx_update_desc_info(struct rtw89_dev *rtwdev,
			       struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct sk_buff *skb = tx_req->skb;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	enum rtw89_core_tx_type tx_type;
	enum btc_pkt_type pkt_type;
	bool is_bmc;
	u16 seq;

	seq = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;
	if (tx_req->tx_type != RTW89_CORE_TX_TYPE_FWCMD) {
		tx_type = rtw89_core_get_tx_type(rtwdev, skb);
		tx_req->tx_type = tx_type;
	}
	is_bmc = (is_broadcast_ether_addr(hdr->addr1) ||
		  is_multicast_ether_addr(hdr->addr1));

	desc_info->seq = seq;
	desc_info->pkt_size = skb->len;
	desc_info->is_bmc = is_bmc;
	desc_info->wd_page = true;

	switch (tx_req->tx_type) {
	case RTW89_CORE_TX_TYPE_MGMT:
		rtw89_core_tx_update_mgmt_info(rtwdev, tx_req);
		break;
	case RTW89_CORE_TX_TYPE_DATA:
		rtw89_core_tx_update_data_info(rtwdev, tx_req);
		pkt_type = rtw89_core_tx_btc_spec_pkt_notify(rtwdev, tx_req);
		rtw89_core_tx_update_he_qos_htc(rtwdev, tx_req, pkt_type);
		break;
	case RTW89_CORE_TX_TYPE_FWCMD:
		rtw89_core_tx_update_h2c_info(rtwdev, tx_req);
		break;
	}
}

void rtw89_core_tx_kick_off(struct rtw89_dev *rtwdev, u8 qsel)
{
	u8 ch_dma;

	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	rtw89_hci_tx_kick_off(rtwdev, ch_dma);
}

int rtw89_h2c_tx(struct rtw89_dev *rtwdev,
		 struct sk_buff *skb, bool fwdl)
{
	struct rtw89_core_tx_request tx_req = {0};
	u32 cnt;
	int ret;

	tx_req.skb = skb;
	tx_req.tx_type = RTW89_CORE_TX_TYPE_FWCMD;
	if (fwdl)
		tx_req.desc_info.fw_dl = true;

	rtw89_core_tx_update_desc_info(rtwdev, &tx_req);

	if (!fwdl)
		rtw89_hex_dump(rtwdev, RTW89_DBG_FW, "H2C: ", skb->data, skb->len);

	cnt = rtw89_hci_check_and_reclaim_tx_resource(rtwdev, RTW89_TXCH_CH12);
	if (cnt == 0) {
		rtw89_err(rtwdev, "no tx fwcmd resource\n");
		return -ENOSPC;
	}

	ret = rtw89_hci_tx_write(rtwdev, &tx_req);
	if (ret) {
		rtw89_err(rtwdev, "failed to transmit skb to HCI\n");
		return ret;
	}
	rtw89_hci_tx_kick_off(rtwdev, RTW89_TXCH_CH12);

	return 0;
}

int rtw89_core_tx_write(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, struct sk_buff *skb, int *qsel)
{
	struct rtw89_core_tx_request tx_req = {0};
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	int ret;

	tx_req.skb = skb;
	tx_req.sta = sta;
	tx_req.vif = vif;

	rtw89_traffic_stats_accu(rtwdev, &rtwdev->stats, skb, true);
	rtw89_traffic_stats_accu(rtwdev, &rtwvif->stats, skb, true);
	rtw89_core_tx_update_desc_info(rtwdev, &tx_req);
	ret = rtw89_hci_tx_write(rtwdev, &tx_req);
	if (ret) {
		rtw89_err(rtwdev, "failed to transmit skb to HCI\n");
		return ret;
	}

	if (qsel)
		*qsel = tx_req.desc_info.qsel;

	return 0;
}

static __le32 rtw89_build_txwd_body0(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_BODY0_WP_OFFSET, desc_info->wp_offset) |
		    FIELD_PREP(RTW89_TXWD_BODY0_WD_INFO_EN, desc_info->en_wd_info) |
		    FIELD_PREP(RTW89_TXWD_BODY0_CHANNEL_DMA, desc_info->ch_dma) |
		    FIELD_PREP(RTW89_TXWD_BODY0_HDR_LLC_LEN, desc_info->hdr_llc_len) |
		    FIELD_PREP(RTW89_TXWD_BODY0_WD_PAGE, desc_info->wd_page) |
		    FIELD_PREP(RTW89_TXWD_BODY0_FW_DL, desc_info->fw_dl);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_BODY2_TID_INDICATE, desc_info->tid_indicate) |
		    FIELD_PREP(RTW89_TXWD_BODY2_QSEL, desc_info->qsel) |
		    FIELD_PREP(RTW89_TXWD_BODY2_TXPKT_SIZE, desc_info->pkt_size);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body3(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_BODY3_SW_SEQ, desc_info->seq) |
		    FIELD_PREP(RTW89_TXWD_BODY3_AGG_EN, desc_info->agg_en) |
		    FIELD_PREP(RTW89_TXWD_BODY3_BK, desc_info->bk);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info0(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_INFO0_USE_RATE, desc_info->use_rate) |
		    FIELD_PREP(RTW89_TXWD_INFO0_DATA_RATE, desc_info->data_rate) |
		    FIELD_PREP(RTW89_TXWD_INFO0_DISDATAFB, desc_info->dis_data_fb);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info1(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_INFO1_MAX_AGGNUM, desc_info->ampdu_num) |
		    FIELD_PREP(RTW89_TXWD_INFO1_A_CTRL_BSR, desc_info->a_ctrl_bsr) |
		    FIELD_PREP(RTW89_TXWD_INFO1_DATA_RTY_LOWEST_RATE,
			       desc_info->data_retry_lowest_rate);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_INFO2_AMPDU_DENSITY, desc_info->ampdu_density) |
		    FIELD_PREP(RTW89_TXWD_INFO2_SEC_TYPE, desc_info->sec_type) |
		    FIELD_PREP(RTW89_TXWD_INFO2_SEC_HW_ENC, desc_info->sec_en) |
		    FIELD_PREP(RTW89_TXWD_INFO2_SEC_CAM_IDX, desc_info->sec_cam_idx);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info4(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_INFO4_RTS_EN, 1) |
		    FIELD_PREP(RTW89_TXWD_INFO4_HW_RTS_EN, 1);

	return cpu_to_le32(dword);
}

void rtw89_core_fill_txdesc(struct rtw89_dev *rtwdev,
			    struct rtw89_tx_desc_info *desc_info,
			    void *txdesc)
{
	struct rtw89_txwd_body *txwd_body = (struct rtw89_txwd_body *)txdesc;
	struct rtw89_txwd_info *txwd_info;

	txwd_body->dword0 = rtw89_build_txwd_body0(desc_info);
	txwd_body->dword2 = rtw89_build_txwd_body2(desc_info);
	txwd_body->dword3 = rtw89_build_txwd_body3(desc_info);

	if (!desc_info->en_wd_info)
		return;

	txwd_info = (struct rtw89_txwd_info *)(txwd_body + 1);
	txwd_info->dword0 = rtw89_build_txwd_info0(desc_info);
	txwd_info->dword1 = rtw89_build_txwd_info1(desc_info);
	txwd_info->dword2 = rtw89_build_txwd_info2(desc_info);
	txwd_info->dword4 = rtw89_build_txwd_info4(desc_info);

}
EXPORT_SYMBOL(rtw89_core_fill_txdesc);

static int rtw89_core_rx_process_mac_ppdu(struct rtw89_dev *rtwdev,
					  struct sk_buff *skb,
					  struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	bool rx_cnt_valid = false;
	u8 plcp_size = 0;
	u8 usr_num = 0;
	u8 *phy_sts;

	rx_cnt_valid = RTW89_GET_RXINFO_RX_CNT_VLD(skb->data);
	plcp_size = RTW89_GET_RXINFO_PLCP_LEN(skb->data) << 3;
	usr_num = RTW89_GET_RXINFO_USR_NUM(skb->data);
	if (usr_num > RTW89_PPDU_MAX_USR) {
		rtw89_warn(rtwdev, "Invalid user number in mac info\n");
		return -EINVAL;
	}

	phy_sts = skb->data + RTW89_PPDU_MAC_INFO_SIZE;
	phy_sts += usr_num * RTW89_PPDU_MAC_INFO_USR_SIZE;
	/* 8-byte alignment */
	if (usr_num & BIT(0))
		phy_sts += RTW89_PPDU_MAC_INFO_USR_SIZE;
	if (rx_cnt_valid)
		phy_sts += RTW89_PPDU_MAC_RX_CNT_SIZE;
	phy_sts += plcp_size;

	phy_ppdu->buf = phy_sts;
	phy_ppdu->len = skb->data + skb->len - phy_sts;

	return 0;
}

static void rtw89_core_rx_process_phy_ppdu_iter(void *data,
						struct ieee80211_sta *sta)
{
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	struct rtw89_rx_phy_ppdu *phy_ppdu = (struct rtw89_rx_phy_ppdu *)data;

	if (rtwsta->mac_id == phy_ppdu->mac_id && phy_ppdu->to_self)
		ewma_rssi_add(&rtwsta->avg_rssi, phy_ppdu->rssi_avg);
}

#define VAR_LEN 0xff
#define VAR_LEN_UNIT 8
static u16 rtw89_core_get_phy_status_ie_len(struct rtw89_dev *rtwdev, u8 *addr)
{
	static const u8 physts_ie_len_tab[32] = {
		16, 32, 24, 24, 8, 8, 8, 8, VAR_LEN, 8, VAR_LEN, 176, VAR_LEN,
		VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, 16, 24, VAR_LEN,
		VAR_LEN, VAR_LEN, 0, 24, 24, 24, 24, 32, 32, 32, 32
	};
	u16 ie_len;
	u8 ie;

	ie = RTW89_GET_PHY_STS_IE_TYPE(addr);
	if (physts_ie_len_tab[ie] != VAR_LEN)
		ie_len = physts_ie_len_tab[ie];
	else
		ie_len = RTW89_GET_PHY_STS_IE_LEN(addr) * VAR_LEN_UNIT;

	return ie_len;
}

static void rtw89_core_parse_phy_status_ie01(struct rtw89_dev *rtwdev, u8 *addr,
					     struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	s16 cfo;

	/* sign conversion for S(12,2) */
	cfo = sign_extend32(RTW89_GET_PHY_STS_IE0_CFO(addr), 11);
	rtw89_phy_cfo_parse(rtwdev, cfo, phy_ppdu);
}

static int rtw89_core_process_phy_status_ie(struct rtw89_dev *rtwdev, u8 *addr,
					    struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	u8 ie;

	ie = RTW89_GET_PHY_STS_IE_TYPE(addr);
	switch (ie) {
	case RTW89_PHYSTS_IE01_CMN_OFDM:
		rtw89_core_parse_phy_status_ie01(rtwdev, addr, phy_ppdu);
		break;
	default:
		break;
	}

	return 0;
}

static void rtw89_core_update_phy_ppdu(struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	s8 *rssi = phy_ppdu->rssi;
	u8 *buf = phy_ppdu->buf;

	phy_ppdu->rssi_avg = RTW89_GET_PHY_STS_RSSI_AVG(buf);
	rssi[RF_PATH_A] = RTW89_RSSI_RAW_TO_DBM(RTW89_GET_PHY_STS_RSSI_A(buf));
	rssi[RF_PATH_B] = RTW89_RSSI_RAW_TO_DBM(RTW89_GET_PHY_STS_RSSI_B(buf));
	rssi[RF_PATH_C] = RTW89_RSSI_RAW_TO_DBM(RTW89_GET_PHY_STS_RSSI_C(buf));
	rssi[RF_PATH_D] = RTW89_RSSI_RAW_TO_DBM(RTW89_GET_PHY_STS_RSSI_D(buf));
}

static int rtw89_core_rx_process_phy_ppdu(struct rtw89_dev *rtwdev,
					  struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	if (RTW89_GET_PHY_STS_LEN(phy_ppdu->buf) << 3 != phy_ppdu->len) {
		rtw89_warn(rtwdev, "phy ppdu len mismatch\n");
		return -EINVAL;
	}
	rtw89_core_update_phy_ppdu(phy_ppdu);
	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_core_rx_process_phy_ppdu_iter,
					  phy_ppdu);

	return 0;
}

static int rtw89_core_rx_parse_phy_sts(struct rtw89_dev *rtwdev,
				       struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	u16 ie_len;
	u8 *pos, *end;

	if (!phy_ppdu->to_self)
		return 0;

	pos = (u8 *)phy_ppdu->buf + PHY_STS_HDR_LEN;
	end = (u8 *)phy_ppdu->buf + phy_ppdu->len;
	while (pos < end) {
		ie_len = rtw89_core_get_phy_status_ie_len(rtwdev, pos);
		rtw89_core_process_phy_status_ie(rtwdev, pos, phy_ppdu);
		pos += ie_len;
		if (pos > end || ie_len == 0) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX,
				    "phy status parse failed\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void rtw89_core_rx_process_phy_sts(struct rtw89_dev *rtwdev,
					  struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	int ret;

	ret = rtw89_core_rx_parse_phy_sts(rtwdev, phy_ppdu);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_TXRX, "parse phy sts failed\n");
	else
		phy_ppdu->valid = true;
}

static u8 rtw89_rxdesc_to_nl_he_gi(struct rtw89_dev *rtwdev,
				   const struct rtw89_rx_desc_info *desc_info,
				   bool rx_status)
{
	switch (desc_info->gi_ltf) {
	case RTW89_GILTF_SGI_4XHE08:
	case RTW89_GILTF_2XHE08:
	case RTW89_GILTF_1XHE08:
		return NL80211_RATE_INFO_HE_GI_0_8;
	case RTW89_GILTF_2XHE16:
	case RTW89_GILTF_1XHE16:
		return NL80211_RATE_INFO_HE_GI_1_6;
	case RTW89_GILTF_LGI_4XHE32:
		return NL80211_RATE_INFO_HE_GI_3_2;
	default:
		rtw89_warn(rtwdev, "invalid gi_ltf=%d", desc_info->gi_ltf);
		return rx_status ? NL80211_RATE_INFO_HE_GI_3_2 : U8_MAX;
	}
}

static bool rtw89_core_rx_ppdu_match(struct rtw89_dev *rtwdev,
				     struct rtw89_rx_desc_info *desc_info,
				     struct ieee80211_rx_status *status)
{
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;
	u8 data_rate_mode, bw, rate_idx = MASKBYTE0, gi_ltf;
	u16 data_rate;
	bool ret;

	data_rate = desc_info->data_rate;
	data_rate_mode = GET_DATA_RATE_MODE(data_rate);
	if (data_rate_mode == DATA_RATE_MODE_NON_HT) {
		rate_idx = GET_DATA_RATE_NOT_HT_IDX(data_rate);
		/* No 4 CCK rates for 5G */
		if (status->band == NL80211_BAND_5GHZ)
			rate_idx -= 4;
	} else if (data_rate_mode == DATA_RATE_MODE_HT) {
		rate_idx = GET_DATA_RATE_HT_IDX(data_rate);
	} else if (data_rate_mode == DATA_RATE_MODE_VHT) {
		rate_idx = GET_DATA_RATE_VHT_HE_IDX(data_rate);
	} else if (data_rate_mode == DATA_RATE_MODE_HE) {
		rate_idx = GET_DATA_RATE_VHT_HE_IDX(data_rate);
	} else {
		rtw89_warn(rtwdev, "invalid RX rate mode %d\n", data_rate_mode);
	}

	if (desc_info->bw == RTW89_CHANNEL_WIDTH_80)
		bw = RATE_INFO_BW_80;
	else if (desc_info->bw == RTW89_CHANNEL_WIDTH_40)
		bw = RATE_INFO_BW_40;
	else
		bw = RATE_INFO_BW_20;

	gi_ltf = rtw89_rxdesc_to_nl_he_gi(rtwdev, desc_info, false);
	ret = rtwdev->ppdu_sts.curr_rx_ppdu_cnt[band] == desc_info->ppdu_cnt &&
	      status->rate_idx == rate_idx &&
	      status->he_gi == gi_ltf &&
	      status->bw == bw;

	return ret;
}

struct rtw89_vif_rx_stats_iter_data {
	struct rtw89_dev *rtwdev;
	struct rtw89_rx_phy_ppdu *phy_ppdu;
	struct rtw89_rx_desc_info *desc_info;
	struct sk_buff *skb;
	const u8 *bssid;
};

static void rtw89_vif_rx_stats_iter(void *data, u8 *mac,
				    struct ieee80211_vif *vif)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	struct rtw89_vif_rx_stats_iter_data *iter_data = data;
	struct rtw89_dev *rtwdev = iter_data->rtwdev;
	struct rtw89_pkt_stat *pkt_stat = &rtwdev->phystat.cur_pkt_stat;
	struct rtw89_rx_desc_info *desc_info = iter_data->desc_info;
	struct sk_buff *skb = iter_data->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	const u8 *bssid = iter_data->bssid;

	if (!ether_addr_equal(vif->bss_conf.bssid, bssid))
		return;

	if (ieee80211_is_beacon(hdr->frame_control))
		pkt_stat->beacon_nr++;

	if (!ether_addr_equal(vif->addr, hdr->addr1))
		return;

	if (desc_info->data_rate < RTW89_HW_RATE_NR)
		pkt_stat->rx_rate_cnt[desc_info->data_rate]++;

	rtw89_traffic_stats_accu(rtwdev, &rtwvif->stats, skb, false);
}

static void rtw89_core_rx_stats(struct rtw89_dev *rtwdev,
				struct rtw89_rx_phy_ppdu *phy_ppdu,
				struct rtw89_rx_desc_info *desc_info,
				struct sk_buff *skb)
{
	struct rtw89_vif_rx_stats_iter_data iter_data;

	rtw89_traffic_stats_accu(rtwdev, &rtwdev->stats, skb, false);

	iter_data.rtwdev = rtwdev;
	iter_data.phy_ppdu = phy_ppdu;
	iter_data.desc_info = desc_info;
	iter_data.skb = skb;
	iter_data.bssid = get_hdr_bssid((struct ieee80211_hdr *)skb->data);
	rtw89_iterate_vifs_bh(rtwdev, rtw89_vif_rx_stats_iter, &iter_data);
}

static void rtw89_core_rx_pending_skb(struct rtw89_dev *rtwdev,
				      struct rtw89_rx_phy_ppdu *phy_ppdu,
				      struct rtw89_rx_desc_info *desc_info,
				      struct sk_buff *skb)
{
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;
	int curr = rtwdev->ppdu_sts.curr_rx_ppdu_cnt[band];
	struct sk_buff *skb_ppdu = NULL, *tmp;
	struct ieee80211_rx_status *rx_status;

	if (curr > RTW89_MAX_PPDU_CNT)
		return;

	skb_queue_walk_safe(&rtwdev->ppdu_sts.rx_queue[band], skb_ppdu, tmp) {
		skb_unlink(skb_ppdu, &rtwdev->ppdu_sts.rx_queue[band]);
		rx_status = IEEE80211_SKB_RXCB(skb_ppdu);
		if (rtw89_core_rx_ppdu_match(rtwdev, desc_info, rx_status))
			rtw89_chip_query_ppdu(rtwdev, phy_ppdu, rx_status);
		rtw89_core_rx_stats(rtwdev, phy_ppdu, desc_info, skb_ppdu);
		ieee80211_rx_napi(rtwdev->hw, NULL, skb_ppdu, &rtwdev->napi);
		rtwdev->napi_budget_countdown--;
	}
}

static void rtw89_core_rx_process_ppdu_sts(struct rtw89_dev *rtwdev,
					   struct rtw89_rx_desc_info *desc_info,
					   struct sk_buff *skb)
{
	struct rtw89_rx_phy_ppdu phy_ppdu = {.buf = skb->data, .valid = false,
					     .len = skb->len,
					     .to_self = desc_info->addr1_match,
					     .mac_id = desc_info->mac_id};
	int ret;

	if (desc_info->mac_info_valid)
		rtw89_core_rx_process_mac_ppdu(rtwdev, skb, &phy_ppdu);
	ret = rtw89_core_rx_process_phy_ppdu(rtwdev, &phy_ppdu);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_TXRX, "process ppdu failed\n");

	rtw89_core_rx_process_phy_sts(rtwdev, &phy_ppdu);
	rtw89_core_rx_pending_skb(rtwdev, &phy_ppdu, desc_info, skb);
	dev_kfree_skb_any(skb);
}

static void rtw89_core_rx_process_report(struct rtw89_dev *rtwdev,
					 struct rtw89_rx_desc_info *desc_info,
					 struct sk_buff *skb)
{
	switch (desc_info->pkt_type) {
	case RTW89_CORE_RX_TYPE_C2H:
		rtw89_fw_c2h_irqsafe(rtwdev, skb);
		break;
	case RTW89_CORE_RX_TYPE_PPDU_STAT:
		rtw89_core_rx_process_ppdu_sts(rtwdev, desc_info, skb);
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_TXRX, "unhandled pkt_type=%d\n",
			    desc_info->pkt_type);
		dev_kfree_skb_any(skb);
		break;
	}
}

void rtw89_core_query_rxdesc(struct rtw89_dev *rtwdev,
			     struct rtw89_rx_desc_info *desc_info,
			     u8 *data, u32 data_offset)
{
	struct rtw89_rxdesc_short *rxd_s;
	struct rtw89_rxdesc_long *rxd_l;
	u8 shift_len, drv_info_len;

	rxd_s = (struct rtw89_rxdesc_short *)(data + data_offset);
	desc_info->pkt_size = RTW89_GET_RXWD_PKT_SIZE(rxd_s);
	desc_info->drv_info_size = RTW89_GET_RXWD_DRV_INFO_SIZE(rxd_s);
	desc_info->long_rxdesc = RTW89_GET_RXWD_LONG_RXD(rxd_s);
	desc_info->pkt_type = RTW89_GET_RXWD_RPKT_TYPE(rxd_s);
	desc_info->mac_info_valid = RTW89_GET_RXWD_MAC_INFO_VALID(rxd_s);
	desc_info->bw = RTW89_GET_RXWD_BW(rxd_s);
	desc_info->data_rate = RTW89_GET_RXWD_DATA_RATE(rxd_s);
	desc_info->gi_ltf = RTW89_GET_RXWD_GI_LTF(rxd_s);
	desc_info->user_id = RTW89_GET_RXWD_USER_ID(rxd_s);
	desc_info->sr_en = RTW89_GET_RXWD_SR_EN(rxd_s);
	desc_info->ppdu_cnt = RTW89_GET_RXWD_PPDU_CNT(rxd_s);
	desc_info->ppdu_type = RTW89_GET_RXWD_PPDU_TYPE(rxd_s);
	desc_info->free_run_cnt = RTW89_GET_RXWD_FREE_RUN_CNT(rxd_s);
	desc_info->icv_err = RTW89_GET_RXWD_ICV_ERR(rxd_s);
	desc_info->crc32_err = RTW89_GET_RXWD_CRC32_ERR(rxd_s);
	desc_info->hw_dec = RTW89_GET_RXWD_HW_DEC(rxd_s);
	desc_info->sw_dec = RTW89_GET_RXWD_SW_DEC(rxd_s);
	desc_info->addr1_match = RTW89_GET_RXWD_A1_MATCH(rxd_s);

	shift_len = desc_info->shift << 1; /* 2-byte unit */
	drv_info_len = desc_info->drv_info_size << 3; /* 8-byte unit */
	desc_info->offset = data_offset + shift_len + drv_info_len;
	desc_info->ready = true;

	if (!desc_info->long_rxdesc)
		return;

	rxd_l = (struct rtw89_rxdesc_long *)(data + data_offset);
	desc_info->frame_type = RTW89_GET_RXWD_TYPE(rxd_l);
	desc_info->addr_cam_valid = RTW89_GET_RXWD_ADDR_CAM_VLD(rxd_l);
	desc_info->addr_cam_id = RTW89_GET_RXWD_ADDR_CAM_ID(rxd_l);
	desc_info->sec_cam_id = RTW89_GET_RXWD_SEC_CAM_ID(rxd_l);
	desc_info->mac_id = RTW89_GET_RXWD_MAC_ID(rxd_l);
	desc_info->rx_pl_id = RTW89_GET_RXWD_RX_PL_ID(rxd_l);
}
EXPORT_SYMBOL(rtw89_core_query_rxdesc);

struct rtw89_core_iter_rx_status {
	struct rtw89_dev *rtwdev;
	struct ieee80211_rx_status *rx_status;
	struct rtw89_rx_desc_info *desc_info;
	u8 mac_id;
};

static
void rtw89_core_stats_sta_rx_status_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_core_iter_rx_status *iter_data =
				(struct rtw89_core_iter_rx_status *)data;
	struct ieee80211_rx_status *rx_status = iter_data->rx_status;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	struct rtw89_rx_desc_info *desc_info = iter_data->desc_info;
	u8 mac_id = iter_data->mac_id;

	if (mac_id != rtwsta->mac_id)
		return;

	rtwsta->rx_status = *rx_status;
	rtwsta->rx_hw_rate = desc_info->data_rate;
}

static void rtw89_core_stats_sta_rx_status(struct rtw89_dev *rtwdev,
					   struct rtw89_rx_desc_info *desc_info,
					   struct ieee80211_rx_status *rx_status)
{
	struct rtw89_core_iter_rx_status iter_data;

	if (!desc_info->addr1_match || !desc_info->long_rxdesc)
		return;

	if (desc_info->frame_type != RTW89_RX_TYPE_DATA)
		return;

	iter_data.rtwdev = rtwdev;
	iter_data.rx_status = rx_status;
	iter_data.desc_info = desc_info;
	iter_data.mac_id = desc_info->mac_id;
	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_core_stats_sta_rx_status_iter,
					  &iter_data);
}

static void rtw89_core_update_rx_status(struct rtw89_dev *rtwdev,
					struct rtw89_rx_desc_info *desc_info,
					struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	u16 data_rate;
	u8 data_rate_mode;

	/* currently using single PHY */
	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;

	if (desc_info->icv_err || desc_info->crc32_err)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (desc_info->hw_dec &&
	    !(desc_info->sw_dec || desc_info->icv_err))
		rx_status->flag |= RX_FLAG_DECRYPTED;

	if (desc_info->bw == RTW89_CHANNEL_WIDTH_80)
		rx_status->bw = RATE_INFO_BW_80;
	else if (desc_info->bw == RTW89_CHANNEL_WIDTH_40)
		rx_status->bw = RATE_INFO_BW_40;
	else
		rx_status->bw = RATE_INFO_BW_20;

	data_rate = desc_info->data_rate;
	data_rate_mode = GET_DATA_RATE_MODE(data_rate);
	if (data_rate_mode == DATA_RATE_MODE_NON_HT) {
		rx_status->encoding = RX_ENC_LEGACY;
		rx_status->rate_idx = GET_DATA_RATE_NOT_HT_IDX(data_rate);
		/* No 4 CCK rates for 5G */
		if (rx_status->band == NL80211_BAND_5GHZ)
			rx_status->rate_idx -= 4;
		if (rtwdev->scanning)
			rx_status->rate_idx = min_t(u8, rx_status->rate_idx,
						    ARRAY_SIZE(rtw89_bitrates) - 5);
	} else if (data_rate_mode == DATA_RATE_MODE_HT) {
		rx_status->encoding = RX_ENC_HT;
		rx_status->rate_idx = GET_DATA_RATE_HT_IDX(data_rate);
		if (desc_info->gi_ltf)
			rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
	} else if (data_rate_mode == DATA_RATE_MODE_VHT) {
		rx_status->encoding = RX_ENC_VHT;
		rx_status->rate_idx = GET_DATA_RATE_VHT_HE_IDX(data_rate);
		rx_status->nss = GET_DATA_RATE_NSS(data_rate) + 1;
		if (desc_info->gi_ltf)
			rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
	} else if (data_rate_mode == DATA_RATE_MODE_HE) {
		rx_status->encoding = RX_ENC_HE;
		rx_status->rate_idx = GET_DATA_RATE_VHT_HE_IDX(data_rate);
		rx_status->nss = GET_DATA_RATE_NSS(data_rate) + 1;
	} else {
		rtw89_warn(rtwdev, "invalid RX rate mode %d\n", data_rate_mode);
	}

	/* he_gi is used to match ppdu, so we always fill it. */
	rx_status->he_gi = rtw89_rxdesc_to_nl_he_gi(rtwdev, desc_info, true);
	rx_status->flag |= RX_FLAG_MACTIME_START;
	rx_status->mactime = desc_info->free_run_cnt;

	rtw89_core_stats_sta_rx_status(rtwdev, desc_info, rx_status);
}

static enum rtw89_ps_mode rtw89_update_ps_mode(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (rtw89_disable_ps_mode || !chip->ps_mode_supported)
		return RTW89_PS_MODE_NONE;

	if (chip->ps_mode_supported & BIT(RTW89_PS_MODE_PWR_GATED))
		return RTW89_PS_MODE_PWR_GATED;

	if (chip->ps_mode_supported & BIT(RTW89_PS_MODE_CLK_GATED))
		return RTW89_PS_MODE_CLK_GATED;

	if (chip->ps_mode_supported & BIT(RTW89_PS_MODE_RFOFF))
		return RTW89_PS_MODE_RFOFF;

	return RTW89_PS_MODE_NONE;
}

static void rtw89_core_flush_ppdu_rx_queue(struct rtw89_dev *rtwdev,
					   struct rtw89_rx_desc_info *desc_info)
{
	struct rtw89_ppdu_sts_info *ppdu_sts = &rtwdev->ppdu_sts;
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;
	struct sk_buff *skb_ppdu, *tmp;

	skb_queue_walk_safe(&ppdu_sts->rx_queue[band], skb_ppdu, tmp) {
		skb_unlink(skb_ppdu, &ppdu_sts->rx_queue[band]);
		rtw89_core_rx_stats(rtwdev, NULL, desc_info, skb_ppdu);
		ieee80211_rx_napi(rtwdev->hw, NULL, skb_ppdu, &rtwdev->napi);
		rtwdev->napi_budget_countdown--;
	}
}

void rtw89_core_rx(struct rtw89_dev *rtwdev,
		   struct rtw89_rx_desc_info *desc_info,
		   struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status;
	struct rtw89_ppdu_sts_info *ppdu_sts = &rtwdev->ppdu_sts;
	u8 ppdu_cnt = desc_info->ppdu_cnt;
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;

	if (desc_info->pkt_type != RTW89_CORE_RX_TYPE_WIFI) {
		rtw89_core_rx_process_report(rtwdev, desc_info, skb);
		return;
	}

	if (ppdu_sts->curr_rx_ppdu_cnt[band] != ppdu_cnt) {
		rtw89_core_flush_ppdu_rx_queue(rtwdev, desc_info);
		ppdu_sts->curr_rx_ppdu_cnt[band] = ppdu_cnt;
	}

	rx_status = IEEE80211_SKB_RXCB(skb);
	memset(rx_status, 0, sizeof(*rx_status));
	rtw89_core_update_rx_status(rtwdev, desc_info, rx_status);
	if (desc_info->long_rxdesc &&
	    BIT(desc_info->frame_type) & PPDU_FILTER_BITMAP) {
		skb_queue_tail(&ppdu_sts->rx_queue[band], skb);
	} else {
		rtw89_core_rx_stats(rtwdev, NULL, desc_info, skb);
		ieee80211_rx_napi(rtwdev->hw, NULL, skb, &rtwdev->napi);
		rtwdev->napi_budget_countdown--;
	}
}
EXPORT_SYMBOL(rtw89_core_rx);

void rtw89_core_napi_start(struct rtw89_dev *rtwdev)
{
	if (test_and_set_bit(RTW89_FLAG_NAPI_RUNNING, rtwdev->flags))
		return;

	napi_enable(&rtwdev->napi);
}
EXPORT_SYMBOL(rtw89_core_napi_start);

void rtw89_core_napi_stop(struct rtw89_dev *rtwdev)
{
	if (!test_and_clear_bit(RTW89_FLAG_NAPI_RUNNING, rtwdev->flags))
		return;

	napi_synchronize(&rtwdev->napi);
	napi_disable(&rtwdev->napi);
}
EXPORT_SYMBOL(rtw89_core_napi_stop);

void rtw89_core_napi_init(struct rtw89_dev *rtwdev)
{
	init_dummy_netdev(&rtwdev->netdev);
	netif_napi_add(&rtwdev->netdev, &rtwdev->napi,
		       rtwdev->hci.ops->napi_poll, NAPI_POLL_WEIGHT);
}
EXPORT_SYMBOL(rtw89_core_napi_init);

void rtw89_core_napi_deinit(struct rtw89_dev *rtwdev)
{
	rtw89_core_napi_stop(rtwdev);
	netif_napi_del(&rtwdev->napi);
}
EXPORT_SYMBOL(rtw89_core_napi_deinit);

static void rtw89_core_ba_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev =
		container_of(work, struct rtw89_dev, ba_work);
	struct rtw89_txq *rtwtxq, *tmp;
	int ret;

	spin_lock_bh(&rtwdev->ba_lock);
	list_for_each_entry_safe(rtwtxq, tmp, &rtwdev->ba_list, list) {
		struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);
		struct ieee80211_sta *sta = txq->sta;
		struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
		u8 tid = txq->tid;

		if (!sta) {
			rtw89_warn(rtwdev, "cannot start BA without sta\n");
			goto skip_ba_work;
		}

		if (rtwsta->disassoc) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX,
				    "cannot start BA with disassoc sta\n");
			goto skip_ba_work;
		}

		ret = ieee80211_start_tx_ba_session(sta, tid, 0);
		if (ret) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX,
				    "failed to setup BA session for %pM:%2d: %d\n",
				    sta->addr, tid, ret);
			if (ret == -EINVAL)
				set_bit(RTW89_TXQ_F_BLOCK_BA, &rtwtxq->flags);
		}
skip_ba_work:
		list_del_init(&rtwtxq->list);
	}
	spin_unlock_bh(&rtwdev->ba_lock);
}

static void rtw89_core_free_sta_pending_ba(struct rtw89_dev *rtwdev,
					   struct ieee80211_sta *sta)
{
	struct rtw89_txq *rtwtxq, *tmp;

	spin_lock_bh(&rtwdev->ba_lock);
	list_for_each_entry_safe(rtwtxq, tmp, &rtwdev->ba_list, list) {
		struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);

		if (sta == txq->sta)
			list_del_init(&rtwtxq->list);
	}
	spin_unlock_bh(&rtwdev->ba_lock);
}

static void rtw89_core_txq_check_agg(struct rtw89_dev *rtwdev,
				     struct rtw89_txq *rtwtxq,
				     struct sk_buff *skb)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);
	struct ieee80211_sta *sta = txq->sta;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;

	if (unlikely(skb_get_queue_mapping(skb) == IEEE80211_AC_VO))
		return;

	if (unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)))
		return;

	if (unlikely(!sta))
		return;

	if (unlikely(test_bit(RTW89_TXQ_F_BLOCK_BA, &rtwtxq->flags)))
		return;

	if (test_bit(RTW89_TXQ_F_AMPDU, &rtwtxq->flags)) {
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_AMPDU;
		return;
	}

	spin_lock_bh(&rtwdev->ba_lock);
	if (!rtwsta->disassoc && list_empty(&rtwtxq->list)) {
		list_add_tail(&rtwtxq->list, &rtwdev->ba_list);
		ieee80211_queue_work(hw, &rtwdev->ba_work);
	}
	spin_unlock_bh(&rtwdev->ba_lock);
}

static void rtw89_core_txq_push(struct rtw89_dev *rtwdev,
				struct rtw89_txq *rtwtxq,
				unsigned long frame_cnt,
				unsigned long byte_cnt)
{
	struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);
	struct ieee80211_vif *vif = txq->vif;
	struct ieee80211_sta *sta = txq->sta;
	struct sk_buff *skb;
	unsigned long i;
	int ret;

	for (i = 0; i < frame_cnt; i++) {
		skb = ieee80211_tx_dequeue_ni(rtwdev->hw, txq);
		if (!skb) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX, "dequeue a NULL skb\n");
			return;
		}
		rtw89_core_txq_check_agg(rtwdev, rtwtxq, skb);
		ret = rtw89_core_tx_write(rtwdev, vif, sta, skb, NULL);
		if (ret) {
			rtw89_err(rtwdev, "failed to push txq: %d\n", ret);
			ieee80211_free_txskb(rtwdev->hw, skb);
			break;
		}
	}
}

static u32 rtw89_check_and_reclaim_tx_resource(struct rtw89_dev *rtwdev, u8 tid)
{
	u8 qsel, ch_dma;

	qsel = rtw89_core_get_qsel(rtwdev, tid);
	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	return rtw89_hci_check_and_reclaim_tx_resource(rtwdev, ch_dma);
}

static bool rtw89_core_txq_agg_wait(struct rtw89_dev *rtwdev,
				    struct ieee80211_txq *txq,
				    unsigned long *frame_cnt,
				    bool *sched_txq, bool *reinvoke)
{
	struct rtw89_txq *rtwtxq = (struct rtw89_txq *)txq->drv_priv;
	struct ieee80211_sta *sta = txq->sta;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;

	if (!sta || rtwsta->max_agg_wait <= 0)
		return false;

	if (rtwdev->stats.tx_tfc_lv <= RTW89_TFC_MID)
		return false;

	if (*frame_cnt > 1) {
		*frame_cnt -= 1;
		*sched_txq = true;
		*reinvoke = true;
		rtwtxq->wait_cnt = 1;
		return false;
	}

	if (*frame_cnt == 1 && rtwtxq->wait_cnt < rtwsta->max_agg_wait) {
		*reinvoke = true;
		rtwtxq->wait_cnt++;
		return true;
	}

	rtwtxq->wait_cnt = 0;
	return false;
}

static void rtw89_core_txq_schedule(struct rtw89_dev *rtwdev, u8 ac, bool *reinvoke)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct ieee80211_txq *txq;
	struct rtw89_txq *rtwtxq;
	unsigned long frame_cnt;
	unsigned long byte_cnt;
	u32 tx_resource;
	bool sched_txq;

	ieee80211_txq_schedule_start(hw, ac);
	while ((txq = ieee80211_next_txq(hw, ac))) {
		rtwtxq = (struct rtw89_txq *)txq->drv_priv;
		tx_resource = rtw89_check_and_reclaim_tx_resource(rtwdev, txq->tid);
		sched_txq = false;

		ieee80211_txq_get_depth(txq, &frame_cnt, &byte_cnt);
		if (rtw89_core_txq_agg_wait(rtwdev, txq, &frame_cnt, &sched_txq, reinvoke)) {
			ieee80211_return_txq(hw, txq, true);
			continue;
		}
		frame_cnt = min_t(unsigned long, frame_cnt, tx_resource);
		rtw89_core_txq_push(rtwdev, rtwtxq, frame_cnt, byte_cnt);
		ieee80211_return_txq(hw, txq, sched_txq);
		if (frame_cnt != 0)
			rtw89_core_tx_kick_off(rtwdev, rtw89_core_get_qsel(rtwdev, txq->tid));
	}
	ieee80211_txq_schedule_end(hw, ac);
}

static void rtw89_core_txq_work(struct work_struct *w)
{
	struct rtw89_dev *rtwdev = container_of(w, struct rtw89_dev, txq_work);
	bool reinvoke = false;
	u8 ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		rtw89_core_txq_schedule(rtwdev, ac, &reinvoke);

	if (reinvoke) {
		/* reinvoke to process the last frame */
		mod_delayed_work(rtwdev->txq_wq, &rtwdev->txq_reinvoke_work, 1);
	}
}

static void rtw89_core_txq_reinvoke_work(struct work_struct *w)
{
	struct rtw89_dev *rtwdev = container_of(w, struct rtw89_dev,
						txq_reinvoke_work.work);

	queue_work(rtwdev->txq_wq, &rtwdev->txq_work);
}

static enum rtw89_tfc_lv rtw89_get_traffic_level(struct rtw89_dev *rtwdev,
						 u32 throughput, u64 cnt)
{
	if (cnt < 100)
		return RTW89_TFC_IDLE;
	if (throughput > 50)
		return RTW89_TFC_HIGH;
	if (throughput > 10)
		return RTW89_TFC_MID;
	if (throughput > 2)
		return RTW89_TFC_LOW;
	return RTW89_TFC_ULTRA_LOW;
}

static bool rtw89_traffic_stats_calc(struct rtw89_dev *rtwdev,
				     struct rtw89_traffic_stats *stats)
{
	enum rtw89_tfc_lv tx_tfc_lv = stats->tx_tfc_lv;
	enum rtw89_tfc_lv rx_tfc_lv = stats->rx_tfc_lv;

	stats->tx_throughput_raw = (u32)(stats->tx_unicast >> RTW89_TP_SHIFT);
	stats->rx_throughput_raw = (u32)(stats->rx_unicast >> RTW89_TP_SHIFT);

	ewma_tp_add(&stats->tx_ewma_tp, stats->tx_throughput_raw);
	ewma_tp_add(&stats->rx_ewma_tp, stats->rx_throughput_raw);

	stats->tx_throughput = ewma_tp_read(&stats->tx_ewma_tp);
	stats->rx_throughput = ewma_tp_read(&stats->rx_ewma_tp);
	stats->tx_tfc_lv = rtw89_get_traffic_level(rtwdev, stats->tx_throughput,
						   stats->tx_cnt);
	stats->rx_tfc_lv = rtw89_get_traffic_level(rtwdev, stats->rx_throughput,
						   stats->rx_cnt);
	stats->tx_avg_len = stats->tx_cnt ?
			    DIV_ROUND_DOWN_ULL(stats->tx_unicast, stats->tx_cnt) : 0;
	stats->rx_avg_len = stats->rx_cnt ?
			    DIV_ROUND_DOWN_ULL(stats->rx_unicast, stats->rx_cnt) : 0;

	stats->tx_unicast = 0;
	stats->rx_unicast = 0;
	stats->tx_cnt = 0;
	stats->rx_cnt = 0;

	if (tx_tfc_lv != stats->tx_tfc_lv || rx_tfc_lv != stats->rx_tfc_lv)
		return true;

	return false;
}

static bool rtw89_traffic_stats_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif *rtwvif;
	bool tfc_changed;

	tfc_changed = rtw89_traffic_stats_calc(rtwdev, &rtwdev->stats);
	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_traffic_stats_calc(rtwdev, &rtwvif->stats);

	return tfc_changed;
}

static void rtw89_vif_enter_lps(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	if (rtwvif->wifi_role != RTW89_WIFI_ROLE_STATION)
		return;

	if (rtwvif->stats.tx_tfc_lv == RTW89_TFC_IDLE &&
	    rtwvif->stats.rx_tfc_lv == RTW89_TFC_IDLE)
		rtw89_enter_lps(rtwdev, rtwvif->mac_id);
}

static void rtw89_enter_lps_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif *rtwvif;

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_vif_enter_lps(rtwdev, rtwvif);
}

void rtw89_traffic_stats_init(struct rtw89_dev *rtwdev,
			      struct rtw89_traffic_stats *stats)
{
	stats->tx_unicast = 0;
	stats->rx_unicast = 0;
	stats->tx_cnt = 0;
	stats->rx_cnt = 0;
	ewma_tp_init(&stats->tx_ewma_tp);
	ewma_tp_init(&stats->rx_ewma_tp);
}

static void rtw89_track_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						track_work.work);
	bool tfc_changed;

	mutex_lock(&rtwdev->mutex);

	if (!test_bit(RTW89_FLAG_RUNNING, rtwdev->flags))
		goto out;

	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->track_work,
				     RTW89_TRACK_WORK_PERIOD);

	tfc_changed = rtw89_traffic_stats_track(rtwdev);
	if (rtwdev->scanning)
		goto out;

	rtw89_leave_lps(rtwdev);

	if (tfc_changed) {
		rtw89_hci_recalc_int_mit(rtwdev);
		rtw89_btc_ntfy_wl_sta(rtwdev);
	}
	rtw89_mac_bf_monitor_track(rtwdev);
	rtw89_phy_stat_track(rtwdev);
	rtw89_phy_env_monitor_track(rtwdev);
	rtw89_phy_dig(rtwdev);
	rtw89_chip_rfk_track(rtwdev);
	rtw89_phy_ra_update(rtwdev);
	rtw89_phy_cfo_track(rtwdev);

	if (rtwdev->lps_enabled && !rtwdev->btc.lps)
		rtw89_enter_lps_track(rtwdev);

out:
	mutex_unlock(&rtwdev->mutex);
}

u8 rtw89_core_acquire_bit_map(unsigned long *addr, unsigned long size)
{
	unsigned long bit;

	bit = find_first_zero_bit(addr, size);
	if (bit < size)
		set_bit(bit, addr);

	return bit;
}

void rtw89_core_release_bit_map(unsigned long *addr, u8 bit)
{
	clear_bit(bit, addr);
}

void rtw89_core_release_all_bits_map(unsigned long *addr, unsigned int nbits)
{
	bitmap_zero(addr, nbits);
}

#define RTW89_TYPE_MAPPING(_type)	\
	case NL80211_IFTYPE_ ## _type:	\
		rtwvif->wifi_role = RTW89_WIFI_ROLE_ ## _type;	\
		break
void rtw89_vif_type_mapping(struct ieee80211_vif *vif, bool assoc)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	switch (vif->type) {
	RTW89_TYPE_MAPPING(ADHOC);
	RTW89_TYPE_MAPPING(STATION);
	RTW89_TYPE_MAPPING(AP);
	RTW89_TYPE_MAPPING(MONITOR);
	RTW89_TYPE_MAPPING(MESH_POINT);
	default:
		WARN_ON(1);
		break;
	}

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		rtwvif->net_type = RTW89_NET_TYPE_AP_MODE;
		rtwvif->self_role = RTW89_SELF_ROLE_AP;
		break;
	case NL80211_IFTYPE_ADHOC:
		rtwvif->net_type = RTW89_NET_TYPE_AD_HOC;
		rtwvif->self_role = RTW89_SELF_ROLE_CLIENT;
		break;
	case NL80211_IFTYPE_STATION:
		if (assoc) {
			rtwvif->net_type = RTW89_NET_TYPE_INFRA;
			rtwvif->trigger = vif->bss_conf.he_support;
		} else {
			rtwvif->net_type = RTW89_NET_TYPE_NO_LINK;
			rtwvif->trigger = false;
		}
		rtwvif->self_role = RTW89_SELF_ROLE_CLIENT;
		rtwvif->addr_cam.sec_ent_mode = RTW89_ADDR_CAM_SEC_NORMAL;
		break;
	default:
		WARN_ON(1);
		break;
	}
}

int rtw89_core_sta_add(struct rtw89_dev *rtwdev,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	int i;

	rtwsta->rtwvif = rtwvif;
	rtwsta->prev_rssi = 0;

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		rtw89_core_txq_init(rtwdev, sta->txq[i]);

	ewma_rssi_init(&rtwsta->avg_rssi);

	if (vif->type == NL80211_IFTYPE_STATION) {
		rtwvif->mgd.ap = sta;
		rtw89_btc_ntfy_role_info(rtwdev, rtwvif, rtwsta,
					 BTC_ROLE_MSTS_STA_CONN_START);
		rtw89_chip_rfk_channel(rtwdev);
	}

	return 0;
}

int rtw89_core_sta_disassoc(struct rtw89_dev *rtwdev,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta)
{
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;

	rtwdev->total_sta_assoc--;
	rtwsta->disassoc = true;

	return 0;
}

int rtw89_core_sta_disconnect(struct rtw89_dev *rtwdev,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	int ret;

	rtw89_mac_bf_monitor_calc(rtwdev, sta, true);
	rtw89_mac_bf_disassoc(rtwdev, vif, sta);
	rtw89_core_free_sta_pending_ba(rtwdev, sta);

	rtw89_vif_type_mapping(vif, false);

	ret = rtw89_fw_h2c_assoc_cmac_tbl(rtwdev, vif, sta);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cmac table\n");
		return ret;
	}

	ret = rtw89_fw_h2c_join_info(rtwdev, rtwvif, 1);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c join info\n");
		return ret;
	}

	/* update cam aid mac_id net_type */
	rtw89_fw_h2c_cam(rtwdev, rtwvif);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cam\n");
		return ret;
	}

	return ret;
}

int rtw89_core_sta_assoc(struct rtw89_dev *rtwdev,
			 struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	int ret;

	rtw89_vif_type_mapping(vif, true);

	ret = rtw89_fw_h2c_assoc_cmac_tbl(rtwdev, vif, sta);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cmac table\n");
		return ret;
	}

	/* for station mode, assign the mac_id from itself */
	if (vif->type == NL80211_IFTYPE_STATION)
		rtwsta->mac_id = rtwvif->mac_id;

	ret = rtw89_fw_h2c_join_info(rtwdev, rtwvif, 0);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c join info\n");
		return ret;
	}

	/* update cam aid mac_id net_type */
	rtw89_fw_h2c_cam(rtwdev, rtwvif);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cam\n");
		return ret;
	}

	ret = rtw89_fw_h2c_general_pkt(rtwdev, rtwsta->mac_id);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c general packet\n");
		return ret;
	}

	rtwdev->total_sta_assoc++;
	rtw89_phy_ra_assoc(rtwdev, sta);
	rtw89_mac_bf_assoc(rtwdev, vif, sta);
	rtw89_mac_bf_monitor_calc(rtwdev, sta, false);

	if (vif->type == NL80211_IFTYPE_STATION) {
		rtw89_btc_ntfy_role_info(rtwdev, rtwvif, rtwsta,
					 BTC_ROLE_MSTS_STA_CONN_END);
		rtw89_core_get_no_ul_ofdma_htc(rtwdev, &rtwsta->htc_template);
	}

	return ret;
}

int rtw89_core_sta_remove(struct rtw89_dev *rtwdev,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;

	if (vif->type == NL80211_IFTYPE_STATION)
		rtw89_btc_ntfy_role_info(rtwdev, rtwvif, rtwsta,
					 BTC_ROLE_MSTS_STA_DIS_CONN);

	return 0;
}

static void rtw89_init_ht_cap(struct rtw89_dev *rtwdev,
			      struct ieee80211_sta_ht_cap *ht_cap)
{
	static const __le16 highest[RF_PATH_MAX] = {
		cpu_to_le16(150), cpu_to_le16(300), cpu_to_le16(450), cpu_to_le16(600),
	};
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 nss = hal->rx_nss;
	int i;

	ht_cap->ht_supported = true;
	ht_cap->cap = 0;
	ht_cap->cap |= IEEE80211_HT_CAP_SGI_20 |
		       IEEE80211_HT_CAP_MAX_AMSDU |
		       IEEE80211_HT_CAP_TX_STBC |
		       (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);
	ht_cap->cap |= IEEE80211_HT_CAP_LDPC_CODING;
	ht_cap->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_DSSSCCK40 |
		       IEEE80211_HT_CAP_SGI_40;
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_cap->ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE;
	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	for (i = 0; i < nss; i++)
		ht_cap->mcs.rx_mask[i] = 0xFF;
	ht_cap->mcs.rx_mask[4] = 0x01;
	ht_cap->mcs.rx_highest = highest[nss - 1];
}

static void rtw89_init_vht_cap(struct rtw89_dev *rtwdev,
			       struct ieee80211_sta_vht_cap *vht_cap)
{
	static const __le16 highest[RF_PATH_MAX] = {
		cpu_to_le16(433), cpu_to_le16(867), cpu_to_le16(1300), cpu_to_le16(1733),
	};
	struct rtw89_hal *hal = &rtwdev->hal;
	u16 tx_mcs_map = 0, rx_mcs_map = 0;
	u8 sts_cap = 3;
	int i;

	for (i = 0; i < 8; i++) {
		if (i < hal->tx_nss)
			tx_mcs_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << (i * 2);
		else
			tx_mcs_map |= IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2);
		if (i < hal->rx_nss)
			rx_mcs_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << (i * 2);
		else
			rx_mcs_map |= IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2);
	}

	vht_cap->vht_supported = true;
	vht_cap->cap = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
		       IEEE80211_VHT_CAP_SHORT_GI_80 |
		       IEEE80211_VHT_CAP_RXSTBC_1 |
		       IEEE80211_VHT_CAP_HTC_VHT |
		       IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK |
		       0;
	vht_cap->cap |= IEEE80211_VHT_CAP_TXSTBC;
	vht_cap->cap |= IEEE80211_VHT_CAP_RXLDPC;
	vht_cap->cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE |
			IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
	vht_cap->cap |= sts_cap << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
	vht_cap->vht_mcs.rx_mcs_map = cpu_to_le16(rx_mcs_map);
	vht_cap->vht_mcs.tx_mcs_map = cpu_to_le16(tx_mcs_map);
	vht_cap->vht_mcs.rx_highest = highest[hal->rx_nss - 1];
	vht_cap->vht_mcs.tx_highest = highest[hal->tx_nss - 1];
}

#define RTW89_SBAND_IFTYPES_NR 2

static void rtw89_init_he_cap(struct rtw89_dev *rtwdev,
			      enum nl80211_band band,
			      struct ieee80211_supported_band *sband)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_hal *hal = &rtwdev->hal;
	struct ieee80211_sband_iftype_data *iftype_data;
	bool no_ng16 = (chip->chip_id == RTL8852A && hal->cv == CHIP_CBV) ||
		       (chip->chip_id == RTL8852B && hal->cv == CHIP_CAV);
	u16 mcs_map = 0;
	int i;
	int nss = hal->rx_nss;
	int idx = 0;

	iftype_data = kcalloc(RTW89_SBAND_IFTYPES_NR, sizeof(*iftype_data), GFP_KERNEL);
	if (!iftype_data)
		return;

	for (i = 0; i < 8; i++) {
		if (i < nss)
			mcs_map |= IEEE80211_HE_MCS_SUPPORT_0_11 << (i * 2);
		else
			mcs_map |= IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2);
	}

	for (i = 0; i < NUM_NL80211_IFTYPES; i++) {
		struct ieee80211_sta_he_cap *he_cap;
		u8 *mac_cap_info;
		u8 *phy_cap_info;

		switch (i) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_AP:
			break;
		default:
			continue;
		}

		if (idx >= RTW89_SBAND_IFTYPES_NR) {
			rtw89_warn(rtwdev, "run out of iftype_data\n");
			break;
		}

		iftype_data[idx].types_mask = BIT(i);
		he_cap = &iftype_data[idx].he_cap;
		mac_cap_info = he_cap->he_cap_elem.mac_cap_info;
		phy_cap_info = he_cap->he_cap_elem.phy_cap_info;

		he_cap->has_he = true;
		if (i == NL80211_IFTYPE_AP)
			mac_cap_info[0] = IEEE80211_HE_MAC_CAP0_HTC_HE;
		if (i == NL80211_IFTYPE_STATION)
			mac_cap_info[1] = IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US;
		mac_cap_info[2] = IEEE80211_HE_MAC_CAP2_ALL_ACK |
				  IEEE80211_HE_MAC_CAP2_BSR;
		mac_cap_info[3] = IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_2;
		if (i == NL80211_IFTYPE_AP)
			mac_cap_info[3] |= IEEE80211_HE_MAC_CAP3_OMI_CONTROL;
		mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_OPS |
				  IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU;
		if (i == NL80211_IFTYPE_STATION)
			mac_cap_info[5] = IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX;
		phy_cap_info[0] = IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
				  IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
		phy_cap_info[1] = IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
				  IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
				  IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US;
		phy_cap_info[2] = IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
				  IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
				  IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
				  IEEE80211_HE_PHY_CAP2_DOPPLER_TX;
		phy_cap_info[3] = IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM;
		if (i == NL80211_IFTYPE_STATION)
			phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM |
					   IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_2;
		if (i == NL80211_IFTYPE_AP)
			phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU;
		phy_cap_info[4] = IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
				  IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
		phy_cap_info[5] = no_ng16 ? 0 :
				  IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
				  IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK;
		phy_cap_info[6] = IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
				  IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
				  IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
				  IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE;
		phy_cap_info[7] = IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP |
				  IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI |
				  IEEE80211_HE_PHY_CAP7_MAX_NC_1;
		phy_cap_info[8] = IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
				  IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI |
				  IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996;
		phy_cap_info[9] = IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM |
				  IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU |
				  IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
				  IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB |
				  IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US;
		if (i == NL80211_IFTYPE_STATION)
			phy_cap_info[9] |= IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU;
		he_cap->he_mcs_nss_supp.rx_mcs_80 = cpu_to_le16(mcs_map);
		he_cap->he_mcs_nss_supp.tx_mcs_80 = cpu_to_le16(mcs_map);

		idx++;
	}

	sband->iftype_data = iftype_data;
	sband->n_iftype_data = idx;
}

static int rtw89_core_set_supported_band(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct ieee80211_supported_band *sband_2ghz = NULL, *sband_5ghz = NULL;
	u32 size = sizeof(struct ieee80211_supported_band);

	sband_2ghz = kmemdup(&rtw89_sband_2ghz, size, GFP_KERNEL);
	if (!sband_2ghz)
		goto err;
	rtw89_init_ht_cap(rtwdev, &sband_2ghz->ht_cap);
	rtw89_init_he_cap(rtwdev, NL80211_BAND_2GHZ, sband_2ghz);
	hw->wiphy->bands[NL80211_BAND_2GHZ] = sband_2ghz;

	sband_5ghz = kmemdup(&rtw89_sband_5ghz, size, GFP_KERNEL);
	if (!sband_5ghz)
		goto err;
	rtw89_init_ht_cap(rtwdev, &sband_5ghz->ht_cap);
	rtw89_init_vht_cap(rtwdev, &sband_5ghz->vht_cap);
	rtw89_init_he_cap(rtwdev, NL80211_BAND_5GHZ, sband_5ghz);
	hw->wiphy->bands[NL80211_BAND_5GHZ] = sband_5ghz;

	return 0;

err:
	hw->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	hw->wiphy->bands[NL80211_BAND_5GHZ] = NULL;
	if (sband_2ghz)
		kfree(sband_2ghz->iftype_data);
	if (sband_5ghz)
		kfree(sband_5ghz->iftype_data);
	kfree(sband_2ghz);
	kfree(sband_5ghz);
	return -ENOMEM;
}

static void rtw89_core_clr_supported_band(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;

	kfree(hw->wiphy->bands[NL80211_BAND_2GHZ]->iftype_data);
	kfree(hw->wiphy->bands[NL80211_BAND_5GHZ]->iftype_data);
	kfree(hw->wiphy->bands[NL80211_BAND_2GHZ]);
	kfree(hw->wiphy->bands[NL80211_BAND_5GHZ]);
	hw->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	hw->wiphy->bands[NL80211_BAND_5GHZ] = NULL;
}

static void rtw89_core_ppdu_sts_init(struct rtw89_dev *rtwdev)
{
	int i;

	for (i = 0; i < RTW89_PHY_MAX; i++)
		skb_queue_head_init(&rtwdev->ppdu_sts.rx_queue[i]);
	for (i = 0; i < RTW89_PHY_MAX; i++)
		rtwdev->ppdu_sts.curr_rx_ppdu_cnt[i] = U8_MAX;
}

int rtw89_core_start(struct rtw89_dev *rtwdev)
{
	int ret;

	rtwdev->mac.qta_mode = RTW89_QTA_SCC;
	ret = rtw89_mac_init(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "mac init fail, ret:%d\n", ret);
		return ret;
	}

	rtw89_btc_ntfy_poweron(rtwdev);

	/* efuse process */

	/* pre-config BB/RF, BB reset/RFC reset */
	rtw89_mac_disable_bb_rf(rtwdev);
	rtw89_mac_enable_bb_rf(rtwdev);
	rtw89_phy_init_bb_reg(rtwdev);
	rtw89_phy_init_rf_reg(rtwdev);

	rtw89_btc_ntfy_init(rtwdev, BTC_MODE_NORMAL);

	rtw89_phy_dm_init(rtwdev);

	rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
	rtw89_mac_update_rts_threshold(rtwdev, RTW89_MAC_0);

	ret = rtw89_hci_start(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to start hci\n");
		return ret;
	}

	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->track_work,
				     RTW89_TRACK_WORK_PERIOD);

	set_bit(RTW89_FLAG_RUNNING, rtwdev->flags);

	rtw89_btc_ntfy_radio_state(rtwdev, BTC_RFCTRL_WL_ON);
	rtw89_fw_h2c_fw_log(rtwdev, rtwdev->fw.fw_log_enable);

	return 0;
}

void rtw89_core_stop(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	/* Prvent to stop twice; enter_ips and ops_stop */
	if (!test_bit(RTW89_FLAG_RUNNING, rtwdev->flags))
		return;

	rtw89_btc_ntfy_radio_state(rtwdev, BTC_RFCTRL_WL_OFF);

	clear_bit(RTW89_FLAG_RUNNING, rtwdev->flags);

	mutex_unlock(&rtwdev->mutex);

	cancel_work_sync(&rtwdev->c2h_work);
	cancel_work_sync(&btc->eapol_notify_work);
	cancel_work_sync(&btc->arp_notify_work);
	cancel_work_sync(&btc->dhcp_notify_work);
	cancel_work_sync(&btc->icmp_notify_work);
	cancel_delayed_work_sync(&rtwdev->txq_reinvoke_work);
	cancel_delayed_work_sync(&rtwdev->track_work);
	cancel_delayed_work_sync(&rtwdev->coex_act1_work);
	cancel_delayed_work_sync(&rtwdev->coex_bt_devinfo_work);
	cancel_delayed_work_sync(&rtwdev->coex_rfk_chk_work);
	cancel_delayed_work_sync(&rtwdev->cfo_track_work);

	mutex_lock(&rtwdev->mutex);

	rtw89_btc_ntfy_poweroff(rtwdev);
	rtw89_hci_flush_queues(rtwdev, BIT(rtwdev->hw->queues) - 1, true);
	rtw89_mac_flush_txq(rtwdev, BIT(rtwdev->hw->queues) - 1, true);
	rtw89_hci_stop(rtwdev);
	rtw89_hci_deinit(rtwdev);
	rtw89_mac_pwr_off(rtwdev);
	rtw89_hci_reset(rtwdev);
}

int rtw89_core_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	int ret;

	INIT_LIST_HEAD(&rtwdev->ba_list);
	INIT_LIST_HEAD(&rtwdev->rtwvifs_list);
	INIT_LIST_HEAD(&rtwdev->early_h2c_list);
	INIT_WORK(&rtwdev->ba_work, rtw89_core_ba_work);
	INIT_WORK(&rtwdev->txq_work, rtw89_core_txq_work);
	INIT_DELAYED_WORK(&rtwdev->txq_reinvoke_work, rtw89_core_txq_reinvoke_work);
	INIT_DELAYED_WORK(&rtwdev->track_work, rtw89_track_work);
	INIT_DELAYED_WORK(&rtwdev->coex_act1_work, rtw89_coex_act1_work);
	INIT_DELAYED_WORK(&rtwdev->coex_bt_devinfo_work, rtw89_coex_bt_devinfo_work);
	INIT_DELAYED_WORK(&rtwdev->coex_rfk_chk_work, rtw89_coex_rfk_chk_work);
	INIT_DELAYED_WORK(&rtwdev->cfo_track_work, rtw89_phy_cfo_track_work);
	rtwdev->txq_wq = alloc_workqueue("rtw89_tx_wq", WQ_UNBOUND | WQ_HIGHPRI, 0);
	spin_lock_init(&rtwdev->ba_lock);
	mutex_init(&rtwdev->mutex);
	mutex_init(&rtwdev->rf_mutex);
	rtwdev->total_sta_assoc = 0;

	INIT_WORK(&rtwdev->c2h_work, rtw89_fw_c2h_work);
	skb_queue_head_init(&rtwdev->c2h_queue);
	rtw89_core_ppdu_sts_init(rtwdev);
	rtw89_traffic_stats_init(rtwdev, &rtwdev->stats);

	rtwdev->ps_mode = rtw89_update_ps_mode(rtwdev);
	rtwdev->hal.rx_fltr = DEFAULT_AX_RX_FLTR;

	INIT_WORK(&btc->eapol_notify_work, rtw89_btc_ntfy_eapol_packet_work);
	INIT_WORK(&btc->arp_notify_work, rtw89_btc_ntfy_arp_packet_work);
	INIT_WORK(&btc->dhcp_notify_work, rtw89_btc_ntfy_dhcp_packet_work);
	INIT_WORK(&btc->icmp_notify_work, rtw89_btc_ntfy_icmp_packet_work);

	ret = rtw89_load_firmware(rtwdev);
	if (ret) {
		rtw89_warn(rtwdev, "no firmware loaded\n");
		return ret;
	}
	rtw89_ser_init(rtwdev);

	return 0;
}
EXPORT_SYMBOL(rtw89_core_init);

void rtw89_core_deinit(struct rtw89_dev *rtwdev)
{
	rtw89_ser_deinit(rtwdev);
	rtw89_unload_firmware(rtwdev);
	rtw89_fw_free_all_early_h2c(rtwdev);

	destroy_workqueue(rtwdev->txq_wq);
	mutex_destroy(&rtwdev->rf_mutex);
	mutex_destroy(&rtwdev->mutex);
}
EXPORT_SYMBOL(rtw89_core_deinit);

static void rtw89_read_chip_ver(struct rtw89_dev *rtwdev)
{
	u8 cv;

	cv = rtw89_read32_mask(rtwdev, R_AX_SYS_CFG1, B_AX_CHIP_VER_MASK);
	if (cv <= CHIP_CBV) {
		if (rtw89_read32(rtwdev, R_AX_GPIO0_7_FUNC_SEL) == RTW89_R32_DEAD)
			cv = CHIP_CAV;
		else
			cv = CHIP_CBV;
	}

	rtwdev->hal.cv = cv;
}

static int rtw89_chip_efuse_info_setup(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_mac_partial_init(rtwdev);
	if (ret)
		return ret;

	ret = rtw89_parse_efuse_map(rtwdev);
	if (ret)
		return ret;

	ret = rtw89_parse_phycap_map(rtwdev);
	if (ret)
		return ret;

	ret = rtw89_mac_setup_phycap(rtwdev);
	if (ret)
		return ret;

	rtw89_mac_pwr_off(rtwdev);

	return 0;
}

static int rtw89_chip_board_info_setup(struct rtw89_dev *rtwdev)
{
	rtw89_chip_fem_setup(rtwdev);

	return 0;
}

int rtw89_chip_info_setup(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_read_chip_ver(rtwdev);

	ret = rtw89_wait_firmware_completion(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to wait firmware completion\n");
		return ret;
	}

	ret = rtw89_fw_recognize(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to recognize firmware\n");
		return ret;
	}

	ret = rtw89_chip_efuse_info_setup(rtwdev);
	if (ret)
		return ret;

	ret = rtw89_chip_board_info_setup(rtwdev);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(rtw89_chip_info_setup);

static int rtw89_core_register_hw(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	int ret;
	int tx_headroom = IEEE80211_HT_CTL_LEN;

	hw->vif_data_size = sizeof(struct rtw89_vif);
	hw->sta_data_size = sizeof(struct rtw89_sta);
	hw->txq_data_size = sizeof(struct rtw89_txq);

	SET_IEEE80211_PERM_ADDR(hw, efuse->addr);

	hw->extra_tx_headroom = tx_headroom;
	hw->queues = IEEE80211_NUM_ACS;
	hw->max_rx_aggregation_subframes = RTW89_MAX_RX_AGG_NUM;
	hw->max_tx_aggregation_subframes = RTW89_MAX_TX_AGG_NUM;

	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, RX_INCLUDES_FCS);
	ieee80211_hw_set(hw, TX_AMSDU);
	ieee80211_hw_set(hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(hw, SUPPORTS_AMSDU_IN_AMPDU);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	hw->wiphy->available_antennas_tx = BIT(rtwdev->chip->rf_path_num) - 1;
	hw->wiphy->available_antennas_rx = BIT(rtwdev->chip->rf_path_num) - 1;

	hw->wiphy->features |= NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR;

	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_CAN_REPLACE_PTK0);

	ret = rtw89_core_set_supported_band(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to set supported band\n");
		return ret;
	}

	hw->wiphy->reg_notifier = rtw89_regd_notifier;
	hw->wiphy->sar_capa = &rtw89_sar_capa;

	ret = ieee80211_register_hw(hw);
	if (ret) {
		rtw89_err(rtwdev, "failed to register hw\n");
		goto err;
	}

	ret = rtw89_regd_init(rtwdev, rtw89_regd_notifier);
	if (ret) {
		rtw89_err(rtwdev, "failed to init regd\n");
		goto err;
	}

	return 0;

err:
	return ret;
}

static void rtw89_core_unregister_hw(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;

	ieee80211_unregister_hw(hw);
	rtw89_core_clr_supported_band(rtwdev);
}

int rtw89_core_register(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_core_register_hw(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to register core hw\n");
		return ret;
	}

	rtw89_debugfs_init(rtwdev);

	return 0;
}
EXPORT_SYMBOL(rtw89_core_register);

void rtw89_core_unregister(struct rtw89_dev *rtwdev)
{
	rtw89_core_unregister_hw(rtwdev);
}
EXPORT_SYMBOL(rtw89_core_unregister);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless core module");
MODULE_LICENSE("Dual BSD/GPL");
