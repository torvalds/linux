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

enum ndis_802_11_net_type {
	Ndis802_11FH,
	Ndis802_11DS,
	Ndis802_11OFDM5,
	Ndis802_11OFDM24,
	Ndis802_11NetworkTypeMax    /*  just an upper bound */
};

struct ndis_802_11_configuration_fh {
	u32           Length;             /*  Length of structure */
	u32           HopPattern;         /*  As defined by 802.11, MSB set */
	u32           HopSet;             /*  to one if non-802.11 */
	u32           DwellTime;          /*  units are Kusec */
};


/*
	FW will only save the channel number in DSConfig.
	ODI Handler will convert the channel number to freq. number.
*/
struct ndis_802_11_config {
	u32           Length;             /*  Length of structure */
	u32           BeaconPeriod;       /*  units are Kusec */
	u32           ATIMWindow;         /*  units are Kusec */
	u32           DSConfig;           /*  Frequency, units are kHz */
	struct ndis_802_11_configuration_fh    FHConfig;
};

enum ndis_802_11_net_infra {
	Ndis802_11IBSS,
	Ndis802_11Infrastructure,
	Ndis802_11AutoUnknown,
	Ndis802_11InfrastructureMax,     /*  Not a real value, defined as upper bound */
	Ndis802_11APMode
};

struct ndis_802_11_fixed_ies {
	u8  Timestamp[8];
	u16  BeaconInterval;
	u16  Capabilities;
};

struct ndis_802_11_var_ies {
	u8  ElementID;
	u8  Length;
	u8  data[1];
};

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

struct ndis_802_11_wep {
	u32     Length;        /*  Length of this structure */
	u32     KeyIndex;      /*  0 is the per-client key, 1-N are global */
	u32     KeyLength;     /*  length of key in bytes */
	u8     KeyMaterial[16];/*  variable length depending on above field */
};

enum NDIS_802_11_STATUS_TYPE {
	Ndis802_11StatusType_Authentication,
	Ndis802_11StatusType_MediaStreamMode,
	Ndis802_11StatusType_PMKID_CandidateList,
	Ndis802_11StatusTypeMax    /*  not a real type, just an upper bound */
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
#define Ndis802_11APMode (Ndis802_11InfrastructureMax+1)
#endif

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
	unsigned short	ht_cap_info;
	unsigned char	ht_info_infos_0;
};

struct wlan_bssid_ex {
	u32  Length;
	u8 MacAddress[ETH_ALEN];
	u16 reserved;
	struct cfg80211_ssid Ssid;
	u32  Privacy;
	long  Rssi;/* in dBM, raw data , get from PHY) */
	enum ndis_802_11_net_type  NetworkTypeInUse;
	struct ndis_802_11_config  Configuration;
	enum ndis_802_11_net_infra  InfrastructureMode;
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
