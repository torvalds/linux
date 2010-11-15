//------------------------------------------------------------------------------
// <copyright file="htc_internal.h" company="Atheros">
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
#ifndef _HTC_INTERNAL_H_
#define _HTC_INTERNAL_H_

/* for debugging, uncomment this to capture the last frame header, on frame header
 * processing errors, the last frame header is dump for comparison */
//#define HTC_CAPTURE_LAST_FRAME

//#define HTC_EP_STAT_PROFILING

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Header files */

#include "a_config.h"
#include "athdefs.h"
#include "a_types.h"
#include "a_osapi.h"
#include "htc_debug.h"
#include "htc.h"
#include "htc_api.h"
#include "bmi_msg.h"
#include "hif.h"
#include "AR6000/ar6k.h"

/* HTC operational parameters */
#define HTC_TARGET_RESPONSE_TIMEOUT        2000 /* in ms */
#define HTC_TARGET_DEBUG_INTR_MASK         0x01
#define HTC_TARGET_CREDIT_INTR_MASK        0xF0

#define HTC_HOST_MAX_MSG_PER_BUNDLE        8
#define HTC_MIN_HTC_MSGS_TO_BUNDLE         2

/* packet flags */

#define HTC_RX_PKT_IGNORE_LOOKAHEAD      (1 << 0)
#define HTC_RX_PKT_REFRESH_HDR           (1 << 1)
#define HTC_RX_PKT_PART_OF_BUNDLE        (1 << 2)
#define HTC_RX_PKT_NO_RECYCLE            (1 << 3)

/* scatter request flags */

#define HTC_SCATTER_REQ_FLAGS_PARTIAL_BUNDLE  (1 << 0)

typedef struct _HTC_ENDPOINT {
    HTC_ENDPOINT_ID             Id;
    HTC_SERVICE_ID              ServiceID;      /* service ID this endpoint is bound to
                                                   non-zero value means this endpoint is in use */
    HTC_PACKET_QUEUE            TxQueue;        /* HTC frame buffer TX queue */
    HTC_PACKET_QUEUE            RxBuffers;      /* HTC frame buffer RX list */
    HTC_ENDPOINT_CREDIT_DIST    CreditDist;     /* credit distribution structure (exposed to driver layer) */
    HTC_EP_CALLBACKS            EpCallBacks;    /* callbacks associated with this endpoint */
    int                         MaxTxQueueDepth;   /* max depth of the TX queue before we need to
                                                      call driver's full handler */
    int                         MaxMsgLength;        /* max length of endpoint message */
    int                         TxProcessCount;  /* reference count to continue tx processing */
    HTC_PACKET_QUEUE            RecvIndicationQueue;    /* recv packets ready to be indicated */
    int                         RxProcessCount;         /* reference count to allow single processing context */
    struct  _HTC_TARGET         *target;                /* back pointer to target */
    A_UINT8                     SeqNo;                  /* TX seq no (helpful) for debugging */
    A_UINT32                    LocalConnectionFlags;   /* local connection flags */
#ifdef HTC_EP_STAT_PROFILING
    HTC_ENDPOINT_STATS          EndPointStats;          /* endpoint statistics */
#endif
} HTC_ENDPOINT;

#ifdef HTC_EP_STAT_PROFILING
#define INC_HTC_EP_STAT(p,stat,count) (p)->EndPointStats.stat += (count);
#else
#define INC_HTC_EP_STAT(p,stat,count)
#endif

#define HTC_SERVICE_TX_PACKET_TAG  HTC_TX_PACKET_TAG_INTERNAL

#define NUM_CONTROL_BUFFERS     8
#define NUM_CONTROL_TX_BUFFERS  2
#define NUM_CONTROL_RX_BUFFERS  (NUM_CONTROL_BUFFERS - NUM_CONTROL_TX_BUFFERS)

typedef struct HTC_CONTROL_BUFFER {
    HTC_PACKET    HtcPacket;
    A_UINT8       *Buffer;
} HTC_CONTROL_BUFFER;

#define HTC_RECV_WAIT_BUFFERS        (1 << 0)
#define HTC_OP_STATE_STOPPING        (1 << 0)

/* our HTC target state */
typedef struct _HTC_TARGET {
    HTC_ENDPOINT                EndPoint[ENDPOINT_MAX];
    HTC_CONTROL_BUFFER          HTCControlBuffers[NUM_CONTROL_BUFFERS];
    HTC_ENDPOINT_CREDIT_DIST   *EpCreditDistributionListHead;
    HTC_PACKET_QUEUE            ControlBufferTXFreeList;
    HTC_PACKET_QUEUE            ControlBufferRXFreeList;
    HTC_CREDIT_DIST_CALLBACK    DistributeCredits;
    HTC_CREDIT_INIT_CALLBACK    InitCredits;
    void                       *pCredDistContext;
    int                         TargetCredits;
    unsigned int                TargetCreditSize;
    A_MUTEX_T                   HTCLock;
    A_MUTEX_T                   HTCRxLock;
    A_MUTEX_T                   HTCTxLock;
    AR6K_DEVICE                 Device;         /* AR6K - specific state */
    A_UINT32                    OpStateFlags;
    A_UINT32                    RecvStateFlags;
    HTC_ENDPOINT_ID             EpWaitingForBuffers;
    A_BOOL                      TargetFailure;
#ifdef HTC_CAPTURE_LAST_FRAME
    HTC_FRAME_HDR               LastFrameHdr;  /* useful for debugging */
    A_UINT8                     LastTrailer[256];
    A_UINT8                     LastTrailerLength;
#endif
    HTC_INIT_INFO               HTCInitInfo;
    A_UINT8                     HTCTargetVersion;
    int                         MaxMsgPerBundle;       /* max messages per bundle for HTC */
    A_BOOL                      SendBundlingEnabled;   /* run time enable for send bundling (dynamic) */
    int                         RecvBundlingEnabled;   /* run time enable for recv bundling (dynamic) */
} HTC_TARGET;

#define HTC_STOPPING(t) ((t)->OpStateFlags & HTC_OP_STATE_STOPPING)
#define LOCK_HTC(t)      A_MUTEX_LOCK(&(t)->HTCLock);
#define UNLOCK_HTC(t)    A_MUTEX_UNLOCK(&(t)->HTCLock);
#define LOCK_HTC_RX(t)   A_MUTEX_LOCK(&(t)->HTCRxLock);
#define UNLOCK_HTC_RX(t) A_MUTEX_UNLOCK(&(t)->HTCRxLock);
#define LOCK_HTC_TX(t)   A_MUTEX_LOCK(&(t)->HTCTxLock);
#define UNLOCK_HTC_TX(t) A_MUTEX_UNLOCK(&(t)->HTCTxLock);

#define GET_HTC_TARGET_FROM_HANDLE(hnd) ((HTC_TARGET *)(hnd))
#define HTC_RECYCLE_RX_PKT(target,p,e)                           \
{                                                                \
    if ((p)->PktInfo.AsRx.HTCRxFlags & HTC_RX_PKT_NO_RECYCLE) {  \
         HTC_PACKET_RESET_RX(pPacket);                           \
         pPacket->Status = A_ECANCELED;                          \
         (e)->EpCallBacks.EpRecv((e)->EpCallBacks.pContext,      \
                                 (p));                           \
    } else {                                                     \
        HTC_PACKET_RESET_RX(pPacket);                            \
        HTCAddReceivePkt((HTC_HANDLE)(target),(p));              \
    }                                                            \
}

/* internal HTC functions */
void        HTCControlTxComplete(void *Context, HTC_PACKET *pPacket);
void        HTCControlRecv(void *Context, HTC_PACKET *pPacket);
A_STATUS    HTCWaitforControlMessage(HTC_TARGET *target, HTC_PACKET **ppControlPacket);
HTC_PACKET *HTCAllocControlBuffer(HTC_TARGET *target, HTC_PACKET_QUEUE *pList);
void        HTCFreeControlBuffer(HTC_TARGET *target, HTC_PACKET *pPacket, HTC_PACKET_QUEUE *pList);
A_STATUS    HTCIssueSend(HTC_TARGET *target, HTC_PACKET *pPacket);
void        HTCRecvCompleteHandler(void *Context, HTC_PACKET *pPacket);
A_STATUS    HTCRecvMessagePendingHandler(void *Context, A_UINT32 MsgLookAheads[], int NumLookAheads, A_BOOL *pAsyncProc, int *pNumPktsFetched);
void        HTCProcessCreditRpt(HTC_TARGET *target, HTC_CREDIT_REPORT *pRpt, int NumEntries, HTC_ENDPOINT_ID FromEndpoint);
A_STATUS    HTCSendSetupComplete(HTC_TARGET *target);
void        HTCFlushRecvBuffers(HTC_TARGET *target);
void        HTCFlushSendPkts(HTC_TARGET *target);

#ifdef ATH_DEBUG_MODULE
void        DumpCreditDist(HTC_ENDPOINT_CREDIT_DIST *pEPDist);
void        DumpCreditDistStates(HTC_TARGET *target);
void 		DebugDumpBytes(A_UCHAR *buffer, A_UINT16 length, char *pDescription);
#endif

static INLINE HTC_PACKET *HTC_ALLOC_CONTROL_TX(HTC_TARGET *target) {
    HTC_PACKET *pPacket = HTCAllocControlBuffer(target,&target->ControlBufferTXFreeList);
    if (pPacket != NULL) {
            /* set payload pointer area with some headroom */
        pPacket->pBuffer = pPacket->pBufferStart + HTC_HDR_LENGTH;
    }
    return pPacket;
}

#define HTC_FREE_CONTROL_TX(t,p) HTCFreeControlBuffer((t),(p),&(t)->ControlBufferTXFreeList)
#define HTC_ALLOC_CONTROL_RX(t)  HTCAllocControlBuffer((t),&(t)->ControlBufferRXFreeList)
#define HTC_FREE_CONTROL_RX(t,p) \
{                                                                \
    HTC_PACKET_RESET_RX(p);                                      \
    HTCFreeControlBuffer((t),(p),&(t)->ControlBufferRXFreeList); \
}

#define HTC_PREPARE_SEND_PKT(pP,sendflags,ctrl0,ctrl1)       \
{                                                   \
    A_UINT8 *pHdrBuf;                               \
    (pP)->pBuffer -= HTC_HDR_LENGTH;                \
    pHdrBuf = (pP)->pBuffer;                        \
    A_SET_UINT16_FIELD(pHdrBuf,HTC_FRAME_HDR,PayloadLen,(A_UINT16)(pP)->ActualLength);  \
    A_SET_UINT8_FIELD(pHdrBuf,HTC_FRAME_HDR,Flags,(sendflags));                         \
    A_SET_UINT8_FIELD(pHdrBuf,HTC_FRAME_HDR,EndpointID, (A_UINT8)(pP)->Endpoint); \
    A_SET_UINT8_FIELD(pHdrBuf,HTC_FRAME_HDR,ControlBytes[0], (A_UINT8)(ctrl0));   \
    A_SET_UINT8_FIELD(pHdrBuf,HTC_FRAME_HDR,ControlBytes[1], (A_UINT8)(ctrl1));   \
}

#define HTC_UNPREPARE_SEND_PKT(pP)     \
    (pP)->pBuffer += HTC_HDR_LENGTH;   \
    
#ifdef __cplusplus
}
#endif

#endif /* _HTC_INTERNAL_H_ */
