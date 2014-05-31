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
 ******************************************************************************/
#ifndef __WLAN_BSSDEF_H__
#define __WLAN_BSSDEF_H__


#define MAX_IE_SZ	768


#define NDIS_802_11_LENGTH_RATES        8
#define NDIS_802_11_LENGTH_RATES_EX     16

/* Length is the 4 bytes multiples of the sum of
 * sizeof(6 * sizeof(unsigned char)) + 2 + sizeof(struct ndis_802_11_ssid) +
 * sizeof(u32) + sizeof(long) + sizeof(enum ndis_802_11_net_type) +
 * sizeof(struct ndis_802_11_config) + sizeof(sizeof(unsigned char) *
 * NDIS_802_11_LENGTH_RATES_EX) + IELength
 *
 * Except the IELength, all other fields are fixed length. Therefore,
 * we can define a macro to present the partial sum.
 */

enum ndis_802_11_auth_mode {
	Ndis802_11AuthModeOpen,
	Ndis802_11AuthModeShared,
	Ndis802_11AuthModeAutoSwitch,
	Ndis802_11AuthModeWPA,
	Ndis802_11AuthModeWPAPSK,
	Ndis802_11AuthModeWPANone,
	dis802_11AuthModeMax       /*  upper bound */
};

enum  {
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
};

/*  Key mapping keys require a BSSID */
struct ndis_802_11_key {
	u32 Length;             /*  Length of this structure */
	u32 KeyIndex;
	u32 KeyLength;          /*  length of key in bytes */
	unsigned char BSSID[6];
	unsigned long long KeyRSC;
	u8 KeyMaterial[32]; /*  variable length depending on above field */
};

struct wlan_phy_info {
	u8	SignalStrength;/* in percentage) */
	u8	SignalQuality;/* in percentage) */
	u8	Optimum_antenna;  /* for Antenna diversity */
	u8	Reserved_0;
};

struct wlan_bcn_info {
	/* these infor get from rtw_get_encrypt_info when
	 *	 * translate scan to UI */
	u8 encryp_protocol;/* ENCRYP_PROTOCOL_E: OPEN/WEP/WPA/WPA2 */
	int group_cipher; /* WPA/WPA2 group cipher */
	int pairwise_cipher;/* WPA/WPA2/WEP pairwise cipher */
	int is_8021x;

	/* bwmode 20/40 and ch_offset UP/LOW */
};

struct wlan_bssid_ex {
	u32  Length;
	u8 MacAddress[ETH_ALEN];
	u16 reserved;
	struct cfg80211_ssid Ssid;
	u32  Privacy;
	long  Rssi;/* in dBM, raw data , get from PHY) */
	u16 BeaconPeriod;       /*  units are Kusec */
	u32 ATIMWindow;         /*  units are Kusec */
	u32 DSConfig;           /*  Frequency, units are kHz */
	enum nl80211_iftype ifmode;
	unsigned char SupportedRates[NDIS_802_11_LENGTH_RATES_EX];
	struct wlan_phy_info	PhyInfo;
	u32  IELength;
	u8  IEs[MAX_IE_SZ]; /* timestamp, beacon interval, and capability info*/
} __packed;

static inline uint get_wlan_bssid_ex_sz(struct wlan_bssid_ex *bss)
{
	return sizeof(struct wlan_bssid_ex) - MAX_IE_SZ + bss->IELength;
}

struct	wlan_network {
	struct list_head	list;
	int	network_type;	/* refer to ieee80211.h for 11A/B/G */
	/*  set to fixed when not to be removed as site-surveying */
	int	fixed;
	unsigned long	last_scanned; /* timestamp for the network */
	int	aid;		/* will only be valid when a BSS is joined. */
	int	join_res;
	struct wlan_bssid_ex	network; /* must be the last item */
	struct wlan_bcn_info	BcnInfo;
};

enum VRTL_CARRIER_SENSE {
	DISABLE_VCS,
	ENABLE_VCS,
	AUTO_VCS
};

enum VCS_TYPE {
	NONE_VCS,
	RTS_CTS,
	CTS_TO_SELF
};

/* john */
#define NUM_PRE_AUTH_KEY 16
#define NUM_PMKID_CACHE NUM_PRE_AUTH_KEY

#endif /* ifndef WLAN_BSSDEF_H_ */
