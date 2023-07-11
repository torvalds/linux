/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries
 * All rights reserved.
 */

#ifndef WILC_HIF_H
#define WILC_HIF_H
#include <linux/ieee80211.h>
#include "wlan_if.h"

enum {
	WILC_IDLE_MODE = 0x0,
	WILC_AP_MODE = 0x1,
	WILC_STATION_MODE = 0x2,
	WILC_GO_MODE = 0x3,
	WILC_CLIENT_MODE = 0x4
};

#define WILC_MAX_NUM_PROBED_SSID		10

#define WILC_TX_MIC_KEY_LEN			8
#define WILC_RX_MIC_KEY_LEN			8

#define WILC_ADD_STA_LENGTH			40
#define WILC_NUM_CONCURRENT_IFC			2

enum {
	WILC_SET_CFG = 0,
	WILC_GET_CFG
};

struct rf_info {
	u8 link_speed;
	s8 rssi;
	u32 tx_cnt;
	u32 rx_cnt;
	u32 tx_fail_cnt;
};

enum host_if_state {
	HOST_IF_IDLE			= 0,
	HOST_IF_SCANNING		= 1,
	HOST_IF_CONNECTING		= 2,
	HOST_IF_WAITING_CONN_RESP	= 3,
	HOST_IF_CONNECTED		= 4,
	HOST_IF_P2P_LISTEN		= 5,
	HOST_IF_EXTERNAL_AUTH           = 6,
	HOST_IF_FORCE_32BIT		= 0xFFFFFFFF
};

struct cfg_param_attr {
	u32 flag;
	u16 short_retry_limit;
	u16 long_retry_limit;
	u16 frag_threshold;
	u16 rts_threshold;
};

enum cfg_param {
	WILC_CFG_PARAM_RETRY_SHORT = BIT(0),
	WILC_CFG_PARAM_RETRY_LONG = BIT(1),
	WILC_CFG_PARAM_FRAG_THRESHOLD = BIT(2),
	WILC_CFG_PARAM_RTS_THRESHOLD = BIT(3)
};

enum scan_event {
	SCAN_EVENT_NETWORK_FOUND	= 0,
	SCAN_EVENT_DONE			= 1,
	SCAN_EVENT_ABORTED		= 2,
	SCAN_EVENT_FORCE_32BIT		= 0xFFFFFFFF
};

enum conn_event {
	CONN_DISCONN_EVENT_CONN_RESP		= 0,
	CONN_DISCONN_EVENT_DISCONN_NOTIF	= 1,
	CONN_DISCONN_EVENT_FORCE_32BIT		= 0xFFFFFFFF
};

enum {
	WILC_HIF_SDIO = 0,
	WILC_HIF_SPI = BIT(0)
};

enum {
	WILC_MAC_STATUS_INIT = -1,
	WILC_MAC_STATUS_DISCONNECTED = 0,
	WILC_MAC_STATUS_CONNECTED = 1
};

struct wilc_rcvd_net_info {
	s8 rssi;
	u8 ch;
	u16 frame_len;
	struct ieee80211_mgmt *mgmt;
};

struct wilc_user_scan_req {
	void (*scan_result)(enum scan_event evt,
			    struct wilc_rcvd_net_info *info, void *priv);
	void *arg;
	u32 ch_cnt;
};

struct wilc_conn_info {
	u8 bssid[ETH_ALEN];
	u8 security;
	enum authtype auth_type;
	enum mfptype mfp_type;
	u8 ch;
	u8 *req_ies;
	size_t req_ies_len;
	u8 *resp_ies;
	u16 resp_ies_len;
	u16 status;
	void (*conn_result)(enum conn_event evt, u8 status, void *priv_data);
	void *arg;
	void *param;
};

struct wilc_remain_ch {
	u16 ch;
	u32 duration;
	void (*expired)(void *priv, u64 cookie);
	void *arg;
	u64 cookie;
};

struct wilc;
struct host_if_drv {
	struct wilc_user_scan_req usr_scan_req;
	struct wilc_conn_info conn_info;
	struct wilc_remain_ch remain_on_ch;
	u64 p2p_timeout;

	enum host_if_state hif_state;

	u8 assoc_bssid[ETH_ALEN];

	struct timer_list scan_timer;
	struct wilc_vif *scan_timer_vif;

	struct timer_list connect_timer;
	struct wilc_vif *connect_timer_vif;

	struct timer_list remain_on_ch_timer;
	struct wilc_vif *remain_on_ch_timer_vif;

	bool ifc_up;
	u8 assoc_resp[WILC_MAX_ASSOC_RESP_FRAME_SIZE];
};

struct wilc_vif;
int wilc_add_ptk(struct wilc_vif *vif, const u8 *ptk, u8 ptk_key_len,
		 const u8 *mac_addr, const u8 *rx_mic, const u8 *tx_mic,
		 u8 mode, u8 cipher_mode, u8 index);
int wilc_add_igtk(struct wilc_vif *vif, const u8 *igtk, u8 igtk_key_len,
		  const u8 *pn, u8 pn_len, const u8 *mac_addr, u8 mode,
		  u8 index);
s32 wilc_get_inactive_time(struct wilc_vif *vif, const u8 *mac,
			   u32 *out_val);
int wilc_add_rx_gtk(struct wilc_vif *vif, const u8 *rx_gtk, u8 gtk_key_len,
		    u8 index, u32 key_rsc_len, const u8 *key_rsc,
		    const u8 *rx_mic, const u8 *tx_mic, u8 mode,
		    u8 cipher_mode);
int wilc_set_pmkid_info(struct wilc_vif *vif, struct wilc_pmkid_attr *pmkid);
int wilc_get_mac_address(struct wilc_vif *vif, u8 *mac_addr);
int wilc_set_mac_address(struct wilc_vif *vif, u8 *mac_addr);
int wilc_set_join_req(struct wilc_vif *vif, u8 *bssid, const u8 *ies,
		      size_t ies_len);
int wilc_disconnect(struct wilc_vif *vif);
int wilc_set_mac_chnl_num(struct wilc_vif *vif, u8 channel);
int wilc_get_rssi(struct wilc_vif *vif, s8 *rssi_level);
int wilc_scan(struct wilc_vif *vif, u8 scan_source, u8 scan_type,
	      u8 *ch_freq_list, u8 ch_list_len,
	      void (*scan_result_fn)(enum scan_event,
				     struct wilc_rcvd_net_info *, void *),
	      void *user_arg, struct cfg80211_scan_request *request);
int wilc_hif_set_cfg(struct wilc_vif *vif,
		     struct cfg_param_attr *cfg_param);
int wilc_init(struct net_device *dev, struct host_if_drv **hif_drv_handler);
int wilc_deinit(struct wilc_vif *vif);
int wilc_add_beacon(struct wilc_vif *vif, u32 interval, u32 dtim_period,
		    struct cfg80211_beacon_data *params);
int wilc_del_beacon(struct wilc_vif *vif);
int wilc_add_station(struct wilc_vif *vif, const u8 *mac,
		     struct station_parameters *params);
int wilc_del_allstation(struct wilc_vif *vif, u8 mac_addr[][ETH_ALEN]);
int wilc_del_station(struct wilc_vif *vif, const u8 *mac_addr);
int wilc_edit_station(struct wilc_vif *vif, const u8 *mac,
		      struct station_parameters *params);
int wilc_set_power_mgmt(struct wilc_vif *vif, bool enabled, u32 timeout);
int wilc_setup_multicast_filter(struct wilc_vif *vif, u32 enabled, u32 count,
				u8 *mc_list);
int wilc_remain_on_channel(struct wilc_vif *vif, u64 cookie,
			   u32 duration, u16 chan,
			   void (*expired)(void *, u64),
			   void *user_arg);
int wilc_listen_state_expired(struct wilc_vif *vif, u64 cookie);
void wilc_frame_register(struct wilc_vif *vif, u16 frame_type, bool reg);
int wilc_set_operation_mode(struct wilc_vif *vif, int index, u8 mode,
			    u8 ifc_id);
int wilc_get_statistics(struct wilc_vif *vif, struct rf_info *stats);
int wilc_get_vif_idx(struct wilc_vif *vif);
int wilc_set_tx_power(struct wilc_vif *vif, u8 tx_power);
int wilc_get_tx_power(struct wilc_vif *vif, u8 *tx_power);
void wilc_set_wowlan_trigger(struct wilc_vif *vif, bool enabled);
int wilc_set_external_auth_param(struct wilc_vif *vif,
				 struct cfg80211_external_auth_params *param);
void wilc_scan_complete_received(struct wilc *wilc, u8 *buffer, u32 length);
void wilc_network_info_received(struct wilc *wilc, u8 *buffer, u32 length);
void wilc_gnrl_async_info_received(struct wilc *wilc, u8 *buffer, u32 length);
void *wilc_parse_join_bss_param(struct cfg80211_bss *bss,
				struct cfg80211_crypto_settings *crypto);
int wilc_set_default_mgmt_key_index(struct wilc_vif *vif, u8 index);
void wilc_handle_disconnect(struct wilc_vif *vif);

#endif
