/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/include/wlan_oid.h#4 $
*/

/*! \file   "wlan_oid.h"
    \brief This file contains the declairation file of the WLAN OID processing routines
           of Windows driver for MediaTek Inc. 802.11 Wireless LAN Adapters.
*/

/*******************************************************************************
* Copyright (c) 2007 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
********************************************************************************
*/

/*******************************************************************************
* LEGAL DISCLAIMER
*
* BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND
* AGREES THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK
* SOFTWARE") RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
* PROVIDED TO BUYER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY
* DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT
* LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
* PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE
* ANY WARRANTY WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY
* WHICH MAY BE USED BY, INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK
* SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY
* WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE
* FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION OR TO
* CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
* BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
* LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL
* BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT
* ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
* BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
* WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT
* OF LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING
* THEREOF AND RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN
* FRANCISCO, CA, UNDER THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE
* (ICC).
********************************************************************************
*/

/*
** $Log: wlan_oid.h $
 *
 * 01 19 2012 yuche.tsai
 * NULL
 * Fix RX filter issue for P2P network.
 *
 * 01 17 2012 yuche.tsai
 * NULL
 * Update mgmt frame filter setting.
 * Please also update FW 2.1
 *
 * 01 13 2012 yuche.tsai
 * NULL
 * WiFi Hot Spot Tethering for ICS ALPHA testing version.
 *
 * 01 09 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * Check in the Add Tx power Cmd to driver.
 *
 * 10 19 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * Branch 2.1
 * Davinci Maintrunk Label: MT6620_WIFI_DRIVER_FW_TRUNK_MT6620E5_111019_0926.
 *
 * 03 22 2011 george.huang
 * [WCXRP00000504] [MT6620 Wi-Fi][FW] Support Sigma CAPI for power saving related command
 * link with supplicant commands
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 03 17 2011 chinglan.wang
 * [WCXRP00000570] [MT6620 Wi-Fi][Driver] Add Wi-Fi Protected Setup v2.0 feature
 * .
 *
 * 03 02 2011 george.huang
 * [WCXRP00000504] [MT6620 Wi-Fi][FW] Support Sigma CAPI for power saving related command
 * Support UAPSD/OppPS/NoA parameter setting
 *
 * 01 20 2011 eddie.chen
 * [WCXRP00000374] [MT6620 Wi-Fi][DRV] SW debug control
 * Add Oid for sw control debug command
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000238] MT6620 Wi-Fi][Driver][FW] Support regulation domain setting from NVRAM and supplicant
 * 1. Country code is from NVRAM or supplicant
 * 2. Change band definition in CMD/EVENT.
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000086] [MT6620 Wi-Fi][Driver] The mac address is all zero at android
 * complete implementation of Android NVRAM access
 *
 * 10 08 2010 cp.wu
 * [WCXRP00000084] [MT6620 Wi-Fi][Driver][FW] Add fixed rate support for distance test
 * adding fixed rate support for distance test. (from registry setting)
 *
 * 09 23 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * add skeleton for NVRAM integration
 *
 * 09 08 2010 cp.wu
 * NULL
 * use static memory pool for storing IEs of scanning result.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 29 2010 yuche.tsai
 * NULL
 * Finish SLT TX/RX & Rate Changing Support.
 *
 * 08 04 2010 cp.wu
 * NULL
 * revert changelist #15371, efuse read/write access will be done by RF test approach
 *
 * 08 04 2010 cp.wu
 * NULL
 * add OID definitions for EFUSE read/write access.
 *
 * 08 04 2010 yarco.yang
 * NULL
 * Add TX_AMPDU and ADDBA_REJECT command
 *
 * 08 02 2010 george.huang
 * NULL
 * add WMM-PS test related OID/ CMD handlers
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 22 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) add command warpper for STA-REC/BSS-INFO sync.
 * 2) enhance command packet sending procedure for non-oid part
 * 3) add command packet definitions for STA-REC/BSS-INFO sync.
 *
 * 06 18 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * migration from MT6620 firmware.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * merge wlan_def.h.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * merge wifi_var.h, precomp.h, cnm_timer.h (data type only)
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 06 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * move timer callback to glue layer.
 *
 * 05 20 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) integrate OID_GEN_NETWORK_LAYER_ADDRESSES with CMD_ID_SET_IP_ADDRESS
 * 2) buffer statistics data for 2 seconds
 * 3) use default value for adhoc parameters instead of 0
 *
 * 05 18 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement Wakeup-on-LAN except firmware integration part
 *
 * 05 17 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * MT6620 is not supporting NDIS_PACKET_TYPE_PROMISCUOUS.
 *

 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * 1) add timeout handler mechanism for pending command packets
 * 2) add p2p add/removal key
 *
 * 05 13 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add NULL OID implementation for WOL-related OIDs.
 *
 * 04 22 2010 cp.wu
 * [WPD00003830]add OID_802_11_PRIVACY_FILTER support
 * enable RX filter OID
 *
 * 04 14 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * information buffer for query oid/ioctl is now buffered in prCmdInfo
 *  *  *  *  * instead of glue-layer variable to improve multiple oid/ioctl capability
 *
 * 03 31 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * modify the wapi related code for new driver's design.
 *
 * 03 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * indicate media stream mode after set is done
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
 *
 * 03 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement custom OID: EEPROM read/write access
 *
 * 02 09 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. Permanent and current MAC address are now retrieved by CMD/EVENT packets instead of hard-coded address
 *  *  *  *  *  * 2. follow MSDN defined behavior when associates to another AP
 *  *  *  *  *  * 3. for firmware download, packet size could be up to 2048 bytes
 *
 * 01 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) implement timeout mechanism when OID is pending for longer than 1 second
 *  *  * 2) allow OID_802_11_CONFIGURATION to be executed when RF test mode is turned on
 *
 * 01 27 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * .
 *
 * 01 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement following 802.11 OIDs:
 *  *  *  * OID_802_11_RSSI,
 *  *  *  * OID_802_11_RSSI_TRIGGER,
 *  *  *  * OID_802_11_STATISTICS,
 *  *  *  * OID_802_11_DISASSOCIATE,
 *  *  *  * OID_802_11_POWER_MODE
 *
 * 01 21 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement OID_802_11_MEDIA_STREAM_MODE
 *
 * 01 21 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * implement OID_802_11_SUPPORTED_RATES / OID_802_11_DESIRED_RATES
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-12-08 11:38:11 GMT mtk02752
**  add declares for RF test related APIs
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-11-24 22:41:53 GMT mtk02752
**  remove u4SysTime, MSDN 10-second will be implemented in FW side
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-11-23 20:30:13 GMT mtk02752
**  add u4SysTime field in PARAM_BSSID_EX_T
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-11-12 19:48:35 GMT mtk02752
**  allow upper layer to set a packet filter with PROMISCUOUS mode
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:12:12 GMT mtk01426
**  Init for develop
**
*/

#ifndef _WLAN_OID_H
#define _WLAN_OID_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

#define PARAM_MAX_LEN_SSID                      32

#define PARAM_MAC_ADDR_LEN                      6

#define ETHERNET_HEADER_SZ                      14
#define ETHERNET_MIN_PKT_SZ                     60
#define ETHERNET_MAX_PKT_SZ                     1514

#define PARAM_MAX_LEN_RATES                     8
#define PARAM_MAX_LEN_RATES_EX                  16

#define PARAM_AUTH_REQUEST_REAUTH               0x01
#define PARAM_AUTH_REQUEST_KEYUPDATE            0x02
#define PARAM_AUTH_REQUEST_PAIRWISE_ERROR       0x06
#define PARAM_AUTH_REQUEST_GROUP_ERROR          0x0E

#define PARAM_EEPROM_READ_METHOD_READ           1
#define PARAM_EEPROM_READ_METHOD_GETSIZE        0

#define PARAM_WHQL_RSSI_MAX_DBM                 (-10)
#define PARAM_WHQL_RSSI_MIN_DBM                 (-200)

#define PARAM_DEVICE_WAKE_UP_ENABLE                     0x00000001
#define PARAM_DEVICE_WAKE_ON_PATTERN_MATCH_ENABLE       0x00000002
#define PARAM_DEVICE_WAKE_ON_MAGIC_PACKET_ENABLE        0x00000004

#define PARAM_WAKE_UP_MAGIC_PACKET              0x00000001
#define PARAM_WAKE_UP_PATTERN_MATCH             0x00000002
#define PARAM_WAKE_UP_LINK_CHANGE               0x00000004


/* Packet filter bit definitioin (UINT_32 bit-wise definition) */
#define PARAM_PACKET_FILTER_DIRECTED            0x00000001
#define PARAM_PACKET_FILTER_MULTICAST           0x00000002
#define PARAM_PACKET_FILTER_ALL_MULTICAST       0x00000004
#define PARAM_PACKET_FILTER_BROADCAST           0x00000008
#define PARAM_PACKET_FILTER_PROMISCUOUS         0x00000020
#define PARAM_PACKET_FILTER_ALL_LOCAL           0x00000080
#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
#define PARAM_PACKET_FILTER_P2P_MASK             0xC0000000
#define PARAM_PACKET_FILTER_PROBE_REQ           0x80000000
#define PARAM_PACKET_FILTER_ACTION_FRAME      0x40000000
#endif

#if CFG_SLT_SUPPORT
#define PARAM_PACKET_FILTER_SUPPORTED   (PARAM_PACKET_FILTER_DIRECTED | \
                                         PARAM_PACKET_FILTER_MULTICAST | \
                                         PARAM_PACKET_FILTER_BROADCAST | \
                                         PARAM_PACKET_FILTER_ALL_MULTICAST)
#else
#define PARAM_PACKET_FILTER_SUPPORTED   (PARAM_PACKET_FILTER_DIRECTED | \
                                         PARAM_PACKET_FILTER_MULTICAST | \
                                         PARAM_PACKET_FILTER_BROADCAST)
#endif


#define BT_PROFILE_PARAM_LEN        8
/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
/*----------------------------------------------------------------------------*/
/* Parameters of User Configuration which match to NDIS5.1                    */
/*----------------------------------------------------------------------------*/
/* NDIS_802_11_AUTHENTICATION_MODE */
typedef enum _ENUM_PARAM_AUTH_MODE_T
{
    AUTH_MODE_OPEN,                     /*!< Open system */
    AUTH_MODE_SHARED,                   /*!< Shared key */
    AUTH_MODE_AUTO_SWITCH,              /*!< Either open system or shared key */
    AUTH_MODE_WPA,
    AUTH_MODE_WPA_PSK,
    AUTH_MODE_WPA_NONE,                 /*!< For Ad hoc */
    AUTH_MODE_WPA2,
    AUTH_MODE_WPA2_PSK,
    AUTH_MODE_NUM                       /*!< Upper bound, not real case */
} ENUM_PARAM_AUTH_MODE_T, *P_ENUM_PARAM_AUTH_MODE_T;

/* NDIS_802_11_ENCRYPTION_STATUS *//* Encryption types */
typedef enum _ENUM_WEP_STATUS_T
{
    ENUM_WEP_ENABLED,
    ENUM_ENCRYPTION1_ENABLED = ENUM_WEP_ENABLED,
    ENUM_WEP_DISABLED,
    ENUM_ENCRYPTION_DISABLED = ENUM_WEP_DISABLED,
    ENUM_WEP_KEY_ABSENT,
    ENUM_ENCRYPTION1_KEY_ABSENT = ENUM_WEP_KEY_ABSENT,
    ENUM_WEP_NOT_SUPPORTED,
    ENUM_ENCRYPTION_NOT_SUPPORTED = ENUM_WEP_NOT_SUPPORTED,
    ENUM_ENCRYPTION2_ENABLED,
    ENUM_ENCRYPTION2_KEY_ABSENT,
    ENUM_ENCRYPTION3_ENABLED,
    ENUM_ENCRYPTION3_KEY_ABSENT
} ENUM_PARAM_ENCRYPTION_STATUS_T, *P_ENUM_PARAM_ENCRYPTION_STATUS_T;



typedef UINT_8          PARAM_MAC_ADDRESS[PARAM_MAC_ADDR_LEN];

typedef UINT_32         PARAM_KEY_INDEX;
typedef UINT_64         PARAM_KEY_RSC;
typedef INT_32          PARAM_RSSI;

typedef UINT_32         PARAM_FRAGMENTATION_THRESHOLD;
typedef UINT_32         PARAM_RTS_THRESHOLD;

typedef UINT_8          PARAM_RATES[PARAM_MAX_LEN_RATES];
typedef UINT_8          PARAM_RATES_EX[PARAM_MAX_LEN_RATES_EX];

typedef enum _ENUM_PARAM_PHY_TYPE_T {
    PHY_TYPE_802_11ABG = 0,             /*!< Can associated with 802.11abg AP, Scan dual band. */
    PHY_TYPE_802_11BG,                  /*!< Can associated with 802_11bg AP, Scan single band and not report 802_11a BSSs. */
    PHY_TYPE_802_11G,                   /*!< Can associated with 802_11g only AP, Scan single band and not report 802_11ab BSSs. */
    PHY_TYPE_802_11A,                   /*!< Can associated with 802_11a only AP, Scan single band and not report 802_11bg BSSs. */
    PHY_TYPE_802_11B,                   /*!< Can associated with 802_11b only AP, Scan single band and not report 802_11ag BSSs. */
    PHY_TYPE_NUM // 5
} ENUM_PARAM_PHY_TYPE_T, *P_ENUM_PARAM_PHY_TYPE_T;

typedef enum _ENUM_PARAM_OP_MODE_T {
    NET_TYPE_IBSS = 0,                  /*!< Try to merge/establish an AdHoc, do periodic SCAN for merging. */
    NET_TYPE_INFRA,                     /*!< Try to join an Infrastructure, do periodic SCAN for joining. */
    NET_TYPE_AUTO_SWITCH,               /*!< Try to join an Infrastructure, if fail then try to merge or
                                         establish an AdHoc, do periodic SCAN for joining or merging. */
    NET_TYPE_DEDICATED_IBSS,            /*!< Try to merge an AdHoc first, if fail then establish AdHoc permanently, no more SCAN. */
    NET_TYPE_NUM // 4
} ENUM_PARAM_OP_MODE_T, *P_ENUM_PARAM_OP_MODE_T;

typedef struct _PARAM_SSID_T {
    UINT_32  u4SsidLen;      /*!< SSID length in bytes. Zero length is broadcast(any) SSID */
    UINT_8   aucSsid[PARAM_MAX_LEN_SSID];
} PARAM_SSID_T, *P_PARAM_SSID_T;

/* This is enum defined for user to select an AdHoc Mode */
typedef enum _ENUM_PARAM_AD_HOC_MODE_T {
    AD_HOC_MODE_11B = 0,                /*!< Create 11b IBSS if we support 802.11abg/802.11bg. */
    AD_HOC_MODE_MIXED_11BG,             /*!< Create 11bg mixed IBSS if we support 802.11abg/802.11bg/802.11g. */
    AD_HOC_MODE_11G,                    /*!< Create 11g only IBSS if we support 802.11abg/802.11bg/802.11g. */
    AD_HOC_MODE_11A,                    /*!< Create 11a only IBSS if we support 802.11abg. */
    AD_HOC_MODE_NUM // 4
} ENUM_PARAM_AD_HOC_MODE_T, *P_ENUM_PARAM_AD_HOC_MODE_T;


typedef enum _ENUM_PARAM_MEDIA_STATE_T {
    PARAM_MEDIA_STATE_CONNECTED,
    PARAM_MEDIA_STATE_DISCONNECTED,
    PARAM_MEDIA_STATE_TO_BE_INDICATED // for following MSDN re-association behavior
} ENUM_PARAM_MEDIA_STATE_T, *P_ENUM_PARAM_MEDIA_STATE_T;


typedef enum _ENUM_PARAM_NETWORK_TYPE_T {
    PARAM_NETWORK_TYPE_FH,
    PARAM_NETWORK_TYPE_DS,
    PARAM_NETWORK_TYPE_OFDM5,
    PARAM_NETWORK_TYPE_OFDM24,
    PARAM_NETWORK_TYPE_AUTOMODE,
    PARAM_NETWORK_TYPE_NUM                    /*!< Upper bound, not real case */
} ENUM_PARAM_NETWORK_TYPE_T, *P_ENUM_PARAM_NETWORK_TYPE_T;


typedef struct _PARAM_NETWORK_TYPE_LIST {
    UINT_32                     NumberOfItems;      /*!< At least 1 */
    ENUM_PARAM_NETWORK_TYPE_T   eNetworkType [1];
} PARAM_NETWORK_TYPE_LIST, *PPARAM_NETWORK_TYPE_LIST;

typedef enum _ENUM_PARAM_PRIVACY_FILTER_T
{
    PRIVACY_FILTER_ACCEPT_ALL,
    PRIVACY_FILTER_8021xWEP,
    PRIVACY_FILTER_NUM
} ENUM_PARAM_PRIVACY_FILTER_T, *P_ENUM_PARAM_PRIVACY_FILTER_T;

typedef enum _ENUM_RELOAD_DEFAULTS
{
    ENUM_RELOAD_WEP_KEYS
} PARAM_RELOAD_DEFAULTS, *P_PARAM_RELOAD_DEFAULTS;

typedef struct _PARAM_PM_PACKET_PATTERN
{
    UINT_32 Priority;                   // Importance of the given pattern.
    UINT_32 Reserved;                   // Context information for transports.
    UINT_32 MaskSize;                   // Size in bytes of the pattern mask.
    UINT_32 PatternOffset;              // Offset from beginning of this
                                        // structure to the pattern bytes.
    UINT_32 PatternSize;                // Size in bytes of the pattern.
    UINT_32 PatternFlags;               // Flags (TBD).
} PARAM_PM_PACKET_PATTERN, *P_PARAM_PM_PACKET_PATTERN;

/*--------------------------------------------------------------*/
/*! \brief Struct definition to indicate specific event.                */
/*--------------------------------------------------------------*/
typedef enum _ENUM_STATUS_TYPE_T
{
    ENUM_STATUS_TYPE_AUTHENTICATION,
    ENUM_STATUS_TYPE_MEDIA_STREAM_MODE,
    ENUM_STATUS_TYPE_CANDIDATE_LIST,
    ENUM_STATUS_TYPE_NUM              /*!< Upper bound, not real case */
} ENUM_STATUS_TYPE_T, *P_ENUM_STATUS_TYPE_T;


typedef struct _PARAM_802_11_CONFIG_FH_T {
    UINT_32                  u4Length;         /*!< Length of structure */
    UINT_32                  u4HopPattern;     /*!< Defined as 802.11 */
    UINT_32                  u4HopSet;         /*!< to one if non-802.11 */
    UINT_32                  u4DwellTime;      /*!< In unit of Kusec */
} PARAM_802_11_CONFIG_FH_T, *P_PARAM_802_11_CONFIG_FH_T;

typedef struct _PARAM_802_11_CONFIG_T {
    UINT_32                  u4Length;         /*!< Length of structure */
    UINT_32                  u4BeaconPeriod;   /*!< In unit of Kusec */
    UINT_32                  u4ATIMWindow;     /*!< In unit of Kusec */
    UINT_32                  u4DSConfig;       /*!< Channel frequency in unit of kHz */
    PARAM_802_11_CONFIG_FH_T rFHConfig;
} PARAM_802_11_CONFIG_T, *P_PARAM_802_11_CONFIG_T;

typedef struct _PARAM_STATUS_INDICATION_T
{
    ENUM_STATUS_TYPE_T   eStatusType;
} PARAM_STATUS_INDICATION_T, *P_PARAM_STATUS_INDICATION_T;

typedef struct _PARAM_AUTH_REQUEST_T
{
    UINT_32             u4Length;             /*!< Length of this struct */
    PARAM_MAC_ADDRESS   arBssid;
    UINT_32             u4Flags;              /*!< Definitions are as follows */
} PARAM_AUTH_REQUEST_T, *P_PARAM_AUTH_REQUEST_T;

typedef struct _PARAM_AUTH_EVENT_T
{
    PARAM_STATUS_INDICATION_T   rStatus;
    PARAM_AUTH_REQUEST_T        arRequest[1];
} PARAM_AUTH_EVENT_T, *P_PARAM_AUTH_EVENT_T;

/*! \brief Capabilities, privacy, rssi and IEs of each BSSID */
typedef struct _PARAM_BSSID_EX_T {
    UINT_32                         u4Length;             /*!< Length of structure */
    PARAM_MAC_ADDRESS               arMacAddress;         /*!< BSSID */
    UINT_8                          Reserved[2];
    PARAM_SSID_T                    rSsid;               /*!< SSID */
    UINT_32                         u4Privacy;            /*!< Need WEP encryption */
    PARAM_RSSI                      rRssi;               /*!< in dBm */
    ENUM_PARAM_NETWORK_TYPE_T       eNetworkTypeInUse;
    PARAM_802_11_CONFIG_T           rConfiguration;
    ENUM_PARAM_OP_MODE_T            eOpMode;
    PARAM_RATES_EX                  rSupportedRates;
    UINT_32                         u4IELength;
    UINT_8                          aucIEs[1];
} PARAM_BSSID_EX_T, *P_PARAM_BSSID_EX_T;

typedef struct _PARAM_BSSID_LIST_EX {
    UINT_32                         u4NumberOfItems;      /*!< at least 1 */
    PARAM_BSSID_EX_T                arBssid[1];
} PARAM_BSSID_LIST_EX_T, *P_PARAM_BSSID_LIST_EX_T;


typedef struct _PARAM_WEP_T
{
    UINT_32             u4Length;             /*!< Length of structure */
    UINT_32             u4KeyIndex;           /*!< 0: pairwise key, others group keys */
    UINT_32             u4KeyLength;          /*!< Key length in bytes */
    UINT_8              aucKeyMaterial[1];    /*!< Key content by above setting */
} PARAM_WEP_T, *P_PARAM_WEP_T;

/*! \brief Key mapping of BSSID */
typedef struct _PARAM_KEY_T
{
    UINT_32             u4Length;             /*!< Length of structure */
    UINT_32             u4KeyIndex;           /*!< KeyID */
    UINT_32             u4KeyLength;          /*!< Key length in bytes */
    PARAM_MAC_ADDRESS   arBSSID;              /*!< MAC address */
    PARAM_KEY_RSC       rKeyRSC;
    UINT_8              aucKeyMaterial[1];    /*!< Key content by above setting */
} PARAM_KEY_T, *P_PARAM_KEY_T;

typedef struct _PARAM_REMOVE_KEY_T
{
    UINT_32             u4Length;             /*!< Length of structure */
    UINT_32             u4KeyIndex;           /*!< KeyID */
    PARAM_MAC_ADDRESS   arBSSID;               /*!< MAC address */
} PARAM_REMOVE_KEY_T, *P_PARAM_REMOVE_KEY_T;

#if CFG_SUPPORT_WAPI
typedef enum _ENUM_KEY_TYPE {
    ENUM_WPI_PAIRWISE_KEY = 0,
    ENUM_WPI_GROUP_KEY
} ENUM_KEY_TYPE;

typedef enum _ENUM_WPI_PROTECT_TYPE
{
    ENUM_WPI_NONE,
    ENUM_WPI_RX,
    ENUM_WPI_TX,
    ENUM_WPI_RX_TX
} ENUM_WPI_PROTECT_TYPE;

typedef struct _PARAM_WPI_KEY_T {
    ENUM_KEY_TYPE           eKeyType;
    ENUM_WPI_PROTECT_TYPE   eDirection;
    UINT_8                  ucKeyID;
    UINT_8                  aucRsv[3];
    UINT_8                  aucAddrIndex[12];
    UINT_32                 u4LenWPIEK;
    UINT_8                  aucWPIEK[256];
    UINT_32                 u4LenWPICK;
    UINT_8                  aucWPICK[256];
    UINT_8                  aucPN[16];
} PARAM_WPI_KEY_T, *P_PARAM_WPI_KEY_T;
#endif


typedef enum _PARAM_POWER_MODE
{
    Param_PowerModeCAM,
    Param_PowerModeMAX_PSP,
    Param_PowerModeFast_PSP,
    Param_PowerModeMax                      /* Upper bound, not real case */
} PARAM_POWER_MODE, *PPARAM_POWER_MODE;

typedef enum _PARAM_DEVICE_POWER_STATE
{
    ParamDeviceStateUnspecified = 0,
    ParamDeviceStateD0,
    ParamDeviceStateD1,
    ParamDeviceStateD2,
    ParamDeviceStateD3,
    ParamDeviceStateMaximum
} PARAM_DEVICE_POWER_STATE, *PPARAM_DEVICE_POWER_STATE;

#if CFG_SUPPORT_802_11D

/*! \brief The enumeration definitions for OID_IPN_MULTI_DOMAIN_CAPABILITY */
typedef enum _PARAM_MULTI_DOMAIN_CAPABILITY {
    ParamMultiDomainCapDisabled,
    ParamMultiDomainCapEnabled
} PARAM_MULTI_DOMAIN_CAPABILITY, *P_PARAM_MULTI_DOMAIN_CAPABILITY;
#endif

typedef struct _COUNTRY_STRING_ENTRY {
    UINT_8  aucCountryCode[2];
    UINT_8  aucEnvironmentCode[2];
} COUNTRY_STRING_ENTRY, *P_COUNTRY_STRING_ENTRY;

/* Power management related definition and enumerations */
#define UAPSD_NONE                              0
#define UAPSD_AC0                               (BIT(0) | BIT(4))
#define UAPSD_AC1                               (BIT(1) | BIT(5))
#define UAPSD_AC2                               (BIT(2) | BIT(6))
#define UAPSD_AC3                               (BIT(3) | BIT(7))
#define UAPSD_ALL                               (UAPSD_AC0 | UAPSD_AC1 | UAPSD_AC2 | UAPSD_AC3)

typedef enum _ENUM_POWER_SAVE_PROFILE_T
{
    ENUM_PSP_CONTINUOUS_ACTIVE = 0,
    ENUM_PSP_CONTINUOUS_POWER_SAVE,
    ENUM_PSP_FAST_SWITCH,
    ENUM_PSP_NUM
} ENUM_POWER_SAVE_PROFILE_T, *PENUM_POWER_SAVE_PROFILE_T;


/*--------------------------------------------------------------*/
/*! \brief Set/Query testing type.                              */
/*--------------------------------------------------------------*/
typedef struct _PARAM_802_11_TEST_T
{
    UINT_32             u4Length;
    UINT_32             u4Type;
    union
    {
        PARAM_AUTH_EVENT_T  AuthenticationEvent;
        PARAM_RSSI          RssiTrigger;
    } u;
} PARAM_802_11_TEST_T, *P_PARAM_802_11_TEST_T;



/*--------------------------------------------------------------*/
/*! \brief Set/Query authentication and encryption capability.  */
/*--------------------------------------------------------------*/
typedef struct _PARAM_AUTH_ENCRYPTION_T
{
    ENUM_PARAM_AUTH_MODE_T              eAuthModeSupported;
    ENUM_PARAM_ENCRYPTION_STATUS_T      eEncryptStatusSupported;
} PARAM_AUTH_ENCRYPTION_T, *P_PARAM_AUTH_ENCRYPTION_T;

typedef struct _PARAM_CAPABILITY_T
{
     UINT_32                  u4Length;
     UINT_32                  u4Version;
     UINT_32                  u4NoOfPMKIDs;
     UINT_32                  u4NoOfAuthEncryptPairsSupported;
     PARAM_AUTH_ENCRYPTION_T  arAuthenticationEncryptionSupported[1];
} PARAM_CAPABILITY_T, *P_PARAM_CAPABILITY_T;

typedef UINT_8   PARAM_PMKID_VALUE[16];

typedef struct _PARAM_BSSID_INFO_T
{
    PARAM_MAC_ADDRESS   arBSSID;
    PARAM_PMKID_VALUE   arPMKID;
} PARAM_BSSID_INFO_T, *P_PARAM_BSSID_INFO_T;

typedef struct _PARAM_PMKID_T
{
    UINT_32             u4Length;
    UINT_32             u4BSSIDInfoCount;
    PARAM_BSSID_INFO_T  arBSSIDInfo[1];
} PARAM_PMKID_T, *P_PARAM_PMKID_T;

/*! \brief PMKID candidate lists. */
typedef struct _PARAM_PMKID_CANDIDATE_T {
    PARAM_MAC_ADDRESS   arBSSID;
    UINT_32             u4Flags;
} PARAM_PMKID_CANDIDATE_T, *P_PARAM_PMKID_CANDIDATE_T;

//#ifdef LINUX
typedef struct _PARAM_PMKID_CANDIDATE_LIST_T
{
    UINT_32                   u4Version;            /*!< Version */
    UINT_32                   u4NumCandidates;      /*!< How many candidates follow */
    PARAM_PMKID_CANDIDATE_T   arCandidateList[1];
} PARAM_PMKID_CANDIDATE_LIST_T, *P_PARAM_PMKID_CANDIDATE_LIST_T;
//#endif

typedef struct _PARAM_CUSTOM_MCR_RW_STRUC_T {
    UINT_32             u4McrOffset;
    UINT_32             u4McrData;
} PARAM_CUSTOM_MCR_RW_STRUC_T, *P_PARAM_CUSTOM_MCR_RW_STRUC_T;


typedef struct _PARAM_CUSTOM_SW_CTRL_STRUC_T {
    UINT_32             u4Id;
    UINT_32             u4Data;
} PARAM_CUSTOM_SW_CTRL_STRUC_T, *P_PARAM_CUSTOM_SW_CTRL_STRUC_T;


typedef struct _PARAM_CUSTOM_EEPROM_RW_STRUC_T {
    UINT_8              ucEepromMethod; /* For read only read: 1, query size: 0*/
    UINT_8              ucEepromIndex;
    UINT_8              reserved;
    UINT_16            u2EepromData;
} PARAM_CUSTOM_EEPROM_RW_STRUC_T, *P_PARAM_CUSTOM_EEPROM_RW_STRUC_T,
    PARAM_CUSTOM_NVRAM_RW_STRUCT_T, *P_PARAM_CUSTOM_NVRAM_RW_STRUCT_T;

typedef struct _PARAM_CUSTOM_WMM_PS_TEST_STRUC_T {
    UINT_8  bmfgApsdEnAc;           /* b0~3: trigger-en AC0~3. b4~7: delivery-en AC0~3 */
    UINT_8  ucIsEnterPsAtOnce;      /* enter PS immediately without 5 second guard after connected */
    UINT_8  ucIsDisableUcTrigger;   /* not to trigger UC on beacon TIM is matched (under U-APSD) */
    UINT_8  reserved;
} PARAM_CUSTOM_WMM_PS_TEST_STRUC_T, *P_PARAM_CUSTOM_WMM_PS_TEST_STRUC_T;

typedef struct _PARAM_CUSTOM_NOA_PARAM_STRUC_T {
    UINT_32  u4NoaDurationMs;
    UINT_32  u4NoaIntervalMs;
    UINT_32  u4NoaCount;
} PARAM_CUSTOM_NOA_PARAM_STRUC_T, *P_PARAM_CUSTOM_NOA_PARAM_STRUC_T;

typedef struct _PARAM_CUSTOM_OPPPS_PARAM_STRUC_T {
    UINT_32  u4CTwindowMs;
} PARAM_CUSTOM_OPPPS_PARAM_STRUC_T, *P_PARAM_CUSTOM_OPPPS_PARAM_STRUC_T;

typedef struct _PARAM_CUSTOM_UAPSD_PARAM_STRUC_T {
    UINT_8  fgEnAPSD;
    UINT_8  fgEnAPSD_AcBe;
    UINT_8  fgEnAPSD_AcBk;
    UINT_8  fgEnAPSD_AcVo;
    UINT_8  fgEnAPSD_AcVi;
    UINT_8  ucMaxSpLen;
    UINT_8  aucResv[2];
} PARAM_CUSTOM_UAPSD_PARAM_STRUC_T, *P_PARAM_CUSTOM_UAPSD_PARAM_STRUC_T;

typedef enum _ENUM_CFG_SRC_TYPE_T {
    CFG_SRC_TYPE_EEPROM,
    CFG_SRC_TYPE_NVRAM,
    CFG_SRC_TYPE_UNKNOWN,
    CFG_SRC_TYPE_NUM
} ENUM_CFG_SRC_TYPE_T, *P_ENUM_CFG_SRC_TYPE_T;

typedef enum _ENUM_EEPROM_TYPE_T {
    EEPROM_TYPE_NO,
    EEPROM_TYPE_PRESENT,
    EEPROM_TYPE_NUM
} ENUM_EEPROM_TYPE_T, *P_ENUM_EEPROM_TYPE_T;

typedef struct _PARAM_QOS_TSINFO {
    UINT_8           ucTrafficType;         /* Traffic Type: 1 for isochronous 0 for asynchronous */
    UINT_8           ucTid;                 /* TSID: must be between 8 ~ 15 */
    UINT_8           ucDirection;           /* direction */
    UINT_8           ucAccessPolicy;        /* access policy */
    UINT_8           ucAggregation;         /* aggregation */
    UINT_8           ucApsd;                /* APSD */
    UINT_8           ucuserPriority;        /* user priority */
    UINT_8           ucTsInfoAckPolicy;     /* TSINFO ACK policy */
    UINT_8           ucSchedule;            /* Schedule */
} PARAM_QOS_TSINFO, *P_PARAM_QOS_TSINFO;

typedef struct _PARAM_QOS_TSPEC {
    PARAM_QOS_TSINFO rTsInfo;               /* TS info field */
    UINT_16          u2NominalMSDUSize;     /* nominal MSDU size */
    UINT_16          u2MaxMSDUsize;         /* maximum MSDU size */
    UINT_32          u4MinSvcIntv;          /* minimum service interval */
    UINT_32          u4MaxSvcIntv;          /* maximum service interval */
    UINT_32          u4InactIntv;           /* inactivity interval */
    UINT_32          u4SpsIntv;             /* suspension interval */
    UINT_32          u4SvcStartTime;        /* service start time */
    UINT_32          u4MinDataRate;         /* minimum Data rate */
    UINT_32          u4MeanDataRate;        /* mean data rate */
    UINT_32          u4PeakDataRate;        /* peak data rate */
    UINT_32          u4MaxBurstSize;        /* maximum burst size */
    UINT_32          u4DelayBound;          /* delay bound */
    UINT_32          u4MinPHYRate;          /* minimum PHY rate */
    UINT_16          u2Sba;                 /* surplus bandwidth allowance */
    UINT_16          u2MediumTime;          /* medium time */
} PARAM_QOS_TSPEC, *P_PARAM_QOS_TSPEC;

typedef struct _PARAM_QOS_ADDTS_REQ_INFO {
    PARAM_QOS_TSPEC             rTspec;
} PARAM_QOS_ADDTS_REQ_INFO, *P_PARAM_QOS_ADDTS_REQ_INFO;

typedef struct _PARAM_VOIP_CONFIG {
    UINT_32         u4VoipTrafficInterval;  /* 0: disable VOIP configuration */
} PARAM_VOIP_CONFIG, *P_PARAM_VOIP_CONFIG;

/*802.11 Statistics Struct*/
typedef struct _PARAM_802_11_STATISTICS_STRUCT_T {
    UINT_32         u4Length;             // Length of structure
    LARGE_INTEGER   rTransmittedFragmentCount;
    LARGE_INTEGER   rMulticastTransmittedFrameCount;
    LARGE_INTEGER   rFailedCount;
    LARGE_INTEGER   rRetryCount;
    LARGE_INTEGER   rMultipleRetryCount;
    LARGE_INTEGER   rRTSSuccessCount;
    LARGE_INTEGER   rRTSFailureCount;
    LARGE_INTEGER   rACKFailureCount;
    LARGE_INTEGER   rFrameDuplicateCount;
    LARGE_INTEGER   rReceivedFragmentCount;
    LARGE_INTEGER   rMulticastReceivedFrameCount;
    LARGE_INTEGER   rFCSErrorCount;
    LARGE_INTEGER   rTKIPLocalMICFailures;
    LARGE_INTEGER   rTKIPICVErrors;
    LARGE_INTEGER   rTKIPCounterMeasuresInvoked;
    LARGE_INTEGER   rTKIPReplays;
    LARGE_INTEGER   rCCMPFormatErrors;
    LARGE_INTEGER   rCCMPReplays;
    LARGE_INTEGER   rCCMPDecryptErrors;
    LARGE_INTEGER   rFourWayHandshakeFailures;
    LARGE_INTEGER   rWEPUndecryptableCount;
    LARGE_INTEGER   rWEPICVErrorCount;
    LARGE_INTEGER   rDecryptSuccessCount;
    LARGE_INTEGER   rDecryptFailureCount;
} PARAM_802_11_STATISTICS_STRUCT_T, *P_PARAM_802_11_STATISTICS_STRUCT_T;

/* Linux Network Device Statistics Struct */
typedef struct _PARAM_LINUX_NETDEV_STATISTICS_T {
    UINT_32         u4RxPackets;
    UINT_32         u4TxPackets;
    UINT_32         u4RxBytes;
    UINT_32         u4TxBytes;
    UINT_32         u4RxErrors;
    UINT_32         u4TxErrors;
    UINT_32         u4Multicast;
} PARAM_LINUX_NETDEV_STATISTICS_T, *P_PARAM_LINUX_NETDEV_STATISTICS_T;


typedef struct _PARAM_MTK_WIFI_TEST_STRUC_T {
    UINT_32                 u4FuncIndex;
    UINT_32                 u4FuncData;
} PARAM_MTK_WIFI_TEST_STRUC_T, *P_PARAM_MTK_WIFI_TEST_STRUC_T;


/* 802.11 Media stream constraints */
typedef enum _ENUM_MEDIA_STREAM_MODE {
    ENUM_MEDIA_STREAM_OFF,
    ENUM_MEDIA_STREAM_ON
} ENUM_MEDIA_STREAM_MODE, *P_ENUM_MEDIA_STREAM_MODE;

/* for NDIS 5.1 Media Streaming Change */
typedef struct _PARAM_MEDIA_STREAMING_INDICATION {
    PARAM_STATUS_INDICATION_T   rStatus;
    ENUM_MEDIA_STREAM_MODE      eMediaStreamMode;
} PARAM_MEDIA_STREAMING_INDICATION, *P_PARAM_MEDIA_STREAMING_INDICATION;


#define PARAM_PROTOCOL_ID_DEFAULT       0x00
#define PARAM_PROTOCOL_ID_TCP_IP        0x02
#define PARAM_PROTOCOL_ID_IPX           0x06
#define PARAM_PROTOCOL_ID_NBF           0x07
#define PARAM_PROTOCOL_ID_MAX           0x0F
#define PARAM_PROTOCOL_ID_MASK          0x0F

/* for NDIS OID_GEN_NETWORK_LAYER_ADDRESSES */
typedef struct _PARAM_NETWORK_ADDRESS_IP
{
    UINT_16     sin_port;
    UINT_32     in_addr;
    UINT_8      sin_zero[8];
} PARAM_NETWORK_ADDRESS_IP, *P_PARAM_NETWORK_ADDRESS_IP;

typedef struct _PARAM_NETWORK_ADDRESS {
    UINT_16     u2AddressLength;    // length in bytes of Address[] in this
    UINT_16     u2AddressType;      // type of this address (PARAM_PROTOCOL_ID_XXX above)
    UINT_8      aucAddress[1];      // actually AddressLength bytes long
} PARAM_NETWORK_ADDRESS, *P_PARAM_NETWORK_ADDRESS;

// The following is used with OID_GEN_NETWORK_LAYER_ADDRESSES to set network layer addresses on an interface

typedef struct _PARAM_NETWORK_ADDRESS_LIST
{
    UINT_32                 u4AddressCount;     // number of addresses following
    UINT_16                 u2AddressType;      // type of this address (NDIS_PROTOCOL_ID_XXX above)
    PARAM_NETWORK_ADDRESS   arAddress[1];       // actually AddressCount elements long
} PARAM_NETWORK_ADDRESS_LIST, *P_PARAM_NETWORK_ADDRESS_LIST;

#if CFG_SLT_SUPPORT

#define FIXED_BW_LG20       0x0000
#define FIXED_BW_UL20       0x2000
#define FIXED_BW_DL40       0x3000

#define FIXED_EXT_CHNL_U20  0x4000   // For AGG register.
#define FIXED_EXT_CHNL_L20  0xC000   // For AGG regsiter.

typedef enum _ENUM_MTK_LP_TEST_MODE_T {
    ENUM_MTK_LP_TEST_NORMAL,
    ENUM_MTK_LP_TEST_GOLDEN_SAMPLE,
    ENUM_MTK_LP_TEST_DUT,
    ENUM_MTK_LP_TEST_MODE_NUM
} ENUM_MTK_LP_TEST_MODE_T, *P_ENUM_MTK_LP_TEST_MODE_T;

typedef enum _ENUM_MTK_SLT_FUNC_IDX_T {
    ENUM_MTK_SLT_FUNC_DO_NOTHING,
    ENUM_MTK_SLT_FUNC_INITIAL,
    ENUM_MTK_SLT_FUNC_RATE_SET,
    ENUM_MTK_SLT_FUNC_LP_SET,
    ENUM_MTK_SLT_FUNC_NUM
} ENUM_MTK_SLT_FUNC_IDX_T, *P_ENUM_MTK_SLT_FUNC_IDX_T;

typedef struct _PARAM_MTK_SLT_LP_TEST_STRUC_T {
    ENUM_MTK_LP_TEST_MODE_T rLpTestMode;
    UINT_32 u4BcnRcvNum;
} PARAM_MTK_SLT_LP_TEST_STRUC_T, *P_PARAM_MTK_SLT_LP_TEST_STRUC_T;

typedef struct _PARAM_MTK_SLT_TR_TEST_STRUC_T {
    ENUM_PARAM_NETWORK_TYPE_T rNetworkType;         // Network Type OFDM5G or OFDM2.4G
    UINT_32 u4FixedRate;                                                 // Fixed Rate including BW
} PARAM_MTK_SLT_TR_TEST_STRUC_T, *P_PARAM_MTK_SLT_TR_TEST_STRUC_T;

typedef struct _PARAM_MTK_SLT_INITIAL_STRUC_T {
    UINT_8 aucTargetMacAddr[PARAM_MAC_ADDR_LEN];
    UINT_16 u2SiteID;
} PARAM_MTK_SLT_INITIAL_STRUC_T, *P_PARAM_MTK_SLT_INITIAL_STRUC_T;

typedef struct _PARAM_MTK_SLT_TEST_STRUC_T {
    ENUM_MTK_SLT_FUNC_IDX_T rSltFuncIdx;
    UINT_32 u4Length;                       /* Length of structure,
                                                     including myself */
    UINT_32  u4FuncInfoLen;  /* Include following content
                                                    field and myself */
    union {
        PARAM_MTK_SLT_INITIAL_STRUC_T rMtkInitTest;
        PARAM_MTK_SLT_LP_TEST_STRUC_T rMtkLpTest;
        PARAM_MTK_SLT_TR_TEST_STRUC_T rMtkTRTest;
    } unFuncInfoContent;

} PARAM_MTK_SLT_TEST_STRUC_T, *P_PARAM_MTK_SLT_TEST_STRUC_T;

#endif

/*--------------------------------------------------------------*/
/*! \brief For Fixed Rate Configuration (Registry)              */
/*--------------------------------------------------------------*/
typedef enum _ENUM_REGISTRY_FIXED_RATE_T {
    FIXED_RATE_NONE,
    FIXED_RATE_1M,
    FIXED_RATE_2M,
    FIXED_RATE_5_5M,
    FIXED_RATE_11M,
    FIXED_RATE_6M,
    FIXED_RATE_9M,
    FIXED_RATE_12M,
    FIXED_RATE_18M,
    FIXED_RATE_24M,
    FIXED_RATE_36M,
    FIXED_RATE_48M,
    FIXED_RATE_54M,
    FIXED_RATE_MCS0_20M_800NS,
    FIXED_RATE_MCS1_20M_800NS,
    FIXED_RATE_MCS2_20M_800NS,
    FIXED_RATE_MCS3_20M_800NS,
    FIXED_RATE_MCS4_20M_800NS,
    FIXED_RATE_MCS5_20M_800NS,
    FIXED_RATE_MCS6_20M_800NS,
    FIXED_RATE_MCS7_20M_800NS,
    FIXED_RATE_MCS0_20M_400NS,
    FIXED_RATE_MCS1_20M_400NS,
    FIXED_RATE_MCS2_20M_400NS,
    FIXED_RATE_MCS3_20M_400NS,
    FIXED_RATE_MCS4_20M_400NS,
    FIXED_RATE_MCS5_20M_400NS,
    FIXED_RATE_MCS6_20M_400NS,
    FIXED_RATE_MCS7_20M_400NS,
    FIXED_RATE_MCS0_40M_800NS,
    FIXED_RATE_MCS1_40M_800NS,
    FIXED_RATE_MCS2_40M_800NS,
    FIXED_RATE_MCS3_40M_800NS,
    FIXED_RATE_MCS4_40M_800NS,
    FIXED_RATE_MCS5_40M_800NS,
    FIXED_RATE_MCS6_40M_800NS,
    FIXED_RATE_MCS7_40M_800NS,
    FIXED_RATE_MCS32_800NS,
    FIXED_RATE_MCS0_40M_400NS,
    FIXED_RATE_MCS1_40M_400NS,
    FIXED_RATE_MCS2_40M_400NS,
    FIXED_RATE_MCS3_40M_400NS,
    FIXED_RATE_MCS4_40M_400NS,
    FIXED_RATE_MCS5_40M_400NS,
    FIXED_RATE_MCS6_40M_400NS,
    FIXED_RATE_MCS7_40M_400NS,
    FIXED_RATE_MCS32_400NS,
    FIXED_RATE_NUM
} ENUM_REGISTRY_FIXED_RATE_T, *P_ENUM_REGISTRY_FIXED_RATE_T;

typedef enum _ENUM_BT_CMD_T {
    BT_CMD_PROFILE = 0,
    BT_CMD_UPDATE,
    BT_CMD_NUM
} ENUM_BT_CMD_T;

typedef enum _ENUM_BT_PROFILE_T {
    BT_PROFILE_CUSTOM = 0,
    BT_PROFILE_SCO,
    BT_PROFILE_ACL,
    BT_PROFILE_MIXED,
    BT_PROFILE_NO_CONNECTION,
    BT_PROFILE_NUM
} ENUM_BT_PROFILE_T;

typedef struct _PTA_PROFILE_T {
    ENUM_BT_PROFILE_T eBtProfile;
    union {
        UINT_8 aucBTPParams[BT_PROFILE_PARAM_LEN];
                                /*  0: sco reserved slot time,
                                    1: sco idle slot time,
                                    2: acl throughput,
                                    3: bt tx power,
                                    4: bt rssi
                                    5: VoIP interval
                                    6: BIT(0) Use this field, BIT(1) 0 apply single/ 1 dual PTA setting.
                                */
        UINT_32 au4Btcr[4];
    } u;
} PTA_PROFILE_T, *P_PTA_PROFILE_T;

typedef struct _PTA_IPC_T {
    UINT_8      ucCmd;
    UINT_8      ucLen;
    union {
        PTA_PROFILE_T rProfile;
        UINT_8      aucBTPParams[BT_PROFILE_PARAM_LEN];
    } u;
} PARAM_PTA_IPC_T, *P_PARAM_PTA_IPC_T, PTA_IPC_T, *P_PTA_IPC_T;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/*--------------------------------------------------------------*/
/* Routines to set parameters or query information.             */
/*--------------------------------------------------------------*/
/***** Routines in wlan_oid.c *****/
WLAN_STATUS
wlanoidQueryNetworkTypesSupported(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryNetworkTypeInUse(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetNetworkTypeInUse (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryBssid(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetBssidListScan(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryBssidList(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetBssid(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetSsid(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQuerySsid(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryInfrastructureMode(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetInfrastructureMode(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryAuthMode(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetAuthMode(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

#if 0
WLAN_STATUS
wlanoidQueryPrivacyFilter (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );


WLAN_STATUS
wlanoidSetPrivacyFilter (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );
#endif

WLAN_STATUS
wlanoidSetEncryptionStatus(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryEncryptionStatus(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetAddWep(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetRemoveWep(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetAddKey(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetRemoveKey(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetReloadDefaults(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetTest(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryCapability(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryFrequency (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetFrequency (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );


WLAN_STATUS
wlanoidQueryAtimWindow (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetAtimWindow (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );


WLAN_STATUS
wlanoidSetChannel (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryRssi(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryRssiTrigger(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetRssiTrigger(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryRtsThreshold (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetRtsThreshold (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQuery802dot11PowerSaveProfile (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSet802dot11PowerSaveProfile (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       prSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryPmkid(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetPmkid(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQuerySupportedRates(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryDesiredRates (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetDesiredRates (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryPermanentAddr (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvQueryBuf,
    IN  UINT_32  u4QueryBufLen,
    OUT PUINT_32 pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryCurrentAddr (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvQueryBuf,
    IN  UINT_32  u4QueryBufLen,
    OUT PUINT_32 pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryPermanentAddr (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvQueryBuf,
    IN  UINT_32  u4QueryBufLen,
    OUT PUINT_32 pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryLinkSpeed(
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvQueryBuffer,
    IN  UINT_32  u4QueryBufferLen,
    OUT PUINT_32 pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryMcrRead (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvQueryBuffer,
    IN  UINT_32  u4QueryBufferLen,
    OUT PUINT_32 pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetMcrWrite (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    );


WLAN_STATUS
wlanoidQuerySwCtrlRead (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvQueryBuffer,
    IN  UINT_32  u4QueryBufferLen,
    OUT PUINT_32 pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetSwCtrlWrite (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    );


WLAN_STATUS
wlanoidQueryEepromRead (
    IN  P_ADAPTER_T  prAdapter,
    IN  PVOID        pvQueryBuffer,
    IN  UINT_32      u4QueryBufferLen,
    OUT PUINT_32     pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetEepromWrite (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryRfTestRxStatus (
    IN  P_ADAPTER_T  prAdapter,
    IN  PVOID        pvQueryBuffer,
    IN  UINT_32      u4QueryBufferLen,
    OUT PUINT_32     pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryRfTestTxStatus (
    IN  P_ADAPTER_T  prAdapter,
    IN  PVOID        pvQueryBuffer,
    IN  UINT_32      u4QueryBufferLen,
    OUT PUINT_32     pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryOidInterfaceVersion (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvQueryBuffer,
    IN  UINT_32  u4QueryBufferLen,
    OUT PUINT_32 pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryVendorId(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryMulticastList(
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetMulticastList(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryRcvError (
    IN  P_ADAPTER_T     prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryRcvNoBuffer (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryRcvCrcError (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryStatistics (
    IN  P_ADAPTER_T     prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    );

#ifdef LINUX

WLAN_STATUS
wlanoidQueryStatisticsForLinux (
    IN  P_ADAPTER_T     prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    );

#endif

WLAN_STATUS
wlanoidQueryMediaStreamMode(
    IN  P_ADAPTER_T     prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetMediaStreamMode(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryRcvOk(
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32        pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryXmitOk(
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryXmitError (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryXmitOneCollision (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryXmitMoreCollisions (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID           pvQueryBuffer,
    IN  UINT_32         u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryXmitMaxCollisions (
    IN  P_ADAPTER_T       prAdapter,
    IN   PVOID           pvQueryBuffer,
    IN   UINT_32         u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );


WLAN_STATUS
wlanoidSetCurrentPacketFilter(
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryCurrentPacketFilter (
    IN P_ADAPTER_T prAdapter,
    IN  PVOID      pvQueryBuffer,
    IN  UINT_32    u4QueryBufferLen,
    OUT PUINT_32   pu4QueryInfoLen
    );


WLAN_STATUS
wlanoidSetAcpiDevicePowerState (
    IN P_ADAPTER_T prAdapter,
    IN  PVOID    pvSetBuffer,
    IN  UINT_32  u4SetBufferLen,
    OUT PUINT_32 pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryAcpiDevicePowerState (
    IN P_ADAPTER_T prAdapter,
    IN  PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );


WLAN_STATUS
wlanoidSetDisassociate (
    IN P_ADAPTER_T        prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryFragThreshold (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetFragThreshold (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );


WLAN_STATUS
wlanoidQueryAdHocMode (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetAdHocMode (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryBeaconInterval (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetBeaconInterval (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetCurrentAddr (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

#if CFG_TCP_IP_CHKSUM_OFFLOAD
WLAN_STATUS
wlanoidSetCSUMOffload (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
);
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */

WLAN_STATUS
wlanoidSetNetworkAddress (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryMaxFrameSize (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryMaxTotalSize (
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetCurrentLookahead (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

/* RF Test related APIs */
WLAN_STATUS
wlanoidRftestSetTestMode (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidRftestSetAbortTestMode (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidRftestQueryAutoTest (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidRftestSetAutoTest (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

#if CFG_SUPPORT_WAPI
WLAN_STATUS
wlanoidSetWapiMode (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetWapiAssocInfo (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetWapiKey(
    IN  P_ADAPTER_T   prAdapter,
    IN  PVOID         pvSetBuffer,
    IN  UINT_32       u4SetBufferLen,
    OUT PUINT_32      pu4SetInfoLen
    );
#endif

#if CFG_SUPPORT_WPS2
WLAN_STATUS
wlanoidSetWSCAssocInfo (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );
#endif

#if CFG_ENABLE_WAKEUP_ON_LAN
WLAN_STATUS
wlanoidSetAddWakeupPattern (
    IN  P_ADAPTER_T   prAdapter,
    IN  PVOID         pvSetBuffer,
    IN  UINT_32       u4SetBufferLen,
    OUT PUINT_32      pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetRemoveWakeupPattern (
    IN  P_ADAPTER_T   prAdapter,
    IN  PVOID         pvSetBuffer,
    IN  UINT_32       u4SetBufferLen,
    OUT PUINT_32      pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryEnableWakeup (
    IN  P_ADAPTER_T   prAdapter,
    OUT PVOID         pvQueryBuffer,
    IN  UINT_32       u4QueryBufferLen,
    OUT PUINT_32      u4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetEnableWakeup (
    IN  P_ADAPTER_T   prAdapter,
    IN  PVOID         pvSetBuffer,
    IN  UINT_32       u4SetBufferLen,
    OUT PUINT_32      pu4SetInfoLen
    );
#endif

WLAN_STATUS
wlanoidSetWiFiWmmPsTest (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetTxAmpdu (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetAddbaReject(
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryNvramRead (
    IN  P_ADAPTER_T  prAdapter,
    OUT PVOID        pvQueryBuffer,
    IN  UINT_32      u4QueryBufferLen,
    OUT PUINT_32     pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetNvramWrite (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryCfgSrcType(
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryEepromType(
    IN  P_ADAPTER_T       prAdapter,
    OUT PVOID             pvQueryBuffer,
    IN  UINT_32           u4QueryBufferLen,
    OUT PUINT_32          pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetCountryCode (
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );


#if CFG_SLT_SUPPORT

WLAN_STATUS
wlanoidQuerySLTStatus(
    IN  P_ADAPTER_T   prAdapter,
    OUT PVOID         pvQueryBuffer,
    IN  UINT_32       u4QueryBufferLen,
    OUT PUINT_32      pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidUpdateSLTMode(
    IN P_ADAPTER_T  prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

#endif

#if 0
WLAN_STATUS
wlanoidSetNoaParam (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetOppPsParam (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetUApsdParam (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );
#endif

/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetBT (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryBT (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetTxPower (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

/*
WLAN_STATUS
wlanoidQueryBtSingleAntenna (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetBtSingleAntenna (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetPta (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryPta (
    IN  P_ADAPTER_T prAdapter,
    OUT PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );
*/
/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _WLAN_OID_H */

