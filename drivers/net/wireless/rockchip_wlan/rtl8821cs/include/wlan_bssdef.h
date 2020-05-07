/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __WLAN_BSSDEF_H__
#define __WLAN_BSSDEF_H__


#define MAX_IE_SZ	768


#ifdef PLATFORM_LINUX

#define NDIS_802_11_LENGTH_SSID         32
#define NDIS_802_11_LENGTH_RATES        8
#define NDIS_802_11_LENGTH_RATES_EX     16

typedef unsigned char   NDIS_802_11_MAC_ADDRESS[ETH_ALEN];
typedef long    		NDIS_802_11_RSSI;           /* in dBm */
typedef unsigned char   NDIS_802_11_RATES[NDIS_802_11_LENGTH_RATES];        /* Set of 8 data rates */
typedef unsigned char   NDIS_802_11_RATES_EX[NDIS_802_11_LENGTH_RATES_EX];  /* Set of 16 data rates */

typedef struct _NDIS_802_11_SSID {
	u32  SsidLength;
	u8  Ssid[32];
} NDIS_802_11_SSID, *PNDIS_802_11_SSID;

/*
	FW will only save the channel number in DSConfig.
	ODI Handler will convert the channel number to freq. number.
*/
typedef struct _NDIS_802_11_CONFIGURATION {
	u32           Length;             /* Length of structure */
	u32           BeaconPeriod;       /* units are Kusec */
	u32           ATIMWindow;         /* units are Kusec */
	u32           DSConfig;           /* channel number */
} NDIS_802_11_CONFIGURATION, *PNDIS_802_11_CONFIGURATION;

typedef enum _NDIS_802_11_NETWORK_INFRASTRUCTURE {
	Ndis802_11IBSS,
	Ndis802_11Infrastructure,
	Ndis802_11AutoUnknown,
	Ndis802_11InfrastructureMax,     /* Not a real value, defined as upper bound */
	Ndis802_11APMode,
	Ndis802_11Monitor,
	Ndis802_11_mesh,
} NDIS_802_11_NETWORK_INFRASTRUCTURE, *PNDIS_802_11_NETWORK_INFRASTRUCTURE;

typedef struct _NDIS_802_11_FIXED_IEs {
	u8  Timestamp[8];
	u16  BeaconInterval;
	u16  Capabilities;
} NDIS_802_11_FIXED_IEs, *PNDIS_802_11_FIXED_IEs;

typedef struct _NDIS_802_11_VARIABLE_IEs {
	u8  ElementID;
	u8  Length;
	u8  data[1];
} NDIS_802_11_VARIABLE_IEs, *PNDIS_802_11_VARIABLE_IEs;

typedef enum _NDIS_802_11_AUTHENTICATION_MODE {
	Ndis802_11AuthModeOpen,
	Ndis802_11AuthModeShared,
	Ndis802_11AuthModeAutoSwitch,
	Ndis802_11AuthModeWPA,
	Ndis802_11AuthModeWPAPSK,
	Ndis802_11AuthModeWPANone,
	Ndis802_11AuthModeWAPI,
	Ndis802_11AuthModeMax               /* Not a real mode, defined as upper bound */
} NDIS_802_11_AUTHENTICATION_MODE, *PNDIS_802_11_AUTHENTICATION_MODE;

typedef enum _NDIS_802_11_WEP_STATUS {
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
} NDIS_802_11_WEP_STATUS, *PNDIS_802_11_WEP_STATUS,
NDIS_802_11_ENCRYPTION_STATUS, *PNDIS_802_11_ENCRYPTION_STATUS;

typedef struct _NDIS_802_11_WEP {
	u32     Length;        /* Length of this structure */
	u32     KeyIndex;      /* 0 is the per-client key, 1-N are the global keys */
	u32     KeyLength;     /* length of key in bytes */
	u8     KeyMaterial[16];/* variable length depending on above field */
} NDIS_802_11_WEP, *PNDIS_802_11_WEP;

#endif /* end of #ifdef PLATFORM_LINUX */

#ifdef PLATFORM_FREEBSD

#define NDIS_802_11_LENGTH_SSID         32
#define NDIS_802_11_LENGTH_RATES        8
#define NDIS_802_11_LENGTH_RATES_EX     16

typedef unsigned char   NDIS_802_11_MAC_ADDRESS[ETH_ALEN];
typedef long    		NDIS_802_11_RSSI;           /* in dBm */
typedef unsigned char   NDIS_802_11_RATES[NDIS_802_11_LENGTH_RATES];        /* Set of 8 data rates */
typedef unsigned char   NDIS_802_11_RATES_EX[NDIS_802_11_LENGTH_RATES_EX];  /* Set of 16 data rates */


typedef struct _NDIS_802_11_SSID {
	u32  SsidLength;
	u8  Ssid[32];
} NDIS_802_11_SSID, *PNDIS_802_11_SSID;

/*
	FW will only save the channel number in DSConfig.
	ODI Handler will convert the channel number to freq. number.
*/
typedef struct _NDIS_802_11_CONFIGURATION {
	u32           Length;             /* Length of structure */
	u32           BeaconPeriod;       /* units are Kusec */
	u32           ATIMWindow;         /* units are Kusec */
	u32           DSConfig;           /* channel number */
} NDIS_802_11_CONFIGURATION, *PNDIS_802_11_CONFIGURATION;

typedef enum _NDIS_802_11_NETWORK_INFRASTRUCTURE {
	Ndis802_11IBSS,
	Ndis802_11Infrastructure,
	Ndis802_11AutoUnknown,
	Ndis802_11InfrastructureMax,     /* Not a real value, defined as upper bound */
	Ndis802_11APMode
} NDIS_802_11_NETWORK_INFRASTRUCTURE, *PNDIS_802_11_NETWORK_INFRASTRUCTURE;

typedef struct _NDIS_802_11_FIXED_IEs {
	u8  Timestamp[8];
	u16  BeaconInterval;
	u16  Capabilities;
} NDIS_802_11_FIXED_IEs, *PNDIS_802_11_FIXED_IEs;

typedef struct _NDIS_802_11_VARIABLE_IEs {
	u8  ElementID;
	u8  Length;
	u8  data[1];
} NDIS_802_11_VARIABLE_IEs, *PNDIS_802_11_VARIABLE_IEs;

typedef enum _NDIS_802_11_AUTHENTICATION_MODE {
	Ndis802_11AuthModeOpen,
	Ndis802_11AuthModeShared,
	Ndis802_11AuthModeAutoSwitch,
	Ndis802_11AuthModeWPA,
	Ndis802_11AuthModeWPAPSK,
	Ndis802_11AuthModeWPANone,
	Ndis802_11AuthModeMax               /* Not a real mode, defined as upper bound */
} NDIS_802_11_AUTHENTICATION_MODE, *PNDIS_802_11_AUTHENTICATION_MODE;

typedef enum _NDIS_802_11_WEP_STATUS {
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
	Ndis802_11Encryption3KeyAbsent
} NDIS_802_11_WEP_STATUS, *PNDIS_802_11_WEP_STATUS,
NDIS_802_11_ENCRYPTION_STATUS, *PNDIS_802_11_ENCRYPTION_STATUS;


typedef struct _NDIS_802_11_WEP {
	u32     Length;        /* Length of this structure */
	u32     KeyIndex;      /* 0 is the per-client key, 1-N are the global keys */
	u32     KeyLength;     /* length of key in bytes */
	u8     KeyMaterial[16];/* variable length depending on above field */
} NDIS_802_11_WEP, *PNDIS_802_11_WEP;

#endif /* PLATFORM_FREEBSD */

#ifndef Ndis802_11APMode
#define Ndis802_11APMode (Ndis802_11InfrastructureMax+1)
#endif

typedef struct _WLAN_PHY_INFO {
	u8	SignalStrength;/* (in percentage) */
	u8	SignalQuality;/* (in percentage) */
	u8	Optimum_antenna;  /* for Antenna diversity */
	u8	is_cck_rate;	/* 1:cck_rate */
	s8	rx_snr[4];
#ifdef CONFIG_RTW_80211K
	u32	free_cnt; 	/* freerun counter */
	u8	rm_en_cap[5];
#endif
} WLAN_PHY_INFO, *PWLAN_PHY_INFO;

typedef struct _WLAN_BCN_INFO {
	/* these infor get from rtw_get_encrypt_info when
	 *	 * translate scan to UI */
	u8 encryp_protocol;/* ENCRYP_PROTOCOL_E: OPEN/WEP/WPA/WPA2/WAPI */
	int group_cipher; /* WPA/WPA2 group cipher */
	int pairwise_cipher;/* //WPA/WPA2/WEP pairwise cipher */
	int is_8021x;

	/* bwmode 20/40 and ch_offset UP/LOW */
	unsigned short	ht_cap_info;
	unsigned char	ht_info_infos_0;
} WLAN_BCN_INFO, *PWLAN_BCN_INFO;

enum bss_type {
	BSS_TYPE_UNDEF,
	BSS_TYPE_BCN = 1,
	BSS_TYPE_PROB_REQ = 2,
	BSS_TYPE_PROB_RSP = 3,
};

/* temporally add #pragma pack for structure alignment issue of
*   WLAN_BSSID_EX and get_WLAN_BSSID_EX_sz()
*/
typedef struct _WLAN_BSSID_EX {
	u32  Length;
	NDIS_802_11_MAC_ADDRESS  MacAddress;
	u8  Reserved[2];/* [0]: IS beacon frame , bss_type*/
	NDIS_802_11_SSID  Ssid;
	NDIS_802_11_SSID  mesh_id;
	u32  Privacy;
	NDIS_802_11_RSSI  Rssi;/* (in dBM,raw data ,get from PHY) */
	NDIS_802_11_CONFIGURATION  Configuration;
	NDIS_802_11_NETWORK_INFRASTRUCTURE  InfrastructureMode;
	NDIS_802_11_RATES_EX  SupportedRates;
	WLAN_PHY_INFO	PhyInfo;
	u32  IELength;
	u8  IEs[MAX_IE_SZ];	/* (timestamp, beacon interval, and capability information) */
}
__attribute__((packed)) WLAN_BSSID_EX, *PWLAN_BSSID_EX;

#define BSS_EX_IES(bss_ex) ((bss_ex)->IEs)
#define BSS_EX_IES_LEN(bss_ex) ((bss_ex)->IELength)
#define BSS_EX_FIXED_IE_OFFSET(bss_ex) ((bss_ex)->Reserved[0] == BSS_TYPE_PROB_REQ ? 0 : 12)
#define BSS_EX_TLV_IES(bss_ex) (BSS_EX_IES((bss_ex)) + BSS_EX_FIXED_IE_OFFSET((bss_ex)))
#define BSS_EX_TLV_IES_LEN(bss_ex) (BSS_EX_IES_LEN((bss_ex)) - BSS_EX_FIXED_IE_OFFSET((bss_ex)))

__inline  static uint get_WLAN_BSSID_EX_sz(WLAN_BSSID_EX *bss)
{
	return sizeof(WLAN_BSSID_EX) - MAX_IE_SZ + bss->IELength;
}

struct	wlan_network {
	_list	list;
	int	network_type;	/* refer to ieee80211.h for WIRELESS_11A/B/G */
	int	fixed;			/* set to fixed when not to be removed as site-surveying */
	systime last_scanned; /* timestamp for the network */
#ifdef CONFIG_RTW_MESH
#if CONFIG_RTW_MESH_ACNODE_PREVENT
	systime acnode_stime;
	systime acnode_notify_etime;
#endif
#endif
	int	aid;			/* will only be valid when a BSS is joinned. */
	int	join_res;
	WLAN_BSSID_EX	network; /* must be the last item */
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




#define PWR_CAM 0
#define PWR_MINPS 1
#define PWR_MAXPS 2
#define PWR_UAPSD 3
#define PWR_VOIP 4


enum UAPSD_MAX_SP {
	NO_LIMIT,
	TWO_MSDU,
	FOUR_MSDU,
	SIX_MSDU
};


/* john */
#define NUM_PRE_AUTH_KEY 16
#define NUM_PMKID_CACHE NUM_PRE_AUTH_KEY

#endif /* #ifndef WLAN_BSSDEF_H_ */
