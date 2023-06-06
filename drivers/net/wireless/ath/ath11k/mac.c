// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <linux/bitfield.h>
#include <linux/inetdevice.h>
#include <net/if_inet6.h>
#include <net/ipv6.h>

#include "mac.h"
#include "core.h"
#include "debug.h"
#include "wmi.h"
#include "hw.h"
#include "dp_tx.h"
#include "dp_rx.h"
#include "testmode.h"
#include "peer.h"
#include "debugfs_sta.h"
#include "hif.h"
#include "wow.h"

#define CHAN2G(_channel, _freq, _flags) { \
	.band                   = NL80211_BAND_2GHZ, \
	.hw_value               = (_channel), \
	.center_freq            = (_freq), \
	.flags                  = (_flags), \
	.max_antenna_gain       = 0, \
	.max_power              = 30, \
}

#define CHAN5G(_channel, _freq, _flags) { \
	.band                   = NL80211_BAND_5GHZ, \
	.hw_value               = (_channel), \
	.center_freq            = (_freq), \
	.flags                  = (_flags), \
	.max_antenna_gain       = 0, \
	.max_power              = 30, \
}

#define CHAN6G(_channel, _freq, _flags) { \
	.band                   = NL80211_BAND_6GHZ, \
	.hw_value               = (_channel), \
	.center_freq            = (_freq), \
	.flags                  = (_flags), \
	.max_antenna_gain       = 0, \
	.max_power              = 30, \
}

static const struct ieee80211_channel ath11k_2ghz_channels[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

static const struct ieee80211_channel ath11k_5ghz_channels[] = {
	CHAN5G(36, 5180, 0),
	CHAN5G(40, 5200, 0),
	CHAN5G(44, 5220, 0),
	CHAN5G(48, 5240, 0),
	CHAN5G(52, 5260, 0),
	CHAN5G(56, 5280, 0),
	CHAN5G(60, 5300, 0),
	CHAN5G(64, 5320, 0),
	CHAN5G(100, 5500, 0),
	CHAN5G(104, 5520, 0),
	CHAN5G(108, 5540, 0),
	CHAN5G(112, 5560, 0),
	CHAN5G(116, 5580, 0),
	CHAN5G(120, 5600, 0),
	CHAN5G(124, 5620, 0),
	CHAN5G(128, 5640, 0),
	CHAN5G(132, 5660, 0),
	CHAN5G(136, 5680, 0),
	CHAN5G(140, 5700, 0),
	CHAN5G(144, 5720, 0),
	CHAN5G(149, 5745, 0),
	CHAN5G(153, 5765, 0),
	CHAN5G(157, 5785, 0),
	CHAN5G(161, 5805, 0),
	CHAN5G(165, 5825, 0),
	CHAN5G(169, 5845, 0),
	CHAN5G(173, 5865, 0),
	CHAN5G(177, 5885, 0),
};

static const struct ieee80211_channel ath11k_6ghz_channels[] = {
	CHAN6G(1, 5955, 0),
	CHAN6G(5, 5975, 0),
	CHAN6G(9, 5995, 0),
	CHAN6G(13, 6015, 0),
	CHAN6G(17, 6035, 0),
	CHAN6G(21, 6055, 0),
	CHAN6G(25, 6075, 0),
	CHAN6G(29, 6095, 0),
	CHAN6G(33, 6115, 0),
	CHAN6G(37, 6135, 0),
	CHAN6G(41, 6155, 0),
	CHAN6G(45, 6175, 0),
	CHAN6G(49, 6195, 0),
	CHAN6G(53, 6215, 0),
	CHAN6G(57, 6235, 0),
	CHAN6G(61, 6255, 0),
	CHAN6G(65, 6275, 0),
	CHAN6G(69, 6295, 0),
	CHAN6G(73, 6315, 0),
	CHAN6G(77, 6335, 0),
	CHAN6G(81, 6355, 0),
	CHAN6G(85, 6375, 0),
	CHAN6G(89, 6395, 0),
	CHAN6G(93, 6415, 0),
	CHAN6G(97, 6435, 0),
	CHAN6G(101, 6455, 0),
	CHAN6G(105, 6475, 0),
	CHAN6G(109, 6495, 0),
	CHAN6G(113, 6515, 0),
	CHAN6G(117, 6535, 0),
	CHAN6G(121, 6555, 0),
	CHAN6G(125, 6575, 0),
	CHAN6G(129, 6595, 0),
	CHAN6G(133, 6615, 0),
	CHAN6G(137, 6635, 0),
	CHAN6G(141, 6655, 0),
	CHAN6G(145, 6675, 0),
	CHAN6G(149, 6695, 0),
	CHAN6G(153, 6715, 0),
	CHAN6G(157, 6735, 0),
	CHAN6G(161, 6755, 0),
	CHAN6G(165, 6775, 0),
	CHAN6G(169, 6795, 0),
	CHAN6G(173, 6815, 0),
	CHAN6G(177, 6835, 0),
	CHAN6G(181, 6855, 0),
	CHAN6G(185, 6875, 0),
	CHAN6G(189, 6895, 0),
	CHAN6G(193, 6915, 0),
	CHAN6G(197, 6935, 0),
	CHAN6G(201, 6955, 0),
	CHAN6G(205, 6975, 0),
	CHAN6G(209, 6995, 0),
	CHAN6G(213, 7015, 0),
	CHAN6G(217, 7035, 0),
	CHAN6G(221, 7055, 0),
	CHAN6G(225, 7075, 0),
	CHAN6G(229, 7095, 0),
	CHAN6G(233, 7115, 0),

	/* new addition in IEEE Std 802.11ax-2021 */
	CHAN6G(2, 5935, 0),
};

static struct ieee80211_rate ath11k_legacy_rates[] = {
	{ .bitrate = 10,
	  .hw_value = ATH11K_HW_RATE_CCK_LP_1M },
	{ .bitrate = 20,
	  .hw_value = ATH11K_HW_RATE_CCK_LP_2M,
	  .hw_value_short = ATH11K_HW_RATE_CCK_SP_2M,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,
	  .hw_value = ATH11K_HW_RATE_CCK_LP_5_5M,
	  .hw_value_short = ATH11K_HW_RATE_CCK_SP_5_5M,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110,
	  .hw_value = ATH11K_HW_RATE_CCK_LP_11M,
	  .hw_value_short = ATH11K_HW_RATE_CCK_SP_11M,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },

	{ .bitrate = 60, .hw_value = ATH11K_HW_RATE_OFDM_6M },
	{ .bitrate = 90, .hw_value = ATH11K_HW_RATE_OFDM_9M },
	{ .bitrate = 120, .hw_value = ATH11K_HW_RATE_OFDM_12M },
	{ .bitrate = 180, .hw_value = ATH11K_HW_RATE_OFDM_18M },
	{ .bitrate = 240, .hw_value = ATH11K_HW_RATE_OFDM_24M },
	{ .bitrate = 360, .hw_value = ATH11K_HW_RATE_OFDM_36M },
	{ .bitrate = 480, .hw_value = ATH11K_HW_RATE_OFDM_48M },
	{ .bitrate = 540, .hw_value = ATH11K_HW_RATE_OFDM_54M },
};

static const int
ath11k_phymodes[NUM_NL80211_BANDS][ATH11K_CHAN_WIDTH_NUM] = {
	[NL80211_BAND_2GHZ] = {
			[NL80211_CHAN_WIDTH_5] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_10] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_20_NOHT] = MODE_11AX_HE20_2G,
			[NL80211_CHAN_WIDTH_20] = MODE_11AX_HE20_2G,
			[NL80211_CHAN_WIDTH_40] = MODE_11AX_HE40_2G,
			[NL80211_CHAN_WIDTH_80] = MODE_11AX_HE80_2G,
			[NL80211_CHAN_WIDTH_80P80] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_160] = MODE_UNKNOWN,
	},
	[NL80211_BAND_5GHZ] = {
			[NL80211_CHAN_WIDTH_5] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_10] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_20_NOHT] = MODE_11AX_HE20,
			[NL80211_CHAN_WIDTH_20] = MODE_11AX_HE20,
			[NL80211_CHAN_WIDTH_40] = MODE_11AX_HE40,
			[NL80211_CHAN_WIDTH_80] = MODE_11AX_HE80,
			[NL80211_CHAN_WIDTH_160] = MODE_11AX_HE160,
			[NL80211_CHAN_WIDTH_80P80] = MODE_11AX_HE80_80,
	},
	[NL80211_BAND_6GHZ] = {
			[NL80211_CHAN_WIDTH_5] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_10] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_20_NOHT] = MODE_11AX_HE20,
			[NL80211_CHAN_WIDTH_20] = MODE_11AX_HE20,
			[NL80211_CHAN_WIDTH_40] = MODE_11AX_HE40,
			[NL80211_CHAN_WIDTH_80] = MODE_11AX_HE80,
			[NL80211_CHAN_WIDTH_160] = MODE_11AX_HE160,
			[NL80211_CHAN_WIDTH_80P80] = MODE_11AX_HE80_80,
	},

};

const struct htt_rx_ring_tlv_filter ath11k_mac_mon_status_filter_default = {
	.rx_filter = HTT_RX_FILTER_TLV_FLAGS_MPDU_START |
		     HTT_RX_FILTER_TLV_FLAGS_PPDU_END |
		     HTT_RX_FILTER_TLV_FLAGS_PPDU_END_STATUS_DONE,
	.pkt_filter_flags0 = HTT_RX_FP_MGMT_FILTER_FLAGS0,
	.pkt_filter_flags1 = HTT_RX_FP_MGMT_FILTER_FLAGS1,
	.pkt_filter_flags2 = HTT_RX_FP_CTRL_FILTER_FLASG2,
	.pkt_filter_flags3 = HTT_RX_FP_DATA_FILTER_FLASG3 |
			     HTT_RX_FP_CTRL_FILTER_FLASG3
};

#define ATH11K_MAC_FIRST_OFDM_RATE_IDX 4
#define ath11k_g_rates ath11k_legacy_rates
#define ath11k_g_rates_size (ARRAY_SIZE(ath11k_legacy_rates))
#define ath11k_a_rates (ath11k_legacy_rates + 4)
#define ath11k_a_rates_size (ARRAY_SIZE(ath11k_legacy_rates) - 4)

#define ATH11K_MAC_SCAN_CMD_EVT_OVERHEAD		200 /* in msecs */

/* Overhead due to the processing of channel switch events from FW */
#define ATH11K_SCAN_CHANNEL_SWITCH_WMI_EVT_OVERHEAD	10 /* in msecs */

static const u32 ath11k_smps_map[] = {
	[WLAN_HT_CAP_SM_PS_STATIC] = WMI_PEER_SMPS_STATIC,
	[WLAN_HT_CAP_SM_PS_DYNAMIC] = WMI_PEER_SMPS_DYNAMIC,
	[WLAN_HT_CAP_SM_PS_INVALID] = WMI_PEER_SMPS_PS_NONE,
	[WLAN_HT_CAP_SM_PS_DISABLED] = WMI_PEER_SMPS_PS_NONE,
};

static int ath11k_start_vdev_delay(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif);

enum nl80211_he_ru_alloc ath11k_mac_phy_he_ru_to_nl80211_he_ru_alloc(u16 ru_phy)
{
	enum nl80211_he_ru_alloc ret;

	switch (ru_phy) {
	case RU_26:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_26;
		break;
	case RU_52:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_52;
		break;
	case RU_106:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_106;
		break;
	case RU_242:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_242;
		break;
	case RU_484:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_484;
		break;
	case RU_996:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_996;
		break;
	default:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_26;
		break;
	}

	return ret;
}

enum nl80211_he_ru_alloc ath11k_mac_he_ru_tones_to_nl80211_he_ru_alloc(u16 ru_tones)
{
	enum nl80211_he_ru_alloc ret;

	switch (ru_tones) {
	case 26:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_26;
		break;
	case 52:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_52;
		break;
	case 106:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_106;
		break;
	case 242:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_242;
		break;
	case 484:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_484;
		break;
	case 996:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_996;
		break;
	case (996 * 2):
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_2x996;
		break;
	default:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_26;
		break;
	}

	return ret;
}

enum nl80211_he_gi ath11k_mac_he_gi_to_nl80211_he_gi(u8 sgi)
{
	enum nl80211_he_gi ret;

	switch (sgi) {
	case RX_MSDU_START_SGI_0_8_US:
		ret = NL80211_RATE_INFO_HE_GI_0_8;
		break;
	case RX_MSDU_START_SGI_1_6_US:
		ret = NL80211_RATE_INFO_HE_GI_1_6;
		break;
	case RX_MSDU_START_SGI_3_2_US:
		ret = NL80211_RATE_INFO_HE_GI_3_2;
		break;
	default:
		ret = NL80211_RATE_INFO_HE_GI_0_8;
		break;
	}

	return ret;
}

u8 ath11k_mac_bw_to_mac80211_bw(u8 bw)
{
	u8 ret = 0;

	switch (bw) {
	case ATH11K_BW_20:
		ret = RATE_INFO_BW_20;
		break;
	case ATH11K_BW_40:
		ret = RATE_INFO_BW_40;
		break;
	case ATH11K_BW_80:
		ret = RATE_INFO_BW_80;
		break;
	case ATH11K_BW_160:
		ret = RATE_INFO_BW_160;
		break;
	}

	return ret;
}

enum ath11k_supported_bw ath11k_mac_mac80211_bw_to_ath11k_bw(enum rate_info_bw bw)
{
	switch (bw) {
	case RATE_INFO_BW_20:
		return ATH11K_BW_20;
	case RATE_INFO_BW_40:
		return ATH11K_BW_40;
	case RATE_INFO_BW_80:
		return ATH11K_BW_80;
	case RATE_INFO_BW_160:
		return ATH11K_BW_160;
	default:
		return ATH11K_BW_20;
	}
}

int ath11k_mac_hw_ratecode_to_legacy_rate(u8 hw_rc, u8 preamble, u8 *rateidx,
					  u16 *rate)
{
	/* As default, it is OFDM rates */
	int i = ATH11K_MAC_FIRST_OFDM_RATE_IDX;
	int max_rates_idx = ath11k_g_rates_size;

	if (preamble == WMI_RATE_PREAMBLE_CCK) {
		hw_rc &= ~ATH11k_HW_RATECODE_CCK_SHORT_PREAM_MASK;
		i = 0;
		max_rates_idx = ATH11K_MAC_FIRST_OFDM_RATE_IDX;
	}

	while (i < max_rates_idx) {
		if (hw_rc == ath11k_legacy_rates[i].hw_value) {
			*rateidx = i;
			*rate = ath11k_legacy_rates[i].bitrate;
			return 0;
		}
		i++;
	}

	return -EINVAL;
}

static int get_num_chains(u32 mask)
{
	int num_chains = 0;

	while (mask) {
		if (mask & BIT(0))
			num_chains++;
		mask >>= 1;
	}

	return num_chains;
}

u8 ath11k_mac_bitrate_to_idx(const struct ieee80211_supported_band *sband,
			     u32 bitrate)
{
	int i;

	for (i = 0; i < sband->n_bitrates; i++)
		if (sband->bitrates[i].bitrate == bitrate)
			return i;

	return 0;
}

static u32
ath11k_mac_max_ht_nss(const u8 ht_mcs_mask[IEEE80211_HT_MCS_MASK_LEN])
{
	int nss;

	for (nss = IEEE80211_HT_MCS_MASK_LEN - 1; nss >= 0; nss--)
		if (ht_mcs_mask[nss])
			return nss + 1;

	return 1;
}

static u32
ath11k_mac_max_vht_nss(const u16 vht_mcs_mask[NL80211_VHT_NSS_MAX])
{
	int nss;

	for (nss = NL80211_VHT_NSS_MAX - 1; nss >= 0; nss--)
		if (vht_mcs_mask[nss])
			return nss + 1;

	return 1;
}

static u32
ath11k_mac_max_he_nss(const u16 he_mcs_mask[NL80211_HE_NSS_MAX])
{
	int nss;

	for (nss = NL80211_HE_NSS_MAX - 1; nss >= 0; nss--)
		if (he_mcs_mask[nss])
			return nss + 1;

	return 1;
}

static u8 ath11k_parse_mpdudensity(u8 mpdudensity)
{
/* 802.11n D2.0 defined values for "Minimum MPDU Start Spacing":
 *   0 for no restriction
 *   1 for 1/4 us
 *   2 for 1/2 us
 *   3 for 1 us
 *   4 for 2 us
 *   5 for 4 us
 *   6 for 8 us
 *   7 for 16 us
 */
	switch (mpdudensity) {
	case 0:
		return 0;
	case 1:
	case 2:
	case 3:
	/* Our lower layer calculations limit our precision to
	 * 1 microsecond
	 */
		return 1;
	case 4:
		return 2;
	case 5:
		return 4;
	case 6:
		return 8;
	case 7:
		return 16;
	default:
		return 0;
	}
}

static int ath11k_mac_vif_chan(struct ieee80211_vif *vif,
			       struct cfg80211_chan_def *def)
{
	struct ieee80211_chanctx_conf *conf;

	rcu_read_lock();
	conf = rcu_dereference(vif->bss_conf.chanctx_conf);
	if (!conf) {
		rcu_read_unlock();
		return -ENOENT;
	}

	*def = conf->def;
	rcu_read_unlock();

	return 0;
}

static bool ath11k_mac_bitrate_is_cck(int bitrate)
{
	switch (bitrate) {
	case 10:
	case 20:
	case 55:
	case 110:
		return true;
	}

	return false;
}

u8 ath11k_mac_hw_rate_to_idx(const struct ieee80211_supported_band *sband,
			     u8 hw_rate, bool cck)
{
	const struct ieee80211_rate *rate;
	int i;

	for (i = 0; i < sband->n_bitrates; i++) {
		rate = &sband->bitrates[i];

		if (ath11k_mac_bitrate_is_cck(rate->bitrate) != cck)
			continue;

		if (rate->hw_value == hw_rate)
			return i;
		else if (rate->flags & IEEE80211_RATE_SHORT_PREAMBLE &&
			 rate->hw_value_short == hw_rate)
			return i;
	}

	return 0;
}

static u8 ath11k_mac_bitrate_to_rate(int bitrate)
{
	return DIV_ROUND_UP(bitrate, 5) |
	       (ath11k_mac_bitrate_is_cck(bitrate) ? BIT(7) : 0);
}

static void ath11k_get_arvif_iter(void *data, u8 *mac,
				  struct ieee80211_vif *vif)
{
	struct ath11k_vif_iter *arvif_iter = data;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;

	if (arvif->vdev_id == arvif_iter->vdev_id)
		arvif_iter->arvif = arvif;
}

struct ath11k_vif *ath11k_mac_get_arvif(struct ath11k *ar, u32 vdev_id)
{
	struct ath11k_vif_iter arvif_iter;
	u32 flags;

	memset(&arvif_iter, 0, sizeof(struct ath11k_vif_iter));
	arvif_iter.vdev_id = vdev_id;

	flags = IEEE80211_IFACE_ITER_RESUME_ALL;
	ieee80211_iterate_active_interfaces_atomic(ar->hw,
						   flags,
						   ath11k_get_arvif_iter,
						   &arvif_iter);
	if (!arvif_iter.arvif) {
		ath11k_warn(ar->ab, "No VIF found for vdev %d\n", vdev_id);
		return NULL;
	}

	return arvif_iter.arvif;
}

struct ath11k_vif *ath11k_mac_get_arvif_by_vdev_id(struct ath11k_base *ab,
						   u32 vdev_id)
{
	int i;
	struct ath11k_pdev *pdev;
	struct ath11k_vif *arvif;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = rcu_dereference(ab->pdevs_active[i]);
		if (pdev && pdev->ar &&
		    (pdev->ar->allocated_vdev_map & (1LL << vdev_id))) {
			arvif = ath11k_mac_get_arvif(pdev->ar, vdev_id);
			if (arvif)
				return arvif;
		}
	}

	return NULL;
}

struct ath11k *ath11k_mac_get_ar_by_vdev_id(struct ath11k_base *ab, u32 vdev_id)
{
	int i;
	struct ath11k_pdev *pdev;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = rcu_dereference(ab->pdevs_active[i]);
		if (pdev && pdev->ar) {
			if (pdev->ar->allocated_vdev_map & (1LL << vdev_id))
				return pdev->ar;
		}
	}

	return NULL;
}

struct ath11k *ath11k_mac_get_ar_by_pdev_id(struct ath11k_base *ab, u32 pdev_id)
{
	int i;
	struct ath11k_pdev *pdev;

	if (ab->hw_params.single_pdev_only) {
		pdev = rcu_dereference(ab->pdevs_active[0]);
		return pdev ? pdev->ar : NULL;
	}

	if (WARN_ON(pdev_id > ab->num_radios))
		return NULL;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = rcu_dereference(ab->pdevs_active[i]);

		if (pdev && pdev->pdev_id == pdev_id)
			return (pdev->ar ? pdev->ar : NULL);
	}

	return NULL;
}

struct ath11k_vif *ath11k_mac_get_vif_up(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	struct ath11k_vif *arvif;
	int i;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		list_for_each_entry(arvif, &ar->arvifs, list) {
			if (arvif->is_up)
				return arvif;
		}
	}

	return NULL;
}

static bool ath11k_mac_band_match(enum nl80211_band band1, enum WMI_HOST_WLAN_BAND band2)
{
	return (((band1 == NL80211_BAND_2GHZ) && (band2 & WMI_HOST_WLAN_2G_CAP)) ||
		(((band1 == NL80211_BAND_5GHZ) || (band1 == NL80211_BAND_6GHZ)) &&
		   (band2 & WMI_HOST_WLAN_5G_CAP)));
}

u8 ath11k_mac_get_target_pdev_id_from_vif(struct ath11k_vif *arvif)
{
	struct ath11k *ar = arvif->ar;
	struct ath11k_base *ab = ar->ab;
	struct ieee80211_vif *vif = arvif->vif;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	u8 pdev_id = ab->target_pdev_ids[0].pdev_id;
	int i;

	if (WARN_ON(ath11k_mac_vif_chan(vif, &def)))
		return pdev_id;

	band = def.chan->band;

	for (i = 0; i < ab->target_pdev_count; i++) {
		if (ath11k_mac_band_match(band, ab->target_pdev_ids[i].supported_bands))
			return ab->target_pdev_ids[i].pdev_id;
	}

	return pdev_id;
}

u8 ath11k_mac_get_target_pdev_id(struct ath11k *ar)
{
	struct ath11k_vif *arvif;

	arvif = ath11k_mac_get_vif_up(ar->ab);

	if (arvif)
		return ath11k_mac_get_target_pdev_id_from_vif(arvif);
	else
		return ar->ab->target_pdev_ids[0].pdev_id;
}

static void ath11k_pdev_caps_update(struct ath11k *ar)
{
	struct ath11k_base *ab = ar->ab;

	ar->max_tx_power = ab->target_caps.hw_max_tx_power;

	/* FIXME Set min_tx_power to ab->target_caps.hw_min_tx_power.
	 * But since the received value in svcrdy is same as hw_max_tx_power,
	 * we can set ar->min_tx_power to 0 currently until
	 * this is fixed in firmware
	 */
	ar->min_tx_power = 0;

	ar->txpower_limit_2g = ar->max_tx_power;
	ar->txpower_limit_5g = ar->max_tx_power;
	ar->txpower_scale = WMI_HOST_TP_SCALE_MAX;
}

static int ath11k_mac_txpower_recalc(struct ath11k *ar)
{
	struct ath11k_pdev *pdev = ar->pdev;
	struct ath11k_vif *arvif;
	int ret, txpower = -1;
	u32 param;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (arvif->txpower <= 0)
			continue;

		if (txpower == -1)
			txpower = arvif->txpower;
		else
			txpower = min(txpower, arvif->txpower);
	}

	if (txpower == -1)
		return 0;

	/* txpwr is set as 2 units per dBm in FW*/
	txpower = min_t(u32, max_t(u32, ar->min_tx_power, txpower),
			ar->max_tx_power) * 2;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "txpower to set in hw %d\n",
		   txpower / 2);

	if ((pdev->cap.supported_bands & WMI_HOST_WLAN_2G_CAP) &&
	    ar->txpower_limit_2g != txpower) {
		param = WMI_PDEV_PARAM_TXPOWER_LIMIT2G;
		ret = ath11k_wmi_pdev_set_param(ar, param,
						txpower, ar->pdev->pdev_id);
		if (ret)
			goto fail;
		ar->txpower_limit_2g = txpower;
	}

	if ((pdev->cap.supported_bands & WMI_HOST_WLAN_5G_CAP) &&
	    ar->txpower_limit_5g != txpower) {
		param = WMI_PDEV_PARAM_TXPOWER_LIMIT5G;
		ret = ath11k_wmi_pdev_set_param(ar, param,
						txpower, ar->pdev->pdev_id);
		if (ret)
			goto fail;
		ar->txpower_limit_5g = txpower;
	}

	return 0;

fail:
	ath11k_warn(ar->ab, "failed to recalc txpower limit %d using pdev param %d: %d\n",
		    txpower / 2, param, ret);
	return ret;
}

static int ath11k_recalc_rtscts_prot(struct ath11k_vif *arvif)
{
	struct ath11k *ar = arvif->ar;
	u32 vdev_param, rts_cts = 0;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	vdev_param = WMI_VDEV_PARAM_ENABLE_RTSCTS;

	/* Enable RTS/CTS protection for sw retries (when legacy stations
	 * are in BSS) or by default only for second rate series.
	 * TODO: Check if we need to enable CTS 2 Self in any case
	 */
	rts_cts = WMI_USE_RTS_CTS;

	if (arvif->num_legacy_stations > 0)
		rts_cts |= WMI_RTSCTS_ACROSS_SW_RETRIES << 4;
	else
		rts_cts |= WMI_RTSCTS_FOR_SECOND_RATESERIES << 4;

	/* Need not send duplicate param value to firmware */
	if (arvif->rtscts_prot_mode == rts_cts)
		return 0;

	arvif->rtscts_prot_mode = rts_cts;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac vdev %d recalc rts/cts prot %d\n",
		   arvif->vdev_id, rts_cts);

	ret =  ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					     vdev_param, rts_cts);
	if (ret)
		ath11k_warn(ar->ab, "failed to recalculate rts/cts prot for vdev %d: %d\n",
			    arvif->vdev_id, ret);

	return ret;
}

static int ath11k_mac_set_kickout(struct ath11k_vif *arvif)
{
	struct ath11k *ar = arvif->ar;
	u32 param;
	int ret;

	ret = ath11k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_STA_KICKOUT_TH,
					ATH11K_KICKOUT_THRESHOLD,
					ar->pdev->pdev_id);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set kickout threshold on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	param = WMI_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS;
	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, param,
					    ATH11K_KEEPALIVE_MIN_IDLE);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set keepalive minimum idle time on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	param = WMI_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS;
	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, param,
					    ATH11K_KEEPALIVE_MAX_IDLE);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set keepalive maximum idle time on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	param = WMI_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS;
	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, param,
					    ATH11K_KEEPALIVE_MAX_UNRESPONSIVE);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set keepalive maximum unresponsive time on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	return 0;
}

void ath11k_mac_peer_cleanup_all(struct ath11k *ar)
{
	struct ath11k_peer *peer, *tmp;
	struct ath11k_base *ab = ar->ab;

	lockdep_assert_held(&ar->conf_mutex);

	mutex_lock(&ab->tbl_mtx_lock);
	spin_lock_bh(&ab->base_lock);
	list_for_each_entry_safe(peer, tmp, &ab->peers, list) {
		ath11k_peer_rx_tid_cleanup(ar, peer);
		ath11k_peer_rhash_delete(ab, peer);
		list_del(&peer->list);
		kfree(peer);
	}
	spin_unlock_bh(&ab->base_lock);
	mutex_unlock(&ab->tbl_mtx_lock);

	ar->num_peers = 0;
	ar->num_stations = 0;
}

static inline int ath11k_mac_vdev_setup_sync(struct ath11k *ar)
{
	lockdep_assert_held(&ar->conf_mutex);

	if (test_bit(ATH11K_FLAG_CRASH_FLUSH, &ar->ab->dev_flags))
		return -ESHUTDOWN;

	if (!wait_for_completion_timeout(&ar->vdev_setup_done,
					 ATH11K_VDEV_SETUP_TIMEOUT_HZ))
		return -ETIMEDOUT;

	return ar->last_wmi_vdev_start_status ? -EINVAL : 0;
}

static void
ath11k_mac_get_any_chandef_iter(struct ieee80211_hw *hw,
				struct ieee80211_chanctx_conf *conf,
				void *data)
{
	struct cfg80211_chan_def **def = data;

	*def = &conf->def;
}

static int ath11k_mac_monitor_vdev_start(struct ath11k *ar, int vdev_id,
					 struct cfg80211_chan_def *chandef)
{
	struct ieee80211_channel *channel;
	struct wmi_vdev_start_req_arg arg = {};
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	channel = chandef->chan;

	arg.vdev_id = vdev_id;
	arg.channel.freq = channel->center_freq;
	arg.channel.band_center_freq1 = chandef->center_freq1;
	arg.channel.band_center_freq2 = chandef->center_freq2;

	arg.channel.mode = ath11k_phymodes[chandef->chan->band][chandef->width];
	arg.channel.chan_radar = !!(channel->flags & IEEE80211_CHAN_RADAR);

	arg.channel.min_power = 0;
	arg.channel.max_power = channel->max_power;
	arg.channel.max_reg_power = channel->max_reg_power;
	arg.channel.max_antenna_gain = channel->max_antenna_gain;

	arg.pref_tx_streams = ar->num_tx_chains;
	arg.pref_rx_streams = ar->num_rx_chains;

	arg.channel.passive = !!(chandef->chan->flags & IEEE80211_CHAN_NO_IR);

	reinit_completion(&ar->vdev_setup_done);
	reinit_completion(&ar->vdev_delete_done);

	ret = ath11k_wmi_vdev_start(ar, &arg, false);
	if (ret) {
		ath11k_warn(ar->ab, "failed to request monitor vdev %i start: %d\n",
			    vdev_id, ret);
		return ret;
	}

	ret = ath11k_mac_vdev_setup_sync(ar);
	if (ret) {
		ath11k_warn(ar->ab, "failed to synchronize setup for monitor vdev %i start: %d\n",
			    vdev_id, ret);
		return ret;
	}

	ret = ath11k_wmi_vdev_up(ar, vdev_id, 0, ar->mac_addr);
	if (ret) {
		ath11k_warn(ar->ab, "failed to put up monitor vdev %i: %d\n",
			    vdev_id, ret);
		goto vdev_stop;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac monitor vdev %i started\n",
		   vdev_id);

	return 0;

vdev_stop:
	reinit_completion(&ar->vdev_setup_done);

	ret = ath11k_wmi_vdev_stop(ar, vdev_id);
	if (ret) {
		ath11k_warn(ar->ab, "failed to stop monitor vdev %i after start failure: %d\n",
			    vdev_id, ret);
		return ret;
	}

	ret = ath11k_mac_vdev_setup_sync(ar);
	if (ret) {
		ath11k_warn(ar->ab, "failed to synchronize setup for vdev %i stop: %d\n",
			    vdev_id, ret);
		return ret;
	}

	return -EIO;
}

static int ath11k_mac_monitor_vdev_stop(struct ath11k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	reinit_completion(&ar->vdev_setup_done);

	ret = ath11k_wmi_vdev_stop(ar, ar->monitor_vdev_id);
	if (ret) {
		ath11k_warn(ar->ab, "failed to request monitor vdev %i stop: %d\n",
			    ar->monitor_vdev_id, ret);
		return ret;
	}

	ret = ath11k_mac_vdev_setup_sync(ar);
	if (ret) {
		ath11k_warn(ar->ab, "failed to synchronize monitor vdev %i stop: %d\n",
			    ar->monitor_vdev_id, ret);
		return ret;
	}

	ret = ath11k_wmi_vdev_down(ar, ar->monitor_vdev_id);
	if (ret) {
		ath11k_warn(ar->ab, "failed to put down monitor vdev %i: %d\n",
			    ar->monitor_vdev_id, ret);
		return ret;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac monitor vdev %i stopped\n",
		   ar->monitor_vdev_id);

	return 0;
}

static int ath11k_mac_monitor_vdev_create(struct ath11k *ar)
{
	struct ath11k_pdev *pdev = ar->pdev;
	struct vdev_create_params param = {};
	int bit, ret;
	u8 tmp_addr[6] = {0};
	u16 nss;

	lockdep_assert_held(&ar->conf_mutex);

	if (test_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED, &ar->monitor_flags))
		return 0;

	if (ar->ab->free_vdev_map == 0) {
		ath11k_warn(ar->ab, "failed to find free vdev id for monitor vdev\n");
		return -ENOMEM;
	}

	bit = __ffs64(ar->ab->free_vdev_map);

	ar->monitor_vdev_id = bit;

	param.if_id = ar->monitor_vdev_id;
	param.type = WMI_VDEV_TYPE_MONITOR;
	param.subtype = WMI_VDEV_SUBTYPE_NONE;
	param.pdev_id = pdev->pdev_id;

	if (pdev->cap.supported_bands & WMI_HOST_WLAN_2G_CAP) {
		param.chains[NL80211_BAND_2GHZ].tx = ar->num_tx_chains;
		param.chains[NL80211_BAND_2GHZ].rx = ar->num_rx_chains;
	}
	if (pdev->cap.supported_bands & WMI_HOST_WLAN_5G_CAP) {
		param.chains[NL80211_BAND_5GHZ].tx = ar->num_tx_chains;
		param.chains[NL80211_BAND_5GHZ].rx = ar->num_rx_chains;
	}

	ret = ath11k_wmi_vdev_create(ar, tmp_addr, &param);
	if (ret) {
		ath11k_warn(ar->ab, "failed to request monitor vdev %i creation: %d\n",
			    ar->monitor_vdev_id, ret);
		ar->monitor_vdev_id = -1;
		return ret;
	}

	nss = get_num_chains(ar->cfg_tx_chainmask) ? : 1;
	ret = ath11k_wmi_vdev_set_param_cmd(ar, ar->monitor_vdev_id,
					    WMI_VDEV_PARAM_NSS, nss);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set vdev %d chainmask 0x%x, nss %d :%d\n",
			    ar->monitor_vdev_id, ar->cfg_tx_chainmask, nss, ret);
		goto err_vdev_del;
	}

	ret = ath11k_mac_txpower_recalc(ar);
	if (ret) {
		ath11k_warn(ar->ab, "failed to recalc txpower for monitor vdev %d: %d\n",
			    ar->monitor_vdev_id, ret);
		goto err_vdev_del;
	}

	ar->allocated_vdev_map |= 1LL << ar->monitor_vdev_id;
	ar->ab->free_vdev_map &= ~(1LL << ar->monitor_vdev_id);
	ar->num_created_vdevs++;
	set_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED, &ar->monitor_flags);

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac monitor vdev %d created\n",
		   ar->monitor_vdev_id);

	return 0;

err_vdev_del:
	ath11k_wmi_vdev_delete(ar, ar->monitor_vdev_id);
	ar->monitor_vdev_id = -1;
	return ret;
}

static int ath11k_mac_monitor_vdev_delete(struct ath11k *ar)
{
	int ret;
	unsigned long time_left;

	lockdep_assert_held(&ar->conf_mutex);

	if (!test_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED, &ar->monitor_flags))
		return 0;

	reinit_completion(&ar->vdev_delete_done);

	ret = ath11k_wmi_vdev_delete(ar, ar->monitor_vdev_id);
	if (ret) {
		ath11k_warn(ar->ab, "failed to request wmi monitor vdev %i removal: %d\n",
			    ar->monitor_vdev_id, ret);
		return ret;
	}

	time_left = wait_for_completion_timeout(&ar->vdev_delete_done,
						ATH11K_VDEV_DELETE_TIMEOUT_HZ);
	if (time_left == 0) {
		ath11k_warn(ar->ab, "Timeout in receiving vdev delete response\n");
	} else {
		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac monitor vdev %d deleted\n",
			   ar->monitor_vdev_id);

		ar->allocated_vdev_map &= ~(1LL << ar->monitor_vdev_id);
		ar->ab->free_vdev_map |= 1LL << (ar->monitor_vdev_id);
		ar->num_created_vdevs--;
		ar->monitor_vdev_id = -1;
		clear_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED, &ar->monitor_flags);
	}

	return ret;
}

static int ath11k_mac_monitor_start(struct ath11k *ar)
{
	struct cfg80211_chan_def *chandef = NULL;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	if (test_bit(ATH11K_FLAG_MONITOR_STARTED, &ar->monitor_flags))
		return 0;

	ieee80211_iter_chan_contexts_atomic(ar->hw,
					    ath11k_mac_get_any_chandef_iter,
					    &chandef);
	if (!chandef)
		return 0;

	ret = ath11k_mac_monitor_vdev_start(ar, ar->monitor_vdev_id, chandef);
	if (ret) {
		ath11k_warn(ar->ab, "failed to start monitor vdev: %d\n", ret);
		ath11k_mac_monitor_vdev_delete(ar);
		return ret;
	}

	set_bit(ATH11K_FLAG_MONITOR_STARTED, &ar->monitor_flags);

	ar->num_started_vdevs++;
	ret = ath11k_dp_tx_htt_monitor_mode_ring_config(ar, false);
	if (ret) {
		ath11k_warn(ar->ab, "failed to configure htt monitor mode ring during start: %d",
			    ret);
		return ret;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac monitor started\n");

	return 0;
}

static int ath11k_mac_monitor_stop(struct ath11k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	if (!test_bit(ATH11K_FLAG_MONITOR_STARTED, &ar->monitor_flags))
		return 0;

	ret = ath11k_mac_monitor_vdev_stop(ar);
	if (ret) {
		ath11k_warn(ar->ab, "failed to stop monitor vdev: %d\n", ret);
		return ret;
	}

	clear_bit(ATH11K_FLAG_MONITOR_STARTED, &ar->monitor_flags);
	ar->num_started_vdevs--;

	ret = ath11k_dp_tx_htt_monitor_mode_ring_config(ar, true);
	if (ret) {
		ath11k_warn(ar->ab, "failed to configure htt monitor mode ring during stop: %d",
			    ret);
		return ret;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac monitor stopped ret %d\n", ret);

	return 0;
}

static int ath11k_mac_vif_setup_ps(struct ath11k_vif *arvif)
{
	struct ath11k *ar = arvif->ar;
	struct ieee80211_vif *vif = arvif->vif;
	struct ieee80211_conf *conf = &ar->hw->conf;
	enum wmi_sta_powersave_param param;
	enum wmi_sta_ps_mode psmode;
	int ret;
	int timeout;
	bool enable_ps;

	lockdep_assert_held(&arvif->ar->conf_mutex);

	if (arvif->vif->type != NL80211_IFTYPE_STATION)
		return 0;

	enable_ps = arvif->ps;

	if (!arvif->is_started) {
		/* mac80211 can update vif powersave state while disconnected.
		 * Firmware doesn't behave nicely and consumes more power than
		 * necessary if PS is disabled on a non-started vdev. Hence
		 * force-enable PS for non-running vdevs.
		 */
		psmode = WMI_STA_PS_MODE_ENABLED;
	} else if (enable_ps) {
		psmode = WMI_STA_PS_MODE_ENABLED;
		param = WMI_STA_PS_PARAM_INACTIVITY_TIME;

		timeout = conf->dynamic_ps_timeout;
		if (timeout == 0) {
			/* firmware doesn't like 0 */
			timeout = ieee80211_tu_to_usec(vif->bss_conf.beacon_int) / 1000;
		}

		ret = ath11k_wmi_set_sta_ps_param(ar, arvif->vdev_id, param,
						  timeout);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set inactivity time for vdev %d: %i\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	} else {
		psmode = WMI_STA_PS_MODE_DISABLED;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac vdev %d psmode %s\n",
		   arvif->vdev_id, psmode ? "enable" : "disable");

	ret = ath11k_wmi_pdev_set_ps_mode(ar, arvif->vdev_id, psmode);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set sta power save mode %d for vdev %d: %d\n",
			    psmode, arvif->vdev_id, ret);
		return ret;
	}

	return 0;
}

static int ath11k_mac_config_ps(struct ath11k *ar)
{
	struct ath11k_vif *arvif;
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ret = ath11k_mac_vif_setup_ps(arvif);
		if (ret) {
			ath11k_warn(ar->ab, "failed to setup powersave: %d\n", ret);
			break;
		}
	}

	return ret;
}

static int ath11k_mac_op_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ath11k *ar = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	int ret = 0;

	mutex_lock(&ar->conf_mutex);

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		if (conf->flags & IEEE80211_CONF_MONITOR) {
			set_bit(ATH11K_FLAG_MONITOR_CONF_ENABLED, &ar->monitor_flags);

			if (test_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED,
				     &ar->monitor_flags))
				goto out;

			ret = ath11k_mac_monitor_vdev_create(ar);
			if (ret) {
				ath11k_warn(ar->ab, "failed to create monitor vdev: %d",
					    ret);
				goto out;
			}

			ret = ath11k_mac_monitor_start(ar);
			if (ret) {
				ath11k_warn(ar->ab, "failed to start monitor: %d",
					    ret);
				goto err_mon_del;
			}
		} else {
			clear_bit(ATH11K_FLAG_MONITOR_CONF_ENABLED, &ar->monitor_flags);

			if (!test_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED,
				      &ar->monitor_flags))
				goto out;

			ret = ath11k_mac_monitor_stop(ar);
			if (ret) {
				ath11k_warn(ar->ab, "failed to stop monitor: %d",
					    ret);
				goto out;
			}

			ret = ath11k_mac_monitor_vdev_delete(ar);
			if (ret) {
				ath11k_warn(ar->ab, "failed to delete monitor vdev: %d",
					    ret);
				goto out;
			}
		}
	}

out:
	mutex_unlock(&ar->conf_mutex);
	return ret;

err_mon_del:
	ath11k_mac_monitor_vdev_delete(ar);
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath11k_mac_setup_bcn_tmpl(struct ath11k_vif *arvif)
{
	struct ath11k *ar = arvif->ar;
	struct ath11k_base *ab = ar->ab;
	struct ieee80211_hw *hw = ar->hw;
	struct ieee80211_vif *vif = arvif->vif;
	struct ieee80211_mutable_offsets offs = {};
	struct sk_buff *bcn;
	struct ieee80211_mgmt *mgmt;
	u8 *ies;
	int ret;

	if (arvif->vdev_type != WMI_VDEV_TYPE_AP)
		return 0;

	bcn = ieee80211_beacon_get_template(hw, vif, &offs, 0);
	if (!bcn) {
		ath11k_warn(ab, "failed to get beacon template from mac80211\n");
		return -EPERM;
	}

	ies = bcn->data + ieee80211_get_hdrlen_from_skb(bcn);
	ies += sizeof(mgmt->u.beacon);

	if (cfg80211_find_ie(WLAN_EID_RSN, ies, (skb_tail_pointer(bcn) - ies)))
		arvif->rsnie_present = true;
	else
		arvif->rsnie_present = false;

	if (cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
				    WLAN_OUI_TYPE_MICROSOFT_WPA,
				    ies, (skb_tail_pointer(bcn) - ies)))
		arvif->wpaie_present = true;
	else
		arvif->wpaie_present = false;

	ret = ath11k_wmi_bcn_tmpl(ar, arvif->vdev_id, &offs, bcn);

	kfree_skb(bcn);

	if (ret)
		ath11k_warn(ab, "failed to submit beacon template command: %d\n",
			    ret);

	return ret;
}

void ath11k_mac_bcn_tx_event(struct ath11k_vif *arvif)
{
	struct ieee80211_vif *vif = arvif->vif;

	if (!vif->bss_conf.color_change_active && !arvif->bcca_zero_sent)
		return;

	if (vif->bss_conf.color_change_active &&
	    ieee80211_beacon_cntdwn_is_complete(vif)) {
		arvif->bcca_zero_sent = true;
		ieee80211_color_change_finish(vif);
		return;
	}

	arvif->bcca_zero_sent = false;

	if (vif->bss_conf.color_change_active)
		ieee80211_beacon_update_cntdwn(vif);
	ath11k_mac_setup_bcn_tmpl(arvif);
}

static void ath11k_control_beaconing(struct ath11k_vif *arvif,
				     struct ieee80211_bss_conf *info)
{
	struct ath11k *ar = arvif->ar;
	int ret = 0;

	lockdep_assert_held(&arvif->ar->conf_mutex);

	if (!info->enable_beacon) {
		ret = ath11k_wmi_vdev_down(ar, arvif->vdev_id);
		if (ret)
			ath11k_warn(ar->ab, "failed to down vdev_id %i: %d\n",
				    arvif->vdev_id, ret);

		arvif->is_up = false;
		return;
	}

	/* Install the beacon template to the FW */
	ret = ath11k_mac_setup_bcn_tmpl(arvif);
	if (ret) {
		ath11k_warn(ar->ab, "failed to update bcn tmpl during vdev up: %d\n",
			    ret);
		return;
	}

	arvif->tx_seq_no = 0x1000;

	arvif->aid = 0;

	ether_addr_copy(arvif->bssid, info->bssid);

	ret = ath11k_wmi_vdev_up(arvif->ar, arvif->vdev_id, arvif->aid,
				 arvif->bssid);
	if (ret) {
		ath11k_warn(ar->ab, "failed to bring up vdev %d: %i\n",
			    arvif->vdev_id, ret);
		return;
	}

	arvif->is_up = true;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac vdev %d up\n", arvif->vdev_id);
}

static void ath11k_mac_handle_beacon_iter(void *data, u8 *mac,
					  struct ieee80211_vif *vif)
{
	struct sk_buff *skb = data;
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	if (!ether_addr_equal(mgmt->bssid, vif->bss_conf.bssid))
		return;

	cancel_delayed_work(&arvif->connection_loss_work);
}

void ath11k_mac_handle_beacon(struct ath11k *ar, struct sk_buff *skb)
{
	ieee80211_iterate_active_interfaces_atomic(ar->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath11k_mac_handle_beacon_iter,
						   skb);
}

static void ath11k_mac_handle_beacon_miss_iter(void *data, u8 *mac,
					       struct ieee80211_vif *vif)
{
	u32 *vdev_id = data;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct ath11k *ar = arvif->ar;
	struct ieee80211_hw *hw = ar->hw;

	if (arvif->vdev_id != *vdev_id)
		return;

	if (!arvif->is_up)
		return;

	ieee80211_beacon_loss(vif);

	/* Firmware doesn't report beacon loss events repeatedly. If AP probe
	 * (done by mac80211) succeeds but beacons do not resume then it
	 * doesn't make sense to continue operation. Queue connection loss work
	 * which can be cancelled when beacon is received.
	 */
	ieee80211_queue_delayed_work(hw, &arvif->connection_loss_work,
				     ATH11K_CONNECTION_LOSS_HZ);
}

void ath11k_mac_handle_beacon_miss(struct ath11k *ar, u32 vdev_id)
{
	ieee80211_iterate_active_interfaces_atomic(ar->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath11k_mac_handle_beacon_miss_iter,
						   &vdev_id);
}

static void ath11k_mac_vif_sta_connection_loss_work(struct work_struct *work)
{
	struct ath11k_vif *arvif = container_of(work, struct ath11k_vif,
						connection_loss_work.work);
	struct ieee80211_vif *vif = arvif->vif;

	if (!arvif->is_up)
		return;

	ieee80211_connection_loss(vif);
}

static void ath11k_peer_assoc_h_basic(struct ath11k *ar,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      struct peer_assoc_params *arg)
{
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	u32 aid;

	lockdep_assert_held(&ar->conf_mutex);

	if (vif->type == NL80211_IFTYPE_STATION)
		aid = vif->cfg.aid;
	else
		aid = sta->aid;

	ether_addr_copy(arg->peer_mac, sta->addr);
	arg->vdev_id = arvif->vdev_id;
	arg->peer_associd = aid;
	arg->auth_flag = true;
	/* TODO: STA WAR in ath10k for listen interval required? */
	arg->peer_listen_intval = ar->hw->conf.listen_interval;
	arg->peer_nss = 1;
	arg->peer_caps = vif->bss_conf.assoc_capability;
}

static void ath11k_peer_assoc_h_crypto(struct ath11k *ar,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta,
				       struct peer_assoc_params *arg)
{
	struct ieee80211_bss_conf *info = &vif->bss_conf;
	struct cfg80211_chan_def def;
	struct cfg80211_bss *bss;
	struct ath11k_vif *arvif = (struct ath11k_vif *)vif->drv_priv;
	const u8 *rsnie = NULL;
	const u8 *wpaie = NULL;

	lockdep_assert_held(&ar->conf_mutex);

	if (WARN_ON(ath11k_mac_vif_chan(vif, &def)))
		return;

	bss = cfg80211_get_bss(ar->hw->wiphy, def.chan, info->bssid, NULL, 0,
			       IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);

	if (arvif->rsnie_present || arvif->wpaie_present) {
		arg->need_ptk_4_way = true;
		if (arvif->wpaie_present)
			arg->need_gtk_2_way = true;
	} else if (bss) {
		const struct cfg80211_bss_ies *ies;

		rcu_read_lock();
		rsnie = ieee80211_bss_get_ie(bss, WLAN_EID_RSN);

		ies = rcu_dereference(bss->ies);

		wpaie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
						WLAN_OUI_TYPE_MICROSOFT_WPA,
						ies->data,
						ies->len);
		rcu_read_unlock();
		cfg80211_put_bss(ar->hw->wiphy, bss);
	}

	/* FIXME: base on RSN IE/WPA IE is a correct idea? */
	if (rsnie || wpaie) {
		ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
			   "%s: rsn ie found\n", __func__);
		arg->need_ptk_4_way = true;
	}

	if (wpaie) {
		ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
			   "%s: wpa ie found\n", __func__);
		arg->need_gtk_2_way = true;
	}

	if (sta->mfp) {
		/* TODO: Need to check if FW supports PMF? */
		arg->is_pmf_enabled = true;
	}

	/* TODO: safe_mode_enabled (bypass 4-way handshake) flag req? */
}

static void ath11k_peer_assoc_h_rates(struct ath11k *ar,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      struct peer_assoc_params *arg)
{
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct wmi_rate_set_arg *rateset = &arg->peer_legacy_rates;
	struct cfg80211_chan_def def;
	const struct ieee80211_supported_band *sband;
	const struct ieee80211_rate *rates;
	enum nl80211_band band;
	u32 ratemask;
	u8 rate;
	int i;

	lockdep_assert_held(&ar->conf_mutex);

	if (WARN_ON(ath11k_mac_vif_chan(vif, &def)))
		return;

	band = def.chan->band;
	sband = ar->hw->wiphy->bands[band];
	ratemask = sta->deflink.supp_rates[band];
	ratemask &= arvif->bitrate_mask.control[band].legacy;
	rates = sband->bitrates;

	rateset->num_rates = 0;

	for (i = 0; i < 32; i++, ratemask >>= 1, rates++) {
		if (!(ratemask & 1))
			continue;

		rate = ath11k_mac_bitrate_to_rate(rates->bitrate);
		rateset->rates[rateset->num_rates] = rate;
		rateset->num_rates++;
	}
}

static bool
ath11k_peer_assoc_h_ht_masked(const u8 ht_mcs_mask[IEEE80211_HT_MCS_MASK_LEN])
{
	int nss;

	for (nss = 0; nss < IEEE80211_HT_MCS_MASK_LEN; nss++)
		if (ht_mcs_mask[nss])
			return false;

	return true;
}

static bool
ath11k_peer_assoc_h_vht_masked(const u16 vht_mcs_mask[])
{
	int nss;

	for (nss = 0; nss < NL80211_VHT_NSS_MAX; nss++)
		if (vht_mcs_mask[nss])
			return false;

	return true;
}

static void ath11k_peer_assoc_h_ht(struct ath11k *ar,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   struct peer_assoc_params *arg)
{
	const struct ieee80211_sta_ht_cap *ht_cap = &sta->deflink.ht_cap;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	const u8 *ht_mcs_mask;
	int i, n;
	u8 max_nss;
	u32 stbc;

	lockdep_assert_held(&ar->conf_mutex);

	if (WARN_ON(ath11k_mac_vif_chan(vif, &def)))
		return;

	if (!ht_cap->ht_supported)
		return;

	band = def.chan->band;
	ht_mcs_mask = arvif->bitrate_mask.control[band].ht_mcs;

	if (ath11k_peer_assoc_h_ht_masked(ht_mcs_mask))
		return;

	arg->ht_flag = true;

	arg->peer_max_mpdu = (1 << (IEEE80211_HT_MAX_AMPDU_FACTOR +
				    ht_cap->ampdu_factor)) - 1;

	arg->peer_mpdu_density =
		ath11k_parse_mpdudensity(ht_cap->ampdu_density);

	arg->peer_ht_caps = ht_cap->cap;
	arg->peer_rate_caps |= WMI_HOST_RC_HT_FLAG;

	if (ht_cap->cap & IEEE80211_HT_CAP_LDPC_CODING)
		arg->ldpc_flag = true;

	if (sta->deflink.bandwidth >= IEEE80211_STA_RX_BW_40) {
		arg->bw_40 = true;
		arg->peer_rate_caps |= WMI_HOST_RC_CW40_FLAG;
	}

	/* As firmware handles this two flags (IEEE80211_HT_CAP_SGI_20
	 * and IEEE80211_HT_CAP_SGI_40) for enabling SGI, we reset
	 * both flags if guard interval is Default GI
	 */
	if (arvif->bitrate_mask.control[band].gi == NL80211_TXRATE_DEFAULT_GI)
		arg->peer_ht_caps &= ~(IEEE80211_HT_CAP_SGI_20 |
				IEEE80211_HT_CAP_SGI_40);

	if (arvif->bitrate_mask.control[band].gi != NL80211_TXRATE_FORCE_LGI) {
		if (ht_cap->cap & (IEEE80211_HT_CAP_SGI_20 |
		    IEEE80211_HT_CAP_SGI_40))
			arg->peer_rate_caps |= WMI_HOST_RC_SGI_FLAG;
	}

	if (ht_cap->cap & IEEE80211_HT_CAP_TX_STBC) {
		arg->peer_rate_caps |= WMI_HOST_RC_TX_STBC_FLAG;
		arg->stbc_flag = true;
	}

	if (ht_cap->cap & IEEE80211_HT_CAP_RX_STBC) {
		stbc = ht_cap->cap & IEEE80211_HT_CAP_RX_STBC;
		stbc = stbc >> IEEE80211_HT_CAP_RX_STBC_SHIFT;
		stbc = stbc << WMI_HOST_RC_RX_STBC_FLAG_S;
		arg->peer_rate_caps |= stbc;
		arg->stbc_flag = true;
	}

	if (ht_cap->mcs.rx_mask[1] && ht_cap->mcs.rx_mask[2])
		arg->peer_rate_caps |= WMI_HOST_RC_TS_FLAG;
	else if (ht_cap->mcs.rx_mask[1])
		arg->peer_rate_caps |= WMI_HOST_RC_DS_FLAG;

	for (i = 0, n = 0, max_nss = 0; i < IEEE80211_HT_MCS_MASK_LEN * 8; i++)
		if ((ht_cap->mcs.rx_mask[i / 8] & BIT(i % 8)) &&
		    (ht_mcs_mask[i / 8] & BIT(i % 8))) {
			max_nss = (i / 8) + 1;
			arg->peer_ht_rates.rates[n++] = i;
		}

	/* This is a workaround for HT-enabled STAs which break the spec
	 * and have no HT capabilities RX mask (no HT RX MCS map).
	 *
	 * As per spec, in section 20.3.5 Modulation and coding scheme (MCS),
	 * MCS 0 through 7 are mandatory in 20MHz with 800 ns GI at all STAs.
	 *
	 * Firmware asserts if such situation occurs.
	 */
	if (n == 0) {
		arg->peer_ht_rates.num_rates = 8;
		for (i = 0; i < arg->peer_ht_rates.num_rates; i++)
			arg->peer_ht_rates.rates[i] = i;
	} else {
		arg->peer_ht_rates.num_rates = n;
		arg->peer_nss = min(sta->deflink.rx_nss, max_nss);
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac ht peer %pM mcs cnt %d nss %d\n",
		   arg->peer_mac,
		   arg->peer_ht_rates.num_rates,
		   arg->peer_nss);
}

static int ath11k_mac_get_max_vht_mcs_map(u16 mcs_map, int nss)
{
	switch ((mcs_map >> (2 * nss)) & 0x3) {
	case IEEE80211_VHT_MCS_SUPPORT_0_7: return BIT(8) - 1;
	case IEEE80211_VHT_MCS_SUPPORT_0_8: return BIT(9) - 1;
	case IEEE80211_VHT_MCS_SUPPORT_0_9: return BIT(10) - 1;
	}
	return 0;
}

static u16
ath11k_peer_assoc_h_vht_limit(u16 tx_mcs_set,
			      const u16 vht_mcs_limit[NL80211_VHT_NSS_MAX])
{
	int idx_limit;
	int nss;
	u16 mcs_map;
	u16 mcs;

	for (nss = 0; nss < NL80211_VHT_NSS_MAX; nss++) {
		mcs_map = ath11k_mac_get_max_vht_mcs_map(tx_mcs_set, nss) &
			  vht_mcs_limit[nss];

		if (mcs_map)
			idx_limit = fls(mcs_map) - 1;
		else
			idx_limit = -1;

		switch (idx_limit) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			mcs = IEEE80211_VHT_MCS_SUPPORT_0_7;
			break;
		case 8:
			mcs = IEEE80211_VHT_MCS_SUPPORT_0_8;
			break;
		case 9:
			mcs = IEEE80211_VHT_MCS_SUPPORT_0_9;
			break;
		default:
			WARN_ON(1);
			fallthrough;
		case -1:
			mcs = IEEE80211_VHT_MCS_NOT_SUPPORTED;
			break;
		}

		tx_mcs_set &= ~(0x3 << (nss * 2));
		tx_mcs_set |= mcs << (nss * 2);
	}

	return tx_mcs_set;
}

static u8 ath11k_get_nss_160mhz(struct ath11k *ar,
				u8 max_nss)
{
	u8 nss_ratio_info = ar->pdev->cap.nss_ratio_info;
	u8 max_sup_nss = 0;

	switch (nss_ratio_info) {
	case WMI_NSS_RATIO_1BY2_NSS:
		max_sup_nss = max_nss >> 1;
		break;
	case WMI_NSS_RATIO_3BY4_NSS:
		ath11k_warn(ar->ab, "WMI_NSS_RATIO_3BY4_NSS not supported\n");
		break;
	case WMI_NSS_RATIO_1_NSS:
		max_sup_nss = max_nss;
		break;
	case WMI_NSS_RATIO_2_NSS:
		ath11k_warn(ar->ab, "WMI_NSS_RATIO_2_NSS not supported\n");
		break;
	default:
		ath11k_warn(ar->ab, "invalid nss ratio received from firmware: %d\n",
			    nss_ratio_info);
		break;
	}

	return max_sup_nss;
}

static void ath11k_peer_assoc_h_vht(struct ath11k *ar,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    struct peer_assoc_params *arg)
{
	const struct ieee80211_sta_vht_cap *vht_cap = &sta->deflink.vht_cap;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	u16 *vht_mcs_mask;
	u8 ampdu_factor;
	u8 max_nss, vht_mcs;
	int i, vht_nss, nss_idx;
	bool user_rate_valid = true;
	u32 rx_nss, tx_nss, nss_160;

	if (WARN_ON(ath11k_mac_vif_chan(vif, &def)))
		return;

	if (!vht_cap->vht_supported)
		return;

	band = def.chan->band;
	vht_mcs_mask = arvif->bitrate_mask.control[band].vht_mcs;

	if (ath11k_peer_assoc_h_vht_masked(vht_mcs_mask))
		return;

	arg->vht_flag = true;

	/* TODO: similar flags required? */
	arg->vht_capable = true;

	if (def.chan->band == NL80211_BAND_2GHZ)
		arg->vht_ng_flag = true;

	arg->peer_vht_caps = vht_cap->cap;

	ampdu_factor = (vht_cap->cap &
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK) >>
		       IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;

	/* Workaround: Some Netgear/Linksys 11ac APs set Rx A-MPDU factor to
	 * zero in VHT IE. Using it would result in degraded throughput.
	 * arg->peer_max_mpdu at this point contains HT max_mpdu so keep
	 * it if VHT max_mpdu is smaller.
	 */
	arg->peer_max_mpdu = max(arg->peer_max_mpdu,
				 (1U << (IEEE80211_HT_MAX_AMPDU_FACTOR +
					ampdu_factor)) - 1);

	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_80)
		arg->bw_80 = true;

	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_160)
		arg->bw_160 = true;

	vht_nss =  ath11k_mac_max_vht_nss(vht_mcs_mask);

	if (vht_nss > sta->deflink.rx_nss) {
		user_rate_valid = false;
		for (nss_idx = sta->deflink.rx_nss - 1; nss_idx >= 0; nss_idx--) {
			if (vht_mcs_mask[nss_idx]) {
				user_rate_valid = true;
				break;
			}
		}
	}

	if (!user_rate_valid) {
		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac setting vht range mcs value to peer supported nss %d for peer %pM\n",
			   sta->deflink.rx_nss, sta->addr);
		vht_mcs_mask[sta->deflink.rx_nss - 1] = vht_mcs_mask[vht_nss - 1];
	}

	/* Calculate peer NSS capability from VHT capabilities if STA
	 * supports VHT.
	 */
	for (i = 0, max_nss = 0; i < NL80211_VHT_NSS_MAX; i++) {
		vht_mcs = __le16_to_cpu(vht_cap->vht_mcs.rx_mcs_map) >>
			  (2 * i) & 3;

		if (vht_mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED &&
		    vht_mcs_mask[i])
			max_nss = i + 1;
	}
	arg->peer_nss = min(sta->deflink.rx_nss, max_nss);
	arg->rx_max_rate = __le16_to_cpu(vht_cap->vht_mcs.rx_highest);
	arg->rx_mcs_set = __le16_to_cpu(vht_cap->vht_mcs.rx_mcs_map);
	arg->tx_max_rate = __le16_to_cpu(vht_cap->vht_mcs.tx_highest);
	arg->tx_mcs_set = ath11k_peer_assoc_h_vht_limit(
		__le16_to_cpu(vht_cap->vht_mcs.tx_mcs_map), vht_mcs_mask);

	/* In IPQ8074 platform, VHT mcs rate 10 and 11 is enabled by default.
	 * VHT mcs rate 10 and 11 is not suppoerted in 11ac standard.
	 * so explicitly disable the VHT MCS rate 10 and 11 in 11ac mode.
	 */
	arg->tx_mcs_set &= ~IEEE80211_VHT_MCS_SUPPORT_0_11_MASK;
	arg->tx_mcs_set |= IEEE80211_DISABLE_VHT_MCS_SUPPORT_0_11;

	if ((arg->tx_mcs_set & IEEE80211_VHT_MCS_NOT_SUPPORTED) ==
			IEEE80211_VHT_MCS_NOT_SUPPORTED)
		arg->peer_vht_caps &= ~IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;

	/* TODO:  Check */
	arg->tx_max_mcs_nss = 0xFF;

	if (arg->peer_phymode == MODE_11AC_VHT160 ||
	    arg->peer_phymode == MODE_11AC_VHT80_80) {
		tx_nss = ath11k_get_nss_160mhz(ar, max_nss);
		rx_nss = min(arg->peer_nss, tx_nss);
		arg->peer_bw_rxnss_override = ATH11K_BW_NSS_MAP_ENABLE;

		if (!rx_nss) {
			ath11k_warn(ar->ab, "invalid max_nss\n");
			return;
		}

		if (arg->peer_phymode == MODE_11AC_VHT160)
			nss_160 = FIELD_PREP(ATH11K_PEER_RX_NSS_160MHZ, rx_nss - 1);
		else
			nss_160 = FIELD_PREP(ATH11K_PEER_RX_NSS_80_80MHZ, rx_nss - 1);

		arg->peer_bw_rxnss_override |= nss_160;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "mac vht peer %pM max_mpdu %d flags 0x%x nss_override 0x%x\n",
		   sta->addr, arg->peer_max_mpdu, arg->peer_flags,
		   arg->peer_bw_rxnss_override);
}

static int ath11k_mac_get_max_he_mcs_map(u16 mcs_map, int nss)
{
	switch ((mcs_map >> (2 * nss)) & 0x3) {
	case IEEE80211_HE_MCS_SUPPORT_0_7: return BIT(8) - 1;
	case IEEE80211_HE_MCS_SUPPORT_0_9: return BIT(10) - 1;
	case IEEE80211_HE_MCS_SUPPORT_0_11: return BIT(12) - 1;
	}
	return 0;
}

static u16 ath11k_peer_assoc_h_he_limit(u16 tx_mcs_set,
					const u16 he_mcs_limit[NL80211_HE_NSS_MAX])
{
	int idx_limit;
	int nss;
	u16 mcs_map;
	u16 mcs;

	for (nss = 0; nss < NL80211_HE_NSS_MAX; nss++) {
		mcs_map = ath11k_mac_get_max_he_mcs_map(tx_mcs_set, nss) &
			he_mcs_limit[nss];

		if (mcs_map)
			idx_limit = fls(mcs_map) - 1;
		else
			idx_limit = -1;

		switch (idx_limit) {
		case 0 ... 7:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_7;
			break;
		case 8:
		case 9:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_9;
			break;
		case 10:
		case 11:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_11;
			break;
		default:
			WARN_ON(1);
			fallthrough;
		case -1:
			mcs = IEEE80211_HE_MCS_NOT_SUPPORTED;
			break;
		}

		tx_mcs_set &= ~(0x3 << (nss * 2));
		tx_mcs_set |= mcs << (nss * 2);
	}

	return tx_mcs_set;
}

static bool
ath11k_peer_assoc_h_he_masked(const u16 he_mcs_mask[NL80211_HE_NSS_MAX])
{
	int nss;

	for (nss = 0; nss < NL80211_HE_NSS_MAX; nss++)
		if (he_mcs_mask[nss])
			return false;

	return true;
}

static void ath11k_peer_assoc_h_he(struct ath11k *ar,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   struct peer_assoc_params *arg)
{
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct cfg80211_chan_def def;
	const struct ieee80211_sta_he_cap *he_cap = &sta->deflink.he_cap;
	enum nl80211_band band;
	u16 he_mcs_mask[NL80211_HE_NSS_MAX];
	u8 max_nss, he_mcs;
	u16 he_tx_mcs = 0, v = 0;
	int i, he_nss, nss_idx;
	bool user_rate_valid = true;
	u32 rx_nss, tx_nss, nss_160;
	u8 ampdu_factor, rx_mcs_80, rx_mcs_160;
	u16 mcs_160_map, mcs_80_map;
	bool support_160;

	if (WARN_ON(ath11k_mac_vif_chan(vif, &def)))
		return;

	if (!he_cap->has_he)
		return;

	band = def.chan->band;
	memcpy(he_mcs_mask, arvif->bitrate_mask.control[band].he_mcs,
	       sizeof(he_mcs_mask));

	if (ath11k_peer_assoc_h_he_masked(he_mcs_mask))
		return;

	arg->he_flag = true;
	support_160 = !!(he_cap->he_cap_elem.phy_cap_info[0] &
		  IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G);

	/* Supported HE-MCS and NSS Set of peer he_cap is intersection with self he_cp */
	mcs_160_map = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_160);
	mcs_80_map = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_80);

	if (support_160) {
		for (i = 7; i >= 0; i--) {
			u8 mcs_160 = (mcs_160_map >> (2 * i)) & 3;

			if (mcs_160 != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
				rx_mcs_160 = i + 1;
				break;
			}
		}
	}

	for (i = 7; i >= 0; i--) {
		u8 mcs_80 = (mcs_80_map >> (2 * i)) & 3;

		if (mcs_80 != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
			rx_mcs_80 = i + 1;
			break;
		}
	}

	if (support_160)
		max_nss = min(rx_mcs_80, rx_mcs_160);
	else
		max_nss = rx_mcs_80;

	arg->peer_nss = min(sta->deflink.rx_nss, max_nss);

	memcpy_and_pad(&arg->peer_he_cap_macinfo,
		       sizeof(arg->peer_he_cap_macinfo),
		       he_cap->he_cap_elem.mac_cap_info,
		       sizeof(he_cap->he_cap_elem.mac_cap_info),
		       0);
	memcpy_and_pad(&arg->peer_he_cap_phyinfo,
		       sizeof(arg->peer_he_cap_phyinfo),
		       he_cap->he_cap_elem.phy_cap_info,
		       sizeof(he_cap->he_cap_elem.phy_cap_info),
		       0);
	arg->peer_he_ops = vif->bss_conf.he_oper.params;

	/* the top most byte is used to indicate BSS color info */
	arg->peer_he_ops &= 0xffffff;

	/* As per section 26.6.1 11ax Draft5.0, if the Max AMPDU Exponent Extension
	 * in HE cap is zero, use the arg->peer_max_mpdu as calculated while parsing
	 * VHT caps(if VHT caps is present) or HT caps (if VHT caps is not present).
	 *
	 * For non-zero value of Max AMPDU Extponent Extension in HE MAC caps,
	 * if a HE STA sends VHT cap and HE cap IE in assoc request then, use
	 * MAX_AMPDU_LEN_FACTOR as 20 to calculate max_ampdu length.
	 * If a HE STA that does not send VHT cap, but HE and HT cap in assoc
	 * request, then use MAX_AMPDU_LEN_FACTOR as 16 to calculate max_ampdu
	 * length.
	 */
	ampdu_factor = u8_get_bits(he_cap->he_cap_elem.mac_cap_info[3],
				   IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK);

	if (ampdu_factor) {
		if (sta->deflink.vht_cap.vht_supported)
			arg->peer_max_mpdu = (1 << (IEEE80211_HE_VHT_MAX_AMPDU_FACTOR +
						    ampdu_factor)) - 1;
		else if (sta->deflink.ht_cap.ht_supported)
			arg->peer_max_mpdu = (1 << (IEEE80211_HE_HT_MAX_AMPDU_FACTOR +
						    ampdu_factor)) - 1;
	}

	if (he_cap->he_cap_elem.phy_cap_info[6] &
	    IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
		int bit = 7;
		int nss, ru;

		arg->peer_ppet.numss_m1 = he_cap->ppe_thres[0] &
					  IEEE80211_PPE_THRES_NSS_MASK;
		arg->peer_ppet.ru_bit_mask =
			(he_cap->ppe_thres[0] &
			 IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK) >>
			IEEE80211_PPE_THRES_RU_INDEX_BITMASK_POS;

		for (nss = 0; nss <= arg->peer_ppet.numss_m1; nss++) {
			for (ru = 0; ru < 4; ru++) {
				u32 val = 0;
				int i;

				if ((arg->peer_ppet.ru_bit_mask & BIT(ru)) == 0)
					continue;
				for (i = 0; i < 6; i++) {
					val >>= 1;
					val |= ((he_cap->ppe_thres[bit / 8] >>
						 (bit % 8)) & 0x1) << 5;
					bit++;
				}
				arg->peer_ppet.ppet16_ppet8_ru3_ru0[nss] |=
								val << (ru * 6);
			}
		}
	}

	if (he_cap->he_cap_elem.mac_cap_info[0] & IEEE80211_HE_MAC_CAP0_TWT_RES)
		arg->twt_responder = true;
	if (he_cap->he_cap_elem.mac_cap_info[0] & IEEE80211_HE_MAC_CAP0_TWT_REQ)
		arg->twt_requester = true;

	he_nss =  ath11k_mac_max_he_nss(he_mcs_mask);

	if (he_nss > sta->deflink.rx_nss) {
		user_rate_valid = false;
		for (nss_idx = sta->deflink.rx_nss - 1; nss_idx >= 0; nss_idx--) {
			if (he_mcs_mask[nss_idx]) {
				user_rate_valid = true;
				break;
			}
		}
	}

	if (!user_rate_valid) {
		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac setting he range mcs value to peer supported nss %d for peer %pM\n",
			   sta->deflink.rx_nss, sta->addr);
		he_mcs_mask[sta->deflink.rx_nss - 1] = he_mcs_mask[he_nss - 1];
	}

	switch (sta->deflink.bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		if (he_cap->he_cap_elem.phy_cap_info[0] &
		    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) {
			v = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_80p80);
			v = ath11k_peer_assoc_h_he_limit(v, he_mcs_mask);
			arg->peer_he_rx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_80_80] = v;

			v = le16_to_cpu(he_cap->he_mcs_nss_supp.tx_mcs_80p80);
			arg->peer_he_tx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_80_80] = v;

			arg->peer_he_mcs_count++;
			he_tx_mcs = v;
		}
		v = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_160);
		arg->peer_he_rx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_160] = v;

		v = le16_to_cpu(he_cap->he_mcs_nss_supp.tx_mcs_160);
		v = ath11k_peer_assoc_h_he_limit(v, he_mcs_mask);
		arg->peer_he_tx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_160] = v;

		arg->peer_he_mcs_count++;
		if (!he_tx_mcs)
			he_tx_mcs = v;
		fallthrough;

	default:
		v = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_80);
		arg->peer_he_rx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_80] = v;

		v = le16_to_cpu(he_cap->he_mcs_nss_supp.tx_mcs_80);
		v = ath11k_peer_assoc_h_he_limit(v, he_mcs_mask);
		arg->peer_he_tx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_80] = v;

		arg->peer_he_mcs_count++;
		if (!he_tx_mcs)
			he_tx_mcs = v;
		break;
	}

	/* Calculate peer NSS capability from HE capabilities if STA
	 * supports HE.
	 */
	for (i = 0, max_nss = 0; i < NL80211_HE_NSS_MAX; i++) {
		he_mcs = he_tx_mcs >> (2 * i) & 3;

		/* In case of fixed rates, MCS Range in he_tx_mcs might have
		 * unsupported range, with he_mcs_mask set, so check either of them
		 * to find nss.
		 */
		if (he_mcs != IEEE80211_HE_MCS_NOT_SUPPORTED ||
		    he_mcs_mask[i])
			max_nss = i + 1;
	}
	arg->peer_nss = min(sta->deflink.rx_nss, max_nss);

	if (arg->peer_phymode == MODE_11AX_HE160 ||
	    arg->peer_phymode == MODE_11AX_HE80_80) {
		tx_nss = ath11k_get_nss_160mhz(ar, max_nss);
		rx_nss = min(arg->peer_nss, tx_nss);
		arg->peer_bw_rxnss_override = ATH11K_BW_NSS_MAP_ENABLE;

		if (!rx_nss) {
			ath11k_warn(ar->ab, "invalid max_nss\n");
			return;
		}

		if (arg->peer_phymode == MODE_11AX_HE160)
			nss_160 = FIELD_PREP(ATH11K_PEER_RX_NSS_160MHZ, rx_nss - 1);
		else
			nss_160 = FIELD_PREP(ATH11K_PEER_RX_NSS_80_80MHZ, rx_nss - 1);

		arg->peer_bw_rxnss_override |= nss_160;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "mac he peer %pM nss %d mcs cnt %d nss_override 0x%x\n",
		   sta->addr, arg->peer_nss,
		   arg->peer_he_mcs_count,
		   arg->peer_bw_rxnss_override);
}

static void ath11k_peer_assoc_h_he_6ghz(struct ath11k *ar,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta,
					struct peer_assoc_params *arg)
{
	const struct ieee80211_sta_he_cap *he_cap = &sta->deflink.he_cap;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	u8  ampdu_factor;

	if (WARN_ON(ath11k_mac_vif_chan(vif, &def)))
		return;

	band = def.chan->band;

	if (!arg->he_flag || band != NL80211_BAND_6GHZ || !sta->deflink.he_6ghz_capa.capa)
		return;

	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_40)
		arg->bw_40 = true;

	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_80)
		arg->bw_80 = true;

	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_160)
		arg->bw_160 = true;

	arg->peer_he_caps_6ghz = le16_to_cpu(sta->deflink.he_6ghz_capa.capa);
	arg->peer_mpdu_density =
		ath11k_parse_mpdudensity(FIELD_GET(IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START,
						   arg->peer_he_caps_6ghz));

	/* From IEEE Std 802.11ax-2021 - Section 10.12.2: An HE STA shall be capable of
	 * receiving A-MPDU where the A-MPDU pre-EOF padding length is up to the value
	 * indicated by the Maximum A-MPDU Length Exponent Extension field in the HE
	 * Capabilities element and the Maximum A-MPDU Length Exponent field in HE 6 GHz
	 * Band Capabilities element in the 6 GHz band.
	 *
	 * Here, we are extracting the Max A-MPDU Exponent Extension from HE caps and
	 * factor is the Maximum A-MPDU Length Exponent from HE 6 GHZ Band capability.
	 */
	ampdu_factor = FIELD_GET(IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK,
				 he_cap->he_cap_elem.mac_cap_info[3]) +
			FIELD_GET(IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP,
				  arg->peer_he_caps_6ghz);

	arg->peer_max_mpdu = (1u << (IEEE80211_HE_6GHZ_MAX_AMPDU_FACTOR +
				     ampdu_factor)) - 1;
}

static void ath11k_peer_assoc_h_smps(struct ieee80211_sta *sta,
				     struct peer_assoc_params *arg)
{
	const struct ieee80211_sta_ht_cap *ht_cap = &sta->deflink.ht_cap;
	int smps;

	if (!ht_cap->ht_supported && !sta->deflink.he_6ghz_capa.capa)
		return;

	if (ht_cap->ht_supported) {
		smps = ht_cap->cap & IEEE80211_HT_CAP_SM_PS;
		smps >>= IEEE80211_HT_CAP_SM_PS_SHIFT;
	} else {
		smps = le16_get_bits(sta->deflink.he_6ghz_capa.capa,
				     IEEE80211_HE_6GHZ_CAP_SM_PS);
	}

	switch (smps) {
	case WLAN_HT_CAP_SM_PS_STATIC:
		arg->static_mimops_flag = true;
		break;
	case WLAN_HT_CAP_SM_PS_DYNAMIC:
		arg->dynamic_mimops_flag = true;
		break;
	case WLAN_HT_CAP_SM_PS_DISABLED:
		arg->spatial_mux_flag = true;
		break;
	default:
		break;
	}
}

static void ath11k_peer_assoc_h_qos(struct ath11k *ar,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    struct peer_assoc_params *arg)
{
	struct ath11k_vif *arvif = (void *)vif->drv_priv;

	switch (arvif->vdev_type) {
	case WMI_VDEV_TYPE_AP:
		if (sta->wme) {
			/* TODO: Check WME vs QoS */
			arg->is_wme_set = true;
			arg->qos_flag = true;
		}

		if (sta->wme && sta->uapsd_queues) {
			/* TODO: Check WME vs QoS */
			arg->is_wme_set = true;
			arg->apsd_flag = true;
			arg->peer_rate_caps |= WMI_HOST_RC_UAPSD_FLAG;
		}
		break;
	case WMI_VDEV_TYPE_STA:
		if (sta->wme) {
			arg->is_wme_set = true;
			arg->qos_flag = true;
		}
		break;
	default:
		break;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac peer %pM qos %d\n",
		   sta->addr, arg->qos_flag);
}

static int ath11k_peer_assoc_qos_ap(struct ath11k *ar,
				    struct ath11k_vif *arvif,
				    struct ieee80211_sta *sta)
{
	struct ap_ps_params params;
	u32 max_sp;
	u32 uapsd;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	params.vdev_id = arvif->vdev_id;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac uapsd_queues 0x%x max_sp %d\n",
		   sta->uapsd_queues, sta->max_sp);

	uapsd = 0;
	if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VO)
		uapsd |= WMI_AP_PS_UAPSD_AC3_DELIVERY_EN |
			 WMI_AP_PS_UAPSD_AC3_TRIGGER_EN;
	if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VI)
		uapsd |= WMI_AP_PS_UAPSD_AC2_DELIVERY_EN |
			 WMI_AP_PS_UAPSD_AC2_TRIGGER_EN;
	if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BK)
		uapsd |= WMI_AP_PS_UAPSD_AC1_DELIVERY_EN |
			 WMI_AP_PS_UAPSD_AC1_TRIGGER_EN;
	if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BE)
		uapsd |= WMI_AP_PS_UAPSD_AC0_DELIVERY_EN |
			 WMI_AP_PS_UAPSD_AC0_TRIGGER_EN;

	max_sp = 0;
	if (sta->max_sp < MAX_WMI_AP_PS_PEER_PARAM_MAX_SP)
		max_sp = sta->max_sp;

	params.param = WMI_AP_PS_PEER_PARAM_UAPSD;
	params.value = uapsd;
	ret = ath11k_wmi_send_set_ap_ps_param_cmd(ar, sta->addr, &params);
	if (ret)
		goto err;

	params.param = WMI_AP_PS_PEER_PARAM_MAX_SP;
	params.value = max_sp;
	ret = ath11k_wmi_send_set_ap_ps_param_cmd(ar, sta->addr, &params);
	if (ret)
		goto err;

	/* TODO revisit during testing */
	params.param = WMI_AP_PS_PEER_PARAM_SIFS_RESP_FRMTYPE;
	params.value = DISABLE_SIFS_RESPONSE_TRIGGER;
	ret = ath11k_wmi_send_set_ap_ps_param_cmd(ar, sta->addr, &params);
	if (ret)
		goto err;

	params.param = WMI_AP_PS_PEER_PARAM_SIFS_RESP_UAPSD;
	params.value = DISABLE_SIFS_RESPONSE_TRIGGER;
	ret = ath11k_wmi_send_set_ap_ps_param_cmd(ar, sta->addr, &params);
	if (ret)
		goto err;

	return 0;

err:
	ath11k_warn(ar->ab, "failed to set ap ps peer param %d for vdev %i: %d\n",
		    params.param, arvif->vdev_id, ret);
	return ret;
}

static bool ath11k_mac_sta_has_ofdm_only(struct ieee80211_sta *sta)
{
	return sta->deflink.supp_rates[NL80211_BAND_2GHZ] >>
	       ATH11K_MAC_FIRST_OFDM_RATE_IDX;
}

static enum wmi_phy_mode ath11k_mac_get_phymode_vht(struct ath11k *ar,
						    struct ieee80211_sta *sta)
{
	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_160) {
		switch (sta->deflink.vht_cap.cap &
			IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) {
		case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ:
			return MODE_11AC_VHT160;
		case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ:
			return MODE_11AC_VHT80_80;
		default:
			/* not sure if this is a valid case? */
			return MODE_11AC_VHT160;
		}
	}

	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_80)
		return MODE_11AC_VHT80;

	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_40)
		return MODE_11AC_VHT40;

	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_20)
		return MODE_11AC_VHT20;

	return MODE_UNKNOWN;
}

static enum wmi_phy_mode ath11k_mac_get_phymode_he(struct ath11k *ar,
						   struct ieee80211_sta *sta)
{
	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_160) {
		if (sta->deflink.he_cap.he_cap_elem.phy_cap_info[0] &
		     IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G)
			return MODE_11AX_HE160;
		else if (sta->deflink.he_cap.he_cap_elem.phy_cap_info[0] &
			 IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
			return MODE_11AX_HE80_80;
		/* not sure if this is a valid case? */
		return MODE_11AX_HE160;
	}

	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_80)
		return MODE_11AX_HE80;

	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_40)
		return MODE_11AX_HE40;

	if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_20)
		return MODE_11AX_HE20;

	return MODE_UNKNOWN;
}

static void ath11k_peer_assoc_h_phymode(struct ath11k *ar,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta,
					struct peer_assoc_params *arg)
{
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	const u8 *ht_mcs_mask;
	const u16 *vht_mcs_mask;
	const u16 *he_mcs_mask;
	enum wmi_phy_mode phymode = MODE_UNKNOWN;

	if (WARN_ON(ath11k_mac_vif_chan(vif, &def)))
		return;

	band = def.chan->band;
	ht_mcs_mask = arvif->bitrate_mask.control[band].ht_mcs;
	vht_mcs_mask = arvif->bitrate_mask.control[band].vht_mcs;
	he_mcs_mask = arvif->bitrate_mask.control[band].he_mcs;

	switch (band) {
	case NL80211_BAND_2GHZ:
		if (sta->deflink.he_cap.has_he &&
		    !ath11k_peer_assoc_h_he_masked(he_mcs_mask)) {
			if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_80)
				phymode = MODE_11AX_HE80_2G;
			else if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_40)
				phymode = MODE_11AX_HE40_2G;
			else
				phymode = MODE_11AX_HE20_2G;
		} else if (sta->deflink.vht_cap.vht_supported &&
			   !ath11k_peer_assoc_h_vht_masked(vht_mcs_mask)) {
			if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_40)
				phymode = MODE_11AC_VHT40;
			else
				phymode = MODE_11AC_VHT20;
		} else if (sta->deflink.ht_cap.ht_supported &&
			   !ath11k_peer_assoc_h_ht_masked(ht_mcs_mask)) {
			if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_40)
				phymode = MODE_11NG_HT40;
			else
				phymode = MODE_11NG_HT20;
		} else if (ath11k_mac_sta_has_ofdm_only(sta)) {
			phymode = MODE_11G;
		} else {
			phymode = MODE_11B;
		}
		break;
	case NL80211_BAND_5GHZ:
	case NL80211_BAND_6GHZ:
		/* Check HE first */
		if (sta->deflink.he_cap.has_he &&
		    !ath11k_peer_assoc_h_he_masked(he_mcs_mask)) {
			phymode = ath11k_mac_get_phymode_he(ar, sta);
		} else if (sta->deflink.vht_cap.vht_supported &&
			   !ath11k_peer_assoc_h_vht_masked(vht_mcs_mask)) {
			phymode = ath11k_mac_get_phymode_vht(ar, sta);
		} else if (sta->deflink.ht_cap.ht_supported &&
			   !ath11k_peer_assoc_h_ht_masked(ht_mcs_mask)) {
			if (sta->deflink.bandwidth >= IEEE80211_STA_RX_BW_40)
				phymode = MODE_11NA_HT40;
			else
				phymode = MODE_11NA_HT20;
		} else {
			phymode = MODE_11A;
		}
		break;
	default:
		break;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac peer %pM phymode %s\n",
		   sta->addr, ath11k_wmi_phymode_str(phymode));

	arg->peer_phymode = phymode;
	WARN_ON(phymode == MODE_UNKNOWN);
}

static void ath11k_peer_assoc_prepare(struct ath11k *ar,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      struct peer_assoc_params *arg,
				      bool reassoc)
{
	struct ath11k_sta *arsta;

	lockdep_assert_held(&ar->conf_mutex);

	arsta = (struct ath11k_sta *)sta->drv_priv;

	memset(arg, 0, sizeof(*arg));

	reinit_completion(&ar->peer_assoc_done);

	arg->peer_new_assoc = !reassoc;
	ath11k_peer_assoc_h_basic(ar, vif, sta, arg);
	ath11k_peer_assoc_h_crypto(ar, vif, sta, arg);
	ath11k_peer_assoc_h_rates(ar, vif, sta, arg);
	ath11k_peer_assoc_h_phymode(ar, vif, sta, arg);
	ath11k_peer_assoc_h_ht(ar, vif, sta, arg);
	ath11k_peer_assoc_h_vht(ar, vif, sta, arg);
	ath11k_peer_assoc_h_he(ar, vif, sta, arg);
	ath11k_peer_assoc_h_he_6ghz(ar, vif, sta, arg);
	ath11k_peer_assoc_h_qos(ar, vif, sta, arg);
	ath11k_peer_assoc_h_smps(sta, arg);

	arsta->peer_nss = arg->peer_nss;

	/* TODO: amsdu_disable req? */
}

static int ath11k_setup_peer_smps(struct ath11k *ar, struct ath11k_vif *arvif,
				  const u8 *addr,
				  const struct ieee80211_sta_ht_cap *ht_cap,
				  u16 he_6ghz_capa)
{
	int smps;

	if (!ht_cap->ht_supported && !he_6ghz_capa)
		return 0;

	if (ht_cap->ht_supported) {
		smps = ht_cap->cap & IEEE80211_HT_CAP_SM_PS;
		smps >>= IEEE80211_HT_CAP_SM_PS_SHIFT;
	} else {
		smps = FIELD_GET(IEEE80211_HE_6GHZ_CAP_SM_PS, he_6ghz_capa);
	}

	if (smps >= ARRAY_SIZE(ath11k_smps_map))
		return -EINVAL;

	return ath11k_wmi_set_peer_param(ar, addr, arvif->vdev_id,
					 WMI_PEER_MIMO_PS_STATE,
					 ath11k_smps_map[smps]);
}

static bool ath11k_mac_set_he_txbf_conf(struct ath11k_vif *arvif)
{
	struct ath11k *ar = arvif->ar;
	u32 param, value;
	int ret;

	if (!arvif->vif->bss_conf.he_support)
		return true;

	param = WMI_VDEV_PARAM_SET_HEMU_MODE;
	value = 0;
	if (arvif->vif->bss_conf.he_su_beamformer) {
		value |= FIELD_PREP(HE_MODE_SU_TX_BFER, HE_SU_BFER_ENABLE);
		if (arvif->vif->bss_conf.he_mu_beamformer &&
		    arvif->vdev_type == WMI_VDEV_TYPE_AP)
			value |= FIELD_PREP(HE_MODE_MU_TX_BFER, HE_MU_BFER_ENABLE);
	}

	if (arvif->vif->type != NL80211_IFTYPE_MESH_POINT) {
		value |= FIELD_PREP(HE_MODE_DL_OFDMA, HE_DL_MUOFDMA_ENABLE) |
			 FIELD_PREP(HE_MODE_UL_OFDMA, HE_UL_MUOFDMA_ENABLE);

		if (arvif->vif->bss_conf.he_full_ul_mumimo)
			value |= FIELD_PREP(HE_MODE_UL_MUMIMO, HE_UL_MUMIMO_ENABLE);

		if (arvif->vif->bss_conf.he_su_beamformee)
			value |= FIELD_PREP(HE_MODE_SU_TX_BFEE, HE_SU_BFEE_ENABLE);
	}

	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, param, value);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set vdev %d HE MU mode: %d\n",
			    arvif->vdev_id, ret);
		return false;
	}

	param = WMI_VDEV_PARAM_SET_HE_SOUNDING_MODE;
	value =	FIELD_PREP(HE_VHT_SOUNDING_MODE, HE_VHT_SOUNDING_MODE_ENABLE) |
		FIELD_PREP(HE_TRIG_NONTRIG_SOUNDING_MODE,
			   HE_TRIG_NONTRIG_SOUNDING_MODE_ENABLE);
	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    param, value);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set vdev %d sounding mode: %d\n",
			    arvif->vdev_id, ret);
		return false;
	}
	return true;
}

static bool ath11k_mac_vif_recalc_sta_he_txbf(struct ath11k *ar,
					      struct ieee80211_vif *vif,
					      struct ieee80211_sta_he_cap *he_cap)
{
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct ieee80211_he_cap_elem he_cap_elem = {0};
	struct ieee80211_sta_he_cap *cap_band = NULL;
	struct cfg80211_chan_def def;
	u32 param = WMI_VDEV_PARAM_SET_HEMU_MODE;
	u32 hemode = 0;
	int ret;

	if (!vif->bss_conf.he_support)
		return true;

	if (vif->type != NL80211_IFTYPE_STATION)
		return false;

	if (WARN_ON(ath11k_mac_vif_chan(vif, &def)))
		return false;

	if (def.chan->band == NL80211_BAND_2GHZ)
		cap_band = &ar->mac.iftype[NL80211_BAND_2GHZ][vif->type].he_cap;
	else
		cap_band = &ar->mac.iftype[NL80211_BAND_5GHZ][vif->type].he_cap;

	memcpy(&he_cap_elem, &cap_band->he_cap_elem, sizeof(he_cap_elem));

	if (HECAP_PHY_SUBFME_GET(he_cap_elem.phy_cap_info)) {
		if (HECAP_PHY_SUBFMR_GET(he_cap->he_cap_elem.phy_cap_info))
			hemode |= FIELD_PREP(HE_MODE_SU_TX_BFEE, HE_SU_BFEE_ENABLE);
		if (HECAP_PHY_MUBFMR_GET(he_cap->he_cap_elem.phy_cap_info))
			hemode |= FIELD_PREP(HE_MODE_MU_TX_BFEE, HE_MU_BFEE_ENABLE);
	}

	if (vif->type != NL80211_IFTYPE_MESH_POINT) {
		hemode |= FIELD_PREP(HE_MODE_DL_OFDMA, HE_DL_MUOFDMA_ENABLE) |
			  FIELD_PREP(HE_MODE_UL_OFDMA, HE_UL_MUOFDMA_ENABLE);

		if (HECAP_PHY_ULMUMIMO_GET(he_cap_elem.phy_cap_info))
			if (HECAP_PHY_ULMUMIMO_GET(he_cap->he_cap_elem.phy_cap_info))
				hemode |= FIELD_PREP(HE_MODE_UL_MUMIMO,
						     HE_UL_MUMIMO_ENABLE);

		if (FIELD_GET(HE_MODE_MU_TX_BFEE, hemode))
			hemode |= FIELD_PREP(HE_MODE_SU_TX_BFEE, HE_SU_BFEE_ENABLE);

		if (FIELD_GET(HE_MODE_MU_TX_BFER, hemode))
			hemode |= FIELD_PREP(HE_MODE_SU_TX_BFER, HE_SU_BFER_ENABLE);
	}

	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, param, hemode);
	if (ret) {
		ath11k_warn(ar->ab, "failed to submit vdev param txbf 0x%x: %d\n",
			    hemode, ret);
		return false;
	}

	return true;
}

static void ath11k_bss_assoc(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *bss_conf)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct peer_assoc_params peer_arg;
	struct ieee80211_sta *ap_sta;
	struct ath11k_peer *peer;
	bool is_auth = false;
	struct ieee80211_sta_he_cap  he_cap;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac vdev %i assoc bssid %pM aid %d\n",
		   arvif->vdev_id, arvif->bssid, arvif->aid);

	rcu_read_lock();

	ap_sta = ieee80211_find_sta(vif, bss_conf->bssid);
	if (!ap_sta) {
		ath11k_warn(ar->ab, "failed to find station entry for bss %pM vdev %i\n",
			    bss_conf->bssid, arvif->vdev_id);
		rcu_read_unlock();
		return;
	}

	/* he_cap here is updated at assoc success for sta mode only */
	he_cap  = ap_sta->deflink.he_cap;

	ath11k_peer_assoc_prepare(ar, vif, ap_sta, &peer_arg, false);

	rcu_read_unlock();

	peer_arg.is_assoc = true;
	ret = ath11k_wmi_send_peer_assoc_cmd(ar, &peer_arg);
	if (ret) {
		ath11k_warn(ar->ab, "failed to run peer assoc for %pM vdev %i: %d\n",
			    bss_conf->bssid, arvif->vdev_id, ret);
		return;
	}

	if (!wait_for_completion_timeout(&ar->peer_assoc_done, 1 * HZ)) {
		ath11k_warn(ar->ab, "failed to get peer assoc conf event for %pM vdev %i\n",
			    bss_conf->bssid, arvif->vdev_id);
		return;
	}

	ret = ath11k_setup_peer_smps(ar, arvif, bss_conf->bssid,
				     &ap_sta->deflink.ht_cap,
				     le16_to_cpu(ap_sta->deflink.he_6ghz_capa.capa));
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup peer SMPS for vdev %d: %d\n",
			    arvif->vdev_id, ret);
		return;
	}

	if (!ath11k_mac_vif_recalc_sta_he_txbf(ar, vif, &he_cap)) {
		ath11k_warn(ar->ab, "failed to recalc he txbf for vdev %i on bss %pM\n",
			    arvif->vdev_id, bss_conf->bssid);
		return;
	}

	WARN_ON(arvif->is_up);

	arvif->aid = vif->cfg.aid;
	ether_addr_copy(arvif->bssid, bss_conf->bssid);

	ret = ath11k_wmi_vdev_up(ar, arvif->vdev_id, arvif->aid, arvif->bssid);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set vdev %d up: %d\n",
			    arvif->vdev_id, ret);
		return;
	}

	arvif->is_up = true;
	arvif->rekey_data.enable_offload = false;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "mac vdev %d up (associated) bssid %pM aid %d\n",
		   arvif->vdev_id, bss_conf->bssid, vif->cfg.aid);

	spin_lock_bh(&ar->ab->base_lock);

	peer = ath11k_peer_find(ar->ab, arvif->vdev_id, arvif->bssid);
	if (peer && peer->is_authorized)
		is_auth = true;

	spin_unlock_bh(&ar->ab->base_lock);

	if (is_auth) {
		ret = ath11k_wmi_set_peer_param(ar, arvif->bssid,
						arvif->vdev_id,
						WMI_PEER_AUTHORIZE,
						1);
		if (ret)
			ath11k_warn(ar->ab, "Unable to authorize BSS peer: %d\n", ret);
	}

	ret = ath11k_wmi_send_obss_spr_cmd(ar, arvif->vdev_id,
					   &bss_conf->he_obss_pd);
	if (ret)
		ath11k_warn(ar->ab, "failed to set vdev %i OBSS PD parameters: %d\n",
			    arvif->vdev_id, ret);

	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_DTIM_POLICY,
					    WMI_DTIM_POLICY_STICK);
	if (ret)
		ath11k_warn(ar->ab, "failed to set vdev %d dtim policy: %d\n",
			    arvif->vdev_id, ret);

	ath11k_mac_11d_scan_stop_all(ar->ab);
}

static void ath11k_bss_disassoc(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac vdev %i disassoc bssid %pM\n",
		   arvif->vdev_id, arvif->bssid);

	ret = ath11k_wmi_vdev_down(ar, arvif->vdev_id);
	if (ret)
		ath11k_warn(ar->ab, "failed to down vdev %i: %d\n",
			    arvif->vdev_id, ret);

	arvif->is_up = false;

	memset(&arvif->rekey_data, 0, sizeof(arvif->rekey_data));

	cancel_delayed_work_sync(&arvif->connection_loss_work);
}

static u32 ath11k_mac_get_rate_hw_value(int bitrate)
{
	u32 preamble;
	u16 hw_value;
	int rate;
	size_t i;

	if (ath11k_mac_bitrate_is_cck(bitrate))
		preamble = WMI_RATE_PREAMBLE_CCK;
	else
		preamble = WMI_RATE_PREAMBLE_OFDM;

	for (i = 0; i < ARRAY_SIZE(ath11k_legacy_rates); i++) {
		if (ath11k_legacy_rates[i].bitrate != bitrate)
			continue;

		hw_value = ath11k_legacy_rates[i].hw_value;
		rate = ATH11K_HW_RATE_CODE(hw_value, 0, preamble);

		return rate;
	}

	return -EINVAL;
}

static void ath11k_recalculate_mgmt_rate(struct ath11k *ar,
					 struct ieee80211_vif *vif,
					 struct cfg80211_chan_def *def)
{
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	const struct ieee80211_supported_band *sband;
	u8 basic_rate_idx;
	int hw_rate_code;
	u32 vdev_param;
	u16 bitrate;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	sband = ar->hw->wiphy->bands[def->chan->band];
	basic_rate_idx = ffs(vif->bss_conf.basic_rates) - 1;
	bitrate = sband->bitrates[basic_rate_idx].bitrate;

	hw_rate_code = ath11k_mac_get_rate_hw_value(bitrate);
	if (hw_rate_code < 0) {
		ath11k_warn(ar->ab, "bitrate not supported %d\n", bitrate);
		return;
	}

	vdev_param = WMI_VDEV_PARAM_MGMT_RATE;
	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, vdev_param,
					    hw_rate_code);
	if (ret)
		ath11k_warn(ar->ab, "failed to set mgmt tx rate %d\n", ret);

	/* For WCN6855, firmware will clear this param when vdev starts, hence
	 * cache it here so that we can reconfigure it once vdev starts.
	 */
	ar->hw_rate_code = hw_rate_code;

	vdev_param = WMI_VDEV_PARAM_BEACON_RATE;
	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, vdev_param,
					    hw_rate_code);
	if (ret)
		ath11k_warn(ar->ab, "failed to set beacon tx rate %d\n", ret);
}

static int ath11k_mac_fils_discovery(struct ath11k_vif *arvif,
				     struct ieee80211_bss_conf *info)
{
	struct ath11k *ar = arvif->ar;
	struct sk_buff *tmpl;
	int ret;
	u32 interval;
	bool unsol_bcast_probe_resp_enabled = false;

	if (info->fils_discovery.max_interval) {
		interval = info->fils_discovery.max_interval;

		tmpl = ieee80211_get_fils_discovery_tmpl(ar->hw, arvif->vif);
		if (tmpl)
			ret = ath11k_wmi_fils_discovery_tmpl(ar, arvif->vdev_id,
							     tmpl);
	} else if (info->unsol_bcast_probe_resp_interval) {
		unsol_bcast_probe_resp_enabled = 1;
		interval = info->unsol_bcast_probe_resp_interval;

		tmpl = ieee80211_get_unsol_bcast_probe_resp_tmpl(ar->hw,
								 arvif->vif);
		if (tmpl)
			ret = ath11k_wmi_probe_resp_tmpl(ar, arvif->vdev_id,
							 tmpl);
	} else { /* Disable */
		return ath11k_wmi_fils_discovery(ar, arvif->vdev_id, 0, false);
	}

	if (!tmpl) {
		ath11k_warn(ar->ab,
			    "mac vdev %i failed to retrieve %s template\n",
			    arvif->vdev_id, (unsol_bcast_probe_resp_enabled ?
			    "unsolicited broadcast probe response" :
			    "FILS discovery"));
		return -EPERM;
	}
	kfree_skb(tmpl);

	if (!ret)
		ret = ath11k_wmi_fils_discovery(ar, arvif->vdev_id, interval,
						unsol_bcast_probe_resp_enabled);

	return ret;
}

static int ath11k_mac_config_obss_pd(struct ath11k *ar,
				     struct ieee80211_he_obss_pd *he_obss_pd)
{
	u32 bitmap[2], param_id, param_val, pdev_id;
	int ret;
	s8 non_srg_th = 0, srg_th = 0;

	pdev_id = ar->pdev->pdev_id;

	/* Set and enable SRG/non-SRG OBSS PD Threshold */
	param_id = WMI_PDEV_PARAM_SET_CMD_OBSS_PD_THRESHOLD;
	if (test_bit(ATH11K_FLAG_MONITOR_STARTED, &ar->monitor_flags)) {
		ret = ath11k_wmi_pdev_set_param(ar, param_id, 0, pdev_id);
		if (ret)
			ath11k_warn(ar->ab,
				    "failed to set obss_pd_threshold for pdev: %u\n",
				    pdev_id);
		return ret;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "mac obss pd sr_ctrl %x non_srg_thres %u srg_max %u\n",
		   he_obss_pd->sr_ctrl, he_obss_pd->non_srg_max_offset,
		   he_obss_pd->max_offset);

	param_val = 0;

	if (he_obss_pd->sr_ctrl &
	    IEEE80211_HE_SPR_NON_SRG_OBSS_PD_SR_DISALLOWED) {
		non_srg_th = ATH11K_OBSS_PD_MAX_THRESHOLD;
	} else {
		if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT)
			non_srg_th = (ATH11K_OBSS_PD_MAX_THRESHOLD +
				      he_obss_pd->non_srg_max_offset);
		else
			non_srg_th = ATH11K_OBSS_PD_NON_SRG_MAX_THRESHOLD;

		param_val |= ATH11K_OBSS_PD_NON_SRG_EN;
	}

	if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT) {
		srg_th = ATH11K_OBSS_PD_MAX_THRESHOLD + he_obss_pd->max_offset;
		param_val |= ATH11K_OBSS_PD_SRG_EN;
	}

	if (test_bit(WMI_TLV_SERVICE_SRG_SRP_SPATIAL_REUSE_SUPPORT,
		     ar->ab->wmi_ab.svc_map)) {
		param_val |= ATH11K_OBSS_PD_THRESHOLD_IN_DBM;
		param_val |= FIELD_PREP(GENMASK(15, 8), srg_th);
	} else {
		non_srg_th -= ATH11K_DEFAULT_NOISE_FLOOR;
		/* SRG not supported and threshold in dB */
		param_val &= ~(ATH11K_OBSS_PD_SRG_EN |
			       ATH11K_OBSS_PD_THRESHOLD_IN_DBM);
	}

	param_val |= (non_srg_th & GENMASK(7, 0));
	ret = ath11k_wmi_pdev_set_param(ar, param_id, param_val, pdev_id);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to set obss_pd_threshold for pdev: %u\n",
			    pdev_id);
		return ret;
	}

	/* Enable OBSS PD for all access category */
	param_id  = WMI_PDEV_PARAM_SET_CMD_OBSS_PD_PER_AC;
	param_val = 0xf;
	ret = ath11k_wmi_pdev_set_param(ar, param_id, param_val, pdev_id);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to set obss_pd_per_ac for pdev: %u\n",
			    pdev_id);
		return ret;
	}

	/* Set SR Prohibit */
	param_id  = WMI_PDEV_PARAM_ENABLE_SR_PROHIBIT;
	param_val = !!(he_obss_pd->sr_ctrl &
		       IEEE80211_HE_SPR_HESIGA_SR_VAL15_ALLOWED);
	ret = ath11k_wmi_pdev_set_param(ar, param_id, param_val, pdev_id);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set sr_prohibit for pdev: %u\n",
			    pdev_id);
		return ret;
	}

	if (!test_bit(WMI_TLV_SERVICE_SRG_SRP_SPATIAL_REUSE_SUPPORT,
		      ar->ab->wmi_ab.svc_map))
		return 0;

	/* Set SRG BSS Color Bitmap */
	memcpy(bitmap, he_obss_pd->bss_color_bitmap, sizeof(bitmap));
	ret = ath11k_wmi_pdev_set_srg_bss_color_bitmap(ar, bitmap);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to set bss_color_bitmap for pdev: %u\n",
			    pdev_id);
		return ret;
	}

	/* Set SRG Partial BSSID Bitmap */
	memcpy(bitmap, he_obss_pd->partial_bssid_bitmap, sizeof(bitmap));
	ret = ath11k_wmi_pdev_set_srg_patial_bssid_bitmap(ar, bitmap);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to set partial_bssid_bitmap for pdev: %u\n",
			    pdev_id);
		return ret;
	}

	memset(bitmap, 0xff, sizeof(bitmap));

	/* Enable all BSS Colors for SRG */
	ret = ath11k_wmi_pdev_srg_obss_color_enable_bitmap(ar, bitmap);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to set srg_color_en_bitmap pdev: %u\n",
			    pdev_id);
		return ret;
	}

	/* Enable all partial BSSID mask for SRG */
	ret = ath11k_wmi_pdev_srg_obss_bssid_enable_bitmap(ar, bitmap);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to set srg_bssid_en_bitmap pdev: %u\n",
			    pdev_id);
		return ret;
	}

	/* Enable all BSS Colors for non-SRG */
	ret = ath11k_wmi_pdev_non_srg_obss_color_enable_bitmap(ar, bitmap);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to set non_srg_color_en_bitmap pdev: %u\n",
			    pdev_id);
		return ret;
	}

	/* Enable all partial BSSID mask for non-SRG */
	ret = ath11k_wmi_pdev_non_srg_obss_bssid_enable_bitmap(ar, bitmap);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to set non_srg_bssid_en_bitmap pdev: %u\n",
			    pdev_id);
		return ret;
	}

	return 0;
}

static void ath11k_mac_op_bss_info_changed(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_bss_conf *info,
					   u64 changed)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	struct cfg80211_chan_def def;
	u32 param_id, param_value;
	enum nl80211_band band;
	u32 vdev_param;
	int mcast_rate;
	u32 preamble;
	u16 hw_value;
	u16 bitrate;
	int ret = 0;
	u8 rateidx;
	u32 rate, param;
	u32 ipv4_cnt;

	mutex_lock(&ar->conf_mutex);

	if (changed & BSS_CHANGED_BEACON_INT) {
		arvif->beacon_interval = info->beacon_int;

		param_id = WMI_VDEV_PARAM_BEACON_INTERVAL;
		ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    param_id,
						    arvif->beacon_interval);
		if (ret)
			ath11k_warn(ar->ab, "Failed to set beacon interval for VDEV: %d\n",
				    arvif->vdev_id);
		else
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
				   "Beacon interval: %d set for VDEV: %d\n",
				   arvif->beacon_interval, arvif->vdev_id);
	}

	if (changed & BSS_CHANGED_BEACON) {
		param_id = WMI_PDEV_PARAM_BEACON_TX_MODE;
		param_value = WMI_BEACON_STAGGERED_MODE;
		ret = ath11k_wmi_pdev_set_param(ar, param_id,
						param_value, ar->pdev->pdev_id);
		if (ret)
			ath11k_warn(ar->ab, "Failed to set beacon mode for VDEV: %d\n",
				    arvif->vdev_id);
		else
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
				   "Set staggered beacon mode for VDEV: %d\n",
				   arvif->vdev_id);

		if (!arvif->do_not_send_tmpl || !arvif->bcca_zero_sent) {
			ret = ath11k_mac_setup_bcn_tmpl(arvif);
			if (ret)
				ath11k_warn(ar->ab, "failed to update bcn template: %d\n",
					    ret);
		}

		if (arvif->bcca_zero_sent)
			arvif->do_not_send_tmpl = true;
		else
			arvif->do_not_send_tmpl = false;

		if (vif->bss_conf.he_support) {
			ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
							    WMI_VDEV_PARAM_BA_MODE,
							    WMI_BA_MODE_BUFFER_SIZE_256);
			if (ret)
				ath11k_warn(ar->ab,
					    "failed to set BA BUFFER SIZE 256 for vdev: %d\n",
					    arvif->vdev_id);
			else
				ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
					   "Set BA BUFFER SIZE 256 for VDEV: %d\n",
					   arvif->vdev_id);
		}
	}

	if (changed & (BSS_CHANGED_BEACON_INFO | BSS_CHANGED_BEACON)) {
		arvif->dtim_period = info->dtim_period;

		param_id = WMI_VDEV_PARAM_DTIM_PERIOD;
		ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    param_id,
						    arvif->dtim_period);

		if (ret)
			ath11k_warn(ar->ab, "Failed to set dtim period for VDEV %d: %i\n",
				    arvif->vdev_id, ret);
		else
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
				   "DTIM period: %d set for VDEV: %d\n",
				   arvif->dtim_period, arvif->vdev_id);
	}

	if (changed & BSS_CHANGED_SSID &&
	    vif->type == NL80211_IFTYPE_AP) {
		arvif->u.ap.ssid_len = vif->cfg.ssid_len;
		if (vif->cfg.ssid_len)
			memcpy(arvif->u.ap.ssid, vif->cfg.ssid,
			       vif->cfg.ssid_len);
		arvif->u.ap.hidden_ssid = info->hidden_ssid;
	}

	if (changed & BSS_CHANGED_BSSID && !is_zero_ether_addr(info->bssid))
		ether_addr_copy(arvif->bssid, info->bssid);

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		if (info->enable_beacon)
			ath11k_mac_set_he_txbf_conf(arvif);
		ath11k_control_beaconing(arvif, info);

		if (arvif->is_up && vif->bss_conf.he_support &&
		    vif->bss_conf.he_oper.params) {
			param_id = WMI_VDEV_PARAM_HEOPS_0_31;
			param_value = vif->bss_conf.he_oper.params;
			ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
							    param_id, param_value);
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
				   "he oper param: %x set for VDEV: %d\n",
				   param_value, arvif->vdev_id);

			if (ret)
				ath11k_warn(ar->ab, "Failed to set he oper params %x for VDEV %d: %i\n",
					    param_value, arvif->vdev_id, ret);
		}
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		u32 cts_prot;

		cts_prot = !!(info->use_cts_prot);
		param_id = WMI_VDEV_PARAM_PROTECTION_MODE;

		if (arvif->is_started) {
			ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
							    param_id, cts_prot);
			if (ret)
				ath11k_warn(ar->ab, "Failed to set CTS prot for VDEV: %d\n",
					    arvif->vdev_id);
			else
				ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "Set CTS prot: %d for VDEV: %d\n",
					   cts_prot, arvif->vdev_id);
		} else {
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "defer protection mode setup, vdev is not ready yet\n");
		}
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		u32 slottime;

		if (info->use_short_slot)
			slottime = WMI_VDEV_SLOT_TIME_SHORT; /* 9us */

		else
			slottime = WMI_VDEV_SLOT_TIME_LONG; /* 20us */

		param_id = WMI_VDEV_PARAM_SLOT_TIME;
		ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    param_id, slottime);
		if (ret)
			ath11k_warn(ar->ab, "Failed to set erp slot for VDEV: %d\n",
				    arvif->vdev_id);
		else
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
				   "Set slottime: %d for VDEV: %d\n",
				   slottime, arvif->vdev_id);
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		u32 preamble;

		if (info->use_short_preamble)
			preamble = WMI_VDEV_PREAMBLE_SHORT;
		else
			preamble = WMI_VDEV_PREAMBLE_LONG;

		param_id = WMI_VDEV_PARAM_PREAMBLE;
		ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    param_id, preamble);
		if (ret)
			ath11k_warn(ar->ab, "Failed to set preamble for VDEV: %d\n",
				    arvif->vdev_id);
		else
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
				   "Set preamble: %d for VDEV: %d\n",
				   preamble, arvif->vdev_id);
	}

	if (changed & BSS_CHANGED_ASSOC) {
		if (vif->cfg.assoc)
			ath11k_bss_assoc(hw, vif, info);
		else
			ath11k_bss_disassoc(hw, vif);
	}

	if (changed & BSS_CHANGED_TXPOWER) {
		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac vdev_id %i txpower %d\n",
			   arvif->vdev_id, info->txpower);

		arvif->txpower = info->txpower;
		ath11k_mac_txpower_recalc(ar);
	}

	if (changed & BSS_CHANGED_PS &&
	    ar->ab->hw_params.supports_sta_ps) {
		arvif->ps = vif->cfg.ps;

		ret = ath11k_mac_config_ps(ar);
		if (ret)
			ath11k_warn(ar->ab, "failed to setup ps on vdev %i: %d\n",
				    arvif->vdev_id, ret);
	}

	if (changed & BSS_CHANGED_MCAST_RATE &&
	    !ath11k_mac_vif_chan(arvif->vif, &def)) {
		band = def.chan->band;
		mcast_rate = vif->bss_conf.mcast_rate[band];

		if (mcast_rate > 0)
			rateidx = mcast_rate - 1;
		else
			rateidx = ffs(vif->bss_conf.basic_rates) - 1;

		if (ar->pdev->cap.supported_bands & WMI_HOST_WLAN_5G_CAP)
			rateidx += ATH11K_MAC_FIRST_OFDM_RATE_IDX;

		bitrate = ath11k_legacy_rates[rateidx].bitrate;
		hw_value = ath11k_legacy_rates[rateidx].hw_value;

		if (ath11k_mac_bitrate_is_cck(bitrate))
			preamble = WMI_RATE_PREAMBLE_CCK;
		else
			preamble = WMI_RATE_PREAMBLE_OFDM;

		rate = ATH11K_HW_RATE_CODE(hw_value, 0, preamble);

		ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
			   "mac vdev %d mcast_rate %x\n",
			   arvif->vdev_id, rate);

		vdev_param = WMI_VDEV_PARAM_MCAST_DATA_RATE;
		ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    vdev_param, rate);
		if (ret)
			ath11k_warn(ar->ab,
				    "failed to set mcast rate on vdev %i: %d\n",
				    arvif->vdev_id,  ret);

		vdev_param = WMI_VDEV_PARAM_BCAST_DATA_RATE;
		ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    vdev_param, rate);
		if (ret)
			ath11k_warn(ar->ab,
				    "failed to set bcast rate on vdev %i: %d\n",
				    arvif->vdev_id,  ret);
	}

	if (changed & BSS_CHANGED_BASIC_RATES &&
	    !ath11k_mac_vif_chan(arvif->vif, &def))
		ath11k_recalculate_mgmt_rate(ar, vif, &def);

	if (changed & BSS_CHANGED_TWT) {
		struct wmi_twt_enable_params twt_params = {0};

		if (info->twt_requester || info->twt_responder) {
			ath11k_wmi_fill_default_twt_params(&twt_params);
			ath11k_wmi_send_twt_enable_cmd(ar, ar->pdev->pdev_id,
						       &twt_params);
		} else {
			ath11k_wmi_send_twt_disable_cmd(ar, ar->pdev->pdev_id);
		}
	}

	if (changed & BSS_CHANGED_HE_OBSS_PD)
		ath11k_mac_config_obss_pd(ar, &info->he_obss_pd);

	if (changed & BSS_CHANGED_HE_BSS_COLOR) {
		if (vif->type == NL80211_IFTYPE_AP) {
			ret = ath11k_wmi_send_obss_color_collision_cfg_cmd(
				ar, arvif->vdev_id, info->he_bss_color.color,
				ATH11K_BSS_COLOR_COLLISION_DETECTION_AP_PERIOD_MS,
				info->he_bss_color.enabled);
			if (ret)
				ath11k_warn(ar->ab, "failed to set bss color collision on vdev %i: %d\n",
					    arvif->vdev_id,  ret);

			param_id = WMI_VDEV_PARAM_BSS_COLOR;
			if (info->he_bss_color.enabled)
				param_value = info->he_bss_color.color <<
						IEEE80211_HE_OPERATION_BSS_COLOR_OFFSET;
			else
				param_value = IEEE80211_HE_OPERATION_BSS_COLOR_DISABLED;

			ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
							    param_id,
							    param_value);
			if (ret)
				ath11k_warn(ar->ab,
					    "failed to set bss color param on vdev %i: %d\n",
					    arvif->vdev_id,  ret);

			ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
				   "bss color param 0x%x set on vdev %i\n",
				   param_value, arvif->vdev_id);
		} else if (vif->type == NL80211_IFTYPE_STATION) {
			ret = ath11k_wmi_send_bss_color_change_enable_cmd(ar,
									  arvif->vdev_id,
									  1);
			if (ret)
				ath11k_warn(ar->ab, "failed to enable bss color change on vdev %i: %d\n",
					    arvif->vdev_id,  ret);
			ret = ath11k_wmi_send_obss_color_collision_cfg_cmd(
				ar, arvif->vdev_id, 0,
				ATH11K_BSS_COLOR_COLLISION_DETECTION_STA_PERIOD_MS, 1);
			if (ret)
				ath11k_warn(ar->ab, "failed to set bss color collision on vdev %i: %d\n",
					    arvif->vdev_id,  ret);
		}
	}

	if (changed & BSS_CHANGED_FTM_RESPONDER &&
	    arvif->ftm_responder != info->ftm_responder &&
	    test_bit(WMI_TLV_SERVICE_RTT, ar->ab->wmi_ab.svc_map) &&
	    (vif->type == NL80211_IFTYPE_AP ||
	     vif->type == NL80211_IFTYPE_MESH_POINT)) {
		arvif->ftm_responder = info->ftm_responder;
		param = WMI_VDEV_PARAM_ENABLE_DISABLE_RTT_RESPONDER_ROLE;
		ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, param,
						    arvif->ftm_responder);
		if (ret)
			ath11k_warn(ar->ab, "Failed to set ftm responder %i: %d\n",
				    arvif->vdev_id, ret);
	}

	if (changed & BSS_CHANGED_FILS_DISCOVERY ||
	    changed & BSS_CHANGED_UNSOL_BCAST_PROBE_RESP)
		ath11k_mac_fils_discovery(arvif, info);

	if (changed & BSS_CHANGED_ARP_FILTER) {
		ipv4_cnt = min(vif->cfg.arp_addr_cnt, ATH11K_IPV4_MAX_COUNT);
		memcpy(arvif->arp_ns_offload.ipv4_addr,
		       vif->cfg.arp_addr_list,
		       ipv4_cnt * sizeof(u32));
		memcpy(arvif->arp_ns_offload.mac_addr, vif->addr, ETH_ALEN);
		arvif->arp_ns_offload.ipv4_count = ipv4_cnt;

		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac arp_addr_cnt %d vif->addr %pM, offload_addr %pI4\n",
			   vif->cfg.arp_addr_cnt,
			   vif->addr, arvif->arp_ns_offload.ipv4_addr);
	}

	mutex_unlock(&ar->conf_mutex);
}

void __ath11k_mac_scan_finish(struct ath11k *ar)
{
	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH11K_SCAN_IDLE:
		break;
	case ATH11K_SCAN_RUNNING:
	case ATH11K_SCAN_ABORTING:
		if (ar->scan.is_roc && ar->scan.roc_notify)
			ieee80211_remain_on_channel_expired(ar->hw);
		fallthrough;
	case ATH11K_SCAN_STARTING:
		if (!ar->scan.is_roc) {
			struct cfg80211_scan_info info = {
				.aborted = ((ar->scan.state ==
					    ATH11K_SCAN_ABORTING) ||
					    (ar->scan.state ==
					    ATH11K_SCAN_STARTING)),
			};

			ieee80211_scan_completed(ar->hw, &info);
		}

		ar->scan.state = ATH11K_SCAN_IDLE;
		ar->scan_channel = NULL;
		ar->scan.roc_freq = 0;
		cancel_delayed_work(&ar->scan.timeout);
		complete_all(&ar->scan.completed);
		break;
	}
}

void ath11k_mac_scan_finish(struct ath11k *ar)
{
	spin_lock_bh(&ar->data_lock);
	__ath11k_mac_scan_finish(ar);
	spin_unlock_bh(&ar->data_lock);
}

static int ath11k_scan_stop(struct ath11k *ar)
{
	struct scan_cancel_param arg = {
		.req_type = WLAN_SCAN_CANCEL_SINGLE,
		.scan_id = ATH11K_SCAN_ID,
	};
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	/* TODO: Fill other STOP Params */
	arg.pdev_id = ar->pdev->pdev_id;

	ret = ath11k_wmi_send_scan_stop_cmd(ar, &arg);
	if (ret) {
		ath11k_warn(ar->ab, "failed to stop wmi scan: %d\n", ret);
		goto out;
	}

	ret = wait_for_completion_timeout(&ar->scan.completed, 3 * HZ);
	if (ret == 0) {
		ath11k_warn(ar->ab,
			    "failed to receive scan abort comple: timed out\n");
		ret = -ETIMEDOUT;
	} else if (ret > 0) {
		ret = 0;
	}

out:
	/* Scan state should be updated upon scan completion but in case
	 * firmware fails to deliver the event (for whatever reason) it is
	 * desired to clean up scan state anyway. Firmware may have just
	 * dropped the scan completion event delivery due to transport pipe
	 * being overflown with data and/or it can recover on its own before
	 * next scan request is submitted.
	 */
	spin_lock_bh(&ar->data_lock);
	if (ar->scan.state != ATH11K_SCAN_IDLE)
		__ath11k_mac_scan_finish(ar);
	spin_unlock_bh(&ar->data_lock);

	return ret;
}

static void ath11k_scan_abort(struct ath11k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH11K_SCAN_IDLE:
		/* This can happen if timeout worker kicked in and called
		 * abortion while scan completion was being processed.
		 */
		break;
	case ATH11K_SCAN_STARTING:
	case ATH11K_SCAN_ABORTING:
		ath11k_warn(ar->ab, "refusing scan abortion due to invalid scan state: %d\n",
			    ar->scan.state);
		break;
	case ATH11K_SCAN_RUNNING:
		ar->scan.state = ATH11K_SCAN_ABORTING;
		spin_unlock_bh(&ar->data_lock);

		ret = ath11k_scan_stop(ar);
		if (ret)
			ath11k_warn(ar->ab, "failed to abort scan: %d\n", ret);

		spin_lock_bh(&ar->data_lock);
		break;
	}

	spin_unlock_bh(&ar->data_lock);
}

static void ath11k_scan_timeout_work(struct work_struct *work)
{
	struct ath11k *ar = container_of(work, struct ath11k,
					 scan.timeout.work);

	mutex_lock(&ar->conf_mutex);
	ath11k_scan_abort(ar);
	mutex_unlock(&ar->conf_mutex);
}

static int ath11k_start_scan(struct ath11k *ar,
			     struct scan_req_params *arg)
{
	int ret;
	unsigned long timeout = 1 * HZ;

	lockdep_assert_held(&ar->conf_mutex);

	if (ath11k_spectral_get_mode(ar) == ATH11K_SPECTRAL_BACKGROUND)
		ath11k_spectral_reset_buffer(ar);

	ret = ath11k_wmi_send_scan_start_cmd(ar, arg);
	if (ret)
		return ret;

	if (test_bit(WMI_TLV_SERVICE_11D_OFFLOAD, ar->ab->wmi_ab.svc_map)) {
		timeout = 5 * HZ;

		if (ar->supports_6ghz)
			timeout += 5 * HZ;
	}

	ret = wait_for_completion_timeout(&ar->scan.started, timeout);
	if (ret == 0) {
		ret = ath11k_scan_stop(ar);
		if (ret)
			ath11k_warn(ar->ab, "failed to stop scan: %d\n", ret);

		return -ETIMEDOUT;
	}

	/* If we failed to start the scan, return error code at
	 * this point.  This is probably due to some issue in the
	 * firmware, but no need to wedge the driver due to that...
	 */
	spin_lock_bh(&ar->data_lock);
	if (ar->scan.state == ATH11K_SCAN_IDLE) {
		spin_unlock_bh(&ar->data_lock);
		return -EINVAL;
	}
	spin_unlock_bh(&ar->data_lock);

	return 0;
}

static int ath11k_mac_op_hw_scan(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_scan_request *hw_req)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	struct cfg80211_scan_request *req = &hw_req->req;
	struct scan_req_params *arg = NULL;
	int ret = 0;
	int i;
	u32 scan_timeout;

	/* Firmwares advertising the support of triggering 11D algorithm
	 * on the scan results of a regular scan expects driver to send
	 * WMI_11D_SCAN_START_CMDID before sending WMI_START_SCAN_CMDID.
	 * With this feature, separate 11D scan can be avoided since
	 * regdomain can be determined with the scan results of the
	 * regular scan.
	 */
	if (ar->state_11d == ATH11K_11D_PREPARING &&
	    test_bit(WMI_TLV_SERVICE_SUPPORT_11D_FOR_HOST_SCAN,
		     ar->ab->wmi_ab.svc_map))
		ath11k_mac_11d_scan_start(ar, arvif->vdev_id);

	mutex_lock(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	switch (ar->scan.state) {
	case ATH11K_SCAN_IDLE:
		reinit_completion(&ar->scan.started);
		reinit_completion(&ar->scan.completed);
		ar->scan.state = ATH11K_SCAN_STARTING;
		ar->scan.is_roc = false;
		ar->scan.vdev_id = arvif->vdev_id;
		ret = 0;
		break;
	case ATH11K_SCAN_STARTING:
	case ATH11K_SCAN_RUNNING:
	case ATH11K_SCAN_ABORTING:
		ret = -EBUSY;
		break;
	}
	spin_unlock_bh(&ar->data_lock);

	if (ret)
		goto exit;

	arg = kzalloc(sizeof(*arg), GFP_KERNEL);

	if (!arg) {
		ret = -ENOMEM;
		goto exit;
	}

	ath11k_wmi_start_scan_init(ar, arg);
	arg->vdev_id = arvif->vdev_id;
	arg->scan_id = ATH11K_SCAN_ID;

	if (req->ie_len) {
		arg->extraie.ptr = kmemdup(req->ie, req->ie_len, GFP_KERNEL);
		if (!arg->extraie.ptr) {
			ret = -ENOMEM;
			goto exit;
		}
		arg->extraie.len = req->ie_len;
	}

	if (req->n_ssids) {
		arg->num_ssids = req->n_ssids;
		for (i = 0; i < arg->num_ssids; i++) {
			arg->ssid[i].length  = req->ssids[i].ssid_len;
			memcpy(&arg->ssid[i].ssid, req->ssids[i].ssid,
			       req->ssids[i].ssid_len);
		}
	} else {
		arg->scan_flags |= WMI_SCAN_FLAG_PASSIVE;
	}

	if (req->n_channels) {
		arg->num_chan = req->n_channels;
		arg->chan_list = kcalloc(arg->num_chan, sizeof(*arg->chan_list),
					 GFP_KERNEL);

		if (!arg->chan_list) {
			ret = -ENOMEM;
			goto exit;
		}

		for (i = 0; i < arg->num_chan; i++) {
			if (test_bit(WMI_TLV_SERVICE_SCAN_CONFIG_PER_CHANNEL,
				     ar->ab->wmi_ab.svc_map)) {
				arg->chan_list[i] =
					u32_encode_bits(req->channels[i]->center_freq,
							WMI_SCAN_CONFIG_PER_CHANNEL_MASK);

				/* If NL80211_SCAN_FLAG_COLOCATED_6GHZ is set in scan
				 * flags, then scan all PSC channels in 6 GHz band and
				 * those non-PSC channels where RNR IE is found during
				 * the legacy 2.4/5 GHz scan.
				 * If NL80211_SCAN_FLAG_COLOCATED_6GHZ is not set,
				 * then all channels in 6 GHz will be scanned.
				 */
				if (req->channels[i]->band == NL80211_BAND_6GHZ &&
				    req->flags & NL80211_SCAN_FLAG_COLOCATED_6GHZ &&
				    !cfg80211_channel_is_psc(req->channels[i]))
					arg->chan_list[i] |=
						WMI_SCAN_CH_FLAG_SCAN_ONLY_IF_RNR_FOUND;
			} else {
				arg->chan_list[i] = req->channels[i]->center_freq;
			}
		}
	}

	if (req->flags & NL80211_SCAN_FLAG_RANDOM_ADDR) {
		arg->scan_f_add_spoofed_mac_in_probe = 1;
		ether_addr_copy(arg->mac_addr.addr, req->mac_addr);
		ether_addr_copy(arg->mac_mask.addr, req->mac_addr_mask);
	}

	/* if duration is set, default dwell times will be overwritten */
	if (req->duration) {
		arg->dwell_time_active = req->duration;
		arg->dwell_time_active_2g = req->duration;
		arg->dwell_time_active_6g = req->duration;
		arg->dwell_time_passive = req->duration;
		arg->dwell_time_passive_6g = req->duration;
		arg->burst_duration = req->duration;

		scan_timeout = min_t(u32, arg->max_rest_time *
				(arg->num_chan - 1) + (req->duration +
				ATH11K_SCAN_CHANNEL_SWITCH_WMI_EVT_OVERHEAD) *
				arg->num_chan, arg->max_scan_time);
	} else {
		scan_timeout = arg->max_scan_time;
	}

	/* Add a margin to account for event/command processing */
	scan_timeout += ATH11K_MAC_SCAN_CMD_EVT_OVERHEAD;

	ret = ath11k_start_scan(ar, arg);
	if (ret) {
		ath11k_warn(ar->ab, "failed to start hw scan: %d\n", ret);
		spin_lock_bh(&ar->data_lock);
		ar->scan.state = ATH11K_SCAN_IDLE;
		spin_unlock_bh(&ar->data_lock);
	}

	ieee80211_queue_delayed_work(ar->hw, &ar->scan.timeout,
				     msecs_to_jiffies(scan_timeout));

exit:
	if (arg) {
		kfree(arg->chan_list);
		kfree(arg->extraie.ptr);
		kfree(arg);
	}

	mutex_unlock(&ar->conf_mutex);

	if (ar->state_11d == ATH11K_11D_PREPARING)
		ath11k_mac_11d_scan_start(ar, arvif->vdev_id);

	return ret;
}

static void ath11k_mac_op_cancel_hw_scan(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif)
{
	struct ath11k *ar = hw->priv;

	mutex_lock(&ar->conf_mutex);
	ath11k_scan_abort(ar);
	mutex_unlock(&ar->conf_mutex);

	cancel_delayed_work_sync(&ar->scan.timeout);
}

static int ath11k_install_key(struct ath11k_vif *arvif,
			      struct ieee80211_key_conf *key,
			      enum set_key_cmd cmd,
			      const u8 *macaddr, u32 flags)
{
	int ret;
	struct ath11k *ar = arvif->ar;
	struct wmi_vdev_install_key_arg arg = {
		.vdev_id = arvif->vdev_id,
		.key_idx = key->keyidx,
		.key_len = key->keylen,
		.key_data = key->key,
		.key_flags = flags,
		.macaddr = macaddr,
	};

	lockdep_assert_held(&arvif->ar->conf_mutex);

	reinit_completion(&ar->install_key_done);

	if (test_bit(ATH11K_FLAG_HW_CRYPTO_DISABLED, &ar->ab->dev_flags))
		return 0;

	if (cmd == DISABLE_KEY) {
		arg.key_cipher = WMI_CIPHER_NONE;
		arg.key_data = NULL;
		goto install;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		arg.key_cipher = WMI_CIPHER_AES_CCM;
		/* TODO: Re-check if flag is valid */
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV_MGMT;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		arg.key_cipher = WMI_CIPHER_TKIP;
		arg.key_txmic_len = 8;
		arg.key_rxmic_len = 8;
		break;
	case WLAN_CIPHER_SUITE_CCMP_256:
		arg.key_cipher = WMI_CIPHER_AES_CCM;
		break;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		arg.key_cipher = WMI_CIPHER_AES_GCM;
		break;
	default:
		ath11k_warn(ar->ab, "cipher %d is not supported\n", key->cipher);
		return -EOPNOTSUPP;
	}

	if (test_bit(ATH11K_FLAG_RAW_MODE, &ar->ab->dev_flags))
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV |
			      IEEE80211_KEY_FLAG_RESERVE_TAILROOM;

install:
	ret = ath11k_wmi_vdev_install_key(arvif->ar, &arg);

	if (ret)
		return ret;

	if (!wait_for_completion_timeout(&ar->install_key_done, 1 * HZ))
		return -ETIMEDOUT;

	return ar->install_key_status ? -EINVAL : 0;
}

static int ath11k_clear_peer_keys(struct ath11k_vif *arvif,
				  const u8 *addr)
{
	struct ath11k *ar = arvif->ar;
	struct ath11k_base *ab = ar->ab;
	struct ath11k_peer *peer;
	int first_errno = 0;
	int ret;
	int i;
	u32 flags = 0;

	lockdep_assert_held(&ar->conf_mutex);

	spin_lock_bh(&ab->base_lock);
	peer = ath11k_peer_find(ab, arvif->vdev_id, addr);
	spin_unlock_bh(&ab->base_lock);

	if (!peer)
		return -ENOENT;

	for (i = 0; i < ARRAY_SIZE(peer->keys); i++) {
		if (!peer->keys[i])
			continue;

		/* key flags are not required to delete the key */
		ret = ath11k_install_key(arvif, peer->keys[i],
					 DISABLE_KEY, addr, flags);
		if (ret < 0 && first_errno == 0)
			first_errno = ret;

		if (ret < 0)
			ath11k_warn(ab, "failed to remove peer key %d: %d\n",
				    i, ret);

		spin_lock_bh(&ab->base_lock);
		peer->keys[i] = NULL;
		spin_unlock_bh(&ab->base_lock);
	}

	return first_errno;
}

static int ath11k_mac_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
				 struct ieee80211_vif *vif, struct ieee80211_sta *sta,
				 struct ieee80211_key_conf *key)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	struct ath11k_peer *peer;
	struct ath11k_sta *arsta;
	const u8 *peer_addr;
	int ret = 0;
	u32 flags = 0;

	/* BIP needs to be done in software */
	if (key->cipher == WLAN_CIPHER_SUITE_AES_CMAC ||
	    key->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_128 ||
	    key->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_256 ||
	    key->cipher == WLAN_CIPHER_SUITE_BIP_CMAC_256)
		return 1;

	if (test_bit(ATH11K_FLAG_HW_CRYPTO_DISABLED, &ar->ab->dev_flags))
		return 1;

	if (key->keyidx > WMI_MAX_KEY_INDEX)
		return -ENOSPC;

	mutex_lock(&ar->conf_mutex);

	if (sta)
		peer_addr = sta->addr;
	else if (arvif->vdev_type == WMI_VDEV_TYPE_STA)
		peer_addr = vif->bss_conf.bssid;
	else
		peer_addr = vif->addr;

	key->hw_key_idx = key->keyidx;

	/* the peer should not disappear in mid-way (unless FW goes awry) since
	 * we already hold conf_mutex. we just make sure its there now.
	 */
	spin_lock_bh(&ab->base_lock);
	peer = ath11k_peer_find(ab, arvif->vdev_id, peer_addr);

	/* flush the fragments cache during key (re)install to
	 * ensure all frags in the new frag list belong to the same key.
	 */
	if (peer && sta && cmd == SET_KEY)
		ath11k_peer_frags_flush(ar, peer);
	spin_unlock_bh(&ab->base_lock);

	if (!peer) {
		if (cmd == SET_KEY) {
			ath11k_warn(ab, "cannot install key for non-existent peer %pM\n",
				    peer_addr);
			ret = -EOPNOTSUPP;
			goto exit;
		} else {
			/* if the peer doesn't exist there is no key to disable
			 * anymore
			 */
			goto exit;
		}
	}

	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
		flags |= WMI_KEY_PAIRWISE;
	else
		flags |= WMI_KEY_GROUP;

	ret = ath11k_install_key(arvif, key, cmd, peer_addr, flags);
	if (ret) {
		ath11k_warn(ab, "ath11k_install_key failed (%d)\n", ret);
		goto exit;
	}

	ret = ath11k_dp_peer_rx_pn_replay_config(arvif, peer_addr, cmd, key);
	if (ret) {
		ath11k_warn(ab, "failed to offload PN replay detection %d\n", ret);
		goto exit;
	}

	spin_lock_bh(&ab->base_lock);
	peer = ath11k_peer_find(ab, arvif->vdev_id, peer_addr);
	if (peer && cmd == SET_KEY) {
		peer->keys[key->keyidx] = key;
		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
			peer->ucast_keyidx = key->keyidx;
			peer->sec_type = ath11k_dp_tx_get_encrypt_type(key->cipher);
		} else {
			peer->mcast_keyidx = key->keyidx;
			peer->sec_type_grp = ath11k_dp_tx_get_encrypt_type(key->cipher);
		}
	} else if (peer && cmd == DISABLE_KEY) {
		peer->keys[key->keyidx] = NULL;
		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
			peer->ucast_keyidx = 0;
		else
			peer->mcast_keyidx = 0;
	} else if (!peer)
		/* impossible unless FW goes crazy */
		ath11k_warn(ab, "peer %pM disappeared!\n", peer_addr);

	if (sta) {
		arsta = (struct ath11k_sta *)sta->drv_priv;

		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_TKIP:
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_CCMP_256:
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
			if (cmd == SET_KEY)
				arsta->pn_type = HAL_PN_TYPE_WPA;
			else
				arsta->pn_type = HAL_PN_TYPE_NONE;
			break;
		default:
			arsta->pn_type = HAL_PN_TYPE_NONE;
			break;
		}
	}

	spin_unlock_bh(&ab->base_lock);

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int
ath11k_mac_bitrate_mask_num_vht_rates(struct ath11k *ar,
				      enum nl80211_band band,
				      const struct cfg80211_bitrate_mask *mask)
{
	int num_rates = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(mask->control[band].vht_mcs); i++)
		num_rates += hweight16(mask->control[band].vht_mcs[i]);

	return num_rates;
}

static int
ath11k_mac_bitrate_mask_num_he_rates(struct ath11k *ar,
				     enum nl80211_band band,
				     const struct cfg80211_bitrate_mask *mask)
{
	int num_rates = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(mask->control[band].he_mcs); i++)
		num_rates += hweight16(mask->control[band].he_mcs[i]);

	return num_rates;
}

static int
ath11k_mac_set_peer_vht_fixed_rate(struct ath11k_vif *arvif,
				   struct ieee80211_sta *sta,
				   const struct cfg80211_bitrate_mask *mask,
				   enum nl80211_band band)
{
	struct ath11k *ar = arvif->ar;
	u8 vht_rate, nss;
	u32 rate_code;
	int ret, i;

	lockdep_assert_held(&ar->conf_mutex);

	nss = 0;

	for (i = 0; i < ARRAY_SIZE(mask->control[band].vht_mcs); i++) {
		if (hweight16(mask->control[band].vht_mcs[i]) == 1) {
			nss = i + 1;
			vht_rate = ffs(mask->control[band].vht_mcs[i]) - 1;
		}
	}

	if (!nss) {
		ath11k_warn(ar->ab, "No single VHT Fixed rate found to set for %pM",
			    sta->addr);
		return -EINVAL;
	}

	/* Avoid updating invalid nss as fixed rate*/
	if (nss > sta->deflink.rx_nss)
		return -EINVAL;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "Setting Fixed VHT Rate for peer %pM. Device will not switch to any other selected rates",
		   sta->addr);

	rate_code = ATH11K_HW_RATE_CODE(vht_rate, nss - 1,
					WMI_RATE_PREAMBLE_VHT);
	ret = ath11k_wmi_set_peer_param(ar, sta->addr,
					arvif->vdev_id,
					WMI_PEER_PARAM_FIXED_RATE,
					rate_code);
	if (ret)
		ath11k_warn(ar->ab,
			    "failed to update STA %pM Fixed Rate %d: %d\n",
			     sta->addr, rate_code, ret);

	return ret;
}

static int
ath11k_mac_set_peer_he_fixed_rate(struct ath11k_vif *arvif,
				  struct ieee80211_sta *sta,
				  const struct cfg80211_bitrate_mask *mask,
				  enum nl80211_band band)
{
	struct ath11k *ar = arvif->ar;
	u8 he_rate, nss;
	u32 rate_code;
	int ret, i;

	lockdep_assert_held(&ar->conf_mutex);

	nss = 0;

	for (i = 0; i < ARRAY_SIZE(mask->control[band].he_mcs); i++) {
		if (hweight16(mask->control[band].he_mcs[i]) == 1) {
			nss = i + 1;
			he_rate = ffs(mask->control[band].he_mcs[i]) - 1;
		}
	}

	if (!nss) {
		ath11k_warn(ar->ab, "No single he fixed rate found to set for %pM",
			    sta->addr);
		return -EINVAL;
	}

	/* Avoid updating invalid nss as fixed rate */
	if (nss > sta->deflink.rx_nss)
		return -EINVAL;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "mac setting fixed he rate for peer %pM, device will not switch to any other selected rates",
		   sta->addr);

	rate_code = ATH11K_HW_RATE_CODE(he_rate, nss - 1,
					WMI_RATE_PREAMBLE_HE);

	ret = ath11k_wmi_set_peer_param(ar, sta->addr,
					arvif->vdev_id,
					WMI_PEER_PARAM_FIXED_RATE,
					rate_code);
	if (ret)
		ath11k_warn(ar->ab,
			    "failed to update sta %pM fixed rate %d: %d\n",
			    sta->addr, rate_code, ret);

	return ret;
}

static int ath11k_station_assoc(struct ath11k *ar,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta,
				bool reassoc)
{
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	struct peer_assoc_params peer_arg;
	int ret = 0;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	struct cfg80211_bitrate_mask *mask;
	u8 num_vht_rates, num_he_rates;

	lockdep_assert_held(&ar->conf_mutex);

	if (WARN_ON(ath11k_mac_vif_chan(vif, &def)))
		return -EPERM;

	band = def.chan->band;
	mask = &arvif->bitrate_mask;

	ath11k_peer_assoc_prepare(ar, vif, sta, &peer_arg, reassoc);

	peer_arg.is_assoc = true;
	ret = ath11k_wmi_send_peer_assoc_cmd(ar, &peer_arg);
	if (ret) {
		ath11k_warn(ar->ab, "failed to run peer assoc for STA %pM vdev %i: %d\n",
			    sta->addr, arvif->vdev_id, ret);
		return ret;
	}

	if (!wait_for_completion_timeout(&ar->peer_assoc_done, 1 * HZ)) {
		ath11k_warn(ar->ab, "failed to get peer assoc conf event for %pM vdev %i\n",
			    sta->addr, arvif->vdev_id);
		return -ETIMEDOUT;
	}

	num_vht_rates = ath11k_mac_bitrate_mask_num_vht_rates(ar, band, mask);
	num_he_rates = ath11k_mac_bitrate_mask_num_he_rates(ar, band, mask);

	/* If single VHT/HE rate is configured (by set_bitrate_mask()),
	 * peer_assoc will disable VHT/HE. This is now enabled by a peer specific
	 * fixed param.
	 * Note that all other rates and NSS will be disabled for this peer.
	 */
	if (sta->deflink.vht_cap.vht_supported && num_vht_rates == 1) {
		ret = ath11k_mac_set_peer_vht_fixed_rate(arvif, sta, mask,
							 band);
		if (ret)
			return ret;
	} else if (sta->deflink.he_cap.has_he && num_he_rates == 1) {
		ret = ath11k_mac_set_peer_he_fixed_rate(arvif, sta, mask,
							band);
		if (ret)
			return ret;
	}

	/* Re-assoc is run only to update supported rates for given station. It
	 * doesn't make much sense to reconfigure the peer completely.
	 */
	if (reassoc)
		return 0;

	ret = ath11k_setup_peer_smps(ar, arvif, sta->addr,
				     &sta->deflink.ht_cap,
				     le16_to_cpu(sta->deflink.he_6ghz_capa.capa));
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup peer SMPS for vdev %d: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	if (!sta->wme) {
		arvif->num_legacy_stations++;
		ret = ath11k_recalc_rtscts_prot(arvif);
		if (ret)
			return ret;
	}

	if (sta->wme && sta->uapsd_queues) {
		ret = ath11k_peer_assoc_qos_ap(ar, arvif, sta);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set qos params for STA %pM for vdev %i: %d\n",
				    sta->addr, arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath11k_station_disassoc(struct ath11k *ar,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta)
{
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	if (!sta->wme) {
		arvif->num_legacy_stations--;
		ret = ath11k_recalc_rtscts_prot(arvif);
		if (ret)
			return ret;
	}

	ret = ath11k_clear_peer_keys(arvif, sta->addr);
	if (ret) {
		ath11k_warn(ar->ab, "failed to clear all peer keys for vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}
	return 0;
}

static void ath11k_sta_rc_update_wk(struct work_struct *wk)
{
	struct ath11k *ar;
	struct ath11k_vif *arvif;
	struct ath11k_sta *arsta;
	struct ieee80211_sta *sta;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	const u8 *ht_mcs_mask;
	const u16 *vht_mcs_mask;
	const u16 *he_mcs_mask;
	u32 changed, bw, nss, smps, bw_prev;
	int err, num_vht_rates, num_he_rates;
	const struct cfg80211_bitrate_mask *mask;
	struct peer_assoc_params peer_arg;
	enum wmi_phy_mode peer_phymode;

	arsta = container_of(wk, struct ath11k_sta, update_wk);
	sta = container_of((void *)arsta, struct ieee80211_sta, drv_priv);
	arvif = arsta->arvif;
	ar = arvif->ar;

	if (WARN_ON(ath11k_mac_vif_chan(arvif->vif, &def)))
		return;

	band = def.chan->band;
	ht_mcs_mask = arvif->bitrate_mask.control[band].ht_mcs;
	vht_mcs_mask = arvif->bitrate_mask.control[band].vht_mcs;
	he_mcs_mask = arvif->bitrate_mask.control[band].he_mcs;

	spin_lock_bh(&ar->data_lock);

	changed = arsta->changed;
	arsta->changed = 0;

	bw = arsta->bw;
	bw_prev = arsta->bw_prev;
	nss = arsta->nss;
	smps = arsta->smps;

	spin_unlock_bh(&ar->data_lock);

	mutex_lock(&ar->conf_mutex);

	nss = max_t(u32, 1, nss);
	nss = min(nss, max(max(ath11k_mac_max_ht_nss(ht_mcs_mask),
			       ath11k_mac_max_vht_nss(vht_mcs_mask)),
			   ath11k_mac_max_he_nss(he_mcs_mask)));

	if (changed & IEEE80211_RC_BW_CHANGED) {
		/* Get the peer phymode */
		ath11k_peer_assoc_h_phymode(ar, arvif->vif, sta, &peer_arg);
		peer_phymode = peer_arg.peer_phymode;

		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac update sta %pM peer bw %d phymode %d\n",
			   sta->addr, bw, peer_phymode);

		if (bw > bw_prev) {
			/* BW is upgraded. In this case we send WMI_PEER_PHYMODE
			 * followed by WMI_PEER_CHWIDTH
			 */
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac BW upgrade for sta %pM new BW %d, old BW %d\n",
				   sta->addr, bw, bw_prev);

			err = ath11k_wmi_set_peer_param(ar, sta->addr, arvif->vdev_id,
							WMI_PEER_PHYMODE, peer_phymode);

			if (err) {
				ath11k_warn(ar->ab, "failed to update STA %pM peer phymode %d: %d\n",
					    sta->addr, peer_phymode, err);
				goto err_rc_bw_changed;
			}

			err = ath11k_wmi_set_peer_param(ar, sta->addr, arvif->vdev_id,
							WMI_PEER_CHWIDTH, bw);

			if (err)
				ath11k_warn(ar->ab, "failed to update STA %pM peer bw %d: %d\n",
					    sta->addr, bw, err);
		} else {
			/* BW is downgraded. In this case we send WMI_PEER_CHWIDTH
			 * followed by WMI_PEER_PHYMODE
			 */
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac BW downgrade for sta %pM new BW %d,old BW %d\n",
				   sta->addr, bw, bw_prev);

			err = ath11k_wmi_set_peer_param(ar, sta->addr, arvif->vdev_id,
							WMI_PEER_CHWIDTH, bw);

			if (err) {
				ath11k_warn(ar->ab, "failed to update STA %pM peer bw %d: %d\n",
					    sta->addr, bw, err);
				goto err_rc_bw_changed;
			}

			err = ath11k_wmi_set_peer_param(ar, sta->addr, arvif->vdev_id,
							WMI_PEER_PHYMODE, peer_phymode);

			if (err)
				ath11k_warn(ar->ab, "failed to update STA %pM peer phymode %d: %d\n",
					    sta->addr, peer_phymode, err);
		}
	}

	if (changed & IEEE80211_RC_NSS_CHANGED) {
		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac update sta %pM nss %d\n",
			   sta->addr, nss);

		err = ath11k_wmi_set_peer_param(ar, sta->addr, arvif->vdev_id,
						WMI_PEER_NSS, nss);
		if (err)
			ath11k_warn(ar->ab, "failed to update STA %pM nss %d: %d\n",
				    sta->addr, nss, err);
	}

	if (changed & IEEE80211_RC_SMPS_CHANGED) {
		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac update sta %pM smps %d\n",
			   sta->addr, smps);

		err = ath11k_wmi_set_peer_param(ar, sta->addr, arvif->vdev_id,
						WMI_PEER_MIMO_PS_STATE, smps);
		if (err)
			ath11k_warn(ar->ab, "failed to update STA %pM smps %d: %d\n",
				    sta->addr, smps, err);
	}

	if (changed & IEEE80211_RC_SUPP_RATES_CHANGED) {
		mask = &arvif->bitrate_mask;
		num_vht_rates = ath11k_mac_bitrate_mask_num_vht_rates(ar, band,
								      mask);
		num_he_rates = ath11k_mac_bitrate_mask_num_he_rates(ar, band,
								    mask);

		/* Peer_assoc_prepare will reject vht rates in
		 * bitrate_mask if its not available in range format and
		 * sets vht tx_rateset as unsupported. So multiple VHT MCS
		 * setting(eg. MCS 4,5,6) per peer is not supported here.
		 * But, Single rate in VHT mask can be set as per-peer
		 * fixed rate. But even if any HT rates are configured in
		 * the bitrate mask, device will not switch to those rates
		 * when per-peer Fixed rate is set.
		 * TODO: Check RATEMASK_CMDID to support auto rates selection
		 * across HT/VHT and for multiple VHT MCS support.
		 */
		if (sta->deflink.vht_cap.vht_supported && num_vht_rates == 1) {
			ath11k_mac_set_peer_vht_fixed_rate(arvif, sta, mask,
							   band);
		} else if (sta->deflink.he_cap.has_he && num_he_rates == 1) {
			ath11k_mac_set_peer_he_fixed_rate(arvif, sta, mask,
							  band);
		} else {
			/* If the peer is non-VHT/HE or no fixed VHT/HE rate
			 * is provided in the new bitrate mask we set the
			 * other rates using peer_assoc command. Also clear
			 * the peer fixed rate settings as it has higher proprity
			 * than peer assoc
			 */
			err = ath11k_wmi_set_peer_param(ar, sta->addr,
							arvif->vdev_id,
							WMI_PEER_PARAM_FIXED_RATE,
							WMI_FIXED_RATE_NONE);
			if (err)
				ath11k_warn(ar->ab,
					    "failed to disable peer fixed rate for sta %pM: %d\n",
					    sta->addr, err);

			ath11k_peer_assoc_prepare(ar, arvif->vif, sta,
						  &peer_arg, true);

			peer_arg.is_assoc = false;
			err = ath11k_wmi_send_peer_assoc_cmd(ar, &peer_arg);
			if (err)
				ath11k_warn(ar->ab, "failed to run peer assoc for STA %pM vdev %i: %d\n",
					    sta->addr, arvif->vdev_id, err);

			if (!wait_for_completion_timeout(&ar->peer_assoc_done, 1 * HZ))
				ath11k_warn(ar->ab, "failed to get peer assoc conf event for %pM vdev %i\n",
					    sta->addr, arvif->vdev_id);
		}
	}

err_rc_bw_changed:
	mutex_unlock(&ar->conf_mutex);
}

static void ath11k_sta_set_4addr_wk(struct work_struct *wk)
{
	struct ath11k *ar;
	struct ath11k_vif *arvif;
	struct ath11k_sta *arsta;
	struct ieee80211_sta *sta;
	int ret = 0;

	arsta = container_of(wk, struct ath11k_sta, set_4addr_wk);
	sta = container_of((void *)arsta, struct ieee80211_sta, drv_priv);
	arvif = arsta->arvif;
	ar = arvif->ar;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "setting USE_4ADDR for peer %pM\n", sta->addr);

	ret = ath11k_wmi_set_peer_param(ar, sta->addr,
					arvif->vdev_id,
					WMI_PEER_USE_4ADDR, 1);

	if (ret)
		ath11k_warn(ar->ab, "failed to set peer %pM 4addr capability: %d\n",
			    sta->addr, ret);
}

static int ath11k_mac_inc_num_stations(struct ath11k_vif *arvif,
				       struct ieee80211_sta *sta)
{
	struct ath11k *ar = arvif->ar;

	lockdep_assert_held(&ar->conf_mutex);

	if (arvif->vdev_type == WMI_VDEV_TYPE_STA && !sta->tdls)
		return 0;

	if (ar->num_stations >= ar->max_num_stations)
		return -ENOBUFS;

	ar->num_stations++;

	return 0;
}

static void ath11k_mac_dec_num_stations(struct ath11k_vif *arvif,
					struct ieee80211_sta *sta)
{
	struct ath11k *ar = arvif->ar;

	lockdep_assert_held(&ar->conf_mutex);

	if (arvif->vdev_type == WMI_VDEV_TYPE_STA && !sta->tdls)
		return;

	ar->num_stations--;
}

static int ath11k_mac_station_add(struct ath11k *ar,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta)
{
	struct ath11k_base *ab = ar->ab;
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	struct ath11k_sta *arsta = (struct ath11k_sta *)sta->drv_priv;
	struct peer_create_params peer_param;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath11k_mac_inc_num_stations(arvif, sta);
	if (ret) {
		ath11k_warn(ab, "refusing to associate station: too many connected already (%d)\n",
			    ar->max_num_stations);
		goto exit;
	}

	arsta->rx_stats = kzalloc(sizeof(*arsta->rx_stats), GFP_KERNEL);
	if (!arsta->rx_stats) {
		ret = -ENOMEM;
		goto dec_num_station;
	}

	peer_param.vdev_id = arvif->vdev_id;
	peer_param.peer_addr = sta->addr;
	peer_param.peer_type = WMI_PEER_TYPE_DEFAULT;

	ret = ath11k_peer_create(ar, arvif, sta, &peer_param);
	if (ret) {
		ath11k_warn(ab, "Failed to add peer: %pM for VDEV: %d\n",
			    sta->addr, arvif->vdev_id);
		goto free_rx_stats;
	}

	ath11k_dbg(ab, ATH11K_DBG_MAC, "Added peer: %pM for VDEV: %d\n",
		   sta->addr, arvif->vdev_id);

	if (ath11k_debugfs_is_extd_tx_stats_enabled(ar)) {
		arsta->tx_stats = kzalloc(sizeof(*arsta->tx_stats), GFP_KERNEL);
		if (!arsta->tx_stats) {
			ret = -ENOMEM;
			goto free_peer;
		}
	}

	if (ieee80211_vif_is_mesh(vif)) {
		ath11k_dbg(ab, ATH11K_DBG_MAC,
			   "setting USE_4ADDR for mesh STA %pM\n", sta->addr);
		ret = ath11k_wmi_set_peer_param(ar, sta->addr,
						arvif->vdev_id,
						WMI_PEER_USE_4ADDR, 1);
		if (ret) {
			ath11k_warn(ab, "failed to set mesh STA %pM 4addr capability: %d\n",
				    sta->addr, ret);
			goto free_tx_stats;
		}
	}

	ret = ath11k_dp_peer_setup(ar, arvif->vdev_id, sta->addr);
	if (ret) {
		ath11k_warn(ab, "failed to setup dp for peer %pM on vdev %i (%d)\n",
			    sta->addr, arvif->vdev_id, ret);
		goto free_tx_stats;
	}

	if (ab->hw_params.vdev_start_delay &&
	    !arvif->is_started &&
	    arvif->vdev_type != WMI_VDEV_TYPE_AP) {
		ret = ath11k_start_vdev_delay(ar->hw, vif);
		if (ret) {
			ath11k_warn(ab, "failed to delay vdev start: %d\n", ret);
			goto free_tx_stats;
		}
	}

	ewma_avg_rssi_init(&arsta->avg_rssi);
	return 0;

free_tx_stats:
	kfree(arsta->tx_stats);
	arsta->tx_stats = NULL;
free_peer:
	ath11k_peer_delete(ar, arvif->vdev_id, sta->addr);
free_rx_stats:
	kfree(arsta->rx_stats);
	arsta->rx_stats = NULL;
dec_num_station:
	ath11k_mac_dec_num_stations(arvif, sta);
exit:
	return ret;
}

static u32 ath11k_mac_ieee80211_sta_bw_to_wmi(struct ath11k *ar,
					      struct ieee80211_sta *sta)
{
	u32 bw = WMI_PEER_CHWIDTH_20MHZ;

	switch (sta->deflink.bandwidth) {
	case IEEE80211_STA_RX_BW_20:
		bw = WMI_PEER_CHWIDTH_20MHZ;
		break;
	case IEEE80211_STA_RX_BW_40:
		bw = WMI_PEER_CHWIDTH_40MHZ;
		break;
	case IEEE80211_STA_RX_BW_80:
		bw = WMI_PEER_CHWIDTH_80MHZ;
		break;
	case IEEE80211_STA_RX_BW_160:
		bw = WMI_PEER_CHWIDTH_160MHZ;
		break;
	default:
		ath11k_warn(ar->ab, "Invalid bandwidth %d for %pM\n",
			    sta->deflink.bandwidth, sta->addr);
		bw = WMI_PEER_CHWIDTH_20MHZ;
		break;
	}

	return bw;
}

static int ath11k_mac_op_sta_state(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   enum ieee80211_sta_state old_state,
				   enum ieee80211_sta_state new_state)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	struct ath11k_sta *arsta = (struct ath11k_sta *)sta->drv_priv;
	struct ath11k_peer *peer;
	int ret = 0;

	/* cancel must be done outside the mutex to avoid deadlock */
	if ((old_state == IEEE80211_STA_NONE &&
	     new_state == IEEE80211_STA_NOTEXIST)) {
		cancel_work_sync(&arsta->update_wk);
		cancel_work_sync(&arsta->set_4addr_wk);
	}

	mutex_lock(&ar->conf_mutex);

	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE) {
		memset(arsta, 0, sizeof(*arsta));
		arsta->arvif = arvif;
		arsta->peer_ps_state = WMI_PEER_PS_STATE_DISABLED;
		INIT_WORK(&arsta->update_wk, ath11k_sta_rc_update_wk);
		INIT_WORK(&arsta->set_4addr_wk, ath11k_sta_set_4addr_wk);

		ret = ath11k_mac_station_add(ar, vif, sta);
		if (ret)
			ath11k_warn(ar->ab, "Failed to add station: %pM for VDEV: %d\n",
				    sta->addr, arvif->vdev_id);
	} else if ((old_state == IEEE80211_STA_NONE &&
		    new_state == IEEE80211_STA_NOTEXIST)) {
		bool skip_peer_delete = ar->ab->hw_params.vdev_start_delay &&
			vif->type == NL80211_IFTYPE_STATION;

		ath11k_dp_peer_cleanup(ar, arvif->vdev_id, sta->addr);

		if (!skip_peer_delete) {
			ret = ath11k_peer_delete(ar, arvif->vdev_id, sta->addr);
			if (ret)
				ath11k_warn(ar->ab,
					    "Failed to delete peer: %pM for VDEV: %d\n",
					    sta->addr, arvif->vdev_id);
			else
				ath11k_dbg(ar->ab,
					   ATH11K_DBG_MAC,
					   "Removed peer: %pM for VDEV: %d\n",
					   sta->addr, arvif->vdev_id);
		}

		ath11k_mac_dec_num_stations(arvif, sta);
		mutex_lock(&ar->ab->tbl_mtx_lock);
		spin_lock_bh(&ar->ab->base_lock);
		peer = ath11k_peer_find(ar->ab, arvif->vdev_id, sta->addr);
		if (skip_peer_delete && peer) {
			peer->sta = NULL;
		} else if (peer && peer->sta == sta) {
			ath11k_warn(ar->ab, "Found peer entry %pM n vdev %i after it was supposedly removed\n",
				    vif->addr, arvif->vdev_id);
			ath11k_peer_rhash_delete(ar->ab, peer);
			peer->sta = NULL;
			list_del(&peer->list);
			kfree(peer);
			ar->num_peers--;
		}
		spin_unlock_bh(&ar->ab->base_lock);
		mutex_unlock(&ar->ab->tbl_mtx_lock);

		kfree(arsta->tx_stats);
		arsta->tx_stats = NULL;

		kfree(arsta->rx_stats);
		arsta->rx_stats = NULL;
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_ASSOC &&
		   (vif->type == NL80211_IFTYPE_AP ||
		    vif->type == NL80211_IFTYPE_MESH_POINT ||
		    vif->type == NL80211_IFTYPE_ADHOC)) {
		ret = ath11k_station_assoc(ar, vif, sta, false);
		if (ret)
			ath11k_warn(ar->ab, "Failed to associate station: %pM\n",
				    sta->addr);

		spin_lock_bh(&ar->data_lock);
		/* Set arsta bw and prev bw */
		arsta->bw = ath11k_mac_ieee80211_sta_bw_to_wmi(ar, sta);
		arsta->bw_prev = arsta->bw;
		spin_unlock_bh(&ar->data_lock);
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTHORIZED) {
		spin_lock_bh(&ar->ab->base_lock);

		peer = ath11k_peer_find(ar->ab, arvif->vdev_id, sta->addr);
		if (peer)
			peer->is_authorized = true;

		spin_unlock_bh(&ar->ab->base_lock);

		if (vif->type == NL80211_IFTYPE_STATION && arvif->is_up) {
			ret = ath11k_wmi_set_peer_param(ar, sta->addr,
							arvif->vdev_id,
							WMI_PEER_AUTHORIZE,
							1);
			if (ret)
				ath11k_warn(ar->ab, "Unable to authorize peer %pM vdev %d: %d\n",
					    sta->addr, arvif->vdev_id, ret);
		}
	} else if (old_state == IEEE80211_STA_AUTHORIZED &&
		   new_state == IEEE80211_STA_ASSOC) {
		spin_lock_bh(&ar->ab->base_lock);

		peer = ath11k_peer_find(ar->ab, arvif->vdev_id, sta->addr);
		if (peer)
			peer->is_authorized = false;

		spin_unlock_bh(&ar->ab->base_lock);
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTH &&
		   (vif->type == NL80211_IFTYPE_AP ||
		    vif->type == NL80211_IFTYPE_MESH_POINT ||
		    vif->type == NL80211_IFTYPE_ADHOC)) {
		ret = ath11k_station_disassoc(ar, vif, sta);
		if (ret)
			ath11k_warn(ar->ab, "Failed to disassociate station: %pM\n",
				    sta->addr);
	}

	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath11k_mac_op_sta_set_txpwr(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	int ret = 0;
	s16 txpwr;

	if (sta->deflink.txpwr.type == NL80211_TX_POWER_AUTOMATIC) {
		txpwr = 0;
	} else {
		txpwr = sta->deflink.txpwr.power;
		if (!txpwr)
			return -EINVAL;
	}

	if (txpwr > ATH11K_TX_POWER_MAX_VAL || txpwr < ATH11K_TX_POWER_MIN_VAL)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	ret = ath11k_wmi_set_peer_param(ar, sta->addr, arvif->vdev_id,
					WMI_PEER_USE_FIXED_PWR, txpwr);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set tx power for station ret: %d\n",
			    ret);
		goto out;
	}

out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static void ath11k_mac_op_sta_set_4addr(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta, bool enabled)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_sta *arsta = (struct ath11k_sta *)sta->drv_priv;

	if (enabled && !arsta->use_4addr_set) {
		ieee80211_queue_work(ar->hw, &arsta->set_4addr_wk);
		arsta->use_4addr_set = true;
	}
}

static void ath11k_mac_op_sta_rc_update(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta,
					u32 changed)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_sta *arsta = (struct ath11k_sta *)sta->drv_priv;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct ath11k_peer *peer;
	u32 bw, smps;

	spin_lock_bh(&ar->ab->base_lock);

	peer = ath11k_peer_find(ar->ab, arvif->vdev_id, sta->addr);
	if (!peer) {
		spin_unlock_bh(&ar->ab->base_lock);
		ath11k_warn(ar->ab, "mac sta rc update failed to find peer %pM on vdev %i\n",
			    sta->addr, arvif->vdev_id);
		return;
	}

	spin_unlock_bh(&ar->ab->base_lock);

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "mac sta rc update for %pM changed %08x bw %d nss %d smps %d\n",
		   sta->addr, changed, sta->deflink.bandwidth,
		   sta->deflink.rx_nss,
		   sta->deflink.smps_mode);

	spin_lock_bh(&ar->data_lock);

	if (changed & IEEE80211_RC_BW_CHANGED) {
		bw = ath11k_mac_ieee80211_sta_bw_to_wmi(ar, sta);
		arsta->bw_prev = arsta->bw;
		arsta->bw = bw;
	}

	if (changed & IEEE80211_RC_NSS_CHANGED)
		arsta->nss = sta->deflink.rx_nss;

	if (changed & IEEE80211_RC_SMPS_CHANGED) {
		smps = WMI_PEER_SMPS_PS_NONE;

		switch (sta->deflink.smps_mode) {
		case IEEE80211_SMPS_AUTOMATIC:
		case IEEE80211_SMPS_OFF:
			smps = WMI_PEER_SMPS_PS_NONE;
			break;
		case IEEE80211_SMPS_STATIC:
			smps = WMI_PEER_SMPS_STATIC;
			break;
		case IEEE80211_SMPS_DYNAMIC:
			smps = WMI_PEER_SMPS_DYNAMIC;
			break;
		default:
			ath11k_warn(ar->ab, "Invalid smps %d in sta rc update for %pM\n",
				    sta->deflink.smps_mode, sta->addr);
			smps = WMI_PEER_SMPS_PS_NONE;
			break;
		}

		arsta->smps = smps;
	}

	arsta->changed |= changed;

	spin_unlock_bh(&ar->data_lock);

	ieee80211_queue_work(hw, &arsta->update_wk);
}

static int ath11k_conf_tx_uapsd(struct ath11k *ar, struct ieee80211_vif *vif,
				u16 ac, bool enable)
{
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	u32 value = 0;
	int ret = 0;

	if (arvif->vdev_type != WMI_VDEV_TYPE_STA)
		return 0;

	switch (ac) {
	case IEEE80211_AC_VO:
		value = WMI_STA_PS_UAPSD_AC3_DELIVERY_EN |
			WMI_STA_PS_UAPSD_AC3_TRIGGER_EN;
		break;
	case IEEE80211_AC_VI:
		value = WMI_STA_PS_UAPSD_AC2_DELIVERY_EN |
			WMI_STA_PS_UAPSD_AC2_TRIGGER_EN;
		break;
	case IEEE80211_AC_BE:
		value = WMI_STA_PS_UAPSD_AC1_DELIVERY_EN |
			WMI_STA_PS_UAPSD_AC1_TRIGGER_EN;
		break;
	case IEEE80211_AC_BK:
		value = WMI_STA_PS_UAPSD_AC0_DELIVERY_EN |
			WMI_STA_PS_UAPSD_AC0_TRIGGER_EN;
		break;
	}

	if (enable)
		arvif->u.sta.uapsd |= value;
	else
		arvif->u.sta.uapsd &= ~value;

	ret = ath11k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
					  WMI_STA_PS_PARAM_UAPSD,
					  arvif->u.sta.uapsd);
	if (ret) {
		ath11k_warn(ar->ab, "could not set uapsd params %d\n", ret);
		goto exit;
	}

	if (arvif->u.sta.uapsd)
		value = WMI_STA_PS_RX_WAKE_POLICY_POLL_UAPSD;
	else
		value = WMI_STA_PS_RX_WAKE_POLICY_WAKE;

	ret = ath11k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
					  WMI_STA_PS_PARAM_RX_WAKE_POLICY,
					  value);
	if (ret)
		ath11k_warn(ar->ab, "could not set rx wake param %d\n", ret);

exit:
	return ret;
}

static int ath11k_mac_op_conf_tx(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 unsigned int link_id, u16 ac,
				 const struct ieee80211_tx_queue_params *params)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct wmi_wmm_params_arg *p = NULL;
	int ret;

	mutex_lock(&ar->conf_mutex);

	switch (ac) {
	case IEEE80211_AC_VO:
		p = &arvif->wmm_params.ac_vo;
		break;
	case IEEE80211_AC_VI:
		p = &arvif->wmm_params.ac_vi;
		break;
	case IEEE80211_AC_BE:
		p = &arvif->wmm_params.ac_be;
		break;
	case IEEE80211_AC_BK:
		p = &arvif->wmm_params.ac_bk;
		break;
	}

	if (WARN_ON(!p)) {
		ret = -EINVAL;
		goto exit;
	}

	p->cwmin = params->cw_min;
	p->cwmax = params->cw_max;
	p->aifs = params->aifs;
	p->txop = params->txop;

	ret = ath11k_wmi_send_wmm_update_cmd_tlv(ar, arvif->vdev_id,
						 &arvif->wmm_params);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set wmm params: %d\n", ret);
		goto exit;
	}

	ret = ath11k_conf_tx_uapsd(ar, vif, ac, params->uapsd);

	if (ret)
		ath11k_warn(ar->ab, "failed to set sta uapsd: %d\n", ret);

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static struct ieee80211_sta_ht_cap
ath11k_create_ht_cap(struct ath11k *ar, u32 ar_ht_cap, u32 rate_cap_rx_chainmask)
{
	int i;
	struct ieee80211_sta_ht_cap ht_cap = {0};
	u32 ar_vht_cap = ar->pdev->cap.vht_cap;

	if (!(ar_ht_cap & WMI_HT_CAP_ENABLED))
		return ht_cap;

	ht_cap.ht_supported = 1;
	ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE;
	ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	ht_cap.cap |= IEEE80211_HT_CAP_DSSSCCK40;
	ht_cap.cap |= WLAN_HT_CAP_SM_PS_STATIC << IEEE80211_HT_CAP_SM_PS_SHIFT;

	if (ar_ht_cap & WMI_HT_CAP_HT20_SGI)
		ht_cap.cap |= IEEE80211_HT_CAP_SGI_20;

	if (ar_ht_cap & WMI_HT_CAP_HT40_SGI)
		ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;

	if (ar_ht_cap & WMI_HT_CAP_DYNAMIC_SMPS) {
		u32 smps;

		smps   = WLAN_HT_CAP_SM_PS_DYNAMIC;
		smps <<= IEEE80211_HT_CAP_SM_PS_SHIFT;

		ht_cap.cap |= smps;
	}

	if (ar_ht_cap & WMI_HT_CAP_TX_STBC)
		ht_cap.cap |= IEEE80211_HT_CAP_TX_STBC;

	if (ar_ht_cap & WMI_HT_CAP_RX_STBC) {
		u32 stbc;

		stbc   = ar_ht_cap;
		stbc  &= WMI_HT_CAP_RX_STBC;
		stbc >>= WMI_HT_CAP_RX_STBC_MASK_SHIFT;
		stbc <<= IEEE80211_HT_CAP_RX_STBC_SHIFT;
		stbc  &= IEEE80211_HT_CAP_RX_STBC;

		ht_cap.cap |= stbc;
	}

	if (ar_ht_cap & WMI_HT_CAP_RX_LDPC)
		ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;

	if (ar_ht_cap & WMI_HT_CAP_L_SIG_TXOP_PROT)
		ht_cap.cap |= IEEE80211_HT_CAP_LSIG_TXOP_PROT;

	if (ar_vht_cap & WMI_VHT_CAP_MAX_MPDU_LEN_MASK)
		ht_cap.cap |= IEEE80211_HT_CAP_MAX_AMSDU;

	for (i = 0; i < ar->num_rx_chains; i++) {
		if (rate_cap_rx_chainmask & BIT(i))
			ht_cap.mcs.rx_mask[i] = 0xFF;
	}

	ht_cap.mcs.tx_params |= IEEE80211_HT_MCS_TX_DEFINED;

	return ht_cap;
}

static int ath11k_mac_set_txbf_conf(struct ath11k_vif *arvif)
{
	u32 value = 0;
	struct ath11k *ar = arvif->ar;
	int nsts;
	int sound_dim;
	u32 vht_cap = ar->pdev->cap.vht_cap;
	u32 vdev_param = WMI_VDEV_PARAM_TXBF;

	if (vht_cap & (IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE)) {
		nsts = vht_cap & IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK;
		nsts >>= IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
		if (nsts > (ar->num_rx_chains - 1))
			nsts = ar->num_rx_chains - 1;
		value |= SM(nsts, WMI_TXBF_STS_CAP_OFFSET);
	}

	if (vht_cap & (IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE)) {
		sound_dim = vht_cap &
			    IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK;
		sound_dim >>= IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
		if (sound_dim > (ar->num_tx_chains - 1))
			sound_dim = ar->num_tx_chains - 1;
		value |= SM(sound_dim, WMI_BF_SOUND_DIM_OFFSET);
	}

	if (!value)
		return 0;

	if (vht_cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE) {
		value |= WMI_VDEV_PARAM_TXBF_SU_TX_BFER;

		if ((vht_cap & IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE) &&
		    arvif->vdev_type == WMI_VDEV_TYPE_AP)
			value |= WMI_VDEV_PARAM_TXBF_MU_TX_BFER;
	}

	/* TODO: SUBFEE not validated in HK, disable here until validated? */

	if (vht_cap & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE) {
		value |= WMI_VDEV_PARAM_TXBF_SU_TX_BFEE;

		if ((vht_cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE) &&
		    arvif->vdev_type == WMI_VDEV_TYPE_STA)
			value |= WMI_VDEV_PARAM_TXBF_MU_TX_BFEE;
	}

	return ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					     vdev_param, value);
}

static void ath11k_set_vht_txbf_cap(struct ath11k *ar, u32 *vht_cap)
{
	bool subfer, subfee;
	int sound_dim = 0, nsts = 0;

	subfer = !!(*vht_cap & (IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE));
	subfee = !!(*vht_cap & (IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE));

	if (ar->num_tx_chains < 2) {
		*vht_cap &= ~(IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE);
		subfer = false;
	}

	if (ar->num_rx_chains < 2) {
		*vht_cap &= ~(IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE);
		subfee = false;
	}

	/* If SU Beaformer is not set, then disable MU Beamformer Capability */
	if (!subfer)
		*vht_cap &= ~(IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE);

	/* If SU Beaformee is not set, then disable MU Beamformee Capability */
	if (!subfee)
		*vht_cap &= ~(IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE);

	sound_dim = (*vht_cap & IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK);
	sound_dim >>= IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
	*vht_cap &= ~IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK;

	nsts = (*vht_cap & IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK);
	nsts >>= IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
	*vht_cap &= ~IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK;

	/* Enable Sounding Dimension Field only if SU BF is enabled */
	if (subfer) {
		if (sound_dim > (ar->num_tx_chains - 1))
			sound_dim = ar->num_tx_chains - 1;

		sound_dim <<= IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
		sound_dim &=  IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK;
		*vht_cap |= sound_dim;
	}

	/* Enable Beamformee STS Field only if SU BF is enabled */
	if (subfee) {
		if (nsts > (ar->num_rx_chains - 1))
			nsts = ar->num_rx_chains - 1;

		nsts <<= IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
		nsts &=  IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK;
		*vht_cap |= nsts;
	}
}

static struct ieee80211_sta_vht_cap
ath11k_create_vht_cap(struct ath11k *ar, u32 rate_cap_tx_chainmask,
		      u32 rate_cap_rx_chainmask)
{
	struct ieee80211_sta_vht_cap vht_cap = {0};
	u16 txmcs_map, rxmcs_map;
	int i;

	vht_cap.vht_supported = 1;
	vht_cap.cap = ar->pdev->cap.vht_cap;

	if (ar->pdev->cap.nss_ratio_enabled)
		vht_cap.vht_mcs.tx_highest |=
			cpu_to_le16(IEEE80211_VHT_EXT_NSS_BW_CAPABLE);

	ath11k_set_vht_txbf_cap(ar, &vht_cap.cap);

	rxmcs_map = 0;
	txmcs_map = 0;
	for (i = 0; i < 8; i++) {
		if (i < ar->num_tx_chains && rate_cap_tx_chainmask & BIT(i))
			txmcs_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << (i * 2);
		else
			txmcs_map |= IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2);

		if (i < ar->num_rx_chains && rate_cap_rx_chainmask & BIT(i))
			rxmcs_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << (i * 2);
		else
			rxmcs_map |= IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2);
	}

	if (rate_cap_tx_chainmask <= 1)
		vht_cap.cap &= ~IEEE80211_VHT_CAP_TXSTBC;

	vht_cap.vht_mcs.rx_mcs_map = cpu_to_le16(rxmcs_map);
	vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(txmcs_map);

	return vht_cap;
}

static void ath11k_mac_setup_ht_vht_cap(struct ath11k *ar,
					struct ath11k_pdev_cap *cap,
					u32 *ht_cap_info)
{
	struct ieee80211_supported_band *band;
	u32 rate_cap_tx_chainmask;
	u32 rate_cap_rx_chainmask;
	u32 ht_cap;

	rate_cap_tx_chainmask = ar->cfg_tx_chainmask >> cap->tx_chain_mask_shift;
	rate_cap_rx_chainmask = ar->cfg_rx_chainmask >> cap->rx_chain_mask_shift;

	if (cap->supported_bands & WMI_HOST_WLAN_2G_CAP) {
		band = &ar->mac.sbands[NL80211_BAND_2GHZ];
		ht_cap = cap->band[NL80211_BAND_2GHZ].ht_cap_info;
		if (ht_cap_info)
			*ht_cap_info = ht_cap;
		band->ht_cap = ath11k_create_ht_cap(ar, ht_cap,
						    rate_cap_rx_chainmask);
	}

	if (cap->supported_bands & WMI_HOST_WLAN_5G_CAP &&
	    (ar->ab->hw_params.single_pdev_only ||
	     !ar->supports_6ghz)) {
		band = &ar->mac.sbands[NL80211_BAND_5GHZ];
		ht_cap = cap->band[NL80211_BAND_5GHZ].ht_cap_info;
		if (ht_cap_info)
			*ht_cap_info = ht_cap;
		band->ht_cap = ath11k_create_ht_cap(ar, ht_cap,
						    rate_cap_rx_chainmask);
		band->vht_cap = ath11k_create_vht_cap(ar, rate_cap_tx_chainmask,
						      rate_cap_rx_chainmask);
	}
}

static int ath11k_check_chain_mask(struct ath11k *ar, u32 ant, bool is_tx_ant)
{
	/* TODO: Check the request chainmask against the supported
	 * chainmask table which is advertised in extented_service_ready event
	 */

	return 0;
}

static void ath11k_gen_ppe_thresh(struct ath11k_ppe_threshold *fw_ppet,
				  u8 *he_ppet)
{
	int nss, ru;
	u8 bit = 7;

	he_ppet[0] = fw_ppet->numss_m1 & IEEE80211_PPE_THRES_NSS_MASK;
	he_ppet[0] |= (fw_ppet->ru_bit_mask <<
		       IEEE80211_PPE_THRES_RU_INDEX_BITMASK_POS) &
		      IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK;
	for (nss = 0; nss <= fw_ppet->numss_m1; nss++) {
		for (ru = 0; ru < 4; ru++) {
			u8 val;
			int i;

			if ((fw_ppet->ru_bit_mask & BIT(ru)) == 0)
				continue;
			val = (fw_ppet->ppet16_ppet8_ru3_ru0[nss] >> (ru * 6)) &
			       0x3f;
			val = ((val >> 3) & 0x7) | ((val & 0x7) << 3);
			for (i = 5; i >= 0; i--) {
				he_ppet[bit / 8] |=
					((val >> i) & 0x1) << ((bit % 8));
				bit++;
			}
		}
	}
}

static void
ath11k_mac_filter_he_cap_mesh(struct ieee80211_he_cap_elem *he_cap_elem)
{
	u8 m;

	m = IEEE80211_HE_MAC_CAP0_TWT_RES |
	    IEEE80211_HE_MAC_CAP0_TWT_REQ;
	he_cap_elem->mac_cap_info[0] &= ~m;

	m = IEEE80211_HE_MAC_CAP2_TRS |
	    IEEE80211_HE_MAC_CAP2_BCAST_TWT |
	    IEEE80211_HE_MAC_CAP2_MU_CASCADING;
	he_cap_elem->mac_cap_info[2] &= ~m;

	m = IEEE80211_HE_MAC_CAP3_FLEX_TWT_SCHED |
	    IEEE80211_HE_MAC_CAP2_BCAST_TWT |
	    IEEE80211_HE_MAC_CAP2_MU_CASCADING;
	he_cap_elem->mac_cap_info[3] &= ~m;

	m = IEEE80211_HE_MAC_CAP4_BSRP_BQRP_A_MPDU_AGG |
	    IEEE80211_HE_MAC_CAP4_BQR;
	he_cap_elem->mac_cap_info[4] &= ~m;

	m = IEEE80211_HE_MAC_CAP5_SUBCHAN_SELECTIVE_TRANSMISSION |
	    IEEE80211_HE_MAC_CAP5_UL_2x996_TONE_RU |
	    IEEE80211_HE_MAC_CAP5_PUNCTURED_SOUNDING |
	    IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX;
	he_cap_elem->mac_cap_info[5] &= ~m;

	m = IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
	    IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO;
	he_cap_elem->phy_cap_info[2] &= ~m;

	m = IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU |
	    IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK |
	    IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_MASK;
	he_cap_elem->phy_cap_info[3] &= ~m;

	m = IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER;
	he_cap_elem->phy_cap_info[4] &= ~m;

	m = IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK;
	he_cap_elem->phy_cap_info[5] &= ~m;

	m = IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
	    IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB |
	    IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB |
	    IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO;
	he_cap_elem->phy_cap_info[6] &= ~m;

	m = IEEE80211_HE_PHY_CAP7_PSR_BASED_SR |
	    IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP |
	    IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ |
	    IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ;
	he_cap_elem->phy_cap_info[7] &= ~m;

	m = IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
	    IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
	    IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU |
	    IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU;
	he_cap_elem->phy_cap_info[8] &= ~m;

	m = IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM |
	    IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK |
	    IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU |
	    IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU |
	    IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
	    IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB;
	he_cap_elem->phy_cap_info[9] &= ~m;
}

static __le16 ath11k_mac_setup_he_6ghz_cap(struct ath11k_pdev_cap *pcap,
					   struct ath11k_band_cap *bcap)
{
	u8 val;

	bcap->he_6ghz_capa = IEEE80211_HT_MPDU_DENSITY_NONE;
	if (bcap->ht_cap_info & WMI_HT_CAP_DYNAMIC_SMPS)
		bcap->he_6ghz_capa |=
			FIELD_PREP(IEEE80211_HE_6GHZ_CAP_SM_PS,
				   WLAN_HT_CAP_SM_PS_DYNAMIC);
	else
		bcap->he_6ghz_capa |=
			FIELD_PREP(IEEE80211_HE_6GHZ_CAP_SM_PS,
				   WLAN_HT_CAP_SM_PS_DISABLED);
	val = FIELD_GET(IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK,
			pcap->vht_cap);
	bcap->he_6ghz_capa |=
		FIELD_PREP(IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP, val);
	val = FIELD_GET(IEEE80211_VHT_CAP_MAX_MPDU_MASK, pcap->vht_cap);
	bcap->he_6ghz_capa |=
		FIELD_PREP(IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN, val);
	if (pcap->vht_cap & IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN)
		bcap->he_6ghz_capa |= IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS;
	if (pcap->vht_cap & IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN)
		bcap->he_6ghz_capa |= IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS;

	return cpu_to_le16(bcap->he_6ghz_capa);
}

static void ath11k_mac_set_hemcsmap(struct ath11k *ar,
				    struct ath11k_pdev_cap *cap,
				    struct ieee80211_sta_he_cap *he_cap,
				    int band)
{
	u16 txmcs_map, rxmcs_map;
	u32 i;

	rxmcs_map = 0;
	txmcs_map = 0;
	for (i = 0; i < 8; i++) {
		if (i < ar->num_tx_chains &&
		    (ar->cfg_tx_chainmask >> cap->tx_chain_mask_shift) & BIT(i))
			txmcs_map |= IEEE80211_HE_MCS_SUPPORT_0_11 << (i * 2);
		else
			txmcs_map |= IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2);

		if (i < ar->num_rx_chains &&
		    (ar->cfg_rx_chainmask >> cap->tx_chain_mask_shift) & BIT(i))
			rxmcs_map |= IEEE80211_HE_MCS_SUPPORT_0_11 << (i * 2);
		else
			rxmcs_map |= IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2);
	}
	he_cap->he_mcs_nss_supp.rx_mcs_80 =
		cpu_to_le16(rxmcs_map & 0xffff);
	he_cap->he_mcs_nss_supp.tx_mcs_80 =
		cpu_to_le16(txmcs_map & 0xffff);
	he_cap->he_mcs_nss_supp.rx_mcs_160 =
		cpu_to_le16(rxmcs_map & 0xffff);
	he_cap->he_mcs_nss_supp.tx_mcs_160 =
		cpu_to_le16(txmcs_map & 0xffff);
	he_cap->he_mcs_nss_supp.rx_mcs_80p80 =
		cpu_to_le16(rxmcs_map & 0xffff);
	he_cap->he_mcs_nss_supp.tx_mcs_80p80 =
		cpu_to_le16(txmcs_map & 0xffff);
}

static int ath11k_mac_copy_he_cap(struct ath11k *ar,
				  struct ath11k_pdev_cap *cap,
				  struct ieee80211_sband_iftype_data *data,
				  int band)
{
	int i, idx = 0;

	for (i = 0; i < NUM_NL80211_IFTYPES; i++) {
		struct ieee80211_sta_he_cap *he_cap = &data[idx].he_cap;
		struct ath11k_band_cap *band_cap = &cap->band[band];
		struct ieee80211_he_cap_elem *he_cap_elem =
				&he_cap->he_cap_elem;

		switch (i) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_MESH_POINT:
			break;

		default:
			continue;
		}

		data[idx].types_mask = BIT(i);
		he_cap->has_he = true;
		memcpy(he_cap_elem->mac_cap_info, band_cap->he_cap_info,
		       sizeof(he_cap_elem->mac_cap_info));
		memcpy(he_cap_elem->phy_cap_info, band_cap->he_cap_phy_info,
		       sizeof(he_cap_elem->phy_cap_info));

		he_cap_elem->mac_cap_info[1] &=
			IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_MASK;

		he_cap_elem->phy_cap_info[5] &=
			~IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK;
		he_cap_elem->phy_cap_info[5] |= ar->num_tx_chains - 1;

		switch (i) {
		case NL80211_IFTYPE_AP:
			he_cap_elem->phy_cap_info[3] &=
				~IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK;
			he_cap_elem->phy_cap_info[9] |=
				IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU;
			break;
		case NL80211_IFTYPE_STATION:
			he_cap_elem->mac_cap_info[0] &=
				~IEEE80211_HE_MAC_CAP0_TWT_RES;
			he_cap_elem->mac_cap_info[0] |=
				IEEE80211_HE_MAC_CAP0_TWT_REQ;
			he_cap_elem->phy_cap_info[9] |=
				IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU;
			break;
		case NL80211_IFTYPE_MESH_POINT:
			ath11k_mac_filter_he_cap_mesh(he_cap_elem);
			break;
		}

		ath11k_mac_set_hemcsmap(ar, cap, he_cap, band);

		memset(he_cap->ppe_thres, 0, sizeof(he_cap->ppe_thres));
		if (he_cap_elem->phy_cap_info[6] &
		    IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT)
			ath11k_gen_ppe_thresh(&band_cap->he_ppet,
					      he_cap->ppe_thres);

		if (band == NL80211_BAND_6GHZ) {
			data[idx].he_6ghz_capa.capa =
				ath11k_mac_setup_he_6ghz_cap(cap, band_cap);
		}
		idx++;
	}

	return idx;
}

static void ath11k_mac_setup_he_cap(struct ath11k *ar,
				    struct ath11k_pdev_cap *cap)
{
	struct ieee80211_supported_band *band;
	int count;

	if (cap->supported_bands & WMI_HOST_WLAN_2G_CAP) {
		count = ath11k_mac_copy_he_cap(ar, cap,
					       ar->mac.iftype[NL80211_BAND_2GHZ],
					       NL80211_BAND_2GHZ);
		band = &ar->mac.sbands[NL80211_BAND_2GHZ];
		band->iftype_data = ar->mac.iftype[NL80211_BAND_2GHZ];
		band->n_iftype_data = count;
	}

	if (cap->supported_bands & WMI_HOST_WLAN_5G_CAP) {
		count = ath11k_mac_copy_he_cap(ar, cap,
					       ar->mac.iftype[NL80211_BAND_5GHZ],
					       NL80211_BAND_5GHZ);
		band = &ar->mac.sbands[NL80211_BAND_5GHZ];
		band->iftype_data = ar->mac.iftype[NL80211_BAND_5GHZ];
		band->n_iftype_data = count;
	}

	if (cap->supported_bands & WMI_HOST_WLAN_5G_CAP &&
	    ar->supports_6ghz) {
		count = ath11k_mac_copy_he_cap(ar, cap,
					       ar->mac.iftype[NL80211_BAND_6GHZ],
					       NL80211_BAND_6GHZ);
		band = &ar->mac.sbands[NL80211_BAND_6GHZ];
		band->iftype_data = ar->mac.iftype[NL80211_BAND_6GHZ];
		band->n_iftype_data = count;
	}
}

static int __ath11k_set_antenna(struct ath11k *ar, u32 tx_ant, u32 rx_ant)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	if (ath11k_check_chain_mask(ar, tx_ant, true))
		return -EINVAL;

	if (ath11k_check_chain_mask(ar, rx_ant, false))
		return -EINVAL;

	ar->cfg_tx_chainmask = tx_ant;
	ar->cfg_rx_chainmask = rx_ant;

	if (ar->state != ATH11K_STATE_ON &&
	    ar->state != ATH11K_STATE_RESTARTED)
		return 0;

	ret = ath11k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_TX_CHAIN_MASK,
					tx_ant, ar->pdev->pdev_id);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set tx-chainmask: %d, req 0x%x\n",
			    ret, tx_ant);
		return ret;
	}

	ar->num_tx_chains = get_num_chains(tx_ant);

	ret = ath11k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_RX_CHAIN_MASK,
					rx_ant, ar->pdev->pdev_id);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set rx-chainmask: %d, req 0x%x\n",
			    ret, rx_ant);
		return ret;
	}

	ar->num_rx_chains = get_num_chains(rx_ant);

	/* Reload HT/VHT/HE capability */
	ath11k_mac_setup_ht_vht_cap(ar, &ar->pdev->cap, NULL);
	ath11k_mac_setup_he_cap(ar, &ar->pdev->cap);

	return 0;
}

static void ath11k_mgmt_over_wmi_tx_drop(struct ath11k *ar, struct sk_buff *skb)
{
	int num_mgmt;

	ieee80211_free_txskb(ar->hw, skb);

	num_mgmt = atomic_dec_if_positive(&ar->num_pending_mgmt_tx);

	if (num_mgmt < 0)
		WARN_ON_ONCE(1);

	if (!num_mgmt)
		wake_up(&ar->txmgmt_empty_waitq);
}

static void ath11k_mac_tx_mgmt_free(struct ath11k *ar, int buf_id)
{
	struct sk_buff *msdu;
	struct ieee80211_tx_info *info;

	spin_lock_bh(&ar->txmgmt_idr_lock);
	msdu = idr_remove(&ar->txmgmt_idr, buf_id);
	spin_unlock_bh(&ar->txmgmt_idr_lock);

	if (!msdu)
		return;

	dma_unmap_single(ar->ab->dev, ATH11K_SKB_CB(msdu)->paddr, msdu->len,
			 DMA_TO_DEVICE);

	info = IEEE80211_SKB_CB(msdu);
	memset(&info->status, 0, sizeof(info->status));

	ath11k_mgmt_over_wmi_tx_drop(ar, msdu);
}

int ath11k_mac_tx_mgmt_pending_free(int buf_id, void *skb, void *ctx)
{
	struct ath11k *ar = ctx;

	ath11k_mac_tx_mgmt_free(ar, buf_id);

	return 0;
}

static int ath11k_mac_vif_txmgmt_idr_remove(int buf_id, void *skb, void *ctx)
{
	struct ieee80211_vif *vif = ctx;
	struct ath11k_skb_cb *skb_cb = ATH11K_SKB_CB((struct sk_buff *)skb);
	struct ath11k *ar = skb_cb->ar;

	if (skb_cb->vif == vif)
		ath11k_mac_tx_mgmt_free(ar, buf_id);

	return 0;
}

static int ath11k_mac_mgmt_tx_wmi(struct ath11k *ar, struct ath11k_vif *arvif,
				  struct sk_buff *skb)
{
	struct ath11k_base *ab = ar->ab;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info;
	dma_addr_t paddr;
	int buf_id;
	int ret;

	ATH11K_SKB_CB(skb)->ar = ar;

	spin_lock_bh(&ar->txmgmt_idr_lock);
	buf_id = idr_alloc(&ar->txmgmt_idr, skb, 0,
			   ATH11K_TX_MGMT_NUM_PENDING_MAX, GFP_ATOMIC);
	spin_unlock_bh(&ar->txmgmt_idr_lock);

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "mac tx mgmt frame, buf id %d\n", buf_id);

	if (buf_id < 0)
		return -ENOSPC;

	info = IEEE80211_SKB_CB(skb);
	if (!(info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP)) {
		if ((ieee80211_is_action(hdr->frame_control) ||
		     ieee80211_is_deauth(hdr->frame_control) ||
		     ieee80211_is_disassoc(hdr->frame_control)) &&
		     ieee80211_has_protected(hdr->frame_control)) {
			skb_put(skb, IEEE80211_CCMP_MIC_LEN);
		}
	}

	paddr = dma_map_single(ab->dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(ab->dev, paddr)) {
		ath11k_warn(ab, "failed to DMA map mgmt Tx buffer\n");
		ret = -EIO;
		goto err_free_idr;
	}

	ATH11K_SKB_CB(skb)->paddr = paddr;

	ret = ath11k_wmi_mgmt_send(ar, arvif->vdev_id, buf_id, skb);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send mgmt frame: %d\n", ret);
		goto err_unmap_buf;
	}

	return 0;

err_unmap_buf:
	dma_unmap_single(ab->dev, ATH11K_SKB_CB(skb)->paddr,
			 skb->len, DMA_TO_DEVICE);
err_free_idr:
	spin_lock_bh(&ar->txmgmt_idr_lock);
	idr_remove(&ar->txmgmt_idr, buf_id);
	spin_unlock_bh(&ar->txmgmt_idr_lock);

	return ret;
}

static void ath11k_mgmt_over_wmi_tx_purge(struct ath11k *ar)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&ar->wmi_mgmt_tx_queue)) != NULL)
		ath11k_mgmt_over_wmi_tx_drop(ar, skb);
}

static void ath11k_mgmt_over_wmi_tx_work(struct work_struct *work)
{
	struct ath11k *ar = container_of(work, struct ath11k, wmi_mgmt_tx_work);
	struct ath11k_skb_cb *skb_cb;
	struct ath11k_vif *arvif;
	struct sk_buff *skb;
	int ret;

	while ((skb = skb_dequeue(&ar->wmi_mgmt_tx_queue)) != NULL) {
		skb_cb = ATH11K_SKB_CB(skb);
		if (!skb_cb->vif) {
			ath11k_warn(ar->ab, "no vif found for mgmt frame\n");
			ath11k_mgmt_over_wmi_tx_drop(ar, skb);
			continue;
		}

		arvif = ath11k_vif_to_arvif(skb_cb->vif);
		mutex_lock(&ar->conf_mutex);
		if (ar->allocated_vdev_map & (1LL << arvif->vdev_id)) {
			ret = ath11k_mac_mgmt_tx_wmi(ar, arvif, skb);
			if (ret) {
				ath11k_warn(ar->ab, "failed to tx mgmt frame, vdev_id %d :%d\n",
					    arvif->vdev_id, ret);
				ath11k_mgmt_over_wmi_tx_drop(ar, skb);
			} else {
				ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
					   "mac tx mgmt frame, vdev_id %d\n",
					   arvif->vdev_id);
			}
		} else {
			ath11k_warn(ar->ab,
				    "dropping mgmt frame for vdev %d, is_started %d\n",
				    arvif->vdev_id,
				    arvif->is_started);
			ath11k_mgmt_over_wmi_tx_drop(ar, skb);
		}
		mutex_unlock(&ar->conf_mutex);
	}
}

static int ath11k_mac_mgmt_tx(struct ath11k *ar, struct sk_buff *skb,
			      bool is_prb_rsp)
{
	struct sk_buff_head *q = &ar->wmi_mgmt_tx_queue;

	if (test_bit(ATH11K_FLAG_CRASH_FLUSH, &ar->ab->dev_flags))
		return -ESHUTDOWN;

	/* Drop probe response packets when the pending management tx
	 * count has reached a certain threshold, so as to prioritize
	 * other mgmt packets like auth and assoc to be sent on time
	 * for establishing successful connections.
	 */
	if (is_prb_rsp &&
	    atomic_read(&ar->num_pending_mgmt_tx) > ATH11K_PRB_RSP_DROP_THRESHOLD) {
		ath11k_warn(ar->ab,
			    "dropping probe response as pending queue is almost full\n");
		return -ENOSPC;
	}

	if (skb_queue_len_lockless(q) >= ATH11K_TX_MGMT_NUM_PENDING_MAX) {
		ath11k_warn(ar->ab, "mgmt tx queue is full\n");
		return -ENOSPC;
	}

	skb_queue_tail(q, skb);
	atomic_inc(&ar->num_pending_mgmt_tx);
	queue_work(ar->ab->workqueue_aux, &ar->wmi_mgmt_tx_work);

	return 0;
}

static void ath11k_mac_op_tx(struct ieee80211_hw *hw,
			     struct ieee80211_tx_control *control,
			     struct sk_buff *skb)
{
	struct ath11k_skb_cb *skb_cb = ATH11K_SKB_CB(skb);
	struct ath11k *ar = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ath11k_sta *arsta = NULL;
	u32 info_flags = info->flags;
	bool is_prb_rsp;
	int ret;

	memset(skb_cb, 0, sizeof(*skb_cb));
	skb_cb->vif = vif;

	if (key) {
		skb_cb->cipher = key->cipher;
		skb_cb->flags |= ATH11K_SKB_CIPHER_SET;
	}

	if (info_flags & IEEE80211_TX_CTL_HW_80211_ENCAP) {
		skb_cb->flags |= ATH11K_SKB_HW_80211_ENCAP;
	} else if (ieee80211_is_mgmt(hdr->frame_control)) {
		is_prb_rsp = ieee80211_is_probe_resp(hdr->frame_control);
		ret = ath11k_mac_mgmt_tx(ar, skb, is_prb_rsp);
		if (ret) {
			ath11k_warn(ar->ab, "failed to queue management frame %d\n",
				    ret);
			ieee80211_free_txskb(ar->hw, skb);
		}
		return;
	}

	if (control->sta)
		arsta = (struct ath11k_sta *)control->sta->drv_priv;

	ret = ath11k_dp_tx(ar, arvif, arsta, skb);
	if (unlikely(ret)) {
		ath11k_warn(ar->ab, "failed to transmit frame %d\n", ret);
		ieee80211_free_txskb(ar->hw, skb);
	}
}

void ath11k_mac_drain_tx(struct ath11k *ar)
{
	/* make sure rcu-protected mac80211 tx path itself is drained */
	synchronize_net();

	cancel_work_sync(&ar->wmi_mgmt_tx_work);
	ath11k_mgmt_over_wmi_tx_purge(ar);
}

static int ath11k_mac_config_mon_status_default(struct ath11k *ar, bool enable)
{
	struct htt_rx_ring_tlv_filter tlv_filter = {0};
	struct ath11k_base *ab = ar->ab;
	int i, ret = 0;
	u32 ring_id;

	if (enable) {
		tlv_filter = ath11k_mac_mon_status_filter_default;
		if (ath11k_debugfs_rx_filter(ar))
			tlv_filter.rx_filter = ath11k_debugfs_rx_filter(ar);
	}

	for (i = 0; i < ab->hw_params.num_rxmda_per_pdev; i++) {
		ring_id = ar->dp.rx_mon_status_refill_ring[i].refill_buf_ring.ring_id;
		ret = ath11k_dp_tx_htt_rx_filter_setup(ar->ab, ring_id,
						       ar->dp.mac_id + i,
						       HAL_RXDMA_MONITOR_STATUS,
						       DP_RX_BUFFER_SIZE,
						       &tlv_filter);
	}

	if (enable && !ar->ab->hw_params.rxdma1_enable)
		mod_timer(&ar->ab->mon_reap_timer, jiffies +
			  msecs_to_jiffies(ATH11K_MON_TIMER_INTERVAL));

	return ret;
}

static void ath11k_mac_wait_reconfigure(struct ath11k_base *ab)
{
	int recovery_start_count;

	if (!ab->is_reset)
		return;

	recovery_start_count = atomic_inc_return(&ab->recovery_start_count);
	ath11k_dbg(ab, ATH11K_DBG_MAC, "recovery start count %d\n", recovery_start_count);

	if (recovery_start_count == ab->num_radios) {
		complete(&ab->recovery_start);
		ath11k_dbg(ab, ATH11K_DBG_MAC, "recovery started success\n");
	}

	ath11k_dbg(ab, ATH11K_DBG_MAC, "waiting reconfigure...\n");

	wait_for_completion_timeout(&ab->reconfigure_complete,
				    ATH11K_RECONFIGURE_TIMEOUT_HZ);
}

static int ath11k_mac_op_start(struct ieee80211_hw *hw)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;
	struct ath11k_pdev *pdev = ar->pdev;
	int ret;

	ath11k_mac_drain_tx(ar);
	mutex_lock(&ar->conf_mutex);

	switch (ar->state) {
	case ATH11K_STATE_OFF:
		ar->state = ATH11K_STATE_ON;
		break;
	case ATH11K_STATE_RESTARTING:
		ar->state = ATH11K_STATE_RESTARTED;
		ath11k_mac_wait_reconfigure(ab);
		break;
	case ATH11K_STATE_RESTARTED:
	case ATH11K_STATE_WEDGED:
	case ATH11K_STATE_ON:
		WARN_ON(1);
		ret = -EINVAL;
		goto err;
	}

	ret = ath11k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_PMF_QOS,
					1, pdev->pdev_id);

	if (ret) {
		ath11k_err(ar->ab, "failed to enable PMF QOS: (%d\n", ret);
		goto err;
	}

	ret = ath11k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_DYNAMIC_BW, 1,
					pdev->pdev_id);
	if (ret) {
		ath11k_err(ar->ab, "failed to enable dynamic bw: %d\n", ret);
		goto err;
	}

	if (test_bit(WMI_TLV_SERVICE_SPOOF_MAC_SUPPORT, ar->wmi->wmi_ab->svc_map)) {
		ret = ath11k_wmi_scan_prob_req_oui(ar, ar->mac_addr);
		if (ret) {
			ath11k_err(ab, "failed to set prob req oui: %i\n", ret);
			goto err;
		}
	}

	ret = ath11k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ARP_AC_OVERRIDE,
					0, pdev->pdev_id);
	if (ret) {
		ath11k_err(ab, "failed to set ac override for ARP: %d\n",
			   ret);
		goto err;
	}

	ret = ath11k_wmi_send_dfs_phyerr_offload_enable_cmd(ar, pdev->pdev_id);
	if (ret) {
		ath11k_err(ab, "failed to offload radar detection: %d\n",
			   ret);
		goto err;
	}

	ret = ath11k_dp_tx_htt_h2t_ppdu_stats_req(ar,
						  HTT_PPDU_STATS_TAG_DEFAULT);
	if (ret) {
		ath11k_err(ab, "failed to req ppdu stats: %d\n", ret);
		goto err;
	}

	ret = ath11k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_MESH_MCAST_ENABLE,
					1, pdev->pdev_id);

	if (ret) {
		ath11k_err(ar->ab, "failed to enable MESH MCAST ENABLE: (%d\n", ret);
		goto err;
	}

	__ath11k_set_antenna(ar, ar->cfg_tx_chainmask, ar->cfg_rx_chainmask);

	/* TODO: Do we need to enable ANI? */

	ath11k_reg_update_chan_list(ar, false);

	ar->num_started_vdevs = 0;
	ar->num_created_vdevs = 0;
	ar->num_peers = 0;
	ar->allocated_vdev_map = 0;

	/* Configure monitor status ring with default rx_filter to get rx status
	 * such as rssi, rx_duration.
	 */
	ret = ath11k_mac_config_mon_status_default(ar, true);
	if (ret) {
		ath11k_err(ab, "failed to configure monitor status ring with default rx_filter: (%d)\n",
			   ret);
		goto err;
	}

	/* Configure the hash seed for hash based reo dest ring selection */
	ath11k_wmi_pdev_lro_cfg(ar, ar->pdev->pdev_id);

	/* allow device to enter IMPS */
	if (ab->hw_params.idle_ps) {
		ret = ath11k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_IDLE_PS_CONFIG,
						1, pdev->pdev_id);
		if (ret) {
			ath11k_err(ab, "failed to enable idle ps: %d\n", ret);
			goto err;
		}
	}

	mutex_unlock(&ar->conf_mutex);

	rcu_assign_pointer(ab->pdevs_active[ar->pdev_idx],
			   &ab->pdevs[ar->pdev_idx]);

	return 0;

err:
	ar->state = ATH11K_STATE_OFF;
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static void ath11k_mac_op_stop(struct ieee80211_hw *hw)
{
	struct ath11k *ar = hw->priv;
	struct htt_ppdu_stats_info *ppdu_stats, *tmp;
	int ret;

	ath11k_mac_drain_tx(ar);

	mutex_lock(&ar->conf_mutex);
	ret = ath11k_mac_config_mon_status_default(ar, false);
	if (ret)
		ath11k_err(ar->ab, "failed to clear rx_filter for monitor status ring: (%d)\n",
			   ret);

	clear_bit(ATH11K_CAC_RUNNING, &ar->dev_flags);
	ar->state = ATH11K_STATE_OFF;
	mutex_unlock(&ar->conf_mutex);

	cancel_delayed_work_sync(&ar->scan.timeout);
	cancel_work_sync(&ar->regd_update_work);
	cancel_work_sync(&ar->ab->update_11d_work);

	if (ar->state_11d == ATH11K_11D_PREPARING) {
		ar->state_11d = ATH11K_11D_IDLE;
		complete(&ar->completed_11d_scan);
	}

	spin_lock_bh(&ar->data_lock);
	list_for_each_entry_safe(ppdu_stats, tmp, &ar->ppdu_stats_info, list) {
		list_del(&ppdu_stats->list);
		kfree(ppdu_stats);
	}
	spin_unlock_bh(&ar->data_lock);

	rcu_assign_pointer(ar->ab->pdevs_active[ar->pdev_idx], NULL);

	synchronize_rcu();

	atomic_set(&ar->num_pending_mgmt_tx, 0);
}

static void
ath11k_mac_setup_vdev_create_params(struct ath11k_vif *arvif,
				    struct vdev_create_params *params)
{
	struct ath11k *ar = arvif->ar;
	struct ath11k_pdev *pdev = ar->pdev;

	params->if_id = arvif->vdev_id;
	params->type = arvif->vdev_type;
	params->subtype = arvif->vdev_subtype;
	params->pdev_id = pdev->pdev_id;

	if (pdev->cap.supported_bands & WMI_HOST_WLAN_2G_CAP) {
		params->chains[NL80211_BAND_2GHZ].tx = ar->num_tx_chains;
		params->chains[NL80211_BAND_2GHZ].rx = ar->num_rx_chains;
	}
	if (pdev->cap.supported_bands & WMI_HOST_WLAN_5G_CAP) {
		params->chains[NL80211_BAND_5GHZ].tx = ar->num_tx_chains;
		params->chains[NL80211_BAND_5GHZ].rx = ar->num_rx_chains;
	}
	if (pdev->cap.supported_bands & WMI_HOST_WLAN_5G_CAP &&
	    ar->supports_6ghz) {
		params->chains[NL80211_BAND_6GHZ].tx = ar->num_tx_chains;
		params->chains[NL80211_BAND_6GHZ].rx = ar->num_rx_chains;
	}
}

static void ath11k_mac_op_update_vif_offload(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	u32 param_id, param_value;
	int ret;

	param_id = WMI_VDEV_PARAM_TX_ENCAP_TYPE;
	if (ath11k_frame_mode != ATH11K_HW_TXRX_ETHERNET ||
	    (vif->type != NL80211_IFTYPE_STATION &&
	     vif->type != NL80211_IFTYPE_AP))
		vif->offload_flags &= ~(IEEE80211_OFFLOAD_ENCAP_ENABLED |
					IEEE80211_OFFLOAD_DECAP_ENABLED);

	if (vif->offload_flags & IEEE80211_OFFLOAD_ENCAP_ENABLED)
		param_value = ATH11K_HW_TXRX_ETHERNET;
	else if (test_bit(ATH11K_FLAG_RAW_MODE, &ab->dev_flags))
		param_value = ATH11K_HW_TXRX_RAW;
	else
		param_value = ATH11K_HW_TXRX_NATIVE_WIFI;

	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    param_id, param_value);
	if (ret) {
		ath11k_warn(ab, "failed to set vdev %d tx encap mode: %d\n",
			    arvif->vdev_id, ret);
		vif->offload_flags &= ~IEEE80211_OFFLOAD_ENCAP_ENABLED;
	}

	param_id = WMI_VDEV_PARAM_RX_DECAP_TYPE;
	if (vif->offload_flags & IEEE80211_OFFLOAD_DECAP_ENABLED)
		param_value = ATH11K_HW_TXRX_ETHERNET;
	else if (test_bit(ATH11K_FLAG_RAW_MODE, &ab->dev_flags))
		param_value = ATH11K_HW_TXRX_RAW;
	else
		param_value = ATH11K_HW_TXRX_NATIVE_WIFI;

	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    param_id, param_value);
	if (ret) {
		ath11k_warn(ab, "failed to set vdev %d rx decap mode: %d\n",
			    arvif->vdev_id, ret);
		vif->offload_flags &= ~IEEE80211_OFFLOAD_DECAP_ENABLED;
	}
}

static bool ath11k_mac_vif_ap_active_any(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	struct ath11k_vif *arvif;
	int i;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		list_for_each_entry(arvif, &ar->arvifs, list) {
			if (arvif->is_up && arvif->vdev_type == WMI_VDEV_TYPE_AP)
				return true;
		}
	}
	return false;
}

void ath11k_mac_11d_scan_start(struct ath11k *ar, u32 vdev_id)
{
	struct wmi_11d_scan_start_params param;
	int ret;

	mutex_lock(&ar->ab->vdev_id_11d_lock);

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac vdev id for 11d scan %d\n",
		   ar->vdev_id_11d_scan);

	if (ar->regdom_set_by_user)
		goto fin;

	if (ar->vdev_id_11d_scan != ATH11K_11D_INVALID_VDEV_ID)
		goto fin;

	if (!test_bit(WMI_TLV_SERVICE_11D_OFFLOAD, ar->ab->wmi_ab.svc_map))
		goto fin;

	if (ath11k_mac_vif_ap_active_any(ar->ab))
		goto fin;

	param.vdev_id = vdev_id;
	param.start_interval_msec = 0;
	param.scan_period_msec = ATH11K_SCAN_11D_INTERVAL;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac start 11d scan\n");

	ret = ath11k_wmi_send_11d_scan_start_cmd(ar, &param);
	if (ret) {
		ath11k_warn(ar->ab, "failed to start 11d scan vdev %d ret: %d\n",
			    vdev_id, ret);
	} else {
		ar->vdev_id_11d_scan = vdev_id;
		if (ar->state_11d == ATH11K_11D_PREPARING)
			ar->state_11d = ATH11K_11D_RUNNING;
	}

fin:
	if (ar->state_11d == ATH11K_11D_PREPARING) {
		ar->state_11d = ATH11K_11D_IDLE;
		complete(&ar->completed_11d_scan);
	}

	mutex_unlock(&ar->ab->vdev_id_11d_lock);
}

void ath11k_mac_11d_scan_stop(struct ath11k *ar)
{
	int ret;
	u32 vdev_id;

	if (!test_bit(WMI_TLV_SERVICE_11D_OFFLOAD, ar->ab->wmi_ab.svc_map))
		return;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac stop 11d scan\n");

	mutex_lock(&ar->ab->vdev_id_11d_lock);

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac stop 11d vdev id %d\n",
		   ar->vdev_id_11d_scan);

	if (ar->state_11d == ATH11K_11D_PREPARING) {
		ar->state_11d = ATH11K_11D_IDLE;
		complete(&ar->completed_11d_scan);
	}

	if (ar->vdev_id_11d_scan != ATH11K_11D_INVALID_VDEV_ID) {
		vdev_id = ar->vdev_id_11d_scan;

		ret = ath11k_wmi_send_11d_scan_stop_cmd(ar, vdev_id);
		if (ret) {
			ath11k_warn(ar->ab,
				    "failed to stopt 11d scan vdev %d ret: %d\n",
				    vdev_id, ret);
		} else {
			ar->vdev_id_11d_scan = ATH11K_11D_INVALID_VDEV_ID;
			ar->state_11d = ATH11K_11D_IDLE;
			complete(&ar->completed_11d_scan);
		}
	}
	mutex_unlock(&ar->ab->vdev_id_11d_lock);
}

void ath11k_mac_11d_scan_stop_all(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	int i;

	ath11k_dbg(ab, ATH11K_DBG_MAC, "mac stop soc 11d scan\n");

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;

		ath11k_mac_11d_scan_stop(ar);
	}
}

static int ath11k_mac_vdev_delete(struct ath11k *ar, struct ath11k_vif *arvif)
{
	unsigned long time_left;
	struct ieee80211_vif *vif = arvif->vif;
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	reinit_completion(&ar->vdev_delete_done);

	ret = ath11k_wmi_vdev_delete(ar, arvif->vdev_id);
	if (ret) {
		ath11k_warn(ar->ab, "failed to delete WMI vdev %d: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	time_left = wait_for_completion_timeout(&ar->vdev_delete_done,
						ATH11K_VDEV_DELETE_TIMEOUT_HZ);
	if (time_left == 0) {
		ath11k_warn(ar->ab, "Timeout in receiving vdev delete response\n");
		return -ETIMEDOUT;
	}

	ar->ab->free_vdev_map |= 1LL << (arvif->vdev_id);
	ar->allocated_vdev_map &= ~(1LL << arvif->vdev_id);
	ar->num_created_vdevs--;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "vdev %pM deleted, vdev_id %d\n",
		   vif->addr, arvif->vdev_id);

	return ret;
}

static int ath11k_mac_op_add_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	struct vdev_create_params vdev_param = {0};
	struct peer_create_params peer_param;
	u32 param_id, param_value;
	u16 nss;
	int i;
	int ret, fbret;
	int bit;

	vif->driver_flags |= IEEE80211_VIF_SUPPORTS_UAPSD;

	mutex_lock(&ar->conf_mutex);

	if (vif->type == NL80211_IFTYPE_AP &&
	    ar->num_peers > (ar->max_num_peers - 1)) {
		ath11k_warn(ab, "failed to create vdev due to insufficient peer entry resource in firmware\n");
		ret = -ENOBUFS;
		goto err;
	}

	if (ar->num_created_vdevs > (TARGET_NUM_VDEVS(ab) - 1)) {
		ath11k_warn(ab, "failed to create vdev %u, reached max vdev limit %d\n",
			    ar->num_created_vdevs, TARGET_NUM_VDEVS(ab));
		ret = -EBUSY;
		goto err;
	}

	/* In the case of hardware recovery, debugfs files are
	 * not deleted since ieee80211_ops.remove_interface() is
	 * not invoked. In such cases, try to delete the files.
	 * These will be re-created later.
	 */
	ath11k_debugfs_remove_interface(arvif);

	memset(arvif, 0, sizeof(*arvif));

	arvif->ar = ar;
	arvif->vif = vif;

	INIT_LIST_HEAD(&arvif->list);
	INIT_DELAYED_WORK(&arvif->connection_loss_work,
			  ath11k_mac_vif_sta_connection_loss_work);

	for (i = 0; i < ARRAY_SIZE(arvif->bitrate_mask.control); i++) {
		arvif->bitrate_mask.control[i].legacy = 0xffffffff;
		arvif->bitrate_mask.control[i].gi = NL80211_TXRATE_FORCE_SGI;
		memset(arvif->bitrate_mask.control[i].ht_mcs, 0xff,
		       sizeof(arvif->bitrate_mask.control[i].ht_mcs));
		memset(arvif->bitrate_mask.control[i].vht_mcs, 0xff,
		       sizeof(arvif->bitrate_mask.control[i].vht_mcs));
		memset(arvif->bitrate_mask.control[i].he_mcs, 0xff,
		       sizeof(arvif->bitrate_mask.control[i].he_mcs));
	}

	bit = __ffs64(ab->free_vdev_map);

	arvif->vdev_id = bit;
	arvif->vdev_subtype = WMI_VDEV_SUBTYPE_NONE;

	switch (vif->type) {
	case NL80211_IFTYPE_UNSPECIFIED:
	case NL80211_IFTYPE_STATION:
		arvif->vdev_type = WMI_VDEV_TYPE_STA;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		arvif->vdev_subtype = WMI_VDEV_SUBTYPE_MESH_11S;
		fallthrough;
	case NL80211_IFTYPE_AP:
		arvif->vdev_type = WMI_VDEV_TYPE_AP;
		break;
	case NL80211_IFTYPE_MONITOR:
		arvif->vdev_type = WMI_VDEV_TYPE_MONITOR;
		ar->monitor_vdev_id = bit;
		break;
	default:
		WARN_ON(1);
		break;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac add interface id %d type %d subtype %d map %llx\n",
		   arvif->vdev_id, arvif->vdev_type, arvif->vdev_subtype,
		   ab->free_vdev_map);

	vif->cab_queue = arvif->vdev_id % (ATH11K_HW_MAX_QUEUES - 1);
	for (i = 0; i < ARRAY_SIZE(vif->hw_queue); i++)
		vif->hw_queue[i] = i % (ATH11K_HW_MAX_QUEUES - 1);

	ath11k_mac_setup_vdev_create_params(arvif, &vdev_param);

	ret = ath11k_wmi_vdev_create(ar, vif->addr, &vdev_param);
	if (ret) {
		ath11k_warn(ab, "failed to create WMI vdev %d: %d\n",
			    arvif->vdev_id, ret);
		goto err;
	}

	ar->num_created_vdevs++;
	ath11k_dbg(ab, ATH11K_DBG_MAC, "vdev %pM created, vdev_id %d\n",
		   vif->addr, arvif->vdev_id);
	ar->allocated_vdev_map |= 1LL << arvif->vdev_id;
	ab->free_vdev_map &= ~(1LL << arvif->vdev_id);

	spin_lock_bh(&ar->data_lock);
	list_add(&arvif->list, &ar->arvifs);
	spin_unlock_bh(&ar->data_lock);

	ath11k_mac_op_update_vif_offload(hw, vif);

	nss = get_num_chains(ar->cfg_tx_chainmask) ? : 1;
	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_NSS, nss);
	if (ret) {
		ath11k_warn(ab, "failed to set vdev %d chainmask 0x%x, nss %d :%d\n",
			    arvif->vdev_id, ar->cfg_tx_chainmask, nss, ret);
		goto err_vdev_del;
	}

	switch (arvif->vdev_type) {
	case WMI_VDEV_TYPE_AP:
		peer_param.vdev_id = arvif->vdev_id;
		peer_param.peer_addr = vif->addr;
		peer_param.peer_type = WMI_PEER_TYPE_DEFAULT;
		ret = ath11k_peer_create(ar, arvif, NULL, &peer_param);
		if (ret) {
			ath11k_warn(ab, "failed to vdev %d create peer for AP: %d\n",
				    arvif->vdev_id, ret);
			goto err_vdev_del;
		}

		ret = ath11k_mac_set_kickout(arvif);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set vdev %i kickout parameters: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_del;
		}

		ath11k_mac_11d_scan_stop_all(ar->ab);
		break;
	case WMI_VDEV_TYPE_STA:
		param_id = WMI_STA_PS_PARAM_RX_WAKE_POLICY;
		param_value = WMI_STA_PS_RX_WAKE_POLICY_WAKE;
		ret = ath11k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
						  param_id, param_value);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set vdev %d RX wake policy: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_del;
		}

		param_id = WMI_STA_PS_PARAM_TX_WAKE_THRESHOLD;
		param_value = WMI_STA_PS_TX_WAKE_THRESHOLD_ALWAYS;
		ret = ath11k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
						  param_id, param_value);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set vdev %d TX wake threshold: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_del;
		}

		param_id = WMI_STA_PS_PARAM_PSPOLL_COUNT;
		param_value = WMI_STA_PS_PSPOLL_COUNT_NO_MAX;
		ret = ath11k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
						  param_id, param_value);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set vdev %d pspoll count: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_del;
		}

		ret = ath11k_wmi_pdev_set_ps_mode(ar, arvif->vdev_id,
						  WMI_STA_PS_MODE_DISABLED);
		if (ret) {
			ath11k_warn(ar->ab, "failed to disable vdev %d ps mode: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_del;
		}

		if (test_bit(WMI_TLV_SERVICE_11D_OFFLOAD, ab->wmi_ab.svc_map)) {
			reinit_completion(&ar->completed_11d_scan);
			ar->state_11d = ATH11K_11D_PREPARING;
		}
		break;
	case WMI_VDEV_TYPE_MONITOR:
		set_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED, &ar->monitor_flags);
		break;
	default:
		break;
	}

	arvif->txpower = vif->bss_conf.txpower;
	ret = ath11k_mac_txpower_recalc(ar);
	if (ret)
		goto err_peer_del;

	param_id = WMI_VDEV_PARAM_RTS_THRESHOLD;
	param_value = ar->hw->wiphy->rts_threshold;
	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    param_id, param_value);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set rts threshold for vdev %d: %d\n",
			    arvif->vdev_id, ret);
	}

	ath11k_dp_vdev_tx_attach(ar, arvif);

	ath11k_debugfs_add_interface(arvif);

	if (vif->type != NL80211_IFTYPE_MONITOR &&
	    test_bit(ATH11K_FLAG_MONITOR_CONF_ENABLED, &ar->monitor_flags)) {
		ret = ath11k_mac_monitor_vdev_create(ar);
		if (ret)
			ath11k_warn(ar->ab, "failed to create monitor vdev during add interface: %d",
				    ret);
	}

	mutex_unlock(&ar->conf_mutex);

	return 0;

err_peer_del:
	if (arvif->vdev_type == WMI_VDEV_TYPE_AP) {
		fbret = ath11k_peer_delete(ar, arvif->vdev_id, vif->addr);
		if (fbret) {
			ath11k_warn(ar->ab, "fallback fail to delete peer addr %pM vdev_id %d ret %d\n",
				    vif->addr, arvif->vdev_id, fbret);
			goto err;
		}
	}

err_vdev_del:
	ath11k_mac_vdev_delete(ar, arvif);
	spin_lock_bh(&ar->data_lock);
	list_del(&arvif->list);
	spin_unlock_bh(&ar->data_lock);

err:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static int ath11k_mac_vif_unref(int buf_id, void *skb, void *ctx)
{
	struct ieee80211_vif *vif = (struct ieee80211_vif *)ctx;
	struct ath11k_skb_cb *skb_cb = ATH11K_SKB_CB((struct sk_buff *)skb);

	if (skb_cb->vif == vif)
		skb_cb->vif = NULL;

	return 0;
}

static void ath11k_mac_op_remove_interface(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	struct ath11k_base *ab = ar->ab;
	int ret;
	int i;

	cancel_delayed_work_sync(&arvif->connection_loss_work);

	mutex_lock(&ar->conf_mutex);

	ath11k_dbg(ab, ATH11K_DBG_MAC, "mac remove interface (vdev %d)\n",
		   arvif->vdev_id);

	ret = ath11k_spectral_vif_stop(arvif);
	if (ret)
		ath11k_warn(ab, "failed to stop spectral for vdev %i: %d\n",
			    arvif->vdev_id, ret);

	if (arvif->vdev_type == WMI_VDEV_TYPE_STA)
		ath11k_mac_11d_scan_stop(ar);

	if (arvif->vdev_type == WMI_VDEV_TYPE_AP) {
		ret = ath11k_peer_delete(ar, arvif->vdev_id, vif->addr);
		if (ret)
			ath11k_warn(ab, "failed to submit AP self-peer removal on vdev %d: %d\n",
				    arvif->vdev_id, ret);
	}

	ret = ath11k_mac_vdev_delete(ar, arvif);
	if (ret) {
		ath11k_warn(ab, "failed to delete vdev %d: %d\n",
			    arvif->vdev_id, ret);
		goto err_vdev_del;
	}

	if (arvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		clear_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED, &ar->monitor_flags);
		ar->monitor_vdev_id = -1;
	} else if (test_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED, &ar->monitor_flags) &&
		   !test_bit(ATH11K_FLAG_MONITOR_STARTED, &ar->monitor_flags)) {
		ret = ath11k_mac_monitor_vdev_delete(ar);
		if (ret)
			/* continue even if there's an error */
			ath11k_warn(ar->ab, "failed to delete vdev monitor during remove interface: %d",
				    ret);
	}

err_vdev_del:
	spin_lock_bh(&ar->data_lock);
	list_del(&arvif->list);
	spin_unlock_bh(&ar->data_lock);

	ath11k_peer_cleanup(ar, arvif->vdev_id);

	idr_for_each(&ar->txmgmt_idr,
		     ath11k_mac_vif_txmgmt_idr_remove, vif);

	for (i = 0; i < ab->hw_params.max_tx_ring; i++) {
		spin_lock_bh(&ab->dp.tx_ring[i].tx_idr_lock);
		idr_for_each(&ab->dp.tx_ring[i].txbuf_idr,
			     ath11k_mac_vif_unref, vif);
		spin_unlock_bh(&ab->dp.tx_ring[i].tx_idr_lock);
	}

	/* Recalc txpower for remaining vdev */
	ath11k_mac_txpower_recalc(ar);

	ath11k_debugfs_remove_interface(arvif);

	/* TODO: recal traffic pause state based on the available vdevs */

	mutex_unlock(&ar->conf_mutex);
}

/* FIXME: Has to be verified. */
#define SUPPORTED_FILTERS			\
	(FIF_ALLMULTI |				\
	FIF_CONTROL |				\
	FIF_PSPOLL |				\
	FIF_OTHER_BSS |				\
	FIF_BCN_PRBRESP_PROMISC |		\
	FIF_PROBE_REQ |				\
	FIF_FCSFAIL)

static void ath11k_mac_op_configure_filter(struct ieee80211_hw *hw,
					   unsigned int changed_flags,
					   unsigned int *total_flags,
					   u64 multicast)
{
	struct ath11k *ar = hw->priv;

	mutex_lock(&ar->conf_mutex);

	*total_flags &= SUPPORTED_FILTERS;
	ar->filter_flags = *total_flags;

	mutex_unlock(&ar->conf_mutex);
}

static int ath11k_mac_op_get_antenna(struct ieee80211_hw *hw, u32 *tx_ant, u32 *rx_ant)
{
	struct ath11k *ar = hw->priv;

	mutex_lock(&ar->conf_mutex);

	*tx_ant = ar->cfg_tx_chainmask;
	*rx_ant = ar->cfg_rx_chainmask;

	mutex_unlock(&ar->conf_mutex);

	return 0;
}

static int ath11k_mac_op_set_antenna(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant)
{
	struct ath11k *ar = hw->priv;
	int ret;

	mutex_lock(&ar->conf_mutex);
	ret = __ath11k_set_antenna(ar, tx_ant, rx_ant);
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static int ath11k_mac_op_ampdu_action(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_ampdu_params *params)
{
	struct ath11k *ar = hw->priv;
	int ret = -EINVAL;

	mutex_lock(&ar->conf_mutex);

	switch (params->action) {
	case IEEE80211_AMPDU_RX_START:
		ret = ath11k_dp_rx_ampdu_start(ar, params);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		ret = ath11k_dp_rx_ampdu_stop(ar, params);
		break;
	case IEEE80211_AMPDU_TX_START:
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		/* Tx A-MPDU aggregation offloaded to hw/fw so deny mac80211
		 * Tx aggregation requests.
		 */
		ret = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static int ath11k_mac_op_add_chanctx(struct ieee80211_hw *hw,
				     struct ieee80211_chanctx_conf *ctx)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;

	ath11k_dbg(ab, ATH11K_DBG_MAC,
		   "mac chanctx add freq %u width %d ptr %pK\n",
		   ctx->def.chan->center_freq, ctx->def.width, ctx);

	mutex_lock(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	/* TODO: In case of multiple channel context, populate rx_channel from
	 * Rx PPDU desc information.
	 */
	ar->rx_channel = ctx->def.chan;
	spin_unlock_bh(&ar->data_lock);

	mutex_unlock(&ar->conf_mutex);

	return 0;
}

static void ath11k_mac_op_remove_chanctx(struct ieee80211_hw *hw,
					 struct ieee80211_chanctx_conf *ctx)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;

	ath11k_dbg(ab, ATH11K_DBG_MAC,
		   "mac chanctx remove freq %u width %d ptr %pK\n",
		   ctx->def.chan->center_freq, ctx->def.width, ctx);

	mutex_lock(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	/* TODO: In case of there is one more channel context left, populate
	 * rx_channel with the channel of that remaining channel context.
	 */
	ar->rx_channel = NULL;
	spin_unlock_bh(&ar->data_lock);

	mutex_unlock(&ar->conf_mutex);
}

static int
ath11k_mac_vdev_start_restart(struct ath11k_vif *arvif,
			      struct ieee80211_chanctx_conf *ctx,
			      bool restart)
{
	struct ath11k *ar = arvif->ar;
	struct ath11k_base *ab = ar->ab;
	struct wmi_vdev_start_req_arg arg = {};
	const struct cfg80211_chan_def *chandef = &ctx->def;
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	reinit_completion(&ar->vdev_setup_done);

	arg.vdev_id = arvif->vdev_id;
	arg.dtim_period = arvif->dtim_period;
	arg.bcn_intval = arvif->beacon_interval;

	arg.channel.freq = chandef->chan->center_freq;
	arg.channel.band_center_freq1 = chandef->center_freq1;
	arg.channel.band_center_freq2 = chandef->center_freq2;
	arg.channel.mode =
		ath11k_phymodes[chandef->chan->band][chandef->width];

	arg.channel.min_power = 0;
	arg.channel.max_power = chandef->chan->max_power;
	arg.channel.max_reg_power = chandef->chan->max_reg_power;
	arg.channel.max_antenna_gain = chandef->chan->max_antenna_gain;

	arg.pref_tx_streams = ar->num_tx_chains;
	arg.pref_rx_streams = ar->num_rx_chains;

	if (arvif->vdev_type == WMI_VDEV_TYPE_AP) {
		arg.ssid = arvif->u.ap.ssid;
		arg.ssid_len = arvif->u.ap.ssid_len;
		arg.hidden_ssid = arvif->u.ap.hidden_ssid;

		/* For now allow DFS for AP mode */
		arg.channel.chan_radar =
			!!(chandef->chan->flags & IEEE80211_CHAN_RADAR);

		arg.channel.freq2_radar = ctx->radar_enabled;

		arg.channel.passive = arg.channel.chan_radar;

		spin_lock_bh(&ab->base_lock);
		arg.regdomain = ar->ab->dfs_region;
		spin_unlock_bh(&ab->base_lock);
	}

	arg.channel.passive |= !!(chandef->chan->flags & IEEE80211_CHAN_NO_IR);

	ath11k_dbg(ab, ATH11K_DBG_MAC,
		   "mac vdev %d start center_freq %d phymode %s\n",
		   arg.vdev_id, arg.channel.freq,
		   ath11k_wmi_phymode_str(arg.channel.mode));

	ret = ath11k_wmi_vdev_start(ar, &arg, restart);
	if (ret) {
		ath11k_warn(ar->ab, "failed to %s WMI vdev %i\n",
			    restart ? "restart" : "start", arg.vdev_id);
		return ret;
	}

	ret = ath11k_mac_vdev_setup_sync(ar);
	if (ret) {
		ath11k_warn(ab, "failed to synchronize setup for vdev %i %s: %d\n",
			    arg.vdev_id, restart ? "restart" : "start", ret);
		return ret;
	}

	if (!restart)
		ar->num_started_vdevs++;

	ath11k_dbg(ab, ATH11K_DBG_MAC,  "vdev %pM started, vdev_id %d\n",
		   arvif->vif->addr, arvif->vdev_id);

	/* Enable CAC Flag in the driver by checking the channel DFS cac time,
	 * i.e dfs_cac_ms value which will be valid only for radar channels
	 * and state as NL80211_DFS_USABLE which indicates CAC needs to be
	 * done before channel usage. This flags is used to drop rx packets.
	 * during CAC.
	 */
	/* TODO Set the flag for other interface types as required */
	if (arvif->vdev_type == WMI_VDEV_TYPE_AP &&
	    chandef->chan->dfs_cac_ms &&
	    chandef->chan->dfs_state == NL80211_DFS_USABLE) {
		set_bit(ATH11K_CAC_RUNNING, &ar->dev_flags);
		ath11k_dbg(ab, ATH11K_DBG_MAC,
			   "CAC Started in chan_freq %d for vdev %d\n",
			   arg.channel.freq, arg.vdev_id);
	}

	ret = ath11k_mac_set_txbf_conf(arvif);
	if (ret)
		ath11k_warn(ab, "failed to set txbf conf for vdev %d: %d\n",
			    arvif->vdev_id, ret);

	return 0;
}

static int ath11k_mac_vdev_stop(struct ath11k_vif *arvif)
{
	struct ath11k *ar = arvif->ar;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	reinit_completion(&ar->vdev_setup_done);

	ret = ath11k_wmi_vdev_stop(ar, arvif->vdev_id);
	if (ret) {
		ath11k_warn(ar->ab, "failed to stop WMI vdev %i: %d\n",
			    arvif->vdev_id, ret);
		goto err;
	}

	ret = ath11k_mac_vdev_setup_sync(ar);
	if (ret) {
		ath11k_warn(ar->ab, "failed to synchronize setup for vdev %i: %d\n",
			    arvif->vdev_id, ret);
		goto err;
	}

	WARN_ON(ar->num_started_vdevs == 0);

	ar->num_started_vdevs--;
	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "vdev %pM stopped, vdev_id %d\n",
		   arvif->vif->addr, arvif->vdev_id);

	if (test_bit(ATH11K_CAC_RUNNING, &ar->dev_flags)) {
		clear_bit(ATH11K_CAC_RUNNING, &ar->dev_flags);
		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "CAC Stopped for vdev %d\n",
			   arvif->vdev_id);
	}

	return 0;
err:
	return ret;
}

static int ath11k_mac_vdev_start(struct ath11k_vif *arvif,
				 struct ieee80211_chanctx_conf *ctx)
{
	return ath11k_mac_vdev_start_restart(arvif, ctx, false);
}

static int ath11k_mac_vdev_restart(struct ath11k_vif *arvif,
				   struct ieee80211_chanctx_conf *ctx)
{
	return ath11k_mac_vdev_start_restart(arvif, ctx, true);
}

struct ath11k_mac_change_chanctx_arg {
	struct ieee80211_chanctx_conf *ctx;
	struct ieee80211_vif_chanctx_switch *vifs;
	int n_vifs;
	int next_vif;
};

static void
ath11k_mac_change_chanctx_cnt_iter(void *data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct ath11k_mac_change_chanctx_arg *arg = data;

	if (rcu_access_pointer(vif->bss_conf.chanctx_conf) != arg->ctx)
		return;

	arg->n_vifs++;
}

static void
ath11k_mac_change_chanctx_fill_iter(void *data, u8 *mac,
				    struct ieee80211_vif *vif)
{
	struct ath11k_mac_change_chanctx_arg *arg = data;
	struct ieee80211_chanctx_conf *ctx;

	ctx = rcu_access_pointer(vif->bss_conf.chanctx_conf);
	if (ctx != arg->ctx)
		return;

	if (WARN_ON(arg->next_vif == arg->n_vifs))
		return;

	arg->vifs[arg->next_vif].vif = vif;
	arg->vifs[arg->next_vif].old_ctx = ctx;
	arg->vifs[arg->next_vif].new_ctx = ctx;
	arg->next_vif++;
}

static void
ath11k_mac_update_vif_chan(struct ath11k *ar,
			   struct ieee80211_vif_chanctx_switch *vifs,
			   int n_vifs)
{
	struct ath11k_base *ab = ar->ab;
	struct ath11k_vif *arvif;
	int ret;
	int i;
	bool monitor_vif = false;

	lockdep_assert_held(&ar->conf_mutex);

	/* Associated channel resources of all relevant vdevs
	 * should be available for the channel switch now.
	 */

	/* TODO: Update ar->rx_channel */

	for (i = 0; i < n_vifs; i++) {
		arvif = (void *)vifs[i].vif->drv_priv;

		if (WARN_ON(!arvif->is_started))
			continue;

		/* change_chanctx can be called even before vdev_up from
		 * ieee80211_start_ap->ieee80211_vif_use_channel->
		 * ieee80211_recalc_radar_chanctx.
		 *
		 * Firmware expect vdev_restart only if vdev is up.
		 * If vdev is down then it expect vdev_stop->vdev_start.
		 */
		if (arvif->is_up) {
			ret = ath11k_mac_vdev_restart(arvif, vifs[i].new_ctx);
			if (ret) {
				ath11k_warn(ab, "failed to restart vdev %d: %d\n",
					    arvif->vdev_id, ret);
				continue;
			}
		} else {
			ret = ath11k_mac_vdev_stop(arvif);
			if (ret) {
				ath11k_warn(ab, "failed to stop vdev %d: %d\n",
					    arvif->vdev_id, ret);
				continue;
			}

			ret = ath11k_mac_vdev_start(arvif, vifs[i].new_ctx);
			if (ret)
				ath11k_warn(ab, "failed to start vdev %d: %d\n",
					    arvif->vdev_id, ret);

			continue;
		}

		ret = ath11k_mac_setup_bcn_tmpl(arvif);
		if (ret)
			ath11k_warn(ab, "failed to update bcn tmpl during csa: %d\n",
				    ret);

		ret = ath11k_wmi_vdev_up(arvif->ar, arvif->vdev_id, arvif->aid,
					 arvif->bssid);
		if (ret) {
			ath11k_warn(ab, "failed to bring vdev up %d: %d\n",
				    arvif->vdev_id, ret);
			continue;
		}
	}

	/* Restart the internal monitor vdev on new channel */
	if (!monitor_vif &&
	    test_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED, &ar->monitor_flags)) {
		ret = ath11k_mac_monitor_stop(ar);
		if (ret) {
			ath11k_warn(ar->ab, "failed to stop monitor during vif channel update: %d",
				    ret);
			return;
		}

		ret = ath11k_mac_monitor_start(ar);
		if (ret) {
			ath11k_warn(ar->ab, "failed to start monitor during vif channel update: %d",
				    ret);
			return;
		}
	}
}

static void
ath11k_mac_update_active_vif_chan(struct ath11k *ar,
				  struct ieee80211_chanctx_conf *ctx)
{
	struct ath11k_mac_change_chanctx_arg arg = { .ctx = ctx };

	lockdep_assert_held(&ar->conf_mutex);

	ieee80211_iterate_active_interfaces_atomic(ar->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath11k_mac_change_chanctx_cnt_iter,
						   &arg);
	if (arg.n_vifs == 0)
		return;

	arg.vifs = kcalloc(arg.n_vifs, sizeof(arg.vifs[0]), GFP_KERNEL);
	if (!arg.vifs)
		return;

	ieee80211_iterate_active_interfaces_atomic(ar->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath11k_mac_change_chanctx_fill_iter,
						   &arg);

	ath11k_mac_update_vif_chan(ar, arg.vifs, arg.n_vifs);

	kfree(arg.vifs);
}

static void ath11k_mac_op_change_chanctx(struct ieee80211_hw *hw,
					 struct ieee80211_chanctx_conf *ctx,
					 u32 changed)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;

	mutex_lock(&ar->conf_mutex);

	ath11k_dbg(ab, ATH11K_DBG_MAC,
		   "mac chanctx change freq %u width %d ptr %pK changed %x\n",
		   ctx->def.chan->center_freq, ctx->def.width, ctx, changed);

	/* This shouldn't really happen because channel switching should use
	 * switch_vif_chanctx().
	 */
	if (WARN_ON(changed & IEEE80211_CHANCTX_CHANGE_CHANNEL))
		goto unlock;

	if (changed & IEEE80211_CHANCTX_CHANGE_WIDTH ||
	    changed & IEEE80211_CHANCTX_CHANGE_RADAR)
		ath11k_mac_update_active_vif_chan(ar, ctx);

	/* TODO: Recalc radar detection */

unlock:
	mutex_unlock(&ar->conf_mutex);
}

static int ath11k_start_vdev_delay(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	int ret;

	if (WARN_ON(arvif->is_started))
		return -EBUSY;

	ret = ath11k_mac_vdev_start(arvif, &arvif->chanctx);
	if (ret) {
		ath11k_warn(ab, "failed to start vdev %i addr %pM on freq %d: %d\n",
			    arvif->vdev_id, vif->addr,
			    arvif->chanctx.def.chan->center_freq, ret);
		return ret;
	}

	/* Reconfigure hardware rate code since it is cleared by firmware.
	 */
	if (ar->hw_rate_code > 0) {
		u32 vdev_param = WMI_VDEV_PARAM_MGMT_RATE;

		ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, vdev_param,
						    ar->hw_rate_code);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set mgmt tx rate %d\n", ret);
			return ret;
		}
	}

	if (arvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ret = ath11k_wmi_vdev_up(ar, arvif->vdev_id, 0, ar->mac_addr);
		if (ret) {
			ath11k_warn(ab, "failed put monitor up: %d\n", ret);
			return ret;
		}
	}

	arvif->is_started = true;

	/* TODO: Setup ps and cts/rts protection */
	return 0;
}

static int
ath11k_mac_op_assign_vif_chanctx(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *link_conf,
				 struct ieee80211_chanctx_conf *ctx)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	int ret;
	struct peer_create_params param;

	mutex_lock(&ar->conf_mutex);

	ath11k_dbg(ab, ATH11K_DBG_MAC,
		   "mac chanctx assign ptr %pK vdev_id %i\n",
		   ctx, arvif->vdev_id);

	/* for QCA6390 bss peer must be created before vdev_start */
	if (ab->hw_params.vdev_start_delay &&
	    arvif->vdev_type != WMI_VDEV_TYPE_AP &&
	    arvif->vdev_type != WMI_VDEV_TYPE_MONITOR &&
	    !ath11k_peer_find_by_vdev_id(ab, arvif->vdev_id)) {
		memcpy(&arvif->chanctx, ctx, sizeof(*ctx));
		ret = 0;
		goto out;
	}

	if (WARN_ON(arvif->is_started)) {
		ret = -EBUSY;
		goto out;
	}

	if (ab->hw_params.vdev_start_delay &&
	    arvif->vdev_type != WMI_VDEV_TYPE_AP &&
	    arvif->vdev_type != WMI_VDEV_TYPE_MONITOR) {
		param.vdev_id = arvif->vdev_id;
		param.peer_type = WMI_PEER_TYPE_DEFAULT;
		param.peer_addr = ar->mac_addr;

		ret = ath11k_peer_create(ar, arvif, NULL, &param);
		if (ret) {
			ath11k_warn(ab, "failed to create peer after vdev start delay: %d",
				    ret);
			goto out;
		}
	}

	if (arvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ret = ath11k_mac_monitor_start(ar);
		if (ret) {
			ath11k_warn(ar->ab, "failed to start monitor during vif channel context assignment: %d",
				    ret);
			goto out;
		}

		arvif->is_started = true;
		goto out;
	}

	ret = ath11k_mac_vdev_start(arvif, ctx);
	if (ret) {
		ath11k_warn(ab, "failed to start vdev %i addr %pM on freq %d: %d\n",
			    arvif->vdev_id, vif->addr,
			    ctx->def.chan->center_freq, ret);
		goto out;
	}

	arvif->is_started = true;

	if (arvif->vdev_type != WMI_VDEV_TYPE_MONITOR &&
	    test_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED, &ar->monitor_flags)) {
		ret = ath11k_mac_monitor_start(ar);
		if (ret) {
			ath11k_warn(ar->ab, "failed to start monitor during vif channel context assignment: %d",
				    ret);
			goto out;
		}
	}

	/* TODO: Setup ps and cts/rts protection */

	ret = 0;

out:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static void
ath11k_mac_op_unassign_vif_chanctx(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *link_conf,
				   struct ieee80211_chanctx_conf *ctx)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct ath11k_peer *peer;
	int ret;

	mutex_lock(&ar->conf_mutex);

	ath11k_dbg(ab, ATH11K_DBG_MAC,
		   "mac chanctx unassign ptr %pK vdev_id %i\n",
		   ctx, arvif->vdev_id);

	WARN_ON(!arvif->is_started);

	if (ab->hw_params.vdev_start_delay &&
	    arvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		spin_lock_bh(&ab->base_lock);
		peer = ath11k_peer_find_by_addr(ab, ar->mac_addr);
		spin_unlock_bh(&ab->base_lock);
		if (peer)
			ath11k_peer_delete(ar, arvif->vdev_id, ar->mac_addr);
	}

	if (arvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ret = ath11k_mac_monitor_stop(ar);
		if (ret) {
			ath11k_warn(ar->ab, "failed to stop monitor during vif channel context unassignment: %d",
				    ret);
			mutex_unlock(&ar->conf_mutex);
			return;
		}

		arvif->is_started = false;
		mutex_unlock(&ar->conf_mutex);
		return;
	}

	ret = ath11k_mac_vdev_stop(arvif);
	if (ret)
		ath11k_warn(ab, "failed to stop vdev %i: %d\n",
			    arvif->vdev_id, ret);

	arvif->is_started = false;

	if (ab->hw_params.vdev_start_delay &&
	    arvif->vdev_type == WMI_VDEV_TYPE_STA) {
		ret = ath11k_peer_delete(ar, arvif->vdev_id, arvif->bssid);
		if (ret)
			ath11k_warn(ar->ab,
				    "failed to delete peer %pM for vdev %d: %d\n",
				    arvif->bssid, arvif->vdev_id, ret);
		else
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
				   "mac removed peer %pM  vdev %d after vdev stop\n",
				   arvif->bssid, arvif->vdev_id);
	}

	if (ab->hw_params.vdev_start_delay &&
	    arvif->vdev_type == WMI_VDEV_TYPE_MONITOR)
		ath11k_wmi_vdev_down(ar, arvif->vdev_id);

	if (arvif->vdev_type != WMI_VDEV_TYPE_MONITOR &&
	    ar->num_started_vdevs == 1 &&
	    test_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED, &ar->monitor_flags)) {
		ret = ath11k_mac_monitor_stop(ar);
		if (ret)
			/* continue even if there's an error */
			ath11k_warn(ar->ab, "failed to stop monitor during vif channel context unassignment: %d",
				    ret);
	}

	if (arvif->vdev_type == WMI_VDEV_TYPE_STA)
		ath11k_mac_11d_scan_start(ar, arvif->vdev_id);

	mutex_unlock(&ar->conf_mutex);
}

static int
ath11k_mac_op_switch_vif_chanctx(struct ieee80211_hw *hw,
				 struct ieee80211_vif_chanctx_switch *vifs,
				 int n_vifs,
				 enum ieee80211_chanctx_switch_mode mode)
{
	struct ath11k *ar = hw->priv;

	mutex_lock(&ar->conf_mutex);

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "mac chanctx switch n_vifs %d mode %d\n",
		   n_vifs, mode);
	ath11k_mac_update_vif_chan(ar, vifs, n_vifs);

	mutex_unlock(&ar->conf_mutex);

	return 0;
}

static int
ath11k_set_vdev_param_to_all_vifs(struct ath11k *ar, int param, u32 value)
{
	struct ath11k_vif *arvif;
	int ret = 0;

	mutex_lock(&ar->conf_mutex);
	list_for_each_entry(arvif, &ar->arvifs, list) {
		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "setting mac vdev %d param %d value %d\n",
			   param, arvif->vdev_id, value);

		ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    param, value);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set param %d for vdev %d: %d\n",
				    param, arvif->vdev_id, ret);
			break;
		}
	}
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

/* mac80211 stores device specific RTS/Fragmentation threshold value,
 * this is set interface specific to firmware from ath11k driver
 */
static int ath11k_mac_op_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct ath11k *ar = hw->priv;
	int param_id = WMI_VDEV_PARAM_RTS_THRESHOLD;

	return ath11k_set_vdev_param_to_all_vifs(ar, param_id, value);
}

static int ath11k_mac_op_set_frag_threshold(struct ieee80211_hw *hw, u32 value)
{
	/* Even though there's a WMI vdev param for fragmentation threshold no
	 * known firmware actually implements it. Moreover it is not possible to
	 * rely frame fragmentation to mac80211 because firmware clears the
	 * "more fragments" bit in frame control making it impossible for remote
	 * devices to reassemble frames.
	 *
	 * Hence implement a dummy callback just to say fragmentation isn't
	 * supported. This effectively prevents mac80211 from doing frame
	 * fragmentation in software.
	 */
	return -EOPNOTSUPP;
}

static int ath11k_mac_flush_tx_complete(struct ath11k *ar)
{
	long time_left;
	int ret = 0;

	time_left = wait_event_timeout(ar->dp.tx_empty_waitq,
				       (atomic_read(&ar->dp.num_tx_pending) == 0),
				       ATH11K_FLUSH_TIMEOUT);
	if (time_left == 0) {
		ath11k_warn(ar->ab, "failed to flush transmit queue, data pkts pending %d\n",
			    atomic_read(&ar->dp.num_tx_pending));
		ret = -ETIMEDOUT;
	}

	time_left = wait_event_timeout(ar->txmgmt_empty_waitq,
				       (atomic_read(&ar->num_pending_mgmt_tx) == 0),
				       ATH11K_FLUSH_TIMEOUT);
	if (time_left == 0) {
		ath11k_warn(ar->ab, "failed to flush mgmt transmit queue, mgmt pkts pending %d\n",
			    atomic_read(&ar->num_pending_mgmt_tx));
		ret = -ETIMEDOUT;
	}

	return ret;
}

int ath11k_mac_wait_tx_complete(struct ath11k *ar)
{
	ath11k_mac_drain_tx(ar);
	return ath11k_mac_flush_tx_complete(ar);
}

static void ath11k_mac_op_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
				u32 queues, bool drop)
{
	struct ath11k *ar = hw->priv;

	if (drop)
		return;

	ath11k_mac_flush_tx_complete(ar);
}

static int
ath11k_mac_bitrate_mask_num_ht_rates(struct ath11k *ar,
				     enum nl80211_band band,
				     const struct cfg80211_bitrate_mask *mask)
{
	int num_rates = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(mask->control[band].ht_mcs); i++)
		num_rates += hweight16(mask->control[band].ht_mcs[i]);

	return num_rates;
}

static bool
ath11k_mac_has_single_legacy_rate(struct ath11k *ar,
				  enum nl80211_band band,
				  const struct cfg80211_bitrate_mask *mask)
{
	int num_rates = 0;

	num_rates = hweight32(mask->control[band].legacy);

	if (ath11k_mac_bitrate_mask_num_ht_rates(ar, band, mask))
		return false;

	if (ath11k_mac_bitrate_mask_num_vht_rates(ar, band, mask))
		return false;

	if (ath11k_mac_bitrate_mask_num_he_rates(ar, band, mask))
		return false;

	return num_rates == 1;
}

static __le16
ath11k_mac_get_tx_mcs_map(const struct ieee80211_sta_he_cap *he_cap)
{
	if (he_cap->he_cap_elem.phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
		return he_cap->he_mcs_nss_supp.tx_mcs_80p80;

	if (he_cap->he_cap_elem.phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G)
		return he_cap->he_mcs_nss_supp.tx_mcs_160;

	return he_cap->he_mcs_nss_supp.tx_mcs_80;
}

static bool
ath11k_mac_bitrate_mask_get_single_nss(struct ath11k *ar,
				       enum nl80211_band band,
				       const struct cfg80211_bitrate_mask *mask,
				       int *nss)
{
	struct ieee80211_supported_band *sband = &ar->mac.sbands[band];
	u16 vht_mcs_map = le16_to_cpu(sband->vht_cap.vht_mcs.tx_mcs_map);
	u16 he_mcs_map = 0;
	u8 ht_nss_mask = 0;
	u8 vht_nss_mask = 0;
	u8 he_nss_mask = 0;
	int i;

	/* No need to consider legacy here. Basic rates are always present
	 * in bitrate mask
	 */

	for (i = 0; i < ARRAY_SIZE(mask->control[band].ht_mcs); i++) {
		if (mask->control[band].ht_mcs[i] == 0)
			continue;
		else if (mask->control[band].ht_mcs[i] ==
			 sband->ht_cap.mcs.rx_mask[i])
			ht_nss_mask |= BIT(i);
		else
			return false;
	}

	for (i = 0; i < ARRAY_SIZE(mask->control[band].vht_mcs); i++) {
		if (mask->control[band].vht_mcs[i] == 0)
			continue;
		else if (mask->control[band].vht_mcs[i] ==
			 ath11k_mac_get_max_vht_mcs_map(vht_mcs_map, i))
			vht_nss_mask |= BIT(i);
		else
			return false;
	}

	he_mcs_map = le16_to_cpu(ath11k_mac_get_tx_mcs_map(&sband->iftype_data->he_cap));

	for (i = 0; i < ARRAY_SIZE(mask->control[band].he_mcs); i++) {
		if (mask->control[band].he_mcs[i] == 0)
			continue;

		if (mask->control[band].he_mcs[i] ==
		    ath11k_mac_get_max_he_mcs_map(he_mcs_map, i))
			he_nss_mask |= BIT(i);
		else
			return false;
	}

	if (ht_nss_mask != vht_nss_mask || ht_nss_mask != he_nss_mask)
		return false;

	if (ht_nss_mask == 0)
		return false;

	if (BIT(fls(ht_nss_mask)) - 1 != ht_nss_mask)
		return false;

	*nss = fls(ht_nss_mask);

	return true;
}

static int
ath11k_mac_get_single_legacy_rate(struct ath11k *ar,
				  enum nl80211_band band,
				  const struct cfg80211_bitrate_mask *mask,
				  u32 *rate, u8 *nss)
{
	int rate_idx;
	u16 bitrate;
	u8 preamble;
	u8 hw_rate;

	if (hweight32(mask->control[band].legacy) != 1)
		return -EINVAL;

	rate_idx = ffs(mask->control[band].legacy) - 1;

	if (band == NL80211_BAND_5GHZ || band == NL80211_BAND_6GHZ)
		rate_idx += ATH11K_MAC_FIRST_OFDM_RATE_IDX;

	hw_rate = ath11k_legacy_rates[rate_idx].hw_value;
	bitrate = ath11k_legacy_rates[rate_idx].bitrate;

	if (ath11k_mac_bitrate_is_cck(bitrate))
		preamble = WMI_RATE_PREAMBLE_CCK;
	else
		preamble = WMI_RATE_PREAMBLE_OFDM;

	*nss = 1;
	*rate = ATH11K_HW_RATE_CODE(hw_rate, 0, preamble);

	return 0;
}

static int
ath11k_mac_set_fixed_rate_gi_ltf(struct ath11k_vif *arvif, u8 he_gi, u8 he_ltf)
{
	struct ath11k *ar = arvif->ar;
	int ret;

	/* 0.8 = 0, 1.6 = 2 and 3.2 = 3. */
	if (he_gi && he_gi != 0xFF)
		he_gi += 1;

	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_SGI, he_gi);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set he gi %d: %d\n",
			    he_gi, ret);
		return ret;
	}
	/* start from 1 */
	if (he_ltf != 0xFF)
		he_ltf += 1;

	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_HE_LTF, he_ltf);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set he ltf %d: %d\n",
			    he_ltf, ret);
		return ret;
	}

	return 0;
}

static int
ath11k_mac_set_auto_rate_gi_ltf(struct ath11k_vif *arvif, u16 he_gi, u8 he_ltf)
{
	struct ath11k *ar = arvif->ar;
	int ret;
	u32 he_ar_gi_ltf;

	if (he_gi != 0xFF) {
		switch (he_gi) {
		case NL80211_RATE_INFO_HE_GI_0_8:
			he_gi = WMI_AUTORATE_800NS_GI;
			break;
		case NL80211_RATE_INFO_HE_GI_1_6:
			he_gi = WMI_AUTORATE_1600NS_GI;
			break;
		case NL80211_RATE_INFO_HE_GI_3_2:
			he_gi = WMI_AUTORATE_3200NS_GI;
			break;
		default:
			ath11k_warn(ar->ab, "invalid he gi: %d\n", he_gi);
			return -EINVAL;
		}
	}

	if (he_ltf != 0xFF) {
		switch (he_ltf) {
		case NL80211_RATE_INFO_HE_1XLTF:
			he_ltf = WMI_HE_AUTORATE_LTF_1X;
			break;
		case NL80211_RATE_INFO_HE_2XLTF:
			he_ltf = WMI_HE_AUTORATE_LTF_2X;
			break;
		case NL80211_RATE_INFO_HE_4XLTF:
			he_ltf = WMI_HE_AUTORATE_LTF_4X;
			break;
		default:
			ath11k_warn(ar->ab, "invalid he ltf: %d\n", he_ltf);
			return -EINVAL;
		}
	}

	he_ar_gi_ltf = he_gi | he_ltf;
	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_AUTORATE_MISC_CFG,
					    he_ar_gi_ltf);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to set he autorate gi %u ltf %u: %d\n",
			    he_gi, he_ltf, ret);
		return ret;
	}

	return 0;
}

static int ath11k_mac_set_rate_params(struct ath11k_vif *arvif,
				      u32 rate, u8 nss, u8 sgi, u8 ldpc,
				      u8 he_gi, u8 he_ltf, bool he_fixed_rate)
{
	struct ath11k *ar = arvif->ar;
	u32 vdev_param;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "mac set rate params vdev %i rate 0x%02x nss 0x%02x sgi 0x%02x ldpc 0x%02x he_gi 0x%02x he_ltf 0x%02x he_fixed_rate %d\n",
		   arvif->vdev_id, rate, nss, sgi, ldpc, he_gi,
		   he_ltf, he_fixed_rate);

	if (!arvif->vif->bss_conf.he_support) {
		vdev_param = WMI_VDEV_PARAM_FIXED_RATE;
		ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    vdev_param, rate);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set fixed rate param 0x%02x: %d\n",
				    rate, ret);
			return ret;
		}
	}

	vdev_param = WMI_VDEV_PARAM_NSS;
	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    vdev_param, nss);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set nss param %d: %d\n",
			    nss, ret);
		return ret;
	}

	vdev_param = WMI_VDEV_PARAM_LDPC;
	ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    vdev_param, ldpc);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set ldpc param %d: %d\n",
			    ldpc, ret);
		return ret;
	}

	if (arvif->vif->bss_conf.he_support) {
		if (he_fixed_rate) {
			ret = ath11k_mac_set_fixed_rate_gi_ltf(arvif, he_gi,
							       he_ltf);
			if (ret) {
				ath11k_warn(ar->ab, "failed to set fixed rate gi ltf: %d\n",
					    ret);
				return ret;
			}
		} else {
			ret = ath11k_mac_set_auto_rate_gi_ltf(arvif, he_gi,
							      he_ltf);
			if (ret) {
				ath11k_warn(ar->ab, "failed to set auto rate gi ltf: %d\n",
					    ret);
				return ret;
			}
		}
	} else {
		vdev_param = WMI_VDEV_PARAM_SGI;
		ret = ath11k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    vdev_param, sgi);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set sgi param %d: %d\n",
				    sgi, ret);
			return ret;
		}
	}

	return 0;
}

static bool
ath11k_mac_vht_mcs_range_present(struct ath11k *ar,
				 enum nl80211_band band,
				 const struct cfg80211_bitrate_mask *mask)
{
	int i;
	u16 vht_mcs;

	for (i = 0; i < NL80211_VHT_NSS_MAX; i++) {
		vht_mcs = mask->control[band].vht_mcs[i];

		switch (vht_mcs) {
		case 0:
		case BIT(8) - 1:
		case BIT(9) - 1:
		case BIT(10) - 1:
			break;
		default:
			return false;
		}
	}

	return true;
}

static bool
ath11k_mac_he_mcs_range_present(struct ath11k *ar,
				enum nl80211_band band,
				const struct cfg80211_bitrate_mask *mask)
{
	int i;
	u16 he_mcs;

	for (i = 0; i < NL80211_HE_NSS_MAX; i++) {
		he_mcs = mask->control[band].he_mcs[i];

		switch (he_mcs) {
		case 0:
		case BIT(8) - 1:
		case BIT(10) - 1:
		case BIT(12) - 1:
			break;
		default:
			return false;
		}
	}

	return true;
}

static void ath11k_mac_set_bitrate_mask_iter(void *data,
					     struct ieee80211_sta *sta)
{
	struct ath11k_vif *arvif = data;
	struct ath11k_sta *arsta = (struct ath11k_sta *)sta->drv_priv;
	struct ath11k *ar = arvif->ar;

	spin_lock_bh(&ar->data_lock);
	arsta->changed |= IEEE80211_RC_SUPP_RATES_CHANGED;
	spin_unlock_bh(&ar->data_lock);

	ieee80211_queue_work(ar->hw, &arsta->update_wk);
}

static void ath11k_mac_disable_peer_fixed_rate(void *data,
					       struct ieee80211_sta *sta)
{
	struct ath11k_vif *arvif = data;
	struct ath11k *ar = arvif->ar;
	int ret;

	ret = ath11k_wmi_set_peer_param(ar, sta->addr,
					arvif->vdev_id,
					WMI_PEER_PARAM_FIXED_RATE,
					WMI_FIXED_RATE_NONE);
	if (ret)
		ath11k_warn(ar->ab,
			    "failed to disable peer fixed rate for STA %pM ret %d\n",
			    sta->addr, ret);
}

static bool
ath11k_mac_validate_vht_he_fixed_rate_settings(struct ath11k *ar, enum nl80211_band band,
					       const struct cfg80211_bitrate_mask *mask)
{
	bool he_fixed_rate = false, vht_fixed_rate = false;
	struct ath11k_peer *peer, *tmp;
	const u16 *vht_mcs_mask, *he_mcs_mask;
	struct ieee80211_link_sta *deflink;
	u8 vht_nss, he_nss;
	bool ret = true;

	vht_mcs_mask = mask->control[band].vht_mcs;
	he_mcs_mask = mask->control[band].he_mcs;

	if (ath11k_mac_bitrate_mask_num_vht_rates(ar, band, mask) == 1)
		vht_fixed_rate = true;

	if (ath11k_mac_bitrate_mask_num_he_rates(ar, band, mask) == 1)
		he_fixed_rate = true;

	if (!vht_fixed_rate && !he_fixed_rate)
		return true;

	vht_nss = ath11k_mac_max_vht_nss(vht_mcs_mask);
	he_nss =  ath11k_mac_max_he_nss(he_mcs_mask);

	rcu_read_lock();
	spin_lock_bh(&ar->ab->base_lock);
	list_for_each_entry_safe(peer, tmp, &ar->ab->peers, list) {
		if (peer->sta) {
			deflink = &peer->sta->deflink;

			if (vht_fixed_rate && (!deflink->vht_cap.vht_supported ||
					       deflink->rx_nss < vht_nss)) {
				ret = false;
				goto out;
			}

			if (he_fixed_rate && (!deflink->he_cap.has_he ||
					      deflink->rx_nss < he_nss)) {
				ret = false;
				goto out;
			}
		}
	}

out:
	spin_unlock_bh(&ar->ab->base_lock);
	rcu_read_unlock();
	return ret;
}

static int
ath11k_mac_op_set_bitrate_mask(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       const struct cfg80211_bitrate_mask *mask)
{
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct cfg80211_chan_def def;
	struct ath11k_pdev_cap *cap;
	struct ath11k *ar = arvif->ar;
	enum nl80211_band band;
	const u8 *ht_mcs_mask;
	const u16 *vht_mcs_mask;
	const u16 *he_mcs_mask;
	u8 he_ltf = 0;
	u8 he_gi = 0;
	u32 rate;
	u8 nss;
	u8 sgi;
	u8 ldpc;
	int single_nss;
	int ret;
	int num_rates;
	bool he_fixed_rate = false;

	if (ath11k_mac_vif_chan(vif, &def))
		return -EPERM;

	band = def.chan->band;
	cap = &ar->pdev->cap;
	ht_mcs_mask = mask->control[band].ht_mcs;
	vht_mcs_mask = mask->control[band].vht_mcs;
	he_mcs_mask = mask->control[band].he_mcs;
	ldpc = !!(cap->band[band].ht_cap_info & WMI_HT_CAP_TX_LDPC);

	sgi = mask->control[band].gi;
	if (sgi == NL80211_TXRATE_FORCE_LGI)
		return -EINVAL;

	he_gi = mask->control[band].he_gi;
	he_ltf = mask->control[band].he_ltf;

	/* mac80211 doesn't support sending a fixed HT/VHT MCS alone, rather it
	 * requires passing at least one of used basic rates along with them.
	 * Fixed rate setting across different preambles(legacy, HT, VHT) is
	 * not supported by the FW. Hence use of FIXED_RATE vdev param is not
	 * suitable for setting single HT/VHT rates.
	 * But, there could be a single basic rate passed from userspace which
	 * can be done through the FIXED_RATE param.
	 */
	if (ath11k_mac_has_single_legacy_rate(ar, band, mask)) {
		ret = ath11k_mac_get_single_legacy_rate(ar, band, mask, &rate,
							&nss);
		if (ret) {
			ath11k_warn(ar->ab, "failed to get single legacy rate for vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
		ieee80211_iterate_stations_atomic(ar->hw,
						  ath11k_mac_disable_peer_fixed_rate,
						  arvif);
	} else if (ath11k_mac_bitrate_mask_get_single_nss(ar, band, mask,
							  &single_nss)) {
		rate = WMI_FIXED_RATE_NONE;
		nss = single_nss;
		mutex_lock(&ar->conf_mutex);
		arvif->bitrate_mask = *mask;
		ieee80211_iterate_stations_atomic(ar->hw,
						  ath11k_mac_set_bitrate_mask_iter,
						  arvif);
		mutex_unlock(&ar->conf_mutex);
	} else {
		rate = WMI_FIXED_RATE_NONE;

		if (!ath11k_mac_validate_vht_he_fixed_rate_settings(ar, band, mask))
			ath11k_warn(ar->ab,
				    "could not update fixed rate settings to all peers due to mcs/nss incompatibility\n");
		nss = min_t(u32, ar->num_tx_chains,
			    max(max(ath11k_mac_max_ht_nss(ht_mcs_mask),
				    ath11k_mac_max_vht_nss(vht_mcs_mask)),
				ath11k_mac_max_he_nss(he_mcs_mask)));

		/* If multiple rates across different preambles are given
		 * we can reconfigure this info with all peers using PEER_ASSOC
		 * command with the below exception cases.
		 * - Single VHT Rate : peer_assoc command accommodates only MCS
		 * range values i.e 0-7, 0-8, 0-9 for VHT. Though mac80211
		 * mandates passing basic rates along with HT/VHT rates, FW
		 * doesn't allow switching from VHT to Legacy. Hence instead of
		 * setting legacy and VHT rates using RATEMASK_CMD vdev cmd,
		 * we could set this VHT rate as peer fixed rate param, which
		 * will override FIXED rate and FW rate control algorithm.
		 * If single VHT rate is passed along with HT rates, we select
		 * the VHT rate as fixed rate for vht peers.
		 * - Multiple VHT Rates : When Multiple VHT rates are given,this
		 * can be set using RATEMASK CMD which uses FW rate-ctl alg.
		 * TODO: Setting multiple VHT MCS and replacing peer_assoc with
		 * RATEMASK_CMDID can cover all use cases of setting rates
		 * across multiple preambles and rates within same type.
		 * But requires more validation of the command at this point.
		 */

		num_rates = ath11k_mac_bitrate_mask_num_vht_rates(ar, band,
								  mask);

		if (!ath11k_mac_vht_mcs_range_present(ar, band, mask) &&
		    num_rates > 1) {
			/* TODO: Handle multiple VHT MCS values setting using
			 * RATEMASK CMD
			 */
			ath11k_warn(ar->ab,
				    "setting %d mcs values in bitrate mask not supported\n",
				num_rates);
			return -EINVAL;
		}

		num_rates = ath11k_mac_bitrate_mask_num_he_rates(ar, band,
								 mask);
		if (num_rates == 1)
			he_fixed_rate = true;

		if (!ath11k_mac_he_mcs_range_present(ar, band, mask) &&
		    num_rates > 1) {
			ath11k_warn(ar->ab,
				    "Setting more than one HE MCS Value in bitrate mask not supported\n");
			return -EINVAL;
		}

		mutex_lock(&ar->conf_mutex);
		ieee80211_iterate_stations_atomic(ar->hw,
						  ath11k_mac_disable_peer_fixed_rate,
						  arvif);

		arvif->bitrate_mask = *mask;
		ieee80211_iterate_stations_atomic(ar->hw,
						  ath11k_mac_set_bitrate_mask_iter,
						  arvif);

		mutex_unlock(&ar->conf_mutex);
	}

	mutex_lock(&ar->conf_mutex);

	ret = ath11k_mac_set_rate_params(arvif, rate, nss, sgi, ldpc, he_gi,
					 he_ltf, he_fixed_rate);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set rate params on vdev %i: %d\n",
			    arvif->vdev_id, ret);
	}

	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static void
ath11k_mac_op_reconfig_complete(struct ieee80211_hw *hw,
				enum ieee80211_reconfig_type reconfig_type)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;
	int recovery_count;
	struct ath11k_vif *arvif;

	if (reconfig_type != IEEE80211_RECONFIG_TYPE_RESTART)
		return;

	mutex_lock(&ar->conf_mutex);

	if (ar->state == ATH11K_STATE_RESTARTED) {
		ath11k_warn(ar->ab, "pdev %d successfully recovered\n",
			    ar->pdev->pdev_id);
		ar->state = ATH11K_STATE_ON;
		ieee80211_wake_queues(ar->hw);

		if (ar->ab->hw_params.current_cc_support &&
		    ar->alpha2[0] != 0 && ar->alpha2[1] != 0) {
			struct wmi_set_current_country_params set_current_param = {};

			memcpy(&set_current_param.alpha2, ar->alpha2, 2);
			ath11k_wmi_send_set_current_country_cmd(ar, &set_current_param);
		}

		if (ab->is_reset) {
			recovery_count = atomic_inc_return(&ab->recovery_count);
			ath11k_dbg(ab, ATH11K_DBG_BOOT,
				   "recovery count %d\n", recovery_count);
			/* When there are multiple radios in an SOC,
			 * the recovery has to be done for each radio
			 */
			if (recovery_count == ab->num_radios) {
				atomic_dec(&ab->reset_count);
				complete(&ab->reset_complete);
				ab->is_reset = false;
				atomic_set(&ab->fail_cont_count, 0);
				ath11k_dbg(ab, ATH11K_DBG_BOOT, "reset success\n");
			}
		}
		if (ar->ab->hw_params.support_fw_mac_sequence) {
			list_for_each_entry(arvif, &ar->arvifs, list) {
				if (arvif->is_up && arvif->vdev_type == WMI_VDEV_TYPE_STA)
					ieee80211_hw_restart_disconnect(arvif->vif);
			}
		}
	}

	mutex_unlock(&ar->conf_mutex);
}

static void
ath11k_mac_update_bss_chan_survey(struct ath11k *ar,
				  struct ieee80211_channel *channel)
{
	int ret;
	enum wmi_bss_chan_info_req_type type = WMI_BSS_SURVEY_REQ_TYPE_READ;

	lockdep_assert_held(&ar->conf_mutex);

	if (!test_bit(WMI_TLV_SERVICE_BSS_CHANNEL_INFO_64, ar->ab->wmi_ab.svc_map) ||
	    ar->rx_channel != channel)
		return;

	if (ar->scan.state != ATH11K_SCAN_IDLE) {
		ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
			   "ignoring bss chan info req while scanning..\n");
		return;
	}

	reinit_completion(&ar->bss_survey_done);

	ret = ath11k_wmi_pdev_bss_chan_info_request(ar, type);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send pdev bss chan info request\n");
		return;
	}

	ret = wait_for_completion_timeout(&ar->bss_survey_done, 3 * HZ);
	if (ret == 0)
		ath11k_warn(ar->ab, "bss channel survey timed out\n");
}

static int ath11k_mac_op_get_survey(struct ieee80211_hw *hw, int idx,
				    struct survey_info *survey)
{
	struct ath11k *ar = hw->priv;
	struct ieee80211_supported_band *sband;
	struct survey_info *ar_survey;
	int ret = 0;

	if (idx >= ATH11K_NUM_CHANS)
		return -ENOENT;

	ar_survey = &ar->survey[idx];

	mutex_lock(&ar->conf_mutex);

	sband = hw->wiphy->bands[NL80211_BAND_2GHZ];
	if (sband && idx >= sband->n_channels) {
		idx -= sband->n_channels;
		sband = NULL;
	}

	if (!sband)
		sband = hw->wiphy->bands[NL80211_BAND_5GHZ];
	if (sband && idx >= sband->n_channels) {
		idx -= sband->n_channels;
		sband = NULL;
	}

	if (!sband)
		sband = hw->wiphy->bands[NL80211_BAND_6GHZ];
	if (!sband || idx >= sband->n_channels) {
		ret = -ENOENT;
		goto exit;
	}

	ath11k_mac_update_bss_chan_survey(ar, &sband->channels[idx]);

	spin_lock_bh(&ar->data_lock);
	memcpy(survey, ar_survey, sizeof(*survey));
	spin_unlock_bh(&ar->data_lock);

	survey->channel = &sband->channels[idx];

	if (ar->rx_channel == survey->channel)
		survey->filled |= SURVEY_INFO_IN_USE;

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static void ath11k_mac_put_chain_rssi(struct station_info *sinfo,
				      struct ath11k_sta *arsta,
				      char *pre,
				      bool clear)
{
	struct ath11k *ar = arsta->arvif->ar;
	int i;
	s8 rssi;

	for (i = 0; i < ARRAY_SIZE(sinfo->chain_signal); i++) {
		sinfo->chains &= ~BIT(i);
		rssi = arsta->chain_signal[i];
		if (clear)
			arsta->chain_signal[i] = ATH11K_INVALID_RSSI_FULL;

		ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
			   "mac sta statistics %s rssi[%d] %d\n", pre, i, rssi);

		if (rssi != ATH11K_DEFAULT_NOISE_FLOOR &&
		    rssi != ATH11K_INVALID_RSSI_FULL &&
		    rssi != ATH11K_INVALID_RSSI_EMPTY &&
		    rssi != 0) {
			sinfo->chain_signal[i] = rssi;
			sinfo->chains |= BIT(i);
			sinfo->filled |= BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL);
		}
	}
}

static void ath11k_mac_op_sta_statistics(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta,
					 struct station_info *sinfo)
{
	struct ath11k_sta *arsta = (struct ath11k_sta *)sta->drv_priv;
	struct ath11k *ar = arsta->arvif->ar;
	s8 signal;
	bool db2dbm = test_bit(WMI_TLV_SERVICE_HW_DB2DBM_CONVERSION_SUPPORT,
			       ar->ab->wmi_ab.svc_map);

	sinfo->rx_duration = arsta->rx_duration;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_DURATION);

	sinfo->tx_duration = arsta->tx_duration;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_DURATION);

	if (arsta->txrate.legacy || arsta->txrate.nss) {
		if (arsta->txrate.legacy) {
			sinfo->txrate.legacy = arsta->txrate.legacy;
		} else {
			sinfo->txrate.mcs = arsta->txrate.mcs;
			sinfo->txrate.nss = arsta->txrate.nss;
			sinfo->txrate.bw = arsta->txrate.bw;
			sinfo->txrate.he_gi = arsta->txrate.he_gi;
			sinfo->txrate.he_dcm = arsta->txrate.he_dcm;
			sinfo->txrate.he_ru_alloc = arsta->txrate.he_ru_alloc;
		}
		sinfo->txrate.flags = arsta->txrate.flags;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
	}

	ath11k_mac_put_chain_rssi(sinfo, arsta, "ppdu", false);

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL)) &&
	    arsta->arvif->vdev_type == WMI_VDEV_TYPE_STA &&
	    ar->ab->hw_params.supports_rssi_stats &&
	    !ath11k_debugfs_get_fw_stats(ar, ar->pdev->pdev_id, 0,
					 WMI_REQUEST_RSSI_PER_CHAIN_STAT)) {
		ath11k_mac_put_chain_rssi(sinfo, arsta, "fw stats", true);
	}

	signal = arsta->rssi_comb;
	if (!signal &&
	    arsta->arvif->vdev_type == WMI_VDEV_TYPE_STA &&
	    ar->ab->hw_params.supports_rssi_stats &&
	    !(ath11k_debugfs_get_fw_stats(ar, ar->pdev->pdev_id, 0,
					WMI_REQUEST_VDEV_STAT)))
		signal = arsta->rssi_beacon;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC,
		   "mac sta statistics db2dbm %u rssi comb %d rssi beacon %d\n",
		   db2dbm, arsta->rssi_comb, arsta->rssi_beacon);

	if (signal) {
		sinfo->signal = db2dbm ? signal : signal + ATH11K_DEFAULT_NOISE_FLOOR;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);
	}

	sinfo->signal_avg = ewma_avg_rssi_read(&arsta->avg_rssi) +
		ATH11K_DEFAULT_NOISE_FLOOR;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL_AVG);
}

#if IS_ENABLED(CONFIG_IPV6)
static void ath11k_generate_ns_mc_addr(struct ath11k *ar,
				       struct ath11k_arp_ns_offload *offload)
{
	int i;

	for (i = 0; i < offload->ipv6_count; i++) {
		offload->self_ipv6_addr[i][0] = 0xff;
		offload->self_ipv6_addr[i][1] = 0x02;
		offload->self_ipv6_addr[i][11] = 0x01;
		offload->self_ipv6_addr[i][12] = 0xff;
		offload->self_ipv6_addr[i][13] =
					offload->ipv6_addr[i][13];
		offload->self_ipv6_addr[i][14] =
					offload->ipv6_addr[i][14];
		offload->self_ipv6_addr[i][15] =
					offload->ipv6_addr[i][15];
		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "NS solicited addr %pI6\n",
			   offload->self_ipv6_addr[i]);
	}
}

static void ath11k_mac_op_ipv6_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct inet6_dev *idev)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_arp_ns_offload *offload;
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	struct inet6_ifaddr *ifa6;
	struct ifacaddr6 *ifaca6;
	struct list_head *p;
	u32 count, scope;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac op ipv6 changed\n");

	offload = &arvif->arp_ns_offload;
	count = 0;

	read_lock_bh(&idev->lock);

	memset(offload->ipv6_addr, 0, sizeof(offload->ipv6_addr));
	memset(offload->self_ipv6_addr, 0, sizeof(offload->self_ipv6_addr));
	memcpy(offload->mac_addr, vif->addr, ETH_ALEN);

	/* get unicast address */
	list_for_each(p, &idev->addr_list) {
		if (count >= ATH11K_IPV6_MAX_COUNT)
			goto generate;

		ifa6 = list_entry(p, struct inet6_ifaddr, if_list);
		if (ifa6->flags & IFA_F_DADFAILED)
			continue;
		scope = ipv6_addr_src_scope(&ifa6->addr);
		if (scope == IPV6_ADDR_SCOPE_LINKLOCAL ||
		    scope == IPV6_ADDR_SCOPE_GLOBAL) {
			memcpy(offload->ipv6_addr[count], &ifa6->addr.s6_addr,
			       sizeof(ifa6->addr.s6_addr));
			offload->ipv6_type[count] = ATH11K_IPV6_UC_TYPE;
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac count %d ipv6 uc %pI6 scope %d\n",
				   count, offload->ipv6_addr[count],
				   scope);
			count++;
		} else {
			ath11k_warn(ar->ab, "Unsupported ipv6 scope: %d\n", scope);
		}
	}

	/* get anycast address */
	for (ifaca6 = idev->ac_list; ifaca6; ifaca6 = ifaca6->aca_next) {
		if (count >= ATH11K_IPV6_MAX_COUNT)
			goto generate;

		scope = ipv6_addr_src_scope(&ifaca6->aca_addr);
		if (scope == IPV6_ADDR_SCOPE_LINKLOCAL ||
		    scope == IPV6_ADDR_SCOPE_GLOBAL) {
			memcpy(offload->ipv6_addr[count], &ifaca6->aca_addr,
			       sizeof(ifaca6->aca_addr));
			offload->ipv6_type[count] = ATH11K_IPV6_AC_TYPE;
			ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac count %d ipv6 ac %pI6 scope %d\n",
				   count, offload->ipv6_addr[count],
				   scope);
			count++;
		} else {
			ath11k_warn(ar->ab, "Unsupported ipv scope: %d\n", scope);
		}
	}

generate:
	offload->ipv6_count = count;
	read_unlock_bh(&idev->lock);

	/* generate ns multicast address */
	ath11k_generate_ns_mc_addr(ar, offload);
}
#endif

static void ath11k_mac_op_set_rekey_data(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct cfg80211_gtk_rekey_data *data)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_vif *arvif = ath11k_vif_to_arvif(vif);
	struct ath11k_rekey_data *rekey_data = &arvif->rekey_data;

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "mac set rekey data vdev %d\n",
		   arvif->vdev_id);

	mutex_lock(&ar->conf_mutex);

	memcpy(rekey_data->kck, data->kck, NL80211_KCK_LEN);
	memcpy(rekey_data->kek, data->kek, NL80211_KEK_LEN);

	/* The supplicant works on big-endian, the firmware expects it on
	 * little endian.
	 */
	rekey_data->replay_ctr = get_unaligned_be64(data->replay_ctr);

	arvif->rekey_data.enable_offload = true;

	ath11k_dbg_dump(ar->ab, ATH11K_DBG_MAC, "kck", NULL,
			rekey_data->kck, NL80211_KCK_LEN);
	ath11k_dbg_dump(ar->ab, ATH11K_DBG_MAC, "kek", NULL,
			rekey_data->kck, NL80211_KEK_LEN);
	ath11k_dbg_dump(ar->ab, ATH11K_DBG_MAC, "replay ctr", NULL,
			&rekey_data->replay_ctr, sizeof(rekey_data->replay_ctr));

	mutex_unlock(&ar->conf_mutex);
}

static int ath11k_mac_op_set_bios_sar_specs(struct ieee80211_hw *hw,
					    const struct cfg80211_sar_specs *sar)
{
	struct ath11k *ar = hw->priv;
	const struct cfg80211_sar_sub_specs *sspec;
	int ret, index;
	u8 *sar_tbl;
	u32 i;

	if (!sar || sar->type != NL80211_SAR_TYPE_POWER ||
	    sar->num_sub_specs == 0)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	if (!test_bit(WMI_TLV_SERVICE_BIOS_SAR_SUPPORT, ar->ab->wmi_ab.svc_map) ||
	    !ar->ab->hw_params.bios_sar_capa) {
		ret = -EOPNOTSUPP;
		goto exit;
	}

	ret = ath11k_wmi_pdev_set_bios_geo_table_param(ar);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set geo table: %d\n", ret);
		goto exit;
	}

	sar_tbl = kzalloc(BIOS_SAR_TABLE_LEN, GFP_KERNEL);
	if (!sar_tbl) {
		ret = -ENOMEM;
		goto exit;
	}

	sspec = sar->sub_specs;
	for (i = 0; i < sar->num_sub_specs; i++) {
		if (sspec->freq_range_index >= (BIOS_SAR_TABLE_LEN >> 1)) {
			ath11k_warn(ar->ab, "Ignore bad frequency index %u, max allowed %u\n",
				    sspec->freq_range_index, BIOS_SAR_TABLE_LEN >> 1);
			continue;
		}

		/* chain0 and chain1 share same power setting */
		sar_tbl[sspec->freq_range_index] = sspec->power;
		index = sspec->freq_range_index + (BIOS_SAR_TABLE_LEN >> 1);
		sar_tbl[index] = sspec->power;
		ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "sar tbl[%d] = %d\n",
			   sspec->freq_range_index, sar_tbl[sspec->freq_range_index]);
		sspec++;
	}

	ret = ath11k_wmi_pdev_set_bios_sar_table_param(ar, sar_tbl);
	if (ret)
		ath11k_warn(ar->ab, "failed to set sar power: %d", ret);

	kfree(sar_tbl);
exit:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static int ath11k_mac_op_cancel_remain_on_channel(struct ieee80211_hw *hw,
						  struct ieee80211_vif *vif)
{
	struct ath11k *ar = hw->priv;

	mutex_lock(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	ar->scan.roc_notify = false;
	spin_unlock_bh(&ar->data_lock);

	ath11k_scan_abort(ar);

	mutex_unlock(&ar->conf_mutex);

	cancel_delayed_work_sync(&ar->scan.timeout);

	return 0;
}

static int ath11k_mac_op_remain_on_channel(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_channel *chan,
					   int duration,
					   enum ieee80211_roc_type type)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_vif *arvif = (void *)vif->drv_priv;
	struct scan_req_params arg;
	int ret;
	u32 scan_time_msec;

	mutex_lock(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	switch (ar->scan.state) {
	case ATH11K_SCAN_IDLE:
		reinit_completion(&ar->scan.started);
		reinit_completion(&ar->scan.completed);
		reinit_completion(&ar->scan.on_channel);
		ar->scan.state = ATH11K_SCAN_STARTING;
		ar->scan.is_roc = true;
		ar->scan.vdev_id = arvif->vdev_id;
		ar->scan.roc_freq = chan->center_freq;
		ar->scan.roc_notify = true;
		ret = 0;
		break;
	case ATH11K_SCAN_STARTING:
	case ATH11K_SCAN_RUNNING:
	case ATH11K_SCAN_ABORTING:
		ret = -EBUSY;
		break;
	}
	spin_unlock_bh(&ar->data_lock);

	if (ret)
		goto exit;

	scan_time_msec = ar->hw->wiphy->max_remain_on_channel_duration * 2;

	memset(&arg, 0, sizeof(arg));
	ath11k_wmi_start_scan_init(ar, &arg);
	arg.num_chan = 1;
	arg.chan_list = kcalloc(arg.num_chan, sizeof(*arg.chan_list),
				GFP_KERNEL);
	if (!arg.chan_list) {
		ret = -ENOMEM;
		goto exit;
	}

	arg.vdev_id = arvif->vdev_id;
	arg.scan_id = ATH11K_SCAN_ID;
	arg.chan_list[0] = chan->center_freq;
	arg.dwell_time_active = scan_time_msec;
	arg.dwell_time_passive = scan_time_msec;
	arg.max_scan_time = scan_time_msec;
	arg.scan_flags |= WMI_SCAN_FLAG_PASSIVE;
	arg.scan_flags |= WMI_SCAN_FILTER_PROBE_REQ;
	arg.burst_duration = duration;

	ret = ath11k_start_scan(ar, &arg);
	if (ret) {
		ath11k_warn(ar->ab, "failed to start roc scan: %d\n", ret);

		spin_lock_bh(&ar->data_lock);
		ar->scan.state = ATH11K_SCAN_IDLE;
		spin_unlock_bh(&ar->data_lock);
		goto free_chan_list;
	}

	ret = wait_for_completion_timeout(&ar->scan.on_channel, 3 * HZ);
	if (ret == 0) {
		ath11k_warn(ar->ab, "failed to switch to channel for roc scan\n");
		ret = ath11k_scan_stop(ar);
		if (ret)
			ath11k_warn(ar->ab, "failed to stop scan: %d\n", ret);
		ret = -ETIMEDOUT;
		goto free_chan_list;
	}

	ieee80211_queue_delayed_work(ar->hw, &ar->scan.timeout,
				     msecs_to_jiffies(duration));

	ret = 0;

free_chan_list:
	kfree(arg.chan_list);
exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath11k_fw_stats_request(struct ath11k *ar,
				   struct stats_request_params *req_param)
{
	struct ath11k_base *ab = ar->ab;
	unsigned long time_left;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	ar->fw_stats_done = false;
	ath11k_fw_stats_pdevs_free(&ar->fw_stats.pdevs);
	spin_unlock_bh(&ar->data_lock);

	reinit_completion(&ar->fw_stats_complete);

	ret = ath11k_wmi_send_stats_request_cmd(ar, req_param);
	if (ret) {
		ath11k_warn(ab, "could not request fw stats (%d)\n",
			    ret);
		return ret;
	}

	time_left = wait_for_completion_timeout(&ar->fw_stats_complete,
						1 * HZ);

	if (!time_left)
		return -ETIMEDOUT;

	return 0;
}

static int ath11k_mac_op_get_txpower(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     int *dbm)
{
	struct ath11k *ar = hw->priv;
	struct ath11k_base *ab = ar->ab;
	struct stats_request_params req_param = {0};
	struct ath11k_fw_stats_pdev *pdev;
	int ret;

	/* Final Tx power is minimum of Target Power, CTL power, Regulatory
	 * Power, PSD EIRP Power. We just know the Regulatory power from the
	 * regulatory rules obtained. FW knows all these power and sets the min
	 * of these. Hence, we request the FW pdev stats in which FW reports
	 * the minimum of all vdev's channel Tx power.
	 */
	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON)
		goto err_fallback;

	req_param.pdev_id = ar->pdev->pdev_id;
	req_param.stats_id = WMI_REQUEST_PDEV_STAT;

	ret = ath11k_fw_stats_request(ar, &req_param);
	if (ret) {
		ath11k_warn(ab, "failed to request fw pdev stats: %d\n", ret);
		goto err_fallback;
	}

	spin_lock_bh(&ar->data_lock);
	pdev = list_first_entry_or_null(&ar->fw_stats.pdevs,
					struct ath11k_fw_stats_pdev, list);
	if (!pdev) {
		spin_unlock_bh(&ar->data_lock);
		goto err_fallback;
	}

	/* tx power is set as 2 units per dBm in FW. */
	*dbm = pdev->chan_tx_power / 2;

	spin_unlock_bh(&ar->data_lock);
	mutex_unlock(&ar->conf_mutex);

	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "txpower from firmware %d, reported %d dBm\n",
		   pdev->chan_tx_power, *dbm);
	return 0;

err_fallback:
	mutex_unlock(&ar->conf_mutex);
	/* We didn't get txpower from FW. Hence, relying on vif->bss_conf.txpower */
	*dbm = vif->bss_conf.txpower;
	ath11k_dbg(ar->ab, ATH11K_DBG_MAC, "txpower from firmware NaN, reported %d dBm\n",
		   *dbm);
	return 0;
}

static const struct ieee80211_ops ath11k_ops = {
	.tx				= ath11k_mac_op_tx,
	.wake_tx_queue			= ieee80211_handle_wake_tx_queue,
	.start                          = ath11k_mac_op_start,
	.stop                           = ath11k_mac_op_stop,
	.reconfig_complete              = ath11k_mac_op_reconfig_complete,
	.add_interface                  = ath11k_mac_op_add_interface,
	.remove_interface		= ath11k_mac_op_remove_interface,
	.update_vif_offload		= ath11k_mac_op_update_vif_offload,
	.config                         = ath11k_mac_op_config,
	.bss_info_changed               = ath11k_mac_op_bss_info_changed,
	.configure_filter		= ath11k_mac_op_configure_filter,
	.hw_scan                        = ath11k_mac_op_hw_scan,
	.cancel_hw_scan                 = ath11k_mac_op_cancel_hw_scan,
	.set_key                        = ath11k_mac_op_set_key,
	.set_rekey_data	                = ath11k_mac_op_set_rekey_data,
	.sta_state                      = ath11k_mac_op_sta_state,
	.sta_set_4addr                  = ath11k_mac_op_sta_set_4addr,
	.sta_set_txpwr			= ath11k_mac_op_sta_set_txpwr,
	.sta_rc_update			= ath11k_mac_op_sta_rc_update,
	.conf_tx                        = ath11k_mac_op_conf_tx,
	.set_antenna			= ath11k_mac_op_set_antenna,
	.get_antenna			= ath11k_mac_op_get_antenna,
	.ampdu_action			= ath11k_mac_op_ampdu_action,
	.add_chanctx			= ath11k_mac_op_add_chanctx,
	.remove_chanctx			= ath11k_mac_op_remove_chanctx,
	.change_chanctx			= ath11k_mac_op_change_chanctx,
	.assign_vif_chanctx		= ath11k_mac_op_assign_vif_chanctx,
	.unassign_vif_chanctx		= ath11k_mac_op_unassign_vif_chanctx,
	.switch_vif_chanctx		= ath11k_mac_op_switch_vif_chanctx,
	.set_rts_threshold		= ath11k_mac_op_set_rts_threshold,
	.set_frag_threshold		= ath11k_mac_op_set_frag_threshold,
	.set_bitrate_mask		= ath11k_mac_op_set_bitrate_mask,
	.get_survey			= ath11k_mac_op_get_survey,
	.flush				= ath11k_mac_op_flush,
	.sta_statistics			= ath11k_mac_op_sta_statistics,
	CFG80211_TESTMODE_CMD(ath11k_tm_cmd)

#ifdef CONFIG_PM
	.suspend			= ath11k_wow_op_suspend,
	.resume				= ath11k_wow_op_resume,
	.set_wakeup			= ath11k_wow_op_set_wakeup,
#endif

#ifdef CONFIG_ATH11K_DEBUGFS
	.sta_add_debugfs		= ath11k_debugfs_sta_op_add,
#endif

#if IS_ENABLED(CONFIG_IPV6)
	.ipv6_addr_change = ath11k_mac_op_ipv6_changed,
#endif
	.get_txpower                    = ath11k_mac_op_get_txpower,

	.set_sar_specs			= ath11k_mac_op_set_bios_sar_specs,
	.remain_on_channel		= ath11k_mac_op_remain_on_channel,
	.cancel_remain_on_channel	= ath11k_mac_op_cancel_remain_on_channel,
};

static void ath11k_mac_update_ch_list(struct ath11k *ar,
				      struct ieee80211_supported_band *band,
				      u32 freq_low, u32 freq_high)
{
	int i;

	if (!(freq_low && freq_high))
		return;

	for (i = 0; i < band->n_channels; i++) {
		if (band->channels[i].center_freq < freq_low ||
		    band->channels[i].center_freq > freq_high)
			band->channels[i].flags |= IEEE80211_CHAN_DISABLED;
	}
}

static u32 ath11k_get_phy_id(struct ath11k *ar, u32 band)
{
	struct ath11k_pdev *pdev = ar->pdev;
	struct ath11k_pdev_cap *pdev_cap = &pdev->cap;

	if (band == WMI_HOST_WLAN_2G_CAP)
		return pdev_cap->band[NL80211_BAND_2GHZ].phy_id;

	if (band == WMI_HOST_WLAN_5G_CAP)
		return pdev_cap->band[NL80211_BAND_5GHZ].phy_id;

	ath11k_warn(ar->ab, "unsupported phy cap:%d\n", band);

	return 0;
}

static int ath11k_mac_setup_channels_rates(struct ath11k *ar,
					   u32 supported_bands)
{
	struct ieee80211_supported_band *band;
	struct ath11k_hal_reg_capabilities_ext *reg_cap, *temp_reg_cap;
	void *channels;
	u32 phy_id;

	BUILD_BUG_ON((ARRAY_SIZE(ath11k_2ghz_channels) +
		      ARRAY_SIZE(ath11k_5ghz_channels) +
		      ARRAY_SIZE(ath11k_6ghz_channels)) !=
		     ATH11K_NUM_CHANS);

	reg_cap = &ar->ab->hal_reg_cap[ar->pdev_idx];
	temp_reg_cap = reg_cap;

	if (supported_bands & WMI_HOST_WLAN_2G_CAP) {
		channels = kmemdup(ath11k_2ghz_channels,
				   sizeof(ath11k_2ghz_channels),
				   GFP_KERNEL);
		if (!channels)
			return -ENOMEM;

		band = &ar->mac.sbands[NL80211_BAND_2GHZ];
		band->band = NL80211_BAND_2GHZ;
		band->n_channels = ARRAY_SIZE(ath11k_2ghz_channels);
		band->channels = channels;
		band->n_bitrates = ath11k_g_rates_size;
		band->bitrates = ath11k_g_rates;
		ar->hw->wiphy->bands[NL80211_BAND_2GHZ] = band;

		if (ar->ab->hw_params.single_pdev_only) {
			phy_id = ath11k_get_phy_id(ar, WMI_HOST_WLAN_2G_CAP);
			temp_reg_cap = &ar->ab->hal_reg_cap[phy_id];
		}
		ath11k_mac_update_ch_list(ar, band,
					  temp_reg_cap->low_2ghz_chan,
					  temp_reg_cap->high_2ghz_chan);
	}

	if (supported_bands & WMI_HOST_WLAN_5G_CAP) {
		if (reg_cap->high_5ghz_chan >= ATH11K_MAX_6G_FREQ) {
			channels = kmemdup(ath11k_6ghz_channels,
					   sizeof(ath11k_6ghz_channels), GFP_KERNEL);
			if (!channels) {
				kfree(ar->mac.sbands[NL80211_BAND_2GHZ].channels);
				return -ENOMEM;
			}

			ar->supports_6ghz = true;
			band = &ar->mac.sbands[NL80211_BAND_6GHZ];
			band->band = NL80211_BAND_6GHZ;
			band->n_channels = ARRAY_SIZE(ath11k_6ghz_channels);
			band->channels = channels;
			band->n_bitrates = ath11k_a_rates_size;
			band->bitrates = ath11k_a_rates;
			ar->hw->wiphy->bands[NL80211_BAND_6GHZ] = band;

			if (ar->ab->hw_params.single_pdev_only) {
				phy_id = ath11k_get_phy_id(ar, WMI_HOST_WLAN_5G_CAP);
				temp_reg_cap = &ar->ab->hal_reg_cap[phy_id];
			}

			ath11k_mac_update_ch_list(ar, band,
						  temp_reg_cap->low_5ghz_chan,
						  temp_reg_cap->high_5ghz_chan);
		}

		if (reg_cap->low_5ghz_chan < ATH11K_MIN_6G_FREQ) {
			channels = kmemdup(ath11k_5ghz_channels,
					   sizeof(ath11k_5ghz_channels),
					   GFP_KERNEL);
			if (!channels) {
				kfree(ar->mac.sbands[NL80211_BAND_2GHZ].channels);
				kfree(ar->mac.sbands[NL80211_BAND_6GHZ].channels);
				return -ENOMEM;
			}

			band = &ar->mac.sbands[NL80211_BAND_5GHZ];
			band->band = NL80211_BAND_5GHZ;
			band->n_channels = ARRAY_SIZE(ath11k_5ghz_channels);
			band->channels = channels;
			band->n_bitrates = ath11k_a_rates_size;
			band->bitrates = ath11k_a_rates;
			ar->hw->wiphy->bands[NL80211_BAND_5GHZ] = band;

			if (ar->ab->hw_params.single_pdev_only) {
				phy_id = ath11k_get_phy_id(ar, WMI_HOST_WLAN_5G_CAP);
				temp_reg_cap = &ar->ab->hal_reg_cap[phy_id];
			}

			ath11k_mac_update_ch_list(ar, band,
						  temp_reg_cap->low_5ghz_chan,
						  temp_reg_cap->high_5ghz_chan);
		}
	}

	return 0;
}

static int ath11k_mac_setup_iface_combinations(struct ath11k *ar)
{
	struct ath11k_base *ab = ar->ab;
	struct ieee80211_iface_combination *combinations;
	struct ieee80211_iface_limit *limits;
	int n_limits;

	combinations = kzalloc(sizeof(*combinations), GFP_KERNEL);
	if (!combinations)
		return -ENOMEM;

	n_limits = 2;

	limits = kcalloc(n_limits, sizeof(*limits), GFP_KERNEL);
	if (!limits) {
		kfree(combinations);
		return -ENOMEM;
	}

	limits[0].max = 1;
	limits[0].types |= BIT(NL80211_IFTYPE_STATION);

	limits[1].max = 16;
	limits[1].types |= BIT(NL80211_IFTYPE_AP);

	if (IS_ENABLED(CONFIG_MAC80211_MESH) &&
	    ab->hw_params.interface_modes & BIT(NL80211_IFTYPE_MESH_POINT))
		limits[1].types |= BIT(NL80211_IFTYPE_MESH_POINT);

	combinations[0].limits = limits;
	combinations[0].n_limits = n_limits;
	combinations[0].max_interfaces = 16;
	combinations[0].num_different_channels = 1;
	combinations[0].beacon_int_infra_match = true;
	combinations[0].beacon_int_min_gcd = 100;
	combinations[0].radar_detect_widths = BIT(NL80211_CHAN_WIDTH_20_NOHT) |
						BIT(NL80211_CHAN_WIDTH_20) |
						BIT(NL80211_CHAN_WIDTH_40) |
						BIT(NL80211_CHAN_WIDTH_80) |
						BIT(NL80211_CHAN_WIDTH_80P80) |
						BIT(NL80211_CHAN_WIDTH_160);

	ar->hw->wiphy->iface_combinations = combinations;
	ar->hw->wiphy->n_iface_combinations = 1;

	return 0;
}

static const u8 ath11k_if_types_ext_capa[] = {
	[0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING,
	[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF,
};

static const u8 ath11k_if_types_ext_capa_sta[] = {
	[0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING,
	[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF,
	[9] = WLAN_EXT_CAPA10_TWT_REQUESTER_SUPPORT,
};

static const u8 ath11k_if_types_ext_capa_ap[] = {
	[0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING,
	[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF,
	[9] = WLAN_EXT_CAPA10_TWT_RESPONDER_SUPPORT,
};

static const struct wiphy_iftype_ext_capab ath11k_iftypes_ext_capa[] = {
	{
		.extended_capabilities = ath11k_if_types_ext_capa,
		.extended_capabilities_mask = ath11k_if_types_ext_capa,
		.extended_capabilities_len = sizeof(ath11k_if_types_ext_capa),
	}, {
		.iftype = NL80211_IFTYPE_STATION,
		.extended_capabilities = ath11k_if_types_ext_capa_sta,
		.extended_capabilities_mask = ath11k_if_types_ext_capa_sta,
		.extended_capabilities_len =
				sizeof(ath11k_if_types_ext_capa_sta),
	}, {
		.iftype = NL80211_IFTYPE_AP,
		.extended_capabilities = ath11k_if_types_ext_capa_ap,
		.extended_capabilities_mask = ath11k_if_types_ext_capa_ap,
		.extended_capabilities_len =
				sizeof(ath11k_if_types_ext_capa_ap),
	},
};

static void __ath11k_mac_unregister(struct ath11k *ar)
{
	cancel_work_sync(&ar->regd_update_work);

	ieee80211_unregister_hw(ar->hw);

	idr_for_each(&ar->txmgmt_idr, ath11k_mac_tx_mgmt_pending_free, ar);
	idr_destroy(&ar->txmgmt_idr);

	kfree(ar->mac.sbands[NL80211_BAND_2GHZ].channels);
	kfree(ar->mac.sbands[NL80211_BAND_5GHZ].channels);
	kfree(ar->mac.sbands[NL80211_BAND_6GHZ].channels);

	kfree(ar->hw->wiphy->iface_combinations[0].limits);
	kfree(ar->hw->wiphy->iface_combinations);

	SET_IEEE80211_DEV(ar->hw, NULL);
}

void ath11k_mac_unregister(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	int i;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		if (!ar)
			continue;

		__ath11k_mac_unregister(ar);
	}

	ath11k_peer_rhash_tbl_destroy(ab);
}

static int __ath11k_mac_register(struct ath11k *ar)
{
	struct ath11k_base *ab = ar->ab;
	struct ath11k_pdev_cap *cap = &ar->pdev->cap;
	static const u32 cipher_suites[] = {
		WLAN_CIPHER_SUITE_TKIP,
		WLAN_CIPHER_SUITE_CCMP,
		WLAN_CIPHER_SUITE_AES_CMAC,
		WLAN_CIPHER_SUITE_BIP_CMAC_256,
		WLAN_CIPHER_SUITE_BIP_GMAC_128,
		WLAN_CIPHER_SUITE_BIP_GMAC_256,
		WLAN_CIPHER_SUITE_GCMP,
		WLAN_CIPHER_SUITE_GCMP_256,
		WLAN_CIPHER_SUITE_CCMP_256,
	};
	int ret;
	u32 ht_cap = 0;

	ath11k_pdev_caps_update(ar);

	SET_IEEE80211_PERM_ADDR(ar->hw, ar->mac_addr);

	SET_IEEE80211_DEV(ar->hw, ab->dev);

	ret = ath11k_mac_setup_channels_rates(ar,
					      cap->supported_bands);
	if (ret)
		goto err;

	ath11k_mac_setup_ht_vht_cap(ar, cap, &ht_cap);
	ath11k_mac_setup_he_cap(ar, cap);

	ret = ath11k_mac_setup_iface_combinations(ar);
	if (ret) {
		ath11k_err(ar->ab, "failed to setup interface combinations: %d\n", ret);
		goto err_free_channels;
	}

	ar->hw->wiphy->available_antennas_rx = cap->rx_chain_mask;
	ar->hw->wiphy->available_antennas_tx = cap->tx_chain_mask;

	ar->hw->wiphy->interface_modes = ab->hw_params.interface_modes;

	if (ab->hw_params.single_pdev_only && ar->supports_6ghz)
		ieee80211_hw_set(ar->hw, SINGLE_SCAN_ON_ALL_BANDS);

	if (ab->hw_params.supports_multi_bssid) {
		ieee80211_hw_set(ar->hw, SUPPORTS_MULTI_BSSID);
		ieee80211_hw_set(ar->hw, SUPPORTS_ONLY_HE_MULTI_BSSID);
	}

	ieee80211_hw_set(ar->hw, SIGNAL_DBM);
	ieee80211_hw_set(ar->hw, SUPPORTS_PS);
	ieee80211_hw_set(ar->hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(ar->hw, MFP_CAPABLE);
	ieee80211_hw_set(ar->hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(ar->hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(ar->hw, AP_LINK_PS);
	ieee80211_hw_set(ar->hw, SPECTRUM_MGMT);
	ieee80211_hw_set(ar->hw, CONNECTION_MONITOR);
	ieee80211_hw_set(ar->hw, SUPPORTS_PER_STA_GTK);
	ieee80211_hw_set(ar->hw, WANT_MONITOR_VIF);
	ieee80211_hw_set(ar->hw, CHANCTX_STA_CSA);
	ieee80211_hw_set(ar->hw, QUEUE_CONTROL);
	ieee80211_hw_set(ar->hw, SUPPORTS_TX_FRAG);
	ieee80211_hw_set(ar->hw, REPORTS_LOW_ACK);

	if (ath11k_frame_mode == ATH11K_HW_TXRX_ETHERNET) {
		ieee80211_hw_set(ar->hw, SUPPORTS_TX_ENCAP_OFFLOAD);
		ieee80211_hw_set(ar->hw, SUPPORTS_RX_DECAP_OFFLOAD);
	}

	if (cap->nss_ratio_enabled)
		ieee80211_hw_set(ar->hw, SUPPORTS_VHT_EXT_NSS_BW);

	if ((ht_cap & WMI_HT_CAP_ENABLED) || ar->supports_6ghz) {
		ieee80211_hw_set(ar->hw, AMPDU_AGGREGATION);
		ieee80211_hw_set(ar->hw, TX_AMPDU_SETUP_IN_HW);
		ieee80211_hw_set(ar->hw, SUPPORTS_REORDERING_BUFFER);
		ieee80211_hw_set(ar->hw, SUPPORTS_AMSDU_IN_AMPDU);
		ieee80211_hw_set(ar->hw, USES_RSS);
	}

	ar->hw->wiphy->features |= NL80211_FEATURE_STATIC_SMPS;
	ar->hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;

	/* TODO: Check if HT capability advertised from firmware is different
	 * for each band for a dual band capable radio. It will be tricky to
	 * handle it when the ht capability different for each band.
	 */
	if (ht_cap & WMI_HT_CAP_DYNAMIC_SMPS ||
	    (ar->supports_6ghz && ab->hw_params.supports_dynamic_smps_6ghz))
		ar->hw->wiphy->features |= NL80211_FEATURE_DYNAMIC_SMPS;

	ar->hw->wiphy->max_scan_ssids = WLAN_SCAN_PARAMS_MAX_SSID;
	ar->hw->wiphy->max_scan_ie_len = WLAN_SCAN_PARAMS_MAX_IE_LEN;

	ar->hw->max_listen_interval = ATH11K_MAX_HW_LISTEN_INTERVAL;

	ar->hw->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	ar->hw->wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	ar->hw->wiphy->max_remain_on_channel_duration = 5000;

	ar->hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
	ar->hw->wiphy->features |= NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
				   NL80211_FEATURE_AP_SCAN;

	ar->max_num_stations = TARGET_NUM_STATIONS(ab);
	ar->max_num_peers = TARGET_NUM_PEERS_PDEV(ab);

	ar->hw->wiphy->max_ap_assoc_sta = ar->max_num_stations;

	if (test_bit(WMI_TLV_SERVICE_SPOOF_MAC_SUPPORT, ar->wmi->wmi_ab->svc_map)) {
		ar->hw->wiphy->features |=
			NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR;
	}

	if (test_bit(WMI_TLV_SERVICE_NLO, ar->wmi->wmi_ab->svc_map)) {
		ar->hw->wiphy->max_sched_scan_ssids = WMI_PNO_MAX_SUPP_NETWORKS;
		ar->hw->wiphy->max_match_sets = WMI_PNO_MAX_SUPP_NETWORKS;
		ar->hw->wiphy->max_sched_scan_ie_len = WMI_PNO_MAX_IE_LENGTH;
		ar->hw->wiphy->max_sched_scan_plans = WMI_PNO_MAX_SCHED_SCAN_PLANS;
		ar->hw->wiphy->max_sched_scan_plan_interval =
			WMI_PNO_MAX_SCHED_SCAN_PLAN_INT;
		ar->hw->wiphy->max_sched_scan_plan_iterations =
			WMI_PNO_MAX_SCHED_SCAN_PLAN_ITRNS;
		ar->hw->wiphy->features |= NL80211_FEATURE_ND_RANDOM_MAC_ADDR;
	}

	ret = ath11k_wow_init(ar);
	if (ret) {
		ath11k_warn(ar->ab, "failed to init wow: %d\n", ret);
		goto err_free_if_combs;
	}

	if (test_bit(WMI_TLV_SERVICE_TX_DATA_MGMT_ACK_RSSI,
		     ar->ab->wmi_ab.svc_map))
		wiphy_ext_feature_set(ar->hw->wiphy,
				      NL80211_EXT_FEATURE_ACK_SIGNAL_SUPPORT);

	ar->hw->queues = ATH11K_HW_MAX_QUEUES;
	ar->hw->wiphy->tx_queue_len = ATH11K_QUEUE_LEN;
	ar->hw->offchannel_tx_hw_queue = ATH11K_HW_MAX_QUEUES - 1;
	ar->hw->max_rx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF_HE;

	ar->hw->vif_data_size = sizeof(struct ath11k_vif);
	ar->hw->sta_data_size = sizeof(struct ath11k_sta);

	wiphy_ext_feature_set(ar->hw->wiphy, NL80211_EXT_FEATURE_CQM_RSSI_LIST);
	wiphy_ext_feature_set(ar->hw->wiphy, NL80211_EXT_FEATURE_STA_TX_PWR);
	if (test_bit(WMI_TLV_SERVICE_BSS_COLOR_OFFLOAD,
		     ar->ab->wmi_ab.svc_map)) {
		wiphy_ext_feature_set(ar->hw->wiphy,
				      NL80211_EXT_FEATURE_BSS_COLOR);
		ieee80211_hw_set(ar->hw, DETECTS_COLOR_COLLISION);
	}

	ar->hw->wiphy->cipher_suites = cipher_suites;
	ar->hw->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

	ar->hw->wiphy->iftype_ext_capab = ath11k_iftypes_ext_capa;
	ar->hw->wiphy->num_iftype_ext_capab =
		ARRAY_SIZE(ath11k_iftypes_ext_capa);

	if (ar->supports_6ghz) {
		wiphy_ext_feature_set(ar->hw->wiphy,
				      NL80211_EXT_FEATURE_FILS_DISCOVERY);
		wiphy_ext_feature_set(ar->hw->wiphy,
				      NL80211_EXT_FEATURE_UNSOL_BCAST_PROBE_RESP);
	}

	wiphy_ext_feature_set(ar->hw->wiphy,
			      NL80211_EXT_FEATURE_SET_SCAN_DWELL);

	if (test_bit(WMI_TLV_SERVICE_RTT, ar->ab->wmi_ab.svc_map))
		wiphy_ext_feature_set(ar->hw->wiphy,
				      NL80211_EXT_FEATURE_ENABLE_FTM_RESPONDER);

	ath11k_reg_init(ar);

	if (!test_bit(ATH11K_FLAG_RAW_MODE, &ab->dev_flags)) {
		ar->hw->netdev_features = NETIF_F_HW_CSUM;
		ieee80211_hw_set(ar->hw, SW_CRYPTO_CONTROL);
		ieee80211_hw_set(ar->hw, SUPPORT_FAST_XMIT);
	}

	if (test_bit(WMI_TLV_SERVICE_BIOS_SAR_SUPPORT, ar->ab->wmi_ab.svc_map) &&
	    ab->hw_params.bios_sar_capa)
		ar->hw->wiphy->sar_capa = ab->hw_params.bios_sar_capa;

	ret = ieee80211_register_hw(ar->hw);
	if (ret) {
		ath11k_err(ar->ab, "ieee80211 registration failed: %d\n", ret);
		goto err_free_if_combs;
	}

	if (!ab->hw_params.supports_monitor)
		/* There's a race between calling ieee80211_register_hw()
		 * and here where the monitor mode is enabled for a little
		 * while. But that time is so short and in practise it make
		 * a difference in real life.
		 */
		ar->hw->wiphy->interface_modes &= ~BIT(NL80211_IFTYPE_MONITOR);

	/* Apply the regd received during initialization */
	ret = ath11k_regd_update(ar);
	if (ret) {
		ath11k_err(ar->ab, "ath11k regd update failed: %d\n", ret);
		goto err_unregister_hw;
	}

	if (ab->hw_params.current_cc_support && ab->new_alpha2[0]) {
		struct wmi_set_current_country_params set_current_param = {};

		memcpy(&set_current_param.alpha2, ab->new_alpha2, 2);
		memcpy(&ar->alpha2, ab->new_alpha2, 2);
		ret = ath11k_wmi_send_set_current_country_cmd(ar, &set_current_param);
		if (ret)
			ath11k_warn(ar->ab,
				    "failed set cc code for mac register: %d\n", ret);
	}

	ret = ath11k_debugfs_register(ar);
	if (ret) {
		ath11k_err(ar->ab, "debugfs registration failed: %d\n", ret);
		goto err_unregister_hw;
	}

	return 0;

err_unregister_hw:
	ieee80211_unregister_hw(ar->hw);

err_free_if_combs:
	kfree(ar->hw->wiphy->iface_combinations[0].limits);
	kfree(ar->hw->wiphy->iface_combinations);

err_free_channels:
	kfree(ar->mac.sbands[NL80211_BAND_2GHZ].channels);
	kfree(ar->mac.sbands[NL80211_BAND_5GHZ].channels);
	kfree(ar->mac.sbands[NL80211_BAND_6GHZ].channels);

err:
	SET_IEEE80211_DEV(ar->hw, NULL);
	return ret;
}

int ath11k_mac_register(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	int i;
	int ret;
	u8 mac_addr[ETH_ALEN] = {0};

	if (test_bit(ATH11K_FLAG_REGISTERED, &ab->dev_flags))
		return 0;

	/* Initialize channel counters frequency value in hertz */
	ab->cc_freq_hz = IPQ8074_CC_FREQ_HERTZ;
	ab->free_vdev_map = (1LL << (ab->num_radios * TARGET_NUM_VDEVS(ab))) - 1;

	ret = ath11k_peer_rhash_tbl_init(ab);
	if (ret)
		return ret;

	device_get_mac_address(ab->dev, mac_addr);

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		if (ab->pdevs_macaddr_valid) {
			ether_addr_copy(ar->mac_addr, pdev->mac_addr);
		} else {
			if (is_zero_ether_addr(mac_addr))
				ether_addr_copy(ar->mac_addr, ab->mac_addr);
			else
				ether_addr_copy(ar->mac_addr, mac_addr);
			ar->mac_addr[4] += i;
		}

		idr_init(&ar->txmgmt_idr);
		spin_lock_init(&ar->txmgmt_idr_lock);

		ret = __ath11k_mac_register(ar);
		if (ret)
			goto err_cleanup;

		init_waitqueue_head(&ar->txmgmt_empty_waitq);
	}

	return 0;

err_cleanup:
	for (i = i - 1; i >= 0; i--) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		__ath11k_mac_unregister(ar);
	}

	ath11k_peer_rhash_tbl_destroy(ab);

	return ret;
}

int ath11k_mac_allocate(struct ath11k_base *ab)
{
	struct ieee80211_hw *hw;
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	int ret;
	int i;

	if (test_bit(ATH11K_FLAG_REGISTERED, &ab->dev_flags))
		return 0;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		hw = ieee80211_alloc_hw(sizeof(struct ath11k), &ath11k_ops);
		if (!hw) {
			ath11k_warn(ab, "failed to allocate mac80211 hw device\n");
			ret = -ENOMEM;
			goto err_free_mac;
		}

		ar = hw->priv;
		ar->hw = hw;
		ar->ab = ab;
		ar->pdev = pdev;
		ar->pdev_idx = i;
		ar->lmac_id = ath11k_hw_get_mac_from_pdev_id(&ab->hw_params, i);

		ar->wmi = &ab->wmi_ab.wmi[i];
		/* FIXME wmi[0] is already initialized during attach,
		 * Should we do this again?
		 */
		ath11k_wmi_pdev_attach(ab, i);

		ar->cfg_tx_chainmask = pdev->cap.tx_chain_mask;
		ar->cfg_rx_chainmask = pdev->cap.rx_chain_mask;
		ar->num_tx_chains = get_num_chains(pdev->cap.tx_chain_mask);
		ar->num_rx_chains = get_num_chains(pdev->cap.rx_chain_mask);

		pdev->ar = ar;
		spin_lock_init(&ar->data_lock);
		INIT_LIST_HEAD(&ar->arvifs);
		INIT_LIST_HEAD(&ar->ppdu_stats_info);
		mutex_init(&ar->conf_mutex);
		init_completion(&ar->vdev_setup_done);
		init_completion(&ar->vdev_delete_done);
		init_completion(&ar->peer_assoc_done);
		init_completion(&ar->peer_delete_done);
		init_completion(&ar->install_key_done);
		init_completion(&ar->bss_survey_done);
		init_completion(&ar->scan.started);
		init_completion(&ar->scan.completed);
		init_completion(&ar->scan.on_channel);
		init_completion(&ar->thermal.wmi_sync);

		INIT_DELAYED_WORK(&ar->scan.timeout, ath11k_scan_timeout_work);
		INIT_WORK(&ar->regd_update_work, ath11k_regd_update_work);

		INIT_WORK(&ar->wmi_mgmt_tx_work, ath11k_mgmt_over_wmi_tx_work);
		skb_queue_head_init(&ar->wmi_mgmt_tx_queue);

		clear_bit(ATH11K_FLAG_MONITOR_STARTED, &ar->monitor_flags);

		ar->monitor_vdev_id = -1;
		clear_bit(ATH11K_FLAG_MONITOR_VDEV_CREATED, &ar->monitor_flags);
		ar->vdev_id_11d_scan = ATH11K_11D_INVALID_VDEV_ID;
		init_completion(&ar->completed_11d_scan);

		ath11k_fw_stats_init(ar);
	}

	return 0;

err_free_mac:
	ath11k_mac_destroy(ab);

	return ret;
}

void ath11k_mac_destroy(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	int i;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		if (!ar)
			continue;

		ieee80211_free_hw(ar->hw);
		pdev->ar = NULL;
	}
}

int ath11k_mac_vif_set_keepalive(struct ath11k_vif *arvif,
				 enum wmi_sta_keepalive_method method,
				 u32 interval)
{
	struct ath11k *ar = arvif->ar;
	struct wmi_sta_keepalive_arg arg = {};
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	if (arvif->vdev_type != WMI_VDEV_TYPE_STA)
		return 0;

	if (!test_bit(WMI_TLV_SERVICE_STA_KEEP_ALIVE, ar->ab->wmi_ab.svc_map))
		return 0;

	arg.vdev_id = arvif->vdev_id;
	arg.enabled = 1;
	arg.method = method;
	arg.interval = interval;

	ret = ath11k_wmi_sta_keepalive(ar, &arg);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set keepalive on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	return 0;
}
