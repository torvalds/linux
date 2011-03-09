//------------------------------------------------------------------------------
// <copyright file="htc_recv.c" company="Atheros">
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

#define HTCIssueRecv(t, p) \
    DevRecvPacket(&(t)->Device,  \
                  (p),          \
                  (p)->ActualLength)

#define DO_RCV_COMPLETION(e,q)  DoRecvCompletion(e,q)

#define DUMP_RECV_PKT_INFO(pP) \
    AR_DEBUG_PRINTF(ATH_DEBUG_RECV, (" HTC RECV packet 0x%lX (%d bytes) (hdr:0x%X) on ep : %d \n", \
                        (unsigned long)(pP),                   \
                        (pP)->ActualLength,                    \
                        (pP)->PktInfo.AsRx.ExpectedHdr,        \
                        (pP)->Endpoint))                         
                        
#ifdef HTC_EP_STAT_PROFILING
#define HTC_RX_STAT_PROFILE(t,ep,numLookAheads)        \
{                                                      \
    INC_HTC_EP_STAT((ep), RxReceived, 1);              \
    if ((numLookAheads) == 1) {                        \
        INC_HTC_EP_STAT((ep), RxLookAheads, 1);        \
    } else if ((numLookAheads) > 1) {                  \
        INC_HTC_EP_STAT((ep), RxBundleLookAheads, 1);  \
    }                                                  \
}
#else
#define HTC_RX_STAT_PROFILE(t,ep,lookAhead)
#endif

static void DoRecvCompletion(HTC_ENDPOINT     *pEndpoint,
                             HTC_PACKET_QUEUE *pQueueToIndicate)
{           
    
    do {
        
        if (HTC_QUEUE_EMPTY(pQueueToIndicate)) {
                /* nothing to indicate */
            break;    
        }
 
        if (pEndpoint->EpCallBacks.EpRecvPktMultiple != NULL) {    
            AR_DEBUG_PRINTF(ATH_DEBUG_RECV, (" HTC calling ep %d, recv multiple callback (%d pkts) \n",
                     pEndpoint->Id, HTC_PACKET_QUEUE_DEPTH(pQueueToIndicate)));
                /* a recv multiple handler is being used, pass the queue to the handler */                             
            pEndpoint->EpCallBacks.EpRecvPktMultiple(pEndpoint->EpCallBacks.pContext,
                                                     pQueueToIndicate);
            INIT_HTC_PACKET_QUEUE(pQueueToIndicate);        
        } else {
            HTC_PACKET *pPacket;  
            /* using legacy EpRecv */         
            do {
                pPacket = HTC_PACKET_DEQUEUE(pQueueToIndicate);
                AR_DEBUG_PRINTF(ATH_DEBUG_RECV, (" HTC calling ep %d recv callback on packet 0x%lX \n", \
                        pEndpoint->Id, (unsigned long)(pPacket)));
                pEndpoint->EpCallBacks.EpRecv(pEndpoint->EpCallBacks.pContext, pPacket);                                              
            } while (!HTC_QUEUE_EMPTY(pQueueToIndicate));                                              
        }
        
    } while (FALSE);

}

static INLINE A_STATUS HTCProcessTrailer(HTC_TARGET *target,
                                         A_UINT8    *pBuffer,
                                         int         Length,
                                         A_UINT32   *pNextLookAheads,
                                         int        *pNumLookAheads,
                                         HTC_ENDPOINT_ID FromEndpoint)
{
    HTC_RECORD_HDR          *pRecord;
    A_UINT8                 *pRecordBuf;
    HTC_LOOKAHEAD_REPORT    *pLookAhead;
    A_UINT8                 *pOrigBuffer;
    int                     origLength;
    A_STATUS                status;

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV, ("+HTCProcessTrailer (length:%d) \n", Length));

    if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_RECV)) {
        AR_DEBUG_PRINTBUF(pBuffer,Length,"Recv Trailer");
    }

    pOrigBuffer = pBuffer;
    origLength = Length;
    status = A_OK;
    
    while (Length > 0) {

        if (Length < sizeof(HTC_RECORD_HDR)) {
            status = A_EPROTO;
            break;
        }
            /* these are byte aligned structs */
        pRecord = (HTC_RECORD_HDR *)pBuffer;
        Length -= sizeof(HTC_RECORD_HDR);
        pBuffer += sizeof(HTC_RECORD_HDR);

        if (pRecord->Length > Length) {
                /* no room left in buffer for record */
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                (" invalid record length: %d (id:%d) buffer has: %d bytes left \n",
                        pRecord->Length, pRecord->RecordID, Length));
            status = A_EPROTO;
            break;
        }
            /* start of record follows the header */
        pRecordBuf = pBuffer;

        switch (pRecord->RecordID) {
            case HTC_RECORD_CREDITS:
                AR_DEBUG_ASSERT(pRecord->Length >= sizeof(HTC_CREDIT_REPORT));
                HTCProcessCreditRpt(target,
                                    (HTC_CREDIT_REPORT *)pRecordBuf,
                                    pRecord->Length / (sizeof(HTC_CREDIT_REPORT)),
                                    FromEndpoint);
                break;
            case HTC_RECORD_LOOKAHEAD:
                AR_DEBUG_ASSERT(pRecord->Length >= sizeof(HTC_LOOKAHEAD_REPORT));
                pLookAhead = (HTC_LOOKAHEAD_REPORT *)pRecordBuf;
                if ((pLookAhead->PreValid == ((~pLookAhead->PostValid) & 0xFF)) &&
                    (pNextLookAheads != NULL)) {

                    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,
                                (" LookAhead Report Found (pre valid:0x%X, post valid:0x%X) \n",
                                pLookAhead->PreValid,
                                pLookAhead->PostValid));

                        /* look ahead bytes are valid, copy them over */
                    ((A_UINT8 *)(&pNextLookAheads[0]))[0] = pLookAhead->LookAhead[0];
                    ((A_UINT8 *)(&pNextLookAheads[0]))[1] = pLookAhead->LookAhead[1];
                    ((A_UINT8 *)(&pNextLookAheads[0]))[2] = pLookAhead->LookAhead[2];
                    ((A_UINT8 *)(&pNextLookAheads[0]))[3] = pLookAhead->LookAhead[3];

#ifdef ATH_DEBUG_MODULE
                    if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_RECV)) {
                        DebugDumpBytes((A_UINT8 *)pNextLookAheads,4,"Next Look Ahead");
                    }
#endif
                        /* just one normal lookahead */
                    *pNumLookAheads = 1;
                }
                break;
            case HTC_RECORD_LOOKAHEAD_BUNDLE:
                AR_DEBUG_ASSERT(pRecord->Length >= sizeof(HTC_BUNDLED_LOOKAHEAD_REPORT));
                if (pRecord->Length >= sizeof(HTC_BUNDLED_LOOKAHEAD_REPORT) &&
                    (pNextLookAheads != NULL)) {                   
                    HTC_BUNDLED_LOOKAHEAD_REPORT    *pBundledLookAheadRpt;
                    int                             i;
                    
                    pBundledLookAheadRpt = (HTC_BUNDLED_LOOKAHEAD_REPORT *)pRecordBuf;
                    
#ifdef ATH_DEBUG_MODULE
                    if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_RECV)) {
                        DebugDumpBytes(pRecordBuf,pRecord->Length,"Bundle LookAhead");
                    }
#endif
                    
                    if ((pRecord->Length / (sizeof(HTC_BUNDLED_LOOKAHEAD_REPORT))) >
                            HTC_HOST_MAX_MSG_PER_BUNDLE) {
                            /* this should never happen, the target restricts the number
                             * of messages per bundle configured by the host */        
                        A_ASSERT(FALSE);
                        status = A_EPROTO;
                        break;        
                    }
                                         
                    for (i = 0; i < (int)(pRecord->Length / (sizeof(HTC_BUNDLED_LOOKAHEAD_REPORT))); i++) {
                        ((A_UINT8 *)(&pNextLookAheads[i]))[0] = pBundledLookAheadRpt->LookAhead[0];
                        ((A_UINT8 *)(&pNextLookAheads[i]))[1] = pBundledLookAheadRpt->LookAhead[1];
                        ((A_UINT8 *)(&pNextLookAheads[i]))[2] = pBundledLookAheadRpt->LookAhead[2];
                        ((A_UINT8 *)(&pNextLookAheads[i]))[3] = pBundledLookAheadRpt->LookAhead[3];
                        pBundledLookAheadRpt++;
                    }
                    
                    *pNumLookAheads = i;
                }               
                break;
            default:
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, (" unhandled record: id:%d length:%d \n",
                        pRecord->RecordID, pRecord->Length));
                break;
        }

        if (A_FAILED(status)) {
            break;
        }

            /* advance buffer past this record for next time around */
        pBuffer += pRecord->Length;
        Length -= pRecord->Length;
    }

#ifdef ATH_DEBUG_MODULE
    if (A_FAILED(status)) {
        DebugDumpBytes(pOrigBuffer,origLength,"BAD Recv Trailer");
    }
#endif

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV, ("-HTCProcessTrailer \n"));
    return status;

}

/* process a received message (i.e. strip off header, process any trailer data)
 * note : locks must be released when this function is called */
static A_STATUS HTCProcessRecvHeader(HTC_TARGET *target, 
                                     HTC_PACKET *pPacket, 
                                     A_UINT32   *pNextLookAheads, 
                                     int        *pNumLookAheads)
{
    A_UINT8   temp;
    A_UINT8   *pBuf;
    A_STATUS  status = A_OK;
    A_UINT16  payloadLen;
    A_UINT32  lookAhead;

    pBuf = pPacket->pBuffer;
    
    if (pNumLookAheads != NULL) {
        *pNumLookAheads = 0;
    }
    
    AR_DEBUG_PRINTF(ATH_DEBUG_RECV, ("+HTCProcessRecvHeader \n"));

    if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_RECV)) {
        AR_DEBUG_PRINTBUF(pBuf,pPacket->ActualLength,"HTC Recv PKT");
    }

    do {
        /* note, we cannot assume the alignment of pBuffer, so we use the safe macros to
         * retrieve 16 bit fields */
        payloadLen = A_GET_UINT16_FIELD(pBuf, HTC_FRAME_HDR, PayloadLen);
        
        ((A_UINT8 *)&lookAhead)[0] = pBuf[0];
        ((A_UINT8 *)&lookAhead)[1] = pBuf[1];
        ((A_UINT8 *)&lookAhead)[2] = pBuf[2];
        ((A_UINT8 *)&lookAhead)[3] = pBuf[3];

        if (pPacket->PktInfo.AsRx.HTCRxFlags & HTC_RX_PKT_REFRESH_HDR) {
                /* refresh expected hdr, since this was unknown at the time we grabbed the packets
                 * as part of a bundle */
            pPacket->PktInfo.AsRx.ExpectedHdr = lookAhead;
                /* refresh actual length since we now have the real header */
            pPacket->ActualLength = payloadLen + HTC_HDR_LENGTH;
            
                /* validate the actual header that was refreshed  */ 
            if (pPacket->ActualLength > pPacket->BufferLength) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                    ("Refreshed HDR payload length (%d) in bundled RECV is invalid (hdr: 0x%X) \n", 
                    payloadLen, lookAhead));
                    /* limit this to max buffer just to print out some of the buffer */    
                pPacket->ActualLength = min(pPacket->ActualLength, pPacket->BufferLength);
                status = A_EPROTO;
                break;    
            }
            
            if (pPacket->Endpoint != A_GET_UINT8_FIELD(pBuf, HTC_FRAME_HDR, EndpointID)) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                    ("Refreshed HDR endpoint (%d) does not match expected endpoint (%d) \n", 
                    A_GET_UINT8_FIELD(pBuf, HTC_FRAME_HDR, EndpointID), pPacket->Endpoint));
                status = A_EPROTO;
                break;      
            }   
        }
                
        if (lookAhead != pPacket->PktInfo.AsRx.ExpectedHdr) {
            /* somehow the lookahead that gave us the full read length did not
             * reflect the actual header in the pending message */
             AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                    ("HTCProcessRecvHeader, lookahead mismatch! (pPkt:0x%lX flags:0x%X) \n", 
                        (unsigned long)pPacket, pPacket->PktInfo.AsRx.HTCRxFlags));
#ifdef ATH_DEBUG_MODULE
             DebugDumpBytes((A_UINT8 *)&pPacket->PktInfo.AsRx.ExpectedHdr,4,"Expected Message LookAhead");
             DebugDumpBytes(pBuf,sizeof(HTC_FRAME_HDR),"Current Frame Header");
#ifdef HTC_CAPTURE_LAST_FRAME
            DebugDumpBytes((A_UINT8 *)&target->LastFrameHdr,sizeof(HTC_FRAME_HDR),"Last Frame Header");
            if (target->LastTrailerLength != 0) {
                DebugDumpBytes(target->LastTrailer,
                               target->LastTrailerLength,
                               "Last trailer");
            }
#endif
#endif
            status = A_EPROTO;
            break;
        }

            /* get flags */
        temp = A_GET_UINT8_FIELD(pBuf, HTC_FRAME_HDR, Flags);

        if (temp & HTC_FLAGS_RECV_TRAILER) {
            /* this packet has a trailer */

                /* extract the trailer length in control byte 0 */
            temp = A_GET_UINT8_FIELD(pBuf, HTC_FRAME_HDR, ControlBytes[0]);

            if ((temp < sizeof(HTC_RECORD_HDR)) || (temp > payloadLen)) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                    ("HTCProcessRecvHeader, invalid header (payloadlength should be :%d, CB[0] is:%d) \n",
                        payloadLen, temp));
                status = A_EPROTO;
                break;
            }

            if (pPacket->PktInfo.AsRx.HTCRxFlags & HTC_RX_PKT_IGNORE_LOOKAHEAD) {
                    /* this packet was fetched as part of an HTC bundle, the embedded lookahead is
                     * not valid since the next packet may have already been fetched as part of the
                     * bundle */
                pNextLookAheads = NULL;   
                pNumLookAheads = NULL;     
            }
            
                /* process trailer data that follows HDR + application payload */
            status = HTCProcessTrailer(target,
                                       (pBuf + HTC_HDR_LENGTH + payloadLen - temp),
                                       temp,
                                       pNextLookAheads,
                                       pNumLookAheads,
                                       pPacket->Endpoint);

            if (A_FAILED(status)) {
                break;
            }

#ifdef HTC_CAPTURE_LAST_FRAME
            A_MEMCPY(target->LastTrailer, (pBuf + HTC_HDR_LENGTH + payloadLen - temp), temp);
            target->LastTrailerLength = temp;
#endif
                /* trim length by trailer bytes */
            pPacket->ActualLength -= temp;
        }
#ifdef HTC_CAPTURE_LAST_FRAME
         else {
            target->LastTrailerLength = 0;
        }
#endif

            /* if we get to this point, the packet is good */
            /* remove header and adjust length */
        pPacket->pBuffer += HTC_HDR_LENGTH;
        pPacket->ActualLength -= HTC_HDR_LENGTH;

    } while (FALSE);

    if (A_FAILED(status)) {
            /* dump the whole packet */
#ifdef ATH_DEBUG_MODULE
        DebugDumpBytes(pBuf,pPacket->ActualLength < 256 ? pPacket->ActualLength : 256 ,"BAD HTC Recv PKT");
#endif
    } else {
#ifdef HTC_CAPTURE_LAST_FRAME
        A_MEMCPY(&target->LastFrameHdr,pBuf,sizeof(HTC_FRAME_HDR));
#endif
        if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_RECV)) {
            if (pPacket->ActualLength > 0) {
                AR_DEBUG_PRINTBUF(pPacket->pBuffer,pPacket->ActualLength,"HTC - Application Msg");
            }
        }
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV, ("-HTCProcessRecvHeader \n"));
    return status;
}

static INLINE void HTCAsyncRecvCheckMorePackets(HTC_TARGET  *target, 
                                                A_UINT32    NextLookAheads[], 
                                                int         NumLookAheads,
                                                A_BOOL      CheckMoreMsgs)
{
        /* was there a lookahead for the next packet? */
    if (NumLookAheads > 0) {
        A_STATUS nextStatus;
        int      fetched = 0;
        AR_DEBUG_PRINTF(ATH_DEBUG_RECV,
                        ("HTCAsyncRecvCheckMorePackets - num lookaheads were non-zero : %d \n",
                         NumLookAheads));
            /* force status re-check */                    
        REF_IRQ_STATUS_RECHECK(&target->Device);
            /* we have more packets, get the next packet fetch started */
        nextStatus = HTCRecvMessagePendingHandler(target, NextLookAheads, NumLookAheads, NULL, &fetched);
        if (A_EPROTO == nextStatus) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("Next look ahead from recv header was INVALID\n"));
#ifdef ATH_DEBUG_MODULE
            DebugDumpBytes((A_UINT8 *)NextLookAheads,
                            NumLookAheads * (sizeof(A_UINT32)),
                            "BAD lookaheads from lookahead report");
#endif
        }
        if (A_SUCCESS(nextStatus) && !fetched) {
                /* we could not fetch any more packets due to resources */
            DevAsyncIrqProcessComplete(&target->Device);        
        }
    } else {
        if (CheckMoreMsgs) {
            AR_DEBUG_PRINTF(ATH_DEBUG_RECV,
                ("HTCAsyncRecvCheckMorePackets - rechecking for more messages...\n"));
            /* if we did not get anything on the look-ahead,
             * call device layer to asynchronously re-check for messages. If we can keep the async
             * processing going we get better performance.  If there is a pending message we will keep processing
             * messages asynchronously which should pipeline things nicely */
            DevCheckPendingRecvMsgsAsync(&target->Device);
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_RECV, ("HTCAsyncRecvCheckMorePackets - no check \n"));    
        }
    }
    
     
}      

    /* unload the recv completion queue */
static INLINE void DrainRecvIndicationQueue(HTC_TARGET *target, HTC_ENDPOINT *pEndpoint)
{
    HTC_PACKET_QUEUE     recvCompletions;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_RECV, ("+DrainRecvIndicationQueue \n"));
                
    INIT_HTC_PACKET_QUEUE(&recvCompletions);
    
    LOCK_HTC_RX(target);
    
            /* increment rx processing count on entry */    
    pEndpoint->RxProcessCount++;
    if (pEndpoint->RxProcessCount > 1) {
         pEndpoint->RxProcessCount--;
            /* another thread or task is draining the RX completion queue on this endpoint
             * that thread will reset the rx processing count when the queue is drained */
         UNLOCK_HTC_RX(target);
         return;
    }
    
    /******* at this point only 1 thread may enter ******/
     
    while (TRUE) { 
                
            /* transfer items from main recv queue to the local one so we can release the lock */ 
        HTC_PACKET_QUEUE_TRANSFER_TO_TAIL(&recvCompletions, &pEndpoint->RecvIndicationQueue);
            
        if (HTC_QUEUE_EMPTY(&recvCompletions)) {
                /* all drained */
            break;    
        }
        
            /* release lock while we do the recv completions 
             * other threads can now queue more recv completions */
        UNLOCK_HTC_RX(target);
        
        AR_DEBUG_PRINTF(ATH_DEBUG_RECV, 
                ("DrainRecvIndicationQueue : completing %d RECV packets \n",
                                        HTC_PACKET_QUEUE_DEPTH(&recvCompletions)));
            /* do completion */
        DO_RCV_COMPLETION(pEndpoint,&recvCompletions);     
              
            /* re-acquire lock to grab some more completions */
        LOCK_HTC_RX(target);    
    }
    
        /* reset count */
    pEndpoint->RxProcessCount = 0;       
    UNLOCK_HTC_RX(target);
    
    AR_DEBUG_PRINTF(ATH_DEBUG_RECV, ("-DrainRecvIndicationQueue \n"));
  
}

    /* optimization for recv packets, we can indicate a "hint" that there are more
     * single-packets to fetch on this endpoint */
#define SET_MORE_RX_PACKET_INDICATION_FLAG(L,N,E,P) \
    if ((N) > 0) { SetRxPacketIndicationFlags((L)[0],(E),(P)); }

    /* for bundled frames, we can force the flag to indicate there are more packets */
#define FORCE_MORE_RX_PACKET_INDICATION_FLAG(P) \
    (P)->PktInfo.AsRx.IndicationFlags |= HTC_RX_FLAGS_INDICATE_MORE_PKTS; 
   
   /* note: this function can be called with the RX lock held */     
static INLINE void SetRxPacketIndicationFlags(A_UINT32      LookAhead, 
                                              HTC_ENDPOINT  *pEndpoint, 
                                              HTC_PACKET    *pPacket)
{
    HTC_FRAME_HDR *pHdr = (HTC_FRAME_HDR *)&LookAhead;
        /* check to see if the "next" packet is from the same endpoint of the
           completing packet */
    if (pHdr->EndpointID == pPacket->Endpoint) {
            /* check that there is a buffer available to actually fetch it */
        if (!HTC_QUEUE_EMPTY(&pEndpoint->RxBuffers)) {                        
                /* provide a hint that there are more RX packets to fetch */
            FORCE_MORE_RX_PACKET_INDICATION_FLAG(pPacket);        
        }             
    }                  
}

     
/* asynchronous completion handler for recv packet fetching, when the device layer
 * completes a read request, it will call this completion handler */
void HTCRecvCompleteHandler(void *Context, HTC_PACKET *pPacket)
{
    HTC_TARGET      *target = (HTC_TARGET *)Context;
    HTC_ENDPOINT    *pEndpoint;
    A_UINT32        nextLookAheads[HTC_HOST_MAX_MSG_PER_BUNDLE];
    int             numLookAheads = 0;
    A_STATUS        status;
    A_BOOL          checkMorePkts = TRUE;

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV, ("+HTCRecvCompleteHandler (pkt:0x%lX, status:%d, ep:%d) \n",
                (unsigned long)pPacket, pPacket->Status, pPacket->Endpoint));

    A_ASSERT(!IS_DEV_IRQ_PROC_SYNC_MODE(&target->Device));
    AR_DEBUG_ASSERT(pPacket->Endpoint < ENDPOINT_MAX);
    pEndpoint = &target->EndPoint[pPacket->Endpoint];
    pPacket->Completion = NULL;

        /* get completion status */
    status = pPacket->Status;

    do {
        
        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HTCRecvCompleteHandler: request failed (status:%d, ep:%d) \n",
                pPacket->Status, pPacket->Endpoint));
            break;
        }
            /* process the header for any trailer data */
        status = HTCProcessRecvHeader(target,pPacket,nextLookAheads,&numLookAheads);

        if (A_FAILED(status)) {
            break;
        }
        
        if (pPacket->PktInfo.AsRx.HTCRxFlags & HTC_RX_PKT_IGNORE_LOOKAHEAD) {
                /* this packet was part of a bundle that had to be broken up. 
                 * It was fetched one message at a time.  There may be other asynchronous reads queued behind this one.
                 * Do no issue another check for more packets since the last one in the series of requests
                 * will handle it */
            checkMorePkts = FALSE;    
        }
          
        DUMP_RECV_PKT_INFO(pPacket);    
        LOCK_HTC_RX(target);
        SET_MORE_RX_PACKET_INDICATION_FLAG(nextLookAheads,numLookAheads,pEndpoint,pPacket);
            /* we have a good packet, queue it to the completion queue */
        HTC_PACKET_ENQUEUE(&pEndpoint->RecvIndicationQueue,pPacket);
        HTC_RX_STAT_PROFILE(target,pEndpoint,numLookAheads);
        UNLOCK_HTC_RX(target);     
       
            /* check for more recv packets before indicating */
        HTCAsyncRecvCheckMorePackets(target,nextLookAheads,numLookAheads,checkMorePkts);

    } while (FALSE);

    if (A_FAILED(status)) {
         AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                         ("HTCRecvCompleteHandler , message fetch failed (status = %d) \n",
                         status));
            /* recycle this packet */
        HTC_RECYCLE_RX_PKT(target, pPacket, pEndpoint);
    } else {
            /* a good packet was queued, drain the queue */
        DrainRecvIndicationQueue(target,pEndpoint);     
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV, ("-HTCRecvCompleteHandler\n"));
}

/* synchronously wait for a control message from the target,
 * This function is used at initialization time ONLY.  At init messages
 * on ENDPOINT 0 are expected. */
A_STATUS HTCWaitforControlMessage(HTC_TARGET *target, HTC_PACKET **ppControlPacket)
{
    A_STATUS        status;
    A_UINT32        lookAhead;
    HTC_PACKET      *pPacket = NULL;
    HTC_FRAME_HDR   *pHdr;

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("+HTCWaitforControlMessage \n"));

    do  {

        *ppControlPacket = NULL;

            /* call the polling function to see if we have a message */
        status = DevPollMboxMsgRecv(&target->Device,
                                    &lookAhead,
                                    HTC_TARGET_RESPONSE_TIMEOUT);

        if (A_FAILED(status)) {
            break;
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_RECV,
                ("HTCWaitforControlMessage : lookAhead : 0x%X \n", lookAhead));

            /* check the lookahead */
        pHdr = (HTC_FRAME_HDR *)&lookAhead;

        if (pHdr->EndpointID != ENDPOINT_0) {
                /* unexpected endpoint number, should be zero */
            AR_DEBUG_ASSERT(FALSE);
            status = A_EPROTO;
            break;
        }

        if (A_FAILED(status)) {
                /* bad message */
            AR_DEBUG_ASSERT(FALSE);
            status = A_EPROTO;
            break;
        }

        pPacket = HTC_ALLOC_CONTROL_RX(target);

        if (pPacket == NULL) {
            AR_DEBUG_ASSERT(FALSE);
            status = A_NO_MEMORY;
            break;
        }
        
        pPacket->PktInfo.AsRx.HTCRxFlags = 0;
        pPacket->PktInfo.AsRx.ExpectedHdr = lookAhead;
        pPacket->ActualLength = pHdr->PayloadLen + HTC_HDR_LENGTH;

        if (pPacket->ActualLength > pPacket->BufferLength) {
            AR_DEBUG_ASSERT(FALSE);
            status = A_EPROTO;
            break;
        }

            /* we want synchronous operation */
        pPacket->Completion = NULL;

            /* get the message from the device, this will block */
        status = HTCIssueRecv(target, pPacket);

        if (A_FAILED(status)) {
            break;
        }

            /* process receive header */
        status = HTCProcessRecvHeader(target,pPacket,NULL,NULL);

        pPacket->Status = status;

        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                    ("HTCWaitforControlMessage, HTCProcessRecvHeader failed (status = %d) \n",
                     status));
            break;
        }

            /* give the caller this control message packet, they are responsible to free */
        *ppControlPacket = pPacket;

    } while (FALSE);

    if (A_FAILED(status)) {
        if (pPacket != NULL) {
                /* cleanup buffer on error */
            HTC_FREE_CONTROL_RX(target,pPacket);
        }
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("-HTCWaitforControlMessage \n"));

    return status;
}

static A_STATUS AllocAndPrepareRxPackets(HTC_TARGET       *target, 
                                         A_UINT32         LookAheads[], 
                                         int              Messages,                                        
                                         HTC_ENDPOINT     *pEndpoint, 
                                         HTC_PACKET_QUEUE *pQueue)
{
    A_STATUS         status = A_OK;
    HTC_PACKET      *pPacket;
    HTC_FRAME_HDR   *pHdr;
    int              i,j;
    int              numMessages;
    int              fullLength;
    A_BOOL           noRecycle;
            
        /* lock RX while we assemble the packet buffers */
    LOCK_HTC_RX(target);
                        
    for (i = 0; i < Messages; i++) {   
         
        pHdr = (HTC_FRAME_HDR *)&LookAheads[i];

        if (pHdr->EndpointID >= ENDPOINT_MAX) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Invalid Endpoint in look-ahead: %d \n",pHdr->EndpointID));
                /* invalid endpoint */
            status = A_EPROTO;
            break;
        }

        if (pHdr->EndpointID != pEndpoint->Id) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Invalid Endpoint in look-ahead: %d should be : %d (index:%d)\n",
                pHdr->EndpointID, pEndpoint->Id, i));
                /* invalid endpoint */
            status = A_EPROTO;
            break;    
        }    
       
        if (pHdr->PayloadLen > HTC_MAX_PAYLOAD_LENGTH) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Payload length %d exceeds max HTC : %d !\n",
                    pHdr->PayloadLen, (A_UINT32)HTC_MAX_PAYLOAD_LENGTH));
            status = A_EPROTO;
            break;
        }

        if (0 == pEndpoint->ServiceID) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Endpoint %d is not connected !\n",pHdr->EndpointID));
                /* endpoint isn't even connected */
            status = A_EPROTO;
            break;
        }

        if ((pHdr->Flags & HTC_FLAGS_RECV_BUNDLE_CNT_MASK) == 0) {
                /* HTC header only indicates 1 message to fetch */
            numMessages = 1;
        } else {
                /* HTC header indicates that every packet to follow has the same padded length so that it can
                 * be optimally fetched as a full bundle */
            numMessages = (pHdr->Flags & HTC_FLAGS_RECV_BUNDLE_CNT_MASK) >> HTC_FLAGS_RECV_BUNDLE_CNT_SHIFT;
                /* the count doesn't include the starter frame, just a count of frames to follow */
            numMessages++;
            A_ASSERT(numMessages <= target->MaxMsgPerBundle);          
            INC_HTC_EP_STAT(pEndpoint, RxBundleIndFromHdr, 1);
            AR_DEBUG_PRINTF(ATH_DEBUG_RECV,
                ("HTC header indicates :%d messages can be fetched as a bundle \n",numMessages));           
        }
     
        fullLength = DEV_CALC_RECV_PADDED_LEN(&target->Device,pHdr->PayloadLen + sizeof(HTC_FRAME_HDR));
            
            /* get packet buffers for each message, if there was a bundle detected in the header,
             * use pHdr as a template to fetch all packets in the bundle */        
        for (j = 0; j < numMessages; j++) {  
            
                /* reset flag, any packets allocated using the RecvAlloc() API cannot be recycled on cleanup,
                 * they must be explicitly returned */
            noRecycle = FALSE;
                                                                                   
            if (pEndpoint->EpCallBacks.EpRecvAlloc != NULL) {
                UNLOCK_HTC_RX(target);
                noRecycle = TRUE;
                    /* user is using a per-packet allocation callback */
                pPacket = pEndpoint->EpCallBacks.EpRecvAlloc(pEndpoint->EpCallBacks.pContext,
                                                             pEndpoint->Id,
                                                             fullLength);
                LOCK_HTC_RX(target);
    
            } else if ((pEndpoint->EpCallBacks.EpRecvAllocThresh != NULL) &&
                       (fullLength > pEndpoint->EpCallBacks.RecvAllocThreshold)) { 
                INC_HTC_EP_STAT(pEndpoint,RxAllocThreshHit,1);
                INC_HTC_EP_STAT(pEndpoint,RxAllocThreshBytes,pHdr->PayloadLen);                
                    /* threshold was hit, call the special recv allocation callback */        
                UNLOCK_HTC_RX(target);
                noRecycle = TRUE;
                    /* user wants to allocate packets above a certain threshold */
                pPacket = pEndpoint->EpCallBacks.EpRecvAllocThresh(pEndpoint->EpCallBacks.pContext,
                                                                   pEndpoint->Id,
                                                                   fullLength);
                LOCK_HTC_RX(target);        
                        
            } else {
                    /* user is using a refill handler that can refill multiple HTC buffers */
                    
                    /* get a packet from the endpoint recv queue */
                pPacket = HTC_PACKET_DEQUEUE(&pEndpoint->RxBuffers);
    
                if (NULL == pPacket) {
                        /* check for refill handler */
                    if (pEndpoint->EpCallBacks.EpRecvRefill != NULL) {
                        UNLOCK_HTC_RX(target);
                            /* call the re-fill handler */
                        pEndpoint->EpCallBacks.EpRecvRefill(pEndpoint->EpCallBacks.pContext,
                                                            pEndpoint->Id);
                        LOCK_HTC_RX(target);
                            /* check if we have more buffers */
                        pPacket = HTC_PACKET_DEQUEUE(&pEndpoint->RxBuffers);
                            /* fall through */
                    }
                }
            }
    
            if (NULL == pPacket) {
                    /* this is not an error, we simply need to mark that we are waiting for buffers.*/
                target->RecvStateFlags |= HTC_RECV_WAIT_BUFFERS;
                target->EpWaitingForBuffers = pEndpoint->Id;
                status = A_NO_RESOURCE;
                break;
            }
                             
            AR_DEBUG_ASSERT(pPacket->Endpoint == pEndpoint->Id);
                /* clear flags */
            pPacket->PktInfo.AsRx.HTCRxFlags = 0;
            pPacket->PktInfo.AsRx.IndicationFlags = 0;
            pPacket->Status = A_OK;
            
            if (noRecycle) {
                    /* flag that these packets cannot be recycled, they have to be returned to the 
                     * user */
                pPacket->PktInfo.AsRx.HTCRxFlags |= HTC_RX_PKT_NO_RECYCLE; 
            }
                /* add packet to queue (also incase we need to cleanup down below)  */
            HTC_PACKET_ENQUEUE(pQueue,pPacket);
            
            if (HTC_STOPPING(target)) {
                status = A_ECANCELED;
                break;
            }
    
                /* make sure this message can fit in the endpoint buffer */
            if ((A_UINT32)fullLength > pPacket->BufferLength) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("Payload Length Error : header reports payload of: %d (%d) endpoint buffer size: %d \n",
                        pHdr->PayloadLen, fullLength, pPacket->BufferLength));
                status = A_EPROTO;
                break;
            }
            
            if (j > 0) {
                    /* for messages fetched in a bundle the expected lookahead is unknown since we
                     * are only using the lookahead of the first packet as a template of what to
                     * expect for lengths */
                    /* flag that once we get the real HTC header we need to refesh the information */     
                pPacket->PktInfo.AsRx.HTCRxFlags |= HTC_RX_PKT_REFRESH_HDR;
                    /* set it to something invalid */
                pPacket->PktInfo.AsRx.ExpectedHdr = 0xFFFFFFFF;    
            } else {
            
                pPacket->PktInfo.AsRx.ExpectedHdr = LookAheads[i]; /* set expected look ahead */
            }
                /* set the amount of data to fetch */
            pPacket->ActualLength = pHdr->PayloadLen + HTC_HDR_LENGTH;
        }
        
        if (A_FAILED(status)) {
            if (A_NO_RESOURCE == status) {
                    /* this is actually okay */
                status = A_OK;    
            }
            break;    
        }
                
    }
    
    UNLOCK_HTC_RX(target);
    
    if (A_FAILED(status)) {
        while (!HTC_QUEUE_EMPTY(pQueue)) {
            pPacket = HTC_PACKET_DEQUEUE(pQueue);
                /* recycle all allocated packets */
            HTC_RECYCLE_RX_PKT(target,pPacket,&target->EndPoint[pPacket->Endpoint]);
        }        
    }
        
    return status; 
}

static void HTCAsyncRecvScatterCompletion(HIF_SCATTER_REQ *pScatterReq)
{
    int                 i;    
    HTC_PACKET          *pPacket;
    HTC_ENDPOINT        *pEndpoint;
    A_UINT32            lookAheads[HTC_HOST_MAX_MSG_PER_BUNDLE];
    int                 numLookAheads = 0;
    HTC_TARGET          *target = (HTC_TARGET *)pScatterReq->Context;
    A_STATUS            status;
    A_BOOL              partialBundle = FALSE;
    HTC_PACKET_QUEUE    localRecvQueue;
    A_BOOL              procError = FALSE;
           
    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("+HTCAsyncRecvScatterCompletion  TotLen: %d  Entries: %d\n",
        pScatterReq->TotalLength, pScatterReq->ValidScatterEntries));
    
    A_ASSERT(!IS_DEV_IRQ_PROC_SYNC_MODE(&target->Device));
           
    if (A_FAILED(pScatterReq->CompletionStatus)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("** Recv Scatter Request Failed: %d \n",pScatterReq->CompletionStatus));            
    }
    
    if (pScatterReq->CallerFlags & HTC_SCATTER_REQ_FLAGS_PARTIAL_BUNDLE) {
        partialBundle = TRUE;    
    }
    
    DEV_FINISH_SCATTER_OPERATION(pScatterReq);
    
    INIT_HTC_PACKET_QUEUE(&localRecvQueue);
        
    pPacket = (HTC_PACKET *)pScatterReq->ScatterList[0].pCallerContexts[0];
        /* note: all packets in a scatter req are for the same endpoint ! */
    pEndpoint = &target->EndPoint[pPacket->Endpoint];
         
        /* walk through the scatter list and process */
        /* **** NOTE: DO NOT HOLD ANY LOCKS here, HTCProcessRecvHeader can take the TX lock
         * as it processes credit reports */
    for (i = 0; i < pScatterReq->ValidScatterEntries; i++) {
        pPacket = (HTC_PACKET *)pScatterReq->ScatterList[i].pCallerContexts[0];
        A_ASSERT(pPacket != NULL);       
            /* reset count, we are only interested in the look ahead in the last packet when we
             * break out of this loop */
        numLookAheads = 0;
        
        if (A_SUCCESS(pScatterReq->CompletionStatus)) {      
                /* process header for each of the recv packets */            
            status = HTCProcessRecvHeader(target,pPacket,lookAheads,&numLookAheads);
        } else {
            status = A_ERROR;    
        }
        
        if (A_SUCCESS(status)) {    
#ifdef HTC_EP_STAT_PROFILING
            LOCK_HTC_RX(target);              
            HTC_RX_STAT_PROFILE(target,pEndpoint,numLookAheads);
            INC_HTC_EP_STAT(pEndpoint, RxPacketsBundled, 1);
            UNLOCK_HTC_RX(target);
#endif      
            if (i == (pScatterReq->ValidScatterEntries - 1)) {
                    /* last packet's more packets flag is set based on the lookahead */
                SET_MORE_RX_PACKET_INDICATION_FLAG(lookAheads,numLookAheads,pEndpoint,pPacket);
            } else {
                    /* packets in a bundle automatically have this flag set */
                FORCE_MORE_RX_PACKET_INDICATION_FLAG(pPacket);
            }
             
            DUMP_RECV_PKT_INFO(pPacket);            
                /* since we can't hold a lock in this loop, we insert into our local recv queue for
                 * storage until we can transfer them to the recv completion queue */
            HTC_PACKET_ENQUEUE(&localRecvQueue,pPacket);
            
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" Recv packet scatter entry %d failed (out of %d) \n",
                    i, pScatterReq->ValidScatterEntries));
                /* recycle failed recv */
            HTC_RECYCLE_RX_PKT(target, pPacket, pEndpoint);
                /* set flag and continue processing the remaining scatter entries */
            procError = TRUE;
        }   
    
    }
  
        /* free scatter request */
    DEV_FREE_SCATTER_REQ(&target->Device,pScatterReq);
   
    LOCK_HTC_RX(target);   
        /* transfer the packets in the local recv queue to the recv completion queue */
    HTC_PACKET_QUEUE_TRANSFER_TO_TAIL(&pEndpoint->RecvIndicationQueue, &localRecvQueue);  
    
    UNLOCK_HTC_RX(target);
    
    if (!procError) {  
            /* pipeline the next check (asynchronously) for more packets */           
        HTCAsyncRecvCheckMorePackets(target,
                                     lookAheads,
                                     numLookAheads,
                                     partialBundle ? FALSE : TRUE);
    }
    
        /* now drain the indication queue */
    DrainRecvIndicationQueue(target,pEndpoint);
          
    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("-HTCAsyncRecvScatterCompletion \n"));
}

static A_STATUS HTCIssueRecvPacketBundle(HTC_TARGET        *target,
                                         HTC_PACKET_QUEUE  *pRecvPktQueue, 
                                         HTC_PACKET_QUEUE  *pSyncCompletionQueue,
                                         int               *pNumPacketsFetched,
                                         A_BOOL             PartialBundle)
{
    A_STATUS        status = A_OK;
    HIF_SCATTER_REQ *pScatterReq;
    int             i, totalLength;
    int             pktsToScatter;
    HTC_PACKET      *pPacket;
    A_BOOL          asyncMode = (pSyncCompletionQueue == NULL) ? TRUE : FALSE;
    int             scatterSpaceRemaining = DEV_GET_MAX_BUNDLE_RECV_LENGTH(&target->Device);
        
    pktsToScatter = HTC_PACKET_QUEUE_DEPTH(pRecvPktQueue);
    pktsToScatter = min(pktsToScatter, target->MaxMsgPerBundle);
        
    if ((HTC_PACKET_QUEUE_DEPTH(pRecvPktQueue) - pktsToScatter) > 0) {
            /* we were forced to split this bundle receive operation
             * all packets in this partial bundle must have their lookaheads ignored */
        PartialBundle = TRUE;
            /* this would only happen if the target ignored our max bundle limit */
        AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
                         ("HTCIssueRecvPacketBundle : partial bundle detected num:%d , %d \n",
                         HTC_PACKET_QUEUE_DEPTH(pRecvPktQueue), pktsToScatter));       
    }
    
    totalLength = 0;

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("+HTCIssueRecvPacketBundle (Numpackets: %d , actual : %d) \n", 
        HTC_PACKET_QUEUE_DEPTH(pRecvPktQueue), pktsToScatter));
    
    do {
        
        pScatterReq = DEV_ALLOC_SCATTER_REQ(&target->Device); 
        
        if (pScatterReq == NULL) {
                /* no scatter resources left, just let caller handle it the legacy way */
            break;    
        }        
    
        pScatterReq->CallerFlags = 0;
             
        if (PartialBundle) {
                /* mark that this is a partial bundle, this has special ramifications to the
                 * scatter completion routine */
            pScatterReq->CallerFlags |= HTC_SCATTER_REQ_FLAGS_PARTIAL_BUNDLE;
        }
                   
            /* convert HTC packets to scatter list */                   
        for (i = 0; i < pktsToScatter; i++) {
            int paddedLength;
            
            pPacket = HTC_PACKET_DEQUEUE(pRecvPktQueue);
            A_ASSERT(pPacket != NULL);
            
            paddedLength = DEV_CALC_RECV_PADDED_LEN(&target->Device, pPacket->ActualLength);
     
            if ((scatterSpaceRemaining - paddedLength) < 0) {
                    /* exceeds what we can transfer, put the packet back */  
                HTC_PACKET_ENQUEUE_TO_HEAD(pRecvPktQueue,pPacket);
                break;    
            }
                        
            scatterSpaceRemaining -= paddedLength;
                       
            if (PartialBundle || (i < (pktsToScatter - 1))) {
                    /* packet 0..n-1 cannot be checked for look-aheads since we are fetching a bundle
                     * the last packet however can have it's lookahead used */
                pPacket->PktInfo.AsRx.HTCRxFlags |= HTC_RX_PKT_IGNORE_LOOKAHEAD;
            }
            
            /* note: 1 HTC packet per scatter entry */           
                /* setup packet into */   
            pScatterReq->ScatterList[i].pBuffer = pPacket->pBuffer;
            pScatterReq->ScatterList[i].Length = paddedLength;
            
            pPacket->PktInfo.AsRx.HTCRxFlags |= HTC_RX_PKT_PART_OF_BUNDLE;
            
            if (asyncMode) {
                    /* save HTC packet for async completion routine */
                pScatterReq->ScatterList[i].pCallerContexts[0] = pPacket;
            } else {
                    /* queue to caller's sync completion queue, caller will unload this when we return */
                HTC_PACKET_ENQUEUE(pSyncCompletionQueue,pPacket);    
            }             
                   
            A_ASSERT(pScatterReq->ScatterList[i].Length);
            totalLength += pScatterReq->ScatterList[i].Length;
        }            
        
        pScatterReq->TotalLength = totalLength;
        pScatterReq->ValidScatterEntries = i;
        
        if (asyncMode) {
            pScatterReq->CompletionRoutine = HTCAsyncRecvScatterCompletion;
            pScatterReq->Context = target;
        }
        
        status = DevSubmitScatterRequest(&target->Device, pScatterReq, DEV_SCATTER_READ, asyncMode);
        
        if (A_SUCCESS(status)) {
            *pNumPacketsFetched = i;    
        }
        
        if (!asyncMode) {
                /* free scatter request */
            DEV_FREE_SCATTER_REQ(&target->Device, pScatterReq);   
        }
        
    } while (FALSE);
   
    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("-HTCIssueRecvPacketBundle (status:%d) (fetched:%d) \n",
            status,*pNumPacketsFetched));
        
    return status;
}

static INLINE void CheckRecvWaterMark(HTC_ENDPOINT    *pEndpoint)
{  
        /* see if endpoint is using a refill watermark 
         * ** no need to use a lock here, since we are only inspecting...
         * caller may must not hold locks when calling this function */
    if (pEndpoint->EpCallBacks.RecvRefillWaterMark > 0) {
        if (HTC_PACKET_QUEUE_DEPTH(&pEndpoint->RxBuffers) < pEndpoint->EpCallBacks.RecvRefillWaterMark) {
                /* call the re-fill handler before we continue */
            pEndpoint->EpCallBacks.EpRecvRefill(pEndpoint->EpCallBacks.pContext,
                                                pEndpoint->Id);
        }
    }  
}

/* callback when device layer or lookahead report parsing detects a pending message */
A_STATUS HTCRecvMessagePendingHandler(void *Context, A_UINT32 MsgLookAheads[], int NumLookAheads, A_BOOL *pAsyncProc, int *pNumPktsFetched)
{
    HTC_TARGET      *target = (HTC_TARGET *)Context;
    A_STATUS         status = A_OK;
    HTC_PACKET      *pPacket;
    HTC_ENDPOINT    *pEndpoint;
    A_BOOL          asyncProc = FALSE;
    A_UINT32        lookAheads[HTC_HOST_MAX_MSG_PER_BUNDLE];
    int             pktsFetched;
    HTC_PACKET_QUEUE recvPktQueue, syncCompletedPktsQueue;
    A_BOOL          partialBundle;
    HTC_ENDPOINT_ID id;
    int             totalFetched = 0;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("+HTCRecvMessagePendingHandler NumLookAheads: %d \n",NumLookAheads));
    
    if (pNumPktsFetched != NULL) {
        *pNumPktsFetched = 0;    
    }
    
    if (IS_DEV_IRQ_PROCESSING_ASYNC_ALLOWED(&target->Device)) {
            /* We use async mode to get the packets if the device layer supports it.
             * The device layer interfaces with HIF in which HIF may have restrictions on
             * how interrupts are processed */
        asyncProc = TRUE;
    }

    if (pAsyncProc != NULL) {
            /* indicate to caller how we decided to process this */
        *pAsyncProc = asyncProc;
    }
    
    if (NumLookAheads > HTC_HOST_MAX_MSG_PER_BUNDLE) {
        A_ASSERT(FALSE);
        return A_EPROTO; 
    }
        
        /* on first entry copy the lookaheads into our temp array for processing */
    A_MEMCPY(lookAheads, MsgLookAheads, (sizeof(A_UINT32)) * NumLookAheads);  
            
    while (TRUE) {
        
            /* reset packets queues */
        INIT_HTC_PACKET_QUEUE(&recvPktQueue);
        INIT_HTC_PACKET_QUEUE(&syncCompletedPktsQueue);
        
        if (NumLookAheads > HTC_HOST_MAX_MSG_PER_BUNDLE) {
            status = A_EPROTO;
            A_ASSERT(FALSE);
            break;    
        }
   
            /* first lookahead sets the expected endpoint IDs for all packets in a bundle */
        id = ((HTC_FRAME_HDR *)&lookAheads[0])->EndpointID;
        pEndpoint = &target->EndPoint[id];
        
        if (id >= ENDPOINT_MAX) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("MsgPend, Invalid Endpoint in look-ahead: %d \n",id));
            status = A_EPROTO;
            break;
        }
        
            /* try to allocate as many HTC RX packets indicated by the lookaheads
             * these packets are stored in the recvPkt queue */
        status = AllocAndPrepareRxPackets(target, 
                                          lookAheads, 
                                          NumLookAheads,
                                          pEndpoint, 
                                          &recvPktQueue);        
        if (A_FAILED(status)) {
            break;    
        }
 
        if (HTC_PACKET_QUEUE_DEPTH(&recvPktQueue) >= 2) {
                /* a recv bundle was detected, force IRQ status re-check again */
            REF_IRQ_STATUS_RECHECK(&target->Device);
        }
        
        totalFetched += HTC_PACKET_QUEUE_DEPTH(&recvPktQueue);
               
            /* we've got packet buffers for all we can currently fetch, 
             * this count is not valid anymore  */
        NumLookAheads = 0;
        partialBundle = FALSE;
       
            /* now go fetch the list of HTC packets */
        while (!HTC_QUEUE_EMPTY(&recvPktQueue)) {   
            
            pktsFetched = 0;
                       
            if (target->RecvBundlingEnabled && (HTC_PACKET_QUEUE_DEPTH(&recvPktQueue) > 1)) {             
                    /* there are enough packets to attempt a bundle transfer and recv bundling is allowed  */
                status = HTCIssueRecvPacketBundle(target,
                                                  &recvPktQueue,
                                                  asyncProc ? NULL : &syncCompletedPktsQueue,
                                                  &pktsFetched,
                                                  partialBundle);                                                   
                if (A_FAILED(status)) {
                    break;
                }
                
                if (HTC_PACKET_QUEUE_DEPTH(&recvPktQueue) != 0) {
                        /* we couldn't fetch all packets at one time, this creates a broken
                         * bundle  */
                    partialBundle = TRUE;    
                }                                                                     
            }
            
                /* see if the previous operation fetched any packets using bundling */
            if (0 == pktsFetched) {  
                    /* dequeue one packet */
                pPacket = HTC_PACKET_DEQUEUE(&recvPktQueue);
                A_ASSERT(pPacket != NULL);                 
                                     
                if (asyncProc) {
                        /* we use async mode to get the packet if the device layer supports it
                         * set our callback and context */
                    pPacket->Completion = HTCRecvCompleteHandler;
                    pPacket->pContext = target;
                } else {
                        /* fully synchronous */
                    pPacket->Completion = NULL;
                }
                
                if (HTC_PACKET_QUEUE_DEPTH(&recvPktQueue) > 0) {
                        /* lookaheads in all packets except the last one in the bundle must be ignored */
                    pPacket->PktInfo.AsRx.HTCRxFlags |= HTC_RX_PKT_IGNORE_LOOKAHEAD;
                }
                                    
                    /* go fetch the packet */
                status = HTCIssueRecv(target, pPacket);              
                if (A_FAILED(status)) {
                    break;
                }  
                               
                if (!asyncProc) {               
                        /* sent synchronously, queue this packet for synchronous completion */
                    HTC_PACKET_ENQUEUE(&syncCompletedPktsQueue,pPacket);
                } 
                               
            }
            
        }

        if (A_SUCCESS(status)) {  
            CheckRecvWaterMark(pEndpoint);
        }
            
        if (asyncProc) {
                /* we did this asynchronously so we can get out of the loop, the asynch processing
                 * creates a chain of requests to continue processing pending messages in the
                 * context of callbacks  */
            break;
        }

            /* synchronous handling */
        if (target->Device.DSRCanYield) {
                /* for the SYNC case, increment count that tracks when the DSR should yield */
            target->Device.CurrentDSRRecvCount++;    
        }
            
            /* in the sync case, all packet buffers are now filled, 
             * we can process each packet, check lookaheads and then repeat */ 
             
             /* unload sync completion queue */      
        while (!HTC_QUEUE_EMPTY(&syncCompletedPktsQueue)) {
            HTC_PACKET_QUEUE    container;
           
            pPacket = HTC_PACKET_DEQUEUE(&syncCompletedPktsQueue);
            A_ASSERT(pPacket != NULL);
            
            pEndpoint = &target->EndPoint[pPacket->Endpoint];           
                /* reset count on each iteration, we are only interested in the last packet's lookahead
                 * information when we break out of this loop */
            NumLookAheads = 0;
                /* process header for each of the recv packets
                 * note: the lookahead of the last packet is useful for us to continue in this loop */            
            status = HTCProcessRecvHeader(target,pPacket,lookAheads,&NumLookAheads);
            if (A_FAILED(status)) {
                break;
            }
            
            if (HTC_QUEUE_EMPTY(&syncCompletedPktsQueue)) {
                    /* last packet's more packets flag is set based on the lookahead */
                SET_MORE_RX_PACKET_INDICATION_FLAG(lookAheads,NumLookAheads,pEndpoint,pPacket);
            } else {
                    /* packets in a bundle automatically have this flag set */
                FORCE_MORE_RX_PACKET_INDICATION_FLAG(pPacket);
            }
                /* good packet, indicate it */
            HTC_RX_STAT_PROFILE(target,pEndpoint,NumLookAheads);
            
            if (pPacket->PktInfo.AsRx.HTCRxFlags & HTC_RX_PKT_PART_OF_BUNDLE) {
                INC_HTC_EP_STAT(pEndpoint, RxPacketsBundled, 1);
            }
            
            INIT_HTC_PACKET_QUEUE_AND_ADD(&container,pPacket);
            DO_RCV_COMPLETION(pEndpoint,&container);
        }

        if (A_FAILED(status)) {
            break;
        }
            
        if (NumLookAheads == 0) {
                /* no more look aheads */
            break;    
        }

            /* when we process recv synchronously we need to check if we should yield and stop
             * fetching more packets indicated by the embedded lookaheads */
        if (target->Device.DSRCanYield) {
            if (DEV_CHECK_RECV_YIELD(&target->Device)) {
                    /* break out, don't fetch any more packets */
                break;  
            }  
        }
            

        /* check whether other OS contexts have queued any WMI command/data for WLAN. 
         * This check is needed only if WLAN Tx and Rx happens in same thread context */
        A_CHECK_DRV_TX();
        
            /* for SYNCH processing, if we get here, we are running through the loop again due to a detected lookahead.
             * Set flag that we should re-check IRQ status registers again before leaving IRQ processing,
             * this can net better performance in high throughput situations */
        REF_IRQ_STATUS_RECHECK(&target->Device);
    }
    
    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("Failed to get pending recv messages (%d) \n",status));
            /* cleanup any packets we allocated but didn't use to actually fetch any packets */                        
        while (!HTC_QUEUE_EMPTY(&recvPktQueue)) {   
            pPacket = HTC_PACKET_DEQUEUE(&recvPktQueue);
                /* clean up packets */
            HTC_RECYCLE_RX_PKT(target, pPacket, &target->EndPoint[pPacket->Endpoint]);
        }
            /* cleanup any packets in sync completion queue */
        while (!HTC_QUEUE_EMPTY(&syncCompletedPktsQueue)) {   
            pPacket = HTC_PACKET_DEQUEUE(&syncCompletedPktsQueue);
                /* clean up packets */
            HTC_RECYCLE_RX_PKT(target, pPacket, &target->EndPoint[pPacket->Endpoint]);
        }
        if  (HTC_STOPPING(target)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
                (" Host is going to stop. blocking receiver for HTCStop.. \n"));
            DevStopRecv(&target->Device, asyncProc ? DEV_STOP_RECV_ASYNC : DEV_STOP_RECV_SYNC);
        }
    }
        /* before leaving, check to see if host ran out of buffers and needs to stop the
         * receiver */
    if (target->RecvStateFlags & HTC_RECV_WAIT_BUFFERS) {
        AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
                (" Host has no RX buffers, blocking receiver to prevent overrun.. \n"));
            /* try to stop receive at the device layer */
        DevStopRecv(&target->Device, asyncProc ? DEV_STOP_RECV_ASYNC : DEV_STOP_RECV_SYNC);
    }
    
    if (pNumPktsFetched != NULL) {
        *pNumPktsFetched = totalFetched;    
    }
    
    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("-HTCRecvMessagePendingHandler \n"));

    return status;
}

A_STATUS HTCAddReceivePktMultiple(HTC_HANDLE HTCHandle, HTC_PACKET_QUEUE *pPktQueue)
{
    HTC_TARGET      *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    HTC_ENDPOINT    *pEndpoint;
    A_BOOL          unblockRecv = FALSE;
    A_STATUS        status = A_OK;
    HTC_PACKET      *pFirstPacket;

    pFirstPacket = HTC_GET_PKT_AT_HEAD(pPktQueue);
    
    if (NULL == pFirstPacket) {
        A_ASSERT(FALSE);
        return A_EINVAL;    
    }
    
    AR_DEBUG_ASSERT(pFirstPacket->Endpoint < ENDPOINT_MAX);
    
    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,
                    ("+- HTCAddReceivePktMultiple : endPointId: %d, cnt:%d, length: %d\n",
                    pFirstPacket->Endpoint,
                    HTC_PACKET_QUEUE_DEPTH(pPktQueue), 
                    pFirstPacket->BufferLength));

    do {

        pEndpoint = &target->EndPoint[pFirstPacket->Endpoint];

        LOCK_HTC_RX(target);

        if (HTC_STOPPING(target)) {
            HTC_PACKET *pPacket;
            
            UNLOCK_HTC_RX(target);
            
                /* walk through queue and mark each one canceled */
            HTC_PACKET_QUEUE_ITERATE_ALLOW_REMOVE(pPktQueue,pPacket) {
                pPacket->Status = A_ECANCELED;    
            } HTC_PACKET_QUEUE_ITERATE_END;
            
            DO_RCV_COMPLETION(pEndpoint,pPktQueue);
            break;
        }

            /* store receive packets */
        HTC_PACKET_QUEUE_TRANSFER_TO_TAIL(&pEndpoint->RxBuffers, pPktQueue);

            /* check if we are blocked waiting for a new buffer */
        if (target->RecvStateFlags & HTC_RECV_WAIT_BUFFERS) {
            if (target->EpWaitingForBuffers == pFirstPacket->Endpoint) {
                AR_DEBUG_PRINTF(ATH_DEBUG_RECV,(" receiver was blocked on ep:%d, unblocking.. \n",
                    target->EpWaitingForBuffers));
                target->RecvStateFlags &= ~HTC_RECV_WAIT_BUFFERS;
                target->EpWaitingForBuffers = ENDPOINT_MAX;
                unblockRecv = TRUE;
            }
        }

        UNLOCK_HTC_RX(target);

        if (unblockRecv && !HTC_STOPPING(target)) {
                /* TODO : implement a buffer threshold count? */
            DevEnableRecv(&target->Device,DEV_ENABLE_RECV_SYNC);
        }

    } while (FALSE);

    return status;
}

/* Makes a buffer available to the HTC module */
A_STATUS HTCAddReceivePkt(HTC_HANDLE HTCHandle, HTC_PACKET *pPacket)
{
    HTC_PACKET_QUEUE queue;
    INIT_HTC_PACKET_QUEUE_AND_ADD(&queue,pPacket); 
    return HTCAddReceivePktMultiple(HTCHandle, &queue);       
}

void HTCUnblockRecv(HTC_HANDLE HTCHandle)
{
    HTC_TARGET *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    A_BOOL      unblockRecv = FALSE;

    LOCK_HTC_RX(target);

        /* check if we are blocked waiting for a new buffer */
    if (target->RecvStateFlags & HTC_RECV_WAIT_BUFFERS) {
        AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("HTCUnblockRx : receiver was blocked on ep:%d, unblocking.. \n",
            target->EpWaitingForBuffers));
        target->RecvStateFlags &= ~HTC_RECV_WAIT_BUFFERS;
        target->EpWaitingForBuffers = ENDPOINT_MAX;
        unblockRecv = TRUE;
    }

    UNLOCK_HTC_RX(target);

    if (unblockRecv && !HTC_STOPPING(target)) {
            /* re-enable */
        DevEnableRecv(&target->Device,DEV_ENABLE_RECV_ASYNC);
    }
}

static void HTCFlushRxQueue(HTC_TARGET *target, HTC_ENDPOINT *pEndpoint, HTC_PACKET_QUEUE *pQueue)
{
    HTC_PACKET  *pPacket;
    HTC_PACKET_QUEUE container;
    
    LOCK_HTC_RX(target);

    while (1) {
        pPacket = HTC_PACKET_DEQUEUE(pQueue);
        if (NULL == pPacket) {
            break;
        }
        UNLOCK_HTC_RX(target);
        pPacket->Status = A_ECANCELED;
        pPacket->ActualLength = 0;
        AR_DEBUG_PRINTF(ATH_DEBUG_RECV, ("  Flushing RX packet:0x%lX, length:%d, ep:%d \n",
                (unsigned long)pPacket, pPacket->BufferLength, pPacket->Endpoint));
        INIT_HTC_PACKET_QUEUE_AND_ADD(&container,pPacket);
            /* give the packet back */
        DO_RCV_COMPLETION(pEndpoint,&container);
        LOCK_HTC_RX(target);
    }
    
    UNLOCK_HTC_RX(target);
}

static void HTCFlushEndpointRX(HTC_TARGET *target, HTC_ENDPOINT *pEndpoint)
{
        /* flush any recv indications not already made */
    HTCFlushRxQueue(target,pEndpoint,&pEndpoint->RecvIndicationQueue);
        /* flush any rx buffers */
    HTCFlushRxQueue(target,pEndpoint,&pEndpoint->RxBuffers);
}

void HTCFlushRecvBuffers(HTC_TARGET *target)
{
    HTC_ENDPOINT    *pEndpoint;
    int             i;

    for (i = ENDPOINT_0; i < ENDPOINT_MAX; i++) {
        pEndpoint = &target->EndPoint[i];
        if (pEndpoint->ServiceID == 0) {
                /* not in use.. */
            continue;
        }
        HTCFlushEndpointRX(target,pEndpoint);
    }
}


void HTCEnableRecv(HTC_HANDLE HTCHandle)
{
    HTC_TARGET *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);

    if (!HTC_STOPPING(target)) {
            /* re-enable */
        DevEnableRecv(&target->Device,DEV_ENABLE_RECV_SYNC);
    }
}

void HTCDisableRecv(HTC_HANDLE HTCHandle)
{
    HTC_TARGET *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);

    if (!HTC_STOPPING(target)) {
            /* disable */
        DevStopRecv(&target->Device,DEV_ENABLE_RECV_SYNC);
    }
}

int HTCGetNumRecvBuffers(HTC_HANDLE      HTCHandle,
                         HTC_ENDPOINT_ID Endpoint)
{
    HTC_TARGET *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);    
    return HTC_PACKET_QUEUE_DEPTH(&(target->EndPoint[Endpoint].RxBuffers));
}

A_STATUS HTCWaitForPendingRecv(HTC_HANDLE   HTCHandle,
                               A_UINT32     TimeoutInMs,
                               A_BOOL      *pbIsRecvPending)
{
    A_STATUS    status  = A_OK;
    HTC_TARGET *target  = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);

    status = DevWaitForPendingRecv(&target->Device,
                                    TimeoutInMs,
                                    pbIsRecvPending);

    return status;
}
