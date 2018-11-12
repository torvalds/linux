/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries
 * All rights reserved.
 */

#ifndef HOST_INT_H
#define HOST_INT_H
#include <linux/ieee80211.h>
#include "wilc_wlan_if.h"

enum {
	WILC_IDLE_MODE = 0x0,
	WILC_AP_MODE = 0x1,
	WILC_STATION_MODE = 0x2,
	WILC_GO_MODE = 0x3,
	WILC_CLIENT_MODE = 0x4
};

enum {
	WILC_ADD_KEY = 0x1,
	WILC_REMOVE_KEY = 0x2,
	WILC_DEFAULT_KEY = 0x4,
	WILC_ADD_KEY_AP = 0x8
};

#define WILC_MAX_NUM_STA			9
#define MAX_NUM_SCANNED_NETWORKS		100
#define MAX_NUM_SCANNED_NETWORKS_SHADOW		130
#define WILC_MAX_NUM_PROBED_SSID		10

#define TX_MIC_KEY_LEN				8
#define RX_MIC_KEY_LEN				8
#define PTK_KEY_LEN				16

#define RX_MIC_KEY_MSG_LEN			48
#define PTK_KEY_MSG_LEN				39

#define PMKSA_KEY_LEN				22
#define WILC_MAX_NUM_PMKIDS			16
#define WILC_ADD_STA_LENGTH			40
#define WILC_NUM_CONCURRENT_IFC			2
#define WILC_DRV_HANDLER_SIZE			5
#define DRV_HANDLER_MASK			0x000000FF

#define NUM_RSSI                5

enum {
	WILC_SET_CFG = 0,
	WILC_GET_CFG
};

#define WILC_MAX_ASSOC_RESP_FRAME_SIZE   256

struct rssi_history_buffer {
	bool full;
	u8 index;
	s8 samples[NUM_RSSI];
};

struct network_info {
	s8 rssi;
	u16 cap_info;
	u8 ssid[MAX_SSID_LEN];
	u8 ssid_len;
	u8 bssid[6];
	u16 beacon_period;
	u8 dtim_period;
	u8 ch;
	unsigned long time_scan_cached;
	unsigned long time_scan;
	bool new_network;
	u8 found;
	u32 tsf_lo;
	u8 *ies;
	u16 ies_len;
	void *join_params;
	struct rssi_history_buffer rssi_history;
	u64 tsf;
};

struct connect_info {
	u8 bssid[6];
	u8 *req_ies;
	size_t req_ies_len;
	u8 *resp_ies;
	u16 resp_ies_len;
	u16 status;
};

struct disconnect_info {
	u16 reason;
	u8 *ie;
	size_t ie_len;
};

struct assoc_resp {
	__le16 capab_info;
	__le16 status_code;
	__le16 aid;
} __packed;

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
	HOST_IF_FORCE_32BIT		= 0xFFFFFFFF
};

struct host_if_pmkid {
	u8 bssid[ETH_ALEN];
	u8 pmkid[WLAN_PMKID_LEN];
};

struct host_if_pmkid_attr {
	u8 numpmkid;
	struct host_if_pmkid pmkidlist[WILC_MAX_NUM_PMKIDS];
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

struct found_net_info {
	u8 bssid[6];
	s8 rssi;
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

enum KEY_TYPE {
	WILC_KEY_TYPE_WEP,
	WILC_KEY_TYPE_WPA_RX_GTK,
	WILC_KEY_TYPE_WPA_PTK,
	WILC_KEY_TYPE_PMKSA,
};

typedef void (*wilc_scan_result)(enum scan_event, struct network_info *,
				 void *, void *);

typedef void (*wilc_connect_result)(enum conn_event,
				     struct connect_info *,
				     u8,
				     struct disconnect_info *,
				     void *);

typedef void (*wilc_remain_on_chan_expired)(void *, u32);
typedef void (*wilc_remain_on_chan_ready)(void *);

struct rcvd_net_info {
	u8 *buffer;
	u32 len;
};

struct hidden_net_info {
	u8  *ssid;
	u8 ssid_len;
};

struct hidden_network {
	struct hidden_net_info *net_info;
	u8 n_ssids;
};

struct user_scan_req {
	wilc_scan_result scan_result;
	void *arg;
	u32 ch_cnt;
	struct found_net_info net_info[MAX_NUM_SCANNED_NETWORKS];
};

struct user_conn_req {
	u8 *bssid;
	u8 *ssid;
	u8 security;
	enum authtype auth_type;
	size_t ssid_len;
	u8 *ies;
	size_t ies_len;
	wilc_connect_result conn_result;
	bool ht_capable;
	void *arg;
};

struct drv_handler {
	u32 handler;
	u8 mode;
	u8 name;
};

struct op_mode {
	u32 mode;
};

struct get_mac_addr {
	u8 *mac_addr;
};

struct ba_session_info {
	u8 bssid[ETH_ALEN];
	u8 tid;
	u16 buf_size;
	u16 time_out;
};

struct remain_ch {
	u16 ch;
	u32 duration;
	wilc_remain_on_chan_expired expired;
	wilc_remain_on_chan_ready ready;
	void *arg;
	u32 id;
};

struct reg_frame {
	bool reg;
	u16 frame_type;
	u8 reg_id;
};

struct wilc;
struct host_if_drv {
	struct user_scan_req usr_scan_req;
	struct user_conn_req usr_conn_req;
	struct remain_ch remain_on_ch;
	u8 remain_on_ch_pending;
	u64 p2p_timeout;
	u8 p2p_connect;

	enum host_if_state hif_state;

	u8 assoc_bssid[ETH_ALEN];

	struct timer_list scan_timer;
	struct wilc_vif *scan_timer_vif;

	struct timer_list connect_timer;
	struct wilc_vif *connect_timer_vif;

	struct timer_list remain_on_ch_timer;
	struct wilc_vif *remain_on_ch_timer_vif;

	bool ifc_up;
	int driver_handler_id;
	u8 assoc_resp[WILC_MAX_ASSOC_RESP_FRAME_SIZE];
};

struct add_sta_param {
	u8 bssid[ETH_ALEN];
	u16 aid;
	u8 rates_len;
	const u8 *rates;
	bool ht_supported;
	struct ieee80211_ht_cap ht_capa;
	u16 flags_mask;
	u16 flags_set;
};

struct wilc_vif;
int wilc_remove_wep_key(struct wilc_vif *vif, u8 index);
int wilc_set_wep_default_keyid(struct wilc_vif *vif, u8 index);
int wilc_add_wep_key_bss_sta(struct wilc_vif *vif, const u8 *key, u8 len,
			     u8 index);
int wilc_add_wep_key_bss_ap(struct wilc_vif *vif, const u8 *key, u8 len,
			    u8 index, u8 mode, enum authtype auth_type);
int wilc_add_ptk(struct wilc_vif *vif, const u8 *ptk, u8 ptk_key_len,
		 const u8 *mac_addr, const u8 *rx_mic, const u8 *tx_mic,
		 u8 mode, u8 cipher_mode, u8 index);
s32 wilc_get_inactive_time(struct wilc_vif *vif, const u8 *mac,
			   u32 *out_val);
int wilc_add_rx_gtk(struct wilc_vif *vif, const u8 *rx_gtk, u8 gtk_key_len,
		    u8 index, u32 key_rsc_len, const u8 *key_rsc,
		    const u8 *rx_mic, const u8 *tx_mic, u8 mode,
		    u8 cipher_mode);
int wilc_set_pmkid_info(struct wilc_vif *vif,
			struct host_if_pmkid_attr *pmkid);
int wilc_get_mac_address(struct wilc_vif *vif, u8 *mac_addr);
int wilc_set_join_req(struct wilc_vif *vif, u8 *bssid, const u8 *ssid,
		      size_t ssid_len, const u8 *ies, size_t ies_len,
		      wilc_connect_result connect_result, void *user_arg,
		      u8 security, enum authtype auth_type,
		      u8 channel, void *join_params);
int wilc_disconnect(struct wilc_vif *vif, u16 reason_code);
int wilc_set_mac_chnl_num(struct wilc_vif *vif, u8 channel);
int wilc_get_rssi(struct wilc_vif *vif, s8 *rssi_level);
int wilc_scan(struct wilc_vif *vif, u8 scan_source, u8 scan_type,
	      u8 *ch_freq_list, u8 ch_list_len, const u8 *ies,
	      size_t ies_len, wilc_scan_result scan_result, void *user_arg,
	      struct hidden_network *hidden_network);
int wilc_hif_set_cfg(struct wilc_vif *vif,
		     struct cfg_param_attr *cfg_param);
int wilc_init(struct net_device *dev, struct host_if_drv **hif_drv_handler);
int wilc_deinit(struct wilc_vif *vif);
int wilc_add_beacon(struct wilc_vif *vif, u32 interval, u32 dtim_period,
		    u32 head_len, u8 *head, u32 tail_len, u8 *tail);
int wilc_del_beacon(struct wilc_vif *vif);
int wilc_add_station(struct wilc_vif *vif, struct add_sta_param *sta_param);
int wilc_del_allstation(struct wilc_vif *vif, u8 mac_addr[][ETH_ALEN]);
int wilc_del_station(struct wilc_vif *vif, const u8 *mac_addr);
int wilc_edit_station(struct wilc_vif *vif,
		      struct add_sta_param *sta_param);
int wilc_set_power_mgmt(struct wilc_vif *vif, bool enabled, u32 timeout);
int wilc_setup_multicast_filter(struct wilc_vif *vif, bool enabled, u32 count,
				u8 *mc_list);
int wilc_remain_on_channel(struct wilc_vif *vif, u32 session_id,
			   u32 duration, u16 chan,
			   wilc_remain_on_chan_expired expired,
			   wilc_remain_on_chan_ready ready,
			   void *user_arg);
int wilc_listen_state_expired(struct wilc_vif *vif, u32 session_id);
void wilc_frame_register(struct wilc_vif *vif, u16 frame_type, bool reg);
int wilc_set_wfi_drv_handler(struct wilc_vif *vif, int index, u8 mode,
			     u8 ifc_id, bool is_sync);
int wilc_set_operation_mode(struct wilc_vif *vif, u32 mode);
int wilc_get_statistics(struct wilc_vif *vif, struct rf_info *stats,
			bool is_sync);
void wilc_resolve_disconnect_aberration(struct wilc_vif *vif);
int wilc_get_vif_idx(struct wilc_vif *vif);
int wilc_set_tx_power(struct wilc_vif *vif, u8 tx_power);
int wilc_get_tx_power(struct wilc_vif *vif, u8 *tx_power);
void wilc_scan_complete_received(struct wilc *wilc, u8 *buffer, u32 length);
void wilc_network_info_received(struct wilc *wilc, u8 *buffer, u32 length);
void wilc_gnrl_async_info_received(struct wilc *wilc, u8 *buffer, u32 length);
#endif
