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

#define TRUE				1
#define FALSE				0
//
// IEEE 802.11 Structures and definitions
//
#define MAX_TX_POWER_LEVEL              100   /* mW */
#define MAX_RSSI_TRIGGER                -10    /* dBm */
#define MIN_RSSI_TRIGGER                -200   /* dBm */
#define MAX_FRAG_THRESHOLD              2346  /* byte count */
#define MIN_FRAG_THRESHOLD              256   /* byte count */
#define MAX_RTS_THRESHOLD               2347  /* byte count */

// new types for Media Specific Indications
// Extension channel offset
#define EXTCHA_NONE			0
#define EXTCHA_ABOVE		0x1
#define EXTCHA_BELOW		0x3

// BW
#define BAND_WIDTH_20		0
#define BAND_WIDTH_40		1
#define BAND_WIDTH_BOTH		2
#define BAND_WIDTH_10		3	// 802.11j has 10MHz. This definition is for internal usage. doesn't fill in the IE or other field.
// SHORTGI
#define GAP_INTERVAL_400	1	// only support in HT mode
#define GAP_INTERVAL_800	0
#define GAP_INTERVAL_BOTH	2

#define NdisMediaStateConnected			1
#define NdisMediaStateDisconnected		0

#define NDIS_802_11_LENGTH_SSID         32
#define NDIS_802_11_LENGTH_RATES        8
#define NDIS_802_11_LENGTH_RATES_EX     16
#define MAC_ADDR_LENGTH                 6
#define MAX_NUM_OF_CHS					49 // 14 channels @2.4G +  12@UNII + 4 @MMAC + 11 @HiperLAN2 + 7 @Japan + 1 as NULL terminationc
#define MAX_NUMBER_OF_EVENT				10  // entry # in EVENT table
#define MAX_NUMBER_OF_MAC				32 // if MAX_MBSSID_NUM is 8, this value can't be larger than 211
#define MAX_NUMBER_OF_ACL				64
#define MAX_LENGTH_OF_SUPPORT_RATES		12    // 1, 2, 5.5, 11, 6, 9, 12, 18, 24, 36, 48, 54
#define MAX_NUMBER_OF_DLS_ENTRY			4

#define OID_GEN_MACHINE_NAME               0x0001021A

#define RT_QUERY_SIGNAL_CONTEXT				0x0402
#define RT_SET_IAPP_PID                 	0x0404
#define RT_SET_APD_PID						0x0405
#define RT_SET_DEL_MAC_ENTRY				0x0406

//
// IEEE 802.11 OIDs
//
#define	OID_GET_SET_TOGGLE			0x8000

#define	OID_802_11_NETWORK_TYPES_SUPPORTED			0x0103
#define	OID_802_11_NETWORK_TYPE_IN_USE				0x0104
#define	OID_802_11_RSSI_TRIGGER						0x0107
#define	RT_OID_802_11_RSSI							0x0108 //rt2860	only , kathy
#define	RT_OID_802_11_RSSI_1						0x0109 //rt2860	only , kathy
#define	RT_OID_802_11_RSSI_2						0x010A //rt2860	only , kathy
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
#ifdef	DBG
#define	RT_OID_802_11_HARDWARE_REGISTER				0x0525
#endif
#define OID_802_11_ENCRYPTION_STATUS            OID_802_11_WEP_STATUS
#define OID_802_11_DEAUTHENTICATION                 0x0526
#define OID_802_11_DROP_UNENCRYPTED                 0x0527
#define OID_802_11_MIC_FAILURE_REPORT_FRAME         0x0528

// For 802.1x daemin using to require current driver configuration
#define OID_802_11_RADIUS_QUERY_SETTING				0x0540

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
//for WPA_SUPPLICANT_SUPPORT
#define OID_SET_COUNTERMEASURES                     0x0616
#define OID_802_11_SET_IEEE8021X                    0x0617
#define OID_802_11_SET_IEEE8021X_REQUIRE_KEY        0x0618
#define OID_802_11_PMKID                            0x0620
#define RT_OID_WPA_SUPPLICANT_SUPPORT               0x0621
#define RT_OID_WE_VERSION_COMPILED                  0x0622
#define RT_OID_NEW_DRIVER                           0x0623


//rt2860 , kathy
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

// Ralink defined OIDs
// Dennis Lee move to platform specific

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

typedef enum _NDIS_802_11_STATUS_TYPE
{
    Ndis802_11StatusType_Authentication,
    Ndis802_11StatusType_MediaStreamMode,
    Ndis802_11StatusType_PMKID_CandidateList,
    Ndis802_11StatusTypeMax    // not a real type, defined as an upper bound
} NDIS_802_11_STATUS_TYPE, *PNDIS_802_11_STATUS_TYPE;

typedef UCHAR   NDIS_802_11_MAC_ADDRESS[6];

typedef struct _NDIS_802_11_STATUS_INDICATION
{
    NDIS_802_11_STATUS_TYPE StatusType;
} NDIS_802_11_STATUS_INDICATION, *PNDIS_802_11_STATUS_INDICATION;

// mask for authentication/integrity fields
#define NDIS_802_11_AUTH_REQUEST_AUTH_FIELDS        0x0f

#define NDIS_802_11_AUTH_REQUEST_REAUTH             0x01
#define NDIS_802_11_AUTH_REQUEST_KEYUPDATE          0x02
#define NDIS_802_11_AUTH_REQUEST_PAIRWISE_ERROR     0x06
#define NDIS_802_11_AUTH_REQUEST_GROUP_ERROR        0x0E

typedef struct _NDIS_802_11_AUTHENTICATION_REQUEST
{
    ULONG Length;            // Length of structure
    NDIS_802_11_MAC_ADDRESS Bssid;
    ULONG Flags;
} NDIS_802_11_AUTHENTICATION_REQUEST, *PNDIS_802_11_AUTHENTICATION_REQUEST;

//Added new types for PMKID Candidate lists.
typedef struct _PMKID_CANDIDATE {
    NDIS_802_11_MAC_ADDRESS BSSID;
    ULONG Flags;
} PMKID_CANDIDATE, *PPMKID_CANDIDATE;

typedef struct _NDIS_802_11_PMKID_CANDIDATE_LIST
{
    ULONG Version;       // Version of the structure
    ULONG NumCandidates; // No. of pmkid candidates
    PMKID_CANDIDATE CandidateList[1];
} NDIS_802_11_PMKID_CANDIDATE_LIST, *PNDIS_802_11_PMKID_CANDIDATE_LIST;

//Flags for PMKID Candidate list structure
#define NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED	0x01

// Added new types for OFDM 5G and 2.4G
typedef enum _NDIS_802_11_NETWORK_TYPE
{
   Ndis802_11FH,
   Ndis802_11DS,
    Ndis802_11OFDM5,
    Ndis802_11OFDM5_N,
    Ndis802_11OFDM24,
    Ndis802_11OFDM24_N,
   Ndis802_11Automode,
    Ndis802_11NetworkTypeMax    // not a real type, defined as an upper bound
} NDIS_802_11_NETWORK_TYPE, *PNDIS_802_11_NETWORK_TYPE;

typedef struct _NDIS_802_11_NETWORK_TYPE_LIST
{
    UINT                       NumberOfItems;  // in list below, at least 1
   NDIS_802_11_NETWORK_TYPE    NetworkType [1];
} NDIS_802_11_NETWORK_TYPE_LIST, *PNDIS_802_11_NETWORK_TYPE_LIST;

typedef enum _NDIS_802_11_POWER_MODE
{
    Ndis802_11PowerModeCAM,
    Ndis802_11PowerModeMAX_PSP,
    Ndis802_11PowerModeFast_PSP,
    Ndis802_11PowerModeLegacy_PSP,
    Ndis802_11PowerModeMax      // not a real mode, defined as an upper bound
} NDIS_802_11_POWER_MODE, *PNDIS_802_11_POWER_MODE;

typedef ULONG   NDIS_802_11_TX_POWER_LEVEL; // in milliwatts

//
// Received Signal Strength Indication
//
typedef LONG    NDIS_802_11_RSSI;           // in dBm

typedef struct _NDIS_802_11_CONFIGURATION_FH
{
   ULONG           Length;            // Length of structure
   ULONG           HopPattern;        // As defined by 802.11, MSB set
   ULONG           HopSet;            // to one if non-802.11
   ULONG           DwellTime;         // units are Kusec
} NDIS_802_11_CONFIGURATION_FH, *PNDIS_802_11_CONFIGURATION_FH;

typedef struct _NDIS_802_11_CONFIGURATION
{
   ULONG                           Length;             // Length of structure
   ULONG                           BeaconPeriod;       // units are Kusec
   ULONG                           ATIMWindow;         // units are Kusec
   ULONG                           DSConfig;           // Frequency, units are kHz
   NDIS_802_11_CONFIGURATION_FH    FHConfig;
} NDIS_802_11_CONFIGURATION, *PNDIS_802_11_CONFIGURATION;

typedef struct _NDIS_802_11_STATISTICS
{
   ULONG           Length;             // Length of structure
   LARGE_INTEGER   TransmittedFragmentCount;
   LARGE_INTEGER   MulticastTransmittedFrameCount;
   LARGE_INTEGER   FailedCount;
   LARGE_INTEGER   RetryCount;
   LARGE_INTEGER   MultipleRetryCount;
   LARGE_INTEGER   RTSSuccessCount;
   LARGE_INTEGER   RTSFailureCount;
   LARGE_INTEGER   ACKFailureCount;
   LARGE_INTEGER   FrameDuplicateCount;
   LARGE_INTEGER   ReceivedFragmentCount;
   LARGE_INTEGER   MulticastReceivedFrameCount;
   LARGE_INTEGER   FCSErrorCount;
   LARGE_INTEGER   TKIPLocalMICFailures;
   LARGE_INTEGER   TKIPRemoteMICErrors;
   LARGE_INTEGER   TKIPICVErrors;
   LARGE_INTEGER   TKIPCounterMeasuresInvoked;
   LARGE_INTEGER   TKIPReplays;
   LARGE_INTEGER   CCMPFormatErrors;
   LARGE_INTEGER   CCMPReplays;
   LARGE_INTEGER   CCMPDecryptErrors;
   LARGE_INTEGER   FourWayHandshakeFailures;
} NDIS_802_11_STATISTICS, *PNDIS_802_11_STATISTICS;

typedef  ULONG  NDIS_802_11_KEY_INDEX;
typedef ULONGLONG   NDIS_802_11_KEY_RSC;

#define MAX_RADIUS_SRV_NUM			2	  // 802.1x failover number

typedef struct PACKED _RADIUS_SRV_INFO {
	UINT32			radius_ip;
	UINT32			radius_port;
	UCHAR			radius_key[64];
	UCHAR			radius_key_len;
} RADIUS_SRV_INFO, *PRADIUS_SRV_INFO;

typedef struct PACKED _RADIUS_KEY_INFO
{
	UCHAR			radius_srv_num;
	RADIUS_SRV_INFO	radius_srv_info[MAX_RADIUS_SRV_NUM];
	UCHAR			ieee8021xWEP;		 // dynamic WEP
    UCHAR           key_index;
    UCHAR           key_length;          // length of key in bytes
    UCHAR           key_material[13];
} RADIUS_KEY_INFO, *PRADIUS_KEY_INFO;

// It's used by 802.1x daemon to require relative configuration
typedef struct PACKED _RADIUS_CONF
{
    UINT32          Length;             // Length of this structure
    UCHAR			mbss_num;			// indicate multiple BSS number
	UINT32			own_ip_addr;
	UINT32			retry_interval;
	UINT32			session_timeout_interval;
	UCHAR			EAPifname[IFNAMSIZ];
	UCHAR			EAPifname_len;
	UCHAR 			PreAuthifname[IFNAMSIZ];
	UCHAR			PreAuthifname_len;
	RADIUS_KEY_INFO	RadiusInfo[8/*MAX_MBSSID_NUM*/];
} RADIUS_CONF, *PRADIUS_CONF;



#ifdef CONFIG_STA_SUPPORT
// Key mapping keys require a BSSID
typedef struct _NDIS_802_11_KEY
{
    UINT           Length;             // Length of this structure
    UINT           KeyIndex;
    UINT           KeyLength;          // length of key in bytes
    NDIS_802_11_MAC_ADDRESS BSSID;
    NDIS_802_11_KEY_RSC KeyRSC;
    UCHAR           KeyMaterial[1];     // variable length depending on above field
} NDIS_802_11_KEY, *PNDIS_802_11_KEY;
#endif // CONFIG_STA_SUPPORT //

typedef struct _NDIS_802_11_REMOVE_KEY
{
    UINT           Length;             // Length of this structure
    UINT           KeyIndex;
    NDIS_802_11_MAC_ADDRESS BSSID;
} NDIS_802_11_REMOVE_KEY, *PNDIS_802_11_REMOVE_KEY;

typedef struct _NDIS_802_11_WEP
{
   UINT     Length;        // Length of this structure
   UINT     KeyIndex;           // 0 is the per-client key, 1-N are the
                                        // global keys
   UINT     KeyLength;     // length of key in bytes
   UCHAR     KeyMaterial[1];// variable length depending on above field
} NDIS_802_11_WEP, *PNDIS_802_11_WEP;


typedef enum _NDIS_802_11_NETWORK_INFRASTRUCTURE
{
   Ndis802_11IBSS,
   Ndis802_11Infrastructure,
   Ndis802_11AutoUnknown,
   Ndis802_11Monitor,
   Ndis802_11InfrastructureMax     // Not a real value, defined as upper bound
} NDIS_802_11_NETWORK_INFRASTRUCTURE, *PNDIS_802_11_NETWORK_INFRASTRUCTURE;

// Add new authentication modes
typedef enum _NDIS_802_11_AUTHENTICATION_MODE
{
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
   Ndis802_11AuthModeMax           // Not a real mode, defined as upper bound
} NDIS_802_11_AUTHENTICATION_MODE, *PNDIS_802_11_AUTHENTICATION_MODE;

typedef UCHAR   NDIS_802_11_RATES[NDIS_802_11_LENGTH_RATES];        // Set of 8 data rates
typedef UCHAR   NDIS_802_11_RATES_EX[NDIS_802_11_LENGTH_RATES_EX];  // Set of 16 data rates

typedef struct PACKED _NDIS_802_11_SSID
{
    UINT   SsidLength;         // length of SSID field below, in bytes;
                                // this can be zero.
    UCHAR   Ssid[NDIS_802_11_LENGTH_SSID];           // SSID information field
} NDIS_802_11_SSID, *PNDIS_802_11_SSID;


typedef struct PACKED _NDIS_WLAN_BSSID
{
   ULONG                               Length;     // Length of this structure
   NDIS_802_11_MAC_ADDRESS             MacAddress; // BSSID
   UCHAR                               Reserved[2];
   NDIS_802_11_SSID                    Ssid;       // SSID
   ULONG                               Privacy;    // WEP encryption requirement
   NDIS_802_11_RSSI                    Rssi;       // receive signal strength in dBm
   NDIS_802_11_NETWORK_TYPE            NetworkTypeInUse;
   NDIS_802_11_CONFIGURATION           Configuration;
   NDIS_802_11_NETWORK_INFRASTRUCTURE  InfrastructureMode;
   NDIS_802_11_RATES                   SupportedRates;
} NDIS_WLAN_BSSID, *PNDIS_WLAN_BSSID;

typedef struct PACKED _NDIS_802_11_BSSID_LIST
{
   UINT           NumberOfItems;      // in list below, at least 1
   NDIS_WLAN_BSSID Bssid[1];
} NDIS_802_11_BSSID_LIST, *PNDIS_802_11_BSSID_LIST;

// Added Capabilities, IELength and IEs for each BSSID
typedef struct PACKED _NDIS_WLAN_BSSID_EX
{
    ULONG                               Length;             // Length of this structure
    NDIS_802_11_MAC_ADDRESS             MacAddress;         // BSSID
    UCHAR                               Reserved[2];
    NDIS_802_11_SSID                    Ssid;               // SSID
    UINT                                Privacy;            // WEP encryption requirement
    NDIS_802_11_RSSI                    Rssi;               // receive signal
                                                            // strength in dBm
    NDIS_802_11_NETWORK_TYPE            NetworkTypeInUse;
    NDIS_802_11_CONFIGURATION           Configuration;
    NDIS_802_11_NETWORK_INFRASTRUCTURE  InfrastructureMode;
    NDIS_802_11_RATES_EX                SupportedRates;
    ULONG                               IELength;
    UCHAR                               IEs[1];
} NDIS_WLAN_BSSID_EX, *PNDIS_WLAN_BSSID_EX;

typedef struct PACKED _NDIS_802_11_BSSID_LIST_EX
{
    UINT                   NumberOfItems;      // in list below, at least 1
    NDIS_WLAN_BSSID_EX      Bssid[1];
} NDIS_802_11_BSSID_LIST_EX, *PNDIS_802_11_BSSID_LIST_EX;

typedef struct PACKED _NDIS_802_11_FIXED_IEs
{
    UCHAR Timestamp[8];
    USHORT BeaconInterval;
    USHORT Capabilities;
} NDIS_802_11_FIXED_IEs, *PNDIS_802_11_FIXED_IEs;

typedef struct _NDIS_802_11_VARIABLE_IEs
{
    UCHAR ElementID;
    UCHAR Length;    // Number of bytes in data field
    UCHAR data[1];
} NDIS_802_11_VARIABLE_IEs, *PNDIS_802_11_VARIABLE_IEs;

typedef  ULONG   NDIS_802_11_FRAGMENTATION_THRESHOLD;

typedef  ULONG   NDIS_802_11_RTS_THRESHOLD;

typedef  ULONG   NDIS_802_11_ANTENNA;

typedef enum _NDIS_802_11_PRIVACY_FILTER
{
   Ndis802_11PrivFilterAcceptAll,
   Ndis802_11PrivFilter8021xWEP
} NDIS_802_11_PRIVACY_FILTER, *PNDIS_802_11_PRIVACY_FILTER;

// Added new encryption types
// Also aliased typedef to new name
typedef enum _NDIS_802_11_WEP_STATUS
{
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
    Ndis802_11Encryption4Enabled,	// TKIP or AES mix
    Ndis802_11Encryption4KeyAbsent,
    Ndis802_11GroupWEP40Enabled,
	Ndis802_11GroupWEP104Enabled,
} NDIS_802_11_WEP_STATUS, *PNDIS_802_11_WEP_STATUS,
  NDIS_802_11_ENCRYPTION_STATUS, *PNDIS_802_11_ENCRYPTION_STATUS;

typedef enum _NDIS_802_11_RELOAD_DEFAULTS
{
   Ndis802_11ReloadWEPKeys
} NDIS_802_11_RELOAD_DEFAULTS, *PNDIS_802_11_RELOAD_DEFAULTS;

#define NDIS_802_11_AI_REQFI_CAPABILITIES      1
#define NDIS_802_11_AI_REQFI_LISTENINTERVAL    2
#define NDIS_802_11_AI_REQFI_CURRENTAPADDRESS  4

#define NDIS_802_11_AI_RESFI_CAPABILITIES      1
#define NDIS_802_11_AI_RESFI_STATUSCODE        2
#define NDIS_802_11_AI_RESFI_ASSOCIATIONID     4

typedef struct _NDIS_802_11_AI_REQFI
{
    USHORT Capabilities;
    USHORT ListenInterval;
    NDIS_802_11_MAC_ADDRESS  CurrentAPAddress;
} NDIS_802_11_AI_REQFI, *PNDIS_802_11_AI_REQFI;

typedef struct _NDIS_802_11_AI_RESFI
{
    USHORT Capabilities;
    USHORT StatusCode;
    USHORT AssociationId;
} NDIS_802_11_AI_RESFI, *PNDIS_802_11_AI_RESFI;

typedef struct _NDIS_802_11_ASSOCIATION_INFORMATION
{
    ULONG                   Length;
    USHORT                  AvailableRequestFixedIEs;
    NDIS_802_11_AI_REQFI    RequestFixedIEs;
    ULONG                   RequestIELength;
    ULONG                   OffsetRequestIEs;
    USHORT                  AvailableResponseFixedIEs;
    NDIS_802_11_AI_RESFI    ResponseFixedIEs;
    ULONG                   ResponseIELength;
    ULONG                   OffsetResponseIEs;
} NDIS_802_11_ASSOCIATION_INFORMATION, *PNDIS_802_11_ASSOCIATION_INFORMATION;

typedef struct _NDIS_802_11_AUTHENTICATION_EVENT
{
    NDIS_802_11_STATUS_INDICATION       Status;
    NDIS_802_11_AUTHENTICATION_REQUEST  Request[1];
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

// 802.11 Media stream constraints, associated with OID_802_11_MEDIA_STREAM_MODE
typedef enum _NDIS_802_11_MEDIA_STREAM_MODE
{
    Ndis802_11MediaStreamOff,
    Ndis802_11MediaStreamOn,
} NDIS_802_11_MEDIA_STREAM_MODE, *PNDIS_802_11_MEDIA_STREAM_MODE;

// PMKID Structures
typedef UCHAR   NDIS_802_11_PMKID_VALUE[16];

#ifdef CONFIG_STA_SUPPORT
typedef struct _BSSID_INFO
{
    NDIS_802_11_MAC_ADDRESS BSSID;
    NDIS_802_11_PMKID_VALUE PMKID;
} BSSID_INFO, *PBSSID_INFO;

typedef struct _NDIS_802_11_PMKID
{
    UINT    Length;
    UINT    BSSIDInfoCount;
    BSSID_INFO BSSIDInfo[1];
} NDIS_802_11_PMKID, *PNDIS_802_11_PMKID;
#endif // CONFIG_STA_SUPPORT //


typedef struct _NDIS_802_11_AUTHENTICATION_ENCRYPTION
{
    NDIS_802_11_AUTHENTICATION_MODE AuthModeSupported;
    NDIS_802_11_ENCRYPTION_STATUS EncryptStatusSupported;
} NDIS_802_11_AUTHENTICATION_ENCRYPTION, *PNDIS_802_11_AUTHENTICATION_ENCRYPTION;

typedef struct _NDIS_802_11_CAPABILITY
{
     ULONG Length;
     ULONG Version;
     ULONG NoOfPMKIDs;
     ULONG NoOfAuthEncryptPairsSupported;
     NDIS_802_11_AUTHENTICATION_ENCRYPTION AuthenticationEncryptionSupported[1];
} NDIS_802_11_CAPABILITY, *PNDIS_802_11_CAPABILITY;

#if WIRELESS_EXT <= 11
#ifndef SIOCDEVPRIVATE
#define SIOCDEVPRIVATE                              0x8BE0
#endif
#define SIOCIWFIRSTPRIV								SIOCDEVPRIVATE
#endif

#ifdef CONFIG_STA_SUPPORT
#define RTPRIV_IOCTL_SET							(SIOCIWFIRSTPRIV + 0x02)

#ifdef DBG
#define RTPRIV_IOCTL_BBP                            (SIOCIWFIRSTPRIV + 0x03)
#define RTPRIV_IOCTL_MAC                            (SIOCIWFIRSTPRIV + 0x05)
#define RTPRIV_IOCTL_E2P                            (SIOCIWFIRSTPRIV + 0x07)
#endif

#define RTPRIV_IOCTL_STATISTICS                     (SIOCIWFIRSTPRIV + 0x09)
#define RTPRIV_IOCTL_ADD_PMKID_CACHE                (SIOCIWFIRSTPRIV + 0x0A)
#define RTPRIV_IOCTL_RADIUS_DATA                    (SIOCIWFIRSTPRIV + 0x0C)
#define RTPRIV_IOCTL_GSITESURVEY					(SIOCIWFIRSTPRIV + 0x0D)
#define RT_PRIV_IOCTL								(SIOCIWFIRSTPRIV + 0x0E) // Sync. with RT61 (for wpa_supplicant)
#define RTPRIV_IOCTL_GET_MAC_TABLE					(SIOCIWFIRSTPRIV + 0x0F)

#define RTPRIV_IOCTL_SHOW							(SIOCIWFIRSTPRIV + 0x11)
enum {
    SHOW_CONN_STATUS = 4,
    SHOW_DRVIER_VERION = 5,
    SHOW_BA_INFO = 6,
	SHOW_DESC_INFO = 7,
#ifdef RT2870
	SHOW_RXBULK_INFO = 8,
	SHOW_TXBULK_INFO = 9,
#endif // RT2870 //
    RAIO_OFF = 10,
    RAIO_ON = 11,
#ifdef QOS_DLS_SUPPORT
	SHOW_DLS_ENTRY_INFO = 19,
#endif // QOS_DLS_SUPPORT //
	SHOW_CFG_VALUE = 20,
	SHOW_ADHOC_ENTRY_INFO = 21,
};


#endif // CONFIG_STA_SUPPORT //

#ifdef SNMP_SUPPORT
//SNMP ieee 802dot11, kathy , 2008_0220
// dot11res(3)
#define RT_OID_802_11_MANUFACTUREROUI			0x0700
#define RT_OID_802_11_MANUFACTURERNAME			0x0701
#define RT_OID_802_11_RESOURCETYPEIDNAME		0x0702

// dot11smt(1)
#define RT_OID_802_11_PRIVACYOPTIONIMPLEMENTED	0x0703
#define RT_OID_802_11_POWERMANAGEMENTMODE		0x0704
#define OID_802_11_WEPDEFAULTKEYVALUE			0x0705 // read , write
#define OID_802_11_WEPDEFAULTKEYID				0x0706
#define RT_OID_802_11_WEPKEYMAPPINGLENGTH		0x0707
#define OID_802_11_SHORTRETRYLIMIT				0x0708
#define OID_802_11_LONGRETRYLIMIT				0x0709
#define RT_OID_802_11_PRODUCTID					0x0710
#define RT_OID_802_11_MANUFACTUREID				0x0711

// //dot11Phy(4)
#define OID_802_11_CURRENTCHANNEL				0x0712

//dot11mac
#define RT_OID_802_11_MAC_ADDRESS				0x0713
#endif // SNMP_SUPPORT //

#define OID_802_11_BUILD_CHANNEL_EX				0x0714
#define OID_802_11_GET_CH_LIST					0x0715
#define OID_802_11_GET_COUNTRY_CODE				0x0716
#define OID_802_11_GET_CHANNEL_GEOGRAPHY		0x0717

#ifdef LLTD_SUPPORT
// for consistency with RT61
#define RT_OID_GET_PHY_MODE                         0x761
#endif // LLTD_SUPPORT //

// MIMO Tx parameter, ShortGI, MCS, STBC, etc.  these are fields in TXWI. Don't change this definition!!!
typedef union  _HTTRANSMIT_SETTING {
#ifdef RT_BIG_ENDIAN
	struct	{
	USHORT		MODE:2;	// Use definition MODE_xxx.
//	USHORT		rsv:3;
	USHORT		TxBF:1;
	USHORT		rsv:2;
	USHORT		STBC:2;	//SPACE
	USHORT		ShortGI:1;
	USHORT		BW:1;	//channel bandwidth 20MHz or 40 MHz
	USHORT   	MCS:7;                 // MCS
	}	field;
#else
	struct	{
	USHORT   	MCS:7;                 // MCS
	USHORT		BW:1;	//channel bandwidth 20MHz or 40 MHz
	USHORT		ShortGI:1;
	USHORT		STBC:2;	//SPACE
//	USHORT		rsv:3;
	USHORT		rsv:2;
	USHORT		TxBF:1;
	USHORT		MODE:2;	// Use definition MODE_xxx.
	}	field;
#endif
	USHORT		word;
 } HTTRANSMIT_SETTING, *PHTTRANSMIT_SETTING;

typedef enum _RT_802_11_PREAMBLE {
    Rt802_11PreambleLong,
    Rt802_11PreambleShort,
    Rt802_11PreambleAuto
} RT_802_11_PREAMBLE, *PRT_802_11_PREAMBLE;

// Only for STA, need to sync with AP
typedef enum _RT_802_11_PHY_MODE {
	PHY_11BG_MIXED = 0,
	PHY_11B,
	PHY_11A,
	PHY_11ABG_MIXED,
	PHY_11G,
#ifdef DOT11_N_SUPPORT
	PHY_11ABGN_MIXED,	// both band   5
	PHY_11N_2_4G,		// 11n-only with 2.4G band   	6
	PHY_11GN_MIXED,	// 2.4G band      7
	PHY_11AN_MIXED,	// 5G  band       8
	PHY_11BGN_MIXED,	// if check 802.11b.      9
	PHY_11AGN_MIXED,	// if check 802.11b.      10
	PHY_11N_5G,			// 11n-only with 5G band		11
#endif // DOT11_N_SUPPORT //
} RT_802_11_PHY_MODE;

// put all proprietery for-query objects here to reduce # of Query_OID
typedef struct _RT_802_11_LINK_STATUS {
    ULONG   CurrTxRate;         // in units of 0.5Mbps
    ULONG   ChannelQuality;     // 0..100 %
    ULONG   TxByteCount;        // both ok and fail
    ULONG   RxByteCount;        // both ok and fail
    ULONG	CentralChannel;		// 40MHz central channel number
} RT_802_11_LINK_STATUS, *PRT_802_11_LINK_STATUS;

typedef struct _RT_802_11_EVENT_LOG {
    LARGE_INTEGER   SystemTime;  // timestammp via NdisGetCurrentSystemTime()
    UCHAR           Addr[MAC_ADDR_LENGTH];
    USHORT          Event;       // EVENT_xxx
} RT_802_11_EVENT_LOG, *PRT_802_11_EVENT_LOG;

typedef struct _RT_802_11_EVENT_TABLE {
    ULONG       Num;
    ULONG       Rsv;     // to align Log[] at LARGE_INEGER boundary
    RT_802_11_EVENT_LOG   Log[MAX_NUMBER_OF_EVENT];
} RT_802_11_EVENT_TABLE, PRT_802_11_EVENT_TABLE;

// MIMO Tx parameter, ShortGI, MCS, STBC, etc.  these are fields in TXWI. Don't change this definition!!!
typedef union  _MACHTTRANSMIT_SETTING {
	struct	{
	USHORT   	MCS:7;                 // MCS
	USHORT		BW:1;	//channel bandwidth 20MHz or 40 MHz
	USHORT		ShortGI:1;
	USHORT		STBC:2;	//SPACE
	USHORT		rsv:3;
	USHORT		MODE:2;	// Use definition MODE_xxx.
	}	field;
	USHORT		word;
 } MACHTTRANSMIT_SETTING, *PMACHTTRANSMIT_SETTING;

typedef struct _RT_802_11_MAC_ENTRY {
    UCHAR       Addr[MAC_ADDR_LENGTH];
    UCHAR       Aid;
    UCHAR       Psm;     // 0:PWR_ACTIVE, 1:PWR_SAVE
    UCHAR		MimoPs;  // 0:MMPS_STATIC, 1:MMPS_DYNAMIC, 3:MMPS_Enabled
    CHAR		AvgRssi0;
	CHAR		AvgRssi1;
	CHAR		AvgRssi2;
	UINT32		ConnectedTime;
    MACHTTRANSMIT_SETTING	TxRate;
} RT_802_11_MAC_ENTRY, *PRT_802_11_MAC_ENTRY;

typedef struct _RT_802_11_MAC_TABLE {
    ULONG       Num;
    RT_802_11_MAC_ENTRY Entry[MAX_NUMBER_OF_MAC];
} RT_802_11_MAC_TABLE, *PRT_802_11_MAC_TABLE;

// structure for query/set hardware register - MAC, BBP, RF register
typedef struct _RT_802_11_HARDWARE_REGISTER {
    ULONG   HardwareType;       // 0:MAC, 1:BBP, 2:RF register, 3:EEPROM
    ULONG   Offset;             // Q/S register offset addr
    ULONG   Data;               // R/W data buffer
} RT_802_11_HARDWARE_REGISTER, *PRT_802_11_HARDWARE_REGISTER;

// structure to tune BBP R17 "RX AGC VGC init"
//typedef struct _RT_802_11_RX_AGC_VGC_TUNING {
//    UCHAR   FalseCcaLowerThreshold;  // 0-255, def 10
//    UCHAR   FalseCcaUpperThreshold;  // 0-255, def 100
//    UCHAR   VgcDelta;                // R17 +-= VgcDelta whenever flase CCA over UpprThreshold
//                                     // or lower than LowerThresholdupper threshold
//    UCHAR   VgcUpperBound;           // max value of R17
//} RT_802_11_RX_AGC_VGC_TUNING, *PRT_802_11_RX_AGC_VGC_TUNING;

typedef struct _RT_802_11_AP_CONFIG {
    ULONG   EnableTxBurst;      // 0-disable, 1-enable
    ULONG   EnableTurboRate;    // 0-disable, 1-enable 72/100mbps turbo rate
    ULONG   IsolateInterStaTraffic;     // 0-disable, 1-enable isolation
    ULONG   HideSsid;           // 0-disable, 1-enable hiding
    ULONG   UseBGProtection;    // 0-AUTO, 1-always ON, 2-always OFF
    ULONG   UseShortSlotTime;   // 0-no use, 1-use 9-us short slot time
    ULONG   Rsv1;               // must be 0
    ULONG   SystemErrorBitmap;  // ignore upon SET, return system error upon QUERY
} RT_802_11_AP_CONFIG, *PRT_802_11_AP_CONFIG;

// structure to query/set STA_CONFIG
typedef struct _RT_802_11_STA_CONFIG {
    ULONG   EnableTxBurst;      // 0-disable, 1-enable
    ULONG   EnableTurboRate;    // 0-disable, 1-enable 72/100mbps turbo rate
    ULONG   UseBGProtection;    // 0-AUTO, 1-always ON, 2-always OFF
    ULONG   UseShortSlotTime;   // 0-no use, 1-use 9-us short slot time when applicable
    ULONG   AdhocMode; 			// 0-11b rates only (WIFI spec), 1 - b/g mixed, 2 - g only
    ULONG   HwRadioStatus;      // 0-OFF, 1-ON, default is 1, Read-Only
    ULONG   Rsv1;               // must be 0
    ULONG   SystemErrorBitmap;  // ignore upon SET, return system error upon QUERY
} RT_802_11_STA_CONFIG, *PRT_802_11_STA_CONFIG;

//
//  For OID Query or Set about BA structure
//
typedef	struct	_OID_BACAP_STRUC	{
		UCHAR		RxBAWinLimit;
		UCHAR		TxBAWinLimit;
		UCHAR		Policy;	// 0: DELAY_BA 1:IMMED_BA  (//BA Policy subfiled value in ADDBA frame)   2:BA-not use. other value invalid
		UCHAR		MpduDensity;	// 0: DELAY_BA 1:IMMED_BA  (//BA Policy subfiled value in ADDBA frame)   2:BA-not use. other value invalid
		UCHAR       	AmsduEnable;	//Enable AMSDU transmisstion
		UCHAR       	AmsduSize;	// 0:3839, 1:7935 bytes. UINT  MSDUSizeToBytes[]	= { 3839, 7935};
		UCHAR       	MMPSmode;	// MIMO power save more, 0:static, 1:dynamic, 2:rsv, 3:mimo enable
		BOOLEAN		AutoBA;	// Auto BA will automatically
} OID_BACAP_STRUC, *POID_BACAP_STRUC;

typedef struct _RT_802_11_ACL_ENTRY {
    UCHAR   Addr[MAC_ADDR_LENGTH];
    USHORT  Rsv;
} RT_802_11_ACL_ENTRY, *PRT_802_11_ACL_ENTRY;

typedef struct PACKED _RT_802_11_ACL {
    ULONG   Policy;             // 0-disable, 1-positive list, 2-negative list
    ULONG   Num;
    RT_802_11_ACL_ENTRY Entry[MAX_NUMBER_OF_ACL];
} RT_802_11_ACL, *PRT_802_11_ACL;

typedef struct _RT_802_11_WDS {
    ULONG						Num;
    NDIS_802_11_MAC_ADDRESS		Entry[24/*MAX_NUM_OF_WDS_LINK*/];
	ULONG						KeyLength;
	UCHAR						KeyMaterial[32];
} RT_802_11_WDS, *PRT_802_11_WDS;

typedef struct _RT_802_11_TX_RATES_ {
    UCHAR       SupRateLen;
    UCHAR       SupRate[MAX_LENGTH_OF_SUPPORT_RATES];
    UCHAR       ExtRateLen;
    UCHAR       ExtRate[MAX_LENGTH_OF_SUPPORT_RATES];
} RT_802_11_TX_RATES, *PRT_802_11_TX_RATES;


// Definition of extra information code
#define	GENERAL_LINK_UP			0x0			// Link is Up
#define	GENERAL_LINK_DOWN		0x1			// Link is Down
#define	HW_RADIO_OFF			0x2			// Hardware radio off
#define	SW_RADIO_OFF			0x3			// Software radio off
#define	AUTH_FAIL				0x4			// Open authentication fail
#define	AUTH_FAIL_KEYS			0x5			// Shared authentication fail
#define	ASSOC_FAIL				0x6			// Association failed
#define	EAP_MIC_FAILURE			0x7			// Deauthencation because MIC failure
#define	EAP_4WAY_TIMEOUT		0x8			// Deauthencation on 4-way handshake timeout
#define	EAP_GROUP_KEY_TIMEOUT	0x9			// Deauthencation on group key handshake timeout
#define	EAP_SUCCESS				0xa			// EAP succeed
#define	DETECT_RADAR_SIGNAL		0xb         // Radar signal occur in current channel
#define EXTRA_INFO_MAX			0xb			// Indicate Last OID

#define EXTRA_INFO_CLEAR		0xffffffff

// This is OID setting structure. So only GF or MM as Mode. This is valid when our wirelss mode has 802.11n in use.
typedef struct {
	RT_802_11_PHY_MODE		PhyMode; 	//
	UCHAR		TransmitNo;
	UCHAR		HtMode; 	//HTMODE_GF or HTMODE_MM
	UCHAR		ExtOffset;	//extension channel above or below
	UCHAR		MCS;
	UCHAR   	BW;
	UCHAR		STBC;
	UCHAR		SHORTGI;
	UCHAR		rsv;
} OID_SET_HT_PHYMODE, *POID_SET_HT_PHYMODE;

#ifdef LLTD_SUPPORT
typedef struct _RT_LLTD_ASSOICATION_ENTRY {
    UCHAR           Addr[ETH_LENGTH_OF_ADDRESS];
    unsigned short  MOR;        // maximum operational rate
    UCHAR           phyMode;
} RT_LLTD_ASSOICATION_ENTRY, *PRT_LLTD_ASSOICATION_ENTRY;

typedef struct _RT_LLTD_ASSOICATION_TABLE {
    unsigned int                Num;
    RT_LLTD_ASSOICATION_ENTRY   Entry[MAX_NUMBER_OF_MAC];
} RT_LLTD_ASSOICATION_TABLE, *PRT_LLTD_ASSOICATION_TABLE;
#endif // LLTD_SUPPORT //

#ifdef CONFIG_STA_SUPPORT
#ifdef QOS_DLS_SUPPORT
//rt2860, kathy 2007-0118
// structure for DLS
typedef struct _RT_802_11_DLS_UI {
	USHORT						TimeOut;		// unit: second , set by UI
	USHORT						CountDownTimer;	// unit: second , used by driver only
	NDIS_802_11_MAC_ADDRESS		MacAddr;		// set by UI
	UCHAR						Status;			// 0: none , 1: wait STAkey, 2: finish DLS setup , set by driver only
	BOOLEAN						Valid;			// 1: valid , 0: invalid , set by UI, use to setup or tear down DLS link
} RT_802_11_DLS_UI, *PRT_802_11_DLS_UI;

typedef struct _RT_802_11_DLS_INFO {
	RT_802_11_DLS_UI	Entry[MAX_NUMBER_OF_DLS_ENTRY];
	UCHAR				num;
} RT_802_11_DLS_INFO, *PRT_802_11_DLS_INFO;

typedef enum _RT_802_11_DLS_MODE {
    DLS_NONE,
    DLS_WAIT_KEY,
    DLS_FINISH
} RT_802_11_DLS_MODE;
#endif // QOS_DLS_SUPPORT //

#ifdef WPA_SUPPLICANT_SUPPORT
#ifndef NATIVE_WPA_SUPPLICANT_SUPPORT
#define	RT_ASSOC_EVENT_FLAG                         0x0101
#define	RT_DISASSOC_EVENT_FLAG                      0x0102
#define	RT_REQIE_EVENT_FLAG                         0x0103
#define	RT_RESPIE_EVENT_FLAG                        0x0104
#define	RT_ASSOCINFO_EVENT_FLAG                     0x0105
#define RT_PMKIDCAND_FLAG                           0x0106
#define RT_INTERFACE_DOWN                           0x0107
#define RT_INTERFACE_UP                             0x0108
#endif // NATIVE_WPA_SUPPLICANT_SUPPORT //
#endif // WPA_SUPPLICANT_SUPPORT //
#endif // CONFIG_STA_SUPPORT //


#define MAX_CUSTOM_LEN 128

#ifdef CONFIG_STA_SUPPORT
typedef enum _RT_802_11_D_CLIENT_MODE
{
   Rt802_11_D_None,
   Rt802_11_D_Flexible,
   Rt802_11_D_Strict,
} RT_802_11_D_CLIENT_MODE, *PRT_802_11_D_CLIENT_MODE;
#endif // CONFIG_STA_SUPPORT //

typedef struct _RT_CHANNEL_LIST_INFO
{
	UCHAR ChannelList[MAX_NUM_OF_CHS];   // list all supported channels for site survey
	UCHAR ChannelListNum; // number of channel in ChannelList[]
} RT_CHANNEL_LIST_INFO, *PRT_CHANNEL_LIST_INFO;

// WSC configured credential
typedef	struct	_WSC_CREDENTIAL
{
	NDIS_802_11_SSID	SSID;				// mandatory
	USHORT				AuthType;			// mandatory, 1: open, 2: wpa-psk, 4: shared, 8:wpa, 0x10: wpa2, 0x20: wpa2-psk
	USHORT				EncrType;			// mandatory, 1: none, 2: wep, 4: tkip, 8: aes
	UCHAR				Key[64];			// mandatory, Maximum 64 byte
	USHORT				KeyLength;
	UCHAR				MacAddr[6];			// mandatory, AP MAC address
	UCHAR				KeyIndex;			// optional, default is 1
	UCHAR				Rsvd[3];			// Make alignment
}	WSC_CREDENTIAL, *PWSC_CREDENTIAL;

// WSC configured profiles
typedef	struct	_WSC_PROFILE
{
	UINT			ProfileCnt;
	WSC_CREDENTIAL	Profile[8];				// Support up to 8 profiles
}	WSC_PROFILE, *PWSC_PROFILE;


#endif // _OID_H_

