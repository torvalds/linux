/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. i*/

#ifndef __IOCTL_CFG80211_H__
#define __IOCTL_CFG80211_H__

struct rtw_wdev_invit_info {
	u8 token;
	u8 flags;
	u8 status;
	u8 req_op_ch;
	u8 rsp_op_ch;
};

#define rtw_wdev_invit_info_init(invit_info) \
	do { \
		(invit_info)->token = 0; \
		(invit_info)->flags = 0x00; \
		(invit_info)->status = 0xff; \
		(invit_info)->req_op_ch = 0; \
		(invit_info)->rsp_op_ch = 0; \
	} while (0)

struct rtw_wdev_priv {
	struct wireless_dev *rtw_wdev;

	struct adapter *padapter;

	struct cfg80211_scan_request *scan_request;
	spinlock_t scan_req_lock;

	struct net_device *pmon_ndev;/* for monitor interface */
	char ifname_mon[IFNAMSIZ + 1]; /* name of monitor interface */

	u8 p2p_enabled;

	u8 provdisc_req_issued;

	struct rtw_wdev_invit_info invit_info;

	u8 bandroid_scan;
	bool block;
	bool power_mgmt;
};

#define wdev_to_priv(w) ((struct rtw_wdev_priv *)(wdev_priv(w)))

#define wiphy_to_wdev(x)				\
((struct wireless_dev *)(((struct rtw_wdev_priv *)wiphy_priv(x))->rtw_wdev))

int rtw_wdev_alloc(struct adapter *padapter, struct device *dev);
void rtw_wdev_free(struct wireless_dev *wdev);
void rtw_wdev_unregister(struct wireless_dev *wdev);

void rtw_cfg80211_init_wiphy(struct adapter *padapter);

void rtw_cfg80211_surveydone_event_callback(struct adapter *padapter);

void rtw_cfg80211_indicate_connect(struct adapter *padapter);
void rtw_cfg80211_indicate_disconnect(struct adapter *padapter);
void rtw_cfg80211_indicate_scan_done(struct rtw_wdev_priv *pwdev_priv,
				     bool aborted);

void rtw_cfg80211_indicate_sta_assoc(struct adapter *padapter,
				     u8 *pmgmt_frame, uint frame_len);
void rtw_cfg80211_indicate_sta_disassoc(struct adapter *padapter,
					unsigned char *da,
					unsigned short reason);

void rtw_cfg80211_issue_p2p_provision_request(struct adapter *padapter,
					      const u8 *buf, size_t len);
void rtw_cfg80211_rx_p2p_action_public(struct adapter *padapter,
				       u8 *pmgmt_frame, uint frame_len);
void rtw_cfg80211_rx_action_p2p(struct adapter *padapter, u8 *pmgmt_frame,
				uint frame_len);
void rtw_cfg80211_rx_action(struct adapter *adapter, u8 *frame,
			    uint frame_len, const char *msg);

int rtw_cfg80211_set_mgnt_wpsp2pie(struct net_device *net,
				   char *buf, int len, int type);

bool rtw_cfg80211_pwr_mgmt(struct adapter *adapter);

#define rtw_cfg80211_rx_mgmt(dev, freq, sig_dbm, buf, len, gfp)		\
	cfg80211_rx_mgmt(dev, freq, sig_dbm, buf, len, gfp)
#define rtw_cfg80211_send_rx_assoc(dev, bss, buf, len)			\
	cfg80211_send_rx_assoc(dev, bss, buf, len)

#endif /* __IOCTL_CFG80211_H__ */
