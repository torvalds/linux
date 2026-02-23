/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
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
#define ATH12K_BW_NSS_MAP_ENABLE		BIT(31)
#define ATH12K_PEER_RX_NSS_160MHZ		GENMASK(2, 0)

#define ATH12K_TX_POWER_MAX_VAL	70
#define ATH12K_TX_POWER_MIN_VAL	0

#define ATH12K_DEFAULT_LINK_ID	0
#define ATH12K_INVALID_LINK_ID	255

/* Default link after the IEEE802.11 defined Max link id limit
 * for driver usage purpose.
 */
#define ATH12K_FIRST_SCAN_LINK	IEEE80211_MLD_MAX_NUM_LINKS
#define ATH12K_SCAN_LINKS_MASK	GENMASK(ATH12K_NUM_MAX_LINKS, IEEE80211_MLD_MAX_NUM_LINKS)

#define ATH12K_NUM_MAX_ACTIVE_LINKS_PER_DEVICE	2

#define HECAP_PHY_SUBFMR_GET(hecap_phy) \
	u8_get_bits(hecap_phy[3], IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER)

#define HECAP_PHY_SUBFME_GET(hecap_phy) \
	u8_get_bits(hecap_phy[4], IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE)

#define HECAP_PHY_MUBFMR_GET(hecap_phy) \
	u8_get_bits(hecap_phy[4], IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER)

#define HECAP_PHY_ULMUMIMO_GET(hecap_phy) \
	u8_get_bits(hecap_phy[2], IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO)

#define HECAP_PHY_ULOFDMA_GET(hecap_phy) \
	u8_get_bits(hecap_phy[2], IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO)

enum ath12k_supported_bw {
	ATH12K_BW_20    = 0,
	ATH12K_BW_40    = 1,
	ATH12K_BW_80    = 2,
	ATH12K_BW_160   = 3,
	ATH12K_BW_320   = 4,
};

enum ath12k_gi {
	ATH12K_RATE_INFO_GI_0_8,
	ATH12K_RATE_INFO_GI_1_6,
	ATH12K_RATE_INFO_GI_3_2,
};

enum ath12k_ltf {
	ATH12K_RATE_INFO_1XLTF,
	ATH12K_RATE_INFO_2XLTF,
	ATH12K_RATE_INFO_4XLTF,
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

#define ATH12K_OBSS_PD_MAX_THRESHOLD		-82
#define ATH12K_OBSS_PD_NON_SRG_MAX_THRESHOLD	-62

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
void ath12k_mac_dp_peer_cleanup(struct ath12k_hw *ah);
int ath12k_mac_tx_mgmt_pending_free(int buf_id, void *skb, void *ctx);
enum rate_info_bw ath12k_mac_bw_to_mac80211_bw(enum ath12k_supported_bw bw);
enum ath12k_supported_bw ath12k_mac_mac80211_bw_to_ath12k_bw(enum rate_info_bw bw);
enum hal_encrypt_type ath12k_dp_tx_get_encrypt_type(u32 cipher);
int ath12k_mac_rfkill_enable_radio(struct ath12k *ar, bool enable);
int ath12k_mac_rfkill_config(struct ath12k *ar);
int ath12k_mac_wait_tx_complete(struct ath12k *ar);
void ath12k_mac_handle_beacon(struct ath12k *ar, struct sk_buff *skb);
void ath12k_mac_handle_beacon_miss(struct ath12k *ar,
				   struct ath12k_link_vif *arvif);
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
int ath12k_mac_op_start(struct ieee80211_hw *hw);
void ath12k_mac_op_stop(struct ieee80211_hw *hw, bool suspend);
void
ath12k_mac_op_reconfig_complete(struct ieee80211_hw *hw,
				enum ieee80211_reconfig_type reconfig_type);
int ath12k_mac_op_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif);
void ath12k_mac_op_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif);
void ath12k_mac_op_update_vif_offload(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif);
int ath12k_mac_op_config(struct ieee80211_hw *hw, int radio_idx, u32 changed);
void ath12k_mac_op_link_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *info,
				     u64 changed);
void ath12k_mac_op_vif_cfg_changed(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   u64 changed);
int
ath12k_mac_op_change_vif_links
			(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 u16 old_links, u16 new_links,
			 struct ieee80211_bss_conf *ol[IEEE80211_MLD_MAX_NUM_LINKS]);
void ath12k_mac_op_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast);
int ath12k_mac_op_hw_scan(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  struct ieee80211_scan_request *hw_req);
void ath12k_mac_op_cancel_hw_scan(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif);
int ath12k_mac_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key);
void ath12k_mac_op_set_rekey_data(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct cfg80211_gtk_rekey_data *data);
int ath12k_mac_op_sta_state(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta,
			    enum ieee80211_sta_state old_state,
			    enum ieee80211_sta_state new_state);
int ath12k_mac_op_sta_set_txpwr(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta);
void ath12k_mac_op_link_sta_rc_update(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_link_sta *link_sta,
				      u32 changed);
int ath12k_mac_op_conf_tx(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  unsigned int link_id, u16 ac,
			  const struct ieee80211_tx_queue_params *params);
int ath12k_mac_op_set_antenna(struct ieee80211_hw *hw, int radio_idx,
			      u32 tx_ant, u32 rx_ant);
int ath12k_mac_op_get_antenna(struct ieee80211_hw *hw, int radio_idx,
			      u32 *tx_ant, u32 *rx_ant);
int ath12k_mac_op_ampdu_action(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_ampdu_params *params);
int ath12k_mac_op_add_chanctx(struct ieee80211_hw *hw,
			      struct ieee80211_chanctx_conf *ctx);
void ath12k_mac_op_remove_chanctx(struct ieee80211_hw *hw,
				  struct ieee80211_chanctx_conf *ctx);
void ath12k_mac_op_change_chanctx(struct ieee80211_hw *hw,
				  struct ieee80211_chanctx_conf *ctx,
				  u32 changed);
int
ath12k_mac_op_assign_vif_chanctx(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *link_conf,
				 struct ieee80211_chanctx_conf *ctx);
void
ath12k_mac_op_unassign_vif_chanctx(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *link_conf,
				   struct ieee80211_chanctx_conf *ctx);
int
ath12k_mac_op_switch_vif_chanctx(struct ieee80211_hw *hw,
				 struct ieee80211_vif_chanctx_switch *vifs,
				 int n_vifs,
				 enum ieee80211_chanctx_switch_mode mode);
int ath12k_mac_op_set_rts_threshold(struct ieee80211_hw *hw,
				    int radio_idx, u32 value);
int ath12k_mac_op_set_frag_threshold(struct ieee80211_hw *hw,
				     int radio_idx, u32 value);
int
ath12k_mac_op_set_bitrate_mask(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       const struct cfg80211_bitrate_mask *mask);
int ath12k_mac_op_get_survey(struct ieee80211_hw *hw, int idx,
			     struct survey_info *survey);
void ath12k_mac_op_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 u32 queues, bool drop);
void ath12k_mac_op_sta_statistics(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  struct station_info *sinfo);
void ath12k_mac_op_link_sta_statistics(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_link_sta *link_sta,
				       struct link_station_info *link_sinfo);
int ath12k_mac_op_remain_on_channel(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_channel *chan,
				    int duration,
				    enum ieee80211_roc_type type);
int ath12k_mac_op_cancel_remain_on_channel(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif);
int ath12k_mac_op_change_sta_links(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   u16 old_links, u16 new_links);
bool ath12k_mac_op_can_activate_links(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      u16 active_links);
int ath12k_mac_op_get_txpower(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      unsigned int link_id,
			      int *dbm);
int ath12k_mac_mgmt_tx(struct ath12k *ar, struct sk_buff *skb,
		       bool is_prb_rsp);
void ath12k_mac_add_p2p_noa_ie(struct ath12k *ar,
			       struct ieee80211_vif *vif,
			       struct sk_buff *skb,
			       bool is_prb_rsp);
u8 ath12k_mac_get_tx_link(struct ieee80211_sta *sta, struct ieee80211_vif *vif,
			  u8 link, struct sk_buff *skb, u32 info_flags);

void ath12k_mlo_mcast_update_tx_link_address(struct ieee80211_vif *vif,
					     u8 link_id, struct sk_buff *skb,
					     u32 info_flags);
#endif
