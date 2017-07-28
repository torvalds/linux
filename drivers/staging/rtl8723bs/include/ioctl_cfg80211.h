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
 ******************************************************************************/
#ifndef __IOCTL_CFG80211_H__
#define __IOCTL_CFG80211_H__

#include <linux/version.h>

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
		memset((invit_info)->peer_mac, 0, ETH_ALEN); \
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
		memset((nego_info)->peer_mac, 0, ETH_ALEN); \
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

	struct adapter *padapter;

	struct cfg80211_scan_request *scan_request;
	_lock scan_req_lock;

	struct net_device *pmon_ndev;/* for monitor interface */
	char ifname_mon[IFNAMSIZ + 1]; /* interface name for monitor interface */

	u8 p2p_enabled;

	u8 provdisc_req_issued;

	struct rtw_wdev_invit_info invit_info;
	struct rtw_wdev_nego_info nego_info;

	u8 bandroid_scan;
	bool block;
	bool power_mgmt;
};

#define wiphy_to_adapter(x) (*((struct adapter **)wiphy_priv(x)))

#define wdev_to_ndev(w) ((w)->netdev)

int rtw_wdev_alloc(struct adapter *padapter, struct device *dev);
void rtw_wdev_free(struct wireless_dev *wdev);
void rtw_wdev_unregister(struct wireless_dev *wdev);

void rtw_cfg80211_init_wiphy(struct adapter *padapter);

void rtw_cfg80211_unlink_bss(struct adapter *padapter, struct wlan_network *pnetwork);
void rtw_cfg80211_surveydone_event_callback(struct adapter *padapter);
struct cfg80211_bss *rtw_cfg80211_inform_bss(struct adapter *padapter, struct wlan_network *pnetwork);
int rtw_cfg80211_check_bss(struct adapter *padapter);
void rtw_cfg80211_ibss_indicate_connect(struct adapter *padapter);
void rtw_cfg80211_indicate_connect(struct adapter *padapter);
void rtw_cfg80211_indicate_disconnect(struct adapter *padapter);
void rtw_cfg80211_indicate_scan_done(struct adapter *adapter, bool aborted);

void rtw_cfg80211_indicate_sta_assoc(struct adapter *padapter, u8 *pmgmt_frame, uint frame_len);
void rtw_cfg80211_indicate_sta_disassoc(struct adapter *padapter, unsigned char *da, unsigned short reason);

void rtw_cfg80211_rx_action(struct adapter *adapter, u8 *frame, uint frame_len, const char*msg);

bool rtw_cfg80211_pwr_mgmt(struct adapter *adapter);

#define rtw_cfg80211_rx_mgmt(adapter, freq, sig_dbm, buf, len, gfp) cfg80211_rx_mgmt((adapter)->rtw_wdev, freq, sig_dbm, buf, len, 0)
#define rtw_cfg80211_send_rx_assoc(adapter, bss, buf, len) cfg80211_send_rx_assoc((adapter)->pnetdev, bss, buf, len)
#define rtw_cfg80211_mgmt_tx_status(adapter, cookie, buf, len, ack, gfp) cfg80211_mgmt_tx_status((adapter)->rtw_wdev, cookie, buf, len, ack, gfp)
#define rtw_cfg80211_ready_on_channel(adapter, cookie, chan, channel_type, duration, gfp)  cfg80211_ready_on_channel((adapter)->rtw_wdev, cookie, chan, duration, gfp)
#define rtw_cfg80211_remain_on_channel_expired(adapter, cookie, chan, chan_type, gfp) cfg80211_remain_on_channel_expired((adapter)->rtw_wdev, cookie, chan, gfp)

#endif /* __IOCTL_CFG80211_H__ */
