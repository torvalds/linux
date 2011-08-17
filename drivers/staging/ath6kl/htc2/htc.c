//------------------------------------------------------------------------------
// <copyright file="htc.c" company="Atheros">
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

#ifdef ATH_DEBUG_MODULE
static struct ath_debug_mask_description g_HTCDebugDescription[] = {
    { ATH_DEBUG_SEND , "Send"},
    { ATH_DEBUG_RECV , "Recv"},
    { ATH_DEBUG_SYNC , "Sync"},
    { ATH_DEBUG_DUMP , "Dump Data (RX or TX)"},
    { ATH_DEBUG_IRQ  , "Interrupt Processing"}
};

ATH_DEBUG_INSTANTIATE_MODULE_VAR(htc,
                                 "htc",
                                 "Host Target Communications",
                                 ATH_DEBUG_MASK_DEFAULTS,
                                 ATH_DEBUG_DESCRIPTION_COUNT(g_HTCDebugDescription),
                                 g_HTCDebugDescription);
                                 
#endif

static void HTCReportFailure(void *Context);
static void ResetEndpointStates(struct htc_target *target);

void HTCFreeControlBuffer(struct htc_target *target, struct htc_packet *pPacket, struct htc_packet_queue *pList)
{
    LOCK_HTC(target);
    HTC_PACKET_ENQUEUE(pList,pPacket);
    UNLOCK_HTC(target);
}

struct htc_packet *HTCAllocControlBuffer(struct htc_target *target,  struct htc_packet_queue *pList)
{
    struct htc_packet *pPacket;

    LOCK_HTC(target);
    pPacket = HTC_PACKET_DEQUEUE(pList);
    UNLOCK_HTC(target);

    return pPacket;
}

/* cleanup the HTC instance */
static void HTCCleanup(struct htc_target *target)
{
    s32 i;

    DevCleanup(&target->Device);
    
    for (i = 0;i < NUM_CONTROL_BUFFERS;i++) {
        if (target->HTCControlBuffers[i].Buffer) {
            kfree(target->HTCControlBuffers[i].Buffer);
        }
    }
    
    if (A_IS_MUTEX_VALID(&target->HTCLock)) {
        A_MUTEX_DELETE(&target->HTCLock);
    }

    if (A_IS_MUTEX_VALID(&target->HTCRxLock)) {
        A_MUTEX_DELETE(&target->HTCRxLock);
    }

    if (A_IS_MUTEX_VALID(&target->HTCTxLock)) {
        A_MUTEX_DELETE(&target->HTCTxLock);
    }
        /* free our instance */
    kfree(target);
}

/* registered target arrival callback from the HIF layer */
HTC_HANDLE HTCCreate(void *hif_handle, struct htc_init_info *pInfo)
{
    struct htc_target              *target = NULL;
    int                 status = 0;
    int                      i;
    u32 ctrl_bufsz;
    u32 blocksizes[HTC_MAILBOX_NUM_MAX];

    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HTCCreate - Enter\n"));

    A_REGISTER_MODULE_DEBUG_INFO(htc);
    
    do {

            /* allocate target memory */
        if ((target = (struct htc_target *)A_MALLOC(sizeof(struct htc_target))) == NULL) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to allocate memory\n"));
            status = A_ERROR;
            break;
        }

        A_MEMZERO(target, sizeof(struct htc_target));
        A_MUTEX_INIT(&target->HTCLock);
        A_MUTEX_INIT(&target->HTCRxLock);
        A_MUTEX_INIT(&target->HTCTxLock);
        INIT_HTC_PACKET_QUEUE(&target->ControlBufferTXFreeList);
        INIT_HTC_PACKET_QUEUE(&target->ControlBufferRXFreeList);
        
            /* give device layer the hif device handle */
        target->Device.HIFDevice = hif_handle;
            /* give the device layer our context (for event processing)
             * the device layer will register it's own context with HIF
             * so we need to set this so we can fetch it in the target remove handler */
        target->Device.HTCContext = target;
            /* set device layer target failure callback */
        target->Device.TargetFailureCallback = HTCReportFailure;
            /* set device layer recv message pending callback */
        target->Device.MessagePendingCallback = HTCRecvMessagePendingHandler;
        target->EpWaitingForBuffers = ENDPOINT_MAX;

        memcpy(&target->HTCInitInfo,pInfo,sizeof(struct htc_init_info));
        
        ResetEndpointStates(target);
          
            /* setup device layer */
        status = DevSetup(&target->Device);

        if (status) {
            break;
        }


        /* get the block sizes */
        status = HIFConfigureDevice(hif_handle, HIF_DEVICE_GET_MBOX_BLOCK_SIZE,
                                    blocksizes, sizeof(blocksizes));
        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Failed to get block size info from HIF layer...\n"));
            break;
        }

        /* Set the control buffer size based on the block size */
        if (blocksizes[1] > HTC_MAX_CONTROL_MESSAGE_LENGTH) {
            ctrl_bufsz = blocksizes[1] + HTC_HDR_LENGTH;
        } else {
            ctrl_bufsz = HTC_MAX_CONTROL_MESSAGE_LENGTH + HTC_HDR_LENGTH;
        }
        for (i = 0;i < NUM_CONTROL_BUFFERS;i++) {
            target->HTCControlBuffers[i].Buffer = A_MALLOC(ctrl_bufsz);
            if (target->HTCControlBuffers[i].Buffer == NULL) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to allocate memory\n"));
                status = A_ERROR;
                break;
            }
        }

        if (status) {
            break;
        }

            /* carve up buffers/packets for control messages */
        for (i = 0; i < NUM_CONTROL_RX_BUFFERS; i++) {
            struct htc_packet *pControlPacket;
            pControlPacket = &target->HTCControlBuffers[i].HtcPacket;
            SET_HTC_PACKET_INFO_RX_REFILL(pControlPacket,
                                          target,
                                          target->HTCControlBuffers[i].Buffer,
                                          ctrl_bufsz,
                                          ENDPOINT_0);
            HTC_FREE_CONTROL_RX(target,pControlPacket);
        }

        for (;i < NUM_CONTROL_BUFFERS;i++) {
             struct htc_packet *pControlPacket;
             pControlPacket = &target->HTCControlBuffers[i].HtcPacket;
             INIT_HTC_PACKET_INFO(pControlPacket,
                                  target->HTCControlBuffers[i].Buffer,
                                  ctrl_bufsz);
             HTC_FREE_CONTROL_TX(target,pControlPacket);
        }

    } while (false);

    if (status) {
        if (target != NULL) {
            HTCCleanup(target);
            target = NULL;
        }
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HTCCreate - Exit\n"));

    return target;
}

void  HTCDestroy(HTC_HANDLE HTCHandle)
{
    struct htc_target *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("+HTCDestroy ..  Destroying :0x%lX \n",(unsigned long)target));
    HTCCleanup(target);
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("-HTCDestroy \n"));
}

/* get the low level HIF device for the caller , the caller may wish to do low level
 * HIF requests */
void *HTCGetHifDevice(HTC_HANDLE HTCHandle)
{
    struct htc_target *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    return target->Device.HIFDevice;
}

/* wait for the target to arrive (sends HTC Ready message)
 * this operation is fully synchronous and the message is polled for */
int HTCWaitTarget(HTC_HANDLE HTCHandle)
{
    struct htc_target              *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    int                 status;
    struct htc_packet              *pPacket = NULL;
    HTC_READY_EX_MSG        *pRdyMsg;

    struct htc_service_connect_req  connect;
    struct htc_service_connect_resp resp;

    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HTCWaitTarget - Enter (target:0x%lX) \n", (unsigned long)target));

    do {

#ifdef MBOXHW_UNIT_TEST

        status = DoMboxHWTest(&target->Device);

        if (status) {
            break;
        }

#endif

            /* we should be getting 1 control message that the target is ready */
        status = HTCWaitforControlMessage(target, &pPacket);

        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, (" Target Not Available!!\n"));
            break;
        }

            /* we controlled the buffer creation so it has to be properly aligned */
        pRdyMsg = (HTC_READY_EX_MSG *)pPacket->pBuffer;

        if ((pRdyMsg->Version2_0_Info.MessageID != HTC_MSG_READY_ID) ||
            (pPacket->ActualLength < sizeof(HTC_READY_MSG))) {
                /* this message is not valid */
            AR_DEBUG_ASSERT(false);
            status = A_EPROTO;
            break;
        }

        
        if (pRdyMsg->Version2_0_Info.CreditCount == 0 || pRdyMsg->Version2_0_Info.CreditSize == 0) {
              /* this message is not valid */
            AR_DEBUG_ASSERT(false);
            status = A_EPROTO;
            break;
        }

        target->TargetCredits = pRdyMsg->Version2_0_Info.CreditCount;
        target->TargetCreditSize = pRdyMsg->Version2_0_Info.CreditSize;
        
        AR_DEBUG_PRINTF(ATH_DEBUG_WARN, (" Target Ready: credits: %d credit size: %d\n",
                target->TargetCredits, target->TargetCreditSize));
        
            /* check if this is an extended ready message */        
        if (pPacket->ActualLength >= sizeof(HTC_READY_EX_MSG)) {
                /* this is an extended message */    
            target->HTCTargetVersion = pRdyMsg->HTCVersion;   
            target->MaxMsgPerBundle = pRdyMsg->MaxMsgsPerHTCBundle;     
        } else {
                /* legacy */
            target->HTCTargetVersion = HTC_VERSION_2P0;
            target->MaxMsgPerBundle = 0;    
        }
        
#ifdef HTC_FORCE_LEGACY_2P0   
            /* for testing and comparison...*/     
        target->HTCTargetVersion = HTC_VERSION_2P0;
        target->MaxMsgPerBundle = 0;
#endif
           
        AR_DEBUG_PRINTF(ATH_DEBUG_TRC, 
                    ("Using HTC Protocol Version : %s (%d)\n ", 
                    (target->HTCTargetVersion == HTC_VERSION_2P0) ? "2.0" : ">= 2.1",
                    target->HTCTargetVersion));
                    
        if (target->MaxMsgPerBundle > 0) {
                /* limit what HTC can handle */
            target->MaxMsgPerBundle = min(HTC_HOST_MAX_MSG_PER_BUNDLE, target->MaxMsgPerBundle);          
                /* target supports message bundling, setup device layer */
            if (DevSetupMsgBundling(&target->Device,target->MaxMsgPerBundle)) {
                    /* device layer can't handle bundling */
                target->MaxMsgPerBundle = 0;        
            } else {
                    /* limit bundle what the device layer can handle */
                target->MaxMsgPerBundle = min(DEV_GET_MAX_MSG_PER_BUNDLE(&target->Device),
                                              target->MaxMsgPerBundle);     
            }
        }
        
        if (target->MaxMsgPerBundle > 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_TRC, 
                    (" HTC bundling allowed. Max Msg Per HTC Bundle: %d\n", target->MaxMsgPerBundle));    
           
            if (DEV_GET_MAX_BUNDLE_SEND_LENGTH(&target->Device) != 0) {           
                target->SendBundlingEnabled = true;
            }            
            if (DEV_GET_MAX_BUNDLE_RECV_LENGTH(&target->Device) != 0) {    
                target->RecvBundlingEnabled = true;
            }
                            
            if (!DEV_IS_LEN_BLOCK_ALIGNED(&target->Device,target->TargetCreditSize)) {
                AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("*** Credit size: %d is not block aligned! Disabling send bundling \n",
                        target->TargetCreditSize));    
                    /* disallow send bundling since the credit size is not aligned to a block size
                     * the I/O block padding will spill into the next credit buffer which is fatal */
                target->SendBundlingEnabled = false;
            }
        }
           
            /* setup our pseudo HTC control endpoint connection */
        A_MEMZERO(&connect,sizeof(connect));
        A_MEMZERO(&resp,sizeof(resp));
        connect.EpCallbacks.pContext = target;
        connect.EpCallbacks.EpTxComplete = HTCControlTxComplete;
        connect.EpCallbacks.EpRecv = HTCControlRecv;
        connect.EpCallbacks.EpRecvRefill = NULL;  /* not needed */
        connect.EpCallbacks.EpSendFull = NULL;    /* not nedded */
        connect.MaxSendQueueDepth = NUM_CONTROL_BUFFERS;
        connect.ServiceID = HTC_CTRL_RSVD_SVC;

            /* connect fake service */
        status = HTCConnectService((HTC_HANDLE)target,
                                   &connect,
                                   &resp);

        if (!status) {
            break;
        }

    } while (false);

    if (pPacket != NULL) {
        HTC_FREE_CONTROL_RX(target,pPacket);
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HTCWaitTarget - Exit\n"));

    return status;
}



/* Start HTC, enable interrupts and let the target know host has finished setup */
int HTCStart(HTC_HANDLE HTCHandle)
{
    struct htc_target *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    struct htc_packet *pPacket;
    int   status;

    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HTCStart Enter\n"));

        /* make sure interrupts are disabled at the chip level,
         * this function can be called again from a reboot of the target without shutting down HTC */
    DevDisableInterrupts(&target->Device);
        /* make sure state is cleared again */
    target->OpStateFlags = 0;
    target->RecvStateFlags = 0;
      
        /* now that we are starting, push control receive buffers into the
         * HTC control endpoint */

    while (1) {
        pPacket = HTC_ALLOC_CONTROL_RX(target);
        if (NULL == pPacket) {
            break;
        }
        HTCAddReceivePkt((HTC_HANDLE)target,pPacket);
    }

    do {

        AR_DEBUG_ASSERT(target->InitCredits != NULL);
        AR_DEBUG_ASSERT(target->EpCreditDistributionListHead != NULL);
        AR_DEBUG_ASSERT(target->EpCreditDistributionListHead->pNext != NULL);

            /* call init credits callback to do the distribution ,
             * NOTE: the first entry in the distribution list is ENDPOINT_0, so
             * we pass the start of the list after this one. */
        target->InitCredits(target->pCredDistContext,
                            target->EpCreditDistributionListHead->pNext,
                            target->TargetCredits);

#ifdef ATH_DEBUG_MODULE

        if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_TRC)) {
            DumpCreditDistStates(target);
        }
#endif

            /* the caller is done connecting to services, so we can indicate to the
            * target that the setup phase is complete */
        status = HTCSendSetupComplete(target);

        if (status) {
            break;
        }

            /* unmask interrupts */
        status = DevUnmaskInterrupts(&target->Device);

        if (status) {
            HTCStop(target);
        }

    } while (false);

    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HTCStart Exit\n"));
    return status;
}

static void ResetEndpointStates(struct htc_target *target)
{
    struct htc_endpoint        *pEndpoint;
    int                  i;

    for (i = ENDPOINT_0; i < ENDPOINT_MAX; i++) {
        pEndpoint = &target->EndPoint[i];
        
        A_MEMZERO(&pEndpoint->CreditDist, sizeof(pEndpoint->CreditDist));
        pEndpoint->ServiceID = 0;
        pEndpoint->MaxMsgLength = 0;
        pEndpoint->MaxTxQueueDepth = 0;
        A_MEMZERO(&pEndpoint->EndPointStats,sizeof(pEndpoint->EndPointStats));
        INIT_HTC_PACKET_QUEUE(&pEndpoint->RxBuffers);
        INIT_HTC_PACKET_QUEUE(&pEndpoint->TxQueue);
        INIT_HTC_PACKET_QUEUE(&pEndpoint->RecvIndicationQueue);
        pEndpoint->target = target;
    }
        /* reset distribution list */
    target->EpCreditDistributionListHead = NULL;
}

/* stop HTC communications, i.e. stop interrupt reception, and flush all queued buffers */
void HTCStop(HTC_HANDLE HTCHandle)
{
    struct htc_target *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("+HTCStop \n"));

    LOCK_HTC(target);
        /* mark that we are shutting down .. */
    target->OpStateFlags |= HTC_OP_STATE_STOPPING;
    UNLOCK_HTC(target);
    
        /* Masking interrupts is a synchronous operation, when this function returns
         * all pending HIF I/O has completed, we can safely flush the queues */
    DevMaskInterrupts(&target->Device);

#ifdef THREAD_X
	//
	//	Is this delay required
	//
    A_MDELAY(200); // wait for IRQ process done
#endif
        /* flush all send packets */
    HTCFlushSendPkts(target);
        /* flush all recv buffers */
    HTCFlushRecvBuffers(target);

    DevCleanupMsgBundling(&target->Device);

    ResetEndpointStates(target);
   
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("-HTCStop \n"));
}

#ifdef ATH_DEBUG_MODULE
void HTCDumpCreditStates(HTC_HANDLE HTCHandle)
{
    struct htc_target *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);

    LOCK_HTC_TX(target);

    DumpCreditDistStates(target);

    UNLOCK_HTC_TX(target);
    
    DumpAR6KDevState(&target->Device);
}
#endif
/* report a target failure from the device, this is a callback from the device layer
 * which uses a mechanism to report errors from the target (i.e. special interrupts) */
static void HTCReportFailure(void *Context)
{
    struct htc_target *target = (struct htc_target *)Context;

    target->TargetFailure = true;

    if (target->HTCInitInfo.TargetFailure != NULL) {
            /* let upper layer know, it needs to call HTCStop() */
        target->HTCInitInfo.TargetFailure(target->HTCInitInfo.pContext, A_ERROR);
    }
}

bool HTCGetEndpointStatistics(HTC_HANDLE               HTCHandle,
                                HTC_ENDPOINT_ID          Endpoint,
                                HTC_ENDPOINT_STAT_ACTION Action,
                                struct htc_endpoint_stats       *pStats)
{

    struct htc_target *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    bool     clearStats = false;
    bool     sample = false;

    switch (Action) {
        case HTC_EP_STAT_SAMPLE :
            sample = true;
            break;
        case HTC_EP_STAT_SAMPLE_AND_CLEAR :
            sample = true;
            clearStats = true;
            break;
        case HTC_EP_STAT_CLEAR :
            clearStats = true;
            break;
        default:
            break;
    }

    A_ASSERT(Endpoint < ENDPOINT_MAX);

        /* lock out TX and RX while we sample and/or clear */
    LOCK_HTC_TX(target);
    LOCK_HTC_RX(target);

    if (sample) {
        A_ASSERT(pStats != NULL);
            /* return the stats to the caller */
        memcpy(pStats, &target->EndPoint[Endpoint].EndPointStats, sizeof(struct htc_endpoint_stats));
    }

    if (clearStats) {
            /* reset stats */
        A_MEMZERO(&target->EndPoint[Endpoint].EndPointStats, sizeof(struct htc_endpoint_stats));
    }

    UNLOCK_HTC_RX(target);
    UNLOCK_HTC_TX(target);

    return true;
}

struct ar6k_device  *HTCGetAR6KDevice(void *HTCHandle)
{
    struct htc_target *target = GET_HTC_TARGET_FROM_HANDLE(HTCHandle);
    return &target->Device;
}

