/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __IOCTL_CFG80211_H__
#define __IOCTL_CFG80211_H__ 


#if defined(RTW_USE_CFG80211_STA_EVENT)
	#undef CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER
#endif

struct rtw_wdev_invit_info {
	u8 state; /* 0: req, 1:rep */
	u8 peer_mac[ETH_ALEN];
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
		(invit_info)->active = 0xff; \
		(invit_info)->token = 0; \
		(invit_info)->flags = 0x00; \
		(invit_info)->status = 0xff; \
		(invit_info)->req_op_ch = 0; \
		(invit_info)->rsp_op_ch = 0; \
	} while (0)

struct rtw_wdev_nego_info {
	u8 state; /* 0: req, 1:rep, 2:conf */
	u8 peer_mac[ETH_ALEN];
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
		_rtw_memset((nego_info)->peer_mac, 0, ETH_ALEN); \
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

struct rtw_wdev_priv
{	
	struct wireless_dev *rtw_wdev;
	
	_adapter *padapter;

	struct cfg80211_scan_request *scan_request;
	_lock scan_req_lock;

	struct net_device *pmon_ndev;//for monitor interface
	char ifname_mon[IFNAMSIZ + 1]; //interface name for monitor interface

	u8 p2p_enabled;

	u8 provdisc_req_issued;

	struct rtw_wdev_invit_info invit_info;
	struct rtw_wdev_nego_info nego_info;

	u8 bandroid_scan;
	bool block;
	bool block_scan;
	bool power_mgmt;

	/* report mgmt_frame registered */
	u16 report_mgmt;

#ifdef CONFIG_CONCURRENT_MODE
	ATOMIC_T ro_ch_to;
	ATOMIC_T switch_ch_to;	
#endif	
	
};

#define wiphy_to_adapter(x) (*((_adapter**)wiphy_priv(x)))

#define wdev_to_ndev(w) ((w)->netdev)
#define wdev_to_wiphy(w) ((w)->wiphy)
#define ndev_to_wdev(n) ((n)->ieee80211_ptr)

#define WIPHY_FMT "%s"
#define WIPHY_ARG(wiphy) wiphy_name(wiphy)
#define FUNC_WIPHY_FMT "%s("WIPHY_FMT")"
#define FUNC_WIPHY_ARG(wiphy) __func__, WIPHY_ARG(wiphy)

#define SET_CFG80211_REPORT_MGMT(w, t, v) (w->report_mgmt |= (v?BIT(t >> 4):0))
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

void rtw_cfg80211_init_wiphy(_adapter *padapter);

void rtw_cfg80211_unlink_bss(_adapter *padapter, struct wlan_network *pnetwork);
void rtw_cfg80211_surveydone_event_callback(_adapter *padapter);
struct cfg80211_bss *rtw_cfg80211_inform_bss(_adapter *padapter, struct wlan_network *pnetwork);
int rtw_cfg80211_check_bss(_adapter *padapter);
void rtw_cfg80211_ibss_indicate_connect(_adapter *padapter);
void rtw_cfg80211_indicate_connect(_adapter *padapter);
void rtw_cfg80211_indicate_disconnect(_adapter *padapter);
void rtw_cfg80211_indicate_scan_done(_adapter *adapter, bool aborted);

#ifdef CONFIG_AP_MODE
void rtw_cfg80211_indicate_sta_assoc(_adapter *padapter, u8 *pmgmt_frame, uint frame_len);
void rtw_cfg80211_indicate_sta_disassoc(_adapter *padapter, unsigned char *da, unsigned short reason);
#endif //CONFIG_AP_MODE

void rtw_cfg80211_issue_p2p_provision_request(_adapter *padapter, const u8 *buf, size_t len);
void rtw_cfg80211_rx_p2p_action_public(_adapter *padapter, u8 *pmgmt_frame, uint frame_len);
void rtw_cfg80211_rx_action_p2p(_adapter *padapter, u8 *pmgmt_frame, uint frame_len);
void rtw_cfg80211_rx_action(_adapter *adapter, u8 *frame, uint frame_len, const char*msg);
void rtw_cfg80211_rx_probe_request(_adapter *padapter, u8 *pmgmt_frame, uint frame_len);

int rtw_cfg80211_set_mgnt_wpsp2pie(struct net_device *net, char *buf, int len, int type);

bool rtw_cfg80211_pwr_mgmt(_adapter *adapter);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))  && !defined(COMPAT_KERNEL_RELEASE)
#define rtw_cfg80211_rx_mgmt(adapter, freq, sig_dbm, buf, len, gfp) cfg80211_rx_mgmt((adapter)->pnetdev, freq, buf, len, gfp)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
#define rtw_cfg80211_rx_mgmt(adapter, freq, sig_dbm, buf, len, gfp) cfg80211_rx_mgmt((adapter)->pnetdev, freq, sig_dbm, buf, len, gfp)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,12,0))
#define rtw_cfg80211_rx_mgmt(adapter, freq, sig_dbm, buf, len, gfp) cfg80211_rx_mgmt((adapter)->rtw_wdev, freq, sig_dbm, buf, len, gfp)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3 , 18 , 0))
#define rtw_cfg80211_rx_mgmt(adapter , freq , sig_dbm , buf , len , gfp) cfg80211_rx_mgmt((adapter)->rtw_wdev , freq , sig_dbm , buf , len , 0 , gfp)
#else
#define rtw_cfg80211_rx_mgmt(adapter , freq , sig_dbm , buf , len , gfp) cfg80211_rx_mgmt((adapter)->rtw_wdev , freq , sig_dbm , buf , len , 0)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))  && !defined(COMPAT_KERNEL_RELEASE)
#define rtw_cfg80211_send_rx_assoc(adapter, bss, buf, len) cfg80211_send_rx_assoc((adapter)->pnetdev, buf, len)
#else
#define rtw_cfg80211_send_rx_assoc(adapter, bss, buf, len) cfg80211_send_rx_assoc((adapter)->pnetdev, bss, buf, len)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
#define rtw_cfg80211_mgmt_tx_status(adapter, cookie, buf, len, ack, gfp) cfg80211_mgmt_tx_status((adapter)->pnetdev, cookie, buf, len, ack, gfp)
#else
#define rtw_cfg80211_mgmt_tx_status(adapter, cookie, buf, len, ack, gfp) cfg80211_mgmt_tx_status((adapter)->rtw_wdev, cookie, buf, len, ack, gfp)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
#define rtw_cfg80211_ready_on_channel(adapter, cookie, chan, channel_type, duration, gfp)  cfg80211_ready_on_channel((adapter)->pnetdev, cookie, chan, channel_type, duration, gfp)
#define rtw_cfg80211_remain_on_channel_expired(adapter, cookie, chan, chan_type, gfp) cfg80211_remain_on_channel_expired((adapter)->pnetdev, cookie, chan, chan_type, gfp)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0))
#define rtw_cfg80211_ready_on_channel(adapter, cookie, chan, channel_type, duration, gfp)  cfg80211_ready_on_channel((adapter)->rtw_wdev, cookie, chan, channel_type, duration, gfp)
#define rtw_cfg80211_remain_on_channel_expired(adapter, cookie, chan, chan_type, gfp) cfg80211_remain_on_channel_expired((adapter)->rtw_wdev, cookie, chan, chan_type, gfp)
#else
#define rtw_cfg80211_ready_on_channel(adapter, cookie, chan, channel_type, duration, gfp)  cfg80211_ready_on_channel((adapter)->rtw_wdev, cookie, chan, duration, gfp)
#define rtw_cfg80211_remain_on_channel_expired(adapter, cookie, chan, chan_type, gfp) cfg80211_remain_on_channel_expired((adapter)->rtw_wdev, cookie, chan, gfp)
#endif

#include "rtw_cfgvendor.h"

#endif //__IOCTL_CFG80211_H__

