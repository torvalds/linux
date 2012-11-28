/*
** $Id: @(#) gl_cfg80211.c@@
*/

/*! \file   gl_cfg80211.c
    \brief  Main routines for supporintg MT6620 cfg80211 control interface

    This file contains the support routines of Linux driver for MediaTek Inc. 802.11
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
** $Log: gl_cfg80211.c $
** 
** 08 29 2012 chinglan.wang
** [ALPS00349655] [Need Patch] [Volunteer Patch] [ALPS.JB] Daily build warning on [mt6575_phone_mhl-eng]
** .
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
#include "gl_os.h"
#include "debug.h"
#include "wlan_lib.h"
#include "gl_wext.h"
#include "precomp.h"

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

#if CFG_SUPPORT_WAPI
    extern UINT_8 keyStructBuf[1024];   /* add/remove key shared buffer */
#else
    extern UINT_8 keyStructBuf[100];   /* add/remove key shared buffer */
#endif

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

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for change STA type between
 *        1. Infrastructure Client (Non-AP STA)
 *        2. Ad-Hoc IBSS
 *
 * @param
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int 
mtk_cfg80211_change_iface (
    struct wiphy *wiphy,
    struct net_device *ndev,
    enum nl80211_iftype type,
    u32 *flags,
    struct vif_params *params
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    ENUM_PARAM_OP_MODE_T eOpMode;
    UINT_32 u4BufLen;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

    if(type == NL80211_IFTYPE_STATION) {
        eOpMode = NET_TYPE_INFRA;
    }
    else if(type == NL80211_IFTYPE_ADHOC) {
        eOpMode = NET_TYPE_IBSS;
    }
    else {
        return -EINVAL;
    }

    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetInfrastructureMode,
            &eOpMode,
            sizeof(eOpMode),
            FALSE,
            FALSE,
            TRUE,
            FALSE,
            &u4BufLen);
    
    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(REQ, WARN, ("set infrastructure mode error:%lx\n", rStatus));
    }

    /* reset wpa info */
    prGlueInfo->rWpaInfo.u4WpaVersion = IW_AUTH_WPA_VERSION_DISABLED;
    prGlueInfo->rWpaInfo.u4KeyMgmt = 0;
    prGlueInfo->rWpaInfo.u4CipherGroup = IW_AUTH_CIPHER_NONE;
    prGlueInfo->rWpaInfo.u4CipherPairwise = IW_AUTH_CIPHER_NONE;
    prGlueInfo->rWpaInfo.u4AuthAlg = IW_AUTH_ALG_OPEN_SYSTEM;
#if CFG_SUPPORT_802_11W
    prGlueInfo->rWpaInfo.u4Mfp = IW_AUTH_MFP_DISABLED;
#endif

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for adding key 
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_cfg80211_add_key (
    struct wiphy *wiphy,
    struct net_device *ndev,
    u8 key_index,
    bool pairwise,
    const u8 *mac_addr,
    struct key_params *params
    )
{
    PARAM_KEY_T rKey;
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    INT_32 i4Rslt = -EINVAL;
    UINT_32 u4BufLen = 0;
    UINT_8 tmp1[8];
    UINT_8 tmp2[8];

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);
    
    kalMemZero(&rKey, sizeof(PARAM_KEY_T));

    rKey.u4KeyIndex = key_index;
    
    if(mac_addr) {
        COPY_MAC_ADDR(rKey.arBSSID, mac_addr);
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
    }
    else {
            rKey.arBSSID[0] = 0xff;
            rKey.arBSSID[1] = 0xff;
            rKey.arBSSID[2] = 0xff;
            rKey.arBSSID[3] = 0xff;
            rKey.arBSSID[4] = 0xff;
            rKey.arBSSID[5] = 0xff;
            //rKey.u4KeyIndex |= BIT(31); //Enable BIT 31 will make tx use bc key id, should use pairwise key id 0 
    }
    
    if(params->key) {
        //rKey.aucKeyMaterial[0] = kalMemAlloc(params->key_len, VIR_MEM_TYPE);
        kalMemCopy(rKey.aucKeyMaterial, params->key, params->key_len);
        if (params->key_len == 32) {
            kalMemCopy(tmp1, &params->key[16], 8);
            kalMemCopy(tmp2, &params->key[24], 8);        
            kalMemCopy(&rKey.aucKeyMaterial[16], tmp2, 8);
            kalMemCopy(&rKey.aucKeyMaterial[24], tmp1, 8);
        }
    }

    rKey.u4KeyLength = params->key_len;
    rKey.u4Length =  ((UINT_32)&(((P_P2P_PARAM_KEY_T)0)->aucKeyMaterial)) + rKey.u4KeyLength;
    
    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetAddKey,
            &rKey,
            rKey.u4Length,
            FALSE,
            FALSE,
            TRUE,
            FALSE,
            &u4BufLen);
    
    if (rStatus == WLAN_STATUS_SUCCESS)
        i4Rslt = 0;

    return i4Rslt;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for getting key for specified STA
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int 
mtk_cfg80211_get_key (
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

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

#if 1
    printk("--> %s()\n", __func__);
#endif

    /* not implemented */

    return -EINVAL;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for removing key for specified STA
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_cfg80211_del_key (
    struct wiphy *wiphy,
    struct net_device *ndev,
    u8 key_index,
    bool pairwise,
    const u8 *mac_addr
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    PARAM_REMOVE_KEY_T rRemoveKey;
    UINT_32 u4BufLen = 0;
    INT_32 i4Rslt = -EINVAL;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

    kalMemZero(&rRemoveKey, sizeof(PARAM_REMOVE_KEY_T));
    if(mac_addr)
        COPY_MAC_ADDR(rRemoveKey.arBSSID, mac_addr);
    rRemoveKey.u4KeyIndex = key_index;
    rRemoveKey.u4Length = sizeof(PARAM_REMOVE_KEY_T);
    

    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetRemoveKey,
            &rRemoveKey,
            rRemoveKey.u4Length,
            FALSE,
            FALSE,
            TRUE,
            FALSE,
            &u4BufLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(REQ, WARN, ("remove key error:%lx\n", rStatus));
    }
    else {
        i4Rslt = 0;
    }

    return i4Rslt;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for setting default key on an interface
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int 
mtk_cfg80211_set_default_key (
    struct wiphy *wiphy,
    struct net_device *ndev,
    u8 key_index,
    bool unicast,
    bool multicast
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

#if 1
    printk("--> %s()\n", __func__);
#endif

    /* not implemented */

    return -EINVAL;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for getting station information such as RSSI
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/

int
mtk_cfg80211_get_station (
    struct wiphy *wiphy,
    struct net_device *ndev,
    u8 *mac,
    struct station_info *sinfo
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus;
    PARAM_MAC_ADDRESS arBssid;
    UINT_32 u4BufLen, u4Rate;
    INT_32 i4Rssi;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

    kalMemZero(arBssid, MAC_ADDR_LEN);
    wlanQueryInformation(prGlueInfo->prAdapter,
            wlanoidQueryBssid,
            &arBssid[0],
            sizeof(arBssid),
            &u4BufLen);

    /* 1. check BSSID */
    if(UNEQUAL_MAC_ADDR(arBssid, mac)) {
        /* wrong MAC address */
        DBGLOG(REQ, WARN, ("incorrect BSSID: ["MACSTR"] currently connected BSSID["MACSTR"]\n", 
                    MAC2STR(mac), MAC2STR(arBssid)));
        return -ENOENT;
    }

    /* 2. fill TX rate */
    rStatus = kalIoctl(prGlueInfo,
        wlanoidQueryLinkSpeed,
        &u4Rate,
        sizeof(u4Rate),
        TRUE,
        FALSE,
        FALSE,
        FALSE,
        &u4BufLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(REQ, WARN, ("unable to retrieve link speed\n"));
    }
    else {
        sinfo->filled |= STATION_INFO_TX_BITRATE;
        sinfo->txrate.legacy = u4Rate / 1000; /* convert from 100bps to 100kbps */
    }

    if(prGlueInfo->eParamMediaStateIndicated != PARAM_MEDIA_STATE_CONNECTED) {
        /* not connected */
        DBGLOG(REQ, WARN, ("not yet connected\n"));
    }
    else {
        /* 3. fill RSSI */
        rStatus = kalIoctl(prGlueInfo,
                wlanoidQueryRssi,
                &i4Rssi,
                sizeof(i4Rssi),
                TRUE,
                FALSE,
                FALSE,
                FALSE,
                &u4BufLen);
        
        if (rStatus != WLAN_STATUS_SUCCESS) {
            DBGLOG(REQ, WARN, ("unable to retrieve link speed\n"));
        }
        else {
            sinfo->filled |= STATION_INFO_SIGNAL;
            sinfo->signal = i4Rssi; /* dBm */
        }
    }

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to do a scan
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int 
mtk_cfg80211_scan (
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_scan_request *request
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus;
    UINT_32 u4BufLen;
    PARAM_SCAN_REQUEST_EXT_T rScanRequest;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);
    
    kalMemZero(&rScanRequest, sizeof(PARAM_SCAN_REQUEST_EXT_T));
    
    /* check if there is any pending scan not yet finished */
    if(prGlueInfo->prScanRequest != NULL) {
        return -EBUSY;
    }

    if(request->n_ssids == 0) {
        rScanRequest.rSsid.u4SsidLen = 0;
    }
    else if(request->n_ssids == 1) {
        COPY_SSID(rScanRequest.rSsid.aucSsid, rScanRequest.rSsid.u4SsidLen, request->ssids[0].ssid, request->ssids[0].ssid_len);
    }
    else {
        return -EINVAL;
    }

    if(request->ie_len > 0) {
        rScanRequest.u4IELength = request->ie_len;
        rScanRequest.pucIE = (PUINT_8)(request->ie);
    }

    rStatus = kalIoctl(prGlueInfo,
        wlanoidSetBssidListScanExt,
        &rScanRequest,
        sizeof(PARAM_SCAN_REQUEST_EXT_T),
        FALSE,
        FALSE,
        FALSE,
        FALSE,
        &u4BufLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(REQ, WARN, ("scan error:%lx\n", rStatus));
        return -EINVAL;
    }

    prGlueInfo->prScanRequest = request;

    return 0;
}

static UINT_8 wepBuf[48];

/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to connect to 
 *        the ESS with the specified parameters
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_cfg80211_connect (
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_connect_params *sme
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus;
    UINT_32 u4BufLen;
    ENUM_PARAM_ENCRYPTION_STATUS_T eEncStatus;
    ENUM_PARAM_AUTH_MODE_T eAuthMode;
    UINT_32 cipher;
    PARAM_SSID_T rNewSsid;
    BOOLEAN fgCarryWPSIE = FALSE;
    ENUM_PARAM_OP_MODE_T eOpMode;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

    if (prGlueInfo->prAdapter->rWifiVar.rConnSettings.eOPMode > NET_TYPE_AUTO_SWITCH)
		eOpMode = NET_TYPE_AUTO_SWITCH;
	else 
	    eOpMode = prGlueInfo->prAdapter->rWifiVar.rConnSettings.eOPMode;
	
	rStatus = kalIoctl(prGlueInfo,
		wlanoidSetInfrastructureMode,
		&eOpMode,
		sizeof(eOpMode),
		FALSE,
		FALSE,
		TRUE,
		FALSE,
		&u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(INIT, INFO, ("wlanoidSetInfrastructureMode fail 0x%lx\n", rStatus));
		return -EFAULT;
	}

	/* after set operation mode, key table are cleared */

	/* reset wpa info */
	prGlueInfo->rWpaInfo.u4WpaVersion = IW_AUTH_WPA_VERSION_DISABLED;
	prGlueInfo->rWpaInfo.u4KeyMgmt = 0;
	prGlueInfo->rWpaInfo.u4CipherGroup = IW_AUTH_CIPHER_NONE;
	prGlueInfo->rWpaInfo.u4CipherPairwise = IW_AUTH_CIPHER_NONE;
	prGlueInfo->rWpaInfo.u4AuthAlg = IW_AUTH_ALG_OPEN_SYSTEM;
#if CFG_SUPPORT_802_11W
	prGlueInfo->rWpaInfo.u4Mfp = IW_AUTH_MFP_DISABLED;
#endif

    if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)
        prGlueInfo->rWpaInfo.u4WpaVersion = IW_AUTH_WPA_VERSION_WPA;
    else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)
        prGlueInfo->rWpaInfo.u4WpaVersion = IW_AUTH_WPA_VERSION_WPA2;
    else
        prGlueInfo->rWpaInfo.u4WpaVersion = IW_AUTH_WPA_VERSION_DISABLED;
 
    switch (sme->auth_type) {
    case NL80211_AUTHTYPE_OPEN_SYSTEM:
        prGlueInfo->rWpaInfo.u4AuthAlg = IW_AUTH_ALG_OPEN_SYSTEM;
        break;
    case NL80211_AUTHTYPE_SHARED_KEY:
        prGlueInfo->rWpaInfo.u4AuthAlg = IW_AUTH_ALG_SHARED_KEY;
        break;
    default:
        prGlueInfo->rWpaInfo.u4AuthAlg = IW_AUTH_ALG_OPEN_SYSTEM | IW_AUTH_ALG_SHARED_KEY;
        break;
    }

    if (sme->crypto.n_ciphers_pairwise) {
        switch (sme->crypto.ciphers_pairwise[0]) {
        case WLAN_CIPHER_SUITE_WEP40:
            prGlueInfo->rWpaInfo.u4CipherPairwise = IW_AUTH_CIPHER_WEP40;
            break;
        case WLAN_CIPHER_SUITE_WEP104:
            prGlueInfo->rWpaInfo.u4CipherPairwise = IW_AUTH_CIPHER_WEP104;
            break;
        case WLAN_CIPHER_SUITE_TKIP:
            prGlueInfo->rWpaInfo.u4CipherPairwise = IW_AUTH_CIPHER_TKIP;
            break;
        case WLAN_CIPHER_SUITE_CCMP:
            prGlueInfo->rWpaInfo.u4CipherPairwise = IW_AUTH_CIPHER_CCMP;
            break;
        case WLAN_CIPHER_SUITE_AES_CMAC:
            prGlueInfo->rWpaInfo.u4CipherPairwise = IW_AUTH_CIPHER_CCMP;
            break;
        default:
            DBGLOG(REQ, WARN, ("invalid cipher pairwise (%d)\n",
                   sme->crypto.ciphers_pairwise[0]));
            return -EINVAL;
        }
    }

    if (sme->crypto.cipher_group) {
        switch (sme->crypto.cipher_group) {
        case WLAN_CIPHER_SUITE_WEP40:
            prGlueInfo->rWpaInfo.u4CipherGroup = IW_AUTH_CIPHER_WEP40;
            break;          
        case WLAN_CIPHER_SUITE_WEP104:
            prGlueInfo->rWpaInfo.u4CipherGroup = IW_AUTH_CIPHER_WEP104;
            break;
        case WLAN_CIPHER_SUITE_TKIP:
            prGlueInfo->rWpaInfo.u4CipherGroup = IW_AUTH_CIPHER_TKIP;
            break;
        case WLAN_CIPHER_SUITE_CCMP:
            prGlueInfo->rWpaInfo.u4CipherGroup = IW_AUTH_CIPHER_CCMP;
            break;
        case WLAN_CIPHER_SUITE_AES_CMAC:
            prGlueInfo->rWpaInfo.u4CipherGroup = IW_AUTH_CIPHER_CCMP;
            break;
        default:
            DBGLOG(REQ, WARN, ("invalid cipher group (%d)\n",
                   sme->crypto.cipher_group));
            return -EINVAL;
        }
    }

    if (sme->crypto.n_akm_suites) {
        if (prGlueInfo->rWpaInfo.u4WpaVersion == IW_AUTH_WPA_VERSION_WPA) {
            switch (sme->crypto.akm_suites[0]) {
            case WLAN_AKM_SUITE_8021X:
                eAuthMode = AUTH_MODE_WPA;
                break;
            case WLAN_AKM_SUITE_PSK:
                eAuthMode = AUTH_MODE_WPA_PSK;
            break;
            default:
                DBGLOG(REQ, WARN, ("invalid cipher group (%d)\n",
                       sme->crypto.cipher_group));
                return -EINVAL;
            }
        } else if (prGlueInfo->rWpaInfo.u4WpaVersion == IW_AUTH_WPA_VERSION_WPA2) {
            switch (sme->crypto.akm_suites[0]) {
            case WLAN_AKM_SUITE_8021X:
            eAuthMode = AUTH_MODE_WPA2;
            break;
            case WLAN_AKM_SUITE_PSK:
            eAuthMode = AUTH_MODE_WPA2_PSK;
            break;
            default:
                DBGLOG(REQ, WARN, ("invalid cipher group (%d)\n",
                       sme->crypto.cipher_group));
                return -EINVAL;
            }
        }
    }

    if (prGlueInfo->rWpaInfo.u4WpaVersion == IW_AUTH_WPA_VERSION_DISABLED) {
        eAuthMode = (prGlueInfo->rWpaInfo.u4AuthAlg == IW_AUTH_ALG_OPEN_SYSTEM) ?
             AUTH_MODE_OPEN : AUTH_MODE_AUTO_SWITCH;
    }

    prGlueInfo->rWpaInfo.fgPrivacyInvoke = sme->privacy;

    //prGlueInfo->prAdapter->rWifiVar.rConnSettings.fgWapiMode = FALSE;
    //prGlueInfo->prAdapter->prGlueInfo->u2WapiAssocInfoIESz = 0;
    prGlueInfo->fgWpsActive = FALSE;
    //prGlueInfo->prAdapter->prGlueInfo->u2WSCAssocInfoIELen = 0;

    if (sme->ie && sme->ie_len > 0) {
        WLAN_STATUS rStatus;
        UINT_32 u4BufLen;
        PUINT_8 prDesiredIE = NULL;

#if CFG_SUPPORT_WAPI
        rStatus = kalIoctl(prGlueInfo,
                wlanoidSetWapiAssocInfo,
                sme->ie,
                sme->ie_len,
                FALSE,
                FALSE,
                FALSE,
                FALSE,
                &u4BufLen);
        
        if (rStatus != WLAN_STATUS_SUCCESS) {
            DBGLOG(SEC, WARN, ("[wapi] set wapi assoc info error:%lx\n", rStatus));
        }
#endif
#if CFG_SUPPORT_WPS2
        if (wextSrchDesiredWPSIE(sme->ie,
                    sme->ie_len,
                    0xDD,
                    (PUINT_8 *)&prDesiredIE)) {
            prGlueInfo->fgWpsActive = TRUE;
            fgCarryWPSIE = TRUE;

            rStatus = kalIoctl(prGlueInfo,
                    wlanoidSetWSCAssocInfo,
                    prDesiredIE,
                    IE_SIZE(prDesiredIE),
                    FALSE,
                    FALSE,
                    FALSE,
                    FALSE,
                    &u4BufLen);
            if (rStatus != WLAN_STATUS_SUCCESS) {
                DBGLOG(SEC, WARN, ("WSC] set WSC assoc info error:%lx\n", rStatus));
            }
        }
#endif
    }

    /* clear WSC Assoc IE buffer in case WPS IE is not detected */
    if(fgCarryWPSIE == FALSE) {
        kalMemZero(&prGlueInfo->aucWSCAssocInfoIE, 200);
        prGlueInfo->u2WSCAssocInfoIELen = 0;
    }

    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetAuthMode,
            &eAuthMode,
            sizeof(eAuthMode),
            FALSE,
            FALSE,
            FALSE,
            FALSE,
            &u4BufLen);
    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(REQ, WARN, ("set auth mode error:%lx\n", rStatus));
    }

    cipher = prGlueInfo->rWpaInfo.u4CipherGroup | prGlueInfo->rWpaInfo.u4CipherPairwise;

    if (prGlueInfo->rWpaInfo.fgPrivacyInvoke) {
        if (cipher & IW_AUTH_CIPHER_CCMP) {
            eEncStatus = ENUM_ENCRYPTION3_ENABLED;
        }
        else if (cipher & IW_AUTH_CIPHER_TKIP) {
            eEncStatus = ENUM_ENCRYPTION2_ENABLED;
        }
        else if (cipher & (IW_AUTH_CIPHER_WEP104 | IW_AUTH_CIPHER_WEP40)) {
            eEncStatus = ENUM_ENCRYPTION1_ENABLED;
        }
        else if (cipher & IW_AUTH_CIPHER_NONE){
            if (prGlueInfo->rWpaInfo.fgPrivacyInvoke)
                eEncStatus = ENUM_ENCRYPTION1_ENABLED;
            else
                eEncStatus = ENUM_ENCRYPTION_DISABLED;
        }
        else {
            eEncStatus = ENUM_ENCRYPTION_DISABLED;
        }
    }
    else {
        eEncStatus = ENUM_ENCRYPTION_DISABLED;
    }
    
    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetEncryptionStatus,
            &eEncStatus,
            sizeof(eEncStatus),
            FALSE,
            FALSE,
            FALSE,
            FALSE,
            &u4BufLen);
    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(REQ, WARN, ("set encryption mode error:%lx\n", rStatus));
    }

    if (sme->key_len != 0 && prGlueInfo->rWpaInfo.u4WpaVersion == IW_AUTH_WPA_VERSION_DISABLED) {
        P_PARAM_WEP_T prWepKey = (P_PARAM_WEP_T) wepBuf;
        
        kalMemSet(prWepKey, 0, sizeof(prWepKey));
        prWepKey->u4Length = 12 + sme->key_len;
        prWepKey->u4KeyLength = (UINT_32) sme->key_len;
        prWepKey->u4KeyIndex = (UINT_32) sme->key_idx;
        prWepKey->u4KeyIndex |= BIT(31);
        if (prWepKey->u4KeyLength > 32) {
            DBGLOG(REQ, WARN, ("Too long key length (%u)\n", prWepKey->u4KeyLength));
            return -EINVAL;
        }
        kalMemCopy(prWepKey->aucKeyMaterial, sme->key, prWepKey->u4KeyLength);

        rStatus = kalIoctl(prGlueInfo,
                     wlanoidSetAddWep,
                     prWepKey,
                     prWepKey->u4Length,
                     FALSE,
                     FALSE,
                     TRUE,
                     FALSE,
                     &u4BufLen);
        
        if (rStatus != WLAN_STATUS_SUCCESS) {
            DBGLOG(INIT, INFO, ("wlanoidSetAddWep fail 0x%lx\n", rStatus));
            return -EFAULT;
        }
    }

    if(sme->ssid_len > 0) {
        /* connect by SSID */
        COPY_SSID(rNewSsid.aucSsid, rNewSsid.u4SsidLen, sme->ssid, sme->ssid_len);

        rStatus = kalIoctl(prGlueInfo,
                wlanoidSetSsid,
                (PVOID) &rNewSsid,
                sizeof(PARAM_SSID_T),
                FALSE,
                FALSE,
                TRUE,
                FALSE,
                &u4BufLen);

        if (rStatus != WLAN_STATUS_SUCCESS) {
            DBGLOG(REQ, WARN, ("set SSID:%lx\n", rStatus));
            return -EINVAL;
        }
    }
    else {
        /* connect by BSSID */
        rStatus = kalIoctl(prGlueInfo,
                wlanoidSetBssid,
                (PVOID) sme->bssid,
                sizeof(MAC_ADDR_LEN),
                FALSE,
                FALSE,
                TRUE,
                FALSE,
                &u4BufLen);

        if (rStatus != WLAN_STATUS_SUCCESS) {
            DBGLOG(REQ, WARN, ("set BSSID:%lx\n", rStatus));
            return -EINVAL;
        }
    }

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to disconnect from
 *        currently connected ESS
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int 
mtk_cfg80211_disconnect (
    struct wiphy *wiphy,
    struct net_device *ndev,
    u16 reason_code
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus;
    UINT_32 u4BufLen;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

    rStatus = kalIoctl(prGlueInfo,
        wlanoidSetDisassociate,
        NULL,
        0,
        FALSE,
        FALSE,
        TRUE,
        FALSE,
        &u4BufLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(REQ, WARN, ("disassociate error:%lx\n", rStatus));
        return -EFAULT;
    }

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to join an IBSS group
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_cfg80211_join_ibss (
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_ibss_params *params
    )
{
    PARAM_SSID_T rNewSsid;
    P_GLUE_INFO_T prGlueInfo = NULL;
    UINT_32 u4ChnlFreq; /* Store channel or frequency information */
    UINT_32 u4BufLen = 0;
    WLAN_STATUS rStatus;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

    /* set channel */
    if(params->channel) {
        u4ChnlFreq = nicChannelNum2Freq(params->channel->hw_value);

        rStatus = kalIoctl(prGlueInfo,
                           wlanoidSetFrequency,
                           &u4ChnlFreq,
                           sizeof(u4ChnlFreq),
                           FALSE,
                           FALSE,
                           FALSE,
                           FALSE,
                           &u4BufLen);
        if (rStatus != WLAN_STATUS_SUCCESS) {
            return -EFAULT;
        }
    }

    /* set SSID */
    kalMemCopy(rNewSsid.aucSsid, params->ssid, params->ssid_len);
    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetSsid,
            (PVOID) &rNewSsid,
            sizeof(PARAM_SSID_T),
            FALSE,
            FALSE,
            TRUE,
            FALSE,
            &u4BufLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(REQ, WARN, ("set SSID:%lx\n", rStatus));
        return -EFAULT;
    }

    return 0;


    return -EINVAL;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to leave from IBSS group
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_cfg80211_leave_ibss (
    struct wiphy *wiphy,
    struct net_device *ndev
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus;
    UINT_32 u4BufLen;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

    rStatus = kalIoctl(prGlueInfo,
        wlanoidSetDisassociate,
        NULL,
        0,
        FALSE,
        FALSE,
        TRUE,
        FALSE,
        &u4BufLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(REQ, WARN, ("disassociate error:%lx\n", rStatus));
        return -EFAULT;
    }

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to configure 
 *        WLAN power managemenet
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_cfg80211_set_power_mgmt (
    struct wiphy *wiphy,
    struct net_device *ndev,
    bool enabled,
    int timeout
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus;
    UINT_32 u4BufLen;
    PARAM_POWER_MODE ePowerMode;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

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

    rStatus = kalIoctl(prGlueInfo,
        wlanoidSet802dot11PowerSaveProfile,
        &ePowerMode,
        sizeof(ePowerMode),
        FALSE,
        FALSE,
        TRUE,
        FALSE,
        &u4BufLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(REQ, WARN, ("set_power_mgmt error:%lx\n", rStatus));
        return -EFAULT;
    }

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to cache
 *        a PMKID for a BSSID
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_cfg80211_set_pmksa (
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_pmksa *pmksa
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS    rStatus;
    UINT_32        u4BufLen;
    P_PARAM_PMKID_T  prPmkid;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

    prPmkid =(P_PARAM_PMKID_T)kalMemAlloc(8 + sizeof(PARAM_BSSID_INFO_T), VIR_MEM_TYPE);
    if (!prPmkid) {
        DBGLOG(INIT, INFO, ("Can not alloc memory for IW_PMKSA_ADD\n"));
        return -ENOMEM;
    }
    
    prPmkid->u4Length = 8 + sizeof(PARAM_BSSID_INFO_T);
    prPmkid->u4BSSIDInfoCount = 1;
    kalMemCopy(prPmkid->arBSSIDInfo->arBSSID, pmksa->bssid, 6);
    kalMemCopy(prPmkid->arBSSIDInfo->arPMKID, pmksa->pmkid, IW_PMKID_LEN);
    
    rStatus = kalIoctl(prGlueInfo,
                 wlanoidSetPmkid,
                 prPmkid,
                 sizeof(PARAM_PMKID_T),
                 FALSE,
                 FALSE,
                 FALSE,
                 FALSE,
                 &u4BufLen);
    
    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(INIT, INFO, ("add pmkid error:%lx\n", rStatus));
    }
    kalMemFree(prPmkid, VIR_MEM_TYPE, 8 + sizeof(PARAM_BSSID_INFO_T));

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to remove
 *        a cached PMKID for a BSSID
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_cfg80211_del_pmksa (
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_pmksa *pmksa
    )
{

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to flush
 *        all cached PMKID
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_cfg80211_flush_pmksa (
    struct wiphy *wiphy,
    struct net_device *ndev
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS    rStatus;
    UINT_32        u4BufLen;
    P_PARAM_PMKID_T  prPmkid;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

    prPmkid =(P_PARAM_PMKID_T)kalMemAlloc(8, VIR_MEM_TYPE);
    if (!prPmkid) {
        DBGLOG(INIT, INFO, ("Can not alloc memory for IW_PMKSA_FLUSH\n"));
        return -ENOMEM;
    }
    
    prPmkid->u4Length = 8;
    prPmkid->u4BSSIDInfoCount = 0;
    
    rStatus = kalIoctl(prGlueInfo,
                 wlanoidSetPmkid,
                 prPmkid,
                 sizeof(PARAM_PMKID_T),
                 FALSE,
                 FALSE,
                 FALSE,
                 FALSE,
                 &u4BufLen);
    
    if (rStatus != WLAN_STATUS_SUCCESS) {
        DBGLOG(INIT, INFO, ("flush pmkid error:%lx\n", rStatus));
    }
    kalMemFree(prPmkid, VIR_MEM_TYPE, 8);

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to stay on a 
 *        specified channel
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int 
mtk_cfg80211_remain_on_channel (
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct ieee80211_channel *chan,
    enum nl80211_channel_type channel_type,
    unsigned int duration,
    u64 *cookie
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

#if 1
    printk("--> %s()\n", __func__);
#endif

    /* not implemented */

    return -EINVAL;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to cancel staying 
 *        on a specified channel
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_cfg80211_cancel_remain_on_channel (
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

    return -EINVAL;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is responsible for requesting to send a management frame
 *
 * @param 
 *
 * @retval 0:       successful
 *         others:  failure
 */
/*----------------------------------------------------------------------------*/
int
mtk_cfg80211_mgmt_tx (
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct ieee80211_channel *channel,
    bool offscan,
    enum nl80211_channel_type channel_type,
    bool channel_type_valid,
    unsigned int wait,
    const u8 *buf,
    size_t len,
    bool no_cck,
    bool dont_wait_for_ack,
    u64 *cookie
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
    ASSERT(prGlueInfo);

#if 1
    printk("--> %s()\n", __func__);
#endif

    /* not implemented */

    return -EINVAL;
}


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
mtk_cfg80211_mgmt_tx_cancel_wait (
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

    return -EINVAL;
}


#if CONFIG_NL80211_TESTMODE

#if CFG_SUPPORT_WAPI
int
mtk_cfg80211_testmode_set_key_ext(
    IN struct wiphy *wiphy,
    IN void *data,
    IN int len)
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_NL80211_DRIVER_SET_KEY_EXTS prParams = (P_NL80211_DRIVER_SET_KEY_EXTS)NULL;
    struct iw_encode_exts *prIWEncExt = (struct iw_encode_exts *)NULL;
    WLAN_STATUS rstatus = WLAN_STATUS_SUCCESS;
    int     fgIsValid = 0;
    UINT_32 u4BufLen = 0;
    
    P_PARAM_WPI_KEY_T prWpiKey = (P_PARAM_WPI_KEY_T) keyStructBuf;
    memset(keyStructBuf, 0, sizeof(keyStructBuf));

    ASSERT(wiphy);
    
    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);
#if 1
        printk("--> %s()\n", __func__);
#endif

    if(data && len) {
        prParams = (P_NL80211_DRIVER_SET_KEY_EXTS)data;
    }
    
    if(prParams) {
        prIWEncExt = (struct iw_encode_exts *) &prParams->ext;
    }

    if (prIWEncExt->alg == IW_ENCODE_ALG_SMS4) {
        /* KeyID */
        prWpiKey->ucKeyID = prParams->key_index;
        prWpiKey->ucKeyID --;
        if (prWpiKey->ucKeyID > 1) {
            /* key id is out of range */
            //printk(KERN_INFO "[wapi] add key error: key_id invalid %d\n", prWpiKey->ucKeyID);
            return -EINVAL;
        }

        if (prIWEncExt->key_len != 32) {
            /* key length not valid */
            //printk(KERN_INFO "[wapi] add key error: key_len invalid %d\n", prIWEncExt->key_len);
            return -EINVAL;
        }

        //printk(KERN_INFO "[wapi] %d ext_flags %d\n", prEnc->flags, prIWEncExt->ext_flags);

        if (prIWEncExt->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
            prWpiKey->eKeyType = ENUM_WPI_GROUP_KEY;
            prWpiKey->eDirection = ENUM_WPI_RX;
        }
        else if (prIWEncExt->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
            prWpiKey->eKeyType = ENUM_WPI_PAIRWISE_KEY;
            prWpiKey->eDirection = ENUM_WPI_RX_TX;
        }

//#if CFG_SUPPORT_WAPI
        //handle_sec_msg_final(prIWEncExt->key, 32, prIWEncExt->key, NULL);
//#endif
        /* PN */
        memcpy(prWpiKey->aucPN, prIWEncExt->tx_seq, IW_ENCODE_SEQ_MAX_SIZE * 2);

        /* BSSID */
        memcpy(prWpiKey->aucAddrIndex, prIWEncExt->addr, 6);

        memcpy(prWpiKey->aucWPIEK, prIWEncExt->key, 16);
        prWpiKey->u4LenWPIEK = 16;

        memcpy(prWpiKey->aucWPICK, &prIWEncExt->key[16], 16);
        prWpiKey->u4LenWPICK = 16;

        rstatus = kalIoctl(prGlueInfo,
                     wlanoidSetWapiKey,
                     prWpiKey,
                     sizeof(PARAM_WPI_KEY_T),
                     FALSE,
                     FALSE,
                     TRUE,
                     FALSE,
                     &u4BufLen);

        if (rstatus != WLAN_STATUS_SUCCESS) {
            //printk(KERN_INFO "[wapi] add key error:%lx\n", rStatus);
            fgIsValid = -EFAULT;
        }

    }
    return fgIsValid;
}
#endif


int
mtk_cfg80211_testmode_sw_cmd(
    IN struct wiphy *wiphy,
    IN void *data,
    IN int len)
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_NL80211_DRIVER_SW_CMD_PARAMS prParams = (P_NL80211_DRIVER_SW_CMD_PARAMS)NULL;
    WLAN_STATUS rstatus = WLAN_STATUS_SUCCESS;
    int     fgIsValid = 0;
    UINT_32 u4SetInfoLen = 0;

    ASSERT(wiphy);

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);

#if 1
        printk("--> %s()\n", __func__);
#endif

    if(data && len)
        prParams = (P_NL80211_DRIVER_SW_CMD_PARAMS)data;

    if(prParams) {
        if(prParams->set == 1){
            rstatus = kalIoctl(prGlueInfo,
                    (PFN_OID_HANDLER_FUNC)wlanoidSetSwCtrlWrite,
                    &prParams->adr,
                    (UINT_32)8,
                    FALSE,
                    FALSE,
                    TRUE,
                    FALSE,
                    &u4SetInfoLen);
        }
    }

    if (WLAN_STATUS_SUCCESS != rstatus) {
        fgIsValid = -EFAULT;
    }
 
    return fgIsValid;
}

int mtk_cfg80211_testmode_cmd(
    IN struct wiphy *wiphy,
    IN void *data,
    IN int len
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_NL80211_DRIVER_TEST_MODE_PARAMS prParams = (P_NL80211_DRIVER_TEST_MODE_PARAMS)NULL;
    BOOLEAN fgIsValid = 0;

    ASSERT(wiphy);

    prGlueInfo = (P_GLUE_INFO_T) wiphy_priv(wiphy);

#if 1
    printk("--> %s()\n", __func__);
#endif

    if(data && len)
        prParams = (P_NL80211_DRIVER_TEST_MODE_PARAMS)data;
        
    /* Clear the version byte */
    prParams->index = prParams->index & ~ BITS(24,31);
    
    if(prParams){
        switch(prParams->index){
            case 1: /* SW cmd */
                if(mtk_cfg80211_testmode_sw_cmd(wiphy, data, len))
                    fgIsValid = TRUE;
                break;
            case 2: /* WAPI */
#if CFG_SUPPORT_WAPI
                if(mtk_cfg80211_testmode_set_key_ext(wiphy, data, len))
                    fgIsValid = TRUE;
#endif
                break;
            default:
                fgIsValid = TRUE;
                break;
        }
    }


    return fgIsValid;
}
#endif

