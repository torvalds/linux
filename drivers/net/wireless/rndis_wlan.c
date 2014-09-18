/*
 * Driver for RNDIS based wireless USB devices.
 *
 * Copyright (C) 2007 by Bjorge Dijkstra <bjd@jooz.net>
 * Copyright (C) 2008-2009 by Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Portions of this file are based on NDISwrapper project,
 *  Copyright (C) 2003-2005 Pontus Fuchs, Giridhar Pemmasani
 *  http://ndiswrapper.sourceforge.net/
 */

// #define	DEBUG			// error path messages, extra info
// #define	VERBOSE			// more; success messages

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/ieee80211.h>
#include <linux/if_arp.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <net/cfg80211.h>
#include <linux/usb/usbnet.h>
#include <linux/usb/rndis_host.h>


/* NOTE: All these are settings for Broadcom chipset */
static char modparam_country[4] = "EU";
module_param_string(country, modparam_country, 4, 0444);
MODULE_PARM_DESC(country, "Country code (ISO 3166-1 alpha-2), default: EU");

static int modparam_frameburst = 1;
module_param_named(frameburst, modparam_frameburst, int, 0444);
MODULE_PARM_DESC(frameburst, "enable frame bursting (default: on)");

static int modparam_afterburner = 0;
module_param_named(afterburner, modparam_afterburner, int, 0444);
MODULE_PARM_DESC(afterburner,
	"enable afterburner aka '125 High Speed Mode' (default: off)");

static int modparam_power_save = 0;
module_param_named(power_save, modparam_power_save, int, 0444);
MODULE_PARM_DESC(power_save,
	"set power save mode: 0=off, 1=on, 2=fast (default: off)");

static int modparam_power_output = 3;
module_param_named(power_output, modparam_power_output, int, 0444);
MODULE_PARM_DESC(power_output,
	"set power output: 0=25%, 1=50%, 2=75%, 3=100% (default: 100%)");

static int modparam_roamtrigger = -70;
module_param_named(roamtrigger, modparam_roamtrigger, int, 0444);
MODULE_PARM_DESC(roamtrigger,
	"set roaming dBm trigger: -80=optimize for distance, "
				"-60=bandwidth (default: -70)");

static int modparam_roamdelta = 1;
module_param_named(roamdelta, modparam_roamdelta, int, 0444);
MODULE_PARM_DESC(roamdelta,
	"set roaming tendency: 0=aggressive, 1=moderate, "
				"2=conservative (default: moderate)");

static int modparam_workaround_interval;
module_param_named(workaround_interval, modparam_workaround_interval,
							int, 0444);
MODULE_PARM_DESC(workaround_interval,
	"set stall workaround interval in msecs (0=disabled) (default: 0)");

/* Typical noise/maximum signal level values taken from ndiswrapper iw_ndis.h */
#define	WL_NOISE	-96	/* typical noise level in dBm */
#define	WL_SIGMAX	-32	/* typical maximum signal level in dBm */


/* Assume that Broadcom 4320 (only chipset at time of writing known to be
 * based on wireless rndis) has default txpower of 13dBm.
 * This value is from Linksys WUSB54GSC User Guide, Appendix F: Specifications.
 *  100% : 20 mW ~ 13dBm
 *   75% : 15 mW ~ 12dBm
 *   50% : 10 mW ~ 10dBm
 *   25% :  5 mW ~  7dBm
 */
#define BCM4320_DEFAULT_TXPOWER_DBM_100 13
#define BCM4320_DEFAULT_TXPOWER_DBM_75  12
#define BCM4320_DEFAULT_TXPOWER_DBM_50  10
#define BCM4320_DEFAULT_TXPOWER_DBM_25  7

/* Known device types */
#define RNDIS_UNKNOWN	0
#define RNDIS_BCM4320A	1
#define RNDIS_BCM4320B	2


/* NDIS data structures. Taken from wpa_supplicant driver_ndis.c
 * slightly modified for datatype endianess, etc
 */
#define NDIS_802_11_LENGTH_SSID 32
#define NDIS_802_11_LENGTH_RATES 8
#define NDIS_802_11_LENGTH_RATES_EX 16

enum ndis_80211_net_type {
	NDIS_80211_TYPE_FREQ_HOP,
	NDIS_80211_TYPE_DIRECT_SEQ,
	NDIS_80211_TYPE_OFDM_A,
	NDIS_80211_TYPE_OFDM_G
};

enum ndis_80211_net_infra {
	NDIS_80211_INFRA_ADHOC,
	NDIS_80211_INFRA_INFRA,
	NDIS_80211_INFRA_AUTO_UNKNOWN
};

enum ndis_80211_auth_mode {
	NDIS_80211_AUTH_OPEN,
	NDIS_80211_AUTH_SHARED,
	NDIS_80211_AUTH_AUTO_SWITCH,
	NDIS_80211_AUTH_WPA,
	NDIS_80211_AUTH_WPA_PSK,
	NDIS_80211_AUTH_WPA_NONE,
	NDIS_80211_AUTH_WPA2,
	NDIS_80211_AUTH_WPA2_PSK
};

enum ndis_80211_encr_status {
	NDIS_80211_ENCR_WEP_ENABLED,
	NDIS_80211_ENCR_DISABLED,
	NDIS_80211_ENCR_WEP_KEY_ABSENT,
	NDIS_80211_ENCR_NOT_SUPPORTED,
	NDIS_80211_ENCR_TKIP_ENABLED,
	NDIS_80211_ENCR_TKIP_KEY_ABSENT,
	NDIS_80211_ENCR_CCMP_ENABLED,
	NDIS_80211_ENCR_CCMP_KEY_ABSENT
};

enum ndis_80211_priv_filter {
	NDIS_80211_PRIV_ACCEPT_ALL,
	NDIS_80211_PRIV_8021X_WEP
};

enum ndis_80211_status_type {
	NDIS_80211_STATUSTYPE_AUTHENTICATION,
	NDIS_80211_STATUSTYPE_MEDIASTREAMMODE,
	NDIS_80211_STATUSTYPE_PMKID_CANDIDATELIST,
	NDIS_80211_STATUSTYPE_RADIOSTATE,
};

enum ndis_80211_media_stream_mode {
	NDIS_80211_MEDIA_STREAM_OFF,
	NDIS_80211_MEDIA_STREAM_ON
};

enum ndis_80211_radio_status {
	NDIS_80211_RADIO_STATUS_ON,
	NDIS_80211_RADIO_STATUS_HARDWARE_OFF,
	NDIS_80211_RADIO_STATUS_SOFTWARE_OFF,
};

enum ndis_80211_addkey_bits {
	NDIS_80211_ADDKEY_8021X_AUTH = cpu_to_le32(1 << 28),
	NDIS_80211_ADDKEY_SET_INIT_RECV_SEQ = cpu_to_le32(1 << 29),
	NDIS_80211_ADDKEY_PAIRWISE_KEY = cpu_to_le32(1 << 30),
	NDIS_80211_ADDKEY_TRANSMIT_KEY = cpu_to_le32(1 << 31)
};

enum ndis_80211_addwep_bits {
	NDIS_80211_ADDWEP_PERCLIENT_KEY = cpu_to_le32(1 << 30),
	NDIS_80211_ADDWEP_TRANSMIT_KEY = cpu_to_le32(1 << 31)
};

enum ndis_80211_power_mode {
	NDIS_80211_POWER_MODE_CAM,
	NDIS_80211_POWER_MODE_MAX_PSP,
	NDIS_80211_POWER_MODE_FAST_PSP,
};

enum ndis_80211_pmkid_cand_list_flag_bits {
	NDIS_80211_PMKID_CAND_PREAUTH = cpu_to_le32(1 << 0)
};

struct ndis_80211_auth_request {
	__le32 length;
	u8 bssid[6];
	u8 padding[2];
	__le32 flags;
} __packed;

struct ndis_80211_pmkid_candidate {
	u8 bssid[6];
	u8 padding[2];
	__le32 flags;
} __packed;

struct ndis_80211_pmkid_cand_list {
	__le32 version;
	__le32 num_candidates;
	struct ndis_80211_pmkid_candidate candidate_list[0];
} __packed;

struct ndis_80211_status_indication {
	__le32 status_type;
	union {
		__le32					media_stream_mode;
		__le32					radio_status;
		struct ndis_80211_auth_request		auth_request[0];
		struct ndis_80211_pmkid_cand_list	cand_list;
	} u;
} __packed;

struct ndis_80211_ssid {
	__le32 length;
	u8 essid[NDIS_802_11_LENGTH_SSID];
} __packed;

struct ndis_80211_conf_freq_hop {
	__le32 length;
	__le32 hop_pattern;
	__le32 hop_set;
	__le32 dwell_time;
} __packed;

struct ndis_80211_conf {
	__le32 length;
	__le32 beacon_period;
	__le32 atim_window;
	__le32 ds_config;
	struct ndis_80211_conf_freq_hop fh_config;
} __packed;

struct ndis_80211_bssid_ex {
	__le32 length;
	u8 mac[6];
	u8 padding[2];
	struct ndis_80211_ssid ssid;
	__le32 privacy;
	__le32 rssi;
	__le32 net_type;
	struct ndis_80211_conf config;
	__le32 net_infra;
	u8 rates[NDIS_802_11_LENGTH_RATES_EX];
	__le32 ie_length;
	u8 ies[0];
} __packed;

struct ndis_80211_bssid_list_ex {
	__le32 num_items;
	struct ndis_80211_bssid_ex bssid[0];
} __packed;

struct ndis_80211_fixed_ies {
	u8 timestamp[8];
	__le16 beacon_interval;
	__le16 capabilities;
} __packed;

struct ndis_80211_wep_key {
	__le32 size;
	__le32 index;
	__le32 length;
	u8 material[32];
} __packed;

struct ndis_80211_key {
	__le32 size;
	__le32 index;
	__le32 length;
	u8 bssid[6];
	u8 padding[6];
	u8 rsc[8];
	u8 material[32];
} __packed;

struct ndis_80211_remove_key {
	__le32 size;
	__le32 index;
	u8 bssid[6];
	u8 padding[2];
} __packed;

struct ndis_config_param {
	__le32 name_offs;
	__le32 name_length;
	__le32 type;
	__le32 value_offs;
	__le32 value_length;
} __packed;

struct ndis_80211_assoc_info {
	__le32 length;
	__le16 req_ies;
	struct req_ie {
		__le16 capa;
		__le16 listen_interval;
		u8 cur_ap_address[6];
	} req_ie;
	__le32 req_ie_length;
	__le32 offset_req_ies;
	__le16 resp_ies;
	struct resp_ie {
		__le16 capa;
		__le16 status_code;
		__le16 assoc_id;
	} resp_ie;
	__le32 resp_ie_length;
	__le32 offset_resp_ies;
} __packed;

struct ndis_80211_auth_encr_pair {
	__le32 auth_mode;
	__le32 encr_mode;
} __packed;

struct ndis_80211_capability {
	__le32 length;
	__le32 version;
	__le32 num_pmkids;
	__le32 num_auth_encr_pair;
	struct ndis_80211_auth_encr_pair auth_encr_pair[0];
} __packed;

struct ndis_80211_bssid_info {
	u8 bssid[6];
	u8 pmkid[16];
} __packed;

struct ndis_80211_pmkid {
	__le32 length;
	__le32 bssid_info_count;
	struct ndis_80211_bssid_info bssid_info[0];
} __packed;

/*
 *  private data
 */
#define CAP_MODE_80211A		1
#define CAP_MODE_80211B		2
#define CAP_MODE_80211G		4
#define CAP_MODE_MASK		7

#define WORK_LINK_UP		(1<<0)
#define WORK_LINK_DOWN		(1<<1)
#define WORK_SET_MULTICAST_LIST	(1<<2)

#define RNDIS_WLAN_ALG_NONE	0
#define RNDIS_WLAN_ALG_WEP	(1<<0)
#define RNDIS_WLAN_ALG_TKIP	(1<<1)
#define RNDIS_WLAN_ALG_CCMP	(1<<2)

#define RNDIS_WLAN_NUM_KEYS		4
#define RNDIS_WLAN_KEY_MGMT_NONE	0
#define RNDIS_WLAN_KEY_MGMT_802_1X	(1<<0)
#define RNDIS_WLAN_KEY_MGMT_PSK		(1<<1)

#define COMMAND_BUFFER_SIZE	(CONTROL_BUFFER_SIZE + sizeof(struct rndis_set))

static const struct ieee80211_channel rndis_channels[] = {
	{ .center_freq = 2412 },
	{ .center_freq = 2417 },
	{ .center_freq = 2422 },
	{ .center_freq = 2427 },
	{ .center_freq = 2432 },
	{ .center_freq = 2437 },
	{ .center_freq = 2442 },
	{ .center_freq = 2447 },
	{ .center_freq = 2452 },
	{ .center_freq = 2457 },
	{ .center_freq = 2462 },
	{ .center_freq = 2467 },
	{ .center_freq = 2472 },
	{ .center_freq = 2484 },
};

static const struct ieee80211_rate rndis_rates[] = {
	{ .bitrate = 10 },
	{ .bitrate = 20, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60 },
	{ .bitrate = 90 },
	{ .bitrate = 120 },
	{ .bitrate = 180 },
	{ .bitrate = 240 },
	{ .bitrate = 360 },
	{ .bitrate = 480 },
	{ .bitrate = 540 }
};

static const u32 rndis_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
};

struct rndis_wlan_encr_key {
	int len;
	u32 cipher;
	u8 material[32];
	u8 bssid[ETH_ALEN];
	bool pairwise;
	bool tx_key;
};

/* RNDIS device private data */
struct rndis_wlan_private {
	struct usbnet *usbdev;

	struct wireless_dev wdev;

	struct cfg80211_scan_request *scan_request;

	struct workqueue_struct *workqueue;
	struct delayed_work dev_poller_work;
	struct delayed_work scan_work;
	struct work_struct work;
	struct mutex command_lock;
	unsigned long work_pending;
	int last_qual;
	s32 cqm_rssi_thold;
	u32 cqm_rssi_hyst;
	int last_cqm_event_rssi;

	struct ieee80211_supported_band band;
	struct ieee80211_channel channels[ARRAY_SIZE(rndis_channels)];
	struct ieee80211_rate rates[ARRAY_SIZE(rndis_rates)];
	u32 cipher_suites[ARRAY_SIZE(rndis_cipher_suites)];

	int device_type;
	int caps;
	int multicast_size;

	/* module parameters */
	char param_country[4];
	int  param_frameburst;
	int  param_afterburner;
	int  param_power_save;
	int  param_power_output;
	int  param_roamtrigger;
	int  param_roamdelta;
	u32  param_workaround_interval;

	/* hardware state */
	bool radio_on;
	int power_mode;
	int infra_mode;
	bool connected;
	u8 bssid[ETH_ALEN];
	u32 current_command_oid;

	/* encryption stuff */
	u8 encr_tx_key_index;
	struct rndis_wlan_encr_key encr_keys[RNDIS_WLAN_NUM_KEYS];
	int  wpa_version;

	u8 command_buffer[COMMAND_BUFFER_SIZE];
};

/*
 * cfg80211 ops
 */
static int rndis_change_virtual_intf(struct wiphy *wiphy,
					struct net_device *dev,
					enum nl80211_iftype type, u32 *flags,
					struct vif_params *params);

static int rndis_scan(struct wiphy *wiphy,
			struct cfg80211_scan_request *request);

static int rndis_set_wiphy_params(struct wiphy *wiphy, u32 changed);

static int rndis_set_tx_power(struct wiphy *wiphy,
			      struct wireless_dev *wdev,
			      enum nl80211_tx_power_setting type,
			      int mbm);
static int rndis_get_tx_power(struct wiphy *wiphy,
			      struct wireless_dev *wdev,
			      int *dbm);

static int rndis_connect(struct wiphy *wiphy, struct net_device *dev,
				struct cfg80211_connect_params *sme);

static int rndis_disconnect(struct wiphy *wiphy, struct net_device *dev,
				u16 reason_code);

static int rndis_join_ibss(struct wiphy *wiphy, struct net_device *dev,
					struct cfg80211_ibss_params *params);

static int rndis_leave_ibss(struct wiphy *wiphy, struct net_device *dev);

static int rndis_add_key(struct wiphy *wiphy, struct net_device *netdev,
			 u8 key_index, bool pairwise, const u8 *mac_addr,
			 struct key_params *params);

static int rndis_del_key(struct wiphy *wiphy, struct net_device *netdev,
			 u8 key_index, bool pairwise, const u8 *mac_addr);

static int rndis_set_default_key(struct wiphy *wiphy, struct net_device *netdev,
				 u8 key_index, bool unicast, bool multicast);

static int rndis_get_station(struct wiphy *wiphy, struct net_device *dev,
			     const u8 *mac, struct station_info *sinfo);

static int rndis_dump_station(struct wiphy *wiphy, struct net_device *dev,
			       int idx, u8 *mac, struct station_info *sinfo);

static int rndis_set_pmksa(struct wiphy *wiphy, struct net_device *netdev,
				struct cfg80211_pmksa *pmksa);

static int rndis_del_pmksa(struct wiphy *wiphy, struct net_device *netdev,
				struct cfg80211_pmksa *pmksa);

static int rndis_flush_pmksa(struct wiphy *wiphy, struct net_device *netdev);

static int rndis_set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
				bool enabled, int timeout);

static int rndis_set_cqm_rssi_config(struct wiphy *wiphy,
					struct net_device *dev,
					s32 rssi_thold, u32 rssi_hyst);

static const struct cfg80211_ops rndis_config_ops = {
	.change_virtual_intf = rndis_change_virtual_intf,
	.scan = rndis_scan,
	.set_wiphy_params = rndis_set_wiphy_params,
	.set_tx_power = rndis_set_tx_power,
	.get_tx_power = rndis_get_tx_power,
	.connect = rndis_connect,
	.disconnect = rndis_disconnect,
	.join_ibss = rndis_join_ibss,
	.leave_ibss = rndis_leave_ibss,
	.add_key = rndis_add_key,
	.del_key = rndis_del_key,
	.set_default_key = rndis_set_default_key,
	.get_station = rndis_get_station,
	.dump_station = rndis_dump_station,
	.set_pmksa = rndis_set_pmksa,
	.del_pmksa = rndis_del_pmksa,
	.flush_pmksa = rndis_flush_pmksa,
	.set_power_mgmt = rndis_set_power_mgmt,
	.set_cqm_rssi_config = rndis_set_cqm_rssi_config,
};

static void *rndis_wiphy_privid = &rndis_wiphy_privid;


static struct rndis_wlan_private *get_rndis_wlan_priv(struct usbnet *dev)
{
	return (struct rndis_wlan_private *)dev->driver_priv;
}

static u32 get_bcm4320_power_dbm(struct rndis_wlan_private *priv)
{
	switch (priv->param_power_output) {
	default:
	case 3:
		return BCM4320_DEFAULT_TXPOWER_DBM_100;
	case 2:
		return BCM4320_DEFAULT_TXPOWER_DBM_75;
	case 1:
		return BCM4320_DEFAULT_TXPOWER_DBM_50;
	case 0:
		return BCM4320_DEFAULT_TXPOWER_DBM_25;
	}
}

static bool is_wpa_key(struct rndis_wlan_private *priv, u8 idx)
{
	int cipher = priv->encr_keys[idx].cipher;

	return (cipher == WLAN_CIPHER_SUITE_CCMP ||
		cipher == WLAN_CIPHER_SUITE_TKIP);
}

static int rndis_cipher_to_alg(u32 cipher)
{
	switch (cipher) {
	default:
		return RNDIS_WLAN_ALG_NONE;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		return RNDIS_WLAN_ALG_WEP;
	case WLAN_CIPHER_SUITE_TKIP:
		return RNDIS_WLAN_ALG_TKIP;
	case WLAN_CIPHER_SUITE_CCMP:
		return RNDIS_WLAN_ALG_CCMP;
	}
}

static int rndis_akm_suite_to_key_mgmt(u32 akm_suite)
{
	switch (akm_suite) {
	default:
		return RNDIS_WLAN_KEY_MGMT_NONE;
	case WLAN_AKM_SUITE_8021X:
		return RNDIS_WLAN_KEY_MGMT_802_1X;
	case WLAN_AKM_SUITE_PSK:
		return RNDIS_WLAN_KEY_MGMT_PSK;
	}
}

#ifdef DEBUG
static const char *oid_to_string(u32 oid)
{
	switch (oid) {
#define OID_STR(oid) case oid: return(#oid)
		/* from rndis_host.h */
		OID_STR(RNDIS_OID_802_3_PERMANENT_ADDRESS);
		OID_STR(RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE);
		OID_STR(RNDIS_OID_GEN_CURRENT_PACKET_FILTER);
		OID_STR(RNDIS_OID_GEN_PHYSICAL_MEDIUM);

		/* from rndis_wlan.c */
		OID_STR(RNDIS_OID_GEN_LINK_SPEED);
		OID_STR(RNDIS_OID_GEN_RNDIS_CONFIG_PARAMETER);

		OID_STR(RNDIS_OID_GEN_XMIT_OK);
		OID_STR(RNDIS_OID_GEN_RCV_OK);
		OID_STR(RNDIS_OID_GEN_XMIT_ERROR);
		OID_STR(RNDIS_OID_GEN_RCV_ERROR);
		OID_STR(RNDIS_OID_GEN_RCV_NO_BUFFER);

		OID_STR(RNDIS_OID_802_3_CURRENT_ADDRESS);
		OID_STR(RNDIS_OID_802_3_MULTICAST_LIST);
		OID_STR(RNDIS_OID_802_3_MAXIMUM_LIST_SIZE);

		OID_STR(RNDIS_OID_802_11_BSSID);
		OID_STR(RNDIS_OID_802_11_SSID);
		OID_STR(RNDIS_OID_802_11_INFRASTRUCTURE_MODE);
		OID_STR(RNDIS_OID_802_11_ADD_WEP);
		OID_STR(RNDIS_OID_802_11_REMOVE_WEP);
		OID_STR(RNDIS_OID_802_11_DISASSOCIATE);
		OID_STR(RNDIS_OID_802_11_AUTHENTICATION_MODE);
		OID_STR(RNDIS_OID_802_11_PRIVACY_FILTER);
		OID_STR(RNDIS_OID_802_11_BSSID_LIST_SCAN);
		OID_STR(RNDIS_OID_802_11_ENCRYPTION_STATUS);
		OID_STR(RNDIS_OID_802_11_ADD_KEY);
		OID_STR(RNDIS_OID_802_11_REMOVE_KEY);
		OID_STR(RNDIS_OID_802_11_ASSOCIATION_INFORMATION);
		OID_STR(RNDIS_OID_802_11_CAPABILITY);
		OID_STR(RNDIS_OID_802_11_PMKID);
		OID_STR(RNDIS_OID_802_11_NETWORK_TYPES_SUPPORTED);
		OID_STR(RNDIS_OID_802_11_NETWORK_TYPE_IN_USE);
		OID_STR(RNDIS_OID_802_11_TX_POWER_LEVEL);
		OID_STR(RNDIS_OID_802_11_RSSI);
		OID_STR(RNDIS_OID_802_11_RSSI_TRIGGER);
		OID_STR(RNDIS_OID_802_11_FRAGMENTATION_THRESHOLD);
		OID_STR(RNDIS_OID_802_11_RTS_THRESHOLD);
		OID_STR(RNDIS_OID_802_11_SUPPORTED_RATES);
		OID_STR(RNDIS_OID_802_11_CONFIGURATION);
		OID_STR(RNDIS_OID_802_11_POWER_MODE);
		OID_STR(RNDIS_OID_802_11_BSSID_LIST);
#undef OID_STR
	}

	return "?";
}
#else
static const char *oid_to_string(u32 oid)
{
	return "?";
}
#endif

/* translate error code */
static int rndis_error_status(__le32 rndis_status)
{
	int ret = -EINVAL;
	switch (le32_to_cpu(rndis_status)) {
	case RNDIS_STATUS_SUCCESS:
		ret = 0;
		break;
	case RNDIS_STATUS_FAILURE:
	case RNDIS_STATUS_INVALID_DATA:
		ret = -EINVAL;
		break;
	case RNDIS_STATUS_NOT_SUPPORTED:
		ret = -EOPNOTSUPP;
		break;
	case RNDIS_STATUS_ADAPTER_NOT_READY:
	case RNDIS_STATUS_ADAPTER_NOT_OPEN:
		ret = -EBUSY;
		break;
	}
	return ret;
}

static int rndis_query_oid(struct usbnet *dev, u32 oid, void *data, int *len)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(dev);
	union {
		void			*buf;
		struct rndis_msg_hdr	*header;
		struct rndis_query	*get;
		struct rndis_query_c	*get_c;
	} u;
	int ret, buflen;
	int resplen, respoffs, copylen;

	buflen = *len + sizeof(*u.get);
	if (buflen < CONTROL_BUFFER_SIZE)
		buflen = CONTROL_BUFFER_SIZE;

	if (buflen > COMMAND_BUFFER_SIZE) {
		u.buf = kmalloc(buflen, GFP_KERNEL);
		if (!u.buf)
			return -ENOMEM;
	} else {
		u.buf = priv->command_buffer;
	}

	mutex_lock(&priv->command_lock);

	memset(u.get, 0, sizeof *u.get);
	u.get->msg_type = cpu_to_le32(RNDIS_MSG_QUERY);
	u.get->msg_len = cpu_to_le32(sizeof *u.get);
	u.get->oid = cpu_to_le32(oid);

	priv->current_command_oid = oid;
	ret = rndis_command(dev, u.header, buflen);
	priv->current_command_oid = 0;
	if (ret < 0)
		netdev_dbg(dev->net, "%s(%s): rndis_command() failed, %d (%08x)\n",
			   __func__, oid_to_string(oid), ret,
			   le32_to_cpu(u.get_c->status));

	if (ret == 0) {
		resplen = le32_to_cpu(u.get_c->len);
		respoffs = le32_to_cpu(u.get_c->offset) + 8;

		if (respoffs > buflen) {
			/* Device returned data offset outside buffer, error. */
			netdev_dbg(dev->net, "%s(%s): received invalid "
				"data offset: %d > %d\n", __func__,
				oid_to_string(oid), respoffs, buflen);

			ret = -EINVAL;
			goto exit_unlock;
		}

		if ((resplen + respoffs) > buflen) {
			/* Device would have returned more data if buffer would
			 * have been big enough. Copy just the bits that we got.
			 */
			copylen = buflen - respoffs;
		} else {
			copylen = resplen;
		}

		if (copylen > *len)
			copylen = *len;

		memcpy(data, u.buf + respoffs, copylen);

		*len = resplen;

		ret = rndis_error_status(u.get_c->status);
		if (ret < 0)
			netdev_dbg(dev->net, "%s(%s): device returned error,  0x%08x (%d)\n",
				   __func__, oid_to_string(oid),
				   le32_to_cpu(u.get_c->status), ret);
	}

exit_unlock:
	mutex_unlock(&priv->command_lock);

	if (u.buf != priv->command_buffer)
		kfree(u.buf);
	return ret;
}

static int rndis_set_oid(struct usbnet *dev, u32 oid, const void *data,
			 int len)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(dev);
	union {
		void			*buf;
		struct rndis_msg_hdr	*header;
		struct rndis_set	*set;
		struct rndis_set_c	*set_c;
	} u;
	int ret, buflen;

	buflen = len + sizeof(*u.set);
	if (buflen < CONTROL_BUFFER_SIZE)
		buflen = CONTROL_BUFFER_SIZE;

	if (buflen > COMMAND_BUFFER_SIZE) {
		u.buf = kmalloc(buflen, GFP_KERNEL);
		if (!u.buf)
			return -ENOMEM;
	} else {
		u.buf = priv->command_buffer;
	}

	mutex_lock(&priv->command_lock);

	memset(u.set, 0, sizeof *u.set);
	u.set->msg_type = cpu_to_le32(RNDIS_MSG_SET);
	u.set->msg_len = cpu_to_le32(sizeof(*u.set) + len);
	u.set->oid = cpu_to_le32(oid);
	u.set->len = cpu_to_le32(len);
	u.set->offset = cpu_to_le32(sizeof(*u.set) - 8);
	u.set->handle = cpu_to_le32(0);
	memcpy(u.buf + sizeof(*u.set), data, len);

	priv->current_command_oid = oid;
	ret = rndis_command(dev, u.header, buflen);
	priv->current_command_oid = 0;
	if (ret < 0)
		netdev_dbg(dev->net, "%s(%s): rndis_command() failed, %d (%08x)\n",
			   __func__, oid_to_string(oid), ret,
			   le32_to_cpu(u.set_c->status));

	if (ret == 0) {
		ret = rndis_error_status(u.set_c->status);

		if (ret < 0)
			netdev_dbg(dev->net, "%s(%s): device returned error, 0x%08x (%d)\n",
				   __func__, oid_to_string(oid),
				   le32_to_cpu(u.set_c->status), ret);
	}

	mutex_unlock(&priv->command_lock);

	if (u.buf != priv->command_buffer)
		kfree(u.buf);
	return ret;
}

static int rndis_reset(struct usbnet *usbdev)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct rndis_reset *reset;
	int ret;

	mutex_lock(&priv->command_lock);

	reset = (void *)priv->command_buffer;
	memset(reset, 0, sizeof(*reset));
	reset->msg_type = cpu_to_le32(RNDIS_MSG_RESET);
	reset->msg_len = cpu_to_le32(sizeof(*reset));
	priv->current_command_oid = 0;
	ret = rndis_command(usbdev, (void *)reset, CONTROL_BUFFER_SIZE);

	mutex_unlock(&priv->command_lock);

	if (ret < 0)
		return ret;
	return 0;
}

/*
 * Specs say that we can only set config parameters only soon after device
 * initialization.
 *   value_type: 0 = u32, 2 = unicode string
 */
static int rndis_set_config_parameter(struct usbnet *dev, char *param,
						int value_type, void *value)
{
	struct ndis_config_param *infobuf;
	int value_len, info_len, param_len, ret, i;
	__le16 *unibuf;
	__le32 *dst_value;

	if (value_type == 0)
		value_len = sizeof(__le32);
	else if (value_type == 2)
		value_len = strlen(value) * sizeof(__le16);
	else
		return -EINVAL;

	param_len = strlen(param) * sizeof(__le16);
	info_len = sizeof(*infobuf) + param_len + value_len;

#ifdef DEBUG
	info_len += 12;
#endif
	infobuf = kmalloc(info_len, GFP_KERNEL);
	if (!infobuf)
		return -ENOMEM;

#ifdef DEBUG
	info_len -= 12;
	/* extra 12 bytes are for padding (debug output) */
	memset(infobuf, 0xCC, info_len + 12);
#endif

	if (value_type == 2)
		netdev_dbg(dev->net, "setting config parameter: %s, value: %s\n",
			   param, (u8 *)value);
	else
		netdev_dbg(dev->net, "setting config parameter: %s, value: %d\n",
			   param, *(u32 *)value);

	infobuf->name_offs = cpu_to_le32(sizeof(*infobuf));
	infobuf->name_length = cpu_to_le32(param_len);
	infobuf->type = cpu_to_le32(value_type);
	infobuf->value_offs = cpu_to_le32(sizeof(*infobuf) + param_len);
	infobuf->value_length = cpu_to_le32(value_len);

	/* simple string to unicode string conversion */
	unibuf = (void *)infobuf + sizeof(*infobuf);
	for (i = 0; i < param_len / sizeof(__le16); i++)
		unibuf[i] = cpu_to_le16(param[i]);

	if (value_type == 2) {
		unibuf = (void *)infobuf + sizeof(*infobuf) + param_len;
		for (i = 0; i < value_len / sizeof(__le16); i++)
			unibuf[i] = cpu_to_le16(((u8 *)value)[i]);
	} else {
		dst_value = (void *)infobuf + sizeof(*infobuf) + param_len;
		*dst_value = cpu_to_le32(*(u32 *)value);
	}

#ifdef DEBUG
	netdev_dbg(dev->net, "info buffer (len: %d)\n", info_len);
	for (i = 0; i < info_len; i += 12) {
		u32 *tmp = (u32 *)((u8 *)infobuf + i);
		netdev_dbg(dev->net, "%08X:%08X:%08X\n",
			   cpu_to_be32(tmp[0]),
			   cpu_to_be32(tmp[1]),
			   cpu_to_be32(tmp[2]));
	}
#endif

	ret = rndis_set_oid(dev, RNDIS_OID_GEN_RNDIS_CONFIG_PARAMETER,
							infobuf, info_len);
	if (ret != 0)
		netdev_dbg(dev->net, "setting rndis config parameter failed, %d\n",
			   ret);

	kfree(infobuf);
	return ret;
}

static int rndis_set_config_parameter_str(struct usbnet *dev,
						char *param, char *value)
{
	return rndis_set_config_parameter(dev, param, 2, value);
}

/*
 * data conversion functions
 */
static int level_to_qual(int level)
{
	int qual = 100 * (level - WL_NOISE) / (WL_SIGMAX - WL_NOISE);
	return qual >= 0 ? (qual <= 100 ? qual : 100) : 0;
}

/*
 * common functions
 */
static int set_infra_mode(struct usbnet *usbdev, int mode);
static void restore_keys(struct usbnet *usbdev);
static int rndis_check_bssid_list(struct usbnet *usbdev, u8 *match_bssid,
					bool *matched);

static int rndis_start_bssid_list_scan(struct usbnet *usbdev)
{
	__le32 tmp;

	/* Note: RNDIS_OID_802_11_BSSID_LIST_SCAN clears internal BSS list. */
	tmp = cpu_to_le32(1);
	return rndis_set_oid(usbdev, RNDIS_OID_802_11_BSSID_LIST_SCAN, &tmp,
							sizeof(tmp));
}

static int set_essid(struct usbnet *usbdev, struct ndis_80211_ssid *ssid)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	int ret;

	ret = rndis_set_oid(usbdev, RNDIS_OID_802_11_SSID,
			    ssid, sizeof(*ssid));
	if (ret < 0) {
		netdev_warn(usbdev->net, "setting SSID failed (%08X)\n", ret);
		return ret;
	}
	if (ret == 0) {
		priv->radio_on = true;
		netdev_dbg(usbdev->net, "%s(): radio_on = true\n", __func__);
	}

	return ret;
}

static int set_bssid(struct usbnet *usbdev, const u8 *bssid)
{
	int ret;

	ret = rndis_set_oid(usbdev, RNDIS_OID_802_11_BSSID,
			    bssid, ETH_ALEN);
	if (ret < 0) {
		netdev_warn(usbdev->net, "setting BSSID[%pM] failed (%08X)\n",
			    bssid, ret);
		return ret;
	}

	return ret;
}

static int clear_bssid(struct usbnet *usbdev)
{
	static const u8 broadcast_mac[ETH_ALEN] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};

	return set_bssid(usbdev, broadcast_mac);
}

static int get_bssid(struct usbnet *usbdev, u8 bssid[ETH_ALEN])
{
	int ret, len;

	len = ETH_ALEN;
	ret = rndis_query_oid(usbdev, RNDIS_OID_802_11_BSSID,
			      bssid, &len);

	if (ret != 0)
		memset(bssid, 0, ETH_ALEN);

	return ret;
}

static int get_association_info(struct usbnet *usbdev,
			struct ndis_80211_assoc_info *info, int len)
{
	return rndis_query_oid(usbdev,
			RNDIS_OID_802_11_ASSOCIATION_INFORMATION,
			info, &len);
}

static bool is_associated(struct usbnet *usbdev)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	u8 bssid[ETH_ALEN];
	int ret;

	if (!priv->radio_on)
		return false;

	ret = get_bssid(usbdev, bssid);

	return (ret == 0 && !is_zero_ether_addr(bssid));
}

static int disassociate(struct usbnet *usbdev, bool reset_ssid)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct ndis_80211_ssid ssid;
	int i, ret = 0;

	if (priv->radio_on) {
		ret = rndis_set_oid(usbdev,
				RNDIS_OID_802_11_DISASSOCIATE,
				NULL, 0);
		if (ret == 0) {
			priv->radio_on = false;
			netdev_dbg(usbdev->net, "%s(): radio_on = false\n",
				   __func__);

			if (reset_ssid)
				msleep(100);
		}
	}

	/* disassociate causes radio to be turned off; if reset_ssid
	 * is given, set random ssid to enable radio */
	if (reset_ssid) {
		/* Set device to infrastructure mode so we don't get ad-hoc
		 * 'media connect' indications with the random ssid.
		 */
		set_infra_mode(usbdev, NDIS_80211_INFRA_INFRA);

		ssid.length = cpu_to_le32(sizeof(ssid.essid));
		get_random_bytes(&ssid.essid[2], sizeof(ssid.essid)-2);
		ssid.essid[0] = 0x1;
		ssid.essid[1] = 0xff;
		for (i = 2; i < sizeof(ssid.essid); i++)
			ssid.essid[i] = 0x1 + (ssid.essid[i] * 0xfe / 0xff);
		ret = set_essid(usbdev, &ssid);
	}
	return ret;
}

static int set_auth_mode(struct usbnet *usbdev, u32 wpa_version,
				enum nl80211_auth_type auth_type, int keymgmt)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	__le32 tmp;
	int auth_mode, ret;

	netdev_dbg(usbdev->net, "%s(): wpa_version=0x%x authalg=0x%x keymgmt=0x%x\n",
		   __func__, wpa_version, auth_type, keymgmt);

	if (wpa_version & NL80211_WPA_VERSION_2) {
		if (keymgmt & RNDIS_WLAN_KEY_MGMT_802_1X)
			auth_mode = NDIS_80211_AUTH_WPA2;
		else
			auth_mode = NDIS_80211_AUTH_WPA2_PSK;
	} else if (wpa_version & NL80211_WPA_VERSION_1) {
		if (keymgmt & RNDIS_WLAN_KEY_MGMT_802_1X)
			auth_mode = NDIS_80211_AUTH_WPA;
		else if (keymgmt & RNDIS_WLAN_KEY_MGMT_PSK)
			auth_mode = NDIS_80211_AUTH_WPA_PSK;
		else
			auth_mode = NDIS_80211_AUTH_WPA_NONE;
	} else if (auth_type == NL80211_AUTHTYPE_SHARED_KEY)
		auth_mode = NDIS_80211_AUTH_SHARED;
	else if (auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM)
		auth_mode = NDIS_80211_AUTH_OPEN;
	else if (auth_type == NL80211_AUTHTYPE_AUTOMATIC)
		auth_mode = NDIS_80211_AUTH_AUTO_SWITCH;
	else
		return -ENOTSUPP;

	tmp = cpu_to_le32(auth_mode);
	ret = rndis_set_oid(usbdev,
			    RNDIS_OID_802_11_AUTHENTICATION_MODE,
			    &tmp, sizeof(tmp));
	if (ret != 0) {
		netdev_warn(usbdev->net, "setting auth mode failed (%08X)\n",
			    ret);
		return ret;
	}

	priv->wpa_version = wpa_version;

	return 0;
}

static int set_priv_filter(struct usbnet *usbdev)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	__le32 tmp;

	netdev_dbg(usbdev->net, "%s(): wpa_version=0x%x\n",
		   __func__, priv->wpa_version);

	if (priv->wpa_version & NL80211_WPA_VERSION_2 ||
	    priv->wpa_version & NL80211_WPA_VERSION_1)
		tmp = cpu_to_le32(NDIS_80211_PRIV_8021X_WEP);
	else
		tmp = cpu_to_le32(NDIS_80211_PRIV_ACCEPT_ALL);

	return rndis_set_oid(usbdev,
			     RNDIS_OID_802_11_PRIVACY_FILTER, &tmp,
			     sizeof(tmp));
}

static int set_encr_mode(struct usbnet *usbdev, int pairwise, int groupwise)
{
	__le32 tmp;
	int encr_mode, ret;

	netdev_dbg(usbdev->net, "%s(): cipher_pair=0x%x cipher_group=0x%x\n",
		   __func__, pairwise, groupwise);

	if (pairwise & RNDIS_WLAN_ALG_CCMP)
		encr_mode = NDIS_80211_ENCR_CCMP_ENABLED;
	else if (pairwise & RNDIS_WLAN_ALG_TKIP)
		encr_mode = NDIS_80211_ENCR_TKIP_ENABLED;
	else if (pairwise & RNDIS_WLAN_ALG_WEP)
		encr_mode = NDIS_80211_ENCR_WEP_ENABLED;
	else if (groupwise & RNDIS_WLAN_ALG_CCMP)
		encr_mode = NDIS_80211_ENCR_CCMP_ENABLED;
	else if (groupwise & RNDIS_WLAN_ALG_TKIP)
		encr_mode = NDIS_80211_ENCR_TKIP_ENABLED;
	else
		encr_mode = NDIS_80211_ENCR_DISABLED;

	tmp = cpu_to_le32(encr_mode);
	ret = rndis_set_oid(usbdev,
			RNDIS_OID_802_11_ENCRYPTION_STATUS, &tmp,
			sizeof(tmp));
	if (ret != 0) {
		netdev_warn(usbdev->net, "setting encr mode failed (%08X)\n",
			    ret);
		return ret;
	}

	return 0;
}

static int set_infra_mode(struct usbnet *usbdev, int mode)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	__le32 tmp;
	int ret;

	netdev_dbg(usbdev->net, "%s(): infra_mode=0x%x\n",
		   __func__, priv->infra_mode);

	tmp = cpu_to_le32(mode);
	ret = rndis_set_oid(usbdev,
			    RNDIS_OID_802_11_INFRASTRUCTURE_MODE,
			    &tmp, sizeof(tmp));
	if (ret != 0) {
		netdev_warn(usbdev->net, "setting infra mode failed (%08X)\n",
			    ret);
		return ret;
	}

	/* NDIS drivers clear keys when infrastructure mode is
	 * changed. But Linux tools assume otherwise. So set the
	 * keys */
	restore_keys(usbdev);

	priv->infra_mode = mode;
	return 0;
}

static int set_rts_threshold(struct usbnet *usbdev, u32 rts_threshold)
{
	__le32 tmp;

	netdev_dbg(usbdev->net, "%s(): %i\n", __func__, rts_threshold);

	if (rts_threshold < 0 || rts_threshold > 2347)
		rts_threshold = 2347;

	tmp = cpu_to_le32(rts_threshold);
	return rndis_set_oid(usbdev,
			     RNDIS_OID_802_11_RTS_THRESHOLD,
			     &tmp, sizeof(tmp));
}

static int set_frag_threshold(struct usbnet *usbdev, u32 frag_threshold)
{
	__le32 tmp;

	netdev_dbg(usbdev->net, "%s(): %i\n", __func__, frag_threshold);

	if (frag_threshold < 256 || frag_threshold > 2346)
		frag_threshold = 2346;

	tmp = cpu_to_le32(frag_threshold);
	return rndis_set_oid(usbdev,
			RNDIS_OID_802_11_FRAGMENTATION_THRESHOLD,
			&tmp, sizeof(tmp));
}

static void set_default_iw_params(struct usbnet *usbdev)
{
	set_infra_mode(usbdev, NDIS_80211_INFRA_INFRA);
	set_auth_mode(usbdev, 0, NL80211_AUTHTYPE_OPEN_SYSTEM,
						RNDIS_WLAN_KEY_MGMT_NONE);
	set_priv_filter(usbdev);
	set_encr_mode(usbdev, RNDIS_WLAN_ALG_NONE, RNDIS_WLAN_ALG_NONE);
}

static int deauthenticate(struct usbnet *usbdev)
{
	int ret;

	ret = disassociate(usbdev, true);
	set_default_iw_params(usbdev);
	return ret;
}

static int set_channel(struct usbnet *usbdev, int channel)
{
	struct ndis_80211_conf config;
	unsigned int dsconfig;
	int len, ret;

	netdev_dbg(usbdev->net, "%s(%d)\n", __func__, channel);

	/* this OID is valid only when not associated */
	if (is_associated(usbdev))
		return 0;

	dsconfig = 1000 *
		ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);

	len = sizeof(config);
	ret = rndis_query_oid(usbdev,
			RNDIS_OID_802_11_CONFIGURATION,
			&config, &len);
	if (ret < 0) {
		netdev_dbg(usbdev->net, "%s(): querying configuration failed\n",
			   __func__);
		return ret;
	}

	config.ds_config = cpu_to_le32(dsconfig);
	ret = rndis_set_oid(usbdev,
			RNDIS_OID_802_11_CONFIGURATION,
			&config, sizeof(config));

	netdev_dbg(usbdev->net, "%s(): %d -> %d\n", __func__, channel, ret);

	return ret;
}

static struct ieee80211_channel *get_current_channel(struct usbnet *usbdev,
						     u32 *beacon_period)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct ieee80211_channel *channel;
	struct ndis_80211_conf config;
	int len, ret;

	/* Get channel and beacon interval */
	len = sizeof(config);
	ret = rndis_query_oid(usbdev,
			RNDIS_OID_802_11_CONFIGURATION,
			&config, &len);
	netdev_dbg(usbdev->net, "%s(): RNDIS_OID_802_11_CONFIGURATION -> %d\n",
				__func__, ret);
	if (ret < 0)
		return NULL;

	channel = ieee80211_get_channel(priv->wdev.wiphy,
				KHZ_TO_MHZ(le32_to_cpu(config.ds_config)));
	if (!channel)
		return NULL;

	if (beacon_period)
		*beacon_period = le32_to_cpu(config.beacon_period);
	return channel;
}

/* index must be 0 - N, as per NDIS  */
static int add_wep_key(struct usbnet *usbdev, const u8 *key, int key_len,
								u8 index)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct ndis_80211_wep_key ndis_key;
	u32 cipher;
	int ret;

	netdev_dbg(usbdev->net, "%s(idx: %d, len: %d)\n",
		   __func__, index, key_len);

	if (index >= RNDIS_WLAN_NUM_KEYS)
		return -EINVAL;

	if (key_len == 5)
		cipher = WLAN_CIPHER_SUITE_WEP40;
	else if (key_len == 13)
		cipher = WLAN_CIPHER_SUITE_WEP104;
	else
		return -EINVAL;

	memset(&ndis_key, 0, sizeof(ndis_key));

	ndis_key.size = cpu_to_le32(sizeof(ndis_key));
	ndis_key.length = cpu_to_le32(key_len);
	ndis_key.index = cpu_to_le32(index);
	memcpy(&ndis_key.material, key, key_len);

	if (index == priv->encr_tx_key_index) {
		ndis_key.index |= NDIS_80211_ADDWEP_TRANSMIT_KEY;
		ret = set_encr_mode(usbdev, RNDIS_WLAN_ALG_WEP,
							RNDIS_WLAN_ALG_NONE);
		if (ret)
			netdev_warn(usbdev->net, "encryption couldn't be enabled (%08X)\n",
				    ret);
	}

	ret = rndis_set_oid(usbdev,
			RNDIS_OID_802_11_ADD_WEP, &ndis_key,
			sizeof(ndis_key));
	if (ret != 0) {
		netdev_warn(usbdev->net, "adding encryption key %d failed (%08X)\n",
			    index + 1, ret);
		return ret;
	}

	priv->encr_keys[index].len = key_len;
	priv->encr_keys[index].cipher = cipher;
	memcpy(&priv->encr_keys[index].material, key, key_len);
	memset(&priv->encr_keys[index].bssid, 0xff, ETH_ALEN);

	return 0;
}

static int add_wpa_key(struct usbnet *usbdev, const u8 *key, int key_len,
			u8 index, const u8 *addr, const u8 *rx_seq,
			int seq_len, u32 cipher, __le32 flags)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct ndis_80211_key ndis_key;
	bool is_addr_ok;
	int ret;

	if (index >= RNDIS_WLAN_NUM_KEYS) {
		netdev_dbg(usbdev->net, "%s(): index out of range (%i)\n",
			   __func__, index);
		return -EINVAL;
	}
	if (key_len > sizeof(ndis_key.material) || key_len < 0) {
		netdev_dbg(usbdev->net, "%s(): key length out of range (%i)\n",
			   __func__, key_len);
		return -EINVAL;
	}
	if (flags & NDIS_80211_ADDKEY_SET_INIT_RECV_SEQ) {
		if (!rx_seq || seq_len <= 0) {
			netdev_dbg(usbdev->net, "%s(): recv seq flag without buffer\n",
				   __func__);
			return -EINVAL;
		}
		if (rx_seq && seq_len > sizeof(ndis_key.rsc)) {
			netdev_dbg(usbdev->net, "%s(): too big recv seq buffer\n", __func__);
			return -EINVAL;
		}
	}

	is_addr_ok = addr && !is_zero_ether_addr(addr) &&
					!is_broadcast_ether_addr(addr);
	if ((flags & NDIS_80211_ADDKEY_PAIRWISE_KEY) && !is_addr_ok) {
		netdev_dbg(usbdev->net, "%s(): pairwise but bssid invalid (%pM)\n",
			   __func__, addr);
		return -EINVAL;
	}

	netdev_dbg(usbdev->net, "%s(%i): flags:%i%i%i\n",
		   __func__, index,
		   !!(flags & NDIS_80211_ADDKEY_TRANSMIT_KEY),
		   !!(flags & NDIS_80211_ADDKEY_PAIRWISE_KEY),
		   !!(flags & NDIS_80211_ADDKEY_SET_INIT_RECV_SEQ));

	memset(&ndis_key, 0, sizeof(ndis_key));

	ndis_key.size = cpu_to_le32(sizeof(ndis_key) -
				sizeof(ndis_key.material) + key_len);
	ndis_key.length = cpu_to_le32(key_len);
	ndis_key.index = cpu_to_le32(index) | flags;

	if (cipher == WLAN_CIPHER_SUITE_TKIP && key_len == 32) {
		/* wpa_supplicant gives us the Michael MIC RX/TX keys in
		 * different order than NDIS spec, so swap the order here. */
		memcpy(ndis_key.material, key, 16);
		memcpy(ndis_key.material + 16, key + 24, 8);
		memcpy(ndis_key.material + 24, key + 16, 8);
	} else
		memcpy(ndis_key.material, key, key_len);

	if (flags & NDIS_80211_ADDKEY_SET_INIT_RECV_SEQ)
		memcpy(ndis_key.rsc, rx_seq, seq_len);

	if (flags & NDIS_80211_ADDKEY_PAIRWISE_KEY) {
		/* pairwise key */
		memcpy(ndis_key.bssid, addr, ETH_ALEN);
	} else {
		/* group key */
		if (priv->infra_mode == NDIS_80211_INFRA_ADHOC)
			memset(ndis_key.bssid, 0xff, ETH_ALEN);
		else
			get_bssid(usbdev, ndis_key.bssid);
	}

	ret = rndis_set_oid(usbdev,
			RNDIS_OID_802_11_ADD_KEY, &ndis_key,
			le32_to_cpu(ndis_key.size));
	netdev_dbg(usbdev->net, "%s(): RNDIS_OID_802_11_ADD_KEY -> %08X\n",
		   __func__, ret);
	if (ret != 0)
		return ret;

	memset(&priv->encr_keys[index], 0, sizeof(priv->encr_keys[index]));
	priv->encr_keys[index].len = key_len;
	priv->encr_keys[index].cipher = cipher;
	memcpy(&priv->encr_keys[index].material, key, key_len);
	if (flags & NDIS_80211_ADDKEY_PAIRWISE_KEY)
		memcpy(&priv->encr_keys[index].bssid, ndis_key.bssid, ETH_ALEN);
	else
		memset(&priv->encr_keys[index].bssid, 0xff, ETH_ALEN);

	if (flags & NDIS_80211_ADDKEY_TRANSMIT_KEY)
		priv->encr_tx_key_index = index;

	return 0;
}

static int restore_key(struct usbnet *usbdev, u8 key_idx)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct rndis_wlan_encr_key key;

	if (is_wpa_key(priv, key_idx))
		return 0;

	key = priv->encr_keys[key_idx];

	netdev_dbg(usbdev->net, "%s(): %i:%i\n", __func__, key_idx, key.len);

	if (key.len == 0)
		return 0;

	return add_wep_key(usbdev, key.material, key.len, key_idx);
}

static void restore_keys(struct usbnet *usbdev)
{
	int i;

	for (i = 0; i < 4; i++)
		restore_key(usbdev, i);
}

static void clear_key(struct rndis_wlan_private *priv, u8 idx)
{
	memset(&priv->encr_keys[idx], 0, sizeof(priv->encr_keys[idx]));
}

/* remove_key is for both wep and wpa */
static int remove_key(struct usbnet *usbdev, u8 index, const u8 *bssid)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct ndis_80211_remove_key remove_key;
	__le32 keyindex;
	bool is_wpa;
	int ret;

	if (index >= RNDIS_WLAN_NUM_KEYS)
		return -ENOENT;

	if (priv->encr_keys[index].len == 0)
		return 0;

	is_wpa = is_wpa_key(priv, index);

	netdev_dbg(usbdev->net, "%s(): %i:%s:%i\n",
		   __func__, index, is_wpa ? "wpa" : "wep",
		   priv->encr_keys[index].len);

	clear_key(priv, index);

	if (is_wpa) {
		remove_key.size = cpu_to_le32(sizeof(remove_key));
		remove_key.index = cpu_to_le32(index);
		if (bssid) {
			/* pairwise key */
			if (!is_broadcast_ether_addr(bssid))
				remove_key.index |=
					NDIS_80211_ADDKEY_PAIRWISE_KEY;
			memcpy(remove_key.bssid, bssid,
					sizeof(remove_key.bssid));
		} else
			memset(remove_key.bssid, 0xff,
						sizeof(remove_key.bssid));

		ret = rndis_set_oid(usbdev,
				RNDIS_OID_802_11_REMOVE_KEY,
				&remove_key, sizeof(remove_key));
		if (ret != 0)
			return ret;
	} else {
		keyindex = cpu_to_le32(index);
		ret = rndis_set_oid(usbdev,
				RNDIS_OID_802_11_REMOVE_WEP,
				&keyindex, sizeof(keyindex));
		if (ret != 0) {
			netdev_warn(usbdev->net,
				    "removing encryption key %d failed (%08X)\n",
				    index, ret);
			return ret;
		}
	}

	/* if it is transmit key, disable encryption */
	if (index == priv->encr_tx_key_index)
		set_encr_mode(usbdev, RNDIS_WLAN_ALG_NONE, RNDIS_WLAN_ALG_NONE);

	return 0;
}

static void set_multicast_list(struct usbnet *usbdev)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct netdev_hw_addr *ha;
	__le32 filter, basefilter;
	int ret;
	char *mc_addrs = NULL;
	int mc_count;

	basefilter = filter = cpu_to_le32(RNDIS_PACKET_TYPE_DIRECTED |
					  RNDIS_PACKET_TYPE_BROADCAST);

	if (usbdev->net->flags & IFF_PROMISC) {
		filter |= cpu_to_le32(RNDIS_PACKET_TYPE_PROMISCUOUS |
				      RNDIS_PACKET_TYPE_ALL_LOCAL);
	} else if (usbdev->net->flags & IFF_ALLMULTI) {
		filter |= cpu_to_le32(RNDIS_PACKET_TYPE_ALL_MULTICAST);
	}

	if (filter != basefilter)
		goto set_filter;

	/*
	 * mc_list should be accessed holding the lock, so copy addresses to
	 * local buffer first.
	 */
	netif_addr_lock_bh(usbdev->net);
	mc_count = netdev_mc_count(usbdev->net);
	if (mc_count > priv->multicast_size) {
		filter |= cpu_to_le32(RNDIS_PACKET_TYPE_ALL_MULTICAST);
	} else if (mc_count) {
		int i = 0;

		mc_addrs = kmalloc_array(mc_count, ETH_ALEN, GFP_ATOMIC);
		if (!mc_addrs) {
			netif_addr_unlock_bh(usbdev->net);
			return;
		}

		netdev_for_each_mc_addr(ha, usbdev->net)
			memcpy(mc_addrs + i++ * ETH_ALEN,
			       ha->addr, ETH_ALEN);
	}
	netif_addr_unlock_bh(usbdev->net);

	if (filter != basefilter)
		goto set_filter;

	if (mc_count) {
		ret = rndis_set_oid(usbdev,
				RNDIS_OID_802_3_MULTICAST_LIST,
				mc_addrs, mc_count * ETH_ALEN);
		kfree(mc_addrs);
		if (ret == 0)
			filter |= cpu_to_le32(RNDIS_PACKET_TYPE_MULTICAST);
		else
			filter |= cpu_to_le32(RNDIS_PACKET_TYPE_ALL_MULTICAST);

		netdev_dbg(usbdev->net, "RNDIS_OID_802_3_MULTICAST_LIST(%d, max: %d) -> %d\n",
			   mc_count, priv->multicast_size, ret);
	}

set_filter:
	ret = rndis_set_oid(usbdev, RNDIS_OID_GEN_CURRENT_PACKET_FILTER, &filter,
							sizeof(filter));
	if (ret < 0) {
		netdev_warn(usbdev->net, "couldn't set packet filter: %08x\n",
			    le32_to_cpu(filter));
	}

	netdev_dbg(usbdev->net, "RNDIS_OID_GEN_CURRENT_PACKET_FILTER(%08x) -> %d\n",
		   le32_to_cpu(filter), ret);
}

#ifdef DEBUG
static void debug_print_pmkids(struct usbnet *usbdev,
				struct ndis_80211_pmkid *pmkids,
				const char *func_str)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	int i, len, count, max_pmkids, entry_len;

	max_pmkids = priv->wdev.wiphy->max_num_pmkids;
	len = le32_to_cpu(pmkids->length);
	count = le32_to_cpu(pmkids->bssid_info_count);

	entry_len = (count > 0) ? (len - sizeof(*pmkids)) / count : -1;

	netdev_dbg(usbdev->net, "%s(): %d PMKIDs (data len: %d, entry len: "
				"%d)\n", func_str, count, len, entry_len);

	if (count > max_pmkids)
		count = max_pmkids;

	for (i = 0; i < count; i++) {
		u32 *tmp = (u32 *)pmkids->bssid_info[i].pmkid;

		netdev_dbg(usbdev->net, "%s():  bssid: %pM, "
				"pmkid: %08X:%08X:%08X:%08X\n",
				func_str, pmkids->bssid_info[i].bssid,
				cpu_to_be32(tmp[0]), cpu_to_be32(tmp[1]),
				cpu_to_be32(tmp[2]), cpu_to_be32(tmp[3]));
	}
}
#else
static void debug_print_pmkids(struct usbnet *usbdev,
				struct ndis_80211_pmkid *pmkids,
				const char *func_str)
{
	return;
}
#endif

static struct ndis_80211_pmkid *get_device_pmkids(struct usbnet *usbdev)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct ndis_80211_pmkid *pmkids;
	int len, ret, max_pmkids;

	max_pmkids = priv->wdev.wiphy->max_num_pmkids;
	len = sizeof(*pmkids) + max_pmkids * sizeof(pmkids->bssid_info[0]);

	pmkids = kzalloc(len, GFP_KERNEL);
	if (!pmkids)
		return ERR_PTR(-ENOMEM);

	pmkids->length = cpu_to_le32(len);
	pmkids->bssid_info_count = cpu_to_le32(max_pmkids);

	ret = rndis_query_oid(usbdev, RNDIS_OID_802_11_PMKID,
			pmkids, &len);
	if (ret < 0) {
		netdev_dbg(usbdev->net, "%s(): RNDIS_OID_802_11_PMKID(%d, %d)"
				" -> %d\n", __func__, len, max_pmkids, ret);

		kfree(pmkids);
		return ERR_PTR(ret);
	}

	if (le32_to_cpu(pmkids->bssid_info_count) > max_pmkids)
		pmkids->bssid_info_count = cpu_to_le32(max_pmkids);

	debug_print_pmkids(usbdev, pmkids, __func__);

	return pmkids;
}

static int set_device_pmkids(struct usbnet *usbdev,
				struct ndis_80211_pmkid *pmkids)
{
	int ret, len, num_pmkids;

	num_pmkids = le32_to_cpu(pmkids->bssid_info_count);
	len = sizeof(*pmkids) + num_pmkids * sizeof(pmkids->bssid_info[0]);
	pmkids->length = cpu_to_le32(len);

	debug_print_pmkids(usbdev, pmkids, __func__);

	ret = rndis_set_oid(usbdev, RNDIS_OID_802_11_PMKID, pmkids,
			    le32_to_cpu(pmkids->length));
	if (ret < 0) {
		netdev_dbg(usbdev->net, "%s(): RNDIS_OID_802_11_PMKID(%d, %d) -> %d"
				"\n", __func__, len, num_pmkids, ret);
	}

	kfree(pmkids);
	return ret;
}

static struct ndis_80211_pmkid *remove_pmkid(struct usbnet *usbdev,
						struct ndis_80211_pmkid *pmkids,
						struct cfg80211_pmksa *pmksa,
						int max_pmkids)
{
	int i, newlen, err;
	unsigned int count;

	count = le32_to_cpu(pmkids->bssid_info_count);

	if (count > max_pmkids)
		count = max_pmkids;

	for (i = 0; i < count; i++)
		if (ether_addr_equal(pmkids->bssid_info[i].bssid,
				     pmksa->bssid))
			break;

	/* pmkid not found */
	if (i == count) {
		netdev_dbg(usbdev->net, "%s(): bssid not found (%pM)\n",
					__func__, pmksa->bssid);
		err = -ENOENT;
		goto error;
	}

	for (; i + 1 < count; i++)
		pmkids->bssid_info[i] = pmkids->bssid_info[i + 1];

	count--;
	newlen = sizeof(*pmkids) + count * sizeof(pmkids->bssid_info[0]);

	pmkids->length = cpu_to_le32(newlen);
	pmkids->bssid_info_count = cpu_to_le32(count);

	return pmkids;
error:
	kfree(pmkids);
	return ERR_PTR(err);
}

static struct ndis_80211_pmkid *update_pmkid(struct usbnet *usbdev,
						struct ndis_80211_pmkid *pmkids,
						struct cfg80211_pmksa *pmksa,
						int max_pmkids)
{
	struct ndis_80211_pmkid *new_pmkids;
	int i, err, newlen;
	unsigned int count;

	count = le32_to_cpu(pmkids->bssid_info_count);

	if (count > max_pmkids)
		count = max_pmkids;

	/* update with new pmkid */
	for (i = 0; i < count; i++) {
		if (!ether_addr_equal(pmkids->bssid_info[i].bssid,
				      pmksa->bssid))
			continue;

		memcpy(pmkids->bssid_info[i].pmkid, pmksa->pmkid,
								WLAN_PMKID_LEN);

		return pmkids;
	}

	/* out of space, return error */
	if (i == max_pmkids) {
		netdev_dbg(usbdev->net, "%s(): out of space\n", __func__);
		err = -ENOSPC;
		goto error;
	}

	/* add new pmkid */
	newlen = sizeof(*pmkids) + (count + 1) * sizeof(pmkids->bssid_info[0]);

	new_pmkids = krealloc(pmkids, newlen, GFP_KERNEL);
	if (!new_pmkids) {
		err = -ENOMEM;
		goto error;
	}
	pmkids = new_pmkids;

	pmkids->length = cpu_to_le32(newlen);
	pmkids->bssid_info_count = cpu_to_le32(count + 1);

	memcpy(pmkids->bssid_info[count].bssid, pmksa->bssid, ETH_ALEN);
	memcpy(pmkids->bssid_info[count].pmkid, pmksa->pmkid, WLAN_PMKID_LEN);

	return pmkids;
error:
	kfree(pmkids);
	return ERR_PTR(err);
}

/*
 * cfg80211 ops
 */
static int rndis_change_virtual_intf(struct wiphy *wiphy,
					struct net_device *dev,
					enum nl80211_iftype type, u32 *flags,
					struct vif_params *params)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;
	int mode;

	switch (type) {
	case NL80211_IFTYPE_ADHOC:
		mode = NDIS_80211_INFRA_ADHOC;
		break;
	case NL80211_IFTYPE_STATION:
		mode = NDIS_80211_INFRA_INFRA;
		break;
	default:
		return -EINVAL;
	}

	priv->wdev.iftype = type;

	return set_infra_mode(usbdev, mode);
}

static int rndis_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;
	int err;

	if (changed & WIPHY_PARAM_FRAG_THRESHOLD) {
		err = set_frag_threshold(usbdev, wiphy->frag_threshold);
		if (err < 0)
			return err;
	}

	if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
		err = set_rts_threshold(usbdev, wiphy->rts_threshold);
		if (err < 0)
			return err;
	}

	return 0;
}

static int rndis_set_tx_power(struct wiphy *wiphy,
			      struct wireless_dev *wdev,
			      enum nl80211_tx_power_setting type,
			      int mbm)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;

	netdev_dbg(usbdev->net, "%s(): type:0x%x mbm:%i\n",
		   __func__, type, mbm);

	if (mbm < 0 || (mbm % 100))
		return -ENOTSUPP;

	/* Device doesn't support changing txpower after initialization, only
	 * turn off/on radio. Support 'auto' mode and setting same dBm that is
	 * currently used.
	 */
	if (type == NL80211_TX_POWER_AUTOMATIC ||
	    MBM_TO_DBM(mbm) == get_bcm4320_power_dbm(priv)) {
		if (!priv->radio_on)
			disassociate(usbdev, true); /* turn on radio */

		return 0;
	}

	return -ENOTSUPP;
}

static int rndis_get_tx_power(struct wiphy *wiphy,
			      struct wireless_dev *wdev,
			      int *dbm)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;

	*dbm = get_bcm4320_power_dbm(priv);

	netdev_dbg(usbdev->net, "%s(): dbm:%i\n", __func__, *dbm);

	return 0;
}

#define SCAN_DELAY_JIFFIES (6 * HZ)
static int rndis_scan(struct wiphy *wiphy,
			struct cfg80211_scan_request *request)
{
	struct net_device *dev = request->wdev->netdev;
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	int ret;
	int delay = SCAN_DELAY_JIFFIES;

	netdev_dbg(usbdev->net, "cfg80211.scan\n");

	/* Get current bssid list from device before new scan, as new scan
	 * clears internal bssid list.
	 */
	rndis_check_bssid_list(usbdev, NULL, NULL);

	if (priv->scan_request && priv->scan_request != request)
		return -EBUSY;

	priv->scan_request = request;

	ret = rndis_start_bssid_list_scan(usbdev);
	if (ret == 0) {
		if (priv->device_type == RNDIS_BCM4320A)
			delay = HZ;

		/* Wait before retrieving scan results from device */
		queue_delayed_work(priv->workqueue, &priv->scan_work, delay);
	}

	return ret;
}

static bool rndis_bss_info_update(struct usbnet *usbdev,
				  struct ndis_80211_bssid_ex *bssid)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct ieee80211_channel *channel;
	struct cfg80211_bss *bss;
	s32 signal;
	u64 timestamp;
	u16 capability;
	u16 beacon_interval;
	struct ndis_80211_fixed_ies *fixed;
	int ie_len, bssid_len;
	u8 *ie;

	netdev_dbg(usbdev->net, " found bssid: '%.32s' [%pM], len: %d\n",
		   bssid->ssid.essid, bssid->mac, le32_to_cpu(bssid->length));

	/* parse bssid structure */
	bssid_len = le32_to_cpu(bssid->length);

	if (bssid_len < sizeof(struct ndis_80211_bssid_ex) +
			sizeof(struct ndis_80211_fixed_ies))
		return NULL;

	fixed = (struct ndis_80211_fixed_ies *)bssid->ies;

	ie = (void *)(bssid->ies + sizeof(struct ndis_80211_fixed_ies));
	ie_len = min(bssid_len - (int)sizeof(*bssid),
					(int)le32_to_cpu(bssid->ie_length));
	ie_len -= sizeof(struct ndis_80211_fixed_ies);
	if (ie_len < 0)
		return NULL;

	/* extract data for cfg80211_inform_bss */
	channel = ieee80211_get_channel(priv->wdev.wiphy,
			KHZ_TO_MHZ(le32_to_cpu(bssid->config.ds_config)));
	if (!channel)
		return NULL;

	signal = level_to_qual(le32_to_cpu(bssid->rssi));
	timestamp = le64_to_cpu(*(__le64 *)fixed->timestamp);
	capability = le16_to_cpu(fixed->capabilities);
	beacon_interval = le16_to_cpu(fixed->beacon_interval);

	bss = cfg80211_inform_bss(priv->wdev.wiphy, channel, bssid->mac,
		timestamp, capability, beacon_interval, ie, ie_len, signal,
		GFP_KERNEL);
	cfg80211_put_bss(priv->wdev.wiphy, bss);

	return (bss != NULL);
}

static struct ndis_80211_bssid_ex *next_bssid_list_item(
					struct ndis_80211_bssid_ex *bssid,
					int *bssid_len, void *buf, int len)
{
	void *buf_end, *bssid_end;

	buf_end = (char *)buf + len;
	bssid_end = (char *)bssid + *bssid_len;

	if ((int)(buf_end - bssid_end) < sizeof(bssid->length)) {
		*bssid_len = 0;
		return NULL;
	} else {
		bssid = (void *)((char *)bssid + *bssid_len);
		*bssid_len = le32_to_cpu(bssid->length);
		return bssid;
	}
}

static bool check_bssid_list_item(struct ndis_80211_bssid_ex *bssid,
				  int bssid_len, void *buf, int len)
{
	void *buf_end, *bssid_end;

	if (!bssid || bssid_len <= 0 || bssid_len > len)
		return false;

	buf_end = (char *)buf + len;
	bssid_end = (char *)bssid + bssid_len;

	return (int)(buf_end - bssid_end) >= 0 && (int)(bssid_end - buf) >= 0;
}

static int rndis_check_bssid_list(struct usbnet *usbdev, u8 *match_bssid,
					bool *matched)
{
	void *buf = NULL;
	struct ndis_80211_bssid_list_ex *bssid_list;
	struct ndis_80211_bssid_ex *bssid;
	int ret = -EINVAL, len, count, bssid_len, real_count, new_len;

	netdev_dbg(usbdev->net, "%s()\n", __func__);

	len = CONTROL_BUFFER_SIZE;
resize_buf:
	buf = kzalloc(len, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	/* BSSID-list might have got bigger last time we checked, keep
	 * resizing until it won't get any bigger.
	 */
	new_len = len;
	ret = rndis_query_oid(usbdev, RNDIS_OID_802_11_BSSID_LIST,
			      buf, &new_len);
	if (ret != 0 || new_len < sizeof(struct ndis_80211_bssid_list_ex))
		goto out;

	if (new_len > len) {
		len = new_len;
		kfree(buf);
		goto resize_buf;
	}

	len = new_len;

	bssid_list = buf;
	count = le32_to_cpu(bssid_list->num_items);
	real_count = 0;
	netdev_dbg(usbdev->net, "%s(): buflen: %d\n", __func__, len);

	bssid_len = 0;
	bssid = next_bssid_list_item(bssid_list->bssid, &bssid_len, buf, len);

	/* Device returns incorrect 'num_items'. Workaround by ignoring the
	 * received 'num_items' and walking through full bssid buffer instead.
	 */
	while (check_bssid_list_item(bssid, bssid_len, buf, len)) {
		if (rndis_bss_info_update(usbdev, bssid) && match_bssid &&
		    matched) {
			if (ether_addr_equal(bssid->mac, match_bssid))
				*matched = true;
		}

		real_count++;
		bssid = next_bssid_list_item(bssid, &bssid_len, buf, len);
	}

	netdev_dbg(usbdev->net, "%s(): num_items from device: %d, really found:"
				" %d\n", __func__, count, real_count);

out:
	kfree(buf);
	return ret;
}

static void rndis_get_scan_results(struct work_struct *work)
{
	struct rndis_wlan_private *priv =
		container_of(work, struct rndis_wlan_private, scan_work.work);
	struct usbnet *usbdev = priv->usbdev;
	int ret;

	netdev_dbg(usbdev->net, "get_scan_results\n");

	if (!priv->scan_request)
		return;

	ret = rndis_check_bssid_list(usbdev, NULL, NULL);

	cfg80211_scan_done(priv->scan_request, ret < 0);

	priv->scan_request = NULL;
}

static int rndis_connect(struct wiphy *wiphy, struct net_device *dev,
					struct cfg80211_connect_params *sme)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;
	struct ieee80211_channel *channel = sme->channel;
	struct ndis_80211_ssid ssid;
	int pairwise = RNDIS_WLAN_ALG_NONE;
	int groupwise = RNDIS_WLAN_ALG_NONE;
	int keymgmt = RNDIS_WLAN_KEY_MGMT_NONE;
	int length, i, ret, chan = -1;

	if (channel)
		chan = ieee80211_frequency_to_channel(channel->center_freq);

	groupwise = rndis_cipher_to_alg(sme->crypto.cipher_group);
	for (i = 0; i < sme->crypto.n_ciphers_pairwise; i++)
		pairwise |=
			rndis_cipher_to_alg(sme->crypto.ciphers_pairwise[i]);

	if (sme->crypto.n_ciphers_pairwise > 0 &&
			pairwise == RNDIS_WLAN_ALG_NONE) {
		netdev_err(usbdev->net, "Unsupported pairwise cipher\n");
		return -ENOTSUPP;
	}

	for (i = 0; i < sme->crypto.n_akm_suites; i++)
		keymgmt |=
			rndis_akm_suite_to_key_mgmt(sme->crypto.akm_suites[i]);

	if (sme->crypto.n_akm_suites > 0 &&
			keymgmt == RNDIS_WLAN_KEY_MGMT_NONE) {
		netdev_err(usbdev->net, "Invalid keymgmt\n");
		return -ENOTSUPP;
	}

	netdev_dbg(usbdev->net, "cfg80211.connect('%.32s':[%pM]:%d:[%d,0x%x:0x%x]:[0x%x:0x%x]:0x%x)\n",
		   sme->ssid, sme->bssid, chan,
		   sme->privacy, sme->crypto.wpa_versions, sme->auth_type,
		   groupwise, pairwise, keymgmt);

	if (is_associated(usbdev))
		disassociate(usbdev, false);

	ret = set_infra_mode(usbdev, NDIS_80211_INFRA_INFRA);
	if (ret < 0) {
		netdev_dbg(usbdev->net, "connect: set_infra_mode failed, %d\n",
			   ret);
		goto err_turn_radio_on;
	}

	ret = set_auth_mode(usbdev, sme->crypto.wpa_versions, sme->auth_type,
								keymgmt);
	if (ret < 0) {
		netdev_dbg(usbdev->net, "connect: set_auth_mode failed, %d\n",
			   ret);
		goto err_turn_radio_on;
	}

	set_priv_filter(usbdev);

	ret = set_encr_mode(usbdev, pairwise, groupwise);
	if (ret < 0) {
		netdev_dbg(usbdev->net, "connect: set_encr_mode failed, %d\n",
			   ret);
		goto err_turn_radio_on;
	}

	if (channel) {
		ret = set_channel(usbdev, chan);
		if (ret < 0) {
			netdev_dbg(usbdev->net, "connect: set_channel failed, %d\n",
				   ret);
			goto err_turn_radio_on;
		}
	}

	if (sme->key && ((groupwise | pairwise) & RNDIS_WLAN_ALG_WEP)) {
		priv->encr_tx_key_index = sme->key_idx;
		ret = add_wep_key(usbdev, sme->key, sme->key_len, sme->key_idx);
		if (ret < 0) {
			netdev_dbg(usbdev->net, "connect: add_wep_key failed, %d (%d, %d)\n",
				   ret, sme->key_len, sme->key_idx);
			goto err_turn_radio_on;
		}
	}

	if (sme->bssid && !is_zero_ether_addr(sme->bssid) &&
				!is_broadcast_ether_addr(sme->bssid)) {
		ret = set_bssid(usbdev, sme->bssid);
		if (ret < 0) {
			netdev_dbg(usbdev->net, "connect: set_bssid failed, %d\n",
				   ret);
			goto err_turn_radio_on;
		}
	} else
		clear_bssid(usbdev);

	length = sme->ssid_len;
	if (length > NDIS_802_11_LENGTH_SSID)
		length = NDIS_802_11_LENGTH_SSID;

	memset(&ssid, 0, sizeof(ssid));
	ssid.length = cpu_to_le32(length);
	memcpy(ssid.essid, sme->ssid, length);

	/* Pause and purge rx queue, so we don't pass packets before
	 * 'media connect'-indication.
	 */
	usbnet_pause_rx(usbdev);
	usbnet_purge_paused_rxq(usbdev);

	ret = set_essid(usbdev, &ssid);
	if (ret < 0)
		netdev_dbg(usbdev->net, "connect: set_essid failed, %d\n", ret);
	return ret;

err_turn_radio_on:
	disassociate(usbdev, true);

	return ret;
}

static int rndis_disconnect(struct wiphy *wiphy, struct net_device *dev,
								u16 reason_code)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;

	netdev_dbg(usbdev->net, "cfg80211.disconnect(%d)\n", reason_code);

	priv->connected = false;
	memset(priv->bssid, 0, ETH_ALEN);

	return deauthenticate(usbdev);
}

static int rndis_join_ibss(struct wiphy *wiphy, struct net_device *dev,
					struct cfg80211_ibss_params *params)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;
	struct ieee80211_channel *channel = params->chandef.chan;
	struct ndis_80211_ssid ssid;
	enum nl80211_auth_type auth_type;
	int ret, alg, length, chan = -1;

	if (channel)
		chan = ieee80211_frequency_to_channel(channel->center_freq);

	/* TODO: How to handle ad-hoc encryption?
	 * connect() has *key, join_ibss() doesn't. RNDIS requires key to be
	 * pre-shared for encryption (open/shared/wpa), is key set before
	 * join_ibss? Which auth_type to use (not in params)? What about WPA?
	 */
	if (params->privacy) {
		auth_type = NL80211_AUTHTYPE_SHARED_KEY;
		alg = RNDIS_WLAN_ALG_WEP;
	} else {
		auth_type = NL80211_AUTHTYPE_OPEN_SYSTEM;
		alg = RNDIS_WLAN_ALG_NONE;
	}

	netdev_dbg(usbdev->net, "cfg80211.join_ibss('%.32s':[%pM]:%d:%d)\n",
		   params->ssid, params->bssid, chan, params->privacy);

	if (is_associated(usbdev))
		disassociate(usbdev, false);

	ret = set_infra_mode(usbdev, NDIS_80211_INFRA_ADHOC);
	if (ret < 0) {
		netdev_dbg(usbdev->net, "join_ibss: set_infra_mode failed, %d\n",
			   ret);
		goto err_turn_radio_on;
	}

	ret = set_auth_mode(usbdev, 0, auth_type, RNDIS_WLAN_KEY_MGMT_NONE);
	if (ret < 0) {
		netdev_dbg(usbdev->net, "join_ibss: set_auth_mode failed, %d\n",
			   ret);
		goto err_turn_radio_on;
	}

	set_priv_filter(usbdev);

	ret = set_encr_mode(usbdev, alg, RNDIS_WLAN_ALG_NONE);
	if (ret < 0) {
		netdev_dbg(usbdev->net, "join_ibss: set_encr_mode failed, %d\n",
			   ret);
		goto err_turn_radio_on;
	}

	if (channel) {
		ret = set_channel(usbdev, chan);
		if (ret < 0) {
			netdev_dbg(usbdev->net, "join_ibss: set_channel failed, %d\n",
				   ret);
			goto err_turn_radio_on;
		}
	}

	if (params->bssid && !is_zero_ether_addr(params->bssid) &&
				!is_broadcast_ether_addr(params->bssid)) {
		ret = set_bssid(usbdev, params->bssid);
		if (ret < 0) {
			netdev_dbg(usbdev->net, "join_ibss: set_bssid failed, %d\n",
				   ret);
			goto err_turn_radio_on;
		}
	} else
		clear_bssid(usbdev);

	length = params->ssid_len;
	if (length > NDIS_802_11_LENGTH_SSID)
		length = NDIS_802_11_LENGTH_SSID;

	memset(&ssid, 0, sizeof(ssid));
	ssid.length = cpu_to_le32(length);
	memcpy(ssid.essid, params->ssid, length);

	/* Don't need to pause rx queue for ad-hoc. */
	usbnet_purge_paused_rxq(usbdev);
	usbnet_resume_rx(usbdev);

	ret = set_essid(usbdev, &ssid);
	if (ret < 0)
		netdev_dbg(usbdev->net, "join_ibss: set_essid failed, %d\n",
			   ret);
	return ret;

err_turn_radio_on:
	disassociate(usbdev, true);

	return ret;
}

static int rndis_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;

	netdev_dbg(usbdev->net, "cfg80211.leave_ibss()\n");

	priv->connected = false;
	memset(priv->bssid, 0, ETH_ALEN);

	return deauthenticate(usbdev);
}

static int rndis_add_key(struct wiphy *wiphy, struct net_device *netdev,
			 u8 key_index, bool pairwise, const u8 *mac_addr,
			 struct key_params *params)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;
	__le32 flags;

	netdev_dbg(usbdev->net, "%s(%i, %pM, %08x)\n",
		   __func__, key_index, mac_addr, params->cipher);

	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		return add_wep_key(usbdev, params->key, params->key_len,
								key_index);
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
		flags = 0;

		if (params->seq && params->seq_len > 0)
			flags |= NDIS_80211_ADDKEY_SET_INIT_RECV_SEQ;
		if (mac_addr)
			flags |= NDIS_80211_ADDKEY_PAIRWISE_KEY |
					NDIS_80211_ADDKEY_TRANSMIT_KEY;

		return add_wpa_key(usbdev, params->key, params->key_len,
				key_index, mac_addr, params->seq,
				params->seq_len, params->cipher, flags);
	default:
		netdev_dbg(usbdev->net, "%s(): unsupported cipher %08x\n",
			   __func__, params->cipher);
		return -ENOTSUPP;
	}
}

static int rndis_del_key(struct wiphy *wiphy, struct net_device *netdev,
			 u8 key_index, bool pairwise, const u8 *mac_addr)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;

	netdev_dbg(usbdev->net, "%s(%i, %pM)\n", __func__, key_index, mac_addr);

	return remove_key(usbdev, key_index, mac_addr);
}

static int rndis_set_default_key(struct wiphy *wiphy, struct net_device *netdev,
				 u8 key_index, bool unicast, bool multicast)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;
	struct rndis_wlan_encr_key key;

	netdev_dbg(usbdev->net, "%s(%i)\n", __func__, key_index);

	if (key_index >= RNDIS_WLAN_NUM_KEYS)
		return -ENOENT;

	priv->encr_tx_key_index = key_index;

	if (is_wpa_key(priv, key_index))
		return 0;

	key = priv->encr_keys[key_index];

	return add_wep_key(usbdev, key.material, key.len, key_index);
}

static void rndis_fill_station_info(struct usbnet *usbdev,
						struct station_info *sinfo)
{
	__le32 linkspeed, rssi;
	int ret, len;

	memset(sinfo, 0, sizeof(*sinfo));

	len = sizeof(linkspeed);
	ret = rndis_query_oid(usbdev, RNDIS_OID_GEN_LINK_SPEED, &linkspeed, &len);
	if (ret == 0) {
		sinfo->txrate.legacy = le32_to_cpu(linkspeed) / 1000;
		sinfo->filled |= STATION_INFO_TX_BITRATE;
	}

	len = sizeof(rssi);
	ret = rndis_query_oid(usbdev, RNDIS_OID_802_11_RSSI,
			      &rssi, &len);
	if (ret == 0) {
		sinfo->signal = level_to_qual(le32_to_cpu(rssi));
		sinfo->filled |= STATION_INFO_SIGNAL;
	}
}

static int rndis_get_station(struct wiphy *wiphy, struct net_device *dev,
			     const u8 *mac, struct station_info *sinfo)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;

	if (!ether_addr_equal(priv->bssid, mac))
		return -ENOENT;

	rndis_fill_station_info(usbdev, sinfo);

	return 0;
}

static int rndis_dump_station(struct wiphy *wiphy, struct net_device *dev,
			       int idx, u8 *mac, struct station_info *sinfo)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;

	if (idx != 0)
		return -ENOENT;

	memcpy(mac, priv->bssid, ETH_ALEN);

	rndis_fill_station_info(usbdev, sinfo);

	return 0;
}

static int rndis_set_pmksa(struct wiphy *wiphy, struct net_device *netdev,
				struct cfg80211_pmksa *pmksa)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;
	struct ndis_80211_pmkid *pmkids;
	u32 *tmp = (u32 *)pmksa->pmkid;

	netdev_dbg(usbdev->net, "%s(%pM, %08X:%08X:%08X:%08X)\n", __func__,
			pmksa->bssid,
			cpu_to_be32(tmp[0]), cpu_to_be32(tmp[1]),
			cpu_to_be32(tmp[2]), cpu_to_be32(tmp[3]));

	pmkids = get_device_pmkids(usbdev);
	if (IS_ERR(pmkids)) {
		/* couldn't read PMKID cache from device */
		return PTR_ERR(pmkids);
	}

	pmkids = update_pmkid(usbdev, pmkids, pmksa, wiphy->max_num_pmkids);
	if (IS_ERR(pmkids)) {
		/* not found, list full, etc */
		return PTR_ERR(pmkids);
	}

	return set_device_pmkids(usbdev, pmkids);
}

static int rndis_del_pmksa(struct wiphy *wiphy, struct net_device *netdev,
				struct cfg80211_pmksa *pmksa)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;
	struct ndis_80211_pmkid *pmkids;
	u32 *tmp = (u32 *)pmksa->pmkid;

	netdev_dbg(usbdev->net, "%s(%pM, %08X:%08X:%08X:%08X)\n", __func__,
			pmksa->bssid,
			cpu_to_be32(tmp[0]), cpu_to_be32(tmp[1]),
			cpu_to_be32(tmp[2]), cpu_to_be32(tmp[3]));

	pmkids = get_device_pmkids(usbdev);
	if (IS_ERR(pmkids)) {
		/* Couldn't read PMKID cache from device */
		return PTR_ERR(pmkids);
	}

	pmkids = remove_pmkid(usbdev, pmkids, pmksa, wiphy->max_num_pmkids);
	if (IS_ERR(pmkids)) {
		/* not found, etc */
		return PTR_ERR(pmkids);
	}

	return set_device_pmkids(usbdev, pmkids);
}

static int rndis_flush_pmksa(struct wiphy *wiphy, struct net_device *netdev)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;
	struct ndis_80211_pmkid pmkid;

	netdev_dbg(usbdev->net, "%s()\n", __func__);

	memset(&pmkid, 0, sizeof(pmkid));

	pmkid.length = cpu_to_le32(sizeof(pmkid));
	pmkid.bssid_info_count = cpu_to_le32(0);

	return rndis_set_oid(usbdev, RNDIS_OID_802_11_PMKID,
			     &pmkid, sizeof(pmkid));
}

static int rndis_set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
				bool enabled, int timeout)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);
	struct usbnet *usbdev = priv->usbdev;
	int power_mode;
	__le32 mode;
	int ret;

	if (priv->device_type != RNDIS_BCM4320B)
		return -ENOTSUPP;

	netdev_dbg(usbdev->net, "%s(): %s, %d\n", __func__,
				enabled ? "enabled" : "disabled",
				timeout);

	if (enabled)
		power_mode = NDIS_80211_POWER_MODE_FAST_PSP;
	else
		power_mode = NDIS_80211_POWER_MODE_CAM;

	if (power_mode == priv->power_mode)
		return 0;

	priv->power_mode = power_mode;

	mode = cpu_to_le32(power_mode);
	ret = rndis_set_oid(usbdev, RNDIS_OID_802_11_POWER_MODE,
			    &mode, sizeof(mode));

	netdev_dbg(usbdev->net, "%s(): RNDIS_OID_802_11_POWER_MODE -> %d\n",
				__func__, ret);

	return ret;
}

static int rndis_set_cqm_rssi_config(struct wiphy *wiphy,
					struct net_device *dev,
					s32 rssi_thold, u32 rssi_hyst)
{
	struct rndis_wlan_private *priv = wiphy_priv(wiphy);

	priv->cqm_rssi_thold = rssi_thold;
	priv->cqm_rssi_hyst = rssi_hyst;
	priv->last_cqm_event_rssi = 0;

	return 0;
}

static void rndis_wlan_craft_connected_bss(struct usbnet *usbdev, u8 *bssid,
					   struct ndis_80211_assoc_info *info)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct ieee80211_channel *channel;
	struct ndis_80211_ssid ssid;
	struct cfg80211_bss *bss;
	s32 signal;
	u64 timestamp;
	u16 capability;
	u32 beacon_period = 0;
	__le32 rssi;
	u8 ie_buf[34];
	int len, ret, ie_len;

	/* Get signal quality, in case of error use rssi=0 and ignore error. */
	len = sizeof(rssi);
	rssi = 0;
	ret = rndis_query_oid(usbdev, RNDIS_OID_802_11_RSSI,
			      &rssi, &len);
	signal = level_to_qual(le32_to_cpu(rssi));

	netdev_dbg(usbdev->net, "%s(): RNDIS_OID_802_11_RSSI -> %d, "
		   "rssi:%d, qual: %d\n", __func__, ret, le32_to_cpu(rssi),
		   level_to_qual(le32_to_cpu(rssi)));

	/* Get AP capabilities */
	if (info) {
		capability = le16_to_cpu(info->resp_ie.capa);
	} else {
		/* Set atleast ESS/IBSS capability */
		capability = (priv->infra_mode == NDIS_80211_INFRA_INFRA) ?
				WLAN_CAPABILITY_ESS : WLAN_CAPABILITY_IBSS;
	}

	/* Get channel and beacon interval */
	channel = get_current_channel(usbdev, &beacon_period);
	if (!channel) {
		netdev_warn(usbdev->net, "%s(): could not get channel.\n",
					__func__);
		return;
	}

	/* Get SSID, in case of error, use zero length SSID and ignore error. */
	len = sizeof(ssid);
	memset(&ssid, 0, sizeof(ssid));
	ret = rndis_query_oid(usbdev, RNDIS_OID_802_11_SSID,
			      &ssid, &len);
	netdev_dbg(usbdev->net, "%s(): RNDIS_OID_802_11_SSID -> %d, len: %d, ssid: "
				"'%.32s'\n", __func__, ret,
				le32_to_cpu(ssid.length), ssid.essid);

	if (le32_to_cpu(ssid.length) > 32)
		ssid.length = cpu_to_le32(32);

	ie_buf[0] = WLAN_EID_SSID;
	ie_buf[1] = le32_to_cpu(ssid.length);
	memcpy(&ie_buf[2], ssid.essid, le32_to_cpu(ssid.length));

	ie_len = le32_to_cpu(ssid.length) + 2;

	/* no tsf */
	timestamp = 0;

	netdev_dbg(usbdev->net, "%s(): channel:%d(freq), bssid:[%pM], tsf:%d, "
		"capa:%x, beacon int:%d, resp_ie(len:%d, essid:'%.32s'), "
		"signal:%d\n", __func__, (channel ? channel->center_freq : -1),
		bssid, (u32)timestamp, capability, beacon_period, ie_len,
		ssid.essid, signal);

	bss = cfg80211_inform_bss(priv->wdev.wiphy, channel, bssid,
		timestamp, capability, beacon_period, ie_buf, ie_len,
		signal, GFP_KERNEL);
	cfg80211_put_bss(priv->wdev.wiphy, bss);
}

/*
 * workers, indication handlers, device poller
 */
static void rndis_wlan_do_link_up_work(struct usbnet *usbdev)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct ndis_80211_assoc_info *info = NULL;
	u8 bssid[ETH_ALEN];
	unsigned int resp_ie_len, req_ie_len;
	unsigned int offset;
	u8 *req_ie, *resp_ie;
	int ret;
	bool roamed = false;
	bool match_bss;

	if (priv->infra_mode == NDIS_80211_INFRA_INFRA && priv->connected) {
		/* received media connect indication while connected, either
		 * device reassociated with same AP or roamed to new. */
		roamed = true;
	}

	req_ie_len = 0;
	resp_ie_len = 0;
	req_ie = NULL;
	resp_ie = NULL;

	if (priv->infra_mode == NDIS_80211_INFRA_INFRA) {
		info = kzalloc(CONTROL_BUFFER_SIZE, GFP_KERNEL);
		if (!info) {
			/* No memory? Try resume work later */
			set_bit(WORK_LINK_UP, &priv->work_pending);
			queue_work(priv->workqueue, &priv->work);
			return;
		}

		/* Get association info IEs from device. */
		ret = get_association_info(usbdev, info, CONTROL_BUFFER_SIZE);
		if (!ret) {
			req_ie_len = le32_to_cpu(info->req_ie_length);
			if (req_ie_len > CONTROL_BUFFER_SIZE)
				req_ie_len = CONTROL_BUFFER_SIZE;
			if (req_ie_len != 0) {
				offset = le32_to_cpu(info->offset_req_ies);

				if (offset > CONTROL_BUFFER_SIZE)
					offset = CONTROL_BUFFER_SIZE;

				req_ie = (u8 *)info + offset;

				if (offset + req_ie_len > CONTROL_BUFFER_SIZE)
					req_ie_len =
						CONTROL_BUFFER_SIZE - offset;
			}

			resp_ie_len = le32_to_cpu(info->resp_ie_length);
			if (resp_ie_len > CONTROL_BUFFER_SIZE)
				resp_ie_len = CONTROL_BUFFER_SIZE;
			if (resp_ie_len != 0) {
				offset = le32_to_cpu(info->offset_resp_ies);

				if (offset > CONTROL_BUFFER_SIZE)
					offset = CONTROL_BUFFER_SIZE;

				resp_ie = (u8 *)info + offset;

				if (offset + resp_ie_len > CONTROL_BUFFER_SIZE)
					resp_ie_len =
						CONTROL_BUFFER_SIZE - offset;
			}
		} else {
			/* Since rndis_wlan_craft_connected_bss() might use info
			 * later and expects info to contain valid data if
			 * non-null, free info and set NULL here.
			 */
			kfree(info);
			info = NULL;
		}
	} else if (WARN_ON(priv->infra_mode != NDIS_80211_INFRA_ADHOC))
		return;

	ret = get_bssid(usbdev, bssid);
	if (ret < 0)
		memset(bssid, 0, sizeof(bssid));

	netdev_dbg(usbdev->net, "link up work: [%pM]%s\n",
		   bssid, roamed ? " roamed" : "");

	/* Internal bss list in device should contain at least the currently
	 * connected bss and we can get it to cfg80211 with
	 * rndis_check_bssid_list().
	 *
	 * NDIS spec says: "If the device is associated, but the associated
	 *  BSSID is not in its BSSID scan list, then the driver must add an
	 *  entry for the BSSID at the end of the data that it returns in
	 *  response to query of RNDIS_OID_802_11_BSSID_LIST."
	 *
	 * NOTE: Seems to be true for BCM4320b variant, but not BCM4320a.
	 */
	match_bss = false;
	rndis_check_bssid_list(usbdev, bssid, &match_bss);

	if (!is_zero_ether_addr(bssid) && !match_bss) {
		/* Couldn't get bss from device, we need to manually craft bss
		 * for cfg80211.
		 */
		rndis_wlan_craft_connected_bss(usbdev, bssid, info);
	}

	if (priv->infra_mode == NDIS_80211_INFRA_INFRA) {
		if (!roamed)
			cfg80211_connect_result(usbdev->net, bssid, req_ie,
						req_ie_len, resp_ie,
						resp_ie_len, 0, GFP_KERNEL);
		else
			cfg80211_roamed(usbdev->net,
					get_current_channel(usbdev, NULL),
					bssid, req_ie, req_ie_len,
					resp_ie, resp_ie_len, GFP_KERNEL);
	} else if (priv->infra_mode == NDIS_80211_INFRA_ADHOC)
		cfg80211_ibss_joined(usbdev->net, bssid,
				     get_current_channel(usbdev, NULL),
				     GFP_KERNEL);

	kfree(info);

	priv->connected = true;
	memcpy(priv->bssid, bssid, ETH_ALEN);

	usbnet_resume_rx(usbdev);
	netif_carrier_on(usbdev->net);
}

static void rndis_wlan_do_link_down_work(struct usbnet *usbdev)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);

	if (priv->connected) {
		priv->connected = false;
		memset(priv->bssid, 0, ETH_ALEN);

		deauthenticate(usbdev);

		cfg80211_disconnected(usbdev->net, 0, NULL, 0, GFP_KERNEL);
	}

	netif_carrier_off(usbdev->net);
}

static void rndis_wlan_worker(struct work_struct *work)
{
	struct rndis_wlan_private *priv =
		container_of(work, struct rndis_wlan_private, work);
	struct usbnet *usbdev = priv->usbdev;

	if (test_and_clear_bit(WORK_LINK_UP, &priv->work_pending))
		rndis_wlan_do_link_up_work(usbdev);

	if (test_and_clear_bit(WORK_LINK_DOWN, &priv->work_pending))
		rndis_wlan_do_link_down_work(usbdev);

	if (test_and_clear_bit(WORK_SET_MULTICAST_LIST, &priv->work_pending))
		set_multicast_list(usbdev);
}

static void rndis_wlan_set_multicast_list(struct net_device *dev)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);

	if (test_bit(WORK_SET_MULTICAST_LIST, &priv->work_pending))
		return;

	set_bit(WORK_SET_MULTICAST_LIST, &priv->work_pending);
	queue_work(priv->workqueue, &priv->work);
}

static void rndis_wlan_auth_indication(struct usbnet *usbdev,
				struct ndis_80211_status_indication *indication,
				int len)
{
	u8 *buf;
	const char *type;
	int flags, buflen, key_id;
	bool pairwise_error, group_error;
	struct ndis_80211_auth_request *auth_req;
	enum nl80211_key_type key_type;

	/* must have at least one array entry */
	if (len < offsetof(struct ndis_80211_status_indication, u) +
				sizeof(struct ndis_80211_auth_request)) {
		netdev_info(usbdev->net, "authentication indication: too short message (%i)\n",
			    len);
		return;
	}

	buf = (void *)&indication->u.auth_request[0];
	buflen = len - offsetof(struct ndis_80211_status_indication, u);

	while (buflen >= sizeof(*auth_req)) {
		auth_req = (void *)buf;
		type = "unknown";
		flags = le32_to_cpu(auth_req->flags);
		pairwise_error = false;
		group_error = false;

		if (flags & 0x1)
			type = "reauth request";
		if (flags & 0x2)
			type = "key update request";
		if (flags & 0x6) {
			pairwise_error = true;
			type = "pairwise_error";
		}
		if (flags & 0xe) {
			group_error = true;
			type = "group_error";
		}

		netdev_info(usbdev->net, "authentication indication: %s (0x%08x)\n",
			    type, le32_to_cpu(auth_req->flags));

		if (pairwise_error) {
			key_type = NL80211_KEYTYPE_PAIRWISE;
			key_id = -1;

			cfg80211_michael_mic_failure(usbdev->net,
							auth_req->bssid,
							key_type, key_id, NULL,
							GFP_KERNEL);
		}

		if (group_error) {
			key_type = NL80211_KEYTYPE_GROUP;
			key_id = -1;

			cfg80211_michael_mic_failure(usbdev->net,
							auth_req->bssid,
							key_type, key_id, NULL,
							GFP_KERNEL);
		}

		buflen -= le32_to_cpu(auth_req->length);
		buf += le32_to_cpu(auth_req->length);
	}
}

static void rndis_wlan_pmkid_cand_list_indication(struct usbnet *usbdev,
				struct ndis_80211_status_indication *indication,
				int len)
{
	struct ndis_80211_pmkid_cand_list *cand_list;
	int list_len, expected_len, i;

	if (len < offsetof(struct ndis_80211_status_indication, u) +
				sizeof(struct ndis_80211_pmkid_cand_list)) {
		netdev_info(usbdev->net, "pmkid candidate list indication: too short message (%i)\n",
			    len);
		return;
	}

	list_len = le32_to_cpu(indication->u.cand_list.num_candidates) *
			sizeof(struct ndis_80211_pmkid_candidate);
	expected_len = sizeof(struct ndis_80211_pmkid_cand_list) + list_len +
			offsetof(struct ndis_80211_status_indication, u);

	if (len < expected_len) {
		netdev_info(usbdev->net, "pmkid candidate list indication: list larger than buffer (%i < %i)\n",
			    len, expected_len);
		return;
	}

	cand_list = &indication->u.cand_list;

	netdev_info(usbdev->net, "pmkid candidate list indication: version %i, candidates %i\n",
		    le32_to_cpu(cand_list->version),
		    le32_to_cpu(cand_list->num_candidates));

	if (le32_to_cpu(cand_list->version) != 1)
		return;

	for (i = 0; i < le32_to_cpu(cand_list->num_candidates); i++) {
		struct ndis_80211_pmkid_candidate *cand =
						&cand_list->candidate_list[i];
		bool preauth = !!(cand->flags & NDIS_80211_PMKID_CAND_PREAUTH);

		netdev_dbg(usbdev->net, "cand[%i]: flags: 0x%08x, preauth: %d, bssid: %pM\n",
			   i, le32_to_cpu(cand->flags), preauth, cand->bssid);

		cfg80211_pmksa_candidate_notify(usbdev->net, i, cand->bssid,
						preauth, GFP_ATOMIC);
	}
}

static void rndis_wlan_media_specific_indication(struct usbnet *usbdev,
			struct rndis_indicate *msg, int buflen)
{
	struct ndis_80211_status_indication *indication;
	unsigned int len, offset;

	offset = offsetof(struct rndis_indicate, status) +
			le32_to_cpu(msg->offset);
	len = le32_to_cpu(msg->length);

	if (len < 8) {
		netdev_info(usbdev->net, "media specific indication, ignore too short message (%i < 8)\n",
			    len);
		return;
	}

	if (len > buflen || offset > buflen || offset + len > buflen) {
		netdev_info(usbdev->net, "media specific indication, too large to fit to buffer (%i > %i)\n",
			    offset + len, buflen);
		return;
	}

	indication = (void *)((u8 *)msg + offset);

	switch (le32_to_cpu(indication->status_type)) {
	case NDIS_80211_STATUSTYPE_RADIOSTATE:
		netdev_info(usbdev->net, "radio state indication: %i\n",
			    le32_to_cpu(indication->u.radio_status));
		return;

	case NDIS_80211_STATUSTYPE_MEDIASTREAMMODE:
		netdev_info(usbdev->net, "media stream mode indication: %i\n",
			    le32_to_cpu(indication->u.media_stream_mode));
		return;

	case NDIS_80211_STATUSTYPE_AUTHENTICATION:
		rndis_wlan_auth_indication(usbdev, indication, len);
		return;

	case NDIS_80211_STATUSTYPE_PMKID_CANDIDATELIST:
		rndis_wlan_pmkid_cand_list_indication(usbdev, indication, len);
		return;

	default:
		netdev_info(usbdev->net, "media specific indication: unknown status type 0x%08x\n",
			    le32_to_cpu(indication->status_type));
	}
}

static void rndis_wlan_indication(struct usbnet *usbdev, void *ind, int buflen)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	struct rndis_indicate *msg = ind;

	switch (le32_to_cpu(msg->status)) {
	case RNDIS_STATUS_MEDIA_CONNECT:
		if (priv->current_command_oid == RNDIS_OID_802_11_ADD_KEY) {
			/* RNDIS_OID_802_11_ADD_KEY causes sometimes extra
			 * "media connect" indications which confuses driver
			 * and userspace to think that device is
			 * roaming/reassociating when it isn't.
			 */
			netdev_dbg(usbdev->net, "ignored RNDIS_OID_802_11_ADD_KEY triggered 'media connect'\n");
			return;
		}

		usbnet_pause_rx(usbdev);

		netdev_info(usbdev->net, "media connect\n");

		/* queue work to avoid recursive calls into rndis_command */
		set_bit(WORK_LINK_UP, &priv->work_pending);
		queue_work(priv->workqueue, &priv->work);
		break;

	case RNDIS_STATUS_MEDIA_DISCONNECT:
		netdev_info(usbdev->net, "media disconnect\n");

		/* queue work to avoid recursive calls into rndis_command */
		set_bit(WORK_LINK_DOWN, &priv->work_pending);
		queue_work(priv->workqueue, &priv->work);
		break;

	case RNDIS_STATUS_MEDIA_SPECIFIC_INDICATION:
		rndis_wlan_media_specific_indication(usbdev, msg, buflen);
		break;

	default:
		netdev_info(usbdev->net, "indication: 0x%08x\n",
			    le32_to_cpu(msg->status));
		break;
	}
}

static int rndis_wlan_get_caps(struct usbnet *usbdev, struct wiphy *wiphy)
{
	struct {
		__le32	num_items;
		__le32	items[8];
	} networks_supported;
	struct ndis_80211_capability *caps;
	u8 caps_buf[sizeof(*caps) + sizeof(caps->auth_encr_pair) * 16];
	int len, retval, i, n;
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);

	/* determine supported modes */
	len = sizeof(networks_supported);
	retval = rndis_query_oid(usbdev,
				 RNDIS_OID_802_11_NETWORK_TYPES_SUPPORTED,
				 &networks_supported, &len);
	if (retval >= 0) {
		n = le32_to_cpu(networks_supported.num_items);
		if (n > 8)
			n = 8;
		for (i = 0; i < n; i++) {
			switch (le32_to_cpu(networks_supported.items[i])) {
			case NDIS_80211_TYPE_FREQ_HOP:
			case NDIS_80211_TYPE_DIRECT_SEQ:
				priv->caps |= CAP_MODE_80211B;
				break;
			case NDIS_80211_TYPE_OFDM_A:
				priv->caps |= CAP_MODE_80211A;
				break;
			case NDIS_80211_TYPE_OFDM_G:
				priv->caps |= CAP_MODE_80211G;
				break;
			}
		}
	}

	/* get device 802.11 capabilities, number of PMKIDs */
	caps = (struct ndis_80211_capability *)caps_buf;
	len = sizeof(caps_buf);
	retval = rndis_query_oid(usbdev,
				 RNDIS_OID_802_11_CAPABILITY,
				 caps, &len);
	if (retval >= 0) {
		netdev_dbg(usbdev->net, "RNDIS_OID_802_11_CAPABILITY -> len %d, "
				"ver %d, pmkids %d, auth-encr-pairs %d\n",
				le32_to_cpu(caps->length),
				le32_to_cpu(caps->version),
				le32_to_cpu(caps->num_pmkids),
				le32_to_cpu(caps->num_auth_encr_pair));
		wiphy->max_num_pmkids = le32_to_cpu(caps->num_pmkids);
	} else
		wiphy->max_num_pmkids = 0;

	return retval;
}

static void rndis_do_cqm(struct usbnet *usbdev, s32 rssi)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	enum nl80211_cqm_rssi_threshold_event event;
	int thold, hyst, last_event;

	if (priv->cqm_rssi_thold >= 0 || rssi >= 0)
		return;
	if (priv->infra_mode != NDIS_80211_INFRA_INFRA)
		return;

	last_event = priv->last_cqm_event_rssi;
	thold = priv->cqm_rssi_thold;
	hyst = priv->cqm_rssi_hyst;

	if (rssi < thold && (last_event == 0 || rssi < last_event - hyst))
		event = NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW;
	else if (rssi > thold && (last_event == 0 || rssi > last_event + hyst))
		event = NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;
	else
		return;

	priv->last_cqm_event_rssi = rssi;
	cfg80211_cqm_rssi_notify(usbdev->net, event, GFP_KERNEL);
}

#define DEVICE_POLLER_JIFFIES (HZ)
static void rndis_device_poller(struct work_struct *work)
{
	struct rndis_wlan_private *priv =
		container_of(work, struct rndis_wlan_private,
							dev_poller_work.work);
	struct usbnet *usbdev = priv->usbdev;
	__le32 rssi, tmp;
	int len, ret, j;
	int update_jiffies = DEVICE_POLLER_JIFFIES;
	void *buf;

	/* Only check/do workaround when connected. Calling is_associated()
	 * also polls device with rndis_command() and catches for media link
	 * indications.
	 */
	if (!is_associated(usbdev)) {
		/* Workaround bad scanning in BCM4320a devices with active
		 * background scanning when not associated.
		 */
		if (priv->device_type == RNDIS_BCM4320A && priv->radio_on &&
		    !priv->scan_request) {
			/* Get previous scan results */
			rndis_check_bssid_list(usbdev, NULL, NULL);

			/* Initiate new scan */
			rndis_start_bssid_list_scan(usbdev);
		}

		goto end;
	}

	len = sizeof(rssi);
	ret = rndis_query_oid(usbdev, RNDIS_OID_802_11_RSSI,
			      &rssi, &len);
	if (ret == 0) {
		priv->last_qual = level_to_qual(le32_to_cpu(rssi));
		rndis_do_cqm(usbdev, le32_to_cpu(rssi));
	}

	netdev_dbg(usbdev->net, "dev-poller: RNDIS_OID_802_11_RSSI -> %d, rssi:%d, qual: %d\n",
		   ret, le32_to_cpu(rssi), level_to_qual(le32_to_cpu(rssi)));

	/* Workaround transfer stalls on poor quality links.
	 * TODO: find right way to fix these stalls (as stalls do not happen
	 * with ndiswrapper/windows driver). */
	if (priv->param_workaround_interval > 0 && priv->last_qual <= 25) {
		/* Decrease stats worker interval to catch stalls.
		 * faster. Faster than 400-500ms causes packet loss,
		 * Slower doesn't catch stalls fast enough.
		 */
		j = msecs_to_jiffies(priv->param_workaround_interval);
		if (j > DEVICE_POLLER_JIFFIES)
			j = DEVICE_POLLER_JIFFIES;
		else if (j <= 0)
			j = 1;
		update_jiffies = j;

		/* Send scan OID. Use of both OIDs is required to get device
		 * working.
		 */
		tmp = cpu_to_le32(1);
		rndis_set_oid(usbdev,
			      RNDIS_OID_802_11_BSSID_LIST_SCAN,
			      &tmp, sizeof(tmp));

		len = CONTROL_BUFFER_SIZE;
		buf = kmalloc(len, GFP_KERNEL);
		if (!buf)
			goto end;

		rndis_query_oid(usbdev,
				RNDIS_OID_802_11_BSSID_LIST,
				buf, &len);
		kfree(buf);
	}

end:
	if (update_jiffies >= HZ)
		update_jiffies = round_jiffies_relative(update_jiffies);
	else {
		j = round_jiffies_relative(update_jiffies);
		if (abs(j - update_jiffies) <= 10)
			update_jiffies = j;
	}

	queue_delayed_work(priv->workqueue, &priv->dev_poller_work,
								update_jiffies);
}

/*
 * driver/device initialization
 */
static void rndis_copy_module_params(struct usbnet *usbdev, int device_type)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);

	priv->device_type = device_type;

	priv->param_country[0] = modparam_country[0];
	priv->param_country[1] = modparam_country[1];
	priv->param_country[2] = 0;
	priv->param_frameburst   = modparam_frameburst;
	priv->param_afterburner  = modparam_afterburner;
	priv->param_power_save   = modparam_power_save;
	priv->param_power_output = modparam_power_output;
	priv->param_roamtrigger  = modparam_roamtrigger;
	priv->param_roamdelta    = modparam_roamdelta;

	priv->param_country[0] = toupper(priv->param_country[0]);
	priv->param_country[1] = toupper(priv->param_country[1]);
	/* doesn't support EU as country code, use FI instead */
	if (!strcmp(priv->param_country, "EU"))
		strcpy(priv->param_country, "FI");

	if (priv->param_power_save < 0)
		priv->param_power_save = 0;
	else if (priv->param_power_save > 2)
		priv->param_power_save = 2;

	if (priv->param_power_output < 0)
		priv->param_power_output = 0;
	else if (priv->param_power_output > 3)
		priv->param_power_output = 3;

	if (priv->param_roamtrigger < -80)
		priv->param_roamtrigger = -80;
	else if (priv->param_roamtrigger > -60)
		priv->param_roamtrigger = -60;

	if (priv->param_roamdelta < 0)
		priv->param_roamdelta = 0;
	else if (priv->param_roamdelta > 2)
		priv->param_roamdelta = 2;

	if (modparam_workaround_interval < 0)
		priv->param_workaround_interval = 500;
	else
		priv->param_workaround_interval = modparam_workaround_interval;
}

static int unknown_early_init(struct usbnet *usbdev)
{
	/* copy module parameters for unknown so that iwconfig reports txpower
	 * and workaround parameter is copied to private structure correctly.
	 */
	rndis_copy_module_params(usbdev, RNDIS_UNKNOWN);

	/* This is unknown device, so do not try set configuration parameters.
	 */

	return 0;
}

static int bcm4320a_early_init(struct usbnet *usbdev)
{
	/* copy module parameters for bcm4320a so that iwconfig reports txpower
	 * and workaround parameter is copied to private structure correctly.
	 */
	rndis_copy_module_params(usbdev, RNDIS_BCM4320A);

	/* bcm4320a doesn't handle configuration parameters well. Try
	 * set any and you get partially zeroed mac and broken device.
	 */

	return 0;
}

static int bcm4320b_early_init(struct usbnet *usbdev)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	char buf[8];

	rndis_copy_module_params(usbdev, RNDIS_BCM4320B);

	/* Early initialization settings, setting these won't have effect
	 * if called after generic_rndis_bind().
	 */

	rndis_set_config_parameter_str(usbdev, "Country", priv->param_country);
	rndis_set_config_parameter_str(usbdev, "FrameBursting",
					priv->param_frameburst ? "1" : "0");
	rndis_set_config_parameter_str(usbdev, "Afterburner",
					priv->param_afterburner ? "1" : "0");
	sprintf(buf, "%d", priv->param_power_save);
	rndis_set_config_parameter_str(usbdev, "PowerSaveMode", buf);
	sprintf(buf, "%d", priv->param_power_output);
	rndis_set_config_parameter_str(usbdev, "PwrOut", buf);
	sprintf(buf, "%d", priv->param_roamtrigger);
	rndis_set_config_parameter_str(usbdev, "RoamTrigger", buf);
	sprintf(buf, "%d", priv->param_roamdelta);
	rndis_set_config_parameter_str(usbdev, "RoamDelta", buf);

	return 0;
}

/* same as rndis_netdev_ops but with local multicast handler */
static const struct net_device_ops rndis_wlan_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= rndis_wlan_set_multicast_list,
};

static int rndis_wlan_bind(struct usbnet *usbdev, struct usb_interface *intf)
{
	struct wiphy *wiphy;
	struct rndis_wlan_private *priv;
	int retval, len;
	__le32 tmp;

	/* allocate wiphy and rndis private data
	 * NOTE: We only support a single virtual interface, so wiphy
	 * and wireless_dev are somewhat synonymous for this device.
	 */
	wiphy = wiphy_new(&rndis_config_ops, sizeof(struct rndis_wlan_private));
	if (!wiphy)
		return -ENOMEM;

	priv = wiphy_priv(wiphy);
	usbdev->net->ieee80211_ptr = &priv->wdev;
	priv->wdev.wiphy = wiphy;
	priv->wdev.iftype = NL80211_IFTYPE_STATION;

	/* These have to be initialized before calling generic_rndis_bind().
	 * Otherwise we'll be in big trouble in rndis_wlan_early_init().
	 */
	usbdev->driver_priv = priv;
	priv->usbdev = usbdev;

	mutex_init(&priv->command_lock);

	/* because rndis_command() sleeps we need to use workqueue */
	priv->workqueue = create_singlethread_workqueue("rndis_wlan");
	INIT_WORK(&priv->work, rndis_wlan_worker);
	INIT_DELAYED_WORK(&priv->dev_poller_work, rndis_device_poller);
	INIT_DELAYED_WORK(&priv->scan_work, rndis_get_scan_results);

	/* try bind rndis_host */
	retval = generic_rndis_bind(usbdev, intf, FLAG_RNDIS_PHYM_WIRELESS);
	if (retval < 0)
		goto fail;

	/* generic_rndis_bind set packet filter to multicast_all+
	 * promisc mode which doesn't work well for our devices (device
	 * picks up rssi to closest station instead of to access point).
	 *
	 * rndis_host wants to avoid all OID as much as possible
	 * so do promisc/multicast handling in rndis_wlan.
	 */
	usbdev->net->netdev_ops = &rndis_wlan_netdev_ops;

	tmp = cpu_to_le32(RNDIS_PACKET_TYPE_DIRECTED | RNDIS_PACKET_TYPE_BROADCAST);
	retval = rndis_set_oid(usbdev,
			       RNDIS_OID_GEN_CURRENT_PACKET_FILTER,
			       &tmp, sizeof(tmp));

	len = sizeof(tmp);
	retval = rndis_query_oid(usbdev,
				 RNDIS_OID_802_3_MAXIMUM_LIST_SIZE,
				 &tmp, &len);
	priv->multicast_size = le32_to_cpu(tmp);
	if (retval < 0 || priv->multicast_size < 0)
		priv->multicast_size = 0;
	if (priv->multicast_size > 0)
		usbdev->net->flags |= IFF_MULTICAST;
	else
		usbdev->net->flags &= ~IFF_MULTICAST;

	/* fill-out wiphy structure and register w/ cfg80211 */
	memcpy(wiphy->perm_addr, usbdev->net->dev_addr, ETH_ALEN);
	wiphy->privid = rndis_wiphy_privid;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION)
					| BIT(NL80211_IFTYPE_ADHOC);
	wiphy->max_scan_ssids = 1;

	/* TODO: fill-out band/encr information based on priv->caps */
	rndis_wlan_get_caps(usbdev, wiphy);

	memcpy(priv->channels, rndis_channels, sizeof(rndis_channels));
	memcpy(priv->rates, rndis_rates, sizeof(rndis_rates));
	priv->band.channels = priv->channels;
	priv->band.n_channels = ARRAY_SIZE(rndis_channels);
	priv->band.bitrates = priv->rates;
	priv->band.n_bitrates = ARRAY_SIZE(rndis_rates);
	wiphy->bands[IEEE80211_BAND_2GHZ] = &priv->band;
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_UNSPEC;

	memcpy(priv->cipher_suites, rndis_cipher_suites,
						sizeof(rndis_cipher_suites));
	wiphy->cipher_suites = priv->cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(rndis_cipher_suites);

	set_wiphy_dev(wiphy, &usbdev->udev->dev);

	if (wiphy_register(wiphy)) {
		retval = -ENODEV;
		goto fail;
	}

	set_default_iw_params(usbdev);

	priv->power_mode = -1;

	/* set default rts/frag */
	rndis_set_wiphy_params(wiphy,
			WIPHY_PARAM_FRAG_THRESHOLD | WIPHY_PARAM_RTS_THRESHOLD);

	/* turn radio off on init */
	priv->radio_on = false;
	disassociate(usbdev, false);
	netif_carrier_off(usbdev->net);

	return 0;

fail:
	cancel_delayed_work_sync(&priv->dev_poller_work);
	cancel_delayed_work_sync(&priv->scan_work);
	cancel_work_sync(&priv->work);
	flush_workqueue(priv->workqueue);
	destroy_workqueue(priv->workqueue);

	wiphy_free(wiphy);
	return retval;
}

static void rndis_wlan_unbind(struct usbnet *usbdev, struct usb_interface *intf)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);

	/* turn radio off */
	disassociate(usbdev, false);

	cancel_delayed_work_sync(&priv->dev_poller_work);
	cancel_delayed_work_sync(&priv->scan_work);
	cancel_work_sync(&priv->work);
	flush_workqueue(priv->workqueue);
	destroy_workqueue(priv->workqueue);

	rndis_unbind(usbdev, intf);

	wiphy_unregister(priv->wdev.wiphy);
	wiphy_free(priv->wdev.wiphy);
}

static int rndis_wlan_reset(struct usbnet *usbdev)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	int retval;

	netdev_dbg(usbdev->net, "%s()\n", __func__);

	retval = rndis_reset(usbdev);
	if (retval)
		netdev_warn(usbdev->net, "rndis_reset failed: %d\n", retval);

	/* rndis_reset cleared multicast list, so restore here.
	   (set_multicast_list() also turns on current packet filter) */
	set_multicast_list(usbdev);

	queue_delayed_work(priv->workqueue, &priv->dev_poller_work,
		round_jiffies_relative(DEVICE_POLLER_JIFFIES));

	return deauthenticate(usbdev);
}

static int rndis_wlan_stop(struct usbnet *usbdev)
{
	struct rndis_wlan_private *priv = get_rndis_wlan_priv(usbdev);
	int retval;
	__le32 filter;

	netdev_dbg(usbdev->net, "%s()\n", __func__);

	retval = disassociate(usbdev, false);

	priv->work_pending = 0;
	cancel_delayed_work_sync(&priv->dev_poller_work);
	cancel_delayed_work_sync(&priv->scan_work);
	cancel_work_sync(&priv->work);
	flush_workqueue(priv->workqueue);

	if (priv->scan_request) {
		cfg80211_scan_done(priv->scan_request, true);
		priv->scan_request = NULL;
	}

	/* Set current packet filter zero to block receiving data packets from
	   device. */
	filter = 0;
	rndis_set_oid(usbdev, RNDIS_OID_GEN_CURRENT_PACKET_FILTER, &filter,
								sizeof(filter));

	return retval;
}

static const struct driver_info	bcm4320b_info = {
	.description =	"Wireless RNDIS device, BCM4320b based",
	.flags =	FLAG_WLAN | FLAG_FRAMING_RN | FLAG_NO_SETINT |
				FLAG_AVOID_UNLINK_URBS,
	.bind =		rndis_wlan_bind,
	.unbind =	rndis_wlan_unbind,
	.status =	rndis_status,
	.rx_fixup =	rndis_rx_fixup,
	.tx_fixup =	rndis_tx_fixup,
	.reset =	rndis_wlan_reset,
	.stop =		rndis_wlan_stop,
	.early_init =	bcm4320b_early_init,
	.indication =	rndis_wlan_indication,
};

static const struct driver_info	bcm4320a_info = {
	.description =	"Wireless RNDIS device, BCM4320a based",
	.flags =	FLAG_WLAN | FLAG_FRAMING_RN | FLAG_NO_SETINT |
				FLAG_AVOID_UNLINK_URBS,
	.bind =		rndis_wlan_bind,
	.unbind =	rndis_wlan_unbind,
	.status =	rndis_status,
	.rx_fixup =	rndis_rx_fixup,
	.tx_fixup =	rndis_tx_fixup,
	.reset =	rndis_wlan_reset,
	.stop =		rndis_wlan_stop,
	.early_init =	bcm4320a_early_init,
	.indication =	rndis_wlan_indication,
};

static const struct driver_info rndis_wlan_info = {
	.description =	"Wireless RNDIS device",
	.flags =	FLAG_WLAN | FLAG_FRAMING_RN | FLAG_NO_SETINT |
				FLAG_AVOID_UNLINK_URBS,
	.bind =		rndis_wlan_bind,
	.unbind =	rndis_wlan_unbind,
	.status =	rndis_status,
	.rx_fixup =	rndis_rx_fixup,
	.tx_fixup =	rndis_tx_fixup,
	.reset =	rndis_wlan_reset,
	.stop =		rndis_wlan_stop,
	.early_init =	unknown_early_init,
	.indication =	rndis_wlan_indication,
};

/*-------------------------------------------------------------------------*/

static const struct usb_device_id products [] = {
#define	RNDIS_MASTER_INTERFACE \
	.bInterfaceClass	= USB_CLASS_COMM, \
	.bInterfaceSubClass	= 2 /* ACM */, \
	.bInterfaceProtocol	= 0x0ff

/* INF driver for these devices have DriverVer >= 4.xx.xx.xx and many custom
 * parameters available. Chipset marked as 'BCM4320SKFBG' in NDISwrapper-wiki.
 */
{
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0411,
	.idProduct		= 0x00bc,	/* Buffalo WLI-U2-KG125S */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0baf,
	.idProduct		= 0x011b,	/* U.S. Robotics USR5421 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x050d,
	.idProduct		= 0x011b,	/* Belkin F5D7051 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x1799,	/* Belkin has two vendor ids */
	.idProduct		= 0x011b,	/* Belkin F5D7051 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x13b1,
	.idProduct		= 0x0014,	/* Linksys WUSB54GSv2 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x13b1,
	.idProduct		= 0x0026,	/* Linksys WUSB54GSC */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0b05,
	.idProduct		= 0x1717,	/* Asus WL169gE */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0a5c,
	.idProduct		= 0xd11b,	/* Eminent EM4045 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x1690,
	.idProduct		= 0x0715,	/* BT Voyager 1055 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
},
/* These devices have DriverVer < 4.xx.xx.xx and do not have any custom
 * parameters available, hardware probably contain older firmware version with
 * no way of updating. Chipset marked as 'BCM4320????' in NDISwrapper-wiki.
 */
{
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x13b1,
	.idProduct		= 0x000e,	/* Linksys WUSB54GSv1 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320a_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0baf,
	.idProduct		= 0x0111,	/* U.S. Robotics USR5420 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320a_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0411,
	.idProduct		= 0x004b,	/* BUFFALO WLI-USB-G54 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320a_info,
},
/* Generic Wireless RNDIS devices that we don't have exact
 * idVendor/idProduct/chip yet.
 */
{
	/* RNDIS is MSFT's un-official variant of CDC ACM */
	USB_INTERFACE_INFO(USB_CLASS_COMM, 2 /* ACM */, 0x0ff),
	.driver_info = (unsigned long) &rndis_wlan_info,
}, {
	/* "ActiveSync" is an undocumented variant of RNDIS, used in WM5 */
	USB_INTERFACE_INFO(USB_CLASS_MISC, 1, 1),
	.driver_info = (unsigned long) &rndis_wlan_info,
},
	{ },		// END
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver rndis_wlan_driver = {
	.name =		"rndis_wlan",
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(rndis_wlan_driver);

MODULE_AUTHOR("Bjorge Dijkstra");
MODULE_AUTHOR("Jussi Kivilinna");
MODULE_DESCRIPTION("Driver for RNDIS based USB Wireless adapters");
MODULE_LICENSE("GPL");

