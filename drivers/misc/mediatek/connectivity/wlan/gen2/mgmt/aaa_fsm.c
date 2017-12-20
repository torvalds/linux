/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/mgmt/aaa_fsm.c#2
*/

/*! \file   "aaa_fsm.c"
    \brief  This file defines the FSM for AAA MODULE.

    This file defines the FSM for AAA MODULE.
*/

/*
** Log: aaa_fsm.c
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 06 13 2012 yuche.tsai
 * NULL
 * Update maintrunk driver.
 * Add support for driver compose assoc request frame.
 *
 * 03 02 2012 terry.wu
 * NULL
 * Sync CFG80211 modification from branch 2,2.
 *
 * 02 22 2012 yuche.tsai
 * NULL
 * Solve sigma test 5.1.3 issue, assoc response should have P2P IE.
 *
 * 12 02 2011 yuche.tsai
 * NULL
 * Resolve inorder issue under AP mode.
 *
 * data frame may TX before assoc response frame.
 *
 * 11 18 2011 yuche.tsai
 * NULL
 * CONFIG P2P support RSSI query, default turned off.
 *
 * 06 17 2011 terry.wu
 * NULL
 * Add BoW 11N support.
 *
 * 06 02 2011 eddie.chen
 * [WCXRP00000759] [MT6620 Wi-Fi][DRV] Update RCPI in AAA
 * Update RCPI when receiving Assoc request.
 *
 * 04 21 2011 terry.wu
 * [WCXRP00000674] [MT6620 Wi-Fi][Driver] Refine AAA authSendAuthFrame
 * Add network type parameter to authSendAuthFrame.
 *
 * 04 15 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Add BOW short range mode.
 *
 * 04 09 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Change Link connection event procedure and change skb length check to 1512 bytes.
 *
 * 03 09 2011 wh.su
 * [WCXRP00000530] [MT6620 Wi-Fi] [Driver] skip doing p2pRunEventAAAComplete after send assoc response Tx Done
 * Skip to call p2pRunEventAAAComplete to avoid indicate STA connect twice.
 *
 * 03 04 2011 terry.wu
 * [WCXRP00000515] [MT6620 Wi-Fi][Driver] Surpress compiler warning which is identified by GNU compiler collection
 * Remove unused variable.
 *
 * 02 16 2011 yuche.tsai
 * [WCXRP00000429] [Volunteer Patch][MT6620][Driver] Hot Spot Client Limit Issue
 * Add more check after RX assoc frame under Hot-Spot mode.
 *
 * 02 09 2011 yuche.tsai
 * [WCXRP00000429] [Volunteer Patch][MT6620][Driver] Hot Spot Client Limit Issue
 * Fix Client Limit Issue.
 *
 * 01 25 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * Change Station Type in Station Record, Modify MACRO definition for getting station type & network type index & Role.
 *
 * 01 15 2011 puff.wen
 * NULL
 * [On behalf of Frog] Add CFG_ENABLE_WIFI_DIRECT to p2pRunEventAAAComplete
 *
 * 01 14 2011 yuche.tsai
 * [WCXRP00000352] [Volunteer Patch][MT6620][Driver] P2P Statsion Record Client List Issue
 * Modify AAA flow according to CM's comment.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 29 2010 yuche.tsai
 * NULL
 * Fix Compile warning, type cast from UINT_32 to UINT_16.
 *
 * 08 26 2010 yuche.tsai
 * NULL
 * In P2P AT GO test mode under WinXP, we would not indicate connected event to host.
 *
 * 08 24 2010 cm.chang
 * NULL
 * Support RLM initail channel of Ad-hoc, P2P and BOW
 *
 * 08 23 2010 chinghwa.yu
 * NULL
 * Update for BOW.
 *
 * 08 20 2010 kevin.huang
 * NULL
 * Modify AAA Module for changing STA STATE 3 at p2p/bowRunEventAAAComplete()
 *
 * 08 17 2010 yuche.tsai
 * NULL
 * Fix bug while enabling P2P GO.
 *
 * 08 16 2010 kevin.huang
 * NULL
 * Refine AAA functions
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * refine TX-DONE callback.
 *
 * 06 21 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * modify due to P2P functino call prototype change.
 *
 * 06 17 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * First draft for migration P2P FSM from FW to Driver.
 *
 * 04 02 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Modify CFG flags
 *
 * 02 26 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * add support of Driver STA_RECORD_T activation
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
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

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#if 0
/*----------------------------------------------------------------------------*/
/*!
* @brief This function will send Event to AIS/BOW/P2P
*
* @param[in] rJoinStatus        To indicate JOIN success or failure.
* @param[in] prStaRec           Pointer to the STA_RECORD_T
* @param[in] prSwRfb            Pointer to the SW_RFB_T

* @return none
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS aaaFsmSendEventJoinComplete(WLAN_STATUS rJoinStatus, P_STA_RECORD_T prStaRec, P_SW_RFB_T prSwRfb)
{
	P_MSG_SAA_JOIN_COMP_T prJoinCompMsg;

	ASSERT(prStaRec);

	prJoinCompMsg = cnmMemAlloc(RAM_TYPE_TCM, sizeof(MSG_SAA_JOIN_COMP_T));
	if (!prJoinCompMsg)
		return WLAN_STATUS_RESOURCES;

	if (IS_STA_IN_AIS(prStaRec))
		prJoinCompMsg->rMsgHdr.eMsgId = MID_SAA_AIS_JOIN_COMPLETE;
	else if (IS_STA_IN_P2P(prStaRec))
		prJoinCompMsg->rMsgHdr.eMsgId = MID_SAA_P2P_JOIN_COMPLETE;
	else if (IS_STA_IN_BOW(prStaRec))
		prJoinCompMsg->rMsgHdr.eMsgId = MID_SAA_BOW_JOIN_COMPLETE;
	else
		ASSERT(0);

	prJoinCompMsg->rJoinStatus = rJoinStatus;
	prJoinCompMsg->prStaRec = prStaRec;
	prJoinCompMsg->prSwRfb = prSwRfb;

	mboxSendMsg(MBOX_ID_0, (P_MSG_HDR_T) prJoinCompMsg, MSG_SEND_METHOD_BUF);

	return WLAN_STATUS_SUCCESS;

}				/* end of saaFsmSendEventJoinComplete() */

/*----------------------------------------------------------------------------*/
/*!
* @brief This function will handle the Start Event to AAA FSM.
*
* @param[in] prMsgHdr   Message of Join Request for a particular STA.
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID aaaFsmRunEventStart(IN P_MSG_HDR_T prMsgHdr)
{
	P_MSG_SAA_JOIN_REQ_T prJoinReqMsg;
	P_STA_RECORD_T prStaRec;
	P_AIS_BSS_INFO_T prAisBssInfo;

	ASSERT(prMsgHdr);

	prJoinReqMsg = (P_MSG_SAA_JOIN_REQ_T) prMsgHdr;
	prStaRec = prJoinReqMsg->prStaRec;

	ASSERT(prStaRec);

	DBGLOG(SAA, LOUD, "EVENT-START: Trigger SAA FSM\n");

	cnmMemFree(prMsgHdr);

	/* 4 <1> Validation of SAA Start Event */
	if (!IS_AP_STA(prStaRec->eStaType)) {

		DBGLOG(SAA, ERROR, "EVENT-START: STA Type - %d was not supported.\n", prStaRec->eStaType);

		/* Ignore the return value because don't care the prSwRfb */
		saaFsmSendEventJoinComplete(WLAN_STATUS_FAILURE, prStaRec, NULL);

		return;
	}
	/* 4 <2> The previous JOIN process is not completed ? */
	if (prStaRec->eAuthAssocState != AA_STATE_IDLE) {
		DBGLOG(SAA, ERROR, "EVENT-START: Reentry of SAA Module.\n");
		prStaRec->eAuthAssocState = AA_STATE_IDLE;
	}
	/* 4 <3> Reset Status Code and Time */
	/* Update Station Record - Status/Reason Code */
	prStaRec->u2StatusCode = STATUS_CODE_SUCCESSFUL;

	/* Update the record join time. */
	GET_CURRENT_SYSTIME(&prStaRec->rLastJoinTime);

	prStaRec->ucTxAuthAssocRetryCount = 0;

	if (prStaRec->prChallengeText) {
		cnmMemFree(prStaRec->prChallengeText);
		prStaRec->prChallengeText = (P_IE_CHALLENGE_TEXT_T) NULL;
	}

	cnmTimerStopTimer(&prStaRec->rTxReqDoneOrRxRespTimer);

	prStaRec->ucStaState = STA_STATE_1;

	/* Trigger SAA MODULE */
	saaFsmSteps(prStaRec, SAA_STATE_SEND_AUTH1, (P_SW_RFB_T) NULL);

}				/* end of saaFsmRunEventStart() */
#endif

#if CFG_SUPPORT_AAA
/*----------------------------------------------------------------------------*/
/*!
* @brief This function will process the Rx Auth Request Frame and then
*        trigger AAA FSM.
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] prSwRfb            Pointer to the SW_RFB_T structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID aaaFsmRunEventRxAuth(IN P_ADAPTER_T prAdapter, IN P_SW_RFB_T prSwRfb)
{
	P_BSS_INFO_T prBssInfo;
	P_STA_RECORD_T prStaRec = (P_STA_RECORD_T) NULL;
	UINT_16 u2StatusCode;
	BOOLEAN fgReplyAuth = FALSE;
	ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex;

	ASSERT(prAdapter);

	do {

		/* 4 <1> Check P2P network conditions */
#if CFG_ENABLE_WIFI_DIRECT
		if (prAdapter->fgIsP2PRegistered) {
			prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);

			if (prBssInfo->fgIsNetActive) {

				/* 4 <1.1> Validate Auth Frame by Auth Algorithm/Transation Seq */
				if (WLAN_STATUS_SUCCESS ==
				    authProcessRxAuth1Frame(prAdapter,
							    prSwRfb,
							    prBssInfo->aucBSSID,
							    AUTH_ALGORITHM_NUM_OPEN_SYSTEM,
							    AUTH_TRANSACTION_SEQ_1, &u2StatusCode)) {

					if (STATUS_CODE_SUCCESSFUL == u2StatusCode) {
						/* 4 <1.2> Validate Auth Frame for Network Specific Conditions */
						fgReplyAuth = p2pFuncValidateAuth(prAdapter,
										  prSwRfb, &prStaRec, &u2StatusCode);
					} else {
						fgReplyAuth = TRUE;
					}
					eNetTypeIndex = NETWORK_TYPE_P2P_INDEX;
					break;
				}
			}
		}
#endif /* CFG_ENABLE_WIFI_DIRECT */

		/* 4 <2> Check BOW network conditions */
#if CFG_ENABLE_BT_OVER_WIFI
		{
			prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_BOW_INDEX]);

			if ((prBssInfo->fgIsNetActive) && (OP_MODE_BOW == prBssInfo->eCurrentOPMode)) {

				/* 4 <2.1> Validate Auth Frame by Auth Algorithm/Transation Seq */
				/* Check if for this BSSID */
				if (WLAN_STATUS_SUCCESS ==
				    authProcessRxAuth1Frame(prAdapter,
							    prSwRfb,
							    prBssInfo->aucBSSID,
							    AUTH_ALGORITHM_NUM_OPEN_SYSTEM,
							    AUTH_TRANSACTION_SEQ_1, &u2StatusCode)) {

					if (STATUS_CODE_SUCCESSFUL == u2StatusCode) {

						/* 4 <2.2> Validate Auth Frame for Network Specific Conditions */
						fgReplyAuth =
						    bowValidateAuth(prAdapter, prSwRfb, &prStaRec, &u2StatusCode);

					} else {

						fgReplyAuth = TRUE;
					}
					eNetTypeIndex = NETWORK_TYPE_BOW_INDEX;
					/* TODO(Kevin): Allocate a STA_RECORD_T for new client */
					break;
				}
			}
		}
#endif /* CFG_ENABLE_BT_OVER_WIFI */

		return;
	} while (FALSE);

	if (prStaRec) {
		/* update RCPI */
		prStaRec->ucRCPI = prSwRfb->prHifRxHdr->ucRcpi;
	}
	/* 4 <3> Update STA_RECORD_T and reply Auth_2(Response to Auth_1) Frame */
	if (fgReplyAuth) {

		if (prStaRec) {

			if (u2StatusCode == STATUS_CODE_SUCCESSFUL) {
				if (prStaRec->eAuthAssocState != AA_STATE_IDLE) {
					DBGLOG(AAA, WARN, "Previous AuthAssocState (%d) != IDLE.\n",
							   prStaRec->eAuthAssocState);
				}

				prStaRec->eAuthAssocState = AAA_STATE_SEND_AUTH2;
			} else {
				prStaRec->eAuthAssocState = AA_STATE_IDLE;

				/* NOTE(Kevin): Change to STATE_1 */
				cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);
			}

			/* Update the record join time. */
			GET_CURRENT_SYSTIME(&prStaRec->rUpdateTime);

			/* Update Station Record - Status/Reason Code */
			prStaRec->u2StatusCode = u2StatusCode;

			prStaRec->ucAuthAlgNum = AUTH_ALGORITHM_NUM_OPEN_SYSTEM;
		} else {
			/* NOTE(Kevin): We should have STA_RECORD_T if the status code was successful */
			ASSERT(!(u2StatusCode == STATUS_CODE_SUCCESSFUL));
		}

		/* NOTE: Ignore the return status for AAA */
		/* 4 <4> Reply  Auth */
		authSendAuthFrame(prAdapter, prStaRec, eNetTypeIndex, prSwRfb, AUTH_TRANSACTION_SEQ_2, u2StatusCode);

	}

}				/* end of aaaFsmRunEventRxAuth() */

/*----------------------------------------------------------------------------*/
/*!
* @brief This function will process the Rx (Re)Association Request Frame and then
*        trigger AAA FSM.
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] prSwRfb            Pointer to the SW_RFB_T structure.
*
* @retval WLAN_STATUS_SUCCESS           Always return success
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS aaaFsmRunEventRxAssoc(IN P_ADAPTER_T prAdapter, IN P_SW_RFB_T prSwRfb)
{
	P_BSS_INFO_T prBssInfo;
	P_STA_RECORD_T prStaRec = (P_STA_RECORD_T) NULL;
	UINT_16 u2StatusCode = STATUS_CODE_RESERVED;
	BOOLEAN fgReplyAssocResp = FALSE;

	ASSERT(prAdapter);

	do {

		/* 4 <1> Check if we have the STA_RECORD_T for incoming Assoc Req */
		prStaRec = cnmGetStaRecByIndex(prAdapter, prSwRfb->ucStaRecIdx);

		/* We should have the corresponding Sta Record. */
		if ((!prStaRec) || (!prStaRec->fgIsInUse)) {
			ASSERT(0);	/* Only for debug phase */
			break;
		}

		if (!IS_CLIENT_STA(prStaRec))
			break;

		if (prStaRec->ucStaState == STA_STATE_3) {
			/* Do Reassocation */
		} else if ((prStaRec->ucStaState == STA_STATE_2) &&
			   (prStaRec->eAuthAssocState == AAA_STATE_SEND_AUTH2)) {
			/* Normal case */
		} else {
			DBGLOG(AAA, INFO, "Previous AuthAssocState (%d) != SEND_AUTH2, ucStaState:%d.\n",
				prStaRec->eAuthAssocState,
				prStaRec->ucStaState);
			/* TODO: Why assoc req event is faster than tx done of auth */
			if (prStaRec->eAuthAssocState != AAA_STATE_SEND_AUTH2)
				break;
		}

		/* update RCPI */
		prStaRec->ucRCPI = prSwRfb->prHifRxHdr->ucRcpi;

		/* 4 <2> Check P2P network conditions */
#if CFG_ENABLE_WIFI_DIRECT
		if ((prAdapter->fgIsP2PRegistered) && (IS_STA_IN_P2P(prStaRec))) {

			prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);

			if (prBssInfo->fgIsNetActive) {

				/* 4 <2.1> Validate Assoc Req Frame and get Status Code */
				/* Check if for this BSSID */
				if (WLAN_STATUS_SUCCESS ==
				    assocProcessRxAssocReqFrame(prAdapter, prSwRfb, &u2StatusCode)) {

					if (STATUS_CODE_SUCCESSFUL == u2StatusCode) {
						/* 4 <2.2> Validate Assoc Req  Frame for Network Specific Conditions */
						fgReplyAssocResp = p2pFuncValidateAssocReq(prAdapter,
											   prSwRfb,
											   (PUINT_16)&u2StatusCode);
					} else {
						fgReplyAssocResp = TRUE;
					}

					break;
				}
			}
		}
#endif /* CFG_ENABLE_WIFI_DIRECT */

		/* 4 <3> Check BOW network conditions */
#if CFG_ENABLE_BT_OVER_WIFI
		if (IS_STA_IN_BOW(prStaRec)) {

			prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_BOW_INDEX]);

			if ((prBssInfo->fgIsNetActive) && (OP_MODE_BOW == prBssInfo->eCurrentOPMode)) {

				/* 4 <3.1> Validate Auth Frame by Auth Algorithm/Transation Seq */
				/* Check if for this BSSID */
				if (WLAN_STATUS_SUCCESS ==
				    assocProcessRxAssocReqFrame(prAdapter, prSwRfb, &u2StatusCode)) {

					if (STATUS_CODE_SUCCESSFUL == u2StatusCode) {

						/* 4 <3.2> Validate Auth Frame for Network Specific Conditions */
						fgReplyAssocResp =
						    bowValidateAssocReq(prAdapter, prSwRfb, &u2StatusCode);

					} else {

						fgReplyAssocResp = TRUE;
					}

					/* TODO(Kevin): Allocate a STA_RECORD_T for new client */
					break;
				}
			}
		}
#endif /* CFG_ENABLE_BT_OVER_WIFI */

		return WLAN_STATUS_SUCCESS;	/* To release the SW_RFB_T */
	} while (FALSE);

	/* 4 <4> Update STA_RECORD_T and reply Assoc Resp Frame */
	if (fgReplyAssocResp) {
		UINT_16 u2IELength;
		PUINT_8 pucIE;

		if ((((P_WLAN_ASSOC_REQ_FRAME_T) (prSwRfb->pvHeader))->u2FrameCtrl & MASK_FRAME_TYPE) ==
		    MAC_FRAME_REASSOC_REQ) {

			u2IELength = prSwRfb->u2PacketLen -
			    (UINT_16) OFFSET_OF(WLAN_REASSOC_REQ_FRAME_T, aucInfoElem[0]);

			pucIE = ((P_WLAN_REASSOC_REQ_FRAME_T) (prSwRfb->pvHeader))->aucInfoElem;
		} else {
			u2IELength = prSwRfb->u2PacketLen - (UINT_16) OFFSET_OF(WLAN_ASSOC_REQ_FRAME_T, aucInfoElem[0]);

			pucIE = ((P_WLAN_ASSOC_REQ_FRAME_T) (prSwRfb->pvHeader))->aucInfoElem;
		}

		rlmProcessAssocReq(prAdapter, prSwRfb, pucIE, u2IELength);

		/* 4 <4.1> Assign Association ID */
		if (u2StatusCode == STATUS_CODE_SUCCESSFUL) {

#if CFG_ENABLE_WIFI_DIRECT
			if ((prAdapter->fgIsP2PRegistered) && (IS_STA_IN_P2P(prStaRec))) {
				if (p2pRunEventAAAComplete(prAdapter, prStaRec) == WLAN_STATUS_SUCCESS) {
					prStaRec->u2AssocId = bssAssignAssocID(prStaRec);
					/* prStaRec->eAuthAssocState = AA_STATE_IDLE; */
					prStaRec->eAuthAssocState = AAA_STATE_SEND_ASSOC2;/* NOTE(Kevin): for TX done */

					/* NOTE(Kevin): Method A: Change to STATE_3 before handle TX Done */
					/* cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_3); */
				} else {
					/* Client List FULL. */
					u2StatusCode = STATUS_CODE_REQ_DECLINED;

					prStaRec->u2AssocId = 0;	/* Invalid Association ID */

					/* If (Re)association fail, the peer can try Association w/o Auth immediately */
					prStaRec->eAuthAssocState = AAA_STATE_SEND_AUTH2;

					/* NOTE(Kevin): Better to change state here, not at TX Done */
					cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_2);
				}
			}
#endif

#if CFG_ENABLE_BT_OVER_WIFI
			if ((IS_STA_IN_BOW(prStaRec))) {
				/* if (bowRunEventAAAComplete(prAdapter, prStaRec) == WLAN_STATUS_SUCCESS) { */
				prStaRec->u2AssocId = bssAssignAssocID(prStaRec);
				prStaRec->eAuthAssocState = AAA_STATE_SEND_ASSOC2;	/* NOTE(Kevin): for TX done */

				/* NOTE(Kevin): Method A: Change to STATE_3 before handle TX Done */
				/* cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_3); */
			}
#if 0
			else {
				/* Client List FULL. */
				u2StatusCode = STATUS_CODE_REQ_DECLINED;

				prStaRec->u2AssocId = 0;	/* Invalid Association ID */

				/* If (Re)association fail, the peer can try Association w/o Auth immediately */
				prStaRec->eAuthAssocState = AAA_STATE_SEND_AUTH2;

				/* NOTE(Kevin): Better to change state here, not at TX Done */
				cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_2);
			}
		}
#endif
#endif
	} else {
		prStaRec->u2AssocId = 0;	/* Invalid Association ID */

		/* If (Re)association fail, the peer can try Association w/o Auth immediately */
		prStaRec->eAuthAssocState = AAA_STATE_SEND_AUTH2;

		/* NOTE(Kevin): Better to change state here, not at TX Done */
		cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_2);
	}

	/* Update the record join time. */
	GET_CURRENT_SYSTIME(&prStaRec->rUpdateTime);

	/* Update Station Record - Status/Reason Code */
	prStaRec->u2StatusCode = u2StatusCode;

	/* NOTE: Ignore the return status for AAA */
	/* 4 <4.2> Reply  Assoc Resp */
	assocSendReAssocRespFrame(prAdapter, prStaRec);

}

return WLAN_STATUS_SUCCESS;

}				/* end of aaaFsmRunEventRxAssoc() */

/*----------------------------------------------------------------------------*/
/*!
* @brief This function will handle TxDone(Auth2/AssocReq) Event of AAA FSM.
*
* @param[in] prAdapter      Pointer to the Adapter structure.
* @param[in] prMsduInfo     Pointer to the MSDU_INFO_T.
* @param[in] rTxDoneStatus  Return TX status of the Auth1/Auth3/AssocReq frame.
*
* @retval WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
aaaFsmRunEventTxDone(IN P_ADAPTER_T prAdapter, IN P_MSDU_INFO_T prMsduInfo, IN ENUM_TX_RESULT_CODE_T rTxDoneStatus)
{
	P_STA_RECORD_T prStaRec;
	P_BSS_INFO_T prBssInfo;

	ASSERT(prAdapter);
	ASSERT(prMsduInfo);

	DBGLOG(AAA, LOUD, "EVENT-TX DONE: Current Time = %lu\n", (unsigned long)kalGetTimeTick());

	prStaRec = cnmGetStaRecByIndex(prAdapter, prMsduInfo->ucStaRecIndex);

	if ((!prStaRec) || (!prStaRec->fgIsInUse)) {
		DBGLOG(AAA, INFO, "EVENT-TX DONE: Invalid StaRec");
		return WLAN_STATUS_SUCCESS;	/* For the case of replying ERROR STATUS CODE */
	}

	ASSERT(prStaRec->ucNetTypeIndex < NETWORK_TYPE_INDEX_NUM);

	prBssInfo = &(prAdapter->rWifiVar.arBssInfo[prStaRec->ucNetTypeIndex]);
	DBGLOG(AAA, INFO, "TX DONE status: %d, AuthAssocState: %d, SeqNo: %d\n",
			rTxDoneStatus, prStaRec->eAuthAssocState,
			prMsduInfo->ucTxSeqNum);

	switch (prStaRec->eAuthAssocState) {
	case AAA_STATE_SEND_AUTH2:
		{
			/* Strictly check the outgoing frame is matched with current AA STATE */
			if (authCheckTxAuthFrame(prAdapter, prMsduInfo, AUTH_TRANSACTION_SEQ_2) != WLAN_STATUS_SUCCESS)
				break;

			if (STATUS_CODE_SUCCESSFUL == prStaRec->u2StatusCode) {
				if (TX_RESULT_SUCCESS == rTxDoneStatus) {

					/* NOTE(Kevin): Change to STATE_2 at TX Done */
					cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_2);
				} else {

					prStaRec->eAuthAssocState = AA_STATE_IDLE;

					/* NOTE(Kevin): Change to STATE_1 */
					cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);

#if CFG_ENABLE_WIFI_DIRECT
					if ((prAdapter->fgIsP2PRegistered) && (IS_STA_IN_P2P(prStaRec)))
						p2pRunEventAAATxFail(prAdapter, prStaRec);
#endif /* CFG_ENABLE_WIFI_DIRECT */

#if CFG_ENABLE_BT_OVER_WIFI
					if (IS_STA_IN_BOW(prStaRec))
						bowRunEventAAATxFail(prAdapter, prStaRec);
#endif /* CFG_ENABLE_BT_OVER_WIFI */
				}

			}
			/* NOTE(Kevin): Ignore the TX Done Event of Auth Frame with Error Status Code */

		}
		break;

	case AAA_STATE_SEND_ASSOC2:
		{
			/* Strictly check the outgoing frame is matched with current SAA STATE */
			if (assocCheckTxReAssocRespFrame(prAdapter, prMsduInfo) != WLAN_STATUS_SUCCESS)
				break;

			if (STATUS_CODE_SUCCESSFUL == prStaRec->u2StatusCode) {
				if (TX_RESULT_SUCCESS == rTxDoneStatus) {

					prStaRec->eAuthAssocState = AA_STATE_IDLE;

					/* NOTE(Kevin): Change to STATE_3 at TX Done */
#if CFG_ENABLE_WIFI_DIRECT
					if ((prAdapter->fgIsP2PRegistered) && (IS_STA_IN_P2P(prStaRec)))
						p2pRunEventAAASuccess(prAdapter, prStaRec);
#endif /* CFG_ENABLE_WIFI_DIRECT */

#if CFG_ENABLE_BT_OVER_WIFI

					if (IS_STA_IN_BOW(prStaRec))
						bowRunEventAAAComplete(prAdapter, prStaRec);
#endif /* CFG_ENABLE_BT_OVER_WIFI */

				} else {

					prStaRec->eAuthAssocState = AAA_STATE_SEND_AUTH2;

					/* NOTE(Kevin): Change to STATE_2 */
					cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_2);

#if CFG_ENABLE_WIFI_DIRECT
					if ((prAdapter->fgIsP2PRegistered) && (IS_STA_IN_P2P(prStaRec)))
						p2pRunEventAAATxFail(prAdapter, prStaRec);
#endif /* CFG_ENABLE_WIFI_DIRECT */

#if CFG_ENABLE_BT_OVER_WIFI
					if (IS_STA_IN_BOW(prStaRec))
						bowRunEventAAATxFail(prAdapter, prStaRec);
#endif /* CFG_ENABLE_BT_OVER_WIFI */

				}
			}
			/* NOTE(Kevin): Ignore the TX Done Event of Auth Frame with Error Status Code */
		}
		break;

	default:
		break;		/* Ignore other cases */
	}

	return WLAN_STATUS_SUCCESS;

}				/* end of aaaFsmRunEventTxDone() */
#endif /* CFG_SUPPORT_AAA */

#if 0				/* TODO(Kevin): for abort event, just reset the STA_RECORD_T. */
/*----------------------------------------------------------------------------*/
/*!
* \brief This function will send ABORT Event to JOIN FSM.
*
* \param[in] prAdapter  Pointer to the Adapter structure.
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID saaFsmRunEventAbort(IN P_MSG_HDR_T prMsgHdr)
{
	P_JOIN_INFO_T prJoinInfo;
	P_STA_RECORD_T prStaRec;

	DEBUGFUNC("joinFsmRunEventAbort");

	ASSERT(prAdapter);
	prJoinInfo = &prAdapter->rJoinInfo;

	DBGLOG(JOIN, EVENT, "JOIN EVENT: ABORT\n");

	/* NOTE(Kevin): when reach here, the ARB_STATE should be in ARB_STATE_JOIN. */
	ASSERT(prJoinInfo->prBssDesc);

	/* 4 <1> Update Flags and Elements of JOIN Module. */
	/* Reset Send Auth/(Re)Assoc Frame Count */
	prJoinInfo->ucTxAuthAssocRetryCount = 0;

	/* Cancel all JOIN relative Timer */
	ARB_CANCEL_TIMER(prAdapter, prJoinInfo->rTxRequestTimer);

	ARB_CANCEL_TIMER(prAdapter, prJoinInfo->rRxResponseTimer);

	ARB_CANCEL_TIMER(prAdapter, prJoinInfo->rJoinTimer);

	/* 4 <2> Update the associated STA_RECORD_T during JOIN. */
	/* Get a Station Record if possible, TA == BSSID for AP */
	prStaRec = staRecGetStaRecordByAddr(prAdapter, prJoinInfo->prBssDesc->aucBSSID);
	if (prStaRec)
		prStaRec->ucStaState = STA_STATE_1; /* Update Station Record - Class 1 Flag */
#if DBG
	else
		ASSERT(0);	/* Shouldn't happened, because we already add this STA_RECORD_T at JOIN_STATE_INIT */
#endif /* DBG */

	/* 4 <3> Pull back to IDLE. */
	joinFsmSteps(prAdapter, JOIN_STATE_IDLE);

	/* 4 <4> If we are in Roaming, recover the settings of previous BSS. */
	/* NOTE: JOIN FAIL -
	 * Restore original setting from current BSS_INFO_T.
	 */
	if (prAdapter->eConnectionState == MEDIA_STATE_CONNECTED)
		joinAdoptParametersFromCurrentBss(prAdapter);

}				/* end of joinFsmRunEventAbort() */
#endif

/* TODO(Kevin): following code will be modified and move to AIS FSM */
#if 0
/*----------------------------------------------------------------------------*/
/*!
* \brief This function will send Join Timeout Event to JOIN FSM.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
*
* \retval WLAN_STATUS_FAILURE   Fail because of Join Timeout
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS joinFsmRunEventJoinTimeOut(IN P_ADAPTER_T prAdapter)
{
	P_JOIN_INFO_T prJoinInfo;
	P_STA_RECORD_T prStaRec;

	DEBUGFUNC("joinFsmRunEventJoinTimeOut");

	ASSERT(prAdapter);
	prJoinInfo = &prAdapter->rJoinInfo;

	DBGLOG(JOIN, EVENT, "JOIN EVENT: JOIN TIMEOUT\n");

	/* Get a Station Record if possible, TA == BSSID for AP */
	prStaRec = staRecGetStaRecordByAddr(prAdapter, prJoinInfo->prBssDesc->aucBSSID);

	/* We have renew this Sta Record when in JOIN_STATE_INIT */
	ASSERT(prStaRec);

	/* Record the Status Code of Authentication Request */
	prStaRec->u2StatusCode = STATUS_CODE_JOIN_TIMEOUT;

	/* Increase Failure Count */
	prStaRec->ucJoinFailureCount++;

	/* Reset Send Auth/(Re)Assoc Frame Count */
	prJoinInfo->ucTxAuthAssocRetryCount = 0;

	/* Cancel other JOIN relative Timer */
	ARB_CANCEL_TIMER(prAdapter, prJoinInfo->rTxRequestTimer);

	ARB_CANCEL_TIMER(prAdapter, prJoinInfo->rRxResponseTimer);

	/* Restore original setting from current BSS_INFO_T */
	if (prAdapter->eConnectionState == MEDIA_STATE_CONNECTED)
		joinAdoptParametersFromCurrentBss(prAdapter);

	/* Pull back to IDLE */
	joinFsmSteps(prAdapter, JOIN_STATE_IDLE);

	return WLAN_STATUS_FAILURE;

}				/* end of joinFsmRunEventJoinTimeOut() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This function will adopt the parameters from Peer BSS.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID joinAdoptParametersFromPeerBss(IN P_ADAPTER_T prAdapter)
{
	P_JOIN_INFO_T prJoinInfo;
	P_BSS_DESC_T prBssDesc;

	DEBUGFUNC("joinAdoptParametersFromPeerBss");

	ASSERT(prAdapter);
	prJoinInfo = &prAdapter->rJoinInfo;
	prBssDesc = prJoinInfo->prBssDesc;

	/* 4 <1> Adopt Peer BSS' PHY TYPE */
	prAdapter->eCurrentPhyType = prBssDesc->ePhyType;

	DBGLOG(JOIN, INFO, "Target BSS[%s]'s PhyType = %s\n",
			    prBssDesc->aucSSID, (prBssDesc->ePhyType == PHY_TYPE_ERP_INDEX) ? "ERP" : "HR_DSSS");

	/* 4 <2> Adopt Peer BSS' Frequency(Band/Channel) */
	DBGLOG(JOIN, INFO, "Target BSS's Channel = %d, Band = %d\n", prBssDesc->ucChannelNum, prBssDesc->eBand);

	nicSwitchChannel(prAdapter, prBssDesc->eBand, prBssDesc->ucChannelNum, 10);

	prJoinInfo->fgIsParameterAdopted = TRUE;

}				/* end of joinAdoptParametersFromPeerBss() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This function will adopt the parameters from current associated BSS.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID joinAdoptParametersFromCurrentBss(IN P_ADAPTER_T prAdapter)
{
	/* P_JOIN_INFO_T prJoinInfo = &prAdapter->rJoinInfo; */
	P_BSS_INFO_T prBssInfo;

	ASSERT(prAdapter);
	prBssInfo = &prAdapter->rBssInfo;

	/* 4 <1> Adopt current BSS' PHY TYPE */
	prAdapter->eCurrentPhyType = prBssInfo->ePhyType;

	/* 4 <2> Adopt current BSS' Frequency(Band/Channel) */
	DBGLOG(JOIN, INFO, "Current BSS's Channel = %d, Band = %d\n", prBssInfo->ucChnl, prBssInfo->eBand);

	nicSwitchChannel(prAdapter, prBssInfo->eBand, prBssInfo->ucChnl, 10);

}				/* end of joinAdoptParametersFromCurrentBss() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This function will update all the SW variables and HW MCR registers after
*        the association with target BSS.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID joinComplete(IN P_ADAPTER_T prAdapter)
{
	P_JOIN_INFO_T prJoinInfo;
	P_BSS_DESC_T prBssDesc;
	P_PEER_BSS_INFO_T prPeerBssInfo;
	P_BSS_INFO_T prBssInfo;
	P_CONNECTION_SETTINGS_T prConnSettings;
	P_STA_RECORD_T prStaRec;
	P_TX_CTRL_T prTxCtrl;
#if CFG_SUPPORT_802_11D
	P_IE_COUNTRY_T prIECountry;
#endif

	DEBUGFUNC("joinComplete");

	ASSERT(prAdapter);
	prJoinInfo = &prAdapter->rJoinInfo;
	prBssDesc = prJoinInfo->prBssDesc;
	prPeerBssInfo = &prAdapter->rPeerBssInfo;
	prBssInfo = &prAdapter->rBssInfo;
	prConnSettings = &prAdapter->rConnSettings;
	prTxCtrl = &prAdapter->rTxCtrl;

/* 4 <1> Update Connecting & Connected Flag of BSS_DESC_T. */
	/* Remove previous AP's Connection Flags if have */
	scanRemoveConnectionFlagOfBssDescByBssid(prAdapter, prBssInfo->aucBSSID);

	prBssDesc->fgIsConnected = TRUE;	/* Mask as Connected */

	if (prBssDesc->fgIsHiddenSSID) {
		/* NOTE(Kevin): This is for the case of Passive Scan and the target BSS didn't
		 * broadcast SSID on its Beacon Frame.
		 */
		COPY_SSID(prBssDesc->aucSSID,
			  prBssDesc->ucSSIDLen, prAdapter->rConnSettings.aucSSID, prAdapter->rConnSettings.ucSSIDLen);

		if (prBssDesc->ucSSIDLen)
			prBssDesc->fgIsHiddenSSID = FALSE;
#if DBG
		else
			ASSERT(0);
#endif /* DBG */

		DBGLOG(JOIN, INFO, "Hidden SSID! - Update SSID : %s\n", prBssDesc->aucSSID);
	}
/* 4 <2> Update BSS_INFO_T from BSS_DESC_T */
	/* 4 <2.A> PHY Type */
	prBssInfo->ePhyType = prBssDesc->ePhyType;

	/* 4 <2.B> BSS Type */
	prBssInfo->eBSSType = BSS_TYPE_INFRASTRUCTURE;

	/* 4 <2.C> BSSID */
	COPY_MAC_ADDR(prBssInfo->aucBSSID, prBssDesc->aucBSSID);

	DBGLOG(JOIN, INFO, "JOIN to BSSID: [%pM]\n", prBssDesc->aucBSSID);

	/* 4 <2.D> SSID */
	COPY_SSID(prBssInfo->aucSSID, prBssInfo->ucSSIDLen, prBssDesc->aucSSID, prBssDesc->ucSSIDLen);

	/* 4 <2.E> Channel / Band information. */
	prBssInfo->eBand = prBssDesc->eBand;
	prBssInfo->ucChnl = prBssDesc->ucChannelNum;

	/* 4 <2.F> RSN/WPA information. */
	secFsmRunEventStart(prAdapter);
	prBssInfo->u4RsnSelectedPairwiseCipher = prBssDesc->u4RsnSelectedPairwiseCipher;
	prBssInfo->u4RsnSelectedGroupCipher = prBssDesc->u4RsnSelectedGroupCipher;
	prBssInfo->u4RsnSelectedAKMSuite = prBssDesc->u4RsnSelectedAKMSuite;

	if (secRsnKeyHandshakeEnabled())
		prBssInfo->fgIsWPAorWPA2Enabled = TRUE;
	else
		prBssInfo->fgIsWPAorWPA2Enabled = FALSE;

	/* 4 <2.G> Beacon interval. */
	prBssInfo->u2BeaconInterval = prBssDesc->u2BeaconInterval;

	/* 4 <2.H> DTIM period. */
	prBssInfo->ucDtimPeriod = prBssDesc->ucDTIMPeriod;

	/* 4 <2.I> ERP Information */
	if ((prBssInfo->ePhyType == PHY_TYPE_ERP_INDEX) &&	/* Our BSS's PHY_TYPE is ERP now. */
	    (prBssDesc->fgIsERPPresent)) {

		prBssInfo->fgIsERPPresent = TRUE;
		prBssInfo->ucERP = prBssDesc->ucERP;	/* Save the ERP for later check */
	} else {	/* Some AP, may send ProbeResp without ERP IE. Thus prBssDesc->fgIsERPPresent is FALSE. */
		prBssInfo->fgIsERPPresent = FALSE;
		prBssInfo->ucERP = 0;
	}

#if CFG_SUPPORT_802_11D
	/* 4 <2.J> Country inforamtion of the associated AP */
	if (prConnSettings->fgMultiDomainCapabilityEnabled) {
		DOMAIN_INFO_ENTRY rDomainInfo;

		if (domainGetDomainInfoByScanResult(prAdapter, &rDomainInfo)) {
			if (prBssDesc->prIECountry) {
				prIECountry = prBssDesc->prIECountry;

				domainParseCountryInfoElem(prIECountry, &prBssInfo->rDomainInfo);

				/* use the domain get from the BSS info */
				prBssInfo->fgIsCountryInfoPresent = TRUE;
				nicSetupOpChnlList(prAdapter, prBssInfo->rDomainInfo.u2CountryCode, FALSE);
			} else {
				/* use the domain get from the scan result */
				prBssInfo->fgIsCountryInfoPresent = TRUE;
				nicSetupOpChnlList(prAdapter, rDomainInfo.u2CountryCode, FALSE);
			}
		}
	}
#endif

	/* 4 <2.K> Signal Power of the associated AP */
	prBssInfo->rRcpi = prBssDesc->rRcpi;
	prBssInfo->rRssi = RCPI_TO_dBm(prBssInfo->rRcpi);
	GET_CURRENT_SYSTIME(&prBssInfo->rRssiLastUpdateTime);

	/* 4 <2.L> Capability Field of the associated AP */
	prBssInfo->u2CapInfo = prBssDesc->u2CapInfo;

	DBGLOG(JOIN, INFO, "prBssInfo-> fgIsERPPresent = %d, ucERP = %02x, rRcpi = %d, rRssi = %ld\n",
			    prBssInfo->fgIsERPPresent, prBssInfo->ucERP, prBssInfo->rRcpi, prBssInfo->rRssi);

/* 4 <3> Update BSS_INFO_T from PEER_BSS_INFO_T & NIC RATE FUNC */
	/* 4 <3.A> Association ID */
	prBssInfo->u2AssocId = prPeerBssInfo->u2AssocId;

	/* 4 <3.B> WMM Information */
	if (prAdapter->fgIsEnableWMM && (prPeerBssInfo->rWmmInfo.ucWmmFlag & WMM_FLAG_SUPPORT_WMM)) {

		prBssInfo->fgIsWmmAssoc = TRUE;
		prTxCtrl->rTxQForVoipAccess = TXQ_AC3;

		qosWmmInfoInit(&prBssInfo->rWmmInfo, (prBssInfo->ePhyType == PHY_TYPE_HR_DSSS_INDEX) ? TRUE : FALSE);

		if (prPeerBssInfo->rWmmInfo.ucWmmFlag & WMM_FLAG_AC_PARAM_PRESENT) {
			kalMemCopy(&prBssInfo->rWmmInfo, &prPeerBssInfo->rWmmInfo, sizeof(WMM_INFO_T));
		} else {
			kalMemCopy(&prBssInfo->rWmmInfo,
				   &prPeerBssInfo->rWmmInfo,
				   sizeof(WMM_INFO_T) - sizeof(prPeerBssInfo->rWmmInfo.arWmmAcParams));
		}
	} else {
		prBssInfo->fgIsWmmAssoc = FALSE;
		prTxCtrl->rTxQForVoipAccess = TXQ_AC1;

		kalMemZero(&prBssInfo->rWmmInfo, sizeof(WMM_INFO_T));
	}

	/* 4 <3.C> Operational Rate Set & BSS Basic Rate Set */
	prBssInfo->u2OperationalRateSet = prPeerBssInfo->u2OperationalRateSet;
	prBssInfo->u2BSSBasicRateSet = prPeerBssInfo->u2BSSBasicRateSet;

	/* 4 <3.D> Short Preamble */
	if (prBssInfo->fgIsERPPresent) {

		/* NOTE(Kevin 2007/12/24): Truth Table.
		 * Short Preamble Bit in
		 * <AssocReq>     <AssocResp w/i ERP>     <BARKER(Long)>  Final Driver Setting(Short)
		 * TRUE            FALSE                  FALSE           FALSE(shouldn't have such case,
		 *							  use the AssocResp)
		 * TRUE            FALSE                  TRUE            FALSE
		 * FALSE           FALSE                  FALSE           FALSE(shouldn't have such case,
		 *							  use the AssocResp)
		 * FALSE           FALSE                  TRUE            FALSE
		 * TRUE            TRUE                   FALSE           TRUE(follow ERP)
		 * TRUE            TRUE                   TRUE            FALSE(follow ERP)
		 * FALSE           TRUE                   FALSE           FALSE(shouldn't have such case,
		 *							  and we should set to FALSE)
		 * FALSE           TRUE                   TRUE            FALSE(we should set to FALSE)
		 */
		if ((prPeerBssInfo->fgIsShortPreambleAllowed) &&
		   ((prConnSettings->ePreambleType == PREAMBLE_TYPE_SHORT) || /* Short Preamble Option Enable is TRUE */
		   ((prConnSettings->ePreambleType == PREAMBLE_TYPE_AUTO) &&
		   (prBssDesc->u2CapInfo & CAP_INFO_SHORT_PREAMBLE)))) {

			prBssInfo->fgIsShortPreambleAllowed = TRUE;

			if (prBssInfo->ucERP & ERP_INFO_BARKER_PREAMBLE_MODE)
				prBssInfo->fgUseShortPreamble = FALSE;
			else
				prBssInfo->fgUseShortPreamble = TRUE;
		} else {
			prBssInfo->fgIsShortPreambleAllowed = FALSE;
			prBssInfo->fgUseShortPreamble = FALSE;
		}
	} else {
		/* NOTE(Kevin 2007/12/24): Truth Table.
		 * Short Preamble Bit in
		 * <AssocReq>     <AssocResp w/o ERP>     Final Driver Setting(Short)
		 * TRUE            FALSE                  FALSE
		 * FALSE           FALSE                  FALSE
		 * TRUE            TRUE                   TRUE
		 * FALSE           TRUE(status success)   TRUE
		 * --> Honor the result of prPeerBssInfo.
		 */

		prBssInfo->fgIsShortPreambleAllowed = prBssInfo->fgUseShortPreamble =
		    prPeerBssInfo->fgIsShortPreambleAllowed;
	}

	DBGLOG(JOIN, INFO, "prBssInfo->fgIsShortPreambleAllowed = %d, prBssInfo->fgUseShortPreamble = %d\n",
			    prBssInfo->fgIsShortPreambleAllowed, prBssInfo->fgUseShortPreamble);

	/* 4 <3.E> Short Slot Time */
	prBssInfo->fgUseShortSlotTime = prPeerBssInfo->fgUseShortSlotTime;	/* AP support Short Slot Time */

	DBGLOG(JOIN, INFO, "prBssInfo->fgUseShortSlotTime = %d\n", prBssInfo->fgUseShortSlotTime);

	nicSetSlotTime(prAdapter,
		       prBssInfo->ePhyType,
		       ((prConnSettings->fgIsShortSlotTimeOptionEnable &&
			 prBssInfo->fgUseShortSlotTime) ? TRUE : FALSE));

	/* 4 <3.F> Update Tx Rate for Control Frame */
	bssUpdateTxRateForControlFrame(prAdapter);

	/* 4 <3.G> Save the available Auth Types during Roaming (Design for Fast BSS Transition). */
	/* if (prAdapter->fgIsEnableRoaming) */ /* NOTE(Kevin): Always prepare info for roaming */
	{

		if (prJoinInfo->ucCurrAuthAlgNum == AUTH_ALGORITHM_NUM_OPEN_SYSTEM)
			prJoinInfo->ucRoamingAuthTypes |= AUTH_TYPE_OPEN_SYSTEM;
		else if (prJoinInfo->ucCurrAuthAlgNum == AUTH_ALGORITHM_NUM_SHARED_KEY)
			prJoinInfo->ucRoamingAuthTypes |= AUTH_TYPE_SHARED_KEY;

		prBssInfo->ucRoamingAuthTypes = prJoinInfo->ucRoamingAuthTypes;

		/* Set the stable time of the associated BSS. We won't do roaming decision
		 * during the stable time.
		 */
		SET_EXPIRATION_TIME(prBssInfo->rRoamingStableExpirationTime,
				    SEC_TO_SYSTIME(ROAMING_STABLE_TIMEOUT_SEC));
	}

	/* 4 <3.H> Update Parameter for TX Fragmentation Threshold */
#if CFG_TX_FRAGMENT
	txFragInfoUpdate(prAdapter);
#endif /* CFG_TX_FRAGMENT */

/* 4 <4> Update STA_RECORD_T */
	/* Get a Station Record if possible */
	prStaRec = staRecGetStaRecordByAddr(prAdapter, prBssDesc->aucBSSID);

	if (prStaRec) {
		UINT_16 u2OperationalRateSet, u2DesiredRateSet;

		/* 4 <4.A> Desired Rate Set */
		u2OperationalRateSet = (rPhyAttributes[prBssInfo->ePhyType].u2SupportedRateSet &
					prBssInfo->u2OperationalRateSet);

		u2DesiredRateSet = (u2OperationalRateSet & prConnSettings->u2DesiredRateSet);
		if (u2DesiredRateSet) {
			prStaRec->u2DesiredRateSet = u2DesiredRateSet;
		} else {
			/* For Error Handling - The Desired Rate Set is not covered in Operational Rate Set. */
			prStaRec->u2DesiredRateSet = u2OperationalRateSet;
		}

		/* Try to set the best initial rate for this entry */
		if (!rateGetBestInitialRateIndex(prStaRec->u2DesiredRateSet,
						 prStaRec->rRcpi, &prStaRec->ucCurrRate1Index)) {

			if (!rateGetLowestRateIndexFromRateSet(prStaRec->u2DesiredRateSet, &prStaRec->ucCurrRate1Index))
				ASSERT(0);
		}

		DBGLOG(JOIN, INFO, "prStaRec->ucCurrRate1Index = %d\n", prStaRec->ucCurrRate1Index);

		/* 4 <4.B> Preamble Mode */
		prStaRec->fgIsShortPreambleOptionEnable = prBssInfo->fgUseShortPreamble;

		/* 4 <4.C> QoS Flag */
		prStaRec->fgIsQoS = prBssInfo->fgIsWmmAssoc;
	}
#if DBG
	else
		ASSERT(0);
#endif /* DBG */

/* 4 <5> Update NIC */
	/* 4 <5.A> Update BSSID & Operation Mode */
	nicSetupBSS(prAdapter, prBssInfo);

	/* 4 <5.B> Update WLAN Table. */
	if (nicSetHwBySta(prAdapter, prStaRec) == FALSE)
		ASSERT(FALSE);
	/* 4 <5.C> Update Desired Rate Set for BT. */
#if CFG_TX_FRAGMENT
	if (prConnSettings->fgIsEnableTxAutoFragmentForBT)
		txRateSetInitForBT(prAdapter, prStaRec);
#endif /* CFG_TX_FRAGMENT */

	/* 4 <5.D> TX AC Parameter and TX/RX Queue Control */
	if (prBssInfo->fgIsWmmAssoc) {

#if CFG_TX_AGGREGATE_HW_FIFO
		nicTxAggregateTXQ(prAdapter, FALSE);
#endif /* CFG_TX_AGGREGATE_HW_FIFO */

		qosUpdateWMMParametersAndAssignAllowedACI(prAdapter, &prBssInfo->rWmmInfo);
	} else {

#if CFG_TX_AGGREGATE_HW_FIFO
		nicTxAggregateTXQ(prAdapter, TRUE);
#endif /* CFG_TX_AGGREGATE_HW_FIFO */

		nicTxNonQoSAssignDefaultAdmittedTXQ(prAdapter);

		nicTxNonQoSUpdateTXQParameters(prAdapter, prBssInfo->ePhyType);
	}

#if CFG_TX_STOP_WRITE_TX_FIFO_UNTIL_JOIN
	{
		prTxCtrl->fgBlockTxDuringJoin = FALSE;

#if !CFG_TX_AGGREGATE_HW_FIFO	/* TX FIFO AGGREGATE already do flush once */
		nicTxFlushStopQueues(prAdapter, (UINT_8) TXQ_DATA_MASK, (UINT_8) NULL);
#endif /* CFG_TX_AGGREGATE_HW_FIFO */

		nicTxRetransmitOfSendWaitQue(prAdapter);

		if (prTxCtrl->fgIsPacketInOsSendQueue)
			nicTxRetransmitOfOsSendQue(prAdapter);
#if CFG_SDIO_TX_ENHANCE
		halTxLeftClusteredMpdu(prAdapter);
#endif /* CFG_SDIO_TX_ENHANCE */

	}
#endif /* CFG_TX_STOP_WRITE_TX_FIFO_UNTIL_JOIN */

/* 4 <6> Setup CONNECTION flag. */
	prAdapter->eConnectionState = MEDIA_STATE_CONNECTED;
	prAdapter->eConnectionStateIndicated = MEDIA_STATE_CONNECTED;

	if (prJoinInfo->fgIsReAssoc)
		prAdapter->fgBypassPortCtrlForRoaming = TRUE;
	else
		prAdapter->fgBypassPortCtrlForRoaming = FALSE;

	kalIndicateStatusAndComplete(prAdapter->prGlueInfo, WLAN_STATUS_MEDIA_CONNECT, (PVOID) NULL, 0);

}				/* end of joinComplete() */
#endif
