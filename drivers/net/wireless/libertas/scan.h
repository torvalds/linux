/**
  * Interface for the wlan network scan routines
  *
  * Driver interface functions and type declarations for the scan module
  *   implemented in wlan_scan.c.
  */
#ifndef _WLAN_SCAN_H
#define _WLAN_SCAN_H

#include <net/ieee80211.h>
#include "hostcmd.h"

/**
 *  @brief Maximum number of channels that can be sent in a setuserscan ioctl
 *
 *  @sa wlan_ioctl_user_scan_cfg
 */
#define WLAN_IOCTL_USER_SCAN_CHAN_MAX  50

//! Infrastructure BSS scan type in wlan_scan_cmd_config
#define WLAN_SCAN_BSS_TYPE_BSS         1

//! Adhoc BSS scan type in wlan_scan_cmd_config
#define WLAN_SCAN_BSS_TYPE_IBSS        2

//! Adhoc or Infrastructure BSS scan type in wlan_scan_cmd_config, no filter
#define WLAN_SCAN_BSS_TYPE_ANY         3

/**
 * @brief Structure used internally in the wlan driver to configure a scan.
 *
 * Sent to the command processing module to configure the firmware
 *   scan command prepared by libertas_cmd_80211_scan.
 *
 * @sa wlan_scan_networks
 *
 */
struct wlan_scan_cmd_config {
    /**
     *  @brief BSS type to be sent in the firmware command
     *
     *  Field can be used to restrict the types of networks returned in the
     *    scan.  valid settings are:
     *
     *   - WLAN_SCAN_BSS_TYPE_BSS  (infrastructure)
     *   - WLAN_SCAN_BSS_TYPE_IBSS (adhoc)
     *   - WLAN_SCAN_BSS_TYPE_ANY  (unrestricted, adhoc and infrastructure)
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
 *  @brief IOCTL channel sub-structure sent in wlan_ioctl_user_scan_cfg
 *
 *  Multiple instances of this structure are included in the IOCTL command
 *   to configure a instance of a scan on the specific channel.
 */
struct wlan_ioctl_user_scan_chan {
	u8 channumber;		//!< channel Number to scan
	u8 radiotype;		//!< Radio type: 'B/G' band = 0, 'A' band = 1
	u8 scantype;		//!< Scan type: Active = 0, Passive = 1
	u16 scantime;		//!< Scan duration in milliseconds; if 0 default used
};

/**
 *  @brief IOCTL input structure to configure an immediate scan cmd to firmware
 *
 *  Used in the setuserscan (WLAN_SET_USER_SCAN) private ioctl.  Specifies
 *   a number of parameters to be used in general for the scan as well
 *   as a channel list (wlan_ioctl_user_scan_chan) for each scan period
 *   desired.
 *
 *  @sa libertas_set_user_scan_ioctl
 */
struct wlan_ioctl_user_scan_cfg {
    /**
     *  @brief BSS type to be sent in the firmware command
     *
     *  Field can be used to restrict the types of networks returned in the
     *    scan.  valid settings are:
     *
     *   - WLAN_SCAN_BSS_TYPE_BSS  (infrastructure)
     *   - WLAN_SCAN_BSS_TYPE_IBSS (adhoc)
     *   - WLAN_SCAN_BSS_TYPE_ANY  (unrestricted, adhoc and infrastructure)
     */
	u8 bsstype;

    /**
     *  @brief Configure the number of probe requests for active chan scans
     */
	u8 numprobes;

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

    /**
     *  @brief Variable number (fixed maximum) of channels to scan up
     */
	struct wlan_ioctl_user_scan_chan chanlist[WLAN_IOCTL_USER_SCAN_CHAN_MAX];
};

/**
 *  @brief Structure used to store information for each beacon/probe response
 */
struct bss_descriptor {
	u8 bssid[ETH_ALEN];

	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;

	/* WEP encryption requirement */
	u32 privacy;

	/* receive signal strength in dBm */
	long rssi;

	u32 channel;

	u16 beaconperiod;

	u32 atimwindow;

	u8 mode;
	u8 libertas_supported_rates[WLAN_SUPPORTED_RATES];

	__le64 timestamp;	//!< TSF value included in the beacon/probe response
	unsigned long last_scanned;

	union ieeetypes_phyparamset phyparamset;
	union IEEEtypes_ssparamset ssparamset;
	struct ieeetypes_capinfo cap;
	u8 datarates[WLAN_SUPPORTED_RATES];

	u64 networktsf;		//!< TSF timestamp from the current firmware TSF

	struct ieeetypes_countryinfofullset countryinfo;

	u8 wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;
	u8 rsn_ie[MAX_WPA_IE_LEN];
	size_t rsn_ie_len;

	struct list_head list;
};

extern int libertas_ssid_cmp(u8 *ssid1, u8 ssid1_len, u8 *ssid2, u8 ssid2_len);

struct bss_descriptor * libertas_find_ssid_in_list(wlan_adapter * adapter,
			u8 *ssid, u8 ssid_len, u8 * bssid, u8 mode,
			int channel);

struct bss_descriptor * libertas_find_best_ssid_in_list(wlan_adapter * adapter,
			u8 mode);

extern struct bss_descriptor * libertas_find_bssid_in_list(wlan_adapter * adapter,
			u8 * bssid, u8 mode);

int libertas_find_best_network_ssid(wlan_private * priv, u8 *out_ssid,
			u8 *out_ssid_len, u8 preferred_mode, u8 *out_mode);

extern int libertas_send_specific_ssid_scan(wlan_private * priv, u8 *ssid,
				u8 ssid_len, u8 clear_ssid);
extern int libertas_send_specific_bssid_scan(wlan_private * priv,
				 u8 * bssid, u8 clear_bssid);

extern int libertas_cmd_80211_scan(wlan_private * priv,
				struct cmd_ds_command *cmd,
				void *pdata_buf);

extern int libertas_ret_80211_scan(wlan_private * priv,
				struct cmd_ds_command *resp);

int wlan_scan_networks(wlan_private * priv,
                const struct wlan_ioctl_user_scan_cfg * puserscanin,
                int full_scan);

struct ifreq;

struct iw_point;
struct iw_param;
struct iw_request_info;
extern int libertas_get_scan(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra);
extern int libertas_set_scan(struct net_device *dev, struct iw_request_info *info,
			 struct iw_param *vwrq, char *extra);

#endif				/* _WLAN_SCAN_H */
