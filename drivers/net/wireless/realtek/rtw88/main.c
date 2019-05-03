// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "regd.h"
#include "fw.h"
#include "ps.h"
#include "sec.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "efuse.h"
#include "debug.h"

static bool rtw_fw_support_lps;
unsigned int rtw_debug_mask;
EXPORT_SYMBOL(rtw_debug_mask);

module_param_named(support_lps, rtw_fw_support_lps, bool, 0644);
module_param_named(debug_mask, rtw_debug_mask, uint, 0644);

MODULE_PARM_DESC(support_lps, "Set Y to enable LPS support");
MODULE_PARM_DESC(debug_mask, "Debugging mask");

static struct ieee80211_channel rtw_channeltable_2g[] = {
	{.center_freq = 2412, .hw_value = 1,},
	{.center_freq = 2417, .hw_value = 2,},
	{.center_freq = 2422, .hw_value = 3,},
	{.center_freq = 2427, .hw_value = 4,},
	{.center_freq = 2432, .hw_value = 5,},
	{.center_freq = 2437, .hw_value = 6,},
	{.center_freq = 2442, .hw_value = 7,},
	{.center_freq = 2447, .hw_value = 8,},
	{.center_freq = 2452, .hw_value = 9,},
	{.center_freq = 2457, .hw_value = 10,},
	{.center_freq = 2462, .hw_value = 11,},
	{.center_freq = 2467, .hw_value = 12,},
	{.center_freq = 2472, .hw_value = 13,},
	{.center_freq = 2484, .hw_value = 14,},
};

static struct ieee80211_channel rtw_channeltable_5g[] = {
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
	{.center_freq = 5745, .hw_value = 149,},
	{.center_freq = 5765, .hw_value = 153,},
	{.center_freq = 5785, .hw_value = 157,},
	{.center_freq = 5805, .hw_value = 161,},
	{.center_freq = 5825, .hw_value = 165,
	 .flags = IEEE80211_CHAN_NO_HT40MINUS},
};

static struct ieee80211_rate rtw_ratetable[] = {
	{.bitrate = 10, .hw_value = 0x00,},
	{.bitrate = 20, .hw_value = 0x01,},
	{.bitrate = 55, .hw_value = 0x02,},
	{.bitrate = 110, .hw_value = 0x03,},
	{.bitrate = 60, .hw_value = 0x04,},
	{.bitrate = 90, .hw_value = 0x05,},
	{.bitrate = 120, .hw_value = 0x06,},
	{.bitrate = 180, .hw_value = 0x07,},
	{.bitrate = 240, .hw_value = 0x08,},
	{.bitrate = 360, .hw_value = 0x09,},
	{.bitrate = 480, .hw_value = 0x0a,},
	{.bitrate = 540, .hw_value = 0x0b,},
};

static struct ieee80211_supported_band rtw_band_2ghz = {
	.band = NL80211_BAND_2GHZ,

	.channels = rtw_channeltable_2g,
	.n_channels = ARRAY_SIZE(rtw_channeltable_2g),

	.bitrates = rtw_ratetable,
	.n_bitrates = ARRAY_SIZE(rtw_ratetable),

	.ht_cap = {0},
	.vht_cap = {0},
};

static struct ieee80211_supported_band rtw_band_5ghz = {
	.band = NL80211_BAND_5GHZ,

	.channels = rtw_channeltable_5g,
	.n_channels = ARRAY_SIZE(rtw_channeltable_5g),

	/* 5G has no CCK rates */
	.bitrates = rtw_ratetable + 4,
	.n_bitrates = ARRAY_SIZE(rtw_ratetable) - 4,

	.ht_cap = {0},
	.vht_cap = {0},
};

struct rtw_watch_dog_iter_data {
	struct rtw_vif *rtwvif;
	bool active;
	u8 assoc_cnt;
};

static void rtw_vif_watch_dog_iter(void *data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct rtw_watch_dog_iter_data *iter_data = data;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;

	if (vif->type == NL80211_IFTYPE_STATION) {
		if (vif->bss_conf.assoc) {
			iter_data->assoc_cnt++;
			iter_data->rtwvif = rtwvif;
		}
		if (rtwvif->stats.tx_cnt > RTW_LPS_THRESHOLD ||
		    rtwvif->stats.rx_cnt > RTW_LPS_THRESHOLD)
			iter_data->active = true;
	} else {
		/* only STATION mode can enter lps */
		iter_data->active = true;
	}

	rtwvif->stats.tx_unicast = 0;
	rtwvif->stats.rx_unicast = 0;
	rtwvif->stats.tx_cnt = 0;
	rtwvif->stats.rx_cnt = 0;
}

/* process TX/RX statistics periodically for hardware,
 * the information helps hardware to enhance performance
 */
static void rtw_watch_dog_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev,
					      watch_dog_work.work);
	struct rtw_watch_dog_iter_data data = {};

	if (!rtw_flag_check(rtwdev, RTW_FLAG_RUNNING))
		return;

	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->watch_dog_work,
				     RTW_WATCH_DOG_DELAY_TIME);

	/* reset tx/rx statictics */
	rtwdev->stats.tx_unicast = 0;
	rtwdev->stats.rx_unicast = 0;
	rtwdev->stats.tx_cnt = 0;
	rtwdev->stats.rx_cnt = 0;

	/* use atomic version to avoid taking local->iflist_mtx mutex */
	rtw_iterate_vifs_atomic(rtwdev, rtw_vif_watch_dog_iter, &data);

	/* fw supports only one station associated to enter lps, if there are
	 * more than two stations associated to the AP, then we can not enter
	 * lps, because fw does not handle the overlapped beacon interval
	 */
	if (rtw_fw_support_lps &&
	    data.rtwvif && !data.active && data.assoc_cnt == 1)
		rtw_enter_lps(rtwdev, data.rtwvif);

	if (rtw_flag_check(rtwdev, RTW_FLAG_SCANNING))
		return;

	rtw_phy_dynamic_mechanism(rtwdev);

	rtwdev->watch_dog_cnt++;
}

static void rtw_c2h_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev, c2h_work);
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&rtwdev->c2h_queue, skb, tmp) {
		skb_unlink(skb, &rtwdev->c2h_queue);
		rtw_fw_c2h_cmd_handle(rtwdev, skb);
		dev_kfree_skb_any(skb);
	}
}

void rtw_get_channel_params(struct cfg80211_chan_def *chandef,
			    struct rtw_channel_params *chan_params)
{
	struct ieee80211_channel *channel = chandef->chan;
	enum nl80211_chan_width width = chandef->width;
	u32 primary_freq, center_freq;
	u8 center_chan;
	u8 bandwidth = RTW_CHANNEL_WIDTH_20;
	u8 primary_chan_idx = 0;

	center_chan = channel->hw_value;
	primary_freq = channel->center_freq;
	center_freq = chandef->center_freq1;

	switch (width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		bandwidth = RTW_CHANNEL_WIDTH_20;
		primary_chan_idx = 0;
		break;
	case NL80211_CHAN_WIDTH_40:
		bandwidth = RTW_CHANNEL_WIDTH_40;
		if (primary_freq > center_freq) {
			primary_chan_idx = 1;
			center_chan -= 2;
		} else {
			primary_chan_idx = 2;
			center_chan += 2;
		}
		break;
	case NL80211_CHAN_WIDTH_80:
		bandwidth = RTW_CHANNEL_WIDTH_80;
		if (primary_freq > center_freq) {
			if (primary_freq - center_freq == 10) {
				primary_chan_idx = 1;
				center_chan -= 2;
			} else {
				primary_chan_idx = 3;
				center_chan -= 6;
			}
		} else {
			if (center_freq - primary_freq == 10) {
				primary_chan_idx = 2;
				center_chan += 2;
			} else {
				primary_chan_idx = 4;
				center_chan += 6;
			}
		}
		break;
	default:
		center_chan = 0;
		break;
	}

	chan_params->center_chan = center_chan;
	chan_params->bandwidth = bandwidth;
	chan_params->primary_chan_idx = primary_chan_idx;
}

void rtw_set_channel(struct rtw_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_channel_params ch_param;
	u8 center_chan, bandwidth, primary_chan_idx;

	rtw_get_channel_params(&hw->conf.chandef, &ch_param);
	if (WARN(ch_param.center_chan == 0, "Invalid channel\n"))
		return;

	center_chan = ch_param.center_chan;
	bandwidth = ch_param.bandwidth;
	primary_chan_idx = ch_param.primary_chan_idx;

	hal->current_band_width = bandwidth;
	hal->current_channel = center_chan;
	hal->current_band_type = center_chan > 14 ? RTW_BAND_5G : RTW_BAND_2G;
	chip->ops->set_channel(rtwdev, center_chan, bandwidth, primary_chan_idx);

	rtw_phy_set_tx_power_level(rtwdev, center_chan);
}

static void rtw_vif_write_addr(struct rtw_dev *rtwdev, u32 start, u8 *addr)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		rtw_write8(rtwdev, start + i, addr[i]);
}

void rtw_vif_port_config(struct rtw_dev *rtwdev,
			 struct rtw_vif *rtwvif,
			 u32 config)
{
	u32 addr, mask;

	if (config & PORT_SET_MAC_ADDR) {
		addr = rtwvif->conf->mac_addr.addr;
		rtw_vif_write_addr(rtwdev, addr, rtwvif->mac_addr);
	}
	if (config & PORT_SET_BSSID) {
		addr = rtwvif->conf->bssid.addr;
		rtw_vif_write_addr(rtwdev, addr, rtwvif->bssid);
	}
	if (config & PORT_SET_NET_TYPE) {
		addr = rtwvif->conf->net_type.addr;
		mask = rtwvif->conf->net_type.mask;
		rtw_write32_mask(rtwdev, addr, mask, rtwvif->net_type);
	}
	if (config & PORT_SET_AID) {
		addr = rtwvif->conf->aid.addr;
		mask = rtwvif->conf->aid.mask;
		rtw_write32_mask(rtwdev, addr, mask, rtwvif->aid);
	}
}

static u8 hw_bw_cap_to_bitamp(u8 bw_cap)
{
	u8 bw = 0;

	switch (bw_cap) {
	case EFUSE_HW_CAP_IGNORE:
	case EFUSE_HW_CAP_SUPP_BW80:
		bw |= BIT(RTW_CHANNEL_WIDTH_80);
		/* fall through */
	case EFUSE_HW_CAP_SUPP_BW40:
		bw |= BIT(RTW_CHANNEL_WIDTH_40);
		/* fall through */
	default:
		bw |= BIT(RTW_CHANNEL_WIDTH_20);
		break;
	}

	return bw;
}

static void rtw_hw_config_rf_ant_num(struct rtw_dev *rtwdev, u8 hw_ant_num)
{
	struct rtw_hal *hal = &rtwdev->hal;

	if (hw_ant_num == EFUSE_HW_CAP_IGNORE ||
	    hw_ant_num >= hal->rf_path_num)
		return;

	switch (hw_ant_num) {
	case 1:
		hal->rf_type = RF_1T1R;
		hal->rf_path_num = 1;
		hal->antenna_tx = BB_PATH_A;
		hal->antenna_rx = BB_PATH_A;
		break;
	default:
		WARN(1, "invalid hw configuration from efuse\n");
		break;
	}
}

static u64 get_vht_ra_mask(struct ieee80211_sta *sta)
{
	u64 ra_mask = 0;
	u16 mcs_map = le16_to_cpu(sta->vht_cap.vht_mcs.rx_mcs_map);
	u8 vht_mcs_cap;
	int i, nss;

	/* 4SS, every two bits for MCS7/8/9 */
	for (i = 0, nss = 12; i < 4; i++, mcs_map >>= 2, nss += 10) {
		vht_mcs_cap = mcs_map & 0x3;
		switch (vht_mcs_cap) {
		case 2: /* MCS9 */
			ra_mask |= 0x3ffULL << nss;
			break;
		case 1: /* MCS8 */
			ra_mask |= 0x1ffULL << nss;
			break;
		case 0: /* MCS7 */
			ra_mask |= 0x0ffULL << nss;
			break;
		default:
			break;
		}
	}

	return ra_mask;
}

static u8 get_rate_id(u8 wireless_set, enum rtw_bandwidth bw_mode, u8 tx_num)
{
	u8 rate_id = 0;

	switch (wireless_set) {
	case WIRELESS_CCK:
		rate_id = RTW_RATEID_B_20M;
		break;
	case WIRELESS_OFDM:
		rate_id = RTW_RATEID_G;
		break;
	case WIRELESS_CCK | WIRELESS_OFDM:
		rate_id = RTW_RATEID_BG;
		break;
	case WIRELESS_OFDM | WIRELESS_HT:
		if (tx_num == 1)
			rate_id = RTW_RATEID_GN_N1SS;
		else if (tx_num == 2)
			rate_id = RTW_RATEID_GN_N2SS;
		else if (tx_num == 3)
			rate_id = RTW_RATEID_ARFR5_N_3SS;
		break;
	case WIRELESS_CCK | WIRELESS_OFDM | WIRELESS_HT:
		if (bw_mode == RTW_CHANNEL_WIDTH_40) {
			if (tx_num == 1)
				rate_id = RTW_RATEID_BGN_40M_1SS;
			else if (tx_num == 2)
				rate_id = RTW_RATEID_BGN_40M_2SS;
			else if (tx_num == 3)
				rate_id = RTW_RATEID_ARFR5_N_3SS;
			else if (tx_num == 4)
				rate_id = RTW_RATEID_ARFR7_N_4SS;
		} else {
			if (tx_num == 1)
				rate_id = RTW_RATEID_BGN_20M_1SS;
			else if (tx_num == 2)
				rate_id = RTW_RATEID_BGN_20M_2SS;
			else if (tx_num == 3)
				rate_id = RTW_RATEID_ARFR5_N_3SS;
			else if (tx_num == 4)
				rate_id = RTW_RATEID_ARFR7_N_4SS;
		}
		break;
	case WIRELESS_OFDM | WIRELESS_VHT:
		if (tx_num == 1)
			rate_id = RTW_RATEID_ARFR1_AC_1SS;
		else if (tx_num == 2)
			rate_id = RTW_RATEID_ARFR0_AC_2SS;
		else if (tx_num == 3)
			rate_id = RTW_RATEID_ARFR4_AC_3SS;
		else if (tx_num == 4)
			rate_id = RTW_RATEID_ARFR6_AC_4SS;
		break;
	case WIRELESS_CCK | WIRELESS_OFDM | WIRELESS_VHT:
		if (bw_mode >= RTW_CHANNEL_WIDTH_80) {
			if (tx_num == 1)
				rate_id = RTW_RATEID_ARFR1_AC_1SS;
			else if (tx_num == 2)
				rate_id = RTW_RATEID_ARFR0_AC_2SS;
			else if (tx_num == 3)
				rate_id = RTW_RATEID_ARFR4_AC_3SS;
			else if (tx_num == 4)
				rate_id = RTW_RATEID_ARFR6_AC_4SS;
		} else {
			if (tx_num == 1)
				rate_id = RTW_RATEID_ARFR2_AC_2G_1SS;
			else if (tx_num == 2)
				rate_id = RTW_RATEID_ARFR3_AC_2G_2SS;
			else if (tx_num == 3)
				rate_id = RTW_RATEID_ARFR4_AC_3SS;
			else if (tx_num == 4)
				rate_id = RTW_RATEID_ARFR6_AC_4SS;
		}
		break;
	default:
		break;
	}

	return rate_id;
}

#define RA_MASK_CCK_RATES	0x0000f
#define RA_MASK_OFDM_RATES	0x00ff0
#define RA_MASK_HT_RATES_1SS	(0xff000ULL << 0)
#define RA_MASK_HT_RATES_2SS	(0xff000ULL << 8)
#define RA_MASK_HT_RATES_3SS	(0xff000ULL << 16)
#define RA_MASK_HT_RATES	(RA_MASK_HT_RATES_1SS | \
				 RA_MASK_HT_RATES_2SS | \
				 RA_MASK_HT_RATES_3SS)
#define RA_MASK_VHT_RATES_1SS	(0x3ff000ULL << 0)
#define RA_MASK_VHT_RATES_2SS	(0x3ff000ULL << 10)
#define RA_MASK_VHT_RATES_3SS	(0x3ff000ULL << 20)
#define RA_MASK_VHT_RATES	(RA_MASK_VHT_RATES_1SS | \
				 RA_MASK_VHT_RATES_2SS | \
				 RA_MASK_VHT_RATES_3SS)
#define RA_MASK_CCK_IN_HT	0x00005
#define RA_MASK_CCK_IN_VHT	0x00005
#define RA_MASK_OFDM_IN_VHT	0x00010
#define RA_MASK_OFDM_IN_HT_2G	0x00010
#define RA_MASK_OFDM_IN_HT_5G	0x00030

void rtw_update_sta_info(struct rtw_dev *rtwdev, struct rtw_sta_info *si)
{
	struct ieee80211_sta *sta = si->sta;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_hal *hal = &rtwdev->hal;
	u8 rssi_level;
	u8 wireless_set;
	u8 bw_mode;
	u8 rate_id;
	u8 rf_type = RF_1T1R;
	u8 stbc_en = 0;
	u8 ldpc_en = 0;
	u8 tx_num = 1;
	u64 ra_mask = 0;
	bool is_vht_enable = false;
	bool is_support_sgi = false;

	if (sta->vht_cap.vht_supported) {
		is_vht_enable = true;
		ra_mask |= get_vht_ra_mask(sta);
		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_RXSTBC_MASK)
			stbc_en = VHT_STBC_EN;
		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC)
			ldpc_en = VHT_LDPC_EN;
		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80)
			is_support_sgi = true;
	} else if (sta->ht_cap.ht_supported) {
		ra_mask |= (sta->ht_cap.mcs.rx_mask[NL80211_BAND_5GHZ] << 20) |
			   (sta->ht_cap.mcs.rx_mask[NL80211_BAND_2GHZ] << 12);
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_RX_STBC)
			stbc_en = HT_STBC_EN;
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING)
			ldpc_en = HT_LDPC_EN;
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20 ||
		    sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
			is_support_sgi = true;
	}

	if (hal->current_band_type == RTW_BAND_5G) {
		ra_mask |= (u64)sta->supp_rates[NL80211_BAND_5GHZ] << 4;
		if (sta->vht_cap.vht_supported) {
			ra_mask &= RA_MASK_VHT_RATES | RA_MASK_OFDM_IN_VHT;
			wireless_set = WIRELESS_OFDM | WIRELESS_VHT;
		} else if (sta->ht_cap.ht_supported) {
			ra_mask &= RA_MASK_HT_RATES | RA_MASK_OFDM_IN_HT_5G;
			wireless_set = WIRELESS_OFDM | WIRELESS_HT;
		} else {
			wireless_set = WIRELESS_OFDM;
		}
	} else if (hal->current_band_type == RTW_BAND_2G) {
		ra_mask |= sta->supp_rates[NL80211_BAND_2GHZ];
		if (sta->vht_cap.vht_supported) {
			ra_mask &= RA_MASK_VHT_RATES | RA_MASK_CCK_IN_VHT |
				   RA_MASK_OFDM_IN_VHT;
			wireless_set = WIRELESS_CCK | WIRELESS_OFDM |
				       WIRELESS_HT | WIRELESS_VHT;
		} else if (sta->ht_cap.ht_supported) {
			ra_mask &= RA_MASK_HT_RATES | RA_MASK_CCK_IN_HT |
				   RA_MASK_OFDM_IN_HT_2G;
			wireless_set = WIRELESS_CCK | WIRELESS_OFDM |
				       WIRELESS_HT;
		} else if (sta->supp_rates[0] <= 0xf) {
			wireless_set = WIRELESS_CCK;
		} else {
			wireless_set = WIRELESS_CCK | WIRELESS_OFDM;
		}
	} else {
		rtw_err(rtwdev, "Unknown band type\n");
		wireless_set = 0;
	}

	if (efuse->hw_cap.nss == 1) {
		ra_mask &= RA_MASK_VHT_RATES_1SS;
		ra_mask &= RA_MASK_HT_RATES_1SS;
	}

	switch (sta->bandwidth) {
	case IEEE80211_STA_RX_BW_80:
		bw_mode = RTW_CHANNEL_WIDTH_80;
		break;
	case IEEE80211_STA_RX_BW_40:
		bw_mode = RTW_CHANNEL_WIDTH_40;
		break;
	default:
		bw_mode = RTW_CHANNEL_WIDTH_20;
		break;
	}

	if (sta->vht_cap.vht_supported && ra_mask & 0xffc00000) {
		tx_num = 2;
		rf_type = RF_2T2R;
	} else if (sta->ht_cap.ht_supported && ra_mask & 0xfff00000) {
		tx_num = 2;
		rf_type = RF_2T2R;
	}

	rate_id = get_rate_id(wireless_set, bw_mode, tx_num);

	if (wireless_set != WIRELESS_CCK) {
		rssi_level = si->rssi_level;
		if (rssi_level == 0)
			ra_mask &= 0xffffffffffffffffULL;
		else if (rssi_level == 1)
			ra_mask &= 0xfffffffffffffff0ULL;
		else if (rssi_level == 2)
			ra_mask &= 0xffffffffffffefe0ULL;
		else if (rssi_level == 3)
			ra_mask &= 0xffffffffffffcfc0ULL;
		else if (rssi_level == 4)
			ra_mask &= 0xffffffffffff8f80ULL;
		else if (rssi_level >= 5)
			ra_mask &= 0xffffffffffff0f00ULL;
	}

	si->bw_mode = bw_mode;
	si->stbc_en = stbc_en;
	si->ldpc_en = ldpc_en;
	si->rf_type = rf_type;
	si->wireless_set = wireless_set;
	si->sgi_enable = is_support_sgi;
	si->vht_enable = is_vht_enable;
	si->ra_mask = ra_mask;
	si->rate_id = rate_id;

	rtw_fw_send_ra_info(rtwdev, si);
}

static int rtw_power_on(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_fw_state *fw = &rtwdev->fw;
	int ret;

	ret = rtw_hci_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup hci\n");
		goto err;
	}

	/* power on MAC before firmware downloaded */
	ret = rtw_mac_power_on(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to power on mac\n");
		goto err;
	}

	wait_for_completion(&fw->completion);
	if (!fw->firmware) {
		ret = -EINVAL;
		rtw_err(rtwdev, "failed to load firmware\n");
		goto err;
	}

	ret = rtw_download_firmware(rtwdev, fw);
	if (ret) {
		rtw_err(rtwdev, "failed to download firmware\n");
		goto err_off;
	}

	/* config mac after firmware downloaded */
	ret = rtw_mac_init(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to configure mac\n");
		goto err_off;
	}

	chip->ops->phy_set_param(rtwdev);

	ret = rtw_hci_start(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to start hci\n");
		goto err_off;
	}

	return 0;

err_off:
	rtw_mac_power_off(rtwdev);

err:
	return ret;
}

int rtw_core_start(struct rtw_dev *rtwdev)
{
	int ret;

	ret = rtw_power_on(rtwdev);
	if (ret)
		return ret;

	rtw_sec_enable_sec_engine(rtwdev);

	/* rcr reset after powered on */
	rtw_write32(rtwdev, REG_RCR, rtwdev->hal.rcr);

	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->watch_dog_work,
				     RTW_WATCH_DOG_DELAY_TIME);

	rtw_flag_set(rtwdev, RTW_FLAG_RUNNING);

	return 0;
}

static void rtw_power_off(struct rtw_dev *rtwdev)
{
	rtwdev->hci.ops->stop(rtwdev);
	rtw_mac_power_off(rtwdev);
}

void rtw_core_stop(struct rtw_dev *rtwdev)
{
	rtw_flag_clear(rtwdev, RTW_FLAG_RUNNING);
	rtw_flag_clear(rtwdev, RTW_FLAG_FW_RUNNING);

	cancel_delayed_work_sync(&rtwdev->watch_dog_work);

	rtw_power_off(rtwdev);
}

static void rtw_init_ht_cap(struct rtw_dev *rtwdev,
			    struct ieee80211_sta_ht_cap *ht_cap)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;

	ht_cap->ht_supported = true;
	ht_cap->cap = 0;
	ht_cap->cap |= IEEE80211_HT_CAP_SGI_20 |
			IEEE80211_HT_CAP_MAX_AMSDU |
			IEEE80211_HT_CAP_LDPC_CODING |
			(1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);
	if (efuse->hw_cap.bw & BIT(RTW_CHANNEL_WIDTH_40))
		ht_cap->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
				IEEE80211_HT_CAP_DSSSCCK40 |
				IEEE80211_HT_CAP_SGI_40;
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_cap->ampdu_density = IEEE80211_HT_MPDU_DENSITY_16;
	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	if (efuse->hw_cap.nss > 1) {
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0xFF;
		ht_cap->mcs.rx_mask[4] = 0x01;
		ht_cap->mcs.rx_highest = cpu_to_le16(300);
	} else {
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0x00;
		ht_cap->mcs.rx_mask[4] = 0x01;
		ht_cap->mcs.rx_highest = cpu_to_le16(150);
	}
}

static void rtw_init_vht_cap(struct rtw_dev *rtwdev,
			     struct ieee80211_sta_vht_cap *vht_cap)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u16 mcs_map;
	__le16 highest;

	if (efuse->hw_cap.ptcl != EFUSE_HW_CAP_IGNORE &&
	    efuse->hw_cap.ptcl != EFUSE_HW_CAP_PTCL_VHT)
		return;

	vht_cap->vht_supported = true;
	vht_cap->cap = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
		       IEEE80211_VHT_CAP_RXLDPC |
		       IEEE80211_VHT_CAP_SHORT_GI_80 |
		       IEEE80211_VHT_CAP_TXSTBC |
		       IEEE80211_VHT_CAP_RXSTBC_1 |
		       IEEE80211_VHT_CAP_HTC_VHT |
		       IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK |
		       0;
	mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 4 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 6 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 8 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 10 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 12 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 14;
	if (efuse->hw_cap.nss > 1) {
		highest = cpu_to_le16(780);
		mcs_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << 2;
	} else {
		highest = cpu_to_le16(390);
		mcs_map |= IEEE80211_VHT_MCS_NOT_SUPPORTED << 2;
	}

	vht_cap->vht_mcs.rx_mcs_map = cpu_to_le16(mcs_map);
	vht_cap->vht_mcs.tx_mcs_map = cpu_to_le16(mcs_map);
	vht_cap->vht_mcs.rx_highest = highest;
	vht_cap->vht_mcs.tx_highest = highest;
}

static void rtw_set_supported_band(struct ieee80211_hw *hw,
				   struct rtw_chip_info *chip)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct ieee80211_supported_band *sband;

	if (chip->band & RTW_BAND_2G) {
		sband = kmemdup(&rtw_band_2ghz, sizeof(*sband), GFP_KERNEL);
		if (!sband)
			goto err_out;
		if (chip->ht_supported)
			rtw_init_ht_cap(rtwdev, &sband->ht_cap);
		hw->wiphy->bands[NL80211_BAND_2GHZ] = sband;
	}

	if (chip->band & RTW_BAND_5G) {
		sband = kmemdup(&rtw_band_5ghz, sizeof(*sband), GFP_KERNEL);
		if (!sband)
			goto err_out;
		if (chip->ht_supported)
			rtw_init_ht_cap(rtwdev, &sband->ht_cap);
		if (chip->vht_supported)
			rtw_init_vht_cap(rtwdev, &sband->vht_cap);
		hw->wiphy->bands[NL80211_BAND_5GHZ] = sband;
	}

	return;

err_out:
	rtw_err(rtwdev, "failed to set supported band\n");
	kfree(sband);
}

static void rtw_unset_supported_band(struct ieee80211_hw *hw,
				     struct rtw_chip_info *chip)
{
	kfree(hw->wiphy->bands[NL80211_BAND_2GHZ]);
	kfree(hw->wiphy->bands[NL80211_BAND_5GHZ]);
}

static void rtw_load_firmware_cb(const struct firmware *firmware, void *context)
{
	struct rtw_dev *rtwdev = context;
	struct rtw_fw_state *fw = &rtwdev->fw;

	if (!firmware)
		rtw_err(rtwdev, "failed to request firmware\n");

	fw->firmware = firmware;
	complete_all(&fw->completion);
}

static int rtw_load_firmware(struct rtw_dev *rtwdev, const char *fw_name)
{
	struct rtw_fw_state *fw = &rtwdev->fw;
	int ret;

	init_completion(&fw->completion);

	ret = request_firmware_nowait(THIS_MODULE, true, fw_name, rtwdev->dev,
				      GFP_KERNEL, rtwdev, rtw_load_firmware_cb);
	if (ret) {
		rtw_err(rtwdev, "async firmware request failed\n");
		return ret;
	}

	return 0;
}

static int rtw_chip_parameter_setup(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u32 wl_bt_pwr_ctrl;
	int ret = 0;

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rtwdev->hci.rpwm_addr = 0x03d9;
		break;
	default:
		rtw_err(rtwdev, "unsupported hci type\n");
		return -EINVAL;
	}

	wl_bt_pwr_ctrl = rtw_read32(rtwdev, REG_WL_BT_PWR_CTRL);
	if (wl_bt_pwr_ctrl & BIT_BT_FUNC_EN)
		rtwdev->efuse.btcoex = true;
	hal->chip_version = rtw_read32(rtwdev, REG_SYS_CFG1);
	hal->fab_version = BIT_GET_VENDOR_ID(hal->chip_version) >> 2;
	hal->cut_version = BIT_GET_CHIP_VER(hal->chip_version);
	hal->mp_chip = (hal->chip_version & BIT_RTL_ID) ? 0 : 1;
	if (hal->chip_version & BIT_RF_TYPE_ID) {
		hal->rf_type = RF_2T2R;
		hal->rf_path_num = 2;
		hal->antenna_tx = BB_PATH_AB;
		hal->antenna_rx = BB_PATH_AB;
	} else {
		hal->rf_type = RF_1T1R;
		hal->rf_path_num = 1;
		hal->antenna_tx = BB_PATH_A;
		hal->antenna_rx = BB_PATH_A;
	}

	if (hal->fab_version == 2)
		hal->fab_version = 1;
	else if (hal->fab_version == 1)
		hal->fab_version = 2;

	efuse->physical_size = chip->phy_efuse_size;
	efuse->logical_size = chip->log_efuse_size;
	efuse->protect_size = chip->ptct_efuse_size;

	/* default use ack */
	rtwdev->hal.rcr |= BIT_VHT_DACK;

	return ret;
}

static int rtw_chip_efuse_enable(struct rtw_dev *rtwdev)
{
	struct rtw_fw_state *fw = &rtwdev->fw;
	int ret;

	ret = rtw_hci_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup hci\n");
		goto err;
	}

	ret = rtw_mac_power_on(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to power on mac\n");
		goto err;
	}

	rtw_write8(rtwdev, REG_C2HEVT, C2H_HW_FEATURE_DUMP);

	wait_for_completion(&fw->completion);
	if (!fw->firmware) {
		ret = -EINVAL;
		rtw_err(rtwdev, "failed to load firmware\n");
		goto err;
	}

	ret = rtw_download_firmware(rtwdev, fw);
	if (ret) {
		rtw_err(rtwdev, "failed to download firmware\n");
		goto err_off;
	}

	return 0;

err_off:
	rtw_mac_power_off(rtwdev);

err:
	return ret;
}

static int rtw_dump_hw_feature(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 hw_feature[HW_FEATURE_LEN];
	u8 id;
	u8 bw;
	int i;

	id = rtw_read8(rtwdev, REG_C2HEVT);
	if (id != C2H_HW_FEATURE_REPORT) {
		rtw_err(rtwdev, "failed to read hw feature report\n");
		return -EBUSY;
	}

	for (i = 0; i < HW_FEATURE_LEN; i++)
		hw_feature[i] = rtw_read8(rtwdev, REG_C2HEVT + 2 + i);

	rtw_write8(rtwdev, REG_C2HEVT, 0);

	bw = GET_EFUSE_HW_CAP_BW(hw_feature);
	efuse->hw_cap.bw = hw_bw_cap_to_bitamp(bw);
	efuse->hw_cap.hci = GET_EFUSE_HW_CAP_HCI(hw_feature);
	efuse->hw_cap.nss = GET_EFUSE_HW_CAP_NSS(hw_feature);
	efuse->hw_cap.ptcl = GET_EFUSE_HW_CAP_PTCL(hw_feature);
	efuse->hw_cap.ant_num = GET_EFUSE_HW_CAP_ANT_NUM(hw_feature);

	rtw_hw_config_rf_ant_num(rtwdev, efuse->hw_cap.ant_num);

	if (efuse->hw_cap.nss == EFUSE_HW_CAP_IGNORE)
		efuse->hw_cap.nss = rtwdev->hal.rf_path_num;

	rtw_dbg(rtwdev, RTW_DBG_EFUSE,
		"hw cap: hci=0x%02x, bw=0x%02x, ptcl=0x%02x, ant_num=%d, nss=%d\n",
		efuse->hw_cap.hci, efuse->hw_cap.bw, efuse->hw_cap.ptcl,
		efuse->hw_cap.ant_num, efuse->hw_cap.nss);

	return 0;
}

static void rtw_chip_efuse_disable(struct rtw_dev *rtwdev)
{
	rtw_hci_stop(rtwdev);
	rtw_mac_power_off(rtwdev);
}

static int rtw_chip_efuse_info_setup(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	int ret;

	mutex_lock(&rtwdev->mutex);

	/* power on mac to read efuse */
	ret = rtw_chip_efuse_enable(rtwdev);
	if (ret)
		goto out;

	ret = rtw_parse_efuse_map(rtwdev);
	if (ret)
		goto out;

	ret = rtw_dump_hw_feature(rtwdev);
	if (ret)
		goto out;

	ret = rtw_check_supported_rfe(rtwdev);
	if (ret)
		goto out;

	if (efuse->crystal_cap == 0xff)
		efuse->crystal_cap = 0;
	if (efuse->pa_type_2g == 0xff)
		efuse->pa_type_2g = 0;
	if (efuse->pa_type_5g == 0xff)
		efuse->pa_type_5g = 0;
	if (efuse->lna_type_2g == 0xff)
		efuse->lna_type_2g = 0;
	if (efuse->lna_type_5g == 0xff)
		efuse->lna_type_5g = 0;
	if (efuse->channel_plan == 0xff)
		efuse->channel_plan = 0x7f;
	if (efuse->bt_setting & BIT(0))
		efuse->share_ant = true;
	if (efuse->regd == 0xff)
		efuse->regd = 0;

	efuse->ext_pa_2g = efuse->pa_type_2g & BIT(4) ? 1 : 0;
	efuse->ext_lna_2g = efuse->lna_type_2g & BIT(3) ? 1 : 0;
	efuse->ext_pa_5g = efuse->pa_type_5g & BIT(0) ? 1 : 0;
	efuse->ext_lna_2g = efuse->lna_type_5g & BIT(3) ? 1 : 0;

	rtw_chip_efuse_disable(rtwdev);

out:
	mutex_unlock(&rtwdev->mutex);
	return ret;
}

static int rtw_chip_board_info_setup(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	const struct rtw_rfe_def *rfe_def = rtw_get_rfe_def(rtwdev);

	if (!rfe_def)
		return -ENODEV;

	rtw_phy_setup_phy_cond(rtwdev, 0);

	rtw_hw_init_tx_power(hal);
	rtw_load_table(rtwdev, rfe_def->phy_pg_tbl);
	rtw_load_table(rtwdev, rfe_def->txpwr_lmt_tbl);
	rtw_phy_tx_power_by_rate_config(hal);
	rtw_phy_tx_power_limit_config(hal);

	return 0;
}

int rtw_chip_info_setup(struct rtw_dev *rtwdev)
{
	int ret;

	ret = rtw_chip_parameter_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup chip parameters\n");
		goto err_out;
	}

	ret = rtw_chip_efuse_info_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup chip efuse info\n");
		goto err_out;
	}

	ret = rtw_chip_board_info_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup chip board info\n");
		goto err_out;
	}

	return 0;

err_out:
	return ret;
}
EXPORT_SYMBOL(rtw_chip_info_setup);

int rtw_core_init(struct rtw_dev *rtwdev)
{
	int ret;

	INIT_LIST_HEAD(&rtwdev->rsvd_page_list);

	timer_setup(&rtwdev->tx_report.purge_timer,
		    rtw_tx_report_purge_timer, 0);

	INIT_DELAYED_WORK(&rtwdev->watch_dog_work, rtw_watch_dog_work);
	INIT_DELAYED_WORK(&rtwdev->lps_work, rtw_lps_work);
	INIT_WORK(&rtwdev->c2h_work, rtw_c2h_work);
	skb_queue_head_init(&rtwdev->c2h_queue);
	skb_queue_head_init(&rtwdev->tx_report.queue);

	spin_lock_init(&rtwdev->dm_lock);
	spin_lock_init(&rtwdev->rf_lock);
	spin_lock_init(&rtwdev->h2c.lock);
	spin_lock_init(&rtwdev->tx_report.q_lock);

	mutex_init(&rtwdev->mutex);
	mutex_init(&rtwdev->hal.tx_power_mutex);

	rtwdev->sec.total_cam_num = 32;
	rtwdev->hal.current_channel = 1;
	set_bit(RTW_BC_MC_MACID, rtwdev->mac_id_map);

	mutex_lock(&rtwdev->mutex);
	rtw_add_rsvd_page(rtwdev, RSVD_BEACON, false);
	mutex_unlock(&rtwdev->mutex);

	/* default rx filter setting */
	rtwdev->hal.rcr = BIT_APP_FCS | BIT_APP_MIC | BIT_APP_ICV |
			  BIT_HTC_LOC_CTRL | BIT_APP_PHYSTS |
			  BIT_AB | BIT_AM | BIT_APM;

	ret = rtw_load_firmware(rtwdev, rtwdev->chip->fw_name);
	if (ret) {
		rtw_warn(rtwdev, "no firmware loaded\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(rtw_core_init);

void rtw_core_deinit(struct rtw_dev *rtwdev)
{
	struct rtw_fw_state *fw = &rtwdev->fw;
	struct rtw_rsvd_page *rsvd_pkt, *tmp;
	unsigned long flags;

	if (fw->firmware)
		release_firmware(fw->firmware);

	spin_lock_irqsave(&rtwdev->tx_report.q_lock, flags);
	skb_queue_purge(&rtwdev->tx_report.queue);
	spin_unlock_irqrestore(&rtwdev->tx_report.q_lock, flags);

	list_for_each_entry_safe(rsvd_pkt, tmp, &rtwdev->rsvd_page_list, list) {
		list_del(&rsvd_pkt->list);
		kfree(rsvd_pkt);
	}

	mutex_destroy(&rtwdev->mutex);
	mutex_destroy(&rtwdev->hal.tx_power_mutex);
}
EXPORT_SYMBOL(rtw_core_deinit);

int rtw_register_hw(struct rtw_dev *rtwdev, struct ieee80211_hw *hw)
{
	int max_tx_headroom = 0;
	int ret;

	/* TODO: USB & SDIO may need extra room? */
	max_tx_headroom = rtwdev->chip->tx_pkt_desc_sz;

	hw->extra_tx_headroom = max_tx_headroom;
	hw->queues = IEEE80211_NUM_ACS;
	hw->sta_data_size = sizeof(struct rtw_sta_info);
	hw->vif_data_size = sizeof(struct rtw_vif);

	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, RX_INCLUDES_FCS);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				     BIT(NL80211_IFTYPE_AP) |
				     BIT(NL80211_IFTYPE_ADHOC) |
				     BIT(NL80211_IFTYPE_MESH_POINT);

	hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS |
			    WIPHY_FLAG_TDLS_EXTERNAL_SETUP;

	rtw_set_supported_band(hw, rtwdev->chip);
	SET_IEEE80211_PERM_ADDR(hw, rtwdev->efuse.addr);

	rtw_regd_init(rtwdev, rtw_regd_notifier);

	ret = ieee80211_register_hw(hw);
	if (ret) {
		rtw_err(rtwdev, "failed to register hw\n");
		return ret;
	}

	if (regulatory_hint(hw->wiphy, rtwdev->regd.alpha2))
		rtw_err(rtwdev, "regulatory_hint fail\n");

	rtw_debugfs_init(rtwdev);

	return 0;
}
EXPORT_SYMBOL(rtw_register_hw);

void rtw_unregister_hw(struct rtw_dev *rtwdev, struct ieee80211_hw *hw)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	ieee80211_unregister_hw(hw);
	rtw_unset_supported_band(hw, chip);
}
EXPORT_SYMBOL(rtw_unregister_hw);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ac wireless core module");
MODULE_LICENSE("Dual BSD/GPL");
