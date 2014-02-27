//------------------------------------------------------------------------------
// <copyright file="htc_services.c" company="Atheros">
//    Copyright (c) 2007-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#include "htc_internal.h"

void HTCControlTxComplete(void *Context, HTC_PACKET *pPacket)
{
        /* not implemented
         * we do not send control TX frames during normal runtime, only during setup  */
    AR_DEBUG_ASSERT(FALSE);
}

    /* callback when a control message arrives on this endpoint */
void HTCControlRecv(void *Context, HTC_PACKET *pPacket)
{
    AR_DEBUG_ASSERT(pPacket->Endpoint == ENDPOINT_0);

    if (pPacket->Status == A_ECANCELED) {
        /* this is a flush operation, return the control packet back to the pool */
        HTC_FREE_CONTROL_RX((HTC_TARGET*)Context,pPacket);    
        return;
    }  
    
        /* the only control messages we are expecting are NULL messages (credit resports) */   
    if (pPacket->ActualLength > 0) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("HTCControlRecv, got message with length:%d \n",
                        pPacket->ActualLength + (A_UINT32)HTC_HDR_LENGTH));
    
            /* dump header and message */
        DebugDumpBytes(pPacket->pBuffer - HTC_HDR_LENGTH,
                       pPacket->ActualLength + HTC_HDR_LENGTH,
                       "Unexpected ENDPOINT 0 Message");
    }

    HTC_RECYCLE_RX_PKT((HTC_TARGET*)Context,pPacket,&((HTC_TARGET*)Context)->EndPoint[0]);
}

A_STATUS HTCSendSetupComplete(HTC_TARGET *target)
{
    HTC_PACKET             *pSendPacket = NULL;
    A_STATUS                status;

    do {
           /* allocate a packet to send to the target */
        pSendPacket = HTC_ALLOC_CONTROL_TX(target);

        if (NULL == pSendPacket) {
            status = A_NO_MEMORY;
            break;
        }

        if (target->HTCTargetVersion >= HTC_VERSION_2P1) {
            HTC_SETUP_COMPLETE_EX_MSG *pSetupCompleteEx;
            A_UINT32                  setupFlags = 0;
                   
            pSetupCompleteEx = (HTC_SETUP_COMPLETE_EX_MSG *)pSendPacket->pBuffer;
            A_MEMZERO(pSetupCompleteEx, sizeof(HTC_SETUP_COMPLETE_EX_MSG));
            pSetupCompleteEx->MessageID = HTC_MSG_SETUP_COMPLETE_EX_ID;   
            if (target->MaxMsgPerBundle > 0) {
                    /* host can do HTC bundling, indicate this to the target */
                setupFlags |= HTC_SETUP_COMPLETE_FLAGS_ENABLE_BUNDLE_RECV; 
                pSetupCompleteEx->MaxMsgsPerBundledRecv = target->MaxMsgPerBundle;
            }    
            A_MEMCPY(&pSetupCompleteEx->SetupFlags, &setupFlags, sizeof(pSetupCompleteEx->SetupFlags));            
            SET_HTC_PACKET_INFO_TX(pSendPacket,
                                   NULL,
                                   (A_UINT8 *)pSetupCompleteEx,
                                   sizeof(HTC_SETUP_COMPLETE_EX_MSG),
                                   ENDPOINT_0,
                                   HTC_SERVICE_TX_PACKET_TAG);
      
        }  else {            
            HTC_SETUP_COMPLETE_MSG *pSetupComplete;
                /* assemble setup complete message */
            pSetupComplete = (HTC_SETUP_COMPLETE_MSG *)pSendPacket->pBuffer;
            A_MEMZERO(pSetupComplete, sizeof(HTC_SETUP_COMPLETE_MSG));
            pSetupComplete->MessageID = HTC_MSG_SETUP_COMPLETE_ID;   
            SET_HTC_PACKET_INFO_TX(pSendPacket,
                                   NULL,
                                   (A_UINT8 *)pSetupComplete,
                                   sizeof(HTC_SETUP_COMPLETE_MSG),
                                   ENDPOINT_0,
                                   HTC_SERVICE_TX_PACKET_TAG);
        }

            /* we want synchronous operation */
        pSendPacket->Completion = NULL;
        HTC_PREPARE_SEND_PKT(pSendPacket,0,0,0);
            /* send the message */
        status = HTCIssueSend(target,pSendPacket);

    } while (FALSE);

    if (pSendPacket != NULL) {
        HTC_FREE_CONTROL_TX(target,pSendPacket);
    }

    return status;
}


A_STATUS HTCConnectService(HTC_HANDLE               HTCHandle,
                           HTC_SERVICE_CONNECT_REQ  *pConnectReq,
                           HTC_SERVICE_CONNECT_RESP *pConnectResp)
{
    HTC_TARGET *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    A_STATUS                            status = A_OK;
    HTC_PACKET                          *pRecvPacket = NULL;
    HTC_PACKET                          *pSendPacket = NULL;
    HTC_CONNECT_SERVICE_RESPONSE_MSG    *pResponseMsg;
    HTC_CONNECT_SERVICE_MSG             *pConnectMsg;
    HTC_ENDPOINT_ID                     assignedEndpoint = ENDPOINT_MAX;
    HTC_ENDPOINT                        *pEndpoint;
    unsigned int                        maxMsgSize = 0;

    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("+HTCConnectService, target:0x%lX SvcID:0x%X \n",
               (unsigned long)target, pConnectReq->ServiceID));

    do {

        AR_DEBUG_ASSERT(pConnectReq->ServiceID != 0);

        if (HTC_CTRL_RSVD_SVC == pConnectReq->ServiceID) {
                /* special case for pseudo control service */
            assignedEndpoint = ENDPOINT_0;
            maxMsgSize = HTC_MAX_CONTROL_MESSAGE_LENGTH;
        } else {
                /* allocate a packet to send to the target */
            pSendPacket = HTC_ALLOC_CONTROL_TX(target);

            if (NULL == pSendPacket) {
                AR_DEBUG_ASSERT(FALSE);
                status = A_NO_MEMORY;
                break;
            }
                /* assemble connect service message */
            pConnectMsg = (HTC_CONNECT_SERVICE_MSG *)pSendPacket->pBuffer;
            AR_DEBUG_ASSERT(pConnectMsg != NULL);
            A_MEMZERO(pConnectMsg,sizeof(HTC_CONNECT_SERVICE_MSG));
            pConnectMsg->MessageID = HTC_MSG_CONNECT_SERVICE_ID;
            pConnectMsg->ServiceID = pConnectReq->ServiceID;
            pConnectMsg->ConnectionFlags = pConnectReq->ConnectionFlags;
                /* check caller if it wants to transfer meta data */
            if ((pConnectReq->pMetaData != NULL) &&
                (pConnectReq->MetaDataLength <= HTC_SERVICE_META_DATA_MAX_LENGTH)) {
                    /* copy meta data into message buffer (after header ) */
                A_MEMCPY((A_UINT8 *)pConnectMsg + sizeof(HTC_CONNECT_SERVICE_MSG),
                         pConnectReq->pMetaData,
                         pConnectReq->MetaDataLength);
                pConnectMsg->ServiceMetaLength = pConnectReq->MetaDataLength;
            }

            SET_HTC_PACKET_INFO_TX(pSendPacket,
                                   NULL,
                                   (A_UINT8 *)pConnectMsg,
                                   sizeof(HTC_CONNECT_SERVICE_MSG) + pConnectMsg->ServiceMetaLength,
                                   ENDPOINT_0,
                                   HTC_SERVICE_TX_PACKET_TAG);

                /* we want synchronous operation */
            pSendPacket->Completion = NULL;
            HTC_PREPARE_SEND_PKT(pSendPacket,0,0,0);
            status = HTCIssueSend(target,pSendPacket);

            if (A_FAILED(status)) {
                break;
            }

                /* wait for response */
            status = HTCWaitforControlMessage(target, &pRecvPacket);

            if (A_FAILED(status)) {
                break;
            }
                /* we controlled the buffer creation so it has to be properly aligned */
            pResponseMsg = (HTC_CONNECT_SERVICE_RESPONSE_MSG *)pRecvPacket->pBuffer;

            if ((pResponseMsg->MessageID != HTC_MSG_CONNECT_SERVICE_RESPONSE_ID) ||
                (pRecvPacket->ActualLength < sizeof(HTC_CONNECT_SERVICE_RESPONSE_MSG))) {
                    /* this message is not valid */
                AR_DEBUG_ASSERT(FALSE);
                status = A_EPROTO;
                break;
            }

            pConnectResp->ConnectRespCode = pResponseMsg->Status;
                /* check response status */
            if (pResponseMsg->Status != HTC_SERVICE_SUCCESS) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                    (" Target failed service 0x%X connect request (status:%d)\n",
                                pResponseMsg->ServiceID, pResponseMsg->Status));
                status = A_EPROTO;
                break;
            }

            assignedEndpoint = (HTC_ENDPOINT_ID) pResponseMsg->EndpointID;
            maxMsgSize = pResponseMsg->MaxMsgSize;

            if ((pConnectResp->pMetaData != NULL) &&
                (pResponseMsg->ServiceMetaLength > 0) &&
                (pResponseMsg->ServiceMetaLength <= HTC_SERVICE_META_DATA_MAX_LENGTH)) {
                    /* caller supplied a buffer and the target responded with data */
                int copyLength = min((int)pConnectResp->BufferLength, (int)pResponseMsg->ServiceMetaLength);
                    /* copy the meta data */
                A_MEMCPY(pConnectResp->pMetaData,
                         ((A_UINT8 *)pResponseMsg) + sizeof(HTC_CONNECT_SERVICE_RESPONSE_MSG),
                         copyLength);
                pConnectResp->ActualLength = copyLength;
            }

        }

            /* the rest of these are parameter checks so set the error status */
        status = A_EPROTO;

        if (assignedEndpoint >= ENDPOINT_MAX) {
            AR_DEBUG_ASSERT(FALSE);
            break;
        }

        if (0 == maxMsgSize) {
            AR_DEBUG_ASSERT(FALSE);
            break;
        }

        pEndpoint = &target->EndPoint[assignedEndpoint];
        pEndpoint->Id = assignedEndpoint;
        if (pEndpoint->ServiceID != 0) {
            /* endpoint already in use! */
            AR_DEBUG_ASSERT(FALSE);
            break;
        }

            /* return assigned endpoint to caller */
        pConnectResp->Endpoint = assignedEndpoint;
        pConnectResp->MaxMsgLength = maxMsgSize;

            /* setup the endpoint */
        pEndpoint->ServiceID = pConnectReq->ServiceID; /* this marks the endpoint in use */
        pEndpoint->MaxTxQueueDepth = pConnectReq->MaxSendQueueDepth;
        pEndpoint->MaxMsgLength = maxMsgSize;
            /* copy all the callbacks */
        pEndpoint->EpCallBacks = pConnectReq->EpCallbacks;
            /* set the credit distribution info for this endpoint, this information is
             * passed back to the credit distribution callback function */
        pEndpoint->CreditDist.ServiceID = pConnectReq->ServiceID;
        pEndpoint->CreditDist.pHTCReserved = pEndpoint;
        pEndpoint->CreditDist.Endpoint = assignedEndpoint;
        pEndpoint->CreditDist.TxCreditSize = target->TargetCreditSize;
        
        if (pConnectReq->MaxSendMsgSize != 0) {
                /* override TxCreditsPerMaxMsg calculation, this optimizes the credit-low indications
                 * since the host will actually issue smaller messages in the Send path */
            if (pConnectReq->MaxSendMsgSize > maxMsgSize) {
                    /* can't be larger than the maximum the target can support */
                AR_DEBUG_ASSERT(FALSE);
                break;       
            }
            pEndpoint->CreditDist.TxCreditsPerMaxMsg = pConnectReq->MaxSendMsgSize / target->TargetCreditSize;
        } else {
            pEndpoint->CreditDist.TxCreditsPerMaxMsg = maxMsgSize / target->TargetCreditSize;
        }
        
        if (0 == pEndpoint->CreditDist.TxCreditsPerMaxMsg) {
            pEndpoint->CreditDist.TxCreditsPerMaxMsg = 1;
        }
        
            /* save local connection flags */
        pEndpoint->LocalConnectionFlags = pConnectReq->LocalConnectionFlags;
        
        status = A_OK;

    } while (FALSE);

    if (pSendPacket != NULL) {
        HTC_FREE_CONTROL_TX(target,pSendPacket);
    }

    if (pRecvPacket != NULL) {
        HTC_FREE_CONTROL_RX(target,pRecvPacket);
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("-HTCConnectService \n"));

    return status;
}

static void AddToEndpointDistList(HTC_TARGET *target, HTC_ENDPOINT_CREDIT_DIST *pEpDist)
{
    HTC_ENDPOINT_CREDIT_DIST *pCurEntry  = NULL;
    HTC_ENDPOINT_CREDIT_DIST *pLastEntry = NULL;

    if (NULL == target->EpCreditDistributionListHead) {
        target->EpCreditDistributionListHead = pEpDist;
        pEpDist->pNext = NULL;
        pEpDist->pPrev = NULL;
        return;
    }

        /* queue to the end of the list, this does not have to be very
         * fast since this list is built at startup time */
    pCurEntry = target->EpCreditDistributionListHead;

    while (pCurEntry) {
        pLastEntry = pCurEntry;
        pCurEntry = pCurEntry->pNext;
    }

    pLastEntry->pNext = pEpDist;
    pEpDist->pPrev = pLastEntry;
    pEpDist->pNext = NULL;
}



/* default credit init callback */
static void HTCDefaultCreditInit(void                     *Context,
                                 HTC_ENDPOINT_CREDIT_DIST *pEPList,
                                 int                      TotalCredits)
{
    HTC_ENDPOINT_CREDIT_DIST *pCurEpDist;
    int                      totalEps = 0;
    int                      creditsPerEndpoint;

    pCurEpDist = pEPList;
        /* first run through the list and figure out how many endpoints we are dealing with */
    while (pCurEpDist != NULL) {
        pCurEpDist = pCurEpDist->pNext;
        totalEps++;
    }

        /* even distribution */
    creditsPerEndpoint = TotalCredits/totalEps;

    pCurEpDist = pEPList;
        /* run through the list and set minimum and normal credits and
         * provide the endpoint with some credits to start */
    while (pCurEpDist != NULL) {

        if (creditsPerEndpoint < pCurEpDist->TxCreditsPerMaxMsg) {
                /* too many endpoints and not enough credits */
            AR_DEBUG_ASSERT(FALSE);
            break;
        }
            /* our minimum is set for at least 1 max message */
        pCurEpDist->TxCreditsMin = pCurEpDist->TxCreditsPerMaxMsg;
            /* this value is ignored by our credit alg, since we do
             * not dynamically adjust credits, this is the policy of
             * the "default" credit distribution, something simple and easy */
        pCurEpDist->TxCreditsNorm = 0xFFFF;
            /* give the endpoint minimum credits */
        pCurEpDist->TxCredits = creditsPerEndpoint;
        pCurEpDist->TxCreditsAssigned = creditsPerEndpoint;
        pCurEpDist = pCurEpDist->pNext;
    }

}

/* default credit distribution callback, NOTE, this callback holds the TX lock */
void HTCDefaultCreditDist(void                     *Context,
                          HTC_ENDPOINT_CREDIT_DIST *pEPDistList,
                          HTC_CREDIT_DIST_REASON   Reason)
{
    HTC_ENDPOINT_CREDIT_DIST *pCurEpDist;

    if (Reason == HTC_CREDIT_DIST_SEND_COMPLETE) {
        pCurEpDist = pEPDistList;
            /* simple distribution */
        while (pCurEpDist != NULL) {
            if (pCurEpDist->TxCreditsToDist > 0) {
                    /* just give the endpoint back the credits */
                pCurEpDist->TxCredits += pCurEpDist->TxCreditsToDist;
                pCurEpDist->TxCreditsToDist = 0;
            }
            pCurEpDist = pCurEpDist->pNext;
        }
    }

    /* note we do not need to handle the other reason codes as this is a very
     * simple distribution scheme, no need to seek for more credits or handle inactivity */
}

void HTCSetCreditDistribution(HTC_HANDLE               HTCHandle,
                              void                     *pCreditDistContext,
                              HTC_CREDIT_DIST_CALLBACK CreditDistFunc,
                              HTC_CREDIT_INIT_CALLBACK CreditInitFunc,
                              HTC_SERVICE_ID           ServicePriorityOrder[],
                              int                      ListLength)
{
    HTC_TARGET *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    int i;
    int ep;

    if (CreditInitFunc != NULL) {
            /* caller has supplied their own distribution functions */
        target->InitCredits = CreditInitFunc;
        AR_DEBUG_ASSERT(CreditDistFunc != NULL);
        target->DistributeCredits = CreditDistFunc;
        target->pCredDistContext = pCreditDistContext;
    } else {
        /* caller wants HTC to do distribution */
        /* if caller wants service to handle distributions then
         * it must set both of these to NULL! */
        AR_DEBUG_ASSERT(CreditDistFunc == NULL);
        target->InitCredits = HTCDefaultCreditInit;
        target->DistributeCredits = HTCDefaultCreditDist;
        target->pCredDistContext = target;
    }

        /* always add HTC control endpoint first, we only expose the list after the
         * first one, this is added for TX queue checking */
    AddToEndpointDistList(target, &target->EndPoint[ENDPOINT_0].CreditDist);

        /* build the list of credit distribution structures in priority order
         * supplied by the caller, these will follow endpoint 0 */
    for (i = 0; i < ListLength; i++) {
            /* match services with endpoints and add the endpoints to the distribution list
             * in FIFO order */
        for (ep = ENDPOINT_1; ep < ENDPOINT_MAX; ep++) {
            if (target->EndPoint[ep].ServiceID == ServicePriorityOrder[i]) {
                    /* queue this one to the list */
                AddToEndpointDistList(target, &target->EndPoint[ep].CreditDist);
                break;
            }
        }
        AR_DEBUG_ASSERT(ep < ENDPOINT_MAX);
    }

}
