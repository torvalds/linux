/*
 * WPA Supplicant - driver_ralink exported functions
 * Copyright (c) 2003-2005, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2007, Snowpin Lee <snowpin_lee@ralinktech.com.tw>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

// Ralink defined OIDs
#if WIRELESS_EXT <= 11
#ifndef SIOCDEVPRIVATE
#define SIOCDEVPRIVATE                              0x8BE0
#endif
#define SIOCIWFIRSTPRIV								SIOCDEVPRIVATE
#endif

#define RT_PRIV_IOCTL								(SIOCIWFIRSTPRIV + 0x0E)  
#define RTPRIV_IOCTL_SET							(SIOCIWFIRSTPRIV + 0x02)

// IEEE 802.11 OIDs  &  Ralink defined OIDs  ******

// (RaConfig Set/QueryInform) ==>
#define OID_GET_SET_TOGGLE							0x8000

#define OID_802_11_ADD_WEP                          0x0112
#define OID_802_11_REMOVE_WEP                       0x0113
#define OID_802_11_DISASSOCIATE                     0x0114
#define OID_802_11_PRIVACY_FILTER                   0x0118
#define OID_802_11_ASSOCIATION_INFORMATION          0x011E
#define OID_802_11_BSSID_LIST_SCAN                  0x0508
#define OID_802_11_SSID                             0x0509
#define OID_802_11_BSSID                            0x050A
#define OID_802_11_WEP_STATUS                       0x0510
#define OID_802_11_AUTHENTICATION_MODE              0x0511
#define OID_802_11_INFRASTRUCTURE_MODE              0x0512
#define OID_802_11_TX_POWER_LEVEL                   0x0517
#define OID_802_11_REMOVE_KEY                       0x0519
#define OID_802_11_ADD_KEY                          0x0520
#define OID_802_11_DEAUTHENTICATION                 0x0526
#define OID_802_11_DROP_UNENCRYPTED                 0x0527
#define OID_802_11_BSSID_LIST                       0x0609
#define OID_802_3_CURRENT_ADDRESS                   0x060A
#define OID_SET_COUNTERMEASURES                     0x0616
#define OID_802_11_SET_IEEE8021X                    0x0617	// For IEEE8021x mode 
#define OID_802_11_SET_IEEE8021X_REQUIRE_KEY        0x0618  // For DynamicWEP in IEEE802.1x mode
#define OID_802_11_PMKID                            0x0620
#define RT_OID_WPA_SUPPLICANT_SUPPORT               0x0621  // for trigger driver enable/disable wpa_supplicant support 
#define RT_OID_WE_VERSION_COMPILED                  0x0622
#define RT_OID_NEW_DRIVER                           0x0623
#define RT_OID_WPS_PROBE_REQ_IE						0x0625

#define PACKED  __attribute__ ((packed))

//wpa_supplicant event flags
#define	RT_ASSOC_EVENT_FLAG                         0x0101
#define	RT_DISASSOC_EVENT_FLAG                      0x0102
#define	RT_REQIE_EVENT_FLAG                         0x0103
#define	RT_RESPIE_EVENT_FLAG                        0x0104
#define	RT_ASSOCINFO_EVENT_FLAG                     0x0105
#define RT_PMKIDCAND_FLAG                           0x0106
#define RT_INTERFACE_DOWN                           0x0107
#define RT_INTERFACE_UP                           	0x0108

//
// IEEE 802.11 Structures and definitions
//
// new types for Media Specific Indications

#ifndef ULONG
#define CHAR            char
#define INT             int
#define SHORT           int
#define UINT            u32
#undef  ULONG           
//#define ULONG           u32
#define ULONG           unsigned long /* 32-bit in 32-bit CPU or 64-bit in 64-bit CPU */
#define USHORT          unsigned short
#define UCHAR           unsigned char

#define uint32		u32
#define uint8		u8


#define BOOLEAN         u8
//#define LARGE_INTEGER s64
#define VOID            void
#define LONG            long
#define LONGLONG        s64
#define ULONGLONG       u64
typedef VOID            *PVOID;
typedef CHAR            *PCHAR;
typedef UCHAR           *PUCHAR;
typedef USHORT          *PUSHORT;
typedef LONG            *PLONG;
typedef ULONG           *PULONG;

typedef union _LARGE_INTEGER {
    struct {
        ULONG LowPart;
        LONG HighPart;
    }vv;
    struct {
        ULONG LowPart;
        LONG HighPart;
    } u;
    s64 QuadPart;
} LARGE_INTEGER;

#endif

#define NDIS_802_11_LENGTH_SSID         32
#define NDIS_802_11_LENGTH_RATES        8
#define NDIS_802_11_LENGTH_RATES_EX     16
#define MAX_LEN_OF_SSID                 32
#define MAC_ADDR_LEN                    6

typedef UCHAR   NDIS_802_11_MAC_ADDRESS[6];

// mask for authentication/integrity fields
#define NDIS_802_11_AUTH_REQUEST_AUTH_FIELDS        0x0f

#define NDIS_802_11_AUTH_REQUEST_REAUTH             0x01
#define NDIS_802_11_AUTH_REQUEST_KEYUPDATE          0x02
#define NDIS_802_11_AUTH_REQUEST_PAIRWISE_ERROR     0x06
#define NDIS_802_11_AUTH_REQUEST_GROUP_ERROR        0x0E

// Added new types for OFDM 5G and 2.4G
typedef enum _NDIS_802_11_NETWORK_TYPE
{
    Ndis802_11FH, 
    Ndis802_11DS, 
    Ndis802_11OFDM5,
    Ndis802_11OFDM24,
    Ndis802_11Automode,
    Ndis802_11NetworkTypeMax    // not a real type, defined as an upper bound
} NDIS_802_11_NETWORK_TYPE, *PNDIS_802_11_NETWORK_TYPE;

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

typedef  ULONG  NDIS_802_11_KEY_INDEX;
typedef ULONGLONG   NDIS_802_11_KEY_RSC;

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

typedef struct _NDIS_802_11_REMOVE_KEY
{
    UINT                   Length;        // Length of this structure
    UINT                   KeyIndex;           
    NDIS_802_11_MAC_ADDRESS BSSID;      
} NDIS_802_11_REMOVE_KEY, *PNDIS_802_11_REMOVE_KEY;

typedef struct PACKED _NDIS_802_11_WEP
{
   UINT     Length;        // Length of this structure
   UINT           KeyIndex;           // 0 is the per-client key, 1-N are the
                                        // global keys
   UINT     KeyLength;     // length of key in bytes
   UCHAR     KeyMaterial[1];// variable length depending on above field
} NDIS_802_11_WEP, *PNDIS_802_11_WEP;


typedef enum _NDIS_802_11_NETWORK_INFRASTRUCTURE
{
   Ndis802_11IBSS,
   Ndis802_11Infrastructure,
   Ndis802_11AutoUnknown,
   Ndis802_11InfrastructureMax     // Not a real value, defined as upper bound
} NDIS_802_11_NETWORK_INFRASTRUCTURE, *PNDIS_802_11_NETWORK_INFRASTRUCTURE;

// PMKID Structures
typedef UCHAR   NDIS_802_11_PMKID_VALUE[16];

typedef struct _BSSID_INFO
{
	NDIS_802_11_MAC_ADDRESS BSSID;
	NDIS_802_11_PMKID_VALUE PMKID;
} BSSID_INFO, *PBSSID_INFO;

typedef struct _NDIS_802_11_PMKID
{
	ULONG Length;
	ULONG BSSIDInfoCount;
	BSSID_INFO BSSIDInfo[1];
} NDIS_802_11_PMKID, *PNDIS_802_11_PMKID;

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
   Ndis802_11AuthModeMax           // Not a real mode, defined as upper bound
} NDIS_802_11_AUTHENTICATION_MODE, *PNDIS_802_11_AUTHENTICATION_MODE;

typedef UCHAR  NDIS_802_11_RATES[NDIS_802_11_LENGTH_RATES];        // Set of 8 data rates
typedef UCHAR  NDIS_802_11_RATES_EX[NDIS_802_11_LENGTH_RATES_EX];  // Set of 16 data rates

typedef struct PACKED _NDIS_802_11_SSID 
{
    INT   SsidLength;         // length of SSID field below, in bytes;
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
    NDIS_802_11_RSSI                    Rssi;               // receive signal
                                                            // strength in dBm
   NDIS_802_11_NETWORK_TYPE            NetworkTypeInUse;
   NDIS_802_11_CONFIGURATION           Configuration;
   NDIS_802_11_NETWORK_INFRASTRUCTURE  InfrastructureMode;
   NDIS_802_11_RATES                   SupportedRates;
} NDIS_WLAN_BSSID, *PNDIS_WLAN_BSSID;

typedef struct PACKED _NDIS_802_11_BSSID_LIST
{
   UINT             NumberOfItems;      // in list below, at least 1
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
   Ndis802_11Encryption3KeyAbsent
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

struct ndis_pmkid_entry {
	struct ndis_pmkid_entry *next;
	u8 bssid[ETH_ALEN];
	u8 pmkid[16];
};

typedef struct _MLME_DEAUTH_REQ_STRUCT {
    UCHAR        Addr[MAC_ADDR_LEN];
    USHORT       Reason;
} MLME_DEAUTH_REQ_STRUCT, *PMLME_DEAUTH_REQ_STRUCT;
