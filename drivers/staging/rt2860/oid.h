/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

	Module Name:
	oid.h

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Name		Date			Modification logs
*/
#ifndef _OID_H_
#define _OID_H_

/*#include <linux/wireless.h> */

#ifndef TRUE
#define TRUE				1
#endif
#ifndef FALSE
#define FALSE				0
#endif
/* */
/* IEEE 802.11 Structures and definitions */
/* */
#define MAX_TX_POWER_LEVEL              100	/* mW */
#define MAX_RSSI_TRIGGER                -10	/* dBm */
#define MIN_RSSI_TRIGGER                -200	/* dBm */
#define MAX_FRAG_THRESHOLD              2346	/* byte count */
#define MIN_FRAG_THRESHOLD              256	/* byte count */
#define MAX_RTS_THRESHOLD               2347	/* byte count */

/* new types for Media Specific Indications */
/* Extension channel offset */
#define EXTCHA_NONE			0
#define EXTCHA_ABOVE		0x1
#define EXTCHA_BELOW		0x3

/* BW */
#define BAND_WIDTH_20		0
#define BAND_WIDTH_40		1
#define BAND_WIDTH_BOTH		2
#define BAND_WIDTH_10		3	/* 802.11j has 10MHz. This definition is for internal usage. doesn't fill in the IE or other field. */
/* SHORTGI */
#define GAP_INTERVAL_400	1	/* only support in HT mode */
#define GAP_INTERVAL_800	0
#define GAP_INTERVAL_BOTH	2

#define NdisMediaStateConnected			1
#define NdisMediaStateDisconnected		0

#define NDIS_802_11_LENGTH_SSID         32
#define NDIS_802_11_LENGTH_RATES        8
#define NDIS_802_11_LENGTH_RATES_EX     16
#define MAC_ADDR_LENGTH                 6
/*#define MAX_NUM_OF_CHS                                        49 // 14 channels @2.4G +  12@UNII + 4 @MMAC + 11 @HiperLAN2 + 7 @Japan + 1 as NULL terminationc */
#define MAX_NUM_OF_CHS				54	/* 14 channels @2.4G +  12@UNII(lower/middle) + 16@HiperLAN2 + 11@UNII(upper) + 0 @Japan + 1 as NULL termination */
#define MAX_NUMBER_OF_EVENT				10	/* entry # in EVENT table */
#define MAX_NUMBER_OF_MAC				32	/* if MAX_MBSSID_NUM is 8, this value can't be larger than 211 */
#define MAX_NUMBER_OF_ACL				64
#define MAX_LENGTH_OF_SUPPORT_RATES		12	/* 1, 2, 5.5, 11, 6, 9, 12, 18, 24, 36, 48, 54 */
#define MAX_NUMBER_OF_DLS_ENTRY			4

#define RT_QUERY_SIGNAL_CONTEXT				0x0402
#define RT_SET_IAPP_PID                 	0x0404
#define RT_SET_APD_PID						0x0405
#define RT_SET_DEL_MAC_ENTRY				0x0406
#define RT_QUERY_EVENT_TABLE			0x0407
/* */
/* IEEE 802.11 OIDs */
/* */
#define	OID_GET_SET_TOGGLE			0x8000
#define	OID_GET_SET_FROM_UI			0x4000

#define OID_802_11_ADD_WEP			0x0112
#define OID_802_11_DISASSOCIATE			0x0114
#define OID_802_11_BSSID_LIST_SCAN		0x0508
#define OID_802_11_SSID				0x0509
#define OID_802_11_BSSID			0x050A
#define OID_802_11_MIC_FAILURE_REPORT_FRAME	0x0528

#define	RT_OID_DEVICE_NAME							0x0607
#define	RT_OID_VERSION_INFO							0x0608
#define	OID_802_11_BSSID_LIST						0x0609
#define	OID_802_3_CURRENT_ADDRESS					0x060A
#define	OID_GEN_MEDIA_CONNECT_STATUS				0x060B
#define	RT_OID_802_11_QUERY_LINK_STATUS				0x060C
#define	OID_802_11_RSSI								0x060D
#define	OID_802_11_STATISTICS						0x060E
#define	OID_GEN_RCV_OK								0x060F
#define	OID_GEN_RCV_NO_BUFFER						0x0610
#define	RT_OID_802_11_QUERY_EEPROM_VERSION			0x0611
#define	RT_OID_802_11_QUERY_FIRMWARE_VERSION		0x0612
#define	RT_OID_802_11_QUERY_LAST_RX_RATE			0x0613
#define	RT_OID_802_11_TX_POWER_LEVEL_1				0x0614
#define	RT_OID_802_11_QUERY_PIDVID					0x0615
/*for WPA_SUPPLICANT_SUPPORT */
#define OID_SET_COUNTERMEASURES                     0x0616
#define RT_OID_WPA_SUPPLICANT_SUPPORT               0x0621
#define RT_OID_WE_VERSION_COMPILED                  0x0622
#define RT_OID_NEW_DRIVER                           0x0623

#define RT_OID_DRIVER_DEVICE_NAME                   0x0645
#define RT_OID_QUERY_MULTIPLE_CARD_SUPPORT          0x0647

typedef enum _NDIS_802_11_STATUS_TYPE {
	Ndis802_11StatusType_Authentication,
	Ndis802_11StatusType_MediaStreamMode,
	Ndis802_11StatusType_PMKID_CandidateList,
	Ndis802_11StatusTypeMax	/* not a real type, defined as an upper bound */
} NDIS_802_11_STATUS_TYPE, *PNDIS_802_11_STATUS_TYPE;

typedef u8 NDIS_802_11_MAC_ADDRESS[6];

struct rt_ndis_802_11_status_indication {
	NDIS_802_11_STATUS_TYPE StatusType;
};

/* mask for authentication/integrity fields */
#define NDIS_802_11_AUTH_REQUEST_AUTH_FIELDS        0x0f

#define NDIS_802_11_AUTH_REQUEST_REAUTH             0x01
#define NDIS_802_11_AUTH_REQUEST_KEYUPDATE          0x02
#define NDIS_802_11_AUTH_REQUEST_PAIRWISE_ERROR     0x06
#define NDIS_802_11_AUTH_REQUEST_GROUP_ERROR        0x0E

struct rt_ndis_802_11_authentication_request {
	unsigned long Length;		/* Length of structure */
	NDIS_802_11_MAC_ADDRESS Bssid;
	unsigned long Flags;
};

/*Added new types for PMKID Candidate lists. */
struct rt_pmkid_candidate {
	NDIS_802_11_MAC_ADDRESS BSSID;
	unsigned long Flags;
};

struct rt_ndis_802_11_pmkid_candidate_list {
	unsigned long Version;		/* Version of the structure */
	unsigned long NumCandidates;	/* No. of pmkid candidates */
	struct rt_pmkid_candidate CandidateList[1];
};

/*Flags for PMKID Candidate list structure */
#define NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED	0x01

/* Added new types for OFDM 5G and 2.4G */
typedef enum _NDIS_802_11_NETWORK_TYPE {
	Ndis802_11FH,
	Ndis802_11DS,
	Ndis802_11OFDM5,
	Ndis802_11OFDM24,
	Ndis802_11Automode,
	Ndis802_11OFDM5_N,
	Ndis802_11OFDM24_N,
	Ndis802_11NetworkTypeMax	/* not a real type, defined as an upper bound */
} NDIS_802_11_NETWORK_TYPE, *PNDIS_802_11_NETWORK_TYPE;

struct rt_ndis_802_11_network_type_list {
	u32 NumberOfItems;	/* in list below, at least 1 */
	NDIS_802_11_NETWORK_TYPE NetworkType[1];
};

typedef enum _NDIS_802_11_POWER_MODE {
	Ndis802_11PowerModeCAM,
	Ndis802_11PowerModeMAX_PSP,
	Ndis802_11PowerModeFast_PSP,
	Ndis802_11PowerModeLegacy_PSP,
	Ndis802_11PowerModeMax	/* not a real mode, defined as an upper bound */
} NDIS_802_11_POWER_MODE, *PNDIS_802_11_POWER_MODE;

typedef unsigned long NDIS_802_11_TX_POWER_LEVEL;	/* in milliwatts */

/* */
/* Received Signal Strength Indication */
/* */
typedef long NDIS_802_11_RSSI;	/* in dBm */

struct rt_ndis_802_11_configuration_fh {
	unsigned long Length;		/* Length of structure */
	unsigned long HopPattern;	/* As defined by 802.11, MSB set */
	unsigned long HopSet;		/* to one if non-802.11 */
	unsigned long DwellTime;	/* units are Kusec */
};

struct rt_ndis_802_11_configuration {
	unsigned long Length;		/* Length of structure */
	unsigned long BeaconPeriod;	/* units are Kusec */
	unsigned long ATIMWindow;	/* units are Kusec */
	unsigned long DSConfig;		/* Frequency, units are kHz */
	struct rt_ndis_802_11_configuration_fh FHConfig;
};

struct rt_ndis_802_11_statistics {
	unsigned long Length;		/* Length of structure */
	LARGE_INTEGER TransmittedFragmentCount;
	LARGE_INTEGER MulticastTransmittedFrameCount;
	LARGE_INTEGER FailedCount;
	LARGE_INTEGER RetryCount;
	LARGE_INTEGER MultipleRetryCount;
	LARGE_INTEGER RTSSuccessCount;
	LARGE_INTEGER RTSFailureCount;
	LARGE_INTEGER ACKFailureCount;
	LARGE_INTEGER FrameDuplicateCount;
	LARGE_INTEGER ReceivedFragmentCount;
	LARGE_INTEGER MulticastReceivedFrameCount;
	LARGE_INTEGER FCSErrorCount;
	LARGE_INTEGER TKIPLocalMICFailures;
	LARGE_INTEGER TKIPRemoteMICErrors;
	LARGE_INTEGER TKIPICVErrors;
	LARGE_INTEGER TKIPCounterMeasuresInvoked;
	LARGE_INTEGER TKIPReplays;
	LARGE_INTEGER CCMPFormatErrors;
	LARGE_INTEGER CCMPReplays;
	LARGE_INTEGER CCMPDecryptErrors;
	LARGE_INTEGER FourWayHandshakeFailures;
};

typedef unsigned long NDIS_802_11_KEY_INDEX;
typedef unsigned long long NDIS_802_11_KEY_RSC;

#define MAX_RADIUS_SRV_NUM			2	/* 802.1x failover number */

struct PACKED rt_radius_srv_info {
	u32 radius_ip;
	u32 radius_port;
	u8 radius_key[64];
	u8 radius_key_len;
};

struct PACKED rt_radius_key_info {
	u8 radius_srv_num;
	struct rt_radius_srv_info radius_srv_info[MAX_RADIUS_SRV_NUM];
	u8 ieee8021xWEP;	/* dynamic WEP */
	u8 key_index;
	u8 key_length;	/* length of key in bytes */
	u8 key_material[13];
};

/* It's used by 802.1x daemon to require relative configuration */
struct PACKED rt_radius_conf {
	u32 Length;		/* Length of this structure */
	u8 mbss_num;		/* indicate multiple BSS number */
	u32 own_ip_addr;
	u32 retry_interval;
	u32 session_timeout_interval;
	u8 EAPifname[8][IFNAMSIZ];
	u8 EAPifname_len[8];
	u8 PreAuthifname[8][IFNAMSIZ];
	u8 PreAuthifname_len[8];
	struct rt_radius_key_info RadiusInfo[8];
};

/* Key mapping keys require a BSSID */
struct rt_ndis_802_11_key {
	u32 Length;		/* Length of this structure */
	u32 KeyIndex;
	u32 KeyLength;		/* length of key in bytes */
	NDIS_802_11_MAC_ADDRESS BSSID;
	NDIS_802_11_KEY_RSC KeyRSC;
	u8 KeyMaterial[1];	/* variable length depending on above field */
};

struct rt_ndis_802_11_passphrase {
	u32 KeyLength;		/* length of key in bytes */
	NDIS_802_11_MAC_ADDRESS BSSID;
	u8 KeyMaterial[1];	/* variable length depending on above field */
};

struct rt_ndis_802_11_remove_key {
	u32 Length;		/* Length of this structure */
	u32 KeyIndex;
	NDIS_802_11_MAC_ADDRESS BSSID;
};

struct rt_ndis_802_11_wep {
	u32 Length;		/* Length of this structure */
	u32 KeyIndex;		/* 0 is the per-client key, 1-N are the */
	/* global keys */
	u32 KeyLength;		/* length of key in bytes */
	u8 KeyMaterial[1];	/* variable length depending on above field */
};

typedef enum _NDIS_802_11_NETWORK_INFRASTRUCTURE {
	Ndis802_11IBSS,
	Ndis802_11Infrastructure,
	Ndis802_11AutoUnknown,
	Ndis802_11Monitor,
	Ndis802_11InfrastructureMax	/* Not a real value, defined as upper bound */
} NDIS_802_11_NETWORK_INFRASTRUCTURE, *PNDIS_802_11_NETWORK_INFRASTRUCTURE;

/* Add new authentication modes */
typedef enum _NDIS_802_11_AUTHENTICATION_MODE {
	Ndis802_11AuthModeOpen,
	Ndis802_11AuthModeShared,
	Ndis802_11AuthModeAutoSwitch,
	Ndis802_11AuthModeWPA,
	Ndis802_11AuthModeWPAPSK,
	Ndis802_11AuthModeWPANone,
	Ndis802_11AuthModeWPA2,
	Ndis802_11AuthModeWPA2PSK,
	Ndis802_11AuthModeWPA1WPA2,
	Ndis802_11AuthModeWPA1PSKWPA2PSK,
	Ndis802_11AuthModeMax	/* Not a real mode, defined as upper bound */
} NDIS_802_11_AUTHENTICATION_MODE, *PNDIS_802_11_AUTHENTICATION_MODE;

typedef u8 NDIS_802_11_RATES[NDIS_802_11_LENGTH_RATES];	/* Set of 8 data rates */
typedef u8 NDIS_802_11_RATES_EX[NDIS_802_11_LENGTH_RATES_EX];	/* Set of 16 data rates */

struct PACKED rt_ndis_802_11_ssid {
	u32 SsidLength;	/* length of SSID field below, in bytes; */
	/* this can be zero. */
	u8 Ssid[NDIS_802_11_LENGTH_SSID];	/* SSID information field */
};

struct PACKED rt_ndis_wlan_bssid {
	unsigned long Length;		/* Length of this structure */
	NDIS_802_11_MAC_ADDRESS MacAddress;	/* BSSID */
	u8 Reserved[2];
	struct rt_ndis_802_11_ssid Ssid;	/* SSID */
	unsigned long Privacy;		/* WEP encryption requirement */
	NDIS_802_11_RSSI Rssi;	/* receive signal strength in dBm */
	NDIS_802_11_NETWORK_TYPE NetworkTypeInUse;
	struct rt_ndis_802_11_configuration Configuration;
	NDIS_802_11_NETWORK_INFRASTRUCTURE InfrastructureMode;
	NDIS_802_11_RATES SupportedRates;
};

struct PACKED rt_ndis_802_11_bssid_list {
	u32 NumberOfItems;	/* in list below, at least 1 */
	struct rt_ndis_wlan_bssid Bssid[1];
};

/* Added Capabilities, IELength and IEs for each BSSID */
struct PACKED rt_ndis_wlan_bssid_ex {
	unsigned long Length;		/* Length of this structure */
	NDIS_802_11_MAC_ADDRESS MacAddress;	/* BSSID */
	u8 Reserved[2];
	struct rt_ndis_802_11_ssid Ssid;	/* SSID */
	u32 Privacy;		/* WEP encryption requirement */
	NDIS_802_11_RSSI Rssi;	/* receive signal */
	/* strength in dBm */
	NDIS_802_11_NETWORK_TYPE NetworkTypeInUse;
	struct rt_ndis_802_11_configuration Configuration;
	NDIS_802_11_NETWORK_INFRASTRUCTURE InfrastructureMode;
	NDIS_802_11_RATES_EX SupportedRates;
	unsigned long IELength;
	u8 IEs[1];
};

struct PACKED rt_ndis_802_11_bssid_list_ex {
	u32 NumberOfItems;	/* in list below, at least 1 */
	struct rt_ndis_wlan_bssid_ex Bssid[1];
};

struct PACKED rt_ndis_802_11_fixed_ies {
	u8 Timestamp[8];
	u16 BeaconInterval;
	u16 Capabilities;
};

struct rt_ndis_802_11_variable_ies {
	u8 ElementID;
	u8 Length;		/* Number of bytes in data field */
	u8 data[1];
};

typedef unsigned long NDIS_802_11_FRAGMENTATION_THRESHOLD;

typedef unsigned long NDIS_802_11_RTS_THRESHOLD;

typedef unsigned long NDIS_802_11_ANTENNA;

typedef enum _NDIS_802_11_PRIVACY_FILTER {
	Ndis802_11PrivFilterAcceptAll,
	Ndis802_11PrivFilter8021xWEP
} NDIS_802_11_PRIVACY_FILTER, *PNDIS_802_11_PRIVACY_FILTER;

/* Added new encryption types */
/* Also aliased typedef to new name */
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
	Ndis802_11Encryption4Enabled,	/* TKIP or AES mix */
	Ndis802_11Encryption4KeyAbsent,
	Ndis802_11GroupWEP40Enabled,
	Ndis802_11GroupWEP104Enabled,
} NDIS_802_11_WEP_STATUS, *PNDIS_802_11_WEP_STATUS,
    NDIS_802_11_ENCRYPTION_STATUS, *PNDIS_802_11_ENCRYPTION_STATUS;

typedef enum _NDIS_802_11_RELOAD_DEFAULTS {
	Ndis802_11ReloadWEPKeys
} NDIS_802_11_RELOAD_DEFAULTS, *PNDIS_802_11_RELOAD_DEFAULTS;

#define NDIS_802_11_AI_REQFI_CAPABILITIES      1
#define NDIS_802_11_AI_REQFI_LISTENINTERVAL    2
#define NDIS_802_11_AI_REQFI_CURRENTAPADDRESS  4

#define NDIS_802_11_AI_RESFI_CAPABILITIES      1
#define NDIS_802_11_AI_RESFI_STATUSCODE        2
#define NDIS_802_11_AI_RESFI_ASSOCIATIONID     4

struct rt_ndis_802_11_ai_reqfi {
	u16 Capabilities;
	u16 ListenInterval;
	NDIS_802_11_MAC_ADDRESS CurrentAPAddress;
};

struct rt_ndis_802_11_ai_resfi {
	u16 Capabilities;
	u16 StatusCode;
	u16 AssociationId;
};

struct rt_ndis_802_11_association_information {
	unsigned long Length;
	u16 AvailableRequestFixedIEs;
	struct rt_ndis_802_11_ai_reqfi RequestFixedIEs;
	unsigned long RequestIELength;
	unsigned long OffsetRequestIEs;
	u16 AvailableResponseFixedIEs;
	struct rt_ndis_802_11_ai_resfi ResponseFixedIEs;
	unsigned long ResponseIELength;
	unsigned long OffsetResponseIEs;
};

struct rt_ndis_802_11_authentication_event {
	struct rt_ndis_802_11_status_indication Status;
	struct rt_ndis_802_11_authentication_request Request[1];
};

/* 802.11 Media stream constraints, associated with OID_802_11_MEDIA_STREAM_MODE */
typedef enum _NDIS_802_11_MEDIA_STREAM_MODE {
	Ndis802_11MediaStreamOff,
	Ndis802_11MediaStreamOn,
} NDIS_802_11_MEDIA_STREAM_MODE, *PNDIS_802_11_MEDIA_STREAM_MODE;

/* PMKID Structures */
typedef u8 NDIS_802_11_PMKID_VALUE[16];

struct rt_bssid_info {
	NDIS_802_11_MAC_ADDRESS BSSID;
	NDIS_802_11_PMKID_VALUE PMKID;
};

struct rt_ndis_802_11_pmkid {
	u32 Length;
	u32 BSSIDInfoCount;
	struct rt_bssid_info BSSIDInfo[1];
};

struct rt_ndis_802_11_authentication_encryption {
	NDIS_802_11_AUTHENTICATION_MODE AuthModeSupported;
	NDIS_802_11_ENCRYPTION_STATUS EncryptStatusSupported;
};

struct rt_ndis_802_11_capability {
	unsigned long Length;
	unsigned long Version;
	unsigned long NoOfPMKIDs;
	unsigned long NoOfAuthEncryptPairsSupported;
	struct rt_ndis_802_11_authentication_encryption
	    AuthenticationEncryptionSupported[1];
};

#define RT_PRIV_IOCTL							(SIOCIWFIRSTPRIV + 0x01)	/* Sync. with AP for wsc upnp daemon */
#define RTPRIV_IOCTL_SET							(SIOCIWFIRSTPRIV + 0x02)

#define RTPRIV_IOCTL_STATISTICS                     (SIOCIWFIRSTPRIV + 0x09)
#define RTPRIV_IOCTL_ADD_PMKID_CACHE                (SIOCIWFIRSTPRIV + 0x0A)
#define RTPRIV_IOCTL_RADIUS_DATA                    (SIOCIWFIRSTPRIV + 0x0C)
#define RTPRIV_IOCTL_GSITESURVEY					(SIOCIWFIRSTPRIV + 0x0D)
#define RT_PRIV_IOCTL_EXT							(SIOCIWFIRSTPRIV + 0x0E)	/* Sync. with RT61 (for wpa_supplicant) */
#define RTPRIV_IOCTL_GET_MAC_TABLE					(SIOCIWFIRSTPRIV + 0x0F)

#define RTPRIV_IOCTL_SHOW							(SIOCIWFIRSTPRIV + 0x11)
enum {
	SHOW_CONN_STATUS = 4,
	SHOW_DRVIER_VERION = 5,
	SHOW_BA_INFO = 6,
	SHOW_DESC_INFO = 7,
#ifdef RTMP_MAC_USB
	SHOW_RXBULK_INFO = 8,
	SHOW_TXBULK_INFO = 9,
#endif /* RTMP_MAC_USB // */
	RAIO_OFF = 10,
	RAIO_ON = 11,
	SHOW_CFG_VALUE = 20,
	SHOW_ADHOC_ENTRY_INFO = 21,
};

#define OID_802_11_BUILD_CHANNEL_EX				0x0714
#define OID_802_11_GET_CH_LIST					0x0715
#define OID_802_11_GET_COUNTRY_CODE				0x0716
#define OID_802_11_GET_CHANNEL_GEOGRAPHY		0x0717

#define RT_OID_WSC_SET_PASSPHRASE                   0x0740	/* passphrase for wpa(2)-psk */
#define RT_OID_WSC_DRIVER_AUTO_CONNECT              0x0741
#define RT_OID_WSC_QUERY_DEFAULT_PROFILE            0x0742
#define RT_OID_WSC_SET_CONN_BY_PROFILE_INDEX        0x0743
#define RT_OID_WSC_SET_ACTION                       0x0744
#define RT_OID_WSC_SET_SSID                         0x0745
#define RT_OID_WSC_SET_PIN_CODE                     0x0746
#define RT_OID_WSC_SET_MODE                         0x0747	/* PIN or PBC */
#define RT_OID_WSC_SET_CONF_MODE                    0x0748	/* Enrollee or Registrar */
#define RT_OID_WSC_SET_PROFILE                      0x0749
#define	RT_OID_WSC_CONFIG_STATUS					0x074F
#define RT_OID_802_11_WSC_QUERY_PROFILE				0x0750
/* for consistency with RT61 */
#define RT_OID_WSC_QUERY_STATUS						0x0751
#define RT_OID_WSC_PIN_CODE							0x0752
#define RT_OID_WSC_UUID								0x0753
#define RT_OID_WSC_SET_SELECTED_REGISTRAR			0x0754
#define RT_OID_WSC_EAPMSG							0x0755
#define RT_OID_WSC_MANUFACTURER						0x0756
#define RT_OID_WSC_MODEL_NAME						0x0757
#define RT_OID_WSC_MODEL_NO							0x0758
#define RT_OID_WSC_SERIAL_NO						0x0759
#define RT_OID_WSC_MAC_ADDRESS						0x0760

/* New for MeetingHouse Api support */
#define OID_MH_802_1X_SUPPORTED               0xFFEDC100

/* MIMO Tx parameter, ShortGI, MCS, STBC, etc.  these are fields in TXWI. Don't change this definition! */
typedef union _HTTRANSMIT_SETTING {
	struct {
		u16 MCS:7;	/* MCS */
		u16 BW:1;	/*channel bandwidth 20MHz or 40 MHz */
		u16 ShortGI:1;
		u16 STBC:2;	/*SPACE */
/*      u16          rsv:3; */
		u16 rsv:2;
		u16 TxBF:1;
		u16 MODE:2;	/* Use definition MODE_xxx. */
	} field;
	u16 word;
} HTTRANSMIT_SETTING, *PHTTRANSMIT_SETTING;

typedef enum _RT_802_11_PREAMBLE {
	Rt802_11PreambleLong,
	Rt802_11PreambleShort,
	Rt802_11PreambleAuto
} RT_802_11_PREAMBLE, *PRT_802_11_PREAMBLE;

typedef enum _RT_802_11_PHY_MODE {
	PHY_11BG_MIXED = 0,
	PHY_11B,
	PHY_11A,
	PHY_11ABG_MIXED,
	PHY_11G,
	PHY_11ABGN_MIXED,	/* both band   5 */
	PHY_11N_2_4G,		/* 11n-only with 2.4G band      6 */
	PHY_11GN_MIXED,		/* 2.4G band      7 */
	PHY_11AN_MIXED,		/* 5G  band       8 */
	PHY_11BGN_MIXED,	/* if check 802.11b.      9 */
	PHY_11AGN_MIXED,	/* if check 802.11b.      10 */
	PHY_11N_5G,		/* 11n-only with 5G band                11 */
} RT_802_11_PHY_MODE;

/* put all proprietery for-query objects here to reduce # of Query_OID */
struct rt_802_11_link_status {
	unsigned long CurrTxRate;	/* in units of 0.5Mbps */
	unsigned long ChannelQuality;	/* 0..100 % */
	unsigned long TxByteCount;	/* both ok and fail */
	unsigned long RxByteCount;	/* both ok and fail */
	unsigned long CentralChannel;	/* 40MHz central channel number */
};

struct rt_802_11_event_log {
	LARGE_INTEGER SystemTime;	/* timestammp via NdisGetCurrentSystemTime() */
	u8 Addr[MAC_ADDR_LENGTH];
	u16 Event;		/* EVENT_xxx */
};

struct rt_802_11_event_table {
	unsigned long Num;
	unsigned long Rsv;		/* to align Log[] at LARGE_INEGER boundary */
	struct rt_802_11_event_log Log[MAX_NUMBER_OF_EVENT];
};

/* MIMO Tx parameter, ShortGI, MCS, STBC, etc.  these are fields in TXWI. Don't change this definition! */
typedef union _MACHTTRANSMIT_SETTING {
	struct {
		u16 MCS:7;	/* MCS */
		u16 BW:1;	/*channel bandwidth 20MHz or 40 MHz */
		u16 ShortGI:1;
		u16 STBC:2;	/*SPACE */
		u16 rsv:3;
		u16 MODE:2;	/* Use definition MODE_xxx. */
	} field;
	u16 word;
} MACHTTRANSMIT_SETTING, *PMACHTTRANSMIT_SETTING;

struct rt_802_11_mac_entry {
	u8 Addr[MAC_ADDR_LENGTH];
	u8 Aid;
	u8 Psm;		/* 0:PWR_ACTIVE, 1:PWR_SAVE */
	u8 MimoPs;		/* 0:MMPS_STATIC, 1:MMPS_DYNAMIC, 3:MMPS_Enabled */
	char AvgRssi0;
	char AvgRssi1;
	char AvgRssi2;
	u32 ConnectedTime;
	MACHTTRANSMIT_SETTING TxRate;
};

struct rt_802_11_mac_table {
	unsigned long Num;
	struct rt_802_11_mac_entry Entry[MAX_NUMBER_OF_MAC];
};

/* structure for query/set hardware register - MAC, BBP, RF register */
struct rt_802_11_hardware_register {
	unsigned long HardwareType;	/* 0:MAC, 1:BBP, 2:RF register, 3:EEPROM */
	unsigned long Offset;		/* Q/S register offset addr */
	unsigned long Data;		/* R/W data buffer */
};

struct rt_802_11_ap_config {
	unsigned long EnableTxBurst;	/* 0-disable, 1-enable */
	unsigned long EnableTurboRate;	/* 0-disable, 1-enable 72/100mbps turbo rate */
	unsigned long IsolateInterStaTraffic;	/* 0-disable, 1-enable isolation */
	unsigned long HideSsid;		/* 0-disable, 1-enable hiding */
	unsigned long UseBGProtection;	/* 0-AUTO, 1-always ON, 2-always OFF */
	unsigned long UseShortSlotTime;	/* 0-no use, 1-use 9-us short slot time */
	unsigned long Rsv1;		/* must be 0 */
	unsigned long SystemErrorBitmap;	/* ignore upon SET, return system error upon QUERY */
};

/* structure to query/set STA_CONFIG */
struct rt_802_11_sta_config {
	unsigned long EnableTxBurst;	/* 0-disable, 1-enable */
	unsigned long EnableTurboRate;	/* 0-disable, 1-enable 72/100mbps turbo rate */
	unsigned long UseBGProtection;	/* 0-AUTO, 1-always ON, 2-always OFF */
	unsigned long UseShortSlotTime;	/* 0-no use, 1-use 9-us short slot time when applicable */
	unsigned long AdhocMode;	/* 0-11b rates only (WIFI spec), 1 - b/g mixed, 2 - g only */
	unsigned long HwRadioStatus;	/* 0-OFF, 1-ON, default is 1, Read-Only */
	unsigned long Rsv1;		/* must be 0 */
	unsigned long SystemErrorBitmap;	/* ignore upon SET, return system error upon QUERY */
};

/* */
/*  For OID Query or Set about BA structure */
/* */
struct rt_oid_bacap {
	u8 RxBAWinLimit;
	u8 TxBAWinLimit;
	u8 Policy;		/* 0: DELAY_BA 1:IMMED_BA  (//BA Policy subfiled value in ADDBA frame)   2:BA-not use. other value invalid */
	u8 MpduDensity;	/* 0: DELAY_BA 1:IMMED_BA  (//BA Policy subfiled value in ADDBA frame)   2:BA-not use. other value invalid */
	u8 AmsduEnable;	/*Enable AMSDU transmisstion */
	u8 AmsduSize;	/* 0:3839, 1:7935 bytes. u32  MSDUSizeToBytes[]        = { 3839, 7935}; */
	u8 MMPSmode;		/* MIMO power save more, 0:static, 1:dynamic, 2:rsv, 3:mimo enable */
	BOOLEAN AutoBA;		/* Auto BA will automatically */
};

struct rt_802_11_acl_entry {
	u8 Addr[MAC_ADDR_LENGTH];
	u16 Rsv;
};

struct PACKED rt_rt_802_11_acl {
	unsigned long Policy;		/* 0-disable, 1-positive list, 2-negative list */
	unsigned long Num;
	struct rt_802_11_acl_entry Entry[MAX_NUMBER_OF_ACL];
};

struct rt_802_11_wds {
	unsigned long Num;
	NDIS_802_11_MAC_ADDRESS Entry[24 /*MAX_NUM_OF_WDS_LINK */];
	unsigned long KeyLength;
	u8 KeyMaterial[32];
};

struct rt_802_11_tx_rates {
	u8 SupRateLen;
	u8 SupRate[MAX_LENGTH_OF_SUPPORT_RATES];
	u8 ExtRateLen;
	u8 ExtRate[MAX_LENGTH_OF_SUPPORT_RATES];
};

/* Definition of extra information code */
#define	GENERAL_LINK_UP			0x0	/* Link is Up */
#define	GENERAL_LINK_DOWN		0x1	/* Link is Down */
#define	HW_RADIO_OFF			0x2	/* Hardware radio off */
#define	SW_RADIO_OFF			0x3	/* Software radio off */
#define	AUTH_FAIL				0x4	/* Open authentication fail */
#define	AUTH_FAIL_KEYS			0x5	/* Shared authentication fail */
#define	ASSOC_FAIL				0x6	/* Association failed */
#define	EAP_MIC_FAILURE			0x7	/* Deauthencation because MIC failure */
#define	EAP_4WAY_TIMEOUT		0x8	/* Deauthencation on 4-way handshake timeout */
#define	EAP_GROUP_KEY_TIMEOUT	0x9	/* Deauthencation on group key handshake timeout */
#define	EAP_SUCCESS				0xa	/* EAP succeed */
#define	DETECT_RADAR_SIGNAL		0xb	/* Radar signal occur in current channel */
#define EXTRA_INFO_MAX			0xb	/* Indicate Last OID */

#define EXTRA_INFO_CLEAR		0xffffffff

/* This is OID setting structure. So only GF or MM as Mode. This is valid when our wirelss mode has 802.11n in use. */
struct rt_oid_set_ht_phymode {
	RT_802_11_PHY_MODE PhyMode;	/* */
	u8 TransmitNo;
	u8 HtMode;		/*HTMODE_GF or HTMODE_MM */
	u8 ExtOffset;	/*extension channel above or below */
	u8 MCS;
	u8 BW;
	u8 STBC;
	u8 SHORTGI;
	u8 rsv;
};

#define MAX_CUSTOM_LEN 128

typedef enum _RT_802_11_D_CLIENT_MODE {
	Rt802_11_D_None,
	Rt802_11_D_Flexible,
	Rt802_11_D_Strict,
} RT_802_11_D_CLIENT_MODE, *PRT_802_11_D_CLIENT_MODE;

struct rt_channel_list_info {
	u8 ChannelList[MAX_NUM_OF_CHS];	/* list all supported channels for site survey */
	u8 ChannelListNum;	/* number of channel in ChannelList[] */
};

/* WSC configured credential */
struct rt_wsc_credential {
	struct rt_ndis_802_11_ssid SSID;	/* mandatory */
	u16 AuthType;	/* mandatory, 1: open, 2: wpa-psk, 4: shared, 8:wpa, 0x10: wpa2, 0x20: wpa2-psk */
	u16 EncrType;	/* mandatory, 1: none, 2: wep, 4: tkip, 8: aes */
	u8 Key[64];		/* mandatory, Maximum 64 byte */
	u16 KeyLength;
	u8 MacAddr[6];	/* mandatory, AP MAC address */
	u8 KeyIndex;		/* optional, default is 1 */
	u8 Rsvd[3];		/* Make alignment */
};

/* WSC configured profiles */
struct rt_wsc_profile {
	u32 ProfileCnt;
	u32 ApplyProfileIdx;	/* add by johnli, fix WPS test plan 5.1.1 */
	struct rt_wsc_credential Profile[8];	/* Support up to 8 profiles */
};

#endif /* _OID_H_ */
