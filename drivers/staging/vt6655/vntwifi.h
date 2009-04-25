/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: vntwifi.h
 *
 * Purpose: export VNT Host WiFi library function
 *
 * Author: Yiching Chen
 *
 * Date: Jan 7, 2004
 *
 */

#ifndef __VNTWIFI_H__
#define __VNTWIFI_H__

#if !defined(__TTYPE_H__)
#include "ttype.h"
#endif
#if !defined(__80211MGR_H__)
#include "80211mgr.h"
#endif
#if !defined(__CARD_H__)
#include "card.h"
#endif

/*---------------------  Export Definitions -------------------------*/
#define RATE_1M         0
#define RATE_2M         1
#define RATE_5M         2
#define RATE_11M        3
#define RATE_6M         4
#define RATE_9M         5
#define RATE_12M        6
#define RATE_18M        7
#define RATE_24M        8
#define RATE_36M        9
#define RATE_48M       10
#define RATE_54M       11
#define RATE_AUTO      12
#define MAX_RATE       12

// key CipherSuite
#define KEY_CTL_WEP         0x00
#define KEY_CTL_NONE        0x01
#define KEY_CTL_TKIP        0x02
#define KEY_CTL_CCMP        0x03
#define KEY_CTL_INVALID     0xFF

#define CHANNEL_MAX_24G         14

#define MAX_BSS_NUM             42

#define MAX_PMKID_CACHE         16

// Pre-configured Authenticaiton Mode (from XP)
typedef enum tagWMAC_AUTHENTICATION_MODE {

    WMAC_AUTH_OPEN,
    WMAC_AUTH_SHAREKEY,
    WMAC_AUTH_AUTO,
    WMAC_AUTH_WPA,
    WMAC_AUTH_WPAPSK,
    WMAC_AUTH_WPANONE,
    WMAC_AUTH_WPA2,
    WMAC_AUTH_WPA2PSK,
    WMAC_AUTH_MAX       // Not a real mode, defined as upper bound

} WMAC_AUTHENTICATION_MODE, *PWMAC_AUTHENTICATION_MODE;

typedef enum tagWMAC_ENCRYPTION_MODE {

    WMAC_ENCRYPTION_WEPEnabled,
    WMAC_ENCRYPTION_WEPDisabled,
    WMAC_ENCRYPTION_WEPKeyAbsent,
    WMAC_ENCRYPTION_WEPNotSupported,
    WMAC_ENCRYPTION_TKIPEnabled,
    WMAC_ENCRYPTION_TKIPKeyAbsent,
    WMAC_ENCRYPTION_AESEnabled,
    WMAC_ENCRYPTION_AESKeyAbsent

} WMAC_ENCRYPTION_MODE, *PWMAC_ENCRYPTION_MODE;

// Pre-configured Mode (from XP)

typedef enum tagWMAC_CONFIG_MODE {

    WMAC_CONFIG_ESS_STA = 0,
    WMAC_CONFIG_IBSS_STA,
    WMAC_CONFIG_AUTO,
    WMAC_CONFIG_AP

} WMAC_CONFIG_MODE, *PWMAC_CONFIG_MODE;



typedef enum tagWMAC_POWER_MODE {

    WMAC_POWER_CAM,
    WMAC_POWER_FAST,
    WMAC_POWER_MAX

} WMAC_POWER_MODE, *PWMAC_POWER_MODE;

#define VNTWIFIbIsShortSlotTime(wCapInfo)               \
        WLAN_GET_CAP_INFO_SHORTSLOTTIME(wCapInfo)       \

#define VNTWIFIbIsProtectMode(byERP)                    \
        ((byERP & WLAN_EID_ERP_USE_PROTECTION) != 0)    \

#define VNTWIFIbIsBarkerMode(byERP)                     \
        ((byERP & WLAN_EID_ERP_BARKER_MODE) != 0)       \

#define VNTWIFIbIsShortPreamble(wCapInfo)               \
        WLAN_GET_CAP_INFO_SHORTPREAMBLE(wCapInfo)       \

#define VNTWIFIbIsEncryption(wCapInfo)                  \
        WLAN_GET_CAP_INFO_PRIVACY(wCapInfo)             \

#define VNTWIFIbIsESS(wCapInfo)                         \
        WLAN_GET_CAP_INFO_ESS(wCapInfo)                 \


/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/


/*---------------------  Export Types  ------------------------------*/


/*---------------------  Export Functions  --------------------------*/

#ifdef __cplusplus
extern "C" {                            /* Assume C declarations for C++ */
#endif /* __cplusplus */


VOID
VNTWIFIvSetIBSSParameter (
    IN PVOID pMgmtHandle,
    IN WORD  wBeaconPeriod,
    IN WORD  wATIMWindow,
    IN UINT  uChannel
    );

VOID
VNTWIFIvSetOPMode (
    IN PVOID pMgmtHandle,
    IN WMAC_CONFIG_MODE eOPMode
    );

PWLAN_IE_SSID
VNTWIFIpGetCurrentSSID(
    IN PVOID pMgmtHandle
    );

UINT
VNTWIFIpGetCurrentChannel(
    IN PVOID pMgmtHandle
    );

WORD
VNTWIFIwGetAssocID (
    IN PVOID pMgmtHandle
    );

BYTE
VNTWIFIbyGetMaxSupportRate (
    IN PWLAN_IE_SUPP_RATES pSupportRateIEs,
    IN PWLAN_IE_SUPP_RATES pExtSupportRateIEs
    );

BYTE
VNTWIFIbyGetACKTxRate (
    IN BYTE byRxDataRate,
    IN PWLAN_IE_SUPP_RATES pSupportRateIEs,
    IN PWLAN_IE_SUPP_RATES pExtSupportRateIEs
    );

VOID
VNTWIFIvSetAuthenticationMode (
    IN PVOID pMgmtHandle,
    IN WMAC_AUTHENTICATION_MODE eAuthMode
    );

VOID
VNTWIFIvSetEncryptionMode (
    IN PVOID pMgmtHandle,
    IN WMAC_ENCRYPTION_MODE eEncryptionMode
    );


BOOL
VNTWIFIbConfigPhyMode(
    IN PVOID pMgmtHandle,
    IN CARD_PHY_TYPE ePhyType
    );

VOID
VNTWIFIbGetConfigPhyMode(
    IN  PVOID pMgmtHandle,
    OUT PVOID pePhyType
    );

VOID
VNTWIFIvQueryBSSList(
    IN PVOID    pMgmtHandle,
    OUT PUINT   puBSSCount,
    OUT PVOID   *pvFirstBSS
    );




VOID
VNTWIFIvGetNextBSS (
    IN PVOID            pMgmtHandle,
    IN PVOID            pvCurrentBSS,
    OUT PVOID           *pvNextBSS
    );



VOID
VNTWIFIvUpdateNodeTxCounter(
    IN PVOID    pMgmtHandle,
    IN PBYTE    pbyDestAddress,
    IN BOOL     bTxOk,
    IN WORD     wRate,
    IN PBYTE    pbyTxFailCount
    );


VOID
VNTWIFIvGetTxRate(
    IN PVOID    pMgmtHandle,
    IN PBYTE    pbyDestAddress,
    OUT PWORD   pwTxDataRate,
    OUT PBYTE   pbyACKRate,
    OUT PBYTE   pbyCCKBasicRate,
    OUT PBYTE   pbyOFDMBasicRate
    );
/*
BOOL
VNTWIFIbInit(
    IN PVOID    pAdapterHandler,
    OUT PVOID   *pMgmtHandler
    );
*/

BYTE
VNTWIFIbyGetKeyCypher(
    IN PVOID    pMgmtHandle,
    IN BOOL     bGroupKey
    );




BOOL
VNTWIFIbSetPMKIDCache (
    IN PVOID pMgmtObject,
    IN ULONG ulCount,
    IN PVOID pPMKIDInfo
    );

BOOL
VNTWIFIbCommandRunning (
    IN PVOID pMgmtObject
    );

WORD
VNTWIFIwGetMaxSupportRate(
    IN PVOID pMgmtObject
    );

// for 802.11h
VOID
VNTWIFIvSet11h (
    IN PVOID pMgmtObject,
    IN BOOL  b11hEnable
    );

BOOL
VNTWIFIbMeasureReport(
    IN PVOID pMgmtObject,
    IN BOOL  bEndOfReport,
    IN PVOID pvMeasureEID,
    IN BYTE  byReportMode,
    IN BYTE  byBasicMap,
    IN BYTE  byCCAFraction,
    IN PBYTE pbyRPIs
    );

BOOL
VNTWIFIbChannelSwitch(
    IN PVOID pMgmtObject,
    IN BYTE  byNewChannel
    );
/*
BOOL
VNTWIFIbRadarPresent(
    IN PVOID pMgmtObject,
    IN BYTE  byChannel
    );
*/

#ifdef __cplusplus
}                                       /* End of extern "C" { */
#endif /* __cplusplus */


#endif //__VNTWIFI_H__
