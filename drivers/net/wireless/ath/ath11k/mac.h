/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#ifndef ATH11K_MAC_H
#define ATH11K_MAC_H

#include <net/mac80211.h>
#include <net/cfg80211.h>

struct ath11k;
struct ath11k_base;

struct ath11k_generic_iter {
	struct ath11k *ar;
	int ret;
};

/* number of failed packets (20 packets with 16 sw reties each) */
#define ATH11K_KICKOUT_THRESHOLD		(20 * 16)

/* Use insanely high numbers to make sure that the firmware implementation
 * won't start, we have the same functionality already in hostapd. Unit
 * is seconds.
 */
#define ATH11K_KEEPALIVE_MIN_IDLE		3747
#define ATH11K_KEEPALIVE_MAX_IDLE		3895
#define ATH11K_KEEPALIVE_MAX_UNRESPONSIVE	3900

#define WMI_HOST_RC_DS_FLAG			0x01
#define WMI_HOST_RC_CW40_FLAG			0x02
#define WMI_HOST_RC_SGI_FLAG			0x04
#define WMI_HOST_RC_HT_FLAG			0x08
#define WMI_HOST_RC_RTSCTS_FLAG			0x10
#define WMI_HOST_RC_TX_STBC_FLAG		0x20
#define WMI_HOST_RC_RX_STBC_FLAG		0xC0
#define WMI_HOST_RC_RX_STBC_FLAG_S		6
#define WMI_HOST_RC_WEP_TKIP_FLAG		0x100
#define WMI_HOST_RC_TS_FLAG			0x200
#define WMI_HOST_RC_UAPSD_FLAG			0x400

#define WMI_HT_CAP_ENABLED			0x0001
#define WMI_HT_CAP_HT20_SGI			0x0002
#define WMI_HT_CAP_DYNAMIC_SMPS			0x0004
#define WMI_HT_CAP_TX_STBC			0x0008
#define WMI_HT_CAP_TX_STBC_MASK_SHIFT		3
#define WMI_HT_CAP_RX_STBC			0x0030
#define WMI_HT_CAP_RX_STBC_MASK_SHIFT		4
#define WMI_HT_CAP_LDPC				0x0040
#define WMI_HT_CAP_L_SIG_TXOP_PROT		0x0080
#define WMI_HT_CAP_MPDU_DENSITY			0x0700
#define WMI_HT_CAP_MPDU_DENSITY_MASK_SHIFT	8
#define WMI_HT_CAP_HT40_SGI			0x0800
#define WMI_HT_CAP_RX_LDPC			0x1000
#define WMI_HT_CAP_TX_LDPC			0x2000
#define WMI_HT_CAP_IBF_BFER			0x4000

/* These macros should be used when we wish to advertise STBC support for
 * only 1SS or 2SS or 3SS.
 */
#define WMI_HT_CAP_RX_STBC_1SS			0x0010
#define WMI_HT_CAP_RX_STBC_2SS			0x0020
#define WMI_HT_CAP_RX_STBC_3SS			0x0030

#define WMI_HT_CAP_DEFAULT_ALL (WMI_HT_CAP_ENABLED    | \
				WMI_HT_CAP_HT20_SGI   | \
				WMI_HT_CAP_HT40_SGI   | \
				WMI_HT_CAP_TX_STBC    | \
				WMI_HT_CAP_RX_STBC    | \
				WMI_HT_CAP_LDPC)

#define WMI_VHT_CAP_MAX_MPDU_LEN_MASK		0x00000003
#define WMI_VHT_CAP_RX_LDPC			0x00000010
#define WMI_VHT_CAP_SGI_80MHZ			0x00000020
#define WMI_VHT_CAP_SGI_160MHZ			0x00000040
#define WMI_VHT_CAP_TX_STBC			0x00000080
#define WMI_VHT_CAP_RX_STBC_MASK		0x00000300
#define WMI_VHT_CAP_RX_STBC_MASK_SHIFT		8
#define WMI_VHT_CAP_SU_BFER			0x00000800
#define WMI_VHT_CAP_SU_BFEE			0x00001000
#define WMI_VHT_CAP_MAX_CS_ANT_MASK		0x0000E000
#define WMI_VHT_CAP_MAX_CS_ANT_MASK_SHIFT	13
#define WMI_VHT_CAP_MAX_SND_DIM_MASK		0x00070000
#define WMI_VHT_CAP_MAX_SND_DIM_MASK_SHIFT	16
#define WMI_VHT_CAP_MU_BFER			0x00080000
#define WMI_VHT_CAP_MU_BFEE			0x00100000
#define WMI_VHT_CAP_MAX_AMPDU_LEN_EXP		0x03800000
#define WMI_VHT_CAP_MAX_AMPDU_LEN_EXP_SHIT	23
#define WMI_VHT_CAP_RX_FIXED_ANT		0x10000000
#define WMI_VHT_CAP_TX_FIXED_ANT		0x20000000

#define WMI_VHT_CAP_MAX_MPDU_LEN_11454		0x00000002

/* These macros should be used when we wish to advertise STBC support for
 * only 1SS or 2SS or 3SS.
 */
#define WMI_VHT_CAP_RX_STBC_1SS			0x00000100
#define WMI_VHT_CAP_RX_STBC_2SS			0x00000200
#define WMI_VHT_CAP_RX_STBC_3SS			0x00000300

#define WMI_VHT_CAP_DEFAULT_ALL (WMI_VHT_CAP_MAX_MPDU_LEN_11454  | \
				 WMI_VHT_CAP_SGI_80MHZ      |       \
				 WMI_VHT_CAP_TX_STBC        |       \
				 WMI_VHT_CAP_RX_STBC_MASK   |       \
				 WMI_VHT_CAP_RX_LDPC        |       \
				 WMI_VHT_CAP_MAX_AMPDU_LEN_EXP   |  \
				 WMI_VHT_CAP_RX_FIXED_ANT   |       \
				 WMI_VHT_CAP_TX_FIXED_ANT)

/* FIXME: should these be in ieee80211.h? */
#define IEEE80211_VHT_MCS_SUPPORT_0_11_MASK	GENMASK(23, 16)
#define IEEE80211_DISABLE_VHT_MCS_SUPPORT_0_11	BIT(24)

#define WMI_MAX_SPATIAL_STREAM			3

#define ATH11K_CHAN_WIDTH_NUM			8
#define ATH11K_BW_NSS_MAP_ENABLE		BIT(31)
#define ATH11K_PEER_RX_NSS_160MHZ		GENMASK(2, 0)
#define ATH11K_PEER_RX_NSS_80_80MHZ		GENMASK(5, 3)

#define ATH11K_OBSS_PD_MAX_THRESHOLD			-82
#define ATH11K_OBSS_PD_NON_SRG_MAX_THRESHOLD		-62
#define ATH11K_OBSS_PD_THRESHOLD_IN_DBM			BIT(29)
#define ATH11K_OBSS_PD_SRG_EN				BIT(30)
#define ATH11K_OBSS_PD_NON_SRG_EN			BIT(31)

extern const struct htt_rx_ring_tlv_filter ath11k_mac_mon_status_filter_default;

#define ATH11K_SCAN_11D_INTERVAL		600000
#define ATH11K_11D_INVALID_VDEV_ID		0xFFFF

void ath11k_mac_11d_scan_start(struct ath11k *ar, u32 vdev_id);
void ath11k_mac_11d_scan_stop(struct ath11k *ar);
void ath11k_mac_11d_scan_stop_all(struct ath11k_base *ab);

void ath11k_mac_destroy(struct ath11k_base *ab);
void ath11k_mac_unregister(struct ath11k_base *ab);
int ath11k_mac_register(struct ath11k_base *ab);
int ath11k_mac_allocate(struct ath11k_base *ab);
int ath11k_mac_hw_ratecode_to_legacy_rate(u8 hw_rc, u8 preamble, u8 *rateidx,
					  u16 *rate);
u8 ath11k_mac_bitrate_to_idx(const struct ieee80211_supported_band *sband,
			     u32 bitrate);
u8 ath11k_mac_hw_rate_to_idx(const struct ieee80211_supported_band *sband,
			     u8 hw_rate, bool cck);

void __ath11k_mac_scan_finish(struct ath11k *ar);
void ath11k_mac_scan_finish(struct ath11k *ar);
int ath11k_mac_rfkill_enable_radio(struct ath11k *ar, bool enable);
int ath11k_mac_rfkill_config(struct ath11k *ar);

struct ath11k_vif *ath11k_mac_get_arvif(struct ath11k *ar, u32 vdev_id);
struct ath11k_vif *ath11k_mac_get_arvif_by_vdev_id(struct ath11k_base *ab,
						   u32 vdev_id);
u8 ath11k_mac_get_target_pdev_id(struct ath11k *ar);
u8 ath11k_mac_get_target_pdev_id_from_vif(struct ath11k_vif *arvif);
struct ath11k_vif *ath11k_mac_get_vif_up(struct ath11k_base *ab);

struct ath11k *ath11k_mac_get_ar_by_vdev_id(struct ath11k_base *ab, u32 vdev_id);
struct ath11k *ath11k_mac_get_ar_by_pdev_id(struct ath11k_base *ab, u32 pdev_id);

void ath11k_mac_drain_tx(struct ath11k *ar);
void ath11k_mac_peer_cleanup_all(struct ath11k *ar);
int ath11k_mac_tx_mgmt_pending_free(int buf_id, void *skb, void *ctx);
u8 ath11k_mac_bw_to_mac80211_bw(u8 bw);
u32 ath11k_mac_he_gi_to_nl80211_he_gi(u8 sgi);
enum nl80211_he_ru_alloc ath11k_mac_phy_he_ru_to_nl80211_he_ru_alloc(u16 ru_phy);
enum nl80211_he_ru_alloc ath11k_mac_he_ru_tones_to_nl80211_he_ru_alloc(u16 ru_tones);
enum ath11k_supported_bw ath11k_mac_mac80211_bw_to_ath11k_bw(enum rate_info_bw bw);
enum hal_encrypt_type ath11k_dp_tx_get_encrypt_type(u32 cipher);
void ath11k_mac_handle_beacon(struct ath11k *ar, struct sk_buff *skb);
void ath11k_mac_handle_beacon_miss(struct ath11k *ar, u32 vdev_id);
void ath11k_mac_bcn_tx_event(struct ath11k_vif *arvif);
#endif
