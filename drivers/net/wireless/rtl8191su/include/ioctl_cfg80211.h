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


struct rtw_wdev_priv
{	
	struct wireless_dev *rtw_wdev;
	
	_adapter *padapter;

	struct cfg80211_scan_request *scan_request;
	_lock scan_req_lock;

	struct net_device *pmon_ndev;//for monitor interface
	char ifname_mon[IFNAMSIZ + 1]; //interface name for monitor interface

};

#define wdev_to_priv(w) (struct rtw_wdev_priv *)(wdev_priv(w))

#define wiphy_to_adapter(x) (_adapter *)(((struct rtw_wdev_priv*)wiphy_priv(x))->padapter)

#define wiphy_to_wdev(x) (struct wireless_dev *)(((struct rtw_wdev_priv*)wiphy_priv(x))->rtw_wdev)



int rtw_wdev_alloc(_adapter *padapter, struct device *dev);
void rtw_wdev_free(struct wireless_dev *wdev);

void rtw_cfg80211_init_wiphy(_adapter *padapter);

void rtw_cfg80211_surveydone_event_callback(_adapter *padapter);

void rtw_cfg80211_indicate_connect(_adapter *padapter);
void rtw_cfg80211_indicate_disconnect(_adapter *padapter);
void rtw_cfg80211_indicate_scan_done(struct rtw_wdev_priv *pwdev_priv, bool aborted);

int rtw_cfg80211_do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

#endif //__IOCTL_CFG80211_H__

