/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_MAC_H
#define ATH12K_MAC_H

#include <net/mac80211.h>
#include <net/cfg80211.h>

struct ath12k;
struct ath12k_base;

struct ath12k_generic_iter {
	struct ath12k *ar;
	int ret;
};

/* number of failed packets (20 packets with 16 sw reties each) */
#define ATH12K_KICKOUT_THRESHOLD		(20 * 16)

/* Use insanely high numbers to make sure that the firmware implementation
 * won't start, we have the same functionality already in hostapd. Unit
 * is seconds.
 */
#define ATH12K_KEEPALIVE_MIN_IDLE		3747
#define ATH12K_KEEPALIVE_MAX_IDLE		3895
#define ATH12K_KEEPALIVE_MAX_UNRESPONSIVE	3900

/* FIXME: should these be in ieee80211.h? */
#define IEEE80211_VHT_MCS_SUPPORT_0_11_MASK	GENMASK(23, 16)
#define IEEE80211_DISABLE_VHT_MCS_SUPPORT_0_11	BIT(24)

#define ATH12K_CHAN_WIDTH_NUM			8

#define ATH12K_TX_POWER_MAX_VAL	70
#define ATH12K_TX_POWER_MIN_VAL	0

enum ath12k_supported_bw {
	ATH12K_BW_20    = 0,
	ATH12K_BW_40    = 1,
	ATH12K_BW_80    = 2,
	ATH12K_BW_160   = 3,
};

extern const struct htt_rx_ring_tlv_filter ath12k_mac_mon_status_filter_default;

void ath12k_mac_destroy(struct ath12k_base *ab);
void ath12k_mac_unregister(struct ath12k_base *ab);
int ath12k_mac_register(struct ath12k_base *ab);
int ath12k_mac_allocate(struct ath12k_base *ab);
int ath12k_mac_hw_ratecode_to_legacy_rate(u8 hw_rc, u8 preamble, u8 *rateidx,
					  u16 *rate);
u8 ath12k_mac_bitrate_to_idx(const struct ieee80211_supported_band *sband,
			     u32 bitrate);
u8 ath12k_mac_hw_rate_to_idx(const struct ieee80211_supported_band *sband,
			     u8 hw_rate, bool cck);

void __ath12k_mac_scan_finish(struct ath12k *ar);
void ath12k_mac_scan_finish(struct ath12k *ar);

struct ath12k_vif *ath12k_mac_get_arvif(struct ath12k *ar, u32 vdev_id);
struct ath12k_vif *ath12k_mac_get_arvif_by_vdev_id(struct ath12k_base *ab,
						   u32 vdev_id);
struct ath12k *ath12k_mac_get_ar_by_vdev_id(struct ath12k_base *ab, u32 vdev_id);
struct ath12k *ath12k_mac_get_ar_by_pdev_id(struct ath12k_base *ab, u32 pdev_id);

void ath12k_mac_drain_tx(struct ath12k *ar);
void ath12k_mac_peer_cleanup_all(struct ath12k *ar);
int ath12k_mac_tx_mgmt_pending_free(int buf_id, void *skb, void *ctx);
enum rate_info_bw ath12k_mac_bw_to_mac80211_bw(enum ath12k_supported_bw bw);
enum ath12k_supported_bw ath12k_mac_mac80211_bw_to_ath12k_bw(enum rate_info_bw bw);
enum hal_encrypt_type ath12k_dp_tx_get_encrypt_type(u32 cipher);
#endif
