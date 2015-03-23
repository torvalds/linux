/*
 * Driver interface definition
 * Copyright (c) 2003-2010, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This file defines a driver interface used by both %wpa_supplicant and
 * hostapd. The first part of the file defines data structures used in various
 * driver operations. This is followed by the struct wpa_driver_ops that each
 * driver wrapper will beed to define with callback functions for requesting
 * driver operations. After this, there are definitions for driver event
 * reporting with wpa_supplicant_event() and some convenience helper functions
 * that can be used to report events.
 */

#ifndef DRIVER_H
#define DRIVER_H

#define WPA_SUPPLICANT_DRIVER_VERSION 4

#include "common/defs.h"

#define HOSTAPD_CHAN_DISABLED 0x00000001
#define HOSTAPD_CHAN_PASSIVE_SCAN 0x00000002
#define HOSTAPD_CHAN_NO_IBSS 0x00000004
#define HOSTAPD_CHAN_RADAR 0x00000008
#define HOSTAPD_CHAN_HT40PLUS 0x00000010
#define HOSTAPD_CHAN_HT40MINUS 0x00000020
#define HOSTAPD_CHAN_HT40 0x00000040

/**
 * struct hostapd_channel_data - Channel information
 */
struct hostapd_channel_data {
	/**
	 * chan - Channel number (IEEE 802.11)
	 */
	short chan;

	/**
	 * freq - Frequency in MHz
	 */
	short freq;

	/**
	 * flag - Channel flags (HOSTAPD_CHAN_*)
	 */
	int flag;

	/**
	 * max_tx_power - maximum transmit power in dBm
	 */
	u8 max_tx_power;
};

/**
 * struct hostapd_hw_modes - Supported hardware mode information
 */
struct hostapd_hw_modes {
	/**
	 * mode - Hardware mode
	 */
	enum hostapd_hw_mode mode;

	/**
	 * num_channels - Number of entries in the channels array
	 */
	int num_channels;

	/**
	 * channels - Array of supported channels
	 */
	struct hostapd_channel_data *channels;

	/**
	 * num_rates - Number of entries in the rates array
	 */
	int num_rates;

	/**
	 * rates - Array of supported rates in 100 kbps units
	 */
	int *rates;

	/**
	 * ht_capab - HT (IEEE 802.11n) capabilities
	 */
	u16 ht_capab;

	/**
	 * mcs_set - MCS (IEEE 802.11n) rate parameters
	 */
	u8 mcs_set[16];

	/**
	 * a_mpdu_params - A-MPDU (IEEE 802.11n) parameters
	 */
	u8 a_mpdu_params;
};


#define IEEE80211_MODE_INFRA	0
#define IEEE80211_MODE_IBSS	1
#define IEEE80211_MODE_AP	2

#define IEEE80211_CAP_ESS	0x0001
#define IEEE80211_CAP_IBSS	0x0002
#define IEEE80211_CAP_PRIVACY	0x0010

#define WPA_SCAN_QUAL_INVALID		BIT(0)
#define WPA_SCAN_NOISE_INVALID		BIT(1)
#define WPA_SCAN_LEVEL_INVALID		BIT(2)
#define WPA_SCAN_LEVEL_DBM		BIT(3)
#define WPA_SCAN_AUTHENTICATED		BIT(4)
#define WPA_SCAN_ASSOCIATED		BIT(5)

/**
 * struct wpa_scan_res - Scan result for an BSS/IBSS
 * @flags: information flags about the BSS/IBSS (WPA_SCAN_*)
 * @bssid: BSSID
 * @freq: frequency of the channel in MHz (e.g., 2412 = channel 1)
 * @beacon_int: beacon interval in TUs (host byte order)
 * @caps: capability information field in host byte order
 * @qual: signal quality
 * @noise: noise level
 * @level: signal level
 * @tsf: Timestamp
 * @age: Age of the information in milliseconds (i.e., how many milliseconds
 * ago the last Beacon or Probe Response frame was received)
 * @ie_len: length of the following IE field in octets
 * @beacon_ie_len: length of the following Beacon IE field in octets
 *
 * This structure is used as a generic format for scan results from the
 * driver. Each driver interface implementation is responsible for converting
 * the driver or OS specific scan results into this format.
 *
 * If the driver does not support reporting all IEs, the IE data structure is
 * constructed of the IEs that are available. This field will also need to
 * include SSID in IE format. All drivers are encouraged to be extended to
 * report all IEs to make it easier to support future additions.
 */
struct wpa_scan_res {
	unsigned int flags;
	u8 bssid[ETH_ALEN];
	int freq;
	u16 beacon_int;
	u16 caps;
	int qual;
	int noise;
	int level;
	u64 tsf;
	unsigned int age;
	size_t ie_len;
	size_t beacon_ie_len;
	/*
	 * Followed by ie_len octets of IEs from Probe Response frame (or if
	 * the driver does not indicate source of IEs, these may also be from
	 * Beacon frame). After the first set of IEs, another set of IEs may
	 * follow (with beacon_ie_len octets of data) if the driver provides
	 * both IE sets.
	 */
};

/**
 * struct wpa_scan_results - Scan results
 * @res: Array of pointers to allocated variable length scan result entries
 * @num: Number of entries in the scan result array
 */
struct wpa_scan_results {
	struct wpa_scan_res **res;
	size_t num;
};

/**
 * struct wpa_interface_info - Network interface information
 * @next: Pointer to the next interface or NULL if this is the last one
 * @ifname: Interface name that can be used with init() or init2()
 * @desc: Human readable adapter description (e.g., vendor/model) or NULL if
 *	not available
 * @drv_name: struct wpa_driver_ops::name (note: unlike other strings, this one
 *	is not an allocated copy, i.e., get_interfaces() caller will not free
 *	this)
 */
struct wpa_interface_info {
	struct wpa_interface_info *next;
	char *ifname;
	char *desc;
	const char *drv_name;
};

#define WPAS_MAX_SCAN_SSIDS 4

/**
 * struct wpa_driver_scan_params - Scan parameters
 * Data for struct wpa_driver_ops::scan2().
 */
struct wpa_driver_scan_params {
	/**
	 * ssids - SSIDs to scan for
	 */
	struct wpa_driver_scan_ssid {
		/**
		 * ssid - specific SSID to scan for (ProbeReq)
		 * %NULL or zero-length SSID is used to indicate active scan
		 * with wildcard SSID.
		 */
		const u8 *ssid;
		/**
		 * ssid_len: Length of the SSID in octets
		 */
		size_t ssid_len;
	} ssids[WPAS_MAX_SCAN_SSIDS];

	/**
	 * num_ssids - Number of entries in ssids array
	 * Zero indicates a request for a passive scan.
	 */
	size_t num_ssids;

	/**
	 * extra_ies - Extra IE(s) to add into Probe Request or %NULL
	 */
	const u8 *extra_ies;

	/**
	 * extra_ies_len - Length of extra_ies in octets
	 */
	size_t extra_ies_len;

	/**
	 * freqs - Array of frequencies to scan or %NULL for all frequencies
	 *
	 * The frequency is set in MHz. The array is zero-terminated.
	 */
	int *freqs;

	/**
	 * filter_ssids - Filter for reporting SSIDs
	 *
	 * This optional parameter can be used to request the driver wrapper to
	 * filter scan results to include only the specified SSIDs. %NULL
	 * indicates that no filtering is to be done. This can be used to
	 * reduce memory needs for scan results in environments that have large
	 * number of APs with different SSIDs.
	 *
	 * The driver wrapper is allowed to take this allocated buffer into its
	 * own use by setting the pointer to %NULL. In that case, the driver
	 * wrapper is responsible for freeing the buffer with os_free() once it
	 * is not needed anymore.
	 */
	struct wpa_driver_scan_filter {
		u8 ssid[32];
		size_t ssid_len;
	} *filter_ssids;

	/**
	 * num_filter_ssids - Number of entries in filter_ssids array
	 */
	size_t num_filter_ssids;
};

/**
 * struct wpa_driver_auth_params - Authentication parameters
 * Data for struct wpa_driver_ops::authenticate().
 */
struct wpa_driver_auth_params {
	int freq;
	const u8 *bssid;
	const u8 *ssid;
	size_t ssid_len;
	int auth_alg;
	const u8 *ie;
	size_t ie_len;
	const u8 *wep_key[4];
	size_t wep_key_len[4];
	int wep_tx_keyidx;
	int local_state_change;
};

enum wps_mode {
	WPS_MODE_NONE /* no WPS provisioning being used */,
	WPS_MODE_OPEN /* WPS provisioning with AP that is in open mode */,
	WPS_MODE_PRIVACY /* WPS provisioning with AP that is using protection
			  */
};

/**
 * struct wpa_driver_associate_params - Association parameters
 * Data for struct wpa_driver_ops::associate().
 */
struct wpa_driver_associate_params {
	/**
	 * bssid - BSSID of the selected AP
	 * This can be %NULL, if ap_scan=2 mode is used and the driver is
	 * responsible for selecting with which BSS to associate. */
	const u8 *bssid;

	/**
	 * ssid - The selected SSID
	 */
	const u8 *ssid;

	/**
	 * ssid_len - Length of the SSID (1..32)
	 */
	size_t ssid_len;

	/**
	 * freq - Frequency of the channel the selected AP is using
	 * Frequency that the selected AP is using (in MHz as
	 * reported in the scan results)
	 */
	int freq;

	/**
	 * wpa_ie - WPA information element for (Re)Association Request
	 * WPA information element to be included in (Re)Association
	 * Request (including information element id and length). Use
	 * of this WPA IE is optional. If the driver generates the WPA
	 * IE, it can use pairwise_suite, group_suite, and
	 * key_mgmt_suite to select proper algorithms. In this case,
	 * the driver has to notify wpa_supplicant about the used WPA
	 * IE by generating an event that the interface code will
	 * convert into EVENT_ASSOCINFO data (see below).
	 *
	 * When using WPA2/IEEE 802.11i, wpa_ie is used for RSN IE
	 * instead. The driver can determine which version is used by
	 * looking at the first byte of the IE (0xdd for WPA, 0x30 for
	 * WPA2/RSN).
	 *
	 * When using WPS, wpa_ie is used for WPS IE instead of WPA/RSN IE.
	 */
	const u8 *wpa_ie;

	/**
	 * wpa_ie_len - length of the wpa_ie
	 */
	size_t wpa_ie_len;

	/**
	 * pairwise_suite - Selected pairwise cipher suite
	 *
	 * This is usually ignored if @wpa_ie is used.
	 */
	enum wpa_cipher pairwise_suite;

	/**
	 * group_suite - Selected group cipher suite
	 *
	 * This is usually ignored if @wpa_ie is used.
	 */
	enum wpa_cipher group_suite;

	/**
	 * key_mgmt_suite - Selected key management suite
	 *
	 * This is usually ignored if @wpa_ie is used.
	 */
	enum wpa_key_mgmt key_mgmt_suite;

	/**
	 * auth_alg - Allowed authentication algorithms
	 * Bit field of WPA_AUTH_ALG_*
	 */
	int auth_alg;

	/**
	 * mode - Operation mode (infra/ibss) IEEE80211_MODE_*
	 */
	int mode;

	/**
	 * wep_key - WEP keys for static WEP configuration
	 */
	const u8 *wep_key[4];

	/**
	 * wep_key_len - WEP key length for static WEP configuration
	 */
	size_t wep_key_len[4];

	/**
	 * wep_tx_keyidx - WEP TX key index for static WEP configuration
	 */
	int wep_tx_keyidx;

	/**
	 * mgmt_frame_protection - IEEE 802.11w management frame protection
	 */
	enum mfp_options mgmt_frame_protection;

	/**
	 * ft_ies - IEEE 802.11r / FT information elements
	 * If the supplicant is using IEEE 802.11r (FT) and has the needed keys
	 * for fast transition, this parameter is set to include the IEs that
	 * are to be sent in the next FT Authentication Request message.
	 * update_ft_ies() handler is called to update the IEs for further
	 * FT messages in the sequence.
	 *
	 * The driver should use these IEs only if the target AP is advertising
	 * the same mobility domain as the one included in the MDIE here.
	 *
	 * In ap_scan=2 mode, the driver can use these IEs when moving to a new
	 * AP after the initial association. These IEs can only be used if the
	 * target AP is advertising support for FT and is using the same MDIE
	 * and SSID as the current AP.
	 *
	 * The driver is responsible for reporting the FT IEs received from the
	 * AP's response using wpa_supplicant_event() with EVENT_FT_RESPONSE
	 * type. update_ft_ies() handler will then be called with the FT IEs to
	 * include in the next frame in the authentication sequence.
	 */
	const u8 *ft_ies;

	/**
	 * ft_ies_len - Length of ft_ies in bytes
	 */
	size_t ft_ies_len;

	/**
	 * ft_md - FT Mobility domain (6 octets) (also included inside ft_ies)
	 *
	 * This value is provided to allow the driver interface easier access
	 * to the current mobility domain. This value is set to %NULL if no
	 * mobility domain is currently active.
	 */
	const u8 *ft_md;

	/**
	 * passphrase - RSN passphrase for PSK
	 *
	 * This value is made available only for WPA/WPA2-Personal (PSK) and
	 * only for drivers that set WPA_DRIVER_FLAGS_4WAY_HANDSHAKE. This is
	 * the 8..63 character ASCII passphrase, if available. Please note that
	 * this can be %NULL if passphrase was not used to generate the PSK. In
	 * that case, the psk field must be used to fetch the PSK.
	 */
	const char *passphrase;

	/**
	 * psk - RSN PSK (alternative for passphrase for PSK)
	 *
	 * This value is made available only for WPA/WPA2-Personal (PSK) and
	 * only for drivers that set WPA_DRIVER_FLAGS_4WAY_HANDSHAKE. This is
	 * the 32-octet (256-bit) PSK, if available. The driver wrapper should
	 * be prepared to handle %NULL value as an error.
	 */
	const u8 *psk;

	/**
	 * drop_unencrypted - Enable/disable unencrypted frame filtering
	 *
	 * Configure the driver to drop all non-EAPOL frames (both receive and
	 * transmit paths). Unencrypted EAPOL frames (ethertype 0x888e) must
	 * still be allowed for key negotiation.
	 */
	int drop_unencrypted;

	/**
	 * prev_bssid - Previously used BSSID in this ESS
	 *
	 * When not %NULL, this is a request to use reassociation instead of
	 * association.
	 */
	const u8 *prev_bssid;

	/**
	 * wps - WPS mode
	 *
	 * If the driver needs to do special configuration for WPS association,
	 * this variable provides more information on what type of association
	 * is being requested. Most drivers should not need ot use this.
	 */
	enum wps_mode wps;

	/**
	 * p2p - Whether this connection is a P2P group
	 */
	int p2p;

	/**
	 * uapsd - UAPSD parameters for the network
	 * -1 = do not change defaults
	 * AP mode: 1 = enabled, 0 = disabled
	 * STA mode: bits 0..3 UAPSD enabled for VO,VI,BK,BE
	 */
	int uapsd;
};

/**
 * struct wpa_driver_capa - Driver capability information
 */
struct wpa_driver_capa {
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA		0x00000001
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA2		0x00000002
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK	0x00000004
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK	0x00000008
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA_NONE	0x00000010
#define WPA_DRIVER_CAPA_KEY_MGMT_FT		0x00000020
#define WPA_DRIVER_CAPA_KEY_MGMT_FT_PSK		0x00000040
	unsigned int key_mgmt;

#define WPA_DRIVER_CAPA_ENC_WEP40	0x00000001
#define WPA_DRIVER_CAPA_ENC_WEP104	0x00000002
#define WPA_DRIVER_CAPA_ENC_TKIP	0x00000004
#define WPA_DRIVER_CAPA_ENC_CCMP	0x00000008
	unsigned int enc;

#define WPA_DRIVER_AUTH_OPEN		0x00000001
#define WPA_DRIVER_AUTH_SHARED		0x00000002
#define WPA_DRIVER_AUTH_LEAP		0x00000004
	unsigned int auth;

/* Driver generated WPA/RSN IE */
#define WPA_DRIVER_FLAGS_DRIVER_IE	0x00000001
/* Driver needs static WEP key setup after association command */
#define WPA_DRIVER_FLAGS_SET_KEYS_AFTER_ASSOC 0x00000002
#define WPA_DRIVER_FLAGS_USER_SPACE_MLME 0x00000004
/* Driver takes care of RSN 4-way handshake internally; PMK is configured with
 * struct wpa_driver_ops::set_key using alg = WPA_ALG_PMK */
#define WPA_DRIVER_FLAGS_4WAY_HANDSHAKE 0x00000008
#define WPA_DRIVER_FLAGS_WIRED		0x00000010
/* Driver provides separate commands for authentication and association (SME in
 * wpa_supplicant). */
#define WPA_DRIVER_FLAGS_SME		0x00000020
/* Driver supports AP mode */
#define WPA_DRIVER_FLAGS_AP		0x00000040
/* Driver needs static WEP key setup after association has been completed */
#define WPA_DRIVER_FLAGS_SET_KEYS_AFTER_ASSOC_DONE	0x00000080
/* Driver takes care of P2P management operations */
#define WPA_DRIVER_FLAGS_P2P_MGMT	0x00000100
/* Driver supports concurrent P2P operations */
#define WPA_DRIVER_FLAGS_P2P_CONCURRENT	0x00000200
/*
 * Driver uses the initial interface as a dedicated management interface, i.e.,
 * it cannot be used for P2P group operations or non-P2P purposes.
 */
#define WPA_DRIVER_FLAGS_P2P_DEDICATED_INTERFACE	0x00000400
/* This interface is P2P capable (P2P Device, GO, or P2P Client */
#define WPA_DRIVER_FLAGS_P2P_CAPABLE	0x00000800
/* Driver supports concurrent operations on multiple channels */
#define WPA_DRIVER_FLAGS_MULTI_CHANNEL_CONCURRENT	0x00001000
/*
 * Driver uses the initial interface for P2P management interface and non-P2P
 * purposes (e.g., connect to infra AP), but this interface cannot be used for
 * P2P group operations.
 */
#define WPA_DRIVER_FLAGS_P2P_MGMT_AND_NON_P2P		0x00002000
/*
 * Driver is known to use sane error codes, i.e., when it indicates that
 * something (e.g., association) fails, there was indeed a failure and the
 * operation does not end up getting completed successfully later.
 */
#define WPA_DRIVER_FLAGS_SANE_ERROR_CODES		0x00004000
/* Driver supports off-channel TX */
#define WPA_DRIVER_FLAGS_OFFCHANNEL_TX			0x00008000
/* Driver indicates TX status events for EAPOL Data frames */
#define WPA_DRIVER_FLAGS_EAPOL_TX_STATUS		0x00010000
	unsigned int flags;

	int max_scan_ssids;

	/**
	 * max_remain_on_chan - Maximum remain-on-channel duration in msec
	 */
	unsigned int max_remain_on_chan;

	/**
	 * max_stations - Maximum number of associated stations the driver
	 * supports in AP mode
	 */
	unsigned int max_stations;
};


struct hostapd_data;

struct hostap_sta_driver_data {
	unsigned long rx_packets, tx_packets, rx_bytes, tx_bytes;
	unsigned long current_tx_rate;
	unsigned long inactive_msec;
	unsigned long flags;
	unsigned long num_ps_buf_frames;
	unsigned long tx_retry_failed;
	unsigned long tx_retry_count;
	int last_rssi;
	int last_ack_rssi;
};

struct hostapd_sta_add_params {
	const u8 *addr;
	u16 aid;
	u16 capability;
	const u8 *supp_rates;
	size_t supp_rates_len;
	u16 listen_interval;
	const struct ieee80211_ht_capabilities *ht_capabilities;
};

struct hostapd_freq_params {
	int mode;
	int freq;
	int channel;
	int ht_enabled;
	int sec_channel_offset; /* 0 = HT40 disabled, -1 = HT40 enabled,
				 * secondary channel below primary, 1 = HT40
				 * enabled, secondary channel above primary */
};

enum wpa_driver_if_type {
	/**
	 * WPA_IF_STATION - Station mode interface
	 */
	WPA_IF_STATION,

	/**
	 * WPA_IF_AP_VLAN - AP mode VLAN interface
	 *
	 * This interface shares its address and Beacon frame with the main
	 * BSS.
	 */
	WPA_IF_AP_VLAN,

	/**
	 * WPA_IF_AP_BSS - AP mode BSS interface
	 *
	 * This interface has its own address and Beacon frame.
	 */
	WPA_IF_AP_BSS,

	/**
	 * WPA_IF_P2P_GO - P2P Group Owner
	 */
	WPA_IF_P2P_GO,

	/**
	 * WPA_IF_P2P_CLIENT - P2P Client
	 */
	WPA_IF_P2P_CLIENT,

	/**
	 * WPA_IF_P2P_GROUP - P2P Group interface (will become either
	 * WPA_IF_P2P_GO or WPA_IF_P2P_CLIENT, but the role is not yet known)
	 */
	WPA_IF_P2P_GROUP
};

struct wpa_init_params {
	const u8 *bssid;
	const char *ifname;
	const u8 *ssid;
	size_t ssid_len;
	const char *test_socket;
	int use_pae_group_addr;
	char **bridge;
	size_t num_bridge;

	u8 *own_addr; /* buffer for writing own MAC address */
};


struct wpa_bss_params {
	/** Interface name (for multi-SSID/VLAN support) */
	const char *ifname;
	/** Whether IEEE 802.1X or WPA/WPA2 is enabled */
	int enabled;

	int wpa;
	int ieee802_1x;
	int wpa_group;
	int wpa_pairwise;
	int wpa_key_mgmt;
	int rsn_preauth;
	enum mfp_options ieee80211w;
};

#define WPA_STA_AUTHORIZED BIT(0)
#define WPA_STA_WMM BIT(1)
#define WPA_STA_SHORT_PREAMBLE BIT(2)
#define WPA_STA_MFP BIT(3)

/**
 * struct p2p_params - P2P parameters for driver-based P2P management
 */
struct p2p_params {
	const char *dev_name;
	u8 pri_dev_type[8];
#define DRV_MAX_SEC_DEV_TYPES 5
	u8 sec_dev_type[DRV_MAX_SEC_DEV_TYPES][8];
	size_t num_sec_dev_types;
};

enum tdls_oper {
	TDLS_DISCOVERY_REQ,
	TDLS_SETUP,
	TDLS_TEARDOWN,
	TDLS_ENABLE_LINK,
	TDLS_DISABLE_LINK,
	TDLS_ENABLE,
	TDLS_DISABLE
};

/**
 * struct wpa_signal_info - Information about channel signal quality
 */
struct wpa_signal_info {
	u32 frequency;
	int above_threshold;
	int current_signal;
	int current_noise;
	int current_txrate;
};

/**
 * struct wpa_driver_ops - Driver interface API definition
 *
 * This structure defines the API that each driver interface needs to implement
 * for core wpa_supplicant code. All driver specific functionality is captured
 * in this wrapper.
 */
struct wpa_driver_ops {
	/** Name of the driver interface */
	const char *name;
	/** One line description of the driver interface */
	const char *desc;

	/**
	 * get_bssid - Get the current BSSID
	 * @priv: private driver interface data
	 * @bssid: buffer for BSSID (ETH_ALEN = 6 bytes)
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Query kernel driver for the current BSSID and copy it to bssid.
	 * Setting bssid to 00:00:00:00:00:00 is recommended if the STA is not
	 * associated.
	 */
	int (*get_bssid)(void *priv, u8 *bssid);

	/**
	 * get_ssid - Get the current SSID
	 * @priv: private driver interface data
	 * @ssid: buffer for SSID (at least 32 bytes)
	 *
	 * Returns: Length of the SSID on success, -1 on failure
	 *
	 * Query kernel driver for the current SSID and copy it to ssid.
	 * Returning zero is recommended if the STA is not associated.
	 *
	 * Note: SSID is an array of octets, i.e., it is not nul terminated and
	 * can, at least in theory, contain control characters (including nul)
	 * and as such, should be processed as binary data, not a printable
	 * string.
	 */
	int (*get_ssid)(void *priv, u8 *ssid);

	/**
	 * set_key - Configure encryption key
	 * @ifname: Interface name (for multi-SSID/VLAN support)
	 * @priv: private driver interface data
	 * @alg: encryption algorithm (%WPA_ALG_NONE, %WPA_ALG_WEP,
	 *	%WPA_ALG_TKIP, %WPA_ALG_CCMP, %WPA_ALG_IGTK, %WPA_ALG_PMK);
	 *	%WPA_ALG_NONE clears the key.
	 * @addr: Address of the peer STA (BSSID of the current AP when setting
	 *	pairwise key in station mode), ff:ff:ff:ff:ff:ff for
	 *	broadcast keys, %NULL for default keys that are used both for
	 *	broadcast and unicast; when clearing keys, %NULL is used to
	 *	indicate that both the broadcast-only and default key of the
	 *	specified key index is to be cleared
	 * @key_idx: key index (0..3), usually 0 for unicast keys; 0..4095 for
	 *	IGTK
	 * @set_tx: configure this key as the default Tx key (only used when
	 *	driver does not support separate unicast/individual key
	 * @seq: sequence number/packet number, seq_len octets, the next
	 *	packet number to be used for in replay protection; configured
	 *	for Rx keys (in most cases, this is only used with broadcast
	 *	keys and set to zero for unicast keys); %NULL if not set
	 * @seq_len: length of the seq, depends on the algorithm:
	 *	TKIP: 6 octets, CCMP: 6 octets, IGTK: 6 octets
	 * @key: key buffer; TKIP: 16-byte temporal key, 8-byte Tx Mic key,
	 *	8-byte Rx Mic Key
	 * @key_len: length of the key buffer in octets (WEP: 5 or 13,
	 *	TKIP: 32, CCMP: 16, IGTK: 16)
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Configure the given key for the kernel driver. If the driver
	 * supports separate individual keys (4 default keys + 1 individual),
	 * addr can be used to determine whether the key is default or
	 * individual. If only 4 keys are supported, the default key with key
	 * index 0 is used as the individual key. STA must be configured to use
	 * it as the default Tx key (set_tx is set) and accept Rx for all the
	 * key indexes. In most cases, WPA uses only key indexes 1 and 2 for
	 * broadcast keys, so key index 0 is available for this kind of
	 * configuration.
	 *
	 * Please note that TKIP keys include separate TX and RX MIC keys and
	 * some drivers may expect them in different order than wpa_supplicant
	 * is using. If the TX/RX keys are swapped, all TKIP encrypted packets
	 * will trigger Michael MIC errors. This can be fixed by changing the
	 * order of MIC keys by swapping te bytes 16..23 and 24..31 of the key
	 * in driver_*.c set_key() implementation, see driver_ndis.c for an
	 * example on how this can be done.
	 */
	int (*set_key)(const char *ifname, void *priv, enum wpa_alg alg,
		       const u8 *addr, int key_idx, int set_tx,
		       const u8 *seq, size_t seq_len,
		       const u8 *key, size_t key_len);

	/**
	 * init - Initialize driver interface
	 * @ctx: context to be used when calling wpa_supplicant functions,
	 * e.g., wpa_supplicant_event()
	 * @ifname: interface name, e.g., wlan0
	 *
	 * Returns: Pointer to private data, %NULL on failure
	 *
	 * Initialize driver interface, including event processing for kernel
	 * driver events (e.g., associated, scan results, Michael MIC failure).
	 * This function can allocate a private configuration data area for
	 * @ctx, file descriptor, interface name, etc. information that may be
	 * needed in future driver operations. If this is not used, non-NULL
	 * value will need to be returned because %NULL is used to indicate
	 * failure. The returned value will be used as 'void *priv' data for
	 * all other driver_ops functions.
	 *
	 * The main event loop (eloop.c) of wpa_supplicant can be used to
	 * register callback for read sockets (eloop_register_read_sock()).
	 *
	 * See below for more information about events and
	 * wpa_supplicant_event() function.
	 */
	void * (*init)(void *ctx, const char *ifname);

	/**
	 * deinit - Deinitialize driver interface
	 * @priv: private driver interface data from init()
	 *
	 * Shut down driver interface and processing of driver events. Free
	 * private data buffer if one was allocated in init() handler.
	 */
	void (*deinit)(void *priv);

	/**
	 * set_param - Set driver configuration parameters
	 * @priv: private driver interface data from init()
	 * @param: driver specific configuration parameters
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Optional handler for notifying driver interface about configuration
	 * parameters (driver_param).
	 */
	int (*set_param)(void *priv, const char *param);

	/**
	 * set_countermeasures - Enable/disable TKIP countermeasures
	 * @priv: private driver interface data
	 * @enabled: 1 = countermeasures enabled, 0 = disabled
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Configure TKIP countermeasures. When these are enabled, the driver
	 * should drop all received and queued frames that are using TKIP.
	 */
	int (*set_countermeasures)(void *priv, int enabled);

	/**
	 * deauthenticate - Request driver to deauthenticate
	 * @priv: private driver interface data
	 * @addr: peer address (BSSID of the AP)
	 * @reason_code: 16-bit reason code to be sent in the deauthentication
	 *	frame
	 *
	 * Returns: 0 on success, -1 on failure
	 */
	int (*deauthenticate)(void *priv, const u8 *addr, int reason_code);

	/**
	 * disassociate - Request driver to disassociate
	 * @priv: private driver interface data
	 * @addr: peer address (BSSID of the AP)
	 * @reason_code: 16-bit reason code to be sent in the disassociation
	 *	frame
	 *
	 * Returns: 0 on success, -1 on failure
	 */
	int (*disassociate)(void *priv, const u8 *addr, int reason_code);

	/**
	 * associate - Request driver to associate
	 * @priv: private driver interface data
	 * @params: association parameters
	 *
	 * Returns: 0 on success, -1 on failure
	 */
	int (*associate)(void *priv,
			 struct wpa_driver_associate_params *params);

	/**
	 * add_pmkid - Add PMKSA cache entry to the driver
	 * @priv: private driver interface data
	 * @bssid: BSSID for the PMKSA cache entry
	 * @pmkid: PMKID for the PMKSA cache entry
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is called when a new PMK is received, as a result of
	 * either normal authentication or RSN pre-authentication.
	 *
	 * If the driver generates RSN IE, i.e., it does not use wpa_ie in
	 * associate(), add_pmkid() can be used to add new PMKSA cache entries
	 * in the driver. If the driver uses wpa_ie from wpa_supplicant, this
	 * driver_ops function does not need to be implemented. Likewise, if
	 * the driver does not support WPA, this function is not needed.
	 */
	int (*add_pmkid)(void *priv, const u8 *bssid, const u8 *pmkid);

	/**
	 * remove_pmkid - Remove PMKSA cache entry to the driver
	 * @priv: private driver interface data
	 * @bssid: BSSID for the PMKSA cache entry
	 * @pmkid: PMKID for the PMKSA cache entry
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is called when the supplicant drops a PMKSA cache
	 * entry for any reason.
	 *
	 * If the driver generates RSN IE, i.e., it does not use wpa_ie in
	 * associate(), remove_pmkid() can be used to synchronize PMKSA caches
	 * between the driver and wpa_supplicant. If the driver uses wpa_ie
	 * from wpa_supplicant, this driver_ops function does not need to be
	 * implemented. Likewise, if the driver does not support WPA, this
	 * function is not needed.
	 */
	int (*remove_pmkid)(void *priv, const u8 *bssid, const u8 *pmkid);

	/**
	 * flush_pmkid - Flush PMKSA cache
	 * @priv: private driver interface data
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is called when the supplicant drops all PMKSA cache
	 * entries for any reason.
	 *
	 * If the driver generates RSN IE, i.e., it does not use wpa_ie in
	 * associate(), remove_pmkid() can be used to synchronize PMKSA caches
	 * between the driver and wpa_supplicant. If the driver uses wpa_ie
	 * from wpa_supplicant, this driver_ops function does not need to be
	 * implemented. Likewise, if the driver does not support WPA, this
	 * function is not needed.
	 */
	int (*flush_pmkid)(void *priv);

	/**
	 * get_capa - Get driver capabilities
	 * @priv: private driver interface data
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Get driver/firmware/hardware capabilities.
	 */
	int (*get_capa)(void *priv, struct wpa_driver_capa *capa);

	/**
	 * poll - Poll driver for association information
	 * @priv: private driver interface data
	 *
	 * This is an option callback that can be used when the driver does not
	 * provide event mechanism for association events. This is called when
	 * receiving WPA EAPOL-Key messages that require association
	 * information. The driver interface is supposed to generate associnfo
	 * event before returning from this callback function. In addition, the
	 * driver interface should generate an association event after having
	 * sent out associnfo.
	 */
	void (*poll)(void *priv);

	/**
	 * get_ifname - Get interface name
	 * @priv: private driver interface data
	 *
	 * Returns: Pointer to the interface name. This can differ from the
	 * interface name used in init() call. Init() is called first.
	 *
	 * This optional function can be used to allow the driver interface to
	 * replace the interface name with something else, e.g., based on an
	 * interface mapping from a more descriptive name.
	 */
	const char * (*get_ifname)(void *priv);

	/**
	 * get_mac_addr - Get own MAC address
	 * @priv: private driver interface data
	 *
	 * Returns: Pointer to own MAC address or %NULL on failure
	 *
	 * This optional function can be used to get the own MAC address of the
	 * device from the driver interface code. This is only needed if the
	 * l2_packet implementation for the OS does not provide easy access to
	 * a MAC address. */
	const u8 * (*get_mac_addr)(void *priv);

	/**
	 * send_eapol - Optional function for sending EAPOL packets
	 * @priv: private driver interface data
	 * @dest: Destination MAC address
	 * @proto: Ethertype
	 * @data: EAPOL packet starting with IEEE 802.1X header
	 * @data_len: Size of the EAPOL packet
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * This optional function can be used to override l2_packet operations
	 * with driver specific functionality. If this function pointer is set,
	 * l2_packet module is not used at all and the driver interface code is
	 * responsible for receiving and sending all EAPOL packets. The
	 * received EAPOL packets are sent to core code with EVENT_EAPOL_RX
	 * event. The driver interface is required to implement get_mac_addr()
	 * handler if send_eapol() is used.
	 */
	int (*send_eapol)(void *priv, const u8 *dest, u16 proto,
			  const u8 *data, size_t data_len);

	/**
	 * set_operstate - Sets device operating state to DORMANT or UP
	 * @priv: private driver interface data
	 * @state: 0 = dormant, 1 = up
	 * Returns: 0 on success, -1 on failure
	 *
	 * This is an optional function that can be used on operating systems
	 * that support a concept of controlling network device state from user
	 * space applications. This function, if set, gets called with
	 * state = 1 when authentication has been completed and with state = 0
	 * when connection is lost.
	 */
	int (*set_operstate)(void *priv, int state);

	/**
	 * mlme_setprotection - MLME-SETPROTECTION.request primitive
	 * @priv: Private driver interface data
	 * @addr: Address of the station for which to set protection (may be
	 * %NULL for group keys)
	 * @protect_type: MLME_SETPROTECTION_PROTECT_TYPE_*
	 * @key_type: MLME_SETPROTECTION_KEY_TYPE_*
	 * Returns: 0 on success, -1 on failure
	 *
	 * This is an optional function that can be used to set the driver to
	 * require protection for Tx and/or Rx frames. This uses the layer
	 * interface defined in IEEE 802.11i-2004 clause 10.3.22.1
	 * (MLME-SETPROTECTION.request). Many drivers do not use explicit
	 * set protection operation; instead, they set protection implicitly
	 * based on configured keys.
	 */
	int (*mlme_setprotection)(void *priv, const u8 *addr, int protect_type,
				  int key_type);

	/**
	 * get_hw_feature_data - Get hardware support data (channels and rates)
	 * @priv: Private driver interface data
	 * @num_modes: Variable for returning the number of returned modes
	 * flags: Variable for returning hardware feature flags
	 * Returns: Pointer to allocated hardware data on success or %NULL on
	 * failure. Caller is responsible for freeing this.
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to %wpa_supplicant or hostapd.
	 */
	struct hostapd_hw_modes * (*get_hw_feature_data)(void *priv,
							 u16 *num_modes,
							 u16 *flags);

	/**
	 * set_channel - Set channel
	 * @priv: Private driver interface data
	 * @phymode: HOSTAPD_MODE_IEEE80211B, ..
	 * @chan: IEEE 802.11 channel number
	 * @freq: Frequency of the channel in MHz
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant.
	 */
	int (*set_channel)(void *priv, enum hostapd_hw_mode phymode, int chan,
			   int freq);

	/**
	 * set_ssid - Set SSID
	 * @priv: Private driver interface data
	 * @ssid: SSID
	 * @ssid_len: SSID length
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant.
	 */
	int (*set_ssid)(void *priv, const u8 *ssid, size_t ssid_len);

	/**
	 * set_bssid - Set BSSID
	 * @priv: Private driver interface data
	 * @bssid: BSSID
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant.
	 */
	int (*set_bssid)(void *priv, const u8 *bssid);

	/**
	 * send_mlme - Send management frame from MLME
	 * @priv: Private driver interface data
	 * @data: IEEE 802.11 management frame with IEEE 802.11 header
	 * @data_len: Size of the management frame
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant.
	 */
	int (*send_mlme)(void *priv, const u8 *data, size_t data_len);

	/**
	 * mlme_add_sta - Add a STA entry into the driver/netstack
	 * @priv: Private driver interface data
	 * @addr: MAC address of the STA (e.g., BSSID of the AP)
	 * @supp_rates: Supported rate set (from (Re)AssocResp); in IEEE 802.11
	 * format (one octet per rate, 1 = 0.5 Mbps)
	 * @supp_rates_len: Number of entries in supp_rates
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant. When the MLME code
	 * completes association with an AP, this function is called to
	 * configure the driver/netstack with a STA entry for data frame
	 * processing (TX rate control, encryption/decryption).
	 */
	int (*mlme_add_sta)(void *priv, const u8 *addr, const u8 *supp_rates,
			    size_t supp_rates_len);

	/**
	 * mlme_remove_sta - Remove a STA entry from the driver/netstack
	 * @priv: Private driver interface data
	 * @addr: MAC address of the STA (e.g., BSSID of the AP)
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant.
	 */
	int (*mlme_remove_sta)(void *priv, const u8 *addr);

	/**
	 * update_ft_ies - Update FT (IEEE 802.11r) IEs
	 * @priv: Private driver interface data
	 * @md: Mobility domain (2 octets) (also included inside ies)
	 * @ies: FT IEs (MDIE, FTIE, ...) or %NULL to remove IEs
	 * @ies_len: Length of FT IEs in bytes
	 * Returns: 0 on success, -1 on failure
	 *
	 * The supplicant uses this callback to let the driver know that keying
	 * material for FT is available and that the driver can use the
	 * provided IEs in the next message in FT authentication sequence.
	 *
	 * This function is only needed for driver that support IEEE 802.11r
	 * (Fast BSS Transition).
	 */
	int (*update_ft_ies)(void *priv, const u8 *md, const u8 *ies,
			     size_t ies_len);

	/**
	 * send_ft_action - Send FT Action frame (IEEE 802.11r)
	 * @priv: Private driver interface data
	 * @action: Action field value
	 * @target_ap: Target AP address
	 * @ies: FT IEs (MDIE, FTIE, ...) (FT Request action frame body)
	 * @ies_len: Length of FT IEs in bytes
	 * Returns: 0 on success, -1 on failure
	 *
	 * The supplicant uses this callback to request the driver to transmit
	 * an FT Action frame (action category 6) for over-the-DS fast BSS
	 * transition.
	 */
	int (*send_ft_action)(void *priv, u8 action, const u8 *target_ap,
			      const u8 *ies, size_t ies_len);

	/**
	 * get_scan_results2 - Fetch the latest scan results
	 * @priv: private driver interface data
	 *
	 * Returns: Allocated buffer of scan results (caller is responsible for
	 * freeing the data structure) on success, NULL on failure
	 */
	 struct wpa_scan_results * (*get_scan_results2)(void *priv);

	/**
	 * set_country - Set country
	 * @priv: Private driver interface data
	 * @alpha2: country to which to switch to
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is for drivers which support some form
	 * of setting a regulatory domain.
	 */
	int (*set_country)(void *priv, const char *alpha2);

	/**
	 * global_init - Global driver initialization
	 * Returns: Pointer to private data (global), %NULL on failure
	 *
	 * This optional function is called to initialize the driver wrapper
	 * for global data, i.e., data that applies to all interfaces. If this
	 * function is implemented, global_deinit() will also need to be
	 * implemented to free the private data. The driver will also likely
	 * use init2() function instead of init() to get the pointer to global
	 * data available to per-interface initializer.
	 */
	void * (*global_init)(void);

	/**
	 * global_deinit - Global driver deinitialization
	 * @priv: private driver global data from global_init()
	 *
	 * Terminate any global driver related functionality and free the
	 * global data structure.
	 */
	void (*global_deinit)(void *priv);

	/**
	 * init2 - Initialize driver interface (with global data)
	 * @ctx: context to be used when calling wpa_supplicant functions,
	 * e.g., wpa_supplicant_event()
	 * @ifname: interface name, e.g., wlan0
	 * @global_priv: private driver global data from global_init()
	 * Returns: Pointer to private data, %NULL on failure
	 *
	 * This function can be used instead of init() if the driver wrapper
	 * uses global data.
	 */
	void * (*init2)(void *ctx, const char *ifname, void *global_priv);

	/**
	 * get_interfaces - Get information about available interfaces
	 * @global_priv: private driver global data from global_init()
	 * Returns: Allocated buffer of interface information (caller is
	 * responsible for freeing the data structure) on success, NULL on
	 * failure
	 */
	struct wpa_interface_info * (*get_interfaces)(void *global_priv);

	/**
	 * scan2 - Request the driver to initiate scan
	 * @priv: private driver interface data
	 * @params: Scan parameters
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Once the scan results are ready, the driver should report scan
	 * results event for wpa_supplicant which will eventually request the
	 * results with wpa_driver_get_scan_results2().
	 */
	int (*scan2)(void *priv, struct wpa_driver_scan_params *params);

	/**
	 * authenticate - Request driver to authenticate
	 * @priv: private driver interface data
	 * @params: authentication parameters
	 * Returns: 0 on success, -1 on failure
	 *
	 * This is an optional function that can be used with drivers that
	 * support separate authentication and association steps, i.e., when
	 * wpa_supplicant can act as the SME. If not implemented, associate()
	 * function is expected to take care of IEEE 802.11 authentication,
	 * too.
	 */
	int (*authenticate)(void *priv,
			    struct wpa_driver_auth_params *params);

	/**
	 * set_beacon - Set Beacon frame template
	 * @priv: Private driver interface data
	 * @head: Beacon head from IEEE 802.11 header to IEs before TIM IE
	 * @head_len: Length of the head buffer in octets
	 * @tail: Beacon tail following TIM IE
	 * @tail_len: Length of the tail buffer in octets
	 * @dtim_period: DTIM period
	 * @beacon_int: Beacon interval
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is used to configure Beacon template for the driver in
	 * AP mode. The driver is responsible for building the full Beacon
	 * frame by concatenating the head part with TIM IE generated by the
	 * driver/firmware and finishing with the tail part.
	 */
	int (*set_beacon)(void *priv, const u8 *head, size_t head_len,
			  const u8 *tail, size_t tail_len, int dtim_period,
			  int beacon_int);

	/**
	 * hapd_init - Initialize driver interface (hostapd only)
	 * @hapd: Pointer to hostapd context
	 * @params: Configuration for the driver wrapper
	 * Returns: Pointer to private data, %NULL on failure
	 *
	 * This function is used instead of init() or init2() when the driver
	 * wrapper is used withh hostapd.
	 */
	void * (*hapd_init)(struct hostapd_data *hapd,
			    struct wpa_init_params *params);

	/**
	 * hapd_deinit - Deinitialize driver interface (hostapd only)
	 * @priv: Private driver interface data from hapd_init()
	 */
	void (*hapd_deinit)(void *priv);

	/**
	 * set_ieee8021x - Enable/disable IEEE 802.1X support (AP only)
	 * @priv: Private driver interface data
	 * @params: BSS parameters
	 * Returns: 0 on success, -1 on failure
	 *
	 * This is an optional function to configure the kernel driver to
	 * enable/disable IEEE 802.1X support and set WPA/WPA2 parameters. This
	 * can be left undefined (set to %NULL) if IEEE 802.1X support is
	 * always enabled and the driver uses set_beacon() to set WPA/RSN IE
	 * for Beacon frames.
	 */
	int (*set_ieee8021x)(void *priv, struct wpa_bss_params *params);

	/**
	 * set_privacy - Enable/disable privacy (AP only)
	 * @priv: Private driver interface data
	 * @enabled: 1 = privacy enabled, 0 = disabled
	 * Returns: 0 on success, -1 on failure
	 *
	 * This is an optional function to configure privacy field in the
	 * kernel driver for Beacon frames. This can be left undefined (set to
	 * %NULL) if the driver uses the Beacon template from set_beacon().
	 */
	int (*set_privacy)(void *priv, int enabled);

	/**
	 * get_seqnum - Fetch the current TSC/packet number (AP only)
	 * @ifname: The interface name (main or virtual)
	 * @priv: Private driver interface data
	 * @addr: MAC address of the station or %NULL for group keys
	 * @idx: Key index
	 * @seq: Buffer for returning the latest used TSC/packet number
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is used to fetch the last used TSC/packet number for
	 * a TKIP, CCMP, or BIP/IGTK key. It is mainly used with group keys, so
	 * there is no strict requirement on implementing support for unicast
	 * keys (i.e., addr != %NULL).
	 */
	int (*get_seqnum)(const char *ifname, void *priv, const u8 *addr,
			  int idx, u8 *seq);

	/**
	 * flush - Flush all association stations (AP only)
	 * @priv: Private driver interface data
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function requests the driver to disassociate all associated
	 * stations. This function does not need to be implemented if the
	 * driver does not process association frames internally.
	 */
	int (*flush)(void *priv);

	/**
	 * set_generic_elem - Add IEs into Beacon/Probe Response frames (AP)
	 * @priv: Private driver interface data
	 * @elem: Information elements
	 * @elem_len: Length of the elem buffer in octets
	 * Returns: 0 on success, -1 on failure
	 *
	 * This is an optional function to add information elements in the
	 * kernel driver for Beacon and Probe Response frames. This can be left
	 * undefined (set to %NULL) if the driver uses the Beacon template from
	 * set_beacon().
	 */
	int (*set_generic_elem)(void *priv, const u8 *elem, size_t elem_len);

	/**
	 * read_sta_data - Fetch station data (AP only)
	 * @priv: Private driver interface data
	 * @data: Buffer for returning station information
	 * @addr: MAC address of the station
	 * Returns: 0 on success, -1 on failure
	 */
	int (*read_sta_data)(void *priv, struct hostap_sta_driver_data *data,
			     const u8 *addr);

	/**
	 * hapd_send_eapol - Send an EAPOL packet (AP only)
	 * @priv: private driver interface data
	 * @addr: Destination MAC address
	 * @data: EAPOL packet starting with IEEE 802.1X header
	 * @data_len: Length of the EAPOL packet in octets
	 * @encrypt: Whether the frame should be encrypted
	 * @own_addr: Source MAC address
	 * @flags: WPA_STA_* flags for the destination station
	 *
	 * Returns: 0 on success, -1 on failure
	 */
	int (*hapd_send_eapol)(void *priv, const u8 *addr, const u8 *data,
			       size_t data_len, int encrypt,
			       const u8 *own_addr, u32 flags);

	/**
	 * sta_deauth - Deauthenticate a station (AP only)
	 * @priv: Private driver interface data
	 * @own_addr: Source address and BSSID for the Deauthentication frame
	 * @addr: MAC address of the station to deauthenticate
	 * @reason: Reason code for the Deauthentiation frame
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function requests a specific station to be deauthenticated and
	 * a Deauthentication frame to be sent to it.
	 */
	int (*sta_deauth)(void *priv, const u8 *own_addr, const u8 *addr,
			  int reason);

	/**
	 * sta_disassoc - Disassociate a station (AP only)
	 * @priv: Private driver interface data
	 * @own_addr: Source address and BSSID for the Disassociation frame
	 * @addr: MAC address of the station to disassociate
	 * @reason: Reason code for the Disassociation frame
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function requests a specific station to be disassociated and
	 * a Disassociation frame to be sent to it.
	 */
	int (*sta_disassoc)(void *priv, const u8 *own_addr, const u8 *addr,
			    int reason);

	/**
	 * sta_remove - Remove a station entry (AP only)
	 * @priv: Private driver interface data
	 * @addr: MAC address of the station to be removed
	 * Returns: 0 on success, -1 on failure
	 */
	int (*sta_remove)(void *priv, const u8 *addr);

	/**
	 * hapd_get_ssid - Get the current SSID (AP only)
	 * @priv: Private driver interface data
	 * @buf: Buffer for returning the SSID
	 * @len: Maximum length of the buffer
	 * Returns: Length of the SSID on success, -1 on failure
	 *
	 * This function need not be implemented if the driver uses Beacon
	 * template from set_beacon() and does not reply to Probe Request
	 * frames.
	 */
	int (*hapd_get_ssid)(void *priv, u8 *buf, int len);

	/**
	 * hapd_set_ssid - Set SSID (AP only)
	 * @priv: Private driver interface data
	 * @buf: SSID
	 * @len: Length of the SSID in octets
	 * Returns: 0 on success, -1 on failure
	 */
	int (*hapd_set_ssid)(void *priv, const u8 *buf, int len);

	/**
	 * hapd_set_countermeasures - Enable/disable TKIP countermeasures (AP)
	 * @priv: Private driver interface data
	 * @enabled: 1 = countermeasures enabled, 0 = disabled
	 * Returns: 0 on success, -1 on failure
	 *
	 * This need not be implemented if the driver does not take care of
	 * association processing.
	 */
	int (*hapd_set_countermeasures)(void *priv, int enabled);

	/**
	 * sta_add - Add a station entry
	 * @priv: Private driver interface data
	 * @params: Station parameters
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is used to add a station entry to the driver once the
	 * station has completed association. This is only used if the driver
	 * does not take care of association processing.
	 */
	int (*sta_add)(void *priv, struct hostapd_sta_add_params *params);

	/**
	 * get_inact_sec - Get station inactivity duration (AP only)
	 * @priv: Private driver interface data
	 * @addr: Station address
	 * Returns: Number of seconds station has been inactive, -1 on failure
	 */
	int (*get_inact_sec)(void *priv, const u8 *addr);

	/**
	 * sta_clear_stats - Clear station statistics (AP only)
	 * @priv: Private driver interface data
	 * @addr: Station address
	 * Returns: 0 on success, -1 on failure
	 */
	int (*sta_clear_stats)(void *priv, const u8 *addr);

	/**
	 * set_freq - Set channel/frequency (AP only)
	 * @priv: Private driver interface data
	 * @freq: Channel parameters
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_freq)(void *priv, struct hostapd_freq_params *freq);

	/**
	 * set_rts - Set RTS threshold
	 * @priv: Private driver interface data
	 * @rts: RTS threshold in octets
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_rts)(void *priv, int rts);

	/**
	 * set_frag - Set fragmentation threshold
	 * @priv: Private driver interface data
	 * @frag: Fragmentation threshold in octets
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_frag)(void *priv, int frag);

	/**
	 * sta_set_flags - Set station flags (AP only)
	 * @priv: Private driver interface data
	 * @addr: Station address
	 * @total_flags: Bitmap of all WPA_STA_* flags currently set
	 * @flags_or: Bitmap of WPA_STA_* flags to add
	 * @flags_and: Bitmap of WPA_STA_* flags to us as a mask
	 * Returns: 0 on success, -1 on failure
	 */
	int (*sta_set_flags)(void *priv, const u8 *addr,
			     int total_flags, int flags_or, int flags_and);

	/**
	 * set_rate_sets - Set supported and basic rate sets (AP only)
	 * @priv: Private driver interface data
	 * @supp_rates: -1 terminated array of supported rates in 100 kbps
	 * @basic_rates: -1 terminated array of basic rates in 100 kbps
	 * @mode: hardware mode (HOSTAPD_MODE_*)
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_rate_sets)(void *priv, int *supp_rates, int *basic_rates,
			     int mode);

	/**
	 * set_cts_protect - Set CTS protection mode (AP only)
	 * @priv: Private driver interface data
	 * @value: Whether CTS protection is enabled
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_cts_protect)(void *priv, int value);

	/**
	 * set_preamble - Set preamble mode (AP only)
	 * @priv: Private driver interface data
	 * @value: Whether short preamble is enabled
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_preamble)(void *priv, int value);

	/**
	 * set_short_slot_time - Set short slot time (AP only)
	 * @priv: Private driver interface data
	 * @value: Whether short slot time is enabled
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_short_slot_time)(void *priv, int value);

	/**
	 * set_tx_queue_params - Set TX queue parameters
	 * @priv: Private driver interface data
	 * @queue: Queue number (0 = VO, 1 = VI, 2 = BE, 3 = BK)
	 * @aifs: AIFS
	 * @cw_min: cwMin
	 * @cw_max: cwMax
	 * @burst_time: Maximum length for bursting in 0.1 msec units
	 */
	int (*set_tx_queue_params)(void *priv, int queue, int aifs, int cw_min,
				   int cw_max, int burst_time);

	/**
	 * valid_bss_mask - Validate BSSID mask
	 * @priv: Private driver interface data
	 * @addr: Address
	 * @mask: Mask
	 * Returns: 0 if mask is valid, -1 if mask is not valid, 1 if mask can
	 * be used, but the main interface address must be the first address in
	 * the block if mask is applied
	 */
	int (*valid_bss_mask)(void *priv, const u8 *addr, const u8 *mask);

	/**
	 * if_add - Add a virtual interface
	 * @priv: Private driver interface data
	 * @type: Interface type
	 * @ifname: Interface name for the new virtual interface
	 * @addr: Local address to use for the interface or %NULL to use the
	 *	parent interface address
	 * @bss_ctx: BSS context for %WPA_IF_AP_BSS interfaces
	 * @drv_priv: Pointer for overwriting the driver context or %NULL if
	 *	not allowed (applies only to %WPA_IF_AP_BSS type)
	 * @force_ifname: Buffer for returning an interface name that the
	 *	driver ended up using if it differs from the requested ifname
	 * @if_addr: Buffer for returning the allocated interface address
	 *	(this may differ from the requested addr if the driver cannot
	 *	change interface address)
	 * @bridge: Bridge interface to use or %NULL if no bridge configured
	 * Returns: 0 on success, -1 on failure
	 */
	int (*if_add)(void *priv, enum wpa_driver_if_type type,
		      const char *ifname, const u8 *addr, void *bss_ctx,
		      void **drv_priv, char *force_ifname, u8 *if_addr,
		      const char *bridge);

	/**
	 * if_remove - Remove a virtual interface
	 * @priv: Private driver interface data
	 * @type: Interface type
	 * @ifname: Interface name of the virtual interface to be removed
	 * Returns: 0 on success, -1 on failure
	 */
	int (*if_remove)(void *priv, enum wpa_driver_if_type type,
			 const char *ifname);

	/**
	 * set_sta_vlan - Bind a station into a specific interface (AP only)
	 * @priv: Private driver interface data
	 * @ifname: Interface (main or virtual BSS or VLAN)
	 * @addr: MAC address of the associated station
	 * @vlan_id: VLAN ID
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is used to bind a station to a specific virtual
	 * interface. It is only used if when virtual interfaces are supported,
	 * e.g., to assign stations to different VLAN interfaces based on
	 * information from a RADIUS server. This allows separate broadcast
	 * domains to be used with a single BSS.
	 */
	int (*set_sta_vlan)(void *priv, const u8 *addr, const char *ifname,
			    int vlan_id);

	/**
	 * commit - Optional commit changes handler (AP only)
	 * @priv: driver private data
	 * Returns: 0 on success, -1 on failure
	 *
	 * This optional handler function can be registered if the driver
	 * interface implementation needs to commit changes (e.g., by setting
	 * network interface up) at the end of initial configuration. If set,
	 * this handler will be called after initial setup has been completed.
	 */
	int (*commit)(void *priv);

	/**
	 * send_ether - Send an ethernet packet (AP only)
	 * @priv: private driver interface data
	 * @dst: Destination MAC address
	 * @src: Source MAC address
	 * @proto: Ethertype
	 * @data: EAPOL packet starting with IEEE 802.1X header
	 * @data_len: Length of the EAPOL packet in octets
	 * Returns: 0 on success, -1 on failure
	 */
	int (*send_ether)(void *priv, const u8 *dst, const u8 *src, u16 proto,
			  const u8 *data, size_t data_len);

	/**
	 * set_radius_acl_auth - Notification of RADIUS ACL change
	 * @priv: Private driver interface data
	 * @mac: MAC address of the station
	 * @accepted: Whether the station was accepted
	 * @session_timeout: Session timeout for the station
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_radius_acl_auth)(void *priv, const u8 *mac, int accepted, 
				   u32 session_timeout);

	/**
	 * set_radius_acl_expire - Notification of RADIUS ACL expiration
	 * @priv: Private driver interface data
	 * @mac: MAC address of the station
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_radius_acl_expire)(void *priv, const u8 *mac);

	/**
	 * set_ht_params - Set HT parameters (AP only)
	 * @priv: Private driver interface data
	 * @ht_capab: HT Capabilities IE
	 * @ht_capab_len: Length of ht_capab in octets
	 * @ht_oper: HT Operation IE
	 * @ht_oper_len: Length of ht_oper in octets
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_ht_params)(void *priv,
			     const u8 *ht_capab, size_t ht_capab_len,
			     const u8 *ht_oper, size_t ht_oper_len);

	/**
	 * set_ap_wps_ie - Add WPS IE(s) into Beacon/Probe Response frames (AP)
	 * @priv: Private driver interface data
	 * @beacon: WPS IE(s) for Beacon frames or %NULL to remove extra IE(s)
	 * @proberesp: WPS IE(s) for Probe Response frames or %NULL to remove
	 *	extra IE(s)
	 * @assocresp: WPS IE(s) for (Re)Association Response frames or %NULL
	 *	to remove extra IE(s)
	 * Returns: 0 on success, -1 on failure
	 *
	 * This is an optional function to add WPS IE in the kernel driver for
	 * Beacon and Probe Response frames. This can be left undefined (set
	 * to %NULL) if the driver uses the Beacon template from set_beacon()
	 * and does not process Probe Request frames. If the driver takes care
	 * of (Re)Association frame processing, the assocresp buffer includes
	 * WPS IE(s) that need to be added to (Re)Association Response frames
	 * whenever a (Re)Association Request frame indicated use of WPS.
	 *
	 * This will also be used to add P2P IE(s) into Beacon/Probe Response
	 * frames when operating as a GO. The driver is responsible for adding
	 * timing related attributes (e.g., NoA) in addition to the IEs
	 * included here by appending them after these buffers. This call is
	 * also used to provide Probe Response IEs for P2P Listen state
	 * operations for drivers that generate the Probe Response frames
	 * internally.
	 */
	int (*set_ap_wps_ie)(void *priv, const struct wpabuf *beacon,
			     const struct wpabuf *proberesp,
			     const struct wpabuf *assocresp);

	/**
	 * set_supp_port - Set IEEE 802.1X Supplicant Port status
	 * @priv: Private driver interface data
	 * @authorized: Whether the port is authorized
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_supp_port)(void *priv, int authorized);

	/**
	 * set_wds_sta - Bind a station into a 4-address WDS (AP only)
	 * @priv: Private driver interface data
	 * @addr: MAC address of the associated station
	 * @aid: Association ID
	 * @val: 1 = bind to 4-address WDS; 0 = unbind
	 * @bridge_ifname: Bridge interface to use for the WDS station or %NULL
	 *	to indicate that bridge is not to be used
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_wds_sta)(void *priv, const u8 *addr, int aid, int val,
	                   const char *bridge_ifname);

	/**
	 * send_action - Transmit an Action frame
	 * @priv: Private driver interface data
	 * @freq: Frequency (in MHz) of the channel
	 * @wait: Time to wait off-channel for a response (in ms), or zero
	 * @dst: Destination MAC address (Address 1)
	 * @src: Source MAC address (Address 2)
	 * @bssid: BSSID (Address 3)
	 * @data: Frame body
	 * @data_len: data length in octets
	 * Returns: 0 on success, -1 on failure
	 *
	 * This command can be used to request the driver to transmit an action
	 * frame to the specified destination.
	 *
	 * If the %WPA_DRIVER_FLAGS_OFFCHANNEL_TX flag is set, the frame will
	 * be transmitted on the given channel and the device will wait for a
	 * response on that channel for the given wait time.
	 *
	 * If the flag is not set, the wait time will be ignored. In this case,
	 * if a remain-on-channel duration is in progress, the frame must be
	 * transmitted on that channel; alternatively the frame may be sent on
	 * the current operational channel (if in associated state in station
	 * mode or while operating as an AP.)
	 */
	int (*send_action)(void *priv, unsigned int freq, unsigned int wait,
			   const u8 *dst, const u8 *src, const u8 *bssid,
			   const u8 *data, size_t data_len);

	/**
	 * send_action_cancel_wait - Cancel action frame TX wait
	 * @priv: Private driver interface data
	 *
	 * This command cancels the wait time associated with sending an action
	 * frame. It is only available when %WPA_DRIVER_FLAGS_OFFCHANNEL_TX is
	 * set in the driver flags.
	 */
	void (*send_action_cancel_wait)(void *priv);

	/**
	 * remain_on_channel - Remain awake on a channel
	 * @priv: Private driver interface data
	 * @freq: Frequency (in MHz) of the channel
	 * @duration: Duration in milliseconds
	 * Returns: 0 on success, -1 on failure
	 *
	 * This command is used to request the driver to remain awake on the
	 * specified channel for the specified duration and report received
	 * Action frames with EVENT_RX_ACTION events. Optionally, received
	 * Probe Request frames may also be requested to be reported by calling
	 * probe_req_report(). These will be reported with EVENT_RX_PROBE_REQ.
	 *
	 * The driver may not be at the requested channel when this function
	 * returns, i.e., the return code is only indicating whether the
	 * request was accepted. The caller will need to wait until the
	 * EVENT_REMAIN_ON_CHANNEL event indicates that the driver has
	 * completed the channel change. This may take some time due to other
	 * need for the radio and the caller should be prepared to timing out
	 * its wait since there are no guarantees on when this request can be
	 * executed.
	 */
	int (*remain_on_channel)(void *priv, unsigned int freq,
				 unsigned int duration);

	/**
	 * cancel_remain_on_channel - Cancel remain-on-channel operation
	 * @priv: Private driver interface data
	 *
	 * This command can be used to cancel a remain-on-channel operation
	 * before its originally requested duration has passed. This could be
	 * used, e.g., when remain_on_channel() is used to request extra time
	 * to receive a response to an Action frame and the response is
	 * received when there is still unneeded time remaining on the
	 * remain-on-channel operation.
	 */
	int (*cancel_remain_on_channel)(void *priv);

	/**
	 * probe_req_report - Request Probe Request frames to be indicated
	 * @priv: Private driver interface data
	 * @report: Whether to report received Probe Request frames
	 * Returns: 0 on success, -1 on failure (or if not supported)
	 *
	 * This command can be used to request the driver to indicate when
	 * Probe Request frames are received with EVENT_RX_PROBE_REQ events.
	 * Since this operation may require extra resources, e.g., due to less
	 * optimal hardware/firmware RX filtering, many drivers may disable
	 * Probe Request reporting at least in station mode. This command is
	 * used to notify the driver when the Probe Request frames need to be
	 * reported, e.g., during remain-on-channel operations.
	 */
	int (*probe_req_report)(void *priv, int report);

	/**
	 * disable_11b_rates - Set whether IEEE 802.11b rates are used for TX
	 * @priv: Private driver interface data
	 * @disabled: Whether IEEE 802.11b rates are disabled
	 * Returns: 0 on success, -1 on failure (or if not supported)
	 *
	 * This command is used to disable IEEE 802.11b rates (1, 2, 5.5, and
	 * 11 Mbps) as TX rates for data and management frames. This can be
	 * used to optimize channel use when there is no need to support IEEE
	 * 802.11b-only devices.
	 */
	int (*disable_11b_rates)(void *priv, int disabled);

	/**
	 * deinit_ap - Deinitialize AP mode
	 * @priv: Private driver interface data
	 * Returns: 0 on success, -1 on failure (or if not supported)
	 *
	 * This optional function can be used to disable AP mode related
	 * configuration and change the driver mode to station mode to allow
	 * normal station operations like scanning to be completed.
	 */
	int (*deinit_ap)(void *priv);

	/**
	 * suspend - Notification on system suspend/hibernate event
	 * @priv: Private driver interface data
	 */
	void (*suspend)(void *priv);

	/**
	 * resume - Notification on system resume/thaw event
	 * @priv: Private driver interface data
	 */
	void (*resume)(void *priv);

	/**
	 * signal_monitor - Set signal monitoring parameters
	 * @priv: Private driver interface data
	 * @threshold: Threshold value for signal change events; 0 = disabled
	 * @hysteresis: Minimum change in signal strength before indicating a
	 *	new event
	 * Returns: 0 on success, -1 on failure (or if not supported)
	 *
	 * This function can be used to configure monitoring of signal strength
	 * with the current AP. Whenever signal strength drops below the
	 * %threshold value or increases above it, EVENT_SIGNAL_CHANGE event
	 * should be generated assuming the signal strength has changed at
	 * least %hysteresis from the previously indicated signal change event.
	 */
	int (*signal_monitor)(void *priv, int threshold, int hysteresis);

	/**
	 * send_frame - Send IEEE 802.11 frame (testing use only)
	 * @priv: Private driver interface data
	 * @data: IEEE 802.11 frame with IEEE 802.11 header
	 * @data_len: Size of the frame
	 * @encrypt: Whether to encrypt the frame (if keys are set)
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only used for debugging purposes and is not
	 * required to be implemented for normal operations.
	 */
	int (*send_frame)(void *priv, const u8 *data, size_t data_len,
			  int encrypt);

	/**
	 * shared_freq - Get operating frequency of shared interface(s)
	 * @priv: Private driver interface data
	 * Returns: Operating frequency in MHz, 0 if no shared operation in
	 * use, or -1 on failure
	 *
	 * This command can be used to request the current operating frequency
	 * of any virtual interface that shares the same radio to provide
	 * information for channel selection for other virtual interfaces.
	 */
	int (*shared_freq)(void *priv);

	/**
	 * get_noa - Get current Notice of Absence attribute payload
	 * @priv: Private driver interface data
	 * @buf: Buffer for returning NoA
	 * @buf_len: Buffer length in octets
	 * Returns: Number of octets used in buf, 0 to indicate no NoA is being
	 * advertized, or -1 on failure
	 *
	 * This function is used to fetch the current Notice of Absence
	 * attribute value from GO.
	 */
	int (*get_noa)(void *priv, u8 *buf, size_t buf_len);

	/**
	 * set_noa - Set Notice of Absence parameters for GO (testing)
	 * @priv: Private driver interface data
	 * @count: Count
	 * @start: Start time in ms from next TBTT
	 * @duration: Duration in ms
	 * Returns: 0 on success or -1 on failure
	 *
	 * This function is used to set Notice of Absence parameters for GO. It
	 * is used only for testing. To disable NoA, all parameters are set to
	 * 0.
	 */
	int (*set_noa)(void *priv, u8 count, int start, int duration);

	/**
	 * set_p2p_powersave - Set P2P power save options
	 * @priv: Private driver interface data
	 * @legacy_ps: 0 = disable, 1 = enable, 2 = maximum PS, -1 = no change
	 * @opp_ps: 0 = disable, 1 = enable, -1 = no change
	 * @ctwindow: 0.. = change (msec), -1 = no change
	 * Returns: 0 on success or -1 on failure
	 */
	int (*set_p2p_powersave)(void *priv, int legacy_ps, int opp_ps,
				 int ctwindow);

	/**
	 * ampdu - Enable/disable aggregation
	 * @priv: Private driver interface data
	 * @ampdu: 1/0 = enable/disable A-MPDU aggregation
	 * Returns: 0 on success or -1 on failure
	 */
	int (*ampdu)(void *priv, int ampdu);

	/**
	 * set_intra_bss - Enables/Disables intra BSS bridging
	 */
	int (*set_intra_bss)(void *priv, int enabled);

	/**
	 * get_radio_name - Get physical radio name for the device
	 * @priv: Private driver interface data
	 * Returns: Radio name or %NULL if not known
	 *
	 * The returned data must not be modified by the caller. It is assumed
	 * that any interface that has the same radio name as another is
	 * sharing the same physical radio. This information can be used to
	 * share scan results etc. information between the virtual interfaces
	 * to speed up various operations.
	 */
	const char * (*get_radio_name)(void *priv);

	/**
	 * p2p_find - Start P2P Device Discovery
	 * @priv: Private driver interface data
	 * @timeout: Timeout for find operation in seconds or 0 for no timeout
	 * @type: Device Discovery type (enum p2p_discovery_type)
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	int (*p2p_find)(void *priv, unsigned int timeout, int type);

	/**
	 * p2p_stop_find - Stop P2P Device Discovery
	 * @priv: Private driver interface data
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	int (*p2p_stop_find)(void *priv);

	/**
	 * p2p_listen - Start P2P Listen state for specified duration
	 * @priv: Private driver interface data
	 * @timeout: Listen state duration in milliseconds
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function can be used to request the P2P module to keep the
	 * device discoverable on the listen channel for an extended set of
	 * time. At least in its current form, this is mainly used for testing
	 * purposes and may not be of much use for normal P2P operations.
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	int (*p2p_listen)(void *priv, unsigned int timeout);

	/**
	 * p2p_connect - Start P2P group formation (GO negotiation)
	 * @priv: Private driver interface data
	 * @peer_addr: MAC address of the peer P2P client
	 * @wps_method: enum p2p_wps_method value indicating config method
	 * @go_intent: Local GO intent value (1..15)
	 * @own_interface_addr: Intended interface address to use with the
	 *	group
	 * @force_freq: The only allowed channel frequency in MHz or 0
	 * @persistent_group: Whether to create persistent group
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	int (*p2p_connect)(void *priv, const u8 *peer_addr, int wps_method,
			   int go_intent, const u8 *own_interface_addr,
			   unsigned int force_freq, int persistent_group);

	/**
	 * wps_success_cb - Report successfully completed WPS provisioning
	 * @priv: Private driver interface data
	 * @peer_addr: Peer address
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is used to report successfully completed WPS
	 * provisioning during group formation in both GO/Registrar and
	 * client/Enrollee roles.
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	int (*wps_success_cb)(void *priv, const u8 *peer_addr);

	/**
	 * p2p_group_formation_failed - Report failed WPS provisioning
	 * @priv: Private driver interface data
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is used to report failed group formation. This can
	 * happen either due to failed WPS provisioning or due to 15 second
	 * timeout during the provisioning phase.
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	int (*p2p_group_formation_failed)(void *priv);

	/**
	 * p2p_set_params - Set P2P parameters
	 * @priv: Private driver interface data
	 * @params: P2P parameters
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	int (*p2p_set_params)(void *priv, const struct p2p_params *params);

	/**
	 * p2p_prov_disc_req - Send Provision Discovery Request
	 * @priv: Private driver interface data
	 * @peer_addr: MAC address of the peer P2P client
	 * @config_methods: WPS Config Methods value (only one bit set)
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function can be used to request a discovered P2P peer to
	 * display a PIN (config_methods = WPS_CONFIG_DISPLAY) or be prepared
	 * to enter a PIN from us (config_methods = WPS_CONFIG_KEYPAD). The
	 * Provision Discovery Request frame is transmitted once immediately
	 * and if no response is received, the frame will be sent again
	 * whenever the target device is discovered during device dsicovery
	 * (start with a p2p_find() call). Response from the peer is indicated
	 * with the EVENT_P2P_PROV_DISC_RESPONSE event.
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	int (*p2p_prov_disc_req)(void *priv, const u8 *peer_addr,
				 u16 config_methods);

	/**
	 * p2p_sd_request - Schedule a service discovery query
	 * @priv: Private driver interface data
	 * @dst: Destination peer or %NULL to apply for all peers
	 * @tlvs: P2P Service Query TLV(s)
	 * Returns: Reference to the query or 0 on failure
	 *
	 * Response to the query is indicated with the
	 * EVENT_P2P_SD_RESPONSE driver event.
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	u64 (*p2p_sd_request)(void *priv, const u8 *dst,
			      const struct wpabuf *tlvs);

	/**
	 * p2p_sd_cancel_request - Cancel a pending service discovery query
	 * @priv: Private driver interface data
	 * @req: Query reference from p2p_sd_request()
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	int (*p2p_sd_cancel_request)(void *priv, u64 req);

	/**
	 * p2p_sd_response - Send response to a service discovery query
	 * @priv: Private driver interface data
	 * @freq: Frequency from EVENT_P2P_SD_REQUEST event
	 * @dst: Destination address from EVENT_P2P_SD_REQUEST event
	 * @dialog_token: Dialog token from EVENT_P2P_SD_REQUEST event
	 * @resp_tlvs: P2P Service Response TLV(s)
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is called as a response to the request indicated with
	 * the EVENT_P2P_SD_REQUEST driver event.
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	int (*p2p_sd_response)(void *priv, int freq, const u8 *dst,
			       u8 dialog_token,
			       const struct wpabuf *resp_tlvs);

	/**
	 * p2p_service_update - Indicate a change in local services
	 * @priv: Private driver interface data
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function needs to be called whenever there is a change in
	 * availability of the local services. This will increment the
	 * Service Update Indicator value which will be used in SD Request and
	 * Response frames.
	 *
	 * This function is only used if the driver implements P2P management,
	 * i.e., if it sets WPA_DRIVER_FLAGS_P2P_MGMT in
	 * struct wpa_driver_capa.
	 */
	int (*p2p_service_update)(void *priv);

	/**
	 * p2p_reject - Reject peer device (explicitly block connections)
	 * @priv: Private driver interface data
	 * @addr: MAC address of the peer
	 * Returns: 0 on success, -1 on failure
	 */
	int (*p2p_reject)(void *priv, const u8 *addr);

	/**
	 * p2p_invite - Invite a P2P Device into a group
	 * @priv: Private driver interface data
	 * @peer: Device Address of the peer P2P Device
	 * @role: Local role in the group
	 * @bssid: Group BSSID or %NULL if not known
	 * @ssid: Group SSID
	 * @ssid_len: Length of ssid in octets
	 * @go_dev_addr: Forced GO Device Address or %NULL if none
	 * @persistent_group: Whether this is to reinvoke a persistent group
	 * Returns: 0 on success, -1 on failure
	 */
	int (*p2p_invite)(void *priv, const u8 *peer, int role,
			  const u8 *bssid, const u8 *ssid, size_t ssid_len,
			  const u8 *go_dev_addr, int persistent_group);

	/**
	 * send_tdls_mgmt - for sending TDLS management packets
	 * @priv: private driver interface data
	 * @dst: Destination (peer) MAC address
	 * @action_code: TDLS action code for the mssage
	 * @dialog_token: Dialog Token to use in the message (if needed)
	 * @status_code: Status Code or Reason Code to use (if needed)
	 * @buf: TDLS IEs to add to the message
	 * @len: Length of buf in octets
	 * Returns: 0 on success, -1 on failure
	 *
	 * This optional function can be used to send packet to driver which is
	 * responsible for receiving and sending all TDLS packets.
	 */
	int (*send_tdls_mgmt)(void *priv, const u8 *dst, u8 action_code,
			      u8 dialog_token, u16 status_code,
			      const u8 *buf, size_t len);

	int (*tdls_oper)(void *priv, enum tdls_oper oper, const u8 *peer);

	/**
	 * signal_poll - Get current connection information
	 * @priv: Private driver interface data
	 * @signal_info: Connection info structure
         */
	int (*signal_poll)(void *priv, struct wpa_signal_info *signal_info);
};


/**
 * enum wpa_event_type - Event type for wpa_supplicant_event() calls
 */
enum wpa_event_type {
	/**
	 * EVENT_ASSOC - Association completed
	 *
	 * This event needs to be delivered when the driver completes IEEE
	 * 802.11 association or reassociation successfully.
	 * wpa_driver_ops::get_bssid() is expected to provide the current BSSID
	 * after this event has been generated. In addition, optional
	 * EVENT_ASSOCINFO may be generated just before EVENT_ASSOC to provide
	 * more information about the association. If the driver interface gets
	 * both of these events at the same time, it can also include the
	 * assoc_info data in EVENT_ASSOC call.
	 */
	EVENT_ASSOC,

	/**
	 * EVENT_DISASSOC - Association lost
	 *
	 * This event should be called when association is lost either due to
	 * receiving deauthenticate or disassociate frame from the AP or when
	 * sending either of these frames to the current AP. If the driver
	 * supports separate deauthentication event, EVENT_DISASSOC should only
	 * be used for disassociation and EVENT_DEAUTH for deauthentication.
	 * In AP mode, union wpa_event_data::disassoc_info is required.
	 */
	EVENT_DISASSOC,

	/**
	 * EVENT_MICHAEL_MIC_FAILURE - Michael MIC (TKIP) detected
	 *
	 * This event must be delivered when a Michael MIC error is detected by
	 * the local driver. Additional data for event processing is
	 * provided with union wpa_event_data::michael_mic_failure. This
	 * information is used to request new encyption key and to initiate
	 * TKIP countermeasures if needed.
	 */
	EVENT_MICHAEL_MIC_FAILURE,

	/**
	 * EVENT_SCAN_RESULTS - Scan results available
	 *
	 * This event must be called whenever scan results are available to be
	 * fetched with struct wpa_driver_ops::get_scan_results(). This event
	 * is expected to be used some time after struct wpa_driver_ops::scan()
	 * is called. If the driver provides an unsolicited event when the scan
	 * has been completed, this event can be used to trigger
	 * EVENT_SCAN_RESULTS call. If such event is not available from the
	 * driver, the driver wrapper code is expected to use a registered
	 * timeout to generate EVENT_SCAN_RESULTS call after the time that the
	 * scan is expected to be completed. Optional information about
	 * completed scan can be provided with union wpa_event_data::scan_info.
	 */
	EVENT_SCAN_RESULTS,

	/**
	 * EVENT_ASSOCINFO - Report optional extra information for association
	 *
	 * This event can be used to report extra association information for
	 * EVENT_ASSOC processing. This extra information includes IEs from
	 * association frames and Beacon/Probe Response frames in union
	 * wpa_event_data::assoc_info. EVENT_ASSOCINFO must be send just before
	 * EVENT_ASSOC. Alternatively, the driver interface can include
	 * assoc_info data in the EVENT_ASSOC call if it has all the
	 * information available at the same point.
	 */
	EVENT_ASSOCINFO,

	/**
	 * EVENT_INTERFACE_STATUS - Report interface status changes
	 *
	 * This optional event can be used to report changes in interface
	 * status (interface added/removed) using union
	 * wpa_event_data::interface_status. This can be used to trigger
	 * wpa_supplicant to stop and re-start processing for the interface,
	 * e.g., when a cardbus card is ejected/inserted.
	 */
	EVENT_INTERFACE_STATUS,

	/**
	 * EVENT_PMKID_CANDIDATE - Report a candidate AP for pre-authentication
	 *
	 * This event can be used to inform wpa_supplicant about candidates for
	 * RSN (WPA2) pre-authentication. If wpa_supplicant is not responsible
	 * for scan request (ap_scan=2 mode), this event is required for
	 * pre-authentication. If wpa_supplicant is performing scan request
	 * (ap_scan=1), this event is optional since scan results can be used
	 * to add pre-authentication candidates. union
	 * wpa_event_data::pmkid_candidate is used to report the BSSID of the
	 * candidate and priority of the candidate, e.g., based on the signal
	 * strength, in order to try to pre-authenticate first with candidates
	 * that are most likely targets for re-association.
	 *
	 * EVENT_PMKID_CANDIDATE can be called whenever the driver has updates
	 * on the candidate list. In addition, it can be called for the current
	 * AP and APs that have existing PMKSA cache entries. wpa_supplicant
	 * will automatically skip pre-authentication in cases where a valid
	 * PMKSA exists. When more than one candidate exists, this event should
	 * be generated once for each candidate.
	 *
	 * Driver will be notified about successful pre-authentication with
	 * struct wpa_driver_ops::add_pmkid() calls.
	 */
	EVENT_PMKID_CANDIDATE,

	/**
	 * EVENT_STKSTART - Request STK handshake (MLME-STKSTART.request)
	 *
	 * This event can be used to inform wpa_supplicant about desire to set
	 * up secure direct link connection between two stations as defined in
	 * IEEE 802.11e with a new PeerKey mechanism that replaced the original
	 * STAKey negotiation. The caller will need to set peer address for the
	 * event.
	 */
	EVENT_STKSTART,

	/**
	 * EVENT_TDLS - Request TDLS operation
	 *
	 * This event can be used to request a TDLS operation to be performed.
	 */
	EVENT_TDLS,

	/**
	 * EVENT_FT_RESPONSE - Report FT (IEEE 802.11r) response IEs
	 *
	 * The driver is expected to report the received FT IEs from
	 * FT authentication sequence from the AP. The FT IEs are included in
	 * the extra information in union wpa_event_data::ft_ies.
	 */
	EVENT_FT_RESPONSE,

	/**
	 * EVENT_IBSS_RSN_START - Request RSN authentication in IBSS
	 *
	 * The driver can use this event to inform wpa_supplicant about a STA
	 * in an IBSS with which protected frames could be exchanged. This
	 * event starts RSN authentication with the other STA to authenticate
	 * the STA and set up encryption keys with it.
	 */
	EVENT_IBSS_RSN_START,

	/**
	 * EVENT_AUTH - Authentication result
	 *
	 * This event should be called when authentication attempt has been
	 * completed. This is only used if the driver supports separate
	 * authentication step (struct wpa_driver_ops::authenticate).
	 * Information about authentication result is included in
	 * union wpa_event_data::auth.
	 */
	EVENT_AUTH,

	/**
	 * EVENT_DEAUTH - Authentication lost
	 *
	 * This event should be called when authentication is lost either due
	 * to receiving deauthenticate frame from the AP or when sending that
	 * frame to the current AP.
	 * In AP mode, union wpa_event_data::deauth_info is required.
	 */
	EVENT_DEAUTH,

	/**
	 * EVENT_ASSOC_REJECT - Association rejected
	 *
	 * This event should be called when (re)association attempt has been
	 * rejected by the AP. Information about the association response is
	 * included in union wpa_event_data::assoc_reject.
	 */
	EVENT_ASSOC_REJECT,

	/**
	 * EVENT_AUTH_TIMED_OUT - Authentication timed out
	 */
	EVENT_AUTH_TIMED_OUT,

	/**
	 * EVENT_ASSOC_TIMED_OUT - Association timed out
	 */
	EVENT_ASSOC_TIMED_OUT,

	/**
	 * EVENT_FT_RRB_RX - FT (IEEE 802.11r) RRB frame received
	 */
	EVENT_FT_RRB_RX,

	/**
	 * EVENT_WPS_BUTTON_PUSHED - Report hardware push button press for WPS
	 */
	EVENT_WPS_BUTTON_PUSHED,

	/**
	 * EVENT_TX_STATUS - Report TX status
	 */
	EVENT_TX_STATUS,

	/**
	 * EVENT_RX_FROM_UNKNOWN - Report RX from unknown STA
	 */
	EVENT_RX_FROM_UNKNOWN,

	/**
	 * EVENT_RX_MGMT - Report RX of a management frame
	 */
	EVENT_RX_MGMT,

	/**
	 * EVENT_RX_ACTION - Action frame received
	 *
	 * This event is used to indicate when an Action frame has been
	 * received. Information about the received frame is included in
	 * union wpa_event_data::rx_action.
	 */
	EVENT_RX_ACTION,

	/**
	 * EVENT_REMAIN_ON_CHANNEL - Remain-on-channel duration started
	 *
	 * This event is used to indicate when the driver has started the
	 * requested remain-on-channel duration. Information about the
	 * operation is included in union wpa_event_data::remain_on_channel.
	 */
	EVENT_REMAIN_ON_CHANNEL,

	/**
	 * EVENT_CANCEL_REMAIN_ON_CHANNEL - Remain-on-channel timed out
	 *
	 * This event is used to indicate when the driver has completed
	 * remain-on-channel duration, i.e., may noot be available on the
	 * requested channel anymore. Information about the
	 * operation is included in union wpa_event_data::remain_on_channel.
	 */
	EVENT_CANCEL_REMAIN_ON_CHANNEL,

	/**
	 * EVENT_MLME_RX - Report reception of frame for MLME (test use only)
	 *
	 * This event is used only by driver_test.c and userspace MLME.
	 */
	EVENT_MLME_RX,

	/**
	 * EVENT_RX_PROBE_REQ - Indicate received Probe Request frame
	 *
	 * This event is used to indicate when a Probe Request frame has been
	 * received. Information about the received frame is included in
	 * union wpa_event_data::rx_probe_req. The driver is required to report
	 * these events only after successfully completed probe_req_report()
	 * commands to request the events (i.e., report parameter is non-zero)
	 * in station mode. In AP mode, Probe Request frames should always be
	 * reported.
	 */
	EVENT_RX_PROBE_REQ,

	/**
	 * EVENT_NEW_STA - New wired device noticed
	 *
	 * This event is used to indicate that a new device has been detected
	 * in a network that does not use association-like functionality (i.e.,
	 * mainly wired Ethernet). This can be used to start EAPOL
	 * authenticator when receiving a frame from a device. The address of
	 * the device is included in union wpa_event_data::new_sta.
	 */
	EVENT_NEW_STA,

	/**
	 * EVENT_EAPOL_RX - Report received EAPOL frame
	 *
	 * When in AP mode with hostapd, this event is required to be used to
	 * deliver the receive EAPOL frames from the driver. With
	 * %wpa_supplicant, this event is used only if the send_eapol() handler
	 * is used to override the use of l2_packet for EAPOL frame TX.
	 */
	EVENT_EAPOL_RX,

	/**
	 * EVENT_SIGNAL_CHANGE - Indicate change in signal strength
	 *
	 * This event is used to indicate changes in the signal strength
	 * observed in frames received from the current AP if signal strength
	 * monitoring has been enabled with signal_monitor().
	 */
	EVENT_SIGNAL_CHANGE,

	/**
	 * EVENT_INTERFACE_ENABLED - Notify that interface was enabled
	 *
	 * This event is used to indicate that the interface was enabled after
	 * having been previously disabled, e.g., due to rfkill.
	 */
	EVENT_INTERFACE_ENABLED,

	/**
	 * EVENT_INTERFACE_DISABLED - Notify that interface was disabled
	 *
	 * This event is used to indicate that the interface was disabled,
	 * e.g., due to rfkill.
	 */
	EVENT_INTERFACE_DISABLED,

	/**
	 * EVENT_CHANNEL_LIST_CHANGED - Channel list changed
	 *
	 * This event is used to indicate that the channel list has changed,
	 * e.g., because of a regulatory domain change triggered by scan
	 * results including an AP advertising a country code.
	 */
	EVENT_CHANNEL_LIST_CHANGED,

	/**
	 * EVENT_INTERFACE_UNAVAILABLE - Notify that interface is unavailable
	 *
	 * This event is used to indicate that the driver cannot maintain this
	 * interface in its operation mode anymore. The most likely use for
	 * this is to indicate that AP mode operation is not available due to
	 * operating channel would need to be changed to a DFS channel when
	 * the driver does not support radar detection and another virtual
	 * interfaces caused the operating channel to change. Other similar
	 * resource conflicts could also trigger this for station mode
	 * interfaces.
	 */
	EVENT_INTERFACE_UNAVAILABLE,

	/**
	 * EVENT_BEST_CHANNEL
	 *
	 * Driver generates this event whenever it detects a better channel
	 * (e.g., based on RSSI or channel use). This information can be used
	 * to improve channel selection for a new AP/P2P group.
	 */
	EVENT_BEST_CHANNEL,

	/**
	 * EVENT_UNPROT_DEAUTH - Unprotected Deauthentication frame received
	 *
	 * This event should be called when a Deauthentication frame is dropped
	 * due to it not being protected (MFP/IEEE 802.11w).
	 * union wpa_event_data::unprot_deauth is required to provide more
	 * details of the frame.
	 */
	EVENT_UNPROT_DEAUTH,

	/**
	 * EVENT_UNPROT_DISASSOC - Unprotected Disassociation frame received
	 *
	 * This event should be called when a Disassociation frame is dropped
	 * due to it not being protected (MFP/IEEE 802.11w).
	 * union wpa_event_data::unprot_disassoc is required to provide more
	 * details of the frame.
	 */
	EVENT_UNPROT_DISASSOC,

	/**
	 * EVENT_STATION_LOW_ACK
	 *
	 * Driver generates this event whenever it detected that a particular
	 * station was lost. Detection can be through massive transmission
	 * failures for example.
	 */
	EVENT_STATION_LOW_ACK,

	/**
	 * EVENT_P2P_DEV_FOUND - Report a discovered P2P device
	 *
	 * This event is used only if the driver implements P2P management
	 * internally. Event data is stored in
	 * union wpa_event_data::p2p_dev_found.
	 */
	EVENT_P2P_DEV_FOUND,

	/**
	 * EVENT_P2P_GO_NEG_REQ_RX - Report reception of GO Negotiation Request
	 *
	 * This event is used only if the driver implements P2P management
	 * internally. Event data is stored in
	 * union wpa_event_data::p2p_go_neg_req_rx.
	 */
	EVENT_P2P_GO_NEG_REQ_RX,

	/**
	 * EVENT_P2P_GO_NEG_COMPLETED - Report completion of GO Negotiation
	 *
	 * This event is used only if the driver implements P2P management
	 * internally. Event data is stored in
	 * union wpa_event_data::p2p_go_neg_completed.
	 */
	EVENT_P2P_GO_NEG_COMPLETED,

	EVENT_P2P_PROV_DISC_REQUEST,
	EVENT_P2P_PROV_DISC_RESPONSE,
	EVENT_P2P_SD_REQUEST,
	EVENT_P2P_SD_RESPONSE,

	/**
	 * EVENT_IBSS_PEER_LOST - IBSS peer not reachable anymore
	 */
	EVENT_IBSS_PEER_LOST
};


/**
 * union wpa_event_data - Additional data for wpa_supplicant_event() calls
 */
union wpa_event_data {
	/**
	 * struct assoc_info - Data for EVENT_ASSOC and EVENT_ASSOCINFO events
	 *
	 * This structure is optional for EVENT_ASSOC calls and required for
	 * EVENT_ASSOCINFO calls. By using EVENT_ASSOC with this data, the
	 * driver interface does not need to generate separate EVENT_ASSOCINFO
	 * calls.
	 */
	struct assoc_info {
		/**
		 * reassoc - Flag to indicate association or reassociation
		 */
		int reassoc;

		/**
		 * req_ies - (Re)Association Request IEs
		 *
		 * If the driver generates WPA/RSN IE, this event data must be
		 * returned for WPA handshake to have needed information. If
		 * wpa_supplicant-generated WPA/RSN IE is used, this
		 * information event is optional.
		 *
		 * This should start with the first IE (fixed fields before IEs
		 * are not included).
		 */
		const u8 *req_ies;

		/**
		 * req_ies_len - Length of req_ies in bytes
		 */
		size_t req_ies_len;

		/**
		 * resp_ies - (Re)Association Response IEs
		 *
		 * Optional association data from the driver. This data is not
		 * required WPA, but may be useful for some protocols and as
		 * such, should be reported if this is available to the driver
		 * interface.
		 *
		 * This should start with the first IE (fixed fields before IEs
		 * are not included).
		 */
		const u8 *resp_ies;

		/**
		 * resp_ies_len - Length of resp_ies in bytes
		 */
		size_t resp_ies_len;

		/**
		 * beacon_ies - Beacon or Probe Response IEs
		 *
		 * Optional Beacon/ProbeResp data: IEs included in Beacon or
		 * Probe Response frames from the current AP (i.e., the one
		 * that the client just associated with). This information is
		 * used to update WPA/RSN IE for the AP. If this field is not
		 * set, the results from previous scan will be used. If no
		 * data for the new AP is found, scan results will be requested
		 * again (without scan request). At this point, the driver is
		 * expected to provide WPA/RSN IE for the AP (if WPA/WPA2 is
		 * used).
		 *
		 * This should start with the first IE (fixed fields before IEs
		 * are not included).
		 */
		const u8 *beacon_ies;

		/**
		 * beacon_ies_len - Length of beacon_ies */
		size_t beacon_ies_len;

		/**
		 * freq - Frequency of the operational channel in MHz
		 */
		unsigned int freq;

		/**
		 * addr - Station address (for AP mode)
		 */
		const u8 *addr;
	} assoc_info;

	/**
	 * struct disassoc_info - Data for EVENT_DISASSOC events
	 */
	struct disassoc_info {
		/**
		 * addr - Station address (for AP mode)
		 */
		const u8 *addr;

		/**
		 * reason_code - Reason Code (host byte order) used in
		 *	Deauthentication frame
		 */
		u16 reason_code;

		/**
		 * ie - Optional IE(s) in Disassociation frame
		 */
		const u8 *ie;

		/**
		 * ie_len - Length of ie buffer in octets
		 */
		size_t ie_len;
	} disassoc_info;

	/**
	 * struct deauth_info - Data for EVENT_DEAUTH events
	 */
	struct deauth_info {
		/**
		 * addr - Station address (for AP mode)
		 */
		const u8 *addr;

		/**
		 * reason_code - Reason Code (host byte order) used in
		 *	Deauthentication frame
		 */
		u16 reason_code;

		/**
		 * ie - Optional IE(s) in Deauthentication frame
		 */
		const u8 *ie;

		/**
		 * ie_len - Length of ie buffer in octets
		 */
		size_t ie_len;
	} deauth_info;

	/**
	 * struct michael_mic_failure - Data for EVENT_MICHAEL_MIC_FAILURE
	 */
	struct michael_mic_failure {
		int unicast;
		const u8 *src;
	} michael_mic_failure;

	/**
	 * struct interface_status - Data for EVENT_INTERFACE_STATUS
	 */
	struct interface_status {
		char ifname[100];
		enum {
			EVENT_INTERFACE_ADDED, EVENT_INTERFACE_REMOVED
		} ievent;
	} interface_status;

	/**
	 * struct pmkid_candidate - Data for EVENT_PMKID_CANDIDATE
	 */
	struct pmkid_candidate {
		/** BSSID of the PMKID candidate */
		u8 bssid[ETH_ALEN];
		/** Smaller the index, higher the priority */
		int index;
		/** Whether RSN IE includes pre-authenticate flag */
		int preauth;
	} pmkid_candidate;

	/**
	 * struct stkstart - Data for EVENT_STKSTART
	 */
	struct stkstart {
		u8 peer[ETH_ALEN];
	} stkstart;

	/**
	 * struct tdls - Data for EVENT_TDLS
	 */
	struct tdls {
		u8 peer[ETH_ALEN];
		enum {
			TDLS_REQUEST_SETUP,
			TDLS_REQUEST_TEARDOWN
		} oper;
		u16 reason_code; /* for teardown */
	} tdls;

	/**
	 * struct ft_ies - FT information elements (EVENT_FT_RESPONSE)
	 *
	 * During FT (IEEE 802.11r) authentication sequence, the driver is
	 * expected to use this event to report received FT IEs (MDIE, FTIE,
	 * RSN IE, TIE, possible resource request) to the supplicant. The FT
	 * IEs for the next message will be delivered through the
	 * struct wpa_driver_ops::update_ft_ies() callback.
	 */
	struct ft_ies {
		const u8 *ies;
		size_t ies_len;
		int ft_action;
		u8 target_ap[ETH_ALEN];
		/** Optional IE(s), e.g., WMM TSPEC(s), for RIC-Request */
		const u8 *ric_ies;
		/** Length of ric_ies buffer in octets */
		size_t ric_ies_len;
	} ft_ies;

	/**
	 * struct ibss_rsn_start - Data for EVENT_IBSS_RSN_START
	 */
	struct ibss_rsn_start {
		u8 peer[ETH_ALEN];
	} ibss_rsn_start;

	/**
	 * struct auth_info - Data for EVENT_AUTH events
	 */
	struct auth_info {
		u8 peer[ETH_ALEN];
		u16 auth_type;
		u16 status_code;
		const u8 *ies;
		size_t ies_len;
	} auth;

	/**
	 * struct assoc_reject - Data for EVENT_ASSOC_REJECT events
	 */
	struct assoc_reject {
		/**
		 * bssid - BSSID of the AP that rejected association
		 */
		const u8 *bssid;

		/**
		 * resp_ies - (Re)Association Response IEs
		 *
		 * Optional association data from the driver. This data is not
		 * required WPA, but may be useful for some protocols and as
		 * such, should be reported if this is available to the driver
		 * interface.
		 *
		 * This should start with the first IE (fixed fields before IEs
		 * are not included).
		 */
		const u8 *resp_ies;

		/**
		 * resp_ies_len - Length of resp_ies in bytes
		 */
		size_t resp_ies_len;

		/**
		 * status_code - Status Code from (Re)association Response
		 */
		u16 status_code;
	} assoc_reject;

	struct timeout_event {
		u8 addr[ETH_ALEN];
	} timeout_event;

	/**
	 * struct ft_rrb_rx - Data for EVENT_FT_RRB_RX events
	 */
	struct ft_rrb_rx {
		const u8 *src;
		const u8 *data;
		size_t data_len;
	} ft_rrb_rx;

	/**
	 * struct tx_status - Data for EVENT_TX_STATUS events
	 */
	struct tx_status {
		u16 type;
		u16 stype;
		const u8 *dst;
		const u8 *data;
		size_t data_len;
		int ack;
	} tx_status;

	/**
	 * struct rx_from_unknown - Data for EVENT_RX_FROM_UNKNOWN events
	 */
	struct rx_from_unknown {
		const u8 *frame;
		size_t len;
	} rx_from_unknown;

	/**
	 * struct rx_mgmt - Data for EVENT_RX_MGMT events
	 */
	struct rx_mgmt {
		const u8 *frame;
		size_t frame_len;
		u32 datarate;
		u32 ssi_signal;
	} rx_mgmt;

	/**
	 * struct rx_action - Data for EVENT_RX_ACTION events
	 */
	struct rx_action {
		/**
		 * da - Destination address of the received Action frame
		 */
		const u8 *da;

		/**
		 * sa - Source address of the received Action frame
		 */
		const u8 *sa;

		/**
		 * bssid - Address 3 of the received Action frame
		 */
		const u8 *bssid;

		/**
		 * category - Action frame category
		 */
		u8 category;

		/**
		 * data - Action frame body after category field
		 */
		const u8 *data;

		/**
		 * len - Length of data in octets
		 */
		size_t len;

		/**
		 * freq - Frequency (in MHz) on which the frame was received
		 */
		int freq;
	} rx_action;

	/**
	 * struct remain_on_channel - Data for EVENT_REMAIN_ON_CHANNEL events
	 *
	 * This is also used with EVENT_CANCEL_REMAIN_ON_CHANNEL events.
	 */
	struct remain_on_channel {
		/**
		 * freq - Channel frequency in MHz
		 */
		unsigned int freq;

		/**
		 * duration - Duration to remain on the channel in milliseconds
		 */
		unsigned int duration;
	} remain_on_channel;

	/**
	 * struct scan_info - Optional data for EVENT_SCAN_RESULTS events
	 * @aborted: Whether the scan was aborted
	 * @freqs: Scanned frequencies in MHz (%NULL = all channels scanned)
	 * @num_freqs: Number of entries in freqs array
	 * @ssids: Scanned SSIDs (%NULL or zero-length SSID indicates wildcard
	 *	SSID)
	 * @num_ssids: Number of entries in ssids array
	 */
	struct scan_info {
		int aborted;
		const int *freqs;
		size_t num_freqs;
		struct wpa_driver_scan_ssid ssids[WPAS_MAX_SCAN_SSIDS];
		size_t num_ssids;
	} scan_info;

	/**
	 * struct mlme_rx - Data for EVENT_MLME_RX events
	 */
	struct mlme_rx {
		const u8 *buf;
		size_t len;
		int freq;
		int channel;
		int ssi;
	} mlme_rx;

	/**
	 * struct rx_probe_req - Data for EVENT_RX_PROBE_REQ events
	 */
	struct rx_probe_req {
		/**
		 * sa - Source address of the received Probe Request frame
		 */
		const u8 *sa;

		/**
		 * ie - IEs from the Probe Request body
		 */
		const u8 *ie;

		/**
		 * ie_len - Length of ie buffer in octets
		 */
		size_t ie_len;
	} rx_probe_req;

	/**
	 * struct new_sta - Data for EVENT_NEW_STA events
	 */
	struct new_sta {
		const u8 *addr;
	} new_sta;

	/**
	 * struct eapol_rx - Data for EVENT_EAPOL_RX events
	 */
	struct eapol_rx {
		const u8 *src;
		const u8 *data;
		size_t data_len;
	} eapol_rx;

	/**
	 * signal_change - Data for EVENT_SIGNAL_CHANGE events
	 */
	struct wpa_signal_info signal_change;

	/**
	 * struct best_channel - Data for EVENT_BEST_CHANNEL events
	 * @freq_24: Best 2.4 GHz band channel frequency in MHz
	 * @freq_5: Best 5 GHz band channel frequency in MHz
	 * @freq_overall: Best channel frequency in MHz
	 *
	 * 0 can be used to indicate no preference in either band.
	 */
	struct best_channel {
		int freq_24;
		int freq_5;
		int freq_overall;
	} best_chan;

	struct unprot_deauth {
		const u8 *sa;
		const u8 *da;
		u16 reason_code;
	} unprot_deauth;

	struct unprot_disassoc {
		const u8 *sa;
		const u8 *da;
		u16 reason_code;
	} unprot_disassoc;

	/**
	 * struct low_ack - Data for EVENT_STATION_LOW_ACK events
	 * @addr: station address
	 */
	struct low_ack {
		u8 addr[ETH_ALEN];
	} low_ack;

	/**
	 * struct p2p_dev_found - Data for EVENT_P2P_DEV_FOUND
	 */
	struct p2p_dev_found {
		const u8 *addr;
		const u8 *dev_addr;
		const u8 *pri_dev_type;
		const char *dev_name;
		u16 config_methods;
		u8 dev_capab;
		u8 group_capab;
	} p2p_dev_found;

	/**
	 * struct p2p_go_neg_req_rx - Data for EVENT_P2P_GO_NEG_REQ_RX
	 */
	struct p2p_go_neg_req_rx {
		const u8 *src;
		u16 dev_passwd_id;
	} p2p_go_neg_req_rx;

	/**
	 * struct p2p_go_neg_completed - Data for EVENT_P2P_GO_NEG_COMPLETED
	 */
	struct p2p_go_neg_completed {
		struct p2p_go_neg_results *res;
	} p2p_go_neg_completed;

	struct p2p_prov_disc_req {
		const u8 *peer;
		u16 config_methods;
		const u8 *dev_addr;
		const u8 *pri_dev_type;
		const char *dev_name;
		u16 supp_config_methods;
		u8 dev_capab;
		u8 group_capab;
	} p2p_prov_disc_req;

	struct p2p_prov_disc_resp {
		const u8 *peer;
		u16 config_methods;
	} p2p_prov_disc_resp;

	struct p2p_sd_req {
		int freq;
		const u8 *sa;
		u8 dialog_token;
		u16 update_indic;
		const u8 *tlvs;
		size_t tlvs_len;
	} p2p_sd_req;

	struct p2p_sd_resp {
		const u8 *sa;
		u16 update_indic;
		const u8 *tlvs;
		size_t tlvs_len;
	} p2p_sd_resp;

	/**
	 * struct ibss_peer_lost - Data for EVENT_IBSS_PEER_LOST
	 */
	struct ibss_peer_lost {
		u8 peer[ETH_ALEN];
	} ibss_peer_lost;
};

/**
 * wpa_supplicant_event - Report a driver event for wpa_supplicant
 * @ctx: Context pointer (wpa_s); this is the ctx variable registered
 *	with struct wpa_driver_ops::init()
 * @event: event type (defined above)
 * @data: possible extra data for the event
 *
 * Driver wrapper code should call this function whenever an event is received
 * from the driver.
 */
void wpa_supplicant_event(void *ctx, enum wpa_event_type event,
			  union wpa_event_data *data);


/*
 * The following inline functions are provided for convenience to simplify
 * event indication for some of the common events.
 */

static inline void drv_event_assoc(void *ctx, const u8 *addr, const u8 *ie,
				   size_t ielen, int reassoc)
{
	union wpa_event_data event;
	os_memset(&event, 0, sizeof(event));
	event.assoc_info.reassoc = reassoc;
	event.assoc_info.req_ies = ie;
	event.assoc_info.req_ies_len = ielen;
	event.assoc_info.addr = addr;
	wpa_supplicant_event(ctx, EVENT_ASSOC, &event);
}

static inline void drv_event_disassoc(void *ctx, const u8 *addr)
{
	union wpa_event_data event;
	os_memset(&event, 0, sizeof(event));
	event.disassoc_info.addr = addr;
	wpa_supplicant_event(ctx, EVENT_DISASSOC, &event);
}

static inline void drv_event_eapol_rx(void *ctx, const u8 *src, const u8 *data,
				      size_t data_len)
{
	union wpa_event_data event;
	os_memset(&event, 0, sizeof(event));
	event.eapol_rx.src = src;
	event.eapol_rx.data = data;
	event.eapol_rx.data_len = data_len;
	wpa_supplicant_event(ctx, EVENT_EAPOL_RX, &event);
}

#endif /* DRIVER_H */
