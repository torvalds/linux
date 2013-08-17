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

	bool block;

};

#define wdev_to_priv(w) ((struct rtw_wdev_priv *)(wdev_priv(w)))

#define wiphy_to_adapter(x) (_adapter *)(((struct rtw_wdev_priv*)wiphy_priv(x))->padapter)

#define wiphy_to_wdev(x) (struct wireless_dev *)(((struct rtw_wdev_priv*)wiphy_priv(x))->rtw_wdev)



int rtw_wdev_alloc(_adapter *padapter, struct device *dev);
void rtw_wdev_free(struct wireless_dev *wdev);

void rtw_cfg80211_init_wiphy(_adapter *padapter);

void rtw_cfg80211_surveydone_event_callback(_adapter *padapter);

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

int rtw_cfg80211_set_mgnt_wpsp2pie(struct net_device *net, char *buf, int len, int type);

#endif //__IOCTL_CFG80211_H__

