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

#if defined(CONFIG_IOCTL_CFG80211) && !defined(CONFIG_CFG80211) && !defined(CONFIG_CFG80211_MODULE)
	#error "Can't define CONFIG_IOCTL_CFG80211 because neither CONFIG_CFG80211 nor CONFIG_CFG80211_MODULE is defined in kernel"
#endif
#if defined(CONFIG_IOCTL_CFG80211) && LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
	#error "We haven't verify our cfg80211 solution below kernel version 2.6.35"
#endif

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
	u8 state; /* 0: req, 1:rep, 3:conf */
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
	bool power_mgmt;

#ifdef CONFIG_CONCURRENT_MODE
	ATOMIC_T ro_ch_to;
	ATOMIC_T switch_ch_to;	
#endif	
	
};

#define wdev_to_priv(w) ((struct rtw_wdev_priv *)(wdev_priv(w)))

#define wiphy_to_adapter(x) (_adapter *)(((struct rtw_wdev_priv*)wiphy_priv(x))->padapter)

#define wiphy_to_wdev(x) (struct wireless_dev *)(((struct rtw_wdev_priv*)wiphy_priv(x))->rtw_wdev)

int rtw_wdev_alloc(_adapter *padapter, struct device *dev);
void rtw_wdev_free(struct wireless_dev *wdev);
void rtw_wdev_unregister(struct wireless_dev *wdev);

void rtw_cfg80211_init_wiphy(_adapter *padapter);

void rtw_cfg80211_surveydone_event_callback(_adapter *padapter);
struct cfg80211_bss *rtw_cfg80211_inform_bss(_adapter *padapter, struct wlan_network *pnetwork);
int rtw_cfg80211_check_bss(_adapter *padapter);
void rtw_cfg80211_indicate_connect(_adapter *padapter);
void rtw_cfg80211_indicate_disconnect(_adapter *padapter);
void rtw_cfg80211_indicate_scan_done(struct rtw_wdev_priv *pwdev_priv, bool aborted);

#ifdef CONFIG_AP_MODE
void rtw_cfg80211_indicate_sta_assoc(_adapter *padapter, u8 *pmgmt_frame, uint frame_len);
void rtw_cfg80211_indicate_sta_disassoc(_adapter *padapter, unsigned char *da, unsigned short reason);
#endif //CONFIG_AP_MODE

void rtw_cfg80211_issue_p2p_provision_request(_adapter *padapter, const u8 *buf, size_t len);
void rtw_cfg80211_rx_p2p_action_public(_adapter *padapter, u8 *pmgmt_frame, uint frame_len);
void rtw_cfg80211_rx_action_p2p(_adapter *padapter, u8 *pmgmt_frame, uint frame_len);
void rtw_cfg80211_rx_action(_adapter *adapter, u8 *frame, uint frame_len, const char*msg);

int rtw_cfg80211_set_mgnt_wpsp2pie(struct net_device *net, char *buf, int len, int type);

bool rtw_cfg80211_pwr_mgmt(_adapter *adapter);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))  && !defined(COMPAT_KERNEL_RELEASE)
#define rtw_cfg80211_rx_mgmt(adapter, freq, sig_dbm, buf, len, gfp) cfg80211_rx_mgmt((adapter)->pnetdev, freq, buf, len, gfp)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
#define rtw_cfg80211_rx_mgmt(adapter, freq, sig_dbm, buf, len, gfp) cfg80211_rx_mgmt((adapter)->pnetdev, freq, sig_dbm, buf, len, gfp)
#else
#define rtw_cfg80211_rx_mgmt(adapter, freq, sig_dbm, buf, len, gfp) cfg80211_rx_mgmt((adapter)->rtw_wdev, freq, sig_dbm, buf, len, gfp)
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

#endif //__IOCTL_CFG80211_H__

