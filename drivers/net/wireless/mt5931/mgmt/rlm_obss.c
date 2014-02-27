/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/mgmt/rlm_obss.c#2 $
*/

/*! \file   "rlm_obss.c"
    \brief

*/



/*
** $Log: rlm_obss.c $
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 11 15 2011 cm.chang
 * NULL
 * Avoid possible OBSS scan when BSS is switched
 *
 * 11 08 2011 cm.chang
 * NULL
 * Add RLM and CNM debug message for XLOG
 *
 * 10 25 2011 cm.chang
 * [WCXRP00001058] [All Wi-Fi][Driver] Fix sta_rec's phyTypeSet and OBSS scan in AP mode
 * Regulation class is changed to 81 in 20_40_coexistence action frame
 *
 * 04 12 2011 cm.chang
 * [WCXRP00000634] [MT6620 Wi-Fi][Driver][FW] 2nd BSS will not support 40MHz bandwidth for concurrency
 * .
 *
 * 03 29 2011 cm.chang
 * [WCXRP00000606] [MT6620 Wi-Fi][Driver][FW] Fix klocwork warning
 * As CR title
 *
 * 01 24 2011 cm.chang
 * [WCXRP00000384] [MT6620 Wi-Fi][Driver][FW] Handle 20/40 action frame in AP mode and stop ampdu timer when sta_rec is freed
 * Process received 20/40 coexistence action frame for AP mode
 *
 * 01 13 2011 cm.chang
 * [WCXRP00000358] [MT6620 Wi-Fi][Driver] Provide concurrent information for each module
 * Refine function when rcv a 20/40M public action frame
 *
 * 01 13 2011 cm.chang
 * [WCXRP00000354] [MT6620 Wi-Fi][Driver][FW] Follow NVRAM bandwidth setting
 * Use SCO of BSS_INFO to replace user-defined setting variables
 *
 * 01 12 2011 cm.chang
 * [WCXRP00000354] [MT6620 Wi-Fi][Driver][FW] Follow NVRAM bandwidth setting
 * User-defined bandwidth is for 2.4G and 5G individually
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * use definition macro to replace hard-coded constant
 *
 * 09 16 2010 cm.chang
 * NULL
 * Change conditional compiling options for BOW
 *
 * 09 10 2010 cm.chang
 * NULL
 * Always update Beacon content if FW sync OBSS info
 *
 * 08 24 2010 cm.chang
 * NULL
 * Support RLM initail channel of Ad-hoc, P2P and BOW
 *
 * 08 20 2010 cm.chang
 * NULL
 * Migrate RLM code to host from FW
 *
 * 07 26 2010 yuche.tsai
 *
 * Fix compile error while enabling WiFi Direct function.
 *
 * 07 21 2010 yuche.tsai
 *
 * Add P2P Scan & Scan Result Parsing & Saving.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Check draft RLM code for HT cap
 *
 * 05 07 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Process 20/40 coexistence public action frame in AP mode
 *
 * 05 05 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * First draft support for 20/40M bandwidth for AP mode
 *
 * 04 24 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * g_aprBssInfo[] depends on CFG_SUPPORT_P2P and CFG_SUPPORT_BOW
 *
 * 04 13 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add more ASSERT to check exception
 *
 * 04 07 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add virtual test for OBSS scan
 *
 * 03 30 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support 2.4G OBSS scan
 *
 * 03 03 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * To support CFG_SUPPORT_BCM_STP
 *
 * 02 13 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support PCO in STA mode
 *
 * 02 12 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Use bss info array for concurrent handle
 *
 * 02 05 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
 *
 * 01 25 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support protection and bandwidth switch
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
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
static VOID
rlmObssScanTimeout (
    P_ADAPTER_T prAdapter,
    UINT_32     u4Data
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
rlmObssInit (
    P_ADAPTER_T     prAdapter
    )
{
    P_BSS_INFO_T    prBssInfo;
    UINT_8          ucNetIdx;

    RLM_NET_FOR_EACH(ucNetIdx) {
        prBssInfo = &prAdapter->rWifiVar.arBssInfo[ucNetIdx];
        ASSERT(prBssInfo);

        cnmTimerInitTimer(prAdapter, &prBssInfo->rObssScanTimer,
            rlmObssScanTimeout, (UINT_32) prBssInfo);
    } /* end of RLM_NET_FOR_EACH */
}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
rlmObssUpdateChnlLists (
    P_ADAPTER_T prAdapter,
    P_SW_RFB_T  prSwRfb
    )
{
    return TRUE;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
rlmObssScanDone (
    P_ADAPTER_T prAdapter,
    P_MSG_HDR_T prMsgHdr
    )
{
    P_MSG_SCN_SCAN_DONE             prScanDoneMsg;
    P_BSS_INFO_T                    prBssInfo;
    P_MSDU_INFO_T                   prMsduInfo;
    P_ACTION_20_40_COEXIST_FRAME    prTxFrame;
    UINT_16                         i, u2PayloadLen;

    ASSERT(prMsgHdr);

    prScanDoneMsg = (P_MSG_SCN_SCAN_DONE) prMsgHdr;
    prBssInfo = &prAdapter->rWifiVar.arBssInfo[prScanDoneMsg->ucNetTypeIndex];
    ASSERT(prBssInfo);

    DBGLOG(RLM, INFO, ("OBSS Scan Done (NetIdx=%d, Mode=%d)\n",
        prScanDoneMsg->ucNetTypeIndex, prBssInfo->eCurrentOPMode));

    cnmMemFree(prAdapter, prMsgHdr);

#if CFG_ENABLE_WIFI_DIRECT
    /* AP mode */
    if ((prAdapter->fgIsP2PRegistered) &&
        (IS_NET_ACTIVE(prAdapter, prBssInfo->ucNetTypeIndex)) &&
        (prBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT)) {
        return;
    }
#endif

    /* STA mode */
    if (prBssInfo->eCurrentOPMode != OP_MODE_INFRASTRUCTURE ||
        !RLM_NET_PARAM_VALID(prBssInfo) || prBssInfo->u2ObssScanInterval == 0) {
        DBGLOG(RLM, WARN, ("OBSS Scan Done (NetIdx=%d) -- Aborted!!\n",
            prBssInfo->ucNetTypeIndex));
        return;
    }

    /* To do: check 2.4G channel list to decide if obss mgmt should be
     *        sent to associated AP. Note: how to handle concurrent network?
     * To do: invoke rlmObssChnlLevel() to decide if 20/40 BSS coexistence
     *        management frame is needed.
     */
    if ((prBssInfo->auc2G_20mReqChnlList[0] > 0 ||
         prBssInfo->auc2G_NonHtChnlList[0] > 0) &&
        (prMsduInfo = (P_MSDU_INFO_T) cnmMgtPktAlloc(prAdapter,
                      MAC_TX_RESERVED_FIELD + PUBLIC_ACTION_MAX_LEN)) != NULL) {

        DBGLOG(RLM, INFO, ("Send 20/40 coexistence mgmt(20mReq=%d, NonHt=%d)\n",
            prBssInfo->auc2G_20mReqChnlList[0],
            prBssInfo->auc2G_NonHtChnlList[0]));

        prTxFrame = (P_ACTION_20_40_COEXIST_FRAME)
            ((UINT_32)(prMsduInfo->prPacket) + MAC_TX_RESERVED_FIELD);

        prTxFrame->u2FrameCtrl = MAC_FRAME_ACTION;
        COPY_MAC_ADDR(prTxFrame->aucDestAddr, prBssInfo->aucBSSID);
        COPY_MAC_ADDR(prTxFrame->aucSrcAddr, prBssInfo->aucOwnMacAddr);
        COPY_MAC_ADDR(prTxFrame->aucBSSID, prBssInfo->aucBSSID);

        prTxFrame->ucCategory = CATEGORY_PUBLIC_ACTION;
        prTxFrame->ucAction = ACTION_PUBLIC_20_40_COEXIST;

        /* To do: find correct algorithm */
        prTxFrame->rBssCoexist.ucId = ELEM_ID_20_40_BSS_COEXISTENCE;
        prTxFrame->rBssCoexist.ucLength = 1;
        prTxFrame->rBssCoexist.ucData =
            (prBssInfo->auc2G_20mReqChnlList[0] > 0) ? BSS_COEXIST_20M_REQ : 0;

        u2PayloadLen = 2 + 3;

        if (prBssInfo->auc2G_NonHtChnlList[0] > 0) {
            ASSERT(prBssInfo->auc2G_NonHtChnlList[0] <= CHNL_LIST_SZ_2G);

            prTxFrame->rChnlReport.ucId = ELEM_ID_20_40_INTOLERANT_CHNL_REPORT;
            prTxFrame->rChnlReport.ucLength =
                prBssInfo->auc2G_NonHtChnlList[0] + 1;
            prTxFrame->rChnlReport.ucRegulatoryClass = 81; /* 2.4GHz, ch1~13 */
            for (i = 0; i < prBssInfo->auc2G_NonHtChnlList[0] &&
                 i < CHNL_LIST_SZ_2G; i++) {
                prTxFrame->rChnlReport.aucChannelList[i] =
                    prBssInfo->auc2G_NonHtChnlList[i+1];
            }

            u2PayloadLen += IE_SIZE(&prTxFrame->rChnlReport);
        }
        ASSERT((WLAN_MAC_HEADER_LEN + u2PayloadLen) <= PUBLIC_ACTION_MAX_LEN);

        /* Clear up channel lists in 2.4G band */
        prBssInfo->auc2G_20mReqChnlList[0] = 0;
        prBssInfo->auc2G_NonHtChnlList[0] = 0;


        //4 Update information of MSDU_INFO_T
        prMsduInfo->ucPacketType = HIF_TX_PACKET_TYPE_MGMT;   /* Management frame */
        prMsduInfo->ucStaRecIndex = prBssInfo->prStaRecOfAP->ucIndex;
        prMsduInfo->ucNetworkType = prBssInfo->ucNetTypeIndex;
        prMsduInfo->ucMacHeaderLength = WLAN_MAC_MGMT_HEADER_LEN;
        prMsduInfo->fgIs802_1x = FALSE;
        prMsduInfo->fgIs802_11 = TRUE;
        prMsduInfo->u2FrameLength = WLAN_MAC_MGMT_HEADER_LEN + u2PayloadLen;
        prMsduInfo->ucTxSeqNum = nicIncreaseTxSeqNum(prAdapter);
        prMsduInfo->pfTxDoneHandler = NULL;
        prMsduInfo->fgIsBasicRate = FALSE;

        //4 Enqueue the frame to send this action frame.
        nicTxEnqueueMsdu(prAdapter, prMsduInfo);
    } /* end of prMsduInfo != NULL */

    if (prBssInfo->u2ObssScanInterval > 0) {
        DBGLOG(RLM, INFO, ("Set OBSS timer (NetIdx=%d, %d sec)\n",
            prBssInfo->ucNetTypeIndex, prBssInfo->u2ObssScanInterval));

        cnmTimerStartTimer(prAdapter, &prBssInfo->rObssScanTimer,
            prBssInfo->u2ObssScanInterval * MSEC_PER_SEC);
    }
}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
static VOID
rlmObssScanTimeout (
    P_ADAPTER_T prAdapter,
    UINT_32     u4Data
    )
{
    P_BSS_INFO_T        prBssInfo;

    prBssInfo = (P_BSS_INFO_T) u4Data;
    ASSERT(prBssInfo);

#if CFG_ENABLE_WIFI_DIRECT
    /* AP mode */
    if (prAdapter->fgIsP2PRegistered &&
        (IS_NET_ACTIVE(prAdapter, prBssInfo->ucNetTypeIndex)) &&
        (prBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT)) {

        prBssInfo->fgObssActionForcedTo20M = FALSE;

        /* Check if Beacon content need to be updated */
        rlmUpdateParamsForAP(prAdapter, prBssInfo, FALSE);
        
        return;
    }
#endif /* end of CFG_ENABLE_WIFI_DIRECT */


    /* STA mode */
    if (prBssInfo->eCurrentOPMode != OP_MODE_INFRASTRUCTURE ||
        !RLM_NET_PARAM_VALID(prBssInfo) || prBssInfo->u2ObssScanInterval == 0) {
        DBGLOG(RLM, WARN, ("OBSS Scan timeout (NetIdx=%d) -- Aborted!!\n",
            prBssInfo->ucNetTypeIndex));
        return;
    }

    rlmObssTriggerScan(prAdapter, prBssInfo);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
rlmObssTriggerScan (
    P_ADAPTER_T         prAdapter,
    P_BSS_INFO_T        prBssInfo
    )
{
    P_MSG_SCN_SCAN_REQ  prScanReqMsg;

    ASSERT(prBssInfo);

    prScanReqMsg = (P_MSG_SCN_SCAN_REQ)
            cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_SCN_SCAN_REQ));
    ASSERT(prScanReqMsg);

    if (!prScanReqMsg) {
        DBGLOG(RLM, WARN, ("No buf for OBSS scan (NetIdx=%d)!!\n",
            prBssInfo->ucNetTypeIndex));

        cnmTimerStartTimer(prAdapter, &prBssInfo->rObssScanTimer,
            prBssInfo->u2ObssScanInterval * MSEC_PER_SEC);
        return;
    }

    /* It is ok that ucSeqNum is set to fixed value because the same network
     * OBSS scan interval is limited to OBSS_SCAN_MIN_INTERVAL (min 10 sec)
     * and scan module don't care seqNum of OBSS scanning
     */
    prScanReqMsg->rMsgHdr.eMsgId = MID_RLM_SCN_SCAN_REQ;
    prScanReqMsg->ucSeqNum = 0x33;
    prScanReqMsg->ucNetTypeIndex = prBssInfo->ucNetTypeIndex;
    prScanReqMsg->eScanType = SCAN_TYPE_ACTIVE_SCAN;
    prScanReqMsg->ucSSIDType = SCAN_REQ_SSID_WILDCARD;
    prScanReqMsg->ucSSIDLength = 0;
    prScanReqMsg->eScanChannel = SCAN_CHANNEL_2G4;
    prScanReqMsg->u2IELen = 0;

    mboxSendMsg(prAdapter,
                MBOX_ID_0,
                (P_MSG_HDR_T) prScanReqMsg,
                MSG_SEND_METHOD_BUF);

    DBGLOG(RLM, INFO, ("Timeout to trigger OBSS scan (NetIdx=%d)!!\n",
        prBssInfo->ucNetTypeIndex));
}


