#include "p2p_precomp.h"


BOOLEAN
p2pStateInit_IDLE (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN P_BSS_INFO_T prP2pBssInfo,
    OUT P_ENUM_P2P_STATE_T peNextState
    )
{
    BOOLEAN fgIsTransOut = FALSE;
//    P_P2P_CHNL_REQ_INFO_T prChnlReqInfo = (P_P2P_CHNL_REQ_INFO_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                        (prP2pFsmInfo != NULL) &&
                        (prP2pBssInfo != NULL) &&
                        (peNextState != NULL));

        if ((prP2pBssInfo->eIntendOPMode == OP_MODE_ACCESS_POINT) && IS_NET_PWR_STATE_ACTIVE(prAdapter, NETWORK_TYPE_P2P_INDEX)) {
            P_P2P_CHNL_REQ_INFO_T prChnlReqInfo = &(prP2pFsmInfo->rChnlReqInfo);

            fgIsTransOut = TRUE;
            prChnlReqInfo->eChannelReqType = CHANNEL_REQ_TYPE_GO_START_BSS;
            *peNextState = P2P_STATE_REQING_CHANNEL;

        }
        else {
#if 0        
        else if (IS_NET_PWR_STATE_ACTIVE(prAdapter, NETWORK_TYPE_P2P_INDEX)) {

            ASSERT((prP2pBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT) || 
                    (prP2pBssInfo->eCurrentOPMode == OP_MODE_INFRASTRUCTURE));
            
            prChnlReqInfo = &prP2pFsmInfo->rChnlReqInfo;

            if (prChnlReqInfo->fgIsChannelRequested) {
                /* Start a timer for return channel. */
                DBGLOG(P2P, TRACE, ("start a GO channel timer.\n"));
            }

        }
        
#endif
            cnmTimerStartTimer(prAdapter, &(prP2pFsmInfo->rP2pFsmTimeoutTimer), 5000);
        }

    } while (FALSE);

    return fgIsTransOut;
} /* p2pStateInit_IDLE */


VOID
p2pStateAbort_IDLE (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN ENUM_P2P_STATE_T eNextState
    )
{

    P_P2P_CHNL_REQ_INFO_T prChnlReqInfo = (P_P2P_CHNL_REQ_INFO_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                                    (prP2pFsmInfo != NULL));

        prChnlReqInfo = &prP2pFsmInfo->rChnlReqInfo;


        if (prChnlReqInfo->fgIsChannelRequested) {
            /* Release channel before timeout. */
            p2pFuncReleaseCh(prAdapter, prChnlReqInfo);
        }


        /* Stop timer for leaving this state. */
        cnmTimerStopTimer(prAdapter, &(prP2pFsmInfo->rP2pFsmTimeoutTimer));

    } while (FALSE);

    return;
} /* p2pStateAbort_IDLE */



VOID
p2pStateInit_CHNL_ON_HAND (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo
    )
{
    P_P2P_CHNL_REQ_INFO_T prChnlReqInfo = (P_P2P_CHNL_REQ_INFO_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prP2pFsmInfo != NULL));

        prChnlReqInfo = &(prP2pFsmInfo->rChnlReqInfo);

        /* Store the original channel info. */
        prChnlReqInfo->ucOriChnlNum = prP2pBssInfo->ucPrimaryChannel;
        prChnlReqInfo->eOriBand = prP2pBssInfo->eBand;
        prChnlReqInfo->eOriChnlSco = prP2pBssInfo->eBssSCO;

        /* RX Probe Request would check primary channel.*/
        prP2pBssInfo->ucPrimaryChannel = prChnlReqInfo->ucReqChnlNum;
        prP2pBssInfo->eBand = prChnlReqInfo->eBand;
        prP2pBssInfo->eBssSCO = prChnlReqInfo->eChnlSco;


        DBGLOG(P2P, TRACE, ("start a channel on hand timer.\n"));
        cnmTimerStartTimer(prAdapter,
                        &(prP2pFsmInfo->rP2pFsmTimeoutTimer),
                        prChnlReqInfo->u4MaxInterval);

        kalP2PIndicateChannelReady(prAdapter->prGlueInfo,
                            prChnlReqInfo->u8Cookie,
                            prChnlReqInfo->ucReqChnlNum,
                            prChnlReqInfo->eBand,
                            prChnlReqInfo->eChnlSco,
                            prChnlReqInfo->u4MaxInterval);

    } while (FALSE);

    return;
} /* p2pStateInit_CHNL_ON_HAND */


VOID
p2pStateAbort_CHNL_ON_HAND (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN ENUM_P2P_STATE_T eNextState
    )
{
    P_P2P_CHNL_REQ_INFO_T prChnlReqInfo = (P_P2P_CHNL_REQ_INFO_T)NULL;


    do {
        ASSERT_BREAK((prAdapter != NULL) && (prP2pFsmInfo != NULL));

        prChnlReqInfo = &(prP2pFsmInfo->rChnlReqInfo);

        cnmTimerStopTimer(prAdapter, &(prP2pFsmInfo->rP2pFsmTimeoutTimer));

        /* Restore the original channel info. */
        prP2pBssInfo->ucPrimaryChannel = prChnlReqInfo->ucOriChnlNum;
        prP2pBssInfo->eBand = prChnlReqInfo->eOriBand;
        prP2pBssInfo->eBssSCO = prChnlReqInfo->eOriChnlSco;

        if (eNextState != P2P_STATE_CHNL_ON_HAND) {
            /* Indicate channel return. */
            kalP2PIndicateChannelExpired(prAdapter->prGlueInfo, &prP2pFsmInfo->rChnlReqInfo);

            // Return Channel.
            p2pFuncReleaseCh(prAdapter, &(prP2pFsmInfo->rChnlReqInfo));
        }

    } while (FALSE);
    return;
} /* p2pStateAbort_CHNL_ON_HAND */


VOID
p2pStateAbort_REQING_CHANNEL (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN ENUM_P2P_STATE_T eNextState
    )
{
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;
    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;

    do {

        ASSERT_BREAK((prAdapter != NULL) &&
                            (prP2pFsmInfo != NULL) &&
                            (eNextState < P2P_STATE_NUM));

        prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);
        prP2pSpecificBssInfo = prAdapter->rWifiVar.prP2pSpecificBssInfo;

        if (eNextState == P2P_STATE_IDLE) {
            if (prP2pBssInfo->eIntendOPMode == OP_MODE_ACCESS_POINT) {
                /* Intend to be AP. */
                /* Setup for AP mode. */
                p2pFuncStartGO(prAdapter,
                                    prP2pBssInfo,
                                    prP2pSpecificBssInfo->aucGroupSsid,
                                    prP2pSpecificBssInfo->u2GroupSsidLen,
                                    prP2pSpecificBssInfo->ucPreferredChannel,
                                    prP2pSpecificBssInfo->eRfBand,
                                    prP2pSpecificBssInfo->eRfSco,
                                    prP2pFsmInfo->fgIsApMode);

            }
            else {
                // Return Channel.
                p2pFuncReleaseCh(prAdapter, &(prP2pFsmInfo->rChnlReqInfo));
            }

        }


    } while (FALSE);

    return;
} /* p2pStateInit_AP_CHANNEL_DETECT */


VOID
p2pStateInit_AP_CHANNEL_DETECT (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo
    )
{
    P_P2P_SCAN_REQ_INFO_T prScanReqInfo = (P_P2P_SCAN_REQ_INFO_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prP2pFsmInfo != NULL));

        prScanReqInfo = &(prP2pFsmInfo->rScanReqInfo);

        prScanReqInfo->eScanType = SCAN_TYPE_PASSIVE_SCAN;
        prScanReqInfo->eChannelSet = SCAN_CHANNEL_2G4;
        prScanReqInfo->u2PassiveDewellTime = 50;    // 50ms for passive channel load detection
        prScanReqInfo->fgIsAbort = TRUE;
        prScanReqInfo->fgIsScanRequest = TRUE;
        prScanReqInfo->ucNumChannelList = 0;
        prScanReqInfo->u4BufLength = 0;
        prScanReqInfo->rSsidStruct.ucSsidLen = 0;

        p2pFuncRequestScan(prAdapter, prScanReqInfo);

    } while (FALSE);

    return;
} /* p2pStateInit_AP_CHANNEL_DETECT */



VOID
p2pStateAbort_AP_CHANNEL_DETECT (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo,
    IN ENUM_P2P_STATE_T eNextState
    )
{
    P_P2P_CHNL_REQ_INFO_T prChnlReqInfo = (P_P2P_CHNL_REQ_INFO_T)NULL;
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;

    do {

        if (eNextState == P2P_STATE_REQING_CHANNEL) {
            UINT_8 ucPreferedChnl = 0;
            ENUM_BAND_T eBand = BAND_NULL;
            ENUM_CHNL_EXT_T eSco = CHNL_EXT_SCN;

            prChnlReqInfo = &(prP2pFsmInfo->rChnlReqInfo);

            /* Determine the channel for AP. */
            if (cnmPreferredChannel(prAdapter,
                    &eBand,
                    &ucPreferedChnl,
                    &eSco) == FALSE) {

                prP2pConnSettings = prAdapter->rWifiVar.prP2PConnSettings;

                if ((ucPreferedChnl = prP2pConnSettings->ucOperatingChnl) == 0) {

                    if (scnQuerySparseChannel(prAdapter, &eBand, &ucPreferedChnl) == FALSE) {

                        // What to do?
                        ASSERT(FALSE);
                        // TODO: Pick up a valid channel from channel list.
                    }
                }
            }


            prChnlReqInfo->eChannelReqType = CHANNEL_REQ_TYPE_GO_START_BSS;
            prChnlReqInfo->ucReqChnlNum = prP2pSpecificBssInfo->ucPreferredChannel = ucPreferedChnl;
            prChnlReqInfo->eBand = prP2pSpecificBssInfo->eRfBand = eBand;
            prChnlReqInfo->eChnlSco = prP2pSpecificBssInfo->eRfSco = eSco;
        }
        else {
            p2pFuncCancelScan(prAdapter, &(prP2pFsmInfo->rScanReqInfo));
        }


    } while (FALSE);

    return;
} /* p2pStateAbort_AP_CHANNEL_DETECT */


VOID
p2pStateInit_SCAN (

    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo
    )
{
    P_P2P_SCAN_REQ_INFO_T prScanReqInfo = (P_P2P_SCAN_REQ_INFO_T)NULL;

    do {

        ASSERT_BREAK((prAdapter != NULL) && (prP2pFsmInfo != NULL));

        prScanReqInfo = &prP2pFsmInfo->rScanReqInfo;

        prScanReqInfo->fgIsScanRequest = TRUE;

        p2pFuncRequestScan(prAdapter, prScanReqInfo);

    } while (FALSE);
    return;
} /* p2pStateInit_SCAN */


VOID
p2pStateAbort_SCAN (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN ENUM_P2P_STATE_T eNextState
    )
{
    do {
        ASSERT_BREAK(prAdapter != NULL);

        // 1. Scan cancel. (Make sure the scan request is invalid.
        p2pFuncCancelScan(prAdapter, &(prP2pFsmInfo->rScanReqInfo));

        // Scan done indication.
        kalP2PIndicateScanDone(prAdapter->prGlueInfo, prP2pFsmInfo->rScanReqInfo.fgIsAbort);
    } while (FALSE);

    return;
} /* p2pStateAbort_SCAN */


VOID
p2pStateInit_GC_JOIN (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN P_P2P_JOIN_INFO_T prJoinInfo,
    IN P_BSS_DESC_T prBssDesc
    )
{
    P_MSG_JOIN_REQ_T prJoinReqMsg = (P_MSG_JOIN_REQ_T)NULL;
    P_STA_RECORD_T prStaRec = (P_STA_RECORD_T)NULL;
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                                    (prP2pFsmInfo != NULL) &&
                                    (prP2pBssInfo != NULL) &&
                                    (prJoinInfo != NULL) &&
                                    (prBssDesc != NULL));

        prP2pConnSettings = prAdapter->rWifiVar.prP2PConnSettings;

        if (prBssDesc->ucSSIDLen) {
            COPY_SSID(prP2pConnSettings->aucSSID,
                            prP2pConnSettings->ucSSIDLen,
                            prBssDesc->aucSSID,
                            prBssDesc->ucSSIDLen);
        }


        // Setup a join timer.
        DBGLOG(P2P, TRACE, ("Start a join init timer\n"));
        cnmTimerStartTimer(prAdapter,
                            &(prP2pFsmInfo->rP2pFsmTimeoutTimer),
                            (prP2pFsmInfo->u4GrantInterval - AIS_JOIN_CH_GRANT_THRESHOLD));

        //2 <1> We are goin to connect to this BSS
        prBssDesc->fgIsConnecting = TRUE;

        //2 <2> Setup corresponding STA_RECORD_T
        prStaRec = bssCreateStaRecFromBssDesc(prAdapter,
                                    (prBssDesc->fgIsP2PPresent?(STA_TYPE_P2P_GO):(STA_TYPE_LEGACY_AP)),
                                    NETWORK_TYPE_P2P_INDEX,
                                    prBssDesc);

        if (prStaRec == NULL) {
            DBGLOG(P2P, TRACE, ("Create station record fail\n"));
            break;
        }


        prJoinInfo->prTargetStaRec = prStaRec;
        prJoinInfo->fgIsJoinComplete = FALSE;
        prJoinInfo->u4BufLength = 0;

        //2 <2.1> Sync. to FW domain
        cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);


        if (prP2pBssInfo->eConnectionState == PARAM_MEDIA_STATE_DISCONNECTED) {
            P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;

            prStaRec->fgIsReAssoc = FALSE;

            prP2pConnSettings = prAdapter->rWifiVar.prP2PConnSettings;

            switch (prP2pConnSettings->eAuthMode) {
            case AUTH_MODE_OPEN:                /* Note: Omit break here. */
            case AUTH_MODE_WPA:
            case AUTH_MODE_WPA_PSK:
            case AUTH_MODE_WPA2:
            case AUTH_MODE_WPA2_PSK:
                prJoinInfo->ucAvailableAuthTypes = (UINT_8)AUTH_TYPE_OPEN_SYSTEM;
                break;
            case AUTH_MODE_SHARED:
                prJoinInfo->ucAvailableAuthTypes = (UINT_8)AUTH_TYPE_SHARED_KEY;
                break;
            case AUTH_MODE_AUTO_SWITCH:
                DBGLOG(P2P, LOUD, ("JOIN INIT: eAuthMode == AUTH_MODE_AUTO_SWITCH\n"));
                prJoinInfo->ucAvailableAuthTypes = (UINT_8)(AUTH_TYPE_OPEN_SYSTEM |
                                                              AUTH_TYPE_SHARED_KEY);
                break;
            default:
                ASSERT(!(prP2pConnSettings->eAuthMode == AUTH_MODE_WPA_NONE));
                DBGLOG(P2P, ERROR, ("JOIN INIT: Auth Algorithm : %d was not supported by JOIN\n",
                                               prP2pConnSettings->eAuthMode));
                /* TODO(Kevin): error handling ? */
                return;
            }
            prStaRec->ucTxAuthAssocRetryLimit = TX_AUTH_ASSOCI_RETRY_LIMIT;
        }
        else {
            ASSERT(FALSE);
            // TODO: Shall we considering ROAMIN case for P2P Device?.
        }


        //2 <4> Use an appropriate Authentication Algorithm Number among the ucAvailableAuthTypes.
        if (prJoinInfo->ucAvailableAuthTypes &
            (UINT_8)AUTH_TYPE_OPEN_SYSTEM) {

            DBGLOG(P2P, TRACE, ("JOIN INIT: Try to do Authentication with AuthType == OPEN_SYSTEM.\n"));

            prJoinInfo->ucAvailableAuthTypes &=
                                            ~(UINT_8)AUTH_TYPE_OPEN_SYSTEM;

            prStaRec->ucAuthAlgNum = (UINT_8)AUTH_ALGORITHM_NUM_OPEN_SYSTEM;
        }
        else if (prJoinInfo->ucAvailableAuthTypes &
            (UINT_8)AUTH_TYPE_SHARED_KEY) {

            DBGLOG(P2P, TRACE, ("JOIN INIT: Try to do Authentication with AuthType == SHARED_KEY.\n"));

            prJoinInfo->ucAvailableAuthTypes &=
                                            ~(UINT_8)AUTH_TYPE_SHARED_KEY;

            prStaRec->ucAuthAlgNum = (UINT_8)AUTH_ALGORITHM_NUM_SHARED_KEY;
        }
        else if (prJoinInfo->ucAvailableAuthTypes &
            (UINT_8)AUTH_TYPE_FAST_BSS_TRANSITION) {

            DBGLOG(P2P, TRACE, ("JOIN INIT: Try to do Authentication with AuthType == FAST_BSS_TRANSITION.\n"));

            prJoinInfo->ucAvailableAuthTypes &=
                                            ~(UINT_8)AUTH_TYPE_FAST_BSS_TRANSITION;

            prStaRec->ucAuthAlgNum = (UINT_8)AUTH_ALGORITHM_NUM_FAST_BSS_TRANSITION;
        }
        else {
            ASSERT(0);
        }


        //4 <5> Overwrite Connection Setting for eConnectionPolicy == ANY (Used by Assoc Req)
        if (prBssDesc->ucSSIDLen) {
            COPY_SSID(prJoinInfo->rSsidStruct.aucSsid,
                      prJoinInfo->rSsidStruct.ucSsidLen,
                      prBssDesc->aucSSID,
                      prBssDesc->ucSSIDLen);
        }

        //2 <5> Backup desired channel.

        //2 <6> Send a Msg to trigger SAA to start JOIN process.
        prJoinReqMsg = (P_MSG_JOIN_REQ_T)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_JOIN_REQ_T));

        if (!prJoinReqMsg) {
            DBGLOG(P2P, TRACE, ("Allocation Join Message Fail\n"));
            ASSERT(FALSE);
            return;
        }

        prJoinReqMsg->rMsgHdr.eMsgId = MID_P2P_SAA_FSM_START;
        prJoinReqMsg->ucSeqNum = ++prJoinInfo->ucSeqNumOfReqMsg;
        prJoinReqMsg->prStaRec = prStaRec;

        // TODO: Consider fragmentation info in station record.

        mboxSendMsg(prAdapter,
                    MBOX_ID_0,
                    (P_MSG_HDR_T)prJoinReqMsg,
                    MSG_SEND_METHOD_BUF);




    } while (FALSE);

    return;
} /* p2pStateInit_GC_JOIN */



/*----------------------------------------------------------------------------*/
/*!
* @brief Process of JOIN Abort. Leave JOIN State & Abort JOIN.
*
* @param (none)
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
p2pStateAbort_GC_JOIN (
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN P_P2P_JOIN_INFO_T prJoinInfo,
    IN ENUM_P2P_STATE_T eNextState
    )
{
    P_MSG_JOIN_ABORT_T prJoinAbortMsg = (P_MSG_JOIN_ABORT_T)NULL;


    do {
        ASSERT_BREAK((prAdapter != NULL) && (prP2pFsmInfo != NULL) && (prJoinInfo != NULL));

        if (prJoinInfo->fgIsJoinComplete == FALSE) {

            prJoinAbortMsg = (P_MSG_JOIN_ABORT_T)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_JOIN_ABORT_T));
            if (!prJoinAbortMsg) {
                DBGLOG(P2P, TRACE, ("Fail to allocate join abort message buffer\n"));
                ASSERT(FALSE);
                return;
            }

            prJoinAbortMsg->rMsgHdr.eMsgId = MID_P2P_SAA_FSM_ABORT;
            prJoinAbortMsg->ucSeqNum = prJoinInfo->ucSeqNumOfReqMsg;
            prJoinAbortMsg->prStaRec = prJoinInfo->prTargetStaRec;

            mboxSendMsg(prAdapter,
                        MBOX_ID_0,
                        (P_MSG_HDR_T)prJoinAbortMsg,
                        MSG_SEND_METHOD_BUF);

        }

        /* Stop Join Timer. */
        cnmTimerStopTimer(prAdapter, &(prP2pFsmInfo->rP2pFsmTimeoutTimer));

        /* Release channel requested. */
        p2pFuncReleaseCh(prAdapter, &(prP2pFsmInfo->rChnlReqInfo));

    } while (FALSE);

    return;

} /* p2pStateAbort_GC_JOIN */



