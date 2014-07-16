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

struct rtw_wdev_priv {
	struct wireless_dev *rtw_wdev;

	struct rtw_adapter *padapter;

	struct cfg80211_scan_request *scan_request;
	spinlock_t scan_req_lock;

	struct net_device *pmon_ndev;/* for monitor interface */
	char ifname_mon[IFNAMSIZ + 1]; /* name for monitor interface */

	u8 p2p_enabled;

	bool power_mgmt;
};

#define wdev_to_priv(w) ((struct rtw_wdev_priv *)(wdev_priv(w)))

#define wiphy_to_adapter(x)					\
	(struct rtw_adapter *)(((struct rtw_wdev_priv *)	\
	wiphy_priv(x))->padapter)

#define wiphy_to_wdev(x)					\
	(struct wireless_dev *)(((struct rtw_wdev_priv *)	\
	wiphy_priv(x))->rtw_wdev)

int rtw_wdev_alloc(struct rtw_adapter *padapter, struct device *dev);
void rtw_wdev_free(struct wireless_dev *wdev);
void rtw_wdev_unregister(struct wireless_dev *wdev);

void rtw_cfg80211_init_wiphy(struct rtw_adapter *padapter);

void rtw_cfg80211_surveydone_event_callback(struct rtw_adapter *padapter);

void rtw_cfg80211_indicate_connect(struct rtw_adapter *padapter);
void rtw_cfg80211_indicate_disconnect(struct rtw_adapter *padapter);
void rtw_cfg80211_indicate_scan_done(struct rtw_wdev_priv *pwdev_priv,
				     bool aborted);

#ifdef CONFIG_8723AU_AP_MODE
void rtw_cfg80211_indicate_sta_assoc(struct rtw_adapter *padapter,
				     u8 *pmgmt_frame, uint frame_len);
void rtw_cfg80211_indicate_sta_disassoc(struct rtw_adapter *padapter,
					unsigned char *da, unsigned short reason);
#endif /* CONFIG_8723AU_AP_MODE */

void rtw_cfg80211_rx_action(struct rtw_adapter *adapter, u8 *frame,
			    uint frame_len, const char*msg);

bool rtw_cfg80211_pwr_mgmt(struct rtw_adapter *adapter);

#endif /* __IOCTL_CFG80211_H__ */
