/*
 * Header for Linux cfg80211 scan
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _wl_cfgscan_h_
#define _wl_cfgscan_h_

#include <linux/wireless.h>
#include <typedefs.h>
#include <ethernet.h>
#include <wlioctl.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>
#include <linux/rfkill.h>
#include <osl.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
#define GET_SCAN_WDEV(scan_request) \
	(scan_request && scan_request->dev) ? scan_request->dev->ieee80211_ptr : NULL;
#else
#define GET_SCAN_WDEV(scan_request) \
	scan_request ? scan_request->wdev : NULL;
#endif
#ifdef WL_SCHED_SCAN
#define GET_SCHED_SCAN_WDEV(scan_request) \
	(scan_request && scan_request->dev) ? scan_request->dev->ieee80211_ptr : NULL;
#endif /* WL_SCHED_SCAN */

#ifdef DUAL_ESCAN_RESULT_BUFFER
#define wl_escan_set_sync_id(a, b) ((a) = (b)->escan_info.cur_sync_id)
#define wl_escan_set_type(a, b) ((a)->escan_info.escan_type\
				[((a)->escan_info.cur_sync_id)%SCAN_BUF_CNT] = (b))
#else
#define wl_escan_set_sync_id(a, b) ((a) = htod16((b)->escan_sync_id_cntr++))
#define wl_escan_set_type(a, b)
#endif /* DUAL_ESCAN_RESULT_BUFFER */
extern s32 wl_escan_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
extern s32 wl_do_escan(struct bcm_cfg80211 *cfg, struct wiphy *wiphy,
	struct net_device *ndev, struct cfg80211_scan_request *request);
extern s32 __wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request, struct cfg80211_ssid *this_ssid);
#if defined(WL_CFG80211_P2P_DEV_IF)
extern s32 wl_cfg80211_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request);
#else
extern s32 wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request);
extern int wl_cfg80211_scan_stop(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev);
#endif /* WL_CFG80211_P2P_DEV_IF */

#if defined(OEM_ANDROID) && defined(DHCP_SCAN_SUPPRESS)
extern void wl_cfg80211_work_handler(struct work_struct *work);
extern void wl_cfg80211_scan_supp_timerfunc(ulong data);
#endif /* DHCP_SCAN_SUPPRESS */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0))
extern void wl_cfg80211_abort_scan(struct wiphy *wiphy, struct wireless_dev *wdev);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)) */
extern s32 wl_init_scan(struct bcm_cfg80211 *cfg);
extern int wl_cfg80211_scan_stop(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev);
extern s32 wl_notify_scan_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
extern void wl_cfg80211_set_passive_scan(struct net_device *dev, char *command);
#ifdef PNO_SUPPORT
extern s32 wl_notify_pfn_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif /* PNO_SUPPORT */
#ifdef GSCAN_SUPPORT
extern s32 wl_notify_gscan_event(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif /* GSCAN_SUPPORT */

#ifdef WES_SUPPORT
#ifdef CUSTOMER_SCAN_TIMEOUT_SETTING
#define CUSTOMER_WL_SCAN_TIMER_INTERVAL_MS	25000 /* Scan timeout */
enum wl_custom_scan_time_type {
	WL_CUSTOM_SCAN_CHANNEL_TIME = 0,
	WL_CUSTOM_SCAN_UNASSOC_TIME,
	WL_CUSTOM_SCAN_PASSIVE_TIME,
	WL_CUSTOM_SCAN_HOME_TIME,
	WL_CUSTOM_SCAN_HOME_AWAY_TIME
};
extern s32 wl_cfg80211_custom_scan_time(struct net_device *dev,
		enum wl_custom_scan_time_type type, int time);
#endif /* CUSTOMER_SCAN_TIMEOUT_SETTING */
#endif /* WES_SUPPORT */

#if defined(SUPPORT_RANDOM_MAC_SCAN)
int wl_cfg80211_set_random_mac(struct net_device *dev, bool enable);
int wl_cfg80211_random_mac_enable(struct net_device *dev);
int wl_cfg80211_random_mac_disable(struct net_device *dev);
int wl_cfg80211_scan_mac_enable(struct net_device *dev);
int wl_cfg80211_scan_mac_disable(struct net_device *dev);
int wl_cfg80211_scan_mac_config(struct net_device *dev, uint8 *rand_mac, uint8 *rand_mask);
#endif /* SUPPORT_RANDOM_MAC_SCAN */

#ifdef WL_SCHED_SCAN
extern int wl_cfg80211_sched_scan_start(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_sched_scan_request *request);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 11, 0))
extern int wl_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev, u64 reqid);
#else
extern int wl_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev);
#endif /* LINUX_VER > 4.11 */
#endif /* WL_SCHED_SCAN */
extern s32 wl_cfgscan_listen_on_channel(struct bcm_cfg80211 *cfg, struct wireless_dev *wdev,
		struct ieee80211_channel *channel, unsigned int duration);
extern void wl_cfgscan_listen_complete_work(struct work_struct *work);
extern s32 wl_cfgscan_notify_listen_complete(struct bcm_cfg80211 *cfg);
extern s32 wl_cfgscan_cancel_listen_on_channel(struct bcm_cfg80211 *cfg, bool notify_user);
#if defined(WL_CFG80211_P2P_DEV_IF)
extern s32 wl_cfgscan_remain_on_channel(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	struct ieee80211_channel *channel, unsigned int duration, u64 *cookie);
#else
extern s32 wl_cfgscan_remain_on_channel(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	struct ieee80211_channel *channel, enum nl80211_channel_type channel_type,
	unsigned int duration, u64 *cookie);
#endif /* WL_CFG80211_P2P_DEV_IF */
extern s32 wl_cfgscan_cancel_remain_on_channel(struct wiphy *wiphy,
	bcm_struct_cfgdev *cfgdev, u64 cookie);
extern chanspec_t wl_freq_to_chanspec(int freq);
extern s32 wl_inform_single_bss(struct bcm_cfg80211 *cfg, wl_bss_info_t *bi, bool update_ssid);
#ifdef WL_GET_RCC
extern int wl_android_get_roam_scan_chanlist(struct bcm_cfg80211 *cfg);
#endif /* WL_GET_RCC */
extern s32 wl_get_assoc_channels(struct bcm_cfg80211 *cfg,
	struct net_device *dev, wlcfg_assoc_info_t *info);
extern void wl_cfgscan_cancel_scan(struct bcm_cfg80211 *cfg);
extern void wl_cfgscan_scan_abort(struct bcm_cfg80211 *cfg);
#ifdef DHD_GET_VALID_CHANNELS
typedef enum {
	WIFI_BAND_UNSPECIFIED,
	/* 2.4 GHz */
	WIFI_BAND_BG = 1,
	/* 5 GHz without DFS */
	WIFI_BAND_A = 2,
	/* 5 GHz DFS only */
	WIFI_BAND_A_DFS = 4,
	/* 5 GHz with DFS */
	WIFI_BAND_A_WITH_DFS = 6,
	/* 2.4 GHz + 5 GHz; no DFS */
	WIFI_BAND_ABG = 3,
	/* 2.4 GHz + 5 GHz with DFS */
	WIFI_BAND_ABG_WITH_DFS = 7,
	/* 6GHz */
	WIFI_BAND_6GHZ = 8,
	/* 5 GHz no DFS + 6 GHz */
	WIFI_BAND_5GHZ_6GHZ = 10,
	/* 2.4 GHz + 5 GHz no DFS + 6 GHz */
	WIFI_AND_24GHZ_5GHZ_6GHZ = 11,
	/* 2.4 GHz + 5 GHz with DFS + 6 GHz */
	WIFI_BAND_24GHZ_5GHZ_WITH_DFS_6GHZ = 15
} wifi_band;

extern bool wl_cfgscan_is_dfs_set(wifi_band band);
extern s32 wl_cfgscan_get_band_freq_list(struct bcm_cfg80211 *cfg, int band,
        uint16 *list, uint32 *num_channels);
#endif /* DHD_GET_VALID_CHANNELS */
#endif /* _wl_cfgscan_h_ */
