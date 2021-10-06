/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __IEEE80211_H
#define __IEEE80211_H

#include "osdep_service.h"
#include "drv_types.h"
#include "wifi.h"
#include <linux/wireless.h>

#define MGMT_QUEUE_NUM 5

#define ETH_ALEN	6
#define ETH_TYPE_LEN		2
#define PAYLOAD_TYPE_LEN	1

#ifdef CONFIG_88EU_AP_MODE

#define RTL_IOCTL_HOSTAPD (SIOCIWFIRSTPRIV + 28)

/* RTL871X_IOCTL_HOSTAPD ioctl() cmd: */
enum {
	RTL871X_HOSTAPD_FLUSH = 1,
	RTL871X_HOSTAPD_ADD_STA = 2,
	RTL871X_HOSTAPD_REMOVE_STA = 3,
	RTL871X_HOSTAPD_GET_INFO_STA = 4,
	/* REMOVED: PRISM2_HOSTAPD_RESET_TXEXC_STA = 5, */
	RTL871X_HOSTAPD_GET_WPAIE_STA = 5,
	RTL871X_SET_ENCRYPTION = 6,
	RTL871X_GET_ENCRYPTION = 7,
	RTL871X_HOSTAPD_SET_FLAGS_STA = 8,
	RTL871X_HOSTAPD_GET_RID = 9,
	RTL871X_HOSTAPD_SET_RID = 10,
	RTL871X_HOSTAPD_SET_ASSOC_AP_ADDR = 11,
	RTL871X_HOSTAPD_SET_GENERIC_ELEMENT = 12,
	RTL871X_HOSTAPD_MLME = 13,
	RTL871X_HOSTAPD_SCAN_REQ = 14,
	RTL871X_HOSTAPD_STA_CLEAR_STATS = 15,
	RTL871X_HOSTAPD_SET_BEACON = 16,
	RTL871X_HOSTAPD_SET_WPS_BEACON = 17,
	RTL871X_HOSTAPD_SET_WPS_PROBE_RESP = 18,
	RTL871X_HOSTAPD_SET_WPS_ASSOC_RESP = 19,
	RTL871X_HOSTAPD_SET_HIDDEN_SSID = 20,
	RTL871X_HOSTAPD_SET_MACADDR_ACL = 21,
	RTL871X_HOSTAPD_ACL_ADD_STA = 22,
	RTL871X_HOSTAPD_ACL_REMOVE_STA = 23,
};

/* STA flags */
#define WLAN_STA_AUTH BIT(0)
#define WLAN_STA_ASSOC BIT(1)
#define WLAN_STA_PS BIT(2)
#define WLAN_STA_TIM BIT(3)
#define WLAN_STA_PERM BIT(4)
#define WLAN_STA_AUTHORIZED BIT(5)
#define WLAN_STA_PENDING_POLL BIT(6) /* pending activity poll not ACKed */
#define WLAN_STA_SHORT_PREAMBLE BIT(7)
#define WLAN_STA_PREAUTH BIT(8)
#define WLAN_STA_WME BIT(9)
#define WLAN_STA_MFP BIT(10)
#define WLAN_STA_HT BIT(11)
#define WLAN_STA_WPS BIT(12)
#define WLAN_STA_MAYBE_WPS BIT(13)
#define WLAN_STA_NONERP BIT(31)

#endif

#define IEEE_CMD_SET_WPA_PARAM			1
#define IEEE_CMD_SET_WPA_IE				2
#define IEEE_CMD_SET_ENCRYPTION			3
#define IEEE_CMD_MLME						4

#define IEEE_PARAM_WPA_ENABLED				1
#define IEEE_PARAM_TKIP_COUNTERMEASURES		2
#define IEEE_PARAM_DROP_UNENCRYPTED			3
#define IEEE_PARAM_PRIVACY_INVOKED			4
#define IEEE_PARAM_AUTH_ALGS					5
#define IEEE_PARAM_IEEE_802_1X				6
#define IEEE_PARAM_WPAX_SELECT				7

#define AUTH_ALG_OPEN_SYSTEM			0x1
#define AUTH_ALG_SHARED_KEY			0x2
#define AUTH_ALG_LEAP				0x00000004

#define IEEE_MLME_STA_DEAUTH				1
#define IEEE_MLME_STA_DISASSOC			2

#define IEEE_CRYPT_ERR_UNKNOWN_ALG			2
#define IEEE_CRYPT_ERR_UNKNOWN_ADDR			3
#define IEEE_CRYPT_ERR_CRYPT_INIT_FAILED		4
#define IEEE_CRYPT_ERR_KEY_SET_FAILED			5
#define IEEE_CRYPT_ERR_TX_KEY_SET_FAILED		6
#define IEEE_CRYPT_ERR_CARD_CONF_FAILED		7

#define	IEEE_CRYPT_ALG_NAME_LEN			16

#define WPA_CIPHER_NONE		BIT(0)
#define WPA_CIPHER_WEP40	BIT(1)
#define WPA_CIPHER_WEP104 BIT(2)
#define WPA_CIPHER_TKIP		BIT(3)
#define WPA_CIPHER_CCMP		BIT(4)


#define WPA_SELECTOR_LEN 4
extern u8 RTW_WPA_OUI_TYPE[];
extern u16 RTW_WPA_VERSION;
extern u8 WPA_AUTH_KEY_MGMT_NONE[];
extern u8 WPA_AUTH_KEY_MGMT_UNSPEC_802_1X[];
extern u8 WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X[];
extern u8 WPA_CIPHER_SUITE_NONE[];
extern u8 WPA_CIPHER_SUITE_WEP40[];
extern u8 WPA_CIPHER_SUITE_TKIP[];
extern u8 WPA_CIPHER_SUITE_WRAP[];
extern u8 WPA_CIPHER_SUITE_CCMP[];
extern u8 WPA_CIPHER_SUITE_WEP104[];

#define RSN_HEADER_LEN 4
#define RSN_SELECTOR_LEN 4

extern u16 RSN_VERSION_BSD;
extern u8 RSN_AUTH_KEY_MGMT_UNSPEC_802_1X[];
extern u8 RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X[];
extern u8 RSN_CIPHER_SUITE_NONE[];
extern u8 RSN_CIPHER_SUITE_WEP40[];
extern u8 RSN_CIPHER_SUITE_TKIP[];
extern u8 RSN_CIPHER_SUITE_WRAP[];
extern u8 RSN_CIPHER_SUITE_CCMP[];
extern u8 RSN_CIPHER_SUITE_WEP104[];

enum ratr_table_mode {
	RATR_INX_WIRELESS_NGB = 0,	/*  BGN 40 Mhz 2SS 1SS */
	RATR_INX_WIRELESS_NG = 1,	/*  GN or N */
	RATR_INX_WIRELESS_NB = 2,	/*  BGN 20 Mhz 2SS 1SS  or BN */
	RATR_INX_WIRELESS_N = 3,
	RATR_INX_WIRELESS_GB = 4,
	RATR_INX_WIRELESS_G = 5,
	RATR_INX_WIRELESS_B = 6,
	RATR_INX_WIRELESS_MC = 7,
	RATR_INX_WIRELESS_AC_N = 8,
};

enum NETWORK_TYPE {
	WIRELESS_INVALID = 0,
	/* Sub-Element */
	WIRELESS_11B = BIT(0), /* tx:cck only, rx:cck only, hw: cck */
	WIRELESS_11G = BIT(1), /* tx:ofdm only, rx:ofdm & cck, hw:cck & ofdm*/
	WIRELESS_11_24N = BIT(3), /* tx:MCS only, rx:MCS & cck, hw:MCS & cck */

	/* Combination */
	/*  tx: cck & ofdm, rx: cck & ofdm & MCS, hw: cck & ofdm */
	WIRELESS_11BG = (WIRELESS_11B | WIRELESS_11G),
	/*  tx: ofdm & MCS, rx: ofdm & cck & MCS, hw: cck & ofdm */
	WIRELESS_11G_24N = (WIRELESS_11G | WIRELESS_11_24N),
	/*  tx: ofdm & cck & MCS, rx: ofdm & cck & MCS, hw: ofdm & cck */
	WIRELESS_11BG_24N = (WIRELESS_11B | WIRELESS_11G | WIRELESS_11_24N),
};

#define SUPPORTED_24G_NETTYPE_MSK				\
	 (WIRELESS_11B | WIRELESS_11G | WIRELESS_11_24N)

#define IsSupported24G(NetType)					\
	((NetType) & SUPPORTED_24G_NETTYPE_MSK ? true : false)

#define IsEnableHWCCK(NetType)					\
	IsSupported24G(NetType)

#define IsSupportedRxCCK(NetType) IsEnableHWCCK(NetType)

#define IsSupportedTxCCK(NetType)				\
	((NetType) & (WIRELESS_11B) ? true : false)
#define IsSupportedTxOFDM(NetType)				\
	((NetType) & (WIRELESS_11G) ? true : false)
#define IsSupportedTxMCS(NetType)				\
	((NetType) & (WIRELESS_11_24N) ? true : false)

struct ieee_param {
	u32 cmd;
	u8 sta_addr[ETH_ALEN];
	union {
		struct {
			u8 name;
			u32 value;
		} wpa_param;
		struct {
			u32 len;
			u8 reserved[32];
			u8 data[0];
		} wpa_ie;
		struct {
			int command;
			int reason_code;
		} mlme;
		struct {
			u8 alg[IEEE_CRYPT_ALG_NAME_LEN];
			u8 set_tx;
			u32 err;
			u8 idx;
			u8 seq[8]; /* sequence counter (set: RX, get: TX) */
			u16 key_len;
			u8 key[0];
		} crypt;
#ifdef CONFIG_88EU_AP_MODE
		struct {
			u16 aid;
			u16 capability;
			int flags;
			u8 tx_supp_rates[16];
			struct ieee80211_ht_cap ht_cap;
		} add_sta;
		struct {
			u8	reserved[2];/* for set max_num_sta */
			u8	buf[0];
		} bcn_ie;
#endif

	} u;
};

#ifdef CONFIG_88EU_AP_MODE
struct ieee_param_ex {
	u32 cmd;
	u8 sta_addr[ETH_ALEN];
	u8 data[0];
};

struct sta_data {
	u16 aid;
	u16 capability;
	int flags;
	u32 sta_set;
	u8 tx_supp_rates[16];
	u32 tx_supp_rates_len;
	struct ieee80211_ht_cap ht_cap;
	u64	rx_pkts;
	u64	rx_bytes;
	u64	rx_drops;
	u64	tx_pkts;
	u64	tx_bytes;
	u64	tx_drops;
};
#endif

#define IEEE80211_DATA_LEN		2304
/* Maximum size for the MA-UNITDATA primitive, 802.11 standard section
   6.2.1.1.2.

   The figure in section 7.1.2 suggests a body size of up to 2312
   bytes is allowed, which is a bit confusing, I suspect this
   represents the 2304 bytes of real data, plus a possible 8 bytes of
   WEP IV and ICV. (this interpretation suggested by Ramiro Barreiro) */

#define IEEE80211_HLEN			30
#define IEEE80211_FRAME_LEN		(IEEE80211_DATA_LEN + IEEE80211_HLEN)

/* this is stolen from ipw2200 driver */
#define IEEE_IBSS_MAC_HASH_SIZE 31

struct ieee_ibss_seq {
	u8 mac[ETH_ALEN];
	u16 seq_num;
	u16 frag_num;
	unsigned long packet_time;
	struct list_head list;
};

struct rtw_ieee80211_hdr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
	u8 addr4[ETH_ALEN];
} __packed;

struct rtw_ieee80211_hdr_3addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
} __packed;

struct rtw_ieee80211_hdr_qos {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
	u8 addr4[ETH_ALEN];
	u16	qc;
}  __packed;

struct rtw_ieee80211_hdr_3addr_qos {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
	u16     qc;
}  __packed;

struct eapol {
	u8 snap[6];
	u16 ethertype;
	u8 version;
	u8 type;
	u16 length;
} __packed;

enum eap_type {
	EAP_PACKET = 0,
	EAPOL_START,
	EAPOL_LOGOFF,
	EAPOL_KEY,
	EAPOL_ENCAP_ASF_ALERT
};

#define IEEE80211_3ADDR_LEN 24
#define IEEE80211_4ADDR_LEN 30
#define IEEE80211_FCS_LEN    4

#define MIN_FRAG_THRESHOLD     256U
#define	MAX_FRAG_THRESHOLD     2346U

/* Frame control field constants */
#define RTW_IEEE80211_FCTL_VERS		0x0003
#define RTW_IEEE80211_FCTL_FTYPE	0x000c
#define RTW_IEEE80211_FCTL_STYPE	0x00f0
#define RTW_IEEE80211_FCTL_TODS		0x0100
#define RTW_IEEE80211_FCTL_FROMDS	0x0200
#define RTW_IEEE80211_FCTL_MOREFRAGS	0x0400
#define RTW_IEEE80211_FCTL_RETRY	0x0800
#define RTW_IEEE80211_FCTL_PM		0x1000
#define RTW_IEEE80211_FCTL_MOREDATA	0x2000
#define RTW_IEEE80211_FCTL_PROTECTED	0x4000
#define RTW_IEEE80211_FCTL_ORDER	0x8000
#define RTW_IEEE80211_FCTL_CTL_EXT	0x0f00

#define RTW_IEEE80211_FTYPE_MGMT	0x0000
#define RTW_IEEE80211_FTYPE_CTL		0x0004
#define RTW_IEEE80211_FTYPE_DATA	0x0008
#define RTW_IEEE80211_FTYPE_EXT		0x000c

/* management */
#define RTW_IEEE80211_STYPE_ASSOC_REQ	0x0000
#define RTW_IEEE80211_STYPE_ASSOC_RESP	0x0010
#define RTW_IEEE80211_STYPE_REASSOC_REQ	0x0020
#define RTW_IEEE80211_STYPE_REASSOC_RESP	0x0030
#define RTW_IEEE80211_STYPE_PROBE_REQ	0x0040
#define RTW_IEEE80211_STYPE_PROBE_RESP	0x0050
#define RTW_IEEE80211_STYPE_BEACON	0x0080
#define RTW_IEEE80211_STYPE_ATIM	0x0090
#define RTW_IEEE80211_STYPE_DISASSOC	0x00A0
#define RTW_IEEE80211_STYPE_AUTH	0x00B0
#define RTW_IEEE80211_STYPE_DEAUTH	0x00C0
#define RTW_IEEE80211_STYPE_ACTION	0x00D0

/* control */
#define RTW_IEEE80211_STYPE_CTL_EXT	0x0060
#define RTW_IEEE80211_STYPE_BACK_REQ	0x0080
#define RTW_IEEE80211_STYPE_BACK	0x0090
#define RTW_IEEE80211_STYPE_PSPOLL	0x00A0
#define RTW_IEEE80211_STYPE_RTS		0x00B0
#define RTW_IEEE80211_STYPE_CTS		0x00C0
#define RTW_IEEE80211_STYPE_ACK		0x00D0
#define RTW_IEEE80211_STYPE_CFEND	0x00E0
#define RTW_IEEE80211_STYPE_CFENDACK	0x00F0

/* data */
#define RTW_IEEE80211_STYPE_DATA	0x0000
#define RTW_IEEE80211_STYPE_DATA_CFACK	0x0010
#define RTW_IEEE80211_STYPE_DATA_CFPOLL	0x0020
#define RTW_IEEE80211_STYPE_DATA_CFACKPOLL	0x0030
#define RTW_IEEE80211_STYPE_NULLFUNC	0x0040
#define RTW_IEEE80211_STYPE_CFACK	0x0050
#define RTW_IEEE80211_STYPE_CFPOLL	0x0060
#define RTW_IEEE80211_STYPE_CFACKPOLL	0x0070
#define RTW_IEEE80211_STYPE_QOS_DATA	0x0080
#define RTW_IEEE80211_STYPE_QOS_DATA_CFACK	0x0090
#define RTW_IEEE80211_STYPE_QOS_DATA_CFPOLL	0x00A0
#define RTW_IEEE80211_STYPE_QOS_DATA_CFACKPOLL	0x00B0
#define RTW_IEEE80211_STYPE_QOS_NULLFUNC	0x00C0
#define RTW_IEEE80211_STYPE_QOS_CFACK		0x00D0
#define RTW_IEEE80211_STYPE_QOS_CFPOLL		0x00E0
#define RTW_IEEE80211_STYPE_QOS_CFACKPOLL	0x00F0

/* sequence control field */
#define RTW_IEEE80211_SCTL_FRAG	0x000F
#define RTW_IEEE80211_SCTL_SEQ	0xFFF0

#define RTW_ERP_INFO_NON_ERP_PRESENT BIT(0)
#define RTW_ERP_INFO_USE_PROTECTION BIT(1)
#define RTW_ERP_INFO_BARKER_PREAMBLE_MODE BIT(2)

/* QoS, QOS */
#define NORMAL_ACK			0
#define NO_ACK				1
#define NON_EXPLICIT_ACK		2
#define BLOCK_ACK			3

#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888E /* Port Access Entity (IEEE 802.1X) */
#endif /* ETH_P_PAE */

#define ETH_P_PREAUTH 0x88C7 /* IEEE 802.11i pre-authentication */

#define ETH_P_ECONET	0x0018

#ifndef ETH_P_80211_RAW
#define ETH_P_80211_RAW (ETH_P_ECONET + 1)
#endif

/* IEEE 802.11 defines */

#define P80211_OUI_LEN 3

struct ieee80211_snap_hdr {
	u8    dsap;   /* always 0xAA */
	u8    ssap;   /* always 0xAA */
	u8    ctrl;   /* always 0x03 */
	u8    oui[P80211_OUI_LEN];    /* organizational universal id */
} __packed;

#define SNAP_SIZE sizeof(struct ieee80211_snap_hdr)

#define WLAN_FC_GET_TYPE(fc) ((fc) & RTW_IEEE80211_FCTL_FTYPE)
#define WLAN_FC_GET_STYPE(fc) ((fc) & RTW_IEEE80211_FCTL_STYPE)

#define WLAN_QC_GET_TID(qc) ((qc) & 0x0f)

#define WLAN_GET_SEQ_FRAG(seq) ((seq) & RTW_IEEE80211_SCTL_FRAG)
#define WLAN_GET_SEQ_SEQ(seq)  ((seq) & RTW_IEEE80211_SCTL_SEQ)

/* Authentication algorithms */
#define WLAN_AUTH_OPEN 0
#define WLAN_AUTH_SHARED_KEY 1

#define WLAN_AUTH_CHALLENGE_LEN 128

#define WLAN_CAPABILITY_BSS (1<<0)
#define WLAN_CAPABILITY_IBSS (1<<1)
#define WLAN_CAPABILITY_CF_POLLABLE (1<<2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST (1<<3)
#define WLAN_CAPABILITY_PRIVACY (1<<4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE (1<<5)
#define WLAN_CAPABILITY_PBCC (1<<6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY (1<<7)
#define WLAN_CAPABILITY_SHORT_SLOT (1<<10)

/* Status codes */
#define WLAN_STATUS_SUCCESS 0
#define WLAN_STATUS_UNSPECIFIED_FAILURE 1
#define WLAN_STATUS_CAPS_UNSUPPORTED 10
#define WLAN_STATUS_REASSOC_NO_ASSOC 11
#define WLAN_STATUS_ASSOC_DENIED_UNSPEC 12
#define WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG 13
#define WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION 14
#define WLAN_STATUS_CHALLENGE_FAIL 15
#define WLAN_STATUS_AUTH_TIMEOUT 16
#define WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA 17
#define WLAN_STATUS_ASSOC_DENIED_RATES 18
/* 802.11b */
#define WLAN_STATUS_ASSOC_DENIED_NOSHORT 19
#define WLAN_STATUS_ASSOC_DENIED_NOPBCC 20
#define WLAN_STATUS_ASSOC_DENIED_NOAGILITY 21

/* Reason codes */
#define WLAN_REASON_UNSPECIFIED 1
#define WLAN_REASON_PREV_AUTH_NOT_VALID 2
#define WLAN_REASON_DEAUTH_LEAVING 3
#define WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY 4
#define WLAN_REASON_DISASSOC_AP_BUSY 5
#define WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA 6
#define WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA 7
#define WLAN_REASON_DISASSOC_STA_HAS_LEFT 8
#define WLAN_REASON_STA_REQ_ASSOC_WITHOUT_AUTH 9
#define WLAN_REASON_JOIN_WRONG_CHANNEL       65534
#define WLAN_REASON_EXPIRATION_CHK 65535

/* Information Element IDs */
#define WLAN_EID_SSID 0
#define WLAN_EID_SUPP_RATES 1
#define WLAN_EID_FH_PARAMS 2
#define WLAN_EID_DS_PARAMS 3
#define WLAN_EID_CF_PARAMS 4
#define WLAN_EID_TIM 5
#define WLAN_EID_IBSS_PARAMS 6
#define WLAN_EID_CHALLENGE 16
/* EIDs defined by IEEE 802.11h - START */
#define WLAN_EID_PWR_CONSTRAINT 32
#define WLAN_EID_PWR_CAPABILITY 33
#define WLAN_EID_TPC_REQUEST 34
#define WLAN_EID_TPC_REPORT 35
#define WLAN_EID_SUPPORTED_CHANNELS 36
#define WLAN_EID_CHANNEL_SWITCH 37
#define WLAN_EID_MEASURE_REQUEST 38
#define WLAN_EID_MEASURE_REPORT 39
#define WLAN_EID_QUITE 40
#define WLAN_EID_IBSS_DFS 41
/* EIDs defined by IEEE 802.11h - END */
#define WLAN_EID_ERP_INFO 42
#define WLAN_EID_HT_CAP 45
#define WLAN_EID_RSN 48
#define WLAN_EID_EXT_SUPP_RATES 50
#define WLAN_EID_MOBILITY_DOMAIN 54
#define WLAN_EID_FAST_BSS_TRANSITION 55
#define WLAN_EID_TIMEOUT_INTERVAL 56
#define WLAN_EID_RIC_DATA 57
#define WLAN_EID_HT_OPERATION 61
#define WLAN_EID_SECONDARY_CHANNEL_OFFSET 62
#define WLAN_EID_20_40_BSS_COEXISTENCE 72
#define WLAN_EID_20_40_BSS_INTOLERANT 73
#define WLAN_EID_OVERLAPPING_BSS_SCAN_PARAMS 74
#define WLAN_EID_MMIE 76
#define WLAN_EID_VENDOR_SPECIFIC 221
#define WLAN_EID_GENERIC (WLAN_EID_VENDOR_SPECIFIC)

#define IEEE80211_MGMT_HDR_LEN 24
#define IEEE80211_DATA_HDR3_LEN 24
#define IEEE80211_DATA_HDR4_LEN 30

#define IEEE80211_STATMASK_SIGNAL (1<<0)
#define IEEE80211_STATMASK_RSSI (1<<1)
#define IEEE80211_STATMASK_NOISE (1<<2)
#define IEEE80211_STATMASK_RATE (1<<3)
#define IEEE80211_STATMASK_WEMASK 0x7

#define IEEE80211_CCK_MODULATION    (1<<0)
#define IEEE80211_OFDM_MODULATION   (1<<1)

#define IEEE80211_24GHZ_BAND     (1<<0)
#define IEEE80211_52GHZ_BAND     (1<<1)

#define IEEE80211_CCK_RATE_LEN			4
#define IEEE80211_NUM_OFDM_RATESLEN	8

#define IEEE80211_CCK_RATE_1MB			0x02
#define IEEE80211_CCK_RATE_2MB			0x04
#define IEEE80211_CCK_RATE_5MB			0x0B
#define IEEE80211_CCK_RATE_11MB			0x16
#define IEEE80211_OFDM_RATE_LEN			8
#define IEEE80211_OFDM_RATE_6MB			0x0C
#define IEEE80211_OFDM_RATE_9MB			0x12
#define IEEE80211_OFDM_RATE_12MB		0x18
#define IEEE80211_OFDM_RATE_18MB		0x24
#define IEEE80211_OFDM_RATE_24MB		0x30
#define IEEE80211_OFDM_RATE_36MB		0x48
#define IEEE80211_OFDM_RATE_48MB		0x60
#define IEEE80211_OFDM_RATE_54MB		0x6C
#define IEEE80211_BASIC_RATE_MASK		0x80

#define IEEE80211_CCK_RATE_1MB_MASK		(1<<0)
#define IEEE80211_CCK_RATE_2MB_MASK		(1<<1)
#define IEEE80211_CCK_RATE_5MB_MASK		(1<<2)
#define IEEE80211_CCK_RATE_11MB_MASK		(1<<3)
#define IEEE80211_OFDM_RATE_6MB_MASK		(1<<4)
#define IEEE80211_OFDM_RATE_9MB_MASK		(1<<5)
#define IEEE80211_OFDM_RATE_12MB_MASK		(1<<6)
#define IEEE80211_OFDM_RATE_18MB_MASK		(1<<7)
#define IEEE80211_OFDM_RATE_24MB_MASK		(1<<8)
#define IEEE80211_OFDM_RATE_36MB_MASK		(1<<9)
#define IEEE80211_OFDM_RATE_48MB_MASK		(1<<10)
#define IEEE80211_OFDM_RATE_54MB_MASK		(1<<11)

#define IEEE80211_CCK_RATES_MASK		0x0000000F
#define IEEE80211_CCK_BASIC_RATES_MASK	(IEEE80211_CCK_RATE_1MB_MASK | \
	IEEE80211_CCK_RATE_2MB_MASK)
#define IEEE80211_CCK_DEFAULT_RATES_MASK				\
	(IEEE80211_CCK_BASIC_RATES_MASK |				\
	IEEE80211_CCK_RATE_5MB_MASK |					\
	IEEE80211_CCK_RATE_11MB_MASK)

#define IEEE80211_OFDM_RATES_MASK		0x00000FF0
#define IEEE80211_OFDM_BASIC_RATES_MASK	(IEEE80211_OFDM_RATE_6MB_MASK | \
	IEEE80211_OFDM_RATE_12MB_MASK |					\
	IEEE80211_OFDM_RATE_24MB_MASK)
#define IEEE80211_OFDM_DEFAULT_RATES_MASK				\
	(IEEE80211_OFDM_BASIC_RATES_MASK |				\
	IEEE80211_OFDM_RATE_9MB_MASK  |					\
	IEEE80211_OFDM_RATE_18MB_MASK |					\
	IEEE80211_OFDM_RATE_36MB_MASK |					\
	IEEE80211_OFDM_RATE_48MB_MASK |					\
	IEEE80211_OFDM_RATE_54MB_MASK)
#define IEEE80211_DEFAULT_RATES_MASK					\
	(IEEE80211_OFDM_DEFAULT_RATES_MASK |				\
	 IEEE80211_CCK_DEFAULT_RATES_MASK)

#define IEEE80211_NUM_OFDM_RATES	8
#define IEEE80211_NUM_CCK_RATES		4
#define IEEE80211_OFDM_SHIFT_MASK_A	4

/* NOTE: This data is for statistical purposes; not all hardware provides this
 *       information for frames received.  Not setting these will not cause
 *       any adverse affects. */
struct ieee80211_rx_stats {
	/* u32 mac_time[2]; */
	s8 rssi;
	u8 signal;
	u8 noise;
	u8 received_channel;
	u16 rate; /* in 100 kbps */
	/* u8 control; */
	u8 mask;
	u8 freq;
	u16 len;
};

/* IEEE 802.11 requires that STA supports concurrent reception of at least
 * three fragmented frames. This define can be increased to support more
 * concurrent frames, but it should be noted that each entry can consume about
 * 2 kB of RAM and increasing cache size will slow down frame reassembly. */
#define IEEE80211_FRAG_CACHE_LEN 4

struct ieee80211_frag_entry {
	u32 first_frag_time;
	uint seq;
	uint last_frag;
	uint qos;   /* jackson */
	uint tid;	/* jackson */
	struct sk_buff *skb;
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
};

struct ieee80211_stats {
	uint tx_unicast_frames;
	uint tx_multicast_frames;
	uint tx_fragments;
	uint tx_unicast_octets;
	uint tx_multicast_octets;
	uint tx_deferred_transmissions;
	uint tx_single_retry_frames;
	uint tx_multiple_retry_frames;
	uint tx_retry_limit_exceeded;
	uint tx_discards;
	uint rx_unicast_frames;
	uint rx_multicast_frames;
	uint rx_fragments;
	uint rx_unicast_octets;
	uint rx_multicast_octets;
	uint rx_fcs_errors;
	uint rx_discards_no_buffer;
	uint tx_discards_wrong_sa;
	uint rx_discards_undecryptable;
	uint rx_message_in_msg_fragments;
	uint rx_message_in_bad_msg_fragments;
};

struct ieee80211_softmac_stats {
	uint rx_ass_ok;
	uint rx_ass_err;
	uint rx_probe_rq;
	uint tx_probe_rs;
	uint tx_beacons;
	uint rx_auth_rq;
	uint rx_auth_rs_ok;
	uint rx_auth_rs_err;
	uint tx_auth_rq;
	uint no_auth_rs;
	uint no_ass_rs;
	uint tx_ass_rq;
	uint rx_ass_rq;
	uint tx_probe_rq;
	uint reassoc;
	uint swtxstop;
	uint swtxawake;
};

#define SEC_KEY_1	(1<<0)
#define SEC_KEY_2	(1<<1)
#define SEC_KEY_3	(1<<2)
#define SEC_KEY_4	(1<<3)
#define SEC_ACTIVE_KEY  (1<<4)
#define SEC_AUTH_MODE   (1<<5)
#define SEC_UNICAST_GROUP (1<<6)
#define SEC_LEVEL	(1<<7)
#define SEC_ENABLED     (1<<8)

#define SEC_LEVEL_0      0 /* None */
#define SEC_LEVEL_1      1 /* WEP 40 and 104 bit */
#define SEC_LEVEL_2      2 /* Level 1 + TKIP */
#define SEC_LEVEL_2_CKIP 3 /* Level 1 + CKIP */
#define SEC_LEVEL_3      4 /* Level 2 + CCMP */

#define WEP_KEYS 4
#define WEP_KEY_LEN 13

struct ieee80211_security {
	u16 active_key:2,
	enabled:1,
	auth_mode:2,
	auth_algo:4,
	unicast_uses_group:1;
	u8 key_sizes[WEP_KEYS];
	u8 keys[WEP_KEYS][WEP_KEY_LEN];
	u8 level;
	u16 flags;
} __packed;

/*

 802.11 data frame from AP

      ,-------------------------------------------------------------------.
Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
      |------|------|---------|---------|---------|------|---------|------|
Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  frame  |  fcs |
      |      | tion | (BSSID) |	 |	 | ence |  data   |      |
      `-------------------------------------------------------------------'

Total: 28-2340 bytes

*/

struct ieee80211_header_data {
	u16 frame_ctl;
	u16 duration_id;
	u8 addr1[6];
	u8 addr2[6];
	u8 addr3[6];
	u16 seq_ctrl;
};

#define BEACON_PROBE_SSID_ID_POSITION 12

/* Management Frame Information Element Types */
#define MFIE_TYPE_SSID		0
#define MFIE_TYPE_RATES		1
#define MFIE_TYPE_FH_SET	2
#define MFIE_TYPE_DS_SET	3
#define MFIE_TYPE_CF_SET	4
#define MFIE_TYPE_TIM		5
#define MFIE_TYPE_IBSS_SET	6
#define MFIE_TYPE_CHALLENGE	16
#define MFIE_TYPE_ERP		42
#define MFIE_TYPE_RSN		48
#define MFIE_TYPE_RATES_EX	50
#define MFIE_TYPE_GENERIC	221

struct ieee80211_info_element_hdr {
	u8 id;
	u8 len;
} __packed;

struct ieee80211_info_element {
	u8 id;
	u8 len;
	u8 data[0];
} __packed;

/*
 * These are the data types that can make up management packets
 *
	u16 auth_algorithm;
	u16 auth_sequence;
	u16 beacon_interval;
	u16 capability;
	u8 current_ap[ETH_ALEN];
	u16 listen_interval;
	struct {
		u16 association_id:14, reserved:2;
	} __packed;
	u32 time_stamp[2];
	u16 reason;
	u16 status;
*/

#define IEEE80211_DEFAULT_TX_ESSID "Penguin"
#define IEEE80211_DEFAULT_BASIC_RATE 10

struct ieee80211_authentication {
	struct ieee80211_header_data header;
	u16 algorithm;
	u16 transaction;
	u16 status;
	/* struct ieee80211_info_element_hdr info_element; */
} __packed;

struct ieee80211_probe_response {
	struct ieee80211_header_data header;
	u32 time_stamp[2];
	u16 beacon_interval;
	u16 capability;
	struct ieee80211_info_element info_element;
} __packed;

struct ieee80211_probe_request {
	struct ieee80211_header_data header;
} __packed;

struct ieee80211_assoc_request_frame {
	struct rtw_ieee80211_hdr_3addr header;
	u16 capability;
	u16 listen_interval;
	struct ieee80211_info_element_hdr info_element;
} __packed;

struct ieee80211_assoc_response_frame {
	struct rtw_ieee80211_hdr_3addr header;
	u16 capability;
	u16 status;
	u16 aid;
} __packed;

struct ieee80211_txb {
	u8 nr_frags;
	u8 encrypted;
	u16 reserved;
	u16 frag_size;
	u16 payload_size;
	struct sk_buff *fragments[0];
};

/* SWEEP TABLE ENTRIES NUMBER*/
#define MAX_SWEEP_TAB_ENTRIES		  42
#define MAX_SWEEP_TAB_ENTRIES_PER_PACKET  7
/* MAX_RATES_LENGTH needs to be 12.  The spec says 8, and many APs
 * only use 8, and then use extended rates for the remaining supported
 * rates.  Other APs, however, stick all of their supported rates on the
 * main rates information element... */
#define MAX_RATES_LENGTH		((u8)12)
#define MAX_RATES_EX_LENGTH		((u8)16)
#define MAX_NETWORK_COUNT		128
#define MAX_CHANNEL_NUMBER		161
#define IEEE80211_SOFTMAC_SCAN_TIME	400
/* HZ / 2) */
#define IEEE80211_SOFTMAC_ASSOC_RETRY_TIME (HZ * 2)

#define CRC_LENGTH		 4U

#define MAX_WPA_IE_LEN (256)
#define MAX_WPS_IE_LEN (512)
#define MAX_P2P_IE_LEN (256)
#define MAX_WFD_IE_LEN (128)

#define NETWORK_EMPTY_ESSID (1<<0)
#define NETWORK_HAS_OFDM    (1<<1)
#define NETWORK_HAS_CCK     (1<<2)

#define IEEE80211_DTIM_MBCAST 4
#define IEEE80211_DTIM_UCAST 2
#define IEEE80211_DTIM_VALID 1
#define IEEE80211_DTIM_INVALID 0

#define IEEE80211_PS_DISABLED 0
#define IEEE80211_PS_UNICAST IEEE80211_DTIM_UCAST
#define IEEE80211_PS_MBCAST IEEE80211_DTIM_MBCAST
#define IW_ESSID_MAX_SIZE 32
/*
join_res:
-1: authentication fail
-2: association fail
> 0: TID
*/

enum ieee80211_state {
	/* the card is not linked at all */
	IEEE80211_NOLINK = 0,

	/* IEEE80211_ASSOCIATING* are for BSS client mode
	 * the driver shall not perform RX filtering unless
	 * the state is LINKED.
	 * The driver shall just check for the state LINKED and
	 * defaults to NOLINK for ALL the other states (including
	 * LINKED_SCANNING)
	 */

	/* the association procedure will start (wq scheduling)*/
	IEEE80211_ASSOCIATING,
	IEEE80211_ASSOCIATING_RETRY,

	/* the association procedure is sending AUTH request*/
	IEEE80211_ASSOCIATING_AUTHENTICATING,

	/* the association procedure has successfully authentcated
	 * and is sending association request
	 */
	IEEE80211_ASSOCIATING_AUTHENTICATED,

	/* the link is ok. the card associated to a BSS or linked
	 * to a ibss cell or acting as an AP and creating the bss
	 */
	IEEE80211_LINKED,

	/* same as LINKED, but the driver shall apply RX filter
	 * rules as we are in NO_LINK mode. As the card is still
	 * logically linked, but it is doing a syncro site survey
	 * then it will be back to LINKED state.
	 */
	IEEE80211_LINKED_SCANNING,

};

#define DEFAULT_MAX_SCAN_AGE (15 * HZ)
#define DEFAULT_FTS 2346

static inline int is_multicast_mac_addr(const u8 *addr)
{
	return ((addr[0] != 0xff) && (0x01 & addr[0]));
}

static inline int is_broadcast_mac_addr(const u8 *addr)
{
	return (addr[0] == 0xff) && (addr[1] == 0xff) && (addr[2] == 0xff) &&
	       (addr[3] == 0xff) && (addr[4] == 0xff) && (addr[5] == 0xff);
}

#define CFG_IEEE80211_RESERVE_FCS (1<<0)
#define CFG_IEEE80211_COMPUTE_FCS (1<<1)

struct tx_pending {
	int frag;
	struct ieee80211_txb *txb;
};

#define MAXTID	16

#define IEEE_A	    (1<<0)
#define IEEE_B	    (1<<1)
#define IEEE_G	    (1<<2)
#define IEEE_MODE_MASK    (IEEE_A|IEEE_B|IEEE_G)

/* Baron move to ieee80211.c */
int ieee80211_is_empty_essid(const char *essid, int essid_len);
int ieee80211_get_hdrlen(u16 fc);

/* Action category code */
enum rtw_ieee80211_category {
	RTW_WLAN_CATEGORY_SPECTRUM_MGMT = 0,
	RTW_WLAN_CATEGORY_QOS = 1,
	RTW_WLAN_CATEGORY_DLS = 2,
	RTW_WLAN_CATEGORY_BACK = 3,
	RTW_WLAN_CATEGORY_PUBLIC = 4, /* IEEE 802.11 public action frames */
	RTW_WLAN_CATEGORY_RADIO_MEASUREMENT  = 5,
	RTW_WLAN_CATEGORY_FT = 6,
	RTW_WLAN_CATEGORY_HT = 7,
	RTW_WLAN_CATEGORY_SA_QUERY = 8,
	RTW_WLAN_CATEGORY_TDLS = 12,
	RTW_WLAN_CATEGORY_WMM = 17,
	RTW_WLAN_CATEGORY_P2P = 0x7f,/* P2P action frames */
};

/* SPECTRUM_MGMT action code */
enum rtw_ieee80211_spectrum_mgmt_actioncode {
	RTW_WLAN_ACTION_SPCT_MSR_REQ = 0,
	RTW_WLAN_ACTION_SPCT_MSR_RPRT = 1,
	RTW_WLAN_ACTION_SPCT_TPC_REQ = 2,
	RTW_WLAN_ACTION_SPCT_TPC_RPRT = 3,
	RTW_WLAN_ACTION_SPCT_CHL_SWITCH = 4,
	RTW_WLAN_ACTION_SPCT_EXT_CHL_SWITCH = 5,
};

enum _PUBLIC_ACTION {
	ACT_PUBLIC_BSSCOEXIST = 0, /*  20/40 BSS Coexistence */
	ACT_PUBLIC_DSE_ENABLE = 1,
	ACT_PUBLIC_DSE_DEENABLE = 2,
	ACT_PUBLIC_DSE_REG_LOCATION = 3,
	ACT_PUBLIC_EXT_CHL_SWITCH = 4,
	ACT_PUBLIC_DSE_MSR_REQ = 5,
	ACT_PUBLIC_DSE_MSR_RPRT = 6,
	ACT_PUBLIC_MP = 7, /*  Measurement Pilot */
	ACT_PUBLIC_DSE_PWR_CONSTRAINT = 8,
	ACT_PUBLIC_VENDOR = 9, /*  for WIFI_DIRECT */
	ACT_PUBLIC_GAS_INITIAL_REQ = 10,
	ACT_PUBLIC_GAS_INITIAL_RSP = 11,
	ACT_PUBLIC_GAS_COMEBACK_REQ = 12,
	ACT_PUBLIC_GAS_COMEBACK_RSP = 13,
	ACT_PUBLIC_TDLS_DISCOVERY_RSP = 14,
	ACT_PUBLIC_LOCATION_TRACK = 15,
	ACT_PUBLIC_MAX
};

/* BACK action code */
enum rtw_ieee80211_back_actioncode {
	RTW_WLAN_ACTION_ADDBA_REQ = 0,
	RTW_WLAN_ACTION_ADDBA_RESP = 1,
	RTW_WLAN_ACTION_DELBA = 2,
};

/* HT features action code */
enum rtw_ieee80211_ht_actioncode {
	RTW_WLAN_ACTION_NOTIFY_CH_WIDTH = 0,
	RTW_WLAN_ACTION_SM_PS = 1,
	RTW_WLAN_ACTION_PSPM = 2,
	RTW_WLAN_ACTION_PCO_PHASE = 3,
	RTW_WLAN_ACTION_MIMO_CSI_MX = 4,
	RTW_WLAN_ACTION_MIMO_NONCP_BF = 5,
	RTW_WLAN_ACTION_MIMP_CP_BF = 6,
	RTW_WLAN_ACTION_ASEL_INDICATES_FB = 7,
	RTW_WLAN_ACTION_HI_INFO_EXCHG = 8,
};

/* BACK (block-ack) parties */
enum rtw_ieee80211_back_parties {
	RTW_WLAN_BACK_RECIPIENT = 0,
	RTW_WLAN_BACK_INITIATOR = 1,
	RTW_WLAN_BACK_TIMER = 2,
};

#define OUI_MICROSOFT 0x0050f2 /* Microsoft (also used in Wi-Fi specs)
				* 00:50:F2 */
#define WME_OUI_TYPE 2
#define WME_OUI_SUBTYPE_INFORMATION_ELEMENT 0
#define WME_OUI_SUBTYPE_PARAMETER_ELEMENT 1
#define WME_OUI_SUBTYPE_TSPEC_ELEMENT 2
#define WME_VERSION 1

#define WME_ACTION_CODE_SETUP_REQUEST 0
#define WME_ACTION_CODE_SETUP_RESPONSE 1
#define WME_ACTION_CODE_TEARDOWN 2

#define WME_SETUP_RESPONSE_STATUS_ADMISSION_ACCEPTED 0
#define WME_SETUP_RESPONSE_STATUS_INVALID_PARAMETERS 1
#define WME_SETUP_RESPONSE_STATUS_REFUSED 3

#define WME_TSPEC_DIRECTION_UPLINK 0
#define WME_TSPEC_DIRECTION_DOWNLINK 1
#define WME_TSPEC_DIRECTION_BI_DIRECTIONAL 3

#define OUI_BROADCOM 0x00904c /* Broadcom (Epigram) */

#define VENDOR_HT_CAPAB_OUI_TYPE 0x33 /* 00-90-4c:0x33 */

/**
 * enum rtw_ieee80211_channel_flags - channel flags
 *
 * Channel flags set by the regulatory control code.
 *
 * @RTW_IEEE80211_CHAN_DISABLED: This channel is disabled.
 * @RTW_IEEE80211_CHAN_PASSIVE_SCAN: Only passive scanning is permitted
 *      on this channel.
 * @RTW_IEEE80211_CHAN_NO_IBSS: IBSS is not allowed on this channel.
 * @RTW_IEEE80211_CHAN_RADAR: Radar detection is required on this channel.
 * @RTW_IEEE80211_CHAN_NO_HT40PLUS: extension channel above this channel
 *      is not permitted.
 * @RTW_IEEE80211_CHAN_NO_HT40MINUS: extension channel below this channel
 *      is not permitted.
 */
enum rtw_ieee80211_channel_flags {
	RTW_IEEE80211_CHAN_DISABLED	 = 1<<0,
	RTW_IEEE80211_CHAN_PASSIVE_SCAN     = 1<<1,
	RTW_IEEE80211_CHAN_NO_IBSS	  = 1<<2,
	RTW_IEEE80211_CHAN_RADAR	    = 1<<3,
	RTW_IEEE80211_CHAN_NO_HT40PLUS      = 1<<4,
	RTW_IEEE80211_CHAN_NO_HT40MINUS     = 1<<5,
};

#define RTW_IEEE80211_CHAN_NO_HT40 \
	  (RTW_IEEE80211_CHAN_NO_HT40PLUS | RTW_IEEE80211_CHAN_NO_HT40MINUS)

/* Represent channel details, subset of ieee80211_channel */
struct rtw_ieee80211_channel {
	u16 hw_value;
	u32 flags;
};

#define CHAN_FMT \
	"hw_value:%u, " \
	"flags:0x%08x" \

#define CHAN_ARG(channel) \
	(channel)->hw_value \
	, (channel)->flags \

/* Parsed Information Elements */
struct rtw_ieee802_11_elems {
	u8 *ssid;
	u8 ssid_len;
	u8 *supp_rates;
	u8 supp_rates_len;
	u8 *fh_params;
	u8 fh_params_len;
	u8 *ds_params;
	u8 ds_params_len;
	u8 *cf_params;
	u8 cf_params_len;
	u8 *tim;
	u8 tim_len;
	u8 *ibss_params;
	u8 ibss_params_len;
	u8 *challenge;
	u8 challenge_len;
	u8 *erp_info;
	u8 erp_info_len;
	u8 *ext_supp_rates;
	u8 ext_supp_rates_len;
	u8 *wpa_ie;
	u8 wpa_ie_len;
	u8 *rsn_ie;
	u8 rsn_ie_len;
	u8 *wme;
	u8 wme_len;
	u8 *wme_tspec;
	u8 wme_tspec_len;
	u8 *wps_ie;
	u8 wps_ie_len;
	u8 *power_cap;
	u8 power_cap_len;
	u8 *supp_channels;
	u8 supp_channels_len;
	u8 *mdie;
	u8 mdie_len;
	u8 *ftie;
	u8 ftie_len;
	u8 *timeout_int;
	u8 timeout_int_len;
	u8 *ht_capabilities;
	u8 ht_capabilities_len;
	u8 *ht_operation;
	u8 ht_operation_len;
	u8 *vendor_ht_cap;
	u8 vendor_ht_cap_len;
};

enum parse_res {
	ParseOK = 0,
	ParseUnknown = 1,
	ParseFailed = -1
};

enum parse_res rtw_ieee802_11_parse_elems(u8 *start, uint len,
					  struct rtw_ieee802_11_elems *elems,
					  int show_errors);

u8 *rtw_set_fixed_ie(unsigned char *pbuf, unsigned int len,
		     unsigned char *source, unsigned int *frlen);
u8 *rtw_set_ie(u8 *pbuf, int index, uint len, u8 *source, uint *frlen);

enum secondary_ch_offset {
	SCN = 0, /* no secondary channel */
	SCA = 1, /* secondary channel above */
	SCB = 3,  /* secondary channel below */
};
u8 secondary_ch_offset_to_hal_ch_offset(u8 ch_offset);
u8 hal_ch_offset_to_secondary_ch_offset(u8 ch_offset);
u8 *rtw_set_ie_ch_switch(u8 *buf, u32 *buf_len, u8 ch_switch_mode,
			 u8 new_ch, u8 ch_switch_cnt);
u8 *rtw_set_ie_secondary_ch_offset(u8 *buf, u32 *buf_len,
				   u8 secondary_ch_offset);
u8 *rtw_set_ie_mesh_ch_switch_parm(u8 *buf, u32 *buf_len, u8 ttl,
				   u8 flags, u16 reason, u16 precedence);

u8 *rtw_get_ie(u8 *pbuf, int index, int *len, int limit);
u8 *rtw_get_ie_ex(u8 *in_ie, uint in_len, u8 eid, u8 *oui,
		  u8 oui_len, u8 *ie, uint *ielen);
int rtw_ies_remove_ie(u8 *ies, uint *ies_len, uint offset,
		      u8 eid, u8 *oui, u8 oui_len);

void rtw_set_supported_rate(u8 *SupportedRates, uint mode);

unsigned char *rtw_get_wpa_ie(unsigned char *pie, int *wpa_ie_len, int limit);
unsigned char *rtw_get_wpa2_ie(unsigned char *pie, int *rsn_ie_len, int limit);
int rtw_get_wpa_cipher_suite(u8 *s);
int rtw_get_wpa2_cipher_suite(u8 *s);
int rtw_get_wapi_ie(u8 *in_ie, uint in_len, u8 *wapi_ie, u16 *wapi_len);
int rtw_parse_wpa_ie(u8 *wpa_ie, int wpa_ie_len, int *group_cipher,
		     int *pairwise_cipher, int *is_8021x);
int rtw_parse_wpa2_ie(u8 *wpa_ie, int wpa_ie_len, int *group_cipher,
		      int *pairwise_cipher, int *is_8021x);

int rtw_get_sec_ie(u8 *in_ie, uint in_len, u8 *rsn_ie, u16 *rsn_len,
		   u8 *wpa_ie, u16 *wpa_len);

u8 rtw_is_wps_ie(u8 *ie_ptr, uint *wps_ielen);
u8 *rtw_get_wps_ie(u8 *in_ie, uint in_len, u8 *wps_ie, uint *wps_ielen);
u8 *rtw_get_wps_attr(u8 *wps_ie, uint wps_ielen, u16 target_attr_id,
		     u8 *buf_attr, u32 *len_attr);
u8 *rtw_get_wps_attr_content(u8 *wps_ie, uint wps_ielen, u16 target_attr_id,
			     u8 *buf_content, uint *len_content);

/**
 * for_each_ie - iterate over continuous IEs
 * @ie:
 * @buf:
 * @buf_len:
 */
#define for_each_ie(ie, buf, buf_len) \
	for (ie = (void *)buf; (((u8 *)ie) - ((u8 *)buf) + 1) < buf_len;	\
		ie = (void *)(((u8 *)ie) + *(((u8 *)ie)+1) + 2))

void dump_ies(u8 *buf, u32 buf_len);
void dump_wps_ie(u8 *ie, u32 ie_len);

#ifdef CONFIG_88EU_P2P
void dump_p2p_ie(u8 *ie, u32 ie_len);
u8 *rtw_get_p2p_ie(u8 *in_ie, int in_len, u8 *p2p_ie, uint *p2p_ielen);
u8 *rtw_get_p2p_attr(u8 *p2p_ie, uint p2p_ielen, u8 target_attr_id,
		     u8 *buf_attr, u32 *len_attr);
u8 *rtw_get_p2p_attr_content(u8 *p2p_ie, uint p2p_ielen, u8 target_attr_id,
			     u8 *buf_content, uint *len_content);
u32 rtw_set_p2p_attr_content(u8 *pbuf, u8 attr_id, u16 attr_len,
			     u8 *pdata_attr);
void rtw_wlan_bssid_ex_remove_p2p_attr(struct wlan_bssid_ex *bss_ex,
				       u8 attr_id);
#endif

uint	rtw_get_rateset_len(u8	*rateset);

struct registry_priv;
int rtw_generate_ie(struct registry_priv *pregistrypriv);

int rtw_get_bit_value_from_ieee_value(u8 val);

uint	rtw_is_cckrates_included(u8 *rate);

uint	rtw_is_cckratesonly_included(u8 *rate);

int rtw_check_network_type(unsigned char *rate, int ratelen, int channel);

void rtw_get_bcn_info(struct wlan_network *pnetwork);

void rtw_macaddr_cfg(u8 *mac_addr);

u16 rtw_mcs_rate(u8 rf_type, u8 bw_40MHz, u8 short_GI_20, u8 short_GI_40,
		 unsigned char *MCS_rate);

int rtw_action_frame_parse(const u8 *frame, u32 frame_len, u8 *category,
			   u8 *action);
const char *action_public_str(u8 action);

#endif /* IEEE80211_H */
