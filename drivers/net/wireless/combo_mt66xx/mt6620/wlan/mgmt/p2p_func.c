#include "precomp.h"

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wformat"
#endif


APPEND_VAR_ATTRI_ENTRY_T txAssocRspAttributesTable[] = {
     { (P2P_ATTRI_HDR_LEN + P2P_ATTRI_MAX_LEN_STATUS)   ,    NULL,                           p2pFuncAppendAttriStatusForAssocRsp        }  /* 0 */                 // Status
    ,{ (P2P_ATTRI_HDR_LEN + P2P_ATTRI_MAX_LEN_EXT_LISTEN_TIMING),    NULL,                           p2pFuncAppendAttriExtListenTiming   }  /* 8 */
};


APPEND_VAR_IE_ENTRY_T txProbeRspIETable[] = {
    { (ELEM_HDR_LEN + (RATE_NUM - ELEM_MAX_LEN_SUP_RATES)), NULL,                           bssGenerateExtSuppRate_IE   }   /* 50 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_ERP),                    NULL,                           rlmRspGenerateErpIE         }   /* 42 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_HT_CAP),                 NULL,                           rlmRspGenerateHtCapIE       }   /* 45 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_HT_OP),                  NULL,                           rlmRspGenerateHtOpIE        }   /* 61 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_RSN),                    NULL,                           rsnGenerateRSNIE            }   /* 48 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_OBSS_SCAN),              NULL,                           rlmRspGenerateObssScanIE    }   /* 74 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_EXT_CAP),                NULL,                           rlmRspGenerateExtCapIE      }   /* 127 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_WPA),                    NULL,                           rsnGenerateWpaNoneIE        }   /* 221 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_WMM_PARAM),              NULL,                           mqmGenerateWmmParamIE       }   /* 221 */
};

/*----------------------------------------------------------------------------*/
/*!
* @brief Function for requesting scan. There is an option to do ACTIVE or PASSIVE scan.
*
* @param eScanType - Specify the scan type of the scan request. It can be an ACTIVE/PASSIVE
*                                  Scan.
*              eChannelSet - Specify the prefered channel set.
*                                    A FULL scan would request a legacy full channel normal scan.(usually ACTIVE).
*                                    A P2P_SOCIAL scan would scan 1+6+11 channels.(usually ACTIVE)
*                                    A SPECIFIC scan would only 1/6/11 channels scan. (Passive Listen/Specific Search)
*               ucChannelNum - A specific channel number. (Only when channel is specified)
*               eBand - A specific band. (Only when channel is specified)
*
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
p2pFuncRequestScan (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_SCAN_REQ_INFO_T prScanReqInfo
    )
{

    P_MSG_SCN_SCAN_REQ prScanReq = (P_MSG_SCN_SCAN_REQ)NULL;

    DEBUGFUNC("p2pFuncRequestScan()");

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                        (prScanReqInfo != NULL));

        if (prScanReqInfo->eChannelSet == SCAN_CHANNEL_SPECIFIED) {
            ASSERT_BREAK(prScanReqInfo->ucNumChannelList > 0);
            DBGLOG(P2P, LOUD, ("P2P Scan Request Channel:%d\n", prScanReqInfo->arScanChannelList[0].ucChannelNum));
        }

        prScanReq = (P_MSG_SCN_SCAN_REQ)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_SCN_SCAN_REQ));
        if (!prScanReq) {
            ASSERT(0); // Can't trigger SCAN FSM
            break;
        }

        prScanReq->rMsgHdr.eMsgId    = MID_P2P_SCN_SCAN_REQ;
        prScanReq->ucSeqNum          = ++prScanReqInfo->ucSeqNumOfScnMsg;
        prScanReq->ucNetTypeIndex    = (UINT_8)NETWORK_TYPE_P2P_INDEX;
        prScanReq->eScanType        = prScanReqInfo->eScanType;
        prScanReq->eScanChannel     = prScanReqInfo->eChannelSet;
        prScanReq->u2IELen = 0;

        /* Copy IE for Probe Request. */
        kalMemCopy(prScanReq->aucIE, prScanReqInfo->aucIEBuf, prScanReqInfo->u4BufLength);
        prScanReq->u2IELen = (UINT_16)prScanReqInfo->u4BufLength;

        prScanReq->u2ChannelDwellTime = prScanReqInfo->u2PassiveDewellTime;

        switch (prScanReqInfo->eChannelSet) {
        case SCAN_CHANNEL_SPECIFIED:
            {
                UINT_32 u4Idx = 0;
                P_RF_CHANNEL_INFO_T prDomainInfo = (P_RF_CHANNEL_INFO_T)prScanReqInfo->arScanChannelList;

                if (prScanReqInfo->ucNumChannelList > MAXIMUM_OPERATION_CHANNEL_LIST) {
                    prScanReqInfo->ucNumChannelList = MAXIMUM_OPERATION_CHANNEL_LIST;
                }


                for (u4Idx = 0; u4Idx < prScanReqInfo->ucNumChannelList; u4Idx++) {
                    prScanReq->arChnlInfoList[u4Idx].ucChannelNum = prDomainInfo->ucChannelNum;
                    prScanReq->arChnlInfoList[u4Idx].eBand = prDomainInfo->eBand;
                    prDomainInfo++;
                }

                prScanReq->ucChannelListNum = prScanReqInfo->ucNumChannelList;
            }
        case SCAN_CHANNEL_FULL:
        case SCAN_CHANNEL_2G4:
        case SCAN_CHANNEL_P2P_SOCIAL:
            {
                UINT_8 aucP2pSsid[] = P2P_WILDCARD_SSID;

                COPY_SSID(prScanReq->aucSSID,
                                    prScanReq->ucSSIDLength,
                                    prScanReqInfo->rSsidStruct.aucSsid,
                                    prScanReqInfo->rSsidStruct.ucSsidLen);

                /* For compatible. */
                if (EQUAL_SSID(aucP2pSsid, P2P_WILDCARD_SSID_LEN, prScanReq->aucSSID, prScanReq->ucSSIDLength)) {
                    prScanReq->ucSSIDType = SCAN_REQ_SSID_P2P_WILDCARD;
                }
                else if (prScanReq->ucSSIDLength != 0) {
                    prScanReq->ucSSIDType = SCAN_REQ_SSID_SPECIFIED;
                }
            }
            break;
        default:
            /* Currently there is no other scan channel set. */
            ASSERT(FALSE);
            break;
        }

        mboxSendMsg(prAdapter,
                    MBOX_ID_0,
                    (P_MSG_HDR_T)prScanReq,
                    MSG_SEND_METHOD_BUF);

    } while (FALSE);

    return;
} /* p2pFuncRequestScan */

VOID
p2pFuncCancelScan (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_SCAN_REQ_INFO_T prScanInfo
    )
{
    P_MSG_SCN_SCAN_CANCEL prScanCancelMsg = (P_MSG_SCN_SCAN_CANCEL)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prScanInfo != NULL));

        if (!prScanInfo->fgIsScanRequest) {
            break;
        }


        if (prScanInfo->ucSeqNumOfScnMsg) {
            /* There is a channel privilege on hand. */
            DBGLOG(P2P, TRACE, ("P2P Cancel Scan\n"));

            prScanCancelMsg = (P_MSG_SCN_SCAN_CANCEL)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_SCN_SCAN_CANCEL));
            if (!prScanCancelMsg) {
                /* Buffer not enough, can not cancel scan request. */
                DBGLOG(P2P, TRACE, ("Buffer not enough, can not cancel scan.\n"));
                ASSERT(FALSE);
                break;
            }

            prScanCancelMsg->rMsgHdr.eMsgId = MID_P2P_SCN_SCAN_CANCEL;
            prScanCancelMsg->ucNetTypeIndex = NETWORK_TYPE_P2P_INDEX;
            prScanCancelMsg->ucSeqNum = prScanInfo->ucSeqNumOfScnMsg++;
            prScanCancelMsg->fgIsChannelExt = FALSE;
            prScanInfo->fgIsScanRequest = FALSE;

            mboxSendMsg(prAdapter,
                                MBOX_ID_0,
                                (P_MSG_HDR_T)prScanCancelMsg,
                                MSG_SEND_METHOD_BUF);


        }


    } while (FALSE);

    return;
} /* p2pFuncCancelScan */


VOID
p2pFuncSwitchOPMode (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN ENUM_OP_MODE_T eOpMode,
    IN BOOLEAN fgSyncToFW
    )
{
    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                                            (prP2pBssInfo != NULL) &&
                                            (eOpMode < OP_MODE_NUM));

        if (prP2pBssInfo->eCurrentOPMode != eOpMode) {
            DBGLOG(P2P, TRACE, ("p2pFuncSwitchOPMode: Switch to from %d, to %d.\n", prP2pBssInfo->eCurrentOPMode, eOpMode));

            switch (prP2pBssInfo->eCurrentOPMode) {
            case OP_MODE_ACCESS_POINT:
                p2pFuncDissolve(prAdapter, prP2pBssInfo, TRUE, REASON_CODE_DEAUTH_LEAVING_BSS);

                p2pFsmRunEventStopAP(prAdapter, NULL);
                break;
            default:
                break;
            }


            prP2pBssInfo->eIntendOPMode = eOpMode;
            prP2pBssInfo->eCurrentOPMode = eOpMode;
            switch (eOpMode) {
            case OP_MODE_INFRASTRUCTURE:
                DBGLOG(P2P, TRACE, ("p2pFuncSwitchOPMode: Switch to Client.\n"));
            case OP_MODE_ACCESS_POINT:
//                if (!IS_BSS_ACTIVE(prP2pBssInfo)) {
//                    SET_NET_ACTIVE(prAdapter, NETWORK_TYPE_P2P_INDEX);
//                    nicActivateNetwork(prAdapter, NETWORK_TYPE_P2P_INDEX);
//                }

                /* Change interface address. */
                if (eOpMode == OP_MODE_ACCESS_POINT) {
                    DBGLOG(P2P, TRACE, ("p2pFuncSwitchOPMode: Switch to AP.\n"));
                    prP2pBssInfo->ucSSIDLen = 0;
                }

                COPY_MAC_ADDR(prP2pBssInfo->aucOwnMacAddr, prAdapter->rWifiVar.aucInterfaceAddress);
                COPY_MAC_ADDR(prP2pBssInfo->aucBSSID, prAdapter->rWifiVar.aucInterfaceAddress);


                break;
            case OP_MODE_P2P_DEVICE:
                {
                    /* Change device address. */
                    DBGLOG(P2P, TRACE, ("p2pFuncSwitchOPMode: Switch back to P2P Device.\n"));

//                    if (!IS_BSS_ACTIVE(prP2pBssInfo)) {
//                        SET_NET_ACTIVE(prAdapter, NETWORK_TYPE_P2P_INDEX);
//                        nicActivateNetwork(prAdapter, NETWORK_TYPE_P2P_INDEX);
//                    }

                    p2pChangeMediaState(prAdapter, PARAM_MEDIA_STATE_DISCONNECTED);

                    COPY_MAC_ADDR(prP2pBssInfo->aucOwnMacAddr, prAdapter->rWifiVar.aucDeviceAddress);
                    COPY_MAC_ADDR(prP2pBssInfo->aucBSSID, prAdapter->rWifiVar.aucDeviceAddress);


                }
                break;
            default:
//                if (IS_BSS_ACTIVE(prP2pBssInfo)) {
//                    UNSET_NET_ACTIVE(prAdapter, NETWORK_TYPE_P2P_INDEX);
                    
//                    nicDeactivateNetwork(prAdapter, NETWORK_TYPE_P2P_INDEX);
//                }
                ASSERT(FALSE);
                break;
            }

            if (1) {
                P2P_DISCONNECT_INFO rP2PDisInfo;

                rP2PDisInfo.ucRole = 2;
                wlanSendSetQueryCmd(prAdapter,
                                CMD_ID_P2P_ABORT,
                                TRUE,
                                FALSE,
                                FALSE,
                                NULL,
                                NULL,
                                sizeof(P2P_DISCONNECT_INFO),
                                (PUINT_8)&rP2PDisInfo,
                                NULL,
                                0);
            }


            DBGLOG(P2P, TRACE, ("The device address is changed to " MACSTR " \n", MAC2STR(prP2pBssInfo->aucOwnMacAddr)));
            DBGLOG(P2P, TRACE, ("The BSSID is changed to " MACSTR " \n", MAC2STR(prP2pBssInfo->aucBSSID)));

            /* Update BSS INFO to FW. */
            if ((fgSyncToFW) && (eOpMode != OP_MODE_ACCESS_POINT)) {
                nicUpdateBss(prAdapter, NETWORK_TYPE_P2P_INDEX);
            }
        }

    } while (FALSE);

    return;
} /* p2pFuncSwitchOPMode */



/*----------------------------------------------------------------------------*/
/*!
* @brief This function will start a P2P Group Owner and send Beacon Frames.
*
* @param (none)
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
p2pFuncStartGO (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN PUINT_8 pucSsidBuf,
    IN UINT_8 ucSsidLen,
    IN UINT_8 ucChannelNum,
    IN ENUM_BAND_T eBand,
    IN ENUM_CHNL_EXT_T eSco,
    IN BOOLEAN fgIsPureAP
    )
{
    do {
        ASSERT_BREAK((prAdapter != NULL) && (prBssInfo != NULL));

        //ASSERT(prBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT);

        DBGLOG(P2P, TRACE, ("p2pFuncStartGO:\n"));

        /* AP mode started. */
        p2pFuncSwitchOPMode(prAdapter, prBssInfo, prBssInfo->eIntendOPMode, FALSE);

        prBssInfo->eIntendOPMode = OP_MODE_NUM;

        //4 <1.1> Assign SSID
        COPY_SSID(prBssInfo->aucSSID,
                        prBssInfo->ucSSIDLen,
                        pucSsidBuf,
                        ucSsidLen);

        DBGLOG(P2P, TRACE, ("GO SSID:%s \n", prBssInfo->aucSSID));

        //4 <1.2> Clear current AP's STA_RECORD_T and current AID
        prBssInfo->prStaRecOfAP = (P_STA_RECORD_T)NULL;
        prBssInfo->u2AssocId = 0;


        //4 <1.3> Setup Channel, Band and Phy Attributes
        prBssInfo->ucPrimaryChannel = ucChannelNum;
        prBssInfo->eBand = eBand;
        prBssInfo->eBssSCO = eSco;

        DBGLOG(P2P, TRACE, ("GO Channel:%d \n", ucChannelNum));


        if (prBssInfo->eBand == BAND_5G) {
            prBssInfo->ucPhyTypeSet = (prAdapter->rWifiVar.ucAvailablePhyTypeSet & PHY_TYPE_SET_802_11AN); /* Depend on eBand */
            prBssInfo->ucConfigAdHocAPMode = AP_MODE_11A; /* Depend on eCurrentOPMode and ucPhyTypeSet */
        }
        else if (fgIsPureAP) {
            prBssInfo->ucPhyTypeSet = (prAdapter->rWifiVar.ucAvailablePhyTypeSet & PHY_TYPE_SET_802_11BGN); /* Depend on eBand */
            prBssInfo->ucConfigAdHocAPMode = AP_MODE_MIXED_11BG; /* Depend on eCurrentOPMode and ucPhyTypeSet */
        }
        else {
            prBssInfo->ucPhyTypeSet = (prAdapter->rWifiVar.ucAvailablePhyTypeSet & PHY_TYPE_SET_802_11GN); /* Depend on eBand */
            prBssInfo->ucConfigAdHocAPMode = AP_MODE_11G_P2P; /* Depend on eCurrentOPMode and ucPhyTypeSet */
        }


        prBssInfo->ucNonHTBasicPhyType = (UINT_8)
                    rNonHTApModeAttributes[prBssInfo->ucConfigAdHocAPMode].ePhyTypeIndex;
        prBssInfo->u2BSSBasicRateSet =
                    rNonHTApModeAttributes[prBssInfo->ucConfigAdHocAPMode].u2BSSBasicRateSet;
        prBssInfo->u2OperationalRateSet =
                    rNonHTPhyAttributes[prBssInfo->ucNonHTBasicPhyType].u2SupportedRateSet;

        if (prBssInfo->ucAllSupportedRatesLen == 0) {
            rateGetDataRatesFromRateSet(prBssInfo->u2OperationalRateSet,
                                        prBssInfo->u2BSSBasicRateSet,
                                        prBssInfo->aucAllSupportedRates,
                                        &prBssInfo->ucAllSupportedRatesLen);
        }

        //4 <1.5> Setup MIB for current BSS
        prBssInfo->u2ATIMWindow = 0;
        prBssInfo->ucBeaconTimeoutCount = 0;

        //3 <2> Update BSS_INFO_T common part
#if CFG_SUPPORT_AAA
        if (!fgIsPureAP) {
            prBssInfo->fgIsProtection = TRUE; /* Always enable protection at P2P GO */
            kalP2PSetCipher(prAdapter->prGlueInfo, IW_AUTH_CIPHER_CCMP);
        }
        else {
            if (kalP2PGetCipher(prAdapter->prGlueInfo))
                prBssInfo->fgIsProtection = TRUE;
        }

        // 20120106 frog: I want separate OP_Mode & Beacon TX Function.
        //p2pFuncSwitchOPMode(prAdapter, prBssInfo, OP_MODE_ACCESS_POINT, FALSE);

        bssInitForAP(prAdapter, prBssInfo, FALSE);

        nicQmUpdateWmmParms(prAdapter, NETWORK_TYPE_P2P_INDEX);
#endif /* CFG_SUPPORT_AAA */


        //3 <3> Set MAC HW
        //4 <3.1> Setup channel and bandwidth
        rlmBssInitForAPandIbss(prAdapter, prBssInfo);

        //4 <3.2> Reset HW TSF Update Mode and Beacon Mode
        nicUpdateBss(prAdapter, NETWORK_TYPE_P2P_INDEX);

        //4 <3.3> Update Beacon again for network phy type confirmed.
        bssUpdateBeaconContent(prAdapter, NETWORK_TYPE_P2P_INDEX);

        //4 <3.4> Setup BSSID
        nicPmIndicateBssCreated(prAdapter, NETWORK_TYPE_P2P_INDEX);

    } while (FALSE);

    return;
} /* p2pFuncStartGO() */




/*----------------------------------------------------------------------------*/
/*!
* \brief    This function is to inform CNM that channel privilege
*           has been released
*
* \param[in] prAdapter  Pointer of ADAPTER_T
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
p2pFuncReleaseCh (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_CHNL_REQ_INFO_T prChnlReqInfo
    )
{
    P_MSG_CH_ABORT_T prMsgChRelease = (P_MSG_CH_ABORT_T)NULL;

    DEBUGFUNC("p2pFuncReleaseCh()");

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prChnlReqInfo != NULL));

        if (!prChnlReqInfo->fgIsChannelRequested) {
            break;
        }
        else {
            DBGLOG(P2P, TRACE, ("P2P Release Channel\n"));
            prChnlReqInfo->fgIsChannelRequested = FALSE;
        }

         /* 1. return channel privilege to CNM immediately */
        prMsgChRelease = (P_MSG_CH_ABORT_T)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_CH_ABORT_T));
        if (!prMsgChRelease) {
            ASSERT(0); // Can't release Channel to CNM
            break;
        }

        prMsgChRelease->rMsgHdr.eMsgId  = MID_MNY_CNM_CH_ABORT;
        prMsgChRelease->ucNetTypeIndex  = NETWORK_TYPE_P2P_INDEX;
        prMsgChRelease->ucTokenID       = prChnlReqInfo->ucSeqNumOfChReq++;

        mboxSendMsg(prAdapter,
                    MBOX_ID_0,
                    (P_MSG_HDR_T) prMsgChRelease,
                    MSG_SEND_METHOD_BUF);

    } while (FALSE);

    return;
} /* p2pFuncReleaseCh */


/*----------------------------------------------------------------------------*/
/*!
* @brief Process of CHANNEL_REQ_JOIN Initial. Enter CHANNEL_REQ_JOIN State.
*
* @param (none)
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
p2pFuncAcquireCh (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_CHNL_REQ_INFO_T prChnlReqInfo
    )
{
    P_MSG_CH_REQ_T prMsgChReq = (P_MSG_CH_REQ_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prChnlReqInfo != NULL));

        p2pFuncReleaseCh(prAdapter, prChnlReqInfo);

        /* send message to CNM for acquiring channel */
        prMsgChReq = (P_MSG_CH_REQ_T)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_CH_REQ_T));

        if (!prMsgChReq) {
            ASSERT(0); // Can't indicate CNM for channel acquiring
            break;
        }

        prMsgChReq->rMsgHdr.eMsgId      = MID_MNY_CNM_CH_REQ;
        prMsgChReq->ucNetTypeIndex      = NETWORK_TYPE_P2P_INDEX;
        prMsgChReq->ucTokenID           = ++prChnlReqInfo->ucSeqNumOfChReq;
        prMsgChReq->eReqType            = CH_REQ_TYPE_JOIN;
        prMsgChReq->u4MaxInterval       = prChnlReqInfo->u4MaxInterval;

        prMsgChReq->ucPrimaryChannel    = prChnlReqInfo->ucReqChnlNum;
        prMsgChReq->eRfSco              = prChnlReqInfo->eChnlSco;
        prMsgChReq->eRfBand             = prChnlReqInfo->eBand;

//		dump_stack();
		
        kalMemZero(prMsgChReq->aucBSSID, MAC_ADDR_LEN);

        /* Channel request join BSSID. */

        mboxSendMsg(prAdapter,
                    MBOX_ID_0,
                    (P_MSG_HDR_T) prMsgChReq,
                    MSG_SEND_METHOD_BUF);

        prChnlReqInfo->fgIsChannelRequested = TRUE;

    } while (FALSE);

    return;
} /* p2pFuncAcquireCh */

#if 0
WLAN_STATUS
p2pFuncBeaconUpdate(
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucBcnHdr,
    IN UINT_32 u4HdrLen,
    IN PUINT_8 pucBcnBody,
    IN UINT_32 u4BodyLen,
    IN UINT_32 u4DtimPeriod,
    IN UINT_32 u4BcnInterval)
{
    WLAN_STATUS rResultStatus = WLAN_STATUS_INVALID_DATA;
    P_WLAN_BEACON_FRAME_T prBcnFrame = (P_WLAN_BEACON_FRAME_T)NULL;
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;
    P_MSDU_INFO_T prBcnMsduInfo = (P_MSDU_INFO_T)NULL;
    PUINT_8 pucTIMBody = (PUINT_8)NULL;
    UINT_16 u2FrameLength = 0, UINT_16 u2OldBodyLen = 0;
    UINT_8 aucIEBuf[MAX_IE_LENGTH];

    do {
        ASSERT_BREAK(prAdapter != NULL);

        prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);
        prBcnMsduInfo = prP2pBssInfo->prBeacon

        ASSERT_BREAK(prBcnMsduInfo != NULL);

        /* TODO: Find TIM IE pointer. */
        prBcnFrame = prBcnMsduInfo->prPacket;

        ASSERT_BREAK(prBcnFrame != NULL);

        do {
            /* Ori header. */
            UINT_16 u2IELength = 0, u2Offset = 0;
            PUINT_8 pucIEBuf = prBcnFrame->aucInfoElem;

            u2IELength = prBcnMsduInfo->u2FrameLength - prBcnMsduInfo->ucMacHeaderLength;

            IE_FOR_EACH(pucIEBuf, u2IELength, u2Offset) {
                if ((IE_ID(pucIEBuf) == ELEM_ID_TIM) ||
                        ((IE_ID(pucIEBuf) > ELEM_ID_IBSS_PARAM_SET))  {
                    pucTIMBody = pucIEBuf;
                    break
                }
                u2FrameLength += IE_SIZE(pucIEBuf);
            }

            if (pucTIMBody == NULL) {
                pucTIMBody = pucIEBuf;
            }

             /* Body not change. */
            u2OldBodyLen = (UINT_16)((UINT_32)pucTIMBody - (UINT_32)prBcnFrame->aucInfoElem);

            // Move body.
            kalMemCmp(aucIEBuf, pucTIMBody, u2OldBodyLen);
        } while (FALSE);


        if (pucBcnHdr) {
            kalMemCopy(prBcnMsduInfo->prPacket, pucBcnHdr, u4HdrLen);

            pucTIMBody = (PUINT_8)((UINT_32)prBcnMsduInfo->prPacket + u4HdrLen);

            prBcnMsduInfo->ucMacHeaderLength = (WLAN_MAC_MGMT_HEADER_LEN +
                (TIMESTAMP_FIELD_LEN + BEACON_INTERVAL_FIELD_LEN + CAP_INFO_FIELD_LEN));

            u2FrameLength = u4HdrLen; /* Header + Partial Body. */

        }
        else {
            /* Header not change. */
            u2FrameLength += prBcnMsduInfo->ucMacHeaderLength;
        }


        if (pucBcnBody) {
            kalMemCopy(pucTIMBody, pucBcnBody, u4BodyLen);
            u2FrameLength += (UINT_16)u4BodyLen;
        }
        else {
            kalMemCopy(pucTIMBody, aucIEBuf, u2OldBodyLen);
            u2FrameLength += u2OldBodyLen;
        }

        /* Frame Length */
        prBcnMsduInfo->u2FrameLength = u2FrameLength;

        prBcnMsduInfo->fgIs802_11 = TRUE;
        prBcnMsduInfo->ucNetworkType = NETWORK_TYPE_P2P_INDEX;

        prP2pBssInfo->u2BeaconInterval = (UINT_16)u4BcnInterval;
        prP2pBssInfo->ucDTIMPeriod = (UINT_8)u4DtimPeriod;
        prP2pBssInfo->u2CapInfo = prBcnFrame->u2CapInfo;
        prBcnMsduInfo->ucPacketType = 3;

        rResultStatus = nicUpdateBeaconIETemplate(prAdapter,
                                        IE_UPD_METHOD_UPDATE_ALL,
                                        NETWORK_TYPE_P2P_INDEX,
                                        prP2pBssInfo->u2CapInfo,
                                        (PUINT_8)prBcnFrame->aucInfoElem,
                                        prBcnMsduInfo->u2FrameLength - OFFSET_OF(WLAN_BEACON_FRAME_T, aucInfoElem));

        if (prP2pBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT) {
            /* AP is created, Beacon Update. */
            nicPmIndicateBssAbort(prAdapter, NETWORK_TYPE_P2P_INDEX);

            nicPmIndicateBssCreated(prAdapter, NETWORK_TYPE_P2P_INDEX);
        }

    } while (FALSE);

    return rResultStatus;
} /* p2pFuncBeaconUpdate */

#else
WLAN_STATUS
p2pFuncBeaconUpdate (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN P_P2P_BEACON_UPDATE_INFO_T prBcnUpdateInfo,
    IN PUINT_8 pucNewBcnHdr,
    IN UINT_32 u4NewHdrLen,
    IN PUINT_8 pucNewBcnBody,
    IN UINT_32 u4NewBodyLen
    )
{
    WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;
    P_WLAN_BEACON_FRAME_T prBcnFrame = (P_WLAN_BEACON_FRAME_T)NULL;
    P_MSDU_INFO_T prBcnMsduInfo = (P_MSDU_INFO_T)NULL;
    PUINT_8 pucIEBuf = (PUINT_8)NULL;
    UINT_8 aucIEBuf[MAX_IE_LENGTH];

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                (prP2pBssInfo != NULL) &&
                (prBcnUpdateInfo != NULL));

        prBcnMsduInfo = prP2pBssInfo->prBeacon;

#if DBG
        if (prBcnUpdateInfo->pucBcnHdr != NULL) {
            ASSERT((UINT_32)prBcnUpdateInfo->pucBcnHdr == ((UINT_32)prBcnMsduInfo->prPacket + MAC_TX_RESERVED_FIELD));
        }

        if (prBcnUpdateInfo->pucBcnBody != NULL) {
            ASSERT((UINT_32)prBcnUpdateInfo->pucBcnBody == ((UINT_32)prBcnUpdateInfo->pucBcnHdr + (UINT_32)prBcnUpdateInfo->u4BcnHdrLen));
        }
#endif
        prBcnFrame = (P_WLAN_BEACON_FRAME_T)((UINT_32)prBcnMsduInfo->prPacket + MAC_TX_RESERVED_FIELD);

        if (!pucNewBcnBody) {
            /* Old body. */
            pucNewBcnBody = prBcnUpdateInfo->pucBcnBody;
            ASSERT(u4NewBodyLen == 0);
            u4NewBodyLen = prBcnUpdateInfo->u4BcnBodyLen;
        }
        else {
            prBcnUpdateInfo->u4BcnBodyLen = u4NewBodyLen;
        }

        /* Temp buffer body part. */
        kalMemCopy(aucIEBuf, pucNewBcnBody, u4NewBodyLen);

        if (pucNewBcnHdr) {
            kalMemCopy(prBcnFrame, pucNewBcnHdr, u4NewHdrLen);
            prBcnUpdateInfo->pucBcnHdr = (PUINT_8)prBcnFrame;
            prBcnUpdateInfo->u4BcnHdrLen = u4NewHdrLen;
        }

        pucIEBuf = (PUINT_8)((UINT_32)prBcnUpdateInfo->pucBcnHdr + (UINT_32)prBcnUpdateInfo->u4BcnHdrLen);
        kalMemCopy(pucIEBuf, aucIEBuf, u4NewBodyLen);
        prBcnUpdateInfo->pucBcnBody = pucIEBuf;

        /* Frame Length */
        prBcnMsduInfo->u2FrameLength = (UINT_16)(prBcnUpdateInfo->u4BcnHdrLen + prBcnUpdateInfo->u4BcnBodyLen);

        prBcnMsduInfo->ucPacketType = 3;
        prBcnMsduInfo->fgIs802_11 = TRUE;
        prBcnMsduInfo->ucNetworkType = NETWORK_TYPE_P2P_INDEX;


        /* Update BSS INFO related information. */
        COPY_MAC_ADDR(prP2pBssInfo->aucOwnMacAddr, prBcnFrame->aucSrcAddr);
        COPY_MAC_ADDR(prP2pBssInfo->aucBSSID, prBcnFrame->aucBSSID);
        prP2pBssInfo->u2CapInfo = prBcnFrame->u2CapInfo;

        p2pFuncParseBeaconContent(prAdapter,
                            prP2pBssInfo,
                            (PUINT_8)prBcnFrame->aucInfoElem,
                            (prBcnMsduInfo->u2FrameLength - OFFSET_OF(WLAN_BEACON_FRAME_T, aucInfoElem)));

#if 1
        //bssUpdateBeaconContent(prAdapter, NETWORK_TYPE_P2P_INDEX);
#else
        nicUpdateBeaconIETemplate(prAdapter,
                                        IE_UPD_METHOD_UPDATE_ALL,
                                        NETWORK_TYPE_P2P_INDEX,
                                        prBcnFrame->u2CapInfo,
                                        (PUINT_8)prBcnFrame->aucInfoElem,
                                        (prBcnMsduInfo->u2FrameLength - OFFSET_OF(WLAN_BEACON_FRAME_T, aucInfoElem)));
#endif
    } while (FALSE);

    return rWlanStatus;
} /* p2pFuncBeaconUpdate */

#endif

// TODO: We do not apply IE in deauth frame set from upper layer now.
WLAN_STATUS
p2pFuncDeauth (
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucPeerMacAddr,
    IN UINT_16 u2ReasonCode,
    IN PUINT_8 pucIEBuf,
    IN UINT_16 u2IELen,
    IN BOOLEAN fgSendDeauth
    )
{
    WLAN_STATUS rWlanStatus = WLAN_STATUS_FAILURE;
    P_STA_RECORD_T prCliStaRec = (P_STA_RECORD_T)NULL;
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;
    BOOLEAN fgIsStaFound = FALSE;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (pucPeerMacAddr != NULL));

        prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);

        prCliStaRec = cnmGetStaRecByAddress(prAdapter,
                                                    NETWORK_TYPE_P2P_INDEX,
                                                    pucPeerMacAddr);

        switch (prP2pBssInfo->eCurrentOPMode) {
        case OP_MODE_ACCESS_POINT:
            {
                P_LINK_T prStaRecOfClientList = (P_LINK_T)NULL;
                P_LINK_ENTRY_T prLinkEntry = (P_LINK_ENTRY_T)NULL;

                prStaRecOfClientList = &(prP2pBssInfo->rStaRecOfClientList);

                LINK_FOR_EACH(prLinkEntry, prStaRecOfClientList) {
                    if ((UINT_32)prCliStaRec == (UINT_32)prLinkEntry) {
                        LINK_REMOVE_KNOWN_ENTRY(prStaRecOfClientList, &prCliStaRec->rLinkEntry);
                        fgIsStaFound = TRUE;
                        break;
                    }
                }

            }
            break;
        case OP_MODE_INFRASTRUCTURE:
            ASSERT(prCliStaRec == prP2pBssInfo->prStaRecOfAP);
            if (prCliStaRec != prP2pBssInfo->prStaRecOfAP) {
                break;
            }
            prP2pBssInfo->prStaRecOfAP = NULL;
            fgIsStaFound = TRUE;
            break;
        default:
            break;
        }

        if (fgIsStaFound) {
            p2pFuncDisconnect(prAdapter, prCliStaRec, fgSendDeauth, u2ReasonCode);
        }

        rWlanStatus = WLAN_STATUS_SUCCESS;
    } while (FALSE);

    return rWlanStatus;
} /* p2pFuncDeauth */

// TODO: We do not apply IE in disassoc frame set from upper layer now.
WLAN_STATUS
p2pFuncDisassoc (
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucPeerMacAddr,
    IN UINT_16 u2ReasonCode,
    IN PUINT_8 pucIEBuf,
    IN UINT_16 u2IELen,
    IN BOOLEAN fgSendDisassoc
    )
{
    WLAN_STATUS rWlanStatus = WLAN_STATUS_FAILURE;
    P_STA_RECORD_T prCliStaRec = (P_STA_RECORD_T)NULL;
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;
    BOOLEAN fgIsStaFound = FALSE;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (pucPeerMacAddr != NULL));

        prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);

        prCliStaRec = cnmGetStaRecByAddress(prAdapter,
                                                    NETWORK_TYPE_P2P_INDEX,
                                                    pucPeerMacAddr);

        switch (prP2pBssInfo->eCurrentOPMode) {
        case OP_MODE_ACCESS_POINT:
            {
                P_LINK_T prStaRecOfClientList = (P_LINK_T)NULL;
                P_LINK_ENTRY_T prLinkEntry = (P_LINK_ENTRY_T)NULL;

                prStaRecOfClientList = &(prP2pBssInfo->rStaRecOfClientList);

                LINK_FOR_EACH(prLinkEntry, prStaRecOfClientList) {
                    if ((UINT_32)prCliStaRec == (UINT_32)prLinkEntry) {
                        LINK_REMOVE_KNOWN_ENTRY(prStaRecOfClientList, &prCliStaRec->rLinkEntry);
                        fgIsStaFound = TRUE;
                        //p2pFuncDisconnect(prAdapter, prCliStaRec, fgSendDisassoc, u2ReasonCode);
                        break;
                    }
                }

            }
            break;
        case OP_MODE_INFRASTRUCTURE:
            ASSERT(prCliStaRec == prP2pBssInfo->prStaRecOfAP);
            if (prCliStaRec != prP2pBssInfo->prStaRecOfAP) {
                break;
            }

            //p2pFuncDisconnect(prAdapter, prCliStaRec, fgSendDisassoc, u2ReasonCode);
            prP2pBssInfo->prStaRecOfAP = NULL;
            fgIsStaFound = TRUE;
            break;
        default:
            break;
        }

        if (fgIsStaFound) {
                        
            p2pFuncDisconnect(prAdapter, prCliStaRec, fgSendDisassoc, u2ReasonCode);
            //20120830 moved into p2pFuncDisconnect().
            //cnmStaRecFree(prAdapter, prCliStaRec, TRUE);

        }

        rWlanStatus = WLAN_STATUS_SUCCESS;
    } while (FALSE);

    return rWlanStatus;
} /* p2pFuncDisassoc */

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to dissolve from group or one group. (Would not change P2P FSM.)
*              1. GC: Disconnect from AP. (Send Deauth)
*              2. GO: Disconnect all STA
*
* @param[in] prAdapter   Pointer to the adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
p2pFuncDissolve (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN BOOLEAN fgSendDeauth,
    IN UINT_16 u2ReasonCode
    )
{
    DEBUGFUNC("p2pFuncDissolve()");

    do {

        ASSERT_BREAK((prAdapter != NULL) && (prP2pBssInfo != NULL));

        switch (prP2pBssInfo->eCurrentOPMode) {
        case OP_MODE_INFRASTRUCTURE:
            /* Reset station record status. */
            if (prP2pBssInfo->prStaRecOfAP) {
                kalP2PGCIndicateConnectionStatus(prAdapter->prGlueInfo, 
                                NULL,
                                NULL,
                                0,
                                REASON_CODE_DEAUTH_LEAVING_BSS);
                
                // 2012/02/14 frog: After formation before join group, prStaRecOfAP is NULL.
                p2pFuncDisconnect(prAdapter,
                            prP2pBssInfo->prStaRecOfAP,
                            fgSendDeauth,
                            u2ReasonCode);
            }

            /* Fix possible KE when RX Beacon & call nicPmIndicateBssConnected(). hit prStaRecOfAP == NULL. */
            p2pChangeMediaState(prAdapter, PARAM_MEDIA_STATE_DISCONNECTED);

            prP2pBssInfo->prStaRecOfAP = NULL;

            break;
        case OP_MODE_ACCESS_POINT:
            /* Under AP mode, we would net send deauthentication frame to each STA.
              * We only stop the Beacon & let all stations timeout.
              */
            {
                P_LINK_T prStaRecOfClientList = (P_LINK_T)NULL;

                /* Send deauth. */
                authSendDeauthFrame(prAdapter,
                                NULL,
                                (P_SW_RFB_T)NULL,
                                u2ReasonCode,
                                (PFN_TX_DONE_HANDLER)NULL);

                prStaRecOfClientList = &prP2pBssInfo->rStaRecOfClientList;

                while (!LINK_IS_EMPTY(prStaRecOfClientList)) {
                    P_STA_RECORD_T prCurrStaRec;

                    LINK_REMOVE_HEAD(prStaRecOfClientList, prCurrStaRec, P_STA_RECORD_T);

                    /* Indicate to Host. */
                    //kalP2PGOStationUpdate(prAdapter->prGlueInfo, prCurrStaRec, FALSE);

                    p2pFuncDisconnect(prAdapter, prCurrStaRec, TRUE, u2ReasonCode);

                }

            }

            break;
        default:
            return;  // 20110420 -- alreay in Device Mode.
        }

        /* Make the deauth frame send to FW ASAP. */
        wlanAcquirePowerControl(prAdapter);
        wlanProcessCommandQueue(prAdapter, &prAdapter->prGlueInfo->rCmdQueue);
        wlanReleasePowerControl(prAdapter);

        kalMdelay(100);

        /* Change Connection Status. */
        p2pChangeMediaState(prAdapter, PARAM_MEDIA_STATE_DISCONNECTED);

    } while (FALSE);

    return;
} /* p2pFuncDissolve */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is called to dissolve from group or one group. (Would not change P2P FSM.)
*              1. GC: Disconnect from AP. (Send Deauth)
*              2. GO: Disconnect all STA
*
* @param[in] prAdapter   Pointer to the adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
p2pFuncDisconnect (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN BOOLEAN fgSendDeauth,
    IN UINT_16 u2ReasonCode
    )
{
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;
    ENUM_PARAM_MEDIA_STATE_T eOriMediaStatus;

    DBGLOG(P2P, TRACE, ("p2pFuncDisconnect()"));

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prStaRec != NULL));

        prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);
        eOriMediaStatus = prP2pBssInfo->eConnectionState;

        /* Indicate disconnect. */
        // TODO:
//        kalP2PGOStationUpdate
//        kalP2PGCIndicateConnectionStatus
        //p2pIndicationOfMediaStateToHost(prAdapter, PARAM_MEDIA_STATE_DISCONNECTED, prStaRec->aucMacAddr);
        if (prP2pBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT) {
            kalP2PGOStationUpdate(prAdapter->prGlueInfo, prStaRec, FALSE);
        }

        if (fgSendDeauth) {
            /* Send deauth. */
            authSendDeauthFrame(prAdapter,
                        prStaRec,
                        (P_SW_RFB_T)NULL,
                        u2ReasonCode,
                        (PFN_TX_DONE_HANDLER)p2pFsmRunEventDeauthTxDone);
		    /* Change station state. */
            cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);

            /* Reset Station Record Status. */
            p2pFuncResetStaRecStatus(prAdapter, prStaRec);

		
        }
        else {
            /* Change station state. */
            cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);

            /* Reset Station Record Status. */
            p2pFuncResetStaRecStatus(prAdapter, prStaRec);

            cnmStaRecFree(prAdapter, prStaRec, TRUE);

            if ((prP2pBssInfo->eCurrentOPMode != OP_MODE_ACCESS_POINT) ||
                    (prP2pBssInfo->rStaRecOfClientList.u4NumElem == 0)) {
                DBGLOG(P2P, TRACE, ("No More Client, Media Status DISCONNECTED\n"));
                p2pChangeMediaState(prAdapter, PARAM_MEDIA_STATE_DISCONNECTED);
            }

            if (eOriMediaStatus != prP2pBssInfo->eConnectionState) {
                /* Update Disconnected state to FW. */
                nicUpdateBss(prAdapter, NETWORK_TYPE_P2P_INDEX);
            }

        }

        if (prP2pBssInfo->eCurrentOPMode != OP_MODE_ACCESS_POINT) {
            /* GO: It would stop Beacon TX. GC: Stop all BSS related PS function. */
            nicPmIndicateBssAbort(prAdapter, NETWORK_TYPE_P2P_INDEX);

            /* Reset RLM related field of BSSINFO. */
            rlmBssAborted(prAdapter, prP2pBssInfo);
        }

    } while (FALSE);

    return;

} /* p2pFuncDisconnect */






WLAN_STATUS
p2pFuncTxMgmtFrame (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_MGMT_TX_REQ_INFO_T prMgmtTxReqInfo,
    IN P_MSDU_INFO_T prMgmtTxMsdu,
    IN UINT_64 u8Cookie
    )
{
    WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;
    P_MSDU_INFO_T prTxMsduInfo = (P_MSDU_INFO_T)NULL;
    P_WLAN_MAC_HEADER_T prWlanHdr = (P_WLAN_MAC_HEADER_T)NULL;
    P_STA_RECORD_T prStaRec = (P_STA_RECORD_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prMgmtTxReqInfo != NULL));

        if (prMgmtTxReqInfo->fgIsMgmtTxRequested) {

            // 1. prMgmtTxReqInfo->prMgmtTxMsdu != NULL
            /* Packet on driver, not done yet, drop it. */
            if ((prTxMsduInfo = prMgmtTxReqInfo->prMgmtTxMsdu) != NULL) {

                kalP2PIndicateMgmtTxStatus(prAdapter->prGlueInfo,
                                prMgmtTxReqInfo->u8Cookie,
                                FALSE,
                                prTxMsduInfo->prPacket,
                                (UINT_32)prTxMsduInfo->u2FrameLength);

                // Leave it to TX Done handler.
                //cnmMgtPktFree(prAdapter, prTxMsduInfo);
                prMgmtTxReqInfo->prMgmtTxMsdu = NULL;
            }

            // 2. prMgmtTxReqInfo->prMgmtTxMsdu == NULL
            /* Packet transmitted, wait tx done. (cookie issue) */
            // 20120105 frog - use another u8cookie to store this value.

        }

        ASSERT(prMgmtTxReqInfo->prMgmtTxMsdu == NULL);



        prWlanHdr = (P_WLAN_MAC_HEADER_T)((UINT_32)prMgmtTxMsdu->prPacket + MAC_TX_RESERVED_FIELD);
        prStaRec = cnmGetStaRecByAddress(prAdapter, NETWORK_TYPE_P2P_INDEX, prWlanHdr->aucAddr1);
        prMgmtTxMsdu->ucNetworkType = (UINT_8)NETWORK_TYPE_P2P_INDEX;

        switch (prWlanHdr->u2FrameCtrl & MASK_FRAME_TYPE) {
        case MAC_FRAME_PROBE_RSP:
			DBGLOG(P2P, TRACE, ("p2pFuncTxMgmtFrame:  TX MAC_FRAME_PROBE_RSP\n"));
            prMgmtTxMsdu = p2pFuncProcessP2pProbeRsp(prAdapter, prMgmtTxMsdu);
            break;
        default:
            break;
        }


        prMgmtTxReqInfo->u8Cookie = u8Cookie;
        prMgmtTxReqInfo->prMgmtTxMsdu = prMgmtTxMsdu;
        prMgmtTxReqInfo->fgIsMgmtTxRequested = TRUE;

        prMgmtTxMsdu->eSrc = TX_PACKET_MGMT;
        prMgmtTxMsdu->ucPacketType = HIF_TX_PACKET_TYPE_MGMT;
        prMgmtTxMsdu->ucStaRecIndex =  (prStaRec != NULL)?(prStaRec->ucIndex):(0xFF);
        if (prStaRec != NULL) {
            DBGLOG(P2P, TRACE, ("Mgmt with station record: "MACSTR" .\n", MAC2STR(prStaRec->aucMacAddr)));
        }

        prMgmtTxMsdu->ucMacHeaderLength = WLAN_MAC_MGMT_HEADER_LEN; // TODO: undcertain.
        prMgmtTxMsdu->fgIs802_1x = FALSE;
        prMgmtTxMsdu->fgIs802_11 = TRUE;
        prMgmtTxMsdu->ucTxSeqNum = nicIncreaseTxSeqNum(prAdapter);
        prMgmtTxMsdu->pfTxDoneHandler = p2pFsmRunEventMgmtFrameTxDone;
        prMgmtTxMsdu->fgIsBasicRate = TRUE;
        DBGLOG(P2P, TRACE, ("Mgmt seq NO. %d .\n", prMgmtTxMsdu->ucTxSeqNum));

        nicTxEnqueueMsdu(prAdapter, prMgmtTxMsdu);

    } while (FALSE);

    return rWlanStatus;
} /* p2pFuncTxMgmtFrame */



VOID
p2pFuncSetChannel (
    IN P_ADAPTER_T prAdapter,
    IN P_RF_CHANNEL_INFO_T prRfChannelInfo
    )
{
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prRfChannelInfo != NULL));

        prP2pConnSettings = prAdapter->rWifiVar.prP2PConnSettings;

        prP2pConnSettings->ucOperatingChnl = prRfChannelInfo->ucChannelNum;
        prP2pConnSettings->eBand = prRfChannelInfo->eBand;


    } while (FALSE);

    return;
}
/* p2pFuncSetChannel */



/*----------------------------------------------------------------------------*/
/*!
* @brief Retry JOIN for AUTH_MODE_AUTO_SWITCH
*
* @param[in] prStaRec       Pointer to the STA_RECORD_T
*
* @retval TRUE      We will retry JOIN
* @retval FALSE     We will not retry JOIN
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
p2pFuncRetryJOIN (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN P_P2P_JOIN_INFO_T prJoinInfo
    )
{
    P_MSG_JOIN_REQ_T prJoinReqMsg = (P_MSG_JOIN_REQ_T)NULL;
    BOOLEAN fgRetValue = FALSE;

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                            (prStaRec != NULL) &&
                            (prJoinInfo != NULL));

        /* Retry other AuthType if possible */
        if (!prJoinInfo->ucAvailableAuthTypes) {
            break;
        }

        if (prJoinInfo->ucAvailableAuthTypes &
            (UINT_8)AUTH_TYPE_SHARED_KEY) {

            DBGLOG(P2P, INFO, ("RETRY JOIN INIT: Retry Authentication with AuthType == SHARED_KEY.\n"));

            prJoinInfo->ucAvailableAuthTypes &=
                ~(UINT_8)AUTH_TYPE_SHARED_KEY;

            prStaRec->ucAuthAlgNum = (UINT_8)AUTH_ALGORITHM_NUM_SHARED_KEY;
        }
        else {
            DBGLOG(P2P, ERROR, ("RETRY JOIN INIT: Retry Authentication with Unexpected AuthType.\n"));
            ASSERT(0);
            break;
        }

        prJoinInfo->ucAvailableAuthTypes = 0; /* No more available Auth Types */

        /* Trigger SAA to start JOIN process. */
        prJoinReqMsg = (P_MSG_JOIN_REQ_T)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_JOIN_REQ_T));
        if (!prJoinReqMsg) {
            ASSERT(0); // Can't trigger SAA FSM
            break;
        }

        prJoinReqMsg->rMsgHdr.eMsgId = MID_P2P_SAA_FSM_START;
        prJoinReqMsg->ucSeqNum = ++prJoinInfo->ucSeqNumOfReqMsg;
        prJoinReqMsg->prStaRec = prStaRec;

        mboxSendMsg(prAdapter,
                MBOX_ID_0,
                (P_MSG_HDR_T) prJoinReqMsg,
                MSG_SEND_METHOD_BUF);


        fgRetValue = TRUE;
    } while (FALSE);

    return fgRetValue;



}/* end of p2pFuncRetryJOIN() */





/*----------------------------------------------------------------------------*/
/*!
* @brief This function will update the contain of BSS_INFO_T for AIS network once
*        the association was completed.
*
* @param[in] prStaRec               Pointer to the STA_RECORD_T
* @param[in] prAssocRspSwRfb        Pointer to SW RFB of ASSOC RESP FRAME.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
p2pFuncUpdateBssInfoForJOIN (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc,
    IN P_STA_RECORD_T prStaRec,
    IN P_SW_RFB_T prAssocRspSwRfb
    )
{
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;
    P_WLAN_ASSOC_RSP_FRAME_T prAssocRspFrame = (P_WLAN_ASSOC_RSP_FRAME_T)NULL;
    UINT_16 u2IELength;
    PUINT_8 pucIE;

    DEBUGFUNC("p2pUpdateBssInfoForJOIN()");

    ASSERT(prAdapter);
    ASSERT(prStaRec);
    ASSERT(prAssocRspSwRfb);

    prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);
    prP2pConnSettings = prAdapter->rWifiVar.prP2PConnSettings;
    prAssocRspFrame = (P_WLAN_ASSOC_RSP_FRAME_T) prAssocRspSwRfb->pvHeader;

    DBGLOG(P2P, INFO, ("Update P2P_BSS_INFO_T and apply settings to MAC\n"));

    //3 <1> Update BSS_INFO_T from AIS_FSM_INFO_T or User Settings
    //4 <1.1> Setup Operation Mode
    prP2pBssInfo->eCurrentOPMode = OP_MODE_INFRASTRUCTURE;

    //4 <1.2> Setup SSID
    COPY_SSID(prP2pBssInfo->aucSSID,
              prP2pBssInfo->ucSSIDLen,
              prP2pConnSettings->aucSSID,
              prP2pConnSettings->ucSSIDLen);

    if (prBssDesc == NULL) {
        /* Target BSS NULL. */
        DBGLOG(P2P, TRACE,("Target BSS NULL\n"));
        return;
    }


    if (UNEQUAL_MAC_ADDR(prBssDesc->aucBSSID, prAssocRspFrame->aucBSSID)) {
        ASSERT(FALSE);
    }

    //4 <1.3> Setup Channel, Band
    prP2pBssInfo->ucPrimaryChannel = prBssDesc->ucChannelNum;
    prP2pBssInfo->eBand = prBssDesc->eBand;


    //3 <2> Update BSS_INFO_T from STA_RECORD_T
    //4 <2.1> Save current AP's STA_RECORD_T and current AID
    prP2pBssInfo->prStaRecOfAP = prStaRec;
    prP2pBssInfo->u2AssocId = prStaRec->u2AssocId;

    //4 <2.2> Setup Capability
    prP2pBssInfo->u2CapInfo = prStaRec->u2CapInfo; /* Use AP's Cap Info as BSS Cap Info */

    if (prP2pBssInfo->u2CapInfo & CAP_INFO_SHORT_PREAMBLE) {
        prP2pBssInfo->fgIsShortPreambleAllowed = TRUE;
    }
    else {
        prP2pBssInfo->fgIsShortPreambleAllowed = FALSE;
    }

    //4 <2.3> Setup PHY Attributes and Basic Rate Set/Operational Rate Set
    prP2pBssInfo->ucPhyTypeSet = prStaRec->ucDesiredPhyTypeSet;

    prP2pBssInfo->ucNonHTBasicPhyType = prStaRec->ucNonHTBasicPhyType;

    prP2pBssInfo->u2OperationalRateSet = prStaRec->u2OperationalRateSet;
    prP2pBssInfo->u2BSSBasicRateSet = prStaRec->u2BSSBasicRateSet;


    //3 <3> Update BSS_INFO_T from SW_RFB_T (Association Resp Frame)
    //4 <3.1> Setup BSSID
    COPY_MAC_ADDR(prP2pBssInfo->aucBSSID, prAssocRspFrame->aucBSSID);


    u2IELength = (UINT_16) ((prAssocRspSwRfb->u2PacketLen - prAssocRspSwRfb->u2HeaderLen) -
        (OFFSET_OF(WLAN_ASSOC_RSP_FRAME_T, aucInfoElem[0]) - WLAN_MAC_MGMT_HEADER_LEN));
    pucIE = prAssocRspFrame->aucInfoElem;


    //4 <3.2> Parse WMM and setup QBSS flag
    /* Parse WMM related IEs and configure HW CRs accordingly */
    mqmProcessAssocRsp(prAdapter, prAssocRspSwRfb, pucIE, u2IELength);

    prP2pBssInfo->fgIsQBSS = prStaRec->fgIsQoS;

    //3 <4> Update BSS_INFO_T from BSS_DESC_T
    ASSERT(prBssDesc);

    prBssDesc->fgIsConnecting = FALSE;
    prBssDesc->fgIsConnected = TRUE;

    //4 <4.1> Setup MIB for current BSS
    prP2pBssInfo->u2BeaconInterval = prBssDesc->u2BeaconInterval;
    /* NOTE: Defer ucDTIMPeriod updating to when beacon is received after connection */
    prP2pBssInfo->ucDTIMPeriod = 0;
    prP2pBssInfo->u2ATIMWindow = 0;

    prP2pBssInfo->ucBeaconTimeoutCount = AIS_BEACON_TIMEOUT_COUNT_INFRA;

    //4 <4.2> Update HT information and set channel
    /* Record HT related parameters in rStaRec and rBssInfo
     * Note: it shall be called before nicUpdateBss()
     */
    rlmProcessAssocRsp(prAdapter, prAssocRspSwRfb, pucIE, u2IELength);

    //4 <4.3> Sync with firmware for BSS-INFO
    nicUpdateBss(prAdapter, NETWORK_TYPE_P2P_INDEX);

    //4 <4.4> *DEFER OPERATION* nicPmIndicateBssConnected() will be invoked
    //inside scanProcessBeaconAndProbeResp() after 1st beacon is received

    return;
} /* end of p2pUpdateBssInfoForJOIN() */



/*----------------------------------------------------------------------------*/
/*!
* @brief This function will validate the Rx Auth Frame and then return
*        the status code to AAA to indicate if need to perform following actions
*        when the specified conditions were matched.
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] prSwRfb            Pointer to SW RFB data structure.
* @param[in] pprStaRec          Pointer to pointer of STA_RECORD_T structure.
* @param[out] pu2StatusCode     The Status Code of Validation Result
*
* @retval TRUE      Reply the Auth
* @retval FALSE     Don't reply the Auth
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
p2pFuncValidateAuth (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    IN PP_STA_RECORD_T pprStaRec,
    OUT PUINT_16 pu2StatusCode
    )
{
    BOOLEAN fgReplyAuth = TRUE;
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;
    P_STA_RECORD_T prStaRec = (P_STA_RECORD_T)NULL;
    P_WLAN_AUTH_FRAME_T prAuthFrame = (P_WLAN_AUTH_FRAME_T)NULL;

   DBGLOG(P2P, TRACE, ("p2pValidate Authentication Frame\n"))

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                                    (prSwRfb != NULL) &&
                                    (pprStaRec != NULL) &&
                                    (pu2StatusCode != NULL));

        /* P2P 3.2.8 */
        *pu2StatusCode = STATUS_CODE_REQ_DECLINED;

        prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);
        prAuthFrame = (P_WLAN_AUTH_FRAME_T)prSwRfb->pvHeader;


        if (prP2pBssInfo->eCurrentOPMode != OP_MODE_ACCESS_POINT) {
            /* We are not under AP Mode yet. */
            fgReplyAuth = FALSE;
            DBGLOG(P2P, WARN, ("Current OP mode is not under AP mode. (%d)\n", prP2pBssInfo->eCurrentOPMode));
            break;
        }

        prStaRec = cnmGetStaRecByAddress(prAdapter,
                            (UINT_8) NETWORK_TYPE_P2P_INDEX,
                            prAuthFrame->aucSrcAddr);

        if (!prStaRec) {
            prStaRec = cnmStaRecAlloc(prAdapter,
                            (UINT_8) NETWORK_TYPE_P2P_INDEX);

            /* TODO(Kevin): Error handling of allocation of STA_RECORD_T for
             * exhausted case and do removal of unused STA_RECORD_T.
             */
            /* Sent a message event to clean un-used STA_RECORD_T. */
            ASSERT(prStaRec);

            COPY_MAC_ADDR(prStaRec->aucMacAddr, prAuthFrame->aucSrcAddr);

            prSwRfb->ucStaRecIdx = prStaRec->ucIndex;

            prStaRec->u2BSSBasicRateSet = prP2pBssInfo->u2BSSBasicRateSet;

            prStaRec->u2DesiredNonHTRateSet = RATE_SET_ERP_P2P;

            prStaRec->u2OperationalRateSet = RATE_SET_ERP_P2P;
            prStaRec->ucPhyTypeSet = PHY_TYPE_SET_802_11GN;
            prStaRec->eStaType = STA_TYPE_P2P_GC;

            /* NOTE(Kevin): Better to change state here, not at TX Done */
            cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);
        }
        else {
            prSwRfb->ucStaRecIdx = prStaRec->ucIndex;

            if ((prStaRec->ucStaState > STA_STATE_1) && (IS_STA_IN_P2P(prStaRec))) {

                cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);

                p2pFuncResetStaRecStatus(prAdapter, prStaRec);

                bssRemoveStaRecFromClientList(prAdapter, prP2pBssInfo, prStaRec);
            }

        }

        if (prP2pBssInfo->rStaRecOfClientList.u4NumElem >= P2P_MAXIMUM_CLIENT_COUNT ||
            kalP2PMaxClients(prAdapter->prGlueInfo, prP2pBssInfo->rStaRecOfClientList.u4NumElem)) {
            /* GROUP limit full. */
            /* P2P 3.2.8 */
            DBGLOG(P2P, WARN, ("Group Limit Full. (%d)\n", (INT_16)prP2pBssInfo->rStaRecOfClientList.u4NumElem));

			bssRemoveStaRecFromClientList(prAdapter, prP2pBssInfo, prStaRec);
			
			cnmStaRecFree(prAdapter, prStaRec, FALSE);
            break;
        }
        else {
            /* Hotspot Blacklist */
            if(prAuthFrame->aucSrcAddr) {
                if(kalP2PCmpBlackList(prAdapter->prGlueInfo, prAuthFrame->aucSrcAddr)) {
                    fgReplyAuth = FALSE;
                    return fgReplyAuth;
                }
            }       
        }

        //prStaRec->eStaType = STA_TYPE_INFRA_CLIENT;
        prStaRec->eStaType = STA_TYPE_P2P_GC;

        prStaRec->ucNetTypeIndex = NETWORK_TYPE_P2P_INDEX;

        /* Update Station Record - Status/Reason Code */
        prStaRec->u2StatusCode = STATUS_CODE_SUCCESSFUL;

        prStaRec->ucJoinFailureCount = 0;

        *pprStaRec = prStaRec;

        *pu2StatusCode = STATUS_CODE_SUCCESSFUL;

    } while (FALSE);


    return fgReplyAuth;

} /* p2pFuncValidateAuth */




VOID
p2pFuncResetStaRecStatus (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    )
{
    do {
        if ((prAdapter == NULL) || (prStaRec == NULL)) {
            ASSERT(FALSE);
            break;
        }


        prStaRec->u2StatusCode = STATUS_CODE_SUCCESSFUL;
        prStaRec->u2ReasonCode = REASON_CODE_RESERVED;
        prStaRec->ucJoinFailureCount = 0;
        prStaRec->fgTransmitKeyExist = FALSE;

        prStaRec->fgSetPwrMgtBit = FALSE;

    } while (FALSE);

    return;
} /* p2pFuncResetStaRecStatus */



/*----------------------------------------------------------------------------*/
/*!
* @brief The function is used to initialize the value of the connection settings for
*        P2P network
*
* @param (none)
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
p2pFuncInitConnectionSettings (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_CONNECTION_SETTINGS_T prP2PConnSettings
    )
{
    P_DEVICE_TYPE_T prDevType;
    UINT_8 aucDefaultDevName[] = P2P_DEFAULT_DEV_NAME;
    UINT_8 aucWfaOui[] = VENDOR_OUI_WFA;

    ASSERT(prP2PConnSettings);

    /* Setup Default Device Name */
    prP2PConnSettings->ucDevNameLen = P2P_DEFAULT_DEV_NAME_LEN;
    kalMemCopy(prP2PConnSettings->aucDevName, aucDefaultDevName, sizeof(aucDefaultDevName));

    /* Setup Primary Device Type (Big-Endian) */
    prDevType = &prP2PConnSettings->rPrimaryDevTypeBE;

    prDevType->u2CategoryId = HTONS(P2P_DEFAULT_PRIMARY_CATEGORY_ID);
    prDevType->u2SubCategoryId = HTONS(P2P_DEFAULT_PRIMARY_SUB_CATEGORY_ID);

    prDevType->aucOui[0] = aucWfaOui[0];
    prDevType->aucOui[1] = aucWfaOui[1];
    prDevType->aucOui[2] = aucWfaOui[2];
    prDevType->aucOui[3] = VENDOR_OUI_TYPE_WPS;

    /* Setup Secondary Device Type */
    prP2PConnSettings->ucSecondaryDevTypeCount = 0;

    /* Setup Default Config Method */
    prP2PConnSettings->eConfigMethodSelType = ENUM_CONFIG_METHOD_SEL_AUTO;
    prP2PConnSettings->u2ConfigMethodsSupport = P2P_DEFAULT_CONFIG_METHOD;
    prP2PConnSettings->u2TargetConfigMethod = 0;
    prP2PConnSettings->u2LocalConfigMethod = 0;
    prP2PConnSettings->fgIsPasswordIDRdy = FALSE;

    /* For Device Capability */
    prP2PConnSettings->fgSupportServiceDiscovery = FALSE;
    prP2PConnSettings->fgSupportClientDiscoverability = TRUE;
    prP2PConnSettings->fgSupportConcurrentOperation = TRUE;
    prP2PConnSettings->fgSupportInfraManaged = FALSE;
    prP2PConnSettings->fgSupportInvitationProcedure = FALSE;

    /* For Group Capability */
#if CFG_SUPPORT_PERSISTENT_GROUP
    prP2PConnSettings->fgSupportPersistentP2PGroup = TRUE;
#else
    prP2PConnSettings->fgSupportPersistentP2PGroup = FALSE;
#endif
    prP2PConnSettings->fgSupportIntraBSSDistribution = TRUE;
    prP2PConnSettings->fgSupportCrossConnection = TRUE;
    prP2PConnSettings->fgSupportPersistentReconnect = FALSE;

    prP2PConnSettings->fgSupportOppPS = FALSE;
    prP2PConnSettings->u2CTWindow = P2P_CTWINDOW_DEFAULT;

    /* For Connection Settings. */
    prP2PConnSettings->eAuthMode = AUTH_MODE_OPEN;

    prP2PConnSettings->prTargetP2pDesc = NULL;
    prP2PConnSettings->ucSSIDLen = 0;

    /* Misc */
    prP2PConnSettings->fgIsScanReqIssued = FALSE;
    prP2PConnSettings->fgIsServiceDiscoverIssued = FALSE;
    prP2PConnSettings->fgP2pGroupLimit = FALSE;
    prP2PConnSettings->ucOperatingChnl = 0;
    prP2PConnSettings->ucListenChnl = 0;
    prP2PConnSettings->ucTieBreaker = (UINT_8)(kalRandomNumber() & 0x1);

    prP2PConnSettings->eFormationPolicy = ENUM_P2P_FORMATION_POLICY_AUTO;

    return;
} /* p2pFuncInitConnectionSettings */





/*----------------------------------------------------------------------------*/
/*!
* @brief This function will validate the Rx Assoc Req Frame and then return
*        the status code to AAA to indicate if need to perform following actions
*        when the specified conditions were matched.
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] prSwRfb            Pointer to SW RFB data structure.
* @param[out] pu2StatusCode     The Status Code of Validation Result
*
* @retval TRUE      Reply the Assoc Resp
* @retval FALSE     Don't reply the Assoc Resp
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
p2pFuncValidateAssocReq (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    OUT PUINT_16 pu2StatusCode
    )
{
    BOOLEAN fgReplyAssocResp = TRUE;
    P_WLAN_ASSOC_REQ_FRAME_T prAssocReqFrame = (P_WLAN_ASSOC_REQ_FRAME_T)NULL;
    P_STA_RECORD_T prStaRec = (P_STA_RECORD_T)NULL;
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;
#if CFG_SUPPORT_WFD
    P_WFD_CFG_SETTINGS_T prWfdCfgSettings = (P_WFD_CFG_SETTINGS_T)NULL;
    P_WFD_ATTRIBUTE_T prWfdAttribute = (P_WFD_ATTRIBUTE_T)NULL;
    BOOLEAN fgNeedFree = FALSE;
#endif


    /* TODO(Kevin): Call P2P functions to check ..
                    2. Check we can accept connection from thsi peer
                       a. If we are in PROVISION state, only accept the peer we do the GO formation previously.
                       b. If we are in OPERATION state, only accept the other peer when P2P_GROUP_LIMIT is 0.
                    3. Check Black List here.
     */

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                                        (prSwRfb != NULL) &&
                                        (pu2StatusCode != NULL));

        *pu2StatusCode = STATUS_CODE_REQ_DECLINED;
        prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);
        prAssocReqFrame = (P_WLAN_ASSOC_REQ_FRAME_T)prSwRfb->pvHeader;

        prStaRec = cnmGetStaRecByIndex(prAdapter, prSwRfb->ucStaRecIdx);

        if (prStaRec == NULL) {
            /* Station record should be ready while RX AUTH frame. */
            fgReplyAssocResp = FALSE;
            ASSERT(FALSE);
            break;
        }
        else {
            prStaRec->ucRCPI = prSwRfb->prHifRxHdr->ucRcpi;
        }

        prStaRec->u2DesiredNonHTRateSet &= prP2pBssInfo->u2OperationalRateSet;
        prStaRec->ucDesiredPhyTypeSet = prStaRec->ucPhyTypeSet & prP2pBssInfo->ucPhyTypeSet;

        if (prStaRec->ucDesiredPhyTypeSet == 0) {
            /* The station only support 11B rate. */
            *pu2StatusCode = STATUS_CODE_ASSOC_DENIED_RATE_NOT_SUPPORTED;
            break;
        }

#if CFG_SUPPORT_WFD && 1
        //LOG_FUNC("Skip check WFD IE becasue some API is not ready\n"); /* Eddie */
        if (!prAdapter->rWifiVar.prP2pFsmInfo) {
            fgReplyAssocResp = FALSE;
            ASSERT(FALSE);
            break;
        }
        
        prWfdCfgSettings = &prAdapter->rWifiVar.prP2pFsmInfo->rWfdConfigureSettings;
        DBGLOG(P2P, INFO,("Current WfdCfgSettings wfd_en %u wfd_info 0x%x  wfd_policy 0x%x wfd_flag 0x%x\n", 
                prWfdCfgSettings->ucWfdEnable, prWfdCfgSettings->u2WfdDevInfo, 
                prWfdCfgSettings->u4WfdPolicy, prWfdCfgSettings->u4WfdFlag)); /* Eddie */
        if (prWfdCfgSettings->ucWfdEnable) {
            if (prWfdCfgSettings->u4WfdPolicy & BIT(6)) {
                /* Rejected all. */
                break;
            }
            else {
                
                //UINT_16 u2AttriListLen = 0;
                UINT_16 u2WfdDevInfo = 0;
                P_WFD_DEVICE_INFORMATION_IE_T prAttriWfdDevInfo = (P_WFD_DEVICE_INFORMATION_IE_T)NULL;
                
                //fgNeedFree = p2pFuncGetAttriList(prAdapter,
                //                VENDOR_OUI_TYPE_WFD,
                //                (PUINT_8)prAssocReqFrame->aucInfoElem,
                //                (prSwRfb->u2PacketLen - OFFSET_OF(WLAN_ASSOC_REQ_FRAME_T, aucInfoElem)),
                //                (PPUINT_8)&prWfdAttribute,
                //                &u2AttriListLen);

                prAttriWfdDevInfo = (P_WFD_DEVICE_INFORMATION_IE_T)
                                        p2pFuncGetSpecAttri(prAdapter,
                                                     VENDOR_OUI_TYPE_WFD,
                                                     (PUINT_8)prAssocReqFrame->aucInfoElem,
                                                     (prSwRfb->u2PacketLen - OFFSET_OF(WLAN_ASSOC_REQ_FRAME_T, aucInfoElem)),
                                                     WFD_ATTRI_ID_DEV_INFO);

                if ((prWfdCfgSettings->u4WfdPolicy & BIT(5)) && (prAttriWfdDevInfo != NULL)) {
                    /* Rejected with WFD IE. */
                    break;
                }

                if ((prWfdCfgSettings->u4WfdPolicy & BIT(0)) && (prAttriWfdDevInfo == NULL)) {
                    /* Rejected without WFD IE. */
                    break;
                }

                if (prAttriWfdDevInfo != NULL) {

                    //prAttriWfdDevInfo = (P_WFD_DEVICE_INFORMATION_IE_T)p2pFuncGetSpecAttri(prAdapter,
                    //                                                               VENDOR_OUI_TYPE_WFD,
                    //                                                                (PUINT_8)prWfdAttribute, 
                    //                                                                u2AttriListLen, 
                    //                                                                WFD_ATTRI_ID_DEV_INFO);
                    //if (prAttriWfdDevInfo == NULL) {
                    //    /* No such attribute. */
                    //    break;
                    //}

                    WLAN_GET_FIELD_BE16(&prAttriWfdDevInfo->u2WfdDevInfo, &u2WfdDevInfo);
                    DBGLOG(P2P, INFO,("RX Assoc Req WFD Info:0x%x.\n", u2WfdDevInfo));
                    
                    if ((prWfdCfgSettings->u4WfdPolicy & BIT(1)) && ((u2WfdDevInfo & 0x3) == 0x0)) {
                        /* Rejected because of SOURCE. */
                        break;
                    }

                    if ((prWfdCfgSettings->u4WfdPolicy & BIT(2)) && ((u2WfdDevInfo & 0x3) == 0x1)) {
                        /* Rejected because of Primary Sink. */
                        break;
                    }

                    if ((prWfdCfgSettings->u4WfdPolicy & BIT(3)) && ((u2WfdDevInfo & 0x3) == 0x2)) {
                        /* Rejected because of Secondary Sink. */
                        break;
                    }

                    if ((prWfdCfgSettings->u4WfdPolicy & BIT(4)) && ((u2WfdDevInfo & 0x3) == 0x3)) {
                        /* Rejected because of Source & Primary Sink. */
                        break;
                    }

                    /* Check role */

                    if(prWfdCfgSettings->u4WfdFlag & WFD_FLAGS_DEV_INFO_VALID) {

                        if((prWfdCfgSettings->u2WfdDevInfo & BITS(0,1)) == 0x3) {
                            //P_MSG_WFD_CONFIG_SETTINGS_CHANGED_T prMsgWfdCfgUpdate = (P_MSG_WFD_CONFIG_SETTINGS_CHANGED_T)NULL;
                            UINT_16 u2DevInfo = prWfdCfgSettings->u2WfdDevInfo;

                           /* We may change role here if we are dual role */

                            if((u2WfdDevInfo & BITS(0,1)) == 0x00 /* Peer is Source*/) {
                                DBGLOG(P2P, INFO,("WFD: Switch role to primary sink\n"));

                                prWfdCfgSettings->u2WfdDevInfo &= ~BITS(0,1);
                                prWfdCfgSettings->u2WfdDevInfo |= 0x1;

                                /* event to annonce the role is chanaged to P-Sink */
                     
                            }
                            else if((u2WfdDevInfo & BITS(0,1)) == 0x01 /* Peer is P-Sink */) {

                                DBGLOG(P2P, INFO,("WFD: Switch role to source\n"));
                                prWfdCfgSettings->u2WfdDevInfo &= ~BITS(0,1);
                                /* event to annonce the role is chanaged to Source */
                           }
                           else {

                                DBGLOG(P2P, INFO,("WFD: Peer role is wrong type(dev 0x%x)\n", (u2DevInfo)));
                                DBGLOG(P2P, INFO,("WFD: Switch role to source\n"));
                                prWfdCfgSettings->u2WfdDevInfo &= ~BITS(0,1);
                                /* event to annonce the role is chanaged to Source */
                           }

                            p2pFsmRunEventWfdSettingUpdate (prAdapter,NULL);

                        } /* Dual role p2p->wfd_params->WfdDevInfo */
                    } /* WFD_FLAG_DEV_INFO_VALID */


                }
                else {
                    /* Without WFD IE. 
                                     * Do nothing. Accept the connection request.
                                     */
                }
            }

        } /* ucWfdEnable */

#endif

        *pu2StatusCode = STATUS_CODE_SUCCESSFUL;

    } while (FALSE);

#if CFG_SUPPORT_WFD
    if ((prWfdAttribute) && (fgNeedFree)) {
        kalMemFree(prWfdAttribute, VIR_MEM_TYPE, WPS_MAXIMUM_ATTRIBUTES_CACHE_SIZE);
    }
#endif

    return fgReplyAssocResp;

} /* p2pFuncValidateAssocReq */




/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to check the P2P IE
*
*
* @return none
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
p2pFuncParseCheckForP2PInfoElem (
    IN  P_ADAPTER_T prAdapter,
    IN  PUINT_8 pucBuf,
    OUT PUINT_8 pucOuiType
    )
{
    UINT_8 aucWfaOui[] = VENDOR_OUI_WFA_SPECIFIC;
    P_IE_WFA_T prWfaIE = (P_IE_WFA_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (pucBuf != NULL) && (pucOuiType != NULL));

        prWfaIE = (P_IE_WFA_T)pucBuf;

        if (IE_LEN(pucBuf) <= ELEM_MIN_LEN_WFA_OUI_TYPE_SUBTYPE) {
            break;
        }
        else if (prWfaIE->aucOui[0] != aucWfaOui[0] ||
                 prWfaIE->aucOui[1] != aucWfaOui[1] ||
                 prWfaIE->aucOui[2] != aucWfaOui[2]) {
            break;
        }

        *pucOuiType = prWfaIE->ucOuiType;

        return TRUE;
    } while (FALSE);

    return FALSE;
} /* p2pFuncParseCheckForP2PInfoElem */




/*----------------------------------------------------------------------------*/
/*!
* @brief This function will validate the Rx Probe Request Frame and then return
*        result to BSS to indicate if need to send the corresponding Probe Response
*        Frame if the specified conditions were matched.
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] prSwRfb            Pointer to SW RFB data structure.
* @param[out] pu4ControlFlags   Control flags for replying the Probe Response
*
* @retval TRUE      Reply the Probe Response
* @retval FALSE     Don't reply the Probe Response
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
p2pFuncValidateProbeReq (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    OUT PUINT_32 pu4ControlFlags
    )
{
    BOOLEAN fgIsReplyProbeRsp = FALSE;
    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;

    DEBUGFUNC("p2pFuncValidateProbeReq");
	DBGLOG(P2P, TRACE, ("p2pFuncValidateProbeReq\n"));

    do {

        ASSERT_BREAK((prAdapter != NULL) && (prSwRfb != NULL));

        prP2pFsmInfo = prAdapter->rWifiVar.prP2pFsmInfo;

        if (prP2pFsmInfo->u4P2pPacketFilter & PARAM_PACKET_FILTER_PROBE_REQ) {


			/* Leave the probe response to p2p_supplicant. */
            kalP2PIndicateRxMgmtFrame(prAdapter->prGlueInfo, prSwRfb);
        }

    } while (FALSE);

    return fgIsReplyProbeRsp;

} /* end of p2pFuncValidateProbeReq() */



/*----------------------------------------------------------------------------*/
/*!
* @brief This function will validate the Rx Probe Request Frame and then return
*        result to BSS to indicate if need to send the corresponding Probe Response
*        Frame if the specified conditions were matched.
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] prSwRfb            Pointer to SW RFB data structure.
* @param[out] pu4ControlFlags   Control flags for replying the Probe Response
*
* @retval TRUE      Reply the Probe Response
* @retval FALSE     Don't reply the Probe Response
*/
/*----------------------------------------------------------------------------*/
VOID
p2pFuncValidateRxActionFrame (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    )
{
    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;

    DEBUGFUNC("p2pFuncValidateProbeReq");

    do {

        ASSERT_BREAK((prAdapter != NULL) && (prSwRfb != NULL));

        prP2pFsmInfo = prAdapter->rWifiVar.prP2pFsmInfo;

        if (prP2pFsmInfo->u4P2pPacketFilter & PARAM_PACKET_FILTER_ACTION_FRAME) {
            /* Leave the probe response to p2p_supplicant. */
            kalP2PIndicateRxMgmtFrame(prAdapter->prGlueInfo, prSwRfb);
        }

    } while (FALSE);

    return;

} /* p2pFuncValidateRxMgmtFrame */



BOOLEAN
p2pFuncIsAPMode (
    IN P_P2P_FSM_INFO_T prP2pFsmInfo
    )
{
    if (prP2pFsmInfo) {      
        if(prP2pFsmInfo->fgIsWPSMode == 1){
            return FALSE;
        }
        return prP2pFsmInfo->fgIsApMode;
    }
    else {
        return FALSE;
    }
}
/* p2pFuncIsAPMode */



VOID
p2pFuncParseBeaconContent (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN PUINT_8 pucIEInfo,
    IN UINT_32 u4IELen
    )
{
    PUINT_8 pucIE = (PUINT_8)NULL;
    UINT_16 u2Offset = 0;
    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    BOOL    ucNewSecMode = FALSE;
    BOOL    ucOldSecMode = FALSE;

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                        (prP2pBssInfo != NULL));

        if (u4IELen == 0) {
            break;
        }

        prP2pSpecificBssInfo = prAdapter->rWifiVar.prP2pSpecificBssInfo;
        prP2pSpecificBssInfo->u2AttributeLen = 0;

        ASSERT_BREAK(pucIEInfo != NULL);

        pucIE = pucIEInfo;

        ucOldSecMode = kalP2PGetCipher(prAdapter->prGlueInfo);

        IE_FOR_EACH(pucIE, u4IELen, u2Offset) {
            switch (IE_ID(pucIE)) {
            case ELEM_ID_SSID:   /* 0 */ /* V */ /* Done */
                {
                    DBGLOG(P2P, TRACE, ("SSID update\n"));

                    /* Update when starting GO. */
                    COPY_SSID(prP2pBssInfo->aucSSID,
                                                prP2pBssInfo->ucSSIDLen,
                                                SSID_IE(pucIE)->aucSSID,
                                                SSID_IE(pucIE)->ucLength);

                    COPY_SSID(prP2pSpecificBssInfo->aucGroupSsid,
                                                prP2pSpecificBssInfo->u2GroupSsidLen,
                                                SSID_IE(pucIE)->aucSSID,
                                                SSID_IE(pucIE)->ucLength);

                }
                break;
            case ELEM_ID_SUP_RATES:  /* 1 */ /* V */ /* Done */
                {
                    DBGLOG(P2P, TRACE, ("Support Rate IE\n"));
                    kalMemCopy(prP2pBssInfo->aucAllSupportedRates,
                                    SUP_RATES_IE(pucIE)->aucSupportedRates,
                                    SUP_RATES_IE(pucIE)->ucLength);

                    prP2pBssInfo->ucAllSupportedRatesLen = SUP_RATES_IE(pucIE)->ucLength;

                    DBGLOG_MEM8(P2P, TRACE, SUP_RATES_IE(pucIE)->aucSupportedRates, SUP_RATES_IE(pucIE)->ucLength);
                }
                break;
            case ELEM_ID_DS_PARAM_SET: /* 3 */ /* V */ /* Done */
                {
                    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = prAdapter->rWifiVar.prP2PConnSettings;

                    DBGLOG(P2P, TRACE, ("DS PARAM IE\n"));

                    ASSERT(prP2pConnSettings->ucOperatingChnl == DS_PARAM_IE(pucIE)->ucCurrChnl);

                    if (prP2pConnSettings->eBand != BAND_2G4) {
                        ASSERT(FALSE);
                        break;
                    }

                    //prP2pBssInfo->ucPrimaryChannel = DS_PARAM_IE(pucIE)->ucCurrChnl;

                    //prP2pBssInfo->eBand = BAND_2G4;
                }
                break;
            case ELEM_ID_TIM: /* 5 */ /* V */
                DBGLOG(P2P, TRACE, ("TIM IE\n"));
                TIM_IE(pucIE)->ucDTIMPeriod = prP2pBssInfo->ucDTIMPeriod;
                break;
            case ELEM_ID_ERP_INFO:  /* 42 */    /* V */
                {
#if 1
                    /* This IE would dynamic change due to FW detection change is required. */
                    DBGLOG(P2P, TRACE, ("ERP IE will be over write by driver\n"));
                    DBGLOG(P2P, TRACE, ("    ucERP: %x. \n", ERP_INFO_IE(pucIE)->ucERP));

#else
                    /* This IE would dynamic change due to FW detection change is required. */
                    DBGLOG(P2P, TRACE, ("ERP IE.\n"));

                    prP2pBssInfo->ucPhyTypeSet |= PHY_TYPE_SET_802_11GN;

                    ASSERT(prP2pBssInfo->eBand == BAND_2G4);

                    prP2pBssInfo->fgObssErpProtectMode = ((ERP_INFO_IE(pucIE)->ucERP & ERP_INFO_USE_PROTECTION)? TRUE : FALSE);

                    prP2pBssInfo->fgErpProtectMode = ((ERP_INFO_IE(pucIE)->ucERP & (ERP_INFO_USE_PROTECTION | ERP_INFO_NON_ERP_PRESENT))? TRUE : FALSE);
#endif

                }
                break;
            case ELEM_ID_HT_CAP:    /* 45 */    /* V */
                {
#if 1
                    DBGLOG(P2P, TRACE, ("HT CAP IE would be overwritten by driver\n"));

                    DBGLOG(P2P, TRACE, ("HT Cap Info:%x, AMPDU Param:%x\n", HT_CAP_IE(pucIE)->u2HtCapInfo, HT_CAP_IE(pucIE)->ucAmpduParam));

                    DBGLOG(P2P, TRACE, ("HT Extended Cap Info:%x, TX Beamforming Cap Info:%lx, Ant Selection Cap Info%x \n",
                                                                                            HT_CAP_IE(pucIE)->u2HtExtendedCap,
                                                                                            HT_CAP_IE(pucIE)->u4TxBeamformingCap,
                                                                                            HT_CAP_IE(pucIE)->ucAselCap));
#else
                    prP2pBssInfo->ucPhyTypeSet |= PHY_TYPE_SET_802_11N;

                    /* u2HtCapInfo */
                    if ((HT_CAP_IE(pucIE)->u2HtCapInfo &
                            (HT_CAP_INFO_SUP_CHNL_WIDTH | HT_CAP_INFO_SHORT_GI_40M | HT_CAP_INFO_DSSS_CCK_IN_40M)) == 0) {
                        prP2pBssInfo->fgAssoc40mBwAllowed = FALSE;
                    }
                    else {
                        prP2pBssInfo->fgAssoc40mBwAllowed = TRUE;
                    }

                    if ((HT_CAP_IE(pucIE)->u2HtCapInfo &
                            (HT_CAP_INFO_SHORT_GI_20M | HT_CAP_INFO_SHORT_GI_40M)) == 0) {
                        prAdapter->rWifiVar.rConnSettings.fgRxShortGIDisabled = TRUE;
                    }
                    else {
                        prAdapter->rWifiVar.rConnSettings.fgRxShortGIDisabled = FALSE;
                    }

                    /* ucAmpduParam */
                    DBGLOG(P2P, TRACE, ("AMPDU setting from supplicant:0x%x, & default value:0x%x\n", (UINT_8)HT_CAP_IE(pucIE)->ucAmpduParam, (UINT_8)AMPDU_PARAM_DEFAULT_VAL));

                    /* rSupMcsSet */
                    /* Can do nothing. the field is default value from other configuration. */
                    //HT_CAP_IE(pucIE)->rSupMcsSet;

                    /* u2HtExtendedCap */
                    ASSERT(HT_CAP_IE(pucIE)->u2HtExtendedCap == (HT_EXT_CAP_DEFAULT_VAL & ~(HT_EXT_CAP_PCO | HT_EXT_CAP_PCO_TRANS_TIME_NONE)));

                    /* u4TxBeamformingCap */
                    ASSERT(HT_CAP_IE(pucIE)->u4TxBeamformingCap == TX_BEAMFORMING_CAP_DEFAULT_VAL);

                    /* ucAselCap */
                    ASSERT(HT_CAP_IE(pucIE)->ucAselCap == ASEL_CAP_DEFAULT_VAL);
#endif
                }
                break;
            case ELEM_ID_RSN: /* 48 */  /* V */
				{
					RSN_INFO_T rRsnIe;

                DBGLOG(P2P, TRACE, ("RSN IE\n"));
                kalP2PSetCipher(prAdapter->prGlueInfo, IW_AUTH_CIPHER_CCMP);
                ucNewSecMode = TRUE;

					if (rsnParseRsnIE(prAdapter, RSN_IE(pucIE), &rRsnIe)) {
						prP2pBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX];
					    prP2pBssInfo->u4RsnSelectedGroupCipher = RSN_CIPHER_SUITE_CCMP;
					    prP2pBssInfo->u4RsnSelectedPairwiseCipher = RSN_CIPHER_SUITE_CCMP;
					    prP2pBssInfo->u4RsnSelectedAKMSuite = RSN_AKM_SUITE_PSK;
						prP2pBssInfo->u2RsnSelectedCapInfo = rRsnIe.u2RsnCap;
					}
            	}
                break;
            case ELEM_ID_EXTENDED_SUP_RATES:   /* 50 */   /* V */
                /* Be attention, ELEM_ID_SUP_RATES should be placed before ELEM_ID_EXTENDED_SUP_RATES. */
                DBGLOG(P2P, TRACE, ("Ex Support Rate IE\n"));
                kalMemCopy(&(prP2pBssInfo->aucAllSupportedRates[prP2pBssInfo->ucAllSupportedRatesLen]),
                                    EXT_SUP_RATES_IE(pucIE)->aucExtSupportedRates,
                                    EXT_SUP_RATES_IE(pucIE)->ucLength);

                DBGLOG_MEM8(P2P, TRACE, EXT_SUP_RATES_IE(pucIE)->aucExtSupportedRates, EXT_SUP_RATES_IE(pucIE)->ucLength);

                prP2pBssInfo->ucAllSupportedRatesLen += EXT_SUP_RATES_IE(pucIE)->ucLength;
                break;
            case ELEM_ID_HT_OP: /* 61 */        /* V */   // TODO:
                {
#if 1
                    DBGLOG(P2P, TRACE, ("HT OP IE would be overwritten by driver\n"));

                    DBGLOG(P2P, TRACE, ("    Primary Channel: %x, Info1: %x, Info2: %x, Info3: %x\n",
                                                                        HT_OP_IE(pucIE)->ucPrimaryChannel,
                                                                        HT_OP_IE(pucIE)->ucInfo1,
                                                                        HT_OP_IE(pucIE)->u2Info2,
                                                                        HT_OP_IE(pucIE)->u2Info3));
#else
                    UINT_16 u2Info2 = 0;
                    prP2pBssInfo->ucPhyTypeSet |= PHY_TYPE_SET_802_11N;

                    DBGLOG(P2P, TRACE, ("HT OP IE\n"));

                    /* ucPrimaryChannel. */
                    ASSERT(HT_OP_IE(pucIE)->ucPrimaryChannel == prP2pBssInfo->ucPrimaryChannel);

                    /* ucInfo1 */
                    prP2pBssInfo->ucHtOpInfo1 = HT_OP_IE(pucIE)->ucInfo1;

                    /* u2Info2 */
                    u2Info2 = HT_OP_IE(pucIE)->u2Info2;

                    if (u2Info2 & HT_OP_INFO2_NON_GF_HT_STA_PRESENT) {
                        ASSERT(prP2pBssInfo->eGfOperationMode != GF_MODE_NORMAL);
                        u2Info2 &= ~HT_OP_INFO2_NON_GF_HT_STA_PRESENT;
                    }

                    if (u2Info2 & HT_OP_INFO2_OBSS_NON_HT_STA_PRESENT) {
                        prP2pBssInfo->eObssHtProtectMode = HT_PROTECT_MODE_NON_MEMBER;
                        u2Info2 &= ~HT_OP_INFO2_OBSS_NON_HT_STA_PRESENT;
                    }

                    switch (u2Info2 & HT_OP_INFO2_HT_PROTECTION) {
                    case HT_PROTECT_MODE_NON_HT:
                        prP2pBssInfo->eHtProtectMode = HT_PROTECT_MODE_NON_HT;
                        break;
                    case HT_PROTECT_MODE_NON_MEMBER:
                        prP2pBssInfo->eHtProtectMode = HT_PROTECT_MODE_NONE;
                        prP2pBssInfo->eObssHtProtectMode = HT_PROTECT_MODE_NON_MEMBER;
                        break;
                    default:
                        prP2pBssInfo->eHtProtectMode = HT_OP_IE(pucIE)->u2Info2;
                        break;
                    }

                    /* u2Info3 */
                    prP2pBssInfo->u2HtOpInfo3 = HT_OP_IE(pucIE)->u2Info3;

                    /* aucBasicMcsSet */
                    DBGLOG_MEM8(P2P, TRACE, HT_OP_IE(pucIE)->aucBasicMcsSet, 16);
#endif
                }
                break;
            case ELEM_ID_OBSS_SCAN_PARAMS: /* 74 */     /* V */
                {
                    DBGLOG(P2P, TRACE, ("ELEM_ID_OBSS_SCAN_PARAMS IE would be replaced by driver\n"));
                }
                break;
            case ELEM_ID_EXTENDED_CAP: /* 127 */  /* V */
                {
                    DBGLOG(P2P, TRACE, ("ELEM_ID_EXTENDED_CAP IE would be replaced by driver\n"));
                }
                break;
            case ELEM_ID_VENDOR: /* 221 */ /* V */
                DBGLOG(P2P, TRACE, ("Vender Specific IE\n"));
                {
                    UINT_8 ucOuiType;
                    UINT_16 u2SubTypeVersion;
                    if (rsnParseCheckForWFAInfoElem(prAdapter, pucIE, &ucOuiType, &u2SubTypeVersion)) {
                        if ((ucOuiType == VENDOR_OUI_TYPE_WPA) &&
                                (u2SubTypeVersion == VERSION_WPA)) {
                            kalP2PSetCipher(prAdapter->prGlueInfo, IW_AUTH_CIPHER_TKIP);
                            ucNewSecMode = TRUE;
							kalMemCopy(prP2pSpecificBssInfo->aucWpaIeBuffer,pucIE,
                                            IE_SIZE(pucIE));
							prP2pSpecificBssInfo->u2WpaIeLen=IE_SIZE(pucIE);
                        }
                        else if ((ucOuiType == VENDOR_OUI_TYPE_WPS)) {
                            kalP2PUpdateWSC_IE(prAdapter->prGlueInfo, 0, pucIE, IE_SIZE(pucIE));
                        }

                        // WMM here.
                    }
                    else if (p2pFuncParseCheckForP2PInfoElem(prAdapter, pucIE, &ucOuiType)) {
                        // TODO Store the whole P2P IE & generate later.
                        // Be aware that there may be one or more P2P IE.
                        if (ucOuiType == VENDOR_OUI_TYPE_P2P) {
                            kalMemCopy(&prP2pSpecificBssInfo->aucAttributesCache[prP2pSpecificBssInfo->u2AttributeLen],
                                            pucIE,
                                            IE_SIZE(pucIE));

                            prP2pSpecificBssInfo->u2AttributeLen += IE_SIZE(pucIE);
                        }
                       else if(ucOuiType == VENDOR_OUI_TYPE_WFD) {
							
			    kalMemCopy(&prP2pSpecificBssInfo->aucAttributesCache[prP2pSpecificBssInfo->u2AttributeLen],
                                            pucIE,
                                            IE_SIZE(pucIE));

                            prP2pSpecificBssInfo->u2AttributeLen += IE_SIZE(pucIE);
							}
                    }
                    else {

                        kalMemCopy(&prP2pSpecificBssInfo->aucAttributesCache[prP2pSpecificBssInfo->u2AttributeLen],
                                            pucIE,
                                            IE_SIZE(pucIE));

                        prP2pSpecificBssInfo->u2AttributeLen += IE_SIZE(pucIE);
                        DBGLOG(P2P, TRACE, ("Driver unprocessed Vender Specific IE\n"));
                        ASSERT(FALSE);
                    }

                    // TODO: Store other Vender IE except for WMM Param.
                }
                break;
            default:
                DBGLOG(P2P, TRACE, ("Unprocessed element ID:%d \n", IE_ID(pucIE)));
                break;
            }
        }

        if (!ucNewSecMode && ucOldSecMode)
            kalP2PSetCipher(prAdapter->prGlueInfo, IW_AUTH_CIPHER_NONE);

    } while (FALSE);

    return;
} /* p2pFuncParseBeaconContent */




P_BSS_DESC_T
p2pFuncKeepOnConnection (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_CONNECTION_REQ_INFO_T prConnReqInfo,
    IN P_P2P_CHNL_REQ_INFO_T prChnlReqInfo,
    IN P_P2P_SCAN_REQ_INFO_T prScanReqInfo
    )
{
    P_BSS_DESC_T prTargetBss = (P_BSS_DESC_T)NULL;
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                (prConnReqInfo != NULL) &&
                (prChnlReqInfo != NULL) &&
                (prScanReqInfo != NULL));

        prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);

        if (prP2pBssInfo->eCurrentOPMode != OP_MODE_INFRASTRUCTURE) {
            break;
        }

        // Update connection request information.
        ASSERT(prConnReqInfo->fgIsConnRequest == TRUE);

        /* Find BSS Descriptor first. */
        prTargetBss =  scanP2pSearchDesc(prAdapter,
                                                                        prP2pBssInfo,
                                                                        prConnReqInfo);

        if (prTargetBss == NULL) {
            /* Update scan parameter... to scan target device. */
            prScanReqInfo->ucNumChannelList = 1;
            prScanReqInfo->eScanType = SCAN_TYPE_ACTIVE_SCAN;
            prScanReqInfo->eChannelSet = SCAN_CHANNEL_FULL;
            prScanReqInfo->u4BufLength = 0;  /* Prevent other P2P ID in IE. */
            prScanReqInfo->fgIsAbort = TRUE;
        }
        else {
            prChnlReqInfo->u8Cookie = 0;
            prChnlReqInfo->ucReqChnlNum = prTargetBss->ucChannelNum;
            prChnlReqInfo->eBand = prTargetBss->eBand;
            prChnlReqInfo->eChnlSco = prTargetBss->eSco;
            prChnlReqInfo->u4MaxInterval = AIS_JOIN_CH_REQUEST_INTERVAL;
            prChnlReqInfo->eChannelReqType = CHANNEL_REQ_TYPE_GC_JOIN_REQ;
        }

    } while (FALSE);

    return prTargetBss;
} /* p2pFuncKeepOnConnection */

/* Currently Only for ASSOC Response Frame. */
VOID
p2pFuncStoreAssocRspIEBuffer (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    )
{
    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;
    P_P2P_JOIN_INFO_T prJoinInfo = (P_P2P_JOIN_INFO_T)NULL;
    P_WLAN_ASSOC_RSP_FRAME_T prAssocRspFrame = (P_WLAN_ASSOC_RSP_FRAME_T)NULL;
    INT_16 i2IELen = 0;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prSwRfb != NULL));

        prAssocRspFrame = (P_WLAN_ASSOC_RSP_FRAME_T)prSwRfb->pvHeader;

        if (prAssocRspFrame->u2FrameCtrl != MAC_FRAME_ASSOC_RSP) {
            break;
        }

        i2IELen = prSwRfb->u2PacketLen - (WLAN_MAC_HEADER_LEN +
                                                                    CAP_INFO_FIELD_LEN +
                                                                    STATUS_CODE_FIELD_LEN +
                                                                    AID_FIELD_LEN);


        if (i2IELen <= 0) {
            break;
        }

        prP2pFsmInfo = prAdapter->rWifiVar.prP2pFsmInfo;
        prJoinInfo = &(prP2pFsmInfo->rJoinInfo);
        prJoinInfo->u4BufLength = (UINT_32)i2IELen;

        kalMemCopy(prJoinInfo->aucIEBuf, prAssocRspFrame->aucInfoElem, prJoinInfo->u4BufLength);

    } while (FALSE);


    return;
} /* p2pFuncStoreAssocRspIEBuffer */




/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set Packet Filter.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_NOT_SUPPORTED
* \retval WLAN_STATUS_ADAPTER_NOT_READY
*/
/*----------------------------------------------------------------------------*/
VOID
p2pFuncMgmtFrameRegister (
    IN P_ADAPTER_T  prAdapter,
    IN  UINT_16 u2FrameType,
    IN BOOLEAN fgIsRegistered,
    OUT PUINT_32 pu4P2pPacketFilter
    )
{
    UINT_32 u4NewPacketFilter = 0;

    DEBUGFUNC("p2pFuncMgmtFrameRegister");

    do {
        ASSERT_BREAK(prAdapter != NULL);

        if (pu4P2pPacketFilter) {
            u4NewPacketFilter = *pu4P2pPacketFilter;
        }

        switch (u2FrameType) {
        case MAC_FRAME_PROBE_REQ:
            if (fgIsRegistered) {
                u4NewPacketFilter |= PARAM_PACKET_FILTER_PROBE_REQ;
                DBGLOG(P2P, TRACE, ("Open packet filer probe request\n"));
            }
            else {
                u4NewPacketFilter &= ~PARAM_PACKET_FILTER_PROBE_REQ;
                DBGLOG(P2P, TRACE, ("Close packet filer probe request\n"));
            }
            break;
        case MAC_FRAME_ACTION:
            if (fgIsRegistered) {
                u4NewPacketFilter |= PARAM_PACKET_FILTER_ACTION_FRAME;
                DBGLOG(P2P, TRACE, ("Open packet filer action frame.\n"));
            }
            else {
                u4NewPacketFilter &= ~PARAM_PACKET_FILTER_ACTION_FRAME;
                DBGLOG(P2P, TRACE, ("Close packet filer action frame.\n"));
            }
            break;
        default:
            DBGLOG(P2P, TRACE, ("Ask frog to add code for mgmt:%x\n", u2FrameType));
            break;
        }

        if (pu4P2pPacketFilter) {
            *pu4P2pPacketFilter = u4NewPacketFilter;
        }

//        u4NewPacketFilter |= prAdapter->u4OsPacketFilter;

        prAdapter->u4OsPacketFilter &= ~PARAM_PACKET_FILTER_P2P_MASK;
        prAdapter->u4OsPacketFilter |= u4NewPacketFilter;

        DBGLOG(P2P, TRACE, ("P2P Set PACKET filter:0x%lx\n", prAdapter->u4OsPacketFilter));

        wlanSendSetQueryCmd(prAdapter,
                CMD_ID_SET_RX_FILTER,
                TRUE,
                FALSE,
                FALSE,
                nicCmdEventSetCommon,
                nicOidCmdTimeoutCommon,
                sizeof(UINT_32),
                (PUINT_8)&prAdapter->u4OsPacketFilter,
                &u4NewPacketFilter,
                sizeof(u4NewPacketFilter)
                );

    } while (FALSE);

    return;
}   /* p2pFuncMgmtFrameRegister */


VOID
p2pFuncUpdateMgmtFrameRegister (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4OsFilter
    )
{

    do {

        prAdapter->rWifiVar.prP2pFsmInfo->u4P2pPacketFilter = u4OsFilter;

        if ((prAdapter->u4OsPacketFilter & PARAM_PACKET_FILTER_P2P_MASK) ^ u4OsFilter) {

            prAdapter->u4OsPacketFilter &= ~PARAM_PACKET_FILTER_P2P_MASK;

            prAdapter->u4OsPacketFilter |= (u4OsFilter & PARAM_PACKET_FILTER_P2P_MASK);
            
            wlanSendSetQueryCmd(prAdapter,
                    CMD_ID_SET_RX_FILTER,
                    TRUE,
                    FALSE,
                    FALSE,
                    nicCmdEventSetCommon,
                    nicOidCmdTimeoutCommon,
                    sizeof(UINT_32),
                    (PUINT_8)&prAdapter->u4OsPacketFilter,
                    &u4OsFilter,
                    sizeof(u4OsFilter)
                    );
            DBGLOG(P2P, TRACE, ("P2P Set PACKET filter:0x%lx\n", prAdapter->u4OsPacketFilter));
        }
        
    } while (FALSE);

    

    
    return;
} /* p2pFuncUpdateMgmtFrameRegister */


VOID
p2pFuncGetStationInfo (
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucMacAddr,
    OUT P_P2P_STATION_INFO_T prStaInfo
    )
{

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                                    (pucMacAddr != NULL) &&
                                    (prStaInfo != NULL));

        prStaInfo->u4InactiveTime = 0;
        prStaInfo->u4RxBytes = 0;
        prStaInfo->u4TxBytes = 0;
        prStaInfo->u4RxPackets = 0;
        prStaInfo->u4TxPackets = 0;
        // TODO:

    } while (FALSE);

    return;
} /* p2pFuncGetStationInfo */


BOOLEAN
p2pFuncGetAttriList (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucOuiType,
    IN PUINT_8 pucIE,
    IN UINT_16 u2IELength,
    OUT PPUINT_8 ppucAttriList,
    OUT PUINT_16 pu2AttriListLen
    )
{
    BOOLEAN fgIsAllocMem = FALSE;
    UINT_8 aucWfaOui[] = VENDOR_OUI_WFA_SPECIFIC;
    UINT_16 u2Offset = 0;
    P_IE_P2P_T prIe = (P_IE_P2P_T)NULL;
    PUINT_8 pucAttriListStart = (PUINT_8)NULL;
    UINT_16 u2AttriListLen = 0, u2BufferSize = 0;
    BOOLEAN fgBackupAttributes = FALSE;
    
    do {
        ASSERT_BREAK((prAdapter != NULL) && 
                        (pucIE != NULL) &&
                        (u2IELength != 0) &&
                        (ppucAttriList != NULL) &&
                        (pu2AttriListLen != NULL));

        if(ppucAttriList) {
            *ppucAttriList = NULL;
        }
        if(pu2AttriListLen) {
            *pu2AttriListLen = 0;
        }

        if (ucOuiType == VENDOR_OUI_TYPE_WPS){
            aucWfaOui[0] = 0x00;
            aucWfaOui[1] = 0x50;
            aucWfaOui[2] = 0xF2;
        }
        else if ((ucOuiType != VENDOR_OUI_TYPE_P2P) 
#if CFG_SUPPORT_WFD
                && (ucOuiType != VENDOR_OUI_TYPE_WFD)
#endif
                ) {
            DBGLOG(P2P, INFO, ("Not supported OUI Type to parsing 0x%x\n", ucOuiType));
            break;
        }


        IE_FOR_EACH(pucIE, u2IELength, u2Offset) {
            if (ELEM_ID_VENDOR == IE_ID(pucIE)) {
                prIe = (P_IE_P2P_T)pucIE;

                if (prIe->ucLength <= P2P_OUI_TYPE_LEN) {
                    continue;
                    
                }

                if ((prIe->aucOui[0] == aucWfaOui[0]) &&
                        (prIe->aucOui[1] == aucWfaOui[1]) &&
                        (prIe->aucOui[2] == aucWfaOui[2]) &&
                        (ucOuiType == prIe->ucOuiType)) {

                    if (!pucAttriListStart) {
                        pucAttriListStart = &prIe->aucP2PAttributes[0];
                        if (prIe->ucLength > P2P_OUI_TYPE_LEN) {
                            u2AttriListLen = (UINT_16)(prIe->ucLength - P2P_OUI_TYPE_LEN);
                        }
                        else {
                            ASSERT(FALSE);
                        }
                    }
                    else {
/* More than 2 attributes. */
                        UINT_16 u2CopyLen;

                        if (FALSE == fgBackupAttributes) {
                            P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo = prAdapter->rWifiVar.prP2pSpecificBssInfo;;
                            
                            fgBackupAttributes = TRUE;
                            if (ucOuiType == VENDOR_OUI_TYPE_P2P) {
                                kalMemCopy(&prP2pSpecificBssInfo->aucAttributesCache[0],
                                       pucAttriListStart,
                                       u2AttriListLen);

                                pucAttriListStart = &prP2pSpecificBssInfo->aucAttributesCache[0];

                                u2BufferSize = P2P_MAXIMUM_ATTRIBUTE_LEN;
                            }
                            else if (ucOuiType == VENDOR_OUI_TYPE_WPS) {
                                kalMemCopy(&prP2pSpecificBssInfo->aucWscAttributesCache[0],
                                       pucAttriListStart,
                                       u2AttriListLen);
                                pucAttriListStart = &prP2pSpecificBssInfo->aucWscAttributesCache[0];

                                u2BufferSize = WPS_MAXIMUM_ATTRIBUTES_CACHE_SIZE;
                            }
#if CFG_SUPPORT_WFD
                            else if (ucOuiType == VENDOR_OUI_TYPE_WFD) {
                                PUINT_8 pucTmpBuf = (PUINT_8)NULL;
                                pucTmpBuf = (PUINT_8)kalMemAlloc(WPS_MAXIMUM_ATTRIBUTES_CACHE_SIZE, VIR_MEM_TYPE);

                                if (pucTmpBuf != NULL) {
                                    fgIsAllocMem = TRUE;
                                }
                                else {
                                    /* Can't alloca memory for WFD IE relocate. */
                                    ASSERT(FALSE);
                                    break;
                                }

                                kalMemCopy(pucTmpBuf,
                                       pucAttriListStart,
                                       u2AttriListLen);
                                
                                pucAttriListStart = pucTmpBuf;

                                u2BufferSize = WPS_MAXIMUM_ATTRIBUTES_CACHE_SIZE;
                            }
#endif
                            else {
                                fgBackupAttributes = FALSE;
                            }
                        }

                        u2CopyLen = (UINT_16)(prIe->ucLength - P2P_OUI_TYPE_LEN);

                        if ((u2AttriListLen + u2CopyLen) > u2BufferSize) {
                        
                            u2CopyLen = u2BufferSize - u2AttriListLen;
                        
                            DBGLOG(P2P, WARN, ("Length of received P2P attributes > maximum cache size.\n"));

                        }
                        
                        if (u2CopyLen) {
                            kalMemCopy((PUINT_8)((UINT_32)pucAttriListStart + (UINT_32)u2AttriListLen), 
                                        &prIe->aucP2PAttributes[0],
                                        u2CopyLen);
                            
                            u2AttriListLen += u2CopyLen;
                        }
                        
                    
                    }
                } /* prIe->aucOui */
            } /* ELEM_ID_VENDOR */
        } /* IE_FOR_EACH */

       
    } while (FALSE);

    if (pucAttriListStart) {
        PUINT_8 pucAttribute = pucAttriListStart;
        DBGLOG(P2P, LOUD, ("Checking Attribute Length.\n"));
        if (ucOuiType == VENDOR_OUI_TYPE_P2P) {
            P2P_ATTRI_FOR_EACH(pucAttribute, u2AttriListLen, u2Offset);
        }
        else if (ucOuiType == VENDOR_OUI_TYPE_WFD) {
        }
        else if (ucOuiType == VENDOR_OUI_TYPE_WPS) {
            /* Big Endian: WSC, WFD. */
            WSC_ATTRI_FOR_EACH(pucAttribute, u2AttriListLen, u2Offset) {
            DBGLOG(P2P, LOUD, ("Attribute ID:%d, Length:%d.\n",
                                    WSC_ATTRI_ID(pucAttribute),
                                    WSC_ATTRI_LEN(pucAttribute)));
            }
        }
        else {
        }

        ASSERT(u2Offset == u2AttriListLen);
    
        *ppucAttriList = pucAttriListStart;
        *pu2AttriListLen = u2AttriListLen;
    
    }
    else {
        *ppucAttriList = (PUINT_8)NULL;
        *pu2AttriListLen = 0;
    }

    return fgIsAllocMem;
} /* p2pFuncGetAttriList */


P_MSDU_INFO_T
p2pFuncProcessP2pProbeRsp (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMgmtTxMsdu
    )
{
    P_MSDU_INFO_T prRetMsduInfo = prMgmtTxMsdu;
    P_WLAN_PROBE_RSP_FRAME_T prProbeRspFrame = (P_WLAN_PROBE_RSP_FRAME_T)NULL;
    PUINT_8 pucIEBuf = (PUINT_8)NULL;
    UINT_16 u2Offset = 0, u2IELength = 0, u2ProbeRspHdrLen = 0;
    BOOLEAN fgIsP2PIE = FALSE, fgIsWSCIE = FALSE;
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;
    UINT_16 u2EstimateSize = 0, u2EstimatedExtraIELen = 0;
    UINT_32 u4IeArraySize = 0, u4Idx = 0;


    do {
        ASSERT_BREAK((prAdapter != NULL) && (prMgmtTxMsdu != NULL));

        prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);

        //3 Make sure this is probe response frame.
        prProbeRspFrame = (P_WLAN_PROBE_RSP_FRAME_T)((UINT_32)prMgmtTxMsdu->prPacket + MAC_TX_RESERVED_FIELD);
        ASSERT_BREAK((prProbeRspFrame->u2FrameCtrl & MASK_FRAME_TYPE) == MAC_FRAME_PROBE_RSP);

        //3 Get the importent P2P IE.
        u2ProbeRspHdrLen = (WLAN_MAC_MGMT_HEADER_LEN + TIMESTAMP_FIELD_LEN + BEACON_INTERVAL_FIELD_LEN + CAP_INFO_FIELD_LEN);
        pucIEBuf = prProbeRspFrame->aucInfoElem;
        u2IELength = prMgmtTxMsdu->u2FrameLength - u2ProbeRspHdrLen;

#if CFG_SUPPORT_WFD
        prAdapter->prGlueInfo->prP2PInfo->u2VenderIELen = 0;
#endif

        IE_FOR_EACH(pucIEBuf, u2IELength, u2Offset) {
            switch (IE_ID(pucIEBuf)) {
            case ELEM_ID_SSID:
                {

                    COPY_SSID(prP2pBssInfo->aucSSID,
                                    prP2pBssInfo->ucSSIDLen,
                                    SSID_IE(pucIEBuf)->aucSSID,
                                    SSID_IE(pucIEBuf)->ucLength);
                }
                break;
            case ELEM_ID_VENDOR:
                {
                    UINT_8 ucOuiType = 0;
                    UINT_16 u2SubTypeVersion = 0;
#if! CFG_SUPPORT_WFD

                    
                    if (rsnParseCheckForWFAInfoElem(prAdapter, pucIEBuf, &ucOuiType, &u2SubTypeVersion)) {
                        if (ucOuiType == VENDOR_OUI_TYPE_WPS) {
                            kalP2PUpdateWSC_IE(prAdapter->prGlueInfo, 2, pucIEBuf, IE_SIZE(pucIEBuf));
                            fgIsWSCIE = TRUE;
                        }

                    }

                    else if (p2pFuncParseCheckForP2PInfoElem(prAdapter, pucIEBuf, &ucOuiType)) {
                        if (ucOuiType == VENDOR_OUI_TYPE_P2P) {
                            //2 Note(frog): I use WSC IE buffer for Probe Request to store the P2P IE for Probe Response.
                            kalP2PUpdateWSC_IE(prAdapter->prGlueInfo, 1, pucIEBuf, IE_SIZE(pucIEBuf));
                            fgIsP2PIE = TRUE;
                        }

                    }
					


                    else {
                        if((prAdapter->prGlueInfo->prP2PInfo->u2VenderIELen+IE_SIZE(pucIEBuf))<512) {
                            kalMemCopy(prAdapter->prGlueInfo->prP2PInfo->aucVenderIE, pucIEBuf, IE_SIZE(pucIEBuf));
                            prAdapter->prGlueInfo->prP2PInfo->u2VenderIELen += IE_SIZE(pucIEBuf);
                        }
                    }
#else
                    /* Eddie May be WFD */
                   if (rsnParseCheckForWFAInfoElem(prAdapter, pucIEBuf, &ucOuiType, &u2SubTypeVersion)) {
				if(ucOuiType == VENDOR_OUI_TYPE_WMM) {
		                                break;
                                 }
					
			}
		            if((prAdapter->prGlueInfo->prP2PInfo->u2VenderIELen+IE_SIZE(pucIEBuf))<1024) {
		                        kalMemCopy(prAdapter->prGlueInfo->prP2PInfo->aucVenderIE + prAdapter->prGlueInfo->prP2PInfo->u2VenderIELen
		                                , pucIEBuf, IE_SIZE(pucIEBuf));
		                        prAdapter->prGlueInfo->prP2PInfo->u2VenderIELen += IE_SIZE(pucIEBuf);
                    		}
#endif


                }
                break;
            default:
                break;
            }

        }


        //3 Check the total size & current frame.
        u2EstimateSize = WLAN_MAC_MGMT_HEADER_LEN + \
                        TIMESTAMP_FIELD_LEN + \
                        BEACON_INTERVAL_FIELD_LEN + \
                        CAP_INFO_FIELD_LEN + \
                        (ELEM_HDR_LEN + ELEM_MAX_LEN_SSID) + \
                        (ELEM_HDR_LEN + ELEM_MAX_LEN_SUP_RATES) + \
                        (ELEM_HDR_LEN + ELEM_MAX_LEN_DS_PARAMETER_SET);

        u2EstimatedExtraIELen = 0;

        u4IeArraySize = sizeof(txProbeRspIETable)/sizeof(APPEND_VAR_IE_ENTRY_T);
        for (u4Idx = 0; u4Idx < u4IeArraySize; u4Idx++) {
            if (txProbeRspIETable[u4Idx].u2EstimatedFixedIELen) {
                u2EstimatedExtraIELen += txProbeRspIETable[u4Idx].u2EstimatedFixedIELen;
            }

            else {
                ASSERT(txProbeRspIETable[u4Idx].pfnCalculateVariableIELen);

                u2EstimatedExtraIELen += (UINT_16)(txProbeRspIETable[u4Idx].pfnCalculateVariableIELen(prAdapter,
                                                                                                                                NETWORK_TYPE_P2P_INDEX,
                                                                                                                                NULL));
            }

        }


        if (fgIsWSCIE) {
            u2EstimatedExtraIELen += kalP2PCalWSC_IELen(prAdapter->prGlueInfo, 2);
        }

        if (fgIsP2PIE) {
            u2EstimatedExtraIELen += kalP2PCalWSC_IELen(prAdapter->prGlueInfo, 1);
        }

#if CFG_SUPPORT_WFD
         u2EstimatedExtraIELen += prAdapter->prGlueInfo->prP2PInfo->u2VenderIELen;
#endif   
      


        if ((u2EstimateSize += u2EstimatedExtraIELen) > (prRetMsduInfo->u2FrameLength)) {
            prRetMsduInfo = cnmMgtPktAlloc(prAdapter, u2EstimateSize);

            if (prRetMsduInfo == NULL) {
                DBGLOG(P2P, WARN, ("No packet for sending new probe response, use original one\n"));
                prRetMsduInfo = prMgmtTxMsdu;
                break;
            }


            prRetMsduInfo->ucNetworkType = NETWORK_TYPE_P2P_INDEX;
        }


        //3 Compose / Re-compose probe response frame.
        bssComposeBeaconProbeRespFrameHeaderAndFF(
                                                        (PUINT_8)((UINT_32)(prRetMsduInfo->prPacket) + MAC_TX_RESERVED_FIELD),
                                                        prProbeRspFrame->aucDestAddr,
                                                        prProbeRspFrame->aucSrcAddr,
                                                        prProbeRspFrame->aucBSSID,
                                                        prProbeRspFrame->u2BeaconInterval,
                                                        prProbeRspFrame->u2CapInfo);

        prRetMsduInfo->u2FrameLength = (WLAN_MAC_MGMT_HEADER_LEN + TIMESTAMP_FIELD_LEN + BEACON_INTERVAL_FIELD_LEN + CAP_INFO_FIELD_LEN);

        bssBuildBeaconProbeRespFrameCommonIEs(prRetMsduInfo,
                                                prP2pBssInfo,
                                                prProbeRspFrame->aucDestAddr);


        for (u4Idx = 0; u4Idx < u4IeArraySize; u4Idx++) {
            if (txProbeRspIETable[u4Idx].pfnAppendIE) {
                txProbeRspIETable[u4Idx].pfnAppendIE(prAdapter, prRetMsduInfo);
            }

        }


        if (fgIsWSCIE) {
            kalP2PGenWSC_IE(prAdapter->prGlueInfo,
                                2,
                                (PUINT_8)((UINT_32)prRetMsduInfo->prPacket + (UINT_32)prRetMsduInfo->u2FrameLength));

            prRetMsduInfo->u2FrameLength += (UINT_16)kalP2PCalWSC_IELen(prAdapter->prGlueInfo, 2);
        }

        if (fgIsP2PIE) {
            kalP2PGenWSC_IE(prAdapter->prGlueInfo,
                                1,
                                (PUINT_8)((UINT_32)prRetMsduInfo->prPacket + (UINT_32)prRetMsduInfo->u2FrameLength));

            prRetMsduInfo->u2FrameLength += (UINT_16)kalP2PCalWSC_IELen(prAdapter->prGlueInfo, 1);
        }

#if CFG_SUPPORT_WFD
        if(prAdapter->prGlueInfo->prP2PInfo->u2VenderIELen>0) {
            kalMemCopy((PUINT_8)((UINT_32)prRetMsduInfo->prPacket + (UINT_32)prRetMsduInfo->u2FrameLength), 
                    prAdapter->prGlueInfo->prP2PInfo->aucVenderIE, prAdapter->prGlueInfo->prP2PInfo->u2VenderIELen);
            prRetMsduInfo->u2FrameLength += (UINT_16) prAdapter->prGlueInfo->prP2PInfo->u2VenderIELen;
        }
#endif


    } while (FALSE);

    if (prRetMsduInfo != prMgmtTxMsdu) {
        cnmMgtPktFree(prAdapter, prMgmtTxMsdu);
    }


    return prRetMsduInfo;
} /* p2pFuncProcessP2pProbeRsp */


#if 0 //LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
UINT_32
p2pFuncCalculateExtra_IELenForBeacon (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    )
{

    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpeBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    UINT_32 u4IELen = 0;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (eNetTypeIndex == NETWORK_TYPE_P2P_INDEX));

        if (p2pFuncIsAPMode(prAdapter->rWifiVar.prP2pFsmInfo)) {
            break;
        }

        prP2pSpeBssInfo = prAdapter->rWifiVar.prP2pSpecificBssInfo;

        u4IELen = prP2pSpeBssInfo->u2IELenForBCN;

    } while (FALSE);

    return u4IELen;
} /* p2pFuncCalculateP2p_IELenForBeacon */

VOID
p2pFuncGenerateExtra_IEForBeacon (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    )
{
    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpeBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    PUINT_8 pucIEBuf = (PUINT_8)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prMsduInfo != NULL));

        prP2pSpeBssInfo = prAdapter->rWifiVar.prP2pSpecificBssInfo;

        if (p2pFuncIsAPMode(prAdapter->rWifiVar.prP2pFsmInfo)) {

            break;
        }

        pucIEBuf = (PUINT_8)((UINT_32)prMsduInfo->prPacket + (UINT_32)prMsduInfo->u2FrameLength);

        kalMemCopy(pucIEBuf, prP2pSpeBssInfo->aucBeaconIECache, prP2pSpeBssInfo->u2IELenForBCN);

        prMsduInfo->u2FrameLength += prP2pSpeBssInfo->u2IELenForBCN;

    } while (FALSE);

    return;
} /* p2pFuncGenerateExtra_IEForBeacon */


#else
UINT_32
p2pFuncCalculateP2p_IELenForBeacon (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    )
{
    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpeBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    UINT_32 u4IELen = 0;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (eNetTypeIndex == NETWORK_TYPE_P2P_INDEX));

        if (!prAdapter->fgIsP2PRegistered) {
            break;
        }


        if (p2pFuncIsAPMode(prAdapter->rWifiVar.prP2pFsmInfo)) {
            break;
        }

        prP2pSpeBssInfo = prAdapter->rWifiVar.prP2pSpecificBssInfo;

        u4IELen = prP2pSpeBssInfo->u2AttributeLen;

    } while (FALSE);

    return u4IELen;
} /* p2pFuncCalculateP2p_IELenForBeacon */


VOID
p2pFuncGenerateP2p_IEForBeacon (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    )
{
    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpeBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    PUINT_8 pucIEBuf = (PUINT_8)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prMsduInfo != NULL));

        if (!prAdapter->fgIsP2PRegistered) {
            break;
        }

        prP2pSpeBssInfo = prAdapter->rWifiVar.prP2pSpecificBssInfo;

        if (p2pFuncIsAPMode(prAdapter->rWifiVar.prP2pFsmInfo)) {

            break;
        }

        pucIEBuf = (PUINT_8)((UINT_32)prMsduInfo->prPacket + (UINT_32)prMsduInfo->u2FrameLength);

        kalMemCopy(pucIEBuf, prP2pSpeBssInfo->aucAttributesCache, prP2pSpeBssInfo->u2AttributeLen);

        prMsduInfo->u2FrameLength += prP2pSpeBssInfo->u2AttributeLen;

    } while (FALSE);

    return;
} /* p2pFuncGenerateP2p_IEForBeacon */





UINT_32
p2pFuncCalculateWSC_IELenForBeacon (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    )
{
    if (eNetTypeIndex != NETWORK_TYPE_P2P_INDEX) {
        return 0;
    }

    return kalP2PCalWSC_IELen(prAdapter->prGlueInfo, 0);
} /* p2pFuncCalculateP2p_IELenForBeacon */


VOID
p2pFuncGenerateWSC_IEForBeacon (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    )
{
    PUINT_8               pucBuffer;
    UINT_16               u2IELen = 0;
    ASSERT(prAdapter);
    ASSERT(prMsduInfo);

    if (prMsduInfo->ucNetworkType != NETWORK_TYPE_P2P_INDEX) {
        return;
    }

    u2IELen = (UINT_16)kalP2PCalWSC_IELen(prAdapter->prGlueInfo, 0);

    pucBuffer = (PUINT_8)((UINT_32)prMsduInfo->prPacket +
                          (UINT_32)prMsduInfo->u2FrameLength);

    ASSERT(pucBuffer);

    // TODO: Check P2P FSM State.
    kalP2PGenWSC_IE(prAdapter->prGlueInfo,
                   0,
                   pucBuffer);

    prMsduInfo->u2FrameLength += u2IELen;

    return;
} /* p2pFuncGenerateP2p_IEForBeacon */

#endif
/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to calculate P2P IE length for Beacon frame.
*
* @param[in] eNetTypeIndex      Specify which network
* @param[in] prStaRec           Pointer to the STA_RECORD_T
*
* @return The length of P2P IE added
*/
/*----------------------------------------------------------------------------*/
UINT_32
p2pFuncCalculateP2p_IELenForAssocRsp (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    )
{

    if (eNetTypeIndex != NETWORK_TYPE_P2P_INDEX) {
        return 0;
    }

    return p2pFuncCalculateP2P_IELen(prAdapter,
                                eNetTypeIndex,
                                prStaRec,
                                txAssocRspAttributesTable,
                                sizeof(txAssocRspAttributesTable)/sizeof(APPEND_VAR_ATTRI_ENTRY_T));

} /* p2pFuncCalculateP2p_IELenForAssocRsp */






/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to generate P2P IE for Beacon frame.
*
* @param[in] prMsduInfo             Pointer to the composed MSDU_INFO_T.
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
p2pFuncGenerateP2p_IEForAssocRsp (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    )
{
    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;
    P_STA_RECORD_T prStaRec = (P_STA_RECORD_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prMsduInfo != NULL));

        prP2pFsmInfo = prAdapter->rWifiVar.prP2pFsmInfo;

        prStaRec = cnmGetStaRecByIndex(prAdapter, prMsduInfo->ucStaRecIndex);

        if (IS_STA_P2P_TYPE(prStaRec)) {
            DBGLOG(P2P, TRACE, ("Generate NULL P2P IE for Assoc Rsp.\n"));

            p2pFuncGenerateP2P_IE(prAdapter,
                    TRUE,
                    &prMsduInfo->u2FrameLength,
                    prMsduInfo->prPacket,
                    1500,
                    txAssocRspAttributesTable,
                    sizeof(txAssocRspAttributesTable)/sizeof(APPEND_VAR_ATTRI_ENTRY_T));
        }
        else {

            DBGLOG(P2P, TRACE, ("Legacy device, no P2P IE.\n"));
        }

    } while (FALSE);

    return;

} /* p2pFuncGenerateP2p_IEForAssocRsp */


UINT_32
p2pFuncCalculateWSC_IELenForAssocRsp (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    )
{
	DBGLOG(P2P, TRACE, ("p2pFuncCalculateWSC_IELenForAssocRsp\n"));
    if (eNetTypeIndex != NETWORK_TYPE_P2P_INDEX) {
        return 0;
    }

    return kalP2PCalWSC_IELen(prAdapter->prGlueInfo, 0);
} /* p2pFuncCalculateP2p_IELenForAssocRsp */


VOID
p2pFuncGenerateWSC_IEForAssocRsp (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    )
{
    PUINT_8               pucBuffer;
    UINT_16               u2IELen = 0;
    ASSERT(prAdapter);
    ASSERT(prMsduInfo);

    if (prMsduInfo->ucNetworkType != NETWORK_TYPE_P2P_INDEX) {
        return;
    }
	DBGLOG(P2P, TRACE, ("p2pFuncGenerateWSC_IEForAssocRsp\n"));

    u2IELen = (UINT_16)kalP2PCalWSC_IELen(prAdapter->prGlueInfo, 0);

    pucBuffer = (PUINT_8)((UINT_32)prMsduInfo->prPacket +
                          (UINT_32)prMsduInfo->u2FrameLength);

    ASSERT(pucBuffer);

    // TODO: Check P2P FSM State.
    kalP2PGenWSC_IE(prAdapter->prGlueInfo,
                   0,
                   pucBuffer);

    prMsduInfo->u2FrameLength += u2IELen;

    return;
} 
/* p2pFuncGenerateP2p_IEForAssocRsp */




UINT_32
p2pFuncCalculateP2P_IELen (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec,
    IN APPEND_VAR_ATTRI_ENTRY_T arAppendAttriTable[],
    IN UINT_32 u4AttriTableSize
    )
{

    UINT_32 u4OverallAttriLen, u4Dummy;
    UINT_16 u2EstimatedFixedAttriLen;
    UINT_32 i;


    /* Overall length of all Attributes */
    u4OverallAttriLen = 0;

    for (i = 0; i < u4AttriTableSize; i++) {
        u2EstimatedFixedAttriLen = arAppendAttriTable[i].u2EstimatedFixedAttriLen;

        if (u2EstimatedFixedAttriLen) {
            u4OverallAttriLen += u2EstimatedFixedAttriLen;
        }
        else {
            ASSERT(arAppendAttriTable[i].pfnCalculateVariableAttriLen);

            u4OverallAttriLen +=
                arAppendAttriTable[i].pfnCalculateVariableAttriLen(prAdapter, prStaRec);
        }
    }

    u4Dummy = u4OverallAttriLen;
    u4OverallAttriLen += P2P_IE_OUI_HDR;

    for (;(u4Dummy > P2P_MAXIMUM_ATTRIBUTE_LEN);) {
        u4OverallAttriLen += P2P_IE_OUI_HDR;
        u4Dummy -= P2P_MAXIMUM_ATTRIBUTE_LEN;
    }

    return u4OverallAttriLen;
} /* p2pFuncCalculateP2P_IELen */


VOID
p2pFuncGenerateP2P_IE (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize,
    IN APPEND_VAR_ATTRI_ENTRY_T arAppendAttriTable[],
    IN UINT_32 u4AttriTableSize
    )
{
    PUINT_8 pucBuffer = (PUINT_8)NULL;
    P_IE_P2P_T prIeP2P = (P_IE_P2P_T)NULL;
    UINT_32 u4OverallAttriLen;
    UINT_32 u4AttriLen;
    UINT_8 aucWfaOui[] = VENDOR_OUI_WFA_SPECIFIC;
    UINT_8 aucTempBuffer[P2P_MAXIMUM_ATTRIBUTE_LEN];
    UINT_32 i;


    do {
        ASSERT_BREAK((prAdapter != NULL) && (pucBuf != NULL));

        pucBuffer = (PUINT_8)((UINT_32)pucBuf + (*pu2Offset));

        ASSERT_BREAK(pucBuffer != NULL);

        /* Check buffer length is still enough. */
        ASSERT_BREAK((u2BufSize - (*pu2Offset)) >= P2P_IE_OUI_HDR);

        prIeP2P = (P_IE_P2P_T)pucBuffer;

        prIeP2P->ucId = ELEM_ID_P2P;

        prIeP2P->aucOui[0] = aucWfaOui[0];
        prIeP2P->aucOui[1] = aucWfaOui[1];
        prIeP2P->aucOui[2] = aucWfaOui[2];
        prIeP2P->ucOuiType = VENDOR_OUI_TYPE_P2P;

        (*pu2Offset) += P2P_IE_OUI_HDR;

        /* Overall length of all Attributes */
        u4OverallAttriLen = 0;


        for (i = 0; i < u4AttriTableSize; i++) {

            if (arAppendAttriTable[i].pfnAppendAttri) {
                u4AttriLen = arAppendAttriTable[i].pfnAppendAttri(prAdapter, fgIsAssocFrame, pu2Offset, pucBuf, u2BufSize);

                u4OverallAttriLen += u4AttriLen;

                if (u4OverallAttriLen > P2P_MAXIMUM_ATTRIBUTE_LEN) {
                    u4OverallAttriLen -= P2P_MAXIMUM_ATTRIBUTE_LEN;

                    prIeP2P->ucLength = (VENDOR_OUI_TYPE_LEN + P2P_MAXIMUM_ATTRIBUTE_LEN);

                    pucBuffer = (PUINT_8)((UINT_32)prIeP2P + (VENDOR_OUI_TYPE_LEN + P2P_MAXIMUM_ATTRIBUTE_LEN));

                    prIeP2P = (P_IE_P2P_T)((UINT_32)prIeP2P +
                            (ELEM_HDR_LEN + (VENDOR_OUI_TYPE_LEN + P2P_MAXIMUM_ATTRIBUTE_LEN)));

                    kalMemCopy(aucTempBuffer, pucBuffer, u4OverallAttriLen);

                    prIeP2P->ucId = ELEM_ID_P2P;

                    prIeP2P->aucOui[0] = aucWfaOui[0];
                    prIeP2P->aucOui[1] = aucWfaOui[1];
                    prIeP2P->aucOui[2] = aucWfaOui[2];
                    prIeP2P->ucOuiType = VENDOR_OUI_TYPE_P2P;

                    kalMemCopy(prIeP2P->aucP2PAttributes, aucTempBuffer, u4OverallAttriLen);
                    (*pu2Offset) += P2P_IE_OUI_HDR;
                }

            }

        }

        prIeP2P->ucLength = (UINT_8)(VENDOR_OUI_TYPE_LEN + u4OverallAttriLen);


    } while (FALSE);

    return;
} /* p2pFuncGenerateP2P_IE */

UINT_32
p2pFuncAppendAttriStatusForAssocRsp (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    )
{
    PUINT_8 pucBuffer;
    P_P2P_ATTRI_STATUS_T prAttriStatus;
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;
    UINT_32 u4AttriLen = 0;

    ASSERT(prAdapter);
    ASSERT(pucBuf);

    prP2pConnSettings = prAdapter->rWifiVar.prP2PConnSettings;

    if (fgIsAssocFrame) {
        return u4AttriLen;
    }

    // TODO: For assoc request P2P IE check in driver & return status in P2P IE.

    pucBuffer = (PUINT_8)((UINT_32)pucBuf +
                            (UINT_32)(*pu2Offset));

    ASSERT(pucBuffer);
    prAttriStatus = (P_P2P_ATTRI_STATUS_T)pucBuffer;

    ASSERT(u2BufSize >= ((*pu2Offset) + (UINT_16)u4AttriLen));




    prAttriStatus->ucId = P2P_ATTRI_ID_STATUS;
    WLAN_SET_FIELD_16(&prAttriStatus->u2Length, P2P_ATTRI_MAX_LEN_STATUS);

    prAttriStatus->ucStatusCode = P2P_STATUS_FAIL_PREVIOUS_PROTOCOL_ERR;

    u4AttriLen = (P2P_ATTRI_HDR_LEN + P2P_ATTRI_MAX_LEN_STATUS);

    (*pu2Offset) += (UINT_16)u4AttriLen;

    return u4AttriLen;
} /* p2pFuncAppendAttriStatusForAssocRsp */

UINT_32
p2pFuncAppendAttriExtListenTiming (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    )
{
    UINT_32 u4AttriLen = 0;
    P_P2P_ATTRI_EXT_LISTEN_TIMING_T prP2pExtListenTiming = (P_P2P_ATTRI_EXT_LISTEN_TIMING_T)NULL;
    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    PUINT_8 pucBuffer = NULL;

    ASSERT(prAdapter);
    ASSERT(pucBuf);

    if (fgIsAssocFrame) {
        return u4AttriLen;
    }

    // TODO: For extend listen timing.

    prP2pSpecificBssInfo = prAdapter->rWifiVar.prP2pSpecificBssInfo;

    u4AttriLen = (P2P_ATTRI_HDR_LEN + P2P_ATTRI_MAX_LEN_EXT_LISTEN_TIMING);

    ASSERT(u2BufSize >= ((*pu2Offset) + (UINT_16)u4AttriLen));

    pucBuffer = (PUINT_8)((UINT_32)pucBuf +
                            (UINT_32)(*pu2Offset));

    ASSERT(pucBuffer);

    prP2pExtListenTiming = (P_P2P_ATTRI_EXT_LISTEN_TIMING_T)pucBuffer;

    prP2pExtListenTiming->ucId = P2P_ATTRI_ID_EXT_LISTEN_TIMING;
    WLAN_SET_FIELD_16(&prP2pExtListenTiming->u2Length, P2P_ATTRI_MAX_LEN_EXT_LISTEN_TIMING);
    WLAN_SET_FIELD_16(&prP2pExtListenTiming->u2AvailInterval, prP2pSpecificBssInfo->u2AvailabilityInterval);
    WLAN_SET_FIELD_16(&prP2pExtListenTiming->u2AvailPeriod, prP2pSpecificBssInfo->u2AvailabilityPeriod);

    (*pu2Offset) += (UINT_16)u4AttriLen;

    return u4AttriLen;
} /* p2pFuncAppendAttriExtListenTiming */


P_IE_HDR_T
p2pFuncGetSpecIE (
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucIEBuf,
    IN UINT_16 u2BufferLen,
    IN UINT_8 ucElemID,
    IN PBOOLEAN pfgIsMore
    )
{
    P_IE_HDR_T prTargetIE = (P_IE_HDR_T)NULL;
    PUINT_8 pucIE = (PUINT_8)NULL;
    UINT_16 u2Offset = 0;
    
    if (pfgIsMore) {
            *pfgIsMore = FALSE;
    }
    
    do {
        ASSERT_BREAK((prAdapter != NULL) 
                && (pucIEBuf != NULL));

        pucIE = pucIEBuf;

        IE_FOR_EACH(pucIE, u2BufferLen, u2Offset) {
            if (IE_ID(pucIE) == ucElemID) {
                if ((prTargetIE) && (pfgIsMore)) {

                    *pfgIsMore = TRUE;
                    break;
                }
                else {
                    prTargetIE = (P_IE_HDR_T)pucIE;

                    if (pfgIsMore == NULL) {
                        break;
                    }

                }

            }
        }

    } while (FALSE);

    return prTargetIE;
} /* p2pFuncGetSpecIE */



P_ATTRIBUTE_HDR_T
p2pFuncGetSpecAttri (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucOuiType,
    IN PUINT_8 pucIEBuf,
    IN UINT_16 u2BufferLen,
    IN UINT_16 u2AttriID
    )
{
    P_IE_P2P_T prP2pIE = (P_IE_P2P_T)NULL;
    P_ATTRIBUTE_HDR_T prTargetAttri = (P_ATTRIBUTE_HDR_T)NULL;
    BOOLEAN fgIsMore = FALSE;
    PUINT_8 pucIE = (PUINT_8)NULL, pucAttri = (PUINT_8)NULL;
    UINT_16 u2OffsetAttri = 0;
    UINT_16 u2BufferLenLeft = 0;
    UINT_8 aucWfaOui[] = VENDOR_OUI_WFA_SPECIFIC;


    DBGLOG(P2P, INFO, ("Check AssocReq Oui type %u attri %u for len %u\n",ucOuiType, u2AttriID, u2BufferLen));

    do {
        ASSERT_BREAK((prAdapter != NULL) 
                && (pucIEBuf != NULL));

        u2BufferLenLeft = u2BufferLen;
        pucIE = pucIEBuf;
        do {
            fgIsMore = FALSE;
            prP2pIE = (P_IE_P2P_T)p2pFuncGetSpecIE(prAdapter,
                                            pucIE,
                                            u2BufferLenLeft,
                                            ELEM_ID_VENDOR,
                                            &fgIsMore);

            if (prP2pIE) {

                ASSERT(prP2pIE>pucIE);

                u2BufferLenLeft = u2BufferLen - (UINT_16)(  ((UINT_32)prP2pIE) - ((UINT_32)pucIEBuf));

                DBGLOG(P2P, INFO, ("Find vendor id %u len %u oui %u more %u LeftLen %u\n",
                        IE_ID(prP2pIE), IE_LEN(prP2pIE), prP2pIE->ucOuiType, fgIsMore, u2BufferLenLeft));

                if(IE_LEN(prP2pIE) > P2P_OUI_TYPE_LEN) {

                if (prP2pIE->ucOuiType == ucOuiType) {
                    switch (ucOuiType) {
                    case VENDOR_OUI_TYPE_WPS:
                        aucWfaOui[0] = 0x00;
                        aucWfaOui[1] = 0x50;
                        aucWfaOui[2] = 0xF2;
                        break;
                    case VENDOR_OUI_TYPE_P2P:
                        break;
                    case VENDOR_OUI_TYPE_WPA:
                    case VENDOR_OUI_TYPE_WMM:
                            case VENDOR_OUI_TYPE_WFD:
                    default:
                        break;
                    }


                        if ((prP2pIE->aucOui[0] == aucWfaOui[0]) 
                              && (prP2pIE->aucOui[1] == aucWfaOui[1]) 
                              && (prP2pIE->aucOui[2] == aucWfaOui[2]) 
                                ) {

                            u2OffsetAttri = 0;
                        pucAttri = prP2pIE->aucP2PAttributes;

                        if (ucOuiType == VENDOR_OUI_TYPE_WPS) {
                                WSC_ATTRI_FOR_EACH(pucAttri, (IE_LEN(prP2pIE) - P2P_OUI_TYPE_LEN), u2OffsetAttri) {
                                    //LOG_FUNC("WSC: attri id=%u len=%u\n",WSC_ATTRI_ID(pucAttri), WSC_ATTRI_LEN(pucAttri));
                                    if (WSC_ATTRI_ID(pucAttri) == u2AttriID) {
                                    prTargetAttri = (P_ATTRIBUTE_HDR_T)pucAttri;
                                    break;
                                }

                            }

                        }

                        else if (ucOuiType == VENDOR_OUI_TYPE_P2P) {
                                P2P_ATTRI_FOR_EACH(pucAttri, (IE_LEN(prP2pIE) - P2P_OUI_TYPE_LEN), u2OffsetAttri) {
                                    //LOG_FUNC("P2P: attri id=%u len=%u\n",ATTRI_ID(pucAttri), ATTRI_LEN(pucAttri));
                                    if (ATTRI_ID(pucAttri) == (UINT_8)u2AttriID) {
                                    prTargetAttri = (P_ATTRIBUTE_HDR_T)pucAttri;
                                    break;
                                }

                            }

                        }
                            else if (ucOuiType == VENDOR_OUI_TYPE_WFD) {
                                WFD_ATTRI_FOR_EACH(pucAttri, (IE_LEN(prP2pIE) - P2P_OUI_TYPE_LEN), u2OffsetAttri) {
                                    //DBGLOG(P2P, INFO, ("WFD: attri id=%u len=%u\n",WFD_ATTRI_ID(pucAttri), WFD_ATTRI_LEN(pucAttri)));
                                    if (ATTRI_ID(pucAttri) == (UINT_8)u2AttriID) {
                                        prTargetAttri = (P_ATTRIBUTE_HDR_T)pucAttri;
                                        break;
                                    }
                     
                        }
                    }
                            else {
                                // Possible or else.
                }

            }
                    } /* ucOuiType */
                } /* P2P_OUI_TYPE_LEN */ 
            
                pucIE = (PUINT_8)(((UINT_32)prP2pIE) + IE_SIZE(prP2pIE));

            } /* prP2pIE */
                
        } while (prP2pIE && fgIsMore && u2BufferLenLeft);

    } while (FALSE);

    return prTargetAttri;
}
/* p2pFuncGetSpecAttri */


WLAN_STATUS
p2pFuncGenerateBeaconProbeRsp (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN P_MSDU_INFO_T prMsduInfo,
    IN BOOLEAN fgIsProbeRsp
    )
{
    WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;
    P_WLAN_BEACON_FRAME_T prBcnFrame = (P_WLAN_BEACON_FRAME_T)NULL;
//    P_APPEND_VAR_IE_ENTRY_T prAppendIeTable = (P_APPEND_VAR_IE_ENTRY_T)NULL;


    do {

        ASSERT_BREAK((prAdapter != NULL) &&
                                (prBssInfo != NULL) &&
                                (prMsduInfo != NULL));


//        txBcnIETable

//        txProbeRspIETable



        prBcnFrame = (P_WLAN_BEACON_FRAME_T)prMsduInfo->prPacket;
    
        return nicUpdateBeaconIETemplate(prAdapter,
                    IE_UPD_METHOD_UPDATE_ALL,
                    NETWORK_TYPE_P2P_INDEX,
                    prBssInfo->u2CapInfo,
                    (PUINT_8)prBcnFrame->aucInfoElem,
                    prMsduInfo->u2FrameLength - OFFSET_OF(WLAN_BEACON_FRAME_T, aucInfoElem));
    
    } while (FALSE);

    return rWlanStatus;
} /* p2pFuncGenerateBeaconProbeRsp */


WLAN_STATUS
p2pFuncComposeBeaconProbeRspTemplate (
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucBcnBuffer,
    IN UINT_32 u4BcnBufLen,
    IN BOOLEAN fgIsProbeRsp,
    IN P_P2P_PROBE_RSP_UPDATE_INFO_T prP2pProbeRspInfo,
    IN BOOLEAN fgSynToFW
    )
{
    WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;
    P_MSDU_INFO_T prMsduInfo = (P_MSDU_INFO_T)NULL;
    P_WLAN_MAC_HEADER_T prWlanBcnFrame = (P_WLAN_MAC_HEADER_T)NULL;
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;
    
    PUINT_8 pucBuffer = (PUINT_8)NULL;
    
    do {
        ASSERT_BREAK((prAdapter != NULL) && (pucBcnBuffer != NULL));

        prWlanBcnFrame = (P_WLAN_MAC_HEADER_T)pucBcnBuffer;

        if ((prWlanBcnFrame->u2FrameCtrl != MAC_FRAME_BEACON) && (!fgIsProbeRsp)) {
            rWlanStatus = WLAN_STATUS_INVALID_DATA;
            break;
        }

        else if (prWlanBcnFrame->u2FrameCtrl != MAC_FRAME_PROBE_RSP) {
            rWlanStatus = WLAN_STATUS_INVALID_DATA;
            break;
        }



        if (fgIsProbeRsp) {
            ASSERT_BREAK(prP2pProbeRspInfo != NULL);

            if (!prP2pProbeRspInfo->prProbeRspMsduTemplate) {
                cnmMgtPktFree(prAdapter, prP2pProbeRspInfo->prProbeRspMsduTemplate);
            }

            prP2pProbeRspInfo->prProbeRspMsduTemplate = cnmMgtPktAlloc(prAdapter, u4BcnBufLen);

            prMsduInfo = prP2pProbeRspInfo->prProbeRspMsduTemplate;

            prMsduInfo->eSrc = TX_PACKET_MGMT;
            prMsduInfo->ucStaRecIndex = 0xFF;
            prMsduInfo->ucNetworkType = NETWORK_TYPE_P2P_INDEX;

        }
        else {
            prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);
            prMsduInfo = prP2pBssInfo->prBeacon;

            if (prMsduInfo == NULL) {
                rWlanStatus = WLAN_STATUS_FAILURE;
                break;
            }

            if (u4BcnBufLen > (OFFSET_OF(WLAN_BEACON_FRAME_T, aucInfoElem[0]) + MAX_IE_LENGTH)) {
                /* Unexpected error, buffer overflow. */
                ASSERT(FALSE);
                break;
            }

        }

        
        pucBuffer = (PUINT_8)((UINT_32)(prMsduInfo->prPacket) + MAC_TX_RESERVED_FIELD);

        kalMemCopy(pucBuffer, pucBcnBuffer, u4BcnBufLen);

        prMsduInfo->fgIs802_11 = TRUE;
        prMsduInfo->u2FrameLength = (UINT_16)u4BcnBufLen;

        if (fgSynToFW) {
            rWlanStatus = p2pFuncGenerateBeaconProbeRsp(prAdapter, prP2pBssInfo, prMsduInfo, fgIsProbeRsp);
        }

    } while (FALSE);

    return rWlanStatus;

} /* p2pFuncComposeBeaconTemplate */



















