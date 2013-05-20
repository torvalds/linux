/*
** $Id: //Department/DaVinci/TRUNK/MT6620_5931_WiFi_Driver/mgmt/wnm.c#1 $
*/

/*! \file   "wnm.c"
    \brief  This file includes the 802.11v default vale and functions.
*/



/*
** $Log: wnm.c $
 *
 * 01 05 2012 tsaiyuan.hsu
 * [WCXRP00001157] [MT6620 Wi-Fi][FW][DRV] add timing measurement support for 802.11v
 * add timing measurement support for 802.11v.
 *
 * 
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

#if CFG_SUPPORT_802_11V

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

#define WNM_MAX_TOD_ERROR  0
#define WNM_MAX_TOA_ERROR  0
#define MICRO_TO_10NANO(x) ((x)*100)
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

static UINT_8 ucTimingMeasToken = 0;

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

WLAN_STATUS
wnmRunEventTimgingMeasTxDone (
    IN P_ADAPTER_T              prAdapter,
    IN P_MSDU_INFO_T            prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T    rTxDoneStatus
    );

VOID
wnmComposeTimingMeasFrame (
    IN P_ADAPTER_T         prAdapter,    
    IN P_STA_RECORD_T      prStaRec,
    IN PFN_TX_DONE_HANDLER pfTxDoneHandler
    );

VOID
wnmTimingMeasRequest (
    IN P_ADAPTER_T                  prAdapter,
    IN P_SW_RFB_T                   prSwRfb
    );
/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
*
* \brief This routine is called to process the 802.11v wnm category action frame.
*
*
* \note
*      Called by: Handle Rx mgmt request
*/
/*----------------------------------------------------------------------------*/
VOID
wnmWNMAction (
    IN P_ADAPTER_T                  prAdapter,
    IN P_SW_RFB_T                   prSwRfb
    )
{
    P_WLAN_ACTION_FRAME prRxFrame;

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    prRxFrame = (P_WLAN_ACTION_FRAME) prSwRfb->pvHeader;

#if CFG_SUPPORT_802_11V_TIMING_MEASUREMENT
    if (prRxFrame->ucAction == ACTION_WNM_TIMING_MEASUREMENT_REQUEST) {
        wnmTimingMeasRequest(prAdapter, prSwRfb);
        return;
    }
#endif

    DBGLOG(WNM, TRACE, ("Unsupport WNM action frame: %d\n", prRxFrame->ucAction));
}

#if CFG_SUPPORT_802_11V_TIMING_MEASUREMENT
/*----------------------------------------------------------------------------*/
/*!
*
* \brief This routine is called to report timing measurement data.
*
*/
/*----------------------------------------------------------------------------*/
VOID
wnmReportTimingMeas (
    IN P_ADAPTER_T         prAdapter,
    IN UINT_8              ucStaRecIndex,
    IN UINT_32             u4ToD,
    IN UINT_32             u4ToA
    )
{	      
    P_STA_RECORD_T prStaRec;    

    prStaRec = cnmGetStaRecByIndex(prAdapter, ucStaRecIndex);

    if ((!prStaRec) || (!prStaRec->fgIsInUse)) {
        return;
    }    
    
    DBGLOG(WNM, TRACE, ("wnmReportTimingMeas: u4ToD %x u4ToA %x", u4ToD, u4ToA));
                 
    if (!prStaRec->rWNMTimingMsmt.ucTrigger)
    	  return;
    
    prStaRec->rWNMTimingMsmt.u4ToD = MICRO_TO_10NANO(u4ToD);
    prStaRec->rWNMTimingMsmt.u4ToA = MICRO_TO_10NANO(u4ToA);
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This function will handle TxDone(TimingMeasurement) Event.
*
* @param[in] prAdapter      Pointer to the Adapter structure.
* @param[in] prMsduInfo     Pointer to the MSDU_INFO_T.
* @param[in] rTxDoneStatus  Return TX status of the Timing Measurement frame.
*
* @retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wnmRunEventTimgingMeasTxDone (
    IN P_ADAPTER_T              prAdapter,
    IN P_MSDU_INFO_T            prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T    rTxDoneStatus
    )
{
    P_STA_RECORD_T prStaRec;    

    ASSERT(prAdapter);
    ASSERT(prMsduInfo);

    DBGLOG(WNM, LOUD, ("EVENT-TX DONE: Current Time = %ld\n", kalGetTimeTick()));

    prStaRec = cnmGetStaRecByIndex(prAdapter, prMsduInfo->ucStaRecIndex);

    if ((!prStaRec) || (!prStaRec->fgIsInUse)) {
        return WLAN_STATUS_SUCCESS; /* For the case of replying ERROR STATUS CODE */
    }

    DBGLOG(WNM, TRACE, ("wnmRunEventTimgingMeasTxDone: ucDialog %d ucFollowUp %d u4ToD %x u4ToA %x",
                      prStaRec->rWNMTimingMsmt.ucDialogToken,
                      prStaRec->rWNMTimingMsmt.ucFollowUpDialogToken,
                      prStaRec->rWNMTimingMsmt.u4ToD,
                      prStaRec->rWNMTimingMsmt.u4ToA));

    prStaRec->rWNMTimingMsmt.ucFollowUpDialogToken = prStaRec->rWNMTimingMsmt.ucDialogToken;
    prStaRec->rWNMTimingMsmt.ucDialogToken = ++ucTimingMeasToken;            
           
    wnmComposeTimingMeasFrame(prAdapter, prStaRec, NULL);

    return WLAN_STATUS_SUCCESS;

} /* end of wnmRunEventTimgingMeasTxDone() */

/*----------------------------------------------------------------------------*/
/*!
* @brief This function will compose the Timing Measurement frame.
*
* @param[in] prAdapter              Pointer to the Adapter structure.
* @param[in] prStaRec               Pointer to the STA_RECORD_T.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
wnmComposeTimingMeasFrame (
    IN P_ADAPTER_T         prAdapter,    
    IN P_STA_RECORD_T      prStaRec,
    IN PFN_TX_DONE_HANDLER pfTxDoneHandler
    )
{
    P_MSDU_INFO_T prMsduInfo;
	  P_BSS_INFO_T prBssInfo;
    P_ACTION_UNPROTECTED_WNM_TIMING_MEAS_FRAME prTxFrame;
    UINT_16 u2PayloadLen;

    prBssInfo = &prAdapter->rWifiVar.arBssInfo[prStaRec->ucNetTypeIndex];
    ASSERT(prBssInfo);

    prMsduInfo = (P_MSDU_INFO_T) cnmMgtPktAlloc(prAdapter,
                      MAC_TX_RESERVED_FIELD + PUBLIC_ACTION_MAX_LEN);

    if (!prMsduInfo)
        return;

    prTxFrame = (P_ACTION_UNPROTECTED_WNM_TIMING_MEAS_FRAME)
        ((UINT_32)(prMsduInfo->prPacket) + MAC_TX_RESERVED_FIELD);

    prTxFrame->u2FrameCtrl = MAC_FRAME_ACTION;
    
    COPY_MAC_ADDR(prTxFrame->aucDestAddr, prStaRec->aucMacAddr);
    COPY_MAC_ADDR(prTxFrame->aucSrcAddr, prBssInfo->aucOwnMacAddr);
    COPY_MAC_ADDR(prTxFrame->aucBSSID, prBssInfo->aucBSSID);

    prTxFrame->ucCategory = CATEGORY_UNPROTECTED_WNM_ACTION;
    prTxFrame->ucAction = ACTION_UNPROTECTED_WNM_TIMING_MEASUREMENT;

    //3 Compose the frame body's frame.
    prTxFrame->ucDialogToken = prStaRec->rWNMTimingMsmt.ucDialogToken;
    prTxFrame->ucFollowUpDialogToken = prStaRec->rWNMTimingMsmt.ucFollowUpDialogToken;
    prTxFrame->u4ToD = prStaRec->rWNMTimingMsmt.u4ToD;
    prTxFrame->u4ToA = prStaRec->rWNMTimingMsmt.u4ToA;
    prTxFrame->ucMaxToDErr = WNM_MAX_TOD_ERROR;
    prTxFrame->ucMaxToAErr = WNM_MAX_TOA_ERROR;
    
    u2PayloadLen = 2 + ACTION_UNPROTECTED_WNM_TIMING_MEAS_LEN;

    //4 Update information of MSDU_INFO_T
    prMsduInfo->ucPacketType = HIF_TX_PACKET_TYPE_MGMT;   /* Management frame */
    prMsduInfo->ucStaRecIndex = prStaRec->ucIndex;
    prMsduInfo->ucNetworkType = prStaRec->ucNetTypeIndex;
    prMsduInfo->ucMacHeaderLength = WLAN_MAC_MGMT_HEADER_LEN;
    prMsduInfo->fgIs802_1x = FALSE;
    prMsduInfo->fgIs802_11 = TRUE;
    prMsduInfo->u2FrameLength = WLAN_MAC_MGMT_HEADER_LEN + u2PayloadLen;
    prMsduInfo->ucTxSeqNum = nicIncreaseTxSeqNum(prAdapter);
    prMsduInfo->pfTxDoneHandler = pfTxDoneHandler;
    prMsduInfo->fgIsBasicRate = FALSE;   

    DBGLOG(WNM, TRACE, ("wnmComposeTimingMeasFrame: ucDialogToken %d ucFollowUpDialogToken %d u4ToD %x u4ToA %x\n",
           prTxFrame->ucDialogToken, prTxFrame->ucFollowUpDialogToken,
           prTxFrame->u4ToD, prTxFrame->u4ToA));

    //4 Enqueue the frame to send this action frame.
    nicTxEnqueueMsdu(prAdapter, prMsduInfo);

    return;

} /* end of wnmComposeTimingMeasFrame() */

/*----------------------------------------------------------------------------*/
/*!
*
* \brief This routine is called to process the 802.11v timing measurement request.
*
*
* \note
*      Handle Rx mgmt request
*/
/*----------------------------------------------------------------------------*/
VOID
wnmTimingMeasRequest (
    IN P_ADAPTER_T                  prAdapter,
    IN P_SW_RFB_T                   prSwRfb
    )
{	      
    P_ACTION_WNM_TIMING_MEAS_REQ_FRAME prRxFrame = NULL;
    P_STA_RECORD_T prStaRec;

    prRxFrame = (P_ACTION_WNM_TIMING_MEAS_REQ_FRAME)prSwRfb->pvHeader;
    if (!prRxFrame)
        return;

    prStaRec = cnmGetStaRecByIndex(prAdapter, prSwRfb->ucStaRecIdx);
    if ((!prStaRec) || (!prStaRec->fgIsInUse)) {
        return;
    }

    DBGLOG(WNM, TRACE, ("IEEE 802.11: Received Timing Measuremen Request from "
           MACSTR"\n", MAC2STR(prStaRec->aucMacAddr)));

    // reset timing msmt    
    prStaRec->rWNMTimingMsmt.fgInitiator = TRUE;
    prStaRec->rWNMTimingMsmt.ucTrigger = prRxFrame->ucTrigger;
    if (!prRxFrame->ucTrigger)
    	  return;
    
    prStaRec->rWNMTimingMsmt.ucDialogToken = ++ucTimingMeasToken;
    prStaRec->rWNMTimingMsmt.ucFollowUpDialogToken = 0;
    
    wnmComposeTimingMeasFrame(prAdapter, prStaRec, wnmRunEventTimgingMeasTxDone);
}

#if WNM_UNIT_TEST
VOID wnmTimingMeasUnitTest1(P_ADAPTER_T prAdapter, UINT_8 ucStaRecIndex)
{
    P_STA_RECORD_T prStaRec;
    
    prStaRec = cnmGetStaRecByIndex(prAdapter, ucStaRecIndex);
    if ((!prStaRec) || (!prStaRec->fgIsInUse)) {
        return;
    }

    DBGLOG(WNM, INFO, ("IEEE 802.11v: Test Timing Measuremen Request from "
           MACSTR"\n", MAC2STR(prStaRec->aucMacAddr)));

    prStaRec->rWNMTimingMsmt.fgInitiator = TRUE;
    prStaRec->rWNMTimingMsmt.ucTrigger = 1;
    
    prStaRec->rWNMTimingMsmt.ucDialogToken = ++ucTimingMeasToken;
    prStaRec->rWNMTimingMsmt.ucFollowUpDialogToken = 0;

    wnmComposeTimingMeasFrame(prAdapter, prStaRec, wnmRunEventTimgingMeasTxDone);
}
#endif

#endif /* CFG_SUPPORT_802_11V_TIMING_MEASUREMENT */

#endif /* CFG_SUPPORT_802_11V */
