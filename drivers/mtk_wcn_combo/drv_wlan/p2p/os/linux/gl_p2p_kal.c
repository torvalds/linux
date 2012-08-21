/*
** $Id: @(#) gl_p2p_cfg80211.c@@
*/

/*! \file   gl_p2p_kal.c
    \brief

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


/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "net/cfg80211.h"
#include "p2p_precomp.h"

extern BOOLEAN
wextSrchDesiredWPAIE (
    IN  PUINT_8         pucIEStart,
    IN  INT_32          i4TotalIeLen,
    IN  UINT_8          ucDesiredElemId,
    OUT PUINT_8         *ppucDesiredIE
    );

#if CFG_SUPPORT_WPS
extern BOOLEAN
wextSrchDesiredWPSIE (
    IN PUINT_8 pucIEStart,
    IN INT_32 i4TotalIeLen,
    IN UINT_8 ucDesiredElemId,
    OUT PUINT_8 *ppucDesiredIE
    );
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
kalP2pFuncGetChannelType(
    IN ENUM_CHNL_EXT_T rChnlSco,
    OUT enum nl80211_channel_type *channel_type
    );


struct ieee80211_channel *
kalP2pFuncGetChannelEntry(
    IN P_GL_P2P_INFO_T prP2pInfo,
    IN P_RF_CHANNEL_INFO_T prChannelInfo
    );


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Wi-Fi Direct state from glue layer
*
* \param[in]
*           prGlueInfo
*           rPeerAddr
* \return
*           ENUM_BOW_DEVICE_STATE
*/
/*----------------------------------------------------------------------------*/
ENUM_PARAM_MEDIA_STATE_T
kalP2PGetState (
    IN P_GLUE_INFO_T        prGlueInfo
    )
{
    ASSERT(prGlueInfo);

    return prGlueInfo->prP2PInfo->eState;
} /* end of kalP2PGetState() */


/*----------------------------------------------------------------------------*/
/*!
* \brief to update the assoc req to p2p
*
* \param[in]
*           prGlueInfo
*           pucFrameBody
*           u4FrameBodyLen
*           fgReassocRequest
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PUpdateAssocInfo (
    IN P_GLUE_INFO_T    prGlueInfo,
    IN PUINT_8          pucFrameBody,
    IN UINT_32          u4FrameBodyLen,
    IN BOOLEAN          fgReassocRequest
    )
{
    union iwreq_data wrqu;
    unsigned char *pucExtraInfo = NULL;
    unsigned char *pucDesiredIE = NULL;
//    unsigned char aucExtraInfoBuf[200];
    PUINT_8             cp;

    memset(&wrqu, 0, sizeof(wrqu));

    if (fgReassocRequest) {
        if (u4FrameBodyLen < 15) {
            /*
            printk(KERN_WARNING "frameBodyLen too short:%ld\n", frameBodyLen);
            */
            return;
        }
    }
    else {
        if (u4FrameBodyLen < 9) {
            /*
            printk(KERN_WARNING "frameBodyLen too short:%ld\n", frameBodyLen);
            */
            return;
        }
    }

    cp = pucFrameBody;

    if (fgReassocRequest) {
        /* Capability information field 2 */
        /* Listen interval field 2*/
        /* Current AP address 6 */
        cp += 10;
        u4FrameBodyLen -= 10;
    }
    else {
        /* Capability information field 2 */
        /* Listen interval field 2*/
        cp += 4;
        u4FrameBodyLen -= 4;
    }

    /* do supplicant a favor, parse to the start of WPA/RSN IE */
    if (wextSrchDesiredWPSIE(cp, u4FrameBodyLen, 0xDD, &pucDesiredIE)) {
        //printk("wextSrchDesiredWPSIE!!\n");
        /* WPS IE found */
    }
    else if (wextSrchDesiredWPAIE(cp, u4FrameBodyLen, 0x30, &pucDesiredIE)) {
        //printk("wextSrchDesiredWPAIE!!\n");
        /* RSN IE found */
    }
    else if (wextSrchDesiredWPAIE(cp, u4FrameBodyLen, 0xDD, &pucDesiredIE)) {
        //printk("wextSrchDesiredWPAIE!!\n");
        /* WPA IE found */
    }
    else {
        /* no WPA/RSN IE found, skip this event */
        goto skip_indicate_event;
    }

     /* IWEVASSOCREQIE, indicate binary string */
    pucExtraInfo = pucDesiredIE;
    wrqu.data.length = pucDesiredIE[1] + 2;

    /* Send event to user space */
    wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler, IWEVASSOCREQIE, &wrqu, pucExtraInfo);

skip_indicate_event:
    return;

}


/*----------------------------------------------------------------------------*/
/*!
* \brief to set Wi-Fi Direct state in glue layer
*
* \param[in]
*           prGlueInfo
*           eBowState
*           rPeerAddr
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PSetState (
    IN P_GLUE_INFO_T            prGlueInfo,
    IN ENUM_PARAM_MEDIA_STATE_T eState,
    IN PARAM_MAC_ADDRESS        rPeerAddr,
    IN UINT_8                   ucRole
    )
{
    union iwreq_data evt;
    UINT_8 aucBuffer[IW_CUSTOM_MAX];

    ASSERT(prGlueInfo);

    memset(&evt, 0, sizeof(evt));

    if(eState == PARAM_MEDIA_STATE_CONNECTED) {
        prGlueInfo->prP2PInfo->eState = PARAM_MEDIA_STATE_CONNECTED;

        snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_STA_CONNECT="MACSTR, MAC2STR(rPeerAddr));
        evt.data.length = strlen(aucBuffer);

        /* indicate in IWECUSTOM event */
        wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler,
                IWEVCUSTOM,
                &evt,
                aucBuffer);

    }
    else if(eState == PARAM_MEDIA_STATE_DISCONNECTED) {
        snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_STA_DISCONNECT="MACSTR, MAC2STR(rPeerAddr));
        evt.data.length = strlen(aucBuffer);

        /* indicate in IWECUSTOM event */
        wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler,
                IWEVCUSTOM,
                &evt,
                aucBuffer);
    }
    else {
        ASSERT(0);
    }

    return;
} /* end of kalP2PSetState() */


/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Wi-Fi Direct operating frequency
*
* \param[in]
*           prGlueInfo
*
* \return
*           in unit of KHz
*/
/*----------------------------------------------------------------------------*/
UINT_32
kalP2PGetFreqInKHz(
    IN P_GLUE_INFO_T            prGlueInfo
    )
{
    ASSERT(prGlueInfo);

    return prGlueInfo->prP2PInfo->u4FreqInKHz;
} /* end of kalP2PGetFreqInKHz() */


/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Bluetooth-over-Wi-Fi role
*
* \param[in]
*           prGlueInfo
*
* \return
*           0: P2P Device
*           1: Group Client
*           2: Group Owner
*/
/*----------------------------------------------------------------------------*/
UINT_8
kalP2PGetRole(
    IN P_GLUE_INFO_T        prGlueInfo
    )
{
    ASSERT(prGlueInfo);

    return prGlueInfo->prP2PInfo->ucRole;
} /* end of kalP2PGetRole() */


/*----------------------------------------------------------------------------*/
/*!
* \brief to set Wi-Fi Direct role
*
* \param[in]
*           prGlueInfo
*           ucResult
*                   0: successful
*                   1: error
*           ucRole
*                   0: P2P Device
*                   1: Group Client
*                   2: Group Owner
*
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PSetRole(
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_8           ucResult,
    IN PUINT_8          pucSSID,
    IN UINT_8           ucSSIDLen,
    IN UINT_8           ucRole
    )
{
    union iwreq_data evt;
    UINT_8 aucBuffer[IW_CUSTOM_MAX];

    ASSERT(prGlueInfo);
    ASSERT(ucRole <= 2);

    memset(&evt, 0, sizeof(evt));

    if(ucResult == 0) {
        prGlueInfo->prP2PInfo->ucRole = ucRole;
    }

    if (pucSSID)
        snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_FORMATION_RST=%d%d%d%c%c", ucResult, ucRole, 1/* persistence or not */, pucSSID[7], pucSSID[8]);
    else
        snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_FORMATION_RST=%d%d%d%c%c", ucResult, ucRole, 1/* persistence or not */, '0', '0');

    evt.data.length = strlen(aucBuffer);

    //if (pucSSID)
    //    printk("P2P GO SSID DIRECT-%c%c\n", pucSSID[7], pucSSID[8]);

    /* indicate in IWECUSTOM event */
    wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler,
            IWEVCUSTOM,
            &evt,
            aucBuffer);

    return;
} /* end of kalP2PSetRole() */


/*----------------------------------------------------------------------------*/
/*!
* \brief to set the cipher for p2p
*
* \param[in]
*           prGlueInfo
*           u4Cipher
*
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PSetCipher(
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_32          u4Cipher
    )
{
    ASSERT(prGlueInfo);
    ASSERT(prGlueInfo->prP2PInfo);

    prGlueInfo->prP2PInfo->u4CipherPairwise = u4Cipher;

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to get the cipher, return for cipher is ccmp
*
* \param[in]
*           prGlueInfo
*
* \return
*           TRUE: cipher is ccmp
*           FALSE: cipher is none
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
kalP2PGetCipher (
    IN P_GLUE_INFO_T    prGlueInfo
    )
{
    ASSERT(prGlueInfo);
    ASSERT(prGlueInfo->prP2PInfo);

    if (prGlueInfo->prP2PInfo->u4CipherPairwise == IW_AUTH_CIPHER_CCMP)
        return TRUE;

    if (prGlueInfo->prP2PInfo->u4CipherPairwise == IW_AUTH_CIPHER_TKIP)
        return TRUE;

    return FALSE;
}

BOOLEAN
kalP2PGetCcmpCipher (
    IN P_GLUE_INFO_T    prGlueInfo
    )
{
    ASSERT(prGlueInfo);
    ASSERT(prGlueInfo->prP2PInfo);

    if (prGlueInfo->prP2PInfo->u4CipherPairwise == IW_AUTH_CIPHER_CCMP)
        return TRUE;

    if (prGlueInfo->prP2PInfo->u4CipherPairwise == IW_AUTH_CIPHER_TKIP)
        return FALSE;

    return FALSE;
}


BOOLEAN
kalP2PGetTkipCipher (
    IN P_GLUE_INFO_T    prGlueInfo
    )
{
    ASSERT(prGlueInfo);
    ASSERT(prGlueInfo->prP2PInfo);

    if (prGlueInfo->prP2PInfo->u4CipherPairwise == IW_AUTH_CIPHER_CCMP)
        return FALSE;

    if (prGlueInfo->prP2PInfo->u4CipherPairwise == IW_AUTH_CIPHER_TKIP)
        return TRUE;

    return FALSE;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief to set the status of WSC
*
* \param[in]
*           prGlueInfo
*
* \return
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PSetWscMode (
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_8           ucWscMode
    )
{
    ASSERT(prGlueInfo);
    ASSERT(prGlueInfo->prP2PInfo);

    prGlueInfo->prP2PInfo->ucWSCRunning = ucWscMode;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to get the status of WSC
*
* \param[in]
*           prGlueInfo
*
* \return
*/
/*----------------------------------------------------------------------------*/
UINT_8
kalP2PGetWscMode (
    IN P_GLUE_INFO_T    prGlueInfo
    )
{
    ASSERT(prGlueInfo);
    ASSERT(prGlueInfo->prP2PInfo);

    return (prGlueInfo->prP2PInfo->ucWSCRunning);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to get the wsc ie length
*
* \param[in]
*           prGlueInfo
*           ucType : 0 for beacon, 1 for probe req, 2 for probe resp
*
* \return
*           The WSC IE length
*/
/*----------------------------------------------------------------------------*/
UINT_16
kalP2PCalWSC_IELen (
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_8           ucType
    )
{
    ASSERT(prGlueInfo);

    ASSERT(ucType < 3);

    return prGlueInfo->prP2PInfo->u2WSCIELen[ucType];
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to copy the wsc ie setting from p2p supplicant
*
* \param[in]
*           prGlueInfo
*
* \return
*           The WPS IE length
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PGenWSC_IE (
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_8           ucType,
    IN PUINT_8          pucBuffer
    )
{
    P_GL_P2P_INFO_T prGlP2pInfo = (P_GL_P2P_INFO_T)NULL;

    do {
        if ((prGlueInfo == NULL) ||
                (ucType >= 3) ||
                (pucBuffer == NULL)) {
            break;
        }


        prGlP2pInfo = prGlueInfo->prP2PInfo;

        kalMemCopy(pucBuffer, prGlP2pInfo->aucWSCIE[ucType], prGlP2pInfo->u2WSCIELen[ucType]);

    } while (FALSE);

    return;
}


VOID
kalP2PUpdateWSC_IE (
    IN P_GLUE_INFO_T prGlueInfo,
    IN UINT_8 ucType,
    IN PUINT_8 pucBuffer,
    IN UINT_16 u2BufferLength
    )
{
    P_GL_P2P_INFO_T prGlP2pInfo = (P_GL_P2P_INFO_T)NULL;

    do {
        if ((prGlueInfo == NULL) ||
                (ucType >= 3) ||
                ((u2BufferLength > 0) && (pucBuffer == NULL))) {
            break;
        }


        if (u2BufferLength > 400) {
            DBGLOG(P2P, ERROR, ("Buffer length is not enough, GLUE only 400 bytes but %d received\n", u2BufferLength));
            ASSERT(FALSE);
            break;
        }


        prGlP2pInfo = prGlueInfo->prP2PInfo;

        kalMemCopy(prGlP2pInfo->aucWSCIE[ucType], pucBuffer, u2BufferLength);

        prGlP2pInfo->u2WSCIELen[ucType] = u2BufferLength;


    } while (FALSE);

    return;
} /* kalP2PUpdateWSC_IE */



/*----------------------------------------------------------------------------*/
/*!
* \brief indicate an event to supplicant for device connection request
*
* \param[in] prGlueInfo Pointer of GLUE_INFO_T
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PIndicateConnReq(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN PUINT_8              pucDevName,
    IN INT_32               u4NameLength,
    IN PARAM_MAC_ADDRESS    rPeerAddr,
    IN UINT_8               ucDevType, /* 0: P2P Device / 1: GC / 2: GO */
    IN INT_32               i4ConfigMethod,
    IN INT_32               i4ActiveConfigMethod
    )
{
    union iwreq_data evt;
    UINT_8 aucBuffer[IW_CUSTOM_MAX];

    ASSERT(prGlueInfo);

    /* buffer peer information for later IOC_P2P_GET_REQ_DEVICE_INFO access */
    prGlueInfo->prP2PInfo->u4ConnReqNameLength = u4NameLength > 32 ? 32 : u4NameLength;
    kalMemCopy(prGlueInfo->prP2PInfo->aucConnReqDevName,
            pucDevName,
            prGlueInfo->prP2PInfo->u4ConnReqNameLength);
    COPY_MAC_ADDR(prGlueInfo->prP2PInfo->rConnReqPeerAddr, rPeerAddr);
    prGlueInfo->prP2PInfo->ucConnReqDevType = ucDevType;
    prGlueInfo->prP2PInfo->i4ConnReqConfigMethod = i4ConfigMethod;
    prGlueInfo->prP2PInfo->i4ConnReqActiveConfigMethod = i4ActiveConfigMethod;

    // prepare event structure
    memset(&evt, 0, sizeof(evt));

    snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_DVC_REQ");
    evt.data.length = strlen(aucBuffer);

    /* indicate in IWEVCUSTOM event */
    wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler,
            IWEVCUSTOM,
            &evt,
            aucBuffer);

    return;
} /* end of kalP2PIndicateConnReq() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Indicate an event to supplicant for device connection request from other device.
*
* \param[in] prGlueInfo Pointer of GLUE_INFO_T
* \param[in] pucGroupBssid  Only valid when invitation Type equals to 0.
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PInvitationIndication (
    IN P_GLUE_INFO_T prGlueInfo,
    IN P_P2P_DEVICE_DESC_T prP2pDevDesc,
    IN PUINT_8 pucSsid,
    IN UINT_8 ucSsidLen,
    IN UINT_8 ucOperatingChnl,
    IN UINT_8 ucInvitationType,
    IN PUINT_8 pucGroupBssid
    )
{
#if 1
    union iwreq_data evt;
    UINT_8 aucBuffer[IW_CUSTOM_MAX];

    ASSERT(prGlueInfo);

    /* buffer peer information for later IOC_P2P_GET_STRUCT access */
    prGlueInfo->prP2PInfo->u4ConnReqNameLength = (UINT_32)((prP2pDevDesc->u2NameLength > 32)? 32 : prP2pDevDesc->u2NameLength);
    kalMemCopy(prGlueInfo->prP2PInfo->aucConnReqDevName,
                    prP2pDevDesc->aucName,
                    prGlueInfo->prP2PInfo->u4ConnReqNameLength);
    COPY_MAC_ADDR(prGlueInfo->prP2PInfo->rConnReqPeerAddr, prP2pDevDesc->aucDeviceAddr);
    COPY_MAC_ADDR(prGlueInfo->prP2PInfo->rConnReqGroupAddr, pucGroupBssid);
    prGlueInfo->prP2PInfo->i4ConnReqConfigMethod = (INT_32)(prP2pDevDesc->u2ConfigMethod);
    prGlueInfo->prP2PInfo->ucOperatingChnl = ucOperatingChnl;
    prGlueInfo->prP2PInfo->ucInvitationType = ucInvitationType;

    // prepare event structure
    memset(&evt, 0, sizeof(evt));

    snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_INV_INDICATE");
    evt.data.length = strlen(aucBuffer);

    /* indicate in IWEVCUSTOM event */
    wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler,
            IWEVCUSTOM,
            &evt,
            aucBuffer);
    return;

#else
    P_MSG_P2P_CONNECTION_REQUEST_T prP2pConnReq = (P_MSG_P2P_CONNECTION_REQUEST_T)NULL;
    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;

    do {
        ASSERT_BREAK((prGlueInfo != NULL) && (prP2pDevDesc != NULL));


        // Not a real solution

        prP2pSpecificBssInfo = prGlueInfo->prAdapter->rWifiVar.prP2pSpecificBssInfo;
        prP2pConnSettings = prGlueInfo->prAdapter->rWifiVar.prP2PConnSettings;

        prP2pConnReq = (P_MSG_P2P_CONNECTION_REQUEST_T)cnmMemAlloc(prGlueInfo->prAdapter,
                                                                                RAM_TYPE_MSG,
                                                                                sizeof(MSG_P2P_CONNECTION_REQUEST_T));

        if (prP2pConnReq == NULL) {
            break;
        }


        kalMemZero(prP2pConnReq, sizeof(MSG_P2P_CONNECTION_REQUEST_T));

        prP2pConnReq->rMsgHdr.eMsgId = MID_MNY_P2P_CONNECTION_REQ;

        prP2pConnReq->eFormationPolicy = ENUM_P2P_FORMATION_POLICY_AUTO;

        COPY_MAC_ADDR(prP2pConnReq->aucDeviceID, prP2pDevDesc->aucDeviceAddr);

        prP2pConnReq->u2ConfigMethod = prP2pDevDesc->u2ConfigMethod;

        if (ucInvitationType == P2P_INVITATION_TYPE_INVITATION) {
            prP2pConnReq->fgIsPersistentGroup = FALSE;
            prP2pConnReq->fgIsTobeGO = FALSE;

        }

        else if (ucInvitationType == P2P_INVITATION_TYPE_REINVOKE) {
            DBGLOG(P2P, TRACE, ("Re-invoke Persistent Group\n"));
            prP2pConnReq->fgIsPersistentGroup = TRUE;
            prP2pConnReq->fgIsTobeGO = (prGlueInfo->prP2PInfo->ucRole == 2)?TRUE:FALSE;

        }


        p2pFsmRunEventDeviceDiscoveryAbort(prGlueInfo->prAdapter, NULL);

        if (ucOperatingChnl != 0) {
            prP2pSpecificBssInfo->ucPreferredChannel = ucOperatingChnl;
        }

        if ((ucSsidLen < 32) && (pucSsid != NULL)) {
            COPY_SSID(prP2pConnSettings->aucSSID,
                            prP2pConnSettings->ucSSIDLen,
                            pucSsid,
                            ucSsidLen);
        }

        mboxSendMsg(prGlueInfo->prAdapter,
                        MBOX_ID_0,
                        (P_MSG_HDR_T)prP2pConnReq,
                        MSG_SEND_METHOD_BUF);



    } while (FALSE);

    // frog add.
    // TODO: Invitation Indication

    return;
#endif

} /* kalP2PInvitationIndication */


/*----------------------------------------------------------------------------*/
/*!
* \brief Indicate an status to supplicant for device invitation status.
*
* \param[in] prGlueInfo Pointer of GLUE_INFO_T
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PInvitationStatus (
    IN P_GLUE_INFO_T prGlueInfo,
    IN UINT_32       u4InvStatus
    )
{
    union iwreq_data evt;
    UINT_8 aucBuffer[IW_CUSTOM_MAX];

    ASSERT(prGlueInfo);

    /* buffer peer information for later IOC_P2P_GET_STRUCT access */
    prGlueInfo->prP2PInfo->u4InvStatus = u4InvStatus;

    // prepare event structure
    memset(&evt, 0, sizeof(evt));

    snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_INV_STATUS");
    evt.data.length = strlen(aucBuffer);

    /* indicate in IWEVCUSTOM event */
    wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler,
            IWEVCUSTOM,
            &evt,
            aucBuffer);

    return;
} /* kalP2PInvitationStatus */

/*----------------------------------------------------------------------------*/
/*!
* \brief Indicate an event to supplicant for Service Discovery request from other device.
*
* \param[in] prGlueInfo Pointer of GLUE_INFO_T
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PIndicateSDRequest(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN PARAM_MAC_ADDRESS    rPeerAddr,
    IN UINT_8 ucSeqNum
    )
{
    union iwreq_data evt;
    UINT_8 aucBuffer[IW_CUSTOM_MAX];

    ASSERT(prGlueInfo);

    memset(&evt, 0, sizeof(evt));

    snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_SD_REQ %d", ucSeqNum);
    evt.data.length = strlen(aucBuffer);

    /* indicate IWEVP2PSDREQ event */
    wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler,
            IWEVCUSTOM,
            &evt,
            aucBuffer);

    return;
} /* end of kalP2PIndicateSDRequest() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Indicate an event to supplicant for Service Discovery response
*         from other device.
*
* \param[in] prGlueInfo Pointer of GLUE_INFO_T
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
void
kalP2PIndicateSDResponse(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN PARAM_MAC_ADDRESS    rPeerAddr,
    IN UINT_8 ucSeqNum
    )
{
    union iwreq_data evt;
    UINT_8 aucBuffer[IW_CUSTOM_MAX];

    ASSERT(prGlueInfo);

    memset(&evt, 0, sizeof(evt));

    snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_SD_RESP %d", ucSeqNum);
    evt.data.length = strlen(aucBuffer);

    /* indicate IWEVP2PSDREQ event */
    wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler,
            IWEVCUSTOM,
            &evt,
            aucBuffer);

    return;
} /* end of kalP2PIndicateSDResponse() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Indicate an event to supplicant for Service Discovery TX Done
*         from other device.
*
* \param[in] prGlueInfo Pointer of GLUE_INFO_T
* \param[in] ucSeqNum   Sequence number of the frame
* \param[in] ucStatus   Status code for TX
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PIndicateTXDone(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN UINT_8               ucSeqNum,
    IN UINT_8               ucStatus
    )
{
    union iwreq_data evt;
    UINT_8 aucBuffer[IW_CUSTOM_MAX];

    ASSERT(prGlueInfo);

    memset(&evt, 0, sizeof(evt));

    snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_SD_XMITTED: %d %d", ucSeqNum, ucStatus);
    evt.data.length = strlen(aucBuffer);

    /* indicate IWEVP2PSDREQ event */
    wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler,
            IWEVCUSTOM,
            &evt,
            aucBuffer);

    return;
} /* end of kalP2PIndicateSDResponse() */


struct net_device*
kalP2PGetDevHdlr(
    P_GLUE_INFO_T prGlueInfo
    )
{
    ASSERT(prGlueInfo);
    ASSERT(prGlueInfo->prP2PInfo);
    return prGlueInfo->prP2PInfo->prDevHandler;
}

#if CFG_SUPPORT_ANTI_PIRACY
/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in] prAdapter  Pointer of ADAPTER_T
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PIndicateSecCheckRsp (
    IN P_GLUE_INFO_T prGlueInfo,
    IN PUINT_8       pucRsp,
    IN UINT_16       u2RspLen
    )
{
    union iwreq_data evt;
    UINT_8 aucBuffer[IW_CUSTOM_MAX];

    ASSERT(prGlueInfo);

    memset(&evt, 0, sizeof(evt));
    snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_SEC_CHECK_RSP=");

    kalMemCopy(prGlueInfo->prP2PInfo->aucSecCheckRsp, pucRsp, u2RspLen);
    evt.data.length = strlen(aucBuffer);

#if DBG
    DBGLOG_MEM8(SEC, LOUD, prGlueInfo->prP2PInfo->aucSecCheckRsp, u2RspLen);
#endif
    /* indicate in IWECUSTOM event */
    wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler,
            IWEVCUSTOM,
            &evt,
            aucBuffer);
    return;
} /* p2pFsmRunEventRxDisassociation */
#endif


/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in] prAdapter  Pointer of ADAPTER_T
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
kalGetChnlList(
    IN P_GLUE_INFO_T           prGlueInfo,
    IN ENUM_BAND_T             eSpecificBand,
    IN UINT_8                  ucMaxChannelNum,
    IN PUINT_8                 pucNumOfChannel,
    IN P_RF_CHANNEL_INFO_T     paucChannelList
    )
{
    rlmDomainGetChnlList(prGlueInfo->prAdapter,
                              eSpecificBand,
                              ucMaxChannelNum,
                              pucNumOfChannel,
                              paucChannelList);
} /* kalGetChnlList */

//////////////////////////////////////ICS SUPPORT//////////////////////////////////////

VOID
kalP2PIndicateChannelReady (
    IN P_GLUE_INFO_T prGlueInfo,
    IN UINT_64 u8SeqNum,
    IN UINT_32 u4ChannelNum,
    IN ENUM_BAND_T eBand,
    IN ENUM_CHNL_EXT_T eSco,
    IN UINT_32 u4Duration
    )
{
    struct ieee80211_channel *prIEEE80211ChnlStruct = (struct ieee80211_channel *)NULL;
    RF_CHANNEL_INFO_T rChannelInfo;
    enum nl80211_channel_type eChnlType = NL80211_CHAN_NO_HT;

    do {
        if (prGlueInfo == NULL) {
            break;
        }


        kalMemZero(&rChannelInfo, sizeof(RF_CHANNEL_INFO_T));

        rChannelInfo.ucChannelNum = u4ChannelNum;
        rChannelInfo.eBand = eBand;

        prIEEE80211ChnlStruct = kalP2pFuncGetChannelEntry(prGlueInfo->prP2PInfo, &rChannelInfo);

        kalP2pFuncGetChannelType(eSco, &eChnlType);

        cfg80211_ready_on_channel(prGlueInfo->prP2PInfo->prDevHandler, //struct net_device * dev,
                        u8SeqNum, //u64 cookie,
                        prIEEE80211ChnlStruct, //struct ieee80211_channel * chan,
                        eChnlType, //enum nl80211_channel_type channel_type,
                        u4Duration, //unsigned int duration,
                        GFP_KERNEL); //gfp_t gfp    /* allocation flags */

    } while (FALSE);

} /* kalP2PIndicateChannelReady */

VOID
kalP2PIndicateChannelExpired (
    IN P_GLUE_INFO_T prGlueInfo,
    IN P_P2P_CHNL_REQ_INFO_T prChnlReqInfo
    )
{

    P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;
    struct ieee80211_channel *prIEEE80211ChnlStruct = (struct ieee80211_channel *)NULL;
    enum nl80211_channel_type eChnlType = NL80211_CHAN_NO_HT;
    RF_CHANNEL_INFO_T rRfChannelInfo;

    do {
        if ((prGlueInfo == NULL) || (prChnlReqInfo == NULL)) {

            ASSERT(FALSE);
            break;
        }

        prGlueP2pInfo = prGlueInfo->prP2PInfo;

        if (prGlueP2pInfo == NULL) {
            ASSERT(FALSE);
            break;
        }


        DBGLOG(P2P, TRACE, ("kalP2PIndicateChannelExpired\n"));

        rRfChannelInfo.eBand = prChnlReqInfo->eBand;
        rRfChannelInfo.ucChannelNum = prChnlReqInfo->ucReqChnlNum;

        prIEEE80211ChnlStruct = kalP2pFuncGetChannelEntry(prGlueP2pInfo, &rRfChannelInfo);


        kalP2pFuncGetChannelType(prChnlReqInfo->eChnlSco,
                                    &eChnlType);


        cfg80211_remain_on_channel_expired(prGlueP2pInfo->prDevHandler, //struct net_device * dev,
                        prChnlReqInfo->u8Cookie,
                        prIEEE80211ChnlStruct,
                        eChnlType,
                        GFP_KERNEL);

    } while (FALSE);

} /* kalP2PIndicateChannelExpired */

VOID
kalP2PIndicateScanDone (
    IN P_GLUE_INFO_T prGlueInfo,
    IN BOOLEAN fgIsAbort
    )
{
    P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;

    do {
        if (prGlueInfo == NULL) {

            ASSERT(FALSE);
            break;
        }

        prGlueP2pInfo = prGlueInfo->prP2PInfo;

        if (prGlueP2pInfo == NULL) {
            ASSERT(FALSE);
            break;
        }


        if (prGlueP2pInfo->prScanRequest) {
            cfg80211_scan_done(prGlueP2pInfo->prScanRequest,
                        fgIsAbort);

            prGlueP2pInfo->prScanRequest = NULL;
        }

    } while (FALSE);


} /* kalP2PIndicateScanDone */

VOID
kalP2PIndicateBssInfo (
    IN P_GLUE_INFO_T prGlueInfo,
    IN PUINT_8 pucFrameBuf,
    IN UINT_32 u4BufLen,
    IN P_RF_CHANNEL_INFO_T prChannelInfo,
    IN INT_32 i4SignalStrength
    )
{
     P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;
     struct ieee80211_channel *prChannelEntry = (struct ieee80211_channel *)NULL;
     struct ieee80211_mgmt *prBcnProbeRspFrame = (struct ieee80211_mgmt *)pucFrameBuf;

    do {
        if ((prGlueInfo == NULL) || (pucFrameBuf == NULL) || (prChannelInfo == NULL)) {
            ASSERT(FALSE);
            break;
        }

        prGlueP2pInfo = prGlueInfo->prP2PInfo;

        if (prGlueP2pInfo == NULL) {
            ASSERT(FALSE);
            break;
        }


        prChannelEntry = kalP2pFuncGetChannelEntry(prGlueP2pInfo, prChannelInfo);

        if (prChannelEntry == NULL) {
            DBGLOG(P2P, TRACE, ("Unknown channel info\n"));
            break;
        }


        //rChannelInfo.center_freq = nicChannelNum2Freq((UINT_32)prChannelInfo->ucChannelNum) / 1000;

        cfg80211_inform_bss_frame(prGlueP2pInfo->wdev.wiphy, //struct wiphy * wiphy,
                        prChannelEntry,
                        prBcnProbeRspFrame,
                        u4BufLen,
                        i4SignalStrength,
                        GFP_KERNEL);

    } while (FALSE);

    return;

} /* kalP2PIndicateBssInfo */

VOID
kalP2PIndicateMgmtTxStatus (
    IN P_GLUE_INFO_T prGlueInfo,
    IN UINT_64 u8Cookie,
    IN BOOLEAN fgIsAck,
    IN PUINT_8 pucFrameBuf,
    IN UINT_32 u4FrameLen
    )
{
    P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;

    do {
        if ((prGlueInfo == NULL) ||
                (pucFrameBuf == NULL) ||
                (u4FrameLen == 0)) {
            DBGLOG(P2P, TRACE, ("Unexpected pointer PARAM. 0x%lx, 0x%lx, %ld.", prGlueInfo, pucFrameBuf, u4FrameLen));
            ASSERT(FALSE);
            break;
        }

        prGlueP2pInfo = prGlueInfo->prP2PInfo;

        cfg80211_mgmt_tx_status(prGlueP2pInfo->prDevHandler, //struct net_device * dev,
                        u8Cookie,
                        pucFrameBuf,
                        u4FrameLen,
                        fgIsAck,
                        GFP_KERNEL);

    } while (FALSE);

} /* kalP2PIndicateMgmtTxStatus */

VOID
kalP2PIndicateRxMgmtFrame (
    IN P_GLUE_INFO_T prGlueInfo,
    IN P_SW_RFB_T prSwRfb
    )
{
#define DBG_P2P_MGMT_FRAME_INDICATION 0
    P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;
    INT_32 i4Freq = 0;
    UINT_8 ucChnlNum = 0;
#if DBG_P2P_MGMT_FRAME_INDICATION
    P_WLAN_MAC_HEADER_T prWlanHeader = (P_WLAN_MAC_HEADER_T)NULL;
#endif


    do {
        if ((prGlueInfo == NULL) || (prSwRfb == NULL)) {
            ASSERT(FALSE);
            break;
        }

        prGlueP2pInfo = prGlueInfo->prP2PInfo;

        ucChnlNum = prSwRfb->prHifRxHdr->ucHwChannelNum;

#if DBG_P2P_MGMT_FRAME_INDICATION

        prWlanHeader = (P_WLAN_MAC_HEADER_T)prSwRfb->pvHeader;

        switch (prWlanHeader->u2FrameCtrl) {
        case MAC_FRAME_PROBE_REQ:
            DBGLOG(P2P, TRACE, ("RX Probe Req at channel %d ", ucChnlNum));
            break;
        case MAC_FRAME_PROBE_RSP:
            DBGLOG(P2P, TRACE, ("RX Probe Rsp at channel %d ", ucChnlNum));
            break;
        case MAC_FRAME_ACTION:
            DBGLOG(P2P, TRACE, ("RX Action frame at channel %d ", ucChnlNum));
            break;
        default:
            DBGLOG(P2P, TRACE, ("RX Packet:%d at channel %d ", prWlanHeader->u2FrameCtrl, ucChnlNum));
            break;
        }

        DBGLOG(P2P, TRACE, ("from: "MACSTR"\n", MAC2STR(prWlanHeader->aucAddr2)));
#endif
        i4Freq = nicChannelNum2Freq(ucChnlNum) / 1000;

        cfg80211_rx_mgmt(prGlueP2pInfo->prDevHandler, //struct net_device * dev,
                            i4Freq,
                            prSwRfb->pvHeader,
                            prSwRfb->u2PacketLen,
                            GFP_KERNEL);

    } while (FALSE);

} /* kalP2PIndicateRxMgmtFrame */

VOID
kalP2PGCIndicateConnectionStatus (
    IN P_GLUE_INFO_T prGlueInfo,
    IN P_P2P_CONNECTION_REQ_INFO_T prP2pConnInfo,
    IN PUINT_8 pucRxIEBuf,
    IN UINT_16 u2RxIELen,
    IN UINT_16 u2StatusReason
    )
{
    P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;

    do {
        if (prGlueInfo == NULL) {
            ASSERT(FALSE);
            break;
        }


        prGlueP2pInfo = prGlueInfo->prP2PInfo;

        if (prP2pConnInfo) {
            cfg80211_connect_result(prGlueP2pInfo->prDevHandler, //struct net_device * dev,
                                    prP2pConnInfo->aucBssid,
                                    prP2pConnInfo->aucIEBuf,
                                    prP2pConnInfo->u4BufLength,
                                    pucRxIEBuf,
                                    u2RxIELen,
                                    u2StatusReason,
                                    GFP_KERNEL);  //gfp_t gfp    /* allocation flags */
            prP2pConnInfo->fgIsConnRequest = FALSE;
        }
        else {
            /* Disconnect, what if u2StatusReason == 0? */
            cfg80211_disconnected(prGlueP2pInfo->prDevHandler, //struct net_device * dev,
                                    u2StatusReason,
                                    pucRxIEBuf,
                                    u2RxIELen,
                                    GFP_KERNEL);
        }



    } while (FALSE);


} /* kalP2PGCIndicateConnectionStatus */


VOID
kalP2PGOStationUpdate (
    IN P_GLUE_INFO_T prGlueInfo,
    IN P_STA_RECORD_T prCliStaRec,
    IN BOOLEAN fgIsNew
    )
{
    P_GL_P2P_INFO_T prP2pGlueInfo = (P_GL_P2P_INFO_T)NULL;
    struct station_info rStationInfo;

    do {
        if ((prGlueInfo == NULL) || (prCliStaRec == NULL)) {
            break;
        }


        prP2pGlueInfo = prGlueInfo->prP2PInfo;

        if (fgIsNew) {
            rStationInfo.filled = 0;
            rStationInfo.generation = ++prP2pGlueInfo->i4Generation;

            rStationInfo.assoc_req_ies = prCliStaRec->pucAssocReqIe;
            rStationInfo.assoc_req_ies_len = prCliStaRec->u2AssocReqIeLen;
//          rStationInfo.filled |= STATION_INFO_ASSOC_REQ_IES;

            cfg80211_new_sta(prGlueInfo->prP2PInfo->prDevHandler, //struct net_device * dev,
                            prCliStaRec->aucMacAddr,
                            &rStationInfo,
                            GFP_KERNEL);
        }
        else {
            ++prP2pGlueInfo->i4Generation;

            cfg80211_del_sta(prGlueInfo->prP2PInfo->prDevHandler, //struct net_device * dev,
                            prCliStaRec->aucMacAddr,
                            GFP_KERNEL);
        }


    } while (FALSE);

    return;

} /* kalP2PGOStationUpdate */




BOOLEAN
kalP2pFuncGetChannelType(
    IN ENUM_CHNL_EXT_T rChnlSco,
    OUT enum nl80211_channel_type *channel_type
    )
{
    BOOLEAN fgIsValid = FALSE;

    do {
        if (channel_type) {

            switch (rChnlSco) {
            case CHNL_EXT_SCN:
                *channel_type = NL80211_CHAN_NO_HT;
                break;
            case CHNL_EXT_SCA:
                *channel_type = NL80211_CHAN_HT40MINUS;
                break;
            case CHNL_EXT_SCB:
                *channel_type = NL80211_CHAN_HT40PLUS;
                break;
            default:
                ASSERT(FALSE);
                *channel_type = NL80211_CHAN_NO_HT;
                break;
            }

        }

        fgIsValid = TRUE;
    } while (FALSE);

    return fgIsValid;
} /* kalP2pFuncGetChannelType */




struct ieee80211_channel *
kalP2pFuncGetChannelEntry (
    IN P_GL_P2P_INFO_T prP2pInfo,
    IN P_RF_CHANNEL_INFO_T prChannelInfo
    )
{
    struct ieee80211_channel *prTargetChannelEntry = (struct ieee80211_channel *)NULL;
    UINT_32 u4TblSize = 0, u4Idx = 0;

    do {
        if ((prP2pInfo == NULL) || (prChannelInfo == NULL)) {
            break;
        }


        switch (prChannelInfo->eBand) {
        case BAND_2G4:
            prTargetChannelEntry = prP2pInfo->wdev.wiphy->bands[IEEE80211_BAND_2GHZ]->channels;
            u4TblSize = prP2pInfo->wdev.wiphy->bands[IEEE80211_BAND_2GHZ]->n_channels;
            break;
        case BAND_5G:
            prTargetChannelEntry = prP2pInfo->wdev.wiphy->bands[IEEE80211_BAND_5GHZ]->channels;
            u4TblSize = prP2pInfo->wdev.wiphy->bands[IEEE80211_BAND_5GHZ]->n_channels;
            break;
        default:
            break;
        }


        if (prTargetChannelEntry == NULL) {
            break;
        }



        for (u4Idx = 0; u4Idx < u4TblSize; u4Idx++, prTargetChannelEntry++) {
            if (prTargetChannelEntry->hw_value == prChannelInfo->ucChannelNum) {
                break;
            }

        }


        if (u4Idx == u4TblSize) {
            prTargetChannelEntry = NULL;
            break;
        }


    } while (FALSE);

    return prTargetChannelEntry;
} /* kalP2pFuncGetChannelEntry */

