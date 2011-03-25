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
PREPACK struct ar6k_irq_proc_registers {
    u8 host_int_status;
    u8 cpu_int_status;
    u8 error_int_status;
    u8 counter_int_status;
    u8 mbox_frame;
    u8 rx_lookahead_valid;
    u8 host_int_status2;
    u8 gmbox_rx_avail;
    u32 rx_lookahead[2];
    u32 rx_gmbox_lookahead_alias[2];
} POSTPACK;

#define AR6K_IRQ_PROC_REGS_SIZE sizeof(struct ar6k_irq_proc_registers)

PREPACK struct ar6k_irq_enable_registers {
    u8 int_status_enable;
    u8 cpu_int_status_enable;
    u8 error_status_enable;
    u8 counter_int_status_enable;
} POSTPACK;

PREPACK struct ar6k_gmbox_ctrl_registers {
    u8 int_status_enable;
} POSTPACK;

#include "athendpack.h"

#define AR6K_IRQ_ENABLE_REGS_SIZE sizeof(struct ar6k_irq_enable_registers)

#define AR6K_REG_IO_BUFFER_SIZE     32
#define AR6K_MAX_REG_IO_BUFFERS     8
#define FROM_DMA_BUFFER true
#define TO_DMA_BUFFER   false
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
struct ar6k_async_reg_io_buffer {
    struct htc_packet    HtcPacket;   /* we use an HTC packet as a wrapper for our async register-based I/O */
    u8 _Pad1[A_CACHE_LINE_PAD];
    u8 Buffer[AR6K_REG_IO_BUFFER_SIZE];  /* cache-line safe with pads around */
    u8 _Pad2[A_CACHE_LINE_PAD];
};

struct ar6k_gmbox_info { 
    void        *pProtocolContext;
    int    (*pMessagePendingCallBack)(void *pContext, u8 LookAheadBytes[], int ValidBytes);
    int    (*pCreditsPendingCallback)(void *pContext, int NumCredits,  bool CreditIRQEnabled);
    void        (*pTargetFailureCallback)(void *pContext, int Status);
    void        (*pStateDumpCallback)(void *pContext);
    bool      CreditCountIRQEnabled;
}; 

struct ar6k_device {
    A_MUTEX_T                   Lock;
    u8 _Pad1[A_CACHE_LINE_PAD];
    struct ar6k_irq_proc_registers     IrqProcRegisters;   /* cache-line safe with pads around */
    u8 _Pad2[A_CACHE_LINE_PAD];
    struct ar6k_irq_enable_registers   IrqEnableRegisters; /* cache-line safe with pads around */
    u8 _Pad3[A_CACHE_LINE_PAD];
    void                        *HIFDevice;
    u32 BlockSize;
    u32 BlockMask;
    struct hif_device_mbox_info        MailBoxInfo;
    HIF_PENDING_EVENTS_FUNC     GetPendingEventsFunc;
    void                        *HTCContext;
    struct htc_packet_queue            RegisterIOList;
    struct ar6k_async_reg_io_buffer    RegIOBuffers[AR6K_MAX_REG_IO_BUFFERS];
    void                        (*TargetFailureCallback)(void *Context);
    int                    (*MessagePendingCallback)(void *Context,
                                                          u32 LookAheads[],
                                                          int NumLookAheads, 
                                                          bool *pAsyncProc,
                                                          int *pNumPktsFetched);
    HIF_DEVICE_IRQ_PROCESSING_MODE  HifIRQProcessingMode;
    HIF_MASK_UNMASK_RECV_EVENT      HifMaskUmaskRecvEvent;
    bool                          HifAttached;
    struct hif_device_irq_yield_params     HifIRQYieldParams;
    bool                          DSRCanYield;
    int                             CurrentDSRRecvCount;
    struct hif_device_scatter_support_info HifScatterInfo;
    struct dl_list                         ScatterReqHead; 
    bool                          ScatterIsVirtual;
    int                             MaxRecvBundleSize;
    int                             MaxSendBundleSize;
    struct ar6k_gmbox_info                 GMboxInfo;
    bool                          GMboxEnabled;
    struct ar6k_gmbox_ctrl_registers       GMboxControlRegisters;
    int                             RecheckIRQStatusCnt;
};

#define LOCK_AR6K(p)      A_MUTEX_LOCK(&(p)->Lock);
#define UNLOCK_AR6K(p)    A_MUTEX_UNLOCK(&(p)->Lock);
#define REF_IRQ_STATUS_RECHECK(p) (p)->RecheckIRQStatusCnt = 1  /* note: no need to lock this, it only gets set */

int DevSetup(struct ar6k_device *pDev);
void     DevCleanup(struct ar6k_device *pDev);
int DevUnmaskInterrupts(struct ar6k_device *pDev);
int DevMaskInterrupts(struct ar6k_device *pDev);
int DevPollMboxMsgRecv(struct ar6k_device *pDev,
                            u32 *pLookAhead,
                            int          TimeoutMS);
int DevRWCompletionHandler(void *context, int status);
int DevDsrHandler(void *context);
int DevCheckPendingRecvMsgsAsync(void *context);
void     DevAsyncIrqProcessComplete(struct ar6k_device *pDev);
void     DevDumpRegisters(struct ar6k_device               *pDev,
                          struct ar6k_irq_proc_registers   *pIrqProcRegs,
                          struct ar6k_irq_enable_registers *pIrqEnableRegs);

#define DEV_STOP_RECV_ASYNC true
#define DEV_STOP_RECV_SYNC  false
#define DEV_ENABLE_RECV_ASYNC true
#define DEV_ENABLE_RECV_SYNC  false
int DevStopRecv(struct ar6k_device *pDev, bool ASyncMode);
int DevEnableRecv(struct ar6k_device *pDev, bool ASyncMode);
int DevEnableInterrupts(struct ar6k_device *pDev);
int DevDisableInterrupts(struct ar6k_device *pDev);
int DevWaitForPendingRecv(struct ar6k_device *pDev,u32 TimeoutInMs,bool *pbIsRecvPending);

#define DEV_CALC_RECV_PADDED_LEN(pDev, length) (((length) + (pDev)->BlockMask) & (~((pDev)->BlockMask)))
#define DEV_CALC_SEND_PADDED_LEN(pDev, length) DEV_CALC_RECV_PADDED_LEN(pDev,length)
#define DEV_IS_LEN_BLOCK_ALIGNED(pDev, length) (((length) % (pDev)->BlockSize) == 0)

static INLINE int DevSendPacket(struct ar6k_device *pDev, struct htc_packet *pPacket, u32 SendLength) {
    u32 paddedLength;
    bool   sync = (pPacket->Completion == NULL) ? true : false;
    int status;

       /* adjust the length to be a multiple of block size if appropriate */
    paddedLength = DEV_CALC_SEND_PADDED_LEN(pDev, SendLength);

#if 0                    
    if (paddedLength > pPacket->BufferLength) {
        A_ASSERT(false);
        if (pPacket->Completion != NULL) {
            COMPLETE_HTC_PACKET(pPacket,A_EINVAL);
            return 0;
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
            status = 0;
        }    
    }

    return status;
}
                    
static INLINE int DevRecvPacket(struct ar6k_device *pDev, struct htc_packet *pPacket, u32 RecvLength) {
    u32 paddedLength;
    int status;
    bool   sync = (pPacket->Completion == NULL) ? true : false;

        /* adjust the length to be a multiple of block size if appropriate */
    paddedLength = DEV_CALC_RECV_PADDED_LEN(pDev, RecvLength);
                    
    if (paddedLength > pPacket->BufferLength) {
        A_ASSERT(false);
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                ("DevRecvPacket, Not enough space for padlen:%d recvlen:%d bufferlen:%d \n",
                    paddedLength,RecvLength,pPacket->BufferLength));
        if (pPacket->Completion != NULL) {
            COMPLETE_HTC_PACKET(pPacket,A_EINVAL);
            return 0;
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
  
int DevCopyScatterListToFromDMABuffer(struct hif_scatter_req *pReq, bool FromDMA);
    
    /* copy any READ data back into scatter list */        
#define DEV_FINISH_SCATTER_OPERATION(pR)				\
do {									\
	if (!((pR)->CompletionStatus) &&				\
	    !((pR)->Request & HIF_WRITE) &&				\
	    ((pR)->ScatterMethod == HIF_SCATTER_DMA_BOUNCE)) {		\
		(pR)->CompletionStatus =				\
			DevCopyScatterListToFromDMABuffer((pR),		\
							  FROM_DMA_BUFFER); \
	}								\
} while (0)
    
    /* copy any WRITE data to bounce buffer */
static INLINE int DEV_PREPARE_SCATTER_OPERATION(struct hif_scatter_req *pReq)  {
    if ((pReq->Request & HIF_WRITE) && (pReq->ScatterMethod == HIF_SCATTER_DMA_BOUNCE)) {
        return DevCopyScatterListToFromDMABuffer(pReq,TO_DMA_BUFFER);    
    } else {
        return 0;
    }
}
        
    
int DevSetupMsgBundling(struct ar6k_device *pDev, int MaxMsgsPerTransfer);

int DevCleanupMsgBundling(struct ar6k_device *pDev);
                                  
#define DEV_GET_MAX_MSG_PER_BUNDLE(pDev)        (pDev)->HifScatterInfo.MaxScatterEntries
#define DEV_GET_MAX_BUNDLE_LENGTH(pDev)         (pDev)->HifScatterInfo.MaxTransferSizePerScatterReq
#define DEV_ALLOC_SCATTER_REQ(pDev)             \
    (pDev)->HifScatterInfo.pAllocateReqFunc((pDev)->ScatterIsVirtual ? (pDev) : (pDev)->HIFDevice)
    
#define DEV_FREE_SCATTER_REQ(pDev,pR)           \
    (pDev)->HifScatterInfo.pFreeReqFunc((pDev)->ScatterIsVirtual ? (pDev) : (pDev)->HIFDevice,(pR))

#define DEV_GET_MAX_BUNDLE_RECV_LENGTH(pDev)   (pDev)->MaxRecvBundleSize
#define DEV_GET_MAX_BUNDLE_SEND_LENGTH(pDev)   (pDev)->MaxSendBundleSize

#define DEV_SCATTER_READ  true
#define DEV_SCATTER_WRITE false
#define DEV_SCATTER_ASYNC true
#define DEV_SCATTER_SYNC  false
int DevSubmitScatterRequest(struct ar6k_device *pDev, struct hif_scatter_req *pScatterReq, bool Read, bool Async);

#ifdef MBOXHW_UNIT_TEST
int DoMboxHWTest(struct ar6k_device *pDev);
#endif

    /* completely virtual */
struct dev_scatter_dma_virtual_info {
    u8 *pVirtDmaBuffer;      /* dma-able buffer - CPU accessible address */
    u8 DataArea[1];      /* start of data area */
};



void     DumpAR6KDevState(struct ar6k_device *pDev);

/**************************************************/
/****** GMBOX functions and definitions
 * 
 *  
 */

#ifdef ATH_AR6K_ENABLE_GMBOX

void     DevCleanupGMbox(struct ar6k_device *pDev);
int DevSetupGMbox(struct ar6k_device *pDev);
int DevCheckGMboxInterrupts(struct ar6k_device *pDev);
void     DevNotifyGMboxTargetFailure(struct ar6k_device *pDev);

#else

    /* compiled out */
#define DevCleanupGMbox(p)
#define DevCheckGMboxInterrupts(p) 0
#define DevNotifyGMboxTargetFailure(p)

static INLINE int DevSetupGMbox(struct ar6k_device *pDev) {
    pDev->GMboxEnabled = false;
    return 0;
}

#endif

#ifdef ATH_AR6K_ENABLE_GMBOX

    /* GMBOX protocol modules must expose each of these internal APIs */
HCI_TRANSPORT_HANDLE GMboxAttachProtocol(struct ar6k_device *pDev, struct hci_transport_config_info *pInfo);
int             GMboxProtocolInstall(struct ar6k_device *pDev);
void                 GMboxProtocolUninstall(struct ar6k_device *pDev);

    /* API used by GMBOX protocol modules */
struct ar6k_device  *HTCGetAR6KDevice(void *HTCHandle);
#define DEV_GMBOX_SET_PROTOCOL(pDev,recv_callback,credits_pending,failure,statedump,context) \
{                                                                  \
    (pDev)->GMboxInfo.pProtocolContext = (context);                \
    (pDev)->GMboxInfo.pMessagePendingCallBack = (recv_callback);   \
    (pDev)->GMboxInfo.pCreditsPendingCallback = (credits_pending); \
    (pDev)->GMboxInfo.pTargetFailureCallback = (failure);          \
    (pDev)->GMboxInfo.pStateDumpCallback = (statedump);            \
}

#define DEV_GMBOX_GET_PROTOCOL(pDev)  (pDev)->GMboxInfo.pProtocolContext

int DevGMboxWrite(struct ar6k_device *pDev, struct htc_packet *pPacket, u32 WriteLength);
int DevGMboxRead(struct ar6k_device *pDev, struct htc_packet *pPacket, u32 ReadLength);

#define PROC_IO_ASYNC true
#define PROC_IO_SYNC  false
typedef enum GMBOX_IRQ_ACTION_TYPE {
    GMBOX_ACTION_NONE = 0,
    GMBOX_DISABLE_ALL,
    GMBOX_ERRORS_IRQ_ENABLE,
    GMBOX_RECV_IRQ_ENABLE,
    GMBOX_RECV_IRQ_DISABLE,
    GMBOX_CREDIT_IRQ_ENABLE,
    GMBOX_CREDIT_IRQ_DISABLE,
} GMBOX_IRQ_ACTION_TYPE;

int DevGMboxIRQAction(struct ar6k_device *pDev, GMBOX_IRQ_ACTION_TYPE, bool AsyncMode);
int DevGMboxReadCreditCounter(struct ar6k_device *pDev, bool AsyncMode, int *pCredits);
int DevGMboxReadCreditSize(struct ar6k_device *pDev, int *pCreditSize);
int DevGMboxRecvLookAheadPeek(struct ar6k_device *pDev, u8 *pLookAheadBuffer, int *pLookAheadBytes);
int DevGMboxSetTargetInterrupt(struct ar6k_device *pDev, int SignalNumber, int AckTimeoutMS);

#endif

#endif /*AR6K_H_*/
