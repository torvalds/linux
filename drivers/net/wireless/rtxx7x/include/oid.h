/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#ifndef _OID_H_
#define _OID_H_

/*#include <linux/wireless.h> */



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

#define NdisApMediaStateConnected			1
#define NdisApMediaStateDisconnected		0


#define NDIS_802_11_LENGTH_SSID         32

#define	IEEE80211_ADDR_LEN		6	/* size of 802.11 address */
#define	IEEE80211_NWID_LEN      32

#define NDIS_802_11_LENGTH_RATES        8
#define NDIS_802_11_LENGTH_RATES_EX     16
#define MAC_ADDR_LENGTH                 6
/*#define MAX_NUM_OF_CHS					49 // 14 channels @2.4G +  12@UNII + 4 @MMAC + 11 @HiperLAN2 + 7 @Japan + 1 as NULL terminationc */
#define MAX_NUM_OF_CHS             		54	/* 14 channels @2.4G +  12@UNII(lower/middle) + 16@HiperLAN2 + 11@UNII(upper) + 0 @Japan + 1 as NULL termination */
#define MAX_NUMBER_OF_EVENT				10	/* entry # in EVENT table */
#define MAX_NUMBER_OF_MAC				32	/* if MAX_MBSSID_NUM is 8, this value can't be larger than 211 */
#define MAX_NUMBER_OF_ACL				64
#define MAX_LENGTH_OF_SUPPORT_RATES		12	/* 1, 2, 5.5, 11, 6, 9, 12, 18, 24, 36, 48, 54 */
#define MAX_NUMBER_OF_DLS_ENTRY			4


#define RT_QUERY_SIGNAL_CONTEXT				0x0402
#define RT_SET_IAPP_PID                 	0x0404
#define RT_SET_APD_PID						0x0405
#define RT_SET_DEL_MAC_ENTRY				0x0406
#define RT_QUERY_EVENT_TABLE            	0x0407
/* */
/* IEEE 802.11 OIDs */
/* */
#define	OID_GET_SET_TOGGLE			0x8000
#define	OID_GET_SET_FROM_UI			0x4000

#define	OID_802_11_NETWORK_TYPES_SUPPORTED			0x0103
#define	OID_802_11_NETWORK_TYPE_IN_USE				0x0104
#define	OID_802_11_RSSI_TRIGGER						0x0107
#define	RT_OID_802_11_RSSI							0x0108	/*rt2860 only , kathy */
#define	RT_OID_802_11_RSSI_1						0x0109	/*rt2860 only , kathy */
#define	RT_OID_802_11_RSSI_2						0x010A	/*rt2860 only , kathy */
#define	OID_802_11_NUMBER_OF_ANTENNAS				0x010B
#define	OID_802_11_RX_ANTENNA_SELECTED				0x010C
#define	OID_802_11_TX_ANTENNA_SELECTED				0x010D
#define	OID_802_11_SUPPORTED_RATES					0x010E
#define	OID_802_11_ADD_WEP							0x0112
#define	OID_802_11_REMOVE_WEP						0x0113
#define	OID_802_11_DISASSOCIATE						0x0114
#define	OID_802_11_PRIVACY_FILTER					0x0118
#define	OID_802_11_ASSOCIATION_INFORMATION			0x011E
#define	OID_802_11_TEST								0x011F


#define	RT_OID_802_11_COUNTRY_REGION				0x0507
#define	OID_802_11_BSSID_LIST_SCAN					0x0508
#define	OID_802_11_SSID								0x0509
#define	OID_802_11_BSSID							0x050A
#define	RT_OID_802_11_RADIO							0x050B
#define	RT_OID_802_11_PHY_MODE						0x050C
#define	RT_OID_802_11_STA_CONFIG					0x050D
#define	OID_802_11_DESIRED_RATES					0x050E
#define	RT_OID_802_11_PREAMBLE						0x050F
#define	OID_802_11_WEP_STATUS						0x0510
#define	OID_802_11_AUTHENTICATION_MODE				0x0511
#define	OID_802_11_INFRASTRUCTURE_MODE				0x0512
#define	RT_OID_802_11_RESET_COUNTERS				0x0513
#define	OID_802_11_RTS_THRESHOLD					0x0514
#define	OID_802_11_FRAGMENTATION_THRESHOLD			0x0515
#define	OID_802_11_POWER_MODE						0x0516
#define	OID_802_11_TX_POWER_LEVEL					0x0517
#define	RT_OID_802_11_ADD_WPA						0x0518
#define	OID_802_11_REMOVE_KEY						0x0519
#define	OID_802_11_ADD_KEY							0x0520
#define	OID_802_11_CONFIGURATION					0x0521
#define	OID_802_11_TX_PACKET_BURST					0x0522
#define	RT_OID_802_11_QUERY_NOISE_LEVEL				0x0523
#define	RT_OID_802_11_EXTRA_INFO					0x0524
#define	RT_OID_802_11_HARDWARE_REGISTER				0x0525
#define OID_802_11_ENCRYPTION_STATUS            OID_802_11_WEP_STATUS
#define OID_802_11_DEAUTHENTICATION                 0x0526
#define OID_802_11_DROP_UNENCRYPTED                 0x0527
#define OID_802_11_MIC_FAILURE_REPORT_FRAME         0x0528
#define OID_802_11_EAP_METHOD						0x0529

/* For 802.1x daemin using */
#ifdef DOT1X_SUPPORT
#define OID_802_DOT1X_CONFIGURATION					0x0540
#define OID_802_DOT1X_PMKID_CACHE					0x0541
#define OID_802_DOT1X_RADIUS_DATA					0x0542
#define OID_802_DOT1X_WPA_KEY						0x0543
#define OID_802_DOT1X_STATIC_WEP_COPY				0x0544
#define OID_802_DOT1X_IDLE_TIMEOUT					0x0545
#endif /* DOT1X_SUPPORT */

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
#define OID_802_11_SET_IEEE8021X                    0x0617
#define OID_802_11_SET_IEEE8021X_REQUIRE_KEY        0x0618
#define OID_802_11_PMKID                            0x0620
#define RT_OID_WPA_SUPPLICANT_SUPPORT               0x0621
#define RT_OID_WE_VERSION_COMPILED                  0x0622
#define RT_OID_NEW_DRIVER                           0x0623
#define	OID_AUTO_PROVISION_BSSID_LIST				0x0624
#define RT_OID_WPS_PROBE_REQ_IE						0x0625

#define	RT_OID_802_11_SNR_0							0x0630
#define	RT_OID_802_11_SNR_1							0x0631
#define	RT_OID_802_11_QUERY_LAST_TX_RATE			0x0632
#define	RT_OID_802_11_QUERY_HT_PHYMODE				0x0633
#define	RT_OID_802_11_SET_HT_PHYMODE				0x0634
#define	OID_802_11_RELOAD_DEFAULTS					0x0635
#define	RT_OID_802_11_QUERY_APSD_SETTING			0x0636
#define	RT_OID_802_11_SET_APSD_SETTING				0x0637
#define	RT_OID_802_11_QUERY_APSD_PSM				0x0638
#define	RT_OID_802_11_SET_APSD_PSM					0x0639
#define	RT_OID_802_11_QUERY_DLS						0x063A
#define	RT_OID_802_11_SET_DLS						0x063B
#define	RT_OID_802_11_QUERY_DLS_PARAM				0x063C
#define	RT_OID_802_11_SET_DLS_PARAM					0x063D
#define RT_OID_802_11_QUERY_WMM              		0x063E
#define RT_OID_802_11_SET_WMM      					0x063F
#define RT_OID_802_11_QUERY_IMME_BA_CAP				0x0640
#define RT_OID_802_11_SET_IMME_BA_CAP				0x0641
#define RT_OID_802_11_QUERY_BATABLE					0x0642
#define RT_OID_802_11_ADD_IMME_BA					0x0643
#define RT_OID_802_11_TEAR_IMME_BA					0x0644
#define RT_OID_DRIVER_DEVICE_NAME                   0x0645
#define RT_OID_802_11_QUERY_DAT_HT_PHYMODE          0x0646
#define RT_OID_QUERY_MULTIPLE_CARD_SUPPORT          0x0647
#define OID_802_11_SET_PSPXLINK_MODE				0x0648
/*+++ add by woody +++*/
#define OID_802_11_SET_PASSPHRASE				0x0649
#define RT_OID_802_11_QUERY_TX_PHYMODE                          0x0650
#define RT_OID_802_11_QUERY_MAP_REAL_TX_RATE                          0x0678
#define RT_OID_802_11_QUERY_MAP_REAL_RX_RATE                          0x0679
#define	RT_OID_802_11_SNR_2							0x067A


#ifdef HOSTAPD_SUPPORT
#define SIOCSIWGENIE	0x8B30
#define OID_HOSTAPD_SUPPORT               0x0661

#define HOSTAPD_OID_STATIC_WEP_COPY   0x0662
#define HOSTAPD_OID_GET_1X_GROUP_KEY   0x0663

#define HOSTAPD_OID_SET_STA_AUTHORIZED   0x0664
#define HOSTAPD_OID_SET_STA_DISASSOC   0x0665
#define HOSTAPD_OID_SET_STA_DEAUTH   0x0666
#define HOSTAPD_OID_DEL_KEY   0x0667
#define HOSTAPD_OID_SET_KEY   0x0668
#define HOSTAPD_OID_SET_802_1X   0x0669
#define HOSTAPD_OID_GET_SEQ   0x0670
#define HOSTAPD_OID_GETWPAIE                 0x0671
#define HOSTAPD_OID_COUNTERMEASURES 0x0672
#define HOSTAPD_OID_SET_WPAPSK 0x0673
#define HOSTAPD_OID_SET_WPS_BEACON_IE 0x0674
#define HOSTAPD_OID_SET_WPS_PROBE_RESP_IE 0x0675

#define	RT_HOSTAPD_OID_HOSTAPD_SUPPORT				(OID_GET_SET_TOGGLE |	OID_HOSTAPD_SUPPORT)
#define	RT_HOSTAPD_OID_STATIC_WEP_COPY				(OID_GET_SET_TOGGLE |	HOSTAPD_OID_STATIC_WEP_COPY)
#define	RT_HOSTAPD_OID_GET_1X_GROUP_KEY				(OID_GET_SET_TOGGLE |	HOSTAPD_OID_GET_1X_GROUP_KEY)
#define	RT_HOSTAPD_OID_SET_STA_AUTHORIZED			(OID_GET_SET_TOGGLE |	HOSTAPD_OID_SET_STA_AUTHORIZED)
#define	RT_HOSTAPD_OID_SET_STA_DISASSOC				(OID_GET_SET_TOGGLE |	HOSTAPD_OID_SET_STA_DISASSOC)
#define	RT_HOSTAPD_OID_SET_STA_DEAUTH				(OID_GET_SET_TOGGLE |	HOSTAPD_OID_SET_STA_DEAUTH)
#define	RT_HOSTAPD_OID_DEL_KEY						(OID_GET_SET_TOGGLE |	HOSTAPD_OID_DEL_KEY)
#define	RT_HOSTAPD_OID_SET_KEY						(OID_GET_SET_TOGGLE |	HOSTAPD_OID_SET_KEY)
#define	RT_HOSTAPD_OID_SET_802_1X						(OID_GET_SET_TOGGLE |	HOSTAPD_OID_SET_802_1X)
#define	RT_HOSTAPD_OID_COUNTERMEASURES				(OID_GET_SET_TOGGLE |	HOSTAPD_OID_COUNTERMEASURES)
#define	RT_HOSTAPD_OID_SET_WPAPSK				(OID_GET_SET_TOGGLE |	HOSTAPD_OID_SET_WPAPSK)
#define	RT_HOSTAPD_OID_SET_WPS_BEACON_IE				(OID_GET_SET_TOGGLE |	HOSTAPD_OID_SET_WPS_BEACON_IE)
#define	RT_HOSTAPD_OID_SET_WPS_PROBE_RESP_IE				(OID_GET_SET_TOGGLE |	HOSTAPD_OID_SET_WPS_PROBE_RESP_IE)

#define IEEE80211_IS_MULTICAST(_a) (*(_a) & 0x01)
#define	IEEE80211_KEYBUF_SIZE	16
#define	IEEE80211_MICBUF_SIZE	(8 + 8)	/* space for both tx+rx keys */
#define IEEE80211_TID_SIZE		17	/* total number of TIDs */

#define	IEEE80211_MLME_ASSOC		    1	/* associate station */
#define	IEEE80211_MLME_DISASSOC		    2	/* disassociate station */
#define	IEEE80211_MLME_DEAUTH		    3	/* deauthenticate station */
#define	IEEE80211_MLME_AUTHORIZE	    4	/* authorize station */
#define	IEEE80211_MLME_UNAUTHORIZE	    5	/* unauthorize station */
#define IEEE80211_MLME_CLEAR_STATS	    6	/* clear station statistic */
#define IEEE80211_1X_COPY_KEY        	7	/* copy static-wep unicast key */

#define	IEEE80211_MAX_OPT_IE	256
#define IWEVEXPIRED	0x8C04

struct ieee80211req_mlme {
	UINT8 im_op;		/* operation to perform */
	UINT8 im_ssid_len;	/* length of optional ssid */
	UINT16 im_reason;	/* 802.11 reason code */
	UINT8 im_macaddr[IEEE80211_ADDR_LEN];
	UINT8 im_ssid[IEEE80211_NWID_LEN];
};

struct ieee80211req_key {
	UINT8 ik_type;		/* key/cipher type */
	UINT8 ik_pad;
	UINT16 ik_keyix;	/* key index */
	UINT8 ik_keylen;	/* key length in bytes */
	UINT8 ik_flags;
	UINT8 ik_macaddr[IEEE80211_ADDR_LEN];
	UINT64 ik_keyrsc;	/* key receive sequence counter */
	UINT64 ik_keytsc;	/* key transmit sequence counter */
	UINT8 ik_keydata[IEEE80211_KEYBUF_SIZE + IEEE80211_MICBUF_SIZE];
	int txkey;
};

struct ieee80211req_del_key {
	UINT8 idk_keyix;	/* key index */
	UINT8 idk_macaddr[IEEE80211_ADDR_LEN];
};

struct default_group_key {
	UINT16 ik_keyix;	/* key index */
	UINT8 ik_keylen;	/* key length in bytes */
	UINT8 ik_keydata[IEEE80211_KEYBUF_SIZE + IEEE80211_MICBUF_SIZE];
};

struct ieee80211req_wpaie {
	UINT8 wpa_macaddr[IEEE80211_ADDR_LEN];
	UINT8 rsn_ie[IEEE80211_MAX_OPT_IE];
};

struct hostapd_wpa_psk {
	struct hostapd_wpa_psk *next;
	int group;
	UCHAR psk[32];
	UCHAR addr[6];
};

#endif /*HOSTAPD_SUPPORT */

#define RT_OID_802_11_QUERY_TDLS_PARAM			0x0676
#define	RT_OID_802_11_QUERY_TDLS				0x0677

/* Ralink defined OIDs */
/* Dennis Lee move to platform specific */

#define	RT_OID_802_11_BSSID					  (OID_GET_SET_TOGGLE |	OID_802_11_BSSID)
#define	RT_OID_802_11_SSID					  (OID_GET_SET_TOGGLE |	OID_802_11_SSID)
#define	RT_OID_802_11_INFRASTRUCTURE_MODE	  (OID_GET_SET_TOGGLE |	OID_802_11_INFRASTRUCTURE_MODE)
#define	RT_OID_802_11_ADD_WEP				  (OID_GET_SET_TOGGLE |	OID_802_11_ADD_WEP)
#define	RT_OID_802_11_ADD_KEY				  (OID_GET_SET_TOGGLE |	OID_802_11_ADD_KEY)
#define	RT_OID_802_11_REMOVE_WEP			  (OID_GET_SET_TOGGLE |	OID_802_11_REMOVE_WEP)
#define	RT_OID_802_11_REMOVE_KEY			  (OID_GET_SET_TOGGLE |	OID_802_11_REMOVE_KEY)
#define	RT_OID_802_11_DISASSOCIATE			  (OID_GET_SET_TOGGLE |	OID_802_11_DISASSOCIATE)
#define	RT_OID_802_11_AUTHENTICATION_MODE	  (OID_GET_SET_TOGGLE |	OID_802_11_AUTHENTICATION_MODE)
#define	RT_OID_802_11_PRIVACY_FILTER		  (OID_GET_SET_TOGGLE |	OID_802_11_PRIVACY_FILTER)
#define	RT_OID_802_11_BSSID_LIST_SCAN		  (OID_GET_SET_TOGGLE |	OID_802_11_BSSID_LIST_SCAN)
#define	RT_OID_802_11_WEP_STATUS			  (OID_GET_SET_TOGGLE |	OID_802_11_WEP_STATUS)
#define	RT_OID_802_11_RELOAD_DEFAULTS		  (OID_GET_SET_TOGGLE |	OID_802_11_RELOAD_DEFAULTS)
#define	RT_OID_802_11_NETWORK_TYPE_IN_USE	  (OID_GET_SET_TOGGLE |	OID_802_11_NETWORK_TYPE_IN_USE)
#define	RT_OID_802_11_TX_POWER_LEVEL		  (OID_GET_SET_TOGGLE |	OID_802_11_TX_POWER_LEVEL)
#define	RT_OID_802_11_RSSI_TRIGGER			  (OID_GET_SET_TOGGLE |	OID_802_11_RSSI_TRIGGER)
#define	RT_OID_802_11_FRAGMENTATION_THRESHOLD (OID_GET_SET_TOGGLE |	OID_802_11_FRAGMENTATION_THRESHOLD)
#define	RT_OID_802_11_RTS_THRESHOLD			  (OID_GET_SET_TOGGLE |	OID_802_11_RTS_THRESHOLD)
#define	RT_OID_802_11_RX_ANTENNA_SELECTED	  (OID_GET_SET_TOGGLE |	OID_802_11_RX_ANTENNA_SELECTED)
#define	RT_OID_802_11_TX_ANTENNA_SELECTED	  (OID_GET_SET_TOGGLE |	OID_802_11_TX_ANTENNA_SELECTED)
#define	RT_OID_802_11_SUPPORTED_RATES		  (OID_GET_SET_TOGGLE |	OID_802_11_SUPPORTED_RATES)
#define	RT_OID_802_11_DESIRED_RATES			  (OID_GET_SET_TOGGLE |	OID_802_11_DESIRED_RATES)
#define	RT_OID_802_11_CONFIGURATION			  (OID_GET_SET_TOGGLE |	OID_802_11_CONFIGURATION)
#define	RT_OID_802_11_POWER_MODE			  (OID_GET_SET_TOGGLE |	OID_802_11_POWER_MODE)
#define RT_OID_802_11_SET_PSPXLINK_MODE		  (OID_GET_SET_TOGGLE |	OID_802_11_SET_PSPXLINK_MODE)
#define RT_OID_802_11_EAP_METHOD			  (OID_GET_SET_TOGGLE | OID_802_11_EAP_METHOD)
#define RT_OID_802_11_SET_PASSPHRASE		  (OID_GET_SET_TOGGLE | OID_802_11_SET_PASSPHRASE)

#ifdef DOT1X_SUPPORT
#define RT_OID_802_DOT1X_PMKID_CACHE		(OID_GET_SET_TOGGLE | OID_802_DOT1X_PMKID_CACHE)
#define RT_OID_802_DOT1X_RADIUS_DATA		(OID_GET_SET_TOGGLE | OID_802_DOT1X_RADIUS_DATA)
#define RT_OID_802_DOT1X_WPA_KEY			(OID_GET_SET_TOGGLE | OID_802_DOT1X_WPA_KEY)
#define RT_OID_802_DOT1X_STATIC_WEP_COPY	(OID_GET_SET_TOGGLE | OID_802_DOT1X_STATIC_WEP_COPY)
#define RT_OID_802_DOT1X_IDLE_TIMEOUT		(OID_GET_SET_TOGGLE | OID_802_DOT1X_IDLE_TIMEOUT)
#endif /* DOT1X_SUPPORT */

#define RT_OID_802_11_SET_TDLS_PARAM			(OID_GET_SET_TOGGLE | RT_OID_802_11_QUERY_TDLS_PARAM)
#define RT_OID_802_11_SET_TDLS				(OID_GET_SET_TOGGLE | RT_OID_802_11_QUERY_TDLS)


typedef enum _NDIS_802_11_STATUS_TYPE {
	Ndis802_11StatusType_Authentication,
	Ndis802_11StatusType_MediaStreamMode,
	Ndis802_11StatusType_PMKID_CandidateList,
	Ndis802_11StatusTypeMax	/* not a real type, defined as an upper bound */
} NDIS_802_11_STATUS_TYPE, *PNDIS_802_11_STATUS_TYPE;

typedef UCHAR NDIS_802_11_MAC_ADDRESS[6];

typedef struct _NDIS_802_11_STATUS_INDICATION {
	NDIS_802_11_STATUS_TYPE StatusType;
} NDIS_802_11_STATUS_INDICATION, *PNDIS_802_11_STATUS_INDICATION;

/* mask for authentication/integrity fields */
#define NDIS_802_11_AUTH_REQUEST_AUTH_FIELDS        0x0f

#define NDIS_802_11_AUTH_REQUEST_REAUTH             0x01
#define NDIS_802_11_AUTH_REQUEST_KEYUPDATE          0x02
#define NDIS_802_11_AUTH_REQUEST_PAIRWISE_ERROR     0x06
#define NDIS_802_11_AUTH_REQUEST_GROUP_ERROR        0x0E

typedef struct _NDIS_802_11_AUTHENTICATION_REQUEST {
	ULONG Length;		/* Length of structure */
	NDIS_802_11_MAC_ADDRESS Bssid;
	ULONG Flags;
} NDIS_802_11_AUTHENTICATION_REQUEST, *PNDIS_802_11_AUTHENTICATION_REQUEST;

/*Added new types for PMKID Candidate lists. */
typedef struct _PMKID_CANDIDATE {
	NDIS_802_11_MAC_ADDRESS BSSID;
	ULONG Flags;
} PMKID_CANDIDATE, *PPMKID_CANDIDATE;

typedef struct _NDIS_802_11_PMKID_CANDIDATE_LIST {
	ULONG Version;		/* Version of the structure */
	ULONG NumCandidates;	/* No. of pmkid candidates */
	PMKID_CANDIDATE CandidateList[1];
} NDIS_802_11_PMKID_CANDIDATE_LIST, *PNDIS_802_11_PMKID_CANDIDATE_LIST;

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

typedef struct _NDIS_802_11_NETWORK_TYPE_LIST {
	UINT NumberOfItems;	/* in list below, at least 1 */
	NDIS_802_11_NETWORK_TYPE NetworkType[1];
} NDIS_802_11_NETWORK_TYPE_LIST, *PNDIS_802_11_NETWORK_TYPE_LIST;

typedef enum _NDIS_802_11_POWER_MODE {
	Ndis802_11PowerModeCAM,
	Ndis802_11PowerModeMAX_PSP,
	Ndis802_11PowerModeFast_PSP,
	Ndis802_11PowerModeLegacy_PSP,
	Ndis802_11PowerModeMax	/* not a real mode, defined as an upper bound */
} NDIS_802_11_POWER_MODE, *PNDIS_802_11_POWER_MODE;

typedef ULONG NDIS_802_11_TX_POWER_LEVEL;	/* in milliwatts */

/* */
/* Received Signal Strength Indication */
/* */
typedef LONG NDIS_802_11_RSSI;	/* in dBm */

typedef struct _NDIS_802_11_CONFIGURATION_FH {
	ULONG Length;		/* Length of structure */
	ULONG HopPattern;	/* As defined by 802.11, MSB set */
	ULONG HopSet;		/* to one if non-802.11 */
	ULONG DwellTime;	/* units are Kusec */
} NDIS_802_11_CONFIGURATION_FH, *PNDIS_802_11_CONFIGURATION_FH;

typedef struct _NDIS_802_11_CONFIGURATION {
	ULONG Length;		/* Length of structure */
	ULONG BeaconPeriod;	/* units are Kusec */
	ULONG ATIMWindow;	/* units are Kusec */
	ULONG DSConfig;		/* Frequency, units are kHz */
	NDIS_802_11_CONFIGURATION_FH FHConfig;
} NDIS_802_11_CONFIGURATION, *PNDIS_802_11_CONFIGURATION;

typedef struct _NDIS_802_11_STATISTICS {
	ULONG Length;		/* Length of structure */
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
} NDIS_802_11_STATISTICS, *PNDIS_802_11_STATISTICS;

typedef ULONG NDIS_802_11_KEY_INDEX;
typedef ULONGLONG NDIS_802_11_KEY_RSC;

#ifdef DOT1X_SUPPORT
#define MAX_RADIUS_SRV_NUM			2	/* 802.1x failover number */

/* The dot1x related structure. 
   It's used to communicate with DOT1X daemon */
typedef struct GNU_PACKED _RADIUS_SRV_INFO {
	UINT32 radius_ip;
	UINT32 radius_port;
	UCHAR radius_key[64];
	UCHAR radius_key_len;
} RADIUS_SRV_INFO, *PRADIUS_SRV_INFO;

typedef struct GNU_PACKED _DOT1X_BSS_INFO {
	UCHAR radius_srv_num;
	RADIUS_SRV_INFO radius_srv_info[MAX_RADIUS_SRV_NUM];
	UCHAR ieee8021xWEP;	/* dynamic WEP */
	UCHAR key_index;
	UCHAR key_length;	/* length of key in bytes */
	UCHAR key_material[13];
	UCHAR nasId[IFNAMSIZ];
	UCHAR nasId_len;
} DOT1X_BSS_INFO, *PDOT1X_BSS_INFO;

typedef struct GNU_PACKED _DOT1X_CMM_CONF {
	UINT32 Length;		/* Length of this structure */
	UCHAR mbss_num;		/* indicate multiple BSS number */
	UINT32 own_ip_addr;
	UINT32 retry_interval;
	UINT32 session_timeout_interval;
	UINT32 quiet_interval;
	UCHAR EAPifname[8][IFNAMSIZ];
	UCHAR EAPifname_len[8];
	UCHAR PreAuthifname[8][IFNAMSIZ];
	UCHAR PreAuthifname_len[8];
	DOT1X_BSS_INFO Dot1xBssInfo[8];
} DOT1X_CMM_CONF, *PDOT1X_CMM_CONF;

typedef struct GNU_PACKED _DOT1X_IDLE_TIMEOUT {
	UCHAR StaAddr[6];
	UINT32 idle_timeout;
} DOT1X_IDLE_TIMEOUT, *PDOT1X_IDLE_TIMEOUT;
#endif /* DOT1X_SUPPORT */


#ifdef CONFIG_STA_SUPPORT
/* Key mapping keys require a BSSID */
typedef struct _NDIS_802_11_KEY {
	UINT Length;		/* Length of this structure */
	UINT KeyIndex;
	UINT KeyLength;		/* length of key in bytes */
	NDIS_802_11_MAC_ADDRESS BSSID;
	NDIS_802_11_KEY_RSC KeyRSC;
	UCHAR KeyMaterial[1];	/* variable length depending on above field */
} NDIS_802_11_KEY, *PNDIS_802_11_KEY;

typedef struct _NDIS_802_11_PASSPHRASE {
	UINT KeyLength;		/* length of key in bytes */
	NDIS_802_11_MAC_ADDRESS BSSID;
	UCHAR KeyMaterial[1];	/* variable length depending on above field */
} NDIS_802_11_PASSPHRASE, *PNDIS_802_11_PASSPHRASE;
#endif /* CONFIG_STA_SUPPORT */

typedef struct _NDIS_802_11_REMOVE_KEY {
	UINT Length;		/* Length of this structure */
	UINT KeyIndex;
	NDIS_802_11_MAC_ADDRESS BSSID;
} NDIS_802_11_REMOVE_KEY, *PNDIS_802_11_REMOVE_KEY;

typedef struct _NDIS_802_11_WEP {
	UINT Length;		/* Length of this structure */
	UINT KeyIndex;		/* 0 is the per-client key, 1-N are the */
	/* global keys */
	UINT KeyLength;		/* length of key in bytes */
	UCHAR KeyMaterial[1];	/* variable length depending on above field */
} NDIS_802_11_WEP, *PNDIS_802_11_WEP;


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

typedef UCHAR NDIS_802_11_RATES[NDIS_802_11_LENGTH_RATES];	/* Set of 8 data rates */
typedef UCHAR NDIS_802_11_RATES_EX[NDIS_802_11_LENGTH_RATES_EX];	/* Set of 16 data rates */

typedef struct GNU_PACKED _NDIS_802_11_SSID {
	UINT SsidLength;	/* length of SSID field below, in bytes; */
	/* this can be zero. */
	UCHAR Ssid[NDIS_802_11_LENGTH_SSID];	/* SSID information field */
} NDIS_802_11_SSID, *PNDIS_802_11_SSID;

typedef struct GNU_PACKED _NDIS_WLAN_BSSID {
	ULONG Length;		/* Length of this structure */
	NDIS_802_11_MAC_ADDRESS MacAddress;	/* BSSID */
	UCHAR Reserved[2];
	NDIS_802_11_SSID Ssid;	/* SSID */
	ULONG Privacy;		/* WEP encryption requirement */
	NDIS_802_11_RSSI Rssi;	/* receive signal strength in dBm */
	NDIS_802_11_NETWORK_TYPE NetworkTypeInUse;
	NDIS_802_11_CONFIGURATION Configuration;
	NDIS_802_11_NETWORK_INFRASTRUCTURE InfrastructureMode;
	NDIS_802_11_RATES SupportedRates;
} NDIS_WLAN_BSSID, *PNDIS_WLAN_BSSID;

typedef struct GNU_PACKED _NDIS_802_11_BSSID_LIST {
	UINT NumberOfItems;	/* in list below, at least 1 */
	NDIS_WLAN_BSSID Bssid[1];
} NDIS_802_11_BSSID_LIST, *PNDIS_802_11_BSSID_LIST;

typedef struct {
	BOOLEAN bValid;		/* 1: variable contains valid value */
	USHORT StaNum;
	UCHAR ChannelUtilization;
	USHORT RemainingAdmissionControl;	/* in unit of 32-us */
} QBSS_LOAD_UI, *PQBSS_LOAD_UI;

/* Added Capabilities, IELength and IEs for each BSSID */
typedef struct GNU_PACKED _NDIS_WLAN_BSSID_EX {
	ULONG Length;		/* Length of this structure */
	NDIS_802_11_MAC_ADDRESS MacAddress;	/* BSSID */
	UCHAR WpsAP; /* 0x00: not support WPS, 0x01: support normal WPS, 0x02: support Ralink auto WPS, 0x04: support Samsung WAC */
	CHAR MinSNR;
	NDIS_802_11_SSID Ssid;	/* SSID */
	UINT Privacy;		/* WEP encryption requirement */
	NDIS_802_11_RSSI Rssi;	/* receive signal */
	/* strength in dBm */
	NDIS_802_11_NETWORK_TYPE NetworkTypeInUse;
	NDIS_802_11_CONFIGURATION Configuration;
	NDIS_802_11_NETWORK_INFRASTRUCTURE InfrastructureMode;
	NDIS_802_11_RATES_EX SupportedRates;
	ULONG IELength;
	UCHAR IEs[1];

} NDIS_WLAN_BSSID_EX, *PNDIS_WLAN_BSSID_EX;

typedef struct GNU_PACKED _NDIS_802_11_BSSID_LIST_EX {
	UINT NumberOfItems;	/* in list below, at least 1 */
	NDIS_WLAN_BSSID_EX Bssid[1];
} NDIS_802_11_BSSID_LIST_EX, *PNDIS_802_11_BSSID_LIST_EX;

typedef struct GNU_PACKED _NDIS_802_11_FIXED_IEs {
	UCHAR Timestamp[8];
	USHORT BeaconInterval;
	USHORT Capabilities;
} NDIS_802_11_FIXED_IEs, *PNDIS_802_11_FIXED_IEs;

typedef struct _NDIS_802_11_VARIABLE_IEs {
	UCHAR ElementID;
	UCHAR Length;		/* Number of bytes in data field */
	UCHAR data[1];
} NDIS_802_11_VARIABLE_IEs, *PNDIS_802_11_VARIABLE_IEs;

typedef ULONG NDIS_802_11_FRAGMENTATION_THRESHOLD;

typedef ULONG NDIS_802_11_RTS_THRESHOLD;

typedef ULONG NDIS_802_11_ANTENNA;

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
} NDIS_802_11_WEP_STATUS, *PNDIS_802_11_WEP_STATUS, NDIS_802_11_ENCRYPTION_STATUS, *PNDIS_802_11_ENCRYPTION_STATUS;

typedef enum _NDIS_802_11_RELOAD_DEFAULTS {
	Ndis802_11ReloadWEPKeys
} NDIS_802_11_RELOAD_DEFAULTS, *PNDIS_802_11_RELOAD_DEFAULTS;

#define NDIS_802_11_AI_REQFI_CAPABILITIES      1
#define NDIS_802_11_AI_REQFI_LISTENINTERVAL    2
#define NDIS_802_11_AI_REQFI_CURRENTAPADDRESS  4

#define NDIS_802_11_AI_RESFI_CAPABILITIES      1
#define NDIS_802_11_AI_RESFI_STATUSCODE        2
#define NDIS_802_11_AI_RESFI_ASSOCIATIONID     4

typedef struct _NDIS_802_11_AI_REQFI {
	USHORT Capabilities;
	USHORT ListenInterval;
	NDIS_802_11_MAC_ADDRESS CurrentAPAddress;
} NDIS_802_11_AI_REQFI, *PNDIS_802_11_AI_REQFI;

typedef struct _NDIS_802_11_AI_RESFI {
	USHORT Capabilities;
	USHORT StatusCode;
	USHORT AssociationId;
} NDIS_802_11_AI_RESFI, *PNDIS_802_11_AI_RESFI;

typedef struct _NDIS_802_11_ASSOCIATION_INFORMATION {
	ULONG Length;
	USHORT AvailableRequestFixedIEs;
	NDIS_802_11_AI_REQFI RequestFixedIEs;
	ULONG RequestIELength;
	ULONG OffsetRequestIEs;
	USHORT AvailableResponseFixedIEs;
	NDIS_802_11_AI_RESFI ResponseFixedIEs;
	ULONG ResponseIELength;
	ULONG OffsetResponseIEs;
} NDIS_802_11_ASSOCIATION_INFORMATION, *PNDIS_802_11_ASSOCIATION_INFORMATION;

typedef struct _NDIS_802_11_AUTHENTICATION_EVENT {
	NDIS_802_11_STATUS_INDICATION Status;
	NDIS_802_11_AUTHENTICATION_REQUEST Request[1];
} NDIS_802_11_AUTHENTICATION_EVENT, *PNDIS_802_11_AUTHENTICATION_EVENT;

/*        
typedef struct _NDIS_802_11_TEST
{
    ULONG Length;
    ULONG Type;
    union
    {
        NDIS_802_11_AUTHENTICATION_EVENT AuthenticationEvent;
        NDIS_802_11_RSSI RssiTrigger;
    };
} NDIS_802_11_TEST, *PNDIS_802_11_TEST;
 */

/* 802.11 Media stream constraints, associated with OID_802_11_MEDIA_STREAM_MODE */
typedef enum _NDIS_802_11_MEDIA_STREAM_MODE {
	Ndis802_11MediaStreamOff,
	Ndis802_11MediaStreamOn,
} NDIS_802_11_MEDIA_STREAM_MODE, *PNDIS_802_11_MEDIA_STREAM_MODE;

/* PMKID Structures */
typedef UCHAR NDIS_802_11_PMKID_VALUE[16];

#ifdef CONFIG_STA_SUPPORT
typedef struct _BSSID_INFO {
	NDIS_802_11_MAC_ADDRESS BSSID;
	NDIS_802_11_PMKID_VALUE PMKID;
} BSSID_INFO, *PBSSID_INFO;

typedef struct _NDIS_802_11_PMKID {
	UINT Length;
	UINT BSSIDInfoCount;
	BSSID_INFO BSSIDInfo[1];
} NDIS_802_11_PMKID, *PNDIS_802_11_PMKID;
#endif /* CONFIG_STA_SUPPORT */


typedef struct _NDIS_802_11_AUTHENTICATION_ENCRYPTION {
	NDIS_802_11_AUTHENTICATION_MODE AuthModeSupported;
	NDIS_802_11_ENCRYPTION_STATUS EncryptStatusSupported;
} NDIS_802_11_AUTHENTICATION_ENCRYPTION, *PNDIS_802_11_AUTHENTICATION_ENCRYPTION;

typedef struct _NDIS_802_11_CAPABILITY {
	ULONG Length;
	ULONG Version;
	ULONG NoOfPMKIDs;
	ULONG NoOfAuthEncryptPairsSupported;
	NDIS_802_11_AUTHENTICATION_ENCRYPTION
	    AuthenticationEncryptionSupported[1];
} NDIS_802_11_CAPABILITY, *PNDIS_802_11_CAPABILITY;



#ifdef DBG
/*
	When use private ioctl oid get/set the configuration, we can use following flags to provide specific rules when handle the cmd
 */
#define RTPRIV_IOCTL_FLAG_UI			0x0001	/* Notidy this private cmd send by UI. */
#define RTPRIV_IOCTL_FLAG_NODUMPMSG	0x0002	/* Notify driver cannot dump msg to stdio/stdout when run this private ioctl cmd */
#define RTPRIV_IOCTL_FLAG_NOSPACE		0x0004	/* Notify driver didn't need copy msg to caller due to the caller didn't reserve space for this cmd */
#endif /* DBG */


#ifdef SNMP_SUPPORT
/*SNMP ieee 802dot11, kathy , 2008_0220 */
/* dot11res(3) */
#define RT_OID_802_11_MANUFACTUREROUI			0x0700
#define RT_OID_802_11_MANUFACTURERNAME			0x0701
#define RT_OID_802_11_RESOURCETYPEIDNAME		0x0702

/* dot11smt(1) */
#define RT_OID_802_11_PRIVACYOPTIONIMPLEMENTED	0x0703
#define RT_OID_802_11_POWERMANAGEMENTMODE		0x0704
#define OID_802_11_WEPDEFAULTKEYVALUE			0x0705	/* read , write */
#define OID_802_11_WEPDEFAULTKEYID				0x0706
#define RT_OID_802_11_WEPKEYMAPPINGLENGTH		0x0707
#define OID_802_11_SHORTRETRYLIMIT				0x0708
#define OID_802_11_LONGRETRYLIMIT				0x0709
#define RT_OID_802_11_PRODUCTID					0x0710
#define RT_OID_802_11_MANUFACTUREID				0x0711

/* //dot11Phy(4) */
#define OID_802_11_CURRENTCHANNEL				0x0712

#endif /* SNMP_SUPPORT */

/*dot11mac */
#define RT_OID_802_11_MAC_ADDRESS				0x0713
#define OID_802_11_BUILD_CHANNEL_EX				0x0714
#define OID_802_11_GET_CH_LIST					0x0715
#define OID_802_11_GET_COUNTRY_CODE				0x0716
#define OID_802_11_GET_CHANNEL_GEOGRAPHY		0x0717

/*#define RT_OID_802_11_STATISTICS              (OID_GET_SET_TOGGLE | OID_802_11_STATISTICS) */


#ifdef CONFIG_STA_SUPPORT
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
#endif /* CONFIG_STA_SUPPORT */
#define	RT_OID_WSC_FRAGMENT_SIZE					0x074D
#define	RT_OID_WSC_V2_SUPPORT						0x074E
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
#define RT_OID_WSC_READ_UFD_FILE					0x075A
#define RT_OID_WSC_WRITE_UFD_FILE					0x075B
#define RT_OID_WSC_QUERY_PEER_INFO_ON_RUNNING		0x075C
#define RT_OID_WSC_MAC_ADDRESS						0x0760

#ifdef LLTD_SUPPORT
/* for consistency with RT61 */
#define RT_OID_GET_PHY_MODE                         0x761
#endif /* LLTD_SUPPORT */

#ifdef NINTENDO_AP
/*#define RT_OID_NINTENDO                             0x0D010770 */
#define RT_OID_802_11_NINTENDO_GET_TABLE			0x0771	/*((RT_OID_NINTENDO + 0x01) & 0xffff) */
#define RT_OID_802_11_NINTENDO_SET_TABLE			0x0772	/*((RT_OID_NINTENDO + 0x02) & 0xffff) */
#define RT_OID_802_11_NINTENDO_CAPABLE				0x0773	/*((RT_OID_NINTENDO + 0x03) & 0xffff) */
#endif /* NINTENDO_AP */



/* New for MeetingHouse Api support */
#define OID_MH_802_1X_SUPPORTED               0xFFEDC100

/* MIMO Tx parameter, ShortGI, MCS, STBC, etc.  these are fields in TXWI. Don't change this definition!!! */
typedef union _HTTRANSMIT_SETTING {
#ifdef RT_BIG_ENDIAN
	struct {
		USHORT MODE:2;	/* Use definition MODE_xxx. */
		USHORT iTxBF:1;
		USHORT rsv:1;
		USHORT eTxBF:1;
		USHORT STBC:2;	/*SPACE */
		USHORT ShortGI:1;
		USHORT BW:1;	/*channel bandwidth 20MHz or 40 MHz */
		USHORT MCS:7;	/* MCS */
	} field;
#else
	struct {
		USHORT MCS:7;	/* MCS */
		USHORT BW:1;	/*channel bandwidth 20MHz or 40 MHz */
		USHORT ShortGI:1;
		USHORT STBC:2;	/*SPACE */
		USHORT eTxBF:1;
		USHORT rsv:1;
		USHORT iTxBF:1;
		USHORT MODE:2;	/* Use definition MODE_xxx. */
	} field;
#endif
	USHORT word;
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
#ifdef DOT11_N_SUPPORT
	PHY_11ABGN_MIXED,	/* both band   5 */
	PHY_11N_2_4G,		/* 11n-only with 2.4G band      6 */
	PHY_11GN_MIXED,		/* 2.4G band      7 */
	PHY_11AN_MIXED,		/* 5G  band       8 */
	PHY_11BGN_MIXED,	/* if check 802.11b.      9 */
	PHY_11AGN_MIXED,	/* if check 802.11b.      10 */
	PHY_11N_5G,		/* 11n-only with 5G band                11 */
#endif /* DOT11_N_SUPPORT */
} RT_802_11_PHY_MODE;

#ifdef DOT11_N_SUPPORT
#define PHY_MODE_IS_5G_BAND(__Mode)	\
	((__Mode == PHY_11A) ||			\
	(__Mode == PHY_11ABG_MIXED) ||	\
	(__Mode == PHY_11ABGN_MIXED) ||	\
	(__Mode == PHY_11AN_MIXED) ||	\
	(__Mode == PHY_11AGN_MIXED) ||	\
	(__Mode == PHY_11N_5G))
#else

#define PHY_MODE_IS_5G_BAND(__Mode)	\
	((__Mode == PHY_11A) ||			\
	(__Mode == PHY_11ABG_MIXED))
#endif /* DOT11_N_SUPPORT */

/* put all proprietery for-query objects here to reduce # of Query_OID */
typedef struct _RT_802_11_LINK_STATUS {
	ULONG CurrTxRate;	/* in units of 0.5Mbps */
	ULONG ChannelQuality;	/* 0..100 % */
	ULONG TxByteCount;	/* both ok and fail */
	ULONG RxByteCount;	/* both ok and fail */
	ULONG CentralChannel;	/* 40MHz central channel number */
} RT_802_11_LINK_STATUS, *PRT_802_11_LINK_STATUS;

#ifdef SYSTEM_LOG_SUPPORT
typedef struct _RT_802_11_EVENT_LOG {
	LARGE_INTEGER SystemTime;	/* timestammp via NdisGetCurrentSystemTime() */
	UCHAR Addr[MAC_ADDR_LENGTH];
	USHORT Event;		/* EVENT_xxx */
} RT_802_11_EVENT_LOG, *PRT_802_11_EVENT_LOG;

typedef struct _RT_802_11_EVENT_TABLE {
	ULONG Num;
	ULONG Rsv;		/* to align Log[] at LARGE_INEGER boundary */
	RT_802_11_EVENT_LOG Log[MAX_NUMBER_OF_EVENT];
} RT_802_11_EVENT_TABLE, *PRT_802_11_EVENT_TABLE;
#endif /* SYSTEM_LOG_SUPPORT */

/* MIMO Tx parameter, ShortGI, MCS, STBC, etc.  these are fields in TXWI. Don't change this definition!!! */
typedef union _MACHTTRANSMIT_SETTING {
	struct {
		USHORT MCS:7;	/* MCS */
		USHORT BW:1;	/*channel bandwidth 20MHz or 40 MHz */
		USHORT ShortGI:1;
		USHORT STBC:2;	/*SPACE */
		USHORT rsv:3;
		USHORT MODE:2;	/* Use definition MODE_xxx. */
	} field;
	USHORT word;
} MACHTTRANSMIT_SETTING, *PMACHTTRANSMIT_SETTING;

typedef struct _RT_802_11_MAC_ENTRY {
	UCHAR ApIdx;
	UCHAR Addr[MAC_ADDR_LENGTH];
	UCHAR Aid;
	UCHAR Psm;		/* 0:PWR_ACTIVE, 1:PWR_SAVE */
	UCHAR MimoPs;		/* 0:MMPS_STATIC, 1:MMPS_DYNAMIC, 3:MMPS_Enabled */
	CHAR AvgRssi0;
	CHAR AvgRssi1;
	CHAR AvgRssi2;
	UINT32 ConnectedTime;
	MACHTTRANSMIT_SETTING TxRate;
} RT_802_11_MAC_ENTRY, *PRT_802_11_MAC_ENTRY;

typedef struct _RT_802_11_MAC_TABLE {
	ULONG Num;
	RT_802_11_MAC_ENTRY Entry[MAX_NUMBER_OF_MAC];
} RT_802_11_MAC_TABLE, *PRT_802_11_MAC_TABLE;

#ifdef DOT11_N_SUPPORT
#ifdef TXBF_SUPPORT
typedef
    struct {
	ULONG TxSuccessCount;
	ULONG TxRetryCount;
	ULONG TxFailCount;
	ULONG ETxSuccessCount;
	ULONG ETxRetryCount;
	ULONG ETxFailCount;
	ULONG ITxSuccessCount;
	ULONG ITxRetryCount;
	ULONG ITxFailCount;
} RT_COUNTER_TXBF;

typedef
    struct {
	ULONG Num;
	RT_COUNTER_TXBF Entry[MAX_NUMBER_OF_MAC];
} RT_802_11_TXBF_TABLE;
#endif /* TXBF_SUPPORT */
#endif /* DOT11_N_SUPPORT */

/* structure for query/set hardware register - MAC, BBP, RF register */
typedef struct _RT_802_11_HARDWARE_REGISTER {
	ULONG HardwareType;	/* 0:MAC, 1:BBP, 2:RF register, 3:EEPROM */
	ULONG Offset;		/* Q/S register offset addr */
	ULONG Data;		/* R/W data buffer */
} RT_802_11_HARDWARE_REGISTER, *PRT_802_11_HARDWARE_REGISTER;

typedef struct _RT_802_11_AP_CONFIG {
	ULONG EnableTxBurst;	/* 0-disable, 1-enable */
	ULONG EnableTurboRate;	/* 0-disable, 1-enable 72/100mbps turbo rate */
	ULONG IsolateInterStaTraffic;	/* 0-disable, 1-enable isolation */
	ULONG HideSsid;		/* 0-disable, 1-enable hiding */
	ULONG UseBGProtection;	/* 0-AUTO, 1-always ON, 2-always OFF */
	ULONG UseShortSlotTime;	/* 0-no use, 1-use 9-us short slot time */
	ULONG Rsv1;		/* must be 0 */
	ULONG SystemErrorBitmap;	/* ignore upon SET, return system error upon QUERY */
} RT_802_11_AP_CONFIG, *PRT_802_11_AP_CONFIG;

/* structure to query/set STA_CONFIG */
typedef struct _RT_802_11_STA_CONFIG {
	ULONG EnableTxBurst;	/* 0-disable, 1-enable */
	ULONG EnableTurboRate;	/* 0-disable, 1-enable 72/100mbps turbo rate */
	ULONG UseBGProtection;	/* 0-AUTO, 1-always ON, 2-always OFF */
	ULONG UseShortSlotTime;	/* 0-no use, 1-use 9-us short slot time when applicable */
	ULONG AdhocMode;	/* 0-11b rates only (WIFI spec), 1 - b/g mixed, 2 - g only */
	ULONG HwRadioStatus;	/* 0-OFF, 1-ON, default is 1, Read-Only */
	ULONG Rsv1;		/* must be 0 */
	ULONG SystemErrorBitmap;	/* ignore upon SET, return system error upon QUERY */
} RT_802_11_STA_CONFIG, *PRT_802_11_STA_CONFIG;

/* */
/*  For OID Query or Set about BA structure */
/* */
typedef struct _OID_BACAP_STRUC {
	UCHAR RxBAWinLimit;
	UCHAR TxBAWinLimit;
	UCHAR Policy;		/* 0: DELAY_BA 1:IMMED_BA  (//BA Policy subfiled value in ADDBA frame)   2:BA-not use. other value invalid */
	UCHAR MpduDensity;	/* 0: DELAY_BA 1:IMMED_BA  (//BA Policy subfiled value in ADDBA frame)   2:BA-not use. other value invalid */
	UCHAR AmsduEnable;	/*Enable AMSDU transmisstion */
	UCHAR AmsduSize;	/* 0:3839, 1:7935 bytes. UINT  MSDUSizeToBytes[]        = { 3839, 7935}; */
	UCHAR MMPSmode;		/* MIMO power save more, 0:static, 1:dynamic, 2:rsv, 3:mimo enable */
	BOOLEAN AutoBA;		/* Auto BA will automatically */
} OID_BACAP_STRUC, *POID_BACAP_STRUC;

typedef struct _RT_802_11_ACL_ENTRY {
	UCHAR Addr[MAC_ADDR_LENGTH];
	USHORT Rsv;
} RT_802_11_ACL_ENTRY, *PRT_802_11_ACL_ENTRY;

typedef struct GNU_PACKED _RT_802_11_ACL {
	ULONG Policy;		/* 0-disable, 1-positive list, 2-negative list */
	ULONG Num;
	RT_802_11_ACL_ENTRY Entry[MAX_NUMBER_OF_ACL];
} RT_802_11_ACL, *PRT_802_11_ACL;

typedef struct _RT_802_11_WDS {
	ULONG Num;
	NDIS_802_11_MAC_ADDRESS Entry[24 /*MAX_NUM_OF_WDS_LINK */ ];
	ULONG KeyLength;
	UCHAR KeyMaterial[32];
} RT_802_11_WDS, *PRT_802_11_WDS;

typedef struct _RT_802_11_TX_RATES_ {
	UCHAR SupRateLen;
	UCHAR SupRate[MAX_LENGTH_OF_SUPPORT_RATES];
	UCHAR ExtRateLen;
	UCHAR ExtRate[MAX_LENGTH_OF_SUPPORT_RATES];
} RT_802_11_TX_RATES, *PRT_802_11_TX_RATES;

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
typedef struct {
	RT_802_11_PHY_MODE PhyMode;	/* */
	UCHAR TransmitNo;
	UCHAR HtMode;		/*HTMODE_GF or HTMODE_MM */
	UCHAR ExtOffset;	/*extension channel above or below */
	UCHAR MCS;
	UCHAR BW;
	UCHAR STBC;
	UCHAR SHORTGI;
	UCHAR rsv;
} OID_SET_HT_PHYMODE, *POID_SET_HT_PHYMODE;

#ifdef NINTENDO_AP
#define NINTENDO_MAX_ENTRY 16
#define NINTENDO_SSID_NAME_LN 8
#define NINTENDO_SSID_NAME "NWCUSBAP"
#define NINTENDO_PROBE_REQ_FLAG_MASK 0x03
#define NINTENDO_PROBE_REQ_ON 0x01
#define NINTENDO_PROBE_REQ_SIGNAL 0x02
#define NINTENDO_PROBE_RSP_ON 0x01
#define NINTENDO_SSID_NICKNAME_LN 20

#define NINTENDO_WEPKEY_LN 13

typedef struct _NINTENDO_SSID {
	UCHAR NINTENDOFixChar[NINTENDO_SSID_NAME_LN];
	UCHAR zero1;
	UCHAR registe;
	UCHAR ID;
	UCHAR zero2;
	UCHAR NICKname[NINTENDO_SSID_NICKNAME_LN];
} RT_NINTENDO_SSID,
*PRT_NINTENDO_SSID;

typedef struct _NINTENDO_ENTRY {
	UCHAR NICKname[NINTENDO_SSID_NICKNAME_LN];
	UCHAR DS_Addr[ETH_LENGTH_OF_ADDRESS];
	UCHAR registe;
	UCHAR UserSpaceAck;
} RT_NINTENDO_ENTRY, *PRT_NINTENDO_ENTRY;

/*RTPRIV_IOCTL_NINTENDO_GET_TABLE */
/*RTPRIV_IOCTL_NINTENDO_SET_TABLE */
typedef struct _NINTENDO_TABLE {
	UINT number;
	RT_NINTENDO_ENTRY entry[NINTENDO_MAX_ENTRY];
} RT_NINTENDO_TABLE, *PRT_NINTENDO_TABLE;

/*RTPRIV_IOCTL_NINTENDO_SEED_WEPKEY */
typedef struct _NINTENDO_SEED_WEPKEY {
	UCHAR seed[NINTENDO_SSID_NICKNAME_LN];
	UCHAR wepkey[16];	/*use 13 for 104 bits wep key */
} RT_NINTENDO_SEED_WEPKEY, *PRT_NINTENDO_SEED_WEPKEY;
#endif /* NINTENDO_AP */

#ifdef LLTD_SUPPORT
typedef struct _RT_LLTD_ASSOICATION_ENTRY {
	UCHAR Addr[ETH_LENGTH_OF_ADDRESS];
	unsigned short MOR;	/* maximum operational rate */
	UCHAR phyMode;
} RT_LLTD_ASSOICATION_ENTRY, *PRT_LLTD_ASSOICATION_ENTRY;

typedef struct _RT_LLTD_ASSOICATION_TABLE {
	unsigned int Num;
	RT_LLTD_ASSOICATION_ENTRY Entry[MAX_NUMBER_OF_MAC];
} RT_LLTD_ASSOICATION_TABLE, *PRT_LLTD_ASSOICATION_TABLE;
#endif /* LLTD_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
#ifdef QOS_DLS_SUPPORT
/*rt2860, kathy 2007-0118 */
/* structure for DLS */
typedef struct _RT_802_11_DLS_UI {
	USHORT TimeOut;		/* unit: second , set by UI */
	USHORT CountDownTimer;	/* unit: second , used by driver only */
	NDIS_802_11_MAC_ADDRESS MacAddr;	/* set by UI */
	UCHAR Status;		/* 0: none , 1: wait STAkey, 2: finish DLS setup , set by driver only */
	BOOLEAN Valid;		/* 1: valid , 0: invalid , set by UI, use to setup or tear down DLS link */
} RT_802_11_DLS_UI, *PRT_802_11_DLS_UI;

typedef struct _RT_802_11_DLS_INFO {
	RT_802_11_DLS_UI Entry[MAX_NUMBER_OF_DLS_ENTRY];
	UCHAR num;
} RT_802_11_DLS_INFO, *PRT_802_11_DLS_INFO;

typedef enum _RT_802_11_DLS_MODE {
	DLS_NONE,
	DLS_WAIT_KEY,
	DLS_FINISH
} RT_802_11_DLS_MODE;
#endif /* QOS_DLS_SUPPORT */


#endif /* CONFIG_STA_SUPPORT */




/*#define MAX_CUSTOM_LEN 128 */

#ifdef CONFIG_STA_SUPPORT
typedef enum _RT_802_11_D_CLIENT_MODE {
	Rt802_11_D_None,
	Rt802_11_D_Flexible,
	Rt802_11_D_Strict,
} RT_802_11_D_CLIENT_MODE, *PRT_802_11_D_CLIENT_MODE;
#endif /* CONFIG_STA_SUPPORT */

typedef struct _RT_CHANNEL_LIST_INFO {
	UCHAR ChannelList[MAX_NUM_OF_CHS];	/* list all supported channels for site survey */
	UCHAR ChannelListNum;	/* number of channel in ChannelList[] */
} RT_CHANNEL_LIST_INFO, *PRT_CHANNEL_LIST_INFO;

/* WSC configured credential */
typedef struct _WSC_CREDENTIAL {
	NDIS_802_11_SSID SSID;	/* mandatory */
	USHORT AuthType;	/* mandatory, 1: open, 2: wpa-psk, 4: shared, 8:wpa, 0x10: wpa2, 0x20: wpa2-psk */
	USHORT EncrType;	/* mandatory, 1: none, 2: wep, 4: tkip, 8: aes */
	UCHAR Key[64];		/* mandatory, Maximum 64 byte */
	USHORT KeyLength;
	UCHAR MacAddr[MAC_ADDR_LENGTH];	/* mandatory, AP MAC address */
	UCHAR KeyIndex;		/* optional, default is 1 */
	UCHAR bFromUPnP;	/* TRUE: This credential is from external UPnP registrar */
	UCHAR Rsvd[2];		/* Make alignment */
} WSC_CREDENTIAL, *PWSC_CREDENTIAL;

/* WSC configured profiles */
typedef struct _WSC_PROFILE {
	UINT ProfileCnt;
	UINT ApplyProfileIdx;	/* add by johnli, fix WPS test plan 5.1.1 */
	WSC_CREDENTIAL Profile[8];	/* Support up to 8 profiles */
} WSC_PROFILE, *PWSC_PROFILE;







#endif /* _OID_H_ */
