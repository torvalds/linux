/* SPDX-License-Identifier: GPL-2.0 */
#ifndef HOST_INT_H
#define HOST_INT_H
#include <linux/ieee80211.h>
#include "coreconfigurator.h"

#define IP_ALEN  4

#define IDLE_MODE	0x00
#define AP_MODE		0x01
#define STATION_MODE	0x02
#define GO_MODE		0x03
#define CLIENT_MODE	0x04
#define ACTION		0xD0
#define PROBE_REQ	0x40
#define PROBE_RESP	0x50

#define ACTION_FRM_IDX				0
#define PROBE_REQ_IDX				1
#define MAX_NUM_STA				9
#define ACTIVE_SCAN_TIME			10
#define PASSIVE_SCAN_TIME			1200
#define MIN_SCAN_TIME				10
#define MAX_SCAN_TIME				1200
#define DEFAULT_SCAN				0
#define USER_SCAN				BIT(0)
#define OBSS_PERIODIC_SCAN			BIT(1)
#define OBSS_ONETIME_SCAN			BIT(2)
#define GTK_RX_KEY_BUFF_LEN			24
#define ADDKEY					0x1
#define REMOVEKEY				0x2
#define DEFAULTKEY				0x4
#define ADDKEY_AP				0x8
#define MAX_NUM_SCANNED_NETWORKS		100
#define MAX_NUM_SCANNED_NETWORKS_SHADOW		130
#define MAX_NUM_PROBED_SSID			10
#define CHANNEL_SCAN_TIME			250

#define TX_MIC_KEY_LEN				8
#define RX_MIC_KEY_LEN				8
#define PTK_KEY_LEN				16

#define TX_MIC_KEY_MSG_LEN			26
#define RX_MIC_KEY_MSG_LEN			48
#define PTK_KEY_MSG_LEN				39

#define PMKSA_KEY_LEN				22
#define ETH_ALEN				6
#define PMKID_LEN				16
#define WILC_MAX_NUM_PMKIDS			16
#define WILC_ADD_STA_LENGTH			40
#define NUM_CONCURRENT_IFC			2
#define DRV_HANDLER_SIZE			5
#define DRV_HANDLER_MASK			0x000000FF

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
	u8 pmkid[PMKID_LEN];
};

struct host_if_pmkid_attr {
	u8 numpmkid;
	struct host_if_pmkid pmkidlist[WILC_MAX_NUM_PMKIDS];
};

enum CURRENT_TXRATE {
	AUTORATE	= 0,
	MBPS_1		= 1,
	MBPS_2		= 2,
	MBPS_5_5	= 5,
	MBPS_11		= 11,
	MBPS_6		= 6,
	MBPS_9		= 9,
	MBPS_12		= 12,
	MBPS_18		= 18,
	MBPS_24		= 24,
	MBPS_36		= 36,
	MBPS_48		= 48,
	MBPS_54		= 54
};

struct cfg_param_attr {
	u32 flag;
	u8 ht_enable;
	u8 bss_type;
	u8 auth_type;
	u16 auth_timeout;
	u8 power_mgmt_mode;
	u16 short_retry_limit;
	u16 long_retry_limit;
	u16 frag_threshold;
	u16 rts_threshold;
	u16 preamble_type;
	u8 short_slot_allowed;
	u8 txop_prot_disabled;
	u16 beacon_interval;
	u16 dtim_period;
	enum SITESURVEY site_survey_enabled;
	u16 site_survey_scan_time;
	u8 scan_source;
	u16 active_scan_time;
	u16 passive_scan_time;
	enum CURRENT_TXRATE curr_tx_rate;

};

enum cfg_param {
	RETRY_SHORT		= BIT(0),
	RETRY_LONG		= BIT(1),
	FRAG_THRESHOLD		= BIT(2),
	RTS_THRESHOLD		= BIT(3),
	BSS_TYPE		= BIT(4),
	AUTH_TYPE		= BIT(5),
	AUTHEN_TIMEOUT		= BIT(6),
	POWER_MANAGEMENT	= BIT(7),
	PREAMBLE		= BIT(8),
	SHORT_SLOT_ALLOWED	= BIT(9),
	TXOP_PROT_DISABLE	= BIT(10),
	BEACON_INTERVAL		= BIT(11),
	DTIM_PERIOD		= BIT(12),
	SITE_SURVEY		= BIT(13),
	SITE_SURVEY_SCAN_TIME	= BIT(14),
	ACTIVE_SCANTIME		= BIT(15),
	PASSIVE_SCANTIME	= BIT(16),
	CURRENT_TX_RATE		= BIT(17),
	HT_ENABLE		= BIT(18),
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
	WEP,
	WPA_RX_GTK,
	WPA_PTK,
	PMKSA,
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
	u32 rcvd_ch_cnt;
	struct found_net_info net_info[MAX_NUM_SCANNED_NETWORKS];
};

struct user_conn_req {
	u8 *bssid;
	u8 *ssid;
	u8 security;
	enum AUTHTYPE auth_type;
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

enum p2p_listen_state {
	P2P_IDLE,
	P2P_LISTEN,
	P2P_GRP_FORMATION
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
	struct cfg_param_attr cfg_values;

	struct mutex cfg_values_lock;
	struct completion comp_test_key_block;
	struct completion comp_test_disconn_block;
	struct completion comp_get_rssi;
	struct completion comp_inactive_time;

	struct timer_list scan_timer;
	struct wilc_vif *scan_timer_vif;

	struct timer_list connect_timer;
	struct wilc_vif *connect_timer_vif;

	struct timer_list remain_on_ch_timer;
	struct wilc_vif *remain_on_ch_timer_vif;

	bool IFC_UP;
	int driver_handler_id;
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
			    u8 index, u8 mode, enum AUTHTYPE auth_type);
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
		      u8 security, enum AUTHTYPE auth_type,
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
int wilc_setup_multicast_filter(struct wilc_vif *vif, bool enabled,
				u32 count);
int wilc_setup_ipaddress(struct wilc_vif *vif, u8 *ip_addr, u8 idx);
int wilc_remain_on_channel(struct wilc_vif *vif, u32 session_id,
			   u32 duration, u16 chan,
			   wilc_remain_on_chan_expired expired,
			   wilc_remain_on_chan_ready ready,
			   void *user_arg);
int wilc_listen_state_expired(struct wilc_vif *vif, u32 session_id);
int wilc_frame_register(struct wilc_vif *vif, u16 frame_type, bool reg);
int wilc_set_wfi_drv_handler(struct wilc_vif *vif, int index, u8 mode,
			     u8 ifc_id);
int wilc_set_operation_mode(struct wilc_vif *vif, u32 mode);
int wilc_get_statistics(struct wilc_vif *vif, struct rf_info *stats);
void wilc_resolve_disconnect_aberration(struct wilc_vif *vif);
int wilc_get_vif_idx(struct wilc_vif *vif);
int wilc_set_tx_power(struct wilc_vif *vif, u8 tx_power);
int wilc_get_tx_power(struct wilc_vif *vif, u8 *tx_power);

extern bool wilc_optaining_ip;
extern u8 wilc_connected_ssid[6];
extern u8 wilc_multicast_mac_addr_list[WILC_MULTICAST_TABLE_SIZE][ETH_ALEN];

extern int wilc_connecting;
extern struct timer_list wilc_during_ip_timer;

#endif
