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

#include "ttype.h"
#include "80211mgr.h"
#include "card.h"
#include "wpa2.h"

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

void
VNTWIFIvSetIBSSParameter (
    void *pMgmtHandle,
    WORD  wBeaconPeriod,
    WORD  wATIMWindow,
    UINT  uChannel
    );

void
VNTWIFIvSetOPMode (
    void *pMgmtHandle,
    WMAC_CONFIG_MODE eOPMode
    );

PWLAN_IE_SSID
VNTWIFIpGetCurrentSSID(
    void *pMgmtHandle
    );

UINT
VNTWIFIpGetCurrentChannel(
    void *pMgmtHandle
    );

WORD
VNTWIFIwGetAssocID (
    void *pMgmtHandle
    );

BYTE
VNTWIFIbyGetMaxSupportRate (
    PWLAN_IE_SUPP_RATES pSupportRateIEs,
    PWLAN_IE_SUPP_RATES pExtSupportRateIEs
    );

BYTE
VNTWIFIbyGetACKTxRate (
    BYTE byRxDataRate,
    PWLAN_IE_SUPP_RATES pSupportRateIEs,
    PWLAN_IE_SUPP_RATES pExtSupportRateIEs
    );

void
VNTWIFIvSetAuthenticationMode (
    void *pMgmtHandle,
    WMAC_AUTHENTICATION_MODE eAuthMode
    );

void
VNTWIFIvSetEncryptionMode (
    void *pMgmtHandle,
    WMAC_ENCRYPTION_MODE eEncryptionMode
    );


BOOL
VNTWIFIbConfigPhyMode(
    void *pMgmtHandle,
    CARD_PHY_TYPE ePhyType
    );

void
VNTWIFIbGetConfigPhyMode(
    void *pMgmtHandle,
    void *pePhyType
    );

void
VNTWIFIvQueryBSSList(
    void *pMgmtHandle,
    PUINT   puBSSCount,
    void **pvFirstBSS
    );




void
VNTWIFIvGetNextBSS (
    void *pMgmtHandle,
    void *pvCurrentBSS,
    void **pvNextBSS
    );



void
VNTWIFIvUpdateNodeTxCounter(
    void *pMgmtHandle,
    PBYTE    pbyDestAddress,
    BOOL     bTxOk,
    WORD     wRate,
    PBYTE    pbyTxFailCount
    );


void
VNTWIFIvGetTxRate(
    void *pMgmtHandle,
    PBYTE    pbyDestAddress,
    PWORD   pwTxDataRate,
    PBYTE   pbyACKRate,
    PBYTE   pbyCCKBasicRate,
    PBYTE   pbyOFDMBasicRate
    );
/*
BOOL
VNTWIFIbInit(
    void *pAdapterHandler,
    void **pMgmtHandler
    );
*/

BYTE
VNTWIFIbyGetKeyCypher(
    void *pMgmtHandle,
    BOOL     bGroupKey
    );




BOOL
VNTWIFIbSetPMKIDCache (
    void *pMgmtObject,
    ULONG ulCount,
    void *pPMKIDInfo
    );

BOOL
VNTWIFIbCommandRunning (
    void *pMgmtObject
    );

WORD
VNTWIFIwGetMaxSupportRate(
    void *pMgmtObject
    );

// for 802.11h
void
VNTWIFIvSet11h (
    void *pMgmtObject,
    BOOL  b11hEnable
    );

BOOL
VNTWIFIbMeasureReport(
    void *pMgmtObject,
    BOOL  bEndOfReport,
    void *pvMeasureEID,
    BYTE  byReportMode,
    BYTE  byBasicMap,
    BYTE  byCCAFraction,
    PBYTE pbyRPIs
    );

BOOL
VNTWIFIbChannelSwitch(
    void *pMgmtObject,
    BYTE  byNewChannel
    );
/*
BOOL
VNTWIFIbRadarPresent(
    void *pMgmtObject,
    BYTE  byChannel
    );
*/

#endif //__VNTWIFI_H__
