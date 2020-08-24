/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef __IOCTL_CFG80211_H__
#define __IOCTL_CFG80211_H__

#define RTW_CFG80211_BLOCK_DISCON_WHEN_CONNECT		BIT0
#define RTW_CFG80211_BLOCK_DISCON_WHEN_DISCONNECT	BIT1

#ifndef RTW_CFG80211_BLOCK_STA_DISCON_EVENT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0))
#define RTW_CFG80211_BLOCK_STA_DISCON_EVENT (RTW_CFG80211_BLOCK_DISCON_WHEN_CONNECT)
#else
#define RTW_CFG80211_BLOCK_STA_DISCON_EVENT (RTW_CFG80211_BLOCK_DISCON_WHEN_CONNECT | RTW_CFG80211_BLOCK_DISCON_WHEN_DISCONNECT)
#endif
#endif

#if defined(RTW_USE_CFG80211_STA_EVENT)
	#undef CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER
#endif

#ifndef RTW_P2P_GROUP_INTERFACE
	#define RTW_P2P_GROUP_INTERFACE 0
#endif

/*
* (RTW_P2P_GROUP_INTERFACE, RTW_DEDICATED_P2P_DEVICE)
* (0, 0): wlan0 + p2p0(PD+PG)
* (1, 0): wlan0(with PD) + dynamic PGs
* (1, 1): wlan0 (with dynamic PD wdev) + dynamic PGs
*/

#if RTW_P2P_GROUP_INTERFACE
	#ifndef CONFIG_RTW_DYNAMIC_NDEV
		#define CONFIG_RTW_DYNAMIC_NDEV
	#endif
	#ifndef RTW_SINGLE_WIPHY
		#define RTW_SINGLE_WIPHY
	#endif
	#ifndef CONFIG_RADIO_WORK
		#define CONFIG_RADIO_WORK
	#endif
	#ifndef RTW_DEDICATED_P2P_DEVICE
		#define RTW_DEDICATED_P2P_DEVICE
	#endif
#endif

#ifndef CONFIG_RADIO_WORK
#define RTW_ROCH_DURATION_ENLARGE
#define RTW_ROCH_BACK_OP
#endif

#if !defined(CONFIG_P2P) && RTW_P2P_GROUP_INTERFACE
	#error "RTW_P2P_GROUP_INTERFACE can't be enabled when CONFIG_P2P is disabled\n"
#endif

#if !RTW_P2P_GROUP_INTERFACE && defined(RTW_DEDICATED_P2P_DEVICE)
	#error "RTW_DEDICATED_P2P_DEVICE can't be enabled when RTW_P2P_GROUP_INTERFACE is disabled\n"
#endif

#if defined(RTW_DEDICATED_P2P_DEVICE) && (LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0))
	#error "RTW_DEDICATED_P2P_DEVICE can't be enabled when kernel < 3.7.0\n"
#endif

#ifdef CONFIG_RTW_MESH
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0))
		#error "CONFIG_RTW_MESH can't be enabled when kernel < 3.10.0\n"
	#endif
#endif

struct rtw_wdev_invit_info {
	u8 state; /* 0: req, 1:rep */
	u8 peer_mac[ETH_ALEN];
	u8 group_bssid[ETH_ALEN];
	u8 active;
	u8 token;
	u8 flags;
	u8 status;
	u8 req_op_ch;
	u8 rsp_op_ch;
};

#define rtw_wdev_invit_info_init(invit_info) \
	do { \
		(invit_info)->state = 0xff; \
		_rtw_memset((invit_info)->peer_mac, 0, ETH_ALEN); \
		_rtw_memset((invit_info)->group_bssid, 0, ETH_ALEN); \
		(invit_info)->active = 0xff; \
		(invit_info)->token = 0; \
		(invit_info)->flags = 0x00; \
		(invit_info)->status = 0xff; \
		(invit_info)->req_op_ch = 0; \
		(invit_info)->rsp_op_ch = 0; \
	} while (0)

struct rtw_wdev_nego_info {
	u8 state; /* 0: req, 1:rep, 2:conf */
	u8 iface_addr[ETH_ALEN];
	u8 peer_mac[ETH_ALEN];
	u8 peer_iface_addr[ETH_ALEN];
	u8 active;
	u8 token;
	u8 status;
	u8 req_intent;
	u8 req_op_ch;
	u8 req_listen_ch;
	u8 rsp_intent;
	u8 rsp_op_ch;
	u8 conf_op_ch;
};

#define rtw_wdev_nego_info_init(nego_info) \
	do { \
		(nego_info)->state = 0xff; \
		_rtw_memset((nego_info)->iface_addr, 0, ETH_ALEN); \
		_rtw_memset((nego_info)->peer_mac, 0, ETH_ALEN); \
		_rtw_memset((nego_info)->peer_iface_addr, 0, ETH_ALEN); \
		(nego_info)->active = 0xff; \
		(nego_info)->token = 0; \
		(nego_info)->status = 0xff; \
		(nego_info)->req_intent = 0xff; \
		(nego_info)->req_op_ch = 0; \
		(nego_info)->req_listen_ch = 0; \
		(nego_info)->rsp_intent = 0xff; \
		(nego_info)->rsp_op_ch = 0; \
		(nego_info)->conf_op_ch = 0; \
	} while (0)

struct rtw_wdev_priv {
	struct wireless_dev *rtw_wdev;

	_adapter *padapter;

	#if RTW_CFG80211_BLOCK_STA_DISCON_EVENT
	u8 not_indic_disco;
	#endif

	struct cfg80211_scan_request *scan_request;
	_lock scan_req_lock;

	struct cfg80211_connect_params *connect_req;
	_lock connect_req_lock;

	struct net_device *pmon_ndev;/* for monitor interface */
	char ifname_mon[IFNAMSIZ + 1]; /* interface name for monitor interface */

	u8 p2p_enabled;
	systime probe_resp_ie_update_time;

	u8 provdisc_req_issued;

	struct rtw_wdev_invit_info invit_info;
	struct rtw_wdev_nego_info nego_info;

	u8 bandroid_scan;
	bool block;
	bool block_scan;
	bool power_mgmt;

	/* report mgmt_frame registered */
	u16 report_mgmt;

	u8 is_mgmt_tx;
	u16 mgmt_tx_cookie;

	_mutex roch_mutex;

#ifdef CONFIG_CONCURRENT_MODE
	ATOMIC_T switch_ch_to;
#endif

#ifdef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
	u8 pno_mac_addr[ETH_ALEN];
	u16 pno_scan_seq_num;
#endif

#ifdef CONFIG_RTW_CFGVENDOR_RSSIMONITOR
        s8 rssi_monitor_max;
        s8 rssi_monitor_min;
        u8 rssi_monitor_enable;
#endif

};

enum external_auth_action {
	EXTERNAL_AUTH_START,
	EXTERNAL_AUTH_ABORT,
};

struct rtw_external_auth_params {
	enum external_auth_action action;
	u8 bssid[ETH_ALEN]__aligned(2);
	struct cfg80211_ssid ssid;
	unsigned int key_mgmt_suite;
	u16 status;
	u8 pmkid[PMKID_LEN];
};

bool rtw_cfg80211_is_connect_requested(_adapter *adapter);

#if RTW_CFG80211_BLOCK_STA_DISCON_EVENT
#define rtw_wdev_not_indic_disco(rtw_wdev_data) ((rtw_wdev_data)->not_indic_disco)
#define rtw_wdev_set_not_indic_disco(rtw_wdev_data, val) do { (rtw_wdev_data)->not_indic_disco = (val); } while (0)
#else
#define rtw_wdev_not_indic_disco(rtw_wdev_data) 0
#define rtw_wdev_set_not_indic_disco(rtw_wdev_data, val) do {} while (0)
#endif

#define rtw_wdev_free_connect_req(rtw_wdev_data) \
	do { \
		if ((rtw_wdev_data)->connect_req) { \
			rtw_mfree((u8 *)(rtw_wdev_data)->connect_req, sizeof(*(rtw_wdev_data)->connect_req)); \
			(rtw_wdev_data)->connect_req = NULL; \
		} \
	} while (0)

#define wdev_to_ndev(w) ((w)->netdev)
#define wdev_to_wiphy(w) ((w)->wiphy)
#define ndev_to_wdev(n) ((n)->ieee80211_ptr)

struct rtw_wiphy_data {
	struct dvobj_priv *dvobj;

#ifndef RTW_SINGLE_WIPHY
	_adapter *adapter;
#endif

#if defined(RTW_DEDICATED_P2P_DEVICE)
	struct wireless_dev *pd_wdev; /* P2P device wdev */
#endif

	s16 txpwr_total_lmt_mbm;
	s16 txpwr_total_target_mbm;
};

#define rtw_wiphy_priv(wiphy) ((struct rtw_wiphy_data *)wiphy_priv(wiphy))
#define wiphy_to_dvobj(wiphy) (((struct rtw_wiphy_data *)wiphy_priv(wiphy))->dvobj)
#ifdef RTW_SINGLE_WIPHY
#define wiphy_to_adapter(wiphy) (dvobj_get_primary_adapter(wiphy_to_dvobj(wiphy)))
#else
#define wiphy_to_adapter(wiphy) (((struct rtw_wiphy_data *)wiphy_priv(wiphy))->adapter)
#endif

#if defined(RTW_DEDICATED_P2P_DEVICE)
#define wiphy_to_pd_wdev(wiphy) (rtw_wiphy_priv(wiphy)->pd_wdev)
#else
#define wiphy_to_pd_wdev(wiphy) NULL
#endif

#define WIPHY_FMT "%s"
#define WIPHY_ARG(wiphy) wiphy_name(wiphy)
#define FUNC_WIPHY_FMT "%s("WIPHY_FMT")"
#define FUNC_WIPHY_ARG(wiphy) __func__, WIPHY_ARG(wiphy)

#define SET_CFG80211_REPORT_MGMT(w, t, v) (w->report_mgmt |= BIT(t >> 4))
#define CLR_CFG80211_REPORT_MGMT(w, t, v) (w->report_mgmt &= (~BIT(t >> 4)))
#define GET_CFG80211_REPORT_MGMT(w, t) ((w->report_mgmt & BIT(t >> 4)) > 0)

struct wiphy *rtw_wiphy_alloc(_adapter *padapter, struct device *dev);
void rtw_wiphy_free(struct wiphy *wiphy);
int rtw_wiphy_register(struct wiphy *wiphy);
void rtw_wiphy_unregister(struct wiphy *wiphy);

int rtw_wdev_alloc(_adapter *padapter, struct wiphy *wiphy);
void rtw_wdev_free(struct wireless_dev *wdev);
void rtw_wdev_unregister(struct wireless_dev *wdev);

int rtw_cfg80211_ndev_res_alloc(_adapter *adapter);
void rtw_cfg80211_ndev_res_free(_adapter *adapter);
int rtw_cfg80211_ndev_res_register(_adapter *adapter);
void rtw_cfg80211_ndev_res_unregister(_adapter *adapter);

int rtw_cfg80211_dev_res_alloc(struct dvobj_priv *dvobj);
void rtw_cfg80211_dev_res_free(struct dvobj_priv *dvobj);
int rtw_cfg80211_dev_res_register(struct dvobj_priv *dvobj);
void rtw_cfg80211_dev_res_unregister(struct dvobj_priv *dvobj);
s16 rtw_cfg80211_dev_get_total_txpwr_lmt_mbm(struct dvobj_priv *dvobj);
s16 rtw_cfg80211_dev_get_total_txpwr_target_mbm(struct dvobj_priv *dvobj);

void rtw_cfg80211_init_wdev_data(_adapter *padapter);
void rtw_cfg80211_init_wiphy(_adapter *padapter);

void rtw_cfg80211_unlink_bss(_adapter *padapter, struct wlan_network *pnetwork);
void rtw_cfg80211_surveydone_event_callback(_adapter *padapter);
struct cfg80211_bss *rtw_cfg80211_inform_bss(_adapter *padapter, struct wlan_network *pnetwork);
int rtw_cfg80211_check_bss(_adapter *padapter);
void rtw_cfg80211_ibss_indicate_connect(_adapter *padapter);
void rtw_cfg80211_indicate_connect(_adapter *padapter);
void rtw_cfg80211_indicate_disconnect(_adapter *padapter, u16 reason, u8 locally_generated);
void rtw_cfg80211_indicate_scan_done(_adapter *adapter, bool aborted);
u32 rtw_cfg80211_wait_scan_req_empty(_adapter *adapter, u32 timeout_ms);

#ifdef CONFIG_CONCURRENT_MODE
u8 rtw_cfg80211_scan_via_buddy(_adapter *padapter, struct cfg80211_scan_request *request);
void rtw_cfg80211_indicate_scan_done_for_buddy(_adapter *padapter, bool bscan_aborted);
#endif

#ifdef CONFIG_AP_MODE
void rtw_cfg80211_indicate_sta_assoc(_adapter *padapter, u8 *pmgmt_frame, uint frame_len);
void rtw_cfg80211_indicate_sta_disassoc(_adapter *padapter, const u8 *da, unsigned short reason);
#endif /* CONFIG_AP_MODE */

#ifdef CONFIG_P2P
void rtw_cfg80211_set_is_roch(_adapter *adapter, bool val);
bool rtw_cfg80211_get_is_roch(_adapter *adapter);
bool rtw_cfg80211_is_ro_ch_once(_adapter *adapter);
void rtw_cfg80211_set_last_ro_ch_time(_adapter *adapter);
s32 rtw_cfg80211_get_last_ro_ch_passing_ms(_adapter *adapter);

int rtw_cfg80211_iface_has_p2p_group_cap(_adapter *adapter);
int rtw_cfg80211_is_p2p_scan(_adapter *adapter);
#if defined(RTW_DEDICATED_P2P_DEVICE)
int rtw_cfg80211_redirect_pd_wdev(struct wiphy *wiphy, u8 *ra, struct wireless_dev **wdev);
int rtw_cfg80211_is_scan_by_pd_wdev(_adapter *adapter);
int rtw_pd_iface_alloc(struct wiphy *wiphy, const char *name, struct wireless_dev **pd_wdev);
void rtw_pd_iface_free(struct wiphy *wiphy);
#endif
#endif /* CONFIG_P2P */

void rtw_cfg80211_set_is_mgmt_tx(_adapter *adapter, u8 val);
u8 rtw_cfg80211_get_is_mgmt_tx(_adapter *adapter);
u8 rtw_mgnt_tx_handler(_adapter *adapter, u8 *buf);

void rtw_cfg80211_issue_p2p_provision_request(_adapter *padapter, const u8 *buf, size_t len);

void rtw_cfg80211_rx_p2p_action_public(_adapter *padapter, union recv_frame *rframe);
void rtw_cfg80211_rx_action_p2p(_adapter *padapter, union recv_frame *rframe);
void rtw_cfg80211_rx_action(_adapter *adapter, union recv_frame *rframe, const char *msg);
void rtw_cfg80211_rx_mframe(_adapter *adapter, union recv_frame *rframe, const char *msg);
void rtw_cfg80211_rx_probe_request(_adapter *padapter, union recv_frame *rframe);

void rtw_cfg80211_external_auth_request(_adapter *padapter, union recv_frame *rframe);
void rtw_cfg80211_external_auth_status(struct wiphy *wiphy, struct net_device *dev,
	struct rtw_external_auth_params *params);

int rtw_cfg80211_set_mgnt_wpsp2pie(struct net_device *net, char *buf, int len, int type);

bool rtw_cfg80211_pwr_mgmt(_adapter *adapter);
#ifdef CONFIG_RTW_80211K
void rtw_cfg80211_rx_rrm_action(_adapter *adapter, union recv_frame *rframe);
#endif

#ifdef CONFIG_RFKILL_POLL
void rtw_cfg80211_init_rfkill(struct wiphy *wiphy);
void rtw_cfg80211_deinit_rfkill(struct wiphy *wiphy);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0))  && !defined(COMPAT_KERNEL_RELEASE)
#define rtw_cfg80211_rx_mgmt(wdev, freq, sig_dbm, buf, len, gfp) cfg80211_rx_mgmt(wdev_to_ndev(wdev), freq, buf, len, gfp)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
#define rtw_cfg80211_rx_mgmt(wdev, freq, sig_dbm, buf, len, gfp) cfg80211_rx_mgmt(wdev_to_ndev(wdev), freq, sig_dbm, buf, len, gfp)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
#define rtw_cfg80211_rx_mgmt(wdev, freq, sig_dbm, buf, len, gfp) cfg80211_rx_mgmt(wdev, freq, sig_dbm, buf, len, gfp)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3 , 18 , 0))
#define rtw_cfg80211_rx_mgmt(wdev , freq , sig_dbm , buf , len , gfp) cfg80211_rx_mgmt(wdev , freq , sig_dbm , buf , len , 0 , gfp)
#else
#define rtw_cfg80211_rx_mgmt(wdev , freq , sig_dbm , buf , len , gfp) cfg80211_rx_mgmt(wdev , freq , sig_dbm , buf , len , 0)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0))  && !defined(COMPAT_KERNEL_RELEASE)
#define rtw_cfg80211_send_rx_assoc(adapter, bss, buf, len) cfg80211_send_rx_assoc((adapter)->pnetdev, buf, len)
#else
#define rtw_cfg80211_send_rx_assoc(adapter, bss, buf, len) cfg80211_send_rx_assoc((adapter)->pnetdev, bss, buf, len)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
#define rtw_cfg80211_mgmt_tx_status(wdev, cookie, buf, len, ack, gfp) cfg80211_mgmt_tx_status(wdev_to_ndev(wdev), cookie, buf, len, ack, gfp)
#else
#define rtw_cfg80211_mgmt_tx_status(wdev, cookie, buf, len, ack, gfp) cfg80211_mgmt_tx_status(wdev, cookie, buf, len, ack, gfp)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
#define rtw_cfg80211_ready_on_channel(wdev, cookie, chan, channel_type, duration, gfp)  cfg80211_ready_on_channel(wdev_to_ndev(wdev), cookie, chan, channel_type, duration, gfp)
#define rtw_cfg80211_remain_on_channel_expired(wdev, cookie, chan, chan_type, gfp) cfg80211_remain_on_channel_expired(wdev_to_ndev(wdev), cookie, chan, chan_type, gfp)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
#define rtw_cfg80211_ready_on_channel(wdev, cookie, chan, channel_type, duration, gfp)  cfg80211_ready_on_channel(wdev, cookie, chan, channel_type, duration, gfp)
#define rtw_cfg80211_remain_on_channel_expired(wdev, cookie, chan, chan_type, gfp) cfg80211_remain_on_channel_expired(wdev, cookie, chan, chan_type, gfp)
#else
#define rtw_cfg80211_ready_on_channel(wdev, cookie, chan, channel_type, duration, gfp)  cfg80211_ready_on_channel(wdev, cookie, chan, duration, gfp)
#define rtw_cfg80211_remain_on_channel_expired(wdev, cookie, chan, chan_type, gfp) cfg80211_remain_on_channel_expired(wdev, cookie, chan, gfp)
#endif

#define rtw_cfg80211_connect_result(wdev, bssid, req_ie, req_ie_len, resp_ie, resp_ie_len, status, gfp) cfg80211_connect_result(wdev_to_ndev(wdev), bssid, req_ie, req_ie_len, resp_ie, resp_ie_len, status, gfp)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0))
#define rtw_cfg80211_disconnected(wdev, reason, ie, ie_len, locally_generated, gfp) cfg80211_disconnected(wdev_to_ndev(wdev), reason, ie, ie_len, gfp)
#else
#define rtw_cfg80211_disconnected(wdev, reason, ie, ie_len, locally_generated, gfp) cfg80211_disconnected(wdev_to_ndev(wdev), reason, ie, ie_len, locally_generated, gfp)
#endif

#ifdef CONFIG_RTW_80211R
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
#define rtw_cfg80211_ft_event(adapter, parm)  cfg80211_ft_event((adapter)->pnetdev, parm)
#else
	#error "Cannot support FT for KERNEL_VERSION < 3.10\n"
#endif
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
#define rtw_cfg80211_notify_new_peer_candidate(wdev, addr, ie, ie_len, sig_dbm, gfp) cfg80211_notify_new_peer_candidate(wdev_to_ndev(wdev), addr, ie, ie_len, sig_dbm, gfp)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
#define rtw_cfg80211_notify_new_peer_candidate(wdev, addr, ie, ie_len, sig_dbm, gfp) cfg80211_notify_new_peer_candidate(wdev_to_ndev(wdev), addr, ie, ie_len, gfp)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
u8 rtw_cfg80211_ch_switch_notify(_adapter *adapter, u8 ch, u8 bw, u8 offset, u8 ht, bool started);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0))
#define NL80211_BAND_2GHZ IEEE80211_BAND_2GHZ
#define NL80211_BAND_5GHZ IEEE80211_BAND_5GHZ
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
#define NL80211_BAND_60GHZ IEEE80211_BAND_60GHZ
#endif
#define NUM_NL80211_BANDS IEEE80211_NUM_BANDS
#endif

#define rtw_band_to_nl80211_band(band) \
	(band == BAND_ON_2_4G) ? NL80211_BAND_2GHZ : \
	(band == BAND_ON_5G) ? NL80211_BAND_5GHZ : NUM_NL80211_BANDS

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36))
#define NL80211_TX_POWER_AUTOMATIC	TX_POWER_AUTOMATIC
#define NL80211_TX_POWER_LIMITED	TX_POWER_LIMITED
#define NL80211_TX_POWER_FIXED		TX_POWER_FIXED
#endif

#include "wifi_regd.h"
#include "rtw_cfgvendor.h"

#endif /* __IOCTL_CFG80211_H__ */
