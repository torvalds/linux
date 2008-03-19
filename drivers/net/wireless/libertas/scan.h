/**
  * Interface for the wlan network scan routines
  *
  * Driver interface functions and type declarations for the scan module
  * implemented in scan.c.
  */
#ifndef _LBS_SCAN_H
#define _LBS_SCAN_H

#include <net/ieee80211.h>
#include "hostcmd.h"

/**
 *  @brief Maximum number of channels that can be sent in a setuserscan ioctl
 */
#define LBS_IOCTL_USER_SCAN_CHAN_MAX  50

//! Infrastructure BSS scan type in cmd_ds_802_11_scan
#define LBS_SCAN_BSS_TYPE_BSS         1

//! Adhoc BSS scan type in cmd_ds_802_11_scan
#define LBS_SCAN_BSS_TYPE_IBSS        2

//! Adhoc or Infrastructure BSS scan type in cmd_ds_802_11_scan, no filter
#define LBS_SCAN_BSS_TYPE_ANY         3

/**
 *  @brief Structure used to store information for each beacon/probe response
 */
struct bss_descriptor {
	u8 bssid[ETH_ALEN];

	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;

	u16 capability;
	u32 rssi;
	u32 channel;
	u16 beaconperiod;
	u32 atimwindow;

	/* IW_MODE_AUTO, IW_MODE_ADHOC, IW_MODE_INFRA */
	u8 mode;

	/* zero-terminated array of supported data rates */
	u8 rates[MAX_RATES + 1];

	unsigned long last_scanned;

	union ieeetypes_phyparamset phyparamset;
	union IEEEtypes_ssparamset ssparamset;

	struct ieeetypes_countryinfofullset countryinfo;

	u8 wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;
	u8 rsn_ie[MAX_WPA_IE_LEN];
	size_t rsn_ie_len;

	u8 mesh;

	struct list_head list;
};

int lbs_ssid_cmp(u8 *ssid1, u8 ssid1_len, u8 *ssid2, u8 ssid2_len);

struct bss_descriptor *lbs_find_ssid_in_list(struct lbs_private *priv,
		u8 *ssid, u8 ssid_len, u8 *bssid, u8 mode,
		int channel);

struct bss_descriptor *lbs_find_bssid_in_list(struct lbs_private *priv,
	u8 *bssid, u8 mode);

int lbs_find_best_network_ssid(struct lbs_private *priv, u8 *out_ssid,
			u8 *out_ssid_len, u8 preferred_mode, u8 *out_mode);

int lbs_send_specific_ssid_scan(struct lbs_private *priv, u8 *ssid,
				u8 ssid_len);

int lbs_get_scan(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra);
int lbs_set_scan(struct net_device *dev, struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra);

void lbs_scan_worker(struct work_struct *work);

#endif
