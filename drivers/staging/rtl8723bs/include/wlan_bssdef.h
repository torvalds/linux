/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __WLAN_BSSDEF_H__
#define __WLAN_BSSDEF_H__


#define MAX_IE_SZ	768


#define NDIS_802_11_LENGTH_SSID         32
#define NDIS_802_11_LENGTH_RATES        8
#define NDIS_802_11_LENGTH_RATES_EX     16

typedef unsigned char   NDIS_802_11_MAC_ADDRESS[6];
typedef unsigned char   NDIS_802_11_RATES[NDIS_802_11_LENGTH_RATES];        /*  Set of 8 data rates */
typedef unsigned char   NDIS_802_11_RATES_EX[NDIS_802_11_LENGTH_RATES_EX];  /*  Set of 16 data rates */

struct ndis_802_11_ssid {
	u32  ssid_length;
	u8  ssid[32];
};

enum ndis_802_11_network_type {
	Ndis802_11FH,
	Ndis802_11DS,
	Ndis802_11OFDM5,
	Ndis802_11OFDM24,
	Ndis802_11NetworkTypeMax    /*  not a real type, defined as an upper bound */
};

/*
	FW will only save the channel number in DSConfig.
	ODI Handler will convert the channel number to freq. number.
*/
struct ndis_802_11_conf {
	u32 length;             /*  Length of structure */
	u32 beacon_period;       /*  units are Kusec */
	u32 atim_window;         /*  units are Kusec */
	u32 ds_config;           /*  Frequency, units are kHz */
};

enum ndis_802_11_network_infrastructure {
	Ndis802_11IBSS,
	Ndis802_11Infrastructure,
	Ndis802_11AutoUnknown,
	Ndis802_11InfrastructureMax,     /*  Not a real value, defined as upper bound */
	Ndis802_11APMode,
};

struct ndis_802_11_fix_ie {
	u8  time_stamp[8];
	u16  beacon_interval;
	u16  capabilities;
};

struct ndis_80211_var_ie {
	u8  element_id;
	u8  length;
	u8  data[];
};

/* Length is the 4 bytes multiples of the sum of
 * sizeof (NDIS_802_11_MAC_ADDRESS) + 2 +
 * sizeof (struct ndis_802_11_ssid) + sizeof (u32) +
 * sizeof (long) + sizeof (enum ndis_802_11_network_type) +
 * sizeof (struct ndis_802_11_conf) + sizeof (NDIS_802_11_RATES_EX) + ie_length
 *
 * Except for ie_length, all other fields are fixed length. Therefore, we can
 * define a macro to present the partial sum.
 */
enum ndis_802_11_authentication_mode {
	Ndis802_11AuthModeOpen,
	Ndis802_11AuthModeShared,
	Ndis802_11AuthModeAutoSwitch,
	Ndis802_11AuthModeWPA,
	Ndis802_11AuthModeWPAPSK,
	Ndis802_11AuthModeWPANone,
	Ndis802_11AuthModeWAPI,
	Ndis802_11AuthModeMax   /*  Not a real mode, defined as upper bound */
};

enum {
	Ndis802_11WEPEnabled,
	Ndis802_11Encryption1Enabled = Ndis802_11WEPEnabled,
	Ndis802_11WEPDisabled,
	Ndis802_11EncryptionDisabled = Ndis802_11WEPDisabled,
	Ndis802_11WEPKeyAbsent,
	Ndis802_11Encryption1KeyAbsent = Ndis802_11WEPKeyAbsent,
	Ndis802_11WEPNotSupported,
	Ndis802_11EncryptionNotSupported = Ndis802_11WEPNotSupported,
	Ndis802_11Encryption2Enabled,
	Ndis802_11Encryption2KeyAbsent,
	Ndis802_11Encryption3Enabled,
	Ndis802_11Encryption3KeyAbsent,
	Ndis802_11_EncrypteionWAPI
};

#define NDIS_802_11_AI_REQFI_CAPABILITIES      1
#define NDIS_802_11_AI_REQFI_LISTENINTERVAL    2
#define NDIS_802_11_AI_REQFI_CURRENTAPADDRESS  4

#define NDIS_802_11_AI_RESFI_CAPABILITIES      1
#define NDIS_802_11_AI_RESFI_STATUSCODE        2
#define NDIS_802_11_AI_RESFI_ASSOCIATIONID     4

/*  Key mapping keys require a BSSID */

struct ndis_802_11_wep {
	u32 length;        /*  Length of this structure */
	u32 key_index;      /*  0 is the per-client key, 1-N are the global keys */
	u32 key_length;     /*  length of key in bytes */
	u8 key_material[16];/*  variable length depending on above field */
};

/*  mask for authentication/integrity fields */
#define NDIS_802_11_AUTH_REQUEST_AUTH_FIELDS        0x0f
#define NDIS_802_11_AUTH_REQUEST_REAUTH			0x01
#define NDIS_802_11_AUTH_REQUEST_KEYUPDATE		0x02
#define NDIS_802_11_AUTH_REQUEST_PAIRWISE_ERROR		0x06
#define NDIS_802_11_AUTH_REQUEST_GROUP_ERROR		0x0E

/*  MIC check time, 60 seconds. */
#define MIC_CHECK_TIME	60000000

#ifndef Ndis802_11APMode
#define Ndis802_11APMode (Ndis802_11InfrastructureMax + 1)
#endif

struct wlan_phy_info {
	u8 signal_strength;/* in percentage) */
	u8 signal_quality;/* in percentage) */
	u8 optimum_antenna;  /* for Antenna diversity */
	u8 reserved_0;
};

struct wlan_bcn_info {
	/* these infor get from rtw_get_encrypt_info when
	 * * translate scan to UI */
	u8 encryp_protocol;/* ENCRYP_PROTOCOL_E: OPEN/WEP/WPA/WPA2/WAPI */
	int group_cipher; /* WPA/WPA2 group cipher */
	int pairwise_cipher;/* WPA/WPA2/WEP pairwise cipher */
	int is_8021x;

	/* bwmode 20/40 and ch_offset UP/LOW */
	unsigned short	ht_cap_info;
	unsigned char ht_info_infos_0;
};

/* temporally add #pragma pack for structure alignment issue of
*   struct wlan_bssid_ex and get_wlan_bssid_ex_sz()
*/
struct wlan_bssid_ex {
	u32  length;
	NDIS_802_11_MAC_ADDRESS  mac_address;
	u8  reserved[2];/* 0]: IS beacon frame */
	struct ndis_802_11_ssid  ssid;
	u32  privacy;
	long  rssi;/* in dBM, raw data , get from PHY) */
	enum ndis_802_11_network_type  network_type_in_use;
	struct ndis_802_11_conf  configuration;
	enum ndis_802_11_network_infrastructure  infrastructure_mode;
	NDIS_802_11_RATES_EX  supported_rates;
	struct wlan_phy_info phy_info;
	u32  ie_length;
	u8  ies[MAX_IE_SZ];	/* timestamp, beacon interval, and capability information) */
} __packed;

static inline uint get_wlan_bssid_ex_sz(struct wlan_bssid_ex *bss)
{
	return (sizeof(struct wlan_bssid_ex) - MAX_IE_SZ + bss->ie_length);
}

struct	wlan_network {
	struct list_head	list;
	int	network_type;	/* refer to ieee80211.h for WIRELESS_11A/B/G */
	int	fixed;			/*  set to fixed when not to be removed as site-surveying */
	unsigned long	last_scanned; /* timestamp for the network */
	int	aid;			/* will only be valid when a BSS is joinned. */
	int	join_res;
	struct wlan_bssid_ex	network; /* must be the last item */
	struct wlan_bcn_info	bcn_info;
};

enum {
	DISABLE_VCS,
	ENABLE_VCS,
	AUTO_VCS
};

enum {
	NONE_VCS,
	RTS_CTS,
	CTS_TO_SELF
};

#define PWR_CAM 0
#define PWR_MINPS 1
#define PWR_MAXPS 2
#define PWR_UAPSD 3
#define PWR_VOIP 4

enum {
	NO_LIMIT,
	TWO_MSDU,
	FOUR_MSDU,
	SIX_MSDU
};

#define NUM_PRE_AUTH_KEY 16
#define NUM_PMKID_CACHE NUM_PRE_AUTH_KEY

#endif /* ifndef WLAN_BSSDEF_H_ */
