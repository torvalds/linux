/*
** $Id: @(#) gl_p2p_cfg80211.c@@
*/

/*! \file   gl_p2p_cfg80211.c
    \brief  Main routines of Linux driver interface for Wi-Fi Direct
            using cfg80211 interface

    This file contains the main routines of Linux driver for MediaTek Inc. 802.11
    Wireless LAN Adapters.
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
** $Log: gl_p2p_cfg80211.c $
** 
** 08 30 2012 yuche.tsai
** NULL
** Fix disconnect issue possible leads KE.
** 
** 08 24 2012 cp.wu
** [WCXRP00001269] [MT6620 Wi-Fi][Driver] cfg80211 porting merge back to DaVinci
** .
** 
** 08 24 2012 cp.wu
** [WCXRP00001269] [MT6620 Wi-Fi][Driver] cfg80211 porting merge back to DaVinci
** cfg80211 support merge back from ALPS.JB to DaVinci - MT6620 Driver v2.3 branch.
** 
** 08 24 2012 yuche.tsai
** NULL
** Fix bug of invitation request.
** 
** 08 20 2012 yuche.tsai
** NULL
** Try to fix frame register KE issue.
** 
** 08 17 2012 yuche.tsai
** NULL
** Fix compile warning.
** 
** 08 16 2012 yuche.tsai
** NULL
** Fix compile warning.
** 
** 08 08 2012 yuche.tsai
** NULL
** Fix bug of hard scan p2p device sometimes
** 
** 08 08 2012 wh.su
** [WCXRP00001246] [MT6620 Wi-Fi][Driver][P2P] Do more filed check for avoid not copy the STA mac address for add key[WCXRP00001262] [MT6620 Wi-Fi][Driver] Fixed the update assoc info pkt length issue
** .
**
** 08 06 2012 yuche.tsai
** [WCXRP00001119] [Volunteer Patch][WiFi Direct][Driver] Connection Policy Set for WFD SIGMA test
** Fix P2P reset would not reset QoS BSS info issue.
**
** 07 31 2012 yuche.tsai
** NULL
** Update Active/Deactive network policy for P2P network.
** Highly related to power saving.
**
** 07 25 2012 yuche.tsai
** NULL
** Add support for null mac address del station.
**
** 07 25 2012 yuche.tsai
** NULL
** Bug fix.
**
** 07 25 2012 yuche.tsai
** NULL
** Bug fix for TX mgmt frame.
**
** 07 24 2012 yuche.tsai
** NULL
** Bug fix for JB.
**
** 07 19 2012 yuche.tsai
** NULL
** Code update for JB.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Fix compile error for JB.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 09 21 2010 kevin.huang
 * [WCXRP00000054] [MT6620 Wi-Fi][Driver] Restructure driver for second Interface
 * Isolate P2P related function for Hardware Software Bundle
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 31 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add cfg80211 interface, which is to replace WE, for further extension
 *
**
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "config.h"

#if CFG_ENABLE_WIFI_DIRECT && CFG_ENABLE_WIFI_DIRECT_CFG_80211
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>

#include "precomp.h"
#ifdef CFG_DUAL_ANTENNA
#include "mtk_porting.h"
#include "dual_ant_bwcs.h"
#endif



/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

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


BOOLEAN
mtk_p2p_cfg80211func_channel_format_switch(
    IN struct ieee80211_channel *channel,
    IN enum nl80211_channel_type channel_type,
    IN P_RF_CHANNEL_INFO_T prRfChnlInfo,
    IN P_ENUM_CHNL_EXT_T prChnlSco
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
int mtk_p2p_cfg80211_add_key(
    struct wiphy *wiphy,
    struct net_device *ndev,
    u8 key_index,
    bool pairwise,
    const u8 *mac_addr,
    struct key_params *params
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
	INT_32 i4Rslt = -EINVAL;
	WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
	UINT_32 u4BufLen = 0;
	P2P_PARAM_KEY_T rKey;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    kalMemZero(&rKey, sizeof(P2P_PARAM_KEY_T));

	rKey.u4KeyIndex = key_index;
    if(mac_addr) {
        memcpy(rKey.arBSSID, mac_addr, ETH_ALEN);
        if ((rKey.arBSSID[0] == 0x00) && (rKey.arBSSID[1] == 0x00) && (rKey.arBSSID[2] == 0x00) && 
            (rKey.arBSSID[3] == 0x00) && (rKey.arBSSID[4] == 0x00) && (rKey.arBSSID[5] == 0x00)) {
            rKey.arBSSID[0] = 0xff;
            rKey.arBSSID[1] = 0xff;
            rKey.arBSSID[2] = 0xff;
            rKey.arBSSID[3] = 0xff;
            rKey.arBSSID[4] = 0xff;
            rKey.arBSSID[5] = 0xff;
        }
        if (rKey.arBSSID[0] != 0xFF) {
            rKey.u4KeyIndex |= BIT(31);
            if ((rKey.arBSSID[0] != 0x00) || (rKey.arBSSID[1] != 0x00) || (rKey.arBSSID[2] != 0x00) ||
                (rKey.arBSSID[3] != 0x00) || (rKey.arBSSID[4] != 0x00) || (rKey.arBSSID[5] != 0x00))
                rKey.u4KeyIndex |= BIT(30);
        }
        else {
            rKey.u4KeyIndex |= BIT(31);
        }
    }
    else {
            rKey.arBSSID[0] = 0xff;
            rKey.arBSSID[1] = 0xff;
            rKey.arBSSID[2] = 0xff;
            rKey.arBSSID[3] = 0xff;
            rKey.arBSSID[4] = 0xff;
            rKey.arBSSID[5] = 0xff;
            rKey.u4KeyIndex |= BIT(31); //????
    }
	if(params->key)
	{
        //rKey.aucKeyMaterial[0] = kalMemAlloc(params->key_len, VIR_MEM_TYPE);
	    kalMemCopy(rKey.aucKeyMaterial, params->key, params->key_len);
	}
	rKey.u4KeyLength = params->key_len;
        rKey.u4Length =  ((UINT_32)&(((P_P2P_PARAM_KEY_T)0)->aucKeyMaterial)) + rKey.u4KeyLength;

	rStatus = kalIoctl(prGlueInfo,
            wlanoidSetAddP2PKey,
            &rKey,
            rKey.u4Length,
            FALSE,
            FALSE,
            TRUE,
            TRUE,
            &u4BufLen);
    if (rStatus == WLAN_STATUS_SUCCESS)
		i4Rslt = 0;

    return i4Rslt;
}


int mtk_p2p_cfg80211_get_key(
    struct wiphy *wiphy,
    struct net_device *ndev,
    u8 key_index,
    bool pairwise,
    const u8 *mac_addr,
    void *cookie,
    void (*callback)(void *cookie, struct key_params*)
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    // not implemented yet

    return -EINVAL;
}

int mtk_p2p_cfg80211_del_key(
    struct wiphy *wiphy,
    struct net_device *ndev,
    u8 key_index,
    bool pairwise,
    const u8 *mac_addr
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
	PARAM_REMOVE_KEY_T prRemoveKey;
	INT_32 i4Rslt = -EINVAL;
	WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
	UINT_32 u4BufLen = 0;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    kalMemZero(&prRemoveKey, sizeof(PARAM_REMOVE_KEY_T));
	if(mac_addr)
		memcpy(prRemoveKey.arBSSID, mac_addr, PARAM_MAC_ADDR_LEN);
	prRemoveKey.u4KeyIndex = key_index;
	prRemoveKey.u4Length = sizeof(PARAM_REMOVE_KEY_T);

    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetRemoveP2PKey,
            &prRemoveKey,
            prRemoveKey.u4Length,
            FALSE,
            FALSE,
            TRUE,
            TRUE,
            &u4BufLen);

    if (rStatus == WLAN_STATUS_SUCCESS)
		i4Rslt = 0;

    return i4Rslt;
}


int
mtk_p2p_cfg80211_set_default_key (
    struct wiphy *wiphy,
    struct net_device *netdev,
    u8 key_index,
    bool unicast,
    bool multicast
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    // not implemented yet

    return 0;
}

int mtk_p2p_cfg80211_get_station(
    struct wiphy *wiphy,
    struct net_device *ndev,
    u8 *mac,
    struct station_info *sinfo
    )
{
    INT_32 i4RetRslt = -EINVAL;
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    P_GL_P2P_INFO_T prP2pGlueInfo = (P_GL_P2P_INFO_T)NULL;
    P2P_STATION_INFO_T rP2pStaInfo;

    ASSERT(wiphy);

    do {
        if ((wiphy == NULL) ||
                (ndev == NULL) ||
                (sinfo == NULL) ||
                (mac == NULL)) {
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_get_station\n"));

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));
        prP2pGlueInfo = prGlueInfo->prP2PInfo;

        sinfo->filled = 0;

        /* Get station information. */
        /* 1. Inactive time? */
        p2pFuncGetStationInfo(prGlueInfo->prAdapter,
                                                mac,
                                                &rP2pStaInfo);

        /* Inactive time. */
        sinfo->filled |= STATION_INFO_INACTIVE_TIME;
        sinfo->inactive_time = rP2pStaInfo.u4InactiveTime;
        sinfo->generation = prP2pGlueInfo->i4Generation;

        i4RetRslt = 0;
    } while (FALSE);

    return i4RetRslt;
}

int
mtk_p2p_cfg80211_scan (
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_scan_request *request
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    P_GL_P2P_INFO_T prP2pGlueInfo = (P_GL_P2P_INFO_T)NULL;
    P_MSG_P2P_SCAN_REQUEST_T prMsgScanRequest = (P_MSG_P2P_SCAN_REQUEST_T)NULL;
    UINT_32 u4MsgSize = 0, u4Idx = 0;
    INT_32 i4RetRslt = -EINVAL;
    P_RF_CHANNEL_INFO_T prRfChannelInfo = (P_RF_CHANNEL_INFO_T)NULL;
    P_P2P_SSID_STRUCT_T prSsidStruct = (P_P2P_SSID_STRUCT_T)NULL;
    struct ieee80211_channel *prChannel = NULL;
    struct cfg80211_ssid *prSsid = NULL;

    /* [---------Channel---------] [---------SSID---------][---------IE---------] */


    do {
        if ((wiphy == NULL) || (request == NULL)) {
            break;
        }

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        prP2pGlueInfo = prGlueInfo->prP2PInfo;

        if (prP2pGlueInfo == NULL) {
            ASSERT(FALSE);
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_scan.\n"));


        if (prP2pGlueInfo->prScanRequest != NULL) {
            /* There have been a scan request on-going processing. */
            DBGLOG(P2P, TRACE, ("There have been a scan request on-going processing.\n"));
            break;
        }

        prP2pGlueInfo->prScanRequest = request;

        /* Should find out why the n_channels so many? */
        if (request->n_channels > MAXIMUM_OPERATION_CHANNEL_LIST) {
            request->n_channels = MAXIMUM_OPERATION_CHANNEL_LIST;
            DBGLOG(P2P, TRACE, ("Channel list exceed the maximun support.\n"));
        }

        u4MsgSize = sizeof(MSG_P2P_SCAN_REQUEST_T) +
                                    (request->n_channels * sizeof(RF_CHANNEL_INFO_T)) +
                                    (request->n_ssids * sizeof(PARAM_SSID_T)) +
                                    request->ie_len;

        prMsgScanRequest = cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, u4MsgSize);

        if (prMsgScanRequest == NULL) {
            ASSERT(FALSE);
            i4RetRslt = -ENOMEM;
            break;
        }

        DBGLOG(P2P, TRACE, ("Generating scan request message.\n"));

        prMsgScanRequest->rMsgHdr.eMsgId = MID_MNY_P2P_DEVICE_DISCOVERY;



        DBGLOG(P2P, TRACE, ("Requesting channel number:%d.\n", request->n_channels));

        for (u4Idx = 0; u4Idx < request->n_channels; u4Idx++) {
            /* Translate Freq from MHz to channel number. */
            prRfChannelInfo = &(prMsgScanRequest->arChannelListInfo[u4Idx]);
            prChannel = request->channels[u4Idx];

            prRfChannelInfo->ucChannelNum = nicFreq2ChannelNum(prChannel->center_freq * 1000);
            DBGLOG(P2P, TRACE, ("Scanning Channel:%d,  freq: %d\n",
                                                        prRfChannelInfo->ucChannelNum,
                                                        prChannel->center_freq));
            switch (prChannel->band) {
            case IEEE80211_BAND_2GHZ:
                prRfChannelInfo->eBand = BAND_2G4;
                break;
            case IEEE80211_BAND_5GHZ:
                prRfChannelInfo->eBand = BAND_5G;
                break;
            default:
                DBGLOG(P2P, TRACE, ("UNKNOWN Band info from supplicant\n"));
                prRfChannelInfo->eBand = BAND_NULL;
                break;
            }

            /* Iteration. */
            prRfChannelInfo++;
        }
        prMsgScanRequest->u4NumChannel = request->n_channels;

        DBGLOG(P2P, TRACE, ("Finish channel list.\n"));

        /* SSID */
        prSsid = request->ssids;
        prSsidStruct = (P_P2P_SSID_STRUCT_T)prRfChannelInfo;
        if (request->n_ssids) {
            ASSERT(prSsidStruct == &(prMsgScanRequest->arChannelListInfo[u4Idx]));
            prMsgScanRequest->prSSID = prSsidStruct;
        }

        for (u4Idx = 0; u4Idx < request->n_ssids; u4Idx++) {
            COPY_SSID(prSsidStruct->aucSsid,
                            prSsidStruct->ucSsidLen,
                            request->ssids->ssid,
                            request->ssids->ssid_len);

            prSsidStruct++;
            prSsid++;
        }

        prMsgScanRequest->i4SsidNum = request->n_ssids;

        DBGLOG(P2P, TRACE, ("Finish SSID list:%d.\n", request->n_ssids));

        /* IE BUFFERS */
        prMsgScanRequest->pucIEBuf = (PUINT_8)prSsidStruct;
        if (request->ie_len) {
            kalMemCopy(prMsgScanRequest->pucIEBuf, request->ie, request->ie_len);
            prMsgScanRequest->u4IELen = request->ie_len;
        }

        DBGLOG(P2P, TRACE, ("Finish IE Buffer.\n"));


        mboxSendMsg(prGlueInfo->prAdapter,
                MBOX_ID_0,
                (P_MSG_HDR_T)prMsgScanRequest,
                MSG_SEND_METHOD_BUF);

        i4RetRslt = 0;
    } while (FALSE);

    return i4RetRslt;
} /* mtk_p2p_cfg80211_scan */

int mtk_p2p_cfg80211_set_wiphy_params(
    struct wiphy *wiphy,
    u32 changed
    )
{
    INT_32 i4Rslt = -EINVAL;
    P_GLUE_INFO_T prGlueInfo = NULL;


    do {
        if (wiphy == NULL) {
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_set_wiphy_params\n"));
        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        if (changed & WIPHY_PARAM_RETRY_SHORT) {
            // TODO:
            DBGLOG(P2P, TRACE, ("The RETRY short param is changed.\n"));
        }

        if (changed & WIPHY_PARAM_RETRY_LONG) {
            // TODO:
            DBGLOG(P2P, TRACE, ("The RETRY long param is changed.\n"));
        }


        if (changed & WIPHY_PARAM_FRAG_THRESHOLD) {
            // TODO:
            DBGLOG(P2P, TRACE, ("The RETRY fragmentation threshold is changed.\n"));
        }

        if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
            // TODO:
            DBGLOG(P2P, TRACE, ("The RETRY RTS threshold is changed.\n"));
        }

        if (changed & WIPHY_PARAM_COVERAGE_CLASS) {
            // TODO:
            DBGLOG(P2P, TRACE, ("The coverage class is changed???\n"));
        }

        i4Rslt = 0;
    } while (FALSE);




    return i4Rslt;
} /* mtk_p2p_cfg80211_set_wiphy_params */



int
mtk_p2p_cfg80211_join_ibss(
    struct wiphy *wiphy,
    struct net_device *dev,
    struct cfg80211_ibss_params *params
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    // not implemented yet

    return -EINVAL;
}

int
mtk_p2p_cfg80211_leave_ibss(
    struct wiphy *wiphy,
    struct net_device *dev
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    // not implemented yet

    return -EINVAL;
}

int
mtk_p2p_cfg80211_set_txpower(
    struct wiphy *wiphy,
    enum nl80211_tx_power_setting type,
    int mbm
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    // not implemented yet

    return -EINVAL;
}

int
mtk_p2p_cfg80211_get_txpower(
    struct wiphy *wiphy,
    int *dbm
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    // not implemented yet

    return -EINVAL;
}

int
mtk_p2p_cfg80211_set_power_mgmt(
    struct wiphy *wiphy,
    struct net_device *dev,
    bool enabled,
    int timeout
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
	PARAM_POWER_MODE ePowerMode;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

	INT_32   value = enabled;
	UINT_32 u4Leng;
	WLAN_STATUS rStatus;
    if(enabled) {
        if(timeout == -1) {
            ePowerMode = Param_PowerModeFast_PSP;
        }
        else {
            ePowerMode = Param_PowerModeMAX_PSP;
        }
    }
    else {
        ePowerMode = Param_PowerModeCAM;
    }

	rStatus	= kalIoctl(prGlueInfo,
		        wlanoidSetP2pPowerSaveProfile,
		        &ePowerMode,
		        sizeof(ePowerMode),
		        FALSE,
		        FALSE,
		        TRUE,
		        TRUE,
		        &u4Leng);
	
    if (rStatus != WLAN_STATUS_SUCCESS) {
    DBGLOG(REQ, WARN, ("p2p set_power_mgmt error:%lx\n", rStatus));
    return -EFAULT;
    }

		 return 0;
}

bool start_beacon;

//&&&&&&&&&&&&&&&&&&&&&&&&&& Add for ICS Wi-Fi Direct Support. &&&&&&&&&&&&&&&&&&&&&&&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
int
mtk_p2p_cfg80211_start_ap (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct cfg80211_ap_settings *settings
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    INT_32 i4Rslt = -EINVAL;
    P_MSG_P2P_BEACON_UPDATE_T prP2pBcnUpdateMsg = (P_MSG_P2P_BEACON_UPDATE_T)NULL;
    P_MSG_P2P_START_AP_T prP2pStartAPMsg = (P_MSG_P2P_START_AP_T)NULL;
    PUINT_8 pucBuffer = (PUINT_8)NULL;
//    P_IE_SSID_T prSsidIE = (P_IE_SSID_T)NULL;
	start_beacon = true;

    do {
        if ((wiphy == NULL) || (settings == NULL)) {
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_start_ap.\n"));
        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        prP2pBcnUpdateMsg = (P_MSG_P2P_BEACON_UPDATE_T)cnmMemAlloc(
                                                                prGlueInfo->prAdapter,
                                                                RAM_TYPE_MSG,
                                                                (sizeof(MSG_P2P_BEACON_UPDATE_T) + settings->beacon.head_len + settings->beacon.tail_len));

        if (prP2pBcnUpdateMsg == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }


        prP2pBcnUpdateMsg->rMsgHdr.eMsgId = MID_MNY_P2P_BEACON_UPDATE;
        pucBuffer = prP2pBcnUpdateMsg->aucBuffer;

        if (settings->beacon.head_len != 0) {
            kalMemCopy(pucBuffer, settings->beacon.head, settings->beacon.head_len);

            prP2pBcnUpdateMsg->u4BcnHdrLen = settings->beacon.head_len;

            prP2pBcnUpdateMsg->pucBcnHdr = pucBuffer;

            pucBuffer = (PUINT_8)((UINT_32)pucBuffer + (UINT_32)settings->beacon.head_len);
        }
        else {
            prP2pBcnUpdateMsg->u4BcnHdrLen = 0;

            prP2pBcnUpdateMsg->pucBcnHdr = NULL;
        }

        if (settings->beacon.tail_len != 0) {
            UINT_8 ucLen = settings->beacon.tail_len;

            prP2pBcnUpdateMsg->pucBcnBody = pucBuffer;

            /*Add TIM IE*/
            // IEEE 802.11 2007 - 7.3.2.6
            TIM_IE(pucBuffer)->ucId = ELEM_ID_TIM;
            TIM_IE(pucBuffer)->ucLength = (3 + MAX_LEN_TIM_PARTIAL_BMP)/*((u4N2 - u4N1) + 4)*/; // NOTE: fixed PVB length (AID is allocated from 8 ~ 15 only)
            TIM_IE(pucBuffer)->ucDTIMCount = 0/*prBssInfo->ucDTIMCount*/; // will be overwrite by FW
            TIM_IE(pucBuffer)->ucDTIMPeriod = 1;
            TIM_IE(pucBuffer)->ucBitmapControl = 0/*ucBitmapControl | (UINT_8)u4N1*/; // will be overwrite by FW
            ucLen += IE_SIZE(pucBuffer);
            pucBuffer += IE_SIZE(pucBuffer);

            kalMemCopy(pucBuffer, settings->beacon.tail, settings->beacon.tail_len);

            prP2pBcnUpdateMsg->u4BcnBodyLen = ucLen;
        }
        else {
            prP2pBcnUpdateMsg->u4BcnBodyLen = 0;

            prP2pBcnUpdateMsg->pucBcnBody = NULL;
        }


        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prP2pBcnUpdateMsg,
                            MSG_SEND_METHOD_BUF);


        prP2pStartAPMsg = (P_MSG_P2P_START_AP_T)cnmMemAlloc(
                                                        prGlueInfo->prAdapter,
                                                        RAM_TYPE_MSG,
                                                        sizeof(MSG_P2P_START_AP_T));

        if (prP2pStartAPMsg == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        prP2pStartAPMsg->rMsgHdr.eMsgId = MID_MNY_P2P_START_AP;

        prP2pStartAPMsg->fgIsPrivacy = settings->privacy;

        prP2pStartAPMsg->u4BcnInterval = settings->beacon_interval;

        prP2pStartAPMsg->u4DtimPeriod = settings->dtim_period;

        /* Copy NO SSID. */
        prP2pStartAPMsg->ucHiddenSsidType = settings->hidden_ssid;

        COPY_SSID(prP2pStartAPMsg->aucSsid,
                        prP2pStartAPMsg->u2SsidLen,
                        settings->ssid,
                        settings->ssid_len);

        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prP2pStartAPMsg,
                            MSG_SEND_METHOD_BUF);

        i4Rslt = 0;

    } while (FALSE);


    return i4Rslt;


/////////////////////////
    /**
         * struct cfg80211_ap_settings - AP configuration
         *
         * Used to configure an AP interface.
         *
         * @beacon: beacon data
         * @beacon_interval: beacon interval
         * @dtim_period: DTIM period
         * @ssid: SSID to be used in the BSS (note: may be %NULL if not provided from
         *      user space)
         * @ssid_len: length of @ssid
         * @hidden_ssid: whether to hide the SSID in Beacon/Probe Response frames
         * @crypto: crypto settings
         * @privacy: the BSS uses privacy
         * @auth_type: Authentication type (algorithm)
         * @inactivity_timeout: time in seconds to determine station's inactivity.
         */
//        struct cfg80211_ap_settings {
//                struct cfg80211_beacon_data beacon;
//
//                int beacon_interval, dtim_period;
//                const u8 *ssid;
//                size_t ssid_len;
//                enum nl80211_hidden_ssid hidden_ssid;
//                struct cfg80211_crypto_settings crypto;
//                bool privacy;
//                enum nl80211_auth_type auth_type;
//                int inactivity_timeout;
//        };
////////////////////

    return i4Rslt;
} /* mtk_p2p_cfg80211_start_ap */


int
mtk_p2p_cfg80211_change_beacon (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct cfg80211_beacon_data *info
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    INT_32 i4Rslt = -EINVAL;
    P_MSG_P2P_BEACON_UPDATE_T prP2pBcnUpdateMsg = (P_MSG_P2P_BEACON_UPDATE_T)NULL;
    PUINT_8 pucBuffer = (PUINT_8)NULL;
	start_beacon = false;
	
    do {
        if ((wiphy == NULL) || (info == NULL)) {
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_change_beacon.\n"));
        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        prP2pBcnUpdateMsg = (P_MSG_P2P_BEACON_UPDATE_T)cnmMemAlloc(
                                                                prGlueInfo->prAdapter,
                                                                RAM_TYPE_MSG,
                                                                (sizeof(MSG_P2P_BEACON_UPDATE_T) + info->head_len + info->tail_len));


        if (prP2pBcnUpdateMsg == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        prP2pBcnUpdateMsg->rMsgHdr.eMsgId = MID_MNY_P2P_BEACON_UPDATE;
        pucBuffer = prP2pBcnUpdateMsg->aucBuffer;

        if (info->head_len != 0) {
            kalMemCopy(pucBuffer, info->head, info->head_len);

            prP2pBcnUpdateMsg->u4BcnHdrLen = info->head_len;

            prP2pBcnUpdateMsg->pucBcnHdr = pucBuffer;

            pucBuffer = (PUINT_8)((UINT_32)pucBuffer + (UINT_32)info->head_len);
        }
        else {
            prP2pBcnUpdateMsg->u4BcnHdrLen = 0;

            prP2pBcnUpdateMsg->pucBcnHdr = NULL;
        }

        if (info->tail_len != 0) {
            UINT_8 ucLen = info->tail_len;

            prP2pBcnUpdateMsg->pucBcnBody = pucBuffer;

            /*Add TIM IE*/
            // IEEE 802.11 2007 - 7.3.2.6
            TIM_IE(pucBuffer)->ucId = ELEM_ID_TIM;
            TIM_IE(pucBuffer)->ucLength = (3 + MAX_LEN_TIM_PARTIAL_BMP)/*((u4N2 - u4N1) + 4)*/; // NOTE: fixed PVB length (AID is allocated from 8 ~ 15 only)
            TIM_IE(pucBuffer)->ucDTIMCount = 0/*prBssInfo->ucDTIMCount*/; // will be overwrite by FW
            TIM_IE(pucBuffer)->ucDTIMPeriod = 1;
            TIM_IE(pucBuffer)->ucBitmapControl = 0/*ucBitmapControl | (UINT_8)u4N1*/; // will be overwrite by FW
            ucLen += IE_SIZE(pucBuffer);
            pucBuffer += IE_SIZE(pucBuffer);

            kalMemCopy(pucBuffer, info->tail, info->tail_len);

            prP2pBcnUpdateMsg->u4BcnBodyLen = ucLen;
        }
        else {
            prP2pBcnUpdateMsg->u4BcnBodyLen = 0;

            prP2pBcnUpdateMsg->pucBcnBody = NULL;
        }


        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prP2pBcnUpdateMsg,
                            MSG_SEND_METHOD_BUF);

////////////////////////////
/**
 * struct cfg80211_beacon_data - beacon data
 * @head: head portion of beacon (before TIM IE)
 *     or %NULL if not changed
 * @tail: tail portion of beacon (after TIM IE)
 *     or %NULL if not changed
 * @head_len: length of @head
 * @tail_len: length of @tail
 * @beacon_ies: extra information element(s) to add into Beacon frames or %NULL
 * @beacon_ies_len: length of beacon_ies in octets
 * @proberesp_ies: extra information element(s) to add into Probe Response
 *      frames or %NULL
 * @proberesp_ies_len: length of proberesp_ies in octets
 * @assocresp_ies: extra information element(s) to add into (Re)Association
 *      Response frames or %NULL
 * @assocresp_ies_len: length of assocresp_ies in octets
 * @probe_resp_len: length of probe response template (@probe_resp)
 * @probe_resp: probe response template (AP mode only)
 */
//struct cfg80211_beacon_data {
//        const u8 *head, *tail;
//        const u8 *beacon_ies;
//        const u8 *proberesp_ies;
//        const u8 *assocresp_ies;
//        const u8 *probe_resp;

//        size_t head_len, tail_len;
//        size_t beacon_ies_len;
//        size_t proberesp_ies_len;
//        size_t assocresp_ies_len;
//        size_t probe_resp_len;
//};

////////////////////////////

    } while (FALSE);

    return i4Rslt;
} /* mtk_p2p_cfg80211_change_beacon */

#else
int
mtk_p2p_cfg80211_add_set_beacon (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct beacon_parameters *info
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    INT_32 i4Rslt = -EINVAL;
    P_MSG_P2P_BEACON_UPDATE_T prP2pBcnUpdateMsg = (P_MSG_P2P_BEACON_UPDATE_T)NULL;
    P_MSG_P2P_START_AP_T prP2pStartAPMsg = (P_MSG_P2P_START_AP_T)NULL;
    PUINT_8 pucBuffer = (PUINT_8)NULL;
    P_IE_SSID_T prSsidIE = (P_IE_SSID_T)NULL;
	start_beacon = true;

    do {
        if ((wiphy == NULL) || (info == NULL)) {
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_add_set_beacon.\n"));
        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        prP2pBcnUpdateMsg = (P_MSG_P2P_BEACON_UPDATE_T)cnmMemAlloc(
                                                                prGlueInfo->prAdapter,
                                                                RAM_TYPE_MSG,
                                                                (sizeof(MSG_P2P_BEACON_UPDATE_T) + info->head_len + info->tail_len));

        if (prP2pBcnUpdateMsg == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }
	kalMemZero(prP2pBcnUpdateMsg, (sizeof(MSG_P2P_BEACON_UPDATE_T) + info->head_len + info->tail_len));

        prP2pBcnUpdateMsg->rMsgHdr.eMsgId = MID_MNY_P2P_BEACON_UPDATE;
        pucBuffer = prP2pBcnUpdateMsg->aucBuffer;

        if (info->head_len != 0) {
            kalMemCopy(pucBuffer, info->head, info->head_len);

            prP2pBcnUpdateMsg->u4BcnHdrLen = info->head_len;

            prP2pBcnUpdateMsg->pucBcnHdr = pucBuffer;

            pucBuffer = (PUINT_8)((UINT_32)pucBuffer + (UINT_32)info->head_len);
        }
        else {
            prP2pBcnUpdateMsg->u4BcnHdrLen = 0;

            prP2pBcnUpdateMsg->pucBcnHdr = NULL;
        }

        if (info->tail_len != 0) {
            UINT_8 ucLen = info->tail_len;

            prP2pBcnUpdateMsg->pucBcnBody = pucBuffer;

            /*Add TIM IE*/
            // IEEE 802.11 2007 - 7.3.2.6
            TIM_IE(pucBuffer)->ucId = ELEM_ID_TIM;
            TIM_IE(pucBuffer)->ucLength = (3 + MAX_LEN_TIM_PARTIAL_BMP)/*((u4N2 - u4N1) + 4)*/; // NOTE: fixed PVB length (AID is allocated from 8 ~ 15 only)
            TIM_IE(pucBuffer)->ucDTIMCount = 0/*prBssInfo->ucDTIMCount*/; // will be overwrite by FW
            TIM_IE(pucBuffer)->ucDTIMPeriod = 1;
            TIM_IE(pucBuffer)->ucBitmapControl = 0/*ucBitmapControl | (UINT_8)u4N1*/; // will be overwrite by FW
            ucLen += IE_SIZE(pucBuffer);
            pucBuffer += IE_SIZE(pucBuffer);

            kalMemCopy(pucBuffer, info->tail, info->tail_len);

            prP2pBcnUpdateMsg->u4BcnBodyLen = ucLen;
        }
        else {
            prP2pBcnUpdateMsg->u4BcnBodyLen = 0;

            prP2pBcnUpdateMsg->pucBcnBody = NULL;
        }


        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prP2pBcnUpdateMsg,
                            MSG_SEND_METHOD_BUF);


        prP2pStartAPMsg = (P_MSG_P2P_START_AP_T)cnmMemAlloc(
                                                        prGlueInfo->prAdapter,
                                                        RAM_TYPE_MSG,
                                                        sizeof(MSG_P2P_START_AP_T));

        if (prP2pStartAPMsg == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }
	kalMemZero(prP2pStartAPMsg, sizeof(MSG_P2P_START_AP_T));

        prP2pStartAPMsg->rMsgHdr.eMsgId = MID_MNY_P2P_START_AP;
		printk("mtk_p2p_cfg80211_add_set_beacon MID_MNY_P2P_START_AP");

        prP2pStartAPMsg->fgIsPrivacy = FALSE;

        prP2pStartAPMsg->u4BcnInterval = info->interval;

        prP2pStartAPMsg->u4DtimPeriod = info->dtim_period;

        /* Copy NO SSID. */
        prP2pStartAPMsg->ucHiddenSsidType = ENUM_HIDDEN_SSID_NONE;

#if 0
        if (info->head_len > OFFSET_OF(WLAN_BEACON_FRAME_T, aucInfoElem)) {
            P_WLAN_BEACON_FRAME_T prWlanBcnFrame = info->head;

            prSsidIE = (P_IE_HDR_T)p2pFuncGetSpecIE(prGlueInfo->prAdapter,
                                    (PUINT_8)prWlanBcnFrame->aucInfoElem,
                                    (info->head_len - OFFSET_OF(WLAN_BEACON_FRAME_T, aucInfoElem)),
                                    ELEM_ID_SSID,
                                    NULL);

            kalMemCopy(prP2pStartAPMsg->aucSsid, SSID_IE(prSsidIE)->aucSSID, IE_LEN(prSsidIE));

        }
#endif

        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prP2pStartAPMsg,
                            MSG_SEND_METHOD_BUF);

        i4Rslt = 0;

    } while (FALSE);

    return i4Rslt;
}
/* mtk_p2p_cfg80211_add_set_beacon */
int
mtk_p2p_cfg80211_add_set_beacon_1 (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct beacon_parameters *info
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    INT_32 i4Rslt = -EINVAL;
    P_MSG_P2P_BEACON_UPDATE_T prP2pBcnUpdateMsg = (P_MSG_P2P_BEACON_UPDATE_T)NULL;
    P_MSG_P2P_START_AP_T prP2pStartAPMsg = (P_MSG_P2P_START_AP_T)NULL;
    PUINT_8 pucBuffer = (PUINT_8)NULL;
    P_IE_SSID_T prSsidIE = (P_IE_SSID_T)NULL;
	start_beacon = false;

    do {
        if ((wiphy == NULL) || (info == NULL)) {
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_add_set_beacon_1.\n"));
        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        prP2pBcnUpdateMsg = (P_MSG_P2P_BEACON_UPDATE_T)cnmMemAlloc(
                                                                prGlueInfo->prAdapter,
                                                                RAM_TYPE_MSG,
                                                                (sizeof(MSG_P2P_BEACON_UPDATE_T) + info->head_len + info->tail_len));

        if (prP2pBcnUpdateMsg == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }
	kalMemZero(prP2pBcnUpdateMsg, (sizeof(MSG_P2P_BEACON_UPDATE_T) + info->head_len + info->tail_len));

        prP2pBcnUpdateMsg->rMsgHdr.eMsgId = MID_MNY_P2P_BEACON_UPDATE;
        pucBuffer = prP2pBcnUpdateMsg->aucBuffer;

        if (info->head_len != 0) {
            kalMemCopy(pucBuffer, info->head, info->head_len);

            prP2pBcnUpdateMsg->u4BcnHdrLen = info->head_len;

            prP2pBcnUpdateMsg->pucBcnHdr = pucBuffer;

            pucBuffer = (PUINT_8)((UINT_32)pucBuffer + (UINT_32)info->head_len);
        }
        else {
            prP2pBcnUpdateMsg->u4BcnHdrLen = 0;

            prP2pBcnUpdateMsg->pucBcnHdr = NULL;
        }

        if (info->tail_len != 0) {
            UINT_8 ucLen = info->tail_len;

            prP2pBcnUpdateMsg->pucBcnBody = pucBuffer;

            /*Add TIM IE*/
            // IEEE 802.11 2007 - 7.3.2.6
            TIM_IE(pucBuffer)->ucId = ELEM_ID_TIM;
            TIM_IE(pucBuffer)->ucLength = (3 + MAX_LEN_TIM_PARTIAL_BMP)/*((u4N2 - u4N1) + 4)*/; // NOTE: fixed PVB length (AID is allocated from 8 ~ 15 only)
            TIM_IE(pucBuffer)->ucDTIMCount = 0/*prBssInfo->ucDTIMCount*/; // will be overwrite by FW
            TIM_IE(pucBuffer)->ucDTIMPeriod = 1;
            TIM_IE(pucBuffer)->ucBitmapControl = 0/*ucBitmapControl | (UINT_8)u4N1*/; // will be overwrite by FW
            ucLen += IE_SIZE(pucBuffer);
            pucBuffer += IE_SIZE(pucBuffer);

            kalMemCopy(pucBuffer, info->tail, info->tail_len);

            prP2pBcnUpdateMsg->u4BcnBodyLen = ucLen;
        }
        else {
            prP2pBcnUpdateMsg->u4BcnBodyLen = 0;

            prP2pBcnUpdateMsg->pucBcnBody = NULL;
        }


        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prP2pBcnUpdateMsg,
                            MSG_SEND_METHOD_BUF);


        prP2pStartAPMsg = (P_MSG_P2P_START_AP_T)cnmMemAlloc(
                                                        prGlueInfo->prAdapter,
                                                        RAM_TYPE_MSG,
                                                        sizeof(MSG_P2P_START_AP_T));

        if (prP2pStartAPMsg == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }
	kalMemZero(prP2pStartAPMsg, sizeof(MSG_P2P_START_AP_T));

        prP2pStartAPMsg->rMsgHdr.eMsgId = MID_MNY_P2P_START_AP;
		printk("mtk_p2p_cfg80211_add_set_beacon_1 MID_MNY_P2P_START_AP");

        prP2pStartAPMsg->fgIsPrivacy = FALSE;

        prP2pStartAPMsg->u4BcnInterval = info->interval;

        prP2pStartAPMsg->u4DtimPeriod = info->dtim_period;

        /* Copy NO SSID. */
        prP2pStartAPMsg->ucHiddenSsidType = ENUM_HIDDEN_SSID_NONE;

#if 0
        if (info->head_len > OFFSET_OF(WLAN_BEACON_FRAME_T, aucInfoElem)) {
            P_WLAN_BEACON_FRAME_T prWlanBcnFrame = info->head;

            prSsidIE = (P_IE_HDR_T)p2pFuncGetSpecIE(prGlueInfo->prAdapter,
                                    (PUINT_8)prWlanBcnFrame->aucInfoElem,
                                    (info->head_len - OFFSET_OF(WLAN_BEACON_FRAME_T, aucInfoElem)),
                                    ELEM_ID_SSID,
                                    NULL);

            kalMemCopy(prP2pStartAPMsg->aucSsid, SSID_IE(prSsidIE)->aucSSID, IE_LEN(prSsidIE));

        }
#endif

        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prP2pStartAPMsg,
                            MSG_SEND_METHOD_BUF);

        i4Rslt = 0;

    } while (FALSE);

    return i4Rslt;
}
/* mtk_p2p_cfg80211_add_set_beacon */

#endif

int
mtk_p2p_cfg80211_stop_ap (
    struct wiphy *wiphy,
    struct net_device *dev
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    INT_32 i4Rslt = -EINVAL;
    P_MSG_P2P_SWITCH_OP_MODE_T prP2pSwitchMode = (P_MSG_P2P_SWITCH_OP_MODE_T)NULL;

    do {
        if (wiphy == NULL) {
            break;
        }


        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_stop_ap.\n"));
        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        // Switch OP MOde.
        prP2pSwitchMode = cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_SWITCH_OP_MODE_T));

        if (prP2pSwitchMode == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        prP2pSwitchMode->rMsgHdr.eMsgId = MID_MNY_P2P_STOP_AP;

        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prP2pSwitchMode,
                            MSG_SEND_METHOD_BUF);

        i4Rslt = 0;
    } while (FALSE);
	
#ifdef CFG_DUAL_ANTENNA
	if(p2pFuncIsAPMode(prGlueInfo->prAdapter->rWifiVar.prP2pFsmInfo))
		wifi2bwcs_connection_event_ind_handler(prGlueInfo->prAdapter->prGlueInfo, WIFI_EVENT_SOFTAP_CONN_DEL);
	else
		wifi2bwcs_connection_event_ind_handler(prGlueInfo->prAdapter->prGlueInfo, WIFI_EVENT_P2P_GO_CONN_DEL);
#endif


    return i4Rslt;
} /* mtk_p2p_cfg80211_stop_ap */

// TODO:
int
mtk_p2p_cfg80211_deauth (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct cfg80211_deauth_request *req
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
    , void *cookie
#endif
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    // not implemented yet
    DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_deauth.\n"));

    return -EINVAL;
} /* mtk_p2p_cfg80211_deauth */


// TODO:
int
mtk_p2p_cfg80211_disassoc (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct cfg80211_disassoc_request *req
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
    , void *cookie
#endif
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_disassoc.\n"));

    // not implemented yet

    return -EINVAL;
} /* mtk_p2p_cfg80211_disassoc */


#if 0//LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 0)
int
mtk_p2p_cfg80211_remain_on_channel2 (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct ieee80211_channel *chan,
    enum nl80211_channel_type channel_type,
    unsigned int duration,
    u64 *cookie,
    bool fgNeedIndSupp
    )
{
    INT_32 i4Rslt = -EINVAL;
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;
    P_MSG_P2P_CHNL_REQUEST_T prMsgChnlReq = (P_MSG_P2P_CHNL_REQUEST_T)NULL;


    do {
        if ((wiphy == NULL) ||
                (dev == NULL) ||
                (chan == NULL) ||
                (cookie == NULL)) {
            break;
        }
        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));
        prGlueP2pInfo = prGlueInfo->prP2PInfo;

        //*cookie = prGlueP2pInfo->u8Cookie++;

        prMsgChnlReq = cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_CHNL_REQUEST_T));

        if (prMsgChnlReq == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        //DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_remain_on_channel\n"));

        prMsgChnlReq->rMsgHdr.eMsgId = MID_MNY_P2P_CHNL_REQ;
        prMsgChnlReq->u8Cookie = *cookie;
        prMsgChnlReq->u4Duration = duration;
        prMsgChnlReq->fgNeedIndSupp = fgNeedIndSupp;


        mtk_p2p_cfg80211func_channel_format_switch(chan,
                                                    channel_type,
                                                    &prMsgChnlReq->rChannelInfo,
                                                    &prMsgChnlReq->eChnlSco);

        
	mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prMsgChnlReq,
                            MSG_SEND_METHOD_BUF);

        i4Rslt = 0;
    } while (FALSE);


    return i4Rslt;
}
/* mtk_p2p_cfg80211_remain_on_channel */

int
mtk_p2p_cfg80211_remain_on_channel (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct ieee80211_channel *chan,
    enum nl80211_channel_type channel_type,
    unsigned int duration,
    u64 *cookie
    )
{
	int ret = -1, atom;
    	P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    	P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));
        prGlueP2pInfo = prGlueInfo->prP2PInfo;
        *cookie = prGlueP2pInfo->u8Cookie++;

	if(atomic_read(&prGlueInfo->rMgmtTxAto) == 1) {
		ret = wait_for_completion_timeout(&prGlueInfo->rMgmtTxComp, 500*HZ/1000);
	} 

	ret = mtk_p2p_cfg80211_remain_on_channel2(wiphy, dev, chan, channel_type, duration, cookie, true);

	return ret;
}
#else
int
mtk_p2p_cfg80211_remain_on_channel (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct ieee80211_channel *chan,
    enum nl80211_channel_type channel_type,
    unsigned int duration,
    u64 *cookie
    )
{
    INT_32 i4Rslt = -EINVAL;
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;
    P_MSG_P2P_CHNL_REQUEST_T prMsgChnlReq = (P_MSG_P2P_CHNL_REQUEST_T)NULL;


    do {
        if ((wiphy == NULL) ||
                (dev == NULL) ||
                (chan == NULL) ||
                (cookie == NULL)) {
            break;
        }
        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));
        prGlueP2pInfo = prGlueInfo->prP2PInfo;

        *cookie = prGlueP2pInfo->u8Cookie++;

        prMsgChnlReq = cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_CHNL_REQUEST_T));

        if (prMsgChnlReq == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_remain_on_channel\n"));

        prMsgChnlReq->rMsgHdr.eMsgId = MID_MNY_P2P_CHNL_REQ;
        prMsgChnlReq->u8Cookie = *cookie;
        prMsgChnlReq->u4Duration = duration;


        mtk_p2p_cfg80211func_channel_format_switch(chan,
                                                    channel_type,
                                                    &prMsgChnlReq->rChannelInfo,
                                                    &prMsgChnlReq->eChnlSco);

		mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prMsgChnlReq,
                            MSG_SEND_METHOD_BUF);

        i4Rslt = 0;

        i4Rslt = 0;
    } while (FALSE);


    return i4Rslt;
}
/* mtk_p2p_cfg80211_remain_on_channel */
#endif



int
mtk_p2p_cfg80211_cancel_remain_on_channel (
    struct wiphy *wiphy,
    struct net_device *dev,
    u64 cookie
    )
{
    INT_32 i4Rslt = -EINVAL;
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    P_MSG_P2P_CHNL_ABORT_T prMsgChnlAbort = (P_MSG_P2P_CHNL_ABORT_T)NULL;

    do {
        if ((wiphy == NULL) || (dev == NULL)) {
            break;
        }


        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        prMsgChnlAbort = cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_CHNL_ABORT_T));

        if (prMsgChnlAbort == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_cancel_remain_on_channel\n"));

        prMsgChnlAbort->rMsgHdr.eMsgId = MID_MNY_P2P_CHNL_ABORT;
        prMsgChnlAbort->u8Cookie = cookie;


        mboxSendMsg(prGlueInfo->prAdapter,
                                    MBOX_ID_0,
                                    (P_MSG_HDR_T)prMsgChnlAbort,
                                    MSG_SEND_METHOD_BUF);

        i4Rslt = 0;
    } while (FALSE);

    return i4Rslt;
} /* mtk_p2p_cfg80211_cancel_remain_on_channel */

#if 0 //LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 0)
int
mtk_p2p_cfg80211_mgmt_tx (
    struct wiphy *wiphy, struct net_device *dev,
    struct ieee80211_channel *chan, bool offchan,
    enum nl80211_channel_type channel_type,
    bool channel_type_valid, unsigned int wait,
    const u8 *buf,
    size_t len,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	bool no_cck,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	bool dont_wait_for_ack,
#endif
#endif
	u64 *cookie

    )
{
	P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
	P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;
	INT_32 i4Rslt = -EINVAL;
	INT_32 channel_time= 0;
	P_MSG_P2P_MGMT_TX_REQUEST_T prMsgTxReq = (P_MSG_P2P_MGMT_TX_REQUEST_T)NULL;
	P_MSDU_INFO_T prMgmtFrame = (P_MSDU_INFO_T)NULL;
	PUINT_8 pucFrameBuf = (PUINT_8)NULL;
	P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;
	RF_CHANNEL_INFO_T ChnlInfo;
	ENUM_CHNL_EXT_T ChnlSco;
	do {
		if ((wiphy == NULL) ||
				(buf == NULL) ||
				(len == 0) ||
				(dev == NULL) ||
				(cookie == NULL)) {
			break;
		}
		prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));
		prGlueP2pInfo = prGlueInfo->prP2PInfo;
		prP2pFsmInfo = prGlueInfo->prAdapter->rWifiVar.prP2pFsmInfo;
		P_P2P_CHNL_REQ_INFO_T prChnlReqInfo = &(prP2pFsmInfo->rChnlReqInfo);

		mtk_p2p_cfg80211func_channel_format_switch(chan,
				channel_type,
				&ChnlInfo,
				&ChnlSco);
		if (prChnlReqInfo->prMsgTxReq!=NULL){
			i4Rslt = -ENOMEM;
			break;
		}
		channel_time=wait;
		//buffer mgmt frame into driver
		*cookie = prGlueP2pInfo->u8Cookie++;

		/* Channel & Channel Type & Wait time are ignored. */
		prMsgTxReq = cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_MGMT_TX_REQUEST_T));

		if (prMsgTxReq == NULL) {
			ASSERT(FALSE);
			i4Rslt = -ENOMEM;
			break;
		}

		prMsgTxReq->fgNoneCckRate = FALSE;
		prMsgTxReq->fgIsWaitRsp = TRUE;

		prMgmtFrame = cnmMgtPktAlloc(prGlueInfo->prAdapter, (UINT_32)(len + MAC_TX_RESERVED_FIELD));

		if ((prMsgTxReq->prMgmtMsduInfo = prMgmtFrame) == NULL) {
			ASSERT(FALSE);
			i4Rslt = -ENOMEM;
			break;
		}

		prMsgTxReq->u8Cookie = *cookie;
		prMsgTxReq->rMsgHdr.eMsgId = MID_MNY_P2P_MGMT_TX;

		pucFrameBuf = (PUINT_8)((UINT_32)prMgmtFrame->prPacket + MAC_TX_RESERVED_FIELD);

		kalMemCopy(pucFrameBuf, buf, len);

		prMgmtFrame->u2FrameLength = len;

		if (wait==0&&prChnlReqInfo->ucReqChnlNum==ChnlInfo.ucChannelNum&&
				prChnlReqInfo->eBand==ChnlInfo.eBand&&
				prChnlReqInfo->eChnlSco==ChnlSco){
			mboxSendMsg(prGlueInfo->prAdapter,
					MBOX_ID_0,
					(P_MSG_HDR_T)prMsgTxReq,
					MSG_SEND_METHOD_BUF);
			i4Rslt=0;
			break;
		}

		prChnlReqInfo->prMsgTxReq=prMsgTxReq;

		atomic_set(&prGlueInfo->rMgmtTxAto, 1);
		i4Rslt=mtk_p2p_cfg80211_remain_on_channel2(wiphy,dev,chan,channel_type,channel_time, cookie, false);
	} while (FALSE);
	channel_time=0;
	return i4Rslt;
} /* mtk_p2p_cfg80211_mgmt_tx */
#else
int
mtk_p2p_cfg80211_mgmt_tx (
    struct wiphy *wiphy, struct net_device *dev,
    struct ieee80211_channel *chan, bool offchan,
    enum nl80211_channel_type channel_type,
    bool channel_type_valid, unsigned int wait,
    const u8 *buf,
    size_t len,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	bool no_cck,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	bool dont_wait_for_ack,
#endif
#endif
	u64 *cookie

    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;
    INT_32 i4Rslt = -EINVAL;
    P_MSG_P2P_MGMT_TX_REQUEST_T prMsgTxReq = (P_MSG_P2P_MGMT_TX_REQUEST_T)NULL;
    P_MSDU_INFO_T prMgmtFrame = (P_MSDU_INFO_T)NULL;
    PUINT_8 pucFrameBuf = (PUINT_8)NULL;

    do {
        if ((wiphy == NULL) ||
                (buf == NULL) ||
                (len == 0) ||
                (dev == NULL) ||
                (cookie == NULL)) {
            break;
        }

        //DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_mgmt_tx\n"));

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));
        prGlueP2pInfo = prGlueInfo->prP2PInfo;

        *cookie = prGlueP2pInfo->u8Cookie++;

        /* Channel & Channel Type & Wait time are ignored. */
        prMsgTxReq = cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_MGMT_TX_REQUEST_T));

        if (prMsgTxReq == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        prMsgTxReq->fgNoneCckRate = FALSE;
        prMsgTxReq->fgIsWaitRsp = TRUE;

        prMgmtFrame = cnmMgtPktAlloc(prGlueInfo->prAdapter, (UINT_32)(len + MAC_TX_RESERVED_FIELD));

        if ((prMsgTxReq->prMgmtMsduInfo = prMgmtFrame) == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        prMsgTxReq->u8Cookie = *cookie;
        prMsgTxReq->rMsgHdr.eMsgId = MID_MNY_P2P_MGMT_TX;

        pucFrameBuf = (PUINT_8)((UINT_32)prMgmtFrame->prPacket + MAC_TX_RESERVED_FIELD);

        kalMemCopy(pucFrameBuf, buf, len);

        prMgmtFrame->u2FrameLength = len;

        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prMsgTxReq,
                            MSG_SEND_METHOD_BUF);

        i4Rslt = 0;
    } while (FALSE);

    if ((i4Rslt != 0) && (prMsgTxReq != NULL)) {
        if (prMsgTxReq->prMgmtMsduInfo != NULL) {
            cnmMgtPktFree(prGlueInfo->prAdapter, prMsgTxReq->prMgmtMsduInfo);
        }

        cnmMemFree(prGlueInfo->prAdapter, prMsgTxReq);
    }

    return i4Rslt;
} /* mtk_p2p_cfg80211_mgmt_tx */
#endif



/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to cancel the wait time
 *        from transmitting a management frame on another channel
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_p2p_cfg80211_mgmt_tx_cancel_wait (
    struct wiphy *wiphy,
    struct net_device *ndev,
    u64 cookie
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

#if 1
    printk("--> %s()\n", __func__);
#endif

    /* not implemented */
	return 0;
//    return -EINVAL;
}


int
mtk_p2p_cfg80211_change_bss (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct bss_parameters *params
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    INT_32 i4Rslt = -EINVAL;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));


    switch (params->use_cts_prot) {
    case -1:
        DBGLOG(P2P, TRACE, ("CTS protection no change\n"));
        break;
    case 0:
        DBGLOG(P2P, TRACE, ("CTS protection disable.\n"));
        break;
    case 1:
        DBGLOG(P2P, TRACE, ("CTS protection enable\n"));
        break;
    default:
        DBGLOG(P2P, TRACE, ("CTS protection unknown\n"));
        break;
    }



    switch (params->use_short_preamble) {
    case -1:
        DBGLOG(P2P, TRACE, ("Short prreamble no change\n"));
        break;
    case 0:
        DBGLOG(P2P, TRACE, ("Short prreamble disable.\n"));
        break;
    case 1:
        DBGLOG(P2P, TRACE, ("Short prreamble enable\n"));
        break;
    default:
        DBGLOG(P2P, TRACE, ("Short prreamble unknown\n"));
        break;
    }



#if 0
    // not implemented yet
    p2pFuncChangeBssParam(prGlueInfo->prAdapter,
                        prBssInfo->fgIsProtection,
                        prBssInfo->fgIsShortPreambleAllowed,
                        prBssInfo->fgUseShortSlotTime,
                        // Basic rates
                        // basic rates len
                        // ap isolate
                        // ht opmode.
                        );
#else
    i4Rslt = 0;
#endif

    return i4Rslt;
} /* mtk_p2p_cfg80211_change_bss */



int
mtk_p2p_cfg80211_del_station (
    struct wiphy *wiphy,
    struct net_device *dev,
    u8 *mac
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    INT_32 i4Rslt = -EINVAL;
    P_MSG_P2P_CONNECTION_ABORT_T prDisconnectMsg = (P_MSG_P2P_CONNECTION_ABORT_T)NULL;
    UINT_8 aucBcMac[] = BC_MAC_ADDR;


    do {
        if ((wiphy == NULL) ||
                    (dev == NULL)) {
            break;
        }

        if (mac == NULL) {
            mac = aucBcMac;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_del_station.\n"));

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        //prDisconnectMsg = (P_MSG_P2P_CONNECTION_ABORT_T)kalMemAlloc(sizeof(MSG_P2P_CONNECTION_ABORT_T), VIR_MEM_TYPE);
        prDisconnectMsg = (P_MSG_P2P_CONNECTION_ABORT_T)cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_CONNECTION_ABORT_T));

        if (prDisconnectMsg == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        prDisconnectMsg->rMsgHdr.eMsgId = MID_MNY_P2P_CONNECTION_ABORT;
        COPY_MAC_ADDR(prDisconnectMsg->aucTargetID, mac);
        prDisconnectMsg->u2ReasonCode = REASON_CODE_UNSPECIFIED;

        mboxSendMsg(prGlueInfo->prAdapter,
                        MBOX_ID_0,
                        (P_MSG_HDR_T)prDisconnectMsg,
                        MSG_SEND_METHOD_BUF);

        i4Rslt = 0;
    } while (FALSE);

    return i4Rslt;

} /* mtk_p2p_cfg80211_del_station */


int 
mtk_p2p_cfg80211_connect (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct cfg80211_connect_params *sme
    )
{
    INT_32 i4Rslt = -EINVAL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_MSG_P2P_CONNECTION_REQUEST_T prConnReqMsg = (P_MSG_P2P_CONNECTION_REQUEST_T)NULL;


    do {
        if ((wiphy == NULL) ||
                (dev == NULL) ||
                (sme == NULL)) {
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_connect.\n"));

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        prConnReqMsg = (P_MSG_P2P_CONNECTION_REQUEST_T)cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, (sizeof(MSG_P2P_CONNECTION_REQUEST_T) + sme->ie_len));

        if (prConnReqMsg == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        prConnReqMsg->rMsgHdr.eMsgId = MID_MNY_P2P_CONNECTION_REQ;

        COPY_SSID(prConnReqMsg->rSsid.aucSsid,
                                prConnReqMsg->rSsid.ucSsidLen,
                                sme->ssid,
                                sme->ssid_len);

        COPY_MAC_ADDR(prConnReqMsg->aucBssid, sme->bssid);

        DBGLOG(P2P, TRACE, ("Assoc Req IE Buffer Length:%d\n", sme->ie_len));
        kalMemCopy(prConnReqMsg->aucIEBuf, sme->ie, sme->ie_len);
        prConnReqMsg->u4IELen = sme->ie_len;

        mtk_p2p_cfg80211func_channel_format_switch(sme->channel,
                                                    NL80211_CHAN_NO_HT,
                                                    &prConnReqMsg->rChannelInfo,
                                                    &prConnReqMsg->eChnlSco);

        mboxSendMsg(prGlueInfo->prAdapter,
                        MBOX_ID_0,
                        (P_MSG_HDR_T)prConnReqMsg,
                        MSG_SEND_METHOD_BUF);


        i4Rslt = 0;
    } while (FALSE);

    return i4Rslt;
} /* mtk_p2p_cfg80211_connect */

int 
mtk_p2p_cfg80211_disconnect (
    struct wiphy *wiphy,
    struct net_device *dev,
    u16 reason_code
    )
{
    INT_32 i4Rslt = -EINVAL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_MSG_P2P_CONNECTION_ABORT_T prDisconnMsg = (P_MSG_P2P_CONNECTION_ABORT_T)NULL;
    UINT_8 aucBCAddr[] = BC_MAC_ADDR;

    do {
        if ((wiphy == NULL) ||
                (dev == NULL)) {
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_disconnect.\n"));

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

//        prDisconnMsg = (P_MSG_P2P_CONNECTION_ABORT_T)kalMemAlloc(sizeof(P_MSG_P2P_CONNECTION_ABORT_T), VIR_MEM_TYPE);
        prDisconnMsg = (P_MSG_P2P_CONNECTION_ABORT_T)cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_CONNECTION_ABORT_T));

        if (prDisconnMsg == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        prDisconnMsg->rMsgHdr.eMsgId = MID_MNY_P2P_CONNECTION_ABORT;
        prDisconnMsg->u2ReasonCode = reason_code;
        prDisconnMsg->fgSendDeauth = TRUE;
        COPY_MAC_ADDR(prDisconnMsg->aucTargetID, aucBCAddr);

        mboxSendMsg(prGlueInfo->prAdapter,
                                MBOX_ID_0,
                                (P_MSG_HDR_T)prDisconnMsg,
                                MSG_SEND_METHOD_UNBUF);

        i4Rslt = 0;
    } while (FALSE);

    return i4Rslt;
} /* mtk_p2p_cfg80211_disconnect */


int
mtk_p2p_cfg80211_change_iface (
    IN struct wiphy *wiphy,
    IN struct net_device *ndev,
    IN enum nl80211_iftype type,
    IN u32 *flags,
    IN struct vif_params *params
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    INT_32 i4Rslt = -EINVAL;
    P_MSG_P2P_SWITCH_OP_MODE_T prSwitchModeMsg = (P_MSG_P2P_SWITCH_OP_MODE_T)NULL;

    do {
        if ((wiphy == NULL) ||
                (ndev == NULL)) {
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_change_iface.\n"));

        if (ndev->ieee80211_ptr) {
            ndev->ieee80211_ptr->iftype = type;
        }

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));


        // Switch OP MOde.
        prSwitchModeMsg = (P_MSG_P2P_SWITCH_OP_MODE_T)cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_SWITCH_OP_MODE_T));

        if (prSwitchModeMsg == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }

        prSwitchModeMsg->rMsgHdr.eMsgId = MID_MNY_P2P_FUN_SWITCH;

        switch (type) {
        case NL80211_IFTYPE_P2P_CLIENT:
            DBGLOG(P2P, TRACE, ("NL80211_IFTYPE_P2P_CLIENT.\n"));
        case NL80211_IFTYPE_STATION:
            if (type == NL80211_IFTYPE_STATION) {
                DBGLOG(P2P, TRACE, ("NL80211_IFTYPE_STATION.\n"));
            }
            prSwitchModeMsg->eOpMode = OP_MODE_INFRASTRUCTURE;
            break;
        case NL80211_IFTYPE_AP:
            DBGLOG(P2P, TRACE, ("NL80211_IFTYPE_AP.\n"));
        case NL80211_IFTYPE_P2P_GO:
            if (type == NL80211_IFTYPE_P2P_GO) {
                DBGLOG(P2P, TRACE, ("NL80211_IFTYPE_P2P_GO not AP.\n"));
            }
            prSwitchModeMsg->eOpMode = OP_MODE_ACCESS_POINT;
            break;
        default:
            DBGLOG(P2P, TRACE, ("Other type :%d .\n", type));
            prSwitchModeMsg->eOpMode = OP_MODE_P2P_DEVICE;
            break;
        }


        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prSwitchModeMsg,
                            MSG_SEND_METHOD_BUF);

        i4Rslt = 0;

    } while (FALSE);

    return i4Rslt;

} /* mtk_p2p_cfg80211_change_iface */


int
mtk_p2p_cfg80211_set_channel (
    IN struct wiphy *wiphy,
    IN struct net_device *dev,
    IN struct ieee80211_channel *chan,
    IN enum nl80211_channel_type channel_type)
{
    INT_32 i4Rslt = -EINVAL;
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    RF_CHANNEL_INFO_T rRfChnlInfo;

    do {
        if ((wiphy == NULL) ||
                (dev == NULL) ||
                (chan == NULL)) {
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_set_channel.\n"));

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        mtk_p2p_cfg80211func_channel_format_switch(chan,
                                        channel_type,
                                        &rRfChnlInfo,
                                        NULL);

        p2pFuncSetChannel(prGlueInfo->prAdapter, &rRfChnlInfo);

        i4Rslt = 0;
    }
while (FALSE);

    return i4Rslt;

}
/* mtk_p2p_cfg80211_set_channel */

int
mtk_p2p_cfg80211_set_bitrate_mask (
    IN struct wiphy *wiphy,
    IN struct net_device *dev,
    IN const u8 *peer,
    IN const struct cfg80211_bitrate_mask *mask
    )
{
    INT_32 i4Rslt = -EINVAL;
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;

    do {
        if ((wiphy == NULL) ||
                (dev == NULL) ||
                (mask == NULL)) {
            break;
        }

        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_set_bitrate_mask\n"));

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        // TODO: Set bitrate mask of the peer?

        i4Rslt = 0;
    }
while (FALSE);

    return i4Rslt;
} /* mtk_p2p_cfg80211_set_bitrate_mask */


void
mtk_p2p_cfg80211_mgmt_frame_register (
    IN struct wiphy *wiphy,
    IN struct net_device *dev,
    IN u16 frame_type,
    IN bool reg
    )
{
#if 1
    P_MSG_P2P_MGMT_FRAME_REGISTER_T prMgmtFrameRegister = (P_MSG_P2P_MGMT_FRAME_REGISTER_T)NULL;
#endif
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;

    do {
        if ((wiphy == NULL) ||
                (dev == NULL)) {
            break;
        }

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

 		prMgmtFrameRegister = kmalloc(sizeof(MSG_P2P_MGMT_FRAME_REGISTER_T),GFP_ATOMIC);
/*  this function is used to set HW filter to RX package, but if we use oringnal method, we may face wrong 
cmd sequence to firmware (supplicant request 1. set probe request fiter  2. remain on channel  but for driver 1. send channel request
first 2. then set RX filter). This wrong schedule sequence will cause other device can not find this DUT
Solution:
1. use mail box to handle set RX filter request
2. use kmalloc to avoid sleep in atomic contex, if alloc mem fail, use oringal method
*/


		if(prMgmtFrameRegister == NULL){

			printk("p2p set fitler error,can not alloc mem from kernel, use tx thread to set rx filter\n");
        switch (frame_type) {
        case MAC_FRAME_PROBE_REQ:
            if (reg) {
                prGlueInfo->prP2PInfo->u4OsMgmtFrameFilter |= PARAM_PACKET_FILTER_PROBE_REQ;
                DBGLOG(P2P, TRACE, ("Open packet filer probe request\n"));
            }
            else {
                prGlueInfo->prP2PInfo->u4OsMgmtFrameFilter &= ~PARAM_PACKET_FILTER_PROBE_REQ;
                DBGLOG(P2P, TRACE, ("Close packet filer probe request\n"));
            }
            break;
        case MAC_FRAME_ACTION:
            if (reg) {
                prGlueInfo->prP2PInfo->u4OsMgmtFrameFilter |= PARAM_PACKET_FILTER_ACTION_FRAME;
                DBGLOG(P2P, TRACE, ("Open packet filer action frame.\n"));
            }
            else {
                prGlueInfo->prP2PInfo->u4OsMgmtFrameFilter &= ~PARAM_PACKET_FILTER_ACTION_FRAME;
                DBGLOG(P2P, TRACE, ("Close packet filer action frame.\n"));
            }
            break;
        default:
                DBGLOG(P2P, ERROR, ("Ask frog to add code for mgmt:%x\n", frame_type));
                break;
        }


        
        if((prGlueInfo->prAdapter != NULL)  && (prGlueInfo->prAdapter->fgIsP2PRegistered == TRUE)){

	           //prGlueInfo->u4Flag |= GLUE_FLAG_FRAME_FILTER;
			   set_bit(GLUE_FLAG_FRAME_FILTER_BIT, &prGlueInfo->u4Flag);

        /* wake up main thread */
        wake_up_interruptible(&prGlueInfo->waitq);

        if (in_interrupt()) {
            DBGLOG(P2P, TRACE, ("It is in interrupt level\n"));
           }
        }
		}else{

#if 1
#if 0
        prMgmtFrameRegister = (P_MSG_P2P_MGMT_FRAME_REGISTER_T)cnmMemAlloc(prGlueInfo->prAdapter, 
                                                                    RAM_TYPE_MSG, 
                                                                    sizeof(MSG_P2P_MGMT_FRAME_REGISTER_T));

        if (prMgmtFrameRegister == NULL) {
            ASSERT(FALSE);
            break;
        }
	#endif
        prMgmtFrameRegister->rMsgHdr.eMsgId = MID_MNY_P2P_MGMT_FRAME_REGISTER;

        prMgmtFrameRegister->u2FrameType = frame_type;
        prMgmtFrameRegister->fgIsRegister = reg;

        mboxSendMsg(prGlueInfo->prAdapter,
                                    MBOX_ID_0,
                                    (P_MSG_HDR_T)prMgmtFrameRegister,
                                    MSG_SEND_METHOD_BUF);

#endif
				}
    } while (FALSE);

    return;
} /* mtk_p2p_cfg80211_mgmt_frame_register */


BOOLEAN
mtk_p2p_cfg80211func_channel_format_switch (
    IN struct ieee80211_channel *channel,
    IN enum nl80211_channel_type channel_type,
    IN P_RF_CHANNEL_INFO_T prRfChnlInfo,
    IN P_ENUM_CHNL_EXT_T prChnlSco
    )
{
    BOOLEAN fgIsValid = FALSE;

    do {
        if (channel == NULL) {
            break;
        }

        if (prRfChnlInfo) {
            prRfChnlInfo->ucChannelNum = nicFreq2ChannelNum(channel->center_freq * 1000);

            switch (channel->band) {
            case IEEE80211_BAND_2GHZ:
                prRfChnlInfo->eBand = BAND_2G4;
                break;
            case IEEE80211_BAND_5GHZ:
                prRfChnlInfo->eBand = BAND_5G;
                break;
            default:
                prRfChnlInfo->eBand = BAND_2G4;
                break;
            }
        
        }

        
        if (prChnlSco) {
            
            switch (channel_type) {
            case NL80211_CHAN_NO_HT:
                *prChnlSco = CHNL_EXT_SCN;
                break;
            case NL80211_CHAN_HT20:
                *prChnlSco = CHNL_EXT_SCN;
                break;
            case NL80211_CHAN_HT40MINUS:
                *prChnlSco = CHNL_EXT_SCA;
                break;
            case NL80211_CHAN_HT40PLUS:
                *prChnlSco = CHNL_EXT_SCB;
                break;
            default:
                ASSERT(FALSE);
                *prChnlSco = CHNL_EXT_SCN;
                break;
            }
        }

        fgIsValid = TRUE;
    }
while (FALSE);

    return fgIsValid;
}
/* mtk_p2p_cfg80211func_channel_format_switch */


#if CONFIG_NL80211_TESTMODE
int mtk_p2p_cfg80211_testmode_cmd(
    IN struct wiphy *wiphy,
    IN void *data,
    IN int len
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_NL80211_DRIVER_TEST_PARAMS prParams = (P_NL80211_DRIVER_TEST_PARAMS)NULL;
    BOOLEAN fgIsValid = FALSE;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

	DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_testmode_cmd\n"));
    
    if(data && len)
        prParams = (P_NL80211_DRIVER_TEST_PARAMS)data;

    if(prParams->index >> 24 == 0x01) { 
        /* New version */
        prParams->index = prParams->index & ~ BITS(24,31);
    }
    else {  
        /* Old version*/
        mtk_p2p_cfg80211_testmode_p2p_sigma_pre_cmd(wiphy, data, len); 
		fgIsValid = TRUE;
        return fgIsValid;
    }

    /* Clear the version byte */
    prParams->index = prParams->index & ~ BITS(24,31);

	if(prParams){
		switch(prParams->index){
		    case 1: /* P2P Simga */
			    if(mtk_p2p_cfg80211_testmode_p2p_sigma_cmd(wiphy, data, len))
					fgIsValid = TRUE;
			    break;
#if CFG_SUPPORT_WFD 
			case 2: /* WFD */
				if(mtk_p2p_cfg80211_testmode_wfd_update_cmd(wiphy, data, len))
					fgIsValid= TRUE;
			    break;
#endif
            case 3: /* Hotspot Client Management */
                if(mtk_p2p_cfg80211_testmode_hotspot_block_list_cmd(wiphy, data, len))
					fgIsValid = TRUE;
                break;
			default:
				fgIsValid = TRUE;
			    break;
		}
	}

	return fgIsValid;

}


int mtk_p2p_cfg80211_testmode_p2p_sigma_pre_cmd(
    IN struct wiphy *wiphy,
    IN void *data,
    IN int len
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
	NL80211_DRIVER_TEST_PRE_PARAMS rParams;
	P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;
	UINT_32 index_mode;
	UINT_32 index;
    INT_32  value;
	int     status = 0;
	UINT_32 u4Leng;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

	kalMemZero(&rParams, sizeof(NL80211_DRIVER_TEST_PRE_PARAMS));

	prP2pSpecificBssInfo = prGlueInfo->prAdapter->rWifiVar.prP2pSpecificBssInfo;
    prP2pConnSettings = prGlueInfo->prAdapter->rWifiVar.prP2PConnSettings;

	DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_testmode_p2p_sigma_pre_cmd\n"));

	if(data && len)
		memcpy(&rParams, data, len);

    DBGLOG(P2P, TRACE, ("NL80211_ATTR_TESTDATA,idx_mode=%d idx=%d value=%lu\n",
		(INT_16)rParams.idx_mode, (INT_16)rParams.idx, rParams.value));

    index_mode = rParams.idx_mode;
	index = rParams.idx;
	value = rParams.value;

    switch (index) {
      case 0: /* Listen CH */
          break;
      case 1: /* P2p mode */
          break;
      case 4: /* Noa duration */
          prP2pSpecificBssInfo->rNoaParam.u4NoaDurationMs = value;
          // only to apply setting when setting NOA count
          //status = mtk_p2p_wext_set_noa_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rNoaParam);
          break;
      case 5: /* Noa interval */
          prP2pSpecificBssInfo->rNoaParam.u4NoaIntervalMs = value;
          // only to apply setting when setting NOA count
          //status = mtk_p2p_wext_set_noa_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rNoaParam);
          break;
      case 6: /* Noa count */
          prP2pSpecificBssInfo->rNoaParam.u4NoaCount = value;
          //status = mtk_p2p_wext_set_noa_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rNoaParam);
          break;
      case 100: /* Oper CH */
          // 20110920 - frog: User configurations are placed in ConnSettings.
          // prP2pConnSettings->ucOperatingChnl = value;
          break;
      case 101: /* Local config Method, for P2P SDK */
          prP2pConnSettings->u2LocalConfigMethod = value;
          break;
      case 102: /* Sigma P2p reset */
          //kalMemZero(prP2pConnSettings->aucTargetDevAddr, MAC_ADDR_LEN);
          //prP2pConnSettings->eConnectionPolicy = ENUM_P2P_CONNECTION_POLICY_AUTO;
          p2pFsmUninit(prGlueInfo->prAdapter);
          p2pFsmInit(prGlueInfo->prAdapter);
          break;
      case 103: /* WPS MODE */
          kalP2PSetWscMode(prGlueInfo, value);
          break;
      case 104: /* P2p send persence, duration */
          break;
      case 105: /* P2p send persence, interval */
          break;
      case 106: /* P2P set sleep  */
            value = 1;
            kalIoctl(prGlueInfo,
                wlanoidSetP2pPowerSaveProfile,
                &value,
                sizeof(value),
                FALSE,
                FALSE,
                TRUE,
                TRUE,
                &u4Leng);
          break;
      case 107: /* P2P set opps, CTWindowl */
            prP2pSpecificBssInfo->rOppPsParam.u4CTwindowMs = value;
            //status = mtk_p2p_wext_set_oppps_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rOppPsParam);
          break;
      case 108: /* p2p_set_power_save */
            kalIoctl(prGlueInfo,
                wlanoidSetP2pPowerSaveProfile,
                &value,
                sizeof(value),
                FALSE,
                FALSE,
                TRUE,
                TRUE,
                &u4Leng);

          break;
      default:
          break;
    }

    return status;

}


int
mtk_p2p_cfg80211_testmode_p2p_sigma_cmd(
    IN struct wiphy *wiphy,
    IN void *data,
    IN int len)
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_NL80211_DRIVER_P2P_SIGMA_PARAMS prParams = (P_NL80211_DRIVER_P2P_SIGMA_PARAMS)NULL;
	P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;
	UINT_32 index;
    INT_32  value;
	int     status = 0;
	UINT_32 u4Leng;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    prP2pSpecificBssInfo = prGlueInfo->prAdapter->rWifiVar.prP2pSpecificBssInfo;
    prP2pConnSettings = prGlueInfo->prAdapter->rWifiVar.prP2PConnSettings;

    DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_testmode_p2p_sigma_cmd\n"));

    if(data && len)
        prParams = (P_NL80211_DRIVER_P2P_SIGMA_PARAMS)data;

    index = (INT_32)prParams->idx;
    value = (INT_32)prParams->value;

	DBGLOG(P2P, TRACE, ("NL80211_ATTR_TESTDATA, idx=%lu value=%lu\n",
		(INT_32)prParams->idx, (INT_32)prParams->value));

    switch (index) {
      case 0: /* Listen CH */
          break;
      case 1: /* P2p mode */
          break;
      case 4: /* Noa duration */
          prP2pSpecificBssInfo->rNoaParam.u4NoaDurationMs = value;
          // only to apply setting when setting NOA count
          //status = mtk_p2p_wext_set_noa_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rNoaParam);
          break;
      case 5: /* Noa interval */
          prP2pSpecificBssInfo->rNoaParam.u4NoaIntervalMs = value;
          // only to apply setting when setting NOA count
          //status = mtk_p2p_wext_set_noa_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rNoaParam);
          break;
      case 6: /* Noa count */
          prP2pSpecificBssInfo->rNoaParam.u4NoaCount = value;
          //status = mtk_p2p_wext_set_noa_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rNoaParam);
          break;
      case 100: /* Oper CH */
          // 20110920 - frog: User configurations are placed in ConnSettings.
          // prP2pConnSettings->ucOperatingChnl = value;
          break;
      case 101: /* Local config Method, for P2P SDK */
          prP2pConnSettings->u2LocalConfigMethod = value;
          break;
      case 102: /* Sigma P2p reset */
          //kalMemZero(prP2pConnSettings->aucTargetDevAddr, MAC_ADDR_LEN);
          //prP2pConnSettings->eConnectionPolicy = ENUM_P2P_CONNECTION_POLICY_AUTO;
          break;
      case 103: /* WPS MODE */
          kalP2PSetWscMode(prGlueInfo, value);
          break;
      case 104: /* P2p send persence, duration */
          break;
      case 105: /* P2p send persence, interval */
          break;
      case 106: /* P2P set sleep  */
            value = 1;
            kalIoctl(prGlueInfo,
                wlanoidSetP2pPowerSaveProfile,
                &value,
                sizeof(value),
                FALSE,
                FALSE,
                TRUE,
                TRUE,
                &u4Leng);
          break;
      case 107: /* P2P set opps, CTWindowl */
            prP2pSpecificBssInfo->rOppPsParam.u4CTwindowMs = value;
            //status = mtk_p2p_wext_set_oppps_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rOppPsParam);
          break;
      case 108: /* p2p_set_power_save */
            kalIoctl(prGlueInfo,
                wlanoidSetP2pPowerSaveProfile,
                &value,
                sizeof(value),
                FALSE,
                FALSE,
                TRUE,
                TRUE,
                &u4Leng);

          break;
      case 109: /* Max Clients*/
          kalP2PSetMaxClients(prGlueInfo, value);
          break;
      default:
          break;
    }

    return status;

}

#if CFG_SUPPORT_WFD
int
mtk_p2p_cfg80211_testmode_wfd_update_cmd(
    IN struct wiphy *wiphy,
    IN void *data,
    IN int len)
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_NL80211_DRIVER_WFD_PARAMS prParams = (P_NL80211_DRIVER_WFD_PARAMS)NULL;
    int status = 0;
    P_WFD_CFG_SETTINGS_T prWfdCfgSettings = (P_WFD_CFG_SETTINGS_T)NULL;
    P_MSG_WFD_CONFIG_SETTINGS_CHANGED_T prMsgWfdCfgUpdate = (P_MSG_WFD_CONFIG_SETTINGS_CHANGED_T)NULL;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    prParams = (P_NL80211_DRIVER_WFD_PARAMS)data;


    DBGLOG(P2P, INFO, ("mtk_p2p_cfg80211_testmode_wfd_update_cmd\n"));

#if 1

    DBGLOG(P2P, INFO,("WFD Enable:%x\n", prParams->WfdEnable));
    DBGLOG(P2P, INFO,("WFD Session Available:%x\n", prParams->WfdSessionAvailable));
    DBGLOG(P2P, INFO,("WFD Couple Sink Status:%x\n", prParams->WfdCoupleSinkStatus));
    //aucReserved0[2]
    DBGLOG(P2P, INFO,("WFD Device Info:%x\n", prParams->WfdDevInfo));
    DBGLOG(P2P, INFO,("WFD Control Port:%x\n", prParams->WfdControlPort));
    DBGLOG(P2P, INFO,("WFD Maximum Throughput:%x\n", prParams->WfdMaximumTp));
    DBGLOG(P2P, INFO,("WFD Extend Capability:%x\n", prParams->WfdExtendCap));
    DBGLOG(P2P, INFO,("WFD Couple Sink Addr "MACSTR" \n", MAC2STR(prParams->WfdCoupleSinkAddress)));
    DBGLOG(P2P, INFO,("WFD Associated BSSID "MACSTR" \n", MAC2STR(prParams->WfdAssociatedBssid)));
    //UINT_8 aucVideolp[4];
    //UINT_8 aucAudiolp[4];
    DBGLOG(P2P, INFO,("WFD Video Port:%x\n", prParams->WfdVideoPort));
    DBGLOG(P2P, INFO,("WFD Audio Port:%x\n", prParams->WfdAudioPort));
    DBGLOG(P2P, INFO,("WFD Flag:%x\n", prParams->WfdFlag));
    DBGLOG(P2P, INFO,("WFD Policy:%x\n", prParams->WfdPolicy));
    DBGLOG(P2P, INFO,("WFD State:%x\n", prParams->WfdState));
    //UINT_8 aucWfdSessionInformationIE[24*8];
    DBGLOG(P2P, INFO,("WFD Session Info Length:%x\n", prParams->WfdSessionInformationIELen));
    //UINT_8 aucReserved1[2];
    DBGLOG(P2P, INFO,("WFD Primary Sink Addr "MACSTR" \n", MAC2STR(prParams->aucWfdPrimarySinkMac)));
    DBGLOG(P2P, INFO,("WFD Secondary Sink Addr "MACSTR" \n", MAC2STR(prParams->aucWfdSecondarySinkMac)));
    DBGLOG(P2P, INFO,("WFD Advanced Flag:%x\n", prParams->WfdAdvanceFlag));
    DBGLOG(P2P, INFO,("WFD Sigma mode:%x\n", prParams->WfdSigmaMode));
    //UINT_8 aucReserved2[64];
    //UINT_8 aucReserved3[64];
    //UINT_8 aucReserved4[64];

#endif

    prWfdCfgSettings = &(prGlueInfo->prAdapter->rWifiVar.prP2pFsmInfo->rWfdConfigureSettings);

    kalMemCopy(&prWfdCfgSettings->u4WfdCmdType, &prParams->WfdCmdType, sizeof(WFD_CFG_SETTINGS_T));

    prMsgWfdCfgUpdate = cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_WFD_CONFIG_SETTINGS_CHANGED_T));

    if (prMsgWfdCfgUpdate == NULL) {
        ASSERT(FALSE);
        return status;
    }

    prMsgWfdCfgUpdate->rMsgHdr.eMsgId = MID_MNY_P2P_WFD_CFG_UPDATE;
    prMsgWfdCfgUpdate->prWfdCfgSettings = prWfdCfgSettings;


    mboxSendMsg(prGlueInfo->prAdapter,
                        MBOX_ID_0,
                        (P_MSG_HDR_T)prMsgWfdCfgUpdate,
                        MSG_SEND_METHOD_BUF);
#if 0 // Test Only
//    prWfdCfgSettings->ucWfdEnable = 1;
//    prWfdCfgSettings->u4WfdFlag |= WFD_FLAGS_DEV_INFO_VALID;
    prWfdCfgSettings->u4WfdFlag |= WFD_FLAGS_DEV_INFO_VALID;
    prWfdCfgSettings->u2WfdDevInfo = 123;
    prWfdCfgSettings->u2WfdControlPort = 456;
    prWfdCfgSettings->u2WfdMaximumTp = 789;


    prWfdCfgSettings->u4WfdFlag |= WFD_FLAGS_SINK_INFO_VALID;
    prWfdCfgSettings->ucWfdCoupleSinkStatus = 0xAB;
    {
        UINT_8 aucTestAddr[MAC_ADDR_LEN] = {0x77, 0x66, 0x55, 0x44, 0x33, 0x22};
        COPY_MAC_ADDR(prWfdCfgSettings->aucWfdCoupleSinkAddress, aucTestAddr);
    }

    prWfdCfgSettings->u4WfdFlag |= WFD_FLAGS_EXT_CAPABILITY_VALID;
    prWfdCfgSettings->u2WfdExtendCap = 0xCDE;

#endif

    return status;

}
#endif /*  CFG_SUPPORT_WFD */



int
mtk_p2p_cfg80211_testmode_hotspot_block_list_cmd(
    IN struct wiphy *wiphy,
    IN void *data,
    IN int len)
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_NL80211_DRIVER_hotspot_block_PARAMS prParams = (P_NL80211_DRIVER_hotspot_block_PARAMS)NULL;
    int fgIsValid = 0;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    if(data && len)
        prParams = (P_NL80211_DRIVER_hotspot_block_PARAMS)data;

    DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_testmode_hotspot_block_list_cmd\n"));

    fgIsValid = kalP2PSetBlackList(prGlueInfo, prParams->aucBssid, prParams->ucblocked);
 
    return fgIsValid;

}

#endif


#endif // LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)

#endif // CFG_ENABLE_WIFI_DIRECT && CFG_ENABLE_WIFI_DIRECT_CFG_80211
