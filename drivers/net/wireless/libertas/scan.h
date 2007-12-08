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
 *
 *  @sa lbs_ioctl_user_scan_cfg
 */
#define LBS_IOCTL_USER_SCAN_CHAN_MAX  50

//! Infrastructure BSS scan type in lbs_scan_cmd_config
#define LBS_SCAN_BSS_TYPE_BSS         1

//! Adhoc BSS scan type in lbs_scan_cmd_config
#define LBS_SCAN_BSS_TYPE_IBSS        2

//! Adhoc or Infrastructure BSS scan type in lbs_scan_cmd_config, no filter
#define LBS_SCAN_BSS_TYPE_ANY         3

/**
 * @brief Structure used internally in the wlan driver to configure a scan.
 *
 * Sent to the command processing module to configure the firmware
 *   scan command prepared by lbs_cmd_80211_scan.
 *
 * @sa lbs_scan_networks
 *
 */
struct lbs_scan_cmd_config {
    /**
     *  @brief BSS type to be sent in the firmware command
     *
     *  Field can be used to restrict the types of networks returned in the
     *    scan.  valid settings are:
     *
     *   - LBS_SCAN_BSS_TYPE_BSS  (infrastructure)
     *   - LBS_SCAN_BSS_TYPE_IBSS (adhoc)
     *   - LBS_SCAN_BSS_TYPE_ANY  (unrestricted, adhoc and infrastructure)
     */
	u8 bsstype;

    /**
     *  @brief Specific BSSID used to filter scan results in the firmware
     */
	u8 bssid[ETH_ALEN];

    /**
     *  @brief length of TLVs sent in command starting at tlvBuffer
     */
	int tlvbufferlen;

    /**
     *  @brief SSID TLV(s) and ChanList TLVs to be sent in the firmware command
     *
     *  @sa TLV_TYPE_CHANLIST, mrvlietypes_chanlistparamset_t
     *  @sa TLV_TYPE_SSID, mrvlietypes_ssidparamset_t
     */
	u8 tlvbuffer[1];	//!< SSID TLV(s) and ChanList TLVs are stored here
};

/**
 *  @brief IOCTL channel sub-structure sent in lbs_ioctl_user_scan_cfg
 *
 *  Multiple instances of this structure are included in the IOCTL command
 *   to configure a instance of a scan on the specific channel.
 */
struct lbs_ioctl_user_scan_chan {
	u8 channumber;		//!< channel Number to scan
	u8 radiotype;		//!< Radio type: 'B/G' band = 0, 'A' band = 1
	u8 scantype;		//!< Scan type: Active = 0, Passive = 1
	u16 scantime;		//!< Scan duration in milliseconds; if 0 default used
};

/**
 *  @brief IOCTL input structure to configure an immediate scan cmd to firmware
 *
 *  Used in the setuserscan (LBS_SET_USER_SCAN) private ioctl.  Specifies
 *   a number of parameters to be used in general for the scan as well
 *   as a channel list (lbs_ioctl_user_scan_chan) for each scan period
 *   desired.
 *
 *  @sa lbs_set_user_scan_ioctl
 */
struct lbs_ioctl_user_scan_cfg {
    /**
     *  @brief BSS type to be sent in the firmware command
     *
     *  Field can be used to restrict the types of networks returned in the
     *    scan.  valid settings are:
     *
     *   - LBS_SCAN_BSS_TYPE_BSS  (infrastructure)
     *   - LBS_SCAN_BSS_TYPE_IBSS (adhoc)
     *   - LBS_SCAN_BSS_TYPE_ANY  (unrestricted, adhoc and infrastructure)
     */
	u8 bsstype;

	/**
	 *  @brief BSSID filter sent in the firmware command to limit the results
	 */
	u8 bssid[ETH_ALEN];

	/* Clear existing scan results matching this BSSID */
	u8 clear_bssid;

	/**
	 *  @brief SSID filter sent in the firmware command to limit the results
	 */
	char ssid[IW_ESSID_MAX_SIZE];
	u8 ssid_len;

	/* Clear existing scan results matching this SSID */
	u8 clear_ssid;
};

/**
 *  @brief Structure used to store information for each beacon/probe response
 */
struct bss_descriptor {
	u8 bssid[ETH_ALEN];

	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;

	u16 capability;

	/* receive signal strength in dBm */
	long rssi;

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
				u8 ssid_len, u8 clear_ssid);

int lbs_cmd_80211_scan(struct lbs_private *priv,
				struct cmd_ds_command *cmd,
				void *pdata_buf);

int lbs_ret_80211_scan(struct lbs_private *priv,
				struct cmd_ds_command *resp);

int lbs_scan_networks(struct lbs_private *priv,
	const struct lbs_ioctl_user_scan_cfg *puserscanin,
                int full_scan);

struct ifreq;

struct iw_point;
struct iw_param;
struct iw_request_info;
int lbs_get_scan(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra);
int lbs_set_scan(struct net_device *dev, struct iw_request_info *info,
			 struct iw_param *vwrq, char *extra);

void lbs_scan_worker(struct work_struct *work);

#endif
