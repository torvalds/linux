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
 * File: wmgr.h
 *
 * Purpose:
 *
 * Author: lyndon chen
 *
 * Date: Jan 2, 2003
 *
 * Functions:
 *
 * Revision History:
 *
 */

#ifndef __WMGR_H__
#define __WMGR_H__

#include "80211mgr.h"
#include "80211hdr.h"
#include "wcmd.h"
#include "bssdb.h"
#include "wpa2.h"
#include "card.h"

// Scan time
#define PROBE_DELAY                  100  // (us)
#define SWITCH_CHANNEL_DELAY         200 // (us)
#define WLAN_SCAN_MINITIME           25   // (ms)
#define WLAN_SCAN_MAXTIME            100  // (ms)
#define TRIVIAL_SYNC_DIFFERENCE      0    // (us)
#define DEFAULT_IBSS_BI              100  // (ms)

#define WCMD_ACTIVE_SCAN_TIME   20 //(ms)
#define WCMD_PASSIVE_SCAN_TIME  100 //(ms)

#define DEFAULT_MSDU_LIFETIME           512  // ms
#define DEFAULT_MSDU_LIFETIME_RES_64us  8000 // 64us

#define DEFAULT_MGN_LIFETIME            8    // ms
#define DEFAULT_MGN_LIFETIME_RES_64us   125  // 64us

#define MAKE_BEACON_RESERVED            10  //(us)

#define TIM_MULTICAST_MASK           0x01
#define TIM_BITMAPOFFSET_MASK        0xFE
#define DEFAULT_DTIM_PERIOD             1

#define AP_LONG_RETRY_LIMIT             4

#define DEFAULT_IBSS_CHANNEL            6  //2.4G

//mike define: make timer  to expire after desired times
#define timer_expire(timer, next_tick) mod_timer(&timer, RUN_AT(next_tick))

typedef void (*TimerFunction)(unsigned long);

//+++ NDIS related

typedef u8 NDIS_802_11_MAC_ADDRESS[ETH_ALEN];
typedef struct _NDIS_802_11_AI_REQFI
{
	u16 Capabilities;
	u16 ListenInterval;
    NDIS_802_11_MAC_ADDRESS  CurrentAPAddress;
} NDIS_802_11_AI_REQFI, *PNDIS_802_11_AI_REQFI;

typedef struct _NDIS_802_11_AI_RESFI
{
	u16 Capabilities;
	u16 StatusCode;
	u16 AssociationId;
} NDIS_802_11_AI_RESFI, *PNDIS_802_11_AI_RESFI;

typedef struct _NDIS_802_11_ASSOCIATION_INFORMATION
{
	u32 Length;
	u16 AvailableRequestFixedIEs;
	NDIS_802_11_AI_REQFI RequestFixedIEs;
	u32 RequestIELength;
	u32 OffsetRequestIEs;
	u16 AvailableResponseFixedIEs;
	NDIS_802_11_AI_RESFI ResponseFixedIEs;
	u32 ResponseIELength;
	u32 OffsetResponseIEs;
} NDIS_802_11_ASSOCIATION_INFORMATION, *PNDIS_802_11_ASSOCIATION_INFORMATION;

typedef struct tagSAssocInfo {
	NDIS_802_11_ASSOCIATION_INFORMATION AssocInfo;
	u8 abyIEs[WLAN_BEACON_FR_MAXLEN+WLAN_BEACON_FR_MAXLEN];
	/* store ReqIEs set by OID_802_11_ASSOCIATION_INFORMATION */
	u32 RequestIELength;
	u8 abyReqIEs[WLAN_BEACON_FR_MAXLEN];
} SAssocInfo, *PSAssocInfo;

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

// Pre-configured Mode (from XP)

typedef enum tagWMAC_CONFIG_MODE {
    WMAC_CONFIG_ESS_STA,
    WMAC_CONFIG_IBSS_STA,
    WMAC_CONFIG_AUTO,
    WMAC_CONFIG_AP

} WMAC_CONFIG_MODE, *PWMAC_CONFIG_MODE;

typedef enum tagWMAC_SCAN_TYPE {

    WMAC_SCAN_ACTIVE,
    WMAC_SCAN_PASSIVE,
    WMAC_SCAN_HYBRID

} WMAC_SCAN_TYPE, *PWMAC_SCAN_TYPE;

typedef enum tagWMAC_SCAN_STATE {

    WMAC_NO_SCANNING,
    WMAC_IS_SCANNING,
    WMAC_IS_PROBEPENDING

} WMAC_SCAN_STATE, *PWMAC_SCAN_STATE;

// Notes:
// Basic Service Set state explained as following:
// WMAC_STATE_IDLE          : no BSS is selected (Adhoc or Infra)
// WMAC_STATE_STARTED       : no BSS is selected, start own IBSS (Adhoc only)
// WMAC_STATE_JOINTED       : BSS is selected and synchronized (Adhoc or Infra)
// WMAC_STATE_AUTHPENDING   : Authentication pending (Infra)
// WMAC_STATE_AUTH          : Authenticated (Infra)
// WMAC_STATE_ASSOCPENDING  : Association pending (Infra)
// WMAC_STATE_ASSOC         : Associated (Infra)

typedef enum tagWMAC_BSS_STATE {

    WMAC_STATE_IDLE,
    WMAC_STATE_STARTED,
    WMAC_STATE_JOINTED,
    WMAC_STATE_AUTHPENDING,
    WMAC_STATE_AUTH,
    WMAC_STATE_ASSOCPENDING,
    WMAC_STATE_ASSOC

} WMAC_BSS_STATE, *PWMAC_BSS_STATE;

// WMAC selected running mode
typedef enum tagWMAC_CURRENT_MODE {

    WMAC_MODE_STANDBY,
    WMAC_MODE_ESS_STA,
    WMAC_MODE_IBSS_STA,
    WMAC_MODE_ESS_AP

} WMAC_CURRENT_MODE, *PWMAC_CURRENT_MODE;

typedef enum tagWMAC_POWER_MODE {

    WMAC_POWER_CAM,
    WMAC_POWER_FAST,
    WMAC_POWER_MAX

} WMAC_POWER_MODE, *PWMAC_POWER_MODE;

/* Tx Management Packet descriptor */
struct vnt_tx_mgmt {
	PUWLAN_80211HDR p80211Header;
	u32 cbMPDULen;
	u32 cbPayloadLen;
};

/* Rx Management Packet descriptor */
struct vnt_rx_mgmt {
	PUWLAN_80211HDR p80211Header;
	u64 qwLocalTSF;
	u32 cbMPDULen;
	u32 cbPayloadLen;
	u32 uRSSI;
	u8 bySQ;
	u8 byRxRate;
	u8 byRxChannel;
};

struct vnt_manager {
	void *pAdapter;

	/* MAC address */
	u8  abyMACAddr[WLAN_ADDR_LEN];

	/* Configuration Mode */
	WMAC_CONFIG_MODE eConfigMode; /* MAC pre-configed mode */

	CARD_PHY_TYPE eCurrentPHYMode;

	/* Operation state variables */
	WMAC_CURRENT_MODE eCurrMode; /* MAC current connection mode */
	WMAC_BSS_STATE eCurrState; /* MAC current BSS state */
	WMAC_BSS_STATE eLastState; /* MAC last BSS state */

	PKnownBSS pCurrBSS;
	u8 byCSSGK;
	u8 byCSSPK;

	int bCurrBSSIDFilterOn;

	/* Current state vars */
	u32 uCurrChannel;
	u8 abyCurrSuppRates[WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1];
	u8 abyCurrExtSuppRates[WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1];
	u8 abyCurrSSID[WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1];
	u8 abyCurrBSSID[WLAN_BSSID_LEN];
	u16 wCurrCapInfo;
	u16 wCurrAID;
	u32 uRSSITrigger;
	u16 wCurrATIMWindow;
	u16 wCurrBeaconPeriod;
	int bIsDS;
	u8 byERPContext;

	CMD_STATE eCommandState;
	u32 uScanChannel;

	/* Desire joinning BSS vars */
	u8 abyDesireSSID[WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1];
	u8 abyDesireBSSID[WLAN_BSSID_LEN];

	/*restore BSS info for Ad-Hoc mode */
	u8 abyAdHocSSID[WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1];

	/* Adhoc or AP configuration vars */
	u16 wIBSSBeaconPeriod;
	u16 wIBSSATIMWindow;
	u32 uIBSSChannel;
	u8 abyIBSSSuppRates[WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1];
	u8 byAPBBType;
	u8 abyWPAIE[MAX_WPA_IE_LEN];
	u16 wWPAIELen;

	u32 uAssocCount;
	int bMoreData;

	/* Scan state vars */
	WMAC_SCAN_STATE eScanState;
	WMAC_SCAN_TYPE eScanType;
	u32 uScanStartCh;
	u32 uScanEndCh;
	u16 wScanSteps;
	u32 uScanBSSType;
	/* Desire scannig vars */
	u8 abyScanSSID[WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1];
	u8 abyScanBSSID[WLAN_BSSID_LEN];

	/* Privacy */
	WMAC_AUTHENTICATION_MODE eAuthenMode;
	int bShareKeyAlgorithm;
	u8 abyChallenge[WLAN_CHALLENGE_LEN];
	int bPrivacyInvoked;

	/* Received beacon state vars */
	int bInTIM;
	int bMulticastTIM;
	u8 byDTIMCount;
	u8 byDTIMPeriod;

	/* Power saving state vars */
	WMAC_POWER_MODE ePSMode;
	u16 wListenInterval;
	u16 wCountToWakeUp;
	int bInTIMWake;
	u8 *pbyPSPacketPool;
	u8 byPSPacketPool[sizeof(struct vnt_tx_mgmt)
		+ WLAN_NULLDATA_FR_MAXLEN];
	int bRxBeaconInTBTTWake;
	u8 abyPSTxMap[MAX_NODE_NUM + 1];

	/* management command related */
	u32 uCmdBusy;
	u32 uCmdHostAPBusy;

	/* management packet pool */
	u8 *pbyMgmtPacketPool;
	u8 byMgmtPacketPool[sizeof(struct vnt_tx_mgmt)
		+ WLAN_A3FR_MAXLEN];

	/* Temporarily Rx Mgmt Packet Descriptor */
	struct vnt_rx_mgmt sRxPacket;

	/* link list of known bss's (scan results) */
	KnownBSS sBSSList[MAX_BSS_NUM];
	/* link list of same bss's */
	KnownBSS pSameBSS[6];
	int Cisco_cckm;
	u8 Roam_dbm;

	/* table list of known node */
	/* sNodeDBList[0] is reserved for AP under Infra mode */
	/* sNodeDBList[0] is reserved for Multicast under adhoc/AP mode */
	KnownNodeDB sNodeDBTable[MAX_NODE_NUM + 1];

	/* WPA2 PMKID Cache */
	SPMKIDCache gsPMKIDCache;
	int bRoaming;

	/* associate info */
	SAssocInfo sAssocInfo;

	/* for 802.11h */
	int b11hEnable;
	int bSwitchChannel;
	u8 byNewChannel;
	PWLAN_IE_MEASURE_REP    pCurrMeasureEIDRep;
	u32 uLengthOfRepEIDs;
	u8 abyCurrentMSRReq[sizeof(struct vnt_tx_mgmt)
		+ WLAN_A3FR_MAXLEN];
	u8 abyCurrentMSRRep[sizeof(struct vnt_tx_mgmt)
		+ WLAN_A3FR_MAXLEN];
	u8 abyIECountry[WLAN_A3FR_MAXLEN];
	u8 abyIBSSDFSOwner[6];
	u8 byIBSSDFSRecovery;

	struct sk_buff skb;

};

void vMgrObjectInit(struct vnt_private *pDevice);

void vMgrAssocBeginSta(struct vnt_private *pDevice,
		struct vnt_manager *, PCMD_STATUS pStatus);

void vMgrReAssocBeginSta(struct vnt_private *pDevice,
		struct vnt_manager *, PCMD_STATUS pStatus);

void vMgrDisassocBeginSta(struct vnt_private *pDevice,
	struct vnt_manager *, u8 *abyDestAddress, u16 wReason,
	PCMD_STATUS pStatus);

void vMgrAuthenBeginSta(struct vnt_private *pDevice,
	struct vnt_manager *, PCMD_STATUS pStatus);

void vMgrCreateOwnIBSS(struct vnt_private *pDevice,
	PCMD_STATUS pStatus);

void vMgrJoinBSSBegin(struct vnt_private *pDevice,
	PCMD_STATUS pStatus);

void vMgrRxManagePacket(struct vnt_private *pDevice,
	struct vnt_manager *, struct vnt_rx_mgmt *);

/*
void
vMgrScanBegin(
      void *hDeviceContext,
     PCMD_STATUS pStatus
    );
*/

void vMgrDeAuthenBeginSta(struct vnt_private *pDevice,
	struct vnt_manager *, u8 *abyDestAddress, u16 wReason,
	PCMD_STATUS pStatus);

int bMgrPrepareBeaconToSend(struct vnt_private *pDevice,
	struct vnt_manager *);

int bAdd_PMKID_Candidate(struct vnt_private *pDevice,
	u8 *pbyBSSID, PSRSNCapObject psRSNCapObj);

void vFlush_PMKID_Candidate(struct vnt_private *pDevice);

#endif /* __WMGR_H__ */
