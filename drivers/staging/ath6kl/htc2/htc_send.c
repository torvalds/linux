//------------------------------------------------------------------------------
// <copyright file="htc_send.c" company="Atheros">
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

typedef enum _HTC_SEND_QUEUE_RESULT {
    HTC_SEND_QUEUE_OK = 0,    /* packet was queued */
    HTC_SEND_QUEUE_DROP = 1,  /* this packet should be dropped */
} HTC_SEND_QUEUE_RESULT;

#define DO_EP_TX_COMPLETION(ep,q)  DoSendCompletion(ep,q)

/* call the distribute credits callback with the distribution */
#define DO_DISTRIBUTION(t,reason,description,pList) \
{                                             \
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,           \
        ("  calling distribute function (%s) (dfn:0x%lX, ctxt:0x%lX, dist:0x%lX) \n", \
                (description),                                           \
                (unsigned long)(t)->DistributeCredits,                   \
                (unsigned long)(t)->pCredDistContext,                    \
                (unsigned long)pList));                                  \
    (t)->DistributeCredits((t)->pCredDistContext,                        \
                           (pList),                                      \
                           (reason));                                    \
}

static void DoSendCompletion(HTC_ENDPOINT       *pEndpoint,
                             HTC_PACKET_QUEUE   *pQueueToIndicate)
{           
    do {
                
        if (HTC_QUEUE_EMPTY(pQueueToIndicate)) {
                /* nothing to indicate */
            break;    
        }
 
        if (pEndpoint->EpCallBacks.EpTxCompleteMultiple != NULL) {    
            AR_DEBUG_PRINTF(ATH_DEBUG_SEND, (" HTC calling ep %d, send complete multiple callback (%d pkts) \n",
                     pEndpoint->Id, HTC_PACKET_QUEUE_DEPTH(pQueueToIndicate)));
                /* a multiple send complete handler is being used, pass the queue to the handler */                             
            pEndpoint->EpCallBacks.EpTxCompleteMultiple(pEndpoint->EpCallBacks.pContext,
                                                        pQueueToIndicate);
                /* all packets are now owned by the callback, reset queue to be safe */
            INIT_HTC_PACKET_QUEUE(pQueueToIndicate);                                                      
        } else {
            HTC_PACKET *pPacket;  
            /* using legacy EpTxComplete */         
            do {
                pPacket = HTC_PACKET_DEQUEUE(pQueueToIndicate);
                AR_DEBUG_PRINTF(ATH_DEBUG_SEND, (" HTC calling ep %d send complete callback on packet 0x%lX \n", \
                        pEndpoint->Id, (unsigned long)(pPacket)));
                pEndpoint->EpCallBacks.EpTxComplete(pEndpoint->EpCallBacks.pContext, pPacket);                                              
            } while (!HTC_QUEUE_EMPTY(pQueueToIndicate));                                              
        }
        
    } while (FALSE);

}

/* do final completion on sent packet */
static INLINE void CompleteSentPacket(HTC_TARGET *target, HTC_ENDPOINT *pEndpoint, HTC_PACKET *pPacket)
{
    pPacket->Completion = NULL;  
    
    if (A_FAILED(pPacket->Status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
            ("CompleteSentPacket: request failed (status:%d, ep:%d, length:%d creds:%d) \n",
                pPacket->Status, pPacket->Endpoint, pPacket->ActualLength, pPacket->PktInfo.AsTx.CreditsUsed));                
            /* on failure to submit, reclaim credits for this packet */        
        LOCK_HTC_TX(target);        
        pEndpoint->CreditDist.TxCreditsToDist += pPacket->PktInfo.AsTx.CreditsUsed;
        pEndpoint->CreditDist.TxQueueDepth = HTC_PACKET_QUEUE_DEPTH(&pEndpoint->TxQueue);
        DO_DISTRIBUTION(target,
                        HTC_CREDIT_DIST_SEND_COMPLETE,
                        "Send Complete",
                        target->EpCreditDistributionListHead->pNext);
        UNLOCK_HTC_TX(target);            
    }
        /* first, fixup the head room we allocated */
    pPacket->pBuffer += HTC_HDR_LENGTH; 
}

/* our internal send packet completion handler when packets are submited to the AR6K device
 * layer */
static void HTCSendPktCompletionHandler(void *Context, HTC_PACKET *pPacket)
{
    HTC_TARGET      *target = (HTC_TARGET *)Context;
    HTC_ENDPOINT    *pEndpoint = &target->EndPoint[pPacket->Endpoint];
    HTC_PACKET_QUEUE container;
    
    CompleteSentPacket(target,pEndpoint,pPacket);
    INIT_HTC_PACKET_QUEUE_AND_ADD(&container,pPacket);
        /* do completion */
    DO_EP_TX_COMPLETION(pEndpoint,&container);
}

A_STATUS HTCIssueSend(HTC_TARGET *target, HTC_PACKET *pPacket)
{
    A_STATUS status;
    A_BOOL   sync = FALSE;

    if (pPacket->Completion == NULL) {
            /* mark that this request was synchronously issued */
        sync = TRUE;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,
                    ("+-HTCIssueSend: transmit length : %d (%s) \n",
                    pPacket->ActualLength + (A_UINT32)HTC_HDR_LENGTH,
                    sync ? "SYNC" : "ASYNC" ));

        /* send message to device */
    status = DevSendPacket(&target->Device,
                           pPacket,
                           pPacket->ActualLength + HTC_HDR_LENGTH);

    if (sync) {
            /* use local sync variable.  If this was issued asynchronously, pPacket is no longer
             * safe to access. */
        pPacket->pBuffer += HTC_HDR_LENGTH;
    }
    
    /* if this request was asynchronous, the packet completion routine will be invoked by
     * the device layer when the HIF layer completes the request */

    return status;
}

    /* get HTC send packets from the TX queue on an endpoint */
static INLINE void GetHTCSendPackets(HTC_TARGET        *target, 
                                     HTC_ENDPOINT      *pEndpoint, 
                                     HTC_PACKET_QUEUE  *pQueue)
{
    int          creditsRequired;
    int          remainder;
    A_UINT8      sendFlags;
    HTC_PACKET   *pPacket;
    unsigned int transferLength;

    /****** NOTE : the TX lock is held when this function is called *****************/
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("+GetHTCSendPackets \n"));
     
        /* loop until we can grab as many packets out of the queue as we can */       
    while (TRUE) {    
        
        sendFlags = 0;   
            /* get packet at head, but don't remove it */
        pPacket = HTC_GET_PKT_AT_HEAD(&pEndpoint->TxQueue);       
        if (pPacket == NULL) {
            break;    
        }
        
        AR_DEBUG_PRINTF(ATH_DEBUG_SEND,(" Got head packet:0x%lX , Queue Depth: %d\n",
                (unsigned long)pPacket, HTC_PACKET_QUEUE_DEPTH(&pEndpoint->TxQueue)));
        
        transferLength = DEV_CALC_SEND_PADDED_LEN(&target->Device, pPacket->ActualLength + HTC_HDR_LENGTH);       
       
        if (transferLength <= target->TargetCreditSize) {
            creditsRequired = 1;    
        } else {
                /* figure out how many credits this message requires */
            creditsRequired = transferLength / target->TargetCreditSize;
            remainder = transferLength % target->TargetCreditSize;
            
            if (remainder) {
                creditsRequired++;
            }
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_SEND,(" Creds Required:%d   Got:%d\n",
                            creditsRequired, pEndpoint->CreditDist.TxCredits));

        if (pEndpoint->CreditDist.TxCredits < creditsRequired) {

                /* not enough credits */
            if (pPacket->Endpoint == ENDPOINT_0) {
                    /* leave it in the queue */
                break;
            }
                /* invoke the registered distribution function only if this is not
                 * endpoint 0, we let the driver layer provide more credits if it can.
                 * We pass the credit distribution list starting at the endpoint in question
                 * */

                /* set how many credits we need  */
            pEndpoint->CreditDist.TxCreditsSeek =
                                    creditsRequired - pEndpoint->CreditDist.TxCredits;
            DO_DISTRIBUTION(target,
                            HTC_CREDIT_DIST_SEEK_CREDITS,
                            "Seek Credits",
                            &pEndpoint->CreditDist);
            pEndpoint->CreditDist.TxCreditsSeek = 0;

            if (pEndpoint->CreditDist.TxCredits < creditsRequired) {
                    /* still not enough credits to send, leave packet in the queue */
                AR_DEBUG_PRINTF(ATH_DEBUG_SEND,
                    (" Not enough credits for ep %d leaving packet in queue..\n",
                    pPacket->Endpoint));
                break;
            }

        }

        pEndpoint->CreditDist.TxCredits -= creditsRequired;
        INC_HTC_EP_STAT(pEndpoint, TxCreditsConsummed, creditsRequired);

            /* check if we need credits back from the target */
        if (pEndpoint->CreditDist.TxCredits < pEndpoint->CreditDist.TxCreditsPerMaxMsg) {
                /* we are getting low on credits, see if we can ask for more from the distribution function */
            pEndpoint->CreditDist.TxCreditsSeek =
                        pEndpoint->CreditDist.TxCreditsPerMaxMsg - pEndpoint->CreditDist.TxCredits;

            DO_DISTRIBUTION(target,
                            HTC_CREDIT_DIST_SEEK_CREDITS,
                            "Seek Credits",
                            &pEndpoint->CreditDist);

            pEndpoint->CreditDist.TxCreditsSeek = 0;
                /* see if we were successful in getting more */
            if (pEndpoint->CreditDist.TxCredits < pEndpoint->CreditDist.TxCreditsPerMaxMsg) {
                    /* tell the target we need credits ASAP! */
                sendFlags |= HTC_FLAGS_NEED_CREDIT_UPDATE;
                INC_HTC_EP_STAT(pEndpoint, TxCreditLowIndications, 1);
                AR_DEBUG_PRINTF(ATH_DEBUG_SEND,(" Host Needs Credits  \n"));
            }
        }
                        
            /* now we can fully dequeue */
        pPacket = HTC_PACKET_DEQUEUE(&pEndpoint->TxQueue); 
            /* save the number of credits this packet consumed */
        pPacket->PktInfo.AsTx.CreditsUsed = creditsRequired;
            /* all TX packets are handled asynchronously */
        pPacket->Completion = HTCSendPktCompletionHandler;
        pPacket->pContext = target;
        INC_HTC_EP_STAT(pEndpoint, TxIssued, 1);
            /* save send flags */
        pPacket->PktInfo.AsTx.SendFlags = sendFlags;
        pPacket->PktInfo.AsTx.SeqNo = pEndpoint->SeqNo;         
        pEndpoint->SeqNo++;
            /* queue this packet into the caller's queue */
        HTC_PACKET_ENQUEUE(pQueue,pPacket);
    }
    
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("-GetHTCSendPackets \n"));
     
}

static void HTCAsyncSendScatterCompletion(HIF_SCATTER_REQ *pScatterReq)
{
    int                 i;    
    HTC_PACKET          *pPacket;
    HTC_ENDPOINT        *pEndpoint = (HTC_ENDPOINT *)pScatterReq->Context;
    HTC_TARGET          *target = (HTC_TARGET *)pEndpoint->target;
    A_STATUS            status = A_OK;
    HTC_PACKET_QUEUE    sendCompletes;
    
    INIT_HTC_PACKET_QUEUE(&sendCompletes);
          
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("+HTCAsyncSendScatterCompletion  TotLen: %d  Entries: %d\n",
        pScatterReq->TotalLength, pScatterReq->ValidScatterEntries));
    
    DEV_FINISH_SCATTER_OPERATION(pScatterReq);
           
    if (A_FAILED(pScatterReq->CompletionStatus)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("** Send Scatter Request Failed: %d \n",pScatterReq->CompletionStatus));            
        status = A_ERROR;
    }
    
        /* walk through the scatter list and process */
    for (i = 0; i < pScatterReq->ValidScatterEntries; i++) {
        pPacket = (HTC_PACKET *)(pScatterReq->ScatterList[i].pCallerContexts[0]);
        A_ASSERT(pPacket != NULL);
        pPacket->Status = status;
        CompleteSentPacket(target,pEndpoint,pPacket);
            /* add it to the completion queue */
        HTC_PACKET_ENQUEUE(&sendCompletes, pPacket);      
    }
    
        /* free scatter request */
    DEV_FREE_SCATTER_REQ(&target->Device,pScatterReq);
        /* complete all packets */
    DO_EP_TX_COMPLETION(pEndpoint,&sendCompletes);
               
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("-HTCAsyncSendScatterCompletion \n"));
}

    /* drain a queue and send as bundles 
     * this function may return without fully draining the queue under the following conditions :
     *    - scatter resources are exhausted
     *    - a message that will consume a partial credit will stop the bundling process early 
     *    - we drop below the minimum number of messages for a bundle 
     * */
static void HTCIssueSendBundle(HTC_ENDPOINT      *pEndpoint, 
                               HTC_PACKET_QUEUE  *pQueue, 
                               int               *pBundlesSent, 
                               int               *pTotalBundlesPkts)
{
    int                 pktsToScatter;
    unsigned int        scatterSpaceRemaining;
    HIF_SCATTER_REQ     *pScatterReq = NULL;
    int                 i, packetsInScatterReq;
    unsigned int        transferLength;
    HTC_PACKET          *pPacket;
    A_BOOL              done = FALSE;
    int                 bundlesSent = 0;
    int                 totalPktsInBundle = 0;
    HTC_TARGET          *target = pEndpoint->target;
    int                 creditRemainder = 0;
    int                 creditPad;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("+HTCIssueSendBundle \n"));
    
    while (!done) {
          
        pktsToScatter = HTC_PACKET_QUEUE_DEPTH(pQueue);
        pktsToScatter = min(pktsToScatter, target->MaxMsgPerBundle);
        
        if (pktsToScatter < HTC_MIN_HTC_MSGS_TO_BUNDLE) {
                /* not enough to bundle */
            break;    
        }
        
        pScatterReq = DEV_ALLOC_SCATTER_REQ(&target->Device); 
        
        if (pScatterReq == NULL) {
                /* no scatter resources  */
            AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("   No more scatter resources \n"));
            break;    
        }       
        
        AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("   pkts to scatter: %d \n", pktsToScatter));
        
        pScatterReq->TotalLength = 0;
        pScatterReq->ValidScatterEntries = 0;  
        
        packetsInScatterReq = 0;
        scatterSpaceRemaining = DEV_GET_MAX_BUNDLE_SEND_LENGTH(&target->Device);
        
        for (i = 0; i < pktsToScatter; i++) {
            
            pScatterReq->ScatterList[i].pCallerContexts[0] = NULL;
            
            pPacket = HTC_GET_PKT_AT_HEAD(pQueue);        
            if (pPacket == NULL) {
                A_ASSERT(FALSE);
                break;    
            }
            
            creditPad = 0;
            transferLength = DEV_CALC_SEND_PADDED_LEN(&target->Device, 
                                                      pPacket->ActualLength + HTC_HDR_LENGTH);               
                /* see if the padded transfer length falls on a credit boundary */         
            creditRemainder = transferLength % target->TargetCreditSize;
                                
            if (creditRemainder != 0) {
                    /* the transfer consumes a "partial" credit, this packet cannot be bundled unless
                     * we add additional "dummy" padding (max 255 bytes) to consume the entire credit 
                     *** NOTE: only allow the send padding if the endpoint is allowed to */
                if (pEndpoint->LocalConnectionFlags & HTC_LOCAL_CONN_FLAGS_ENABLE_SEND_BUNDLE_PADDING) {
                    if (transferLength < target->TargetCreditSize) {
                            /* special case where the transfer is less than a credit */
                        creditPad = target->TargetCreditSize - transferLength;                    
                    } else {
                        creditPad = creditRemainder;    
                    }
                                    
                        /* now check to see if we can indicate padding in the HTC header */
                    if ((creditPad > 0) && (creditPad <= 255)) {
                            /* adjust the transferlength of this packet with the new credit padding */
                        transferLength += creditPad;            
                    } else {
                            /* the amount to pad is too large, bail on this packet, we have to 
                             * send it using the non-bundled method */
                        pPacket = NULL;
                    }
                } else {
                        /* bail on this packet, user does not want padding applied */
                    pPacket = NULL;    
                }
            }                       
                       
            if (NULL == pPacket) {
                    /* can't bundle */
                done = TRUE;
                break;    
            }         
               
            if (scatterSpaceRemaining < transferLength) {
                    /* exceeds what we can transfer */
                break;    
            }
            
            scatterSpaceRemaining -= transferLength;
                /* now remove it from the queue */ 
            pPacket = HTC_PACKET_DEQUEUE(pQueue);           
                /* save it in the scatter list */
            pScatterReq->ScatterList[i].pCallerContexts[0] = pPacket;            
                /* prepare packet and flag message as part of a send bundle */               
            HTC_PREPARE_SEND_PKT(pPacket,
                                 pPacket->PktInfo.AsTx.SendFlags | HTC_FLAGS_SEND_BUNDLE, 
                                 creditPad,                                 
                                 pPacket->PktInfo.AsTx.SeqNo); 
            pScatterReq->ScatterList[i].pBuffer = pPacket->pBuffer;
            pScatterReq->ScatterList[i].Length = transferLength;
            A_ASSERT(transferLength);
            pScatterReq->TotalLength += transferLength;
            pScatterReq->ValidScatterEntries++;
            packetsInScatterReq++;             
            AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("  %d, Adding packet : 0x%lX, len:%d (remaining space:%d) \n", 
                    i, (unsigned long)pPacket,transferLength,scatterSpaceRemaining));                                                      
        }
                    
        if (packetsInScatterReq >= HTC_MIN_HTC_MSGS_TO_BUNDLE) {          
                /* send path is always asynchronous */
            pScatterReq->CompletionRoutine = HTCAsyncSendScatterCompletion;
            pScatterReq->Context = pEndpoint;
            bundlesSent++;
            totalPktsInBundle += packetsInScatterReq;
            packetsInScatterReq = 0;
            AR_DEBUG_PRINTF(ATH_DEBUG_SEND,(" Send Scatter total bytes: %d , entries: %d\n",
                                pScatterReq->TotalLength,pScatterReq->ValidScatterEntries));
            DevSubmitScatterRequest(&target->Device, pScatterReq, DEV_SCATTER_WRITE, DEV_SCATTER_ASYNC);
                /* we don't own this anymore */
            pScatterReq = NULL;
                /* try to send some more */
            continue;               
        } 
        
            /* not enough packets to use the scatter request, cleanup */
        if (pScatterReq != NULL) {
            if (packetsInScatterReq > 0) {
                    /* work backwards to requeue requests */
                for (i = (packetsInScatterReq - 1); i >= 0; i--) {
                    pPacket = (HTC_PACKET *)(pScatterReq->ScatterList[i].pCallerContexts[0]);
                    if (pPacket != NULL) {
                            /* undo any prep */
                        HTC_UNPREPARE_SEND_PKT(pPacket);
                            /* queue back to the head */
                        HTC_PACKET_ENQUEUE_TO_HEAD(pQueue,pPacket);   
                    }  
                }  
            }               
            DEV_FREE_SCATTER_REQ(&target->Device,pScatterReq);    
        }  
        
        /* if we get here, we sent all that we could, get out */
        break;  
        
    }
    
    *pBundlesSent = bundlesSent;
    *pTotalBundlesPkts = totalPktsInBundle;
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("-HTCIssueSendBundle (sent:%d) \n",bundlesSent));  
     
    return; 
}

/*
 * if there are no credits, the packet(s) remains in the queue.
 * this function returns the result of the attempt to send a queue of HTC packets */
static HTC_SEND_QUEUE_RESULT HTCTrySend(HTC_TARGET       *target,
                                        HTC_ENDPOINT     *pEndpoint,
                                        HTC_PACKET_QUEUE *pCallersSendQueue)
{
    HTC_PACKET_QUEUE      sendQueue; /* temp queue to hold packets at various stages */
    HTC_PACKET            *pPacket;
    int                   bundlesSent;
    int                   pktsInBundles;
    int                   overflow;
    HTC_SEND_QUEUE_RESULT result = HTC_SEND_QUEUE_OK;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("+HTCTrySend (Queue:0x%lX Depth:%d)\n",
            (unsigned long)pCallersSendQueue, 
            (pCallersSendQueue == NULL) ? 0 : HTC_PACKET_QUEUE_DEPTH(pCallersSendQueue)));

        /* init the local send queue */
    INIT_HTC_PACKET_QUEUE(&sendQueue);
    
    do {
        
        if (NULL == pCallersSendQueue) {
                /* caller didn't provide a queue, just wants us to check queues and send */
            break;    
        }
        
        if (HTC_QUEUE_EMPTY(pCallersSendQueue)) {
                /* empty queue */
            result = HTC_SEND_QUEUE_DROP;
            break;    
        }
  
        if (HTC_PACKET_QUEUE_DEPTH(&pEndpoint->TxQueue) >= pEndpoint->MaxTxQueueDepth) {
                    /* we've already overflowed */
            overflow = HTC_PACKET_QUEUE_DEPTH(pCallersSendQueue);    
        } else {
                /* figure out how much we will overflow by */
            overflow = HTC_PACKET_QUEUE_DEPTH(&pEndpoint->TxQueue);
            overflow += HTC_PACKET_QUEUE_DEPTH(pCallersSendQueue); 
                /* figure out how much we will overflow the TX queue by */
            overflow -= pEndpoint->MaxTxQueueDepth;     
        }
                     
            /* if overflow is negative or zero, we are okay */    
        if (overflow > 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_SEND, 
                (" Endpoint %d, TX queue will overflow :%d , Tx Depth:%d, Max:%d \n",
                pEndpoint->Id, overflow, HTC_PACKET_QUEUE_DEPTH(&pEndpoint->TxQueue), pEndpoint->MaxTxQueueDepth));      
        }   
        if ((overflow <= 0) || (pEndpoint->EpCallBacks.EpSendFull == NULL)) {
                /* all packets will fit or caller did not provide send full indication handler
                 * --  just move all of them to the local sendQueue object */
            HTC_PACKET_QUEUE_TRANSFER_TO_TAIL(&sendQueue, pCallersSendQueue);           
        } else {
            int               i;
            int               goodPkts = HTC_PACKET_QUEUE_DEPTH(pCallersSendQueue) - overflow;
                        
            A_ASSERT(goodPkts >= 0);
                /* we have overflowed, and a callback is provided */        
                /* dequeue all non-overflow packets into the sendqueue */
            for (i = 0; i < goodPkts; i++) {
                    /* pop off caller's queue*/
                pPacket = HTC_PACKET_DEQUEUE(pCallersSendQueue);
                A_ASSERT(pPacket != NULL);
                    /* insert into local queue */
                HTC_PACKET_ENQUEUE(&sendQueue,pPacket);
            }
            
                /* the caller's queue has all the packets that won't fit*/                
                /* walk through the caller's queue and indicate each one to the send full handler */            
            ITERATE_OVER_LIST_ALLOW_REMOVE(&pCallersSendQueue->QueueHead, pPacket, HTC_PACKET, ListLink) {            
                
                AR_DEBUG_PRINTF(ATH_DEBUG_SEND, (" Indicating overflowed TX packet: 0x%lX \n", 
                                            (unsigned long)pPacket));    
                if (pEndpoint->EpCallBacks.EpSendFull(pEndpoint->EpCallBacks.pContext,
                                                      pPacket) == HTC_SEND_FULL_DROP) {
                        /* callback wants the packet dropped */
                    INC_HTC_EP_STAT(pEndpoint, TxDropped, 1);
                        /* leave this one in the caller's queue for cleanup */
                } else {
                        /* callback wants to keep this packet, remove from caller's queue */
                    HTC_PACKET_REMOVE(pCallersSendQueue, pPacket);
                        /* put it in the send queue */
                    HTC_PACKET_ENQUEUE(&sendQueue,pPacket);                                      
                }
                
            } ITERATE_END;
            
            if (HTC_QUEUE_EMPTY(&sendQueue)) {
                    /* no packets made it in, caller will cleanup */
                result = HTC_SEND_QUEUE_DROP;
                break;   
            } 
        }
        
    } while (FALSE);
    
    if (result != HTC_SEND_QUEUE_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("-HTCTrySend:  \n"));
        return result;
    }

    LOCK_HTC_TX(target);
    
    if (!HTC_QUEUE_EMPTY(&sendQueue)) {
            /* transfer packets */
        HTC_PACKET_QUEUE_TRANSFER_TO_TAIL(&pEndpoint->TxQueue,&sendQueue);
        A_ASSERT(HTC_QUEUE_EMPTY(&sendQueue));
        INIT_HTC_PACKET_QUEUE(&sendQueue); 
    }
    
        /* increment tx processing count on entry */    
    pEndpoint->TxProcessCount++;
    if (pEndpoint->TxProcessCount > 1) {
            /* another thread or task is draining the TX queues on this endpoint
             * that thread will reset the tx processing count when the queue is drained */
        pEndpoint->TxProcessCount--;
        UNLOCK_HTC_TX(target);
        AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("-HTCTrySend (busy) \n"));
        return HTC_SEND_QUEUE_OK; 
    }
    
    /***** beyond this point only 1 thread may enter ******/
            
        /* now drain the endpoint TX queue for transmission as long as we have enough
         * credits */
    while (TRUE) {
          
        if (HTC_PACKET_QUEUE_DEPTH(&pEndpoint->TxQueue) == 0) {
            break;
        }
                
            /* get all the packets for this endpoint that we can for this pass */
        GetHTCSendPackets(target, pEndpoint, &sendQueue);        
     
        if (HTC_PACKET_QUEUE_DEPTH(&sendQueue) == 0) {
                /* didn't get any packets due to a lack of credits */
            break;    
        }
        
        UNLOCK_HTC_TX(target);
        
            /* any packets to send are now in our local send queue */    
         
        bundlesSent = 0;
        pktsInBundles = 0;
     
        while (TRUE) {
            
                /* try to send a bundle on each pass */            
            if ((target->SendBundlingEnabled) &&
                    (HTC_PACKET_QUEUE_DEPTH(&sendQueue) >= HTC_MIN_HTC_MSGS_TO_BUNDLE)) {
                 int temp1,temp2;       
                    /* bundling is enabled and there is at least a minimum number of packets in the send queue
                     * send what we can in this pass */                       
                 HTCIssueSendBundle(pEndpoint, &sendQueue, &temp1, &temp2);
                 bundlesSent += temp1;
                 pktsInBundles += temp2;
            }
        
                /* if not bundling or there was a packet that could not be placed in a bundle, pull it out
                 * and send it the normal way */
            pPacket = HTC_PACKET_DEQUEUE(&sendQueue);
            if (NULL == pPacket) {
                    /* local queue is fully drained */
                break;    
            }
            HTC_PREPARE_SEND_PKT(pPacket,
                                 pPacket->PktInfo.AsTx.SendFlags,
                                 0,
                                 pPacket->PktInfo.AsTx.SeqNo);  
            HTCIssueSend(target, pPacket);
            
                /* go back and see if we can bundle some more */
        }
        
        LOCK_HTC_TX(target);
        
        INC_HTC_EP_STAT(pEndpoint, TxBundles, bundlesSent);
        INC_HTC_EP_STAT(pEndpoint, TxPacketsBundled, pktsInBundles);
        
    }
        
        /* done with this endpoint, we can clear the count */
    pEndpoint->TxProcessCount = 0;
    UNLOCK_HTC_TX(target);
    
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("-HTCTrySend:  \n"));

    return HTC_SEND_QUEUE_OK;
}

A_STATUS  HTCSendPktsMultiple(HTC_HANDLE HTCHandle, HTC_PACKET_QUEUE *pPktQueue)
{
    HTC_TARGET      *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    HTC_ENDPOINT    *pEndpoint;
    HTC_PACKET      *pPacket;

    AR_DEBUG_PRINTF(ATH_DEBUG_SEND, ("+HTCSendPktsMultiple: Queue: 0x%lX, Pkts %d \n",
                    (unsigned long)pPktQueue, HTC_PACKET_QUEUE_DEPTH(pPktQueue)));
    
        /* get packet at head to figure out which endpoint these packets will go into */
    pPacket = HTC_GET_PKT_AT_HEAD(pPktQueue);
    if (NULL == pPacket) {
        AR_DEBUG_PRINTF(ATH_DEBUG_SEND, ("-HTCSendPktsMultiple \n"));
        return A_EINVAL;   
    }
    
    AR_DEBUG_ASSERT(pPacket->Endpoint < ENDPOINT_MAX);
    pEndpoint = &target->EndPoint[pPacket->Endpoint];
    
    HTCTrySend(target, pEndpoint, pPktQueue);

        /* do completion on any packets that couldn't get in */
    if (!HTC_QUEUE_EMPTY(pPktQueue)) {        
        
        HTC_PACKET_QUEUE_ITERATE_ALLOW_REMOVE(pPktQueue,pPacket) {
            if (HTC_STOPPING(target)) {
                pPacket->Status = A_ECANCELED;
            } else {
                pPacket->Status = A_NO_RESOURCE;
            } 
        } HTC_PACKET_QUEUE_ITERATE_END;
                   
        DO_EP_TX_COMPLETION(pEndpoint,pPktQueue);
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_SEND, ("-HTCSendPktsMultiple \n"));

    return A_OK;   
}

/* HTC API - HTCSendPkt */
A_STATUS HTCSendPkt(HTC_HANDLE HTCHandle, HTC_PACKET *pPacket)
{
    HTC_PACKET_QUEUE queue;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,
                    ("+-HTCSendPkt: Enter endPointId: %d, buffer: 0x%lX, length: %d \n",
                    pPacket->Endpoint, (unsigned long)pPacket->pBuffer, pPacket->ActualLength));                   
    INIT_HTC_PACKET_QUEUE_AND_ADD(&queue,pPacket); 
    return HTCSendPktsMultiple(HTCHandle, &queue);
}

/* check TX queues to drain because of credit distribution update */
static INLINE void HTCCheckEndpointTxQueues(HTC_TARGET *target)
{
    HTC_ENDPOINT                *pEndpoint;
    HTC_ENDPOINT_CREDIT_DIST    *pDistItem;

    AR_DEBUG_PRINTF(ATH_DEBUG_SEND, ("+HTCCheckEndpointTxQueues \n"));
    pDistItem = target->EpCreditDistributionListHead;

        /* run through the credit distribution list to see
         * if there are packets queued
         * NOTE: no locks need to be taken since the distribution list
         * is not dynamic (cannot be re-ordered) and we are not modifying any state */
    while (pDistItem != NULL) {
        pEndpoint = (HTC_ENDPOINT *)pDistItem->pHTCReserved;

        if (HTC_PACKET_QUEUE_DEPTH(&pEndpoint->TxQueue) > 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_SEND, (" Ep %d has %d credits and %d Packets in TX Queue \n",
                    pDistItem->Endpoint, pEndpoint->CreditDist.TxCredits, HTC_PACKET_QUEUE_DEPTH(&pEndpoint->TxQueue)));
                /* try to start the stalled queue, this list is ordered by priority.
                 * Highest priority queue get's processed first, if there are credits available the
                 * highest priority queue will get a chance to reclaim credits from lower priority
                 * ones */
            HTCTrySend(target, pEndpoint, NULL);
        }

        pDistItem = pDistItem->pNext;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_SEND, ("-HTCCheckEndpointTxQueues \n"));
}

/* process credit reports and call distribution function */
void HTCProcessCreditRpt(HTC_TARGET *target, HTC_CREDIT_REPORT *pRpt, int NumEntries, HTC_ENDPOINT_ID FromEndpoint)
{
    int             i;
    HTC_ENDPOINT    *pEndpoint;
    int             totalCredits = 0;
    A_BOOL          doDist = FALSE;

    AR_DEBUG_PRINTF(ATH_DEBUG_SEND, ("+HTCProcessCreditRpt, Credit Report Entries:%d \n", NumEntries));

        /* lock out TX while we update credits */
    LOCK_HTC_TX(target);

    for (i = 0; i < NumEntries; i++, pRpt++) {
        if (pRpt->EndpointID >= ENDPOINT_MAX) {
            AR_DEBUG_ASSERT(FALSE);
            break;
        }

        pEndpoint = &target->EndPoint[pRpt->EndpointID];

        AR_DEBUG_PRINTF(ATH_DEBUG_SEND, ("  Endpoint %d got %d credits \n",
                pRpt->EndpointID, pRpt->Credits));


#ifdef HTC_EP_STAT_PROFILING

        INC_HTC_EP_STAT(pEndpoint, TxCreditRpts, 1);
        INC_HTC_EP_STAT(pEndpoint, TxCreditsReturned, pRpt->Credits);

        if (FromEndpoint == pRpt->EndpointID) {
                /* this credit report arrived on the same endpoint indicating it arrived in an RX
                 * packet */
            INC_HTC_EP_STAT(pEndpoint, TxCreditsFromRx, pRpt->Credits);
            INC_HTC_EP_STAT(pEndpoint, TxCreditRptsFromRx, 1);
        } else if (FromEndpoint == ENDPOINT_0) {
                /* this credit arrived on endpoint 0 as a NULL message */
            INC_HTC_EP_STAT(pEndpoint, TxCreditsFromEp0, pRpt->Credits);
            INC_HTC_EP_STAT(pEndpoint, TxCreditRptsFromEp0, 1);
        } else {
                /* arrived on another endpoint */
            INC_HTC_EP_STAT(pEndpoint, TxCreditsFromOther, pRpt->Credits);
            INC_HTC_EP_STAT(pEndpoint, TxCreditRptsFromOther, 1);
        }

#endif

        if (ENDPOINT_0 == pRpt->EndpointID) {
                /* always give endpoint 0 credits back */
            pEndpoint->CreditDist.TxCredits += pRpt->Credits;
        } else {
                /* for all other endpoints, update credits to distribute, the distribution function
                 * will handle giving out credits back to the endpoints */
            pEndpoint->CreditDist.TxCreditsToDist += pRpt->Credits;
                /* flag that we have to do the distribution */
            doDist = TRUE;
        }
        
            /* refresh tx depth for distribution function that will recover these credits
             * NOTE: this is only valid when there are credits to recover! */
        pEndpoint->CreditDist.TxQueueDepth = HTC_PACKET_QUEUE_DEPTH(&pEndpoint->TxQueue);
        
        totalCredits += pRpt->Credits;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_SEND, ("  Report indicated %d credits to distribute \n", totalCredits));

    if (doDist) {
            /* this was a credit return based on a completed send operations
             * note, this is done with the lock held */
        DO_DISTRIBUTION(target,
                        HTC_CREDIT_DIST_SEND_COMPLETE,
                        "Send Complete",
                        target->EpCreditDistributionListHead->pNext);
    }

    UNLOCK_HTC_TX(target);

    if (totalCredits) {
        HTCCheckEndpointTxQueues(target);
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_SEND, ("-HTCProcessCreditRpt \n"));
}

/* flush endpoint TX queue */
static void HTCFlushEndpointTX(HTC_TARGET *target, HTC_ENDPOINT *pEndpoint, HTC_TX_TAG Tag)
{
    HTC_PACKET          *pPacket;
    HTC_PACKET_QUEUE    discardQueue;
    HTC_PACKET_QUEUE    container;

        /* initialize the discard queue */
    INIT_HTC_PACKET_QUEUE(&discardQueue);

    LOCK_HTC_TX(target);

        /* interate from the front of the TX queue and flush out packets */
    ITERATE_OVER_LIST_ALLOW_REMOVE(&pEndpoint->TxQueue.QueueHead, pPacket, HTC_PACKET, ListLink) {

            /* check for removal */
        if ((HTC_TX_PACKET_TAG_ALL == Tag) || (Tag == pPacket->PktInfo.AsTx.Tag)) {
                /* remove from queue */
            HTC_PACKET_REMOVE(&pEndpoint->TxQueue, pPacket);
                /* add it to the discard pile */
            HTC_PACKET_ENQUEUE(&discardQueue, pPacket);
        }

    } ITERATE_END;

    UNLOCK_HTC_TX(target);

        /* empty the discard queue */
    while (1) {
        pPacket = HTC_PACKET_DEQUEUE(&discardQueue);
        if (NULL == pPacket) {
            break;
        }
        pPacket->Status = A_ECANCELED;
        AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("  Flushing TX packet:0x%lX, length:%d, ep:%d tag:0x%X \n",
                (unsigned long)pPacket, pPacket->ActualLength, pPacket->Endpoint, pPacket->PktInfo.AsTx.Tag));
        INIT_HTC_PACKET_QUEUE_AND_ADD(&container,pPacket);
        DO_EP_TX_COMPLETION(pEndpoint,&container);
    }

}

void DumpCreditDist(HTC_ENDPOINT_CREDIT_DIST *pEPDist)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, ("--- EP : %d  ServiceID: 0x%X    --------------\n",
                        pEPDist->Endpoint, pEPDist->ServiceID));
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, (" this:0x%lX next:0x%lX prev:0x%lX\n",
                (unsigned long)pEPDist, (unsigned long)pEPDist->pNext, (unsigned long)pEPDist->pPrev));
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, (" DistFlags          : 0x%X \n", pEPDist->DistFlags));
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, (" TxCreditsNorm      : %d \n", pEPDist->TxCreditsNorm));
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, (" TxCreditsMin       : %d \n", pEPDist->TxCreditsMin));
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, (" TxCredits          : %d \n", pEPDist->TxCredits));
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, (" TxCreditsAssigned  : %d \n", pEPDist->TxCreditsAssigned));
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, (" TxCreditsSeek      : %d \n", pEPDist->TxCreditsSeek));
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, (" TxCreditSize       : %d \n", pEPDist->TxCreditSize));
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, (" TxCreditsPerMaxMsg : %d \n", pEPDist->TxCreditsPerMaxMsg));
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, (" TxCreditsToDist    : %d \n", pEPDist->TxCreditsToDist));
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, (" TxQueueDepth       : %d \n", 
                    HTC_PACKET_QUEUE_DEPTH(&((HTC_ENDPOINT *)pEPDist->pHTCReserved)->TxQueue)));                                      
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, ("----------------------------------------------------\n"));
}

void DumpCreditDistStates(HTC_TARGET *target)
{
    HTC_ENDPOINT_CREDIT_DIST *pEPList = target->EpCreditDistributionListHead;

    while (pEPList != NULL) {
        DumpCreditDist(pEPList);
        pEPList = pEPList->pNext;
    }

    if (target->DistributeCredits != NULL) {
        DO_DISTRIBUTION(target,
                        HTC_DUMP_CREDIT_STATE,
                        "Dump State",
                        NULL);
    }
}

/* flush all send packets from all endpoint queues */
void HTCFlushSendPkts(HTC_TARGET *target)
{
    HTC_ENDPOINT    *pEndpoint;
    int             i;

    if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_TRC)) {
        DumpCreditDistStates(target);
    }

    for (i = ENDPOINT_0; i < ENDPOINT_MAX; i++) {
        pEndpoint = &target->EndPoint[i];
        if (pEndpoint->ServiceID == 0) {
                /* not in use.. */
            continue;
        }
        HTCFlushEndpointTX(target,pEndpoint,HTC_TX_PACKET_TAG_ALL);
    }


}

/* HTC API to flush an endpoint's TX queue*/
void HTCFlushEndpoint(HTC_HANDLE HTCHandle, HTC_ENDPOINT_ID Endpoint, HTC_TX_TAG Tag)
{
    HTC_TARGET      *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    HTC_ENDPOINT    *pEndpoint = &target->EndPoint[Endpoint];

    if (pEndpoint->ServiceID == 0) {
        AR_DEBUG_ASSERT(FALSE);
        /* not in use.. */
        return;
    }

    HTCFlushEndpointTX(target, pEndpoint, Tag);
}

/* HTC API to indicate activity to the credit distribution function */
void HTCIndicateActivityChange(HTC_HANDLE      HTCHandle,
                               HTC_ENDPOINT_ID Endpoint,
                               A_BOOL          Active)
{
    HTC_TARGET      *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    HTC_ENDPOINT    *pEndpoint = &target->EndPoint[Endpoint];
    A_BOOL          doDist = FALSE;

    if (pEndpoint->ServiceID == 0) {
        AR_DEBUG_ASSERT(FALSE);
        /* not in use.. */
        return;
    }

    LOCK_HTC_TX(target);

    if (Active) {
        if (!(pEndpoint->CreditDist.DistFlags & HTC_EP_ACTIVE)) {
                /* mark active now */
            pEndpoint->CreditDist.DistFlags |= HTC_EP_ACTIVE;
            doDist = TRUE;
        }
    } else {
        if (pEndpoint->CreditDist.DistFlags & HTC_EP_ACTIVE) {
                /* mark inactive now */
            pEndpoint->CreditDist.DistFlags &= ~HTC_EP_ACTIVE;
            doDist = TRUE;
        }
    }

    if (doDist) {
            /* indicate current Tx Queue depth to the credit distribution function */
        pEndpoint->CreditDist.TxQueueDepth = HTC_PACKET_QUEUE_DEPTH(&pEndpoint->TxQueue);
        /* do distribution again based on activity change
         * note, this is done with the lock held */
        DO_DISTRIBUTION(target,
                        HTC_CREDIT_DIST_ACTIVITY_CHANGE,
                        "Activity Change",
                        target->EpCreditDistributionListHead->pNext);
    }

    UNLOCK_HTC_TX(target);

    if (doDist && !Active) {
        /* if a stream went inactive and this resulted in a credit distribution change,
         * some credits may now be available for HTC packets that are stuck in
         * HTC queues */
        HTCCheckEndpointTxQueues(target);
    }
}

A_BOOL HTCIsEndpointActive(HTC_HANDLE      HTCHandle,
                           HTC_ENDPOINT_ID Endpoint)
{
    HTC_TARGET      *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    HTC_ENDPOINT    *pEndpoint = &target->EndPoint[Endpoint];

    if (pEndpoint->ServiceID == 0) {
        return FALSE;
    }
    
    if (pEndpoint->CreditDist.DistFlags & HTC_EP_ACTIVE) {
        return TRUE;
    }
    
    return FALSE;
}
