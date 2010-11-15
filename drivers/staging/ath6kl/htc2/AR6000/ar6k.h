//------------------------------------------------------------------------------
// <copyright file="ar6k.h" company="Atheros">
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
// AR6K device layer that handles register level I/O
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef AR6K_H_
#define AR6K_H_

#include "hci_transport_api.h"
#include "../htc_debug.h"

#define AR6K_MAILBOXES 4

/* HTC runs over mailbox 0 */
#define HTC_MAILBOX          0

#define AR6K_TARGET_DEBUG_INTR_MASK     0x01

#define OTHER_INTS_ENABLED (INT_STATUS_ENABLE_ERROR_MASK |   \
                            INT_STATUS_ENABLE_CPU_MASK   |   \
                            INT_STATUS_ENABLE_COUNTER_MASK)


//#define MBOXHW_UNIT_TEST 1

#include "athstartpack.h"
typedef PREPACK struct _AR6K_IRQ_PROC_REGISTERS {
    A_UINT8                      host_int_status;
    A_UINT8                      cpu_int_status;
    A_UINT8                      error_int_status;
    A_UINT8                      counter_int_status;
    A_UINT8                      mbox_frame;
    A_UINT8                      rx_lookahead_valid;
    A_UINT8                      host_int_status2;
    A_UINT8                      gmbox_rx_avail;
    A_UINT32                     rx_lookahead[2];
    A_UINT32                     rx_gmbox_lookahead_alias[2];
} POSTPACK AR6K_IRQ_PROC_REGISTERS;

#define AR6K_IRQ_PROC_REGS_SIZE sizeof(AR6K_IRQ_PROC_REGISTERS)

typedef PREPACK struct _AR6K_IRQ_ENABLE_REGISTERS {
    A_UINT8                      int_status_enable;
    A_UINT8                      cpu_int_status_enable;
    A_UINT8                      error_status_enable;
    A_UINT8                      counter_int_status_enable;
} POSTPACK AR6K_IRQ_ENABLE_REGISTERS;

typedef PREPACK struct _AR6K_GMBOX_CTRL_REGISTERS {
    A_UINT8                      int_status_enable;
} POSTPACK AR6K_GMBOX_CTRL_REGISTERS;

#include "athendpack.h"

#define AR6K_IRQ_ENABLE_REGS_SIZE sizeof(AR6K_IRQ_ENABLE_REGISTERS)

#define AR6K_REG_IO_BUFFER_SIZE     32
#define AR6K_MAX_REG_IO_BUFFERS     8
#define FROM_DMA_BUFFER TRUE
#define TO_DMA_BUFFER   FALSE
#define AR6K_SCATTER_ENTRIES_PER_REQ            16
#define AR6K_MAX_TRANSFER_SIZE_PER_SCATTER      16*1024
#define AR6K_SCATTER_REQS                       4
#define AR6K_LEGACY_MAX_WRITE_LENGTH            2048

#ifndef A_CACHE_LINE_PAD
#define A_CACHE_LINE_PAD                        128
#endif
#define AR6K_MIN_SCATTER_ENTRIES_PER_REQ        2
#define AR6K_MIN_TRANSFER_SIZE_PER_SCATTER      4*1024

/* buffers for ASYNC I/O */
typedef struct AR6K_ASYNC_REG_IO_BUFFER {
    HTC_PACKET    HtcPacket;   /* we use an HTC packet as a wrapper for our async register-based I/O */
    A_UINT8       _Pad1[A_CACHE_LINE_PAD];
    A_UINT8       Buffer[AR6K_REG_IO_BUFFER_SIZE];  /* cache-line safe with pads around */
    A_UINT8       _Pad2[A_CACHE_LINE_PAD];
} AR6K_ASYNC_REG_IO_BUFFER;

typedef struct _AR6K_GMBOX_INFO { 
    void        *pProtocolContext;
    A_STATUS    (*pMessagePendingCallBack)(void *pContext, A_UINT8 LookAheadBytes[], int ValidBytes);
    A_STATUS    (*pCreditsPendingCallback)(void *pContext, int NumCredits,  A_BOOL CreditIRQEnabled);
    void        (*pTargetFailureCallback)(void *pContext, A_STATUS Status);
    void        (*pStateDumpCallback)(void *pContext);
    A_BOOL      CreditCountIRQEnabled;    
} AR6K_GMBOX_INFO; 

typedef struct _AR6K_DEVICE {
    A_MUTEX_T                   Lock;
    A_UINT8       _Pad1[A_CACHE_LINE_PAD];
    AR6K_IRQ_PROC_REGISTERS     IrqProcRegisters;   /* cache-line safe with pads around */
    A_UINT8       _Pad2[A_CACHE_LINE_PAD];
    AR6K_IRQ_ENABLE_REGISTERS   IrqEnableRegisters; /* cache-line safe with pads around */
    A_UINT8       _Pad3[A_CACHE_LINE_PAD];
    void                        *HIFDevice;
    A_UINT32                    BlockSize;
    A_UINT32                    BlockMask;
    HIF_DEVICE_MBOX_INFO        MailBoxInfo;
    HIF_PENDING_EVENTS_FUNC     GetPendingEventsFunc;
    void                        *HTCContext;
    HTC_PACKET_QUEUE            RegisterIOList;
    AR6K_ASYNC_REG_IO_BUFFER    RegIOBuffers[AR6K_MAX_REG_IO_BUFFERS];
    void                        (*TargetFailureCallback)(void *Context);
    A_STATUS                    (*MessagePendingCallback)(void *Context, 
                                                          A_UINT32 LookAheads[], 
                                                          int NumLookAheads, 
                                                          A_BOOL *pAsyncProc,
                                                          int *pNumPktsFetched);
    HIF_DEVICE_IRQ_PROCESSING_MODE  HifIRQProcessingMode;
    HIF_MASK_UNMASK_RECV_EVENT      HifMaskUmaskRecvEvent;
    A_BOOL                          HifAttached;
    HIF_DEVICE_IRQ_YIELD_PARAMS     HifIRQYieldParams;
    A_BOOL                          DSRCanYield;
    int                             CurrentDSRRecvCount;
    HIF_DEVICE_SCATTER_SUPPORT_INFO HifScatterInfo;
    DL_LIST                         ScatterReqHead; 
    A_BOOL                          ScatterIsVirtual;    
    int                             MaxRecvBundleSize;
    int                             MaxSendBundleSize;
    AR6K_GMBOX_INFO                 GMboxInfo;
    A_BOOL                          GMboxEnabled; 
    AR6K_GMBOX_CTRL_REGISTERS       GMboxControlRegisters;
    int                             RecheckIRQStatusCnt;
} AR6K_DEVICE;

#define LOCK_AR6K(p)      A_MUTEX_LOCK(&(p)->Lock);
#define UNLOCK_AR6K(p)    A_MUTEX_UNLOCK(&(p)->Lock);
#define REF_IRQ_STATUS_RECHECK(p) (p)->RecheckIRQStatusCnt = 1  /* note: no need to lock this, it only gets set */

A_STATUS DevSetup(AR6K_DEVICE *pDev);
void     DevCleanup(AR6K_DEVICE *pDev);
A_STATUS DevUnmaskInterrupts(AR6K_DEVICE *pDev);
A_STATUS DevMaskInterrupts(AR6K_DEVICE *pDev);
A_STATUS DevPollMboxMsgRecv(AR6K_DEVICE *pDev,
                            A_UINT32    *pLookAhead,
                            int          TimeoutMS);
A_STATUS DevRWCompletionHandler(void *context, A_STATUS status);
A_STATUS DevDsrHandler(void *context);
A_STATUS DevCheckPendingRecvMsgsAsync(void *context);
void     DevAsyncIrqProcessComplete(AR6K_DEVICE *pDev);
void     DevDumpRegisters(AR6K_DEVICE               *pDev,
                          AR6K_IRQ_PROC_REGISTERS   *pIrqProcRegs,
                          AR6K_IRQ_ENABLE_REGISTERS *pIrqEnableRegs);

#define DEV_STOP_RECV_ASYNC TRUE
#define DEV_STOP_RECV_SYNC  FALSE
#define DEV_ENABLE_RECV_ASYNC TRUE
#define DEV_ENABLE_RECV_SYNC  FALSE
A_STATUS DevStopRecv(AR6K_DEVICE *pDev, A_BOOL ASyncMode);
A_STATUS DevEnableRecv(AR6K_DEVICE *pDev, A_BOOL ASyncMode);
A_STATUS DevEnableInterrupts(AR6K_DEVICE *pDev);
A_STATUS DevDisableInterrupts(AR6K_DEVICE *pDev);
A_STATUS DevWaitForPendingRecv(AR6K_DEVICE *pDev,A_UINT32 TimeoutInMs,A_BOOL *pbIsRecvPending);

#define DEV_CALC_RECV_PADDED_LEN(pDev, length) (((length) + (pDev)->BlockMask) & (~((pDev)->BlockMask)))
#define DEV_CALC_SEND_PADDED_LEN(pDev, length) DEV_CALC_RECV_PADDED_LEN(pDev,length)
#define DEV_IS_LEN_BLOCK_ALIGNED(pDev, length) (((length) % (pDev)->BlockSize) == 0)

static INLINE A_STATUS DevSendPacket(AR6K_DEVICE *pDev, HTC_PACKET *pPacket, A_UINT32 SendLength) {
    A_UINT32 paddedLength;
    A_BOOL   sync = (pPacket->Completion == NULL) ? TRUE : FALSE;
    A_STATUS status;

       /* adjust the length to be a multiple of block size if appropriate */
    paddedLength = DEV_CALC_SEND_PADDED_LEN(pDev, SendLength);

#if 0                    
    if (paddedLength > pPacket->BufferLength) {
        A_ASSERT(FALSE);
        if (pPacket->Completion != NULL) {
            COMPLETE_HTC_PACKET(pPacket,A_EINVAL);
            return A_OK;
        }
        return A_EINVAL;
    }
#endif
    
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,
                ("DevSendPacket, Padded Length: %d Mbox:0x%X (mode:%s)\n",
                paddedLength,
                pDev->MailBoxInfo.MboxAddresses[HTC_MAILBOX],
                sync ? "SYNC" : "ASYNC"));

    status = HIFReadWrite(pDev->HIFDevice,
                          pDev->MailBoxInfo.MboxAddresses[HTC_MAILBOX],
                          pPacket->pBuffer,
                          paddedLength,     /* the padded length */
                          sync ? HIF_WR_SYNC_BLOCK_INC : HIF_WR_ASYNC_BLOCK_INC,
                          sync ? NULL : pPacket);  /* pass the packet as the context to the HIF request */

    if (sync) {
        pPacket->Status = status;
    } else {
        if (status == A_PENDING) {
            status = A_OK;    
        }    
    }

    return status;
}
                    
static INLINE A_STATUS DevRecvPacket(AR6K_DEVICE *pDev, HTC_PACKET *pPacket, A_UINT32 RecvLength) {
    A_UINT32 paddedLength;
    A_STATUS status;
    A_BOOL   sync = (pPacket->Completion == NULL) ? TRUE : FALSE;

        /* adjust the length to be a multiple of block size if appropriate */
    paddedLength = DEV_CALC_RECV_PADDED_LEN(pDev, RecvLength);
                    
    if (paddedLength > pPacket->BufferLength) {
        A_ASSERT(FALSE);
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                ("DevRecvPacket, Not enough space for padlen:%d recvlen:%d bufferlen:%d \n",
                    paddedLength,RecvLength,pPacket->BufferLength));
        if (pPacket->Completion != NULL) {
            COMPLETE_HTC_PACKET(pPacket,A_EINVAL);
            return A_OK;
        }
        return A_EINVAL;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,
                ("DevRecvPacket (0x%lX : hdr:0x%X) Padded Length: %d Mbox:0x%X (mode:%s)\n",
                (unsigned long)pPacket, pPacket->PktInfo.AsRx.ExpectedHdr,
                paddedLength,
                pDev->MailBoxInfo.MboxAddresses[HTC_MAILBOX],
                sync ? "SYNC" : "ASYNC"));

    status = HIFReadWrite(pDev->HIFDevice,
                          pDev->MailBoxInfo.MboxAddresses[HTC_MAILBOX],
                          pPacket->pBuffer,
                          paddedLength,
                          sync ? HIF_RD_SYNC_BLOCK_FIX : HIF_RD_ASYNC_BLOCK_FIX,
                          sync ? NULL : pPacket);  /* pass the packet as the context to the HIF request */

    if (sync) {
        pPacket->Status = status;
    }

    return status;
}

#define DEV_CHECK_RECV_YIELD(pDev) \
            ((pDev)->CurrentDSRRecvCount >= (pDev)->HifIRQYieldParams.RecvPacketYieldCount)
            
#define IS_DEV_IRQ_PROC_SYNC_MODE(pDev) (HIF_DEVICE_IRQ_SYNC_ONLY == (pDev)->HifIRQProcessingMode)
#define IS_DEV_IRQ_PROCESSING_ASYNC_ALLOWED(pDev) ((pDev)->HifIRQProcessingMode != HIF_DEVICE_IRQ_SYNC_ONLY)

/**************************************************/
/****** Scatter Function and Definitions
 * 
 *  
 */
  
A_STATUS DevCopyScatterListToFromDMABuffer(HIF_SCATTER_REQ *pReq, A_BOOL FromDMA);
    
    /* copy any READ data back into scatter list */        
#define DEV_FINISH_SCATTER_OPERATION(pR)                      \
    if (A_SUCCESS((pR)->CompletionStatus) &&                  \
        !((pR)->Request & HIF_WRITE) &&                       \
         ((pR)->ScatterMethod == HIF_SCATTER_DMA_BOUNCE)) {   \
         (pR)->CompletionStatus = DevCopyScatterListToFromDMABuffer((pR),FROM_DMA_BUFFER); \
    }
    
    /* copy any WRITE data to bounce buffer */
static INLINE A_STATUS DEV_PREPARE_SCATTER_OPERATION(HIF_SCATTER_REQ *pReq)  { 
    if ((pReq->Request & HIF_WRITE) && (pReq->ScatterMethod == HIF_SCATTER_DMA_BOUNCE)) {
        return DevCopyScatterListToFromDMABuffer(pReq,TO_DMA_BUFFER);    
    } else {
        return A_OK;    
    }
}
        
    
A_STATUS DevSetupMsgBundling(AR6K_DEVICE *pDev, int MaxMsgsPerTransfer);
                                  
#define DEV_GET_MAX_MSG_PER_BUNDLE(pDev)        (pDev)->HifScatterInfo.MaxScatterEntries
#define DEV_GET_MAX_BUNDLE_LENGTH(pDev)         (pDev)->HifScatterInfo.MaxTransferSizePerScatterReq
#define DEV_ALLOC_SCATTER_REQ(pDev)             \
    (pDev)->HifScatterInfo.pAllocateReqFunc((pDev)->ScatterIsVirtual ? (pDev) : (pDev)->HIFDevice)
    
#define DEV_FREE_SCATTER_REQ(pDev,pR)           \
    (pDev)->HifScatterInfo.pFreeReqFunc((pDev)->ScatterIsVirtual ? (pDev) : (pDev)->HIFDevice,(pR))

#define DEV_GET_MAX_BUNDLE_RECV_LENGTH(pDev)   (pDev)->MaxRecvBundleSize
#define DEV_GET_MAX_BUNDLE_SEND_LENGTH(pDev)   (pDev)->MaxSendBundleSize

#define DEV_SCATTER_READ  TRUE
#define DEV_SCATTER_WRITE FALSE
#define DEV_SCATTER_ASYNC TRUE
#define DEV_SCATTER_SYNC  FALSE
A_STATUS DevSubmitScatterRequest(AR6K_DEVICE *pDev, HIF_SCATTER_REQ *pScatterReq, A_BOOL Read, A_BOOL Async);

#ifdef MBOXHW_UNIT_TEST
A_STATUS DoMboxHWTest(AR6K_DEVICE *pDev);
#endif

    /* completely virtual */
typedef struct _DEV_SCATTER_DMA_VIRTUAL_INFO {
    A_UINT8            *pVirtDmaBuffer;      /* dma-able buffer - CPU accessible address */
    A_UINT8            DataArea[1];      /* start of data area */
} DEV_SCATTER_DMA_VIRTUAL_INFO;



void     DumpAR6KDevState(AR6K_DEVICE *pDev);

/**************************************************/
/****** GMBOX functions and definitions
 * 
 *  
 */

#ifdef ATH_AR6K_ENABLE_GMBOX

void     DevCleanupGMbox(AR6K_DEVICE *pDev);
A_STATUS DevSetupGMbox(AR6K_DEVICE *pDev);
A_STATUS DevCheckGMboxInterrupts(AR6K_DEVICE *pDev);
void     DevNotifyGMboxTargetFailure(AR6K_DEVICE *pDev);

#else

    /* compiled out */
#define DevCleanupGMbox(p)
#define DevCheckGMboxInterrupts(p) A_OK
#define DevNotifyGMboxTargetFailure(p)

static INLINE A_STATUS DevSetupGMbox(AR6K_DEVICE *pDev) {
    pDev->GMboxEnabled = FALSE;
    return A_OK;    
}

#endif

#ifdef ATH_AR6K_ENABLE_GMBOX

    /* GMBOX protocol modules must expose each of these internal APIs */
HCI_TRANSPORT_HANDLE GMboxAttachProtocol(AR6K_DEVICE *pDev, HCI_TRANSPORT_CONFIG_INFO *pInfo);
A_STATUS             GMboxProtocolInstall(AR6K_DEVICE *pDev);
void                 GMboxProtocolUninstall(AR6K_DEVICE *pDev);

    /* API used by GMBOX protocol modules */
AR6K_DEVICE  *HTCGetAR6KDevice(void *HTCHandle);
#define DEV_GMBOX_SET_PROTOCOL(pDev,recv_callback,credits_pending,failure,statedump,context) \
{                                                                  \
    (pDev)->GMboxInfo.pProtocolContext = (context);                \
    (pDev)->GMboxInfo.pMessagePendingCallBack = (recv_callback);   \
    (pDev)->GMboxInfo.pCreditsPendingCallback = (credits_pending); \
    (pDev)->GMboxInfo.pTargetFailureCallback = (failure);          \
    (pDev)->GMboxInfo.pStateDumpCallback = (statedump);            \
}

#define DEV_GMBOX_GET_PROTOCOL(pDev)  (pDev)->GMboxInfo.pProtocolContext

A_STATUS DevGMboxWrite(AR6K_DEVICE *pDev, HTC_PACKET *pPacket, A_UINT32 WriteLength);
A_STATUS DevGMboxRead(AR6K_DEVICE *pDev, HTC_PACKET *pPacket, A_UINT32 ReadLength);

#define PROC_IO_ASYNC TRUE
#define PROC_IO_SYNC  FALSE
typedef enum GMBOX_IRQ_ACTION_TYPE {
    GMBOX_ACTION_NONE = 0,
    GMBOX_DISABLE_ALL,
    GMBOX_ERRORS_IRQ_ENABLE,
    GMBOX_RECV_IRQ_ENABLE,
    GMBOX_RECV_IRQ_DISABLE,
    GMBOX_CREDIT_IRQ_ENABLE,
    GMBOX_CREDIT_IRQ_DISABLE,
} GMBOX_IRQ_ACTION_TYPE;

A_STATUS DevGMboxIRQAction(AR6K_DEVICE *pDev, GMBOX_IRQ_ACTION_TYPE, A_BOOL AsyncMode);
A_STATUS DevGMboxReadCreditCounter(AR6K_DEVICE *pDev, A_BOOL AsyncMode, int *pCredits);
A_STATUS DevGMboxReadCreditSize(AR6K_DEVICE *pDev, int *pCreditSize);
A_STATUS DevGMboxRecvLookAheadPeek(AR6K_DEVICE *pDev, A_UINT8 *pLookAheadBuffer, int *pLookAheadBytes);
A_STATUS DevGMboxSetTargetInterrupt(AR6K_DEVICE *pDev, int SignalNumber, int AckTimeoutMS);

#endif

#endif /*AR6K_H_*/
