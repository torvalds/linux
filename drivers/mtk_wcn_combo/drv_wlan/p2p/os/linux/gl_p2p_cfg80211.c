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
 *
 * 02 23 2012 chinglan.wang
 * [ALPS00240621] [Wifi P2P]Run Sigma tool of A69. Always run fail on 7.1.3. sniffer check  Sta-IsActive-No-Frames-With-PowerMgmt-1
 * .
 *
 * 02 06 2012 yuche.tsai
 * [ALPS00118350] [ALPS.ICS][WiFi Direct] Dirver update for wifi direct connection
 * Small bug fix for CNM memory usage, scan SSID search issue, P2P network deactivate issue.
 *
 * 01 31 2012 yuche.tsai
 * NULL
 * Fix compile error & del beacon scenario.
 *
 * 01 27 2012 yuche.tsai
 * NULL
 * Update for GC connection .
 *
 * 01 26 2012 yuche.tsai
 * NULL
 * Fix compile warning.
 *
 * 01 18 2012 yuche.tsai
 * NULL
 * Bug fix for memory allocation.
 *
 * 01 18 2012 yuche.tsai
 * NULL
 * Change debug log from printk to DBGLOG.
 * Fix probe request issue.
 *
 * 01 18 2012 yuche.tsai
 * NULL
 * Add get station info API.
 * TX deauth before start beacon.
 *
 * 01 17 2012 wh.su
 * [WCXRP00001173] [MT6620 Wi-Fi][Driver] Adding the ICS Tethering WPA2-PSK supporting
 * adding the code to fix the add key function.
 *
 * 01 17 2012 yuche.tsai
 * NULL
 * Update mgmt frame filter setting.
 * Please also update FW 2.1
 *
 * 01 16 2012 yuche.tsai
 * NULL
 * Update Driver for wifi driect gc join IE update issue.
 *
 * 01 16 2012 chinglan.wang
 * NULL
 * Update security code..
 *
 * 01 16 2012 yuche.tsai
 * NULL
 * Fix wifi direct scan bug.
 *
 * 01 15 2012 yuche.tsai
 * NULL
 * Update P2P scan issue for ICS.
 *
 * 01 15 2012 yuche.tsai
 * NULL
 * ICS P2P Driver Update.
 *
 * 01 15 2012 yuche.tsai
 * NULL
 * Fix p2p scan bug.
 *
 * 01 15 2012 yuche.tsai
 * NULL
 * Fix scan bug.
 *
 * 01 15 2012 yuche.tsai
 * NULL
 * ICS Wi-Fi Direct Update.
 *
 * 01 13 2012 yuche.tsai
 * NULL
 * Update driver/p2p driver for ICS tethering mode.
 * Fix FW reply probe request issue.
 *
 * 01 13 2012 yuche.tsai
 * NULL
 * WiFi Tethering for ICS code update. - fix KE when indicate Probe Request.
 *
 * 01 13 2012 yuche.tsai
 * NULL
 * WiFi Direct Driver Update for ICS.
 *
 * 01 13 2012 yuche.tsai
 * NULL
 * WiFi Hot Spot Tethering for ICS ALPHA testing version.
 *
 * 01 09 2012 terry.wu
 * [WCXRP00001166] [Wi-Fi] [Driver] cfg80211 integration for p2p newtork
 * cfg80211 integration for p2p network.
 *
 * 03 19 2011 terry.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 p2p driver release based on label "MT6620_WIFI_P2P_DRIVER_V2_0_2100_0319_2011" from main trunk.
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

#include "p2p_precomp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
#if 0
static add_set_beacon_count = 0;
#endif
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
        if (rKey.arBSSID[1] == 0x00) {
            rKey.arBSSID[0] = 0xff;
            rKey.arBSSID[1] = 0xff;
            rKey.arBSSID[2] = 0xff;
            rKey.arBSSID[3] = 0xff;
            rKey.arBSSID[4] = 0xff;
            rKey.arBSSID[5] = 0xff;
        }
        if (rKey.arBSSID[0] != 0xFF) {
            rKey.u4KeyIndex |= BIT(31);
            if (/* rKey.arBSSID[0] != 0x00 && */
                rKey.arBSSID[1] != 0x00 )
            rKey.u4KeyIndex |= BIT(30);
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

    return -EINVAL;
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


        DBGLOG(P2P, TRACE, ("Finish SSID list.\n"));

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

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    // not implemented yet

    return -EINVAL;
}

//&&&&&&&&&&&&&&&&&&&&&&&&&& Add for ICS Wi-Fi Direct Support. &&&&&&&&&&&&&&&&&&&&&&&

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
    PUINT_8 pucBuffer = (PUINT_8)NULL;

    do {
        if ((wiphy == NULL) || (info == NULL)) {
            break;
        }
        #if 0
        if (add_set_beacon_count >= 1) {
           break;
        }
        #endif



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

        #if 0
        add_set_beacon_count += 1;
        #endif
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

        prP2pBcnUpdateMsg->u4BcnInterval = info->interval;

        prP2pBcnUpdateMsg->u4DtimPeriod = info->dtim_period;


        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prP2pBcnUpdateMsg,
                            MSG_SEND_METHOD_BUF);

        i4Rslt = 0;

    } while (FALSE);

    return i4Rslt;
}


int
mtk_p2p_cfg80211_del_beacon (
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



        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_del_beacon.\n"));
        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        // Switch OP MOde.
        prP2pSwitchMode = cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_SWITCH_OP_MODE_T));

        if (prP2pSwitchMode == NULL) {
            ASSERT(FALSE);
            i4Rslt = -ENOMEM;
            break;
        }


        prP2pSwitchMode->rMsgHdr.eMsgId = MID_MNY_P2P_BEACON_DEL;

        mboxSendMsg(prGlueInfo->prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prP2pSwitchMode,
                            MSG_SEND_METHOD_BUF);

        i4Rslt = 0;
    } while (FALSE);


    return i4Rslt;
} /* mtk_p2p_cfg80211_del_beacon */

// TODO:
int
mtk_p2p_cfg80211_deauth (
    struct wiphy *wiphy,
    struct net_device *dev,
    struct cfg80211_deauth_request *req,
    void *cookie
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
    struct cfg80211_disassoc_request *req,
    void *cookie
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

    DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_disassoc.\n"));

    // not implemented yet

    return -EINVAL;
} /* mtk_p2p_cfg80211_disassoc */


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

int
mtk_p2p_cfg80211_mgmt_tx (
    struct wiphy *wiphy, struct net_device *dev,
    struct ieee80211_channel *chan, bool offchan,
    enum nl80211_channel_type channel_type,
    bool channel_type_valid, unsigned int wait,
    const u8 *buf,
    size_t len,
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


        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_mgmt_tx\n"));

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


    do {
        if ((wiphy == NULL) ||
                    (dev == NULL) ||
                    (mac == NULL)) {
            break;
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


int mtk_p2p_cfg80211_connect (
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
}

int mtk_p2p_cfg80211_disconnect(
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
        COPY_MAC_ADDR(prDisconnMsg->aucTargetID, aucBCAddr);

        mboxSendMsg(prGlueInfo->prAdapter,
                                MBOX_ID_0,
                                (P_MSG_HDR_T)prDisconnMsg,
                                MSG_SEND_METHOD_BUF);

        i4Rslt = 0;
    } while (FALSE);

    return i4Rslt;
}


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
    P_MSG_P2P_MGMT_FRAME_REGISTER_T prMgmtFrameRegister = (P_MSG_P2P_MGMT_FRAME_REGISTER_T)NULL;
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;

    do {
        if ((wiphy == NULL) ||
                (dev == NULL)) {
            break;
        }


        DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_mgmt_frame_register\n"));

        prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

        prMgmtFrameRegister = cnmMemAlloc(prGlueInfo->prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_MGMT_FRAME_REGISTER_T));

        if (prMgmtFrameRegister == NULL) {
            ASSERT(FALSE);
            break;
        }


        prMgmtFrameRegister->rMsgHdr.eMsgId = MID_MNY_P2P_MGMT_FRAME_REGISTER;

        prMgmtFrameRegister->u2FrameType = frame_type;
        prMgmtFrameRegister->fgIsRegister = reg;

        mboxSendMsg(prGlueInfo->prAdapter,
                                    MBOX_ID_0,
                                    (P_MSG_HDR_T)prMgmtFrameRegister,
                                    MSG_SEND_METHOD_BUF);

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
	NL80211_DRIVER_TEST_PARAMS rParams;
	P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;
	UINT_32 index_mode;
	UINT_32 index;
    INT_32  value;
	int     status = 0;
	UINT_32 u4Leng;

    ASSERT(wiphy);

    prGlueInfo = *((P_GLUE_INFO_T *) wiphy_priv(wiphy));

	kalMemZero(&rParams, sizeof(NL80211_DRIVER_TEST_PARAMS));
	
	prP2pSpecificBssInfo = prGlueInfo->prAdapter->rWifiVar.prP2pSpecificBssInfo;
    prP2pConnSettings = prGlueInfo->prAdapter->rWifiVar.prP2PConnSettings;

	DBGLOG(P2P, TRACE, ("mtk_p2p_cfg80211_testmode_cmd\n"));
	
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
#endif


#endif // LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)

#endif // CFG_ENABLE_WIFI_DIRECT && CFG_ENABLE_WIFI_DIRECT_CFG_80211
