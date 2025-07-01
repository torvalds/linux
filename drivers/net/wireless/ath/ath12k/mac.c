// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/etherdevice.h>

#include "mac.h"
#include "core.h"
#include "debug.h"
#include "wmi.h"
#include "hw.h"
#include "dp_tx.h"
#include "dp_rx.h"
#include "testmode.h"
#include "peer.h"
#include "debugfs.h"
#include "hif.h"
#include "wow.h"
#include "debugfs_sta.h"

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

static const struct ieee80211_channel ath12k_2ghz_channels[] = {
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

static const struct ieee80211_channel ath12k_5ghz_channels[] = {
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
};

static const struct ieee80211_channel ath12k_6ghz_channels[] = {
	/* Operating Class 136 */
	CHAN6G(2, 5935, 0),

	/* Operating Classes 131-135 */
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
};

static struct ieee80211_rate ath12k_legacy_rates[] = {
	{ .bitrate = 10,
	  .hw_value = ATH12K_HW_RATE_CCK_LP_1M },
	{ .bitrate = 20,
	  .hw_value = ATH12K_HW_RATE_CCK_LP_2M,
	  .hw_value_short = ATH12K_HW_RATE_CCK_SP_2M,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,
	  .hw_value = ATH12K_HW_RATE_CCK_LP_5_5M,
	  .hw_value_short = ATH12K_HW_RATE_CCK_SP_5_5M,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110,
	  .hw_value = ATH12K_HW_RATE_CCK_LP_11M,
	  .hw_value_short = ATH12K_HW_RATE_CCK_SP_11M,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },

	{ .bitrate = 60, .hw_value = ATH12K_HW_RATE_OFDM_6M },
	{ .bitrate = 90, .hw_value = ATH12K_HW_RATE_OFDM_9M },
	{ .bitrate = 120, .hw_value = ATH12K_HW_RATE_OFDM_12M },
	{ .bitrate = 180, .hw_value = ATH12K_HW_RATE_OFDM_18M },
	{ .bitrate = 240, .hw_value = ATH12K_HW_RATE_OFDM_24M },
	{ .bitrate = 360, .hw_value = ATH12K_HW_RATE_OFDM_36M },
	{ .bitrate = 480, .hw_value = ATH12K_HW_RATE_OFDM_48M },
	{ .bitrate = 540, .hw_value = ATH12K_HW_RATE_OFDM_54M },
};

static const int
ath12k_phymodes[NUM_NL80211_BANDS][ATH12K_CHAN_WIDTH_NUM] = {
	[NL80211_BAND_2GHZ] = {
			[NL80211_CHAN_WIDTH_5] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_10] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_20_NOHT] = MODE_11BE_EHT20_2G,
			[NL80211_CHAN_WIDTH_20] = MODE_11BE_EHT20_2G,
			[NL80211_CHAN_WIDTH_40] = MODE_11BE_EHT40_2G,
			[NL80211_CHAN_WIDTH_80] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_80P80] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_160] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_320] = MODE_UNKNOWN,
	},
	[NL80211_BAND_5GHZ] = {
			[NL80211_CHAN_WIDTH_5] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_10] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_20_NOHT] = MODE_11BE_EHT20,
			[NL80211_CHAN_WIDTH_20] = MODE_11BE_EHT20,
			[NL80211_CHAN_WIDTH_40] = MODE_11BE_EHT40,
			[NL80211_CHAN_WIDTH_80] = MODE_11BE_EHT80,
			[NL80211_CHAN_WIDTH_160] = MODE_11BE_EHT160,
			[NL80211_CHAN_WIDTH_80P80] = MODE_11BE_EHT80_80,
			[NL80211_CHAN_WIDTH_320] = MODE_11BE_EHT320,
	},
	[NL80211_BAND_6GHZ] = {
			[NL80211_CHAN_WIDTH_5] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_10] = MODE_UNKNOWN,
			[NL80211_CHAN_WIDTH_20_NOHT] = MODE_11BE_EHT20,
			[NL80211_CHAN_WIDTH_20] = MODE_11BE_EHT20,
			[NL80211_CHAN_WIDTH_40] = MODE_11BE_EHT40,
			[NL80211_CHAN_WIDTH_80] = MODE_11BE_EHT80,
			[NL80211_CHAN_WIDTH_160] = MODE_11BE_EHT160,
			[NL80211_CHAN_WIDTH_80P80] = MODE_11BE_EHT80_80,
			[NL80211_CHAN_WIDTH_320] = MODE_11BE_EHT320,
	},

};

const struct htt_rx_ring_tlv_filter ath12k_mac_mon_status_filter_default = {
	.rx_filter = HTT_RX_FILTER_TLV_FLAGS_MPDU_START |
		     HTT_RX_FILTER_TLV_FLAGS_PPDU_END |
		     HTT_RX_FILTER_TLV_FLAGS_PPDU_END_STATUS_DONE |
		     HTT_RX_FILTER_TLV_FLAGS_PPDU_START_USER_INFO,
	.pkt_filter_flags0 = HTT_RX_FP_MGMT_FILTER_FLAGS0,
	.pkt_filter_flags1 = HTT_RX_FP_MGMT_FILTER_FLAGS1,
	.pkt_filter_flags2 = HTT_RX_FP_CTRL_FILTER_FLASG2,
	.pkt_filter_flags3 = HTT_RX_FP_DATA_FILTER_FLASG3 |
			     HTT_RX_FP_CTRL_FILTER_FLASG3
};

#define ATH12K_MAC_FIRST_OFDM_RATE_IDX 4
#define ath12k_g_rates ath12k_legacy_rates
#define ath12k_g_rates_size (ARRAY_SIZE(ath12k_legacy_rates))
#define ath12k_a_rates (ath12k_legacy_rates + 4)
#define ath12k_a_rates_size (ARRAY_SIZE(ath12k_legacy_rates) - 4)

#define ATH12K_MAC_SCAN_TIMEOUT_MSECS 200 /* in msecs */

static const u32 ath12k_smps_map[] = {
	[WLAN_HT_CAP_SM_PS_STATIC] = WMI_PEER_SMPS_STATIC,
	[WLAN_HT_CAP_SM_PS_DYNAMIC] = WMI_PEER_SMPS_DYNAMIC,
	[WLAN_HT_CAP_SM_PS_INVALID] = WMI_PEER_SMPS_PS_NONE,
	[WLAN_HT_CAP_SM_PS_DISABLED] = WMI_PEER_SMPS_PS_NONE,
};

static int ath12k_start_vdev_delay(struct ath12k *ar,
				   struct ath12k_link_vif *arvif);
static void ath12k_mac_stop(struct ath12k *ar);
static int ath12k_mac_vdev_create(struct ath12k *ar, struct ath12k_link_vif *arvif);
static int ath12k_mac_vdev_delete(struct ath12k *ar, struct ath12k_link_vif *arvif);

static const char *ath12k_mac_phymode_str(enum wmi_phy_mode mode)
{
	switch (mode) {
	case MODE_11A:
		return "11a";
	case MODE_11G:
		return "11g";
	case MODE_11B:
		return "11b";
	case MODE_11GONLY:
		return "11gonly";
	case MODE_11NA_HT20:
		return "11na-ht20";
	case MODE_11NG_HT20:
		return "11ng-ht20";
	case MODE_11NA_HT40:
		return "11na-ht40";
	case MODE_11NG_HT40:
		return "11ng-ht40";
	case MODE_11AC_VHT20:
		return "11ac-vht20";
	case MODE_11AC_VHT40:
		return "11ac-vht40";
	case MODE_11AC_VHT80:
		return "11ac-vht80";
	case MODE_11AC_VHT160:
		return "11ac-vht160";
	case MODE_11AC_VHT80_80:
		return "11ac-vht80+80";
	case MODE_11AC_VHT20_2G:
		return "11ac-vht20-2g";
	case MODE_11AC_VHT40_2G:
		return "11ac-vht40-2g";
	case MODE_11AC_VHT80_2G:
		return "11ac-vht80-2g";
	case MODE_11AX_HE20:
		return "11ax-he20";
	case MODE_11AX_HE40:
		return "11ax-he40";
	case MODE_11AX_HE80:
		return "11ax-he80";
	case MODE_11AX_HE80_80:
		return "11ax-he80+80";
	case MODE_11AX_HE160:
		return "11ax-he160";
	case MODE_11AX_HE20_2G:
		return "11ax-he20-2g";
	case MODE_11AX_HE40_2G:
		return "11ax-he40-2g";
	case MODE_11AX_HE80_2G:
		return "11ax-he80-2g";
	case MODE_11BE_EHT20:
		return "11be-eht20";
	case MODE_11BE_EHT40:
		return "11be-eht40";
	case MODE_11BE_EHT80:
		return "11be-eht80";
	case MODE_11BE_EHT80_80:
		return "11be-eht80+80";
	case MODE_11BE_EHT160:
		return "11be-eht160";
	case MODE_11BE_EHT160_160:
		return "11be-eht160+160";
	case MODE_11BE_EHT320:
		return "11be-eht320";
	case MODE_11BE_EHT20_2G:
		return "11be-eht20-2g";
	case MODE_11BE_EHT40_2G:
		return "11be-eht40-2g";
	case MODE_UNKNOWN:
		/* skip */
		break;

		/* no default handler to allow compiler to check that the
		 * enum is fully handled
		 */
	}

	return "<unknown>";
}

u16 ath12k_mac_he_convert_tones_to_ru_tones(u16 tones)
{
	switch (tones) {
	case 26:
		return RU_26;
	case 52:
		return RU_52;
	case 106:
		return RU_106;
	case 242:
		return RU_242;
	case 484:
		return RU_484;
	case 996:
		return RU_996;
	case (996 * 2):
		return RU_2X996;
	default:
		return RU_26;
	}
}

enum nl80211_eht_gi ath12k_mac_eht_gi_to_nl80211_eht_gi(u8 sgi)
{
	switch (sgi) {
	case RX_MSDU_START_SGI_0_8_US:
		return NL80211_RATE_INFO_EHT_GI_0_8;
	case RX_MSDU_START_SGI_1_6_US:
		return NL80211_RATE_INFO_EHT_GI_1_6;
	case RX_MSDU_START_SGI_3_2_US:
		return NL80211_RATE_INFO_EHT_GI_3_2;
	default:
		return NL80211_RATE_INFO_EHT_GI_0_8;
	}
}

enum nl80211_eht_ru_alloc ath12k_mac_eht_ru_tones_to_nl80211_eht_ru_alloc(u16 ru_tones)
{
	switch (ru_tones) {
	case 26:
		return NL80211_RATE_INFO_EHT_RU_ALLOC_26;
	case 52:
		return NL80211_RATE_INFO_EHT_RU_ALLOC_52;
	case (52 + 26):
		return NL80211_RATE_INFO_EHT_RU_ALLOC_52P26;
	case 106:
		return NL80211_RATE_INFO_EHT_RU_ALLOC_106;
	case (106 + 26):
		return NL80211_RATE_INFO_EHT_RU_ALLOC_106P26;
	case 242:
		return NL80211_RATE_INFO_EHT_RU_ALLOC_242;
	case 484:
		return NL80211_RATE_INFO_EHT_RU_ALLOC_484;
	case (484 + 242):
		return NL80211_RATE_INFO_EHT_RU_ALLOC_484P242;
	case 996:
		return NL80211_RATE_INFO_EHT_RU_ALLOC_996;
	case (996 + 484):
		return NL80211_RATE_INFO_EHT_RU_ALLOC_996P484;
	case (996 + 484 + 242):
		return NL80211_RATE_INFO_EHT_RU_ALLOC_996P484P242;
	case (2 * 996):
		return NL80211_RATE_INFO_EHT_RU_ALLOC_2x996;
	case (2 * 996 + 484):
		return NL80211_RATE_INFO_EHT_RU_ALLOC_2x996P484;
	case (3 * 996):
		return NL80211_RATE_INFO_EHT_RU_ALLOC_3x996;
	case (3 * 996 + 484):
		return NL80211_RATE_INFO_EHT_RU_ALLOC_3x996P484;
	case (4 * 996):
		return NL80211_RATE_INFO_EHT_RU_ALLOC_4x996;
	default:
		return NL80211_RATE_INFO_EHT_RU_ALLOC_26;
	}
}

enum rate_info_bw
ath12k_mac_bw_to_mac80211_bw(enum ath12k_supported_bw bw)
{
	u8 ret = RATE_INFO_BW_20;

	switch (bw) {
	case ATH12K_BW_20:
		ret = RATE_INFO_BW_20;
		break;
	case ATH12K_BW_40:
		ret = RATE_INFO_BW_40;
		break;
	case ATH12K_BW_80:
		ret = RATE_INFO_BW_80;
		break;
	case ATH12K_BW_160:
		ret = RATE_INFO_BW_160;
		break;
	case ATH12K_BW_320:
		ret = RATE_INFO_BW_320;
		break;
	}

	return ret;
}

enum ath12k_supported_bw ath12k_mac_mac80211_bw_to_ath12k_bw(enum rate_info_bw bw)
{
	switch (bw) {
	case RATE_INFO_BW_20:
		return ATH12K_BW_20;
	case RATE_INFO_BW_40:
		return ATH12K_BW_40;
	case RATE_INFO_BW_80:
		return ATH12K_BW_80;
	case RATE_INFO_BW_160:
		return ATH12K_BW_160;
	case RATE_INFO_BW_320:
		return ATH12K_BW_320;
	default:
		return ATH12K_BW_20;
	}
}

int ath12k_mac_hw_ratecode_to_legacy_rate(u8 hw_rc, u8 preamble, u8 *rateidx,
					  u16 *rate)
{
	/* As default, it is OFDM rates */
	int i = ATH12K_MAC_FIRST_OFDM_RATE_IDX;
	int max_rates_idx = ath12k_g_rates_size;

	if (preamble == WMI_RATE_PREAMBLE_CCK) {
		hw_rc &= ~ATH12K_HW_RATECODE_CCK_SHORT_PREAM_MASK;
		i = 0;
		max_rates_idx = ATH12K_MAC_FIRST_OFDM_RATE_IDX;
	}

	while (i < max_rates_idx) {
		if (hw_rc == ath12k_legacy_rates[i].hw_value) {
			*rateidx = i;
			*rate = ath12k_legacy_rates[i].bitrate;
			return 0;
		}
		i++;
	}

	return -EINVAL;
}

u8 ath12k_mac_bitrate_to_idx(const struct ieee80211_supported_band *sband,
			     u32 bitrate)
{
	int i;

	for (i = 0; i < sband->n_bitrates; i++)
		if (sband->bitrates[i].bitrate == bitrate)
			return i;

	return 0;
}

static u32
ath12k_mac_max_ht_nss(const u8 *ht_mcs_mask)
{
	int nss;

	for (nss = IEEE80211_HT_MCS_MASK_LEN - 1; nss >= 0; nss--)
		if (ht_mcs_mask[nss])
			return nss + 1;

	return 1;
}

static u32
ath12k_mac_max_vht_nss(const u16 *vht_mcs_mask)
{
	int nss;

	for (nss = NL80211_VHT_NSS_MAX - 1; nss >= 0; nss--)
		if (vht_mcs_mask[nss])
			return nss + 1;

	return 1;
}

static u32
ath12k_mac_max_he_nss(const u16 he_mcs_mask[NL80211_HE_NSS_MAX])
{
	int nss;

	for (nss = NL80211_HE_NSS_MAX - 1; nss >= 0; nss--)
		if (he_mcs_mask[nss])
			return nss + 1;

	return 1;
}

static u8 ath12k_parse_mpdudensity(u8 mpdudensity)
{
/*  From IEEE Std 802.11-2020 defined values for "Minimum MPDU Start Spacing":
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

static int ath12k_mac_vif_link_chan(struct ieee80211_vif *vif, u8 link_id,
				    struct cfg80211_chan_def *def)
{
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_chanctx_conf *conf;

	rcu_read_lock();
	link_conf = rcu_dereference(vif->link_conf[link_id]);

	if (!link_conf) {
		rcu_read_unlock();
		return -ENOLINK;
	}

	conf = rcu_dereference(link_conf->chanctx_conf);
	if (!conf) {
		rcu_read_unlock();
		return -ENOENT;
	}
	*def = conf->def;
	rcu_read_unlock();

	return 0;
}

static struct ath12k_link_vif *
ath12k_mac_get_tx_arvif(struct ath12k_link_vif *arvif,
			struct ieee80211_bss_conf *link_conf)
{
	struct ieee80211_bss_conf *tx_bss_conf;
	struct ath12k *ar = arvif->ar;
	struct ath12k_vif *tx_ahvif;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	tx_bss_conf = wiphy_dereference(ath12k_ar_to_hw(ar)->wiphy,
					link_conf->tx_bss_conf);
	if (tx_bss_conf) {
		tx_ahvif = ath12k_vif_to_ahvif(tx_bss_conf->vif);
		return wiphy_dereference(tx_ahvif->ah->hw->wiphy,
					 tx_ahvif->link[tx_bss_conf->link_id]);
	}

	return NULL;
}

static const u8 *ath12k_mac_get_tx_bssid(struct ath12k_link_vif *arvif)
{
	struct ieee80211_bss_conf *link_conf;
	struct ath12k_link_vif *tx_arvif;
	struct ath12k *ar = arvif->ar;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	link_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!link_conf) {
		ath12k_warn(ar->ab,
			    "unable to access bss link conf for link %u required to retrieve transmitting link conf\n",
			    arvif->link_id);
		return NULL;
	}
	if (link_conf->vif->type == NL80211_IFTYPE_STATION) {
		if (link_conf->nontransmitted)
			return link_conf->transmitter_bssid;
	} else {
		tx_arvif = ath12k_mac_get_tx_arvif(arvif, link_conf);
		if (tx_arvif)
			return tx_arvif->bssid;
	}

	return NULL;
}

struct ieee80211_bss_conf *
ath12k_mac_get_link_bss_conf(struct ath12k_link_vif *arvif)
{
	struct ieee80211_vif *vif = arvif->ahvif->vif;
	struct ieee80211_bss_conf *link_conf;
	struct ath12k *ar = arvif->ar;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (arvif->link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return NULL;

	link_conf = wiphy_dereference(ath12k_ar_to_hw(ar)->wiphy,
				      vif->link_conf[arvif->link_id]);

	return link_conf;
}

static struct ieee80211_link_sta *ath12k_mac_get_link_sta(struct ath12k_link_sta *arsta)
{
	struct ath12k_sta *ahsta = arsta->ahsta;
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(ahsta);
	struct ieee80211_link_sta *link_sta;

	lockdep_assert_wiphy(ahsta->ahvif->ah->hw->wiphy);

	if (arsta->link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return NULL;

	link_sta = wiphy_dereference(ahsta->ahvif->ah->hw->wiphy,
				     sta->link[arsta->link_id]);

	return link_sta;
}

static bool ath12k_mac_bitrate_is_cck(int bitrate)
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

u8 ath12k_mac_hw_rate_to_idx(const struct ieee80211_supported_band *sband,
			     u8 hw_rate, bool cck)
{
	const struct ieee80211_rate *rate;
	int i;

	for (i = 0; i < sband->n_bitrates; i++) {
		rate = &sband->bitrates[i];

		if (ath12k_mac_bitrate_is_cck(rate->bitrate) != cck)
			continue;

		if (rate->hw_value == hw_rate)
			return i;
		else if (rate->flags & IEEE80211_RATE_SHORT_PREAMBLE &&
			 rate->hw_value_short == hw_rate)
			return i;
	}

	return 0;
}

static u8 ath12k_mac_bitrate_to_rate(int bitrate)
{
	return DIV_ROUND_UP(bitrate, 5) |
	       (ath12k_mac_bitrate_is_cck(bitrate) ? BIT(7) : 0);
}

static void ath12k_get_arvif_iter(void *data, u8 *mac,
				  struct ieee80211_vif *vif)
{
	struct ath12k_vif_iter *arvif_iter = data;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	unsigned long links_map = ahvif->links_map;
	struct ath12k_link_vif *arvif;
	u8 link_id;

	for_each_set_bit(link_id, &links_map, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = rcu_dereference(ahvif->link[link_id]);

		if (WARN_ON(!arvif))
			continue;

		if (!arvif->is_created)
			continue;

		if (arvif->vdev_id == arvif_iter->vdev_id &&
		    arvif->ar == arvif_iter->ar) {
			arvif_iter->arvif = arvif;
			break;
		}
	}
}

struct ath12k_link_vif *ath12k_mac_get_arvif(struct ath12k *ar, u32 vdev_id)
{
	struct ath12k_vif_iter arvif_iter = {};
	u32 flags;

	/* To use the arvif returned, caller must have held rcu read lock.
	 */
	WARN_ON(!rcu_read_lock_any_held());
	arvif_iter.vdev_id = vdev_id;
	arvif_iter.ar = ar;

	flags = IEEE80211_IFACE_ITER_RESUME_ALL;
	ieee80211_iterate_active_interfaces_atomic(ath12k_ar_to_hw(ar),
						   flags,
						   ath12k_get_arvif_iter,
						   &arvif_iter);
	if (!arvif_iter.arvif) {
		ath12k_warn(ar->ab, "No VIF found for vdev %d\n", vdev_id);
		return NULL;
	}

	return arvif_iter.arvif;
}

struct ath12k_link_vif *ath12k_mac_get_arvif_by_vdev_id(struct ath12k_base *ab,
							u32 vdev_id)
{
	int i;
	struct ath12k_pdev *pdev;
	struct ath12k_link_vif *arvif;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = rcu_dereference(ab->pdevs_active[i]);
		if (pdev && pdev->ar &&
		    (pdev->ar->allocated_vdev_map & (1LL << vdev_id))) {
			arvif = ath12k_mac_get_arvif(pdev->ar, vdev_id);
			if (arvif)
				return arvif;
		}
	}

	return NULL;
}

struct ath12k *ath12k_mac_get_ar_by_vdev_id(struct ath12k_base *ab, u32 vdev_id)
{
	int i;
	struct ath12k_pdev *pdev;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = rcu_dereference(ab->pdevs_active[i]);
		if (pdev && pdev->ar) {
			if (pdev->ar->allocated_vdev_map & (1LL << vdev_id))
				return pdev->ar;
		}
	}

	return NULL;
}

struct ath12k *ath12k_mac_get_ar_by_pdev_id(struct ath12k_base *ab, u32 pdev_id)
{
	int i;
	struct ath12k_pdev *pdev;

	if (ab->hw_params->single_pdev_only) {
		pdev = rcu_dereference(ab->pdevs_active[0]);
		return pdev ? pdev->ar : NULL;
	}

	if (WARN_ON(pdev_id > ab->num_radios))
		return NULL;

	for (i = 0; i < ab->num_radios; i++) {
		if (ab->fw_mode == ATH12K_FIRMWARE_MODE_FTM)
			pdev = &ab->pdevs[i];
		else
			pdev = rcu_dereference(ab->pdevs_active[i]);

		if (pdev && pdev->pdev_id == pdev_id)
			return (pdev->ar ? pdev->ar : NULL);
	}

	return NULL;
}

static bool ath12k_mac_is_ml_arvif(struct ath12k_link_vif *arvif)
{
	struct ath12k_vif *ahvif = arvif->ahvif;

	lockdep_assert_wiphy(ahvif->ah->hw->wiphy);

	if (ahvif->vif->valid_links & BIT(arvif->link_id))
		return true;

	return false;
}

static struct ath12k *ath12k_mac_get_ar_by_chan(struct ieee80211_hw *hw,
						struct ieee80211_channel *channel)
{
	struct ath12k_hw *ah = hw->priv;
	struct ath12k *ar;
	int i;

	ar = ah->radio;

	if (ah->num_radio == 1)
		return ar;

	for_each_ar(ah, ar, i) {
		if (channel->center_freq >= KHZ_TO_MHZ(ar->freq_range.start_freq) &&
		    channel->center_freq <= KHZ_TO_MHZ(ar->freq_range.end_freq))
			return ar;
	}
	return NULL;
}

static struct ath12k *ath12k_get_ar_by_ctx(struct ieee80211_hw *hw,
					   struct ieee80211_chanctx_conf *ctx)
{
	if (!ctx)
		return NULL;

	return ath12k_mac_get_ar_by_chan(hw, ctx->def.chan);
}

struct ath12k *ath12k_get_ar_by_vif(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    u8 link_id)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k_link_vif *arvif;

	lockdep_assert_wiphy(hw->wiphy);

	/* If there is one pdev within ah, then we return
	 * ar directly.
	 */
	if (ah->num_radio == 1)
		return ah->radio;

	if (!(ahvif->links_map & BIT(link_id)))
		return NULL;

	arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
	if (arvif && arvif->is_created)
		return arvif->ar;

	return NULL;
}

void ath12k_mac_get_any_chanctx_conf_iter(struct ieee80211_hw *hw,
					  struct ieee80211_chanctx_conf *conf,
					  void *data)
{
	struct ath12k_mac_get_any_chanctx_conf_arg *arg = data;
	struct ath12k *ctx_ar = ath12k_get_ar_by_ctx(hw, conf);

	if (ctx_ar == arg->ar)
		arg->chanctx_conf = conf;
}

static struct ath12k_link_vif *ath12k_mac_get_vif_up(struct ath12k *ar)
{
	struct ath12k_link_vif *arvif;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (arvif->is_up)
			return arvif;
	}

	return NULL;
}

static bool ath12k_mac_band_match(enum nl80211_band band1, enum WMI_HOST_WLAN_BAND band2)
{
	switch (band1) {
	case NL80211_BAND_2GHZ:
		if (band2 & WMI_HOST_WLAN_2GHZ_CAP)
			return true;
		break;
	case NL80211_BAND_5GHZ:
	case NL80211_BAND_6GHZ:
		if (band2 & WMI_HOST_WLAN_5GHZ_CAP)
			return true;
		break;
	default:
		return false;
	}

	return false;
}

static u8 ath12k_mac_get_target_pdev_id_from_vif(struct ath12k_link_vif *arvif)
{
	struct ath12k *ar = arvif->ar;
	struct ath12k_base *ab = ar->ab;
	struct ieee80211_vif *vif = arvif->ahvif->vif;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	u8 pdev_id = ab->fw_pdev[0].pdev_id;
	int i;

	if (WARN_ON(ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)))
		return pdev_id;

	band = def.chan->band;

	for (i = 0; i < ab->fw_pdev_count; i++) {
		if (ath12k_mac_band_match(band, ab->fw_pdev[i].supported_bands))
			return ab->fw_pdev[i].pdev_id;
	}

	return pdev_id;
}

u8 ath12k_mac_get_target_pdev_id(struct ath12k *ar)
{
	struct ath12k_link_vif *arvif;
	struct ath12k_base *ab = ar->ab;

	if (!ab->hw_params->single_pdev_only)
		return ar->pdev->pdev_id;

	arvif = ath12k_mac_get_vif_up(ar);

	/* fw_pdev array has pdev ids derived from phy capability
	 * service ready event (pdev_and_hw_link_ids).
	 * If no vif is active, return default first index.
	 */
	if (!arvif)
		return ar->ab->fw_pdev[0].pdev_id;

	/* If active vif is found, return the pdev id matching chandef band */
	return ath12k_mac_get_target_pdev_id_from_vif(arvif);
}

static void ath12k_pdev_caps_update(struct ath12k *ar)
{
	struct ath12k_base *ab = ar->ab;

	ar->max_tx_power = ab->target_caps.hw_max_tx_power;

	/* FIXME: Set min_tx_power to ab->target_caps.hw_min_tx_power.
	 * But since the received value in svcrdy is same as hw_max_tx_power,
	 * we can set ar->min_tx_power to 0 currently until
	 * this is fixed in firmware
	 */
	ar->min_tx_power = 0;

	ar->txpower_limit_2g = ar->max_tx_power;
	ar->txpower_limit_5g = ar->max_tx_power;
	ar->txpower_scale = WMI_HOST_TP_SCALE_MAX;
}

static int ath12k_mac_txpower_recalc(struct ath12k *ar)
{
	struct ath12k_pdev *pdev = ar->pdev;
	struct ath12k_link_vif *arvif;
	int ret, txpower = -1;
	u32 param;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

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

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "txpower to set in hw %d\n",
		   txpower / 2);

	if ((pdev->cap.supported_bands & WMI_HOST_WLAN_2GHZ_CAP) &&
	    ar->txpower_limit_2g != txpower) {
		param = WMI_PDEV_PARAM_TXPOWER_LIMIT2G;
		ret = ath12k_wmi_pdev_set_param(ar, param,
						txpower, ar->pdev->pdev_id);
		if (ret)
			goto fail;
		ar->txpower_limit_2g = txpower;
	}

	if ((pdev->cap.supported_bands & WMI_HOST_WLAN_5GHZ_CAP) &&
	    ar->txpower_limit_5g != txpower) {
		param = WMI_PDEV_PARAM_TXPOWER_LIMIT5G;
		ret = ath12k_wmi_pdev_set_param(ar, param,
						txpower, ar->pdev->pdev_id);
		if (ret)
			goto fail;
		ar->txpower_limit_5g = txpower;
	}

	return 0;

fail:
	ath12k_warn(ar->ab, "failed to recalc txpower limit %d using pdev param %d: %d\n",
		    txpower / 2, param, ret);
	return ret;
}

static int ath12k_recalc_rtscts_prot(struct ath12k_link_vif *arvif)
{
	struct ath12k *ar = arvif->ar;
	u32 vdev_param, rts_cts;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

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

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac vdev %d recalc rts/cts prot %d\n",
		   arvif->vdev_id, rts_cts);

	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    vdev_param, rts_cts);
	if (ret)
		ath12k_warn(ar->ab, "failed to recalculate rts/cts prot for vdev %d: %d\n",
			    arvif->vdev_id, ret);

	return ret;
}

static int ath12k_mac_set_kickout(struct ath12k_link_vif *arvif)
{
	struct ath12k *ar = arvif->ar;
	u32 param;
	int ret;

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_STA_KICKOUT_TH,
					ATH12K_KICKOUT_THRESHOLD,
					ar->pdev->pdev_id);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set kickout threshold on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	param = WMI_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS;
	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, param,
					    ATH12K_KEEPALIVE_MIN_IDLE);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set keepalive minimum idle time on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	param = WMI_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS;
	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, param,
					    ATH12K_KEEPALIVE_MAX_IDLE);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set keepalive maximum idle time on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	param = WMI_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS;
	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, param,
					    ATH12K_KEEPALIVE_MAX_UNRESPONSIVE);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set keepalive maximum unresponsive time on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	return 0;
}

void ath12k_mac_peer_cleanup_all(struct ath12k *ar)
{
	struct ath12k_peer *peer, *tmp;
	struct ath12k_base *ab = ar->ab;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	spin_lock_bh(&ab->base_lock);
	list_for_each_entry_safe(peer, tmp, &ab->peers, list) {
		/* Skip Rx TID cleanup for self peer */
		if (peer->sta)
			ath12k_dp_rx_peer_tid_cleanup(ar, peer);

		list_del(&peer->list);
		kfree(peer);
	}
	spin_unlock_bh(&ab->base_lock);

	ar->num_peers = 0;
	ar->num_stations = 0;
}

static int ath12k_mac_vdev_setup_sync(struct ath12k *ar)
{
	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (test_bit(ATH12K_FLAG_CRASH_FLUSH, &ar->ab->dev_flags))
		return -ESHUTDOWN;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "vdev setup timeout %d\n",
		   ATH12K_VDEV_SETUP_TIMEOUT_HZ);

	if (!wait_for_completion_timeout(&ar->vdev_setup_done,
					 ATH12K_VDEV_SETUP_TIMEOUT_HZ))
		return -ETIMEDOUT;

	return ar->last_wmi_vdev_start_status ? -EINVAL : 0;
}

static int ath12k_monitor_vdev_up(struct ath12k *ar, int vdev_id)
{
	struct ath12k_wmi_vdev_up_params params = {};
	int ret;

	params.vdev_id = vdev_id;
	params.bssid = ar->mac_addr;
	ret = ath12k_wmi_vdev_up(ar, &params);
	if (ret) {
		ath12k_warn(ar->ab, "failed to put up monitor vdev %i: %d\n",
			    vdev_id, ret);
		return ret;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac monitor vdev %i started\n",
		   vdev_id);
	return 0;
}

static int ath12k_mac_monitor_vdev_start(struct ath12k *ar, int vdev_id,
					 struct cfg80211_chan_def *chandef)
{
	struct ieee80211_channel *channel;
	struct wmi_vdev_start_req_arg arg = {};
	struct ath12k_wmi_vdev_up_params params = {};
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	channel = chandef->chan;
	arg.vdev_id = vdev_id;
	arg.freq = channel->center_freq;
	arg.band_center_freq1 = chandef->center_freq1;
	arg.band_center_freq2 = chandef->center_freq2;
	arg.mode = ath12k_phymodes[chandef->chan->band][chandef->width];
	arg.chan_radar = !!(channel->flags & IEEE80211_CHAN_RADAR);

	arg.min_power = 0;
	arg.max_power = channel->max_power;
	arg.max_reg_power = channel->max_reg_power;
	arg.max_antenna_gain = channel->max_antenna_gain;

	arg.pref_tx_streams = ar->num_tx_chains;
	arg.pref_rx_streams = ar->num_rx_chains;
	arg.punct_bitmap = 0xFFFFFFFF;

	arg.passive |= !!(chandef->chan->flags & IEEE80211_CHAN_NO_IR);

	reinit_completion(&ar->vdev_setup_done);
	reinit_completion(&ar->vdev_delete_done);

	ret = ath12k_wmi_vdev_start(ar, &arg, false);
	if (ret) {
		ath12k_warn(ar->ab, "failed to request monitor vdev %i start: %d\n",
			    vdev_id, ret);
		return ret;
	}

	ret = ath12k_mac_vdev_setup_sync(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to synchronize setup for monitor vdev %i start: %d\n",
			    vdev_id, ret);
		return ret;
	}

	params.vdev_id = vdev_id;
	params.bssid = ar->mac_addr;
	ret = ath12k_wmi_vdev_up(ar, &params);
	if (ret) {
		ath12k_warn(ar->ab, "failed to put up monitor vdev %i: %d\n",
			    vdev_id, ret);
		goto vdev_stop;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac monitor vdev %i started\n",
		   vdev_id);
	return 0;

vdev_stop:
	ret = ath12k_wmi_vdev_stop(ar, vdev_id);
	if (ret)
		ath12k_warn(ar->ab, "failed to stop monitor vdev %i after start failure: %d\n",
			    vdev_id, ret);
	return ret;
}

static int ath12k_mac_monitor_vdev_stop(struct ath12k *ar)
{
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	reinit_completion(&ar->vdev_setup_done);

	ret = ath12k_wmi_vdev_stop(ar, ar->monitor_vdev_id);
	if (ret)
		ath12k_warn(ar->ab, "failed to request monitor vdev %i stop: %d\n",
			    ar->monitor_vdev_id, ret);

	ret = ath12k_mac_vdev_setup_sync(ar);
	if (ret)
		ath12k_warn(ar->ab, "failed to synchronize monitor vdev %i stop: %d\n",
			    ar->monitor_vdev_id, ret);

	ret = ath12k_wmi_vdev_down(ar, ar->monitor_vdev_id);
	if (ret)
		ath12k_warn(ar->ab, "failed to put down monitor vdev %i: %d\n",
			    ar->monitor_vdev_id, ret);

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac monitor vdev %i stopped\n",
		   ar->monitor_vdev_id);
	return ret;
}

static int ath12k_mac_monitor_vdev_delete(struct ath12k *ar)
{
	int ret;
	unsigned long time_left;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (!ar->monitor_vdev_created)
		return 0;

	reinit_completion(&ar->vdev_delete_done);

	ret = ath12k_wmi_vdev_delete(ar, ar->monitor_vdev_id);
	if (ret) {
		ath12k_warn(ar->ab, "failed to request wmi monitor vdev %i removal: %d\n",
			    ar->monitor_vdev_id, ret);
		return ret;
	}

	time_left = wait_for_completion_timeout(&ar->vdev_delete_done,
						ATH12K_VDEV_DELETE_TIMEOUT_HZ);
	if (time_left == 0) {
		ath12k_warn(ar->ab, "Timeout in receiving vdev delete response\n");
	} else {
		ar->allocated_vdev_map &= ~(1LL << ar->monitor_vdev_id);
		ar->ab->free_vdev_map |= 1LL << (ar->monitor_vdev_id);
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac monitor vdev %d deleted\n",
			   ar->monitor_vdev_id);
		ar->num_created_vdevs--;
		ar->monitor_vdev_id = -1;
		ar->monitor_vdev_created = false;
	}

	return ret;
}

static int ath12k_mac_monitor_start(struct ath12k *ar)
{
	struct ath12k_mac_get_any_chanctx_conf_arg arg;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (ar->monitor_started)
		return 0;

	arg.ar = ar;
	arg.chanctx_conf = NULL;
	ieee80211_iter_chan_contexts_atomic(ath12k_ar_to_hw(ar),
					    ath12k_mac_get_any_chanctx_conf_iter,
					    &arg);
	if (!arg.chanctx_conf)
		return 0;

	ret = ath12k_mac_monitor_vdev_start(ar, ar->monitor_vdev_id,
					    &arg.chanctx_conf->def);
	if (ret) {
		ath12k_warn(ar->ab, "failed to start monitor vdev: %d\n", ret);
		return ret;
	}

	ret = ath12k_dp_tx_htt_monitor_mode_ring_config(ar, false);
	if (ret) {
		ath12k_warn(ar->ab, "fail to set monitor filter: %d\n", ret);
		return ret;
	}

	ar->monitor_started = true;
	ar->num_started_vdevs++;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac monitor started\n");

	return 0;
}

static int ath12k_mac_monitor_stop(struct ath12k *ar)
{
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (!ar->monitor_started)
		return 0;

	ret = ath12k_mac_monitor_vdev_stop(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to stop monitor vdev: %d\n", ret);
		return ret;
	}

	ar->monitor_started = false;
	ar->num_started_vdevs--;
	ret = ath12k_dp_tx_htt_monitor_mode_ring_config(ar, true);
	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac monitor stopped ret %d\n", ret);
	return ret;
}

int ath12k_mac_vdev_stop(struct ath12k_link_vif *arvif)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k *ar = arvif->ar;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	reinit_completion(&ar->vdev_setup_done);

	ret = ath12k_wmi_vdev_stop(ar, arvif->vdev_id);
	if (ret) {
		ath12k_warn(ar->ab, "failed to stop WMI vdev %i: %d\n",
			    arvif->vdev_id, ret);
		goto err;
	}

	ret = ath12k_mac_vdev_setup_sync(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to synchronize setup for vdev %i: %d\n",
			    arvif->vdev_id, ret);
		goto err;
	}

	WARN_ON(ar->num_started_vdevs == 0);

	ar->num_started_vdevs--;
	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "vdev %pM stopped, vdev_id %d\n",
		   ahvif->vif->addr, arvif->vdev_id);

	if (test_bit(ATH12K_FLAG_CAC_RUNNING, &ar->dev_flags)) {
		clear_bit(ATH12K_FLAG_CAC_RUNNING, &ar->dev_flags);
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "CAC Stopped for vdev %d\n",
			   arvif->vdev_id);
	}

	return 0;
err:
	return ret;
}

static int ath12k_mac_op_config(struct ieee80211_hw *hw, int radio_idx, u32 changed)
{
	return 0;
}

static int ath12k_mac_setup_bcn_p2p_ie(struct ath12k_link_vif *arvif,
				       struct sk_buff *bcn)
{
	struct ath12k *ar = arvif->ar;
	struct ieee80211_mgmt *mgmt;
	const u8 *p2p_ie;
	int ret;

	mgmt = (void *)bcn->data;
	p2p_ie = cfg80211_find_vendor_ie(WLAN_OUI_WFA, WLAN_OUI_TYPE_WFA_P2P,
					 mgmt->u.beacon.variable,
					 bcn->len - (mgmt->u.beacon.variable -
						     bcn->data));
	if (!p2p_ie) {
		ath12k_warn(ar->ab, "no P2P ie found in beacon\n");
		return -ENOENT;
	}

	ret = ath12k_wmi_p2p_go_bcn_ie(ar, arvif->vdev_id, p2p_ie);
	if (ret) {
		ath12k_warn(ar->ab, "failed to submit P2P GO bcn ie for vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	return 0;
}

static int ath12k_mac_remove_vendor_ie(struct sk_buff *skb, unsigned int oui,
				       u8 oui_type, size_t ie_offset)
{
	const u8 *next, *end;
	size_t len;
	u8 *ie;

	if (WARN_ON(skb->len < ie_offset))
		return -EINVAL;

	ie = (u8 *)cfg80211_find_vendor_ie(oui, oui_type,
					   skb->data + ie_offset,
					   skb->len - ie_offset);
	if (!ie)
		return -ENOENT;

	len = ie[1] + 2;
	end = skb->data + skb->len;
	next = ie + len;

	if (WARN_ON(next > end))
		return -EINVAL;

	memmove(ie, next, end - next);
	skb_trim(skb, skb->len - len);

	return 0;
}

static void ath12k_mac_set_arvif_ies(struct ath12k_link_vif *arvif, struct sk_buff *bcn,
				     u8 bssid_index, bool *nontx_profile_found)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)bcn->data;
	const struct element *elem, *nontx, *index, *nie;
	const u8 *start, *tail;
	u16 rem_len;
	u8 i;

	start = bcn->data + ieee80211_get_hdrlen_from_skb(bcn) + sizeof(mgmt->u.beacon);
	tail = skb_tail_pointer(bcn);
	rem_len = tail - start;

	arvif->rsnie_present = false;
	arvif->wpaie_present = false;

	if (cfg80211_find_ie(WLAN_EID_RSN, start, rem_len))
		arvif->rsnie_present = true;
	if (cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT, WLAN_OUI_TYPE_MICROSOFT_WPA,
				    start, rem_len))
		arvif->wpaie_present = true;

	/* Return from here for the transmitted profile */
	if (!bssid_index)
		return;

	/* Initial rsnie_present for the nontransmitted profile is set to be same as that
	 * of the transmitted profile. It will be changed if security configurations are
	 * different.
	 */
	*nontx_profile_found = false;
	for_each_element_id(elem, WLAN_EID_MULTIPLE_BSSID, start, rem_len) {
		/* Fixed minimum MBSSID element length with at least one
		 * nontransmitted BSSID profile is 12 bytes as given below;
		 * 1 (max BSSID indicator) +
		 * 2 (Nontransmitted BSSID profile: Subelement ID + length) +
		 * 4 (Nontransmitted BSSID Capabilities: tag + length + info)
		 * 2 (Nontransmitted BSSID SSID: tag + length)
		 * 3 (Nontransmitted BSSID Index: tag + length + BSSID index
		 */
		if (elem->datalen < 12 || elem->data[0] < 1)
			continue; /* Max BSSID indicator must be >=1 */

		for_each_element(nontx, elem->data + 1, elem->datalen - 1) {
			start = nontx->data;

			if (nontx->id != 0 || nontx->datalen < 4)
				continue; /* Invalid nontransmitted profile */

			if (nontx->data[0] != WLAN_EID_NON_TX_BSSID_CAP ||
			    nontx->data[1] != 2) {
				continue; /* Missing nontransmitted BSS capabilities */
			}

			if (nontx->data[4] != WLAN_EID_SSID)
				continue; /* Missing SSID for nontransmitted BSS */

			index = cfg80211_find_elem(WLAN_EID_MULTI_BSSID_IDX,
						   start, nontx->datalen);
			if (!index || index->datalen < 1 || index->data[0] == 0)
				continue; /* Invalid MBSSID Index element */

			if (index->data[0] == bssid_index) {
				*nontx_profile_found = true;
				if (cfg80211_find_ie(WLAN_EID_RSN,
						     nontx->data,
						     nontx->datalen)) {
					arvif->rsnie_present = true;
					return;
				} else if (!arvif->rsnie_present) {
					return; /* Both tx and nontx BSS are open */
				}

				nie = cfg80211_find_ext_elem(WLAN_EID_EXT_NON_INHERITANCE,
							     nontx->data,
							     nontx->datalen);
				if (!nie || nie->datalen < 2)
					return; /* Invalid non-inheritance element */

				for (i = 1; i < nie->datalen - 1; i++) {
					if (nie->data[i] == WLAN_EID_RSN) {
						arvif->rsnie_present = false;
						break;
					}
				}

				return;
			}
		}
	}
}

static int ath12k_mac_setup_bcn_tmpl_ema(struct ath12k_link_vif *arvif,
					 struct ath12k_link_vif *tx_arvif,
					 u8 bssid_index)
{
	struct ath12k_wmi_bcn_tmpl_ema_arg ema_args;
	struct ieee80211_ema_beacons *beacons;
	bool nontx_profile_found = false;
	int ret = 0;
	u8 i;

	beacons = ieee80211_beacon_get_template_ema_list(ath12k_ar_to_hw(tx_arvif->ar),
							 tx_arvif->ahvif->vif,
							 tx_arvif->link_id);
	if (!beacons || !beacons->cnt) {
		ath12k_warn(arvif->ar->ab,
			    "failed to get ema beacon templates from mac80211\n");
		return -EPERM;
	}

	if (tx_arvif == arvif)
		ath12k_mac_set_arvif_ies(arvif, beacons->bcn[0].skb, 0, NULL);

	for (i = 0; i < beacons->cnt; i++) {
		if (tx_arvif != arvif && !nontx_profile_found)
			ath12k_mac_set_arvif_ies(arvif, beacons->bcn[i].skb,
						 bssid_index,
						 &nontx_profile_found);

		ema_args.bcn_cnt = beacons->cnt;
		ema_args.bcn_index = i;
		ret = ath12k_wmi_bcn_tmpl(tx_arvif, &beacons->bcn[i].offs,
					  beacons->bcn[i].skb, &ema_args);
		if (ret) {
			ath12k_warn(tx_arvif->ar->ab,
				    "failed to set ema beacon template id %i error %d\n",
				    i, ret);
			break;
		}
	}

	if (tx_arvif != arvif && !nontx_profile_found)
		ath12k_warn(arvif->ar->ab,
			    "nontransmitted bssid index %u not found in beacon template\n",
			    bssid_index);

	ieee80211_beacon_free_ema_list(beacons);
	return ret;
}

static int ath12k_mac_setup_bcn_tmpl(struct ath12k_link_vif *arvif)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(ahvif);
	struct ieee80211_bss_conf *link_conf;
	struct ath12k_link_vif *tx_arvif;
	struct ath12k *ar = arvif->ar;
	struct ath12k_base *ab = ar->ab;
	struct ieee80211_mutable_offsets offs = {};
	bool nontx_profile_found = false;
	struct sk_buff *bcn;
	int ret;

	if (ahvif->vdev_type != WMI_VDEV_TYPE_AP)
		return 0;

	link_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!link_conf) {
		ath12k_warn(ar->ab, "unable to access bss link conf to set bcn tmpl for vif %pM link %u\n",
			    vif->addr, arvif->link_id);
		return -ENOLINK;
	}

	tx_arvif = ath12k_mac_get_tx_arvif(arvif, link_conf);
	if (tx_arvif) {
		if (tx_arvif != arvif && arvif->is_up)
			return 0;

		if (link_conf->ema_ap)
			return ath12k_mac_setup_bcn_tmpl_ema(arvif, tx_arvif,
							     link_conf->bssid_index);
	} else {
		tx_arvif = arvif;
	}

	bcn = ieee80211_beacon_get_template(ath12k_ar_to_hw(tx_arvif->ar),
					    tx_arvif->ahvif->vif,
					    &offs, tx_arvif->link_id);
	if (!bcn) {
		ath12k_warn(ab, "failed to get beacon template from mac80211\n");
		return -EPERM;
	}

	if (tx_arvif == arvif) {
		ath12k_mac_set_arvif_ies(arvif, bcn, 0, NULL);
	} else {
		ath12k_mac_set_arvif_ies(arvif, bcn,
					 link_conf->bssid_index,
					 &nontx_profile_found);
		if (!nontx_profile_found)
			ath12k_warn(ab,
				    "nontransmitted profile not found in beacon template\n");
	}

	if (ahvif->vif->type == NL80211_IFTYPE_AP && ahvif->vif->p2p) {
		ret = ath12k_mac_setup_bcn_p2p_ie(arvif, bcn);
		if (ret) {
			ath12k_warn(ab, "failed to setup P2P GO bcn ie: %d\n",
				    ret);
			goto free_bcn_skb;
		}

		/* P2P IE is inserted by firmware automatically (as
		 * configured above) so remove it from the base beacon
		 * template to avoid duplicate P2P IEs in beacon frames.
		 */
		ret = ath12k_mac_remove_vendor_ie(bcn, WLAN_OUI_WFA,
						  WLAN_OUI_TYPE_WFA_P2P,
						  offsetof(struct ieee80211_mgmt,
							   u.beacon.variable));
		if (ret) {
			ath12k_warn(ab, "failed to remove P2P vendor ie: %d\n",
				    ret);
			goto free_bcn_skb;
		}
	}

	ret = ath12k_wmi_bcn_tmpl(arvif, &offs, bcn, NULL);

	if (ret)
		ath12k_warn(ab, "failed to submit beacon template command: %d\n",
			    ret);

free_bcn_skb:
	kfree_skb(bcn);
	return ret;
}

static void ath12k_control_beaconing(struct ath12k_link_vif *arvif,
				     struct ieee80211_bss_conf *info)
{
	struct ath12k_wmi_vdev_up_params params = {};
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k *ar = arvif->ar;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(arvif->ar)->wiphy);

	if (!info->enable_beacon) {
		ret = ath12k_wmi_vdev_down(ar, arvif->vdev_id);
		if (ret)
			ath12k_warn(ar->ab, "failed to down vdev_id %i: %d\n",
				    arvif->vdev_id, ret);

		arvif->is_up = false;
		return;
	}

	/* Install the beacon template to the FW */
	ret = ath12k_mac_setup_bcn_tmpl(arvif);
	if (ret) {
		ath12k_warn(ar->ab, "failed to update bcn tmpl during vdev up: %d\n",
			    ret);
		return;
	}

	ahvif->aid = 0;

	ether_addr_copy(arvif->bssid, info->addr);

	params.vdev_id = arvif->vdev_id;
	params.aid = ahvif->aid;
	params.bssid = arvif->bssid;
	params.tx_bssid = ath12k_mac_get_tx_bssid(arvif);
	if (params.tx_bssid) {
		params.nontx_profile_idx = info->bssid_index;
		params.nontx_profile_cnt = 1 << info->bssid_indicator;
	}
	ret = ath12k_wmi_vdev_up(arvif->ar, &params);
	if (ret) {
		ath12k_warn(ar->ab, "failed to bring up vdev %d: %i\n",
			    arvif->vdev_id, ret);
		return;
	}

	arvif->is_up = true;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac vdev %d up\n", arvif->vdev_id);
}

static void ath12k_mac_handle_beacon_iter(void *data, u8 *mac,
					  struct ieee80211_vif *vif)
{
	struct sk_buff *skb = data;
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif = &ahvif->deflink;

	if (vif->type != NL80211_IFTYPE_STATION || !arvif->is_created)
		return;

	if (!ether_addr_equal(mgmt->bssid, vif->bss_conf.bssid))
		return;

	cancel_delayed_work(&arvif->connection_loss_work);
}

void ath12k_mac_handle_beacon(struct ath12k *ar, struct sk_buff *skb)
{
	ieee80211_iterate_active_interfaces_atomic(ath12k_ar_to_hw(ar),
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath12k_mac_handle_beacon_iter,
						   skb);
}

static void ath12k_mac_handle_beacon_miss_iter(void *data, u8 *mac,
					       struct ieee80211_vif *vif)
{
	u32 *vdev_id = data;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif = &ahvif->deflink;
	struct ieee80211_hw *hw;

	if (!arvif->is_created || arvif->vdev_id != *vdev_id)
		return;

	if (!arvif->is_up)
		return;

	ieee80211_beacon_loss(vif);
	hw = ath12k_ar_to_hw(arvif->ar);

	/* Firmware doesn't report beacon loss events repeatedly. If AP probe
	 * (done by mac80211) succeeds but beacons do not resume then it
	 * doesn't make sense to continue operation. Queue connection loss work
	 * which can be cancelled when beacon is received.
	 */
	ieee80211_queue_delayed_work(hw, &arvif->connection_loss_work,
				     ATH12K_CONNECTION_LOSS_HZ);
}

void ath12k_mac_handle_beacon_miss(struct ath12k *ar, u32 vdev_id)
{
	ieee80211_iterate_active_interfaces_atomic(ath12k_ar_to_hw(ar),
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath12k_mac_handle_beacon_miss_iter,
						   &vdev_id);
}

static void ath12k_mac_vif_sta_connection_loss_work(struct work_struct *work)
{
	struct ath12k_link_vif *arvif = container_of(work, struct ath12k_link_vif,
						     connection_loss_work.work);
	struct ieee80211_vif *vif = arvif->ahvif->vif;

	if (!arvif->is_up)
		return;

	ieee80211_connection_loss(vif);
}

static void ath12k_peer_assoc_h_basic(struct ath12k *ar,
				      struct ath12k_link_vif *arvif,
				      struct ath12k_link_sta *arsta,
				      struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	struct ieee80211_hw *hw = ath12k_ar_to_hw(ar);
	struct ieee80211_bss_conf *bss_conf;
	u32 aid;

	lockdep_assert_wiphy(hw->wiphy);

	if (vif->type == NL80211_IFTYPE_STATION)
		aid = vif->cfg.aid;
	else
		aid = sta->aid;

	ether_addr_copy(arg->peer_mac, arsta->addr);
	arg->vdev_id = arvif->vdev_id;
	arg->peer_associd = aid;
	arg->auth_flag = true;
	/* TODO: STA WAR in ath10k for listen interval required? */
	arg->peer_listen_intval = hw->conf.listen_interval;
	arg->peer_nss = 1;

	bss_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!bss_conf) {
		ath12k_warn(ar->ab, "unable to access bss link conf in peer assoc for vif %pM link %u\n",
			    vif->addr, arvif->link_id);
		return;
	}

	arg->peer_caps = bss_conf->assoc_capability;
}

static void ath12k_peer_assoc_h_crypto(struct ath12k *ar,
				       struct ath12k_link_vif *arvif,
				       struct ath12k_link_sta *arsta,
				       struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	struct ieee80211_bss_conf *info;
	struct cfg80211_chan_def def;
	struct cfg80211_bss *bss;
	struct ieee80211_hw *hw = ath12k_ar_to_hw(ar);
	const u8 *rsnie = NULL;
	const u8 *wpaie = NULL;

	lockdep_assert_wiphy(hw->wiphy);

	info = ath12k_mac_get_link_bss_conf(arvif);
	if (!info) {
		ath12k_warn(ar->ab, "unable to access bss link conf for peer assoc crypto for vif %pM link %u\n",
			    vif->addr, arvif->link_id);
		return;
	}

	if (WARN_ON(ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)))
		return;

	bss = cfg80211_get_bss(hw->wiphy, def.chan, info->bssid, NULL, 0,
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
		cfg80211_put_bss(hw->wiphy, bss);
	}

	/* FIXME: base on RSN IE/WPA IE is a correct idea? */
	if (rsnie || wpaie) {
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "%s: rsn ie found\n", __func__);
		arg->need_ptk_4_way = true;
	}

	if (wpaie) {
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "%s: wpa ie found\n", __func__);
		arg->need_gtk_2_way = true;
	}

	if (sta->mfp) {
		/* TODO: Need to check if FW supports PMF? */
		arg->is_pmf_enabled = true;
	}

	/* TODO: safe_mode_enabled (bypass 4-way handshake) flag req? */
}

static void ath12k_peer_assoc_h_rates(struct ath12k *ar,
				      struct ath12k_link_vif *arvif,
				      struct ath12k_link_sta *arsta,
				      struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	struct wmi_rate_set_arg *rateset = &arg->peer_legacy_rates;
	struct ieee80211_link_sta *link_sta;
	struct cfg80211_chan_def def;
	const struct ieee80211_supported_band *sband;
	const struct ieee80211_rate *rates;
	struct ieee80211_hw *hw = ath12k_ar_to_hw(ar);
	enum nl80211_band band;
	u32 ratemask;
	u8 rate;
	int i;

	lockdep_assert_wiphy(hw->wiphy);

	if (WARN_ON(ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)))
		return;

	link_sta = ath12k_mac_get_link_sta(arsta);
	if (!link_sta) {
		ath12k_warn(ar->ab, "unable to access link sta in peer assoc rates for sta %pM link %u\n",
			    sta->addr, arsta->link_id);
		return;
	}

	band = def.chan->band;
	sband = hw->wiphy->bands[band];
	ratemask = link_sta->supp_rates[band];
	ratemask &= arvif->bitrate_mask.control[band].legacy;
	rates = sband->bitrates;

	rateset->num_rates = 0;

	for (i = 0; i < 32; i++, ratemask >>= 1, rates++) {
		if (!(ratemask & 1))
			continue;

		rate = ath12k_mac_bitrate_to_rate(rates->bitrate);
		rateset->rates[rateset->num_rates] = rate;
		rateset->num_rates++;
	}
}

static bool
ath12k_peer_assoc_h_ht_masked(const u8 *ht_mcs_mask)
{
	int nss;

	for (nss = 0; nss < IEEE80211_HT_MCS_MASK_LEN; nss++)
		if (ht_mcs_mask[nss])
			return false;

	return true;
}

static bool
ath12k_peer_assoc_h_vht_masked(const u16 *vht_mcs_mask)
{
	int nss;

	for (nss = 0; nss < NL80211_VHT_NSS_MAX; nss++)
		if (vht_mcs_mask[nss])
			return false;

	return true;
}

static void ath12k_peer_assoc_h_ht(struct ath12k *ar,
				   struct ath12k_link_vif *arvif,
				   struct ath12k_link_sta *arsta,
				   struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	const struct ieee80211_sta_ht_cap *ht_cap;
	struct ieee80211_link_sta *link_sta;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	const u8 *ht_mcs_mask;
	int i, n;
	u8 max_nss;
	u32 stbc;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (WARN_ON(ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)))
		return;

	link_sta = ath12k_mac_get_link_sta(arsta);
	if (!link_sta) {
		ath12k_warn(ar->ab, "unable to access link sta in peer assoc ht for sta %pM link %u\n",
			    sta->addr, arsta->link_id);
		return;
	}

	ht_cap = &link_sta->ht_cap;
	if (!ht_cap->ht_supported)
		return;

	band = def.chan->band;
	ht_mcs_mask = arvif->bitrate_mask.control[band].ht_mcs;

	if (ath12k_peer_assoc_h_ht_masked(ht_mcs_mask))
		return;

	arg->ht_flag = true;

	arg->peer_max_mpdu = (1 << (IEEE80211_HT_MAX_AMPDU_FACTOR +
				    ht_cap->ampdu_factor)) - 1;

	arg->peer_mpdu_density =
		ath12k_parse_mpdudensity(ht_cap->ampdu_density);

	arg->peer_ht_caps = ht_cap->cap;
	arg->peer_rate_caps |= WMI_HOST_RC_HT_FLAG;

	if (ht_cap->cap & IEEE80211_HT_CAP_LDPC_CODING)
		arg->ldpc_flag = true;

	if (link_sta->bandwidth >= IEEE80211_STA_RX_BW_40) {
		arg->bw_40 = true;
		arg->peer_rate_caps |= WMI_HOST_RC_CW40_FLAG;
	}

	/* As firmware handles these two flags (IEEE80211_HT_CAP_SGI_20
	 * and IEEE80211_HT_CAP_SGI_40) for enabling SGI, reset both
	 * flags if guard interval is to force Long GI
	 */
	if (arvif->bitrate_mask.control[band].gi == NL80211_TXRATE_FORCE_LGI) {
		arg->peer_ht_caps &= ~(IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40);
	} else {
		/* Enable SGI flag if either SGI_20 or SGI_40 is supported */
		if (ht_cap->cap & (IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40))
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
		arg->peer_nss = min(link_sta->rx_nss, max_nss);
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac ht peer %pM mcs cnt %d nss %d\n",
		   arg->peer_mac,
		   arg->peer_ht_rates.num_rates,
		   arg->peer_nss);
}

static int ath12k_mac_get_max_vht_mcs_map(u16 mcs_map, int nss)
{
	switch ((mcs_map >> (2 * nss)) & 0x3) {
	case IEEE80211_VHT_MCS_SUPPORT_0_7: return BIT(8) - 1;
	case IEEE80211_VHT_MCS_SUPPORT_0_8: return BIT(9) - 1;
	case IEEE80211_VHT_MCS_SUPPORT_0_9: return BIT(10) - 1;
	}
	return 0;
}

static u16
ath12k_peer_assoc_h_vht_limit(u16 tx_mcs_set,
			      const u16 vht_mcs_limit[NL80211_VHT_NSS_MAX])
{
	int idx_limit;
	int nss;
	u16 mcs_map;
	u16 mcs;

	for (nss = 0; nss < NL80211_VHT_NSS_MAX; nss++) {
		mcs_map = ath12k_mac_get_max_vht_mcs_map(tx_mcs_set, nss) &
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

static void ath12k_peer_assoc_h_vht(struct ath12k *ar,
				    struct ath12k_link_vif *arvif,
				    struct ath12k_link_sta *arsta,
				    struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	const struct ieee80211_sta_vht_cap *vht_cap;
	struct ieee80211_link_sta *link_sta;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	u16 *vht_mcs_mask;
	u16 tx_mcs_map;
	u8 ampdu_factor;
	u8 max_nss, vht_mcs;
	int i, vht_nss, nss_idx;
	bool user_rate_valid = true;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (WARN_ON(ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)))
		return;

	link_sta = ath12k_mac_get_link_sta(arsta);
	if (!link_sta) {
		ath12k_warn(ar->ab, "unable to access link sta in peer assoc vht for sta %pM link %u\n",
			    sta->addr, arsta->link_id);
		return;
	}

	vht_cap = &link_sta->vht_cap;
	if (!vht_cap->vht_supported)
		return;

	band = def.chan->band;
	vht_mcs_mask = arvif->bitrate_mask.control[band].vht_mcs;

	if (ath12k_peer_assoc_h_vht_masked(vht_mcs_mask))
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

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_80)
		arg->bw_80 = true;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_160)
		arg->bw_160 = true;

	vht_nss =  ath12k_mac_max_vht_nss(vht_mcs_mask);

	if (vht_nss > link_sta->rx_nss) {
		user_rate_valid = false;
		for (nss_idx = link_sta->rx_nss - 1; nss_idx >= 0; nss_idx--) {
			if (vht_mcs_mask[nss_idx]) {
				user_rate_valid = true;
				break;
			}
		}
	}

	if (!user_rate_valid) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
			   "Setting vht range MCS value to peer supported nss:%d for peer %pM\n",
			   link_sta->rx_nss, arsta->addr);
		vht_mcs_mask[link_sta->rx_nss - 1] = vht_mcs_mask[vht_nss - 1];
	}

	/* Calculate peer NSS capability from VHT capabilities if STA
	 * supports VHT.
	 */
	for (i = 0, max_nss = 0, vht_mcs = 0; i < NL80211_VHT_NSS_MAX; i++) {
		vht_mcs = __le16_to_cpu(vht_cap->vht_mcs.rx_mcs_map) >>
			  (2 * i) & 3;

		if (vht_mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED &&
		    vht_mcs_mask[i])
			max_nss = i + 1;
	}
	arg->peer_nss = min(link_sta->rx_nss, max_nss);
	arg->rx_max_rate = __le16_to_cpu(vht_cap->vht_mcs.rx_highest);
	arg->rx_mcs_set = __le16_to_cpu(vht_cap->vht_mcs.rx_mcs_map);
	arg->tx_max_rate = __le16_to_cpu(vht_cap->vht_mcs.tx_highest);

	tx_mcs_map = __le16_to_cpu(vht_cap->vht_mcs.tx_mcs_map);
	arg->tx_mcs_set = ath12k_peer_assoc_h_vht_limit(tx_mcs_map, vht_mcs_mask);

	/* In QCN9274 platform, VHT MCS rate 10 and 11 is enabled by default.
	 * VHT MCS rate 10 and 11 is not supported in 11ac standard.
	 * so explicitly disable the VHT MCS rate 10 and 11 in 11ac mode.
	 */
	arg->tx_mcs_set &= ~IEEE80211_VHT_MCS_SUPPORT_0_11_MASK;
	arg->tx_mcs_set |= IEEE80211_DISABLE_VHT_MCS_SUPPORT_0_11;

	if ((arg->tx_mcs_set & IEEE80211_VHT_MCS_NOT_SUPPORTED) ==
			IEEE80211_VHT_MCS_NOT_SUPPORTED)
		arg->peer_vht_caps &= ~IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;

	/* TODO:  Check */
	arg->tx_max_mcs_nss = 0xFF;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac vht peer %pM max_mpdu %d flags 0x%x\n",
		   arsta->addr, arg->peer_max_mpdu, arg->peer_flags);

	/* TODO: rxnss_override */
}

static int ath12k_mac_get_max_he_mcs_map(u16 mcs_map, int nss)
{
	switch ((mcs_map >> (2 * nss)) & 0x3) {
	case IEEE80211_HE_MCS_SUPPORT_0_7: return BIT(8) - 1;
	case IEEE80211_HE_MCS_SUPPORT_0_9: return BIT(10) - 1;
	case IEEE80211_HE_MCS_SUPPORT_0_11: return BIT(12) - 1;
	}
	return 0;
}

static u16 ath12k_peer_assoc_h_he_limit(u16 tx_mcs_set,
					const u16 *he_mcs_limit)
{
	int idx_limit;
	int nss;
	u16 mcs_map;
	u16 mcs;

	for (nss = 0; nss < NL80211_HE_NSS_MAX; nss++) {
		mcs_map = ath12k_mac_get_max_he_mcs_map(tx_mcs_set, nss) &
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
ath12k_peer_assoc_h_he_masked(const u16 he_mcs_mask[NL80211_HE_NSS_MAX])
{
	int nss;

	for (nss = 0; nss < NL80211_HE_NSS_MAX; nss++)
		if (he_mcs_mask[nss])
			return false;

	return true;
}

static void ath12k_peer_assoc_h_he(struct ath12k *ar,
				   struct ath12k_link_vif *arvif,
				   struct ath12k_link_sta *arsta,
				   struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	const struct ieee80211_sta_he_cap *he_cap;
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_link_sta *link_sta;
	struct cfg80211_chan_def def;
	int i;
	u8 ampdu_factor, max_nss;
	u8 rx_mcs_80 = IEEE80211_HE_MCS_NOT_SUPPORTED;
	u8 rx_mcs_160 = IEEE80211_HE_MCS_NOT_SUPPORTED;
	u16 mcs_160_map, mcs_80_map;
	u8 link_id = arvif->link_id;
	bool support_160;
	enum nl80211_band band;
	u16 *he_mcs_mask;
	u8 he_mcs;
	u16 he_tx_mcs = 0, v = 0;
	int he_nss, nss_idx;
	bool user_rate_valid = true;

	if (WARN_ON(ath12k_mac_vif_link_chan(vif, link_id, &def)))
		return;

	link_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!link_conf) {
		ath12k_warn(ar->ab, "unable to access bss link conf in peer assoc he for vif %pM link %u",
			    vif->addr, link_id);
		return;
	}

	link_sta = ath12k_mac_get_link_sta(arsta);
	if (!link_sta) {
		ath12k_warn(ar->ab, "unable to access link sta in peer assoc he for sta %pM link %u\n",
			    sta->addr, arsta->link_id);
		return;
	}

	he_cap = &link_sta->he_cap;
	if (!he_cap->has_he)
		return;

	band = def.chan->band;
	he_mcs_mask = arvif->bitrate_mask.control[band].he_mcs;

	if (ath12k_peer_assoc_h_he_masked(he_mcs_mask))
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

			if (mcs_160 != IEEE80211_HE_MCS_NOT_SUPPORTED) {
				rx_mcs_160 = i + 1;
				break;
			}
		}
	}

	for (i = 7; i >= 0; i--) {
		u8 mcs_80 = (mcs_80_map >> (2 * i)) & 3;

		if (mcs_80 != IEEE80211_HE_MCS_NOT_SUPPORTED) {
			rx_mcs_80 = i + 1;
			break;
		}
	}

	if (support_160)
		max_nss = min(rx_mcs_80, rx_mcs_160);
	else
		max_nss = rx_mcs_80;

	arg->peer_nss = min(link_sta->rx_nss, max_nss);

	memcpy(&arg->peer_he_cap_macinfo, he_cap->he_cap_elem.mac_cap_info,
	       sizeof(he_cap->he_cap_elem.mac_cap_info));
	memcpy(&arg->peer_he_cap_phyinfo, he_cap->he_cap_elem.phy_cap_info,
	       sizeof(he_cap->he_cap_elem.phy_cap_info));
	arg->peer_he_ops = link_conf->he_oper.params;

	/* the top most byte is used to indicate BSS color info */
	arg->peer_he_ops &= 0xffffff;

	/* As per section 26.6.1 IEEE Std 802.11ax‐2022, if the Max AMPDU
	 * Exponent Extension in HE cap is zero, use the arg->peer_max_mpdu
	 * as calculated while parsing VHT caps(if VHT caps is present)
	 * or HT caps (if VHT caps is not present).
	 *
	 * For non-zero value of Max AMPDU Exponent Extension in HE MAC caps,
	 * if a HE STA sends VHT cap and HE cap IE in assoc request then, use
	 * MAX_AMPDU_LEN_FACTOR as 20 to calculate max_ampdu length.
	 * If a HE STA that does not send VHT cap, but HE and HT cap in assoc
	 * request, then use MAX_AMPDU_LEN_FACTOR as 16 to calculate max_ampdu
	 * length.
	 */
	ampdu_factor = u8_get_bits(he_cap->he_cap_elem.mac_cap_info[3],
				   IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK);

	if (ampdu_factor) {
		if (link_sta->vht_cap.vht_supported)
			arg->peer_max_mpdu = (1 << (IEEE80211_HE_VHT_MAX_AMPDU_FACTOR +
						    ampdu_factor)) - 1;
		else if (link_sta->ht_cap.ht_supported)
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

	he_nss = ath12k_mac_max_he_nss(he_mcs_mask);

	if (he_nss > link_sta->rx_nss) {
		user_rate_valid = false;
		for (nss_idx = link_sta->rx_nss - 1; nss_idx >= 0; nss_idx--) {
			if (he_mcs_mask[nss_idx]) {
				user_rate_valid = true;
				break;
			}
		}
	}

	if (!user_rate_valid) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
			   "Setting he range MCS value to peer supported nss:%d for peer %pM\n",
			   link_sta->rx_nss, arsta->addr);
		he_mcs_mask[link_sta->rx_nss - 1] = he_mcs_mask[he_nss - 1];
	}

	switch (link_sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		if (he_cap->he_cap_elem.phy_cap_info[0] &
		    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) {
			v = ath12k_peer_assoc_h_he_limit(v, he_mcs_mask);
			arg->peer_he_rx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_80_80] = v;

			v = le16_to_cpu(he_cap->he_mcs_nss_supp.tx_mcs_80p80);
			arg->peer_he_tx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_80_80] = v;

			arg->peer_he_mcs_count++;
			he_tx_mcs = v;
		}
		v = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_160);
		arg->peer_he_rx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_160] = v;

		v = ath12k_peer_assoc_h_he_limit(v, he_mcs_mask);
		arg->peer_he_tx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_160] = v;

		arg->peer_he_mcs_count++;
		if (!he_tx_mcs)
			he_tx_mcs = v;
		fallthrough;

	default:
		v = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_80);
		arg->peer_he_rx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_80] = v;

		v = le16_to_cpu(he_cap->he_mcs_nss_supp.tx_mcs_80);
		v = ath12k_peer_assoc_h_he_limit(v, he_mcs_mask);
		arg->peer_he_tx_mcs_set[WMI_HECAP_TXRX_MCS_NSS_IDX_80] = v;

		arg->peer_he_mcs_count++;
		if (!he_tx_mcs)
			he_tx_mcs = v;
		break;
	}

	/* Calculate peer NSS capability from HE capabilities if STA
	 * supports HE.
	 */
	for (i = 0, max_nss = 0, he_mcs = 0; i < NL80211_HE_NSS_MAX; i++) {
		he_mcs = he_tx_mcs >> (2 * i) & 3;

		/* In case of fixed rates, MCS Range in he_tx_mcs might have
		 * unsupported range, with he_mcs_mask set, so check either of them
		 * to find nss.
		 */
		if (he_mcs != IEEE80211_HE_MCS_NOT_SUPPORTED ||
		    he_mcs_mask[i])
			max_nss = i + 1;
	}

	max_nss = min(max_nss, ar->num_tx_chains);
	arg->peer_nss = min(link_sta->rx_nss, max_nss);

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "mac he peer %pM nss %d mcs cnt %d\n",
		   arsta->addr, arg->peer_nss, arg->peer_he_mcs_count);
}

static void ath12k_peer_assoc_h_he_6ghz(struct ath12k *ar,
					struct ath12k_link_vif *arvif,
					struct ath12k_link_sta *arsta,
					struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	const struct ieee80211_sta_he_cap *he_cap;
	struct ieee80211_link_sta *link_sta;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	u8 ampdu_factor, mpdu_density;

	if (WARN_ON(ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)))
		return;

	band = def.chan->band;

	link_sta = ath12k_mac_get_link_sta(arsta);
	if (!link_sta) {
		ath12k_warn(ar->ab, "unable to access link sta in peer assoc he 6ghz for sta %pM link %u\n",
			    sta->addr, arsta->link_id);
		return;
	}

	he_cap = &link_sta->he_cap;

	if (!arg->he_flag || band != NL80211_BAND_6GHZ || !link_sta->he_6ghz_capa.capa)
		return;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_40)
		arg->bw_40 = true;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_80)
		arg->bw_80 = true;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_160)
		arg->bw_160 = true;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_320)
		arg->bw_320 = true;

	arg->peer_he_caps_6ghz = le16_to_cpu(link_sta->he_6ghz_capa.capa);

	mpdu_density = u32_get_bits(arg->peer_he_caps_6ghz,
				    IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START);
	arg->peer_mpdu_density = ath12k_parse_mpdudensity(mpdu_density);

	/* From IEEE Std 802.11ax-2021 - Section 10.12.2: An HE STA shall be capable of
	 * receiving A-MPDU where the A-MPDU pre-EOF padding length is up to the value
	 * indicated by the Maximum A-MPDU Length Exponent Extension field in the HE
	 * Capabilities element and the Maximum A-MPDU Length Exponent field in HE 6 GHz
	 * Band Capabilities element in the 6 GHz band.
	 *
	 * Here, we are extracting the Max A-MPDU Exponent Extension from HE caps and
	 * factor is the Maximum A-MPDU Length Exponent from HE 6 GHZ Band capability.
	 */
	ampdu_factor = u8_get_bits(he_cap->he_cap_elem.mac_cap_info[3],
				   IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK) +
			u32_get_bits(arg->peer_he_caps_6ghz,
				     IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP);

	arg->peer_max_mpdu = (1u << (IEEE80211_HE_6GHZ_MAX_AMPDU_FACTOR +
				     ampdu_factor)) - 1;
}

static int ath12k_get_smps_from_capa(const struct ieee80211_sta_ht_cap *ht_cap,
				     const struct ieee80211_he_6ghz_capa *he_6ghz_capa,
				     int *smps)
{
	if (ht_cap->ht_supported)
		*smps = u16_get_bits(ht_cap->cap, IEEE80211_HT_CAP_SM_PS);
	else
		*smps = le16_get_bits(he_6ghz_capa->capa,
				      IEEE80211_HE_6GHZ_CAP_SM_PS);

	if (*smps >= ARRAY_SIZE(ath12k_smps_map))
		return -EINVAL;

	return 0;
}

static void ath12k_peer_assoc_h_smps(struct ath12k_link_sta *arsta,
				     struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	const struct ieee80211_he_6ghz_capa *he_6ghz_capa;
	struct ath12k_link_vif *arvif = arsta->arvif;
	const struct ieee80211_sta_ht_cap *ht_cap;
	struct ieee80211_link_sta *link_sta;
	struct ath12k *ar = arvif->ar;
	int smps;

	link_sta = ath12k_mac_get_link_sta(arsta);
	if (!link_sta) {
		ath12k_warn(ar->ab, "unable to access link sta in peer assoc he for sta %pM link %u\n",
			    sta->addr, arsta->link_id);
		return;
	}

	he_6ghz_capa = &link_sta->he_6ghz_capa;
	ht_cap = &link_sta->ht_cap;

	if (!ht_cap->ht_supported && !he_6ghz_capa->capa)
		return;

	if (ath12k_get_smps_from_capa(ht_cap, he_6ghz_capa, &smps))
		return;

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

static void ath12k_peer_assoc_h_qos(struct ath12k *ar,
				    struct ath12k_link_vif *arvif,
				    struct ath12k_link_sta *arsta,
				    struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);

	switch (arvif->ahvif->vdev_type) {
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

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac peer %pM qos %d\n",
		   arsta->addr, arg->qos_flag);
}

static int ath12k_peer_assoc_qos_ap(struct ath12k *ar,
				    struct ath12k_link_vif *arvif,
				    struct ath12k_link_sta *arsta)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	struct ath12k_wmi_ap_ps_arg arg;
	u32 max_sp;
	u32 uapsd;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	arg.vdev_id = arvif->vdev_id;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac uapsd_queues 0x%x max_sp %d\n",
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

	arg.param = WMI_AP_PS_PEER_PARAM_UAPSD;
	arg.value = uapsd;
	ret = ath12k_wmi_send_set_ap_ps_param_cmd(ar, arsta->addr, &arg);
	if (ret)
		goto err;

	arg.param = WMI_AP_PS_PEER_PARAM_MAX_SP;
	arg.value = max_sp;
	ret = ath12k_wmi_send_set_ap_ps_param_cmd(ar, arsta->addr, &arg);
	if (ret)
		goto err;

	/* TODO: revisit during testing */
	arg.param = WMI_AP_PS_PEER_PARAM_SIFS_RESP_FRMTYPE;
	arg.value = DISABLE_SIFS_RESPONSE_TRIGGER;
	ret = ath12k_wmi_send_set_ap_ps_param_cmd(ar, arsta->addr, &arg);
	if (ret)
		goto err;

	arg.param = WMI_AP_PS_PEER_PARAM_SIFS_RESP_UAPSD;
	arg.value = DISABLE_SIFS_RESPONSE_TRIGGER;
	ret = ath12k_wmi_send_set_ap_ps_param_cmd(ar, arsta->addr, &arg);
	if (ret)
		goto err;

	return 0;

err:
	ath12k_warn(ar->ab, "failed to set ap ps peer param %d for vdev %i: %d\n",
		    arg.param, arvif->vdev_id, ret);
	return ret;
}

static bool ath12k_mac_sta_has_ofdm_only(struct ieee80211_link_sta *sta)
{
	return sta->supp_rates[NL80211_BAND_2GHZ] >>
	       ATH12K_MAC_FIRST_OFDM_RATE_IDX;
}

static enum wmi_phy_mode ath12k_mac_get_phymode_vht(struct ath12k *ar,
						    struct ieee80211_link_sta *link_sta)
{
	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_160) {
		switch (link_sta->vht_cap.cap &
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

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_80)
		return MODE_11AC_VHT80;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_40)
		return MODE_11AC_VHT40;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_20)
		return MODE_11AC_VHT20;

	return MODE_UNKNOWN;
}

static enum wmi_phy_mode ath12k_mac_get_phymode_he(struct ath12k *ar,
						   struct ieee80211_link_sta *link_sta)
{
	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_160) {
		if (link_sta->he_cap.he_cap_elem.phy_cap_info[0] &
		     IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G)
			return MODE_11AX_HE160;
		else if (link_sta->he_cap.he_cap_elem.phy_cap_info[0] &
		     IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
			return MODE_11AX_HE80_80;
		/* not sure if this is a valid case? */
		return MODE_11AX_HE160;
	}

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_80)
		return MODE_11AX_HE80;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_40)
		return MODE_11AX_HE40;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_20)
		return MODE_11AX_HE20;

	return MODE_UNKNOWN;
}

static enum wmi_phy_mode ath12k_mac_get_phymode_eht(struct ath12k *ar,
						    struct ieee80211_link_sta *link_sta)
{
	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_320)
		if (link_sta->eht_cap.eht_cap_elem.phy_cap_info[0] &
		    IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ)
			return MODE_11BE_EHT320;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_160) {
		if (link_sta->he_cap.he_cap_elem.phy_cap_info[0] &
		    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G)
			return MODE_11BE_EHT160;

		if (link_sta->he_cap.he_cap_elem.phy_cap_info[0] &
			 IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
			return MODE_11BE_EHT80_80;

		ath12k_warn(ar->ab, "invalid EHT PHY capability info for 160 Mhz: %d\n",
			    link_sta->he_cap.he_cap_elem.phy_cap_info[0]);

		return MODE_11BE_EHT160;
	}

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_80)
		return MODE_11BE_EHT80;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_40)
		return MODE_11BE_EHT40;

	if (link_sta->bandwidth == IEEE80211_STA_RX_BW_20)
		return MODE_11BE_EHT20;

	return MODE_UNKNOWN;
}

static void ath12k_peer_assoc_h_phymode(struct ath12k *ar,
					struct ath12k_link_vif *arvif,
					struct ath12k_link_sta *arsta,
					struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_link_sta *link_sta;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	const u8 *ht_mcs_mask;
	const u16 *vht_mcs_mask;
	const u16 *he_mcs_mask;
	enum wmi_phy_mode phymode = MODE_UNKNOWN;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);

	if (WARN_ON(ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)))
		return;

	band = def.chan->band;
	ht_mcs_mask = arvif->bitrate_mask.control[band].ht_mcs;
	vht_mcs_mask = arvif->bitrate_mask.control[band].vht_mcs;
	he_mcs_mask = arvif->bitrate_mask.control[band].he_mcs;

	link_sta = ath12k_mac_get_link_sta(arsta);
	if (!link_sta) {
		ath12k_warn(ar->ab, "unable to access link sta in peer assoc he for sta %pM link %u\n",
			    sta->addr, arsta->link_id);
		return;
	}

	switch (band) {
	case NL80211_BAND_2GHZ:
		if (link_sta->eht_cap.has_eht) {
			if (link_sta->bandwidth == IEEE80211_STA_RX_BW_40)
				phymode = MODE_11BE_EHT40_2G;
			else
				phymode = MODE_11BE_EHT20_2G;
		} else if (link_sta->he_cap.has_he &&
			   !ath12k_peer_assoc_h_he_masked(he_mcs_mask)) {
			if (link_sta->bandwidth == IEEE80211_STA_RX_BW_80)
				phymode = MODE_11AX_HE80_2G;
			else if (link_sta->bandwidth == IEEE80211_STA_RX_BW_40)
				phymode = MODE_11AX_HE40_2G;
			else
				phymode = MODE_11AX_HE20_2G;
		} else if (link_sta->vht_cap.vht_supported &&
		    !ath12k_peer_assoc_h_vht_masked(vht_mcs_mask)) {
			if (link_sta->bandwidth == IEEE80211_STA_RX_BW_40)
				phymode = MODE_11AC_VHT40;
			else
				phymode = MODE_11AC_VHT20;
		} else if (link_sta->ht_cap.ht_supported &&
			   !ath12k_peer_assoc_h_ht_masked(ht_mcs_mask)) {
			if (link_sta->bandwidth == IEEE80211_STA_RX_BW_40)
				phymode = MODE_11NG_HT40;
			else
				phymode = MODE_11NG_HT20;
		} else if (ath12k_mac_sta_has_ofdm_only(link_sta)) {
			phymode = MODE_11G;
		} else {
			phymode = MODE_11B;
		}
		break;
	case NL80211_BAND_5GHZ:
	case NL80211_BAND_6GHZ:
		/* Check EHT first */
		if (link_sta->eht_cap.has_eht) {
			phymode = ath12k_mac_get_phymode_eht(ar, link_sta);
		} else if (link_sta->he_cap.has_he &&
			   !ath12k_peer_assoc_h_he_masked(he_mcs_mask)) {
			phymode = ath12k_mac_get_phymode_he(ar, link_sta);
		} else if (link_sta->vht_cap.vht_supported &&
		    !ath12k_peer_assoc_h_vht_masked(vht_mcs_mask)) {
			phymode = ath12k_mac_get_phymode_vht(ar, link_sta);
		} else if (link_sta->ht_cap.ht_supported &&
			   !ath12k_peer_assoc_h_ht_masked(ht_mcs_mask)) {
			if (link_sta->bandwidth >= IEEE80211_STA_RX_BW_40)
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

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac peer %pM phymode %s\n",
		   arsta->addr, ath12k_mac_phymode_str(phymode));

	arg->peer_phymode = phymode;
	WARN_ON(phymode == MODE_UNKNOWN);
}

static void ath12k_mac_set_eht_mcs(u8 rx_tx_mcs7, u8 rx_tx_mcs9,
				   u8 rx_tx_mcs11, u8 rx_tx_mcs13,
				   u32 *rx_mcs, u32 *tx_mcs)
{
	*rx_mcs = 0;
	u32p_replace_bits(rx_mcs,
			  u8_get_bits(rx_tx_mcs7, IEEE80211_EHT_MCS_NSS_RX),
			  WMI_EHT_MCS_NSS_0_7);
	u32p_replace_bits(rx_mcs,
			  u8_get_bits(rx_tx_mcs9, IEEE80211_EHT_MCS_NSS_RX),
			  WMI_EHT_MCS_NSS_8_9);
	u32p_replace_bits(rx_mcs,
			  u8_get_bits(rx_tx_mcs11, IEEE80211_EHT_MCS_NSS_RX),
			  WMI_EHT_MCS_NSS_10_11);
	u32p_replace_bits(rx_mcs,
			  u8_get_bits(rx_tx_mcs13, IEEE80211_EHT_MCS_NSS_RX),
			  WMI_EHT_MCS_NSS_12_13);

	*tx_mcs = 0;
	u32p_replace_bits(tx_mcs,
			  u8_get_bits(rx_tx_mcs7, IEEE80211_EHT_MCS_NSS_TX),
			  WMI_EHT_MCS_NSS_0_7);
	u32p_replace_bits(tx_mcs,
			  u8_get_bits(rx_tx_mcs9, IEEE80211_EHT_MCS_NSS_TX),
			  WMI_EHT_MCS_NSS_8_9);
	u32p_replace_bits(tx_mcs,
			  u8_get_bits(rx_tx_mcs11, IEEE80211_EHT_MCS_NSS_TX),
			  WMI_EHT_MCS_NSS_10_11);
	u32p_replace_bits(tx_mcs,
			  u8_get_bits(rx_tx_mcs13, IEEE80211_EHT_MCS_NSS_TX),
			  WMI_EHT_MCS_NSS_12_13);
}

static void ath12k_mac_set_eht_ppe_threshold(const u8 *ppe_thres,
					     struct ath12k_wmi_ppe_threshold_arg *ppet)
{
	u32 bit_pos = IEEE80211_EHT_PPE_THRES_INFO_HEADER_SIZE, val;
	u8 nss, ru, i;
	u8 ppet_bit_len_per_ru = IEEE80211_EHT_PPE_THRES_INFO_PPET_SIZE * 2;

	ppet->numss_m1 = u8_get_bits(ppe_thres[0], IEEE80211_EHT_PPE_THRES_NSS_MASK);
	ppet->ru_bit_mask = u16_get_bits(get_unaligned_le16(ppe_thres),
					 IEEE80211_EHT_PPE_THRES_RU_INDEX_BITMASK_MASK);

	for (nss = 0; nss <= ppet->numss_m1; nss++) {
		for (ru = 0;
		     ru < hweight16(IEEE80211_EHT_PPE_THRES_RU_INDEX_BITMASK_MASK);
		     ru++) {
			if ((ppet->ru_bit_mask & BIT(ru)) == 0)
				continue;

			val = 0;
			for (i = 0; i < ppet_bit_len_per_ru; i++) {
				val |= (((ppe_thres[bit_pos / 8] >>
					  (bit_pos % 8)) & 0x1) << i);
				bit_pos++;
			}
			ppet->ppet16_ppet8_ru3_ru0[nss] |=
					(val << (ru * ppet_bit_len_per_ru));
		}
	}
}

static void ath12k_peer_assoc_h_eht(struct ath12k *ar,
				    struct ath12k_link_vif *arvif,
				    struct ath12k_link_sta *arsta,
				    struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	const struct ieee80211_eht_mcs_nss_supp_20mhz_only *bw_20;
	const struct ieee80211_eht_mcs_nss_supp_bw *bw;
	const struct ieee80211_sta_eht_cap *eht_cap;
	const struct ieee80211_sta_he_cap *he_cap;
	struct ieee80211_link_sta *link_sta;
	struct ieee80211_bss_conf *link_conf;
	u32 *rx_mcs, *tx_mcs;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	link_sta = ath12k_mac_get_link_sta(arsta);
	if (!link_sta) {
		ath12k_warn(ar->ab, "unable to access link sta in peer assoc eht for sta %pM link %u\n",
			    sta->addr, arsta->link_id);
		return;
	}

	link_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!link_conf) {
		ath12k_warn(ar->ab, "unable to access link_conf in peer assoc eht set\n");
		return;
	}

	eht_cap = &link_sta->eht_cap;
	he_cap = &link_sta->he_cap;
	if (!he_cap->has_he || !eht_cap->has_eht)
		return;

	arg->eht_flag = true;

	if ((eht_cap->eht_cap_elem.phy_cap_info[5] &
	     IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT) &&
	    eht_cap->eht_ppe_thres[0] != 0)
		ath12k_mac_set_eht_ppe_threshold(eht_cap->eht_ppe_thres,
						 &arg->peer_eht_ppet);

	memcpy(arg->peer_eht_cap_mac, eht_cap->eht_cap_elem.mac_cap_info,
	       sizeof(eht_cap->eht_cap_elem.mac_cap_info));
	memcpy(arg->peer_eht_cap_phy, eht_cap->eht_cap_elem.phy_cap_info,
	       sizeof(eht_cap->eht_cap_elem.phy_cap_info));

	rx_mcs = arg->peer_eht_rx_mcs_set;
	tx_mcs = arg->peer_eht_tx_mcs_set;

	switch (link_sta->bandwidth) {
	case IEEE80211_STA_RX_BW_320:
		bw = &eht_cap->eht_mcs_nss_supp.bw._320;
		ath12k_mac_set_eht_mcs(bw->rx_tx_mcs9_max_nss,
				       bw->rx_tx_mcs9_max_nss,
				       bw->rx_tx_mcs11_max_nss,
				       bw->rx_tx_mcs13_max_nss,
				       &rx_mcs[WMI_EHTCAP_TXRX_MCS_NSS_IDX_320],
				       &tx_mcs[WMI_EHTCAP_TXRX_MCS_NSS_IDX_320]);
		arg->peer_eht_mcs_count++;
		fallthrough;
	case IEEE80211_STA_RX_BW_160:
		bw = &eht_cap->eht_mcs_nss_supp.bw._160;
		ath12k_mac_set_eht_mcs(bw->rx_tx_mcs9_max_nss,
				       bw->rx_tx_mcs9_max_nss,
				       bw->rx_tx_mcs11_max_nss,
				       bw->rx_tx_mcs13_max_nss,
				       &rx_mcs[WMI_EHTCAP_TXRX_MCS_NSS_IDX_160],
				       &tx_mcs[WMI_EHTCAP_TXRX_MCS_NSS_IDX_160]);
		arg->peer_eht_mcs_count++;
		fallthrough;
	default:
		if ((he_cap->he_cap_elem.phy_cap_info[0] &
		     (IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
		      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
		      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
		      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)) == 0) {
			bw_20 = &eht_cap->eht_mcs_nss_supp.only_20mhz;

			ath12k_mac_set_eht_mcs(bw_20->rx_tx_mcs7_max_nss,
					       bw_20->rx_tx_mcs9_max_nss,
					       bw_20->rx_tx_mcs11_max_nss,
					       bw_20->rx_tx_mcs13_max_nss,
					       &rx_mcs[WMI_EHTCAP_TXRX_MCS_NSS_IDX_80],
					       &tx_mcs[WMI_EHTCAP_TXRX_MCS_NSS_IDX_80]);
		} else {
			bw = &eht_cap->eht_mcs_nss_supp.bw._80;
			ath12k_mac_set_eht_mcs(bw->rx_tx_mcs9_max_nss,
					       bw->rx_tx_mcs9_max_nss,
					       bw->rx_tx_mcs11_max_nss,
					       bw->rx_tx_mcs13_max_nss,
					       &rx_mcs[WMI_EHTCAP_TXRX_MCS_NSS_IDX_80],
					       &tx_mcs[WMI_EHTCAP_TXRX_MCS_NSS_IDX_80]);
		}

		arg->peer_eht_mcs_count++;
		break;
	}

	arg->punct_bitmap = ~arvif->punct_bitmap;
	arg->eht_disable_mcs15 = link_conf->eht_disable_mcs15;
}

static void ath12k_peer_assoc_h_mlo(struct ath12k_link_sta *arsta,
				    struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	struct peer_assoc_mlo_params *ml = &arg->ml;
	struct ath12k_sta *ahsta = arsta->ahsta;
	struct ath12k_link_sta *arsta_p;
	struct ath12k_link_vif *arvif;
	unsigned long links;
	u8 link_id;
	int i;

	if (!sta->mlo || ahsta->ml_peer_id == ATH12K_MLO_PEER_ID_INVALID)
		return;

	ml->enabled = true;
	ml->assoc_link = arsta->is_assoc_link;

	/* For now considering the primary umac based on assoc link */
	ml->primary_umac = arsta->is_assoc_link;
	ml->peer_id_valid = true;
	ml->logical_link_idx_valid = true;

	ether_addr_copy(ml->mld_addr, sta->addr);
	ml->logical_link_idx = arsta->link_idx;
	ml->ml_peer_id = ahsta->ml_peer_id;
	ml->ieee_link_id = arsta->link_id;
	ml->num_partner_links = 0;
	ml->eml_cap = sta->eml_cap;
	links = ahsta->links_map;

	rcu_read_lock();

	i = 0;

	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		if (i >= ATH12K_WMI_MLO_MAX_LINKS)
			break;

		arsta_p = rcu_dereference(ahsta->link[link_id]);
		arvif = rcu_dereference(ahsta->ahvif->link[link_id]);

		if (arsta_p == arsta)
			continue;

		if (!arvif->is_started)
			continue;

		ml->partner_info[i].vdev_id = arvif->vdev_id;
		ml->partner_info[i].hw_link_id = arvif->ar->pdev->hw_link_id;
		ml->partner_info[i].assoc_link = arsta_p->is_assoc_link;
		ml->partner_info[i].primary_umac = arsta_p->is_assoc_link;
		ml->partner_info[i].logical_link_idx_valid = true;
		ml->partner_info[i].logical_link_idx = arsta_p->link_idx;
		ml->num_partner_links++;

		i++;
	}

	rcu_read_unlock();
}

static void ath12k_peer_assoc_prepare(struct ath12k *ar,
				      struct ath12k_link_vif *arvif,
				      struct ath12k_link_sta *arsta,
				      struct ath12k_wmi_peer_assoc_arg *arg,
				      bool reassoc)
{
	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	memset(arg, 0, sizeof(*arg));

	reinit_completion(&ar->peer_assoc_done);

	arg->peer_new_assoc = !reassoc;
	ath12k_peer_assoc_h_basic(ar, arvif, arsta, arg);
	ath12k_peer_assoc_h_crypto(ar, arvif, arsta, arg);
	ath12k_peer_assoc_h_rates(ar, arvif, arsta, arg);
	ath12k_peer_assoc_h_ht(ar, arvif, arsta, arg);
	ath12k_peer_assoc_h_vht(ar, arvif, arsta, arg);
	ath12k_peer_assoc_h_he(ar, arvif, arsta, arg);
	ath12k_peer_assoc_h_he_6ghz(ar, arvif, arsta, arg);
	ath12k_peer_assoc_h_eht(ar, arvif, arsta, arg);
	ath12k_peer_assoc_h_qos(ar, arvif, arsta, arg);
	ath12k_peer_assoc_h_phymode(ar, arvif, arsta, arg);
	ath12k_peer_assoc_h_smps(arsta, arg);
	ath12k_peer_assoc_h_mlo(arsta, arg);

	arsta->peer_nss = arg->peer_nss;
	/* TODO: amsdu_disable req? */
}

static int ath12k_setup_peer_smps(struct ath12k *ar, struct ath12k_link_vif *arvif,
				  const u8 *addr,
				  const struct ieee80211_sta_ht_cap *ht_cap,
				  const struct ieee80211_he_6ghz_capa *he_6ghz_capa)
{
	int smps, ret = 0;

	if (!ht_cap->ht_supported && !he_6ghz_capa)
		return 0;

	ret = ath12k_get_smps_from_capa(ht_cap, he_6ghz_capa, &smps);
	if (ret < 0)
		return ret;

	return ath12k_wmi_set_peer_param(ar, addr, arvif->vdev_id,
					 WMI_PEER_MIMO_PS_STATE,
					 ath12k_smps_map[smps]);
}

static int ath12k_mac_set_he_txbf_conf(struct ath12k_link_vif *arvif)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k *ar = arvif->ar;
	u32 param = WMI_VDEV_PARAM_SET_HEMU_MODE;
	u32 value = 0;
	int ret;
	struct ieee80211_bss_conf *link_conf;

	link_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!link_conf) {
		ath12k_warn(ar->ab, "unable to access bss link conf in txbf conf\n");
		return -EINVAL;
	}

	if (!link_conf->he_support)
		return 0;

	if (link_conf->he_su_beamformer) {
		value |= u32_encode_bits(HE_SU_BFER_ENABLE, HE_MODE_SU_TX_BFER);
		if (link_conf->he_mu_beamformer &&
		    ahvif->vdev_type == WMI_VDEV_TYPE_AP)
			value |= u32_encode_bits(HE_MU_BFER_ENABLE, HE_MODE_MU_TX_BFER);
	}

	if (ahvif->vif->type != NL80211_IFTYPE_MESH_POINT) {
		value |= u32_encode_bits(HE_DL_MUOFDMA_ENABLE, HE_MODE_DL_OFDMA) |
			 u32_encode_bits(HE_UL_MUOFDMA_ENABLE, HE_MODE_UL_OFDMA);

		if (link_conf->he_full_ul_mumimo)
			value |= u32_encode_bits(HE_UL_MUMIMO_ENABLE, HE_MODE_UL_MUMIMO);

		if (link_conf->he_su_beamformee)
			value |= u32_encode_bits(HE_SU_BFEE_ENABLE, HE_MODE_SU_TX_BFEE);
	}

	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, param, value);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set vdev %d HE MU mode: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	param = WMI_VDEV_PARAM_SET_HE_SOUNDING_MODE;
	value =	u32_encode_bits(HE_VHT_SOUNDING_MODE_ENABLE, HE_VHT_SOUNDING_MODE) |
		u32_encode_bits(HE_TRIG_NONTRIG_SOUNDING_MODE_ENABLE,
				HE_TRIG_NONTRIG_SOUNDING_MODE);
	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    param, value);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set vdev %d sounding mode: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	return 0;
}

static int ath12k_mac_vif_recalc_sta_he_txbf(struct ath12k *ar,
					     struct ath12k_link_vif *arvif,
					     struct ieee80211_sta_he_cap *he_cap,
					     int *hemode)
{
	struct ieee80211_vif *vif = arvif->ahvif->vif;
	struct ieee80211_he_cap_elem he_cap_elem = {};
	struct ieee80211_sta_he_cap *cap_band;
	struct cfg80211_chan_def def;
	u8 link_id = arvif->link_id;
	struct ieee80211_bss_conf *link_conf;

	link_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!link_conf) {
		ath12k_warn(ar->ab, "unable to access bss link conf in recalc txbf conf\n");
		return -EINVAL;
	}

	if (!link_conf->he_support)
		return 0;

	if (vif->type != NL80211_IFTYPE_STATION)
		return -EINVAL;

	if (WARN_ON(ath12k_mac_vif_link_chan(vif, link_id, &def)))
		return -EINVAL;

	if (def.chan->band == NL80211_BAND_2GHZ)
		cap_band = &ar->mac.iftype[NL80211_BAND_2GHZ][vif->type].he_cap;
	else
		cap_band = &ar->mac.iftype[NL80211_BAND_5GHZ][vif->type].he_cap;

	memcpy(&he_cap_elem, &cap_band->he_cap_elem, sizeof(he_cap_elem));

	*hemode = 0;
	if (HECAP_PHY_SUBFME_GET(he_cap_elem.phy_cap_info)) {
		if (HECAP_PHY_SUBFMR_GET(he_cap->he_cap_elem.phy_cap_info))
			*hemode |= u32_encode_bits(HE_SU_BFEE_ENABLE, HE_MODE_SU_TX_BFEE);
		if (HECAP_PHY_MUBFMR_GET(he_cap->he_cap_elem.phy_cap_info))
			*hemode |= u32_encode_bits(HE_MU_BFEE_ENABLE, HE_MODE_MU_TX_BFEE);
	}

	if (vif->type != NL80211_IFTYPE_MESH_POINT) {
		*hemode |= u32_encode_bits(HE_DL_MUOFDMA_ENABLE, HE_MODE_DL_OFDMA) |
			  u32_encode_bits(HE_UL_MUOFDMA_ENABLE, HE_MODE_UL_OFDMA);

		if (HECAP_PHY_ULMUMIMO_GET(he_cap_elem.phy_cap_info))
			if (HECAP_PHY_ULMUMIMO_GET(he_cap->he_cap_elem.phy_cap_info))
				*hemode |= u32_encode_bits(HE_UL_MUMIMO_ENABLE,
							  HE_MODE_UL_MUMIMO);

		if (u32_get_bits(*hemode, HE_MODE_MU_TX_BFEE))
			*hemode |= u32_encode_bits(HE_SU_BFEE_ENABLE, HE_MODE_SU_TX_BFEE);

		if (u32_get_bits(*hemode, HE_MODE_MU_TX_BFER))
			*hemode |= u32_encode_bits(HE_SU_BFER_ENABLE, HE_MODE_SU_TX_BFER);
	}

	return 0;
}

static int ath12k_mac_set_eht_txbf_conf(struct ath12k_link_vif *arvif)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k *ar = arvif->ar;
	u32 param = WMI_VDEV_PARAM_SET_EHT_MU_MODE;
	u32 value = 0;
	int ret;
	struct ieee80211_bss_conf *link_conf;

	link_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!link_conf) {
		ath12k_warn(ar->ab, "unable to access bss link conf in eht txbf conf\n");
		return -ENOENT;
	}

	if (!link_conf->eht_support)
		return 0;

	if (link_conf->eht_su_beamformer) {
		value |= u32_encode_bits(EHT_SU_BFER_ENABLE, EHT_MODE_SU_TX_BFER);
		if (link_conf->eht_mu_beamformer &&
		    ahvif->vdev_type == WMI_VDEV_TYPE_AP)
			value |= u32_encode_bits(EHT_MU_BFER_ENABLE,
						 EHT_MODE_MU_TX_BFER) |
				 u32_encode_bits(EHT_DL_MUOFDMA_ENABLE,
						 EHT_MODE_DL_OFDMA_MUMIMO) |
				 u32_encode_bits(EHT_UL_MUOFDMA_ENABLE,
						 EHT_MODE_UL_OFDMA_MUMIMO);
	}

	if (ahvif->vif->type != NL80211_IFTYPE_MESH_POINT) {
		value |= u32_encode_bits(EHT_DL_MUOFDMA_ENABLE, EHT_MODE_DL_OFDMA) |
			 u32_encode_bits(EHT_UL_MUOFDMA_ENABLE, EHT_MODE_UL_OFDMA);

		if (link_conf->eht_80mhz_full_bw_ul_mumimo)
			value |= u32_encode_bits(EHT_UL_MUMIMO_ENABLE, EHT_MODE_MUMIMO);

		if (link_conf->eht_su_beamformee)
			value |= u32_encode_bits(EHT_SU_BFEE_ENABLE,
						 EHT_MODE_SU_TX_BFEE);
	}

	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, param, value);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set vdev %d EHT MU mode: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	return 0;
}

static u32 ath12k_mac_ieee80211_sta_bw_to_wmi(struct ath12k *ar,
					      struct ieee80211_link_sta *link_sta)
{
	u32 bw;

	switch (link_sta->bandwidth) {
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
	case IEEE80211_STA_RX_BW_320:
		bw = WMI_PEER_CHWIDTH_320MHZ;
		break;
	default:
		ath12k_warn(ar->ab, "Invalid bandwidth %d for link station %pM\n",
			    link_sta->bandwidth, link_sta->addr);
		bw = WMI_PEER_CHWIDTH_20MHZ;
		break;
	}

	return bw;
}

static void ath12k_bss_assoc(struct ath12k *ar,
			     struct ath12k_link_vif *arvif,
			     struct ieee80211_bss_conf *bss_conf)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(ahvif);
	struct ath12k_wmi_vdev_up_params params = {};
	struct ieee80211_link_sta *link_sta;
	u8 link_id = bss_conf->link_id;
	struct ath12k_link_sta *arsta;
	struct ieee80211_sta *ap_sta;
	struct ath12k_sta *ahsta;
	struct ath12k_peer *peer;
	bool is_auth = false;
	u32 hemode = 0;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	struct ath12k_wmi_peer_assoc_arg *peer_arg __free(kfree) =
					kzalloc(sizeof(*peer_arg), GFP_KERNEL);
	if (!peer_arg)
		return;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "mac vdev %i link id %u assoc bssid %pM aid %d\n",
		   arvif->vdev_id, link_id, arvif->bssid, ahvif->aid);

	rcu_read_lock();

	/* During ML connection, cfg.ap_addr has the MLD address. For
	 * non-ML connection, it has the BSSID.
	 */
	ap_sta = ieee80211_find_sta(vif, vif->cfg.ap_addr);
	if (!ap_sta) {
		ath12k_warn(ar->ab, "failed to find station entry for bss %pM vdev %i\n",
			    vif->cfg.ap_addr, arvif->vdev_id);
		rcu_read_unlock();
		return;
	}

	ahsta = ath12k_sta_to_ahsta(ap_sta);

	arsta = wiphy_dereference(ath12k_ar_to_hw(ar)->wiphy,
				  ahsta->link[link_id]);
	if (WARN_ON(!arsta)) {
		rcu_read_unlock();
		return;
	}

	link_sta = ath12k_mac_get_link_sta(arsta);
	if (WARN_ON(!link_sta)) {
		rcu_read_unlock();
		return;
	}

	ath12k_peer_assoc_prepare(ar, arvif, arsta, peer_arg, false);

	/* link_sta->he_cap must be protected by rcu_read_lock */
	ret = ath12k_mac_vif_recalc_sta_he_txbf(ar, arvif, &link_sta->he_cap, &hemode);
	if (ret) {
		ath12k_warn(ar->ab, "failed to recalc he txbf for vdev %i on bss %pM: %d\n",
			    arvif->vdev_id, bss_conf->bssid, ret);
		rcu_read_unlock();
		return;
	}

	rcu_read_unlock();

	/* keep this before ath12k_wmi_send_peer_assoc_cmd() */
	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_SET_HEMU_MODE, hemode);
	if (ret) {
		ath12k_warn(ar->ab, "failed to submit vdev param txbf 0x%x: %d\n",
			    hemode, ret);
		return;
	}

	peer_arg->is_assoc = true;
	ret = ath12k_wmi_send_peer_assoc_cmd(ar, peer_arg);
	if (ret) {
		ath12k_warn(ar->ab, "failed to run peer assoc for %pM vdev %i: %d\n",
			    bss_conf->bssid, arvif->vdev_id, ret);
		return;
	}

	if (!wait_for_completion_timeout(&ar->peer_assoc_done, 1 * HZ)) {
		ath12k_warn(ar->ab, "failed to get peer assoc conf event for %pM vdev %i\n",
			    bss_conf->bssid, arvif->vdev_id);
		return;
	}

	ret = ath12k_setup_peer_smps(ar, arvif, bss_conf->bssid,
				     &link_sta->ht_cap, &link_sta->he_6ghz_capa);
	if (ret) {
		ath12k_warn(ar->ab, "failed to setup peer SMPS for vdev %d: %d\n",
			    arvif->vdev_id, ret);
		return;
	}

	WARN_ON(arvif->is_up);

	ahvif->aid = vif->cfg.aid;
	ether_addr_copy(arvif->bssid, bss_conf->bssid);

	params.vdev_id = arvif->vdev_id;
	params.aid = ahvif->aid;
	params.bssid = arvif->bssid;
	params.tx_bssid = ath12k_mac_get_tx_bssid(arvif);
	if (params.tx_bssid) {
		params.nontx_profile_idx = bss_conf->bssid_index;
		params.nontx_profile_cnt = 1 << bss_conf->bssid_indicator;
	}
	ret = ath12k_wmi_vdev_up(ar, &params);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set vdev %d up: %d\n",
			    arvif->vdev_id, ret);
		return;
	}

	arvif->is_up = true;
	arvif->rekey_data.enable_offload = false;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "mac vdev %d up (associated) bssid %pM aid %d\n",
		   arvif->vdev_id, bss_conf->bssid, vif->cfg.aid);

	spin_lock_bh(&ar->ab->base_lock);

	peer = ath12k_peer_find(ar->ab, arvif->vdev_id, arvif->bssid);
	if (peer && peer->is_authorized)
		is_auth = true;

	spin_unlock_bh(&ar->ab->base_lock);

	/* Authorize BSS Peer */
	if (is_auth) {
		ret = ath12k_wmi_set_peer_param(ar, arvif->bssid,
						arvif->vdev_id,
						WMI_PEER_AUTHORIZE,
						1);
		if (ret)
			ath12k_warn(ar->ab, "Unable to authorize BSS peer: %d\n", ret);
	}

	ret = ath12k_wmi_send_obss_spr_cmd(ar, arvif->vdev_id,
					   &bss_conf->he_obss_pd);
	if (ret)
		ath12k_warn(ar->ab, "failed to set vdev %i OBSS PD parameters: %d\n",
			    arvif->vdev_id, ret);

	if (test_bit(WMI_TLV_SERVICE_11D_OFFLOAD, ar->ab->wmi_ab.svc_map) &&
	    ahvif->vdev_type == WMI_VDEV_TYPE_STA &&
	    ahvif->vdev_subtype == WMI_VDEV_SUBTYPE_NONE)
		ath12k_mac_11d_scan_stop_all(ar->ab);
}

static void ath12k_bss_disassoc(struct ath12k *ar,
				struct ath12k_link_vif *arvif)
{
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac vdev %i disassoc bssid %pM\n",
		   arvif->vdev_id, arvif->bssid);

	ret = ath12k_wmi_vdev_down(ar, arvif->vdev_id);
	if (ret)
		ath12k_warn(ar->ab, "failed to down vdev %i: %d\n",
			    arvif->vdev_id, ret);

	arvif->is_up = false;

	memset(&arvif->rekey_data, 0, sizeof(arvif->rekey_data));

	cancel_delayed_work(&arvif->connection_loss_work);
}

static u32 ath12k_mac_get_rate_hw_value(int bitrate)
{
	u32 preamble;
	u16 hw_value;
	int rate;
	size_t i;

	if (ath12k_mac_bitrate_is_cck(bitrate))
		preamble = WMI_RATE_PREAMBLE_CCK;
	else
		preamble = WMI_RATE_PREAMBLE_OFDM;

	for (i = 0; i < ARRAY_SIZE(ath12k_legacy_rates); i++) {
		if (ath12k_legacy_rates[i].bitrate != bitrate)
			continue;

		hw_value = ath12k_legacy_rates[i].hw_value;
		rate = ATH12K_HW_RATE_CODE(hw_value, 0, preamble);

		return rate;
	}

	return -EINVAL;
}

static void ath12k_recalculate_mgmt_rate(struct ath12k *ar,
					 struct ath12k_link_vif *arvif,
					 struct cfg80211_chan_def *def)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_hw *hw = ath12k_ar_to_hw(ar);
	const struct ieee80211_supported_band *sband;
	struct ieee80211_bss_conf *bss_conf;
	u8 basic_rate_idx;
	int hw_rate_code;
	u32 vdev_param;
	u16 bitrate;
	int ret;

	lockdep_assert_wiphy(hw->wiphy);

	bss_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!bss_conf) {
		ath12k_warn(ar->ab, "unable to access bss link conf in mgmt rate calc for vif %pM link %u\n",
			    vif->addr, arvif->link_id);
		return;
	}

	sband = hw->wiphy->bands[def->chan->band];
	if (bss_conf->basic_rates)
		basic_rate_idx = __ffs(bss_conf->basic_rates);
	else
		basic_rate_idx = 0;
	bitrate = sband->bitrates[basic_rate_idx].bitrate;

	hw_rate_code = ath12k_mac_get_rate_hw_value(bitrate);
	if (hw_rate_code < 0) {
		ath12k_warn(ar->ab, "bitrate not supported %d\n", bitrate);
		return;
	}

	vdev_param = WMI_VDEV_PARAM_MGMT_RATE;
	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, vdev_param,
					    hw_rate_code);
	if (ret)
		ath12k_warn(ar->ab, "failed to set mgmt tx rate %d\n", ret);

	vdev_param = WMI_VDEV_PARAM_BEACON_RATE;
	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id, vdev_param,
					    hw_rate_code);
	if (ret)
		ath12k_warn(ar->ab, "failed to set beacon tx rate %d\n", ret);
}

static void ath12k_mac_init_arvif(struct ath12k_vif *ahvif,
				  struct ath12k_link_vif *arvif, int link_id)
{
	struct ath12k_hw *ah = ahvif->ah;
	u8 _link_id;
	int i;

	lockdep_assert_wiphy(ah->hw->wiphy);

	if (WARN_ON(!arvif))
		return;

	if (WARN_ON(link_id >= ATH12K_NUM_MAX_LINKS))
		return;

	if (link_id < 0)
		_link_id = 0;
	else
		_link_id = link_id;

	arvif->ahvif = ahvif;
	arvif->link_id = _link_id;

	/* Protects the datapath stats update on a per link basis */
	spin_lock_init(&arvif->link_stats_lock);

	INIT_LIST_HEAD(&arvif->list);
	INIT_DELAYED_WORK(&arvif->connection_loss_work,
			  ath12k_mac_vif_sta_connection_loss_work);

	for (i = 0; i < ARRAY_SIZE(arvif->bitrate_mask.control); i++) {
		arvif->bitrate_mask.control[i].legacy = 0xffffffff;
		arvif->bitrate_mask.control[i].gi = NL80211_TXRATE_DEFAULT_GI;
		memset(arvif->bitrate_mask.control[i].ht_mcs, 0xff,
		       sizeof(arvif->bitrate_mask.control[i].ht_mcs));
		memset(arvif->bitrate_mask.control[i].vht_mcs, 0xff,
		       sizeof(arvif->bitrate_mask.control[i].vht_mcs));
		memset(arvif->bitrate_mask.control[i].he_mcs, 0xff,
		       sizeof(arvif->bitrate_mask.control[i].he_mcs));
	}

	/* Handle MLO related assignments */
	if (link_id >= 0) {
		rcu_assign_pointer(ahvif->link[arvif->link_id], arvif);
		ahvif->links_map |= BIT(_link_id);
	}

	ath12k_generic_dbg(ATH12K_DBG_MAC,
			   "mac init link arvif (link_id %d%s) for vif %pM. links_map 0x%x",
			   _link_id, (link_id < 0) ? " deflink" : "", ahvif->vif->addr,
			   ahvif->links_map);
}

static void ath12k_mac_remove_link_interface(struct ieee80211_hw *hw,
					     struct ath12k_link_vif *arvif)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k_hw *ah = hw->priv;
	struct ath12k *ar = arvif->ar;
	int ret;

	lockdep_assert_wiphy(ah->hw->wiphy);

	cancel_delayed_work_sync(&arvif->connection_loss_work);

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac remove link interface (vdev %d link id %d)",
		   arvif->vdev_id, arvif->link_id);

	if (test_bit(WMI_TLV_SERVICE_11D_OFFLOAD, ar->ab->wmi_ab.svc_map) &&
	    ahvif->vdev_type == WMI_VDEV_TYPE_STA &&
	    ahvif->vdev_subtype == WMI_VDEV_SUBTYPE_NONE)
		ath12k_mac_11d_scan_stop(ar);

	if (ahvif->vdev_type == WMI_VDEV_TYPE_AP) {
		ret = ath12k_peer_delete(ar, arvif->vdev_id, arvif->bssid);
		if (ret)
			ath12k_warn(ar->ab, "failed to submit AP self-peer removal on vdev %d link id %d: %d",
				    arvif->vdev_id, arvif->link_id, ret);
	}
	ath12k_mac_vdev_delete(ar, arvif);
}

static struct ath12k_link_vif *ath12k_mac_assign_link_vif(struct ath12k_hw *ah,
							  struct ieee80211_vif *vif,
							  u8 link_id)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif;

	lockdep_assert_wiphy(ah->hw->wiphy);

	arvif = wiphy_dereference(ah->hw->wiphy, ahvif->link[link_id]);
	if (arvif)
		return arvif;

	/* If this is the first link arvif being created for an ML VIF
	 * use the preallocated deflink memory except for scan arvifs
	 */
	if (!ahvif->links_map && link_id < ATH12K_FIRST_SCAN_LINK) {
		arvif = &ahvif->deflink;

		if (vif->type == NL80211_IFTYPE_STATION)
			arvif->is_sta_assoc_link = true;
	} else {
		arvif = kzalloc(sizeof(*arvif), GFP_KERNEL);
		if (!arvif)
			return NULL;
	}

	ath12k_mac_init_arvif(ahvif, arvif, link_id);

	return arvif;
}

static void ath12k_mac_unassign_link_vif(struct ath12k_link_vif *arvif)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k_hw *ah = ahvif->ah;

	lockdep_assert_wiphy(ah->hw->wiphy);

	rcu_assign_pointer(ahvif->link[arvif->link_id], NULL);
	synchronize_rcu();
	ahvif->links_map &= ~BIT(arvif->link_id);

	if (arvif != &ahvif->deflink)
		kfree(arvif);
	else
		memset(arvif, 0, sizeof(*arvif));
}

static int
ath12k_mac_op_change_vif_links(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       u16 old_links, u16 new_links,
			       struct ieee80211_bss_conf *ol[IEEE80211_MLD_MAX_NUM_LINKS])
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	unsigned long to_remove = old_links & ~new_links;
	unsigned long to_add = ~old_links & new_links;
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k_link_vif *arvif;
	u8 link_id;

	lockdep_assert_wiphy(hw->wiphy);

	ath12k_generic_dbg(ATH12K_DBG_MAC,
			   "mac vif link changed for MLD %pM old_links 0x%x new_links 0x%x\n",
			   vif->addr, old_links, new_links);

	for_each_set_bit(link_id, &to_add, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
		/* mac80211 wants to add link but driver already has the
		 * link. This should not happen ideally.
		 */
		if (WARN_ON(arvif))
			return -EINVAL;

		arvif = ath12k_mac_assign_link_vif(ah, vif, link_id);
		if (WARN_ON(!arvif))
			return -EINVAL;
	}

	for_each_set_bit(link_id, &to_remove, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
		if (WARN_ON(!arvif))
			return -EINVAL;

		if (!arvif->is_created)
			continue;

		if (WARN_ON(!arvif->ar))
			return -EINVAL;

		ath12k_mac_remove_link_interface(hw, arvif);
		ath12k_mac_unassign_link_vif(arvif);
	}

	return 0;
}

static int ath12k_mac_fils_discovery(struct ath12k_link_vif *arvif,
				     struct ieee80211_bss_conf *info)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ath12k *ar = arvif->ar;
	struct ieee80211_hw *hw = ath12k_ar_to_hw(ar);
	struct sk_buff *tmpl;
	int ret;
	u32 interval;
	bool unsol_bcast_probe_resp_enabled = false;

	if (info->fils_discovery.max_interval) {
		interval = info->fils_discovery.max_interval;

		tmpl = ieee80211_get_fils_discovery_tmpl(hw, vif);
		if (tmpl)
			ret = ath12k_wmi_fils_discovery_tmpl(ar, arvif->vdev_id,
							     tmpl);
	} else if (info->unsol_bcast_probe_resp_interval) {
		unsol_bcast_probe_resp_enabled = 1;
		interval = info->unsol_bcast_probe_resp_interval;

		tmpl = ieee80211_get_unsol_bcast_probe_resp_tmpl(hw, vif);
		if (tmpl)
			ret = ath12k_wmi_probe_resp_tmpl(ar, arvif->vdev_id,
							 tmpl);
	} else { /* Disable */
		return ath12k_wmi_fils_discovery(ar, arvif->vdev_id, 0, false);
	}

	if (!tmpl) {
		ath12k_warn(ar->ab,
			    "mac vdev %i failed to retrieve %s template\n",
			    arvif->vdev_id, (unsol_bcast_probe_resp_enabled ?
			    "unsolicited broadcast probe response" :
			    "FILS discovery"));
		return -EPERM;
	}
	kfree_skb(tmpl);

	if (!ret)
		ret = ath12k_wmi_fils_discovery(ar, arvif->vdev_id, interval,
						unsol_bcast_probe_resp_enabled);

	return ret;
}

static void ath12k_mac_op_vif_cfg_changed(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  u64 changed)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	unsigned long links = ahvif->links_map;
	struct ieee80211_bss_conf *info;
	struct ath12k_link_vif *arvif;
	struct ieee80211_sta *sta;
	struct ath12k_sta *ahsta;
	struct ath12k *ar;
	u8 link_id;

	lockdep_assert_wiphy(hw->wiphy);

	if (changed & BSS_CHANGED_SSID && vif->type == NL80211_IFTYPE_AP) {
		ahvif->u.ap.ssid_len = vif->cfg.ssid_len;
		if (vif->cfg.ssid_len)
			memcpy(ahvif->u.ap.ssid, vif->cfg.ssid, vif->cfg.ssid_len);
	}

	if (changed & BSS_CHANGED_ASSOC) {
		if (vif->cfg.assoc) {
			/* only in station mode we can get here, so it's safe
			 * to use ap_addr
			 */
			rcu_read_lock();
			sta = ieee80211_find_sta(vif, vif->cfg.ap_addr);
			if (!sta) {
				rcu_read_unlock();
				WARN_ONCE(1, "failed to find sta with addr %pM\n",
					  vif->cfg.ap_addr);
				return;
			}

			ahsta = ath12k_sta_to_ahsta(sta);
			arvif = wiphy_dereference(hw->wiphy,
						  ahvif->link[ahsta->assoc_link_id]);
			rcu_read_unlock();

			ar = arvif->ar;
			/* there is no reason for which an assoc link's
			 * bss info does not exist
			 */
			info = ath12k_mac_get_link_bss_conf(arvif);
			ath12k_bss_assoc(ar, arvif, info);

			/* exclude assoc link as it is done above */
			links &= ~BIT(ahsta->assoc_link_id);
		}

		for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
			arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
			if (!arvif || !arvif->ar)
				continue;

			ar = arvif->ar;

			if (vif->cfg.assoc) {
				info = ath12k_mac_get_link_bss_conf(arvif);
				if (!info)
					continue;

				ath12k_bss_assoc(ar, arvif, info);
			} else {
				ath12k_bss_disassoc(ar, arvif);
			}
		}
	}
}

static void ath12k_mac_vif_setup_ps(struct ath12k_link_vif *arvif)
{
	struct ath12k *ar = arvif->ar;
	struct ieee80211_vif *vif = arvif->ahvif->vif;
	struct ieee80211_conf *conf = &ath12k_ar_to_hw(ar)->conf;
	enum wmi_sta_powersave_param param;
	struct ieee80211_bss_conf *info;
	enum wmi_sta_ps_mode psmode;
	int ret;
	int timeout;
	bool enable_ps;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	enable_ps = arvif->ahvif->ps;
	if (enable_ps) {
		psmode = WMI_STA_PS_MODE_ENABLED;
		param = WMI_STA_PS_PARAM_INACTIVITY_TIME;

		timeout = conf->dynamic_ps_timeout;
		if (timeout == 0) {
			info = ath12k_mac_get_link_bss_conf(arvif);
			if (!info) {
				ath12k_warn(ar->ab, "unable to access bss link conf in setup ps for vif %pM link %u\n",
					    vif->addr, arvif->link_id);
				return;
			}

			/* firmware doesn't like 0 */
			timeout = ieee80211_tu_to_usec(info->beacon_int) / 1000;
		}

		ret = ath12k_wmi_set_sta_ps_param(ar, arvif->vdev_id, param,
						  timeout);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set inactivity time for vdev %d: %i\n",
				    arvif->vdev_id, ret);
			return;
		}
	} else {
		psmode = WMI_STA_PS_MODE_DISABLED;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac vdev %d psmode %s\n",
		   arvif->vdev_id, psmode ? "enable" : "disable");

	ret = ath12k_wmi_pdev_set_ps_mode(ar, arvif->vdev_id, psmode);
	if (ret)
		ath12k_warn(ar->ab, "failed to set sta power save mode %d for vdev %d: %d\n",
			    psmode, arvif->vdev_id, ret);
}

static bool ath12k_mac_supports_tpc(struct ath12k *ar, struct ath12k_vif *ahvif,
				    const struct cfg80211_chan_def *chandef)
{
	return ath12k_wmi_supports_6ghz_cc_ext(ar) &&
		test_bit(WMI_TLV_SERVICE_EXT_TPC_REG_SUPPORT, ar->ab->wmi_ab.svc_map) &&
		(ahvif->vdev_type == WMI_VDEV_TYPE_STA  ||
		 ahvif->vdev_type == WMI_VDEV_TYPE_AP) &&
		ahvif->vdev_subtype == WMI_VDEV_SUBTYPE_NONE &&
		chandef->chan &&
		chandef->chan->band == NL80211_BAND_6GHZ;
}

static void ath12k_mac_bss_info_changed(struct ath12k *ar,
					struct ath12k_link_vif *arvif,
					struct ieee80211_bss_conf *info,
					u64 changed)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(ahvif);
	struct ieee80211_vif_cfg *vif_cfg = &vif->cfg;
	struct cfg80211_chan_def def;
	u32 param_id, param_value;
	enum nl80211_band band;
	u32 vdev_param;
	int mcast_rate;
	u32 preamble;
	u16 hw_value;
	u16 bitrate;
	int ret;
	u8 rateidx;
	u32 rate;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (changed & BSS_CHANGED_BEACON_INT) {
		arvif->beacon_interval = info->beacon_int;

		param_id = WMI_VDEV_PARAM_BEACON_INTERVAL;
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    param_id,
						    arvif->beacon_interval);
		if (ret)
			ath12k_warn(ar->ab, "Failed to set beacon interval for VDEV: %d\n",
				    arvif->vdev_id);
		else
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
				   "Beacon interval: %d set for VDEV: %d\n",
				   arvif->beacon_interval, arvif->vdev_id);
	}

	if (changed & BSS_CHANGED_BEACON) {
		param_id = WMI_PDEV_PARAM_BEACON_TX_MODE;
		param_value = WMI_BEACON_BURST_MODE;
		ret = ath12k_wmi_pdev_set_param(ar, param_id,
						param_value, ar->pdev->pdev_id);
		if (ret)
			ath12k_warn(ar->ab, "Failed to set beacon mode for VDEV: %d\n",
				    arvif->vdev_id);
		else
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
				   "Set burst beacon mode for VDEV: %d\n",
				   arvif->vdev_id);

		ret = ath12k_mac_setup_bcn_tmpl(arvif);
		if (ret)
			ath12k_warn(ar->ab, "failed to update bcn template: %d\n",
				    ret);
	}

	if (changed & (BSS_CHANGED_BEACON_INFO | BSS_CHANGED_BEACON)) {
		arvif->dtim_period = info->dtim_period;

		param_id = WMI_VDEV_PARAM_DTIM_PERIOD;
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    param_id,
						    arvif->dtim_period);

		if (ret)
			ath12k_warn(ar->ab, "Failed to set dtim period for VDEV %d: %i\n",
				    arvif->vdev_id, ret);
		else
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
				   "DTIM period: %d set for VDEV: %d\n",
				   arvif->dtim_period, arvif->vdev_id);
	}

	if (changed & BSS_CHANGED_SSID &&
	    vif->type == NL80211_IFTYPE_AP) {
		ahvif->u.ap.ssid_len = vif->cfg.ssid_len;
		if (vif->cfg.ssid_len)
			memcpy(ahvif->u.ap.ssid, vif->cfg.ssid, vif->cfg.ssid_len);
		ahvif->u.ap.hidden_ssid = info->hidden_ssid;
	}

	if (changed & BSS_CHANGED_BSSID && !is_zero_ether_addr(info->bssid))
		ether_addr_copy(arvif->bssid, info->bssid);

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		if (info->enable_beacon) {
			ret = ath12k_mac_set_he_txbf_conf(arvif);
			if (ret)
				ath12k_warn(ar->ab,
					    "failed to set HE TXBF config for vdev: %d\n",
					    arvif->vdev_id);

			ret = ath12k_mac_set_eht_txbf_conf(arvif);
			if (ret)
				ath12k_warn(ar->ab,
					    "failed to set EHT TXBF config for vdev: %d\n",
					    arvif->vdev_id);
		}
		ath12k_control_beaconing(arvif, info);

		if (arvif->is_up && info->he_support &&
		    info->he_oper.params) {
			/* TODO: Extend to support 1024 BA Bitmap size */
			ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
							    WMI_VDEV_PARAM_BA_MODE,
							    WMI_BA_MODE_BUFFER_SIZE_256);
			if (ret)
				ath12k_warn(ar->ab,
					    "failed to set BA BUFFER SIZE 256 for vdev: %d\n",
					    arvif->vdev_id);

			param_id = WMI_VDEV_PARAM_HEOPS_0_31;
			param_value = info->he_oper.params;
			ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
							    param_id, param_value);
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
				   "he oper param: %x set for VDEV: %d\n",
				   param_value, arvif->vdev_id);

			if (ret)
				ath12k_warn(ar->ab, "Failed to set he oper params %x for VDEV %d: %i\n",
					    param_value, arvif->vdev_id, ret);
		}
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		u32 cts_prot;

		cts_prot = !!(info->use_cts_prot);
		param_id = WMI_VDEV_PARAM_PROTECTION_MODE;

		if (arvif->is_started) {
			ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
							    param_id, cts_prot);
			if (ret)
				ath12k_warn(ar->ab, "Failed to set CTS prot for VDEV: %d\n",
					    arvif->vdev_id);
			else
				ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "Set CTS prot: %d for VDEV: %d\n",
					   cts_prot, arvif->vdev_id);
		} else {
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "defer protection mode setup, vdev is not ready yet\n");
		}
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		u32 slottime;

		if (info->use_short_slot)
			slottime = WMI_VDEV_SLOT_TIME_SHORT; /* 9us */

		else
			slottime = WMI_VDEV_SLOT_TIME_LONG; /* 20us */

		param_id = WMI_VDEV_PARAM_SLOT_TIME;
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    param_id, slottime);
		if (ret)
			ath12k_warn(ar->ab, "Failed to set erp slot for VDEV: %d\n",
				    arvif->vdev_id);
		else
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
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
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    param_id, preamble);
		if (ret)
			ath12k_warn(ar->ab, "Failed to set preamble for VDEV: %d\n",
				    arvif->vdev_id);
		else
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
				   "Set preamble: %d for VDEV: %d\n",
				   preamble, arvif->vdev_id);
	}

	if (changed & BSS_CHANGED_ASSOC) {
		if (vif->cfg.assoc)
			ath12k_bss_assoc(ar, arvif, info);
		else
			ath12k_bss_disassoc(ar, arvif);
	}

	if (changed & BSS_CHANGED_TXPOWER) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac vdev_id %i txpower %d\n",
			   arvif->vdev_id, info->txpower);

		arvif->txpower = info->txpower;
		ath12k_mac_txpower_recalc(ar);
	}

	if (changed & BSS_CHANGED_MCAST_RATE &&
	    !ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)) {
		band = def.chan->band;
		mcast_rate = info->mcast_rate[band];

		if (mcast_rate > 0) {
			rateidx = mcast_rate - 1;
		} else {
			if (info->basic_rates)
				rateidx = __ffs(info->basic_rates);
			else
				rateidx = 0;
		}

		if (ar->pdev->cap.supported_bands & WMI_HOST_WLAN_5GHZ_CAP)
			rateidx += ATH12K_MAC_FIRST_OFDM_RATE_IDX;

		bitrate = ath12k_legacy_rates[rateidx].bitrate;
		hw_value = ath12k_legacy_rates[rateidx].hw_value;

		if (ath12k_mac_bitrate_is_cck(bitrate))
			preamble = WMI_RATE_PREAMBLE_CCK;
		else
			preamble = WMI_RATE_PREAMBLE_OFDM;

		rate = ATH12K_HW_RATE_CODE(hw_value, 0, preamble);

		ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
			   "mac vdev %d mcast_rate %x\n",
			   arvif->vdev_id, rate);

		vdev_param = WMI_VDEV_PARAM_MCAST_DATA_RATE;
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    vdev_param, rate);
		if (ret)
			ath12k_warn(ar->ab,
				    "failed to set mcast rate on vdev %i: %d\n",
				    arvif->vdev_id,  ret);

		vdev_param = WMI_VDEV_PARAM_BCAST_DATA_RATE;
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    vdev_param, rate);
		if (ret)
			ath12k_warn(ar->ab,
				    "failed to set bcast rate on vdev %i: %d\n",
				    arvif->vdev_id,  ret);
	}

	if (changed & BSS_CHANGED_BASIC_RATES &&
	    !ath12k_mac_vif_link_chan(vif, arvif->link_id, &def))
		ath12k_recalculate_mgmt_rate(ar, arvif, &def);

	if (changed & BSS_CHANGED_TWT) {
		if (info->twt_requester || info->twt_responder)
			ath12k_wmi_send_twt_enable_cmd(ar, ar->pdev->pdev_id);
		else
			ath12k_wmi_send_twt_disable_cmd(ar, ar->pdev->pdev_id);
	}

	if (changed & BSS_CHANGED_HE_OBSS_PD)
		ath12k_wmi_send_obss_spr_cmd(ar, arvif->vdev_id,
					     &info->he_obss_pd);

	if (changed & BSS_CHANGED_HE_BSS_COLOR) {
		if (vif->type == NL80211_IFTYPE_AP) {
			ret = ath12k_wmi_obss_color_cfg_cmd(ar,
							    arvif->vdev_id,
							    info->he_bss_color.color,
							    ATH12K_BSS_COLOR_AP_PERIODS,
							    info->he_bss_color.enabled);
			if (ret)
				ath12k_warn(ar->ab, "failed to set bss color collision on vdev %i: %d\n",
					    arvif->vdev_id,  ret);
		} else if (vif->type == NL80211_IFTYPE_STATION) {
			ret = ath12k_wmi_send_bss_color_change_enable_cmd(ar,
									  arvif->vdev_id,
									  1);
			if (ret)
				ath12k_warn(ar->ab, "failed to enable bss color change on vdev %i: %d\n",
					    arvif->vdev_id,  ret);
			ret = ath12k_wmi_obss_color_cfg_cmd(ar,
							    arvif->vdev_id,
							    0,
							    ATH12K_BSS_COLOR_STA_PERIODS,
							    1);
			if (ret)
				ath12k_warn(ar->ab, "failed to set bss color collision on vdev %i: %d\n",
					    arvif->vdev_id,  ret);
		}
	}

	ath12k_mac_fils_discovery(arvif, info);

	if (changed & BSS_CHANGED_PS &&
	    ar->ab->hw_params->supports_sta_ps) {
		ahvif->ps = vif_cfg->ps;
		ath12k_mac_vif_setup_ps(arvif);
	}
}

static struct ath12k_vif_cache *ath12k_ahvif_get_link_cache(struct ath12k_vif *ahvif,
							    u8 link_id)
{
	if (!ahvif->cache[link_id]) {
		ahvif->cache[link_id] = kzalloc(sizeof(*ahvif->cache[0]), GFP_KERNEL);
		if (ahvif->cache[link_id])
			INIT_LIST_HEAD(&ahvif->cache[link_id]->key_conf.list);
	}

	return ahvif->cache[link_id];
}

static void ath12k_ahvif_put_link_key_cache(struct ath12k_vif_cache *cache)
{
	struct ath12k_key_conf *key_conf, *tmp;

	if (!cache || list_empty(&cache->key_conf.list))
		return;
	list_for_each_entry_safe(key_conf, tmp, &cache->key_conf.list, list) {
		list_del(&key_conf->list);
		kfree(key_conf);
	}
}

static void ath12k_ahvif_put_link_cache(struct ath12k_vif *ahvif, u8 link_id)
{
	if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return;

	ath12k_ahvif_put_link_key_cache(ahvif->cache[link_id]);
	kfree(ahvif->cache[link_id]);
	ahvif->cache[link_id] = NULL;
}

static void ath12k_mac_op_link_info_changed(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif,
					    struct ieee80211_bss_conf *info,
					    u64 changed)
{
	struct ath12k *ar;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_vif_cache *cache;
	struct ath12k_link_vif *arvif;
	u8 link_id = info->link_id;

	lockdep_assert_wiphy(hw->wiphy);

	arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);

	/* if the vdev is not created on a certain radio,
	 * cache the info to be updated later on vdev creation
	 */

	if (!arvif || !arvif->is_created) {
		cache = ath12k_ahvif_get_link_cache(ahvif, link_id);
		if (!cache)
			return;

		cache->bss_conf_changed |= changed;

		return;
	}

	ar = arvif->ar;

	ath12k_mac_bss_info_changed(ar, arvif, info, changed);
}

static struct ath12k*
ath12k_mac_select_scan_device(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      u32 center_freq)
{
	struct ath12k_hw *ah = hw->priv;
	enum nl80211_band band;
	struct ath12k *ar;
	int i;

	if (ah->num_radio == 1)
		return ah->radio;

	/* Currently mac80211 supports splitting scan requests into
	 * multiple scan requests per band.
	 * Loop through first channel and determine the scan radio
	 * TODO: There could be 5 GHz low/high channels in that case
	 * split the hw request and perform multiple scans
	 */

	if (center_freq < ATH12K_MIN_5GHZ_FREQ)
		band = NL80211_BAND_2GHZ;
	else if (center_freq < ATH12K_MIN_6GHZ_FREQ)
		band = NL80211_BAND_5GHZ;
	else
		band = NL80211_BAND_6GHZ;

	for_each_ar(ah, ar, i) {
		if (ar->mac.sbands[band].channels &&
		    center_freq >= KHZ_TO_MHZ(ar->freq_range.start_freq) &&
		    center_freq <= KHZ_TO_MHZ(ar->freq_range.end_freq))
			return ar;
	}

	return NULL;
}

void __ath12k_mac_scan_finish(struct ath12k *ar)
{
	struct ieee80211_hw *hw = ath12k_ar_to_hw(ar);

	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH12K_SCAN_IDLE:
		break;
	case ATH12K_SCAN_RUNNING:
	case ATH12K_SCAN_ABORTING:
		if (ar->scan.is_roc && ar->scan.roc_notify)
			ieee80211_remain_on_channel_expired(hw);
		fallthrough;
	case ATH12K_SCAN_STARTING:
		cancel_delayed_work(&ar->scan.timeout);
		complete_all(&ar->scan.completed);
		wiphy_work_queue(ar->ah->hw->wiphy, &ar->scan.vdev_clean_wk);
		break;
	}
}

void ath12k_mac_scan_finish(struct ath12k *ar)
{
	spin_lock_bh(&ar->data_lock);
	__ath12k_mac_scan_finish(ar);
	spin_unlock_bh(&ar->data_lock);
}

static int ath12k_scan_stop(struct ath12k *ar)
{
	struct ath12k_wmi_scan_cancel_arg arg = {
		.req_type = WLAN_SCAN_CANCEL_SINGLE,
		.scan_id = ATH12K_SCAN_ID,
	};
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	/* TODO: Fill other STOP Params */
	arg.pdev_id = ar->pdev->pdev_id;

	ret = ath12k_wmi_send_scan_stop_cmd(ar, &arg);
	if (ret) {
		ath12k_warn(ar->ab, "failed to stop wmi scan: %d\n", ret);
		goto out;
	}

	ret = wait_for_completion_timeout(&ar->scan.completed, 3 * HZ);
	if (ret == 0) {
		ath12k_warn(ar->ab,
			    "failed to receive scan abort comple: timed out\n");
		ret = -ETIMEDOUT;
	} else if (ret > 0) {
		ret = 0;
	}

out:
	/* Scan state should be updated in scan completion worker but in
	 * case firmware fails to deliver the event (for whatever reason)
	 * it is desired to clean up scan state anyway. Firmware may have
	 * just dropped the scan completion event delivery due to transport
	 * pipe being overflown with data and/or it can recover on its own
	 * before next scan request is submitted.
	 */
	spin_lock_bh(&ar->data_lock);
	if (ret)
		__ath12k_mac_scan_finish(ar);
	spin_unlock_bh(&ar->data_lock);

	return ret;
}

static void ath12k_scan_abort(struct ath12k *ar)
{
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	spin_lock_bh(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH12K_SCAN_IDLE:
		/* This can happen if timeout worker kicked in and called
		 * abortion while scan completion was being processed.
		 */
		break;
	case ATH12K_SCAN_STARTING:
	case ATH12K_SCAN_ABORTING:
		ath12k_warn(ar->ab, "refusing scan abortion due to invalid scan state: %d\n",
			    ar->scan.state);
		break;
	case ATH12K_SCAN_RUNNING:
		ar->scan.state = ATH12K_SCAN_ABORTING;
		spin_unlock_bh(&ar->data_lock);

		ret = ath12k_scan_stop(ar);
		if (ret)
			ath12k_warn(ar->ab, "failed to abort scan: %d\n", ret);

		spin_lock_bh(&ar->data_lock);
		break;
	}

	spin_unlock_bh(&ar->data_lock);
}

static void ath12k_scan_timeout_work(struct work_struct *work)
{
	struct ath12k *ar = container_of(work, struct ath12k,
					 scan.timeout.work);

	wiphy_lock(ath12k_ar_to_hw(ar)->wiphy);
	ath12k_scan_abort(ar);
	wiphy_unlock(ath12k_ar_to_hw(ar)->wiphy);
}

static void ath12k_mac_scan_send_complete(struct ath12k *ar,
					  struct cfg80211_scan_info *info)
{
	struct ath12k_hw *ah = ar->ah;
	struct ath12k *partner_ar;
	int i;

	lockdep_assert_wiphy(ah->hw->wiphy);

	for_each_ar(ah, partner_ar, i)
		if (partner_ar != ar &&
		    partner_ar->scan.state == ATH12K_SCAN_RUNNING)
			return;

	ieee80211_scan_completed(ah->hw, info);
}

static void ath12k_scan_vdev_clean_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct ath12k *ar = container_of(work, struct ath12k,
					 scan.vdev_clean_wk);
	struct ath12k_hw *ah = ar->ah;
	struct ath12k_link_vif *arvif;

	lockdep_assert_wiphy(wiphy);

	arvif = ar->scan.arvif;

	/* The scan vdev has already been deleted. This can occur when a
	 * new scan request is made on the same vif with a different
	 * frequency, causing the scan arvif to move from one radio to
	 * another. Or, scan was abrupted and via remove interface, the
	 * arvif is already deleted. Alternatively, if the scan vdev is not
	 * being used as an actual vdev, then do not delete it.
	 */
	if (!arvif || arvif->is_started)
		goto work_complete;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac clean scan vdev (link id %u)",
		   arvif->link_id);

	ath12k_mac_remove_link_interface(ah->hw, arvif);
	ath12k_mac_unassign_link_vif(arvif);

work_complete:
	spin_lock_bh(&ar->data_lock);
	ar->scan.arvif = NULL;
	if (!ar->scan.is_roc) {
		struct cfg80211_scan_info info = {
			.aborted = ((ar->scan.state ==
				    ATH12K_SCAN_ABORTING) ||
				    (ar->scan.state ==
				    ATH12K_SCAN_STARTING)),
		};

		ath12k_mac_scan_send_complete(ar, &info);
	}

	ar->scan.state = ATH12K_SCAN_IDLE;
	ar->scan_channel = NULL;
	ar->scan.roc_freq = 0;
	spin_unlock_bh(&ar->data_lock);
}

static int ath12k_start_scan(struct ath12k *ar,
			     struct ath12k_wmi_scan_req_arg *arg)
{
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ret = ath12k_wmi_send_scan_start_cmd(ar, arg);
	if (ret)
		return ret;

	ret = wait_for_completion_timeout(&ar->scan.started, 1 * HZ);
	if (ret == 0) {
		ret = ath12k_scan_stop(ar);
		if (ret)
			ath12k_warn(ar->ab, "failed to stop scan: %d\n", ret);

		return -ETIMEDOUT;
	}

	/* If we failed to start the scan, return error code at
	 * this point.  This is probably due to some issue in the
	 * firmware, but no need to wedge the driver due to that...
	 */
	spin_lock_bh(&ar->data_lock);
	if (ar->scan.state == ATH12K_SCAN_IDLE) {
		spin_unlock_bh(&ar->data_lock);
		return -EINVAL;
	}
	spin_unlock_bh(&ar->data_lock);

	return 0;
}

int ath12k_mac_get_fw_stats(struct ath12k *ar,
			    struct ath12k_fw_stats_req_params *param)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_hw *ah = ath12k_ar_to_ah(ar);
	unsigned long time_left;
	int ret;

	guard(mutex)(&ah->hw_mutex);

	if (ah->state != ATH12K_HW_STATE_ON)
		return -ENETDOWN;

	ath12k_fw_stats_reset(ar);

	reinit_completion(&ar->fw_stats_complete);
	reinit_completion(&ar->fw_stats_done);

	ret = ath12k_wmi_send_stats_request_cmd(ar, param->stats_id,
						param->vdev_id, param->pdev_id);
	if (ret) {
		ath12k_warn(ab, "failed to request fw stats: %d\n", ret);
		return ret;
	}

	ath12k_dbg(ab, ATH12K_DBG_WMI,
		   "get fw stat pdev id %d vdev id %d stats id 0x%x\n",
		   param->pdev_id, param->vdev_id, param->stats_id);

	time_left = wait_for_completion_timeout(&ar->fw_stats_complete, 1 * HZ);
	if (!time_left) {
		ath12k_warn(ab, "time out while waiting for get fw stats\n");
		return -ETIMEDOUT;
	}

	/* Firmware sends WMI_UPDATE_STATS_EVENTID back-to-back
	 * when stats data buffer limit is reached. fw_stats_complete
	 * is completed once host receives first event from firmware, but
	 * still there could be more events following. Below is to wait
	 * until firmware completes sending all the events.
	 */
	time_left = wait_for_completion_timeout(&ar->fw_stats_done, 3 * HZ);
	if (!time_left) {
		ath12k_warn(ab, "time out while waiting for fw stats done\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int ath12k_mac_op_get_txpower(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     unsigned int link_id,
				     int *dbm)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_fw_stats_req_params params = {};
	struct ath12k_fw_stats_pdev *pdev;
	struct ath12k_hw *ah = hw->priv;
	struct ath12k_link_vif *arvif;
	struct ath12k_base *ab;
	struct ath12k *ar;
	int ret;

	/* Final Tx power is minimum of Target Power, CTL power, Regulatory
	 * Power, PSD EIRP Power. We just know the Regulatory power from the
	 * regulatory rules obtained. FW knows all these power and sets the min
	 * of these. Hence, we request the FW pdev stats in which FW reports
	 * the minimum of all vdev's channel Tx power.
	 */
	lockdep_assert_wiphy(hw->wiphy);

	arvif = wiphy_dereference(ah->hw->wiphy, ahvif->link[link_id]);
	if (!arvif || !arvif->ar)
		return -EINVAL;

	ar = arvif->ar;
	ab = ar->ab;
	if (ah->state != ATH12K_HW_STATE_ON)
		goto err_fallback;

	if (test_bit(ATH12K_FLAG_CAC_RUNNING, &ar->dev_flags))
		return -EAGAIN;

	/* Limit the requests to Firmware for fetching the tx power */
	if (ar->chan_tx_pwr != ATH12K_PDEV_TX_POWER_INVALID &&
	    time_before(jiffies,
			msecs_to_jiffies(ATH12K_PDEV_TX_POWER_REFRESH_TIME_MSECS) +
					 ar->last_tx_power_update))
		goto send_tx_power;

	params.pdev_id = ar->pdev->pdev_id;
	params.vdev_id = arvif->vdev_id;
	params.stats_id = WMI_REQUEST_PDEV_STAT;
	ret = ath12k_mac_get_fw_stats(ar, &params);
	if (ret) {
		ath12k_warn(ab, "failed to request fw pdev stats: %d\n", ret);
		goto err_fallback;
	}

	spin_lock_bh(&ar->data_lock);
	pdev = list_first_entry_or_null(&ar->fw_stats.pdevs,
					struct ath12k_fw_stats_pdev, list);
	if (!pdev) {
		spin_unlock_bh(&ar->data_lock);
		goto err_fallback;
	}

	/* tx power reported by firmware is in units of 0.5 dBm */
	ar->chan_tx_pwr = pdev->chan_tx_power / 2;
	spin_unlock_bh(&ar->data_lock);
	ar->last_tx_power_update = jiffies;

send_tx_power:
	*dbm = ar->chan_tx_pwr;
	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "txpower fetched from firmware %d dBm\n",
		   *dbm);
	return 0;

err_fallback:
	/* We didn't get txpower from FW. Hence, relying on vif->bss_conf.txpower */
	*dbm = vif->bss_conf.txpower;
	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "txpower from firmware NaN, reported %d dBm\n",
		   *dbm);
	return 0;
}

static u8
ath12k_mac_find_link_id_by_ar(struct ath12k_vif *ahvif, struct ath12k *ar)
{
	struct ath12k_link_vif *arvif;
	struct ath12k_hw *ah = ahvif->ah;
	unsigned long links = ahvif->links_map;
	unsigned long scan_links_map;
	u8 link_id;

	lockdep_assert_wiphy(ah->hw->wiphy);

	for_each_set_bit(link_id, &links, ATH12K_NUM_MAX_LINKS) {
		arvif = wiphy_dereference(ah->hw->wiphy, ahvif->link[link_id]);

		if (!arvif || !arvif->is_created)
			continue;

		if (ar == arvif->ar)
			return link_id;
	}

	/* input ar is not assigned to any of the links of ML VIF, use next
	 * available scan link for scan vdev creation. There are cases where
	 * single scan req needs to be split in driver and initiate separate
	 * scan requests to firmware based on device.
	 */

	 /* Unset all non-scan links (0-14) of scan_links_map so that ffs() will
	  * choose an available link among scan links (i.e link id >= 15)
	  */
	scan_links_map = ~ahvif->links_map & ATH12K_SCAN_LINKS_MASK;
	if (scan_links_map)
		return __ffs(scan_links_map);

	return ATH12K_FIRST_SCAN_LINK;
}

static int ath12k_mac_initiate_hw_scan(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_scan_request *hw_req,
				       int n_channels,
				       struct ieee80211_channel **chan_list,
				       struct ath12k *ar)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif;
	struct cfg80211_scan_request *req = &hw_req->req;
	struct ath12k_wmi_scan_req_arg *arg = NULL;
	u8 link_id;
	int ret;
	int i;
	bool create = true;

	lockdep_assert_wiphy(hw->wiphy);

	arvif = &ahvif->deflink;

	/* check if any of the links of ML VIF is already started on
	 * radio(ar) corresponding to given scan frequency and use it,
	 * if not use scan link (link id >= 15) for scan purpose.
	 */
	link_id = ath12k_mac_find_link_id_by_ar(ahvif, ar);
	/* All scan links are occupied. ideally this shouldn't happen as
	 * mac80211 won't schedule scan for same band until ongoing scan is
	 * completed, don't try to exceed max links just in case if it happens.
	 */
	if (link_id >= ATH12K_NUM_MAX_LINKS)
		return -EBUSY;

	arvif = ath12k_mac_assign_link_vif(ah, vif, link_id);

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac link ID %d selected for scan",
		   arvif->link_id);

	/* If the vif is already assigned to a specific vdev of an ar,
	 * check whether its already started, vdev which is started
	 * are not allowed to switch to a new radio.
	 * If the vdev is not started, but was earlier created on a
	 * different ar, delete that vdev and create a new one. We don't
	 * delete at the scan stop as an optimization to avoid redundant
	 * delete-create vdev's for the same ar, in case the request is
	 * always on the same band for the vif
	 */
	if (arvif->is_created) {
		if (WARN_ON(!arvif->ar))
			return -EINVAL;

		if (ar != arvif->ar && arvif->is_started)
			return -EINVAL;

		if (ar != arvif->ar) {
			ath12k_mac_remove_link_interface(hw, arvif);
			ath12k_mac_unassign_link_vif(arvif);
		} else {
			create = false;
		}
	}

	if (create) {
		/* Previous arvif would've been cleared in radio switch block
		 * above, assign arvif again for create.
		 */
		arvif = ath12k_mac_assign_link_vif(ah, vif, link_id);

		ret = ath12k_mac_vdev_create(ar, arvif);
		if (ret) {
			ath12k_warn(ar->ab, "unable to create scan vdev %d\n", ret);
			return -EINVAL;
		}
	}

	spin_lock_bh(&ar->data_lock);
	switch (ar->scan.state) {
	case ATH12K_SCAN_IDLE:
		reinit_completion(&ar->scan.started);
		reinit_completion(&ar->scan.completed);
		ar->scan.state = ATH12K_SCAN_STARTING;
		ar->scan.is_roc = false;
		ar->scan.arvif = arvif;
		ret = 0;
		break;
	case ATH12K_SCAN_STARTING:
	case ATH12K_SCAN_RUNNING:
	case ATH12K_SCAN_ABORTING:
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

	ath12k_wmi_start_scan_init(ar, arg);
	arg->vdev_id = arvif->vdev_id;
	arg->scan_id = ATH12K_SCAN_ID;

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
		for (i = 0; i < arg->num_ssids; i++)
			arg->ssid[i] = req->ssids[i];
	} else {
		arg->scan_f_passive = 1;
	}

	if (n_channels) {
		arg->num_chan = n_channels;
		arg->chan_list = kcalloc(arg->num_chan, sizeof(*arg->chan_list),
					 GFP_KERNEL);
		if (!arg->chan_list) {
			ret = -ENOMEM;
			goto exit;
		}

		for (i = 0; i < arg->num_chan; i++)
			arg->chan_list[i] = chan_list[i]->center_freq;
	}

	ret = ath12k_start_scan(ar, arg);
	if (ret) {
		if (ret == -EBUSY)
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
				   "scan engine is busy 11d state %d\n", ar->state_11d);
		else
			ath12k_warn(ar->ab, "failed to start hw scan: %d\n", ret);

		spin_lock_bh(&ar->data_lock);
		ar->scan.state = ATH12K_SCAN_IDLE;
		spin_unlock_bh(&ar->data_lock);
		goto exit;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac scan started");

	/* Add a margin to account for event/command processing */
	ieee80211_queue_delayed_work(ath12k_ar_to_hw(ar), &ar->scan.timeout,
				     msecs_to_jiffies(arg->max_scan_time +
						      ATH12K_MAC_SCAN_TIMEOUT_MSECS));

exit:
	if (arg) {
		kfree(arg->chan_list);
		kfree(arg->extraie.ptr);
		kfree(arg);
	}

	if (ar->state_11d == ATH12K_11D_PREPARING &&
	    ahvif->vdev_type == WMI_VDEV_TYPE_STA &&
	    ahvif->vdev_subtype == WMI_VDEV_SUBTYPE_NONE)
		ath12k_mac_11d_scan_start(ar, arvif->vdev_id);

	return ret;
}

static int ath12k_mac_op_hw_scan(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_scan_request *hw_req)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ieee80211_channel **chan_list, *chan;
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	unsigned long links_map, link_id;
	struct ath12k_link_vif *arvif;
	struct ath12k *ar, *scan_ar;
	int i, j, ret = 0;

	lockdep_assert_wiphy(hw->wiphy);

	chan_list = kcalloc(hw_req->req.n_channels, sizeof(*chan_list), GFP_KERNEL);
	if (!chan_list)
		return -ENOMEM;

	/* There could be channels that belong to multiple underlying radio
	 * in same scan request as mac80211 sees it as single band. In that
	 * case split the hw_req based on frequency range and schedule scans to
	 * corresponding radio.
	 */
	for_each_ar(ah, ar, i) {
		int n_chans = 0;

		for (j = 0; j < hw_req->req.n_channels; j++) {
			chan = hw_req->req.channels[j];
			scan_ar = ath12k_mac_select_scan_device(hw, vif,
								chan->center_freq);
			if (!scan_ar) {
				ath12k_hw_warn(ah, "unable to select scan device for freq %d\n",
					       chan->center_freq);
				ret = -EINVAL;
				goto abort;
			}
			if (ar != scan_ar)
				continue;

			chan_list[n_chans++] = chan;
		}
		if (n_chans) {
			ret = ath12k_mac_initiate_hw_scan(hw, vif, hw_req, n_chans,
							  chan_list, ar);
			if (ret)
				goto abort;
		}
	}
abort:
	/* If any of the parallel scans initiated fails, abort all and
	 * remove the scan interfaces created. Return complete scan
	 * failure as mac80211 assumes this as single scan request.
	 */
	if (ret) {
		ath12k_hw_warn(ah, "Scan failed %d , cleanup all scan vdevs\n", ret);
		links_map = ahvif->links_map;
		for_each_set_bit(link_id, &links_map, ATH12K_NUM_MAX_LINKS) {
			arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
			if (!arvif)
				continue;

			ar = arvif->ar;
			if (ar->scan.arvif == arvif) {
				wiphy_work_cancel(hw->wiphy, &ar->scan.vdev_clean_wk);
				spin_lock_bh(&ar->data_lock);
				ar->scan.arvif = NULL;
				ar->scan.state = ATH12K_SCAN_IDLE;
				ar->scan_channel = NULL;
				ar->scan.roc_freq = 0;
				spin_unlock_bh(&ar->data_lock);
			}
			if (link_id >= ATH12K_FIRST_SCAN_LINK) {
				ath12k_mac_remove_link_interface(hw, arvif);
				ath12k_mac_unassign_link_vif(arvif);
			}
		}
	}
	kfree(chan_list);
	return ret;
}

static void ath12k_mac_op_cancel_hw_scan(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	unsigned long link_id, links_map = ahvif->links_map;
	struct ath12k_link_vif *arvif;
	struct ath12k *ar;

	lockdep_assert_wiphy(hw->wiphy);

	for_each_set_bit(link_id, &links_map, ATH12K_NUM_MAX_LINKS) {
		arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
		if (!arvif || arvif->is_started)
			continue;

		ar = arvif->ar;

		ath12k_scan_abort(ar);

		cancel_delayed_work_sync(&ar->scan.timeout);
	}
}

static int ath12k_install_key(struct ath12k_link_vif *arvif,
			      struct ieee80211_key_conf *key,
			      enum set_key_cmd cmd,
			      const u8 *macaddr, u32 flags)
{
	int ret;
	struct ath12k *ar = arvif->ar;
	struct wmi_vdev_install_key_arg arg = {
		.vdev_id = arvif->vdev_id,
		.key_idx = key->keyidx,
		.key_len = key->keylen,
		.key_data = key->key,
		.key_flags = flags,
		.ieee80211_key_cipher = key->cipher,
		.macaddr = macaddr,
	};
	struct ath12k_vif *ahvif = arvif->ahvif;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (test_bit(ATH12K_FLAG_HW_CRYPTO_DISABLED, &ar->ab->dev_flags))
		return 0;

	if (cmd == DISABLE_KEY) {
		/* TODO: Check if FW expects  value other than NONE for del */
		/* arg.key_cipher = WMI_CIPHER_NONE; */
		arg.key_len = 0;
		arg.key_data = NULL;
		goto check_order;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
		arg.key_cipher = WMI_CIPHER_AES_CCM;
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV_MGMT;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		arg.key_cipher = WMI_CIPHER_TKIP;
		arg.key_txmic_len = 8;
		arg.key_rxmic_len = 8;
		break;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		arg.key_cipher = WMI_CIPHER_AES_GCM;
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV_MGMT;
		break;
	default:
		ath12k_warn(ar->ab, "cipher %d is not supported\n", key->cipher);
		return -EOPNOTSUPP;
	}

	if (test_bit(ATH12K_FLAG_RAW_MODE, &ar->ab->dev_flags))
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV |
			      IEEE80211_KEY_FLAG_RESERVE_TAILROOM;

check_order:
	if (ahvif->vdev_type == WMI_VDEV_TYPE_STA &&
	    arg.key_flags == WMI_KEY_GROUP) {
		if (cmd == SET_KEY) {
			if (arvif->pairwise_key_done) {
				ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
					   "vdev %u pairwise key done, go install group key\n",
					   arg.vdev_id);
				goto install;
			} else {
				/* WCN7850 firmware requires pairwise key to be installed
				 * before group key. In case group key comes first, cache
				 * it and return. Will revisit it once pairwise key gets
				 * installed.
				 */
				arvif->group_key = arg;
				arvif->group_key_valid = true;
				ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
					   "vdev %u group key before pairwise key, cache and skip\n",
					   arg.vdev_id);

				ret = 0;
				goto out;
			}
		} else {
			arvif->group_key_valid = false;
		}
	}

install:
	reinit_completion(&ar->install_key_done);

	ret = ath12k_wmi_vdev_install_key(arvif->ar, &arg);
	if (ret)
		return ret;

	if (!wait_for_completion_timeout(&ar->install_key_done, 1 * HZ))
		return -ETIMEDOUT;

	if (ether_addr_equal(arg.macaddr, arvif->bssid))
		ahvif->key_cipher = arg.ieee80211_key_cipher;

	if (ar->install_key_status) {
		ret = -EINVAL;
		goto out;
	}

	if (ahvif->vdev_type == WMI_VDEV_TYPE_STA &&
	    arg.key_flags == WMI_KEY_PAIRWISE) {
		if (cmd == SET_KEY) {
			arvif->pairwise_key_done = true;
			if (arvif->group_key_valid) {
				/* Install cached GTK */
				arvif->group_key_valid = false;
				arg = arvif->group_key;
				ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
					   "vdev %u pairwise key done, group key ready, go install\n",
					   arg.vdev_id);
				goto install;
			}
		} else {
			arvif->pairwise_key_done = false;
		}
	}

out:
	if (ret) {
		/* In case of failure userspace may not do DISABLE_KEY
		 * but triggers re-connection directly, so manually reset
		 * status here.
		 */
		arvif->group_key_valid = false;
		arvif->pairwise_key_done = false;
	}

	return ret;
}

static int ath12k_clear_peer_keys(struct ath12k_link_vif *arvif,
				  const u8 *addr)
{
	struct ath12k *ar = arvif->ar;
	struct ath12k_base *ab = ar->ab;
	struct ath12k_peer *peer;
	int first_errno = 0;
	int ret;
	int i;
	u32 flags = 0;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	spin_lock_bh(&ab->base_lock);
	peer = ath12k_peer_find(ab, arvif->vdev_id, addr);
	spin_unlock_bh(&ab->base_lock);

	if (!peer)
		return -ENOENT;

	for (i = 0; i < ARRAY_SIZE(peer->keys); i++) {
		if (!peer->keys[i])
			continue;

		/* key flags are not required to delete the key */
		ret = ath12k_install_key(arvif, peer->keys[i],
					 DISABLE_KEY, addr, flags);
		if (ret < 0 && first_errno == 0)
			first_errno = ret;

		if (ret < 0)
			ath12k_warn(ab, "failed to remove peer key %d: %d\n",
				    i, ret);

		spin_lock_bh(&ab->base_lock);
		peer->keys[i] = NULL;
		spin_unlock_bh(&ab->base_lock);
	}

	return first_errno;
}

static int ath12k_mac_set_key(struct ath12k *ar, enum set_key_cmd cmd,
			      struct ath12k_link_vif *arvif,
			      struct ath12k_link_sta *arsta,
			      struct ieee80211_key_conf *key)
{
	struct ieee80211_sta *sta = NULL;
	struct ath12k_base *ab = ar->ab;
	struct ath12k_peer *peer;
	struct ath12k_sta *ahsta;
	const u8 *peer_addr;
	int ret;
	u32 flags = 0;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (arsta)
		sta = ath12k_ahsta_to_sta(arsta->ahsta);

	if (test_bit(ATH12K_FLAG_HW_CRYPTO_DISABLED, &ab->dev_flags))
		return 1;

	if (sta)
		peer_addr = arsta->addr;
	else
		peer_addr = arvif->bssid;

	key->hw_key_idx = key->keyidx;

	/* the peer should not disappear in mid-way (unless FW goes awry) since
	 * we already hold wiphy lock. we just make sure its there now.
	 */
	spin_lock_bh(&ab->base_lock);
	peer = ath12k_peer_find(ab, arvif->vdev_id, peer_addr);
	spin_unlock_bh(&ab->base_lock);

	if (!peer) {
		if (cmd == SET_KEY) {
			ath12k_warn(ab, "cannot install key for non-existent peer %pM\n",
				    peer_addr);
			return -EOPNOTSUPP;
		}

		/* if the peer doesn't exist there is no key to disable
		 * anymore
		 */
		return 0;
	}

	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
		flags = WMI_KEY_PAIRWISE;
	else
		flags = WMI_KEY_GROUP;

	ret = ath12k_install_key(arvif, key, cmd, peer_addr, flags);
	if (ret) {
		ath12k_warn(ab, "ath12k_install_key failed (%d)\n", ret);
		return ret;
	}

	ret = ath12k_dp_rx_peer_pn_replay_config(arvif, peer_addr, cmd, key);
	if (ret) {
		ath12k_warn(ab, "failed to offload PN replay detection %d\n", ret);
		return ret;
	}

	spin_lock_bh(&ab->base_lock);
	peer = ath12k_peer_find(ab, arvif->vdev_id, peer_addr);
	if (peer && cmd == SET_KEY) {
		peer->keys[key->keyidx] = key;
		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
			peer->ucast_keyidx = key->keyidx;
			peer->sec_type = ath12k_dp_tx_get_encrypt_type(key->cipher);
		} else {
			peer->mcast_keyidx = key->keyidx;
			peer->sec_type_grp = ath12k_dp_tx_get_encrypt_type(key->cipher);
		}
	} else if (peer && cmd == DISABLE_KEY) {
		peer->keys[key->keyidx] = NULL;
		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
			peer->ucast_keyidx = 0;
		else
			peer->mcast_keyidx = 0;
	} else if (!peer)
		/* impossible unless FW goes crazy */
		ath12k_warn(ab, "peer %pM disappeared!\n", peer_addr);

	if (sta) {
		ahsta = ath12k_sta_to_ahsta(sta);

		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_TKIP:
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_CCMP_256:
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
			if (cmd == SET_KEY)
				ahsta->pn_type = HAL_PN_TYPE_WPA;
			else
				ahsta->pn_type = HAL_PN_TYPE_NONE;
			break;
		default:
			ahsta->pn_type = HAL_PN_TYPE_NONE;
			break;
		}
	}

	spin_unlock_bh(&ab->base_lock);

	return 0;
}

static int ath12k_mac_update_key_cache(struct ath12k_vif_cache *cache,
				       enum set_key_cmd cmd,
				       struct ieee80211_sta *sta,
				       struct ieee80211_key_conf *key)
{
	struct ath12k_key_conf *key_conf, *tmp;

	list_for_each_entry_safe(key_conf, tmp, &cache->key_conf.list, list) {
		if (key_conf->key != key)
			continue;

		/* If SET key entry is already present in cache, nothing to do,
		 * just return
		 */
		if (cmd == SET_KEY)
			return 0;

		/* DEL key for an old SET key which driver hasn't flushed yet.
		 */
		list_del(&key_conf->list);
		kfree(key_conf);
	}

	if (cmd == SET_KEY) {
		key_conf = kzalloc(sizeof(*key_conf), GFP_KERNEL);

		if (!key_conf)
			return -ENOMEM;

		key_conf->cmd = cmd;
		key_conf->sta = sta;
		key_conf->key = key;
		list_add_tail(&key_conf->list,
			      &cache->key_conf.list);
	}

	return 0;
}

static int ath12k_mac_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
				 struct ieee80211_vif *vif, struct ieee80211_sta *sta,
				 struct ieee80211_key_conf *key)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif;
	struct ath12k_link_sta *arsta = NULL;
	struct ath12k_vif_cache *cache;
	struct ath12k_sta *ahsta;
	unsigned long links;
	u8 link_id;
	int ret;

	lockdep_assert_wiphy(hw->wiphy);

	/* BIP needs to be done in software */
	if (key->cipher == WLAN_CIPHER_SUITE_AES_CMAC ||
	    key->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_128 ||
	    key->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_256 ||
	    key->cipher == WLAN_CIPHER_SUITE_BIP_CMAC_256) {
		return 1;
	}

	if (key->keyidx > WMI_MAX_KEY_INDEX)
		return -ENOSPC;

	if (sta) {
		ahsta = ath12k_sta_to_ahsta(sta);

		/* For an ML STA Pairwise key is same for all associated link Stations,
		 * hence do set key for all link STAs which are active.
		 */
		if (sta->mlo) {
			links = ahsta->links_map;
			for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
				arvif = wiphy_dereference(hw->wiphy,
							  ahvif->link[link_id]);
				arsta = wiphy_dereference(hw->wiphy,
							  ahsta->link[link_id]);

				if (WARN_ON(!arvif || !arsta))
					/* arvif and arsta are expected to be valid when
					 * STA is present.
					 */
					continue;

				ret = ath12k_mac_set_key(arvif->ar, cmd, arvif,
							 arsta, key);
				if (ret)
					break;
			}

			return 0;
		}

		arsta = &ahsta->deflink;
		arvif = arsta->arvif;
		if (WARN_ON(!arvif))
			return -EINVAL;

		ret = ath12k_mac_set_key(arvif->ar, cmd, arvif, arsta, key);
		if (ret)
			return ret;

		return 0;
	}

	if (key->link_id >= 0 && key->link_id < IEEE80211_MLD_MAX_NUM_LINKS) {
		link_id = key->link_id;
		arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
	} else {
		link_id = 0;
		arvif = &ahvif->deflink;
	}

	if (!arvif || !arvif->is_created) {
		cache = ath12k_ahvif_get_link_cache(ahvif, link_id);
		if (!cache)
			return -ENOSPC;

		ret = ath12k_mac_update_key_cache(cache, cmd, sta, key);
		if (ret)
			return ret;

		return 0;
	}

	ret = ath12k_mac_set_key(arvif->ar, cmd, arvif, NULL, key);
	if (ret)
		return ret;

	return 0;
}

static int
ath12k_mac_bitrate_mask_num_vht_rates(struct ath12k *ar,
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
ath12k_mac_bitrate_mask_num_he_rates(struct ath12k *ar,
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
ath12k_mac_set_peer_vht_fixed_rate(struct ath12k_link_vif *arvif,
				   struct ath12k_link_sta *arsta,
				   const struct cfg80211_bitrate_mask *mask,
				   enum nl80211_band band)
{
	struct ath12k *ar = arvif->ar;
	u8 vht_rate, nss;
	u32 rate_code;
	int ret, i;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	nss = 0;

	for (i = 0; i < ARRAY_SIZE(mask->control[band].vht_mcs); i++) {
		if (hweight16(mask->control[band].vht_mcs[i]) == 1) {
			nss = i + 1;
			vht_rate = ffs(mask->control[band].vht_mcs[i]) - 1;
		}
	}

	if (!nss) {
		ath12k_warn(ar->ab, "No single VHT Fixed rate found to set for %pM",
			    arsta->addr);
		return -EINVAL;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "Setting Fixed VHT Rate for peer %pM. Device will not switch to any other selected rates",
		   arsta->addr);

	rate_code = ATH12K_HW_RATE_CODE(vht_rate, nss - 1,
					WMI_RATE_PREAMBLE_VHT);
	ret = ath12k_wmi_set_peer_param(ar, arsta->addr,
					arvif->vdev_id,
					WMI_PEER_PARAM_FIXED_RATE,
					rate_code);
	if (ret)
		ath12k_warn(ar->ab,
			    "failed to update STA %pM Fixed Rate %d: %d\n",
			     arsta->addr, rate_code, ret);

	return ret;
}

static int
ath12k_mac_set_peer_he_fixed_rate(struct ath12k_link_vif *arvif,
				  struct ath12k_link_sta *arsta,
				  const struct cfg80211_bitrate_mask *mask,
				  enum nl80211_band band)
{
	struct ath12k *ar = arvif->ar;
	u8 he_rate, nss;
	u32 rate_code;
	int ret, i;
	struct ath12k_sta *ahsta = arsta->ahsta;
	struct ieee80211_sta *sta;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	sta = ath12k_ahsta_to_sta(ahsta);
	nss = 0;

	for (i = 0; i < ARRAY_SIZE(mask->control[band].he_mcs); i++) {
		if (hweight16(mask->control[band].he_mcs[i]) == 1) {
			nss = i + 1;
			he_rate = ffs(mask->control[band].he_mcs[i]) - 1;
		}
	}

	if (!nss) {
		ath12k_warn(ar->ab, "No single HE Fixed rate found to set for %pM",
			    arsta->addr);
		return -EINVAL;
	}

	/* Avoid updating invalid nss as fixed rate*/
	if (nss > sta->deflink.rx_nss)
		return -EINVAL;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "Setting Fixed HE Rate for peer %pM. Device will not switch to any other selected rates",
		   arsta->addr);

	rate_code = ATH12K_HW_RATE_CODE(he_rate, nss - 1,
					WMI_RATE_PREAMBLE_HE);

	ret = ath12k_wmi_set_peer_param(ar, arsta->addr,
					arvif->vdev_id,
					WMI_PEER_PARAM_FIXED_RATE,
					rate_code);
	if (ret)
		ath12k_warn(ar->ab,
			    "failed to update STA %pM Fixed Rate %d: %d\n",
			    arsta->addr, rate_code, ret);

	return ret;
}

static int ath12k_mac_station_assoc(struct ath12k *ar,
				    struct ath12k_link_vif *arvif,
				    struct ath12k_link_sta *arsta,
				    bool reassoc)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	struct ieee80211_link_sta *link_sta;
	int ret;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	struct cfg80211_bitrate_mask *mask;
	u8 num_vht_rates, num_he_rates;
	u8 link_id = arvif->link_id;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (WARN_ON(ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)))
		return -EPERM;

	if (WARN_ON(!rcu_access_pointer(sta->link[link_id])))
		return -EINVAL;

	band = def.chan->band;
	mask = &arvif->bitrate_mask;

	struct ath12k_wmi_peer_assoc_arg *peer_arg __free(kfree) =
		kzalloc(sizeof(*peer_arg), GFP_KERNEL);
	if (!peer_arg)
		return -ENOMEM;

	ath12k_peer_assoc_prepare(ar, arvif, arsta, peer_arg, reassoc);

	if (peer_arg->peer_nss < 1) {
		ath12k_warn(ar->ab,
			    "invalid peer NSS %d\n", peer_arg->peer_nss);
		return -EINVAL;
	}

	peer_arg->is_assoc = true;
	ret = ath12k_wmi_send_peer_assoc_cmd(ar, peer_arg);
	if (ret) {
		ath12k_warn(ar->ab, "failed to run peer assoc for STA %pM vdev %i: %d\n",
			    arsta->addr, arvif->vdev_id, ret);
		return ret;
	}

	if (!wait_for_completion_timeout(&ar->peer_assoc_done, 1 * HZ)) {
		ath12k_warn(ar->ab, "failed to get peer assoc conf event for %pM vdev %i\n",
			    arsta->addr, arvif->vdev_id);
		return -ETIMEDOUT;
	}

	num_vht_rates = ath12k_mac_bitrate_mask_num_vht_rates(ar, band, mask);
	num_he_rates = ath12k_mac_bitrate_mask_num_he_rates(ar, band, mask);

	/* If single VHT/HE rate is configured (by set_bitrate_mask()),
	 * peer_assoc will disable VHT/HE. This is now enabled by a peer specific
	 * fixed param.
	 * Note that all other rates and NSS will be disabled for this peer.
	 */
	link_sta = ath12k_mac_get_link_sta(arsta);
	if (!link_sta) {
		ath12k_warn(ar->ab, "unable to access link sta in station assoc\n");
		return -EINVAL;
	}

	spin_lock_bh(&ar->data_lock);
	arsta->bw = ath12k_mac_ieee80211_sta_bw_to_wmi(ar, link_sta);
	arsta->bw_prev = link_sta->bandwidth;
	spin_unlock_bh(&ar->data_lock);

	if (link_sta->vht_cap.vht_supported && num_vht_rates == 1) {
		ret = ath12k_mac_set_peer_vht_fixed_rate(arvif, arsta, mask, band);
	} else if (link_sta->he_cap.has_he && num_he_rates == 1) {
		ret = ath12k_mac_set_peer_he_fixed_rate(arvif, arsta, mask, band);
		if (ret)
			return ret;
	}

	/* Re-assoc is run only to update supported rates for given station. It
	 * doesn't make much sense to reconfigure the peer completely.
	 */
	if (reassoc)
		return 0;

	ret = ath12k_setup_peer_smps(ar, arvif, arsta->addr,
				     &link_sta->ht_cap, &link_sta->he_6ghz_capa);
	if (ret) {
		ath12k_warn(ar->ab, "failed to setup peer SMPS for vdev %d: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	if (!sta->wme) {
		arvif->num_legacy_stations++;
		ret = ath12k_recalc_rtscts_prot(arvif);
		if (ret)
			return ret;
	}

	if (sta->wme && sta->uapsd_queues) {
		ret = ath12k_peer_assoc_qos_ap(ar, arvif, arsta);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set qos params for STA %pM for vdev %i: %d\n",
				    arsta->addr, arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath12k_mac_station_disassoc(struct ath12k *ar,
				       struct ath12k_link_vif *arvif,
				       struct ath12k_link_sta *arsta)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (!sta->wme) {
		arvif->num_legacy_stations--;
		return ath12k_recalc_rtscts_prot(arvif);
	}

	return 0;
}

static void ath12k_sta_rc_update_wk(struct wiphy *wiphy, struct wiphy_work *wk)
{
	struct ieee80211_link_sta *link_sta;
	struct ath12k *ar;
	struct ath12k_link_vif *arvif;
	struct ieee80211_sta *sta;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	const u8 *ht_mcs_mask;
	const u16 *vht_mcs_mask;
	const u16 *he_mcs_mask;
	u32 changed, bw, nss, mac_nss, smps, bw_prev;
	int err, num_vht_rates, num_he_rates;
	const struct cfg80211_bitrate_mask *mask;
	enum wmi_phy_mode peer_phymode;
	struct ath12k_link_sta *arsta;
	struct ieee80211_vif *vif;

	lockdep_assert_wiphy(wiphy);

	arsta = container_of(wk, struct ath12k_link_sta, update_wk);
	sta = ath12k_ahsta_to_sta(arsta->ahsta);
	arvif = arsta->arvif;
	vif = ath12k_ahvif_to_vif(arvif->ahvif);
	ar = arvif->ar;

	if (WARN_ON(ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)))
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

	nss = max_t(u32, 1, nss);
	mac_nss = max3(ath12k_mac_max_ht_nss(ht_mcs_mask),
		       ath12k_mac_max_vht_nss(vht_mcs_mask),
		       ath12k_mac_max_he_nss(he_mcs_mask));
	nss = min(nss, mac_nss);

	struct ath12k_wmi_peer_assoc_arg *peer_arg __free(kfree) =
					kzalloc(sizeof(*peer_arg), GFP_KERNEL);
	if (!peer_arg)
		return;

	if (changed & IEEE80211_RC_BW_CHANGED) {
		ath12k_peer_assoc_h_phymode(ar, arvif, arsta, peer_arg);
		peer_phymode = peer_arg->peer_phymode;

		if (bw > bw_prev) {
			/* Phymode shows maximum supported channel width, if we
			 * upgrade bandwidth then due to sanity check of firmware,
			 * we have to send WMI_PEER_PHYMODE followed by
			 * WMI_PEER_CHWIDTH
			 */
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac bandwidth upgrade for sta %pM new %d old %d\n",
				   arsta->addr, bw, bw_prev);
			err = ath12k_wmi_set_peer_param(ar, arsta->addr,
							arvif->vdev_id, WMI_PEER_PHYMODE,
							peer_phymode);
			if (err) {
				ath12k_warn(ar->ab, "failed to update STA %pM to peer phymode %d: %d\n",
					    arsta->addr, peer_phymode, err);
				return;
			}
			err = ath12k_wmi_set_peer_param(ar, arsta->addr,
							arvif->vdev_id, WMI_PEER_CHWIDTH,
							bw);
			if (err)
				ath12k_warn(ar->ab, "failed to update STA %pM to peer bandwidth %d: %d\n",
					    arsta->addr, bw, err);
		} else {
			/* When we downgrade bandwidth this will conflict with phymode
			 * and cause to trigger firmware crash. In this case we send
			 * WMI_PEER_CHWIDTH followed by WMI_PEER_PHYMODE
			 */
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac bandwidth downgrade for sta %pM new %d old %d\n",
				   arsta->addr, bw, bw_prev);
			err = ath12k_wmi_set_peer_param(ar, arsta->addr,
							arvif->vdev_id, WMI_PEER_CHWIDTH,
							bw);
			if (err) {
				ath12k_warn(ar->ab, "failed to update STA %pM peer to bandwidth %d: %d\n",
					    arsta->addr, bw, err);
				return;
			}
			err = ath12k_wmi_set_peer_param(ar, arsta->addr,
							arvif->vdev_id, WMI_PEER_PHYMODE,
							peer_phymode);
			if (err)
				ath12k_warn(ar->ab, "failed to update STA %pM to peer phymode %d: %d\n",
					    arsta->addr, peer_phymode, err);
		}
	}

	if (changed & IEEE80211_RC_NSS_CHANGED) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac update sta %pM nss %d\n",
			   arsta->addr, nss);

		err = ath12k_wmi_set_peer_param(ar, arsta->addr, arvif->vdev_id,
						WMI_PEER_NSS, nss);
		if (err)
			ath12k_warn(ar->ab, "failed to update STA %pM nss %d: %d\n",
				    arsta->addr, nss, err);
	}

	if (changed & IEEE80211_RC_SMPS_CHANGED) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac update sta %pM smps %d\n",
			   arsta->addr, smps);

		err = ath12k_wmi_set_peer_param(ar, arsta->addr, arvif->vdev_id,
						WMI_PEER_MIMO_PS_STATE, smps);
		if (err)
			ath12k_warn(ar->ab, "failed to update STA %pM smps %d: %d\n",
				    arsta->addr, smps, err);
	}

	if (changed & IEEE80211_RC_SUPP_RATES_CHANGED) {
		mask = &arvif->bitrate_mask;
		num_vht_rates = ath12k_mac_bitrate_mask_num_vht_rates(ar, band,
								      mask);
		num_he_rates = ath12k_mac_bitrate_mask_num_he_rates(ar, band,
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
		link_sta = ath12k_mac_get_link_sta(arsta);
		if (!link_sta) {
			ath12k_warn(ar->ab, "unable to access link sta in peer assoc he for sta %pM link %u\n",
				    sta->addr, arsta->link_id);
			return;
		}

		if (link_sta->vht_cap.vht_supported && num_vht_rates == 1) {
			ath12k_mac_set_peer_vht_fixed_rate(arvif, arsta, mask,
							   band);
		} else if (link_sta->he_cap.has_he && num_he_rates == 1) {
			ath12k_mac_set_peer_he_fixed_rate(arvif, arsta, mask, band);
		} else {
			/* If the peer is non-VHT/HE or no fixed VHT/HE rate
			 * is provided in the new bitrate mask we set the
			 * other rates using peer_assoc command. Also clear
			 * the peer fixed rate settings as it has higher proprity
			 * than peer assoc
			 */
			err = ath12k_wmi_set_peer_param(ar, arsta->addr,
							arvif->vdev_id,
							WMI_PEER_PARAM_FIXED_RATE,
							WMI_FIXED_RATE_NONE);
			if (err)
				ath12k_warn(ar->ab,
					    "failed to disable peer fixed rate for STA %pM ret %d\n",
					    arsta->addr, err);

			ath12k_peer_assoc_prepare(ar, arvif, arsta,
						  peer_arg, true);

			peer_arg->is_assoc = false;
			err = ath12k_wmi_send_peer_assoc_cmd(ar, peer_arg);
			if (err)
				ath12k_warn(ar->ab, "failed to run peer assoc for STA %pM vdev %i: %d\n",
					    arsta->addr, arvif->vdev_id, err);

			if (!wait_for_completion_timeout(&ar->peer_assoc_done, 1 * HZ))
				ath12k_warn(ar->ab, "failed to get peer assoc conf event for %pM vdev %i\n",
					    arsta->addr, arvif->vdev_id);
		}
	}
}

static void ath12k_mac_free_unassign_link_sta(struct ath12k_hw *ah,
					      struct ath12k_sta *ahsta,
					      u8 link_id)
{
	struct ath12k_link_sta *arsta;

	lockdep_assert_wiphy(ah->hw->wiphy);

	if (WARN_ON(link_id >= IEEE80211_MLD_MAX_NUM_LINKS))
		return;

	arsta = wiphy_dereference(ah->hw->wiphy, ahsta->link[link_id]);
	if (WARN_ON(!arsta))
		return;

	ahsta->links_map &= ~BIT(link_id);
	rcu_assign_pointer(ahsta->link[link_id], NULL);
	synchronize_rcu();

	if (arsta == &ahsta->deflink) {
		arsta->link_id = ATH12K_INVALID_LINK_ID;
		arsta->ahsta = NULL;
		arsta->arvif = NULL;
		return;
	}

	kfree(arsta);
}

static int ath12k_mac_inc_num_stations(struct ath12k_link_vif *arvif,
				       struct ath12k_link_sta *arsta)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	struct ath12k *ar = arvif->ar;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (arvif->ahvif->vdev_type == WMI_VDEV_TYPE_STA && !sta->tdls)
		return 0;

	if (ar->num_stations >= ar->max_num_stations)
		return -ENOBUFS;

	ar->num_stations++;

	return 0;
}

static void ath12k_mac_dec_num_stations(struct ath12k_link_vif *arvif,
					struct ath12k_link_sta *arsta)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	struct ath12k *ar = arvif->ar;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (arvif->ahvif->vdev_type == WMI_VDEV_TYPE_STA && !sta->tdls)
		return;

	ar->num_stations--;
}

static void ath12k_mac_station_post_remove(struct ath12k *ar,
					   struct ath12k_link_vif *arvif,
					   struct ath12k_link_sta *arsta)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	struct ath12k_peer *peer;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ath12k_mac_dec_num_stations(arvif, arsta);

	spin_lock_bh(&ar->ab->base_lock);

	peer = ath12k_peer_find(ar->ab, arvif->vdev_id, arsta->addr);
	if (peer && peer->sta == sta) {
		ath12k_warn(ar->ab, "Found peer entry %pM n vdev %i after it was supposedly removed\n",
			    vif->addr, arvif->vdev_id);
		peer->sta = NULL;
		list_del(&peer->list);
		kfree(peer);
		ar->num_peers--;
	}

	spin_unlock_bh(&ar->ab->base_lock);

	kfree(arsta->rx_stats);
	arsta->rx_stats = NULL;
}

static int ath12k_mac_station_unauthorize(struct ath12k *ar,
					  struct ath12k_link_vif *arvif,
					  struct ath12k_link_sta *arsta)
{
	struct ath12k_peer *peer;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	spin_lock_bh(&ar->ab->base_lock);

	peer = ath12k_peer_find(ar->ab, arvif->vdev_id, arsta->addr);
	if (peer)
		peer->is_authorized = false;

	spin_unlock_bh(&ar->ab->base_lock);

	/* Driver must clear the keys during the state change from
	 * IEEE80211_STA_AUTHORIZED to IEEE80211_STA_ASSOC, since after
	 * returning from here, mac80211 is going to delete the keys
	 * in __sta_info_destroy_part2(). This will ensure that the driver does
	 * not retain stale key references after mac80211 deletes the keys.
	 */
	ret = ath12k_clear_peer_keys(arvif, arsta->addr);
	if (ret) {
		ath12k_warn(ar->ab, "failed to clear all peer keys for vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	return 0;
}

static int ath12k_mac_station_authorize(struct ath12k *ar,
					struct ath12k_link_vif *arvif,
					struct ath12k_link_sta *arsta)
{
	struct ath12k_peer *peer;
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	spin_lock_bh(&ar->ab->base_lock);

	peer = ath12k_peer_find(ar->ab, arvif->vdev_id, arsta->addr);
	if (peer)
		peer->is_authorized = true;

	spin_unlock_bh(&ar->ab->base_lock);

	if (vif->type == NL80211_IFTYPE_STATION && arvif->is_up) {
		ret = ath12k_wmi_set_peer_param(ar, arsta->addr,
						arvif->vdev_id,
						WMI_PEER_AUTHORIZE,
						1);
		if (ret) {
			ath12k_warn(ar->ab, "Unable to authorize peer %pM vdev %d: %d\n",
				    arsta->addr, arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath12k_mac_station_remove(struct ath12k *ar,
				     struct ath12k_link_vif *arvif,
				     struct ath12k_link_sta *arsta)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	struct ath12k_vif *ahvif = arvif->ahvif;
	int ret = 0;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	wiphy_work_cancel(ar->ah->hw->wiphy, &arsta->update_wk);

	if (ahvif->vdev_type == WMI_VDEV_TYPE_STA) {
		ath12k_bss_disassoc(ar, arvif);
		ret = ath12k_mac_vdev_stop(arvif);
		if (ret)
			ath12k_warn(ar->ab, "failed to stop vdev %i: %d\n",
				    arvif->vdev_id, ret);
	}

	if (sta->mlo)
		return ret;

	ath12k_dp_peer_cleanup(ar, arvif->vdev_id, arsta->addr);

	ret = ath12k_peer_delete(ar, arvif->vdev_id, arsta->addr);
	if (ret)
		ath12k_warn(ar->ab, "Failed to delete peer: %pM for VDEV: %d\n",
			    arsta->addr, arvif->vdev_id);
	else
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "Removed peer: %pM for VDEV: %d\n",
			   arsta->addr, arvif->vdev_id);

	ath12k_mac_station_post_remove(ar, arvif, arsta);

	if (sta->valid_links)
		ath12k_mac_free_unassign_link_sta(ahvif->ah,
						  arsta->ahsta, arsta->link_id);

	return ret;
}

static int ath12k_mac_station_add(struct ath12k *ar,
				  struct ath12k_link_vif *arvif,
				  struct ath12k_link_sta *arsta)
{
	struct ath12k_base *ab = ar->ab;
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(arsta->ahsta);
	struct ath12k_wmi_peer_create_arg peer_param = {0};
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ret = ath12k_mac_inc_num_stations(arvif, arsta);
	if (ret) {
		ath12k_warn(ab, "refusing to associate station: too many connected already (%d)\n",
			    ar->max_num_stations);
		goto exit;
	}

	if (ath12k_debugfs_is_extd_rx_stats_enabled(ar) && !arsta->rx_stats) {
		arsta->rx_stats = kzalloc(sizeof(*arsta->rx_stats), GFP_KERNEL);
		if (!arsta->rx_stats) {
			ret = -ENOMEM;
			goto dec_num_station;
		}
	}

	peer_param.vdev_id = arvif->vdev_id;
	peer_param.peer_addr = arsta->addr;
	peer_param.peer_type = WMI_PEER_TYPE_DEFAULT;
	peer_param.ml_enabled = sta->mlo;

	ret = ath12k_peer_create(ar, arvif, sta, &peer_param);
	if (ret) {
		ath12k_warn(ab, "Failed to add peer: %pM for VDEV: %d\n",
			    arsta->addr, arvif->vdev_id);
		goto free_peer;
	}

	ath12k_dbg(ab, ATH12K_DBG_MAC, "Added peer: %pM for VDEV: %d\n",
		   arsta->addr, arvif->vdev_id);

	if (ieee80211_vif_is_mesh(vif)) {
		ret = ath12k_wmi_set_peer_param(ar, arsta->addr,
						arvif->vdev_id,
						WMI_PEER_USE_4ADDR, 1);
		if (ret) {
			ath12k_warn(ab, "failed to STA %pM 4addr capability: %d\n",
				    arsta->addr, ret);
			goto free_peer;
		}
	}

	ret = ath12k_dp_peer_setup(ar, arvif->vdev_id, arsta->addr);
	if (ret) {
		ath12k_warn(ab, "failed to setup dp for peer %pM on vdev %i (%d)\n",
			    arsta->addr, arvif->vdev_id, ret);
		goto free_peer;
	}

	if (ab->hw_params->vdev_start_delay &&
	    !arvif->is_started &&
	    arvif->ahvif->vdev_type != WMI_VDEV_TYPE_AP) {
		ret = ath12k_start_vdev_delay(ar, arvif);
		if (ret) {
			ath12k_warn(ab, "failed to delay vdev start: %d\n", ret);
			goto free_peer;
		}
	}

	ewma_avg_rssi_init(&arsta->avg_rssi);
	return 0;

free_peer:
	ath12k_peer_delete(ar, arvif->vdev_id, arsta->addr);
	kfree(arsta->rx_stats);
	arsta->rx_stats = NULL;
dec_num_station:
	ath12k_mac_dec_num_stations(arvif, arsta);
exit:
	return ret;
}

static int ath12k_mac_assign_link_sta(struct ath12k_hw *ah,
				      struct ath12k_sta *ahsta,
				      struct ath12k_link_sta *arsta,
				      struct ath12k_vif *ahvif,
				      u8 link_id)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(ahsta);
	struct ieee80211_link_sta *link_sta;
	struct ath12k_link_vif *arvif;

	lockdep_assert_wiphy(ah->hw->wiphy);

	if (!arsta || link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return -EINVAL;

	arvif = wiphy_dereference(ah->hw->wiphy, ahvif->link[link_id]);
	if (!arvif)
		return -EINVAL;

	memset(arsta, 0, sizeof(*arsta));

	link_sta = wiphy_dereference(ah->hw->wiphy, sta->link[link_id]);
	if (!link_sta)
		return -EINVAL;

	ether_addr_copy(arsta->addr, link_sta->addr);

	/* logical index of the link sta in order of creation */
	arsta->link_idx = ahsta->num_peer++;

	arsta->link_id = link_id;
	ahsta->links_map |= BIT(arsta->link_id);
	arsta->arvif = arvif;
	arsta->ahsta = ahsta;
	ahsta->ahvif = ahvif;

	wiphy_work_init(&arsta->update_wk, ath12k_sta_rc_update_wk);

	rcu_assign_pointer(ahsta->link[link_id], arsta);

	return 0;
}

static void ath12k_mac_ml_station_remove(struct ath12k_vif *ahvif,
					 struct ath12k_sta *ahsta)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(ahsta);
	struct ath12k_hw *ah = ahvif->ah;
	struct ath12k_link_vif *arvif;
	struct ath12k_link_sta *arsta;
	unsigned long links;
	struct ath12k *ar;
	u8 link_id;

	lockdep_assert_wiphy(ah->hw->wiphy);

	ath12k_peer_mlo_link_peers_delete(ahvif, ahsta);

	/* validate link station removal and clear arsta links */
	links = ahsta->links_map;
	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = wiphy_dereference(ah->hw->wiphy, ahvif->link[link_id]);
		arsta = wiphy_dereference(ah->hw->wiphy, ahsta->link[link_id]);
		if (!arvif || !arsta)
			continue;

		ar = arvif->ar;

		ath12k_mac_station_post_remove(ar, arvif, arsta);

		ath12k_mac_free_unassign_link_sta(ah, ahsta, link_id);
	}

	ath12k_peer_ml_delete(ah, sta);
}

static int ath12k_mac_handle_link_sta_state(struct ieee80211_hw *hw,
					    struct ath12k_link_vif *arvif,
					    struct ath12k_link_sta *arsta,
					    enum ieee80211_sta_state old_state,
					    enum ieee80211_sta_state new_state)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_bss_conf *link_conf;
	struct ath12k *ar = arvif->ar;
	struct ath12k_reg_info *reg_info;
	struct ath12k_base *ab = ar->ab;
	int ret = 0;

	lockdep_assert_wiphy(hw->wiphy);

	ath12k_dbg(ab, ATH12K_DBG_MAC, "mac handle link %u sta %pM state %d -> %d\n",
		   arsta->link_id, arsta->addr, old_state, new_state);

	/* IEEE80211_STA_NONE -> IEEE80211_STA_NOTEXIST: Remove the station
	 * from driver
	 */
	if ((old_state == IEEE80211_STA_NONE &&
	     new_state == IEEE80211_STA_NOTEXIST)) {
		ret = ath12k_mac_station_remove(ar, arvif, arsta);
		if (ret) {
			ath12k_warn(ab, "Failed to remove station: %pM for VDEV: %d\n",
				    arsta->addr, arvif->vdev_id);
			goto exit;
		}
	}

	/* IEEE80211_STA_NOTEXIST -> IEEE80211_STA_NONE: Add new station to driver */
	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE) {
		ret = ath12k_mac_station_add(ar, arvif, arsta);
		if (ret)
			ath12k_warn(ab, "Failed to add station: %pM for VDEV: %d\n",
				    arsta->addr, arvif->vdev_id);

	/* IEEE80211_STA_AUTH -> IEEE80211_STA_ASSOC: Send station assoc command for
	 * peer associated to AP/Mesh/ADHOC vif type.
	 */
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_ASSOC &&
		   (vif->type == NL80211_IFTYPE_AP ||
		    vif->type == NL80211_IFTYPE_MESH_POINT ||
		    vif->type == NL80211_IFTYPE_ADHOC)) {
		ret = ath12k_mac_station_assoc(ar, arvif, arsta, false);
		if (ret)
			ath12k_warn(ab, "Failed to associate station: %pM\n",
				    arsta->addr);

	/* IEEE80211_STA_ASSOC -> IEEE80211_STA_AUTHORIZED: set peer status as
	 * authorized
	 */
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTHORIZED) {
		ret = ath12k_mac_station_authorize(ar, arvif, arsta);
		if (ret) {
			ath12k_warn(ab, "Failed to authorize station: %pM\n",
				    arsta->addr);
			goto exit;
		}

		if (ath12k_wmi_supports_6ghz_cc_ext(ar) &&
		    arvif->ahvif->vdev_type == WMI_VDEV_TYPE_STA) {
			link_conf = ath12k_mac_get_link_bss_conf(arvif);
			reg_info = ab->reg_info[ar->pdev_idx];
			ath12k_dbg(ab, ATH12K_DBG_MAC, "connection done, update reg rules\n");
			ath12k_hw_to_ah(hw)->regd_updated = false;
			ath12k_reg_handle_chan_list(ab, reg_info, arvif->ahvif->vdev_type,
						    link_conf->power_type);
		}

	/* IEEE80211_STA_AUTHORIZED -> IEEE80211_STA_ASSOC: station may be in removal,
	 * deauthorize it.
	 */
	} else if (old_state == IEEE80211_STA_AUTHORIZED &&
		   new_state == IEEE80211_STA_ASSOC) {
		ath12k_mac_station_unauthorize(ar, arvif, arsta);

	/* IEEE80211_STA_ASSOC -> IEEE80211_STA_AUTH: disassoc peer connected to
	 * AP/mesh/ADHOC vif type.
	 */
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTH &&
		   (vif->type == NL80211_IFTYPE_AP ||
		    vif->type == NL80211_IFTYPE_MESH_POINT ||
		    vif->type == NL80211_IFTYPE_ADHOC)) {
		ret = ath12k_mac_station_disassoc(ar, arvif, arsta);
		if (ret)
			ath12k_warn(ab, "Failed to disassociate station: %pM\n",
				    arsta->addr);
	}

exit:
	return ret;
}

static bool ath12k_mac_is_freq_on_mac(struct ath12k_hw_mode_freq_range_arg *freq_range,
				      u32 freq, u8 mac_id)
{
	return (freq >= freq_range[mac_id].low_2ghz_freq &&
		freq <= freq_range[mac_id].high_2ghz_freq) ||
	       (freq >= freq_range[mac_id].low_5ghz_freq &&
		freq <= freq_range[mac_id].high_5ghz_freq);
}

static bool
ath12k_mac_2_freq_same_mac_in_freq_range(struct ath12k_base *ab,
					 struct ath12k_hw_mode_freq_range_arg *freq_range,
					 u32 freq_link1, u32 freq_link2)
{
	u8 i;

	for (i = 0; i < MAX_RADIOS; i++) {
		if (ath12k_mac_is_freq_on_mac(freq_range, freq_link1, i) &&
		    ath12k_mac_is_freq_on_mac(freq_range, freq_link2, i))
			return true;
	}

	return false;
}

static bool ath12k_mac_is_hw_dbs_capable(struct ath12k_base *ab)
{
	return test_bit(WMI_TLV_SERVICE_DUAL_BAND_SIMULTANEOUS_SUPPORT,
			ab->wmi_ab.svc_map) &&
	       ab->wmi_ab.hw_mode_info.support_dbs;
}

static bool ath12k_mac_2_freq_same_mac_in_dbs(struct ath12k_base *ab,
					      u32 freq_link1, u32 freq_link2)
{
	struct ath12k_hw_mode_freq_range_arg *freq_range;

	if (!ath12k_mac_is_hw_dbs_capable(ab))
		return true;

	freq_range = ab->wmi_ab.hw_mode_info.freq_range_caps[ATH12K_HW_MODE_DBS];
	return ath12k_mac_2_freq_same_mac_in_freq_range(ab, freq_range,
							freq_link1, freq_link2);
}

static bool ath12k_mac_is_hw_sbs_capable(struct ath12k_base *ab)
{
	return test_bit(WMI_TLV_SERVICE_DUAL_BAND_SIMULTANEOUS_SUPPORT,
			ab->wmi_ab.svc_map) &&
	       ab->wmi_ab.hw_mode_info.support_sbs;
}

static bool ath12k_mac_2_freq_same_mac_in_sbs(struct ath12k_base *ab,
					      u32 freq_link1, u32 freq_link2)
{
	struct ath12k_hw_mode_info *info = &ab->wmi_ab.hw_mode_info;
	struct ath12k_hw_mode_freq_range_arg *sbs_uppr_share;
	struct ath12k_hw_mode_freq_range_arg *sbs_low_share;
	struct ath12k_hw_mode_freq_range_arg *sbs_range;

	if (!ath12k_mac_is_hw_sbs_capable(ab))
		return true;

	if (ab->wmi_ab.sbs_lower_band_end_freq) {
		sbs_uppr_share = info->freq_range_caps[ATH12K_HW_MODE_SBS_UPPER_SHARE];
		sbs_low_share = info->freq_range_caps[ATH12K_HW_MODE_SBS_LOWER_SHARE];

		return ath12k_mac_2_freq_same_mac_in_freq_range(ab, sbs_low_share,
								freq_link1, freq_link2) ||
		       ath12k_mac_2_freq_same_mac_in_freq_range(ab, sbs_uppr_share,
								freq_link1, freq_link2);
	}

	sbs_range = info->freq_range_caps[ATH12K_HW_MODE_SBS];
	return ath12k_mac_2_freq_same_mac_in_freq_range(ab, sbs_range,
							freq_link1, freq_link2);
}

static bool ath12k_mac_freqs_on_same_mac(struct ath12k_base *ab,
					 u32 freq_link1, u32 freq_link2)
{
	return ath12k_mac_2_freq_same_mac_in_dbs(ab, freq_link1, freq_link2) &&
	       ath12k_mac_2_freq_same_mac_in_sbs(ab, freq_link1, freq_link2);
}

static int ath12k_mac_mlo_sta_set_link_active(struct ath12k_base *ab,
					      enum wmi_mlo_link_force_reason reason,
					      enum wmi_mlo_link_force_mode mode,
					      u8 *mlo_vdev_id_lst,
					      u8 num_mlo_vdev,
					      u8 *mlo_inactive_vdev_lst,
					      u8 num_mlo_inactive_vdev)
{
	struct wmi_mlo_link_set_active_arg param = {0};
	u32 entry_idx, entry_offset, vdev_idx;
	u8 vdev_id;

	param.reason = reason;
	param.force_mode = mode;

	for (vdev_idx = 0; vdev_idx < num_mlo_vdev; vdev_idx++) {
		vdev_id = mlo_vdev_id_lst[vdev_idx];
		entry_idx = vdev_id / 32;
		entry_offset = vdev_id % 32;
		if (entry_idx >= WMI_MLO_LINK_NUM_SZ) {
			ath12k_warn(ab, "Invalid entry_idx %d num_mlo_vdev %d vdev %d",
				    entry_idx, num_mlo_vdev, vdev_id);
			return -EINVAL;
		}
		param.vdev_bitmap[entry_idx] |= 1 << entry_offset;
		/* update entry number if entry index changed */
		if (param.num_vdev_bitmap < entry_idx + 1)
			param.num_vdev_bitmap = entry_idx + 1;
	}

	ath12k_dbg(ab, ATH12K_DBG_MAC,
		   "num_vdev_bitmap %d vdev_bitmap[0] = 0x%x, vdev_bitmap[1] = 0x%x",
		   param.num_vdev_bitmap, param.vdev_bitmap[0], param.vdev_bitmap[1]);

	if (mode == WMI_MLO_LINK_FORCE_MODE_ACTIVE_INACTIVE) {
		for (vdev_idx = 0; vdev_idx < num_mlo_inactive_vdev; vdev_idx++) {
			vdev_id = mlo_inactive_vdev_lst[vdev_idx];
			entry_idx = vdev_id / 32;
			entry_offset = vdev_id % 32;
			if (entry_idx >= WMI_MLO_LINK_NUM_SZ) {
				ath12k_warn(ab, "Invalid entry_idx %d num_mlo_vdev %d vdev %d",
					    entry_idx, num_mlo_inactive_vdev, vdev_id);
				return -EINVAL;
			}
			param.inactive_vdev_bitmap[entry_idx] |= 1 << entry_offset;
			/* update entry number if entry index changed */
			if (param.num_inactive_vdev_bitmap < entry_idx + 1)
				param.num_inactive_vdev_bitmap = entry_idx + 1;
		}

		ath12k_dbg(ab, ATH12K_DBG_MAC,
			   "num_vdev_bitmap %d inactive_vdev_bitmap[0] = 0x%x, inactive_vdev_bitmap[1] = 0x%x",
			   param.num_inactive_vdev_bitmap,
			   param.inactive_vdev_bitmap[0],
			   param.inactive_vdev_bitmap[1]);
	}

	if (mode == WMI_MLO_LINK_FORCE_MODE_ACTIVE_LINK_NUM ||
	    mode == WMI_MLO_LINK_FORCE_MODE_INACTIVE_LINK_NUM) {
		param.num_link_entry = 1;
		param.link_num[0].num_of_link = num_mlo_vdev - 1;
	}

	return ath12k_wmi_send_mlo_link_set_active_cmd(ab, &param);
}

static int ath12k_mac_mlo_sta_update_link_active(struct ath12k_base *ab,
						 struct ieee80211_hw *hw,
						 struct ath12k_vif *ahvif)
{
	u8 mlo_vdev_id_lst[IEEE80211_MLD_MAX_NUM_LINKS] = {0};
	u32 mlo_freq_list[IEEE80211_MLD_MAX_NUM_LINKS] = {0};
	unsigned long links = ahvif->links_map;
	enum wmi_mlo_link_force_reason reason;
	struct ieee80211_chanctx_conf *conf;
	enum wmi_mlo_link_force_mode mode;
	struct ieee80211_bss_conf *info;
	struct ath12k_link_vif *arvif;
	u8 num_mlo_vdev = 0;
	u8 link_id;

	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
		/* make sure vdev is created on this device */
		if (!arvif || !arvif->is_created || arvif->ar->ab != ab)
			continue;

		info = ath12k_mac_get_link_bss_conf(arvif);
		conf = wiphy_dereference(hw->wiphy, info->chanctx_conf);
		mlo_freq_list[num_mlo_vdev] = conf->def.chan->center_freq;

		mlo_vdev_id_lst[num_mlo_vdev] = arvif->vdev_id;
		num_mlo_vdev++;
	}

	/* It is not allowed to activate more links than a single device
	 * supported. Something goes wrong if we reach here.
	 */
	if (num_mlo_vdev > ATH12K_NUM_MAX_ACTIVE_LINKS_PER_DEVICE) {
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	/* if 2 links are established and both link channels fall on the
	 * same hardware MAC, send command to firmware to deactivate one
	 * of them.
	 */
	if (num_mlo_vdev == 2 &&
	    ath12k_mac_freqs_on_same_mac(ab, mlo_freq_list[0],
					 mlo_freq_list[1])) {
		mode = WMI_MLO_LINK_FORCE_MODE_INACTIVE_LINK_NUM;
		reason = WMI_MLO_LINK_FORCE_REASON_NEW_CONNECT;
		return ath12k_mac_mlo_sta_set_link_active(ab, reason, mode,
							  mlo_vdev_id_lst, num_mlo_vdev,
							  NULL, 0);
	}

	return 0;
}

static bool ath12k_mac_are_sbs_chan(struct ath12k_base *ab, u32 freq_1, u32 freq_2)
{
	if (!ath12k_mac_is_hw_sbs_capable(ab))
		return false;

	if (ath12k_is_2ghz_channel_freq(freq_1) ||
	    ath12k_is_2ghz_channel_freq(freq_2))
		return false;

	return !ath12k_mac_2_freq_same_mac_in_sbs(ab, freq_1, freq_2);
}

static bool ath12k_mac_are_dbs_chan(struct ath12k_base *ab, u32 freq_1, u32 freq_2)
{
	if (!ath12k_mac_is_hw_dbs_capable(ab))
		return false;

	return !ath12k_mac_2_freq_same_mac_in_dbs(ab, freq_1, freq_2);
}

static int ath12k_mac_select_links(struct ath12k_base *ab,
				   struct ieee80211_vif *vif,
				   struct ieee80211_hw *hw,
				   u16 *selected_links)
{
	unsigned long useful_links = ieee80211_vif_usable_links(vif);
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	u8 num_useful_links = hweight_long(useful_links);
	struct ieee80211_chanctx_conf *chanctx;
	struct ath12k_link_vif *assoc_arvif;
	u32 assoc_link_freq, partner_freq;
	u16 sbs_links = 0, dbs_links = 0;
	struct ieee80211_bss_conf *info;
	struct ieee80211_channel *chan;
	struct ieee80211_sta *sta;
	struct ath12k_sta *ahsta;
	u8 link_id;

	/* activate all useful links if less than max supported */
	if (num_useful_links <= ATH12K_NUM_MAX_ACTIVE_LINKS_PER_DEVICE) {
		*selected_links = useful_links;
		return 0;
	}

	/* only in station mode we can get here, so it's safe
	 * to use ap_addr
	 */
	rcu_read_lock();
	sta = ieee80211_find_sta(vif, vif->cfg.ap_addr);
	if (!sta) {
		rcu_read_unlock();
		ath12k_warn(ab, "failed to find sta with addr %pM\n", vif->cfg.ap_addr);
		return -EINVAL;
	}

	ahsta = ath12k_sta_to_ahsta(sta);
	assoc_arvif = wiphy_dereference(hw->wiphy, ahvif->link[ahsta->assoc_link_id]);
	info = ath12k_mac_get_link_bss_conf(assoc_arvif);
	chanctx = rcu_dereference(info->chanctx_conf);
	assoc_link_freq = chanctx->def.chan->center_freq;
	rcu_read_unlock();
	ath12k_dbg(ab, ATH12K_DBG_MAC, "assoc link %u freq %u\n",
		   assoc_arvif->link_id, assoc_link_freq);

	/* assoc link is already activated and has to be kept active,
	 * only need to select a partner link from others.
	 */
	useful_links &= ~BIT(assoc_arvif->link_id);
	for_each_set_bit(link_id, &useful_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		info = wiphy_dereference(hw->wiphy, vif->link_conf[link_id]);
		if (!info) {
			ath12k_warn(ab, "failed to get link info for link: %u\n",
				    link_id);
			return -ENOLINK;
		}

		chan = info->chanreq.oper.chan;
		if (!chan) {
			ath12k_warn(ab, "failed to get chan for link: %u\n", link_id);
			return -EINVAL;
		}

		partner_freq = chan->center_freq;
		if (ath12k_mac_are_sbs_chan(ab, assoc_link_freq, partner_freq)) {
			sbs_links |= BIT(link_id);
			ath12k_dbg(ab, ATH12K_DBG_MAC, "new SBS link %u freq %u\n",
				   link_id, partner_freq);
			continue;
		}

		if (ath12k_mac_are_dbs_chan(ab, assoc_link_freq, partner_freq)) {
			dbs_links |= BIT(link_id);
			ath12k_dbg(ab, ATH12K_DBG_MAC, "new DBS link %u freq %u\n",
				   link_id, partner_freq);
			continue;
		}

		ath12k_dbg(ab, ATH12K_DBG_MAC, "non DBS/SBS link %u freq %u\n",
			   link_id, partner_freq);
	}

	/* choose the first candidate no matter how many is in the list */
	if (sbs_links)
		link_id = __ffs(sbs_links);
	else if (dbs_links)
		link_id = __ffs(dbs_links);
	else
		link_id = ffs(useful_links) - 1;

	ath12k_dbg(ab, ATH12K_DBG_MAC, "select partner link %u\n", link_id);

	*selected_links = BIT(assoc_arvif->link_id) | BIT(link_id);

	return 0;
}

static int ath12k_mac_op_sta_state(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   enum ieee80211_sta_state old_state,
				   enum ieee80211_sta_state new_state)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k_base *prev_ab = NULL, *ab;
	struct ath12k_link_vif *arvif;
	struct ath12k_link_sta *arsta;
	unsigned long valid_links;
	u16 selected_links = 0;
	u8 link_id = 0, i;
	struct ath12k *ar;
	int ret;

	lockdep_assert_wiphy(hw->wiphy);

	if (ieee80211_vif_is_mld(vif) && sta->valid_links) {
		WARN_ON(!sta->mlo && hweight16(sta->valid_links) != 1);
		link_id = ffs(sta->valid_links) - 1;
	}

	/* IEEE80211_STA_NOTEXIST -> IEEE80211_STA_NONE:
	 * New station add received. If this is a ML station then
	 * ahsta->links_map will be zero and sta->valid_links will be 1.
	 * Assign default link to the first link sta.
	 */
	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE) {
		memset(ahsta, 0, sizeof(*ahsta));

		arsta = &ahsta->deflink;

		/* ML sta */
		if (sta->mlo && !ahsta->links_map &&
		    (hweight16(sta->valid_links) == 1)) {
			ret = ath12k_peer_ml_create(ah, sta);
			if (ret) {
				ath12k_hw_warn(ah, "unable to create ML peer for sta %pM",
					       sta->addr);
				goto exit;
			}
		}

		ret = ath12k_mac_assign_link_sta(ah, ahsta, arsta, ahvif,
						 link_id);
		if (ret) {
			ath12k_hw_warn(ah, "unable assign link %d for sta %pM",
				       link_id, sta->addr);
			goto exit;
		}

		/* above arsta will get memset, hence do this after assign
		 * link sta
		 */
		if (sta->mlo) {
			/* For station mode, arvif->is_sta_assoc_link has been set when
			 * vdev starts. Make sure the arvif/arsta pair have same setting
			 */
			if (vif->type == NL80211_IFTYPE_STATION &&
			    !arsta->arvif->is_sta_assoc_link) {
				ath12k_hw_warn(ah, "failed to verify assoc link setting with link id %u\n",
					       link_id);
				ret = -EINVAL;
				goto exit;
			}

			arsta->is_assoc_link = true;
			ahsta->assoc_link_id = link_id;
		}
	}

	/* In the ML station scenario, activate all partner links once the
	 * client is transitioning to the associated state.
	 *
	 * FIXME: Ideally, this activation should occur when the client
	 * transitions to the authorized state. However, there are some
	 * issues with handling this in the firmware. Until the firmware
	 * can manage it properly, activate the links when the client is
	 * about to move to the associated state.
	 */
	if (ieee80211_vif_is_mld(vif) && vif->type == NL80211_IFTYPE_STATION &&
	    old_state == IEEE80211_STA_AUTH && new_state == IEEE80211_STA_ASSOC) {
		/* TODO: for now only do link selection for single device
		 * MLO case. Other cases would be handled in the future.
		 */
		ab = ah->radio[0].ab;
		if (ab->ag->num_devices == 1) {
			ret = ath12k_mac_select_links(ab, vif, hw, &selected_links);
			if (ret) {
				ath12k_warn(ab,
					    "failed to get selected links: %d\n", ret);
				goto exit;
			}
		} else {
			selected_links = ieee80211_vif_usable_links(vif);
		}

		ieee80211_set_active_links(vif, selected_links);
	}

	/* Handle all the other state transitions in generic way */
	valid_links = ahsta->links_map;
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
		arsta = wiphy_dereference(hw->wiphy, ahsta->link[link_id]);
		/* some assumptions went wrong! */
		if (WARN_ON(!arvif || !arsta))
			continue;

		/* vdev might be in deleted */
		if (WARN_ON(!arvif->ar))
			continue;

		ret = ath12k_mac_handle_link_sta_state(hw, arvif, arsta,
						       old_state, new_state);
		if (ret) {
			ath12k_hw_warn(ah, "unable to move link sta %d of sta %pM from state %d to %d",
				       link_id, arsta->addr, old_state, new_state);
			goto exit;
		}
	}

	if (ieee80211_vif_is_mld(vif) && vif->type == NL80211_IFTYPE_STATION &&
	    old_state == IEEE80211_STA_ASSOC && new_state == IEEE80211_STA_AUTHORIZED) {
		for_each_ar(ah, ar, i) {
			ab = ar->ab;
			if (prev_ab == ab)
				continue;

			ret = ath12k_mac_mlo_sta_update_link_active(ab, hw, ahvif);
			if (ret) {
				ath12k_warn(ab,
					    "failed to update link active state on connect %d\n",
					    ret);
				goto exit;
			}

			prev_ab = ab;
		}
	}
	/* IEEE80211_STA_NONE -> IEEE80211_STA_NOTEXIST:
	 * Remove the station from driver (handle ML sta here since that
	 * needs special handling. Normal sta will be handled in generic
	 * handler below
	 */
	if (old_state == IEEE80211_STA_NONE &&
	    new_state == IEEE80211_STA_NOTEXIST && sta->mlo)
		ath12k_mac_ml_station_remove(ahvif, ahsta);

	ret = 0;

exit:
	/* update the state if everything went well */
	if (!ret)
		ahsta->state = new_state;

	return ret;
}

static int ath12k_mac_op_sta_set_txpwr(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta)
{
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);
	struct ath12k *ar;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif;
	struct ath12k_link_sta *arsta;
	u8 link_id;
	int ret;
	s16 txpwr;

	lockdep_assert_wiphy(hw->wiphy);

	/* TODO: use link id from mac80211 once that's implemented */
	link_id = 0;

	arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
	arsta = wiphy_dereference(hw->wiphy, ahsta->link[link_id]);

	if (sta->deflink.txpwr.type == NL80211_TX_POWER_AUTOMATIC) {
		txpwr = 0;
	} else {
		txpwr = sta->deflink.txpwr.power;
		if (!txpwr) {
			ret = -EINVAL;
			goto out;
		}
	}

	if (txpwr > ATH12K_TX_POWER_MAX_VAL || txpwr < ATH12K_TX_POWER_MIN_VAL) {
		ret = -EINVAL;
		goto out;
	}

	ar = arvif->ar;

	ret = ath12k_wmi_set_peer_param(ar, arsta->addr, arvif->vdev_id,
					WMI_PEER_USE_FIXED_PWR, txpwr);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set tx power for station ret: %d\n",
			    ret);
		goto out;
	}

out:
	return ret;
}

static void ath12k_mac_op_link_sta_rc_update(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif,
					     struct ieee80211_link_sta *link_sta,
					     u32 changed)
{
	struct ieee80211_sta *sta = link_sta->sta;
	struct ath12k *ar;
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k_link_sta *arsta;
	struct ath12k_link_vif *arvif;
	struct ath12k_peer *peer;
	u32 bw, smps;

	rcu_read_lock();
	arvif = rcu_dereference(ahvif->link[link_sta->link_id]);
	if (!arvif) {
		ath12k_hw_warn(ah, "mac sta rc update failed to fetch link vif on link id %u for peer %pM\n",
			       link_sta->link_id, sta->addr);
		rcu_read_unlock();
		return;
	}

	ar = arvif->ar;

	arsta = rcu_dereference(ahsta->link[link_sta->link_id]);
	if (!arsta) {
		rcu_read_unlock();
		ath12k_warn(ar->ab, "mac sta rc update failed to fetch link sta on link id %u for peer %pM\n",
			    link_sta->link_id, sta->addr);
		return;
	}
	spin_lock_bh(&ar->ab->base_lock);

	peer = ath12k_peer_find(ar->ab, arvif->vdev_id, arsta->addr);
	if (!peer) {
		spin_unlock_bh(&ar->ab->base_lock);
		rcu_read_unlock();
		ath12k_warn(ar->ab, "mac sta rc update failed to find peer %pM on vdev %i\n",
			    arsta->addr, arvif->vdev_id);
		return;
	}

	spin_unlock_bh(&ar->ab->base_lock);

	if (arsta->link_id >= IEEE80211_MLD_MAX_NUM_LINKS) {
		rcu_read_unlock();
		return;
	}

	link_sta = rcu_dereference(sta->link[arsta->link_id]);
	if (!link_sta) {
		rcu_read_unlock();
		ath12k_warn(ar->ab, "unable to access link sta in rc update for sta %pM link %u\n",
			    sta->addr, arsta->link_id);
		return;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "mac sta rc update for %pM changed %08x bw %d nss %d smps %d\n",
		   arsta->addr, changed, link_sta->bandwidth, link_sta->rx_nss,
		   link_sta->smps_mode);

	spin_lock_bh(&ar->data_lock);

	if (changed & IEEE80211_RC_BW_CHANGED) {
		bw = ath12k_mac_ieee80211_sta_bw_to_wmi(ar, link_sta);
		arsta->bw_prev = arsta->bw;
		arsta->bw = bw;
	}

	if (changed & IEEE80211_RC_NSS_CHANGED)
		arsta->nss = link_sta->rx_nss;

	if (changed & IEEE80211_RC_SMPS_CHANGED) {
		smps = WMI_PEER_SMPS_PS_NONE;

		switch (link_sta->smps_mode) {
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
			ath12k_warn(ar->ab, "Invalid smps %d in sta rc update for %pM link %u\n",
				    link_sta->smps_mode, arsta->addr, link_sta->link_id);
			smps = WMI_PEER_SMPS_PS_NONE;
			break;
		}

		arsta->smps = smps;
	}

	arsta->changed |= changed;

	spin_unlock_bh(&ar->data_lock);

	wiphy_work_queue(hw->wiphy, &arsta->update_wk);

	rcu_read_unlock();
}

static struct ath12k_link_sta *ath12k_mac_alloc_assign_link_sta(struct ath12k_hw *ah,
								struct ath12k_sta *ahsta,
								struct ath12k_vif *ahvif,
								u8 link_id)
{
	struct ath12k_link_sta *arsta;
	int ret;

	lockdep_assert_wiphy(ah->hw->wiphy);

	if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return NULL;

	arsta = wiphy_dereference(ah->hw->wiphy, ahsta->link[link_id]);
	if (arsta)
		return NULL;

	arsta = kmalloc(sizeof(*arsta), GFP_KERNEL);
	if (!arsta)
		return NULL;

	ret = ath12k_mac_assign_link_sta(ah, ahsta, arsta, ahvif, link_id);
	if (ret) {
		kfree(arsta);
		return NULL;
	}

	return arsta;
}

static int ath12k_mac_op_change_sta_links(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_sta *sta,
					  u16 old_links, u16 new_links)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);
	struct ath12k_hw *ah = hw->priv;
	struct ath12k_link_vif *arvif;
	struct ath12k_link_sta *arsta;
	unsigned long valid_links;
	struct ath12k *ar;
	u8 link_id;
	int ret;

	lockdep_assert_wiphy(hw->wiphy);

	if (!sta->valid_links)
		return -EINVAL;

	/* Firmware does not support removal of one of link stas. All sta
	 * would be removed during ML STA delete in sta_state(), hence link
	 * sta removal is not handled here.
	 */
	if (new_links < old_links)
		return 0;

	if (ahsta->ml_peer_id == ATH12K_MLO_PEER_ID_INVALID) {
		ath12k_hw_warn(ah, "unable to add link for ml sta %pM", sta->addr);
		return -EINVAL;
	}

	/* this op is expected only after initial sta insertion with default link */
	if (WARN_ON(ahsta->links_map == 0))
		return -EINVAL;

	valid_links = new_links;
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		if (ahsta->links_map & BIT(link_id))
			continue;

		arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
		arsta = ath12k_mac_alloc_assign_link_sta(ah, ahsta, ahvif, link_id);

		if (!arvif || !arsta) {
			ath12k_hw_warn(ah, "Failed to alloc/assign link sta");
			continue;
		}

		ar = arvif->ar;
		if (!ar)
			continue;

		ret = ath12k_mac_station_add(ar, arvif, arsta);
		if (ret) {
			ath12k_warn(ar->ab, "Failed to add station: %pM for VDEV: %d\n",
				    arsta->addr, arvif->vdev_id);
			ath12k_mac_free_unassign_link_sta(ah, ahsta, link_id);
			return ret;
		}
	}

	return 0;
}

static bool ath12k_mac_op_can_activate_links(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif,
					     u16 active_links)
{
	/* TODO: Handle recovery case */

	return true;
}

static int ath12k_conf_tx_uapsd(struct ath12k_link_vif *arvif,
				u16 ac, bool enable)
{
	struct ath12k *ar = arvif->ar;
	struct ath12k_vif *ahvif = arvif->ahvif;
	u32 value;
	int ret;

	if (ahvif->vdev_type != WMI_VDEV_TYPE_STA)
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
		ahvif->u.sta.uapsd |= value;
	else
		ahvif->u.sta.uapsd &= ~value;

	ret = ath12k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
					  WMI_STA_PS_PARAM_UAPSD,
					  ahvif->u.sta.uapsd);
	if (ret) {
		ath12k_warn(ar->ab, "could not set uapsd params %d\n", ret);
		goto exit;
	}

	if (ahvif->u.sta.uapsd)
		value = WMI_STA_PS_RX_WAKE_POLICY_POLL_UAPSD;
	else
		value = WMI_STA_PS_RX_WAKE_POLICY_WAKE;

	ret = ath12k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
					  WMI_STA_PS_PARAM_RX_WAKE_POLICY,
					  value);
	if (ret)
		ath12k_warn(ar->ab, "could not set rx wake param %d\n", ret);

exit:
	return ret;
}

static int ath12k_mac_conf_tx(struct ath12k_link_vif *arvif, u16 ac,
			      const struct ieee80211_tx_queue_params *params)
{
	struct wmi_wmm_params_arg *p = NULL;
	struct ath12k *ar = arvif->ar;
	struct ath12k_base *ab = ar->ab;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

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

	ret = ath12k_wmi_send_wmm_update_cmd(ar, arvif->vdev_id,
					     &arvif->wmm_params);
	if (ret) {
		ath12k_warn(ab, "pdev idx %d failed to set wmm params: %d\n",
			    ar->pdev_idx, ret);
		goto exit;
	}

	ret = ath12k_conf_tx_uapsd(arvif, ac, params->uapsd);
	if (ret)
		ath12k_warn(ab, "pdev idx %d failed to set sta uapsd: %d\n",
			    ar->pdev_idx, ret);

exit:
	return ret;
}

static int ath12k_mac_op_conf_tx(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 unsigned int link_id, u16 ac,
				 const struct ieee80211_tx_queue_params *params)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif;
	struct ath12k_vif_cache *cache;
	int ret;

	lockdep_assert_wiphy(hw->wiphy);

	if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return -EINVAL;

	arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
	if (!arvif || !arvif->is_created) {
		cache = ath12k_ahvif_get_link_cache(ahvif, link_id);
		if (!cache)
			return -ENOSPC;

		cache->tx_conf.changed = true;
		cache->tx_conf.ac = ac;
		cache->tx_conf.tx_queue_params = *params;

		return 0;
	}

	ret = ath12k_mac_conf_tx(arvif, ac, params);

	return ret;
}

static struct ieee80211_sta_ht_cap
ath12k_create_ht_cap(struct ath12k *ar, u32 ar_ht_cap, u32 rate_cap_rx_chainmask)
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

static int ath12k_mac_set_txbf_conf(struct ath12k_link_vif *arvif)
{
	u32 value = 0;
	struct ath12k *ar = arvif->ar;
	struct ath12k_vif *ahvif = arvif->ahvif;
	int nsts;
	int sound_dim;
	u32 vht_cap = ar->pdev->cap.vht_cap;
	u32 vdev_param = WMI_VDEV_PARAM_TXBF;

	if (vht_cap & (IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE)) {
		nsts = vht_cap & IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK;
		nsts >>= IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
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
		    ahvif->vdev_type == WMI_VDEV_TYPE_AP)
			value |= WMI_VDEV_PARAM_TXBF_MU_TX_BFER;
	}

	if (vht_cap & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE) {
		value |= WMI_VDEV_PARAM_TXBF_SU_TX_BFEE;

		if ((vht_cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE) &&
		    ahvif->vdev_type == WMI_VDEV_TYPE_STA)
			value |= WMI_VDEV_PARAM_TXBF_MU_TX_BFEE;
	}

	return ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					     vdev_param, value);
}

static void ath12k_set_vht_txbf_cap(struct ath12k *ar, u32 *vht_cap)
{
	bool subfer, subfee;
	int sound_dim = 0;

	subfer = !!(*vht_cap & (IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE));
	subfee = !!(*vht_cap & (IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE));

	if (ar->num_tx_chains < 2) {
		*vht_cap &= ~(IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE);
		subfer = false;
	}

	/* If SU Beaformer is not set, then disable MU Beamformer Capability */
	if (!subfer)
		*vht_cap &= ~(IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE);

	/* If SU Beaformee is not set, then disable MU Beamformee Capability */
	if (!subfee)
		*vht_cap &= ~(IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE);

	sound_dim = u32_get_bits(*vht_cap,
				 IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK);
	*vht_cap = u32_replace_bits(*vht_cap, 0,
				    IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK);

	/* TODO: Need to check invalid STS and Sound_dim values set by FW? */

	/* Enable Sounding Dimension Field only if SU BF is enabled */
	if (subfer) {
		if (sound_dim > (ar->num_tx_chains - 1))
			sound_dim = ar->num_tx_chains - 1;

		*vht_cap = u32_replace_bits(*vht_cap, sound_dim,
					    IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK);
	}

	/* Use the STS advertised by FW unless SU Beamformee is not supported*/
	if (!subfee)
		*vht_cap &= ~(IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK);
}

static struct ieee80211_sta_vht_cap
ath12k_create_vht_cap(struct ath12k *ar, u32 rate_cap_tx_chainmask,
		      u32 rate_cap_rx_chainmask)
{
	struct ieee80211_sta_vht_cap vht_cap = {0};
	u16 txmcs_map, rxmcs_map;
	int i;

	vht_cap.vht_supported = 1;
	vht_cap.cap = ar->pdev->cap.vht_cap;

	ath12k_set_vht_txbf_cap(ar, &vht_cap.cap);

	/* TODO: Enable back VHT160 mode once association issues are fixed */
	/* Disabling VHT160 and VHT80+80 modes */
	vht_cap.cap &= ~IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;
	vht_cap.cap &= ~IEEE80211_VHT_CAP_SHORT_GI_160;

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

static void ath12k_mac_setup_ht_vht_cap(struct ath12k *ar,
					struct ath12k_pdev_cap *cap,
					u32 *ht_cap_info)
{
	struct ieee80211_supported_band *band;
	u32 rate_cap_tx_chainmask;
	u32 rate_cap_rx_chainmask;
	u32 ht_cap;

	rate_cap_tx_chainmask = ar->cfg_tx_chainmask >> cap->tx_chain_mask_shift;
	rate_cap_rx_chainmask = ar->cfg_rx_chainmask >> cap->rx_chain_mask_shift;

	if (cap->supported_bands & WMI_HOST_WLAN_2GHZ_CAP) {
		band = &ar->mac.sbands[NL80211_BAND_2GHZ];
		ht_cap = cap->band[NL80211_BAND_2GHZ].ht_cap_info;
		if (ht_cap_info)
			*ht_cap_info = ht_cap;
		band->ht_cap = ath12k_create_ht_cap(ar, ht_cap,
						    rate_cap_rx_chainmask);
	}

	if (cap->supported_bands & WMI_HOST_WLAN_5GHZ_CAP &&
	    (ar->ab->hw_params->single_pdev_only ||
	     !ar->supports_6ghz)) {
		band = &ar->mac.sbands[NL80211_BAND_5GHZ];
		ht_cap = cap->band[NL80211_BAND_5GHZ].ht_cap_info;
		if (ht_cap_info)
			*ht_cap_info = ht_cap;
		band->ht_cap = ath12k_create_ht_cap(ar, ht_cap,
						    rate_cap_rx_chainmask);
		band->vht_cap = ath12k_create_vht_cap(ar, rate_cap_tx_chainmask,
						      rate_cap_rx_chainmask);
	}
}

static int ath12k_check_chain_mask(struct ath12k *ar, u32 ant, bool is_tx_ant)
{
	/* TODO: Check the request chainmask against the supported
	 * chainmask table which is advertised in extented_service_ready event
	 */

	return 0;
}

static void ath12k_gen_ppe_thresh(struct ath12k_wmi_ppe_threshold_arg *fw_ppet,
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
ath12k_mac_filter_he_cap_mesh(struct ieee80211_he_cap_elem *he_cap_elem)
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

static __le16 ath12k_mac_setup_he_6ghz_cap(struct ath12k_pdev_cap *pcap,
					   struct ath12k_band_cap *bcap)
{
	u8 val;

	bcap->he_6ghz_capa = IEEE80211_HT_MPDU_DENSITY_NONE;
	if (bcap->ht_cap_info & WMI_HT_CAP_DYNAMIC_SMPS)
		bcap->he_6ghz_capa |=
			u32_encode_bits(WLAN_HT_CAP_SM_PS_DYNAMIC,
					IEEE80211_HE_6GHZ_CAP_SM_PS);
	else
		bcap->he_6ghz_capa |=
			u32_encode_bits(WLAN_HT_CAP_SM_PS_DISABLED,
					IEEE80211_HE_6GHZ_CAP_SM_PS);
	val = u32_get_bits(pcap->vht_cap,
			   IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK);
	bcap->he_6ghz_capa |=
		u32_encode_bits(val, IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP);
	val = u32_get_bits(pcap->vht_cap,
			   IEEE80211_VHT_CAP_MAX_MPDU_MASK);
	bcap->he_6ghz_capa |=
		u32_encode_bits(val, IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN);
	if (pcap->vht_cap & IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN)
		bcap->he_6ghz_capa |= IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS;
	if (pcap->vht_cap & IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN)
		bcap->he_6ghz_capa |= IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS;

	return cpu_to_le16(bcap->he_6ghz_capa);
}

static void ath12k_mac_set_hemcsmap(struct ath12k *ar,
				    struct ath12k_pdev_cap *cap,
				    struct ieee80211_sta_he_cap *he_cap)
{
	struct ieee80211_he_mcs_nss_supp *mcs_nss = &he_cap->he_mcs_nss_supp;
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

	mcs_nss->rx_mcs_80 = cpu_to_le16(rxmcs_map & 0xffff);
	mcs_nss->tx_mcs_80 = cpu_to_le16(txmcs_map & 0xffff);
	mcs_nss->rx_mcs_160 = cpu_to_le16(rxmcs_map & 0xffff);
	mcs_nss->tx_mcs_160 = cpu_to_le16(txmcs_map & 0xffff);
	mcs_nss->rx_mcs_80p80 = cpu_to_le16(rxmcs_map & 0xffff);
	mcs_nss->tx_mcs_80p80 = cpu_to_le16(txmcs_map & 0xffff);
}

static void ath12k_mac_copy_he_cap(struct ath12k *ar,
				   struct ath12k_band_cap *band_cap,
				   int iftype, u8 num_tx_chains,
				   struct ieee80211_sta_he_cap *he_cap)
{
	struct ieee80211_he_cap_elem *he_cap_elem = &he_cap->he_cap_elem;

	he_cap->has_he = true;
	memcpy(he_cap_elem->mac_cap_info, band_cap->he_cap_info,
	       sizeof(he_cap_elem->mac_cap_info));
	memcpy(he_cap_elem->phy_cap_info, band_cap->he_cap_phy_info,
	       sizeof(he_cap_elem->phy_cap_info));

	he_cap_elem->mac_cap_info[1] &=
		IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_MASK;
	he_cap_elem->phy_cap_info[0] &=
		IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
		IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
		IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;
	he_cap_elem->phy_cap_info[0] &=
		~IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G;
	he_cap_elem->phy_cap_info[5] &=
		~IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK;
	he_cap_elem->phy_cap_info[5] |= num_tx_chains - 1;

	switch (iftype) {
	case NL80211_IFTYPE_AP:
		he_cap_elem->mac_cap_info[2] &=
			~IEEE80211_HE_MAC_CAP2_BCAST_TWT;
		he_cap_elem->phy_cap_info[3] &=
			~IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK;
		he_cap_elem->phy_cap_info[9] |=
			IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU;
		break;
	case NL80211_IFTYPE_STATION:
		he_cap_elem->mac_cap_info[0] &= ~IEEE80211_HE_MAC_CAP0_TWT_RES;
		he_cap_elem->mac_cap_info[0] |= IEEE80211_HE_MAC_CAP0_TWT_REQ;
		he_cap_elem->phy_cap_info[9] |=
			IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		ath12k_mac_filter_he_cap_mesh(he_cap_elem);
		break;
	}

	ath12k_mac_set_hemcsmap(ar, &ar->pdev->cap, he_cap);
	memset(he_cap->ppe_thres, 0, sizeof(he_cap->ppe_thres));
	if (he_cap_elem->phy_cap_info[6] &
	    IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT)
		ath12k_gen_ppe_thresh(&band_cap->he_ppet, he_cap->ppe_thres);
}

static void
ath12k_mac_copy_eht_mcs_nss(struct ath12k_band_cap *band_cap,
			    struct ieee80211_eht_mcs_nss_supp *mcs_nss,
			    const struct ieee80211_he_cap_elem *he_cap,
			    const struct ieee80211_eht_cap_elem_fixed *eht_cap)
{
	if ((he_cap->phy_cap_info[0] &
	     (IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)) == 0)
		memcpy(&mcs_nss->only_20mhz, &band_cap->eht_mcs_20_only,
		       sizeof(struct ieee80211_eht_mcs_nss_supp_20mhz_only));

	if (he_cap->phy_cap_info[0] &
	    (IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
	     IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G))
		memcpy(&mcs_nss->bw._80, &band_cap->eht_mcs_80,
		       sizeof(struct ieee80211_eht_mcs_nss_supp_bw));

	if (he_cap->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G)
		memcpy(&mcs_nss->bw._160, &band_cap->eht_mcs_160,
		       sizeof(struct ieee80211_eht_mcs_nss_supp_bw));

	if (eht_cap->phy_cap_info[0] & IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ)
		memcpy(&mcs_nss->bw._320, &band_cap->eht_mcs_320,
		       sizeof(struct ieee80211_eht_mcs_nss_supp_bw));
}

static void ath12k_mac_copy_eht_ppe_thresh(struct ath12k_wmi_ppe_threshold_arg *fw_ppet,
					   struct ieee80211_sta_eht_cap *cap)
{
	u16 bit = IEEE80211_EHT_PPE_THRES_INFO_HEADER_SIZE;
	u8 i, nss, ru, ppet_bit_len_per_ru = IEEE80211_EHT_PPE_THRES_INFO_PPET_SIZE * 2;

	u8p_replace_bits(&cap->eht_ppe_thres[0], fw_ppet->numss_m1,
			 IEEE80211_EHT_PPE_THRES_NSS_MASK);

	u16p_replace_bits((u16 *)&cap->eht_ppe_thres[0], fw_ppet->ru_bit_mask,
			  IEEE80211_EHT_PPE_THRES_RU_INDEX_BITMASK_MASK);

	for (nss = 0; nss <= fw_ppet->numss_m1; nss++) {
		for (ru = 0;
		     ru < hweight16(IEEE80211_EHT_PPE_THRES_RU_INDEX_BITMASK_MASK);
		     ru++) {
			u32 val = 0;

			if ((fw_ppet->ru_bit_mask & BIT(ru)) == 0)
				continue;

			u32p_replace_bits(&val, fw_ppet->ppet16_ppet8_ru3_ru0[nss] >>
						(ru * ppet_bit_len_per_ru),
					  GENMASK(ppet_bit_len_per_ru - 1, 0));

			for (i = 0; i < ppet_bit_len_per_ru; i++) {
				cap->eht_ppe_thres[bit / 8] |=
					(((val >> i) & 0x1) << ((bit % 8)));
				bit++;
			}
		}
	}
}

static void
ath12k_mac_filter_eht_cap_mesh(struct ieee80211_eht_cap_elem_fixed
			       *eht_cap_elem)
{
	u8 m;

	m = IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS;
	eht_cap_elem->mac_cap_info[0] &= ~m;

	m = IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO;
	eht_cap_elem->phy_cap_info[0] &= ~m;

	m = IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
	    IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
	    IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
	    IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK;
	eht_cap_elem->phy_cap_info[3] &= ~m;

	m = IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO |
	    IEEE80211_EHT_PHY_CAP4_PSR_SR_SUPP |
	    IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP |
	    IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI;
	eht_cap_elem->phy_cap_info[4] &= ~m;

	m = IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK |
	    IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP |
	    IEEE80211_EHT_PHY_CAP5_RX_LESS_242_TONE_RU_SUPP |
	    IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK;
	eht_cap_elem->phy_cap_info[5] &= ~m;

	m = IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK;
	eht_cap_elem->phy_cap_info[6] &= ~m;

	m = IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ |
	    IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ |
	    IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_320MHZ |
	    IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ |
	    IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ |
	    IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_320MHZ;
	eht_cap_elem->phy_cap_info[7] &= ~m;
}

static void ath12k_mac_copy_eht_cap(struct ath12k *ar,
				    struct ath12k_band_cap *band_cap,
				    struct ieee80211_he_cap_elem *he_cap_elem,
				    int iftype,
				    struct ieee80211_sta_eht_cap *eht_cap)
{
	struct ieee80211_eht_cap_elem_fixed *eht_cap_elem = &eht_cap->eht_cap_elem;

	memset(eht_cap, 0, sizeof(struct ieee80211_sta_eht_cap));

	if (!(test_bit(WMI_TLV_SERVICE_11BE, ar->ab->wmi_ab.svc_map)) ||
	    ath12k_acpi_get_disable_11be(ar->ab))
		return;

	eht_cap->has_eht = true;
	memcpy(eht_cap_elem->mac_cap_info, band_cap->eht_cap_mac_info,
	       sizeof(eht_cap_elem->mac_cap_info));
	memcpy(eht_cap_elem->phy_cap_info, band_cap->eht_cap_phy_info,
	       sizeof(eht_cap_elem->phy_cap_info));

	switch (iftype) {
	case NL80211_IFTYPE_AP:
		eht_cap_elem->phy_cap_info[0] &=
			~IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ;
		eht_cap_elem->phy_cap_info[4] &=
			~IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO;
		eht_cap_elem->phy_cap_info[5] &=
			~IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP;
		break;
	case NL80211_IFTYPE_STATION:
		eht_cap_elem->phy_cap_info[7] &=
			~(IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ |
			  IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ |
			  IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_320MHZ);
		eht_cap_elem->phy_cap_info[7] &=
			~(IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ |
			  IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ |
			  IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_320MHZ);
		break;
	case NL80211_IFTYPE_MESH_POINT:
		ath12k_mac_filter_eht_cap_mesh(eht_cap_elem);
		break;
	default:
		break;
	}

	ath12k_mac_copy_eht_mcs_nss(band_cap, &eht_cap->eht_mcs_nss_supp,
				    he_cap_elem, eht_cap_elem);

	if (eht_cap_elem->phy_cap_info[5] &
	    IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT)
		ath12k_mac_copy_eht_ppe_thresh(&band_cap->eht_ppet, eht_cap);
}

static int ath12k_mac_copy_sband_iftype_data(struct ath12k *ar,
					     struct ath12k_pdev_cap *cap,
					     struct ieee80211_sband_iftype_data *data,
					     int band)
{
	struct ath12k_band_cap *band_cap = &cap->band[band];
	int i, idx = 0;

	for (i = 0; i < NUM_NL80211_IFTYPES; i++) {
		struct ieee80211_sta_he_cap *he_cap = &data[idx].he_cap;

		switch (i) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_MESH_POINT:
			break;

		default:
			continue;
		}

		data[idx].types_mask = BIT(i);

		ath12k_mac_copy_he_cap(ar, band_cap, i, ar->num_tx_chains, he_cap);
		if (band == NL80211_BAND_6GHZ) {
			data[idx].he_6ghz_capa.capa =
				ath12k_mac_setup_he_6ghz_cap(cap, band_cap);
		}
		ath12k_mac_copy_eht_cap(ar, band_cap, &he_cap->he_cap_elem, i,
					&data[idx].eht_cap);
		idx++;
	}

	return idx;
}

static void ath12k_mac_setup_sband_iftype_data(struct ath12k *ar,
					       struct ath12k_pdev_cap *cap)
{
	struct ieee80211_supported_band *sband;
	enum nl80211_band band;
	int count;

	if (cap->supported_bands & WMI_HOST_WLAN_2GHZ_CAP) {
		band = NL80211_BAND_2GHZ;
		count = ath12k_mac_copy_sband_iftype_data(ar, cap,
							  ar->mac.iftype[band],
							  band);
		sband = &ar->mac.sbands[band];
		_ieee80211_set_sband_iftype_data(sband, ar->mac.iftype[band],
						 count);
	}

	if (cap->supported_bands & WMI_HOST_WLAN_5GHZ_CAP) {
		band = NL80211_BAND_5GHZ;
		count = ath12k_mac_copy_sband_iftype_data(ar, cap,
							  ar->mac.iftype[band],
							  band);
		sband = &ar->mac.sbands[band];
		_ieee80211_set_sband_iftype_data(sband, ar->mac.iftype[band],
						 count);
	}

	if (cap->supported_bands & WMI_HOST_WLAN_5GHZ_CAP &&
	    ar->supports_6ghz) {
		band = NL80211_BAND_6GHZ;
		count = ath12k_mac_copy_sband_iftype_data(ar, cap,
							  ar->mac.iftype[band],
							  band);
		sband = &ar->mac.sbands[band];
		_ieee80211_set_sband_iftype_data(sband, ar->mac.iftype[band],
						 count);
	}
}

static int __ath12k_set_antenna(struct ath12k *ar, u32 tx_ant, u32 rx_ant)
{
	struct ath12k_hw *ah = ath12k_ar_to_ah(ar);
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (ath12k_check_chain_mask(ar, tx_ant, true))
		return -EINVAL;

	if (ath12k_check_chain_mask(ar, rx_ant, false))
		return -EINVAL;

	/* Since we advertised the max cap of all radios combined during wiphy
	 * registration, ensure we don't set the antenna config higher than the
	 * limits
	 */
	tx_ant = min_t(u32, tx_ant, ar->pdev->cap.tx_chain_mask);
	rx_ant = min_t(u32, rx_ant, ar->pdev->cap.rx_chain_mask);

	ar->cfg_tx_chainmask = tx_ant;
	ar->cfg_rx_chainmask = rx_ant;

	if (ah->state != ATH12K_HW_STATE_ON &&
	    ah->state != ATH12K_HW_STATE_RESTARTED)
		return 0;

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_TX_CHAIN_MASK,
					tx_ant, ar->pdev->pdev_id);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set tx-chainmask: %d, req 0x%x\n",
			    ret, tx_ant);
		return ret;
	}

	ar->num_tx_chains = hweight32(tx_ant);

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_RX_CHAIN_MASK,
					rx_ant, ar->pdev->pdev_id);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set rx-chainmask: %d, req 0x%x\n",
			    ret, rx_ant);
		return ret;
	}

	ar->num_rx_chains = hweight32(rx_ant);

	/* Reload HT/VHT/HE capability */
	ath12k_mac_setup_ht_vht_cap(ar, &ar->pdev->cap, NULL);
	ath12k_mac_setup_sband_iftype_data(ar, &ar->pdev->cap);

	return 0;
}

static void ath12k_mgmt_over_wmi_tx_drop(struct ath12k *ar, struct sk_buff *skb)
{
	int num_mgmt;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ieee80211_free_txskb(ath12k_ar_to_hw(ar), skb);

	num_mgmt = atomic_dec_if_positive(&ar->num_pending_mgmt_tx);

	if (num_mgmt < 0)
		WARN_ON_ONCE(1);

	if (!num_mgmt)
		wake_up(&ar->txmgmt_empty_waitq);
}

int ath12k_mac_tx_mgmt_pending_free(int buf_id, void *skb, void *ctx)
{
	struct sk_buff *msdu = skb;
	struct ieee80211_tx_info *info;
	struct ath12k *ar = ctx;
	struct ath12k_base *ab = ar->ab;

	spin_lock_bh(&ar->txmgmt_idr_lock);
	idr_remove(&ar->txmgmt_idr, buf_id);
	spin_unlock_bh(&ar->txmgmt_idr_lock);
	dma_unmap_single(ab->dev, ATH12K_SKB_CB(msdu)->paddr, msdu->len,
			 DMA_TO_DEVICE);

	info = IEEE80211_SKB_CB(msdu);
	memset(&info->status, 0, sizeof(info->status));

	ath12k_mgmt_over_wmi_tx_drop(ar, skb);

	return 0;
}

static int ath12k_mac_vif_txmgmt_idr_remove(int buf_id, void *skb, void *ctx)
{
	struct ieee80211_vif *vif = ctx;
	struct ath12k_skb_cb *skb_cb = ATH12K_SKB_CB(skb);
	struct sk_buff *msdu = skb;
	struct ath12k *ar = skb_cb->ar;
	struct ath12k_base *ab = ar->ab;

	if (skb_cb->vif == vif) {
		spin_lock_bh(&ar->txmgmt_idr_lock);
		idr_remove(&ar->txmgmt_idr, buf_id);
		spin_unlock_bh(&ar->txmgmt_idr_lock);
		dma_unmap_single(ab->dev, skb_cb->paddr, msdu->len,
				 DMA_TO_DEVICE);
	}

	return 0;
}

static int ath12k_mac_mgmt_tx_wmi(struct ath12k *ar, struct ath12k_link_vif *arvif,
				  struct sk_buff *skb)
{
	struct ath12k_base *ab = ar->ab;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ath12k_skb_cb *skb_cb = ATH12K_SKB_CB(skb);
	struct ieee80211_tx_info *info;
	enum hal_encrypt_type enctype;
	unsigned int mic_len;
	dma_addr_t paddr;
	int buf_id;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	skb_cb->ar = ar;
	spin_lock_bh(&ar->txmgmt_idr_lock);
	buf_id = idr_alloc(&ar->txmgmt_idr, skb, 0,
			   ATH12K_TX_MGMT_NUM_PENDING_MAX, GFP_ATOMIC);
	spin_unlock_bh(&ar->txmgmt_idr_lock);
	if (buf_id < 0)
		return -ENOSPC;

	info = IEEE80211_SKB_CB(skb);
	if ((skb_cb->flags & ATH12K_SKB_CIPHER_SET) &&
	    !(info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP)) {
		if ((ieee80211_is_action(hdr->frame_control) ||
		     ieee80211_is_deauth(hdr->frame_control) ||
		     ieee80211_is_disassoc(hdr->frame_control)) &&
		     ieee80211_has_protected(hdr->frame_control)) {
			enctype = ath12k_dp_tx_get_encrypt_type(skb_cb->cipher);
			mic_len = ath12k_dp_rx_crypto_mic_len(ar, enctype);
			skb_put(skb, mic_len);
		}
	}

	paddr = dma_map_single(ab->dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(ab->dev, paddr)) {
		ath12k_warn(ab, "failed to DMA map mgmt Tx buffer\n");
		ret = -EIO;
		goto err_free_idr;
	}

	skb_cb->paddr = paddr;

	ret = ath12k_wmi_mgmt_send(ar, arvif->vdev_id, buf_id, skb);
	if (ret) {
		ath12k_warn(ar->ab, "failed to send mgmt frame: %d\n", ret);
		goto err_unmap_buf;
	}

	return 0;

err_unmap_buf:
	dma_unmap_single(ab->dev, skb_cb->paddr,
			 skb->len, DMA_TO_DEVICE);
err_free_idr:
	spin_lock_bh(&ar->txmgmt_idr_lock);
	idr_remove(&ar->txmgmt_idr, buf_id);
	spin_unlock_bh(&ar->txmgmt_idr_lock);

	return ret;
}

static void ath12k_mgmt_over_wmi_tx_purge(struct ath12k *ar)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&ar->wmi_mgmt_tx_queue)) != NULL)
		ath12k_mgmt_over_wmi_tx_drop(ar, skb);
}

static void ath12k_mgmt_over_wmi_tx_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct ath12k *ar = container_of(work, struct ath12k, wmi_mgmt_tx_work);
	struct ath12k_hw *ah = ar->ah;
	struct ath12k_skb_cb *skb_cb;
	struct ath12k_vif *ahvif;
	struct ath12k_link_vif *arvif;
	struct sk_buff *skb;
	int ret;

	lockdep_assert_wiphy(wiphy);

	while ((skb = skb_dequeue(&ar->wmi_mgmt_tx_queue)) != NULL) {
		skb_cb = ATH12K_SKB_CB(skb);
		if (!skb_cb->vif) {
			ath12k_warn(ar->ab, "no vif found for mgmt frame\n");
			ath12k_mgmt_over_wmi_tx_drop(ar, skb);
			continue;
		}

		ahvif = ath12k_vif_to_ahvif(skb_cb->vif);
		if (!(ahvif->links_map & BIT(skb_cb->link_id))) {
			ath12k_warn(ar->ab,
				    "invalid linkid %u in mgmt over wmi tx with linkmap 0x%x\n",
				    skb_cb->link_id, ahvif->links_map);
			ath12k_mgmt_over_wmi_tx_drop(ar, skb);
			continue;
		}

		arvif = wiphy_dereference(ah->hw->wiphy, ahvif->link[skb_cb->link_id]);
		if (ar->allocated_vdev_map & (1LL << arvif->vdev_id)) {
			ret = ath12k_mac_mgmt_tx_wmi(ar, arvif, skb);
			if (ret) {
				ath12k_warn(ar->ab, "failed to tx mgmt frame, vdev_id %d :%d\n",
					    arvif->vdev_id, ret);
				ath12k_mgmt_over_wmi_tx_drop(ar, skb);
			}
		} else {
			ath12k_warn(ar->ab,
				    "dropping mgmt frame for vdev %d link %u is_started %d\n",
				    arvif->vdev_id,
				    skb_cb->link_id,
				    arvif->is_started);
			ath12k_mgmt_over_wmi_tx_drop(ar, skb);
		}
	}
}

static int ath12k_mac_mgmt_tx(struct ath12k *ar, struct sk_buff *skb,
			      bool is_prb_rsp)
{
	struct sk_buff_head *q = &ar->wmi_mgmt_tx_queue;

	if (test_bit(ATH12K_FLAG_CRASH_FLUSH, &ar->ab->dev_flags))
		return -ESHUTDOWN;

	/* Drop probe response packets when the pending management tx
	 * count has reached a certain threshold, so as to prioritize
	 * other mgmt packets like auth and assoc to be sent on time
	 * for establishing successful connections.
	 */
	if (is_prb_rsp &&
	    atomic_read(&ar->num_pending_mgmt_tx) > ATH12K_PRB_RSP_DROP_THRESHOLD) {
		ath12k_warn(ar->ab,
			    "dropping probe response as pending queue is almost full\n");
		return -ENOSPC;
	}

	if (skb_queue_len_lockless(q) >= ATH12K_TX_MGMT_NUM_PENDING_MAX) {
		ath12k_warn(ar->ab, "mgmt tx queue is full\n");
		return -ENOSPC;
	}

	skb_queue_tail(q, skb);
	atomic_inc(&ar->num_pending_mgmt_tx);
	wiphy_work_queue(ath12k_ar_to_hw(ar)->wiphy, &ar->wmi_mgmt_tx_work);

	return 0;
}

static void ath12k_mac_add_p2p_noa_ie(struct ath12k *ar,
				      struct ieee80211_vif *vif,
				      struct sk_buff *skb,
				      bool is_prb_rsp)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);

	if (likely(!is_prb_rsp))
		return;

	spin_lock_bh(&ar->data_lock);

	if (ahvif->u.ap.noa_data &&
	    !pskb_expand_head(skb, 0, ahvif->u.ap.noa_len,
			      GFP_ATOMIC))
		skb_put_data(skb, ahvif->u.ap.noa_data,
			     ahvif->u.ap.noa_len);

	spin_unlock_bh(&ar->data_lock);
}

/* Note: called under rcu_read_lock() */
static void ath12k_mlo_mcast_update_tx_link_address(struct ieee80211_vif *vif,
						    u8 link_id, struct sk_buff *skb,
						    u32 info_flags)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_bss_conf *bss_conf;

	if (info_flags & IEEE80211_TX_CTL_HW_80211_ENCAP)
		return;

	bss_conf = rcu_dereference(vif->link_conf[link_id]);
	if (bss_conf)
		ether_addr_copy(hdr->addr2, bss_conf->addr);
}

/* Note: called under rcu_read_lock() */
static u8 ath12k_mac_get_tx_link(struct ieee80211_sta *sta, struct ieee80211_vif *vif,
				 u8 link, struct sk_buff *skb, u32 info_flags)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ieee80211_link_sta *link_sta;
	struct ieee80211_bss_conf *bss_conf;
	struct ath12k_sta *ahsta;

	/* Use the link id passed or the default vif link */
	if (!sta) {
		if (link != IEEE80211_LINK_UNSPECIFIED)
			return link;

		return ahvif->deflink.link_id;
	}

	ahsta = ath12k_sta_to_ahsta(sta);

	/* Below translation ensures we pass proper A2 & A3 for non ML clients.
	 * Also it assumes for now support only for MLO AP in this path
	 */
	if (!sta->mlo) {
		link = ahsta->deflink.link_id;

		if (info_flags & IEEE80211_TX_CTL_HW_80211_ENCAP)
			return link;

		bss_conf = rcu_dereference(vif->link_conf[link]);
		if (bss_conf) {
			ether_addr_copy(hdr->addr2, bss_conf->addr);
			if (!ieee80211_has_tods(hdr->frame_control) &&
			    !ieee80211_has_fromds(hdr->frame_control))
				ether_addr_copy(hdr->addr3, bss_conf->addr);
		}

		return link;
	}

	/* enqueue eth enacap & data frames on primary link, FW does link
	 * selection and address translation.
	 */
	if (info_flags & IEEE80211_TX_CTL_HW_80211_ENCAP ||
	    ieee80211_is_data(hdr->frame_control))
		return ahsta->assoc_link_id;

	/* 802.11 frame cases */
	if (link == IEEE80211_LINK_UNSPECIFIED)
		link = ahsta->deflink.link_id;

	if (!ieee80211_is_mgmt(hdr->frame_control))
		return link;

	/* Perform address conversion for ML STA Tx */
	bss_conf = rcu_dereference(vif->link_conf[link]);
	link_sta = rcu_dereference(sta->link[link]);

	if (bss_conf && link_sta) {
		ether_addr_copy(hdr->addr1, link_sta->addr);
		ether_addr_copy(hdr->addr2, bss_conf->addr);

		if (vif->type == NL80211_IFTYPE_STATION && bss_conf->bssid)
			ether_addr_copy(hdr->addr3, bss_conf->bssid);
		else if (vif->type == NL80211_IFTYPE_AP)
			ether_addr_copy(hdr->addr3, bss_conf->addr);

		return link;
	}

	if (bss_conf) {
		/* In certain cases where a ML sta associated and added subset of
		 * links on which the ML AP is active, but now sends some frame
		 * (ex. Probe request) on a different link which is active in our
		 * MLD but was not added during previous association, we can
		 * still honor the Tx to that ML STA via the requested link.
		 * The control would reach here in such case only when that link
		 * address is same as the MLD address or in worst case clients
		 * used MLD address at TA wrongly which would have helped
		 * identify the ML sta object and pass it here.
		 * If the link address of that STA is different from MLD address,
		 * then the sta object would be NULL and control won't reach
		 * here but return at the start of the function itself with !sta
		 * check. Also this would not need any translation at hdr->addr1
		 * from MLD to link address since the RA is the MLD address
		 * (same as that link address ideally) already.
		 */
		ether_addr_copy(hdr->addr2, bss_conf->addr);

		if (vif->type == NL80211_IFTYPE_STATION && bss_conf->bssid)
			ether_addr_copy(hdr->addr3, bss_conf->bssid);
		else if (vif->type == NL80211_IFTYPE_AP)
			ether_addr_copy(hdr->addr3, bss_conf->addr);
	}

	return link;
}

/* Note: called under rcu_read_lock() */
static void ath12k_mac_op_tx(struct ieee80211_hw *hw,
			     struct ieee80211_tx_control *control,
			     struct sk_buff *skb)
{
	struct ath12k_skb_cb *skb_cb = ATH12K_SKB_CB(skb);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif = &ahvif->deflink;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ieee80211_sta *sta = control->sta;
	struct ath12k_link_vif *tmp_arvif;
	u32 info_flags = info->flags;
	struct sk_buff *msdu_copied;
	struct ath12k *ar, *tmp_ar;
	struct ath12k_peer *peer;
	unsigned long links_map;
	bool is_mcast = false;
	bool is_dvlan = false;
	struct ethhdr *eth;
	bool is_prb_rsp;
	u16 mcbc_gsn;
	u8 link_id;
	int ret;

	if (ahvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ieee80211_free_txskb(hw, skb);
		return;
	}

	link_id = u32_get_bits(info->control.flags, IEEE80211_TX_CTRL_MLO_LINK);
	memset(skb_cb, 0, sizeof(*skb_cb));
	skb_cb->vif = vif;

	if (key) {
		skb_cb->cipher = key->cipher;
		skb_cb->flags |= ATH12K_SKB_CIPHER_SET;
	}

	/* handle only for MLO case, use deflink for non MLO case */
	if (ieee80211_vif_is_mld(vif)) {
		link_id = ath12k_mac_get_tx_link(sta, vif, link_id, skb, info_flags);
		if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS) {
			ieee80211_free_txskb(hw, skb);
			return;
		}
	} else {
		link_id = 0;
	}

	arvif = rcu_dereference(ahvif->link[link_id]);
	if (!arvif || !arvif->ar) {
		ath12k_warn(ahvif->ah, "failed to find arvif link id %u for frame transmission",
			    link_id);
		ieee80211_free_txskb(hw, skb);
		return;
	}

	ar = arvif->ar;
	skb_cb->link_id = link_id;
	is_prb_rsp = ieee80211_is_probe_resp(hdr->frame_control);

	if (info_flags & IEEE80211_TX_CTL_HW_80211_ENCAP) {
		eth = (struct ethhdr *)skb->data;
		is_mcast = is_multicast_ether_addr(eth->h_dest);

		skb_cb->flags |= ATH12K_SKB_HW_80211_ENCAP;
	} else if (ieee80211_is_mgmt(hdr->frame_control)) {
		ret = ath12k_mac_mgmt_tx(ar, skb, is_prb_rsp);
		if (ret) {
			ath12k_warn(ar->ab, "failed to queue management frame %d\n",
				    ret);
			ieee80211_free_txskb(hw, skb);
		}
		return;
	}

	if (!(info_flags & IEEE80211_TX_CTL_HW_80211_ENCAP))
		is_mcast = is_multicast_ether_addr(hdr->addr1);

	/* This is case only for P2P_GO */
	if (vif->type == NL80211_IFTYPE_AP && vif->p2p)
		ath12k_mac_add_p2p_noa_ie(ar, vif, skb, is_prb_rsp);

	/* Checking if it is a DVLAN frame */
	if (!test_bit(ATH12K_FLAG_HW_CRYPTO_DISABLED, &ar->ab->dev_flags) &&
	    !(skb_cb->flags & ATH12K_SKB_HW_80211_ENCAP) &&
	    !(skb_cb->flags & ATH12K_SKB_CIPHER_SET) &&
	    ieee80211_has_protected(hdr->frame_control))
		is_dvlan = true;

	if (!vif->valid_links || !is_mcast || is_dvlan ||
	    test_bit(ATH12K_FLAG_RAW_MODE, &ar->ab->dev_flags)) {
		ret = ath12k_dp_tx(ar, arvif, skb, false, 0, is_mcast);
		if (unlikely(ret)) {
			ath12k_warn(ar->ab, "failed to transmit frame %d\n", ret);
			ieee80211_free_txskb(ar->ah->hw, skb);
			return;
		}
	} else {
		mcbc_gsn = atomic_inc_return(&ahvif->mcbc_gsn) & 0xfff;

		links_map = ahvif->links_map;
		for_each_set_bit(link_id, &links_map,
				 IEEE80211_MLD_MAX_NUM_LINKS) {
			tmp_arvif = rcu_dereference(ahvif->link[link_id]);
			if (!tmp_arvif || !tmp_arvif->is_up)
				continue;

			tmp_ar = tmp_arvif->ar;
			msdu_copied = skb_copy(skb, GFP_ATOMIC);
			if (!msdu_copied) {
				ath12k_err(ar->ab,
					   "skb copy failure link_id 0x%X vdevid 0x%X\n",
					   link_id, tmp_arvif->vdev_id);
				continue;
			}

			ath12k_mlo_mcast_update_tx_link_address(vif, link_id,
								msdu_copied,
								info_flags);

			skb_cb = ATH12K_SKB_CB(msdu_copied);
			skb_cb->link_id = link_id;

			/* For open mode, skip peer find logic */
			if (unlikely(!ahvif->key_cipher))
				goto skip_peer_find;

			spin_lock_bh(&tmp_ar->ab->base_lock);
			peer = ath12k_peer_find_by_addr(tmp_ar->ab, tmp_arvif->bssid);
			if (!peer) {
				spin_unlock_bh(&tmp_ar->ab->base_lock);
				ath12k_warn(tmp_ar->ab,
					    "failed to find peer for vdev_id 0x%X addr %pM link_map 0x%X\n",
					    tmp_arvif->vdev_id, tmp_arvif->bssid,
					    ahvif->links_map);
				dev_kfree_skb_any(msdu_copied);
				continue;
			}

			key = peer->keys[peer->mcast_keyidx];
			if (key) {
				skb_cb->cipher = key->cipher;
				skb_cb->flags |= ATH12K_SKB_CIPHER_SET;

				hdr = (struct ieee80211_hdr *)msdu_copied->data;
				if (!ieee80211_has_protected(hdr->frame_control))
					hdr->frame_control |=
						cpu_to_le16(IEEE80211_FCTL_PROTECTED);
			}
			spin_unlock_bh(&tmp_ar->ab->base_lock);

skip_peer_find:
			ret = ath12k_dp_tx(tmp_ar, tmp_arvif,
					   msdu_copied, true, mcbc_gsn, is_mcast);
			if (unlikely(ret)) {
				if (ret == -ENOMEM) {
					/* Drops are expected during heavy multicast
					 * frame flood. Print with debug log
					 * level to avoid lot of console prints
					 */
					ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
						   "failed to transmit frame %d\n",
						   ret);
				} else {
					ath12k_warn(ar->ab,
						    "failed to transmit frame %d\n",
						    ret);
				}

				dev_kfree_skb_any(msdu_copied);
			}
		}
		ieee80211_free_txskb(ar->ah->hw, skb);
	}
}

void ath12k_mac_drain_tx(struct ath12k *ar)
{
	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	/* make sure rcu-protected mac80211 tx path itself is drained */
	synchronize_net();

	wiphy_work_cancel(ath12k_ar_to_hw(ar)->wiphy, &ar->wmi_mgmt_tx_work);
	ath12k_mgmt_over_wmi_tx_purge(ar);
}

static int ath12k_mac_config_mon_status_default(struct ath12k *ar, bool enable)
{
	struct htt_rx_ring_tlv_filter tlv_filter = {};
	struct ath12k_base *ab = ar->ab;
	u32 ring_id, i;
	int ret = 0;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (!ab->hw_params->rxdma1_enable)
		return ret;

	if (enable) {
		tlv_filter = ath12k_mac_mon_status_filter_default;

		if (ath12k_debugfs_rx_filter(ar))
			tlv_filter.rx_filter = ath12k_debugfs_rx_filter(ar);
	} else {
		tlv_filter.rxmon_disable = true;
	}

	for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
		ring_id = ar->dp.rxdma_mon_dst_ring[i].ring_id;
		ret = ath12k_dp_tx_htt_rx_filter_setup(ab, ring_id,
						       ar->dp.mac_id + i,
						       HAL_RXDMA_MONITOR_DST,
						       DP_RXDMA_REFILL_RING_SIZE,
						       &tlv_filter);
		if (ret) {
			ath12k_err(ab,
				   "failed to setup filter for monitor buf %d\n",
				   ret);
		}
	}

	return ret;
}

static int ath12k_mac_start(struct ath12k *ar)
{
	struct ath12k_hw *ah = ar->ah;
	struct ath12k_base *ab = ar->ab;
	struct ath12k_pdev *pdev = ar->pdev;
	int ret;

	lockdep_assert_held(&ah->hw_mutex);
	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_PMF_QOS,
					1, pdev->pdev_id);

	if (ret) {
		ath12k_err(ab, "failed to enable PMF QOS: %d\n", ret);
		goto err;
	}

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_DYNAMIC_BW, 1,
					pdev->pdev_id);
	if (ret) {
		ath12k_err(ab, "failed to enable dynamic bw: %d\n", ret);
		goto err;
	}

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ARP_AC_OVERRIDE,
					0, pdev->pdev_id);
	if (ret) {
		ath12k_err(ab, "failed to set ac override for ARP: %d\n",
			   ret);
		goto err;
	}

	ret = ath12k_wmi_send_dfs_phyerr_offload_enable_cmd(ar, pdev->pdev_id);
	if (ret) {
		ath12k_err(ab, "failed to offload radar detection: %d\n",
			   ret);
		goto err;
	}

	ret = ath12k_dp_tx_htt_h2t_ppdu_stats_req(ar,
						  HTT_PPDU_STATS_TAG_DEFAULT);
	if (ret) {
		ath12k_err(ab, "failed to req ppdu stats: %d\n", ret);
		goto err;
	}

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_MESH_MCAST_ENABLE,
					1, pdev->pdev_id);

	if (ret) {
		ath12k_err(ab, "failed to enable MESH MCAST ENABLE: (%d\n", ret);
		goto err;
	}

	__ath12k_set_antenna(ar, ar->cfg_tx_chainmask, ar->cfg_rx_chainmask);

	/* TODO: Do we need to enable ANI? */

	ret = ath12k_reg_update_chan_list(ar, false);

	/* The ar state alone can be turned off for non supported country
	 * without returning the error value. As we need to update the channel
	 * for the next ar.
	 */
	if (ret) {
		if (ret == -EINVAL)
			ret = 0;
		goto err;
	}

	ar->num_started_vdevs = 0;
	ar->num_created_vdevs = 0;
	ar->num_peers = 0;
	ar->allocated_vdev_map = 0;
	ar->chan_tx_pwr = ATH12K_PDEV_TX_POWER_INVALID;

	/* Configure monitor status ring with default rx_filter to get rx status
	 * such as rssi, rx_duration.
	 */
	ret = ath12k_mac_config_mon_status_default(ar, true);
	if (ret && (ret != -EOPNOTSUPP)) {
		ath12k_err(ab, "failed to configure monitor status ring with default rx_filter: (%d)\n",
			   ret);
		goto err;
	}

	if (ret == -EOPNOTSUPP)
		ath12k_dbg(ab, ATH12K_DBG_MAC,
			   "monitor status config is not yet supported");

	/* Configure the hash seed for hash based reo dest ring selection */
	ath12k_wmi_pdev_lro_cfg(ar, ar->pdev->pdev_id);

	/* allow device to enter IMPS */
	if (ab->hw_params->idle_ps) {
		ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_IDLE_PS_CONFIG,
						1, pdev->pdev_id);
		if (ret) {
			ath12k_err(ab, "failed to enable idle ps: %d\n", ret);
			goto err;
		}
	}

	rcu_assign_pointer(ab->pdevs_active[ar->pdev_idx],
			   &ab->pdevs[ar->pdev_idx]);

	return 0;
err:

	return ret;
}

static void ath12k_drain_tx(struct ath12k_hw *ah)
{
	struct ath12k *ar = ah->radio;
	int i;

	if (ath12k_ftm_mode) {
		ath12k_err(ar->ab, "fail to start mac operations in ftm mode\n");
		return;
	}

	lockdep_assert_wiphy(ah->hw->wiphy);

	for_each_ar(ah, ar, i)
		ath12k_mac_drain_tx(ar);
}

static int ath12k_mac_op_start(struct ieee80211_hw *hw)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar;
	int ret, i;

	lockdep_assert_wiphy(hw->wiphy);

	ath12k_drain_tx(ah);

	guard(mutex)(&ah->hw_mutex);

	switch (ah->state) {
	case ATH12K_HW_STATE_OFF:
		ah->state = ATH12K_HW_STATE_ON;
		break;
	case ATH12K_HW_STATE_RESTARTING:
		ah->state = ATH12K_HW_STATE_RESTARTED;
		break;
	case ATH12K_HW_STATE_RESTARTED:
	case ATH12K_HW_STATE_WEDGED:
	case ATH12K_HW_STATE_ON:
	case ATH12K_HW_STATE_TM:
		ah->state = ATH12K_HW_STATE_OFF;

		WARN_ON(1);
		return -EINVAL;
	}

	for_each_ar(ah, ar, i) {
		ret = ath12k_mac_start(ar);
		if (ret) {
			ah->state = ATH12K_HW_STATE_OFF;

			ath12k_err(ar->ab, "fail to start mac operations in pdev idx %d ret %d\n",
				   ar->pdev_idx, ret);
			goto fail_start;
		}
	}

	return 0;

fail_start:
	for (; i > 0; i--) {
		ar = ath12k_ah_to_ar(ah, i - 1);
		ath12k_mac_stop(ar);
	}

	return ret;
}

int ath12k_mac_rfkill_config(struct ath12k *ar)
{
	struct ath12k_base *ab = ar->ab;
	u32 param;
	int ret;

	if (ab->hw_params->rfkill_pin == 0)
		return -EOPNOTSUPP;

	ath12k_dbg(ab, ATH12K_DBG_MAC,
		   "mac rfkill_pin %d rfkill_cfg %d rfkill_on_level %d",
		   ab->hw_params->rfkill_pin, ab->hw_params->rfkill_cfg,
		   ab->hw_params->rfkill_on_level);

	param = u32_encode_bits(ab->hw_params->rfkill_on_level,
				WMI_RFKILL_CFG_RADIO_LEVEL) |
		u32_encode_bits(ab->hw_params->rfkill_pin,
				WMI_RFKILL_CFG_GPIO_PIN_NUM) |
		u32_encode_bits(ab->hw_params->rfkill_cfg,
				WMI_RFKILL_CFG_PIN_AS_GPIO);

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_HW_RFKILL_CONFIG,
					param, ar->pdev->pdev_id);
	if (ret) {
		ath12k_warn(ab,
			    "failed to set rfkill config 0x%x: %d\n",
			    param, ret);
		return ret;
	}

	return 0;
}

int ath12k_mac_rfkill_enable_radio(struct ath12k *ar, bool enable)
{
	enum wmi_rfkill_enable_radio param;
	int ret;

	if (enable)
		param = WMI_RFKILL_ENABLE_RADIO_ON;
	else
		param = WMI_RFKILL_ENABLE_RADIO_OFF;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac %d rfkill enable %d",
		   ar->pdev_idx, param);

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_RFKILL_ENABLE,
					param, ar->pdev->pdev_id);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set rfkill enable param %d: %d\n",
			    param, ret);
		return ret;
	}

	return 0;
}

static void ath12k_mac_stop(struct ath12k *ar)
{
	struct ath12k_hw *ah = ar->ah;
	struct htt_ppdu_stats_info *ppdu_stats, *tmp;
	struct ath12k_wmi_scan_chan_list_arg *arg;
	int ret;

	lockdep_assert_held(&ah->hw_mutex);
	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ret = ath12k_mac_config_mon_status_default(ar, false);
	if (ret && (ret != -EOPNOTSUPP))
		ath12k_err(ar->ab, "failed to clear rx_filter for monitor status ring: (%d)\n",
			   ret);

	clear_bit(ATH12K_FLAG_CAC_RUNNING, &ar->dev_flags);

	cancel_delayed_work_sync(&ar->scan.timeout);
	wiphy_work_cancel(ath12k_ar_to_hw(ar)->wiphy, &ar->scan.vdev_clean_wk);
	cancel_work_sync(&ar->regd_channel_update_work);
	cancel_work_sync(&ar->regd_update_work);
	cancel_work_sync(&ar->ab->rfkill_work);
	cancel_work_sync(&ar->ab->update_11d_work);
	ar->state_11d = ATH12K_11D_IDLE;
	complete(&ar->completed_11d_scan);

	spin_lock_bh(&ar->data_lock);

	list_for_each_entry_safe(ppdu_stats, tmp, &ar->ppdu_stats_info, list) {
		list_del(&ppdu_stats->list);
		kfree(ppdu_stats);
	}

	while ((arg = list_first_entry_or_null(&ar->regd_channel_update_queue,
					       struct ath12k_wmi_scan_chan_list_arg,
					       list))) {
		list_del(&arg->list);
		kfree(arg);
	}
	spin_unlock_bh(&ar->data_lock);

	rcu_assign_pointer(ar->ab->pdevs_active[ar->pdev_idx], NULL);

	synchronize_rcu();

	atomic_set(&ar->num_pending_mgmt_tx, 0);
}

static void ath12k_mac_op_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar;
	int i;

	lockdep_assert_wiphy(hw->wiphy);

	ath12k_drain_tx(ah);

	mutex_lock(&ah->hw_mutex);

	ah->state = ATH12K_HW_STATE_OFF;

	for_each_ar(ah, ar, i)
		ath12k_mac_stop(ar);

	mutex_unlock(&ah->hw_mutex);
}

static u8
ath12k_mac_get_vdev_stats_id(struct ath12k_link_vif *arvif)
{
	struct ath12k_base *ab = arvif->ar->ab;
	u8 vdev_stats_id = 0;

	do {
		if (ab->free_vdev_stats_id_map & (1LL << vdev_stats_id)) {
			vdev_stats_id++;
			if (vdev_stats_id >= ATH12K_MAX_VDEV_STATS_ID) {
				vdev_stats_id = ATH12K_INVAL_VDEV_STATS_ID;
				break;
			}
		} else {
			ab->free_vdev_stats_id_map |= (1LL << vdev_stats_id);
			break;
		}
	} while (vdev_stats_id);

	arvif->vdev_stats_id = vdev_stats_id;
	return vdev_stats_id;
}

static int ath12k_mac_setup_vdev_params_mbssid(struct ath12k_link_vif *arvif,
					       u32 *flags, u32 *tx_vdev_id)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ieee80211_bss_conf *link_conf;
	struct ath12k *ar = arvif->ar;
	struct ath12k_link_vif *tx_arvif;

	link_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!link_conf) {
		ath12k_warn(ar->ab, "unable to access bss link conf in set mbssid params for vif %pM link %u\n",
			    ahvif->vif->addr, arvif->link_id);
		return -ENOLINK;
	}

	tx_arvif = ath12k_mac_get_tx_arvif(arvif, link_conf);
	if (!tx_arvif)
		return 0;

	if (link_conf->nontransmitted) {
		if (ath12k_ar_to_hw(ar)->wiphy !=
		    ath12k_ar_to_hw(tx_arvif->ar)->wiphy)
			return -EINVAL;

		*flags = WMI_VDEV_MBSSID_FLAGS_NON_TRANSMIT_AP;
		*tx_vdev_id = tx_arvif->vdev_id;
	} else if (tx_arvif == arvif) {
		*flags = WMI_VDEV_MBSSID_FLAGS_TRANSMIT_AP;
	} else {
		return -EINVAL;
	}

	if (link_conf->ema_ap)
		*flags |= WMI_VDEV_MBSSID_FLAGS_EMA_MODE;

	return 0;
}

static int ath12k_mac_setup_vdev_create_arg(struct ath12k_link_vif *arvif,
					    struct ath12k_wmi_vdev_create_arg *arg)
{
	struct ath12k *ar = arvif->ar;
	struct ath12k_pdev *pdev = ar->pdev;
	struct ath12k_vif *ahvif = arvif->ahvif;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	arg->if_id = arvif->vdev_id;
	arg->type = ahvif->vdev_type;
	arg->subtype = ahvif->vdev_subtype;
	arg->pdev_id = pdev->pdev_id;

	arg->mbssid_flags = WMI_VDEV_MBSSID_FLAGS_NON_MBSSID_AP;
	arg->mbssid_tx_vdev_id = 0;
	if (!test_bit(WMI_TLV_SERVICE_MBSS_PARAM_IN_VDEV_START_SUPPORT,
		      ar->ab->wmi_ab.svc_map)) {
		ret = ath12k_mac_setup_vdev_params_mbssid(arvif,
							  &arg->mbssid_flags,
							  &arg->mbssid_tx_vdev_id);
		if (ret)
			return ret;
	}

	if (pdev->cap.supported_bands & WMI_HOST_WLAN_2GHZ_CAP) {
		arg->chains[NL80211_BAND_2GHZ].tx = ar->num_tx_chains;
		arg->chains[NL80211_BAND_2GHZ].rx = ar->num_rx_chains;
	}
	if (pdev->cap.supported_bands & WMI_HOST_WLAN_5GHZ_CAP) {
		arg->chains[NL80211_BAND_5GHZ].tx = ar->num_tx_chains;
		arg->chains[NL80211_BAND_5GHZ].rx = ar->num_rx_chains;
	}
	if (pdev->cap.supported_bands & WMI_HOST_WLAN_5GHZ_CAP &&
	    ar->supports_6ghz) {
		arg->chains[NL80211_BAND_6GHZ].tx = ar->num_tx_chains;
		arg->chains[NL80211_BAND_6GHZ].rx = ar->num_rx_chains;
	}

	arg->if_stats_id = ath12k_mac_get_vdev_stats_id(arvif);

	if (ath12k_mac_is_ml_arvif(arvif)) {
		if (hweight16(ahvif->vif->valid_links) > ATH12K_WMI_MLO_MAX_LINKS) {
			ath12k_warn(ar->ab, "too many MLO links during setting up vdev: %d",
				    ahvif->vif->valid_links);
			return -EINVAL;
		}

		ether_addr_copy(arg->mld_addr, ahvif->vif->addr);
	}

	return 0;
}

static void ath12k_mac_update_vif_offload(struct ath12k_link_vif *arvif)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(ahvif);
	struct ath12k *ar = arvif->ar;
	struct ath12k_base *ab = ar->ab;
	u32 param_id, param_value;
	int ret;

	param_id = WMI_VDEV_PARAM_TX_ENCAP_TYPE;
	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		vif->offload_flags &= ~(IEEE80211_OFFLOAD_ENCAP_ENABLED |
					IEEE80211_OFFLOAD_DECAP_ENABLED);

	if (vif->offload_flags & IEEE80211_OFFLOAD_ENCAP_ENABLED)
		ahvif->tx_encap_type = ATH12K_HW_TXRX_ETHERNET;
	else if (test_bit(ATH12K_FLAG_RAW_MODE, &ab->dev_flags))
		ahvif->tx_encap_type = ATH12K_HW_TXRX_RAW;
	else
		ahvif->tx_encap_type = ATH12K_HW_TXRX_NATIVE_WIFI;

	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    param_id, ahvif->tx_encap_type);
	if (ret) {
		ath12k_warn(ab, "failed to set vdev %d tx encap mode: %d\n",
			    arvif->vdev_id, ret);
		vif->offload_flags &= ~IEEE80211_OFFLOAD_ENCAP_ENABLED;
	}

	param_id = WMI_VDEV_PARAM_RX_DECAP_TYPE;
	if (vif->offload_flags & IEEE80211_OFFLOAD_DECAP_ENABLED)
		param_value = ATH12K_HW_TXRX_ETHERNET;
	else if (test_bit(ATH12K_FLAG_RAW_MODE, &ab->dev_flags))
		param_value = ATH12K_HW_TXRX_RAW;
	else
		param_value = ATH12K_HW_TXRX_NATIVE_WIFI;

	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    param_id, param_value);
	if (ret) {
		ath12k_warn(ab, "failed to set vdev %d rx decap mode: %d\n",
			    arvif->vdev_id, ret);
		vif->offload_flags &= ~IEEE80211_OFFLOAD_DECAP_ENABLED;
	}
}

static void ath12k_mac_op_update_vif_offload(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif;
	unsigned long links;
	int link_id;

	lockdep_assert_wiphy(hw->wiphy);

	if (vif->valid_links) {
		links = vif->valid_links;
		for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
			arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
			if (!(arvif && arvif->ar))
				continue;

			ath12k_mac_update_vif_offload(arvif);
		}

		return;
	}

	ath12k_mac_update_vif_offload(&ahvif->deflink);
}

static bool ath12k_mac_vif_ap_active_any(struct ath12k_base *ab)
{
	struct ath12k *ar;
	struct ath12k_pdev *pdev;
	struct ath12k_link_vif *arvif;
	int i;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		list_for_each_entry(arvif, &ar->arvifs, list) {
			if (arvif->is_up &&
			    arvif->ahvif->vdev_type == WMI_VDEV_TYPE_AP)
				return true;
		}
	}
	return false;
}

void ath12k_mac_11d_scan_start(struct ath12k *ar, u32 vdev_id)
{
	struct wmi_11d_scan_start_arg arg;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (ar->regdom_set_by_user)
		goto fin;

	if (ar->vdev_id_11d_scan != ATH12K_11D_INVALID_VDEV_ID)
		goto fin;

	if (!test_bit(WMI_TLV_SERVICE_11D_OFFLOAD, ar->ab->wmi_ab.svc_map))
		goto fin;

	if (ath12k_mac_vif_ap_active_any(ar->ab))
		goto fin;

	arg.vdev_id = vdev_id;
	arg.start_interval_msec = 0;
	arg.scan_period_msec = ATH12K_SCAN_11D_INTERVAL;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "mac start 11d scan for vdev %d\n", vdev_id);

	ret = ath12k_wmi_send_11d_scan_start_cmd(ar, &arg);
	if (ret) {
		ath12k_warn(ar->ab, "failed to start 11d scan vdev %d ret: %d\n",
			    vdev_id, ret);
	} else {
		ar->vdev_id_11d_scan = vdev_id;
		if (ar->state_11d == ATH12K_11D_PREPARING)
			ar->state_11d = ATH12K_11D_RUNNING;
	}

fin:
	if (ar->state_11d == ATH12K_11D_PREPARING) {
		ar->state_11d = ATH12K_11D_IDLE;
		complete(&ar->completed_11d_scan);
	}
}

void ath12k_mac_11d_scan_stop(struct ath12k *ar)
{
	int ret;
	u32 vdev_id;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (!test_bit(WMI_TLV_SERVICE_11D_OFFLOAD, ar->ab->wmi_ab.svc_map))
		return;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac stop 11d for vdev %d\n",
		   ar->vdev_id_11d_scan);

	if (ar->state_11d == ATH12K_11D_PREPARING) {
		ar->state_11d = ATH12K_11D_IDLE;
		complete(&ar->completed_11d_scan);
	}

	if (ar->vdev_id_11d_scan != ATH12K_11D_INVALID_VDEV_ID) {
		vdev_id = ar->vdev_id_11d_scan;

		ret = ath12k_wmi_send_11d_scan_stop_cmd(ar, vdev_id);
		if (ret) {
			ath12k_warn(ar->ab,
				    "failed to stopt 11d scan vdev %d ret: %d\n",
				    vdev_id, ret);
		} else {
			ar->vdev_id_11d_scan = ATH12K_11D_INVALID_VDEV_ID;
			ar->state_11d = ATH12K_11D_IDLE;
			complete(&ar->completed_11d_scan);
		}
	}
}

void ath12k_mac_11d_scan_stop_all(struct ath12k_base *ab)
{
	struct ath12k *ar;
	struct ath12k_pdev *pdev;
	int i;

	ath12k_dbg(ab, ATH12K_DBG_MAC, "mac stop soc 11d scan\n");

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;

		ath12k_mac_11d_scan_stop(ar);
	}
}

static void ath12k_mac_determine_vdev_type(struct ieee80211_vif *vif,
					   struct ath12k_vif *ahvif)
{
	ahvif->vdev_subtype = WMI_VDEV_SUBTYPE_NONE;

	switch (vif->type) {
	case NL80211_IFTYPE_UNSPECIFIED:
	case NL80211_IFTYPE_STATION:
		ahvif->vdev_type = WMI_VDEV_TYPE_STA;

		if (vif->p2p)
			ahvif->vdev_subtype = WMI_VDEV_SUBTYPE_P2P_CLIENT;

		break;
	case NL80211_IFTYPE_MESH_POINT:
		ahvif->vdev_subtype = WMI_VDEV_SUBTYPE_MESH_11S;
		fallthrough;
	case NL80211_IFTYPE_AP:
		ahvif->vdev_type = WMI_VDEV_TYPE_AP;

		if (vif->p2p)
			ahvif->vdev_subtype = WMI_VDEV_SUBTYPE_P2P_GO;

		break;
	case NL80211_IFTYPE_MONITOR:
		ahvif->vdev_type = WMI_VDEV_TYPE_MONITOR;
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		ahvif->vdev_type = WMI_VDEV_TYPE_STA;
		ahvif->vdev_subtype = WMI_VDEV_SUBTYPE_P2P_DEVICE;
		break;
	default:
		WARN_ON(1);
		break;
	}
}

int ath12k_mac_vdev_create(struct ath12k *ar, struct ath12k_link_vif *arvif)
{
	struct ath12k_hw *ah = ar->ah;
	struct ath12k_base *ab = ar->ab;
	struct ieee80211_hw *hw = ah->hw;
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(ahvif);
	struct ath12k_wmi_vdev_create_arg vdev_arg = {0};
	struct ath12k_wmi_peer_create_arg peer_param = {0};
	struct ieee80211_bss_conf *link_conf = NULL;
	u32 param_id, param_value;
	u16 nss;
	int i;
	int ret, vdev_id;
	u8 link_id;

	lockdep_assert_wiphy(hw->wiphy);

	/* In NO_VIRTUAL_MONITOR, its necessary to restrict only one monitor
	 * interface in each radio
	 */
	if (vif->type == NL80211_IFTYPE_MONITOR && ar->monitor_vdev_created)
		return -EINVAL;

	link_id = arvif->link_id;

	if (link_id < IEEE80211_MLD_MAX_NUM_LINKS) {
		link_conf = wiphy_dereference(hw->wiphy, vif->link_conf[link_id]);
		if (!link_conf) {
			ath12k_warn(ar->ab, "unable to access bss link conf in vdev create for vif %pM link %u\n",
				    vif->addr, arvif->link_id);
			return -ENOLINK;
		}
	}

	if (link_conf)
		memcpy(arvif->bssid, link_conf->addr, ETH_ALEN);
	else
		memcpy(arvif->bssid, vif->addr, ETH_ALEN);

	arvif->ar = ar;
	vdev_id = __ffs64(ab->free_vdev_map);
	arvif->vdev_id = vdev_id;
	if (vif->type == NL80211_IFTYPE_MONITOR)
		ar->monitor_vdev_id = vdev_id;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac vdev create id %d type %d subtype %d map %llx\n",
		   arvif->vdev_id, ahvif->vdev_type, ahvif->vdev_subtype,
		   ab->free_vdev_map);

	vif->cab_queue = arvif->vdev_id % (ATH12K_HW_MAX_QUEUES - 1);
	for (i = 0; i < ARRAY_SIZE(vif->hw_queue); i++)
		vif->hw_queue[i] = i % (ATH12K_HW_MAX_QUEUES - 1);

	ret = ath12k_mac_setup_vdev_create_arg(arvif, &vdev_arg);
	if (ret) {
		ath12k_warn(ab, "failed to create vdev parameters %d: %d\n",
			    arvif->vdev_id, ret);
		goto err;
	}

	ret = ath12k_wmi_vdev_create(ar, arvif->bssid, &vdev_arg);
	if (ret) {
		ath12k_warn(ab, "failed to create WMI vdev %d: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	ar->num_created_vdevs++;
	arvif->is_created = true;
	ath12k_dbg(ab, ATH12K_DBG_MAC, "vdev %pM created, vdev_id %d\n",
		   vif->addr, arvif->vdev_id);
	ar->allocated_vdev_map |= 1LL << arvif->vdev_id;
	ab->free_vdev_map &= ~(1LL << arvif->vdev_id);

	spin_lock_bh(&ar->data_lock);
	list_add(&arvif->list, &ar->arvifs);
	spin_unlock_bh(&ar->data_lock);

	ath12k_mac_update_vif_offload(arvif);

	nss = hweight32(ar->cfg_tx_chainmask) ? : 1;
	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_NSS, nss);
	if (ret) {
		ath12k_warn(ab, "failed to set vdev %d chainmask 0x%x, nss %d :%d\n",
			    arvif->vdev_id, ar->cfg_tx_chainmask, nss, ret);
		goto err_vdev_del;
	}

	switch (ahvif->vdev_type) {
	case WMI_VDEV_TYPE_AP:
		peer_param.vdev_id = arvif->vdev_id;
		peer_param.peer_addr = arvif->bssid;
		peer_param.peer_type = WMI_PEER_TYPE_DEFAULT;
		ret = ath12k_peer_create(ar, arvif, NULL, &peer_param);
		if (ret) {
			ath12k_warn(ab, "failed to vdev %d create peer for AP: %d\n",
				    arvif->vdev_id, ret);
			goto err_vdev_del;
		}

		ret = ath12k_mac_set_kickout(arvif);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set vdev %i kickout parameters: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_del;
		}
		ath12k_mac_11d_scan_stop_all(ar->ab);
		break;
	case WMI_VDEV_TYPE_STA:
		param_id = WMI_STA_PS_PARAM_RX_WAKE_POLICY;
		param_value = WMI_STA_PS_RX_WAKE_POLICY_WAKE;
		ret = ath12k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
						  param_id, param_value);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set vdev %d RX wake policy: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_del;
		}

		param_id = WMI_STA_PS_PARAM_TX_WAKE_THRESHOLD;
		param_value = WMI_STA_PS_TX_WAKE_THRESHOLD_ALWAYS;
		ret = ath12k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
						  param_id, param_value);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set vdev %d TX wake threshold: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_del;
		}

		param_id = WMI_STA_PS_PARAM_PSPOLL_COUNT;
		param_value = WMI_STA_PS_PSPOLL_COUNT_NO_MAX;
		ret = ath12k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
						  param_id, param_value);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set vdev %d pspoll count: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_del;
		}

		ret = ath12k_wmi_pdev_set_ps_mode(ar, arvif->vdev_id, false);
		if (ret) {
			ath12k_warn(ar->ab, "failed to disable vdev %d ps mode: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_del;
		}

		if (test_bit(WMI_TLV_SERVICE_11D_OFFLOAD, ab->wmi_ab.svc_map) &&
		    ahvif->vdev_type == WMI_VDEV_TYPE_STA &&
		    ahvif->vdev_subtype == WMI_VDEV_SUBTYPE_NONE) {
			reinit_completion(&ar->completed_11d_scan);
			ar->state_11d = ATH12K_11D_PREPARING;
		}
		break;
	case WMI_VDEV_TYPE_MONITOR:
		ar->monitor_vdev_created = true;
		break;
	default:
		break;
	}

	if (link_conf)
		arvif->txpower = link_conf->txpower;
	else
		arvif->txpower = NL80211_TX_POWER_AUTOMATIC;

	ret = ath12k_mac_txpower_recalc(ar);
	if (ret)
		goto err_peer_del;

	param_id = WMI_VDEV_PARAM_RTS_THRESHOLD;
	param_value = hw->wiphy->rts_threshold;
	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    param_id, param_value);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set rts threshold for vdev %d: %d\n",
			    arvif->vdev_id, ret);
	}

	ath12k_dp_vdev_tx_attach(ar, arvif);

	return ret;

err_peer_del:
	if (ahvif->vdev_type == WMI_VDEV_TYPE_AP) {
		reinit_completion(&ar->peer_delete_done);

		ret = ath12k_wmi_send_peer_delete_cmd(ar, arvif->bssid,
						      arvif->vdev_id);
		if (ret) {
			ath12k_warn(ar->ab, "failed to delete peer vdev_id %d addr %pM\n",
				    arvif->vdev_id, arvif->bssid);
			goto err;
		}

		ret = ath12k_wait_for_peer_delete_done(ar, arvif->vdev_id,
						       arvif->bssid);
		if (ret)
			goto err_vdev_del;

		ar->num_peers--;
	}

err_vdev_del:
	if (ahvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ar->monitor_vdev_id = -1;
		ar->monitor_vdev_created = false;
	}

	ath12k_wmi_vdev_delete(ar, arvif->vdev_id);
	ar->num_created_vdevs--;
	arvif->is_created = false;
	arvif->ar = NULL;
	ar->allocated_vdev_map &= ~(1LL << arvif->vdev_id);
	ab->free_vdev_map |= 1LL << arvif->vdev_id;
	ab->free_vdev_stats_id_map &= ~(1LL << arvif->vdev_stats_id);
	spin_lock_bh(&ar->data_lock);
	list_del(&arvif->list);
	spin_unlock_bh(&ar->data_lock);

err:
	arvif->ar = NULL;
	return ret;
}

static void ath12k_mac_vif_flush_key_cache(struct ath12k_link_vif *arvif)
{
	struct ath12k_key_conf *key_conf, *tmp;
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k_hw *ah = ahvif->ah;
	struct ath12k_sta *ahsta;
	struct ath12k_link_sta *arsta;
	struct ath12k_vif_cache *cache = ahvif->cache[arvif->link_id];
	int ret;

	lockdep_assert_wiphy(ah->hw->wiphy);

	list_for_each_entry_safe(key_conf, tmp, &cache->key_conf.list, list) {
		arsta = NULL;
		if (key_conf->sta) {
			ahsta = ath12k_sta_to_ahsta(key_conf->sta);
			arsta = wiphy_dereference(ah->hw->wiphy,
						  ahsta->link[arvif->link_id]);
			if (!arsta)
				goto free_cache;
		}

		ret = ath12k_mac_set_key(arvif->ar, key_conf->cmd,
					 arvif, arsta,
					 key_conf->key);
		if (ret)
			ath12k_warn(arvif->ar->ab, "unable to apply set key param to vdev %d ret %d\n",
				    arvif->vdev_id, ret);
free_cache:
		list_del(&key_conf->list);
		kfree(key_conf);
	}
}

static void ath12k_mac_vif_cache_flush(struct ath12k *ar, struct ath12k_link_vif *arvif)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(ahvif);
	struct ath12k_vif_cache *cache = ahvif->cache[arvif->link_id];
	struct ath12k_base *ab = ar->ab;
	struct ieee80211_bss_conf *link_conf;

	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (!cache)
		return;

	if (cache->tx_conf.changed) {
		ret = ath12k_mac_conf_tx(arvif, cache->tx_conf.ac,
					 &cache->tx_conf.tx_queue_params);
		if (ret)
			ath12k_warn(ab,
				    "unable to apply tx config parameters to vdev %d\n",
				    ret);
	}

	if (cache->bss_conf_changed) {
		link_conf = ath12k_mac_get_link_bss_conf(arvif);
		if (!link_conf) {
			ath12k_warn(ar->ab, "unable to access bss link conf in cache flush for vif %pM link %u\n",
				    vif->addr, arvif->link_id);
			return;
		}
		ath12k_mac_bss_info_changed(ar, arvif, link_conf,
					    cache->bss_conf_changed);
	}

	if (!list_empty(&cache->key_conf.list))
		ath12k_mac_vif_flush_key_cache(arvif);

	ath12k_ahvif_put_link_cache(ahvif, arvif->link_id);
}

static struct ath12k *ath12k_mac_assign_vif_to_vdev(struct ieee80211_hw *hw,
						    struct ath12k_link_vif *arvif,
						    struct ieee80211_chanctx_conf *ctx)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(ahvif);
	struct ath12k_link_vif *scan_arvif;
	struct ath12k_hw *ah = hw->priv;
	struct ath12k *ar;
	struct ath12k_base *ab;
	u8 link_id = arvif->link_id, scan_link_id;
	unsigned long scan_link_map;
	int ret;

	lockdep_assert_wiphy(hw->wiphy);

	if (ah->num_radio == 1)
		ar = ah->radio;
	else if (ctx)
		ar = ath12k_get_ar_by_ctx(hw, ctx);
	else
		return NULL;

	if (!ar)
		return NULL;

	/* cleanup the scan vdev if we are done scan on that ar
	 * and now we want to create for actual usage.
	 */
	if (ieee80211_vif_is_mld(vif)) {
		scan_link_map = ahvif->links_map & ATH12K_SCAN_LINKS_MASK;
		for_each_set_bit(scan_link_id, &scan_link_map, ATH12K_NUM_MAX_LINKS) {
			scan_arvif = wiphy_dereference(hw->wiphy,
						       ahvif->link[scan_link_id]);
			if (scan_arvif && scan_arvif->ar == ar) {
				ar->scan.arvif = NULL;
				ath12k_mac_remove_link_interface(hw, scan_arvif);
				ath12k_mac_unassign_link_vif(scan_arvif);
				break;
			}
		}
	}

	if (arvif->ar) {
		/* This is not expected really */
		if (WARN_ON(!arvif->is_created)) {
			arvif->ar = NULL;
			return NULL;
		}

		if (ah->num_radio == 1)
			return arvif->ar;

		/* This can happen as scan vdev gets created during multiple scans
		 * across different radios before a vdev is brought up in
		 * a certain radio.
		 */
		if (ar != arvif->ar) {
			if (WARN_ON(arvif->is_started))
				return NULL;

			ath12k_mac_remove_link_interface(hw, arvif);
			ath12k_mac_unassign_link_vif(arvif);
		}
	}

	ab = ar->ab;

	/* Assign arvif again here since previous radio switch block
	 * would've unassigned and cleared it.
	 */
	arvif = ath12k_mac_assign_link_vif(ah, vif, link_id);
	if (vif->type == NL80211_IFTYPE_AP &&
	    ar->num_peers > (ar->max_num_peers - 1)) {
		ath12k_warn(ab, "failed to create vdev due to insufficient peer entry resource in firmware\n");
		goto unlock;
	}

	if (arvif->is_created)
		goto flush;

	if (ar->num_created_vdevs > (TARGET_NUM_VDEVS - 1)) {
		ath12k_warn(ab, "failed to create vdev, reached max vdev limit %d\n",
			    TARGET_NUM_VDEVS);
		goto unlock;
	}

	ret = ath12k_mac_vdev_create(ar, arvif);
	if (ret) {
		ath12k_warn(ab, "failed to create vdev %pM ret %d", vif->addr, ret);
		goto unlock;
	}

flush:
	/* If the vdev is created during channel assign and not during
	 * add_interface(), Apply any parameters for the vdev which were received
	 * after add_interface, corresponding to this vif.
	 */
	ath12k_mac_vif_cache_flush(ar, arvif);
unlock:
	return arvif->ar;
}

static int ath12k_mac_op_add_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_reg_info *reg_info;
	struct ath12k_link_vif *arvif;
	struct ath12k_base *ab;
	struct ath12k *ar;
	int i;

	lockdep_assert_wiphy(hw->wiphy);

	memset(ahvif, 0, sizeof(*ahvif));

	ahvif->ah = ah;
	ahvif->vif = vif;
	arvif = &ahvif->deflink;

	ath12k_mac_init_arvif(ahvif, arvif, -1);

	/* Allocate Default Queue now and reassign during actual vdev create */
	vif->cab_queue = ATH12K_HW_DEFAULT_QUEUE;
	for (i = 0; i < ARRAY_SIZE(vif->hw_queue); i++)
		vif->hw_queue[i] = ATH12K_HW_DEFAULT_QUEUE;

	vif->driver_flags |= IEEE80211_VIF_SUPPORTS_UAPSD;

	ath12k_mac_determine_vdev_type(vif, ahvif);

	for_each_ar(ah, ar, i) {
		if (!ath12k_wmi_supports_6ghz_cc_ext(ar))
			continue;

		ab = ar->ab;
		reg_info = ab->reg_info[ar->pdev_idx];
		ath12k_dbg(ab, ATH12K_DBG_MAC, "interface added to change reg rules\n");
		ah->regd_updated = false;
		ath12k_reg_handle_chan_list(ab, reg_info, ahvif->vdev_type,
					    IEEE80211_REG_UNSET_AP);
		break;
	}

	/* Defer vdev creation until assign_chanctx or hw_scan is initiated as driver
	 * will not know if this interface is an ML vif at this point.
	 */
	return 0;
}

static void ath12k_mac_vif_unref(struct ath12k_dp *dp, struct ieee80211_vif *vif)
{
	struct ath12k_tx_desc_info *tx_desc_info;
	struct ath12k_skb_cb *skb_cb;
	struct sk_buff *skb;
	int i;

	for (i = 0; i < ATH12K_HW_MAX_QUEUES; i++) {
		spin_lock_bh(&dp->tx_desc_lock[i]);

		list_for_each_entry(tx_desc_info, &dp->tx_desc_used_list[i],
				    list) {
			skb = tx_desc_info->skb;
			if (!skb)
				continue;

			skb_cb = ATH12K_SKB_CB(skb);
			if (skb_cb->vif == vif)
				skb_cb->vif = NULL;
		}

		spin_unlock_bh(&dp->tx_desc_lock[i]);
	}
}

static int ath12k_mac_vdev_delete(struct ath12k *ar, struct ath12k_link_vif *arvif)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(ahvif);
	struct ath12k_base *ab = ar->ab;
	unsigned long time_left;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	reinit_completion(&ar->vdev_delete_done);

	ret = ath12k_wmi_vdev_delete(ar, arvif->vdev_id);
	if (ret) {
		ath12k_warn(ab, "failed to delete WMI vdev %d: %d\n",
			    arvif->vdev_id, ret);
		goto err_vdev_del;
	}

	time_left = wait_for_completion_timeout(&ar->vdev_delete_done,
						ATH12K_VDEV_DELETE_TIMEOUT_HZ);
	if (time_left == 0) {
		ath12k_warn(ab, "Timeout in receiving vdev delete response\n");
		goto err_vdev_del;
	}

	ab->free_vdev_map |= 1LL << arvif->vdev_id;
	ar->allocated_vdev_map &= ~(1LL << arvif->vdev_id);
	ar->num_created_vdevs--;

	if (ahvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ar->monitor_vdev_id = -1;
		ar->monitor_vdev_created = false;
	}

	ath12k_dbg(ab, ATH12K_DBG_MAC, "vdev %pM deleted, vdev_id %d\n",
		   vif->addr, arvif->vdev_id);

err_vdev_del:
	spin_lock_bh(&ar->data_lock);
	list_del(&arvif->list);
	spin_unlock_bh(&ar->data_lock);

	ath12k_peer_cleanup(ar, arvif->vdev_id);
	ath12k_ahvif_put_link_cache(ahvif, arvif->link_id);

	idr_for_each(&ar->txmgmt_idr,
		     ath12k_mac_vif_txmgmt_idr_remove, vif);

	ath12k_mac_vif_unref(&ab->dp, vif);
	ath12k_dp_tx_put_bank_profile(&ab->dp, arvif->bank_id);

	/* Recalc txpower for remaining vdev */
	ath12k_mac_txpower_recalc(ar);

	/* TODO: recal traffic pause state based on the available vdevs */
	arvif->is_created = false;
	arvif->ar = NULL;

	return ret;
}

static void ath12k_mac_op_remove_interface(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif;
	struct ath12k *ar;
	u8 link_id;

	lockdep_assert_wiphy(hw->wiphy);

	for (link_id = 0; link_id < ATH12K_NUM_MAX_LINKS; link_id++) {
		/* if we cached some config but never received assign chanctx,
		 * free the allocated cache.
		 */
		ath12k_ahvif_put_link_cache(ahvif, link_id);
		arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
		if (!arvif || !arvif->is_created)
			continue;

		ar = arvif->ar;

		/* Scan abortion is in progress since before this, cancel_hw_scan()
		 * is expected to be executed. Since link is anyways going to be removed
		 * now, just cancel the worker and send the scan aborted to user space
		 */
		if (ar->scan.arvif == arvif) {
			wiphy_work_cancel(hw->wiphy, &ar->scan.vdev_clean_wk);

			spin_lock_bh(&ar->data_lock);
			ar->scan.arvif = NULL;
			if (!ar->scan.is_roc) {
				struct cfg80211_scan_info info = {
					.aborted = true,
				};

				ath12k_mac_scan_send_complete(ar, &info);
			}

			ar->scan.state = ATH12K_SCAN_IDLE;
			ar->scan_channel = NULL;
			ar->scan.roc_freq = 0;
			spin_unlock_bh(&ar->data_lock);
		}

		ath12k_mac_remove_link_interface(hw, arvif);
		ath12k_mac_unassign_link_vif(arvif);
	}
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

static void ath12k_mac_op_configure_filter(struct ieee80211_hw *hw,
					   unsigned int changed_flags,
					   unsigned int *total_flags,
					   u64 multicast)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar;

	lockdep_assert_wiphy(hw->wiphy);

	ar = ath12k_ah_to_ar(ah, 0);

	*total_flags &= SUPPORTED_FILTERS;
	ar->filter_flags = *total_flags;
}

static int ath12k_mac_op_get_antenna(struct ieee80211_hw *hw, int radio_idx,
				     u32 *tx_ant, u32 *rx_ant)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	int antennas_rx = 0, antennas_tx = 0;
	struct ath12k *ar;
	int i;

	lockdep_assert_wiphy(hw->wiphy);

	for_each_ar(ah, ar, i) {
		antennas_rx = max_t(u32, antennas_rx, ar->cfg_rx_chainmask);
		antennas_tx = max_t(u32, antennas_tx, ar->cfg_tx_chainmask);
	}

	*tx_ant = antennas_tx;
	*rx_ant = antennas_rx;

	return 0;
}

static int ath12k_mac_op_set_antenna(struct ieee80211_hw *hw, int radio_idx,
				     u32 tx_ant, u32 rx_ant)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar;
	int ret = 0;
	int i;

	lockdep_assert_wiphy(hw->wiphy);

	for_each_ar(ah, ar, i) {
		ret = __ath12k_set_antenna(ar, tx_ant, rx_ant);
		if (ret)
			break;
	}

	return ret;
}

static int ath12k_mac_ampdu_action(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_ampdu_params *params,
				   u8 link_id)
{
	struct ath12k *ar;
	int ret = -EINVAL;

	lockdep_assert_wiphy(hw->wiphy);

	ar = ath12k_get_ar_by_vif(hw, vif, link_id);
	if (!ar)
		return -EINVAL;

	switch (params->action) {
	case IEEE80211_AMPDU_RX_START:
		ret = ath12k_dp_rx_ampdu_start(ar, params, link_id);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		ret = ath12k_dp_rx_ampdu_stop(ar, params, link_id);
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

	if (ret)
		ath12k_warn(ar->ab, "unable to perform ampdu action %d for vif %pM link %u ret %d\n",
			    params->action, vif->addr, link_id, ret);

	return ret;
}

static int ath12k_mac_op_ampdu_action(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_ampdu_params *params)
{
	struct ieee80211_sta *sta = params->sta;
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);
	unsigned long links_map = ahsta->links_map;
	int ret = -EINVAL;
	u8 link_id;

	lockdep_assert_wiphy(hw->wiphy);

	if (WARN_ON(!links_map))
		return ret;

	for_each_set_bit(link_id, &links_map, IEEE80211_MLD_MAX_NUM_LINKS) {
		ret = ath12k_mac_ampdu_action(hw, vif, params, link_id);
		if (ret)
			return ret;
	}

	return 0;
}

static int ath12k_mac_op_add_chanctx(struct ieee80211_hw *hw,
				     struct ieee80211_chanctx_conf *ctx)
{
	struct ath12k *ar;
	struct ath12k_base *ab;

	lockdep_assert_wiphy(hw->wiphy);

	ar = ath12k_get_ar_by_ctx(hw, ctx);
	if (!ar)
		return -EINVAL;

	ab = ar->ab;

	ath12k_dbg(ab, ATH12K_DBG_MAC,
		   "mac chanctx add freq %u width %d ptr %p\n",
		   ctx->def.chan->center_freq, ctx->def.width, ctx);

	spin_lock_bh(&ar->data_lock);
	/* TODO: In case of multiple channel context, populate rx_channel from
	 * Rx PPDU desc information.
	 */
	ar->rx_channel = ctx->def.chan;
	spin_unlock_bh(&ar->data_lock);
	ar->chan_tx_pwr = ATH12K_PDEV_TX_POWER_INVALID;

	return 0;
}

static void ath12k_mac_op_remove_chanctx(struct ieee80211_hw *hw,
					 struct ieee80211_chanctx_conf *ctx)
{
	struct ath12k *ar;
	struct ath12k_base *ab;

	lockdep_assert_wiphy(hw->wiphy);

	ar = ath12k_get_ar_by_ctx(hw, ctx);
	if (!ar)
		return;

	ab = ar->ab;

	ath12k_dbg(ab, ATH12K_DBG_MAC,
		   "mac chanctx remove freq %u width %d ptr %p\n",
		   ctx->def.chan->center_freq, ctx->def.width, ctx);

	spin_lock_bh(&ar->data_lock);
	/* TODO: In case of there is one more channel context left, populate
	 * rx_channel with the channel of that remaining channel context.
	 */
	ar->rx_channel = NULL;
	spin_unlock_bh(&ar->data_lock);
	ar->chan_tx_pwr = ATH12K_PDEV_TX_POWER_INVALID;
}

static enum wmi_phy_mode
ath12k_mac_check_down_grade_phy_mode(struct ath12k *ar,
				     enum wmi_phy_mode mode,
				     enum nl80211_band band,
				     enum nl80211_iftype type)
{
	struct ieee80211_sta_eht_cap *eht_cap = NULL;
	enum wmi_phy_mode down_mode;
	int n = ar->mac.sbands[band].n_iftype_data;
	int i;
	struct ieee80211_sband_iftype_data *data;

	if (mode < MODE_11BE_EHT20)
		return mode;

	data = ar->mac.iftype[band];
	for (i = 0; i < n; i++) {
		if (data[i].types_mask & BIT(type)) {
			eht_cap = &data[i].eht_cap;
			break;
		}
	}

	if (eht_cap && eht_cap->has_eht)
		return mode;

	switch (mode) {
	case MODE_11BE_EHT20:
		down_mode = MODE_11AX_HE20;
		break;
	case MODE_11BE_EHT40:
		down_mode = MODE_11AX_HE40;
		break;
	case MODE_11BE_EHT80:
		down_mode = MODE_11AX_HE80;
		break;
	case MODE_11BE_EHT80_80:
		down_mode = MODE_11AX_HE80_80;
		break;
	case MODE_11BE_EHT160:
	case MODE_11BE_EHT160_160:
	case MODE_11BE_EHT320:
		down_mode = MODE_11AX_HE160;
		break;
	case MODE_11BE_EHT20_2G:
		down_mode = MODE_11AX_HE20_2G;
		break;
	case MODE_11BE_EHT40_2G:
		down_mode = MODE_11AX_HE40_2G;
		break;
	default:
		down_mode = mode;
		break;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "mac vdev start phymode %s downgrade to %s\n",
		   ath12k_mac_phymode_str(mode),
		   ath12k_mac_phymode_str(down_mode));

	return down_mode;
}

static void
ath12k_mac_mlo_get_vdev_args(struct ath12k_link_vif *arvif,
			     struct wmi_ml_arg *ml_arg)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct wmi_ml_partner_info *partner_info;
	struct ieee80211_bss_conf *link_conf;
	struct ath12k_link_vif *arvif_p;
	unsigned long links;
	u8 link_id;

	lockdep_assert_wiphy(ahvif->ah->hw->wiphy);

	if (!ath12k_mac_is_ml_arvif(arvif))
		return;

	if (hweight16(ahvif->vif->valid_links) > ATH12K_WMI_MLO_MAX_LINKS)
		return;

	ml_arg->enabled = true;

	/* Driver always add a new link via VDEV START, FW takes
	 * care of internally adding this link to existing
	 * link vdevs which are advertised as partners below
	 */
	ml_arg->link_add = true;

	ml_arg->assoc_link = arvif->is_sta_assoc_link;

	partner_info = ml_arg->partner_info;

	links = ahvif->links_map;
	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif_p = wiphy_dereference(ahvif->ah->hw->wiphy, ahvif->link[link_id]);

		if (WARN_ON(!arvif_p))
			continue;

		if (arvif == arvif_p)
			continue;

		if (!arvif_p->is_created)
			continue;

		link_conf = wiphy_dereference(ahvif->ah->hw->wiphy,
					      ahvif->vif->link_conf[arvif_p->link_id]);

		if (!link_conf)
			continue;

		partner_info->vdev_id = arvif_p->vdev_id;
		partner_info->hw_link_id = arvif_p->ar->pdev->hw_link_id;
		ether_addr_copy(partner_info->addr, link_conf->addr);
		ml_arg->num_partner_links++;
		partner_info++;
	}
}

static int
ath12k_mac_vdev_start_restart(struct ath12k_link_vif *arvif,
			      struct ieee80211_chanctx_conf *ctx,
			      bool restart)
{
	struct ath12k *ar = arvif->ar;
	struct ath12k_base *ab = ar->ab;
	struct wmi_vdev_start_req_arg arg = {};
	const struct cfg80211_chan_def *chandef = &ctx->def;
	struct ieee80211_hw *hw = ath12k_ar_to_hw(ar);
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ieee80211_bss_conf *link_conf;
	unsigned int dfs_cac_time;
	int ret;

	lockdep_assert_wiphy(hw->wiphy);

	link_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!link_conf) {
		ath12k_warn(ar->ab, "unable to access bss link conf in vdev start for vif %pM link %u\n",
			    ahvif->vif->addr, arvif->link_id);
		return -ENOLINK;
	}

	reinit_completion(&ar->vdev_setup_done);

	arg.vdev_id = arvif->vdev_id;
	arg.dtim_period = arvif->dtim_period;
	arg.bcn_intval = arvif->beacon_interval;
	arg.punct_bitmap = ~arvif->punct_bitmap;

	arg.freq = chandef->chan->center_freq;
	arg.band_center_freq1 = chandef->center_freq1;
	arg.band_center_freq2 = chandef->center_freq2;
	arg.mode = ath12k_phymodes[chandef->chan->band][chandef->width];

	arg.mode = ath12k_mac_check_down_grade_phy_mode(ar, arg.mode,
							chandef->chan->band,
							ahvif->vif->type);
	arg.min_power = 0;
	arg.max_power = chandef->chan->max_power;
	arg.max_reg_power = chandef->chan->max_reg_power;
	arg.max_antenna_gain = chandef->chan->max_antenna_gain;

	arg.pref_tx_streams = ar->num_tx_chains;
	arg.pref_rx_streams = ar->num_rx_chains;

	arg.mbssid_flags = WMI_VDEV_MBSSID_FLAGS_NON_MBSSID_AP;
	arg.mbssid_tx_vdev_id = 0;
	if (test_bit(WMI_TLV_SERVICE_MBSS_PARAM_IN_VDEV_START_SUPPORT,
		     ar->ab->wmi_ab.svc_map)) {
		ret = ath12k_mac_setup_vdev_params_mbssid(arvif,
							  &arg.mbssid_flags,
							  &arg.mbssid_tx_vdev_id);
		if (ret)
			return ret;
	}

	if (ahvif->vdev_type == WMI_VDEV_TYPE_AP) {
		arg.ssid = ahvif->u.ap.ssid;
		arg.ssid_len = ahvif->u.ap.ssid_len;
		arg.hidden_ssid = ahvif->u.ap.hidden_ssid;

		/* For now allow DFS for AP mode */
		arg.chan_radar = !!(chandef->chan->flags & IEEE80211_CHAN_RADAR);

		arg.freq2_radar = ctx->radar_enabled;

		arg.passive = arg.chan_radar;

		spin_lock_bh(&ab->base_lock);
		arg.regdomain = ar->ab->dfs_region;
		spin_unlock_bh(&ab->base_lock);

		/* TODO: Notify if secondary 80Mhz also needs radar detection */
	}

	arg.passive |= !!(chandef->chan->flags & IEEE80211_CHAN_NO_IR);

	if (!restart)
		ath12k_mac_mlo_get_vdev_args(arvif, &arg.ml);

	ath12k_dbg(ab, ATH12K_DBG_MAC,
		   "mac vdev %d start center_freq %d phymode %s punct_bitmap 0x%x\n",
		   arg.vdev_id, arg.freq,
		   ath12k_mac_phymode_str(arg.mode), arg.punct_bitmap);

	ret = ath12k_wmi_vdev_start(ar, &arg, restart);
	if (ret) {
		ath12k_warn(ar->ab, "failed to %s WMI vdev %i\n",
			    restart ? "restart" : "start", arg.vdev_id);
		return ret;
	}

	ret = ath12k_mac_vdev_setup_sync(ar);
	if (ret) {
		ath12k_warn(ab, "failed to synchronize setup for vdev %i %s: %d\n",
			    arg.vdev_id, restart ? "restart" : "start", ret);
		return ret;
	}

	/* TODO: For now we only set TPC power here. However when
	 * channel changes, say CSA, it should be updated again.
	 */
	if (ath12k_mac_supports_tpc(ar, ahvif, chandef)) {
		ath12k_mac_fill_reg_tpc_info(ar, arvif, ctx);
		ath12k_wmi_send_vdev_set_tpc_power(ar, arvif->vdev_id,
						   &arvif->reg_tpc_info);
	}

	ar->num_started_vdevs++;
	ath12k_dbg(ab, ATH12K_DBG_MAC,  "vdev %pM started, vdev_id %d\n",
		   ahvif->vif->addr, arvif->vdev_id);

	/* Enable CAC Running Flag in the driver by checking all sub-channel's DFS
	 * state as NL80211_DFS_USABLE which indicates CAC needs to be
	 * done before channel usage. This flag is used to drop rx packets.
	 * during CAC.
	 */
	/* TODO: Set the flag for other interface types as required */
	if (arvif->ahvif->vdev_type == WMI_VDEV_TYPE_AP && ctx->radar_enabled &&
	    cfg80211_chandef_dfs_usable(hw->wiphy, chandef)) {
		set_bit(ATH12K_FLAG_CAC_RUNNING, &ar->dev_flags);
		dfs_cac_time = cfg80211_chandef_dfs_cac_time(hw->wiphy, chandef);

		ath12k_dbg(ab, ATH12K_DBG_MAC,
			   "CAC started dfs_cac_time %u center_freq %d center_freq1 %d for vdev %d\n",
			   dfs_cac_time, arg.freq, arg.band_center_freq1, arg.vdev_id);
	}

	ret = ath12k_mac_set_txbf_conf(arvif);
	if (ret)
		ath12k_warn(ab, "failed to set txbf conf for vdev %d: %d\n",
			    arvif->vdev_id, ret);

	return 0;
}

static int ath12k_mac_vdev_start(struct ath12k_link_vif *arvif,
				 struct ieee80211_chanctx_conf *ctx)
{
	return ath12k_mac_vdev_start_restart(arvif, ctx, false);
}

static int ath12k_mac_vdev_restart(struct ath12k_link_vif *arvif,
				   struct ieee80211_chanctx_conf *ctx)
{
	return ath12k_mac_vdev_start_restart(arvif, ctx, true);
}

struct ath12k_mac_change_chanctx_arg {
	struct ieee80211_chanctx_conf *ctx;
	struct ieee80211_vif_chanctx_switch *vifs;
	int n_vifs;
	int next_vif;
	struct ath12k *ar;
};

static void
ath12k_mac_change_chanctx_cnt_iter(void *data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_mac_change_chanctx_arg *arg = data;
	struct ieee80211_bss_conf *link_conf;
	struct ath12k_link_vif *arvif;
	unsigned long links_map;
	u8 link_id;

	lockdep_assert_wiphy(ahvif->ah->hw->wiphy);

	links_map = ahvif->links_map;
	for_each_set_bit(link_id, &links_map, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = wiphy_dereference(ahvif->ah->hw->wiphy, ahvif->link[link_id]);
		if (WARN_ON(!arvif))
			continue;

		if (!arvif->is_created || arvif->ar != arg->ar)
			continue;

		link_conf = wiphy_dereference(ahvif->ah->hw->wiphy,
					      vif->link_conf[link_id]);
		if (WARN_ON(!link_conf))
			continue;

		if (rcu_access_pointer(link_conf->chanctx_conf) != arg->ctx)
			continue;

		arg->n_vifs++;
	}
}

static void
ath12k_mac_change_chanctx_fill_iter(void *data, u8 *mac,
				    struct ieee80211_vif *vif)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_mac_change_chanctx_arg *arg = data;
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_chanctx_conf *ctx;
	struct ath12k_link_vif *arvif;
	unsigned long links_map;
	u8 link_id;

	lockdep_assert_wiphy(ahvif->ah->hw->wiphy);

	links_map = ahvif->links_map;
	for_each_set_bit(link_id, &links_map, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = wiphy_dereference(ahvif->ah->hw->wiphy, ahvif->link[link_id]);
		if (WARN_ON(!arvif))
			continue;

		if (!arvif->is_created || arvif->ar != arg->ar)
			continue;

		link_conf = wiphy_dereference(ahvif->ah->hw->wiphy,
					      vif->link_conf[arvif->link_id]);
		if (WARN_ON(!link_conf))
			continue;

		ctx = rcu_access_pointer(link_conf->chanctx_conf);
		if (ctx != arg->ctx)
			continue;

		if (WARN_ON(arg->next_vif == arg->n_vifs))
			return;

		arg->vifs[arg->next_vif].vif = vif;
		arg->vifs[arg->next_vif].old_ctx = ctx;
		arg->vifs[arg->next_vif].new_ctx = ctx;
		arg->vifs[arg->next_vif].link_conf = link_conf;
		arg->next_vif++;
	}
}

static u32 ath12k_mac_nlwidth_to_wmiwidth(enum nl80211_chan_width width)
{
	switch (width) {
	case NL80211_CHAN_WIDTH_20:
		return WMI_CHAN_WIDTH_20;
	case NL80211_CHAN_WIDTH_40:
		return WMI_CHAN_WIDTH_40;
	case NL80211_CHAN_WIDTH_80:
		return WMI_CHAN_WIDTH_80;
	case NL80211_CHAN_WIDTH_160:
		return WMI_CHAN_WIDTH_160;
	case NL80211_CHAN_WIDTH_80P80:
		return WMI_CHAN_WIDTH_80P80;
	case NL80211_CHAN_WIDTH_5:
		return WMI_CHAN_WIDTH_5;
	case NL80211_CHAN_WIDTH_10:
		return WMI_CHAN_WIDTH_10;
	case NL80211_CHAN_WIDTH_320:
		return WMI_CHAN_WIDTH_320;
	default:
		WARN_ON(1);
		return WMI_CHAN_WIDTH_20;
	}
}

static int ath12k_mac_update_peer_puncturing_width(struct ath12k *ar,
						   struct ath12k_link_vif *arvif,
						   struct cfg80211_chan_def def)
{
	u32 param_id, param_value;
	int ret;

	if (arvif->ahvif->vdev_type != WMI_VDEV_TYPE_STA)
		return 0;

	param_id = WMI_PEER_CHWIDTH_PUNCTURE_20MHZ_BITMAP;
	param_value = ath12k_mac_nlwidth_to_wmiwidth(def.width) |
		u32_encode_bits((~def.punctured),
				WMI_PEER_PUNCTURE_BITMAP);

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "punctured bitmap %02x width %d vdev %d\n",
		   def.punctured, def.width, arvif->vdev_id);

	ret = ath12k_wmi_set_peer_param(ar, arvif->bssid,
					arvif->vdev_id, param_id,
					param_value);

	return ret;
}

static void
ath12k_mac_update_vif_chan(struct ath12k *ar,
			   struct ieee80211_vif_chanctx_switch *vifs,
			   int n_vifs)
{
	struct ath12k_wmi_vdev_up_params params = {};
	struct ath12k_link_vif *arvif;
	struct ieee80211_bss_conf *link_conf;
	struct ath12k_base *ab = ar->ab;
	struct ieee80211_vif *vif;
	struct ath12k_vif *ahvif;
	u8 link_id;
	int ret;
	int i;
	bool monitor_vif = false;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	for (i = 0; i < n_vifs; i++) {
		vif = vifs[i].vif;
		ahvif = ath12k_vif_to_ahvif(vif);
		link_conf = vifs[i].link_conf;
		link_id = link_conf->link_id;
		arvif = wiphy_dereference(ath12k_ar_to_hw(ar)->wiphy,
					  ahvif->link[link_id]);

		if (vif->type == NL80211_IFTYPE_MONITOR) {
			monitor_vif = true;
			continue;
		}

		ath12k_dbg(ab, ATH12K_DBG_MAC,
			   "mac chanctx switch vdev_id %i freq %u->%u width %d->%d\n",
			   arvif->vdev_id,
			   vifs[i].old_ctx->def.chan->center_freq,
			   vifs[i].new_ctx->def.chan->center_freq,
			   vifs[i].old_ctx->def.width,
			   vifs[i].new_ctx->def.width);

		if (WARN_ON(!arvif->is_started))
			continue;

		arvif->punct_bitmap = vifs[i].new_ctx->def.punctured;

		/* Firmware expect vdev_restart only if vdev is up.
		 * If vdev is down then it expect vdev_stop->vdev_start.
		 */
		if (arvif->is_up) {
			ret = ath12k_mac_vdev_restart(arvif, vifs[i].new_ctx);
			if (ret) {
				ath12k_warn(ab, "failed to restart vdev %d: %d\n",
					    arvif->vdev_id, ret);
				continue;
			}
		} else {
			ret = ath12k_mac_vdev_stop(arvif);
			if (ret) {
				ath12k_warn(ab, "failed to stop vdev %d: %d\n",
					    arvif->vdev_id, ret);
				continue;
			}

			ret = ath12k_mac_vdev_start(arvif, vifs[i].new_ctx);
			if (ret)
				ath12k_warn(ab, "failed to start vdev %d: %d\n",
					    arvif->vdev_id, ret);
			continue;
		}

		ret = ath12k_mac_setup_bcn_tmpl(arvif);
		if (ret)
			ath12k_warn(ab, "failed to update bcn tmpl during csa: %d\n",
				    ret);

		memset(&params, 0, sizeof(params));
		params.vdev_id = arvif->vdev_id;
		params.aid = ahvif->aid;
		params.bssid = arvif->bssid;
		params.tx_bssid = ath12k_mac_get_tx_bssid(arvif);
		if (params.tx_bssid) {
			params.nontx_profile_idx = link_conf->bssid_index;
			params.nontx_profile_cnt = 1 << link_conf->bssid_indicator;
		}
		ret = ath12k_wmi_vdev_up(arvif->ar, &params);
		if (ret) {
			ath12k_warn(ab, "failed to bring vdev up %d: %d\n",
				    arvif->vdev_id, ret);
			continue;
		}

		ret = ath12k_mac_update_peer_puncturing_width(arvif->ar, arvif,
							      vifs[i].new_ctx->def);
		if (ret) {
			ath12k_warn(ar->ab,
				    "failed to update puncturing bitmap %02x and width %d: %d\n",
				    vifs[i].new_ctx->def.punctured,
				    vifs[i].new_ctx->def.width, ret);
			continue;
		}
	}

	/* Restart the internal monitor vdev on new channel */
	if (!monitor_vif && ar->monitor_vdev_created) {
		if (!ath12k_mac_monitor_stop(ar))
			ath12k_mac_monitor_start(ar);
	}
}

static void
ath12k_mac_update_active_vif_chan(struct ath12k *ar,
				  struct ieee80211_chanctx_conf *ctx)
{
	struct ath12k_mac_change_chanctx_arg arg = { .ctx = ctx, .ar = ar };
	struct ieee80211_hw *hw = ath12k_ar_to_hw(ar);

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ieee80211_iterate_active_interfaces_atomic(hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath12k_mac_change_chanctx_cnt_iter,
						   &arg);
	if (arg.n_vifs == 0)
		return;

	arg.vifs = kcalloc(arg.n_vifs, sizeof(arg.vifs[0]), GFP_KERNEL);
	if (!arg.vifs)
		return;

	ieee80211_iterate_active_interfaces_atomic(hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath12k_mac_change_chanctx_fill_iter,
						   &arg);

	ath12k_mac_update_vif_chan(ar, arg.vifs, arg.n_vifs);

	kfree(arg.vifs);
}

static void ath12k_mac_op_change_chanctx(struct ieee80211_hw *hw,
					 struct ieee80211_chanctx_conf *ctx,
					 u32 changed)
{
	struct ath12k *ar;
	struct ath12k_base *ab;

	lockdep_assert_wiphy(hw->wiphy);

	ar = ath12k_get_ar_by_ctx(hw, ctx);
	if (!ar)
		return;

	ab = ar->ab;

	ath12k_dbg(ab, ATH12K_DBG_MAC,
		   "mac chanctx change freq %u width %d ptr %p changed %x\n",
		   ctx->def.chan->center_freq, ctx->def.width, ctx, changed);

	/* This shouldn't really happen because channel switching should use
	 * switch_vif_chanctx().
	 */
	if (WARN_ON(changed & IEEE80211_CHANCTX_CHANGE_CHANNEL))
		return;

	if (changed & IEEE80211_CHANCTX_CHANGE_WIDTH ||
	    changed & IEEE80211_CHANCTX_CHANGE_RADAR ||
	    changed & IEEE80211_CHANCTX_CHANGE_PUNCTURING)
		ath12k_mac_update_active_vif_chan(ar, ctx);

	/* TODO: Recalc radar detection */
}

static int ath12k_start_vdev_delay(struct ath12k *ar,
				   struct ath12k_link_vif *arvif)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ieee80211_chanctx_conf *chanctx;
	struct ieee80211_bss_conf *link_conf;
	int ret;

	if (WARN_ON(arvif->is_started))
		return -EBUSY;

	link_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!link_conf) {
		ath12k_warn(ab, "failed to get link conf for vdev %u\n", arvif->vdev_id);
		return -EINVAL;
	}

	chanctx	= wiphy_dereference(ath12k_ar_to_hw(arvif->ar)->wiphy,
				    link_conf->chanctx_conf);
	ret = ath12k_mac_vdev_start(arvif, chanctx);
	if (ret) {
		ath12k_warn(ab, "failed to start vdev %i addr %pM on freq %d: %d\n",
			    arvif->vdev_id, vif->addr,
			    chanctx->def.chan->center_freq, ret);
		return ret;
	}

	if (ahvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ret = ath12k_monitor_vdev_up(ar, arvif->vdev_id);
		if (ret) {
			ath12k_warn(ab, "failed put monitor up: %d\n", ret);
			return ret;
		}
	}

	arvif->is_started = true;

	/* TODO: Setup ps and cts/rts protection */
	return 0;
}

static u8 ath12k_mac_get_num_pwr_levels(struct cfg80211_chan_def *chan_def)
{
	if (chan_def->chan->flags & IEEE80211_CHAN_PSD) {
		switch (chan_def->width) {
		case NL80211_CHAN_WIDTH_20:
			return 1;
		case NL80211_CHAN_WIDTH_40:
			return 2;
		case NL80211_CHAN_WIDTH_80:
			return 4;
		case NL80211_CHAN_WIDTH_160:
			return 8;
		case NL80211_CHAN_WIDTH_320:
			return 16;
		default:
			return 1;
		}
	} else {
		switch (chan_def->width) {
		case NL80211_CHAN_WIDTH_20:
			return 1;
		case NL80211_CHAN_WIDTH_40:
			return 2;
		case NL80211_CHAN_WIDTH_80:
			return 3;
		case NL80211_CHAN_WIDTH_160:
			return 4;
		case NL80211_CHAN_WIDTH_320:
			return 5;
		default:
			return 1;
		}
	}
}

static u16 ath12k_mac_get_6ghz_start_frequency(struct cfg80211_chan_def *chan_def)
{
	u16 diff_seq;

	/* It is to get the lowest channel number's center frequency of the chan.
	 * For example,
	 * bandwidth=40 MHz, center frequency is 5965, lowest channel is 1
	 * with center frequency 5955, its diff is 5965 - 5955 = 10.
	 * bandwidth=80 MHz, center frequency is 5985, lowest channel is 1
	 * with center frequency 5955, its diff is 5985 - 5955 = 30.
	 * bandwidth=160 MHz, center frequency is 6025, lowest channel is 1
	 * with center frequency 5955, its diff is 6025 - 5955 = 70.
	 * bandwidth=320 MHz, center frequency is 6105, lowest channel is 1
	 * with center frequency 5955, its diff is 6105 - 5955 = 70.
	 */
	switch (chan_def->width) {
	case NL80211_CHAN_WIDTH_320:
		diff_seq = 150;
		break;
	case NL80211_CHAN_WIDTH_160:
		diff_seq = 70;
		break;
	case NL80211_CHAN_WIDTH_80:
		diff_seq = 30;
		break;
	case NL80211_CHAN_WIDTH_40:
		diff_seq = 10;
		break;
	default:
		diff_seq = 0;
	}

	return chan_def->center_freq1 - diff_seq;
}

static u16 ath12k_mac_get_seg_freq(struct cfg80211_chan_def *chan_def,
				   u16 start_seq, u8 seq)
{
	u16 seg_seq;

	/* It is to get the center frequency of the specific bandwidth.
	 * start_seq means the lowest channel number's center frequency.
	 * seq 0/1/2/3 means 20 MHz/40 MHz/80 MHz/160 MHz.
	 * For example,
	 * lowest channel is 1, its center frequency 5955,
	 * center frequency is 5955 when bandwidth=20 MHz, its diff is 5955 - 5955 = 0.
	 * lowest channel is 1, its center frequency 5955,
	 * center frequency is 5965 when bandwidth=40 MHz, its diff is 5965 - 5955 = 10.
	 * lowest channel is 1, its center frequency 5955,
	 * center frequency is 5985 when bandwidth=80 MHz, its diff is 5985 - 5955 = 30.
	 * lowest channel is 1, its center frequency 5955,
	 * center frequency is 6025 when bandwidth=160 MHz, its diff is 6025 - 5955 = 70.
	 */
	seg_seq = 10 * (BIT(seq) - 1);
	return seg_seq + start_seq;
}

static void ath12k_mac_get_psd_channel(struct ath12k *ar,
				       u16 step_freq,
				       u16 *start_freq,
				       u16 *center_freq,
				       u8 i,
				       struct ieee80211_channel **temp_chan,
				       s8 *tx_power)
{
	/* It is to get the center frequency for each 20 MHz.
	 * For example, if the chan is 160 MHz and center frequency is 6025,
	 * then it include 8 channels, they are 1/5/9/13/17/21/25/29,
	 * channel number 1's center frequency is 5955, it is parameter start_freq.
	 * parameter i is the step of the 8 channels. i is 0~7 for the 8 channels.
	 * the channel 1/5/9/13/17/21/25/29 maps i=0/1/2/3/4/5/6/7,
	 * and maps its center frequency is 5955/5975/5995/6015/6035/6055/6075/6095,
	 * the gap is 20 for each channel, parameter step_freq means the gap.
	 * after get the center frequency of each channel, it is easy to find the
	 * struct ieee80211_channel of it and get the max_reg_power.
	 */
	*center_freq = *start_freq + i * step_freq;
	*temp_chan = ieee80211_get_channel(ar->ah->hw->wiphy, *center_freq);
	*tx_power = (*temp_chan)->max_reg_power;
}

static void ath12k_mac_get_eirp_power(struct ath12k *ar,
				      u16 *start_freq,
				      u16 *center_freq,
				      u8 i,
				      struct ieee80211_channel **temp_chan,
				      struct cfg80211_chan_def *def,
				      s8 *tx_power)
{
	/* It is to get the center frequency for 20 MHz/40 MHz/80 MHz/
	 * 160 MHz bandwidth, and then plus 10 to the center frequency,
	 * it is the center frequency of a channel number.
	 * For example, when configured channel number is 1.
	 * center frequency is 5965 when bandwidth=40 MHz, after plus 10, it is 5975,
	 * then it is channel number 5.
	 * center frequency is 5985 when bandwidth=80 MHz, after plus 10, it is 5995,
	 * then it is channel number 9.
	 * center frequency is 6025 when bandwidth=160 MHz, after plus 10, it is 6035,
	 * then it is channel number 17.
	 * after get the center frequency of each channel, it is easy to find the
	 * struct ieee80211_channel of it and get the max_reg_power.
	 */
	*center_freq = ath12k_mac_get_seg_freq(def, *start_freq, i);

	/* For the 20 MHz, its center frequency is same with same channel */
	if (i != 0)
		*center_freq += 10;

	*temp_chan = ieee80211_get_channel(ar->ah->hw->wiphy, *center_freq);
	*tx_power = (*temp_chan)->max_reg_power;
}

void ath12k_mac_fill_reg_tpc_info(struct ath12k *ar,
				  struct ath12k_link_vif *arvif,
				  struct ieee80211_chanctx_conf *ctx)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_reg_tpc_power_info *reg_tpc_info = &arvif->reg_tpc_info;
	struct ieee80211_bss_conf *bss_conf = ath12k_mac_get_link_bss_conf(arvif);
	struct ieee80211_channel *chan, *temp_chan;
	u8 pwr_lvl_idx, num_pwr_levels, pwr_reduction;
	bool is_psd_power = false, is_tpe_present = false;
	s8 max_tx_power[ATH12K_NUM_PWR_LEVELS],
		psd_power, tx_power, eirp_power;
	struct ath12k_vif *ahvif = arvif->ahvif;
	u16 start_freq, center_freq;
	u8 reg_6ghz_power_mode;

	chan = ctx->def.chan;
	start_freq = ath12k_mac_get_6ghz_start_frequency(&ctx->def);
	pwr_reduction = bss_conf->pwr_reduction;

	if (arvif->reg_tpc_info.num_pwr_levels) {
		is_tpe_present = true;
		num_pwr_levels = arvif->reg_tpc_info.num_pwr_levels;
	} else {
		num_pwr_levels = ath12k_mac_get_num_pwr_levels(&ctx->def);
	}

	for (pwr_lvl_idx = 0; pwr_lvl_idx < num_pwr_levels; pwr_lvl_idx++) {
		/* STA received TPE IE*/
		if (is_tpe_present) {
			/* local power is PSD power*/
			if (chan->flags & IEEE80211_CHAN_PSD) {
				/* Connecting AP is psd power */
				if (reg_tpc_info->is_psd_power) {
					is_psd_power = true;
					ath12k_mac_get_psd_channel(ar, 20,
								   &start_freq,
								   &center_freq,
								   pwr_lvl_idx,
								   &temp_chan,
								   &tx_power);
					psd_power = temp_chan->psd;
					eirp_power = tx_power;
					max_tx_power[pwr_lvl_idx] =
						min_t(s8,
						      psd_power,
						      reg_tpc_info->tpe[pwr_lvl_idx]);
				/* Connecting AP is not psd power */
				} else {
					ath12k_mac_get_eirp_power(ar,
								  &start_freq,
								  &center_freq,
								  pwr_lvl_idx,
								  &temp_chan,
								  &ctx->def,
								  &tx_power);
					psd_power = temp_chan->psd;
					/* convert psd power to EIRP power based
					 * on channel width
					 */
					tx_power =
						min_t(s8, tx_power,
						      psd_power + 13 + pwr_lvl_idx * 3);
					max_tx_power[pwr_lvl_idx] =
						min_t(s8,
						      tx_power,
						      reg_tpc_info->tpe[pwr_lvl_idx]);
				}
			/* local power is not PSD power */
			} else {
				/* Connecting AP is psd power */
				if (reg_tpc_info->is_psd_power) {
					is_psd_power = true;
					ath12k_mac_get_psd_channel(ar, 20,
								   &start_freq,
								   &center_freq,
								   pwr_lvl_idx,
								   &temp_chan,
								   &tx_power);
					eirp_power = tx_power;
					max_tx_power[pwr_lvl_idx] =
						reg_tpc_info->tpe[pwr_lvl_idx];
				/* Connecting AP is not psd power */
				} else {
					ath12k_mac_get_eirp_power(ar,
								  &start_freq,
								  &center_freq,
								  pwr_lvl_idx,
								  &temp_chan,
								  &ctx->def,
								  &tx_power);
					max_tx_power[pwr_lvl_idx] =
						min_t(s8,
						      tx_power,
						      reg_tpc_info->tpe[pwr_lvl_idx]);
				}
			}
		/* STA not received TPE IE */
		} else {
			/* local power is PSD power*/
			if (chan->flags & IEEE80211_CHAN_PSD) {
				is_psd_power = true;
				ath12k_mac_get_psd_channel(ar, 20,
							   &start_freq,
							   &center_freq,
							   pwr_lvl_idx,
							   &temp_chan,
							   &tx_power);
				psd_power = temp_chan->psd;
				eirp_power = tx_power;
				max_tx_power[pwr_lvl_idx] = psd_power;
			} else {
				ath12k_mac_get_eirp_power(ar,
							  &start_freq,
							  &center_freq,
							  pwr_lvl_idx,
							  &temp_chan,
							  &ctx->def,
							  &tx_power);
				max_tx_power[pwr_lvl_idx] = tx_power;
			}
		}

		if (is_psd_power) {
			/* If AP local power constraint is present */
			if (pwr_reduction)
				eirp_power = eirp_power - pwr_reduction;

			/* If firmware updated max tx power is non zero, then take
			 * the min of firmware updated ap tx power
			 * and max power derived from above mentioned parameters.
			 */
			ath12k_dbg(ab, ATH12K_DBG_MAC,
				   "eirp power : %d firmware report power : %d\n",
				   eirp_power, ar->max_allowed_tx_power);
			/* Firmware reports lower max_allowed_tx_power during vdev
			 * start response. In case of 6 GHz, firmware is not aware
			 * of EIRP power unless driver sets EIRP power through WMI
			 * TPC command. So radio which does not support idle power
			 * save can set maximum calculated EIRP power directly to
			 * firmware through TPC command without min comparison with
			 * vdev start response's max_allowed_tx_power.
			 */
			if (ar->max_allowed_tx_power && ab->hw_params->idle_ps)
				eirp_power = min_t(s8,
						   eirp_power,
						   ar->max_allowed_tx_power);
		} else {
			/* If AP local power constraint is present */
			if (pwr_reduction)
				max_tx_power[pwr_lvl_idx] =
					max_tx_power[pwr_lvl_idx] - pwr_reduction;
			/* If firmware updated max tx power is non zero, then take
			 * the min of firmware updated ap tx power
			 * and max power derived from above mentioned parameters.
			 */
			if (ar->max_allowed_tx_power && ab->hw_params->idle_ps)
				max_tx_power[pwr_lvl_idx] =
					min_t(s8,
					      max_tx_power[pwr_lvl_idx],
					      ar->max_allowed_tx_power);
		}
		reg_tpc_info->chan_power_info[pwr_lvl_idx].chan_cfreq = center_freq;
		reg_tpc_info->chan_power_info[pwr_lvl_idx].tx_power =
			max_tx_power[pwr_lvl_idx];
	}

	reg_tpc_info->num_pwr_levels = num_pwr_levels;
	reg_tpc_info->is_psd_power = is_psd_power;
	reg_tpc_info->eirp_power = eirp_power;
	if (ahvif->vdev_type == WMI_VDEV_TYPE_STA)
		reg_6ghz_power_mode = bss_conf->power_type;
	else
		/* For now, LPI is the only supported AP power mode */
		reg_6ghz_power_mode = IEEE80211_REG_LPI_AP;

	reg_tpc_info->ap_power_type =
		ath12k_reg_ap_pwr_convert(reg_6ghz_power_mode);
}

static void ath12k_mac_parse_tx_pwr_env(struct ath12k *ar,
					struct ath12k_link_vif *arvif)
{
	struct ieee80211_bss_conf *bss_conf = ath12k_mac_get_link_bss_conf(arvif);
	struct ath12k_reg_tpc_power_info *tpc_info = &arvif->reg_tpc_info;
	struct ieee80211_parsed_tpe_eirp *local_non_psd, *reg_non_psd;
	struct ieee80211_parsed_tpe_psd *local_psd, *reg_psd;
	struct ieee80211_parsed_tpe *tpe = &bss_conf->tpe;
	enum wmi_reg_6g_client_type client_type;
	struct ath12k_reg_info *reg_info;
	struct ath12k_base *ab = ar->ab;
	bool psd_valid, non_psd_valid;
	int i;

	reg_info = ab->reg_info[ar->pdev_idx];
	client_type = reg_info->client_type;

	local_psd = &tpe->psd_local[client_type];
	reg_psd = &tpe->psd_reg_client[client_type];
	local_non_psd = &tpe->max_local[client_type];
	reg_non_psd = &tpe->max_reg_client[client_type];

	psd_valid = local_psd->valid | reg_psd->valid;
	non_psd_valid = local_non_psd->valid | reg_non_psd->valid;

	if (!psd_valid && !non_psd_valid) {
		ath12k_warn(ab,
			    "no transmit power envelope match client power type %d\n",
			    client_type);
		return;
	};

	if (psd_valid) {
		tpc_info->is_psd_power = true;

		tpc_info->num_pwr_levels = max(local_psd->count,
					       reg_psd->count);
		if (tpc_info->num_pwr_levels > ATH12K_NUM_PWR_LEVELS)
			tpc_info->num_pwr_levels = ATH12K_NUM_PWR_LEVELS;

		for (i = 0; i < tpc_info->num_pwr_levels; i++) {
			tpc_info->tpe[i] = min(local_psd->power[i],
					       reg_psd->power[i]) / 2;
			ath12k_dbg(ab, ATH12K_DBG_MAC,
				   "TPE PSD power[%d] : %d\n",
				   i, tpc_info->tpe[i]);
		}
	} else {
		tpc_info->is_psd_power = false;
		tpc_info->eirp_power = 0;

		tpc_info->num_pwr_levels = max(local_non_psd->count,
					       reg_non_psd->count);
		if (tpc_info->num_pwr_levels > ATH12K_NUM_PWR_LEVELS)
			tpc_info->num_pwr_levels = ATH12K_NUM_PWR_LEVELS;

		for (i = 0; i < tpc_info->num_pwr_levels; i++) {
			tpc_info->tpe[i] = min(local_non_psd->power[i],
					       reg_non_psd->power[i]) / 2;
			ath12k_dbg(ab, ATH12K_DBG_MAC,
				   "non PSD power[%d] : %d\n",
				   i, tpc_info->tpe[i]);
		}
	}
}

static int
ath12k_mac_op_assign_vif_chanctx(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *link_conf,
				 struct ieee80211_chanctx_conf *ctx)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar;
	struct ath12k_base *ab;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	u8 link_id = link_conf->link_id;
	struct ath12k_link_vif *arvif;
	int ret;

	lockdep_assert_wiphy(hw->wiphy);

	/* For multi radio wiphy, the vdev was not created during add_interface
	 * create now since we have a channel ctx now to assign to a specific ar/fw
	 */
	arvif = ath12k_mac_assign_link_vif(ah, vif, link_id);
	if (!arvif) {
		WARN_ON(1);
		return -ENOMEM;
	}

	ar = ath12k_mac_assign_vif_to_vdev(hw, arvif, ctx);
	if (!ar) {
		ath12k_hw_warn(ah, "failed to assign chanctx for vif %pM link id %u link vif is already started",
			       vif->addr, link_id);
		return -EINVAL;
	}

	ab = ar->ab;

	ath12k_dbg(ab, ATH12K_DBG_MAC,
		   "mac chanctx assign ptr %p vdev_id %i\n",
		   ctx, arvif->vdev_id);

	if (ath12k_wmi_supports_6ghz_cc_ext(ar) &&
	    ctx->def.chan->band == NL80211_BAND_6GHZ &&
	    ahvif->vdev_type == WMI_VDEV_TYPE_STA)
		ath12k_mac_parse_tx_pwr_env(ar, arvif);

	arvif->punct_bitmap = ctx->def.punctured;

	/* for some targets bss peer must be created before vdev_start */
	if (ab->hw_params->vdev_start_delay &&
	    ahvif->vdev_type != WMI_VDEV_TYPE_AP &&
	    ahvif->vdev_type != WMI_VDEV_TYPE_MONITOR &&
	    !ath12k_peer_exist_by_vdev_id(ab, arvif->vdev_id)) {
		ret = 0;
		goto out;
	}

	if (WARN_ON(arvif->is_started)) {
		ret = -EBUSY;
		goto out;
	}

	if (ahvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ret = ath12k_mac_monitor_start(ar);
		if (ret) {
			ath12k_mac_monitor_vdev_delete(ar);
			goto out;
		}

		arvif->is_started = true;
		goto out;
	}

	ret = ath12k_mac_vdev_start(arvif, ctx);
	if (ret) {
		ath12k_warn(ab, "failed to start vdev %i addr %pM on freq %d: %d\n",
			    arvif->vdev_id, vif->addr,
			    ctx->def.chan->center_freq, ret);
		goto out;
	}

	arvif->is_started = true;

	/* TODO: Setup ps and cts/rts protection */

out:
	return ret;
}

static void
ath12k_mac_op_unassign_vif_chanctx(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *link_conf,
				   struct ieee80211_chanctx_conf *ctx)
{
	struct ath12k *ar;
	struct ath12k_base *ab;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif;
	u8 link_id = link_conf->link_id;
	int ret;

	lockdep_assert_wiphy(hw->wiphy);

	arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);

	/* The vif is expected to be attached to an ar's VDEV.
	 * We leave the vif/vdev in this function as is
	 * and not delete the vdev symmetric to assign_vif_chanctx()
	 * the VDEV will be deleted and unassigned either during
	 * remove_interface() or when there is a change in channel
	 * that moves the vif to a new ar
	 */
	if (!arvif || !arvif->is_created)
		return;

	ar = arvif->ar;
	ab = ar->ab;

	ath12k_dbg(ab, ATH12K_DBG_MAC,
		   "mac chanctx unassign ptr %p vdev_id %i\n",
		   ctx, arvif->vdev_id);

	WARN_ON(!arvif->is_started);

	if (ahvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ret = ath12k_mac_monitor_stop(ar);
		if (ret)
			return;

		arvif->is_started = false;
	}

	if (ahvif->vdev_type != WMI_VDEV_TYPE_STA &&
	    ahvif->vdev_type != WMI_VDEV_TYPE_MONITOR) {
		ath12k_bss_disassoc(ar, arvif);
		ret = ath12k_mac_vdev_stop(arvif);
		if (ret)
			ath12k_warn(ab, "failed to stop vdev %i: %d\n",
				    arvif->vdev_id, ret);
	}
	arvif->is_started = false;

	if (test_bit(WMI_TLV_SERVICE_11D_OFFLOAD, ab->wmi_ab.svc_map) &&
	    ahvif->vdev_type == WMI_VDEV_TYPE_STA &&
	    ahvif->vdev_subtype == WMI_VDEV_SUBTYPE_NONE &&
	    ar->state_11d != ATH12K_11D_PREPARING) {
		reinit_completion(&ar->completed_11d_scan);
		ar->state_11d = ATH12K_11D_PREPARING;
	}

	if (ar->scan.arvif == arvif && ar->scan.state == ATH12K_SCAN_RUNNING) {
		ath12k_scan_abort(ar);
		ar->scan.arvif = NULL;
	}
}

static int
ath12k_mac_op_switch_vif_chanctx(struct ieee80211_hw *hw,
				 struct ieee80211_vif_chanctx_switch *vifs,
				 int n_vifs,
				 enum ieee80211_chanctx_switch_mode mode)
{
	struct ath12k *ar;

	lockdep_assert_wiphy(hw->wiphy);

	ar = ath12k_get_ar_by_ctx(hw, vifs->old_ctx);
	if (!ar)
		return -EINVAL;

	/* Switching channels across radio is not allowed */
	if (ar != ath12k_get_ar_by_ctx(hw, vifs->new_ctx))
		return -EINVAL;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "mac chanctx switch n_vifs %d mode %d\n",
		   n_vifs, mode);
	ath12k_mac_update_vif_chan(ar, vifs, n_vifs);

	return 0;
}

static int
ath12k_set_vdev_param_to_all_vifs(struct ath12k *ar, int param, u32 value)
{
	struct ath12k_link_vif *arvif;
	int ret = 0;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "setting mac vdev %d param %d value %d\n",
			   param, arvif->vdev_id, value);

		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    param, value);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set param %d for vdev %d: %d\n",
				    param, arvif->vdev_id, ret);
			break;
		}
	}

	return ret;
}

/* mac80211 stores device specific RTS/Fragmentation threshold value,
 * this is set interface specific to firmware from ath12k driver
 */
static int ath12k_mac_op_set_rts_threshold(struct ieee80211_hw *hw,
					   int radio_idx, u32 value)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar;
	int param_id = WMI_VDEV_PARAM_RTS_THRESHOLD, ret = 0, i;

	lockdep_assert_wiphy(hw->wiphy);

	/* Currently we set the rts threshold value to all the vifs across
	 * all radios of the single wiphy.
	 * TODO Once support for vif specific RTS threshold in mac80211 is
	 * available, ath12k can make use of it.
	 */
	for_each_ar(ah, ar, i) {
		ret = ath12k_set_vdev_param_to_all_vifs(ar, param_id, value);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set RTS config for all vdevs of pdev %d",
				    ar->pdev->pdev_id);
			break;
		}
	}

	return ret;
}

static int ath12k_mac_op_set_frag_threshold(struct ieee80211_hw *hw,
					    int radio_idx, u32 value)
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

	lockdep_assert_wiphy(hw->wiphy);

	return -EOPNOTSUPP;
}

static int ath12k_mac_flush(struct ath12k *ar)
{
	long time_left;
	int ret = 0;

	time_left = wait_event_timeout(ar->dp.tx_empty_waitq,
				       (atomic_read(&ar->dp.num_tx_pending) == 0),
				       ATH12K_FLUSH_TIMEOUT);
	if (time_left == 0) {
		ath12k_warn(ar->ab,
			    "failed to flush transmit queue, data pkts pending %d\n",
			    atomic_read(&ar->dp.num_tx_pending));
		ret = -ETIMEDOUT;
	}

	time_left = wait_event_timeout(ar->txmgmt_empty_waitq,
				       (atomic_read(&ar->num_pending_mgmt_tx) == 0),
				       ATH12K_FLUSH_TIMEOUT);
	if (time_left == 0) {
		ath12k_warn(ar->ab,
			    "failed to flush mgmt transmit queue, mgmt pkts pending %d\n",
			    atomic_read(&ar->num_pending_mgmt_tx));
		ret = -ETIMEDOUT;
	}

	return ret;
}

int ath12k_mac_wait_tx_complete(struct ath12k *ar)
{
	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ath12k_mac_drain_tx(ar);
	return ath12k_mac_flush(ar);
}

static void ath12k_mac_op_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
				u32 queues, bool drop)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k_link_vif *arvif;
	struct ath12k_vif *ahvif;
	unsigned long links;
	struct ath12k *ar;
	u8 link_id;
	int i;

	lockdep_assert_wiphy(hw->wiphy);

	if (drop)
		return;

	/* vif can be NULL when flush() is considered for hw */
	if (!vif) {
		for_each_ar(ah, ar, i)
			ath12k_mac_flush(ar);
		return;
	}

	for_each_ar(ah, ar, i)
		wiphy_work_flush(hw->wiphy, &ar->wmi_mgmt_tx_work);

	ahvif = ath12k_vif_to_ahvif(vif);
	links = ahvif->links_map;
	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_id]);
		if (!(arvif && arvif->ar))
			continue;

		ath12k_mac_flush(arvif->ar);
	}
}

static int
ath12k_mac_bitrate_mask_num_ht_rates(struct ath12k *ar,
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
ath12k_mac_has_single_legacy_rate(struct ath12k *ar,
				  enum nl80211_band band,
				  const struct cfg80211_bitrate_mask *mask)
{
	int num_rates = 0;

	num_rates = hweight32(mask->control[band].legacy);

	if (ath12k_mac_bitrate_mask_num_ht_rates(ar, band, mask))
		return false;

	if (ath12k_mac_bitrate_mask_num_vht_rates(ar, band, mask))
		return false;

	if (ath12k_mac_bitrate_mask_num_he_rates(ar, band, mask))
		return false;

	return num_rates == 1;
}

static __le16
ath12k_mac_get_tx_mcs_map(const struct ieee80211_sta_he_cap *he_cap)
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
ath12k_mac_bitrate_mask_get_single_nss(struct ath12k *ar,
				       struct ieee80211_vif *vif,
				       enum nl80211_band band,
				       const struct cfg80211_bitrate_mask *mask,
				       int *nss)
{
	struct ieee80211_supported_band *sband = &ar->mac.sbands[band];
	u16 vht_mcs_map = le16_to_cpu(sband->vht_cap.vht_mcs.tx_mcs_map);
	const struct ieee80211_sta_he_cap *he_cap;
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
			 ath12k_mac_get_max_vht_mcs_map(vht_mcs_map, i))
			vht_nss_mask |= BIT(i);
		else
			return false;
	}

	he_cap = ieee80211_get_he_iftype_cap_vif(sband, vif);
	if (!he_cap)
		return false;

	he_mcs_map = le16_to_cpu(ath12k_mac_get_tx_mcs_map(he_cap));

	for (i = 0; i < ARRAY_SIZE(mask->control[band].he_mcs); i++) {
		if (mask->control[band].he_mcs[i] == 0)
			continue;

		if (mask->control[band].he_mcs[i] ==
		    ath12k_mac_get_max_he_mcs_map(he_mcs_map, i))
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
ath12k_mac_get_single_legacy_rate(struct ath12k *ar,
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
		rate_idx += ATH12K_MAC_FIRST_OFDM_RATE_IDX;

	hw_rate = ath12k_legacy_rates[rate_idx].hw_value;
	bitrate = ath12k_legacy_rates[rate_idx].bitrate;

	if (ath12k_mac_bitrate_is_cck(bitrate))
		preamble = WMI_RATE_PREAMBLE_CCK;
	else
		preamble = WMI_RATE_PREAMBLE_OFDM;

	*nss = 1;
	*rate = ATH12K_HW_RATE_CODE(hw_rate, 0, preamble);

	return 0;
}

static int
ath12k_mac_set_fixed_rate_gi_ltf(struct ath12k_link_vif *arvif, u8 he_gi, u8 he_ltf)
{
	struct ath12k *ar = arvif->ar;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	/* 0.8 = 0, 1.6 = 2 and 3.2 = 3. */
	if (he_gi && he_gi != 0xFF)
		he_gi += 1;

	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_SGI, he_gi);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set HE GI:%d, error:%d\n",
			    he_gi, ret);
		return ret;
	}
	/* start from 1 */
	if (he_ltf != 0xFF)
		he_ltf += 1;

	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_HE_LTF, he_ltf);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set HE LTF:%d, error:%d\n",
			    he_ltf, ret);
		return ret;
	}
	return 0;
}

static int
ath12k_mac_set_auto_rate_gi_ltf(struct ath12k_link_vif *arvif, u16 he_gi, u8 he_ltf)
{
	struct ath12k *ar = arvif->ar;
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
			ath12k_warn(ar->ab, "Invalid GI\n");
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
			ath12k_warn(ar->ab, "Invalid LTF\n");
			return -EINVAL;
		}
	}

	he_ar_gi_ltf = he_gi | he_ltf;

	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_AUTORATE_MISC_CFG,
					    he_ar_gi_ltf);
	if (ret) {
		ath12k_warn(ar->ab,
			    "failed to set HE autorate GI:%u, LTF:%u params, error:%d\n",
			    he_gi, he_ltf, ret);
		return ret;
	}

	return 0;
}

static u32 ath12k_mac_nlgi_to_wmigi(enum nl80211_txrate_gi gi)
{
	switch (gi) {
	case NL80211_TXRATE_DEFAULT_GI:
		return WMI_GI_400_NS;
	case NL80211_TXRATE_FORCE_LGI:
		return WMI_GI_800_NS;
	default:
		return WMI_GI_400_NS;
	}
}

static int ath12k_mac_set_rate_params(struct ath12k_link_vif *arvif,
				      u32 rate, u8 nss, u8 sgi, u8 ldpc,
				      u8 he_gi, u8 he_ltf, bool he_fixed_rate)
{
	struct ieee80211_bss_conf *link_conf;
	struct ath12k *ar = arvif->ar;
	u32 vdev_param;
	u32 param_value;
	int ret;
	bool he_support;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	link_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!link_conf)
		return -EINVAL;

	he_support = link_conf->he_support;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "mac set rate params vdev %i rate 0x%02x nss 0x%02x sgi 0x%02x ldpc 0x%02x\n",
		   arvif->vdev_id, rate, nss, sgi, ldpc);

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "he_gi 0x%02x he_ltf 0x%02x he_fixed_rate %d\n", he_gi,
		   he_ltf, he_fixed_rate);

	if (!he_support) {
		vdev_param = WMI_VDEV_PARAM_FIXED_RATE;
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    vdev_param, rate);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set fixed rate param 0x%02x: %d\n",
				    rate, ret);
			return ret;
		}
	}

	vdev_param = WMI_VDEV_PARAM_NSS;

	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    vdev_param, nss);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set nss param %d: %d\n",
			    nss, ret);
		return ret;
	}

	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_LDPC, ldpc);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set ldpc param %d: %d\n",
			    ldpc, ret);
		return ret;
	}

	if (he_support) {
		if (he_fixed_rate)
			ret = ath12k_mac_set_fixed_rate_gi_ltf(arvif, he_gi, he_ltf);
		else
			ret = ath12k_mac_set_auto_rate_gi_ltf(arvif, he_gi, he_ltf);
		if (ret)
			return ret;
	} else {
		vdev_param = WMI_VDEV_PARAM_SGI;
		param_value = ath12k_mac_nlgi_to_wmigi(sgi);
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
						    vdev_param, param_value);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set sgi param %d: %d\n",
				    sgi, ret);
			return ret;
		}
	}

	return 0;
}

static bool
ath12k_mac_vht_mcs_range_present(struct ath12k *ar,
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
ath12k_mac_he_mcs_range_present(struct ath12k *ar,
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

static void ath12k_mac_set_bitrate_mask_iter(void *data,
					     struct ieee80211_sta *sta)
{
	struct ath12k_link_vif *arvif = data;
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);
	struct ath12k_link_sta *arsta;
	struct ath12k *ar = arvif->ar;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	arsta = wiphy_dereference(ath12k_ar_to_hw(ar)->wiphy,
				  ahsta->link[arvif->link_id]);
	if (!arsta || arsta->arvif != arvif)
		return;

	spin_lock_bh(&ar->data_lock);
	arsta->changed |= IEEE80211_RC_SUPP_RATES_CHANGED;
	spin_unlock_bh(&ar->data_lock);

	wiphy_work_queue(ath12k_ar_to_hw(ar)->wiphy, &arsta->update_wk);
}

static void ath12k_mac_disable_peer_fixed_rate(void *data,
					       struct ieee80211_sta *sta)
{
	struct ath12k_link_vif *arvif = data;
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);
	struct ath12k_link_sta *arsta;
	struct ath12k *ar = arvif->ar;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	arsta = wiphy_dereference(ath12k_ar_to_hw(ar)->wiphy,
				  ahsta->link[arvif->link_id]);

	if (!arsta || arsta->arvif != arvif)
		return;

	ret = ath12k_wmi_set_peer_param(ar, arsta->addr,
					arvif->vdev_id,
					WMI_PEER_PARAM_FIXED_RATE,
					WMI_FIXED_RATE_NONE);
	if (ret)
		ath12k_warn(ar->ab,
			    "failed to disable peer fixed rate for STA %pM ret %d\n",
			    arsta->addr, ret);
}

static bool
ath12k_mac_validate_fixed_rate_settings(struct ath12k *ar, enum nl80211_band band,
					const struct cfg80211_bitrate_mask *mask,
					unsigned int link_id)
{
	bool he_fixed_rate = false, vht_fixed_rate = false;
	const u16 *vht_mcs_mask, *he_mcs_mask;
	struct ieee80211_link_sta *link_sta;
	struct ath12k_peer *peer, *tmp;
	u8 vht_nss, he_nss;
	int ret = true;

	vht_mcs_mask = mask->control[band].vht_mcs;
	he_mcs_mask = mask->control[band].he_mcs;

	if (ath12k_mac_bitrate_mask_num_vht_rates(ar, band, mask) == 1)
		vht_fixed_rate = true;

	if (ath12k_mac_bitrate_mask_num_he_rates(ar, band, mask) == 1)
		he_fixed_rate = true;

	if (!vht_fixed_rate && !he_fixed_rate)
		return true;

	vht_nss = ath12k_mac_max_vht_nss(vht_mcs_mask);
	he_nss =  ath12k_mac_max_he_nss(he_mcs_mask);

	rcu_read_lock();
	spin_lock_bh(&ar->ab->base_lock);
	list_for_each_entry_safe(peer, tmp, &ar->ab->peers, list) {
		if (peer->sta) {
			link_sta = rcu_dereference(peer->sta->link[link_id]);
			if (!link_sta) {
				ret = false;
				goto exit;
			}

			if (vht_fixed_rate && (!link_sta->vht_cap.vht_supported ||
					       link_sta->rx_nss < vht_nss)) {
				ret = false;
				goto exit;
			}
			if (he_fixed_rate && (!link_sta->he_cap.has_he ||
					      link_sta->rx_nss < he_nss)) {
				ret = false;
				goto exit;
			}
		}
	}
exit:
	spin_unlock_bh(&ar->ab->base_lock);
	rcu_read_unlock();
	return ret;
}

static int
ath12k_mac_op_set_bitrate_mask(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       const struct cfg80211_bitrate_mask *mask)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif;
	struct cfg80211_chan_def def;
	struct ath12k *ar;
	enum nl80211_band band;
	const u8 *ht_mcs_mask;
	const u16 *vht_mcs_mask;
	const u16 *he_mcs_mask;
	u8 he_ltf = 0;
	u8 he_gi = 0;
	u32 rate;
	u8 nss, mac_nss;
	u8 sgi;
	u8 ldpc;
	int single_nss;
	int ret;
	int num_rates;
	bool he_fixed_rate = false;

	lockdep_assert_wiphy(hw->wiphy);

	arvif = &ahvif->deflink;

	ar = arvif->ar;
	if (ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)) {
		ret = -EPERM;
		goto out;
	}

	band = def.chan->band;
	ht_mcs_mask = mask->control[band].ht_mcs;
	vht_mcs_mask = mask->control[band].vht_mcs;
	he_mcs_mask = mask->control[band].he_mcs;
	ldpc = !!(ar->ht_cap_info & WMI_HT_CAP_LDPC);

	sgi = mask->control[band].gi;
	if (sgi == NL80211_TXRATE_FORCE_SGI) {
		ret = -EINVAL;
		goto out;
	}

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
	if (ath12k_mac_has_single_legacy_rate(ar, band, mask)) {
		ret = ath12k_mac_get_single_legacy_rate(ar, band, mask, &rate,
							&nss);
		if (ret) {
			ath12k_warn(ar->ab, "failed to get single legacy rate for vdev %i: %d\n",
				    arvif->vdev_id, ret);
			goto out;
		}

		ieee80211_iterate_stations_mtx(hw,
					       ath12k_mac_disable_peer_fixed_rate,
					       arvif);
	} else if (ath12k_mac_bitrate_mask_get_single_nss(ar, vif, band, mask,
							  &single_nss)) {
		rate = WMI_FIXED_RATE_NONE;
		nss = single_nss;
		arvif->bitrate_mask = *mask;

		ieee80211_iterate_stations_atomic(hw,
						  ath12k_mac_set_bitrate_mask_iter,
						  arvif);
	} else {
		rate = WMI_FIXED_RATE_NONE;

		if (!ath12k_mac_validate_fixed_rate_settings(ar, band,
							     mask, arvif->link_id))
			ath12k_warn(ar->ab,
				    "failed to update fixed rate settings due to mcs/nss incompatibility\n");

		mac_nss = max3(ath12k_mac_max_ht_nss(ht_mcs_mask),
			       ath12k_mac_max_vht_nss(vht_mcs_mask),
			       ath12k_mac_max_he_nss(he_mcs_mask));
		nss = min_t(u32, ar->num_tx_chains, mac_nss);

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

		num_rates = ath12k_mac_bitrate_mask_num_vht_rates(ar, band,
								  mask);

		if (!ath12k_mac_vht_mcs_range_present(ar, band, mask) &&
		    num_rates > 1) {
			/* TODO: Handle multiple VHT MCS values setting using
			 * RATEMASK CMD
			 */
			ath12k_warn(ar->ab,
				    "Setting more than one MCS Value in bitrate mask not supported\n");
			ret = -EINVAL;
			goto out;
		}

		num_rates = ath12k_mac_bitrate_mask_num_he_rates(ar, band, mask);
		if (num_rates == 1)
			he_fixed_rate = true;

		if (!ath12k_mac_he_mcs_range_present(ar, band, mask) &&
		    num_rates > 1) {
			ath12k_warn(ar->ab,
				    "Setting more than one HE MCS Value in bitrate mask not supported\n");
			ret = -EINVAL;
			goto out;
		}
		ieee80211_iterate_stations_mtx(hw,
					       ath12k_mac_disable_peer_fixed_rate,
					       arvif);

		arvif->bitrate_mask = *mask;
		ieee80211_iterate_stations_mtx(hw,
					       ath12k_mac_set_bitrate_mask_iter,
					       arvif);
	}

	ret = ath12k_mac_set_rate_params(arvif, rate, nss, sgi, ldpc, he_gi,
					 he_ltf, he_fixed_rate);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set rate params on vdev %i: %d\n",
			    arvif->vdev_id, ret);
	}

out:
	return ret;
}

static void
ath12k_mac_op_reconfig_complete(struct ieee80211_hw *hw,
				enum ieee80211_reconfig_type reconfig_type)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar;
	struct ath12k_base *ab;
	struct ath12k_vif *ahvif;
	struct ath12k_link_vif *arvif;
	int recovery_count, i;

	lockdep_assert_wiphy(hw->wiphy);

	if (reconfig_type != IEEE80211_RECONFIG_TYPE_RESTART)
		return;

	guard(mutex)(&ah->hw_mutex);

	if (ah->state != ATH12K_HW_STATE_RESTARTED)
		return;

	ah->state = ATH12K_HW_STATE_ON;
	ieee80211_wake_queues(hw);

	for_each_ar(ah, ar, i) {
		ab = ar->ab;

		ath12k_warn(ar->ab, "pdev %d successfully recovered\n",
			    ar->pdev->pdev_id);

		if (ar->ab->hw_params->current_cc_support &&
		    ar->alpha2[0] != 0 && ar->alpha2[1] != 0) {
			struct wmi_set_current_country_arg arg = {};

			memcpy(&arg.alpha2, ar->alpha2, 2);
			reinit_completion(&ar->regd_update_completed);
			ath12k_wmi_send_set_current_country_cmd(ar, &arg);
		}

		if (ab->is_reset) {
			recovery_count = atomic_inc_return(&ab->recovery_count);

			ath12k_dbg(ab, ATH12K_DBG_BOOT, "recovery count %d\n",
				   recovery_count);

			/* When there are multiple radios in an SOC,
			 * the recovery has to be done for each radio
			 */
			if (recovery_count == ab->num_radios) {
				atomic_dec(&ab->reset_count);
				complete(&ab->reset_complete);
				ab->is_reset = false;
				atomic_set(&ab->fail_cont_count, 0);
				ath12k_dbg(ab, ATH12K_DBG_BOOT, "reset success\n");
			}
		}

		list_for_each_entry(arvif, &ar->arvifs, list) {
			ahvif = arvif->ahvif;
			ath12k_dbg(ab, ATH12K_DBG_BOOT,
				   "reconfig cipher %d up %d vdev type %d\n",
				   ahvif->key_cipher,
				   arvif->is_up,
				   ahvif->vdev_type);

			/* After trigger disconnect, then upper layer will
			 * trigger connect again, then the PN number of
			 * upper layer will be reset to keep up with AP
			 * side, hence PN number mismatch will not happen.
			 */
			if (arvif->is_up &&
			    ahvif->vdev_type == WMI_VDEV_TYPE_STA &&
			    ahvif->vdev_subtype == WMI_VDEV_SUBTYPE_NONE) {
				ieee80211_hw_restart_disconnect(ahvif->vif);

				ath12k_dbg(ab, ATH12K_DBG_BOOT,
					   "restart disconnect\n");
			}
		}
	}
}

static void
ath12k_mac_update_bss_chan_survey(struct ath12k *ar,
				  struct ieee80211_channel *channel)
{
	int ret;
	enum wmi_bss_chan_info_req_type type = WMI_BSS_SURVEY_REQ_TYPE_READ;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (!test_bit(WMI_TLV_SERVICE_BSS_CHANNEL_INFO_64, ar->ab->wmi_ab.svc_map) ||
	    ar->rx_channel != channel)
		return;

	if (ar->scan.state != ATH12K_SCAN_IDLE) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
			   "ignoring bss chan info req while scanning..\n");
		return;
	}

	reinit_completion(&ar->bss_survey_done);

	ret = ath12k_wmi_pdev_bss_chan_info_request(ar, type);
	if (ret) {
		ath12k_warn(ar->ab, "failed to send pdev bss chan info request\n");
		return;
	}

	ret = wait_for_completion_timeout(&ar->bss_survey_done, 3 * HZ);
	if (ret == 0)
		ath12k_warn(ar->ab, "bss channel survey timed out\n");
}

static int ath12k_mac_op_get_survey(struct ieee80211_hw *hw, int idx,
				    struct survey_info *survey)
{
	struct ath12k *ar;
	struct ieee80211_supported_band *sband;
	struct survey_info *ar_survey;

	lockdep_assert_wiphy(hw->wiphy);

	if (idx >= ATH12K_NUM_CHANS)
		return -ENOENT;

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

	if (!sband || idx >= sband->n_channels)
		return -ENOENT;

	ar = ath12k_mac_get_ar_by_chan(hw, &sband->channels[idx]);
	if (!ar) {
		if (sband->channels[idx].flags & IEEE80211_CHAN_DISABLED) {
			memset(survey, 0, sizeof(*survey));
			return 0;
		}
		return -ENOENT;
	}

	ar_survey = &ar->survey[idx];

	ath12k_mac_update_bss_chan_survey(ar, &sband->channels[idx]);

	spin_lock_bh(&ar->data_lock);
	memcpy(survey, ar_survey, sizeof(*survey));
	spin_unlock_bh(&ar->data_lock);

	survey->channel = &sband->channels[idx];

	if (ar->rx_channel == survey->channel)
		survey->filled |= SURVEY_INFO_IN_USE;

	return 0;
}

static void ath12k_mac_op_sta_statistics(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta,
					 struct station_info *sinfo)
{
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);
	struct ath12k_fw_stats_req_params params = {};
	struct ath12k_link_sta *arsta;
	s8 signal, noise_floor;
	struct ath12k *ar;
	bool db2dbm;

	lockdep_assert_wiphy(hw->wiphy);

	arsta = &ahsta->deflink;
	ar = ath12k_get_ar_by_vif(hw, vif, arsta->link_id);
	if (!ar)
		return;

	db2dbm = test_bit(WMI_TLV_SERVICE_HW_DB2DBM_CONVERSION_SUPPORT,
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
			sinfo->txrate.eht_gi = arsta->txrate.eht_gi;
			sinfo->txrate.eht_ru_alloc = arsta->txrate.eht_ru_alloc;
		}
		sinfo->txrate.flags = arsta->txrate.flags;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
	}

	/* TODO: Use real NF instead of default one. */
	signal = arsta->rssi_comb;

	params.pdev_id = ar->pdev->pdev_id;
	params.vdev_id = 0;
	params.stats_id = WMI_REQUEST_VDEV_STAT;

	if (!signal &&
	    ahsta->ahvif->vdev_type == WMI_VDEV_TYPE_STA &&
	    !(ath12k_mac_get_fw_stats(ar, &params)))
		signal = arsta->rssi_beacon;

	spin_lock_bh(&ar->data_lock);
	noise_floor = ath12k_pdev_get_noise_floor(ar);
	spin_unlock_bh(&ar->data_lock);

	if (signal) {
		sinfo->signal = db2dbm ? signal : signal + noise_floor;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);
	}

	sinfo->signal_avg = ewma_avg_rssi_read(&arsta->avg_rssi);

	if (!db2dbm)
		sinfo->signal_avg += noise_floor;

	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL_AVG);
}

static int ath12k_mac_op_cancel_remain_on_channel(struct ieee80211_hw *hw,
						  struct ieee80211_vif *vif)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar;

	ar = ath12k_ah_to_ar(ah, 0);

	lockdep_assert_wiphy(hw->wiphy);

	spin_lock_bh(&ar->data_lock);
	ar->scan.roc_notify = false;
	spin_unlock_bh(&ar->data_lock);

	ath12k_scan_abort(ar);

	cancel_delayed_work_sync(&ar->scan.timeout);
	wiphy_work_cancel(hw->wiphy, &ar->scan.vdev_clean_wk);

	return 0;
}

static int ath12k_mac_op_remain_on_channel(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_channel *chan,
					   int duration,
					   enum ieee80211_roc_type type)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k_link_vif *arvif;
	struct ath12k *ar;
	u32 scan_time_msec;
	bool create = true;
	u8 link_id;
	int ret;

	lockdep_assert_wiphy(hw->wiphy);

	ar = ath12k_mac_select_scan_device(hw, vif, chan->center_freq);
	if (!ar)
		return -EINVAL;

	/* check if any of the links of ML VIF is already started on
	 * radio(ar) corresponding to given scan frequency and use it,
	 * if not use deflink(link 0) for scan purpose.
	 */

	link_id = ath12k_mac_find_link_id_by_ar(ahvif, ar);
	arvif = ath12k_mac_assign_link_vif(ah, vif, link_id);
	/* If the vif is already assigned to a specific vdev of an ar,
	 * check whether its already started, vdev which is started
	 * are not allowed to switch to a new radio.
	 * If the vdev is not started, but was earlier created on a
	 * different ar, delete that vdev and create a new one. We don't
	 * delete at the scan stop as an optimization to avoid redundant
	 * delete-create vdev's for the same ar, in case the request is
	 * always on the same band for the vif
	 */
	if (arvif->is_created) {
		if (WARN_ON(!arvif->ar))
			return -EINVAL;

		if (ar != arvif->ar && arvif->is_started)
			return -EBUSY;

		if (ar != arvif->ar) {
			ath12k_mac_remove_link_interface(hw, arvif);
			ath12k_mac_unassign_link_vif(arvif);
		} else {
			create = false;
		}
	}

	if (create) {
		arvif = ath12k_mac_assign_link_vif(ah, vif, link_id);

		ret = ath12k_mac_vdev_create(ar, arvif);
		if (ret) {
			ath12k_warn(ar->ab, "unable to create scan vdev for roc: %d\n",
				    ret);
			return ret;
		}
	}

	spin_lock_bh(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH12K_SCAN_IDLE:
		reinit_completion(&ar->scan.started);
		reinit_completion(&ar->scan.completed);
		reinit_completion(&ar->scan.on_channel);
		ar->scan.state = ATH12K_SCAN_STARTING;
		ar->scan.is_roc = true;
		ar->scan.arvif = arvif;
		ar->scan.roc_freq = chan->center_freq;
		ar->scan.roc_notify = true;
		ret = 0;
		break;
	case ATH12K_SCAN_STARTING:
	case ATH12K_SCAN_RUNNING:
	case ATH12K_SCAN_ABORTING:
		ret = -EBUSY;
		break;
	}

	spin_unlock_bh(&ar->data_lock);

	if (ret)
		return ret;

	scan_time_msec = hw->wiphy->max_remain_on_channel_duration * 2;

	struct ath12k_wmi_scan_req_arg *arg __free(kfree) =
					kzalloc(sizeof(*arg), GFP_KERNEL);
	if (!arg)
		return -ENOMEM;

	ath12k_wmi_start_scan_init(ar, arg);
	arg->num_chan = 1;

	u32 *chan_list __free(kfree) = kcalloc(arg->num_chan, sizeof(*chan_list),
					       GFP_KERNEL);
	if (!chan_list)
		return -ENOMEM;

	arg->chan_list = chan_list;
	arg->vdev_id = arvif->vdev_id;
	arg->scan_id = ATH12K_SCAN_ID;
	arg->chan_list[0] = chan->center_freq;
	arg->dwell_time_active = scan_time_msec;
	arg->dwell_time_passive = scan_time_msec;
	arg->max_scan_time = scan_time_msec;
	arg->scan_f_passive = 1;
	arg->burst_duration = duration;

	ret = ath12k_start_scan(ar, arg);
	if (ret) {
		ath12k_warn(ar->ab, "failed to start roc scan: %d\n", ret);

		spin_lock_bh(&ar->data_lock);
		ar->scan.state = ATH12K_SCAN_IDLE;
		spin_unlock_bh(&ar->data_lock);
		return ret;
	}

	ret = wait_for_completion_timeout(&ar->scan.on_channel, 3 * HZ);
	if (ret == 0) {
		ath12k_warn(ar->ab, "failed to switch to channel for roc scan\n");
		ret = ath12k_scan_stop(ar);
		if (ret)
			ath12k_warn(ar->ab, "failed to stop scan: %d\n", ret);
		return -ETIMEDOUT;
	}

	ieee80211_queue_delayed_work(hw, &ar->scan.timeout,
				     msecs_to_jiffies(duration));

	return 0;
}

static void ath12k_mac_op_set_rekey_data(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct cfg80211_gtk_rekey_data *data)
{
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_rekey_data *rekey_data;
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar = ath12k_ah_to_ar(ah, 0);
	struct ath12k_link_vif *arvif;

	lockdep_assert_wiphy(hw->wiphy);

	arvif = &ahvif->deflink;
	rekey_data = &arvif->rekey_data;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac set rekey data vdev %d\n",
		   arvif->vdev_id);

	memcpy(rekey_data->kck, data->kck, NL80211_KCK_LEN);
	memcpy(rekey_data->kek, data->kek, NL80211_KEK_LEN);

	/* The supplicant works on big-endian, the firmware expects it on
	 * little endian.
	 */
	rekey_data->replay_ctr = get_unaligned_be64(data->replay_ctr);

	arvif->rekey_data.enable_offload = true;

	ath12k_dbg_dump(ar->ab, ATH12K_DBG_MAC, "kck", NULL,
			rekey_data->kck, NL80211_KCK_LEN);
	ath12k_dbg_dump(ar->ab, ATH12K_DBG_MAC, "kek", NULL,
			rekey_data->kck, NL80211_KEK_LEN);
	ath12k_dbg_dump(ar->ab, ATH12K_DBG_MAC, "replay ctr", NULL,
			&rekey_data->replay_ctr, sizeof(rekey_data->replay_ctr));
}

static const struct ieee80211_ops ath12k_ops = {
	.tx				= ath12k_mac_op_tx,
	.wake_tx_queue			= ieee80211_handle_wake_tx_queue,
	.start                          = ath12k_mac_op_start,
	.stop                           = ath12k_mac_op_stop,
	.reconfig_complete              = ath12k_mac_op_reconfig_complete,
	.add_interface                  = ath12k_mac_op_add_interface,
	.remove_interface		= ath12k_mac_op_remove_interface,
	.update_vif_offload		= ath12k_mac_op_update_vif_offload,
	.config                         = ath12k_mac_op_config,
	.link_info_changed              = ath12k_mac_op_link_info_changed,
	.vif_cfg_changed		= ath12k_mac_op_vif_cfg_changed,
	.change_vif_links               = ath12k_mac_op_change_vif_links,
	.configure_filter		= ath12k_mac_op_configure_filter,
	.hw_scan                        = ath12k_mac_op_hw_scan,
	.cancel_hw_scan                 = ath12k_mac_op_cancel_hw_scan,
	.set_key                        = ath12k_mac_op_set_key,
	.set_rekey_data	                = ath12k_mac_op_set_rekey_data,
	.sta_state                      = ath12k_mac_op_sta_state,
	.sta_set_txpwr			= ath12k_mac_op_sta_set_txpwr,
	.link_sta_rc_update		= ath12k_mac_op_link_sta_rc_update,
	.conf_tx                        = ath12k_mac_op_conf_tx,
	.set_antenna			= ath12k_mac_op_set_antenna,
	.get_antenna			= ath12k_mac_op_get_antenna,
	.ampdu_action			= ath12k_mac_op_ampdu_action,
	.add_chanctx			= ath12k_mac_op_add_chanctx,
	.remove_chanctx			= ath12k_mac_op_remove_chanctx,
	.change_chanctx			= ath12k_mac_op_change_chanctx,
	.assign_vif_chanctx		= ath12k_mac_op_assign_vif_chanctx,
	.unassign_vif_chanctx		= ath12k_mac_op_unassign_vif_chanctx,
	.switch_vif_chanctx		= ath12k_mac_op_switch_vif_chanctx,
	.get_txpower			= ath12k_mac_op_get_txpower,
	.set_rts_threshold		= ath12k_mac_op_set_rts_threshold,
	.set_frag_threshold		= ath12k_mac_op_set_frag_threshold,
	.set_bitrate_mask		= ath12k_mac_op_set_bitrate_mask,
	.get_survey			= ath12k_mac_op_get_survey,
	.flush				= ath12k_mac_op_flush,
	.sta_statistics			= ath12k_mac_op_sta_statistics,
	.remain_on_channel              = ath12k_mac_op_remain_on_channel,
	.cancel_remain_on_channel       = ath12k_mac_op_cancel_remain_on_channel,
	.change_sta_links               = ath12k_mac_op_change_sta_links,
	.can_activate_links             = ath12k_mac_op_can_activate_links,
#ifdef CONFIG_PM
	.suspend			= ath12k_wow_op_suspend,
	.resume				= ath12k_wow_op_resume,
	.set_wakeup			= ath12k_wow_op_set_wakeup,
#endif
#ifdef CONFIG_ATH12K_DEBUGFS
	.vif_add_debugfs                = ath12k_debugfs_op_vif_add,
#endif
	CFG80211_TESTMODE_CMD(ath12k_tm_cmd)
#ifdef CONFIG_ATH12K_DEBUGFS
	.link_sta_add_debugfs           = ath12k_debugfs_link_sta_op_add,
#endif
};

void ath12k_mac_update_freq_range(struct ath12k *ar,
				  u32 freq_low, u32 freq_high)
{
	if (!(freq_low && freq_high))
		return;

	if (ar->freq_range.start_freq || ar->freq_range.end_freq) {
		ar->freq_range.start_freq = min(ar->freq_range.start_freq,
						MHZ_TO_KHZ(freq_low));
		ar->freq_range.end_freq = max(ar->freq_range.end_freq,
					      MHZ_TO_KHZ(freq_high));
	} else {
		ar->freq_range.start_freq = MHZ_TO_KHZ(freq_low);
		ar->freq_range.end_freq = MHZ_TO_KHZ(freq_high);
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "mac pdev %u freq limit updated. New range %u->%u MHz\n",
		   ar->pdev->pdev_id, KHZ_TO_MHZ(ar->freq_range.start_freq),
		   KHZ_TO_MHZ(ar->freq_range.end_freq));
}

static void ath12k_mac_update_ch_list(struct ath12k *ar,
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

static u32 ath12k_get_phy_id(struct ath12k *ar, u32 band)
{
	struct ath12k_pdev *pdev = ar->pdev;
	struct ath12k_pdev_cap *pdev_cap = &pdev->cap;

	if (band == WMI_HOST_WLAN_2GHZ_CAP)
		return pdev_cap->band[NL80211_BAND_2GHZ].phy_id;

	if (band == WMI_HOST_WLAN_5GHZ_CAP)
		return pdev_cap->band[NL80211_BAND_5GHZ].phy_id;

	ath12k_warn(ar->ab, "unsupported phy cap:%d\n", band);

	return 0;
}

static int ath12k_mac_update_band(struct ath12k *ar,
				  struct ieee80211_supported_band *orig_band,
				  struct ieee80211_supported_band *new_band)
{
	int i;

	if (!orig_band || !new_band)
		return -EINVAL;

	if (orig_band->band != new_band->band)
		return -EINVAL;

	for (i = 0; i < new_band->n_channels; i++) {
		if (new_band->channels[i].flags & IEEE80211_CHAN_DISABLED)
			continue;
		/* An enabled channel in new_band should not be already enabled
		 * in the orig_band
		 */
		if (WARN_ON(!(orig_band->channels[i].flags &
			      IEEE80211_CHAN_DISABLED)))
			return -EINVAL;
		orig_band->channels[i].flags &= ~IEEE80211_CHAN_DISABLED;
	}
	return 0;
}

static int ath12k_mac_setup_channels_rates(struct ath12k *ar,
					   u32 supported_bands,
					   struct ieee80211_supported_band *bands[])
{
	struct ieee80211_supported_band *band;
	struct ath12k_wmi_hal_reg_capabilities_ext_arg *reg_cap;
	struct ath12k_base *ab = ar->ab;
	u32 phy_id, freq_low, freq_high;
	struct ath12k_hw *ah = ar->ah;
	void *channels;
	int ret;

	BUILD_BUG_ON((ARRAY_SIZE(ath12k_2ghz_channels) +
		      ARRAY_SIZE(ath12k_5ghz_channels) +
		      ARRAY_SIZE(ath12k_6ghz_channels)) !=
		     ATH12K_NUM_CHANS);

	reg_cap = &ab->hal_reg_cap[ar->pdev_idx];

	if (supported_bands & WMI_HOST_WLAN_2GHZ_CAP) {
		channels = kmemdup(ath12k_2ghz_channels,
				   sizeof(ath12k_2ghz_channels),
				   GFP_KERNEL);
		if (!channels)
			return -ENOMEM;

		band = &ar->mac.sbands[NL80211_BAND_2GHZ];
		band->band = NL80211_BAND_2GHZ;
		band->n_channels = ARRAY_SIZE(ath12k_2ghz_channels);
		band->channels = channels;
		band->n_bitrates = ath12k_g_rates_size;
		band->bitrates = ath12k_g_rates;

		if (ab->hw_params->single_pdev_only) {
			phy_id = ath12k_get_phy_id(ar, WMI_HOST_WLAN_2GHZ_CAP);
			reg_cap = &ab->hal_reg_cap[phy_id];
		}

		freq_low = max(reg_cap->low_2ghz_chan,
			       ab->reg_freq_2ghz.start_freq);
		freq_high = min(reg_cap->high_2ghz_chan,
				ab->reg_freq_2ghz.end_freq);

		ath12k_mac_update_ch_list(ar, band,
					  reg_cap->low_2ghz_chan,
					  reg_cap->high_2ghz_chan);

		ath12k_mac_update_freq_range(ar, freq_low, freq_high);

		if (!bands[NL80211_BAND_2GHZ]) {
			bands[NL80211_BAND_2GHZ] = band;
		} else {
			/* Split mac in same band under same wiphy */
			ret = ath12k_mac_update_band(ar, bands[NL80211_BAND_2GHZ], band);
			if (ret) {
				kfree(channels);
				band->channels = NULL;
				return ret;
			}
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac pdev %u identified as 2 GHz split mac with start freq %d end freq %d",
				   ar->pdev->pdev_id,
				   KHZ_TO_MHZ(ar->freq_range.start_freq),
				   KHZ_TO_MHZ(ar->freq_range.end_freq));
		}
	}

	if (supported_bands & WMI_HOST_WLAN_5GHZ_CAP) {
		if (reg_cap->high_5ghz_chan >= ATH12K_MIN_6GHZ_FREQ) {
			channels = kmemdup(ath12k_6ghz_channels,
					   sizeof(ath12k_6ghz_channels), GFP_KERNEL);
			if (!channels) {
				kfree(ar->mac.sbands[NL80211_BAND_2GHZ].channels);
				return -ENOMEM;
			}

			ar->supports_6ghz = true;
			band = &ar->mac.sbands[NL80211_BAND_6GHZ];
			band->band = NL80211_BAND_6GHZ;
			band->n_channels = ARRAY_SIZE(ath12k_6ghz_channels);
			band->channels = channels;
			band->n_bitrates = ath12k_a_rates_size;
			band->bitrates = ath12k_a_rates;

			freq_low = max(reg_cap->low_5ghz_chan,
				       ab->reg_freq_6ghz.start_freq);
			freq_high = min(reg_cap->high_5ghz_chan,
					ab->reg_freq_6ghz.end_freq);

			ath12k_mac_update_ch_list(ar, band,
						  reg_cap->low_5ghz_chan,
						  reg_cap->high_5ghz_chan);

			ath12k_mac_update_freq_range(ar, freq_low, freq_high);
			ah->use_6ghz_regd = true;

			if (!bands[NL80211_BAND_6GHZ]) {
				bands[NL80211_BAND_6GHZ] = band;
			} else {
				/* Split mac in same band under same wiphy */
				ret = ath12k_mac_update_band(ar,
							     bands[NL80211_BAND_6GHZ],
							     band);
				if (ret) {
					kfree(ar->mac.sbands[NL80211_BAND_2GHZ].channels);
					ar->mac.sbands[NL80211_BAND_2GHZ].channels = NULL;
					kfree(channels);
					band->channels = NULL;
					return ret;
				}
				ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac pdev %u identified as 6 GHz split mac with start freq %d end freq %d",
					   ar->pdev->pdev_id,
					   KHZ_TO_MHZ(ar->freq_range.start_freq),
					   KHZ_TO_MHZ(ar->freq_range.end_freq));
			}
		}

		if (reg_cap->low_5ghz_chan < ATH12K_MIN_6GHZ_FREQ) {
			channels = kmemdup(ath12k_5ghz_channels,
					   sizeof(ath12k_5ghz_channels),
					   GFP_KERNEL);
			if (!channels) {
				kfree(ar->mac.sbands[NL80211_BAND_2GHZ].channels);
				kfree(ar->mac.sbands[NL80211_BAND_6GHZ].channels);
				return -ENOMEM;
			}

			band = &ar->mac.sbands[NL80211_BAND_5GHZ];
			band->band = NL80211_BAND_5GHZ;
			band->n_channels = ARRAY_SIZE(ath12k_5ghz_channels);
			band->channels = channels;
			band->n_bitrates = ath12k_a_rates_size;
			band->bitrates = ath12k_a_rates;

			if (ab->hw_params->single_pdev_only) {
				phy_id = ath12k_get_phy_id(ar, WMI_HOST_WLAN_5GHZ_CAP);
				reg_cap = &ab->hal_reg_cap[phy_id];
			}

			freq_low = max(reg_cap->low_5ghz_chan,
				       ab->reg_freq_5ghz.start_freq);
			freq_high = min(reg_cap->high_5ghz_chan,
					ab->reg_freq_5ghz.end_freq);

			ath12k_mac_update_ch_list(ar, band,
						  reg_cap->low_5ghz_chan,
						  reg_cap->high_5ghz_chan);

			ath12k_mac_update_freq_range(ar, freq_low, freq_high);

			if (!bands[NL80211_BAND_5GHZ]) {
				bands[NL80211_BAND_5GHZ] = band;
			} else {
				/* Split mac in same band under same wiphy */
				ret = ath12k_mac_update_band(ar,
							     bands[NL80211_BAND_5GHZ],
							     band);
				if (ret) {
					kfree(ar->mac.sbands[NL80211_BAND_2GHZ].channels);
					ar->mac.sbands[NL80211_BAND_2GHZ].channels = NULL;
					kfree(ar->mac.sbands[NL80211_BAND_6GHZ].channels);
					ar->mac.sbands[NL80211_BAND_2GHZ].channels = NULL;
					kfree(channels);
					band->channels = NULL;
					return ret;
				}
				ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mac pdev %u identified as 5 GHz split mac with start freq %d end freq %d",
					   ar->pdev->pdev_id,
					   KHZ_TO_MHZ(ar->freq_range.start_freq),
					   KHZ_TO_MHZ(ar->freq_range.end_freq));
			}
		}
	}

	return 0;
}

static u16 ath12k_mac_get_ifmodes(struct ath12k_hw *ah)
{
	struct ath12k *ar;
	int i;
	u16 interface_modes = U16_MAX;

	for_each_ar(ah, ar, i)
		interface_modes &= ar->ab->hw_params->interface_modes;

	return interface_modes == U16_MAX ? 0 : interface_modes;
}

static bool ath12k_mac_is_iface_mode_enable(struct ath12k_hw *ah,
					    enum nl80211_iftype type)
{
	struct ath12k *ar;
	int i;
	u16 interface_modes, mode = 0;
	bool is_enable = false;

	if (type == NL80211_IFTYPE_MESH_POINT) {
		if (IS_ENABLED(CONFIG_MAC80211_MESH))
			mode = BIT(type);
	} else {
		mode = BIT(type);
	}

	for_each_ar(ah, ar, i) {
		interface_modes = ar->ab->hw_params->interface_modes;
		if (interface_modes & mode) {
			is_enable = true;
			break;
		}
	}

	return is_enable;
}

static int
ath12k_mac_setup_radio_iface_comb(struct ath12k *ar,
				  struct ieee80211_iface_combination *comb)
{
	u16 interface_modes = ar->ab->hw_params->interface_modes;
	struct ieee80211_iface_limit *limits;
	int n_limits, max_interfaces;
	bool ap, mesh, p2p;

	ap = interface_modes & BIT(NL80211_IFTYPE_AP);
	p2p = interface_modes & BIT(NL80211_IFTYPE_P2P_DEVICE);

	mesh = IS_ENABLED(CONFIG_MAC80211_MESH) &&
	       (interface_modes & BIT(NL80211_IFTYPE_MESH_POINT));

	if ((ap || mesh) && !p2p) {
		n_limits = 2;
		max_interfaces = 16;
	} else if (p2p) {
		n_limits = 3;
		if (ap || mesh)
			max_interfaces = 16;
		else
			max_interfaces = 3;
	} else {
		n_limits = 1;
		max_interfaces = 1;
	}

	limits = kcalloc(n_limits, sizeof(*limits), GFP_KERNEL);
	if (!limits)
		return -ENOMEM;

	limits[0].max = 1;
	limits[0].types |= BIT(NL80211_IFTYPE_STATION);

	if (ap || mesh || p2p)
		limits[1].max = max_interfaces;

	if (ap)
		limits[1].types |= BIT(NL80211_IFTYPE_AP);

	if (mesh)
		limits[1].types |= BIT(NL80211_IFTYPE_MESH_POINT);

	if (p2p) {
		limits[1].types |= BIT(NL80211_IFTYPE_P2P_CLIENT) |
					BIT(NL80211_IFTYPE_P2P_GO);
		limits[2].max = 1;
		limits[2].types |= BIT(NL80211_IFTYPE_P2P_DEVICE);
	}

	comb[0].limits = limits;
	comb[0].n_limits = n_limits;
	comb[0].max_interfaces = max_interfaces;
	comb[0].beacon_int_infra_match = true;
	comb[0].beacon_int_min_gcd = 100;

	if (ar->ab->hw_params->single_pdev_only) {
		comb[0].num_different_channels = 2;
	} else {
		comb[0].num_different_channels = 1;
		comb[0].radar_detect_widths = BIT(NL80211_CHAN_WIDTH_20_NOHT) |
						BIT(NL80211_CHAN_WIDTH_20) |
						BIT(NL80211_CHAN_WIDTH_40) |
						BIT(NL80211_CHAN_WIDTH_80);
	}

	return 0;
}

static int
ath12k_mac_setup_global_iface_comb(struct ath12k_hw *ah,
				   struct wiphy_radio *radio,
				   u8 n_radio,
				   struct ieee80211_iface_combination *comb)
{
	const struct ieee80211_iface_combination *iter_comb;
	struct ieee80211_iface_limit *limits;
	int i, j, n_limits;
	bool ap, mesh, p2p;

	if (!n_radio)
		return 0;

	ap = ath12k_mac_is_iface_mode_enable(ah, NL80211_IFTYPE_AP);
	p2p = ath12k_mac_is_iface_mode_enable(ah, NL80211_IFTYPE_P2P_DEVICE);
	mesh = ath12k_mac_is_iface_mode_enable(ah, NL80211_IFTYPE_MESH_POINT);

	if ((ap || mesh) && !p2p)
		n_limits = 2;
	else if (p2p)
		n_limits = 3;
	else
		n_limits = 1;

	limits = kcalloc(n_limits, sizeof(*limits), GFP_KERNEL);
	if (!limits)
		return -ENOMEM;

	for (i = 0; i < n_radio; i++) {
		iter_comb = radio[i].iface_combinations;
		for (j = 0; j < iter_comb->n_limits && j < n_limits; j++) {
			limits[j].types |= iter_comb->limits[j].types;
			limits[j].max += iter_comb->limits[j].max;
		}

		comb->max_interfaces += iter_comb->max_interfaces;
		comb->num_different_channels += iter_comb->num_different_channels;
		comb->radar_detect_widths |= iter_comb->radar_detect_widths;
	}

	comb->limits = limits;
	comb->n_limits = n_limits;
	comb->beacon_int_infra_match = true;
	comb->beacon_int_min_gcd = 100;

	return 0;
}

static
void ath12k_mac_cleanup_iface_comb(const struct ieee80211_iface_combination *iface_comb)
{
	kfree(iface_comb[0].limits);
	kfree(iface_comb);
}

static void ath12k_mac_cleanup_iface_combinations(struct ath12k_hw *ah)
{
	struct wiphy *wiphy = ah->hw->wiphy;
	const struct wiphy_radio *radio;
	int i;

	if (wiphy->n_radio > 0) {
		radio = wiphy->radio;
		for (i = 0; i < wiphy->n_radio; i++)
			ath12k_mac_cleanup_iface_comb(radio[i].iface_combinations);

		kfree(wiphy->radio);
	}

	ath12k_mac_cleanup_iface_comb(wiphy->iface_combinations);
}

static int ath12k_mac_setup_iface_combinations(struct ath12k_hw *ah)
{
	struct ieee80211_iface_combination *combinations, *comb;
	struct wiphy *wiphy = ah->hw->wiphy;
	struct wiphy_radio *radio;
	struct ath12k *ar;
	int i, ret;

	combinations = kzalloc(sizeof(*combinations), GFP_KERNEL);
	if (!combinations)
		return -ENOMEM;

	if (ah->num_radio == 1) {
		ret = ath12k_mac_setup_radio_iface_comb(&ah->radio[0],
							combinations);
		if (ret) {
			ath12k_hw_warn(ah, "failed to setup radio interface combinations for one radio: %d",
				       ret);
			goto err_free_combinations;
		}

		goto out;
	}

	/* there are multiple radios */

	radio = kcalloc(ah->num_radio, sizeof(*radio), GFP_KERNEL);
	if (!radio) {
		ret = -ENOMEM;
		goto err_free_combinations;
	}

	for_each_ar(ah, ar, i) {
		comb = kzalloc(sizeof(*comb), GFP_KERNEL);
		if (!comb) {
			ret = -ENOMEM;
			goto err_free_radios;
		}

		ret = ath12k_mac_setup_radio_iface_comb(ar, comb);
		if (ret) {
			ath12k_hw_warn(ah, "failed to setup radio interface combinations for radio %d: %d",
				       i, ret);
			kfree(comb);
			goto err_free_radios;
		}

		radio[i].freq_range = &ar->freq_range;
		radio[i].n_freq_range = 1;

		radio[i].iface_combinations = comb;
		radio[i].n_iface_combinations = 1;
	}

	ret = ath12k_mac_setup_global_iface_comb(ah, radio, ah->num_radio, combinations);
	if (ret) {
		ath12k_hw_warn(ah, "failed to setup global interface combinations: %d",
			       ret);
		goto err_free_all_radios;
	}

	wiphy->radio = radio;
	wiphy->n_radio = ah->num_radio;

out:
	wiphy->iface_combinations = combinations;
	wiphy->n_iface_combinations = 1;

	return 0;

err_free_all_radios:
	i = ah->num_radio;

err_free_radios:
	while (i--)
		ath12k_mac_cleanup_iface_comb(radio[i].iface_combinations);

	kfree(radio);

err_free_combinations:
	kfree(combinations);

	return ret;
}

static const u8 ath12k_if_types_ext_capa[] = {
	[0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING,
	[2] = WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT,
	[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF,
};

static const u8 ath12k_if_types_ext_capa_sta[] = {
	[0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING,
	[2] = WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT,
	[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF,
	[9] = WLAN_EXT_CAPA10_TWT_REQUESTER_SUPPORT,
};

static const u8 ath12k_if_types_ext_capa_ap[] = {
	[0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING,
	[2] = WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT,
	[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF,
	[9] = WLAN_EXT_CAPA10_TWT_RESPONDER_SUPPORT,
	[10] = WLAN_EXT_CAPA11_EMA_SUPPORT,
};

static struct wiphy_iftype_ext_capab ath12k_iftypes_ext_capa[] = {
	{
		.extended_capabilities = ath12k_if_types_ext_capa,
		.extended_capabilities_mask = ath12k_if_types_ext_capa,
		.extended_capabilities_len = sizeof(ath12k_if_types_ext_capa),
	}, {
		.iftype = NL80211_IFTYPE_STATION,
		.extended_capabilities = ath12k_if_types_ext_capa_sta,
		.extended_capabilities_mask = ath12k_if_types_ext_capa_sta,
		.extended_capabilities_len =
				sizeof(ath12k_if_types_ext_capa_sta),
	}, {
		.iftype = NL80211_IFTYPE_AP,
		.extended_capabilities = ath12k_if_types_ext_capa_ap,
		.extended_capabilities_mask = ath12k_if_types_ext_capa_ap,
		.extended_capabilities_len =
				sizeof(ath12k_if_types_ext_capa_ap),
		.eml_capabilities = 0,
		.mld_capa_and_ops = 0,
	},
};

static void ath12k_mac_cleanup_unregister(struct ath12k *ar)
{
	idr_for_each(&ar->txmgmt_idr, ath12k_mac_tx_mgmt_pending_free, ar);
	idr_destroy(&ar->txmgmt_idr);

	kfree(ar->mac.sbands[NL80211_BAND_2GHZ].channels);
	kfree(ar->mac.sbands[NL80211_BAND_5GHZ].channels);
	kfree(ar->mac.sbands[NL80211_BAND_6GHZ].channels);
}

static void ath12k_mac_hw_unregister(struct ath12k_hw *ah)
{
	struct ieee80211_hw *hw = ah->hw;
	struct ath12k *ar;
	int i;

	for_each_ar(ah, ar, i) {
		cancel_work_sync(&ar->regd_channel_update_work);
		cancel_work_sync(&ar->regd_update_work);
		ath12k_debugfs_unregister(ar);
		ath12k_fw_stats_reset(ar);
	}

	ieee80211_unregister_hw(hw);

	for_each_ar(ah, ar, i)
		ath12k_mac_cleanup_unregister(ar);

	ath12k_mac_cleanup_iface_combinations(ah);

	SET_IEEE80211_DEV(hw, NULL);
}

static int ath12k_mac_setup_register(struct ath12k *ar,
				     u32 *ht_cap,
				     struct ieee80211_supported_band *bands[])
{
	struct ath12k_pdev_cap *cap = &ar->pdev->cap;
	int ret;

	init_waitqueue_head(&ar->txmgmt_empty_waitq);
	idr_init(&ar->txmgmt_idr);
	spin_lock_init(&ar->txmgmt_idr_lock);

	ath12k_pdev_caps_update(ar);

	ret = ath12k_mac_setup_channels_rates(ar,
					      cap->supported_bands,
					      bands);
	if (ret)
		return ret;

	ath12k_mac_setup_ht_vht_cap(ar, cap, ht_cap);
	ath12k_mac_setup_sband_iftype_data(ar, cap);

	ar->max_num_stations = ath12k_core_get_max_station_per_radio(ar->ab);
	ar->max_num_peers = ath12k_core_get_max_peers_per_radio(ar->ab);

	ar->rssi_info.min_nf_dbm = ATH12K_DEFAULT_NOISE_FLOOR;
	ar->rssi_info.temp_offset = 0;
	ar->rssi_info.noise_floor = ar->rssi_info.min_nf_dbm + ar->rssi_info.temp_offset;

	return 0;
}

static int ath12k_mac_hw_register(struct ath12k_hw *ah)
{
	struct ieee80211_hw *hw = ah->hw;
	struct wiphy *wiphy = hw->wiphy;
	struct ath12k *ar = ath12k_ah_to_ar(ah, 0);
	struct ath12k_base *ab = ar->ab;
	struct ath12k_pdev *pdev;
	struct ath12k_pdev_cap *cap;
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
	int ret, i, j;
	u32 ht_cap = U32_MAX, antennas_rx = 0, antennas_tx = 0;
	bool is_6ghz = false, is_raw_mode = false, is_monitor_disable = false;
	u8 *mac_addr = NULL;
	u8 mbssid_max_interfaces = 0;

	wiphy->max_ap_assoc_sta = 0;

	for_each_ar(ah, ar, i) {
		u32 ht_cap_info = 0;

		pdev = ar->pdev;
		if (ar->ab->pdevs_macaddr_valid) {
			ether_addr_copy(ar->mac_addr, pdev->mac_addr);
		} else {
			ether_addr_copy(ar->mac_addr, ar->ab->mac_addr);
			ar->mac_addr[4] += ar->pdev_idx;
		}

		ret = ath12k_mac_setup_register(ar, &ht_cap_info, hw->wiphy->bands);
		if (ret)
			goto err_cleanup_unregister;

		/* 6 GHz does not support HT Cap, hence do not consider it */
		if (!ar->supports_6ghz)
			ht_cap &= ht_cap_info;

		wiphy->max_ap_assoc_sta += ar->max_num_stations;

		/* Advertise the max antenna support of all radios, driver can handle
		 * per pdev specific antenna setting based on pdev cap when antenna
		 * changes are made
		 */
		cap = &pdev->cap;

		antennas_rx = max_t(u32, antennas_rx, cap->rx_chain_mask);
		antennas_tx = max_t(u32, antennas_tx, cap->tx_chain_mask);

		if (ar->supports_6ghz)
			is_6ghz = true;

		if (test_bit(ATH12K_FLAG_RAW_MODE, &ar->ab->dev_flags))
			is_raw_mode = true;

		if (!ar->ab->hw_params->supports_monitor)
			is_monitor_disable = true;

		if (i == 0)
			mac_addr = ar->mac_addr;
		else
			mac_addr = ab->mac_addr;

		mbssid_max_interfaces += TARGET_NUM_VDEVS;
	}

	wiphy->available_antennas_rx = antennas_rx;
	wiphy->available_antennas_tx = antennas_tx;

	SET_IEEE80211_PERM_ADDR(hw, mac_addr);
	SET_IEEE80211_DEV(hw, ab->dev);

	ret = ath12k_mac_setup_iface_combinations(ah);
	if (ret) {
		ath12k_err(ab, "failed to setup interface combinations: %d\n", ret);
		goto err_complete_cleanup_unregister;
	}

	wiphy->interface_modes = ath12k_mac_get_ifmodes(ah);

	if (ah->num_radio == 1 &&
	    wiphy->bands[NL80211_BAND_2GHZ] &&
	    wiphy->bands[NL80211_BAND_5GHZ] &&
	    wiphy->bands[NL80211_BAND_6GHZ])
		ieee80211_hw_set(hw, SINGLE_SCAN_ON_ALL_BANDS);

	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, AP_LINK_PS);
	ieee80211_hw_set(hw, SPECTRUM_MGMT);
	ieee80211_hw_set(hw, CONNECTION_MONITOR);
	ieee80211_hw_set(hw, SUPPORTS_PER_STA_GTK);
	ieee80211_hw_set(hw, CHANCTX_STA_CSA);
	ieee80211_hw_set(hw, QUEUE_CONTROL);
	ieee80211_hw_set(hw, SUPPORTS_TX_FRAG);
	ieee80211_hw_set(hw, REPORTS_LOW_ACK);
	ieee80211_hw_set(hw, NO_VIRTUAL_MONITOR);

	if ((ht_cap & WMI_HT_CAP_ENABLED) || is_6ghz) {
		ieee80211_hw_set(hw, AMPDU_AGGREGATION);
		ieee80211_hw_set(hw, TX_AMPDU_SETUP_IN_HW);
		ieee80211_hw_set(hw, SUPPORTS_REORDERING_BUFFER);
		ieee80211_hw_set(hw, SUPPORTS_AMSDU_IN_AMPDU);
		ieee80211_hw_set(hw, USES_RSS);
	}

	wiphy->features |= NL80211_FEATURE_STATIC_SMPS;
	wiphy->flags |= WIPHY_FLAG_IBSS_RSN;

	/* TODO: Check if HT capability advertised from firmware is different
	 * for each band for a dual band capable radio. It will be tricky to
	 * handle it when the ht capability different for each band.
	 */
	if (ht_cap & WMI_HT_CAP_DYNAMIC_SMPS ||
	    (is_6ghz && ab->hw_params->supports_dynamic_smps_6ghz))
		wiphy->features |= NL80211_FEATURE_DYNAMIC_SMPS;

	wiphy->max_scan_ssids = WLAN_SCAN_PARAMS_MAX_SSID;
	wiphy->max_scan_ie_len = WLAN_SCAN_PARAMS_MAX_IE_LEN;

	hw->max_listen_interval = ATH12K_MAX_HW_LISTEN_INTERVAL;

	wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	wiphy->max_remain_on_channel_duration = 5000;

	wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
	wiphy->features |= NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
				   NL80211_FEATURE_AP_SCAN;

	/* MLO is not yet supported so disable Wireless Extensions for now
	 * to make sure ath12k users don't use it. This flag can be removed
	 * once WIPHY_FLAG_SUPPORTS_MLO is enabled.
	 */
	wiphy->flags |= WIPHY_FLAG_DISABLE_WEXT;

	/* Copy over MLO related capabilities received from
	 * WMI_SERVICE_READY_EXT2_EVENT if single_chip_mlo_supp is set.
	 */
	if (ab->ag->mlo_capable) {
		ath12k_iftypes_ext_capa[2].eml_capabilities = cap->eml_cap;
		ath12k_iftypes_ext_capa[2].mld_capa_and_ops = cap->mld_cap;
		wiphy->flags |= WIPHY_FLAG_SUPPORTS_MLO;

		ieee80211_hw_set(hw, MLO_MCAST_MULTI_LINK_TX);
	}

	hw->queues = ATH12K_HW_MAX_QUEUES;
	wiphy->tx_queue_len = ATH12K_QUEUE_LEN;
	hw->offchannel_tx_hw_queue = ATH12K_HW_MAX_QUEUES - 1;
	hw->max_rx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF_EHT;

	hw->vif_data_size = sizeof(struct ath12k_vif);
	hw->sta_data_size = sizeof(struct ath12k_sta);
	hw->extra_tx_headroom = ab->hw_params->iova_mask;

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_CQM_RSSI_LIST);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_STA_TX_PWR);

	wiphy->cipher_suites = cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

	wiphy->iftype_ext_capab = ath12k_iftypes_ext_capa;
	wiphy->num_iftype_ext_capab = ARRAY_SIZE(ath12k_iftypes_ext_capa);

	wiphy->mbssid_max_interfaces = mbssid_max_interfaces;
	wiphy->ema_max_profile_periodicity = TARGET_EMA_MAX_PROFILE_PERIOD;
	ieee80211_hw_set(hw, SUPPORTS_MULTI_BSSID);

	if (is_6ghz) {
		wiphy_ext_feature_set(wiphy,
				      NL80211_EXT_FEATURE_FILS_DISCOVERY);
		wiphy_ext_feature_set(wiphy,
				      NL80211_EXT_FEATURE_UNSOL_BCAST_PROBE_RESP);
	}

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_PUNCT);

	ath12k_reg_init(hw);

	if (!is_raw_mode) {
		hw->netdev_features = NETIF_F_HW_CSUM;
		ieee80211_hw_set(hw, SW_CRYPTO_CONTROL);
		ieee80211_hw_set(hw, SUPPORT_FAST_XMIT);
	}

	if (test_bit(WMI_TLV_SERVICE_NLO, ar->wmi->wmi_ab->svc_map)) {
		wiphy->max_sched_scan_ssids = WMI_PNO_MAX_SUPP_NETWORKS;
		wiphy->max_match_sets = WMI_PNO_MAX_SUPP_NETWORKS;
		wiphy->max_sched_scan_ie_len = WMI_PNO_MAX_IE_LENGTH;
		wiphy->max_sched_scan_plans = WMI_PNO_MAX_SCHED_SCAN_PLANS;
		wiphy->max_sched_scan_plan_interval =
					WMI_PNO_MAX_SCHED_SCAN_PLAN_INT;
		wiphy->max_sched_scan_plan_iterations =
					WMI_PNO_MAX_SCHED_SCAN_PLAN_ITRNS;
		wiphy->features |= NL80211_FEATURE_ND_RANDOM_MAC_ADDR;
	}

	ret = ath12k_wow_init(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to init wow: %d\n", ret);
		goto err_cleanup_if_combs;
	}

	/* Boot-time regulatory updates have already been processed.
	 * Mark them as complete now, because after registration,
	 * cfg80211 will notify us again if there are any pending hints.
	 * We need to wait for those hints to be processed, so it's
	 * important to mark the boot-time updates as complete before
	 * proceeding with registration.
	 */
	for_each_ar(ah, ar, i)
		complete_all(&ar->regd_update_completed);

	ret = ieee80211_register_hw(hw);
	if (ret) {
		ath12k_err(ab, "ieee80211 registration failed: %d\n", ret);
		goto err_cleanup_if_combs;
	}

	if (is_monitor_disable)
		/* There's a race between calling ieee80211_register_hw()
		 * and here where the monitor mode is enabled for a little
		 * while. But that time is so short and in practise it make
		 * a difference in real life.
		 */
		wiphy->interface_modes &= ~BIT(NL80211_IFTYPE_MONITOR);

	for_each_ar(ah, ar, i) {
		/* Apply the regd received during initialization */
		ret = ath12k_regd_update(ar, true);
		if (ret) {
			ath12k_err(ar->ab, "ath12k regd update failed: %d\n", ret);
			goto err_unregister_hw;
		}

		if (ar->ab->hw_params->current_cc_support && ab->new_alpha2[0]) {
			struct wmi_set_current_country_arg current_cc = {};

			memcpy(&current_cc.alpha2, ab->new_alpha2, 2);
			memcpy(&ar->alpha2, ab->new_alpha2, 2);

			reinit_completion(&ar->regd_update_completed);

			ret = ath12k_wmi_send_set_current_country_cmd(ar, &current_cc);
			if (ret)
				ath12k_warn(ar->ab,
					    "failed set cc code for mac register: %d\n",
					    ret);
		}

		ath12k_fw_stats_init(ar);
		ath12k_debugfs_register(ar);
	}

	return 0;

err_unregister_hw:
	for_each_ar(ah, ar, i)
		ath12k_debugfs_unregister(ar);

	ieee80211_unregister_hw(hw);

err_cleanup_if_combs:
	ath12k_mac_cleanup_iface_combinations(ah);

err_complete_cleanup_unregister:
	i = ah->num_radio;

err_cleanup_unregister:
	for (j = 0; j < i; j++) {
		ar = ath12k_ah_to_ar(ah, j);
		ath12k_mac_cleanup_unregister(ar);
	}

	SET_IEEE80211_DEV(hw, NULL);

	return ret;
}

static void ath12k_mac_setup(struct ath12k *ar)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_pdev *pdev = ar->pdev;
	u8 pdev_idx = ar->pdev_idx;

	ar->lmac_id = ath12k_hw_get_mac_from_pdev_id(ab->hw_params, pdev_idx);

	ar->wmi = &ab->wmi_ab.wmi[pdev_idx];
	/* FIXME: wmi[0] is already initialized during attach,
	 * Should we do this again?
	 */
	ath12k_wmi_pdev_attach(ab, pdev_idx);

	ar->cfg_tx_chainmask = pdev->cap.tx_chain_mask;
	ar->cfg_rx_chainmask = pdev->cap.rx_chain_mask;
	ar->num_tx_chains = hweight32(pdev->cap.tx_chain_mask);
	ar->num_rx_chains = hweight32(pdev->cap.rx_chain_mask);
	ar->scan.arvif = NULL;
	ar->vdev_id_11d_scan = ATH12K_11D_INVALID_VDEV_ID;

	spin_lock_init(&ar->data_lock);
	INIT_LIST_HEAD(&ar->arvifs);
	INIT_LIST_HEAD(&ar->ppdu_stats_info);

	init_completion(&ar->vdev_setup_done);
	init_completion(&ar->vdev_delete_done);
	init_completion(&ar->peer_assoc_done);
	init_completion(&ar->peer_delete_done);
	init_completion(&ar->install_key_done);
	init_completion(&ar->bss_survey_done);
	init_completion(&ar->scan.started);
	init_completion(&ar->scan.completed);
	init_completion(&ar->scan.on_channel);
	init_completion(&ar->mlo_setup_done);
	init_completion(&ar->completed_11d_scan);
	init_completion(&ar->regd_update_completed);

	INIT_DELAYED_WORK(&ar->scan.timeout, ath12k_scan_timeout_work);
	wiphy_work_init(&ar->scan.vdev_clean_wk, ath12k_scan_vdev_clean_work);
	INIT_WORK(&ar->regd_channel_update_work, ath12k_regd_update_chan_list_work);
	INIT_LIST_HEAD(&ar->regd_channel_update_queue);
	INIT_WORK(&ar->regd_update_work, ath12k_regd_update_work);

	wiphy_work_init(&ar->wmi_mgmt_tx_work, ath12k_mgmt_over_wmi_tx_work);
	skb_queue_head_init(&ar->wmi_mgmt_tx_queue);

	ar->monitor_vdev_id = -1;
	ar->monitor_vdev_created = false;
	ar->monitor_started = false;
}

static int __ath12k_mac_mlo_setup(struct ath12k *ar)
{
	u8 num_link = 0, partner_link_id[ATH12K_GROUP_MAX_RADIO] = {};
	struct ath12k_base *partner_ab, *ab = ar->ab;
	struct ath12k_hw_group *ag = ab->ag;
	struct wmi_mlo_setup_arg mlo = {};
	struct ath12k_pdev *pdev;
	unsigned long time_left;
	int i, j, ret;

	lockdep_assert_held(&ag->mutex);

	reinit_completion(&ar->mlo_setup_done);

	for (i = 0; i < ag->num_devices; i++) {
		partner_ab = ag->ab[i];

		for (j = 0; j < partner_ab->num_radios; j++) {
			pdev = &partner_ab->pdevs[j];

			/* Avoid the self link */
			if (ar == pdev->ar)
				continue;

			partner_link_id[num_link] = pdev->hw_link_id;
			num_link++;

			ath12k_dbg(ab, ATH12K_DBG_MAC, "device %d pdev %d hw_link_id %d num_link %d\n",
				   i, j, pdev->hw_link_id, num_link);
		}
	}

	if (num_link == 0)
		return 0;

	mlo.group_id = cpu_to_le32(ag->id);
	mlo.partner_link_id = partner_link_id;
	mlo.num_partner_links = num_link;
	ar->mlo_setup_status = 0;

	ath12k_dbg(ab, ATH12K_DBG_MAC, "group id %d num_link %d\n", ag->id, num_link);

	ret = ath12k_wmi_mlo_setup(ar, &mlo);
	if (ret) {
		ath12k_err(ab, "failed to send  setup MLO WMI command for pdev %d: %d\n",
			   ar->pdev_idx, ret);
		return ret;
	}

	time_left = wait_for_completion_timeout(&ar->mlo_setup_done,
						WMI_MLO_CMD_TIMEOUT_HZ);

	if (!time_left || ar->mlo_setup_status)
		return ar->mlo_setup_status ? : -ETIMEDOUT;

	ath12k_dbg(ab, ATH12K_DBG_MAC, "mlo setup done for pdev %d\n", ar->pdev_idx);

	return 0;
}

static int __ath12k_mac_mlo_teardown(struct ath12k *ar)
{
	struct ath12k_base *ab = ar->ab;
	int ret;
	u8 num_link;

	if (test_bit(ATH12K_FLAG_RECOVERY, &ab->dev_flags))
		return 0;

	num_link = ath12k_get_num_partner_link(ar);

	if (num_link == 0)
		return 0;

	ret = ath12k_wmi_mlo_teardown(ar);
	if (ret) {
		ath12k_warn(ab, "failed to send MLO teardown WMI command for pdev %d: %d\n",
			    ar->pdev_idx, ret);
		return ret;
	}

	ath12k_dbg(ab, ATH12K_DBG_MAC, "mlo teardown for pdev %d\n", ar->pdev_idx);

	return 0;
}

int ath12k_mac_mlo_setup(struct ath12k_hw_group *ag)
{
	struct ath12k_hw *ah;
	struct ath12k *ar;
	int ret;
	int i, j;

	for (i = 0; i < ag->num_hw; i++) {
		ah = ag->ah[i];
		if (!ah)
			continue;

		for_each_ar(ah, ar, j) {
			ar = &ah->radio[j];
			ret = __ath12k_mac_mlo_setup(ar);
			if (ret) {
				ath12k_err(ar->ab, "failed to setup MLO: %d\n", ret);
				goto err_setup;
			}
		}
	}

	return 0;

err_setup:
	for (i = i - 1; i >= 0; i--) {
		ah = ag->ah[i];
		if (!ah)
			continue;

		for (j = j - 1; j >= 0; j--) {
			ar = &ah->radio[j];
			if (!ar)
				continue;

			__ath12k_mac_mlo_teardown(ar);
		}
	}

	return ret;
}

void ath12k_mac_mlo_teardown(struct ath12k_hw_group *ag)
{
	struct ath12k_hw *ah;
	struct ath12k *ar;
	int ret, i, j;

	for (i = 0; i < ag->num_hw; i++) {
		ah = ag->ah[i];
		if (!ah)
			continue;

		for_each_ar(ah, ar, j) {
			ar = &ah->radio[j];
			ret = __ath12k_mac_mlo_teardown(ar);
			if (ret) {
				ath12k_err(ar->ab, "failed to teardown MLO: %d\n", ret);
				break;
			}
		}
	}
}

int ath12k_mac_register(struct ath12k_hw_group *ag)
{
	struct ath12k_hw *ah;
	int i;
	int ret;

	for (i = 0; i < ag->num_hw; i++) {
		ah = ath12k_ag_to_ah(ag, i);

		ret = ath12k_mac_hw_register(ah);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (i = i - 1; i >= 0; i--) {
		ah = ath12k_ag_to_ah(ag, i);
		if (!ah)
			continue;

		ath12k_mac_hw_unregister(ah);
	}

	return ret;
}

void ath12k_mac_unregister(struct ath12k_hw_group *ag)
{
	struct ath12k_hw *ah;
	int i;

	for (i = ag->num_hw - 1; i >= 0; i--) {
		ah = ath12k_ag_to_ah(ag, i);
		if (!ah)
			continue;

		ath12k_mac_hw_unregister(ah);
	}
}

static void ath12k_mac_hw_destroy(struct ath12k_hw *ah)
{
	ieee80211_free_hw(ah->hw);
}

static struct ath12k_hw *ath12k_mac_hw_allocate(struct ath12k_hw_group *ag,
						struct ath12k_pdev_map *pdev_map,
						u8 num_pdev_map)
{
	struct ieee80211_hw *hw;
	struct ath12k *ar;
	struct ath12k_base *ab;
	struct ath12k_pdev *pdev;
	struct ath12k_hw *ah;
	int i;
	u8 pdev_idx;

	hw = ieee80211_alloc_hw(struct_size(ah, radio, num_pdev_map),
				&ath12k_ops);
	if (!hw)
		return NULL;

	ah = ath12k_hw_to_ah(hw);
	ah->hw = hw;
	ah->num_radio = num_pdev_map;

	mutex_init(&ah->hw_mutex);
	INIT_LIST_HEAD(&ah->ml_peers);

	for (i = 0; i < num_pdev_map; i++) {
		ab = pdev_map[i].ab;
		pdev_idx = pdev_map[i].pdev_idx;
		pdev = &ab->pdevs[pdev_idx];

		ar = ath12k_ah_to_ar(ah, i);
		ar->ah = ah;
		ar->ab = ab;
		ar->hw_link_id = pdev->hw_link_id;
		ar->pdev = pdev;
		ar->pdev_idx = pdev_idx;
		pdev->ar = ar;

		ag->hw_links[ar->hw_link_id].device_id = ab->device_id;
		ag->hw_links[ar->hw_link_id].pdev_idx = pdev_idx;

		ath12k_mac_setup(ar);
		ath12k_dp_pdev_pre_alloc(ar);
	}

	return ah;
}

void ath12k_mac_destroy(struct ath12k_hw_group *ag)
{
	struct ath12k_pdev *pdev;
	struct ath12k_base *ab = ag->ab[0];
	int i, j;
	struct ath12k_hw *ah;

	for (i = 0; i < ag->num_devices; i++) {
		ab = ag->ab[i];
		if (!ab)
			continue;

		for (j = 0; j < ab->num_radios; j++) {
			pdev = &ab->pdevs[j];
			if (!pdev->ar)
				continue;
			pdev->ar = NULL;
		}
	}

	for (i = 0; i < ag->num_hw; i++) {
		ah = ath12k_ag_to_ah(ag, i);
		if (!ah)
			continue;

		ath12k_mac_hw_destroy(ah);
		ath12k_ag_set_ah(ag, i, NULL);
	}
}

static void ath12k_mac_set_device_defaults(struct ath12k_base *ab)
{
	/* Initialize channel counters frequency value in hertz */
	ab->cc_freq_hz = 320000;
	ab->free_vdev_map = (1LL << (ab->num_radios * TARGET_NUM_VDEVS)) - 1;
}

int ath12k_mac_allocate(struct ath12k_hw_group *ag)
{
	struct ath12k_pdev_map pdev_map[ATH12K_GROUP_MAX_RADIO];
	int mac_id, device_id, total_radio, num_hw;
	struct ath12k_base *ab;
	struct ath12k_hw *ah;
	int ret, i, j;
	u8 radio_per_hw;

	total_radio = 0;
	for (i = 0; i < ag->num_devices; i++) {
		ab = ag->ab[i];
		if (!ab)
			continue;

		ath12k_debugfs_pdev_create(ab);
		ath12k_mac_set_device_defaults(ab);
		total_radio += ab->num_radios;
	}

	if (!total_radio)
		return -EINVAL;

	if (WARN_ON(total_radio > ATH12K_GROUP_MAX_RADIO))
		return -ENOSPC;

	/* All pdev get combined and register as single wiphy based on
	 * hardware group which participate in multi-link operation else
	 * each pdev get register separately.
	 */
	if (ag->mlo_capable)
		radio_per_hw = total_radio;
	else
		radio_per_hw = 1;

	num_hw = total_radio / radio_per_hw;

	ag->num_hw = 0;
	device_id = 0;
	mac_id = 0;
	for (i = 0; i < num_hw; i++) {
		for (j = 0; j < radio_per_hw; j++) {
			if (device_id >= ag->num_devices || !ag->ab[device_id]) {
				ret = -ENOSPC;
				goto err;
			}

			ab = ag->ab[device_id];
			pdev_map[j].ab = ab;
			pdev_map[j].pdev_idx = mac_id;
			mac_id++;

			/* If mac_id falls beyond the current device MACs then
			 * move to next device
			 */
			if (mac_id >= ab->num_radios) {
				mac_id = 0;
				device_id++;
			}
		}

		ab = pdev_map->ab;

		ah = ath12k_mac_hw_allocate(ag, pdev_map, radio_per_hw);
		if (!ah) {
			ath12k_warn(ab, "failed to allocate mac80211 hw device for hw_idx %d\n",
				    i);
			ret = -ENOMEM;
			goto err;
		}

		ah->dev = ab->dev;

		ag->ah[i] = ah;
		ag->num_hw++;
	}

	return 0;

err:
	for (i = i - 1; i >= 0; i--) {
		ah = ath12k_ag_to_ah(ag, i);
		if (!ah)
			continue;

		ath12k_mac_hw_destroy(ah);
		ath12k_ag_set_ah(ag, i, NULL);
	}

	return ret;
}

int ath12k_mac_vif_set_keepalive(struct ath12k_link_vif *arvif,
				 enum wmi_sta_keepalive_method method,
				 u32 interval)
{
	struct wmi_sta_keepalive_arg arg = {};
	struct ath12k *ar = arvif->ar;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	if (arvif->ahvif->vdev_type != WMI_VDEV_TYPE_STA)
		return 0;

	if (!test_bit(WMI_TLV_SERVICE_STA_KEEP_ALIVE, ar->ab->wmi_ab.svc_map))
		return 0;

	arg.vdev_id = arvif->vdev_id;
	arg.enabled = 1;
	arg.method = method;
	arg.interval = interval;

	ret = ath12k_wmi_sta_keepalive(ar, &arg);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set keepalive on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	return 0;
}
