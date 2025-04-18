/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_MAC_H
#define ATH12K_MAC_H

#include <net/mac80211.h>
#include <net/cfg80211.h>
#include "wmi.h"

struct ath12k;
struct ath12k_base;
struct ath12k_hw;
struct ath12k_hw_group;
struct ath12k_pdev_map;

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

#define ATH12K_PDEV_TX_POWER_INVALID		((u32)-1)
#define ATH12K_PDEV_TX_POWER_REFRESH_TIME_MSECS	5000 /* msecs */

/* FIXME: should these be in ieee80211.h? */
#define IEEE80211_VHT_MCS_SUPPORT_0_11_MASK	GENMASK(23, 16)
#define IEEE80211_DISABLE_VHT_MCS_SUPPORT_0_11	BIT(24)

#define ATH12K_CHAN_WIDTH_NUM			14

#define ATH12K_TX_POWER_MAX_VAL	70
#define ATH12K_TX_POWER_MIN_VAL	0

#define ATH12K_DEFAULT_LINK_ID	0
#define ATH12K_INVALID_LINK_ID	255

/* Default link after the IEEE802.11 defined Max link id limit
 * for driver usage purpose.
 */
#define ATH12K_DEFAULT_SCAN_LINK	IEEE80211_MLD_MAX_NUM_LINKS
#define ATH12K_NUM_MAX_LINKS		(IEEE80211_MLD_MAX_NUM_LINKS + 1)

enum ath12k_supported_bw {
	ATH12K_BW_20    = 0,
	ATH12K_BW_40    = 1,
	ATH12K_BW_80    = 2,
	ATH12K_BW_160   = 3,
	ATH12K_BW_320   = 4,
};

struct ath12k_mac_get_any_chanctx_conf_arg {
	struct ath12k *ar;
	struct ieee80211_chanctx_conf *chanctx_conf;
};

/**
 * struct ath12k_chan_power_info - TPE containing power info per channel chunk
 * @chan_cfreq: channel center freq (MHz)
 * e.g.
 * channel 37/20 MHz,  it is 6135
 * channel 37/40 MHz,  it is 6125
 * channel 37/80 MHz,  it is 6145
 * channel 37/160 MHz, it is 6185
 * @tx_power: transmit power (dBm)
 */
struct ath12k_chan_power_info {
	u16 chan_cfreq;
	s8 tx_power;
};

/* ath12k only deals with 320 MHz, so 16 subchannels */
#define ATH12K_NUM_PWR_LEVELS  16

/**
 * struct ath12k_reg_tpc_power_info - regulatory TPC power info
 * @is_psd_power: is PSD power or not
 * @eirp_power: Maximum EIRP power (dBm), valid only if power is PSD
 * @ap_power_type: type of power (SP/LPI/VLP)
 * @num_pwr_levels: number of power levels
 * @reg_max: Array of maximum TX power (dBm) per PSD value
 * @ap_constraint_power: AP constraint power (dBm)
 * @tpe: TPE values processed from TPE IE
 * @chan_power_info: power info to send to firmware
 */
struct ath12k_reg_tpc_power_info {
	bool is_psd_power;
	u8 eirp_power;
	enum wmi_reg_6g_ap_type ap_power_type;
	u8 num_pwr_levels;
	u8 reg_max[ATH12K_NUM_PWR_LEVELS];
	u8 ap_constraint_power;
	s8 tpe[ATH12K_NUM_PWR_LEVELS];
	struct ath12k_chan_power_info chan_power_info[ATH12K_NUM_PWR_LEVELS];
};

extern const struct htt_rx_ring_tlv_filter ath12k_mac_mon_status_filter_default;

#define ATH12K_SCAN_11D_INTERVAL		600000
#define ATH12K_11D_INVALID_VDEV_ID		0xFFFF

void ath12k_mac_11d_scan_start(struct ath12k *ar, u32 vdev_id);
void ath12k_mac_11d_scan_stop(struct ath12k *ar);
void ath12k_mac_11d_scan_stop_all(struct ath12k_base *ab);

void ath12k_mac_destroy(struct ath12k_hw_group *ag);
void ath12k_mac_unregister(struct ath12k_hw_group *ag);
int ath12k_mac_register(struct ath12k_hw_group *ag);
int ath12k_mac_allocate(struct ath12k_hw_group *ag);
int ath12k_mac_hw_ratecode_to_legacy_rate(u8 hw_rc, u8 preamble, u8 *rateidx,
					  u16 *rate);
u8 ath12k_mac_bitrate_to_idx(const struct ieee80211_supported_band *sband,
			     u32 bitrate);
u8 ath12k_mac_hw_rate_to_idx(const struct ieee80211_supported_band *sband,
			     u8 hw_rate, bool cck);

void __ath12k_mac_scan_finish(struct ath12k *ar);
void ath12k_mac_scan_finish(struct ath12k *ar);

struct ath12k_link_vif *ath12k_mac_get_arvif(struct ath12k *ar, u32 vdev_id);
struct ath12k_link_vif *ath12k_mac_get_arvif_by_vdev_id(struct ath12k_base *ab,
							u32 vdev_id);
struct ath12k *ath12k_mac_get_ar_by_vdev_id(struct ath12k_base *ab, u32 vdev_id);
struct ath12k *ath12k_mac_get_ar_by_pdev_id(struct ath12k_base *ab, u32 pdev_id);

void ath12k_mac_drain_tx(struct ath12k *ar);
void ath12k_mac_peer_cleanup_all(struct ath12k *ar);
int ath12k_mac_tx_mgmt_pending_free(int buf_id, void *skb, void *ctx);
enum rate_info_bw ath12k_mac_bw_to_mac80211_bw(enum ath12k_supported_bw bw);
enum ath12k_supported_bw ath12k_mac_mac80211_bw_to_ath12k_bw(enum rate_info_bw bw);
enum hal_encrypt_type ath12k_dp_tx_get_encrypt_type(u32 cipher);
int ath12k_mac_rfkill_enable_radio(struct ath12k *ar, bool enable);
int ath12k_mac_rfkill_config(struct ath12k *ar);
int ath12k_mac_wait_tx_complete(struct ath12k *ar);
void ath12k_mac_handle_beacon(struct ath12k *ar, struct sk_buff *skb);
void ath12k_mac_handle_beacon_miss(struct ath12k *ar, u32 vdev_id);
int ath12k_mac_vif_set_keepalive(struct ath12k_link_vif *arvif,
				 enum wmi_sta_keepalive_method method,
				 u32 interval);
u8 ath12k_mac_get_target_pdev_id(struct ath12k *ar);
int ath12k_mac_mlo_setup(struct ath12k_hw_group *ag);
int ath12k_mac_mlo_ready(struct ath12k_hw_group *ag);
void ath12k_mac_mlo_teardown(struct ath12k_hw_group *ag);
int ath12k_mac_vdev_stop(struct ath12k_link_vif *arvif);
void ath12k_mac_get_any_chanctx_conf_iter(struct ieee80211_hw *hw,
					  struct ieee80211_chanctx_conf *conf,
					  void *data);
u16 ath12k_mac_he_convert_tones_to_ru_tones(u16 tones);
enum nl80211_eht_ru_alloc ath12k_mac_eht_ru_tones_to_nl80211_eht_ru_alloc(u16 ru_tones);
enum nl80211_eht_gi ath12k_mac_eht_gi_to_nl80211_eht_gi(u8 sgi);
struct ieee80211_bss_conf *ath12k_mac_get_link_bss_conf(struct ath12k_link_vif *arvif);
struct ath12k *ath12k_get_ar_by_vif(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    u8 link_id);
int ath12k_mac_get_fw_stats(struct ath12k *ar, struct ath12k_fw_stats_req_params *param);
void ath12k_mac_update_freq_range(struct ath12k *ar,
				  u32 freq_low, u32 freq_high);
void ath12k_mac_fill_reg_tpc_info(struct ath12k *ar,
				  struct ath12k_link_vif *arvif,
				  struct ieee80211_chanctx_conf *ctx);
#endif
